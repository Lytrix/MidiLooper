"""Playback and edit serial verification for layered HITL."""

from __future__ import annotations

from hitl.context import ScenarioContext
from hitl.serial.protocol import count_track_transitions
from hitl.verify.legacy_adapter import verification_from_legacy_dict
from hitl.verify.result import VerificationFailure, VerificationResult


def verify_playback(lines: list[str], ctx: ScenarioContext) -> VerificationResult:
    if ctx.scenario_id == "edit_minimal":
        from hitl.scenarios.edit_minimal import verify_edit_minimal_scenario

        from hitl.layered_legacy_bridge import LayeredLegacyBridge

        bridge = LayeredLegacyBridge.from_config(ctx.action.config)
        args = bridge.edit_minimal_args(ctx)
        return verification_from_legacy_dict(verify_edit_minimal_scenario(lines, args))

    if ctx.scenario_id == "slot_queued_start":
        return _verify_slot_queued_start(lines, ctx)

    return VerificationResult(
        passed=False,
        failures=[
            VerificationFailure(
                check="playback_verifier",
                expected="known scenario",
                observed=ctx.scenario_id,
            )
        ],
    )


def _verify_slot_queued_start(lines: list[str], ctx: ScenarioContext) -> VerificationResult:
    failures: list[VerificationFailure] = []
    transitions = count_track_transitions(lines)
    playing_entries = transitions.get(("PLAYING", "PLAYING"), 0)
    disp_rows = sum(1 for line in lines if ",DISP," in line)
    slot_press = any("slot_queued_start:pressed_slot=" in m for m in ctx.action.markers)
    if not slot_press:
        failures.append(
            VerificationFailure(
                check="slot_press_marker",
                expected="slot short-press marker",
                observed=str(ctx.action.markers),
            )
        )
    if disp_rows < 1:
        failures.append(
            VerificationFailure(
                check="disp_rows",
                expected="at least one DISP row after slot switch",
                observed=str(disp_rows),
            )
        )
    return VerificationResult(
        passed=not failures,
        failures=failures,
        metrics={"disp_rows": disp_rows, "playing_self_transitions": playing_entries},
    )
