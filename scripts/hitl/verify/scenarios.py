"""Layered verify dispatch for legacy scenario verify functions (Phase 3)."""

from __future__ import annotations

from hitl.context import ScenarioContext
from hitl.layered_legacy_bridge import LayeredLegacyBridge
from hitl.verify.legacy_adapter import verification_from_legacy_dict
from hitl.verify.result import VerificationFailure, VerificationResult


def verify_scenario(lines: list[str], ctx: ScenarioContext) -> VerificationResult:
    bridge = LayeredLegacyBridge.from_config(ctx.action.config)
    scenario_id = ctx.scenario_id

    if scenario_id == "two_overdub_undo_redo":
        from hitl.verify.two_overdub_undo_redo import verify_two_overdub_undo_redo

        args = bridge.two_overdub_undo_redo_args(ctx)
        return verification_from_legacy_dict(verify_two_overdub_undo_redo(lines, args))

    if scenario_id == "edit_overdub_during_note_edit":
        from hitl.verify.edit_overdub_during_note_edit import verify_edit_overdub_during_note_edit

        args = bridge.edit_overdub_during_note_edit_args(ctx)
        return verification_from_legacy_dict(verify_edit_overdub_during_note_edit(lines, args))

    if scenario_id == "note_edit_select_dependent_faders":
        from hitl.scenarios.note_edit_select_dependent_faders import (
            verify_note_edit_select_dependent_faders,
        )

        args = bridge.note_edit_select_dependent_faders_args(ctx)
        return verification_from_legacy_dict(verify_note_edit_select_dependent_faders(lines, args))

    if scenario_id == "current_set_incremental_save":
        from hitl.verify.current_set_incremental_save import verify_current_set_incremental_save

        args = bridge.current_set_incremental_save_args(ctx)
        return verification_from_legacy_dict(verify_current_set_incremental_save(lines, args))

    return VerificationResult(
        passed=False,
        failures=[
            VerificationFailure(
                check="scenario_verifier",
                expected="registered Phase 3 scenario",
                observed=scenario_id,
            )
        ],
    )
