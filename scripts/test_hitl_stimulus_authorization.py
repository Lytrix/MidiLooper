#!/usr/bin/env python3
"""Host tests for the run-scoped target authorization manifest (task 4.1, host half).

Task 4.1 requires the target authorization manifest as a run input and requires an **unauthorized
target to stop the run before further mutation**. Three things are pinned here without a device:

1. **The shape** -- a declaration carries a 1-based track, the slots it may mutate and the provenance
   of the grant; an empty scope is a read-only profile; a violation names the target *and* the scope.
2. **The declarations** -- the profiles the response-driven recipes drive declare the bench target they
   were actually driven on, the listing and the YAML view carry it, and a session can fork the scope
   without widening the canonical declaration.
3. **The refusal** -- a manifest that does not cover the run's targets stops both drivers *before* the
   board is reset and before any gesture, and the run still publishes the refusal by name. The control
   is the same targets on the explicit CLI path, which stays the compatibility path.

Run: .venv/bin/python scripts/test_hitl_stimulus_authorization.py
"""

import json
import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))

import hitl_accepted_sequence  # noqa: E402
import hitl_retention_handoff  # noqa: E402
from hitl.stimuli.authorization import (  # noqa: E402
    CLI_SOURCE,
    AuthorizationRefused,
    authorize_targets,
)
from hitl.stimuli.registry import (  # noqa: E402
    BENCH_SLOTS,
    BENCH_TRACK,
    STIM_BASE_2_2_01,
    STIM_BASE_2_ONE_OD_01,
    authorization_for,
    format_stimulus_list_lines,
    list_stimuli,
    temporary_variant,
)
from hitl.stimuli.spec import Authorization  # noqa: E402
from hitl.stimuli.yaml_view import export_registry_yaml, spec_to_mapping  # noqa: E402

SESSION_GRANT = "session 2026-09-30: bench scratch scope for these hosts"


class AuthorizationShapeTests(unittest.TestCase):
    """What a declaration means, independent of any device or driver."""

    def test_a_declaration_names_its_scope_and_its_source(self):
        declared = Authorization(track=8, slots=(4, 5), source=SESSION_GRANT)
        self.assertEqual("track 8 slots 4,5", declared.targets_text())
        self.assertEqual("4,5", declared.slots_text())
        self.assertTrue(declared.allows(track=8, slots=(5, 4)))

    def test_a_target_outside_the_scope_is_named_with_the_scope(self):
        declared = Authorization(track=8, slots=(4, 5), source=SESSION_GRANT)
        self.assertEqual(
            ("slot 7 is not authorized on track 8 (authorized slots: 4,5)",),
            declared.violations(track=8, slots=(4, 7)))
        self.assertEqual(
            ("track 3 is not the authorized track 8",),
            declared.violations(track=3, slots=(4, 5)))
        self.assertEqual((), declared.violations(track=8, slots=(4, 5)))
        self.assertFalse(declared.allows(track=8, slots=(7,)))

    def test_an_empty_scope_is_a_read_only_profile(self):
        declared = Authorization(track=8, slots=(), source=SESSION_GRANT)
        self.assertEqual("none", declared.slots_text())
        self.assertEqual("track 8 slots none", declared.targets_text())
        self.assertFalse(declared.allows(track=8, slots=(4,)))

    def test_numbers_outside_the_device_range_are_refused(self):
        for track in (0, 9):
            with self.subTest(track=track):
                with self.assertRaises(ValueError):
                    Authorization(track=track, slots=(4,), source=SESSION_GRANT)
        for slot in (0, 9):
            with self.subTest(slot=slot):
                with self.assertRaises(ValueError):
                    Authorization(track=8, slots=(slot,), source=SESSION_GRANT)

    def test_a_duplicated_slot_is_refused(self):
        with self.assertRaises(ValueError):
            Authorization(track=8, slots=(4, 4), source=SESSION_GRANT)

    def test_a_source_is_required(self):
        """An authorization nobody can trace is not one."""
        with self.assertRaises(ValueError):
            Authorization(track=8, slots=(4,), source="  ")

    def test_as_dict_carries_what_a_report_needs(self):
        declared = Authorization(track=8, slots=(4, 5), source=SESSION_GRANT, notes="legs")
        self.assertEqual(
            {"track": 8, "slots": [4, 5], "slots_text": "4,5", "targets": "track 8 slots 4,5",
             "source": SESSION_GRANT, "notes": "legs"},
            declared.as_dict())


class RegistryAuthorizationTests(unittest.TestCase):
    """The registry is the manifest's authority, so the declarations are pinned there."""

    def test_the_response_driven_profiles_declare_the_bench_scope(self):
        for spec in (STIM_BASE_2_2_01, STIM_BASE_2_ONE_OD_01):
            with self.subTest(spec.stimulus_id):
                declared = spec.authorization
                self.assertIsNotNone(declared, f"{spec.stimulus_id} drives the legs and must declare a scope")
                self.assertEqual(BENCH_TRACK, declared.track)
                self.assertEqual(BENCH_SLOTS, declared.slots)
                self.assertTrue(declared.allows(track=BENCH_TRACK, slots=BENCH_SLOTS))
                # Provenance is part of the declaration: the grant is quoted, not invented at run time.
                self.assertIn("2026-09-30", declared.source)

    def test_every_registered_declaration_is_well_formed(self):
        for spec in list_stimuli():
            with self.subTest(spec.stimulus_id):
                declared = authorization_for(spec.stimulus_id)
                if declared is None:
                    continue
                self.assertGreaterEqual(declared.track, 1)
                self.assertLessEqual(declared.track, 8)
                for slot in declared.slots:
                    self.assertGreaterEqual(slot, 1)
                    self.assertLessEqual(slot, 8)

    def test_an_undeclared_profile_has_no_manifest(self):
        self.assertIsNone(authorization_for("STIM-BASE-2-FIXTURE-01"))

    def test_an_unknown_id_raises_the_registrys_own_error(self):
        with self.assertRaises(KeyError):
            authorization_for("STIM-NOPE-01")

    def test_the_listing_names_each_profiles_targets(self):
        lines = format_stimulus_list_lines()
        self.assertEqual(len(lines), len(list_stimuli()))
        by_id = {line.split("@")[0]: line for line in lines}
        self.assertIn("targets=track 8 slots 4,5", by_id["STIM-BASE-2-2-01"])
        self.assertIn("targets=undeclared", by_id["STIM-BASE-2-FIXTURE-01"])
        for line in lines:
            self.assertIn("targets=", line)

    def test_the_yaml_view_exports_the_declaration(self):
        declared = STIM_BASE_2_2_01.authorization
        mapping = spec_to_mapping(STIM_BASE_2_2_01)
        # The view is `dataclasses.asdict`, so it carries the declaration's own fields (the derived
        # `targets=` text is the listing's, not an exported field).
        self.assertEqual({"track": BENCH_TRACK, "slots": tuple(BENCH_SLOTS),
                          "source": declared.source, "notes": declared.notes},
                         mapping["authorization"])
        text = export_registry_yaml()
        self.assertIn("authorization:", text)
        self.assertIn(f"track: {BENCH_TRACK}", text)
        # The declared slots are a sequence inside a mapping: they must come out as indented block
        # entries, never inlined after their key (the historical invalid-YAML symptom).
        self.assertRegex(text, r"slots:\n\s+- 4\n\s+- 5")
        self.assertIn("targets=track 8 slots 4,5", "\n".join(format_stimulus_list_lines()))

    def test_a_session_can_fork_the_scope_without_widening_the_canonical_one(self):
        temp = temporary_variant(
            "STIM-BASE-2-2-01", "SCOPE",
            authorization=Authorization(track=6, slots=(3,), source="session: other bench target"))
        self.assertEqual((3,), temp.authorization.slots)
        self.assertEqual(6, temp.authorization.track)
        self.assertEqual(BENCH_SLOTS, STIM_BASE_2_2_01.authorization.slots,
                         "the canonical declaration must not move with a session fork")


class AuthorizeTargetsTests(unittest.TestCase):
    """The three inputs, and what each one records for the report."""

    def test_the_cli_path_is_the_declared_authority_and_says_so(self):
        record = authorize_targets(None, track=3, slots=(1, 2))
        self.assertEqual(CLI_SOURCE, record["source"])
        self.assertIsNone(record["declared"])
        self.assertIsNone(record["fingerprint"])
        self.assertEqual([1, 2], record["slots"])
        self.assertEqual("track 3 slots 1,2", record["targets"])
        self.assertIn("no stimulus manifest was consulted", record["notes"])
        self.assertEqual(record, authorize_targets(CLI_SOURCE, track=3, slots=(1, 2)))

    def test_a_manifest_run_records_the_profile_it_was_driven_under(self):
        record = authorize_targets("STIM-BASE-2-2-01", track=8, slots=(4, 5))
        self.assertEqual("manifest", record["source"])
        self.assertEqual("STIM-BASE-2-2-01@v1", record["fingerprint"])
        self.assertEqual(BENCH_TRACK, record["track"])
        self.assertEqual(list(BENCH_SLOTS), record["declared"]["slots"])
        self.assertIn("authorized by STIM-BASE-2-2-01@v1", record["notes"])

    def test_a_target_outside_the_manifest_is_refused_with_its_record(self):
        with self.assertRaises(AuthorizationRefused) as caught:
            authorize_targets("STIM-BASE-2-2-01", track=8, slots=(4, 7))
        self.assertEqual(
            ("slot 7 is not authorized on track 8 (authorized slots: 4,5)",),
            caught.exception.violations)
        self.assertEqual("manifest", caught.exception.record["source"])
        self.assertEqual([4, 7], caught.exception.record["slots"],
                         "the record keeps the run's targets, so a refusal says what was asked for")
        self.assertEqual(list(BENCH_SLOTS), caught.exception.record["declared"]["slots"])

    def test_a_wrong_track_is_refused_even_with_the_authorized_slots(self):
        with self.assertRaises(AuthorizationRefused) as caught:
            authorize_targets("STIM-BASE-2-2-01", track=3, slots=(4, 5))
        self.assertEqual(("track 3 is not the authorized track 8",), caught.exception.violations)

    def test_a_profile_that_declares_nothing_cannot_authorize_a_run(self):
        with self.assertRaises(AuthorizationRefused) as caught:
            authorize_targets("STIM-BASE-2-FIXTURE-01", track=8, slots=(4,))
        self.assertIn("declares no authorized targets", caught.exception.violations[0])
        self.assertIsNone(caught.exception.record["declared"])

    def test_an_unknown_id_is_a_refusal_not_a_traceback(self):
        with self.assertRaises(AuthorizationRefused) as caught:
            authorize_targets("STIM-NOPE-01", track=8, slots=(4,))
        self.assertEqual("unknown", caught.exception.record["source"])
        self.assertIn("unknown authorization", caught.exception.violations[0])


class _ReachedDevice(RuntimeError):
    """Sentinel: the guard was passed and the run moved on to the device step."""


class _SentinelResetStep(RuntimeError):
    """Sentinel raised in place of the reset/upload step.

    The refusal tests assert that the guard stops the run *before* the board is reset. Raising here means
    a revision that lost the guard fails loudly instead of flashing the board inside a host test.
    """


def _device_step_sentinel():
    class _Boom:
        def __init__(self, *args, **kwargs):
            raise _ReachedDevice("the authorization guard passed; the run reached the capture step")

    return _Boom


class DriverAuthorizationTests(unittest.TestCase):
    """Both drivers stop before the reset, before the census and before any gesture."""

    def test_the_accepted_sequence_driver_refuses_an_unauthorized_slot(self):
        with TemporaryDirectory() as tmp:
            out_dir = Path(tmp) / "run"
            with patch.object(hitl_accepted_sequence, "refuse_reset_if_latched",
                              side_effect=_SentinelResetStep):
                rc = hitl_accepted_sequence.main([
                    "--leg", "rec", "--track", "8", "--slot-a", "4", "--slot-b", "7",
                    "--out-dir", str(out_dir), "--authorization", "STIM-BASE-2-2-01"])
            report = json.loads((out_dir / "accepted-sequence.json").read_text())
        self.assertEqual(3, rc)
        self.assertEqual("UNAUTHORIZED_TARGET", report["result"])
        self.assertEqual("manifest", report["authorization"]["source"])
        self.assertEqual("slot 7 is not authorized on track 8 (authorized slots: 4,5)",
                         report["events"][-1]["violations"][0])
        # Nothing downstream of the guard ran: no census, no plan, no MIDI record, no upload attempt.
        self.assertNotIn("midi", report)
        self.assertNotIn("plan", report)
        self.assertNotIn("upload_attempts", report)
        self.assertIsNone(report["census"])

    def test_the_retention_driver_refuses_the_same_way_before_its_reset(self):
        with TemporaryDirectory() as tmp:
            out_dir = Path(tmp) / "run"
            with patch.object(hitl_retention_handoff, "refuse_reset_if_latched",
                              side_effect=_SentinelResetStep):
                rc = hitl_retention_handoff.main([
                    "--track", "8", "--slot-a", "4", "--slot-b", "5", "--slot-c", "7",
                    "--out-dir", str(out_dir), "--authorization", "STIM-BASE-2-ONE-OD-01"])
            report = json.loads((out_dir / "retention-handoff.json").read_text())
        self.assertEqual(3, rc)
        self.assertEqual("UNAUTHORIZED_TARGET", report["result"])
        self.assertIn("slot 7", report["events"][-1]["violations"][0])
        self.assertNotIn("midi", report)

    def test_the_same_targets_are_refused_under_a_manifest_and_accepted_on_the_cli_path(self):
        """The manifest is what decides, and the compatibility path is not silently narrowed by it."""
        with TemporaryDirectory() as tmp:
            cli_out = Path(tmp) / "cli"
            with patch.object(hitl_accepted_sequence, "ManagedCaptureSession", _device_step_sentinel()):
                with self.assertRaises(_ReachedDevice):
                    hitl_accepted_sequence.main([
                        "--no-upload", "--leg", "rec", "--track", "3", "--slot-a", "4", "--slot-b", "5",
                        "--out-dir", str(cli_out), "--authorization", "cli"])
            manifest_out = Path(tmp) / "manifest"
            with patch.object(hitl_accepted_sequence, "refuse_reset_if_latched",
                              side_effect=_SentinelResetStep):
                rc = hitl_accepted_sequence.main([
                    "--leg", "rec", "--track", "3", "--slot-a", "4", "--slot-b", "5",
                    "--out-dir", str(manifest_out), "--authorization", "STIM-BASE-2-2-01"])
            report = json.loads((manifest_out / "accepted-sequence.json").read_text())
        self.assertEqual(3, rc)
        self.assertEqual("UNAUTHORIZED_TARGET", report["result"])
        self.assertEqual(("track 3 is not the authorized track 8",), tuple(
            report["events"][-1]["violations"]))

    def test_a_declared_scope_that_covers_the_run_reaches_the_device_step(self):
        with TemporaryDirectory() as tmp:
            with patch.object(hitl_accepted_sequence, "ManagedCaptureSession", _device_step_sentinel()):
                with self.assertRaises(_ReachedDevice):
                    hitl_accepted_sequence.main([
                        "--no-upload", "--leg", "od", "--track", "8", "--slot-a", "4", "--slot-b", "5",
                        "--out-dir", str(Path(tmp) / "run"), "--authorization", "STIM-BASE-2-2-01"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
