"""Serial verification for dirty-prompt revision load HITL."""

from __future__ import annotations

import re
from typing import Optional

from hitl.verify.revision_commit_save import _first_match_after

_REV_COMPLETE_RE = re.compile(
    r"#CAP,\d+,PERS,rev_complete,\d+,\d+,\d+,S(\d{4})_v(\d{4})\b"
)
_REV_LOAD_DIRTY_PROMPT_RE = re.compile(
    r"#CAP,\d+,PERS,rev_load_dirty_prompt,\d+,(\d+),(\d+),shown\b"
)
_REV_LOAD_DIRTY_YES_RE = re.compile(
    r"#CAP,\d+,PERS,rev_load_dirty_yes,\d+,(\d+),(\d+),save_then_load\b"
)
_REV_LOAD_DIRTY_NO_RE = re.compile(
    r"#CAP,\d+,PERS,rev_load_dirty_no,\d+,(\d+),(\d+),discard_load\b"
)
_REV_LOAD_DIRTY_CANCEL_RE = re.compile(
    r"#CAP,\d+,PERS,rev_load_dirty_cancel,\d+,(\d+),(\d+),cancel\b"
)
_REV_LOAD_DISPATCH_RE = re.compile(r"#CAP,\d+,PERS,rev_load_dispatch,\d+,(\d+),(\d+),run\b")
_REV_LOAD_COMPLETE_RE = re.compile(
    r"#CAP,\d+,PERS,rev_load_complete,\d+,(\d+),(\d+),S(\d{4})_v(\d{4})\b"
)


def _anchor_index(args: object, name: str, default: int = -1) -> int:
    if not hasattr(args, name):
        return default
    value = getattr(args, name)
    if value is None:
        return default
    return int(value)


def verify_revision_load_dirty(lines: list[str], args: object) -> dict[str, object]:
    issues: list[str] = []
    choice = str(getattr(args, "dirty_prompt_choice", "") or "").lower()
    setup_commit_anchor = _anchor_index(args, "revision_setup_commit_anchor", 0)
    dirty_record_anchor = _anchor_index(args, "revision_dirty_record_anchor", -1)
    load_anchor = _anchor_index(args, "revision_load_serial_anchor", -1)
    if load_anchor < 0:
        load_anchor = _anchor_index(args, "overlay_load_confirm_anchor", -1)
    dirty_choice_anchor = _anchor_index(args, "revision_dirty_choice_anchor", -1)
    if dirty_choice_anchor < 0:
        dirty_choice_anchor = _anchor_index(args, "overlay_dirty_confirm_anchor", -1)

    setup_complete = _first_match_after(lines, _REV_COMPLETE_RE, setup_commit_anchor)
    if setup_complete is None:
        issues.append("missing_setup_rev_complete")
    else:
        setup_set_id = int(setup_complete.group(1))
        setup_revision_id = int(setup_complete.group(2))
        setattr(args, "setup_set_id", setup_set_id)
        setattr(args, "setup_revision_id", setup_revision_id)

    if dirty_record_anchor >= 0:
        post_record_ok = any(
            re.search(r"#CAP,\d+,PERS,result,\d+,\d+,\d+,ok\b", line) is not None
            and "rev_" not in line
            for line in lines[dirty_record_anchor:]
        )
        if not post_record_ok:
            issues.append("missing_pers_result_ok_after_dirty_record")

    if load_anchor < 0:
        issues.append("missing_revision_load_serial_anchor")
    else:
        prompt_match = _first_match_after(lines, _REV_LOAD_DIRTY_PROMPT_RE, load_anchor)
        if prompt_match is None:
            issues.append("missing_rev_load_dirty_prompt_after_load_request")
        elif setup_complete is not None:
            prompt_set = int(prompt_match.group(1))
            prompt_rev = int(prompt_match.group(2))
            if prompt_set != setup_set_id or prompt_rev != setup_revision_id:
                issues.append(
                    f"dirty_prompt_target_mismatch "
                    f"expected=S{setup_set_id:04d}_v{setup_revision_id:04d} "
                    f"got={prompt_set}/{prompt_rev}"
                )

    if dirty_choice_anchor < 0:
        issues.append("missing_revision_dirty_choice_anchor")
    else:
        if choice == "yes":
            if _first_match_after(lines, _REV_LOAD_DIRTY_YES_RE, dirty_choice_anchor) is None:
                issues.append("missing_rev_load_dirty_yes")
            second_commit = _first_match_after(lines, _REV_COMPLETE_RE, dirty_choice_anchor)
            if second_commit is None:
                issues.append("missing_second_rev_complete_after_dirty_yes")
            load_complete = _first_match_after(lines, _REV_LOAD_COMPLETE_RE, dirty_choice_anchor)
            if load_complete is None:
                issues.append("missing_rev_load_complete_after_dirty_yes")
            elif setup_complete is not None and load_complete is not None:
                load_rev = int(load_complete.group(4))
                if load_rev != setup_revision_id:
                    issues.append(
                        f"dirty_yes_load_revision_mismatch "
                        f"expected=v{setup_revision_id:04d} got=v{load_rev:04d}"
                    )
        elif choice == "no":
            if _first_match_after(lines, _REV_LOAD_DIRTY_NO_RE, dirty_choice_anchor) is None:
                issues.append("missing_rev_load_dirty_no")
            if _first_match_after(lines, _REV_COMPLETE_RE, dirty_choice_anchor) is not None:
                issues.append("unexpected_rev_complete_after_dirty_no")
            load_complete = _first_match_after(lines, _REV_LOAD_COMPLETE_RE, dirty_choice_anchor)
            if load_complete is None:
                issues.append("missing_rev_load_complete_after_dirty_no")
        elif choice == "cancel":
            if _first_match_after(lines, _REV_LOAD_DIRTY_CANCEL_RE, dirty_choice_anchor) is None:
                issues.append("missing_rev_load_dirty_cancel")
            if _first_match_after(lines, _REV_LOAD_DISPATCH_RE, dirty_choice_anchor) is not None:
                issues.append("unexpected_rev_load_dispatch_after_dirty_cancel")
            if _first_match_after(lines, _REV_LOAD_COMPLETE_RE, dirty_choice_anchor) is not None:
                issues.append("unexpected_rev_load_complete_after_dirty_cancel")
        else:
            issues.append(f"unknown_dirty_prompt_choice:{choice!r}")

    return {
        "ok": not issues,
        "issues": issues,
        "dirty_prompt_choice": choice,
        "setup_commit_anchor": setup_commit_anchor,
        "dirty_record_anchor": dirty_record_anchor,
        "load_anchor": load_anchor,
        "dirty_choice_anchor": dirty_choice_anchor,
        "setup_set_id": getattr(args, "setup_set_id", None),
        "setup_revision_id": getattr(args, "setup_revision_id", None),
    }
