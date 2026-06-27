"""Serial verification for load/save set browser overlay toggle."""

from __future__ import annotations

import re
from typing import Optional

_LDSV_RE = re.compile(r"#CAP,\d+,LDSV,([01])")
_GS_DOUBLE_PRESS_RE = re.compile(r"#CAP,\d+,GS,15,40,1\b")


def extract_load_save_mode_events(lines: list[str]) -> list[int]:
    values: list[int] = []
    for line in lines:
        match = _LDSV_RE.search(line)
        if match is None:
            continue
        values.append(int(match.group(1)))
    return values


def count_play_stop_double_press_gestures(lines: list[str]) -> int:
    return sum(1 for line in lines if _GS_DOUBLE_PRESS_RE.search(line))


def verify_load_save_display(lines: list[str], args: object) -> dict[str, object]:
    issues: list[str] = []
    min_double_presses = int(getattr(args, "min_double_presses", 2) or 2)

    ldsv = extract_load_save_mode_events(lines)
    double_press_gestures = count_play_stop_double_press_gestures(lines)

    enter_idx: Optional[int] = None
    exit_idx: Optional[int] = None
    for i, value in enumerate(ldsv):
        if value == 1 and enter_idx is None:
            enter_idx = i
        elif value == 0 and enter_idx is not None and exit_idx is None:
            exit_idx = i

    if enter_idx is None:
        issues.append("missing_load_save_enter_ldsv_1")
    if exit_idx is None:
        issues.append("missing_load_save_exit_ldsv_0")
    if enter_idx is not None and exit_idx is not None and exit_idx <= enter_idx:
        issues.append("load_save_exit_before_enter")

    if double_press_gestures < min_double_presses:
        issues.append(
            "insufficient_play_stop_double_press_gestures "
            f"count={double_press_gestures} min={min_double_presses}"
        )

    return {
        "ok": not issues,
        "issues": issues,
        "ldsv_events": ldsv,
        "enter_index": enter_idx,
        "exit_index": exit_idx,
        "play_stop_double_press_gestures": double_press_gestures,
    }
