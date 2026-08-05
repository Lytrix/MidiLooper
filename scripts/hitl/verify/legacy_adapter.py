"""Convert legacy verify dicts to VerificationResult."""

from __future__ import annotations

from hitl.verify.result import VerificationFailure, VerificationResult


def verification_from_legacy_dict(result: dict[str, object]) -> VerificationResult:
    issues = list(result.get("issues", []))
    failures: list[VerificationFailure] = []
    for issue in issues:
        failures.append(
            VerificationFailure(
                check=str(issue),
                expected="pass",
                observed=str(issue),
            )
        )
    ok = bool(result.get("ok", not issues))
    return VerificationResult(
        passed=ok,
        failures=failures,
        metrics={k: v for k, v in result.items() if k not in ("ok", "issues")},
    )
