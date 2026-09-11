"""OpenSpec 5.5 — deferred persist stays sliced on StorageManager, not a sync drain."""

from __future__ import annotations

from typing import Optional

from hitl.persistence_rows import extract_persistence_drain_rows, extract_persistence_work_rows


def summarize_deferred_persist_nonblocking(
    lines: list[str],
    *,
    record_stop_ts: Optional[int] = None,
) -> dict[str, object]:
    """After record stop, LoopPersist must be sliced PERS,work with no PERS,drain.

    ``MO`` interleave is recorded when present but is not a fail: a short persist
    window can sit between playback notes.
    """
    work = extract_persistence_work_rows(lines)
    drain = extract_persistence_drain_rows(lines)
    if record_stop_ts is not None:
        work = [row for row in work if int(row["timestamp"]) >= record_stop_ts]
        drain = [row for row in drain if int(row["timestamp"]) >= record_stop_ts]

    loop_persist = [row for row in work if str(row.get("work_type", "")) == "LoopPersist"]
    phases = {str(row.get("phase", "")) for row in loop_persist}
    start_ts = min((int(row["timestamp"]) for row in loop_persist), default=None)
    done_ts = max(
        (int(row["timestamp"]) for row in loop_persist if str(row.get("phase", "")) == "done"),
        default=None,
    )
    mo_during = 0
    if start_ts is not None and done_ts is not None:
        for line in lines:
            if ",MO," not in line:
                continue
            parts = line.split(",")
            if len(parts) < 3 or parts[0] != "#CAP":
                continue
            try:
                ts = int(parts[1])
            except ValueError:
                continue
            if start_ts <= ts <= done_ts:
                mo_during += 1

    sliced = len(loop_persist) >= 2 and "start" in phases
    sync_drain_count = len(drain)
    return {
        "loop_persist_work_count": len(loop_persist),
        "loop_persist_sliced": sliced,
        "sync_drain_count": sync_drain_count,
        "mo_during_loop_persist": mo_during,
        "ok": sliced and sync_drain_count == 0,
    }


def deferred_persist_nonblocking_issues(summary: dict[str, object]) -> list[str]:
    issues: list[str] = []
    if not summary.get("loop_persist_sliced"):
        issues.append("persistence_loop_persist_not_sliced")
    if int(summary.get("sync_drain_count") or 0) > 0:
        issues.append("persistence_sync_drain_after_record_stop")
    return issues
