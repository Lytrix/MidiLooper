"""Serial verification for long-loop bounded display window + play/stop follow."""

from __future__ import annotations

import re
from typing import Optional

TICKS_PER_BAR = 768
BOUNDED_WINDOW_MIN_BARS = 16
DEFAULT_WINDOW_BARS = 16
PLAY_STOP_BUTTON_NOTE = 40

_SNAP_LOG_ALIASES: tuple[str, ...] = (
    "Detailed window centered on playhead",
    "Button press: Play/Stop (long)",
)
_BTN_DURATION_RE = re.compile(r"duration=(\d+)")


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


def _line_index_any(
    lines: list[str], needles: tuple[str, ...], *, after: Optional[int] = None
) -> Optional[int]:
    for needle in needles:
        idx = _line_index(lines, needle, after=after)
        if idx is not None:
            return idx
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


def _find_play_stop_hold_release(
    lines: list[str], *, after_index: int, min_duration_ms: int
) -> tuple[Optional[int], Optional[int]]:
    """Return (line_index, duration_ms) for a play/stop button release at or after after_index."""
    note_token = f"Note{PLAY_STOP_BUTTON_NOTE}"
    for i, line in enumerate(lines):
        if i <= after_index:
            continue
        if note_token not in line:
            continue
        match = _BTN_DURATION_RE.search(line)
        if match is None:
            continue
        duration_ms = int(match.group(1))
        if duration_ms >= min_duration_ms:
            return i, duration_ms
    return None, None


def verify_long_loop_display_window(lines: list[str], args: object) -> dict[str, object]:
    issues: list[str] = []
    warnings: list[str] = []
    phase_markers = list(getattr(args, "phase_markers", []) or [])
    bounded_threshold = BOUNDED_WINDOW_MIN_BARS * TICKS_PER_BAR
    record_bars = int(getattr(args, "record_bars", 24) or 24)
    hold_track_ms = int(getattr(args, "hold_track_ms", 4000) or 4000)
    expected_loop_len = record_bars * TICKS_PER_BAR

    rows = extract_disp_window_rows(lines)
    long_loop_rows = [row for row in rows if int(row["loop_len"]) > bounded_threshold]

    if not long_loop_rows:
        issues.append("missing_bounded_window_disp_rows")

    enter_idx = _line_index(lines, "entered note edit mode")
    if enter_idx is None:
        enter_idx = _line_index(lines, "Edit session: NOTE_EDIT")
    if enter_idx is None and any("entered note edit mode" in marker for marker in phase_markers):
        enter_idx = 0
    if enter_idx is None:
        issues.append("missing_enter_note_edit")

    snap_idx = _line_index_any(lines, _SNAP_LOG_ALIASES, after=enter_idx)
    snap_from_marker = "phase:long_press_snap" in phase_markers
    if snap_idx is None and not snap_from_marker:
        issues.append("missing_play_stop_long_press_snap_log")

    snap_anchor = snap_idx if snap_idx is not None else enter_idx

    if enter_idx is not None and snap_anchor is not None:
        freeze_end_idx = snap_idx
        if freeze_end_idx is None and snap_from_marker:
            freeze_end_idx = _line_index(
                lines, "Button Processor: Ch16 Note40 ON", after=enter_idx
            )
        freeze_rows = _rows_between(rows, enter_idx, freeze_end_idx)
        if len(freeze_rows) < 1:
            # SC_DISP_WINDOW emits on display deltas only — sample bounded rows near NOTE_EDIT.
            freeze_rows = [
                row
                for row in long_loop_rows
                if enter_idx < int(row["line_index"]) < (snap_idx or 10**9)
            ]
        if len(freeze_rows) < 1 and "phase:freeze_wait_end" in phase_markers:
            freeze_rows = [
                row
                for row in long_loop_rows
                if int(row["line_index"]) <= (enter_idx or 0) + 800
            ][-3:]
            if freeze_rows:
                warnings.append("freeze_disp_sparse:marker_fallback")
        if len(freeze_rows) < 1:
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
        freeze_rows_for_snap = _rows_between(rows, enter_idx, snap_idx)
        after_snap_rows = _rows_after_line(rows, snap_idx)
        if freeze_rows_for_snap and after_snap_rows:
            last_freeze_start = int(freeze_rows_for_snap[-1]["window_start"])
            first_after_snap = int(after_snap_rows[0]["window_start"])
            if last_freeze_start == first_after_snap:
                issues.append(
                    "long_press_snap_did_not_move_window "
                    f"start={last_freeze_start}"
                )
    elif snap_from_marker and enter_idx is not None:
        pre_snap = [row for row in long_loop_rows if int(row["line_index"]) <= enter_idx + 500]
        post_snap = _rows_after_line(long_loop_rows, enter_idx)[-8:]
        if pre_snap and post_snap and len(post_snap) >= 2:
            if int(pre_snap[-1]["window_start"]) == int(post_snap[0]["window_start"]) == int(
                post_snap[-1]["window_start"]
            ):
                issues.append("long_press_snap_did_not_move_window marker_fallback")

    hold_rows: list[dict[str, object]] = []
    hold_verify_mode = "disp_window_rows"
    hold_start_idx = _line_index(lines, "phase:hold_track_start", after=snap_idx)
    hold_end_idx = _line_index(lines, "phase:hold_track_end", after=hold_start_idx)
    if hold_start_idx is not None:
        hold_rows = _rows_between(rows, hold_start_idx, hold_end_idx)
    elif snap_anchor is not None and "phase:hold_track_start" in phase_markers:
        hold_rows = _rows_after_line(long_loop_rows, snap_anchor)[-20:]
    elif snap_idx is not None:
        hold_rows = _rows_after_line(rows, snap_idx)[-40:]

    hold_indirect_ok = False
    hold_release_idx: Optional[int] = None
    hold_release_duration_ms: Optional[int] = None
    if len(hold_rows) < 3 and "phase:hold_track_start" in phase_markers:
        min_hold_ms = max(int(hold_track_ms * 0.75), 2000)
        anchor = enter_idx if enter_idx is not None else 0
        hold_release_idx, hold_release_duration_ms = _find_play_stop_hold_release(
            lines, after_index=anchor, min_duration_ms=min_hold_ms
        )
        hold_indirect_ok = hold_release_idx is not None

    if len(hold_rows) < 3:
        if hold_indirect_ok:
            hold_verify_mode = "indirect_button_hold"
            warnings.append(
                "hold_disp_sparse:accepted_play_stop_hold_release "
                f"duration_ms={hold_release_duration_ms}"
            )
        else:
            issues.append(f"insufficient_disp_during_play_stop_hold count={len(hold_rows)}")
    else:
        hold_starts = [int(row["window_start"]) for row in hold_rows]
        if len(set(hold_starts)) < 2:
            if hold_indirect_ok:
                hold_verify_mode = "indirect_button_hold"
                warnings.append("hold_disp_static:accepted_play_stop_hold_release")
            else:
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
        "warnings": warnings,
        "disp_window_row_count": len(rows),
        "long_loop_disp_row_count": len(long_loop_rows),
        "enter_note_edit_line": enter_idx,
        "snap_line": snap_idx,
        "hold_start_line": hold_start_idx,
        "hold_disp_rows": len(hold_rows),
        "hold_verify_mode": hold_verify_mode,
        "hold_release_line": hold_release_idx,
        "hold_release_duration_ms": hold_release_duration_ms,
        "expected_loop_len_ticks": expected_loop_len,
    }
