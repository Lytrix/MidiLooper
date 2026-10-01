#!/usr/bin/env python3
"""S4.1 device-leg oracle: does this run show the working-set lease release for the slot under test?

S4.1 -- release when the slot leaves the active set and the guard reports no live owner -- is verified natively
by the three fixtures in `test_runtime_working_set_ownership`. What it owes on device is *evidence*: a run in
which `Track::releaseEligiblePlaybackRuntimeSlots` fires for the slot under test, observed as
`DIAG,ws_release,<slot>,<generation>,<outcome>` (already emitted and Tier-A), read by name rather than inferred.

This reader is that half, and it is deliberately small: `hitl.lease_release` owns the row, and everything here
is quoting and one verdict.

    python scripts/hitl_s41_lease_release_read.py --log captures/session_<...>.log --slot 1 \
        [--generation 0] [--metadata captures/metadata.json] [--out captures/s41-lease-release.json]

Exit codes:

    0  the slot under test has a `released` row in this log
    2  it does not (absence is absence: the row is emitted only where a sweep runs, and `NotHeld` is silent)
    3  the log cannot be read

Base convention, because it has cost two wrong readings in this change: `--slot` is the **1-based** loop
number the button and every driver CLI use, while the row's own slot field is **0-based**. The conversion
happens once, and both numbers are printed, so a reader never has to guess which base a number is in.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from hitl.lease_release import (  # noqa: E402
    lease_release_rows,
    outcome_for_slot,
    release_context,
    release_rows_for_slot,
    released_slot_numbers,
)

IDENTITY_KEYS = ("git_commit", "git_dirty", "firmware_hex_sha256", "preset", "track_number", "loop_slot",
                 "session_log", "timestamp")


def _identity(metadata: Path | None) -> dict:
    """The run's own identity, quoted only when the run's metadata names it."""
    if metadata is None or not metadata.exists():
        return {}
    try:
        raw = json.loads(metadata.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}
    return {key: raw[key] for key in IDENTITY_KEYS if key in raw}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--log", type=Path, required=True, help="the run's session log (or serial snapshot)")
    parser.add_argument("--slot", type=int, required=True, choices=range(1, 9),
                       help="the loop slot under test, 1-based (the button's `Loop N`)")
    parser.add_argument("--generation", type=int, default=None,
                       help="optional: require this generation, so a sibling release cannot stand in")
    parser.add_argument("--metadata", type=Path, default=None,
                       help="the run's metadata.json, read only to quote the run's identity")
    parser.add_argument("--out", type=Path, default=None, help="optional path for the JSON record")
    parser.add_argument("--context", type=int, default=4, help="how many preceding edge rows to quote")
    args = parser.parse_args(argv)

    if not args.log.exists():
        print(f"[s41] no such log: {args.log}")
        return 3

    lines = args.log.read_text(encoding="utf-8", errors="replace").splitlines()
    rows = lease_release_rows(lines)
    slot0 = args.slot - 1
    named = release_rows_for_slot(rows, slot0)
    outcome = outcome_for_slot(rows, slot0)
    hit = [row for row in named if row.released and (args.generation is None or row.generation == args.generation)]

    record = {
        "log": str(args.log),
        "slot_1based": args.slot,
        "slot0": slot0,
        "required_generation": args.generation,
        "outcome": outcome,
        "release_rows": [row.raw for row in named],
        "released_slots_1based": released_slot_numbers(rows),
        "all_release_rows": [row.raw for row in rows],
        "identity": _identity(args.metadata),
        "verdict": "LEASE_RELEASE_OBSERVED" if hit else "LEASE_RELEASE_NOT_OBSERVED",
    }
    if hit:
        record["edge"] = release_context(lines, hit[0], limit=args.context)

    print(f"[s41] {args.log}: {len(rows)} working-set lease release row(s); "
          f"released slot(s) {released_slot_numbers(rows) or 'none'} (1-based)")
    for row in rows:
        print(f"  line {row.line_no}: {row.raw}   (slot {row.slot0} 0-based = Loop {row.slot_number})")
    if named:
        for row in named:
            print(f"  slot under test (Loop {args.slot} = slot {slot0}) row: {row.raw}")
            for quote in release_context(lines, row, limit=args.context):
                print(f"    edge: {quote}")
    else:
        print(f"  slot under test (Loop {args.slot} = slot {slot0}): no release row -- "
              f"outcome `{outcome}`. Absence means no sweep ran on this slot (or it held no lease); "
              f"`NotHeld` is deliberately silent.")
    if record.get("identity"):
        print(f"  run identity: {json.dumps(record['identity'])}")

    if args.out is not None:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")

    if hit:
        print(f"\n[s41] LEASE RELEASE OBSERVED for slot {args.slot} (slot {slot0} 0-based): {hit[0].raw}")
        return 0
    print(f"\n[s41] LEASE RELEASE NOT OBSERVED for slot {args.slot} (slot {slot0} 0-based); "
          f"{len(rows)} row(s) in this log named other slots.")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
