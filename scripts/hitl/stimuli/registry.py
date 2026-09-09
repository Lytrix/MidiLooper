"""Canonical HITL stimuli. Temporary variants are not registered here."""

from __future__ import annotations

from dataclasses import replace

from hitl.stimuli.spec import (
    KIND_CANONICAL,
    KIND_TEMPORARY,
    OverdubRecipe,
    StimulusSpec,
)

STIM_BASE_2_ONE_OD_01 = StimulusSpec(
    stimulus_id="STIM-BASE-2-ONE-OD-01",
    version=1,
    kind=KIND_CANONICAL,
    scenario_id="base",
    description="2-bar record plus one overdub; no second overdub; no post-stop undo",
    generator="canonical_record_one_overdub_legacy_args",
    base_bars=2,
    base_pattern="BASE-2",
    base_stimulus_id=None,
    overdubs=(
        OverdubRecipe(name="OD1", bars=2, pattern="BASELINE_OVERDUB", note_low=24, note_high=39),
    ),
    interactions=(),
    must_prove=("base_recorded", "committed_od1"),
    human_checks=("seed loop plays",),
)

STIM_NE_OD_COMPOSE_01 = StimulusSpec(
    stimulus_id="STIM-NE-OD-COMPOSE-01",
    version=1,
    kind=KIND_CANONICAL,
    scenario_id="note_edit_overdub_compose",
    description=(
        "BASE-2 + one overdub, then NOTE_EDIT, in-edit overdub, live Capture "
        "notes 12–23, and a selected-note move"
    ),
    generator="scenario:note_edit_overdub_compose",
    base_bars=2,
    base_pattern="BASE-2-ONE-OD",
    base_stimulus_id=STIM_BASE_2_ONE_OD_01.stimulus_id,
    overdubs=(
        OverdubRecipe(name="OD1", bars=2, pattern="BASELINE_OVERDUB", note_low=24, note_high=39),
        OverdubRecipe(
            name="IN_EDIT_OD",
            bars=2,
            pattern="LIVE_CAPTURE_COMPOSE",
            note_low=12,
            note_high=23,
        ),
    ),
    interactions=("note_edit", "in_edit_overdub", "move_selected_note"),
    must_prove=(
        "base_recorded",
        "committed_od1",
        "in_edit_overdub_active",
        "live_capture_notes_present",
        "selected_note_moved",
    ),
    human_checks=(
        "overdub notes audible",
        "moved note visible",
        "no display corruption",
        "stop/restart behaves normally",
    ),
)

_STIMULI: dict[str, StimulusSpec] = {
    STIM_BASE_2_ONE_OD_01.stimulus_id: STIM_BASE_2_ONE_OD_01,
    STIM_NE_OD_COMPOSE_01.stimulus_id: STIM_NE_OD_COMPOSE_01,
}

SCENARIO_STIMULUS: dict[str, str] = {
    "note_edit_overdub_compose": STIM_NE_OD_COMPOSE_01.stimulus_id,
}


def get_stimulus(stimulus_id: str) -> StimulusSpec:
    try:
        return _STIMULI[stimulus_id]
    except KeyError as exc:
        known = ", ".join(sorted(_STIMULI))
        raise KeyError(f"Unknown stimulus {stimulus_id!r}; known: {known}") from exc


def stimulus_for_scenario(scenario_id: str) -> StimulusSpec | None:
    stimulus_id = SCENARIO_STIMULUS.get(scenario_id)
    if stimulus_id is None:
        return None
    return get_stimulus(stimulus_id)


def list_stimuli() -> list[StimulusSpec]:
    return [get_stimulus(stimulus_id) for stimulus_id in sorted(_STIMULI)]


def temporary_variant(parent_id: str, suffix: str, **overrides: object) -> StimulusSpec:
    """Copy a canonical stimulus for an experiment. Not registered."""
    parent = get_stimulus(parent_id)
    temp_id = f"{parent.stimulus_id}-TEMP-{suffix}"
    return replace(
        parent,
        stimulus_id=temp_id,
        kind=KIND_TEMPORARY,
        parent_id=parent.stimulus_id,
        **overrides,
    )
