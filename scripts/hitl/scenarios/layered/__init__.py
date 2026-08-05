"""Layered scenario implementations (Phase 2)."""

from __future__ import annotations

from hitl.context import ScenarioContext
from hitl.layered_legacy_bridge import LayeredLegacyBridge
from hitl.types import ScenarioResult

_BRIDGE: LayeredLegacyBridge | None = None


def set_bridge(bridge: LayeredLegacyBridge) -> None:
    global _BRIDGE
    _BRIDGE = bridge


def _bridge(ctx: ScenarioContext) -> LayeredLegacyBridge:
    if _BRIDGE is None:
        raise RuntimeError("LayeredLegacyBridge not set — foundation_runner must call set_bridge")
    return _BRIDGE


def _result(ctx: ScenarioContext, *, exit_code: int, flow: str) -> ScenarioResult:
    return ScenarioResult(
        observations={"exit_code": exit_code, "flow": flow},
        markers=list(ctx.action.session.markers),
        artifacts={},
    )


def run_record_overdub(ctx: ScenarioContext) -> ScenarioResult:
    from hitl.scenarios.base import run_base_scenario

    bridge = _bridge(ctx)
    args = bridge.record_overdub_args(ctx)
    code = run_base_scenario(args)
    return _result(ctx, exit_code=code, flow="record_overdub")


def run_edit_full(ctx: ScenarioContext) -> ScenarioResult:
    from hitl.scenarios.edit_full import run_edit_full_scenario

    bridge = _bridge(ctx)
    args = bridge.edit_full_args(ctx)
    args.hitl_session = ctx.action.session
    code = run_edit_full_scenario(args)
    return _result(ctx, exit_code=code, flow="edit_full")
