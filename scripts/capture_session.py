#!/usr/bin/env python3
"""Record Teensy USB serial to a capture log (Bucket 1 S1)."""
from __future__ import annotations

import argparse
import os
import select
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
    parser.add_argument(
        "--arm-snapshot-yield",
        action="store_true",
        help="Write !SNAPSHOT_YIELD once after the first timing-critical ST line",
    )
    parser.add_argument("--run-token", default=None,
                        help="Hex token echoed by capture firmware with its device timestamp")
    parser.add_argument(
        "--owner-fd",
        type=int,
        default=-1,
        help="Read end of a parent-death pipe; exit when the owner closes it",
    )
    args = parser.parse_args()
    if args.run_token is not None and (len(args.run_token) != 32 or
            any(c not in "0123456789abcdef" for c in args.run_token)):
        parser.error("--run-token must be 32 lowercase hexadecimal characters")

    args.out_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_path = args.out_dir / f"session_{stamp}.log"
    pointer = args.out_dir / ".current_session"
    ready_pointer = args.out_dir / ".current_session.ready"
    if ready_pointer.exists():
        ready_pointer.unlink()
    pointer.write_text(str(out_path) + "\n", encoding="utf-8")

    print(f"Recording {args.port} -> {out_path}", flush=True)
    print(f"Boot wait: {args.boot_wait:.1f}s (Teensy resets when serial opens)", flush=True)
    print("Press Ctrl+C when the session is done.", flush=True)

    ser: serial.Serial | None = None
    boot_wait_done = False
    last_fsync_ms = 0.0
    fsync_interval_s = 0.25
    yield_armed = False
    line_buffer = b""
    # Orphan guard: when the owning harness exits (including SIGKILL, which gives
    # it no chance to stop us), this process must exit promptly so the USB serial
    # port is never held by a leaked capture session. The owner-death pipe is the
    # primary signal (it fires even when the owner is an unreaped zombie); the
    # reparent check is a fallback.
    owner_pid = os.getppid()
    owner_fd = args.owner_fd

    def _owner_gone() -> bool:
        if owner_fd >= 0:
            try:
                readable, _, _ = select.select([owner_fd], [], [], 0)
            except (OSError, ValueError):
                return True
            if readable:
                try:
                    return os.read(owner_fd, 1) == b""
                except OSError:
                    return True
        return os.getppid() != owner_pid

    timing_critical_names = (
        b",RECORDING",
        b",PLAYING",
        b",OVERDUBBING",
        b",STOPPED_RECORDING",
        b",ARMED",
    )

    def _arm_yield_from_chunk(chunk: bytes) -> None:
        nonlocal yield_armed, line_buffer
        if yield_armed or not args.arm_snapshot_yield or ser is None or not ser.is_open:
            return
        line_buffer += chunk
        while b"\n" in line_buffer:
            raw, line_buffer = line_buffer.split(b"\n", 1)
            if b",ST," not in raw:
                continue
            if not any(name in raw for name in timing_critical_names):
                continue
            ser.write(b"!SNAPSHOT_YIELD\n")
            ser.flush()
            yield_armed = True
            print("[capture] armed snapshot yield", flush=True)
            return

    with out_path.open("ab") as log:
        try:
            while True:
                if _owner_gone():
                    print("[capture] owner process exited; stopping capture", flush=True)
                    break
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
                        if args.run_token:
                            ser.write(f"!TRACE_FENCE {args.run_token}\n".encode("ascii"))
                            ser.flush()
                        boot_wait_done = True
                        ready_pointer.write_text(str(out_path) + "\n", encoding="utf-8")
                        print("[capture] boot settle complete", flush=True)
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
                    _arm_yield_from_chunk(chunk)
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
