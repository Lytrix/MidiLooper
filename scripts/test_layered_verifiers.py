#!/usr/bin/env python3
"""Unit tests for layered verify modules."""

from __future__ import annotations

import sys
import unittest
from datetime import datetime, timezone
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.config import HitlConfig
from hitl.context import ActionContext, ScenarioContext
from hitl.session import HitlSession
from hitl.verify.capture import verify_capture
from hitl.verify.playback import verify_playback


def _scenario_ctx(scenario_id: str) -> ScenarioContext:
    config = HitlConfig(
        preset=None,
        scenario_ids=(),
        track_number=5,
        loop_slot=1,
        midi_channel=5,
        record_bars=2,
        overdub_bars=2,
        midi_out_name="Teensy",
        midi_in_name="Teensy",
        serial_port=None,
        verify_serial_log=None,
        verify_only=False,
        out_dir=Path("captures"),
    )
    session = HitlSession(out_port=None, in_port=None, collector=None)
    return ScenarioContext(
        action=ActionContext(session=session, config=config),
        scenario_id=scenario_id,
        out_dir=Path("captures"),
        started_at=datetime.now(timezone.utc),
    )


class LayeredVerifierTests(unittest.TestCase):
    def test_record_seed_capture_verifier_passes_minimal_log(self) -> None:
        lines = [
            "#CAP,1000,ST,Track,ARMED,RECORDING",
            "#CAP,2000,RECS,stop,100,200,1536,1536",
        ]
        result = verify_capture(lines, _scenario_ctx("record_seed"))
        self.assertTrue(result.ok)

    def test_record_overdub_capture_verifier_uses_base_report_config(self) -> None:
        import json
        import tempfile

        lines = [
            "[1836.133] [DEBUG] [TRACK] Recording started @ tick 0",
            "[1840.889] [INFO] Record stop truncation rewind: raw=1827 final=1536 playbackTick=291",
            "[1840.890] [DEBUG] [TRACK] Recording stopped @ tick 291 (recStart=0 length=1536)",
            "[1841.970] [DEBUG] [TRACK] Overdubbing started @ tick 702",
            "[1847.139] [DEBUG] [TRACK] Overdubbing stopped @ tick 2677",
        ]
        report = {
            "config": {
                "record_bars": 2,
                "overdub_bars": 2,
                "second_overdub_bars": 2,
                "second_overdub_step_clocks": 24,
                "bar_sync_from_midi_clock": True,
                "record_only": False,
                "midi_channel": 5,
                "tempo_bpm": 120.0,
                "record_low_note": 48,
                "record_high_note": 79,
                "overdub_low_note": 24,
                "overdub_high_note": 39,
                "second_overdub_low_note": 12,
                "second_overdub_high_note": 35,
                "overdub_start_delay_bars": 0,
                "overdub_start_delay_beats": 1,
                "second_overdub_start_delay_bars": 0,
                "second_overdub_start_delay_beats": 1,
                "record_first_note_max_clocks": 12,
                "overdub_first_note_max_clocks": 12,
                "long_run_bar_threshold": 48,
                "record_stop_min_free_ram2_bytes": 0,
                "record_stop_min_free_ram2_warn_bytes": 12288,
            },
            "per_track_stats": [
                {
                    "record_notes_sent": 33,
                    "record_clock_pulses_seen": 192,
                    "overdub_notes_sent": 17,
                    "overdub_clock_pulses_seen": 192,
                    "second_overdub_notes_sent": 9,
                    "second_overdub_clock_pulses_seen": 192,
                }
            ],
        }
        with tempfile.TemporaryDirectory() as tmp:
            out_dir = Path(tmp)
            report_path = out_dir / "host_midi_automation_baseline_20260804_test.json"
            report_path.write_text(json.dumps(report), encoding="utf-8")
            ctx = _scenario_ctx("record_overdub")
            ctx = ScenarioContext(
                action=ctx.action,
                scenario_id="record_overdub",
                out_dir=out_dir,
                started_at=ctx.started_at,
            )
            result = verify_capture(lines, ctx)
            self.assertTrue(result.ok, result.failures)

    def test_slot_queued_start_requires_marker(self) -> None:
        ctx = _scenario_ctx("slot_queued_start")
        result = verify_playback(["#CAP,1000,DISP,0,PLAYING"], ctx)
        self.assertFalse(result.ok)
        ctx.action.markers.append("slot_queued_start:pressed_slot=1")
        result = verify_playback(["#CAP,1000,DISP,0,PLAYING"], ctx)
        self.assertTrue(result.ok)


if __name__ == "__main__":
    unittest.main()
