"""OpenSpec 5.6 — large-loop persist progress, bounds, and active snapshot during save."""

from __future__ import annotations

import re
from typing import Optional

from hitl.persistence_rows import extract_persistence_work_rows

_LOOP_PERSIST_WORK_RE = re.compile(
    r"#CAP,(\d+),PERS,work,(\d+),\d+,\d+,LoopPersist,([^,]+),([^,]+),"
)
_PERSIST_SAVE_RE = re.compile(r"#CAP,(\d+),DIAG,persist_save,(\d+),")
_PLAYBACK_SNAPSHOT_RE = re.compile(r"#CAP,(\d+).*playback_build,snapshot")
_MO_RE = re.compile(r"#CAP,(\d+),MO,")
_ST_TRACK_RE = re.compile(r"#CAP,(\d+),ST,Track,[^,]+,([^,\s]+)")
_RECORD_STOP_STAGE_RE = re.compile(
    r"#CAP,(\d+),RECS,stage,([^,]+),\d+,(\d+),"
)

_PLAYBACK_ACTIVE_STATES = frozenset({"PLAYING", "OVERDUBBING"})
# Short persist windows can sit between MO ticks while PLAYING continues.
_ACTIVE_SNAPSHOT_PAD_US = 500_000

_REQUIRED_LOOP_PERSIST_PHASES = (
    "start",
    "payload_slice",
    "payload_done",
    "finalize_slice",
    "done",
)

# Empirically bounded SD indivisible steps (VerifyAndRename) — not content-sized full-loop sync.
_MAX_PERSIST_SLICE_US = 100_000
_MAX_RECORD_STOP_STAGE_US = 100_000


def _track_state_intervals(lines: list[str]) -> list[tuple[int, int, str]]:
    transitions: list[tuple[int, str]] = []
    for line in lines:
        match = _ST_TRACK_RE.search(line)
        if not match:
            continue
        transitions.append((int(match.group(1)), match.group(2)))
    if not transitions:
        return []
    transitions.sort(key=lambda item: item[0])
    intervals: list[tuple[int, int, str]] = []
    for index, (start_ts, state) in enumerate(transitions):
        end_ts = (
            transitions[index + 1][0] - 1
            if index + 1 < len(transitions)
            else 2**63 - 1
        )
        intervals.append((start_ts, end_ts, state))
    return intervals


def _playback_active_during_window(
    intervals: list[tuple[int, int, str]],
    *,
    start_ts: int,
    done_ts: int,
) -> bool:
    for interval_start, interval_end, state in intervals:
        if state not in _PLAYBACK_ACTIVE_STATES:
            continue
        overlap_start = max(start_ts, interval_start)
        overlap_end = min(done_ts, interval_end)
        if overlap_start <= overlap_end:
            return True
    return False


def _loop_persist_windows(
    lines: list[str],
    *,
    record_stop_ts: Optional[int],
) -> list[dict[str, object]]:
    work = extract_persistence_work_rows(lines)
    if record_stop_ts is not None:
        work = [row for row in work if int(row["timestamp"]) >= record_stop_ts]
    windows: list[dict[str, object]] = []
    current: Optional[dict[str, object]] = None
    for row in work:
        if str(row.get("work_type", "")) != "LoopPersist":
            continue
        phase = str(row.get("phase", ""))
        ts = int(row["timestamp"])
        if phase == "start":
            if current is not None and current.get("done_ts") is None:
                windows.append(current)
            current = {
                "key_label": str(row.get("key_label", "")),
                "start_ts": ts,
                "done_ts": None,
                "phases": set(),
            }
        if current is None:
            continue
        current["phases"].add(phase)
        if phase == "done":
            current["done_ts"] = ts
            windows.append(current)
            current = None
    if current is not None:
        windows.append(current)
    return windows


def summarize_deferred_persist_large_loop_bounds(
    lines: list[str],
    *,
    record_stop_ts: Optional[int] = None,
) -> dict[str, object]:
    windows = _loop_persist_windows(lines, record_stop_ts=record_stop_ts)
    complete_windows = [w for w in windows if w.get("done_ts") is not None]

    progress_ok = bool(complete_windows) and all(
        set(_REQUIRED_LOOP_PERSIST_PHASES).issubset(set(w.get("phases") or set()))
        for w in complete_windows
    )

    max_pers_work_us = 0
    for line in lines:
        match = _LOOP_PERSIST_WORK_RE.search(line)
        if not match:
            continue
        ts = int(match.group(1))
        if record_stop_ts is not None and ts < record_stop_ts:
            continue
        max_pers_work_us = max(max_pers_work_us, int(match.group(2)))

    max_persist_save_us = 0
    for line in lines:
        match = _PERSIST_SAVE_RE.search(line)
        if not match:
            continue
        ts = int(match.group(1))
        if record_stop_ts is not None and ts < record_stop_ts:
            continue
        max_persist_save_us = max(max_persist_save_us, int(match.group(2)))

    slice_bounds_ok = (
        max_pers_work_us <= _MAX_PERSIST_SLICE_US
        and max_persist_save_us <= _MAX_PERSIST_SLICE_US
    )

    state_intervals = _track_state_intervals(lines)
    active_snapshot_windows = 0
    playback_active_windows = 0
    for window in complete_windows:
        start_ts = int(window["start_ts"])
        done_ts = int(window["done_ts"])
        if not _playback_active_during_window(
            state_intervals,
            start_ts=start_ts,
            done_ts=done_ts,
        ):
            continue
        playback_active_windows += 1
        pad_start = max(0, start_ts - _ACTIVE_SNAPSHOT_PAD_US)
        pad_done = done_ts + _ACTIVE_SNAPSHOT_PAD_US
        has_playback = False
        for line in lines:
            match = _PLAYBACK_SNAPSHOT_RE.search(line)
            if not match:
                continue
            ts = int(match.group(1))
            if pad_start <= ts <= pad_done:
                has_playback = True
                break
        mo_count = 0
        for line in lines:
            match = _MO_RE.search(line)
            if not match:
                continue
            ts = int(match.group(1))
            if pad_start <= ts <= pad_done:
                mo_count += 1
        if has_playback or mo_count > 0:
            active_snapshot_windows += 1
    active_snapshot_ok = (
        playback_active_windows == 0
        or active_snapshot_windows == playback_active_windows
    )

    stop_stage_rows: list[dict[str, object]] = []
    for line in lines:
        match = _RECORD_STOP_STAGE_RE.search(line)
        if not match:
            continue
        stop_stage_rows.append(
            {
                "timestamp": int(match.group(1)),
                "stage": match.group(2),
                "duration_us": int(match.group(3)),
            }
        )

    save_request_rows = [
        row for row in stop_stage_rows if str(row.get("stage", "")) == "save_request"
    ]
    save_request_deferred = any(
        ",RECS,stage,save_request," in line and line.rstrip().endswith(",requested")
        for line in lines
    )
    slowest_stop_stage_us = max(
        (int(row["duration_us"]) for row in stop_stage_rows),
        default=0,
    )
    stop_path_ok = (
        save_request_deferred
        and slowest_stop_stage_us <= _MAX_RECORD_STOP_STAGE_US
    )

    return {
        "loop_persist_windows": len(complete_windows),
        "progress_ok": progress_ok,
        "max_pers_work_us": max_pers_work_us,
        "max_persist_save_us": max_persist_save_us,
        "slice_bounds_ok": slice_bounds_ok,
        "active_snapshot_windows": active_snapshot_windows,
        "playback_active_persist_windows": playback_active_windows,
        "active_snapshot_ok": active_snapshot_ok,
        "save_request_deferred": save_request_deferred,
        "slowest_record_stop_stage_us": slowest_stop_stage_us,
        "stop_path_ok": stop_path_ok,
        "ok": progress_ok and slice_bounds_ok and active_snapshot_ok and stop_path_ok,
    }


def deferred_persist_large_loop_bounds_issues(summary: dict[str, object]) -> list[str]:
    issues: list[str] = []
    if not summary.get("progress_ok"):
        issues.append("persistence_loop_persist_progress_incomplete")
    if not summary.get("slice_bounds_ok"):
        if int(summary.get("max_pers_work_us") or 0) > _MAX_PERSIST_SLICE_US:
            issues.append("persistence_pers_work_slice_too_large")
        if int(summary.get("max_persist_save_us") or 0) > _MAX_PERSIST_SLICE_US:
            issues.append("persistence_persist_save_slice_too_large")
    if not summary.get("active_snapshot_ok"):
        issues.append("persistence_active_snapshot_unavailable_during_save")
    if not summary.get("stop_path_ok"):
        if not summary.get("save_request_deferred"):
            issues.append("persistence_stop_save_not_deferred")
        elif int(summary.get("slowest_record_stop_stage_us") or 0) > _MAX_RECORD_STOP_STAGE_US:
            issues.append("persistence_stop_stage_content_sized")
    return issues
