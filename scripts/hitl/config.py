"""Immutable HITL run configuration."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Optional


@dataclass(frozen=True)
class HitlConfig:
    preset: Optional[str]
    scenario_ids: tuple[str, ...]
    track_number: int
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

    @property
    def uses_external_serial_capture(self) -> bool:
        return self.follow_current_session and not self.serial_port
