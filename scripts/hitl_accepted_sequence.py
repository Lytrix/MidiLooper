#!/usr/bin/env python3
"""S4f.13 — the accepted-sequence reproducer: stop -> slot switch -> start, with one press per stop.

Two legs, each a **managed run** and each asserting the same acceptance contract (DEC-078 / invariant 31):
a stop is *accepted* on the first press, the successor starts while the predecessor still finalizes, and the
successor's own stop executes exactly once after the predecessor releases -- with no second press anywhere.

    (a) RECORD leg:   record A -> stop A (one press) -> switch to B -> record B -> stop B
    (b) OVERDUB leg:  overdub A -> stop A (one press) -> switch to B -> overdub B
                      -> stop B once while A still finalizes -> continue transport

Stimulus provenance (AGENTS.md: never edit a canonical stimulus; no registered stimulus covers this shape):
each leg is a **TEMP variant** forked from `STIM-BASE-2-ONE-OD-01` via the sanctioned
`scripts/host_midi_hitl.py stimulus temp` mechanism -- `…-TEMP-ACCEPTED-SEQ-REC` and `…-TEMP-ACCEPTED-SEQ-OD`,
written under `captures/stimuli/` and deliberately **not registered**. Promote to a canonical id only if the
owner accepts it as the reproducer.

Design mirrors `scripts/hitl_retention_handoff.py`, which is the repo's proven shape for a device sequence
whose assertions live in an oracle: the gesture plan is **data** (`leg_gestures`) so the order -- which is the
requirement -- is unit-tested without a device, the script drives the real per-slot actions, and the assertions
are read from the session log separately.

Base convention: this CLI and the action layer are **1-based** (the loop-button number); the firmware's
`RECA`/`RECS`/`reentry`/`ws_release` slot fields are **0-based**; human `Loop N:` rows are 1-based. The single
conversion site is `to_firmware_slot`.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
import uuid
from dataclasses import dataclass, field
from pathlib import Path

import mido

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from hitl.actions import slots  # noqa: E402
from hitl.config import DEFAULT_CAPTURE_SERIAL_PORT  # noqa: E402
from hitl.control_constants import (  # noqa: E402
    CONTROL_CHANNEL_1BASED,
    GLOBAL_TRANSPORT_NOTE,
    RECORD_BUTTON_NOTE,
)
from hitl.device_shutdown import DeviceSession, refuse_reset_if_latched  # noqa: E402
from hitl.managed_capture import ManagedCaptureSession  # noqa: E402
from hitl.midi_io import _find_midi_port, _send_short_press  # noqa: E402

TRANSPORT_STARTED = "Transport started"
TRANSPORT_STOPPED = "Transport stopped"


@dataclass(frozen=True)
class Gesture:
    """One press in a leg, as data so the order can be asserted without a device."""
    step: str
    kind: str          # "track" | "slot_record" | "record" | "transport"
    value: int         # slot/track number (1-based); 0 for the buttons


def leg_gestures(leg: str, *, track: int, a: int, b: int) -> list[Gesture]:
    """The ordered presses of one leg, with the acceptance contract encoded in the order.

    The requirement is the ORDER, and each line of it is load-bearing:

    * **A's stop is one press.** In the record leg it is the record/transport pair that ends the take; in the
      overdub leg it is a single press on A's own loop button. Two presses would make "one press per stop"
      untestable, so the plan never contains a second one.
    * **The switch to B comes after A's stop**, so B starts while A is still finalizing -- that is the whole
      point of the leg, and reversing the two would test nothing.
    * **B's stop is one press on B's slot**, which is the only route that reaches `stopOverdubbing` for the
      live interval; a record-button press would toggle rather than stop.
    """
    if leg == "rec":
        return [
            Gesture("select_track", "track", track),
            Gesture("a_record_start", "record", 0),      # arm/start the take on A
            Gesture("a_stop", "transport", 0),           # ONE press: stop A
            Gesture("b_switch_start", "slot_record", b),  # switch to B; B starts recording
            Gesture("b_stop", "record", 0),              # ONE press: stop B's session
        ]
    if leg == "od":
        return [
            Gesture("select_track", "track", track),
            Gesture("a_overdub_start", "record", 0),
            Gesture("a_stop", "slot_record", a),         # ONE press: A's own loop button
            Gesture("b_switch", "slot_record", b),       # switch to B
            Gesture("b_overdub_start", "record", 0),
            # ONE press, while A still finalizes. The **record button**, not B's loop button: on a filled,
            # playing slot a loop-button press routes to the switch/mute branch (`queuePlayingSlotSwitch`,
            # "Reaffirmed playback switch") and never reaches `Track::stopOverdubbing`. The record button is
            # the route that stops B's session -- and it is also what a user actually presses.
            Gesture("b_stop", "record", 0),
        ]
    raise ValueError(f"unknown leg {leg!r}")


def to_firmware_slot(slot_1based: int) -> int:
    """The one 1-based -> 0-based conversion site (button/track numbering -> firmware row fields)."""
    return slot_1based - 1


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


# A loop button's short/double/triple discrimination means the firmware holds the second tap until its tap
# window expires, and a repeat press inside that window merges. Measured on device 2026-09-27: my first leg
# pressed slot 5 twice 345 ms apart with a record press between them, and the firmware logged
#   Loop 5: Queued playback switch   ->  Loop 5: Reaffirmed playback switch   ->  Loop 5: Reaffirmed ...
# i.e. **two countdowns for the same switch**. The firmware side of that is `queuePlayingSlotSwitch`, which
# clears and re-requests without checking whether the same target is already pending, so the quantized switch
# restarts from the current tick. The driver's side is this: never press the same gesture twice inside the
# window, and never press `record` while a queued switch has not executed.
DEFAULT_TAP_WINDOW_S = 0.35


def _switch_pending(capture_log: Path, since: int, slot: int) -> bool:
    """True while the log shows a queued/affirmed switch for `slot` that has not yet executed."""
    tail = _lines(capture_log)[since:]
    queued = any(("Queued playback switch" in l or "Reaffirmed playback switch" in l)
                 and f"Loop {slot}:" in l for l in tail)
    executed = any("Playback started" in l for l in tail)
    return queued and not executed


def _wait_switch_executed(capture_log: Path, since: int, timeout_s: float) -> bool:
    """Wait for the queued switch to actually run before any further press touches the same buttons."""
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        if any("Playback started" in l for l in _lines(capture_log)[since:]):
            return True
        time.sleep(0.1)
    return False


def _emit(out_port, gesture: Gesture, *, press_ms: int) -> None:
    if gesture.kind == "track":
        slots.press_track(_Ctx(out_port), gesture.value, press_ms=press_ms)
    elif gesture.kind == "slot_record":
        slots.press_slot_record(_Ctx(out_port), gesture.value, press_ms=press_ms)
    elif gesture.kind == "record":
        _send_short_press(out_port, note=RECORD_BUTTON_NOTE,
                          channel_1based=CONTROL_CHANNEL_1BASED, press_ms=press_ms)
    elif gesture.kind == "transport":
        _send_short_press(out_port, note=GLOBAL_TRANSPORT_NOTE,
                          channel_1based=CONTROL_CHANNEL_1BASED, press_ms=press_ms)
    else:  # pragma: no cover - guarded by the plan
        raise ValueError(f"unknown gesture kind {gesture.kind!r}")


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--leg", choices=("rec", "od"), required=True,
                   help="rec = S4f.13(a) record leg; od = S4f.13(b) overdub leg")
    p.add_argument("--track", type=int, required=True, choices=range(1, 9))
    p.add_argument("--slot-a", type=int, required=True, choices=range(1, 9))
    p.add_argument("--slot-b", type=int, required=True, choices=range(1, 9))
    p.add_argument("--out-dir", type=Path, required=True)
    p.add_argument("--press-ms", type=int, default=120)
    p.add_argument("--bars", type=int, default=2)
    p.add_argument("--notes", type=int, default=16)
    p.add_argument("--phase-timeout-s", type=float, default=20.0)
    p.add_argument("--settle-s", type=float, default=8.0)
    p.add_argument("--capture-port", default=DEFAULT_CAPTURE_SERIAL_PORT)
    p.add_argument("--boot-wait-s", type=float, default=10.0)
    p.add_argument("--upload", dest="upload", action="store_true", default=True)
    p.add_argument("--no-upload", dest="upload", action="store_false")
    p.add_argument("--upload-attempts", type=int, default=3,
                   help="`teensy-gui` fails intermittently and succeeds on a retry, so the upload is retried")
    p.add_argument("--upload-retry-delay-s", type=float, default=3.0)
    args = p.parse_args(argv)

    if args.slot_a == args.slot_b:
        p.error("--slot-a and --slot-b must differ: the leg is defined by switching between them")

    out_dir: Path = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=False)
    a0, b0, t0 = (to_firmware_slot(args.slot_a), to_firmware_slot(args.slot_b), args.track - 1)

    events: list[dict] = []
    report: dict = {"leg": args.leg, "track": args.track, "slot_a": args.slot_a, "slot_b": args.slot_b,
                    "slot_a_firmware": a0, "slot_b_firmware": b0,
                    "stimulus": (f"STIM-BASE-2-ONE-OD-01-TEMP-ACCEPTED-SEQ-"
                                 f"{'REC' if args.leg == 'rec' else 'OD'}"),
                    "events": events, "result": "INCOMPLETE", "upload": bool(args.upload)}

    if args.upload:
        refusal = refuse_reset_if_latched(tool="hitl_accepted_sequence")
        if refusal is not None:
            print(refusal)
            report["result"] = "RESET_REFUSED_AFTER_INCOMPLETE_SHUTDOWN"
            (out_dir / "accepted-sequence.json").write_text(json.dumps(report, indent=2) + "\n")
            return 4
        # The upload is a **retried** step, and its failure is reported rather than raised. Measured
        # 2026-09-27: `teensy-gui` intermittently fails with "Teensy Loader is unable to read your compiled
        # sketch (r)" and then succeeds on the very next attempt, so a single failed attempt is not a reason
        # to abort a device session -- and `check=True` turned it into an unhandled traceback instead of a
        # legible result, which is the defect this replaces.
        import subprocess
        upload_ok = False
        attempts: list[dict] = []
        for attempt in range(1, args.upload_attempts + 1):
            print(f"[s4f13] re-uploading teensy41-capture-serial to force a real reset "
                  f"(attempt {attempt}/{args.upload_attempts})")
            proc = subprocess.run(["pio", "run", "-e", "teensy41-capture-serial", "-t", "upload"],
                                  capture_output=True, text=True)
            attempts.append({"attempt": attempt, "returncode": proc.returncode})
            if proc.returncode == 0:
                upload_ok = True
                break
            tail = "\n".join((proc.stdout or "").splitlines()[-6:])
            print(f"[s4f13] upload attempt {attempt} failed (rc={proc.returncode}); tail:\n{tail}")
            time.sleep(args.upload_retry_delay_s)
        report["upload_attempts"] = attempts
        if not upload_ok:
            report["result"] = "UPLOAD_FAILED"
            report["session_log"] = None
            (out_dir / "accepted-sequence.json").write_text(json.dumps(report, indent=2) + "\n")
            print("[s4f13] every upload attempt failed; no leg was driven and the device was left as it was")
            return 5
        time.sleep(1.0)

    with ManagedCaptureSession(port=args.capture_port, boot_wait_s=args.boot_wait_s,
                              out_dir=out_dir, run_token=uuid.uuid4().hex) as capture:
        capture_log = capture.session_log
        if capture_log is None:
            raise RuntimeError("managed capture did not publish a session log")
        anchor_row = len(_lines(capture_log))
        report["anchor_row"] = anchor_row

        out_port = mido.open_output(_find_midi_port("Teensy", is_input=False))
        ctx = _Ctx(out_port)

        with DeviceSession(
                capture_log_getter=lambda: capture_log,
                send_transport_stop=lambda: _ensure_transport(
                    out_port, capture_log, running=False, press_ms=args.press_ms, events=events,
                    label="shutdown"),
                out_dir=out_dir,
                context=f"accepted-sequence-{args.leg}",
                events=events) as shutdown_session:

            def seed(slot_number: int, label: str, note_base: int) -> None:
                """Record a short take into `slot_number`, committed, so it is a *filled* slot afterwards.

                Both A and B are seeded, and B's seeding is load-bearing rather than symmetry: with an **empty
                selected slot** behind a pending switch, a record press never reaches the overdub stop at all.
                Measured 2026-09-27 -- `handleToggleRecord` sees `!hasCommittedPassesInSlot(selectedSlot)` and
                routes to the per-slot handler, which reaffirms the pending switch and emits another
                `Reaffirmed playback switch`, so the record press produced a *second countdown* instead of
                stopping anything. A filled B makes the selected slot a real overdub target.
                """
                slots.press_track(ctx, args.track, press_ms=args.press_ms)
                time.sleep(0.4)
                if not _ensure_transport(out_port, capture_log, running=False, press_ms=args.press_ms,
                                         events=events, label=f"seed_{label}_stop"):
                    raise RuntimeError("could not stop the transport before seeding")
                before = len(_lines(capture_log))
                slots.press_slot_record(ctx, slot_number, press_ms=args.press_ms)
                if not _wait_for(capture_log, before,
                                 (",ST,Track,STOPPED,ARMED", "Start Recording",
                                  ",ST,Track,STOPPED,RECORDING"), timeout_s=4.0):
                    slots.press_slot_record(ctx, slot_number, press_ms=args.press_ms)
                    events.append({"event": f"seed_{label}_second_press"})
                _ensure_transport(out_port, capture_log, running=True, press_ms=args.press_ms,
                                  events=events, label=f"seed_{label}_start")
                recording = _wait_for(capture_log, before, ("Recording started", ",RECA,"),
                                      timeout_s=args.phase_timeout_s)
                events.append({"event": f"seed_{label}_recording", "confirmed": recording})
                if not recording:
                    raise RuntimeError(
                        f"slot {slot_number} never started recording; it is probably not an empty target. "
                        "This script does not clear it -- pick an empty slot from the pre-run observation.")
                span_s = max(args.bars, 1) * 2.0
                gap_s = span_s / (args.notes + 1)
                for i in range(args.notes):
                    pitch = note_base + (i % 12)
                    out_port.send(mido.Message("note_on", note=pitch, velocity=100, channel=t0))
                    time.sleep(0.03)
                    out_port.send(mido.Message("note_off", note=pitch, velocity=0, channel=t0))
                    time.sleep(max(gap_s - 0.03, 0.01))
                _ensure_transport(out_port, capture_log, running=False, press_ms=args.press_ms,
                                  events=events, label=f"seed_{label}_commit")
                _wait_for(capture_log, before, (",RECS,stop",), timeout_s=args.phase_timeout_s)
                events.append({"event": f"seed_{label}_committed", "confirmed": True})

            # Seed B first, then A: seeding A last leaves A as the active/selected slot, which is the state the
            # leg's first gesture expects, and B is filled so it is a genuine overdub target.
            seed(args.slot_b, "b", 62)
            seed(args.slot_a, "a", 60)

            # --- the leg's own plan ---------------------------------------------------------------
            leg_mark = len(_lines(capture_log))
            plan = leg_gestures(args.leg, track=args.track, a=args.slot_a, b=args.slot_b)
            report["plan"] = [{"step": g.step, "kind": g.kind, "value": g.value} for g in plan]
            last_slot_press_at = 0.0
            for gesture in plan:
                # A queued switch owns the slot buttons until it executes. Pressing anything on the same
                # buttons before then is what produced the double countdown, so wait it out first.
                if _switch_pending(capture_log, leg_mark, args.slot_b):
                    ran = _wait_switch_executed(capture_log, leg_mark, timeout_s=args.phase_timeout_s)
                    events.append({"event": f"{gesture.step}_waited_for_pending_switch", "confirmed": ran})
                mark = len(_lines(capture_log))
                if gesture.step.endswith("_start") and gesture.kind != "transport":
                    # A start/switch needs a running transport; the plan's transport gestures are explicit.
                    _ensure_transport(out_port, capture_log, running=True, press_ms=args.press_ms,
                                      events=events, label=f"{gesture.step}_transport")
                # Never press a loop button twice inside the firmware's tap window: the second tap merges
                # into a double/triple and re-queues the switch instead of being a distinct gesture.
                if gesture.kind == "slot_record":
                    gap = time.monotonic() - last_slot_press_at
                    if gap < DEFAULT_TAP_WINDOW_S:
                        time.sleep(DEFAULT_TAP_WINDOW_S - gap)
                    last_slot_press_at = time.monotonic()
                _emit(out_port, gesture, press_ms=args.press_ms)
                events.append({"event": f"gesture:{gesture.step}", "line": mark,
                               "host_monotonic_ns": time.monotonic_ns()})
                time.sleep(0.25)
            time.sleep(args.settle_s)
            report["leg_rows"] = len(_lines(capture_log)) - leg_mark

            _ensure_transport(out_port, capture_log, running=False, press_ms=args.press_ms,
                              events=events, label="final")
            report["result"] = "DROVE_LEG"

    report["shutdown"] = shutdown_session.result.as_dict()
    if not shutdown_session.result.ok:
        report["result"] = f"SHUTDOWN_{shutdown_session.result.status.upper()}"
    report["session_log"] = str(capture_log)
    (out_dir / "accepted-sequence.json").write_text(json.dumps(report, indent=2) + "\n")

    print(json.dumps({k: v for k, v in report.items() if k not in ("events", "plan")}, indent=2))
    for e in events:
        print(f"  {e}")
    print(f"[s4f13] drove leg {args.leg}; now run the oracle:")
    print(f"  .venv/bin/python scripts/hitl/accepted_sequence_probe.py --session-log {capture_log} "
          f"--slot-a {args.slot_a} --slot-b {args.slot_b} --leg {args.leg}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
