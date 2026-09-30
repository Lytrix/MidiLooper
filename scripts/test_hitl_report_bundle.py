#!/usr/bin/env python3
"""Tests for the sealed report bundle and its device-free replay (task 3.3).

The clauses this pins, each on its own evidence:

* **The schema is versioned and pinned.** A bundle carries `BUNDLE_SCHEMA_VERSION` and a digest over its whole
  content, so a reader can tell a bundle it does not know from one that was edited.
* **The verdict is derived from the evidence, once.** A pass needs every recorded predicate ok *and* a
  successful teardown; a caller cannot claim a success its own evidence denies, and the inconsistency is named.
* **A bundle is sealed once.** Writing is atomic and refuses to overwrite, so a rerun cannot quietly replace a
  run's evidence.
* **Replay needs no device and reaches the same verdict.** Verifying a bundle opens no port and re-runs the
  same rule over the same recorded predicates; flipping a predicate is caught whether or not the digest was
  recomputed to match.
* **The four named failure shapes are representable**: setup failure, protocol absence, teardown failure and a
  legitimate non-pass.

Run: .venv/bin/python scripts/test_hitl_report_bundle.py
"""

from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.reporting import (  # noqa: E402
    BUNDLE_NAME,
    BUNDLE_SCHEMA_VERSION,
    BUNDLE_SECTIONS,
    BundleInvalid,
    build_report_bundle,
    bundle_digest,
    derive_verdict,
    load_report_bundle,
    verify_report_bundle,
    write_report_bundle,
)


def bundle(*, predicates=(), teardown=None, claims_success=True, status="DROVE_LEG", **kw):
    """A bundle with this schema's sections, for the cases below."""
    return build_report_bundle(
        run_id=kw.pop("run_id", "accepted-sequence-od-2026-09-30"),
        kind=kw.pop("kind", "accepted-sequence"),
        identity=kw.pop("identity", {"git_commit": "abc", "firmware_hex_sha256": "def"}),
        status=status,
        claims_success=claims_success,
        predicates=predicates,
        teardown=teardown if teardown is not None else {"ok": True, "status": "complete"},
        **kw,
    )


class BundleSchemaTests(unittest.TestCase):
    def test_a_bundle_is_versioned_and_digest_pinned(self):
        record = bundle(predicates=[{"check": "L2_no_capacity_denial", "ok": True}])
        self.assertEqual(BUNDLE_SCHEMA_VERSION, record["schema_version"])
        self.assertEqual([], [section for section in BUNDLE_SECTIONS if section not in record])
        self.assertEqual(bundle_digest(record), record["digest"])
        self.assertTrue(record["passed"])
        self.assertEqual([], record["failures"])

    def test_a_success_claim_over_a_failed_predicate_is_not_a_pass(self):
        record = bundle(predicates=[{"check": "overdub_interval_identity", "ok": False,
                                     "observed": "RECA/RECS do not identify overdub intervals"}])
        self.assertFalse(record["passed"])
        self.assertEqual(["overdub_interval_identity", "bundle_verdict"],
                         [failure["check"] for failure in record["failures"]])
        self.assertIn("with 1 failing check(s)", record["failures"][-1]["observed"])

    def test_shutdown_failure_prevents_success(self):
        """The spec's own rule: `shutdown failure SHALL prevent overall success`."""
        record = bundle(predicates=[{"check": "L2_no_capacity_denial", "ok": True}],
                        teardown={"ok": False, "status": "incomplete_timeout"})
        self.assertFalse(record["passed"])
        self.assertEqual(["teardown", "bundle_verdict"], [f["check"] for f in record["failures"]])
        self.assertEqual("incomplete_timeout", record["failures"][0]["observed"])

    def test_a_bundle_with_no_teardown_result_is_not_a_pass(self):
        record = bundle(teardown={}, predicates=[{"check": "ok", "ok": True}])
        self.assertFalse(record["passed"])
        self.assertEqual("none recorded", record["failures"][0]["observed"])

    def test_setup_failure_and_protocol_absence_are_named_predicates(self):
        record = bundle(status="PREPARATION_FAILED", claims_success=False, teardown={"ok": True, "status": "complete"},
                        predicates=[{"check": "census_available", "ok": False, "detail": "no observation completed"},
                                    {"check": "protocol_absent", "ok": False, "detail": "no LIBOBS family"}])
        self.assertFalse(record["passed"])
        self.assertEqual(["census_available", "protocol_absent"],
                         [failure["check"] for failure in record["failures"]])
        self.assertFalse(record["claims_success"], "a failed setup does not claim success")


class SealTests(unittest.TestCase):
    def test_a_bundle_is_sealed_once_and_leaves_nothing_half_written(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "run"
            path = write_report_bundle(out, bundle())
            self.assertEqual(out / BUNDLE_NAME, path)
            self.assertTrue(path.is_file())
            self.assertEqual([], [p.name for p in out.iterdir() if p.suffix == ".tmp"])
            with self.assertRaises(BundleInvalid):
                write_report_bundle(out, bundle())
            self.assertEqual(out / "other.json", write_report_bundle(out, bundle(), name="other.json"),
                             "a different name is a different bundle")

    def test_sealing_refuses_a_bundle_this_schema_does_not_describe(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "run"
            old = dict(bundle(), schema_version=1)
            with self.assertRaises(BundleInvalid):
                write_report_bundle(out, old)
            tampered = bundle()
            tampered["identity"]["git_commit"] = "something-else"
            with self.assertRaises(BundleInvalid):
                write_report_bundle(out, tampered)
            incomplete = bundle()
            del incomplete["timing"]
            with self.assertRaises(BundleInvalid):
                write_report_bundle(out, incomplete)

    def test_a_reader_refuses_an_unknown_version(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / BUNDLE_NAME
            path.write_text(json.dumps({"schema_version": 99}), encoding="utf-8")
            with self.assertRaises(BundleInvalid):
                load_report_bundle(path)
            with self.assertRaises(BundleInvalid):
                verify_report_bundle(path)
            path.write_text("not json at all", encoding="utf-8")
            with self.assertRaises(BundleInvalid):
                verify_report_bundle(path)


class ReplayTests(unittest.TestCase):
    def _sealed(self, tmp: str, **kw) -> Path:
        out = Path(tmp) / "run"
        return write_report_bundle(out, bundle(**kw))

    def test_replay_reaches_the_recorded_verdict_with_no_device_attached(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            path = self._sealed(tmp, predicates=[{"check": "L2", "ok": True},
                                                 {"check": "L3", "ok": True}])
            with patch("mido.open_output", side_effect=AssertionError("no MIDI port in a replay")), \
                    patch("serial.Serial", side_effect=AssertionError("no serial port in a replay")):
                first = verify_report_bundle(path)
                second = verify_report_bundle(path)
            self.assertTrue(first.ok, first.failures)
            self.assertTrue(first.passed)
            self.assertEqual(BUNDLE_SCHEMA_VERSION, first.schema_version)
            self.assertEqual(first.as_dict(), second.as_dict(),
                             "the same versioned predicates produce the same verdict")

    def test_a_legitimate_non_pass_replays_as_a_non_pass(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            path = self._sealed(tmp, status="SETUP_INCOMPLETE", claims_success=False,
                                predicates=[{"check": "fresh_boot", "ok": False, "observed": "no banner"}])
            verdict = verify_report_bundle(path)
            self.assertTrue(verdict.ok, "the bundle is intact")
            self.assertFalse(verdict.passed)
            self.assertEqual(["fresh_boot"], [failure["check"] for failure in verdict.failures])

    def test_a_bundle_cannot_silently_disagree_with_its_own_evidence(self):
        """Flip a predicate and re-pin the digest: the recorded verdict no longer matches the derivation.

        This is the guarantee the replay can actually make. A digest is integrity, not a signature: an editor
        who rewrites *everything* the rule reads -- predicate, verdict, failures and digest -- produces a
        consistent bundle, and no reader without a trusted store can tell. What it cannot produce is a bundle
        that still claims the old verdict over the new evidence.
        """
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            path = self._sealed(tmp, status="DROVE_LEG", claims_success=True,
                                predicates=[{"check": "overdub_interval_identity", "ok": True}])
            record = json.loads(path.read_text())
            record["predicates"][0]["ok"] = False            # the evidence now contradicts the verdict
            record["predicates"][0]["observed"] = "RECA/RECS do not identify overdub intervals"
            record["digest"] = bundle_digest(record)         # a careful editor re-pins the digest too
            path.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8")
            verdict = verify_report_bundle(path)
            self.assertFalse(verdict.ok)
            checks = [failure["check"] for failure in verdict.failures]
            self.assertIn("bundle_verdict", checks, "the recorded verdict no longer matches the derivation")
            self.assertIn("overdub_interval_identity", checks, "and the predicate it no longer matches")
            self.assertIn("re-deriving", verdict.failures[0]["observed"])
            self.assertTrue(record["passed"], "the file still says it passed; the replay says otherwise")

    def test_an_edited_bundle_that_keeps_its_digest_is_caught(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            path = self._sealed(tmp, predicates=[{"check": "L2", "ok": True}])
            record = json.loads(path.read_text())
            record["identity"]["firmware_hex_sha256"] = "0000"
            path.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8")
            verdict = verify_report_bundle(path)
            self.assertFalse(verdict.ok)
            self.assertEqual("bundle_digest", verdict.failures[0]["check"])
            self.assertIn("does not hash", verdict.notes[0])

    def test_the_derivation_is_the_same_rule_the_builder_used(self):
        record = bundle(predicates=[{"check": "a", "ok": True}, {"check": "b", "ok": False}],
                        teardown={"ok": True, "status": "complete"})
        passed, failures = derive_verdict(record)
        self.assertFalse(passed)
        self.assertEqual(record["passed"], passed)
        self.assertEqual([f["check"] for f in record["failures"]], [f["check"] for f in failures])


class ReplayToolTests(unittest.TestCase):
    def test_the_tool_exits_zero_for_a_pass_two_for_a_non_pass_and_three_for_a_tamper(self):
        import tempfile

        from hitl_report_replay import main

        with tempfile.TemporaryDirectory() as tmp:
            good = write_report_bundle(Path(tmp) / "good", bundle(predicates=[{"check": "L2", "ok": True}]))
            with patch("builtins.print"):
                self.assertEqual(0, main(["--bundle", str(good)]))
            incomplete = write_report_bundle(Path(tmp) / "incomplete",
                                             bundle(status="DROVE_LEG_INCOMPLETE", claims_success=False,
                                                    predicates=[{"check": "L3", "ok": False}]))
            with patch("builtins.print"):
                self.assertEqual(2, main(["--bundle", str(incomplete)]))
                self.assertEqual(0, main(["--bundle", str(incomplete), "--expect", "any"]))
            record = json.loads(good.read_text())
            record["status"] = "edited"
            good.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8")
            with patch("builtins.print"):
                self.assertEqual(3, main(["--bundle", str(good)]))
            with patch("builtins.print"):
                self.assertEqual(3, main(["--bundle", str(Path(tmp) / "absent.json")]))


if __name__ == "__main__":
    unittest.main(verbosity=2)
