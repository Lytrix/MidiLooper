#!/usr/bin/env python3
"""Can a MISSING capture row be read as "the device never emitted it"?

Usually not: the capture ring sheds non-Tier-A rows under transport and can lose Tier-A rows too, so an
empty window is only evidence once the same capture shows no loss. This reads the capture's own accounting
(`#CAP,<us>,RING,stats,...` and `RING,shed,...` rows) and prints a verdict per tag.

    python3 scripts/hitl_absence_check.py captures/<run>/session_*.log 'DIAG,my_event,' ['RECS,' ...]

Verdicts (exit status 0 for the first, 1 otherwise):
  PRESENT                row(s) found; the count is printed.
  ABSENT-EVIDENCE        none found AND the last RING,stats row reports zero shed/evict/overflow/reject/wipe.
  ABSENT-NOT-EVIDENCE    none found AND the ring lost rows, or no RING,stats row exists to say it did not.
A Tier-A tag ignores routine non-Tier-A shed (it cannot displace Tier-A); any other tag does not.
Streams the file line by line; safe on very large captures.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path
from typing import Iterable, Optional

_STATS_FIELDS = ("shed", "tierA_evict", "overflow", "crit_rej", "wipe")
_KV_RE = re.compile(r"(\w+)=(\d+)")


def scan(lines: Iterable[str], tags: list[str]) -> dict:
    counts = {tag: 0 for tag in tags}
    stats: Optional[dict[str, int]] = None
    shed_rows = 0
    total = 0
    for raw in lines:
        total += 1
        if not raw.startswith("#CAP,"):
            continue
        for tag in tags:
            if f",{tag}" in raw[:80] and raw.split(",", 2)[2].startswith(tag):
                counts[tag] += 1
        body = raw.split(",", 2)[2] if raw.count(",") >= 2 else ""
        if body.startswith("RING,stats,"):
            stats = {k: int(v) for k, v in _KV_RE.findall(body)}
        elif body.startswith("RING,shed,"):
            shed_rows += 1
    return {"lines": total, "counts": counts, "stats": stats, "shed_rows": shed_rows}


def is_tier_a(tag: str) -> bool:
    """Tier-A rows are only displaced by Tier-A, so routine `shed` (non-Tier-A) cannot have hidden them."""
    try:
        import check_marker_contract as cmc

        accepted, excluded = cmc.tier_a_prefixes()
        return cmc.classify(tag, accepted, excluded) is not None
    except (OSError, ValueError, ImportError):
        return False


def losses(result: dict, *, tier_a: bool = False) -> Optional[dict[str, int]]:
    """Non-zero loss counters, {} when provably clean, None when the capture carries no RING,stats row."""
    stats = result["stats"]
    if stats is None:
        return None
    ignored = {"shed"} if tier_a else set()
    lost = {k: stats.get(k, 0) for k in _STATS_FIELDS if stats.get(k, 0) and k not in ignored}
    if result["shed_rows"] and "shed" not in lost and not tier_a:
        lost["shed_rows"] = result["shed_rows"]
    return lost


def verdicts(result: dict, *, tier_a_of=is_tier_a) -> list[tuple[str, str]]:
    out = []
    for tag, count in result["counts"].items():
        lost = losses(result, tier_a=tier_a_of(tag))
        if count:
            out.append(("PRESENT", f"{tag}: {count} row(s)"))
        elif lost is None:
            out.append(("ABSENT-NOT-EVIDENCE", f"{tag}: none found, and the capture has no RING,stats row to prove no loss"))
        elif lost:
            out.append(("ABSENT-NOT-EVIDENCE", f"{tag}: none found, but the ring lost rows: {lost}"))
        else:
            out.append(("ABSENT-EVIDENCE", f"{tag}: none found and RING,stats reports no loss"))
    return out


def main(argv: Optional[list[str]] = None) -> int:
    args = list(sys.argv[1:] if argv is None else argv)
    if len(args) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    path, tags = Path(args[0]), args[1:]
    if not path.is_file():
        print(f"error: {path} is not a file", file=sys.stderr)
        return 2
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        result = scan(handle, tags)
    print(f"{path.name}: {result['lines']} lines; RING,stats={result['stats']}; RING,shed rows={result['shed_rows']}")
    worst = 0
    for verdict, message in verdicts(result):
        print(f"{verdict:20} {message}")
        worst = max(worst, 0 if verdict == "PRESENT" else 1)
    return worst


if __name__ == "__main__":
    sys.exit(main())
