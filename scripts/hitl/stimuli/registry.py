"""Canonical HITL stimuli. Temporary variants are not registered here."""

from __future__ import annotations

from dataclasses import replace

from hitl.stimuli.spec import (
    Authorization,
    KIND_CANONICAL,
    KIND_TEMPORARY,
    OverdubRecipe,
    StimulusSpec,
)


# Task 4.1's run-scoped target authorization. The scope is declared once here for the profiles the
# response-driven recipes drive, so a run that is pointed anywhere else is refused before it mutates
# anything (`hitl/stimuli/authorization.py` enforces it; the drivers record it in their report).
# Provenance: the user's standing authorization (2026-09-30: any slot may be cleared or used, without
# asking again) as narrowed to the bench target these legs were actually driven on -- track 8 with the
# scratch slots 4 and 5 (recorded device runs `s4f13-*`/`s4f22-dec096-*`). A session that needs a
# different bench scope forks the profile (`temporary_variant(..., authorization=...)`) rather than
# widening the canonical declaration.
BENCH_TRACK = 8
BENCH_SLOTS = (4, 5)
BENCH_SCOPE_SOURCE = (
    "user standing authorization 2026-09-30 (any slot may be cleared or used, no further asking) "
    "narrowed to this profile's bench target: track 8, slots 4 and 5"
)


def _bench_authorization() -> Authorization:
    return Authorization(track=BENCH_TRACK, slots=BENCH_SLOTS, source=BENCH_SCOPE_SOURCE,
                         notes="declared scope for the S4f.13/S4f.22 response-driven legs")

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
    authorization=_bench_authorization(),
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

STIM_NE_OD_COMPOSE_02 = StimulusSpec(
    stimulus_id="STIM-NE-OD-COMPOSE-02",
    version=1,
    kind=KIND_CANONICAL,
    scenario_id="note_edit_overdub_compose",
    description=(
        "BASE-2 + one overdub, then NOTE_EDIT, in-edit overdub at the selected "
        "seed pitch, and a same-pitch hold move that must Hide or Shorten"
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
            pattern="LIVE_CAPTURE_SAME_PITCH",
            note_low=None,
            note_high=None,
        ),
    ),
    interactions=("note_edit", "in_edit_overdub", "same_pitch_hold", "move_selected_note"),
    must_prove=(
        "base_recorded",
        "committed_od1",
        "in_edit_overdub_active",
        "live_capture_notes_present",
        "selected_note_moved",
        "same_pitch_hold_overlap",
    ),
    human_checks=(
        "same-pitch hold Hide or Shorten visible",
        "moved note visible",
        "no display corruption",
        "stop/restart behaves normally",
    ),
)

STIM_NE_OD_COMPOSE_03 = StimulusSpec(
    stimulus_id="STIM-NE-OD-COMPOSE-03",
    version=1,
    kind=KIND_CANONICAL,
    scenario_id="note_edit_overdub_compose_last_bar_shorten",
    description=(
        "Compose same-pitch live Capture, then move the seed onto the new note "
        "at beat 4 of the last bar so that note is Shortened"
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
            pattern="LIVE_CAPTURE_SAME_PITCH",
            note_low=None,
            note_high=None,
        ),
    ),
    interactions=(
        "note_edit",
        "in_edit_overdub",
        "same_pitch_hold",
        "shorten_new_note_last_bar",
        "move_selected_note",
    ),
    must_prove=(
        "base_recorded",
        "committed_od1",
        "in_edit_overdub_active",
        "live_capture_notes_present",
        "selected_note_moved",
        "new_note_shortened",
    ),
    human_checks=(
        "new note Shortened at last-bar beat 4",
        "moved note visible",
        "no display corruption",
        "stop/restart behaves normally",
    ),
)

STIM_NE_OD_COMPOSE_04 = StimulusSpec(
    stimulus_id="STIM-NE-OD-COMPOSE-04",
    version=1,
    kind=KIND_CANONICAL,
    scenario_id="note_edit_overdub_compose_last_bar_reopen",
    description=(
        "Last-bar Shorten, then exit NOTE_EDIT, re-enter, select a seed note, "
        "and move it"
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
            pattern="LIVE_CAPTURE_SAME_PITCH",
            note_low=None,
            note_high=None,
        ),
    ),
    interactions=(
        "note_edit",
        "in_edit_overdub",
        "same_pitch_hold",
        "shorten_new_note_last_bar",
        "move_selected_note",
        "reenter_note_edit",
        "select_and_move_after_reopen",
    ),
    must_prove=(
        "base_recorded",
        "committed_od1",
        "in_edit_overdub_active",
        "live_capture_notes_present",
        "selected_note_moved",
        "new_note_shortened",
        "reopen_note_edit_selected",
        "reopen_note_moved",
    ),
    human_checks=(
        "new note Shortened at last-bar beat 4",
        "reopen select lands on a note",
        "reopen move visible",
        "no display corruption",
    ),
)

STIM_BASE_2_2_01 = StimulusSpec(
    stimulus_id="STIM-BASE-2-2-01",
    version=1,
    kind=KIND_CANONICAL,
    scenario_id="base",
    description="2-bar record, 2-bar overdub, default second overdub, post-stop undo/redo",
    generator="canonical_baseline_legacy_args",
    base_bars=2,
    base_pattern="BASE-2-2",
    base_stimulus_id=None,
    overdubs=(
        OverdubRecipe(name="OD1", bars=2, pattern="BASELINE_OVERDUB", note_low=24, note_high=39),
        OverdubRecipe(name="OD2", bars=2, pattern="BASELINE_SECOND_OVERDUB", note_low=12, note_high=35),
    ),
    interactions=("undo_redo_after_last_overdub",),
    must_prove=("base_recorded", "committed_od1", "committed_od2"),
    human_checks=("record and both overdub layers audible",),
    authorization=_bench_authorization(),
)

#: The **mixed** response-driven leg's profile (task 5.1's *Mixed record predecessor and overdub successor*
#: scenario). Its two targets deliberately carry **different verified content preconditions**, which the
#: requirement *Scoped same-boot fixture preparation* demands ("Record and overdub fixtures MUST have different
#: verified content preconditions"): slot A is the **record** target and must be verified empty *and idle*
#: before the recipe's start press (`require_idle` -- an armed or recording target would consume the press), and
#: slot B is the **overdub** target and must carry committed, verified content whose seed receipt is confirmed
#: before it is overdubbed (`seed_take`). Its predecessor/successor shape is a property of the driver, not of a
#: legacy argument recipe, so the generator names the flow rather than a canonical argument builder.
STIM_BASE_2_MIXED_01 = StimulusSpec(
    stimulus_id="STIM-BASE-2-MIXED-01",
    version=1,
    kind=KIND_CANONICAL,
    scenario_id="accepted_sequence_mixed",
    description=(
        "S4f.13(c) mixed leg: a record predecessor (A verified empty and idle) and a seeded overdub "
        "successor (B's committed content verified), one switch and one stop request each"
    ),
    generator="flow:accepted_sequence(mixed)",
    base_bars=2,
    base_pattern="BASE-2-MIXED",
    base_stimulus_id=STIM_BASE_2_2_01.stimulus_id,
    overdubs=(
        OverdubRecipe(name="SUCCESSOR", bars=2, pattern="BASELINE_OVERDUB", note_low=24, note_high=39),
    ),
    interactions=(),
    must_prove=("predecessor_recorded", "successor_committed", "successor_overdub_started"),
    human_checks=("predecessor loop plays, then the successor's overdub layer is audible",),
    authorization=_bench_authorization(),
)

STIM_BASE_2_FIXTURE_01 = StimulusSpec(
    stimulus_id="STIM-BASE-2-FIXTURE-01",
    version=1,
    kind=KIND_CANONICAL,
    scenario_id="base",
    description="2-bar record-only EDIT_RECORD_FIXTURE seed; no overdub",
    generator="canonical_edit_minimal_base_legacy_args",
    base_bars=2,
    base_pattern="EDIT_RECORD_FIXTURE",
    base_stimulus_id=None,
    overdubs=(),
    interactions=(),
    must_prove=("base_recorded",),
    human_checks=("fixture seed plays",),
)

STIM_OCCUPY_PAINT_01 = StimulusSpec(
    stimulus_id="STIM-OCCUPY-PAINT-01",
    version=1,
    kind=KIND_CANONICAL,
    scenario_id="overdub_occupy_paint",
    description=(
        "4-bar seed, OD1 half-bar source phrase, OD2 occupy "
        "(same-start hide / +1/4 shorten / −1/2 hide), two undo then two redo"
    ),
    generator="canonical_occupy_paint_legacy_args",
    base_bars=4,
    base_pattern="OCCUPY_SEED",
    base_stimulus_id=None,
    overdubs=(
        OverdubRecipe(name="OD1", bars=4, pattern="OCCUPY_SOURCE_PHRASE", note_low=48, note_high=60),
        OverdubRecipe(name="OD2", bars=8, pattern="OCCUPY_PAINT", note_low=48, note_high=60),
    ),
    interactions=("undo_redo",),
    must_prove=("seed_recorded", "od1_source_phrase", "od2_occupy", "undo_redo"),
    human_checks=(
        "occupy hide/shorten visible",
        "undo restores source phrase",
        "redo reapplies occupy",
    ),
)

STIM_NE_RESELECT_STACKED_01 = StimulusSpec(
    stimulus_id="STIM-NE-RESELECT-STACKED-01",
    version=1,
    kind=KIND_CANONICAL,
    scenario_id="note_edit_reselect_stacked",
    description=(
        "EDIT_RECORD_FIXTURE seed, NOTE_EDIT, move a higher-pitch note onto "
        "an occupied sixteenth, deselect, then F1 intra-step reselect"
    ),
    generator="scenario:note_edit_reselect_stacked",
    base_bars=2,
    base_pattern="EDIT_RECORD_FIXTURE",
    base_stimulus_id=STIM_BASE_2_FIXTURE_01.stimulus_id,
    overdubs=(),
    interactions=("note_edit", "move_onto_occupied_step", "deselect", "reselect_stacked"),
    must_prove=(
        "note_edit_entered",
        "selected_note_moved",
        "stacked_reselect_after_deselect",
    ),
    human_checks=(
        "moved note reselectable on the stacked sixteenth",
        "first stacked note still selectable",
    ),
)

STIM_EDIT_OD_DURING_NE_01 = StimulusSpec(
    stimulus_id="STIM-EDIT-OD-DURING-NE-01",
    version=1,
    kind=KIND_CANONICAL,
    scenario_id="edit_overdub_during_note_edit",
    description=(
        "EDIT_RECORD_FIXTURE seed, pre-edit overdub 24–39, NOTE_EDIT, "
        "in-edit overdub 12–35, session undo/redo, exit, global undo"
    ),
    generator="scenario:edit_overdub_during_note_edit",
    base_bars=2,
    base_pattern="EDIT_RECORD_FIXTURE",
    base_stimulus_id=STIM_BASE_2_FIXTURE_01.stimulus_id,
    overdubs=(
        OverdubRecipe(name="OD1", bars=2, pattern="PRE_EDIT_OVERDUB", note_low=24, note_high=39),
        OverdubRecipe(name="IN_EDIT_OD", bars=2, pattern="IN_EDIT_OVERDUB", note_low=12, note_high=35),
    ),
    interactions=("note_edit", "in_edit_overdub", "session_undo_redo", "post_exit_global_undo"),
    must_prove=(
        "pre_edit_overdub",
        "note_edit_entered",
        "in_edit_overdub_active",
        "session_undo_redo",
    ),
    human_checks=(
        "pre-edit and in-edit layers separable",
        "session undo removes only the in-edit layer",
    ),
)

_STIMULI: dict[str, StimulusSpec] = {
    STIM_BASE_2_ONE_OD_01.stimulus_id: STIM_BASE_2_ONE_OD_01,
    STIM_BASE_2_2_01.stimulus_id: STIM_BASE_2_2_01,
    STIM_BASE_2_FIXTURE_01.stimulus_id: STIM_BASE_2_FIXTURE_01,
    STIM_BASE_2_MIXED_01.stimulus_id: STIM_BASE_2_MIXED_01,
    STIM_NE_OD_COMPOSE_01.stimulus_id: STIM_NE_OD_COMPOSE_01,
    STIM_NE_OD_COMPOSE_02.stimulus_id: STIM_NE_OD_COMPOSE_02,
    STIM_NE_OD_COMPOSE_03.stimulus_id: STIM_NE_OD_COMPOSE_03,
    STIM_NE_OD_COMPOSE_04.stimulus_id: STIM_NE_OD_COMPOSE_04,
    STIM_OCCUPY_PAINT_01.stimulus_id: STIM_OCCUPY_PAINT_01,
    STIM_EDIT_OD_DURING_NE_01.stimulus_id: STIM_EDIT_OD_DURING_NE_01,
    STIM_NE_RESELECT_STACKED_01.stimulus_id: STIM_NE_RESELECT_STACKED_01,
}

SCENARIO_STIMULUS: dict[str, str] = {
    "note_edit_overdub_compose": STIM_NE_OD_COMPOSE_02.stimulus_id,
    "note_edit_overdub_compose_last_bar_shorten": STIM_NE_OD_COMPOSE_03.stimulus_id,
    "note_edit_overdub_compose_last_bar_reopen": STIM_NE_OD_COMPOSE_04.stimulus_id,
    "overdub_occupy_paint": STIM_OCCUPY_PAINT_01.stimulus_id,
    "edit_overdub_during_note_edit": STIM_EDIT_OD_DURING_NE_01.stimulus_id,
    "note_edit_reselect_stacked": STIM_NE_RESELECT_STACKED_01.stimulus_id,
    "base": STIM_BASE_2_2_01.stimulus_id,
}


def stimulus_for_base_preset(preset: str | None) -> StimulusSpec | None:
    if preset in (
        "note_edit_overdub_compose",
        "note_edit_overdub_compose_last_bar_shorten",
        "note_edit_overdub_compose_last_bar_reopen",
    ):
        return STIM_BASE_2_ONE_OD_01
    if preset in ("edit_minimal", "edit_overdub_during_note_edit", "note_edit_reselect_stacked"):
        return STIM_BASE_2_FIXTURE_01
    return STIM_BASE_2_2_01


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



def authorization_for(stimulus_id: str) -> Authorization | None:
    """This profile's declared target scope, or None when it declares none.

    The registry is the manifest's authority (task 4.1): a profile that has not declared a
    scope cannot authorize a run, and `hitl.stimuli.authorization` refuses rather than
    falling back to an unchecked CLI scope when a manifest is named.
    """
    return get_stimulus(stimulus_id).authorization


def _authorization_text(spec: StimulusSpec) -> str:
    declared = spec.authorization
    return "undeclared" if declared is None else declared.targets_text()

def format_stimulus_list_lines() -> list[str]:
    return [
        f"{spec.fingerprint_key()}  {spec.scenario_id}  {spec.generator}  "
        f"targets={_authorization_text(spec)}  {spec.description}"
        for spec in list_stimuli()
    ]


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
