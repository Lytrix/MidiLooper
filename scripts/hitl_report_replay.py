#!/usr/bin/env python3
"""Verify a sealed HITL report bundle again, with no device attached.

    .venv/bin/python scripts/hitl_report_replay.py --bundle captures/<run>/run-bundle.json

The bundle is the run's sealed evidence (`hitl.reporting.write_report_bundle`): versioned, digest-pinned, and
carrying the predicates, teardown result and identity the run recorded. This tool re-runs the schema's own
verdict rule over exactly those recorded predicates, so the verdict it prints is the one the bundle's evidence
supports -- independent of any device, port or session.

It is deliberately a *reader*: nothing here opens MIDI or serial, and nothing changes the bundle.

Exit codes: 0 when the bundle is intact and its evidence is a pass; 2 when it is intact but not a pass (a
legitimate non-pass is still a successful *verification*); 3 when the bundle cannot be trusted -- unknown
schema version, edited content, or a recorded verdict its own evidence denies.

    --expect passed   (default) exit 0 only for a passing bundle
    --expect any      exit 0 for any intact bundle, pass or not
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from hitl.reporting import BundleInvalid, verify_report_bundle  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Replay a sealed HITL report bundle without a device")
    parser.add_argument("--bundle", type=Path, required=True, help="the run-bundle.json to verify")
    parser.add_argument("--expect", choices=("passed", "any"), default="passed",
                       help="whether the bundle must also be a pass (default) or only be intact")
    parser.add_argument("--quiet", action="store_true", help="exit code only")
    args = parser.parse_args(argv)

    try:
        verdict = verify_report_bundle(args.bundle)
    except BundleInvalid as exc:
        if not args.quiet:
            print(f"[replay] INVALID: {exc}")
        return 3

    if not args.quiet:
        print(json.dumps({"bundle": str(args.bundle), **verdict.as_dict()}, indent=2))
        state = "PASS" if verdict.passed else "NOT A PASS"
        print(f"[replay] {args.bundle}: {state}"
              + ("" if verdict.ok else " -- the bundle's own verdict does not survive re-derivation"))
    if not verdict.ok:
        return 3
    if args.expect == "passed" and not verdict.passed:
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
