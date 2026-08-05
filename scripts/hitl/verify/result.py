"""Typed verification results for HITL verifiers."""

from __future__ import annotations

from dataclasses import dataclass, field


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

    @property
    def ok(self) -> bool:
        return self.passed and not self.failures
