"""Capture ST,Track transition / state-entry helpers for HITL serial verification."""

from __future__ import annotations

import re
import time
from typing import Optional

from hitl.serial.protocol import (
    count_track_state_entries as _count_track_state_entries,
    count_track_transitions as _count_track_transitions,
    parse_cap_micros as _parse_cap_micros,
    parse_disp_track_state as _parse_disp_track_state,
)
from hitl.serial_collector import RunAbort, SerialCaptureCollector

_HUMAN_LOG_TS_RE = re.compile(r"^\[(\d+\.\d+)\]")


def _cap_prefixed_line(line: str) -> str:
    cap_index = line.find("#CAP,")
    if cap_index >= 0:
        return line[cap_index:]
    return line


def _count_capture_transitions(lines: list[str]) -> dict[tuple[str, str], int]:
    return _count_track_transitions([_cap_prefixed_line(line) for line in lines])


def _count_capture_state_entries(lines: list[str]) -> dict[str, int]:
    return _count_track_state_entries([_cap_prefixed_line(line) for line in lines])


def _human_log_ts_micros(line: str) -> Optional[int]:
    """Parse firmware wall-clock prefix like ``[1836.133]`` into CAP microsecond scale."""
    match = _HUMAN_LOG_TS_RE.match(line.lstrip())
    if match is None:
        return None
    try:
        return int(round(float(match.group(1)) * 1_000_000))
    except ValueError:
        return None


def _phase_ts_from_marker_line(lines: list[str], index: int) -> Optional[int]:
    ts = _human_log_ts_micros(lines[index])
    if ts is not None:
        return ts
    return _cap_ts_near_line(lines, index, search_back=0)


def _extract_recs_lengths_from_human_logs(lines: list[str]) -> list[dict[str, int]]:
    rows: list[dict[str, int]] = []
    for index, line in enumerate(lines):
        if "Recording stopped" not in line:
            continue
        ts = _phase_ts_from_marker_line(lines, index) or 0
        final_length: Optional[int] = None
        raw_length: Optional[int] = None
        length_match = re.search(r"length=(\d+)", line)
        if length_match is not None:
            final_length = int(length_match.group(1))
        window_start = max(0, index - 4)
        window_end = min(len(lines), index + 4)
        for nearby in lines[window_start:window_end]:
            rewind_match = re.search(r"raw=(\d+) final=(\d+)", nearby)
            if rewind_match is not None:
                raw_length = int(rewind_match.group(1))
                final_length = int(rewind_match.group(2))
                break
        if final_length is None:
            continue
        rows.append(
            {
                "timestamp": ts,
                "raw_length": raw_length if raw_length is not None else final_length,
                "final_length": final_length,
            }
        )
    return rows


def _serial_capture_sparse_for_verification(lines: list[str], *, mi_u_min: int = 8) -> bool:
    reca = sum(1 for line in lines if "#CAP," in line and ",RECA," in line)
    recs = sum(1 for line in lines if "#CAP," in line and ",RECS," in line)
    mi_u = sum(1 for line in lines if ",MI,U," in line)
    return reca == 0 and recs == 0 and mi_u < mi_u_min


CAP_ONLY_SERIAL_ISSUES = frozenset(
    {
        "missing_phase_boundaries",
        "record_first_note_missing",
        "overdub_first_note_missing",
        "record_recs_missing",
        "record_stored_revt_missing",
        "second_overdub_missing_phase_boundaries",
        "second_overdub_first_note_missing",
        "record_note_span_too_short",
        "record_loop_length_mismatch",
        "record_stored_note_grid_bad",
        "second_overdub_stored_sevt_missing",
        "second_overdub_stored_count_short",
        "second_overdub_stored_span_too_short",
        "second_overdub_stored_bars_short",
        "overdub_stored_sevt_missing",
        "overdub_stored_count_short",
        "overdub_stored_span_too_short",
        "overdub_stored_bars_short",
    }
)


def _is_cap_only_serial_issue(issue: str) -> bool:
    if issue.startswith(
        (
            "record_stop_stage_missing:",
            "persistence_stage_missing:",
            "display_",
        )
    ):
        return True
    return issue in CAP_ONLY_SERIAL_ISSUES


def _cap_ts_near_line(
    lines: list[str],
    index: int,
    *,
    search_ahead: int = 40,
    search_back: int = 8,
) -> Optional[int]:
    end = min(len(lines), index + search_ahead)
    for i in range(index, end):
        ts = _parse_cap_micros(lines[i])
        if ts is not None:
            return ts
    start = max(0, index - search_back)
    for i in range(index - 1, start - 1, -1):
        ts = _parse_cap_micros(lines[i])
        if ts is not None:
            return ts
    return None


def _count_human_log_transition_events(lines: list[str]) -> dict[str, int]:
    """Count phase markers from human-readable TRACK logs when CAP ST lines are dropped."""
    recording_started = 0
    recording_stopped = 0
    playback_started = 0
    overdubbing_started = 0
    overdubbing_stopped = 0
    for line in lines:
        if "Recording started" in line:
            recording_started += 1
        if "Recording stopped" in line:
            recording_stopped += 1
        if "Playback started" in line:
            playback_started += 1
        if "Overdubbing started" in line:
            overdubbing_started += 1
        if "Overdubbing stopped" in line:
            overdubbing_stopped += 1
    playing_after_record = (
        min(recording_stopped, playback_started) if recording_stopped else playback_started
    )
    return {
        "record_entry": recording_started,
        "recording_stopped": recording_stopped,
        "playing_after_record": playing_after_record,
        "overdubbing_started": overdubbing_started,
        "overdubbing_stopped": overdubbing_stopped,
    }


def _merge_transition_counts_with_evidence(
    lines: list[str],
    *,
    st_counts: Optional[dict[tuple[str, str], int]] = None,
) -> dict[tuple[str, str], int]:
    """Merge #CAP,ST,Track counts with human-log phase markers (max per transition)."""
    if st_counts is None:
        st_counts = _count_capture_transitions(lines)
    merged = dict(st_counts)
    events = _count_human_log_transition_events(lines)

    record_entry = max(_record_entry_to_recording_count(merged), events["record_entry"])
    if record_entry > 0:
        merged[("ARMED", "RECORDING")] = max(merged.get(("ARMED", "RECORDING"), 0), record_entry)

    merged[("RECORDING", "STOPPED_RECORDING")] = max(
        merged.get(("RECORDING", "STOPPED_RECORDING"), 0),
        events["recording_stopped"],
    )
    merged[("STOPPED_RECORDING", "PLAYING")] = max(
        merged.get(("STOPPED_RECORDING", "PLAYING"), 0),
        events["playing_after_record"],
    )
    merged[("PLAYING", "OVERDUBBING")] = max(
        merged.get(("PLAYING", "OVERDUBBING"), 0),
        events["overdubbing_started"],
    )
    merged[("OVERDUBBING", "PLAYING")] = max(
        merged.get(("OVERDUBBING", "PLAYING"), 0),
        events["overdubbing_stopped"],
    )
    return merged


def _effective_transition_actual(
    from_state: str,
    to_state: str,
    transition_counts: dict[tuple[str, str], int],
) -> int:
    if from_state == "ARMED" and to_state == "RECORDING":
        return _record_entry_to_recording_count(transition_counts)
    return transition_counts.get((from_state, to_state), 0)


def _record_entry_to_recording_count(
    transition_counts: dict[tuple[str, str], int],
) -> int:
    """Count record arm from ARMED, EMPTY, or STOPPED (transport restart before record)."""
    total = 0
    for from_state in ("ARMED", "EMPTY", "STOPPED"):
        total += transition_counts.get((from_state, "RECORDING"), 0)
    return total


def _latest_track_state(lines: list[str], *, after_index: int = 0) -> Optional[str]:
    latest: Optional[str] = None
    latest_ts = -1
    for line in lines[after_index:]:
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


def _serial_has_armed_after(lines: list[str], *, after_index: int = 0) -> bool:
    suffix = lines[after_index:]
    if any("armed, waiting for clock to start recording" in line for line in suffix):
        return True
    if any(",ST,Track," in line and ",ARMED" in line for line in suffix):
        return True
    return _latest_track_state(lines, after_index=after_index) == "ARMED"


def _serial_has_recording_active(lines: list[str], *, after_index: int = 0) -> bool:
    suffix = lines[after_index:]
    if any("Recording started" in line for line in suffix):
        return True
    if any(",RECA," in line and "#CAP," in line for line in suffix):
        return True
    if any(",DISP," in line and ",RECORDING," in line for line in suffix):
        return True
    return _latest_track_state(lines, after_index=after_index) == "RECORDING"


def _serial_has_playing_after_record_stop(lines: list[str], *, after_index: int = 0) -> bool:
    suffix = lines[after_index:]
    if not any("Recording stopped" in line for line in suffix):
        return False
    if any("Playback started" in line for line in suffix):
        return True
    return _latest_track_state(lines, after_index=after_index) == "PLAYING"


def _serial_has_overdubbing_after(lines: list[str], *, after_index: int = 0) -> bool:
    suffix = lines[after_index:]
    if any("Overdubbing started" in line for line in suffix):
        return True
    if any("Overdub session opened" in line for line in suffix):
        return True
    if any("MIDI Button A: Live Overdub" in line for line in suffix):
        return True
    if any(",DISP," in line and ",OVERDUBBING," in line for line in suffix):
        return True
    return _latest_track_state(lines, after_index=after_index) == "OVERDUBBING"


def _serial_has_overdubbing_stopped(lines: list[str], *, after_index: int = 0) -> bool:
    suffix = lines[after_index:]
    if any("Overdubbing stopped" in line for line in suffix):
        return True
    if any("MIDI Button A: Stop Overdub" in line for line in suffix):
        return True
    if any(",ST,Track,OVERDUBBING,PLAYING" in line for line in suffix):
        return True
    return False


def _wait_for_armed_state(
    collector: SerialCaptureCollector,
    *,
    baseline_len: int,
    timeout_s: float,
    abort: Optional[RunAbort] = None,
) -> bool:
    deadline = time.monotonic() + max(timeout_s, 0.0)
    while time.monotonic() < deadline:
        if abort is not None and abort.check() is not None:
            return False
        if _serial_has_armed_after(collector.snapshot(), after_index=baseline_len):
            return True
        time.sleep(0.01)
    return False


def _wait_for_recording_active(
    collector: SerialCaptureCollector,
    *,
    baseline_len: int,
    timeout_s: float,
    abort: Optional[RunAbort] = None,
) -> bool:
    deadline = time.monotonic() + max(timeout_s, 0.0)
    while time.monotonic() < deadline:
        if abort is not None and abort.check() is not None:
            return False
        if _serial_has_recording_active(collector.snapshot(), after_index=baseline_len):
            return True
        time.sleep(0.01)
    return False


def _wait_for_overdubbing_active(
    collector: SerialCaptureCollector,
    *,
    baseline_len: int,
    timeout_s: float,
    abort: Optional[RunAbort] = None,
) -> bool:
    deadline = time.monotonic() + max(timeout_s, 0.0)
    while time.monotonic() < deadline:
        if abort is not None and abort.check() is not None:
            return False
        if _serial_has_overdubbing_after(collector.snapshot(), after_index=baseline_len):
            return True
        time.sleep(0.01)
    return False


def _wait_for_playing_after_record_stop(
    collector: SerialCaptureCollector,
    *,
    baseline_len: int,
    timeout_s: float,
    abort: Optional[RunAbort] = None,
) -> bool:
    deadline = time.monotonic() + max(timeout_s, 0.0)
    while time.monotonic() < deadline:
        if abort is not None and abort.check() is not None:
            return False
        if _serial_has_playing_after_record_stop(
            collector.snapshot(), after_index=baseline_len
        ):
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
        lines = collector.snapshot()
        counts = _count_capture_transitions(lines)
        if counts.get(key, 0) >= target_count:
            return True
        if from_state == "STOPPED_RECORDING" and to_state == "PLAYING":
            if _serial_has_playing_after_record_stop(lines):
                return True
        if from_state == "PLAYING" and to_state == "OVERDUBBING":
            if _serial_has_overdubbing_after(lines):
                return True
        if from_state == "OVERDUBBING" and to_state == "PLAYING":
            if _serial_has_overdubbing_stopped(lines):
                return True
        time.sleep(0.01)
    return False
