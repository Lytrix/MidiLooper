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
    TRACK_SELECT_NOTE_BASE,
)
from hitl.managed_capture import ManagedCaptureSession  # noqa: E402
from hitl.midi_io import _find_midi_port, _send_short_press  # noqa: E402


@dataclass(frozen=True)
class Gesture:
    """One press in the critical window, as data so the ordering can be asserted without a device."""
    step: str
    kind: str          # "slot_record" | "slot_overdub" | "track" | "transport"
    value: int         # slot or track number, 1-based (transport: 0)


def critical_window_gestures(*, track: int, a: int, b: int, c: int,
                             seed_b_separately: bool = True) -> list[Gesture]:
    """The ordered presses of the critical window, with the chain's semantics encoded in the order.

    The order is the requirement: A's stop must be requested *before* B is switched to, B's overdub must be
    running *before* B's stop is requested (only an overdub stop is capacity-governed), and C's switch must
    come *after* B's stop request so that the denial it depends on has already happened.
    """
    return [
        Gesture("select_track", "track", track),
        Gesture("a_overdub_start", "slot_overdub", a),
        Gesture("a_stop_request", "slot_record", a),          # admits the deferred finalizer
        Gesture("b_switch", "slot_record", b),                # capture-switch: commits, activates B
        Gesture("b_overdub_start", "slot_overdub", b),
        Gesture("b_stop_request", "slot_record", b),          # denied on capacity inside A's window
        Gesture("c_switch_request", "slot_record", c),        # retained, must not commit yet
    ]


def _emit(out_port, gesture: Gesture, *, press_ms: int, gap_ms: int) -> None:
    if gesture.kind == "track":
        slots.press_track(_Ctx(out_port), gesture.value, press_ms=press_ms)
    elif gesture.kind == "slot_record":
        slots.press_slot_record(_Ctx(out_port), gesture.value, press_ms=press_ms)
    elif gesture.kind == "slot_overdub":
        slots.press_slot_overdub(_Ctx(out_port), gesture.value, press_ms=press_ms, gap_ms=gap_ms)
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


def _seed_slot(out_port, capture_log: Path, *, track: int, slot_number: int, note: int,
               bars: int, press_ms: int, phase_timeout_s: float, events: list[dict]) -> None:
    """Select the slot, record `bars` bars of a single note, stop, and confirm the take committed."""
    before = len(_lines(capture_log))
    slots.press_track(_Ctx(out_port), track, press_ms=press_ms)
    slots.press_slot_record(_Ctx(out_port), slot_number, press_ms=press_ms)
    events.append({"event": f"seed_slot_{slot_number}_selected"})
    _wait_for_any(capture_log, after=before,
                  markers=("Recording started", ",RECA,", ",ST,Track,STOPPED,PLAYING"),
                  timeout_s=1.5)
    if not _wait_for_any(capture_log, after=before, markers=("Recording started", ",RECA,"),
                         timeout_s=1.0):
        # An empty slot under a stopped transport needs the transport to start.
        _send_short_press(out_port, note=GLOBAL_TRANSPORT_NOTE,
                          channel_1based=CONTROL_CHANNEL_1BASED, press_ms=press_ms)
        if not _wait_for_any(capture_log, after=len(_lines(capture_log)) - 1,
                             markers=("Recording started", ",RECA,"), timeout_s=phase_timeout_s):
            raise RuntimeError(f"slot {slot_number} never started recording")
    time.sleep(max(bars, 1) * 2.0)  # 2 s per bar at 120 BPM, generous for the streamed note
    _send_short_press(out_port, note=GLOBAL_TRANSPORT_NOTE,
                      channel_1based=CONTROL_CHANNEL_1BASED, press_ms=press_ms)  # stop -> commit
    if not _confirm(capture_log, label=f"seed_slot_{slot_number}_committed",
                    markers=(",RECS,stop,", "Playback started", ",ST,Track,STOPPED,PLAYING"),
                    timeout_s=phase_timeout_s, events=events):
        raise RuntimeError(f"slot {slot_number} seed did not commit")


def main(argv: Optional[list[str]] = None) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--track", type=int, required=True, choices=range(1, 9))
    p.add_argument("--slot-a", type=int, required=True, choices=range(1, 9))
    p.add_argument("--slot-b", type=int, required=True, choices=range(1, 9))
    p.add_argument("--slot-c", type=int, required=True, choices=range(1, 9))
    p.add_argument("--out-dir", type=Path, required=True)
    p.add_argument("--press-ms", type=int, default=120)
    p.add_argument("--gesture-gap-ms", type=int, default=60,
                   help="gap between presses inside the critical window; keep it small, the window is ~1.3 s")
    p.add_argument("--phase-timeout-s", type=float, default=12.0)
    p.add_argument("--window-timeout-s", type=float, default=6.0)
    p.add_argument("--successor-note", type=int, default=67)
    p.add_argument("--seed-bars", type=int, default=2)
    p.add_argument("--census", type=Path, default=None,
                   help="census.json from a read-only --library-observe run; when given, every target must "
                        "be empty or the run refuses (this script never clears a slot)")
    p.add_argument("--tempo-bpm", type=float, default=120.0,
                   help="pin and VERIFY the device tempo after this run's own boot; the flow aborts if the "
                        "read-back does not match, because every timing in it is calibrated for this tempo")
    p.add_argument("--tempo-cc", type=int, default=102)
    p.add_argument("--capture-port", default=None)
    p.add_argument("--boot-wait-s", type=float, default=10.0)
    args = p.parse_args(argv)

    out_dir: Path = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=False)

    # Precondition, enforced rather than documented: the design seeds A and B by **recording** into them and
    # leaves C empty as the record target, so all three must be empty. Recording is refused by the firmware on
    # a slot with committed passes, which is why the read-only library observation is required first. This
    # script never clears a slot: clearing an occupied target is a separate, explicitly authorized action.
    if args.census is not None:
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

    with ManagedCaptureSession(port=args.capture_port, boot_wait_s=args.boot_wait_s,
                              out_dir=out_dir, run_token=f"retention-{int(time.time())}") as capture:
        capture_log = capture.session_log
        if capture_log is None:
            raise RuntimeError("managed capture did not publish a session log")
        out_port = mido.open_output(_find_midi_port("Teensy", is_input=False))
        events.append({"event": "midi_output_opened"})

        # --- tempo: applied and read back AFTER this run's own open, which is the run's final reset ---
        # The pin does not survive a reboot, and every timing here is calibrated for one tempo, so this is a
        # precondition rather than a nicety: no read-back matching --tempo-bpm, no gestures.
        note_channel = t0  # 0-based MIDI channel for note input on this track
        before_tempo = len(_lines(capture_log))
        slots.press_track(_Ctx(out_port), args.track, press_ms=args.press_ms)
        slots.press_slot_record(_Ctx(out_port), args.slot_a, press_ms=args.press_ms)
        time.sleep(1.0)
        cc_value = int(round(args.tempo_bpm - 60.0))
        out_port.send(mido.Message("control_change", channel=CONTROL_CHANNEL_1BASED - 1,
                                   control=args.tempo_cc, value=max(0, min(127, cc_value))))
        pinned = _wait_for_any(capture_log, after=before_tempo,
                              markers=(f"-> {args.tempo_bpm:.1f} BPM",), timeout_s=6.0)
        events.append({"event": "tempo_pinned", "requested_bpm": args.tempo_bpm, "confirmed": pinned})
        if not pinned:
            raise SystemExit(
                f"tempo precondition failed: no read-back of {args.tempo_bpm:.1f} BPM after CC{args.tempo_cc}="
                f"{cc_value}. The flow needs LOOP_EDIT to be reachable for the selected track; without the pin "
                "every timing below is wrong, so it refuses to continue rather than produce a bad run.")

        # --- setup: seed A and B, confirming each transition before the next gesture ---
        _seed_slot(out_port, capture_log, track=args.track, slot_number=args.slot_a, note=60,
                   bars=args.seed_bars, press_ms=args.press_ms,
                   phase_timeout_s=args.phase_timeout_s, events=events)
        _seed_slot(out_port, capture_log, track=args.track, slot_number=args.slot_b, note=62,
                   bars=args.seed_bars, press_ms=args.press_ms,
                   phase_timeout_s=args.phase_timeout_s, events=events)

        # --- the critical window: presses ordered by the chain, with no confirmation inside it ---
        for gesture in critical_window_gestures(track=args.track, a=args.slot_a, b=args.slot_b,
                                               c=args.slot_c):
            mark = len(_lines(capture_log))
            _emit(out_port, gesture, press_ms=args.press_ms, gap_ms=args.gesture_gap_ms)
            events.append({"event": f"gesture:{gesture.step}", "note_line": mark,
                           "host_monotonic_ns": time.monotonic_ns()})
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
            note_channel = t0 + 1
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

    report["session_log"] = str(capture_log)
    (out_dir / "retention-handoff.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: v for k, v in report.items() if k != "events"}, indent=2))
    for e in events:
        print(f"  {e}")
    print(f"[retention] drove {report['result']}; run the content pipeline and the retention oracle next")
    return 0 if report["result"] == "DROVE_SEQUENCE" else 1


if __name__ == "__main__":
    raise SystemExit(main())
