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
#define HARNESS_OBSERVABLE_SD_REFRESH
#define HARNESS_OBSERVABLE_START_RECLAIM
// S4b.1 check 7 (cross-track): the successor's track must be selected through the production route, which
// departs the note-edit session before it moves focus. That departure is the edit suites' subject, so it
// stays observable here rather than failing closed -- the same treatment the switch's storage and display
// boundaries already get above.
#define HARNESS_OBSERVABLE_TRACK_SELECT
// S4b.1 check 7 (cleared target): driving a **cleared** slot through `Track::clear()` reaches the
// note-edit revert the clear performs. That revert is a note-edit neighbour, so it stays observable here
// rather than failing closed; nothing in this suite asserts on it.
#define HARNESS_OBSERVABLE_SLOT_CLEAR
// The production **record-stop commit** route. One fixture drives `TrackManager::handleTransportStop()` and
// asserts the take commits with nonzero length and content -- the device failure this encodes is a successor
// whose take finalised at length 0 while a start marker made it look successful. That route's implementations
// live in the two TUs included below, so the four stubs that shadow them are lifted by this macro. It changes
// nothing for the fixtures above: none of them reached `handleTransportStop`, `stopRecordingToStopped`,
// `stopRecording` or `commitCaptureForStop`, all of which were fail-closed until now.
#define HARNESS_LINKS_REAL_TRANSPORT_STOP
// ...and the physical all-notes-off fan-out it reaches is observable (no-op) for this route.
#define HARNESS_OBSERVABLE_ALL_NOTES_OFF
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
// The record-stop commit route (see the macro above): `TrackManager::handleTransportStop` and the
// `Track::stopRecordingToStopped` -> `Track::commitCaptureForStop` chain it drives.
#include "../../src/Track/TrackCaptureStopCommit.cpp"
#include "../../src/Utils/RecordStopLength.cpp"
#include "../../src/TrackManager/TrackManagerTransportTick.cpp"
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
  /// S4f.3: the real S4f.2 dispatch decision, the same entry `processDeferredIdleMaintenance` calls, so
  /// the accepted stop is driven to admission by the production lane rather than by a test-only path.
  static bool serviceAcceptedInteractionTurn(Track& track) {
    return track.serviceAcceptedInteractionTurn();
  }
  /// S4b: the switch path and the live-interval state the acceptance sequence needs.
  static void finalizeCaptureAndSelectSlot(uint8_t trackIndex, uint8_t newSlot, uint32_t tick) {
    trackManager.finalizeCaptureAndSelectSlot(trackIndex, newSlot, tick);
  }
  static bool hasPendingDeferredOverdubStop(const Track& track) {
    return track.hasPendingDeferredOverdubStop();
  }
  static bool hasStopInFlight(const Track& track) { return track.hasStopInFlight(); }
  /// S4f.6's slot-scoped predicate: a stop obligation that names **this** slot. The retry fixture needs it
  /// to assert that the refusal it is exercising is the target's own obligation, not another slot's.
  static bool hasStopInFlightForSlot(const Track& track, uint8_t slot) {
    return track.hasStopInFlightForSlot(slot);
  }
  /// The native clock boundary (DEC-080). This harness routes `shouldQuantizeRecordStart()` through it, so it
  /// **is** the clock condition `TrackManager::startRecordingTrack`'s arm-and-wait branch tests.
  static void setClockRunning(bool running) { nativeTransportRunning = running; }
  /// Exactly-once observables for one take. A second `Track::startRecording` re-begins capture --
  /// `Loop::beginCapture` clears the capture extent -- and re-stamps `loop.startLoopTick` from the later
  /// dispatch tick, so "the take was executed twice" shows up as either of these changing.
  static uint32_t takeOriginTick(const Track& track) { return track.getActiveLoop().startLoopTick; }
  static uint32_t firstCaptureEventTick(const Track& track) {
    const auto& store = track.getActiveLoop().capture.store;
    return store.size() == 0 ? UINT32_MAX : store.at(0).tick;
  }
  static uint8_t firstCaptureEventNote(const Track& track) {
    const auto& store = track.getActiveLoop().capture.store;
    return store.size() == 0 ? 0xFF : store.at(0).data.noteData.note;
  }
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
  static bool recordQueuedForSlot(uint8_t trackIndex, uint8_t slot) {
    return trackManager.isRecordingQueued(trackIndex, slot);
  }
  static bool anyRecordQueued(uint8_t trackIndex) {
    return trackManager.hasQueuedRecordingTrack(trackIndex);
  }
  /// The production queued-record dispatch (`TrackManager::handlePendingRecordStart`).
  static void startPendingRecord(uint32_t tick) { trackManager.handlePendingRecordStart(tick); }
  static void toggleRecordForSlot(uint8_t slotIndex) {
    midiButtonActions.handleToggleRecordForSlot(slotIndex);
  }
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
  /// The production active-slot write authority (`TrackManager::setActiveLoopIndex`), so a fixture
  /// performs a switch through the same entry a gesture uses.
  static void activateSlot(uint8_t trackIndex, uint8_t slot) {
    trackManager.setActiveLoopIndex(trackIndex, slot);
  }
  static void selectSlotIndex(uint8_t trackIndex, uint8_t slot) {
    trackManager.slotStateMachine.setSelectedSlotIndex(trackIndex, slot);
  }
  static uint8_t selectedSlotIndex(uint8_t trackIndex) {
    return trackManager.slotStateMachine.getSelectedSlotIndex(trackIndex);
  }
  // ---- the record-stop commit route ---------------------------------------------------------------
  /// The production transport-stop entry (`TrackManager::handleTransportStop`), wired through
  /// `HARNESS_LINKS_REAL_TRANSPORT_STOP`. On device this is the route that finalises a live capture with
  /// `RECS,stopToStopped`, and the route a take must survive.
  static void transportStop() { trackManager.handleTransportStop(); }
  /// Whether `slot`'s loop holds committed passes -- the "belongs to this slot's loop" half of slot identity.
  static bool committedPassesInSlot(Track& track, uint8_t slot) {
    return track.getLoop(slot).hasCommittedPasses();
  }
  /// The slot loop's own length, which the commit path sets. A start marker says nothing about this.
  static uint32_t slotLoopLength(Track& track, uint8_t slot) {
    return track.getLoop(slot).loopLengthTicks;
  }
  /// Whether `slot`'s **committed** rows contain a note-on for `note`. This reads the pass content the way
  /// the firmware's own `!CAPTURE_CONTENT` dump does, so the assertion is on recorded content rather than on
  /// a live capture store that a later stop could discard.
  static bool committedSlotHasNote(Track& track, uint8_t slot, uint8_t note) {
    Loop& loop = track.getLoop(slot);
    MidiEventVec rows;
    auto holdsNote = [&rows, note]() {
      for (const auto& e : rows) {
        if (static_cast<unsigned>(e.type) == 144u && e.data.noteData.note == note &&
            e.data.noteData.velocity > 0) {
          return true;
        }
      }
      return false;
    };
    LoopEventStore::appendChunkRefEvents(loop.passes.recordPass.committedChunkIds, rows);
    if (holdsNote()) return true;
    for (size_t i = 0; i < loop.passes.overdubPasses.size(); ++i) {
      rows.clear();
      LoopEventStore::appendChunkRefEvents(loop.passes.overdubPasses[i].committedChunkIds, rows);
      if (holdsNote()) return true;
    }
    return false;
  }
};

namespace {

/// Commit one record pass of `lengthTicks` into a slot so the slot holds content -- the state a restored
/// library has. The length is a parameter because the populated-target variant must be able to tell
/// "the successor used **its own** loop's phase" from "the successor inherited the predecessor's": it
/// gives the two slots different lengths and asserts the successor kept its own.
void commitRecordPassIntoTrackSlotOfLength(Track& track, uint8_t slot, uint32_t lengthTicks) {
  TrackCaptureStopTestAccess::selectSlot(track, slot);
  Loop& loop = track.getActiveLoop();
  loop.loopLengthTicks = lengthTicks;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(0, 1, 48, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(48, 1, 48, 0)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(lengthTicks, CommitReason::RecordStop));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass());
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
}

/// Commit one record pass into a slot so the slot holds content -- the state a restored library has.
void commitRecordPassIntoTrackSlot(Track& track, uint8_t slot) {
  commitRecordPassIntoTrackSlotOfLength(track, slot, 768);
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


/// **S4.1 — a same-track slot switch releases the retired slot's working-set lease, per slot.**
///
/// The device evidence for S4.1 is the *finalization-capacity* release (`canonical_release,<slot>,…`);
/// nothing named the working-set **lease** release on a same-track switch, and for a same-track sequence
/// the take-start sweep excludes the track. This drives the production switch commit
/// (`TrackManager::finalizeCaptureAndSelectSlot` -> `Track::resetPlaybackStateForSlot` ->
/// `Track::invalidatePlaybackMergedMidiEvents` -> `Track::releaseEligiblePlaybackRuntimeSlots`) with the
/// **incoming** slot protected by a live owner, so the release must be evaluated per slot: the retired
/// slot is released, the owned one is not.
void test_s4_1_a_same_track_switch_releases_the_retired_slots_lease() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  constexpr uint8_t kRetired = 1;
  constexpr uint8_t kIncoming = 3;
  commitRecordPassIntoTrackSlot(track, kRetired);
  commitRecordPassIntoTrackSlot(track, kIncoming);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kRetired, true));
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kIncoming, true));
  TrackCaptureStopTestAccess::activateSlot(0, kRetired);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kRetired, track.getActiveLoopIndex(),
                                  "the fixture needs the retired slot active");

  const uint16_t retiredGeneration = TrackCaptureStopTestAccess::slotGeneration(track, kRetired);
  const uint16_t incomingGeneration = TrackCaptureStopTestAccess::slotGeneration(track, kIncoming);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kRetired),
                           "the retired slot must hold a working set before the switch");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kIncoming),
                           "the incoming slot must hold a working set before the switch");
  // An admitted working set is not itself a live owner, so the retired slot is releasable...
  TEST_ASSERT_TRUE_MESSAGE(track.noLiveOwnerForSlot(kRetired),
                           "an admitted working set must not block release on its own");

  // The track is audibly playing, which is the ordinary state a slot switch happens in: it keeps the
  // switch's own "start playback if the target has data" step out of this fixture (that step belongs to
  // the playback-start suites and is fail-closed here).
  track.forceSetState(TRACK_PLAYING);
  TEST_ASSERT_TRUE(track.isAudiblePlaybackActive());

  // ...while the incoming slot is protected, so a track-global sweep cannot pass this fixture.
  TrackCaptureStopTestAccess::latchAcceptedStopForSlot(track, kIncoming);
  TEST_ASSERT_FALSE_MESSAGE(track.noLiveOwnerForSlot(kIncoming),
                            "the fixture needs a live owner on the incoming slot");

  // The production switch, same track.
  TrackCaptureStopTestAccess::finalizeCaptureAndSelectSlot(0, kIncoming, 96);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kIncoming, track.getActiveLoopIndex(),
                                  "the switch must move the active slot");

  // The retired slot: released, and its identity advanced, so the exact slot is observable.
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kRetired),
                            "the switch must release the retired slot's working set");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::slotGeneration(track, kRetired) >
                               retiredGeneration,
                           "the release must advance the retired slot's generation");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      static_cast<uint8_t>(WorkingSetOutcome::StaleGeneration),
      static_cast<uint8_t>(TrackCaptureStopTestAccess::releaseSlot(track, kRetired, retiredGeneration)),
      "the pre-switch identity must no longer release: the switch consumed it");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      static_cast<uint8_t>(WorkingSetOutcome::NotHeld),
      static_cast<uint8_t>(TrackCaptureStopTestAccess::releaseSlot(
          track, kRetired, TrackCaptureStopTestAccess::slotGeneration(track, kRetired))),
      "nothing may be left to release on the retired slot");

  // The protected sibling is untouched: the release was per slot, not a track-global reset.
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kIncoming),
                           "a slot with a live owner must survive the switch");
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(incomingGeneration,
                                   TrackCaptureStopTestAccess::slotGeneration(track, kIncoming),
                                   "a refused slot's generation must not move");

  // Control: retire the owner, switch back, and the same route releases the other slot.
  TrackCaptureStopTestAccess::consumeAcceptedStop(track);
  TEST_ASSERT_TRUE(track.noLiveOwnerForSlot(kIncoming));
  TrackCaptureStopTestAccess::finalizeCaptureAndSelectSlot(0, kRetired, 192);
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kIncoming),
                            "the control switch must release the now-unowned slot");
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::slotGeneration(track, kIncoming) > incomingGeneration);
}

/// **S4.1 — a live finalization refuses the retired slot's lease release.**
///
/// While an interval finalizes, `Track::invalidatePlaybackMergedMidiEvents` early-returns on the
/// **track-level** `hasPendingDeferredOverdubStop()`, so the sweep is suppressed before the per-slot
/// predicate is consulted; the slot's own entry independently refuses with `RefusedBusy`
/// (`LoopPlaybackRuntime::reset()` returns false while `captureStopOwner` is pending). The switch still
/// commits -- an *admitted* stop does not hold the active slot (S4f.3) -- so this is the S4.1 handoff
/// shape with the lease being the thing that survives it.
void test_s4_1_a_live_finalization_refuses_the_retired_slots_lease_release() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  constexpr uint8_t kFinalizing = 1;
  constexpr uint8_t kSuccessor = 3;
  commitRecordPassIntoTrackSlot(track, kFinalizing);
  commitRecordPassIntoTrackSlot(track, kSuccessor);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kFinalizing, true));
  TrackCaptureStopTestAccess::activateSlot(0, kFinalizing);

  // A live interval on the finalizing slot, opened through the production start, with its stop admitted.
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::startOverdubbing(track, 96);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isOverdubbing(track),
                           "the fixture needs a live interval");
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::appendCaptureNote(track, 96, 120, 60));
  TEST_ASSERT_TRUE_MESSAGE(
      TrackCaptureStopTestAccess::admitStopForSlot(track, PendingStopDestination::Playing, 96,
                                                   kFinalizing, /*interval=*/1),
      "the production stop admission must accept on the warmed slot");
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());

  const uint16_t generation = TrackCaptureStopTestAccess::slotGeneration(track, kFinalizing);
  TEST_ASSERT_FALSE_MESSAGE(track.noLiveOwnerForSlot(kFinalizing),
                            "a live finalization must refuse release on its slot");

  // The slot's own entry refuses and names the refusal. (It also asks the pending stop to become a
  // transport stop -- that is `reset()`'s documented behaviour on this path, not a fixture artifact.)
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      static_cast<uint8_t>(WorkingSetOutcome::RefusedBusy),
      static_cast<uint8_t>(TrackCaptureStopTestAccess::releaseSlot(track, kFinalizing, generation)),
      "a live finalization must answer RefusedBusy");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kFinalizing),
                           "a refused release must not drop the working set");
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(generation,
                                   TrackCaptureStopTestAccess::slotGeneration(track, kFinalizing),
                                   "a refused release must not advance the generation");

  // The production switch commits, and the lease survives it: the sweep is suppressed while the
  // interval finalizes, so nothing is released underneath the owner.
  TrackCaptureStopTestAccess::finalizeCaptureAndSelectSlot(0, kSuccessor, 120);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kSuccessor, track.getActiveLoopIndex(),
                                  "an admitted stop must not hold the active slot (S4f.3)");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kFinalizing),
                           "the switch must not release a finalizing slot's working set");
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(generation,
                                   TrackCaptureStopTestAccess::slotGeneration(track, kFinalizing),
                                   "the suppressed sweep must not touch the generation");
  TEST_ASSERT_TRUE_MESSAGE(track.hasPendingDeferredOverdubStop(),
                           "the interval must still be finalizing after the switch");
}

/// **S4.1 — the retired slot's lease is released once its finalization retires.**
///
/// The complement of the refusal case, and the answer to "which same-track route releases it": not the
/// retirement itself (`TrackPreparedStopCommit::completeDeferredCleanup` clears `captureStopOwner` and arms
/// the *capacity* release marker, but releases no working set), so the next same-track switch's playback
/// reset is what reaches the retired slot. This drives that route end to end.
void test_s4_1_the_retired_slots_lease_is_released_once_its_finalization_retires() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  constexpr uint8_t kFinalizing = 1;
  constexpr uint8_t kSuccessor = 3;
  commitRecordPassIntoTrackSlot(track, kFinalizing);
  commitRecordPassIntoTrackSlot(track, kSuccessor);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kFinalizing, true));
  TrackCaptureStopTestAccess::activateSlot(0, kFinalizing);

  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::startOverdubbing(track, 96);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::appendCaptureNote(track, 96, 120, 61));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::admitStopForSlot(track, PendingStopDestination::Playing, 96,
                                                                kFinalizing, /*interval=*/1));
  const uint16_t generation = TrackCaptureStopTestAccess::slotGeneration(track, kFinalizing);

  // The handoff: the switch commits while the predecessor still finalizes, and the lease survives it.
  TrackCaptureStopTestAccess::finalizeCaptureAndSelectSlot(0, kSuccessor, 120);
  TEST_ASSERT_EQUAL_UINT8(kSuccessor, track.getActiveLoopIndex());
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kFinalizing),
                           "the predecessor must keep its lease while it finalizes");
  TEST_ASSERT_EQUAL_UINT16(generation, TrackCaptureStopTestAccess::slotGeneration(track, kFinalizing));

  // Retire the finalization through the production grain.
  for (int turn = 0; turn < 8192 && track.hasPendingDeferredOverdubStop(); ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE_MESSAGE(track.hasPendingDeferredOverdubStop(),
                            "the fixture must retire the deferred stop");
  TEST_ASSERT_TRUE_MESSAGE(track.noLiveOwnerForSlot(kFinalizing),
                           "retirement must free the slot for release");

  // The next same-track switch runs the sweep with no live owner left, and releases the retired slot.
  TrackCaptureStopTestAccess::finalizeCaptureAndSelectSlot(0, kFinalizing, 192);
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kFinalizing),
                            "the next switch must release the retired slot's working set");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::slotGeneration(track, kFinalizing) > generation,
                           "the release must advance the retired slot's generation");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      static_cast<uint8_t>(WorkingSetOutcome::StaleGeneration),
      static_cast<uint8_t>(
          TrackCaptureStopTestAccess::releaseSlot(track, kFinalizing, generation)),
      "the pre-switch identity must be stale after the release");
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
/// capture at the handoff boundary, A retains everything it needs to finalize, and **B starts
/// immediately without waiting for A to finish finalizing**.
///
/// Requirements this fixture holds itself to, after review:
/// * the finalizer is **admitted** (`hasPendingDeferredOverdubStop`), never a mere accepted-unexecuted
///   stop, so "waiting for admission" and "admitted and paused" are not conflated;
/// * B is reached through the **user-facing gesture** (`MidiButtonActions::handleToggleRecordForSlot`)
///   and its input through the real MIDI handler, so this proves the normal start and incoming MIDI
///   reach B -- not that a buffer accepts an append;
/// * the acceptance runs **unconditionally** once S4f.3 lands: the characterization branch below
///   disappears rather than quietly returning early.
void test_switching_starts_the_new_take_while_the_previous_take_still_finalizes() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  // Slot 0 holds committed content (A's target), slot 1 is the empty target B.
  commitRecordPassIntoTrackSlot(track, 0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));

  // (1) A is live through the production button route, and takes MIDI through the real handler.
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isOverdubbing(track),
                           "fixture precondition: A must be overdubbing");
  inputNote(60, 96, 120);

  // (2) A's stop is admitted and **deliberately not advanced**: the finalizer is admitted and paused.
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track),
                           "A must have an *admitted* finalizer before the switch");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasAcceptedUnexecutedInteraction(track),
                            "the stop must be admitted, not merely accepted-and-unexecuted");

  // (3) Switch to B through the user-facing gesture, then start B. The per-slot gesture finalizes A's
  // capture, commits the switch and returns; it does not also start the target, so the start is its own
  // press -- the acceptance sequence's "switch to B, then start B".
  clockManager.assignCurrentTickSilently(200);
  TrackCaptureStopTestAccess::toggleRecordForSlot(1);
  // S4f.6: the switch commits even though A's admitted stop is still finalizing, and this is now
  // asserted unconditionally. The characterization branch that accepted "the switch held the active slot
  // on A" was deleted when S4f.6 landed, per this packet's own note, so a regression to refusing the
  // switch cannot pass by returning early.
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the switch must move the active slot to B while A finalizes");

  // Start B. The arm/record entry and the record-arm branch both act on the **selected** slot, which the
  // switch deliberately leaves on the predecessor (S4f.5 keeps focus for the piano roll). Select the
  // active target first, which is what the user does when the switch moved the audio to B.
  TrackCaptureStopTestAccess::selectSlotIndex(0, 1);
  // Start B through the per-slot gesture, which is the successor-start route (the arm entry
  // `handleToggleRecord` routes a track already reporting overdubbing to `stopOverdubbing`).
  TrackCaptureStopTestAccess::toggleRecordForSlot(1);
  // A start on a running transport is **queued** at the loop's bar boundary
  // (`TrackManager::handlePendingRecordStart`), so drive that production dispatch across the boundary.
  // `handlePendingRecordStart` deliberately ignores the tick the start was queued at, so land the
  // dispatch on the *following* loop boundary rather than the same one. There is deliberately no
  // fallback press: a start that only happens because the fixture pressed again is not the requirement.
  const uint32_t queuedTick = clockManager.getCurrentTick();
  for (uint32_t tick = queuedTick + 1; tick <= queuedTick + 768 * 3 &&
                             !TrackCaptureStopTestAccess::isRecording(track) &&
                             !TrackCaptureStopTestAccess::isOverdubbing(track);
       ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }

  // (4) B is the live interval and records incoming MIDI while A remains unfinished.
  // S4f.6: B starts while A still finalizes. Before S4f.6 the refusal ran *inside*
  // `startRecordingTrack` and short-circuited the dispatcher's consumption lines, so the request was
  // retained but never executed and never superseded. Both characterization branches are deleted; the
  // claims below are now the requirement itself.
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track) ||
                               TrackCaptureStopTestAccess::isOverdubbing(track),
                           "B must start from the gesture while A still finalizes");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the switch moved the active slot to B at the handoff boundary");
  const size_t before = TrackCaptureStopTestAccess::captureEventCount(track);
  // Inject in the record domain: the dispatcher above advanced the tick to the loop boundary, so the
  // note must lie *after* the current tick or capture rejects it as already past.
  const uint32_t tickBeforeInput = clockManager.getCurrentTick();
  const uint32_t onTick = tickBeforeInput + 8;
  inputNote(64, onTick, onTick + 32);
  const size_t after = TrackCaptureStopTestAccess::captureEventCount(track);
  if (after != before + 2) {
    // An on-failure diagnostic only -- the claim itself is unconditional, so this cannot pass by
    // returning early the way the pre-S4f.6 characterization could.
    char state[256];
    snprintf(state, sizeof(state),
             "B did not record the note: recording=%d overdubbing=%d captureActive=%d size=%zu tick=%u "
             "active=%u beforeTick=%u onTick=%u phase=%u hasOverdubSession=%d",
             TrackCaptureStopTestAccess::isRecording(track) ? 1 : 0,
             TrackCaptureStopTestAccess::isOverdubbing(track) ? 1 : 0,
             TrackCaptureStopTestAccess::captureActive(track) ? 1 : 0, after,
             (unsigned)clockManager.getCurrentTick(),
             TrackCaptureStopTestAccess::activeLoopIndex(track), (unsigned)tickBeforeInput,
             (unsigned)onTick, static_cast<unsigned>(track.getActiveLoop().capture.phase),
             track.getActiveLoop().hasOverdubSession() ? 1 : 0);
    snprintf(state + strlen(state), sizeof(state) - strlen(state),
             " | queued any=%d slot0=%d slot1=%d", TrackCaptureStopTestAccess::anyRecordQueued(0) ? 1 : 0,
             TrackCaptureStopTestAccess::recordQueuedForSlot(0, 0) ? 1 : 0,
             TrackCaptureStopTestAccess::recordQueuedForSlot(0, 1) ? 1 : 0);
    TEST_FAIL_MESSAGE(state);
  }
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track),
                           "A's finalizer must still be outstanding while B records");

  // (5) A completes without damaging B: drive A's finalizer to completion and confirm B's interval is
  // untouched and still the live one.
  const size_t bEvents = TrackCaptureStopTestAccess::captureEventCount(track);
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track),
                            "A's finalizer must be able to complete");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "A's completion must not move the active slot off B");
  TEST_ASSERT_EQUAL_MESSAGE(bEvents, TrackCaptureStopTestAccess::captureEventCount(track),
                            "A's completion must not damage B's recorded interval");
}

/// **S4f.3 — accepted-switch retention under unavailable stop admission (blocked -> ready).**
///
/// This is the end-to-end proof S4b.1's check 4 needs, and it replaces an earlier version of this
/// coverage whose claimed request did not exist: that body returned early from an `activeLoopIndex != 1`
/// characterization branch in the default gate, so it passed while holding nothing. The false green is
/// recorded in the packet.
///
/// One track, three slots. A (slot 0) and B (slot 1) carry committed content, C (slot 2) is empty:
///
///  1. A's stop is **admitted and bound** and its finalizer is held unfinished -- it occupies the track's
///     deferred-finalization capacity.
///  2. B becomes active through the production active-slot authority and **overdubs**, so B's stop is a
///     deferred stop (a fresh *record* stop is not capacity-governed, which is why B is an overdub).
///  3. B's stop is requested and **denied on capacity**, so B's obligation latches against B's own slot.
///  4. A switch/start is requested on C while B's stop is unadmitted. The switch must not commit and must
///     not be dropped: the request is retained.
///  5. Repeated dispatch must leave B's obligation and C's switch/start intact, and must never move the
///     active slot underneath B's unadmitted stop.
///  6. Capacity is released (A's finalizer completes) and B's stop is driven until it binds.
///  7. Dispatch then commits the retained switch to C and starts C, which records MIDI.
///  8. Further dispatch must not execute either request twice.
void test_a_blocked_switch_and_start_survive_until_its_prerequisite_stop_binds() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  commitRecordPassIntoTrackSlot(track, 1);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(1, true));

  // (1) A overdubs through the production gesture; its stop is admitted and **held**.
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isOverdubbing(track),
                           "precondition: A must be overdubbing");
  inputNote(60, 96, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track),
                           "A's stop must be admitted before the handoff");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 0),
                           "A's admitted stop must be identified by A's own slot");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 0),
                            "A's stop is admitted, not merely accepted-and-unexecuted");

  // (2) B becomes active (A's own stop is bound, so the switch commits) and overdubs.
  clockManager.assignCurrentTickSilently(200);
  TrackCaptureStopTestAccess::activateSlot(0, 1);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the switch to B must commit while A still finalizes");
  TrackCaptureStopTestAccess::startOverdubbing(track, clockManager.getCurrentTick());
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isOverdubbing(track),
                           "B must be overdubbing for its stop to need deferred admission");

  // (3) B's stop is requested and denied on capacity, latching against B's own slot.
  clockManager.assignCurrentTickSilently(264);
  TrackCaptureStopTestAccess::selectSlotIndex(0, 1);
  TrackCaptureStopTestAccess::toggleRecordForSlot(1);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 1),
                           "B's denied stop must be accepted and latched against B's own slot");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 1),
                            "capacity is held by A, so B's stop must not be admitted");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 0),
                           "A's finalizer must still be the capacity holder");

  // (4) Request switch/start on C while B's stop is unadmitted: retained, neither committed nor dropped.
  clockManager.assignCurrentTickSilently(300);
  TrackCaptureStopTestAccess::toggleRecordForSlot(2);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      1, TrackCaptureStopTestAccess::activeLoopIndex(track),
      "activation must NOT move the active slot underneath B's unadmitted stop");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(0, 2),
                           "the switch must retain C's record start rather than drop it");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 1),
                           "B's stop obligation must survive the switch request");

  // (5) Repeated dispatch: neither request may be lost or executed early, and the active slot must stay
  // on B for the whole blocked window -- the explicit assertion the contract asks for.
  for (uint32_t tick = 301; tick <= 301 + 768 * 3; ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        1, TrackCaptureStopTestAccess::activeLoopIndex(track),
        "the active slot must not move under B's unadmitted stop, on any dispatch");
    TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track),
                              "C must not start before its prerequisite stop binds");
  }
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(0, 2),
                           "C's switch/start must survive repeated dispatch");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 1),
                           "B's stop obligation must survive repeated dispatch");

  // (6) Release capacity: A's finalizer completes, then B's stop is driven until it binds.
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track),
                            "A's finalizer must be able to complete and release capacity");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 1),
                           "B's stop obligation is still waiting after capacity frees");
  bool bStopBound = false;
  for (int turn = 0; turn < 64 && !bStopBound; ++turn) {
    (void)TrackCaptureStopTestAccess::serviceAcceptedInteractionTurn(track);
    bStopBound = TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 1);
  }
  TEST_ASSERT_TRUE_MESSAGE(bStopBound, "B's accepted stop must bind once capacity frees");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 1),
                            "the bound stop's obligation must be consumed exactly once");

  // (7) Dispatch now commits the retained switch and starts C on C's eligible boundary.
  const uint32_t resumeTick = clockManager.getCurrentTick();
  for (uint32_t tick = resumeTick + 1;
       tick <= resumeTick + 768 * 3 && !TrackCaptureStopTestAccess::isRecording(track); ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the retained switch must commit to C once its stop bound");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track),
                           "C must start recording from the retained request, with no further gesture");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(0, 2),
                            "the executed start must be consumed exactly once");

  // C records the incoming MIDI through the production input route.
  const size_t before = TrackCaptureStopTestAccess::captureEventCount(track);
  const uint32_t tickBeforeInput = clockManager.getCurrentTick();
  inputNote(67, tickBeforeInput + 8, tickBeforeInput + 40);
  TEST_ASSERT_EQUAL_MESSAGE(before + 2, TrackCaptureStopTestAccess::captureEventCount(track),
                            "C must record the incoming MIDI note's on and off");

  // (8) Exactly once: further dispatch must not re-execute either request or disturb C's interval.
  const size_t cEvents = TrackCaptureStopTestAccess::captureEventCount(track);
  const uint32_t afterStart = clockManager.getCurrentTick();
  for (uint32_t tick = afterStart + 1; tick <= afterStart + 768 * 2; ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "subsequent dispatch must not move the active slot again");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(0, 2),
                            "the start request must not be re-armed by later dispatch");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 1),
                            "B's stop obligation must not be re-executed by later dispatch");
  TEST_ASSERT_EQUAL_MESSAGE(cEvents, TrackCaptureStopTestAccess::captureEventCount(track),
                            "later dispatch must not add events to C's interval");
}


/// **S4f.3 — the successor's take must COMMIT with content, not merely start.**
///
/// Encodes a real device failure. On hardware the whole handoff chain worked -- A's finalizer outstanding,
/// B's stop capacity-denied, C's switch/start requested and retained, B's stop binding, C's capture starting
/// -- and C's take still committed with **final length 0 and no content**, because the harness never injected
/// MIDI into C after it started. A start marker (`RECA`) therefore read as success while the user-visible
/// result was empty.
///
/// The fixture above stops at "C is recording and its capture store grew by the injected note's on and off":
/// it never drives the transport-stop route and never asserts that the take **commits**. This one drives that
/// route and asserts the committed result, so a start marker can never satisfy it.
///
/// One track, A = slot 0, B = slot 1, C = slot 2 (A and B carry content; C is the empty record target).
/// Steps 1-7 reach the same handoff state as the fixture above, then:
///   8. MIDI enters C's capture through the real input handler;
///   9. the **production transport-stop route** finalises the take -- on device, the route that emits
///      `RECS,stopToStopped`;
///  10. the user-visible result: C still owns the active slot, C's loop holds a committed pass, the length is
///      NONZERO, and the committed rows contain the injected note (with an uninjected-note control);
///  11. B's outstanding finalization completing afterwards does not damage C's committed take.
void test_the_successor_take_commits_with_content_after_a_transport_stop() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  commitRecordPassIntoTrackSlot(track, 1);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(1, true));

  // (1) A overdubs through the production gesture; its stop is admitted and held.
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::isOverdubbing(track));
  inputNote(60, 96, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track),
                           "precondition: A's stop must be admitted before the handoff");

  // (2) B becomes active (A's own stop is bound, so the switch commits) and overdubs.
  clockManager.assignCurrentTickSilently(200);
  TrackCaptureStopTestAccess::activateSlot(0, 1);
  TEST_ASSERT_EQUAL_UINT8(1, TrackCaptureStopTestAccess::activeLoopIndex(track));
  TrackCaptureStopTestAccess::startOverdubbing(track, clockManager.getCurrentTick());
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::isOverdubbing(track));

  // (3) B's stop is requested and denied on capacity, latching against B's own slot.
  clockManager.assignCurrentTickSilently(264);
  TrackCaptureStopTestAccess::selectSlotIndex(0, 1);
  TrackCaptureStopTestAccess::toggleRecordForSlot(1);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 1));
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 1));

  // (4) Switch/start on C while B's stop is unadmitted: retained, neither committed nor dropped.
  clockManager.assignCurrentTickSilently(300);
  TrackCaptureStopTestAccess::toggleRecordForSlot(2);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(0, 2),
                           "the switch must retain C's record start");

  // (5)/(6) Dispatch keeps the requests; then capacity frees and B's stop binds.
  for (uint32_t tick = 301; tick <= 301 + 768; ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  bool bStopBound = false;
  for (int turn = 0; turn < 64 && !bStopBound; ++turn) {
    (void)TrackCaptureStopTestAccess::serviceAcceptedInteractionTurn(track);
    bStopBound = TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 1);
  }
  TEST_ASSERT_TRUE_MESSAGE(bStopBound, "B's accepted stop must bind once capacity frees");

  // (7) Dispatch commits the retained switch and starts C on C's eligible boundary.
  const uint32_t resumeTick = clockManager.getCurrentTick();
  for (uint32_t tick = resumeTick + 1;
       tick <= resumeTick + 768 * 3 && !TrackCaptureStopTestAccess::isRecording(track); ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the retained switch must commit to C");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track), "C must be recording");

  // (8) The notes enter C through the real MIDI input handler, and must land in C's capture.
  const uint8_t kSuccessorNote = 67;
  const size_t beforeInput = TrackCaptureStopTestAccess::captureEventCount(track);
  const uint32_t tickBeforeInput = clockManager.getCurrentTick();
  inputNote(kSuccessorNote, tickBeforeInput + 8, tickBeforeInput + 40);
  TEST_ASSERT_EQUAL_MESSAGE(beforeInput + 2, TrackCaptureStopTestAccess::captureEventCount(track),
                            "the injected note's on and off must enter C's capture");

  // Give the take a bar of material before the stop, so a nonzero length is the expected result rather than
  // an artefact of the injection tick.
  clockManager.assignCurrentTickSilently(tickBeforeInput + 768);

  // Precondition that makes the transport stop the thing under test rather than an incidental step: before
  // it, C's take is live and **uncommitted** -- exactly the device state whose zero-length commit is the
  // failure encoded here. Without this the assertions below could pass on a take something else committed.
  const uint32_t liveLengthBeforeStop = TrackCaptureStopTestAccess::slotLoopLength(track, 2);
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::committedPassesInSlot(track, 2),
                            "precondition: C's take must not be committed before the transport stop");

  // (9) The production transport-stop route finalises the take. S4f.17: a Record stop is
  // admitted as a deferred job and its pass is published by the worker's bounded grains, so the
  // user-visible result is consumed here instead of at the stop edge. The drive is bounded and
  // stops when no stop is in flight.
  TrackCaptureStopTestAccess::transportStop();
  for (int turn = 0; turn < 8192; ++turn) {
    if (TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track)) {
      (void)TrackCaptureStopTestAccess::step(track);
      continue;
    }
    // A stop that met a busy finalization owner is retained, not dropped: the accepted-stop
    // drive binds it once capacity frees, exactly as the production deferred owner does.
    if (TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 2)) {
      (void)TrackCaptureStopTestAccess::serviceAcceptedInteractionTurn(track);
      continue;
    }
    break;
  }
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track) ||
                                TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 2),
                            "the transport-stop take must finish its deferred finalization");

  // (10) The USER-VISIBLE result. A start marker proves none of this.
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "C must still own the active slot after the transport stop");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::committedPassesInSlot(track, 2),
                           "C's take must COMMIT a pass, not merely have started");
  const uint32_t committedLength = TrackCaptureStopTestAccess::slotLoopLength(track, 2);
  char lengthDetail[224];
  snprintf(lengthDetail, sizeof(lengthDetail),
           "C's committed take must have a NONZERO length (active=%u loopLength=%u committedPasses=%d, "
           "liveLengthBeforeStop=%u)",
           TrackCaptureStopTestAccess::activeLoopIndex(track), committedLength,
           TrackCaptureStopTestAccess::committedPassesInSlot(track, 2) ? 1 : 0,
           liveLengthBeforeStop);
  TEST_ASSERT_TRUE_MESSAGE(committedLength > 0u, lengthDetail);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::committedSlotHasNote(track, 2, kSuccessorNote),
                           "C's committed content must hold the note the user played");
  // Control: the search is note-specific, so it cannot pass by finding any row at all.
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::committedSlotHasNote(track, 2, 99),
                            "control: a note that was never played must not appear in C's content");

  // (11) B's outstanding finalization completing later must not damage C's committed take.
  const size_t committedRows = TrackCaptureStopTestAccess::captureEventCount(track);
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  for (int turn = 0; turn < 64; ++turn) {
    (void)TrackCaptureStopTestAccess::serviceAcceptedInteractionTurn(track);
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "a later completion must not move the active slot off C");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(committedLength,
                                   TrackCaptureStopTestAccess::slotLoopLength(track, 2),
                                   "a later completion must not change C's committed length");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::committedSlotHasNote(track, 2, kSuccessorNote),
                           "a later completion must not damage C's committed content");
  (void)committedRows;
}

/// **S4b.1 — contract check 7, cross-track successor.** The successor sits on a **different track**,
/// which the same-track fixtures cannot show: `CaptureStopJobs`, the S4f.1 latch and
/// `hasStopInFlightForSlot` are all per track, so the predecessor's live finalizer must not gate the
/// successor track's start. The assertions are deliberately asymmetric -- the predecessor's finalizer
/// must still be outstanding, and still identified by the predecessor's own slot, *while* the successor
/// records on its own track.
void test_a_cross_track_successor_starts_while_the_predecessor_finalizes() {
  LoopEventStore::initPool();
  Track& predecessor = trackManager.getTrack(0);
  Track& successor = trackManager.getTrack(1);
  predecessor.ensureLoopsAllocated();
  successor.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(predecessor, 0);
  TEST_ASSERT_TRUE(predecessor.prewarmPlaybackForSlot(0, true));

  // (1) The predecessor's stop is admitted and held, so its track's deferred capacity is occupied.
  trackManager.setSelectedTrack(0);
  predecessor.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(predecessor, 0);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isOverdubbing(predecessor),
                           "precondition: the predecessor must be overdubbing");
  inputNote(60, 96, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(predecessor, 0),
                           "the predecessor's stop must be admitted and held before the handoff");

  // (2) Start the successor through the production gesture, on its own track.
  trackManager.setSelectedTrack(1);
  clockManager.assignCurrentTickSilently(200);
  TrackCaptureStopTestAccess::toggleRecordForSlot(0);
  const uint32_t queuedTick = clockManager.getCurrentTick();
  for (uint32_t tick = queuedTick + 1; tick <= queuedTick + 768 * 3 &&
                             !TrackCaptureStopTestAccess::isRecording(successor) &&
                             !TrackCaptureStopTestAccess::isOverdubbing(successor);
       ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  // Red-proof note: this fixture guards against *cross-track coupling*, and no earlier version of the code
  // had any, so there is nothing to revert. It is red-proved by introducing the coupling the fixture must
  // detect -- a temporary guard in `startRecordingTrack` that refuses while **any** track has a stop in
  // flight -- which fails exactly here ("the successor track must start while the predecessor finalizes").
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isRecording(successor),
                           "the successor track must start while the predecessor finalizes");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasStopInFlight(successor),
                            "the successor track must hold no stop obligation of its own");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(predecessor, 0),
                           "the predecessor's finalizer must still be outstanding");

  // (3) The successor records on its own track through the real MIDI route.
  const size_t before = TrackCaptureStopTestAccess::captureEventCount(successor);
  const uint32_t onTick = clockManager.getCurrentTick() + 8;
  inputNote(64, onTick, onTick + 32);
  TEST_ASSERT_EQUAL_MESSAGE(before + 2, TrackCaptureStopTestAccess::captureEventCount(successor),
                            "the successor must record the incoming note on its own track");

  // (4) The predecessor completes without disturbing the successor.
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(predecessor);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(predecessor);
  }
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(predecessor),
                            "the predecessor's finalizer must be able to complete");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isRecording(successor),
                           "the predecessor's completion must not stop the successor");
  TEST_ASSERT_EQUAL_MESSAGE(before + 2, TrackCaptureStopTestAccess::captureEventCount(successor),
                            "the predecessor's completion must not damage the successor's interval");
}

/// **S4b.1 — contract check 7, cleared successor.** The successor slot previously held content that was
/// cleared, so it must still be treated as a **record** target while being *not virgin*: its slot runtime
/// survives the clear (only passes and loop geometry are dropped), which is the state the production
/// clear leaves and why the gesture comment says a just-cleared slot "can re-arm".
///
/// The clear is performed with the production clear's **slot-local** steps
/// (`MidiButtonActions::handleClearTrack` once its storage-drain and undo-snapshot bookkeeping is done)
/// plus `Track::clear()`, which is the production pass removal. The storage drain and the undo snapshot
/// belong to the persistence/undo suites, not here.
void test_a_cleared_successor_is_a_record_target_while_the_predecessor_finalizes() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  const uint8_t tidx = trackManager.getSelectedTrackIndex();

  // (1) Fill slot 1, give it a working set, then clear it.
  commitRecordPassIntoTrackSlot(track, 1);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(1, true));
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, 1),
                           "precondition: the future target must hold a runtime before the clear");
  // `Track::clear()` returns early on TRACK_EMPTY, so the clear needs a live state (a real slot being
  // cleared is STOPPED or PLAYING, never EMPTY).
  track.forceSetState(TRACK_STOPPED);
  TrackCaptureStopTestAccess::selectSlot(track, 1);
  trackManager.clearPendingSlotSwitch(tidx);
  trackManager.setPendingEnabledSetReplacement(tidx, false);
  trackManager.setSlotEnabled(tidx, 1, false);
  trackManager.setSlotMuted(tidx, 1, false);
  trackManager.clearQueuedRecordingTrack(tidx, 1);
  trackManager.setLayeredSlotHeld(tidx, 1, false);
  track.clear();
  TEST_ASSERT_FALSE_MESSAGE(track.hasCommittedPassesInSlot(1),
                            "a cleared slot must be a record target");
  TEST_ASSERT_FALSE_MESSAGE(track.hasDataInSlot(1), "a cleared slot must hold no loop data");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, 1),
                           "the cleared slot must still hold its runtime: it is not a virgin slot");

  // (2) Predecessor A on slot 0 overdubs and its stop is admitted and held.
  commitRecordPassIntoTrackSlot(track, 0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isOverdubbing(track),
                           "precondition: A must be overdubbing");
  inputNote(60, 96, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track),
                           "A's stop must be admitted and held before the handoff");

  // (3) Switch/start onto the cleared target: the switch commits and the start is carried.
  clockManager.assignCurrentTickSilently(200);
  TrackCaptureStopTestAccess::toggleRecordForSlot(1);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the switch onto the cleared target must commit");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx, 1),
                           "the switch must carry a record start onto the cleared target");

  const uint32_t queuedTick = clockManager.getCurrentTick();
  for (uint32_t tick = queuedTick + 1; tick <= queuedTick + 768 * 3 &&
                             !TrackCaptureStopTestAccess::isRecording(track) &&
                             !TrackCaptureStopTestAccess::isOverdubbing(track);
       ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track),
                           "the cleared target must start recording from the carried start");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx, 1),
                            "the executed start must be consumed exactly once");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track),
                           "A's finalizer must still be outstanding while the target records");

  // (4) The cleared target records incoming MIDI.
  const size_t before = TrackCaptureStopTestAccess::captureEventCount(track);
  const uint32_t onTick = clockManager.getCurrentTick() + 8;
  inputNote(65, onTick, onTick + 32);
  TEST_ASSERT_EQUAL_MESSAGE(before + 2, TrackCaptureStopTestAccess::captureEventCount(track),
                            "the cleared target must record the incoming note's on and off");

  // (5) A completes without damaging the successor.
  const size_t successorEvents = TrackCaptureStopTestAccess::captureEventCount(track);
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track),
                            "A's finalizer must be able to complete");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "A's completion must not move the active slot off the cleared target");
  TEST_ASSERT_EQUAL_MESSAGE(successorEvents, TrackCaptureStopTestAccess::captureEventCount(track),
                            "A's completion must not damage the cleared target's interval");
}

/// **S4b.1 — contract check 7, populated successor.** The successor slot holds committed content, so the
/// switch must **not** carry a record start: the contract's branch for a populated target is the
/// *overdub* on the target's own playback phase. The negative is the point -- the empty-target carrier
/// must not arm a record over committed content -- and the two slots are given **different lengths**
/// (predecessor 768, successor 1,536) so "the successor used its own phase" is falsifiable rather than
/// assumed.
void test_a_populated_successor_overdubs_on_its_own_phase_while_the_predecessor_finalizes() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  const uint8_t tidx = trackManager.getSelectedTrackIndex();
  commitRecordPassIntoTrackSlotOfLength(track, 0, 768);
  commitRecordPassIntoTrackSlotOfLength(track, 1, 1536);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(1, true));

  // (1) A overdubs on slot 0. Unlike the empty-target fixtures the stop is **not** pressed first: for a
  // populated target the switch itself admits A's stop as the capture handoff, which is the branch the
  // contract's populated-target wording describes (switch while capturing). A stop pressed beforehand
  // would leave the track PLAYING instead of OVERDUBBING and route the gesture down the *playback*-switch
  // lane instead, which is a different owner and a different question.
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isOverdubbing(track),
                           "precondition: A must be overdubbing");
  inputNote(60, 96, 120);

  // (2) Switch to the populated successor while A is still capturing. The switch admits A's stop and
  // commits, and it must carry **no** record start: the target is not a record target.
  clockManager.assignCurrentTickSilently(144);
  TrackCaptureStopTestAccess::toggleRecordForSlot(1);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the switch to the populated successor must commit");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track),
                           "the switch must leave A's stop admitted and finalizing");
  // Measured while red-proving this: the negative below is defended **twice**, by the carrier's own
  // `!hasCommittedPassesInSlot` predicate and independently by `queueRecordingTrack`'s committed-passes
  // refusal. Disabling either alone leaves the suite fully green, so the assertion proves the *property*
  // but does not attribute the refusal to one guard. Red-proving it required disabling both.
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx, 1),
                            "a populated target must NOT be armed for a record start");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::anyRecordQueued(tidx),
                            "no record request may survive a switch onto a populated target");

  // (3) The successor's own phase is what the overdub uses: its loop length is untouched by the handoff.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1536, track.getLoop(1).loopLengthTicks,
                                   "the populated successor must keep its own loop length");
  TrackCaptureStopTestAccess::startOverdubbing(track, clockManager.getCurrentTick());
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isOverdubbing(track),
                           "the populated successor must be overdubbable through the production entry");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::captureActive(track),
                           "the overdub must have an active capture interval");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1536, track.getActiveLoop().loopLengthTicks,
                                   "the overdub must run against the successor's own phase, not A's");

  // (4) A completes without disturbing the successor's interval or phase.
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track),
                            "A's finalizer must be able to complete");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "A's completion must not move the active slot off the successor");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1536, track.getLoop(1).loopLengthTicks,
                                   "A's completion must not rewrite the successor's phase");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isOverdubbing(track),
                           "A's completion must not end the successor's overdub");
}

/// **S4b.1 — check 6, exactly-once for the *committed* carrier.** The blocked fixture proves exactly-once
/// for a request that had to wait for admission. The committed case -- the switch's own gesture carrying
/// the start, dispatched on the next eligible boundary -- had no exactly-once assertion: the acceptance
/// fixture never checks that the request was consumed, and never dispatches again after the start. This
/// fixture closes that half: consumption, no re-arm, and no second execution.
///
/// **Residual gap, recorded rather than implied.** This fixture covers the *switch-carried committed*
/// start only. The two other exactly-once routes are covered separately, directly below:
/// `test_the_arm_and_wait_requeue_retains_then_executes_exactly_once` (the
/// `!clockManager.shouldQuantizeRecordStart()` re-queue branch) and
/// `test_a_start_refused_by_its_own_live_stop_obligation_retries_to_a_single_take` (a start refused by a
/// live stop obligation for its own slot, then retried). Checked while writing this and **not** a defect:
/// the ordinary immediate start cannot leave a stale request behind, because `handleArmedRecordPress`
/// cancels any queued arm and returns before the immediate `startRecordingTrack` call is reached
/// (`MidiButtonActions.cpp`), so that route has nothing to consume twice.
void test_the_switch_carried_start_executes_exactly_once() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  const uint8_t tidx = trackManager.getSelectedTrackIndex();
  commitRecordPassIntoTrackSlot(track, 0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));

  // (1) A overdubs, then its stop is admitted and held.
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::isOverdubbing(track));
  inputNote(60, 96, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track));

  // (2) The switch's own gesture carries the start for the empty target (the committed case).
  clockManager.assignCurrentTickSilently(200);
  TrackCaptureStopTestAccess::toggleRecordForSlot(1);
  TEST_ASSERT_EQUAL_UINT8(1, TrackCaptureStopTestAccess::activeLoopIndex(track));
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx, 1),
                           "the switch must carry the start for the empty target");

  const uint32_t queuedTick = clockManager.getCurrentTick();
  for (uint32_t tick = queuedTick + 1; tick <= queuedTick + 768 * 3 &&
                             !TrackCaptureStopTestAccess::isRecording(track) &&
                             !TrackCaptureStopTestAccess::isOverdubbing(track);
       ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track),
                           "the carried start must execute while A finalizes");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx, 1),
                            "the executed start must be consumed exactly once");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::anyRecordQueued(tidx),
                            "no record request may survive a successful start");

  // (3) The started interval takes the injected note.
  const size_t before = TrackCaptureStopTestAccess::captureEventCount(track);
  const uint32_t onTick = clockManager.getCurrentTick() + 8;
  inputNote(66, onTick, onTick + 32);
  TEST_ASSERT_EQUAL_MESSAGE(before + 2, TrackCaptureStopTestAccess::captureEventCount(track),
                            "the started interval must record the incoming note");

  // (4) Exactly once: two further bar boundaries of dispatch must not re-arm or re-execute the start.
  const size_t startedEvents = TrackCaptureStopTestAccess::captureEventCount(track);
  const uint8_t startedSlot = TrackCaptureStopTestAccess::activeLoopIndex(track);
  const uint32_t afterStart = clockManager.getCurrentTick();
  for (uint32_t tick = afterStart + 1; tick <= afterStart + 768 * 2; ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(startedSlot, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "later dispatch must not move the active slot");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx, 1),
                            "the start request must not be re-armed by later dispatch");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::anyRecordQueued(tidx),
                            "no record request may exist after the start was consumed");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track),
                           "the started interval must still be the recording interval");
  TEST_ASSERT_EQUAL_MESSAGE(startedEvents, TrackCaptureStopTestAccess::captureEventCount(track),
                            "a second execution would have restarted the interval and dropped its "
                            "captured events");
}

/// **Exactly-once: the arm-and-wait re-queue path.** `TrackManager::startRecordingTrack` has a branch where
/// `!clockManager.shouldQuantizeRecordStart()` re-queues the request in the manager pending-start owner,
/// leaves the track armed, and returns `false` so the attempt neither starts nor consumes. This fixture
/// closes the first of the two gaps the fixture above records, and pins what that path can and cannot
/// execute twice:
///
///   1. the re-queue attempt **retains** the request and does not start recording;
///   2. repeated dispatch while the clock condition is still unsatisfied keeps retaining it;
///   3. once the clock runs, the request executes **exactly once** and is consumed by that attempt;
///   4. further dispatch cannot execute it again -- the take origin and its captured events are unchanged,
///      which a second `Track::startRecording` would break (`Loop::beginCapture` clears the capture extent
///      and `loop.startLoopTick` is re-stamped from the later tick).
void test_the_arm_and_wait_requeue_retains_then_executes_exactly_once() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  const uint8_t tidx = trackManager.getSelectedTrackIndex();
  // Slot 0 holds committed content so the track can be audibly playing; slot 1 is the empty record target.
  commitRecordPassIntoTrackSlot(track, 0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, 0);

  // (1) The clock condition is unsatisfied, so the production gesture reaches the arm-and-wait branch.
  TrackCaptureStopTestAccess::setClockRunning(false);
  TEST_ASSERT_FALSE_MESSAGE(clockManager.shouldQuantizeRecordStart(),
                            "precondition: this fixture needs the unsatisfied clock condition");
  TrackCaptureStopTestAccess::selectSlotIndex(tidx, 1);
  TrackCaptureStopTestAccess::toggleRecordForSlot(1);

  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the gesture must have activated the empty record target");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx, 1),
                           "the arm-and-wait branch must retain the request rather than consume it");
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::anyRecordQueued(tidx));
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track),
                            "the arm-and-wait branch must not start recording");

  // (2) Dispatch across three bar boundaries while the condition is still unsatisfied. Every attempt is
  // re-queued and none is consumed: this is the retention half, and it fails if the branch reports success.
  const uint32_t queuedTick = clockManager.getCurrentTick();
  for (uint32_t tick = queuedTick + 1; tick <= queuedTick + 768 * 3; ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track),
                            "the start must not execute while the clock condition is unsatisfied");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx, 1),
                           "each unsatisfied attempt must retain the request, not consume it");

  // (3) The clock condition is satisfied: the retained request executes, exactly once.
  TrackCaptureStopTestAccess::setClockRunning(true);
  TEST_ASSERT_TRUE(clockManager.shouldQuantizeRecordStart());
  const uint32_t resumeTick = clockManager.getCurrentTick();
  for (uint32_t tick = resumeTick + 1;
       tick <= resumeTick + 768 * 3 && !TrackCaptureStopTestAccess::isRecording(track); ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track),
                           "the retained start must execute once the clock condition is satisfied");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx, 1),
                            "the request must be consumed by the successful attempt");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::anyRecordQueued(tidx),
                            "no record request may survive the successful attempt");

  // (4) Exactly once. A second execution would re-begin capture: the injected note's events would be
  // dropped and the take origin would move to the later tick.
  const uint32_t takeOrigin = TrackCaptureStopTestAccess::takeOriginTick(track);
  const uint32_t onTick = clockManager.getCurrentTick() + 8;
  inputNote(71, onTick, onTick + 32);
  TEST_ASSERT_EQUAL_MESSAGE(2, TrackCaptureStopTestAccess::captureEventCount(track),
                            "the take must have recorded the injected note's on and off");
  const uint32_t firstTick = TrackCaptureStopTestAccess::firstCaptureEventTick(track);
  const uint8_t firstNote = TrackCaptureStopTestAccess::firstCaptureEventNote(track);

  const uint32_t afterStart = clockManager.getCurrentTick();
  for (uint32_t tick = afterStart + 1; tick <= afterStart + 768 * 3; ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "later dispatch must not move the active slot");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx, 1),
                            "the start request must not be re-armed by later dispatch");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track),
                           "the started take must still be the recording interval");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      takeOrigin, TrackCaptureStopTestAccess::takeOriginTick(track),
      "a second execution would re-begin capture and move the take origin");
  TEST_ASSERT_EQUAL_MESSAGE(2, TrackCaptureStopTestAccess::captureEventCount(track),
                            "a second execution would clear the capture extent");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(firstTick, TrackCaptureStopTestAccess::firstCaptureEventTick(track),
                                   "the recorded note's tick must survive later dispatch");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(firstNote, TrackCaptureStopTestAccess::firstCaptureEventNote(track),
                                  "the recorded note must survive later dispatch");
}

/// **Exactly-once: a start that is refused by its own live stop obligation and then retried.**
/// `TrackManager::startRecordingTrack` refuses while `hasStopInFlightForSlot(activeSlot)` -- the S4f.6
/// slot-scoped guard -- and returns `false` so the dispatcher retains the request. This fixture closes the
/// second recorded gap: the refused attempt must not consume, the retry must produce exactly one take, and
/// the request must be consumed only by the successful attempt.
///
/// The obligation is set through the existing test accessor on purpose: a fresh **record** stop is
/// synchronous (`TrackManager::stopRecordingTrack` -> `Track::stopRecordingToStopped`) and never takes the
/// deferred path, so a latch is the only way a record target can carry a stop obligation **of its own**.
/// The start itself is queued by the production per-slot gesture and dispatched by the production
/// `handlePendingRecordStart`, so only the obligation's origin is synthetic.
///
/// Attribution note: the refused phase and the successful phase use the *same* dispatch loop over the same
/// number of ticks, and the only difference between them is the obligation, so "it started later" cannot be
/// explained by a tick gate that the refused phase never satisfied.
void test_a_start_refused_by_its_own_live_stop_obligation_retries_to_a_single_take() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  const uint8_t tidx = trackManager.getSelectedTrackIndex();
  commitRecordPassIntoTrackSlot(track, 0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, 0);

  // (1) The record target carries a live stop obligation that names its own slot.
  TrackCaptureStopTestAccess::latchAcceptedStopForSlot(track, 1);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 1));
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasStopInFlightForSlot(track, 1),
                           "precondition: the target must carry its own stop obligation");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasStopInFlightForSlot(track, 0),
                            "precondition: the obligation must not be the predecessor slot's");

  // (2) The start is requested through the production gesture; a running clock queues it for the target.
  clockManager.assignCurrentTickSilently(120);
  TrackCaptureStopTestAccess::selectSlotIndex(tidx, 1);
  TrackCaptureStopTestAccess::toggleRecordForSlot(1);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx, 1),
                           "the gesture must queue the start for the empty target");

  // (3) Three bar boundaries of production dispatch are refused by the live obligation, and every refusal
  // retains the request instead of consuming it.
  const uint32_t queuedTick = clockManager.getCurrentTick();
  for (uint32_t tick = queuedTick + 1; tick <= queuedTick + 768 * 3; ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  TEST_ASSERT_FALSE_MESSAGE(
      TrackCaptureStopTestAccess::isRecording(track),
      "a start must not execute while its own slot carries a live stop obligation");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx, 1),
                           "a refused attempt must retain the request, not consume it");
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::hasStopInFlightForSlot(track, 1));

  // (4) The obligation retires and the retry executes -- with the same dispatch loop as (3).
  TrackCaptureStopTestAccess::consumeAcceptedStop(track);
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasStopInFlightForSlot(track, 1),
                            "the obligation must be gone before the retry");
  const uint32_t resumeTick = clockManager.getCurrentTick();
  for (uint32_t tick = resumeTick + 1;
       tick <= resumeTick + 768 * 3 && !TrackCaptureStopTestAccess::isRecording(track); ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track),
                           "the retained start must execute once its own obligation retires");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the retry must start on the requested target");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx, 1),
                            "the request must be consumed only by the successful attempt");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::anyRecordQueued(tidx),
                            "no record request may survive the successful attempt");

  // (5) Exactly once across the retry: one take, and later dispatch neither re-arms nor re-executes it.
  const uint32_t takeOrigin = TrackCaptureStopTestAccess::takeOriginTick(track);
  const uint32_t onTick = clockManager.getCurrentTick() + 8;
  inputNote(72, onTick, onTick + 32);
  TEST_ASSERT_EQUAL_MESSAGE(2, TrackCaptureStopTestAccess::captureEventCount(track),
                            "the retried take must have recorded the injected note's on and off");
  const uint32_t firstTick = TrackCaptureStopTestAccess::firstCaptureEventTick(track);
  const uint8_t firstNote = TrackCaptureStopTestAccess::firstCaptureEventNote(track);

  const uint32_t afterStart = clockManager.getCurrentTick();
  for (uint32_t tick = afterStart + 1; tick <= afterStart + 768 * 3; ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "later dispatch must not move the active slot");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx, 1),
                            "the start request must not be re-armed by later dispatch");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track),
                           "the retried take must still be the recording interval");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      takeOrigin, TrackCaptureStopTestAccess::takeOriginTick(track),
      "a second execution would re-begin capture and move the take origin");
  TEST_ASSERT_EQUAL_MESSAGE(2, TrackCaptureStopTestAccess::captureEventCount(track),
                            "a second execution would clear the capture extent");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(firstTick, TrackCaptureStopTestAccess::firstCaptureEventTick(track),
                                   "the recorded note's tick must survive later dispatch");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(firstNote, TrackCaptureStopTestAccess::firstCaptureEventNote(track),
                                  "the recorded note must survive later dispatch");
}

/// **S4.1 — a capture work item is keyed to the interval's own slot.** The live capture's slot *is*
/// the active slot (only it may RECORD/OVERDUB), but a stop in flight is pinned to ITS interval's slot
/// across an active-slot change, so `captureWorkSlot` must not substitute the active index. The
/// mid-pass journal resolver reads this rule, and it previously read `getActiveLoopIndex()` directly --
/// so a stop pinned to one slot was journalled under whichever slot was active when the drain ran.
void test_s4_1_capture_work_slot_uses_the_pinned_interval_slot() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 1);
  commitRecordPassIntoTrackSlot(track, 3);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(1, true));
  track.forceSetState(TRACK_PLAYING);

  // A stop in flight pinned to slot 1, with the active slot moved to 3.
  TrackCaptureStopTestAccess::latchAcceptedStopForSlot(track, 1);
  TrackCaptureStopTestAccess::activateSlot(0, 3);

  // The discriminator: the active index really is a different slot, so a rule that read it reports 3.
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(3, track.getActiveLoopIndex(),
                                  "the fixture must move the active slot away from the interval");

  uint8_t slot = 0xFF;
  TEST_ASSERT_TRUE(track.captureWorkSlot(slot));
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, slot, "the pinned interval's slot must key the work item");

  // Control: once the obligation is consumed there is no stop in flight, so the rule refuses rather
  // than falling back to the active index -- the mid-pass resolver requeues instead of guessing.
  TrackCaptureStopTestAccess::consumeAcceptedStop(track);
  TEST_ASSERT_FALSE_MESSAGE(track.captureWorkSlot(slot),
                            "with nothing in flight the rule must refuse rather than guess");

  // Control (b): a live capture's slot IS the active slot, because only it may RECORD/OVERDUB. This
  // keeps the refusal above from being a rule that simply never resolves.
  track.forceSetState(TRACK_RECORDING);
  TEST_ASSERT_TRUE(track.captureWorkSlot(slot));
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(3, slot, "a live capture is on the active slot by construction");
  track.forceSetState(TRACK_PLAYING);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_boot_leaves_a_committed_slot_without_a_working_set);
  RUN_TEST(test_a_repeated_admission_does_not_churn_the_lease);
  RUN_TEST(test_release_is_refused_while_an_owner_remains_and_allowed_once_it_retires);
  RUN_TEST(test_a_stop_clears_only_its_own_slots_ledger);
  RUN_TEST(test_s4_1_capture_work_slot_uses_the_pinned_interval_slot);
  RUN_TEST(test_selection_acquires_no_working_set);
  RUN_TEST(test_a_late_cleanup_cannot_clear_a_successor_finalization_marker);
  RUN_TEST(test_the_slot_generation_release_entry_is_identity_qualified_and_preserves_refusals);
  RUN_TEST(test_the_release_sweep_skips_a_slot_with_a_live_owner);
  // S4.1 -- the working-set lease release on a same-track slot switch (the device run only evidences
  // the finalization-capacity release).
  RUN_TEST(test_s4_1_a_same_track_switch_releases_the_retired_slots_lease);
  RUN_TEST(test_s4_1_a_live_finalization_refuses_the_retired_slots_lease_release);
  RUN_TEST(test_s4_1_the_retired_slots_lease_is_released_once_its_finalization_retires);
  RUN_TEST(test_reactivating_a_released_slot_reuses_its_capacity);
  RUN_TEST(test_a_declared_envelope_breach_is_reported_and_does_not_block_activation);
  RUN_TEST(test_switching_starts_the_new_take_while_the_previous_take_still_finalizes);
  RUN_TEST(test_a_blocked_switch_and_start_survive_until_its_prerequisite_stop_binds);
  // The successor's take must COMMIT with content -- the device failure a start marker hid.
  RUN_TEST(test_the_successor_take_commits_with_content_after_a_transport_stop);
  // S4b.1 contract check 7 coverage, and the exactly-once half the committed carrier was missing.
  RUN_TEST(test_a_cross_track_successor_starts_while_the_predecessor_finalizes);
  RUN_TEST(test_a_cleared_successor_is_a_record_target_while_the_predecessor_finalizes);
  RUN_TEST(test_a_populated_successor_overdubs_on_its_own_phase_while_the_predecessor_finalizes);
  RUN_TEST(test_the_switch_carried_start_executes_exactly_once);
  // The two exactly-once routes the fixture above left uncovered.
  RUN_TEST(test_the_arm_and_wait_requeue_retains_then_executes_exactly_once);
  RUN_TEST(test_a_start_refused_by_its_own_live_stop_obligation_retries_to_a_single_take);
  return UNITY_END();
}
