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


# Read-only HITL observations this capture owner will forward on request. Anything
# else is refused by name: the capture session is the single serial owner, so an
# unfiltered passthrough would let a host-side bug issue a mutating command.
ALLOWED_HITL_COMMANDS = frozenset({
    "!LIB_OBSERVE",
    "!CAP_IDENTITY_STATS",
    "!HEAP_STATS",
    "!LIB_OBSERVE_STATUS",
    "!STOP_STATUS",
    # Read-only ring flush. The observation family is emitted through the capture
    # ring, so an idle device can hold it unflushed; the drain makes a completed
    # observation visible without mutating content.
    "!TRACE_DRAIN",
})

# How this capture treats the boot. The settle exists only to stop the harness driving a board
# that opening serial has just restarted, so the mode decides what evidence ends it:
#   auto  -- ask the device once, read-only, whether it is already stopped; attach if it is
#            (`_probe_device_stopped`), otherwise request the reset and own the boot.
#   force -- always request the reset, so the boot banner lands inside this capture.
#   never -- attach to the running device; never reset.
# `!REBOOT` is refused by the firmware while the transport runs, so a device left playing cannot
# be reset until something stops it; the managed harness owns that stop (one idempotent MIDI Stop).
BOOT_RESET_MODES = ("auto", "force", "never")

# The device's own acknowledgement of an accepted `!REBOOT` (`StorageManagerHitlSerial.cpp`), and
# the boot-load completion row it prints when the workspace restore has finished.
REBOOT_ACK_MARKER = "host-requested reboot"
BOOT_LOAD_OK_MARKER = "BOOT,load,ok"
BOOT_BANNER_MARKER = "DIAG,boot_reset,"
STOP_STATUS_MARKER = "#STOP_STATUS,"


def normalize_boot_reset_mode(value) -> str:
    """`auto` (default), `force` or `never`.

    The legacy boolean flags keep exactly their old meaning -- `True` requested the reset, `False`
    asked for a plain attach -- while the new default is `auto`, which asks the device first. Only
    an explicit mode (or `auto`) may reset a device that is not stopped.
    """
    if value is None:
        return "auto"
    if isinstance(value, bool):
        return "force" if value else "never"
    mode = str(value).strip().lower()
    if mode not in BOOT_RESET_MODES:
        raise ValueError(f"unknown boot-reset mode: {value!r}")
    return mode


def device_reports_stopped(lines) -> bool:
    """Whether the latest `!STOP_STATUS` row of ``lines`` reports a stopped selected track.

    The row is written straight to `Serial` (not through the capture ring), so it is present
    immediately. Only the newest row counts: an earlier one may predate the state it is asked about.
    """
    for line in reversed(list(lines)):
        if STOP_STATUS_MARKER in line:
            return "state=STOPPED" in line
    return False


def settle_end_reason(
    *,
    reset_expected: bool,
    saw_banner: bool,
    saw_boot_load_ok: bool,
    elapsed_s: float,
    ceiling_s: float,
):
    """Why the post-open settle may end now, or None to keep reading.

    ``--boot-wait`` is a ceiling for a device that never reaches a usable state, never the wait
    itself: an attaching run is finished the moment its state was already confirmed, and a run that
    owns the boot ends when the boot load it asked for reports completion.
    """
    if elapsed_s >= ceiling_s:
        return "ceiling"
    if not reset_expected:
        return "attach_no_reset"
    if saw_banner and saw_boot_load_ok:
        return "boot_loaded"
    return None


# The firmware accepts `!TRACE_FENCE <token>` only when the token is exactly 32
# characters, so both fence tokens are generated in that shape.
FENCE_TOKEN_LENGTH = 32


def is_fence_token(token: str) -> bool:
    return len(token) == FENCE_TOKEN_LENGTH and all(c in "0123456789abcdef" for c in token)


def parse_command_request(line: str):
    """``<start> <end> <!COMMAND>`` -> ``(start, end, command)``; malformed input returns None."""
    parts = line.strip().split()
    if len(parts) != 3:
        return None
    start, end, command = parts
    command = command.strip()
    # A command is `!` plus a name: no bare bang and no embedded whitespace, so a
    # coalesced or truncated line is refused instead of forwarded as something else.
    if not is_fence_token(start) or not is_fence_token(end) or start == end:
        return None
    if len(command) < 2 or not command.startswith("!") or any(c.isspace() for c in command):
        return None
    return start, end, command


def _service_command_requests(ser, *, enabled: bool) -> None:
    """Forward at most one allowlisted request, fenced so the host can detect loss.

    The request is framed by two fence rows carrying the caller's nonce, so a
    missing fence means the response window was lost or the device restarted --
    never an inferred success.
    """
    if not enabled or ser is None or not ser.is_open:
        return
    while True:
        try:
            ready, _, _ = select.select([sys.stdin], [], [], 0)
        except (OSError, ValueError):
            return
        if not ready:
            return
        line = sys.stdin.readline()
        if not line:
            return
        parsed = parse_command_request(line)
        if parsed is None:
            print("[capture] command refused: malformed request", flush=True)
            continue
        start, end, command = parsed
        if command not in ALLOWED_HITL_COMMANDS:
            print(f"[capture] command refused nonce={start} command={command}", flush=True)
            continue
        ser.write(f"!TRACE_FENCE {start}\n".encode("ascii"))
        ser.write(f"{command}\n".encode("ascii"))
        ser.write(f"!TRACE_FENCE {end}\n".encode("ascii"))
        ser.flush()
        print(f"[capture] command accepted nonce={start} command={command}", flush=True)


def _read_for(ser, log, *, timeout_s: float, poll_s: float = 0.05):
    """Read whatever arrives before ``timeout_s``, writing it to the capture log.

    Every byte read here is logged, so the decision this capture made is visible in its own
    evidence rather than only on stdout.
    """
    lines: list[str] = []
    buffered = b""
    deadline = time.monotonic() + max(timeout_s, 0.0)
    while True:
        try:
            pending = ser.in_waiting
        except (serial.SerialException, OSError):
            break
        chunk = b""
        if pending:
            try:
                chunk = ser.read(pending)
            except serial.SerialException as exc:
                if not _is_transient_read_error(exc):
                    break
        if chunk:
            log.write(chunk)
            _sync_log(log)
            buffered += chunk
            while b"\n" in buffered:
                raw, buffered = buffered.split(b"\n", 1)
                lines.append(raw.decode("utf-8", "replace").rstrip("\r"))
        if time.monotonic() >= deadline:
            break
        if not chunk:
            time.sleep(poll_s)
    if buffered:
        lines.append(buffered.decode("utf-8", "replace").rstrip("\r"))
    return lines


def _probe_device_stopped(ser, log, *, timeout_s: float) -> bool:
    """This capture's own read-only `!STOP_STATUS` probe, before deciding to reset.

    The transport-gated reply answers exactly the question the reset decision needs: a stopped
    device is one the harness can drive straight away, and one that does not answer is the state a
    fresh boot exists to fix. The probe is read-only and is not fenced, so it stays outside the
    host's fenced read-only request channel.
    """
    try:
        ser.write(b"!STOP_STATUS\n")
        ser.flush()
    except (serial.SerialException, OSError) as exc:
        print(f"[capture] state probe could not be written: {exc}", flush=True)
        return False
    lines = _read_for(ser, log, timeout_s=timeout_s)
    stopped = device_reports_stopped(lines)
    detail = next((line for line in reversed(lines) if STOP_STATUS_MARKER in line), "no #STOP_STATUS reply")
    print(f"[capture] state probe: stopped={int(stopped)} {detail.strip()}", flush=True)
    return stopped


def _request_boot_reset(ser, log, *, timeout_s: float) -> bool:
    """Request the reset and report whether the firmware acknowledged it.

    `!REBOOT` is refused while the transport runs, and a refused request prints nothing: waiting a
    fixed window for a boot banner that was never asked for is what made a run with a left-playing
    transport look like a slow boot. Read for the acknowledgement instead.
    """
    try:
        ser.write(b"!REBOOT\n")
        ser.flush()
    except (serial.SerialException, OSError) as exc:
        print(f"[capture] reset request could not be written: {exc}", flush=True)
        return False
    lines = _read_for(ser, log, timeout_s=timeout_s)
    acknowledged = any(REBOOT_ACK_MARKER in line for line in lines)
    if acknowledged:
        print("[capture] reset acknowledged by the device", flush=True)
    else:
        print(
            "[capture] reset refused (no acknowledgement): !REBOOT is transport-gated, so the "
            "transport is most likely still running",
            flush=True,
        )
    return acknowledged


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
        help="Ceiling in seconds for the post-open settle (default 10). The settle ends on "
             "evidence that the device is up, not on this timeout",
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
    parser.add_argument(
        "--boot-reset",
        action="store_true",
        default=None,
        help="Deprecated alias for --boot-reset-mode force (always request the reset first)",
    )
    parser.add_argument(
        "--boot-reset-mode",
        choices=BOOT_RESET_MODES,
        default=None,
        help="auto (default): ask the device once whether it is already stopped and attach "
             "without a reset when it is; force: always request the reset, so the boot banner "
             "lands inside this capture (verdict-grade fresh-boot evidence); never: attach to "
             "the running device and never reset",
    )
    parser.add_argument(
        "--state-probe-s",
        type=float,
        default=2.0,
        help="auto mode: seconds to wait for this capture's own read-only !STOP_STATUS probe",
    )
    parser.add_argument(
        "--reset-ack-s",
        type=float,
        default=1.0,
        help="Seconds to wait for the firmware's !REBOOT acknowledgement before reporting "
             "the refusal by name",
    )
    parser.add_argument(
        "--boot-reset-settle",
        type=float,
        default=2.0,
        help="Delay after the reset request before the reconnect loop reopens the port",
    )
    parser.add_argument(
        "--command-stdin",
        action="store_true",
        help="Forward allowlisted read-only HITL requests read from stdin, fenced by nonce",
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

    # The legacy `--boot-reset` boolean keeps its old meaning (force the reset); the mode is what
    # decides whether a device that is already in the right state is worth a fresh boot at all.
    boot_reset_mode = normalize_boot_reset_mode(
        args.boot_reset_mode if args.boot_reset_mode is not None else args.boot_reset
    )

    args.out_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_path = args.out_dir / f"session_{stamp}.log"
    pointer = args.out_dir / ".current_session"
    ready_pointer = args.out_dir / ".current_session.ready"
    if ready_pointer.exists():
        ready_pointer.unlink()
    pointer.write_text(str(out_path) + "\n", encoding="utf-8")

    print(f"Recording {args.port} -> {out_path}", flush=True)
    print(
        f"Boot mode: {boot_reset_mode} (settle ceiling {args.boot_wait:.1f}s; the settle ends on "
        "device evidence, not on the ceiling)",
        flush=True,
    )
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

    boot_decision_done = False

    # True when this capture owns a boot: it asked for the reset, so a missing banner is missing
    # evidence and not an attaching run.
    reset_expected = False
    boot_banner_seen = False
    boot_load_ok_seen = False
    settle_buffer = b""
    boot_reset_settle_s = max(args.boot_reset_settle, 0.0)
    state_probe_s = max(args.state_probe_s, 0.0)
    reset_ack_s = max(args.reset_ack_s, 0.0)
    settle_ceiling_s = max(args.boot_wait, 0.0)

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

                    if boot_reset_mode != "never" and not boot_decision_done:
                        # The reset decision is a state question, asked once, read-only: a device that
                        # is already stopped can be driven straight away (`s4f22-base-device-16`
                        # spent its whole setup budget waiting for a boot-time deferred save that the
                        # firmware owns, and never recorded). Only a device that is *not* in that
                        # state is worth a fresh boot. `!REBOOT` is refused while the transport runs,
                        # so a refusal is reported by name and left to the harness, which owns MIDI
                        # and can stop the transport once.
                        boot_decision_done = True
                        already_stopped = (
                            boot_reset_mode == "auto"
                            and _probe_device_stopped(ser, log, timeout_s=state_probe_s)
                        )
                        if already_stopped:
                            print(
                                "[capture] device is already stopped; attaching without a boot reset",
                                flush=True,
                            )
                        else:
                            reset_expected = _request_boot_reset(ser, log, timeout_s=reset_ack_s)
                            try:
                                ser.close()
                            except (serial.SerialException, OSError):
                                pass
                            ser = None
                            if not reset_expected:
                                print(
                                    "[capture] continuing without a reset: the harness owns the "
                                    "transport stop and can request a fresh boot afterwards",
                                    flush=True,
                                )
                            time.sleep(boot_reset_settle_s)
                            continue
                    if not boot_wait_done:
                        started = time.monotonic()
                        reason = None
                        elapsed = 0.0
                        while reason is None:
                            try:
                                pending = ser.in_waiting
                            except (serial.SerialException, OSError):
                                pending = 0
                            chunk = b""
                            if pending:
                                chunk = ser.read(pending)
                            if chunk:
                                log.write(chunk)
                                _sync_log(log)
                                settle_buffer += chunk
                                while b"\n" in settle_buffer:
                                    raw, settle_buffer = settle_buffer.split(b"\n", 1)
                                    line = raw.decode("utf-8", "replace")
                                    if BOOT_BANNER_MARKER in line:
                                        boot_banner_seen = True
                                    if BOOT_LOAD_OK_MARKER in line:
                                        boot_load_ok_seen = True
                            else:
                                time.sleep(0.05)
                            elapsed = time.monotonic() - started
                            reason = settle_end_reason(
                                reset_expected=reset_expected,
                                saw_banner=boot_banner_seen,
                                saw_boot_load_ok=boot_load_ok_seen,
                                elapsed_s=elapsed,
                                ceiling_s=settle_ceiling_s,
                            )
                        print(
                            f"[capture] boot settle complete ({reason} after {elapsed:.1f}s)",
                            flush=True,
                        )
                        if reset_expected and not boot_load_ok_seen:
                            # A settle that ran out its ceiling has no fresh-boot evidence to offer;
                            # say which one is missing instead of recording a reset that never landed.
                            print(
                                "[capture] WARNING: no boot "
                                + ("banner" if not boot_banner_seen else "load completion")
                                + " within the settle ceiling; this capture has no fresh-boot evidence",
                                flush=True,
                            )
                        if args.run_token:
                            ser.write(f"!TRACE_FENCE {args.run_token}\n".encode("ascii"))
                            ser.flush()
                        boot_wait_done = True
                        ready_pointer.write_text(str(out_path) + "\n", encoding="utf-8")
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

                _service_command_requests(ser, enabled=args.command_stdin)
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
