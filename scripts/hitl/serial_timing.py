"""Serial log timing helpers for HITL verification."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional

from hitl.capture_transitions import _phase_ts_from_marker_line
from hitl.contract import PhaseAmbiguity
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


def phase_evidence_start_index(lines: list[str], after_index: Optional[int] = None) -> int:
    """First line that may authorize phase sessions for this run."""
    if after_index is not None:
        return max(after_index, 0)
    for index, line in enumerate(lines):
        if "#HITL," in line:
            return index
    return 0


@dataclass(frozen=True)
class MatchedPhaseSession:
    name: str
    start_ts: Optional[int]
    stop_ts: Optional[int]
    ambiguity: Optional[PhaseAmbiguity] = None
    diagnostic: str = ""


@dataclass(frozen=True)
class PhaseSessionReport:
    after_index: int
    record: Optional[MatchedPhaseSession] = None
    overdub: Optional[MatchedPhaseSession] = None
    second_overdub: Optional[MatchedPhaseSession] = None
    ambiguities: tuple[PhaseAmbiguity, ...] = ()
    diagnostics: tuple[str, ...] = ()

    def boundaries(self) -> dict[str, Optional[int]]:
        return {
            "record_start_ts": None if self.record is None else self.record.start_ts,
            "record_stop_ts": None if self.record is None else self.record.stop_ts,
            "overdub_start_ts": None if self.overdub is None else self.overdub.start_ts,
            "overdub_stop_ts": None if self.overdub is None else self.overdub.stop_ts,
            "second_overdub_start_ts": None
            if self.second_overdub is None
            else self.second_overdub.start_ts,
            "second_overdub_stop_ts": None
            if self.second_overdub is None
            else self.second_overdub.stop_ts,
        }


def _pair_record_sessions(
    lines: list[str], *, start_index: int
) -> tuple[list[tuple[int, int]], Optional[int]]:
    record_sessions: list[tuple[int, int]] = []
    open_record_start: Optional[int] = None
    for line in lines[start_index:]:
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

    if record_sessions:
        return record_sessions, open_record_start

    human_record_starts: list[int] = []
    human_record_stops: list[int] = []
    for index, line in enumerate(lines):
        if index < start_index:
            continue
        if "Recording started" in line:
            ts = _phase_ts_from_marker_line(lines, index)
            if ts is not None:
                human_record_starts.append(ts)
        if "Recording stopped" in line:
            ts = _phase_ts_from_marker_line(lines, index)
            if ts is not None:
                human_record_stops.append(ts)
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
        elif open_record_start is None:
            open_record_start = start_ts
    return record_sessions, open_record_start


def _pair_overdub_sessions(
    lines: list[str], *, start_index: int
) -> tuple[list[tuple[int, int]], list[int], list[int]]:
    st_overdub_starts: list[int] = []
    st_overdub_stops: list[int] = []
    for line in lines[start_index:]:
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
        if from_state == "PLAYING" and to_state == "OVERDUBBING":
            st_overdub_starts.append(ts)
        elif from_state == "OVERDUBBING" and to_state in (
            "PLAYING",
            "STOPPED",
            # Deferred stop: the interval's last admissible input is when it leaves
            # OVERDUBBING for OVERDUB_STOPPING; the audible completion is the later
            # OVERDUB_STOPPING -> PLAYING hop. Both are legitimate ends of the phase
            # window, and the earliest matching stop is the precise one.
            "OVERDUB_STOPPING",
        ):
            st_overdub_stops.append(ts)
        elif from_state == "OVERDUB_STOPPING" and to_state in ("PLAYING", "STOPPED"):
            st_overdub_stops.append(ts)

    human_overdub_starts: list[int] = []
    human_overdub_stops: list[int] = []
    odub_enter_stops: list[int] = []
    for index, line in enumerate(lines):
        if index < start_index:
            continue
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

    unmatched_starts = [
        start_ts
        for start_ts in st_overdub_starts + human_overdub_starts
        if not _session_covers_start(start_ts, overdub_sessions)
    ]
    leftover_stops = [stop_ts for stop_ts in overdub_stops if stop_ts not in used_overdub_stops]
    return overdub_sessions, unmatched_starts, leftover_stops


def extract_phase_sessions(
    lines: list[str], *, after_index: Optional[int] = None
) -> PhaseSessionReport:
    start_index = phase_evidence_start_index(lines, after_index)
    record_sessions, open_record_start = _pair_record_sessions(lines, start_index=start_index)
    overdub_sessions, unmatched_od_starts, leftover_od_stops = _pair_overdub_sessions(
        lines, start_index=start_index
    )

    ambiguities: list[PhaseAmbiguity] = []
    diagnostics: list[str] = []
    record: Optional[MatchedPhaseSession] = None
    if record_sessions:
        start_ts, stop_ts = record_sessions[-1]
        record_ambiguity = None
        if stop_ts <= start_ts:
            record_ambiguity = PhaseAmbiguity.OUT_OF_ORDER
            ambiguities.append(record_ambiguity)
        record = MatchedPhaseSession("record", start_ts, stop_ts, record_ambiguity)
    elif open_record_start is not None:
        ambiguities.append(PhaseAmbiguity.MISSING_STOP)
        diagnostics.append("record start has no matching stop")
        record = MatchedPhaseSession(
            "record",
            open_record_start,
            None,
            PhaseAmbiguity.MISSING_STOP,
            "record start has no matching stop",
        )

    if unmatched_od_starts:
        ambiguities.append(PhaseAmbiguity.MISSING_STOP)
        diagnostics.append(f"overdub start(s) without stop: {unmatched_od_starts}")
    first_start: Optional[int] = None
    if overdub_sessions:
        first_start = overdub_sessions[0][0]
    elif unmatched_od_starts:
        first_start = min(unmatched_od_starts)
    for stop_ts in leftover_od_stops:
        if first_start is not None and stop_ts < first_start:
            ambiguities.append(PhaseAmbiguity.STALE_STOP)
            diagnostics.append(f"overdub stop {stop_ts} before first start")
        elif first_start is None:
            ambiguities.append(PhaseAmbiguity.MISSING_START)
            diagnostics.append(f"overdub stop {stop_ts} has no matching start")

    def _overdub_session(name: str, pair: tuple[int, int]) -> MatchedPhaseSession:
        start_ts, stop_ts = pair
        ambiguity = PhaseAmbiguity.OUT_OF_ORDER if stop_ts <= start_ts else None
        if ambiguity is not None:
            ambiguities.append(ambiguity)
        return MatchedPhaseSession(name, start_ts, stop_ts, ambiguity)

    overdub = _overdub_session("overdub", overdub_sessions[0]) if overdub_sessions else None
    second = (
        _overdub_session("second_overdub", overdub_sessions[1])
        if len(overdub_sessions) > 1
        else None
    )
    if unmatched_od_starts and overdub is None:
        overdub = MatchedPhaseSession(
            "overdub",
            unmatched_od_starts[0],
            None,
            PhaseAmbiguity.MISSING_STOP,
            "overdub start has no matching stop",
        )

    unique_ambiguities = tuple(dict.fromkeys(ambiguities))
    return PhaseSessionReport(
        after_index=start_index,
        record=record,
        overdub=overdub,
        second_overdub=second,
        ambiguities=unique_ambiguities,
        diagnostics=tuple(diagnostics),
    )


def extract_phase_boundaries(
    lines: list[str], *, after_index: Optional[int] = None
) -> dict[str, Optional[int]]:
    return extract_phase_sessions(lines, after_index=after_index).boundaries()


def _bpm_for_phase_window(
    lines: list[str],
    *,
    phase_start_ts: int,
    phase_stop_ts: int,
    fallback_bpm: float,
) -> tuple[float, str]:
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
    bpm = parse_latest_bpm(window) if window else None
    if bpm is not None:
        return bpm, "phase_window"
    return fallback_bpm, "fallback_cli"


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
    missing = {
        "offset_us": None,
        "offset_ms": None,
        "offset_clocks": None,
        "phase_start_delay_clocks": delay_clocks,
        "capture_latency_clocks": None,
        "bpm_source": None,
        "first_note_ts": None,
    }
    if phase_start_ts is None or phase_stop_ts is None or phase_stop_ts <= phase_start_ts:
        return {**missing, "phase_missing": True}

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
            velocity = int(parts[7])
        except ValueError:
            continue
        if ts < phase_start_ts or ts >= phase_stop_ts:
            continue
        if msg_type == 144 and channel == midi_channel_1based and velocity > 0:
            first_note_ts = ts
            break

    if first_note_ts is None:
        return {**missing, "phase_missing": False}

    offset_us = first_note_ts - phase_start_ts
    bpm, bpm_source = _bpm_for_phase_window(
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
        "bpm_source": bpm_source,
        "first_note_ts": first_note_ts,
    }
