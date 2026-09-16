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
#include "../../src/Track/TrackDeferredMaintenance.cpp"
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
  static auto generation(const Track& track) { return track.playbackGeneration; }
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
  static bool appendNoteOffAtPhase(Track& track, uint8_t channel, uint8_t note, uint32_t phase) {
    return track.appendCaptureNoteOffAtPhase(channel, note, phase);
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

void test_button_a_three_intervals_accept_notes_with_service_paused() {
  Track& track = preparePlayingTrack();
  bool accepted[3]{};
  bool exactInput[3]{};
  for (unsigned i = 0; i < 3; ++i) {
    const uint32_t punch = 96 + i * 96;
    const uint8_t pitch = 60 + i * 2;
    clockManager.assignCurrentTickSilently(punch);
    midiButtonActions.handleToggleRecord();
    accepted[i] = track.isOverdubbing();
    inputNote(pitch, punch + 1, punch + 24);
    const auto& capture = track.getActiveLoop().capture.store;
    exactInput[i] = capture.size() == 2 && capture.at(0).tick == punch + 1 &&
        capture.at(1).tick == punch + 24 && capture.at(0).data.noteData.note == pitch;
    clockManager.assignCurrentTickSilently(punch + 48);
    midiButtonActions.handleToggleRecord();
    TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
    // Deliberately no deferred maintenance, playback preparation or stop grains.
  }
  TEST_ASSERT_TRUE(accepted[0] && exactInput[0]);
  TEST_ASSERT_TRUE_MESSAGE(accepted[1] && exactInput[1],
      "B must capture its original input ticks while A has received zero worker turns");
  TEST_ASSERT_TRUE_MESSAGE(accepted[2] && exactInput[2],
      "C must capture independently while A and B remain queued");
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
  RUN_TEST(test_stop_job_owns_held_notes_and_closes_original_slot);
  RUN_TEST(test_stop_job_denies_changed_geometry_before_closure_mutation);
  RUN_TEST(test_button_a_three_intervals_accept_notes_with_service_paused);
  RUN_TEST(test_manager_reentry_with_a_held_same_pitch_note);
  RUN_TEST(test_track_reentry_does_not_depend_on_manager_guard);
  RUN_TEST(test_redundant_stored_verification_queue_keeps_pending_dump);
  RUN_TEST(test_ineligible_stored_verification_queue_keeps_pending_dump);
  RUN_TEST(test_deferred_overdub_stop_completion_queues_stored_verification);
  return UNITY_END();
}
