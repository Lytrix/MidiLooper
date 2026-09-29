#!/usr/bin/env python3
"""Acceptance replay for the stored-record grid (`REVT`) take window.

Replays every stored capture through three implementations of `_extract_revt_note_on_ticks` and checks
the criteria the fix was accepted on:

    pre-21b8ca8f   the unbounded take slice (lower bound only) -- what a single-take run needs
    current-21b8ca8f  bounded by the flush stamp (`ts > recs_stop_ts`), the regression
    fixed          bounded by the take's row span (`_record_take_row_index` .. `_next_record_take_row_index`)

Criteria (each is asserted, and a violation exits non-zero):

1. `fixed` equals the pre-regression set for every take **without a successor** -- the fix repairs the
   regression rather than inventing a third behaviour.
2. `fixed` excludes a successor take's rows (`s4f22-step8-e2`: 4 -> 0), which is what `21b8ca8f` intended.
3. `fixed` never *adds* rows that the pre-regression extractor did not see.

Why this exists instead of a verdict re-score: the *verdict* consumer of `revt_missing` is a single inline
gate in the baseline (`if stored_record_grid.get("revt_missing"): issues.append(...)`), and the offline
`--verify-only` path runs only scenario stimulus validation, so the extractor is the honest unit to pin.

Usage:
    .venv/bin/python scripts/hitl_replay_stored_record_grid.py                 # the nine stored runs
    .venv/bin/python scripts/hitl_replay_stored_record_grid.py --captures-dir captures --run s4f22-step11-h5
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(_REPO_ROOT / "scripts"))

from hitl import legacy_record_baseline as baseline  # noqa: E402

#: The runs this acceptance was measured on, with their expected `pre-21b8ca8f / current / fixed` counts.
EXPECTED: dict[str, tuple[int, int, int]] = {
    "s4f22-step3-base-01": (0, 0, 0),
    "s4f22-step4-a1": (32, 0, 32),
    "s4f22-step5-b1": (32, 0, 32),
    "s4f22-step8-e1": (0, 0, 0),
    "s4f22-step8-e2": (4, 0, 0),
    "s4f22-step8-e3": (32, 0, 32),
    "s4f22-step11-h3": (0, 0, 0),
    "s4f22-step11-h5": (32, 0, 32),
    "s4f22-step11-h6": (32, 0, 32),
}

_RECA_STRICT = re.compile(r"^#CAP,\d+,RECA,")


def pre_regression_ticks(lines: list[str]) -> list[int]:
    """`_extract_revt_note_on_ticks` as it stood before `21b8ca8f`: every `REVT` row after the `RECA`."""
    take_start = next((i for i, line in enumerate(lines) if _RECA_STRICT.match(line)), None)
    if take_start is None:
        return []
    ticks: list[int] = []
    for line in lines[take_start:]:
        if ",REVT," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 6:
            continue
        try:
            ticks.append(int(parts[3]))
        except ValueError:
            continue
    return ticks


def current_regression_ticks(lines: list[str]) -> list[int]:
    """`21b8ca8f`'s bound: the take closes at the `RECS,stop` **flush** stamp."""
    take_start_us = None
    stop_us = None
    for line in lines:
        if ",RECA," in line and take_start_us is None:
            try:
                take_start_us = int(line.split(",")[1])
            except ValueError:
                pass
        if ",RECS,stop," in line and stop_us is None:
            try:
                stop_us = int(line.split(",")[1])
            except ValueError:
                pass
            break
    if stop_us is None:
        return []
    ticks: list[int] = []
    for line in lines:
        if ",REVT," not in line:
            continue
        parts = line.split(",")
        if len(parts) < 6:
            continue
        try:
            us = int(parts[1])
            tick = int(parts[3])
        except ValueError:
            continue
        if take_start_us is not None and us < take_start_us:
            continue
        if us > stop_us:
            continue
        ticks.append(tick)
    return ticks


def _session_log(captures_dir: Path, run: str) -> Path | None:
    logs = sorted((captures_dir / run).glob("session_*.log"))
    return logs[-1] if logs else None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--captures-dir", type=Path, default=_REPO_ROOT / "captures")
    parser.add_argument("--run", action="append", default=None,
                        help="Replay only this capture directory name (repeatable; default: all)")
    args = parser.parse_args()

    runs = args.run or sorted(EXPECTED)
    failures: list[str] = []
    replayed = 0
    print(f"{'run':22s} {'pre-21b8ca8f':>13} {'current':>8} {'fixed':>6}  criteria")
    for run in runs:
        log = _session_log(args.captures_dir, run)
        if log is None:
            print(f"{run:22s} {'-':>13} {'-':>8} {'-':>6}  skipped (capture not present)")
            continue
        lines = log.read_text(encoding="utf-8", errors="replace").splitlines()
        pre = pre_regression_ticks(lines)
        cur = current_regression_ticks(lines)
        fixed = baseline._extract_revt_note_on_ticks(lines)
        take_start = baseline._record_take_row_index(lines)
        successor = (
            baseline._next_record_take_row_index(lines, after_index=take_start)
            if take_start is not None
            else None
        )

        notes: list[str] = []
        expected = EXPECTED.get(run)
        if expected is not None and (len(pre), len(cur), len(fixed)) != expected:
            failures.append(
                f"{run}: expected pre/current/fixed {expected}, got {(len(pre), len(cur), len(fixed))}"
            )
            notes.append("COUNTS DIFFER FROM THE ACCEPTED TABLE")
        if successor is None:
            if fixed != pre:
                failures.append(
                    f"{run}: no successor take, so the fix must equal the pre-regression set "
                    f"({len(pre)} vs {len(fixed)})"
                )
                notes.append("EQUIVALENCE VIOLATED")
            else:
                notes.append("fixed == pre-regression")
        else:
            successor_rows = [t for t in pre if t not in fixed]
            if not successor_rows:
                failures.append(f"{run}: successor take at line {successor} but its rows were not excluded")
                notes.append("EXCLUSION MISSING")
            else:
                notes.append(f"successor excluded ({len(successor_rows)} rows)")
        if not set(fixed) <= set(pre):
            failures.append(f"{run}: the fix reports ticks the pre-regression extractor never saw")
            notes.append("SUPERSET VIOLATION")

        replayed += 1
        print(f"{run:22s} {len(pre):>13} {len(cur):>8} {len(fixed):>6}  {'; '.join(notes)}")

    if replayed == 0:
        print("[replay] no captures present; nothing to check (acceptance table: scripts/test_hitl_stored_record_grid.py)")
        return 0
    if failures:
        print("\n[replay] FAILED:")
        for failure in failures:
            print(f"  - {failure}")
        return 1
    print(f"\n[replay] OK: {replayed} capture(s) meet the accepted criteria")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
