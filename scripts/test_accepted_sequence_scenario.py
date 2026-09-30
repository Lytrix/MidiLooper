#!/usr/bin/env python3
"""Host tests for the registered response-driven acceptance scenarios (task 4.4).

Task 4.4: *register both scenarios and route the standalone CLI through the same runner; verify legacy
argument translation, explicit unsupported combinations, scenario listing, identical verdicts via both entry
points and unchanged base/edit_full behavior.* Five things are pinned here, all without a device:

1. **Registration and listing** — both legs are in the classic registry, carry the oracle as their verifier,
   declare that they own their capture, and appear in `--list-scenarios`.
2. **Translation** — the runner's target flags, the allowlist and the profile's authorization become one
   argv a caller can read (`accepted_sequence_argv`), and the runner's own appended bookkeeping is consumed
   rather than forwarded.
3. **Explicit refusals** — a mismatched `--leg`, an ambiguous or missing target, one target used twice, a
   note channel that is not the track's own, an unknown flag and a stray positional are each named, and
   `run_accepted_sequence` returns a refusal code instead of raising or reaching the device.
4. **Identical verdicts through both entry points** — the wired verifier and the standalone oracle reach the
   same verdict on the retained fixtures, both directly and through `--verify-only`; both carry the same
   verdict on a non-pass fixture too.
5. **Unchanged base/edit_full behavior** — resolution, preset keys and the runner's capture decision for the
   existing scenarios are unchanged, and `--layered` with a classic-scenario id is a named refusal instead
   of a `KeyError`.

Run: .venv/bin/python scripts/test_accepted_sequence_scenario.py
"""

import ast
import contextlib
import io
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))

import hitl_accepted_sequence  # noqa: E402
from hitl.accepted_sequence_probe import analyse, main as probe_main  # noqa: E402
from hitl.registry import PRESET_SCENARIOS, get_registry, resolve_scenarios  # noqa: E402
from hitl.runner import _should_use_managed_capture, main as runner_main, scenarios_own_capture  # noqa: E402
from hitl.scenarios import accepted_sequence as scenario  # noqa: E402

FIXTURES = Path(__file__).parent / "hitl" / "fixtures" / "accepted_sequence"
OD_EXCERPT = FIXTURES / "s4f22-dec096-od-13-excerpt.log"
OD_EXCERPT_PROVENANCE = FIXTURES / "s4f22-dec096-od-13-excerpt.json"
OD_NON_PASS = FIXTURES / "s4f13-od-20260927_015145.log"
#: The best-case base report the fixture's own suite uses to make the capture-integrity predicates decidable.
BASE_REPORT = {"capture_integrity": "COMPLETE", "capture_complete": True,
               "setup_transitions_confirmed": True, "issues": []}
REC = "accepted_sequence_rec"
OD = "accepted_sequence_od"


def runner_args(**overrides) -> SimpleNamespace:
    base = dict(track_number=8, loop_slot=4, midi_channel=8, out_dir=Path("/tmp/hitl-out"),
                legacy_args=[], boot_reset=False, build_upload=False, active_scenario_id=REC)
    base.update(overrides)
    return SimpleNamespace(**base)


def fixture_evidence(tmp: Path) -> tuple[Path, Path]:
    provenance = json.loads(OD_EXCERPT_PROVENANCE.read_text())
    metadata = tmp / "metadata.json"
    metadata.write_text(json.dumps({"git_commit": provenance["git_commit"],
                                    "firmware_hex_sha256": provenance["firmware_hex_sha256"]}))
    base_report = tmp / "base-report.json"
    base_report.write_text(json.dumps(BASE_REPORT))
    return metadata, base_report


def verify_report_paths(out_dir: Path) -> Path:
    found = sorted(p for p in out_dir.iterdir() if p.name.startswith("host_midi_hitl_verify"))
    assert len(found) == 1, f"expected one verify report in {out_dir}, found {found}"
    return found[0]


class RegistrationTests(unittest.TestCase):
    """Both legs are registered, listed, and declared as capture-owning."""

    def test_both_legs_are_registered_with_the_oracle_as_their_verifier(self):
        registry = get_registry()
        for scenario_id, leg in ((REC, "rec"), (OD, "od")):
            with self.subTest(scenario_id):
                spec = registry[scenario_id]
                self.assertIs(spec.run, scenario.run_accepted_sequence)
                self.assertIs(spec.verify, scenario.verify_accepted_sequence)
                self.assertTrue(spec.owns_capture, "the sequence owns its own capture")
                self.assertEqual(leg, scenario.LEG_FOR_SCENARIO[scenario_id])
                self.assertIn(spec.description, spec.description)
                self.assertTrue(spec.description.strip())

    def test_the_preset_drives_both_legs_in_order(self):
        self.assertEqual((REC, OD), tuple(resolve_scenarios(preset="accepted_sequence", scenario_ids=None)))

    def test_the_listing_shows_both_legs(self):
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            code = runner_main(["run", "--list-scenarios"])
        self.assertEqual(0, code)
        listed = buffer.getvalue().split()
        self.assertIn(REC, listed)
        self.assertIn(OD, listed)

    def test_capture_owning_scenarios_step_the_runner_out_of_managed_capture(self):
        self.assertTrue(scenarios_own_capture([REC]))
        self.assertTrue(scenarios_own_capture(["base", OD]))
        self.assertFalse(scenarios_own_capture(["base"]))
        self.assertTrue(_should_use_managed_capture(runner_args(), [], ["base"]),
                        "a scenario that does not own its capture keeps the runner's managed capture")
        self.assertFalse(_should_use_managed_capture(runner_args(), [], [REC]))
        self.assertTrue(_should_use_managed_capture(runner_args(), [], []))
        self.assertFalse(_should_use_managed_capture(runner_args(no_managed_capture=True), [], ["base"]))

    def test_a_layered_run_with_a_classic_scenario_id_is_refused_by_name(self):
        """`--layered` knows only the layered registry; the classic ids must say so, not die mid-run."""
        from hitl.foundation_runner import _run_layered_preset_inner

        config = SimpleNamespace(preset=None, scenario_ids=[REC], track_number=8, loop_slot=4,
                                 midi_channel=8, out_dir=Path("/tmp/hitl-out"))
        buffer = io.StringIO()
        # The bridge is built before the scenario loop; this test is about the refusal, so the bridge is a
        # stand-in (it is the legacy adapter, not the registry).
        with patch("hitl.foundation_runner.LayeredLegacyBridge") as bridge:
            bridge.from_config.return_value = SimpleNamespace(record_overdub_args=lambda ctx: None)
            with contextlib.redirect_stdout(buffer):
                code = _run_layered_preset_inner(config)
        self.assertEqual(2, code)
        self.assertIn("unsupported combination", buffer.getvalue())
        self.assertIn(REC, buffer.getvalue())


class TranslationTests(unittest.TestCase):
    """One readable argv per run, and the runner's own bookkeeping is not forwarded."""

    def test_the_runner_targets_and_the_profile_authorization_become_the_wrapper_argv(self):
        argv = scenario.accepted_sequence_argv(
            runner_args(build_upload=True, legacy_args=["--slot-b", "5"]), scenario_id=REC)
        self.assertEqual([
            "--leg", "rec", "--track", "8", "--slot-a", "4", "--slot-b", "5",
            "--authorization", "STIM-BASE-2-2-01",
            "--out-dir", "/tmp/hitl-out/accepted-sequence-rec", "--no-upload",
        ], argv)

    def test_each_leg_defaults_to_its_own_registered_profile(self):
        self.assertEqual("STIM-BASE-2-2-01",
                         scenario.PROFILE_FOR_SCENARIO[REC])
        argv = scenario.accepted_sequence_argv(
            runner_args(active_scenario_id=OD, legacy_args=["--slot-b", "5"]), scenario_id=OD)
        self.assertEqual("od", argv[argv.index("--leg") + 1])
        self.assertEqual("STIM-BASE-2-ONE-OD-01", argv[argv.index("--authorization") + 1])

    def test_an_explicit_authorization_replaces_the_profile_default(self):
        argv = scenario.accepted_sequence_argv(
            runner_args(legacy_args=["--slot-b", "5", "--authorization", "cli"]), scenario_id=REC)
        self.assertEqual("cli", argv[argv.index("--authorization") + 1])

    def test_the_allowlisted_leg_flags_are_forwarded_verbatim(self):
        argv = scenario.accepted_sequence_argv(
            runner_args(legacy_args=["--slot-b", "5", "--bars", "8", "--notes", "4",
                                     "--settle-s", "2.5", "--no-clear-targets", "--boot-reset"]),
            scenario_id=REC)
        self.assertEqual("8", argv[argv.index("--bars") + 1])
        self.assertEqual("4", argv[argv.index("--notes") + 1])
        self.assertEqual("2.5", argv[argv.index("--settle-s") + 1])
        self.assertIn("--no-clear-targets", argv)
        self.assertIn("--boot-reset", argv)

    def test_the_runners_appended_bookkeeping_is_consumed_not_forwarded(self):
        legacy = ["--track-number", "8", "--track", "8", "--loop-slot", "4", "--midi-channel", "8",
                  "--slot-b", "5", "--verify-serial-log", "/tmp/session.log", "--metadata", "/tmp/m.json"]
        argv = scenario.accepted_sequence_argv(runner_args(legacy_args=legacy), scenario_id=REC)
        self.assertEqual(1, argv.count("--track"))
        self.assertEqual(1, argv.count("--slot-a"))
        self.assertNotIn("--track-number", argv)
        self.assertNotIn("--loop-slot", argv)
        self.assertNotIn("--midi-channel", argv)
        self.assertNotIn("--verify-serial-log", argv)
        self.assertNotIn("--metadata", argv)

    def test_the_upload_decision_follows_the_runner_and_an_explicit_statement_wins(self):
        cases = [
            (dict(legacy_args=["--slot-b", "5"]), "--upload"),
            (dict(legacy_args=["--slot-b", "5"], build_upload=True), "--no-upload"),
            (dict(legacy_args=["--slot-b", "5", "--no-upload"]), "--no-upload"),
            (dict(legacy_args=["--slot-b", "5", "--upload"], build_upload=True), "--upload"),
        ]
        for overrides, expected in cases:
            with self.subTest(overrides=overrides):
                argv = scenario.accepted_sequence_argv(runner_args(**overrides), scenario_id=REC)
                uploads = [token for token in argv if token in ("--upload", "--no-upload")]
                self.assertEqual([expected], uploads)


class RefusalTests(unittest.TestCase):
    """Every combination the leg cannot honour is named before the device is touched."""

    def assert_refused(self, needle: str, *, scenario_id: str = REC, **overrides) -> None:
        with self.assertRaises(scenario.UnsupportedCombination) as caught:
            scenario.accepted_sequence_argv(runner_args(active_scenario_id=scenario_id, **overrides),
                                            scenario_id=scenario_id)
        self.assertIn(needle, str(caught.exception))

    def test_a_mismatched_leg_is_refused(self):
        self.assert_refused("drives the 'rec' leg", legacy_args=["--slot-b", "5", "--leg", "od"])

    def test_a_missing_track_or_first_target_is_refused(self):
        self.assert_refused("--track <1-8>", track_number=None, loop_slot=None, legacy_args=["--slot-b", "5"])
        self.assert_refused("--loop-slot <1-8>", loop_slot=None, legacy_args=["--slot-b", "5"])

    def test_a_missing_second_target_is_refused(self):
        self.assert_refused("--slot-b <1-8>", legacy_args=[])

    def test_one_target_used_twice_is_refused(self):
        self.assert_refused("must differ", legacy_args=["--slot-b", "4"])

    def test_two_different_names_for_the_first_target_are_refused(self):
        self.assert_refused("ambiguous first target", legacy_args=["--slot-b", "5", "--slot-a", "6"])

    def test_a_note_channel_that_is_not_the_tracks_own_is_refused(self):
        self.assert_refused("not the track's own channel", midi_channel=3, legacy_args=["--slot-b", "5"])

    def test_a_channel_stated_twice_differently_is_refused(self):
        self.assert_refused("ambiguous note channel",
                            legacy_args=["--slot-b", "5", "--midi-channel", "3"])

    def test_an_unknown_flag_is_refused_by_name_not_dropped(self):
        self.assert_refused("unsupported flag '--definitely-not-a-leg-flag'",
                            legacy_args=["--slot-b", "5", "--definitely-not-a-leg-flag", "1"])

    def test_a_stray_positional_is_refused(self):
        self.assert_refused("unexpected positional argument",
                            legacy_args=["--slot-b", "5", "stray"])

    def test_an_unknown_scenario_id_is_refused(self):
        self.assert_refused("is not a response-driven acceptance scenario",
                            scenario_id="not-a-scenario", legacy_args=["--slot-b", "5"])

    def test_the_run_handler_reports_the_refusal_instead_of_raising_or_pressing(self):
        args = runner_args(legacy_args=["--slot-b", "5", "--leg", "od"])
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            code = scenario.run_accepted_sequence(args)
        self.assertEqual(2, code)
        self.assertIn("unsupported combination", buffer.getvalue())
        self.assertNotIn("wrapper", buffer.getvalue(), "no run may be started on a refused combination")

    def test_the_verifier_names_an_unsupported_combination_as_a_non_pass(self):
        result = scenario.verify_accepted_sequence(
            OD_EXCERPT.read_text().splitlines(), runner_args(legacy_args=[]))
        self.assertFalse(result["ok"])
        self.assertEqual("UNSUPPORTED_COMBINATION", result["verdict"])
        self.assertIn("--slot-b", result["issues"][0])

    def test_the_run_handler_passes_the_translated_argv_to_the_wrapper(self):
        args = runner_args(legacy_args=["--slot-b", "5"])
        with patch.object(hitl_accepted_sequence, "main", return_value=3) as wrapper:
            with contextlib.redirect_stdout(io.StringIO()):
                code = scenario.run_accepted_sequence(args)
        self.assertEqual(3, code, "the wrapper's exit code is the scenario's exit code")
        self.assertEqual(list(args.accepted_sequence_argv), wrapper.call_args.args[0])


class VerdictIdentityTests(unittest.TestCase):
    """Both entry points reach the same verdict on the same log, evidence included."""

    def test_the_wired_verifier_agrees_with_the_oracle_on_the_retained_fixtures(self):
        for fixture, expected in ((OD_EXCERPT, "ACCEPTED_SEQUENCE_PASS"), (OD_NON_PASS, "INSUFFICIENT_EVIDENCE")):
            with self.subTest(fixture=fixture.name):
                text = fixture.read_text()
                with tempfile.TemporaryDirectory() as tmp:
                    metadata, base_report = fixture_evidence(Path(tmp))
                    legacy = ["--slot-b", "5", "--metadata", str(metadata), "--base-report", str(base_report)]
                    wired = scenario.verify_accepted_sequence(
                        text.splitlines(), runner_args(active_scenario_id=OD, legacy_args=legacy))
                    direct = analyse(text, leg="od", slot_a=4, slot_b=5,
                                     metadata=json.loads(metadata.read_text()),
                                     base_report=json.loads(base_report.read_text()))
                self.assertEqual(direct.verdict, wired["verdict"])
                self.assertEqual(direct.verdict, expected)
                self.assertEqual(direct.verdict == "ACCEPTED_SEQUENCE_PASS", wired["ok"])
                self.assertEqual(len(direct.checks), wired["checks_total"])

    def test_a_pass_and_a_non_pass_agree_through_the_runner_cli(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            metadata, base_report = fixture_evidence(tmp_path)
            cases = [
                (OD_EXCERPT, True),
                (OD_NON_PASS, False),
            ]
            for fixture, should_pass in cases:
                with self.subTest(fixture=fixture.name):
                    oracle_out = tmp_path / f"oracle-{fixture.stem}.json"
                    oracle_code = probe_main(["--session-log", str(fixture), "--leg", "od", "--slot-a", "4",
                                              "--slot-b", "5", "--metadata", str(metadata),
                                              "--base-report", str(base_report), "--out", str(oracle_out)])
                    runner_out = tmp_path / f"runner-{fixture.stem}"
                    with contextlib.redirect_stdout(io.StringIO()):
                        runner_code = runner_main([
                            "run", "--verify-only", "--verify-serial-log", str(fixture),
                            "--scenarios", OD, "--track", "8", "--loop-slot", "4", "--midi-channel", "8",
                            "--slot-b", "5", "--metadata", str(metadata), "--base-report", str(base_report),
                            "--out-dir", str(runner_out)])
                    wired = json.loads(verify_report_paths(runner_out).read_text())["results"][OD]
                    oracle = json.loads(oracle_out.read_text())
                    self.assertEqual(oracle["verdict"], wired["verdict"])
                    self.assertEqual(should_pass, wired["ok"])
                    # The oracle keeps its own exit-code vocabulary (0/2/3/4); the runner reports pass or
                    # non-pass (0/2). What must agree is the verdict and the fact of passing.
                    self.assertEqual(should_pass, oracle_code == 0 and runner_code == 0)

    def test_verify_only_without_a_log_is_refused_by_the_runner(self):
        with tempfile.TemporaryDirectory() as tmp:
            buffer = io.StringIO()
            with contextlib.redirect_stdout(buffer):
                code = runner_main(["run", "--verify-only", "--scenarios", OD, "--track", "8",
                                    "--loop-slot", "4", "--midi-channel", "8", "--slot-b", "5",
                                    "--out-dir", tmp])
        self.assertEqual(2, code)
        self.assertIn("requires --verify-serial-log", buffer.getvalue())


class ScenarioImportGateTests(unittest.TestCase):
    """The new scenario sits in the scenario layer: no forbidden import, and nothing device-side at import."""

    MODULE = Path(__file__).parent / "hitl" / "scenarios" / "accepted_sequence.py"
    FORBIDDEN = ("hitl.serial", "hitl.midi_io", "hitl.transport_clock", "hitl.actions", "hitl.verify",
                 "hitl.legacy_")

    def test_the_module_imports_nothing_the_scenario_layer_may_not(self):
        tree = ast.parse(self.MODULE.read_text(encoding="utf-8"))
        imported: set[str] = set()
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                imported.update(alias.name for alias in node.names)
            elif isinstance(node, ast.ImportFrom) and node.module:
                imported.add(node.module)
        violations = sorted(name for name in imported
                            if any(name == p or name.startswith(p + ".") for p in self.FORBIDDEN))
        self.assertEqual([], violations)
        # The wrapper and the oracle are the device/verify side, so they are imported *inside* functions:
        # importing this module must not drag either layer in. Only module-level statements are inspected
        # here -- the walk above deliberately covers nested imports for the forbidden prefixes, which is
        # the stricter rule for a layer the scenario may not reach at all.
        top_level: set[str] = set()
        for node in tree.body:
            if isinstance(node, ast.Import):
                top_level.update(alias.name for alias in node.names)
            elif isinstance(node, ast.ImportFrom) and node.module:
                top_level.add(node.module)
        self.assertNotIn("hitl_accepted_sequence", top_level)
        self.assertNotIn("hitl.accepted_sequence_probe", top_level)
        self.assertNotIn("hitl.verify", top_level)

    def test_importing_the_scenario_module_loads_no_device_or_verify_layer(self):
        code = (
            "import sys; sys.path.insert(0, 'scripts');"
            "import hitl.scenarios.accepted_sequence as m;"
            "print('hitl_accepted_sequence' in sys.modules,"
            "'hitl.accepted_sequence_probe' in sys.modules,"
            "'hitl.verify' in sys.modules,"
            "'hitl.midi_io' in sys.modules)"
        )
        proc = subprocess.run([sys.executable, "-c", code], cwd=str(Path(__file__).parent.parent),
                              capture_output=True, text=True, check=True)
        self.assertEqual("False False False False", proc.stdout.strip())


class UnchangedClassicBehaviourTests(unittest.TestCase):
    """`base` and `edit_full` keep resolving and behaving exactly as before."""

    def test_the_existing_presets_and_scenarios_are_unchanged(self):
        self.assertEqual(("base",), tuple(resolve_scenarios(preset="base", scenario_ids=None)))
        self.assertEqual(("edit_record_prelude", "edit_full"),
                         tuple(resolve_scenarios(preset="edit_full", scenario_ids=None)))
        self.assertEqual(
            {"base", "edit_full", "overdub_occupy_paint", "note_edit_overdub_compose",
             "note_edit_overdub_compose_last_bar_shorten", "note_edit_overdub_compose_last_bar_reopen",
             "edit_overdub_during_note_edit", "note_edit_reselect_stacked", "accepted_sequence"},
            set(PRESET_SCENARIOS))

    def test_every_registered_spec_agrees_with_its_registry_key(self):
        """A spec's own id is the registry's key: two names for one scenario is how a run gets mislabelled."""
        mismatched = sorted(sid for sid, spec in get_registry().items() if spec.scenario_id != sid)
        self.assertEqual([], mismatched)

    def test_only_the_two_new_scenarios_declare_capture_ownership(self):
        owners = sorted(sid for sid, spec in get_registry().items() if spec.owns_capture)
        self.assertEqual([OD, REC], owners)


if __name__ == "__main__":
    unittest.main(verbosity=2)
