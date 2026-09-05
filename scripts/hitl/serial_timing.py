"""Serial log timing helpers for HITL verification."""

from __future__ import annotations

from typing import Optional

from hitl.capture_transitions import _phase_ts_from_marker_line
from hitl.control_constants import MIDI_CLOCKS_PER_BAR, MIDI_CLOCKS_PER_BEAT
from hitl.serial_transport import parse_latest_bpm


def phase_start_delay_clocks(*, delay_bars: int = 0, delay_beats: int = 0) -> int:
    """Host grid delay after ST entry before first note (matches stream_pattern_for_bars)."""
    return max(delay_bars, 0) * MIDI_CLOCKS_PER_BAR + max(delay_beats, 0) * MIDI_CLOCKS_PER_BEAT


_PHASE_SESSION_SLOP_US = 100_000


def _cap_ts(line: str) -> Optional[int]:
    if "#CAP," not in line:
        return None
    parts = line.split(",")
    if len(parts) < 2:
        return None
    try:
        return int(parts[1])
    except ValueError:
        return None


def _session_covers_start(start_ts: int, sessions: list[tuple[int, int]]) -> bool:
    for session_start, session_stop in sessions:
        if abs(start_ts - session_start) <= _PHASE_SESSION_SLOP_US:
            return True
        if session_start <= start_ts <= session_stop:
            return True
    return False


def _next_unused_stop(start_ts: int, stops: list[int], used: set[int]) -> Optional[int]:
    for stop_ts in stops:
        if stop_ts > start_ts and stop_ts not in used:
            return stop_ts
    return None


def extract_phase_boundaries(lines: list[str]) -> dict[str, Optional[int]]:
    boundaries: dict[str, Optional[int]] = {
        "record_start_ts": None,
        "record_stop_ts": None,
        "overdub_start_ts": None,
        "overdub_stop_ts": None,
        "second_overdub_start_ts": None,
        "second_overdub_stop_ts": None,
    }
    record_sessions: list[tuple[int, int]] = []
    open_record_start: Optional[int] = None
    st_overdub_starts: list[int] = []
    st_overdub_stops: list[int] = []
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
        if to_state == "RECORDING":
            open_record_start = ts
        elif (
            from_state == "RECORDING"
            and to_state == "STOPPED_RECORDING"
            and open_record_start is not None
        ):
            record_sessions.append((open_record_start, ts))
            open_record_start = None
        elif from_state == "PLAYING" and to_state == "OVERDUBBING":
            st_overdub_starts.append(ts)
        elif from_state == "OVERDUBBING" and to_state in ("PLAYING", "STOPPED"):
            st_overdub_stops.append(ts)

    human_record_starts: list[int] = []
    human_record_stops: list[int] = []
    human_overdub_starts: list[int] = []
    human_overdub_stops: list[int] = []
    odub_enter_stops: list[int] = []
    for index, line in enumerate(lines):
        if "Recording started" in line:
            ts = _phase_ts_from_marker_line(lines, index)
            if ts is not None:
                human_record_starts.append(ts)
        if "Recording stopped" in line:
            ts = _phase_ts_from_marker_line(lines, index)
            if ts is not None:
                human_record_stops.append(ts)
        if "Overdubbing started" in line:
            ts = _phase_ts_from_marker_line(lines, index)
            if ts is not None:
                human_overdub_starts.append(ts)
        if "Overdubbing stopped" in line:
            ts = _phase_ts_from_marker_line(lines, index)
            if ts is not None:
                human_overdub_stops.append(ts)
        if "#CAP," in line and ",ODUB,stop,enter" in line:
            ts = _cap_ts(line)
            if ts is not None:
                odub_enter_stops.append(ts)

    if not record_sessions and human_record_starts:
        remaining_stops = list(human_record_stops)
        for start_ts in human_record_starts:
            stop_ts = None
            for candidate in remaining_stops:
                if candidate > start_ts:
                    stop_ts = candidate
                    break
            if stop_ts is not None:
                record_sessions.append((start_ts, stop_ts))
                remaining_stops.remove(stop_ts)

    if record_sessions:
        boundaries["record_start_ts"] = record_sessions[-1][0]
        boundaries["record_stop_ts"] = record_sessions[-1][1]

    overdub_stops = sorted(set(st_overdub_stops + human_overdub_stops + odub_enter_stops))
    overdub_sessions: list[tuple[int, int]] = []
    used_overdub_stops: set[int] = set()
    for start_ts in st_overdub_starts:
        stop_ts = _next_unused_stop(start_ts, overdub_stops, used_overdub_stops)
        if stop_ts is None:
            continue
        overdub_sessions.append((start_ts, stop_ts))
        used_overdub_stops.add(stop_ts)
    for start_ts in human_overdub_starts:
        if _session_covers_start(start_ts, overdub_sessions):
            continue
        stop_ts = _next_unused_stop(start_ts, overdub_stops, used_overdub_stops)
        if stop_ts is None:
            continue
        overdub_sessions.append((start_ts, stop_ts))
        used_overdub_stops.add(stop_ts)
    overdub_sessions.sort(key=lambda session: session[0])

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
