"""Serial cases for NOTE_EDIT plus in-edit overdub compose paint."""

from __future__ import annotations

import re
from typing import Any, Optional

from hitl.capture_transitions import _count_capture_transitions
from hitl.stimuli.spec import KIND_TEMPORARY

NOTE_EDIT_OPEN_RE = re.compile(r"NOTE_EDIT_OPEN,sync,visual_notes,(\d+)")
DFRAME_RE = re.compile(r"#CAP,(\d+),DFRAME,(\d+),")
ST_RE = re.compile(r"#CAP,(\d+),ST,Track,([^,]+),([^,\s]+)")
MI_U_RE = re.compile(r"#CAP,(\d+),MI,U,(\d+),(\d+),(\d+),(\d+)")

LIVE_CAPTURE_LOW = 12
LIVE_CAPTURE_HIGH = 23
NOTE_ON_STATUS = 144
HIDE_NOTE_TYPE = 2
SHORTEN_NOTE_TYPE = 1
GEOM_APPLY_PHASE_RE = re.compile(
    r"#CAP,\d+,GEOM_APPLY,phase,([^,]+),(\d+),(\d+),(\d+)"
)
EDIT_SESSION_ACTION_RE = re.compile(
    r"EditSessionAction: type=(\d+) noteId="
)
OVERLAP_NOTES_RE = re.compile(r"overlapNotes=(\d+)")


def compose_uses_same_pitch_hold(spec) -> bool:
    if spec is None:
        return False
    if spec.kind == KIND_TEMPORARY:
        return True
    return "same_pitch_hold" in spec.interactions


def compose_uses_last_bar_shorten(spec) -> bool:
    if spec is None:
        return False
    return "shorten_new_note_last_bar" in spec.interactions


def live_capture_band_from_spec(spec) -> tuple[int, int]:
    for overdub in spec.overdubs:
        if overdub.name == "IN_EDIT_OD" and overdub.note_low is not None:
            high = overdub.note_high if overdub.note_high is not None else overdub.note_low
            return int(overdub.note_low), int(high)
    return LIVE_CAPTURE_LOW, LIVE_CAPTURE_HIGH


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


def _case_same_pitch_hold_emits_overlap(lines: list[str], *, after_index: int) -> dict[str, Any]:
    issues: list[str] = []
    build_actions = 0
    hide_or_shorten = 0
    overlap_notes = 0
    for index, line in enumerate(lines):
        if index <= after_index:
            continue
        phase = GEOM_APPLY_PHASE_RE.search(line)
        if phase is not None and phase.group(1) == "build":
            build_actions = max(build_actions, int(phase.group(3)))
        action = EDIT_SESSION_ACTION_RE.search(line)
        if action is not None and int(action.group(1)) in (HIDE_NOTE_TYPE, SHORTEN_NOTE_TYPE):
            hide_or_shorten += 1
        overlap = OVERLAP_NOTES_RE.search(line)
        if overlap is not None:
            overlap_notes = max(overlap_notes, int(overlap.group(1)))
    window = lines[after_index + 1 :]
    if any("coarse_blocked" in line for line in window):
        issues.append("coarse_fader_blocked_pending_select")
    elif any("No note selected for coarse editing" in line for line in window):
        issues.append("coarse_fader_no_note_selected")
    elif hide_or_shorten < 1:
        issues.append("missing_same_pitch_hold_overlap_action")
    return {
        "ok": not issues,
        "issues": issues,
        "build_actions": build_actions,
        "hide_or_shorten": hide_or_shorten,
        "overlap_notes": overlap_notes,
    }


def _case_new_note_shortened(lines: list[str], *, after_index: int) -> dict[str, Any]:
    issues: list[str] = []
    shorten = 0
    for index, line in enumerate(lines):
        if index <= after_index:
            continue
        action = EDIT_SESSION_ACTION_RE.search(line)
        if action is not None and int(action.group(1)) == SHORTEN_NOTE_TYPE:
            shorten += 1
    window = lines[after_index + 1 :]
    if any("coarse_blocked" in line for line in window):
        issues.append("coarse_fader_blocked_pending_select")
    elif any("No note selected for coarse editing" in line for line in window):
        issues.append("coarse_fader_no_note_selected")
    elif shorten < 1:
        issues.append("missing_new_note_shorten_action")
    return {
        "ok": not issues,
        "issues": issues,
        "shorten": shorten,
    }


def _case_live_overdub_notes_stay_on_roll(
    lines: list[str], *, low: int = LIVE_CAPTURE_LOW, high: int = LIVE_CAPTURE_HIGH
) -> dict[str, Any]:
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
            "mover_drag_index": None,
        }

    overdubs = [
        row for row in parse_playing_to_overdubbing(lines) if row["index"] > opened["index"]
    ]
    if not overdubs:
        issues.append("missing_in_edit_playing_overdubbing")

    live_after = overdubs[0]["index"] if overdubs else opened["index"]
    live_ons = parse_live_capture_note_ons(
        lines, after_index=live_after, low=low, high=high
    )
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
        "mover_drag_index": move_index,
    }


def verify_note_edit_overdub_compose(lines: list[str], args: object) -> dict[str, object]:
    from hitl.stimuli import (
        get_stimulus,
        run_stimulus,
        stimulus_for_scenario,
        stimulus_report_fields,
    )
    from hitl.stimuli.fingerprint import parse_hitl_fields
    from hitl.stimuli.validate import validate_stimulus

    scenario_id = getattr(args, "active_scenario_id", None) or "note_edit_overdub_compose"
    bound = stimulus_for_scenario(scenario_id)
    spec = run_stimulus(bound) if bound is not None else bound
    fields = parse_hitl_fields(lines)
    header_id = fields.get("STIMULUS")
    if header_id:
        try:
            spec = run_stimulus(get_stimulus(header_id))
        except KeyError:
            pass
    require_header = bool(getattr(args, "require_stimulus_header", False))
    stimulus = validate_stimulus(
        lines, spec, require_header=require_header
    )
    low, high = live_capture_band_from_spec(spec)
    transitions = _count_capture_transitions(lines)
    live_case = _case_live_overdub_notes_stay_on_roll(lines, low=low, high=high)
    cases = {
        "stimulus_validation": stimulus,
        "live_overdub_notes_stay_on_roll": live_case,
    }
    header_present = bool(stimulus.get("header", {}).get("header_present"))
    require_hold = (
        spec.kind == KIND_TEMPORARY or (
            header_present and compose_uses_same_pitch_hold(spec)
        )
    ) and not compose_uses_last_bar_shorten(spec)
    if require_hold:
        after = live_case.get("mover_drag_index")
        if after is None:
            after = 0
        cases["same_pitch_hold_emits_overlap"] = _case_same_pitch_hold_emits_overlap(
            lines, after_index=after
        )
    require_shorten = header_present and compose_uses_last_bar_shorten(spec)
    if require_shorten:
        after = live_case.get("mover_drag_index")
        if after is None:
            after = 0
        cases["new_note_shortened"] = _case_new_note_shortened(lines, after_index=after)
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
            "low": low,
            "high": high,
        },
        "stimulus": stimulus_report_fields(spec),
    }
