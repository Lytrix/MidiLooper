"""Declarative scenario and preset registry for the layered HITL framework."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Callable, Optional

from hitl.context import ScenarioContext
from hitl.types import ScenarioResult
from hitl.verify.result import VerificationResult

ScenarioRunFn = Callable[[ScenarioContext], ScenarioResult]
VerifierFn = Callable[[list[str], ScenarioContext], VerificationResult]


@dataclass(frozen=True)
class LayeredScenarioSpec:
    id: str
    description: str
    tags: tuple[str, ...]
    verifier_id: str | None
    run: ScenarioRunFn | None = None


@dataclass(frozen=True)
class PresetSpec:
    scenario_ids: tuple[str, ...]
    tags: tuple[str, ...] = ()


LAYERED_PRESETS: dict[str, PresetSpec] = {
    "base": PresetSpec(scenario_ids=("record_overdub",), tags=("record",)),
    "edit_full": PresetSpec(scenario_ids=("edit_full",), tags=("edit",)),
}


def _verifier_stub(_lines: list[str], _ctx: ScenarioContext) -> VerificationResult:
    return VerificationResult(passed=True)


_VERIFIER_REGISTRY: dict[str, VerifierFn] = {
    "capture": _verifier_stub,
    "scenario": _verifier_stub,
}


def register_verifier(verifier_id: str, fn: VerifierFn) -> None:
    _VERIFIER_REGISTRY[verifier_id] = fn


def get_verifier(verifier_id: str | None) -> VerifierFn:
    if verifier_id is None:
        return _verifier_stub
    if verifier_id not in _VERIFIER_REGISTRY:
        raise KeyError(f"Unknown verifier_id: {verifier_id!r}")
    return _VERIFIER_REGISTRY[verifier_id]


def verify(
    verifier_id: str | None,
    lines: list[str],
    ctx: ScenarioContext,
) -> VerificationResult:
    return get_verifier(verifier_id)(lines, ctx)


def resolve_preset_scenario_ids(preset: str) -> tuple[str, ...]:
    if preset not in LAYERED_PRESETS:
        known = ", ".join(sorted(LAYERED_PRESETS))
        raise ValueError(f"Unknown preset {preset!r}; known presets: {known}")
    return LAYERED_PRESETS[preset].scenario_ids


_LAYERED_SCENARIO_REGISTRY: dict[str, LayeredScenarioSpec] | None = None


def get_layered_registry() -> dict[str, LayeredScenarioSpec]:
    global _LAYERED_SCENARIO_REGISTRY
    if _LAYERED_SCENARIO_REGISTRY is None:
        _LAYERED_SCENARIO_REGISTRY = _build_layered_registry()
    return _LAYERED_SCENARIO_REGISTRY


def _build_layered_registry() -> dict[str, LayeredScenarioSpec]:
    from hitl.scenarios.layered import run_edit_full, run_record_overdub

    return {
        "record_overdub": LayeredScenarioSpec(
            id="record_overdub",
            description="Record seed plus overdub passes",
            tags=("record",),
            verifier_id="capture",
            run=run_record_overdub,
        ),
        "edit_full": LayeredScenarioSpec(
            id="edit_full",
            description="Full note-edit overlap suite",
            tags=("edit",),
            verifier_id="scenario",
            run=run_edit_full,
        ),
    }


def _register_default_verifiers() -> None:
    from hitl.verify import capture, scenarios

    register_verifier("capture", capture.verify_capture)
    register_verifier("scenario", scenarios.verify_scenario)


_register_default_verifiers()
