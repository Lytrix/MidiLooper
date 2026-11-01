#!/usr/bin/env python3
"""Totals of the consumed-route / playback-miss telemetry (playback-tick-consume-only phase 1), read-only.

Reads `#CAP,<us>,DIAG,tick_route,view=,bridge=,paged=,merged=,none=,miss_int=,miss_ext=` rows (one per 5 s window,
deltas) and splits them by clock source using the firmware's own clock log lines:

  `External MIDI clock detected`               -> the following windows are external-clock
  `MIDI clock lost, switching to internal ...` -> the following windows are internal-clock

Windows before the first clock line are reported as `before_clock`. Nothing is guessed: a capture without rows, or whose
`RING,stats` reports Tier-A loss, is reported as unavailable / incomplete rather than as zero.

Phase 2 adds `#CAP,<us>,DIAG,route_reject,t=,s=,why=,cause=,prev=,prev_n=,since=,chg=,len=,prod=` -- one row when a
slot's merged-fallback reason (the first failed predicate, `PlaybackTickContext::RouteReject`) changes. Reported as a
per-slot timeline with names, and per reason the merged ticks attributed to it (`prev_n` of the following row, so the
reason a slot ends the capture in is listed as open).

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
_REJECT_RE = re.compile(r"^#CAP,(\d+),DIAG,route_reject,(.*)$")
_PROD_RE = re.compile(r"prod=([\w-]+)")

# `PlaybackTickContext::RouteReject`, append-only; keep in step with include/Utils/PlaybackTickContext.h.
REJECT_NAMES = (
    "not_evaluated", "served", "rolling_disabled",
    "inactive_slot", "note_edit_active", "overdub_no_live_window", "not_playing",
    "no_runtime", "frame_idle", "frame_blocked",
    "gate_not_active_slot", "gate_not_playing", "gate_jam", "gate_queued_start", "gate_note_edit",
    "gate_deferred_overdub_stop",
    "clock_no_epoch", "clock_epoch_changed", "clock_tick_skew", "generation_changed", "loop_length_changed",
    "projection_moved", "no_view_set", "cold_waiting",
    "serving_view_not_bound", "serving_view_not_published", "serving_view_source_stale", "serving_view_geometry",
    "serving_expired", "serving_uncovered", "cold_ready_off_boundary", "cold_ready_refused",
    "waiting_for_handoff", "handoff_missed", "candidate_not_ready", "handoff_phase_mismatch", "candidate_rejected",
    "adopt_refused",
)


def reject_name(code: int, cause: int = 0) -> str:
    name = REJECT_NAMES[code] if 0 <= code < len(REJECT_NAMES) else f"reason_{code}"
    if code == REJECT_NAMES.index("frame_blocked"):
        name += "(" + (REJECT_NAMES[cause] if 0 < cause < len(REJECT_NAMES) else "producer") + ")"
    return name


def decode(lines: Iterable[str]) -> dict:
    intervals: dict[str, dict[str, int]] = {}
    windows: dict[str, int] = {}
    clock = "before_clock"
    ring_stats: Optional[dict[str, int]] = None
    rejects: list[dict] = []
    for raw in lines:
        line = raw.rstrip("\r\n")
        m = _CLOCK_RE.match(line)
        if m:
            clock = "external" if m.group(2).startswith("External") else "internal"
            continue
        if line.startswith("#CAP,") and ",RING,stats," in line[:40]:
            ring_stats = {k: int(v) for k, v in _KV_RE.findall(line)}
            continue
        m = _REJECT_RE.match(line)
        if m:
            row = {k: int(v) for k, v in _KV_RE.findall(m.group(2))}
            prod = _PROD_RE.search(m.group(2))
            row.update(us=int(m.group(1)), clock=clock, prod=prod.group(1) if prod else "-")
            rejects.append(row)
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
    # Merged ticks per reason: each row's `prev_n` closes the slot's previous reason.
    by_reason: dict[str, int] = {}
    open_reasons: dict[tuple, str] = {}
    for r in rejects:
        key = (r.get("t"), r.get("s"))
        if key in open_reasons and r.get("prev_n", 0):
            by_reason[open_reasons[key]] = by_reason.get(open_reasons[key], 0) + r["prev_n"]
        open_reasons[key] = reject_name(r.get("why", 0), r.get("cause", 0))
    return {
        "route_reject": {"rows": rejects, "ticks_by_reason": by_reason,
                         "open": {f"t{k[0]}s{k[1]}": v for k, v in open_reasons.items()}},
        "available": bool(windows),
        "windows": windows,
        "intervals": intervals,
        "total": total,
        "ring_loss": loss,
        "complete": loss is not None and not any(loss.values()),
    }


def render_rejects(report: dict) -> list[str]:
    rr = report.get("route_reject") or {}
    rows = rr.get("rows") or []
    if not rows:
        return ["route_reject: unavailable (no DIAG,route_reject rows in this capture)"]
    out = ["route_reject timeline (first failed predicate when the merged fallback was selected):"]
    for r in rows:
        out.append(f"  {r['us'] / 1e6:10.3f}s {r['clock']:<12} t{r.get('t')} s{r.get('s')} len={r.get('len')} "
                   f"{reject_name(r.get('why', 0), r.get('cause', 0)):<40} since={r.get('since')} "
                   f"chg={r.get('chg')} prev_n={r.get('prev_n')} prod={r['prod']}")
    out.append("merged ticks by closed reason: " + (", ".join(
        f"{k}={v}" for k, v in sorted(rr["ticks_by_reason"].items(), key=lambda kv: -kv[1])) or "none closed"))
    out.append("open at end of capture: " + ", ".join(f"{k}={v}" for k, v in sorted(rr["open"].items())))
    return out


def render(report: dict) -> str:
    if not report["available"]:
        return "\n".join(["tick_route: unavailable (no DIAG,tick_route rows in this capture)"] + render_rejects(report))
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
    out.extend(render_rejects(report))
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
