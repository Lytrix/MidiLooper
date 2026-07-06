#!/usr/bin/env python3
"""Record Teensy USB serial to a capture log (Bucket 1 S1)."""
from __future__ import annotations

import argparse
import os
import sys
import time
from datetime import datetime
from pathlib import Path

import serial


def _sync_log(log) -> None:
    log.flush()
    os.fsync(log.fileno())


def _is_transient_read_error(exc: serial.SerialException) -> bool:
    msg = str(exc).lower()
    return "returned no data" in msg or "no data" in msg


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
        "--boot-wait",
        type=float,
        default=10.0,
        help="Seconds to wait after opening serial (Teensy resets on connect)",
    )
    parser.add_argument(
        "--reconnect-delay",
        type=float,
        default=5.0,
        help="Seconds between serial reconnect attempts after disconnect/crash",
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
    print(f"Boot wait: {args.boot_wait:.1f}s (Teensy resets when serial opens)", flush=True)
    print("Press Ctrl+C when the session is done.", flush=True)

    ser: serial.Serial | None = None
    boot_wait_done = False
    last_fsync_ms = 0.0
    fsync_interval_s = 0.25

    with out_path.open("ab") as log:
        try:
            while True:
                if ser is None or not ser.is_open:
                    if not Path(args.port).exists():
                        time.sleep(args.reconnect_delay)
                        continue
                    try:
                        ser = serial.Serial(args.port, args.baud, timeout=0.25)
                        ser.reset_input_buffer()
                    except serial.SerialException as exc:
                        print(f"[capture] open failed: {exc}", file=sys.stderr, flush=True)
                        time.sleep(args.reconnect_delay)
                        continue

                    if not boot_wait_done:
                        deadline = time.monotonic() + args.boot_wait
                        while time.monotonic() < deadline:
                            try:
                                pending = ser.in_waiting
                            except (serial.SerialException, OSError):
                                time.sleep(0.05)
                                continue
                            if pending:
                                chunk = ser.read(pending)
                                if chunk:
                                    log.write(chunk)
                                    _sync_log(log)
                            else:
                                time.sleep(0.05)
                        boot_wait_done = True
                    else:
                        marker = (
                            f"\n#CAPTURE_RECONNECT,{datetime.now().isoformat()},port={args.port}\n"
                        )
                        log.write(marker.encode("utf-8"))
                        _sync_log(log)
                        print("[capture] serial reconnected; appending to same log", flush=True)

                try:
                    try:
                        pending = ser.in_waiting
                    except (serial.SerialException, OSError):
                        time.sleep(0.05)
                        continue
                    chunk = ser.read(pending if pending else 4096)
                except serial.SerialException as exc:
                    if _is_transient_read_error(exc):
                        time.sleep(0.05)
                        continue
                    print(f"[capture] read failed (device gone?): {exc}", file=sys.stderr, flush=True)
                    try:
                        ser.close()
                    except serial.SerialException:
                        pass
                    ser = None
                    time.sleep(args.reconnect_delay)
                    continue

                if chunk:
                    log.write(chunk)
                    now_s = time.monotonic()
                    if now_s - last_fsync_ms >= fsync_interval_s:
                        _sync_log(log)
                        last_fsync_ms = now_s
                else:
                    time.sleep(0.05)
        finally:
            _sync_log(log)
            if ser is not None and ser.is_open:
                ser.close()

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\nCapture stopped.", flush=True)
        raise SystemExit(0)
