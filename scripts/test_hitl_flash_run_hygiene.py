#!/usr/bin/env python3
"""Tests for post-flash MIDI port waiting and invocation recording (host-only, no device)."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.midi_io import wait_for_midi_ports
from hitl.run_metadata import write_invocation


class Clock:
    def __init__(self) -> None:
        self.t = 0.0

    def now(self) -> float:
        return self.t

    def sleep(self, s: float) -> None:
        self.t += s


class WaitForPortsTests(unittest.TestCase):
    def test_returns_true_once_both_ports_reappear(self) -> None:
        clock, calls = Clock(), {"n": 0}

        def ins():
            calls["n"] += 1
            return ["Teensy MIDI"] if calls["n"] > 3 else []

        self.assertTrue(wait_for_midi_ports("teensy", 10, get_inputs=ins, get_outputs=lambda: ["Teensy MIDI"],
                                            sleep=clock.sleep, now=clock.now))

    def test_needs_both_input_and_output(self) -> None:
        clock = Clock()
        self.assertFalse(wait_for_midi_ports("Teensy", 2, get_inputs=lambda: ["Teensy MIDI"],
                                             get_outputs=lambda: ["IAC Driver"], sleep=clock.sleep, now=clock.now))

    def test_times_out_without_raising(self) -> None:
        clock = Clock()
        self.assertFalse(wait_for_midi_ports("Teensy", 1, get_inputs=list, get_outputs=list,
                                             sleep=clock.sleep, now=clock.now))
        self.assertGreaterEqual(clock.t, 1)


class InvocationTests(unittest.TestCase):
    def test_records_argv_once_and_keeps_the_first(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = write_invocation(Path(tmp), ["x.py", "--leg", "od", "--slot-a", "4"])
            record = json.loads(path.read_text())
            self.assertEqual(record["argv"], ["x.py", "--leg", "od", "--slot-a", "4"])
            self.assertIn("do not execute", record["note"])
            write_invocation(Path(tmp), ["other.py"])
            self.assertEqual(json.loads(path.read_text())["argv"][0], "x.py")


if __name__ == "__main__":
    unittest.main()
