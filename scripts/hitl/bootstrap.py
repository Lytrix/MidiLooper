"""Construct HitlSession and scenario contexts from HitlConfig."""

from __future__ import annotations

from datetime import datetime, timezone
from pathlib import Path

from hitl.config import HitlConfig
from hitl.context import ActionContext, ScenarioContext
from hitl.session import HitlSession, open_midi_session


def build_session(config: HitlConfig) -> HitlSession:
    if config.verify_only:
        return HitlSession(out_port=None, in_port=None, collector=None)
    return open_midi_session(
        midi_out_name=config.midi_out_name,
        midi_in_name=config.midi_in_name,
        serial_port=config.serial_port,
    )


def action_context(config: HitlConfig, session: HitlSession) -> ActionContext:
    return ActionContext(session=session, config=config)


def scenario_context(
    config: HitlConfig,
    session: HitlSession,
    scenario_id: str,
) -> ScenarioContext:
    return ScenarioContext(
        action=ActionContext(session=session, config=config),
        scenario_id=scenario_id,
        out_dir=Path(config.out_dir),
        started_at=datetime.now(timezone.utc),
    )
