#!/usr/bin/env python3
"""One-shot serial debug for revision load failure lines."""

import sys
import time

from hitl.serial_collector import SerialCaptureCollector

def main() -> int:
    port = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbmodem154944801"
    set_id = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    rev_id = int(sys.argv[3]) if len(sys.argv) > 3 else 23
    collector = SerialCaptureCollector(port, baud=115200)
    collector.start()
    time.sleep(0.5)
    start = len(collector.snapshot())
    collector.write_line(f"!REV_LOAD {set_id} {rev_id}")
    deadline = time.monotonic() + 120.0
    while time.monotonic() < deadline:
        lines = collector.snapshot()
        tail = lines[start:]
        for line in tail:
            if any(
                token in line
                for token in ("ERROR", "rev_load", "Revision load", "readLoopPersisted")
            ):
                print(line.rstrip())
        if any("rev_load_complete" in line for line in tail):
            print("--- SUCCESS ---")
            collector.stop()
            return 0
        if any("Revision load failed reloading" in line for line in tail):
            print("--- FAILED RELOAD ---")
            for line in tail:
                if "ERROR" in line:
                    print("ERR:", line.rstrip())
            collector.stop()
            return 1
        time.sleep(0.05)
    print("timeout")
    collector.stop()
    return 2

if __name__ == "__main__":
    raise SystemExit(main())
