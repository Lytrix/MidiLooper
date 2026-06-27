"""Serial verification for CurrentSet incremental deferred-save slot-skip policy."""

from __future__ import annotations

import re
from typing import Optional

_RESULT_OK_RE = re.compile(r"#CAP,\d+,PERS,result,\d+,\d+,\d+,ok\b")
_RESULT_STATS_RE = re.compile(
    r"#CAP,\d+,PERS,result_stats,\d+,\d+,\d+,w(\d+)_s(\d+)_db(\d+)\b"
)


def extract_persistence_result_stats(lines: list[str]) -> list[dict[str, int]]:
    stats: list[dict[str, int]] = []
    for line in lines:
        match = _RESULT_STATS_RE.search(line)
        if match is None:
            continue
        stats.append(
            {
                "writes": int(match.group(1)),
                "skips": int(match.group(2)),
                "display_block_us": int(match.group(3)),
            }
        )
    return stats


def find_result_stats_after_line(
    lines: list[str],
    after_line_index: int,
) -> Optional[dict[str, int]]:
    start = max(0, after_line_index)
    for line in lines[start:]:
        match = _RESULT_STATS_RE.search(line)
        if match is not None:
            return {
                "writes": int(match.group(1)),
                "skips": int(match.group(2)),
                "display_block_us": int(match.group(3)),
            }
    return None


def persistence_result_ok_after_line(lines: list[str], after_line_index: int) -> bool:
    start = max(0, after_line_index)
    for line in lines[start:]:
        if _RESULT_OK_RE.search(line):
            return True
    return False


def verify_current_set_incremental_save(lines: list[str], args: object) -> dict[str, object]:
    issues: list[str] = []
    transport_anchor = int(getattr(args, "transport_stop_serial_anchor", 0) or 0)
    record_anchor = int(getattr(args, "record_stop_serial_anchor", -1) or -1)

    transport_ok = persistence_result_ok_after_line(lines, transport_anchor)
    if not transport_ok:
        issues.append("missing_pers_result_ok_after_transport_stop")

    transport_stats = find_result_stats_after_line(lines, transport_anchor)
    if transport_stats is None:
        issues.append("missing_result_stats_after_transport_stop")
    else:
        if transport_stats["writes"] != 0:
            issues.append(
                f"transport_stop_writes_expected_0_got_{transport_stats['writes']}"
            )
        if transport_stats["skips"] != 64:
            issues.append(
                f"transport_stop_skips_expected_64_got_{transport_stats['skips']}"
            )

    verify_record = bool(getattr(args, "verify_record_incremental_save", True))
    record_stats: Optional[dict[str, int]] = None
    if verify_record and record_anchor >= 0:
        record_ok = persistence_result_ok_after_line(lines, record_anchor)
        if not record_ok:
            issues.append("missing_pers_result_ok_after_record_stop")
        record_stats = find_result_stats_after_line(lines, record_anchor)
        if record_stats is None:
            issues.append("missing_result_stats_after_record_stop")
        else:
            if record_stats["writes"] != 1:
                issues.append(
                    f"record_stop_writes_expected_1_got_{record_stats['writes']}"
                )
            if record_stats["skips"] != 63:
                issues.append(
                    f"record_stop_skips_expected_63_got_{record_stats['skips']}"
                )

    return {
        "ok": not issues,
        "issues": issues,
        "transport_stop_stats": transport_stats,
        "record_stop_stats": record_stats,
        "all_result_stats": extract_persistence_result_stats(lines),
        "transport_stop_serial_anchor": transport_anchor,
        "record_stop_serial_anchor": record_anchor,
    }
