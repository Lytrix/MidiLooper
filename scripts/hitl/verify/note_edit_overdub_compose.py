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
MOVE_NOTE_TYPE = 3
SELECTION_CHANGED_RE = re.compile(r"Note selection changed: .* -> (-?\d+)")
GEOM_APPLY_PHASE_RE = re.compile(
    r"#CAP,\d+,GEOM_APPLY,phase,([^,]+),(\d+),(\d+),(\d+)"
)
GEOM_APPLY_RESOLVE_RE = re.compile(r"#CAP,\d+,GEOM_APPLY,resolve,(\d+),")
# 170818 PLAYING resolve 3.6–4.0 ms; overdub gather was 91–234 ms.
MAX_OVERDUB_RESOLVE_US = 20000
EDIT_SESSION_ACTION_RE = re.compile(
    r"EditSessionAction: type=(\d+) noteId="
)
OVERLAP_NOTES_RE = re.compile(r"overlapNotes=(\d+)")
COORD_RE = re.compile(
    r"#CAP,\d+,COORD,abs,\d+,storage,\d+,proj,\d+,display,\d+,"
    r"startLoop,\d+,projStart,-?\d+,loopStart,\d+,ch,(\d+),note,(\d+)"
)
SEVT_RE = re.compile(r"#CAP,\d+,SEVT,[A-Za-z],\d+,(\d+),(\d+)")
DNTE_FULL_RE = re.compile(r"#CAP,\d+,DNTE,(\d+),(\d+),(\d+),(\d+),")
DISP_LOOP_LEN_RE = re.compile(r"#CAP,\d+,DISP,\d+,[A-Za-z_]+,(\d+),")
DISP_STATE_RE = re.compile(r"#CAP,\d+,DISP,\d+,([A-Z_]+),")
DRAW_SRC_LAYERS_RE = re.compile(
    r"draw_src,layers,visual,(\d+),preview,\d+,sealed,\d+,snap,\d+,frame,(\d+),hidden,(\d+)"
)
LOOP_EDIT_ENTER_RE = re.compile(r"Edit session: LOOP_EDIT")
MOTOR_ACK_CHANNEL = 13
FADER_COARSE_CHANNEL = 14
FADER_CC_CHANNEL = 15
MOTOR_ACK_NOTES = frozenset(range(80, 88))
FADER_CC_NUMBERS = frozenset({2, 3})


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


def compose_uses_reopen_select_move(spec) -> bool:
    if spec is None:
        return False
    return "reenter_note_edit" in spec.interactions


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


def parse_note_edit_open_rows(lines: list[str]) -> list[dict[str, int]]:
    rows: list[dict[str, int]] = []
    for index, line in enumerate(lines):
        match = NOTE_EDIT_OPEN_RE.search(line)
        if match is None:
            continue
        rows.append(
            {
                "index": index,
                "visual_notes": int(match.group(1)),
                "ts": _cap_ts(line) if _cap_ts(line) is not None else index,
            }
        )
    return rows


def parse_note_edit_open_visual_notes(lines: list[str]) -> Optional[dict[str, int]]:
    rows = parse_note_edit_open_rows(lines)
    return rows[-1] if rows else None


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


def _overdubbing_index_ranges(lines: list[str]) -> list[tuple[int, int]]:
    starts = parse_playing_to_overdubbing(lines)
    stops = parse_overdubbing_to_playing(lines)
    ranges: list[tuple[int, int]] = []
    stop_i = 0
    for start in starts:
        end = len(lines)
        while stop_i < len(stops) and stops[stop_i]["index"] <= start["index"]:
            stop_i += 1
        if stop_i < len(stops):
            end = stops[stop_i]["index"]
            stop_i += 1
        ranges.append((start["index"], end))
    return ranges


def _index_in_ranges(index: int, ranges: list[tuple[int, int]]) -> bool:
    return any(start <= index < end for start, end in ranges)


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


def _case_reopen_select_and_move(lines: list[str]) -> dict[str, Any]:
    issues: list[str] = []
    opens = parse_note_edit_open_rows(lines)
    if len(opens) < 2:
        issues.append("missing_reopen_note_edit")
        return {"ok": False, "issues": issues, "selection_idx": None, "moves": 0}
    reopen_index = opens[1]["index"]
    selection_idx: Optional[int] = None
    moves = 0
    for index, line in enumerate(lines):
        if index <= reopen_index:
            continue
        changed = SELECTION_CHANGED_RE.search(line)
        if changed is not None:
            selection_idx = int(changed.group(1))
        action = EDIT_SESSION_ACTION_RE.search(line)
        if action is not None and int(action.group(1)) == MOVE_NOTE_TYPE:
            moves += 1
    if selection_idx is None or selection_idx < 0:
        issues.append("reopen_select_empty")
    if moves < 1:
        issues.append("missing_reopen_move_action")
    return {
        "ok": not issues,
        "issues": issues,
        "selection_idx": selection_idx,
        "moves": moves,
        "reopen_index": reopen_index,
    }


def _case_live_overdub_notes_stay_on_roll(
    lines: list[str], *, low: int = LIVE_CAPTURE_LOW, high: int = LIVE_CAPTURE_HIGH
) -> dict[str, Any]:
    issues: list[str] = []
    opens = parse_note_edit_open_rows(lines)
    opened = opens[0] if opens else None
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


def _case_control_plane_not_captured(lines: list[str]) -> dict[str, Any]:
    issues: list[str] = []
    ranges = _overdubbing_index_ranges(lines)
    coord_ack = 0
    coord_fader_notes = 0
    sevt_control = 0
    for index, line in enumerate(lines):
        coord = COORD_RE.search(line)
        if coord is not None and _index_in_ranges(index, ranges):
            channel = int(coord.group(1))
            note = int(coord.group(2))
            if channel == MOTOR_ACK_CHANNEL and note in MOTOR_ACK_NOTES:
                coord_ack += 1
            elif channel in (FADER_COARSE_CHANNEL, FADER_CC_CHANNEL):
                coord_fader_notes += 1
        sevt = SEVT_RE.search(line)
        if sevt is not None:
            channel = int(sevt.group(1))
            note = int(sevt.group(2))
            if channel == MOTOR_ACK_CHANNEL and note in MOTOR_ACK_NOTES:
                sevt_control += 1
            elif channel == FADER_COARSE_CHANNEL:
                sevt_control += 1
            elif channel == FADER_CC_CHANNEL and note in FADER_CC_NUMBERS:
                sevt_control += 1
    if coord_ack:
        issues.append(f"overdub_coord_ch13_ack count={coord_ack}")
    if coord_fader_notes:
        issues.append(f"overdub_coord_ch14_or_ch15 count={coord_fader_notes}")
    if sevt_control:
        issues.append(f"stored_control_plane_sevt count={sevt_control}")
    return {
        "ok": not issues,
        "issues": issues,
        "overdub_coord_ch13_ack": coord_ack,
        "overdub_coord_ch14_or_ch15": coord_fader_notes,
        "stored_control_plane_sevt": sevt_control,
    }


def _case_overdub_apply_reuses_committed_base(lines: list[str]) -> dict[str, Any]:
    issues: list[str] = []
    ranges = _overdubbing_index_ranges(lines)
    resolve_us: list[int] = []
    for index, line in enumerate(lines):
        if not _index_in_ranges(index, ranges):
            continue
        match = GEOM_APPLY_RESOLVE_RE.search(line)
        if match is None:
            continue
        resolve_us.append(int(match.group(1)))
    max_us = max(resolve_us) if resolve_us else 0
    if resolve_us and max_us > MAX_OVERDUB_RESOLVE_US:
        issues.append(
            f"overdub_resolve_us={max_us} max={MAX_OVERDUB_RESOLVE_US}"
        )
    return {
        "ok": not issues,
        "issues": issues,
        "overdub_resolve_count": len(resolve_us),
        "max_overdub_resolve_us": max_us,
    }


def _loop_length_before(lines: list[str], index: int) -> Optional[int]:
    loop_len: Optional[int] = None
    for row in lines[:index]:
        match = DISP_LOOP_LEN_RE.search(row)
        if match is None:
            continue
        value = int(match.group(1))
        if value > 0:
            loop_len = value
    return loop_len


def _case_pending_same_pitch_note_on_not_loop_end_tail(lines: list[str]) -> dict[str, Any]:
    # 202226/203112: rejected second On leaves first On paired. A painted note
    # whose length is loopLength-start and whose start is in the first half is
    # the finishOpenNotes orphan tail (48@435 length 1869 on loop 2304).
    tails: list[str] = []
    judged = 0
    for index, line in enumerate(lines):
        match = DNTE_FULL_RE.search(line)
        if match is None:
            continue
        start = int(match.group(2))
        length = int(match.group(4))
        loop_len = _loop_length_before(lines, index)
        if loop_len is None or loop_len < 2:
            continue
        judged += 1
        if start == 0 or start >= (loop_len // 2):
            continue
        if length == (loop_len - start):
            tails.append(f"{match.group(1)}@{start}len{length}")
    issues: list[str] = []
    if tails:
        issues.append("same_pitch_pending_loop_end_tail=" + ",".join(tails))
    return {
        "ok": not issues,
        "issues": issues,
        "dnte_judged": judged,
        "loop_end_tails": tails,
    }


def _case_pending_note_edit_close_keeps_overdub_frame(lines: list[str]) -> dict[str, Any]:
    # 214900: NOTE_EDIT close while OVERDUBBING gathered stale snapshot 56
    # against visual 65. Vacuous when the log never leaves NOTE_EDIT in overdub.
    judged = 0
    issues: list[str] = []
    last_dframe = None
    last_disp_state = None
    for index, line in enumerate(lines):
        dframe = DFRAME_RE.search(line)
        if dframe is not None:
            last_dframe = int(dframe.group(2))
        disp = DISP_STATE_RE.search(line)
        if disp is not None:
            last_disp_state = disp.group(1)
        if LOOP_EDIT_ENTER_RE.search(line) is None:
            continue
        if last_disp_state != "OVERDUBBING":
            continue
        judged += 1
        visual = None
        frame = None
        for row in lines[index + 1 : min(len(lines), index + 400)]:
            layers = DRAW_SRC_LAYERS_RE.search(row)
            if layers is not None:
                visual = int(layers.group(1))
                frame = int(layers.group(2))
                break
            later = DFRAME_RE.search(row)
            if later is not None and frame is None:
                frame = int(later.group(2))
        if frame is None:
            issues.append("overdub_close_missing_frame")
            continue
        if last_dframe is not None and frame < last_dframe:
            issues.append(f"overdub_close_frame={frame}<dframe={last_dframe}")
        if visual is not None and visual > frame:
            issues.append(f"overdub_close_stale_gather=visual{visual}>frame{frame}")
    return {
        "ok": not issues,
        "issues": issues,
        "closes_judged": judged,
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
        "control_plane_not_captured": _case_control_plane_not_captured(lines),
        "overdub_apply_reuses_committed_base": _case_overdub_apply_reuses_committed_base(
            lines
        ),
        "pending_same_pitch_note_on_not_loop_end_tail": (
            _case_pending_same_pitch_note_on_not_loop_end_tail(lines)
        ),
        "pending_note_edit_close_keeps_overdub_frame": (
            _case_pending_note_edit_close_keeps_overdub_frame(lines)
        ),
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
    if header_present and compose_uses_reopen_select_move(spec):
        cases["reopen_select_and_move"] = _case_reopen_select_and_move(lines)
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
