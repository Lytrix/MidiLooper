#!/usr/bin/env python3
"""S4.1 device leg: does a **same-track switch after retirement** release the retired slot's lease?

S4.1's release surface is verified natively (`test_runtime_working_set_ownership`, 23/23), but its **device
row** has never been observed. `DIAG,ws_release,<slot>,<generation>,<outcome>` already exists and is Tier-A
(`bbedfeac`); what is owed is a run in which the path fires for the slot under test.

The route, established by the G0 trace and confirmed by the 2026-09-27 campaign:

  * a lease is acquired only when `Track::prewarmPlaybackForSlot` is called with `prepareCapture == true`, and
    only the record arm and the boot-cold overdub start do that — so a lease exists only on the track that
    armed the record;
  * the cross-track sweep **excludes the capturing track**, so a take start can never sweep a slot that holds
    one (route (a) in the packet is retired as structurally impossible);
  * therefore the reachable route is the **next playback reset on the same track** — a slot switch — which
    runs `releasePlaybackStateForSlot` -> `invalidatePlaybackMergedMidiEvents` ->
    `releaseEligiblePlaybackRuntimeSlots` -> `releasePlaybackRuntimeSlot(slot, generation)`.

The protocol this drives, on one track with an empty target slot:

    select track -> select slot A (empty) -> start transport -> record A (a take with content)
    -> stop A -> wait for the finalizer to retire A -> select slot B (a different slot)
    -> the switch's playback reset releases A

and the oracle is the device row itself: `DIAG,ws_release,<A>,<generation>,released`.

**No upload.** This script attaches to the device as it is; the device's own timestamps bound the window. It
never clears or writes any slot other than recording into the named target, which is the point of the run.

Usage:
  .venv/bin/python scripts/hitl_s41_lease_release_probe.py \
      --track 8 --slot-a 4 --slot-b 5 --out-dir captures/s41-lease-<ts>
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

# Wire form: DIAG,ws_release,<slot>,<generation>,<outcome>. Slot and generation use the firmware's 0-based
# slot identity, which is what `#CAP` rows and the census use.
WS_RELEASE_RE = re.compile(r"^DIAG,ws_release,(\d+),(\d+),([a-z_]+)$")
CAP_RE = re.compile(r"^#CAP,(\d+),(.*)$")
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
    """Make the transport running or stopped, requiring the transition rather than assuming it.

    Pressing the global transport note toggles it, so a blind press when it is already in the wanted state
    flips it the wrong way. The device's own `Transport started`/`Transport stopped` lines are readable, so
    the state is read first and the transition is then required.
    """
    tag = f"{label}_" if label else ""
    for attempt in range(attempts):
        if _transport_running(capture_log) == running:
            events.append({"event": f"{tag}transport_{'start' if running else 'stop'}", "attempt": attempt})
            return True
        before = len(_lines(capture_log))
        _send_short_press(out_port, note=GLOBAL_TRANSPORT_NOTE,
                          channel_1based=CONTROL_CHANNEL_1BASED, press_ms=press_ms)
        want = TRANSPORT_STARTED if running else TRANSPORT_STOPPED
        _wait_for(capture_log, before, (want,), timeout_s=5.0)
        time.sleep(0.5)
    ok = _transport_running(capture_log) == running
    events.append({"event": f"{tag}transport_{'start' if running else 'stop'}", "confirmed": ok})
    return ok


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--track", type=int, required=True, choices=range(1, 9))
    p.add_argument("--slot-a", type=int, required=True, choices=range(1, 9),
                   help="the retired slot: recorded into, then switched away from")
    p.add_argument("--slot-b", type=int, required=True, choices=range(1, 9),
                   help="the switch target; must differ from A")
    p.add_argument("--out-dir", type=Path, required=True)
    p.add_argument("--press-ms", type=int, default=120)
    p.add_argument("--bars", type=int, default=2)
    p.add_argument("--notes", type=int, default=16)
    p.add_argument("--phase-timeout-s", type=float, default=20.0)
    p.add_argument("--retire-timeout-s", type=float, default=25.0)
    p.add_argument("--settle-s", type=float, default=12.0,
                   help="transport-running settle after the switch, so a quantized switch commits and the "
                        "maintenance lane can run the release sweep inside the window")
    p.add_argument("--capture-port", default=DEFAULT_CAPTURE_SERIAL_PORT)
    p.add_argument("--boot-wait-s", type=float, default=0.0,
                   help="0 by default: this run must NOT reset the device")
    args = p.parse_args(argv)

    if args.slot_a == args.slot_b:
        p.error("--slot-a and --slot-b must differ: the release needs a switch away from A")

    out_dir: Path = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=False)
    a0, b0, t0 = args.slot_a - 1, args.slot_b - 1, args.track - 1

    events: list[dict] = []
    report: dict = {"track": args.track, "slot_a": args.slot_a, "slot_b": args.slot_b,
                    "events": events, "result": "INCOMPLETE", "upload": False}

    with ManagedCaptureSession(port=args.capture_port, boot_wait_s=args.boot_wait_s,
                              out_dir=out_dir, run_token=uuid.uuid4().hex) as capture:
        capture_log = capture.session_log
        if capture_log is None:
            raise RuntimeError("managed capture did not publish a session log")

        # The run window is bounded by the CAPTURE FILE's own row count at attach, deliberately NOT by a
        # device timestamp. The device emits at least two timestamp domains -- `#CAP,<us>` and the
        # `#DMA_XFER,<ms>` census run at different bases -- so "the newest timestamp" is not comparable across
        # rows, and anchoring on it silently excluded this run's own telemetry (measured 2026-09-27: a window
        # of 0 rows while the run logged 21,319 lines). A capture session starts recording when this script
        # starts it, so the file offset at attach is exactly the boundary between the pre-existing buffer and
        # this run.
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
                context="s41-lease-release-probe",
                events=events) as shutdown_session:

            # --- select the track, then the target slot while STOPPED -------------------------------
            # A slot press under a running transport takes the playback-switch branch; while stopped it arms
            # the record path, which is the admission that establishes the working set (and its lease).
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
                # A cleared slot can take the arm-and-wait path, where the first press only arms.
                slots.press_slot_record(ctx, args.slot_a, press_ms=args.press_ms)
                events.append({"event": "slot_a_second_press"})
                _wait_for(capture_log, before,
                          (",ST,Track,STOPPED,ARMED", "Start Recording", ",ST,Track,STOPPED,RECORDING"),
                          timeout_s=4.0)
            events.append({"event": "slot_a_selected", "confirmed": armed})

            # --- record A ---------------------------------------------------------------------------
            _ensure_transport(out_port, capture_log, running=True, press_ms=args.press_ms,
                              events=events, label="record_a_start")
            recording = _wait_for(capture_log, before,
                                  ("Recording started", ",RECA,", ",ST,Track,ARMED,RECORDING"),
                                  timeout_s=args.phase_timeout_s)
            events.append({"event": "slot_a_recording", "confirmed": recording})
            if not recording:
                raise RuntimeError(
                    f"slot {args.slot_a} never started recording; it is probably not an empty target. This "
                    "script deliberately does not clear it -- choose an empty slot (the pre-run census lists "
                    "them) or clear one under a separate authorization.")

            span_s = max(args.bars, 1) * 2.0
            gap_s = span_s / (args.notes + 1)
            for i in range(args.notes):
                pitch = 60 + (i % 12)
                out_port.send(mido.Message("note_on", note=pitch, velocity=100, channel=t0))
                time.sleep(0.03)
                out_port.send(mido.Message("note_off", note=pitch, velocity=0, channel=t0))
                time.sleep(max(gap_s - 0.03, 0.01))

            # --- stop A and let the finalizer retire -------------------------------------------------
            stopped_at = len(_lines(capture_log))
            _send_short_press(out_port, note=GLOBAL_TRANSPORT_NOTE,
                              channel_1based=CONTROL_CHANNEL_1BASED, press_ms=args.press_ms)
            committed = _wait_for(capture_log, stopped_at,
                                  (",RECS,stop", ",ST,Track,RECORDING,STOPPED_RECORDING",
                                   ",ST,Track,RECORDING,STOPPED"),
                                  timeout_s=args.phase_timeout_s)
            events.append({"event": "slot_a_committed", "confirmed": committed})
            if not committed:
                raise RuntimeError(f"slot {args.slot_a} never committed its take")

            # The finalizer's own edges: the release marker is armed at retirement and consumed by a later
            # maintenance turn, so the switch must come after the window opened.
            retired = _wait_for(capture_log, stopped_at,
                                (f"Reentry window open slot={a0}", f"DIAG,canonical_release,{a0},"),
                                timeout_s=args.retire_timeout_s)
            events.append({"event": "slot_a_finalizer_retired", "confirmed": retired})

            # --- the switch: this playback reset is what releases A's lease --------------------------
            # Land it on a MIDI boundary, which is the switch contract (S4b.3).
            switch_mark = len(_lines(capture_log))
            _ensure_transport(out_port, capture_log, running=True, press_ms=args.press_ms,
                              events=events, label="switch_transport")
            time.sleep(0.3)
            slots.press_slot_record(ctx, args.slot_b, press_ms=args.press_ms)
            events.append({"event": "switched_to_slot_b", "slot": args.slot_b})
            # A switch can be **quantized** -- the run of 2026-09-27 logged "Queued playback switch" and the
            # 3 s that followed were not enough for it to commit, so nothing was released inside the window.
            # Keep the transport running until the switch actually reports, bounded by the phase timeout, and
            # then let the maintenance lane run the release sweep.
            switch_deadline = time.monotonic() + args.phase_timeout_s
            switch_seen = False
            while time.monotonic() < switch_deadline:
                tail = " ".join(_lines(capture_log)[switch_mark:])
                if "Playback started" in tail or "Switch gesture from slot" in tail:
                    switch_seen = True
                    break
                time.sleep(0.2)
            events.append({"event": "slot_b_switch_committed", "confirmed": switch_seen})
            time.sleep(args.settle_s)

            _ensure_transport(out_port, capture_log, running=False, press_ms=args.press_ms,
                              events=events, label="final")
            report["result"] = "DROVE_SEQUENCE"

    report["shutdown"] = shutdown_session.result.as_dict()
    if not shutdown_session.result.ok:
        report["result"] = f"SHUTDOWN_{shutdown_session.result.status.upper()}"

    # --- the oracle: the device row, in the window this run created --------------------------------
    releases: list[dict] = []
    window_rows = 0
    switch_human: list[str] = []
    for line in _lines(capture_log)[anchor_row:]:
        m = CAP_RE.match(line.strip())
        if m:
            window_rows += 1
            w = WS_RELEASE_RE.match(m.group(2))
            if w:
                releases.append({"us": int(m.group(1)), "slot0": int(w.group(1)),
                                 "generation": int(w.group(2)), "outcome": w.group(3),
                                 "raw": m.group(2)})
        elif "Switch gesture from slot" in line or "Playback started" in line:
            switch_human.append(line.strip()[:150])

    report["anchor_row"] = anchor_row
    report["window_rows"] = window_rows
    report["switch_human"] = switch_human[-6:]
    report["ws_release_rows"] = releases
    target0 = args.slot_a - 1
    hit = [r for r in releases if r["slot0"] == target0 and r["outcome"] == "released"]
    report["leased_slot_released"] = bool(hit)
    if hit:
        report["result"] = "LEASE_RELEASE_OBSERVED"
    elif releases:
        report["result"] = "LEASE_ROWS_BUT_NOT_THE_TARGET"
    else:
        report["result"] = "NO_WS_RELEASE_ROW"

    report["session_log"] = str(capture_log)
    (out_dir / "s41-lease-release.json").write_text(json.dumps(report, indent=2) + "\n")

    print(json.dumps({k: v for k, v in report.items() if k != "events"}, indent=2))
    print("[s41] events:")
    for e in events:
        print(f"  {e}")
    if hit:
        print(f"[s41] S4.1 DEVICE ROW OBSERVED: {hit[0]['raw']} at {hit[0]['us']} us "
              f"(slot {args.slot_a}, generation {hit[0]['generation']})")
        return 0
    print(f"[s41] no release row for slot {args.slot_a}; {len(releases)} ws_release row(s) elsewhere in the "
          "window. Inspect the session log before concluding the route is wrong.")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
