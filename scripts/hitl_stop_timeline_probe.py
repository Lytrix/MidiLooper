#!/usr/bin/env python3
"""Capture one or more normal overdub-stop timelines on an authorized slot.

This intentionally small HITL probe bypasses the full baseline recipe after a
host-runner interruption. It selects the requested track and slot, optionally
clears that explicitly authorized scratch location, records a short seed,
enters Live Overdub for each requested pass, adds a note, then requires a fresh
``OVERDUB_STOPPING -> PLAYING`` transition per pass. The resulting serial log
and JSON report are evidence for the device-clock stop timeline.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
import time
import uuid
import threading

import mido

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from hitl.control_constants import (  # noqa: E402
    CONTROL_CHANNEL_1BASED,
    GLOBAL_TRANSPORT_NOTE,
    LOOP_SELECT_NOTE_BASE,
    RECORD_BUTTON_NOTE,
    TRACK_SELECT_NOTE_BASE,
)
from hitl.managed_capture import ManagedCaptureSession  # noqa: E402
from hitl.midi_io import _find_midi_port, _send_long_press, _send_short_press  # noqa: E402
from hitl.verify.midi_boundary_timing import (  # noqa: E402
    midi_boundary_timing_issues,
    summarize_midi_boundary_timing,
)


def _lines(path: Path) -> list[str]:
    if not path.exists():
        return []
    return path.read_text(encoding="utf-8", errors="replace").splitlines()


def fresh_capture_lines(lines: list[str], fence_us: int | None) -> list[str]:
    """Exclude pre-fence device records even when flushed after the acknowledgement."""
    if fence_us is None:
        return []
    fresh = []
    for line in lines:
        if not line.startswith("#CAP,"):
            continue
        try:
            fields = line.split(",")
            timestamp = int(fields[1])
            # Queued telemetry uses its occurrence time, not its later CAP print time.
            if len(fields) > 3 and fields[2] == "DIAG":
                occurrence = {"stop_state": 9, "stop_grain": 11,
                              "stop_service": 5, "stop_emit": 7,
                              "stop_phase": 13, "late_event": 7}.get(fields[3])
                if occurrence is not None and len(fields) > occurrence:
                    timestamp = int(fields[occurrence])
                if fields[3] == "playback_build" and len(fields) >= 16 and int(fields[12]) != 0:
                    timestamp = int(fields[12])
        except (ValueError, IndexError):
            continue
        if ((timestamp - fence_us) & 0xFFFFFFFF) < 0x80000000:
            fresh.append(line)
    return fresh


def _wait_for(path: Path, *, after: int, marker: str, timeout_s: float) -> bool:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        if any(marker in line for line in _lines(path)[after:]):
            return True
        time.sleep(0.025)
    return False


def _wait_for_any(
    path: Path, *, after: int, markers: tuple[str, ...], timeout_s: float
) -> bool:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        suffix = _lines(path)[after:]
        if any(marker in line for line in suffix for marker in markers):
            return True
        time.sleep(0.025)
    return False


def _press(out_port: mido.ports.BaseOutput, note: int, press_ms: int) -> None:
    _send_short_press(
        out_port,
        note=note,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=press_ms,
    )
    # Button short presses take effect after the firmware's double-tap window.
    time.sleep(0.38)


def dense_seed_events() -> list[tuple[float, str, int]]:
    events = [(0.0, "note_on", 95), (0.032, "note_off", 95)]
    for index in range(160):
        at = 0.008 * (index + 1)
        pitch = 60 + index % 16
        events += [(at, "note_on", pitch), (at + 0.032, "note_off", pitch)]
    return sorted(events, key=lambda event: (event[0], event[1] == "note_on"))


def emission_evidence_complete(timeline: dict, track: int, slot: int) -> bool:
    return bool(timeline["correlation_complete"] and timeline["state_correlation_complete"]
                and {row["kind"] for row in timeline["emissions"] if row["valid"]
                    and row.get("track") == track and row.get("slot") == slot}
                >= {"on", "off"})


def emission_deadlines_met(timeline: dict, track: int, slot: int) -> bool:
    return emission_evidence_complete(timeline, track, slot) and all(
        row.get("lateness_us") == 0 for row in timeline["emissions"]
        if row.get("track") == track and row.get("slot") == slot)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--track-number", type=int, default=5, choices=range(1, 9))
    parser.add_argument("--loop-slot", type=int, default=1, choices=range(1, 9))
    parser.add_argument("--capture-port", default="/dev/cu.usbmodem154944801")
    parser.add_argument("--boot-wait-s", type=float, default=10.0)
    parser.add_argument("--press-ms", type=int, default=120)
    parser.add_argument("--clear-before-seed", action="store_true",
                        help="Clear only the requested authorized scratch slot before seeding it.")
    parser.add_argument("--clear-press-ms", type=int, default=1600,
                        help="Hold time for the authorized clear; leaves margin under loaded MIDI service.")
    parser.add_argument("--settle-after-seed-s", type=float, default=5.0,
                        help="Idle time after record-stop before entering overdub.")
    parser.add_argument("--seed-record-duration-s", type=float, default=4.5,
                        help="Minimum seed-record window before stopping (two bars at 120 BPM).")
    parser.add_argument("--phase-timeout-s", type=float, default=20.0)
    parser.add_argument("--overdub-passes", type=int, default=1, choices=range(1, 9),
                        help="Number of sequential note-based overdub passes to stop into PLAYING.")
    parser.add_argument("--empty-overdub", action="store_true",
                        help="Stop without new notes to exercise the empty frozen tail.")
    parser.add_argument("--overdub-duration-s", type=float, default=0.4,
                        help="Time in each overdub before stopping; use a full loop to exercise wrap.")
    parser.add_argument("--dense-stop-probe", action="store_true",
                        help="Seed dense notes, align stop to playback marker, and require On/Off trace evidence")
    parser.add_argument("--stop-after-marker-ms", type=float, default=0.0,
                        help="Additional marker-to-stop delay; device trace still decides overlap")
    parser.add_argument("--record-only", action="store_true",
                        help="Capture the fresh recording interval without entering overdub.")
    args = parser.parse_args()
    if args.record_only and not args.clear_before_seed:
        parser.error("--record-only requires --clear-before-seed on the authorized scratch slot")
    if args.overdub_duration_s < 0:
        parser.error("--overdub-duration-s must be nonnegative")
    if args.stop_after_marker_ms < 0:
        parser.error("--stop-after-marker-ms must be nonnegative")
    if args.dense_stop_probe and not args.clear_before_seed:
        parser.error("--dense-stop-probe requires --clear-before-seed on the authorized scratch slot")

    out_dir = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=False)
    events: list[dict[str, object]] = []
    result: dict[str, object] = {
        "track_number": args.track_number,
        "loop_slot": args.loop_slot,
        "cleared_storage": bool(args.clear_before_seed),
        "overdub_passes_requested": args.overdub_passes,
        "empty_overdub": args.empty_overdub,
        "overdub_duration_s": args.overdub_duration_s,
        "events": events,
    }
    run_token = uuid.uuid4().hex
    result["run_token"] = run_token
    fence_us: int | None = None
    exit_code = 1
    before_stop: int | None = None
    capture_log: Path | None = None
    out_port: mido.ports.BaseOutput | None = None
    in_port = None
    marker_seen = threading.Event()
    marker_armed = threading.Event()
    midi_rows: list[dict] = []

    def observe_midi(message):
        midi_rows.append({"host_ns": time.monotonic_ns(), "message": message.dict()})
        if (marker_armed.is_set() and message.type == "note_on" and message.velocity > 0
                and message.channel == args.track_number - 1 and message.note == 95):
            marker_seen.set()

    with ManagedCaptureSession(
        port=args.capture_port,
        boot_wait_s=args.boot_wait_s,
        out_dir=out_dir,
        run_token=run_token,
    ) as capture:
        try:
            capture_log = capture.session_log
            if capture_log is None:
                raise RuntimeError("managed capture did not publish a session log")
            if not _wait_for(capture_log, after=0, marker=f",TRACE_FENCE,{run_token}",
                             timeout_s=args.phase_timeout_s):
                raise RuntimeError("fresh device-clock fence was not acknowledged")
            fence_line = next(line for line in _lines(capture_log)
                              if f",TRACE_FENCE,{run_token}" in line)
            fence_us = int(fence_line.split(",")[1])
            result["fence_us"] = fence_us
            out_name = _find_midi_port("Teensy", is_input=False)
            out_port = mido.open_output(out_name)
            events.append({"event": "midi_output_opened", "port": out_name})
            if args.dense_stop_probe:
                in_port = mido.open_input(_find_midi_port("Teensy", is_input=True), callback=observe_midi)

            # This control sequence uses the pre-authorized scratch location
            # only; clearing requires the explicit --clear-before-seed flag.
            _press(out_port, TRACK_SELECT_NOTE_BASE + args.track_number - 1, args.press_ms)
            events.append({"event": "track_selected", "track": args.track_number})
            before_slot = len(_lines(capture_log))
            _press(out_port, LOOP_SELECT_NOTE_BASE + args.loop_slot - 1, args.press_ms)
            events.append({"event": "slot_selected", "slot": args.loop_slot})

            midi_channel = args.track_number - 1
            slot_started_playing = _wait_for_any(
                capture_log,
                after=before_slot,
                markers=(",ST,Track,STOPPED,PLAYING", "Playback started"),
                timeout_s=1.5,
            )
            slot_started_recording = _wait_for_any(
                capture_log,
                after=before_slot,
                markers=("Recording started", ",RECA,"),
                timeout_s=0.5,
            )
            if args.clear_before_seed:
                if slot_started_playing:
                    _press(out_port, GLOBAL_TRANSPORT_NOTE, args.press_ms)
                    events.append({"event": "transport_stop_before_clear"})
                    time.sleep(1.0)
                elif slot_started_recording:
                    # Selecting an empty slot can immediately arm and begin a
                    # capture.  Commit that capture before the clear request:
                    # a live capture owns deferred persistence and cannot be
                    # synchronously drained by the clear handler.
                    before_record_stop = len(_lines(capture_log))
                    _press(out_port, RECORD_BUTTON_NOTE, args.press_ms)
                    stopped_recording = _wait_for_any(
                        capture_log,
                        after=before_record_stop,
                        markers=("Playback started", ",ST,Track,RECORDING,PLAYING",
                                 ",ST,Track,RECORDING,STOPPED"),
                        timeout_s=args.phase_timeout_s,
                    )
                    events.append({
                        "event": "recording_stop_before_clear",
                        "confirmed": stopped_recording,
                    })
                    if not stopped_recording:
                        raise RuntimeError("live recording did not stop before scratch-slot clear")
                # Let the recent deferred persistence work settle before the
                # firmware's bounded clear drain. This is the same ownership
                # order used by the focused clear fixture, without opening a
                # second serial reader beside the managed capture process.
                time.sleep(3.0)
                before_clear = len(_lines(capture_log))
                _send_long_press(
                    out_port,
                    note=RECORD_BUTTON_NOTE,
                    channel_1based=CONTROL_CHANNEL_1BASED,
                    press_ms=args.clear_press_ms,
                )
                cleared = _wait_for_any(
                    capture_log,
                    after=before_clear,
                    markers=("Track cleared", "MIDI: Clear selected slot",
                             "Clear ignored — selected slot is empty"),
                    timeout_s=args.phase_timeout_s,
                )
                events.append({"event": "scratch_slot_clear", "confirmed": cleared})
                if not cleared:
                    raise RuntimeError("authorized scratch-slot clear was not confirmed")
                before_slot = len(_lines(capture_log))
                _press(out_port, LOOP_SELECT_NOTE_BASE + args.loop_slot - 1, args.press_ms)
                events.append({"event": "slot_reselected_after_clear", "slot": args.loop_slot})
                slot_started_playing = False
                slot_started_recording = _wait_for_any(
                    capture_log,
                    after=before_slot,
                    markers=("Recording started", ",RECA,"),
                    timeout_s=1.5,
                )
            if slot_started_playing:
                events.append({"event": "transport_ready", "source": "selected_playable_slot"})
            else:
                # Selecting an empty slot may arm it and start recording as
                # part of the firmware's existing record flow. Do not send a
                # second Start in that case: it would immediately toggle the
                # transport back to Stop and create a zero-length seed.
                if not slot_started_recording:
                    before_start = len(_lines(capture_log))
                    _press(out_port, GLOBAL_TRANSPORT_NOTE, args.press_ms)
                    events.append({"event": "transport_start"})
                else:
                    before_start = before_slot
                    events.append({"event": "transport_ready", "source": "slot_selection_record_arm"})
                # A Loop-row press arms an empty scratch slot. Starting
                # transport begins its seed recording; commit it before overdub.
                recording = _wait_for_any(
                    capture_log,
                    after=before_start,
                    markers=("Recording started", ",RECA,"),
                    timeout_s=args.phase_timeout_s,
                )
                events.append({"event": "seed_recording_start", "fresh_recording": recording})
                if not recording:
                    raise RuntimeError("fresh seed recording start was not observed")

                if args.dense_stop_probe:
                    origin = time.monotonic()
                    for at, kind, pitch in dense_seed_events():
                        time.sleep(max(0.0, origin + at - time.monotonic()))
                        out_port.send(mido.Message(kind, channel=midi_channel, note=pitch,
                                                  velocity=100 if kind == "note_on" else 0))
                    events.append({"event": "dense_seed", "notes": 161, "marker": 95})
                else:
                    out_port.send(mido.Message("note_on", channel=midi_channel, note=71, velocity=100))
                    time.sleep(0.10)
                    out_port.send(mido.Message("note_off", channel=midi_channel, note=71, velocity=0))
                    events.append({"event": "seed_note", "channel": midi_channel, "note": 71})
                    # Keep the seed open for a complete two-bar window. A
                    # short fixed delay can stop before the first boundary,
                    # yielding a zero-length recording and invalidating the
                    # normal-stop fixture before overdub begins.
                    time.sleep(max(args.seed_record_duration_s - 0.10, 0.0))
                before_seed_stop = len(_lines(capture_log))
                _press(out_port, RECORD_BUTTON_NOTE, args.press_ms)
                seed_playing = _wait_for_any(
                    capture_log,
                    after=before_seed_stop,
                    markers=("Playback started", ",ST,Track,RECORDING,PLAYING"),
                    timeout_s=args.phase_timeout_s,
                )
                events.append({"event": "seed_recording_stop", "fresh_playing": seed_playing})
                if not seed_playing:
                    raise RuntimeError("fresh seed recording stop to PLAYING was not observed")
                time.sleep(max(args.settle_after_seed_s, 0.0))
                events.append({"event": "seed_settle", "seconds": args.settle_after_seed_s})

            if not args.record_only:
                for pass_index in range(args.overdub_passes):
                    before_overdub = len(_lines(capture_log))
                    _press(out_port, RECORD_BUTTON_NOTE, args.press_ms)
                    overdubbing = _wait_for(
                        capture_log,
                        after=before_overdub,
                        marker="Overdubbing started",
                        timeout_s=args.phase_timeout_s,
                    )
                    events.append({"event": "overdub_start", "pass": pass_index + 1,
                                   "fresh_overdubbing": overdubbing})
                    if not overdubbing:
                        raise RuntimeError(f"Live Overdub pass {pass_index + 1} start was not observed")

                    # A distinct note makes each active per-pass playback path observable.
                    note = 83 + pass_index
                    if not args.empty_overdub:
                        out_port.send(mido.Message("note_on", channel=midi_channel, note=note, velocity=100))
                        time.sleep(0.10)
                        out_port.send(mido.Message("note_off", channel=midi_channel, note=note, velocity=0))
                        events.append({"event": "overdub_note", "pass": pass_index + 1,
                                       "channel": midi_channel, "note": note})
                    time.sleep(max(0.0, args.overdub_duration_s - (0 if args.empty_overdub else 0.10)))

                    if args.dense_stop_probe:
                        marker_seen.clear()
                        marker_armed.set()
                        if not marker_seen.wait(args.phase_timeout_s):
                            raise RuntimeError("fresh playback marker 95 was not observed on MIDI output")
                        marker_armed.clear()
                        events.append({"event": "playback_marker_before_stop", "pass": pass_index + 1,
                                       "host_ns": time.monotonic_ns(),
                                       "additional_stop_delay_ms": args.stop_after_marker_ms})
                        time.sleep(args.stop_after_marker_ms / 1000.0)
                    before_stop = len(_lines(capture_log))
                    _press(out_port, RECORD_BUTTON_NOTE, args.press_ms)
                    # State-edge trace records are retained until capture cleanup, so
                    # they are not a reliable live-file signal. Give the bounded owner
                    # one second, then let the next pass's actual overdub admission prove
                    # that the previous frozen transaction retired.
                    time.sleep(1.0)
                    events.append({
                        "event": "normal_stop_observation_end",
                        "pass": pass_index + 1,
                        "background_settle_wait_s": 1.0,
                    })
                # Active transport retains CAP records in the PSRAM ring and
                # flushes them only after cleanup. The post-session device-clock
                # timeline remains the authoritative correlation evidence.

        except Exception as exc:
            result["error"] = str(exc)
        finally:
            if out_port is not None:
                # Restore a known transport state after evidence is captured.
                out_port.send(mido.Message("stop"))
                events.append({"event": "cleanup_transport_stop"})
                # Active-transport capture is deliberately retained. Give the
                # deferred owner a bounded chance to resolve the transport
                # barrier and flush its trace before this managed session
                # closes, even when the normal-stop observation timed out.
                time.sleep(3.0)
                out_port.close()
            if in_port is not None:
                in_port.close()
            if args.dense_stop_probe:
                (out_dir / "midi-output.jsonl").write_text(
                    "".join(json.dumps(row) + "\n" for row in midi_rows))

    # The capture child has now closed its log after cleanup/flush. Evaluate
    # evidence here, including failure paths, rather than against an active ring.
    if capture_log is not None:
        result["capture_log"] = str(capture_log)
    if capture_log is not None and before_stop is not None:
        summary = summarize_midi_boundary_timing(
            fresh_capture_lines(_lines(capture_log), fence_us),
            after_index=None, repo_root=ROOT
        )
        timeline = summary["stop_timeline"]
        states = timeline["states"]
        normal = any(
            entry["valid"] and entry["track"] == args.track_number - 1
            and entry["slot"] == args.loop_slot - 1
            and entry["from_state"] == 6 and entry["to_state"] == 7
            and any(
                terminal["valid"] and terminal["track"] == entry["track"]
                and terminal["slot"] == entry["slot"]
                and terminal["from_state"] == 7 and terminal["to_state"] == 5
                and ((terminal["at_us"] - entry["at_us"]) & 0xFFFFFFFF) < 0x80000000
                for terminal in states[index + 1:]
            )
            for index, entry in enumerate(states)
        )
        stop_to_playing_count = sum(
            1 for entry in states
            if entry["valid"] and entry["track"] == args.track_number - 1
            and entry["slot"] == args.loop_slot - 1
            and entry["from_state"] == 7 and entry["to_state"] == 5
        )
        result.update(stop_start_line=before_stop, stop_timeline=timeline,
                      timing_issues=midi_boundary_timing_issues(summary),
                      normal_stop_verified=normal,
                      overdub_passes_observed=stop_to_playing_count)
        if any("#CAPTURE_RECONNECT" in line for line in _lines(capture_log)):
            result["error"] = "serial reconnected after run fence; attribution invalid"
        if args.dense_stop_probe:
            result["emission_evidence_complete"] = emission_evidence_complete(timeline, args.track_number - 1, args.loop_slot - 1)
            result["stop_emission_deadlines_met"] = emission_deadlines_met(
                timeline, args.track_number - 1, args.loop_slot - 1)
            result["probe_scope"] = "stop correlation; full real-time reservation is not established"
            if not result["emission_evidence_complete"]:
                result["error"] = "stop trace lacks correlated NoteOn and NoteOff evidence"
        if (normal and stop_to_playing_count >= args.overdub_passes
                and timeline["correlation_complete"]
                and not result["timing_issues"] and "error" not in result):
            exit_code = 0
        elif "error" not in result:
            result["error"] = "flushed capture did not prove a correlated normal stop"

    if args.record_only:
        result["probe_scope"] = "recording interval only; no overdub-stop acceptance"
        result["recording_sequence_completed"] = "error" not in result and any(
            event.get("event") == "seed_recording_stop" and event.get("fresh_playing")
            for event in events)
        exit_code = 0 if result["recording_sequence_completed"] else 1

    (out_dir / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2), flush=True)
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
