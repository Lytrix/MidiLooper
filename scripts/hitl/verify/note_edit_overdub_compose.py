"""Serial cases for NOTE_EDIT plus in-edit overdub compose paint."""

from __future__ import annotations

import re
from typing import Any, Optional

from hitl.capture_transitions import _count_capture_transitions

NOTE_EDIT_OPEN_RE = re.compile(r"NOTE_EDIT_OPEN,sync,visual_notes,(\d+)")
DFRAME_RE = re.compile(r"#CAP,(\d+),DFRAME,(\d+),")
ST_RE = re.compile(r"#CAP,(\d+),ST,Track,([^,]+),([^,\s]+)")
MI_U_RE = re.compile(r"#CAP,(\d+),MI,U,(\d+),(\d+),(\d+),(\d+)")

LIVE_CAPTURE_LOW = 12
LIVE_CAPTURE_HIGH = 23
NOTE_ON_STATUS = 144


def _cap_ts(line: str) -> Optional[int]:
    cap = line.find("#CAP,")
    if cap < 0:
        return None
    parts = line[cap:].split(",")
    if len(parts) < 2:
        return None
    try:
        return int(parts[1])
    except ValueError:
        return None


def parse_note_edit_open_visual_notes(lines: list[str]) -> Optional[dict[str, int]]:
    last: Optional[dict[str, int]] = None
    for index, line in enumerate(lines):
        match = NOTE_EDIT_OPEN_RE.search(line)
        if match is None:
            continue
        last = {
            "index": index,
            "visual_notes": int(match.group(1)),
            "ts": _cap_ts(line) if _cap_ts(line) is not None else index,
        }
    return last


def parse_dframes(lines: list[str]) -> list[dict[str, int]]:
    rows: list[dict[str, int]] = []
    for index, line in enumerate(lines):
        match = DFRAME_RE.search(line)
        if match is None:
            continue
        rows.append(
            {
                "index": index,
                "ts": int(match.group(1)),
                "notes": int(match.group(2)),
            }
        )
    return rows


def parse_playing_to_overdubbing(lines: list[str]) -> list[dict[str, int]]:
    rows: list[dict[str, int]] = []
    for index, line in enumerate(lines):
        match = ST_RE.search(line)
        if match is None:
            continue
        if match.group(2).strip() != "PLAYING" or match.group(3).strip() != "OVERDUBBING":
            continue
        rows.append({"index": index, "ts": int(match.group(1))})
    return rows


def parse_overdubbing_to_playing(lines: list[str]) -> list[dict[str, int]]:
    rows: list[dict[str, int]] = []
    for index, line in enumerate(lines):
        match = ST_RE.search(line)
        if match is None:
            continue
        if match.group(2).strip() != "OVERDUBBING" or match.group(3).strip() != "PLAYING":
            continue
        rows.append({"index": index, "ts": int(match.group(1))})
    return rows


def parse_live_capture_note_ons(
    lines: list[str],
    *,
    after_index: int,
    low: int = LIVE_CAPTURE_LOW,
    high: int = LIVE_CAPTURE_HIGH,
) -> list[dict[str, int]]:
    rows: list[dict[str, int]] = []
    for index, line in enumerate(lines):
        if index <= after_index:
            continue
        match = MI_U_RE.search(line)
        if match is None:
            continue
        status = int(match.group(2))
        pitch = int(match.group(4))
        velocity = int(match.group(5))
        if status != NOTE_ON_STATUS or velocity <= 0:
            continue
        if pitch < low or pitch > high:
            continue
        rows.append(
            {
                "index": index,
                "ts": int(match.group(1)),
                "pitch": pitch,
            }
        )
    return rows


def _move_marker_index(lines: list[str], *, after_index: int) -> Optional[int]:
    for index, line in enumerate(lines):
        if index <= after_index:
            continue
        if "POSITION EDIT" in line or "GEOM_APPLY" in line:
            return index
        if ",MI,U,224,14," in line:
            return index
    return None


def _case_live_overdub_notes_stay_on_roll(lines: list[str]) -> dict[str, Any]:
    issues: list[str] = []
    opened = parse_note_edit_open_visual_notes(lines)
    if opened is None:
        issues.append("missing_note_edit_open")
        return {
            "ok": False,
            "issues": issues,
            "open_visual_notes": None,
            "live_capture_note_ons": 0,
            "dframes_during_overdub": 0,
            "max_dframe_during_overdub": None,
        }

    overdubs = [
        row for row in parse_playing_to_overdubbing(lines) if row["index"] > opened["index"]
    ]
    if not overdubs:
        issues.append("missing_in_edit_playing_overdubbing")

    live_after = overdubs[0]["index"] if overdubs else opened["index"]
    live_ons = parse_live_capture_note_ons(lines, after_index=live_after)
    if not live_ons:
        issues.append("missing_live_capture_note_on")

    stops = [
        row
        for row in parse_overdubbing_to_playing(lines)
        if row["index"] > (live_ons[0]["index"] if live_ons else live_after)
    ]
    stop_index = stops[0]["index"] if stops else len(lines)
    window_start = live_ons[0]["index"] if live_ons else live_after
    dframes = parse_dframes(lines)
    during = [
        row for row in dframes if window_start <= row["index"] < stop_index
    ]
    max_during = max((row["notes"] for row in during), default=None)
    open_notes = opened["visual_notes"]
    if not during:
        issues.append("missing_dframe_during_in_edit_overdub")
    elif max_during is not None and max_during <= open_notes:
        issues.append(
            f"dframe_omitted_live_capture max={max_during} open={open_notes}"
        )

    move_index = _move_marker_index(
        lines, after_index=live_ons[0]["index"] if live_ons else live_after
    )

    return {
        "ok": not issues,
        "issues": issues,
        "open_visual_notes": open_notes,
        "live_capture_note_ons": len(live_ons),
        "dframes_during_overdub": len(during),
        "max_dframe_during_overdub": max_during,
        "mover_drag_seen": move_index is not None,
    }


def verify_note_edit_overdub_compose(lines: list[str], args: object) -> dict[str, object]:
    from hitl.stimuli import STIM_NE_OD_COMPOSE_01, run_stimulus, stimulus_report_fields
    from hitl.stimuli.validate import validate_stimulus

    spec = run_stimulus(STIM_NE_OD_COMPOSE_01)
    require_header = bool(getattr(args, "require_stimulus_header", False))
    stimulus = validate_stimulus(
        lines, spec, require_header=require_header
    )
    transitions = _count_capture_transitions(lines)
    cases = {
        "stimulus_validation": stimulus,
        "live_overdub_notes_stay_on_roll": _case_live_overdub_notes_stay_on_roll(lines),
    }
    issues: list[str] = []
    for name, case in cases.items():
        for issue in case.get("issues") or []:
            issues.append(f"{name}:{issue}")
    return {
        "ok": not issues,
        "issues": issues,
        "cases": cases,
        "transitions": {f"{a}->{b}": c for (a, b), c in transitions.items()},
        "live_capture_pitch_band": {
            "low": LIVE_CAPTURE_LOW,
            "high": LIVE_CAPTURE_HIGH,
        },
        "stimulus": stimulus_report_fields(spec),
    }
