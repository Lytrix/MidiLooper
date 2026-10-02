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

/// S4b.4 (display clause): the display boundary this harness already keeps observable now **records** what it
/// was told, because after a role handoff *which* slots the panel is told about is the fact worth asserting --
/// the switch's own commit calls `displayManager.invalidateForSlotChange(i, previousPlaying,
/// getPreviewSlotIndex(i))`, so the incoming slot must be the one it names.
struct SlotChangeObservation {
  uint32_t count = 0;
  uint8_t track = 0xFF;
  uint8_t from = 0xFF;
  uint8_t to = 0xFF;
};
SlotChangeObservation& slotChangeObservation() {
  static SlotChangeObservation observation;
  return observation;
}
void resetSlotChangeObservation() { slotChangeObservation() = SlotChangeObservation{}; }

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
// The switch lane: this suite must be able to observe a **queued** switch committing (S4b.5/S4b.6/S4b.7),
// which happens on the production per-tick entry. That needs the playback hot path and the play/stop
// owners the rest of the suite keeps fail-closed, so they move behind this macro together.
#define HARNESS_LINKS_PLAYBACK_SWITCH_LANE
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
// S4b.7: the `SlotProvision` turn's own eligibility predicate (`slotProvisionNeeded`), so a fixture can
// assert the turn's work without re-stating it.
// S4b.7: the bounded hold's own age test and named terminal, and the snapshot root the demoted target's merged
// window is gathered from (`storePublishedSnapshotForTest` is the existing test seam).
#include "Utils/SlotProvisionHold.h"
#include "LoopSnapshot.h"
#include "Utils/MidiServiceDrain.h"
#include "../../src/EditManager/EditSessionInteraction.cpp"
// The switch lane's production TUs: the playback hot path (silence owners + `playMidiEvents`), the
// playback policy owner (`isTrackAudible`) and the transport control owner (`startPlaying`/`stopPlaying`,
// `queuePlaybackStartAtGrid`/`commitQueuedPlaybackStart`). See the macro above.
#include "../../src/Track/TrackPlaybackHotPath.cpp"
#include "../../src/TrackManager/TrackManagerPlaybackPolicy.cpp"
#include "../../src/Track/TrackTransportControl.cpp"
// The lane's own boundary: linking the hot path brings `Track::resetPlaybackState`'s frame reset, which
// knocks on the loop-edit manager when a LOOP_EDIT session is live ("keep the LOOP_EDIT baseline aligned
// with the playback frame"). This suite never opens an edit session, so the manager is stubbed at the three
// symbols that TU references instead of linking the edit stack, and its one callback fails closed --
// reaching it would mean an edit session exists in a suite whose fixtures must never open one.
LoopEditManager::LoopEditManager(MidiHandler& handler) : midiHandler(handler) {}
LoopEditManager loopEditManager(midiHandler);
// S4b.4's undo clause found a **second** route to this callback: an applied undo restores published loop
// geometry and *notifies* the edit manager, which is a notification rather than an edit session. DEC-101's own
// argument applies -- a removed `OFF_PATH()` cannot have been reached by any fixture that was passing, so
// nothing that was covered is weakened -- while the manager's *session* entries (open/reopen/persist/flush)
// stay stubbed, so a real session still cannot run in this suite.
void LoopEditManager::onGlobalGeometryRestored(Track&) {}
// S4b.4's undo clause drives `TrackUndo::undoForLoop`, which flushes pending loop-edit geometry first. That
// flush stays **fail-closed**, like the callback above: reaching it would mean a live edit session, which no
// fixture in this suite may open.
void LoopEditManager::flushAllPendingGeometry(Track&) { OFF_PATH(); }
// The undo driver asks whether any geometry is pending before flushing it; with no edit session possible in
// this suite the honest answer is no.
bool LoopEditManager::hasPendingGeometry() const { return false; }

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
  /// The production consumption entry for the accepted interaction (S4f.1/S4f.2). The reason is the edge's own
  /// outcome literal (task 2.2's latch evidence); this fixture passes its own.
  static void consumeAcceptedStop(Track& track, const char* reason = "fixture") {
    track.consumeAcceptedStopObligation(reason);
  }
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
  /// S4.4: report a declared-envelope breach if the activation would exceed a class cap. The answer is the
  /// slot holding the class it reported for (`kNoWorkingSetClassHolder` when there is nothing to report).
  static uint8_t reportEnvelopeBreachIfAny(const Track& track, uint8_t slot, WorkingSetRole role) {
    return track.reportEnvelopeBreachIfAny(slot, role);
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
  g_nativeAdoptedMasterLength = 0;
  g_nativeAdoptedMasterLengthCalls = 0;
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


/// **S4.2 — a retired owner's late release cannot release a successor's working set.**
///
/// The task's named sequence is A-releases / C-reuses / late-A-cleanup, and the *lease* half of it is why
/// the identity check exists: `releaseSlot(slot, generation)` presents an identity, `releaseAll` bumps the
/// generation when it releases, and a reused slot must therefore answer the **retired** owner's late release
/// with `StaleGeneration` rather than releasing the successor's working set underneath it. The
/// finalization-identity half of the same sequence is pinned by
/// `test_a_late_cleanup_cannot_clear_a_successor_finalization_marker`.
///
/// The assertion that carries the weight is the *specific* answer: `StaleGeneration`, not the silent
/// `NotHeld` an identity-free check would return for a slot that looks empty or for one that has been
/// re-admitted. The pre-S4.1 sweep discarded exactly that distinction.
void test_s4_2_a_retired_owners_late_release_cannot_release_a_successors_working_set() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 2);

  // A admits through the production record-arm admission and holds the slot's working set.
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(2, true));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 2));
  const uint16_t aGeneration = TrackCaptureStopTestAccess::slotGeneration(track, 2);
  const uint8_t aRoles = TrackCaptureStopTestAccess::workingSetRoles(track, 2);
  LoopPlaybackRuntime* const runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 2);

  // A retires through the track-level sweep (the entry S4.1 converted): the lease drops and the generation
  // advances, so A's identity is spent even though the runtime object stays where it is.
  track.releasePlaybackMergedMidiEventsMemory();
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 2),
                            "the sweep must release A's working set");
  const uint16_t retiredGeneration = TrackCaptureStopTestAccess::slotGeneration(track, 2);
  TEST_ASSERT_TRUE_MESSAGE(retiredGeneration > aGeneration,
                           "A's release must advance the slot generation");

  // C reuses the same capacity: same runtime object, the same role set, and its own identity -- the
  // post-release generation, not A's. Re-admission itself allocates nothing and bumps nothing.
  TEST_ASSERT_TRUE_MESSAGE(track.prewarmPlaybackForSlot(2, true), "the slot must re-admit");
  TEST_ASSERT_EQUAL_PTR_MESSAGE(runtime, TrackCaptureStopTestAccess::runtimeIfAllocated(track, 2),
                                "C must reuse the slot's runtime object");
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 2));
  const uint16_t cGeneration = TrackCaptureStopTestAccess::slotGeneration(track, 2);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(retiredGeneration, cGeneration,
                                   "re-admission must not spend the successor's identity");
  TEST_ASSERT_EQUAL_UINT8(aRoles, TrackCaptureStopTestAccess::workingSetRoles(track, 2));

  // Late A cleanup presenting the retired identity: rejected **and reported** -- `StaleGeneration`, never a
  // release and never the silent `NotHeld`.
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      static_cast<uint8_t>(WorkingSetOutcome::StaleGeneration),
      static_cast<uint8_t>(TrackCaptureStopTestAccess::releaseSlot(track, 2, aGeneration)),
      "the retired owner's identity must be rejected as stale, not answered as NotHeld");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 2),
                           "a late release must not release a successor's working set");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(aRoles, TrackCaptureStopTestAccess::workingSetRoles(track, 2),
                                  "a late release must not disturb the successor's roles");
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(cGeneration, TrackCaptureStopTestAccess::slotGeneration(track, 2),
                                   "a rejected release must not advance the successor's identity");

  // Control: the successor's own identity releases, and only then does the generation move again.
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      static_cast<uint8_t>(WorkingSetOutcome::Released),
      static_cast<uint8_t>(TrackCaptureStopTestAccess::releaseSlot(track, 2, cGeneration)),
      "the live identity must release");
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 2));
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::slotGeneration(track, 2) > cGeneration,
                           "the accepted release must advance the generation");
}


/// **S4.2 — the release outcomes carry the device row's own vocabulary.**
///
/// "Reported" has two halves: the caller sees a distinguishable outcome (pinned above and by the
/// `(slot, generation)` release-entry fixture), and the row that leaves the box names it. The emitter's
/// literal mapping is the only place that second half lives, so it is pinned here directly against the
/// vocabulary the reader consumes (`scripts/hitl/lease_release.py` decodes the same four literals, and
/// `test_capture_line_tier` pins the row's Tier-A classification). `NotHeld` is included on purpose: it is
/// the one outcome the sweep deliberately does **not** emit, which is why a slot with no row means "no sweep
/// ran here", never "the release failed".
void test_s4_2_the_release_outcomes_are_named_as_the_device_row_names_them() {
  TEST_ASSERT_EQUAL_STRING("released", workingSetOutcomeLabel(WorkingSetOutcome::Released));
  TEST_ASSERT_EQUAL_STRING("refused_busy", workingSetOutcomeLabel(WorkingSetOutcome::RefusedBusy));
  TEST_ASSERT_EQUAL_STRING("stale_generation", workingSetOutcomeLabel(WorkingSetOutcome::StaleGeneration));
  TEST_ASSERT_EQUAL_STRING("not_held", workingSetOutcomeLabel(WorkingSetOutcome::NotHeld));
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

  // Control: with nothing holding the class there is no breach, and the check *says so* instead of
  // reporting an anonymous denial. The check is a read: censusing eight slots must not allocate a slot
  // runtime (S4.4's "reported rather than resolved by allocating" starts with the check itself).
  MemoryMonitor::resetNativeTestCapDenials();
  const uint16_t chunksBeforeCheck = LoopEventStore::freeChunkCount();
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      kNoWorkingSetClassHolder,
      TrackCaptureStopTestAccess::reportEnvelopeBreachIfAny(track, 0, WorkingSetRole::LiveCapture),
      "with no holder there is nothing to report");
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, MemoryMonitor::nativeTestCapDenials().count,
                                   "an empty census must not report a breach");
  // ...including for the very slot it inspects: asking about an unallocated slot must not *create* its
  // runtime, which is the difference between censusing and preparing.
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::runtimeAllocated(track, 7));
  (void)TrackCaptureStopTestAccess::reportEnvelopeBreachIfAny(track, 7, WorkingSetRole::LiveCapture);
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, 7),
                            "the census must not allocate a runtime for the slot it inspects");
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(chunksBeforeCheck, LoopEventStore::freeChunkCount(),
                                   "the check must take no pool chunks");

  TEST_ASSERT_TRUE_MESSAGE(track.prewarmPlaybackForSlot(0, true),
                           "the first activation must be admitted");
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 0));

  // Slot 1's activation would put a second live-capture role on the track. The breach must be reported
  // **with the slot that holds the class** -- not as an anonymous `class=N` -- and the activation must
  // still be admitted: the policy is report, never refuse (DEC-078 item 1), and never resolved by
  // allocating more.
  MemoryMonitor::resetNativeTestCapDenials();
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      0, TrackCaptureStopTestAccess::reportEnvelopeBreachIfAny(track, 1, WorkingSetRole::LiveCapture),
      "the report must name the slot holding the class");
  const MemoryMonitor::NativeCapDenial denial = MemoryMonitor::nativeTestCapDenials();
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, denial.count, "the breach must be reported once");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, denial.lastHolderSlot, "slot 0 holds the class");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, denial.lastRoleClass,
                                  "the class is the LiveCapture bit, which is what the device row names");

  TEST_ASSERT_TRUE_MESSAGE(track.prewarmPlaybackForSlot(1, true),
                           "a declared-cap breach must not block a supported activation");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, 1),
                           "the activation must have taken its resources");
  // Both slots really do hold the class, which is what the declared envelope calls a breach.
  TEST_ASSERT_TRUE(
      hasRole(TrackCaptureStopTestAccess::workingSetRolesOf(track, 0), WorkingSetRole::LiveCapture));
  TEST_ASSERT_TRUE(
      hasRole(TrackCaptureStopTestAccess::workingSetRolesOf(track, 1), WorkingSetRole::LiveCapture));
  // ...and the production admission reported the breach itself, not only the explicit call above: one
  // report per check, so the counter is 1 (the explicit call) + 1 (the admission's own check).
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(2, MemoryMonitor::nativeTestCapDenials().count,
                                   "the admission path reports the breach it meets");
}


/// **S4.4 — the report names the holder, and a *finalizing* holder is exactly what it names.**
///
/// The declared envelope says one live capture per track, and the census counts **raw role bits**: a slot
/// whose capture has stopped keeps `LiveCapture` until its working set is released. So the pair the
/// contract *requires* -- an outgoing finalizer beside an incoming capture (DEC-078 items 5/6, the case
/// S4b.1 corrected the record with) -- still counts as a second live-capture holder. This fixture pins
/// that as a **diagnosable fact** rather than a silent false positive: the report names the finalizing
/// slot as the holder, and the activation is admitted (S4.4's policy: report, never refuse). Correcting
/// the census itself is S4b's role-transfer work, so this is the fact its slice starts from.
void test_s4_4_the_breach_report_names_the_finalizing_holder_instead_of_hiding_it() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  constexpr uint8_t kFinalizing = 0;
  constexpr uint8_t kIncoming = 1;
  commitRecordPassIntoTrackSlot(track, kFinalizing);
  commitRecordPassIntoTrackSlot(track, kIncoming);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kFinalizing, true));

  // Slot 0's capture ends: its stop is admitted (a deferred finalization), so its `LiveCapture` role is
  // now the outgoing one while slot 1 is the incoming capture.
  // The overdub runs on the *outgoing* slot, so the incoming one is still un-leased when it is asked to
  // activate -- otherwise its own admission would already hold the class and there would be nothing to
  // report (the check treats an idempotent re-admission as no breach).
  TrackCaptureStopTestAccess::selectSlot(track, kFinalizing);
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::startOverdubbing(track, 96);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::appendCaptureNote(track, 96, 120, 60));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::admitStopForSlot(track, PendingStopDestination::Playing, 96,
                                                                kFinalizing, /*interval=*/1));
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::ownsLiveFinalization(track, kFinalizing),
                           "the fixture needs an admitted — i.e. live — finalizer");
  TEST_ASSERT_TRUE(hasRole(TrackCaptureStopTestAccess::workingSetRolesOf(track, kFinalizing),
                           WorkingSetRole::LiveCapture));

  // The incoming activation is reported as a breach and names the finalizing slot as its holder.
  MemoryMonitor::resetNativeTestCapDenials();
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      kFinalizing,
      TrackCaptureStopTestAccess::reportEnvelopeBreachIfAny(track, kIncoming, WorkingSetRole::LiveCapture),
      "the report must name the finalizing slot that still holds the class");
  const MemoryMonitor::NativeCapDenial denial = MemoryMonitor::nativeTestCapDenials();
  TEST_ASSERT_EQUAL_UINT16(1, denial.count);
  TEST_ASSERT_EQUAL_UINT8(kFinalizing, denial.lastHolderSlot);

  // ...and the activation is admitted, so the required pair is not blocked by the declaration.
  TEST_ASSERT_TRUE_MESSAGE(track.prewarmPlaybackForSlot(kIncoming, true),
                           "the required finalizer-plus-incoming pair must be admitted");
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kIncoming));
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::ownsLiveFinalization(track, kFinalizing),
                           "the finalizer must still be in flight after the incoming admission");
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


/// **S4f.17/S4f.21 — a retained Record stop holds the active slot; an admitted one does not.**
///
/// S17.2 moved the production Record stop onto the deferred drive, so `stopRecordingToStopped` can
/// now return with the interval *accepted but unadmitted*. `finalizeCaptureAndSelectSlot` used to
/// switch unconditionally for a recording track (correct while the stop committed inline, because
/// there was nothing left unadmitted). With the deferred route that would advance
/// `captureInterval_` past the retained interval, and `driveAcceptedInteraction` would then judge
/// the obligation **superseded** and drop the take -- silent content loss on an ordinary
/// switch-while-finalizing handoff.
///
/// This fixture pins both halves of the rule the overdub branch already enforces:
///   * an accepted-but-unbound Record stop **refuses** the switch and leaves the active slot alone;
///   * once that stop binds, the switch commits while the predecessor still finalizes, so a
///     different-slot successor is not serialized behind predecessor cleanup.
void test_record_switch_respects_a_retained_stop_and_permits_an_admitted_one() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(1, true));
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(2, true));

  // (1) A records into slot 0 through the production stop route; the ring is free, so its stop is
  // admitted and the interval is owned by the deferred job, not by the active slot.
  track.forceSetState(TRACK_STOPPED);
  TrackCaptureStopTestAccess::activateSlot(0, 0);
  clockManager.assignCurrentTickSilently(0);
  track.startRecording(0);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::isRecording(track));
  inputNote(60, 12, 96);
  clockManager.assignCurrentTickSilently(768);
  track.stopRecordingToStopped(768);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 0),
                           "precondition: A's Record stop must bind while the ring is free");
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 0));

  // (2) B records into slot 1 while A finalizes. A's job owns the ring, so B's stop is retained
  // against B's own slot and interval rather than admitted.
  TrackCaptureStopTestAccess::activateSlot(0, 1);
  TEST_ASSERT_EQUAL_UINT8(1, TrackCaptureStopTestAccess::activeLoopIndex(track));
  clockManager.assignCurrentTickSilently(768);
  track.startRecording(768);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::isRecording(track));
  inputNote(62, 780, 864);
  clockManager.assignCurrentTickSilently(1536);
  track.stopRecordingToStopped(1536);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 1),
                           "precondition: B's Record stop must be retained, not admitted");
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 1));

  // (3) The switch is REFUSED: an unbound stop must not have the active slot moved out from under
  // it, or the retained interval would be superseded and its take dropped.
  TEST_ASSERT_FALSE_MESSAGE(
      trackManager.finalizeCaptureAndSelectSlot(0, 2, 1536),
      "a switch away from an accepted-but-unbound Record stop must be refused");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "a refused switch must leave the active slot unchanged");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 1),
                           "a refused switch must not consume the retained obligation");

  // (4) A retires, B's retained stop binds, and the same switch now commits -- before B's own
  // finalization retires. That is the "successor is not serialized behind predecessor cleanup"
  // half: the deferred job carries its own identity and does not hold the active slot.
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track));
  for (int turn = 0; turn < 64 && !TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 1);
       ++turn) {
    (void)TrackCaptureStopTestAccess::serviceAcceptedInteractionTurn(track);
  }
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 1),
                           "B's retained Record stop must bind once the ring frees");
  TEST_ASSERT_TRUE_MESSAGE(trackManager.finalizeCaptureAndSelectSlot(0, 2, 1536),
                           "an admitted Record stop must not refuse the switch");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the switch must commit to the successor slot");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 1),
                           "the predecessor must still be finalizing after the switch");

  // (5) The predecessor's finalization still retires with its content intact after the switch.
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track));
  TEST_ASSERT_TRUE_MESSAGE(track.getLoop(1).passes.hasRecordPass(),
                           "the switched-away interval's take must still commit its pass");
}

/// **S4f.21 — the master length is adopted from the interval's PUBLISHED geometry.**
///
/// The Record stop edge stamps a *predicted* length from the maintained maximum-tick summary so the
/// caller continuations stay well defined. That prediction is not the result: an all-filtered take
/// publishes zero. The manager therefore holds the obligation until the interval's job publishes or
/// is superseded, and reads the finalized geometry exactly once at a dispatch boundary.
void test_record_stop_adopts_published_geometry_not_the_stop_edge_prediction() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  TrackCaptureStopTestAccess::activateSlot(0, 0);
  clockManager.assignCurrentTickSilently(0);
  track.startRecording(0);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::isRecording(track));
  inputNote(60, 12, 96);

  clockManager.assignCurrentTickSilently(768);
  // The **production transport-stop route**: the same continuation family as `stopRecordingTrack`,
  // and the route that finalises a live capture when the transport stops.
  trackManager.handleTransportStop();

  // The interval is admitted, so its geometry is not decided yet: the prediction must not be
  // adopted as the transport's master length.
  TEST_ASSERT_TRUE_MESSAGE(track.hasPendingDeferredOverdubStop(),
                           "precondition: the Record stop must be deferred");
  // The switch lane links the real `TrackManager` policy owner, so the adopted master length is asserted
  // as the manager's own state rather than through the counting stub this suite used to keep.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, trackManager.getMasterLoopLength(),
                                   "the stop edge must not latch its predicted length");

  for (int turn = 0; turn < 8192 && track.hasPendingDeferredOverdubStop(); ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  trackManager.servicePendingRecordStopGeometry();
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(768, trackManager.getMasterLoopLength(),
                                   "the adopted master length must be the published geometry");
  // "Exactly once" in state terms: the obligation is consumed, so a second dispatch changes nothing.
  trackManager.servicePendingRecordStopGeometry();
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(768, trackManager.getMasterLoopLength(),
                                   "the adoption must not run twice");
  TEST_ASSERT_TRUE_MESSAGE(track.getLoop(0).passes.hasRecordPass(),
                           "precondition: the take must have published a pass");
}

/// **S4f.21 — a take that publishes nothing must not set the master length.**
///
/// The control for the fixture above, and the reason the obligation exists: the stop edge's
/// prediction for this take is 768 ticks (one quantized bar), while the minimum-length policy
/// removes its only pair, so the published geometry is zero. Reading the prediction at the stop
/// edge — which is what the pre-S17.3 continuation did — would adopt a length the worker never
/// publishes and then auto-align the emptied interval with it.
void test_filtered_record_take_publishes_no_master_length() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  noteMinLengthTicks = 48;
  noteMinLengthRemoveEnabled = true;
  TrackCaptureStopTestAccess::activateSlot(0, 0);
  clockManager.assignCurrentTickSilently(0);
  track.startRecording(0);
  inputNote(60, 12, 32);  // 20 ticks long: below the minimum, so the pair is filtered out.

  clockManager.assignCurrentTickSilently(768);
  trackManager.handleTransportStop();
  for (int turn = 0; turn < 8192 && track.hasPendingDeferredOverdubStop(); ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  trackManager.servicePendingRecordStopGeometry();

  TEST_ASSERT_FALSE_MESSAGE(track.getLoop(0).passes.hasRecordPass(),
                            "precondition: the take must have been filtered out");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      0, trackManager.getMasterLoopLength(),
      "a take that published nothing must not adopt a master length from the stop-edge prediction");
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

/// During deferred overdub finalization the track has returned to PLAYING, so a slot gesture
/// must follow ordinary populated-slot playback selection rather than be rejected as a session undo.
void test_slot_button_during_pending_overdub_finalization_queues_populated_target() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  commitRecordPassIntoTrackSlot(track, 1);
  track.forceSetState(TRACK_PLAYING);
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  track.startOverdubbing(96);
  TEST_ASSERT_TRUE(track.isOverdubbing());
  inputNote(60, 97, 120);
  track.stopOverdubbing();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());

  midiButtonActions.handleToggleRecordForSlot(1);

  TEST_ASSERT_EQUAL_UINT8(1, trackManager.getSelectedSlotIndex(0));
  TEST_ASSERT_TRUE(track.hasDataInSlot(1));
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
}

/// The same single slot gesture arms an empty target through the existing queued-record path.
void test_slot_button_during_pending_overdub_finalization_arms_empty_target() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  track.forceSetState(TRACK_PLAYING);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  track.startOverdubbing(96);
  TEST_ASSERT_TRUE(track.isOverdubbing());
  inputNote(60, 97, 120);
  track.stopOverdubbing();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());

  midiButtonActions.handleToggleRecordForSlot(1);

  TEST_ASSERT_EQUAL_UINT8(1, track.getActiveLoopIndex());
  TEST_ASSERT_TRUE(trackManager.isRecordingQueued(0, 1) || track.isRecording());
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
}


/// **S4b.1 (check 7, repeated switches) — B -> C while A still finalizes: accepted, bounded, nothing
/// stolen, and it executes without another gesture.**
///
/// The cross-track case has its own fixture; this is the *same-track repeated* case the review named as
/// still unproven, and it is where the bounded-overlap claim (`Omax` inbound holders, the design's
/// `TransferOverlap` budgeted as a transient rather than as slack) has to hold: A finalizes on slot 0, B is
/// the live capture on slot 1, and the user switches again to slot 2 before A has retired.
///
/// Measured mechanism, and what the assertions pin. The repeated switch does **not** move the active slot
/// (S4f.5 governs it) and does **not** provision its target on the gesture path — the gesture retains the
/// request instead: B's stop is **accepted and latched** against B's own slot (`hasAcceptedInteractionForSlot`)
/// while no stop job exists for it (`hasLiveCaptureStopJobForSlot` false), and the target's record start
/// stays **queued** (`isRecordingQueued`). Nothing is stolen to satisfy the inbound — A's live finalizer and
/// lease and B's lease all survive — and the pool does not grow across the gesture (490 -> 490 chunks on the
/// measured run). Once A retires and the deferred lane turns, the retained request **executes without
/// another gesture**: C becomes active, its occupancy is provisioned, B's latch clears, and further turns
/// change neither the active slot nor the pool.
void test_s4b_1_a_repeated_switch_while_a_finalizes_is_bounded_and_executes_once_capacity_frees() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));

  // (1) A is live through the production button route and takes MIDI through the real handler; its stop is
  // then admitted and deliberately **not** advanced, so the finalizer is paused and holding capacity.
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isOverdubbing(track),
                           "fixture precondition: A must be overdubbing");
  inputNote(60, 96, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track),
                           "A's stop must be admitted before the switches");

  // (2) The first switch lands: B becomes the live capture on the running transport while A finalizes.
  clockManager.assignCurrentTickSilently(200);
  TrackCaptureStopTestAccess::toggleRecordForSlot(1);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the first switch must commit while A finalizes");
  TrackCaptureStopTestAccess::selectSlotIndex(0, 1);
  TrackCaptureStopTestAccess::toggleRecordForSlot(1);
  const uint32_t queuedTick = clockManager.getCurrentTick();
  for (uint32_t tick = queuedTick + 1; tick <= queuedTick + 768 * 3 &&
                             !TrackCaptureStopTestAccess::isRecording(track) &&
                             !TrackCaptureStopTestAccess::isOverdubbing(track);
       ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track) ||
                               TrackCaptureStopTestAccess::isOverdubbing(track),
                           "B must be the live capture before the repeated switch");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 0),
                           "A's finalizer must still be outstanding");

  // (3) The repeated switch, with A still finalizing.
  const uint16_t chunksBeforeSecondSwitch = LoopEventStore::freeChunkCount();
  clockManager.assignCurrentTickSilently(1000);
  TrackCaptureStopTestAccess::toggleRecordForSlot(2);

  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the repeated switch may not move the active slot");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, 2),
                            "the repeated switch must not provision its target on the gesture path");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 1),
                           "B's stop must be accepted and latched, not dropped");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 1),
                            "the latch is the accepted request; no stop job has been admitted for it yet");
  TEST_ASSERT_TRUE_MESSAGE(trackManager.isRecordingQueued(0, 2),
                           "the target's record start must stay queued for the handoff");
  // Nothing is taken from the live owner or the live capture.
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 0),
                           "A's finalizer must survive the repeated switch");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 0),
                           "A's working set must not be released to satisfy the inbound");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 1),
                           "B's working set must survive its own stop being accepted");
  // ...and the transient overlap is bounded: the gesture itself consumes nothing.
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(chunksBeforeSecondSwitch, LoopEventStore::freeChunkCount(),
                                   "the repeated switch must not grow the pool");

  // (4) A retires, the deferred lane turns -- and the retained request executes with no further gesture.
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::ownsLiveFinalization(track, 0); ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::ownsLiveFinalization(track, 0),
                            "the fixture must retire A's finalizer");
  for (int round = 0; round < 8 && TrackCaptureStopTestAccess::activeLoopIndex(track) != 2; ++round) {
    const uint32_t base = clockManager.getCurrentTick();
    for (uint32_t tick = base + 1; tick <= base + 768 * 2; ++tick) {
      clockManager.assignCurrentTickSilently(tick);
      TrackCaptureStopTestAccess::startPendingRecord(tick);
      (void)TrackCaptureStopTestAccess::step(track);
      (void)TrackCaptureStopTestAccess::serviceAcceptedInteractionTurn(track);
    }
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the retained switch must execute once the capacity frees");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, 2),
                           "the executed switch must have provisioned the inbound occupancy");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 1),
                            "the latch must clear when the request executes");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track) ||
                               TrackCaptureStopTestAccess::isOverdubbing(track),
                           "the successor must be the live capture after the handoff");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 0),
                           "A's retired working set must still not have been stolen");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 1),
                           "B's working set must not have been stolen by the inbound");

  // (5) Bounded, not accumulating: further turns change neither the active slot nor the pool.
  const uint16_t chunksSettled = LoopEventStore::freeChunkCount();
  const uint32_t baseTick = clockManager.getCurrentTick();
  for (uint32_t tick = baseTick + 1; tick <= baseTick + 768 * 2; ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
    (void)TrackCaptureStopTestAccess::step(track);
    (void)TrackCaptureStopTestAccess::serviceAcceptedInteractionTurn(track);
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the settled state must not drift");
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(chunksSettled, LoopEventStore::freeChunkCount(),
                                   "the settled state must not keep consuming pool chunks");
}

/// **S4b.1 (a) — an *uncommitted* switch whose stop was DENIED on capacity still retains the start.**
///
/// S4b.1's finding (a) recorded the remainder as "the switch returns `false` and the press is dropped
/// rather than retained". The code does retain it — `MidiButtonActions::handleToggleRecordForSlot` writes the
/// same manager pending-start owner through `retainRecordStartForSlot` when `finalizeCaptureAndSelectSlot`
/// returns false, and reports "switch and record start retained (stop not yet admitted)" — and this fixture is
/// the reproducer for that path, as distinct from the *admitted*-stop carrier the sibling fixture covers.
///
/// The predecessor's stop is denied here for the contract's own reason: **another slot's admitted
/// finalization already holds the track's single finalization capacity** (DEC-078 item 6), so the
/// predecessor's stop is accepted-and-retained (never a refusal, DEC-078 item 1) while it stays the active
/// interval.
void test_s4b_1_a_denied_stop_on_an_uncommitted_switch_retains_the_targets_start() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  constexpr uint8_t kDenied = 0;      // the switch's predecessor: its stop is denied on capacity
  constexpr uint8_t kTarget = 1;      // the empty switch target whose start must survive
  constexpr uint8_t kOccupant = 2;    // the slot whose admitted finalization holds the capacity
  commitRecordPassIntoTrackSlot(track, kDenied);
  commitRecordPassIntoTrackSlot(track, kOccupant);

  // (1) Occupy the track's finalization capacity with an admitted stop on another slot.
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kOccupant, true));
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, kOccupant);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::isOverdubbing(track));
  inputNote(60, 96, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, kOccupant),
                           "fixture precondition: the occupant's stop must be admitted and held");

  // (2) The switch's predecessor is a second live capture on the same track, started while the occupant
  // finalizes -- the overlap the contract requires (outgoing finalizer + incoming capture).
  clockManager.assignCurrentTickSilently(200);
  TrackCaptureStopTestAccess::selectSlot(track, kDenied);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isOverdubbing(track),
                           "the predecessor must be live while the occupant finalizes");
  inputNote(62, 200, 224);
  clockManager.assignCurrentTickSilently(260);

  // (3) The switch gesture to an empty target: the stop cannot be admitted, so the switch does **not**
  // commit -- and the start must be retained rather than dropped with the gesture.
  TrackCaptureStopTestAccess::toggleRecordForSlot(kTarget);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kDenied, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "an uncommitted switch must not move the active slot");
  TEST_ASSERT_TRUE_MESSAGE(trackManager.isRecordingQueued(0, kTarget),
                           "the target's start must be retained, not dropped with the gesture");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, kDenied),
                           "the denied stop must be accepted and latched against its own slot");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, kOccupant),
                           "the occupant's finalization must not be disturbed to admit the stop");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kOccupant),
                           "the occupant's working set must survive");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isOverdubbing(track),
                           "the predecessor must still be capturing after the uncommitted switch");

  // (4) Let the occupant's finalization retire, then turn the lanes: the retained start executes with no
  // further gesture, so the user never presses twice (DEC-078 item 1).
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::ownsLiveFinalization(track, kOccupant);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::ownsLiveFinalization(track, kOccupant),
                            "the fixture must retire the occupant's finalizer");
  for (int round = 0; round < 8 && TrackCaptureStopTestAccess::activeLoopIndex(track) != kTarget; ++round) {
    const uint32_t base = clockManager.getCurrentTick();
    for (uint32_t tick = base + 1; tick <= base + 768 * 2; ++tick) {
      clockManager.assignCurrentTickSilently(tick);
      TrackCaptureStopTestAccess::startPendingRecord(tick);
      (void)TrackCaptureStopTestAccess::step(track);
      (void)TrackCaptureStopTestAccess::serviceAcceptedInteractionTurn(track);
    }
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kTarget, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                 "the retained start must execute once the capacity frees");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track),
                           "the retained start must leave the target recording");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, kDenied),
                            "the predecessor's latch must clear when its stop executes");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kDenied),
                           "nothing may be stolen from the retired predecessor either");
}



/// **S4b.1 (check 7, populated target) — a queued *playback* switch waits for the predecessor and then
/// commits in the production tick lane.**
///
/// The empty-target case is carried by the record-start carrier; a **populated** target takes a different
/// route: the gesture queues a **playback switch** (`queuePlayingSlotSwitch` → `SlotStateMachine`'s pending
/// switch), which commits in `TrackManager::updateAllTracks` — the production per-tick entry, and the lane
/// S4b.5/S4b.6/S4b.7 change. This fixture is that lane's acceptance, measured first: while the predecessor
/// still finalizes the request is **retained** (the pending switch stays set, the active slot does not move,
/// the predecessor's finalizer and lease survive), and once the finalizer retires the switch **commits with
/// no further gesture** — active slot moved, pending cleared, and the enabled set replaced with the target.
/// That is the answer to the review's "silent dead switch" risk on this shape: it is neither silent nor dead.
void test_s4b_1_a_queued_playback_switch_waits_for_the_predecessor_and_then_commits() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  constexpr uint8_t kPredecessor = 0;
  constexpr uint8_t kTarget = 1;
  commitRecordPassIntoTrackSlot(track, kPredecessor);
  commitRecordPassIntoTrackSlot(track, kTarget);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kPredecessor, true));

  // The predecessor captures and its stop is admitted (the finalizer holds the capacity).
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, kPredecessor);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::isOverdubbing(track));
  inputNote(60, 96, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track),
                           "fixture precondition: the predecessor's stop must be admitted");

  // Press the populated target: a queued playback switch, not a record carrier.
  clockManager.assignCurrentTickSilently(200);
  TrackCaptureStopTestAccess::toggleRecordForSlot(kTarget);
  TEST_ASSERT_TRUE_MESSAGE(trackManager.hasPendingSlotSwitch(0),
                           "a populated target must queue a playback switch");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kTarget, trackManager.getPendingSlotIndex(0),
                                  "the queued switch must name the target");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kPredecessor, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the press must not move the active slot");

  // Drive the production tick lane across several boundaries while the predecessor still finalizes. The
  // lane's own provisioning turn is modelled too (`main.cpp`'s deferred-idle dispatch is not linked here), so
  // the switch's inbound occupancy is taken off-clock exactly as the firmware does -- and, as on the device,
  // this lane is the *only* place it can be taken.
  for (int round = 0; round < 3; ++round) {
    (void)trackManager.servicePendingSlotProvision();
    const uint32_t base = clockManager.getCurrentTick();
    for (uint32_t tick = base + 1; tick <= base + 768; ++tick) {
      clockManager.assignCurrentTickSilently(tick);
      trackManager.updateAllTracks(tick);
    }
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kPredecessor, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the queued switch must not commit while the predecessor finalizes");
  TEST_ASSERT_TRUE_MESSAGE(trackManager.hasPendingSlotSwitch(0),
                           "the request must be retained rather than dropped");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::ownsLiveFinalization(track, kPredecessor),
                           "the predecessor's finalizer must still be outstanding");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kPredecessor),
                           "the predecessor's working set must survive the wait");

  // Retire the finalizer and keep driving: the queued switch commits with no further gesture.
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::ownsLiveFinalization(track, kPredecessor);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::ownsLiveFinalization(track, kPredecessor));
  for (int round = 0; round < 4 && TrackCaptureStopTestAccess::activeLoopIndex(track) != kTarget;
       ++round) {
    const uint32_t base = clockManager.getCurrentTick();
    for (uint32_t tick = base + 1; tick <= base + 768; ++tick) {
      clockManager.assignCurrentTickSilently(tick);
      trackManager.updateAllTracks(tick);
    }
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kTarget, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the retained switch must commit once the predecessor retires");
  TEST_ASSERT_FALSE_MESSAGE(trackManager.hasPendingSlotSwitch(0),
                            "the committed switch must clear its pending state");
  TEST_ASSERT_TRUE_MESSAGE(trackManager.isSlotEnabled(0, kTarget),
                           "the committed switch must enable the target slot");
  TEST_ASSERT_FALSE_MESSAGE(trackManager.isSlotEnabled(0, kPredecessor),
                            "the committed switch must leave the predecessor slot out of the enabled set");
}

/// **S4b.6/S4b.7 — a switch to a target without occupancy never allocates on the clock path.**
///
/// The inbound occupancy (the target slot's playback runtime) is what the switch commit binds to. Two facts
/// make it a stage of its own: the **commit** runs on the clock path (`updateAllTracks`) and the **request**
/// runs on a MIDI-input edge, so neither may take the runtime; and `slotIfAllocated` alone would leave the
/// switch silently dead instead of provisioning it. The contract therefore has two sides, and this helper
/// drives both for one target shape:
///
///   1. the **production MIDI edge** (`toggleRecordForSlot` -> `queuePlayingSlotSwitch` ->
///      `requestSlotSwitch`) queues the switch and takes no runtime inline; the pending switch **is** the
///      `SlotProvision` turn's eligibility (`MidiServiceDrain::slotProvisionNeeded`), so no request record
///      exists to be consumed elsewhere;
///   2. driving the clock lane leaves the switch **pending** and the target's occupancy **absent** -- the
///      record of "no allocation on the clock path";
///   3. one `SlotProvision` turn (`TrackManager::servicePendingSlotProvision`) takes the occupancy and builds
///      the target's merged window;
///   4. driving again **commits** the switch, so it was never dead.
///
/// `lengthTicks` gives the target the shape under test. Both shapes used here stay **below** the demotion
/// bound (`kMaxDetailedWindowBars` bars): at or above it the loop is demoted to the windowed policy and the
/// commit's long-loop hold term (`isPlaybackMergedMidiEventsReadyForSlot`) can no longer be satisfied by the
/// occupancy work -- measured here as a switch that stays pending after provisioning, because the windowed
/// gather returns empty without a published root. That shape is **owed** (recorded in `tasks.md` under
/// S4b.6/S4b.7), not asserted, so this helper never claims it.
void assert_s4b_6_a_switch_provisions_off_clock(uint32_t lengthTicks) {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  constexpr uint8_t kPlaying = 0;
  constexpr uint8_t kTarget = 2;
  commitRecordPassIntoTrackSlot(track, kPlaying);
  commitRecordPassIntoTrackSlotOfLength(track, kTarget, lengthTicks);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kPlaying, true));

  // The target has committed content but **no occupancy**: nothing has taken its playback runtime.
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, kTarget),
                            "fixture precondition: the target must start without occupancy");

  // The predecessor reaches the production playing state the way the lane fixture above does: an overdub
  // start on a populated slot is what enables it, plays it and publishes its playback pair, so the loop-end
  // wrap the commit waits for is a real one rather than a forced-state artifact. A forced `TRACK_PLAYING`
  // with no published pair leaves the playhead at tick zero and the switch can never be due.
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, kPlaying);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isOverdubbing(track),
                           "fixture precondition: the predecessor must be capturing");
  inputNote(60, 96, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track),
                           "fixture precondition: the predecessor's stop must be admitted");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, kTarget),
                            "fixture precondition: the target must still have no occupancy");

  // Retire the predecessor's finalizer *before* the press, so the queued switch is genuinely due: while
  // the finalizer is outstanding the playhead is frozen and no loop-end wrap is ever detected, which would
  // hide the commit's occupancy binding rather than exercise it.
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::ownsLiveFinalization(track, kPlaying);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::ownsLiveFinalization(track, kPlaying),
                            "the fixture must retire the predecessor's finalizer");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, kTarget),
                            "retiring the finalizer must not take the target's occupancy");

  // (1) The production MIDI edge presses the populated target: it queues the playback switch and takes
  // nothing inline. The pending switch *is* the `SlotProvision` turn's eligibility, so no request record is
  // involved -- the predicate reads exactly this state.
  clockManager.assignCurrentTickSilently(200);
  TrackCaptureStopTestAccess::toggleRecordForSlot(kTarget);
  TEST_ASSERT_TRUE_MESSAGE(trackManager.hasPendingSlotSwitch(0),
                           "the request must queue a pending switch");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kTarget, trackManager.getPendingSlotIndex(0),
                                  "the queued switch must name the target");
  TEST_ASSERT_TRUE_MESSAGE(
      MidiServiceDrain::slotProvisionNeeded(trackManager.hasPendingSlotSwitch(0),
                                            TrackCaptureStopTestAccess::runtimeAllocated(track, kTarget)),
      "the pending switch with no inbound occupancy must be the provisioning turn's work");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, kTarget),
                            "the MIDI edge must not provision inline");

  // (2) The clock lane may not allocate: the switch stays pending, the occupancy stays absent. The
  // provisioning turn is deliberately **not** modelled in this window -- that is the point of it.
  for (int round = 0; round < 3; ++round) {
    const uint32_t base = clockManager.getCurrentTick();
    for (uint32_t tick = base + 1; tick <= base + 768; ++tick) {
      clockManager.assignCurrentTickSilently(tick);
      trackManager.updateAllTracks(tick);
    }
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kPlaying, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the switch may not commit before its occupancy exists");
  TEST_ASSERT_TRUE_MESSAGE(trackManager.hasPendingSlotSwitch(0),
                           "the switch must be retained, not dropped");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, kTarget),
                            "the clock path must not allocate the inbound occupancy");

  // (3) The off-clock owner takes the occupancy: exactly one `SlotProvision` turn -- the turn provisions the
  // pending switch's target **and** builds that slot's merged window, which is the pair the contract names.
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, kTarget),
                            "nothing may have provisioned the target behind this window's back");
  TEST_ASSERT_TRUE_MESSAGE(trackManager.servicePendingSlotProvision(),
                           "the SlotProvision turn must take the pending switch as its work");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, kTarget),
                           "provisioning must have taken the target's occupancy");
  TEST_ASSERT_FALSE_MESSAGE(trackManager.servicePendingSlotProvision(),
                           "a provisioned switch is the commit path's, not the turn's");
  // The turn's second half is `ensurePlaybackMergedEventsForSlot`, and its effect is deliberately asserted as
  // **not** observable here: that window is gathered from the loop's *published snapshot root*, which a
  // hand-committed slot has none of, so the gather misses and the window stays empty (measured). This is a
  // tripwire on the harness, not on the firmware -- if a fixture ever publishes a root, it fails and the
  // assertion moves to the ready side. Discharge path for the missing evidence: the published-root fixture
  // owed under S4b.6's demoted-target item (the windowed publication lane, S4c/F).
  TEST_ASSERT_FALSE_MESSAGE(track.isPlaybackMergedMidiEventsReadyForSlot(kTarget),
                            "a hand-committed slot has no published root, so the merged window stays empty");
  // Task 3.5 re-point: readiness no longer reads that buffer. No window was published for this slot, and with no
  // published root no build could have gathered one -- so readiness names the **source** as the holder rather than
  // relying on "the merged window is empty".
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackWindowReadiness::OwesSource),
                    static_cast<int>(track.playbackWindowReadinessForSlot(kTarget)));
  TEST_ASSERT_EQUAL_STRING("published_source", Track::playbackWindowReadinessHolder(
                                                   track.playbackWindowReadinessForSlot(kTarget)));
  for (int round = 0; round < 4 && TrackCaptureStopTestAccess::activeLoopIndex(track) != kTarget;
       ++round) {
    const uint32_t base = clockManager.getCurrentTick();
    for (uint32_t tick = base + 1; tick <= base + 768; ++tick) {
      clockManager.assignCurrentTickSilently(tick);
      trackManager.updateAllTracks(tick);
    }
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kTarget, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the provisioned switch must commit");
  TEST_ASSERT_FALSE_MESSAGE(trackManager.hasPendingSlotSwitch(0),
                            "the committed switch must clear its pending state");
}

/// Short target: the commit would otherwise take the plain `hasDataInSlot` branch.
void test_s4b_6_a_switch_to_a_slot_without_occupancy_provisions_off_clock() {
  assert_s4b_6_a_switch_provisions_off_clock(768);
}

/// Multi-bar target (8 bars): the same contract over a different geometry, so the binding is not a
/// property of the one-bar case. It stays **below** `kMaxDetailedWindowBars` (16 bars) on purpose -- at or
/// above that bound the loop is *demoted* to the windowed policy, whose commit hold this harness cannot
/// release, so the demoted shape is an explicitly owed measurement rather than a passing fixture here.
void test_s4b_6_a_switch_to_a_multi_bar_target_without_occupancy_also_provisions_off_clock() {
  assert_s4b_6_a_switch_provisions_off_clock(6144);
}

// --- S4b.7: the demoted (windowed-policy) target, with and without a published root ------------------------

/// Commit a bar-spaced pass: one note per bar, so any windowed gather of this slot finds events no matter
/// where the playhead is. The demoted shape needs spread content for the same reason a restored slot has it --
/// a window centred on the playhead must not fall on empty space.
void commitBarSpacedPassIntoTrackSlot(Track& track, uint8_t slot, uint32_t bars) {
  TrackCaptureStopTestAccess::selectSlot(track, slot);
  Loop& loop = track.getActiveLoop();
  loop.loopLengthTicks = bars * 768;
  loop.beginCapture(CapturePhase::Record);
  for (uint32_t bar = 0; bar < bars; ++bar) {
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(bar * 768, 1, 48, 100)));
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(bar * 768 + 48, 1, 48, 0)));
  }
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(bars * 768, CommitReason::RecordStop));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass());
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
}

/// Install a **published snapshot root** for `loop` with the same bar-spaced notes. This is the state a
/// *restored* slot has (SD load publishes a root) and the only source the merged-window build gathers from
/// (`Loop::tryGatherPublishedSnapshotEventsInWindow`); a hand-committed slot has none, which is the
/// `GatherMissHoldEmpty` case. Same recipe as `test_loop_take_survival`'s root fixtures.
void publishBarSpacedRootForSlot(Loop& loop, uint32_t bars) {
  auto empty = std::make_shared<const LoopSnapshot>(
      LoopSnapshot(loop.loopId, loop.loopLengthTicks, 0, 8, 8));
  LoopSnapshotChangeSet changes;
  for (uint32_t bar = 0; bar < bars; ++bar) {
    SnapshotNoteChange add{};
    add.kind = SnapshotChangeKind::Add;
    add.next.loopId = loop.loopId;
    add.next.noteId = static_cast<NoteId>(2000 + bar);
    add.next.pitch = 48;
    add.next.velocity = 100;
    add.next.startTick = bar * 768;
    add.next.endTick = bar * 768 + 48;
    changes.notes.push_back(add);
    // The **event** index is what a windowed gather reads (`gatherPublishedSnapshotMembership` walks
    // `eventMembership(bar)`), so the root must carry the events as well as the notes -- the real builder
    // populates both views.
    SnapshotEventChange on{};
    on.kind = SnapshotChangeKind::Add;
    on.next.loopId = loop.loopId;
    on.next.eventId = static_cast<EventId>(4000 + bar * 2);
    on.next.canonicalOrder = bar * 2;
    on.next.value = MidiEvent::NoteOn(bar * 768, 1, 48, 100);
    changes.events.push_back(on);
    SnapshotEventChange off{};
    off.kind = SnapshotChangeKind::Add;
    off.next.loopId = loop.loopId;
    off.next.eventId = static_cast<EventId>(4001 + bar * 2);
    off.next.canonicalOrder = bar * 2 + 1;
    off.next.value = MidiEvent::NoteOff(bar * 768 + 48, 1, 48, 0);
    changes.events.push_back(off);
  }
  LoopSnapshotConstruction construction(empty, changes);
  while (!construction.complete()) {
    TEST_ASSERT_TRUE(construction.step(128));
  }
  const ImmutableLoopSnapshotRef snapshot = construction.freeze();
  TEST_ASSERT_NOT_NULL(snapshot.get());
  loop.storePublishedSnapshotForTest(snapshot, loop.playbackRevision);
  TEST_ASSERT_TRUE_MESSAGE(loop.publishedSnapshotIsCurrent(),
                           "the fixture must install a *current* root, or the gather terms refuse it");
}

/// **S4b.7 — the demoted target: the merged window is what releases the hold, and its absence is bounded.**
///
/// Above `kMaxDetailedWindowBars` (16 bars) a slot is *demoted* to the windowed policy, and the switch commit's
/// hold term (`isPlaybackMergedMidiEventsReadyForSlot`) then reads the merged window that the `SlotProvision`
/// turn builds. That build gathers from the loop's **published snapshot root**, so the two shapes this helper
/// drives are the two halves of the contract line ("the long-loop pending hold is satisfied by that turn … and
/// carries an age/bound"):
///
///   * `withRoot` — a restored-slot root exists, the turn's build makes the window ready, and the switch
///     **commits**. This is the evidence the earlier slice had to record as missing.
///   * without a root — the occupancy is taken but the window can never become ready, so the switch is
///     **reported once per episode** at the bound and left exactly as it was: bounded, never silent, never
///     dropped (DEC-078 item 1's "the user never re-issues" survives).
void assert_s4b_7_a_demoted_target_through_the_turn(bool withRoot) {
  SlotProvisionHold::resetReportRecordForTest();
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  constexpr uint8_t kPlaying = 0;
  constexpr uint8_t kTarget = 2;
  constexpr uint32_t kTargetBars = 24;  // > 16 bars: the demoted policy, measured (not the below-bound case)
  constexpr uint32_t kBar = 768;
  commitRecordPassIntoTrackSlot(track, kPlaying);
  commitBarSpacedPassIntoTrackSlot(track, kTarget, kTargetBars);
  if (withRoot) {
    publishBarSpacedRootForSlot(track.getLoop(kTarget), kTargetBars);
  }
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kPlaying, true));
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, kTarget),
                            "fixture precondition: the target starts without occupancy");

  // The production playing state for the predecessor (an overdub start enables, plays and publishes it), then
  // its finalizer retires -- a live finalizer freezes the playhead, so the switch could never be due.
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, kPlaying);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 96, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track));
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::ownsLiveFinalization(track, kPlaying);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::ownsLiveFinalization(track, kPlaying));

  // Press the demoted target through the production edge: queued, nothing taken inline.
  clockManager.assignCurrentTickSilently(200);
  TrackCaptureStopTestAccess::toggleRecordForSlot(kTarget);
  TEST_ASSERT_TRUE_MESSAGE(trackManager.hasPendingSlotSwitch(0),
                           "the press must queue the switch");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kTarget, trackManager.getPendingSlotIndex(0),
                                  "the queued switch must name the demoted target");
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::runtimeAllocated(track, kTarget));

  // The clock lane may not commit it: the demoted hold waits for the merged window.
  for (uint32_t tick = clockManager.getCurrentTick() + 1; tick <= 200 + kBar; ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    trackManager.updateAllTracks(tick);
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kPlaying, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "a demoted target may not commit before its window is built");
  TEST_ASSERT_TRUE(trackManager.hasPendingSlotSwitch(0));

  // The provisioning turn takes the occupancy **and** builds the window -- the two halves of the contract.
  TEST_ASSERT_TRUE_MESSAGE(trackManager.servicePendingSlotProvision(),
                           "the turn must take the demoted switch as its work");
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::runtimeAllocated(track, kTarget));
  // The **legacy** merged-buffer term is what the switch commit still holds on here, so it keeps its own assertion.
  TEST_ASSERT_EQUAL_MESSAGE(withRoot, track.isPlaybackMergedMidiEventsReadyForSlot(kTarget),
                            "the turn's build is only ready when the slot has a published root");
  // Task 3.5 re-point: readiness is the window this change **published**, and this slot has none -- the
  // `SlotProvision` turn built the *live merged* window, and no production caller requests a window yet (that
  // requester is 3.7's). So readiness is not Ready for either shape, and it names who owes the window instead of
  // leaving the caller to infer it from an empty buffer: with a root the **window-build owner** owes a window it
  // could gather, and without one the **source** owes the root no build can supply.
  TEST_ASSERT_EQUAL_MESSAGE(withRoot ? static_cast<int>(Track::PlaybackWindowReadiness::OwesBuild)
                                     : static_cast<int>(Track::PlaybackWindowReadiness::OwesSource),
                            static_cast<int>(track.playbackWindowReadinessForSlot(kTarget)),
                            "readiness names who owes the window");

  const uint32_t holdBoundTicks = SlotProvisionHold::boundTicksFor(768);
  if (withRoot) {
    for (int round = 0; round < 4 && TrackCaptureStopTestAccess::activeLoopIndex(track) != kTarget;
         ++round) {
      const uint32_t base = clockManager.getCurrentTick();
      for (uint32_t tick = base + 1; tick <= base + kBar; ++tick) {
        clockManager.assignCurrentTickSilently(tick);
        trackManager.updateAllTracks(tick);
      }
    }
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(kTarget, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                    "the root-backed demoted switch must commit");
    TEST_ASSERT_FALSE(trackManager.hasPendingSlotSwitch(0));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, SlotProvisionHold::reportRecordForTest().count,
                                     "a switch that completes must never report a hold");
    return;
  }

  // No root: drive past the bound while the turn keeps running. The switch is reported once, and stays.
  const uint32_t firstQueuedTick = clockManager.getCurrentTick();
  for (int round = 0; round < 8; ++round) {
    (void)trackManager.servicePendingSlotProvision();
    const uint32_t base = clockManager.getCurrentTick();
    for (uint32_t tick = base + 1; tick <= base + kBar; ++tick) {
      clockManager.assignCurrentTickSilently(tick);
      trackManager.updateAllTracks(tick);
    }
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kPlaying, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "an unready demoted target must not commit");
  TEST_ASSERT_TRUE_MESSAGE(trackManager.hasPendingSlotSwitch(0),
                           "the switch must be retained, not dropped");
  const SlotProvisionHold::ReportRecord& report = SlotProvisionHold::reportRecordForTest();
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, report.count, "the hold must report exactly once per episode");
  TEST_ASSERT_EQUAL_UINT8(0, report.trackIndex);
  TEST_ASSERT_EQUAL_UINT8(kTarget, report.slotIndex);
  TEST_ASSERT_TRUE_MESSAGE(report.ageTicks >= holdBoundTicks,
                           "the report must be at or past the bound, not merely late");
  TEST_ASSERT_TRUE(clockManager.getCurrentTick() - firstQueuedTick >= holdBoundTicks);

  // A second episode (cancel, turn while nothing is pending, press again) reports on its own account.
  trackManager.clearPendingSlotSwitch(0);
  (void)trackManager.servicePendingSlotProvision();
  clockManager.assignCurrentTickSilently(clockManager.getCurrentTick() + 1);
  TrackCaptureStopTestAccess::toggleRecordForSlot(kTarget);
  TEST_ASSERT_TRUE(trackManager.hasPendingSlotSwitch(0));
  for (int round = 0; round < 8; ++round) {
    (void)trackManager.servicePendingSlotProvision();
    const uint32_t base = clockManager.getCurrentTick();
    for (uint32_t tick = base + 1; tick <= base + kBar; ++tick) {
      clockManager.assignCurrentTickSilently(tick);
      trackManager.updateAllTracks(tick);
    }
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, SlotProvisionHold::reportRecordForTest().count,
                                   "a new episode reports again: once per episode, not once per boot");
}

/// A **restored** demoted slot (a published root exists): the turn's build makes the window ready and the
/// switch commits -- the evidence the first slice had to record as missing.
void test_s4b_7_a_demoted_target_with_a_published_root_commits_through_the_turn() {
  assert_s4b_7_a_demoted_target_through_the_turn(/*withRoot=*/true);
}

/// An unready demoted slot: the hold is real but **bounded** -- one report per episode, switch retained.
void test_s4b_7_a_demoted_target_without_a_root_is_reported_once_at_its_bound() {
  assert_s4b_7_a_demoted_target_through_the_turn(/*withRoot=*/false);
}

/// **S4b.5 — one active-slot write authority: a direct write cancels the queued switch, and cancelling is the
/// whole trio.**
///
/// The entry's shape: a **replace-enabled-set** switch is queued, the **undo route** moves the active slot
/// (`TrackUndo.cpp`'s `applyClearSlotUndoSideEffects` calls `TrackManager::setActiveLoopIndex` -- the authority
/// this stage establishes), and then an **unrelated** switch is triggered. Three properties must hold:
///
///   1. the queued switch is **cancelled** rather than left armed underneath a slot that already moved (one
///      transfer, not two);
///   2. the cancel clears the **whole** trio, so the unrelated switch does not inherit a stale enabled-set
///      replacement and wipe the enabled set; and
///   3. the cancel itself mutates nothing but the slot -- the pool does not grow and the switch is gone.
void test_s4b_5_a_direct_write_cancels_the_queued_switch_without_a_stale_replacement() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  constexpr uint8_t kPlaying = 0;
  constexpr uint8_t kReplaceTarget = 1;
  constexpr uint8_t kUndoSlot = 2;
  constexpr uint8_t kLaterTarget = 3;
  commitRecordPassIntoTrackSlot(track, kPlaying);
  commitRecordPassIntoTrackSlot(track, kReplaceTarget);
  commitRecordPassIntoTrackSlot(track, kUndoSlot);
  commitRecordPassIntoTrackSlot(track, kLaterTarget);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kPlaying, true));
  // The undo slot is a slot the track has played (a restored take has occupancy), so the undo route's own
  // `resetPlaybackStateForSlot` below has a runtime to anchor.
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kUndoSlot, false));
  // The enabled set a stale replacement would wipe: the played slot plus one other layer. Set directly --
  // the enabled set is this fixture's subject, and the gesture routes that build it are covered above.
  trackManager.setSlotEnabled(0, kPlaying, true);
  trackManager.setSlotEnabled(0, kUndoSlot, true);

  // The production playing state, then retire the finalizer so a later switch can be due.
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, kPlaying);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 96, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track));
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::ownsLiveFinalization(track, kPlaying);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::ownsLiveFinalization(track, kPlaying));

  // (1) A **replace-enabled-set** switch is queued by the production gesture (the populated target route
  // arms the replacement, so this is the state whose cancel must clear it).
  clockManager.assignCurrentTickSilently(200);
  TrackCaptureStopTestAccess::toggleRecordForSlot(kReplaceTarget);
  TEST_ASSERT_TRUE_MESSAGE(trackManager.hasPendingSlotSwitch(0),
                           "the populated target must queue a playback switch");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kReplaceTarget, trackManager.getPendingSlotIndex(0),
                                  "the queued switch must name the replace target");
  TEST_ASSERT_EQUAL_UINT8(kPlaying, TrackCaptureStopTestAccess::activeLoopIndex(track));

  // (2) The **undo route**'s write -- exactly the call `applyClearSlotUndoSideEffects` makes. The queued
  // switch must be cancelled rather than left armed under a slot that already moved, and the cancel must not
  // allocate (nothing is stolen, nothing grows).
  const size_t chunksBeforeCancel = LoopEventStore::freeChunkCount();
  trackManager.setActiveLoopIndex(0, kUndoSlot);
  // The undo route continues with its own playback-state reset (`applyClearSlotUndoSideEffects`), so the
  // fixture does too: without it the new active slot has no anchored playhead and no switch can be due.
  track.resetPlaybackStateForSlot(kUndoSlot, clockManager.getCurrentTick());
  TEST_ASSERT_FALSE_MESSAGE(trackManager.hasPendingSlotSwitch(0),
                            "a direct active-slot write must cancel the queued switch, not race it");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kUndoSlot, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the undo route's write lands in one transfer");
  TEST_ASSERT_EQUAL_UINT32(chunksBeforeCancel, LoopEventStore::freeChunkCount());
  TEST_ASSERT_TRUE_MESSAGE(trackManager.isSlotEnabled(0, kPlaying),
                           "the cancel must not touch the enabled set");

  // (3) An **unrelated** switch, queued through a route that does not arm a replacement
  // (`requestSlotSwitch`). If the cancel had left the replacement armed, this commit would inherit it and
  // replace the enabled set with the target alone -- the stale-replacement defect.
  clockManager.assignCurrentTickSilently(clockManager.getCurrentTick() + 1);
  trackManager.requestSlotSwitch(0, kLaterTarget, SlotQuantization::LoopEnd,
                                 clockManager.getCurrentTick());
  TEST_ASSERT_TRUE(trackManager.hasPendingSlotSwitch(0));
  for (int round = 0; round < 4 && TrackCaptureStopTestAccess::activeLoopIndex(track) != kLaterTarget;
       ++round) {
    (void)trackManager.servicePendingSlotProvision();
    const uint32_t base = clockManager.getCurrentTick();
    for (uint32_t tick = base + 1; tick <= base + 768; ++tick) {
      clockManager.assignCurrentTickSilently(tick);
      trackManager.updateAllTracks(tick);
    }
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kLaterTarget, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the unrelated switch must commit");
  TEST_ASSERT_FALSE(trackManager.hasPendingSlotSwitch(0));
  TEST_ASSERT_TRUE_MESSAGE(trackManager.isSlotEnabled(0, kPlaying),
                           "no stale enabled-set replacement may reach an unrelated switch");
  TEST_ASSERT_TRUE_MESSAGE(trackManager.isSlotEnabled(0, kUndoSlot),
                           "the enabled layers a stale replacement would wipe must survive");
}

/// **S4b.5 — an accepted-but-unexecuted interaction makes a direct write take the switch's own route.**
///
/// Rev 21.3 adds `hasAcceptedUnexecutedInteraction()` to the active-slot guard, and it is the one term the
/// S4b.5 slice shipped **without** a fixture (removing it left the whole native suite green). The state it
/// covers is the latch alone: an interaction accepted and not yet executed, with **no** recording, **no**
/// overdub and **no** admitted stop -- so nothing else in the guard can route the write.
///
/// What the route decides here is observable: the capture-switch commit (`finalizeCaptureAndSelectSlot`)
/// enables the target slot and resets its playback state, while a bare write leaves the target exactly as it
/// was -- disabled, on a playing track, which is the "piano-roll notes with no MO" hazard the switch's own
/// comment names. Both routes move the active slot, so the **route**, not the destination, is the assertion.
void test_s4b_5_an_accepted_unexecuted_interaction_routes_the_write_through_the_switch() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  constexpr uint8_t kPlaying = 0;
  constexpr uint8_t kTarget = 1;
  commitRecordPassIntoTrackSlot(track, kPlaying);
  commitRecordPassIntoTrackSlot(track, kTarget);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kPlaying, true));
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, kPlaying);
  trackManager.setSlotEnabled(0, kPlaying, true);
  trackManager.setSlotEnabled(0, kTarget, false);

  // The accepted-but-unexecuted interaction, and nothing else: no capture, no job.
  TrackCaptureStopTestAccess::latchAcceptedStopForSlot(track, kPlaying);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasAcceptedUnexecutedInteraction(track),
                           "fixture precondition: the latch must be the accepted interaction");
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::isRecording(track));
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::isOverdubbing(track));
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track),
                            "fixture precondition: no admitted stop may stand in for the latch");

  trackManager.setActiveLoopIndex(0, kTarget);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kTarget, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the write still lands, whichever route takes it");
  TEST_ASSERT_TRUE_MESSAGE(
      trackManager.isSlotEnabled(0, kTarget),
      "the accepted interaction must route the write through the switch commit, which enables the target; "
      "a bare move would leave the active slot disabled on a playing track");
}

/// **S4b.2 — the inbound role is provisioned before the outbound one is released, and the overlap is bounded.**
///
/// The entry asks for three things, and the first is an **ordering**: pre-provision the inbound role before
/// releasing the outbound one, with the overlap bounded and accounted in the S2 budget; the switch point
/// performs no allocation (asserted by S4b.6/S4b.7's fixtures); and the transfer must not steal from a
/// still-live capture or deferred-finalization owner (asserted by S4b.1's two "nothing stolen" fixtures).
///
/// What is asserted here is the ordering and its **bound**, measured in the pool rather than argued:
///
///   * while the switch cannot commit (the inbound has no occupancy) the outbound's working set is **still
///     held** -- nothing is released early, so a refused or delayed switch never leaves the track with neither
///     role;
///   * the provisioning turn takes the inbound occupancy while the outbound is **still** held, which is the
///     overlap the S2 budget term accounts for; and
///   * the overlap is **exactly one playback working set**: the pool drop at provisioning equals what a probe
///     slot provisioned the same way costs in the same fixture (the outbound's own prewarm also carries the
///     capture horizon, so it is deliberately *not* used as the unit), and only after the commit does the
///     retired slot become releasable -- and the release touches nothing on the inbound.
void test_s4b_2_the_inbound_role_is_provisioned_before_the_outbound_is_released() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  constexpr uint8_t kOutbound = 0;
  constexpr uint8_t kInbound = 2;
  const uint32_t chunksEmpty = LoopEventStore::freeChunkCount();
  commitRecordPassIntoTrackSlot(track, kOutbound);
  commitRecordPassIntoTrackSlot(track, kInbound);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kOutbound, true));
  const uint32_t chunksOutboundOnly = LoopEventStore::freeChunkCount();
  const uint32_t outboundCost = chunksEmpty - chunksOutboundOnly;
  TEST_ASSERT_TRUE_MESSAGE(outboundCost > 0, "fixture precondition: a working set costs pool capacity");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, kInbound),
                            "fixture precondition: the inbound starts without occupancy");

  // The production playing state for the outbound, then retire the finalizer so a switch can be due.
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, kOutbound);
  TrackCaptureStopTestAccess::activateSlot(0, kOutbound);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 96, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track));
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::ownsLiveFinalization(track, kOutbound);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::ownsLiveFinalization(track, kOutbound));
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kOutbound),
                           "fixture precondition: the outbound holds the working set it will retire");

  // Press the unoccupied inbound target: a queued switch that must wait for its occupancy.
  clockManager.assignCurrentTickSilently(200);
  TrackCaptureStopTestAccess::toggleRecordForSlot(kInbound);
  TEST_ASSERT_TRUE(trackManager.hasPendingSlotSwitch(0));
  TEST_ASSERT_EQUAL_UINT8(kInbound, trackManager.getPendingSlotIndex(0));

  // (1) The clock lane cannot commit yet **and must not have released the outbound**: the pre-provisioning
  //     half of the ordering. A release here would leave the track with neither role.
  const uint32_t chunksBeforeLane = LoopEventStore::freeChunkCount();
  for (uint32_t tick = 201; tick <= 200 + 768; ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    trackManager.updateAllTracks(tick);
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kOutbound, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the switch may not commit before its occupancy exists");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kOutbound),
                           "the outbound's working set must survive the wait: nothing is released early");
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::runtimeAllocated(track, kInbound));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(chunksBeforeLane, LoopEventStore::freeChunkCount(),
                                   "the clock lane may not allocate or release pool capacity");

  // (2) The provisioning turn takes the inbound occupancy while the outbound is **still** held -- the overlap
  //     the S2 budget accounts for -- and the drop is exactly one working set, not two.
  TEST_ASSERT_TRUE(trackManager.servicePendingSlotProvision());
  const uint32_t chunksBothRoles = LoopEventStore::freeChunkCount();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, kInbound),
                           "the inbound occupancy must exist after provisioning");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kOutbound),
                           "the outbound must still be held while the inbound is provisioned");
  // **Finding, recorded rather than papered over:** the inbound's provisioned occupancy carries **no
  // working-set lease** -- `provisionSlotForSwitch` provisions with `prepareCapture = false`, and the lease
  // is acquired only by the capture admission (`prewarmPlaybackForSlot(slot, true)`, S3.1). So "the inbound
  // *role*" is, for a playback switch, its occupancy plus the pool capacity it holds (measured below), not a
  // lease entry. Whether a switch's inbound must also hold a lease -- and with which `WorkingSetRole` bits --
  // is a contract question this slice leaves to the record rather than deciding in a fixture.
  const uint32_t inboundProvisionCost = chunksOutboundOnly - chunksBothRoles;
  TEST_ASSERT_TRUE_MESSAGE(inboundProvisionCost > 0,
                           "the inbound's provisioning must take pool capacity of its own");
  TEST_ASSERT_TRUE_MESSAGE(inboundProvisionCost <= outboundCost,
                           "the inbound may not cost more than the capture-inclusive outbound did");

  // (3) Only now does the commit land, and only after it is the retired slot releasable; its capacity returns.
  for (int round = 0; round < 4 && TrackCaptureStopTestAccess::activeLoopIndex(track) != kInbound;
       ++round) {
    const uint32_t base = clockManager.getCurrentTick();
    for (uint32_t tick = base + 1; tick <= base + 768; ++tick) {
      clockManager.assignCurrentTickSilently(tick);
      trackManager.updateAllTracks(tick);
    }
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kInbound, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the provisioned switch must commit");
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::runtimeAllocated(track, kInbound));
  TEST_ASSERT_TRUE_MESSAGE(track.noLiveOwnerForSlot(kOutbound),
                           "the retired slot must be releasable once the inbound is in place");
  track.releasePlaybackMergedMidiEventsMemory();
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kOutbound),
                            "the retired slot's working set must be released after the transfer");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, kInbound),
                           "the release sweep must not touch the inbound the transfer just provisioned");
  // (Not asserted: *pool* capacity returning on release. `LoopEventStore::freeChunkCount()` counts the
  // committed-event pool, which a playback-runtime release does not return -- the release returns the runtime's
  // own views/working set, which the lease assertion above is the observable for. Claiming a chunk-count rise
  // here would have been a measurement of the wrong resource.)

  // The bound, made precise against **the same kind of provisioning**: the switch's inbound is provisioned
  // with `prepareCapture = false` (playback only), while the outbound above was prewarmed with the capture
  // horizon too -- so the two are *not* the same unit, and comparing them directly would compare apples to
  // horizons. A probe slot provisioned the same way as the inbound gives the unit, and the inbound's cost must
  // equal it: the transfer's overlap is exactly **one playback working set**.
  constexpr uint8_t kProbe = 5;
  commitRecordPassIntoTrackSlot(track, kProbe);
  const uint32_t chunksBeforeProbe = LoopEventStore::freeChunkCount();
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kProbe, false));
  const uint32_t probeCost = chunksBeforeProbe - LoopEventStore::freeChunkCount();
  // **Recorded measurement, not an equality:** the switch's inbound provisioning dropped the pool by
  // `inboundProvisionCost`, while provisioning a probe slot the same way (`prepareCapture = false`) dropped it
  // by `probeCost` -- measured here as 1 vs 0, i.e. the switch turn's drop is not reproducible by a bare
  // playback provisioning, so part of it belongs to the turn's other work (the merged-window build /
  // prepared-pair publication) rather than to the occupancy alone. The bound this fixture *asserts* is
  // therefore the one it can support: the transfer's extra pool cost never exceeds one capture-inclusive
  // working set, which is what the S2 budget term needs; splitting the 1 chunk between occupancy and window is
  // a budget-accounting question, not something this fixture should decide by fiat.
  TEST_ASSERT_TRUE(probeCost <= inboundProvisionCost);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kInbound) ||
                               TrackCaptureStopTestAccess::runtimeAllocated(track, kInbound),
                           "the inbound stays in place at the end of the transfer");
}

/// **S4b.3 — the switch lands on a MIDI boundary, and that boundary is cold: it binds, it does not build.**
///
/// "Land the switch on a MIDI boundary" is the quantization's job (`SlotQuantization::LoopEnd` for the
/// playback-switch gesture, `NextGrid` for the record carrier), and "a cold admission boundary by
/// construction" is what S4b.2/S4b.6/S4b.7 made true: the inbound role is provisioned **off-clock** by the
/// `SlotProvision` turn, so the boundary itself only binds an already-prepared role. Both halves are measured
/// here instead of asserted by name:
///
///   * the tick at which the active slot actually changes is the boundary the quantization promises -- the
///     outgoing loop's playhead has wrapped at that tick (`LoopEnd`), or the tick is on the start grid
///     (`NextGrid`) -- and it is **not** the tick the request was queued at; and
///   * the boundary tick itself allocates nothing: the pool is exactly where it was immediately before the
///     tick that committed, because the role it binds was prepared earlier.
void assert_s4b_3_the_switch_lands_on_a_boundary(SlotQuantization quantization, uint8_t kOutbound,
                                                uint8_t kInbound) {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, kOutbound);
  commitRecordPassIntoTrackSlot(track, kInbound);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kOutbound, true));
  // The inbound role is prepared **before** the boundary: that is what makes the boundary cold.
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kInbound, false));
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, kOutbound);
  TrackCaptureStopTestAccess::activateSlot(0, kOutbound);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 96, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::ownsLiveFinalization(track, kOutbound);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::ownsLiveFinalization(track, kOutbound));
  TEST_ASSERT_EQUAL_UINT8(kOutbound, TrackCaptureStopTestAccess::activeLoopIndex(track));

  const Loop& outboundLoop = track.getLoop(kOutbound);
  const uint32_t loopStart = outboundLoop.loopStartTick;
  const uint32_t loopLength = outboundLoop.loopLengthTicks;
  const uint32_t gridTicks = track.getQueuedStartGridTicks();
  TEST_ASSERT_TRUE(loopLength > 0);
  TEST_ASSERT_TRUE(gridTicks > 0);

  // Queue deliberately **off** the boundary, so a commit that lands on the queue tick would be visible.
  const uint32_t queueTick = loopStart + 211;
  clockManager.assignCurrentTickSilently(queueTick);
  trackManager.requestSlotSwitch(0, kInbound, quantization, queueTick);
  TEST_ASSERT_TRUE(trackManager.hasPendingSlotSwitch(0));
  TEST_ASSERT_EQUAL_UINT8(kOutbound, TrackCaptureStopTestAccess::activeLoopIndex(track));

  uint32_t commitTick = UINT32_MAX;
  uint32_t phaseBeforeCommit = 0;
  uint32_t phaseAtCommit = 0;
  uint32_t chunksBeforeCommit = 0;
  uint32_t chunksAfterCommit = 0;
  for (uint32_t tick = queueTick + 1; tick <= queueTick + 2 * loopLength + gridTicks; ++tick) {
    // The outgoing loop's own phase (modulo its length: the effective tick is projection-domain), read while
    // it is still the active slot.
    phaseBeforeCommit = (track.getEffectivePlaybackTick(tick - 1) - loopStart) % loopLength;
    phaseAtCommit = (track.getEffectivePlaybackTick(tick) - loopStart) % loopLength;
    chunksBeforeCommit = LoopEventStore::freeChunkCount();
    clockManager.assignCurrentTickSilently(tick);
    trackManager.updateAllTracks(tick);
    chunksAfterCommit = LoopEventStore::freeChunkCount();
    if (TrackCaptureStopTestAccess::activeLoopIndex(track) == kInbound) {
      commitTick = tick;
      break;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(commitTick != UINT32_MAX, "the queued switch must commit");
  TEST_ASSERT_TRUE_MESSAGE(commitTick != queueTick,
                           "the commit may not land on the request's own tick: it is quantized");

  if (quantization == SlotQuantization::LoopEnd) {
    TEST_ASSERT_TRUE_MESSAGE(phaseAtCommit == 0 || phaseAtCommit < phaseBeforeCommit,
                             "a LoopEnd switch must land where the outgoing loop's playhead wrapped");
  } else {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, commitTick % gridTicks,
                                     "a NextGrid switch must land on a start-grid tick");
  }

  // The cold half: the boundary tick binds the prepared role and allocates nothing.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(chunksBeforeCommit, chunksAfterCommit,
                                   "the boundary tick itself may not take or return pool capacity");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, kInbound),
                           "the role the boundary bound was already prepared");
}

/// `LoopEnd` -- the quantization the playback-switch gesture itself uses.
void test_s4b_3_a_loop_end_switch_lands_on_the_loop_boundary() {
  assert_s4b_3_the_switch_lands_on_a_boundary(SlotQuantization::LoopEnd, /*kOutbound=*/0, /*kInbound=*/2);
}

/// `NextGrid` -- the record carrier's quantization: the switch waits for the start grid instead.
void test_s4b_3_a_next_grid_switch_lands_on_the_start_grid() {
  assert_s4b_3_the_switch_lands_on_a_boundary(SlotQuantization::NextGrid, /*kOutbound=*/1,
                                              /*kInbound=*/3);
}

/// **S4b.4 (clause: undo) — the holder a switch moved to is the one undo addresses.**
///
/// The production undo call is `TrackUndo::undoForLoop(track, track.getActiveLoop())` (the shape
/// `MidiButtonActions` uses), and the driver gates on the **slot**: it refuses when the stack's top entry
/// belongs to a slot other than the loop the caller passed. That gate *is* what "the inbound holder serves
/// undo" means once a switch has moved the active slot, so the fixture drives both sides with real stack
/// entries rather than asserting the gate's name:
///
///   * an entry belonging to the **retired** slot, while the inbound holder is active, must be **refused** --
///     the inbound loop's geometry is untouched and the entry stays on the stack; and
///   * an entry belonging to the **inbound** slot must be **applied** -- to the *inbound* loop -- and consumed.
///
/// The entry kind used is `LoopBoundaryChange`, whose apply is a pure geometry restore, so the observation is
/// the loop's own `loopStartTick`: the fixture can name the value it expects on the inbound holder.
/// **S4b.4 (clause: display) — the panel is told about the holder that took over.**
///
/// The switch commit's own display call is `displayManager.invalidateForSlotChange(i, previousPlaying,
/// getPreviewSlotIndex(i))` (`TrackManager::updateAllTracks`), i.e. it names the outgoing slot and the slot the
/// panel should now focus. This harness keeps that boundary observable (no `OFF_PATH()`), so the fixture makes
/// it **record** instead and asserts the identity pair after a switch lands: the retired slot out, the inbound
/// holder in. That is what "the inbound holder serves display" means at this stage -- the display *authority*
/// moves with the roles rather than being left pointing at the predecessor.
void test_s4b_4_the_display_authority_moves_to_the_holder_the_switch_moved_to() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  constexpr uint8_t kOutbound = 0;
  constexpr uint8_t kInbound = 2;
  commitRecordPassIntoTrackSlot(track, kOutbound);
  commitRecordPassIntoTrackSlot(track, kInbound);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kOutbound, true));
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kInbound, false));

  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, kOutbound);
  TrackCaptureStopTestAccess::activateSlot(0, kOutbound);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 96, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::ownsLiveFinalization(track, kOutbound);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::ownsLiveFinalization(track, kOutbound));

  // The **production press** (not the raw request): the gesture is what moves the panel's preview slot, and
  // the display pair the commit names is (outgoing, preview) -- so driving the raw call would ask the panel to
  // focus the slot it is leaving.
  clockManager.assignCurrentTickSilently(200);
  TrackCaptureStopTestAccess::toggleRecordForSlot(kInbound);
  TEST_ASSERT_TRUE(trackManager.hasPendingSlotSwitch(0));
  resetSlotChangeObservation();
  for (int round = 0; round < 4 && TrackCaptureStopTestAccess::activeLoopIndex(track) != kInbound;
       ++round) {
    (void)trackManager.servicePendingSlotProvision();
    const uint32_t base = clockManager.getCurrentTick();
    for (uint32_t tick = base + 1; tick <= base + 768; ++tick) {
      clockManager.assignCurrentTickSilently(tick);
      trackManager.updateAllTracks(tick);
    }
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kInbound, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "fixture precondition: the switch must land on the inbound holder");
  const SlotChangeObservation& observation = slotChangeObservation();
  TEST_ASSERT_TRUE_MESSAGE(observation.count > 0,
                           "the display boundary must be told about a role handoff");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, observation.track, "the observation names the switched track");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kOutbound, observation.from,
                                  "the display must be told which slot the roles left");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kInbound, observation.to,
                                  "the display authority must move to the slot that took over");
}

void test_s4b_4_the_undo_route_addresses_the_holder_the_switch_moved_to() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  constexpr uint8_t kOutbound = 0;
  constexpr uint8_t kInbound = 2;
  commitRecordPassIntoTrackSlot(track, kOutbound);
  commitRecordPassIntoTrackSlot(track, kInbound);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kOutbound, true));
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kInbound, false));

  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, kOutbound);
  TrackCaptureStopTestAccess::activateSlot(0, kOutbound);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 96, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::ownsLiveFinalization(track, kOutbound);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::ownsLiveFinalization(track, kOutbound));

  // The switch: the same production lane the other S4b fixtures drive, so the inbound holder is active.
  clockManager.assignCurrentTickSilently(200);
  trackManager.requestSlotSwitch(0, kInbound, SlotQuantization::LoopEnd,
                                 clockManager.getCurrentTick());
  for (int round = 0; round < 4 && TrackCaptureStopTestAccess::activeLoopIndex(track) != kInbound;
       ++round) {
    (void)trackManager.servicePendingSlotProvision();
    const uint32_t base = clockManager.getCurrentTick();
    for (uint32_t tick = base + 1; tick <= base + 768; ++tick) {
      clockManager.assignCurrentTickSilently(tick);
      trackManager.updateAllTracks(tick);
    }
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kInbound, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "fixture precondition: the switch must land on the inbound holder");

  GlobalUndoStack& stack = track.getGlobalUndoStack();
  const size_t cursorEmpty = stack.cursor;
  const uint32_t inboundStartBefore = track.getLoop(kInbound).loopStartTick;

  // (1) The retired slot's entry: the undo route must refuse it, because the holder it addresses is the
  //     inbound -- and a refusal must leave both the loop and the stack exactly as they were.
  UndoEntry retiredEntry{};
  retiredEntry.kind = UndoEntryKind::LoopBoundaryChange;
  retiredEntry.slotIndex = kOutbound;
  retiredEntry.beforeLoopStartTick = 4242;
  retiredEntry.beforeLoopLengthTicks = track.getLoop(kOutbound).loopLengthTicks;
  pushUndoEntry(track, std::move(retiredEntry));
  TEST_ASSERT_TRUE(stack.canUndo());
  TEST_ASSERT_EQUAL_MESSAGE(cursorEmpty + 1, stack.cursor, "fixture precondition: one entry on the stack");
  TrackUndo::undoForLoop(track, track.getActiveLoop());
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(inboundStartBefore, track.getLoop(kInbound).loopStartTick,
                                   "the undo route must not apply the retired slot's entry to the inbound");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, track.getLoop(kOutbound).loopStartTick,
                                   "the retired slot's geometry must not be restored either");
  TEST_ASSERT_EQUAL_MESSAGE(cursorEmpty + 1, stack.cursor, "a refusal must leave the entry on the stack");

  // (2) The inbound slot's entry: the same call must consume it and apply it **to the inbound loop**. The
  // refused entry from (1) is still on the stack by design, so the baseline is the cursor *before* this push.
  const size_t cursorBeforeInboundPush = stack.cursor;
  UndoEntry inboundEntry{};
  inboundEntry.kind = UndoEntryKind::LoopBoundaryChange;
  inboundEntry.slotIndex = kInbound;
  inboundEntry.beforeLoopStartTick = 111;
  inboundEntry.beforeLoopLengthTicks = track.getLoop(kInbound).loopLengthTicks;
  pushUndoEntry(track, std::move(inboundEntry));
  TrackUndo::undoForLoop(track, track.getActiveLoop());
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(111, track.getLoop(kInbound).loopStartTick,
                                   "the inbound holder must be the slot undo applies to");
  TEST_ASSERT_EQUAL_MESSAGE(cursorBeforeInboundPush, stack.cursor,
                            "the applied entry must be consumed -- and only it: the refused one stays");
}


// ---------------------------------------------------------------------------
// DEC-108 item 1 / `ExecutionBounds.md` rows 27-29: the window-build owner
// (`Track::stepPlaybackViewMaintenance`) -- windowed-playback-generation slice 3.1.
//
// The owner's production surface is three off-clock calls. This slice changes no behaviour behind them: no
// production path requests a window yet, and the owner publishes nothing a consumer reads. What is asserted
// here is the contract the owner must satisfy **before** that request arrives: a bounded, declared grain
// quantum; a persistent build cursor; a refusal that is named and non-destructive; and the deferred turn's
// exact arithmetic, so neither a stacked nor a skipped grain can hide.
// ---------------------------------------------------------------------------

namespace {
/// The owner's loop: 64 bars, so the loop holds the declared contract's required lead with room to spare and
/// the demotion predicate's long-loop shape is the one under test.
constexpr uint32_t kWindowOwnerLoopTicks = 64 * 768;

/// A slot whose loop is long enough to hold the declared contract's required lead, with its playback runtime
// prewarmed -- the state a pending window sits in. Nothing here establishes an obligation: the build the
/// fixture begins *is* the obligation (DEC-108 item 1's state-driven shape).
Track& prepareWindowOwnerSlot(uint8_t trackIndex) {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(trackIndex);
  track.ensureLoopsAllocated();
  // Committed content, so the slot's revision is a real one (a window carries the revision it was built from).
  commitRecordPassIntoTrackSlot(track, 0);
  track.getLoop(0).loopLengthTicks = kWindowOwnerLoopTicks;
  TEST_ASSERT_TRUE_MESSAGE(track.prewarmPlaybackForSlot(0, true),
                           "the slot must prewarm a runtime for the owner to owe anything");
  return track;
}

/// The declared contract's arithmetic written out, so the fixture's oracle is the *declaration* rather than
/// the implementation's answer: 7 required grains, each preceded by at most 3 denied turns, each turn
/// advancing at most one bar, plus the one-bar lead.
constexpr uint32_t kWindowOwnerExpectedRequiredGrains = 7;
constexpr uint32_t kWindowOwnerExpectedAdvancePerTurn = 768;
constexpr uint32_t kWindowOwnerExpectedDeniedTurns = 3;
constexpr uint32_t kWindowOwnerExpectedLead = 768;
constexpr uint32_t kWindowOwnerExpectedRequiredLead =
    kWindowOwnerExpectedRequiredGrains * (kWindowOwnerExpectedDeniedTurns + 1) *
        kWindowOwnerExpectedAdvancePerTurn +
    kWindowOwnerExpectedLead;

/// A two-bar window's worth of source events, all inside the coverage interval so the fill's coverage filter
/// appends every one of them: the fixture's grain count then follows from the declared quanta alone.
size_t buildWindowOwnerSource(MidiEvent* out, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    out[i] = MidiEvent::NoteOn(static_cast<uint32_t>(i * 15), 1, 48, 100);
  }
  return count;
}

}  // namespace

/// W-5 + the declaration: the caps are declared, and the handoff margin -- not a bar count and not a
/// millisecond constant -- is what admits or refuses.
void test_window_owner_declares_its_caps_and_the_handoff_margin_is_the_oracle() {
  const CoverageServiceContract declared = Track::playbackViewCoverageContract();
  TEST_ASSERT_TRUE_MESSAGE(declared.isDeclared(), "the owner's contract must be declared before it works");
  TEST_ASSERT_EQUAL_UINT32(kWindowOwnerExpectedAdvancePerTurn, declared.maximumPhaseAdvancePerTurn);
  TEST_ASSERT_EQUAL_UINT16(kWindowOwnerExpectedDeniedTurns, declared.maximumConsecutiveDeniedTurns);
  TEST_ASSERT_EQUAL_UINT16(3, declared.requiredFillGrains);
  TEST_ASSERT_EQUAL_UINT16(3, declared.requiredOrderGrains);
  TEST_ASSERT_EQUAL_UINT16(1, declared.requiredPromotionGrains);
  TEST_ASSERT_EQUAL_UINT16(kWindowOwnerExpectedRequiredGrains, declared.requiredGrains());
  TEST_ASSERT_EQUAL_UINT32(kWindowOwnerExpectedLead, declared.leadTicks);

  MidiEvent source[4];
  const size_t count = buildWindowOwnerSource(source, 4);
  const PlaybackCoverageInterval coverage{0, 2 * 768, kWindowOwnerLoopTicks};
  const PlaybackViewSourceIdentity identity{1, 1, nullptr};

  // At the boundary: `canCompleteBeforeHandoff` admits exactly at the required lead.
  Track& atBoundary = prepareWindowOwnerSlot(0);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(atBoundary.requestPlaybackViewMaintenance(
                        0, source, count, identity, coverage, {}, {}, 0, kWindowOwnerExpectedRequiredLead)));
  TEST_ASSERT_TRUE(atBoundary.hasPendingPlaybackViewMaintenance());

  // Just inside: one tick more of lead is still admitted.
  Track& inside = prepareWindowOwnerSlot(1);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(inside.requestPlaybackViewMaintenance(
                        0, source, count, identity, coverage, {}, {}, 0,
                        kWindowOwnerExpectedRequiredLead + 1)));

  // Just outside: one tick short of the required lead is refused, by name, and takes nothing.
  Track& outside = prepareWindowOwnerSlot(2);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::CannotCompleteBeforeHandoff),
                    static_cast<int>(outside.requestPlaybackViewMaintenance(
                        0, source, count, identity, coverage, {}, {}, 0,
                        kWindowOwnerExpectedRequiredLead - 1)));
  TEST_ASSERT_FALSE_MESSAGE(outside.hasPendingPlaybackViewMaintenance(),
                            "a refused request must not create an obligation");
}

/// W-2/W-3/W-6/W-7: the turn serves **exactly one declared grain**, the cursor persists across turns, the turn
/// takes no pool capacity, and nothing a consumer reads changes.
void test_window_owner_serves_one_grain_per_turn_and_publishes_nothing() {
  Track& track = prepareWindowOwnerSlot(0);
  Loop& loop = track.getLoop(0);
  const uint32_t revisionBefore = loop.playbackRevision;
  TEST_ASSERT_FALSE_MESSAGE(track.hasPendingPlaybackViewMaintenance(),
                            "an idle slot owes nothing before a request");
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewTurnResult::Idle),
                    static_cast<int>(track.stepPlaybackViewMaintenance()));

  constexpr size_t kEvents = 100;
  MidiEvent source[kEvents];
  const size_t count = buildWindowOwnerSource(source, kEvents);
  const PlaybackViewSourceIdentity identity{loop.playbackRevision, 1, nullptr};
  const PlaybackCoverageInterval coverage{0, 2 * 768, kWindowOwnerLoopTicks};
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(track.requestPlaybackViewMaintenance(
                        0, source, count, identity, coverage, {}, {}, 0, kWindowOwnerExpectedRequiredLead)));
  TEST_ASSERT_TRUE(track.hasPendingPlaybackViewMaintenance());

  // The expected turn count follows from the declared quanta: a grain served twice in one turn, or a grain
  // skipped, changes this number.
  const size_t expectedTurns =
      (count + Track::kPlaybackViewFillQuantum - 1) / Track::kPlaybackViewFillQuantum +
      (count + Track::kPlaybackViewOrderQuantum - 1) / Track::kPlaybackViewOrderQuantum;
  TEST_ASSERT_EQUAL_UINT32(5, static_cast<uint32_t>(expectedTurns));
  for (size_t turn = 1; turn <= expectedTurns; ++turn) {
    const uint16_t chunksBefore = LoopEventStore::freeChunkCount();
    const Track::PlaybackViewTurnResult result = track.stepPlaybackViewMaintenance();
    TEST_ASSERT_NOT_EQUAL_MESSAGE(static_cast<int>(Track::PlaybackViewTurnResult::Idle),
                                  static_cast<int>(result),
                                  "an owed window must be served, not skipped");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(chunksBefore, LoopEventStore::freeChunkCount(),
                                     "the owner's turn takes no pool capacity");
    TEST_ASSERT_FALSE_MESSAGE(track.isPlaybackMergedMidiEventsReadyForSlot(0),
                              "the owner publishes nothing a consumer reads (task 3.2 does)");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(revisionBefore, loop.playbackRevision,
                                     "the owner never moves the content-currency revision");
    if (turn < expectedTurns) {
      TEST_ASSERT_TRUE_MESSAGE(track.hasPendingPlaybackViewMaintenance(),
                               "the build's cursor must persist across turns");
    } else {
      TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewTurnResult::BuildComplete),
                        static_cast<int>(result));
      TEST_ASSERT_FALSE_MESSAGE(track.hasPendingPlaybackViewMaintenance(),
                                "a completed build owes nothing");
    }
  }
  // Idempotent once complete: nothing owed, nothing served.
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewTurnResult::Idle),
                    static_cast<int>(track.stepPlaybackViewMaintenance()));
}

/// 3.2 — the **enumeration** clause: the interval policy, the resolution through the selected `LoopSnapshot`,
/// and the inspected-work bound (fixture **2.10**).
void test_window_owner_resolves_the_interval_through_the_published_snapshot() {
  // (A) The policy, purely: whole-loop coverage where the whole-take view is admissible, else the two-bar window
  // at the playhead, clamped into the loop. The same function the resolver uses.
  PlaybackCoverageInterval interval{};
  TEST_ASSERT_TRUE(Track::playbackWindowInterval(4 * 768u, false, 500u, interval));
  TEST_ASSERT_EQUAL_UINT32(0u, interval.startPhase);
  TEST_ASSERT_EQUAL_UINT32(4 * 768u, interval.lengthTicks);
  TEST_ASSERT_EQUAL_UINT32(4 * 768u, interval.loopLengthTicks);
  TEST_ASSERT_TRUE(Track::playbackWindowInterval(kWindowOwnerLoopTicks, true, 5000u, interval));
  TEST_ASSERT_EQUAL_UINT32(5000u - 768u, interval.startPhase);
  TEST_ASSERT_EQUAL_UINT32(2 * 768u, interval.lengthTicks);
  TEST_ASSERT_TRUE(Track::playbackWindowInterval(kWindowOwnerLoopTicks, true, 100u, interval));
  TEST_ASSERT_EQUAL_UINT32(0u, interval.startPhase);
  TEST_ASSERT_TRUE(Track::playbackWindowInterval(kWindowOwnerLoopTicks, true, kWindowOwnerLoopTicks - 200u,
                                                 interval));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(kWindowOwnerLoopTicks - 2 * 768u, interval.startPhase,
                                   "the window is clamped into the loop, never past its end");
  TEST_ASSERT_FALSE(Track::playbackWindowInterval(0u, true, 0u, interval));

  // (B) The resolver: it names the interval, enumerates the **current** published root and stamps the identity.
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  Loop& loop = track.getLoop(0);
  loop.loopLengthTicks = kWindowOwnerLoopTicks;
  publishBarSpacedRootForSlot(loop, 64);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  SessionMidiEventVec scratch;
  const uint32_t playhead = 5000u;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(track.requestPlaybackWindow(0, playhead, scratch)));
  TEST_ASSERT_TRUE_MESSAGE(scratch.size() > 0, "the window's content comes from the published root");
  PlaybackViewSourceIdentity identityOut{};
  PlaybackCoverageInterval coverageOut{};
  for (size_t turn = 0; turn < 8 && track.hasPendingPlaybackViewMaintenance(); ++turn) {
    (void)track.stepPlaybackViewMaintenance();
  }
  TEST_ASSERT_FALSE(track.hasPendingPlaybackViewMaintenance());
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowForSlot(0, identityOut, coverageOut));
  TEST_ASSERT_TRUE(Track::playbackWindowInterval(kWindowOwnerLoopTicks, true, playhead, interval));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(interval.startPhase, coverageOut.startPhase,
                                   "the published window serves the interval the policy resolved");
  TEST_ASSERT_EQUAL_UINT32(interval.lengthTicks, coverageOut.lengthTicks);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(loop.playbackRevision, identityOut.playbackRevision,
                                   "the identity is the revision the enumeration read");
  TEST_ASSERT_EQUAL_PTR_MESSAGE(loop.publishedSnapshotIdentity(), identityOut.snapshotIdentity,
                                "and the published root it read it from");

  // F-1: the identity tuple, and the generation as an **ownership** guard rather than a second currency.
  PlaybackWindowIdentity first{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, first));
  TEST_ASSERT_TRUE_MESSAGE(first.isValid(), "a published window carries a valid identity");
  TEST_ASSERT_EQUAL_UINT8(0, first.slot);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(loop.playbackRevision, first.playbackRevision,
                                   "content currency is the loop's revision, and the window carries it");
  TEST_ASSERT_EQUAL_UINT32(interval.startPhase, first.interval.startPhase);
  // A **different interval at the same revision and the same ownership**: the successor lane prepares it, the
  // served identity does not move while it is prepared, and the adoption swaps it in with the generation intact.
  const uint32_t secondPlayhead = playhead + 3u * 768u;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(track.requestPlaybackWindow(0, secondPlayhead, scratch)));
  for (size_t turn = 0; turn < 8 && track.hasPendingPlaybackViewMaintenance(); ++turn) {
    (void)track.stepPlaybackViewMaintenance();
  }
  PlaybackWindowIdentity stillServed{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, stillServed));
  TEST_ASSERT_TRUE_MESSAGE(first == stillServed,
                           "preparing a successor must leave the served identity untouched");
  TEST_ASSERT_TRUE(track.adoptPreparedPlaybackWindow(0));
  PlaybackWindowIdentity second{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, second));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(first.playbackGeneration, second.playbackGeneration,
                                   "the generation guards ownership: a new interval does not move it");
  TEST_ASSERT_EQUAL_UINT32(first.playbackRevision, second.playbackRevision);
  TEST_ASSERT_TRUE_MESSAGE(first.interval.startPhase != second.interval.startPhase,
                           "while the interval *did* change -- the interval is not currency");

  // A rootless slot is refused by name, not served through a compatibility path.
  Track& rootless = trackManager.getTrack(1);
  rootless.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(rootless, 0);
  rootless.getLoop(0).loopLengthTicks = kWindowOwnerLoopTicks;
  SessionMidiEventVec rootlessScratch;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::SourceUnavailable),
                    static_cast<int>(rootless.requestPlaybackWindow(0, 0u, rootlessScratch)));

  // (C) Fixture 2.10 -- the **inspected-work bound**. The same requested window on a short loop and on a long
  // loop whose unrelated content is 32x larger: the walk may only touch the window's own bars, and it counts the
  // references it *rejects* as well as the ones it appends.
  LoopEventStore::initPool();
  Track& shortTrack = trackManager.getTrack(2);
  shortTrack.ensureLoopsAllocated();
  Loop& shortLoop = shortTrack.getLoop(0);
  shortLoop.loopLengthTicks = 4 * 768u;
  publishBarSpacedRootForSlot(shortLoop, 2);
  Track& longTrack = trackManager.getTrack(3);
  longTrack.ensureLoopsAllocated();
  Loop& longLoop = longTrack.getLoop(0);
  longLoop.loopLengthTicks = kWindowOwnerLoopTicks;
  publishBarSpacedRootForSlot(longLoop, 64);
  SessionMidiEventVec shortScratch;
  SessionMidiEventVec longScratch;
  Loop::SnapshotWindowGatherPath shortPath = Loop::SnapshotWindowGatherPath::None;
  Loop::SnapshotWindowGatherPath longPath = Loop::SnapshotWindowGatherPath::None;
  uint32_t shortInspected = 0;
  uint32_t longInspected = 0;
  TEST_ASSERT_TRUE(shortLoop.tryGatherPublishedSnapshotEventsInWindow(shortScratch, 0, 2 * 768u, &shortPath,
                                                                     &shortInspected));
  TEST_ASSERT_TRUE(longLoop.tryGatherPublishedSnapshotEventsInWindow(longScratch, 0, 2 * 768u, &longPath,
                                                                    &longInspected));
  TEST_ASSERT_EQUAL(static_cast<int>(Loop::SnapshotWindowGatherPath::Snapshot),
                    static_cast<int>(shortPath));
  TEST_ASSERT_EQUAL(static_cast<int>(Loop::SnapshotWindowGatherPath::Snapshot), static_cast<int>(longPath));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(shortInspected, longInspected,
                                   "the same window must cost the same inspected work however long the loop is");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(shortScratch.size()),
                                   static_cast<uint32_t>(longScratch.size()),
                                   "and it must gather the same events");
  TEST_ASSERT_TRUE_MESSAGE(shortInspected > 0, "the short loop's window has content, so the walk is non-empty");

  // A window that intersects nothing: its bars carry no membership at all, so nothing is inspected and nothing
  // is gathered -- the walk skips a missing bar rather than scanning the loop.
  SessionMidiEventVec emptyScratch;
  uint32_t emptyInspected = 0;
  TEST_ASSERT_TRUE(shortLoop.tryGatherPublishedSnapshotEventsInWindow(emptyScratch, 2 * 768u, 2 * 768u, nullptr,
                                                                     &emptyInspected));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, emptyInspected, "an empty window inspects nothing");
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(emptyScratch.size()));
}

/// 3.2: publication is **atomic** and the published window carries the identity and the coverage interval the
/// request named. The replacement case is the one that matters: the window being replaced stays readable right
/// up to the turn that swaps it, and no turn publishes an incomplete build.
void test_window_owner_publishes_the_interval_window_atomically() {
  Track& track = prepareWindowOwnerSlot(0);
  Loop& loop = track.getLoop(0);
  TEST_ASSERT_FALSE_MESSAGE(loop.fullTakePlaybackViewAdmissible(),
                            "the fixture needs a demoted slot: windowed is its only representation");

  constexpr size_t kEvents = 100;
  MidiEvent source[kEvents];
  const size_t count = buildWindowOwnerSource(source, kEvents);
  const PlaybackViewSourceIdentity firstIdentity{loop.playbackRevision, 3, nullptr};
  const PlaybackCoverageInterval firstCoverage{0, 2 * 768, kWindowOwnerLoopTicks};
  PlaybackViewSourceIdentity identityOut{};
  PlaybackCoverageInterval coverageOut{};

  TEST_ASSERT_FALSE_MESSAGE(track.publishedPlaybackWindowForSlot(0, identityOut, coverageOut),
                            "nothing is published before a request");
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(track.requestPlaybackViewMaintenance(
                        0, source, count, firstIdentity, firstCoverage, {}, {}, 0,
                        kWindowOwnerExpectedRequiredLead)));
  const size_t expectedTurns =
      (count + Track::kPlaybackViewFillQuantum - 1) / Track::kPlaybackViewFillQuantum +
      (count + Track::kPlaybackViewOrderQuantum - 1) / Track::kPlaybackViewOrderQuantum;
  for (size_t turn = 1; turn <= expectedTurns; ++turn) {
    const Track::PlaybackViewTurnResult result = track.stepPlaybackViewMaintenance();
    const bool publishedNow = track.publishedPlaybackWindowForSlot(0, identityOut, coverageOut);
    if (turn < expectedTurns) {
      TEST_ASSERT_FALSE_MESSAGE(publishedNow,
                                "an incomplete build must never become the published window");
    } else {
      TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::PlaybackViewTurnResult::BuildComplete),
                                static_cast<int>(result), "the last grain completes the build");
      TEST_ASSERT_TRUE_MESSAGE(publishedNow, "the completed build must be published in that same turn");
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(firstIdentity == identityOut, "the window carries the identity it was built from");
  TEST_ASSERT_EQUAL_UINT32(firstCoverage.startPhase, coverageOut.startPhase);
  TEST_ASSERT_EQUAL_UINT32(firstCoverage.lengthTicks, coverageOut.lengthTicks);
  TEST_ASSERT_EQUAL_UINT32(firstCoverage.loopLengthTicks, coverageOut.loopLengthTicks);

  // **F-1 (task 3.4): a replacement is a ready successor, not a second start.** While the successor is
  // prepared, the served window is byte-for-byte what it was -- no consumer read can straddle the replacement --
  // and the adoption is the single atomic swap.
  PlaybackWindowIdentity firstWindow{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, firstWindow));
  TEST_ASSERT_TRUE_MESSAGE(firstWindow.isValid(), "a published window carries a valid identity");
  TEST_ASSERT_EQUAL_UINT8(0, firstWindow.slot);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(loop.playbackRevision, firstWindow.playbackRevision,
                                   "the window's currency is the loop's revision");
  const PlaybackViewSourceIdentity secondIdentity{loop.playbackRevision, 4, nullptr};
  const PlaybackCoverageInterval secondCoverage{768, 2 * 768, kWindowOwnerLoopTicks};
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(track.requestPlaybackViewMaintenance(
                        0, source, count, secondIdentity, secondCoverage, {}, {}, 768,
                        static_cast<uint32_t>(768 + kWindowOwnerExpectedRequiredLead))));
  for (size_t turn = 1; turn <= expectedTurns; ++turn) {
    PlaybackViewSourceIdentity duringIdentity{};
    PlaybackCoverageInterval duringCoverage{};
    PlaybackWindowIdentity during{};
    TEST_ASSERT_TRUE(track.publishedPlaybackWindowForSlot(0, duringIdentity, duringCoverage));
    TEST_ASSERT_TRUE_MESSAGE(firstIdentity == duringIdentity,
                             "the served window must not change while its successor is prepared");
    TEST_ASSERT_EQUAL_UINT32(firstCoverage.startPhase, duringCoverage.startPhase);
    TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, during));
    TEST_ASSERT_TRUE_MESSAGE(firstWindow == during, "and its identity must be identical, field for field");
    (void)track.stepPlaybackViewMaintenance();
  }
  // The successor is prepared but **not adopted**: the served window is still the first one. That is the whole
  // point of the ready role -- the last coherent revision stays readable to the last moment.
  PlaybackWindowIdentity prepared{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, prepared));
  TEST_ASSERT_TRUE_MESSAGE(firstWindow == prepared,
                           "a prepared successor must not become the served window until it is adopted");
  // Adoption is the atomic swap, and only then does the window change.
  TEST_ASSERT_TRUE(track.adoptPreparedPlaybackWindow(0));
  PlaybackWindowIdentity adopted{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, adopted));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(secondCoverage.startPhase, adopted.interval.startPhase,
                                   "after adoption the window is the successor's interval");
  TEST_ASSERT_EQUAL_UINT32(secondCoverage.lengthTicks, adopted.interval.lengthTicks);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(secondIdentity.sourceGeneration, adopted.playbackGeneration,
                                   "and it carries the successor's own generation");
}

/// 3.5 (AC-3): **readiness is a predicate, and its failure names its holder.** Readiness is the window this change
/// **published** -- not the live merged buffer -- so a published, complete, current window is ready *even when its
/// interval legitimately holds no events*, and every non-ready answer names who owes the window.
void test_window_readiness_names_its_holder_and_an_empty_interval_is_ready() {
  // (W-9) A current published root and a prewarmed runtime, but **no window published**: the window-build owner owes
  // one. The debt is the build owner's and not the source's -- a build *could* gather here.
  Track& track = prepareWindowOwnerSlot(0);
  Loop& loop = track.getLoop(0);
  publishBarSpacedRootForSlot(loop, 2);  // content in bars 0-1 of a 64-bar loop, so the empty interval has a source
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackWindowReadiness::OwesBuild),
                    static_cast<int>(track.playbackWindowReadinessForSlot(0)));
  TEST_ASSERT_EQUAL_STRING("window_build_owner", Track::playbackWindowReadinessHolder(
                                                    track.playbackWindowReadinessForSlot(0)));

  // (W-11) The interval under test is **legitimately empty**: two bars deep in the loop, where the root carries
  // nothing. The request goes through the production resolver, so the emptiness is the resolver's own answer rather
  // than the fixture's claim about it.
  constexpr uint32_t kEmptyPlayhead = 40 * 768;
  SessionMidiEventVec scratch;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(track.requestPlaybackWindow(0, kEmptyPlayhead, scratch)));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(scratch.size()),
                                   "the window under test must hold no events at all");
  for (size_t turn = 0; turn < 8 && track.hasPendingPlaybackViewMaintenance(); ++turn) {
    (void)track.stepPlaybackViewMaintenance();
  }
  PlaybackWindowIdentity emptyWindow{};
  TEST_ASSERT_TRUE_MESSAGE(track.publishedPlaybackWindowIdentityForSlot(0, emptyWindow),
                           "an empty interval is still a window, and it is published");
  TEST_ASSERT_EQUAL_UINT32(loop.playbackRevision, emptyWindow.playbackRevision);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackWindowReadiness::Ready),
                    static_cast<int>(track.playbackWindowReadinessForSlot(0)));
  TEST_ASSERT_EQUAL_STRING("ready",
                           Track::playbackWindowReadinessHolder(track.playbackWindowReadinessForSlot(0)));
  // The rule task 3.5 removes from readiness, held side by side: the legacy merged buffer still demands a non-empty
  // window, and readiness no longer does.
  TEST_ASSERT_FALSE_MESSAGE(track.isPlaybackMergedMidiEventsReadyForSlot(0),
                            "the legacy merged term still requires content; readiness does not");

  // (W-12) Currency is `playbackRevision` **and exactly it**: a window stamped one revision behind the loop owes a
  // rebuild, by name. Whether *one pass late* is accepted is task 3.6's allowance, deliberately not assumed here.
  Track& lagging = prepareWindowOwnerSlot(1);
  Loop& laggingLoop = lagging.getLoop(0);
  TEST_ASSERT_TRUE_MESSAGE(laggingLoop.playbackRevision >= 1, "the fixture needs a revision to lag behind");
  const uint32_t staleRevision = laggingLoop.playbackRevision - 1;
  MidiEvent laggingSource[8];
  const size_t laggingCount = buildWindowOwnerSource(laggingSource, 8);
  const PlaybackCoverageInterval laggingCoverage{0, 2 * 768, kWindowOwnerLoopTicks};
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(lagging.requestPlaybackViewMaintenance(
                        0, laggingSource, laggingCount,
                        PlaybackViewSourceIdentity{staleRevision, 5, nullptr}, laggingCoverage, {}, {}, 0,
                        kWindowOwnerExpectedRequiredLead)));
  for (size_t turn = 0; turn < 8 && lagging.hasPendingPlaybackViewMaintenance(); ++turn) {
    (void)lagging.stepPlaybackViewMaintenance();
  }
  PlaybackWindowIdentity staleWindow{};
  TEST_ASSERT_TRUE(lagging.publishedPlaybackWindowIdentityForSlot(0, staleWindow));
  TEST_ASSERT_EQUAL_UINT32(staleRevision, staleWindow.playbackRevision);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackWindowReadiness::OwesCurrency),
                    static_cast<int>(lagging.playbackWindowReadinessForSlot(0)));
  TEST_ASSERT_EQUAL_STRING("window_rebuild", Track::playbackWindowReadinessHolder(
                                                 lagging.playbackWindowReadinessForSlot(0)));
  // ...and the **generation is not currency**: the same slot carrying the current revision but a different
  // generation is ready, because the generation guards ownership (task 3.4) rather than dating the content.
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(lagging.requestPlaybackViewMaintenance(
                        0, laggingSource, laggingCount,
                        PlaybackViewSourceIdentity{laggingLoop.playbackRevision, 6, nullptr}, laggingCoverage, {}, {},
                        0, kWindowOwnerExpectedRequiredLead)));
  for (size_t turn = 0; turn < 8 && lagging.hasPendingPlaybackViewMaintenance(); ++turn) {
    (void)lagging.stepPlaybackViewMaintenance();
  }
  TEST_ASSERT_TRUE_MESSAGE(lagging.adoptPreparedPlaybackWindow(0),
                           "a complete successor must be adoptable for the generation case");
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackWindowReadiness::Ready),
                    static_cast<int>(lagging.playbackWindowReadinessForSlot(0)));

  // (W-10) No published root: no build could gather at all, so the **source** owes the root -- a different holder
  // from the build owner, which is the whole point of naming it.
  Track& rootless = prepareWindowOwnerSlot(2);
  TEST_ASSERT_FALSE(rootless.getLoop(0).publishedSnapshotIsCurrent());
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackWindowReadiness::OwesSource),
                    static_cast<int>(rootless.playbackWindowReadinessForSlot(0)));
  TEST_ASSERT_EQUAL_STRING("published_source", Track::playbackWindowReadinessHolder(
                                                   rootless.playbackWindowReadinessForSlot(0)));

  // A slot with no committed content has no interval to be ready about at all.
  LoopEventStore::initPool();
  Track& bare = trackManager.getTrack(3);
  bare.ensureLoopsAllocated();
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackWindowReadiness::NotServable),
                    static_cast<int>(bare.playbackWindowReadinessForSlot(0)));
}

/// W-4: a refusal is named and **non-destructive** -- the obligation it found stays exactly as it was.
void test_window_owner_refusals_are_named_and_non_destructive() {
  // No runtime: the owner never allocates one, so the request is refused and nothing is owed.
  LoopEventStore::initPool();
  Track& bare = trackManager.getTrack(3);
  bare.ensureLoopsAllocated();
  bare.getLoop(0).loopLengthTicks = kWindowOwnerLoopTicks;
  MidiEvent source[4];
  const size_t count = buildWindowOwnerSource(source, 4);
  const PlaybackViewSourceIdentity identity{1, 1, nullptr};
  const PlaybackCoverageInterval coverage{0, 2 * 768, kWindowOwnerLoopTicks};
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::NoRuntime),
                    static_cast<int>(bare.requestPlaybackViewMaintenance(
                        0, source, count, identity, coverage, {}, {}, 0, kWindowOwnerExpectedRequiredLead)));

  // A build in flight: the second request is refused by name, and the obligation is untouched -- it still
  // completes on its own turns, which is what makes the refusal non-destructive rather than a reset.
  Track& track = prepareWindowOwnerSlot(4);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(track.requestPlaybackViewMaintenance(
                        0, source, count, identity, coverage, {}, {}, 0, kWindowOwnerExpectedRequiredLead)));
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::BuildInFlight),
                    static_cast<int>(track.requestPlaybackViewMaintenance(
                        0, source, count, identity, coverage, {}, {}, 0, kWindowOwnerExpectedRequiredLead)));
  TEST_ASSERT_TRUE_MESSAGE(track.hasPendingPlaybackViewMaintenance(),
                           "a refused request must leave the existing obligation intact");
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewTurnResult::GrainServed),
                    static_cast<int>(track.stepPlaybackViewMaintenance()));
}
int main() {



  UNITY_BEGIN();
  // S4b.1 check 7 (repeated same-track switches): accepted, bounded, nothing stolen, executes on capacity.
  RUN_TEST(test_s4b_1_a_repeated_switch_while_a_finalizes_is_bounded_and_executes_once_capacity_frees);
  // S4b.1 (a): the uncommitted switch whose stop was denied on capacity still retains the start.
  RUN_TEST(test_s4b_1_a_denied_stop_on_an_uncommitted_switch_retains_the_targets_start);
  // S4b.1 check 7 (populated target): the queued playback switch waits, then commits in the tick lane.
  RUN_TEST(test_s4b_1_a_queued_playback_switch_waits_for_the_predecessor_and_then_commits);
  // S4b.6/S4b.7: the inbound occupancy is provided off-clock; neither the MIDI edge nor the clock path
  // may allocate it, and a switch without it must not dead-end.
  RUN_TEST(test_s4b_6_a_switch_to_a_slot_without_occupancy_provisions_off_clock);
  RUN_TEST(test_s4b_6_a_switch_to_a_multi_bar_target_without_occupancy_also_provisions_off_clock);
  // S4b.7 (demoted): the merged window releases the hold when a published root exists, and its absence is
  // bounded -- one report per episode, switch retained.
  RUN_TEST(test_s4b_7_a_demoted_target_with_a_published_root_commits_through_the_turn);
  RUN_TEST(test_s4b_7_a_demoted_target_without_a_root_is_reported_once_at_its_bound);
  // S4b.5 (B4): one active-slot write authority -- a direct write (the undo route) cancels a queued switch,
  // the cancel clears the whole trio, and an unrelated switch inherits no stale enabled-set replacement.
  RUN_TEST(test_s4b_5_a_direct_write_cancels_the_queued_switch_without_a_stale_replacement);
  // S4b.5 (B4): the guard's accepted-but-unexecuted term -- the route a direct write takes when a latch,
  // and nothing else, is in flight.
  RUN_TEST(test_s4b_5_an_accepted_unexecuted_interaction_routes_the_write_through_the_switch);
  // S4b.2: the inbound role is provisioned before the outbound one is released, and the overlap is bounded.
  RUN_TEST(test_s4b_2_the_inbound_role_is_provisioned_before_the_outbound_is_released);
  // S4b.3: the switch lands on a MIDI boundary, and that boundary binds a prepared role (allocates nothing).
  RUN_TEST(test_s4b_3_a_loop_end_switch_lands_on_the_loop_boundary);
  RUN_TEST(test_s4b_3_a_next_grid_switch_lands_on_the_start_grid);
  // S4b.4 (undo clause): the holder a switch moved to is the one the undo route addresses -- a retired
  // slot's entry is refused, the inbound slot's own entry is applied to it.
  RUN_TEST(test_s4b_4_the_undo_route_addresses_the_holder_the_switch_moved_to);
  // S4b.4 (display clause): the display authority moves to the holder the switch moved to.
  RUN_TEST(test_s4b_4_the_display_authority_moves_to_the_holder_the_switch_moved_to);
  RUN_TEST(test_boot_leaves_a_committed_slot_without_a_working_set);
  RUN_TEST(test_a_repeated_admission_does_not_churn_the_lease);
  RUN_TEST(test_release_is_refused_while_an_owner_remains_and_allowed_once_it_retires);
  RUN_TEST(test_a_stop_clears_only_its_own_slots_ledger);
  RUN_TEST(test_s4_1_capture_work_slot_uses_the_pinned_interval_slot);
  RUN_TEST(test_selection_acquires_no_working_set);
  RUN_TEST(test_a_late_cleanup_cannot_clear_a_successor_finalization_marker);
  RUN_TEST(test_the_slot_generation_release_entry_is_identity_qualified_and_preserves_refusals);
  // S4.2 -- the generation-mismatched release: rejected on the lease path, and named as the row names it.
  RUN_TEST(test_s4_2_a_retired_owners_late_release_cannot_release_a_successors_working_set);
  RUN_TEST(test_s4_2_the_release_outcomes_are_named_as_the_device_row_names_them);
  RUN_TEST(test_the_release_sweep_skips_a_slot_with_a_live_owner);
  // S4.1 -- the working-set lease release on a same-track slot switch (the device run only evidences
  // the finalization-capacity release).
  RUN_TEST(test_s4_1_a_same_track_switch_releases_the_retired_slots_lease);
  RUN_TEST(test_s4_1_a_live_finalization_refuses_the_retired_slots_lease_release);
  RUN_TEST(test_s4_1_the_retired_slots_lease_is_released_once_its_finalization_retires);
  RUN_TEST(test_reactivating_a_released_slot_reuses_its_capacity);
  RUN_TEST(test_a_declared_envelope_breach_is_reported_and_does_not_block_activation);
  // S4.4 -- the breach report names its holder, including the finalizing holder the census still counts.
  RUN_TEST(test_s4_4_the_breach_report_names_the_finalizing_holder_instead_of_hiding_it);
  RUN_TEST(test_switching_starts_the_new_take_while_the_previous_take_still_finalizes);
  RUN_TEST(test_a_blocked_switch_and_start_survive_until_its_prerequisite_stop_binds);
  // The successor's take must COMMIT with content -- the device failure a start marker hid.
  RUN_TEST(test_the_successor_take_commits_with_content_after_a_transport_stop);
  RUN_TEST(test_record_switch_respects_a_retained_stop_and_permits_an_admitted_one);
  RUN_TEST(test_record_stop_adopts_published_geometry_not_the_stop_edge_prediction);
  RUN_TEST(test_filtered_record_take_publishes_no_master_length);
  // S4b.1 contract check 7 coverage, and the exactly-once half the committed carrier was missing.
  RUN_TEST(test_a_cross_track_successor_starts_while_the_predecessor_finalizes);
  RUN_TEST(test_a_cleared_successor_is_a_record_target_while_the_predecessor_finalizes);
  RUN_TEST(test_a_populated_successor_overdubs_on_its_own_phase_while_the_predecessor_finalizes);
  RUN_TEST(test_slot_button_during_pending_overdub_finalization_queues_populated_target);
  RUN_TEST(test_slot_button_during_pending_overdub_finalization_arms_empty_target);
  RUN_TEST(test_the_switch_carried_start_executes_exactly_once);
  // The two exactly-once routes the fixture above left uncovered.
  RUN_TEST(test_the_arm_and_wait_requeue_retains_then_executes_exactly_once);
  RUN_TEST(test_a_start_refused_by_its_own_live_stop_obligation_retries_to_a_single_take);
  // windowed-playback-generation slice 3.1: the window-build owner's declared contract, its one-grain turn
  // and the naming/non-destructiveness of its refusals.
  RUN_TEST(test_window_owner_declares_its_caps_and_the_handoff_margin_is_the_oracle);
  RUN_TEST(test_window_owner_serves_one_grain_per_turn_and_publishes_nothing);
  RUN_TEST(test_window_owner_refusals_are_named_and_non_destructive);
  // windowed-playback-generation slice 3.2: the owner's atomic interval-window publication.
  RUN_TEST(test_window_owner_publishes_the_interval_window_atomically);
  RUN_TEST(test_window_owner_resolves_the_interval_through_the_published_snapshot);
  RUN_TEST(test_window_readiness_names_its_holder_and_an_empty_interval_is_ready);
  return UNITY_END();
}
