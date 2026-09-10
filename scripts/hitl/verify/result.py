"""Typed verification results for HITL verifiers."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional

from hitl.contract import HitlVerdict


@dataclass
class VerificationFailure:
    check: str
    expected: str
    observed: str
    serial_line: int | None = None
    known_good_bundle: str | None = None
    suggestion: str | None = None


@dataclass
class VerificationResult:
    passed: bool
    warnings: list[str] = field(default_factory=list)
    failures: list[VerificationFailure] = field(default_factory=list)
    metrics: dict[str, object] = field(default_factory=dict)
    attachments: dict[str, object] = field(default_factory=dict)
    verdict: Optional[HitlVerdict] = None

    @property
    def ok(self) -> bool:
        if self.verdict is HitlVerdict.INCONCLUSIVE_CAPTURE:
            return False
        if self.verdict is HitlVerdict.FAIL:
            return False
        return self.passed and not self.failures
