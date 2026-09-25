#!/usr/bin/env python3
"""S1.3 save/restore-survival — record one approved scratch slot and gate on its persist target.

This is the **fixture-side** half of the record-stop identity investigation. It drives the proven record
baseline into one pinned, owner-authorized scratch slot, then correlates the take's slot across the
boundaries (`scripts/hitl/record_stop_persist_target.py`) before anything else happens:

    recorded slot at capture start -> slot at stop -> slot passed to
    `finalizeCommitSideEffects` -> admitted persistence key -> (separately) the payload write

**The gate is a stop condition, not a device action.** On a mismatch this script prints the divergent
boundary and exits non-zero *without* rebooting: a fixture that wrote a different slot than it claims to
grow cannot produce evidence about payload survival, so the next step is to trace the slot identity, not
to reboot. Nothing here clears, quarantines or renames anything.

**Two evidence cautions this script states rather than hides:**

* a missing `LoopPersist` line is not proof that no payload was written — the `RID`/`PERS` telemetry
  covers the deferred persistence worker, and four other paths can write a current-set slot payload
  without a `PERS` work record. The demonstrated finding is a **wrong-target admission**; the payload
  write itself is verified separately.
* a boot's `credits=0` is the **cold** reservation walk, not peak capture reservation: it is not the
  `peak capture reservation + 16` term S2b.3 needs, which has to be measured during the capture workload.

Usage:
  .venv/bin/python scripts/hitl_record_stop_target.py --out-dir captures/s13-target-01 \
      --track 6 --loop-slot 2 --serial-port /dev/cu.usbmodem...
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "scripts"))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--serial-port", default="")
    parser.add_argument("--track", type=int, required=True,
                        help="User-facing track number (1-8) of the approved scratch slot.")
    parser.add_argument("--loop-slot", type=int, required=True,
                        help="User-facing loop slot (1-8) of the approved scratch slot.")
    parser.add_argument("--record-bars", type=int, default=2, help="Integer bar count (the baseline takes an int here).")
    parser.add_argument("--record-seconds", type=float, default=6.0)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    out_dir: Path = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    serial_log = out_dir / "record.log"

    # Import third-party before `scripts` lands on sys.path: `scripts/hitl/serial` and
    # `scripts/hitl/types` would otherwise shadow pyserial and the stdlib `types` module.
    import mido  # noqa: F401
    import serial  # noqa: F401

    from hitl.legacy_record_baseline import run as run_record_baseline
    from hitl.record_stop_persist_target import (
        correlate_takes,
        format_verdict,
        parse_persist_admissions,
    )

    record_argv = [
        "legacy_record_baseline",
        "--track", str(args.track),
        "--loop-slot", str(args.loop_slot),
        "--record-only",
        "--record-bars", str(args.record_bars),
        "--record-seconds", str(args.record_seconds),
        "--serial-log-path", str(serial_log),
        "--out-dir", str(out_dir),
    ]
    if args.serial_port:
        record_argv += ["--serial-port", args.serial_port]

    saved_argv = sys.argv
    sys.argv = record_argv
    try:
        record_exit = run_record_baseline()
    finally:
        sys.argv = saved_argv

    lines: list[str] = []
    if serial_log.exists():
        lines = serial_log.read_text(encoding="utf-8", errors="replace").splitlines()

    # The pinned target is zero-based: user track N -> index N-1, loop slot M -> index M-1.
    expected_track = args.track - 1
    expected_slot = args.loop_slot - 1
    correlation = correlate_takes(lines, expected_track=expected_track, expected_slot=expected_slot)
    admissions = parse_persist_admissions(lines)
    loop_persist = [row for row in admissions if row["type"] == "LoopPersist"]

    print(format_verdict(correlation))
    print(f"[rid] deferred-worker work items: {len(admissions)} (LoopPersist: {len(loop_persist)})")
    for row in loop_persist[:6]:
        print(f"[rid]   LoopPersist {row['key']} {row['phase']} {row['result']}")
    if not loop_persist:
        print("[rid] no LoopPersist work item observed. This is NOT proof that no payload was written: "
              "the telemetry covers the deferred worker only, and revision-load / boot-recovery / "
              "saved-set-copy can write a slot payload without a PERS record. Verify the payload "
              "separately (file size and a boot that reads the events back).")

    report = {
        "track_user": args.track,
        "loop_slot_user": args.loop_slot,
        "expected_track_index": expected_track,
        "expected_slot_index": expected_slot,
        "record_baseline_exit": record_exit,
        "rid_correlation": correlation,
        "persist_work_items": admissions,
        "loop_persist_items": loop_persist,
        "serial_log": str(serial_log),
        "reboot_allowed": bool(correlation["pass"]),
        "stop_reason": None if correlation["pass"] else (
            "persist target does not match the recorded slot: trace the slot identity before rebooting"
        ),
    }
    report_path = out_dir / "target-correlation.json"
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"[rid] wrote {report_path}")

    if not correlation["pass"]:
        print("[rid] STOP: reboot withheld; this fixture did not write the slot it means to grow.")
        return 2
    print("[rid] correlation OK. The payload write still needs separate verification before reboot is "
          "evidence about survival.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
