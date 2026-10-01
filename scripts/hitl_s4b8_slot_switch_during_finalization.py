#!/usr/bin/env python3
"""S4b.8 / S4b.9 device leg: switch the active slot (or track) while a predecessor still finalizes.

Reuses the existing pieces rather than adding a runner: the stop-interruption wrapper's shape (a port proxy
that arms on the overdub-stop button release and fires one intervention later), the baseline preset's own
record/overdub recipe, and the harness's established control-surface presses
(`hitl.midi_io._send_short_press` with `LOOP_SELECT_NOTE_BASE` / `TRACK_SELECT_NOTE_BASE`).

The one difference from `hitl_stop_interruption.py` is what the intervention *sends*: instead of a transport
Stop it presses the destination slot (S4b.8) and optionally the destination track first (S4b.9), i.e. exactly
the "switch while a predecessor finalizes" gesture those legs ask for. It never builds or uploads firmware.

The destination slot is expected to be **populated already** (a second, preceding run of the same preset seeds
it): an empty destination takes the record carrier instead of the playback-switch lane, which is a different
leg (`hitl_stop_interruption.py`'s territory), and this runner says so rather than pretending both are one case.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import sys
import threading
import time

import mido

root = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / "scripts"))

from hitl import legacy_record_baseline as baseline  # noqa: E402
from hitl.control_constants import CONTROL_CHANNEL_1BASED, LOOP_SELECT_NOTE_BASE, TRACK_SELECT_NOTE_BASE  # noqa: E402
from hitl.midi_io import _send_short_press  # noqa: E402
from hitl.runner import main  # noqa: E402
from hitl.stop_intervention import StopInterventionPort  # noqa: E402


class SlotSwitchInterventionPort(StopInterventionPort):
    """The stop-interruption proxy, with the intervention turned into a switch press.

    It arms at the same measured edge (the overdub-stop button release) and fires `delay_s` later, which is the
    window in which the predecessor's stop is admitted and its finalization is still in flight.
    """

    def __init__(self, output, emit, *, delay_s, channel, target_track_index, target_slots,
                 press_ms=120, phase_wait_ms=500, switch_interval_ms=0, preplay_track_index=None,
                 preplay_slot=None, preplay_source_track_index=None, **kw):
        super().__init__(output, emit, delay_s=delay_s, channel=channel, **kw)
        # The destination *track* must be playing for a press on one of its other filled slots to be a playback
        # switch at all: with that track silent, the press is a selection/toggle (measured, `captures/s4b9-cross-track-15/-16`).
        # So the gesture starts the destination track's own loop and hands the focus back to the source track,
        # before the pass that owns the finalization window runs.
        self.preplay_track_index = preplay_track_index
        self.preplay_slot = preplay_slot
        self.preplay_source_track_index = preplay_source_track_index
        self.preplay_done = False
        # Both are 0-based here; the wire notes are the base plus the index (the harness's own convention).
        self.target_track_index = target_track_index
        self.target_slots = list(target_slots)
        self.press_ms = press_ms
        self.phase_wait_ms = phase_wait_ms
        self.switch_interval_ms = switch_interval_ms

    def _preplay_destination_track(self):
        """Start the destination track's loop and return the focus to the source track."""
        try:
            _send_short_press(self.output,
                              note=TRACK_SELECT_NOTE_BASE + self.preplay_track_index,
                              channel_1based=CONTROL_CHANNEL_1BASED, press_ms=self.press_ms)
            self._event("preplay_track_select_sent", track_index=self.preplay_track_index)
            time.sleep(self.phase_wait_ms / 1000.0)
            _send_short_press(self.output,
                              note=LOOP_SELECT_NOTE_BASE + self.preplay_slot - 1,
                              channel_1based=CONTROL_CHANNEL_1BASED, press_ms=self.press_ms)
            self._event("preplay_slot_select_sent", target_slot=self.preplay_slot,
                        target_track_index=self.preplay_track_index)
            time.sleep(self.phase_wait_ms / 1000.0)
            if self.preplay_source_track_index is not None:
                _send_short_press(self.output,
                                  note=TRACK_SELECT_NOTE_BASE + self.preplay_source_track_index,
                                  channel_1based=CONTROL_CHANNEL_1BASED, press_ms=self.press_ms)
                self._event("preplay_source_track_reselected",
                            track_index=self.preplay_source_track_index)
        except Exception as exc:  # recorded, not raised: the run's evidence must survive a send failure
            self.error = str(exc)
            self._event("preplay_error", detail=self.error)

    def _interrupt(self):
        try:
            for index, target_slot in enumerate(self.target_slots):
                if index and self.switch_interval_ms:
                    # The S4b gate asks for the timing evidence "across repeated switches": the interval is
                    # what makes the presses a sequence rather than one gesture.
                    time.sleep(self.switch_interval_ms / 1000.0)
                if self.target_track_index is not None:
                    _send_short_press(self.output, note=TRACK_SELECT_NOTE_BASE + self.target_track_index,
                                      channel_1based=CONTROL_CHANNEL_1BASED, press_ms=self.press_ms)
                    self._event("switch_track_select_sent", track_index=self.target_track_index)
                    time.sleep(self.phase_wait_ms / 1000.0)
                _send_short_press(self.output, note=LOOP_SELECT_NOTE_BASE + target_slot - 1,
                                  channel_1based=CONTROL_CHANNEL_1BASED, press_ms=self.press_ms)
                self._event("switch_slot_select_sent", target_slot=target_slot,
                            target_track_index=self.target_track_index, sequence_index=index)
            self.sent = True
        except Exception as exc:  # recorded, not raised: the run's evidence must survive a send failure
            self.error = str(exc)
            self._event("switch_intervention_error", detail=self.error)


def run_fixture() -> None:
    parser = argparse.ArgumentParser(description="S4b.8/S4b.9 switch-during-finalization device leg")
    parser.add_argument("--track-number", type=int, required=True, choices=range(1, 9),
                        help="The track the predecessor is recorded on (1-based, as the CLI speaks).")
    parser.add_argument("--loop-slot", type=int, required=True, choices=range(1, 9),
                        help="The predecessor's slot (1-based).")
    parser.add_argument("--switch-loop-slot", type=str, required=True,
                        help="Destination slot(s) on the predecessor's track, comma-separated: a single "
                             "press (S4b.8) or a sequence (the S4b gate's repeated switches).")
    parser.add_argument("--switch-interval-ms", type=float, default=0.0,
                        help="Spacing between the presses of a sequence; 0 fires them back to back.")
    parser.add_argument("--switch-track-number", type=int, default=None, choices=range(1, 9),
                        help="S4b.9: also press this track before the slot (a cross-track switch).")
    parser.add_argument("--preplay-track-number", type=int, default=None, choices=range(1, 9),
                        help="Start this track's loop (and optionally hand the focus back) before the pass, so a "
                             "press on one of its other filled slots is a playback switch rather than a selection.")
    parser.add_argument("--preplay-loop-slot", type=int, default=None, choices=range(1, 9),
                        help="The destination track's own filled slot to start (required with --preplay-track-number).")
    parser.add_argument("--out-dir", type=pathlib.Path, required=True)
    parser.add_argument("--intervention-delay-ms", type=float, default=305.0,
                        help="Delay from the overdub-stop button release to the switch press.")
    parser.add_argument("--record-bars", type=int, default=2)
    parser.add_argument("--overdub-bars", type=int, default=2)
    parser.add_argument("--tempo-bpm", type=float, default=120.0)
    parser.add_argument("--overdub-low-note", type=int, default=24)
    parser.add_argument("--overdub-high-note", type=int, default=39)
    parser.add_argument("--overdub-step-clocks", type=int, default=12)
    parser.add_argument("--overdub-gate-clocks", type=int, default=12)
    parser.add_argument("--state-sync-timeout-ms", type=int, default=120000,
                        help="The harness's precondition wait for serial-confirmed state transitions. Raised far above "
                             "its 8 s default because the leg's own preconditions are *not* the property under test, "
                             "and because the device's own rows show the receipts exist: measured 2026-10-01, "
                             "`captures/s4b9-cross-track-18` has the `PERS,result` receipt 3.1 s (device time) after "
                             "the record stop while the harness's 25 s wait still expired -- the ring's drain under "
                             "transport delays *delivery*, not emission. Waiting is the safe action (the harness says "
                             "so itself); pressing into an unretired predecessor is the unsafe one. The run's own "
                             "deadline still bounds this, and it never touches the intervention, which fires from the "
                             "overdub-stop button release.")
    parser.add_argument("--final-wait-ms", type=int, default=12000,
                        help="Long enough to observe the finalization and the switch commit that follows it.")
    config = parser.parse_args()
    if config.preplay_track_number is not None and config.preplay_loop_slot is None:
        parser.error("--preplay-track-number needs --preplay-loop-slot")
    for token in str(config.switch_loop_slot).split(','):
        if token.strip() and not (1 <= int(token) <= 8):
            parser.error("--switch-loop-slot entries must be 1..8")

    out = config.out_dir.resolve()
    out.mkdir(parents=True, exist_ok=False)
    log = (out / "intervention.jsonl").open("w", buffering=1)
    midilog = (out / "midi-output.jsonl").open("w", buffering=1)
    port = None
    finished = threading.Event()

    def monitor():
        nonlocal port
        while not finished.is_set():
            if port is None and (out / ".current_session.ready").exists():
                try:
                    port = mido.open_input(
                        "Teensy MIDI",
                        callback=lambda m: midilog.write(
                            json.dumps({"host_ns": time.monotonic_ns(), "message": m.dict()}) + "\n"))
                    midilog.write(json.dumps({"host_ns": time.monotonic_ns(),
                                              "event": "monitor_opened"}) + "\n")
                except Exception:
                    pass
            finished.wait(0.1)

    original_pass = baseline._run_overdub_pass
    original_transition = baseline.request_state_transition
    interventions: list[SlotSwitchInterventionPort] = []

    def run_pass(*args, **kw):
        wrapped = SlotSwitchInterventionPort(
            args[1], lambda row: log.write(json.dumps(row) + "\n"),
            delay_s=config.intervention_delay_ms / 1000.0,
            channel=int(args[5].midi_channel) - 1,
            target_track_index=(config.switch_track_number - 1
                                if config.switch_track_number is not None else None),
            target_slots=[int(x) for x in str(config.switch_loop_slot).split(',') if x.strip()],
            switch_interval_ms=config.switch_interval_ms,
            preplay_track_index=(config.preplay_track_number - 1
                                 if config.preplay_track_number is not None else None),
            preplay_slot=config.preplay_loop_slot,
            preplay_source_track_index=(config.track_number - 1
                                        if config.preplay_track_number is not None else None))
        if config.preplay_track_number is not None and not any(x.preplay_done for x in interventions):
            wrapped.preplay_done = True
            wrapped._preplay_destination_track()
        interventions.append(wrapped)
        changed = list(args)
        changed[1] = wrapped
        log.write(json.dumps({"host_ns": time.monotonic_ns(),
                              "event": "overdub_pass_wrapper_entered"}) + "\n")
        try:
            return original_pass(*changed, **kw)
        finally:
            wrapped.finish()
            if not wrapped.sent:
                # No switch press went out; the run is still graded, and the ledger says why.
                log.write(json.dumps({"host_ns": time.monotonic_ns(),
                                      "event": "switch_not_sent",
                                      "stream_seen": wrapped.stream_seen,
                                      "armed": wrapped.armed}) + "\n")

    baseline._run_overdub_pass = run_pass
    thread = threading.Thread(target=monitor)
    thread.start()
    args = ["run", "--preset", "base", "--midi-out", "Teensy", "--midi-in", "Teensy",
            "--track-number", str(config.track_number), "--loop-slot", str(config.loop_slot),
            "--midi-channel", str(config.track_number),
            "--record-bars", str(config.record_bars), "--overdub-bars", str(config.overdub_bars),
            "--overdub-low-note", str(config.overdub_low_note),
            "--overdub-high-note", str(config.overdub_high_note),
            "--overdub-step-clocks", str(config.overdub_step_clocks),
            "--overdub-gate-clocks", str(config.overdub_gate_clocks),
            "--tempo-bpm", str(config.tempo_bpm), "--no-fixed-grid-notes", "--start-transport",
            "--overdub-start-delay-bars", "0", "--overdub-start-delay-beats", "1",
            "--phase-wait-ms", "500", "--final-wait-ms", str(config.final_wait_ms),
            "--state-sync-timeout-ms", str(config.state_sync_timeout_ms),
            "--press-ms", "120", "--no-undo-redo-after-overdub-stop", "--second-overdub-bars", "0",
            "--no-stop-after-overdub", "--deferred-save-wait-ms", "15000",
            "--out-dir", str(out),
            "--stimulus-temp-parent", "STIM-BASE-2-ONE-OD-01",
            "--stimulus-temp-suffix", "S4B8-SWITCH-DURING-FINALIZATION",
            "--stimulus-temp-reason",
            "S4b.8/S4b.9: press the destination slot (and optionally its track) while the predecessor's "
            "stop is admitted and its finalization is still in flight"]
    (out / "command.json").write_text(json.dumps(args, indent=2))
    try:
        code = main(args)
        print("Baseline return:", code,
              "switch sent:", any((x.sent for x in interventions)), flush=True)
    finally:
        baseline._run_overdub_pass = original_pass
        baseline.request_state_transition = original_transition
        finished.set()
        thread.join()
        if port:
            port.close()
            midilog.write(json.dumps({"host_ns": time.monotonic_ns(),
                                      "event": "monitor_closed"}) + "\n")
        log.close()
        midilog.close()


if __name__ == "__main__":
    run_fixture()
