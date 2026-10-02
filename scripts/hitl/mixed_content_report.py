#!/usr/bin/env python3
"""Produce the mixed-leg A/B storage-to-effective content report.

The device content owner dumps one stopped slot at a time. This reader combines the two
authorized slots into the existing acceptance-evidence shape without inventing a second
content authority or comparing unrelated playback output from other tracks.
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

# Running this file directly places scripts/hitl on sys.path[0], where the local
# hitl/serial package shadows pyserial. Remove that entry before importing the
# shared hitl readers so this CLI uses the real serial.Serial implementation.
ROOT = Path(__file__).resolve().parents[1]
_SCRIPT_DIR = str(Path(__file__).resolve().parent)
if _SCRIPT_DIR in sys.path:
    sys.path.remove(_SCRIPT_DIR)
sys.path.insert(0, str(ROOT))

import serial

from hitl.retention_probe import analyse_content_dump


def _read_dump(dev: serial.Serial, *, slot0: int, timeout_s: float) -> str:
    dev.reset_input_buffer()
    dev.write(f"!CAPTURE_CONTENT_SLOT {slot0}\n".encode())
    deadline = time.monotonic() + timeout_s
    lines: list[str] = []
    while time.monotonic() < deadline:
        raw = dev.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").strip()
        lines.append(line)
        if line == "CAPTURE_CONTENT,end":
            break
    return "\n".join(lines)


def build_report(*, port: str, slots: tuple[int, int], timeout_s: float, baud: int) -> dict:
    slot_reports: list[dict] = []
    with serial.Serial(port, baud, timeout=0.05) as dev:
        time.sleep(1.0)
        for slot in slots:
            dump = _read_dump(dev, slot0=slot - 1, timeout_s=timeout_s)
            parsed = analyse_content_dump(dump, slot_c=slot - 1, expected_notes=())
            slot_reports.append({
                "slot": slot,
                "firmware_slot": slot - 1,
                "complete": bool(parsed["complete"]),
                "dump_slot": parsed["slot"],
                "declared_counts_match": bool(parsed["declared_counts_match"]),
                "storage_matches_effective": parsed["storage_effective_match"],
                "storage_rows": parsed.get("declared_counts", {}),
                "storage_note_ons": parsed.get("storage_note_ons", []),
                "effective_note_ons": parsed.get("effective_note_ons", []),
            })
    passed = all(
        item["complete"] and item["dump_slot"] == item["firmware_slot"] and
        item["declared_counts_match"] and item["storage_matches_effective"] is True
        for item in slot_reports
    )
    return {
        "pass": passed,
        "verdict": "pass" if passed else "fail",
        "inventory_equal": passed,
        "storage_matches_effective": passed,
        "slots": slot_reports,
        "unexplained_manifest_differences": [] if passed else ["mixed slot dump parity failed"],
        "unexplained_lifecycle_differences": [],
        "count_differences": {},
        "owner": "mixed_content_report",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--slot-a", type=int, required=True)
    parser.add_argument("--slot-b", type=int, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--timeout-s", type=float, default=8.0)
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()
    report = build_report(port=args.port, slots=(args.slot_a, args.slot_b),
                          timeout_s=args.timeout_s, baud=args.baud)
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0 if report["pass"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
