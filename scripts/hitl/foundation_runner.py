"""Foundation orchestration loop for layered HITL (Phase 1+)."""

from __future__ import annotations

from pathlib import Path

from hitl import bootstrap, reporting
from hitl.config import HitlConfig
from hitl.layered_registry import (
    LayeredScenarioSpec,
    get_layered_registry,
    resolve_preset_scenario_ids,
    verify,
)
from hitl.types import ScenarioResult


def _read_serial_lines(config: HitlConfig) -> list[str]:
    if config.verify_serial_log is None:
        return []
    return config.verify_serial_log.read_text(encoding="utf-8", errors="replace").splitlines()


def run_layered_scenario(
    spec: LayeredScenarioSpec,
    config: HitlConfig,
) -> int:
    session = bootstrap.build_session(config)
    scenario_ctx = bootstrap.scenario_context(config, session, spec.id)
    try:
        if config.verify_only:
            lines = _read_serial_lines(config)
            result: ScenarioResult | None = None
        else:
            if spec.run is None:
                raise RuntimeError(f"Scenario {spec.id!r} has no run handler")
            result = spec.run(scenario_ctx)
            lines = session.snapshot_lines()
        verification = verify(spec.verifier_id, lines, scenario_ctx)
        reporting.write_scenario_report(
            scenario_id=spec.id,
            result=result,
            verification=verification,
            scenario_ctx=scenario_ctx,
            out_dir=Path(config.out_dir),
        )
        return reporting.exit_code_from_verification(verification)
    finally:
        session.close()


def run_layered_preset(config: HitlConfig) -> int:
    if not config.preset:
        raise ValueError("HitlConfig.preset is required for run_layered_preset")
    scenario_ids = (
        config.scenario_ids
        if config.scenario_ids
        else resolve_preset_scenario_ids(config.preset)
    )
    registry = get_layered_registry()
    exit_code = 0
    for scenario_id in scenario_ids:
        spec = registry[scenario_id]
        code = run_layered_scenario(spec, config)
        exit_code = max(exit_code, code)
        if code != 0:
            break
    return exit_code
