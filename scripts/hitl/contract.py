"""Frozen HITL host-harness contract (Stage 0).

Host-only. These terms are not firmware domain nouns.

Never convert ``INCONCLUSIVE_CAPTURE`` to ``PASS`` or ``FAIL``. A required
gate may still exit non-zero for inconclusive evidence, but the verdict
field must stay ``INCONCLUSIVE_CAPTURE``.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import StrEnum
from typing import Optional


class HitlVerdict(StrEnum):
    """Machine-readable run result. Agents must branch on this, not on prose."""

    PASS = "PASS"
    FAIL = "FAIL"
    INCONCLUSIVE_CAPTURE = "INCONCLUSIVE_CAPTURE"


class ActionResultKind(StrEnum):
    """Outcome of one state-changing control action."""

    CONFIRMED = "CONFIRMED"
    ALREADY_SATISFIED = "ALREADY_SATISFIED"
    RECOVERY_FAILED = "RECOVERY_FAILED"
    EVIDENCE_UNAVAILABLE = "EVIDENCE_UNAVAILABLE"


class PhaseAmbiguity(StrEnum):
    """Machine-readable phase-session pairing failure (Stage 3)."""

    AMBIGUOUS_PHASE = "AMBIGUOUS_PHASE"
    MISSING_START = "MISSING_START"
    MISSING_STOP = "MISSING_STOP"
    STALE_START = "STALE_START"
    STALE_STOP = "STALE_STOP"
    MULTIPLE_CANDIDATES = "MULTIPLE_CANDIDATES"
    OUT_OF_ORDER = "OUT_OF_ORDER"


class CaptureIntegrity(StrEnum):
    """Whether serial evidence is complete enough to judge firmware."""

    COMPLETE = "COMPLETE"
    INCOMPLETE = "INCOMPLETE"
    UNKNOWN = "UNKNOWN"


PROCESS_EXIT_PASS = 0
PROCESS_EXIT_FAIL = 2
PROCESS_EXIT_INCONCLUSIVE = 3


@dataclass(frozen=True)
class ActionEvidence:
    requested: str
    baseline_index: int
    matched_line_index: Optional[int] = None
    matched_line: Optional[str] = None
    latest_state: Optional[str] = None
    retry_count: int = 0
    diagnostic: str = ""


@dataclass(frozen=True)
class ActionOutcome:
    kind: ActionResultKind
    evidence: ActionEvidence

    @property
    def ok(self) -> bool:
        return self.kind in (
            ActionResultKind.CONFIRMED,
            ActionResultKind.ALREADY_SATISFIED,
        )

    def as_dict(self) -> dict[str, object]:
        return {
            "kind": str(self.kind),
            "requested": self.evidence.requested,
            "baseline_index": self.evidence.baseline_index,
            "matched_line_index": self.evidence.matched_line_index,
            "matched_line": self.evidence.matched_line,
            "latest_state": self.evidence.latest_state,
            "retry_count": self.evidence.retry_count,
            "diagnostic": self.evidence.diagnostic,
        }


@dataclass(frozen=True)
class HitlRunReport:
    verdict: HitlVerdict
    capture_complete: bool
    stimulus_valid: bool
    setup_transitions_confirmed: bool
    firmware_contradiction: bool
    firmware_assertions_pass: bool
    capture_integrity: CaptureIntegrity = CaptureIntegrity.UNKNOWN
    reason: str = ""
    action_outcomes: tuple[ActionOutcome, ...] = ()
    issues: tuple[str, ...] = ()
    extras: dict[str, object] = field(default_factory=dict)

    def as_dict(self) -> dict[str, object]:
        return {
            "verdict": str(self.verdict),
            "capture_complete": self.capture_complete,
            "stimulus_valid": self.stimulus_valid,
            "setup_transitions_confirmed": self.setup_transitions_confirmed,
            "firmware_contradiction": self.firmware_contradiction,
            "firmware_assertions_pass": self.firmware_assertions_pass,
            "capture_integrity": str(self.capture_integrity),
            "reason": self.reason,
            "action_outcomes": [outcome.as_dict() for outcome in self.action_outcomes],
            "issues": list(self.issues),
        }


def process_exit_code(verdict: HitlVerdict) -> int:
    """Fail-closed process status. Distinct codes; never rewrite the verdict."""
    if verdict is HitlVerdict.PASS:
        return PROCESS_EXIT_PASS
    if verdict is HitlVerdict.FAIL:
        return PROCESS_EXIT_FAIL
    return PROCESS_EXIT_INCONCLUSIVE


def classify_hitl_verdict(
    *,
    capture_complete: bool,
    stimulus_valid: bool,
    setup_transitions_confirmed: bool,
    firmware_contradiction: bool,
    firmware_assertions_pass: bool,
    capture_integrity: CaptureIntegrity = CaptureIntegrity.UNKNOWN,
    sparse_capture: bool = False,
    runtime_stats_ok: bool = False,
) -> HitlVerdict:
    """Map evidence flags to a frozen verdict.

    Sparse-capture plus host runtime stats must not produce ``PASS``.
    Incomplete or unknown capture must not produce ``FAIL``.
    """
    del runtime_stats_ok  # Host counts are never a substitute for serial evidence.
    evidence_complete = (
        capture_complete
        and capture_integrity is CaptureIntegrity.COMPLETE
        and not sparse_capture
    )
    if not evidence_complete or not stimulus_valid or not setup_transitions_confirmed:
        return HitlVerdict.INCONCLUSIVE_CAPTURE
    if firmware_contradiction or not firmware_assertions_pass:
        return HitlVerdict.FAIL
    return HitlVerdict.PASS


def report_hitl_run(
    *,
    capture_complete: bool,
    stimulus_valid: bool,
    setup_transitions_confirmed: bool,
    firmware_contradiction: bool,
    firmware_assertions_pass: bool,
    capture_integrity: CaptureIntegrity = CaptureIntegrity.UNKNOWN,
    sparse_capture: bool = False,
    runtime_stats_ok: bool = False,
    reason: str = "",
    action_outcomes: tuple[ActionOutcome, ...] = (),
    issues: tuple[str, ...] = (),
) -> HitlRunReport:
    verdict = classify_hitl_verdict(
        capture_complete=capture_complete,
        stimulus_valid=stimulus_valid,
        setup_transitions_confirmed=setup_transitions_confirmed,
        firmware_contradiction=firmware_contradiction,
        firmware_assertions_pass=firmware_assertions_pass,
        capture_integrity=capture_integrity,
        sparse_capture=sparse_capture,
        runtime_stats_ok=runtime_stats_ok,
    )
    resolved_reason = reason
    if not resolved_reason:
        if verdict is HitlVerdict.INCONCLUSIVE_CAPTURE:
            if sparse_capture:
                resolved_reason = "SPARSE_CAPTURE"
            elif not capture_complete or capture_integrity is not CaptureIntegrity.COMPLETE:
                resolved_reason = "SERIAL_INCOMPLETE"
            elif not stimulus_valid:
                resolved_reason = "STIMULUS_INVALID"
            elif not setup_transitions_confirmed:
                resolved_reason = "SETUP_UNCONFIRMED"
            else:
                resolved_reason = "EVIDENCE_UNAVAILABLE"
        elif verdict is HitlVerdict.FAIL:
            resolved_reason = "FIRMWARE_CONTRADICTION"
    return HitlRunReport(
        verdict=verdict,
        capture_complete=capture_complete,
        stimulus_valid=stimulus_valid,
        setup_transitions_confirmed=setup_transitions_confirmed,
        firmware_contradiction=firmware_contradiction,
        firmware_assertions_pass=firmware_assertions_pass,
        capture_integrity=capture_integrity,
        reason=resolved_reason,
        action_outcomes=action_outcomes,
        issues=issues,
    )
