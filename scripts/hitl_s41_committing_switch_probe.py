#!/usr/bin/env python3
"""S4.1 device leg: a **committing** same-track switch after retirement, and the lease-release row.

`DIAG,ws_release,<slot0>,<generation>,<outcome>` exists and is Tier-A, but has never been observed in a run
whose shape could produce it. Two earlier attempts established the constraints the hard way:

  * the release runs from a **playback reset on the track**, i.e. a slot switch that *commits*;
  * the retention acceptance's 4->5->8 sequence cannot produce it, because its slot-8 switch is **retained**
    (it never commits inside the window), so no reset runs after the predecessor retires;
  * a switch taken while still recording goes through the capture-switch, and the release then depends on that
    switch committing -- which is what this sequence arranges explicitly.

So the sequence is deliberately the simplest one that can commit:

    select track -> arm+record slot A while STOPPED -> start transport -> record a take with content
    -> transport stop -> wait for the commit -> wait (bounded) for the finalizer to retire
    -> switch to slot B -> require the switch to COMMIT (a playback start on the new slot)
    -> read DIAG,ws_release for slot A

The switch target must be **empty** so the press takes the record/playback path rather than being retained
behind a filled slot.

**No upload.** The run window is the capture file's own row count at attach -- NOT a device timestamp, because
the device emits at least two timestamp domains (`#CAP,<us>` and the `#DMA_XFER,<ms>` census) and anchoring on
"the newest timestamp" silently produced an empty window once already.

Base convention, pinned because it has cost two wrong readings: this script's CLI is **1-based** (matching the
button and the HITL driver), while the firmware's `RECA`/`RECS`/`DIAG,ws_release` slot fields are **0-based**,
and the human `Loop N:` rows are 1-based. The conversion happens in exactly one place below.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
import uuid
from pathlib import Path

import mido

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from hitl.actions import slots  # noqa: E402
from hitl.config import DEFAULT_CAPTURE_SERIAL_PORT  # noqa: E402
from hitl.control_constants import CONTROL_CHANNEL_1BASED, GLOBAL_TRANSPORT_NOTE  # noqa: E402
from hitl.device_shutdown import DeviceSession  # noqa: E402
from hitl.managed_capture import ManagedCaptureSession  # noqa: E402
from hitl.midi_io import _find_midi_port, _send_short_press  # noqa: E402

CAP_RE = re.compile(r"^#CAP,(\d+),(.*)$")
WS_RELEASE_RE = re.compile(r"^DIAG,ws_release,(\d+),(\d+),([a-z_]+)$")
TRANSPORT_STARTED = "Transport started"
TRANSPORT_STOPPED = "Transport stopped"


class _Ctx:
    """Minimal ActionContext stand-in: the slot actions need only `out_port` and `config.press_ms`."""

    class _Cfg:
        press_ms = 120

    def __init__(self, out_port):
        self.out_port = out_port
        self.config = self._Cfg()


def _lines(path: Path) -> list[str]:
    return path.read_text(encoding="utf-8", errors="replace").splitlines() if path.exists() else []


def _wait_for(capture_log: Path, after: int, markers: tuple[str, ...], timeout_s: float) -> bool:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        tail = _lines(capture_log)[after:]
        if any(m in line for line in tail for m in markers):
            return True
        time.sleep(0.05)
    return False


def _transport_running(capture_log: Path) -> bool:
    state = False
    for line in _lines(capture_log):
        if TRANSPORT_STARTED in line:
            state = True
        elif TRANSPORT_STOPPED in line:
            state = False
    return state


def _ensure_transport(out_port, capture_log: Path, *, running: bool, press_ms: int,
                      events: list[dict], label: str = "", attempts: int = 3) -> bool:
    tag = f"{label}_" if label else ""
    for attempt in range(attempts):
        if _transport_running(capture_log) == running:
            events.append({"event": f"{tag}transport_{'start' if running else 'stop'}", "attempt": attempt})
            return True
        before = len(_lines(capture_log))
        _send_short_press(out_port, note=GLOBAL_TRANSPORT_NOTE,
                          channel_1based=CONTROL_CHANNEL_1BASED, press_ms=press_ms)
        _wait_for(capture_log, before, (TRANSPORT_STARTED if running else TRANSPORT_STOPPED,), timeout_s=5.0)
        time.sleep(0.5)
    ok = _transport_running(capture_log) == running
    events.append({"event": f"{tag}transport_{'start' if running else 'stop'}", "confirmed": ok})
    return ok


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--track", type=int, required=True, choices=range(1, 9))
    p.add_argument("--slot-a", type=int, required=True, choices=range(1, 9),
                   help="the predecessor: recorded into, then switched away from")
    p.add_argument("--slot-b", type=int, required=True, choices=range(1, 9),
                   help="the switch target; must be empty so the switch commits rather than being retained")
    p.add_argument("--out-dir", type=Path, required=True)
    p.add_argument("--press-ms", type=int, default=120)
    p.add_argument("--bars", type=int, default=2)
    p.add_argument("--notes", type=int, default=16)
    p.add_argument("--phase-timeout-s", type=float, default=20.0)
    p.add_argument("--retire-timeout-s", type=float, default=20.0)
    p.add_argument("--settle-s", type=float, default=10.0)
    p.add_argument("--capture-port", default=DEFAULT_CAPTURE_SERIAL_PORT)
    p.add_argument("--boot-wait-s", type=float, default=10.0,
                   help="a fresh boot drains the capture ring and gives the run a boot banner; 0 attaches to "
                        "the device as it is")
    p.add_argument("--upload", dest="upload", action="store_true", default=True,
                   help="re-upload before the run (DEFAULT): resets the device, which drains the ring and "
                        "makes the session verdict-grade")
    p.add_argument("--no-upload", dest="upload", action="store_false")
    args = p.parse_args(argv)

    if args.slot_a == args.slot_b:
        p.error("--slot-a and --slot-b must differ: the release needs a switch away from A")

    out_dir: Path = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=False)
    # 1-based (this CLI, the button, the HITL driver) -> 0-based (firmware rows). The one conversion site.
    a0, b0, t0 = args.slot_a - 1, args.slot_b - 1, args.track - 1

    events: list[dict] = []
    report: dict = {"track": args.track, "slot_a": args.slot_a, "slot_b": args.slot_b,
                    "slot_a_firmware": a0, "events": events, "result": "INCOMPLETE",
                    "upload": bool(args.upload)}

    if args.upload:
        # A reset is the dangerous step when the previous session left persistence unfinished, so honour the
        # latch rather than resetting on top of it (the 2026-09-26 incident).
        from hitl.device_shutdown import refuse_reset_if_latched

        refusal = refuse_reset_if_latched(tool="hitl_s41_committing_switch_probe")
        if refusal is not None:
            print(refusal)
            report["result"] = "RESET_REFUSED_AFTER_INCOMPLETE_SHUTDOWN"
            (out_dir / "s41-lease-release.json").write_text(json.dumps(report, indent=2) + "\n")
            return 4
        import subprocess
        print("[s41] re-uploading teensy41-capture-serial to force a real reset before the run")
        subprocess.run(["pio", "run", "-e", "teensy41-capture-serial", "-t", "upload"], check=True)
        time.sleep(1.0)

    with ManagedCaptureSession(port=args.capture_port, boot_wait_s=args.boot_wait_s,
                              out_dir=out_dir, run_token=uuid.uuid4().hex) as capture:
        capture_log = capture.session_log
        if capture_log is None:
            raise RuntimeError("managed capture did not publish a session log")

        anchor_row = len(_lines(capture_log))
        report["anchor_row"] = anchor_row
        print(f"[s41] attached; run window starts at capture row {anchor_row}")

        out_port = mido.open_output(_find_midi_port("Teensy", is_input=False))
        ctx = _Ctx(out_port)

        with DeviceSession(
                capture_log_getter=lambda: capture_log,
                send_transport_stop=lambda: _ensure_transport(
                    out_port, capture_log, running=False, press_ms=args.press_ms, events=events,
                    label="shutdown"),
                out_dir=out_dir,
                context="s41-committing-switch-probe",
                events=events) as shutdown_session:

            # --- arm + record A while STOPPED -------------------------------------------------------
            slots.press_track(ctx, args.track, press_ms=args.press_ms)
            time.sleep(0.4)
            if not _ensure_transport(out_port, capture_log, running=False, press_ms=args.press_ms,
                                     events=events, label="arm"):
                raise RuntimeError("could not stop the transport before arming the target")

            before = len(_lines(capture_log))
            slots.press_slot_record(ctx, args.slot_a, press_ms=args.press_ms)
            armed = _wait_for(capture_log, before,
                              (",ST,Track,STOPPED,ARMED", "Start Recording", ",ST,Track,STOPPED,RECORDING"),
                              timeout_s=4.0)
            if not armed:
                slots.press_slot_record(ctx, args.slot_a, press_ms=args.press_ms)
                events.append({"event": "slot_a_second_press"})
                _wait_for(capture_log, before,
                          (",ST,Track,STOPPED,ARMED", "Start Recording", ",ST,Track,STOPPED,RECORDING"),
                          timeout_s=4.0)
            events.append({"event": "slot_a_armed", "confirmed": armed})

            _ensure_transport(out_port, capture_log, running=True, press_ms=args.press_ms,
                              events=events, label="record_start")
            recording = _wait_for(capture_log, before,
                                  ("Recording started", ",RECA,", ",ST,Track,ARMED,RECORDING"),
                                  timeout_s=args.phase_timeout_s)
            events.append({"event": "slot_a_recording", "confirmed": recording})
            if not recording:
                raise RuntimeError(
                    f"slot {args.slot_a} never started recording; it is probably not an empty target. This "
                    "script does not clear it -- pick an empty slot from the pre-run observation.")

            span_s = max(args.bars, 1) * 2.0
            gap_s = span_s / (args.notes + 1)
            for i in range(args.notes):
                pitch = 60 + (i % 12)
                out_port.send(mido.Message("note_on", note=pitch, velocity=100, channel=t0))
                time.sleep(0.03)
                out_port.send(mido.Message("note_off", note=pitch, velocity=0, channel=t0))
                time.sleep(max(gap_s - 0.03, 0.01))

            # --- stop A and let the finalizer retire ------------------------------------------------
            stop_mark = len(_lines(capture_log))
            _send_short_press(out_port, note=GLOBAL_TRANSPORT_NOTE,
                              channel_1based=CONTROL_CHANNEL_1BASED, press_ms=args.press_ms)
            committed = _wait_for(capture_log, stop_mark,
                                  (",RECS,stop", ",ST,Track,RECORDING,STOPPED_RECORDING",
                                   ",ST,Track,RECORDING,STOPPED"),
                                  timeout_s=args.phase_timeout_s)
            events.append({"event": "slot_a_committed", "confirmed": committed})
            if not committed:
                raise RuntimeError(f"slot {args.slot_a} never committed its take")

            retired = _wait_for(capture_log, stop_mark,
                                (f"Reentry window open slot={a0}", f"DIAG,canonical_release,{a0},"),
                                timeout_s=args.retire_timeout_s)
            events.append({"event": "slot_a_finalizer_retired", "confirmed": retired})

            # --- the switch, and it must COMMIT -----------------------------------------------------
            # A retained switch leaves the active slot where it was, so no playback reset runs and the release
            # has no caller. Success is therefore a playback start on the new loop.
            switch_mark = len(_lines(capture_log))
            _ensure_transport(out_port, capture_log, running=True, press_ms=args.press_ms,
                              events=events, label="switch_transport")
            time.sleep(0.3)
            slots.press_slot_record(ctx, args.slot_b, press_ms=args.press_ms)
            events.append({"event": "switched_to_slot_b", "slot": args.slot_b})
            switch_committed = _wait_for(capture_log, switch_mark,
                                         (f"Loop {args.slot_b}: Switch gesture from slot", "Playback started"),
                                         timeout_s=args.phase_timeout_s)
            events.append({"event": "slot_b_switch_committed", "confirmed": switch_committed})
            time.sleep(args.settle_s)

            _ensure_transport(out_port, capture_log, running=False, press_ms=args.press_ms,
                              events=events, label="final")
            report["result"] = "DROVE_SEQUENCE"

    report["shutdown"] = shutdown_session.result.as_dict()
    if not shutdown_session.result.ok:
        report["result"] = f"SHUTDOWN_{shutdown_session.result.status.upper()}"

    # --- the oracle: the device row, in the window this run created ------------------------------
    releases: list[dict] = []
    window_rows = 0
    switch_rows: list[str] = []
    for line in _lines(capture_log)[anchor_row:]:
        m = CAP_RE.match(line.strip())
        if m:
            window_rows += 1
            w = WS_RELEASE_RE.match(m.group(2))
            if w:
                releases.append({"us": int(m.group(1)), "slot0": int(w.group(1)),
                                 "generation": int(w.group(2)), "outcome": w.group(3),
                                 "raw": m.group(2)})
        elif "Switch gesture from slot" in line or "Playback started @ tick" in line:
            switch_rows.append(line.strip()[:150])

    report["window_rows"] = window_rows
    report["switch_rows"] = switch_rows[-8:]
    report["ws_release_rows"] = releases
    hit = [r for r in releases if r["slot0"] == a0 and r["outcome"] == "released"]
    report["slot_a0"] = a0
    report["slot_a_lease_released"] = bool(hit)
    if hit:
        report["result"] = "LEASE_RELEASE_OBSERVED"
    elif releases:
        report["result"] = "LEASE_ROWS_BUT_NOT_SLOT_A"
    else:
        report["result"] = "NO_WS_RELEASE_ROW"

    report["session_log"] = str(capture_log)
    (out_dir / "s41-lease-release.json").write_text(json.dumps(report, indent=2) + "\n")

    print(json.dumps({k: v for k, v in report.items() if k != "events"}, indent=2))
    for e in events:
        print(f"  {e}")
    if hit:
        print(f"\n[s41] S4.1 DEVICE ROW OBSERVED for slot {args.slot_a} (firmware slot {a0}): "
              f"{hit[0]['raw']} at {hit[0]['us']} us")
        return 0
    print(f"\n[s41] no release row for slot {args.slot_a} (firmware slot {a0}); "
          f"{len(releases)} ws_release row(s) in the window.")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
