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
    "edit_minimal": ("edit_record_prelude", "edit_minimal"),
    "edit_overdub_during_note_edit": ("edit_overdub_during_note_edit",),
    "long_loop_display_window": ("long_loop_display_window",),
    "load_save_display": ("load_save_display",),
    "load_save_overlay_scroll": ("load_save_overlay_scroll",),
    "load_save_overlay_scroll_only": ("load_save_overlay_scroll_only",),
    "load_save_overlay_load": ("load_save_overlay_load",),
    "current_set_incremental_save": ("current_set_incremental_save",),
    "revision_commit_save": ("revision_commit_save",),
    "revision_load": ("revision_load",),
    "revision_load_record": ("base", "revision_load_post_record"),
    "revision_load_dirty_yes": ("base", "revision_load_dirty_yes"),
    "revision_load_dirty_no": ("base", "revision_load_dirty_no"),
    "revision_load_dirty_cancel": ("base", "revision_load_dirty_cancel"),
    "two_overdub_undo_redo": ("two_overdub_undo_redo",),
}


def _lazy_registry() -> dict[str, ScenarioSpec]:
    from hitl.scenarios.base import run_base_scenario, verify_base_scenario
    from hitl.scenarios.edit_full import run_edit_full_scenario, verify_edit_full_scenario
    from hitl.scenarios.edit_minimal import run_edit_minimal_scenario, verify_edit_minimal_scenario
    from hitl.scenarios.edit_overdub_during_note_edit import (
        run_edit_overdub_during_note_edit,
        verify_edit_overdub_during_note_edit,
    )
    from hitl.scenarios.edit_record_prelude import (
        run_edit_record_prelude,
        verify_edit_record_prelude,
    )
    from hitl.scenarios.long_loop_display_window import (
        run_long_loop_display_window,
        verify_long_loop_display_window,
    )
    from hitl.scenarios.load_save_display import run_load_save_display
    from hitl.verify.load_save_display import verify_load_save_display
    from hitl.scenarios.load_save_overlay_scroll import (
        run_load_save_overlay_scroll,
        run_load_save_overlay_scroll_only,
    )
    from hitl.verify.load_save_overlay_scroll import verify_load_save_overlay_scroll
    from hitl.scenarios.load_save_overlay_load import run_load_save_overlay_load
    from hitl.verify.load_save_overlay_load import verify_load_save_overlay_load
    from hitl.scenarios.current_set_incremental_save import run_current_set_incremental_save
    from hitl.verify.current_set_incremental_save import verify_current_set_incremental_save
    from hitl.scenarios.revision_commit_save import run_revision_commit_save
    from hitl.verify.revision_commit_save import verify_revision_commit_save
    from hitl.scenarios.revision_load import run_revision_load
    from hitl.scenarios.revision_load_post_record import run_revision_load_post_record
    from hitl.scenarios.revision_load_dirty import (
        run_revision_load_dirty_cancel,
        run_revision_load_dirty_no,
        run_revision_load_dirty_yes,
    )
    from hitl.verify.revision_load import verify_revision_load
    from hitl.verify.revision_load_dirty import verify_revision_load_dirty
    from hitl.scenarios.two_overdub_undo_redo import run_two_overdub_undo_redo
    from hitl.verify.two_overdub_undo_redo import verify_two_overdub_undo_redo

    return {
        "base": ScenarioSpec(
            scenario_id="base",
            description="Record/overdub baseline (legacy host_midi_automation_baseline)",
            run=run_base_scenario,
            verify=verify_base_scenario,
        ),
        "edit_record_prelude": ScenarioSpec(
            scenario_id="edit_record_prelude",
            description="2-bar fixture record prelude for edit scenarios",
            run=run_edit_record_prelude,
            verify=verify_edit_record_prelude,
        ),
        "edit_full": ScenarioSpec(
            scenario_id="edit_full",
            description="Full edit overlap suite (legacy edit baseline body)",
            run=run_edit_full_scenario,
            verify=verify_edit_full_scenario,
        ),
        "edit_minimal": ScenarioSpec(
            scenario_id="edit_minimal",
            description="Fast edit smoke: add/delete/move/length then exit",
            run=run_edit_minimal_scenario,
            verify=verify_edit_minimal_scenario,
        ),
        "edit_overdub_during_note_edit": ScenarioSpec(
            scenario_id="edit_overdub_during_note_edit",
            description="Record, overdub, edit, in-edit overdub, session undo, exit, global undo",
            run=run_edit_overdub_during_note_edit,
            verify=verify_edit_overdub_during_note_edit,
        ),
        "long_loop_display_window": ScenarioSpec(
            scenario_id="long_loop_display_window",
            description="Long loop: NOTE_EDIT window freeze, play/stop long-press snap, hold-to-track",
            run=run_long_loop_display_window,
            verify=verify_long_loop_display_window,
        ),
        "load_save_display": ScenarioSpec(
            scenario_id="load_save_display",
            description="Play/Stop double-press enters and exits load/save set browser",
            run=run_load_save_display,
            verify=verify_load_save_display,
        ),
        "load_save_overlay_scroll": ScenarioSpec(
            scenario_id="load_save_overlay_scroll",
            description="Serial overlay scroll + dirty-prompt cancel (!OVERLAY_* hooks)",
            run=run_load_save_overlay_scroll,
            verify=verify_load_save_overlay_scroll,
        ),
        "load_save_overlay_scroll_only": ScenarioSpec(
            scenario_id="load_save_overlay_scroll_only",
            description="Root overlay scroll only; 3s/row + 12s dwell; no record/dirty phase",
            run=run_load_save_overlay_scroll_only,
            verify=verify_load_save_overlay_scroll,
        ),
        "load_save_overlay_load": ScenarioSpec(
            scenario_id="load_save_overlay_load",
            description="MIDI overlay load Set row (Edit enter, Record scroll, Edit short confirm)",
            run=run_load_save_overlay_load,
            verify=verify_load_save_overlay_load,
        ),
        "current_set_incremental_save": ScenarioSpec(
            scenario_id="current_set_incremental_save",
            description="Transport-stop idle save w0_s64; record-stop incremental w1_s63",
            run=run_current_set_incremental_save,
            verify=verify_current_set_incremental_save,
        ),
        "revision_commit_save": ScenarioSpec(
            scenario_id="revision_commit_save",
            description="HITL revision commit (!REV_COMMIT) with catalog cleanup (!REV_CLEANUP)",
            run=run_revision_commit_save,
            verify=verify_revision_commit_save,
        ),
        "revision_load": ScenarioSpec(
            scenario_id="revision_load",
            description="HITL revision commit, load (!REV_LOAD), catalog cleanup (!REV_CLEANUP)",
            run=run_revision_load,
            verify=verify_revision_load,
        ),
        "revision_load_post_record": ScenarioSpec(
            scenario_id="revision_load_post_record",
            description="Revision commit/load after record baseline (no transport-stop prelude)",
            run=run_revision_load_post_record,
            verify=verify_revision_load,
        ),
        "revision_load_dirty_yes": ScenarioSpec(
            scenario_id="revision_load_dirty_yes",
            description="Dirty prompt Yes: save-then-load after second record",
            run=run_revision_load_dirty_yes,
            verify=verify_revision_load_dirty,
        ),
        "revision_load_dirty_no": ScenarioSpec(
            scenario_id="revision_load_dirty_no",
            description="Dirty prompt No: discard uncommitted and load",
            run=run_revision_load_dirty_no,
            verify=verify_revision_load_dirty,
        ),
        "revision_load_dirty_cancel": ScenarioSpec(
            scenario_id="revision_load_dirty_cancel",
            description="Dirty prompt Cancel: abort staged load",
            run=run_revision_load_dirty_cancel,
            verify=verify_revision_load_dirty,
        ),
        "two_overdub_undo_redo": ScenarioSpec(
            scenario_id="two_overdub_undo_redo",
            description="Record + 2 overdub passes + undo to display-empty + redo restore",
            run=run_two_overdub_undo_redo,
            verify=verify_two_overdub_undo_redo,
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
