"""Serial verification for overlay list scroll and dirty-prompt cancel confirm."""

from __future__ import annotations

import re
from typing import Optional

from hitl.verify.load_save_display import extract_load_save_mode_events, last_load_save_mode_active

_OVLY_SEL_RE = re.compile(r"#CAP,\d+,OVLY,sel,(\d+),(\d+)\b")
_OVLY_CONFIRM_RE = re.compile(r"#CAP,\d+,OVLY,confirm,(\d+),(\d+)\b")
_REV_LOAD_DIRTY_PROMPT_RE = re.compile(
    r"#CAP,\d+,PERS,rev_load_dirty_prompt,\d+,\d+,\d+,shown\b"
)
_REV_LOAD_DIRTY_CANCEL_RE = re.compile(
    r"#CAP,\d+,PERS,rev_load_dirty_cancel,\d+,\d+,\d+,cancel\b"
)
_REV_LOAD_COMPLETE_RE = re.compile(
    r"#CAP,\d+,PERS,rev_load_complete,\d+,\d+,\d+,S\d{4}_v\d{4}\b"
)


def extract_overlay_selection_events(lines: list[str]) -> list[tuple[int, int]]:
    events: list[tuple[int, int]] = []
    for line in lines:
        match = _OVLY_SEL_RE.search(line)
        if match is None:
            continue
        events.append((int(match.group(1)), int(match.group(2))))
    return events


def extract_overlay_confirm_events(lines: list[str]) -> list[tuple[int, int]]:
    events: list[tuple[int, int]] = []
    for line in lines:
        match = _OVLY_CONFIRM_RE.search(line)
        if match is None:
            continue
        events.append((int(match.group(1)), int(match.group(2))))
    return events


def _last_index_matching(lines: list[str], pattern: re.Pattern[str], start: int = 0) -> Optional[int]:
    last: Optional[int] = None
    for i in range(start, len(lines)):
        if pattern.search(lines[i]):
            last = i
    return last


def verify_load_save_overlay_scroll(lines: list[str], args: object) -> dict[str, object]:
    issues: list[str] = []
    scroll_only = bool(getattr(args, "overlay_scroll_only", False))

    root_anchor = int(getattr(args, "overlay_root_scroll_anchor", 0) or 0)
    dirty_load_anchor = int(getattr(args, "overlay_dirty_load_anchor", 0) or 0)
    dirty_scroll_anchor = int(getattr(args, "overlay_dirty_scroll_anchor", 0) or 0)
    dirty_confirm_anchor = int(getattr(args, "overlay_dirty_confirm_anchor", 0) or 0)

    root_sel = extract_overlay_selection_events(lines[root_anchor:])
    dirty_sel: list[tuple[int, int]] = []
    confirm_events: list[tuple[int, int]] = []

    if not any(mode == 0 and row == 1 for mode, row in root_sel):
        issues.append("missing_root_scroll_to_current_row_ovly_sel_0_1")
    if not any(mode == 0 and row == 0 for mode, row in root_sel):
        issues.append("missing_root_scroll_back_to_save_row_ovly_sel_0_0")

    if not scroll_only:
        dirty_sel = extract_overlay_selection_events(
            lines[dirty_scroll_anchor : dirty_confirm_anchor or len(lines)]
        )

        if _last_index_matching(lines, _REV_LOAD_DIRTY_PROMPT_RE, dirty_load_anchor) is None:
            issues.append("missing_rev_load_dirty_prompt")

        if not any(mode == 1 and row == 2 for mode, row in dirty_sel):
            issues.append("missing_dirty_prompt_scroll_to_cancel_row_ovly_sel_1_2")

        confirm_events = extract_overlay_confirm_events(lines[dirty_confirm_anchor:])
        if not any(mode == 1 and row == 2 for mode, row in confirm_events):
            issues.append("missing_dirty_prompt_confirm_on_cancel_row")

        if _last_index_matching(lines, _REV_LOAD_DIRTY_CANCEL_RE, dirty_confirm_anchor) is None:
            issues.append("missing_rev_load_dirty_cancel_after_confirm")

        if _last_index_matching(lines, _REV_LOAD_COMPLETE_RE, dirty_confirm_anchor) is not None:
            issues.append("unexpected_rev_load_complete_after_cancel")

    ldsv = extract_load_save_mode_events(lines)
    if ldsv and ldsv[-1] != 0:
        issues.append("load_save_overlay_still_active_at_end")

    return {
        "ok": not issues,
        "issues": issues,
        "scroll_only": scroll_only,
        "root_overlay_selection_events": root_sel,
        "dirty_overlay_selection_events": dirty_sel,
        "overlay_confirm_events": confirm_events,
        "ldsv_events": ldsv,
        "last_ldsv": last_load_save_mode_active(lines),
    }
