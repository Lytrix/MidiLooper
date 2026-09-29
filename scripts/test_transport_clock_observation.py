#!/usr/bin/env python3
"""The clock helper must never press transport to confirm a start the caller already made.

`ensure_midi_clock`'s press is a *toggle*: sent while the transport already runs, it stops it.
This board emits no USB MIDI clock out, so the pulse wait always expires and the press always
fires. Measured twice on device: `captures/s4f22-base-device-03` (start 25.086, toggle 25.603)
and `captures/s4f22-step7-d2` (start 53.527, recording 53.974, unexplained transport press
54.526, then the intended Record stop at 59.248 armed a new take).
"""

from __future__ import annotations

import sys
import time
import unittest
from pathlib import Path
from types import SimpleNamespace

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.control_constants import CONTROL_CHANNEL_1BASED, GLOBAL_TRANSPORT_NOTE
from hitl.transport_clock import (
    clear_control_action_log,
    control_action_log,
    ensure_transport_clock,
    transport_press_count,
)


class FakePort:
    def __init__(self) -> None:
        self.sent: list = []

    def send(self, message) -> None:
        self.sent.append(message)

    def poll(self):
        return None


class FakeCollector:
    """Serial snapshot that reports the transport only after ``ack_after_s`` seconds."""

    def __init__(self, *, ack_after_s: float | None) -> None:
        self._ack_after_s = ack_after_s
        self._t0 = time.monotonic()

    def snapshot(self) -> list[str]:
        if self._ack_after_s is None:
            return []
        if time.monotonic() - self._t0 >= self._ack_after_s:
            return ["[1.000] [INFO] [TRACK] Transport started"]
        return []


def _args() -> SimpleNamespace:
    # serial_capture_active() only needs a port/follow flag for the proxy path to be consulted.
    return SimpleNamespace(serial_port="/dev/cu.fake", follow_serial_log=Path("captures/fake.log"))


class ClockObservationTests(unittest.TestCase):
    def setUp(self) -> None:
        clear_control_action_log()

    def test_delayed_start_receipt_is_accepted_without_another_press(self) -> None:
        """The offline repro: a receipt at 0.3 s must not be followed by a toggle at 0.5 s."""
        out_port = FakePort()
        collector = FakeCollector(ack_after_s=0.3)
        started_since_index = 0
        ok, using_proxy = ensure_transport_clock(
            FakePort(),  # no clocks ever arrive
            out_port,
            collector,
            _args(),
            min_clocks=24,
            timeout_s=3.0,
            started_since_index=started_since_index,
        )
        self.assertTrue(ok)
        self.assertTrue(using_proxy)
        self.assertEqual(0, len(out_port.sent), "confirmation must not press transport")
        self.assertEqual(0, transport_press_count())

    def test_unacknowledged_start_is_a_named_failure_not_a_press(self) -> None:
        out_port = FakePort()
        ok, using_proxy = ensure_transport_clock(
            FakePort(),
            out_port,
            FakeCollector(ack_after_s=None),
            _args(),
            min_clocks=24,
            timeout_s=0.2,
            started_since_index=0,
        )
        self.assertFalse(ok)
        self.assertFalse(using_proxy)
        self.assertEqual(0, len(out_port.sent), "a missing receipt must never re-press transport")

    def test_press_is_refused_unless_explicitly_allowed(self) -> None:
        out_port = FakePort()
        ok, using_proxy = ensure_transport_clock(
            FakePort(),
            out_port,
            FakeCollector(ack_after_s=None),
            _args(),
            min_clocks=24,
            timeout_s=0.2,
        )
        self.assertFalse(ok)
        self.assertFalse(using_proxy)
        self.assertEqual(0, len(out_port.sent))

    def test_baseline_presses_land_in_the_same_ledger(self) -> None:
        """The baseline's own press primitives must be attributable too, not just the clock's."""
        from hitl.midi_io import _send_multi_short_press, _send_short_press

        out_port = FakePort()
        _send_short_press(
            out_port, note=GLOBAL_TRANSPORT_NOTE, channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=1,
        )
        _send_multi_short_press(
            out_port, note=GLOBAL_TRANSPORT_NOTE, channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=1, count=2, gap_ms=1,
        )
        actions = control_action_log()
        # One short press plus the two constituent presses of the multi gesture.
        self.assertEqual(3, len(actions))
        self.assertEqual(
            ["test_baseline_presses_land_in_the_same_ledger"] * 3,
            [a["caller"] for a in actions],
        )
        self.assertEqual(3, transport_press_count())

    def test_control_action_ledger_names_the_caller(self) -> None:
        from hitl.transport_clock import _send_short_press

        out_port = FakePort()
        _send_short_press(
            out_port,
            note=GLOBAL_TRANSPORT_NOTE,
            channel_1based=CONTROL_CHANNEL_1BASED,
            press_ms=1,
        )
        actions = control_action_log()
        self.assertEqual(1, len(actions))
        self.assertEqual(GLOBAL_TRANSPORT_NOTE, actions[0]["note"])
        self.assertEqual("test_control_action_ledger_names_the_caller", actions[0]["caller"])
        self.assertEqual(1, transport_press_count())


if __name__ == "__main__":
    unittest.main()
