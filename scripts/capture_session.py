#!/usr/bin/env python3
"""Record Teensy USB serial to a capture log (Bucket 1 S1)."""
from __future__ import annotations

import argparse
import sys
import time
from datetime import datetime
from pathlib import Path

import serial


def main() -> int:
    parser = argparse.ArgumentParser(description="Record Teensy serial capture session")
    parser.add_argument(
        "--port",
        default="/dev/cu.usbmodem154944801",
        help="USB serial port (default: Teensy cu device)",
    )
    parser.add_argument(
        "--baud",
        type=int,
        default=115200,
        help="Serial baud rate (default: 115200)",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=Path("captures"),
        help="Output directory (default: captures/)",
    )
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_path = args.out_dir / f"session_{stamp}.log"
    pointer = args.out_dir / ".current_session"
    pointer.write_text(str(out_path) + "\n", encoding="utf-8")

    print(f"Recording {args.port} -> {out_path}", flush=True)
    print("Press Ctrl+C when the session is done.", flush=True)

    with serial.Serial(args.port, args.baud, timeout=0.25) as ser:
        # Opening the port resets Teensy; brief pause for boot + HDR line.
        time.sleep(2.0)
        with out_path.open("ab") as log:
            while True:
                chunk = ser.read(4096)
                if chunk:
                    log.write(chunk)
                    log.flush()
                else:
                    time.sleep(0.05)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\nCapture stopped.", flush=True)
        raise SystemExit(0)
