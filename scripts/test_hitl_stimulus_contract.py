#!/usr/bin/env python3
"""Host tests for the HITL stimulus registry and fingerprint."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.stimuli import (
    STIM_BASE_2_ONE_OD_01,
    STIM_NE_OD_COMPOSE_01,
    format_hitl_header,
    get_stimulus,
    prepend_hitl_header,
    stimulus_for_scenario,
    temporary_variant,
    validate_stimulus,
)
from hitl.verify.note_edit_overdub_compose import verify_note_edit_overdub_compose
from test_note_edit_overdub_compose_serial_verify import _pass_lines


class StimulusRegistryTests(unittest.TestCase):
    def test_compose_scenario_binds_canonical_stimulus(self) -> None:
        spec = stimulus_for_scenario("note_edit_overdub_compose")
        self.assertIsNotNone(spec)
        assert spec is not None
        self.assertEqual(spec.stimulus_id, "STIM-NE-OD-COMPOSE-01")
        self.assertEqual(spec.version, 1)
        self.assertEqual(spec.base_stimulus_id, STIM_BASE_2_ONE_OD_01.stimulus_id)
        self.assertEqual(spec.generator, "scenario:note_edit_overdub_compose")
        self.assertIn("live_capture_notes_present", spec.must_prove)

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


if __name__ == "__main__":
    unittest.main()
