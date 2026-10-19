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
    STIM_WINDOW_CONTENT_01,
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
from hitl.registry import list_scenarios
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

    def test_the_windowed_leg_has_a_registered_profile_and_an_exercisable_driver(self) -> None:
        """Task 6.4: the windowed profile is *registered and authorized*; its driver is a separate step.

        The task separates registration, authorization, exercise and qualification on purpose. This test pins the
        registration half: the profile's identity, its recipe shape (a loop far longer than the served window, with a
        running transport), its **declared oracle** and its **declared target scope** -- plus the fact that the driver
        that exercises it is the reused long-loop leg rather than a second pipeline.
        """
        spec = stimulus_for_scenario("windowed_content_parity")
        self.assertIsNotNone(spec)
        assert spec is not None
        self.assertIs(spec, STIM_WINDOW_CONTENT_01)
        self.assertEqual(get_stimulus("STIM-WINDOW-CONTENT-01"), STIM_WINDOW_CONTENT_01)
        self.assertEqual(1, spec.version)
        self.assertEqual("canonical", spec.kind)
        self.assertEqual("scenario:windowed_content_parity", spec.generator)
        # The recipe shape is the point, not a bar count for its own sake: the loop is far longer than the two-bar
        # span the lane serves, so a window is a strict sub-interval, and it carries a second revision.
        self.assertEqual(16, spec.base_bars)
        self.assertEqual(1, len(spec.overdubs))
        self.assertEqual(16, spec.overdubs[0].bars)
        self.assertIn("transport_running_across_window_boundary", spec.interactions)
        # No provenance link is claimed: no registered profile shares this recipe shape.
        self.assertIsNone(spec.base_stimulus_id)
        # The declared oracle names the F-7 / F-8 / AC-8 outcomes themselves, not a naming.
        self.assertEqual(
            (
                "content_parity_across_window_boundary",
                "root_demoted_residency_over_loop_lengths",
                "readiness_receipts_under_transport",
                "windowed_service_coverage_under_transport",
            ),
            spec.must_prove,
        )
        # Its target scope is declared with the recipe, so a run pointed outside it is refused before it mutates
        # anything -- the profile is authorized, not merely named.
        declared = spec.authorization
        self.assertIsNotNone(declared)
        assert declared is not None
        self.assertEqual("track 8 slots 4,5", declared.targets_text())
        self.assertIn("2026-09-30", declared.source)
        # **And it is exercisable**: the previous revision of this pin asserted the opposite on purpose, as the
        # reminder that the driver was owed. The driver landed (task 6.4's exercise half) and this is that reminder
        # doing its job -- the clause is inverted rather than deleted, so a *removed* driver would fail here too.
        self.assertIn("windowed_content_parity", list_scenarios())
        self.assertEqual(
            ["windowed_content_parity"],
            resolve_scenarios(preset="windowed_content_parity", scenario_ids=None),
        )

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

        from test_hitl_reselect_validation import good_lines
        lines = good_lines()
        result = verify_note_edit_reselect_stacked(lines, SimpleNamespace())
        self.assertTrue(result["ok"], result["issues"])
        self.assertEqual(result["dest_distinct_note_idxs"], [3, 5])

    def test_verify_accepts_off_grid_note_ticks_in_dest_sixteenth(self) -> None:
        from hitl.verify.note_edit_reselect_stacked import (
            select_apply_lands_on_step,
            verify_note_edit_reselect_stacked,
        )

        self.assertFalse(select_apply_lands_on_step(428, 3, 0, 384, want_note=True))
        self.assertFalse(select_apply_lands_on_step(384, -1, 1, 384, want_note=True))
        self.assertFalse(select_apply_lands_on_step(432, 3, 1, 384, want_note=True))
        from test_hitl_reselect_validation import good_lines
        lines = good_lines()
        lines[5] = lines[5].replace('tick 192', 'tick 196')
        lines[8] = lines[8].replace('tick 192', 'tick 220')
        lines[10] = lines[10].replace('64,192,192', '64,220,220')
        result = verify_note_edit_reselect_stacked(lines, SimpleNamespace(), dest_note_tick=220)
        self.assertTrue(result["ok"], result["issues"])
        self.assertEqual(result["dest_distinct_note_idxs"], [3, 5])

    def test_direct_select_rows_are_the_live_witness_of_the_same_applies(self) -> None:
        """The device's direct `Select fader: …` row is the select proof that is on the wire.

        `#DBG select_apply …` is deferred into the capture ring (`Logger.cpp`), and the
        timing-critical (PLAYING) flush transmits nothing, so a live wait keyed only to it expires
        (`captures/i3b-seeded-edit-t8s4-04`: the deselect was applied, yet attempts 1/3-3/3 saw no
        row). Both rows below are verbatim from that capture: the direct one at line 6766 (present
        at the press) and the deferred one at line 10835 (a drained record).
        """
        from hitl.verify.note_edit_reselect_stacked import (
            direct_select_lands_on_step,
            parse_direct_select,
        )

        empty_row = "[75.579] [DEBUG] [MIDI] Select fader: selected empty step at tick 0 (no note)"
        note_row = (
            "[75.500] [DEBUG] [MIDI] Select fader: selected note 3 at tick 528 "
            "(1/2 notes at this position)"
        )
        self.assertEqual(parse_direct_select(empty_row), (-1, 0))
        self.assertEqual(parse_direct_select(note_row), (3, 528))
        self.assertIsNone(
            parse_direct_select(
                "#DBG select_apply selected_tick=0 note_idx=-1 slot=0 prior_slot=-1 apply=1 "
                "reason=empty_step"
            )
        )
        self.assertTrue(direct_select_lands_on_step(-1, 0, 0, want_note=False))
        self.assertFalse(direct_select_lands_on_step(-1, 0, 0, want_note=True))
        self.assertTrue(direct_select_lands_on_step(3, 528, 528, want_note=True))
        self.assertFalse(direct_select_lands_on_step(3, 528, 48, want_note=True))
        self.assertFalse(direct_select_lands_on_step(-1, 48, 0, want_note=False))

    # The -07 leg's own snapshot rows, verbatim (`i3b-seeded-edit-t8s4-07`, lines 2199-2318):
    # the enter, the move, the applied deselect and the two applied stacked reselects. That
    # snapshot carries the direct channel only -- the `#DBG` family was still in the capture ring
    # when it was taken -- so a verifier that reads `#DBG` alone called the same leg FAIL
    # (`deselect_empty_step`, `stacked_reselect_after_deselect`) that the drained session log
    # passed.
    _SEVEN_ENTER = "[60.867] [INFO] [MIDI] Edit session: NOTE_EDIT (Program 1, Note 0 trigger)"
    _SEVEN_MOVE = (
        "[73.261] [DEBUG] [MIDI] Note movement with overlap handling: from=768 to=528 "
        "(EditSessionAction: type=3 noteId=3 start=528 end=616 pitch=64)"
    )
    _SEVEN_DESELECT = (
        "[78.998] [DEBUG] [MIDI] Select fader: selected empty step at tick 0 (no note)"
    )
    _SEVEN_RESELECT_0 = "[80.758] [DEBUG] [MIDI] Select fader: selected note 1 at tick 528"
    _SEVEN_RESELECT_1 = "[82.481] [DEBUG] [MIDI] Select fader: selected note 2 at tick 575"

    def _seven_lines(self, *rows: str) -> list[str]:
        lines = [self._SEVEN_ENTER, self._SEVEN_MOVE, *rows]
        return lines

    def test_direct_rows_without_focus_geometry_are_unqualified(self) -> None:
        from hitl.verify.note_edit_reselect_stacked import verify_note_edit_reselect_stacked

        lines = self._seven_lines(
            self._SEVEN_DESELECT, self._SEVEN_RESELECT_0, self._SEVEN_RESELECT_1
        )
        result = verify_note_edit_reselect_stacked(
            lines, SimpleNamespace(), dest_tick=528, empty_tick=0
        )
        self.assertFalse(result["ok"])
        self.assertIn("stacked_note_identity_unproven", result["issues"])

    def test_verify_still_requires_the_deselect_on_the_device_rows(self) -> None:
        from hitl.verify.note_edit_reselect_stacked import verify_note_edit_reselect_stacked

        lines = self._seven_lines(self._SEVEN_RESELECT_0, self._SEVEN_RESELECT_1)
        result = verify_note_edit_reselect_stacked(
            lines, SimpleNamespace(), dest_tick=528, empty_tick=0
        )
        self.assertFalse(result["ok"])
        self.assertIn("deselect_empty_step", result["issues"])

    def test_verify_rejects_an_unchanged_empty_step_answer(self) -> None:
        """`apply=0 reason=empty_step` is a repeat, not an applied deselect."""
        from hitl.verify.note_edit_reselect_stacked import verify_note_edit_reselect_stacked

        lines = self._seven_lines(
            "#DBG select_apply selected_tick=0 note_idx=-1 slot=0 prior_slot=-1 apply=0 "
            "reason=empty_step",
            self._SEVEN_RESELECT_0,
            self._SEVEN_RESELECT_1,
        )
        result = verify_note_edit_reselect_stacked(
            lines, SimpleNamespace(), dest_tick=528, empty_tick=0
        )
        self.assertFalse(result["ok"])
        self.assertIn("deselect_empty_step", result["issues"])

    def test_verify_rejects_a_direct_deselect_from_another_sixteenth(self) -> None:
        from hitl.verify.note_edit_reselect_stacked import verify_note_edit_reselect_stacked

        lines = self._seven_lines(
            "[78.998] [DEBUG] [MIDI] Select fader: selected empty step at tick 48 (no note)",
            self._SEVEN_RESELECT_0,
            self._SEVEN_RESELECT_1,
        )
        result = verify_note_edit_reselect_stacked(
            lines, SimpleNamespace(), dest_tick=528, empty_tick=0
        )
        self.assertFalse(result["ok"])
        self.assertIn("deselect_empty_step", result["issues"])

    def test_verify_still_requires_two_distinct_direct_reselects(self) -> None:
        from hitl.verify.note_edit_reselect_stacked import verify_note_edit_reselect_stacked

        lines = self._seven_lines(self._SEVEN_DESELECT, self._SEVEN_RESELECT_0)
        result = verify_note_edit_reselect_stacked(
            lines, SimpleNamespace(), dest_tick=528, empty_tick=0
        )
        self.assertFalse(result["ok"])
        self.assertIn("stacked_reselect_after_deselect", result["issues"])

    def test_verify_rejects_a_direct_reselect_on_another_sixteenth(self) -> None:
        from hitl.verify.note_edit_reselect_stacked import verify_note_edit_reselect_stacked

        lines = self._seven_lines(
            self._SEVEN_DESELECT,
            self._SEVEN_RESELECT_0,
            "[82.481] [DEBUG] [MIDI] Select fader: selected note 2 at tick 768",
        )
        result = verify_note_edit_reselect_stacked(
            lines, SimpleNamespace(), dest_tick=528, empty_tick=0
        )
        self.assertFalse(result["ok"])
        self.assertIn("stacked_reselect_after_deselect", result["issues"])

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

        from test_hitl_reselect_validation import good_lines
        lines = good_lines() + [
            "#CAP,5000000,ST,Track,STOPPED_RECORDING,PLAYING",
            "#DBG select_motor_sync mode=SELECT_SYNC sequence=value_only",
            "#DBG geometry_motor_sync sent=1 bracket_tick=192 pb=0 mode=GEOMETRY_SYNC sequence=value_only",
            "#CAP,5000010,DIAG,midi_gap,18420,1",
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

        from test_hitl_reselect_validation import good_lines
        lines = good_lines()
        lines.insert(4, "#CAP,1500000,VCACHE,full,ev,16,notes,8,first,0,last,1,total,2,dsz,0,dcnt,0,dirty,0")
        lines.insert(6, "#CAP,2500000,VCACHE,full,ev,16,notes,7,first,0,last,1,total,2,dsz,0,dcnt,0,dirty,0")
        result = verify_note_edit_reselect_stacked(lines, SimpleNamespace())
        self.assertFalse(result["ok"])
        self.assertTrue(any("moved_note_dropped_from_vcache=" in issue for issue in result["issues"]))


if __name__ == "__main__":
    unittest.main()
