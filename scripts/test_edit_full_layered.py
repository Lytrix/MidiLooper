#!/usr/bin/env python3
"""Unit tests for layered edit_full session wiring (Phase 3.2)."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import MagicMock, patch

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.baseline_canonical_args import canonical_edit_full_legacy_args
from hitl.config import HitlConfig
from hitl.context import ActionContext, ScenarioContext
from hitl.layered_legacy_bridge import LayeredLegacyBridge
from hitl.layered_registry import LAYERED_PRESETS, get_layered_registry, resolve_preset_scenario_ids
from hitl.legacy_edit_baseline import parse_edit_baseline_args
from hitl.scenarios.edit_full import run_edit_full_scenario
from hitl.scenarios.layered import run_edit_full, set_bridge
from hitl.session import HitlSession
from hitl.verify.scenarios import verify_scenario


def _config(**overrides: object) -> HitlConfig:
    base: dict[str, object] = dict(
        preset="edit_full",
        scenario_ids=(),
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
        follow_current_session=True,
        managed_capture=True,
        legacy_extra_args=(
            "--track-number",
            "5",
            "--loop-slot",
            "2",
            "--midi-channel",
            "5",
            "--follow-current-session",
        ),
    )
    base.update(overrides)
    return HitlConfig(**base)


class EditFullLayeredTests(unittest.TestCase):
    def test_layered_presets_are_base_and_edit_full_only(self) -> None:
        self.assertEqual(sorted(LAYERED_PRESETS), ["base", "edit_full"])
        self.assertEqual(resolve_preset_scenario_ids("edit_full"), ("edit_full",))
        self.assertEqual(resolve_preset_scenario_ids("base"), ("record_overdub",))
        registry = get_layered_registry()
        self.assertEqual(sorted(registry), ["edit_full", "record_overdub"])

    def test_run_edit_full_reuses_layered_session(self) -> None:
        session = HitlSession(out_port="out", in_port="in", collector=object())
        args = SimpleNamespace(
            legacy_args=[
                "--record-bars",
                "2",
                "--track-number",
                "5",
                "--midi-channel",
                "5",
                "--start-transport",
            ],
            hitl_session=session,
            out_dir=Path("captures"),
            verify_serial_log=None,
            hitl_context=None,
        )
        with patch("hitl.legacy_edit_baseline.run_edit_baseline", return_value=0) as run_mock:
            code = run_edit_full_scenario(args)
        self.assertEqual(code, 0)
        run_mock.assert_called_once()
        _, kwargs = run_mock.call_args
        self.assertIs(kwargs["out_port"], session.out_port)
        self.assertIs(kwargs["in_port"], session.in_port)
        self.assertIs(kwargs["serial_collector"], session.collector)
        self.assertFalse(kwargs["owns_resources"])

    def test_edit_full_args_include_slot_targets_and_mode_b_follow(self) -> None:
        config = _config()
        bridge = LayeredLegacyBridge.from_config(config)
        session = HitlSession(out_port=None, in_port=None, collector=None)
        ctx = ScenarioContext(
            action=ActionContext(session=session, config=config),
            scenario_id="edit_full",
            out_dir=Path("captures"),
            started_at=__import__("datetime").datetime.now(
                __import__("datetime").timezone.utc
            ),
        )
        args = bridge.edit_full_args(ctx)
        legacy = list(args.legacy_args)
        self.assertIn("--follow-current-session", legacy)
        self.assertEqual(legacy[legacy.index("--track-number") + 1], "5")
        self.assertEqual(legacy[legacy.index("--loop-slot") + 1], "2")
        self.assertEqual(legacy[legacy.index("--midi-channel") + 1], "5")
        self.assertEqual(args.preset, "edit_full")

        parsed = parse_edit_baseline_args(legacy)
        self.assertEqual(parsed.track_number, 5)
        self.assertEqual(parsed.loop_slot, 2)
        self.assertEqual(parsed.midi_channel, 5)
        self.assertTrue(parsed.follow_current_session)
        self.assertTrue(parsed.start_transport)
        self.assertTrue(parsed.bar_sync_from_midi_clock)

    def test_canonical_edit_full_flags_parse(self) -> None:
        flags = canonical_edit_full_legacy_args() + [
            "--track-number",
            "5",
            "--loop-slot",
            "1",
            "--midi-channel",
            "5",
            "--follow-current-session",
        ]
        parsed = parse_edit_baseline_args(flags)
        self.assertEqual(parsed.record_bars, 2)
        self.assertEqual(parsed.loop_slot, 1)
        self.assertTrue(parsed.follow_current_session)

    def test_layered_run_edit_full_injects_hitl_session(self) -> None:
        config = _config()
        bridge = LayeredLegacyBridge.from_config(config)
        set_bridge(bridge)
        collector = object()
        session = HitlSession(out_port="out", in_port="in", collector=collector)
        ctx = ScenarioContext(
            action=ActionContext(session=session, config=config),
            scenario_id="edit_full",
            out_dir=Path("captures"),
            started_at=__import__("datetime").datetime.now(
                __import__("datetime").timezone.utc
            ),
        )
        with patch(
            "hitl.scenarios.edit_full.run_edit_full_scenario", return_value=0
        ) as run_mock:
            result = run_edit_full(ctx)
        self.assertEqual(result.observations["exit_code"], 0)
        self.assertEqual(result.observations["flow"], "edit_full")
        run_mock.assert_called_once()
        call_args = run_mock.call_args[0][0]
        self.assertIs(call_args.hitl_session, session)

    def test_run_edit_baseline_owns_resources_false_does_not_open_serial(self) -> None:
        from hitl.legacy_edit_baseline import run_edit_baseline

        parsed = parse_edit_baseline_args(
            canonical_edit_full_legacy_args()
            + [
                "--track-number",
                "5",
                "--loop-slot",
                "1",
                "--midi-channel",
                "5",
                "--follow-current-session",
            ]
        )
        parsed.bar_sync_from_midi_clock = False
        out_port = MagicMock()
        in_port = MagicMock()
        with patch(
            "hitl.legacy_edit_baseline._open_edit_baseline_serial"
        ) as open_serial, patch(
            "hitl.legacy_edit_baseline._clock_seen_within", return_value=True
        ), patch(
            "hitl.legacy_edit_baseline._send_short_press"
        ), patch(
            "hitl.legacy_record_baseline._send_record_clear_long_press"
        ), patch(
            "host_midi_automation_baseline._send_record_arm_press"
        ):
            code = run_edit_baseline(
                parsed,
                out_port=out_port,
                in_port=in_port,
                serial_collector=None,
                owns_resources=False,
            )
        self.assertEqual(code, 1)
        open_serial.assert_not_called()

    def test_verify_scenario_dispatches_edit_full(self) -> None:
        config = _config(verify_only=True, managed_capture=False)
        session = HitlSession(out_port=None, in_port=None, collector=None)
        ctx = ScenarioContext(
            action=ActionContext(session=session, config=config),
            scenario_id="edit_full",
            out_dir=Path("captures"),
            started_at=__import__("datetime").datetime.now(
                __import__("datetime").timezone.utc
            ),
        )
        with patch(
            "hitl.scenarios.edit_full.verify_edit_full_scenario",
            return_value={"ok": True},
        ) as verify_mock:
            result = verify_scenario([], ctx)
        self.assertTrue(result.ok)
        verify_mock.assert_called_once()


if __name__ == "__main__":
    unittest.main()
