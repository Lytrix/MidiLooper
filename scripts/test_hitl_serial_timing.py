#!/usr/bin/env python3
"""Unit tests for HITL serial timing verification helpers."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.serial_timing import extract_first_note_offset, extract_phase_boundaries, phase_start_delay_clocks


class ExtractPhaseBoundariesTests(unittest.TestCase):
    def test_record_and_overdub_windows(self) -> None:
        lines = [
            "#CAP,1000,ST,Track,ARMED,RECORDING",
            "#CAP,2000,ST,Track,RECORDING,STOPPED_RECORDING",
            "#CAP,3000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,4000,ST,Track,OVERDUBBING,PLAYING",
        ]
        bounds = extract_phase_boundaries(lines)
        self.assertEqual(bounds["record_start_ts"], 1000)
        self.assertEqual(bounds["record_stop_ts"], 2000)
        self.assertEqual(bounds["overdub_start_ts"], 3000)
        self.assertEqual(bounds["overdub_stop_ts"], 4000)

    def test_overdub_windows_from_human_logs_when_st_missing(self) -> None:
        lines = [
            "#CAP,1000,ST,Track,ARMED,RECORDING",
            "#CAP,2000,ST,Track,RECORDING,STOPPED_RECORDING",
            "[1841.970] [DEBUG] [TRACK] Overdubbing started @ tick 702",
            "[1847.139] [DEBUG] [TRACK] Overdubbing stopped @ tick 2677",
        ]
        bounds = extract_phase_boundaries(lines)
        self.assertEqual(bounds["overdub_start_ts"], 1841970000)
        self.assertEqual(bounds["overdub_stop_ts"], 1847139000)

    def test_record_boundaries_from_human_wall_clock(self) -> None:
        lines = [
            "[1836.133] [DEBUG] [TRACK] Recording started @ tick 0",
            "[1840.889] [INFO] Record stop truncation rewind: raw=1827 final=1536 playbackTick=291",
            "[1840.890] [DEBUG] [TRACK] Recording stopped @ tick 291 (recStart=0 length=1536)",
        ]
        bounds = extract_phase_boundaries(lines)
        self.assertEqual(bounds["record_start_ts"], 1836133000)
        self.assertEqual(bounds["record_stop_ts"], 1840890000)


class ExtractFirstNoteOffsetTests(unittest.TestCase):
    def test_offset_from_us_delta_at_120_bpm(self) -> None:
        # 120 BPM → 20833 us per clock; 2 clocks ≈ 41666 us
        lines = [
            "#CAP,1000000,ST,Track,ARMED,RECORDING",
            "#CAP,1041666,MI,U,144,5,60,100",
        ]
        result = extract_first_note_offset(
            lines,
            midi_channel_1based=5,
            phase_start_ts=1_000_000,
            phase_stop_ts=2_000_000,
            fallback_bpm=120.0,
        )
        self.assertEqual(result["offset_us"], 41666)
        self.assertEqual(result["offset_clocks"], 2)
        self.assertEqual(result["capture_latency_clocks"], 2)
        self.assertFalse(result["phase_missing"])

    def test_subtracts_configured_phase_start_delay(self) -> None:
        # Default overdub: 1 beat delay (24 clocks); first note at 25 clocks → latency 1
        delay = phase_start_delay_clocks(delay_bars=0, delay_beats=1)
        self.assertEqual(delay, 24)
        us_per_clock = 60_000_000.0 / (120.0 * 24.0)
        phase_start = 1_000_000
        first_note_ts = phase_start + int(round(25 * us_per_clock))
        lines = [
            f"#CAP,{phase_start},ST,Track,PLAYING,OVERDUBBING",
            f"#CAP,{first_note_ts},MI,U,144,5,24,100",
        ]
        result = extract_first_note_offset(
            lines,
            midi_channel_1based=5,
            phase_start_ts=phase_start,
            phase_stop_ts=phase_start + 10_000_000,
            fallback_bpm=120.0,
            phase_start_delay_clocks=delay,
        )
        self.assertEqual(result["offset_clocks"], 25)
        self.assertEqual(result["capture_latency_clocks"], 1)
        self.assertEqual(result["phase_start_delay_clocks"], 24)

    def test_uses_serial_bpm_in_phase_window(self) -> None:
        lines = [
            "#CAP,1000000,ST,Track,ARMED,RECORDING",
            "#CAP,1002000,BPM,60.0,60.0",
            "#CAP,1010000,MI,U,144,5,60,100",
        ]
        result = extract_first_note_offset(
            lines,
            midi_channel_1based=5,
            phase_start_ts=1_000_000,
            phase_stop_ts=2_000_000,
            fallback_bpm=120.0,
        )
        # 60 BPM → 41666.67 us/clock; 10000 us ≈ 0.24 clocks → rounds to 0
        self.assertEqual(result["offset_us"], 10_000)
        self.assertEqual(result["offset_clocks"], 0)

    def test_missing_first_note(self) -> None:
        lines = ["#CAP,1000000,ST,Track,ARMED,RECORDING"]
        result = extract_first_note_offset(
            lines,
            midi_channel_1based=5,
            phase_start_ts=1_000_000,
            phase_stop_ts=2_000_000,
            fallback_bpm=120.0,
        )
        self.assertIsNone(result["offset_clocks"])


if __name__ == "__main__":
    unittest.main()
