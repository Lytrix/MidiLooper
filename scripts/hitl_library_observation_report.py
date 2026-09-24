#!/usr/bin/env python3
"""restored-library-stability Packet A / task 1.4 — read-only re-analysis of the recorded sweeps.

Runs the shared library-observation classifier (`scripts/hitl/library_observation.py`) over the existing
captures and writes the observed fields, the four D4 quantities, the window completeness and the
nullable cause per slot. It opens the captures for reading only: there is no device access, no upload,
and no SD write of any kind.

The point of the exercise is as much the *gaps* as the results. The `s13` sweeps recorded no payload
token, no committed-content dump and no active/selected index, so the analysis reports those quantities
as `unknown` **with the missing field named** rather than printing a zero — which is precisely the
observability gap the on-demand device observation (task 1.3) closes.

Usage:
  .venv/bin/python scripts/hitl_library_observation_report.py \
      --capture-dir captures/s13-slope-02 --capture-dir captures/s13-slope-final \
      --out captures/s13-library-observation.json
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from hitl.library_observation import analyze_capture_dir  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--capture-dir", type=Path, action="append", required=True,
                        help="A capture directory; repeatable.")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--all-logs", action="store_true",
                        help="Analyze every *.log, not just each step's boot.log.")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    log_name = None if args.all_logs else "boot.log"
    reports = [analyze_capture_dir(directory.resolve(), log_name=log_name)
               for directory in args.capture_dir]
    payload = {"reports": reports}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(payload, indent=2), encoding="utf-8")

    if not args.quiet:
        for report in reports:
            print(f"[libobs] {report['capture_dir']}: {report['boot_count']} {report['log_name']}")
            for boot in report["boots"]:
                summary = boot["summary"]
                counts = summary["counts"]
                unknown = ", ".join(summary["counts_unknown_fields"]) or "none"
                causes: dict[str, int] = {}
                for slot in boot["slots"]:
                    causes[slot["cause"]] = causes.get(slot["cause"], 0) + 1
                cause_text = " ".join(f"{name}={count}" for name, count in sorted(causes.items()))
                print(
                    f"   {Path(boot['capture']).parent.name}: "
                    f"window=[{summary['window']['start_us']},{summary['window']['end_us']}] "
                    f"lines={summary['window']['line_count']} requests={boot['requests_seen']} "
                    f"N(window-scoped)={boot['n_committed_slots_window_scoped']} "
                    f"payload_inventory={counts['payload_inventory']} "
                    f"initial_playback={counts['initial_playback_required']} "
                    f"committed_at_checkpoint={counts['committed_at_checkpoint']} "
                    f"complete={summary['complete']} "
                    f"counts_unknown=[{unknown}]"
                )
                print(f"      causes: {cause_text}")
                # Only the slots the classifier could say something *positive* about are listed; the
                # never-observed ones are summarized as `unknown` above, with their missing fields named.
                for slot in boot["slots"]:
                    if slot["cause"] in ("unknown", "not_applicable"):
                        continue
                    missing = ", ".join(slot["unknown_fields"])
                    print(
                        f"      t{slot['track']}s{slot['slot']}: "
                        f"state={slot['state_names']} cause={slot['cause']} "
                        f"evidence={slot['evidence']} missing=[{missing}]"
                    )
    print(f"[libobs] wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
