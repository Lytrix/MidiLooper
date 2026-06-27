"""Serial verification for revision load HITL (commit → load → cleanup)."""

from __future__ import annotations

import re
from typing import Optional

from hitl.verify.revision_commit_save import (
    _REV_CLEANUP_OK_RE,
    _REV_DISPATCH_RE,
    _REV_HITL_ARM_RE,
    _REV_REQUEST_RE,
    _STORAGE_COMPLETE_RE,
    _first_match_after,
)

_REV_COMPLETE_RE = re.compile(
    r"#CAP,\d+,PERS,rev_complete,\d+,\d+,\d+,S(\d{4})_v(\d{4})\b"
)
_REV_LOAD_REQUEST_RE = re.compile(r"#CAP,\d+,PERS,rev_load_request,\d+,(\d+),(\d+),queued\b")
_REV_LOAD_HITL_ARM_RE = re.compile(
    r"#CAP,\d+,PERS,rev_load_hitl_arm,\d+,(\d+),(\d+),armed\b"
)
_REV_LOAD_DISPATCH_RE = re.compile(r"#CAP,\d+,PERS,rev_load_dispatch,\d+,(\d+),(\d+),run\b")
_REV_LOAD_COMPLETE_RE = re.compile(
    r"#CAP,\d+,PERS,rev_load_complete,\d+,(\d+),(\d+),S(\d{4})_v(\d{4})\b"
)
_REV_NUKE_SETS_OK_RE = re.compile(r"#CAP,\d+,PERS,rev_nuke_sets,\d+,\d+,\d+,ok\b")
_STORAGE_LOAD_COMPLETE_RE = re.compile(
    r"\[StorageManager\] Revision load complete S(\d+) v(\d+)\b"
)


def _anchor_index(args: object, name: str, default: int = -1) -> int:
    if not hasattr(args, name):
        return default
    value = getattr(args, name)
    if value is None:
        return default
    return int(value)


def verify_revision_load(lines: list[str], args: object) -> dict[str, object]:
    issues: list[str] = []
    nuke_anchor = _anchor_index(args, "revision_nuke_serial_anchor", -1)
    commit_anchor = _anchor_index(args, "revision_commit_serial_anchor", 0)
    load_anchor = _anchor_index(args, "revision_load_serial_anchor", -1)
    cleanup_anchor = _anchor_index(args, "revision_cleanup_serial_anchor", -1)
    require_nuke = bool(getattr(args, "require_sets_nuke", True))
    require_cleanup = bool(getattr(args, "require_hitl_cleanup", True))

    nuke_ok = False
    if require_nuke:
        if nuke_anchor < 0:
            issues.append("missing_revision_nuke_serial_anchor")
        elif _first_match_after(lines, _REV_NUKE_SETS_OK_RE, nuke_anchor) is None:
            issues.append("missing_rev_nuke_sets_ok_after_nuke_anchor")
        else:
            nuke_ok = True

    if commit_anchor > 0 and not bool(getattr(args, "skip_workspace_save_prelude", False)):
        pre_commit_ok = any(
            re.search(r"#CAP,\d+,PERS,result,\d+,\d+,\d+,ok\b", line) is not None
            and "rev_" not in line
            for line in lines[:commit_anchor]
        )
        if not pre_commit_ok:
            issues.append("missing_pers_result_ok_before_revision_commit")

    if _first_match_after(lines, _REV_REQUEST_RE, commit_anchor) is None:
        issues.append("missing_rev_request_after_commit_anchor")
    if _first_match_after(lines, _REV_HITL_ARM_RE, commit_anchor) is None:
        issues.append("missing_rev_hitl_arm_after_commit_anchor")
    if _first_match_after(lines, _REV_DISPATCH_RE, commit_anchor) is None:
        issues.append("missing_rev_dispatch_after_commit_anchor")

    complete_match = _first_match_after(lines, _REV_COMPLETE_RE, commit_anchor)
    storage_commit_match = _first_match_after(lines, _STORAGE_COMPLETE_RE, commit_anchor)
    if complete_match is None:
        issues.append("missing_rev_complete_cap_line")
    if storage_commit_match is None:
        issues.append("missing_storage_manager_revision_complete_line")

    committed_set_id: Optional[int] = None
    committed_revision_id: Optional[int] = None
    if complete_match is not None and storage_commit_match is not None:
        cap_set = int(complete_match.group(1))
        cap_rev = int(complete_match.group(2))
        log_set = int(storage_commit_match.group(1))
        log_rev = int(storage_commit_match.group(2))
        committed_set_id = cap_set
        committed_revision_id = cap_rev
        if cap_set != log_set or cap_rev != log_rev:
            issues.append(
                f"rev_complete_id_mismatch cap=S{cap_set:04d}_v{cap_rev:04d} "
                f"log=S{log_set}_v{log_rev}"
            )

    if load_anchor < 0:
        issues.append("missing_revision_load_serial_anchor")
    else:
        load_request_match = _first_match_after(lines, _REV_LOAD_REQUEST_RE, load_anchor)
        load_arm_match = _first_match_after(lines, _REV_LOAD_HITL_ARM_RE, load_anchor)
        load_dispatch_match = _first_match_after(lines, _REV_LOAD_DISPATCH_RE, load_anchor)
        load_complete_match = _first_match_after(lines, _REV_LOAD_COMPLETE_RE, load_anchor)
        storage_load_match = _first_match_after(lines, _STORAGE_LOAD_COMPLETE_RE, load_anchor)

        if load_request_match is None:
            issues.append("missing_rev_load_request_after_load_anchor")
        if load_arm_match is None:
            issues.append("missing_rev_load_hitl_arm_after_load_anchor")
        if load_dispatch_match is None:
            issues.append("missing_rev_load_dispatch_after_load_anchor")
        if load_complete_match is None:
            issues.append("missing_rev_load_complete_cap_line")
        if storage_load_match is None:
            issues.append("missing_storage_manager_revision_load_complete_line")

        if (
            committed_set_id is not None
            and committed_revision_id is not None
            and load_complete_match is not None
            and storage_load_match is not None
        ):
            for label, match in (
                ("load_request", load_request_match),
                ("load_arm", load_arm_match),
                ("load_dispatch", load_dispatch_match),
            ):
                if match is None:
                    continue
                req_set = int(match.group(1))
                req_rev = int(match.group(2))
                if req_set != committed_set_id or req_rev != committed_revision_id:
                    issues.append(
                        f"{label}_id_mismatch expected=S{committed_set_id:04d}_v"
                        f"{committed_revision_id:04d} got={req_set}/{req_rev}"
                    )

            cap_set = int(load_complete_match.group(3))
            cap_rev = int(load_complete_match.group(4))
            log_set = int(storage_load_match.group(1))
            log_rev = int(storage_load_match.group(2))
            if cap_set != log_set or cap_rev != log_rev:
                issues.append(
                    f"rev_load_complete_id_mismatch cap=S{cap_set:04d}_v{cap_rev:04d} "
                    f"log=S{log_set}_v{log_rev}"
                )
            if cap_set != committed_set_id or cap_rev != committed_revision_id:
                issues.append(
                    f"rev_load_complete_does_not_match_commit "
                    f"commit=S{committed_set_id:04d}_v{committed_revision_id:04d} "
                    f"load=S{cap_set:04d}_v{cap_rev:04d}"
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
        "nuke_anchor": nuke_anchor,
        "commit_anchor": commit_anchor,
        "load_anchor": load_anchor,
        "cleanup_anchor": cleanup_anchor,
        "committed_set_id": committed_set_id,
        "committed_revision_id": committed_revision_id,
        "rev_complete": complete_match.group(0) if complete_match else None,
        "rev_load_complete": load_complete_match.group(0) if load_anchor >= 0 else None,
        "nuke_ok": nuke_ok,
        "cleanup_ok": cleanup_ok,
    }
