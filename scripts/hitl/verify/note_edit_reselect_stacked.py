"""Serial cases for NOTE_EDIT reselect of a moved note on a stacked sixteenth."""

from __future__ import annotations

import re
from typing import Any, Optional

from hitl.control_constants import TICKS_PER_16TH_STEP
from hitl.verify.note_edit_overdub_compose import _case_value_only_notegate_held

SELECT_APPLY_RE = re.compile(
    r"select_apply selected_tick=(\d+) note_idx=(-?\d+) .* apply=(\d+)"
)
# The device prints these two select proofs **straight to USB serial** (`SelectFaderInput.cpp`,
# lines 331/334 and 416), from exactly the branches that log the deferred `#DBG select_apply …`
# row. A live wait has to use them: `Logger.cpp` queues every `#DBG` line into the capture ring,
# the timing-critical (PLAYING) flush transmits nothing, and `!TRACE_DRAIN` is transport-gated --
# so while the target plays the `#DBG` row reaches the log only in the post-stop drain.
DIRECT_NOTE_SELECT_RE = re.compile(r"Select fader: selected note (\d+) at tick (\d+)")
DIRECT_EMPTY_SELECT_RE = re.compile(r"Select fader: selected empty step at tick (\d+)")
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

FOCUS_NOTE_RE = re.compile(r"NOTE_EDIT focus rebuild: noteId=(\d+)")
FOCUS_GEOMETRY_RE = re.compile(
    r"NOTE_EDIT focus rebuild: noteId=(\d+) presence=(-?\d+) rowProjectsToStore=([01]) "
    r"pitch=(\d+) start=(\d+) end=(\d+)"
)
MOVE_GEOMETRY_RE = re.compile(r"Note movement with overlap handling: from=(\d+) to=(\d+)")
MOVE_ACTION_RE = re.compile(r"EditSessionAction: type=3 noteId=(\d+) start=(\d+) end=(\d+) pitch=(\d+)")
SELECT_NOTE_GEOMETRY_RE = re.compile(r"#CAP,(\d+),DNTE,(\d+),(\d+),(\d+),(\d+),(-?\d+)")
ROW_CLOCK_RE = re.compile(r"#CAP,(\d+),|^\[(\d+)\.(\d+)\]")


def _row_us(line: str) -> Optional[int]:
    match = ROW_CLOCK_RE.search(line)
    if match is None:
        return None
    if match.group(1) is not None:
        return int(match.group(1))
    return int(match.group(2)) * 1000000 + int((match.group(3) + "000000")[:6])


def _qualified_move(lines: list[str], source_tick: int, dest_tick: int,
                    source_pitch: int) -> Optional[tuple[int, int]]:
    """Require actual source focus and intended geometry, not any Move in the log."""
    focus = movement = None
    for index, line in enumerate(lines):
        selected = FOCUS_NOTE_RE.search(line)
        if selected:
            focus = int(selected.group(1))
        trace = MOVE_GEOMETRY_RE.search(line)
        if trace:
            movement = tuple(map(int, trace.groups()))
        action = MOVE_ACTION_RE.search(line)
        if action:
            note_id, start, end, pitch = map(int, action.groups())
            if (movement == (source_tick, dest_tick) and focus == note_id and
                    note_id > 0 and start == dest_tick and end > start and pitch == source_pitch):
                return index, note_id
            movement = None
    return None


def _optional_result(case: dict[str, Any], measured: bool) -> dict[str, Any]:
    case = dict(case)
    case["status"] = "FAIL" if case["issues"] else ("PASS" if measured else "UNMEASURED")
    case["ok"] = False if case["issues"] else (True if measured else None)
    return case

# Compatibility reexports; generation and verification share the same logical mapping.
from hitl.select_mapping import (
    PITCHBEND_MIN, PITCHBEND_MAX, MIDI_PITCHBEND_MAX,
    select_tick_from_pitchbend, select_stacked_note_index_from_pitchbend,
    pitchbend_for_step_and_stack,
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
        return note_idx >= 0 and applied == 1
    return note_idx == -1 and applied == 1


def parse_direct_select(line: str) -> Optional[tuple[int, int]]:
    """`(note_idx, tick)` from the device's direct select proof; `-1` means the empty step.

    The row only exists when the select was **applied**: both variants are printed from the apply
    branch, so `note_idx == -1` here is the deselect (`reason=empty_step`) event itself, not the
    `apply=0` repeat of an unchanged selection.
    """
    match = DIRECT_EMPTY_SELECT_RE.search(line)
    if match is not None:
        return -1, int(match.group(1))
    match = DIRECT_NOTE_SELECT_RE.search(line)
    if match is not None:
        return int(match.group(1)), int(match.group(2))
    return None


def direct_select_lands_on_step(
    note_idx: int,
    tick: int,
    expected_tick: int,
    *,
    want_note: bool = True,
) -> bool:
    """The direct row names the select the fixture aimed at, on the intended sixteenth."""
    if not ticks_share_sixteenth(tick, expected_tick):
        return False
    return note_idx >= 0 if want_note else note_idx == -1


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


def applied_selects(lines: list[str], *, after_index: int = -1) -> list[tuple[int, int, int]]:
    """One occurrence per select, not both immediate and delayed twin rows.

    Direct rows are the authoritative occurrence channel when present. Untagged
    #DBG-only rows remain diagnostic; arrival order cannot qualify their chronology.
    """
    direct, deferred = [], []
    for index, line in enumerate(lines):
        if index <= after_index:
            continue
        parsed = parse_direct_select(line)
        if parsed is not None:
            direct.append((index, parsed[1], parsed[0]))
            continue
        parsed = parse_select_apply(line)
        if parsed is not None and parsed[2] == 1:
            deferred.append((index, parsed[0], parsed[1]))
    events = direct if direct else deferred
    if events and all(_row_us(lines[event[0]]) is not None for event in events):
        events.sort(key=lambda event: (_row_us(lines[event[0]]), event[0]))
    return events


def _selection_identity(lines: list[str], index: int, next_index: int,
                        note_idx: int) -> tuple[Optional[int], set[tuple[int, int]]]:
    """Bind focus ID and geometry to this select, not a later reselect."""
    event_us = _row_us(lines[index])
    end_us = _row_us(lines[next_index]) if next_index < len(lines) else None
    if event_us is None:
        return None, set()
    ids, geometry, current_geometry = set(), set(), set()
    for line in lines[index + 1:next_index]:
        stamp = _row_us(line)
        if stamp is None or stamp < event_us or (end_us is not None and stamp >= end_us):
            continue
        focus = FOCUS_NOTE_RE.search(line)
        if focus and int(focus.group(1)) > 0:
            ids.add(int(focus.group(1)))
        current = FOCUS_GEOMETRY_RE.search(line)
        if current:
            note_id, presence, projects, pitch, start, end = map(int, current.groups())
            if note_id <= 0 or presence != 0 or projects != 1 or not 0 <= pitch <= 127 or end <= start:
                return None, set()
            current_geometry.add((pitch, start))
        note = SELECT_NOTE_GEOMETRY_RE.search(line)
        if note and int(note.group(6)) == note_idx and int(note.group(5)) > 0:
            geometry.add((int(note.group(2)), int(note.group(3))))
    # Focus is keyed by the current NoteId. DNTE's mutable display index can be
    # reused by a later inventory, so it is compatibility evidence only when no
    # current focus geometry was observed. Conflicting direct rows still refuse.
    return (next(iter(ids)) if len(ids) == 1 else None), current_geometry or geometry


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
    # Counted from `applied_selects`, not from the `#DBG` family alone: on a PLAYING leg's own
    # snapshot the applied note selects are the direct rows, and a counter that reads one channel
    # would silently judge nothing (the starvation rule below only fires above a real count).
    apply_count = sum(1 for _index, _tick, note_idx in applied_selects(lines) if note_idx >= 0)
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
    for _index, selected_tick, note_idx in applied_selects(lines, after_index=after_index):
        if ticks_share_sixteenth(selected_tick, tick) and note_idx >= 0:
            idxs.append(note_idx)
    return idxs


def verify_note_edit_reselect_stacked(
    lines: list[str], args: object, *, dest_tick: int = 192, empty_tick: int = 48,
    source_tick: int = 8 * TICKS_PER_16TH_STEP, source_pitch: int = 67,
    dest_pitch: int = 64, dest_note_tick: Optional[int] = None,
    after_index: Optional[int] = None,
) -> dict[str, Any]:
    """Qualify intended Move -> applied empty deselect -> both expected notes.

    Bind current focus NoteId to the fixture's pitch/start, not mutable UI index.
    Core proof and optional timing/retention qualification are separate outcomes.
    """
    del args
    if after_index is None:
        entries = [index for index, line in enumerate(lines) if 'Edit session: NOTE_EDIT' in line]
        after_index = entries[-1] if entries else 0
    if not isinstance(after_index, int) or not 0 <= after_index <= len(lines):
        return {"ok": False, "verdict": "INCONCLUSIVE_CAPTURE",
                "issues": ["invalid_edit_evidence_window"], "qualification_complete": False}
    window_start = after_index
    lines = lines[window_start:]
    movements = {line for line in lines if MOVE_GEOMETRY_RE.search(line)}
    if len(movements) > 1:
        return {"ok": False, "verdict": "INCONCLUSIVE_CAPTURE",
                "issues": ["ambiguous_edit_replay_window"], "qualification_complete": False}
    if dest_note_tick is None:
        dest_note_tick = dest_tick
    issues: list[str] = []
    entered = _note_edit_entered(lines)
    if not entered:
        issues.append("note_edit_entered")
    # Coarse position editing preserves the source's within-sixteenth offset.
    move_tick = dest_tick + source_tick % TICKS_PER_16TH_STEP
    move = _qualified_move(lines, source_tick, move_tick, source_pitch)
    move_index, moved_note_id = move if move is not None else (-1, None)
    if move is None:
        issues.append("selected_note_moved")
    move_us = _row_us(lines[move_index]) if move_index >= 0 else None
    events = applied_selects(lines, after_index=move_index)
    qualified = [event for event in events if move_us is not None and
                 (stamp := _row_us(lines[event[0]])) is not None and stamp > move_us]
    empty = next((event for event in qualified if event[2] == -1 and
                  ticks_share_sixteenth(event[1], empty_tick)), None)
    if empty is None:
        issues.append("deselect_empty_step")
    empty_idx = empty[0] if empty else move_index
    empty_us = _row_us(lines[empty_idx]) if empty else None
    dest_idxs = []
    selected_ids = set()
    moved_seen = original_seen = False
    for position, (index, tick, note_idx) in enumerate(events):
        stamp = _row_us(lines[index])
        if (empty_us is None or stamp is None or stamp <= empty_us or
                note_idx < 0 or not ticks_share_sixteenth(tick, dest_tick)):
            continue
        dest_idxs.append(note_idx)
        next_index = events[position + 1][0] if position + 1 < len(events) else len(lines)
        if next_index <= index:
            continue  # ambiguous arrival order cannot bind another select's ID
        note_id, geometry = _selection_identity(lines, index, next_index, note_idx)
        if note_id is None:
            continue
        selected_ids.add(note_id)
        if note_id == moved_note_id and geometry == {(source_pitch, move_tick)}:
            moved_seen = True
        if note_id != moved_note_id and geometry == {(dest_pitch, dest_note_tick)}:
            original_seen = True
    if not (moved_seen and original_seen and len(selected_ids) >= 2):
        issues.extend(["stacked_reselect_after_deselect", "stacked_note_identity_unproven"])
    motor = _case_playing_motor_value_only(lines)
    motor = _optional_result(motor, motor["playing_motor_sync_judged"] > 0)
    held = _case_value_only_notegate_held(lines)
    held = _optional_result(held, held["value_only_notegate_held_judged"] > 0)
    highlight = _case_select_apply_highlight_follows(lines)
    highlight = _optional_result(highlight, highlight["select_apply_note_changed"] >=
                                 SELECT_APPLY_HIGHLIGHT_MIN_APPLIES and highlight["dnte_count"] > 0)
    kept = _case_moved_note_kept_after_deselect(lines, empty_idx)
    kept = _optional_result(kept, kept["vcache_notes_before_deselect"] is not None and
                            kept["vcache_notes_after_deselect"] is not None)
    optional = {"playing_motor_value_only": motor, "value_only_notegate_held": held,
                "select_apply_highlight_follows": highlight, "moved_note_kept_after_deselect": kept}
    for case in optional.values():
        issues.extend(case["issues"])
    unmeasured = [name for name, case in optional.items() if case["status"] == "UNMEASURED"]
    return {
        "ok": not issues, "issues": issues, "verdict": "PASS_CORE" if not issues else "FAIL",
        "qualification_complete": not issues and not unmeasured, "unmeasured_cases": unmeasured,
        "evidence_start_index": window_start,
        "note_edit_entered": entered, "move_index": move_index if move_index >= 0 else None,
        "moved_note_id": moved_note_id, "source_tick": source_tick, "move_tick": move_tick,
        "source_pitch": source_pitch, "dest_pitch": dest_pitch, "dest_note_tick": dest_note_tick,
        "empty_select_index": empty_idx, "dest_note_idxs": dest_idxs,
        "dest_distinct_note_idxs": sorted(set(dest_idxs)), "dest_note_ids": sorted(selected_ids),
        "dest_tick": dest_tick, "empty_tick": empty_tick, **optional,
    }
