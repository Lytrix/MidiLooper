//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Packet A (A1.7 / C5) and Packet C of `deferred-slot-restore-liveness`: the **real** blocking
// persistence drain driven with a restore in flight. Kept as its own suite because the drain pulls
// the persistence-side owners, which the load suite deliberately does not link.
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
#include "../test_storage_manager_load/load_harness_stubs.h"
#include "drain_harness_stubs.h"
#include "../test_storage_manager_load/load_and_restore_harness.h"
#include "../../src/TrackUndo.cpp"
#include "../../src/PersistenceSyncDrainBudget.cpp"
#include "../../src/StorageManager/PersistenceWorkQueue.cpp"
#include "../../src/StorageManager/DeferredSaveScheduler.cpp"
#include "../../src/PersistenceBudget.cpp"
#include "../../src/RevisionLoadPolicy.cpp"
#include "../../src/StorageManager/Internal.cpp"
#include "../../src/StorageManager/PersistenceAdmit.cpp"
#include "../../src/PersistenceFailurePolicy.cpp"
#include "../../src/StorageManager/RevisionLoad.cpp"
#include "../../src/StorageManager/RevisionCommit.cpp"
#include "../../src/StorageManager/MidPassChunkPersist.cpp"
#include "../../src/StorageManager/WallClockSdSync.cpp"
#include "../../src/StorageManager/PersistenceWorkItemPersist.cpp"
#include "../../src/StorageManager/SyncDrainBudget.cpp"
#include "../../src/PersistenceSchema.cpp"
#include "../../src/RevisionPackedBlob.cpp"
#include "../../src/SetRevisionCatalog.cpp"
#include "../../src/SavedSetCatalog.cpp"
#include "../../src/StorageManager/DeferredSaveJobStages.cpp"
#include "../../src/StorageManager/WorkspaceSave.cpp"
#include "../../src/StorageManager/RuntimeBundleFooter.cpp"
#include "../../src/StorageManager/CurrentSetBootLoad.cpp"
#include "../../src/StorageManager/LegacyMonolithLoad.cpp"
#include "../../src/StorageManager/SavedSetIo.cpp"
#include "../../src/Loop/LoopContentHistory.cpp"
#include "../../src/StorageManager/LoadedTrackStateApply.cpp"
#include "../../src/Track/TrackDisplayState.cpp"
#include "../../src/TrackManager/TrackManagerMidiLedFeedback.cpp"
#include "../../src/TrackManager/TrackManagerMemoryPressure.cpp"
#include "../../src/TrackManager/TrackManagerPlaybackPolicy.cpp"
#include "../../src/TrackManager/TrackManagerCaptureQueue.cpp"
// Capture-queue owner: supplies `hasQueuedRecordingTrack` and the capture-side `TrackManager`
// members. `TrackPlaybackHotPath.cpp` follows it deliberately: the capture-queue TU calls a
// `requestLoopSlotPersistAndSaveState` overload that only that TU declares, so without it the call is
// ambiguous. The physical-output boundary in it (`silenceTrackMidiOutput`) collides with the shared
// harness's stub, so it is included only for the declaration and its duplicate definition is avoided
// by not including it -- see the note in drain_harness_stubs.h.

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
// The runtime's own explicit-request / background-fill entry points live beside the SD boot load, not
// in the load-job TU: without them the suite could only reach the queue through internal helpers.
#include "../../src/StorageManager.cpp"
// Packet A's C5 fixture and Packet C both drive the **real blocking persistence drain**, so its TU is
// linked here rather than modelled by a test-only stand-in.
#include "../../src/StorageManager/SyncDrainSaveState.cpp"
// A3.5: the real drain now services MIDI while it waits, so the dispatch path it reaches must be linked.
#include "../../src/MidiButtonActions.cpp"
#include "../../src/Track/SlotClearPlaybackRestore.cpp"
#include "../../src/EditManager/EditSessionInteraction.cpp"
#include "../../src/EditManager/ResolveConstrainedGeometry.cpp"
// A3.4a/A3.5: the drain guard and the restore-request record both have their own translation units.
#include "../../src/StorageManager/PersistenceDrainGuard.cpp"
#include "../../src/StorageManager/RestoreRequestState.cpp"

// The MIDI-service path reaches the transport/clock owners and the note-edit geometry owners. None is on
// the asserted path (the fixture queues a transport message purely to observe that MIDI was serviced), so
// each fails closed rather than pretending to act.
/// The queued message is a **transport** message, so its callbacks are the observation of "MIDI was
/// serviced". They are counted, not fail-closed: the point of the case is that the message reaches them.
unsigned g_a35TransportServiceCalls = 0;
void ClockManager::onMidiStart() { ++g_a35TransportServiceCalls; }
void ClockManager::onMidiStop() { ++g_a35TransportServiceCalls; }
void ClockManager::onMidiClockPulse() { ++g_a35TransportServiceCalls; }
void ClockManager::requestTransitionTo(ClockSource) { ++g_a35TransportServiceCalls; }
uint32_t ClockManager::setLastMidiClockTime(uint32_t) {
  ++g_a35TransportServiceCalls;
  return 0;
}
/// `handleMidiInput` needs the clock's micros projection. `ClockManager.cpp` pulls the Teensy
/// `IntervalTimer`, which has no native stub, so the one accessor is supplied directly; it is a pure read
/// of the manager's own state.
uint32_t ClockManager::getMicrosPerTick() const { return microsPerTick; }
void TrackManager::handleTransportStop() { TEST_FAIL_MESSAGE(__PRETTY_FUNCTION__); }
// The MIDI dispatch route's own boundaries. The fixture queues a **transport** message, so these are not
// reached by the asserted path; they exist because `MidiButtonManager` is part of the button TU's closure.
void MidiButtonManager::handleMidiNote(uint8_t, uint8_t, uint8_t, bool) { TEST_FAIL_MESSAGE(__PRETTY_FUNCTION__); }
bool BarStepButtonHandler::isBarStepButtonNote(uint8_t, uint8_t) const {
  TEST_FAIL_MESSAGE(__PRETTY_FUNCTION__);
  return false;
}
// The MIDI dispatch path's global owners and the tempo global, which `StorageManager.cpp`'s TU would
// normally supply; this suite links the drain closure rather than the whole manager.
DisplayManager::DisplayManager() {}
MidiFaderManager::MidiFaderManager() {}
MidiFaderProcessor::MidiFaderProcessor() {}
MidiFaderActions::MidiFaderActions() {}
MidiButtonProcessor::MidiButtonProcessor() {}
MidiButtonManager::MidiButtonManager() {}
ControlSurfaceManager::ControlSurfaceManager() {}
void ControlSurfaceManager::onEditEvent(EditEvent) { TEST_FAIL_MESSAGE(__PRETTY_FUNCTION__); }
BarStepButtonHandler::BarStepButtonHandler() {}
MidiButtonManager midiButtonManager;
ControlSurfaceManager controlSurfaceManager;
DisplayManager displayManager;
BarStepButtonHandler barStepButtonHandler;
float bpm = 120.0f;
void BarStepButtonHandler::handleMidiNote(uint8_t, uint8_t, uint8_t, bool) { TEST_FAIL_MESSAGE(__PRETTY_FUNCTION__); }
void ControlSurfaceManager::handleMidiCC(uint8_t, uint8_t, uint8_t) { TEST_FAIL_MESSAGE(__PRETTY_FUNCTION__); }
void ControlSurfaceManager::handleMidiPitchbend(uint8_t, int16_t) { TEST_FAIL_MESSAGE(__PRETTY_FUNCTION__); }

void setUp() {}
void tearDown() {}

/// The suite currently links the drain TU and its measured closure but drives **no** production call,
/// because the remaining closure is larger than Packet A needs. A1.7 and Packet C re-enable the real
/// drain call here; the include list above is the measured closure recorded in `REVIEW.md`.
/// Behaviour-free closure proof: the boundary stubs and the closed import set compile and link
/// together. The **real** drain call is deliberately not retained here: A1.7 measured that doing so also
/// needs the display stack, which is a recorded scope decision (see `REVIEW.md` A1.7), not a fixture
/// mechanic.
void test_drain_suite_links_the_closed_import_set() {
  TEST_ASSERT_TRUE(StorageManagerInternal::pendingLoopSlotRestoreCount() == 0);
}

// ---------------------------------------------------------------------------
// Packet C / A3.5 — the finite post-load commit holdoff is **reported, not waited out**.
//
// Restructured for the MIDI-service topology: the drain runs on the interval path, and the timing
// contract puts every `midiHandler.handleMidiInput()` call in `main.cpp` (nine of them). A drain that
// blocked on the holdoff and serviced MIDI would move an input drain into the interval, so the drain
// now returns the named `post_load_holdoff` immediately: nothing is discarded, the owed save survives,
// and the holdoff expires on its own deadline while `main` keeps servicing MIDI. The holdoff is armed
// through the documented controllable input (the production setter is `cleanupLoadLoopJobPublish`).
// ---------------------------------------------------------------------------

/// **A3.5 (restructured):** with the holdoff armed, the real drain refuses **by name** without burning
/// its stuck budget, without servicing MIDI, and without discarding the restore or the owed save.
void test_a35_drain_reports_the_holdoff_by_name_instead_of_waiting_it_out() {
  SD.reset();
  StorageManagerInternal::armPostLoadCommitSaveHoldoff(LoadLoopBudget::PostLoadCommitSaveHoldoffMs);
  TEST_ASSERT_TRUE_MESSAGE(StorageManagerInternal::postLoadCommitSaveHoldoffRemainingMs() > 0u,
                           "the fixture must arm the real holdoff");
  TEST_ASSERT_TRUE(StorageManagerInternal::deferredSaveBlockedByPostLoadCommitHoldoff());

  // A queued MIDI message must **survive** the drain: the drain is not a MIDI-service point any more.
  Serial8.clearInbox();
  g_a35TransportServiceCalls = 0;
  Serial8.pushMessage(midi::Start, MidiConfig::Channels::SELECT, 0, 0);

  const SyncDrainResult result =
      StorageManagerInternal::drainPersistenceWorkBlockingDetailed(looperState.getLooperState());

  char message[256];
  snprintf(message, sizeof(message),
           "the drain must name the finite post-load holdoff (outcome=%d reason=%s inbox=%u calls=%u)",
           static_cast<int>(result.outcome), result.reason, (unsigned)Serial8.inbox.size(),
           g_a35TransportServiceCalls);
  TEST_ASSERT_TRUE_MESSAGE(strcmp(result.reason, "post_load_holdoff") == 0, message);
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(SyncDrainOutcome::TemporarilyBlocked),
                                static_cast<int>(result.outcome), message);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, static_cast<uint32_t>(Serial8.inbox.size()), message);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, g_a35TransportServiceCalls, message);
  TEST_ASSERT_TRUE_MESSAGE(StorageManagerInternal::deferredSaveBlockedByPostLoadCommitHoldoff(),
                           "the holdoff must still be armed: the drain preserved the wait, not consumed it");
}

/// The remaining-time reader is **read-only**: asking how long the holdoff has left must not clear it.
/// The scheduler's predicate clears an expired holdoff as a side effect, which is right for the scheduler
/// and wrong for a caller that wants to know the state.
void test_a35_holdoff_remaining_reader_does_not_clear_the_holdoff() {
  StorageManagerInternal::armPostLoadCommitSaveHoldoff(50u);
  TEST_ASSERT_EQUAL_UINT32(50u,
                           StorageManagerInternal::postLoadCommitSaveHoldoffRemainingMs());
  TEST_ASSERT_TRUE_MESSAGE(StorageManagerInternal::deferredSaveBlockedByPostLoadCommitHoldoff(),
                           "the scheduler predicate must still see the holdoff after a read");
}

// ---------------------------------------------------------------------------
// Packet C / A3.6 — preserve the DEC-081 and capture behavior.
//
// These are **regression** cases: A3.4a's guard and A3.5's wait must not have changed who gates
// persistence. `isCaptureRecordingForPersistence()` is the only persistence gate (DEC-081: audible
// playback is not one), and the deferred-save scheduler's recording branch keeps the multi-frame
// workspace/bundle writers parked while still running the one-chunk-per-turn mid-pass flush.
// ---------------------------------------------------------------------------

/// **DEC-081:** reaching the capture gate needs *recording*, not audible playback. A playing track with a
/// save owed must not be classified `BlockedByCapture`, and the scheduler must not take the recording
/// early-return for it.
void test_a36_audible_playback_is_not_a_persistence_gate() {
  Track& track = trackManager.getTrack(0);
  track.forceSetState(TRACK_PLAYING);
  TEST_ASSERT_TRUE_MESSAGE(track.isAudiblePlaybackActive(), "the fixture must leave the track audibly playing");
  TEST_ASSERT_TRUE_MESSAGE(track.isPlaying(), "the fixture must leave the track playing");
  TEST_ASSERT_FALSE_MESSAGE(StorageManagerInternal::isCaptureRecordingForPersistence(),
                            "audible playback alone must never satisfy the capture gate");
  // The pure classifier agrees: with playback and no capture, no gate denies the save.
  const PersistenceSyncDrainBudget::SyncDrainVerdict verdict =
      PersistenceSyncDrainBudget::classifySyncDrainVerdict(
          StorageManagerInternal::isCaptureRecordingForPersistence(), false, false,
          SyncDrainWait::kNone);
  TEST_ASSERT_TRUE_MESSAGE(verdict.reason == SyncDrainFailureReason::Stuck,
                           "with playback and no gate, the classifier must not name a denial");
  TEST_ASSERT_NOT_EQUAL_MESSAGE(static_cast<int>(SyncDrainFailureReason::BlockedByCapture),
                                static_cast<int>(verdict.reason),
                                "audible playback must not read as capture");
}

/// **A recording track parks the multi-frame writers:** the scheduler returns before it can queue or
/// advance workspace/bundle work, so a save owed during a take stays owed and no multi-frame write runs.
void test_a36_a_recording_track_parks_the_multi_frame_writers() {
  Track& track = trackManager.getTrack(0);
  track.forceSetState(TRACK_RECORDING);
  TEST_ASSERT_TRUE_MESSAGE(StorageManagerInternal::isCaptureRecordingForPersistence(),
                           "a recording track is the capture gate");
  storageSession.currentWorkspaceSave.pending = true;
  const uint16_t queueBefore = PersistenceWorkQueue::queueDepth();

  StorageManager::processDeferredSaveState(looperState.getLooperState());

  TEST_ASSERT_EQUAL_UINT16_MESSAGE(queueBefore,
                                   PersistenceWorkQueue::queueDepth(),
                                   "no multi-frame persistence work may be queued during a take");
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(0u,
                                   PersistenceWorkQueue::writingWorkItemCount(),
                                   "no multi-frame persistence work may be writing during a take");
  TEST_ASSERT_TRUE_MESSAGE(storageSession.currentWorkspaceSave.pending,
                           "the owed save must stay owed during a take");
}

/// **The mid-pass one-chunk flush runs during a take.** The recording branch is an early return, so the
/// flush is the *only* persistence that runs: with a sealed chunk queued it is serviced (any movement in
/// the chunk queue/state), while a take with nothing sealed leaves both paths untouched.
void test_a36_the_mid_pass_one_chunk_flush_runs_during_a_take() {
  SD.reset();
  Track& track = trackManager.getTrack(0);
  track.forceSetState(TRACK_RECORDING);
  TEST_ASSERT_TRUE(StorageManagerInternal::isCaptureRecordingForPersistence());

  // Arm a MIDI message the flush's round trip must **not** consume: the recording branch returns before
  // the MIDI-servicing holdoff wait, so this is the negative control for A3.5's service path.
  Serial8.clearInbox();
  g_a35TransportServiceCalls = 0;
  Serial8.pushMessage(midi::Start, MidiConfig::Channels::SELECT, 0, 0);

  // A real sealed chunk in the mid-pass queue, so the flush has something to service.
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;
  for (uint16_t i = 0; i < LoopEventStoreConfig::CHUNK_CAPACITY + 1; ++i) {
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(static_cast<uint32_t>(i), 1, 60, 100)));
  }
  CaptureChunkIdList refs;
  store.detachChunksTo(refs);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0u, static_cast<uint32_t>(PersistenceQueue::queueDepth()),
                                          "the fixture must admit at least one sealed chunk");
  const uint16_t chunkQueueBefore = PersistenceQueue::queueDepth();
  storageSession.currentWorkspaceSave.pending = true;

  StorageManager::processDeferredSaveState(looperState.getLooperState());

  // The take path runs the flush, not the multi-frame writers, and it does not wait on a holdoff.
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(0u,
                                   PersistenceWorkQueue::writingWorkItemCount(),
                                   "the take path must not run a multi-frame write");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, g_a35TransportServiceCalls,
                                   "the take path must not service MIDI through the holdoff wait");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, static_cast<uint32_t>(Serial8.inbox.size()),
                                   "the queued message must survive the take path untouched");
  // The flush was reached and did work: at least one queued chunk left the queued state (written, or
  // handed to the writer). Without the recording branch's flush this queue would be untouched.
  TEST_ASSERT_TRUE_MESSAGE(
      PersistenceQueue::queueDepth() < chunkQueueBefore ||
          PersistenceQueue::writingChunkCount() > 0u,
      "the mid-pass flush must service a queued chunk during a take");
}

// ---------------------------------------------------------------------------
// S2b.7 — a restore that holds the SD session at the drain edge.
//
// The question this closes: does the blocking drain **advance** a restore it waits on, or does it
// **name the wait**? The archived `deferred-slot-restore-liveness` deferred the advancement claim, and
// the A3.7 shape-(b) device run showed the drain completing through the urgent-save priority bypass
// (`shouldPrioritizeWorkspaceSaveDrain()`) -- which advances the *save*, not the restore. This fixture
// measures the plain case: another track audibly playing, a restore mid-preparation holding the SD
// session (so `deferredSaveBlockedByActiveSlotLoadSd()` is true), and a save owed. It asserts the named
// wait and the **absence** of advancement, then proves with a control that the same drain completes once
// the restore releases the session -- so "nothing moved" is the gate's doing, not a fail-closed route.
// The commit gate is not relaxed here (`resumable-midi-boundary-playback-maintenance` task 3.3).
// ---------------------------------------------------------------------------

/// **S2b.7:** a session-holding restore is a **named wait**, not a failure, and the drain does not
/// advance it.
void test_s2b7_a_session_holding_restore_is_a_named_wait_and_is_not_advanced() {
  SD.reset();
  RestoreHarness::resetAllTracksAndPool();
  StorageManagerInternal::resetAllRestoreRequestState();
  RestoreHarness::setRuntimeTransportIdle();
  // Another track plays: DEC-081 says audible playback is not a persistence gate, so this must not be
  // what blocks the save.
  RestoreHarness::setTrackAudiblyPlaying(0);
  RestoreHarness::armBackgroundRestoreRequest(2, 3, 70);
  RestoreHarness::runLoadServiceTurn();

  const bool sessionHeld = StorageManagerInternal::deferredSaveBlockedByActiveSlotLoadSd();
  const uint32_t progressBefore = StorageManagerInternal::restoreProgressCounter();
  const uint8_t phaseBefore = StorageManagerInternal::restoreWorkPhase();
  storageSession.currentWorkspaceSave.pending = true;

  const SyncDrainResult result =
      StorageManagerInternal::drainPersistenceWorkBlockingDetailed(looperState.getLooperState());

  const uint32_t progressAfter = StorageManagerInternal::restoreProgressCounter();
  const uint8_t phaseAfter = StorageManagerInternal::restoreWorkPhase();
  char message[420];
  snprintf(message, sizeof(message),
           "held=%d outcome=%d reason=%s progress=%u->%u phase=%u->%u save_owed=%d",
           sessionHeld ? 1 : 0, static_cast<int>(result.outcome), result.reason,
           static_cast<unsigned>(progressBefore), static_cast<unsigned>(progressAfter),
           static_cast<unsigned>(phaseBefore), static_cast<unsigned>(phaseAfter),
           storageSession.currentWorkspaceSave.pending ? 1 : 0);

  TEST_ASSERT_TRUE_MESSAGE(sessionHeld, "the fixture must hold the SD session with a restore");
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(SyncDrainOutcome::TemporarilyBlocked),
                                static_cast<int>(result.outcome),
                                "a session-holding restore is a temporary block, never a failure");
  TEST_ASSERT_EQUAL_STRING_MESSAGE("slot_load_active", result.reason,
                                   "the drain must name the gate it waits on, not report `unexplained`");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      progressBefore, progressAfter,
      "S2b.7: the drain must not be claimed to advance the restore -- progress must NOT move");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(phaseBefore),
                                   static_cast<uint32_t>(phaseAfter),
                                   "S2b.7: the in-flight restore's phase must NOT move");
  TEST_ASSERT_TRUE_MESSAGE(storageSession.currentWorkspaceSave.pending,
                           "the owed save must stay owed: a temporary block discards nothing");
  TEST_ASSERT_TRUE_MESSAGE(StorageManagerInternal::restoreRequestRecordExists(2, 3),
                           "the request must be preserved, not decided, by the drain");
}

/// **S2b.7 control:** once the restore releases the SD session, the *same* drain completes and the owed
/// save is written. This is what makes the block above the gate's doing rather than a fail-closed route.
void test_s2b7_control_the_drain_completes_once_the_restore_releases_the_session() {
  SD.reset();
  RestoreHarness::resetAllTracksAndPool();
  StorageManagerInternal::resetAllRestoreRequestState();
  RestoreHarness::setRuntimeTransportIdle();
  RestoreHarness::armBackgroundRestoreRequest(4, 1, 72);

  // Hold the SD session, then let the restore run to its terminal.
  RestoreHarness::runLoadServiceTurn();
  TEST_ASSERT_TRUE_MESSAGE(StorageManagerInternal::deferredSaveBlockedByActiveSlotLoadSd(),
                           "the control needs the restore to hold the SD session first");
  uint32_t turns = 0;
  while (StorageManagerInternal::deferredSaveBlockedByActiveSlotLoadSd() && turns < 64) {
    RestoreHarness::runLoadServiceTurn();
    ++turns;
  }
  TEST_ASSERT_FALSE_MESSAGE(StorageManagerInternal::deferredSaveBlockedByActiveSlotLoadSd(),
                            "the restore must release the SD session within the bounded control wait");

  // The commit that just landed arms the finite post-load holdoff (A3.5), which the drain reports by
  // name rather than waiting out. Advance the fixture clock past it so the control reaches the save.
  nativeMillisAdvance(LoadLoopBudget::PostLoadCommitSaveHoldoffMs + 1u);

  storageSession.currentWorkspaceSave.pending = true;
  const SyncDrainResult result =
      StorageManagerInternal::drainPersistenceWorkBlockingDetailed(looperState.getLooperState());
  char message[256];
  snprintf(message, sizeof(message), "control outcome=%d reason=%s save_owed=%d",
           static_cast<int>(result.outcome), result.reason,
           storageSession.currentWorkspaceSave.pending ? 1 : 0);
  // The gate under test is the slot-load wait, so the control asserts that the *same* drain no longer
  // stops there and does discharge the owed save. It deliberately does not assert `Completed`: this
  // minimal fixture has no valid workspace to write, so the drain's own `lastCompletedOk` check reports
  // `Failed` for a reason unrelated to the restore gate (the full save path has its own coverage).
  TEST_ASSERT_FALSE_MESSAGE(storageSession.currentWorkspaceSave.pending,
                            "the control drain must discharge the owed save once the SD session is free");
  TEST_ASSERT_TRUE_MESSAGE(strcmp(result.reason, "slot_load_active") != 0,
                           "the released restore must no longer be the named gate");
  TEST_ASSERT_FALSE_MESSAGE(strcmp(result.reason, "unexplained") == 0, message);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_drain_suite_links_the_closed_import_set);
  RUN_TEST(test_a35_drain_reports_the_holdoff_by_name_instead_of_waiting_it_out);
  RUN_TEST(test_a35_holdoff_remaining_reader_does_not_clear_the_holdoff);
  // A3.6 — preserve DEC-081 and the capture behavior.
  RUN_TEST(test_a36_audible_playback_is_not_a_persistence_gate);
  RUN_TEST(test_a36_a_recording_track_parks_the_multi_frame_writers);
  RUN_TEST(test_a36_the_mid_pass_one_chunk_flush_runs_during_a_take);
  // S2b.7 — a session-holding restore is a named wait, not an advancement.
  RUN_TEST(test_s2b7_a_session_holding_restore_is_a_named_wait_and_is_not_advanced);
  RUN_TEST(test_s2b7_control_the_drain_completes_once_the_restore_releases_the_session);
  return UNITY_END();
}
