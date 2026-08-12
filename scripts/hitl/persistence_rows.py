"""Parse #CAP,PERS,... serial lines for heap telemetry vs chunk-pool diagnostics."""

from __future__ import annotations

import re

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

_SAVE_PHASE_RE = re.compile(r"#CAP,(\d+),SAVE,([^,]+),(\d+)")
_PERS_REQUEST_RE = re.compile(r"#CAP,(\d+),PERS,request,\d+,\d+,\d+,([^,\r\n]+)")
_PERS_WORK_RE = re.compile(
    r"#CAP,(\d+),PERS,work,\d+,\d+,\d+,([^,]+),([^,]+),([^,]+),([^,\r\n]+)"
)
_PERS_DRAIN_RE = re.compile(
    r"#CAP,(\d+),PERS,drain,([^,]+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+)"
)
_CLEAR_ABORT_RE = re.compile(r"Clear aborted", re.IGNORECASE)


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
        if len(parts) < 19:
            continue
        try:
            ts = int(parts[1])
            free_chunks = int(parts[4])
            used_chunks = int(parts[5])
            chunk_reserve = int(parts[6])
            chunk_queue_depth = int(parts[7])
            writing_chunks = int(parts[8])
            transport_block_count = int(parts[9])
            heap_floor_block_count = int(parts[10])
            budget_block_count = int(parts[11])
            slice_done_count = int(parts[12])
            peak_writer_latency_us = int(parts[13])
            dirty_age_ms = int(parts[14])
            max_chunk_backlog = int(parts[15])
            save_pending = int(parts[16])
            save_in_progress = int(parts[17])
            capture_active = int(parts[18])
        except ValueError:
            continue
        rows.append(
            {
                "timestamp": ts,
                "free_chunks": free_chunks,
                "used_chunks": used_chunks,
                "chunk_reserve": chunk_reserve,
                "chunk_queue_depth": chunk_queue_depth,
                "writing_chunks": writing_chunks,
                "transport_block_count": transport_block_count,
                "heap_floor_block_count": heap_floor_block_count,
                "budget_block_count": budget_block_count,
                "slice_done_count": slice_done_count,
                "peak_writer_latency_us": peak_writer_latency_us,
                "dirty_age_ms": dirty_age_ms,
                "max_chunk_backlog": max_chunk_backlog,
                "save_pending": save_pending,
                "save_in_progress": save_in_progress,
                "capture_active": capture_active,
            }
        )
    return rows


def extract_persistence_backlog_rows(lines: list[str]) -> list[dict[str, object]]:
    """Parse #CAP,<us>,PERS,backlog,... workspace drain estimate lines."""
    rows: list[dict[str, object]] = []
    for line in lines:
        if ",PERS,backlog," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 15:
            continue
        try:
            ts = int(parts[1])
            work_queue_depth = int(parts[4])
            writing_work_items = int(parts[5])
            chunk_queue_depth = int(parts[6])
            dirty_age_ms = int(parts[7])
            est_slice_steps = int(parts[8])
            est_sd_bytes = int(parts[9])
            save_pending = int(parts[10])
            urgent_requested = int(parts[11])
            transport_block_count = int(parts[12])
            budget_block_count = int(parts[13])
            heap_floor_block_count = int(parts[14])
        except ValueError:
            continue
        rows.append(
            {
                "timestamp": ts,
                "work_queue_depth": work_queue_depth,
                "writing_work_items": writing_work_items,
                "chunk_queue_depth": chunk_queue_depth,
                "dirty_age_ms": dirty_age_ms,
                "est_slice_steps": est_slice_steps,
                "est_sd_bytes": est_sd_bytes,
                "save_pending": save_pending,
                "urgent_requested": urgent_requested,
                "transport_block_count": transport_block_count,
                "budget_block_count": budget_block_count,
                "heap_floor_block_count": heap_floor_block_count,
            }
        )
    return rows


def extract_persistence_request_rows(lines: list[str]) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for line in lines:
        match = _PERS_REQUEST_RE.search(line)
        if not match:
            continue
        rows.append(
            {
                "timestamp": int(match.group(1)),
                "outcome": match.group(2).strip(),
            }
        )
    return rows


def extract_persistence_work_rows(lines: list[str]) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for line in lines:
        match = _PERS_WORK_RE.search(line)
        if not match:
            continue
        rows.append(
            {
                "timestamp": int(match.group(1)),
                "work_type": match.group(2).strip(),
                "key_label": match.group(3).strip(),
                "phase": match.group(4).strip(),
                "outcome": match.group(5).strip(),
            }
        )
    return rows


def extract_persistence_drain_rows(lines: list[str]) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for line in lines:
        match = _PERS_DRAIN_RE.search(line)
        if not match:
            continue
        rows.append(
            {
                "timestamp": int(match.group(1)),
                "reason": match.group(2).strip(),
                "steps": int(match.group(3)),
                "stuck_iterations": int(match.group(4)),
                "work_queue_depth": int(match.group(5)),
                "chunk_queue_depth": int(match.group(6)),
                "est_slice_steps": int(match.group(7)),
                "est_sd_bytes": int(match.group(8)),
            }
        )
    return rows


def extract_save_phase_rows(lines: list[str]) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for line in lines:
        match = _SAVE_PHASE_RE.search(line)
        if not match:
            continue
        rows.append(
            {
                "timestamp": int(match.group(1)),
                "phase": match.group(2).strip(),
                "rotate_step": int(match.group(3)),
            }
        )
    return rows


def count_clear_abort_lines(lines: list[str]) -> int:
    return sum(1 for line in lines if _CLEAR_ABORT_RE.search(line))
