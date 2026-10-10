"""Regression for the real fixture streamer with optional stop trigger."""
import sys
import unittest
from pathlib import Path
from unittest.mock import Mock, patch
sys.path.insert(0, str(Path(__file__).resolve().parent))
from hitl import legacy_edit_baseline as baseline


class FixtureRecordStopTest(unittest.TestCase):
    def test_stop_modes(self):
        for advance, expected in ((0, 0), (-1, 1), (12, 1)):
            with self.subTest(advance=advance):
                output, input_port, master = Mock(), Mock(), Mock()
                input_port.poll.return_value = None
                with patch.object(baseline, "_send_short_press") as stop:
                    notes, clocks = baseline._stream_fixture_record(
                        output, input_port, fixture=(), target_bars=1,
                        midi_channel_1based=8, stop_press_advance_clocks=advance,
                        master=master, max_seconds_guard=10,
                    )
                self.assertEqual((notes, clocks), (0, baseline.MIDI_CLOCKS_PER_BAR))
                self.assertEqual(stop.call_count, expected)
                self.assertEqual(master.send_pulses.call_count, baseline.MIDI_CLOCKS_PER_BAR)


if __name__ == "__main__":
    unittest.main()
