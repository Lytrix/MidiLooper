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

/// The sketch owns this global; the load path reads it, so the harness provides it.
bool sequencerRunning = false;

// The shared production harness (Loop/Track/TrackManager objects + harness globals). Extracted so this
// suite links the same production code as test_continuous_capture without duplicating the include list.
// This suite links src/TrackUndo.cpp, so it omits MidiButtonActions.cpp -- see the seam in the include.
// This suite links src/TrackUndo.cpp, so it omits the note-edit button TU (see the seam in the include).
#define HARNESS_OMITS_MIDI_BUTTON_ACTIONS
#include "../test_support/track_harness.inc"
// The stub surface: physical outputs and unrelated domain routes stay fail-closed or observable.
// This suite links the real load path, which defines the two loop-slot-payload functions, so it opts
// out of those stubs.
#include "load_harness_stubs.h"
#include "../../src/TrackUndo.cpp"

// TrackManager's constructor references the global MIDI handler; the load path pulls it in.
#include "../../src/MidiHandler.cpp"
#include "../../src/StorageManager/CurrentSetStorage.cpp"
#include "../../src/StorageManager/FileIo.cpp"
#include "../../src/StorageManager/SlotReset.cpp"
#include "../../src/StorageManager/LoadLoopBudget.cpp"
#include "../../src/StorageLoopIo.cpp"
#include "../../src/StorageLoopIoBodyFormatV1.cpp"
#include "../../src/StorageManager/CurrentWorkspaceStorage.cpp"
#include "../../src/StorageManager/LoadLoopSelectionPolicy.cpp"
#include "../../src/StorageManager/SlotLoadAdmission.cpp"
#include "../../src/StorageManager/SlotLoadSession.cpp"
#include "../../src/StorageManager/LoadLoopJob.cpp"
#include "../../src/Loop/LoopPendingNoteChange.cpp"
#include "../../src/TrackManager/TrackManagerSlotMatrix.cpp"
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

/// Commit one record pass into a slot, so the slot holds content -- the state that makes
/// `needsSlotLoad` false, exactly as a finalizing predecessor's slot does.
static void commitRecordPassIntoSlot(Track& track, uint8_t slot) {
  track.ensureLoopsAllocated();
  Loop& loop = track.getLoop(slot);
  loop.loopLengthTicks = 768;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(0, 1, 48, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(48, 1, 48, 0)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(768, CommitReason::RecordStop));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass());
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
}

/// S2b.5/B04, defect mechanism. `LoadLoopJob.cpp`'s callers **pop the restore entry before** calling
/// `beginLoadLoopJob`, and re-queueing is **inert** for a slot that has committed passes, because
/// `needsSlotLoad` refuses it. The pop therefore loses the request outright -- which is why the fix must
/// peek/pre-check before the pop, or use the forced enqueue.
void test_b04_pop_then_requeue_loses_the_request_for_a_committed_slot() {
  Track& track = trackManager.getSelectedTrack();
  commitRecordPassIntoSlot(track, 0);

  // Re-queueing is inert for a slot with committed content: nothing is enqueued at all.
  StorageManagerInternal::queueDeferredLoopSlotRestore(0, 0);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, StorageManagerInternal::pendingLoopSlotRestores_.count,
                                   "queueDeferredLoopSlotRestore must refuse a committed slot");

  // Seed the request the way the forced path can, then do what the caller does today: pop first.
  TEST_ASSERT_TRUE(StorageManagerInternal::appendBootLoopSlotRestore(0, 0, 1));
  StorageManagerInternal::DeferredLoopSlotRestore popped{};
  // Stronger than a lost request: for a slot with committed passes the pop's done-test treats the
  // request as already satisfied and **silently discards it**, returning false and dropping the entry.
  // A caller that pops first therefore loses the obligation with no signal at all -- and this is exactly
  // the state a finalizing predecessor's slot is in.
  TEST_ASSERT_FALSE_MESSAGE(StorageManagerInternal::popFocusDeferredLoopSlotRestore(0, 0, popped),
                            "the pop discards a committed slot's request as already done");
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, StorageManagerInternal::pendingLoopSlotRestores_.count,
                                   "the discarded request has left the queue");

  // The request is gone and cannot be put back: the queued obligation was destroyed by the pop.
  StorageManagerInternal::queueDeferredLoopSlotRestore(0, 0);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(
      0, StorageManagerInternal::pendingLoopSlotRestores_.count,
      "a popped request for a committed slot cannot be re-queued -- the caller must not pop first");

  // The content the live owner depends on is still intact, so the loss here is the *request*; the slot's
  // destruction follows later, when the loader runs for a slot it was told to load.
  TEST_ASSERT_TRUE(track.getLoop(0).hasCommittedPasses());
}

/// `Track` names this struct as a friend, so a suite can reach the private admission surface. Only the
/// helpers this suite needs are defined; `test_continuous_capture` defines its own set.
struct TrackCaptureStopTestAccess {
  static bool admit(Track& track, PendingStopDestination destination, uint32_t tick) {
    return track.beginDeferredOverdubStop(destination, tick);
  }
  static bool step(Track& track) { return track.processDeferredOverdubStopGrain(); }
};

/// B04 (S2b.5), the real case: a **live finalization owner** on the slot whose restore is queued. The pop is
/// a one-way door -- it discards an entry whose slot it judges done and the queue will not take it back -- so
/// the production selection entry must not pop before it has checked liveness.
void test_b04_real_load_selection_keeps_the_request_while_the_slot_is_owned() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  commitRecordPassIntoSlot(track, 0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  track.forceSetState(TRACK_PLAYING);

  // A live finalization owner, built through the production admission entry. The MIDI button route is
  // deliberately avoided: it would drag the note-edit input layer into this suite.
  clockManager.assignCurrentTickSilently(96);
  Loop& loop = track.getLoop(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  track.forceSetState(TRACK_OVERDUBBING);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(96, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(120, 1, 60, 0)));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::admit(track, PendingStopDestination::Playing, 96));
  TEST_ASSERT_TRUE_MESSAGE(track.hasPendingDeferredOverdubStop(),
                           "the fixture needs a live finalization owner on slot 0");
  TEST_ASSERT_FALSE(track.noLiveOwnerForSlot(0));

  // Queue a restore for the owned slot through the forced path, then run the real selection entry.
  TEST_ASSERT_TRUE(StorageManagerInternal::appendBootLoopSlotRestore(0, 0, 1));
  (void)StorageManager::selectSubmittedLoadJobs();

  TEST_ASSERT_EQUAL_UINT16_MESSAGE(
      1, StorageManagerInternal::pendingLoopSlotRestores_.count,
      "a queued restore for a slot with a live finalization owner must stay queued");
  TEST_ASSERT_TRUE_MESSAGE(track.getLoop(0).hasCommittedPasses(),
                           "the load must not have reset the slot out from under its owner");

}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_restore_queue_orders_by_priority_and_peek_returns_the_head);
  RUN_TEST(test_restore_queue_peek_has_no_done_test_for_an_attempted_slot);
  RUN_TEST(test_restore_queue_forced_enqueue_bypasses_the_needs_slot_load_guard);
  RUN_TEST(test_b04_pop_then_requeue_loses_the_request_for_a_committed_slot);
  RUN_TEST(test_b04_real_load_selection_keeps_the_request_while_the_slot_is_owned);
  return UNITY_END();
}
