#!/usr/bin/env python3
"""Stage 0 HITL result-contract tests."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.contract import (
    CaptureIntegrity,
    HitlVerdict,
    classify_hitl_verdict,
    process_exit_code,
    report_hitl_run,
)


class HitlResultContractTests(unittest.TestCase):
    def test_pass_requires_complete_confirmed_assertions(self) -> None:
        verdict = classify_hitl_verdict(
            capture_complete=True,
            stimulus_valid=True,
            setup_transitions_confirmed=True,
            firmware_contradiction=False,
            firmware_assertions_pass=True,
            capture_integrity=CaptureIntegrity.COMPLETE,
        )
        self.assertEqual(verdict, HitlVerdict.PASS)
        self.assertEqual(process_exit_code(verdict), 0)

    def test_fail_requires_complete_evidence_and_firmware_contradiction(self) -> None:
        verdict = classify_hitl_verdict(
            capture_complete=True,
            stimulus_valid=True,
            setup_transitions_confirmed=True,
            firmware_contradiction=True,
            firmware_assertions_pass=False,
            capture_integrity=CaptureIntegrity.COMPLETE,
        )
        self.assertEqual(verdict, HitlVerdict.FAIL)
        self.assertEqual(process_exit_code(verdict), 2)

    def test_sparse_capture_with_runtime_stats_is_inconclusive_not_pass(self) -> None:
        verdict = classify_hitl_verdict(
            capture_complete=False,
            stimulus_valid=True,
            setup_transitions_confirmed=True,
            firmware_contradiction=False,
            firmware_assertions_pass=True,
            capture_integrity=CaptureIntegrity.INCOMPLETE,
            sparse_capture=True,
            runtime_stats_ok=True,
        )
        self.assertEqual(verdict, HitlVerdict.INCONCLUSIVE_CAPTURE)
        self.assertEqual(process_exit_code(verdict), 3)

    def test_incomplete_capture_is_not_fail_even_if_assertions_look_wrong(self) -> None:
        verdict = classify_hitl_verdict(
            capture_complete=False,
            stimulus_valid=True,
            setup_transitions_confirmed=True,
            firmware_contradiction=True,
            firmware_assertions_pass=False,
            capture_integrity=CaptureIntegrity.INCOMPLETE,
        )
        self.assertEqual(verdict, HitlVerdict.INCONCLUSIVE_CAPTURE)

    def test_unconfirmed_setup_is_inconclusive(self) -> None:
        verdict = classify_hitl_verdict(
            capture_complete=True,
            stimulus_valid=True,
            setup_transitions_confirmed=False,
            firmware_contradiction=False,
            firmware_assertions_pass=True,
            capture_integrity=CaptureIntegrity.COMPLETE,
        )
        self.assertEqual(verdict, HitlVerdict.INCONCLUSIVE_CAPTURE)

    def test_unknown_integrity_is_inconclusive(self) -> None:
        verdict = classify_hitl_verdict(
            capture_complete=True,
            stimulus_valid=True,
            setup_transitions_confirmed=True,
            firmware_contradiction=False,
            firmware_assertions_pass=True,
            capture_integrity=CaptureIntegrity.UNKNOWN,
        )
        self.assertEqual(verdict, HitlVerdict.INCONCLUSIVE_CAPTURE)

    def test_report_does_not_rewrite_inconclusive_to_fail(self) -> None:
        report = report_hitl_run(
            capture_complete=False,
            stimulus_valid=True,
            setup_transitions_confirmed=True,
            firmware_contradiction=True,
            firmware_assertions_pass=False,
            capture_integrity=CaptureIntegrity.INCOMPLETE,
            sparse_capture=True,
            runtime_stats_ok=True,
        )
        self.assertEqual(report.verdict, HitlVerdict.INCONCLUSIVE_CAPTURE)
        self.assertEqual(report.reason, "SPARSE_CAPTURE")
        self.assertNotEqual(report.verdict, HitlVerdict.FAIL)
        self.assertNotEqual(report.verdict, HitlVerdict.PASS)

    def test_legacy_adapter_sparse_is_inconclusive(self) -> None:
        from hitl.verify.legacy_adapter import verification_from_legacy_dict

        result = verification_from_legacy_dict(
            {"ok": True, "issues": ["record_first_note_missing"], "cap_sparse": True}
        )
        self.assertEqual(result.verdict, HitlVerdict.INCONCLUSIVE_CAPTURE)
        self.assertFalse(result.ok)

    def test_legacy_adapter_complete_issues_are_fail(self) -> None:
        from hitl.verify.legacy_adapter import verification_from_legacy_dict

        result = verification_from_legacy_dict(
            {"ok": False, "issues": ["record_first_note_too_late"], "cap_sparse": False}
        )
        self.assertEqual(result.verdict, HitlVerdict.FAIL)
        self.assertFalse(result.ok)


if __name__ == "__main__":
    unittest.main()
