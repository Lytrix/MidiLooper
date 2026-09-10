"""Serial cases for NOTE_EDIT reselect of a moved note on a stacked sixteenth."""

from __future__ import annotations

import re
from typing import Any, Optional

from hitl.control_constants import TICKS_PER_16TH_STEP
from hitl.verify.note_edit_overdub_compose import _case_value_only_notegate_held

SELECT_APPLY_RE = re.compile(
    r"select_apply selected_tick=(\d+) note_idx=(-?\d+) .* apply=(\d+)"
)
EDIT_SESSION_ACTION_RE = re.compile(r"EditSessionAction: type=(\d+) noteId=")
NOTE_EDIT_OPEN_RE = re.compile(r"NOTE_EDIT_OPEN,sync,visual_notes,(\d+)")
ST_RE = re.compile(r"#CAP,(\d+),ST,Track,([^,]+),([^,\s]+)")
MOTOR_SYNC_SEQUENCE_RE = re.compile(
    r"(?:select_motor_sync|geometry_motor_sync).*sequence=(\S+)"
)
MIDI_GAP_RE = re.compile(r"#CAP,(\d+),DIAG,midi_gap,(\d+),")
DNTE_RE = re.compile(r"#CAP,\d+,DNTE,")
VCACHE_FULL_NOTES_RE = re.compile(r"VCACHE,full,ev,-?\d+,notes,(\d+)")
MOVE_NOTE_TYPE = 3
MAX_PLAYING_MOTOR_MIDI_GAP_US = 200000
SELECT_APPLY_HIGHLIGHT_MIN_APPLIES = 10

PITCHBEND_MIN = -8192
PITCHBEND_MAX = 8192
MIDI_PITCHBEND_MAX = 8191


def select_tick_from_pitchbend(
    pitch_value: int, nav_length: int, *, pitch_min: int = PITCHBEND_MIN, pitch_max: int = PITCHBEND_MAX
) -> int:
    if nav_length == 0:
        return 0
    num_steps = nav_length // TICKS_PER_16TH_STEP
    if num_steps == 0:
        return 0
    span = pitch_max - pitch_min
    step = 0
    if span != 0:
        step = ((pitch_value - pitch_min) * (num_steps - 1)) // span
    if step < 0:
        step = 0
    if step >= num_steps:
        step = num_steps - 1
    return step * TICKS_PER_16TH_STEP


def select_stacked_note_index_from_pitchbend(
    pitch_value: int,
    nav_length: int,
    stacked_count: int,
    *,
    pitch_min: int = PITCHBEND_MIN,
    pitch_max: int = PITCHBEND_MAX,
) -> int:
    if stacked_count <= 1:
        return 0
    num_steps = nav_length // TICKS_PER_16TH_STEP
    if num_steps <= 1:
        return 0
    span = pitch_max - pitch_min
    if span == 0:
        return 0
    pos = (pitch_value - pitch_min) * (num_steps - 1)
    rem = pos % span
    if rem < 0:
        rem += span
    idx = (rem * stacked_count) // span
    if idx >= stacked_count:
        return stacked_count - 1
    return idx


def pitchbend_for_step_and_stack(
    step: int,
    nav_length: int,
    stacked_index: int,
    stacked_count: int,
    *,
    pitch_min: int = PITCHBEND_MIN,
    pitch_max: int = PITCHBEND_MAX,
) -> int:
    target_tick = step * TICKS_PER_16TH_STEP
    search_max = min(pitch_max, MIDI_PITCHBEND_MAX)
    for pitch in range(pitch_min, search_max + 1):
        if select_tick_from_pitchbend(pitch, nav_length, pitch_min=pitch_min, pitch_max=pitch_max) != target_tick:
            continue
        if (
            select_stacked_note_index_from_pitchbend(
                pitch,
                nav_length,
                stacked_count,
                pitch_min=pitch_min,
                pitch_max=pitch_max,
            )
            == stacked_index
        ):
            return pitch
    raise ValueError(
        f"no pitchbend for step={step} stacked={stacked_index}/{stacked_count} nav={nav_length}"
    )


def parse_select_apply(line: str) -> Optional[tuple[int, int, int]]:
    match = SELECT_APPLY_RE.search(line)
    if match is None:
        return None
    return int(match.group(1)), int(match.group(2)), int(match.group(3))


def ticks_share_sixteenth(left: int, right: int) -> bool:
    return (left // TICKS_PER_16TH_STEP) == (right // TICKS_PER_16TH_STEP)


def select_apply_lands_on_step(
    selected_tick: int,
    note_idx: int,
    applied: int,
    expected_tick: int,
    *,
    want_note: bool = True,
) -> bool:
    """F1 aims at a grid tick; apply may report a note start in that sixteenth."""
    if not ticks_share_sixteenth(selected_tick, expected_tick):
        return False
    if want_note:
        return note_idx >= 0
    return note_idx < 0 and applied == 1


def _note_edit_entered(lines: list[str]) -> bool:
    if any(NOTE_EDIT_OPEN_RE.search(line) for line in lines):
        return True
    return any("entered note edit mode" in line for line in lines) or any(
        "Edit session: NOTE_EDIT" in line for line in lines
    )


def _move_action_index(lines: list[str]) -> Optional[int]:
    for index, line in enumerate(lines):
        match = EDIT_SESSION_ACTION_RE.search(line)
        if match is not None and int(match.group(1)) == MOVE_NOTE_TYPE:
            return index
    return None


def _last_apply_at_tick(lines: list[str], tick: int, *, after_index: int) -> Optional[int]:
    last: Optional[int] = None
    for index, line in enumerate(lines):
        if index <= after_index:
            continue
        parsed = parse_select_apply(line)
        if parsed is None:
            continue
        selected_tick, _note_idx, applied = parsed
        if applied == 1 and ticks_share_sixteenth(selected_tick, tick):
            last = index
    return last


def _index_in_ranges(index: int, ranges: list[tuple[int, int]]) -> bool:
    return any(start <= index < end for start, end in ranges)


def _playing_index_ranges(lines: list[str]) -> list[tuple[int, int]]:
    ranges: list[tuple[int, int]] = []
    start: Optional[int] = None
    for index, line in enumerate(lines):
        match = ST_RE.search(line)
        if match is None:
            continue
        from_state = match.group(2).strip()
        to_state = match.group(3).strip()
        if to_state == "PLAYING" and start is None:
            start = index
        elif start is not None and from_state == "PLAYING" and to_state != "PLAYING":
            ranges.append((start, index))
            start = None
    if start is not None:
        ranges.append((start, len(lines)))
    return ranges


def _next_midi_gap_us(lines: list[str], after_index: int) -> Optional[int]:
    for line in lines[after_index + 1 : after_index + 81]:
        match = MIDI_GAP_RE.search(line)
        if match is not None:
            return int(match.group(2))
    return None


def _case_playing_motor_value_only(lines: list[str]) -> dict[str, Any]:
    ranges = _playing_index_ranges(lines)
    judged = 0
    timed_burst = 0
    midi_gap_over = 0
    for index, line in enumerate(lines):
        if not ranges or not _index_in_ranges(index, ranges):
            continue
        match = MOTOR_SYNC_SEQUENCE_RE.search(line)
        if match is None:
            continue
        judged += 1
        if "timed_burst" in match.group(1):
            timed_burst += 1
        gap = _next_midi_gap_us(lines, index)
        if gap is not None and gap >= MAX_PLAYING_MOTOR_MIDI_GAP_US:
            midi_gap_over += 1
    issues: list[str] = []
    if timed_burst:
        issues.append(f"playing_motor_timed_burst={timed_burst}")
    if midi_gap_over:
        issues.append(f"playing_motor_midi_gap={midi_gap_over}")
    return {
        "ok": not issues,
        "issues": issues,
        "playing_motor_sync_judged": judged,
        "playing_motor_timed_burst": timed_burst,
        "playing_motor_midi_gap_over": midi_gap_over,
    }


def _case_select_apply_highlight_follows(lines: list[str]) -> dict[str, Any]:
    apply_count = 0
    for line in lines:
        parsed = parse_select_apply(line)
        if parsed is None:
            continue
        _tick, note_idx, applied = parsed
        if applied == 1 and note_idx >= 0:
            apply_count += 1
    dnte_count = sum(1 for line in lines if DNTE_RE.search(line))
    issues: list[str] = []
    if apply_count >= SELECT_APPLY_HIGHLIGHT_MIN_APPLIES and dnte_count * 2 < apply_count:
        issues.append(f"select_apply_highlight_starved={dnte_count}/{apply_count}")
    return {
        "ok": not issues,
        "issues": issues,
        "select_apply_note_changed": apply_count,
        "dnte_count": dnte_count,
    }


def _case_moved_note_kept_after_deselect(lines: list[str], empty_idx: int) -> dict[str, Any]:
    before: Optional[int] = None
    after: Optional[int] = None
    for index, line in enumerate(lines):
        match = VCACHE_FULL_NOTES_RE.search(line)
        if match is None:
            continue
        notes = int(match.group(1))
        if index <= empty_idx:
            before = notes
        elif after is None:
            after = notes
    issues: list[str] = []
    if before is not None and after is not None and after < before:
        issues.append(f"moved_note_dropped_from_vcache={before}->{after}")
    return {
        "ok": not issues,
        "issues": issues,
        "vcache_notes_before_deselect": before,
        "vcache_notes_after_deselect": after,
    }


def _applied_note_idxs_at_tick(
    lines: list[str], tick: int, *, after_index: int
) -> list[int]:
    idxs: list[int] = []
    for index, line in enumerate(lines):
        if index <= after_index:
            continue
        parsed = parse_select_apply(line)
        if parsed is None:
            continue
        selected_tick, note_idx, applied = parsed
        if applied == 1 and ticks_share_sixteenth(selected_tick, tick) and note_idx >= 0:
            idxs.append(note_idx)
    return idxs


def verify_note_edit_reselect_stacked(
    lines: list[str],
    args: object,
    *,
    dest_tick: int = 192,
    empty_tick: int = 48,
) -> dict[str, Any]:
    """Require a move, a deselect, then two different notes selectable at dest_tick."""
    del args
    issues: list[str] = []
    entered = _note_edit_entered(lines)
    if not entered:
        issues.append("note_edit_entered")
    move_index = _move_action_index(lines)
    if move_index is None:
        issues.append("selected_note_moved")
    after = move_index if move_index is not None else -1
    empty_idx = _last_apply_at_tick(lines, empty_tick, after_index=after)
    if empty_idx is None:
        issues.append("deselect_empty_step")
        empty_idx = after
    dest_idxs = _applied_note_idxs_at_tick(lines, dest_tick, after_index=empty_idx)
    distinct = sorted(set(dest_idxs))
    stacked_ok = len(distinct) >= 2
    if not stacked_ok:
        issues.append("stacked_reselect_after_deselect")
    motor = _case_playing_motor_value_only(lines)
    issues.extend(motor["issues"])
    held = _case_value_only_notegate_held(lines)
    issues.extend(held["issues"])
    highlight = _case_select_apply_highlight_follows(lines)
    issues.extend(highlight["issues"])
    kept = _case_moved_note_kept_after_deselect(lines, empty_idx)
    issues.extend(kept["issues"])
    return {
        "ok": not issues,
        "issues": issues,
        "note_edit_entered": entered,
        "move_index": move_index,
        "empty_select_index": empty_idx,
        "dest_note_idxs": dest_idxs,
        "dest_distinct_note_idxs": distinct,
        "dest_tick": dest_tick,
        "empty_tick": empty_tick,
        "playing_motor_value_only": motor,
        "value_only_notegate_held": held,
        "select_apply_highlight_follows": highlight,
        "moved_note_kept_after_deselect": kept,
    }
