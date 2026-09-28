"""#CAP protocol parsing — single owner for serial line decode."""

from __future__ import annotations

import re

from dataclasses import dataclass
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


# --- S4f.18 / response-driven HITL task 2.3: fenced read-only requests ---------
#
# The capture session owns the serial port, so a request is sent to it on stdin and
# it forwards an allowlisted command to the device, framed by two `!TRACE_FENCE`
# rows carrying the caller's nonce. The response window is exactly the rows between
# those fences; a missing fence means the window was lost (device reset, dropped
# write, coalesced line) and must never be read as an empty observation.

FENCE_MARKER = "TRACE_FENCE,"

# A fence row is exactly `#CAP,<micros>,TRACE_FENCE,<token>`. Requiring that shape
# matters: a dropped newline can merge a content row with a fence, and the merged
# line must read as a lost window rather than as a complete one.
_FENCE_ROW = re.compile(r"^#CAP,\d+,TRACE_FENCE,(\S+)$")


def format_command_request(command: str, nonce: str, end_nonce: str) -> str:
    """One stdin request line: two distinct 32-char fence tokens, then the command."""
    return f"{nonce} {end_nonce} {command}\n"


def parse_command_receipt(line: str) -> Optional[tuple[str, str, bool]]:
    """``(nonce, command, accepted)`` for a capture-owner receipt line, else None."""
    if "[capture] command accepted " in line:
        accepted = True
    elif "[capture] command refused" in line:
        accepted = False
    else:
        return None
    fields = {}
    for token in line.split()[3:]:
        if "=" in token:
            key, value = token.split("=", 1)
            fields[key] = value
    nonce = fields.get("nonce")
    command = fields.get("command")
    if not nonce:
        return None
    return nonce, command or "", accepted


def fence_token(line: str) -> Optional[str]:
    """The token of an exact ``TRACE_FENCE`` row, else None."""
    match = _FENCE_ROW.match(line.strip())
    return match.group(1) if match else None


def fenced_rows(lines: list[str], nonce: str, end_nonce: str) -> Optional[list[str]]:
    """Rows strictly between this request's two fences; None when either is missing.

    Two distinct tokens are used, so a duplicate, delayed or coalesced fence from an
    earlier request can never close this window, and a stale nonce yields None rather
    than the current window. Missing fences are loss, never an empty observation.
    """
    start = None
    for index, line in enumerate(lines):
        token = fence_token(line)
        if token is None:
            continue
        if start is None:
            if token == nonce:
                start = index
            continue
        if token == end_nonce:
            return lines[start + 1:index]
        if token == nonce:
            # A repeated start restarts the window; rows before it cannot extend it.
            start = index
    return None


# --- Asynchronous observation family -------------------------------------------
#
# `!LIB_OBSERVE` is a request, not a synchronous reply: the device emits the
# observation family later, once its SD session frees. The family is self-delimiting
# -- `LIBOBS,B` opens and `LIBOBS,E` closes it -- so completion is proven by the
# terminal row rather than by a timeout, and a missing terminal row is loss.

LIBOBS_BEGIN = ",LIBOBS,B,"
LIBOBS_END = ",LIBOBS,E,"


@dataclass(frozen=True)
class ObservationWindow:
    """One library-observation family and whether its terminal row was seen."""

    rows: list[str]
    closed: bool

    @property
    def slots(self) -> list[str]:
        return [row for row in self.rows if ",LIBOBS,S," in row]


def fenced_window(lines: list[str], nonce: str, end_nonce: str):
    """``(start_index, end_index, rows)`` for this request's fences, else None."""
    start = None
    for index, line in enumerate(lines):
        token = fence_token(line)
        if token is None:
            continue
        if start is None:
            if token == nonce:
                start = index
            continue
        if token == end_nonce:
            return start, index, lines[start + 1:index]
        if token == nonce:
            start = index
    return None


def collect_library_observation(lines: list[str], after_index: int = 0,
                                ) -> Optional[ObservationWindow]:
    """The observation family beginning after ``after_index``.

    Returns None when no family has begun yet. Once begun, the window is returned
    with ``closed=False`` until its ``LIBOBS,E`` terminal row appears, so a lost
    terminal row is never read as a settled census.
    """
    begin = None
    collected: list[str] = []
    for line in lines[after_index:]:
        if begin is None:
            if LIBOBS_BEGIN in line:
                begin = len(collected)
                collected.append(line)
            continue
        collected.append(line)
        if LIBOBS_END in line:
            return ObservationWindow(collected, True)
    if begin is None:
        return None
    return ObservationWindow(collected, False)
