"""Capture-phase serial verification for layered HITL."""

from __future__ import annotations

import argparse
from pathlib import Path

from hitl.baseline_loop_inventory import latest_base_report
from hitl.context import ScenarioContext
from hitl.serial.protocol import count_track_transitions, extract_recs_stop_lengths
from hitl.verify.legacy_adapter import verification_from_legacy_dict
from hitl.verify.result import VerificationFailure, VerificationResult


def verify_capture(lines: list[str], ctx: ScenarioContext) -> VerificationResult:
    scenario_id = ctx.scenario_id
    if scenario_id == "record_overdub":
        return _verify_record_overdub(lines, ctx)
    if scenario_id == "record_seed":
        return _verify_record_seed(lines, ctx)
    return VerificationResult(
        passed=False,
        failures=[
            VerificationFailure(
                check="capture_verifier",
                expected="known scenario",
                observed=scenario_id,
            )
        ],
    )


def _verify_record_seed(lines: list[str], ctx: ScenarioContext) -> VerificationResult:
    transitions = count_track_transitions(lines)
    recs = extract_recs_stop_lengths(lines)
    failures: list[VerificationFailure] = []
    if transitions.get(("ARMED", "RECORDING"), 0) + transitions.get(("EMPTY", "RECORDING"), 0) < 1:
        failures.append(
            VerificationFailure(
                check="record_start_transition",
                expected="ARMED|EMPTY -> RECORDING",
                observed=str(dict(transitions)),
            )
        )
    if not recs:
        failures.append(
            VerificationFailure(
                check="recs_stop_row",
                expected="RECS stop row",
                observed="none",
            )
        )
    return VerificationResult(passed=not failures, failures=failures, metrics={"recs_rows": len(recs)})


def _verify_record_overdub(lines: list[str], ctx: ScenarioContext) -> VerificationResult:
    try:
        from hitl.legacy_record_baseline import _build_serial_verification

        report = latest_base_report(Path(ctx.out_dir))
        if report is None:
            return VerificationResult(
                passed=False,
                failures=[
                    VerificationFailure(
                        check="capture_legacy_verify",
                        expected="host_midi_automation_baseline report",
                        observed="none",
                    )
                ],
            )
        config = dict(report.get("config") or {})
        args = argparse.Namespace(**config)
        per_track_stats = list(report.get("per_track_stats") or [])
        legacy = _build_serial_verification(
            lines,
            args,
            per_track_stats=per_track_stats,
        )
        legacy["ok"] = not legacy.get("issues")
        return verification_from_legacy_dict(legacy)
    except Exception as exc:  # pragma: no cover
        return VerificationResult(
            passed=False,
            failures=[
                VerificationFailure(
                    check="capture_legacy_verify",
                    expected="legacy verification",
                    observed=str(exc),
                )
            ],
        )
