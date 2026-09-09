#!/usr/bin/env python3
"""Host tests for NOTE_EDIT plus in-edit overdub compose HITL verifier."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.baseline_loop_inventory import resolve_base_seed_serial_log
from hitl.registry import resolve_scenarios
from hitl.scenarios.note_edit_overdub_compose import (
    _temp_hold_target_step,
    _temp_layout_with_live_notes,
    _temp_seed_slot_index,
    last_bar_fourth_beat_tick,
    live_tick_near_target,
    parse_select_apply,
)
from hitl.stimuli import (
    STIM_NE_OD_COMPOSE_02,
    STIM_NE_OD_COMPOSE_03,
    STIM_NE_OD_COMPOSE_04,
    prepend_hitl_header,
    set_run_stimulus_override,
    temporary_variant,
)
from hitl.verify.note_edit_overdub_compose import (
    LIVE_CAPTURE_HIGH,
    LIVE_CAPTURE_LOW,
    verify_note_edit_overdub_compose,
)
from dataclasses import replace


def _pass_lines() -> list[str]:
    return [
        "[31.000] #CAP,31000000,ST,Track,PLAYING,OVERDUBBING",
        "[31.100] #CAP,31100000,MI,U,144,5,24,100",
        "[53.290] #CAP,NOTE_EDIT_OPEN,sync,visual_notes,48,session_events,0,current_state,1",
        "[53.300] Edit session: NOTE_EDIT",
        "[53.400] #CAP,53400000,DFRAME,48,1000,1,100,100,100",
        "[55.785] #CAP,55785000,ST,Track,PLAYING,OVERDUBBING",
        "[56.000] #CAP,56000000,MI,U,144,5,12,100",
        "[56.050] #CAP,56050000,DFRAME,49,1000,2,100,100,100",
        "[56.200] #CAP,56200000,MI,U,224,14,44,16",
        "[56.300] #CAP,56300000,DFRAME,50,1000,3,100,100,100",
        "[56.400] #CAP,56400000,MI,U,144,5,13,100",
        "[56.500] #CAP,56500000,DFRAME,51,1000,4,100,100,100",
        "[57.000] #CAP,57000000,ST,Track,OVERDUBBING,PLAYING",
        "[57.100] #CAP,57100000,DFRAME,51,1000,5,100,100,100",
    ]


def _collapse_lines() -> list[str]:
    return [
        "[53.290] #CAP,NOTE_EDIT_OPEN,sync,visual_notes,48,session_events,0,current_state,1",
        "[53.300] Edit session: NOTE_EDIT",
        "[55.785] #CAP,55785000,ST,Track,PLAYING,OVERDUBBING",
        "[56.000] #CAP,56000000,MI,U,144,5,12,100",
        "[56.300] #CAP,56300000,DFRAME,48,1000,3,100,100,100",
        "[56.500] #CAP,56500000,DFRAME,48,1000,4,100,100,100",
        "[57.000] #CAP,57000000,ST,Track,OVERDUBBING,PLAYING",
        "[57.100] #CAP,57100000,DFRAME,66,1000,5,100,100,100",
    ]


class NoteEditOverdubComposeSerialVerifyTests(unittest.TestCase):
    def test_follow_session_resolves_seed_serial_without_report_path(self) -> None:
        import tempfile

        with tempfile.TemporaryDirectory() as raw:
            out_dir = Path(raw)
            session = out_dir / "session_follow.log"
            session.write_text("#CAP,1,ST,Track,PLAYING,OVERDUBBING\n", encoding="utf-8")
            (out_dir / ".current_session").write_text(str(session), encoding="utf-8")
            resolved = resolve_base_seed_serial_log({"serial_log_path": None}, out_dir)
            self.assertEqual(resolved, session)

    def test_preset_is_seed_then_compose_cases(self) -> None:
        self.assertEqual(
            resolve_scenarios(preset="note_edit_overdub_compose", scenario_ids=None),
            ["base", "note_edit_overdub_compose"],
        )
        self.assertEqual(
            resolve_scenarios(
                preset="note_edit_overdub_compose_last_bar_shorten",
                scenario_ids=None,
            ),
            ["base", "note_edit_overdub_compose_last_bar_shorten"],
        )
        self.assertEqual(
            resolve_scenarios(
                preset="note_edit_overdub_compose_last_bar_reopen",
                scenario_ids=None,
            ),
            ["base", "note_edit_overdub_compose_last_bar_reopen"],
        )

    def test_last_bar_fourth_beat_tick(self) -> None:
        self.assertEqual(last_bar_fourth_beat_tick(2304), 2112)
        self.assertEqual(last_bar_fourth_beat_tick(1536), 1344)
        self.assertEqual(
            live_tick_near_target([2118, 15], 2112, 2304, 192),
            2118,
        )
        self.assertIsNone(live_tick_near_target([15], 2112, 2304, 192))

    def test_live_notes_stay_on_roll_pass(self) -> None:
        check = verify_note_edit_overdub_compose(_pass_lines(), SimpleNamespace())
        case = check["cases"]["live_overdub_notes_stay_on_roll"]
        self.assertTrue(check["ok"], check["issues"])
        self.assertTrue(case["ok"], case["issues"])
        self.assertEqual(case["open_visual_notes"], 48)
        self.assertGreaterEqual(case["live_capture_note_ons"], 1)
        self.assertGreater(case["max_dframe_during_overdub"], 48)
        self.assertEqual(check["live_capture_pitch_band"]["low"], LIVE_CAPTURE_LOW)
        self.assertEqual(check["live_capture_pitch_band"]["high"], LIVE_CAPTURE_HIGH)

    def test_dframe_collapse_to_snapshot_count_fails(self) -> None:
        check = verify_note_edit_overdub_compose(_collapse_lines(), SimpleNamespace())
        self.assertFalse(check["ok"])
        self.assertTrue(
            any("dframe_omitted_live_capture" in issue for issue in check["issues"])
        )

    def test_missing_note_edit_open_fails(self) -> None:
        check = verify_note_edit_overdub_compose(
            [
                "[55.785] #CAP,55785000,ST,Track,PLAYING,OVERDUBBING",
                "[56.000] #CAP,56000000,MI,U,144,5,12,100",
                "[56.200] POSITION EDIT: start=96",
                "[56.300] #CAP,56300000,DFRAME,49,1000,3,100,100,100",
            ],
            SimpleNamespace(),
        )
        self.assertFalse(check["ok"])
        self.assertTrue(any("missing_note_edit_open" in issue for issue in check["issues"]))

    def test_device_110653_omits_live_capture_until_stop(self) -> None:
        capture = Path(__file__).resolve().parent.parent / (
            "captures/note_edit_overdub_compose_20260909_110653_serial.log"
        )
        if not capture.is_file():
            self.skipTest("110653 compose serial log not in workspace")
        lines = capture.read_text(encoding="utf-8", errors="replace").splitlines()
        check = verify_note_edit_overdub_compose(lines, SimpleNamespace())
        case = check["cases"]["live_overdub_notes_stay_on_roll"]
        self.assertFalse(check["ok"])
        self.assertEqual(case["open_visual_notes"], 49)
        self.assertGreaterEqual(case["live_capture_note_ons"], 1)
        self.assertEqual(case["max_dframe_during_overdub"], 49)
        self.assertTrue(
            any("dframe_omitted_live_capture" in issue for issue in check["issues"])
        )

    def test_seed_overdub_pitch_is_not_live_capture(self) -> None:
        check = verify_note_edit_overdub_compose(
            [
                "[53.290] #CAP,NOTE_EDIT_OPEN,sync,visual_notes,48,session_events,0,current_state,1",
                "[55.785] #CAP,55785000,ST,Track,PLAYING,OVERDUBBING",
                "[56.000] #CAP,56000000,MI,U,144,5,24,100",
                "[56.200] POSITION EDIT: start=96",
                "[56.300] #CAP,56300000,DFRAME,49,1000,3,100,100,100",
            ],
            SimpleNamespace(),
        )
        self.assertFalse(check["ok"])
        self.assertTrue(
            any("missing_live_capture_note_on" in issue for issue in check["issues"])
        )

    def test_temp_lock_keeps_seed_and_moves_onto_later_live_note(self) -> None:
        seed_tick = 104
        live_ticks = [0, 96, 192, 288, 384]
        layout = _temp_layout_with_live_notes(
            [(seed_tick, 48), (480, 49)], live_ticks, 48, 2304
        )
        seed_slot = _temp_seed_slot_index(layout, seed_tick, 48)
        self.assertIsNotNone(seed_slot)
        seed = layout.nav_slots[seed_slot]
        self.assertEqual(seed.rel_tick, seed_tick)
        self.assertEqual(layout.note_index_to_pitch[seed.note_idx], 48)
        self.assertEqual(_temp_hold_target_step(live_ticks, seed_tick, 2304), 6)
        tick, note_idx, applied = parse_select_apply(
            "#DBG select_apply selected_tick=81 note_idx=7 slot=3 prior_slot=1 apply=1 reason=note"
        )
        self.assertEqual((tick, note_idx, applied), (81, 7, 1))

    def test_compose_02_header_requires_overlap_action(self) -> None:
        lines = prepend_hitl_header(
            [
                "[53.290] #CAP,NOTE_EDIT_OPEN,sync,visual_notes,48,session_events,0,current_state,1",
                "[55.785] #CAP,55785000,ST,Track,PLAYING,OVERDUBBING",
                "[56.000] #CAP,56000000,MI,U,144,5,12,100",
                "[56.050] #CAP,56050000,DFRAME,49,1000,2,100,100,100",
                "[56.200] POSITION EDIT: start=192",
                "[56.250] #CAP,56250000,GEOM_APPLY,phase,build,800,2,0",
                "[56.260] EditSessionAction: type=2 noteId=99 start=96 end=192 pitch=12",
                "[56.270] overlapNotes=1",
                "[56.300] #CAP,56300000,DFRAME,50,1000,3,100,100,100",
                "[57.000] #CAP,57000000,ST,Track,OVERDUBBING,PLAYING",
            ],
            STIM_NE_OD_COMPOSE_02,
        )
        check = verify_note_edit_overdub_compose(lines, SimpleNamespace())
        self.assertTrue(check["ok"], check["issues"])
        overlap = check["cases"]["same_pitch_hold_emits_overlap"]
        self.assertTrue(overlap["ok"], overlap["issues"])
        self.assertGreaterEqual(overlap["hide_or_shorten"], 1)

    def test_compose_02_move_note_only_is_not_overlap(self) -> None:
        lines = prepend_hitl_header(
            [
                "[53.290] #CAP,NOTE_EDIT_OPEN,sync,visual_notes,48,session_events,0,current_state,1",
                "[55.785] #CAP,55785000,ST,Track,PLAYING,OVERDUBBING",
                "[56.000] #CAP,56000000,MI,U,144,5,12,100",
                "[56.050] #CAP,56050000,DFRAME,49,1000,2,100,100,100",
                "[56.200] POSITION EDIT: start=192",
                "[56.250] #CAP,56250000,GEOM_APPLY,phase,build,800,1,0",
                "[56.260] EditSessionAction: type=3 noteId=58 start=240 end=382 pitch=12",
                "[56.300] #CAP,56300000,DFRAME,50,1000,3,100,100,100",
                "[57.000] #CAP,57000000,ST,Track,OVERDUBBING,PLAYING",
            ],
            STIM_NE_OD_COMPOSE_02,
        )
        check = verify_note_edit_overdub_compose(lines, SimpleNamespace())
        self.assertFalse(check["ok"])
        self.assertTrue(
            any("missing_same_pitch_hold_overlap_action" in issue for issue in check["issues"])
        )

    def test_temp_a_same_pitch_hold_requires_overlap_action(self) -> None:
        overdubs = []
        for overdub in STIM_NE_OD_COMPOSE_02.overdubs:
            if overdub.name == "IN_EDIT_OD":
                overdubs.append(replace(overdub, note_low=48, note_high=48))
            else:
                overdubs.append(overdub)
        temp = replace(
            temporary_variant(STIM_NE_OD_COMPOSE_02.stimulus_id, "A"),
            overdubs=tuple(overdubs),
        )
        set_run_stimulus_override(temp)
        try:
            check = verify_note_edit_overdub_compose(
                [
                    "[53.290] #CAP,NOTE_EDIT_OPEN,sync,visual_notes,48,session_events,0,current_state,1",
                    "[55.785] #CAP,55785000,ST,Track,PLAYING,OVERDUBBING",
                    "[56.000] #CAP,56000000,MI,U,144,5,48,100",
                    "[56.050] #CAP,56050000,DFRAME,49,1000,2,100,100,100",
                    "[56.200] POSITION EDIT: start=192",
                    "[56.250] #CAP,56250000,GEOM_APPLY,phase,build,800,2,0",
                    "[56.260] EditSessionAction: type=2 noteId=99 start=96 end=192 pitch=48",
                    "[56.270] overlapNotes=1",
                    "[56.300] #CAP,56300000,DFRAME,50,1000,3,100,100,100",
                    "[57.000] #CAP,57000000,ST,Track,OVERDUBBING,PLAYING",
                ],
                SimpleNamespace(),
            )
            self.assertTrue(check["ok"], check["issues"])
            self.assertEqual(check["live_capture_pitch_band"]["low"], 48)
            overlap = check["cases"]["same_pitch_hold_emits_overlap"]
            self.assertTrue(overlap["ok"], overlap["issues"])
            self.assertGreaterEqual(overlap["hide_or_shorten"], 1)
        finally:
            set_run_stimulus_override(None)

    def test_temp_a_move_note_only_is_not_overlap(self) -> None:
        overdubs = []
        for overdub in STIM_NE_OD_COMPOSE_02.overdubs:
            if overdub.name == "IN_EDIT_OD":
                overdubs.append(replace(overdub, note_low=48, note_high=48))
            else:
                overdubs.append(overdub)
        temp = replace(
            temporary_variant(STIM_NE_OD_COMPOSE_02.stimulus_id, "A"),
            overdubs=tuple(overdubs),
        )
        set_run_stimulus_override(temp)
        try:
            check = verify_note_edit_overdub_compose(
                [
                    "[53.290] #CAP,NOTE_EDIT_OPEN,sync,visual_notes,48,session_events,0,current_state,1",
                    "[55.785] #CAP,55785000,ST,Track,PLAYING,OVERDUBBING",
                    "[56.000] #CAP,56000000,MI,U,144,5,48,100",
                    "[56.050] #CAP,56050000,DFRAME,49,1000,2,100,100,100",
                    "[56.200] POSITION EDIT: start=192",
                    "[56.250] #CAP,56250000,GEOM_APPLY,phase,build,800,1,0",
                    "[56.260] EditSessionAction: type=3 noteId=58 start=240 end=382 pitch=48",
                    "[56.300] #CAP,56300000,DFRAME,50,1000,3,100,100,100",
                    "[57.000] #CAP,57000000,ST,Track,OVERDUBBING,PLAYING",
                ],
                SimpleNamespace(),
            )
            self.assertFalse(check["ok"])
            self.assertTrue(
                any("missing_same_pitch_hold_overlap_action" in issue for issue in check["issues"])
            )
        finally:
            set_run_stimulus_override(None)

    def test_compose_03_header_requires_shorten(self) -> None:
        lines = prepend_hitl_header(
            [
                "[53.290] #CAP,NOTE_EDIT_OPEN,sync,visual_notes,48,session_events,0,current_state,1",
                "[55.785] #CAP,55785000,ST,Track,PLAYING,OVERDUBBING",
                "[56.000] #CAP,56000000,MI,U,144,5,12,100",
                "[56.050] #CAP,56050000,DFRAME,49,1000,2,100,100,100",
                "[56.200] POSITION EDIT: start=2112",
                "[56.250] #CAP,56250000,GEOM_APPLY,phase,build,800,2,0",
                "[56.260] EditSessionAction: type=1 noteId=99 start=2112 end=2166 pitch=12",
                "[56.270] overlapNotes=1",
                "[56.300] #CAP,56300000,DFRAME,50,1000,3,100,100,100",
                "[57.000] #CAP,57000000,ST,Track,OVERDUBBING,PLAYING",
            ],
            STIM_NE_OD_COMPOSE_03,
        )
        check = verify_note_edit_overdub_compose(
            lines,
            SimpleNamespace(active_scenario_id="note_edit_overdub_compose_last_bar_shorten"),
        )
        self.assertTrue(check["ok"], check["issues"])
        shortened = check["cases"]["new_note_shortened"]
        self.assertTrue(shortened["ok"], shortened["issues"])
        self.assertGreaterEqual(shortened["shorten"], 1)
        self.assertNotIn("same_pitch_hold_emits_overlap", check["cases"])

    def test_compose_03_hide_only_is_not_shorten(self) -> None:
        lines = prepend_hitl_header(
            [
                "[53.290] #CAP,NOTE_EDIT_OPEN,sync,visual_notes,48,session_events,0,current_state,1",
                "[55.785] #CAP,55785000,ST,Track,PLAYING,OVERDUBBING",
                "[56.000] #CAP,56000000,MI,U,144,5,12,100",
                "[56.050] #CAP,56050000,DFRAME,49,1000,2,100,100,100",
                "[56.200] POSITION EDIT: start=2112",
                "[56.250] #CAP,56250000,GEOM_APPLY,phase,build,800,2,0",
                "[56.260] EditSessionAction: type=2 noteId=99 start=96 end=192 pitch=12",
                "[56.270] overlapNotes=1",
                "[56.300] #CAP,56300000,DFRAME,50,1000,3,100,100,100",
                "[57.000] #CAP,57000000,ST,Track,OVERDUBBING,PLAYING",
            ],
            STIM_NE_OD_COMPOSE_03,
        )
        check = verify_note_edit_overdub_compose(
            lines,
            SimpleNamespace(active_scenario_id="note_edit_overdub_compose_last_bar_shorten"),
        )
        self.assertFalse(check["ok"])
        self.assertTrue(
            any("missing_new_note_shorten_action" in issue for issue in check["issues"])
        )

    def test_compose_04_header_requires_reopen_select_and_move(self) -> None:
        lines = prepend_hitl_header(
            [
                "[53.290] #CAP,NOTE_EDIT_OPEN,sync,visual_notes,48,session_events,0,current_state,1",
                "[55.785] #CAP,55785000,ST,Track,PLAYING,OVERDUBBING",
                "[56.000] #CAP,56000000,MI,U,144,5,12,100",
                "[56.050] #CAP,56050000,DFRAME,49,1000,2,100,100,100",
                "[56.200] POSITION EDIT: start=2112",
                "[56.250] #CAP,56250000,GEOM_APPLY,phase,build,800,2,0",
                "[56.260] EditSessionAction: type=1 noteId=99 start=2112 end=2166 pitch=12",
                "[56.270] overlapNotes=1",
                "[56.300] #CAP,56300000,DFRAME,50,1000,3,100,100,100",
                "[57.000] #CAP,57000000,ST,Track,OVERDUBBING,PLAYING",
                "[58.000] #CAP,NOTE_EDIT_OPEN,sync,visual_notes,72,session_events,255,current_state,0",
                "[58.010] Note selection changed: -1 -> 12",
                "[58.100] POSITION EDIT: start=192",
                "[58.110] EditSessionAction: type=3 noteId=12 start=192 end=288 pitch=48",
            ],
            STIM_NE_OD_COMPOSE_04,
        )
        check = verify_note_edit_overdub_compose(
            lines,
            SimpleNamespace(active_scenario_id="note_edit_overdub_compose_last_bar_reopen"),
        )
        self.assertTrue(check["ok"], check["issues"])
        reopen = check["cases"]["reopen_select_and_move"]
        self.assertTrue(reopen["ok"], reopen["issues"])
        self.assertGreaterEqual(reopen["selection_idx"], 0)
        self.assertGreaterEqual(reopen["moves"], 1)

    def test_compose_04_reopen_empty_select_fails(self) -> None:
        lines = prepend_hitl_header(
            [
                "[53.290] #CAP,NOTE_EDIT_OPEN,sync,visual_notes,48,session_events,0,current_state,1",
                "[55.785] #CAP,55785000,ST,Track,PLAYING,OVERDUBBING",
                "[56.000] #CAP,56000000,MI,U,144,5,12,100",
                "[56.050] #CAP,56050000,DFRAME,49,1000,2,100,100,100",
                "[56.200] POSITION EDIT: start=2112",
                "[56.260] EditSessionAction: type=1 noteId=99 start=2112 end=2166 pitch=12",
                "[57.000] #CAP,57000000,ST,Track,OVERDUBBING,PLAYING",
                "[58.000] #CAP,NOTE_EDIT_OPEN,sync,visual_notes,72,session_events,255,current_state,0",
                "[58.010] Note selection changed: 59 -> -1",
            ],
            STIM_NE_OD_COMPOSE_04,
        )
        check = verify_note_edit_overdub_compose(
            lines,
            SimpleNamespace(active_scenario_id="note_edit_overdub_compose_last_bar_reopen"),
        )
        self.assertFalse(check["ok"])
        self.assertTrue(any("reopen_select_empty" in issue for issue in check["issues"]))

    def test_control_plane_coord_ack_during_overdub_fails(self) -> None:
        lines = list(_pass_lines())
        lines.insert(
            6,
            (
                "[56.080] #CAP,56080000,COORD,abs,100,storage,13,proj,13,display,13,"
                "startLoop,0,projStart,0,loopStart,0,ch,13,note,84"
            ),
        )
        check = verify_note_edit_overdub_compose(lines, SimpleNamespace())
        self.assertFalse(check["ok"])
        self.assertTrue(
            any("overdub_coord_ch13_ack" in issue for issue in check["issues"])
        )

    def test_control_plane_sevt_ch15_cc_note_fails(self) -> None:
        lines = list(_pass_lines())
        lines.append("[57.200] #CAP,57200000,SEVT,N,96,15,3")
        check = verify_note_edit_overdub_compose(lines, SimpleNamespace())
        self.assertFalse(check["ok"])
        self.assertTrue(
            any("stored_control_plane_sevt" in issue for issue in check["issues"])
        )

    def test_pass_lines_have_no_control_plane_capture(self) -> None:
        check = verify_note_edit_overdub_compose(_pass_lines(), SimpleNamespace())
        case = check["cases"]["control_plane_not_captured"]
        self.assertTrue(case["ok"], case["issues"])
        self.assertEqual(case["overdub_coord_ch13_ack"], 0)


if __name__ == "__main__":
    unittest.main()
