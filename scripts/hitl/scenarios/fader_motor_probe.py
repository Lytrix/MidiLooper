"""HITL: probe DROID fader motor with ch16 PC + pitchbend/note-0 sequences."""

from __future__ import annotations

import argparse
import time
from typing import Optional

from hitl.context import get_context
from hitl.fader_motor_probe import (
    FaderTarget,
    MotorTriggerTiming,
    SWEEP_QUARTER_PERCENTS,
    motor_channel_for_fader,
    run_fader_motor_probe,
)


def _parse_args(args: object) -> argparse.Namespace:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--midi-out", default="Teensy")
    parser.add_argument("--midi-in", default="Teensy")
    parser.add_argument("--serial-port", default=None)
    parser.add_argument(
        "--fader",
        choices=("fader1", "fader2", "both"),
        default="fader1",
        help="Motorfader target: fader1/ch16, fader2/ch14, or both in parallel",
    )
    parser.add_argument(
        "--timing",
        choices=[t.value for t in MotorTriggerTiming],
        default=MotorTriggerTiming.PITCH_NOTE_OFF.value,
        help="pitchbend/note-off ordering (default: pitchbend, then note on, then off)",
    )
    parser.add_argument(
        "--settle-ms",
        type=int,
        default=2500,
        help="Wait after each coarse step for motor echo (ms)",
    )
    parser.add_argument(
        "--compare-all-timings",
        action="store_true",
        help="Run burst, note_pitch_off, pitch_note_off, and long_gate in one session",
    )
    parser.add_argument(
        "--sweep-quarters",
        action="store_true",
        help="Sweep fader 0%% → 25%% → 50%% → 75%% → 100%% (default: 0%% and 100%% only)",
    )
    parser.add_argument(
        "--percent-steps",
        default=None,
        help="Comma-separated percent positions, e.g. 0,25,50,75,100",
    )
    parser.add_argument(
        "--no-edit-state-pc",
        action="store_true",
        help="Skip PC 1 ch16 + ch15 note-0 NOTE_EDIT arm (use if already active on DROID)",
    )
    parser.add_argument("--pc-delay-ms", type=int, default=50)
    parser.add_argument("--edit-arm-settle-ms", type=int, default=200)
    parser.add_argument("--final-wait-ms", type=int, default=1000)
    legacy = list(getattr(args, "legacy_args", []) or [])
    return parser.parse_args(legacy)


def _resolve_percent_steps(ns: argparse.Namespace) -> tuple[int, ...]:
    if ns.percent_steps:
        parts = [p.strip() for p in str(ns.percent_steps).split(",") if p.strip()]
        return tuple(int(p) for p in parts)
    if ns.sweep_quarters:
        return SWEEP_QUARTER_PERCENTS
    from hitl.fader_motor_probe import DEFAULT_PROBE_PERCENTS

    return DEFAULT_PROBE_PERCENTS


def run_fader_motor_probe_scenario(args: object) -> int:
    import mido
    from host_midi_automation_baseline import (
        SerialCaptureCollector,
        _find_midi_port,
    )
    from hitl.fader_motor_probe import drain_pitchbend_echoes, motor_channels_for_fader

    ns = _parse_args(args)
    ctx = get_context(args)
    fader: FaderTarget = ns.fader
    motor_channels = motor_channels_for_fader(fader)
    percent_steps = _resolve_percent_steps(ns)

    out_port = mido.open_output(_find_midi_port(ns.midi_out, "output"))
    in_port = mido.open_input(_find_midi_port(ns.midi_in, "input"))
    serial_collector: Optional[SerialCaptureCollector] = None

    timings: list[MotorTriggerTiming]
    if ns.compare_all_timings:
        timings = list(MotorTriggerTiming)
    else:
        timings = [MotorTriggerTiming(ns.timing)]

    print(
        "[fader-motor-probe] Requires firmware with MIDI_USB_FADER_PROBE_PASSTHROUGH "
        "(env teensy41-capture-serial-fader-probe). Host USB → Teensy → DROID usb host."
    )

    try:
        if ns.serial_port:
            serial_collector = SerialCaptureCollector(ns.serial_port, baud=115200)
            serial_collector.start()
            time.sleep(1.5)

        send_pc = not ns.no_edit_state_pc
        for idx, timing in enumerate(timings):
            if idx > 0:
                print(f"[fader-motor-probe] --- next timing profile ({idx + 1}/{len(timings)}) ---")
                time.sleep(ns.final_wait_ms / 1000.0)
            run_fader_motor_probe(
                out_port,
                fader=fader,
                timing=timing,
                settle_ms=ns.settle_ms,
                send_edit_state_pc=send_pc and idx == 0,
                pc_delay_ms=ns.pc_delay_ms,
                edit_arm_settle_ms=ns.edit_arm_settle_ms,
                percent_steps=percent_steps,
            )
            echo_window_ms = ns.settle_ms * len(percent_steps)
            for motor_ch in motor_channels:
                echoes = drain_pitchbend_echoes(
                    in_port, channel_1based=motor_ch, window_ms=echo_window_ms
                )
                print(
                    f"[fader-motor-probe] MIDI in ch{motor_ch} pitchwheel echoes "
                    f"({timing.value}): {len(echoes)} sample(s)"
                )
                for offset_s, pitch in echoes[:8]:
                    print(f"  ch{motor_ch} +{offset_s:.3f}s pitch={pitch}")

        time.sleep(ns.final_wait_ms / 1000.0)
        ctx.markers.append(
            f"fader_motor_probe:{fader}:{'-'.join(str(p) for p in percent_steps)}"
        )
        return 0
    finally:
        in_port.close()
        out_port.close()
        if serial_collector is not None:
            serial_collector.stop()


def run_fader_motor_sweep_scenario(args: object) -> int:
    """Quarter sweep preset: 0 % → 25 % → 50 % → 75 % → 100 %."""
    legacy = list(getattr(args, "legacy_args", []) or [])
    if "--sweep-quarters" not in legacy and "--percent-steps" not in legacy:
        legacy.append("--sweep-quarters")
    setattr(args, "legacy_args", legacy)
    return run_fader_motor_probe_scenario(args)


def verify_fader_motor_probe_scenario(lines: list[str], args: object) -> dict[str, object]:
    from hitl.verify.fader_motor_probe import verify_fader_motor_probe

    result = verify_fader_motor_probe(lines, args)
    result["scenario"] = "fader_motor_probe"
    return result
