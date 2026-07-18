"""Parse #CAP,PERS,... serial lines for heap telemetry vs chunk-pool diagnostics."""

from __future__ import annotations

# Stages emitted via SC_PERSIST(stage, durationUs, heapBefore, heapAfter, outcome) where
# heapBefore/heapAfter are internal-heap free bytes. Other PERS lines (diag, work, mid_pass,
# rev_*, …) reuse the PERS tag with different field semantics and must not be parsed as heap.
PERSISTENCE_HEAP_STAGES: frozenset[str] = frozenset(
    {
        "result",
        "slice",
        "request",
    }
)


def extract_persistence_heap_rows(lines: list[str]) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for line in lines:
        if ",PERS," not in line:
            continue
        parts = line.split(",")
        # Standard SC_PERSIST line: #CAP,<us>,PERS,<stage>,<duration>,<heap_before>,<heap_after>,<outcome>
        if len(parts) != 8:
            continue
        stage = parts[3].strip()
        if stage not in PERSISTENCE_HEAP_STAGES:
            continue
        try:
            ts = int(parts[1])
            duration_us = int(parts[4])
            heap_before = int(parts[5])
            heap_after = int(parts[6])
        except ValueError:
            continue
        rows.append(
            {
                "timestamp": ts,
                "stage": stage,
                "duration_us": duration_us,
                "heap_before": heap_before,
                "heap_after": heap_after,
                "outcome": parts[7].strip(),
            }
        )
    return rows


def extract_persistence_diag_rows(lines: list[str]) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for line in lines:
        if ",PERS,diag," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 18:
            continue
        try:
            ts = int(parts[1])
            free_chunks = int(parts[4])
            used_chunks = int(parts[5])
            chunk_reserve = int(parts[6])
            queue_depth = int(parts[7])
            writing_chunks = int(parts[8])
        except ValueError:
            continue
        rows.append(
            {
                "timestamp": ts,
                "free_chunks": free_chunks,
                "used_chunks": used_chunks,
                "chunk_reserve": chunk_reserve,
                "queue_depth": queue_depth,
                "writing_chunks": writing_chunks,
            }
        )
    return rows
