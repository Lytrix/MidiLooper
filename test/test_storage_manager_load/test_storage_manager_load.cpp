//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Shared native harness for the StorageManager load path (S2b.5 / B04, and later S2b.4's mid-copy
// protection and B06's non-focus dispatch leg). It links the real load queue, selection, admission and
// reset paths against an in-memory SD card -- see test/test_support/firmware/SD.h.
// `CurrentSetStorage`'s SD-dependent declarations (including `verifySaveFileTokenAtPath`) sit behind
// `#if defined(ARDUINO)`. Not defining it is exactly why this path has never been natively testable, so
// this harness declares it and supplies the hardware boundaries as the native SD stub.
#define ARDUINO 1

#include <unity.h>

#include "../test_support/firmware/Arduino.h"
#include "../test_support/firmware/SD.h"

SDClass SD;

// The shared production harness (Loop/Track/TrackManager objects + harness globals). Extracted so this
// suite links the same production code as test_continuous_capture without duplicating the include list.
#include "../test_support/track_harness.inc"
// The stub surface: physical outputs and unrelated domain routes stay fail-closed or observable.
// This suite links the real load path, which defines the two loop-slot-payload functions, so it opts
// out of those stubs.
#define HARNESS_LINKS_LOOP_SLOT_PAYLOAD_ON_SD
#include "../test_continuous_capture/off_path_deps.h"

// TrackManager's constructor references the global MIDI handler; the load path pulls it in.
#include "../../src/MidiHandler.cpp"
#include "../../src/StorageManager/CurrentSetStorage.cpp"
#include "../../src/StorageManager/SlotLoadAdmission.cpp"
#include "../../src/StorageManager/LoopSlotRestoreQueue.cpp"

void setUp() {
  StorageManagerInternal::clearPendingLoopSlotRestoresAtBoot();
  StorageManagerInternal::resetAllLoopSlotRestoreAttempted();
}
void tearDown() {}

/// The queue's ordering contract: entries come back by ASCENDING restore priority (a lower number is
/// served first), which is what `sortPendingLoopSlotRestoresByPriority`'s insertion sort produces.
void test_restore_queue_orders_by_priority_and_peek_returns_the_head() {
  TEST_ASSERT_TRUE(StorageManagerInternal::appendBootLoopSlotRestore(2, 3, 5));
  TEST_ASSERT_TRUE(StorageManagerInternal::appendBootLoopSlotRestore(4, 1, 1));
  TEST_ASSERT_TRUE(StorageManagerInternal::appendBootLoopSlotRestore(0, 7, 9));
  StorageManagerInternal::sortPendingLoopSlotRestoreQueue();

  StorageManagerInternal::DeferredLoopSlotRestore head{};
  TEST_ASSERT_TRUE(StorageManagerInternal::peekFirstPendingLoopSlotRestore(head));
  TEST_ASSERT_EQUAL_UINT8(4, head.track);
  TEST_ASSERT_EQUAL_UINT8(1, head.slot);
  TEST_ASSERT_EQUAL_UINT16(1, head.restorePriority);
}

/// S2b.5 characterization, and the reason the contract calls the peek helper insufficient:
/// `peekFirstPendingLoopSlotRestore` has **no done-test and no track/slot filter** (unlike
/// `popFocusDeferredLoopSlotRestore`). A caller that pops before checking therefore cannot rely on the
/// peek to tell it the request is already satisfied -- the done-test has to be added at the call site
/// (or a forced enqueue used), which is exactly what S2b.5 must do.
void test_restore_queue_peek_has_no_done_test_for_an_attempted_slot() {
  StorageManagerInternal::loopSlotRestoreAttempted_[2][3] = true;
  TEST_ASSERT_TRUE(StorageManagerInternal::appendBootLoopSlotRestore(2, 3, 5));

  StorageManagerInternal::DeferredLoopSlotRestore head{};
  TEST_ASSERT_TRUE_MESSAGE(
      StorageManagerInternal::peekFirstPendingLoopSlotRestore(head),
      "peek returns an already-attempted slot: the done-test is the caller's responsibility");
  TEST_ASSERT_EQUAL_UINT8(2, head.track);
  TEST_ASSERT_EQUAL_UINT8(3, head.slot);
}

/// The contract's alternative to peek-before-pop already exists: a **forced enqueue** that bypasses
/// `needsSlotLoad`. That matters because a pop-then-requeue is inert for a slot whose restore was
/// already attempted or which has committed passes.
void test_restore_queue_forced_enqueue_bypasses_the_needs_slot_load_guard() {
  StorageManagerInternal::loopSlotRestoreAttempted_[1][1] = true;
  // The forced enqueue takes the request regardless of the attempted flag or committed content.
  TEST_ASSERT_TRUE(StorageManagerInternal::appendBootLoopSlotRestore(1, 1, 3));
  TEST_ASSERT_EQUAL_UINT16(1, StorageManagerInternal::pendingLoopSlotRestores_.count);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_restore_queue_orders_by_priority_and_peek_returns_the_head);
  RUN_TEST(test_restore_queue_peek_has_no_done_test_for_an_attempted_slot);
  RUN_TEST(test_restore_queue_forced_enqueue_bypasses_the_needs_slot_load_guard);
  return UNITY_END();
}
