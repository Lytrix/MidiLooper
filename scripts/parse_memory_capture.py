#!/usr/bin/env python3
"""Parse Teensy capture logs for internal heap and PSRAM pool snapshots."""
from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path

HEAP_RE = re.compile(
    r"\[Memory\]\s+heap free=(\d+)\s+used=(\d+)\s+total=(\d+)\s+KB",
    re.IGNORECASE,
)
HEAP_INFO_RE = re.compile(
    r"\[INFO\]\s+\[Memory\]\s+heap free=(\d+)\s+used=(\d+)\s+total=(\d+)\s+KB",
    re.IGNORECASE,
)
PSRAM_RE = re.compile(
    r"\[Memory\]\s+psram chip=\d+ MB free=(\d+)\s+used=(\d+)\s+pool=(\d+)\s+KB",
    re.IGNORECASE,
)
PSRAM_INFO_RE = re.compile(
    r"\[INFO\]\s+\[Memory\]\s+psram chip=\d+ MB free=(\d+)\s+used=(\d+)\s+pool=(\d+)\s+KB",
    re.IGNORECASE,
)


@dataclass
class MemorySnapshot:
    line_no: int
    heap_free_kb: int
    heap_used_kb: int
    heap_total_kb: int
    psram_free_kb: int | None = None
    psram_used_kb: int | None = None
    psram_pool_kb: int | None = None


def parse_log(path: Path) -> list[MemorySnapshot]:
    snapshots: list[MemorySnapshot] = []
    pending_heap: MemorySnapshot | None = None
    with path.open(encoding="utf-8", errors="replace") as handle:
        for line_no, line in enumerate(handle, start=1):
            heap_match = HEAP_RE.search(line) or HEAP_INFO_RE.search(line)
            if heap_match:
                pending_heap = MemorySnapshot(
                    line_no=line_no,
                    heap_free_kb=int(heap_match.group(1)),
                    heap_used_kb=int(heap_match.group(2)),
                    heap_total_kb=int(heap_match.group(3)),
                )
                snapshots.append(pending_heap)
                continue
            psram_match = PSRAM_RE.search(line) or PSRAM_INFO_RE.search(line)
            if psram_match and pending_heap is not None and pending_heap.psram_free_kb is None:
                pending_heap.psram_free_kb = int(psram_match.group(1))
                pending_heap.psram_used_kb = int(psram_match.group(2))
                pending_heap.psram_pool_kb = int(psram_match.group(3))
    return snapshots


def summarize(path: Path, snapshots: list[MemorySnapshot]) -> str:
    if not snapshots:
        return f"{path}: no [Memory] heap lines found"
    first = snapshots[0]
    min_free = min(s.heap_free_kb for s in snapshots)
    max_used = max(s.heap_used_kb for s in snapshots)
    lines = [
        f"file: {path}",
        f"snapshots: {len(snapshots)}",
        f"first line {first.line_no}: heap free={first.heap_free_kb} used={first.heap_used_kb} total={first.heap_total_kb} KB",
        f"min heap free: {min_free} KB",
        f"max heap used: {max_used} KB",
    ]
    if first.psram_free_kb is not None:
        lines.append(
            f"first psram: free={first.psram_free_kb} used={first.psram_used_kb} pool={first.psram_pool_kb} KB"
        )
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Summarize heap/psram lines in capture logs")
    parser.add_argument("logs", nargs="+", type=Path, help="Capture log file(s)")
    args = parser.parse_args()
    for path in args.logs:
        snapshots = parse_log(path)
        print(summarize(path, snapshots))
        print()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
