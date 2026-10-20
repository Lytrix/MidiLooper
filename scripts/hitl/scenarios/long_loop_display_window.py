"""Long-loop bounded window: NOTE_EDIT freeze, play/stop long-press snap, hold-to-track.

The note overlay is entered **and** left with short presses of the Edit button: short press is
`CYCLE_EDIT_MODE` (LOOP_EDIT ↔ NOTE_EDIT), while the long press is `EXIT_EDIT_MODE` and leaves edit mode altogether.
"""

from __future__ import annotations

import argparse
import json
import time
from datetime import datetime
from pathlib import Path
from typing import Optional

from hitl.context import get_context

def record_press_needs_retry(latest_state: Optional[str]) -> bool:
    """Whether a record press that was not *observed* may be pressed again.

    The managed capture's session log lags the wire, so "not confirmed" is not the same as "did not start". A second
    press on a device that **is** recording stops the take — measured 2026-10-20 as a one-bar take
    (`DIAG,record_length,ticks=768`) whose display rows never arrive, which then reads as
    `missing_playing_bounded_window_disp` in every downstream check. Only the device's own latest state decides.
    """
    return latest_state != "RECORDING"


def record_stop_needs_retry(latest_state: Optional[str]) -> bool:
    """Whether a record-stop press that was not *observed* may be pressed again.

    The same lag trap as the start press, one step later: the first stop press may have landed while the capture log
    still shows the take as unfinished. A second press there is a fresh record gesture — measured 2026-10-20 as a
    transport stop at the end of the take, after which every NOTE_EDIT phase ran with the transport stopped and the
    playing window rows never arrived. Only a device that is **still recording** gets the second stop press.
    """
    return latest_state == "RECORDING"


def latest_edit_session_mode(lines: list[str]) -> Optional[str]:
    """The device's latest edit-session ack (`NOTE_EDIT` / `LOOP_EDIT`), or None when none is in the log.

    The Edit button's bindings are a **cycle**: short press is `CYCLE_EDIT_MODE` (LOOP_EDIT ↔ NOTE_EDIT; from no
    session it opens NOTE_EDIT) and long press is `EXIT_EDIT_MODE`. So the note overlay is entered *and* left with
    short presses, and the gesture that leaves it must be chosen from the mode the device last announced — a second
    short press on a device that already cycled is the return trip.
    """
    mode: Optional[str] = None
    for line in lines:
        if "Edit session: NOTE_EDIT" in line:
            mode = "NOTE_EDIT"
        elif "Edit session: LOOP_EDIT" in line:
            mode = "LOOP_EDIT"
    return mode


def note_edit_repress_needed(mode_since_press: Optional[str]) -> bool:
    """Whether an unconfirmed NOTE_EDIT entry may be pressed again.

    Only a fresh `LOOP_EDIT` ack *since our own press* says the press cycled instead of entering; then the next short
    press is the way into NOTE_EDIT. With no ack yet the state is unknown, so pressing again could cycle an
    unobserved NOTE_EDIT straight back to LOOP_EDIT — the measured re-press trap.
    """
    return mode_since_press == "LOOP_EDIT"


def _track_index(track_number_1based: int) -> int:
    """0-based track index — matches host_midi_automation_baseline first_track_index."""
    return track_number_1based - 1

# Seconds of lateness the host-mode record stop must lead by: the post-stream wait the leg keeps for its
# transition baseline (0.5 s), the press's own dwell (0.12 s at the default `--press-ms`) and the device's
# short-press double-tap window (~0.3 s). The device quantises the committed length to the nearest bar, so the
# press only has to land inside the last bar; a lead left at zero lands in the next bucket at a slow tempo
# (90 BPM: bar 2.67 s, half-bar quantum 1.33 s, measured lateness ~1.3 s ->
# `captures/i5-s37g-host-clock-02` committed 33 bars). At 120 BPM the same lateness is inside the quantum, which
# is why the device-clock leg never showed this. 0.9 s is the measured lateness and leaves margin below half a
# bar even at 120 BPM (43 clocks against the 48 a tie would need).
HOST_STOP_LEAD_SECONDS = 0.9


def _host_stop_lead_clocks(tempo_bpm: float) -> int:
    """Clocks of lead the host-mode stop press needs at ``tempo_bpm`` (24 PPQN).

    ``HOST_STOP_LEAD_SECONDS`` is a wall-clock lateness, so the lead in clocks follows the tempo (32 clocks at
    90 BPM, 43 at 120). It is capped at 0.4 bar (38 clocks) because a press further ahead of the boundary than that
    starts costing the bar it is meant to close at a fast tempo; the cap still lands the press inside the last bar
    the quantiser rounds to.
    """
    lateness_clocks = HOST_STOP_LEAD_SECONDS * 24.0 * tempo_bpm / 60.0
    return max(1, min(int(round(lateness_clocks)), 38))


def _seconds_per_bar(clock_source: str, tempo_bpm: float) -> float:
    """Bar length the record phase's guard is sized with, for the leg's two clock sources.

    Device mode keeps the measured 2 s bar (24 PPQN at the device's own 120 BPM transport), so its behaviour is
    unchanged; host mode follows the tempo the host actually sends, so a slow host clock is not cut off by a guard
    sized for the device's clock.
    """
    if clock_source == "host":
        return (60.0 / tempo_bpm) * 4.0
    return 2.0

def _effective_sync_timeout_s(record_bars: int, configured_ms: int) -> float:
    """Scale state-sync waits for long-loop stop finalize + deferred save."""
    scaled_ms = max(configured_ms, record_bars * 1000)
    return min(scaled_ms, 120_000) / 1000.0

def _track_select_note(track_number_1based: int) -> int:
    from hitl.control_constants import TRACK_SELECT_NOTE_BASE

    return TRACK_SELECT_NOTE_BASE + _track_index(track_number_1based)

def _ensure_note_edit_entered(
    out_port,
    serial_collector,
    *,
    press_ms: int,
    phase_wait_ms: int,
    timeout_s: float = 12.0,
) -> bool:
    from hitl.control_constants import (
        CONTROL_CHANNEL_1BASED,
        EDIT_BUTTON_DEBOUNCE_MS,
        EDIT_BUTTON_NOTE,
    )
    from hitl.midi_io import _send_short_press

    def _note_edit_active(lines: list[str]) -> bool:
        if latest_edit_session_mode(lines) == "NOTE_EDIT":
            return True
        return any("entered note edit mode" in line for line in lines)

    def _note_edit_entered_in_suffix(suffix: list[str]) -> bool:
        # Only note-specific acks count. `MIDI Edit Mode: Short press` is printed for every cycle press, including the
        # one that left NOTE_EDIT for LOOP_EDIT, so it cannot confirm an entry.
        if any("Edit session: NOTE_EDIT" in line for line in suffix):
            return True
        if any("entered note edit mode" in line for line in suffix):
            return True
        if any("EditSession opened editPass=0" in line for line in suffix):
            return True
        return False

    snapshot = serial_collector.snapshot()
    if _note_edit_active(snapshot):
        print("[long-loop-display-hitl] NOTE_EDIT already active")
        return True

    for attempt in range(1, 3):
        baseline = len(serial_collector.snapshot())
        print(f"[long-loop-display-hitl] enter NOTE_EDIT attempt {attempt}/2")
        _send_short_press(
            out_port,
            note=EDIT_BUTTON_NOTE,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=press_ms,
        )
        time.sleep(max(EDIT_BUTTON_DEBOUNCE_MS, phase_wait_ms) / 1000.0)
        deadline = time.monotonic() + max(timeout_s, 0.0)
        last_press_at = time.monotonic()
        while time.monotonic() < deadline:
            suffix = serial_collector.snapshot()[baseline:]
            if _note_edit_entered_in_suffix(suffix):
                print("[long-loop-display-hitl] NOTE_EDIT enter confirmed")
                return True
            mode_since_press = latest_edit_session_mode(suffix)
            if time.monotonic() - last_press_at >= 5.0 and note_edit_repress_needed(mode_since_press):
                print(
                    "[long-loop-display-hitl] re-press edit (the press cycled to LOOP_EDIT, not NOTE_EDIT)"
                )
                _send_short_press(
                    out_port,
                    note=EDIT_BUTTON_NOTE,
                    channel_1based=CONTROL_CHANNEL_1BASED,
                    press_ms=press_ms,
                )
                last_press_at = time.monotonic()
                time.sleep(max(EDIT_BUTTON_DEBOUNCE_MS, phase_wait_ms) / 1000.0)
            time.sleep(0.05)
    return False


def _ensure_note_edit_exited(
    out_port,
    serial_collector,
    *,
    press_ms: int,
    timeout_s: float = 8.0,
) -> bool:
    """Leave NOTE_EDIT through the surface's own cycle: a short press moves NOTE_EDIT → LOOP_EDIT.

    The Edit button's long press is `EXIT_EDIT_MODE` — it leaves edit mode altogether rather than closing the note
    overlay, and the leg's description ("exit NOTE_EDIT") does not mean that. The short press is the documented
    cycle (`EditManager::cycleEditSession`), and its ack (`Edit session: LOOP_EDIT`) is the exit evidence.

    State-aware like the record press: the press is sent only when the device's latest ack says NOTE_EDIT, and a lagging
    ack is **not** answered with a second press — that would cycle straight back into NOTE_EDIT.
    """
    from hitl.control_constants import (
        CONTROL_CHANNEL_1BASED,
        EDIT_BUTTON_NOTE,
    )
    from hitl.midi_io import _send_short_press

    snapshot = serial_collector.snapshot()
    mode = latest_edit_session_mode(snapshot)
    if mode == "LOOP_EDIT":
        print("[long-loop-display-hitl] NOTE_EDIT already left (LOOP_EDIT)")
        return True
    if mode != "NOTE_EDIT":
        print(
            f"[long-loop-display-hitl] error: NOTE_EDIT exit refused — the device's latest edit session is "
            f"{mode or 'unknown'}, so a cycle press has no defined direction"
        )
        return False

    baseline = len(snapshot)
    _send_short_press(
        out_port,
        note=EDIT_BUTTON_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=press_ms,
    )
    deadline = time.monotonic() + max(timeout_s, 0.0)
    while time.monotonic() < deadline:
        if latest_edit_session_mode(serial_collector.snapshot()[baseline:]) == "LOOP_EDIT":
            print("[long-loop-display-hitl] NOTE_EDIT exit confirmed (LOOP_EDIT)")
            return True
        time.sleep(0.05)
    print("[long-loop-display-hitl] error: NOTE_EDIT exit not confirmed (no LOOP_EDIT ack)")
    return False


def _parse_common_args(args: object) -> argparse.Namespace:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--midi-out", default="Teensy")
    parser.add_argument("--midi-in", default="Teensy")
    parser.add_argument("--serial-port", default=None)
    parser.add_argument("--serial-baud", type=int, default=115200)
    parser.add_argument("--track-number", type=int, default=5)
    parser.add_argument("--track", type=int, dest="track_number")
    parser.add_argument("--midi-channel", type=int, default=None)
    parser.add_argument("--record-bars", type=int, default=24)
    parser.add_argument(
        "--stop-press-advance-clocks",
        type=int,
        default=0,
        help=(
            "Send the record stop this many clocks early. The device quantises the take up to the next bar, so a stop "
            "that lands just past the requested bar yields one bar more than asked for; advancing by the overshoot "
            "(96 clocks = one bar at 24 PPQN / 4-4) lands the take on the requested length"
        ),
    )
    parser.add_argument(
        "--clock-source",
        choices=("device", "host"),
        default="device",
        help=(
            "Which transport clock paces the take. 'device' (default) is the device's own transport: the leg "
            "presses play/stop and the stream is paced by the wall-clock proxy. 'host' is the host's 24 PPQN at "
            "--tempo-bpm: the host sends the realtime Start (which is the device's tick 0) and owns the transport, "
            "so the leg sends no transport press and the stream is paced by the pump. Same shape as "
            "legacy_record_baseline.py's --clock-source"
        ),
    )
    parser.add_argument(
        "--tempo-bpm",
        type=float,
        default=120.0,
        help=(
            "Tempo the host clock runs at in --clock-source host, and the bar-to-seconds conversion for either "
            "source. 24 PPQN at this tempo is what the device slaves to; the take's length must still come out at "
            "a whole number of bars (len % TICKS_PER_BAR == 0)"
        ),
    )
    parser.add_argument("--press-ms", type=int, default=120)
    parser.add_argument(
        "--long-press-ms",
        type=int,
        default=700,
        help=(
            "Unused by the note-overlay exit: NOTE_EDIT is entered and left by the short-press cycle "
            "(CYCLE_EDIT_MODE), not by the long press (EXIT_EDIT_MODE). Retained so older invocations still parse"
        ),
    )
    parser.add_argument(
        "--play-stop-long-press-ms",
        type=int,
        default=3100,
        help="Play/stop (note 40) long-press for window snap — must exceed firmware 3000ms threshold",
    )
    parser.add_argument("--phase-wait-ms", type=int, default=500)
    parser.add_argument("--freeze-wait-ms", type=int, default=8000)
    parser.add_argument("--hold-track-ms", type=int, default=4000)
    parser.add_argument(
        "--adoption-hold-s",
        type=float,
        default=0.0,
        help=(
            "Keep the transport running this many seconds immediately after the leg starts it, while the take "
            "already in the slot plays back. This is where the rolling lane's demand is created without a record "
            "window bound (`rw=0`), so it is the only place in this leg where a `win_ready` successor can reach its "
            "own adoption boundary: measured 2026-10-19, the ready at 3399.8 s was cancelled 13.5 s later when the "
            "leg cleared and re-recorded, before its boundary. Zero keeps the leg's behaviour unchanged"
        ),
    )
    parser.add_argument(
        "--display-hold-s",
        type=float,
        default=0.0,
        help=(
            "Keep the transport running this many seconds after NOTE_EDIT exits, before the capture is read. The "
            "rolling lane's readiness receipt is delivered when a successor is adopted at its own span boundary, and a "
            "re-positioned demand can land late in a long loop's traversal: without a hold the leg stops before that "
            "boundary and the serving receipt is never reachable (measured 2026-10-19: ready at 2162.7 s, stop at "
            "2176.2 s, three 2-bar boundaries short of adoption)"
        ),
    )
    parser.add_argument("--start-transport", action="store_true", default=True)
    parser.add_argument("--clear-before-record", action="store_true", default=True)
    parser.add_argument("--clear-press-ms", type=int, default=900)
    parser.add_argument("--state-sync-timeout-ms", type=int, default=8000)
    parser.add_argument("--boot-settle-ms", type=int, default=10000)
    parser.add_argument("--serial-grace-ms", type=int, default=3000)
    # The runner's **managed capture** owns the USB port for those runs and hands the session log to a
    # legacy driver rather than letting it open the port (`runner.py`: `merged += ["--out-dir", ...]` and
    # `legacy_args_with_managed_capture` adds `--follow-serial-log <path>`); the base legacy drivers
    # (`legacy_record_baseline`, `legacy_edit_baseline`) take both. This leg did not, so a managed run
    # aborted in argparse **before driving anything** (`unrecognized arguments: --follow-serial-log …
    # --out-dir …`, F-17). Accepting them is the bounded prerequisite for driving task 6.4's profile
    # through the content-verification pipeline's managed path; neither is a capture-ownership argument
    # (`--serial-port` is), and this leg never writes the followed log.
    parser.add_argument("--follow-serial-log", default=None)
    parser.add_argument("--follow-current-session", action="store_true")
    parser.add_argument("--out-dir", default=None)
    legacy = list(getattr(args, "legacy_args", []) or [])
    return parser.parse_args(legacy)


class FollowedSerialLog:
    """Read-only view of a capture log **another process owns** (the runner's managed capture).

    It offers exactly what this leg and its helpers ask of a collector -- `snapshot()` -- and a no-op
    `stop()`, so the leg's serial-derived waits, its LOOP_EDIT precondition and its verification all run
    against the managed session log. That is also the only place the boot banner (`Edit session:
    LOOP_EDIT`) can be seen when the capture attached before the sketch started, which is the state a
    managed run's `--boot-reset` deliberately produces.
    """

    def __init__(self, path: Path) -> None:
        self.path = Path(path)

    def snapshot(self) -> list[str]:
        try:
            return self.path.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError:
            return []

    def stop(self) -> None:
        """Nothing to stop: this view does not own the port."""


def followed_serial_log_path(ns: argparse.Namespace) -> Optional[Path]:
    """The capture log to *read* instead of owning the port, when the runner named one."""
    if ns.follow_serial_log:
        return Path(ns.follow_serial_log)
    if ns.follow_current_session:
        from hitl.baseline_loop_inventory import current_session_serial_log

        out_dir = Path(ns.out_dir) if ns.out_dir else Path("captures")
        return current_session_serial_log(out_dir)
    return None
def run_long_loop_display_window(args: object) -> int:
    import mido
    from hitl.serial_transport import resolve_wall_tempo_for_proxy
    from hitl.transport_clock import ensure_transport_clock
    from hitl.midi_clock import MidiClockMaster
    from hitl.control_constants import (
        CONTROL_CHANNEL_1BASED,
        EDIT_BUTTON_NOTE,
        GLOBAL_TRANSPORT_NOTE,
        PLAY_STOP_BUTTON_NOTE,
        RECORD_BUTTON_NOTE,
        RECORD_GRID_STEP_CLOCKS,
    )
    from hitl.serial_collector import (
        RunAbort,
        SerialCaptureCollector,
    )
    from hitl.midi_io import (
        _find_midi_port,
        _send_short_press,
    )
    from hitl.capture_transitions import (
        _latest_track_state,
        _wait_for_transition_count,
    )
    from host_midi_automation_baseline import (
        _count_reca_markers,
        _recording_transition_baseline,
        _stream_pattern_for_bars,
        _wait_for_recording_started,
    )
    from hitl.edit_controls import (
        _ensure_transport_running,
        _send_long_press,
        _stop_transport_if_running,
    )
    from host_midi_automation_edit_baseline import _ensure_clear_to_empty

    ns = _parse_common_args(args)
    ctx = get_context(args)
    track_index = _track_index(ns.track_number)
    midi_channel = ns.midi_channel if ns.midi_channel is not None else ns.track_number
    ctx.record_bars = ns.record_bars
    setattr(args, "record_bars", ns.record_bars)

    if ns.record_bars <= 16:
        print("[long-loop-display-hitl] error: --record-bars must be > 16 for bounded window")
        return 2
    if ns.tempo_bpm <= 0:
        print("[long-loop-display-hitl] error: --tempo-bpm must be > 0")
        return 2

    # The runner's `--out-dir` is this leg's artifact directory too (the base legacy drivers route theirs
    # the same way), so a managed run's log/report land with its session log rather than in the default.
    if ns.out_dir:
        setattr(args, "out_dir", Path(ns.out_dir))

    out_port = mido.open_output(_find_midi_port(ns.midi_out, "output"))
    in_port = mido.open_input(_find_midi_port(ns.midi_in, "input"))
    # Host clock source: one master per run, bound to the output port it drives. `None` in device mode, so every
    # branch below keeps its measured device-clock shape and the flag's default changes nothing.
    clock_master: Optional[MidiClockMaster] = (
        MidiClockMaster(out_port) if ns.clock_source == "host" else None
    )
    serial_collector: Optional[SerialCaptureCollector] = None
    abort = RunAbort()
    exit_code = 0

    try:
        if ns.serial_port:
            serial_collector = SerialCaptureCollector(ns.serial_port, baud=ns.serial_baud)
            serial_collector.start()
            if ns.boot_settle_ms > 0:
                print(
                    f"[long-loop-display-hitl] boot settle {ns.boot_settle_ms}ms "
                    "(Teensy resets when serial opens)"
                )
                time.sleep(ns.boot_settle_ms / 1000.0)
        else:
            # The runner's managed capture owns the port and hands us its session log instead (F-17): read it,
            # never open the port -- two owners of one USB serial port is a broken run.
            followed_log = followed_serial_log_path(ns)
            if followed_log is not None:
                serial_collector = FollowedSerialLog(followed_log)  # type: ignore[assignment]
                print(
                    f"[long-loop-display-hitl] following the capture's session log: {followed_log} "
                    "(the capture owns the port; this leg reads the log instead)"
                )
                if ns.boot_settle_ms > 0:
                    time.sleep(ns.boot_settle_ms / 1000.0)


        if ns.start_transport:
            if clock_master is not None:
                # Host clock source: the host owns the transport, and nothing starts here. The realtime Start goes
                # out **after the record arm** (below), because that Start is the device's tick 0 -- the origin the
                # take is measured from. Starting it here would leave the take's first bar ahead of the stream's
                # anchor: measured as a 33-bar commit on the first host-clock run (`captures/i5-s37g-host-clock-01`,
                # `DIAG,record_length,ticks=25344`). A transport press would toggle off whatever the Start begins,
                # and an inbound-clock wait can only fail because a slaved device emits no clock of its own.
                print(
                    "[long-loop-display-hitl] clock source host: transport stays stopped until the record arm "
                    f"({ns.tempo_bpm:.1f} BPM, continuous 24 PPQN pump after it)"
                )
            else:
                _ensure_transport_running(
                    out_port,
                    in_port,
                    press_ms=ns.press_ms,
                    phase_wait_ms=ns.phase_wait_ms,
                    serial_collector=serial_collector,
                    transport_args=ns,
                )

        if ns.adoption_hold_s > 0:
            print(
                f"[long-loop-display-hitl] adoption hold {ns.adoption_hold_s:.1f}s on the slot's existing take "
                "(the lane's demand is live here with no record window bound)"
            )
            ctx.markers.append("phase:adoption_hold_start")
            time.sleep(ns.adoption_hold_s)
            ctx.markers.append("phase:adoption_hold_end")

        print(
            f"[long-loop-display-hitl] select track {ns.track_number} "
            f"(index {track_index}, MIDI ch {midi_channel})"
        )
        _send_short_press(
            out_port,
            note=_track_select_note(ns.track_number),
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
        )
        time.sleep(ns.phase_wait_ms / 1000.0)

        if serial_collector is not None:
            from hitl.edit_mode_precondition import ensure_loop_edit_before_record

            if not ensure_loop_edit_before_record(
                out_port,
                serial_collector,
                phase_wait_ms=ns.phase_wait_ms,
            ):
                print("[long-loop] LOOP_EDIT not proven by the long edit press; aborting")
                return 2

        if ns.clear_before_record:
            if serial_collector is not None:
                # Recover transport clock after serial boot / prior STOPPED (same as base preset). Host mode owes no
                # recovery: there is no device clock to wait for (a slaved device emits none) and a transport press
                # would start a run the later realtime Start is meant to own (the reference
                # `legacy_record_baseline.py` has the same asymmetry).
                if ns.start_transport and clock_master is None:
                    _send_short_press(
                        out_port,
                        note=GLOBAL_TRANSPORT_NOTE,
                        channel_1based=CONTROL_CHANNEL_1BASED,
                        press_ms=ns.press_ms,
                    )
                    time.sleep(ns.phase_wait_ms / 1000.0)
                    clock_ok, _using_proxy = ensure_transport_clock(
                        in_port,
                        out_port,
                        serial_collector,
                        ns,
                        min_clocks=24,
                        timeout_s=2.0,
                        abort=abort,
                    )
                    if not clock_ok:
                        print(
                            "[long-loop-display-hitl] MIDI clock missing after transport "
                            "start; retrying transport"
                        )
                        _send_short_press(
                            out_port,
                            note=GLOBAL_TRANSPORT_NOTE,
                            channel_1based=CONTROL_CHANNEL_1BASED,
                            press_ms=ns.press_ms,
                        )
                        time.sleep(ns.phase_wait_ms / 1000.0)
                if clock_master is not None:
                    # The clear needs a stopped transport, and the host owns it: a realtime Stop (plus a pump stop,
                    # which is a no-op here). Nothing restarts it until the record arm below.
                    clock_master.stop()
                else:
                    _stop_transport_if_running(
                        out_port,
                        in_port,
                        press_ms=ns.press_ms,
                        phase_wait_ms=ns.phase_wait_ms,
                    )
                if not _ensure_clear_to_empty(
                    out_port,
                    serial_collector,
                    clear_press_ms=ns.clear_press_ms,
                    state_sync_timeout_ms=ns.state_sync_timeout_ms,
                    abort=abort,
                ):
                    print("[long-loop-display-hitl] clear failed")
                    return 1
            else:
                _send_long_press(
                    out_port,
                    note=RECORD_BUTTON_NOTE,
                    channel_1based=CONTROL_CHANNEL_1BASED,
                    press_ms=ns.clear_press_ms,
                )
            if ns.start_transport:
                if clock_master is None:
                    # Device clock: the transport must run again for the record press to capture. Host mode stays
                    # stopped on purpose -- the Start that makes tick 0 is the one after the record arm below (an
                    # early Start is the 33-bar take noted above).
                    _ensure_transport_running(
                        out_port,
                        in_port,
                        press_ms=ns.press_ms,
                        phase_wait_ms=ns.phase_wait_ms,
                        serial_collector=serial_collector,
                        transport_args=ns,
                    )

        print(f"[track {track_index}] record {ns.record_bars} bars")
        record_baseline_len = len(serial_collector.snapshot()) if serial_collector is not None else 0
        baseline_reca = (
            _count_reca_markers(serial_collector.snapshot()) if serial_collector is not None else 0
        )
        baseline_recording_transitions = (
            _recording_transition_baseline(serial_collector.snapshot())
            if serial_collector is not None
            else 0
        )
        baseline_latest_state = (
            _latest_track_state(serial_collector.snapshot()) if serial_collector is not None else None
        )
        _send_short_press(
            out_port,
            note=RECORD_BUTTON_NOTE,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.press_ms,
        )
        # Grid origin of the take, when the host owns the clock: the realtime Start below is the device's tick 0,
        # and the first pump pulse after it is grid position 1. Passing this to the streamer makes its bar budget
        # count from the take's own origin, so a slow RECORDING confirmation costs notes at the start of the
        # pattern instead of adding a bar to the commit (`grid_anchor_missed_pulses` reports how far behind it was).
        record_grid_origin_pulse: Optional[int] = None
        if clock_master is not None:
            clock_master.start(bpm=ns.tempo_bpm, abort=abort)
            record_grid_origin_pulse = clock_master.grid_origin_pulse()
            print(
                "[long-loop-display-hitl] host Start after record arm (device tick 0), grid origin pulse "
                f"{record_grid_origin_pulse}"
            )
        reached_recording = False
        if serial_collector is not None:
            reached_recording = _wait_for_recording_started(
                serial_collector,
                baseline_len=record_baseline_len,
                baseline_reca=baseline_reca,
                baseline_recording_transitions=baseline_recording_transitions,
                baseline_latest_state=baseline_latest_state,
                timeout_s=ns.state_sync_timeout_ms / 1000.0,
                serial_grace_s=ns.serial_grace_ms / 1000.0,
                abort=abort,
            )
            if not reached_recording:
                # The managed capture's session log lags the wire, so "not confirmed" can mean "not observed yet".
                # Pressing again in that state **stops** the take the first press started -- measured 2026-10-20 as a
                # one-bar take (`DIAG,record_length,ticks=768`) whose display rows never arrive, which then reads as
                # `missing_playing_bounded_window_disp` in every downstream check. Re-read the device's own latest
                # state before deciding; only a device that is genuinely not recording gets the second press.
                lagging = _latest_track_state(serial_collector.snapshot(), after_index=record_baseline_len)
                if not record_press_needs_retry(lagging):
                    print(
                        "[long-loop-display-hitl] RECORDING confirmed late (the capture log lagged the press); "
                        "not pressing again"
                    )
                    reached_recording = True
                else:
                    print("[long-loop-display-hitl] warn: RECORDING not confirmed; retrying record press")
                    _send_short_press(
                        out_port,
                        note=RECORD_BUTTON_NOTE,
                        channel_1based=CONTROL_CHANNEL_1BASED,
                        press_ms=ns.press_ms,
                    )
                    reached_recording = _wait_for_recording_started(
                        serial_collector,
                        baseline_len=record_baseline_len,
                        baseline_reca=baseline_reca,
                        baseline_recording_transitions=baseline_recording_transitions,
                        baseline_latest_state=baseline_latest_state,
                        timeout_s=ns.state_sync_timeout_ms / 1000.0,
                        serial_grace_s=ns.serial_grace_ms / 1000.0,
                        abort=abort,
                    )
            if not reached_recording:
                # Nothing is recording: continuing would record nothing, then fail the display checks for reasons that
                # have nothing to do with the window. Name the real cause instead.
                print("[long-loop-display-hitl] error: the record press was not confirmed; the leg cannot record")
                return 2
        else:
            time.sleep(ns.phase_wait_ms / 1000.0)

        if serial_collector is not None and not reached_recording:
            print("[long-loop-display-hitl] error: record did not reach RECORDING")
            return 1

        if clock_master is not None:
            # A slaved device emits no clock of its own, so there is nothing to wait for and no proxy to resolve:
            # the pump is the clock, and the phase below is paced by it (`master=`), never by wall-clock seconds.
            clock_ok, using_serial_proxy = True, False
            wall_clock_tempo = None
            print(
                "[long-loop-display-hitl] host clock before record stream: "
                f"{ns.tempo_bpm:.1f} BPM, pump pulse {clock_master.pulses_sent()}"
            )
        else:
            clock_ok, using_serial_proxy = ensure_transport_clock(
                in_port,
                out_port,
                serial_collector,
                ns,
                min_clocks=24,
                timeout_s=3.0,
                abort=abort,
            )
            if not clock_ok:
                print("[long-loop-display-hitl] MIDI clock missing before record stream")
                return 1
            wall_clock_tempo = resolve_wall_tempo_for_proxy(
                serial_collector, ns, using_serial_proxy=using_serial_proxy
            )

        # The guard and the stop lead are bar-based; a host clock at a stated BPM must not keep the device-clock 2 s.
        seconds_per_bar = _seconds_per_bar(ns.clock_source, ns.tempo_bpm)
        guard = max(30.0, ns.record_bars * seconds_per_bar * 2.5)
        # Host mode presses the stop from inside the stream, one lead ahead of the last bar: the post-stream path
        # waits 0.5 s for the transition baseline and the device acts on a short press ~0.3 s later, which at a slow
        # tempo is more than the bar's half-bar quantum (see HOST_STOP_LEAD_SECONDS). The lead is in clocks, so it
        # follows the tempo the host sends.
        host_stop_lead_clocks = 0
        if clock_master is not None:
            host_stop_lead_clocks = _host_stop_lead_clocks(ns.tempo_bpm)
        rec_notes, _rec_cc, rec_clock_count, _timing = _stream_pattern_for_bars(
            out_port,
            in_port,
            midi_channel_1based=midi_channel,
            low_note=36,
            high_note=51,
            step_clocks=RECORD_GRID_STEP_CLOCKS,
            gate_clocks=RECORD_GRID_STEP_CLOCKS,
            target_bars=ns.record_bars,
            cc_number=74,
            cc_step=9,
            pitch_cycle_bars=2,
            phase_start_delay_bars=0,
            phase_start_delay_beats=0,
            max_seconds_guard=guard,
            fixed_note=None,
            # Device clock: the leg presses the stop itself after the stream. Host clock: the stream presses it a
            # lead early, because the leg's own press comes ~0.9 s late (see HOST_STOP_LEAD_SECONDS).
            stop_press_advance_clocks=(
                ns.stop_press_advance_clocks if clock_master is None else host_stop_lead_clocks
            ),
            stop_press_note=None if clock_master is None else RECORD_BUTTON_NOTE,
            stop_press_channel_1based=CONTROL_CHANNEL_1BASED,
            stop_press_press_ms=ns.press_ms,
            abort=abort,
            emit_immediate_first_step=True,
            # Exactly one pacing source: the pump in host mode (None here), the wall-clock proxy otherwise. Passing
            # both raises inside the streamer by design.
            master=clock_master,
            wall_clock_tempo_bpm=wall_clock_tempo,
            # Only meaningful with the host pump: the take's own grid origin, so the bar budget is measured from
            # tick 0 instead of from when this process got around to streaming.
            grid_origin_pulse=record_grid_origin_pulse,
        )
        print(
            f"[long-loop-display-hitl] record stream notes={rec_notes} clocks={rec_clock_count}"
        )
        if clock_master is not None:
            print(
                "[long-loop-display-hitl] host clock anchor: "
                f"source={_timing.get('clock_source')} "
                f"missed_pulses={_timing.get('grid_anchor_missed_pulses')} "
                f"first_note_tick={_timing.get('grid_first_note_tick')}"
            )
        if rec_notes == 0 or rec_clock_count == 0:
            print(
                "[long-loop-display-hitl] error: no MIDI notes/clocks streamed during record "
                "(transport or RECORDING sync failed)"
            )
            return 1

        sync_timeout_s = _effective_sync_timeout_s(ns.record_bars, ns.state_sync_timeout_ms)
        time.sleep(0.5)

        expected_play = None
        stop_baseline_len = 0
        # Host mode may have pressed the stop from inside the stream (a lead early, to keep the commit inside the
        # last bar). That press's transition is already in the snapshot, so the expected count does not advance again
        # and no second press is sent -- a second press would arm a *new* take.
        stop_press_sent_during_stream = bool(
            clock_master is not None and _timing.get("stop_press_sent_during_stream")
        )
        if serial_collector is not None:
            from hitl.capture_transitions import _count_capture_transitions

            counts = _count_capture_transitions(serial_collector.snapshot())
            expected_play = counts.get(("STOPPED_RECORDING", "PLAYING"), 0) + (
                0 if stop_press_sent_during_stream else 1
            )
            stop_baseline_len = len(serial_collector.snapshot())

        if stop_press_sent_during_stream:
            print("[long-loop-display-hitl] record stop was sent from inside the stream (host clock lead)")
        else:
            print("[long-loop-display-hitl] record stop (returns to play)")
            _send_short_press(
                out_port,
                note=RECORD_BUTTON_NOTE,
                channel_1based=CONTROL_CHANNEL_1BASED,
                press_ms=ns.press_ms,
            )

        if serial_collector is not None and expected_play is not None:
            reached_play = _wait_for_transition_count(
                serial_collector,
                from_state="STOPPED_RECORDING",
                to_state="PLAYING",
                target_count=expected_play,
                timeout_s=sync_timeout_s,
                abort=abort,
            )
            if not reached_play:
                # The stop press may have landed while the log still showed the take unfinished. Decide from the
                # device's own latest state: only a take that is **still recording** gets a second press.
                lagging = _latest_track_state(serial_collector.snapshot(), after_index=stop_baseline_len)
                if not record_stop_needs_retry(lagging):
                    print(
                        "[long-loop-display-hitl] record stop confirmed late (the capture log lagged the press); "
                        "not pressing again"
                    )
                    reached_play = True
                else:
                    print(
                        "[long-loop-display-hitl] warn: STOPPED_RECORDING->PLAYING not confirmed; "
                        "retrying record stop press"
                    )
                    _send_short_press(
                        out_port,
                        note=RECORD_BUTTON_NOTE,
                        channel_1based=CONTROL_CHANNEL_1BASED,
                        press_ms=ns.press_ms,
                    )
                    reached_play = _wait_for_transition_count(
                        serial_collector,
                        from_state="STOPPED_RECORDING",
                        to_state="PLAYING",
                        target_count=expected_play,
                        timeout_s=sync_timeout_s,
                        abort=abort,
                    )
            if not reached_play:
                print("[long-loop-display-hitl] warn: did not confirm STOPPED_RECORDING->PLAYING")

            if ns.record_bars >= 48:
                from hitl.deferred_save_idle import wait_for_deferred_save_idle

                print(
                    f"[long-loop-display-hitl] waiting for deferred save idle "
                    f"(timeout {sync_timeout_s:.0f}s)"
                )
                if not wait_for_deferred_save_idle(
                    serial_collector,
                    after_line_index=stop_baseline_len,
                    timeout_s=sync_timeout_s,
                    log_prefix="[long-loop-display-hitl]",
                ):
                    print("[long-loop-display-hitl] warn: deferred save idle not confirmed")

            playing_deadline = time.monotonic() + sync_timeout_s
            while time.monotonic() < playing_deadline:
                if _latest_track_state(serial_collector.snapshot()) == "PLAYING":
                    break
                time.sleep(0.05)
            else:
                print("[long-loop-display-hitl] warn: latest track state is not PLAYING before NOTE_EDIT")

            from hitl.scenarios.load_save_overlay_helpers import recover_load_save_overlay
            from hitl.edit_controls import _send_double_press

            recover_load_save_overlay(
                out_port,
                serial_collector,
                press_ms=ns.press_ms,
                gap_ms=80,
                gesture_settle_ms=600,
                send_double_press=_send_double_press,
                log_prefix="[long-loop-display-hitl]",
            )

            time.sleep(2.0)

        time.sleep(ns.phase_wait_ms / 1000.0)

        print("[long-loop-display-hitl] enter NOTE_EDIT while playing")
        if serial_collector is not None:
            if not _ensure_note_edit_entered(
                out_port,
                serial_collector,
                press_ms=ns.press_ms,
                phase_wait_ms=ns.phase_wait_ms,
                timeout_s=sync_timeout_s,
            ):
                lines = serial_collector.snapshot()
                out_dir = Path(getattr(args, "out_dir", Path("captures")))
                out_dir.mkdir(parents=True, exist_ok=True)
                stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
                fail_path = out_dir / f"host_midi_hitl_long_loop_display_fail_{stamp}.log"
                fail_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
                edit_tail = [
                    line
                    for line in lines[-200:]
                    if any(
                        token in line
                        for token in (
                            "Edit session",
                            "edit mode",
                            "Edit Mode",
                            "EditSession",
                            "LDSV",
                            "NOTE_EDIT",
                        )
                    )
                ]
                print(f"[long-loop-display-hitl] serial fail log: {fail_path}")
                if edit_tail:
                    print("[long-loop-display-hitl] recent edit-related serial:")
                    for line in edit_tail[-15:]:
                        print(f"  {line}")
                print("[long-loop-display-hitl] error: NOTE_EDIT enter not confirmed")
                return 1
        else:
            _send_short_press(
                out_port,
                note=EDIT_BUTTON_NOTE,
                channel_1based=CONTROL_CHANNEL_1BASED,
                press_ms=ns.press_ms,
            )
            time.sleep(ns.phase_wait_ms / 1000.0)
        ctx.markers.append("entered note edit mode")
        time.sleep(ns.phase_wait_ms / 1000.0)

        print(f"[long-loop-display-hitl] freeze wait {ns.freeze_wait_ms}ms (window should stay fixed)")
        time.sleep(ns.freeze_wait_ms / 1000.0)
        ctx.markers.append("phase:freeze_wait_end")

        from hitl.control_constants import DISPLAY_SETTLE_MS

        print(
            f"[long-loop-display-hitl] play/stop long press ({ns.play_stop_long_press_ms}ms) "
            "— snap window to playhead"
        )
        _send_long_press(
            out_port,
            note=PLAY_STOP_BUTTON_NOTE,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=ns.play_stop_long_press_ms,
        )
        ctx.markers.append("phase:long_press_snap")
        time.sleep(max(DISPLAY_SETTLE_MS, ns.phase_wait_ms) / 1000.0)

        print(f"[long-loop-display-hitl] play/stop hold {ns.hold_track_ms}ms (window should track)")
        ch = CONTROL_CHANNEL_1BASED - 1
        out_port.send(
            mido.Message("note_on", channel=ch, note=PLAY_STOP_BUTTON_NOTE, velocity=127)
        )
        ctx.markers.append("phase:hold_track_start")
        time.sleep(ns.hold_track_ms / 1000.0)
        out_port.send(
            mido.Message("note_off", channel=ch, note=PLAY_STOP_BUTTON_NOTE, velocity=0)
        )
        ctx.markers.append("phase:hold_track_end")
        time.sleep(max(DISPLAY_SETTLE_MS, ns.phase_wait_ms) / 1000.0)

        print("[long-loop-display-hitl] exit NOTE_EDIT (short press cycles to LOOP_EDIT)")
        if serial_collector is not None:
            exit_confirmed = _ensure_note_edit_exited(
                out_port,
                serial_collector,
                press_ms=ns.press_ms,
                timeout_s=sync_timeout_s,
            )
            if not exit_confirmed:
                lines = serial_collector.snapshot()
                out_dir = Path(getattr(args, "out_dir", Path("captures")))
                out_dir.mkdir(parents=True, exist_ok=True)
                stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
                fail_path = out_dir / f"host_midi_hitl_long_loop_display_fail_{stamp}.log"
                fail_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
                print(f"[long-loop-display-hitl] serial fail log: {fail_path}")
                print("[long-loop-display-hitl] error: NOTE_EDIT exit not confirmed; the leg cannot leave the overlay")
                return 1
        else:
            _send_short_press(
                out_port,
                note=EDIT_BUTTON_NOTE,
                channel_1based=CONTROL_CHANNEL_1BASED,
                press_ms=ns.press_ms,
            )
        ctx.markers.append("phase:note_edit_exited")

        if ns.display_hold_s > 0:
            print(
                f"[long-loop-display-hitl] display hold {ns.display_hold_s:.1f}s with the transport running "
                "(the lane's adoption boundary must fall inside it)"
            )
            ctx.markers.append("phase:display_hold_start")
            time.sleep(ns.display_hold_s)
            ctx.markers.append("phase:display_hold_end")

        if serial_collector is not None:
            setattr(args, "phase_markers", list(ctx.markers))
            setattr(args, "hold_track_ms", ns.hold_track_ms)
            setattr(args, "hitl_context", ctx)
            if ns.serial_grace_ms > 0:
                time.sleep(ns.serial_grace_ms / 1000.0)
            lines = serial_collector.snapshot()
            out_dir = Path(getattr(args, "out_dir", Path("captures")))
            out_dir.mkdir(parents=True, exist_ok=True)
            stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            serial_path = out_dir / f"host_midi_hitl_long_loop_display_serial_{stamp}.log"
            serial_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
            print(f"[long-loop-display-hitl] serial log: {serial_path}")
            check = verify_long_loop_display_window(lines, args)
            print(f"[long-loop-display-hitl] serial verification ok={check.get('ok')}")
            for issue in check.get("issues", []):
                print(f"  issue: {issue}")
            for warning in check.get("warnings", []):
                print(f"  warn: {warning}")

            out_dir = Path(getattr(args, "out_dir", Path("captures")))
            out_dir.mkdir(parents=True, exist_ok=True)
            stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            report_path = out_dir / f"host_midi_hitl_long_loop_display_window_{stamp}.json"
            report_path.write_text(
                json.dumps(
                    {
                        "scenario": "long_loop_display_window",
                        "track_number": ns.track_number,
                        "record_bars": ns.record_bars,
                        "clock_source": ns.clock_source,
                        "tempo_bpm": ns.tempo_bpm,
                        "freeze_wait_ms": ns.freeze_wait_ms,
                        "hold_track_ms": ns.hold_track_ms,
                        "display_hold_s": ns.display_hold_s,
                        "adoption_hold_s": ns.adoption_hold_s,
                        "serial_verification": check,
                        "markers": ctx.markers,
                    },
                    indent=2,
                )
            )
            print(f"[long-loop-display-hitl] report: {report_path}")
            if not check.get("ok", False):
                exit_code = 2
        else:
            print("[long-loop-display-hitl] warn: no --serial-port; skipping verification")
            exit_code = 1

        return exit_code
    finally:
        if serial_collector is not None:
            serial_collector.stop()
        if clock_master is not None:
            # Retire the pump only -- never `Stop`. The device's slaving is a 500 ms timeout, not a latch, so it
            # falls back to its own clock and keeps playing; the wrapper's switch leg needs the transport running
            # (it queues its switch on the selection itself), and a realtime Stop here would end the take the run
            # just committed.
            clock_master.stop_pump()
        out_port.close()
        in_port.close()
        from hitl.midi_io import _drain_input_messages

        _drain_input_messages(in_port)

def verify_long_loop_display_window(lines: list[str], args: object) -> dict[str, object]:
    from hitl.verify.display_window import verify_long_loop_display_window as _verify

    return _verify(lines, args)
