#!/usr/bin/env python3
"""Runtime-profiler device leg (DEC-239, runtime-profiling-layer task 3.7). Read-only on content.

1. Opens a managed capture that owns a fresh boot, so the once-per-boot `profile_boot` row lands in it.
2. Starts the host MIDI clock (transport running) with the shared `MidiClockMaster` pump.
3. Drains `!PROFILE_STATS` and then `!PROFILE_RING` batch by batch until each terminator says
   `resume=0`, through the capture owner's fenced, allowlisted request path.
4. Optionally sends one `!PROFILE_RESET` and re-reads the table.
5. Stops the transport and the capture, and prints a JSON summary; decode the log with
   `scripts/profile_report.py`.

No gesture, no slot, no SD operation. A dump row that USB had no room for is enqueued on the capture
ring and may arrive after its request's end fence, so each request waits for its own terminator in the
log rather than trusting the fenced window alone.

Usage:
    .venv/bin/python scripts/hitl_profile_dump.py --out-dir captures [--bpm 120] [--reset]
    .venv/bin/python scripts/hitl_profile_dump.py --out-dir captures --attach   # after a rig run
"""
from __future__ import annotations

import argparse
import json
import re
import sys
import time
import uuid
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import mido  # noqa: E402

from hitl.managed_capture import ManagedCaptureSession, wait_for_capture_boot_ready  # noqa: E402
from hitl.midi_clock import MidiClockMaster  # noqa: E402
from hitl.midi_io import _find_midi_port  # noqa: E402

_TERMINATOR = {
    "!PROFILE_STATS": "DIAG,profile_stat,end,",
    "!PROFILE_RING": "DIAG,profile_ring,end,",
    "!PROFILE_RESET": "DIAG,profile_reset,",
}


def _log_lines(log: Path) -> list[str]:
    return log.read_text(encoding="utf-8", errors="replace").splitlines()


def _wait_for(log: Path, needle: str, *, after: int, timeout_s: float) -> tuple[int, str | None]:
    deadline = time.monotonic() + timeout_s
    while True:
        lines = _log_lines(log)
        for i in range(after, len(lines)):
            if needle in lines[i]:
                return i, lines[i]
        if time.monotonic() >= deadline:
            return len(lines), None
        time.sleep(0.05)


def _request(session: ManagedCaptureSession, log: Path, command: str, *, timeout_s: float) -> dict:
    """One fenced request; returns its terminator (or refusal) and the row count it reported."""
    start_index = len(_log_lines(log))
    nonce, end_nonce = uuid.uuid4().hex, uuid.uuid4().hex
    accepted, detail = session.send_command(command, nonce=nonce, end_nonce=end_nonce)
    if not accepted:
        return {"command": command, "error": detail}
    session.fenced_response(nonce, end_nonce, timeout_s=timeout_s)
    _, refused = _wait_for(log, "DIAG,profile_cmd,refused", after=start_index, timeout_s=0.0)
    if refused is not None:
        return {"command": command, "refused": refused}
    _, terminator = _wait_for(log, _TERMINATOR[command], after=start_index, timeout_s=timeout_s)
    if terminator is None:
        return {"command": command, "error": "no terminator"}
    fields = dict(re.findall(r"(\w+)=(\d+)", terminator))
    return {"command": command, "terminator": terminator.split(",DIAG,", 1)[-1], **fields}


def _drain(session: ManagedCaptureSession, log: Path, command: str, *, max_batches: int,
           timeout_s: float) -> list[dict]:
    batches = []
    for _ in range(max_batches):
        result = _request(session, log, command, timeout_s=timeout_s)
        batches.append(result)
        if result.get("resume") != "1":
            break
    return batches


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out-dir", type=Path, default=Path("captures"))
    parser.add_argument("--bpm", type=float, default=120.0)
    parser.add_argument("--settle-s", type=float, default=2.0, help="clock time before the first dump")
    parser.add_argument("--max-batches", type=int, default=20)
    parser.add_argument("--request-timeout-s", type=float, default=5.0)
    parser.add_argument("--reset", action="store_true", help="also send !PROFILE_RESET and re-read stats")
    parser.add_argument("--attach", action="store_true",
                        help="attach to the running device without a reset and without starting the clock, "
                             "so the profile of a run that just finished is read back intact")
    parser.add_argument("--port-substring", default="Teensy")
    args = parser.parse_args(argv)

    summary: dict[str, object] = {"started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}
    boot_mode = "never" if args.attach else "force"
    with ManagedCaptureSession(out_dir=args.out_dir, boot_reset=boot_mode, boot_wait_s=10.0) as session:
        log = session.session_log  # `__enter__` started the capture
        if log is None:
            raise RuntimeError("capture session did not report its log")
        wait_for_capture_boot_ready(args.out_dir, log, timeout_s=40.0)
        summary["capture_log"] = str(log)
        if args.attach:
            summary["mode"] = "attach (no reset, no clock)"
            summary["stats"] = _drain(session, log, "!PROFILE_STATS", max_batches=args.max_batches,
                                      timeout_s=args.request_timeout_s)
            summary["ring"] = _drain(session, log, "!PROFILE_RING", max_batches=args.max_batches,
                                     timeout_s=args.request_timeout_s)
            summary["finished_utc"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
            print(json.dumps(summary, indent=2))
            return 0
        _, boot_row = _wait_for(log, "DIAG,profile_boot,", after=0, timeout_s=15.0)
        summary["boot_row"] = boot_row
        lines = _log_lines(log)
        summary["boot_reset"] = next((l for l in lines if "DIAG,boot_reset," in l), None)
        summary["boot_load_ok"] = any("BOOT,load,ok" in l for l in lines)
        out_name = _find_midi_port(args.port_substring, is_input=False, timeout_s=30.0)
        with mido.open_output(out_name) as out_port:
            master = MidiClockMaster(out_port)
            try:
                master.start(bpm=args.bpm)
                time.sleep(args.settle_s)
                summary["stats"] = _drain(session, log, "!PROFILE_STATS", max_batches=args.max_batches,
                                          timeout_s=args.request_timeout_s)
                summary["ring"] = _drain(session, log, "!PROFILE_RING", max_batches=args.max_batches,
                                         timeout_s=args.request_timeout_s)
                if args.reset:
                    summary["reset"] = _request(session, log, "!PROFILE_RESET",
                                                timeout_s=args.request_timeout_s)
                    summary["stats_after_reset"] = _drain(session, log, "!PROFILE_STATS",
                                                          max_batches=args.max_batches,
                                                          timeout_s=args.request_timeout_s)
                summary["transport_running_during_dumps"] = True
            finally:
                master.stop()
        time.sleep(0.5)
    summary["finished_utc"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
