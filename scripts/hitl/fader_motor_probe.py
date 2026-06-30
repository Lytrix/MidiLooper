"""DROID fader motor probe — ch16 PC + pitchbend / note-0 trigger on fader1 or fader2.

See docs/Guides/DROID_MOTORFADER_PITCHBEND.md (scale, NOTE_EDIT arm, pitch_note_off timing).
"""

from __future__ import annotations

import time
from dataclasses import dataclass
from enum import Enum
from typing import Literal

import mido

# Match MidiConfig.h / DROID midilooper_v1.ini (1-based channel numbers for logs).
EDIT_STATE_CHANNEL_1BASED = 16
NOTE_EDIT_LED_CHANNEL_1BASED = 15
FADER1_MOTOR_CHANNEL_1BASED = 16
FADER2_MOTOR_CHANNEL_1BASED = 14
MOTOR_TRIGGER_NOTE = 0
NOTE_EDIT_PROGRAM = 1
NOTE_EDIT_LED_TRIGGER_NOTE = 0
NOTE_EDIT_LED_TRIGGER_VELOCITY = 64
NOTE_EDIT_LED_TRIGGER_MS = 10

# Signed MIDI pitchbend endpoints (match firmware MidiConfig / bipolar motorfader path).
# 0 % → -8192, 50 % → 0, 100 % → +8192 (wire encodes +8192 as unsigned 16383).
PITCHBEND_MIN = -8192
PITCHBEND_CENTER = 0
PITCHBEND_MAX = 8192

DEFAULT_PROBE_PERCENTS: tuple[int, ...] = (0, 100)
SWEEP_QUARTER_PERCENTS: tuple[int, ...] = (0, 25, 50, 75, 100)

FaderTarget = Literal["fader1", "fader2", "both"]

FADER_MOTOR_CHANNEL: dict[str, int] = {
    "fader1": FADER1_MOTOR_CHANNEL_1BASED,
    "fader2": FADER2_MOTOR_CHANNEL_1BASED,
}

BOTH_MOTOR_CHANNELS: tuple[int, ...] = (
    FADER1_MOTOR_CHANNEL_1BASED,
    FADER2_MOTOR_CHANNEL_1BASED,
)


class MotorTriggerTiming(str, Enum):
    """How note-on, pitchbend, and note-off are ordered in time."""

    BURST = "burst"
    """pitchbend → note on → note off with ~1 ms gaps (current firmware-style)."""
    NOTE_PITCH_OFF = "note_pitch_off"
    """note on → wait → pitchbend → wait → note off (DROID processes gate before value)."""
    PITCH_NOTE_OFF = "pitch_note_off"
    """pitchbend → wait → note on → wait → note off (value set before motor trigger)."""
    LONG_GATE = "long_gate"
    """note on → 100 ms → pitchbend → 50 ms → note off (extended gate for slow motor path)."""


@dataclass(frozen=True)
class TimingDelaysMs:
    before_pitchbend: int = 0
    before_note_off: int = 0
    after_burst: int = 1


TIMING_DELAYS: dict[MotorTriggerTiming, TimingDelaysMs] = {
    MotorTriggerTiming.BURST: TimingDelaysMs(before_pitchbend=0, before_note_off=1, after_burst=1),
    MotorTriggerTiming.NOTE_PITCH_OFF: TimingDelaysMs(
        before_pitchbend=20, before_note_off=20, after_burst=0
    ),
    MotorTriggerTiming.PITCH_NOTE_OFF: TimingDelaysMs(
        before_pitchbend=20, before_note_off=20, after_burst=0
    ),
    MotorTriggerTiming.LONG_GATE: TimingDelaysMs(
        before_pitchbend=100, before_note_off=50, after_burst=0
    ),
}


def _ch0(channel_1based: int) -> int:
    return channel_1based - 1


def motor_channel_for_fader(fader: FaderTarget) -> int:
    if fader == "both":
        return FADER1_MOTOR_CHANNEL_1BASED
    return FADER_MOTOR_CHANNEL[fader]


def motor_channels_for_fader(fader: FaderTarget) -> tuple[int, ...]:
    """Motor MIDI channels (1-based) for one fader or both in parallel."""
    if fader == "both":
        return BOTH_MOTOR_CHANNELS
    return (FADER_MOTOR_CHANNEL[fader],)


def clamp_pitchbend(pitch: int) -> int:
    """Keep signed pitchbend in [-8192, +8192] (both endpoints valid)."""
    return max(PITCHBEND_MIN, min(PITCHBEND_MAX, int(pitch)))


def pitchbend_for_mido(pitch: int) -> int:
    """
    Map logical pitchbend to a value mido accepts (-8192 … +8191).

    +8192 (100 % endpoint) encodes as unsigned 16383, same as +8191 on the wire.
    """
    logical = clamp_pitchbend(pitch)
    if logical == PITCHBEND_MAX:
        return 8191
    return logical


def pitchbend_for_fader_percent(percent: float) -> int:
    """Map 0–100 % motor travel → signed pitchbend (-8192 … +8192)."""
    clamped = max(0.0, min(100.0, float(percent)))
    span = PITCHBEND_MAX - PITCHBEND_MIN
    return clamp_pitchbend(int(PITCHBEND_MIN + (span * clamped / 100.0)))


def send_program_change(out_port: mido.ports.BaseOutput, channel_1based: int, program: int) -> None:
    out_port.send(
        mido.Message("program_change", channel=_ch0(channel_1based), program=program & 0x7F)
    )


def send_note_edit_program_change(
    out_port: mido.ports.BaseOutput,
    *,
    channel_1based: int = EDIT_STATE_CHANNEL_1BASED,
    program: int = NOTE_EDIT_PROGRAM,
    delay_ms: int = 50,
    step_label: str = "",
) -> None:
    """PC NOTE_EDIT (1) on ch16 before each motor update."""
    prefix = f"{step_label}: " if step_label else ""
    send_program_change(out_port, channel_1based, program)
    print(f"[fader-motor-probe] {prefix}PC {program} ch{channel_1based} (NOTE_EDIT)")
    if delay_ms > 0:
        time.sleep(delay_ms / 1000.0)


def send_note_edit_mode_led_trigger(
    out_port: mido.ports.BaseOutput,
    *,
    channel_1based: int = NOTE_EDIT_LED_CHANNEL_1BASED,
    note: int = NOTE_EDIT_LED_TRIGGER_NOTE,
    velocity: int = NOTE_EDIT_LED_TRIGGER_VELOCITY,
    gate_ms: int = NOTE_EDIT_LED_TRIGGER_MS,
    settle_ms: int = 200,
    step_label: str = "",
) -> None:
    """Mirror EditManager::sendEditSessionChange — note 0 on ch15 after PC."""
    prefix = f"{step_label}: " if step_label else ""
    ch = _ch0(channel_1based)
    out_port.send(mido.Message("note_on", channel=ch, note=note, velocity=velocity))
    print(
        f"[fader-motor-probe] {prefix}note {note} on ch{channel_1based} "
        f"(NOTE_EDIT LED trigger)"
    )
    time.sleep(gate_ms / 1000.0)
    out_port.send(mido.Message("note_off", channel=ch, note=note, velocity=0))
    if settle_ms > 0:
        time.sleep(settle_ms / 1000.0)


def send_pitchbend_only(
    out_port: mido.ports.BaseOutput,
    *,
    channel_1based: int | tuple[int, ...],
    pitch: int,
    settle_ms: int = 50,
) -> None:
    """Set motorfader position without notegate trigger (DROID midiin pitchbend path)."""
    channels = (channel_1based,) if isinstance(channel_1based, int) else channel_1based
    wire_pitch = pitchbend_for_mido(pitch)
    for ch_1based in channels:
        out_port.send(
            mido.Message("pitchwheel", channel=_ch0(ch_1based), pitch=wire_pitch)
        )
    if settle_ms > 0:
        time.sleep(settle_ms / 1000.0)


def arm_note_edit_session(
    out_port: mido.ports.BaseOutput,
    *,
    pc_delay_ms: int = 50,
    led_settle_ms: int = 200,
    step_label: str = "session",
    prime_pitch: int | None = None,
    prime_channel_1based: int | tuple[int, ...] = FADER1_MOTOR_CHANNEL_1BASED,
) -> None:
    send_note_edit_program_change(out_port, delay_ms=pc_delay_ms, step_label=step_label)
    send_note_edit_mode_led_trigger(
        out_port, settle_ms=led_settle_ms, step_label=step_label
    )
    # DROID NOTE_EDIT arm applies startvalue at last stored position (~0.5 / 50 %).
    # Prime pitchbend so the first notegate trigger does not snap to center first.
    if prime_pitch is not None:
        send_pitchbend_only(
            out_port,
            channel_1based=prime_channel_1based,
            pitch=prime_pitch,
            settle_ms=80,
        )


def send_motor_trigger_burst(
    out_port: mido.ports.BaseOutput,
    *,
    channel_1based: int | tuple[int, ...],
    pitch: int,
    timing: MotorTriggerTiming,
    note_velocity: int = 127,
) -> None:
    """Send one motor update: pitchbend + notegate note 0 on each channel in ``channel_1based``."""
    channels = (channel_1based,) if isinstance(channel_1based, int) else channel_1based
    wire_pitch = pitchbend_for_mido(pitch)
    delays = TIMING_DELAYS[timing]
    ch0_list = [_ch0(ch) for ch in channels]

    def _note_on() -> None:
        for ch in ch0_list:
            out_port.send(
                mido.Message("note_on", channel=ch, note=MOTOR_TRIGGER_NOTE, velocity=note_velocity)
            )

    def _note_off() -> None:
        for ch in ch0_list:
            out_port.send(
                mido.Message("note_off", channel=ch, note=MOTOR_TRIGGER_NOTE, velocity=0)
            )

    def _pitch() -> None:
        for ch in ch0_list:
            out_port.send(mido.Message("pitchwheel", channel=ch, pitch=wire_pitch))

    if timing == MotorTriggerTiming.BURST:
        _pitch()
        time.sleep(delays.after_burst / 1000.0)
        _note_on()
        time.sleep(delays.after_burst / 1000.0)
        _note_off()
        return

    if timing == MotorTriggerTiming.NOTE_PITCH_OFF:
        _note_on()
        time.sleep(delays.before_pitchbend / 1000.0)
        _pitch()
        time.sleep(delays.before_note_off / 1000.0)
        _note_off()
        return

    if timing == MotorTriggerTiming.PITCH_NOTE_OFF:
        _pitch()
        time.sleep(delays.before_pitchbend / 1000.0)
        _note_on()
        time.sleep(delays.before_note_off / 1000.0)
        _note_off()
        return

    if timing == MotorTriggerTiming.LONG_GATE:
        _note_on()
        time.sleep(delays.before_pitchbend / 1000.0)
        _pitch()
        time.sleep(delays.before_note_off / 1000.0)
        _note_off()
        return

    raise ValueError(f"unsupported timing {timing!r}")


def run_fader_motor_probe(
    out_port: mido.ports.BaseOutput,
    *,
    fader: FaderTarget = "fader1",
    timing: MotorTriggerTiming = MotorTriggerTiming.PITCH_NOTE_OFF,
    settle_ms: int = 2000,
    send_edit_state_pc: bool = True,
    pc_delay_ms: int = 50,
    edit_arm_settle_ms: int = 200,
    percent_steps: tuple[int, ...] = DEFAULT_PROBE_PERCENTS,
) -> None:
    """
  Sequence when PC enabled:
    1. PC 1 ch16 + ch15 note-0 (NOTE_EDIT arm) once
    2. For each percent in percent_steps: pitchbend + motor trigger → wait settle_ms
    """
    motor_channels = motor_channels_for_fader(fader)
    motor_ch_label = ",".join(str(ch) for ch in motor_channels)

    label = timing.value
    steps_label = ",".join(f"{p}%" for p in percent_steps)
    print(
        f"[fader-motor-probe] fader={fader} motor_ch=[{motor_ch_label}] timing={label} "
        f"steps=[{steps_label}] settle_ms={settle_ms}"
    )

    if send_edit_state_pc:
        first_pitch = (
            pitchbend_for_fader_percent(percent_steps[0]) if percent_steps else PITCHBEND_MIN
        )
        arm_note_edit_session(
            out_port,
            pc_delay_ms=pc_delay_ms,
            led_settle_ms=edit_arm_settle_ms,
            prime_pitch=first_pitch,
            prime_channel_1based=motor_channels,
        )

    for index, percent in enumerate(percent_steps):
        pitch = pitchbend_for_fader_percent(percent)
        wire_pitch = pitchbend_for_mido(pitch)
        unsigned = wire_pitch + 8192
        step_name = chr(ord("A") + index) if index < 26 else str(index + 1)
        wire_note = f" wire={wire_pitch}" if wire_pitch != pitch else ""
        print(
            f"[fader-motor-probe] step {step_name}: ch[{motor_ch_label}] "
            f"pitch={pitch}{wire_note} unsigned={unsigned} ({percent}%)"
        )
        send_motor_trigger_burst(
            out_port,
            channel_1based=motor_channels,
            pitch=pitch,
            timing=timing,
        )
        time.sleep(settle_ms / 1000.0)


# Backward-compatible alias (defaults now target fader1).
run_fader2_motor_probe = run_fader_motor_probe


def drain_pitchbend_echoes(
    in_port: mido.ports.BaseInput,
    *,
    channel_1based: int = FADER1_MOTOR_CHANNEL_1BASED,
    window_ms: int = 2500,
) -> list[tuple[float, int]]:
    """Collect pitchwheel messages from Teensy (DROID motor echo) for ``window_ms``."""
    ch = _ch0(channel_1based)
    deadline = time.monotonic() + window_ms / 1000.0
    echoes: list[tuple[float, int]] = []
    t0 = time.monotonic()
    while time.monotonic() < deadline:
        for msg in in_port.iter_pending():
            if msg.type == "pitchwheel" and msg.channel == ch:
                echoes.append((time.monotonic() - t0, msg.pitch))
        time.sleep(0.005)
    return echoes


def verify_pitchbend_echoes(
    lines: list[str],
    *,
    channel_1based: int = FADER1_MOTOR_CHANNEL_1BASED,
    min_echo_count: int = 1,
) -> dict[str, object]:
    """Parse #CAP MI,H,224,<ch> from serial capture (USB host inbound from DROID)."""
    needle = f",MI,H,224,{channel_1based},"
    echoes = []
    for line in lines:
        if "#CAP," not in line or needle not in line:
            continue
        parts = line.strip().split(",")
        if len(parts) < 7:
            continue
        try:
            lsb = int(parts[5])
            msb = int(parts[6])
        except ValueError:
            continue
        echoes.append((lsb, msb))
    ok = len(echoes) >= min_echo_count
    return {
        "ok": ok,
        "motor_channel_1based": channel_1based,
        "mi_pitchbend_echo_count": len(echoes),
        "min_echo_count": min_echo_count,
        "issues": [] if ok else [f"no_mi_ch{channel_1based}_pitchbend_echo"],
    }


def verify_ch14_pitchbend_echoes(
    lines: list[str],
    *,
    min_echo_count: int = 1,
) -> dict[str, object]:
    return verify_pitchbend_echoes(
        lines,
        channel_1based=FADER2_MOTOR_CHANNEL_1BASED,
        min_echo_count=min_echo_count,
    )
