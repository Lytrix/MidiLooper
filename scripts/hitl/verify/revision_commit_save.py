"""Serial verification for revision commit save (HITL with post-run catalog cleanup)."""

from __future__ import annotations

import re
from typing import Optional

_REV_REQUEST_RE = re.compile(r"#CAP,\d+,PERS,rev_request,\d+,\d+,\d+,queued\b")
_REV_HITL_ARM_RE = re.compile(r"#CAP,\d+,PERS,rev_hitl_arm,\d+,\d+,\d+,armed\b")
_REV_DISPATCH_RE = re.compile(r"#CAP,\d+,PERS,rev_dispatch,\d+,\d+,\d+,run\b")
_REV_COMPLETE_RE = re.compile(
    r"#CAP,\d+,PERS,rev_complete,\d+,\d+,\d+,S(\d{4})_v(\d{4})\b"
)
_REV_CLEANUP_OK_RE = re.compile(r"#CAP,\d+,PERS,rev_cleanup,\d+,\d+,\d+,ok\b")
_STORAGE_COMPLETE_RE = re.compile(
    r"\[StorageManager\] Revision commit complete S(\d+) v(\d+)\b"
)
_RESULT_OK_RE = re.compile(r"#CAP,\d+,PERS,result,\d+,\d+,\d+,ok\b")


def _first_match_after(
    lines: list[str],
    pattern: re.Pattern[str],
    after_line_index: int,
) -> Optional[re.Match[str]]:
    start = max(0, after_line_index)
    for line in lines[start:]:
        match = pattern.search(line)
        if match is not None:
            return match
    return None


def verify_revision_commit_save(lines: list[str], args: object) -> dict[str, object]:
    issues: list[str] = []
    commit_anchor = int(getattr(args, "revision_commit_serial_anchor", 0) or 0)
    cleanup_anchor = int(getattr(args, "revision_cleanup_serial_anchor", -1) or -1)
    require_cleanup = bool(getattr(args, "require_hitl_cleanup", True))

    pre_commit_ok = any(
        _RESULT_OK_RE.search(line) is not None and "rev_" not in line
        for line in lines[: max(0, commit_anchor)]
    )
    if commit_anchor > 0 and not pre_commit_ok:
        issues.append("missing_pers_result_ok_before_revision_commit")

    if _first_match_after(lines, _REV_REQUEST_RE, commit_anchor) is None:
        issues.append("missing_rev_request_after_commit_anchor")
    if _first_match_after(lines, _REV_HITL_ARM_RE, commit_anchor) is None:
        issues.append("missing_rev_hitl_arm_after_commit_anchor")
    if _first_match_after(lines, _REV_DISPATCH_RE, commit_anchor) is None:
        issues.append("missing_rev_dispatch_after_commit_anchor")

    complete_match = _first_match_after(lines, _REV_COMPLETE_RE, commit_anchor)
    storage_match = _first_match_after(lines, _STORAGE_COMPLETE_RE, commit_anchor)
    if complete_match is None:
        issues.append("missing_rev_complete_cap_line")
    if storage_match is None:
        issues.append("missing_storage_manager_revision_complete_line")
    if complete_match is not None and storage_match is not None:
        cap_set = int(complete_match.group(1))
        cap_rev = int(complete_match.group(2))
        log_set = int(storage_match.group(1))
        log_rev = int(storage_match.group(2))
        if cap_set != log_set or cap_rev != log_rev:
            issues.append(
                f"rev_complete_id_mismatch cap=S{cap_set:04d}_v{cap_rev:04d} "
                f"log=S{log_set}_v{log_rev}"
            )

    cleanup_ok = False
    if require_cleanup:
        if cleanup_anchor < 0:
            issues.append("missing_revision_cleanup_serial_anchor")
        elif _first_match_after(lines, _REV_CLEANUP_OK_RE, cleanup_anchor) is None:
            issues.append("missing_rev_cleanup_ok_after_cleanup_anchor")
        else:
            cleanup_ok = True

    return {
        "ok": not issues,
        "issues": issues,
        "commit_anchor": commit_anchor,
        "cleanup_anchor": cleanup_anchor,
        "rev_complete": complete_match.group(0) if complete_match else None,
        "cleanup_ok": cleanup_ok,
    }
