#!/usr/bin/env python3
"""Host contract tests for split rolling-Record HITL passes and offline aggregation."""
from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl_rolling_record_aggregate import _load_run_bundle
from hitl.runner import resolve_build_env
from hitl.verify.rolling_record import (
    ROLLING_RECORD_PASS_GROUPS,
    aggregate_rolling_record_bundles,
    classify_rolling_record_pass,
    full_recipe_complete,
    rolling_record_contract_fingerprint,
)

QUAL_ENV = "teensy41-capture-serial-window-qualification"
STIMULUS = "STIM-ROLLING-RECORD-TEMP-01@v1"
FIRMWARE_HASH = "a" * 64
GIT_COMMIT = "1234567890abcdef"

_TERMINALS = {
    "growth": "corrected_frozen_handoff",
    "empty": "corrected_frozen_handoff",
    "held": "corrected_frozen_root_return",
    "matched": "matching_physical_reader_preserved",
}


def _leg(name: str, **overrides):
    leg = dict(name=name, ok=True, issues=[], stop_outcome=_TERMINALS[name],
               stop={"root_return_completed": name == "held"},
               record_evidence_drain={"complete": True})
    if name == "matched":
        leg.update(wire_inventory={"ok": None, "notes": []},
                   wire_schedule={"ok": None, "skipped": "matched leg: Stop class only"})
    else:
        leg.update(wire_inventory={"ok": True, "notes": [{}]},
                   wire_schedule={"ok": True},
                   content={"record": [{}], "effective": [{}]})
    leg.update(overrides)
    return leg


def _has_issue(result, code):
    return any(issue == code or issue.endswith(":" + code) for issue in result["issues"])


def _bundle(pass_group: str, names, *, firmware_hash=FIRMWARE_HASH,
            contract_hash=None, shutdown=None, cleanup=None, report_overrides=None,
            metadata_overrides=None):
    legs = [_leg(name) for name in names]
    classification = classify_rolling_record_pass(pass_group, legs)
    report = dict(
        report_schema_version=2,
        pass_group=pass_group,
        ok=classification["ok"],
        functional_legs_ok=classification["functional_legs_ok"],
        verdict=classification["verdict"],
        full_recipe_complete=classification["full_recipe_complete"],
        stop_terminals_by_leg={leg["name"]: leg["stop_outcome"] for leg in legs},
        issues=[],
        stimulus=STIMULUS,
        authorization={"track": 8, "slots": [4, 5]},
        validation_contract_fingerprint=(contract_hash or rolling_record_contract_fingerprint()),
        legs=legs,
    )
    report.update(report_overrides or {})
    metadata = dict(
        build_env=QUAL_ENV,
        firmware_hex_sha256=firmware_hash,
        git_commit=GIT_COMMIT,
        scenario_ids=["rolling_record"],
        track_number=8,
        loop_slot=4,
        midi_channel=8,
    )
    metadata.update(metadata_overrides or {})
    return dict(
        run_dir=f"run-{pass_group}",
        report=report,
        metadata=metadata,
        shutdown=shutdown or {"status": "complete", "ok": True, "drains": 2},
        midi_cleanup=cleanup or {"status": "complete", "ok": True, "completed": 1, "total": 1},
    )


class RollingRecordPassAggregationTests(unittest.TestCase):
    def test_grouping_keeps_the_cross_take_pair_together(self):
        self.assertEqual(ROLLING_RECORD_PASS_GROUPS["growth-empty"], ("growth", "empty"))
        self.assertEqual(ROLLING_RECORD_PASS_GROUPS["held"], ("held",))
        self.assertEqual(ROLLING_RECORD_PASS_GROUPS["matched"], ("matched",))

    def test_partial_pass_is_smoke_only_and_never_recipe_pass(self):
        result = classify_rolling_record_pass("matched", [_leg("matched")])
        self.assertTrue(result["ok"])
        self.assertEqual(result["verdict"], "SMOKE_ONLY")
        self.assertFalse(result["full_recipe_complete"])

    def test_growth_and_empty_individual_diagnostics_cannot_substitute_for_group(self):
        reports = [
            _bundle("growth", ["growth"]),
            _bundle("empty", ["empty"]),
            _bundle("held", ["held"]),
            _bundle("matched", ["matched"]),
        ]
        result = aggregate_rolling_record_bundles(reports)
        self.assertFalse(result["ok"])
        self.assertTrue(_has_issue(result, "growth_empty_same_run_required"))

    def test_three_short_passes_aggregate_to_full_recipe(self):
        result = aggregate_rolling_record_bundles([
            _bundle("growth-empty", ["growth", "empty"]),
            _bundle("held", ["held"]),
            _bundle("matched", ["matched"]),
        ])
        self.assertTrue(result["ok"], result["issues"])
        self.assertTrue(result["full_recipe_complete"])
        self.assertEqual(result["verdict"], "PASS")
        self.assertEqual(set(result["stop_terminals_by_leg"]), set(_TERMINALS))

    def test_one_all_pass_report_remains_supported(self):
        result = aggregate_rolling_record_bundles([
            _bundle("all", ("growth", "empty", "held", "matched")),
        ])
        self.assertTrue(result["ok"], result["issues"])
        self.assertEqual(result["verdict"], "PASS")

    def test_missing_or_duplicate_group_refuses(self):
        short = aggregate_rolling_record_bundles([
            _bundle("growth-empty", ["growth", "empty"]),
            _bundle("held", ["held"]),
        ])
        duplicate_reports = [
            _bundle("growth-empty", ["growth", "empty"]),
            _bundle("held", ["held"]),
            _bundle("held", ["held"]),
            _bundle("matched", ["matched"]),
        ]
        duplicate_reports[2]["run_dir"] = "run-held-duplicate"
        duplicate = aggregate_rolling_record_bundles(duplicate_reports)
        self.assertFalse(short["ok"])
        self.assertTrue(_has_issue(short, "required_pass_group_missing"))
        self.assertFalse(duplicate["ok"])
        self.assertTrue(_has_issue(duplicate, "duplicate_pass_group"))

    def test_cross_image_or_contract_reports_refuse(self):
        reports = [
            _bundle("growth-empty", ["growth", "empty"]),
            _bundle("held", ["held"], firmware_hash="b" * 64),
            _bundle("matched", ["matched"]),
        ]
        result = aggregate_rolling_record_bundles(reports)
        self.assertFalse(result["ok"])
        self.assertTrue(_has_issue(result, "firmware_identity_mismatch"))

        reports[1]["metadata"]["firmware_hex_sha256"] = FIRMWARE_HASH
        reports[1]["report"]["validation_contract_fingerprint"] = "c" * 64
        result = aggregate_rolling_record_bundles(reports)
        self.assertFalse(result["ok"])
        self.assertTrue(_has_issue(result, "validation_contract_mismatch"))

    def test_unpinned_wrong_profile_or_unauthorized_report_refuses(self):
        cases = [
            (_bundle("held", ["held"], firmware_hash=None), "firmware_identity_missing"),
            (_bundle("held", ["held"], metadata_overrides={"build_env": "teensy41-capture-serial"}),
             "qualification_build_required"),
            (_bundle("held", ["held"], report_overrides={"stimulus": "STIM-OTHER@v1"}),
             "stimulus_mismatch"),
            (_bundle("held", ["held"], report_overrides={"authorization": {"track": 5, "slots": [1]}}),
             "authorization_mismatch"),
        ]
        for invalid, issue in cases:
            with self.subTest(issue=issue):
                result = aggregate_rolling_record_bundles([
                    _bundle("growth-empty", ["growth", "empty"]),
                    invalid,
                    _bundle("matched", ["matched"]),
                ])
                self.assertFalse(result["ok"])
                self.assertTrue(_has_issue(result, issue))

    def test_incomplete_shutdown_or_midi_close_refuses(self):
        shutdown_bad = _bundle("held", ["held"], shutdown={"status": "incomplete_timeout", "ok": False, "drains": 1})
        cleanup_bad = _bundle("matched", ["matched"], cleanup={"status": "failed", "ok": False,
                                                                 "completed": 0, "total": 1})
        base = _bundle("growth-empty", ["growth", "empty"])
        result = aggregate_rolling_record_bundles([base, shutdown_bad,
                                                    _bundle("matched", ["matched"])])
        self.assertFalse(result["ok"])
        self.assertTrue(_has_issue(result, "shutdown_incomplete"))
        result = aggregate_rolling_record_bundles([base,
                                                    _bundle("held", ["held"]), cleanup_bad])
        self.assertFalse(result["ok"])
        self.assertTrue(_has_issue(result, "midi_cleanup_incomplete"))

    def test_partial_cannot_claim_recipe_pass_and_bad_leg_terminal_refuses(self):
        partial_claim = _bundle("matched", ["matched"], report_overrides={
            "verdict": "PASS", "full_recipe_complete": True,
        })
        result = aggregate_rolling_record_bundles([
            _bundle("growth-empty", ["growth", "empty"]),
            _bundle("held", ["held"]),
            partial_claim,
        ])
        self.assertFalse(result["ok"])
        self.assertTrue(_has_issue(result, "partial_report_claims_recipe_pass"))

        broken = _bundle("matched", ["matched"])
        broken["report"]["legs"][0]["stop_outcome"] = "corrected_frozen_handoff"
        broken["report"]["stop_terminals_by_leg"]["matched"] = "corrected_frozen_handoff"
        result = aggregate_rolling_record_bundles([
            _bundle("growth-empty", ["growth", "empty"]),
            _bundle("held", ["held"]),
            broken,
        ])
        self.assertFalse(result["ok"])
        self.assertTrue(_has_issue(result, "full_recipe_predicate_failed"))

    def test_authorized_slot_five_is_accepted_but_mixed_slots_refuse(self):
        slot_five = [
            _bundle("growth-empty", ["growth", "empty"], metadata_overrides={"loop_slot": 5}),
            _bundle("held", ["held"], metadata_overrides={"loop_slot": 5}),
            _bundle("matched", ["matched"], metadata_overrides={"loop_slot": 5}),
        ]
        result = aggregate_rolling_record_bundles(slot_five)
        self.assertTrue(result["ok"], result["issues"])

        mixed_slots = [
            _bundle("growth-empty", ["growth", "empty"]),
            _bundle("held", ["held"], metadata_overrides={"loop_slot": 5}),
            _bundle("matched", ["matched"]),
        ]
        result = aggregate_rolling_record_bundles(mixed_slots)
        self.assertFalse(result["ok"])
        self.assertTrue(_has_issue(result, "selected_slot_mismatch"))

    def test_cleanup_completion_rejects_boolean_count(self):
        malformed = _bundle("matched", ["matched"], cleanup={
            "status": "complete", "ok": True, "completed": True, "total": 1,
        })
        result = aggregate_rolling_record_bundles([
            _bundle("growth-empty", ["growth", "empty"]),
            _bundle("held", ["held"]),
            malformed,
        ])
        self.assertFalse(result["ok"])
        self.assertTrue(_has_issue(result, "midi_cleanup_incomplete"))

    def test_invalid_utf8_artifact_is_refused_as_unreadable(self):
        with tempfile.TemporaryDirectory() as temporary:
            run_dir = Path(temporary)
            names = ("rolling-record.json", "metadata.json", "shutdown-result.json",
                     "midi-cleanup-result.json")
            for name in names:
                payload = bytes([0xFF]) if name == "rolling-record.json" else b"{}"
                (run_dir / name).write_bytes(payload)
            with self.assertRaisesRegex(ValueError, "run_artifact_unreadable:rolling-record.json"):
                _load_run_bundle(run_dir)

    def test_rolling_record_build_env_defaults_to_qualification_only_for_its_preset(self):
        self.assertEqual(resolve_build_env(None, ["rolling_record"]), QUAL_ENV)
        self.assertEqual(resolve_build_env(None, ["base"]), "teensy41-capture-serial")
        with self.assertRaisesRegex(ValueError, "rolling_record_requires_qualification_build_env"):
            resolve_build_env("teensy41-capture-serial", ["rolling_record"])
        self.assertEqual(resolve_build_env(QUAL_ENV, ["rolling_record"]), QUAL_ENV)


if __name__ == "__main__":
    unittest.main()
