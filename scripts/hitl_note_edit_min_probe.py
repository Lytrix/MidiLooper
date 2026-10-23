#!/usr/bin/env python3
"""Minimal NOTE_EDIT device oracle (nav-slot selection, fresh acknowledgement, commit on depart).

Proves on the device that NOTE_EDIT can identify a **persisted note at its actual tick** (including an off-48-tick
position) and move it with the coarse fader, without disturbing any other persisted note.

Selection is by the seed's **actual nav slot** (`SelectNavigation` layout, built from the freshly captured seed), not by
a hard-coded step tick. The playhead is positioned at the target note's tick first, because the session's select
navigation is a neighbourhood around it. The edit is sent only after the firmware's own select decision acknowledges
the intended tick:

  `#DBG select_apply selected_tick=<t> note_idx=<n> ... reason=<why>`

If selection resolves to another tick (e.g. `empty_step`) the leg fails before any fader edit. The destination is the
farthest reachable empty 16th step so the coarse delta clears the significant-movement threshold; the coarse move
preserves the note's intra-step offset. NOTE_EDIT is then departed, which commits the pending edit, and the persisted
content is dumped.

The previous persisted focus (an unrelated note, default pitch 55) must keep its tick; the target note must move to the
destination step.

Usage:
  .venv/bin/python scripts/hitl_note_edit_min_probe.py --pitch 64 [--to-step N] [--out-dir captures/...]

The seed should be the sparse edit fixture:
  host_midi_hitl.py run --preset base --record-only --edit-record-fixture --track 5 --loop-slot 1 \\
      --midi-channel 5 --build-env <env> --out-dir <seed> --no-boot-reset --clock-source host --tempo-bpm 120
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import mido
import serial

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
from hitl.control_constants import TICKS_PER_16TH_STEP  # noqa: E402
from hitl.select_mapping import pitchbend_for_step_and_stack  # noqa: E402
from hitl.legacy_edit_baseline import (  # noqa: E402
    RecordLayout,
    _build_select_navigation_slots,
    _fader1_select_nav_slot_index,
    _fader2_move_to_sixteenth_step,
    _note_index_to_pitch,
)

CONTROL_CHANNEL_1BASED = 16
SELECT_CHANNEL_1BASED = 16
COARSE_CHANNEL_1BASED = 14
EDIT_BUTTON_NOTE = 38
TRACK_SELECT_NOTE_BASE = 60


def _press(out: mido.ports.BaseOutput, note: int, *, channel_1based: int = CONTROL_CHANNEL_1BASED,
           down_ms: int = 80) -> None:
    out.send(mido.Message("note_on", channel=channel_1based - 1, note=note, velocity=127))
    time.sleep(down_ms / 1000.0)
    out.send(mido.Message("note_off", channel=channel_1based - 1, note=note, velocity=0))
    time.sleep(0.15)


def _read_edit_mode(ser: serial.Serial, timeout_s: float) -> str | None:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        line = ser.readline().decode("utf-8", errors="replace")
        if "Edit session: NOTE_EDIT" in line:
            return "NOTE_EDIT"
        if "Edit session: LOOP_EDIT" in line:
            return "LOOP_EDIT"
    return None


def _ensure_loop_edit(ser: serial.Serial, out: mido.ports.BaseOutput) -> bool:
    """Reach LOOP_EDIT, departing any open NOTE_EDIT session (which commits its pending actions)."""
    for _ in range(4):
        mode = _read_edit_mode(ser, 0.5)
        if mode == "LOOP_EDIT":
            return True
        _press(out, EDIT_BUTTON_NOTE, channel_1based=CONTROL_CHANNEL_1BASED)
        mode = _read_edit_mode(ser, 2.0)
        if mode == "LOOP_EDIT":
            return True
    return False


def _fresh_note_edit(ser: serial.Serial, out: mido.ports.BaseOutput) -> bool:
    """Enter NOTE_EDIT from LOOP_EDIT (a fresh session; no carry-over focus)."""
    _press(out, EDIT_BUTTON_NOTE, channel_1based=CONTROL_CHANNEL_1BASED)
    mode = _read_edit_mode(ser, 2.0)
    return mode == "NOTE_EDIT"


def _leave_note_edit(ser: serial.Serial, out: mido.ports.BaseOutput) -> bool:
    """Depart NOTE_EDIT back to LOOP_EDIT so pending edits commit before the content dump."""
    _press(out, EDIT_BUTTON_NOTE, channel_1based=CONTROL_CHANNEL_1BASED)
    mode = _read_edit_mode(ser, 2.5)
    return mode == "LOOP_EDIT"


def _drain(ser: serial.Serial, seconds: float = 0.35) -> None:
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        ser.readline()


# The firmware's own select decision (INFO, always emitted): the applied target's note index and bracket tick, plus
# the reason (`note_changed`/`unchanged_note`/`empty_step`/...). This is the fresh acknowledgement the leg requires.
_SELECT_DECISION = re.compile(
    r"#DBG select_apply selected_tick=(\d+) note_idx=(-?\d+) slot=(-?\d+) prior_slot=(-?\d+) "
    r"apply=(\d+) reason=(\w+)")


def _read_select_ack(ser: serial.Serial, timeout_s: float, target_index: int):
    """Return (note_idx, selected_tick, reason) for the last decision, preferring a target match."""
    deadline = time.monotonic() + timeout_s
    last = None
    while time.monotonic() < deadline:
        line = ser.readline().decode("utf-8", errors="replace")
        low = line.lower()
        if any(k in low for k in ("select", "fader", "#dbg", "editselect")):
            print("[note-edit-probe] serial:", line.strip())
        match = _SELECT_DECISION.search(line)
        if not match:
            continue
        decision = (int(match.group(2)), int(match.group(1)), match.group(6))
        last = decision
        if decision[0] == target_index:
            return decision
    return last


def _dump_effective_notes(out_dir: Path, *, track: int, slot: int) -> list[tuple[int, int]]:
    cmd = [
        str(REPO / ".venv/bin/python"),
        str(REPO / ".agents/skills/hitl-content-verification/scripts/dump_and_capture.py"),
        "--out-dir", str(out_dir),
        "--select-track", str(track),
        "--dump-slot", str(slot),
        "--boot-settle-s", "6",
        "--dump-only",
    ]
    subprocess.run(cmd, check=True, capture_output=True, text=True)
    rows: list[tuple[int, int]] = []
    for line in (out_dir / "serial.log").read_text(errors="replace").splitlines():
        if "CAPTURE_CONTENT,event,effective," not in line:
            continue
        fields = line.strip().split(",")
        try:
            status, note, tick = int(fields[5]), int(fields[7]), int(fields[4])
        except (IndexError, ValueError):
            continue
        if status & 0xF0 == 0x90:
            rows.append((tick, note))
    return rows


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--midi-out", default="Teensy MIDI")
    parser.add_argument("--track", type=int, default=5)
    parser.add_argument("--slot", type=int, default=1)
    parser.add_argument("--port", default=None)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--pitch", type=int, default=64)
    parser.add_argument("--previous-focus-pitch", type=int, default=55,
                        help="pitch of an unrelated persisted note that must not move (regression: silent focus reuse)")
    parser.add_argument("--to-step", type=int, default=None)
    parser.add_argument("--out-dir", type=Path, default=None)
    parser.add_argument("--loop-length", type=int, default=1536)
    args = parser.parse_args(argv)

    port = args.port
    if port is None:
        ports = sorted(Path("/dev").glob("cu.usbmodem*"))
        if not ports:
            print("[note-edit-probe] FAILED: no /dev/cu.usbmodem*")
            return 1
        port = str(ports[0])

    out_dir = args.out_dir or Path(tempfile.mkdtemp(prefix="note-edit-probe-"))
    out_dir.mkdir(parents=True, exist_ok=True)
    # Commit any NOTE_EDIT session a previous probe left open, so the pre-dump reflects the true persisted seed.
    probe_ser = serial.Serial(port, args.baud, timeout=0.2)
    probe_out = mido.open_output(args.midi_out)
    try:
        if not _ensure_loop_edit(probe_ser, probe_out):
            print("[note-edit-probe] FAILED: could not depart a previously open NOTE_EDIT session")
            return 1
    finally:
        probe_ser.close()
        probe_out.close()
    pre_dir = out_dir / "pre"
    pre_rows = _dump_effective_notes(pre_dir, track=args.track, slot=args.slot)
    print(f"[note-edit-probe] seed notes={pre_rows}")
    if not pre_rows:
        print("[note-edit-probe] FAILED: the seed is empty")
        return 1

    # The layout the firmware uses: every 16th step (empty slots included), a slot per note at its persisted tick.
    revt = list(pre_rows)
    slots = _build_select_navigation_slots(revt, loop_length=args.loop_length, loop_start=0)
    layout = RecordLayout(loop_start=0, loop_length=args.loop_length, step_to_tick={}, nav_slots=slots,
                          note_index_to_pitch=_note_index_to_pitch(revt))
    target_index = next((i for i, (_t, p) in enumerate(revt) if p == args.pitch), None)
    if target_index is None:
        print(f"[note-edit-probe] FAILED: pitch {args.pitch} not in the seed")
        return 1
    target_tick, target_pitch = revt[target_index]
    slot_index = next(i for i, s in enumerate(slots) if s.note_idx == target_index)
    src_step = target_tick // TICKS_PER_16TH_STEP
    empty_steps = [i for i, s in enumerate(slots) if s.note_idx < 0]
    empty_step_indices = sorted({s.rel_tick // TICKS_PER_16TH_STEP for s in slots if s.note_idx < 0})
    if args.to_step is not None:
        dst_step = args.to_step
        if dst_step in empty_step_indices:
            pass
        elif any(s.note_idx >= 0 and s.rel_tick // TICKS_PER_16TH_STEP == dst_step for s in slots):
            print(f"[note-edit-probe] FAILED: destination step {dst_step} is occupied")
            return 1
    else:
        # Avoid the coarse fader's rail (step 0) and the source step; prefer the nearest empty step to the source.
        options = [s for s in empty_step_indices if s != src_step and s != 0]
        if not options:
            options = [s for s in empty_step_indices if s != src_step]
        if not options:
            print("[note-edit-probe] FAILED: no empty destination step")
            return 1
        # A one-step delta is below the coarse fader's significant-movement threshold; choose the farthest empty step
        # so the movement is unambiguous (the fader is parked at the selected note's step).
        def _reachable(step: int) -> bool:
            try:
                pitchbend_for_step_and_stack(step, args.loop_length, 0, 1)
                return True
            except ValueError:
                return False
        options = [s for s in options if _reachable(s)]
        if not options:
            print("[note-edit-probe] FAILED: no reachable empty destination step")
            return 1
        dst_step = max(options, key=lambda s: abs(s - src_step))
    dst_tick = dst_step * TICKS_PER_16TH_STEP
    print(f"[note-edit-probe] track {args.track} target pitch {target_pitch}@{target_tick} "
          f"slot {slot_index}/{len(slots)} step {src_step} -> dst step {dst_step} tick {dst_tick} port={port}")

    ser = serial.Serial(port, args.baud, timeout=0.2)
    out = mido.open_output(args.midi_out)
    try:
        out.send(mido.Message("stop"))
        time.sleep(0.4)
        out.send(mido.Message("stop"))
        time.sleep(0.4)
        _press(out, TRACK_SELECT_NOTE_BASE + (args.track - 1))
        time.sleep(0.6)
        if not _ensure_loop_edit(ser, out):
            print("[note-edit-probe] FAILED: could not reach LOOP_EDIT (session did not depart)")
            return 1
        # The edit session's select-navigation slots are a neighborhood around the playhead. Position the playhead at
        # the intended note's tick so that note's actual slot is in the neighborhood before the session opens.
        out.send(mido.Message("start"))
        clocks = max(1, target_tick // 8)  # 8 ticks per MIDI clock on the 1,536-tick loop at 120 BPM
        for _ in range(clocks):
            out.send(mido.Message("clock"))
            time.sleep(1.0 / 48.0)
        out.send(mido.Message("stop"))
        time.sleep(0.3)
        if not _fresh_note_edit(ser, out):
            print("[note-edit-probe] FAILED: could not establish a fresh NOTE_EDIT session")
            return 1
        print("[note-edit-probe] fresh NOTE_EDIT session")
        time.sleep(1.6)  # let the session-entry fader settle/grace elapse before the select
        _drain(ser)
        # The select is the legacy nav-slot selector: the actual persisted tick, not a hard-coded step.
        _fader1_select_nav_slot_index(out, layout=layout, slot_index=slot_index, expected_tick=target_tick)
        ack = _read_select_ack(ser, 2.5, target_index)
        if ack is None:
            print("[note-edit-probe] FAILED: no selection decision after the nav-slot select")
            return 1
        ack_idx, ack_tick, ack_reason = ack
        print(f"[note-edit-probe] selection ack note_idx={ack_idx} selected_tick={ack_tick} reason={ack_reason} "
              f"(intended note_idx={target_index} tick={target_tick})")
        # The firmware's note_idx is its own display index; the **selected_tick** is the persisted identity this leg
        # requires (it must be the intended note's actual tick, including off-grid positions).
        if ack_tick != target_tick:
            print(f"[note-edit-probe] FAILED: the decision's selected_tick={ack_tick} ({ack_reason}) is not the "
                  f"intended note's tick {target_tick}; refusing to send the edit")
            return 1
        # Only now apply the coarse move to the empty destination step. The coarse fader moves the note by whole
        # 16th steps, so its intra-step offset is preserved.
        expected_dst_tick = target_tick + (dst_step - src_step) * TICKS_PER_16TH_STEP
        _fader2_move_to_sixteenth_step(out, layout=layout, fixture_step=dst_step)
        time.sleep(0.8)
        if not _leave_note_edit(ser, out):
            print("[note-edit-probe] FAILED: NOTE_EDIT did not depart, so the move cannot commit")
            return 1
        print("[note-edit-probe] departed NOTE_EDIT (pending edit committed)")
        time.sleep(0.5)
    finally:
        ser.close()
        out.close()

    post_rows = _dump_effective_notes(out_dir, track=args.track, slot=args.slot)
    print(f"[note-edit-probe] post notes={post_rows}")
    at_src = [t for t, p in post_rows if p == target_pitch and t == target_tick]
    at_dst = [t for t, p in post_rows if p == target_pitch and t == expected_dst_tick]
    unchanged = [(t, p) for (t, p) in pre_rows if (t, p) != (target_tick, target_pitch)]
    post_set = set(post_rows)
    lost = [row for row in unchanged if row not in post_set]
    prior_focus = [row for row in pre_rows if row[1] == args.previous_focus_pitch]
    prior_focus_kept = [row for row in prior_focus if row in post_set]
    print(f"[note-edit-probe] target src={at_src} dst={at_dst} expected_dst={expected_dst_tick} "
          f"lost_others={lost}")
    print(f"[note-edit-probe] previous focus pitch {args.previous_focus_pitch}: "
          f"{prior_focus} -> {prior_focus_kept}")
    if at_dst and not at_src and not lost and prior_focus == prior_focus_kept:
        print(f"[note-edit-probe] PASS: pitch {target_pitch} moved {target_tick}->{expected_dst_tick}; "
              "every other persisted note kept its tick")
        return 0
    print("[note-edit-probe] FAILED: the intended note did not move, or another persisted note changed")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
