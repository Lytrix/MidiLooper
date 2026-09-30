"""Versioned HITL stimulus recipe (host contract, not a MIDI sender)."""

from __future__ import annotations

from dataclasses import dataclass

from typing import Any, Iterable

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
class Authorization:
    """Which targets one run of this profile may mutate, and who granted that.

    Task 4.1's *target authorization manifest*, declared with the recipe instead of
    inferred from whatever a command line happened to pass. A run that is pointed at a
    track or slot outside this scope is refused **before** it mutates anything, so
    "unauthorized target" is a preparation outcome and not a post-hoc observation.

    Numbers are the 1-based user-facing ones the drivers use. An empty `slots` declares
    a read-only profile: nothing may be cleared, seeded or driven. `source` is the
    provenance of the grant and is required -- an authorization nobody can trace is not
    one, and the report carries this record verbatim.
    """

    track: int
    slots: tuple[int, ...]
    source: str
    notes: str = ""

    def __post_init__(self) -> None:
        if not 1 <= self.track <= 8:
            raise ValueError(f"authorized track must be 1-8, got {self.track}")
        if not self.source.strip():
            raise ValueError("an authorization needs a source: who granted it and when")
        seen: set[int] = set()
        for slot in self.slots:
            if not 1 <= slot <= 8:
                raise ValueError(f"authorized slot must be 1-8, got {slot}")
            if slot in seen:
                raise ValueError(f"authorized slot {slot} is declared twice")
            seen.add(slot)

    def violations(self, *, track: int, slots: Iterable[int]) -> tuple[str, ...]:
        """Every requested target outside this scope, named with the declared scope."""
        found: list[str] = []
        if track != self.track:
            found.append(f"track {track} is not the authorized track {self.track}")
        for slot in slots:
            if slot not in self.slots:
                found.append(
                    f"slot {slot} is not authorized on track {self.track} "
                    f"(authorized slots: {self.slots_text()})")
        return tuple(found)

    def allows(self, *, track: int, slots: Iterable[int]) -> bool:
        return not self.violations(track=track, slots=slots)

    def slots_text(self) -> str:
        return "none" if not self.slots else ",".join(str(slot) for slot in self.slots)

    def targets_text(self) -> str:
        return f"track {self.track} slots {self.slots_text()}"

    def as_dict(self) -> dict[str, Any]:
        return {"track": self.track, "slots": list(self.slots), "slots_text": self.slots_text(),
                "targets": self.targets_text(), "source": self.source, "notes": self.notes}

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
    authorization: Authorization | None = None

    def fingerprint_key(self) -> str:
        return f"{self.stimulus_id}@v{self.version}"
