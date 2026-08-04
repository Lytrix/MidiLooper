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


def run_record_seed(ctx: ScenarioContext) -> ScenarioResult:
    from hitl.scenarios.base import run_base_scenario

    bridge = _bridge(ctx)
    args = bridge.record_seed_args(ctx)
    code = run_base_scenario(args)
    run_ctx = getattr(args, "hitl_context")
    if code == 0 and run_ctx.base_preset_passed:
        ctx.action.markers.append("record_seed:base_ok")
    return _result(ctx, exit_code=code, flow="record_seed")


def run_record_overdub(ctx: ScenarioContext) -> ScenarioResult:
    from hitl.scenarios.base import run_base_scenario

    bridge = _bridge(ctx)
    args = bridge.record_overdub_args(ctx)
    code = run_base_scenario(args)
    return _result(ctx, exit_code=code, flow="record_overdub")


def run_edit_minimal(ctx: ScenarioContext) -> ScenarioResult:
    from hitl.scenarios.edit_minimal import run_edit_minimal_scenario

    bridge = _bridge(ctx)
    args = bridge.edit_minimal_args(ctx)
    code = run_edit_minimal_scenario(args)
    return _result(ctx, exit_code=code, flow="edit_minimal")


def run_long_loop_display_window(ctx: ScenarioContext) -> ScenarioResult:
    from hitl.scenarios.long_loop_display_window import run_long_loop_display_window

    bridge = _bridge(ctx)
    args = bridge.long_loop_display_args(ctx)
    code = run_long_loop_display_window(args)
    return _result(ctx, exit_code=code, flow="long_loop_display_window")


def run_slot_queued_start(ctx: ScenarioContext) -> ScenarioResult:
    import time

    import mido

    from hitl.control_constants import CONTROL_CHANNEL_1BASED, LOOP_SELECT_NOTE_BASE
    from hitl.edit_controls import _ensure_transport_running
    from hitl.midi_io import _send_short_press

    config = ctx.action.config
    session = ctx.action.session
    out_port = session.out_port
    in_port = session.in_port
    press_ms = config.press_ms
    phase_ms = config.phase_wait_ms

    _ensure_transport_running(
        out_port,
        in_port,
        press_ms=press_ms,
        phase_wait_ms=phase_ms,
        serial_collector=session.collector,
        transport_args=config,
    )
    time.sleep(phase_ms / 1000.0)

    active_slot_index = config.loop_slot - 1
    target_slot_index = 1 if active_slot_index == 0 else 0
    slot_note = LOOP_SELECT_NOTE_BASE + target_slot_index
    _send_short_press(
        out_port,
        note=slot_note,
        channel_1based=CONTROL_CHANNEL_1BASED,
        press_ms=press_ms,
    )
    ctx.action.markers.append(f"slot_queued_start:pressed_slot={target_slot_index + 1}")
    time.sleep(max(phase_ms * 4, 2000) / 1000.0)

    return _result(ctx, exit_code=0, flow="slot_queued_start")


def run_two_overdub_undo_redo(ctx: ScenarioContext) -> ScenarioResult:
    from hitl.scenarios.two_overdub_undo_redo import run_two_overdub_undo_redo

    bridge = _bridge(ctx)
    args = bridge.two_overdub_undo_redo_args(ctx)
    code = run_two_overdub_undo_redo(args)
    return _result(ctx, exit_code=code, flow="two_overdub_undo_redo")


def run_edit_overdub_during_note_edit(ctx: ScenarioContext) -> ScenarioResult:
    from hitl.scenarios.edit_overdub_during_note_edit import run_edit_overdub_during_note_edit

    bridge = _bridge(ctx)
    args = bridge.edit_overdub_during_note_edit_args(ctx)
    code = run_edit_overdub_during_note_edit(args)
    return _result(ctx, exit_code=code, flow="edit_overdub_during_note_edit")


def run_note_edit_select_dependent_faders(ctx: ScenarioContext) -> ScenarioResult:
    from hitl.scenarios.note_edit_select_dependent_faders import (
        run_note_edit_select_dependent_faders,
    )

    bridge = _bridge(ctx)
    args = bridge.note_edit_select_dependent_faders_args(ctx)
    code = run_note_edit_select_dependent_faders(args)
    return _result(ctx, exit_code=code, flow="note_edit_select_dependent_faders")


def run_current_set_incremental_save(ctx: ScenarioContext) -> ScenarioResult:
    from hitl.scenarios.current_set_incremental_save import run_current_set_incremental_save

    bridge = _bridge(ctx)
    args = bridge.current_set_incremental_save_args(ctx)
    code = run_current_set_incremental_save(args)
    return _result(ctx, exit_code=code, flow="current_set_incremental_save")
