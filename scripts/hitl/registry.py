"""Named HITL scenario registry."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Callable, Optional

RunFn = Callable[[object], int]
VerifyFn = Callable[[list[str], object], dict[str, object]]


@dataclass(frozen=True)
class ScenarioSpec:
    scenario_id: str
    description: str
    run: Optional[RunFn] = None
    verify: Optional[VerifyFn] = None
    preset: Optional[str] = None


PRESET_SCENARIOS: dict[str, tuple[str, ...]] = {
    "base": ("base",),
    "edit_full": ("edit_record_prelude", "edit_full"),
}


def _lazy_registry() -> dict[str, ScenarioSpec]:
    from hitl.scenarios.base import run_base_scenario, verify_base_scenario
    from hitl.scenarios.edit_full import run_edit_full_scenario, verify_edit_full_scenario
    from hitl.scenarios.edit_record_prelude import (
        run_edit_record_prelude,
        verify_edit_record_prelude,
    )

    return {
        "base": ScenarioSpec(
            scenario_id="base",
            description="Record/overdub baseline (legacy host_midi_automation_baseline)",
            run=run_base_scenario,
            verify=verify_base_scenario,
        ),
        "edit_record_prelude": ScenarioSpec(
            scenario_id="edit_record_prelude",
            description="2-bar fixture record prelude for edit_full",
            run=run_edit_record_prelude,
            verify=verify_edit_record_prelude,
        ),
        "edit_full": ScenarioSpec(
            scenario_id="edit_full",
            description="Full note-edit overlap suite (legacy edit baseline body)",
            run=run_edit_full_scenario,
            verify=verify_edit_full_scenario,
        ),
    }


SCENARIO_REGISTRY: dict[str, ScenarioSpec] | None = None


def get_registry() -> dict[str, ScenarioSpec]:
    global SCENARIO_REGISTRY
    if SCENARIO_REGISTRY is None:
        SCENARIO_REGISTRY = _lazy_registry()
    return SCENARIO_REGISTRY


def list_scenarios() -> list[str]:
    return sorted(get_registry().keys())


def resolve_scenarios(*, preset: Optional[str], scenario_ids: Optional[list[str]]) -> list[str]:
    if preset:
        if preset not in PRESET_SCENARIOS:
            known = ", ".join(sorted(PRESET_SCENARIOS))
            raise ValueError(f"Unknown preset {preset!r}; known presets: {known}")
        return list(PRESET_SCENARIOS[preset])
    if scenario_ids:
        registry = get_registry()
        unknown = [sid for sid in scenario_ids if sid not in registry]
        if unknown:
            raise ValueError(f"Unknown scenario(s): {', '.join(unknown)}")
        return list(scenario_ids)
    return ["base"]
