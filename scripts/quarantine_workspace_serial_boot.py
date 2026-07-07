#!/usr/bin/env python3
"""Quarantine MidiLooper/current on Teensy SD via serial (no Mac SD mount).

Send !QUARANTINE_WORKSPACE during the boot listen window (before loadState).
Use after upload/reset while capture-serial firmware is flashed.

  pio run -e teensy41-capture-serial -t upload
  .venv/bin/python scripts/quarantine_workspace_serial_boot.py --port /dev/cu.usbmodem154944801

One-shot compile flag (no serial needed for that boot only):
  Add -D BOOT_QUARANTINE_WORKSPACE=1 to env:teensy41-capture-serial build_flags, upload once,
  remove the flag, upload again.
"""

from __future__ import annotations

import argparse
import sys
import time

try:
    import serial
except ImportError as exc:  # pragma: no cover
    raise SystemExit("Install pyserial: python3 -m pip install pyserial") from exc

COMMAND = b"!QUARANTINE_WORKSPACE\n"
BOOT_HINT = "Boot: send !QUARANTINE_WORKSPACE"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--port",
        default="/dev/cu.usbmodem154944801",
        help="Teensy USB serial port",
    )
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument(
        "--listen-ms",
        type=int,
        default=4000,
        help="How long to watch serial before/after sending the command",
    )
    args = parser.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.05)
    try:
        time.sleep(0.3)
        deadline = time.monotonic() + args.listen_ms / 1000.0
        sent = False
        while time.monotonic() < deadline:
            chunk = ser.read(4096)
            if chunk:
                text = chunk.decode("utf-8", errors="replace")
                sys.stdout.write(text)
                sys.stdout.flush()
                if not sent and BOOT_HINT in text:
                    ser.write(COMMAND)
                    ser.flush()
                    sent = True
                    print("[host] sent !QUARANTINE_WORKSPACE", flush=True)
            elif not sent and time.monotonic() > deadline - (args.listen_ms / 2000.0):
                ser.write(COMMAND)
                ser.flush()
                sent = True
                print("[host] sent !QUARANTINE_WORKSPACE (timeout fallback)", flush=True)
            time.sleep(0.02)

        tail_deadline = time.monotonic() + 2.0
        while time.monotonic() < tail_deadline:
            chunk = ser.read(4096)
            if not chunk:
                time.sleep(0.05)
                continue
            sys.stdout.write(chunk.decode("utf-8", errors="replace"))
            sys.stdout.flush()
    finally:
        ser.close()

    if not sent:
        print("[host] warn: command may not have been sent", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
