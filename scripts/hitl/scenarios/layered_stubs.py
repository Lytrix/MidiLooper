"""Layered scenario stubs (Phase 1 placeholders; wired in Phase 2)."""

from __future__ import annotations

from hitl import flows
from hitl.context import ScenarioContext
from hitl.types import ScenarioResult


def _result_from(ctx: ScenarioContext, *, flow_name: str) -> ScenarioResult:
    return ScenarioResult(
        observations={"flow": flow_name},
        markers=list(ctx.action.session.markers),
        artifacts={},
    )


def run_record_seed_stub(ctx: ScenarioContext) -> ScenarioResult:
    flows.record_seed(ctx.action)
    return _result_from(ctx, flow_name="record_seed")


def run_record_overdub_stub(ctx: ScenarioContext) -> ScenarioResult:
    flows.record_overdub(ctx.action)
    return _result_from(ctx, flow_name="record_overdub")


def run_edit_minimal_stub(ctx: ScenarioContext) -> ScenarioResult:
    flows.note_edit_smoke(ctx.action)
    return _result_from(ctx, flow_name="note_edit_smoke")


def run_long_loop_display_window_stub(ctx: ScenarioContext) -> ScenarioResult:
    flows.long_loop_display(ctx.action)
    return _result_from(ctx, flow_name="long_loop_display")


def run_slot_queued_start_stub(ctx: ScenarioContext) -> ScenarioResult:
    flows.queued_switch(ctx.action)
    return _result_from(ctx, flow_name="queued_switch")
