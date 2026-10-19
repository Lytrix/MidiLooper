#!/usr/bin/env python3
"""Host tests for edit HITL fader1 selection wait helpers."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from unittest.mock import patch

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from hitl.legacy_edit_baseline import (  # noqa: E402
    EDIT_RECORD_FIXTURE,
    FixtureNote,
    M0_PITCH,
    RecordLayout,
    SelectNavSlot,
    WRAP_SEAM_PITCH,
    _extract_revt_notes_from_dnte,
    _length_edit_mode_line,
    _m0_length_change_line,
    _nav_slot_indices_for_fixture_note,
    _parse_select_note_idx_tick,
    _parse_select_tick_from_line,
    _serial_length_edit_mode_enabled,
    _wait_for_select_at_tick,
    _wait_for_select_quiet,
    _wait_for_select_stable_at_tick,
)


class _FakeCollector:
    def __init__(self, lines: list[str]) -> None:
        self._lines = list(lines)

    def snapshot(self) -> list[str]:
        return list(self._lines)


class EditSelectWaitTests(unittest.TestCase):
    def test_parse_select_apply_line(self) -> None:
        parsed = _parse_select_tick_from_line(
            "#DBG select_apply selected_tick=192 note_idx=4 slot=4 apply=1 reason=note_changed"
        )
        self.assertEqual(parsed, (192, True))

    def test_parse_human_select_line(self) -> None:
        parsed = _parse_select_tick_from_line(
            "[101.452] [INFO] EditSelectNoteState: Found and selected note 0 at tick 0"
        )
        self.assertEqual(parsed, (0, True))

    def test_parse_select_note_idx_tick(self) -> None:
        parsed = _parse_select_note_idx_tick(
            "[INFO] Select fader: selected note 0 at tick 46"
        )
        self.assertEqual(parsed, (0, 46))

    def test_wait_for_select_at_tick(self) -> None:
        collector = _FakeCollector(
            [
                "[1.0] noise",
                "#DBG select_apply selected_tick=48 note_idx=1 slot=1 apply=1 reason=note_changed",
            ]
        )
        self.assertTrue(
            _wait_for_select_at_tick(collector, 46, timeout_s=0.2, quiet_ms=0)
        )

    def test_wait_for_select_at_tick_with_note_idx(self) -> None:
        collector = _FakeCollector(
            [
                "Select fader: selected note 0 at tick 46",
            ]
        )
        self.assertTrue(
            _wait_for_select_at_tick(
                collector, 46, timeout_s=0.2, quiet_ms=0, expected_note_idx=0
            )
        )

    def test_wait_for_select_stable_rejects_wrong_tick(self) -> None:
        collector = _FakeCollector(
            [
                "#DBG select_apply selected_tick=1391 note_idx=7 slot=7 apply=1 reason=note_changed",
            ]
        )
        self.assertFalse(
            _wait_for_select_stable_at_tick(
                collector, 0, timeout_s=0.15, quiet_ms=0
            )
        )

    def test_wait_for_select_quiet(self) -> None:
        collector = _FakeCollector(
            [
                "#DBG select_apply selected_tick=0 note_idx=0 slot=0 apply=1 reason=note_changed",
            ]
        )
        _wait_for_select_quiet(collector, quiet_ms=50, timeout_s=0.5)

    def test_nav_slot_pitch_filters_wrap_seam(self) -> None:
        layout = RecordLayout(
            loop_start=0,
            loop_length=1536,
            step_to_tick={0: 0, 28: 1344},
            nav_slots=[
                SelectNavSlot(rel_tick=0, note_idx=0),
                SelectNavSlot(rel_tick=0, note_idx=7),
                SelectNavSlot(rel_tick=1391, note_idx=7),
            ],
            note_index_to_pitch={0: 60, 7: 55},
        )
        m0_slots = _nav_slot_indices_for_fixture_note(
            layout, fixture_step=0, fixture=EDIT_RECORD_FIXTURE
        )
        self.assertEqual(m0_slots, [0])
        wrap_at_head = _nav_slot_indices_for_fixture_note(
            layout,
            fixture_step=0,
            fixture=(FixtureNote(0, WRAP_SEAM_PITCH),),
        )
        self.assertEqual(wrap_at_head, [1])

    def test_extract_revt_from_dnte_fixture_order(self) -> None:
        lines = [
            "#CAP,1,DNTE,60,46,46,192,2",
            "#CAP,2,DNTE,64,238,238,192,3",
            "#CAP,3,DNTE,67,430,430,192,1",
            "#CAP,4,DNTE,60,622,622,192,2",
        ]
        pairs = _extract_revt_notes_from_dnte(lines, EDIT_RECORD_FIXTURE[:4])
        self.assertEqual(
            pairs,
            [(46, 60), (238, 64), (430, 67), (622, 60)],
        )

class FinalSelectIndexTests(unittest.TestCase):
    def wait(self, rows, *, baseline=0):
        now = [0.0]
        def sleep(seconds):
            now[0] += seconds
        with patch("hitl.legacy_edit_baseline.time.monotonic", side_effect=lambda: now[0]), \
             patch("hitl.legacy_edit_baseline.time.sleep", side_effect=sleep):
            return _wait_for_select_stable_at_tick(
                _FakeCollector(rows), 392, expected_note_idx=2,
                baseline_len=baseline, quiet_ms=80, timeout_s=.2,
            )

    def test_current_fader_commit_reindexes_same_target(self):
        self.assertTrue(self.wait([
            "[65.021] Select fader: selected note 3 at tick 392",
            "[65.022] NOTE_EDIT focus rebuild: noteId=3 presence=0 rowProjectsToStore=1",
            "[65.023] Note selection changed: 4 -> 2",
        ]))

    def test_delayed_precommit_twin_does_not_undo_final_index(self):
        self.assertTrue(self.wait([
            "[65.021] Select fader: selected note 3 at tick 392",
            "[65.023] Note selection changed: 4 -> 2",
            "#DBG select_apply selected_tick=392 note_idx=3 slot=2 apply=1 reason=note_changed",
        ]))

    def test_final_wrong_index_overrides_matching_precommit_index(self):
        self.assertFalse(self.wait([
            "[65.021] Select fader: selected note 2 at tick 392",
            "[65.023] Note selection changed: 4 -> 3",
        ]))

    def test_final_index_cannot_repair_wrong_tick(self):
        self.assertFalse(self.wait([
            "[65.021] Select fader: selected note 3 at tick 336",
            "[65.023] Note selection changed: 4 -> 2",
        ]))

    def test_final_index_without_current_target_is_not_proof(self):
        self.assertFalse(self.wait(["[65.023] Note selection changed: 4 -> 2"]))

    def test_target_before_command_boundary_cannot_pair_with_final_index(self):
        self.assertFalse(self.wait([
            "[65.021] Select fader: selected note 3 at tick 392",
            "[65.023] Note selection changed: 4 -> 2",
        ], baseline=1))

    def test_deselect_overrides_matching_precommit_index(self):
        self.assertFalse(self.wait([
            "[65.021] Select fader: selected note 2 at tick 392",
            "[65.023] Note selection changed: 2 -> -1",
        ]))

    def test_later_target_supersedes_matching_final_selection(self):
        self.assertFalse(self.wait([
            "[65.021] Select fader: selected note 3 at tick 392",
            "[65.023] Note selection changed: 4 -> 2",
            "[65.024] Select fader: selected note 6 at tick 584",
            "[65.025] Note selection changed: 2 -> 2",
        ]))



class TestLengthEditHelpers(unittest.TestCase):
    def test_length_mode_line_case_insensitive(self) -> None:
        self.assertTrue(
            _length_edit_mode_line(
                "[INFO] Length editing mode ENABLED", enabled=True
            )
        )
        self.assertTrue(
            _length_edit_mode_line(
                "[INFO] Length editing mode DISABLED (note select)", enabled=False
            )
        )

    def test_m0_length_change_line(self) -> None:
        line = (
            "Note length change with overlap handling: pitch=60, start=1, end 190->693"
        )
        self.assertTrue(_m0_length_change_line(line, pitch=M0_PITCH))
        self.assertFalse(_m0_length_change_line(line, pitch=64))

    def test_serial_length_mode_tail(self) -> None:
        collector = _FakeCollector(
            [
                "[INFO] Length editing mode ENABLED",
                "[INFO] Length editing mode DISABLED (note select)",
            ]
        )
        self.assertFalse(_serial_length_edit_mode_enabled(collector))


if __name__ == "__main__":
    raise SystemExit(unittest.main())
