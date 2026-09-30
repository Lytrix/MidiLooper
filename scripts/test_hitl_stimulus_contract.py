#!/usr/bin/env python3
"""Host tests for the HITL stimulus registry and fingerprint."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.registry import resolve_scenarios
from hitl.runner import main as hitl_main
from hitl.stimuli import (
    STIM_BASE_2_2_01,
    STIM_BASE_2_FIXTURE_01,
    STIM_BASE_2_MIXED_01,
    STIM_BASE_2_ONE_OD_01,
    STIM_EDIT_OD_DURING_NE_01,
    STIM_NE_OD_COMPOSE_01,
    STIM_NE_OD_COMPOSE_02,
    STIM_NE_OD_COMPOSE_03,
    STIM_NE_RESELECT_STACKED_01,
    STIM_OCCUPY_PAINT_01,
    format_hitl_header,
    get_stimulus,
    prepend_hitl_header,
    run_stimulus,
    set_run_stimulus_override,
    stimulus_for_base_preset,
    stimulus_for_scenario,
    temporary_variant,
    validate_stimulus,
)
from hitl.stimuli.cli import default_temp_parent_id, write_temporary_sidecar
from hitl.stimuli.yaml_view import export_registry_yaml
from hitl.verify.note_edit_overdub_compose import verify_note_edit_overdub_compose
from test_note_edit_overdub_compose_serial_verify import _pass_lines


class StimulusRegistryTests(unittest.TestCase):
    def test_compose_scenario_binds_canonical_stimulus(self) -> None:
        spec = stimulus_for_scenario("note_edit_overdub_compose")
        self.assertIsNotNone(spec)
        assert spec is not None
        self.assertEqual(spec, STIM_NE_OD_COMPOSE_02)
        self.assertEqual(spec.stimulus_id, "STIM-NE-OD-COMPOSE-02")
        self.assertEqual(spec.version, 1)
        self.assertEqual(spec.base_stimulus_id, STIM_BASE_2_ONE_OD_01.stimulus_id)
        self.assertEqual(spec.generator, "scenario:note_edit_overdub_compose")
        self.assertIn("same_pitch_hold", spec.interactions)
        self.assertIn("same_pitch_hold_overlap", spec.must_prove)
        self.assertEqual(
            get_stimulus("STIM-NE-OD-COMPOSE-01").stimulus_id, "STIM-NE-OD-COMPOSE-01"
        )
        last_bar = stimulus_for_scenario("note_edit_overdub_compose_last_bar_shorten")
        self.assertEqual(last_bar, STIM_NE_OD_COMPOSE_03)
        self.assertEqual(last_bar.stimulus_id, "STIM-NE-OD-COMPOSE-03")
        self.assertIn("shorten_new_note_last_bar", last_bar.interactions)
        self.assertIn("new_note_shortened", last_bar.must_prove)
        self.assertEqual(
            stimulus_for_base_preset("note_edit_overdub_compose_last_bar_shorten"),
            STIM_BASE_2_ONE_OD_01,
        )
        last_bar_reopen = stimulus_for_scenario(
            "note_edit_overdub_compose_last_bar_reopen"
        )
        self.assertEqual(last_bar_reopen.stimulus_id, "STIM-NE-OD-COMPOSE-04")
        self.assertIn("reenter_note_edit", last_bar_reopen.interactions)
        self.assertIn("reopen_note_edit_selected", last_bar_reopen.must_prove)
        self.assertIn("reopen_note_moved", last_bar_reopen.must_prove)
        self.assertEqual(
            stimulus_for_base_preset("note_edit_overdub_compose_last_bar_reopen"),
            STIM_BASE_2_ONE_OD_01,
        )

    def test_stage2_scenarios_bind_canonical_stimuli(self) -> None:
        occupy = stimulus_for_scenario("overdub_occupy_paint")
        edit_od = stimulus_for_scenario("edit_overdub_during_note_edit")
        self.assertEqual(occupy, STIM_OCCUPY_PAINT_01)
        self.assertEqual(edit_od, STIM_EDIT_OD_DURING_NE_01)
        self.assertEqual(stimulus_for_scenario("base"), STIM_BASE_2_2_01)
        self.assertEqual(
            stimulus_for_base_preset("note_edit_overdub_compose"),
            STIM_BASE_2_ONE_OD_01,
        )
        self.assertEqual(stimulus_for_base_preset("base"), STIM_BASE_2_2_01)
        self.assertEqual(
            stimulus_for_base_preset("edit_overdub_during_note_edit"),
            STIM_BASE_2_FIXTURE_01,
        )
        self.assertEqual(stimulus_for_base_preset("edit_minimal"), STIM_BASE_2_FIXTURE_01)
        self.assertEqual(
            STIM_EDIT_OD_DURING_NE_01.base_stimulus_id,
            STIM_BASE_2_FIXTURE_01.stimulus_id,
        )
        self.assertEqual(
            STIM_BASE_2_FIXTURE_01.generator,
            "canonical_edit_minimal_base_legacy_args",
        )
        self.assertEqual(occupy.generator, "canonical_occupy_paint_legacy_args")
        self.assertEqual(STIM_BASE_2_2_01.generator, "canonical_baseline_legacy_args")
        self.assertEqual(
            resolve_scenarios(preset="edit_overdub_during_note_edit", scenario_ids=None),
            ["base", "edit_overdub_during_note_edit"],
        )
        self.assertEqual(
            stimulus_for_scenario("note_edit_reselect_stacked"),
            STIM_NE_RESELECT_STACKED_01,
        )
        self.assertEqual(
            stimulus_for_base_preset("note_edit_reselect_stacked"),
            STIM_BASE_2_FIXTURE_01,
        )
        self.assertEqual(
            resolve_scenarios(preset="note_edit_reselect_stacked", scenario_ids=None),
            ["base", "note_edit_reselect_stacked"],
        )

    def test_the_mixed_leg_has_its_own_registered_profile_and_scope(self) -> None:
        """Task 5.1's mixed leg is registered as a profile of its own, with its own declared scope.

        Its two targets carry *different* verified content preconditions -- slot A must be verified empty and
        idle, slot B carries verified committed content -- which is what the requirement `Scoped same-boot
        fixture preparation` asks for, so it is a declaration rather than a reuse of either leg's profile.
        """
        spec = get_stimulus("STIM-BASE-2-MIXED-01")
        self.assertIs(spec, STIM_BASE_2_MIXED_01)
        self.assertEqual("accepted_sequence_mixed", spec.scenario_id)
        self.assertEqual("canonical", spec.kind)
        self.assertEqual(1, spec.version)
        self.assertEqual("flow:accepted_sequence(mixed)", spec.generator)
        declared = spec.authorization
        self.assertIsNotNone(declared)
        assert declared is not None
        self.assertEqual(("track 8 slots 4,5"), declared.targets_text())
        # The two preconditions are stated, not implied: the predecessor empty, the successor committed.
        self.assertIn("empty and idle", spec.description)
        self.assertIn("committed content verified", spec.description)
        # And it is *not* the record leg's or the overdub leg's profile, even though it shares their bench.
        self.assertNotIn(spec.stimulus_id, (STIM_BASE_2_2_01.stimulus_id, STIM_BASE_2_ONE_OD_01.stimulus_id))

    def test_occupy_header_requires_two_overdubs_and_undo(self) -> None:
        lines = prepend_hitl_header(
            [
                "#CAP,1,ST,Track,STOPPED,RECORDING",
                "#CAP,2,ST,Track,PLAYING,OVERDUBBING",
                "#CAP,3,ST,Track,OVERDUBBING,PLAYING",
                "#CAP,4,ST,Track,PLAYING,OVERDUBBING",
                "Overdub undone",
                "Overdub undone",
                "Overdub redone",
                "Overdub redone",
            ],
            STIM_OCCUPY_PAINT_01,
        )
        result = validate_stimulus(lines, STIM_OCCUPY_PAINT_01)
        self.assertTrue(result["ok"], result["issues"])
        self.assertFalse(result["must_prove"]["skipped"])

    def test_unknown_stimulus_raises(self) -> None:
        with self.assertRaises(KeyError):
            get_stimulus("STIM-DOES-NOT-EXIST")

    def test_temporary_variant_is_not_registered(self) -> None:
        temp = temporary_variant("STIM-NE-OD-COMPOSE-01", "A")
        self.assertEqual(temp.stimulus_id, "STIM-NE-OD-COMPOSE-01-TEMP-A")
        self.assertEqual(temp.kind, "temporary")
        self.assertEqual(temp.parent_id, "STIM-NE-OD-COMPOSE-01")
        with self.assertRaises(KeyError):
            get_stimulus(temp.stimulus_id)


class StimulusFingerprintTests(unittest.TestCase):
    def test_header_round_trip(self) -> None:
        header = format_hitl_header(STIM_NE_OD_COMPOSE_01)
        self.assertIn("#HITL,STIMULUS,STIM-NE-OD-COMPOSE-01", header)
        self.assertIn("#HITL,STIMULUS_VERSION,1", header)
        self.assertIn("#HITL,SCENARIO,note_edit_overdub_compose", header)
        self.assertTrue(any("LIVE_CAPTURE_COMPOSE" in line for line in header))

    def test_missing_header_does_not_fail_firmware_replay(self) -> None:
        check = verify_note_edit_overdub_compose(_pass_lines(), SimpleNamespace())
        self.assertTrue(check["ok"], check["issues"])
        stimulus = check["cases"]["stimulus_validation"]
        self.assertTrue(stimulus["ok"])
        self.assertFalse(stimulus["header"]["header_present"])
        self.assertTrue(stimulus["must_prove"]["skipped"])

    def test_wrong_stimulus_id_fails(self) -> None:
        lines = [
            "#HITL,SCENARIO,note_edit_overdub_compose",
            "#HITL,STIMULUS,STIM-OTHER-01",
            "#HITL,STIMULUS_VERSION,1",
            *_pass_lines(),
        ]
        result = validate_stimulus(lines, STIM_NE_OD_COMPOSE_01)
        self.assertFalse(result["ok"])
        self.assertTrue(
            any("stimulus_id_mismatch" in issue for issue in result["issues"])
        )

    def test_header_plus_recipe_pass(self) -> None:
        lines = prepend_hitl_header(_pass_lines(), STIM_NE_OD_COMPOSE_01)
        check = verify_note_edit_overdub_compose(lines, SimpleNamespace())
        self.assertTrue(check["ok"], check["issues"])
        stimulus = check["cases"]["stimulus_validation"]
        self.assertTrue(stimulus["header"]["header_present"])
        self.assertFalse(stimulus["must_prove"]["skipped"])
        self.assertTrue(stimulus["must_prove"]["ok"], stimulus["must_prove"])

    def test_require_header_fails_old_capture(self) -> None:
        result = validate_stimulus(
            _pass_lines(), STIM_NE_OD_COMPOSE_01, require_header=True
        )
        self.assertFalse(result["ok"])
        self.assertIn("stimulus_header_missing", result["issues"])


class StimulusStage3Tests(unittest.TestCase):
    def setUp(self) -> None:
        set_run_stimulus_override(None)

    def tearDown(self) -> None:
        set_run_stimulus_override(None)

    def test_export_yaml_is_a_view_of_registered_ids(self) -> None:
        text = export_registry_yaml()
        self.assertIn("scripts/hitl/stimuli/registry.py", text)
        for stimulus_id in (
            "STIM-BASE-2-ONE-OD-01",
            "STIM-BASE-2-2-01",
            "STIM-BASE-2-FIXTURE-01",
            "STIM-NE-OD-COMPOSE-01",
            "STIM-NE-OD-COMPOSE-02",
            "STIM-NE-OD-COMPOSE-03",
            "STIM-NE-OD-COMPOSE-04",
            "STIM-OCCUPY-PAINT-01",
            "STIM-EDIT-OD-DURING-NE-01",
            "STIM-NE-RESELECT-STACKED-01",
        ):
            self.assertIn(stimulus_id, text)
        self.assertNotIn("-TEMP-", text)

    def test_temp_sidecar_is_not_registered(self) -> None:
        with TemporaryDirectory() as tmp:
            spec, path = write_temporary_sidecar(
                "STIM-NE-OD-COMPOSE-01",
                "A",
                out_dir=Path(tmp),
                reason="unit test",
            )
            self.assertEqual(spec.stimulus_id, "STIM-NE-OD-COMPOSE-01-TEMP-A")
            self.assertEqual(spec.kind, "temporary")
            self.assertEqual(spec.parent_id, "STIM-NE-OD-COMPOSE-01")
            self.assertTrue(path.is_file())
            text = path.read_text(encoding="utf-8")
            self.assertIn("STIM-NE-OD-COMPOSE-01-TEMP-A", text)
            self.assertIn('kind: "temporary"', text)
            self.assertIn('parent_id: "STIM-NE-OD-COMPOSE-01"', text)
            self.assertIn("reason: unit test", text)
            self.assertIn("LIVE_CAPTURE_COMPOSE", text)
            self.assertNotIn("({", text)
            with self.assertRaises(KeyError):
                get_stimulus(spec.stimulus_id)

    def test_temp_override_matches_parent_only(self) -> None:
        temp = temporary_variant("STIM-NE-OD-COMPOSE-01", "A")
        set_run_stimulus_override(temp)
        self.assertEqual(run_stimulus(STIM_NE_OD_COMPOSE_01), temp)
        self.assertEqual(run_stimulus(STIM_OCCUPY_PAINT_01), STIM_OCCUPY_PAINT_01)
        self.assertEqual(
            default_temp_parent_id(["base", "note_edit_overdub_compose"]),
            "STIM-NE-OD-COMPOSE-02",
        )

    def test_stimulus_cli_export_and_temp(self) -> None:
        with TemporaryDirectory() as tmp:
            catalog = Path(tmp) / "catalog.yaml"
            self.assertEqual(
                hitl_main(["stimulus", "export-yaml", "--out", str(catalog)]),
                0,
            )
            catalog_text = catalog.read_text(encoding="utf-8")
            self.assertIn("STIM-NE-OD-COMPOSE-01", catalog_text)
            self.assertNotIn("-TEMP-", catalog_text)
            self.assertEqual(
                hitl_main(
                    [
                        "stimulus",
                        "temp",
                        "STIM-NE-OD-COMPOSE-01",
                        "A",
                        "--out-dir",
                        tmp,
                        "--reason",
                        "cli test",
                    ]
                ),
                0,
            )
            sidecar = Path(tmp) / "stimuli" / "STIM-NE-OD-COMPOSE-01-TEMP-A.yaml"
            self.assertTrue(sidecar.is_file())
            with self.assertRaises(KeyError):
                get_stimulus("STIM-NE-OD-COMPOSE-01-TEMP-A")


class StimulusStage4Tests(unittest.TestCase):
    def test_fixture_primitive_is_record_only(self) -> None:
        self.assertEqual(STIM_BASE_2_FIXTURE_01.overdubs, ())
        self.assertEqual(STIM_BASE_2_FIXTURE_01.base_pattern, "EDIT_RECORD_FIXTURE")
        lines = prepend_hitl_header(
            ["#CAP,1,ST,Track,STOPPED,RECORDING"],
            STIM_BASE_2_FIXTURE_01,
        )
        result = validate_stimulus(lines, STIM_BASE_2_FIXTURE_01)
        self.assertTrue(result["ok"], result["issues"])

    def test_stimulus_list_includes_generator(self) -> None:
        from contextlib import redirect_stdout
        from io import StringIO

        buf = StringIO()
        with redirect_stdout(buf):
            self.assertEqual(hitl_main(["stimulus", "list"]), 0)
            self.assertEqual(hitl_main(["run", "--list-stimuli"]), 0)
        text = buf.getvalue()
        self.assertIn("STIM-BASE-2-FIXTURE-01@v1", text)
        self.assertIn("canonical_edit_minimal_base_legacy_args", text)
        self.assertEqual(text.count("STIM-BASE-2-FIXTURE-01@v1"), 2)


class ReselectStackedHelperTests(unittest.TestCase):
    def test_pitchbend_map_matches_153550_native_fixture(self) -> None:
        from hitl.verify.note_edit_reselect_stacked import (
            select_stacked_note_index_from_pitchbend,
            select_tick_from_pitchbend,
        )

        nav = 3840
        self.assertEqual(select_tick_from_pitchbend(1763, nav), 2304)
        self.assertEqual(select_stacked_note_index_from_pitchbend(1763, nav, 3), 0)
        self.assertEqual(select_tick_from_pitchbend(1910, nav), 2304)
        self.assertEqual(select_stacked_note_index_from_pitchbend(1910, nav, 3), 2)

    def test_verify_requires_two_dest_notes_after_deselect(self) -> None:
        from hitl.verify.note_edit_reselect_stacked import verify_note_edit_reselect_stacked

        lines = [
            "Edit session: NOTE_EDIT",
            "EditSessionAction: type=3 noteId=81",
            "#DBG select_apply selected_tick=48 note_idx=-1 slot=1 prior_slot=0 apply=1 reason=empty",
            "#DBG select_apply selected_tick=192 note_idx=3 slot=2 prior_slot=1 apply=1 reason=nav",
            "#DBG select_apply selected_tick=192 note_idx=5 slot=3 prior_slot=2 apply=1 reason=nav",
        ]
        result = verify_note_edit_reselect_stacked(lines, SimpleNamespace())
        self.assertTrue(result["ok"], result["issues"])
        self.assertEqual(result["dest_distinct_note_idxs"], [3, 5])

    def test_verify_accepts_off_grid_note_ticks_in_dest_sixteenth(self) -> None:
        from hitl.verify.note_edit_reselect_stacked import (
            select_apply_lands_on_step,
            verify_note_edit_reselect_stacked,
        )

        self.assertTrue(select_apply_lands_on_step(428, 3, 0, 384, want_note=True))
        self.assertFalse(select_apply_lands_on_step(384, -1, 1, 384, want_note=True))
        self.assertFalse(select_apply_lands_on_step(432, 3, 1, 384, want_note=True))
        lines = [
            "Edit session: NOTE_EDIT",
            "EditSessionAction: type=3 noteId=81",
            "#DBG select_apply selected_tick=48 note_idx=-1 slot=1 prior_slot=0 apply=1 reason=empty",
            "#DBG select_apply selected_tick=196 note_idx=3 slot=2 prior_slot=1 apply=1 reason=nav",
            "#DBG select_apply selected_tick=220 note_idx=5 slot=3 prior_slot=2 apply=1 reason=nav",
        ]
        result = verify_note_edit_reselect_stacked(lines, SimpleNamespace())
        self.assertTrue(result["ok"], result["issues"])
        self.assertEqual(result["dest_distinct_note_idxs"], [3, 5])

    def test_playing_motor_value_only_rejects_timed_burst(self) -> None:
        from hitl.verify.note_edit_reselect_stacked import verify_note_edit_reselect_stacked

        lines = [
            "Edit session: NOTE_EDIT",
            "EditSessionAction: type=3 noteId=81",
            "#CAP,100,ST,Track,STOPPED_RECORDING,PLAYING",
            "#DBG select_motor_sync mode=SELECT_SYNC sequence=parallel_timed_burst",
            "#CAP,200,DIAG,midi_gap,265402,1",
            "#DBG select_apply selected_tick=48 note_idx=-1 slot=1 prior_slot=0 apply=1 reason=empty",
            "#DBG select_apply selected_tick=192 note_idx=3 slot=2 prior_slot=1 apply=1 reason=nav",
            "#DBG select_apply selected_tick=192 note_idx=5 slot=3 prior_slot=2 apply=1 reason=nav",
        ]
        result = verify_note_edit_reselect_stacked(lines, SimpleNamespace())
        self.assertFalse(result["ok"])
        self.assertTrue(any("playing_motor_timed_burst" in issue for issue in result["issues"]))
        self.assertTrue(any("playing_motor_midi_gap" in issue for issue in result["issues"]))

    def test_playing_motor_value_only_accepts_value_only(self) -> None:
        from hitl.verify.note_edit_reselect_stacked import verify_note_edit_reselect_stacked

        lines = [
            "Edit session: NOTE_EDIT",
            "EditSessionAction: type=3 noteId=81",
            "#CAP,100,ST,Track,STOPPED_RECORDING,PLAYING",
            "#DBG select_motor_sync mode=SELECT_SYNC sequence=value_only",
            "#DBG geometry_motor_sync sent=1 bracket_tick=192 pb=0 mode=GEOMETRY_SYNC sequence=value_only",
            "#CAP,200,DIAG,midi_gap,18420,1",
            "#DBG select_apply selected_tick=48 note_idx=-1 slot=1 prior_slot=0 apply=1 reason=empty",
            "#DBG select_apply selected_tick=192 note_idx=3 slot=2 prior_slot=1 apply=1 reason=nav",
            "#DBG select_apply selected_tick=192 note_idx=5 slot=3 prior_slot=2 apply=1 reason=nav",
        ]
        result = verify_note_edit_reselect_stacked(lines, SimpleNamespace())
        self.assertTrue(result["ok"], result["issues"])
        self.assertEqual(result["playing_motor_value_only"]["playing_motor_sync_judged"], 2)
        self.assertEqual(result["value_only_notegate_held"]["value_only_notegate_held_judged"], 0)

    def test_playing_value_only_notegate_one_ms_pair_fails(self) -> None:
        from hitl.verify.note_edit_reselect_stacked import verify_note_edit_reselect_stacked

        lines = [
            "Edit session: NOTE_EDIT",
            "EditSessionAction: type=3 noteId=81",
            "#CAP,100,ST,Track,STOPPED_RECORDING,PLAYING",
            "#DBG select_motor_sync mode=SELECT_SYNC sequence=value_only",
            "#CAP,229701914,MO,144,16,0,127",
            "#CAP,229702914,MO,128,16,0,0",
            "#DBG select_apply selected_tick=48 note_idx=-1 slot=1 prior_slot=0 apply=1 reason=empty",
            "#DBG select_apply selected_tick=192 note_idx=3 slot=2 prior_slot=1 apply=1 reason=nav",
            "#DBG select_apply selected_tick=192 note_idx=5 slot=3 prior_slot=2 apply=1 reason=nav",
        ]
        result = verify_note_edit_reselect_stacked(lines, SimpleNamespace())
        self.assertFalse(result["ok"])
        self.assertTrue(any("value_only_notegate_short=" in issue for issue in result["issues"]))

    def test_f1_highlight_starved_fails_when_applies_outpace_dnte(self) -> None:
        from hitl.verify.note_edit_reselect_stacked import verify_note_edit_reselect_stacked

        applies = [
            f"#DBG select_apply selected_tick={i * 48} note_idx={i} slot=1 prior_slot=-1 apply=1 reason=note_changed"
            for i in range(12)
        ]
        lines = [
            "Edit session: NOTE_EDIT",
            "EditSessionAction: type=3 noteId=81",
            "#DBG select_apply selected_tick=48 note_idx=-1 slot=1 prior_slot=0 apply=1 reason=empty",
            "#DBG select_apply selected_tick=192 note_idx=3 slot=2 prior_slot=1 apply=1 reason=nav",
            "#DBG select_apply selected_tick=192 note_idx=5 slot=3 prior_slot=2 apply=1 reason=nav",
            *applies,
        ]
        result = verify_note_edit_reselect_stacked(lines, SimpleNamespace())
        self.assertFalse(result["ok"])
        self.assertTrue(any("select_apply_highlight_starved=" in issue for issue in result["issues"]))

    def test_vcache_drop_after_deselect_fails(self) -> None:
        from hitl.verify.note_edit_reselect_stacked import verify_note_edit_reselect_stacked

        lines = [
            "Edit session: NOTE_EDIT",
            "EditSessionAction: type=3 noteId=81",
            "#CAP,1,VCACHE,full,ev,16,notes,8,first,0,last,1,total,2,dsz,0,dcnt,0,dirty,0",
            "#DBG select_apply selected_tick=48 note_idx=-1 slot=1 prior_slot=0 apply=1 reason=empty",
            "#CAP,2,VCACHE,full,ev,16,notes,7,first,0,last,1,total,2,dsz,0,dcnt,0,dirty,0",
            "#DBG select_apply selected_tick=192 note_idx=3 slot=2 prior_slot=1 apply=1 reason=nav",
            "#DBG select_apply selected_tick=192 note_idx=5 slot=3 prior_slot=2 apply=1 reason=nav",
        ]
        result = verify_note_edit_reselect_stacked(lines, SimpleNamespace())
        self.assertFalse(result["ok"])
        self.assertTrue(any("moved_note_dropped_from_vcache=" in issue for issue in result["issues"]))


if __name__ == "__main__":
    unittest.main()
