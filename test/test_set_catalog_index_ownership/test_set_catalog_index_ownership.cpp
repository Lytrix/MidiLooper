//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// I4 packet "Set browser index ownership": one owner per persisted index path.
//
// Cause (see `SET-BROWSER-EMPTY-CATALOG-DIAGNOSIS.md`): `/MidiLooper/sets/index.bin` was written by two
// owners in two layouts -- the 16-byte `SetRevisionCatalog` index and the 8-byte legacy SavedSet
// sequence index -- and the boot reconcile always rewrote it in the legacy layout, so
// `listSetRevisionBrowserEntries` failed its 16-byte read and the browser showed no Sets at all.
//
// This suite pins the invariant over the in-memory card: after the boot catalog order (catalog ensure,
// then the legacy reconcile) the catalog index is readable and truthful, whether the card still carries
// an intact pre-split index or only the Set folders.
#define ARDUINO 1
#include <unity.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "../test_support/firmware/Arduino.h"
#include "../test_support/firmware/SD.h"

SDClass SD;

#include "CurrentSetStorage.h"
#include "PersistenceSchema.h"
#include "SavedSetCatalog.h"
#include "SetRevisionCatalog.h"
#include "StorageLoopIo.h"
#include "StorageManagerInternal.h"
#include "SetBrowserOverlayPolicy.h"

#include "../../src/PersistenceSchema.cpp"
#include "../../src/SavedSetCatalog.cpp"
#include "../../src/SetRevisionCatalog.cpp"
#include "../../src/StorageManager/SavedSetIo.cpp"
#include "../../src/StorageManager/CurrentSetStorage.cpp"
#include "../../src/StorageManager/FileIo.cpp"
// The boot ensure under test (the catalog's own index owner).
#include "../../src/StorageManager/SetCatalogIndexEnsure.cpp"
// The overlay's list-refresh policy (Slice B of the same packet: cache freshness).
#include "../../src/SetBrowserOverlayPolicy.cpp"

namespace {

/// The wall clock the persistence path reads through `RtcTime`.
uint32_t g_wallClock = 1700000000u;

constexpr uint16_t kSetOne = 1;

void writeFileBytes(const char* path, const std::vector<uint8_t>& body) {
  SD.writeFile(path, body);
}

bool writeSetMeta(uint16_t setId, uint16_t latestRevisionId, uint16_t revisionCount) {
  SetRevisionCatalog::SetMetaRecord meta{};
  meta.setId = setId;
  meta.latestRevisionId = latestRevisionId;
  meta.revisionCount = revisionCount;
  meta.createdUnix = 1700000000ull;
  meta.updatedUnix = 1700000100ull;
  char path[64];
  if (!SetRevisionCatalog::formatSetMetaPath(path, sizeof(path), setId)) {
    return false;
  }
  File file = CurrentSetStorage::openForRewrite(path);
  if (!file) {
    return false;
  }
  const StorageIo io = StorageManagerInternal::storageIoFromFileWrite(file);
  const bool ok = SetRevisionCatalog::writeSetMetaRecord(io, meta);
  file.close();
  return ok;
}

bool writeRevisionBody(uint16_t setId, uint16_t revisionId, uint8_t fill) {
  char path[80];
  if (!SetRevisionCatalog::formatRevisionPath(path, sizeof(path), setId, revisionId, false)) {
    return false;
  }
  writeFileBytes(path, std::vector<uint8_t>(32, fill));
  return true;
}

/// The card the diagnosis describes: a Set folder with two revisions, no catalog index yet.
void buildCardWithSetOne() {
  TEST_ASSERT_TRUE(writeSetMeta(kSetOne, 2, 2));
  TEST_ASSERT_TRUE(writeRevisionBody(kSetOne, 1, 0x11));
  TEST_ASSERT_TRUE(writeRevisionBody(kSetOne, 2, 0x22));
}

/// Write a catalog index the way the commit's `CatalogUpdate` stage does: temp file, then rename onto
/// the catalog path. The heavy end-to-end version of this write (the real commit stage) is covered by
/// `test_storage_drain_restore`; this suite stays on the catalog/boot owners alone.
bool writeCatalogIndex(uint32_t nextSetId, uint32_t setCount) {
  SetRevisionCatalog::SetCatalogIndex index{};
  index.nextSetId = nextSetId;
  index.setCount = setCount;
  File temp = CurrentSetStorage::openForRewrite(SetRevisionCatalog::kSetIndexTempPath);
  if (!temp) {
    return false;
  }
  const StorageIo io = StorageManagerInternal::storageIoFromFileWrite(temp);
  const bool written = SetRevisionCatalog::writeSetCatalogIndex(io, index);
  temp.close();
  if (!written) {
    return false;
  }
  return CurrentSetStorage::atomicRenameTempFile(SetRevisionCatalog::kSetIndexTempPath,
                                                 SetRevisionCatalog::kSetIndexPath);
}

bool readCatalogIndexFrom(const char* path, SetRevisionCatalog::SetCatalogIndex& out) {
  if (path == nullptr || !SD.exists(path)) {
    return false;
  }
  File file = SD.open(path, FILE_READ);
  if (!file) {
    return false;
  }
  const StorageIo io = StorageManagerInternal::storageIoFromFileRead(file);
  const bool ok = SetRevisionCatalog::readSetCatalogIndex(io, out);
  file.close();
  return ok;
}

bool readCatalogIndex(SetRevisionCatalog::SetCatalogIndex& out) {
  return readCatalogIndexFrom(SetRevisionCatalog::kSetIndexPath, out);
}

/// A pre-split card: the 16-byte catalog index still sitting at the legacy path (what a commit left
/// there before the path split, and what the next boot used to destroy).
bool writeCatalogIndexAtLegacyPath(uint32_t nextSetId, uint32_t setCount) {
  SetRevisionCatalog::SetCatalogIndex index{};
  index.nextSetId = nextSetId;
  index.setCount = setCount;
  File file = CurrentSetStorage::openForRewrite(CurrentSetStorage::kSetIndexPath);
  if (!file) {
    return false;
  }
  const StorageIo io = StorageManagerInternal::storageIoFromFileWrite(file);
  const bool ok = SetRevisionCatalog::writeSetCatalogIndex(io, index);
  file.close();
  return ok;
}

bool readLegacyIndex(SavedSetCatalog::SetIndex& out) {
  if (!SD.exists(CurrentSetStorage::kSetIndexPath)) {
    return false;
  }
  File file = SD.open(CurrentSetStorage::kSetIndexPath, FILE_READ);
  if (!file) {
    return false;
  }
  const StorageIo io = StorageManagerInternal::storageIoFromFileRead(file);
  const bool ok = SavedSetCatalog::readSetIndex(io, out);
  file.close();
  return ok;
}
/// The boot catalog order, in production order: the catalog ensures its **own** index first, and only
/// then does the legacy SavedSet reconcile repair its separate `sets/index.bin`. Returns the ensure's
/// named outcome so a case can pin which path it took.
StorageManagerInternal::SetCatalogEnsureOutcome runBootCatalogOrder() {
  const StorageManagerInternal::SetCatalogEnsureOutcome outcome =
      StorageManagerInternal::ensureSetCatalogIndexOnSd();
  SavedSetCatalog::SetIndex legacy{};
  (void)StorageManagerInternal::reconcileSetIndexOnSdWithOutcome(legacy);
  return outcome;
}

}  // namespace

void setUp() { SD.reset(); }
void tearDown() {}

namespace RtcTime {
uint32_t getUnixTime() { return g_wallClock; }
void raiseWallClockToAtLeast(uint32_t value) {
  if (value > g_wallClock) g_wallClock = value;
}
}  // namespace RtcTime

/// A1 — a valid 16-byte catalog index written by a commit must still read back after the boot order.
/// Pre-fix this fails: the legacy reconcile rewrites the shared path in its 8-byte layout.
void test_catalog_index_survives_the_boot_catalog_order() {
  buildCardWithSetOne();
  TEST_ASSERT_TRUE(writeCatalogIndex(2, 1));

  runBootCatalogOrder();

  SetRevisionCatalog::SetCatalogIndex read{};
  TEST_ASSERT_TRUE_MESSAGE(readCatalogIndex(read),
                           "the 16-byte catalog index must survive the boot catalog order");
  TEST_ASSERT_EQUAL_UINT32(2, read.nextSetId);
  TEST_ASSERT_EQUAL_UINT32(1, read.setCount);
  // The legacy index keeps its own layout and path, so it is still readable for its own owner.
  SavedSetCatalog::SetIndex legacy{};
  TEST_ASSERT_TRUE(readLegacyIndex(legacy));
}

/// A2 — the observed device card (legacy index present, no catalog index) must come back with a
/// rebuilt, truthful catalog index derived from the validated Set folders.
void test_boot_order_rebuilds_a_clobbered_catalog_index_from_the_set_folders() {
  buildCardWithSetOne();
  SavedSetCatalog::SetIndex legacy{};
  legacy.nextSequence = 1;
  TEST_ASSERT_TRUE(StorageManagerInternal::writeSetIndexToSd(legacy));
  TEST_ASSERT_FALSE(SD.exists(SetRevisionCatalog::kSetIndexPath));

  runBootCatalogOrder();

  SetRevisionCatalog::SetCatalogIndex read{};
  TEST_ASSERT_TRUE_MESSAGE(readCatalogIndex(read),
                           "the boot order must rebuild the catalog index from the Set folders");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, read.nextSetId,
                                   "nextSetId must clear every existing Set folder id");
  TEST_ASSERT_EQUAL_UINT32(1, read.setCount);
}

/// A3 — a pre-split card whose intact 16-byte index still sits at the legacy path is adopted (copied to
/// the catalog path) **before** the reconcile rewrites that file, so the Set stays listed.
void test_boot_order_adopts_an_intact_catalog_index_from_the_legacy_path() {
  buildCardWithSetOne();
  TEST_ASSERT_TRUE(writeCatalogIndexAtLegacyPath(2, 1));

  const auto outcome = runBootCatalogOrder();
  TEST_ASSERT_EQUAL_INT_MESSAGE(
      static_cast<int>(StorageManagerInternal::SetCatalogEnsureOutcome::Adopted),
      static_cast<int>(outcome), "an intact index at the legacy path must be adopted, not rebuilt");
  SetRevisionCatalog::SetCatalogIndex read{};
  TEST_ASSERT_TRUE(readCatalogIndex(read));
  TEST_ASSERT_EQUAL_UINT32(2, read.nextSetId);
  TEST_ASSERT_EQUAL_UINT32(1, read.setCount);
  // The legacy path is the reconcile's own again, and it may rewrite it in its 8-byte layout.
  SavedSetCatalog::SetIndex legacy{};
  TEST_ASSERT_TRUE(readLegacyIndex(legacy));
}

/// A5a — leftover temp files from either owner do not disturb the rebuild (the writer removes its own
/// temp before creating it).
void test_rebuild_ignores_stale_temp_files_from_either_owner() {
  buildCardWithSetOne();
  SD.writeFile(SetRevisionCatalog::kSetIndexTempPath, std::vector<uint8_t>{0x01, 0x02});
  SD.writeFile(CurrentSetStorage::kSetIndexTempPath, std::vector<uint8_t>{0x03, 0x04});

  const auto outcome = runBootCatalogOrder();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StorageManagerInternal::SetCatalogEnsureOutcome::Rebuilt),
                        static_cast<int>(outcome));
  SetRevisionCatalog::SetCatalogIndex read{};
  TEST_ASSERT_TRUE(readCatalogIndex(read));
  TEST_ASSERT_EQUAL_UINT32(2, read.nextSetId);
  TEST_ASSERT_EQUAL_UINT32(1, read.setCount);
}

/// A5b — when the adoption write cannot land, the ensure reports `Failed` and writes nothing: no half
/// file at the catalog path, and the legacy index is still there for the next boot.
void test_failed_adoption_reports_failed_and_leaves_the_card_alone() {
  buildCardWithSetOne();
  TEST_ASSERT_TRUE(writeCatalogIndexAtLegacyPath(2, 1));
  nativeSdRenameBudget = 0;
  const auto outcome = StorageManagerInternal::ensureSetCatalogIndexOnSd();
  nativeSdRenameBudget = -1;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StorageManagerInternal::SetCatalogEnsureOutcome::Failed),
                        static_cast<int>(outcome));
  TEST_ASSERT_FALSE(SD.exists(SetRevisionCatalog::kSetIndexPath));
  SetRevisionCatalog::SetCatalogIndex stillThere{};
  TEST_ASSERT_TRUE_MESSAGE(readCatalogIndexFrom(CurrentSetStorage::kSetIndexPath, stillThere),
                           "a failed adoption must leave the source index intact");
  TEST_ASSERT_EQUAL_UINT32(2, stillThere.nextSetId);
}

/// A6a — the strict folder-name parse: a suffixed or malformed name can never contribute an id.
void test_set_folder_basename_parse_is_strict() {
  uint16_t id = 0;
  TEST_ASSERT_TRUE(SetRevisionCatalog::parseSetFolderBasename("S0001", id));
  TEST_ASSERT_EQUAL_UINT16(1, id);
  TEST_ASSERT_TRUE(SetRevisionCatalog::parseSetFolderBasename("/MidiLooper/sets/S0012", id));
  TEST_ASSERT_EQUAL_UINT16(12, id);
  TEST_ASSERT_TRUE(SetRevisionCatalog::parseSetFolderBasename("S65535", id));
  TEST_ASSERT_EQUAL_UINT16(65535, id);
  TEST_ASSERT_FALSE(SetRevisionCatalog::parseSetFolderBasename("S0001_backup", id));
  TEST_ASSERT_FALSE(SetRevisionCatalog::parseSetFolderBasename("S0000", id));
  TEST_ASSERT_FALSE(SetRevisionCatalog::parseSetFolderBasename("S", id));
  TEST_ASSERT_FALSE(SetRevisionCatalog::parseSetFolderBasename("S99999", id));
  TEST_ASSERT_FALSE(SetRevisionCatalog::parseSetFolderBasename("index.bin", id));
  TEST_ASSERT_FALSE(SetRevisionCatalog::parseSetFolderBasename(nullptr, id));
}

/// A6b — the scan is validated and bounded in what it accepts: a suffixed folder and a garbage meta
/// produce no row, while `nextSetId` still clears every strictly-parsed folder id.
void test_rebuild_skips_invalid_folders_but_clears_every_parsed_id() {
  buildCardWithSetOne();
  TEST_ASSERT_TRUE(writeSetMeta(3, 1, 1));
  SD.writeFile("/MidiLooper/sets/S0001_backup/set.bin", std::vector<uint8_t>(92, 0));
  SD.writeFile("/MidiLooper/sets/S0005/set.bin", std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF});

  const auto outcome = StorageManagerInternal::ensureSetCatalogIndexOnSd();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StorageManagerInternal::SetCatalogEnsureOutcome::Rebuilt),
                        static_cast<int>(outcome));
  SetRevisionCatalog::SetCatalogIndex read{};
  TEST_ASSERT_TRUE(readCatalogIndex(read));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(6, read.nextSetId,
                                   "S0005 has an invalid meta, but its id must never be reallocated");
  TEST_ASSERT_EQUAL_UINT32(2, read.setCount);
}

/// A7 — idempotent: an already-valid catalog index is `Ok` and costs no SD write at all.
void test_ensure_is_idempotent_and_never_writes_when_valid() {
  buildCardWithSetOne();
  TEST_ASSERT_TRUE(writeCatalogIndex(2, 1));
  nativeSdResetWriteObservation();

  TEST_ASSERT_EQUAL_INT(static_cast<int>(StorageManagerInternal::SetCatalogEnsureOutcome::Ok),
                        static_cast<int>(StorageManagerInternal::ensureSetCatalogIndexOnSd()));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      0, static_cast<uint32_t>(nativeSdTotalWrittenBytes),
      "a valid catalog index must not be rewritten on boot");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StorageManagerInternal::SetCatalogEnsureOutcome::Ok),
                        static_cast<int>(StorageManagerInternal::ensureSetCatalogIndexOnSd()));
  SetRevisionCatalog::SetCatalogIndex read{};
  TEST_ASSERT_TRUE(readCatalogIndex(read));
  TEST_ASSERT_EQUAL_UINT32(2, read.nextSetId);
}

/// B1–B4 — the bounded Set-list refresh decision: a refresh the catalog gate skipped, or one that failed to
/// read the index, is retried under a budget and a minimum interval; a completed read (even an empty
/// catalog) caches as valid; and a catalog change re-arms the budget through the caller's reset.
void test_list_refresh_is_bounded_and_distinguishes_failure_from_empty() {
  SetBrowserOverlayPolicy::ListRefreshState state{};
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(SetBrowserOverlayPolicy::ListRefreshAction::Idle),
      static_cast<int>(SetBrowserOverlayPolicy::resolveListRefreshAction(false, false, true, state, 1000)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(SetBrowserOverlayPolicy::ListRefreshAction::Idle),
      static_cast<int>(SetBrowserOverlayPolicy::resolveListRefreshAction(true, true, true, state, 1000)));
  // A closed catalog gate waits, and records no attempt.
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(SetBrowserOverlayPolicy::ListRefreshAction::WaitForGate),
      static_cast<int>(SetBrowserOverlayPolicy::resolveListRefreshAction(true, false, false, state, 1000)));
  TEST_ASSERT_EQUAL_UINT8(0, state.failedAttempts);
  // Gate open, first attempt.
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(SetBrowserOverlayPolicy::ListRefreshAction::Refresh),
      static_cast<int>(SetBrowserOverlayPolicy::resolveListRefreshAction(true, false, true, state, 1000)));

  // A failed read consumes budget and spaces the retries instead of reading every frame.
  SetBrowserOverlayPolicy::applyListRefreshResult(state, false, 1000);
  TEST_ASSERT_EQUAL_UINT8(1, state.failedAttempts);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(SetBrowserOverlayPolicy::ListRefreshAction::Idle),
      static_cast<int>(SetBrowserOverlayPolicy::resolveListRefreshAction(true, false, true, state, 1100)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(SetBrowserOverlayPolicy::ListRefreshAction::Refresh),
      static_cast<int>(SetBrowserOverlayPolicy::resolveListRefreshAction(true, false, true, state, 1300)));
  SetBrowserOverlayPolicy::applyListRefreshResult(state, false, 1300);
  SetBrowserOverlayPolicy::applyListRefreshResult(state, false, 1600);
  SetBrowserOverlayPolicy::applyListRefreshResult(state, false, 1900);
  TEST_ASSERT_EQUAL_UINT8(SetBrowserOverlayPolicy::kListRefreshMaxFailedAttempts, state.failedAttempts);
  TEST_ASSERT_EQUAL_INT_MESSAGE(
      static_cast<int>(SetBrowserOverlayPolicy::ListRefreshAction::GiveUp),
      static_cast<int>(SetBrowserOverlayPolicy::resolveListRefreshAction(true, false, true, state, 2600)),
      "an unreadable catalog must stop being retried");

  // A completed read resets the budget: the caller then leaves the cache valid (an empty catalog included).
  SetBrowserOverlayPolicy::applyListRefreshResult(state, true, 2700);
  TEST_ASSERT_EQUAL_UINT8(0, state.failedAttempts);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(SetBrowserOverlayPolicy::ListRefreshAction::Idle),
      static_cast<int>(SetBrowserOverlayPolicy::resolveListRefreshAction(true, false, true, state, 2700)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(SetBrowserOverlayPolicy::ListRefreshAction::Refresh),
      static_cast<int>(SetBrowserOverlayPolicy::resolveListRefreshAction(true, false, true, state, 3000)));

  // What the caller does on a catalog publication: re-arm and re-read on the next frame.
  state = SetBrowserOverlayPolicy::ListRefreshState{};
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(SetBrowserOverlayPolicy::ListRefreshAction::Refresh),
      static_cast<int>(SetBrowserOverlayPolicy::resolveListRefreshAction(true, false, true, state, 5000)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_catalog_index_survives_the_boot_catalog_order);
  RUN_TEST(test_boot_order_rebuilds_a_clobbered_catalog_index_from_the_set_folders);
  RUN_TEST(test_boot_order_adopts_an_intact_catalog_index_from_the_legacy_path);
  RUN_TEST(test_rebuild_ignores_stale_temp_files_from_either_owner);
  RUN_TEST(test_failed_adoption_reports_failed_and_leaves_the_card_alone);
  RUN_TEST(test_set_folder_basename_parse_is_strict);
  RUN_TEST(test_rebuild_skips_invalid_folders_but_clears_every_parsed_id);
  RUN_TEST(test_ensure_is_idempotent_and_never_writes_when_valid);
  RUN_TEST(test_list_refresh_is_bounded_and_distinguishes_failure_from_empty);
  return UNITY_END();
}
