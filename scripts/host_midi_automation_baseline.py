#!/usr/bin/env python3
"""Local hardware automation baseline for record/overdub per track.

This script runs on the Mac and drives a connected Teensy over USB MIDI.
It automates:
  - track select
  - record start
  - record stop (which returns to playback in firmware)
  - overdub start
  - overdub stop
  - a second overdub pass (2 bars, C0–B1 one-beat notes — default; disable with
    --second-overdub-bars 0)
while streaming dense chromatic notes plus CC data on a recordable channel.

It can optionally read USB serial capture output from the Teensy capture build
to assert expected Track state transitions. When serial capture is enabled, the
run aborts if no serial lines arrive for 20 seconds (crash/hang detection) and
writes the capture log plus JSON report before exiting.
"""
from __future__ import annotations

import argparse
import json
import re
import threading
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any, Optional

import serial
from serial import SerialException

try:
    import mido
except ImportError as exc:  # pragma: no cover - import guard
    raise SystemExit(
        "Missing dependency 'mido'. Install with:\n"
        "  python3 -m pip install mido python-rtmidi pyserial"
    ) from exc

from hitl.serial_timing import (
    extract_first_note_offset as _extract_first_note_offset,
    extract_phase_boundaries as _extract_phase_boundaries,
    phase_start_delay_clocks as _phase_start_delay_clocks,
)
from hitl.serial_transport import (
    resolve_wall_tempo_for_proxy as _resolve_wall_tempo_for_proxy,
    serial_capture_active as _serial_capture_active,
    serial_follow_active as _serial_follow_active,
    serial_lines_show_bpm_activity as _serial_lines_show_bpm_activity,
    serial_sequencer_running as _serial_sequencer_running,
    use_serial_transport_proxy as _use_serial_transport_proxy,
)
from hitl.transport_clock import (
    clock_seen_within as _clock_seen_within,
    ensure_midi_clock as _ensure_midi_clock,
    ensure_transport_clock as _ensure_midi_clock_or_serial_proxy,
    stream_pattern_for_bars as _stream_pattern_for_bars,
    wait_for_clock_pulses as _wait_for_clock_pulses,
)


TRACK_SELECT_NOTE_BASE = 60
RECORD_BUTTON_NOTE = 36
PLAY_STOP_BUTTON_NOTE = 40
GLOBAL_TRANSPORT_NOTE = 39
CONTROL_CHANNEL_1BASED = 16

# Must match Config::TICKS_PER_BAR / Config::TICKS_PER_16TH_STEP in include/Globals.h.
TICKS_PER_BAR = 768
TICKS_PER_16TH_STEP = 48
TICKS_PER_8TH_STEP = TICKS_PER_16TH_STEP * 2
MIDI_CLOCKS_PER_BAR = 96
MIDI_CLOCKS_PER_BEAT = 24
RECORD_GRID_STEP_CLOCKS = 6  # 16th notes at 24 PPQN
OVERDUB_GRID_STEP_CLOCKS = 12  # 8th notes at 24 PPQN
TICKS_PER_BEAT = TICKS_PER_BAR // 4
DEFAULT_RECORD_NOTE_SPAN_MIN_RATIO = 0.9
DEFAULT_LONG_RUN_BAR_THRESHOLD = 48
DEFAULT_RECORD_STOP_RAM2_FLOOR_BYTES = 0
DEFAULT_RECORD_STOP_RAM2_WARN_BYTES = 12 * 1024


@dataclass(frozen=True)
class TransitionExpectation:
    from_state: str
    to_state: str
    per_track_min: int


EXPECTED_TRANSITIONS = (
    TransitionExpectation("ARMED", "RECORDING", 1),
    TransitionExpectation("RECORDING", "STOPPED_RECORDING", 1),
    TransitionExpectation("STOPPED_RECORDING", "PLAYING", 1),
    TransitionExpectation("PLAYING", "OVERDUBBING", 1),
    TransitionExpectation("OVERDUBBING", "PLAYING", 1),
)

STOP_STAGE_SEQUENCE: tuple[str, ...] = (
    "record_stop",
    "seal",
    "publish",
    "finalize",
    "visual_cache_request",
    "revt_queue",
    "state_advance",
    "save_request",
)

PERSISTENCE_STAGE_SEQUENCE: tuple[str, ...] = (
    "request",
    "dispatch",
    "result",
)


def _expected_transition_expectations(args: argparse.Namespace) -> tuple[TransitionExpectation, ...]:
    if getattr(args, "record_only", False):
        return (
            TransitionExpectation("ARMED", "RECORDING", 1),
            TransitionExpectation("RECORDING", "STOPPED_RECORDING", 1),
            TransitionExpectation("STOPPED_RECORDING", "PLAYING", 1),
        )
    overdub_pass_count = 2 if getattr(args, "second_overdub_bars", 0) else 1
    return (
        TransitionExpectation("ARMED", "RECORDING", 1),
        TransitionExpectation("RECORDING", "STOPPED_RECORDING", 1),
        TransitionExpectation("STOPPED_RECORDING", "PLAYING", 1),
        TransitionExpectation("PLAYING", "OVERDUBBING", overdub_pass_count),
        TransitionExpectation("OVERDUBBING", "PLAYING", overdub_pass_count),
    )


@dataclass
class RunAbort:
    """Optional hard deadline plus serial heartbeat watchdog."""

    run_deadline: Optional[float] = None
    serial_collector: Optional[Any] = None
    heartbeat_timeout_s: float = 20.0

    def check(self) -> Optional[str]:
        now = time.monotonic()
        if self.run_deadline is not None and now >= self.run_deadline:
            return "run deadline exceeded"
        if self.serial_collector is not None and self.heartbeat_timeout_s > 0:
            return self.serial_collector.heartbeat_abort_reason(self.heartbeat_timeout_s)
        return None


def _wait_for_serial_line_idle(
    line_count_fn: Any,
    *,
    idle_ms: int,
    max_drain_ms: int,
    poll_s: float = 0.02,
) -> int:
    """Wait until serial line count is stable for idle_ms (USB TX / deferred #CAP flush)."""
    if max_drain_ms <= 0:
        return 0
    start_len = int(line_count_fn())
    idle_s = max(idle_ms, 0) / 1000.0
    deadline = time.monotonic() + max(max_drain_ms, 0) / 1000.0
    last_len = start_len
    last_change_at = time.monotonic()
    while time.monotonic() < deadline:
        current_len = int(line_count_fn())
        if current_len > last_len:
            last_len = current_len
            last_change_at = time.monotonic()
        elif idle_s <= 0.0 or (time.monotonic() - last_change_at) >= idle_s:
            break
        time.sleep(max(poll_s, 0.001))
    return last_len - start_len


class SerialCaptureCollector:
    """Collects Teensy serial lines in a background thread."""

    def __init__(self, port: str, baud: int, timeout: float = 0.05) -> None:
        self._serial = serial.Serial(port, baud, timeout=timeout)
        self._lines: list[str] = []
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._error: str = ""
        self._last_line_at: Optional[float] = None
        self._thread = threading.Thread(target=self._run, daemon=True)

    def start(self) -> None:
        self._thread.start()

    def line_count(self) -> int:
        with self._lock:
            return len(self._lines)

    def drain_until_idle(
        self,
        *,
        idle_ms: int = 750,
        max_drain_ms: int = 10000,
    ) -> int:
        """Keep the reader thread running until trailing serial lines stop arriving."""
        return _wait_for_serial_line_idle(
            self.line_count,
            idle_ms=idle_ms,
            max_drain_ms=max_drain_ms,
        )

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(timeout=2.0)
        self._serial.close()

    def _run(self) -> None:
        try:
            while not self._stop.is_set():
                raw = self._serial.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                if not line:
                    continue
                with self._lock:
                    self._lines.append(line)
                    self._last_line_at = time.monotonic()
        except SerialException as exc:
            self._error = str(exc)
            self._stop.set()

    def snapshot(self) -> list[str]:
        with self._lock:
            return list(self._lines)

    def error(self) -> str:
        return self._error

    def seconds_since_last_line(self) -> Optional[float]:
        with self._lock:
            if self._last_line_at is None:
                return None
            return time.monotonic() - self._last_line_at

    def write_line(self, text: str) -> None:
        payload = (text.rstrip("\n") + "\n").encode("utf-8")
        self._serial.write(payload)
        self._serial.flush()

    def heartbeat_abort_reason(self, timeout_s: float) -> Optional[str]:
        if self._error:
            return f"serial read error: {self._error}"
        with self._lock:
            if self._last_line_at is None:
                return None
            elapsed = time.monotonic() - self._last_line_at
        if elapsed > timeout_s:
            return (
                f"serial heartbeat lost ({elapsed:.1f}s since last line, "
                f"limit {timeout_s:.1f}s)"
            )
        return None


def _find_midi_port(name_substring: str, is_input: bool, timeout_s: float = 5.0) -> str:
    lowered = name_substring.lower()
    deadline = time.monotonic() + max(timeout_s, 0.0)
    names: list[str] = []
    while True:
        names = mido.get_input_names() if is_input else mido.get_output_names()
        for name in names:
            if lowered in name.lower():
                return name
        if time.monotonic() >= deadline:
            break
        time.sleep(0.1)
    role = "input" if is_input else "output"
    available = "\n".join(f"  - {n}" for n in names) or "  (none)"
    raise RuntimeError(
        f"No MIDI {role} port matching '{name_substring}'.\nAvailable {role} ports:\n{available}"
    )


def _send_short_press(
    out_port: mido.ports.BaseOutput, *, note: int, channel_1based: int, press_ms: int
) -> None:
    ch = channel_1based - 1
    out_port.send(mido.Message("note_on", channel=ch, note=note, velocity=127))
    time.sleep(max(press_ms, 1) / 1000.0)
    out_port.send(mido.Message("note_off", channel=ch, note=note, velocity=0))


def _send_multi_short_press(
    out_port: mido.ports.BaseOutput,
    *,
    note: int,
    channel_1based: int,
    press_ms: int,
    count: int,
    gap_ms: int = 80,
) -> None:
    for i in range(max(count, 0)):
        _send_short_press(
            out_port,
            note=note,
            channel_1based=channel_1based,
            press_ms=press_ms,
        )
        if i + 1 < count:
            time.sleep(max(gap_ms, 1) / 1000.0)


def _drain_input_messages(in_port: mido.ports.BaseInput) -> int:
    count = 0
    while True:
        msg = in_port.poll()
        if msg is None:
            break
        count += 1
    return count


def _stream_dense_chromatic(
    out_port: mido.ports.BaseOutput,
    in_port: mido.ports.BaseInput,
    *,
    midi_channel_1based: int,
    root_note: int,
    semitone_span: int,
    duration_s: float,
    note_gap_ms: int,
    gate_ms: int,
    cc_number: int,
    cc_step: int,
    abort: Optional[RunAbort] = None,
) -> tuple[int, int]:
    ch = midi_channel_1based - 1
    start = time.monotonic()
    note_on_count = 0
    cc_count = 0
    i = 0
    cc_val = 0

    while time.monotonic() - start < duration_s:
        if abort is not None and abort.check() is not None:
            break
        note = root_note + (i % semitone_span)
        out_port.send(mido.Message("note_on", channel=ch, note=note, velocity=98))
        note_on_count += 1
        out_port.send(mido.Message("control_change", channel=ch, control=cc_number, value=cc_val))
        cc_count += 1

        time.sleep(max(gate_ms, 1) / 1000.0)
        out_port.send(mido.Message("note_off", channel=ch, note=note, velocity=0))
        time.sleep(max(note_gap_ms, 1) / 1000.0)

        i += 1
        cc_val = (cc_val + cc_step) % 128
        _drain_input_messages(in_port)

    return note_on_count, cc_count


def _stream_overdub_wrap_note_off_test(
    out_port: mido.ports.BaseOutput,
    in_port: mido.ports.BaseInput,
    *,
    midi_channel_1based: int,
    wrap_note: int,
    loop_bars: int,
    target_bars: int,
    phase_start_delay_bars: int,
    phase_start_delay_beats: int,
    max_seconds_guard: float,
    abort: Optional[RunAbort] = None,
) -> tuple[int, int, int, dict[str, float]]:
    """Hold one note across a loop wrap, then release in the head window."""
    if loop_bars <= 0:
        raise ValueError("loop_bars must be > 0")
    if phase_start_delay_bars < 0:
        raise ValueError("phase_start_delay_bars must be >= 0")
    if phase_start_delay_beats < 0:
        raise ValueError("phase_start_delay_beats must be >= 0")

    loop_clocks = loop_bars * MIDI_CLOCKS_PER_BAR
    # Run one extra loop cycle so tail-on and post-wrap head-off both fit.
    target_clocks = max(target_bars, loop_bars + 1) * MIDI_CLOCKS_PER_BAR
    phase_delay_clocks = (phase_start_delay_bars * MIDI_CLOCKS_PER_BAR) + (
        phase_start_delay_beats * 24
    )
    ch = midi_channel_1based - 1
    note_on_sent = False
    note_off_sent = False
    note_on_pos: Optional[int] = None
    phase_clock_count = 0
    clock_count_total = 0
    start = time.monotonic()

    while phase_clock_count < target_clocks:
        if abort is not None and abort.check() is not None:
            break
        if time.monotonic() - start > max_seconds_guard:
            break
        msg = in_port.poll()
        if msg is None:
            time.sleep(0.0005)
            continue
        if msg.type != "clock":
            continue
        clock_count_total += 1
        phase_clock_count += 1
        if phase_clock_count <= phase_delay_clocks:
            continue
        pos_in_loop = (phase_clock_count - phase_delay_clocks - 1) % loop_clocks
        if not note_on_sent and pos_in_loop >= loop_clocks - 24:
            out_port.send(mido.Message("note_on", channel=ch, note=wrap_note, velocity=100))
            note_on_sent = True
            note_on_pos = pos_in_loop
        elif (
            note_on_sent
            and not note_off_sent
            and note_on_pos is not None
            and pos_in_loop < note_on_pos
        ):
            out_port.send(mido.Message("note_off", channel=ch, note=wrap_note, velocity=0))
            note_off_sent = True

    if note_on_sent and not note_off_sent:
        out_port.send(mido.Message("note_off", channel=ch, note=wrap_note, velocity=0))
        note_off_sent = True

    timing = {
        "grid_steps_emitted": 1.0 if note_on_sent else 0.0,
        "max_abs_grid_jitter_clocks": 0.0,
        "mean_abs_grid_jitter_clocks": 0.0,
        "stop_press_sent_during_stream": 0.0,
        "wrap_note_on_sent": 1.0 if note_on_sent else 0.0,
        "wrap_note_off_sent": 1.0 if note_off_sent else 0.0,
    }
    return int(note_on_sent), int(note_off_sent), clock_count_total, timing


def _stream_pattern_for_seconds(
    out_port: mido.ports.BaseOutput,
    *,
    midi_channel_1based: int,
    low_note: int,
    high_note: int,
    step_seconds: float,
    duration_s: float,
    cc_number: int,
    cc_step: int,
    abort: Optional[RunAbort] = None,
) -> tuple[int, int]:
    """Send a note pattern paced by wall-clock duration."""
    if high_note < low_note:
        raise ValueError("high_note must be >= low_note")
    if step_seconds <= 0:
        raise ValueError("step_seconds must be > 0")
    if duration_s <= 0:
        return 0, 0

    ch = midi_channel_1based - 1
    note_range = list(range(low_note, high_note + 1))
    note_index = 0
    note_on_count = 0
    cc_count = 0
    cc_val = 0
    start = time.monotonic()

    while True:
        if abort is not None and abort.check() is not None:
            break
        elapsed = time.monotonic() - start
        if elapsed >= duration_s:
            break
        note = note_range[note_index % len(note_range)]
        out_port.send(mido.Message("note_on", channel=ch, note=note, velocity=98))
        out_port.send(mido.Message("control_change", channel=ch, control=cc_number, value=cc_val))
        note_on_count += 1
        cc_count += 1

        gate_seconds = max(0.005, step_seconds * 0.5)
        time.sleep(gate_seconds)
        out_port.send(mido.Message("note_off", channel=ch, note=note, velocity=0))

        rest = step_seconds - gate_seconds
        if rest > 0:
            time.sleep(rest)

        note_index += 1
        cc_val = (cc_val + cc_step) % 128

    return note_on_count, cc_count


def _parse_cap_micros(line: str) -> Optional[int]:
    if not line.startswith("#CAP,"):
        return None
    parts = line.split(",", 2)
    if len(parts) < 2:
        return None
    try:
        return int(parts[1])
    except ValueError:
        return None


def _parse_disp_track_state(line: str) -> Optional[str]:
    marker = ",DISP,"
    if marker not in line or not line.startswith("#CAP,"):
        return None
    tail = line.split(marker, 1)[1]
    parts = tail.split(",")
    if len(parts) < 2:
        return None
    return parts[1].strip()


def _count_capture_transitions(lines: list[str]) -> dict[tuple[str, str], int]:
    counts: dict[tuple[str, str], int] = {}
    for line in lines:
        marker = ",ST,Track,"
        if marker not in line:
            continue
        tail = line.split(marker, 1)[1]
        parts = tail.split(",")
        if len(parts) < 2:
            continue
        key = (parts[0].strip(), parts[1].strip())
        counts[key] = counts.get(key, 0) + 1
    return counts


def _count_capture_record_markers(lines: list[str]) -> tuple[int, int]:
    reca = 0
    recs = 0
    for line in lines:
        if "#CAP," in line and ",RECA," in line:
            reca += 1
        if "#CAP," in line and ",RECS," in line:
            recs += 1
    return reca, recs


def _count_capture_state_entries(lines: list[str]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for line in lines:
        marker = ",ST,Track,"
        if marker not in line:
            continue
        tail = line.split(marker, 1)[1]
        parts = tail.split(",")
        if len(parts) < 2:
            continue
        to_state = parts[1].strip()
        counts[to_state] = counts.get(to_state, 0) + 1
    return counts


def _record_entry_to_recording_count(
    transition_counts: dict[tuple[str, str], int],
) -> int:
    """Count record arm from ARMED, EMPTY, or STOPPED (transport restart before record)."""
    total = 0
    for from_state in ("ARMED", "EMPTY", "STOPPED"):
        total += transition_counts.get((from_state, "RECORDING"), 0)
    return total


_CLEAR_RESULTS_EXEMPT_UNDO_PRUNE = frozenset(
    {
        "already_empty_ignored",
        "already_empty_skip",
        "clear_log_confirmed",
        "cleared_without_empty_transition",
        "empty_transition",
    }
)


def _clear_undo_prune_gate_ok(
    clear_precondition_results: list[dict[str, object]],
    clear_undo_prune: dict[str, object],
) -> bool:
    """Pass when clear was confirmed without global-undo prune log, or prune left stack empty."""
    if any(
        str(r.get("result", "")) in _CLEAR_RESULTS_EXEMPT_UNDO_PRUNE
        for r in clear_precondition_results
    ):
        return True
    return bool(clear_undo_prune.get("found")) and bool(clear_undo_prune.get("remaining_zero"))


def _extract_clear_undo_prune(lines: list[str]) -> dict[str, object]:
    pattern = re.compile(
        r"Global undo pruned for slot\s+(?P<slot>\d+):\s+removed=(?P<removed>\d+)\s+remaining=(?P<remaining>\d+)\s+cursor=(?P<cursor>\d+)"
    )
    last_match: Optional[dict[str, int]] = None
    for line in lines:
        m = pattern.search(line)
        if not m:
            continue
        last_match = {
            "slot": int(m.group("slot")),
            "removed": int(m.group("removed")),
            "remaining": int(m.group("remaining")),
            "cursor": int(m.group("cursor")),
        }
    if last_match is None:
        return {
            "found": False,
            "remaining_zero": False,
        }
    return {
        "found": True,
        "remaining_zero": last_match["remaining"] == 0,
        "slot": last_match["slot"],
        "removed": last_match["removed"],
        "remaining": last_match["remaining"],
        "cursor": last_match["cursor"],
    }


def _serial_has_clear_ignored_empty(lines: list[str], *, after_index: int = 0) -> bool:
    for line in lines[after_index:]:
        if "Clear ignored" in line and (
            "track is empty" in line or "selected slot is empty" in line
        ):
            return True
    return False


def _serial_has_clear_completed(lines: list[str], *, after_index: int = 0) -> bool:
    for line in lines[after_index:]:
        if "MIDI: Clear Track" in line:
            return True
        if "MIDI: Clear selected slot" in line:
            return True
        if "Track cleared @" in line:
            return True
        if "Clear reset non-empty track state to EMPTY" in line:
            return True
    return False


def _count_reca_markers(lines: list[str]) -> int:
    return sum(1 for line in lines if ",RECA," in line and "#CAP," in line)


def _recording_transition_baseline(lines: list[str]) -> int:
    return sum(
        count
        for (from_state, to_state), count in _count_capture_transitions(lines).items()
        if to_state == "RECORDING"
    )


def _armed_transition_baseline(lines: list[str]) -> int:
    return sum(
        count
        for (from_state, to_state), count in _count_capture_transitions(lines).items()
        if to_state == "ARMED"
    )


# Loop-slot row MIDI (Ch.16 notes 50-57): HITL uses slot short press for record arm when
# --loop-slot is set; preview/selected-slot gestures beyond that remain in
# openspec/changes/slot-performance-interaction/.
LOOP_SELECT_NOTE_BASE = 50  # reserved for slot-performance revert


def _log_loop_slot_report_only(args: Any, *, track_index: int) -> None:
    """Log --loop-slot intent before capture (Loops-row MIDI used at record arm when set)."""
    loop_slot = int(getattr(args, "loop_slot", 0) or 0)
    if loop_slot <= 0:
        return
    print(
        f"[info] --loop-slot {loop_slot} on track {track_index} "
        "(record arm uses Loops row note "
        f"{LOOP_SELECT_NOTE_BASE + loop_slot - 1}; omit --loop-slot to use Record button 36)"
    )


def _stop_transport_before_clear(
    out_port: mido.ports.BaseOutput,
    in_port: mido.ports.BaseInput,
    serial_collector: Optional[Any],
    args: Any,
    *,
    press_ms: int,
    phase_wait_ms: int,
) -> bool:
    """Stop global transport before clear (clear while PLAYING leaves slot empty but not EMPTY).

    Returns True when a transport-stop press was sent (caller must restart before record).
    """
    from hitl.serial_transport import use_serial_transport_proxy

    latest: Optional[str] = None
    if serial_collector is not None:
        latest = _latest_track_state(serial_collector.snapshot())

    should_stop = _clock_seen_within(in_port, 0.5)
    if not should_stop and serial_collector is not None:
        should_stop = use_serial_transport_proxy(args, serial_collector)
    if latest in ("PLAYING", "OVERDUBBING", "RECORDING", "STOPPED_RECORDING"):
        should_stop = True
    if not should_stop:
        return False
    print("[info] transport stop before clear long press")
    _send_short_press(
        out_port,
        note=GLOBAL_TRANSPORT_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=press_ms,
    )
    time.sleep(phase_wait_ms / 1000.0)
    return True


def _restart_transport_after_clear(
    out_port: mido.ports.BaseOutput,
    in_port: mido.ports.BaseInput,
    serial_collector: Optional[Any],
    args: Any,
    *,
    press_ms: int,
    phase_wait_ms: int,
    abort: Optional[RunAbort],
) -> bool:
    """Resume global transport after stop-before-clear so record/overdub have a running clock."""
    print("[info] transport start after clear (before record)")
    _send_short_press(
        out_port,
        note=GLOBAL_TRANSPORT_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=press_ms,
    )
    time.sleep(phase_wait_ms / 1000.0)
    clock_ok, using_serial_proxy = _ensure_midi_clock_or_serial_proxy(
        in_port,
        out_port,
        serial_collector,
        args,
        min_clocks=24,
        timeout_s=3.0,
        abort=abort,
    )
    if not clock_ok:
        print(
            "[error] MIDI clock / serial transport proxy missing after transport restart; "
            "aborting track run."
        )
        return False
    if using_serial_proxy:
        proxy_bpm = _resolve_wall_tempo_for_proxy(serial_collector, args, using_serial_proxy=True)
        print(
            "[info] Transport running after clear via wall-clock tempo proxy "
            f"({(proxy_bpm or args.tempo_bpm):.1f} BPM)."
        )
    return True


def _send_record_arm_press(
    out_port: mido.ports.BaseOutput,
    *,
    press_ms: int,
    loop_slot: Optional[int] = None,
) -> None:
    """Arm/start record via main Record button or a loop-row slot short press.

    When ``loop_slot`` is set (1-8), press Loops row note 50+(slot-1) so capture targets
    that slot (``TOGGLE_RECORD_FOR_SLOT``). Otherwise use Record button (36) on the
    device-selected slot.
    """
    if loop_slot is not None and loop_slot > 0:
        _send_short_press(
            out_port,
            note=LOOP_SELECT_NOTE_BASE + (loop_slot - 1),
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=press_ms,
        )
        return
    _send_short_press(
        out_port,
        note=RECORD_BUTTON_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=press_ms,
    )


def _serial_has_recording_started(lines: list[str], *, after_index: int = 0) -> bool:
    suffix = lines[after_index:]
    if any(",RECA," in line and "#CAP," in line for line in suffix):
        return True
    if any("Recording started" in line for line in suffix):
        return True
    if any("MIDI Button A: Start Recording" in line for line in suffix):
        return True
    if _suffix_shows_disp_recording(lines, after_index=after_index):
        return True
    latest = _latest_track_state(lines)
    if latest == "RECORDING":
        return True
    return False


def _wait_for_recording_started(
    collector: SerialCaptureCollector,
    *,
    baseline_len: int,
    baseline_reca: int,
    baseline_recording_transitions: int,
    baseline_latest_state: Optional[str],
    baseline_armed_transitions: int = 0,
    timeout_s: float,
    serial_grace_s: float = 3.0,
    abort: Optional[RunAbort] = None,
) -> bool:
    """Wait for firmware evidence that record actually started (not play/stop toggle)."""

    def _evidence(lines: list[str]) -> bool:
        suffix = lines[baseline_len:]
        if _count_reca_markers(lines) > baseline_reca:
            return True
        if any("Recording started" in line for line in suffix):
            return True
        if any("MIDI Button A: Start Recording" in line for line in suffix):
            return True
        if any("Start Recording" in line for line in suffix):
            return True
        if any("armed, waiting for clock to start recording" in line for line in suffix):
            return True
        if _suffix_shows_disp_recording(lines, after_index=baseline_len):
            return True
        latest = _latest_track_state(lines)
        if latest == "RECORDING" and baseline_latest_state != "RECORDING":
            return True
        if latest == "ARMED" and baseline_latest_state not in ("ARMED", "RECORDING"):
            return True
        recording_transitions = sum(
            count
            for (from_state, to_state), count in _count_capture_transitions(lines).items()
            if to_state == "RECORDING"
        )
        if recording_transitions > baseline_recording_transitions:
            return True
        armed_transitions = sum(
            count
            for (from_state, to_state), count in _count_capture_transitions(lines).items()
            if to_state == "ARMED"
        )
        return armed_transitions > baseline_armed_transitions

    deadline = time.monotonic() + max(timeout_s, 0.0)
    while time.monotonic() < deadline:
        if abort is not None and abort.check() is not None:
            return False
        if _evidence(collector.snapshot()):
            return True
        time.sleep(0.01)

    grace_deadline = time.monotonic() + max(serial_grace_s, 0.0)
    while time.monotonic() < grace_deadline:
        if abort is not None and abort.check() is not None:
            return False
        if _evidence(collector.snapshot()):
            return True
        time.sleep(0.02)
    return False


def _latest_track_state(lines: list[str]) -> Optional[str]:
    latest: Optional[str] = None
    latest_ts = -1
    for line in lines:
        state: Optional[str] = None
        if ",ST,Track," in line:
            tail = line.split(",ST,Track,", 1)[1]
            parts = tail.split(",")
            if len(parts) >= 2:
                state = parts[1].strip()
        else:
            state = _parse_disp_track_state(line)
        if state is None:
            continue
        ts = _parse_cap_micros(line)
        if ts is None:
            ts = 0
        if ts >= latest_ts:
            latest_ts = ts
            latest = state
    return latest


def _suffix_shows_disp_recording(lines: list[str], *, after_index: int = 0) -> bool:
    for line in lines[after_index:]:
        state = _parse_disp_track_state(line)
        if state == "RECORDING":
            return True
    return False


def _serial_suggests_loop_content(lines: list[str]) -> bool:
    if _extract_revt_note_on_ticks(lines):
        return True
    latest = _latest_track_state(lines)
    if latest in ("RECORDING", "PLAYING", "OVERDUBBING", "STOPPED_RECORDING"):
        return True
    if latest == "STOPPED":
        for line in lines:
            if ",RECS," in line and "#CAP," in line:
                return True
    return False


def _can_skip_clear_before_record(lines: list[str]) -> bool:
    """Track already empty/idle — no clear long-press needed.

    Requires positive serial evidence. Missing #CAP ST lines (fresh USB open, capture
  flush lag) is not treated as empty — the slot may still hold SD-loaded loop data.
    """
    latest = _latest_track_state(lines)
    if latest == "EMPTY":
        return True
    if _serial_has_clear_ignored_empty(lines):
        return True
    return False


def _track_cleared_for_record(lines: list[str]) -> bool:
    latest = _latest_track_state(lines)
    if latest == "EMPTY":
        return True
    if latest == "ARMED":
        return not _serial_suggests_loop_content(lines)
    if latest == "STOPPED" and not _serial_suggests_loop_content(lines):
        return True
    if _serial_has_clear_completed(lines):
        return True
    return False


def _verify_phase_note_pairs(
    lines: list[str],
    *,
    midi_channel_1based: int,
    phase_start_ts: Optional[int],
    phase_stop_ts: Optional[int],
    low_note: int,
    high_note: int,
) -> dict[str, object]:
    if phase_start_ts is None or phase_stop_ts is None or phase_stop_ts <= phase_start_ts:
        return {
            "note_on_count": 0,
            "note_off_count": 0,
            "unmatched_open_notes": 0,
            "out_of_range_count": 0,
            "sequence_mismatch_count": 0,
            "phase_missing": True,
        }

    note_on_count = 0
    note_off_count = 0
    out_of_range_count = 0
    sequence_mismatch_count = 0
    active_counts: dict[int, int] = {}
    pitch_sequence: list[int] = []

    for line in lines:
        if ",MI,U," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 8:
            continue
        try:
            ts = int(parts[1])
            msg_type = int(parts[4])
            channel = int(parts[5])
            note = int(parts[6])
        except ValueError:
            continue
        if channel != midi_channel_1based:
            continue
        if ts < phase_start_ts or ts >= phase_stop_ts:
            continue
        if msg_type == 144:
            note_on_count += 1
            active_counts[note] = active_counts.get(note, 0) + 1
            pitch_sequence.append(note)
            if note < low_note or note > high_note:
                out_of_range_count += 1
        elif msg_type == 128:
            note_off_count += 1
            active_counts[note] = max(0, active_counts.get(note, 0) - 1)

    if low_note <= high_note and pitch_sequence:
        expected = pitch_sequence[0]
        span = high_note - low_note + 1
        for observed in pitch_sequence:
            if observed != expected:
                sequence_mismatch_count += 1
                expected = observed
            expected = low_note + ((expected - low_note + 1) % span)

    return {
        "note_on_count": note_on_count,
        "note_off_count": note_off_count,
        "unmatched_open_notes": sum(v for v in active_counts.values() if v > 0),
        "out_of_range_count": out_of_range_count,
        "sequence_mismatch_count": sequence_mismatch_count,
        "phase_missing": False,
    }


def _last_recs_row(recs_lengths: list[dict[str, int]]) -> Optional[dict[str, int]]:
    if not recs_lengths:
        return None
    return recs_lengths[-1]


def _verify_record_loop_length(
    recs_lengths: list[dict[str, int]],
    *,
    record_bars: int,
    ticks_per_bar: int = TICKS_PER_BAR,
) -> dict[str, object]:
    if record_bars <= 0:
        return {"phase_disabled": True}
    last = _last_recs_row(recs_lengths)
    if last is None:
        return {
            "phase_disabled": False,
            "recs_missing": True,
            "expected_final_length": record_bars * ticks_per_bar,
            "actual_final_length": None,
            "final_length_ok": False,
        }
    expected = record_bars * ticks_per_bar
    actual = int(last["final_length"])
    return {
        "phase_disabled": False,
        "recs_missing": False,
        "expected_final_length": expected,
        "actual_final_length": actual,
        "final_length_ok": actual == expected,
        "raw_length": int(last["raw_length"]),
    }


def _verify_record_note_span(
    recs_lengths: list[dict[str, int]],
    *,
    record_bars: int,
    note_on_count: int,
    ticks_per_bar: int = TICKS_PER_BAR,
    ticks_per_step: int = TICKS_PER_16TH_STEP,
    span_min_ratio: float = DEFAULT_RECORD_NOTE_SPAN_MIN_RATIO,
) -> dict[str, object]:
    if record_bars <= 0:
        return {"phase_disabled": True}
    last = _last_recs_row(recs_lengths)
    if last is None:
        return {
            "phase_disabled": False,
            "recs_missing": True,
            "expected_min_raw_length": None,
            "actual_raw_length": None,
            "raw_span_ok": False,
        }
    if note_on_count <= 1:
        return {
            "phase_disabled": False,
            "recs_missing": False,
            "expected_min_raw_length": 0,
            "actual_raw_length": int(last["raw_length"]),
            "raw_span_ok": False,
            "note_on_count": note_on_count,
        }

    expected_span = max(
        (record_bars * 16 - 1) * ticks_per_step,
        (note_on_count - 1) * ticks_per_step,
    )
    expected_min = int(expected_span * span_min_ratio)
    actual = int(last["raw_length"])
    return {
        "phase_disabled": False,
        "recs_missing": False,
        "note_on_count": note_on_count,
        "expected_span_ticks": expected_span,
        "expected_min_raw_length": expected_min,
        "actual_raw_length": actual,
        "raw_span_ok": actual >= expected_min,
    }


def _extract_recs_lengths(lines: list[str]) -> list[dict[str, int]]:
    rows: list[dict[str, int]] = []
    for line in lines:
        if ",RECS," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 6:
            continue
        if parts[3] not in ("stop", "stopToStopped"):
            continue
        try:
            ts = int(parts[1])
        except ValueError:
            continue
        ints: list[int] = []
        for token in parts[3:]:
            token = token.strip()
            if token.isdigit():
                ints.append(int(token))
        if len(ints) < 3:
            continue
        rows.append(
            {
                "timestamp": ts,
                "raw_length": ints[-3],
                "final_length": ints[-2],
            }
        )
    return rows


def _extract_record_stop_stage_rows(lines: list[str]) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for line in lines:
        if ",RECS,stage," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 12:
            continue
        try:
            ts = int(parts[1])
            elapsed_us = int(parts[5])
            duration_us = int(parts[6])
            heap_before = int(parts[7])
            heap_after = int(parts[8])
            event_count = int(parts[9])
            chunk_ref_count = int(parts[10])
        except ValueError:
            continue
        rows.append(
            {
                "timestamp": ts,
                "stage": parts[4].strip(),
                "elapsed_us": elapsed_us,
                "duration_us": duration_us,
                "heap_before": heap_before,
                "heap_after": heap_after,
                "event_count": event_count,
                "chunk_ref_count": chunk_ref_count,
                "outcome": parts[11].strip(),
            }
        )
    return rows


def _summarize_record_stop_stages(
    stage_rows: list[dict[str, object]],
) -> dict[str, object]:
    completed_outcomes = {
        "entered",
        "ok",
        "published",
        "deferred",
        "requested",
        "skipped",
        "skipped_empty",
    }
    by_stage_all: dict[str, list[dict[str, object]]] = {}
    for row in stage_rows:
        stage = str(row.get("stage", ""))
        by_stage_all.setdefault(stage, []).append(row)

    by_stage_completed: dict[str, dict[str, object]] = {}
    for stage, rows in by_stage_all.items():
        for row in rows:
            if str(row.get("outcome", "")) in completed_outcomes:
                by_stage_completed[stage] = row
                break

    first_missing_stage: Optional[str] = None
    for stage in STOP_STAGE_SEQUENCE:
        if stage not in by_stage_completed:
            first_missing_stage = stage
            break

    slowest_completed_stage: Optional[dict[str, object]] = None
    completed_rows = [by_stage_completed[s] for s in STOP_STAGE_SEQUENCE if s in by_stage_completed]
    if completed_rows:
        slowest_completed_stage = max(completed_rows, key=lambda row: int(row.get("duration_us", 0)))

    return {
        "rows": stage_rows,
        "first_missing_stage": first_missing_stage,
        "slowest_completed_stage": slowest_completed_stage,
    }


def _extract_persistence_rows(lines: list[str]) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for line in lines:
        if ",PERS," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 8:
            continue
        try:
            ts = int(parts[1])
            duration_us = int(parts[4])
            heap_before = int(parts[5])
            heap_after = int(parts[6])
        except ValueError:
            continue
        rows.append(
            {
                "timestamp": ts,
                "stage": parts[3].strip(),
                "duration_us": duration_us,
                "heap_before": heap_before,
                "heap_after": heap_after,
                "outcome": parts[7].strip(),
            }
        )
    return rows


def _summarize_persistence_handoff(
    persistence_rows: list[dict[str, object]],
    *,
    record_stop_ts: Optional[int],
) -> dict[str, object]:
    if record_stop_ts is None:
        return {"phase_disabled": True}

    rows_after_stop = [row for row in persistence_rows if int(row["timestamp"]) >= record_stop_ts]
    by_stage: dict[str, dict[str, object]] = {}
    for stage in PERSISTENCE_STAGE_SEQUENCE:
        for row in rows_after_stop:
            if str(row.get("stage", "")) == stage:
                by_stage[stage] = row
                break

    first_missing_stage: Optional[str] = None
    for stage in PERSISTENCE_STAGE_SEQUENCE:
        if stage not in by_stage:
            first_missing_stage = stage
            break

    result_row = by_stage.get("result")
    result_ok = result_row is not None and str(result_row.get("outcome", "")) == "ok"
    return {
        "phase_disabled": False,
        "rows": rows_after_stop,
        "first_missing_stage": first_missing_stage,
        "request": by_stage.get("request"),
        "dispatch": by_stage.get("dispatch"),
        "result": result_row,
        "result_ok": result_ok,
    }


def _summarize_ram2_headroom(
    record_stop_stage_rows: list[dict[str, object]],
    persistence_rows: list[dict[str, object]],
) -> dict[str, object]:
    samples: list[dict[str, object]] = []
    for row in record_stop_stage_rows:
        stage_name = str(row.get("stage", ""))
        timestamp = int(row.get("timestamp", 0))
        for key in ("heap_before", "heap_after"):
            if key not in row:
                continue
            samples.append(
                {
                    "source": "record_stop_stage",
                    "stage": stage_name,
                    "field": key,
                    "timestamp": timestamp,
                    "free_ram2_bytes": int(row[key]),
                }
            )
    for row in persistence_rows:
        stage_name = str(row.get("stage", ""))
        timestamp = int(row.get("timestamp", 0))
        for key in ("heap_before", "heap_after"):
            if key not in row:
                continue
            free_ram2 = int(row[key])
            if free_ram2 <= 0:
                continue
            samples.append(
                {
                    "source": "persistence",
                    "stage": stage_name,
                    "field": key,
                    "timestamp": timestamp,
                    "free_ram2_bytes": free_ram2,
                }
            )

    if not samples:
        return {
            "phase_disabled": True,
            "sample_count": 0,
            "min_free_ram2_bytes": None,
            "min_sample": None,
        }

    min_sample = min(samples, key=lambda sample: int(sample["free_ram2_bytes"]))
    return {
        "phase_disabled": False,
        "sample_count": len(samples),
        "min_free_ram2_bytes": int(min_sample["free_ram2_bytes"]),
        "min_sample": min_sample,
    }


def _verify_record_stop_ram2_floor(
    record_stop_stage_rows: list[dict[str, object]],
    *,
    floor_bytes: int,
    warn_bytes: int,
    enabled: bool,
) -> dict[str, object]:
    if not enabled:
        return {
            "phase_disabled": True,
            "enabled": False,
            "floor_bytes": floor_bytes,
            "warn_bytes": warn_bytes,
            "record_stop_heap_before": None,
            "record_stop_heap_after": None,
            "floor_ok": True,
            "warn_ok": True,
            "record_stop_row_missing": False,
        }

    record_stop_row: Optional[dict[str, object]] = None
    for row in record_stop_stage_rows:
        if str(row.get("stage", "")) == "record_stop":
            record_stop_row = row
            break

    if record_stop_row is None:
        return {
            "phase_disabled": False,
            "enabled": True,
            "floor_bytes": floor_bytes,
            "warn_bytes": warn_bytes,
            "record_stop_heap_before": None,
            "record_stop_heap_after": None,
            "floor_ok": floor_bytes <= 0,
            "warn_ok": True,
            "record_stop_row_missing": True,
        }

    record_stop_heap_before = int(record_stop_row["heap_before"])
    record_stop_heap_after = int(record_stop_row["heap_after"])
    floor_ok = floor_bytes <= 0 or record_stop_heap_before >= floor_bytes
    warn_ok = warn_bytes <= 0 or record_stop_heap_before >= warn_bytes
    return {
        "phase_disabled": False,
        "enabled": True,
        "floor_bytes": floor_bytes,
        "warn_bytes": warn_bytes,
        "record_stop_heap_before": record_stop_heap_before,
        "record_stop_heap_after": record_stop_heap_after,
        "floor_ok": floor_ok,
        "warn_ok": warn_ok,
        "record_stop_row_missing": False,
    }


def _verify_long_run_persistence_result(
    persistence_handoff: dict[str, object],
    *,
    enabled: bool,
) -> dict[str, object]:
    if not enabled:
        return {
            "phase_disabled": True,
            "enabled": False,
            "result_present": False,
            "result_ok": True,
            "outcome": None,
            "stage": None,
        }

    result_row = persistence_handoff.get("result")
    result_row_dict = result_row if isinstance(result_row, dict) else None
    result_present = result_row_dict is not None
    result_ok = bool(persistence_handoff.get("result_ok"))
    outcome: Optional[str] = None
    stage: Optional[str] = None
    if result_row_dict is not None:
        outcome_value = result_row_dict.get("outcome")
        stage_value = result_row_dict.get("stage")
        outcome = str(outcome_value) if outcome_value is not None else None
        stage = str(stage_value) if stage_value is not None else None

    return {
        "phase_disabled": False,
        "enabled": True,
        "result_present": result_present,
        "result_ok": result_ok,
        "outcome": outcome,
        "stage": stage,
    }


def _extract_revt_note_on_ticks(lines: list[str]) -> list[int]:
    """Stored note-on ticks logged for the first record phase.

    REVT emission is deferred (idle flush), so lines can appear after RECS,stop.
    """
    recs_stop_ts: Optional[int] = None
    record_start_ts: Optional[int] = None
    for line in lines:
        if ",RECA," in line and record_start_ts is None:
            try:
                record_start_ts = int(line.split(",")[1])
            except ValueError:
                pass
        if ",RECS,stop," in line and recs_stop_ts is None:
            try:
                recs_stop_ts = int(line.split(",")[1])
            except ValueError:
                pass
            break
    if recs_stop_ts is None:
        return []

    ticks: list[int] = []
    for line in lines:
        if ",REVT," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 6:
            continue
        try:
            ts = int(parts[1])
            tick = int(parts[3])
        except ValueError:
            continue
        if record_start_ts is not None and ts < record_start_ts:
            continue
        ticks.append(tick)
    return ticks


def _verify_stored_record_note_grid(
    lines: list[str],
    *,
    ticks_per_step: int = TICKS_PER_16TH_STEP,
) -> dict[str, object]:
    ticks = _extract_revt_note_on_ticks(lines)
    if len(ticks) < 2:
        return {
            "phase_disabled": False,
            "revt_missing": len(ticks) == 0,
            "note_on_count": len(ticks),
            "grid_ok": False,
        }
    deltas = [ticks[i] - ticks[i - 1] for i in range(1, len(ticks))]
    # Stored spacing follows raw capture ticks (no record quantize); allow one 16th tolerance
    # and a 2x step at pitch-cycle wrap. Long runs may see 1–2 jitter deltas at wrap.
    tol = max(12, ticks_per_step // 2)
    bad = [
        d
        for d in deltas
        if d <= 0
        or (
            abs(d - ticks_per_step) > tol
            and abs(d - 2 * ticks_per_step) > tol
        )
    ]
    bad_budget = max(2, len(deltas) // 400)
    return {
        "phase_disabled": False,
        "revt_missing": False,
        "note_on_count": len(ticks),
        "first_tick": ticks[0],
        "last_tick": ticks[-1],
        "min_delta": min(deltas),
        "max_delta": max(deltas),
        "bad_delta_count": len(bad),
        "bad_delta_budget": bad_budget,
        "grid_ok": len(bad) <= bad_budget,
    }


def _extract_sevt_note_on_ticks(
    lines: list[str],
    *,
    low_note: int,
    high_note: int,
    midi_channel_1based: int,
    after_ts: Optional[int] = None,
    before_ts: Optional[int] = None,
) -> list[int]:
    ticks: list[int] = []
    for line in lines:
        if ",SEVT,N," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 7:
            continue
        try:
            ts = int(parts[1])
            tick = int(parts[4])
            channel = int(parts[5])
            note = int(parts[6])
        except ValueError:
            continue
        if after_ts is not None and ts < after_ts:
            continue
        if before_ts is not None and ts >= before_ts:
            continue
        if channel != midi_channel_1based or note < low_note or note > high_note:
            continue
        ticks.append(tick)
    ticks.sort()
    return ticks


def _extract_overdub_memory_note_count(
    lines: list[str], *, overdub_stop_ts: Optional[int]
) -> Optional[int]:
    if overdub_stop_ts is None:
        return None
    stop_idx: Optional[int] = None
    marker = f"#CAP,{overdub_stop_ts},ST,Track,OVERDUBBING,PLAYING"
    for i, line in enumerate(lines):
        if marker in line:
            stop_idx = i
            break
    if stop_idx is None:
        return None
    last_count: Optional[int] = None
    for line in lines[:stop_idx]:
        m = re.search(r"\[Memory\] notes=(\d+)", line)
        if m:
            last_count = int(m.group(1))
    return last_count


def _verify_stored_overdub_note_span(
    lines: list[str],
    *,
    loop_length_ticks: int,
    overdub_bars: int,
    record_bars: int,
    low_note: int,
    high_note: int,
    midi_channel_1based: int,
    note_on_count: int,
    memory_note_count: Optional[int] = None,
    span_min_ratio: float = DEFAULT_RECORD_NOTE_SPAN_MIN_RATIO,
    grid_step_clocks: int = OVERDUB_GRID_STEP_CLOCKS,
    after_ts: Optional[int] = None,
    before_ts: Optional[int] = None,
) -> dict[str, object]:
    if overdub_bars <= 0 or loop_length_ticks <= 0:
        return {"phase_disabled": True}

    ticks = _extract_sevt_note_on_ticks(
        lines,
        low_note=low_note,
        high_note=high_note,
        midi_channel_1based=midi_channel_1based,
        after_ts=after_ts,
        before_ts=before_ts,
    )
    if grid_step_clocks <= 0:
        grid_step_clocks = OVERDUB_GRID_STEP_CLOCKS
    notes_per_bar = MIDI_CLOCKS_PER_BAR // grid_step_clocks
    expected_note_on_count = max(1, overdub_bars * notes_per_bar - 1)
    step_ticks = grid_step_clocks * TICKS_PER_BAR // MIDI_CLOCKS_PER_BAR
    if len(ticks) < 2:
        return {
            "phase_disabled": False,
            "sevt_missing": len(ticks) == 0,
            "note_on_count": len(ticks),
            "expected_note_on_count": expected_note_on_count,
            "memory_note_count": memory_note_count,
            "count_ok": memory_note_count is not None and memory_note_count >= expected_note_on_count,
            "span_ok": False,
            "bars_match_ok": False,
        }

    first_tick = ticks[0]
    last_tick = ticks[-1]
    span = last_tick - first_tick
    expected_span = max(
        (overdub_bars * notes_per_bar - 1) * step_ticks,
        (note_on_count - 1) * step_ticks,
        (expected_note_on_count - 1) * step_ticks,
    )
    expected_min_span = int(expected_span * span_min_ratio)
    span_ok = span >= expected_min_span and last_tick < loop_length_ticks

    bars_in_loop = loop_length_ticks // TICKS_PER_BAR
    overdub_bars_reached = (last_tick // TICKS_PER_BAR) + 1
    expected_bars = min(overdub_bars, bars_in_loop)
    bars_match_ok = overdub_bars_reached >= max(1, expected_bars - 1)

    if record_bars == overdub_bars:
        revt_ticks = _extract_revt_note_on_ticks(lines)
        if revt_ticks:
            revt_bars_reached = (revt_ticks[-1] // TICKS_PER_BAR) + 1
            bars_match_ok = bars_match_ok and overdub_bars_reached >= max(1, revt_bars_reached - 1)

    count_ok = len(ticks) >= max(1, int(expected_note_on_count * span_min_ratio))
    if memory_note_count is not None:
        count_ok = count_ok and memory_note_count >= max(1, int(expected_note_on_count * span_min_ratio))

    return {
        "phase_disabled": False,
        "sevt_missing": False,
        "note_on_count": len(ticks),
        "expected_note_on_count": expected_note_on_count,
        "memory_note_count": memory_note_count,
        "count_ok": count_ok,
        "first_tick": first_tick,
        "last_tick": last_tick,
        "span_ticks": span,
        "expected_min_span_ticks": expected_min_span,
        "span_ok": span_ok,
        "bars_reached": overdub_bars_reached,
        "expected_bars": expected_bars,
        "bars_match_ok": bars_match_ok,
        "record_bars": record_bars,
        "overdub_bars": overdub_bars,
    }


def _extract_disp_snapshots(lines: list[str], *, after_ts: Optional[int] = None,
                            before_ts: Optional[int] = None) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for line in lines:
        if ",DISP," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 11:
            continue
        try:
            ts = int(parts[1])
            slot = int(parts[3])
            state = parts[4]
            loop_len = int(parts[5])
            epoch_events = int(parts[6])
            visual_notes = int(parts[7])
            frame_notes = int(parts[8])
            buffer_events = int(parts[9])
            published = int(parts[10])
        except ValueError:
            continue
        if after_ts is not None and ts < after_ts:
            continue
        if before_ts is not None and ts > before_ts:
            continue
        rows.append(
            {
                "timestamp": ts,
                "slot": slot,
                "state": state,
                "loop_len": loop_len,
                "epoch_events": epoch_events,
                "visual_notes": visual_notes,
                "frame_notes": frame_notes,
                "buffer_events": buffer_events,
                "published": published,
            }
        )
    return rows


def _verify_display_snapshots(
    lines: list[str],
    *,
    boundaries: dict[str, Optional[int]],
    loop_length_ticks: int,
    window_us: int = 8_000_000,
) -> dict[str, object]:
    issues: list[str] = []
    overdub_stop_ts = boundaries.get("overdub_stop_ts")
    snapshots_after_overdub: list[dict[str, object]] = []
    snapshots_after_transport_stop: list[dict[str, object]] = []
    transport_stop_ts: Optional[int] = None

    if overdub_stop_ts is not None:
        snapshots_after_overdub = _extract_disp_snapshots(
            lines,
            after_ts=overdub_stop_ts,
            before_ts=overdub_stop_ts + window_us,
        )

    for line in lines:
        if ",ST,Track,PLAYING,STOPPED" not in line:
            continue
        parts = line.split(",")
        if len(parts) < 5:
            continue
        try:
            ts = int(parts[1])
        except ValueError:
            continue
        if overdub_stop_ts is not None and ts < overdub_stop_ts:
            continue
        transport_stop_ts = ts

    if transport_stop_ts is not None:
        snapshots_after_transport_stop = _extract_disp_snapshots(
            lines,
            after_ts=transport_stop_ts,
            before_ts=transport_stop_ts + window_us,
        )

    def _phase_ok(snaps: list[dict[str, object]]) -> bool:
        if loop_length_ticks <= 0:
            return True
        for row in snaps:
            if int(row["loop_len"]) <= 0:
                continue
            if int(row["published"]) != 1:
                continue
            if int(row["frame_notes"]) > 0:
                return True
        return False

    overdub_ok = _phase_ok(snapshots_after_overdub)
    transport_ok = _phase_ok(snapshots_after_transport_stop)

    if overdub_stop_ts is not None and not overdub_ok:
        issues.append("display_empty_after_overdub_stop")
    if transport_stop_ts is not None and not transport_ok:
        issues.append("display_empty_after_transport_stop")

    return {
        "overdub_stop_ts": overdub_stop_ts,
        "transport_stop_ts": transport_stop_ts,
        "snapshots_after_overdub": snapshots_after_overdub[-5:],
        "snapshots_after_transport_stop": snapshots_after_transport_stop[-5:],
        "overdub_ok": overdub_ok,
        "transport_ok": transport_ok,
        "issues": issues,
    }


def _extract_wrap_pairs(lines: list[str], *, after_ts: Optional[int] = None) -> list[dict[str, int]]:
    rows: list[dict[str, int]] = []
    for line in lines:
        if ",WRAP," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 7:
            continue
        try:
            ts = int(parts[1])
            on_tick = int(parts[3])
            off_tick = int(parts[4])
            ch = int(parts[5])
            note = int(parts[6])
        except ValueError:
            continue
        if after_ts is not None and ts < after_ts:
            continue
        rows.append(
            {
                "timestamp": ts,
                "on_tick": on_tick,
                "off_tick": off_tick,
                "ch": ch,
                "note": note,
            }
        )
    return rows


def _extract_sevt_events(lines: list[str], *, after_ts: Optional[int] = None) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for line in lines:
        if ",SEVT," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 7:
            continue
        try:
            ts = int(parts[1])
            kind = parts[3]
            tick = int(parts[4])
            ch = int(parts[5])
            note = int(parts[6])
        except ValueError:
            continue
        if after_ts is not None and ts < after_ts:
            continue
        rows.append(
            {
                "timestamp": ts,
                "is_on": kind == "N",
                "tick": tick,
                "ch": ch,
                "note": note,
            }
        )
    return rows


def _verify_overdub_wrap_storage(
    lines: list[str],
    *,
    boundaries: dict[str, Optional[int]],
    loop_length_ticks: int,
    wrap_test_ran: bool,
    wrap_window_ticks: int = TICKS_PER_BAR,
) -> dict[str, object]:
    overdub_stop_ts = boundaries.get("overdub_stop_ts")
    if overdub_stop_ts is None or loop_length_ticks <= 0:
        return {"phase_disabled": True}

    wrap_pairs = _extract_wrap_pairs(lines, after_ts=overdub_stop_ts)
    sevts = _extract_sevt_events(lines, after_ts=overdub_stop_ts)
    if not wrap_test_ran:
        return {
            "phase_disabled": False,
            "wrap_test_ran": False,
            "loop_length_ticks": loop_length_ticks,
            "wrap_pair_count": len(wrap_pairs),
            "sevt_count": len(sevts),
            "pairs_ok": True,
            "wrap_pairs": [],
            "issues": [],
        }

    head_end = min(wrap_window_ticks, loop_length_ticks)
    tail_start = (
        loop_length_ticks - min(wrap_window_ticks, loop_length_ticks)
        if loop_length_ticks > wrap_window_ticks
        else 0
    )
    close_tick = loop_length_ticks - 1

    issues: list[str] = []
    checked_pairs: list[dict[str, object]] = []
    pairs_ok = True

    for pair in wrap_pairs:
        row: dict[str, object] = dict(pair)
        geometry_ok = (
            pair["off_tick"] < head_end
            and pair["on_tick"] >= tail_start
            and pair["on_tick"] < loop_length_ticks
            and pair["off_tick"] < loop_length_ticks
        )
        has_on = any(
            s["is_on"]
            and s["ch"] == pair["ch"]
            and s["note"] == pair["note"]
            and s["tick"] == pair["on_tick"]
            for s in sevts
        )
        has_off = any(
            (not s["is_on"])
            and s["ch"] == pair["ch"]
            and s["note"] == pair["note"]
            and s["tick"] == pair["off_tick"]
            for s in sevts
        )
        synthetic_close = any(
            (not s["is_on"])
            and s["ch"] == pair["ch"]
            and s["note"] == pair["note"]
            and s["tick"] == close_tick
            for s in sevts
        )
        row["geometry_ok"] = geometry_ok
        row["sevt_on_ok"] = has_on
        row["sevt_off_ok"] = has_off
        row["synthetic_loop_end_off"] = synthetic_close
        row_ok = geometry_ok and has_on and has_off and not synthetic_close
        row["ok"] = row_ok
        if not geometry_ok:
            if "overdub_wrap_pair_geometry_bad" not in issues:
                issues.append("overdub_wrap_pair_geometry_bad")
            if pair["on_tick"] >= loop_length_ticks or pair["off_tick"] >= loop_length_ticks:
                issues.append("overdub_wrap_tick_out_of_range")
        if not has_on or not has_off:
            issues.append("overdub_wrap_pair_sevt_missing")
        if synthetic_close:
            issues.append("overdub_wrap_synthetic_loop_end_off")
        if not row_ok:
            pairs_ok = False
        checked_pairs.append(row)

    if wrap_test_ran and not wrap_pairs:
        issues.append("overdub_wrap_test_no_wrap_pair")
        pairs_ok = False

    return {
        "phase_disabled": False,
        "wrap_test_ran": wrap_test_ran,
        "loop_length_ticks": loop_length_ticks,
        "wrap_pair_count": len(wrap_pairs),
        "sevt_count": len(sevts),
        "pairs_ok": pairs_ok and not issues,
        "wrap_pairs": checked_pairs,
        "issues": issues,
    }


def _extract_capture_cleanup_rows(lines: list[str]) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for line in lines:
        if ",CLN," not in line:
            continue
        parts = line.strip().split(",")
        if len(parts) < 6 or parts[0] != "#CAP" or parts[2] != "CLN":
            continue
        try:
            ts = int(parts[1])
            phase = parts[3]
            kind = parts[4]
            count = int(parts[5])
        except ValueError:
            continue
        rows.append({"ts": ts, "phase": phase, "kind": kind, "count": count})
    return rows


def _summarize_capture_cleanup_by_phase_kind(
    rows: list[dict[str, object]],
) -> dict[str, dict[str, int]]:
    totals: dict[str, dict[str, int]] = {}
    for row in rows:
        phase = str(row["phase"])
        kind = str(row["kind"])
        totals.setdefault(phase, {})
        totals[phase][kind] = totals[phase].get(kind, 0) + int(row["count"])
    return totals


def _verify_capture_cleanup(
    lines: list[str],
    args: argparse.Namespace,
    per_track_stats: Optional[list[dict[str, object]]] = None,
) -> dict[str, object]:
    rows = _extract_capture_cleanup_rows(lines)
    by_phase_kind = _summarize_capture_cleanup_by_phase_kind(rows)

    dedup_total = sum(phase_totals.get("dedup", 0) for phase_totals in by_phase_kind.values())
    minlen_total = sum(phase_totals.get("minlen", 0) for phase_totals in by_phase_kind.values())
    wrap_synth_total = sum(
        phase_totals.get("wrap_synth", 0) for phase_totals in by_phase_kind.values()
    )

    issues: list[str] = []
    max_dedup = int(getattr(args, "max_capture_dedup_events", 0))
    if dedup_total > max_dedup:
        issues.append("capture_dedup_exceeded")

    max_minlen = getattr(args, "max_capture_minlen_pairs", None)
    if max_minlen is not None and minlen_total > int(max_minlen):
        issues.append("capture_minlen_exceeded")

    reconciliation: list[dict[str, object]] = []
    if per_track_stats:
        track = per_track_stats[0]
        phase_sent = {
            "record": int(track.get("record_notes_sent", 0)),
            "overdub": int(track.get("overdub_notes_sent", 0))
            + int(track.get("second_overdub_notes_sent", 0)),
        }
        for phase, notes_sent in phase_sent.items():
            cleanup = by_phase_kind.get(phase, {})
            reconciliation.append(
                {
                    "phase": phase,
                    "notes_sent": notes_sent,
                    "dedup_events": cleanup.get("dedup", 0),
                    "minlen_pairs": cleanup.get("minlen", 0),
                    "wrap_synth_offs": cleanup.get("wrap_synth", 0),
                }
            )

    return {
        "rows": rows,
        "by_phase_kind": by_phase_kind,
        "dedup_total": dedup_total,
        "minlen_pairs_total": minlen_total,
        "wrap_synth_total": wrap_synth_total,
        "max_dedup_events": max_dedup,
        "max_minlen_pairs": max_minlen,
        "reconciliation": reconciliation,
        "issues": issues,
        "ok": len(issues) == 0,
    }


def _build_serial_verification(lines: list[str], args: argparse.Namespace) -> dict[str, object]:
    record_only = bool(getattr(args, "record_only", False))
    boundaries = _extract_phase_boundaries(lines)
    record_stop_stage_rows = _extract_record_stop_stage_rows(lines)
    record_stop_stage_trace = _summarize_record_stop_stages(record_stop_stage_rows)
    persistence_rows = _extract_persistence_rows(lines)
    persistence_handoff = _summarize_persistence_handoff(
        persistence_rows,
        record_stop_ts=boundaries.get("record_stop_ts"),
    )
    is_long_record_run = bool(args.record_bars and args.record_bars >= args.long_run_bar_threshold)
    ram2_headroom = _summarize_ram2_headroom(record_stop_stage_rows, persistence_rows)
    record_stop_ram2_floor = _verify_record_stop_ram2_floor(
        record_stop_stage_rows,
        floor_bytes=args.record_stop_min_free_ram2_bytes,
        warn_bytes=args.record_stop_min_free_ram2_warn_bytes,
        enabled=is_long_record_run,
    )
    long_run_persistence_result = _verify_long_run_persistence_result(
        persistence_handoff,
        enabled=is_long_record_run,
    )
    record = _verify_phase_note_pairs(
        lines,
        midi_channel_1based=args.midi_channel,
        phase_start_ts=boundaries["record_start_ts"],
        phase_stop_ts=boundaries["record_stop_ts"],
        low_note=args.record_low_note,
        high_note=args.record_high_note,
    )
    if record_only:
        overdub = {
            "phase_disabled": True,
            "phase_missing": False,
            "note_on_count": 0,
            "note_off_count": 0,
            "unmatched_open_notes": 0,
            "out_of_range_count": 0,
            "sequence_mismatch_count": 0,
            "out_of_range_examples": [],
            "sequence_examples": [],
        }
    else:
        overdub = _verify_phase_note_pairs(
            lines,
            midi_channel_1based=args.midi_channel,
            phase_start_ts=boundaries["overdub_start_ts"],
            phase_stop_ts=boundaries["overdub_stop_ts"],
            low_note=args.overdub_low_note,
            high_note=args.overdub_high_note,
        )
    recs_lengths = _extract_recs_lengths(lines)
    record_loop_length: Optional[dict[str, object]] = None
    record_note_span: Optional[dict[str, object]] = None
    stored_record_grid: Optional[dict[str, object]] = None
    if args.record_bars and args.bar_sync_from_midi_clock:
        record_loop_length = _verify_record_loop_length(
            recs_lengths,
            record_bars=args.record_bars,
        )
        record_note_span = _verify_record_note_span(
            recs_lengths,
            record_bars=args.record_bars,
            note_on_count=int(record["note_on_count"]),
        )
        stored_record_grid = _verify_stored_record_note_grid(lines)
    record_first_note_offset = _extract_first_note_offset(
        lines,
        midi_channel_1based=args.midi_channel,
        phase_start_ts=boundaries["record_start_ts"],
        phase_stop_ts=boundaries["record_stop_ts"],
        fallback_bpm=args.tempo_bpm,
    )
    if record_only:
        overdub_first_note_offset = {
            "offset_us": None,
            "offset_ms": None,
            "offset_clocks": None,
            "phase_missing": True,
        }
    else:
        overdub_first_note_offset = _extract_first_note_offset(
            lines,
            midi_channel_1based=args.midi_channel,
            phase_start_ts=boundaries["overdub_start_ts"],
            phase_stop_ts=boundaries["overdub_stop_ts"],
            fallback_bpm=args.tempo_bpm,
            phase_start_delay_clocks=_phase_start_delay_clocks(
                delay_bars=args.overdub_start_delay_bars,
                delay_beats=args.overdub_start_delay_beats,
            ),
        )
    issues: list[str] = []
    if record["phase_missing"] or (not record_only and overdub["phase_missing"]):
        issues.append("missing_phase_boundaries")
    if record["unmatched_open_notes"] > 0:
        issues.append("record_unmatched_open_notes")
    if not record_only and overdub["unmatched_open_notes"] > 0:
        issues.append("overdub_unmatched_open_notes")
    if record["out_of_range_count"] > 0:
        issues.append("record_out_of_range_notes")
    if not record_only and overdub["out_of_range_count"] > 0:
        issues.append("overdub_out_of_range_notes")
    record_offset_clocks = record_first_note_offset.get("capture_latency_clocks")
    if record_offset_clocks is None:
        record_offset_clocks = record_first_note_offset["offset_clocks"]
    overdub_offset_clocks = overdub_first_note_offset.get("capture_latency_clocks")
    if overdub_offset_clocks is None:
        overdub_offset_clocks = overdub_first_note_offset["offset_clocks"]
    if record_offset_clocks is None:
        issues.append("record_first_note_missing")
    elif record_offset_clocks > args.record_first_note_max_clocks:
        issues.append("record_first_note_too_late")
    if not record_only:
        if overdub_offset_clocks is None:
            issues.append("overdub_first_note_missing")
        elif overdub_offset_clocks > args.overdub_first_note_max_clocks:
            issues.append("overdub_first_note_too_late")
    if record_loop_length is not None:
        if record_loop_length.get("recs_missing"):
            if "record_recs_missing" not in issues:
                issues.append("record_recs_missing")
        elif not record_loop_length.get("final_length_ok", False):
            issues.append("record_loop_length_mismatch")
    if record_note_span is not None:
        if record_note_span.get("recs_missing"):
            if "record_recs_missing" not in issues:
                issues.append("record_recs_missing")
        elif not record_note_span.get("raw_span_ok", False):
            issues.append("record_note_span_too_short")
    if stored_record_grid is not None and not record_only:
        if stored_record_grid.get("revt_missing"):
            issues.append("record_stored_revt_missing")
        elif not stored_record_grid.get("grid_ok", False):
            issues.append("record_stored_note_grid_bad")
    first_missing_stage = record_stop_stage_trace.get("first_missing_stage")
    if first_missing_stage is not None:
        issues.append(f"record_stop_stage_missing:{first_missing_stage}")
    if not persistence_handoff.get("phase_disabled"):
        persistence_missing_stage = persistence_handoff.get("first_missing_stage")
        if persistence_missing_stage is not None:
            issues.append(f"persistence_stage_missing:{persistence_missing_stage}")
        elif not persistence_handoff.get("result_ok", False):
            issues.append("persistence_result_failed")
    if not record_stop_ram2_floor.get("phase_disabled", True):
        if record_stop_ram2_floor.get("record_stop_row_missing", False):
            issues.append("record_stop_ram2_heap_missing")
        elif int(record_stop_ram2_floor.get("floor_bytes", 0)) > 0 and not record_stop_ram2_floor.get(
            "floor_ok", False
        ):
            issues.append("record_stop_ram2_heap_below_floor")
    if not long_run_persistence_result.get("phase_disabled", True):
        if not long_run_persistence_result.get("result_present", False):
            issues.append("long_run_persistence_result_missing")
        elif not long_run_persistence_result.get("result_ok", False):
            issues.append("long_run_persistence_result_failed")
    overdub_wrap_storage: Optional[dict[str, object]] = None
    stored_overdub_span: Optional[dict[str, object]] = None
    loop_length_ticks = 0
    if record_loop_length and record_loop_length.get("actual_final_length") is not None:
        loop_length_ticks = int(record_loop_length["actual_final_length"])
    if (not record_only) and loop_length_ticks > 0 and args.overdub_bars and args.bar_sync_from_midi_clock:
        memory_note_count = _extract_overdub_memory_note_count(
            lines, overdub_stop_ts=boundaries.get("overdub_stop_ts")
        )
        stored_overdub_span = _verify_stored_overdub_note_span(
            lines,
            loop_length_ticks=loop_length_ticks,
            overdub_bars=args.overdub_bars,
            record_bars=args.record_bars or 0,
            low_note=args.overdub_low_note,
            high_note=args.overdub_high_note,
            midi_channel_1based=args.midi_channel,
            note_on_count=int(overdub["note_on_count"]),
            memory_note_count=memory_note_count,
            before_ts=boundaries.get("second_overdub_start_ts"),
        )
    if (not record_only) and loop_length_ticks > 0:
        overdub_wrap_storage = _verify_overdub_wrap_storage(
            lines,
            boundaries=boundaries,
            loop_length_ticks=loop_length_ticks,
            wrap_test_ran=bool(getattr(args, "overdub_wrap_note_off_test", False)),
        )
        if not overdub_wrap_storage.get("phase_disabled"):
            for issue in overdub_wrap_storage.get("issues", []):
                if issue not in issues:
                    issues.append(str(issue))
    if stored_overdub_span is not None and not stored_overdub_span.get("phase_disabled"):
        if stored_overdub_span.get("sevt_missing"):
            issues.append("overdub_stored_sevt_missing")
        elif not stored_overdub_span.get("count_ok", False):
            issues.append("overdub_stored_count_short")
        elif not stored_overdub_span.get("span_ok", False):
            issues.append("overdub_stored_span_too_short")
        elif not stored_overdub_span.get("bars_match_ok", False):
            issues.append("overdub_stored_bars_short")

    second_overdub: Optional[dict[str, object]] = None
    second_overdub_first_note_offset: Optional[dict[str, object]] = None
    stored_second_overdub_span: Optional[dict[str, object]] = None
    if (not record_only) and getattr(args, "second_overdub_bars", 0) and args.bar_sync_from_midi_clock:
        second_overdub = _verify_phase_note_pairs(
            lines,
            midi_channel_1based=args.midi_channel,
            phase_start_ts=boundaries.get("second_overdub_start_ts"),
            phase_stop_ts=boundaries.get("second_overdub_stop_ts"),
            low_note=args.second_overdub_low_note,
            high_note=args.second_overdub_high_note,
        )
        second_overdub_first_note_offset = _extract_first_note_offset(
            lines,
            midi_channel_1based=args.midi_channel,
            phase_start_ts=boundaries.get("second_overdub_start_ts"),
            phase_stop_ts=boundaries.get("second_overdub_stop_ts"),
            fallback_bpm=args.tempo_bpm,
            phase_start_delay_clocks=_phase_start_delay_clocks(
                delay_bars=args.second_overdub_start_delay_bars,
                delay_beats=args.second_overdub_start_delay_beats,
            ),
        )
        if second_overdub.get("phase_missing"):
            issues.append("second_overdub_missing_phase_boundaries")
        if int(second_overdub.get("unmatched_open_notes", 0)) > 0:
            issues.append("second_overdub_unmatched_open_notes")
        if int(second_overdub.get("out_of_range_count", 0)) > 0:
            issues.append("second_overdub_out_of_range_notes")
        second_offset_clocks = second_overdub_first_note_offset.get("capture_latency_clocks")
        if second_offset_clocks is None:
            second_offset_clocks = second_overdub_first_note_offset.get("offset_clocks")
        if second_offset_clocks is None:
            issues.append("second_overdub_first_note_missing")
        elif second_offset_clocks > args.overdub_first_note_max_clocks:
            issues.append("second_overdub_first_note_too_late")
        if loop_length_ticks > 0:
            memory_note_count = _extract_overdub_memory_note_count(
                lines, overdub_stop_ts=boundaries.get("second_overdub_stop_ts")
            )
            stored_second_overdub_span = _verify_stored_overdub_note_span(
                lines,
                loop_length_ticks=loop_length_ticks,
                overdub_bars=args.second_overdub_bars,
                record_bars=args.record_bars or 0,
                low_note=args.second_overdub_low_note,
                high_note=args.second_overdub_high_note,
                midi_channel_1based=args.midi_channel,
                note_on_count=int(second_overdub.get("note_on_count", 0)),
                memory_note_count=memory_note_count,
                grid_step_clocks=args.second_overdub_step_clocks,
                after_ts=boundaries.get("second_overdub_start_ts"),
            )
            if stored_second_overdub_span is not None and not stored_second_overdub_span.get("phase_disabled"):
                if stored_second_overdub_span.get("sevt_missing"):
                    issues.append("second_overdub_stored_sevt_missing")
                elif not stored_second_overdub_span.get("count_ok", False):
                    issues.append("second_overdub_stored_count_short")
                elif not stored_second_overdub_span.get("span_ok", False):
                    issues.append("second_overdub_stored_span_too_short")
                elif not stored_second_overdub_span.get("bars_match_ok", False):
                    issues.append("second_overdub_stored_bars_short")
    display_verification: Optional[dict[str, object]] = None
    if not record_only and loop_length_ticks > 0:
        display_verification = _verify_display_snapshots(
            lines,
            boundaries=boundaries,
            loop_length_ticks=loop_length_ticks,
        )
        for issue in display_verification.get("issues", []):
            if issue not in issues:
                issues.append(str(issue))
    return {
        "boundaries": boundaries,
        "record_phase": record,
        "overdub_phase": overdub,
        "record_loop_length": record_loop_length,
        "record_note_span": record_note_span,
        "stored_record_grid": stored_record_grid,
        "record_stop_stage_trace": record_stop_stage_trace,
        "persistence_handoff": persistence_handoff,
        "ram2_headroom": ram2_headroom,
        "record_stop_ram2_floor": record_stop_ram2_floor,
        "long_run_persistence_result": long_run_persistence_result,
        "overdub_wrap_storage": overdub_wrap_storage,
        "stored_overdub_span": stored_overdub_span,
        "display_verification": display_verification,
        "record_first_note_offset": record_first_note_offset,
        "overdub_first_note_offset": overdub_first_note_offset,
        "second_overdub_phase": second_overdub,
        "second_overdub_first_note_offset": second_overdub_first_note_offset,
        "stored_second_overdub_span": stored_second_overdub_span,
        "recs_lengths": recs_lengths,
        "issues": issues,
    }


def _expected_overdub_notes_min(overdub_bars: int, step_clocks: int) -> int:
    if overdub_bars <= 0 or step_clocks <= 0:
        return 0
    notes_per_bar = MIDI_CLOCKS_PER_BAR // step_clocks
    return max(1, overdub_bars * notes_per_bar - 1)


def _run_overdub_pass(
    idx: int,
    out_port: mido.ports.BaseOutput,
    in_port: mido.ports.BaseInput,
    serial_collector: Optional[Any],
    abort: Optional[RunAbort],
    args: argparse.Namespace,
    *,
    overdub_bars: int,
    low_note: int,
    high_note: int,
    step_clocks: int,
    gate_clocks: int,
    phase_start_delay_bars: int,
    phase_start_delay_beats: int,
    pass_label: str,
    seconds_per_bar: float,
) -> tuple[Optional[str], int, int, int, dict[str, float], bool]:
    """Start overdub, stream pattern, stop overdub."""

    print(f"[track {idx}] {pass_label} start")
    reached_overdub = False
    expected_overdub_count = None
    if serial_collector is not None:
        counts = _count_capture_transitions(serial_collector.snapshot())
        expected_overdub_count = counts.get(("PLAYING", "OVERDUBBING"), 0) + 1
    _send_short_press(
        out_port,
        note=RECORD_BUTTON_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=args.press_ms,
    )
    if serial_collector is not None and expected_overdub_count is not None:
        reached_overdub = _wait_for_transition_count(
            serial_collector,
            from_state="PLAYING",
            to_state="OVERDUBBING",
            target_count=expected_overdub_count,
            timeout_s=args.state_sync_timeout_ms / 1000.0,
            abort=abort,
        )
        if not reached_overdub:
            print(f"[warn] Timed out waiting for PLAYING->OVERDUBBING ({pass_label}); retrying overdub start press.")
            _send_short_press(
                out_port,
                note=RECORD_BUTTON_NOTE,
                channel_1based=CONTROL_CHANNEL_1BASED,
                press_ms=args.press_ms,
            )
            reached_overdub = _wait_for_transition_count(
                serial_collector,
                from_state="PLAYING",
                to_state="OVERDUBBING",
                target_count=expected_overdub_count,
                timeout_s=args.state_sync_timeout_ms / 1000.0,
                abort=abort,
            )
            if not reached_overdub:
                print(f"[warn] Retry did not reach PLAYING->OVERDUBBING ({pass_label}).")
    if serial_collector is not None and reached_overdub:
        time.sleep(0.0)
    else:
        time.sleep(min(args.phase_wait_ms, 120) / 1000.0)

    od_clock_count = 0
    od_fallback_seconds = False
    od_timing: dict[str, float] = {
        "grid_steps_emitted": 0.0,
        "max_abs_grid_jitter_clocks": 0.0,
        "mean_abs_grid_jitter_clocks": 0.0,
        "stop_press_sent_during_stream": 0.0,
    }
    od_notes = 0
    od_cc = 0

    if serial_collector is not None and expected_overdub_count is not None and not reached_overdub:
        return (
            f"{pass_label} transition not confirmed",
            od_notes,
            od_cc,
            od_clock_count,
            od_timing,
            od_fallback_seconds,
        )

    if overdub_bars and args.bar_sync_from_midi_clock:
        clock_ok, using_serial_proxy = _ensure_midi_clock_or_serial_proxy(
            in_port,
            out_port,
            serial_collector,
            args,
            min_clocks=24,
            timeout_s=2.0,
            abort=abort,
        )
        if not clock_ok:
            return "midi clock missing before overdub phase", od_notes, od_cc, od_clock_count, od_timing, od_fallback_seconds
        wall_clock_tempo = _resolve_wall_tempo_for_proxy(
            serial_collector, args, using_serial_proxy=using_serial_proxy
        )
        guard = max(10.0, overdub_bars * seconds_per_bar * 3.0)
        overdub_stop_advance_clocks = args.stop_press_advance_clocks
        if overdub_stop_advance_clocks <= 0:
            overdub_stop_advance_clocks = 0
        if args.overdub_wrap_note_off_test and pass_label != "overdub":
            return "wrap test incompatible with multi-pass overdub", od_notes, od_cc, od_clock_count, od_timing, od_fallback_seconds
        if args.overdub_wrap_note_off_test:
            if not args.record_bars:
                return "wrap test requires record-bars", od_notes, od_cc, od_clock_count, od_timing, od_fallback_seconds
            od_notes, od_cc, od_clock_count, od_timing = _stream_overdub_wrap_note_off_test(
                out_port,
                in_port,
                midi_channel_1based=args.midi_channel,
                wrap_note=args.overdub_fixed_note,
                loop_bars=args.record_bars,
                target_bars=overdub_bars,
                phase_start_delay_bars=0,
                phase_start_delay_beats=0,
                max_seconds_guard=guard,
                abort=abort,
            )
        else:
            od_notes, od_cc, od_clock_count, od_timing = _stream_pattern_for_bars(
                out_port,
                in_port,
                midi_channel_1based=args.midi_channel,
                low_note=low_note,
                high_note=high_note,
                step_clocks=step_clocks,
                gate_clocks=gate_clocks,
                target_bars=overdub_bars,
                cc_number=args.cc_number,
                cc_step=args.cc_step,
                pitch_cycle_bars=args.pitch_cycle_bars,
                phase_start_delay_bars=phase_start_delay_bars,
                phase_start_delay_beats=phase_start_delay_beats,
                max_seconds_guard=guard,
                fixed_note=args.overdub_fixed_note if args.fixed_grid_notes else None,
                stop_press_advance_clocks=overdub_stop_advance_clocks,
                stop_press_note=RECORD_BUTTON_NOTE,
                stop_press_channel_1based=CONTROL_CHANNEL_1BASED,
                stop_press_press_ms=args.press_ms,
                abort=abort,
                emit_immediate_first_step=True,
                wall_clock_tempo_bpm=wall_clock_tempo,
            )
    else:
        od_notes, od_cc = _stream_dense_chromatic(
            out_port,
            in_port,
            midi_channel_1based=args.midi_channel,
            root_note=args.root_note + ((idx + 5) % 12),
            semitone_span=args.semitone_span,
            duration_s=args.overdub_seconds,
            note_gap_ms=args.note_gap_ms,
            gate_ms=args.gate_ms,
            cc_number=args.cc_number,
            cc_step=args.cc_step,
            abort=abort,
        )
        od_fallback_seconds = True

    if abort is not None and abort.check() is not None:
        return abort.check(), od_notes, od_cc, od_clock_count, od_timing, od_fallback_seconds

    time.sleep(max(args.post_stream_settle_ms, 0) / 1000.0)
    print(f"[track {idx}] {pass_label} stop")
    if od_timing.get("stop_press_sent_during_stream", 0.0) <= 0.0:
        _send_short_press(
            out_port,
            note=RECORD_BUTTON_NOTE,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=args.press_ms,
        )
    time.sleep(args.phase_wait_ms / 1000.0)
    return None, od_notes, od_cc, od_clock_count, od_timing, od_fallback_seconds


def _wait_for_state_entry_count(
    collector: SerialCaptureCollector,
    *,
    to_state: str,
    target_count: int,
    timeout_s: float,
    abort: Optional[RunAbort] = None,
) -> bool:
    deadline = time.monotonic() + max(timeout_s, 0.0)
    while time.monotonic() < deadline:
        if abort is not None and abort.check() is not None:
            return False
        counts = _count_capture_state_entries(collector.snapshot())
        if counts.get(to_state, 0) >= target_count:
            return True
        time.sleep(0.01)
    return False


def _wait_for_transition_count(
    collector: SerialCaptureCollector,
    *,
    from_state: str,
    to_state: str,
    target_count: int,
    timeout_s: float,
    abort: Optional[RunAbort] = None,
) -> bool:
    deadline = time.monotonic() + max(timeout_s, 0.0)
    key = (from_state, to_state)
    while time.monotonic() < deadline:
        if abort is not None and abort.check() is not None:
            return False
        counts = _count_capture_transitions(collector.snapshot())
        if counts.get(key, 0) >= target_count:
            return True
        time.sleep(0.01)
    return False


_STUCK_CAPTURE_STATES = frozenset({"RECORDING", "OVERDUBBING"})


def _should_reset_transport_for_recovery(
    *,
    clock_present: bool,
    serial_lines: Optional[list[str]],
    serial_proxy_active: bool = False,
) -> bool:
    """Run stop→start transport recovery only when clock is missing or capture is stuck."""
    if serial_proxy_active:
        return False
    if not clock_present:
        return True
    if serial_lines is not None:
        latest = _latest_track_state(serial_lines)
        if latest in _STUCK_CAPTURE_STATES:
            return True
    return False


def run() -> int:
    parser = argparse.ArgumentParser(description="Host-side MIDI automation baseline")
    parser.add_argument("--midi-out", default="Teensy", help="MIDI output port substring")
    parser.add_argument("--midi-in", default="Teensy", help="MIDI input port substring")
    parser.add_argument("--serial-port", default="", help="Optional Teensy USB serial port (capture build)")
    parser.add_argument(
        "--follow-current-session",
        action="store_true",
        help="Tail captures/.current_session log from external capture_session.py (no USB serial open)",
    )
    parser.add_argument(
        "--follow-serial-log",
        type=Path,
        default=None,
        help="Tail an external capture log during the run (from capture_session.py)",
    )
    parser.add_argument("--serial-baud", type=int, default=115200, help="USB serial baud")
    parser.add_argument("--serial-log-path", type=Path, default=None, help="Optional path to save raw serial lines")
    parser.add_argument(
        "--verify-serial-log",
        type=Path,
        default=None,
        help="Optional external serial log for verification when --serial-port is not used",
    )
    parser.add_argument("--out-dir", type=Path, default=Path("captures"), help="Run report output directory")
    parser.add_argument("--start-transport", action="store_true", help="Press global transport once at start")
    parser.add_argument(
        "--record-only",
        action="store_true",
        help="Run only through record stop (skip overdub and second overdub phases)",
    )
    parser.add_argument(
        "--edit-record-fixture",
        action="store_true",
        help="Record EDIT_RECORD_FIXTURE notes (edit_minimal) instead of chromatic grid",
    )
    parser.add_argument(
        "--stop-after-overdub",
        action="store_true",
        default=True,
        help="Press global transport after overdub stop to end playback (default: enabled)",
    )
    parser.add_argument(
        "--no-stop-after-overdub",
        action="store_false",
        dest="stop_after_overdub",
        help="Do not press global transport after overdub stop",
    )
    parser.add_argument("--track-count", type=int, default=8, help="How many tracks to run (1-8)")
    parser.add_argument("--first-track-index", type=int, default=0, help="Start track index (0-based)")
    parser.add_argument(
        "--track",
        "--track-number",
        dest="track_number",
        type=int,
        default=0,
        metavar="N",
        help="Run baseline on a single track (1-8, user-facing track number). "
        "Overrides --track-count/--first-track-index. "
        "Defaults --midi-channel to N when channel is omitted.",
    )
    parser.add_argument(
        "--loop-slot",
        type=int,
        default=0,
        metavar="N",
        help="Target loop slot 1-8 for record arm (Loops row notes 50-57). "
        "0 = Record button (36) on device-selected slot.",
    )
    parser.add_argument("--record-seconds", type=float, default=3.0, help="Dense stream duration for record phase")
    parser.add_argument("--overdub-seconds", type=float, default=2.0, help="Dense stream duration for overdub phase")
    parser.add_argument(
        "--record-bars",
        type=int,
        choices=[1, 2, 4, 5, 8, 16, 24, 32, 48, 64, 128],
        default=0,
        help="Record duration in bars (overrides --record-seconds)",
    )
    parser.add_argument(
        "--overdub-bars",
        type=int,
        choices=[1, 2, 4, 5, 8, 16, 24, 32, 48, 64, 128],
        default=0,
        help="Overdub duration in bars (overrides --overdub-seconds)",
    )
    parser.add_argument("--tempo-bpm", type=float, default=120.0, help="Tempo used for bar-to-seconds conversion")
    parser.add_argument(
        "--bar-sync-from-midi-clock",
        action="store_true",
        default=True,
        help="When using --record-bars/--overdub-bars, follow incoming MIDI clock pulses (default: enabled)",
    )
    parser.add_argument(
        "--no-bar-sync-from-midi-clock",
        action="store_false",
        dest="bar_sync_from_midi_clock",
        help="Disable MIDI clock bar sync and use tempo-based seconds conversion",
    )
    parser.add_argument(
        "--clear-before-record",
        action="store_true",
        default=True,
        help="Long-press record button to clear selected loop before recording (default: enabled)",
    )
    parser.add_argument(
        "--no-clear-before-record",
        action="store_false",
        dest="clear_before_record",
        help="Disable clear-before-record step",
    )
    parser.add_argument(
        "--no-loop-edit-precondition",
        action="store_true",
        help="Skip NOTE_EDIT exit before record (default: no-op unless serial shows NOTE_EDIT)",
    )
    parser.add_argument(
        "--clear-press-ms",
        type=int,
        default=900,
        help="Long-press duration for clear command (milliseconds)",
    )
    parser.add_argument(
        "--phase-wait-ms",
        type=int,
        default=320,
        help="Wait after each short press (should match short-press window + small margin)",
    )
    parser.add_argument(
        "--final-wait-ms",
        type=int,
        default=1200,
        help="Final wait before exit so delayed short-press actions are captured",
    )
    parser.add_argument("--press-ms", type=int, default=85, help="Button press duration (short press)")
    parser.add_argument(
        "--undo-redo-after-overdub-stop",
        action="store_true",
        default=True,
        help="After overdub stop, wait then Undo (double-press), wait, then Redo (triple-press)",
    )
    parser.add_argument(
        "--no-undo-redo-after-overdub-stop",
        action="store_false",
        dest="undo_redo_after_overdub_stop",
        help="Skip undo/redo presses after overdub stop",
    )
    parser.add_argument(
        "--undo-redo-delay-ms",
        type=int,
        default=3000,
        help=(
            "Wait after overdub stop before undo, and between undo and redo "
            "(default: 3000 — long loops need deferred save to finish first)"
        ),
    )
    parser.add_argument(
        "--midi-channel",
        type=int,
        default=None,
        help="Dense input MIDI channel (1-15). Defaults to --track when set, else 1",
    )
    parser.add_argument("--root-note", type=int, default=60, help="Chromatic root note")
    parser.add_argument("--semitone-span", type=int, default=12, help="Chromatic span size")
    parser.add_argument("--note-gap-ms", type=int, default=20, help="Gap between note-offs and next note-ons")
    parser.add_argument("--gate-ms", type=int, default=20, help="Note-on gate duration")
    parser.add_argument(
        "--post-stream-settle-ms",
        type=int,
        default=120,
        help="Delay after each note stream before stop press, to flush trailing note-offs",
    )
    parser.add_argument("--cc-number", type=int, default=74, help="CC number for dense CC lane")
    parser.add_argument("--cc-step", type=int, default=9, help="CC step per sent note")
    parser.add_argument(
        "--max-capture-dedup-events",
        type=int,
        default=0,
        help="Fail when live capture dedup drops exceed this total (CLN dedup sum)",
    )
    parser.add_argument(
        "--max-capture-minlen-pairs",
        type=int,
        default=None,
        help="Fail when NoteMinLength hot-stop pair removals exceed this (default: no limit)",
    )
    parser.add_argument(
        "--fixed-grid-notes",
        action="store_true",
        default=False,
        help="Use fixed pitch per phase for strict drift assertions (default: disabled)",
    )
    parser.add_argument(
        "--no-fixed-grid-notes",
        action="store_false",
        dest="fixed_grid_notes",
        help="Use chromatic moving notes instead of fixed per-phase pitch",
    )
    parser.add_argument("--record-fixed-note", type=int, default=60, help="Fixed record-phase note (default C4)")
    parser.add_argument("--overdub-fixed-note", type=int, default=36, help="Fixed overdub-phase note (default C2)")
    parser.add_argument(
        "--stop-press-advance-clocks",
        type=int,
        default=0,
        help="End stream this many MIDI clocks early so short-press action lands on target boundary",
    )
    parser.add_argument(
        "--max-run-seconds",
        type=float,
        default=0.0,
        help="Optional hard timeout for the full script run (0 disables; prefer serial heartbeat)",
    )
    parser.add_argument(
        "--serial-heartbeat-timeout-seconds",
        type=float,
        default=20.0,
        help="Abort when serial capture has no lines for this long (0 disables; requires --serial-port)",
    )
    parser.add_argument(
        "--state-sync-timeout-ms",
        type=int,
        default=8000,
        help="Max wait for serial-confirmed state transitions before phase stream starts",
    )
    parser.add_argument(
        "--serial-grace-ms",
        type=int,
        default=3000,
        help="Extra serial poll after state-sync timeout (PSRAM capture flush lag)",
    )
    parser.add_argument(
        "--serial-close-drain-ms",
        type=int,
        default=10000,
        help=(
            "Mode A (--serial-port): max wait after final_wait for trailing serial "
            "before closing USB (firmware TX ring / deferred #CAP flush)"
        ),
    )
    parser.add_argument(
        "--serial-close-idle-ms",
        type=int,
        default=750,
        help=(
            "Mode A: stop serial close drain when no new lines for this many ms "
            "(within --serial-close-drain-ms cap)"
        ),
    )
    parser.add_argument(
        "--boot-settle-ms",
        type=int,
        default=0,
        help="Wait after opening USB serial before MIDI actions (Teensy resets on connect)",
    )
    parser.add_argument(
        "--overdub-start-delay-bars",
        type=int,
        default=0,
        help="Delay overdub note stream by this many bars after entering overdub (default: 0)",
    )
    parser.add_argument(
        "--overdub-start-delay-beats",
        type=int,
        default=1,
        help="Delay overdub note stream by this many beats after entering overdub (default: 1)",
    )
    parser.add_argument(
        "--record-first-note-max-clocks",
        type=int,
        default=12,
        help="Max allowed MIDI clocks from RECORDING entry to first record note-on (default: 12)",
    )
    parser.add_argument(
        "--overdub-first-note-max-clocks",
        type=int,
        default=12,
        help="Max allowed MIDI clocks from OVERDUBBING entry to first overdub note-on (default: 12)",
    )
    parser.add_argument(
        "--long-run-bar-threshold",
        type=int,
        default=DEFAULT_LONG_RUN_BAR_THRESHOLD,
        help="Treat runs with --record-bars >= this value as long runs (default: 48)",
    )
    parser.add_argument(
        "--record-stop-min-free-ram2-bytes",
        type=int,
        default=DEFAULT_RECORD_STOP_RAM2_FLOOR_BYTES,
        help="Long-run FAIL gate: minimum RAM2 free heap at record_stop (default: 0 = telemetry only)",
    )
    parser.add_argument(
        "--record-stop-min-free-ram2-warn-bytes",
        type=int,
        default=DEFAULT_RECORD_STOP_RAM2_WARN_BYTES,
        help="Long-run JSON warn when record_stop heap below this (default: 12288; 0 disables)",
    )
    parser.add_argument("--record-low-note", type=int, default=48, help="Record phase lowest note (default C3)")
    parser.add_argument("--record-high-note", type=int, default=79, help="Record phase highest note (default G5)")
    parser.add_argument("--overdub-low-note", type=int, default=24, help="Overdub phase lowest note (default C1)")
    parser.add_argument("--overdub-high-note", type=int, default=39, help="Overdub phase highest note (default D#2)")
    parser.add_argument(
        "--second-overdub-bars",
        type=int,
        choices=[0, 1, 2, 4, 5, 8, 16, 24, 32, 48, 64, 128],
        default=2,
        help="Second overdub pass length in bars (default 2; 0 disables)",
    )
    parser.add_argument(
        "--second-overdub-low-note",
        type=int,
        default=12,
        help="Second overdub lowest note (default C0 / MIDI 12)",
    )
    parser.add_argument(
        "--second-overdub-high-note",
        type=int,
        default=35,
        help="Second overdub highest note (default B1 / MIDI 35)",
    )
    parser.add_argument(
        "--second-overdub-step-clocks",
        type=int,
        default=MIDI_CLOCKS_PER_BEAT,
        help="Second overdub grid step in MIDI clocks (default 24 = 1 beat)",
    )
    parser.add_argument(
        "--second-overdub-gate-clocks",
        type=int,
        default=MIDI_CLOCKS_PER_BEAT,
        help="Second overdub note gate in MIDI clocks (default 24 = 1 beat)",
    )
    parser.add_argument(
        "--second-overdub-start-delay-bars",
        type=int,
        default=0,
        help="Delay second overdub note stream by this many bars after entering overdub",
    )
    parser.add_argument(
        "--second-overdub-start-delay-beats",
        type=int,
        default=1,
        help="Delay second overdub note stream by this many beats after entering overdub",
    )
    parser.add_argument(
        "--pitch-cycle-bars",
        type=int,
        default=2,
        help="Reset chromatic pitch progression every N bars (default: 2)",
    )
    parser.add_argument(
        "--overdub-wrap-note-off-test",
        action="store_true",
        help="Overdub: hold one note across loop wrap and release in head window; require WRAP/SEVT serial verify",
    )
    args = parser.parse_args()

    if args.track_number:
        if not (1 <= args.track_number <= 8):
            raise SystemExit("--track must be in [1, 8]")
        args.first_track_index = args.track_number - 1
        args.track_count = 1
    else:
        if not (1 <= args.track_count <= 8):
            raise SystemExit("--track-count must be in [1, 8]")
        if not (0 <= args.first_track_index <= 7):
            raise SystemExit("--first-track-index must be in [0, 7]")
        if args.first_track_index + args.track_count > 8:
            raise SystemExit("first-track-index + track-count exceeds 8 tracks")

    if args.midi_channel is None:
        args.midi_channel = args.track_number if args.track_number else 1

    if args.loop_slot and not (1 <= args.loop_slot <= 8):
        raise SystemExit("--loop-slot must be in [1, 8]")

    if args.record_only:
        args.second_overdub_bars = 0
        args.undo_redo_after_overdub_stop = False

    if args.edit_record_fixture and not args.record_bars:
        raise SystemExit("--edit-record-fixture requires --record-bars")

    if not (1 <= args.midi_channel <= 16):
        raise SystemExit("--midi-channel must be in [1, 16]")
    if args.midi_channel == CONTROL_CHANNEL_1BASED:
        raise SystemExit("--midi-channel 16 is excluded from recording; choose 1-15")
    if args.tempo_bpm <= 0:
        raise SystemExit("--tempo-bpm must be > 0")
    if args.max_run_seconds < 0:
        raise SystemExit("--max-run-seconds must be >= 0 (0 disables)")
    if args.serial_heartbeat_timeout_seconds < 0:
        raise SystemExit("--serial-heartbeat-timeout-seconds must be >= 0 (0 disables)")
    if args.record_first_note_max_clocks < 0:
        raise SystemExit("--record-first-note-max-clocks must be >= 0")
    if args.overdub_first_note_max_clocks < 0:
        raise SystemExit("--overdub-first-note-max-clocks must be >= 0")
    if args.long_run_bar_threshold < 0:
        raise SystemExit("--long-run-bar-threshold must be >= 0")
    if args.record_stop_min_free_ram2_bytes < 0:
        raise SystemExit("--record-stop-min-free-ram2-bytes must be >= 0")
    if args.record_stop_min_free_ram2_warn_bytes < 0:
        raise SystemExit("--record-stop-min-free-ram2-warn-bytes must be >= 0")
    if args.overdub_start_delay_beats < 0:
        raise SystemExit("--overdub-start-delay-beats must be >= 0")
    if args.serial_port and _serial_follow_active(args):
        raise SystemExit("--serial-port is mutually exclusive with --follow-current-session / --follow-serial-log")
    if args.undo_redo_after_overdub_stop and not (
        _serial_capture_active(args) or args.verify_serial_log
    ):
        raise SystemExit(
            "--undo-redo-after-overdub-stop requires --serial-port, "
            "--follow-current-session, --follow-serial-log, or --verify-serial-log"
        )

    seconds_per_bar = (60.0 / args.tempo_bpm) * 4.0
    if args.record_bars and not args.bar_sync_from_midi_clock:
        args.record_seconds = args.record_bars * seconds_per_bar
    if args.overdub_bars and not args.bar_sync_from_midi_clock:
        args.overdub_seconds = args.overdub_bars * seconds_per_bar

    midi_out_name = _find_midi_port(args.midi_out, is_input=False)
    midi_in_name = _find_midi_port(args.midi_in, is_input=True)
    print(f"MIDI out: {midi_out_name}")
    print(f"MIDI in : {midi_in_name}")
    if args.bar_sync_from_midi_clock and (args.record_bars or args.overdub_bars):
        print(
            f"Durations: bar-synced from MIDI clock "
            f"(record-bars={args.record_bars or 'off'} overdub-bars={args.overdub_bars or 'off'})"
        )
    else:
        print(
            f"Durations: record={args.record_seconds:.2f}s overdub={args.overdub_seconds:.2f}s "
            f"(tempo={args.tempo_bpm:.2f} BPM)"
        )

    serial_collector: Optional[Any] = None
    serial_active = _serial_capture_active(args)
    if args.serial_port:
        serial_collector = SerialCaptureCollector(args.serial_port, args.serial_baud)
        serial_collector.start()
        boot_ms = max(getattr(args, "boot_settle_ms", 0), 0)
        if boot_ms > 0:
            print(f"Serial boot settle: {boot_ms}ms (Teensy resets when serial opens)")
            time.sleep(boot_ms / 1000.0)
        else:
            time.sleep(1.5)  # Teensy serial can reset on open.
        print(f"Serial capture enabled: {args.serial_port}")
    elif _serial_follow_active(args):
        from hitl.serial_follow import ExternalSerialFollowCollector

        follow_path = (
            ExternalSerialFollowCollector.resolve_current_session_path(args.out_dir)
            if args.follow_current_session
            else args.follow_serial_log
        )
        if follow_path is None:
            raise SystemExit("--follow-serial-log requires a path")
        serial_collector = ExternalSerialFollowCollector(follow_path, out_dir=args.out_dir)
        serial_collector.start()
        time.sleep(0.15)
        print(f"Serial follow enabled: {follow_path}")
    else:
        print("Serial capture disabled (no #CAP assertions).")

    run_started = datetime.now()
    run_deadline = (
        time.monotonic() + args.max_run_seconds if args.max_run_seconds > 0 else None
    )
    abort = RunAbort(
        run_deadline=run_deadline,
        serial_collector=serial_collector,
        heartbeat_timeout_s=args.serial_heartbeat_timeout_seconds if serial_active else 0.0,
    )
    if serial_active and args.serial_heartbeat_timeout_seconds > 0:
        print(
            "Serial heartbeat enabled: "
            f"abort after {args.serial_heartbeat_timeout_seconds:.0f}s without serial lines"
        )
    per_track_stats: list[dict[str, int]] = []
    midi_in_messages = 0
    abort_reason: Optional[str] = None
    precondition_failures: list[dict[str, object]] = []
    clear_precondition_results: list[dict[str, object]] = []

    try:
        with mido.open_output(midi_out_name) as out_port, mido.open_input(midi_in_name) as in_port:
            if args.start_transport:
                # Ensure transport is running without blindly toggling it off.
                if _use_serial_transport_proxy(args, serial_collector):
                    print(
                        "[info] Serial #CAP,BPM/BAR shows transport running; "
                        "skipping transport start presses (no USB MIDI clock on host)."
                    )
                elif not _clock_seen_within(in_port, 0.5):
                    clock_started = False
                    for attempt in range(1, 4):
                        _send_short_press(
                            out_port,
                            note=GLOBAL_TRANSPORT_NOTE,
                            channel_1based=CONTROL_CHANNEL_1BASED,
                            press_ms=args.press_ms,
                        )
                        time.sleep(args.phase_wait_ms / 1000.0)
                        if _clock_seen_within(in_port, 1.0):
                            clock_started = True
                            break
                        print(f"[warn] No MIDI clock observed after transport start press (attempt {attempt}/3).")
                    if not clock_started:
                        print("[warn] MIDI clock still missing after transport retries.")

            for idx in range(args.first_track_index, args.first_track_index + args.track_count):
                if (reason := abort.check()) is not None:
                    abort_reason = reason
                    break
                print(f"[track {idx}] select")
                _send_short_press(
                    out_port,
                    note=TRACK_SELECT_NOTE_BASE + idx,
                    channel_1based=CONTROL_CHANNEL_1BASED,
                    press_ms=args.press_ms,
                )
                time.sleep(args.phase_wait_ms / 1000.0)

                _log_loop_slot_report_only(args, track_index=idx)

                from hitl.edit_mode_precondition import ensure_loop_edit_before_record

                ensure_loop_edit_before_record(
                    out_port,
                    serial_collector,
                    press_ms=args.press_ms,
                    phase_wait_ms=args.phase_wait_ms,
                    skip=args.no_loop_edit_precondition,
                )

                # Recover from a prior aborted run: stop then restart transport so
                # USB MIDI clock reaches the host again (stuck RECORDING can mute it).
                if args.start_transport:
                    serial_snap = serial_collector.snapshot() if serial_collector is not None else None
                    clock_present = _clock_seen_within(in_port, 0.5)
                    serial_proxy = _use_serial_transport_proxy(args, serial_collector)
                    if serial_proxy:
                        print(
                            "[info] Serial transport proxy active; skipping transport reset recovery."
                        )
                    elif _should_reset_transport_for_recovery(
                        clock_present=clock_present,
                        serial_lines=serial_snap,
                    ):
                        _send_short_press(
                            out_port,
                            note=GLOBAL_TRANSPORT_NOTE,
                            channel_1based=CONTROL_CHANNEL_1BASED,
                            press_ms=args.press_ms,
                        )
                        time.sleep(args.phase_wait_ms / 1000.0)
                        if not _ensure_midi_clock(
                            in_port,
                            out_port,
                            min_clocks=24,
                            timeout_s=2.0,
                            abort=abort,
                        ):
                            print(
                                "[warn] MIDI clock missing after transport stop; "
                                "retrying transport start."
                            )
                        _send_short_press(
                            out_port,
                            note=GLOBAL_TRANSPORT_NOTE,
                            channel_1based=CONTROL_CHANNEL_1BASED,
                            press_ms=args.press_ms,
                        )
                        time.sleep(args.phase_wait_ms / 1000.0)
                        if not _ensure_midi_clock(
                            in_port,
                            out_port,
                            min_clocks=24,
                            timeout_s=2.0,
                            abort=abort,
                        ):
                            print(
                                "[error] MIDI clock unavailable after transport reset; "
                                "aborting track run."
                            )
                            abort_reason = "midi clock unavailable after transport reset"
                            break
                    elif clock_present:
                        print("[info] MIDI clock active; skipping transport reset recovery.")

                if args.clear_before_record:
                    if (reason := abort.check()) is not None:
                        abort_reason = reason
                        break
                    print(f"[track {idx}] clear selected loop (long press)")
                    reached_empty = False
                    clear_result = ""
                    pre_clear_snap = (
                        serial_collector.snapshot() if serial_collector is not None else []
                    )
                    if serial_collector is not None and _can_skip_clear_before_record(
                        pre_clear_snap
                    ):
                        print(
                            "[info] Track already empty or idle; skipping clear long press."
                        )
                        reached_empty = True
                        clear_result = "already_empty_skip"
                    stopped_transport_for_clear = False
                    if not reached_empty:
                        stopped_transport_for_clear = _stop_transport_before_clear(
                            out_port,
                            in_port,
                            serial_collector,
                            args,
                            press_ms=args.press_ms,
                            phase_wait_ms=args.phase_wait_ms,
                        )
                    expected_empty_count = None
                    clear_baseline_len = len(pre_clear_snap)
                    if not reached_empty:
                        if serial_collector is not None:
                            state_counts = _count_capture_state_entries(
                                serial_collector.snapshot()
                            )
                            expected_empty_count = state_counts.get("EMPTY", 0) + 1
                        _send_short_press(
                            out_port,
                            note=RECORD_BUTTON_NOTE,
                            channel_1based=CONTROL_CHANNEL_1BASED,
                            press_ms=args.clear_press_ms,
                        )
                    if (
                        not reached_empty
                        and serial_collector is not None
                        and expected_empty_count is not None
                    ):
                        reached_empty = _wait_for_state_entry_count(
                            serial_collector,
                            to_state="EMPTY",
                            target_count=expected_empty_count,
                            timeout_s=args.state_sync_timeout_ms / 1000.0,
                            abort=abort,
                        )
                        clear_result = "empty_transition" if reached_empty else ""
                        post_snap = serial_collector.snapshot()
                        if not reached_empty and _serial_has_clear_ignored_empty(
                            post_snap, after_index=clear_baseline_len
                        ):
                            print(
                                "[info] Clear ignored on already-empty track; "
                                "treating clear precondition as satisfied."
                            )
                            reached_empty = True
                            clear_result = "already_empty_ignored"
                        if not reached_empty and _serial_has_clear_completed(
                            post_snap, after_index=clear_baseline_len
                        ):
                            print("[info] Clear completed (serial log); precondition satisfied")
                            reached_empty = True
                            clear_result = "clear_log_confirmed"
                        if not reached_empty and _track_cleared_for_record(post_snap):
                            latest = _latest_track_state(post_snap)
                            print(
                                f"[info] Track cleared for record without EMPTY transition "
                                f"(latest={latest})"
                            )
                            reached_empty = True
                            clear_result = "cleared_without_empty_transition"
                        if not reached_empty:
                            print("[warn] Timed out waiting for clear->EMPTY transition; retrying clear long press.")
                            retry_baseline_len = len(serial_collector.snapshot())
                            _send_short_press(
                                out_port,
                                note=RECORD_BUTTON_NOTE,
                                channel_1based=CONTROL_CHANNEL_1BASED,
                                press_ms=args.clear_press_ms,
                            )
                            reached_empty = _wait_for_state_entry_count(
                                serial_collector,
                                to_state="EMPTY",
                                target_count=expected_empty_count,
                                timeout_s=args.state_sync_timeout_ms / 1000.0,
                                abort=abort,
                            )
                            if reached_empty:
                                clear_result = "empty_transition"
                            post_retry = serial_collector.snapshot()
                            if not reached_empty and _serial_has_clear_ignored_empty(
                                post_retry, after_index=retry_baseline_len
                            ):
                                print(
                                    "[info] Clear ignored on already-empty track; "
                                    "treating clear precondition as satisfied."
                                )
                                reached_empty = True
                                clear_result = "already_empty_ignored"
                            if not reached_empty and _serial_has_clear_completed(
                                post_retry, after_index=retry_baseline_len
                            ):
                                print("[info] Clear completed after retry (serial log)")
                                reached_empty = True
                                clear_result = "clear_log_confirmed"
                            if not reached_empty and _track_cleared_for_record(post_retry):
                                latest = _latest_track_state(post_retry)
                                print(
                                    f"[info] Track cleared for record after retry "
                                    f"(latest={latest})"
                                )
                                reached_empty = True
                                clear_result = "cleared_without_empty_transition"
                    elif not reached_empty:
                        _send_short_press(
                            out_port,
                            note=RECORD_BUTTON_NOTE,
                            channel_1based=CONTROL_CHANNEL_1BASED,
                            press_ms=args.clear_press_ms,
                        )
                        reached_empty = True
                        clear_result = "no_serial_capture"
                    if reached_empty and clear_result:
                        clear_precondition_results.append(
                            {
                                "track_index": idx,
                                "result": clear_result,
                            }
                        )
                    if not reached_empty:
                            print("[warn] Retry did not reach EMPTY after clear long press.")
                            precondition_failures.append(
                                {
                                    "track_index": idx,
                                    "step": "clear_to_empty",
                                    "reason": "clear_not_confirmed",
                                }
                            )
                            print(
                                f"[error] Preconditions failed on track {idx}: "
                                "clear step did not confirm EMPTY state. Aborting run."
                            )
                            break
                    time.sleep(args.phase_wait_ms / 1000.0)
                    if precondition_failures:
                        break
                    if stopped_transport_for_clear:
                        if not _restart_transport_after_clear(
                            out_port,
                            in_port,
                            serial_collector,
                            args,
                            press_ms=args.press_ms,
                            phase_wait_ms=args.phase_wait_ms,
                            abort=abort,
                        ):
                            precondition_failures.append(
                                {
                                    "track_index": idx,
                                    "step": "transport_restart_after_clear",
                                    "reason": "clock_missing_after_restart",
                                }
                            )
                            abort_reason = "transport restart after clear failed"
                            break

                print(f"[track {idx}] record start")
                reached_recording = False
                recording_confirmed_at: Optional[float] = None
                record_baseline_len = len(serial_collector.snapshot()) if serial_collector else 0
                baseline_reca = _count_reca_markers(serial_collector.snapshot()) if serial_collector else 0
                baseline_recording_transitions = (
                    _recording_transition_baseline(serial_collector.snapshot())
                    if serial_collector
                    else 0
                )
                baseline_armed_transitions = (
                    _armed_transition_baseline(serial_collector.snapshot())
                    if serial_collector
                    else 0
                )
                baseline_latest_state = (
                    _latest_track_state(serial_collector.snapshot()) if serial_collector else None
                )
                record_loop_slot = args.loop_slot if args.loop_slot > 0 else None
                _send_record_arm_press(
                    out_port,
                    press_ms=args.press_ms,
                    loop_slot=record_loop_slot,
                )
                if serial_collector is not None:
                    timeout_s = args.state_sync_timeout_ms / 1000.0
                    grace_s = max(getattr(args, "serial_grace_ms", 0), 0) / 1000.0
                    reached_recording = _wait_for_recording_started(
                        serial_collector,
                        baseline_len=record_baseline_len,
                        baseline_reca=baseline_reca,
                        baseline_recording_transitions=baseline_recording_transitions,
                        baseline_latest_state=baseline_latest_state,
                        baseline_armed_transitions=baseline_armed_transitions,
                        timeout_s=timeout_s,
                        serial_grace_s=grace_s,
                        abort=abort,
                    )
                    if not reached_recording:
                        fresh = serial_collector.snapshot()[record_baseline_len:]
                        if any("MIDI Button A: Toggle Play/Stop" in line for line in fresh):
                            print(
                                "[warn] Record press toggled play/stop (track likely not empty); "
                                "retrying clear then record"
                            )
                            _send_short_press(
                                out_port,
                                note=RECORD_BUTTON_NOTE,
                                channel_1based=CONTROL_CHANNEL_1BASED,
                                press_ms=args.clear_press_ms,
                            )
                            time.sleep(args.state_sync_timeout_ms / 1000.0)
                            record_baseline_len = len(serial_collector.snapshot())
                            baseline_reca = _count_reca_markers(serial_collector.snapshot())
                            baseline_recording_transitions = _recording_transition_baseline(
                                serial_collector.snapshot()
                            )
                            baseline_armed_transitions = _armed_transition_baseline(
                                serial_collector.snapshot()
                            )
                            baseline_latest_state = _latest_track_state(serial_collector.snapshot())
                        if not _serial_has_recording_started(
                            serial_collector.snapshot(), after_index=record_baseline_len
                        ):
                            print("[warn] Timed out waiting for recording start; retrying record press.")
                            _send_record_arm_press(
                                out_port,
                                press_ms=args.press_ms,
                                loop_slot=record_loop_slot,
                            )
                            reached_recording = _wait_for_recording_started(
                                serial_collector,
                                baseline_len=record_baseline_len,
                                baseline_reca=baseline_reca,
                                baseline_recording_transitions=baseline_recording_transitions,
                                baseline_latest_state=baseline_latest_state,
                                baseline_armed_transitions=baseline_armed_transitions,
                                timeout_s=timeout_s,
                                serial_grace_s=grace_s,
                                abort=abort,
                            )
                        if _serial_has_recording_started(
                            serial_collector.snapshot(), after_index=record_baseline_len
                        ):
                            reached_recording = True
                            recording_confirmed_at = time.monotonic()
                        elif not reached_recording:
                            print("[warn] Retry did not reach RECORDING.")
                    elif recording_confirmed_at is None:
                        recording_confirmed_at = time.monotonic()
                if serial_collector is not None and not reached_recording:
                    print(
                        "[error] Record phase did not confirm RECORDING in serial capture; "
                        "aborting track run (notes would start before capture)."
                    )
                    precondition_failures.append(
                        {
                            "track_index": idx,
                            "step": "record_start",
                            "reason": "recording_not_confirmed",
                        }
                    )
                    abort_reason = "recording not confirmed before note stream"
                    break
                if serial_collector is not None and reached_recording:
                    # Serial state sync confirms capture is active; start stream immediately.
                    time.sleep(0.0)
                elif serial_collector is None:
                    time.sleep(min(args.phase_wait_ms, 120) / 1000.0)

                rec_clock_count = 0
                rec_fallback_seconds = False
                rec_timing = {
                    "grid_steps_emitted": 0.0,
                    "max_abs_grid_jitter_clocks": 0.0,
                    "mean_abs_grid_jitter_clocks": 0.0,
                }
                if args.record_bars and args.bar_sync_from_midi_clock:
                    clock_ok, using_serial_proxy = _ensure_midi_clock_or_serial_proxy(
                        in_port,
                        out_port,
                        serial_collector,
                        args,
                        min_clocks=24,
                        timeout_s=2.0,
                        abort=abort,
                    )
                    if not clock_ok:
                        print("[error] MIDI clock missing before record phase; aborting track run.")
                        abort_reason = "midi clock missing before record phase"
                        break
                    wall_clock_tempo = _resolve_wall_tempo_for_proxy(
                        serial_collector, args, using_serial_proxy=using_serial_proxy
                    )
                    if using_serial_proxy:
                        proxy_bpm = wall_clock_tempo or args.tempo_bpm
                        print(
                            "[info] Bar sync via wall-clock tempo proxy "
                            f"({proxy_bpm:.1f} BPM) — Teensy clock not on USB MIDI in."
                        )
                    guard = max(10.0, args.record_bars * seconds_per_bar * 3.0)
                    if getattr(args, "edit_record_fixture", False):
                        from host_midi_automation_edit_baseline import (
                            EDIT_RECORD_FIXTURE,
                            _stream_fixture_record,
                        )

                        rec_notes, rec_clock_count = _stream_fixture_record(
                            out_port,
                            in_port,
                            fixture=EDIT_RECORD_FIXTURE,
                            target_bars=args.record_bars,
                            midi_channel_1based=args.midi_channel,
                            stop_press_advance_clocks=args.stop_press_advance_clocks,
                            press_ms=args.press_ms,
                            abort=abort,
                        )
                        rec_cc = 0
                        rec_timing = {
                            "grid_steps_emitted": float(rec_notes),
                            "max_abs_grid_jitter_clocks": 0.0,
                            "mean_abs_grid_jitter_clocks": 0.0,
                        }
                    else:
                        rec_notes, rec_cc, rec_clock_count, rec_timing = _stream_pattern_for_bars(
                            out_port,
                            in_port,
                            midi_channel_1based=args.midi_channel,
                            low_note=args.record_low_note,
                            high_note=args.record_high_note,
                            step_clocks=RECORD_GRID_STEP_CLOCKS,
                            gate_clocks=RECORD_GRID_STEP_CLOCKS,
                            target_bars=args.record_bars,
                            cc_number=args.cc_number,
                            cc_step=args.cc_step,
                            pitch_cycle_bars=args.pitch_cycle_bars,
                            phase_start_delay_bars=0,
                            phase_start_delay_beats=0,
                            max_seconds_guard=guard,
                            fixed_note=args.record_fixed_note if args.fixed_grid_notes else None,
                            stop_press_advance_clocks=args.stop_press_advance_clocks,
                            abort=abort,
                            emit_immediate_first_step=True,
                            wall_clock_tempo_bpm=wall_clock_tempo,
                            stream_start_monotonic=recording_confirmed_at,
                        )
                else:
                    rec_notes, rec_cc = _stream_dense_chromatic(
                        out_port,
                        in_port,
                        midi_channel_1based=args.midi_channel,
                        root_note=args.root_note + (idx % 12),
                        semitone_span=args.semitone_span,
                        duration_s=args.record_seconds,
                        note_gap_ms=args.note_gap_ms,
                        gate_ms=args.gate_ms,
                        cc_number=args.cc_number,
                        cc_step=args.cc_step,
                        abort=abort,
                    )

                if (reason := abort.check()) is not None:
                    abort_reason = reason
                    break

                if args.record_bars and args.bar_sync_from_midi_clock and rec_clock_count <= 0:
                    print("[error] Record phase saw 0 MIDI clock pulses; aborting track run.")
                    abort_reason = "record phase saw 0 midi clock pulses"
                    break

                time.sleep(max(args.post_stream_settle_ms, 0) / 1000.0)
                print(f"[track {idx}] record stop (returns to play)")
                expected_play_count = None
                if serial_collector is not None:
                    counts = _count_capture_transitions(serial_collector.snapshot())
                    expected_play_count = counts.get(("STOPPED_RECORDING", "PLAYING"), 0) + 1
                _send_short_press(
                    out_port,
                    note=RECORD_BUTTON_NOTE,
                    channel_1based=CONTROL_CHANNEL_1BASED,
                    press_ms=args.press_ms,
                )
                if serial_collector is not None and expected_play_count is not None:
                    reached = _wait_for_transition_count(
                        serial_collector,
                        from_state="STOPPED_RECORDING",
                        to_state="PLAYING",
                        target_count=expected_play_count,
                        timeout_s=args.state_sync_timeout_ms / 1000.0,
                        abort=abort,
                    )
                    if not reached:
                        print("[warn] Timed out waiting for STOPPED_RECORDING->PLAYING transition; retrying record stop press.")
                        _send_short_press(
                            out_port,
                            note=RECORD_BUTTON_NOTE,
                            channel_1based=CONTROL_CHANNEL_1BASED,
                            press_ms=args.press_ms,
                        )
                        reached = _wait_for_transition_count(
                            serial_collector,
                            from_state="STOPPED_RECORDING",
                            to_state="PLAYING",
                            target_count=expected_play_count,
                            timeout_s=args.state_sync_timeout_ms / 1000.0,
                            abort=abort,
                        )
                        if not reached:
                            print("[warn] Retry did not reach STOPPED_RECORDING->PLAYING transition.")
                            if args.record_only:
                                print(
                                    "[info] record-only seed: continuing without PLAYING "
                                    "transition (edit_minimal uses captured loop on device)."
                                )
                            else:
                                print(
                                    "[error] Record stop did not return to PLAYING; "
                                    "aborting track run (avoid overdub presses on wrong state)."
                                )
                                abort_reason = "record stop did not reach PLAYING"
                                break
                time.sleep(args.phase_wait_ms / 1000.0)
                if abort_reason:
                    break

                if args.record_only:
                    midi_in_messages += _drain_input_messages(in_port)
                    per_track_stats.append(
                        {
                            "track_index": idx,
                            "record_notes_sent": rec_notes,
                            "record_cc_sent": rec_cc,
                            "record_clock_pulses_seen": rec_clock_count,
                            "record_used_seconds_fallback": rec_fallback_seconds,
                            "record_grid_steps_emitted": int(rec_timing["grid_steps_emitted"]),
                            "record_max_abs_grid_jitter_clocks": rec_timing["max_abs_grid_jitter_clocks"],
                            "record_mean_abs_grid_jitter_clocks": rec_timing["mean_abs_grid_jitter_clocks"],
                            "overdub_notes_sent": 0,
                            "overdub_cc_sent": 0,
                            "overdub_clock_pulses_seen": 0,
                            "overdub_used_seconds_fallback": False,
                            "overdub_grid_steps_emitted": 0,
                            "overdub_max_abs_grid_jitter_clocks": 0.0,
                            "overdub_mean_abs_grid_jitter_clocks": 0.0,
                            "second_overdub_notes_sent": 0,
                            "second_overdub_cc_sent": 0,
                            "second_overdub_clock_pulses_seen": 0,
                            "second_overdub_used_seconds_fallback": False,
                            "second_overdub_grid_steps_emitted": 0,
                            "second_overdub_max_abs_grid_jitter_clocks": 0.0,
                            "second_overdub_mean_abs_grid_jitter_clocks": 0.0,
                        }
                    )
                    continue

                od2_notes = 0
                od2_cc = 0
                od2_clock_count = 0
                od2_timing: dict[str, float] = {
                    "grid_steps_emitted": 0.0,
                    "max_abs_grid_jitter_clocks": 0.0,
                    "mean_abs_grid_jitter_clocks": 0.0,
                    "stop_press_sent_during_stream": 0.0,
                }
                od2_fallback_seconds = False
                od_notes = 0
                od_cc = 0
                od_clock_count = 0
                od_timing: dict[str, float] = {
                    "grid_steps_emitted": 0.0,
                    "max_abs_grid_jitter_clocks": 0.0,
                    "mean_abs_grid_jitter_clocks": 0.0,
                    "stop_press_sent_during_stream": 0.0,
                }
                od_fallback_seconds = False

                if not args.record_only:
                    od_pass_reason, od_notes, od_cc, od_clock_count, od_timing, od_fallback_seconds = _run_overdub_pass(
                        idx,
                        out_port,
                        in_port,
                        serial_collector,
                        abort,
                        args,
                        overdub_bars=args.overdub_bars or 0,
                        low_note=args.overdub_low_note,
                        high_note=args.overdub_high_note,
                        step_clocks=OVERDUB_GRID_STEP_CLOCKS,
                        gate_clocks=OVERDUB_GRID_STEP_CLOCKS,
                        phase_start_delay_bars=args.overdub_start_delay_bars,
                        phase_start_delay_beats=args.overdub_start_delay_beats,
                        pass_label="overdub",
                        seconds_per_bar=seconds_per_bar,
                    )
                    if od_pass_reason:
                        if od_pass_reason.startswith("midi clock"):
                            print(f"[error] {od_pass_reason}; aborting track run.")
                        abort_reason = od_pass_reason
                        break

                    if args.second_overdub_bars:
                        od2_pass_reason, od2_notes, od2_cc, od2_clock_count, od2_timing, od2_fallback_seconds = (
                            _run_overdub_pass(
                                idx,
                                out_port,
                                in_port,
                                serial_collector,
                                abort,
                                args,
                                overdub_bars=args.second_overdub_bars,
                                low_note=args.second_overdub_low_note,
                                high_note=args.second_overdub_high_note,
                                step_clocks=args.second_overdub_step_clocks,
                                gate_clocks=args.second_overdub_gate_clocks,
                                phase_start_delay_bars=args.second_overdub_start_delay_bars,
                                phase_start_delay_beats=args.second_overdub_start_delay_beats,
                                pass_label="second overdub",
                                seconds_per_bar=seconds_per_bar,
                            )
                        )
                        if od2_pass_reason:
                            if od2_pass_reason.startswith("midi clock"):
                                print(f"[error] {od2_pass_reason}; aborting track run.")
                            abort_reason = od2_pass_reason
                            break

                    if args.undo_redo_after_overdub_stop:
                        undo_redo_gap_s = max(args.undo_redo_delay_ms, 0) / 1000.0
                        time.sleep(undo_redo_gap_s)
                        print(f"[track {idx}] undo after overdub stop (double press)")
                        _send_multi_short_press(
                            out_port,
                            note=RECORD_BUTTON_NOTE,
                            channel_1based=CONTROL_CHANNEL_1BASED,
                            press_ms=args.press_ms,
                            count=2,
                        )
                        time.sleep(undo_redo_gap_s)
                        print(f"[track {idx}] redo after overdub stop (triple press)")
                        _send_multi_short_press(
                            out_port,
                            note=RECORD_BUTTON_NOTE,
                            channel_1based=CONTROL_CHANNEL_1BASED,
                            press_ms=args.press_ms,
                            count=3,
                        )
                        time.sleep(args.phase_wait_ms / 1000.0)
                    if args.stop_after_overdub:
                        print(f"[track {idx}] transport stop")
                        _send_short_press(
                            out_port,
                            note=GLOBAL_TRANSPORT_NOTE,
                            channel_1based=CONTROL_CHANNEL_1BASED,
                            press_ms=args.press_ms,
                        )
                        time.sleep(args.phase_wait_ms / 1000.0)

                if (reason := abort.check()) is not None:
                    abort_reason = reason
                    break

                midi_in_messages += _drain_input_messages(in_port)
                per_track_stats.append(
                    {
                        "track_index": idx,
                        "record_notes_sent": rec_notes,
                        "record_cc_sent": rec_cc,
                        "record_clock_pulses_seen": rec_clock_count,
                        "record_used_seconds_fallback": rec_fallback_seconds,
                        "record_grid_steps_emitted": int(rec_timing["grid_steps_emitted"]),
                        "record_max_abs_grid_jitter_clocks": rec_timing["max_abs_grid_jitter_clocks"],
                        "record_mean_abs_grid_jitter_clocks": rec_timing["mean_abs_grid_jitter_clocks"],
                        "overdub_notes_sent": od_notes,
                        "overdub_cc_sent": od_cc,
                        "overdub_clock_pulses_seen": od_clock_count,
                        "overdub_used_seconds_fallback": od_fallback_seconds,
                        "overdub_grid_steps_emitted": int(od_timing["grid_steps_emitted"]),
                        "overdub_max_abs_grid_jitter_clocks": od_timing["max_abs_grid_jitter_clocks"],
                        "overdub_mean_abs_grid_jitter_clocks": od_timing["mean_abs_grid_jitter_clocks"],
                        "second_overdub_notes_sent": od2_notes,
                        "second_overdub_cc_sent": od2_cc,
                        "second_overdub_clock_pulses_seen": od2_clock_count,
                        "second_overdub_used_seconds_fallback": od2_fallback_seconds,
                        "second_overdub_grid_steps_emitted": int(od2_timing["grid_steps_emitted"]),
                        "second_overdub_max_abs_grid_jitter_clocks": od2_timing["max_abs_grid_jitter_clocks"],
                        "second_overdub_mean_abs_grid_jitter_clocks": od2_timing["mean_abs_grid_jitter_clocks"],
                    }
                )

            if abort_reason:
                print(f"Run aborted: {abort_reason}")
            if precondition_failures:
                print("Run aborted due to failed preconditions.")

            # Allow delayed short-press expiration + state transition logs to flush.
            time.sleep(max(args.final_wait_ms, 1) / 1000.0)

    finally:
        serial_lines: list[str] = []
        serial_error = ""
        if serial_collector is not None:
            close_drain_ms = max(getattr(args, "serial_close_drain_ms", 0), 0)
            close_idle_ms = max(getattr(args, "serial_close_idle_ms", 750), 0)
            if close_drain_ms > 0:
                added = serial_collector.drain_until_idle(
                    idle_ms=close_idle_ms,
                    max_drain_ms=close_drain_ms,
                )
                if added > 0:
                    print(
                        f"[info] Serial close drain collected {added} trailing line(s) "
                        f"(idle={close_idle_ms}ms cap={close_drain_ms}ms)"
                    )
                else:
                    print(
                        f"[info] Serial close drain idle "
                        f"({close_idle_ms}ms, cap={close_drain_ms}ms)"
                    )
            serial_lines = serial_collector.snapshot()
            serial_error = serial_collector.error()
            serial_collector.stop()

    verification_lines = serial_lines
    if not verification_lines and args.verify_serial_log is not None and args.verify_serial_log.exists():
        verification_lines = args.verify_serial_log.read_text(encoding="utf-8", errors="replace").splitlines()

    transition_counts = _count_capture_transitions(verification_lines)
    reca_count, recs_count = _count_capture_record_markers(verification_lines)
    cap_lines_count = sum(1 for l in verification_lines if "#CAP," in l)
    undo_log_count = sum(1 for l in verification_lines if "Overdub undone" in l)
    redo_log_count = sum(1 for l in verification_lines if "Overdub redone" in l)
    clear_undo_prune = _extract_clear_undo_prune(verification_lines)

    expected_min = args.track_count
    expected_record_notes_min = 0
    expected_overdub_notes_min = 0
    expected_record_clocks = 0
    expected_overdub_clocks = 0
    expected_second_overdub_notes_min = 0
    expected_second_overdub_clocks = 0
    if args.record_bars and args.bar_sync_from_midi_clock:
        if getattr(args, "edit_record_fixture", False):
            from host_midi_automation_edit_baseline import EDIT_RECORD_FIXTURE

            expected_record_notes_min = len(EDIT_RECORD_FIXTURE)
        else:
            # 16th-note grid; allow one-step edge variance at boundaries.
            expected_record_notes_min = max(1, args.record_bars * 16 - 1)
        expected_record_clocks = args.record_bars * MIDI_CLOCKS_PER_BAR
    if (not args.record_only) and args.overdub_bars and args.bar_sync_from_midi_clock:
        expected_overdub_notes_min = _expected_overdub_notes_min(args.overdub_bars, OVERDUB_GRID_STEP_CLOCKS)
        expected_overdub_clocks = args.overdub_bars * MIDI_CLOCKS_PER_BAR
    if args.second_overdub_bars and args.bar_sync_from_midi_clock:
        expected_second_overdub_notes_min = _expected_overdub_notes_min(
            args.second_overdub_bars, args.second_overdub_step_clocks
        )
        expected_second_overdub_clocks = args.second_overdub_bars * MIDI_CLOCKS_PER_BAR
    if args.overdub_wrap_note_off_test:
        expected_overdub_notes_min = 1
        if args.record_bars:
            expected_overdub_clocks = max(args.overdub_bars, args.record_bars + 1) * MIDI_CLOCKS_PER_BAR

    phase_note_failures: list[dict[str, int]] = []
    phase_activation_failures: list[dict[str, int | str]] = []
    for row in per_track_stats:
        idx = int(row["track_index"])
        rec_notes = int(row["record_notes_sent"])
        od_notes = int(row["overdub_notes_sent"])
        rec_clocks = int(row["record_clock_pulses_seen"])
        od_clocks = int(row["overdub_clock_pulses_seen"])
        od2_notes = int(row.get("second_overdub_notes_sent", 0))
        od2_clocks = int(row.get("second_overdub_clock_pulses_seen", 0))

        if expected_record_notes_min > 0 and rec_clocks <= 0:
            phase_activation_failures.append(
                {
                    "track_index": idx,
                    "phase": "record",
                    "actual_clocks": rec_clocks,
                    "reason": "phase_not_activated_or_no_clock",
                }
            )
        else:
            if expected_record_notes_min > 0 and rec_notes < expected_record_notes_min:
                phase_note_failures.append(
                    {
                        "track_index": idx,
                        "phase": "record",
                        "actual_notes": rec_notes,
                        "expected_notes_min": expected_record_notes_min,
                    }
                )
            if expected_record_clocks > 0 and rec_clocks != expected_record_clocks:
                phase_activation_failures.append(
                    {
                        "track_index": idx,
                        "phase": "record",
                        "actual_clocks": rec_clocks,
                        "expected_clocks": expected_record_clocks,
                        "reason": "clock_count_mismatch",
                    }
                )
        if expected_overdub_notes_min > 0 and od_clocks <= 0:
            phase_activation_failures.append(
                {
                    "track_index": idx,
                    "phase": "overdub",
                    "actual_clocks": od_clocks,
                    "reason": "phase_not_activated_or_no_clock",
                }
            )
        else:
            if expected_overdub_notes_min > 0 and od_notes < expected_overdub_notes_min:
                phase_note_failures.append(
                    {
                        "track_index": idx,
                        "phase": "overdub",
                        "actual_notes": od_notes,
                        "expected_notes_min": expected_overdub_notes_min,
                    }
                )
            if expected_overdub_clocks > 0 and od_clocks != expected_overdub_clocks:
                phase_activation_failures.append(
                    {
                        "track_index": idx,
                        "phase": "overdub",
                        "actual_clocks": od_clocks,
                        "expected_clocks": expected_overdub_clocks,
                        "reason": "clock_count_mismatch",
                    }
                )
        if expected_second_overdub_notes_min > 0 and od2_clocks <= 0:
            phase_activation_failures.append(
                {
                    "track_index": idx,
                    "phase": "second_overdub",
                    "actual_clocks": od2_clocks,
                    "reason": "phase_not_activated_or_no_clock",
                }
            )
        else:
            if expected_second_overdub_notes_min > 0 and od2_notes < expected_second_overdub_notes_min:
                phase_note_failures.append(
                    {
                        "track_index": idx,
                        "phase": "second_overdub",
                        "actual_notes": od2_notes,
                        "expected_notes_min": expected_second_overdub_notes_min,
                    }
                )
            if expected_second_overdub_clocks > 0 and od2_clocks != expected_second_overdub_clocks:
                phase_activation_failures.append(
                    {
                        "track_index": idx,
                        "phase": "second_overdub",
                        "actual_clocks": od2_clocks,
                        "expected_clocks": expected_second_overdub_clocks,
                        "reason": "clock_count_mismatch",
                    }
                )

    transition_checks = []
    for expectation in _expected_transition_expectations(args):
        key = (expectation.from_state, expectation.to_state)
        actual = transition_counts.get(key, 0)
        # After clear, first record can start from EMPTY or STOPPED (transport restart), not only ARMED.
        if expectation.from_state == "ARMED" and expectation.to_state == "RECORDING":
            actual = _record_entry_to_recording_count(transition_counts)
        target = expectation.per_track_min * expected_min
        transition_checks.append(
            {
                "from": expectation.from_state,
                "to": expectation.to_state,
                "actual": actual,
                "expected_min": target,
                "ok": actual >= target if verification_lines else None,
            }
        )

    serial_verification = _build_serial_verification(verification_lines, args) if verification_lines else None
    if serial_verification is not None:
        capture_cleanup = _verify_capture_cleanup(verification_lines, args, per_track_stats)
        serial_verification["capture_cleanup"] = capture_cleanup
        for issue in capture_cleanup.get("issues", []):
            if issue not in serial_verification["issues"]:
                serial_verification["issues"].append(str(issue))

    assertions = {
        "serial_capture_enabled": serial_active,
        "serial_follow_enabled": _serial_follow_active(args),
        "serial_error": serial_error if serial_active else "",
        "serial_line_count": len(serial_lines),
        "verification_line_count": len(verification_lines),
        "reca_count": reca_count,
        "recs_count": recs_count,
        "expected_reca_min": expected_min if verification_lines else None,
        "expected_recs_min": expected_min if verification_lines else None,
        "transition_checks": transition_checks,
        "midi_in_message_count": midi_in_messages,
        "abort_reason": abort_reason or "",
        "timed_out": abort_reason == "run deadline exceeded",
        "heartbeat_lost": bool(abort_reason and "heartbeat" in abort_reason),
        "precondition_failures": precondition_failures,
        "clear_precondition_results": clear_precondition_results,
        "phase_note_failures": phase_note_failures,
        "phase_activation_failures": phase_activation_failures,
        "serial_verification": serial_verification,
        "undo_redo_after_overdub_stop": {
            "enabled": bool(args.undo_redo_after_overdub_stop),
            "delay_ms": args.undo_redo_delay_ms if args.undo_redo_after_overdub_stop else None,
            "undo_log_count": undo_log_count if verification_lines else None,
            "redo_log_count": redo_log_count if verification_lines else None,
            "expected_min": expected_min if (verification_lines and args.undo_redo_after_overdub_stop) else None,
        },
        "clear_undo_prune": clear_undo_prune if (verification_lines and args.clear_before_record) else None,
    }

    overall_ok = True
    if abort_reason:
        overall_ok = False
    if precondition_failures:
        overall_ok = False
    if serial_active:
        if serial_error:
            overall_ok = False
        if not serial_lines:
            overall_ok = False
        if not any("#CAP," in l for l in serial_lines):
            overall_ok = False
    if verification_lines:
        if reca_count < expected_min or recs_count < expected_min:
            overall_ok = False
        for row in transition_checks:
            if row["ok"] is False:
                overall_ok = False
        if args.undo_redo_after_overdub_stop and (undo_log_count < expected_min or redo_log_count < expected_min):
            overall_ok = False
        if (not args.record_only) and args.clear_before_record and not _clear_undo_prune_gate_ok(
            clear_precondition_results,
            clear_undo_prune,
        ):
            overall_ok = False
    if phase_note_failures:
        overall_ok = False
    if phase_activation_failures:
        overall_ok = False
    if serial_verification is not None and serial_verification["issues"]:
        overall_ok = False

    run_finished = datetime.now()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    stamp = run_finished.strftime("%Y%m%d_%H%M%S")
    report_path = args.out_dir / f"host_midi_automation_baseline_{stamp}.json"
    config_for_report = dict(vars(args))
    config_for_report["out_dir"] = str(config_for_report["out_dir"])
    if config_for_report.get("serial_log_path") is not None:
        config_for_report["serial_log_path"] = str(config_for_report["serial_log_path"])
    if config_for_report.get("verify_serial_log") is not None:
        config_for_report["verify_serial_log"] = str(config_for_report["verify_serial_log"])

    report = {
        "started_at": run_started.isoformat(),
        "finished_at": run_finished.isoformat(),
        "config": config_for_report,
        "per_track_stats": per_track_stats,
        "assertions": assertions,
        "overall_ok": overall_ok,
    }
    if args.serial_port:
        serial_log_path = args.serial_log_path
        if serial_log_path is None:
            serial_log_path = args.out_dir / f"host_midi_automation_serial_{stamp}.log"
        serial_log_path.parent.mkdir(parents=True, exist_ok=True)
        serial_log_path.write_text("\n".join(serial_lines) + ("\n" if serial_lines else ""), encoding="utf-8")
        report["serial_log_path"] = str(serial_log_path)
    elif _serial_follow_active(args) and serial_lines:
        report["serial_follow_line_count"] = len(serial_lines)
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")

    print("\nRun summary:")
    print(f"  Tracks run: {args.track_count} (from {args.first_track_index})")
    print(f"  MIDI in messages observed: {midi_in_messages}")
    if verification_lines:
        print(f"  #CAP lines observed: {cap_lines_count}")
        print(f"  RECA count: {reca_count}, RECS count: {recs_count}")
        if args.undo_redo_after_overdub_stop:
            print(
                "  Undo/Redo after overdub-stop logs: "
                f"undo={undo_log_count} redo={redo_log_count} (min {expected_min})"
            )
        if args.clear_before_record:
            print(
                "  Clear undo prune check: "
                f"found={clear_undo_prune.get('found')} "
                f"remaining_zero={clear_undo_prune.get('remaining_zero')} "
                f"remaining={clear_undo_prune.get('remaining')}"
            )
        for row in transition_checks:
            print(f"  ST {row['from']}->{row['to']}: {row['actual']} (min {row['expected_min']})")
        if serial_verification is not None:
            rv = serial_verification["record_phase"]
            ov = serial_verification["overdub_phase"]
            rf = serial_verification["record_first_note_offset"]
            of = serial_verification["overdub_first_note_offset"]
            print(
                "  VERIFY record on/off/open/out/seq: "
                f"{rv['note_on_count']}/{rv['note_off_count']}/{rv['unmatched_open_notes']}/"
                f"{rv['out_of_range_count']}/{rv['sequence_mismatch_count']}"
            )
            print(
                "  VERIFY overdub on/off/open/out/seq: "
                f"{ov['note_on_count']}/{ov['note_off_count']}/{ov['unmatched_open_notes']}/"
                f"{ov['out_of_range_count']}/{ov['sequence_mismatch_count']}"
            )
            second_ov = serial_verification.get("second_overdub_phase")
            if second_ov is not None:
                print(
                    "  VERIFY second overdub on/off/open/out/seq: "
                    f"{second_ov['note_on_count']}/{second_ov['note_off_count']}/"
                    f"{second_ov['unmatched_open_notes']}/{second_ov['out_of_range_count']}/"
                    f"{second_ov['sequence_mismatch_count']}"
                )
            print(
                "  VERIFY first-note capture latency clocks (record/overdub): "
                f"{rf.get('capture_latency_clocks', rf['offset_clocks'])}/"
                f"{of.get('capture_latency_clocks', of['offset_clocks'])} "
                f"(max {args.record_first_note_max_clocks}/{args.overdub_first_note_max_clocks})"
            )
            if (
                of.get("phase_start_delay_clocks")
                and int(of["phase_start_delay_clocks"]) > 0
                and of.get("offset_clocks") is not None
            ):
                print(
                    "    overdub raw offset clocks (ST entry → first note, incl. grid delay): "
                    f"{of['offset_clocks']} (delay {of['phase_start_delay_clocks']})"
                )
            record_loop_length = serial_verification.get("record_loop_length")
            if record_loop_length and not record_loop_length.get("phase_disabled"):
                print(
                    "  VERIFY record loop length (final ticks): "
                    f"{record_loop_length.get('actual_final_length')}/"
                    f"{record_loop_length.get('expected_final_length')} "
                    f"(raw {record_loop_length.get('raw_length')})"
                )
            record_note_span = serial_verification.get("record_note_span")
            if record_note_span and not record_note_span.get("phase_disabled"):
                print(
                    "  VERIFY record note span (raw ticks): "
                    f"{record_note_span.get('actual_raw_length')}/"
                    f"{record_note_span.get('expected_min_raw_length')} "
                    f"(expected span {record_note_span.get('expected_span_ticks')})"
                )
            stored_record_grid = serial_verification.get("stored_record_grid")
            if stored_record_grid and not stored_record_grid.get("phase_disabled"):
                print(
                    "  VERIFY stored record grid (REVT ticks): "
                    f"count={stored_record_grid.get('note_on_count')} "
                    f"delta={stored_record_grid.get('min_delta')}-"
                    f"{stored_record_grid.get('max_delta')} "
                    f"bad={stored_record_grid.get('bad_delta_count')}/"
                    f"{stored_record_grid.get('bad_delta_budget')}"
                )
            record_stop_stage_trace = serial_verification.get("record_stop_stage_trace")
            if record_stop_stage_trace:
                slowest_stage = record_stop_stage_trace.get("slowest_completed_stage")
                slowest_name = "none"
                slowest_duration = 0
                if isinstance(slowest_stage, dict):
                    slowest_name = str(slowest_stage.get("stage", "none"))
                    slowest_duration = int(slowest_stage.get("duration_us", 0))
                print(
                    "  VERIFY record stop stages: "
                    f"first_missing={record_stop_stage_trace.get('first_missing_stage')} "
                    f"slowest={slowest_name}:{slowest_duration}us"
                )
            persistence_handoff = serial_verification.get("persistence_handoff")
            if persistence_handoff and not persistence_handoff.get("phase_disabled"):
                result_row = persistence_handoff.get("result") or {}
                print(
                    "  VERIFY persistence handoff: "
                    f"first_missing={persistence_handoff.get('first_missing_stage')} "
                    f"result={result_row.get('outcome')} "
                    f"duration_us={result_row.get('duration_us')}"
                )
            ram2_headroom = serial_verification.get("ram2_headroom")
            if ram2_headroom and not ram2_headroom.get("phase_disabled"):
                min_sample = ram2_headroom.get("min_sample") or {}
                print(
                    "  VERIFY min free RAM2: "
                    f"{ram2_headroom.get('min_free_ram2_bytes')} bytes "
                    f"at {min_sample.get('source')}:{min_sample.get('stage')}/{min_sample.get('field')}"
                )
            record_stop_ram2_floor = serial_verification.get("record_stop_ram2_floor")
            if record_stop_ram2_floor and not record_stop_ram2_floor.get("phase_disabled"):
                print(
                    "  VERIFY record_stop RAM2 floor: "
                    f"heap_before={record_stop_ram2_floor.get('record_stop_heap_before')} "
                    f"floor={record_stop_ram2_floor.get('floor_bytes')} "
                    f"warn={record_stop_ram2_floor.get('warn_bytes')} "
                    f"ok={record_stop_ram2_floor.get('floor_ok')} "
                    f"warn_ok={record_stop_ram2_floor.get('warn_ok')}"
                )
            long_run_persistence_result = serial_verification.get("long_run_persistence_result")
            if long_run_persistence_result and not long_run_persistence_result.get("phase_disabled"):
                print(
                    "  VERIFY long-run save outcome stage: "
                    f"stage={long_run_persistence_result.get('stage')} "
                    f"outcome={long_run_persistence_result.get('outcome')} "
                    f"ok={long_run_persistence_result.get('result_ok')}"
                )
            overdub_wrap_storage = serial_verification.get("overdub_wrap_storage")
            if overdub_wrap_storage and not overdub_wrap_storage.get("phase_disabled"):
                print(
                    "  VERIFY overdub wrap storage: "
                    f"pairs={overdub_wrap_storage.get('wrap_pair_count')} "
                    f"sevt={overdub_wrap_storage.get('sevt_count')} "
                    f"ok={overdub_wrap_storage.get('pairs_ok')}"
                )
                for pair in overdub_wrap_storage.get("wrap_pairs", []):
                    print(
                        "    WRAP "
                        f"on={pair.get('on_tick')} off={pair.get('off_tick')} "
                        f"note={pair.get('note')} ch={pair.get('ch')} "
                        f"ok={pair.get('ok')}"
                    )
            capture_cleanup = serial_verification.get("capture_cleanup")
            if capture_cleanup is not None:
                print(
                    "  VERIFY capture cleanup (CLN): "
                    f"dedup={capture_cleanup.get('dedup_total')} "
                    f"minlen_pairs={capture_cleanup.get('minlen_pairs_total')} "
                    f"wrap_synth={capture_cleanup.get('wrap_synth_total')} "
                    f"ok={capture_cleanup.get('ok')}"
                )
                by_phase_kind = capture_cleanup.get("by_phase_kind") or {}
                for phase in sorted(by_phase_kind.keys()):
                    kinds = by_phase_kind[phase]
                    print(
                        f"    CLN {phase}: "
                        f"dedup={kinds.get('dedup', 0)} "
                        f"minlen={kinds.get('minlen', 0)} "
                        f"wrap_synth={kinds.get('wrap_synth', 0)}"
                    )
                for row in capture_cleanup.get("reconciliation", []):
                    print(
                        "    CLN reconcile "
                        f"{row.get('phase')}: sent={row.get('notes_sent')} "
                        f"dedup={row.get('dedup_events')} "
                        f"minlen={row.get('minlen_pairs')} "
                        f"wrap_synth={row.get('wrap_synth_offs')}"
                    )
            stored_overdub_span = serial_verification.get("stored_overdub_span")
            if stored_overdub_span and not stored_overdub_span.get("phase_disabled"):
                print(
                    "  VERIFY stored overdub span (SEVT ticks): "
                    f"count={stored_overdub_span.get('note_on_count')}/"
                    f"{stored_overdub_span.get('expected_note_on_count')} "
                    f"mem={stored_overdub_span.get('memory_note_count')} "
                    f"last={stored_overdub_span.get('last_tick')} "
                    f"min_span={stored_overdub_span.get('expected_min_span_ticks')} "
                    f"bars={stored_overdub_span.get('bars_reached')}/{stored_overdub_span.get('expected_bars')} "
                    f"count_ok={stored_overdub_span.get('count_ok')} "
                    f"span_ok={stored_overdub_span.get('span_ok')} "
                    f"bars_ok={stored_overdub_span.get('bars_match_ok')}"
                )
            stored_second_overdub_span = serial_verification.get("stored_second_overdub_span")
            if stored_second_overdub_span and not stored_second_overdub_span.get("phase_disabled"):
                print(
                    "  VERIFY stored second overdub span (SEVT ticks): "
                    f"count={stored_second_overdub_span.get('note_on_count')}/"
                    f"{stored_second_overdub_span.get('expected_note_on_count')} "
                    f"mem={stored_second_overdub_span.get('memory_note_count')} "
                    f"last={stored_second_overdub_span.get('last_tick')} "
                    f"min_span={stored_second_overdub_span.get('expected_min_span_ticks')} "
                    f"bars={stored_second_overdub_span.get('bars_reached')}/"
                    f"{stored_second_overdub_span.get('expected_bars')} "
                    f"count_ok={stored_second_overdub_span.get('count_ok')} "
                    f"span_ok={stored_second_overdub_span.get('span_ok')} "
                    f"bars_ok={stored_second_overdub_span.get('bars_match_ok')}"
                )
            display_verification = serial_verification.get("display_verification")
            if display_verification:
                print(
                    "  VERIFY display (DISP frame notes after overdub/transport stop): "
                    f"overdub_ok={display_verification.get('overdub_ok')} "
                    f"transport_ok={display_verification.get('transport_ok')}"
                )
                last_od = display_verification.get("snapshots_after_overdub") or []
                if last_od:
                    row = last_od[-1]
                    print(
                        "    DISP after overdub: "
                        f"state={row.get('state')} frame={row.get('frame_notes')} "
                        f"epoch={row.get('epoch_events')} visual={row.get('visual_notes')} "
                        f"buffer={row.get('buffer_events')}"
                    )
                last_ts = display_verification.get("snapshots_after_transport_stop") or []
                if last_ts:
                    row = last_ts[-1]
                    print(
                        "    DISP after transport stop: "
                        f"state={row.get('state')} frame={row.get('frame_notes')} "
                        f"epoch={row.get('epoch_events')} visual={row.get('visual_notes')} "
                        f"buffer={row.get('buffer_events')}"
                    )
            if serial_verification["issues"]:
                print(f"  VERIFY issues: {', '.join(serial_verification['issues'])}")
    if phase_note_failures:
        for row in phase_note_failures:
            print(
                "  PHASE FAIL: "
                f"track {row['track_index']} {row['phase']} notes={row['actual_notes']} "
                f"(min {row['expected_notes_min']})"
            )
    if phase_activation_failures:
        for row in phase_activation_failures:
            print(
                "  PHASE BLOCKED: "
                f"track {row['track_index']} {row['phase']} clocks={row['actual_clocks']} "
                f"reason={row['reason']}"
            )
    if not verification_lines:
        if serial_active:
            print("  Serial assertions requested but no serial lines were captured.")
        else:
            print("  Serial assertions skipped (no serial capture).")
    if serial_active and serial_error:
        print(f"  Serial error: {serial_error}")
    if abort_reason:
        print(f"  Abort reason: {abort_reason}")
    print(f"  Report: {report_path}")

    if not overall_ok:
        print("  Result: FAIL (serial/assertion checks failed)")
        return 2
    print("  Result: PASS")
    return 0


if __name__ == "__main__":
    import sys
    from pathlib import Path

    if "-h" in sys.argv or "--help" in sys.argv:
        raise SystemExit(run())

    _root = Path(__file__).resolve().parent
    if str(_root) not in sys.path:
        sys.path.insert(0, str(_root))
    from hitl.runner import main as hitl_main

    raise SystemExit(hitl_main(["run", "--preset", "base", *sys.argv[1:]]))
