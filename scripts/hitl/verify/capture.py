"""Capture-phase serial verification for layered HITL."""

from __future__ import annotations

import argparse
from pathlib import Path

from hitl.baseline_loop_inventory import latest_base_report
from hitl.context import ScenarioContext
from hitl.verify.legacy_adapter import verification_from_legacy_dict
from hitl.verify.result import VerificationFailure, VerificationResult


def verify_capture(lines: list[str], ctx: ScenarioContext) -> VerificationResult:
    if ctx.scenario_id == "record_overdub":
        return _verify_record_overdub(lines, ctx)
    return VerificationResult(
        passed=False,
        failures=[
            VerificationFailure(
                check="capture_verifier",
                expected="record_overdub",
                observed=ctx.scenario_id,
            )
        ],
    )


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
