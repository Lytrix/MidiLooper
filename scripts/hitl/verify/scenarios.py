"""Layered verify dispatch for legacy scenario verify functions."""

from __future__ import annotations

from hitl.context import ScenarioContext
from hitl.layered_legacy_bridge import LayeredLegacyBridge
from hitl.verify.legacy_adapter import verification_from_legacy_dict
from hitl.verify.result import VerificationFailure, VerificationResult


def verify_scenario(lines: list[str], ctx: ScenarioContext) -> VerificationResult:
    bridge = LayeredLegacyBridge.from_config(ctx.action.config)
    scenario_id = ctx.scenario_id

    if scenario_id == "edit_full":
        from hitl.scenarios.edit_full import verify_edit_full_scenario

        args = bridge.edit_full_args(ctx)
        return verification_from_legacy_dict(verify_edit_full_scenario(lines, args))

    return VerificationResult(
        passed=False,
        failures=[
            VerificationFailure(
                check="scenario_verifier",
                expected="edit_full",
                observed=scenario_id,
            )
        ],
    )
