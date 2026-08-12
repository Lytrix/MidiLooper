#!/usr/bin/env python3
"""Summarize persistence save/load buildup from a capture-serial session log."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.persistence_rows import (  # noqa: E402
    count_clear_abort_lines,
    extract_persistence_backlog_rows,
    extract_persistence_diag_rows,
    extract_persistence_drain_rows,
    extract_persistence_request_rows,
    extract_persistence_work_rows,
    extract_save_phase_rows,
)


def _us_to_s(timestamp_us: int) -> float:
    return timestamp_us / 1_000_000.0


def _fmt_bytes(value: int) -> str:
    if value >= 1_048_576:
        return f"{value / 1_048_576:.1f} MB"
    if value >= 1024:
        return f"{value / 1024:.1f} KB"
    return f"{value} B"


def _peak_row(rows: list[dict[str, object]], key: str) -> dict[str, object] | None:
    if not rows:
        return None
    return max(rows, key=lambda row: int(row[key]))


def _print_backlog_timeline(rows: list[dict[str, object]], limit: int) -> None:
    if not rows:
        print("  (no PERS,backlog lines — flash teensy41-capture-serial with backlog telemetry)")
        return
    print(f"  samples: {len(rows)} (every ~5s during capture, ~10s idle)")
    peak = _peak_row(rows, "est_slice_steps")
    assert peak is not None
    print(
        "  peak est_slice_steps: "
        f"{peak['est_slice_steps']} @ {_us_to_s(int(peak['timestamp'])):.1f}s "
        f"(workQ={peak['work_queue_depth']}, chunkQ={peak['chunk_queue_depth']}, "
        f"dirtyAge={peak['dirty_age_ms']}ms, estSd={_fmt_bytes(int(peak['est_sd_bytes']))})"
    )
    if limit <= 0:
        return
    print("  timeline (workQ / chunkQ / estSteps / dirtyAgeMs / transportBlk / budgetBlk):")
    for row in rows[:limit]:
        print(
            f"    {_us_to_s(int(row['timestamp'])):8.1f}s  "
            f"{row['work_queue_depth']:3} / {row['chunk_queue_depth']:3} / "
            f"{row['est_slice_steps']:5} / {row['dirty_age_ms']:6} / "
            f"{row['transport_block_count']:4} / {row['budget_block_count']:4}"
        )
    if len(rows) > limit:
        print(f"    ... {len(rows) - limit} more (use --backlog-limit 0 for all)")


def summarize_session(lines: list[str], backlog_limit: int) -> None:
    backlog_rows = extract_persistence_backlog_rows(lines)
    diag_rows = extract_persistence_diag_rows(lines)
    request_rows = extract_persistence_request_rows(lines)
    work_rows = extract_persistence_work_rows(lines)
    drain_rows = extract_persistence_drain_rows(lines)
    save_rows = extract_save_phase_rows(lines)
    clear_aborts = count_clear_abort_lines(lines)

    print("=== Persistence buildup summary ===")
    print()
    print("Workspace backlog (PERS,backlog):")
    _print_backlog_timeline(backlog_rows, backlog_limit)
    print()

    if diag_rows:
        peak_dirty = _peak_row(diag_rows, "dirty_age_ms")
        assert peak_dirty is not None
        print("Chunk pool diag (PERS,diag):")
        print(f"  samples: {len(diag_rows)}")
        print(
            f"  peak dirty_age_ms: {peak_dirty['dirty_age_ms']} "
            f"@ {_us_to_s(int(peak_dirty['timestamp'])):.1f}s "
            f"(chunkQ={peak_dirty['chunk_queue_depth']}, savePending={peak_dirty['save_pending']})"
        )
        print()

    if request_rows:
        outcomes: dict[str, int] = {}
        for row in request_rows:
            outcome = str(row["outcome"])
            outcomes[outcome] = outcomes.get(outcome, 0) + 1
        print("Save admission (PERS,request):")
        for outcome, count in sorted(outcomes.items(), key=lambda item: (-item[1], item[0])):
            print(f"  {outcome}: {count}")
        print()

    if save_rows:
        phases: dict[str, int] = {}
        for row in save_rows:
            phase = str(row["phase"])
            phases[phase] = phases.get(phase, 0) + 1
        print("Save display FSM (#CAP,SAVE):")
        for phase, count in sorted(phases.items()):
            print(f"  {phase}: {count}")
        print()

    if work_rows:
        completed = sum(1 for row in work_rows if row["phase"] == "complete" and row["outcome"] == "ok")
        started = sum(1 for row in work_rows if row["phase"] == "start")
        print(f"Work items (PERS,work): start={started}, complete_ok={completed}, total={len(work_rows)}")
        print()

    if drain_rows:
        print("Sync drain failures (PERS,drain):")
        for row in drain_rows:
            print(
                f"  {_us_to_s(int(row['timestamp'])):.1f}s  {row['reason']}  "
                f"steps={row['steps']} stuck={row['stuck_iterations']}  "
                f"workQ={row['work_queue_depth']} estSteps={row['est_slice_steps']} "
                f"estSd={_fmt_bytes(int(row['est_sd_bytes']))}"
            )
        print()

    if clear_aborts:
        print(f"Clear aborted (Serial): {clear_aborts}")
        print()

    print("Compare sessions:")
    print("  python scripts/parse_persistence_buildup.py captures/session_A.log captures/session_B.log")
    print()
    print("Grep anchors:")
    print("  rg 'PERS,backlog|PERS,drain|Clear aborted|Persistence drain stuck' <session.log>")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", type=Path, help="capture session log path(s)")
    parser.add_argument(
        "--backlog-limit",
        type=int,
        default=12,
        help="max PERS,backlog rows to print per session (0 = all)",
    )
    args = parser.parse_args()

    for index, log_path in enumerate(args.logs):
        if index > 0:
            print("\n" + "=" * 60 + "\n")
        if not log_path.is_file():
            print(f"ERROR: missing file: {log_path}", file=sys.stderr)
            return 1
        print(f"Session: {log_path}")
        lines = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
        summarize_session(lines, args.backlog_limit)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
