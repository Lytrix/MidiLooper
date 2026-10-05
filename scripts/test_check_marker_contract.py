#!/usr/bin/env python3
"""Unit tests for scripts/check_marker_contract.py (host-only, no device)."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

import check_marker_contract as cmc

HEADER = '''
inline bool isTierALine(const char* line) {
  const char* tag = tagOf(line);
  if (strncmp(tag, "DIAG,playback_build,hold_empty,", 31) == 0) { return false; }
  // strncmp(tag, "DIAG,commented_out,", 19) == 0
  return strncmp(tag, "ST,", 3) == 0 ||
         strncmp(tag, "DIAG,playback_build,", 20) == 0 ||
         strncmp(tag, "DIAG,this_prefix_is_far_too_long_for_ring,", 40) == 0;
}
'''


class ParseTests(unittest.TestCase):
    def test_parse_splits_accepted_and_excluded_and_skips_comments(self) -> None:
        accepted, excluded = cmc.tier_a_prefixes(HEADER)
        self.assertEqual(excluded, ["DIAG,playback_build,hold_empty,"])
        self.assertIn("ST,", accepted)
        self.assertNotIn("DIAG,commented_out,", accepted)

    def test_real_header_parses_nonempty(self) -> None:
        accepted, _ = cmc.tier_a_prefixes()
        self.assertGreater(len(accepted), 20)
        self.assertIn("RECS,", accepted)

    def test_classify_prefers_exclusion_and_longest_match(self) -> None:
        accepted, excluded = cmc.tier_a_prefixes(HEADER)
        self.assertIsNone(cmc.classify("DIAG,playback_build,hold_empty,x", accepted, excluded))
        self.assertEqual(cmc.classify("DIAG,playback_build,rolling,1", accepted, excluded), "DIAG,playback_build,")
        self.assertIsNone(cmc.classify("DIAG,unknown,1", accepted, excluded))


class ContractTests(unittest.TestCase):
    def test_fit_limit_matches_head_read(self) -> None:
        self.assertEqual(cmc.MAX_PREFIX_LEN, 31)

    def test_too_long_prefix_fails_fit_even_when_classified(self) -> None:
        accepted, excluded = cmc.tier_a_prefixes(HEADER)
        ok, message = cmc.check_tag("DIAG,this_prefix_is_far_too_long_for_ring,1", accepted, excluded)
        self.assertFalse(ok)
        self.assertIn("FIT=no", message)

    def test_unknown_marker_fails_with_both_reasons(self) -> None:
        accepted, excluded = cmc.tier_a_prefixes()
        ok, message = cmc.check_tag("DIAG,zz_no_such_marker_zz,", accepted, excluded)
        self.assertFalse(ok)
        self.assertIn("EMITTED=no", message)
        self.assertIn("TIER-A=no", message)

    def test_known_marker_passes(self) -> None:
        accepted, excluded = cmc.tier_a_prefixes()
        ok, message = cmc.check_tag("DIAG,post_restore_census,", accepted, excluded)
        self.assertTrue(ok, message)

    def test_cli_usage_error_without_arguments(self) -> None:
        self.assertEqual(cmc.main([]), 2)


if __name__ == "__main__":
    unittest.main()
