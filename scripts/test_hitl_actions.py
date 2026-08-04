#!/usr/bin/env python3
"""Unit tests for layered HITL action helpers."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from unittest.mock import MagicMock, patch

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.actions import buttons, transport
from hitl.config import HitlConfig
from hitl.context import ActionContext
from hitl.session import HitlSession


def _action_context() -> ActionContext:
    config = HitlConfig(
        preset="base",
        scenario_ids=("record_overdub",),
        track_number=5,
        loop_slot=2,
        midi_channel=5,
        record_bars=2,
        overdub_bars=2,
        midi_out_name="Teensy",
        midi_in_name="Teensy",
        serial_port=None,
        verify_serial_log=None,
        verify_only=False,
        out_dir=Path("captures"),
        press_ms=120,
        phase_wait_ms=10,
    )
    session = HitlSession(out_port=MagicMock(), in_port=MagicMock())
    return ActionContext(session=session, config=config)


class ActionTests(unittest.TestCase):
    @patch("hitl.actions.buttons._send_short_press")
    def test_press_record_uses_config_press_ms(self, send_short_press: MagicMock) -> None:
        ctx = _action_context()
        buttons.press_record(ctx)
        send_short_press.assert_called_once()
        self.assertEqual(send_short_press.call_args.kwargs["press_ms"], 120)

    @patch("hitl.actions.transport.time.sleep")
    def test_sleep_ms(self, sleep_mock: MagicMock) -> None:
        transport.sleep_ms(_action_context(), 250)
        sleep_mock.assert_called_once_with(0.25)


if __name__ == "__main__":
    unittest.main()
