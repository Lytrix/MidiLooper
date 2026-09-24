#!/usr/bin/env python3
"""restored-library-stability Packet B / task 2.2 — complete-manifest stability gate.

Compares the **entire** expected per-slot manifest across three consecutive fresh boots, using the
read-only device observation records (`!LIB_OBSERVE` → `LIBOBS`). Any unexplained difference between
the observed and expected manifest is a **failure**, including a difference in a slot that is not part
of the fixture. Reuses existing captures when they satisfy the requirement; it never touches the
device itself.

The same tool reuses the Packet A captures read-only, so the run first establishes what the existing
evidence does and does not settle:

```
.venv/bin/python scripts/hitl_library_manifest_stability.py \
    --boot captures/s13-libobs-full-02 \
    --boot captures/s13-libobs-incomplete-01 \
    --boot captures/s13-libobs-incomplete-03 \
    --out captures/s13-libobs-stability-existing.json
```

Usage:
  .venv/bin/python scripts/hitl_library_manifest_stability.py --boot <dir> [--boot <dir> ...] \
      --out <report.json> [--expected-dir <dir>] [--authorized-change t,s]
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from hitl.library_manifest import format_report, stability_report  # noqa: E402


def _authorized_change(value: str) -> tuple[int, int]:
    try:
        track, slot = value.split(",")
        return int(track), int(slot)
    except ValueError as exc:  # noqa: BLE001
        raise argparse.ArgumentTypeError(
            f"expected 'track,slot' (for example 5,1), got {value!r}"
        ) from exc


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--boot", type=Path, action="append", required=True,
                        help="A fresh-boot capture directory, in boot order; repeatable.")
    parser.add_argument("--expected-dir", type=Path, default=None,
                        help="Capture directory to record the expected manifest from. Defaults to "
                             "the first --boot.")
    parser.add_argument("--authorized-change", type=_authorized_change, action="append", default=[],
                        help="A fixture slot 'track,slot' whose change is authorized; repeatable. Any "
                             "difference outside these slots is unexplained.")
    parser.add_argument("--required-boots", type=int, default=3)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    report = stability_report(
        args.boot,
        expected_dir=args.expected_dir,
        authorized_changes=args.authorized_change,
        required_boots=args.required_boots,
    )
    report["authorized_changes"] = sorted(f"{t},{s}" for t, s in args.authorized_change)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(format_report(report))
    print(f"[manifest] wrote {args.out}")
    return 0 if report["verdict"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
