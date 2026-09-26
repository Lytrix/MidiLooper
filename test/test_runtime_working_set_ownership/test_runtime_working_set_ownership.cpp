//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// **S3 gate — `runtime-working-set-ownership` acceptance suite.** The stage's unit contracts live in
// `test_working_set_lease` (the vocabulary) and `test_continuous_capture` (the per-owner-kind matrix
// and the selection/no-churn proof); this suite is the stage's acceptance over the **production
// admission paths**, so the gate names one place where the law's observable consequences hold:
//
//   1. boot/load is an admission boundary  -> a committed slot leaves boot with no working set;
//   2. the record arm is a cold admission  -> it establishes the working set and records its roles;
//   3. release is guarded by liveness      -> `noLiveOwnerForSlot` refuses while an owner remains and
//                                             allows once it retires;
//   4. selection is cheap                  -> selection acquires nothing anywhere.
//
// Every assertion has a control, so "nothing happened" cannot pass by accident: the boot case asserts
// the *absence* of a working set while the record arm asserts its presence on the same slot.
//
// The boundary is the continuous-capture harness's measured set (physical outputs and the note-edit
// neighbours fail closed). The stage's own unit suites remain the place for the lease arithmetic; this
// suite deliberately does not restate them.

#define ARDUINO 1

#include <unity.h>

#include <cstdio>

#include "../test_support/firmware/Arduino.h"
#include "../test_support/firmware/SD.h"

SDClass SD;

/// The sketch owns this global; the load path reads it, so the harness provides it.
bool sequencerRunning = false;

#include "../test_support/track_harness.inc"

// The stub surface: physical outputs and unrelated domain routes stay fail-closed or observable.
#include "off_path_deps.h"

// The production symbols this suite's admission routes reach. Kept to the measured closure: the undo
// publication the deferred-stop grain calls, the slot-matrix predicate the boot pass reads, the
// pending-note display apply the overdub span rebuild calls, and the global MIDI handler the
// TrackManager constructor references.
#include "../../src/TrackUndo.cpp"
#include "../../src/TrackManager/TrackManagerSlotMatrix.cpp"
#include "../../src/Loop/LoopPendingNoteChange.cpp"
#include "../../src/MidiHandler.cpp"
#include "../../src/EditManager/ResolveConstrainedGeometry.cpp"
#include "../../src/EditManager/NoteGeometryResolver.cpp"
#include "../../src/EditManager/EditSessionInteraction.cpp"

struct TrackCaptureStopTestAccess {
  static void selectSlot(Track& track, uint8_t slot) { track.activeLoopIndex = slot; }
  static bool runtimeAllocated(const Track& track, uint8_t slot) {
    return track.playbackRuntime.slotIfAllocated(slot) != nullptr;
  }
  static LoopPlaybackRuntime* runtimeIfAllocated(Track& track, uint8_t slot) {
    return track.playbackRuntime.slotIfAllocated(slot);
  }
  static uint8_t workingSetRoles(const Track& track, uint8_t slot) {
    const auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    return rt != nullptr ? static_cast<uint8_t>(rt->workingSetRoles()) : 0;
  }
  static uint16_t workingSetGeneration(const Track& track, uint8_t slot) {
    const auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    return rt != nullptr ? rt->workingSetGeneration() : 0;
  }
  static bool hasLiveCaptureStopJobForSlot(const Track& track, uint8_t slot) {
    return track.hasLiveCaptureStopJobForSlot(slot);
  }
  static bool hasAcceptedInteractionForSlot(const Track& track, uint8_t slot) {
    return track.hasAcceptedInteractionForSlot(slot);
  }
  static bool ownsLiveFinalization(const Track& track, uint8_t slot) {
    const auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    return rt != nullptr && rt->ownsLiveFinalization();
  }
  static bool ownsInFlightPreparedHandover(const Track& track, uint8_t slot) {
    const auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    return rt != nullptr && rt->ownsInFlightPreparedHandover();
  }
  static void setInFlightPreparedHandover(Track& track, uint8_t slot, bool pending) {
    auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    if (rt != nullptr) rt->preparedPairRequestPending = pending;
  }
  static bool admitStopForSlot(Track& track, PendingStopDestination destination, uint32_t tick,
                               uint8_t slot, uint64_t interval) {
    return track.beginDeferredOverdubStopForSlot(destination, tick, slot, interval);
  }
  static bool step(Track& track) { return track.processDeferredOverdubStopGrain(); }
  static void latchAcceptedStopForSlot(Track& track, uint8_t slot) {
    track.latchAcceptedStop(PendingStopDestination::Playing, slot, 0);
  }
  static void consumeAcceptedStop(Track& track) { track.consumeAcceptedStopObligation(); }
  /// S4.1: the slot's open-note ledger. Marking an entry is the observable that a stop must not
  /// sweep off a sibling slot.
  static void markLedgerEntry(Track& track, uint8_t slot, uint8_t note) {
    auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    if (rt != nullptr) rt->ledger.noteOn(1, note, kInvalidNoteId, 0, 100);
  }
  static bool ledgerEntryActive(const Track& track, uint8_t slot, uint8_t note) {
    const auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    return rt != nullptr && rt->ledger.isActive(1, note);
  }
  static void clearAllLedgersForTests(Track& track) { track.playbackRuntime.clearAllLedgers(); }
  /// S4.1: the named per-interval ledger clear the two stop routes now call.
  static void clearStoppingIntervalLedger(Track& track) { track.clearStoppingIntervalLedger(); }
  static void selectSlotIndex(uint8_t trackIndex, uint8_t slot) {
    trackManager.slotStateMachine.setSelectedSlotIndex(trackIndex, slot);
  }
  static uint8_t selectedSlotIndex(uint8_t trackIndex) {
    return trackManager.slotStateMachine.getSelectedSlotIndex(trackIndex);
  }
};

namespace {

/// Commit one record pass into a slot so the slot holds content -- the state a restored library has.
void commitRecordPassIntoTrackSlot(Track& track, uint8_t slot) {
  TrackCaptureStopTestAccess::selectSlot(track, slot);
  Loop& loop = track.getActiveLoop();
  loop.loopLengthTicks = 768;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(0, 1, 48, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(48, 1, 48, 0)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(768, CommitReason::RecordStop));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass());
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
}

constexpr uint8_t kPlaybackWindowBit =
    static_cast<uint8_t>(WorkingSetRole::PlaybackWindow);
constexpr uint8_t kLiveCaptureBit = static_cast<uint8_t>(WorkingSetRole::LiveCapture);

}  // namespace

void setUp() {
  nativeTransportRunning = true;
  g_nativeSilenceTrackCalls = 0;
  trackManager.~TrackManager();
  LoopEventStore::resetPoolForTests();
  new (&trackManager) TrackManager();
}
void tearDown() {}

/// **Gate item 1 + control.** Boot/load is an admission boundary (S2b.1, invariant 34): a committed
/// slot leaves boot with descriptors only -- no working set -- and the *same* slot gains one at the
/// record arm (item 2). Asserting both on one slot is what makes the absence meaningful.
void test_boot_leaves_a_committed_slot_without_a_working_set() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 1);
  TEST_ASSERT_TRUE(track.getLoop(1).hasCommittedPasses());

  // The boot pass: descriptors only, so the slot must not hold the working set...
  track.prewarmBootPlaybackRuntimeSlots();
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, TrackCaptureStopTestAccess::workingSetRoles(track, 1),
                                  "boot must not acquire a working-set lease");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::ownsLiveFinalization(track, 1),
                            "a booted slot must not own a finalization");
  // ...and the same slot must gain it at the cold admission boundary (the record arm).
  TEST_ASSERT_TRUE_MESSAGE(track.prewarmPlaybackForSlot(1, true),
                           "the record arm must admit the working set");
  const uint8_t roles = TrackCaptureStopTestAccess::workingSetRoles(track, 1);
  TEST_ASSERT_TRUE_MESSAGE((roles & kPlaybackWindowBit) != 0,
                           "the record arm must hold the playback-window role");
  TEST_ASSERT_TRUE_MESSAGE((roles & kLiveCaptureBit) != 0,
                           "the record arm must hold the live-capture role");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, 1),
                           "the record arm must admit the slot runtime");
}

/// **Gate item 2, idempotence.** A repeated admission at the same boundary must not churn: the lease
/// is identity-qualified, so re-establishing an already-held set is free and the generation must not
/// move (only a release moves it).
void test_a_repeated_admission_does_not_churn_the_lease() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  const uint8_t roles = TrackCaptureStopTestAccess::workingSetRoles(track, 0);
  const uint16_t generation = TrackCaptureStopTestAccess::workingSetGeneration(track, 0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(roles, TrackCaptureStopTestAccess::workingSetRoles(track, 0),
                                  "a repeated admission must not change the held set");
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(generation,
                                   TrackCaptureStopTestAccess::workingSetGeneration(track, 0),
                                   "a repeated admission must not move the generation");
}

/// **Gate item 3.** Release is guarded by liveness. Each owner kind refuses release on its own, and
/// the slot is releasable once the owner retires -- the control that makes the refusals meaningful.
void test_release_is_refused_while_an_owner_remains_and_allowed_once_it_retires() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 1);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(1, true));

  // Control (a): a working set is not by itself a live owner.
  TEST_ASSERT_TRUE_MESSAGE(track.noLiveOwnerForSlot(1),
                           "an admitted working set must not block release on its own");

  // Owner: an accepted interaction latched for the slot.
  TrackCaptureStopTestAccess::latchAcceptedStopForSlot(track, 1);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 1));
  TEST_ASSERT_FALSE_MESSAGE(track.noLiveOwnerForSlot(1),
                            "an accepted interaction must refuse release");
  TrackCaptureStopTestAccess::consumeAcceptedStop(track);
  TEST_ASSERT_TRUE_MESSAGE(track.noLiveOwnerForSlot(1),
                           "consuming the obligation must free the slot");

  // Owner: a prepared handover still being built (independent of the ring and of finalization).
  TrackCaptureStopTestAccess::setInFlightPreparedHandover(track, 1, true);
  TEST_ASSERT_FALSE_MESSAGE(track.noLiveOwnerForSlot(1),
                            "an in-flight prepared handover must refuse release");
  TrackCaptureStopTestAccess::setInFlightPreparedHandover(track, 1, false);
  TEST_ASSERT_TRUE_MESSAGE(track.noLiveOwnerForSlot(1), "retiring it must free the slot");

  // Owner: a capture stop job admitted for the slot. The interval is opened through the production
  // start (a hand-built interval admits a job that can never finalize), then the stop goes through the
  // production admission entry on the slot the record arm left warm.
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  Loop& loop = track.getLoop(1);
  track.startOverdubbing(96);
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(), "the fixture needs a live interval");
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(96, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(120, 1, 60, 0)));
  TEST_ASSERT_TRUE_MESSAGE(
      TrackCaptureStopTestAccess::admitStopForSlot(track, PendingStopDestination::Playing, 96, 1,
                                                   /*interval=*/1),
      "the production stop admission must accept on the warmed slot");
  TEST_ASSERT_TRUE_MESSAGE(track.hasPendingDeferredOverdubStop(),
                           "the fixture needs a live finalization owner on the slot");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 1),
                           "an admitted stop must appear in the ring's live window");
  TEST_ASSERT_FALSE_MESSAGE(track.noLiveOwnerForSlot(1), "a live stop job must refuse release");
  // Retire it through the production grain drive; the slot must be releasable again.
  for (int turn = 0; turn < 8192 && track.hasPendingDeferredOverdubStop(); ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE_MESSAGE(track.hasPendingDeferredOverdubStop(),
                            "the fixture must retire the stop job");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 1),
                            "the retired job must leave the live window");
  TEST_ASSERT_TRUE_MESSAGE(track.noLiveOwnerForSlot(1), "a retired stop job must free the slot");
}

/// **Gate item 4.** Selection is cheap and acquires nothing (spec: selection requirement). This
/// restates the continuous-capture fixture at the gate's level: the stage's acceptance must fail if
/// selection ever starts admitting.
void test_selection_acquires_no_working_set() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  for (uint8_t slot = 0; slot < 3; ++slot) {
    commitRecordPassIntoTrackSlot(track, slot);
  }
  for (int pass = 0; pass < 3; ++pass) {
    for (uint8_t slot = 0; slot < 3; ++slot) {
      TrackCaptureStopTestAccess::selectSlotIndex(0, slot);
      TEST_ASSERT_EQUAL_UINT8(slot, TrackCaptureStopTestAccess::selectedSlotIndex(0));
    }
  }
  for (uint8_t slot = 0; slot < 3; ++slot) {
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, TrackCaptureStopTestAccess::workingSetRoles(track, slot),
                                    "selection must not acquire a working set");
    TEST_ASSERT_EQUAL_UINT16(0, TrackCaptureStopTestAccess::workingSetGeneration(track, slot));
    TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, slot),
                              "selection must not admit a slot runtime");
  }
}


/// **S4.1 — an interval's stop clears only its own slot's ledger.** The release surface was
/// track-global: while a predecessor finalizes on one slot, a sibling slot's stop swept every
/// allocated slot's ledger, erasing the predecessor's open-note suppression state. This drives the
/// named per-interval clear that both converted stop routes call, and the track-global sweep is the
/// control that proves the conversion narrowed the stop path rather than disabling the sweep.
void test_a_stop_clears_only_its_own_slots_ledger() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  commitRecordPassIntoTrackSlot(track, 1);

  // Both slots hold a working set with a marked ledger entry.
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(1, true));
  TrackCaptureStopTestAccess::markLedgerEntry(track, 0, 60);
  TrackCaptureStopTestAccess::markLedgerEntry(track, 1, 62);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::ledgerEntryActive(track, 0, 60));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::ledgerEntryActive(track, 1, 62));

  // The stopping interval is slot 1. Slot 0 is the sibling that must survive -- the state a
  // finalizing predecessor holds.
  TrackCaptureStopTestAccess::selectSlot(track, 1);
  TrackCaptureStopTestAccess::clearStoppingIntervalLedger(track);

  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::ledgerEntryActive(track, 1, 62),
                            "the stopping interval must clear its own slot's ledger");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::ledgerEntryActive(track, 0, 60),
                           "a sibling slot's ledger must survive the stop");

  // Control: the deliberate whole-track sweep (global transport stop) still clears every slot, so the
  // conversion narrowed the per-interval path rather than weakening the sweep. It drives the same
  // `TrackPlaybackRuntime::clearAllLedgers()` that `Track::sendAllNotesOff` calls, without pulling the
  // MIDI-sending hot path into this boundary.
  TrackCaptureStopTestAccess::clearAllLedgersForTests(track);
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::ledgerEntryActive(track, 0, 60),
                            "the track-global sweep must still clear every slot");
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::ledgerEntryActive(track, 1, 62));
}


int main() {
  UNITY_BEGIN();
  RUN_TEST(test_boot_leaves_a_committed_slot_without_a_working_set);
  RUN_TEST(test_a_repeated_admission_does_not_churn_the_lease);
  RUN_TEST(test_release_is_refused_while_an_owner_remains_and_allowed_once_it_retires);
  RUN_TEST(test_a_stop_clears_only_its_own_slots_ledger);
  RUN_TEST(test_selection_acquires_no_working_set);
  return UNITY_END();
}
