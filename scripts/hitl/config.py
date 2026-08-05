"""Immutable HITL run configuration."""

from __future__ import annotations

from dataclasses import dataclass, replace
from pathlib import Path
from typing import Optional


@dataclass(frozen=True)
class HitlConfig:
    preset: Optional[str]
    scenario_ids: tuple[str, ...]
    track_number: int
    loop_slot: int
    midi_channel: int
    record_bars: int
    overdub_bars: int
    midi_out_name: str
    midi_in_name: str
    serial_port: Optional[str]
    verify_serial_log: Optional[Path]
    verify_only: bool
    out_dir: Path
    press_ms: int = 120
    phase_wait_ms: int = 500
    follow_current_session: bool = False
    follow_serial_log: Optional[Path] = None
    legacy_extra_args: tuple[str, ...] = ()
    boot_settle_ms: int = 10000
    managed_capture: bool = True
    capture_serial_port: str = "/dev/cu.usbmodem154944801"
    capture_boot_wait_s: float = 10.0

    def needs_managed_capture(self) -> bool:
        return bool(self.managed_capture and not self.verify_only and not self.serial_port)

    def with_follow_current_session(self) -> HitlConfig:
        merged_legacy = list(self.legacy_extra_args)
        if "--follow-current-session" not in merged_legacy:
            merged_legacy.append("--follow-current-session")
        return replace(
            self,
            follow_current_session=True,
            legacy_extra_args=tuple(merged_legacy),
        )

    @property
    def uses_external_serial_capture(self) -> bool:
        return bool(
            not self.serial_port
            and (
                self.follow_current_session
                or self.follow_serial_log is not None
                or self.needs_managed_capture()
            )
        )
