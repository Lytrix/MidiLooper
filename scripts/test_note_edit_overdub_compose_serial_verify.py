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
from hitl.stimuli import STIM_NE_OD_COMPOSE_01, set_run_stimulus_override, temporary_variant
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

    def test_temp_a_same_pitch_hold_requires_overlap_action(self) -> None:
        overdubs = []
        for overdub in STIM_NE_OD_COMPOSE_01.overdubs:
            if overdub.name == "IN_EDIT_OD":
                overdubs.append(replace(overdub, note_low=48, note_high=48))
            else:
                overdubs.append(overdub)
        temp = replace(
            temporary_variant(STIM_NE_OD_COMPOSE_01.stimulus_id, "A"),
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
        for overdub in STIM_NE_OD_COMPOSE_01.overdubs:
            if overdub.name == "IN_EDIT_OD":
                overdubs.append(replace(overdub, note_low=48, note_high=48))
            else:
                overdubs.append(overdub)
        temp = replace(
            temporary_variant(STIM_NE_OD_COMPOSE_01.stimulus_id, "A"),
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


if __name__ == "__main__":
    unittest.main()
