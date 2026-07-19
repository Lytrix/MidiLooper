"""Capture ST,Track transition / state-entry helpers for HITL serial verification."""

from __future__ import annotations

import time
from typing import Optional

from hitl.serial_collector import RunAbort, SerialCaptureCollector


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
