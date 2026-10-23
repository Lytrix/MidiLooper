#!/usr/bin/env python3
"""3.15f route oracle: NOTE_EDIT while playing, observe the positioned C handoff.

Requires the qualification (window-lane) image and a pristine sparse seed, e.g.

  .venv/bin/python scripts/host_midi_hitl.py run --preset base --record-only --edit-record-fixture \
      --track 5 --loop-slot 1 --midi-channel 5 \
      --build-env teensy41-capture-serial-window-qualification \
      --out-dir <seed> --no-boot-reset --clock-source host --tempo-bpm 120

The probe commits any open NOTE_EDIT session, dumps the seed, starts the host clock, enters NOTE_EDIT at the
target note's phase, selects the note's actual nav slot and moves it with the coarse fader while the transport
runs, keeps playing past the positioned handoff, then stops and drains the lane's retained rows. It departs
NOTE_EDIT so the edit commits, dumps the content, and verifies:

  * the target note moved and no other persisted note did (content oracle);
  * a preview frame (``playback_read`` origin=Preview) is served only at/after the positioned handoff, and the
    handoff is the earliest loop boundary whose absolute lead clears the DEC-136 preparation bound -- i.e.
    the lane did not adopt at the first boundary (route oracle);
  * the recorded MIDI stream switches from the committed note to the edited note at that boundary (audibility).

No firmware change: the lane's own rows and the emitted MIDI are the evidence.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import tempfile
import threading
import time
from pathlib import Path

import mido

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
from hitl.control_constants import TICKS_PER_16TH_STEP  # noqa: E402
from hitl.legacy_edit_baseline import (  # noqa: E402
    FADER_COARSE_CHANNEL_1BASED,
    FADER_SELECT_CHANNEL_1BASED,
    RecordLayout,
    _build_select_navigation_slots,
    _note_index_to_pitch,
    _send_program_change,
)
from hitl.select_mapping import pitchbend_for_step_and_stack  # noqa: E402
from hitl.serial_collector import SerialCaptureCollector  # noqa: E402
from hitl_note_edit_min_probe import _dump_effective_notes, _press  # noqa: E402

EDIT_BUTTON_NOTE = 38
TRACK_SELECT_NOTE_BASE = 60
CONTROL_CHANNEL_1BASED = 16
DEFAULT_LOOP_LENGTH = 1536
#: DEC-136's minimum coverage-preparation lead (the bound C must respect).
DEC136_MIN_LEAD = 1792
CLOCKS_PER_SECOND = 48.0  # 120 BPM, 24 PPQN
TICKS_PER_CLOCK = 8  # 1,536-tick loop / 192 clocks per two bars


class ClockSender:
    """Steady host clock; the only writer of clock messages, serialized with the gesture sends."""

    def __init__(self, out: mido.ports.BaseOutput, lock: threading.Lock) -> None:
        self._out = out
        self._lock = lock
        self._stop = threading.Event()
        self.clocks = 0
        self._thread = threading.Thread(target=self._run, daemon=True)

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(timeout=2.0)

    def _run(self) -> None:
        next_at = time.monotonic()
        while not self._stop.is_set():
            with self._lock:
                self._out.send(mido.Message("clock"))
            self.clocks += 1
            next_at += 1.0 / CLOCKS_PER_SECOND
            delay = next_at - time.monotonic()
            if delay > 0:
                time.sleep(delay)


class MidiRecorder:
    """Records the device's USB-MIDI output with host monotonic arrival times."""

    def __init__(self, port_name: str) -> None:
        self._port = mido.open_input(port_name)
        self._events: list[tuple[float, mido.Message]] = []
        self._lock = threading.Lock()
        self._thread = threading.Thread(target=self._run, daemon=True)

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        try:
            self._port.close()
        except Exception:
            pass
        self._thread.join(timeout=2.0)

    def events(self) -> list[tuple[float, mido.Message]]:
        with self._lock:
            return list(self._events)

    def _run(self) -> None:
        try:
            for message in self._port:
                with self._lock:
                    self._events.append((time.monotonic(), message))
        except Exception:
            return


def _tap(out: mido.ports.BaseOutput, lock: threading.Lock, note: int,
         channel_1based: int = CONTROL_CHANNEL_1BASED, down_ms: int = 80) -> None:
    """A control press that does not hold the clock lock across its sleeps."""
    with lock:
        out.send(mido.Message("note_on", channel=channel_1based - 1, note=note, velocity=127))
    time.sleep(down_ms / 1000.0)
    with lock:
        out.send(mido.Message("note_off", channel=channel_1based - 1, note=note, velocity=0))
    time.sleep(0.15)


def _wait_mode(collector: SerialCaptureCollector, needle: str, timeout_s: float = 2.5) -> bool:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        index = collector.find_line(needle)
        if index is not None:
            return True
        time.sleep(0.05)
    return False


def _last_edit_mode(collector: SerialCaptureCollector) -> str | None:
    for line in reversed(collector.snapshot()):
        if "Edit session: NOTE_EDIT" in line:
            return "NOTE_EDIT"
        if "Edit session: LOOP_EDIT" in line:
            return "LOOP_EDIT"
    return None


def _ensure_loop_edit(collector: SerialCaptureCollector, out, lock) -> bool:
    for _ in range(4):
        with lock:
            _press(out, EDIT_BUTTON_NOTE, channel_1based=CONTROL_CHANNEL_1BASED)
        time.sleep(0.4)
        mode = _last_edit_mode(collector)
        if mode == "LOOP_EDIT":
            return True
    return False


def _row_us(line: str) -> int | None:
    match = re.search(r"#CAP,(\d+),", line)
    if match:
        return int(match.group(1))
    match = re.match(r"\[(\d+)\.(\d+)\]", line)
    if match:
        return int(match.group(1)) * 1_000_000 + int((match.group(2) + "000000")[:6])
    return None


READ_ROW = re.compile(
    r"#CAP,(\d+),DIAG,playback_read,1,(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+)")
GATE_ROW = re.compile(r"#CAP,(\d+),DIAG,win_gate,([^\r\n]*)")
MOVE_ROW = re.compile(r"Note movement with overlap handling: from=(\d+) to=(\d+)")


def _select_pitchbend(layout: RecordLayout, slot_index: int) -> int:
    slot = layout.nav_slots[slot_index]
    step = slot.rel_tick // TICKS_PER_16TH_STEP
    step_slots = sorted(
        (i for i, candidate in enumerate(layout.nav_slots)
         if candidate.note_idx >= 0 and candidate.rel_tick // TICKS_PER_16TH_STEP == step),
        key=lambda i: (layout.nav_slots[i].rel_tick,
                       layout.note_index_to_pitch.get(layout.nav_slots[i].note_idx, 0)),
    )
    stacked_index, stacked_count = (step_slots.index(slot_index), len(step_slots)) if slot.note_idx >= 0 else (0, 1)
    return pitchbend_for_step_and_stack(step, layout.loop_length, stacked_index, stacked_count)


def _reachable(step: int, loop_length: int) -> bool:
    try:
        pitchbend_for_step_and_stack(step, loop_length, 0, 1)
        return True
    except ValueError:
        return False


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--midi-out", default="Teensy MIDI")
    parser.add_argument("--midi-in", default="Teensy MIDI")
    parser.add_argument("--port", default=None)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--track", type=int, default=5)
    parser.add_argument("--slot", type=int, default=1)
    parser.add_argument("--pitch", type=int, default=64)
    parser.add_argument("--previous-focus-pitch", type=int, default=55)
    parser.add_argument("--loop-length", type=int, default=DEFAULT_LOOP_LENGTH)
    parser.add_argument("--entry-tick", type=int, default=200,
                        help="tick at which NOTE_EDIT is entered (the target note's phase)")
    parser.add_argument("--clocks-after-edit", type=int, default=768,
                        help="keep playing this many clocks (768 = four two-bar loops)")
    parser.add_argument("--start-settle-clocks", type=int, default=12,
                        help="clocks after start before NOTE_EDIT opens (the transport must be flagged playing)")
    parser.add_argument("--to-step", type=int, default=None)
    parser.add_argument("--out-dir", type=Path, default=None)
    args = parser.parse_args(argv)

    port = args.port
    if port is None:
        ports = sorted(Path("/dev").glob("cu.usbmodem*"))
        if not ports:
            print("[handoff-probe] FAILED: no /dev/cu.usbmodem*")
            return 1
        port = str(ports[0])
    out_dir = args.out_dir or Path(tempfile.mkdtemp(prefix="note-edit-handoff-"))
    out_dir.mkdir(parents=True, exist_ok=True)

    out = mido.open_output(args.midi_out)
    lock = threading.Lock()

    # Depart any open session so the pre-dump is the persisted seed.
    collector = SerialCaptureCollector(port, args.baud)
    collector.start()
    try:
        if not _ensure_loop_edit(collector, out, lock):
            print("[handoff-probe] FAILED: could not depart a previously open NOTE_EDIT session")
            return 1
    finally:
        collector.stop()
    pre_rows = _dump_effective_notes(out_dir / "pre", track=args.track, slot=args.slot)
    print(f"[handoff-probe] seed notes={pre_rows}")
    if not pre_rows:
        print("[handoff-probe] FAILED: empty seed")
        return 1

    # The actual nav layout, from the freshly captured seed.
    revt = list(pre_rows)
    slots = _build_select_navigation_slots(revt, loop_length=args.loop_length, loop_start=0)
    layout = RecordLayout(loop_start=0, loop_length=args.loop_length, step_to_tick={}, nav_slots=slots,
                          note_index_to_pitch=_note_index_to_pitch(revt))
    target_index = next((i for i, (_t, p) in enumerate(revt) if p == args.pitch), None)
    if target_index is None:
        print(f"[handoff-probe] FAILED: pitch {args.pitch} not in seed")
        return 1
    target_tick, target_pitch = revt[target_index]
    slot_index = next(i for i, s in enumerate(slots) if s.note_idx == target_index)
    src_step = target_tick // TICKS_PER_16TH_STEP
    empty_steps = sorted({s.rel_tick // TICKS_PER_16TH_STEP for s in slots if s.note_idx < 0})
    options = [s for s in empty_steps if s != src_step and s != 0 and _reachable(s, args.loop_length)]
    if not options:
        print("[handoff-probe] FAILED: no reachable empty destination step")
        return 1
    dst_step = args.to_step if args.to_step is not None else max(options, key=lambda s: abs(s - src_step))
    expected_dst_tick = target_tick + (dst_step - src_step) * TICKS_PER_16TH_STEP
    select_pb = _select_pitchbend(layout, slot_index)
    coarse_pb = pitchbend_for_step_and_stack(dst_step, args.loop_length, 0, 1)
    print(f"[handoff-probe] track {args.track} target {target_pitch}@{target_tick} slot {slot_index} "
          f"step {src_step}->{dst_step} expected_dst {expected_dst_tick} select_pb={select_pb} coarse_pb={coarse_pb}")

    collector = SerialCaptureCollector(port, args.baud)
    collector.start()
    recorder = MidiRecorder(args.midi_in)
    recorder.start()
    clock: ClockSender | None = None
    demand_host = None
    entry_host = None
    entry_clocks = 0
    demand_clocks = 0
    play_start_host = None
    try:
        _tap(out, lock, TRACK_SELECT_NOTE_BASE + (args.track - 1))
        time.sleep(0.4)
        # Position the stopped playhead/bracket at the target phase first: a fresh NOTE_EDIT session otherwise restores
        # the previous session's bracket (measured: 392 while the playhead was at 200).
        with lock:
            out.send(mido.Message("start"))
        for _ in range(max(1, args.entry_tick // TICKS_PER_CLOCK)):
            with lock:
                out.send(mido.Message("clock"))
            time.sleep(1.0 / CLOCKS_PER_SECOND)
        with lock:
            out.send(mido.Message("stop"))
        time.sleep(0.35)
        with lock:
            play_start_host = time.monotonic()
            out.send(mido.Message("start"))
        clock = ClockSender(out, lock)
        clock.start()
        settle_deadline = time.monotonic() + 2.0
        while clock.clocks < args.start_settle_clocks and time.monotonic() < settle_deadline:
            time.sleep(0.005)
        entry_clocks = clock.clocks
        entry_host = time.monotonic()
        _tap(out, lock, EDIT_BUTTON_NOTE)
        if not _wait_mode(collector, "Edit session: NOTE_EDIT", 2.5):
            print("[handoff-probe] FAILED: NOTE_EDIT did not open while playing")
            return 1
        print(f"[handoff-probe] NOTE_EDIT opened at clock {entry_clocks} (~tick {entry_clocks * TICKS_PER_CLOCK})")
        time.sleep(1.1)  # fader feedback-ignore period
        focus_tick = None
        for line in reversed(collector.snapshot()):
            if "EditSelectNoteState: Found and selected note" in line:
                focus = re.search(r"at tick (\d+)", line)
                if focus:
                    focus_tick = int(focus.group(1))
                break
        print(f"[handoff-probe] entry focus tick={focus_tick} (intended {target_tick})")
        if focus_tick is not None and focus_tick != target_tick:
            print("[handoff-probe] FAILED: the entry focus is not the intended note; refusing the coarse move")
            return 1
        # Do NOT send the select fader here: the nav neighbourhood follows the live playhead, and a select sent after
        # the settle re-targets a later step. The entry's own bracket selected the note at the target phase.
        with lock:
            _send_program_change(out, FADER_COARSE_CHANNEL_1BASED, 2)
            out.send(mido.Message("pitchwheel", channel=FADER_COARSE_CHANNEL_1BASED - 1, pitch=coarse_pb))
        demand_host = time.monotonic()
        demand_clocks = clock.clocks
        print(f"[handoff-probe] edit sent at clock {demand_clocks} (~tick {demand_clocks * TICKS_PER_CLOCK})")
        target_total = clock.clocks + args.clocks_after_edit
        deadline = time.monotonic() + (args.clocks_after_edit / CLOCKS_PER_SECOND) + 10.0
        while clock.clocks < target_total and time.monotonic() < deadline:
            time.sleep(0.01)
        clock.stop()
        clock = None
        with lock:
            out.send(mido.Message("stop"))
        print("[handoff-probe] transport stopped; draining the lane's retained rows")
        collector.drain_until_idle(idle_ms=900, max_drain_ms=15000)
        # Depart NOTE_EDIT so the pending edit commits before the content dump.
        _tap(out, lock, EDIT_BUTTON_NOTE)
        time.sleep(0.4)
        print(f"[handoff-probe] after-depart mode={_last_edit_mode(collector)}")
        collector.drain_until_idle(idle_ms=700, max_drain_ms=6000)
    finally:
        if clock is not None:
            clock.stop()
        recorder.stop()
        lines = collector.snapshot()
        collector.stop()
        out.close()

    (out_dir / "run_serial.log").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"[handoff-probe] run serial lines={len(lines)} integrity={collector.capture_integrity()} "
          f"reason={collector.capture_integrity_reason()!r} error={collector.error()!r}")
    for marker in ("win_gate", "win_readiness", "win_frozen_readiness", "playback_build", "playback_read",
                   "playback_available", "win_gap", "Note movement", "select_apply", "EditSelectNoteState"):
        print(f"[handoff-probe] marker {marker}={sum(1 for line in lines if marker in line)}")
    events = recorder.events()
    origin = play_start_host if play_start_host is not None else (events[0][0] if events else time.monotonic())
    (out_dir / "midi.log").write_text(
        "\n".join(f"{stamp - origin:.6f} {message}" for stamp, message in events) + "\n",
        encoding="utf-8")

    post_rows = _dump_effective_notes(out_dir, track=args.track, slot=args.slot)
    print(f"[handoff-probe] post notes={post_rows}")
    changed = [row for row in pre_rows if row not in set(post_rows)]
    new_rows = [row for row in post_rows if row not in set(pre_rows)]
    prior_focus = [row for row in pre_rows if row[1] == args.previous_focus_pitch]
    prior_focus_kept = [row for row in prior_focus if row in set(post_rows)]

    read_rows = []
    for line in lines:
        match = READ_ROW.search(line)
        if not match:
            continue
        fields = list(map(int, match.groups()))
        # The wire rows are zero-based (track index / slot index); the probe's --track/--slot are the HITL 1-based
        # controls.
        if fields[1] != args.track - 1 or fields[2] != args.slot - 1:
            continue
        read_rows.append({
            "us": fields[0], "tick": fields[3], "generation": fields[4],
            "storage_generation": fields[5], "origin": fields[6], "start": fields[7],
            "length": fields[8], "period": fields[9], "phase": fields[10],
        })
    gate_rows = [match.group(2) for match in (GATE_ROW.search(line) for line in lines) if match]
    move = None
    for line in lines:
        match = MOVE_ROW.search(line)
        if match:
            move = (int(match.group(1)), int(match.group(2)), _row_us(line))
    preview_rows = [row for row in read_rows if row["origin"] == 1]
    first_preview = preview_rows[0] if preview_rows else None

    # Demand tick: map the movement's device time through the read rows' own (us, tick) relation.
    demand_tick = None
    if move is not None and move[2] is not None and read_rows:
        before = [r for r in read_rows if r["us"] <= move[2]]
        after = [r for r in read_rows if r["us"] >= move[2]]
        if before and after:
            lo, hi = before[-1], after[0]
            if hi["us"] != lo["us"]:
                demand_tick = lo["tick"] + round((hi["tick"] - lo["tick"]) * (move[2] - lo["us"]) / (hi["us"] - lo["us"]))
            else:
                demand_tick = hi["tick"]
        elif before:
            demand_tick = before[-1]["tick"]
        elif after:
            demand_tick = after[0]["tick"]
    entry_tick = entry_clocks * TICKS_PER_CLOCK if entry_clocks else args.entry_tick
    if demand_tick is None:
        demand_tick = demand_clocks * TICKS_PER_CLOCK if demand_clocks else entry_tick
    handoff_tick = first_preview["tick"] if first_preview else None
    handoff_boundary = (handoff_tick // args.loop_length) * args.loop_length if handoff_tick is not None else None
    expected_deadline = None
    if demand_tick is not None:
        expected_deadline = (demand_tick // args.loop_length) * args.loop_length
        while expected_deadline < demand_tick + DEC136_MIN_LEAD:
            expected_deadline += args.loop_length

    # Audibility from the emitted stream. The edited note (tick 1448) follows 55@1352 by 96 ticks (0.25 s);
    # the untouched committed note 64@200 follows 60@8 by 192 ticks (0.5 s). The other 64@872 follows 60@584
    # by 288 ticks (0.75 s), so the three are separable by their predecessor and gap.
    note_ons = [(stamp, m.note) for stamp, m in events
                if m.type == "note_on" and m.velocity > 0]
    edited_hits: list[float] = []
    committed_200_hits: list[float] = []
    for index, (stamp, note) in enumerate(note_ons):
        if note != target_pitch or index == 0:
            continue
        prev_stamp, prev_note = note_ons[index - 1]
        gap = stamp - prev_stamp
        if prev_note == 55 and 0.15 <= gap <= 0.38:
            edited_hits.append(stamp - origin)
        elif prev_note == 60 and 0.42 <= gap <= 0.60:
            committed_200_hits.append(stamp - origin)
    handoff_host = None
    if handoff_tick is not None and play_start_host is not None:
        handoff_host = (play_start_host + handoff_tick / 384.0) - origin
    edited_after = [t for t in edited_hits if handoff_host is None or t >= handoff_host]
    edited_before = [t for t in edited_hits if handoff_host is not None and t < handoff_host]
    committed_after = [t for t in committed_200_hits if handoff_host is None or t >= handoff_host]
    committed_before = [t for t in committed_200_hits if handoff_host is not None and t < handoff_host]

    summary = {
        "seed": pre_rows,
        "post": post_rows,
        "changed_rows": changed,
        "new_rows": new_rows,
        "previous_focus": {"rows": prior_focus, "kept": prior_focus_kept},
        "entry_tick": entry_tick,
        "movement": {"from": move[0], "to": move[1], "us": move[2]} if move else None,
        "demand_tick": demand_tick,
        "expected_deadline": expected_deadline,
        "first_preview_read": first_preview,
        "handoff_boundary": handoff_boundary,
        "latency_from_entry": None if handoff_tick is None else handoff_tick - entry_tick,
        "latency_from_demand": None if handoff_tick is None or demand_tick is None else handoff_tick - demand_tick,
        "win_gate_rows": gate_rows,
        "preview_read_rows": len(preview_rows),
        "committed_read_rows": len([r for r in read_rows if r["origin"] == 0]),
        "midi_edited_before": len(edited_before),
        "midi_edited_after": len(edited_after),
        "midi_committed_200_before": len(committed_before),
        "midi_committed_200_after": len(committed_after),
        "midi_note_ons": len(note_ons),
    }
    (out_dir / "handoff.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(f"[handoff-probe] summary={json.dumps({k: v for k, v in summary.items() if k not in ('seed', 'post', 'win_gate_rows')})}")

    ok_content = (changed == [(target_tick, target_pitch)] and new_rows == [(expected_dst_tick, target_pitch)]
                  and prior_focus == prior_focus_kept)
    ok_route = (first_preview is not None and handoff_boundary is not None
                and expected_deadline is not None and handoff_boundary == expected_deadline
                and summary["committed_read_rows"] > 0
                and any("win_" in row for row in gate_rows))
    ok_audible = (not edited_before and bool(edited_after) and bool(committed_before)
                  and not committed_after)
    print(f"[handoff-probe] content={ok_content} route={ok_route} audible={ok_audible} "
          f"(preview rows={len(preview_rows)}, committed rows={summary['committed_read_rows']}, "
          f"handoff={handoff_boundary}, expected=={expected_deadline}, "
          f"latency_entry={summary['latency_from_entry']}, latency_demand={summary['latency_from_demand']}, "
          f"edited before/after={len(edited_before)}/{len(edited_after)}, "
          f"committed200 before/after={len(committed_before)}/{len(committed_after)})")
    if ok_content and ok_route and ok_audible:
        print("[handoff-probe] PASS: the edit committed, the windowed preview was adopted at the positioned "
              "(not the first) loop boundary, and the audible stream switched there")
        return 0
    print("[handoff-probe] FAILED: content, route or audibility terms are not met (see handoff.json)")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
