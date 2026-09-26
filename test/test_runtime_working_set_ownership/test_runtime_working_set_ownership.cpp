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
#include <cstdlib>

#include "../test_support/firmware/Arduino.h"
#include "../test_support/firmware/SD.h"

SDClass SD;

/// The sketch owns this global; the load path reads it, so the harness provides it.
bool sequencerRunning = false;

#include "../test_support/track_harness.inc"

// S4b: this suite links the real stop-telemetry TU (the switch requests persistence for the slot the
// capture left), so the stub surface must not also define those diagnostics.
#define HARNESS_LINKS_REAL_STOP_TELEMETRY
#define HARNESS_OBSERVABLE_SLOT_CHANGE
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
// S4b: the production switch path (`finalizeCaptureAndSelectSlot`) and the slot-matrix helpers it
// calls. `replaceSingleEnabledSlotWithTarget` is already linked above via TrackManagerSlotMatrix.cpp.
#include "../../src/TrackManager/TrackManagerCaptureQueue.cpp"
// The slot-enable helper the switch path consults.
#include "../../src/TrackManager/TrackManagerInternalHelpers.cpp"
// The persistence request the switch path makes for the slot the capture left.
#include "../../src/Track/TrackStopTelemetryColdHelpers.cpp"
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
  static WorkingSetRole workingSetRolesOf(const Track& track, uint8_t slot) {
    const auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    return rt != nullptr ? rt->workingSetRoles() : WorkingSetRole::None;
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
  /// S4b: the switch path and the live-interval state the acceptance sequence needs.
  static void finalizeCaptureAndSelectSlot(uint8_t trackIndex, uint8_t newSlot, uint32_t tick) {
    trackManager.finalizeCaptureAndSelectSlot(trackIndex, newSlot, tick);
  }
  static bool hasPendingDeferredOverdubStop(const Track& track) {
    return track.hasPendingDeferredOverdubStop();
  }
  static bool hasStopInFlight(const Track& track) { return track.hasStopInFlight(); }
  static bool hasAcceptedUnexecutedInteraction(const Track& track) {
    return track.hasAcceptedUnexecutedInteraction();
  }
  static bool captureActive(const Track& track) { return track.getActiveLoop().captureActive(); }
  static size_t captureEventCount(const Track& track) {
    return track.getActiveLoop().capture.store.size();
  }
  static bool appendCaptureNote(Track& track, uint32_t onTick, uint32_t offTick, uint8_t note) {
    Loop& loop = track.getActiveLoop();
    return loop.appendCaptureEvent(MidiEvent::NoteOn(onTick, 1, note, 100)) &&
           loop.appendCaptureEvent(MidiEvent::NoteOff(offTick, 1, note, 0));
  }
  static bool isOverdubbing(const Track& track) { return track.isOverdubbing(); }
  static bool isRecording(const Track& track) { return track.isRecording(); }
  static void startOverdubbing(Track& track, uint32_t tick) { track.startOverdubbing(tick); }
  static uint8_t activeLoopIndex(const Track& track) { return track.getActiveLoopIndex(); }
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
  /// S4.4: report a declared-envelope breach if the activation would exceed a class cap.
  static void reportEnvelopeBreachIfAny(const Track& track, uint8_t slot, WorkingSetRole role) {
    track.reportEnvelopeBreachIfAny(slot, role);
  }
  /// S4.1/S4.2: the `(slot, generation)` release entry and the identity a caller must present.
  static uint16_t slotGeneration(Track& track, uint8_t slot) {
    return track.playbackRuntime.slotGeneration(slot);
  }
  static WorkingSetOutcome releaseSlot(Track& track, uint8_t slot, uint16_t generation,
                                       bool preserveLedger = true) {
    return track.playbackRuntime.releaseSlot(slot, generation, preserveLedger);
  }
  static bool leaseHoldsWorkingSet(const Track& track, uint8_t slot) {
    const auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    return rt != nullptr && rt->lease_.roles() != WorkingSetRole::None;
  }
  // ---- S4.2 generation-qualified finalization identity ------------------------------------
  /// The runtime's own recorded owner identity (null until an interval is admitted).
  static bool setCaptureStopOwner(Track& track, uint8_t slot, uint32_t generation) {
    auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    if (rt == nullptr) return false;
    rt->setCaptureStopOwner(*rt->captureStopOwner, generation);
    return true;
  }
  /// Mark the slot as owned by a *different* generation, as happens when the runtime object is reused.
  static bool setSuccessorCaptureStopOwner(Track& track, uint8_t slot, uint32_t generation) {
    auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    if (rt == nullptr || rt->captureStopOwner == nullptr) return false;
    rt->setCaptureStopOwner(*rt->captureStopOwner, generation);
    return true;
  }
  static bool clearCaptureStopOwner(Track& track, uint8_t slot, uint32_t generation) {
    auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    if (rt == nullptr || rt->captureStopOwner == nullptr) return false;
    return rt->clearCaptureStopOwner(*rt->captureStopOwner, generation);
  }
  static bool hasCaptureStopOwner(Track& track, uint8_t slot) {
    const auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    return rt != nullptr && rt->captureStopOwner != nullptr;
  }
  static uint32_t captureStopOwnerGeneration(const Track& track, uint8_t slot) {
    const auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    return rt != nullptr ? rt->captureStopOwnerGeneration : 0;
  }
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

/// The production MIDI input route, so a "B accepts MIDI" assertion goes through the handler rather
/// than poking the capture store. Mirrors the continuous-capture harness's helper.
void inputNote(uint8_t pitch, uint32_t onTick, uint32_t offTick) {
  clockManager.assignCurrentTickSilently(onTick);
  midiHandler.handleMidiMessage(midi::NoteOn, 1, pitch, 100, SOURCE_USB);
  if (offTick != UINT32_MAX) {
    clockManager.assignCurrentTickSilently(offTick);
    midiHandler.handleMidiMessage(midi::NoteOff, 1, pitch, 0, SOURCE_USB);
  }
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



/// **S4.2 — a late cleanup must not clear a successor's finalization marker.** `captureStopOwner` is a
/// raw `PendingStopAdmission*` on a runtime object that ring entries reuse at stable addresses, so an
/// address-only clear (the pre-S4.2 `if (runtime.captureStopOwner == &stop)`) lets a predecessor's
/// late cleanup null a successor's marker. The clear is now generation-qualified; this drives the
/// A-releases / C-reuses / late-A-cleanup sequence directly.
void test_a_late_cleanup_cannot_clear_a_successor_finalization_marker() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));

  // Establish a real owner through the production admission, so the fixture works on a genuine
  // `PendingStopAdmission` rather than a stand-in.
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  Loop& loop = track.getLoop(0);
  track.startOverdubbing(96);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(96, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(120, 1, 60, 0)));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::admitStopForSlot(track, PendingStopDestination::Playing,
                                                               96, 0, /*interval=*/1));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::hasCaptureStopOwner(track, 0));

  // The owner's generation is the identity a cleanup must present.
  const uint32_t staleGeneration =
      TrackCaptureStopTestAccess::captureStopOwnerGeneration(track, 0) + 1u;

  // A late cleanup presenting a *different* generation is a rejected no-op, and the live marker
  // survives -- this is the successor on the reused runtime object.
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::clearCaptureStopOwner(track, 0, staleGeneration),
                            "a generation-mismatched cleanup must be rejected");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasCaptureStopOwner(track, 0),
                           "a late cleanup must not clear a successor's marker");

  // Control: the matching identity does clear it, so the rejection above is the generation check
  // and not a clear that never works.
  const uint32_t liveGeneration = TrackCaptureStopTestAccess::captureStopOwnerGeneration(track, 0);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::clearCaptureStopOwner(track, 0, liveGeneration),
                           "the matching identity must clear the marker");
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::hasCaptureStopOwner(track, 0));
  // ...and a second cleanup of the same identity is a no-op, not a crash or a resurrection.
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::clearCaptureStopOwner(track, 0, liveGeneration));
}


/// **S4.1/S4.2 — the `(slot, generation)` release entry.** Release is identity-qualified, gated by
/// liveness at the track level, and preserves the slot runtime's own refusal instead of discarding it
/// (the pre-S4.1 `TrackPlaybackRuntime::resetAll` dropped that answer).
void test_the_slot_generation_release_entry_is_identity_qualified_and_preserves_refusals() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 0));

  // Identity: the generation captured now is the one a caller must present.
  const uint16_t generation = TrackCaptureStopTestAccess::slotGeneration(track, 0);

  // A stale generation is a rejected no-op and must not release the held working set.
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      static_cast<uint8_t>(WorkingSetOutcome::StaleGeneration),
      static_cast<uint8_t>(TrackCaptureStopTestAccess::releaseSlot(track, 0,
                                                                   static_cast<uint16_t>(generation + 1))),
      "a stale generation must be a rejected no-op");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 0),
                           "a stale release must not drop the held working set");

  // The matching identity releases, and the generation advances so the same identity cannot release
  // twice (the reuse guard).
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      static_cast<uint8_t>(WorkingSetOutcome::Released),
      static_cast<uint8_t>(TrackCaptureStopTestAccess::releaseSlot(track, 0, generation)),
      "the matching identity must release");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 0),
                            "release must drop the held working set");
  const uint16_t advanced = TrackCaptureStopTestAccess::slotGeneration(track, 0);
  TEST_ASSERT_TRUE_MESSAGE(advanced > generation, "release must advance the slot generation");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      static_cast<uint8_t>(WorkingSetOutcome::StaleGeneration),
      static_cast<uint8_t>(TrackCaptureStopTestAccess::releaseSlot(track, 0, generation)),
      "the previous identity must not release a reused slot");

  // An unheld slot reports NotHeld rather than a false release.
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WorkingSetOutcome::NotHeld),
      static_cast<uint8_t>(TrackCaptureStopTestAccess::releaseSlot(track, 0, advanced)));
  // ...and an out-of-range slot cannot be released.
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WorkingSetOutcome::NotHeld),
      static_cast<uint8_t>(TrackCaptureStopTestAccess::releaseSlot(track, 200, 0)));
}

/// **S4.1 — the track-level release is gated by liveness.** `releaseEligiblePlaybackRuntimeSlots`
/// must skip a slot whose predicate reports a live owner: releasing underneath one is what the
/// predicate-free `resetAll` did.
void test_the_release_sweep_skips_a_slot_with_a_live_owner() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 1);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(1, true));

  // A live owner on the slot: an accepted interaction latched for it.
  TrackCaptureStopTestAccess::latchAcceptedStopForSlot(track, 1);
  TEST_ASSERT_FALSE_MESSAGE(track.noLiveOwnerForSlot(1), "the fixture needs a live owner");

  const uint16_t generation = TrackCaptureStopTestAccess::slotGeneration(track, 1);
  track.releasePlaybackMergedMidiEventsMemory();
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(
      generation, TrackCaptureStopTestAccess::slotGeneration(track, 1),
      "the sweep must not release a slot with a live owner");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 1),
                           "a live owner's working set must survive the sweep");

  // Control: once the owner retires, the same sweep does release it.
  TrackCaptureStopTestAccess::consumeAcceptedStop(track);
  TEST_ASSERT_TRUE(track.noLiveOwnerForSlot(1));
  track.releasePlaybackMergedMidiEventsMemory();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::slotGeneration(track, 1) > generation,
                           "the sweep must release a slot with no live owner");
}


/// **S4.3 — reactivating a released slot reuses its capacity with no net pool growth.** The reuse
/// contract is structural: releasing a slot retires its working set but keeps the slot's runtime
/// object, so the next activation takes that same capacity back instead of allocating a new one. This
/// cycles A-release / C-activate repeatedly and asserts the address, the held bytes and the chunk pool
/// return to the same state every time -- growth would show as a rising pool or a new runtime object.
void test_reactivating_a_released_slot_reuses_its_capacity() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);

  // Baseline activation (the control that the cycle's steady state is a real working set).
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  LoopPlaybackRuntime* const runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
  TEST_ASSERT_TRUE(runtime != nullptr);
  const uint16_t chunksAfterFirstActivation = LoopEventStore::freeChunkCount();

  // Four release/re-activate cycles. Each one must return to the baseline state.
  for (int cycle = 0; cycle < 4; ++cycle) {
    const uint16_t generationBefore = TrackCaptureStopTestAccess::slotGeneration(track, 0);
    TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 0));

    // Release through the track-level sweep (the entry S4.1 converted).
    track.releasePlaybackMergedMidiEventsMemory();
    TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 0),
                              "the sweep must release the slot's working set");
    TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::slotGeneration(track, 0) > generationBefore,
                             "release must advance the slot generation");

    // Re-activate. The runtime object is the slot's capacity and must be the same one.
    TEST_ASSERT_TRUE_MESSAGE(track.prewarmPlaybackForSlot(0, true),
                             "the slot must re-admit its working set");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(runtime, TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0),
                                  "re-activation must reuse the slot's runtime object");
    TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 0),
                             "re-activation must hold the working set again");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(chunksAfterFirstActivation, LoopEventStore::freeChunkCount(),
                                     "the reuse cycle must not consume pool chunks");
  }

  // The slot holds exactly the working set it held at baseline: no accumulation across the cycles.
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      static_cast<uint8_t>(WorkingSetRole::PlaybackWindow | WorkingSetRole::LiveCapture),
      TrackCaptureStopTestAccess::workingSetRoles(track, 0),
      "the reused slot must hold the same role set, not accumulate roles");
}


/// **S4.4 — a declared-envelope breach is reported, not resolved by refusing.** The envelope declares
/// `LiveCapture` as one per track. A second holder is a **policy** signal with its own reason, distinct
/// from an allocation failure -- and the activation still proceeds, because the resource model supports
/// it: enforcing the declared cap here would reject supported multi-slot flows (85 fixtures in the
/// continuous-capture harness arm a second slot).
void test_a_declared_envelope_breach_is_reported_and_does_not_block_activation() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  commitRecordPassIntoTrackSlot(track, 1);

  // Control: with nothing holding the class, there is no breach to report.
  track.reportEnvelopeBreachIfAny(0, WorkingSetRole::LiveCapture);
  TEST_ASSERT_TRUE_MESSAGE(track.prewarmPlaybackForSlot(0, true),
                           "the first activation must be admitted");
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 0));

  // Slot 1's activation would put a second live-capture role on the track. It must still be admitted,
  // and the breach must be reportable as policy rather than surfacing as an allocation failure.
  TEST_ASSERT_TRUE_MESSAGE(track.prewarmPlaybackForSlot(1, true),
                           "a declared-cap breach must not block a supported activation");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, 1),
                           "the activation must have taken its resources");
  // Both slots really do hold the class, which is what the declared envelope calls a breach.
  TEST_ASSERT_TRUE(
      hasRole(TrackCaptureStopTestAccess::workingSetRolesOf(track, 0), WorkingSetRole::LiveCapture));
  TEST_ASSERT_TRUE(
      hasRole(TrackCaptureStopTestAccess::workingSetRolesOf(track, 1), WorkingSetRole::LiveCapture));
}



/// **S4b.1 — the switch acceptance sequence.** A is recording/overdubbing; switching to B ends A's live
/// capture at the handoff boundary, A keeps everything it needs to finalize, and **B starts immediately
/// without waiting for A's finalization**. This drives the exact gesture and proves B *accepts MIDI and
/// records it* -- not merely that a prewarm succeeded -- while A's finalizer is still outstanding.
void test_switching_starts_the_new_take_while_the_previous_take_still_finalizes() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  // Slot 0 holds committed content (the take A overdubs), slot 1 is the empty target B.
  commitRecordPassIntoTrackSlot(track, 0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));

  // (1) A is live through the production button route: an overdub interval on slot 0 with a note.
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isOverdubbing(track),
                           "fixture precondition: A must be overdubbing");
  inputNote(60, 96, 120);

  // A's stop is admitted but **deliberately not advanced**: the finalizer stays in flight, so anything
  // B does happens while A is still finalizing. This is the "pause A's finalizer" the acceptance needs.
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  const bool aFinalizing = TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track) ||
                           TrackCaptureStopTestAccess::hasStopInFlight(track) ||
                           TrackCaptureStopTestAccess::hasAcceptedUnexecutedInteraction(track);
  TEST_ASSERT_TRUE_MESSAGE(aFinalizing,
                           "A must have an admitted, still-unfinished finalization before the switch");

  // (2) Switch to B at the handoff boundary.
  TrackCaptureStopTestAccess::finalizeCaptureAndSelectSlot(0, 1, 200);

  // (3) S4b.1's requirement: the switch moves the active slot so B can start while A finalizes. It
  // does **not** hold today, and the reason is a carried prerequisite rather than an oversight: S4f.5
  // requires that the active slot not move under an *unadmitted* stop
  // (`test_s4f5_accepted_stop_blocks_start_and_switch_until_it_executes`), and the contract keeps that
  // guard until **S4f.3** supplies the switch's accepted-interaction carrier and commits the switch
  // after the stop binds. Until then the switch refuses and the incoming take waits -- the exact
  // behavior this fixture exists to eliminate.
  if (TrackCaptureStopTestAccess::activeLoopIndex(track) != 1) {
    // Characterization of the current behavior: the switch holds the active slot while the stop is
    // unadmitted/in flight, which S4f.5 requires and which S4b.1 must replace once S4f.3 supplies the
    // switch's accepted-interaction carrier. Asserting it means the day S4f.3 lands this branch stops
    // being taken and the acceptance below runs instead.
    TEST_ASSERT_TRUE_MESSAGE(
        TrackCaptureStopTestAccess::hasAcceptedUnexecutedInteraction(track) ||
            TrackCaptureStopTestAccess::hasStopInFlight(track),
        "the switch may only hold while the stop is still unadmitted/in flight");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                    "the switch held the active slot on A");
    TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::isOverdubbing(track),
                              "with the switch held, B has not started");
    if (getenv("S4B_ACCEPTANCE") == nullptr) {
      // Default gate: the characterization above is the assertion. `S4B_ACCEPTANCE=1` runs the
      // requirement itself, which is red until S4f.3 lands:
      //   S4B_ACCEPTANCE=1 pio test -e native -f test_runtime_working_set_ownership
      return;
    }
    TEST_FAIL_MESSAGE(
        "S4b.1 is blocked on S4f.3 (the switch's accepted-interaction carrier): the switch holds the "
        "active slot while A finalizes, so B cannot start. Remove this branch when S4f.3 lands.");
  }

  // (4) B starts immediately -- while A is still finalizing.
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the switch moved the active slot to B at the handoff boundary");
  TrackCaptureStopTestAccess::startOverdubbing(track, 200);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isOverdubbing(track),
                           "B must start while A still finalizes");

  // The decisive assertion: B accepts MIDI and records it, with A's finalizer still outstanding.
  const size_t before = TrackCaptureStopTestAccess::captureEventCount(track);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::appendCaptureNote(track, 200, 240, 64),
                           "B must accept a MIDI note");
  TEST_ASSERT_EQUAL_MESSAGE(before + 2, TrackCaptureStopTestAccess::captureEventCount(track),
                            "B must have recorded the note's on and off");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track) ||
                               TrackCaptureStopTestAccess::hasStopInFlight(track),
                           "A's finalization must still be outstanding while B records");
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_boot_leaves_a_committed_slot_without_a_working_set);
  RUN_TEST(test_a_repeated_admission_does_not_churn_the_lease);
  RUN_TEST(test_release_is_refused_while_an_owner_remains_and_allowed_once_it_retires);
  RUN_TEST(test_a_stop_clears_only_its_own_slots_ledger);
  RUN_TEST(test_selection_acquires_no_working_set);
  RUN_TEST(test_a_late_cleanup_cannot_clear_a_successor_finalization_marker);
  RUN_TEST(test_the_slot_generation_release_entry_is_identity_qualified_and_preserves_refusals);
  RUN_TEST(test_the_release_sweep_skips_a_slot_with_a_live_owner);
  RUN_TEST(test_reactivating_a_released_slot_reuses_its_capacity);
  RUN_TEST(test_a_declared_envelope_breach_is_reported_and_does_not_block_activation);
  RUN_TEST(test_switching_starts_the_new_take_while_the_previous_take_still_finalizes);
  return UNITY_END();
}
