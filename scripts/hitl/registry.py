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
    #: True when the scenario owns its own device session (capture + MIDI guard). The runner then does not
    #: wrap it in managed capture: two owners of one serial port is not a compatibility question, it is a
    #: broken run. The response-driven acceptance legs declare it because their sequence owns its own
    #: correlation, cancellation and report.
    owns_capture: bool = False


PRESET_SCENARIOS: dict[str, tuple[str, ...]] = {
    "base": ("base",),
    "edit_full": ("edit_record_prelude", "edit_full"),
    "overdub_occupy_paint": ("overdub_occupy_paint",),
    "note_edit_overdub_compose": ("base", "note_edit_overdub_compose"),
    "note_edit_overdub_compose_last_bar_shorten": (
        "base",
        "note_edit_overdub_compose_last_bar_shorten",
    ),
    "note_edit_overdub_compose_last_bar_reopen": (
        "base",
        "note_edit_overdub_compose_last_bar_reopen",
    ),
    "edit_overdub_during_note_edit": ("base", "edit_overdub_during_note_edit"),
    "note_edit_reselect_stacked": ("base", "note_edit_reselect_stacked"),
    # Task 4.4: the two response-driven acceptance legs. Both are capture-owning (see `ScenarioSpec`), so
    # the preset drives them in order through the wrapper's own sessions, one leg per boot anchor.
    "accepted_sequence": ("accepted_sequence_rec", "accepted_sequence_od"),
}


def _lazy_registry() -> dict[str, ScenarioSpec]:
    from hitl.scenarios.base import run_base_scenario, verify_base_scenario
    from hitl.scenarios.edit_full import run_edit_full_scenario, verify_edit_full_scenario
    from hitl.scenarios.edit_record_prelude import (
        run_edit_record_prelude,
        verify_edit_record_prelude,
    )
    from hitl.scenarios.overdub_occupy_paint import (
        run_overdub_occupy_paint,
        verify_overdub_occupy_paint,
    )
    from hitl.scenarios.note_edit_overdub_compose import (
        run_note_edit_overdub_compose,
        verify_note_edit_overdub_compose,
    )
    from hitl.scenarios.edit_overdub_during_note_edit import (
        run_edit_overdub_during_note_edit,
        verify_edit_overdub_during_note_edit,
    )
    from hitl.scenarios.note_edit_reselect_stacked import (
        run_note_edit_reselect_stacked,
        verify_note_edit_reselect_stacked,
    )
    # Task 4.4: the two response-driven acceptance legs (capture-owning; see `ScenarioSpec`).
    from hitl.scenarios.accepted_sequence import (
        run_accepted_sequence,
        verify_accepted_sequence,
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
        "overdub_occupy_paint": ScenarioSpec(
            scenario_id="overdub_occupy_paint",
            description=(
                "Clear/arm, seed record, first overdub half-bar phrase, second "
                "overdub occupy (same-start hide / +1/4 shorten / -1/2 hide), "
                "two undo then two redo"
            ),
            run=run_overdub_occupy_paint,
            verify=verify_overdub_occupy_paint,
        ),
        "note_edit_overdub_compose": ScenarioSpec(
            scenario_id="note_edit_overdub_compose",
            description=(
                "After 2-bar record plus one overdub: NOTE_EDIT open, in-edit "
                "overdub, move the selected note, and keep live Capture notes "
                "on the roll"
            ),
            run=run_note_edit_overdub_compose,
            verify=verify_note_edit_overdub_compose,
        ),
        "note_edit_overdub_compose_last_bar_shorten": ScenarioSpec(
            scenario_id="note_edit_overdub_compose_last_bar_shorten",
            description=(
                "Compose same-pitch live Capture, then move the seed onto the "
                "new note at beat 4 of the last bar and require Shorten"
            ),
            run=run_note_edit_overdub_compose,
            verify=verify_note_edit_overdub_compose,
        ),
        "note_edit_overdub_compose_last_bar_reopen": ScenarioSpec(
            scenario_id="note_edit_overdub_compose_last_bar_reopen",
            description=(
                "Last-bar Shorten, then exit NOTE_EDIT, re-enter, select a "
                "seed note, and move it"
            ),
            run=run_note_edit_overdub_compose,
            verify=verify_note_edit_overdub_compose,
        ),
        "edit_overdub_during_note_edit": ScenarioSpec(
            scenario_id="edit_overdub_during_note_edit",
            description=(
                "EDIT_RECORD_FIXTURE seed, pre-edit overdub, NOTE_EDIT, "
                "in-edit overdub, session undo/redo, exit, global undo"
            ),
            run=run_edit_overdub_during_note_edit,
            verify=verify_edit_overdub_during_note_edit,
        ),
        "note_edit_reselect_stacked": ScenarioSpec(
            scenario_id="note_edit_reselect_stacked",
            description=(
                "EDIT_RECORD_FIXTURE seed, NOTE_EDIT, move onto an occupied "
                "sixteenth, deselect, and F1 intra-step reselect"
            ),
            run=run_note_edit_reselect_stacked,
            verify=verify_note_edit_reselect_stacked,
        ),
        "accepted_sequence_rec": ScenarioSpec(
            scenario_id="accepted_sequence_rec",
            description=(
                "S4f.13(a) record leg: prepare two targets, real record mode, "
                "a slot switch that carries the first target's stop, one stop per interval"
            ),
            run=run_accepted_sequence,
            verify=verify_accepted_sequence,
            owns_capture=True,
        ),
        "accepted_sequence_od": ScenarioSpec(
            scenario_id="accepted_sequence_od",
            description=(
                "S4f.13(b) overdub leg: confirmed seeds, the proven single capture-switch "
                "gesture, the successor's recognized overdub start and stop"
            ),
            run=run_accepted_sequence,
            verify=verify_accepted_sequence,
            owns_capture=True,
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
