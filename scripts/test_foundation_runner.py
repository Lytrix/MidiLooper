#!/usr/bin/env python3
"""Unit tests for foundation_runner verify-only path."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.config import HitlConfig
from hitl.foundation_runner import run_layered_preset


def _test_config(**overrides: object) -> HitlConfig:
    base = dict(
        preset=None,
        scenario_ids=("record_overdub",),
        track_number=5,
        loop_slot=1,
        midi_channel=5,
        record_bars=2,
        overdub_bars=2,
        midi_out_name="Teensy",
        midi_in_name="Teensy",
        serial_port=None,
        verify_serial_log=None,
        verify_only=True,
        out_dir=Path("captures"),
        managed_capture=False,
    )
    base.update(overrides)
    return HitlConfig(**base)


class FoundationRunnerTests(unittest.TestCase):
    def test_verify_only_record_overdub_writes_report(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out_dir = Path(tmp)
            log_path = out_dir / "serial.log"
            log_path.write_text(
                "\n".join(
                    [
                        "[1836.133] [DEBUG] [TRACK] Recording started @ tick 0",
                        "[1840.890] [DEBUG] [TRACK] Recording stopped @ tick 291 (recStart=0 length=1536)",
                        "[1841.970] [DEBUG] [TRACK] Overdubbing started @ tick 702",
                        "[1847.139] [DEBUG] [TRACK] Overdubbing stopped @ tick 2677",
                    ]
                ),
                encoding="utf-8",
            )
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
            (out_dir / "host_midi_automation_baseline_20260804_test.json").write_text(
                json.dumps(report),
                encoding="utf-8",
            )
            config = _test_config(verify_serial_log=log_path, out_dir=out_dir)
            code = run_layered_preset(config)
            self.assertEqual(code, 0)
            reports = list(out_dir.glob("host_midi_hitl_*.json"))
            self.assertEqual(len(reports), 1)


if __name__ == "__main__":
    unittest.main()
