#!/usr/bin/env python3
"""S4f.11 — print the acceptance -> execution latency matrix for one capture.

The measurement itself belongs to `hitl.interaction_latency` (device rows only, every gap named); this module is
only the CLI the evidence runs are invoked with, so a recorded number always names the capture it came from.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from hitl.interaction_latency import as_dict, measure  # noqa: E402


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--session-log", type=Path, required=True,
                   help="the managed capture to measure (device rows, not host timestamps)")
    p.add_argument("--out", type=Path, default=None, help="write the matrix as JSON for the run's evidence")
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    text = args.session_log.read_text(encoding="utf-8", errors="replace")
    report = measure(text, source=str(args.session_log))
    if args.out is not None:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(as_dict(report), indent=2) + "\n", encoding="utf-8")
    print(f"acceptance -> execution latency (device rows): {report.source}")
    for cls in report.classes:
        if cls.not_exercised is not None:
            print(f"  [     gap] {cls.spec.interaction}: not_exercised -- {cls.not_exercised}")
            continue
        worst = cls.worst_us or 0
        flag = (f"  EXCEEDS the declared bound x{len(cls.exceeded)}" if cls.exceeded else "")
        print(f"  [{worst:>8} us] {cls.spec.interaction}: {len(cls.samples)} sample(s), worst {worst} us; "
              f"declared: {cls.spec.declared}{flag}")
    for event in report.events:
        print(f"  event: {event}")
    if args.out is not None:
        print(f"  matrix: {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
