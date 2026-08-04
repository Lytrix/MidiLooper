#!/usr/bin/env python3
"""Unit tests for foundation_runner verify-only path."""

from __future__ import annotations

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
        scenario_ids=("record_seed",),
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
    def test_verify_only_record_seed_writes_report(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log_path = Path(tmp) / "serial.log"
            log_path.write_text(
                "\n".join(
                    [
                        "#CAP,1000,ST,Track,ARMED,RECORDING",
                        "#CAP,2000,RECS,stop,100,200,1536,1536",
                    ]
                ),
                encoding="utf-8",
            )
            config = _test_config(verify_serial_log=log_path, out_dir=Path(tmp))
            code = run_layered_preset(config)
            self.assertEqual(code, 0)
            reports = list(Path(tmp).glob("host_midi_hitl_*.json"))
            self.assertEqual(len(reports), 1)


if __name__ == "__main__":
    unittest.main()
