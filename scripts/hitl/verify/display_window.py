"""Serial verification for long-loop bounded display window + play/stop follow."""

from __future__ import annotations

from typing import Optional

TICKS_PER_BAR = 768
BOUNDED_WINDOW_MIN_BARS = 16
DEFAULT_WINDOW_BARS = 16


def extract_disp_window_rows(lines: list[str]) -> list[dict[str, object]]:
    """Parse #CAP DISP lines that include windowStartTick, windowBars, windowNoteCount."""
    rows: list[dict[str, object]] = []
    for line_index, line in enumerate(lines):
        if ",DISP," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 14:
            continue
        try:
            loop_len = int(parts[5])
            rows.append(
                {
                    "line_index": line_index,
                    "timestamp": int(parts[1]),
                    "slot": int(parts[3]),
                    "state": parts[4],
                    "loop_len": loop_len,
                    "frame_notes": int(parts[8]),
                    "window_start": int(parts[11]),
                    "window_bars": int(parts[12]),
                    "window_note_count": int(parts[13]),
                }
            )
        except ValueError:
            continue
    return rows


def _line_index(lines: list[str], needle: str, *, after: Optional[int] = None) -> Optional[int]:
    for i, line in enumerate(lines):
        if after is not None and i <= after:
            continue
        if needle in line:
            return i
    return None


def _rows_after_line(rows: list[dict[str, object]], line_index: int) -> list[dict[str, object]]:
    return [row for row in rows if int(row["line_index"]) > line_index]


def _rows_between(rows: list[dict[str, object]], start: int, end: Optional[int]) -> list[dict[str, object]]:
    out: list[dict[str, object]] = []
    for row in rows:
        idx = int(row["line_index"])
        if idx <= start:
            continue
        if end is not None and idx >= end:
            break
        out.append(row)
    return out


def verify_long_loop_display_window(lines: list[str], args: object) -> dict[str, object]:
    issues: list[str] = []
    bounded_threshold = BOUNDED_WINDOW_MIN_BARS * TICKS_PER_BAR
    record_bars = int(getattr(args, "record_bars", 24) or 24)
    expected_loop_len = record_bars * TICKS_PER_BAR

    rows = extract_disp_window_rows(lines)
    long_loop_rows = [row for row in rows if int(row["loop_len"]) > bounded_threshold]

    if not long_loop_rows:
        issues.append("missing_bounded_window_disp_rows")

    enter_idx = _line_index(lines, "entered note edit mode")
    if enter_idx is None:
        issues.append("missing_enter_note_edit")

    snap_idx = _line_index(lines, "Detailed window centered on playhead", after=enter_idx)
    if snap_idx is None:
        issues.append("missing_play_stop_long_press_snap_log")

    if enter_idx is not None:
        freeze_rows = _rows_between(rows, enter_idx, snap_idx)
        if len(freeze_rows) < 3:
            issues.append(f"insufficient_disp_during_note_edit_freeze count={len(freeze_rows)}")
        else:
            freeze_starts = {int(row["window_start"]) for row in freeze_rows}
            if len(freeze_starts) != 1:
                issues.append(
                    f"window_not_frozen_during_note_edit unique_starts={sorted(freeze_starts)}"
                )
            for row in freeze_rows:
                window_bars = int(row["window_bars"])
                if window_bars < 1 or window_bars > DEFAULT_WINDOW_BARS:
                    issues.append(f"window_bars_out_of_range value={window_bars}")
                    break

    if snap_idx is not None and enter_idx is not None:
        freeze_rows = _rows_between(rows, enter_idx, snap_idx)
        after_snap_rows = _rows_after_line(rows, snap_idx)
        if freeze_rows and after_snap_rows:
            last_freeze_start = int(freeze_rows[-1]["window_start"])
            first_after_snap = int(after_snap_rows[0]["window_start"])
            if last_freeze_start == first_after_snap:
                issues.append(
                    "long_press_snap_did_not_move_window "
                    f"start={last_freeze_start}"
                )

    hold_rows: list[dict[str, object]] = []
    hold_start_idx = _line_index(lines, "phase:hold_track_start", after=snap_idx)
    hold_end_idx = _line_index(lines, "phase:hold_track_end", after=hold_start_idx)
    if hold_start_idx is not None:
        hold_rows = _rows_between(rows, hold_start_idx, hold_end_idx)
    elif snap_idx is not None:
        hold_rows = _rows_after_line(rows, snap_idx)[-40:]

    if len(hold_rows) < 3:
        issues.append(f"insufficient_disp_during_play_stop_hold count={len(hold_rows)}")
    else:
        hold_starts = [int(row["window_start"]) for row in hold_rows]
        if len(set(hold_starts)) < 2:
            issues.append(
                "play_stop_hold_did_not_track_playhead "
                f"unique_starts={sorted(set(hold_starts))}"
            )

    loop_len_ok = any(abs(int(row["loop_len"]) - expected_loop_len) <= TICKS_PER_BAR for row in long_loop_rows)
    if long_loop_rows and not loop_len_ok:
        observed = sorted({int(row["loop_len"]) for row in long_loop_rows})
        issues.append(f"unexpected_loop_len expected≈{expected_loop_len} observed={observed}")

    playing_rows = [row for row in long_loop_rows if str(row["state"]) == "PLAYING"]
    if not playing_rows:
        issues.append("missing_playing_bounded_window_disp")

    return {
        "ok": not issues,
        "issues": issues,
        "disp_window_row_count": len(rows),
        "long_loop_disp_row_count": len(long_loop_rows),
        "enter_note_edit_line": enter_idx,
        "snap_line": snap_idx,
        "hold_start_line": hold_start_idx,
        "hold_disp_rows": len(hold_rows),
        "expected_loop_len_ticks": expected_loop_len,
    }
