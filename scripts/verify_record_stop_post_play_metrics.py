#!/usr/bin/env python3
"""Verify record-stop → PLAYING metrics from a #CAP serial log."""

from __future__ import annotations

import argparse
import re
import sys
from collections import defaultdict


def parse_play_ts(lines: list[str]) -> int | None:
    for line in lines:
        if "#CAP," in line and ",ST,Track,STOPPED_RECORDING,PLAYING" in line:
            m = re.search(r"#CAP,(\d+),ST,Track,STOPPED_RECORDING,PLAYING", line)
            if m:
                return int(m.group(1))
    return None


def parse_coord_alignment(lines: list[str], play_ts: int, window_us: int) -> dict[str, object]:
    coord_re = re.compile(
        r"#CAP,\d+,COORD,abs,\d+,storage,(\d+),proj,(\d+),display,(\d+)"
    )
    max_delta = 0
    samples = 0
    for line in lines:
        if "#CAP," not in line:
            continue
        m_ts = re.search(r"#CAP,(\d+),", line)
        if not m_ts:
            continue
        ts = int(m_ts.group(1))
        if not (play_ts <= ts < play_ts + window_us):
            continue
        m = coord_re.search(line)
        if not m:
            continue
        storage, proj, display = map(int, m.groups())
        delta = max(abs(storage - proj), abs(storage - display), abs(proj - display))
        max_delta = max(max_delta, delta)
        samples += 1
    return {
        "samples": samples,
        "max_delta": max_delta,
        "aligned": samples == 0 or max_delta <= 1,
    }


def analyze(path: str, window_us: int = 2_000_000) -> dict[str, object]:
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = f.readlines()

    play_ts = parse_play_ts(lines)
    if play_ts is None:
        return {"ok": False, "error": "no ST,Track,STOPPED_RECORDING,PLAYING transition"}

    disp = 0
    pers_dispatch = 0
    pers_mid_pass = 0
    mo_ch: dict[int, int] = defaultdict(int)
    rapid_reon = 0
    prev_on: dict[tuple[int, int], int] = {}

    for line in lines:
        if "#CAP," not in line:
            continue
        m = re.search(r"#CAP,(\d+),", line)
        if not m:
            continue
        ts = int(m.group(1))
        if not (play_ts <= ts < play_ts + window_us):
            continue
        if ",DISP," in line:
            disp += 1
        if ",PERS,dispatch," in line:
            pers_dispatch += 1
        if ",PERS,mid_pass," in line:
            pers_mid_pass += 1
        mo = re.search(r"#CAP,\d+,MO,(\d+),(\d+),(\d+),", line)
        if mo:
            status, ch, note = map(int, mo.groups())
            if status == 144:
                mo_ch[ch] += 1
                key = (ch, note)
                if key in prev_on and ts - prev_on[key] < 50_000:
                    rapid_reon += 1
                prev_on[key] = ts

    mo_total = sum(mo_ch.values())
    dur_s = window_us / 1_000_000
    mo_rate = mo_total / dur_s if dur_s else 0.0
    ch4_rate = mo_ch.get(4, 0) / dur_s if dur_s else 0.0
    coord = parse_coord_alignment(lines, play_ts, window_us)

    checks = {
        "disp_ge_5": disp >= 5,
        "pers_dispatch_none": pers_dispatch == 0,
        "mo_rate_le_50": mo_rate <= 50,
        "ch4_rapid_reon_near_zero": rapid_reon <= 5,
        "coord_aligned": coord["aligned"],
    }
    return {
        "ok": all(checks.values()),
        "play_ts": play_ts,
        "window_s": dur_s,
        "disp": disp,
        "pers_dispatch": pers_dispatch,
        "pers_mid_pass": pers_mid_pass,
        "mo_total": mo_total,
        "mo_rate_per_s": round(mo_rate, 1),
        "ch4_mo_rate_per_s": round(ch4_rate, 1),
        "rapid_reon_ch4": rapid_reon,
        "coord_samples": coord["samples"],
        "coord_max_delta": coord["max_delta"],
        "checks": checks,
        "mo_by_channel": dict(sorted(mo_ch.items())),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", help="Path to session_*.log")
    parser.add_argument("--window-s", type=float, default=2.0)
    args = parser.parse_args()
    result = analyze(args.log, int(args.window_s * 1_000_000))
    if "error" in result:
        print(f"FAIL: {result['error']}", file=sys.stderr)
        return 2
    print(f"log: {args.log}")
    print(f"play_ts: {result['play_ts']}")
    print(f"DISP in {result['window_s']}s: {result['disp']} (need >= 5)")
    print(f"PERS,dispatch in window: {result['pers_dispatch']} (need 0)")
    print(f"PERS,mid_pass in window: {result['pers_mid_pass']}")
    print(f"MO rate/s: {result['mo_rate_per_s']} (need <= 50)")
    print(f"ch4 MO rate/s: {result['ch4_mo_rate_per_s']}")
    print(f"rapid re-on (<50ms): {result['rapid_reon_ch4']} (need <= 5)")
    print(
        f"COORD alignment: samples={result['coord_samples']} "
        f"max_delta={result['coord_max_delta']} (need <= 1)"
    )
    print(f"MO by channel: {result['mo_by_channel']}")
    for name, passed in result["checks"].items():
        print(f"  {'PASS' if passed else 'FAIL'}: {name}")
    print("OVERALL:", "PASS" if result["ok"] else "FAIL")
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
