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
CHANGE_PITCH_TYPE = 5
PITCH_ACTION_RE = re.compile(
    r"EditSessionAction: type=5 noteId=\d+ start=(\d+) end=\d+ pitch=(\d+)"
)
DNTE_RE = re.compile(r"#CAP,\d+,DNTE,(\d+),(\d+),")
DNTE_FULL_RE = re.compile(r"#CAP,\d+,DNTE,(\d+),(\d+),(\d+),(\d+),")
SELECTION_CHANGED_RE = re.compile(r"Note selection changed: .* -> (-?\d+)")
GEOM_APPLY_PHASE_RE = re.compile(
    r"#CAP,\d+,GEOM_APPLY,phase,([^,]+),(\d+),(\d+),(\d+)"
)
GEOM_APPLY_RESOLVE_RE = re.compile(r"#CAP,\d+,GEOM_APPLY,resolve,(\d+),")
COMMIT_TRACE_RECONSTRUCT_RE = re.compile(
    r"commitEditAction (replay_flat|take_only|session_store|loop_materialized):"
)
COMMIT_CHANGE_LENGTH_RE = re.compile(
    r"commitEditAction incoming ChangeLength targetNoteId=\d+ "
    r"start=\d+ moverBaselineEnd=(\d+) newEnd=(\d+)"
)
CLOSE_COMMIT_PARITY_APPLY_OWNED_RE = re.compile(
    r"NOTE_EDIT commit parity mismatch canonical=(\d+) apply_owned=(\d+)"
)
NOTE_EDIT_CLOSE_RE = re.compile(
    r"NoteEditPass close keep committed|NoteEditPassClosed"
)
DISP_LOOP_LEN_RE = re.compile(r"#CAP,\d+,DISP,\d+,[A-Za-z_]+,(\d+),")
DISP_STATE_RE = re.compile(r"#CAP,\d+,DISP,\d+,([A-Z_]+),")
DISP_SLOT_STATE_LEN_RE = re.compile(r"#CAP,\d+,DISP,(\d+),([A-Z_]+),(\d+),")
DRAW_SRC_LAYERS_RE = re.compile(
    r"draw_src,layers,visual,(\d+),preview,\d+,sealed,\d+,snap,\d+,frame,(\d+),hidden,(\d+)"
)
LOOP_EDIT_ENTER_RE = re.compile(r"Edit session: LOOP_EDIT")
MOTOR_SYNC_SENT_RE = re.compile(
    r"(?:select_motor_sync sent=1|geometry_motor_sync sent=1)"
)
MOTOR_NOTEGATE_MO_RE = re.compile(r"#CAP,\d+,MO,144,(14|15|16),([0-2]),")
MOTOR_NOTEGATE_ON_RE = re.compile(r"#CAP,(\d+),MO,144,(14|15|16),([0-2]),")
MOTOR_NOTEGATE_OFF_RE = re.compile(r"#CAP,(\d+),MO,128,(14|15|16),([0-2]),")
MOTOR_SYNC_VALUE_ONLY_RE = re.compile(
    r"(?:select_motor_sync|geometry_motor_sync).*sequence=value_only"
)
# Firmware hold is 125 ms; 80 ms rejects the 1 ms ValueOnly On/Off pair (`232309`).
MIN_VALUE_ONLY_NOTEGATE_HOLD_US = 80000
# 170818 PLAYING resolve 3.6–4.0 ms; overdub gather was 91–234 ms.
MAX_OVERDUB_RESOLVE_US = 20000
# Architectural confirm (183453 ~3.5 ms). Fail ~33 ms reconstruct even if resolve dips under 20 ms.
MAX_OVERDUB_RECONSTRUCT_US = 10000
EDIT_SESSION_ACTION_RE = re.compile(
    r"EditSessionAction: type=(\d+) noteId="
)
OVERLAP_NOTES_RE = re.compile(r"overlapNotes=(\d+)")
COORD_RE = re.compile(
    r"#CAP,\d+,COORD,abs,\d+,storage,\d+,proj,\d+,display,\d+,"
    r"startLoop,\d+,projStart,-?\d+,loopStart,\d+,ch,(\d+),note,(\d+)"
)
SEVT_RE = re.compile(r"#CAP,\d+,SEVT,[A-Za-z],\d+,(\d+),(\d+)")
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
    reconstruct_us: list[int] = []
    for index, line in enumerate(lines):
        if not _index_in_ranges(index, ranges):
            continue
        resolve_match = GEOM_APPLY_RESOLVE_RE.search(line)
        if resolve_match is not None:
            resolve_us.append(int(resolve_match.group(1)))
        phase_match = GEOM_APPLY_PHASE_RE.search(line)
        if phase_match is not None and phase_match.group(1) == "reconstruct":
            reconstruct_us.append(int(phase_match.group(2)))
    max_us = max(resolve_us) if resolve_us else 0
    max_reconstruct_us = max(reconstruct_us) if reconstruct_us else 0
    if resolve_us and max_us > MAX_OVERDUB_RESOLVE_US:
        issues.append(
            f"overdub_resolve_us={max_us} max={MAX_OVERDUB_RESOLVE_US}"
        )
    if reconstruct_us and max_reconstruct_us > MAX_OVERDUB_RECONSTRUCT_US:
        issues.append(
            f"overdub_reconstruct_us={max_reconstruct_us} max={MAX_OVERDUB_RECONSTRUCT_US}"
        )
    return {
        "ok": not issues,
        "issues": issues,
        "overdub_resolve_count": len(resolve_us),
        "max_overdub_resolve_us": max_us,
        "overdub_reconstruct_count": len(reconstruct_us),
        "max_overdub_reconstruct_us": max_reconstruct_us,
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


def _case_note_edit_close_persists_apply_owned(lines: list[str]) -> dict[str, Any]:
    # 223532: exit after grace-deferred select-away logged canonical=0
    # apply_owned=1 and bake kept earlier rows. Vacuous when no close
    # carries an apply_owned parity line.
    judged = 0
    issues: list[str] = []
    last_apply_owned = None
    for line in lines:
        parity = CLOSE_COMMIT_PARITY_APPLY_OWNED_RE.search(line)
        if parity is not None:
            last_apply_owned = (int(parity.group(1)), int(parity.group(2)))
            continue
        if NOTE_EDIT_CLOSE_RE.search(line) is None:
            continue
        if last_apply_owned is None:
            continue
        judged += 1
        canonical, apply_owned = last_apply_owned
        if canonical == 0 and apply_owned > 0:
            issues.append(
                f"close_dropped_apply_owned=canonical{canonical},apply_owned{apply_owned}"
            )
        last_apply_owned = None
    return {
        "ok": not issues,
        "issues": issues,
        "closes_judged": judged,
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


def _case_stop_selected_slot_not_foreign_loop(lines: list[str]) -> dict[str, Any]:
    # 214900: after OVERDUBBING→STOPPED the selected slot must keep the
    # overdub loopLength. Later DISP on that slot must not become 6144/50688.
    last_overdub_slot = None
    last_overdub_len = None
    for line in lines:
        disp = DISP_SLOT_STATE_LEN_RE.search(line)
        if disp is not None and disp.group(2) == "OVERDUBBING":
            last_overdub_slot = int(disp.group(1))
            last_overdub_len = int(disp.group(3))
    judged = 0
    issues: list[str] = []
    watching = False
    saw_selected = False
    for line in lines:
        st = ST_RE.search(line)
        if st is not None and st.group(2) == "OVERDUBBING" and st.group(3) == "STOPPED":
            watching = True
            continue
        if not watching:
            continue
        later_st = ST_RE.search(line)
        if later_st is not None and later_st.group(3) in {
            "PLAYING",
            "RECORDING",
            "OVERDUBBING",
        }:
            break
        disp = DISP_SLOT_STATE_LEN_RE.search(line)
        if disp is None or disp.group(2) != "STOPPED":
            continue
        if last_overdub_slot is None or int(disp.group(1)) != last_overdub_slot:
            continue
        judged += 1
        loop_len = int(disp.group(3))
        if not saw_selected:
            saw_selected = True
            if last_overdub_len is not None and loop_len != last_overdub_len:
                issues.append(
                    f"stop_selected_slot_len={loop_len} overdub_len={last_overdub_len}"
                )
            continue
        if last_overdub_len is not None and loop_len != last_overdub_len:
            issues.append(
                f"stop_selected_slot_later_len={loop_len} overdub_len={last_overdub_len}"
            )
    if watching and last_overdub_len is not None and not saw_selected:
        issues.append("stop_selected_slot_missing_disp")
    return {
        "ok": not issues,
        "issues": issues,
        "stops_judged": judged,
    }


def _case_commit_skips_unpainted_mover_loop_end(lines: list[str]) -> dict[str, Any]:
    # 192140: close wrote ChangeLength 80–2303 after in-edit overdub. Inclusive
    # loop-end Off is loopLength-1. Skip when baseline was not already there.
    judged = 0
    invented = 0
    for index, line in enumerate(lines):
        match = COMMIT_CHANGE_LENGTH_RE.search(line)
        if match is None:
            continue
        baseline_end = int(match.group(1))
        new_end = int(match.group(2))
        loop_len = _loop_length_before(lines, index)
        if loop_len is None:
            continue
        judged += 1
        loop_ends = {loop_len, loop_len - 1}
        if new_end not in loop_ends:
            continue
        if baseline_end in loop_ends:
            continue
        invented += 1
    issues: list[str] = []
    if invented:
        issues.append(f"mover_length_to_loop_end={invented}/{judged}")
    return {
        "ok": not issues,
        "issues": issues,
        "change_length_commits": judged,
        "mover_length_to_loop_end": invented,
    }


def _case_commit_skips_trace_reconstruct(lines: list[str]) -> dict[str, Any]:
    stages: list[str] = []
    for line in lines:
        match = COMMIT_TRACE_RECONSTRUCT_RE.search(line)
        if match is None:
            continue
        stages.append(match.group(1))
    issues: list[str] = []
    if stages:
        issues.append("commit_trace_reconstruct=" + ",".join(stages))
    return {
        "ok": not issues,
        "issues": issues,
        "trace_reconstruct_count": len(stages),
    }


def _case_pitch_keeps_mover_dnte(lines: list[str]) -> dict[str, Any]:
    # 190408: type=5 of note 94 @2481 had no DNTE for the whole gesture.
    # After overlap Hide the mover must still highlight (DNTE pitch+start).
    seen = 0
    missing = 0
    for index, line in enumerate(lines):
        match = PITCH_ACTION_RE.search(line)
        if match is None:
            continue
        seen += 1
        start = int(match.group(1))
        pitch = int(match.group(2))
        found = False
        for row in lines[index + 1 : min(len(lines), index + 80)]:
            if PITCH_ACTION_RE.search(row) is not None:
                break
            dnte = DNTE_RE.search(row)
            if dnte is not None and int(dnte.group(1)) == pitch and int(dnte.group(2)) == start:
                found = True
                break
        if not found:
            missing += 1
    issues: list[str] = []
    if seen and missing:
        issues.append(f"pitch_without_dnte={missing}/{seen}")
    return {
        "ok": not issues,
        "issues": issues,
        "pitch_actions": seen,
        "pitch_without_dnte": missing,
    }


def _case_overdub_motor_sends_notegate(lines: list[str]) -> dict[str, Any]:
    ranges = _overdubbing_index_ranges(lines)
    sent = 0
    missing = 0
    for index, line in enumerate(lines):
        if not _index_in_ranges(index, ranges):
            continue
        if MOTOR_SYNC_SENT_RE.search(line) is None:
            continue
        sent += 1
        window = lines[max(0, index - 8) : index + 9]
        if not any(MOTOR_NOTEGATE_MO_RE.search(row) for row in window):
            missing += 1
    issues: list[str] = []
    if sent and missing:
        issues.append(f"overdub_motor_missing_notegate={missing}/{sent}")
    return {
        "ok": not issues,
        "issues": issues,
        "overdub_motor_sync_sent": sent,
        "overdub_motor_missing_notegate": missing,
    }


def _case_value_only_notegate_held(lines: list[str]) -> dict[str, Any]:
    judged = 0
    short = 0
    missing_off = 0
    for index, line in enumerate(lines):
        if MOTOR_SYNC_VALUE_ONLY_RE.search(line) is None:
            continue
        ons: list[tuple[int, int, str, str]] = []
        window_start = max(0, index - 8)
        window_end = min(len(lines), index + 21)
        for row_index in range(window_start, window_end):
            match = MOTOR_NOTEGATE_ON_RE.search(lines[row_index])
            if match is None:
                continue
            ons.append(
                (row_index, int(match.group(1)), match.group(2), match.group(3))
            )
        if not ons:
            continue
        judged += 1
        for on_index, on_us, channel, note in ons:
            off_us = None
            for row in lines[on_index + 1 :]:
                off_match = MOTOR_NOTEGATE_OFF_RE.search(row)
                if (
                    off_match is not None
                    and off_match.group(2) == channel
                    and off_match.group(3) == note
                ):
                    off_us = int(off_match.group(1))
                    break
            if off_us is None:
                missing_off += 1
            elif off_us - on_us < MIN_VALUE_ONLY_NOTEGATE_HOLD_US:
                short += 1
    issues: list[str] = []
    if short:
        issues.append(f"value_only_notegate_short={short}")
    if missing_off:
        issues.append(f"value_only_notegate_off_missing={missing_off}")
    return {
        "ok": not issues,
        "issues": issues,
        "value_only_notegate_held_judged": judged,
        "value_only_notegate_short": short,
        "value_only_notegate_off_missing": missing_off,
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
        "commit_skips_trace_reconstruct": _case_commit_skips_trace_reconstruct(lines),
        "commit_skips_unpainted_mover_loop_end": _case_commit_skips_unpainted_mover_loop_end(
            lines
        ),
        "pending_same_pitch_note_on_not_loop_end_tail": (
            _case_pending_same_pitch_note_on_not_loop_end_tail(lines)
        ),
        "pending_note_edit_close_keeps_overdub_frame": (
            _case_pending_note_edit_close_keeps_overdub_frame(lines)
        ),
        "note_edit_close_persists_apply_owned": (
            _case_note_edit_close_persists_apply_owned(lines)
        ),
        "stop_selected_slot_not_foreign_loop": _case_stop_selected_slot_not_foreign_loop(
            lines
        ),
        "overdub_motor_sends_notegate": _case_overdub_motor_sends_notegate(lines),
        "value_only_notegate_held": _case_value_only_notegate_held(lines),
        "pitch_keeps_mover_dnte": _case_pitch_keeps_mover_dnte(lines),
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
