"""Host-side MIDI clock orchestration for HITL scenarios."""

from __future__ import annotations

import argparse
import collections
import inspect
import time
from typing import Any, Optional

try:
    import mido
except ImportError as exc:  # pragma: no cover
    raise SystemExit(
        "Missing dependency 'mido'. Install with:\n"
        "  python3 -m pip install mido python-rtmidi pyserial"
    ) from exc

from hitl.serial_transport import (
    resolve_wall_tempo_bpm,
    serial_global_transport_running,
    use_serial_transport_proxy,
)
from hitl.control_constants import (
    CONTROL_CHANNEL_1BASED,
    GLOBAL_TRANSPORT_NOTE,
    MIDI_CLOCKS_PER_BAR,
    MIDI_CLOCKS_PER_BEAT,
)
from hitl.midi_clock import MidiClockMaster, MidiClockReader


# Tempo a host-driven phase runs at when the caller pins none.
DEFAULT_MASTER_BPM = 120.0


# Every outgoing control action, with the function that sent it. An unexpected transport
# press is otherwise only inferable from its effect on the device
# (`captures/s4f22-step7-d2`: start 53.527, recording 53.974, an unexplained transport press
# 54.526 that stopped it, and the intended Record stop at 59.248 arming a new take).
_CONTROL_ACTIONS: "collections.deque[dict[str, object]]" = collections.deque(maxlen=128)


def record_control_action(action: str, *, note: int, channel_1based: int, caller: str) -> None:
    _CONTROL_ACTIONS.append(
        {
            "action": action,
            "note": int(note),
            "channel_1based": int(channel_1based),
            "caller": caller,
            "monotonic_s": round(time.monotonic(), 6),
        }
    )


def control_action_log() -> list[dict[str, object]]:
    """Outgoing control actions, oldest first, for run attribution."""
    return list(_CONTROL_ACTIONS)


def clear_control_action_log() -> None:
    _CONTROL_ACTIONS.clear()


def _transport_actions() -> list[dict[str, object]]:
    return [a for a in _CONTROL_ACTIONS if a.get("note") == GLOBAL_TRANSPORT_NOTE]


def transport_press_count() -> int:
    """How many global-transport presses this process has sent (all callers)."""
    return len(_transport_actions())


def _send_short_press(
    out_port: mido.ports.BaseOutput, *, note: int, channel_1based: int, press_ms: int,
    action: str = "short_press",
) -> None:
    record_control_action(
        action, note=note, channel_1based=channel_1based, caller=inspect.stack()[1].function
    )
    ch = channel_1based - 1
    out_port.send(mido.Message("note_on", channel=ch, note=note, velocity=127))
    time.sleep(max(press_ms, 1) / 1000.0)
    out_port.send(mido.Message("note_off", channel=ch, note=note, velocity=0))


# The reader that currently owns the input port (device clock source). Every guard below asks
# "is a clock flowing?" by probing `in_port`, and a drain-owning reader has already taken those
# pulses, so the guards must ask the reader instead. Their failure mode is not a warning: the
# confirmation path presses the global transport, which is a *toggle*, so a guard that wrongly
# reports "no clock" stops the run's transport (DEC-097). Measured: with the reader pacing the
# record phase and the guards still probing the port, the transport was pressed off ~11 s after
# the record stop, the overdub was never entered (`ODUB: 0`) and the preset ended
# `INCONCLUSIVE_CAPTURE` (`captures/session_20260930_122053.log`).
_ACTIVE_CLOCK_READER: Optional[MidiClockReader] = None


def set_active_clock_reader(reader: Optional[MidiClockReader]) -> None:
    """Declare which reader (if any) owns the input port for the current run."""
    global _ACTIVE_CLOCK_READER
    _ACTIVE_CLOCK_READER = reader


def _active_reader_sees_clock() -> bool:
    """True when the port-owning reader is receiving the device clock *right now*.

    Recency, not stickiness: the device stops emitting clock when its transport stops, and the
    callers below use "is a clock flowing?" as their evidence about the transport. A sticky check
    kept answering True after the record-arm step had stopped the transport, which suppressed the
    transport restart the record leg needs (`record on/off 0/0`, a 1-bar take) while the churn it
    was meant to stop stayed fixed -- so it has to be a window, not a history.
    """
    reader = _ACTIVE_CLOCK_READER
    if reader is None:
        return False
    reader.poll()
    return reader.recent_pulse()


def clock_seen_within(in_port: mido.ports.BaseInput, timeout_s: float) -> bool:
    if _active_reader_sees_clock():
        return True
    deadline = time.monotonic() + max(timeout_s, 0.0)
    while time.monotonic() < deadline:
        msg = in_port.poll()
        if msg is None:
            time.sleep(0.001)
            continue
        if msg.type == "clock":
            return True
    return False


def wait_for_clock_pulses(
    in_port: mido.ports.BaseInput,
    pulses: int,
    *,
    timeout_s: float,
    abort: Optional[Any] = None,
    wall_clock_tempo_bpm: Optional[float] = None,
) -> int:
    if pulses <= 0:
        return 0
    # The port-owning reader is the only observer of that port: count through it, or this guard
    # reports "0 midi clock pulses" for a phase that is being paced by the device clock.
    reader = _ACTIVE_CLOCK_READER
    if reader is not None and (wall_clock_tempo_bpm is None or wall_clock_tempo_bpm <= 0):
        reader.poll()
        start = reader.observed_pulses()
        deadline = time.monotonic() + max(timeout_s, 0.0)
        seen = 0
        while seen < pulses:
            if abort is not None and abort.check() is not None:
                break
            if time.monotonic() >= deadline:
                break
            reader.poll()
            seen = reader.observed_pulses() - start
            if seen >= pulses:
                break
            time.sleep(0.0005)
        return min(seen, pulses)
    if wall_clock_tempo_bpm is not None and wall_clock_tempo_bpm > 0:
        seconds_per_clock = (60.0 / wall_clock_tempo_bpm) / 24.0
        deadline = time.monotonic() + max(timeout_s, 0.0)
        seen = 0
        while seen < pulses:
            if abort is not None and abort.check() is not None:
                break
            if time.monotonic() >= deadline:
                break
            time.sleep(seconds_per_clock)
            seen += 1
        return seen

    deadline = time.monotonic() + max(timeout_s, 0.0)
    seen = 0
    while seen < pulses:
        if abort is not None and abort.check() is not None:
            break
        now = time.monotonic()
        if now >= deadline:
            break
        msg = in_port.poll()
        if msg is None:
            time.sleep(0.0005)
            continue
        if msg.type == "clock":
            seen += 1
    return seen


def ensure_midi_clock(
    in_port: mido.ports.BaseInput,
    out_port: mido.ports.BaseOutput,
    *,
    min_clocks: int,
    timeout_s: float,
    abort: Optional[Any] = None,
    allow_transport_press: bool = False,
) -> bool:
    """Wait for incoming MIDI clock; toggle transport once only if explicitly allowed.

    The toggle is a **toggle**: sent while the transport already runs it stops it. The device
    emits clock only while its own transport runs (`ClockManager::updateInternalClock`), so a
    wait made *before* any transport was started always expires and a press always fires -- so
    any caller that is *confirming* transport it already started must not allow it. Those
    callers get a named failure instead (`ensure_transport_clock`).

    The device clock is real and usable (`hitl/midi_clock.py`); the failure mode above is about
    *when* the probe waits, not about the clock being absent.
    """
    if min_clocks <= 0:
        return True
    # The port-owning reader is proof the clock flows, so the wait cannot expire and the toggle
    # press below must not fire: pressing the transport while it already runs *stops* it.
    if _active_reader_sees_clock():
        return True
    # Without a reader, count through the port; the reader's pulses are not visible here.
    def count_clocks(deadline: float) -> int:
        seen = 0
        while time.monotonic() < deadline:
            if abort is not None and abort.check() is not None:
                break
            msg = in_port.poll()
            if msg is None:
                time.sleep(0.0005)
                continue
            if msg.type == "clock":
                seen += 1
                if seen >= min_clocks:
                    break
        return seen

    deadline = time.monotonic() + max(timeout_s, 0.0)
    if count_clocks(deadline) >= min_clocks:
        return True

    if not allow_transport_press:
        return False

    _send_short_press(
        out_port,
        note=GLOBAL_TRANSPORT_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=120,
        action="midi_clock_toggle",
    )
    time.sleep(0.3)
    deadline = time.monotonic() + max(timeout_s, 0.0)
    return count_clocks(deadline) >= min_clocks


def ensure_transport_clock(
    in_port: mido.ports.BaseInput,
    out_port: mido.ports.BaseOutput,
    serial_collector: Any | None,
    args: argparse.Namespace,
    *,
    min_clocks: int,
    timeout_s: float,
    abort: Optional[Any] = None,
    after_index: int = 0,
    started_since_index: int | None = None,
    allow_transport_press: bool = False,
) -> tuple[bool, bool]:
    """Return (ok, using_serial_proxy). Proxy = serial capture shows transport without USB clock in.

    ``started_since_index`` is for the callers that invoke this **to confirm a transport start
    press they have just made**: pass the serial length captured *before* that press. The press
    inside ``ensure_midi_clock`` is a toggle, and a wait made before any transport was started
    sees no clock (`ClockManager::updateInternalClock` emits it only while running), so its
    pulse wait expires and the press fires -- pressing while the transport already
    runs *stops* it. Measured on device (``captures/s4f22-base-device-03``): a start at 25.086 was
    followed by a toggle press at 25.603 that stopped the transport, which the device correctly
    resolved as a record stop to STOPPED ("record stop did not reach PLAYING", one-bar take).

    Scoping the receipt to the caller's own press is what separates "my start was acknowledged"
    from "a start is still needed": a stale ``Transport started`` from earlier in the run cannot
    suppress the press (that over-correction left the device unstarted for a whole run), while the
    press's own acknowledgement can. Callers that are not confirming a press leave this ``None``
    and keep the previous behaviour.
    """
    if use_serial_transport_proxy(args, serial_collector, after_index=after_index):
        return True, True

    def _start_acknowledged() -> bool:
        if started_since_index is None or serial_collector is None:
            return False
        return serial_global_transport_running(
            serial_collector.snapshot(), after_index=started_since_index
        )

    if _start_acknowledged():
        return True, True
    if started_since_index is not None:
        ack_deadline = time.monotonic() + min(max(timeout_s, 0.0), 2.0)
        while time.monotonic() < ack_deadline:
            if abort is not None and abort.check() is not None:
                return False, False
            if use_serial_transport_proxy(args, serial_collector, after_index=after_index):
                return True, True
            if _start_acknowledged():
                return True, True
            time.sleep(0.05)

    if started_since_index is not None:
        # The caller has already sent Start and is only confirming it. `ensure_midi_clock`'s press
        # is a toggle, so a late acknowledgement would stop the transport that start just began --
        # measured twice on device (`captures/s4f22-base-device-03`: start 25.086, toggle 25.603;
        # `captures/s4f22-step7-d2`: start 53.527, recording 53.974, unexplained press 54.526).
        # Missing clocks or a delayed receipt is a named failure for the caller, never a re-press.
        return False, False

    if not allow_transport_press:
        return False, False

    ok = ensure_midi_clock(
        in_port,
        out_port,
        min_clocks=min_clocks,
        timeout_s=timeout_s,
        abort=abort,
        allow_transport_press=True,
    )
    return ok, False


def stream_pattern_for_bars(
    out_port: mido.ports.BaseOutput,
    in_port: mido.ports.BaseInput,
    *,
    midi_channel_1based: int,
    low_note: int,
    high_note: int,
    step_clocks: int,
    target_bars: int,
    cc_number: int,
    cc_step: int,
    pitch_cycle_bars: int,
    phase_start_delay_bars: int,
    phase_start_delay_beats: int,
    max_seconds_guard: float,
    fixed_note: Optional[int] = None,
    stop_press_advance_clocks: int = 0,
    stop_press_note: Optional[int] = None,
    stop_press_channel_1based: int = CONTROL_CHANNEL_1BASED,
    stop_press_press_ms: int = 85,
    clock_start_timeout_seconds: float = 0.75,
    abort: Optional[Any] = None,
    emit_immediate_first_step: bool = False,
    gate_clocks: Optional[int] = None,
    wall_clock_tempo_bpm: Optional[float] = None,
    stream_start_monotonic: Optional[float] = None,
    device_bar_probe: Optional[Callable[[], Optional[int]]] = None,
    device_bar_baseline: Optional[int] = None,
    clock: Optional[MidiClockReader] = None,
    master: Optional[MidiClockMaster] = None,
) -> tuple[int, int, int, dict[str, Any]]:
    """Send note pattern until target bar count, paced by one of four clock sources.

    ``device_bar_probe`` returns the device's own latest ``BAR,<tick>,<index>`` index, and
    is the authoritative end condition when it is available: pacing by host clock messages
    or by wall-clock seconds both over-ran on device (11-34 bars for an 8-bar request),
    while the device's own bar counter is exactly what the loop length is quantized from.
    The stream ends once the device index has advanced by ``target_bars`` from its value at
    stream start.

    Pacing is chosen by exactly one of ``clock``/``master``/``wall_clock_tempo_bpm``:

    * ``clock`` -- the device is the clock master. The phase origin is the reader's current
      grid position and the *first* grid step is emitted on the very next pulse
      (``clock.wait_pulse(origin + 1)``); step ``k`` belongs to grid position
      ``origin + 1 + (k - 1) * step_clocks``. The phase ends when the reader reaches
      ``origin + 1 + phase_delay_clocks + target_bars * MIDI_CLOCKS_PER_BAR`` grid positions --
      counted in grid positions, never in wall-clock seconds -- or at ``max_seconds_guard``.
      The reader's index is phase-locked, so a dropped pulse does not stall the phase the way
      the raw ``in_port`` count below does.
    * ``master`` -- the host is the clock master. Pulse ``i`` of the phase is grid position
      ``origin + i`` and is sent by ``master``: the first pulse *is* the downbeat and the first
      step is emitted with it, so step ``k`` is emitted with pulse
      ``1 + (k - 1) * step_clocks``. The phase ends after
      ``phase_delay_clocks + target_bars * MIDI_CLOCKS_PER_BAR`` pulses have been sent. The
      tempo is ``wall_clock_tempo_bpm`` (``DEFAULT_MASTER_BPM`` when unset). No realtime
      Start/Stop is sent here: the caller owns the transport.
    * ``wall_clock_tempo_bpm`` set -- unchanged: pace to absolute wall-clock deadlines.
    * none of them -- unchanged: count raw ``in_port`` clock messages.

    ``in_port`` is not polled for pacing in the ``clock``/``master`` modes (in ``clock`` mode
    the reader owns the port).
    """
    if high_note < low_note:
        raise ValueError("high_note must be >= low_note")
    if step_clocks <= 0:
        raise ValueError("step_clocks must be > 0")
    note_gate_clocks = step_clocks if gate_clocks is None else gate_clocks
    if note_gate_clocks <= 0:
        raise ValueError("gate_clocks must be > 0")
    if pitch_cycle_bars <= 0:
        raise ValueError("pitch_cycle_bars must be > 0")
    if phase_start_delay_bars < 0:
        raise ValueError("phase_start_delay_bars must be >= 0")
    if phase_start_delay_beats < 0:
        raise ValueError("phase_start_delay_beats must be >= 0")

    ch = midi_channel_1based - 1
    note_range = list(range(low_note, high_note + 1))
    note_index = 0
    note_on_count = 0
    cc_count = 0
    clock_count_total = 0
    phase_clock_count = 0
    cc_val = 0
    target_clocks = target_bars * MIDI_CLOCKS_PER_BAR
    phase_delay_clocks = (phase_start_delay_bars * MIDI_CLOCKS_PER_BAR) + (
        phase_start_delay_beats * MIDI_CLOCKS_PER_BEAT
    )
    pitch_cycle_clocks = pitch_cycle_bars * MIDI_CLOCKS_PER_BAR
    if clock is not None and master is not None:
        raise ValueError("clock and master are mutually exclusive pacing sources")

    def select_grid_note() -> Optional[int]:
        if fixed_note is not None:
            return fixed_note
        return note_range[note_index % len(note_range)]

    while True:
        msg = in_port.poll()
        if msg is None:
            break
        if msg.type in ("start", "stop", "continue", "clock"):
            continue
    stop_press_trigger_clock: Optional[int] = None
    if stop_press_note is not None and stop_press_advance_clocks < 0:
        stop_press_trigger_clock = target_clocks
    elif stop_press_note is not None and stop_press_advance_clocks > 0:
        stop_press_trigger_clock = max(1, target_clocks - stop_press_advance_clocks)
    stop_press_sent_during_stream = False
    stop_press_noteoff_due_at: Optional[float] = None
    next_step_clock = step_clocks
    immediate_step_emitted = False
    held_notes: list[tuple[int, int]] = []
    start = stream_start_monotonic if stream_start_monotonic is not None else time.monotonic()
    jitter_samples: list[int] = []
    seconds_per_clock: Optional[float] = None
    if wall_clock_tempo_bpm is not None and wall_clock_tempo_bpm > 0:
        seconds_per_clock = (60.0 / wall_clock_tempo_bpm) / 24.0

    # ---- pacing mode ------------------------------------------------------------
    # Exactly one source paces the phase. The two grid modes below are the ones that time the
    # phase from a clock rather than from the host's own wall clock.
    clock_source = "received_clock"
    clock_origin: Optional[float] = None
    clock_pulse_timeout_s = 0.5
    master_origin: Optional[float] = None
    master_pulses_sent = 0
    master_bpm = wall_clock_tempo_bpm if (wall_clock_tempo_bpm or 0.0) > 0 else DEFAULT_MASTER_BPM
    if clock is not None:
        clock_source = "device_clock"
        # The phase origin is the reader's **current grid position**, and the grid positions are
        # integers (one per pulse), so `origin + 1` is the very next pulse. Anchoring on a
        # fractional `index_now()` would land the first step one pulse late whenever the phase
        # started mid-interval, and anchoring on the phase *start* (what this replaces) put the
        # first note ~15 clocks after the device had already begun recording.
        #
        # Drain FIRST: nothing polls the reader between the transport start and this call, so a
        # backlog (~250 pulses) is queued. Draining it inside the first `wait_pulse` made the
        # phase-locked index jump far past every step target, so the streamer emitted a whole
        # phase in milliseconds and the device captured nothing
        # (`captures/...-121454`, `-121720`: `record_first_note_missing`, a 1-bar take).
        clock.poll()
        clock_origin = float(int(clock.index_now()))
        # The reader's index keeps moving with time, so a dropped pulse needs no extra budget;
        # this only bounds a clock that stopped for good.
        clock_pulse_timeout_s = max(0.5, 4.0 * (clock.period() or 0.0))
    elif master is not None:
        clock_source = "host_clock"
    # The baseline must be the device bar index at the *phase* start (the record or overdub
    # boundary), not at the note stream's first clock: the device begins counting before the
    # host streams, so a stream-relative baseline makes an 8-bar request record 10 bars.
    device_bar_start: Optional[int] = device_bar_baseline
    device_bar_latest: Optional[int] = None
    device_bar_relative_bars = 0
    if device_bar_start is None and device_bar_probe is not None:
        device_bar_start = device_bar_probe()

    while phase_clock_count < target_clocks:
        if abort is not None and abort.check() is not None:
            break
        if stop_press_noteoff_due_at is not None and time.monotonic() >= stop_press_noteoff_due_at:
            out_port.send(
                mido.Message(
                    "note_off",
                    channel=stop_press_channel_1based - 1,
                    note=stop_press_note,
                    velocity=0,
                )
            )
            stop_press_noteoff_due_at = None
        elapsed = time.monotonic() - start
        if elapsed > max_seconds_guard:
            break
        if seconds_per_clock is None and clock_count_total == 0 and elapsed >= clock_start_timeout_seconds:
            break

        if clock is not None:
            # Device-paced: wait for the next grid position of the phase. `wait_pulse` is the
            # phase-length primitive, and the reader's index is phase-locked, so a dropout does
            # not stall the phase the way the raw `in_port` count below does (that count is the
            # 2026-09-17 experiment whose dropped pulses let an 8-bar request run 20 bars).
            if not clock.wait_pulse(
                clock_origin + clock_count_total + 1,
                timeout_s=clock_pulse_timeout_s,
                abort=abort,
            ):
                break
        elif master is not None:
            # Host-paced: pulse `clock_count_total + 1` of the phase is grid position
            # `clock_origin + clock_count_total + 1`. One pulse per grid position, all of them on
            # one absolute schedule anchored at the phase origin (`deadline_origin` +
            # `deadline_index`), so the per-call send overhead cannot accumulate into drift.
            if master_origin is None:
                master_origin = time.perf_counter()
            master.send_pulses(
                1,
                bpm=master_bpm,
                abort=abort,
                deadline_origin=master_origin,
                deadline_index=master_pulses_sent,
            )
            master_pulses_sent += 1
        elif seconds_per_clock is not None:
            # Pace to an absolute deadline. `sleep(seconds_per_clock)` per step adds the
            # per-iteration MIDI send and loop overhead on top of the interval, so a
            # 16 s (8-bar) stream ran ~20 s and the device recorded 10 bars. Deadline
            # pacing keeps the total at `target_clocks * seconds_per_clock`.
            deadline = start + (clock_count_total + 1) * seconds_per_clock
            remaining = deadline - time.monotonic()
            if remaining > 0:
                time.sleep(remaining)
        else:
            msg = in_port.poll()
            if msg is None:
                time.sleep(0.0005)
                continue
            if msg.type != "clock":
                continue

        clock_count_total += 1

        if clock_count_total <= phase_delay_clocks:
            continue

        phase_clock_count += 1

        still_held: list[tuple[int, int]] = []
        for note, off_at in held_notes:
            if phase_clock_count >= off_at:
                out_port.send(mido.Message("note_off", channel=ch, note=note, velocity=0))
            else:
                still_held.append((note, off_at))
        held_notes = still_held

        if emit_immediate_first_step and not immediate_step_emitted:
            # First grid step now — advance next_step_clock so this does not also
            # fire again at step_clocks (that double-count made 2-bar record 33
            # notes: immediate 48 + full 6..192 grid ending in another 48).
            note = select_grid_note()
            if note is not None:
                out_port.send(mido.Message("note_on", channel=ch, note=note, velocity=98))
                note_on_count += 1
                out_port.send(mido.Message("control_change", channel=ch, control=cc_number, value=cc_val))
                cc_count += 1
                gate = note_gate_clocks
                held_notes.append((note, phase_clock_count + gate))
                note_index += 1
                cc_val = (cc_val + cc_step) % 128
            immediate_step_emitted = True
            next_step_clock = phase_clock_count + step_clocks

        if phase_clock_count >= next_step_clock:
            note = select_grid_note()
            if note is not None:
                jitter_samples.append(phase_clock_count - next_step_clock)
                out_port.send(mido.Message("note_on", channel=ch, note=note, velocity=98))
                note_on_count += 1
                out_port.send(mido.Message("control_change", channel=ch, control=cc_number, value=cc_val))
                cc_count += 1
                gate = note_gate_clocks
                held_notes.append((note, phase_clock_count + gate))
                note_index += 1
                cc_val = (cc_val + cc_step) % 128
            next_step_clock += step_clocks
            if fixed_note is None and (phase_clock_count % pitch_cycle_clocks) == 0:
                note_index = 0

        if device_bar_probe is not None and (phase_clock_count % MIDI_CLOCKS_PER_BEAT) == 0:
            probe = device_bar_probe()
            if probe is not None:
                # The baseline is captured lazily: at stream start the capture tail may not
                # hold a BAR row yet, and a None baseline would disable the end condition
                # for the whole stream (the 8-bar request then ran 10 bars).
                if device_bar_start is None:
                    device_bar_start = probe
                device_bar_latest = probe
                device_bar_relative_bars = probe - device_bar_start
                if device_bar_relative_bars >= target_bars:
                    break

        if (
            stop_press_note is not None
            and stop_press_trigger_clock is not None
            and not stop_press_sent_during_stream
            and phase_clock_count >= stop_press_trigger_clock
        ):
            out_port.send(
                mido.Message(
                    "note_on",
                    channel=stop_press_channel_1based - 1,
                    note=stop_press_note,
                    velocity=127,
                )
            )
            stop_press_noteoff_due_at = time.monotonic() + max(stop_press_press_ms, 1) / 1000.0
            stop_press_sent_during_stream = True

    for note, _off_at in held_notes:
        out_port.send(mido.Message("note_off", channel=ch, note=note, velocity=0))
    if stop_press_noteoff_due_at is not None:
        out_port.send(
            mido.Message(
                "note_off",
                channel=stop_press_channel_1based - 1,
                note=stop_press_note,
                velocity=0,
            )
        )

    if jitter_samples:
        max_abs = max(abs(x) for x in jitter_samples)
        mean_abs = sum(abs(x) for x in jitter_samples) / len(jitter_samples)
    else:
        max_abs = 0
        mean_abs = 0.0
    timing = {
        "grid_steps_emitted": float(len(jitter_samples)),
        "max_abs_grid_jitter_clocks": float(max_abs),
        "mean_abs_grid_jitter_clocks": float(mean_abs),
        "stop_press_sent_during_stream": 1.0 if stop_press_sent_during_stream else 0.0,
        # Pacing evidence: which source timed this phase, and how many of its positions the
        # phase actually consumed (grid positions for the two clock modes, deadlined steps for
        # the wall-clock proxy, raw `in_port` messages for the legacy path). `pulses_observed`
        # is the reader's raw count, so a dropout shows up as observed < counted.
        "clock_source": clock_source,
        "clock_pulses_counted": float(clock_count_total),
        "clock_pulses_observed": (
            float(clock.observed_pulses()) if clock is not None else -1.0
        ),
        "clock_gap_count": float(clock.gap_count()) if clock is not None else -1.0,
        # Device-authoritative end condition evidence (see device_bar_probe).
        "device_bar_start": -1.0 if device_bar_start is None else float(device_bar_start),
        "device_bar_latest": -1.0 if device_bar_latest is None else float(device_bar_latest),
        "device_bar_relative_bars": float(device_bar_relative_bars),
    }
    return note_on_count, cc_count, phase_clock_count, timing


def wait_for_phase_clocks(
    in_port: mido.ports.BaseInput,
    serial_collector: Any | None,
    args: argparse.Namespace,
    pulses: int,
    *,
    timeout_s: float,
    abort: Optional[Any] = None,
) -> int:
    """Wait for phase-length clock count using USB MIDI in or serial transport proxy."""
    using_proxy = use_serial_transport_proxy(args, serial_collector)
    wall_tempo: Optional[float] = None
    if using_proxy:
        fallback = float(getattr(args, "tempo_bpm", 120.0) or 120.0)
        wall_tempo = resolve_wall_tempo_bpm(serial_collector, fallback)
    return wait_for_clock_pulses(
        in_port,
        pulses,
        timeout_s=timeout_s,
        abort=abort,
        wall_clock_tempo_bpm=wall_tempo,
    )
