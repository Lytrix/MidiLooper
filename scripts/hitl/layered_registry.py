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
    "edit_minimal": PresetSpec(
        scenario_ids=("record_seed", "edit_minimal"),
        tags=("edit",),
    ),
    "uip_5_5": PresetSpec(
        scenario_ids=(
            "record_seed",
            "edit_minimal",
            "long_loop_display_window",
            "slot_queued_start",
        ),
        tags=("uip", "gate"),
    ),
}


def _verifier_stub(_lines: list[str], _ctx: ScenarioContext) -> VerificationResult:
    return VerificationResult(passed=True)


_VERIFIER_REGISTRY: dict[str, VerifierFn] = {
    "capture": _verifier_stub,
    "playback": _verifier_stub,
    "display": _verifier_stub,
}


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
    from hitl.scenarios.layered_stubs import (
        run_edit_minimal_stub,
        run_long_loop_display_window_stub,
        run_record_overdub_stub,
        run_record_seed_stub,
        run_slot_queued_start_stub,
    )

    return {
        "record_seed": LayeredScenarioSpec(
            id="record_seed",
            description="Clear slot and record seed loop",
            tags=("record",),
            verifier_id="capture",
            run=run_record_seed_stub,
        ),
        "record_overdub": LayeredScenarioSpec(
            id="record_overdub",
            description="Record seed plus overdub passes",
            tags=("record",),
            verifier_id="capture",
            run=run_record_overdub_stub,
        ),
        "edit_minimal": LayeredScenarioSpec(
            id="edit_minimal",
            description="Note edit smoke after record seed",
            tags=("edit",),
            verifier_id="playback",
            run=run_edit_minimal_stub,
        ),
        "long_loop_display_window": LayeredScenarioSpec(
            id="long_loop_display_window",
            description="Long loop display window behaviour",
            tags=("display",),
            verifier_id="display",
            run=run_long_loop_display_window_stub,
        ),
        "slot_queued_start": LayeredScenarioSpec(
            id="slot_queued_start",
            description="Queued slot switch playback",
            tags=("slot",),
            verifier_id="playback",
            run=run_slot_queued_start_stub,
        ),
    }


def register_verifier(verifier_id: str, fn: VerifierFn) -> None:
    _VERIFIER_REGISTRY[verifier_id] = fn
