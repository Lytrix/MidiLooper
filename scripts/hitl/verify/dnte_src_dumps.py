"""Parse SESSION_CAPTURE reconstruct / effective / frame DNTE dumps."""

from __future__ import annotations

import re
from typing import Any, Optional

_DNTE_SRC_HEADER_RE = re.compile(
    r"DIAG,dnte,src,(reconstruct|effective|frame),count,(\d+)"
)
_DUMP_KINDS = ("reconstruct", "effective", "frame")


def _cap_ts(line: str) -> Optional[int]:
    if "#CAP," not in line:
        return None
    parts = line.split(",")
    if len(parts) < 2:
        return None
    try:
        return int(parts[1])
    except ValueError:
        return None


def _parse_dnte_row(line: str) -> Optional[dict[str, int]]:
    if ",DNTE," not in line or "#CAP," not in line:
        return None
    parts = line.split(",")
    if len(parts) < 8:
        return None
    try:
        return {
            "ts": int(parts[1]),
            "pitch": int(parts[3]),
            "start": int(parts[4]),
            "display_start": int(parts[5]),
            "length": int(parts[6]),
            "idx": int(parts[7]),
        }
    except ValueError:
        return None


def parse_dnte_src_dumps(lines: list[str]) -> list[dict[str, Any]]:
    """Headers plus following DNTE rows, in log order."""
    dumps: list[dict[str, Any]] = []
    current: Optional[dict[str, Any]] = None
    for line in lines:
        header = _DNTE_SRC_HEADER_RE.search(line)
        if header is not None:
            if current is not None:
                dumps.append(current)
            current = {
                "kind": header.group(1),
                "ts": _cap_ts(line),
                "expected": int(header.group(2)),
                "rows": [],
            }
            if current["expected"] == 0:
                dumps.append(current)
                current = None
            continue
        if current is None:
            continue
        row = _parse_dnte_row(line)
        if row is None:
            continue
        current["rows"].append(row)
        if len(current["rows"]) >= current["expected"]:
            dumps.append(current)
            current = None
    if current is not None:
        dumps.append(current)
    return dumps


def dump_is_complete(dump: dict[str, Any]) -> bool:
    return len(dump.get("rows") or []) >= int(dump.get("expected") or 0)


def last_complete_dump(
    dumps: list[dict[str, Any]],
    kind: str,
    *,
    after_ts: Optional[int] = None,
    max_expected: Optional[int] = None,
) -> Optional[dict[str, Any]]:
    chosen: Optional[dict[str, Any]] = None
    for dump in dumps:
        if dump.get("kind") != kind or not dump_is_complete(dump):
            continue
        ts = dump.get("ts")
        if after_ts is not None and ts is not None and ts < after_ts:
            continue
        expected = int(dump.get("expected") or 0)
        if max_expected is not None and expected > max_expected:
            continue
        chosen = dump
    return chosen


def dnte_src_dump_trio_complete(
    lines: list[str],
    *,
    after_ts: Optional[int] = None,
    max_expected: Optional[int] = None,
    required_kinds: tuple[str, ...] = _DUMP_KINDS,
) -> bool:
    dumps = parse_dnte_src_dumps(lines)
    kinds = required_kinds if required_kinds else _DUMP_KINDS
    return all(
        last_complete_dump(
            dumps,
            kind,
            after_ts=after_ts,
            max_expected=max_expected,
        )
        is not None
        for kind in kinds
    )


def lengths_by_pitch_start(rows: list[dict[str, int]]) -> dict[tuple[int, int], set[int]]:
    grouped: dict[tuple[int, int], set[int]] = {}
    for row in rows:
        key = (int(row["pitch"]), int(row["start"]))
        grouped.setdefault(key, set()).add(int(row["length"]))
    return grouped


def two_length_pairs(rows: list[dict[str, int]]) -> list[dict[str, Any]]:
    pairs: list[dict[str, Any]] = []
    for (pitch, start), lengths in sorted(lengths_by_pitch_start(rows).items()):
        if len(lengths) < 2:
            continue
        pairs.append({"pitch": pitch, "start": start, "lengths": sorted(lengths)})
    return pairs


def frame_effective_length_mismatches(
    frame_rows: list[dict[str, int]],
    effective_rows: list[dict[str, int]],
) -> list[dict[str, Any]]:
    """Same pitch+start in both dumps with disagreeing single lengths."""
    frame_map = lengths_by_pitch_start(frame_rows)
    effective_map = lengths_by_pitch_start(effective_rows)
    mismatches: list[dict[str, Any]] = []
    for key in sorted(set(frame_map) & set(effective_map)):
        frame_lengths = frame_map[key]
        effective_lengths = effective_map[key]
        if frame_lengths == effective_lengths:
            continue
        mismatches.append(
            {
                "pitch": key[0],
                "start": key[1],
                "frame": sorted(frame_lengths),
                "effective": sorted(effective_lengths),
            }
        )
    return mismatches
