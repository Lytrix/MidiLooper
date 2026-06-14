#!/usr/bin/env python3
"""Local hardware automation baseline for record/overdub per track.

This script runs on the Mac and drives a connected Teensy over USB MIDI.
It automates:
  - track select
  - record start
  - record stop (which returns to playback in firmware)
  - overdub start
  - overdub stop
while streaming dense chromatic notes plus CC data on a recordable channel.

It can optionally read USB serial capture output from the Teensy capture build
to assert expected Track state transitions.
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
from typing import Optional

import serial
from serial import SerialException

try:
    import mido
except ImportError as exc:  # pragma: no cover - import guard
    raise SystemExit(
        "Missing dependency 'mido'. Install with:\n"
        "  python3 -m pip install mido python-rtmidi pyserial"
    ) from exc


TRACK_SELECT_NOTE_BASE = 60
RECORD_BUTTON_NOTE = 36
GLOBAL_TRANSPORT_NOTE = 39
CONTROL_CHANNEL_1BASED = 16


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


class SerialCaptureCollector:
    """Collects Teensy serial lines in a background thread."""

    def __init__(self, port: str, baud: int, timeout: float = 0.05) -> None:
        self._serial = serial.Serial(port, baud, timeout=timeout)
        self._lines: list[str] = []
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._error: str = ""
        self._thread = threading.Thread(target=self._run, daemon=True)

    def start(self) -> None:
        self._thread.start()

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
        except SerialException as exc:
            self._error = str(exc)
            self._stop.set()

    def snapshot(self) -> list[str]:
        with self._lock:
            return list(self._lines)

    def error(self) -> str:
        return self._error


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


def _drain_input_messages(in_port: mido.ports.BaseInput) -> int:
    count = 0
    while True:
        msg = in_port.poll()
        if msg is None:
            break
        count += 1
    return count


def _clock_seen_within(in_port: mido.ports.BaseInput, timeout_s: float) -> bool:
    deadline = time.monotonic() + max(timeout_s, 0.0)
    while time.monotonic() < deadline:
        msg = in_port.poll()
        if msg is None:
            time.sleep(0.001)
            continue
        if msg.type == "clock":
            return True
    return False


def _wait_for_clock_pulses(
    in_port: mido.ports.BaseInput,
    pulses: int,
    *,
    timeout_s: float,
    run_deadline: Optional[float],
) -> int:
    if pulses <= 0:
        return 0
    deadline = time.monotonic() + max(timeout_s, 0.0)
    seen = 0
    while seen < pulses:
        now = time.monotonic()
        if run_deadline is not None and now >= run_deadline:
            break
        if now >= deadline:
            break
        msg = in_port.poll()
        if msg is None:
            time.sleep(0.0005)
            continue
        if msg.type == "clock":
            seen += 1
    return seen


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
    run_deadline: Optional[float] = None,
) -> tuple[int, int]:
    ch = midi_channel_1based - 1
    start = time.monotonic()
    note_on_count = 0
    cc_count = 0
    i = 0
    cc_val = 0

    while time.monotonic() - start < duration_s:
        if run_deadline is not None and time.monotonic() >= run_deadline:
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


def _stream_pattern_for_bars(
    out_port: mido.ports.BaseOutput,
    in_port: mido.ports.BaseInput,
    *,
    midi_channel_1based: int,
    low_note: int,
    high_note: int,
    step_clocks: int,
    target_bars: int,
    cc_number: int,
    cc_step: int,
    pitch_cycle_bars: int,
    phase_start_delay_bars: int,
    phase_start_delay_beats: int,
    max_seconds_guard: float,
    fixed_note: Optional[int] = None,
    stop_press_advance_clocks: int = 0,
    stop_press_note: Optional[int] = None,
    stop_press_channel_1based: int = CONTROL_CHANNEL_1BASED,
    stop_press_press_ms: int = 85,
    clock_start_timeout_seconds: float = 0.75,
    run_deadline: Optional[float] = None,
    emit_immediate_first_step: bool = False,
) -> tuple[int, int, int, dict[str, float]]:
    """Send note pattern until target bar count from MIDI clock.

    Uses incoming realtime MIDI Clock messages (`type == "clock"`, 24 PPQN).
    One 4/4 bar is 96 clock pulses.
    """
    if high_note < low_note:
        raise ValueError("high_note must be >= low_note")
    if step_clocks <= 0:
        raise ValueError("step_clocks must be > 0")
    if pitch_cycle_bars <= 0:
        raise ValueError("pitch_cycle_bars must be > 0")
    if phase_start_delay_bars < 0:
        raise ValueError("phase_start_delay_bars must be >= 0")
    if phase_start_delay_beats < 0:
        raise ValueError("phase_start_delay_beats must be >= 0")

    ch = midi_channel_1based - 1
    # Drop queued MIDI from previous phases so bar counting starts at this phase boundary.
    while True:
        msg = in_port.poll()
        if msg is None:
            break

    note_range = list(range(low_note, high_note + 1))
    note_index = 0
    note_on_count = 0
    cc_count = 0
    clock_count_total = 0
    phase_clock_count = 0
    cc_val = 0
    target_clocks = target_bars * 96
    phase_delay_clocks = (phase_start_delay_bars * 96) + (phase_start_delay_beats * 24)
    pitch_cycle_clocks = pitch_cycle_bars * 96
    stop_press_trigger_clock: Optional[int] = None
    if stop_press_note is not None and stop_press_advance_clocks > 0:
        stop_press_trigger_clock = max(1, target_clocks - stop_press_advance_clocks)
    stop_press_sent_during_stream = False
    stop_press_noteoff_due_at: Optional[float] = None
    next_step_clock = step_clocks
    immediate_step_emitted = False
    held_notes: list[tuple[int, int]] = []  # (note, off_at_clock)
    start = time.monotonic()
    jitter_samples: list[int] = []

    while phase_clock_count < target_clocks:
        if run_deadline is not None and time.monotonic() >= run_deadline:
            break
        if stop_press_noteoff_due_at is not None and time.monotonic() >= stop_press_noteoff_due_at:
            out_port.send(
                mido.Message(
                    "note_off",
                    channel=stop_press_channel_1based - 1,
                    note=stop_press_note,
                    velocity=0,
                )
            )
            stop_press_noteoff_due_at = None
        elapsed = time.monotonic() - start
        if elapsed > max_seconds_guard:
            break
        # Fast fallback trigger: if clock never appears, don't stall the phase.
        if clock_count_total == 0 and elapsed >= clock_start_timeout_seconds:
            break

        msg = in_port.poll()
        if msg is None:
            time.sleep(0.0005)
            continue
        if msg.type != "clock":
            continue

        clock_count_total += 1

        if clock_count_total <= phase_delay_clocks:
            continue

        phase_clock_count += 1

        # Close notes whose gate duration elapsed.
        still_held: list[tuple[int, int]] = []
        for note, off_at in held_notes:
            if phase_clock_count >= off_at:
                out_port.send(mido.Message("note_off", channel=ch, note=note, velocity=0))
            else:
                still_held.append((note, off_at))
        held_notes = still_held

        if emit_immediate_first_step and not immediate_step_emitted:
            if fixed_note is not None:
                note = fixed_note
            else:
                note = note_range[note_index % len(note_range)]
            out_port.send(mido.Message("note_on", channel=ch, note=note, velocity=98))
            note_on_count += 1
            out_port.send(mido.Message("control_change", channel=ch, control=cc_number, value=cc_val))
            cc_count += 1
            gate_clocks = max(1, step_clocks // 2)
            held_notes.append((note, phase_clock_count + gate_clocks))
            note_index += 1
            cc_val = (cc_val + cc_step) % 128
            immediate_step_emitted = True

        if phase_clock_count >= next_step_clock:
            if fixed_note is not None:
                note = fixed_note
            else:
                note = note_range[note_index % len(note_range)]
            jitter_samples.append(phase_clock_count - next_step_clock)
            out_port.send(mido.Message("note_on", channel=ch, note=note, velocity=98))
            note_on_count += 1
            out_port.send(mido.Message("control_change", channel=ch, control=cc_number, value=cc_val))
            cc_count += 1

            gate_clocks = max(1, step_clocks // 2)
            held_notes.append((note, phase_clock_count + gate_clocks))
            note_index += 1
            cc_val = (cc_val + cc_step) % 128
            next_step_clock += step_clocks
            if fixed_note is None and (phase_clock_count % pitch_cycle_clocks) == 0:
                note_index = 0

        if (
            stop_press_note is not None
            and stop_press_trigger_clock is not None
            and not stop_press_sent_during_stream
            and phase_clock_count >= stop_press_trigger_clock
        ):
            out_port.send(
                mido.Message(
                    "note_on",
                    channel=stop_press_channel_1based - 1,
                    note=stop_press_note,
                    velocity=127,
                )
            )
            stop_press_noteoff_due_at = time.monotonic() + max(stop_press_press_ms, 1) / 1000.0
            stop_press_sent_during_stream = True

    # Ensure all active notes are released at phase end.
    for note, _off_at in held_notes:
        out_port.send(mido.Message("note_off", channel=ch, note=note, velocity=0))
    if stop_press_noteoff_due_at is not None:
        out_port.send(
            mido.Message(
                "note_off",
                channel=stop_press_channel_1based - 1,
                note=stop_press_note,
                velocity=0,
            )
        )

    if jitter_samples:
        max_abs = max(abs(x) for x in jitter_samples)
        mean_abs = sum(abs(x) for x in jitter_samples) / len(jitter_samples)
    else:
        max_abs = 0
        mean_abs = 0.0
    timing = {
        "grid_steps_emitted": float(len(jitter_samples)),
        "max_abs_grid_jitter_clocks": float(max_abs),
        "mean_abs_grid_jitter_clocks": float(mean_abs),
        "stop_press_sent_during_stream": 1.0 if stop_press_sent_during_stream else 0.0,
    }
    return note_on_count, cc_count, phase_clock_count, timing


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
    run_deadline: Optional[float] = None,
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
        if run_deadline is not None and time.monotonic() >= run_deadline:
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


def _extract_phase_boundaries(lines: list[str]) -> dict[str, Optional[int]]:
    boundaries: dict[str, Optional[int]] = {
        "record_start_ts": None,
        "record_stop_ts": None,
        "overdub_start_ts": None,
        "overdub_stop_ts": None,
    }
    for line in lines:
        if ",ST,Track," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 6:
            continue
        try:
            ts = int(parts[1])
        except ValueError:
            continue
        from_state = parts[4].strip()
        to_state = parts[5].strip()
        if to_state == "RECORDING" and boundaries["record_start_ts"] is None:
            boundaries["record_start_ts"] = ts
        elif from_state == "RECORDING" and to_state == "STOPPED_RECORDING" and boundaries["record_stop_ts"] is None:
            boundaries["record_stop_ts"] = ts
        elif from_state == "PLAYING" and to_state == "OVERDUBBING" and boundaries["overdub_start_ts"] is None:
            boundaries["overdub_start_ts"] = ts
        elif from_state == "OVERDUBBING" and to_state in ("PLAYING", "STOPPED") and boundaries["overdub_stop_ts"] is None:
            boundaries["overdub_stop_ts"] = ts
    return boundaries


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


def _extract_recs_lengths(lines: list[str]) -> list[dict[str, int]]:
    rows: list[dict[str, int]] = []
    for line in lines:
        if ",RECS," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 6:
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


def _extract_first_note_offset(
    lines: list[str],
    *,
    midi_channel_1based: int,
    phase_start_ts: Optional[int],
    phase_stop_ts: Optional[int],
) -> dict[str, object]:
    if phase_start_ts is None or phase_stop_ts is None or phase_stop_ts <= phase_start_ts:
        return {
            "offset_us": None,
            "offset_ms": None,
            "offset_clocks": None,
            "phase_missing": True,
        }

    first_note_ts: Optional[int] = None
    clocks_until_first_note = 0
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
        except ValueError:
            continue
        if ts < phase_start_ts or ts >= phase_stop_ts:
            continue
        if msg_type == 240 and first_note_ts is None:
            clocks_until_first_note += 1
            continue
        if msg_type == 144 and channel == midi_channel_1based:
            first_note_ts = ts
            break

    if first_note_ts is None:
        return {
            "offset_us": None,
            "offset_ms": None,
            "offset_clocks": None,
            "phase_missing": False,
        }

    offset_us = first_note_ts - phase_start_ts
    return {
        "offset_us": offset_us,
        "offset_ms": offset_us / 1000.0,
        "offset_clocks": clocks_until_first_note,
        "phase_missing": False,
    }


def _build_serial_verification(lines: list[str], args: argparse.Namespace) -> dict[str, object]:
    boundaries = _extract_phase_boundaries(lines)
    record = _verify_phase_note_pairs(
        lines,
        midi_channel_1based=args.midi_channel,
        phase_start_ts=boundaries["record_start_ts"],
        phase_stop_ts=boundaries["record_stop_ts"],
        low_note=args.record_low_note,
        high_note=args.record_high_note,
    )
    overdub = _verify_phase_note_pairs(
        lines,
        midi_channel_1based=args.midi_channel,
        phase_start_ts=boundaries["overdub_start_ts"],
        phase_stop_ts=boundaries["overdub_stop_ts"],
        low_note=args.overdub_low_note,
        high_note=args.overdub_high_note,
    )
    recs_lengths = _extract_recs_lengths(lines)
    record_first_note_offset = _extract_first_note_offset(
        lines,
        midi_channel_1based=args.midi_channel,
        phase_start_ts=boundaries["record_start_ts"],
        phase_stop_ts=boundaries["record_stop_ts"],
    )
    overdub_first_note_offset = _extract_first_note_offset(
        lines,
        midi_channel_1based=args.midi_channel,
        phase_start_ts=boundaries["overdub_start_ts"],
        phase_stop_ts=boundaries["overdub_stop_ts"],
    )
    issues: list[str] = []
    if record["phase_missing"] or overdub["phase_missing"]:
        issues.append("missing_phase_boundaries")
    if record["unmatched_open_notes"] > 0:
        issues.append("record_unmatched_open_notes")
    if overdub["unmatched_open_notes"] > 0:
        issues.append("overdub_unmatched_open_notes")
    if record["out_of_range_count"] > 0:
        issues.append("record_out_of_range_notes")
    if overdub["out_of_range_count"] > 0:
        issues.append("overdub_out_of_range_notes")
    record_offset_clocks = record_first_note_offset["offset_clocks"]
    overdub_offset_clocks = overdub_first_note_offset["offset_clocks"]
    if record_offset_clocks is None:
        issues.append("record_first_note_missing")
    elif record_offset_clocks > args.record_first_note_max_clocks:
        issues.append("record_first_note_too_late")
    if overdub_offset_clocks is None:
        issues.append("overdub_first_note_missing")
    elif overdub_offset_clocks > args.overdub_first_note_max_clocks:
        issues.append("overdub_first_note_too_late")
    return {
        "boundaries": boundaries,
        "record_phase": record,
        "overdub_phase": overdub,
        "record_first_note_offset": record_first_note_offset,
        "overdub_first_note_offset": overdub_first_note_offset,
        "recs_lengths": recs_lengths,
        "issues": issues,
    }


def _wait_for_state_entry_count(
    collector: SerialCaptureCollector,
    *,
    to_state: str,
    target_count: int,
    timeout_s: float,
) -> bool:
    deadline = time.monotonic() + max(timeout_s, 0.0)
    while time.monotonic() < deadline:
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
) -> bool:
    deadline = time.monotonic() + max(timeout_s, 0.0)
    key = (from_state, to_state)
    while time.monotonic() < deadline:
        counts = _count_capture_transitions(collector.snapshot())
        if counts.get(key, 0) >= target_count:
            return True
        time.sleep(0.01)
    return False


def run() -> int:
    parser = argparse.ArgumentParser(description="Host-side MIDI automation baseline")
    parser.add_argument("--midi-out", default="Teensy", help="MIDI output port substring")
    parser.add_argument("--midi-in", default="Teensy", help="MIDI input port substring")
    parser.add_argument("--serial-port", default="", help="Optional Teensy USB serial port (capture build)")
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
    parser.add_argument("--track-number", type=int, default=0, help="Single track to run (1-8); overrides track-count/index")
    parser.add_argument("--record-seconds", type=float, default=3.0, help="Dense stream duration for record phase")
    parser.add_argument("--overdub-seconds", type=float, default=2.0, help="Dense stream duration for overdub phase")
    parser.add_argument(
        "--record-bars",
        type=int,
        choices=[2, 4, 5, 8, 16, 32, 64, 128],
        default=0,
        help="Record duration in bars (overrides --record-seconds)",
    )
    parser.add_argument(
        "--overdub-bars",
        type=int,
        choices=[2, 4, 5, 8, 16, 32, 64, 128],
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
    parser.add_argument("--midi-channel", type=int, default=1, help="Dense input MIDI channel (1-16)")
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
        default=180.0,
        help="Hard timeout for the full script run; writes report and exits if exceeded",
    )
    parser.add_argument(
        "--state-sync-timeout-ms",
        type=int,
        default=2500,
        help="Max wait for serial-confirmed state transitions before phase stream starts",
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
    parser.add_argument("--record-low-note", type=int, default=48, help="Record phase lowest note (default C3)")
    parser.add_argument("--record-high-note", type=int, default=79, help="Record phase highest note (default G5)")
    parser.add_argument("--overdub-low-note", type=int, default=24, help="Overdub phase lowest note (default C1)")
    parser.add_argument("--overdub-high-note", type=int, default=39, help="Overdub phase highest note (default D#2)")
    parser.add_argument(
        "--pitch-cycle-bars",
        type=int,
        default=2,
        help="Reset chromatic pitch progression every N bars (default: 2)",
    )
    args = parser.parse_args()

    if args.track_number:
        if not (1 <= args.track_number <= 8):
            raise SystemExit("--track-number must be in [1, 8]")
        args.first_track_index = args.track_number - 1
        args.track_count = 1
    else:
        if not (1 <= args.track_count <= 8):
            raise SystemExit("--track-count must be in [1, 8]")
        if not (0 <= args.first_track_index <= 7):
            raise SystemExit("--first-track-index must be in [0, 7]")
        if args.first_track_index + args.track_count > 8:
            raise SystemExit("first-track-index + track-count exceeds 8 tracks")

    if not (1 <= args.midi_channel <= 16):
        raise SystemExit("--midi-channel must be in [1, 16]")
    if args.midi_channel == CONTROL_CHANNEL_1BASED:
        raise SystemExit("--midi-channel 16 is excluded from recording; choose 1-15")
    if args.tempo_bpm <= 0:
        raise SystemExit("--tempo-bpm must be > 0")
    if args.max_run_seconds <= 0:
        raise SystemExit("--max-run-seconds must be > 0")
    if args.record_first_note_max_clocks < 0:
        raise SystemExit("--record-first-note-max-clocks must be >= 0")
    if args.overdub_first_note_max_clocks < 0:
        raise SystemExit("--overdub-first-note-max-clocks must be >= 0")
    if args.overdub_start_delay_beats < 0:
        raise SystemExit("--overdub-start-delay-beats must be >= 0")

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

    serial_collector: Optional[SerialCaptureCollector] = None
    if args.serial_port:
        serial_collector = SerialCaptureCollector(args.serial_port, args.serial_baud)
        serial_collector.start()
        time.sleep(1.5)  # Teensy serial can reset on open.
        print(f"Serial capture enabled: {args.serial_port}")
    else:
        print("Serial capture disabled (no #CAP assertions).")

    run_started = datetime.now()
    run_deadline = time.monotonic() + args.max_run_seconds
    per_track_stats: list[dict[str, int]] = []
    midi_in_messages = 0
    timed_out = False
    precondition_failures: list[dict[str, object]] = []

    try:
        with mido.open_output(midi_out_name) as out_port, mido.open_input(midi_in_name) as in_port:
            if args.start_transport:
                # Ensure transport is running without blindly toggling it off.
                if not _clock_seen_within(in_port, 0.5):
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
                if time.monotonic() >= run_deadline:
                    timed_out = True
                    break
                print(f"[track {idx}] select")
                _send_short_press(
                    out_port,
                    note=TRACK_SELECT_NOTE_BASE + idx,
                    channel_1based=CONTROL_CHANNEL_1BASED,
                    press_ms=args.press_ms,
                )
                time.sleep(args.phase_wait_ms / 1000.0)

                if args.clear_before_record:
                    if time.monotonic() >= run_deadline:
                        timed_out = True
                        break
                    print(f"[track {idx}] clear selected loop (long press)")
                    expected_empty_count = None
                    if serial_collector is not None:
                        state_counts = _count_capture_state_entries(serial_collector.snapshot())
                        expected_empty_count = state_counts.get("EMPTY", 0) + 1
                    _send_short_press(
                        out_port,
                        note=RECORD_BUTTON_NOTE,
                        channel_1based=CONTROL_CHANNEL_1BASED,
                        press_ms=args.clear_press_ms,
                    )
                    if serial_collector is not None and expected_empty_count is not None:
                        reached_empty = _wait_for_state_entry_count(
                            serial_collector,
                            to_state="EMPTY",
                            target_count=expected_empty_count,
                            timeout_s=args.state_sync_timeout_ms / 1000.0,
                        )
                        if not reached_empty:
                            print("[warn] Timed out waiting for clear->EMPTY transition; retrying clear long press.")
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

                print(f"[track {idx}] record start")
                reached_recording = False
                expected_recording_count = None
                if serial_collector is not None:
                    state_counts = _count_capture_state_entries(serial_collector.snapshot())
                    expected_recording_count = state_counts.get("RECORDING", 0) + 1
                _send_short_press(
                    out_port,
                    note=RECORD_BUTTON_NOTE,
                    channel_1based=CONTROL_CHANNEL_1BASED,
                    press_ms=args.press_ms,
                )
                if serial_collector is not None and expected_recording_count is not None:
                    reached_recording = _wait_for_state_entry_count(
                        serial_collector,
                        to_state="RECORDING",
                        target_count=expected_recording_count,
                        timeout_s=args.state_sync_timeout_ms / 1000.0,
                    )
                    if not reached_recording:
                        print("[warn] Timed out waiting for ->RECORDING transition; retrying record start press.")
                        _send_short_press(
                            out_port,
                            note=RECORD_BUTTON_NOTE,
                            channel_1based=CONTROL_CHANNEL_1BASED,
                            press_ms=args.press_ms,
                        )
                        reached_recording = _wait_for_state_entry_count(
                            serial_collector,
                            to_state="RECORDING",
                            target_count=expected_recording_count,
                            timeout_s=args.state_sync_timeout_ms / 1000.0,
                        )
                        if not reached_recording:
                            print("[warn] Retry did not reach RECORDING.")
                if serial_collector is not None and reached_recording:
                    # Serial state sync confirms capture is active; start stream immediately.
                    time.sleep(0.0)
                else:
                    time.sleep(min(args.phase_wait_ms, 120) / 1000.0)

                rec_clock_count = 0
                rec_fallback_seconds = False
                rec_timing = {
                    "grid_steps_emitted": 0.0,
                    "max_abs_grid_jitter_clocks": 0.0,
                    "mean_abs_grid_jitter_clocks": 0.0,
                }
                if args.record_bars and args.bar_sync_from_midi_clock:
                    guard = max(10.0, args.record_bars * seconds_per_bar * 3.0)
                    rec_notes, rec_cc, rec_clock_count, rec_timing = _stream_pattern_for_bars(
                        out_port,
                        in_port,
                        midi_channel_1based=args.midi_channel,
                        low_note=args.record_low_note,
                        high_note=args.record_high_note,
                        step_clocks=6,  # 16th notes at 24 PPQN clock
                        target_bars=args.record_bars,
                        cc_number=args.cc_number,
                        cc_step=args.cc_step,
                        pitch_cycle_bars=args.pitch_cycle_bars,
                        phase_start_delay_bars=0,
                        phase_start_delay_beats=0,
                        max_seconds_guard=guard,
                        fixed_note=args.record_fixed_note if args.fixed_grid_notes else None,
                        stop_press_advance_clocks=args.stop_press_advance_clocks,
                        run_deadline=run_deadline,
                        emit_immediate_first_step=True,
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
                        run_deadline=run_deadline,
                    )

                if time.monotonic() >= run_deadline:
                    timed_out = True
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
                        )
                        if not reached:
                            print("[warn] Retry did not reach STOPPED_RECORDING->PLAYING transition.")
                time.sleep(args.phase_wait_ms / 1000.0)

                print(f"[track {idx}] overdub start")
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
                    )
                    if not reached_overdub:
                        print("[warn] Timed out waiting for PLAYING->OVERDUBBING transition; retrying overdub start press.")
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
                        )
                        if not reached_overdub:
                            print("[warn] Retry did not reach PLAYING->OVERDUBBING transition.")
                if serial_collector is not None and reached_overdub:
                    # Overdub phase is live; avoid an extra fixed delay before first note.
                    time.sleep(0.0)
                else:
                    time.sleep(min(args.phase_wait_ms, 120) / 1000.0)

                od_clock_count = 0
                od_fallback_seconds = False
                od_timing = {
                    "grid_steps_emitted": 0.0,
                    "max_abs_grid_jitter_clocks": 0.0,
                    "mean_abs_grid_jitter_clocks": 0.0,
                    "stop_press_sent_during_stream": 0.0,
                }
                if args.overdub_bars and args.bar_sync_from_midi_clock:
                    guard = max(10.0, args.overdub_bars * seconds_per_bar * 3.0)
                    overdub_stop_advance_clocks = args.stop_press_advance_clocks
                    if overdub_stop_advance_clocks <= 0:
                        # Conservative default: keep full note count (16 for 2 bars at 8ths)
                        # and avoid early state cutover that can drop the final overdub note.
                        overdub_stop_advance_clocks = 0
                    od_notes, od_cc, od_clock_count, od_timing = _stream_pattern_for_bars(
                        out_port,
                        in_port,
                        midi_channel_1based=args.midi_channel,
                        low_note=args.overdub_low_note,
                        high_note=args.overdub_high_note,
                        step_clocks=12,  # 8th notes at 24 PPQN clock
                        target_bars=args.overdub_bars,
                        cc_number=args.cc_number,
                        cc_step=args.cc_step,
                        pitch_cycle_bars=args.pitch_cycle_bars,
                        phase_start_delay_bars=args.overdub_start_delay_bars,
                        phase_start_delay_beats=args.overdub_start_delay_beats,
                        max_seconds_guard=guard,
                        fixed_note=args.overdub_fixed_note if args.fixed_grid_notes else None,
                        stop_press_advance_clocks=overdub_stop_advance_clocks,
                        stop_press_note=RECORD_BUTTON_NOTE,
                        stop_press_channel_1based=CONTROL_CHANNEL_1BASED,
                        stop_press_press_ms=args.press_ms,
                        run_deadline=run_deadline,
                        emit_immediate_first_step=True,
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
                        run_deadline=run_deadline,
                    )

                if time.monotonic() >= run_deadline:
                    timed_out = True
                    break

                time.sleep(max(args.post_stream_settle_ms, 0) / 1000.0)
                print(f"[track {idx}] overdub stop")
                if od_timing.get("stop_press_sent_during_stream", 0.0) <= 0.0:
                    _send_short_press(
                        out_port,
                        note=RECORD_BUTTON_NOTE,
                        channel_1based=CONTROL_CHANNEL_1BASED,
                        press_ms=args.press_ms,
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
                    }
                )

            if timed_out:
                print("Run timeout reached before finishing all phases/tracks.")
            if precondition_failures:
                print("Run aborted due to failed preconditions.")

            # Allow delayed short-press expiration + state transition logs to flush.
            time.sleep(max(args.final_wait_ms, 1) / 1000.0)

    finally:
        serial_lines: list[str] = []
        serial_error = ""
        if serial_collector is not None:
            serial_lines = serial_collector.snapshot()
            serial_error = serial_collector.error()
            serial_collector.stop()

    verification_lines = serial_lines
    if not verification_lines and args.verify_serial_log is not None and args.verify_serial_log.exists():
        verification_lines = args.verify_serial_log.read_text(encoding="utf-8", errors="replace").splitlines()

    transition_counts = _count_capture_transitions(verification_lines)
    reca_count, recs_count = _count_capture_record_markers(verification_lines)
    cap_lines_count = sum(1 for l in verification_lines if "#CAP," in l)

    expected_min = args.track_count
    expected_record_notes_min = 0
    expected_overdub_notes_min = 0
    if args.record_bars and args.bar_sync_from_midi_clock:
        # 16th-note grid; allow one-step edge variance at boundaries.
        expected_record_notes_min = max(1, args.record_bars * 16 - 1)
    if args.overdub_bars and args.bar_sync_from_midi_clock:
        # 8th-note grid; allow one-step edge variance at boundaries.
        expected_overdub_notes_min = max(1, args.overdub_bars * 8 - 1)

    phase_note_failures: list[dict[str, int]] = []
    phase_activation_failures: list[dict[str, int | str]] = []
    for row in per_track_stats:
        idx = int(row["track_index"])
        rec_notes = int(row["record_notes_sent"])
        od_notes = int(row["overdub_notes_sent"])
        rec_clocks = int(row["record_clock_pulses_seen"])
        od_clocks = int(row["overdub_clock_pulses_seen"])

        if expected_record_notes_min > 0 and rec_clocks <= 0:
            phase_activation_failures.append(
                {
                    "track_index": idx,
                    "phase": "record",
                    "actual_clocks": rec_clocks,
                    "reason": "phase_not_activated_or_no_clock",
                }
            )
        elif expected_record_notes_min > 0 and rec_notes < expected_record_notes_min:
            phase_note_failures.append(
                {
                    "track_index": idx,
                    "phase": "record",
                    "actual_notes": rec_notes,
                    "expected_notes_min": expected_record_notes_min,
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
        elif expected_overdub_notes_min > 0 and od_notes < expected_overdub_notes_min:
            phase_note_failures.append(
                {
                    "track_index": idx,
                    "phase": "overdub",
                    "actual_notes": od_notes,
                    "expected_notes_min": expected_overdub_notes_min,
                }
            )

    transition_checks = []
    for expectation in EXPECTED_TRANSITIONS:
        key = (expectation.from_state, expectation.to_state)
        actual = transition_counts.get(key, 0)
        # After clear, first record can start from EMPTY instead of ARMED.
        if expectation.from_state == "ARMED" and expectation.to_state == "RECORDING":
            actual += transition_counts.get(("EMPTY", "RECORDING"), 0)
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

    assertions = {
        "serial_capture_enabled": bool(args.serial_port),
        "serial_error": serial_error if args.serial_port else "",
        "serial_line_count": len(serial_lines),
        "verification_line_count": len(verification_lines),
        "reca_count": reca_count,
        "recs_count": recs_count,
        "expected_reca_min": expected_min if verification_lines else None,
        "expected_recs_min": expected_min if verification_lines else None,
        "transition_checks": transition_checks,
        "midi_in_message_count": midi_in_messages,
        "timed_out": timed_out,
        "precondition_failures": precondition_failures,
        "phase_note_failures": phase_note_failures,
        "phase_activation_failures": phase_activation_failures,
        "serial_verification": serial_verification,
    }

    overall_ok = True
    if timed_out:
        overall_ok = False
    if precondition_failures:
        overall_ok = False
    if args.serial_port:
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
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")

    print("\nRun summary:")
    print(f"  Tracks run: {args.track_count} (from {args.first_track_index})")
    print(f"  MIDI in messages observed: {midi_in_messages}")
    if verification_lines:
        print(f"  #CAP lines observed: {cap_lines_count}")
        print(f"  RECA count: {reca_count}, RECS count: {recs_count}")
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
            print(
                "  VERIFY first-note clocks (record/overdub): "
                f"{rf['offset_clocks']}/{of['offset_clocks']} "
                f"(max {args.record_first_note_max_clocks}/{args.overdub_first_note_max_clocks})"
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
        if args.serial_port:
            print("  Serial assertions requested but no serial lines were captured.")
        else:
            print("  Serial assertions skipped (no --serial-port).")
    if args.serial_port and serial_error:
        print(f"  Serial error: {serial_error}")
    print(f"  Report: {report_path}")

    if not overall_ok:
        print("  Result: FAIL (serial/assertion checks failed)")
        return 2
    print("  Result: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(run())
