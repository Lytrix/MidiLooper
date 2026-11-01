#!/usr/bin/env python3
"""Musical overdub-chain run driven by two MIDI files (DEC-221).

Owner recipe this reproduces:

1. clear the scratch slot, then record ``--record-bars`` bars from the *record* pattern (16ths),
2. **play the overdub pattern continuously** and time the Record button to slice it: an overdub is
   a capture window opened and closed over the running chord stream, never a restarted pattern,
3. windows are ``--overdub-beats`` beats long with ``--overdub-gap-beats`` between them (default
   1, 4 and 8 beats with a 1-beat gap, all inside the same continuous pass),
4. keep playing ``--play-bars`` bars with the chords still running, then stop the transport and wait
   ``--stop-bars`` bars so the deferred lanes and the capture settle.

Why the chords keep running
---------------------------
A player holds the chord phrase and opens/closes the capture window over it, so each overdub takes
a musically coherent slice. Restarting the pattern at every pass would put the same downbeat in
every window and make the passes indistinguishable. The button is therefore an *event on the same
pulse timeline as the notes*, not a phase boundary.

Why the record pattern is anchored on the device's take start
-------------------------------------------------------------
``MidiClockMaster.start()`` sends realtime ``Start``; the device resets its tick to 0 and holds it
through the first pulse. The Record press is sent before ``Start``, but the device acts on it only
when its short-press window expires, 15-20 clocks later (measured take starts at ticks 120, 128 and
160), and it anchors the take there: ``startLoopTick = currentTick``, stored ticks are
``tick - startLoopTick``. Streaming the record pattern from the grid origin therefore put the whole
take off the grid by ``startLoopTick`` (2026-11-01: 316/316 Record NoteOns 24 ticks off the 16th grid,
every overdub on it), so the same pattern overdubbed onto itself never overlapped and never hid a
note. The run reads ``Recording started @ tick N`` from the capture log and streams the record
pattern with position 0 on the take start (pulse ``origin + N / TICKS_PER_CLOCK``). Without a
capture log the take start is unknown and the pattern stays on the grid origin (reported).

Usage
-----
    .venv/bin/python scripts/hitl_midifile_chain.py \
        --record-pattern "…/16ths.mid" --overdub-pattern "…/chords.mid" \
        --record-bars 20 --out-dir captures/midifile-chain-01

The clear is **verified against the device log** and retried while the deferred-save holdoff refuses
it; a run without a capture session refuses to clear unless `--allow-unverified-clear` is passed.
Nothing is uploaded and no SD operation is performed beyond the explicit slot clear (which is
destructive and only for an authorized scratch slot). This script only speaks MIDI; start
``scripts/capture_session.py`` first: the clear is verified against that session's log (and the run
refuses to record over content without it, unless ``--allow-unverified-clear`` is passed).
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys
import time
from typing import Optional

import mido

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from hitl.actions.slots import DEFAULT_LONG_PRESS_MS  # noqa: E402
from hitl.control_constants import (  # noqa: E402
    CONTROL_CHANNEL_1BASED,
    GLOBAL_TRANSPORT_NOTE,
    LOOP_SELECT_NOTE_BASE,
    MIDI_CLOCKS_PER_BAR,
    MIDI_CLOCKS_PER_BEAT,
    RECORD_BUTTON_NOTE,
)
from hitl.midi_clock import MidiClockMaster  # noqa: E402
from hitl.midi_io import _find_midi_port, _send_long_press, _send_short_press  # noqa: E402
from hitl.midi_pattern import MidiPattern, load_midi_pattern, stream_pattern  # noqa: E402
from hitl.serial_tick_anchor import TICKS_PER_CLOCK  # noqa: E402

CONTROL_CHANNEL = CONTROL_CHANNEL_1BASED - 1
CONTROL_VELOCITY = 127


def _press(out_port, note: int, *, press_ms: int) -> None:
    _send_short_press(out_port, note=note, channel_1based=CONTROL_CHANNEL_1BASED,
                      press_ms=press_ms, _caller="hitl_midifile_chain")


def _hold(out_port, note: int, *, press_ms: int) -> None:
    _send_long_press(out_port, note=note, channel_1based=CONTROL_CHANNEL_1BASED,
                     press_ms=press_ms)


# --- clear verification (2026-10-30) -------------------------------------------------------------
# The firmware refuses a CLEAR while a deferred save is owed and its synchronous drain is blocked
# (`Clear refused: deferred save temporarily blocked by post_load_holdoff; slot left untouched`),
# which is what happened on the aborted recapture: the slot was not cleared and the next Record press
# opened an overdub on the old take. The clear is therefore a **verified, retried** gesture: read the
# device's own log row from the active capture session, and refuse to continue unless the requested
# slot is actually empty. `captures/.current_session` is written by `scripts/capture_session.py`.


def _default_capture_log() -> Optional[pathlib.Path]:
    """The active capture session's log, if a reader started one."""
    pointer = ROOT / "captures" / ".current_session"
    if not pointer.exists():
        return None
    try:
        first = pointer.read_text(encoding="utf-8").strip().splitlines()[0]
    except (OSError, IndexError):
        return None
    path = pathlib.Path(first)
    candidates = [path] if path.is_absolute() else [ROOT / path, pathlib.Path.cwd() / path]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def _clear_retry_delay(text: str, settle_s: float) -> float:
    """Retry the clear after the device's named gate, not a fixed settle.

    The post-load commit holdoff is bounded at 100 ms (`LoadLoopBudget::PostLoadCommitSaveHoldoffMs`);
    once the J2 direct row names it, 200 ms is enough. Every other refusal keeps the conservative settle
    (the save may still be converging).
    """
    if re.search(r"DIAG,clear_refused,\d+,post_load_holdoff", text) or \
            "blocked by post_load_holdoff" in text:
        return min(max(settle_s, 0.2), 0.2)
    return max(settle_s, 0.5)


def _read_log_since(path: pathlib.Path, offset: int) -> tuple[str, int]:
    """Text appended to ``path`` after byte ``offset``; returns the new offset."""
    with path.open("rb") as handle:
        handle.seek(offset)
        data = handle.read()
    return data.decode("utf-8", errors="replace"), offset + len(data)


def _classify_clear(text: str, slot: int) -> Optional[str]:
    """Classify the clear outcome rows in ``text`` for a 1-based ``slot``.

    Returns ``cleared`` (RAM content) / ``empty`` (already empty) / ``refused`` / ``aborted`` /
    ``other_slot`` (the device cleared a different slot), or ``None`` when no outcome row is present.
    """
    # J2 direct row: `DIAG,clear_refused,<1-based slot>,<reason>`. It is emitted before the queued
    # logger line, so it is the deterministic outcome for the attempt window.
    direct = re.findall(r"DIAG,clear_refused,(\d+),([a-z_]+)", text)
    if direct:
        seen_slot, _reason = direct[-1]
        return "refused" if int(seen_slot) == slot else "other_slot"
    if re.search(r"Clear refused:", text):
        return "refused"
    if re.search(r"Clear aborted:", text):
        return "aborted"
    selected = re.findall(r"Clear selected slot (\d+)", text)
    if selected:
        return "cleared" if int(selected[-1]) == slot else "other_slot"
    removed = re.findall(r"Clear removed persisted slot (\d+) payload", text)
    if removed:
        return "cleared" if int(removed[-1]) == slot else "other_slot"
    # The "selected slot is empty" row prints the firmware's 0-based slot; the two success rows above
    # print 1-based operator slots. Compare each in its own base.
    ignored_selected = re.findall(
        r"Clear ignored .*selected slot is empty \(track \d+ slot (\d+)\)", text)
    if ignored_selected:
        return "empty" if int(ignored_selected[-1]) + 1 == slot else "other_slot"
    if re.search(r"Clear ignored .*track is empty", text):
        return "empty"
    return None


def _assert_loop_edit_mode(capture_log: pathlib.Path, *, timeout_s: float = 2.0) -> dict[str, object]:
    """Prove LOOP_EDIT from the device's own `disp_src` heartbeat (K3), read-only.

    The display emits one `DIAG,disp_src` row per second even when the frame key is unchanged, with
    `mode=0` for LOOP_EDIT and `mode=1` for NOTE_EDIT. The rig reads only the appended window and
    refuses to clear when the device reports NOTE_EDIT or no fresh heartbeat -- it must not send a
    mode-changing gesture to manufacture the proof (that was the -25/-26 regression).
    """
    size = capture_log.stat().st_size
    # The heartbeat is 1 s; accept the most recent one already in the log, then wait for the next
    # only if the tail has none. The mode cannot change without a gesture, and the rig sends none.
    tail_offset = max(0, size - 65536)
    tail, _ = _read_log_since(capture_log, tail_offset)
    modes = re.findall(r"(?:DIAG,disp_src|DIAG,alloc_census),.*?\bmode=(\d+)", tail)
    if modes:
        if modes[-1] == "0":
            return {"mode": "LOOP_EDIT", "source": "disp_src"}
        raise RuntimeError(
            "the device reports NOTE_EDIT in its disp_src heartbeat; refusing to clear "
            "without LOOP_EDIT")
    offset = size
    deadline = time.monotonic() + max(timeout_s, 0.05)
    text = ""
    while time.monotonic() < deadline:
        chunk, offset = _read_log_since(capture_log, offset)
        text += chunk
        modes = re.findall(r"(?:DIAG,disp_src|DIAG,alloc_census),.*?\bmode=(\d+)", text)
        if modes:
            if modes[-1] == "0":
                return {"mode": "LOOP_EDIT", "source": "disp_src"}
            raise RuntimeError(
                "the device reports NOTE_EDIT in its disp_src heartbeat; refusing to clear "
                "without LOOP_EDIT")
        time.sleep(0.1)
    raise RuntimeError(
        "no fresh DIAG,disp_src mode heartbeat; cannot prove LOOP_EDIT before the clear")


def _wait_for_view_prewarm(capture_log: pathlib.Path, *, timeout_s: float = 5.0) -> dict[str, object]:
    """Wait for the off-edge view preparation's terminal `view_prewarm_step,...,ready=1` row.

    The Record edge is strict: if preparation has not reached Prepared it stays pending. A HITL run
    wants the prepared path, so synchronize on the device's own completion row rather than guessing
    the idle cadence. Returns `ready=false` after the timeout; the caller decides whether to press.
    """
    size = capture_log.stat().st_size
    tail_offset = max(0, size - 65536)
    tail, _ = _read_log_since(capture_log, tail_offset)
    if re.search(r"DIAG,view_prewarm_step,.*ready=1", tail):
        return {"ready": True, "waited_s": 0.0}
    offset = size
    deadline = time.monotonic() + max(timeout_s, 0.1)
    text = ""
    start = time.monotonic()
    while time.monotonic() < deadline:
        chunk, offset = _read_log_since(capture_log, offset)
        text += chunk
        if re.search(r"DIAG,view_prewarm_step,.*ready=1", text):
            return {"ready": True, "waited_s": round(time.monotonic() - start, 3)}
        time.sleep(0.1)
    return {"ready": False, "waited_s": round(time.monotonic() - start, 3),
            "tail": text[-300:]}


def _wait_for_device_rows(
    capture_log: pathlib.Path,
    offset: int,
    *,
    pattern: str,
    min_count: int,
    timeout_s: float,
    what: str,
) -> tuple[str, int]:
    """Wait until the device log contains at least ``min_count`` matches for ``pattern``.

    A host-side phase report is not proof that the device accepted a press. This gate is read-only and
    fails the run by name when the Record or an overdub never started (the silent no-op shape measured
    on 2026-10-31: 12 host windows, `cap=0`, zero `Overdubbing started`).
    """
    deadline = time.monotonic() + max(timeout_s, 0.1)
    text = ""
    while time.monotonic() < deadline:
        chunk, offset = _read_log_since(capture_log, offset)
        text += chunk
        if len(re.findall(pattern, text)) >= min_count:
            return text, offset
        time.sleep(0.1)
    raise RuntimeError(
        f"the device did not accept {what}: saw {len(re.findall(pattern, text))}/{min_count} "
        f"matching rows. Last device rows:\n{text[-600:]}")


def _clear_slot_verified(
    out_port,
    note: int,
    *,
    slot: int,
    press_ms: int,
    capture_log: pathlib.Path,
    attempts: int,
    settle_s: float,
) -> dict[str, object]:
    """Long-press the slot's loop button until the device log confirms it is empty.

    A refused clear leaves the slot untouched, so retrying is safe; each attempt waits for the
    holdoff to lapse and for the device's own outcome row. Raises rather than recording over content.
    """
    last_text = ""
    for attempt in range(1, max(attempts, 1) + 1):
        offset = capture_log.stat().st_size
        _hold(out_port, note, press_ms=press_ms)
        deadline = time.monotonic() + max(settle_s, 0.5)
        text = ""
        outcome = None
        while time.monotonic() < deadline:
            time.sleep(0.25)
            chunk, offset = _read_log_since(capture_log, offset)
            text += chunk
            outcome = _classify_clear(text, slot)
            if outcome is not None:
                break
        if outcome in ("cleared", "empty"):
            return {"attempts": attempt, "outcome": outcome}
        if outcome == "other_slot":
            raise RuntimeError(
                f"clear was pressed for slot {slot}, but the device reported another slot: {text[-400:]}")
        last_text = text
        # `refused`/`aborted` (the holdoff) or no row yet: wait for the named gate and retry.
        time.sleep(_clear_retry_delay(text, settle_s))
    raise RuntimeError(
        f"clear of slot {slot} was not confirmed after {attempts} attempt(s); refusing to record "
        f"over content. Last device rows:\n{last_text[-600:]}")


def _beats_list(values: str) -> list[float]:
    beats = [float(part) for part in values.replace(",", " ").split() if part]
    if not beats or any(beat <= 0 for beat in beats):
        raise argparse.ArgumentTypeError("--overdub-beats needs positive beat lengths")
    return beats


def _press_clocks(press_ms: int, bpm: float) -> int:
    """How many clock pulses a press occupies (its release is the action edge)."""
    return max(1, int(round(press_ms / 1000.0 * bpm / 60.0 * MIDI_CLOCKS_PER_BEAT)))


def _window_offsets(lengths: list[float], gap_beats: float) -> list[tuple[int, int]]:
    """``(start_clock, length_clock)`` for each pass, laid end to end with a gap between them."""
    windows: list[tuple[int, int]] = []
    cursor = 0.0
    for beats in lengths:
        start = int(round(cursor * MIDI_CLOCKS_PER_BEAT))
        length = int(round(beats * MIDI_CLOCKS_PER_BEAT))
        windows.append((start, length))
        cursor += beats + gap_beats
    return windows


def run_stream(
    out_port,
    master: MidiClockMaster,
    pattern: MidiPattern,
    *,
    start_pulse: int,
    span_clocks: int,
    note_channel_1based: int,
    capture_windows: list[tuple[int, int]],
    press_ms: int,
    bpm: float,
    abort=None,
) -> dict[str, object]:
    """Stream ``pattern`` continuously and open/close capture over it on the same pulse grid.

    ``capture_windows`` are ``(start_clock, length_clock)`` offsets from the stream start. A Record
    press is emitted early enough that its **release** (the edge the firmware acts on) lands on the
    window edge, so the captured slice is the one the beat plan asked for.
    """
    note_channel = int(note_channel_1based) - 1
    lead = _press_clocks(press_ms, bpm)
    control_at: dict[int, list[tuple[int, bool]]] = {}

    def schedule(offset: int, on: bool) -> None:
        offset = max(0, offset)
        control_at.setdefault(offset, []).append((RECORD_BUTTON_NOTE, on))

    for start, length in capture_windows:
        # Release at the window start/end: press one press-length earlier.
        schedule(start - lead, True)
        schedule(start, False)
        schedule(start + length - lead, True)
        schedule(start + length, False)

    sent_notes = 0
    sent_controls = 0
    behind_pulses = 0
    for offset in range(max(int(span_clocks), 0)):
        pulse = int(start_pulse) + offset
        if not master.wait_pulse(pulse, timeout_s=10.0, abort=abort):
            pump_error = master.pump_error()
            raise RuntimeError(
                f"clock pulse {pulse} was not sent in time"
                + (f" (pump error: {pump_error!r})" if pump_error is not None else
                   " (the pump thread is blocked: the device stopped draining MIDI IN)"))
        behind_pulses = max(behind_pulses, master.pulses_sent() - (pulse + 1))
        for note, on in control_at.get(offset, ()):
            out_port.send(mido.Message("note_on" if on else "note_off", channel=CONTROL_CHANNEL,
                                       note=note, velocity=CONTROL_VELOCITY if on else 0))
            sent_controls += 1
        for event in pattern.events_at(offset % pattern.clock_length):
            out_port.send(mido.Message("note_on" if event.note_on else "note_off",
                                       channel=note_channel, note=event.note,
                                       velocity=(event.velocity or 100) if event.note_on else 0))
            sent_notes += 1
    return {
        "clocks": int(span_clocks),
        "start_pulse": int(start_pulse),
        "note_events": sent_notes,
        "control_events": sent_controls,
        "capture_windows": capture_windows,
        "behind_pulses": behind_pulses,
    }


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="MIDI-file driven record + sliced overdub chain")
    parser.add_argument("--record-pattern", required=True, help="MIDI file looped while recording")
    parser.add_argument("--overdub-pattern", required=True,
                        help="MIDI file streamed continuously under the overdub windows")
    parser.add_argument("--record-bars", type=int, default=20)
    parser.add_argument("--bpm", type=float, default=120.0)
    parser.add_argument("--midi-channel", type=int, default=1, help="1-based note channel")
    parser.add_argument("--record-loop-bars", type=int, default=None,
                        help="Force the record pattern loop length (default: file rounded to bars)")
    parser.add_argument("--overdub-loop-bars", type=int, default=None,
                        help="Force the overdub pattern loop length (default: file rounded to bars)")
    parser.add_argument("--settle-beats", type=float, default=4.0,
                        help="Idle beats after the record stop before the first window")
    parser.add_argument("--overdub-beats", default=None,
                        help="Explicit overdub window lengths in beats, in order "
                             "(overrides --overdub-count)")
    parser.add_argument("--overdub-count", type=int, default=12,
                        help="Number of overdub passes when --overdub-beats is not given (default 12)")
    parser.add_argument("--overdub-min-beats", type=float, default=4.0,
                        help="Shortest window in beats: 4 = one bar (default 4). Shorter windows cut "
                             "the chord phrase and miss notes; pass --allow-short-windows to force")
    parser.add_argument("--overdub-max-beats", type=float, default=8.0,
                        help="Longest window in beats: 8 = two bars (default 8)")
    parser.add_argument("--allow-short-windows", action="store_true",
                        help="Permit windows shorter than 4 beats (owner measured note loss below 4)")
    parser.add_argument("--hold-clock-on-settle", action="store_true",
                        help="I1 gate: keep the host clock pump running through the record and run-end "
                             "settles (transport stopped) instead of releasing it. Proves the firmware "
                             "advances the stop lane with the host clock still arriving.")
    parser.add_argument("--record-stop-mode", choices=("transport", "record"), default="transport",
                        help="How the take is ended. `transport` presses global Stop, which retires the "
                             "record lane; `record` presses Record again (wedges on a cleared slot).")
    parser.add_argument("--record-settle-s", type=float, default=12.0,
                        help="Seconds to wait after the record-end transport stop before resuming")
    parser.add_argument("--overdub-gap-beats", type=float, default=2.0,
                        help="Idle beats between windows (owner asks for <= 4; default 2 because a "
                             "1-beat gap at 120 BPM is 500 ms, only just above the firmware's ~400 ms "
                             "multi-tap window, and presses then merge into double/triple taps)")
    parser.add_argument("--play-bars", type=int, default=4,
                        help="Bars of chords after the last window (transport still running)")
    parser.add_argument("--stop-bars", type=int, default=4,
                        help="Bars of wall time to wait after the transport stop")
    parser.add_argument("--slot", type=int, default=4, choices=range(1, 9),
                        help="Scratch slot cleared before recording (1-based, default 4)")
    parser.add_argument("--no-clear", action="store_true",
                        help="Do not clear the scratch slot (retry over existing content)")
    parser.add_argument("--press-ms", type=int, default=85)
    parser.add_argument("--clear-ms", type=int, default=DEFAULT_LONG_PRESS_MS)
    parser.add_argument("--capture-log", default=None,
                        help="Capture-session log used to verify the clear (default: "
                             "captures/.current_session from scripts/capture_session.py)")
    parser.add_argument("--allow-unverified-clear", action="store_true",
                        help="Clear without a capture log (unverified; the old blind behaviour)")
    parser.add_argument("--clear-attempts", type=int, default=6,
                        help="Clear attempts while the device's deferred save is in its post-load holdoff")
    parser.add_argument("--clear-settle-s", type=float, default=1.5,
                        help="Seconds to wait for a clear outcome row before retrying")
    parser.add_argument("--port-substring", default="Teensy")
    parser.add_argument("--port-timeout-s", type=float, default=30.0)
    parser.add_argument("--out-dir", type=pathlib.Path, required=True)
    parser.add_argument("--dry-run", action="store_true",
                        help="Print the parsed patterns and the beat plan, then exit")
    args = parser.parse_args(argv)

    if args.overdub_beats:
        lengths = _beats_list(args.overdub_beats)
    else:
        count = max(int(args.overdub_count), 1)
        lo = max(float(args.overdub_min_beats), 0.125)
        hi = max(float(args.overdub_max_beats), lo)
        step = 0.0 if count == 1 else (hi - lo) / float(count - 1)
        lengths = [round(lo + step * index, 3) for index in range(count)]
    if min(lengths) < 4.0 and not args.allow_short_windows:
        parser.error(
            f"shortest window is {min(lengths):g} beats; below 4 beats the chord phrase is cut and "
            "notes are missed (owner measurement). Raise --overdub-min-beats or pass "
            "--allow-short-windows to override")
    if args.overdub_gap_beats < 0:
        parser.error("--overdub-gap-beats cannot be negative")
    if args.overdub_gap_beats > 4:
        parser.error("--overdub-gap-beats above 4 contradicts the owner recipe")

    record_pattern = load_midi_pattern(args.record_pattern, loop_bars=args.record_loop_bars)
    overdub_pattern = load_midi_pattern(args.overdub_pattern, loop_bars=args.overdub_loop_bars)
    windows = _window_offsets(lengths, args.overdub_gap_beats)
    overdub_span = max((start + length for start, length in windows), default=0)

    if args.dry_run:
        print(json.dumps({
            "record": record_pattern.summary(),
            "overdub": overdub_pattern.summary(),
            "record_bars": args.record_bars,
            "overdub_beats": lengths,
            "overdub_gap_beats": args.overdub_gap_beats,
            "windows_clocks": windows,
            "settle_beats": args.settle_beats,
            "play_bars": args.play_bars,
            "stop_bars": args.stop_bars,
            "bpm": args.bpm,
            "slot": args.slot,
            "clear": not args.no_clear,
        }, indent=2))
        return 0

    capture_log: Optional[pathlib.Path] = None
    if not args.no_clear:
        explicit = pathlib.Path(args.capture_log).expanduser() if args.capture_log else None
        capture_log = explicit.resolve() if explicit is not None else _default_capture_log()
        if capture_log is None and not args.allow_unverified_clear:
            parser.error(
                "the clear phase needs the device log to prove the slot cleared; start "
                "scripts/capture_session.py first, pass --capture-log PATH, or pass "
                "--allow-unverified-clear (not recommended: the slot may keep its take)")
        if capture_log is not None and not capture_log.exists():
            parser.error(f"--capture-log does not exist: {capture_log}")

    out_dir = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    report_path = out_dir / "midifile-chain.json"
    report: dict[str, object] = {
        "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "bpm": args.bpm,
        "midi_channel": args.midi_channel,
        "record_bars": args.record_bars,
        "overdub_beats": lengths,
        "overdub_gap_beats": args.overdub_gap_beats,
        "slot": args.slot,
        "cleared": not args.no_clear,
        "record_pattern": record_pattern.summary(),
        "overdub_pattern": overdub_pattern.summary(),
        "phases": [],
    }

    in_name = _find_midi_port(args.port_substring, is_input=True, timeout_s=args.port_timeout_s)
    out_name = _find_midi_port(args.port_substring, is_input=False, timeout_s=args.port_timeout_s)
    report["midi_in"], report["midi_out"] = in_name, out_name

    with mido.open_input(in_name) as _in_port, mido.open_output(out_name) as out_port:
        master = MidiClockMaster(out_port)
        try:
            # 0) Known state: realtime Stop, then clear the scratch slot so a retry never stacks.
            master.stop()
            time.sleep(0.35)
            if not args.no_clear:
                # K3: prove the clear's required edit mode from the device's own 1 s `disp_src`
                # heartbeat before any slot-touching gesture. Read-only; no mode cycle.
                if capture_log is not None:
                    mode = _assert_loop_edit_mode(capture_log, timeout_s=3.0)
                    report["phases"].append({"phase": "mode", **mode})
                clear_note = LOOP_SELECT_NOTE_BASE + (args.slot - 1)
                if capture_log is not None:
                    # Verified + retried: a refused clear (the post-load holdoff) leaves the slot
                    # untouched, so retrying is safe; the run must never record over the old take.
                    clear = _clear_slot_verified(
                        out_port, clear_note, slot=args.slot, press_ms=args.clear_ms,
                        capture_log=capture_log, attempts=args.clear_attempts,
                        settle_s=args.clear_settle_s)
                else:
                    _hold(out_port, clear_note, press_ms=args.clear_ms)
                    time.sleep(0.8)
                    clear = {"attempts": 1, "outcome": "unverified"}
                report["phases"].append({"phase": "clear", "slot": args.slot, **clear})
                if capture_log is not None:
                    prep = _wait_for_view_prewarm(capture_log)
                    report["phases"].append({"phase": "view_prewarm", **prep})
                    if not prep.get("ready"):
                        raise RuntimeError(
                            "view preparation did not reach Prepared before the Record edge; "
                            "refusing to record cold: " + str(prep.get("tail", ""))[-300:])
            # 1) Record: press first, then Start so the take is live at the grid origin.
            record_log_offset = (
                capture_log.stat().st_size if capture_log is not None else None)
            _press(out_port, RECORD_BUTTON_NOTE, press_ms=args.press_ms)
            master.start(bpm=args.bpm)
            grid = master.grid_origin_pulse()
            if grid is None:
                raise RuntimeError("the clock master has no grid origin after Start")
            record_clocks = args.record_bars * MIDI_CLOCKS_PER_BAR
            # The stop press must **release before the loop boundary**. The take starts a few clocks
            # after the grid origin (the record press release), so a stop scheduled at the boundary
            # wraps: measured `Recording stopped @ tick 136 (recStart=128 length=15360)` -- an
            # 8-tick take that then wedges the record stop. Land ~5 clocks short instead; the length
            # quantizes to the full bar count.
            stop_lead = _press_clocks(args.press_ms, args.bpm) + 2
            record_end = grid + record_clocks - stop_lead
            # Anchor the pattern on the take the device actually started (see the module docstring).
            record_anchor = grid
            stream_log_offset = record_log_offset
            if capture_log is not None:
                text, stream_log_offset = _wait_for_device_rows(
                    capture_log, stream_log_offset,
                    pattern=r"Recording started @ tick \d+", min_count=1, timeout_s=3.0,
                    what="the Record start")
                report["device_record_starts"] = 1
                start_tick = int(re.search(r"Recording started @ tick (\d+)", text).group(1))
                if start_tick % TICKS_PER_CLOCK != 0:
                    raise RuntimeError(f"take start tick {start_tick} is not on a clock pulse")
                record_anchor = grid + start_tick // TICKS_PER_CLOCK
                report["record_take_start_tick"] = start_tick
            report["record_phase_origin_pulse"] = record_anchor
            # Notes before the take start are not captured; join the pattern at the next pulse not
            # yet sent so no event is emitted late in a burst.
            record_start = max(record_anchor, master.pulses_sent() + 1)
            report["phases"].append({"phase": "record", **stream_pattern(
                out_port, master, record_pattern,
                start_pulse=record_start, total_clocks=record_end - record_start,
                channel_1based=args.midi_channel, phase_origin_pulse=record_anchor)})
            if args.record_stop_mode == "transport":
                # End the take with the global transport stop. `--hold-clock-on-settle` keeps the host
                # clock pump running through the settle (the I1 gate): the firmware's all-stopped
                # stop-lane branch (DEC-228) now advances the record stop even while the host clock
                # arrives, instead of the old 4-grains-in-60-s park (session_20261030_143427.log).
                # Without the flag the pump is released (legacy cadence).
                _press(out_port, GLOBAL_TRANSPORT_NOTE, press_ms=args.press_ms)
                if args.hold_clock_on_settle:
                    # I1: transport stop only; the host clock keeps arriving. The firmware's all-stopped
                    # stop-lane branch must advance the record stop without the pump release.
                    master.send_realtime_stop()
                else:
                    master.stop()
                time.sleep(max(args.record_settle_s, 0.0))
                master.start(bpm=args.bpm)
                grid = master.grid_origin_pulse()
                if grid is None:
                    raise RuntimeError("the clock master has no grid origin after the restart")
            else:
                _press(out_port, RECORD_BUTTON_NOTE, press_ms=args.press_ms)

            # Device content gate: a host phase report is not proof that the Record press landed.
            # The 2026-10-31 chain-53 run sent 20 bars + 12 windows and the device accepted neither
            # (`cap=0`); the take-start read above is that gate (it fails the run by name).

            # 2) One continuous chord stream under all the capture windows.
            settle_clocks = int(round(args.settle_beats * MIDI_CLOCKS_PER_BEAT))
            stream_start = grid + settle_clocks
            span = overdub_span + args.play_bars * MIDI_CLOCKS_PER_BAR
            report["phases"].append({"phase": "overdub_stream", **run_stream(
                out_port, master, overdub_pattern,
                start_pulse=stream_start, span_clocks=span,
                note_channel_1based=args.midi_channel,
                capture_windows=windows,
                press_ms=args.press_ms, bpm=args.bpm)})

            if capture_log is not None:
                text, stream_log_offset = _wait_for_device_rows(
                    capture_log, stream_log_offset,
                    pattern=r"Overdubbing started @ tick", min_count=len(windows),
                    timeout_s=3.0, what=f"all {len(windows)} overdub starts")
                report["device_overdub_starts"] = len(
                    re.findall(r"Overdubbing started @ tick", text))

            # 3) Stop the transport and let the deferred lanes settle.
            _press(out_port, GLOBAL_TRANSPORT_NOTE, press_ms=args.press_ms)
            if args.hold_clock_on_settle:
                master.send_realtime_stop()
            else:
                master.stop()
            stop_seconds = args.stop_bars * 4 * 60.0 / args.bpm
            report["phases"].append({"phase": "transport_stop",
                                     "stop_bars": args.stop_bars,
                                     "stop_seconds": stop_seconds})
            time.sleep(max(stop_seconds, 0.0))

            if capture_log is not None:
                tail_offset = max(0, capture_log.stat().st_size - 65536)
                tail, _ = _read_log_since(capture_log, tail_offset)
                caps = re.findall(r"alloc_census,.*\bcap=(\d+)", tail)
                starts = int(report.get("device_overdub_starts", 0))
                expected = 1 + starts
                if caps:
                    report["device_cap_last"] = int(caps[-1])
                if not caps or int(caps[-1]) < expected:
                    raise RuntimeError(
                        "device content gate failed: "
                        f"cap={caps[-1] if caps else 'absent'} < expected {expected} "
                        f"(1 Record + {starts} overdub starts); refusing to treat this run as "
                        f"content evidence. Last rows:\n{tail[-600:]}")
        finally:
            master.stop_pump()

    report["pulses_total"] = master.pulses_sent()
    report["finished_utc"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"report": str(report_path)}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
