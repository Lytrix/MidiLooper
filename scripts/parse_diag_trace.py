#!/usr/bin/env python3
"""Parse #CAP DIAG and DIAGCHK lines from capture session logs."""
from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path

DIAG_RE = re.compile(
    r"#CAP,(\d+),DIAG,(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+)"
)
DIAGCHK_RE = re.compile(
    r"#CAP,(\d+),DIAGCHK,(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+)"
)

CATEGORY_NAMES = {
    1: "Memory",
    2: "Playback",
    3: "Edit",
    4: "Storage",
    5: "Display",
    6: "Validation",
}


@dataclass
class DiagRecord:
    line_no: int
    boot_us: int
    kind: str
    format_version: int
    event_id: int
    track_state: int
    edit_session: int
    looper_state: int
    context_flags: int
    record_flags: int
    heap_free: int
    heap_used: int
    extmem_free: int
    payload: int

    @property
    def category(self) -> str:
        return CATEGORY_NAMES.get(self.event_id >> 8, f"cat{self.event_id >> 8}")

    @property
    def local_id(self) -> int:
        return self.event_id & 0xFF


def parse_log(path: Path) -> list[DiagRecord]:
    records: list[DiagRecord] = []
    with path.open(encoding="utf-8", errors="replace") as handle:
        for line_no, line in enumerate(handle, start=1):
            for kind, pattern in (("DIAG", DIAG_RE), ("DIAGCHK", DIAGCHK_RE)):
                match = pattern.search(line)
                if not match:
                    continue
                groups = [int(g) for g in match.groups()]
                records.append(
                    DiagRecord(
                        line_no=line_no,
                        boot_us=groups[0],
                        kind=kind,
                        format_version=groups[1],
                        event_id=groups[2],
                        track_state=groups[3],
                        edit_session=groups[4],
                        looper_state=groups[5],
                        context_flags=groups[6],
                        record_flags=groups[7],
                        heap_free=groups[8],
                        heap_used=groups[9],
                        extmem_free=groups[10],
                        payload=groups[11],
                    )
                )
                break
    return records


def summarize(path: Path, records: list[DiagRecord]) -> str:
    if not records:
        return f"{path}: no DIAG/DIAGCHK lines found"
    lines = [f"file: {path}", f"records: {len(records)}"]
    checkpoints = [r for r in records if r.kind == "DIAGCHK"]
    if checkpoints:
        last = checkpoints[-1]
        lines.append(
            f"last DIAGCHK line {last.line_no}: event={last.event_id} "
            f"({last.category}:{last.local_id}) heap_free={last.heap_free} "
            f"heap_used={last.heap_used} extmem_free={last.extmem_free}"
        )
    min_heap = min((r.heap_free for r in records if r.record_flags & 1), default=None)
    if min_heap is not None:
        lines.append(f"min heap_free (snapshots): {min_heap}")
    edit_events = [r for r in records if r.category == "Edit"]
    if edit_events:
        lines.append("edit timeline:")
        for record in edit_events:
            heap = f" free={record.heap_free}" if record.record_flags & 1 else ""
            lines.append(
                f"  line {record.line_no}: id={record.local_id}{heap} "
                f"track={record.track_state} edit={record.edit_session}"
            )
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", type=Path)
    args = parser.parse_args()
    for path in args.logs:
        records = parse_log(path)
        print(summarize(path, records))
        print()


if __name__ == "__main__":
    main()
