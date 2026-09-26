#!/usr/bin/env python3
"""Host tests for the S4f.3 retention oracle (`hitl/retention_probe.py`).

These run without a device: they feed synthetic capture logs shaped exactly like the device's `#CAP`
records and assert the oracle's verdict, its setup/retention separation and its exit codes. The
telemetry shapes are taken from a real managed run
(`captures/s4f3-stop-t5s1-120bpm-20260926_141559`), so a format drift here is a real regression.

Run: .venv/bin/python scripts/test_retention_probe.py
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from hitl.retention_probe import DEFAULT_MIN_WINDOW_US, analyse  # noqa: E402

BOOT = "BOOT,usb_host,deferred"
TEMPO = "[91.823] [DEBUG] [MIDI] Sent tempo CC feedback: 120.0 BPM -> CC=60"


def log(*lines: str, boot: bool = True, tempo: bool = True) -> str:
    head = ([BOOT] if boot else []) + ([TEMPO] if tempo else [])
    return "\n".join(head + list(lines)) + "\n"


def full_pass_log(*, window_us: int = 1_335_389, denial_us: int | None = None,
                  extra_after_start: list[str] | None = None) -> str:
    """A well-formed staged run: A finalizes on slot 0, B's stop is denied on slot 1, C starts once.

    `denial_us` moves the denial records' *timestamps* (the `#CAP,<us>,` field) as well as their
    payloads, which is what decides whether they fall inside the finalization window.
    """
    open_us = 100_000_000
    release_us = open_us + window_us
    denial_us = open_us + 20_000 if denial_us is None else denial_us
    start_us = open_us + 200_000
    lines = [
        f"#CAP,{open_us},RECS,stop,0,13930,13663,1803,1536,0",
        f"#CAP,{open_us},DIAG,reentry_window_open,0,16387,1536",
        f"#CAP,{denial_us},DIAG,reentry,deny,16400,3,1,1",
        f"#CAP,{denial_us},ODUB,stop,admission,0,0,4294967295,4294967295,1,0,"
        "successor_stop_denied_capacity",
        f"#CAP,{open_us + 30_000},ODUB,stop,admission,0,0,4294967295,4294967295,1,0,"
        "accepted_stop_finalizer_wait",
        f"#CAP,{release_us},DIAG,canonical_release,0,1536,8",
        f"#CAP,{start_us},ST,Track,ARMED,RECORDING",
        f"#CAP,{start_us},RECA,2,17000",
        *(extra_after_start or []),
    ]
    return log(*lines)


def checks_by_name(analysis) -> dict:
    return {c.name: c for c in analysis.checks}


class RetentionProbeTests(unittest.TestCase):
    def analyse(self, text, **kw):
        kw.setdefault("slot_a", 0)
        kw.setdefault("slot_b", 1)
        kw.setdefault("slot_c", 2)
        return analyse(text, **kw)

    def test_a_complete_sequence_passes_and_reports_the_measured_window(self):
        result = self.analyse(full_pass_log())
        self.assertEqual("RETENTION_PASS", result.verdict, [c.detail for c in result.checks])
        self.assertEqual(0, result.exit_code)
        self.assertIsNotNone(result.episode)
        self.assertEqual(1_335_389, result.episode.window_us)
        self.assertTrue(all(c.ok for c in result.checks))

    def test_a_missing_tempo_readback_is_a_setup_failure_not_a_retention_failure(self):
        result = self.analyse(full_pass_log(), tempo_bpm=120.0)
        self.assertEqual("RETENTION_PASS", result.verdict)
        # Same sequence, but the tempo was never read back: the run is not a retention verdict.
        result = self.analyse(log(*full_pass_log().splitlines()[2:], tempo=False))
        self.assertEqual("SETUP_INCOMPLETE", result.verdict)
        self.assertEqual(3, result.exit_code)
        failed = [c.name for c in result.checks if not c.ok]
        self.assertEqual(["tempo_verified"], failed)
        # ...and crucially no retention check was allowed to decide the verdict.
        self.assertFalse(any(c.kind == "retention" and not c.ok for c in result.checks))

    def test_a_stale_device_without_a_boot_banner_is_a_setup_failure(self):
        body = "\n".join(full_pass_log().splitlines()[2:])
        result = self.analyse(body + "\n")
        self.assertEqual("SETUP_INCOMPLETE", result.verdict)
        self.assertIn("fresh_boot", [c.name for c in result.checks if not c.ok])

    def test_no_denial_means_not_exercised_rather_than_failure(self):
        text = full_pass_log().replace("deny,16400,3,1,1", "admit,16400,3,1,1")
        text = "\n".join(l for l in text.splitlines()
                         if "successor_stop_denied_capacity" not in l) + "\n"
        result = self.analyse(text)
        self.assertEqual("RETENTION_NOT_EXERCISED", result.verdict)
        self.assertEqual(2, result.exit_code)
        self.assertFalse(checks_by_name(result)["successor_stop_admission_denied"].ok)

    def test_a_denial_outside_the_finalization_window_does_not_count_as_staged(self):
        # Denial before A's window opens: the blocked state was not what was exercised. Both the DIAG
        # denial and its stop-stage record move, because the timestamp is what places them in the window.
        result = self.analyse(full_pass_log(denial_us=50_000))
        self.assertEqual("RETENTION_NOT_EXERCISED", result.verdict)
        self.assertIn("never entered", checks_by_name(result)["successor_stop_admission_denied"].detail)

    def test_two_successor_starts_fail_exactly_once(self):
        extra = ["#CAP,300000000,ST,Track,ARMED,RECORDING", "#CAP,300000000,RECA,2,18000"]
        result = self.analyse(full_pass_log(extra_after_start=extra))
        self.assertEqual("RETENTION_FAIL", result.verdict)
        self.assertIn("more than once", checks_by_name(result)["successor_started_exactly_once"].detail)

    def test_a_start_on_the_wrong_slot_does_not_satisfy_the_requirement(self):
        # The retained start executed on B instead of C: no RECA for slot 2 at all.
        text = full_pass_log().replace("RECA,2,17000", "RECA,1,17000")
        result = self.analyse(text)
        self.assertEqual("RETENTION_FAIL", result.verdict)
        self.assertIn("never executed", checks_by_name(result)["successor_started_exactly_once"].detail)

    def test_an_activation_invariant_violation_fails_loudly(self):
        extra = ["[INFO] [TRACK] Track 0: record activation invariant violated"]
        result = self.analyse(full_pass_log(extra_after_start=extra))
        self.assertEqual("RETENTION_FAIL", result.verdict)
        self.assertFalse(checks_by_name(result)["activation_invariant_held"].ok)

    def test_a_narrow_window_is_an_exposure_setup_failure_not_a_retention_result(self):
        result = self.analyse(full_pass_log(window_us=40_000), min_window_us=DEFAULT_MIN_WINDOW_US)
        self.assertEqual("SETUP_INCOMPLETE", result.verdict)
        self.assertIn("too narrow", checks_by_name(result)["capacity_denied_window_wide_enough"].detail)

    def test_an_empty_log_is_a_setup_failure(self):
        result = self.analyse("")
        self.assertEqual("SETUP_INCOMPLETE", result.verdict)
        self.assertEqual(["telemetry_present"], [c.name for c in result.checks if not c.ok])

    def test_replay_of_the_stop_obligation_after_release_fails(self):
        extra = ["#CAP,400000000,DIAG,reentry,deny,20000,4,1,0"]
        result = self.analyse(full_pass_log(extra_after_start=extra))
        self.assertEqual("RETENTION_FAIL", result.verdict)
        self.assertFalse(checks_by_name(result)["no_stop_obligation_replay"].ok)


if __name__ == "__main__":
    unittest.main(verbosity=2)
