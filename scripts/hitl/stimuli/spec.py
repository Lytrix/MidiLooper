"""Versioned HITL stimulus recipe (host contract, not a MIDI sender)."""

from __future__ import annotations

from dataclasses import dataclass


KIND_CANONICAL = "canonical"
KIND_TEMPORARY = "temporary"


@dataclass(frozen=True)
class OverdubRecipe:
    name: str
    bars: int
    pattern: str
    note_low: int | None = None
    note_high: int | None = None


@dataclass(frozen=True)
class StimulusSpec:
    """Named live-MIDI experiment. Generators stay in flows / canonical args."""

    stimulus_id: str
    version: int
    kind: str
    scenario_id: str
    description: str
    generator: str
    base_bars: int
    base_pattern: str
    base_stimulus_id: str | None
    overdubs: tuple[OverdubRecipe, ...]
    interactions: tuple[str, ...]
    must_prove: tuple[str, ...]
    human_checks: tuple[str, ...]
    tempo_bpm: float = 120.0
    clock_ppqn: int = 24
    parent_id: str | None = None

    def fingerprint_key(self) -> str:
        return f"{self.stimulus_id}@v{self.version}"
