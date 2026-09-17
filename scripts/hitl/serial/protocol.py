"""#CAP protocol parsing — single owner for serial line decode."""

from __future__ import annotations

from typing import Optional


def parse_cap_micros(line: str) -> Optional[int]:
    if not line.startswith("#CAP,"):
        return None
    parts = line.split(",", 2)
    if len(parts) < 2:
        return None
    try:
        return int(parts[1])
    except ValueError:
        return None


def parse_track_transition(line: str) -> Optional[tuple[str, str]]:
    marker = ",ST,Track,"
    if marker not in line or not line.startswith("#CAP,"):
        return None
    tail = line.split(marker, 1)[1]
    parts = tail.split(",")
    if len(parts) < 2:
        return None
    return parts[0].strip(), parts[1].strip()


# The firmware reports one track state change from two observers (the Track state facade and
# the logger hook), so the same (from, to) edge appears twice within microseconds. Two
# observers of one state change must not read as two transitions: counting both doubled every
# transition row and let the phase pairing bind a successor interval's start to the
# predecessor's stop. Edges that repeat inside this window collapse into one.
TRACK_TRANSITION_DUPLICATE_WINDOW_US = 5_000


def track_transitions(lines: list[str]) -> list[tuple[int, tuple[str, str]]]:
    """Distinct track transitions as ``(timestamp_us, (from, to))``, duplicates collapsed."""
    transitions: list[tuple[int, tuple[str, str]]] = []
    for line in lines:
        transition = parse_track_transition(line)
        if transition is None:
            continue
        ts = parse_cap_micros(line)
        if ts is None:
            continue
        if transitions:
            previous_ts, previous_transition = transitions[-1]
            if (
                transition == previous_transition
                and ts - previous_ts <= TRACK_TRANSITION_DUPLICATE_WINDOW_US
            ):
                continue
        transitions.append((ts, transition))
    return transitions


def count_track_transitions(lines: list[str]) -> dict[tuple[str, str], int]:
    counts: dict[tuple[str, str], int] = {}
    for _ts, transition in track_transitions(lines):
        counts[transition] = counts.get(transition, 0) + 1
    return counts


def count_track_state_entries(lines: list[str]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for _ts, (_from_state, to_state) in track_transitions(lines):
        counts[to_state] = counts.get(to_state, 0) + 1
    return counts


def parse_disp_track_state(line: str) -> Optional[str]:
    marker = ",DISP,"
    if marker not in line or not line.startswith("#CAP,"):
        return None
    tail = line.split(marker, 1)[1]
    parts = tail.split(",")
    if len(parts) < 2:
        return None
    return parts[1].strip()


def latest_track_state(lines: list[str]) -> Optional[str]:
    latest: Optional[str] = None
    latest_ts = -1
    for line in lines:
        transition = parse_track_transition(line)
        if transition is None:
            continue
        _from_state, to_state = transition
        ts = parse_cap_micros(line)
        if ts is not None and ts >= latest_ts:
            latest_ts = ts
            latest = to_state
    return latest


def extract_recs_stop_lengths(lines: list[str]) -> list[dict[str, int]]:
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
