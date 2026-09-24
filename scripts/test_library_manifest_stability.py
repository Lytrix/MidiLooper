#!/usr/bin/env python3
"""Unit tests for the complete per-slot manifest comparator (Packet B / task 2.2).

The expected verdicts are written explicitly here; they are not read back from the implementation. A
difference in a non-fixture slot is a failure, a window-scoped difference is not, and an incomplete or
mid-session capture is ineligible rather than compared.
"""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.library_manifest import (  # noqa: E402
    BootManifest,
    compare_manifests,
    load_boot_manifest,
    manifest_fingerprint,
    stability_report,
)


def slot(
    *,
    payload: str = "present_verified",
    has_committed_content: bool = True,
    pass_count: int = 2,
    highest_pass_id: int = 2,
    loop_length: int = 15360,
    event_count: int = 729,
    required: bool = True,
    request: str = "terminal",
    terminal: str = "committed",
    completion_in_window: bool = False,
) -> dict[str, object]:
    return {
        "track": 0,
        "slot": 0,
        "state_mask": 2,
        "state": ["populated"],
        "cause": "not_applicable",
        "payload": payload,
        "offered": "offered",
        "request": request,
        "wait": 0,
        "terminal": terminal,
        "generation": 0,
        "epoch": 0,
        "has_committed_content": has_committed_content,
        "committed_pass_count": pass_count,
        "highest_committed_pass_id": highest_pass_id,
        "loop_length_ticks": loop_length,
        "committed_event_count": event_count,
        "required_for_initial_playback": required,
        "committed_terminal_in_window": completion_in_window,
        "invalidated": False,
    }


def manifest(slots: dict[tuple[int, int], dict[str, object]], name: str) -> BootManifest:
    boot = BootManifest(
        capture_dir=name,
        fresh_boot=True,
        boot_banner=True,
        boot_reset_us=900000,
        complete=True,
        incomplete_window=False,
        window={"slots_observed": len(slots), "restarts": 0},
        counts={
            "payload_inventory": len(slots),
            "initial_playback_required": sum(
                1 for record in slots.values() if record["required_for_initial_playback"]
            ),
            "observed_restore_completions": 0,
            "committed_at_checkpoint": len(slots),
        },
    )
    boot.slots = dict(slots)
    return boot


class ComparisonTests(unittest.TestCase):
    def test_identical_manifests_pass_in_full(self) -> None:
        expected = manifest({(0, 0): slot()}, "expected")
        observed = manifest({(0, 0): slot()}, "observed")
        result = compare_manifests(expected, observed)
        self.assertTrue(result["pass"])
        self.assertEqual([], result["manifest_differences"])

    def test_content_difference_in_a_non_fixture_slot_fails(self) -> None:
        expected = manifest({(2, 7): slot(event_count=100)}, "expected")
        observed = manifest({(2, 7): slot(event_count=101)}, "observed")
        result = compare_manifests(expected, observed, authorized_changes=[(5, 1)])
        self.assertFalse(result["pass"])
        difference = result["unexplained_manifest_differences"][0]
        self.assertEqual((2, 7), (difference["track"], difference["slot"]))
        self.assertIn("committed_event_count", difference["field_differences"])

    def test_authorized_change_in_the_fixture_slot_is_not_a_failure(self) -> None:
        expected = manifest({(5, 1): slot(event_count=100), (0, 0): slot()}, "expected")
        observed = manifest({(5, 1): slot(event_count=250), (0, 0): slot()}, "observed")
        result = compare_manifests(expected, observed, authorized_changes=[(5, 1)])
        self.assertTrue(result["pass"])
        self.assertEqual(1, len(result["manifest_differences"]))
        self.assertEqual([], result["unexplained_manifest_differences"])

    def test_payload_presence_difference_fails(self) -> None:
        expected = manifest({(3, 4): slot()}, "expected")
        observed = manifest({(3, 4): slot(payload="absent", has_committed_content=False)}, "observed")
        result = compare_manifests(expected, observed)
        self.assertFalse(result["pass"])
        self.assertIn("payload", result["unexplained_manifest_differences"][0]["field_differences"])

    def test_window_scoped_completion_difference_does_not_fail(self) -> None:
        expected = manifest({(0, 0): slot(completion_in_window=False)}, "expected")
        observed = manifest({(0, 0): slot(completion_in_window=True)}, "observed")
        result = compare_manifests(expected, observed)
        self.assertTrue(result["pass"])
        self.assertEqual(1, len(result["window_scoped_differences"]))

    def test_lifecycle_difference_is_reported_and_fails(self) -> None:
        expected = manifest({(1, 2): slot()}, "expected")
        observed = manifest({(1, 2): slot(request="none", terminal="none")}, "observed")
        result = compare_manifests(expected, observed)
        self.assertFalse(result["pass"])
        self.assertEqual([], result["unexplained_manifest_differences"])
        self.assertEqual(1, len(result["unexplained_lifecycle_differences"]))

    def test_settled_count_difference_fails(self) -> None:
        expected = manifest({(0, 0): slot()}, "expected")
        observed = manifest({(0, 0): slot()}, "observed")
        observed.counts["committed_at_checkpoint"] = 0
        result = compare_manifests(expected, observed)
        self.assertFalse(result["pass"])
        self.assertIn("committed_at_checkpoint", result["count_differences"])


class FingerprintTests(unittest.TestCase):
    def test_identical_manifests_share_a_fingerprint(self) -> None:
        first = manifest({(0, 0): slot(), (5, 1): slot()}, "a")
        second = manifest({(0, 0): slot(), (5, 1): slot()}, "b")
        self.assertEqual(manifest_fingerprint(first), manifest_fingerprint(second))

    def test_fingerprint_changes_with_content(self) -> None:
        first = manifest({(0, 0): slot(event_count=729)}, "a")
        second = manifest({(0, 0): slot(event_count=730)}, "b")
        self.assertNotEqual(manifest_fingerprint(first), manifest_fingerprint(second))

    def test_fingerprint_ignores_the_window_scoped_field(self) -> None:
        first = manifest({(0, 0): slot(completion_in_window=False)}, "a")
        second = manifest({(0, 0): slot(completion_in_window=True)}, "b")
        self.assertEqual(manifest_fingerprint(first), manifest_fingerprint(second))


def write_capture(
    directory: Path,
    *,
    boot_reset_us: int | None,
    complete: bool,
    slots: dict[tuple[int, int], dict[str, object]],
) -> None:
    observation = {
        "requested": True,
        "begin": {"a": "900000", "e": "0", "s": "64"},
        "end_fields": {"a": "900000", "b": "1200000", "t": "64", "r": "0", "s": str(len(slots))},
        "slots": [
            {**record, "track": track, "slot": slot_index}
            for (track, slot_index), record in slots.items()
        ],
        "counts": {
            "payload_inventory": len(slots),
            "initial_playback_required": 1,
            "observed_restore_completions": 0,
            "committed_at_checkpoint": len(slots),
        },
        "window": {
            "start_us": 900000,
            "end_us": 1200000,
            "turns": 64,
            "restarts": 0,
            "slots_observed": len(slots),
            "admission_complete": complete,
            "every_initial_playback_slot_observed": complete,
            "consistent_view": False,
            "complete": complete,
        },
        "complete": complete,
        "incomplete_window": not complete,
    }
    census = {"boot_banner": boot_reset_us is not None, "library_observation": observation}
    (directory / "census.json").write_text(json.dumps(census), encoding="utf-8")
    lines = []
    if boot_reset_us is not None:
        lines.append(f"#CAP,{boot_reset_us},DIAG,boot_reset,crash=0")
    lines.append("#CAP,900000,LIBOBS,B,a=900000,e=0,s=64")
    (directory / "boot-census.log").write_text("\n".join(lines) + "\n", encoding="utf-8")


class EligibilityTests(unittest.TestCase):
    def test_incomplete_window_is_ineligible_and_not_compared(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name, boot_reset, complete in (
                ("boot-a", 900000, True),
                ("boot-b", 910000, False),
                ("boot-c", 920000, True),
            ):
                directory = root / name
                directory.mkdir()
                write_capture(directory, boot_reset_us=boot_reset, complete=complete,
                              slots={(0, 0): slot()})
            report = stability_report([root / "boot-a", root / "boot-b", root / "boot-c"])
            self.assertEqual(2, report["eligible_boots"])
            self.assertFalse(report["enough_boots"])
            self.assertEqual("fail", report["verdict"])
            ineligible = {Path(b["capture_dir"]).name for b in report["ineligible_boots"]}
            self.assertEqual({"boot-b"}, ineligible)
            compared = {Path(c["observed_capture"]).name for c in report["comparisons"]}
            self.assertEqual({"boot-c"}, compared)

    def test_mid_session_observation_is_ineligible(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp) / "mid-session"
            directory.mkdir()
            write_capture(directory, boot_reset_us=None, complete=True, slots={(0, 0): slot()})
            boot = load_boot_manifest(directory)
            self.assertFalse(boot.fresh_boot)
            self.assertFalse(boot.eligible)
            self.assertIn("mid-session", boot.ineligible_reason or "")

    def test_three_eligible_consecutive_boots_pass_and_reuse_an_existing_expected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            directories = []
            for index, name in enumerate(("existing-expected", "boot-1", "boot-2", "boot-3")):
                directory = root / name
                directory.mkdir()
                write_capture(directory, boot_reset_us=900000 + index, complete=True,
                              slots={(0, 0): slot(), (5, 1): slot(required=False, request="none",
                                                                     terminal="none")})
                directories.append(directory)
            report = stability_report(
                directories[1:], expected_dir=directories[0], required_boots=3
            )
            self.assertEqual(4, report["eligible_boots"])
            self.assertEqual(3, report["observed_eligible_boots"])
            self.assertEqual(4, report["distinct_boot_resets"])
            self.assertTrue(report["all_match"])
            self.assertEqual("pass", report["verdict"])

    def test_a_difference_in_a_non_fixture_slot_fails_the_three_boot_gate(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            directories = []
            for index, name in enumerate(("boot-1", "boot-2", "boot-3")):
                directory = root / name
                directory.mkdir()
                events = 729 if index != 2 else 730
                write_capture(directory, boot_reset_us=900000 + index, complete=True,
                              slots={(7, 6): slot(event_count=events)})
                directories.append(directory)
            report = stability_report(directories, required_boots=3)
            self.assertEqual("fail", report["verdict"])
            self.assertFalse(report["all_match"])


if __name__ == "__main__":
    unittest.main()
