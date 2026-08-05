"""Display window serial verification for layered HITL."""

from __future__ import annotations

from hitl.context import ScenarioContext
from hitl.verify.legacy_adapter import verification_from_legacy_dict
from hitl.verify.result import VerificationResult


def verify_display(lines: list[str], ctx: ScenarioContext) -> VerificationResult:
    from hitl.scenarios.long_loop_display_window import verify_long_loop_display_window
    from hitl.layered_legacy_bridge import LayeredLegacyBridge

    bridge = LayeredLegacyBridge.from_config(ctx.action.config)
    args = bridge.long_loop_display_args(ctx)
    return verification_from_legacy_dict(verify_long_loop_display_window(lines, args))
