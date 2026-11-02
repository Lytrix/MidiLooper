#!/usr/bin/env python3
"""Per-phase window-lane cadence from one capture log (DEC-246 amendment 25; STIM-LANE-CADENCE-TEMP-01).

  scripts/parse_lane_cadence.py <session.log> [--bpm 300] [--json out.json]

Phases come from the device's **live logger lines** (Tier-A rows are retained in the ring while the transport runs, so
their position in the file is not their time; their `#CAP,<micros>` stamp is, and the logger's `[s.ms]` prefix is the
same clock):

  record   "Transport started" (the armed take starts on it) .. "Stop Recording"
  overdub  "Overdub session opened" .. the next slot short press (the stream's stop) or stop line
  play     the overdub's end .. "Transport stopped"

Per phase: `win_gap` schema 4 (`span16` / `span128` ticks and the 128-span microseconds, the worst gap after an
eligible turn), `lane_fair` (per track: the longest denial streak and the worst ticks between its served grains), and
the cumulative `succ` per-track counters at the phase's end (they are since boot; the maxima cannot be split by phase).
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

CAP_RE = re.compile(r"^#CAP,(\d+),DIAG,(win_gap|lane_fair|succ),(.*)$")
INFO_RE = re.compile(r"^\[(\d+)\.(\d+)\] \[(?:INFO|WARN)\] (.*)$")


def _us(sec: str, frac: str) -> int:
    return int(sec) * 1_000_000 + int(frac.ljust(3, "0")[:3]) * 1000


def phases(lines):
    events = []
    for line in lines:
        m = INFO_RE.match(line.strip())
        if m:
            events.append((_us(m.group(1), m.group(2)), m.group(3)))
    events.sort()
    def first(pred, after=0):
        for t, text in events:
            if t >= after and pred(text):
                return t
        return None
    start = first(lambda x: "Transport started" in x)
    rec_end = first(lambda x: "Stop Recording" in x, start or 0)
    od_start = first(lambda x: "Overdub session opened" in x, rec_end or 0)
    # The overdub ends on the stream's own stop press (the slot's short press, logged live); the stop's own line may be
    # retained or absent, so the press is the boundary when it comes first.
    od_end = first(lambda x: any(k in x for k in ("Stop Overdub", "Overdub stopped", "capture hot stop",
                                                  "Button press: Loop 4 (short)")), (od_start or 0) + 1) \
        if od_start else None
    stop = first(lambda x: "Transport stopped" in x, od_end or rec_end or start or 0)
    out = {}
    if start and rec_end:
        out["record"] = (start, rec_end)
    if od_start and od_end:
        out["overdub"] = (od_start, od_end)
    if od_end and stop and stop > od_end:
        out["play"] = (od_end, stop)
    elif rec_end and stop and not od_start:
        out["play"] = (rec_end, stop)
    return out


def rows(lines):
    out = []
    for line in lines:
        m = CAP_RE.match(line.strip())
        if m:
            out.append((int(m.group(1)), m.group(2), m.group(3).split(",")))
    out.sort(key=lambda r: r[0])
    return out


def q(values, k):
    if not values:
        return None
    s = sorted(values)
    return s[min(len(s) - 1, int(k * len(s)))]


def summarize(lines, bpm):
    ph = phases(lines)
    rs = rows(lines)
    ticks_per_s = bpm * 192 / 60.0
    report = {"bpm": bpm, "ticks_per_second": ticks_per_s, "phases": {}}
    for name, (t0, t1) in ph.items():
        gaps = [f for t, kind, f in rs if kind == "win_gap" and t0 <= t <= t1 and len(f) >= 19 and f[15] == "4"
                and f[3] == "1"]
        s16 = [int(f[16]) for f in gaps if int(f[16]) > 0]
        s128 = [int(f[17]) for f in gaps if int(f[17]) > 0]
        s128us = [int(f[18]) for f in gaps if int(f[18]) > 0]
        elig = [int(f[5]) for f in gaps]
        fair = [f for t, kind, f in rs if kind == "lane_fair" and t0 <= t <= t1 and len(f) >= 18]
        per_track = {}
        for tr in range(8):
            streaks = [int(f[2 + 2 * tr]) for f in fair]
            inter = [int(f[3 + 2 * tr]) for f in fair]
            if any(streaks) or any(inter):
                per_track[tr + 1] = {"max_denied_streak": max(streaks), "p99_denied_streak": q(streaks, .99),
                                     "max_inter_grain_ticks": max(inter), "p99_inter_grain_ticks": q(inter, .99)}
        succ = {}
        for t, kind, f in rs:
            if kind == "succ" and t <= t1 and len(f) >= 10:
                succ[int(f[0]) + 1] = dict(requested=int(f[1]), handed_off=int(f[2]), missed=int(f[3]),
                                           cancelled=int(f[4]), price_refused=int(f[5]),
                                           max_request_to_ready_ticks=int(f[6]), max_request_to_admit_ticks=int(f[7]),
                                           min_ready_margin_ticks=int(f[8]), max_priced_grains=int(f[9]))
        report["phases"][name] = {
            "device_s": [round(t0 / 1e6, 3), round(t1 / 1e6, 3)],
            "seams_2bar": round((t1 - t0) / 1e6 * ticks_per_s / 1536, 1),
            "win_gap_blocks": len(gaps),
            "span16_ticks": {"p50": q(s16, .5), "p99": q(s16, .99), "max": max(s16) if s16 else None},
            "span128_ticks": {"p50": q(s128, .5), "p99": q(s128, .99), "max": max(s128) if s128 else None},
            "span128_ms_max": round(max(s128us) / 1000, 1) if s128us else None,
            "eligible_gap_ticks_max": max(elig) if elig else None,
            "lane_fair_blocks": len(fair),
            "per_track": per_track,
            "succ_cumulative_at_end": succ,
        }
    return report


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log", type=Path)
    ap.add_argument("--bpm", type=float, default=300.0)
    ap.add_argument("--json", type=Path)
    args = ap.parse_args(argv)
    lines = args.log.read_text(errors="replace").splitlines()
    report = summarize(lines, args.bpm)
    text = json.dumps(report, indent=1)
    if args.json:
        args.json.write_text(text + "\n")
    print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
