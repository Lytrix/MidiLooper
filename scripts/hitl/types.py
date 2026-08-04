"""Shared HITL result types."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class ScenarioResult:
    observations: dict[str, object] = field(default_factory=dict)
    markers: list[str] = field(default_factory=list)
    artifacts: dict[str, Path] = field(default_factory=dict)
