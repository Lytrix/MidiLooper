#!/usr/bin/env python3
"""Totals of the consumed-route / playback-miss telemetry (playback-tick-consume-only phase 1), read-only.

Reads `#CAP,<us>,DIAG,tick_route,view=,bridge=,paged=,merged=,none=,miss_int=,miss_ext=` rows (one per 5 s window,
deltas) and splits them by clock source using the firmware's own clock log lines:

  `External MIDI clock detected`               -> the following windows are external-clock
  `MIDI clock lost, switching to internal ...` -> the following windows are internal-clock

Windows before the first clock line are reported as `before_clock`. Nothing is guessed: a capture without rows, or whose
`RING,stats` reports Tier-A loss, is reported as unavailable / incomplete rather than as zero.

Usage:
    python3 scripts/parse_tick_routes.py captures/session_YYYYMMDD_HHMMSS.log [--json]
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Iterable, Optional

FIELDS = ("view", "bridge", "paged", "merged", "none", "miss_int", "miss_ext")
_ROW_RE = re.compile(r"^#CAP,(\d+),DIAG,tick_route,(.*)$")
_KV_RE = re.compile(r"(\w+)=(\d+)")
_CLOCK_RE = re.compile(r"^\[(\d+(?:\.\d+)?)\].*(External MIDI clock detected|MIDI clock lost, switching to internal)")
_LOSS_FIELDS = ("tierA_evict", "overflow", "crit_rej", "wipe")


def decode(lines: Iterable[str]) -> dict:
    intervals: dict[str, dict[str, int]] = {}
    windows: dict[str, int] = {}
    clock = "before_clock"
    ring_stats: Optional[dict[str, int]] = None
    for raw in lines:
        line = raw.rstrip("\r\n")
        m = _CLOCK_RE.match(line)
        if m:
            clock = "external" if m.group(2).startswith("External") else "internal"
            continue
        if line.startswith("#CAP,") and ",RING,stats," in line[:40]:
            ring_stats = {k: int(v) for k, v in _KV_RE.findall(line)}
            continue
        m = _ROW_RE.match(line)
        if not m:
            continue
        values = {k: int(v) for k, v in _KV_RE.findall(m.group(2))}
        bucket = intervals.setdefault(clock, {f: 0 for f in FIELDS})
        for f in FIELDS:
            bucket[f] += values.get(f, 0)
        windows[clock] = windows.get(clock, 0) + 1
    total = {f: sum(b[f] for b in intervals.values()) for f in FIELDS}
    loss = None if ring_stats is None else {k: ring_stats.get(k, 0) for k in _LOSS_FIELDS}
    return {
        "available": bool(windows),
        "windows": windows,
        "intervals": intervals,
        "total": total,
        "ring_loss": loss,
        "complete": loss is not None and not any(loss.values()),
    }


def render(report: dict) -> str:
    if not report["available"]:
        return "tick_route: unavailable (no DIAG,tick_route rows in this capture)"
    out = []
    if report["ring_loss"] is None:
        out.append("capture ring: no RING,stats row -- totals may be incomplete")
    elif not report["complete"]:
        out.append(f"capture ring: LOST ROWS {report['ring_loss']} -- totals are a lower bound")
    out.append(f"{'interval':<13}{'windows':>8}" + "".join(f"{f:>11}" for f in FIELDS))
    for name in ("before_clock", "external", "internal"):
        if name in report["intervals"]:
            b = report["intervals"][name]
            out.append(f"{name:<13}{report['windows'][name]:>8}" + "".join(f"{b[f]:>11}" for f in FIELDS))
    t = report["total"]
    out.append(f"{'total':<13}{sum(report['windows'].values()):>8}" + "".join(f"{t[f]:>11}" for f in FIELDS))
    consumed = sum(t[f] for f in ("view", "bridge", "paged", "merged", "none"))
    if consumed:
        out.append(f"merged share {100.0 * t['merged'] / consumed:.1f}% of {consumed} slot-ticks; "
                   f"misses: interrupt {t['miss_int']}, external {t['miss_ext']}")
    return "\n".join(out)


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("log", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    with args.log.open(encoding="utf-8", errors="replace") as fh:
        report = decode(fh)
    print(json.dumps(report, indent=2) if args.json else render(report))
    return 0


if __name__ == "__main__":
    sys.exit(main())
