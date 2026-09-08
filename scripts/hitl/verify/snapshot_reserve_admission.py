#!/usr/bin/env python3
"""Verify DEC-053 Snapshot yield → retry → complete-publish on a capture log."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

_ST_RE = re.compile(r"#CAP,(\d+),ST,Track,([^,]+),([^,\s]+)")
_YIELD_RE = re.compile(
    r"#CAP,(\d+),DIAG,snapshot_idle,yield,remaining=(\d+),margin=(\d+),"
    r"admitted=(\d+),cost=(\d+),crit=(\d+),pending=(\d+),root_rev=(\d+)"
)
_IDLE_RE = re.compile(
    r"#CAP,(\d+),DIAG,snapshot_idle,us=(\d+),pending=(\d+),root=([^,]+),root_rev=(\d+),"
    r"playback_rev=(\d+),queued=(\d+),applied=(\d+),construction=(\d+),exit=(\d+),"
    r"inflight=(\d+),journals=(\d+)"
)

_TIMING_CRITICAL = frozenset({"RECORDING", "PLAYING", "OVERDUBBING", "STOPPED_RECORDING", "ARMED"})


def _state_at(lines: list[str], ts: int) -> str:
    state = "STOPPED"
    for line in lines:
        match = _ST_RE.search(line)
        if match is None:
            continue
        if int(match.group(1)) > ts:
            break
        state = match.group(3)
    return state


def verify_snapshot_reserve_admission(lines: list[str]) -> dict[str, object]:
    issues: list[str] = []
    yields = [_YIELD_RE.search(line) for line in lines]
    yields = [match for match in yields if match is not None]
    idles = [_IDLE_RE.search(line) for line in lines]
    idles = [match for match in idles if match is not None]

    live_yields = []
    for match in yields:
        state = _state_at(lines, int(match.group(1)))
        if state == "STOPPED":
            issues.append("yield_during_stopped")
            continue
        if state not in _TIMING_CRITICAL:
            issues.append(f"yield_outside_timing_critical:{state}")
            continue
        live_yields.append((state, match))

    if not live_yields:
        issues.append("missing_timing_critical_yield")

    first_yield = live_yields[0][1] if live_yields else None
    if first_yield is not None and first_yield.group(6) != "1":
        issues.append("yield_crit_not_set")
    if first_yield is not None and first_yield.group(7) != "1":
        issues.append("yield_pending_not_retained")

    yield_root = int(first_yield.group(8)) if first_yield is not None else None
    yield_ts = int(first_yield.group(1)) if first_yield is not None else None

    complete = None
    prefix = False
    if yield_ts is not None:
        for match in idles:
            ts = int(match.group(1))
            if ts <= yield_ts:
                continue
            pending = int(match.group(3))
            root_rev = int(match.group(5))
            construction = int(match.group(9))
            journals = int(match.group(12))
            if pending == 0 and construction == 0 and journals == 0:
                complete = match
                break
            if yield_root is not None and root_rev != yield_root and pending != 0:
                prefix = True
    if prefix:
        issues.append("prefix_publication_after_yield")
    if complete is None:
        issues.append("missing_complete_publish_after_yield")
    elif yield_root is not None and int(complete.group(5)) == yield_root:
        issues.append("retry_did_not_advance_root")

    return {
        "ok": not issues,
        "issues": issues,
        "yield_count": len(live_yields),
        "yield_state": live_yields[0][0] if live_yields else None,
        "yield_root_rev": yield_root,
        "complete_root_rev": int(complete.group(5)) if complete is not None else None,
        "complete_pending": int(complete.group(3)) if complete is not None else None,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="capture session log")
    args = parser.parse_args()
    lines = args.log.read_text(encoding="utf-8", errors="replace").splitlines()
    result = verify_snapshot_reserve_admission(lines)
    print(result)
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
