"""Build HitlConfig from runner CLI args."""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Optional

from hitl.baseline_canonical_args import canonical_external_capture_legacy_args
from hitl.config import DEFAULT_CAPTURE_SERIAL_PORT, HitlConfig


def _legacy_flag_value(
    legacy: list[str],
    flag: str,
    default: Optional[str] = None,
) -> Optional[str]:
    for index, token in enumerate(legacy):
        if token == flag and index + 1 < len(legacy):
            return legacy[index + 1]
    return default


def _legacy_has_flag(legacy: list[str], flag: str) -> bool:
    return flag in legacy


def _read_int_flag(
    args: argparse.Namespace,
    legacy: list[str],
    *,
    attr: str,
    legacy_flags: tuple[str, ...],
) -> Optional[int]:
    value = getattr(args, attr, None)
    if value is not None:
        return int(value)
    for flag in legacy_flags:
        raw = _legacy_flag_value(legacy, flag)
        if raw is not None:
            return int(raw)
    return None


def require_slot_target(
    args: argparse.Namespace,
    legacy: list[str],
) -> tuple[int, int, int]:
    """Track row, loop slot, and MIDI channel must all be set explicitly."""
    track_number = _read_int_flag(
        args,
        legacy,
        attr="track_number",
        legacy_flags=("--track-number", "--track"),
    )
    loop_slot = _read_int_flag(
        args,
        legacy,
        attr="loop_slot",
        legacy_flags=("--loop-slot",),
    )
    midi_channel = _read_int_flag(
        args,
        legacy,
        attr="midi_channel",
        legacy_flags=("--midi-channel",),
    )
    missing: list[str] = []
    if track_number is None:
        missing.append("--track-number")
    if loop_slot is None:
        missing.append("--loop-slot")
    if midi_channel is None:
        missing.append("--midi-channel")
    if missing:
        raise ValueError(
            "Required flags missing (no derived defaults): " + ", ".join(missing)
        )
    return track_number, loop_slot, midi_channel


def slot_target_legacy_flags(
    track_number: int,
    loop_slot: int,
    midi_channel: int,
) -> list[str]:
    return [
        "--track-number",
        str(track_number),
        "--loop-slot",
        str(loop_slot),
        "--midi-channel",
        str(midi_channel),
    ]


def legacy_args_with_slot_flags(args: argparse.Namespace, legacy: list[str]) -> list[str]:
    track_number, loop_slot, midi_channel = require_slot_target(args, legacy)
    merged = list(legacy)
    merged.extend(slot_target_legacy_flags(track_number, loop_slot, midi_channel))
    return merged


def config_from_runner_args(args: argparse.Namespace, legacy: list[str]) -> HitlConfig:
    scenario_ids: tuple[str, ...] = ()
    if getattr(args, "scenarios", None):
        scenario_ids = tuple(part.strip() for part in args.scenarios.split(",") if part.strip())

    follow_current_session = _legacy_has_flag(legacy, "--follow-current-session")
    follow_serial_log_raw = _legacy_flag_value(legacy, "--follow-serial-log")
    follow_serial_log = Path(follow_serial_log_raw) if follow_serial_log_raw else None
    serial_port = _legacy_flag_value(legacy, "--serial-port")

    managed_capture = not getattr(args, "no_managed_capture", False)
    if serial_port or getattr(args, "verify_only", False):
        managed_capture = False

    track_number, loop_slot, midi_channel = require_slot_target(args, legacy)

    merged_legacy = legacy_args_with_slot_flags(args, legacy)
    if managed_capture and "--follow-current-session" not in merged_legacy:
        merged_legacy.extend(canonical_external_capture_legacy_args())
        follow_current_session = True

    return HitlConfig(
        preset=getattr(args, "preset", None),
        scenario_ids=scenario_ids,
        track_number=track_number,
        loop_slot=loop_slot,
        midi_channel=midi_channel,
        record_bars=int(_legacy_flag_value(legacy, "--record-bars", "2") or "2"),
        overdub_bars=int(_legacy_flag_value(legacy, "--overdub-bars", "2") or "2"),
        midi_out_name=_legacy_flag_value(legacy, "--midi-out", "Teensy") or "Teensy",
        midi_in_name=_legacy_flag_value(legacy, "--midi-in", "Teensy") or "Teensy",
        serial_port=serial_port,
        verify_serial_log=getattr(args, "verify_serial_log", None),
        verify_only=bool(getattr(args, "verify_only", False)),
        out_dir=Path(getattr(args, "out_dir", Path("captures"))),
        press_ms=int(_legacy_flag_value(legacy, "--press-ms", "120") or "120"),
        phase_wait_ms=int(_legacy_flag_value(legacy, "--phase-wait-ms", "500") or "500"),
        follow_current_session=follow_current_session,
        follow_serial_log=follow_serial_log,
        legacy_extra_args=tuple(merged_legacy),
        boot_settle_ms=int(_legacy_flag_value(legacy, "--boot-settle-ms", "10000") or "10000"),
        managed_capture=managed_capture,
        capture_serial_port=getattr(args, "capture_port", DEFAULT_CAPTURE_SERIAL_PORT),
        capture_boot_wait_s=float(getattr(args, "capture_boot_wait", 10.0)),
    )
