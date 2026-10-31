#!/usr/bin/env python3
"""Score MIDI timing over the overdub span of an owner-rig chain capture (read-only).

The span runs from the first `Overdubbing started` line to the last `ST,Track,OVERDUB_STOPPING,PLAYING` row, so
runs of `scripts/hitl_midifile_chain.py` compare like for like. It reports the 5 s runtime-timing family (worst
MIDI-service gap per window and the median of those maxima, worst late NoteOn), display frames that held the loop
over 50 ms (`DIAG,loop_rem,display_frame`), committed-range window gathers of 10 ms or more (`DIAG,cer_win`), and
the display rebuild mix when the image carries it (`DIAG,disp_rebuild`, `DIAG,disp_passwalk`).

`--windows` also prints the per-window rows, which is how a regression is placed in time (before, during or after
the chain).

Usage:
  .venv/bin/python scripts/parse_overdub_chain_timing.py captures/session_A.log captures/session_B.log [--windows]
"""
from __future__ import annotations

import argparse
import re
import statistics
import sys
from pathlib import Path

SUMMARY_TAGS = ("midi_gap", "late_on", "idle_maint", "load_frame", "disp_rebuild", "disp_passwalk")


def read(path: Path):
    rows: list[tuple[float, list[str]]] = []
    starts: list[float] = []
    for line in path.read_text(errors="replace").splitlines():
        m = re.match(r"\[(\d+\.\d+)\].*Overdubbing started", line)
        if m:
            starts.append(float(m.group(1)))
        if line.startswith("#CAP,"):
            parts = line.split(",")
            try:
                rows.append((int(parts[1]) / 1e6, parts))
            except ValueError:
                pass
    rows.sort(key=lambda r: r[0])
    return rows, starts


def score(path: Path, windows: bool) -> None:
    rows, starts = read(path)
    ends = [t for t, p in rows if p[2:5] == ["ST", "Track", "OVERDUB_STOPPING"] and len(p) > 5 and p[5] == "PLAYING"]
    if not starts or not ends:
        print(f"{path.name}: no overdub span (overdub starts={len(starts)}); not a chain capture")
        return
    lo, hi = starts[0] - 0.5, max(ends)
    gaps, lates, frames, cer, full, inc, walks = [], [], [], 0, 0, 0, 0
    for t, p in rows:
        if not lo <= t <= hi or len(p) < 5 or p[2] != "DIAG":
            continue
        tag = p[3]
        if tag == "midi_gap" and p[4].isdigit():
            gaps.append(int(p[4]))
        elif tag == "late_on" and p[4].isdigit():
            lates.append(int(p[4]))
        elif tag == "loop_rem" and p[4] == "display_frame":
            frames.append(int(p[5]))
        elif tag == "cer_win":
            cer += 1
        elif tag == "disp_rebuild" and p[4].isdigit():
            full += int(p[4])
            inc += int(p[5])
        elif tag == "disp_passwalk" and p[4].isdigit():
            walks += int(p[4])
    ms = lambda v: v // 1000
    med = lambda v: ms(int(statistics.median(v))) if v else 0
    print(f"{path.name}: overdubs={len(starts)} span={hi - lo:.0f}s midi_gap med/max={med(gaps)}/{ms(max(gaps, default=0))}ms "
          f"late_on med/max={med(lates)}/{ms(max(lates, default=0))}ms frames>50ms={len(frames)} "
          f"(max {ms(max(frames, default=0))}ms) cer_win={cer} rebuild full/incr={full}/{inc} passwalk={walks}")
    if windows:
        table: dict[int, dict[str, int]] = {}
        for t, p in rows:
            if len(p) > 4 and p[2] == "DIAG" and p[3] in SUMMARY_TAGS and p[4].isdigit():
                table.setdefault(int(round(t / 5.0)) * 5, {})[p[3]] = int(p[4])
        for k in sorted(table):
            mark = "*" if lo - 5 <= k <= hi + 5 else " "
            cells = " ".join(f"{tag}={table[k].get(tag, 0) // (1 if tag.startswith('disp_') else 1000)}" for tag in SUMMARY_TAGS)
            print(f"  {mark} t~{k:4d}s {cells}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("logs", nargs="+", type=Path)
    ap.add_argument("--windows", action="store_true", help="print the per-5 s window rows (* = overdub span)")
    args = ap.parse_args()
    for log in args.logs:
        score(log, args.windows)
    return 0


if __name__ == "__main__":
    sys.exit(main())
