#!/usr/bin/env python3
"""Unit tests for VerificationResult diagnostics."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.verify.result import VerificationFailure, VerificationResult


class VerificationResultTests(unittest.TestCase):
    def test_ok_when_passed_without_failures(self) -> None:
        result = VerificationResult(passed=True)
        self.assertTrue(result.ok)

    def test_not_ok_when_failure_present(self) -> None:
        result = VerificationResult(
            passed=True,
            failures=[
                VerificationFailure(
                    check="ST transition",
                    expected="RECORDING -> PLAYING",
                    observed="RECORDING -> STOPPED",
                    serial_line=421,
                    suggestion="Compare ST sequence with known-good bundle",
                )
            ],
        )
        self.assertFalse(result.ok)
        self.assertEqual(result.failures[0].serial_line, 421)


if __name__ == "__main__":
    unittest.main()
