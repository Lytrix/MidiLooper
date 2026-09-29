"""Host-side MIDI clock orchestration for HITL scenarios."""

from __future__ import annotations

import argparse
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


def _send_short_press(
    out_port: mido.ports.BaseOutput, *, note: int, channel_1based: int, press_ms: int
) -> None:
    ch = channel_1based - 1
    out_port.send(mido.Message("note_on", channel=ch, note=note, velocity=127))
    time.sleep(max(press_ms, 1) / 1000.0)
    out_port.send(mido.Message("note_off", channel=ch, note=note, velocity=0))


def clock_seen_within(in_port: mido.ports.BaseInput, timeout_s: float) -> bool:
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
) -> bool:
    """Wait for incoming MIDI clock; toggle transport once if clock is missing."""
    if min_clocks <= 0:
        return True

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

    _send_short_press(
        out_port,
        note=GLOBAL_TRANSPORT_NOTE,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=120,
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
) -> tuple[bool, bool]:
    """Return (ok, using_serial_proxy). Proxy = serial capture shows transport without USB clock in.

    ``started_since_index`` is for the callers that invoke this **to confirm a transport start
    press they have just made**: pass the serial length captured *before* that press. The press
    inside ``ensure_midi_clock`` is a toggle, and this board emits no USB MIDI clock out, so its
    pulse wait always expires and the press always fires -- pressing while the transport already
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

    ok = ensure_midi_clock(
        in_port,
        out_port,
        min_clocks=min_clocks,
        timeout_s=timeout_s,
        abort=abort,
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
) -> tuple[int, int, int, dict[str, float]]:
    """Send note pattern until target bar count from MIDI clock or wall-clock proxy.

    ``device_bar_probe`` returns the device's own latest ``BAR,<tick>,<index>`` index, and
    is the authoritative end condition when it is available: pacing by host clock messages
    or by wall-clock seconds both over-ran on device (11-34 bars for an 8-bar request),
    while the device's own bar counter is exactly what the loop length is quantized from.
    The stream ends once the device index has advanced by ``target_bars`` from its value at
    stream start.
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
    if stop_press_note is not None and stop_press_advance_clocks > 0:
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

        if seconds_per_clock is not None:
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
