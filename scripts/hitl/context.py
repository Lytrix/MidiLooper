"""Shared run state passed between composable scenarios."""

from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Any, Optional

from hitl.config import HitlConfig
from hitl.session import HitlSession


@dataclass
class ActionContext:
    session: HitlSession
    config: HitlConfig

    @property
    def out_port(self) -> Any:
        return self.session.out_port

    @property
    def in_port(self) -> Any:
        return self.session.in_port

    @property
    def collector(self) -> Any:
        return self.session.collector

    @property
    def markers(self) -> list[str]:
        return self.session.markers


@dataclass
class ScenarioContext:
    action: ActionContext
    scenario_id: str
    out_dir: Path
    started_at: datetime


@dataclass
class HitlRunContext:
    record_layout: Any = None
    record_fixture: Any = None
    markers: list[str] = field(default_factory=list)
    midi_channel: int = 5
    record_bars: int = 2
    base_preset_passed: bool = False
    base_report: dict[str, Any] | None = None
    base_serial_log_path: Path | None = None


def get_context(args: object) -> HitlRunContext:
    ctx = getattr(args, "hitl_context", None)
    if ctx is None:
        ctx = HitlRunContext()
        setattr(args, "hitl_context", ctx)
    return ctx
