"""JSON report output for layered HITL runs."""

from __future__ import annotations

import json
from datetime import datetime
from pathlib import Path
from typing import Optional

from hitl.context import ScenarioContext
from hitl.types import ScenarioResult
from hitl.verify.result import VerificationResult


def write_scenario_report(
    *,
    scenario_id: str,
    result: Optional[ScenarioResult],
    verification: VerificationResult,
    scenario_ctx: ScenarioContext,
    out_dir: Path,
) -> Path:
    out_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    path = out_dir / f"host_midi_hitl_{scenario_id}_{stamp}.json"
    payload = {
        "schema_version": 1,
        "scenario_id": scenario_id,
        "passed": verification.ok,
        "verification": _verification_to_dict(verification),
        "observations": {} if result is None else result.observations,
        "markers": [] if result is None else result.markers,
        "artifacts": {
            key: str(value) for key, value in ({} if result is None else result.artifacts).items()
        },
        "out_dir": str(scenario_ctx.out_dir),
    }
    path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    return path


def exit_code_from_verification(verification: VerificationResult) -> int:
    return 0 if verification.ok else 2


def _verification_to_dict(verification: VerificationResult) -> dict[str, object]:
    return {
        "passed": verification.passed,
        "ok": verification.ok,
        "warnings": list(verification.warnings),
        "failures": [
            {
                "check": failure.check,
                "expected": failure.expected,
                "observed": failure.observed,
                "serial_line": failure.serial_line,
                "known_good_bundle": failure.known_good_bundle,
                "suggestion": failure.suggestion,
            }
            for failure in verification.failures
        ],
        "metrics": verification.metrics,
        "attachments": verification.attachments,
    }
