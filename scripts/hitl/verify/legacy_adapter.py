"""Convert legacy verify dicts to VerificationResult."""

from __future__ import annotations

from hitl.capture_transitions import serial_issue_is_evidence_absent
from hitl.contract import CaptureIntegrity, HitlVerdict, report_hitl_run
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
    sparse = bool(result.get("cap_sparse"))
    capture_complete = not sparse
    if result.get("capture_complete") is not None:
        capture_complete = bool(result.get("capture_complete"))
    integrity = CaptureIntegrity.INCOMPLETE if sparse else CaptureIntegrity.COMPLETE
    # The same split as the baseline report: absent rows leave the claim unproven (non-PASS)
    # without being read as the device asserting the opposite.
    evidence_gaps = tuple(str(item) for item in issues if serial_issue_is_evidence_absent(str(item)))
    contradictions = tuple(str(item) for item in issues if str(item) not in evidence_gaps)
    report = report_hitl_run(
        capture_complete=capture_complete,
        stimulus_valid=True,
        setup_transitions_confirmed=capture_complete and not evidence_gaps,
        firmware_contradiction=bool(contradictions) and capture_complete,
        firmware_assertions_pass=(
            (not contradictions and not evidence_gaps) if capture_complete else True
        ),
        capture_integrity=integrity,
        sparse_capture=sparse,
        reason="EVIDENCE_MISSING" if evidence_gaps and not contradictions else "",
        issues=tuple(str(issue) for issue in issues),
    )
    return VerificationResult(
        passed=report.verdict is HitlVerdict.PASS,
        verdict=report.verdict,
        failures=failures,
        metrics={k: v for k, v in result.items() if k not in ("ok", "issues")},
    )
