//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Packet D (A4.1) of `deferred-slot-restore-liveness`: the boot catalog reconcile outcome.
//
// `reconcileSetIndexOnSd` is self-healing since the SD temp-write fix -- an unreadable `index.bin` is
// recoverable metadata, not a failure. What it never reported was *which* of three things happened:
// the index was already consistent (`ok`), it was rebuilt/rewritten (`repaired`), or the repair could
// not be written (`failed`). This suite pins that mapping over the in-memory card.
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
#include "StorageManagerInternal.h"

#include "../../src/PersistenceSchema.cpp"
#include "../../src/SavedSetCatalog.cpp"
#include "../../src/StorageManager/SavedSetIo.cpp"
// The SD boundary the reconcile writes through.
#include "../../src/StorageManager/CurrentSetStorage.cpp"
#include "../../src/StorageManager/FileIo.cpp"

namespace {

/// `RtcTime` reads the Teensy RTC through `core_pins.h` and cannot compile natively; the reconcile path
/// only needs a monotonic wall clock.
uint32_t g_wallClock = 1700000000u;

}  // namespace

namespace RtcTime {
uint32_t getUnixTime() { return g_wallClock; }
void raiseWallClockToAtLeast(uint32_t value) {
  if (value > g_wallClock) g_wallClock = value;
}
}  // namespace RtcTime

void setUp() { SD.reset(); }
void tearDown() {}

/// A healthy, already-consistent index: nothing to repair, so the outcome is `ok`.
void test_reconcile_reports_ok_for_a_consistent_index() {
  SavedSetCatalog::SetIndex written{};
  written.nextSequence = 7;
  TEST_ASSERT_TRUE(StorageManagerInternal::writeSetIndexToSd(written));

  SavedSetCatalog::SetIndex read{};
  const auto outcome = StorageManagerInternal::reconcileSetIndexOnSdWithOutcome(read);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StorageManagerInternal::SetIndexReconcileOutcome::Ok),
                        static_cast<int>(outcome));
  TEST_ASSERT_EQUAL_UINT32(7, read.nextSequence);
}

/// A missing index is recoverable metadata: the reconcile rebuilds it and reports `repaired`, not a
/// failure. This is the distinction the boot log never made.
void test_reconcile_reports_repaired_for_a_missing_index() {
  SavedSetCatalog::SetIndex read{};
  const auto outcome = StorageManagerInternal::reconcileSetIndexOnSdWithOutcome(read);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StorageManagerInternal::SetIndexReconcileOutcome::Repaired),
                        static_cast<int>(outcome));
  TEST_ASSERT_TRUE_MESSAGE(SD.exists(CurrentSetStorage::kSetIndexPath),
                           "a repair must have written the index back");

  // A second reconcile over the repaired file is now `ok`: the repair is durable, not repeated.
  SavedSetCatalog::SetIndex again{};
  const auto second = StorageManagerInternal::reconcileSetIndexOnSdWithOutcome(again);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StorageManagerInternal::SetIndexReconcileOutcome::Ok),
                        static_cast<int>(second));
}

/// A malformed index is repaired the same way, and the repaired file reads back.
void test_reconcile_reports_repaired_for_a_malformed_index() {
  SD.writeFile(CurrentSetStorage::kSetIndexPath, std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF, 0x01});

  SavedSetCatalog::SetIndex read{};
  const auto outcome = StorageManagerInternal::reconcileSetIndexOnSdWithOutcome(read);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StorageManagerInternal::SetIndexReconcileOutcome::Repaired),
                        static_cast<int>(outcome));

  SavedSetCatalog::SetIndex reread{};
  const auto second = StorageManagerInternal::reconcileSetIndexOnSdWithOutcome(reread);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StorageManagerInternal::SetIndexReconcileOutcome::Ok),
                        static_cast<int>(second));
}

/// The compatibility bool stays the `Ok || Repaired` answer, so existing callers are unchanged.
void test_reconcile_bool_wrapper_matches_the_outcome() {
  SavedSetCatalog::SetIndex missing{};
  TEST_ASSERT_TRUE(StorageManagerInternal::reconcileSetIndexOnSd(missing));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StorageManagerInternal::SetIndexReconcileOutcome::Ok),
                        static_cast<int>(
                            StorageManagerInternal::reconcileSetIndexOnSdWithOutcome(missing)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_reconcile_reports_ok_for_a_consistent_index);
  RUN_TEST(test_reconcile_reports_repaired_for_a_missing_index);
  RUN_TEST(test_reconcile_reports_repaired_for_a_malformed_index);
  RUN_TEST(test_reconcile_bool_wrapper_matches_the_outcome);
  return UNITY_END();
}
