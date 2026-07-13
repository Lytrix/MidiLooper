"""Serial log timing helpers for HITL verification."""

from __future__ import annotations

from typing import Optional

from hitl.serial_transport import parse_latest_bpm

MIDI_CLOCKS_PER_BAR = 96
MIDI_CLOCKS_PER_BEAT = 24


def phase_start_delay_clocks(*, delay_bars: int = 0, delay_beats: int = 0) -> int:
    """Host grid delay after ST entry before first note (matches stream_pattern_for_bars)."""
    return max(delay_bars, 0) * MIDI_CLOCKS_PER_BAR + max(delay_beats, 0) * MIDI_CLOCKS_PER_BEAT


def extract_phase_boundaries(lines: list[str]) -> dict[str, Optional[int]]:
    boundaries: dict[str, Optional[int]] = {
        "record_start_ts": None,
        "record_stop_ts": None,
        "overdub_start_ts": None,
        "overdub_stop_ts": None,
        "second_overdub_start_ts": None,
        "second_overdub_stop_ts": None,
    }
    overdub_sessions: list[tuple[int, int]] = []
    open_overdub_start: Optional[int] = None
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
        elif from_state == "PLAYING" and to_state == "OVERDUBBING":
            open_overdub_start = ts
        elif (
            from_state == "OVERDUBBING"
            and to_state in ("PLAYING", "STOPPED")
            and open_overdub_start is not None
        ):
            overdub_sessions.append((open_overdub_start, ts))
            open_overdub_start = None
    if overdub_sessions:
        boundaries["overdub_start_ts"] = overdub_sessions[0][0]
        boundaries["overdub_stop_ts"] = overdub_sessions[0][1]
    if len(overdub_sessions) > 1:
        boundaries["second_overdub_start_ts"] = overdub_sessions[1][0]
        boundaries["second_overdub_stop_ts"] = overdub_sessions[1][1]
    return boundaries


def _bpm_for_phase_window(
    lines: list[str],
    *,
    phase_start_ts: int,
    phase_stop_ts: int,
    fallback_bpm: float,
) -> float:
    window: list[str] = []
    for line in lines:
        if "#CAP," not in line or ",BPM," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 2:
            continue
        try:
            ts = int(parts[1])
        except ValueError:
            continue
        if phase_start_ts <= ts < phase_stop_ts:
            window.append(line)
    bpm = parse_latest_bpm(window) if window else parse_latest_bpm(lines)
    if bpm is not None:
        return bpm
    return fallback_bpm


def extract_first_note_offset(
    lines: list[str],
    *,
    midi_channel_1based: int,
    phase_start_ts: Optional[int],
    phase_stop_ts: Optional[int],
    fallback_bpm: float = 120.0,
    phase_start_delay_clocks: int = 0,
) -> dict[str, object]:
    delay_clocks = max(phase_start_delay_clocks, 0)
    if phase_start_ts is None or phase_stop_ts is None or phase_stop_ts <= phase_start_ts:
        return {
            "offset_us": None,
            "offset_ms": None,
            "offset_clocks": None,
            "phase_start_delay_clocks": delay_clocks,
            "capture_latency_clocks": None,
            "phase_missing": True,
        }

    first_note_ts: Optional[int] = None
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
        if msg_type == 144 and channel == midi_channel_1based:
            first_note_ts = ts
            break

    if first_note_ts is None:
        return {
            "offset_us": None,
            "offset_ms": None,
            "offset_clocks": None,
            "phase_start_delay_clocks": delay_clocks,
            "capture_latency_clocks": None,
            "phase_missing": False,
        }

    offset_us = first_note_ts - phase_start_ts
    bpm = _bpm_for_phase_window(
        lines,
        phase_start_ts=phase_start_ts,
        phase_stop_ts=phase_stop_ts,
        fallback_bpm=fallback_bpm,
    )
    us_per_clock = 60_000_000.0 / (bpm * 24.0)
    offset_clocks = int(round(offset_us / us_per_clock)) if us_per_clock > 0 else 0
    capture_latency_clocks = max(0, offset_clocks - delay_clocks)
    return {
        "offset_us": offset_us,
        "offset_ms": offset_us / 1000.0,
        "offset_clocks": offset_clocks,
        "phase_start_delay_clocks": delay_clocks,
        "capture_latency_clocks": capture_latency_clocks,
        "phase_missing": False,
    }
