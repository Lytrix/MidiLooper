"""Host tests for scripts/test_native.py: selection, result parsing and sticky shard assignment."""
from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_native as tn  # noqa: E402

MAP = {
    "no_gate": ["docs/**", "*.md", "openspec/**"],
    "full": {"paths": ["include/Track.h", "test/test_support/**", "platformio.ini"]},
    "closures": [
        {"paths": ["src/Track/TrackPlaybackWindowBuild.cpp", "include/RuntimePlayback*.h"],
         "suites": ["test_lane", "test_successor"]},
    ],
    "sentinels": {"suites": ["test_sentinel"]},
}
SUITES = ["test_lane", "test_successor", "test_sentinel", "test_other", "test_harness_user"]
DIRECT = {
    "src/Track/TrackPlaybackWindowBuild.cpp": {"test_lane"},
    "test/test_lane/harness.h": {"test_lane", "test_harness_user"},
    "src/Unmapped.cpp": set(),
}
DEEP = {
    "src/Track/TrackPlaybackWindowBuild.cpp": {"test_lane", "test_other", "test_harness_user"},
    "src/Unmapped.cpp": {"test_other"},
    "include/Widely.h": {"test_lane", "test_other", "test_successor"},
}


def pick(*changed):
    return tn.select(list(changed), MAP, DEEP, SUITES, DIRECT)


class SelectionTests(unittest.TestCase):
    def test_docs_only_needs_no_native_gate(self):
        sel = pick("docs/Runtime/PROJECT_STATE.md", "README.md", "openspec/changes/x/tasks.md")
        self.assertEqual(set(), sel.suites)
        self.assertFalse(sel.full)
        self.assertEqual(3, len(sel.no_gate))

    def test_a_test_edit_selects_its_suite_and_the_sentinels(self):
        sel = pick("test/test_other/test_other.cpp")
        self.assertEqual({"test_other", "test_sentinel"}, sel.suites)

    def test_a_shared_harness_edit_selects_every_suite_that_includes_it(self):
        sel = pick("test/test_lane/harness.h")
        self.assertEqual({"test_lane", "test_harness_user", "test_sentinel"}, sel.suites)

    def test_a_mapped_cpp_selects_its_closure_not_the_whole_compile_closure(self):
        sel = pick("src/Track/TrackPlaybackWindowBuild.cpp")
        self.assertEqual({"test_lane", "test_successor", "test_sentinel"}, sel.suites)
        self.assertNotIn("test_other", sel.suites)  # it compiles the file, but only through test_support

    def test_an_unmapped_firmware_file_falls_back_to_its_compile_closure(self):
        sel = pick("src/Unmapped.cpp")
        self.assertEqual({"test_other", "test_sentinel"}, sel.suites)
        sel = pick("include/Widely.h")
        self.assertEqual({"test_lane", "test_other", "test_successor", "test_sentinel"}, sel.suites)

    def test_a_glob_closure_matches(self):
        sel = pick("include/RuntimePlaybackView.h")
        self.assertEqual({"test_lane", "test_successor", "test_sentinel"}, sel.suites)

    def test_a_broad_path_requires_full(self):
        for path in ("include/Track.h", "test/test_support/test_support.cpp", "platformio.ini"):
            sel = pick(path)
            self.assertTrue(sel.full, path)
            self.assertIn(path, sel.reasons["FULL"])

    def test_a_firmware_file_no_suite_reaches_is_reported(self):
        sel = pick("src/Nowhere.cpp")
        self.assertEqual(set(), sel.suites)
        self.assertEqual(["src/Nowhere.cpp"], sel.unmapped)

    def test_native_ignored_suites_never_run(self):
        sel = tn.select(["test/test_support_only/x.cpp"], MAP, DEEP, SUITES, DIRECT)
        self.assertEqual(set(), sel.suites)

    def test_glob_semantics(self):
        self.assertTrue(tn.glob_match("docs/a/b/c.md", "docs/**"))
        self.assertTrue(tn.glob_match("deep/dir/notes.md", "*.md"))
        self.assertTrue(tn.glob_match("include/RevisionStore/RevisionStore.h", "include/RevisionStore/**"))
        self.assertFalse(tn.glob_match("src/Track/Track.cpp", "include/Track.h"))


class ResultParsingTests(unittest.TestCase):
    def test_suite_lines_and_case_totals_are_summed_across_shards(self):
        a = ("-------- native:test_lane [PASSED] Took 2.50 seconds --------\n"
             "----- native:test_other [ERRORED] Took 1.00 seconds -----\n"
             "-------- native:test_skipped_only [SKIPPED] Took 1.72 seconds --------\n"
             "native         test_lane     PASSED    00:00:02.500\n"
             "========== 12 test cases: 1 skipped, 11 succeeded in 00:00:03.500 ==========\n")
        b = ("------ native:test_successor [FAILED] Took 65.25 seconds ------\n"
             "========== 5 test cases: 1 failed, 4 succeeded in 00:01:05.250 ==========\n")
        parsed = tn.parse_results([a, b])
        self.assertEqual(17, parsed["cases"])
        self.assertEqual(1, parsed["skipped"])
        self.assertEqual(1, parsed["failed"])
        self.assertEqual(("ERRORED", 1.0), parsed["suites"]["test_other"])
        self.assertAlmostEqual(65.25, parsed["suites"]["test_successor"][1])
        self.assertEqual("SKIPPED", parsed["suites"]["test_skipped_only"][0])  # absent from pio's summary table


class ShardTests(unittest.TestCase):
    def test_assignment_is_sticky_and_balances_new_suites(self):
        with tempfile.TemporaryDirectory() as tmp:
            state = Path(tmp) / "shards.json"
            durations = {"test_big": 100.0, "test_a": 10.0, "test_b": 10.0}
            first = tn.assign_shards(["test_big", "test_a", "test_b"], 2, state, durations)
            self.assertNotEqual(first["test_big"], first["test_a"])
            self.assertEqual(first["test_a"], first["test_b"])  # both go beside each other, opposite the big one
            # A new suite does not move existing ones; it goes to the lighter shard.
            second = tn.assign_shards(["test_big", "test_a", "test_b", "test_new"], 2, state, durations)
            for suite in first:
                self.assertEqual(first[suite], second[suite])
            self.assertEqual(first["test_a"], second["test_new"])
            # A different job count re-plans from scratch.
            third = tn.assign_shards(["test_big", "test_a"], 3, state, durations)
            self.assertEqual(3, json.loads(state.read_text())["jobs"])
            self.assertEqual(2, len(set(third.values())))


if __name__ == "__main__":
    unittest.main()
