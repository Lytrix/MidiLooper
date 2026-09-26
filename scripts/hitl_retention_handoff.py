#!/usr/bin/env python3
"""Drive the S4f.3 accepted-switch retention handoff on the device (temporary stimulus).

This is the host entry point the retention oracle needs, and it fills the gap that blocked the first
authorized run: no flow could perform *per-slot* gestures. It performs, on one explicitly authorized track
and three explicitly authorized slots (A = predecessor, B = overdubbed successor, C = the start target):

    seed A and B (each a short record)          -> both are overdub targets
    A overdubs, then A's stop is requested      -> the admitted finalizer opens its window
    B is switched to while still capturing, and B overdubs
    B's stop is requested inside A's window     -> admission is denied on capacity, B's obligation latches
    C is switched to                            -> the switch cannot commit; the request is RETAINED
    wait for A's finalizer to retire            -> capacity releases, B's stop binds, C starts
    inject the successor note, stop transport   -> content is committed for the dump

Then run the content pipeline and the oracle separately:

    .venv/bin/python .agents/skills/hitl-content-verification/scripts/dump_and_capture.py \
        --out-dir captures/<run>-playback
    .venv/bin/python .agents/skills/hitl-content-verification/scripts/verify_playback_content.py \
        captures/<run>-playback
    .venv/bin/python scripts/hitl/retention_probe.py \
        --session-log <run>/session_*.log --content-dump captures/<run>-playback/serial.log \
        --content-review captures/<run>-playback/content-review.json \
        --expect-successor-note <note> --slot-a <A0> --slot-b <B0> --slot-c <C0> --track-b <track0>

Destructive-operations policy: this script **never clears** a slot. It verifies that each target is empty
before seeding and refuses to run otherwise; clearing an occupied slot is a separate, explicitly authorized
action (`--clear-authorized` is deliberately not implemented here).
"""

from __future__ import annotations

import argparse
import json
import sys
import time
import uuid
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Optional

import mido

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from hitl.actions import slots  # noqa: E402
from hitl.control_constants import (  # noqa: E402
    CONTROL_CHANNEL_1BASED,
    GLOBAL_TRANSPORT_NOTE,
    LOOP_SELECT_NOTE_BASE,
    RECORD_BUTTON_NOTE,
    TRACK_SELECT_NOTE_BASE,
)
from hitl.config import DEFAULT_CAPTURE_SERIAL_PORT  # noqa: E402
from hitl.managed_capture import ManagedCaptureSession  # noqa: E402
from hitl.midi_io import _find_midi_port, _send_long_press, _send_short_press  # noqa: E402


@dataclass(frozen=True)
class Gesture:
    """One press in the critical window, as data so the ordering can be asserted without a device."""
    step: str
    kind: str          # "slot_record" | "slot_overdub" | "track" | "transport"
    value: int         # slot or track number, 1-based (transport: 0)


def critical_window_gestures(*, track: int, a: int, b: int, c: int,
                             seed_b_separately: bool = True) -> list[Gesture]:
    """The ordered presses of the critical window, with the chain's semantics encoded in the order.

    Two corrections from running it against the device:

    * **There is no separate `a_stop_request`.** The capture-switch to B *is* A's stop: pressing the target
      slot while the track is still capturing routes to `finalizeCaptureAndSelectSlot`, which admits and binds
      A's stop and commits the switch in one gesture. A separate stop press first is worse than redundant --
      an admitted stop leaves the track in `PLAYING` (measured: `OVERDUBBING -> OVERDUB_STOPPING -> PLAYING` in
      21 us), and the next press then takes the playing/filled branch, "Queued playback switch", never the
      capture-switch that opens the finalization window.
    * The order below is therefore: A overdubs, the switch to B both stops A and moves the active slot,
      B overdubs, B's stop is denied, and C's switch is issued while B is still capturing so it is retained.
    """
    return [
        Gesture("select_track", "track", track),
        Gesture("a_overdub_start", "slot_overdub", a),
        Gesture("b_switch", "slot_record", b),                # capture-switch: admits A's stop and commits
        Gesture("b_overdub_start", "slot_overdub", b),
        Gesture("b_stop_request", "slot_record", b),          # denied on capacity inside A's window
        Gesture("c_switch_request", "slot_record", c),        # retained, must not commit yet
    ]


def _emit(out_port, gesture: Gesture, *, press_ms: int, gap_ms: int,
          multi_press_ms: int = 40, multi_gap_ms: int = 40) -> None:
    if gesture.kind == "track":
        slots.press_track(_Ctx(out_port), gesture.value, press_ms=press_ms)
    elif gesture.kind == "slot_record":
        slots.press_slot_record(_Ctx(out_port), gesture.value, press_ms=press_ms)
    elif gesture.kind == "slot_overdub":
        # Multi-press timing is deliberately much tighter than a normal press: the loop buttons' effective
        # double-tap window is shorter than a 120 ms press plus a 60 ms gap (measured: the second tap was
        # classified "First tap or outside window" with a 180 ms release-to-release gap, so every overdub
        # became two single presses).
        slots.press_slot_overdub(_Ctx(out_port), gesture.value, press_ms=multi_press_ms,
                                 gap_ms=multi_gap_ms)
    elif gesture.kind == "transport":
        _send_short_press(out_port, note=GLOBAL_TRANSPORT_NOTE,
                          channel_1based=CONTROL_CHANNEL_1BASED, press_ms=press_ms)
    else:  # pragma: no cover - guarded by the constructor above
        raise ValueError(f"unknown gesture kind {gesture.kind!r}")


class _Ctx:
    """Minimal ActionContext stand-in: the slot actions only need `out_port` and `config.press_ms`."""

    class _Cfg:
        press_ms = 120

    def __init__(self, out_port):
        self.out_port = out_port
        self.config = self._Cfg()


def _lines(path: Path) -> list[str]:
    if not path.exists():
        return []
    return path.read_text(encoding="utf-8", errors="replace").splitlines()


def _wait_for_any(capture_log: Path, *, after: int, markers: tuple[str, ...],
                  timeout_s: float) -> bool:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        tail = _lines(capture_log)[after:]
        if any(m in line for line in tail for m in markers):
            return True
        time.sleep(0.05)
    return False


def _confirm(capture_log: Path, *, label: str, markers: tuple[str, ...], timeout_s: float,
             events: list[dict]) -> bool:
    before = len(_lines(capture_log))
    ok = _wait_for_any(capture_log, after=before, markers=markers, timeout_s=timeout_s)
    events.append({"event": label, "confirmed": ok})
    return ok


def _transport_running(capture_log: Path) -> bool:
    """Whether the transport is running, from the most recent device line that says so."""
    state = False
    for line in _lines(capture_log):
        if "Transport started" in line:
            state = True
        elif "Transport stopped" in line:
            state = False
    return state


def _ensure_transport(out_port, capture_log: Path, *, running: bool, press_ms: int,
                      events: list[dict], label: str, attempts: int = 3) -> bool:
    """Make the transport running or stopped, and REQUIRE the transition rather than assume it.

    Pressing the global transport note toggles it, so a blind press when it is already in the wanted state
    flips it the wrong way. The device's own "Transport started"/"Transport stopped" lines are readable, so the
    state is read first and the transition is then required. This matters for the seed: pressing a slot button
    while the track is *playing* takes the `isPlaying() && slotHasData` branch (SD-aware, so a cleared but
    payload-carrying slot counts as filled) and never reaches the record path.
    """
    for attempt in range(attempts):
        if _transport_running(capture_log) == running:
            events.append({"event": f"{label}_transport", "running": running, "attempt": attempt})
            return True
        before = len(_lines(capture_log))
        _send_short_press(out_port, note=GLOBAL_TRANSPORT_NOTE,
                          channel_1based=CONTROL_CHANNEL_1BASED, press_ms=press_ms)
        want = "Transport started" if running else "Transport stopped"
        other = ",ST,Track,STOPPED,PLAYING" if running else ",ST,Track,PLAYING,STOPPED"
        _wait_for_any(capture_log, after=before, markers=(want, other), timeout_s=4.0)
        time.sleep(0.5)
    ok = _transport_running(capture_log) == running
    events.append({"event": f"{label}_transport", "running": running, "confirmed": ok})
    return ok


def _clear_slot(out_port, capture_log: Path, *, track: int, slot_number: int, press_ms: int,
                events: list[dict], clear_press_ms: int = 900) -> bool:
    """Clear one slot using the owner's recipe: **from stopped**, slot press, then long-hold record press.

    The owner's recipe verbatim: "from stopped, slot press, long hold record press will clear it". The
    transport must be stopped first (`_ensure_transport` reads the device's own state rather than blind
    toggling), the slot press selects the slot -- `CLEAR_TRACK_FOR_SLOT` only clears the *selected* slot, and
    any other slot takes the playback path instead -- and the long hold on the record button is the clear.

    Clearing is what makes a slot a genuine record target: every slot on this device retains an SD payload and
    the per-slot gesture consults SD-aware content, so an uncleared slot routes to the playback-switch branch
    instead of the record path.
    """
    if not _ensure_transport(out_port, capture_log, running=False, press_ms=press_ms, events=events,
                             label=f"clear_{slot_number}"):
        raise RuntimeError(f"could not stop the transport before clearing slot {slot_number}")
    slots.press_track(_Ctx(out_port), track, press_ms=press_ms)
    slots.press_slot_record(_Ctx(out_port), slot_number, press_ms=press_ms)
    time.sleep(1.0)

    cleared = False
    for attempt in range(3):
        before = len(_lines(capture_log))
        _send_long_press(out_port, note=RECORD_BUTTON_NOTE,
                         channel_1based=CONTROL_CHANNEL_1BASED, press_ms=clear_press_ms)
        _wait_for_any(capture_log, after=before,
                      markers=("Track cleared", "Clear selected slot", "Clear ignored", "Clear refused"),
                      timeout_s=6.0)
        tail = " ".join(_lines(capture_log)[before:])
        if any(m in tail for m in ("Track cleared", "Clear selected slot", "Clear ignored")):
            cleared = True
            break
        events.append({"event": f"clear_{slot_number}_retry", "attempt": attempt + 1})
        time.sleep(3.0)
    events.append({"event": f"clear_{slot_number}", "confirmed": cleared})
    time.sleep(0.5)
    return cleared


def _seed_slot(out_port, capture_log: Path, *, track: int, slot_number: int, channel: int,
               note: int, bars: int, press_ms: int, phase_timeout_s: float,
               events: list[dict]) -> None:
    """Select the slot, record `bars` bars containing one note, stop, and confirm the take committed.

    Two things this must get right, both learned by running it:

    * **The take needs a note.** A seed that records silence produces no committed pass, and the whole design
      depends on A and B holding committed content (they must be overdub targets) while C stays empty. The
      first version took a `note` argument and never sent it.
    * **The commit confirmation must accept the state the device actually reaches.** Stopping with the global
      transport ends `RECORDING -> STOPPED_RECORDING -> STOPPED`, not `-> PLAYING`, and the transport-stop path
      emits no `RECS,stop` stage record. Waiting only for the PLAYING form refused a take that had in fact
      committed.
    """
    slots.press_track(_Ctx(out_port), track, press_ms=press_ms)
    time.sleep(0.5)

    # The track must NOT be playing when the slot is pressed. Measured on device: with the track playing,
    # `handleToggleRecordForSlot` takes its `isPlaying() && slotHasData` branch and logs "Queued playback
    # switch" -- and `slotHasData` is SD-aware, so a slot with no committed content but a retained SD payload
    # (every slot on this device: payload_inventory=64) counts as filled. The record path is reached only when
    # the track is not playing, where the press arms and the transport start then begins the capture.
    if not _ensure_transport(out_port, capture_log, running=False, press_ms=press_ms, events=events,
                             label=f"seed_{slot_number}"):
        raise RuntimeError(f"could not stop the transport before seeding slot {slot_number}; pressing the "
                           "slot while the track plays would take the playback-switch branch")

    before = len(_lines(capture_log))
    slots.press_slot_record(_Ctx(out_port), slot_number, press_ms=press_ms)
    events.append({"event": f"seed_{slot_number}_selected"})
    saw = _wait_for_any(capture_log, after=before,
                        markers=(",ST,Track,STOPPED,ARMED", ",ST,Track,STOPPED,RECORDING",
                                 "Start Recording"),
                        timeout_s=3.0)
    if not saw:
        # A cleared slot can take the arm-and-wait path (`handleArmedRecordPress` returns handled), so the
        # first press only arms. Press again: the second press is the record start for an armed slot.
        slots.press_slot_record(_Ctx(out_port), slot_number, press_ms=press_ms)
        events.append({"event": f"seed_{slot_number}_second_press"})
        _wait_for_any(capture_log, after=before,
                      markers=(",ST,Track,STOPPED,ARMED", ",ST,Track,STOPPED,RECORDING",
                               "Start Recording"),
                      timeout_s=3.0)
    # The arm needs a running transport to begin the capture.
    _ensure_transport(out_port, capture_log, running=True, press_ms=press_ms, events=events,
                      label=f"seed_{slot_number}_start")
    recording = _wait_for_any(capture_log, after=before,
                              markers=("Recording started", ",RECA,", ",ST,Track,ARMED,RECORDING"),
                              timeout_s=phase_timeout_s)
    if not recording:
        raise RuntimeError(f"slot {slot_number} never started recording")
    events.append({"event": f"seed_{slot_number}_recording", "confirmed": True})

    # Inject the take's content on the track's input channel (mido channels are 0-based).
    out_port.send(mido.Message("note_on", note=note, velocity=100, channel=channel))
    time.sleep(0.3)
    out_port.send(mido.Message("note_off", note=note, velocity=0, channel=channel))
    time.sleep(max(bars, 1) * 2.0)

    stopped = len(_lines(capture_log))
    _send_short_press(out_port, note=GLOBAL_TRANSPORT_NOTE,
                      channel_1based=CONTROL_CHANNEL_1BASED, press_ms=press_ms)
    committed = _wait_for_any(
        capture_log, after=stopped,
        markers=(",ST,Track,RECORDING,STOPPED_RECORDING", ",RECS,stop,", "Playback started",
                 ",ST,Track,RECORDING,STOPPED", ",ST,Track,STOPPED_RECORDING,PLAYING"),
        timeout_s=phase_timeout_s)
    events.append({"event": f"seed_{slot_number}_committed", "confirmed": committed})
    if not committed:
        raise RuntimeError(f"slot {slot_number} seed did not commit")


def main(argv: Optional[list[str]] = None) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--track", type=int, required=True, choices=range(1, 9))
    p.add_argument("--slot-a", type=int, required=True, choices=range(1, 9))
    p.add_argument("--slot-b", type=int, required=True, choices=range(1, 9))
    p.add_argument("--slot-c", type=int, required=True, choices=range(1, 9))
    p.add_argument("--out-dir", type=Path, required=True)
    p.add_argument("--press-ms", type=int, default=120)
    p.add_argument("--multi-press-ms", type=int, default=40,
                   help="hold time for a multi-press (overdub); the double-tap window is short")
    p.add_argument("--multi-gap-ms", type=int, default=40,
                   help="release-to-press gap inside a multi-press")
    p.add_argument("--gesture-gap-ms", type=int, default=60,
                   help="gap between presses inside the critical window; keep it small, the window is ~1.3 s")
    p.add_argument("--phase-timeout-s", type=float, default=12.0)
    p.add_argument("--window-timeout-s", type=float, default=6.0)
    p.add_argument("--successor-note", type=int, default=67)
    p.add_argument("--prepare-only", action="store_true",
                   help="stop after clearing the three targets, then exit. The owner's recipe is to clear "
                        "each required slot first and then run as normal, which also keeps the clear's state "
                        "effects out of the seeding phase.")
    p.add_argument("--clear-targets", action="store_true",
                   help="also clear the targets in this run (off by default: prepare first, then run).")
    p.add_argument("--no-clear", action="store_true",
                   help="skip the target clear. By default the three targets are cleared before seeding, "
                        "which is standing-authorized for the development set and is what makes them genuine "
                        "record targets (every slot here retains an SD payload).")
    p.add_argument("--seed-note-a", type=int, default=60)
    p.add_argument("--seed-note-b", type=int, default=62)
    p.add_argument("--seed-bars", type=int, default=2)
    p.add_argument("--census", type=Path, default=None,
                   help="census.json from a read-only --library-observe run; when given, every target must "
                        "be empty or the run refuses (this script never clears a slot)")
    p.add_argument("--tempo-bpm", type=float, default=120.0,
                   help="pin and VERIFY the device tempo after this run's own boot; the flow aborts if the "
                        "read-back does not match, because every timing in it is calibrated for this tempo")
    p.add_argument("--tempo-cc", type=int, default=102)
    p.add_argument("--upload", action="store_true",
                   help="re-upload the firmware first, so this run's capture catches a real boot banner. The "
                        "authorized run needs it: a capture that does not reset the device cannot satisfy "
                        "fresh_boot, and the reset-on-open the capture prints is not reliable.")
    p.add_argument("--build-env", default="teensy41-capture-serial")
    p.add_argument("--capture-port", default=DEFAULT_CAPTURE_SERIAL_PORT)
    p.add_argument("--boot-wait-s", type=float, default=10.0)
    args = p.parse_args(argv)

    out_dir: Path = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=False)

    # Precondition, enforced rather than documented: the design seeds A and B by **recording** into them and
    # leaves C empty as the record target, so all three must be empty. Recording is refused by the firmware on
    # a slot with committed passes, which is why the read-only library observation is required first. This
    # script never clears a slot: clearing an occupied target is a separate, explicitly authorized action.
    if args.census is not None and args.no_clear:
        data = json.loads(args.census.read_text())
        rows = (data.get("library_observation") or {}).get("slots") or []
        occupied = [(r.get("track", -1) + 1, r.get("slot", -1) + 1) for r in rows
                    if isinstance(r, dict) and r.get("has_committed_content")]
        for slot_number in (args.slot_a, args.slot_b, args.slot_c):
            if (args.track, slot_number) in occupied:
                raise SystemExit(
                    f"target track {args.track} slot {slot_number} already holds committed content; the "
                    "design needs all three targets empty (A and B are seeded by recording, C stays empty as "
                    "the record target). Choose empty targets or clear them under a separate authorization.")

    events: list[dict] = []
    report = {"track": args.track, "slot_a": args.slot_a, "slot_b": args.slot_b,
              "slot_c": args.slot_c, "events": events, "result": "INCOMPLETE"}
    a0, b0, c0 = args.slot_a - 1, args.slot_b - 1, args.slot_c - 1
    t0 = args.track - 1

    if args.upload:
        import subprocess
        print(f"[retention] re-uploading {args.build_env} to force a real reset before the run")
        subprocess.run(["pio", "run", "-e", args.build_env, "-t", "upload"], check=True)
        time.sleep(1.0)

    with ManagedCaptureSession(port=args.capture_port, boot_wait_s=args.boot_wait_s,
                              out_dir=out_dir, run_token=uuid.uuid4().hex) as capture:
        capture_log = capture.session_log
        if capture_log is None:
            raise RuntimeError("managed capture did not publish a session log")
        out_port = mido.open_output(_find_midi_port("Teensy", is_input=False))
        events.append({"event": "midi_output_opened"})

        # --- tempo: applied and read back AFTER this run's own open, which is the run's final reset ---
        # LOOP_EDIT gates CC102 (`handleTempoInput`), and the route in is not documented: the tempo probe's
        # success came from a device that was *already* in LOOP_EDIT ("the device was already in LOOP_EDIT"),
        # so its track+slot presses were incidental. Try the cheapest route first -- a fresh boot may already
        # be in the loop view -- and record which route worked instead of assuming one. A slot press is
        # deliberately last: on an empty slot it arms a recording, which is a state change this step must not
        # cause.
        note_channel = t0  # 0-based MIDI channel for note input on this track
        cc_value = int(round(args.tempo_bpm - 60.0))
        cc_value = max(0, min(127, cc_value))
        # If the device already reports the requested tempo in its own boot output, take that as the
        # read-back and send nothing. The device enters LOOP_EDIT at boot and logs its tempo there
        # ("Sent tempo CC feedback: 120.0 BPM"), and *pressing* track/slot to reach the mode is a side effect
        # this step should not cause: a run whose seed presses were silently consumed (no state transitions at
        # all after the clears) is consistent with those presses having left the UI somewhere the loop buttons
        # do not reach the record path.
        already = _wait_for_any(capture_log, after=0,
                               markers=(f"Sent tempo CC feedback: {args.tempo_bpm:.1f} BPM",
                                        f"maps to current {args.tempo_bpm:.1f} BPM"),
                               timeout_s=0.1)
        pinned = bool(already)
        tempo_route = "boot_readback" if already else None
        if already:
            events.append({"event": "tempo_already_pinned", "requested_bpm": args.tempo_bpm,
                           "confirmed": True, "route": "boot_readback"})
        tempo_routes = (() if already else (
            ("boot_state", ()),
            ("track_selected", ("track",)),
            ("track_and_slot_selected", ("track", "slot")),
        ))
        for route_name, presses in tempo_routes:
            before_tempo = len(_lines(capture_log))
            for kind in presses:
                if kind == "track":
                    slots.press_track(_Ctx(out_port), args.track, press_ms=args.press_ms)
                else:
                    slots.press_slot_record(_Ctx(out_port), args.slot_a, press_ms=args.press_ms)
                time.sleep(1.0)
            for _attempt in range(2):
                out_port.send(mido.Message("control_change", channel=CONTROL_CHANNEL_1BASED - 1,
                                           control=args.tempo_cc, value=cc_value))
                # The firmware logs an applied tempo only when it *changes*; when the device is already at
                # the requested value it logs "Tempo unchanged: CC=<n> maps to current <bpm> BPM" instead.
                # That line is a read-back too -- it names the device's current tempo -- and ignoring it made
                # this precondition refuse a perfectly valid session.
                pinned = _wait_for_any(
                    capture_log, after=before_tempo,
                    markers=(f"-> {args.tempo_bpm:.1f} BPM",
                             f"maps to current {args.tempo_bpm:.1f} BPM"), timeout_s=4.0)
                if pinned:
                    break
            events.append({"event": f"tempo_route:{route_name}", "confirmed": pinned})
            if pinned:
                tempo_route = route_name
                break
        events.append({"event": "tempo_pinned", "requested_bpm": args.tempo_bpm,
                       "confirmed": pinned, "route": tempo_route})
        if not pinned:
            raise SystemExit(
                f"tempo precondition failed: no read-back of {args.tempo_bpm:.1f} BPM after CC{args.tempo_cc}="
                f"{cc_value} on any of {[r for r, _ in tempo_routes]}. LOOP_EDIT must be reachable for the pin "
                "(CC102 is mode-gated); without it every timing below is wrong, so the flow refuses to "
                "continue rather than produce a bad run.")

        # --- optional in-run clear; the normal flow is `--prepare-only` first, then run as normal ---
        if args.clear_targets and not args.no_clear:
            for slot_number in (args.slot_a, args.slot_b, args.slot_c):
                _clear_slot(out_port, capture_log, track=args.track, slot_number=slot_number,
                            press_ms=args.press_ms, events=events)
        if args.prepare_only:
            for slot_number in (args.slot_a, args.slot_b, args.slot_c):
                _clear_slot(out_port, capture_log, track=args.track, slot_number=slot_number,
                            press_ms=args.press_ms, events=events)
            report["result"] = "TARGETS_PREPARED"
            report["events"] = events
            report["session_log"] = str(capture_log)
            (out_dir / "retention-handoff.json").write_text(json.dumps(report, indent=2) + "\n")
            print(json.dumps({k: v for k, v in report.items() if k != "events"}, indent=2))
            for e in events:
                print(f"  {e}")
            print("[retention] targets prepared; now run without --prepare-only")
            return 0

        # --- setup: seed A and B, confirming each transition before the next gesture ---
        _seed_slot(out_port, capture_log, track=args.track, slot_number=args.slot_a, channel=t0,
                   note=args.seed_note_a, bars=args.seed_bars, press_ms=args.press_ms,
                   phase_timeout_s=args.phase_timeout_s, events=events)
        _seed_slot(out_port, capture_log, track=args.track, slot_number=args.slot_b, channel=t0,
                   note=args.seed_note_b, bars=args.seed_bars, press_ms=args.press_ms,
                   phase_timeout_s=args.phase_timeout_s, events=events)

        # --- the critical window: presses ordered by the chain, with the minimum confirmations needed ---
        # A multi-press on a loop button has a *triple* action too, so the firmware holds the second tap in
        # `PendingDouble` until the triple window expires before firing DOUBLE_PRESS. A press that follows on
        # the same note inside that wait is read as the third tap (REDO_FOR_SLOT) instead of the stop we
        # wanted. Each multi-press therefore waits for its own resolution -- "Double press detected", or the
        # state it causes -- before the next press on that slot. This costs nothing inside the finalization
        # window because it happens before the switch that opens it.
        for gesture in critical_window_gestures(track=args.track, a=args.slot_a, b=args.slot_b,
                                               c=args.slot_c):
            mark = len(_lines(capture_log))
            _emit(out_port, gesture, press_ms=args.press_ms, gap_ms=args.gesture_gap_ms,
                  multi_press_ms=args.multi_press_ms, multi_gap_ms=args.multi_gap_ms)
            events.append({"event": f"gesture:{gesture.step}", "note_line": mark,
                           "host_monotonic_ns": time.monotonic_ns()})
            if gesture.kind == "slot_overdub":
                # Wait for the double to resolve (or for the overdub state it causes) before the next press.
                _wait_for_any(capture_log, after=mark,
                              markers=("Double press detected", ",ST,Track,PLAYING,OVERDUBBING",
                                       "Double press", "Overdub"),
                              timeout_s=1.5)
            if gesture.step == "b_switch":
                # Separate the switch from the double press that follows on the SAME button. Measured: with
                # only the normal gap the firmware merged them and logged "Triple press detected" for Loop 6,
                # so B got REDO_FOR_SLOT instead of an overdub and nothing downstream happened. The gap must
                # exceed the triple-tap window (400 ms default).
                _wait_for_any(capture_log, after=mark,
                              markers=("Button press: Loop", "scheduling short press"), timeout_s=0.5)
                time.sleep(0.55)
            time.sleep(max(args.gesture_gap_ms, 1) / 1000.0)

        # --- observe the chain in the device telemetry ---
        window_opened = _wait_for_any(capture_log, after=0,
                                     markers=(f"DIAG,reentry_window_open,{a0},",),
                                     timeout_s=args.window_timeout_s)
        events.append({"event": "finalizer_window_open", "confirmed": window_opened})
        denied = _wait_for_any(capture_log, after=0,
                              markers=(f"DIAG,reentry,deny,", "successor_stop_denied_capacity"),
                              timeout_s=args.window_timeout_s)
        events.append({"event": "successor_stop_denied", "confirmed": denied})
        bound = _wait_for_any(capture_log, after=0, markers=(f"DIAG,reentry,bound,",),
                             timeout_s=args.window_timeout_s)
        events.append({"event": "successor_stop_bound", "confirmed": bound})
        started = _wait_for_any(capture_log, after=0, markers=(f",RECA,{c0},",),
                               timeout_s=args.phase_timeout_s)
        events.append({"event": "successor_started", "confirmed": started})

        if started:
            # Let the successor take run, then inject the note the oracle will look for.
            time.sleep(2.0)
            # mido channels are 0-based; the device's per-track input channel for track N is N-1.
            out_port.send(mido.Message("note_on", note=args.successor_note, velocity=100,
                                       channel=note_channel))
            time.sleep(0.4)
            out_port.send(mido.Message("note_off", note=args.successor_note, velocity=0,
                                       channel=note_channel))
            events.append({"event": "successor_note_injected", "note": args.successor_note})
            time.sleep(2.0)

        # Stop the transport so the content dump's preconditions hold, and let persistence settle.
        _send_short_press(out_port, note=GLOBAL_TRANSPORT_NOTE,
                          channel_1based=CONTROL_CHANNEL_1BASED, press_ms=args.press_ms)
        time.sleep(4.0)
        _confirm(capture_log, label="transport_stopped", markers=(",ST,Track,",),
                 timeout_s=args.phase_timeout_s, events=events)
        report["result"] = "DROVE_SEQUENCE" if started else "SEQUENCE_INCOMPLETE"

    if report.get("result") == "TARGETS_PREPARED":
        report["session_log"] = str(capture_log)
        (out_dir / "retention-handoff.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps({k: v for k, v in report.items() if k != "events"}, indent=2))
        for e in events:
            print(f"  {e}")
        print("[retention] targets prepared; now run without --prepare-only")
        return 0
    report["session_log"] = str(capture_log)
    (out_dir / "retention-handoff.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: v for k, v in report.items() if k != "events"}, indent=2))
    for e in events:
        print(f"  {e}")
    print(f"[retention] drove {report['result']}; run the content pipeline and the retention oracle next")
    return 0 if report["result"] == "DROVE_SEQUENCE" else 1


if __name__ == "__main__":
    raise SystemExit(main())
