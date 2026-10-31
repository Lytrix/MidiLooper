#!/usr/bin/env python3
"""Decode runtime-profiler rows from a capture log (DEC-239, read-only, no device).

Reads only the `#CAP,...,DIAG,profile_*` rows and the capture ring's own `RING,stats` row, and prints:

- the boot row (whether the profiler was armed, and its per-scope cost);
- the aggregate table from the last stats dump, marked complete or incomplete;
- the breach timeline from every ring dump, ordered by `seq`, each with its **immediate** enclosing
  profiled scope (never a call chain);
- every `seq` gap, attributed to an overwrite before the read (`gap=` in the terminator) or to loss in
  the capture.

Nothing is guessed: a missing identity is "not reported", never a measured zero, and a capture whose
ring lost Tier-A rows says so.

Usage:
    python3 scripts/profile_report.py captures/session_YYYYMMDD_HHMMSS.log [--json]
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Iterable, Optional

_CAP_RE = re.compile(r"^#CAP,(\d+),DIAG,(profile_\w+),(.*)$")
_KV_RE = re.compile(r"(\w+)=([^,]*)")
_RING_LOSS_FIELDS = ("tierA_evict", "overflow", "crit_rej", "wipe")
_STAT_FIELDS = ("calls", "avg_us", "avg_self_us", "max_us", "last_us", "over", "ref", "order", "coarse",
                "last_result")


def _kv(text: str) -> dict[str, str]:
    return dict(_KV_RE.findall(text))


def _int(value: Optional[str]) -> Optional[int]:
    try:
        return int(value) if value is not None else None
    except ValueError:
        return None


def decode(lines: Iterable[str]) -> dict:
    boot: Optional[dict] = None
    ring_stats: Optional[dict[str, int]] = None
    dump_rows: dict[int, dict] = {}
    last_dump: Optional[dict] = None
    dumps_completed = 0
    ring: dict[int, dict] = {}
    ring_terminators: list[dict] = []
    refused = 0
    resets = 0
    poisoned = False
    for raw in lines:
        line = raw.rstrip("\r\n")
        if line.startswith("#CAP,") and ",RING,stats," in line[:40]:
            ring_stats = {k: int(v) for k, v in re.findall(r"(\w+)=(\d+)", line)}
            continue
        m = _CAP_RE.match(line)
        if not m:
            continue
        family, rest = m.group(2), m.group(3)
        if family == "profile_boot":
            boot = {k: _int(v) for k, v in _kv(rest).items()}
        elif family == "profile_stat":
            if rest.startswith("end,"):
                fields = _kv(rest)
                poisoned = poisoned or fields.get("poisoned") == "1"
                if fields.get("resume") == "0":
                    last_dump = {"rows": dict(dump_rows), "complete": True, "end": fields}
                    dumps_completed += 1
                    dump_rows = {}
                else:
                    last_dump = {"rows": dict(dump_rows), "complete": False, "end": fields}
                continue
            parts = rest.split(",", 2)
            if len(parts) < 3:
                continue
            ident = _int(parts[0])
            if ident is None:
                continue
            row = {"name": parts[1]}
            row.update({k: _int(v) for k, v in _kv(parts[2]).items() if k in _STAT_FIELDS})
            dump_rows[ident] = row
        elif family == "profile_ring":
            if rest.startswith("end,"):
                fields = {k: _int(v) for k, v in _kv(rest).items()}
                ring_terminators.append(fields)
                poisoned = poisoned or fields.get("poisoned") == 1
                continue
            seq_text, _, tail = rest.partition(",")
            seq = _int(seq_text)
            if seq is None:
                continue
            record = _kv(tail)
            ring[seq] = {
                "seq": seq,
                "t_us": _int(record.get("t_us")),
                "name": record.get("name"),
                "parent": record.get("parent"),
                "dur_us": _int(record.get("dur_us")),
                "trk": _int(record.get("trk")),
                "slot": _int(record.get("slot")),
                "result": _int(record.get("result")),
                "coarse": record.get("src") == "1",
            }
        elif family == "profile_cmd":
            refused += 1
        elif family == "profile_reset":
            resets += 1
            dump_rows = {}
    if dump_rows and (last_dump is None or last_dump.get("complete")):
        # Rows after the last terminator: the dump was cut off.
        last_dump = {"rows": dict(dump_rows), "complete": False, "end": None}

    ring_loss = None
    if ring_stats is not None:
        ring_loss = {k: ring_stats.get(k, 0) for k in _RING_LOSS_FIELDS}
    lossy = ring_loss is None or any(ring_loss.values())

    seqs = sorted(ring)
    overwritten = sum((t.get("gap") or 0) for t in ring_terminators)
    dropped = max([t.get("dropped") or 0 for t in ring_terminators], default=0)
    gaps = []
    for a, b in zip(seqs, seqs[1:]):
        if b > a + 1:
            gaps.append({"after": a, "before": b, "missing": b - a - 1})
    missing_total = sum(g["missing"] for g in gaps)
    unexplained = max(missing_total - overwritten, 0)

    return {
        "boot": boot,
        "ring_loss": ring_loss,
        "capture_lossy": lossy,
        "aggregate": last_dump,
        "dumps_completed": dumps_completed,
        "timeline": [ring[s] for s in seqs],
        "gaps": gaps,
        "overwritten_before_read": overwritten,
        "unexplained_missing": unexplained,
        "dropped": dropped,
        "refused_commands": refused,
        "resets": resets,
        "poisoned": poisoned,
    }


def render(report: dict) -> str:
    out: list[str] = []
    boot = report["boot"]
    if boot is None:
        out.append("boot: no profile_boot row -- profiler not armed in this image, or the row was lost")
    else:
        out.append(
            "boot: source={source} cpu_hz={cpu_hz} trust_us={trust_us} samples={samples} "
            "short_min/avg={short_min_cycles}/{short_avg_cycles} cycles "
            "long_min/avg={long_min_cycles}/{long_avg_cycles} cycles".format(
                **{k: boot.get(k) for k in ("source", "cpu_hz", "trust_us", "samples", "short_min_cycles",
                                            "short_avg_cycles", "long_min_cycles", "long_avg_cycles")}))
    loss = report["ring_loss"]
    if loss is None:
        out.append("capture ring: no RING,stats row -- absence of profile rows is not evidence")
    elif report["capture_lossy"]:
        out.append(f"capture ring: LOST ROWS {loss} -- output below may be incomplete")
    else:
        out.append("capture ring: no loss")
    if report["poisoned"]:
        out.append("WARNING: profiler nesting poisoned (order fault) -- measurements stopped until reset")

    agg = report["aggregate"]
    if agg is None:
        out.append("aggregate: unavailable (no !PROFILE_STATS dump in this capture)")
    else:
        state = "complete" if agg["complete"] else "INCOMPLETE (dump not drained to resume=0)"
        out.append(f"aggregate ({state}):")
        out.append("  id  name                        calls     avg_us  self_us     max_us  over  ref  ord  coarse")
        for ident in sorted(agg["rows"]):
            r = agg["rows"][ident]
            out.append(f"  {ident:>2}  {r['name']:<26} {r.get('calls')!s:>7} {r.get('avg_us')!s:>10} "
                       f"{r.get('avg_self_us')!s:>8} {r.get('max_us')!s:>10} {r.get('over')!s:>5} "
                       f"{r.get('ref')!s:>4} {r.get('order')!s:>4} {r.get('coarse')!s:>7}")
        if not agg["complete"]:
            out.append("  identities not listed above were not reported (not measured zeros)")

    timeline = report["timeline"]
    if not timeline:
        out.append("breaches: none decoded" + (" (no !PROFILE_RING dump)" if report["dropped"] == 0 else ""))
    else:
        out.append(f"breaches: {len(timeline)} records, seq {timeline[0]['seq']}..{timeline[-1]['seq']}, "
                   f"device dropped={report['dropped']}, overwritten before read={report['overwritten_before_read']}")
        out.append("  (parent = immediate enclosing profiled scope only; no call chain is implied)")
        for r in timeline:
            src = "wall" if r["coarse"] else "cyc"
            out.append(f"  seq={r['seq']:<6} t_us={r['t_us']!s:<11} {r['name']:<26} parent={r['parent']:<22} "
                       f"dur_us={r['dur_us']!s:<9} trk={r['trk']} slot={r['slot']} result={r['result']} src={src}")
    for g in report["gaps"]:
        out.append(f"  gap: {g['missing']} seq missing between {g['after']} and {g['before']}")
    if report["unexplained_missing"]:
        cause = "capture ring loss" if report["capture_lossy"] else "rows missing from the capture"
        out.append(f"  {report['unexplained_missing']} missing seq not explained by overwrite: {cause}; "
                   "the timeline is NOT a complete breach history")
    if report["refused_commands"]:
        out.append(f"refused profile commands: {report['refused_commands']} (issued under a live scope)")
    return "\n".join(out)


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("log", type=Path)
    parser.add_argument("--json", action="store_true", help="print the decoded report as JSON")
    args = parser.parse_args(argv)
    with args.log.open(encoding="utf-8", errors="replace") as fh:
        report = decode(fh)
    print(json.dumps(report, indent=2) if args.json else render(report))
    return 0


if __name__ == "__main__":
    sys.exit(main())
