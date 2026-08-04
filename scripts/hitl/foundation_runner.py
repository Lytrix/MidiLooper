"""Foundation orchestration loop for layered HITL (Phase 1+)."""

from __future__ import annotations

from pathlib import Path

from hitl import bootstrap, reporting
from hitl.config import HitlConfig
from hitl.layered_legacy_bridge import LayeredLegacyBridge
from hitl.layered_registry import (
    LayeredScenarioSpec,
    get_layered_registry,
    resolve_preset_scenario_ids,
    verify,
)
from hitl.session import HitlSession
from hitl.types import ScenarioResult
from hitl.verify.result import VerificationFailure, VerificationResult


def _read_serial_lines(config: HitlConfig) -> list[str]:
    if config.verify_serial_log is None:
        return []
    return config.verify_serial_log.read_text(encoding="utf-8", errors="replace").splitlines()


def _resolve_serial_lines(config: HitlConfig, session: bootstrap.HitlSession) -> list[str]:
    if config.verify_serial_log is not None and config.verify_serial_log.is_file():
        return _read_serial_lines(config)
    if session.collector is not None:
        lines = session.collector.snapshot()
        if lines:
            return lines
    if config.uses_external_serial_capture:
        from hitl.serial_follow import ExternalSerialFollowCollector

        follow_path = (
            config.follow_serial_log
            if config.follow_serial_log is not None
            else ExternalSerialFollowCollector.resolve_current_session_path(config.out_dir)
        )
        if follow_path.is_file():
            return follow_path.read_text(encoding="utf-8", errors="replace").splitlines()
    return []


def run_layered_scenario(
    spec: LayeredScenarioSpec,
    config: HitlConfig,
    *,
    session: HitlSession | None = None,
    owns_session: bool = True,
) -> int:
    local_session = session or bootstrap.build_session(config)
    scenario_ctx = bootstrap.scenario_context(config, local_session, spec.id)
    try:
        result: ScenarioResult | None = None
        run_code = 0
        if config.verify_only:
            lines = _read_serial_lines(config)
        else:
            if spec.run is None:
                raise RuntimeError(f"Scenario {spec.id!r} has no run handler")
            print(f"[hitl-layered] running scenario: {spec.id}")
            result = spec.run(scenario_ctx)
            run_code = int(result.observations.get("exit_code", 0))
            if run_code != 0:
                verification = VerificationResult(
                    passed=False,
                    failures=[
                        VerificationFailure(
                            check="scenario_run",
                            expected="exit 0",
                            observed=str(run_code),
                        )
                    ],
                )
                reporting.write_scenario_report(
                    scenario_id=spec.id,
                    result=result,
                    verification=verification,
                    scenario_ctx=scenario_ctx,
                    out_dir=Path(config.out_dir),
                )
                print(f"[hitl-layered] scenario {spec.id} failed (exit {run_code})")
                return run_code
            lines = _resolve_serial_lines(config, local_session)
        verification = verify(spec.verifier_id, lines, scenario_ctx)
        reporting.write_scenario_report(
            scenario_id=spec.id,
            result=result,
            verification=verification,
            scenario_ctx=scenario_ctx,
            out_dir=Path(config.out_dir),
        )
        if verification.ok:
            print(f"[hitl-layered] scenario {spec.id}: PASS")
        else:
            print(f"[hitl-layered] scenario {spec.id}: FAIL — {verification.failures}")
        return reporting.exit_code_from_verification(verification)
    finally:
        if owns_session:
            local_session.close()


def run_layered_preset(config: HitlConfig) -> int:
    if config.needs_managed_capture():
        from hitl.managed_capture import run_with_managed_capture

        return run_with_managed_capture(config, _run_layered_preset_inner)
    return _run_layered_preset_inner(config)


def _run_layered_preset_inner(config: HitlConfig) -> int:
    if not config.preset and not config.scenario_ids:
        raise ValueError("HitlConfig.preset or scenario_ids is required for run_layered_preset")
    scenario_ids = (
        config.scenario_ids
        if config.scenario_ids
        else resolve_preset_scenario_ids(config.preset or "")
    )
    registry = get_layered_registry()
    bridge = LayeredLegacyBridge.from_config(config)
    from hitl.scenarios import layered as layered_scenarios

    layered_scenarios.set_bridge(bridge)

    print(
        f"[hitl-layered] preset={config.preset!r} "
        f"track={config.track_number} loop_slot={config.loop_slot} "
        f"midi_channel={config.midi_channel}",
        flush=True,
    )

    session = bootstrap.build_session(config)
    exit_code = 0
    try:
        for scenario_id in scenario_ids:
            spec = registry[scenario_id]
            code = run_layered_scenario(spec, config, session=session, owns_session=False)
            exit_code = max(exit_code, code)
            if code != 0:
                print(f"[hitl-layered] preset aborted after {scenario_id!r} (exit {code})")
                break
    finally:
        session.close()
    return exit_code
