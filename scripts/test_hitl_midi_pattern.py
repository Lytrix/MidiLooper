#!/usr/bin/env python3
"""Parser contract for the HITL MIDI-file option (DEC-221).

Pins the properties a run depends on: bar-rounded loop length, clock positions, the wrap that makes
a note-off written at the end of the bar re-trigger cleanly on the next loop, velocity-0 note_on as
note-off, and the explicit rejection of a loop length shorter than the file.
"""

from __future__ import annotations

import pathlib
import sys
import tempfile
import unittest

import mido

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from hitl.midi_pattern import MIDI_CLOCKS_PER_BEAT, load_midi_pattern  # noqa: E402


def _write_pattern(path: pathlib.Path, ticks_per_beat: int = 96) -> None:
    """One bar of 16ths, gate = one step, with the last note-off written at the bar line."""
    midi = mido.MidiFile(ticks_per_beat=ticks_per_beat)
    track = mido.MidiTrack()
    midi.tracks.append(track)
    step = ticks_per_beat // 4  # a 16th
    track.append(mido.Message("note_on", note=84, velocity=100, time=0))
    for index in range(16):
        note = 84 - index
        track.append(mido.Message("note_off", note=note, velocity=64, time=step))
        if index < 15:
            track.append(mido.Message("note_on", note=note - 1, velocity=100, time=0))
    midi.save(str(path))


class MidiPatternTests(unittest.TestCase):
    def test_one_bar_of_16ths_maps_to_clocks(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "16ths.mid"
            _write_pattern(path)
            pattern = load_midi_pattern(path)
            self.assertEqual(96, pattern.ticks_per_beat)
            self.assertEqual(384, pattern.length_ticks)
            self.assertEqual(96, pattern.clock_length)  # one 4/4 bar
            self.assertEqual(16, pattern.source_note_ons)
            # A 16th is a quarter of a beat = 6 clocks.
            for step in range(16):
                events = pattern.events_at(step * 6)
                self.assertTrue(any(event.note_on for event in events), f"no on at step {step}")
            self.assertEqual(0, pattern.rounded_positions)

    def test_bar_line_note_off_wraps_and_stays_after_the_next_note_on_set(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "16ths.mid"
            _write_pattern(path)
            pattern = load_midi_pattern(path)
            events = pattern.events_at(0)
            # The last off edits position 0 (wrap), and it must be emitted before the new on so the
            # re-trigger is clean rather than a same-pulse on/off.
            self.assertEqual(["off", "on"], ["on" if e.note_on else "off" for e in events])
            self.assertFalse(events[0].note_on)

    def test_velocity_zero_note_on_is_a_note_off(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "zero.mid"
            midi = mido.MidiFile(ticks_per_beat=96)
            track = mido.MidiTrack()
            midi.tracks.append(track)
            track.append(mido.Message("note_on", note=60, velocity=100, time=0))
            track.append(mido.Message("note_on", note=60, velocity=0, time=96))
            midi.save(str(path))
            pattern = load_midi_pattern(path)
            self.assertEqual(1, pattern.source_note_ons)
            offs = [e for e in pattern.events if not e.note_on]
            self.assertEqual(1, len(offs))
            self.assertEqual(1 * MIDI_CLOCKS_PER_BEAT, offs[0].clock)  # a quarter note later

    def test_loop_bars_shorter_than_the_file_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "long.mid"
            midi = mido.MidiFile(ticks_per_beat=96)
            track = mido.MidiTrack()
            midi.tracks.append(track)
            track.append(mido.Message("note_on", note=60, velocity=100, time=0))
            track.append(mido.Message("note_off", note=60, velocity=0, time=96 * 8))
            midi.save(str(path))
            with self.assertRaises(ValueError):
                load_midi_pattern(path, loop_bars=1)

    def test_explicit_loop_bars_sets_the_wrap_length(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "16ths.mid"
            _write_pattern(path)
            pattern = load_midi_pattern(path, loop_bars=2)
            self.assertEqual(192, pattern.clock_length)


class WindowPlanTests(unittest.TestCase):
    """The beat plan is the contract the Record button is timed against."""

    def test_windows_lay_out_end_to_end_with_the_gap(self) -> None:
        module = _load_chain_module()
        self.assertEqual([(0, 24), (48, 96), (168, 192)], module._window_offsets([1.0, 4.0, 8.0], 1.0))

    def test_press_clocks_matches_the_pulse_rate(self) -> None:
        module = _load_chain_module()
        # 85 ms at 120 BPM, 24 PPQN -> ~4 clocks.
        self.assertEqual(4, module._press_clocks(85, 120.0))
        self.assertEqual(1, module._press_clocks(5, 120.0))

    def test_gap_above_four_beats_is_refused_by_the_cli(self) -> None:
        module = _load_chain_module()
        with self.assertRaises(SystemExit):
            module.main([
                "--record-pattern", "x.mid", "--overdub-pattern", "y.mid",
                "--overdub-gap-beats", "5", "--out-dir", "/tmp/never",
            ])


def _load_chain_module():
    import importlib

    return importlib.import_module("hitl_midifile_chain")


if __name__ == "__main__":
    unittest.main()
