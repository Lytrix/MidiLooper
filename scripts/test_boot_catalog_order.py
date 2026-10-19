#!/usr/bin/env python3
"""I4 — Set browser index ownership: the two index owners keep their own paths and their boot order.

The native fixtures exercise the catalog/boot/commit owners directly. What they cannot compose is the
**order** inside `StorageManager::loadState` (ensure the catalog index, *then* let the legacy SavedSet
reconcile repair its separate file) and the placement of the commit guard before the allocation. Both are
one-line invariants whose only failure mode is silent (an intact pre-split index overwritten before it can
be adopted, or a Set id reallocated over an existing Set), so they are pinned here against the sources.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parent.parent
_BOOT_RECOVERY = _REPO_ROOT / "src/StorageManager/BootRecovery.cpp"
_REVISION_COMMIT = _REPO_ROOT / "src/StorageManager/RevisionCommit.cpp"
_CATALOG_HEADER = _REPO_ROOT / "include/SetRevisionCatalog.h"
_LEGACY_HEADER = _REPO_ROOT / "include/CurrentSetStorage.h"

_ENSURE_CALL_RE = re.compile(r"\bensureSetCatalogIndexOnSd\s*\(\s*\)")
_RECONCILE_CALL_RE = re.compile(r"\breconcileSetIndexOnSdWithOutcome\s*\(")
_ALLOCATE_CALL_RE = re.compile(r"\ballocateNextSetId\s*\(")
_CATALOG_PATH_RE = re.compile(r'kSetIndexPath\s*=\s*"([^"]+)"')
_CATALOG_TEMP_RE = re.compile(r'kSetIndexTempPath\s*=\s*"([^"]+)"')


class BootCatalogOrderGateTests(unittest.TestCase):
    def test_catalog_ensure_runs_before_the_legacy_reconcile_at_boot(self) -> None:
        text = _BOOT_RECOVERY.read_text(encoding="utf-8", errors="replace")
        ensure = _ENSURE_CALL_RE.search(text)
        reconcile = _RECONCILE_CALL_RE.search(text)
        self.assertIsNotNone(ensure, "the boot path must ensure the catalog index")
        self.assertIsNotNone(reconcile, "the boot path must still reconcile the legacy index")
        self.assertLess(
            ensure.start(),
            reconcile.start(),
            "the catalog index must be ensured before the legacy reconcile rewrites its own file",
        )

    def test_the_commit_ensures_the_catalog_index_before_allocating_a_set_id(self) -> None:
        text = _REVISION_COMMIT.read_text(encoding="utf-8", errors="replace")
        ensure = _ENSURE_CALL_RE.search(text)
        allocate = _ALLOCATE_CALL_RE.search(text)
        self.assertIsNotNone(ensure, "the commit must be able to establish the catalog index")
        self.assertIsNotNone(allocate, "the commit still allocates Set ids")
        self.assertLess(
            ensure.start(),
            allocate.start(),
            "an unreadable index must be repaired before a Set id is handed out",
        )

    def test_the_two_index_owners_do_not_share_a_path_or_a_temp_path(self) -> None:
        catalog = _CATALOG_HEADER.read_text(encoding="utf-8", errors="replace")
        legacy = _LEGACY_HEADER.read_text(encoding="utf-8", errors="replace")
        catalog_path = _CATALOG_PATH_RE.search(catalog)
        catalog_temp = _CATALOG_TEMP_RE.search(catalog)
        legacy_path = _CATALOG_PATH_RE.search(legacy)
        legacy_temp = _CATALOG_TEMP_RE.search(legacy)
        for name, match in (
            ("catalog index path", catalog_path),
            ("catalog temp path", catalog_temp),
            ("legacy index path", legacy_path),
            ("legacy temp path", legacy_temp),
        ):
            self.assertIsNotNone(match, f"{name} must be declared")
        self.assertNotEqual(catalog_path.group(1), legacy_path.group(1))
        self.assertNotEqual(catalog_temp.group(1), legacy_temp.group(1))


if __name__ == "__main__":
    unittest.main()
