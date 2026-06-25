"""Serial verification: record + two overdub passes + full undo chain to EMPTY + redo restore."""

from __future__ import annotations

from typing import Optional

REQUIRED_OVERDUB_PASSES = 2
# Record + two overdubs on the global stack; undo all three to reach EMPTY.
REQUIRED_UNDO_STEPS = REQUIRED_OVERDUB_PASSES + 1
REQUIRED_REDO_STEPS = REQUIRED_UNDO_STEPS


def _count_substring(lines: list[str], needle: str) -> int:
    return sum(1 for line in lines if needle in line)


def _count_transition(lines: list[str], from_state: str, to_state: str) -> int:
    marker = ",ST,Track,"
    count = 0
    for line in lines:
        if marker not in line:
            continue
        tail = line.split(marker, 1)[1]
        parts = tail.split(",")
        if len(parts) < 2:
            continue
        if parts[0].strip() == from_state and parts[1].strip() == to_state:
            count += 1
    return count


def _first_disp_row_after(lines: list[str], start_index: int) -> Optional[dict[str, int]]:
    for i, line in enumerate(lines):
        if i <= start_index or ",DISP," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 9:
            continue
        try:
            return {
                "line_index": i,
                "frame_notes": int(parts[8]),
                "window_note_count": int(parts[13]) if len(parts) >= 14 else int(parts[8]),
            }
        except ValueError:
            continue
    return None


def _undo_redo_order_ok(lines: list[str], *, undo_steps: int, redo_steps: int) -> bool:
    undo_indices: list[int] = []
    redo_indices: list[int] = []
    for i, line in enumerate(lines):
        if "Overdub undone" in line:
            undo_indices.append(i)
        if "Overdub redone" in line:
            redo_indices.append(i)
    if len(undo_indices) < undo_steps or len(redo_indices) < redo_steps:
        return False
    tail_undo = undo_indices[-undo_steps:]
    tail_redo = redo_indices[-redo_steps:]
    return max(tail_undo) < min(tail_redo)


def verify_two_overdub_undo_redo(lines: list[str], args: object) -> dict[str, object]:
    overdub_passes = int(getattr(args, "overdub_passes", REQUIRED_OVERDUB_PASSES))
    undo_steps = int(getattr(args, "undo_steps", overdub_passes + 1))
    redo_steps = int(getattr(args, "redo_steps", undo_steps))
    issues: list[str] = []

    undo_count = _count_substring(lines, "Overdub undone")
    redo_count = _count_substring(lines, "Overdub redone")
    if undo_count < undo_steps:
        issues.append(f"undo_log_short:{undo_count}<{undo_steps}")
    if redo_count < redo_steps:
        issues.append(f"redo_log_short:{redo_count}<{redo_steps}")

    overdub_starts = _count_transition(lines, "PLAYING", "OVERDUBBING")
    overdub_stops = _count_transition(lines, "OVERDUBBING", "PLAYING")
    if overdub_starts < overdub_passes:
        issues.append(f"overdub_start_short:{overdub_starts}<{overdub_passes}")
    if overdub_stops < overdub_passes:
        issues.append(f"overdub_stop_short:{overdub_stops}<{overdub_passes}")

    if not _undo_redo_order_ok(lines, undo_steps=undo_steps, redo_steps=redo_steps):
        issues.append("undo_redo_order_invalid")

    cannot_redo = _count_substring(lines, "Cannot redo overdub right now")
    if cannot_redo > 0:
        issues.append(f"cannot_redo_logged:{cannot_redo}")

    undo_indices = [i for i, line in enumerate(lines) if "Overdub undone" in line]
    if len(undo_indices) >= undo_steps:
        disp_after_undo = _first_disp_row_after(lines, undo_indices[undo_steps - 1])
        if disp_after_undo is None:
            issues.append("disp_missing_after_undo_chain")
        elif disp_after_undo["frame_notes"] != 0:
            issues.append(f"display_not_empty_after_undo_chain:frame_notes={disp_after_undo['frame_notes']}")

    redo_indices = [i for i, line in enumerate(lines) if "Overdub redone" in line]
    if len(redo_indices) >= redo_steps:
        disp_after_redo = _first_disp_row_after(lines, redo_indices[redo_steps - 1])
        if disp_after_redo is None:
            issues.append("disp_missing_after_redo_chain")
        elif disp_after_redo["frame_notes"] <= 0:
            issues.append("display_empty_after_redo_chain")

    return {
        "ok": not issues,
        "issues": issues,
        "undo_log_count": undo_count,
        "redo_log_count": redo_count,
        "overdub_start_count": overdub_starts,
        "overdub_stop_count": overdub_stops,
        "required_overdub_passes": overdub_passes,
        "required_undo_steps": undo_steps,
        "required_redo_steps": redo_steps,
    }
