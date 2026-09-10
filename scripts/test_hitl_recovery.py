#!/usr/bin/env python3
"""State-aware HITL recovery kernel tests."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.contract import ActionResultKind
from hitl.recovery import request_state_transition


class _FakeCollector:
    def __init__(self, lines: list[str]) -> None:
        self.lines = list(lines)

    def snapshot(self) -> list[str]:
        return list(self.lines)


class RecoveryKernelTests(unittest.TestCase):
    def test_already_satisfied_does_not_send(self) -> None:
        collector = _FakeCollector(["#CAP,1,ST,Track,STOPPED,EMPTY"])
        sends: list[int] = []

        def send() -> None:
            sends.append(1)

        outcome = request_state_transition(
            snapshot=collector.snapshot,
            requested="EMPTY",
            already_satisfied=lambda lines, _baseline: any(",EMPTY" in line for line in lines),
            confirmed=lambda lines, baseline: any(",EMPTY" in line for line in lines[baseline:]),
            disproved=lambda _lines, _baseline: False,
            send_request=send,
            timeout_s=0.05,
        )
        self.assertEqual(outcome.kind, ActionResultKind.ALREADY_SATISFIED)
        self.assertEqual(sends, [])

    def test_confirmed_after_one_request(self) -> None:
        collector = _FakeCollector(["#CAP,1,ST,Track,PLAYING,STOPPED"])
        sends: list[int] = []

        def send() -> None:
            sends.append(1)
            collector.lines.append("#CAP,2,ST,Track,STOPPED,ARMED")

        outcome = request_state_transition(
            snapshot=collector.snapshot,
            requested="ARMED",
            already_satisfied=lambda _lines, _baseline: False,
            confirmed=lambda lines, baseline: any(",ARMED" in line for line in lines[baseline:]),
            disproved=lambda _lines, _baseline: False,
            send_request=send,
            timeout_s=0.2,
        )
        self.assertEqual(outcome.kind, ActionResultKind.CONFIRMED)
        self.assertEqual(sends, [1])
        self.assertEqual(outcome.evidence.retry_count, 0)

    def test_stale_pre_baseline_marker_does_not_confirm(self) -> None:
        collector = _FakeCollector(["#CAP,1,ST,Track,STOPPED,ARMED"])
        sends: list[int] = []

        def send() -> None:
            sends.append(1)
            collector.lines.append("#CAP,2,ST,Track,ARMED,STOPPED")

        outcome = request_state_transition(
            snapshot=collector.snapshot,
            requested="ARMED",
            already_satisfied=lambda _lines, _baseline: False,
            confirmed=lambda lines, baseline: any(
                line.endswith(",ARMED") for line in lines[baseline:]
            ),
            disproved=lambda lines, baseline: any(
                ",STOPPED" in line for line in lines[baseline:]
            ),
            send_request=send,
            timeout_s=0.05,
            max_retries=0,
        )
        self.assertEqual(outcome.kind, ActionResultKind.RECOVERY_FAILED)
        self.assertEqual(sends, [1])

    def test_timeout_without_disproof_does_not_retry(self) -> None:
        collector = _FakeCollector(["bootstrap"])
        sends: list[int] = []

        def send() -> None:
            sends.append(1)

        outcome = request_state_transition(
            snapshot=collector.snapshot,
            requested="ARMED",
            already_satisfied=lambda _lines, _baseline: False,
            confirmed=lambda _lines, _baseline: False,
            disproved=lambda _lines, _baseline: False,
            send_request=send,
            timeout_s=0.03,
            max_retries=3,
        )
        self.assertEqual(outcome.kind, ActionResultKind.EVIDENCE_UNAVAILABLE)
        self.assertEqual(sends, [1])
        self.assertEqual(outcome.evidence.retry_count, 0)

    def test_retry_only_after_disproof(self) -> None:
        collector = _FakeCollector(["#CAP,1,ST,Track,STOPPED,PLAYING"])
        sends: list[int] = []

        def send() -> None:
            sends.append(1)
            if len(sends) == 1:
                collector.lines.append("#CAP,2,ST,Track,PLAYING,STOPPED")
            else:
                collector.lines.append("#CAP,3,ST,Track,STOPPED,ARMED")

        outcome = request_state_transition(
            snapshot=collector.snapshot,
            requested="ARMED",
            already_satisfied=lambda _lines, _baseline: False,
            confirmed=lambda lines, baseline: any(",ARMED" in line for line in lines[baseline:]),
            disproved=lambda lines, baseline: (
                any(",STOPPED" in line for line in lines[baseline:])
                and not any(",ARMED" in line for line in lines[baseline:])
            ),
            send_request=send,
            timeout_s=0.05,
            max_retries=1,
        )
        self.assertEqual(outcome.kind, ActionResultKind.CONFIRMED)
        self.assertEqual(sends, [1, 1])
        self.assertEqual(outcome.evidence.retry_count, 1)

    def test_source_state_mid_wait_does_not_early_retry(self) -> None:
        """Still RECORDING after a stop press is pending, not mid-wait disproof.

        [`session_20260910_122002.log`] mid-wait RECORDING disproof retried inside
        the press window and became Record/Overdub (double) → undo.
        """
        import time as _time

        collector = _FakeCollector(["#CAP,1,ST,Track,ARMED,RECORDING"])
        send_times: list[float] = []

        def send() -> None:
            send_times.append(_time.monotonic())

        outcome = request_state_transition(
            snapshot=collector.snapshot,
            requested="PLAYING_AFTER_RECORD_STOP",
            already_satisfied=lambda _lines, _baseline: False,
            confirmed=lambda lines, baseline: any(
                line.endswith(",PLAYING") for line in lines[baseline:]
            ),
            disproved=lambda _lines, _baseline: True,
            send_request=send,
            timeout_s=0.08,
            max_retries=1,
            poll_s=0.01,
        )
        self.assertEqual(outcome.kind, ActionResultKind.RECOVERY_FAILED)
        self.assertEqual(len(send_times), 2)
        self.assertEqual(outcome.evidence.retry_count, 1)
        self.assertGreaterEqual(send_times[1] - send_times[0], 0.07)


if __name__ == "__main__":
    unittest.main()
