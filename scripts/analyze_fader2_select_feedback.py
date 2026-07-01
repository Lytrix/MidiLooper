#!/usr/bin/env python3
"""Correlate fader1 select with fader2 coarse outbound from a capture log."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


def decode_pitchbend(d1: int, d2: int) -> tuple[int, int, float]:
    wire = d1 | (d2 << 7)
    logical = wire - 8192
    percent = (logical + 8192) / 16384 * 100.0
    return wire, logical, percent


@dataclass
class SelectRow:
    wall_s: float
    slot: int
    f1_pb: int
    f2_pb: int | None
    f2_pct: float | None
    outbound_ctx: str | None


def parse_capture(lines: list[str]) -> list[SelectRow]:
    wall_s = 0.0
    rows: list[SelectRow] = []
    pending_ctx: str | None = None
    last_f2_pb: int | None = None

    for line in lines:
        wall_match = re.match(r"\[(\d+\.\d+)\]", line)
        if wall_match:
            wall_s = float(wall_match.group(1))

        ctx_match = re.search(
            r"#DBG outbound_ctx f2 anchor_tick=(\d+) rel_tick=(\d+) loop_start=(\d+) "
            r"loop_len=(\d+) pb=(-?\d+) expected_pb_rel=(-?\d+) step=(\d+) "
            r"f1_pb=(-?\d+) slot=(-?\d+) mode=(\S+)",
            line,
        )
        if ctx_match:
            pending_ctx = line.strip()

        cap_mo = re.match(r"#CAP,\d+,MO,224,14,(\d+),(\d+)", line)
        if cap_mo:
            _, logical, _ = decode_pitchbend(int(cap_mo.group(1)), int(cap_mo.group(2)))
            last_f2_pb = logical

        select_match = re.search(
            r"#DBG select_slot idx=(-?\d+) pitch=(-?\d+) ignored=(\d)", line
        )
        if select_match and select_match.group(3) == "0":
            slot = int(select_match.group(1))
            f1_pb = int(select_match.group(2))
            f2_pct = None
            if last_f2_pb is not None:
                f2_pct = (last_f2_pb + 8192) / 16384 * 100.0
            rows.append(
                SelectRow(
                    wall_s=wall_s,
                    slot=slot,
                    f1_pb=f1_pb,
                    f2_pb=last_f2_pb,
                    f2_pct=f2_pct,
                    outbound_ctx=pending_ctx,
                )
            )
            pending_ctx = None

    return rows


def summarize(rows: list[SelectRow]) -> dict[str, object]:
    anchor_rel_mismatch = 0
    for row in rows:
        if not row.outbound_ctx:
            continue
        m = re.search(
            r"anchor_tick=(\d+) rel_tick=(\d+) pb=(-?\d+) expected_pb_rel=(-?\d+)",
            row.outbound_ctx,
        )
        if not m:
            continue
        anchor = int(m.group(1))
        rel = int(m.group(2))
        pb = int(m.group(3))
        expected = int(m.group(4))
        if anchor != rel and pb != expected:
            anchor_rel_mismatch += 1

    f2_changes = 0
    prev_f2: int | None = None
    for row in rows:
        if row.f2_pb is not None and row.f2_pb != prev_f2:
            f2_changes += 1
            prev_f2 = row.f2_pb

    return {
        "select_events": len(rows),
        "f2_value_changes": f2_changes,
        "anchor_rel_mismatch_rows": anchor_rel_mismatch,
    }


def print_table(rows: list[SelectRow], limit: int) -> None:
    print(f"{'time':>8}  {'slot':>4}  {'F1 logical':>10}  {'F1 %':>6}  {'F2 logical':>10}  {'F2 %':>6}  delta")
    print("-" * 72)
    shown = 0
    prev_f2: int | None = None
    for row in rows:
        if shown >= limit:
            break
        f1_pct = (row.f1_pb + 8192) / 16384 * 100.0
        f2_pb = row.f2_pb if row.f2_pb is not None else 0
        f2_pct = row.f2_pct if row.f2_pct is not None else 0.0
        delta = (f2_pb - row.f1_pb) if row.f2_pb is not None else None
        delta_s = f"{delta:+d}" if delta is not None else "—"
        marker = ""
        if row.f2_pb is not None and row.f2_pb != prev_f2:
            marker = " *"
            prev_f2 = row.f2_pb
        print(
            f"{row.wall_s:8.3f}  {row.slot:4d}  {row.f1_pb:10d}  {f1_pct:5.1f}%  "
            f"{f2_pb:10d}  {f2_pct:5.1f}%  {delta_s}{marker}"
        )
        shown += 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="Capture log path")
    parser.add_argument("--limit", type=int, default=30, help="Rows to print (default 30)")
    parser.add_argument("--after", type=float, default=0.0, help="Only rows after wall time (seconds)")
    parser.add_argument(
        "--mi-strict",
        action="store_true",
        help="Exit code 3 when mo_mi_f2_miss_rate > 0 after note_changed apply",
    )
    args = parser.parse_args()

    if not args.log.is_file():
        print(f"Not found: {args.log}", file=sys.stderr)
        return 1

    lines = args.log.read_text(errors="replace").splitlines()
    rows = [r for r in parse_capture(lines) if r.wall_s >= args.after]

    print(f"Log: {args.log}")
    print(f"Summary: {summarize(rows)}")
    print()

    scripts_dir = Path(__file__).resolve().parent
    if str(scripts_dir) not in sys.path:
        sys.path.insert(0, str(scripts_dir))
    exit_code = 0
    try:
        from hitl.verify.note_edit_fader_select_refresh import verify_note_edit_fader_select_refresh
        from hitl.verify.fader_select_dwell_gap import verify_fader_select_dwell_gap
        from hitl.verify.fader_motor_echo_correlation import verify_fader_motor_echo_correlation
        from hitl.verify.fader_select_sibling_sync import verify_fader_select_sibling_sync

        refresh = verify_note_edit_fader_select_refresh(lines)
        dwell = verify_fader_select_dwell_gap(lines)
        echo = verify_fader_motor_echo_correlation(lines)
        sibling = verify_fader_select_sibling_sync(lines)
        print("HITL refresh verifier:", refresh)
        print("Dwell-gap verifier:", dwell)
        print("Motor echo correlator:", echo)
        print("Sibling sync verifier:", sibling)
        print()
        if not dwell.get("fader_select_dwell_gap_ok", True):
            exit_code = 2
        if not sibling.get("fader_select_sibling_sync_ok", True):
            exit_code = 4
        if args.mi_strict:
            miss_rate = float(echo.get("mo_mi_f2_miss_rate", 0.0))
            note_changed = int(echo.get("note_changed_count", 0))
            if note_changed > 0 and miss_rate > 0:
                exit_code = 3
    except ImportError:
        pass

    print_table(rows, args.limit)
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
