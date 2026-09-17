//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>
#include <random>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/EditManager/EditApply.cpp"
#include "../../src/Loop/LoopPasses.cpp"
#include "../test_support/MemoryMonitorNativeDeps.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#define LOOP_PENDING_NOTE_CHANGE_PROVIDES_DISPLAY_APPLY
#include "../test_support/LoopCaptureTestDeps.cpp"
#include "../../src/Loop.cpp"
#include "../../src/Track/RuntimePlaybackView.cpp"
#include "../../src/Track/RuntimePlaybackOrderBuilder.cpp"
#include "../../src/Track/RuntimePlaybackBridge.cpp"
#include "../../src/Track/RuntimeCaptureCompanions.cpp"
#include "../../src/Loop/LoopPreparedCompanionHistory.cpp"
#include "../../src/Track/RuntimeNotePairLookup.cpp"
#include "../../src/Track/RuntimeCompanionOverlapWalk.cpp"
#include "../../src/Track/RuntimeCompanionSpanWalk.cpp"
#include "../../src/Track/RuntimeCompanionReconstruction.cpp"
#include "../../src/Track/RuntimeCompanionApply.cpp"
#include "../../src/Track/RuntimeCompanionBridgeMap.cpp"
#include "../../src/Track/RuntimeCompanionPasses.cpp"
#include "../../src/Track/RuntimeCanonicalPassInputs.cpp"
#include "../../src/Utils/PlaybackCursorAdvance.cpp"
#include "../../src/Track/RuntimePlaybackViewFill.cpp"
#include "../../src/Track/RuntimePlaybackViewBuild.cpp"
#include "../../src/Track/TrackPlaybackRuntime.cpp"
#include "../../src/StorageManager/PersistedLoopSnapshotRelease.cpp"
#include "../../src/PassReclaim.cpp"
#include "../../src/Track/TrackUndoRetirement.cpp"
#include "../../src/Track/TrackUndoPublication.cpp"
#include "../../src/Track/TrackPreparedStopCommit.cpp"
#include "../../src/Track/TrackPreparedUndo.cpp"
#include "Loop.h"
#include "TrackPlaybackRuntime.h"
#include "TrackUndoPublication.h"
#include "Utils/MidiServiceDrain.h"


#include "../../src/Loop/LoopPool.cpp"
#include "../../src/SlotStateMachine.cpp"
#include "../../src/MidiLedManager.cpp"
#include "../../src/CaptureAppendResult.cpp"
#include "../../src/Track.cpp"
#include "../../src/Track/TrackStateMachine.cpp"
#include "../../src/Track/TrackStateFacade.cpp"
#include "../../src/Track/TrackSlotPool.cpp"
#include "../../src/Track/TrackCaptureInput.cpp"
#include "../../src/Track/TrackPlaybackWindowBuild.cpp"
#include "../../src/Track/TrackLoopJamGeometry.cpp"
#include "../../src/Track/TrackOverdubLifecycle.cpp"
#include "../../src/Track/TrackOverdubWrap.cpp"
#include "../../src/Track/TrackDeferredMaintenance.cpp"
#include "../../src/Utils/CommittedMidiScratch.cpp"
// Record-arm admission now runs on this suite's route, so the derived-cache
// invalidation the admitted arm performs is production code here rather than an
// off-path stub (see off_path_deps.h).
#include "../../src/Track/TrackMidiEventRouting.cpp"
#include "../../src/TrackManager.cpp"

#include "../../src/MidiButtonActions.cpp"

#include "../../src/Utils/RuntimeTimingTelemetry.cpp"
#include "../../src/Utils/CommittedPlaybackLedgerCatchUp.cpp"


ClockManager::ClockManager() : pendingStart(false) {}
ClockManager clockManager;
EditManager::EditManager() {}
EditManager editManager;
LooperStateManager looperState;
uint32_t ClockManager::getCurrentTick() const { return currentTick; }
void ClockManager::assignCurrentTickSilently(uint32_t tick) { currentTick = tick; }
bool ClockManager::isTransportRunning() const { return true; }
bool ClockManager::shouldQuantizeRecordStart() const { return true; }

// Production lifecycle/input/undo implementations are linked above. The clock
// and physical outputs are native boundaries; off-path domain stubs fail closed.
void setUp() {
  trackManager.~TrackManager();
  LoopEventStore::resetPoolForTests();
  new (&trackManager) TrackManager();
}
void tearDown() {}

Track& preparePlayingTrack() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  Loop& loop = track.getActiveLoop();
  loop.loopLengthTicks = 768;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(0, 1, 48, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(48, 1, 48, 0)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(768, CommitReason::RecordStop));
  loop.commitPendingCapturePass();
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
  TEST_ASSERT_TRUE(loop.prewarmActiveOverdubTransferHorizon());
  TEST_ASSERT_TRUE(loop.prewarmSessionExitSnapshotHandoff());
  track.prewarmPlaybackForSlot(0, true);
  track.forceSetState(TRACK_PLAYING);
  return track;
}

struct TrackCaptureStopTestAccess {
  static auto& job(Track& track) { return track.captureStopJob(); }
  static size_t queueSize(const Track& track) { return track.captureStopJobs_->count; }
  static size_t queueCapacity() { return Track::CaptureStopJobs::kCapacity; }
  static void discardColdQueue(Track& track) { track.captureStopJobs_.reset(); }
  static auto& liveNotes(Track& track) { return track.pendingNotes; }
  static auto& runtime(Track& track) { return *track.playbackRuntime.slotIfAllocated(0); }
  static bool step(Track& track) { return track.processDeferredOverdubStopGrain(); }
  static void selectSlot(Track& track, uint8_t slot) { track.activeLoopIndex = slot; }
  static void advanceInterval(Track& track) { ++track.captureInterval_; }
  static uint64_t interval(const Track& track) { return track.captureInterval_; }
  static uint64_t jobInterval(Track& track) { return job(track).interval; }
  static size_t jobReceiptRing(Track& track) { return job(track).admission.sessionExitReceiptRing; }
  static const SessionExitSnapshotHandoff* receiptAt(Track& track, size_t offset) {
    return track.getActiveLoop().sessionExitReceiptAt(offset);
  }
  static size_t receiptCount(Track& track) {
    return track.getActiveLoop().sessionExitReceiptCount();
  }
  static uint8_t jobPhase(Track& track) {
    return static_cast<uint8_t>(job(track).admission.phase);
  }
  static auto generation(const Track& track) { return track.playbackGeneration; }
  static size_t liveReservations(Track& track) {
    auto* staging = runtime(track).liveCaptureStagingIfPrewarmed();
    return staging != nullptr ? staging->openReservationCount() : 0;
  }
  static void invalidate(Track& track) { track.invalidatePlaybackMergedMidiEvents(true); }
  static const void* headPtr(Track& track) { return track.captureStopJobHead(); }
  static const void* tailPtr(Track& track) { return track.captureStopJobTail(); }
  static const void* entryPtr(Track& track, size_t index) {
    return &track.captureStopJobs_->entries[index];
  }
  static size_t headIndex(const Track& track) { return track.captureStopJobs_->head; }
  static void setQueueCount(Track& track, size_t count) { track.captureStopJobs_->count = count; }
  static bool admit(Track& track, PendingStopDestination destination, uint32_t tick) {
    return track.beginDeferredOverdubStop(destination, tick);
  }
  static auto& rawRange(Track& track) { return track.captureStopJob().rawRange; }
  static bool ownsRawRange(Track& track) { return track.captureStopJob().ownsRawRange; }
  static TrackUndoPublication* undoPublication(Track& track) { return track.protectedUndo(); }
  static GlobalUndoStack& undoStack(Track& track) { return track.getGlobalUndoStack(); }
  static bool appendNoteOffAtPhase(Track& track, uint8_t channel, uint8_t note, uint32_t phase) {
    return track.appendCaptureNoteOffAtPhase(channel, note, phase);
  }
  static bool jobHasBeforePublished(Track& track) {
    return track.captureStopJob().hasBeforePublished;
  }
  static bool liveHasBeforePublished(const Track& track) {
    return track.hasOverdubSessionBeforePublished_;
  }
  static void setLiveBeforePublished(Track& track, bool value) {
    track.hasOverdubSessionBeforePublished_ = value;
  }
  static const PlaybackViewSourceIdentity& causalBase(Track& track) {
    return track.captureStopJob().causalBase;
  }
  static bool causalBaseRecorded(Track& track) {
    return track.captureStopJob().causalBaseRecorded;
  }
  static bool entryCausalBaseRecorded(Track& track, size_t index) {
    return track.captureStopJobs_->entries[index].causalBaseRecorded;
  }
  static const void* causalBaseSnapshotIdentity(Track& track) {
    return track.captureStopJob().causalBase.snapshotIdentity;
  }
  static void* pinnedCausalSnapshot(Track& track) {
    return const_cast<void*>(static_cast<const void*>(track.captureStopJob().causalBaseSnapshot.get()));
  }
  static uint8_t bridgeSelected(Track& track) {
    return runtime(track).viewSetIfPrewarmed()->bridgeLifecycle.selected;
  }
  static uint32_t playbackRevision(const Track& track) {
    return track.getActiveLoop().playbackRevision;
  }
  static void setPlaybackRevision(Track& track, uint32_t revision) {
    track.getActiveLoop().playbackRevision = revision;
  }
  static bool canonicalOrderComplete(Track& track) {
    return runtime(track).frozenCaptureCanonicalOrder.complete();
  }
  static bool frozenOrderComplete(Track& track) { return runtime(track).frozenCaptureOrder.complete(); }
  static bool wrapCommitPending(Track& track) { return track.overdubWrapCommitPending_; }
  static uint8_t jobDestination(Track& track) {
    return static_cast<uint8_t>(job(track).admission.destination);
  }
  static void requestStoppedStop(Track& track) { track.requestDeferredOverdubStopToStopped(); }
  static bool captureStopOwnerClear(Track& track) { return runtime(track).captureStopOwner == nullptr; }
  static bool canonicalHasFrozenBinding(Track& track) {
    return runtime(track).canonicalPassInputs.hasFrozenBinding();
  }
  static uint64_t frozenOrderStorageGeneration(Track& track) {
    return runtime(track).frozenCaptureOrderStorageGeneration;
  }
  static bool canonicalOutputAdmitted(Track& track) {
    return runtime(track).canonicalCaptureOutputAdmitted();
  }
  static size_t canonicalChunks(Track& track) {
    return runtime(track).canonicalCaptureChunks.availableChunks();
  }
  /// Emulate the predecessor's transfer spending the slot's canonical copy credits.
  static void drainCanonicalChunks(Track& track) {
    auto& reservation = runtime(track).canonicalCaptureChunks;
    (void)reservation.releaseGrain(reservation.availableChunks());
  }
  static bool restoreCanonicalChunks(Track& track, size_t chunks) {
    return runtime(track).canonicalCaptureChunks.reserveChunks(chunks);
  }
};

/// Read a job-owned sealed extent's rows in stored order (bounded by its exact
/// captured end, not a live producer extent).
SessionMidiEventVec sealedRangeRows(const LoopEventStore::CaptureRange& range) {
  SessionMidiEventVec rows;
  const uint64_t generation = range.generation();
  for (size_t chunkIndex = 0; chunkIndex < range.chunkCount(); ++chunkIndex) {
    for (uint16_t eventIndex = 0;; ++eventIndex) {
      MidiEvent event;
      bool atEnd = false;
      if (!range.read(generation, chunkIndex, eventIndex, event, atEnd)) break;
      if (atEnd) break;
      rows.push_back(event);
    }
  }
  return rows;
}

void inputNote(uint8_t pitch, uint32_t onTick, uint32_t offTick) {
  clockManager.assignCurrentTickSilently(onTick);
  midiHandler.handleMidiMessage(midi::NoteOn, 1, pitch, 100, SOURCE_USB);
  if (offTick != UINT32_MAX) {
    clockManager.assignCurrentTickSilently(offTick);
    midiHandler.handleMidiMessage(midi::NoteOff, 1, pitch, 0, SOURCE_USB);
  }
}

void test_first_interval_uses_production_routes_and_freezes_at_punch_out() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.isOverdubbing());
  inputNote(60, 97, 120);
  const auto& capture = track.getActiveLoop().capture.store;
  TEST_ASSERT_EQUAL(2, capture.size());
  TEST_ASSERT_EQUAL(97, capture.at(0).tick);
  TEST_ASSERT_EQUAL(120, capture.at(1).tick);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_TRUE(track.isPlaying());
}

/// Button A successor reentry under the directional capacity. Starting a successor
/// while the predecessor finalizes is admitted (live role free). Stopping that
/// successor is denied while the predecessor owns the sole frozen/deferred role, and
/// the denial must be non-destructive: the successor stays the live recording
/// interval with no stop descriptor, and its rows, notes, reservations and preview
/// are untouched. Once the predecessor retires, the same interval's subsequent stop
/// is admitted and only then may a further interval start.
///
/// The N-job deferred queueing scenario these guards previously shaded (two frozen
/// jobs plus a live third interval) is a documented follow-up capacity case; see
/// openspec/changes/successor-capture-reentry-unguarding/design.md (D2).

/// A successor-stop admission failure because the deferred capacity is occupied
/// leaves the live interval completely intact and still recording. Once capacity
/// becomes available, a subsequent stop request transfers that same interval
/// exactly once, and the finalized output equals the accepted input exactly.
void test_successor_stop_denied_by_occupied_deferred_capacity_is_non_destructive() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  // A deferred (owns the frozen role).
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  // B live (Track-level admission) with exact input.
  clockManager.assignCurrentTickSilently(192);
  track.startOverdubbing(192);
  TEST_ASSERT_TRUE(track.isOverdubbing());
  inputNote(62, 193, 216);
  const size_t rowsBefore = loop.capture.store.size();
  const size_t notesBefore = TrackCaptureStopTestAccess::liveNotes(track).size();
  const size_t queueBefore = TrackCaptureStopTestAccess::queueSize(track);
  TEST_ASSERT_EQUAL(2, rowsBefore);
  // Denied stop: no descriptor, no consumed rows/notes, still recording.
  clockManager.assignCurrentTickSilently(240);
  track.stopOverdubbing();
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(),
      "an occupied deferred role must deny the stop without ending the live interval");
  TEST_ASSERT_EQUAL(queueBefore, TrackCaptureStopTestAccess::queueSize(track));
  TEST_ASSERT_EQUAL(rowsBefore, loop.capture.store.size());
  TEST_ASSERT_EQUAL(notesBefore, TrackCaptureStopTestAccess::liveNotes(track).size());
  TEST_ASSERT_EQUAL(193, loop.capture.store.at(0).tick);
  TEST_ASSERT_EQUAL(216, loop.capture.store.at(1).tick);
  // A retires. Its cleanup is job-local: B stays live and intact.
  for (unsigned i = 0; i < 10000 && TrackCaptureStopTestAccess::queueSize(track) > 0; ++i)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL(0, TrackCaptureStopTestAccess::queueSize(track));
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(),
      "the predecessor's completion must not end the live successor");
  TEST_ASSERT_EQUAL(rowsBefore, loop.capture.store.size());
  TEST_ASSERT_EQUAL(notesBefore, TrackCaptureStopTestAccess::liveNotes(track).size());
  // The same interval's subsequent stop is admitted and transfers exactly once.
  inputNote(63, 241, 264);
  clockManager.assignCurrentTickSilently(288);
  track.stopOverdubbing();
  TEST_ASSERT_FALSE(track.isOverdubbing());
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_EQUAL(1, TrackCaptureStopTestAccess::queueSize(track));
  const SessionMidiEventVec finalized = sealedRangeRows(TrackCaptureStopTestAccess::rawRange(track));
  TEST_ASSERT_EQUAL(4, finalized.size());
  TEST_ASSERT_EQUAL(193, finalized[0].tick);
  TEST_ASSERT_EQUAL(216, finalized[1].tick);
  TEST_ASSERT_EQUAL(241, finalized[2].tick);
  TEST_ASSERT_EQUAL(264, finalized[3].tick);
}

void test_button_a_successor_stop_denied_until_deferred_role_available() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  // A: record at 96, stop at 144 -> A owns the frozen/deferred role.
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.isOverdubbing());
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_EQUAL(1, TrackCaptureStopTestAccess::queueSize(track));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::ownsRawRange(track));
  const SessionMidiEventVec aRows = sealedRangeRows(TrackCaptureStopTestAccess::rawRange(track));
  TEST_ASSERT_EQUAL(2, aRows.size());
  // B: start at 192 while A pends -> admitted into the live role, exact input.
  clockManager.assignCurrentTickSilently(192);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(),
      "B must start while A's deferred finalization is pending");
  inputNote(62, 193, 216);
  const size_t bRowsBeforeStop = loop.capture.store.size();
  const size_t bNotesBeforeStop = TrackCaptureStopTestAccess::liveNotes(track).size();
  TEST_ASSERT_EQUAL(2, bRowsBeforeStop);
  TEST_ASSERT_EQUAL(193, loop.capture.store.at(0).tick);
  TEST_ASSERT_EQUAL(216, loop.capture.store.at(1).tick);
  // B stop at 240 while A owns the frozen role: explicit, non-destructive denial.
  clockManager.assignCurrentTickSilently(240);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(),
      "a denied successor stop must leave the live interval recording");
  TEST_ASSERT_EQUAL(1, TrackCaptureStopTestAccess::queueSize(track));
  TEST_ASSERT_EQUAL(bRowsBeforeStop, loop.capture.store.size());
  TEST_ASSERT_EQUAL(bNotesBeforeStop, TrackCaptureStopTestAccess::liveNotes(track).size());
  TEST_ASSERT_EQUAL(193, loop.capture.store.at(0).tick);
  // A retires -> the frozen role frees. A completes while B is live, so its cleanup
  // must be job-local: B's state, rows and notes stay its own.
  for (unsigned i = 0; i < 10000 && TrackCaptureStopTestAccess::queueSize(track) > 0; ++i)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL(0, TrackCaptureStopTestAccess::queueSize(track));
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(),
      "A's completion must not end B's live interval");
  TEST_ASSERT_EQUAL(bRowsBeforeStop, loop.capture.store.size());
  TEST_ASSERT_EQUAL(bNotesBeforeStop, TrackCaptureStopTestAccess::liveNotes(track).size());
  // B's subsequent stop request is admitted: the same interval transfers once.
  inputNote(63, 241, 264);
  clockManager.assignCurrentTickSilently(288);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_FALSE(track.isOverdubbing());
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_EQUAL(1, TrackCaptureStopTestAccess::queueSize(track));
  // The admitted transfer moved B's accepted interval into the job's sealed range, so the
  // live store is empty and the finalized output is read from the job - asserting the
  // finalized B output equals the accepted B input, exactly once.
  TEST_ASSERT_EQUAL(0, loop.capture.store.size());
  const SessionMidiEventVec bSealed = sealedRangeRows(TrackCaptureStopTestAccess::rawRange(track));
  TEST_ASSERT_EQUAL(4, bSealed.size());
  TEST_ASSERT_EQUAL(193, bSealed.at(0).tick);
  TEST_ASSERT_EQUAL(216, bSealed.at(1).tick);
  TEST_ASSERT_EQUAL(241, bSealed.at(2).tick);
  TEST_ASSERT_EQUAL(264, bSealed.at(3).tick);
  // C start is legal now that the live role is free.
  clockManager.assignCurrentTickSilently(336);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(),
      "C must start once B has transferred into the deferred role");
  inputNote(64, 337, 360);
  TEST_ASSERT_EQUAL(2, loop.capture.store.size());
  TEST_ASSERT_EQUAL(337, loop.capture.store.at(0).tick);
}


// Record admission is atomic: a failed *required* prewarm refuses the take instead of
// being ignored and letting RECORDING start with an unestablished stop prerequisite.
// The fixture prewarms slot 0, so this uses a fresh slot where the companion-history
// horizon genuinely has to be reserved.
void test_record_admission_denied_when_required_prewarm_fails() {
  Track& track = preparePlayingTrack();
  TrackCaptureStopTestAccess::selectSlot(track, 1);
  Loop& loop = track.getActiveLoop();
  const size_t stopQueueBefore = TrackCaptureStopTestAccess::queueSize(track);

  MemoryMonitor::setNativeForcedReserveDenied(true);
  track.forceSetState(TRACK_ARMED);
  track.startRecording(0);
  MemoryMonitor::setNativeForcedReserveDenied(false);

  // Denied: no RECORDING transition, no capture interval admitted, no pending stop
  // ownership, no pre-roll admitted, and the armed state is what it was.
  TEST_ASSERT_EQUAL(static_cast<int>(TRACK_ARMED), static_cast<int>(track.getState()));
  TEST_ASSERT_FALSE(loop.captureActive());
  TEST_ASSERT_EQUAL(stopQueueBefore, TrackCaptureStopTestAccess::queueSize(track));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::liveNotes(track).empty());
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(loop.passes.overdubPasses.size()));

  // Control: with the reserve available the same call is admitted, so the assertions
  // above are about admission and not about the state machine refusing everything.
  track.forceSetState(TRACK_ARMED);
  track.startRecording(0);
  TEST_ASSERT_EQUAL(static_cast<int>(TRACK_RECORDING), static_cast<int>(track.getState()));
  TEST_ASSERT_TRUE(loop.captureActive());
}

void test_manager_reentry_with_a_held_same_pitch_note() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, UINT32_MAX);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  clockManager.assignCurrentTickSilently(192);
  trackManager.startOverdubbingTrack(trackManager.getSelectedTrackIndex());
  inputNote(60, 193, 216);
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(),
      "manager must admit B without consuming A's same-pitch closure");
  const auto& capture = track.getActiveLoop().capture.store;
  TEST_ASSERT_EQUAL(2, capture.size());
  TEST_ASSERT_EQUAL(193, capture.at(0).tick);
  TEST_ASSERT_EQUAL(216, capture.at(1).tick);
}

void test_track_reentry_does_not_depend_on_manager_guard() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  clockManager.assignCurrentTickSilently(192);
  track.startOverdubbing(192);
  inputNote(62, 193, 216);
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(),
      "Track must own a distinct live interval before removing outer guards");
  // Completion-during-capture (task 5.2): let A's deferred job retire while B is live and
  // assert B's interval is untouched - D6 makes A's cleanup job-local because A's interval
  // identity no longer matches the live one.
  const uint8_t stateBeforeCompletion = track.getState();
  const size_t rowsBeforeCompletion = track.getActiveLoop().capture.store.size();
  for (unsigned turn = 0; turn < 10000 && TrackCaptureStopTestAccess::queueSize(track) > 0; ++turn)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(TrackCaptureStopTestAccess::queueSize(track)));
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(),
      "A's completion must not transition B's live interval");
  TEST_ASSERT_EQUAL(stateBeforeCompletion, track.getState());
  TEST_ASSERT_EQUAL(rowsBeforeCompletion, track.getActiveLoop().capture.store.size());
}

/// DEC-068 (1)(5): a session-exit receipt belongs to the interval that captured it. A's
/// receipt is taken at A's stop admission (not ready), carries A's committed pass id once A
/// retires, and B's receipt is a distinct one for a distinct interval.
void test_session_exit_receipts_are_per_interval() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  // A's tail is captured at admission, before any successor exists, and is not consumable yet.
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(TrackCaptureStopTestAccess::receiptCount(track)));
  const size_t aRing = TrackCaptureStopTestAccess::jobReceiptRing(track);
  TEST_ASSERT_NOT_EQUAL(Loop::kInvalidSessionExitReceipt, aRing);
  const SessionExitSnapshotHandoff* aReceipt = TrackCaptureStopTestAccess::receiptAt(track, 0);
  TEST_ASSERT_NOT_NULL(aReceipt);
  TEST_ASSERT_TRUE(aReceipt->pending);
  TEST_ASSERT_FALSE(aReceipt->ready);
  TEST_ASSERT_EQUAL(kInvalidPassId, aReceipt->lastPassId);
  // B starts while A pends and gets its own session.
  clockManager.assignCurrentTickSilently(192);
  track.startOverdubbing(192);
  TEST_ASSERT_TRUE(track.isOverdubbing());
  TEST_ASSERT_TRUE(loop.hasOverdubSession());
  inputNote(62, 193, 216);
  // A retires: its receipt becomes ready with A's pass id, and B's session survives.
  for (unsigned turn = 0; turn < 10000 && TrackCaptureStopTestAccess::queueSize(track) > 0; ++turn)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  const PassId aPassId = loop.passes.overdubPasses.back().id;
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(TrackCaptureStopTestAccess::receiptCount(track)));
  aReceipt = TrackCaptureStopTestAccess::receiptAt(track, 0);
  TEST_ASSERT_NOT_NULL(aReceipt);
  TEST_ASSERT_TRUE(aReceipt->ready);
  TEST_ASSERT_EQUAL(aPassId, aReceipt->lastPassId);
  // The publisher consumes it once; B's own receipt is a different slot and a different pass.
  for (unsigned grain = 0; grain < 10000 && loop.hasPendingSnapshotPublish(); ++grain)
    loop.stepPendingSnapshotPublish(1);
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(TrackCaptureStopTestAccess::receiptCount(track)));
  const size_t passesBeforeB = loop.passes.overdubPasses.size();
  clockManager.assignCurrentTickSilently(288);
  track.stopOverdubbing();
  TEST_ASSERT_FALSE(track.isOverdubbing());
  const size_t bRing = TrackCaptureStopTestAccess::jobReceiptRing(track);
  TEST_ASSERT_NOT_EQUAL(Loop::kInvalidSessionExitReceipt, bRing);
  TEST_ASSERT_NOT_EQUAL(aRing, bRing);
  for (unsigned turn = 0; turn < 20000 && TrackCaptureStopTestAccess::queueSize(track) > 0; ++turn)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(TrackCaptureStopTestAccess::queueSize(track)));
  TEST_ASSERT_EQUAL_UINT32(passesBeforeB + 1,
                           static_cast<uint32_t>(loop.passes.overdubPasses.size()));
  const PassId bPassId = loop.passes.overdubPasses.back().id;
  TEST_ASSERT_TRUE(bPassId != aPassId);
  const SessionExitSnapshotHandoff* bReceipt = TrackCaptureStopTestAccess::receiptAt(track, 0);
  TEST_ASSERT_NOT_NULL(bReceipt);
  TEST_ASSERT_TRUE(bReceipt->ready);
  TEST_ASSERT_EQUAL(bPassId, bReceipt->lastPassId);
}

/// DEC-068 (2)(3): A's cleanup retires A's descriptor while B stays live, and it must not close
/// the successor's session. B's own session is still the live one after A is gone.
void test_predecessor_cleanup_keeps_the_successor_session() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  clockManager.assignCurrentTickSilently(192);
  track.startOverdubbing(192);
  inputNote(62, 193, 240);
  const size_t rowsBefore = loop.capture.store.size();
  const size_t notesBefore = TrackCaptureStopTestAccess::liveNotes(track).size();
  for (unsigned turn = 0; turn < 10000 && TrackCaptureStopTestAccess::queueSize(track) > 0; ++turn)
    (void)TrackCaptureStopTestAccess::step(track);
  // A retired exactly once...
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(TrackCaptureStopTestAccess::queueSize(track)));
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  // ...without ending B's session or touching B's accepted state.
  TEST_ASSERT_TRUE_MESSAGE(loop.hasOverdubSession(),
                           "A's cleanup closed the session of the live successor");
  TEST_ASSERT_TRUE(track.isOverdubbing());
  TEST_ASSERT_EQUAL(rowsBefore, loop.capture.store.size());
  TEST_ASSERT_EQUAL(notesBefore, TrackCaptureStopTestAccess::liveNotes(track).size());
}

/// DEC-068 (4): after B stops, B captures its own receipt, publishes it, and its descriptor
/// retires — the successor's session ends with the successor.
void test_successor_session_ends_with_its_own_retirement() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  clockManager.assignCurrentTickSilently(192);
  track.startOverdubbing(192);
  inputNote(62, 193, 216);
  for (unsigned turn = 0; turn < 10000 && TrackCaptureStopTestAccess::queueSize(track) > 0; ++turn)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_TRUE(loop.hasOverdubSession());
  // The publisher drains A's receipt on idle turns (the harness drives them explicitly).
  for (unsigned grain = 0; grain < 10000 && loop.hasPendingSnapshotPublish(); ++grain)
    loop.stepPendingSnapshotPublish(1);
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(TrackCaptureStopTestAccess::receiptCount(track)));
  clockManager.assignCurrentTickSilently(288);
  track.stopOverdubbing();
  TEST_ASSERT_FALSE(track.isOverdubbing());
  for (unsigned turn = 0; turn < 20000 && TrackCaptureStopTestAccess::queueSize(track) > 0; ++turn)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(TrackCaptureStopTestAccess::queueSize(track)));
  // B owns the last interval, so its retirement ends the session, and its receipt is queued for
  // the publisher rather than dropped.
  TEST_ASSERT_FALSE(loop.hasOverdubSession());
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(TrackCaptureStopTestAccess::receiptCount(track)));
  const SessionExitSnapshotHandoff* receipt = TrackCaptureStopTestAccess::receiptAt(track, 0);
  TEST_ASSERT_NOT_NULL(receipt);
  TEST_ASSERT_TRUE(receipt->ready);
  TEST_ASSERT_EQUAL(loop.passes.overdubPasses.back().id, receipt->lastPassId);
}

/// DEC-068 (6): the same rule on the superseded route — A retiring through
/// `commitSupersededCausalBase` releases only its own receipt and cannot close B's session.
void test_superseded_predecessor_releases_only_its_own_receipt() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  loop.storePublishedSnapshotForTest(
      LoopSnapshot::createEmpty(loop.loopId, loop.loopLengthTicks, loop.loopStartTick,
                                loop.nextNoteId_, 1),
      loop.playbackRevision);
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, UINT32_MAX);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  // The recorded causal base stops being the live root, so A retires superseded.
  TrackCaptureStopTestAccess::setPlaybackRevision(track, TrackCaptureStopTestAccess::playbackRevision(track) + 7);
  clockManager.assignCurrentTickSilently(192);
  track.startOverdubbing(192);
  inputNote(62, 193, 216);
  const size_t passesBefore = loop.passes.overdubPasses.size();
  for (unsigned turn = 0; turn < 20000 && TrackCaptureStopTestAccess::queueSize(track) > 0; ++turn)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(TrackCaptureStopTestAccess::queueSize(track)));
  TEST_ASSERT_EQUAL_UINT8(RuntimePlaybackBridgeLifecycle::kNoBridge,
                          TrackCaptureStopTestAccess::bridgeSelected(track));
  TEST_ASSERT_EQUAL_UINT32(passesBefore + 1,
                           static_cast<uint32_t>(loop.passes.overdubPasses.size()));
  // A released its own receipt (consumed or still pending, but always with A's pass id)...
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(TrackCaptureStopTestAccess::receiptCount(track)));
  const SessionExitSnapshotHandoff* receipt = TrackCaptureStopTestAccess::receiptAt(track, 0);
  TEST_ASSERT_NOT_NULL(receipt);
  TEST_ASSERT_TRUE(receipt->ready);
  TEST_ASSERT_EQUAL(loop.passes.overdubPasses.back().id, receipt->lastPassId);
  // ...and B's session is untouched by A's superseded retirement.
  TEST_ASSERT_TRUE(loop.hasOverdubSession());
  TEST_ASSERT_TRUE(track.isOverdubbing());
}

/// Task 5.1(d): after A retires, B's own stop is admitted and that same interval transfers
/// exactly once — one job descriptor for B's interval identity, B's accepted rows moved out of
/// the live store in order, and exactly one committed pass whose content is those rows.
///
/// Scope note (open finding, CURRENT_WORK): the assertions stop at the committed pass, because
/// a retiring predecessor's cleanup closes the *live successor's* overdub session
/// (`Loop::closeOverdubSession`), so B's own `DeferredCleanup` can never hand off its session
/// exit and B's descriptor leaks. That is a session-ownership defect under reentry, documented
/// separately; the transfer and the committed content below are independent of it.
void test_successor_same_interval_transfers_exactly_once() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  // A: deferred stop owning the finalization resources.
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  // B: live interval with distinctive input while A pends.
  clockManager.assignCurrentTickSilently(192);
  track.startOverdubbing(192);
  TEST_ASSERT_TRUE(track.isOverdubbing());
  inputNote(62, 193, 216);
  inputNote(64, 217, 240);
  const uint64_t bInterval = TrackCaptureStopTestAccess::interval(track);
  TEST_ASSERT_EQUAL_UINT64(2u, bInterval);
  MidiEventVec bAccepted;
  for (size_t row = 0; row < loop.capture.store.size(); ++row) {
    bAccepted.push_back(loop.capture.store.at(row));
  }
  TEST_ASSERT_EQUAL_UINT32(4, static_cast<uint32_t>(bAccepted.size()));
  // B's stop while A owns those resources: refused, nothing consumed, same interval.
  clockManager.assignCurrentTickSilently(240);
  track.stopOverdubbing();
  TEST_ASSERT_TRUE(track.isOverdubbing());
  TEST_ASSERT_EQUAL_UINT64(bInterval, TrackCaptureStopTestAccess::interval(track));
  TEST_ASSERT_EQUAL_UINT32(4, static_cast<uint32_t>(loop.capture.store.size()));
  // A retires.
  for (unsigned turn = 0; turn < 10000 && TrackCaptureStopTestAccess::queueSize(track) > 0; ++turn)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  const size_t passesBeforeB = loop.passes.overdubPasses.size();
  // The same interval's stop is admitted, once, and it belongs to B's interval identity.
  clockManager.assignCurrentTickSilently(288);
  track.stopOverdubbing();
  TEST_ASSERT_FALSE(track.isOverdubbing());
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(TrackCaptureStopTestAccess::queueSize(track)));
  TEST_ASSERT_EQUAL_UINT64(bInterval, TrackCaptureStopTestAccess::jobInterval(track));
  // The transfer moved B's accepted rows out of the live store, in order.
  const SessionMidiEventVec sealed = sealedRangeRows(TrackCaptureStopTestAccess::rawRange(track));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(bAccepted.size()),
                           static_cast<uint32_t>(loop.capture.store.size() + sealed.size()));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(bAccepted.size()),
                           static_cast<uint32_t>(sealed.size()));
  for (size_t row = 0; row < bAccepted.size() && row < sealed.size(); ++row) {
    TEST_ASSERT_EQUAL(bAccepted[row].tick, sealed[row].tick);
    TEST_ASSERT_EQUAL(bAccepted[row].data.noteData.note, sealed[row].data.noteData.note);
  }
  // Exactly one committed pass, whose content is the accepted B input, once.
  for (unsigned turn = 0;
       turn < 20000 && loop.passes.overdubPasses.size() == passesBeforeB; ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_EQUAL_UINT32(passesBeforeB + 1,
                           static_cast<uint32_t>(loop.passes.overdubPasses.size()));
  SessionMidiEventVec committed;
  LoopEventStore::appendChunkRefEvents(loop.passes.overdubPasses.back().committedChunkIds, committed);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(bAccepted.size()),
                           static_cast<uint32_t>(committed.size()));
  for (size_t row = 0; row < bAccepted.size(); ++row) {
    TEST_ASSERT_EQUAL(bAccepted[row].tick, committed[row].tick);
    TEST_ASSERT_EQUAL(bAccepted[row].data.noteData.note, committed[row].data.noteData.note);
  }
  // ...and that interval's job retires, releasing its own receipt (DEC-068).
  for (unsigned turn = 0; turn < 20000 && TrackCaptureStopTestAccess::queueSize(track) > 0; ++turn)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(TrackCaptureStopTestAccess::queueSize(track)));
  TEST_ASSERT_FALSE(loop.hasOverdubSession());
  // A's receipt may still be queued ahead of it (FIFO); this interval's own is the newest.
  const size_t receiptCount = TrackCaptureStopTestAccess::receiptCount(track);
  TEST_ASSERT_TRUE(receiptCount >= 1);
  const SessionExitSnapshotHandoff* receipt =
      TrackCaptureStopTestAccess::receiptAt(track, receiptCount - 1);
  TEST_ASSERT_NOT_NULL(receipt);
  TEST_ASSERT_TRUE(receipt->ready);
  TEST_ASSERT_EQUAL(loop.passes.overdubPasses.back().id, receipt->lastPassId);
}

/// Task 5.1(e): a start request while B is still live is not admitted as a second live
/// interval — same interval identity, no staging reset, accepted state untouched — and a start
/// becomes admissible again after B's interval transfers (the live role is free from B's
/// admission; the transfer releases nothing the live role needs).
void test_start_request_while_successor_is_live_is_not_a_second_interval() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  clockManager.assignCurrentTickSilently(192);
  track.startOverdubbing(192);
  inputNote(62, 193, 216);
  const uint64_t bInterval = TrackCaptureStopTestAccess::interval(track);
  const size_t rowsBefore = loop.capture.store.size();
  const size_t notesBefore = TrackCaptureStopTestAccess::liveNotes(track).size();
  const size_t reservationsBefore = TrackCaptureStopTestAccess::liveReservations(track);
  const uint32_t stagingGenerationBefore =
      TrackCaptureStopTestAccess::runtime(track).liveCaptureStagingIfPrewarmed()->captureGeneration;
  // The request is refused as a *new* interval: same identity, no reset, nothing consumed.
  clockManager.assignCurrentTickSilently(220);
  track.startOverdubbing(220);
  TEST_ASSERT_TRUE(track.isOverdubbing());
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(bInterval, TrackCaptureStopTestAccess::interval(track),
                                   "a start while live must not create a second interval");
  TEST_ASSERT_EQUAL_UINT32(stagingGenerationBefore,
                           TrackCaptureStopTestAccess::runtime(track)
                               .liveCaptureStagingIfPrewarmed()->captureGeneration);
  TEST_ASSERT_EQUAL(rowsBefore, loop.capture.store.size());
  TEST_ASSERT_EQUAL(notesBefore, TrackCaptureStopTestAccess::liveNotes(track).size());
  TEST_ASSERT_EQUAL(reservationsBefore, TrackCaptureStopTestAccess::liveReservations(track));
  // Input after the request continues the same interval.
  inputNote(65, 221, 240);
  TEST_ASSERT_EQUAL(rowsBefore + 2, loop.capture.store.size());
  // A retires, then B's own stop is admitted once (its interval identity is preserved).
  for (unsigned turn = 0; turn < 10000 && TrackCaptureStopTestAccess::queueSize(track) > 0; ++turn)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  clockManager.assignCurrentTickSilently(288);
  track.stopOverdubbing();
  TEST_ASSERT_FALSE(track.isOverdubbing());
  TEST_ASSERT_EQUAL_UINT64(bInterval, TrackCaptureStopTestAccess::jobInterval(track));
  // A start is admissible again — the live role was freed when B froze into its job — as a
  // new interval with a clean live store. Its own stop is denied while B's job holds the
  // finalization resources, which is the same capacity rule one level down.
  clockManager.assignCurrentTickSilently(336);
  track.startOverdubbing(336);
  TEST_ASSERT_TRUE(track.isOverdubbing());
  TEST_ASSERT_EQUAL_UINT64(bInterval + 1, TrackCaptureStopTestAccess::interval(track));
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(loop.capture.store.size()));
  clockManager.assignCurrentTickSilently(400);
  track.stopOverdubbing();
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(),
                           "a successor stop must wait for the previous job's resources");
}

/// DEC-067: START is admitted on the live role alone. The device refused successor starts
/// with "canonical copy chunks are not reserved" while the predecessor finalized
/// (`successor-reentry-19` / `-20`); those resources are first used at the successor's own
/// stop edge, so they must not gate its start.
void test_successor_start_is_admitted_while_finalization_resources_are_busy() {
  Track& track = preparePlayingTrack();
  const size_t chunksBefore = TrackCaptureStopTestAccess::canonicalChunks(track);
  // A: deferred stop admitted while the resources were still available.
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  // A now owns them: its copy target is in use and its transfer credits are spent.
  TrackCaptureStopTestAccess::drainCanonicalChunks(track);
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::canonicalOutputAdmitted(track));
  // B: START must not wait for them.
  clockManager.assignCurrentTickSilently(192);
  track.startOverdubbing(192);
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(),
      "a successor start must not require the predecessor's finalization resources");
  inputNote(62, 193, 216);
  TEST_ASSERT_TRUE(track.getActiveLoop().capture.store.size() > 0);
  // Let A retire with its credits restored, so the next case starts from the prewarmed state.
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::restoreCanonicalChunks(track, chunksBefore));
  for (unsigned grain = 0; grain < 20000 && track.hasPendingDeferredOverdubStop(); ++grain)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
}

/// DEC-067: the frozen order/rewrite builders are per slot, not per interval. The start path
/// used to discard them unconditionally, which would clobber a pending predecessor mid-order;
/// it now only clears them when no job owns them.
void test_successor_start_keeps_the_predecessors_frozen_builders() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, UINT32_MAX);  // held: the interval carries an open reservation and closure work
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  for (unsigned grain = 0; grain < 64 && !TrackCaptureStopTestAccess::frozenOrderComplete(track); ++grain)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::frozenOrderComplete(track),
      "the predecessor must reach its frozen order before the successor starts");
  const uint64_t generationBefore = TrackCaptureStopTestAccess::frozenOrderStorageGeneration(track);
  clockManager.assignCurrentTickSilently(192);
  track.startOverdubbing(192);
  TEST_ASSERT_TRUE(track.isOverdubbing());
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::frozenOrderComplete(track),
      "the successor start must not discard the pending predecessor's frozen order");
  TEST_ASSERT_EQUAL_UINT64(generationBefore,
                           TrackCaptureStopTestAccess::frozenOrderStorageGeneration(track));
  const size_t passesBefore = track.getActiveLoop().passes.overdubPasses.size();
  for (unsigned grain = 0; grain < 20000 && track.hasPendingDeferredOverdubStop(); ++grain)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_EQUAL_UINT32(passesBefore + 1, track.getActiveLoop().passes.overdubPasses.size());
}

/// DEC-067: the stop admission is the single owner of the finalization-resource questions.
/// With no stop owner yet, an unavailable canonical copy target must refuse the stop
/// atomically: still recording, no descriptor, accepted rows intact.
void test_successor_stop_is_denied_when_the_canonical_copy_target_is_busy() {
  Track& track = preparePlayingTrack();
  const size_t chunksBefore = TrackCaptureStopTestAccess::canonicalChunks(track);
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  TEST_ASSERT_TRUE(track.isOverdubbing());
  TrackCaptureStopTestAccess::drainCanonicalChunks(track);
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::canonicalOutputAdmitted(track));
  const uint8_t stateBefore = track.getState();
  const size_t rowsBefore = track.getActiveLoop().capture.store.size();
  const size_t descriptorsBefore = TrackCaptureStopTestAccess::queueSize(track);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(),
      "a stop without the canonical copy target must stay in the live interval");
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(descriptorsBefore),
                           static_cast<uint32_t>(TrackCaptureStopTestAccess::queueSize(track)));
  TEST_ASSERT_EQUAL_UINT8(stateBefore, track.getState());
  TEST_ASSERT_EQUAL(rowsBefore, track.getActiveLoop().capture.store.size());
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::restoreCanonicalChunks(track, chunksBefore));
}

/// Task 5.2 isolation: A's deferred completion during B's capture leaves B's rows, pending
/// notes, reservations, preview and track state untouched, and forces no transition on B's
/// behalf. This is the assertion breadth task 2.3 was waiting on.
void test_deferred_predecessor_completion_leaves_successor_interval_untouched() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  // A: deferred stop, owning the frozen role.
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  // B: successor interval with a distinctive held note, so a reservation and a pending
  // note exist to be trampled if A's cleanup were not job-local.
  clockManager.assignCurrentTickSilently(192);
  track.startOverdubbing(192);
  inputNote(64, 193, UINT32_MAX);
  TEST_ASSERT_TRUE(track.isOverdubbing());
  const uint8_t stateBefore = track.getState();
  const size_t rowsBefore = loop.capture.store.size();
  const size_t notesBefore = TrackCaptureStopTestAccess::liveNotes(track).size();
  const size_t previewBefore = loop.capturePreview.notes.size();
  const size_t reservationsBefore = TrackCaptureStopTestAccess::liveReservations(track);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(rowsBefore));
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(notesBefore));
  TEST_ASSERT_TRUE(reservationsBefore > 0);
  // A retires while B is live.
  for (unsigned turn = 0; turn < 10000 && TrackCaptureStopTestAccess::queueSize(track) > 0; ++turn)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(TrackCaptureStopTestAccess::queueSize(track)));
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(),
      "A's deferred completion must not force a transition on B");
  TEST_ASSERT_EQUAL(stateBefore, track.getState());
  TEST_ASSERT_EQUAL(rowsBefore, loop.capture.store.size());
  TEST_ASSERT_EQUAL(193, loop.capture.store.at(0).tick);
  TEST_ASSERT_EQUAL(notesBefore, TrackCaptureStopTestAccess::liveNotes(track).size());
  TEST_ASSERT_EQUAL(previewBefore, loop.capturePreview.notes.size());
  TEST_ASSERT_EQUAL(reservationsBefore, TrackCaptureStopTestAccess::liveReservations(track));
}

/// The deferred stop records the published source it succeeded, and pins that
/// source's root, at admission. A later grain must never re-resolve it against a
/// source a successor published in the meantime.
/// The deferred stop records the published source it succeeded and pins that
/// source's root at admission. A later grain must never re-resolve it against a
/// source published in the meantime: once the recorded predecessor is no longer
/// the live root, the interval retires without a successor window, keeping its
/// accepted capture pass and publishing no audible bridge.
void test_deferred_stop_records_and_pins_its_causal_predecessor_source() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  loop.storePublishedSnapshotForTest(
      LoopSnapshot::createEmpty(loop.loopId, loop.loopLengthTicks, loop.loopStartTick,
                                loop.nextNoteId_, 1),
      loop.playbackRevision);
  const uint32_t admittedRevision = loop.playbackRevision;
  const void* admittedSnapshot = loop.publishedSnapshotIdentity();
  TEST_ASSERT_NOT_NULL(admittedSnapshot);
  const size_t admittedHead = TrackCaptureStopTestAccess::headIndex(track);
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, UINT32_MAX);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::causalBaseRecorded(track));
  const PlaybackViewSourceIdentity recorded = TrackCaptureStopTestAccess::causalBase(track);
  TEST_ASSERT_EQUAL_UINT32(admittedRevision, recorded.playbackRevision);
  TEST_ASSERT_EQUAL_UINT32(TrackCaptureStopTestAccess::generation(track), recorded.sourceGeneration);
  TEST_ASSERT_EQUAL_PTR(admittedSnapshot, recorded.snapshotIdentity);
  TEST_ASSERT_EQUAL_PTR(recorded.snapshotIdentity, TrackCaptureStopTestAccess::pinnedCausalSnapshot(track));
  // A later publication must not rewrite where this interval came from.
  TrackCaptureStopTestAccess::setPlaybackRevision(track, admittedRevision + 7);
  TEST_ASSERT_EQUAL_UINT32(admittedRevision, TrackCaptureStopTestAccess::causalBase(track).playbackRevision);
  TEST_ASSERT_EQUAL_PTR(admittedSnapshot, TrackCaptureStopTestAccess::pinnedCausalSnapshot(track));
  TEST_ASSERT_EQUAL_UINT32(admittedRevision + 7, TrackCaptureStopTestAccess::playbackRevision(track));
  // Approved superseded rule: retire the interval, keep its accepted capture, and
  // never select an audible bridge against a source it did not succeed.
  const size_t passesBefore = loop.passes.overdubPasses.size();
  for (unsigned grain = 0; grain < 4096 && track.hasPendingDeferredOverdubStop(); ++grain)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_EQUAL_UINT32(passesBefore + 1, loop.passes.overdubPasses.size());
  TEST_ASSERT_EQUAL_UINT8(RuntimePlaybackBridgeLifecycle::kNoBridge,
                          TrackCaptureStopTestAccess::bridgeSelected(track));
  // The unpublished companion preparation is back on its prewarmed horizon, so
  // the next interval needs no STOPPED-only re-prewarm, and the retired
  // descriptor carries no causal receipt.
  TEST_ASSERT_FALSE(loop.preparedCompanionHistoryInFlight());
  TEST_ASSERT_TRUE(loop.preparedCompanionHistoryPrewarmed());
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::entryCausalBaseRecorded(track, admittedHead));
}

void test_stop_job_owns_held_notes_and_closes_original_slot() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, UINT32_MAX);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  auto& job = TrackCaptureStopTestAccess::job(track);
  TEST_ASSERT_EQUAL(1, job.pendingNotes.size());
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::liveNotes(track).empty());
  TEST_ASSERT_EQUAL(144, job.phaseTick);
  Loop& original = track.getLoop(0);
  Loop& other = track.getLoop(1);
  other.loopLengthTicks = 384;
  other.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(other.appendCaptureEvent(MidiEvent::NoteOn(11, 1, 72, 90)));
  // Inject a newer ingress map and selection independently of the guarded
  // public reentry path. The worker must use its receipt, never this selection.
  TrackCaptureStopTestAccess::liveNotes(track)[{60, 1}] = PendingNote{60, 1, 193, 100};
  TrackCaptureStopTestAccess::selectSlot(track, 1);
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::step(track));
  TEST_ASSERT_EQUAL(0, job.admission.remainingClosureGrains);
  // The captured NoteOn belongs to the job-owned sealed extent. The reserved
  // terminal Off is appended afterwards to the successor producer extent, which
  // is why the readable capture interval exposes both sources.
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::ownsRawRange(track));
  const SessionMidiEventVec sealedRows =
      sealedRangeRows(TrackCaptureStopTestAccess::rawRange(track));
  TEST_ASSERT_EQUAL(1, sealedRows.size());
  TEST_ASSERT_TRUE(sealedRows[0].isNoteOn());
  TEST_ASSERT_EQUAL(97, sealedRows[0].tick);
  TEST_ASSERT_EQUAL(1, original.capture.store.size());
  TEST_ASSERT_TRUE(original.capture.store.at(0).isNoteOff());
  TEST_ASSERT_EQUAL(144, original.capture.store.at(0).tick);
  TEST_ASSERT_EQUAL(1, other.capture.store.size());
  TEST_ASSERT_EQUAL(11, other.capture.store.at(0).tick);
  TEST_ASSERT_EQUAL(193, TrackCaptureStopTestAccess::liveNotes(track).at({60, 1}).startNoteTick);
}

void test_stop_job_denies_changed_geometry_before_closure_mutation() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, UINT32_MAX);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  auto& job = TrackCaptureStopTestAccess::job(track);
  Loop& original = track.getLoop(0);
  original.loopLengthTicks = 384;
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::step(track));
  // Denial mutates neither the job-owned sealed extent nor the producer.
  TEST_ASSERT_EQUAL(1, TrackCaptureStopTestAccess::rawRange(track).size());
  TEST_ASSERT_EQUAL(0, original.capture.store.size());
  TEST_ASSERT_EQUAL(1, job.admission.remainingClosureGrains);
  TEST_ASSERT_EQUAL(1, job.pendingNotes.size());
  original.loopLengthTicks = 768;
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::step(track));
  TEST_ASSERT_EQUAL(1, original.capture.store.size());
  TEST_ASSERT_EQUAL(0, job.admission.remainingClosureGrains);
}

void test_empty_stop_runs_existing_worker_to_completion() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  for (unsigned i = 0; i < 32 && track.hasPendingDeferredOverdubStop(); ++i)
    TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_TRUE(track.isPlaying());
  TEST_ASSERT_NULL(TrackCaptureStopTestAccess::job(track).owner);
}

void test_completed_job_cannot_transition_a_newer_track_interval() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  // State/receipt isolation only: actual reentry is still guarded until Loop
  // session, raw storage and staging are migrated. Do not claim a live B here.
  TrackCaptureStopTestAccess::advanceInterval(track);
  TrackCaptureStopTestAccess::liveNotes(track)[{60, 1}] = PendingNote{60, 1, 193, 100};
  track.forceSetState(TRACK_OVERDUBBING);
  for (unsigned i = 0; i < 32 && track.hasPendingDeferredOverdubStop(); ++i)
    TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_TRUE(track.isOverdubbing());
  TEST_ASSERT_EQUAL(193, TrackCaptureStopTestAccess::liveNotes(track).at({60, 1}).startNoteTick);
}

void test_populated_stop_runs_existing_worker_to_completion() {
  for (bool held : {false, true}) {
    if (held) setUp();
    Track& track = preparePlayingTrack();
    clockManager.assignCurrentTickSilently(96);
    midiButtonActions.handleToggleRecord();
    inputNote(60, 97, held ? UINT32_MAX : 120);
    clockManager.assignCurrentTickSilently(144);
    midiButtonActions.handleToggleRecord();
    for (unsigned i = 0; i < 10000 && track.hasPendingDeferredOverdubStop(); ++i)
      TrackCaptureStopTestAccess::step(track);
    TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
    TEST_ASSERT_TRUE(track.isPlaying());
    TEST_ASSERT_NULL(TrackCaptureStopTestAccess::job(track).owner);
    TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::job(track).pendingNotes.empty());
    TEST_ASSERT_EQUAL(1, track.getActiveLoop().passes.overdubPasses.size());
  }
}

void test_job_note_retirement_does_not_clear_live_notes() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, UINT32_MAX);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  auto& job = TrackCaptureStopTestAccess::job(track);
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::step(track));
  // Isolate the post-commit node-retirement grain. Do not pretend this phase
  // injection proves canonical publication; the empty-stop test runs that path.
  job.admission.phase = PendingStopPhase::DeferredCleanup;
  TrackCaptureStopTestAccess::liveNotes(track)[{60, 1}] = PendingNote{60, 1, 193, 100};
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::step(track));
  TEST_ASSERT_TRUE(job.pendingNotes.empty());
  TEST_ASSERT_EQUAL(193, TrackCaptureStopTestAccess::liveNotes(track).at({60, 1}).startNoteTick);
  TEST_ASSERT_TRUE(job.admission.pending);
}

void test_stop_copies_canonical_rows_without_mutating_sealed_raw_input() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  for (unsigned i = 0; i < 300; ++i)
    TEST_ASSERT_TRUE(track.recordMidiEvents(midi::ControlChange, 1, 7, i % 128, 300 - i));
  auto& loop = track.getActiveLoop();
  const auto rawChunk = loop.capture.store.chunkIds().front();
  TEST_ASSERT_EQUAL(ChunkLifecycleState::Sealed, LoopEventStore::chunkLifecycleState(rawChunk));
  clockManager.assignCurrentTickSilently(600);
  midiButtonActions.handleToggleRecord();
  auto& job = TrackCaptureStopTestAccess::job(track);
  auto& runtime = TrackCaptureStopTestAccess::runtime(track);
  for (unsigned i = 0; i < 10000 && job.admission.phase != PendingStopPhase::CanonicalRewrite; ++i)
    TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL(PendingStopPhase::CanonicalRewrite, job.admission.phase);
  TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL(32, runtime.canonicalCaptureOutput.store.size());
  TEST_ASSERT_EQUAL(1, runtime.canonicalCaptureOutput.store.at(0).tick);
  // Ownership transferred at punch-out: the job owns the sealed 300-row extent
  // and the producer holds only the reserved successor directory.
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::ownsRawRange(track));
  TEST_ASSERT_EQUAL(300, TrackCaptureStopTestAccess::rawRange(track).size());
  const SessionMidiEventVec sealedRows =
      sealedRangeRows(TrackCaptureStopTestAccess::rawRange(track));
  TEST_ASSERT_EQUAL(300, sealedRows.size());
  // The sealed extent preserves raw append order (descending ticks here); the
  // canonical copy is what imposes ascending order.
  TEST_ASSERT_EQUAL(300, sealedRows[0].tick);
  TEST_ASSERT_EQUAL(1, sealedRows[299].tick);
  TEST_ASSERT_TRUE(loop.capture.store.empty());
  TEST_ASSERT_NOT_EQUAL(rawChunk, runtime.canonicalCaptureOutput.store.chunkIds().front());
  // The sealed pool chunk is still byte-identical: the canonical copy wrote to
  // different physical chunks and never rewrote the job-owned extent.
  MidiEvent raw; bool end = true;
  TEST_ASSERT_TRUE(LoopEventStore::readSealedChunkEvent(rawChunk, 0, raw, end));
  TEST_ASSERT_EQUAL(300, raw.tick);
  for (unsigned i = 0; i < 10000 && track.hasPendingDeferredOverdubStop(); ++i)
    TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_TRUE(runtime.canonicalCaptureOutputAdmitted());
  TEST_ASSERT_EQUAL(1, loop.passes.overdubPasses.size());
  SessionMidiEventVec canonical;
  LoopEventStore::appendChunkRefEvents(loop.passes.overdubPasses.back().committedChunkIds, canonical);
  TEST_ASSERT_EQUAL(300, canonical.size());
  for (unsigned i = 0; i < 300; ++i) TEST_ASSERT_EQUAL(i + 1, canonical[i].tick);
}

void test_transport_stop_during_canonical_copy_preserves_accepted_rows() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  for (unsigned i = 0; i < 300; ++i)
    TEST_ASSERT_TRUE(track.recordMidiEvents(midi::ControlChange, 1, 7, i % 128, 300 - i));
  clockManager.assignCurrentTickSilently(600);
  midiButtonActions.handleToggleRecord();
  auto& job = TrackCaptureStopTestAccess::job(track);
  auto& runtime = TrackCaptureStopTestAccess::runtime(track);
  for (unsigned i = 0; i < 10000 && job.admission.phase != PendingStopPhase::CanonicalRewrite; ++i)
    TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL(PendingStopPhase::CanonicalRewrite, job.admission.phase);
  TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL(32, runtime.canonicalCaptureOutput.store.size());
  const auto epoch = job.admission.transportCommandEpoch;
  track.stopOverdubbingToStopped();
  TEST_ASSERT_EQUAL(PendingStopDestination::Stopped, job.admission.destination);
  TEST_ASSERT_NOT_EQUAL(epoch, job.admission.transportCommandEpoch);
  TEST_ASSERT_EQUAL(32, runtime.canonicalCaptureOutput.store.size());
  for (unsigned i = 0; i < 10000 && job.admission.phase != PendingStopPhase::DeferredCleanup; ++i)
    TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL(PendingStopPhase::DeferredCleanup, job.admission.phase);
  TEST_ASSERT_EQUAL(PendingStopDestination::Stopped, job.admission.destination);
  TEST_ASSERT_TRUE(runtime.canonicalCaptureOutput.store.empty());
  auto& loop = track.getActiveLoop();
  TEST_ASSERT_EQUAL(1, loop.passes.overdubPasses.size());
  SessionMidiEventVec rows;
  LoopEventStore::appendChunkRefEvents(loop.passes.overdubPasses.back().committedChunkIds, rows);
  TEST_ASSERT_EQUAL(300, rows.size());
  for (unsigned i = 0; i < 300; ++i) TEST_ASSERT_EQUAL(i + 1, rows[i].tick);
}

void test_playback_reset_preserves_partial_copy_until_stopped_recovery() {
  for (unsigned route = 0; route < 2; ++route) {
    if (route) setUp();
    Track& track = preparePlayingTrack();
    clockManager.assignCurrentTickSilently(96);
    midiButtonActions.handleToggleRecord();
    for (unsigned i = 0; i < 300; ++i)
      TEST_ASSERT_TRUE(track.recordMidiEvents(midi::ControlChange, 1, 7, i % 128, 300 - i));
    clockManager.assignCurrentTickSilently(600);
    midiButtonActions.handleToggleRecord();
    auto& job = TrackCaptureStopTestAccess::job(track);
    auto& runtime = TrackCaptureStopTestAccess::runtime(track);
    TEST_ASSERT_EQUAL_PTR(&job.admission, runtime.captureStopOwner);
    for (unsigned i = 0; i < 10000 && job.admission.phase != PendingStopPhase::CanonicalRewrite; ++i)
      TrackCaptureStopTestAccess::step(track);
    TEST_ASSERT_EQUAL(PendingStopPhase::CanonicalRewrite, job.admission.phase);
    TrackCaptureStopTestAccess::step(track);
    TEST_ASSERT_EQUAL(32, runtime.canonicalCaptureOutput.store.size());
    const auto generation = TrackCaptureStopTestAccess::generation(track);
    if (route == 0) TEST_ASSERT_FALSE(runtime.reset(false));
    else TrackCaptureStopTestAccess::invalidate(track);
    TEST_ASSERT_EQUAL(generation, TrackCaptureStopTestAccess::generation(track));
    TEST_ASSERT_EQUAL(PendingStopDestination::Stopped, job.admission.destination);
    TEST_ASSERT_FALSE(job.admission.permitsPlaybackEmission());
    TEST_ASSERT_EQUAL(32, runtime.canonicalCaptureOutput.store.size());
    TEST_ASSERT_EQUAL(300, runtime.captureStagingIfPrewarmed()->storage.eventCount);
    TEST_ASSERT_TRUE(runtime.captureStagingIfPrewarmed()->frozen);
    const auto epoch = job.admission.transportCommandEpoch;
    TEST_ASSERT_FALSE(runtime.reset(true));
    TEST_ASSERT_EQUAL(epoch, job.admission.transportCommandEpoch);
    // Isolate job cleanup from the physical silence boundary in this fixture.
    // This does not claim that a newer live interval can already be admitted.
    TrackCaptureStopTestAccess::advanceInterval(track);
    for (unsigned i = 0; i < 10000 && track.hasPendingDeferredOverdubStop(); ++i)
      TrackCaptureStopTestAccess::step(track);
    TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
    TEST_ASSERT_NULL(runtime.captureStopOwner);
    auto& loop = track.getActiveLoop();
    TEST_ASSERT_EQUAL(1, loop.passes.overdubPasses.size());
    SessionMidiEventVec rows;
    LoopEventStore::appendChunkRefEvents(loop.passes.overdubPasses.back().committedChunkIds, rows);
    TEST_ASSERT_EQUAL(300, rows.size());
    for (unsigned i = 0; i < 300; ++i) TEST_ASSERT_EQUAL(i + 1, rows[i].tick);
    TEST_ASSERT_TRUE(runtime.reset(false));
  }
}

void test_cache_reset_at_each_stop_phase_keeps_accepted_capture() {
  const PendingStopPhase phases[] = {
      PendingStopPhase::ReservedClosure, PendingStopPhase::CanonicalPrefix,
      PendingStopPhase::CanonicalOrder, PendingStopPhase::CanonicalRewrite,
      PendingStopPhase::AudibleOrder, PendingStopPhase::PrepareTransfer,
      PendingStopPhase::CanonicalTail, PendingStopPhase::CompanionHistory,
      PendingStopPhase::CompanionBridge, PendingStopPhase::AtomicTransfer,
      PendingStopPhase::DeferredCleanup};
  for (size_t phaseIndex = 0; phaseIndex < sizeof(phases) / sizeof(phases[0]); ++phaseIndex) {
    if (phaseIndex) setUp();
    Track& track = preparePlayingTrack();
    clockManager.assignCurrentTickSilently(96);
    midiButtonActions.handleToggleRecord();
    for (unsigned i = 0; i < 300; ++i)
      TEST_ASSERT_TRUE(track.recordMidiEvents(midi::ControlChange, 1, 7, i % 128, 300 - i));
    inputNote(65, 97, UINT32_MAX);
    clockManager.assignCurrentTickSilently(600);
    midiButtonActions.handleToggleRecord();
    auto& job = TrackCaptureStopTestAccess::job(track);
    auto& runtime = TrackCaptureStopTestAccess::runtime(track);
    for (unsigned i = 0; i < 20000 && job.admission.phase != phases[phaseIndex]; ++i)
      TrackCaptureStopTestAccess::step(track);
    TEST_ASSERT_EQUAL(phases[phaseIndex], job.admission.phase);
    TEST_ASSERT_FALSE(runtime.reset(false));
    TEST_ASSERT_EQUAL(PendingStopDestination::Stopped, job.admission.destination);
    TEST_ASSERT_FALSE(job.admission.permitsPlaybackEmission());
    // Keep physical output outside this native fixture, as in the partial-copy test.
    TrackCaptureStopTestAccess::advanceInterval(track);
    for (unsigned i = 0; i < 20000 && track.hasPendingDeferredOverdubStop(); ++i)
      TrackCaptureStopTestAccess::step(track);
    TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
    TEST_ASSERT_NULL(runtime.captureStopOwner);
    auto& loop = track.getActiveLoop();
    TEST_ASSERT_EQUAL(1, loop.passes.overdubPasses.size());
    SessionMidiEventVec rows;
    LoopEventStore::appendChunkRefEvents(loop.passes.overdubPasses.back().committedChunkIds, rows);
    TEST_ASSERT_EQUAL(302, rows.size());
    unsigned ons = 0, offs = 0, controls = 0;
    for (const auto& event : rows) {
      if (event.isNoteOn()) { ++ons; TEST_ASSERT_EQUAL(97, event.tick); }
      else if (event.isNoteOff()) { ++offs; TEST_ASSERT_EQUAL(600, event.tick); }
      else ++controls;
    }
    TEST_ASSERT_EQUAL(1, ons);
    TEST_ASSERT_EQUAL(1, offs);
    TEST_ASSERT_EQUAL(300, controls);
    TEST_ASSERT_TRUE(runtime.reset(false));
  }
}

void test_overdub_shortens_record_pass_note_and_completes_stop() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(0);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.isOverdubbing());
  inputNote(48, 24, 72);
  auto& loop = track.getActiveLoop();
  TEST_ASSERT_TRUE(loop.hasPendingNoteChanges());
  clockManager.assignCurrentTickSilently(100);
  midiButtonActions.handleToggleRecord();
  for (unsigned i = 0; i < 20000 && track.hasPendingDeferredOverdubStop(); ++i)
    TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_TRUE(track.isPlaying());
  TEST_ASSERT_EQUAL(1, loop.passes.overdubPasses.size());
  SessionMidiEventVec rows;
  loop.passes.materializeToEventVector(rows, loop.loopLengthTicks);
  unsigned onCount = 0, offCount = 0;
  bool shortened = false;
  for (const auto& event : rows) {
    if (event.isNoteOn()) ++onCount;
    if (event.isNoteOff()) { ++offCount; shortened |= event.tick == 23; }
  }
  TEST_ASSERT_EQUAL(2, onCount);
  TEST_ASSERT_EQUAL(2, offCount);
  TEST_ASSERT_TRUE(shortened);
}

void test_stop_descriptor_rotation_preserves_worker_borrow_and_reuses_retired_slots() {
  Track& track = preparePlayingTrack();
  const void* first = &TrackCaptureStopTestAccess::job(track);
  for (size_t i = 0; i <= TrackCaptureStopTestAccess::queueCapacity(); ++i) {
    clockManager.assignCurrentTickSilently(96);
    midiButtonActions.handleToggleRecord();
    TEST_ASSERT_TRUE(track.isOverdubbing());
    auto* receipt = &TrackCaptureStopTestAccess::job(track).admission;
    if (i == TrackCaptureStopTestAccess::queueCapacity())
      TEST_ASSERT_EQUAL_PTR(first, &TrackCaptureStopTestAccess::job(track));
    clockManager.assignCurrentTickSilently(144);
    midiButtonActions.handleToggleRecord();
    TEST_ASSERT_EQUAL(1, TrackCaptureStopTestAccess::queueSize(track));
    TEST_ASSERT_EQUAL_PTR(receipt, TrackCaptureStopTestAccess::runtime(track).captureStopOwner);
    for (unsigned grain = 0; grain < 10000 && track.hasPendingDeferredOverdubStop(); ++grain)
      TrackCaptureStopTestAccess::step(track);
    TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
    TEST_ASSERT_EQUAL(0, TrackCaptureStopTestAccess::queueSize(track));
    TEST_ASSERT_NULL(TrackCaptureStopTestAccess::runtime(track).captureStopOwner);
    TEST_ASSERT_TRUE(track.isPlaying());
    // This test covers sequential descriptor reuse, not concurrent reentry.
    // The existing session handoff must finish before the next ordinary start.
    auto& loop = track.getActiveLoop();
    for (unsigned grain = 0; grain < 10000 && loop.hasPendingSnapshotPublish(); ++grain)
      loop.stepPendingSnapshotPublish(1);
    TEST_ASSERT_TRUE(loop.canHandoffSessionExitSnapshot());
  }
}

void test_missing_stop_descriptors_rejects_before_opening_capture() {
  Track& track = preparePlayingTrack();
  TrackCaptureStopTestAccess::discardColdQueue(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.isPlaying());
  TEST_ASSERT_FALSE(track.getActiveLoop().captureActive());
  inputNote(60, 97, 120);
  TEST_ASSERT_TRUE(track.getActiveLoop().capture.store.empty());
}

/// Task 2.3 FIFO binding: the ring is the admission authority. A new interval
/// always takes the tail descriptor, the worker resolves one stable head for the
/// whole grain, and a full ring is an explicit denial that consumes no accepted
/// input or live state.
void test_stop_ring_admits_at_tail_and_binds_worker_to_head() {
  Track& track = preparePlayingTrack();
  // Empty ring: no head, and the next admission target is the entry at head.
  TEST_ASSERT_NULL(TrackCaptureStopTestAccess::headPtr(track));
  TEST_ASSERT_EQUAL_PTR(TrackCaptureStopTestAccess::entryPtr(track, 0),
                        TrackCaptureStopTestAccess::tailPtr(track));

  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_EQUAL(1, TrackCaptureStopTestAccess::queueSize(track));
  // With one queued job the admitted tail descriptor is the head the worker
  // binds, and it stays pending across grains.
  TEST_ASSERT_EQUAL_PTR(TrackCaptureStopTestAccess::entryPtr(track, 0),
                        TrackCaptureStopTestAccess::headPtr(track));
  TEST_ASSERT_EQUAL_PTR(TrackCaptureStopTestAccess::headPtr(track),
                        &TrackCaptureStopTestAccess::job(track));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::job(track).admission.pending);
}

/// Seal transfers the exact admitted extent, and a successor interval on the
/// same loop may reuse the producer without altering the job's owned extent.
void test_seal_transfers_extent_and_successor_capture_preserves_it() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  inputNote(62, 100, 130);
  Loop& loop = track.getActiveLoop();
  TEST_ASSERT_EQUAL(4, loop.capture.store.size());
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::ownsRawRange(track));
  TEST_ASSERT_TRUE(loop.capture.store.empty());
  const auto& range = TrackCaptureStopTestAccess::rawRange(track);
  const SessionMidiEventVec before = sealedRangeRows(range);
  TEST_ASSERT_EQUAL(4, before.size());
  const uint64_t generation = range.generation();

  // Successor interval begins on the same loop (the guarded public reentry
  // route is deliberately bypassed). The predecessor extent must survive.
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(200, 1, 64, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(210, 1, 64, 0)));
  TEST_ASSERT_EQUAL(2, loop.capture.store.size());
  TEST_ASSERT_EQUAL_UINT64(generation, range.generation());
  const SessionMidiEventVec after = sealedRangeRows(range);
  TEST_ASSERT_EQUAL(before.size(), after.size());
  for (size_t i = 0; i < before.size(); ++i) {
    TEST_ASSERT_EQUAL(before[i].tick, after[i].tick);
    TEST_ASSERT_EQUAL(static_cast<int>(before[i].type), static_cast<int>(after[i].type));
    TEST_ASSERT_EQUAL(before[i].data.noteData.note, after[i].data.noteData.note);
  }
}

/// The readable capture interval yields the same effective rows before and after
/// sealing, and ordering the merge is scratch-only: a borrowed sealed extent is
/// never sorted or rewritten in place.
void test_read_source_yields_same_rows_before_and_after_sealing() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  // Deliberately unsorted append order: the merge must impose scratch order.
  inputNote(62, 130, 150);
  inputNote(64, 100, 120);
  Loop& loop = track.getActiveLoop();
  SessionMidiEventVec before;
  loop.copyEffectiveCommittedEventsWithCapture(before);
  TEST_ASSERT_TRUE(before.size() >= 4);

  clockManager.assignCurrentTickSilently(180);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::ownsRawRange(track));
  const SessionMidiEventVec rangeBefore =
      sealedRangeRows(TrackCaptureStopTestAccess::rawRange(track));

  SessionMidiEventVec after;
  loop.copyEffectiveCommittedEventsWithCapture(after);
  TEST_ASSERT_EQUAL(before.size(), after.size());
  for (size_t i = 0; i < before.size(); ++i) {
    TEST_ASSERT_EQUAL(before[i].tick, after[i].tick);
    TEST_ASSERT_EQUAL(static_cast<int>(before[i].type), static_cast<int>(after[i].type));
    TEST_ASSERT_EQUAL(before[i].data.noteData.note, after[i].data.noteData.note);
  }
  // The sealed extent is byte-identical after the merge: ordering used scratch.
  const SessionMidiEventVec rangeAfter =
      sealedRangeRows(TrackCaptureStopTestAccess::rawRange(track));
  TEST_ASSERT_EQUAL(rangeBefore.size(), rangeAfter.size());
  for (size_t i = 0; i < rangeBefore.size(); ++i) {
    TEST_ASSERT_EQUAL(rangeBefore[i].tick, rangeAfter[i].tick);
    TEST_ASSERT_EQUAL(static_cast<int>(rangeBefore[i].type), static_cast<int>(rangeAfter[i].type));
    TEST_ASSERT_EQUAL(rangeBefore[i].data.noteData.note, rangeAfter[i].data.noteData.note);
  }
}

/// A closure whose source NoteOn now lives only in the job-owned sealed extent
/// still resolves the original NoteId.
void test_sealed_extent_resolves_closure_note_id() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(65, 97, UINT32_MAX);
  Loop& loop = track.getActiveLoop();
  TEST_ASSERT_EQUAL(1, loop.capture.store.size());
  const NoteId capturedId = loop.capture.store.at(0).noteId;
  TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, capturedId);

  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::ownsRawRange(track));
  TEST_ASSERT_TRUE(loop.capture.store.empty());
  TEST_ASSERT_EQUAL(1, TrackCaptureStopTestAccess::rawRange(track).size());

  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::appendNoteOffAtPhase(track, 1, 65, 150));
  TEST_ASSERT_EQUAL(1, loop.capture.store.size());
  TEST_ASSERT_TRUE(loop.capture.store.at(0).isNoteOff());
  TEST_ASSERT_EQUAL(capturedId, loop.capture.store.at(0).noteId);
  // The sealed extent stays immutable and still holds exactly the captured On.
  TEST_ASSERT_EQUAL(1, TrackCaptureStopTestAccess::rawRange(track).size());
}

/// A stale job may not drop a successor job's read borrow.
void test_sealed_range_borrow_release_is_owner_identified() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  LoopEventStore producer;
  TEST_ASSERT_TRUE(producer.reserveCaptureTail());
  LoopEventStore::CaptureRange rangeA;
  LoopEventStore::CaptureRange rangeB;
  TEST_ASSERT_TRUE(rangeA.reserveTail());
  TEST_ASSERT_TRUE(rangeB.reserveTail());
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOn(10, 1, 60, 100)));
  TEST_ASSERT_TRUE(producer.sealCaptureRange(rangeA));
  TEST_ASSERT_TRUE(producer.sealCaptureRange(rangeB));

  const int ownerA = 1;
  const int ownerB = 2;
  TEST_ASSERT_TRUE(loop.borrowSealedCaptureRange(rangeA, &ownerA));
  TEST_ASSERT_TRUE(loop.borrowSealedCaptureRange(rangeB, &ownerB));
  TEST_ASSERT_FALSE(loop.releaseSealedCaptureRangeBorrow(&ownerA));
  TEST_ASSERT_TRUE(loop.hasSealedCaptureBorrow());
  TEST_ASSERT_EQUAL_PTR(&rangeB, loop.captureReadSource().finalizing);
  TEST_ASSERT_TRUE(loop.releaseSealedCaptureRangeBorrow(&ownerB));
  TEST_ASSERT_FALSE(loop.hasSealedCaptureBorrow());
}

/// Pending note changes (Add/Shorten/Hide intents) are moved into job ownership
/// at punch-out. The worker must resolve against that owned set, and read
/// consumers must keep seeing it through the job-identified borrow.
void test_stop_job_owns_pending_note_changes() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(0);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.isOverdubbing());
  inputNote(48, 24, 72);
  Loop& loop = track.getActiveLoop();
  TEST_ASSERT_TRUE(loop.hasPendingNoteChanges());
  const size_t changeCount = loop.pendingNoteChanges().size();
  TEST_ASSERT_TRUE(changeCount > 0);

  clockManager.assignCurrentTickSilently(100);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  auto& job = TrackCaptureStopTestAccess::job(track);
  // Ownership moved to the job; the readable set is the borrowed job copy.
  TEST_ASSERT_EQUAL(changeCount, job.pendingChanges.size());
  TEST_ASSERT_EQUAL_PTR(&job.pendingChanges, &loop.pendingNoteChanges());

  for (unsigned i = 0; i < 20000 && track.hasPendingDeferredOverdubStop(); ++i)
    TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  // The borrow is retired with the job, and the live session's own set — which
  // a successor interval would use — is independent of the job's.
  TEST_ASSERT_TRUE(&loop.pendingNoteChanges() != &job.pendingChanges);
  TEST_ASSERT_TRUE(loop.pendingNoteChanges().empty());
  TEST_ASSERT_EQUAL(changeCount, job.pendingChanges.size());
}

/// A stale job may not drop a successor job's pending-change borrow.
void test_pending_change_borrow_release_is_owner_identified() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  PendingNoteChangeVec changesA;
  PendingNoteChangeVec changesB;
  const int ownerA = 1;
  const int ownerB = 2;
  TEST_ASSERT_TRUE(loop.borrowPendingNoteChanges(changesA, &ownerA));
  TEST_ASSERT_TRUE(loop.borrowPendingNoteChanges(changesB, &ownerB));
  TEST_ASSERT_FALSE(loop.releasePendingNoteChangesBorrow(&ownerA));
  TEST_ASSERT_EQUAL_PTR(&changesB, &loop.pendingNoteChanges());
  TEST_ASSERT_TRUE(loop.releasePendingNoteChangesBorrow(&ownerB));
  const PendingNoteChangeVec* live = &loop.pendingNoteChanges();
  TEST_ASSERT_TRUE(live != &changesA);
  TEST_ASSERT_TRUE(live != &changesB);
}

/// The capture preview is moved into job ownership at punch-out, so the deferred
/// cleanup seals this interval's preview even if a successor replaces the live
/// one. Display readers keep seeing it through the job-identified borrow.
void test_stop_job_owns_capture_preview() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  Loop& loop = track.getActiveLoop();
  TEST_ASSERT_TRUE(loop.capturePreview.notes.size() > 0);
  const size_t previewRows = loop.capturePreview.notes.size();

  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  auto& job = TrackCaptureStopTestAccess::job(track);
  // Ownership moved to the job; the display view is the borrowed job preview.
  TEST_ASSERT_EQUAL(previewRows, job.preview.notes.size());
  TEST_ASSERT_EQUAL_PTR(&job.preview, &loop.capturePreviewView());
  TEST_ASSERT_EQUAL(previewRows, loop.capturePreviewView().notes.size());

  for (unsigned i = 0; i < 20000 && track.hasPendingDeferredOverdubStop(); ++i)
    TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  // The borrow is retired with the job; the live preview a successor would use is
  // a different buffer and the job's copy was consumed by the deferred seal.
  TEST_ASSERT_TRUE(&loop.capturePreviewView() != &job.preview);
  TEST_ASSERT_TRUE(job.preview.notes.empty());
}

/// A stale job may not drop a successor job's preview borrow.
void test_capture_preview_borrow_release_is_owner_identified() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  CapturePreview previewA;
  CapturePreview previewB;
  const int ownerA = 1;
  const int ownerB = 2;
  TEST_ASSERT_TRUE(loop.borrowCapturePreview(previewA, &ownerA));
  TEST_ASSERT_TRUE(loop.borrowCapturePreview(previewB, &ownerB));
  TEST_ASSERT_FALSE(loop.releaseCapturePreviewBorrow(&ownerA));
  TEST_ASSERT_EQUAL_PTR(&previewB, &loop.capturePreviewView());
  TEST_ASSERT_TRUE(loop.releaseCapturePreviewBorrow(&ownerB));
  const CapturePreview* live = &loop.capturePreviewView();
  TEST_ASSERT_TRUE(live != &previewA);
  TEST_ASSERT_TRUE(live != &previewB);
}

/// The overdub-session undo baseline (published root at session open) moves into
/// job ownership at punch-out, so a successor session's own baseline cannot
/// rewrite the before-snapshot this interval's OverdubPassAdded entry records.
void test_stop_job_owns_overdub_session_undo_baseline() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.isOverdubbing());
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::liveHasBeforePublished(track));

  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::jobHasBeforePublished(track));
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::liveHasBeforePublished(track));

  // A successor session records its own baseline; the job's stays intact.
  TrackCaptureStopTestAccess::setLiveBeforePublished(track, true);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::liveHasBeforePublished(track));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::jobHasBeforePublished(track));

  for (unsigned i = 0; i < 20000 && track.hasPendingDeferredOverdubStop(); ++i)
    TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  // Consumed by the undo entry or retired with the job; never carried forward.
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::jobHasBeforePublished(track));
}

/// The deferred stop collects the session's undo identities in bounded grains and
/// builds the OverdubPassAdded entry from the job's own slot and baseline, on the
/// prepared PLAYING route.
void test_deferred_stop_prepares_job_owned_undo_entry() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  auto& job = TrackCaptureStopTestAccess::job(track);
  TEST_ASSERT_FALSE(job.undoCollected);

  for (unsigned i = 0; i < 20000 && !job.undoCollected; ++i)
    TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_TRUE(job.undoCollectionStarted);
  TEST_ASSERT_TRUE_MESSAGE(job.undoCollected, "the deferred stop must build a prepared undo entry");
  TEST_ASSERT_TRUE(job.undoCollectable);
  TEST_ASSERT_EQUAL(static_cast<int>(UndoEntryKind::OverdubPassAdded),
                    static_cast<int>(job.undoEntry.kind));
  TEST_ASSERT_EQUAL_UINT8(job.slot, job.undoEntry.slotIndex);
  TEST_ASSERT_EQUAL_UINT32(job.loopId, job.undoEntry.loopId);
  TEST_ASSERT_NOT_EQUAL(kInvalidPassId, job.undoEntry.passId);
  // The entry's baseline is the job's own captured one (null is legitimate when
  // the session opened before any snapshot was published).
  TEST_ASSERT_EQUAL_PTR(job.beforePublished.get(), job.undoEntry.beforePublishedSnapshot.get());

  // Run the deferred stop to completion: the prepared entry must actually be
  // published as one OverdubPassAdded on the atomic commit, and the protected
  // transaction must return to Idle so a later interval can publish again.
  for (unsigned i = 0; i < 20000 && track.hasPendingDeferredOverdubStop(); ++i)
    TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  auto* publication = TrackCaptureStopTestAccess::undoPublication(track);
  TEST_ASSERT_NOT_NULL(publication);
  TEST_ASSERT_FALSE(publication->protectedState());
  const GlobalUndoStack& stack = TrackCaptureStopTestAccess::undoStack(track);
  size_t overdubEntries = 0;
  for (const UndoEntry& entry : stack.entries) {
    if (entry.kind == UndoEntryKind::OverdubPassAdded && entry.slotIndex == job.slot) ++overdubEntries;
  }
  TEST_ASSERT_EQUAL(1, overdubEntries);
}

/// A full ring is explicit exhaustion, not finalization latency: admission is
/// denied, performs no seal/ownership transfer, and consumes no accepted input.
void test_full_stop_ring_denies_admission_and_preserves_accepted_capture() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.isOverdubbing());
  inputNote(60, 97, 120);
  TEST_ASSERT_EQUAL(2, track.getActiveLoop().capture.store.size());
  const size_t liveNotesBefore = TrackCaptureStopTestAccess::liveNotes(track).size();

  // Saturate the ring outside the guarded public reentry route. The ring, not a
  // staging-buffer side effect, is the admission authority.
  TrackCaptureStopTestAccess::setQueueCount(track, TrackCaptureStopTestAccess::queueCapacity());
  TEST_ASSERT_EQUAL(TrackCaptureStopTestAccess::queueCapacity(),
                    TrackCaptureStopTestAccess::queueSize(track));
  TEST_ASSERT_NULL(TrackCaptureStopTestAccess::tailPtr(track));
  TEST_ASSERT_NOT_NULL(TrackCaptureStopTestAccess::headPtr(track));

  TEST_ASSERT_FALSE(
      TrackCaptureStopTestAccess::admit(track, PendingStopDestination::Playing, 144));
  // A denied admission must not consume accepted input or live state, and it
  // performs no seal and no ownership transfer.
  TEST_ASSERT_TRUE(track.isOverdubbing());
  TEST_ASSERT_EQUAL(2, track.getActiveLoop().capture.store.size());
  TEST_ASSERT_EQUAL(liveNotesBefore, TrackCaptureStopTestAccess::liveNotes(track).size());
  TEST_ASSERT_EQUAL(TrackCaptureStopTestAccess::queueCapacity(),
                    TrackCaptureStopTestAccess::queueSize(track));
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::ownsRawRange(track));
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::rawRange(track).ready());
}

/// Task 2.3 staging ownership: an admitted stop job borrows the frozen admission
/// staging while live ingress owns a physically separate buffer. Punch-out
/// exchanges the two roles in constant work, so old-job completion can never
/// clear, reset or alias a successor interval's ingress rows.
void test_stop_job_and_live_ingress_own_distinct_staging() {
  Track& track = preparePlayingTrack();
  auto& runtime = TrackCaptureStopTestAccess::runtime(track);
  auto* liveRoleBefore = runtime.liveCaptureStagingIfPrewarmed();
  auto* frozenRoleBefore = runtime.captureStagingIfPrewarmed();
  TEST_ASSERT_NOT_NULL(liveRoleBefore);
  TEST_ASSERT_NOT_NULL(frozenRoleBefore);
  TEST_ASSERT_TRUE(liveRoleBefore->storage.events != frozenRoleBefore->storage.events);

  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());

  // The punch-out exchanged the roles: the job owns the rows it captured, and
  // the previous clean buffer is now live ingress ownership.
  auto* frozen = runtime.captureStagingIfPrewarmed();
  auto* live = runtime.liveCaptureStagingIfPrewarmed();
  TEST_ASSERT_NOT_NULL(frozen);
  TEST_ASSERT_NOT_NULL(live);
  TEST_ASSERT_EQUAL_PTR(liveRoleBefore, frozen);
  TEST_ASSERT_EQUAL_PTR(frozenRoleBefore, live);
  TEST_ASSERT_TRUE(frozen->frozen);
  TEST_ASSERT_EQUAL(2, frozen->storage.eventCount);
  TEST_ASSERT_FALSE(live->frozen);
  TEST_ASSERT_EQUAL(0, live->storage.eventCount);

  // A successor interval's accepted ingress rows must survive this job's
  // canonical work, commit and cleanup unchanged.
  TEST_ASSERT_TRUE(live->storage.append(MidiEvent::NoteOn(193, 1, 62, 100)));
  for (unsigned i = 0; i < 10000 && track.hasPendingDeferredOverdubStop(); ++i)
    TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_TRUE(track.isPlaying());
  TEST_ASSERT_EQUAL(1, live->storage.eventCount);
  TEST_ASSERT_EQUAL(193, live->storage.events[0].tick);
}

#include "off_path_deps.h"

/// Regression for the reset-before-guard defect: a redundant or ineligible
/// queueDeferredStoredMidiVerification() call must not cancel an already-queued dump.
/// Production reaches this with two overdub-stop finalize attempts (two
/// ODUB,stop,prefix_reuse lines); here the two queue attempts are direct.
void test_redundant_stored_verification_queue_keeps_pending_dump() {
  Track& track = preparePlayingTrack();
  track.setDeferredStoredVerificationArmedForTest(true);
  track.queueDeferredStoredMidiVerification();
  TEST_ASSERT_TRUE(track.deferredStoredVerificationPendingForTest());
  TEST_ASSERT_FALSE(track.deferredStoredVerificationArmedForTest());
  TEST_ASSERT_EQUAL_UINT32(1, track.deferredStoredVerificationOpportunityCountForTest());

  // Second call with the arm already consumed is ineligible and must be a no-op.
  track.queueDeferredStoredMidiVerification();
  TEST_ASSERT_TRUE_MESSAGE(track.deferredStoredVerificationPendingForTest(),
                           "ineligible queue must not clear a pending dump");
  TEST_ASSERT_EQUAL_UINT32(1, track.deferredStoredVerificationOpportunityCountForTest());
}

/// Ineligible because the loop is not representable (no committed pass): also a no-op.
void test_ineligible_stored_verification_queue_keeps_pending_dump() {
  Track& track = preparePlayingTrack();
  track.setDeferredStoredVerificationArmedForTest(true);
  track.queueDeferredStoredMidiVerification();
  TEST_ASSERT_TRUE(track.deferredStoredVerificationPendingForTest());

  track.setDeferredStoredVerificationArmedForTest(true);
  track.getActiveLoop().loopLengthTicks = 0;
  track.queueDeferredStoredMidiVerification();
  TEST_ASSERT_TRUE_MESSAGE(track.deferredStoredVerificationPendingForTest(),
                           "ineligible (unrepresentable) queue must not clear a pending dump");
  TEST_ASSERT_EQUAL_UINT32(1, track.deferredStoredVerificationOpportunityCountForTest());
}

/// DEC-069 defensive guard: `Track::commitOverdubWrap` may only be entered when no deferred job
/// owns the slot (the deferral gate in `maybeCommitOverdubWrap` is the primary owner of that
/// question), but the wrap must still not clobber a pending predecessor's canonical work if it is
/// ever reached with a job pending. It used to call `resumeCanonicalAfterAppend` -- whose
/// `resumeCurrentAfterAppend` resets the frozen capture binding -- and to discard the frozen
/// order/rewrite builders unconditionally. This pins the guard directly.
void test_successor_wrap_keeps_the_predecessors_frozen_builders() {
  Track& track = preparePlayingTrack();
  // A: deferred stop whose job owns the frozen role and binds the slot's canonical source.
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, UINT32_MAX);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  for (unsigned grain = 0;
       grain < 20000 &&
       TrackCaptureStopTestAccess::jobPhase(track) <
           static_cast<uint8_t>(PendingStopPhase::CanonicalTail);
       ++grain)
    (void)TrackCaptureStopTestAccess::step(track);
  for (unsigned grain = 0;
       grain < 64 && !TrackCaptureStopTestAccess::canonicalHasFrozenBinding(track); ++grain)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::canonicalHasFrozenBinding(track),
      "the predecessor must bind its frozen canonical source first");
  const uint64_t generationBefore = TrackCaptureStopTestAccess::frozenOrderStorageGeneration(track);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::canonicalOrderComplete(track));
  // B: successor interval with rows, then a direct commit attempt while A is still pending.
  clockManager.assignCurrentTickSilently(192);
  track.startOverdubbing(192);
  inputNote(64, 193, UINT32_MAX);
  inputNote(62, 200, 240);
  (void)track.commitOverdubWrap();
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      generationBefore, TrackCaptureStopTestAccess::frozenOrderStorageGeneration(track),
      "the wrap must not discard the pending predecessor's frozen builders");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::canonicalOrderComplete(track),
      "the predecessor's completed canonical order must survive the wrap");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::canonicalHasFrozenBinding(track),
      "the wrap must not reset the pending predecessor's frozen canonical source");
}

/// DEC-069 Option A: while a deferred stop job owns the slot's canonical revision, the live
/// interval's wrap records across loop end but seals no pass; the commit is owed to the first turn
/// after the job retires. Before this, the seal advanced `Loop::playbackRevision`, which
/// invalidated the job's frozen canonical source, so it never retired and this interval's own stop
/// was refused as occupied for the rest of the session.
void test_successor_wrap_defers_its_pass_commit_while_a_stop_job_is_pending() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  // A: deferred stop whose job owns the slot's canonical work (held note gives it closure work).
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, UINT32_MAX);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  const uint32_t revisionBefore = loop.playbackRevision;
  const size_t passesBefore = loop.passes.overdubPasses.size();
  // B: successor interval that crosses loop end while A is still pending.
  clockManager.assignCurrentTickSilently(192);
  track.startOverdubbing(192);
  inputNote(64, 193, UINT32_MAX);
  inputNote(62, 200, 240);
  TEST_ASSERT_FALSE_MESSAGE(track.maybeCommitOverdubWrap(700, 40),
      "the crossing must not seal a pass while a stop job owns the slot's revision");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::wrapCommitPending(track),
      "the deferred wrap commit must be owed, not dropped");
  TEST_ASSERT_EQUAL_UINT32(revisionBefore, loop.playbackRevision);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(passesBefore),
                           static_cast<uint32_t>(loop.passes.overdubPasses.size()));
  // A retires: nothing advanced the revision it pinned, so its own commit succeeds.
  for (unsigned grain = 0; grain < 20000 && track.hasPendingDeferredOverdubStop(); ++grain)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE_MESSAGE(track.hasPendingDeferredOverdubStop(),
      "the predecessor must retire when the successor wrap defers its pass commit");
  const size_t passesAfterPredecessor = loop.passes.overdubPasses.size();
  const uint32_t revisionAfterPredecessor = loop.playbackRevision;
  // The owed commit runs on the next ordinary playback turn and seals exactly one pass.
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::wrapCommitPending(track));
  TEST_ASSERT_TRUE(track.maybeCommitOverdubWrap(40, 41));
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::wrapCommitPending(track));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(passesAfterPredecessor + 1),
                           static_cast<uint32_t>(loop.passes.overdubPasses.size()));
  TEST_ASSERT_GREATER_THAN_UINT32(revisionAfterPredecessor, loop.playbackRevision);
}

/// DEC-070: a transport stop retargets a job that was admitted for PLAYING. Its PLAYING-only
/// preparation (canonical tail, companion history, undo collection, companion bridge) is bound to
/// the pre-stop transport command epoch, so continuing it parked the job forever -- holding
/// `captureStopOwner`, the frozen role and the session-exit receipt -- and every later stop was
/// refused until a reboot (`Overdub stop refused: stop_job_pending phase=7 dest=1`). The retarget
/// must re-route onto the STOPPED route's continuation and retire the job.
void test_transport_stop_retarget_retires_a_midflight_playing_job() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PendingStopDestination::Playing),
                          TrackCaptureStopTestAccess::jobDestination(track));
  // Drive the job past the transfer decision point into the PLAYING-only branch
  // (`CanonicalTail`..`CompanionBridge`); without a retarget it parks there.
  for (unsigned grain = 0; grain < 20000; ++grain) {
    const uint8_t phase = TrackCaptureStopTestAccess::jobPhase(track);
    if (phase >= static_cast<uint8_t>(PendingStopPhase::CanonicalTail) &&
        phase != static_cast<uint8_t>(PendingStopPhase::PrepareTransfer))
      break;
    (void)TrackCaptureStopTestAccess::step(track);
  }
  const uint8_t phaseBeforeStop = TrackCaptureStopTestAccess::jobPhase(track);
  TEST_ASSERT_TRUE_MESSAGE(
      phaseBeforeStop >= static_cast<uint8_t>(PendingStopPhase::CanonicalTail) &&
          phaseBeforeStop != static_cast<uint8_t>(PendingStopPhase::PrepareTransfer),
      "the job must reach the PLAYING-only branch before the transport stop");
  // Transport Stop while the job is mid-flight.
  TrackCaptureStopTestAccess::requestStoppedStop(track);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PendingStopDestination::Stopped),
                          TrackCaptureStopTestAccess::jobDestination(track));
  // A newer interval owns the live state (the device case: the transport stops while a successor is
  // live), so the retiring job must be job-local and must not drive the track's state transition.
  TrackCaptureStopTestAccess::advanceInterval(track);
  // The retarget keeps the interval's companion (note-edit) preparation: the display resolves the
  // overdubbed notes from it, so it must be *prepared and published*, not skipped. The job therefore
  // continues into the PLAYING-route companion phases instead of jumping straight to the transfer.
  (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<uint8_t>(PendingStopPhase::CanonicalTail),
      TrackCaptureStopTestAccess::jobPhase(track),
      "a retargeted job must keep preparing its companion representation");
  // Those phases cannot complete in this fixture (their playback-side inputs never run), so the
  // bounded fallback must take over and retire the job rather than park it.
  for (unsigned grain = 0; grain < 20000 && track.hasPendingDeferredOverdubStop(); ++grain)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE_MESSAGE(track.hasPendingDeferredOverdubStop(),
      "a transport-stop retarget must retire the mid-flight job, not park it");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::captureStopOwnerClear(track),
      "the retired job must release the stop owner");
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::canonicalOutputAdmitted(track));
  // And a later stop of a live interval is admitted again.
  clockManager.assignCurrentTickSilently(240);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.isOverdubbing());
  clockManager.assignCurrentTickSilently(288);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(track.hasPendingDeferredOverdubStop(),
      "the slot must accept a later stop once the retargeted job retired");
}

/// With the wrap commit deferred (DEC-069 Option A) a note held across loop end never loses its
/// staging reservation at all: the rows stay in the same unsealed pass, so the physical NoteOff
/// that arrives after the crossing closes the recorded note instead of being rejected as
/// `unmatched NoteOff`. Rejection left the note in the live pending set (the next NoteOn for that
/// pitch is dropped as a duplicate and every later off is rejected too) and the committed content
/// carried a NoteOn with no terminal off -- the reported "third overdub lengthens the note offs".
void test_successor_wrap_keeps_a_held_note_reservation_for_its_physical_note_off() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  clockManager.assignCurrentTickSilently(192);
  track.startOverdubbing(192);
  inputNote(64, 193, UINT32_MAX);
  inputNote(62, 200, 240);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(TrackCaptureStopTestAccess::liveNotes(track).size()));
  TEST_ASSERT_GREATER_THAN_UINT32(0u, static_cast<uint32_t>(TrackCaptureStopTestAccess::liveReservations(track)));
  TEST_ASSERT_FALSE(track.maybeCommitOverdubWrap(700, 40));
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(
      0u, static_cast<uint32_t>(TrackCaptureStopTestAccess::liveReservations(track)),
      "a deferred wrap must not reset the held note's reservation");
  // The physical NoteOff arrives after the crossing and must be admitted.
  clockManager.assignCurrentTickSilently(768 + 250);
  midiHandler.handleMidiMessage(midi::NoteOff, 1, 64, 0, SOURCE_USB);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      0u, static_cast<uint32_t>(TrackCaptureStopTestAccess::liveNotes(track).size()),
      "the post-crossing NoteOff must be admitted, not rejected as unmatched");
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(TrackCaptureStopTestAccess::liveReservations(track)));
  // The pass still in the producer now carries the complete pair: a NoteOn and its terminal off,
  // with no dangling NoteOn for the resolver to lengthen into the previous overdub's content.
  const auto& capture = loop.capture.store;
  bool hasOn = false;
  bool hasOff = false;
  for (size_t i = 0; i < capture.size(); ++i) {
    const MidiEvent& evt = capture.at(i);
    if (evt.channel != 1 || evt.data.noteData.note != 64) continue;
    if (evt.isNoteOn()) hasOn = true;
    if (evt.isNoteOff()) hasOff = true;
  }
  TEST_ASSERT_TRUE_MESSAGE(hasOn && hasOff,
      "the deferred pass must carry the held note's on and its terminal off");
  // Once the predecessor retires, the owed commit seals that pass exactly once.
  for (unsigned grain = 0; grain < 20000 && track.hasPendingDeferredOverdubStop(); ++grain)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  const size_t passesAfterPredecessor = loop.passes.overdubPasses.size();
  TEST_ASSERT_TRUE(track.maybeCommitOverdubWrap(40, 41));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(passesAfterPredecessor + 1),
                           static_cast<uint32_t>(loop.passes.overdubPasses.size()));
}

/// The deferred overdub-stop completion is the normal commit route for a stop during
/// OVERDUBBING (both destinations share this completion). It must request the stored-MIDI
/// verification dump exactly like the synchronous `commitCaptureForStop(OverdubStop*, ...)`
/// fallback, otherwise the SEVT rows are never emitted (`sevt-diag-stop` capture had zero
/// `stored_verify` markers despite a completed stop grain). The STOPPED destination cannot be
/// driven to completion natively (it calls `silenceTrackMidiOutput`), so the shared completion
/// is asserted on the PLAYING destination.
void test_deferred_overdub_stop_completion_queues_stored_verification() {
  Track& track = preparePlayingTrack();
  track.setDeferredStoredVerificationArmedForTest(true);
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_FALSE(track.deferredStoredVerificationPendingForTest());

  for (unsigned i = 0; i < 10000 && track.hasPendingDeferredOverdubStop(); ++i)
    TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_TRUE_MESSAGE(
      track.deferredStoredVerificationPendingForTest(),
      "a completed deferred overdub stop must queue the stored-MIDI verification dump");
  TEST_ASSERT_EQUAL_UINT32(1, track.deferredStoredVerificationOpportunityCountForTest());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_first_interval_uses_production_routes_and_freezes_at_punch_out);
  RUN_TEST(test_stop_descriptor_rotation_preserves_worker_borrow_and_reuses_retired_slots);
  RUN_TEST(test_missing_stop_descriptors_rejects_before_opening_capture);
  RUN_TEST(test_stop_ring_admits_at_tail_and_binds_worker_to_head);
  RUN_TEST(test_full_stop_ring_denies_admission_and_preserves_accepted_capture);
  RUN_TEST(test_seal_transfers_extent_and_successor_capture_preserves_it);
  RUN_TEST(test_read_source_yields_same_rows_before_and_after_sealing);
  RUN_TEST(test_sealed_extent_resolves_closure_note_id);
  RUN_TEST(test_sealed_range_borrow_release_is_owner_identified);
  RUN_TEST(test_stop_job_owns_pending_note_changes);
  RUN_TEST(test_pending_change_borrow_release_is_owner_identified);
  RUN_TEST(test_stop_job_owns_capture_preview);
  RUN_TEST(test_capture_preview_borrow_release_is_owner_identified);
  RUN_TEST(test_stop_job_owns_overdub_session_undo_baseline);
  RUN_TEST(test_deferred_stop_prepares_job_owned_undo_entry);
  RUN_TEST(test_stop_job_and_live_ingress_own_distinct_staging);
  RUN_TEST(test_overdub_shortens_record_pass_note_and_completes_stop);
  RUN_TEST(test_stop_copies_canonical_rows_without_mutating_sealed_raw_input);
  RUN_TEST(test_transport_stop_during_canonical_copy_preserves_accepted_rows);
  RUN_TEST(test_playback_reset_preserves_partial_copy_until_stopped_recovery);
  RUN_TEST(test_cache_reset_at_each_stop_phase_keeps_accepted_capture);
  RUN_TEST(test_empty_stop_runs_existing_worker_to_completion);
  RUN_TEST(test_populated_stop_runs_existing_worker_to_completion);
  RUN_TEST(test_completed_job_cannot_transition_a_newer_track_interval);
  RUN_TEST(test_job_note_retirement_does_not_clear_live_notes);
  RUN_TEST(test_deferred_stop_records_and_pins_its_causal_predecessor_source);
  RUN_TEST(test_stop_job_owns_held_notes_and_closes_original_slot);
  RUN_TEST(test_stop_job_denies_changed_geometry_before_closure_mutation);
  RUN_TEST(test_button_a_successor_stop_denied_until_deferred_role_available);
  RUN_TEST(test_successor_stop_denied_by_occupied_deferred_capacity_is_non_destructive);
  RUN_TEST(test_record_admission_denied_when_required_prewarm_fails);
  RUN_TEST(test_manager_reentry_with_a_held_same_pitch_note);
  RUN_TEST(test_track_reentry_does_not_depend_on_manager_guard);
  RUN_TEST(test_session_exit_receipts_are_per_interval);
  RUN_TEST(test_predecessor_cleanup_keeps_the_successor_session);
  RUN_TEST(test_successor_session_ends_with_its_own_retirement);
  RUN_TEST(test_superseded_predecessor_releases_only_its_own_receipt);
  RUN_TEST(test_successor_same_interval_transfers_exactly_once);
  RUN_TEST(test_start_request_while_successor_is_live_is_not_a_second_interval);
  RUN_TEST(test_successor_start_is_admitted_while_finalization_resources_are_busy);
  RUN_TEST(test_successor_start_keeps_the_predecessors_frozen_builders);
  RUN_TEST(test_successor_stop_is_denied_when_the_canonical_copy_target_is_busy);
  RUN_TEST(test_deferred_predecessor_completion_leaves_successor_interval_untouched);
  RUN_TEST(test_transport_stop_retarget_retires_a_midflight_playing_job);
  RUN_TEST(test_successor_wrap_defers_its_pass_commit_while_a_stop_job_is_pending);
  RUN_TEST(test_successor_wrap_keeps_the_predecessors_frozen_builders);
  RUN_TEST(test_successor_wrap_keeps_a_held_note_reservation_for_its_physical_note_off);
  RUN_TEST(test_redundant_stored_verification_queue_keeps_pending_dump);
  RUN_TEST(test_ineligible_stored_verification_queue_keeps_pending_dump);
  RUN_TEST(test_deferred_overdub_stop_completion_queues_stored_verification);
  return UNITY_END();
}
