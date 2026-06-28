"""Serial verification for MIDI load/save overlay Set load HITL."""

from __future__ import annotations

import re
from typing import Optional

from hitl.verify.load_save_display import (
    extract_load_save_mode_events,
    last_load_save_mode_active,
    load_save_overlay_exited_after_line,
)
from hitl.verify.revision_load import (
    _REV_LOAD_COMPLETE_RE,
    _REV_LOAD_DISPATCH_RE,
    _REV_LOAD_REQUEST_RE,
    _STORAGE_LOAD_COMPLETE_RE,
    _first_match_after,
)

_OVLY_CONFIRM_RE = re.compile(r"#CAP,\d+,OVLY,confirm,(\d+),(\d+)\b")
_REV_LOAD_DIRTY_PROMPT_RE = re.compile(
    r"#CAP,\d+,PERS,rev_load_dirty_prompt,\d+,\d+,\d+,shown\b"
)
_REV_LOAD_DIRTY_YES_RE = re.compile(
    r"#CAP,\d+,PERS,rev_load_dirty_yes,\d+,\d+,\d+,save_then_load\b"
)


def _anchor_index(args: object, name: str, default: int = -1) -> int:
    if not hasattr(args, name):
        return default
    value = getattr(args, name)
    if value is None:
        return default
    return int(value)


def verify_load_save_overlay_load(lines: list[str], args: object) -> dict[str, object]:
    issues: list[str] = []
    verify_offset = _anchor_index(args, "overlay_load_verify_offset", 0)
    enter_anchor = _anchor_index(args, "overlay_load_enter_anchor", -1) - verify_offset
    confirm_anchor = _anchor_index(args, "overlay_load_confirm_anchor", -1) - verify_offset
    expected_set_id = int(getattr(args, "expected_set_id", 1) or 1)
    expected_root_row = int(getattr(args, "overlay_target_root_row", 2) or 2)
    expected_revision_id: Optional[int] = getattr(args, "expected_revision_id", None)
    if expected_revision_id is not None:
        expected_revision_id = int(expected_revision_id)

    scoped = lines[verify_offset:] if verify_offset > 0 else lines

    ldsv = extract_load_save_mode_events(scoped)
    if 1 not in ldsv:
        issues.append("missing_load_save_enter_ldsv_1")

    if confirm_anchor >= 0:
        search_anchor = confirm_anchor
    elif enter_anchor >= 0:
        search_anchor = enter_anchor
    else:
        search_anchor = 0

    load_request_match = _first_match_after(scoped, _REV_LOAD_REQUEST_RE, search_anchor)
    load_dispatch_match = _first_match_after(scoped, _REV_LOAD_DISPATCH_RE, search_anchor)
    load_complete_match = _first_match_after(scoped, _REV_LOAD_COMPLETE_RE, search_anchor)
    storage_load_match = _first_match_after(scoped, _STORAGE_LOAD_COMPLETE_RE, search_anchor)
    dirty_prompt_match = _first_match_after(scoped, _REV_LOAD_DIRTY_PROMPT_RE, search_anchor)
    expect_dirty = bool(getattr(args, "expect_dirty_prompt", False))
    dirty_choice = str(getattr(args, "dirty_prompt_choice", "yes") or "yes").lower()

    set_row_confirm = _first_match_after(scoped, _OVLY_CONFIRM_RE, search_anchor)
    if set_row_confirm is not None:
        confirm_mode = int(set_row_confirm.group(1))
        confirm_row = int(set_row_confirm.group(2))
        if confirm_mode == 0 and confirm_row != expected_root_row:
            issues.append(
                f"ovly_confirm_wrong_row expected={expected_root_row} got={confirm_row}"
            )

    if dirty_prompt_match is not None:
        if not expect_dirty:
            issues.append("unexpected_rev_load_dirty_prompt")
        elif dirty_choice == "yes":
            if _first_match_after(scoped, _REV_LOAD_DIRTY_YES_RE, search_anchor) is None:
                issues.append("missing_rev_load_dirty_yes")
    elif expect_dirty:
        issues.append("missing_rev_load_dirty_prompt")

    if load_complete_match is not None:
        complete_line = load_complete_match.group(0)
        complete_line_index = next(
            (i for i, line in enumerate(scoped) if complete_line in line and i >= search_anchor),
            -1,
        )
        if complete_line_index < 0 or not load_save_overlay_exited_after_line(
            scoped, complete_line_index
        ):
            issues.append("load_save_overlay_still_active_after_rev_load_complete")

    if load_request_match is None:
        issues.append("missing_rev_load_request_after_overlay_confirm")
    if load_dispatch_match is None:
        issues.append("missing_rev_load_dispatch_after_overlay_confirm")
    if load_complete_match is None:
        issues.append("missing_rev_load_complete_cap_line")
    if storage_load_match is None:
        issues.append("missing_storage_manager_revision_load_complete_line")

    loaded_set_id: Optional[int] = None
    loaded_revision_id: Optional[int] = None
    if load_complete_match is not None and storage_load_match is not None:
        loaded_set_id = int(load_complete_match.group(3))
        loaded_revision_id = int(load_complete_match.group(4))
        log_set = int(storage_load_match.group(1))
        log_rev = int(storage_load_match.group(2))
        if loaded_set_id != log_set or loaded_revision_id != log_rev:
            issues.append(
                f"rev_load_complete_id_mismatch cap=S{loaded_set_id:04d}_v{loaded_revision_id:04d} "
                f"log=S{log_set}_v{log_rev}"
            )
        if loaded_set_id != expected_set_id:
            issues.append(
                f"loaded_set_id_mismatch expected=S{expected_set_id:04d} "
                f"got=S{loaded_set_id:04d}"
            )
        if expected_revision_id is not None and loaded_revision_id != expected_revision_id:
            issues.append(
                f"loaded_revision_id_mismatch expected=v{expected_revision_id:04d} "
                f"got=v{loaded_revision_id:04d}"
            )

    if load_request_match is not None:
        req_set = int(load_request_match.group(1))
        if req_set != expected_set_id:
            issues.append(
                f"rev_load_request_set_mismatch expected=S{expected_set_id:04d} got={req_set}"
            )

    return {
        "ok": not issues,
        "issues": issues,
        "last_ldsv": last_load_save_mode_active(scoped),
        "expected_set_id": expected_set_id,
        "expected_root_row": expected_root_row,
        "loaded_set_id": loaded_set_id,
        "loaded_revision_id": loaded_revision_id,
        "rev_load_complete": load_complete_match.group(0) if load_complete_match else None,
    }
