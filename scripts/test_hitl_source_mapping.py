#!/usr/bin/env python3
"""Source geometry must round-trip through the firmware's logical PB mapping."""
from __future__ import annotations

import sys
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
from hitl import legacy_edit_baseline as edit
from hitl import select_mapping as mapping


class SourceMappingTests(unittest.TestCase):
    def setUp(self):
        self.port = Mock()
        self.collector = Mock()
        self.collector.snapshot.return_value = []
        self.wait = patch.object(edit, '_wait_for_select_stable_at_tick', return_value=True)
        self.wait_mock = self.wait.start()
        self.addCleanup(self.wait.stop)
        self.sleep = patch.object(edit.time, 'sleep')
        self.sleep.start()
        self.addCleanup(self.sleep.stop)

    def layout(self, notes=((0, 60), (12, 62), (24, 63), (200, 64), (392, 67))):
        slots = edit._build_select_navigation_slots(list(notes), loop_length=1536)
        return edit.RecordLayout(0, 1536, {4: 200, 8: 392}, slots,
                                 dict(enumerate(pitch for _, pitch in notes)))

    def select(self, layout, tick, note_idx=None, **kwargs):
        index = next(i for i, slot in enumerate(layout.nav_slots)
                     if slot.rel_tick == tick and (note_idx is None or slot.note_idx == note_idx))
        return edit._fader1_select_nav_slot_index(self.port, layout=layout, slot_index=index,
                                                 **kwargs)

    def last_pb(self):
        return self.port.send.call_args_list[-1].args[0].pitch

    def test_source_392_uses_step8_despite_extra_earlier_slots(self):
        self.select(self.layout(), 392, serial_collector=self.collector)
        self.assertEqual(mapping.select_tick_from_pitchbend(self.last_pb(), 1536), 384)
        self.wait_mock.assert_called_once_with(self.collector, 392, baseline_len=0,
                                              expected_note_idx=None)

    def test_move_step4_not_step3(self):
        pb = edit._pb_for_sixteenth_step(4, 32)
        self.assertEqual(mapping.select_tick_from_pitchbend(pb, 1536), 192)
        self.assertLessEqual(pb, 8191)

    def test_nav_false_wait_propagates(self):
        self.wait_mock.return_value = False
        self.assertIs(self.select(self.layout(), 392, serial_collector=self.collector), False)

    def test_fixture_false_wait_propagates(self):
        self.wait_mock.return_value = False
        self.assertIs(edit._fader1_select_fixture_note(self.port, layout=self.layout(),
                      fixture_step=8, serial_collector=self.collector), False)

    def test_without_serial_attempt_only_returns_true(self):
        self.assertIs(self.select(self.layout(), 392), True)
        self.wait_mock.assert_not_called()

    def test_local_stacked_ordinal_excludes_earlier_and_empty_slots(self):
        layout = self.layout(((0, 60), (12, 62), (24, 63), (200, 64),
                              (386, 76), (392, 67), (392, 72)))
        # Target is third in step8's actual start/pitch order, not global note index6.
        self.select(layout, 392, note_idx=6)
        pb = self.last_pb()
        self.assertEqual(mapping.select_tick_from_pitchbend(pb, 1536), 384)
        self.assertEqual(mapping.select_stacked_note_index_from_pitchbend(pb, 1536, 3), 2)

    def test_empty_slot_maps_step_not_global_slot(self):
        self.select(self.layout(), 240)
        self.assertEqual(mapping.select_tick_from_pitchbend(self.last_pb(), 1536), 240)

    def test_unreachable_last_step_refuses_before_any_message(self):
        with self.assertRaises(ValueError):
            self.select(self.layout(), 1488)
        self.port.send.assert_not_called()

    def test_invalid_slot_refuses_before_any_message(self):
        with self.assertRaises(ValueError):
            edit._fader1_select_nav_slot_index(self.port, layout=self.layout(), slot_index=-1)
        self.port.send.assert_not_called()

    def test_sixteenth_invalid_and_unreachable_not_clamped(self):
        for step, count in [(-1, 32), (32, 32), (31, 32), (0, 0)]:
            with self.subTest(step=step, count=count), self.assertRaises(ValueError):
                edit._pb_for_sixteenth_step(step, count)

    def test_single_step_zero_is_legal(self):
        pb = edit._pb_for_sixteenth_step(0, 1)
        self.assertTrue(-8192 <= pb <= 8191)
        self.assertEqual(mapping.select_tick_from_pitchbend(pb, 48), 0)

    def test_unreachable_move_endpoint_sends_nothing(self):
        with self.assertRaises(ValueError):
            edit._fader2_move_to_sixteenth_step(self.port, layout=self.layout(), fixture_step=31)
        self.port.send.assert_not_called()

    def test_pure_inverse_refuses_invalid_or_unreachable_targets(self):
        for step, nav, rank, count in [(31, 1536, 0, 1), (0, 0, 0, 1),
                                       (-1, 1536, 0, 1), (0, 1536, 1, 1),
                                       (0, 48, 1, 2), (0, 1536, 0, 0)]:
            with self.subTest(step=step, nav=nav, rank=rank, count=count):
                with self.assertRaises(ValueError):
                    mapping.pitchbend_for_step_and_stack(step, nav, rank, count)

    def test_custom_logical_bounds_still_produce_only_legal_wire(self):
        pb = mapping.pitchbend_for_step_and_stack(2, 1536, 0, 1, pitch_min=-9000)
        self.assertTrue(-8192 <= pb <= 8191)
        self.assertEqual(mapping.select_tick_from_pitchbend(pb, 1536, pitch_min=-9000), 96)

    def test_reachable_steps_and_ranks_roundtrip(self):
        for num_steps in (2, 16, 32):
            for step in range(num_steps - 1):
                for count in (1, 2, 3):
                    for rank in range(count):
                        with self.subTest(num_steps=num_steps, step=step, count=count, rank=rank):
                            pb = mapping.pitchbend_for_step_and_stack(step, num_steps * 48, rank, count)
                            self.assertTrue(-8192 <= pb <= 8191)
                            self.assertEqual(mapping.select_tick_from_pitchbend(pb, num_steps * 48), step * 48)
                            self.assertEqual(mapping.select_stacked_note_index_from_pitchbend(
                                pb, num_steps * 48, count), rank)


if __name__ == '__main__':
    unittest.main()
