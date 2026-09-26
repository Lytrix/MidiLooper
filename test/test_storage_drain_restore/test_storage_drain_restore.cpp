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
#define HARNESS_LINKS_REAL_STOP_TELEMETRY
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
#include "../../src/StorageManager/RuntimeBundleRecovery.cpp"
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
#ifdef RECOVERY_TEENSY_ABI
#include "RecoveryStorageLoopIo.cpp"
#else
#include "../../src/StorageLoopIo.cpp"
#endif
#ifdef RECOVERY_TEENSY_ABI
#include "RecoveryStorageLoopIoBodyFormatV1.cpp"
#else
#include "../../src/StorageLoopIoBodyFormatV1.cpp"
#endif
#include "../../src/StorageManager/CurrentWorkspaceStorage.cpp"
#include "../../src/StorageManager/LoadLoopSelectionPolicy.cpp"
#include "../../src/StorageManager/SlotLoadAdmission.cpp"
#include "../../src/StorageManager/SlotLoadSession.cpp"
#include "../../src/StorageManager/LoadLoopJob.cpp"
#include "../../src/Loop/LoopPendingNoteChange.cpp"
#include "../../src/TrackManager/TrackManagerSlotMatrix.cpp"
// S4.1 persistence-identity fixture: the production slot-keyed persistence admission
// (`requestLoopSlotPersistAndSaveState`) and the stop-telemetry identity resolution live in this TU, so
// the fixture admits persistence through the same entry a capture stop uses rather than restating it.
#include "../../src/Track/TrackStopTelemetryColdHelpers.cpp"
// S4.1 persistence-identity fixture: the slot move's enabled-set helper, so the focus move's own
// selection step is the production one rather than a hand-poked flag.
#include "../../src/TrackManager/TrackManagerInternalHelpers.cpp"
#include "../../src/StorageManager/LoopSlotRestoreQueue.cpp"
// The runtime's own explicit-request / background-fill entry points live beside the SD boot load, not
// in the load-job TU: without them the suite could only reach the queue through internal helpers.
#include "../../src/StorageManager.cpp"
// Packet A's C5 fixture and Packet C both drive the **real blocking persistence drain**, so its TU is
// linked here rather than modelled by a test-only stand-in.
#include "../../src/StorageManager/SyncDrainSaveState.cpp"
// S2b.7: the drain now services the restore it waits on, through the same scheduler entry the deferred
// load frame uses, so that entry must be linked for the real drain to resolve.
#include "../../src/DeferredJobScheduler.cpp"
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
// S2b.7 / D4 — the blocking drain **advances** the restore it waits on.
//
// The archived `deferred-slot-restore-liveness` deferred this claim: the drain loop ran only
// `processDeferredSaveState`, and the main loop is *inside* the drain, so a restore holding the SD
// session could never release it. The earlier fixture in this file recorded the gap (the drain returned
// `slot_load_active` with restore progress and phase unchanged, nothing advanced). These cases now
// assert the delivered behavior and its three contract shapes from D4 / archived A3.1's verify list:
// another track playing (named commit-gate wait, nothing published), the gate admitting (completion),
// and a **parked** instance holding the session (serviced rather than stranded).
// The commit gate is never relaxed: `resumable-midi-boundary-playback-maintenance` task 3.3 owns that.
// ---------------------------------------------------------------------------

/// Order-independent reset for the S2b.7 cases. The load-job instances, the deferred-restore queue, the
/// request records and the boot background-fill scan all outlive `resetAllTracksAndPool()`, and a
/// completed fill arms a 2 s `armBackgroundRestoreHoldoff` that would otherwise block a later case's
/// non-focus request.
void resetLoadSubsystemForS2b7() {
  SD.reset();
  RestoreHarness::resetAllTracksAndPool();
  StorageManagerInternal::clearLoadLoopJob();
  StorageManagerInternal::clearPendingLoopSlotRestoresAtBoot();
  StorageManagerInternal::resetAllRestoreRequestState();
  StorageManagerInternal::armBackgroundRestoreHoldoff(0);
}

/// **S2b.7 (D4 point 3, commit-gate shape):** another track audibly plays, so the global commit gate
/// denies. The drain must still **advance** the restore's preparation, then report the request's own
/// named wait -- and it must publish nothing and preserve both the request and the owed save.
void test_s2b7_the_drain_advances_a_session_holding_restore_and_names_the_commit_gate() {
  resetLoadSubsystemForS2b7();
  RestoreHarness::setRuntimeTransportIdle();
  // Another track plays: DEC-081 says audible playback is not a persistence gate, so this must not be
  // what blocks the save -- it is what makes the restore's **commit** the gated step.
  RestoreHarness::setTrackAudiblyPlaying(0);
  RestoreHarness::armBackgroundRestoreRequest(2, 3, 70);
  RestoreHarness::runLoadServiceTurn();

  const bool sessionHeld = StorageManagerInternal::deferredSaveBlockedByActiveSlotLoadSd();
  const uint32_t progressBefore = StorageManagerInternal::restoreProgressCounter();
  storageSession.currentWorkspaceSave.pending = true;

  const SyncDrainResult result =
      StorageManagerInternal::drainPersistenceWorkBlockingDetailed(looperState.getLooperState());

  const uint32_t progressAfter = StorageManagerInternal::restoreProgressCounter();
  char message[320];
  snprintf(message, sizeof(message), "held=%d outcome=%d reason=%s progress=%u->%u save_owed=%d",
           sessionHeld ? 1 : 0, static_cast<int>(result.outcome), result.reason,
           static_cast<unsigned>(progressBefore), static_cast<unsigned>(progressAfter),
           storageSession.currentWorkspaceSave.pending ? 1 : 0);

  TEST_ASSERT_TRUE_MESSAGE(sessionHeld, "the fixture must hold the SD session with a restore");
  TEST_ASSERT_GREATER_THAN_MESSAGE(
      progressBefore, progressAfter,
      "S2b.7: the drain must ADVANCE the restore it waits on, not only report it");
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(SyncDrainOutcome::TemporarilyBlocked),
                                static_cast<int>(result.outcome), message);
  TEST_ASSERT_EQUAL_STRING_MESSAGE(
      "waiting_commit_gate", result.reason,
      "the drain must report the restore's own named commit-gate wait, not the generic slot-load gate");
  TEST_ASSERT_FALSE_MESSAGE(trackManager.getTrack(2).getLoop(3).hasCommittedPasses(),
                            "the commit gate must NOT be bypassed: nothing may be published");
  TEST_ASSERT_TRUE_MESSAGE(storageSession.currentWorkspaceSave.pending,
                           "the owed save must stay owed: the block discards nothing");
  TEST_ASSERT_EQUAL_STRING_MESSAGE(
      "waiting",
      StorageManagerInternal::restoreRequestStateName(
          StorageManagerInternal::restoreRequestState(2, 3)),
      "the request must be preserved in its named wait, not decided");
  TEST_ASSERT_EQUAL_STRING_MESSAGE(
      "none",
      StorageManagerInternal::restoreRequestTerminalName(
          StorageManagerInternal::restoreRequestTerminalFor(2, 3)),
      "no terminal may be recorded for a request that is only waiting");
}

/// **S2b.7 (D4 point 3, gate-admits shape):** with nothing timing-critical, the drain advances the
/// restore to its **committed** terminal with **no external advancement** between the assertions, and
/// then discharges the owed save. This is the case the archived A3.1 verify list requires.
///
/// The restore is armed for the **focused** slot on purpose: with the transport idle but the job on a
/// non-focus slot, focus policy parks it before it can be stepped and the urgent-save priority discharges
/// the save without advancing the restore -- correct for the save, but not the advancement case. Focused,
/// the drain itself must complete it.
void test_s2b7_the_drain_advances_the_restore_to_completion_where_the_gate_admits() {
  resetLoadSubsystemForS2b7();
  RestoreHarness::setRuntimeTransportIdle();
  const uint8_t focusTrack = trackManager.getSelectedTrackIndex();
  const uint8_t focusSlot = trackManager.getSelectedSlotIndex(focusTrack);
  RestoreHarness::armBackgroundRestoreRequest(focusTrack, focusSlot, 72);
  // Admit the job so it holds the SD session. After this the drain -- not the fixture -- must do the work.
  RestoreHarness::runLoadServiceTurn();
  const bool sessionHeld = StorageManagerInternal::deferredSaveBlockedByActiveSlotLoadSd();
  const uint32_t progressBefore = StorageManagerInternal::restoreProgressCounter();
  storageSession.currentWorkspaceSave.pending = true;

  (void)StorageManagerInternal::drainPersistenceWorkBlockingDetailed(looperState.getLooperState());
  // The commit arms the finite post-load holdoff, which the drain reports by name instead of waiting it
  // out; move the fixture clock past it so the second drain can reach the save.
  nativeMillisAdvance(LoadLoopBudget::PostLoadCommitSaveHoldoffMs + 1u);
  (void)StorageManagerInternal::drainPersistenceWorkBlockingDetailed(looperState.getLooperState());

  char message[256];
  snprintf(message, sizeof(message), "held=%d progress=%u->%u committed=%d save_owed=%d",
           sessionHeld ? 1 : 0, static_cast<unsigned>(progressBefore),
           static_cast<unsigned>(StorageManagerInternal::restoreProgressCounter()),
           trackManager.getTrack(focusTrack).getLoop(focusSlot).hasCommittedPasses() ? 1 : 0,
           storageSession.currentWorkspaceSave.pending ? 1 : 0);
  TEST_ASSERT_TRUE_MESSAGE(sessionHeld, "the fixture must hold the SD session with a restore");
  // The commit/cleanup steps do not bump the preparation progress counter (only the preparation grains
  // do), so completion is asserted by the terminal state itself, which is the stronger evidence.
  TEST_ASSERT_TRUE_MESSAGE(trackManager.getTrack(focusTrack).getLoop(focusSlot).hasCommittedPasses(),
                           message);
  TEST_ASSERT_FALSE_MESSAGE(storageSession.currentWorkspaceSave.pending,
                            "the drain must then discharge the owed save it was blocked on");
}

/// **S2b.7 (D4 point 1, parked instance):** a parked job keeps its session, so the drain's wait can
/// outlive the active instance. With the session held by the **parked** instance alone the drain must still
/// service it rather than leave it stranded.
///
/// **DEC-091 changed what this case can observe, and the replacement is the direct form of the same claim.**
/// The old oracle was the restore-progress counter advancing, which needed the drain to loop many times --
/// and it looped only because the owed save was re-queued at the head after every failed completion. That
/// failure policy is gone (a failed completion is discharged), so the drain now services the parked
/// instance for the single frame its episode has, and that frame performs the **ownership transfer**:
/// asserted here by probe (the parked instance becomes active and owns the request) instead of by a counter
/// that a second frame would have moved. The request must still be owed afterwards -- serviced, not
/// consumed or dropped.
void test_s2b7_the_drain_services_a_parked_instance_holding_the_session() {
  resetLoadSubsystemForS2b7();
  RestoreHarness::setRuntimeTransportIdle();
  RestoreHarness::setTrackAudiblyPlaying(0);
  RestoreHarness::armBackgroundRestoreRequest(3, 2, 66);
  RestoreHarness::runLoadServiceTurn();
  // Force the stall shape D4 point 1 names: the only live instance is the parked one.
  StorageManagerInternal::parkActiveLoadLoopJob();
  const bool sessionHeld = StorageManagerInternal::deferredSaveBlockedByActiveSlotLoadSd();
  bool parkedBefore = false;
  uint8_t parkedBeforeTrack = 0, parkedBeforeSlot = 0, parkedBeforePhase = 0;
  StorageManager::probeParkedLoadLoopJob(parkedBefore, parkedBeforeTrack, parkedBeforeSlot,
                                         parkedBeforePhase);
  storageSession.currentWorkspaceSave.pending = true;

  const SyncDrainResult result =
      StorageManagerInternal::drainPersistenceWorkBlockingDetailed(looperState.getLooperState());

  bool activeAfter = false;
  uint8_t activeTrack = 0, activeSlot = 0, activePhase = 0, activeFocus = 0;
  StorageManager::probeActiveLoadLoopJob(activeAfter, activeTrack, activeSlot, activePhase,
                                         activeFocus);
  bool parkedAfter = false;
  uint8_t parkedTrack = 0, parkedSlot = 0, parkedPhase = 0;
  StorageManager::probeParkedLoadLoopJob(parkedAfter, parkedTrack, parkedSlot, parkedPhase);

  char transferMessage[320];
  snprintf(transferMessage, sizeof(transferMessage),
           "before parked=%d %u/%u; after active=%d %u/%u ph=%u parked=%d %u/%u",
           parkedBefore ? 1 : 0, static_cast<unsigned>(parkedBeforeTrack),
           static_cast<unsigned>(parkedBeforeSlot), activeAfter ? 1 : 0,
           static_cast<unsigned>(activeTrack), static_cast<unsigned>(activeSlot),
           static_cast<unsigned>(activePhase), parkedAfter ? 1 : 0,
           static_cast<unsigned>(parkedTrack), static_cast<unsigned>(parkedSlot));

  char message[512];
  snprintf(message, sizeof(message),
           "held=%d active=%d any=%d outcome=%d reason=%s save_owed=%d | %s", sessionHeld ? 1 : 0,
           StorageManagerInternal::isActiveLoadLoopJobRunning() ? 1 : 0,
           StorageManagerInternal::anyLoadLoopJobActive() ? 1 : 0,
           static_cast<int>(result.outcome), result.reason,
           storageSession.currentWorkspaceSave.pending ? 1 : 0, transferMessage);
  char diag[512];
  snprintf(diag, sizeof(diag),
           "held=%d outcome=%d reason=%s committed=%d save_owed=%d owned=%d",
           sessionHeld ? 1 : 0, static_cast<int>(result.outcome), result.reason,
           trackManager.getTrack(3).getLoop(2).hasCommittedPasses() ? 1 : 0,
           storageSession.currentWorkspaceSave.pending ? 1 : 0,
           StorageManagerInternal::anyLoadLoopJobActive() ? 1 : 0);

  TEST_ASSERT_TRUE_MESSAGE(sessionHeld, "the parked instance must be holding the SD session");
  TEST_ASSERT_TRUE_MESSAGE(parkedBefore && parkedBeforeTrack == 3 && parkedBeforeSlot == 2,
                           transferMessage);
  // The drain serviced it: the parked instance was promoted and now owns the request.
  TEST_ASSERT_FALSE_MESSAGE(parkedAfter,
                            "the drain must take the parked instance into service, not leave it parked");
  TEST_ASSERT_TRUE_MESSAGE(activeAfter && activeTrack == 3 && activeSlot == 2, transferMessage);
  // Serviced does not mean consumed: the request is still owed, and it published nothing while the global
  // commit gate (another track playing) denies.
  TEST_ASSERT_TRUE_MESSAGE(StorageManagerInternal::anyLoadLoopJobActive(),
                           "the drain must leave the request owned, not dropped");
  TEST_ASSERT_FALSE_MESSAGE(trackManager.getTrack(3).getLoop(2).hasCommittedPasses(),
                            "the commit gate must not be bypassed for a parked request either");
  // **The verdict is `Failed`, not `TemporarilyBlocked/waiting_commit_gate`, and DEC-091 is why.** This
  // minimal fixture has no valid workspace to complete, so the owed save genuinely fails; previously the
  // drain never reached that step -- it spun on the re-queued completion and broke out on the commit gate
  // first, reporting the gate's wait. A failed save must be reported as failed rather than masked by the
  // restore's wait (the *blocked* shape keeps its named wait in the active-instance sibling above, which
  // still passes), so this case pins the corrected priority.
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(SyncDrainOutcome::Failed),
                                static_cast<int>(result.outcome), message);
  TEST_ASSERT_FALSE_MESSAGE(storageSession.currentWorkspaceSave.lastCompletedOk,
                            "the drain must not report success for a save it could not complete");
  TEST_ASSERT_EQUAL_STRING_MESSAGE("none", result.reason,
                                   "the failed save is the outcome; no wait masks it");
  TEST_ASSERT_FALSE_MESSAGE(storageSession.currentWorkspaceSave.pending,
                            "the failed completion must be discharged, not left owed");
}

/// **S2b.7 control:** once the restore releases the SD session, the *same* drain is no longer gated by
/// the slot load and discharges the owed save. This keeps "the restore is the gate" falsifiable.
void test_s2b7_control_the_drain_completes_once_the_restore_releases_the_session() {
  resetLoadSubsystemForS2b7();
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

// ---------------------------------------------------------------------------
// S4.1 — persistence identity during handoff: what the workspace footer records, and **when**.
//
// S4.1's release acceptance is the A/B/C device run; this section closes the half of S4.1 that is a
// **persistence** question rather than a release one (the `DeferredSaveJobStages` footer read carried in
// the task's remainder, and [NEXT-STEPS.md](../../openspec/changes/runtime-working-set-ownership/NEXT-STEPS.md)
// § "Recommended next native slice"). The footer stage writes each track's `activeLoopIndex` and
// `selectedSlotIndex` from **live** state at footer-write time
// (`trackManager.getActiveLoopIndex(footerTrackCursor)`), while the loop bodies are written under the
// **interval's** slot identity. These cases answer, with the real deferred worker and the real loader,
// whether those two identities disagree in a way the persisted image cannot represent:
//
//   * PAUSE the real worker with the footer DUE but not yet emitted (`stage == Footer`,
//     `footerWriteStage == SelectedTrack`, `footerTrackCursor == 0`). The driver simply stops calling
//     `processDeferredSaveState`, so no test-only owner or hook is added to production code;
//   * perform the A -> B handoff through the production manager route
//     (`TrackManager::setSelectedSlotIndex(..., SyncPlayback::Yes)`), the move a slot gesture makes;
//   * resume the worker and RELOAD through `StorageManager::loadCurrentWorkspaceFromSd`, then drain the
//     restore it queues with the production load-service turns.
//
// The oracle keeps the two identities distinct, which is the contract's own distinction:
// **content identity** is the interval's slot (A's body lands under A's id and nowhere else), and
// **workspace focus** is the selection at footer-write time (B, because the footer is written *after*
// the handoff -- not A, the interval whose capture-stop admission owed the save). The no-switch control
// restores A, so "B" cannot pass by construction, and the failure case proves a failed footer write never
// publishes a torn image.
// ---------------------------------------------------------------------------

namespace S41Footer {

constexpr uint8_t kTrack = 0;
/// The interval whose slot-keyed persistence admission owes the save (the stop's own admission shape).
constexpr uint8_t kSlotA = 1;
/// The successor the handoff moves the workspace focus to.
constexpr uint8_t kSlotB = 3;
/// A slot the fixture parks the focus on before moving it to A: the production selection route returns
/// early when the selection already **is** the target, so a case that started there would owe no save.
constexpr uint8_t kSlotParked = 0;
constexpr uint8_t kNoteA = 60;
constexpr uint8_t kNoteB = 67;
constexpr uint32_t kSeedLoopLengthTicks = 768;
/// Generous bound: reaching the footer costs one slice per header/meta field (~250 on this geometry), and
/// a bounded wait is what keeps a stalled worker a failure instead of a hang.
constexpr uint32_t kTurnBudget = 4096;

/// The loop identity the production payload builder stamps for `(track, slot)`. Distinct per slot, which
/// is what makes a slot-keyed mix-up visible in the restored content.
inline LoopId seededLoopId(uint8_t trackIndex, uint8_t slotIndex) {
  return 1000u + static_cast<uint32_t>(trackIndex) * 16u + slotIndex;
}

/// The fixture's read of the workspace focus, through the same production accessors the runtime uses.
struct WorkspaceFocus {
  uint8_t selectedTrack = 0xFF;
  uint8_t active[Config::NUM_TRACKS] = {};
  uint8_t selected[Config::NUM_TRACKS] = {};
};

inline WorkspaceFocus readWorkspaceFocus() {
  WorkspaceFocus focus;
  focus.selectedTrack = trackManager.getSelectedTrackIndex();
  for (uint8_t t = 0; t < Config::NUM_TRACKS; ++t) {
    focus.active[t] = trackManager.getActiveLoopIndex(t);
    focus.selected[t] = trackManager.getSelectedSlotIndex(t);
  }
  return focus;
}

inline bool focusOnTrackIs(const WorkspaceFocus& focus, uint8_t track, uint8_t slot) {
  return focus.selectedTrack == track && focus.active[track] == slot &&
         focus.selected[track] == slot;
}

/// True when the owed save is down to its **completion**: the bundle is on the card, nothing is queued or
/// writing, and the save is still owed. This is the observable pause point an injected card fault needs to
/// land in the completion's own first sub-step -- the completion is admitted *and* stepped inside one
/// scheduler slice, so "queued but not yet writing" is never observable in the healthy path.
inline bool workspaceCompletionIsNext() {
  return !storageSession.persistenceWorkItem.itemActive &&
         PersistenceWorkQueue::writingWorkItemCount() == 0 &&
         PersistenceWorkQueue::queueDepth() == 0 &&
         storageSession.currentWorkspaceSave.pending &&
         !StorageManagerInternal::workspaceFooterPersistDeferred &&
         SD.exists(CurrentSetStorage::kCurrentRuntimeBundlePath);
}

/// One production scheduler turn -- the same entry `main.cpp` calls after MIDI/clock/playback servicing.
inline void workerTurn() {
  StorageManager::processDeferredSaveState(looperState.getLooperState());
}

/// Drive production turns until `stop` reports true. Returns the turns run; the caller asserts against
/// `kTurnBudget` so a worker that never reaches the state fails rather than hangs.
template <typename StopPredicate>
inline uint32_t driveTurnsUntil(uint32_t maxTurns, StopPredicate stop) {
  uint32_t turns = 0;
  for (; turns < maxTurns; ++turns) {
    if (stop()) break;
    workerTurn();
  }
  return turns;
}

/// The pause point this section is built on: the worker owes the footer and has emitted none of it.
inline bool workspaceFooterIsDue() {
  return storageSession.currentWorkspaceSave.stage == DeferredSaveStage::Footer &&
         storageSession.currentWorkspaceSave.footerWriteStage ==
             DeferredFooterWriteStage::SelectedTrack &&
         storageSession.currentWorkspaceSave.footerTrackCursor == 0;
}

/// The worker has nothing left owed: no pending save, no queued/writing item, and no deferred footer
/// request left over from a focus change.
inline bool workerIsIdle() {
  return !StorageManagerInternal::hasPersistenceWorkPending() &&
         !StorageManagerInternal::workspaceFooterPersistDeferred;
}

inline uint32_t driveWorkerToIdle() {
  return driveTurnsUntil(kTurnBudget, workerIsIdle);
}

/// One line naming every gate the worker can be parked behind, for a bounded-wait failure message. A
/// "nothing happened" failure must say which gate denied.
inline void formatWorkerState(char* out, size_t outSize) {
  snprintf(out, outSize,
           "stage=%u footerStage=%u cursor=%u pending=%u footerDeferred=%u queue=%u writing=%u "
           "itemActive=%u captureActive=%u recording=%u armedOrQueued=%u holdoff=%u slotLoad=%u "
           "completedOk=%u bundle=%u temp=%u",
           static_cast<unsigned>(storageSession.currentWorkspaceSave.stage),
           static_cast<unsigned>(storageSession.currentWorkspaceSave.footerWriteStage),
           static_cast<unsigned>(storageSession.currentWorkspaceSave.footerTrackCursor),
           storageSession.currentWorkspaceSave.pending ? 1u : 0u,
           StorageManagerInternal::workspaceFooterPersistDeferred ? 1u : 0u,
           static_cast<unsigned>(PersistenceWorkQueue::queueDepth()),
           static_cast<unsigned>(PersistenceWorkQueue::writingWorkItemCount()),
           storageSession.persistenceWorkItem.itemActive ? 1u : 0u,
           StorageManagerInternal::isCaptureActiveForPersistence() ? 1u : 0u,
           StorageManagerInternal::isCaptureRecordingForPersistence() ? 1u : 0u,
           StorageManagerInternal::anyTrackArmedOrPendingRecordForPersistence() ? 1u : 0u,
           StorageManagerInternal::deferredSaveBlockedByPostLoadCommitHoldoff() ? 1u : 0u,
           StorageManagerInternal::deferredSaveBlockedByActiveSlotLoadSd() ? 1u : 0u,
           storageSession.currentWorkspaceSave.lastCompletedOk ? 1u : 0u,
           SD.exists(CurrentSetStorage::kCurrentRuntimeBundlePath) ? 1u : 0u,
           SD.exists(CurrentSetStorage::kCurrentMetaTempPath) ? 1u : 0u);
}

/// Whether the slot's committed pass rows contain a note-on for `note` -- the same read the firmware's
/// own content dump makes, so the assertion is on recorded content rather than on a live capture store.
inline bool committedSlotHasNote(Track& track, uint8_t slot, uint8_t note) {
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

/// Drop what a previous case owed and start this one from a known persistence state. The load subsystem
/// reset is the S2b.7 one (it is the same load/restore state these cases queue through).
inline void resetPersistenceSessionForS41() {
  resetLoadSubsystemForS2b7();
  // Drop what an earlier case owed rather than draining it: a leftover `FinalizeWorkspace` entry outlives
  // the image it was admitted against, so it would sit at the queue head failing forever (the completion
  // step patches `runtime.bundle.bin`, which a fresh case has not written yet).
  StorageManagerInternal::resetDeferredSaveJobState();
  StorageManagerInternal::resetPersistenceWorkItemJobState();
  PersistenceWorkQueue::resetForTests();
  StorageManagerInternal::resetRevisionCommitJobState();
  StorageManagerInternal::resetRevisionLoadJobState();
  storageSession.revisionCommit.pending = false;
  storageSession.revisionCommit.inProgress = false;
  storageSession.revisionLoad.pending = false;
  storageSession.revisionLoad.inProgress = false;
  storageSession.bootRecovery.pending = false;
  storageSession.currentWorkspaceSave.pending = false;
  storageSession.currentWorkspaceSave.urgentRequested = false;
  storageSession.currentWorkspaceSave.deferDispatchUntilMs = 0;
  StorageManagerInternal::workspaceFooterPersistDeferred = false;
  StorageManagerInternal::armPostLoadCommitSaveHoldoff(0);
}

/// Two committed slots on the focus track, seeded through the production payload writer and the
/// production deferred-restore service. Their notes and loop identities differ, so a slot-keyed mix-up
/// anywhere in the save/load path is visible in the assertions.
inline void seedTwoCommittedSlots() {
  trackManager.setSelectedTrack(kTrack);
  TEST_ASSERT_TRUE_MESSAGE(RestoreHarness::writePayloadWithOneNoteToSd(kTrack, kSlotA, kNoteA),
                           "the fixture needs slot A's payload built by the production writer");
  TEST_ASSERT_TRUE_MESSAGE(RestoreHarness::writePayloadWithOneNoteToSd(kTrack, kSlotB, kNoteB),
                           "the fixture needs slot B's payload built by the production writer");
  RestoreHarness::armRestoreRequestThroughProductionQueue(kTrack, kSlotA);
  RestoreHarness::armRestoreRequestThroughProductionQueue(kTrack, kSlotB);
  RestoreHarness::runLoadServiceTurns(128, RestoreHarness::restoreWorkIdle);

  Track& track = trackManager.getTrack(kTrack);
  TEST_ASSERT_TRUE_MESSAGE(track.getLoop(kSlotA).hasCommittedPasses(),
                           "slot A must hold committed content before the handoff");
  TEST_ASSERT_TRUE_MESSAGE(track.getLoop(kSlotB).hasCommittedPasses(),
                           "slot B must hold committed content before the handoff");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(kSeedLoopLengthTicks, track.getLoop(kSlotA).loopLengthTicks,
                                   "slot A must carry the seeded loop length");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(kSeedLoopLengthTicks, track.getLoop(kSlotB).loopLengthTicks,
                                   "slot B must carry the seeded loop length");

  // The restore's commit arms the finite post-load save holdoff; advance the fixture clock past it so the
  // deferred worker is not parked by the *previous* load's own grace period.
  nativeMillisAdvance(LoadLoopBudget::PostLoadCommitSaveHoldoffMs + 1u);
}

/// Put the workspace focus on slot A through the production selection route and let the worker land it,
/// so the card holds a complete, durable image whose focus is A before the case starts.
inline void establishDurableFocusOnA() {
  if (trackManager.getSelectedSlotIndex(kTrack) == kSlotA) {
    trackManager.setSelectedSlotIndex(kTrack, kSlotParked, SyncPlayback::Yes);
    (void)driveWorkerToIdle();
  }
  trackManager.setSelectedSlotIndex(kTrack, kSlotA, SyncPlayback::Yes);
  TEST_ASSERT_TRUE_MESSAGE(focusOnTrackIs(readWorkspaceFocus(), kTrack, kSlotA),
                           "the production slot-move route must put the runtime focus on A");
  const uint32_t turns = driveWorkerToIdle();
  char state[512];
  formatWorkerState(state, sizeof(state));
  char message[640];
  snprintf(message, sizeof(message),
           "the focus move's own persist request must discharge within the turn budget (turns=%u %s)",
           static_cast<unsigned>(turns), state);
  TEST_ASSERT_TRUE_MESSAGE(workerIsIdle(), message);
  // `workerIsIdle()` alone is satisfied by a save that **failed and dropped** its obligation, so the
  // precondition also names the completion: the durable image must exist and have been written ok.
  TEST_ASSERT_TRUE_MESSAGE(storageSession.currentWorkspaceSave.lastCompletedOk,
                           "the durable image must be a completed save, not an idle failed one");
  TEST_ASSERT_TRUE_MESSAGE(SD.exists(CurrentSetStorage::kCurrentRuntimeBundlePath),
                           "the durable image must leave a runtime bundle on the card");
}

/// The slot-keyed persistence admission a capture stop performs, through the production composite
/// itself: `Track::requestLoopSlotPersist` (`markLoopSlotMaterialDirty` + `admitLoopSlotPersist`) plus the
/// owed save. `UINT32_MAX` is the "no stop-path heap sample" admission the current-heap gate then serves.
inline void admitSlotKeyedPersistForA() {
  requestLoopSlotPersistAndSaveState(trackManager.getTrack(kTrack), kSlotA, UINT32_MAX);
}

/// Reload the workspace from the card and drive the restore it queues, then report what the card held.
inline void reloadWorkspaceFromCard() {
  TEST_ASSERT_TRUE_MESSAGE(StorageManager::loadCurrentWorkspaceFromSd(looperState.getLooperState()),
                           "the persisted workspace must reload from the card");
  // The boot playback set is the per-track union of the footer's active and selected slots, so only the
  // persisted focus is restored by the load itself. The slot the footer did **not** name is read back
  // through the runtime's own focus request -- the same entry the selection path uses -- so both slots'
  // content is observed through production calls rather than by poking the payload loader.
  StorageManager::requestLoopSlotRestoreFromSd(kTrack, kSlotA);
  StorageManager::requestLoopSlotRestoreFromSd(kTrack, kSlotB);
  RestoreHarness::runLoadServiceTurns(128, RestoreHarness::restoreWorkIdle);
}

/// Post-reload restore state, for the content assertion's failure message.
inline void formatRestoreState(char* out, size_t outSize) {
  DeferredLoopSlotRestore head{};
  const bool hasHead = peekFirstPendingLoopSlotRestore(head);
  snprintf(out, outSize,
           "pending=%u jobActive=%u running=%u session=%u bootDrain=%u seq=%u committed=%u/%u "
           "payloadRam=%u/%u attempted=%u/%u head=%u/%u",
           static_cast<unsigned>(StorageManagerInternal::pendingLoopSlotRestoreCount()),
           StorageManagerInternal::anyLoadLoopJobActive() ? 1u : 0u,
           StorageManagerInternal::isActiveLoadLoopJobRunning() ? 1u : 0u,
           SlotLoadSession::isActive() ? 1u : 0u,
           StorageManagerInternal::getBootTitleLoadDrain() ? 1u : 0u,
           sequencerRunning ? 1u : 0u,
           trackManager.getTrack(kTrack).getLoop(kSlotA).hasCommittedPasses() ? 1u : 0u,
           trackManager.getTrack(kTrack).getLoop(kSlotB).hasCommittedPasses() ? 1u : 0u,
           StorageManager::hasLoopSlotPayloadOnSdInRam(kTrack, kSlotA) ? 1u : 0u,
           StorageManager::hasLoopSlotPayloadOnSdInRam(kTrack, kSlotB) ? 1u : 0u,
           StorageManagerInternal::isLoopSlotRestoreAttempted(kTrack, kSlotA) ? 1u : 0u,
           StorageManagerInternal::isLoopSlotRestoreAttempted(kTrack, kSlotB) ? 1u : 0u,
           hasHead ? static_cast<unsigned>(head.track) : 255u,
           hasHead ? static_cast<unsigned>(head.slot) : 255u);
}

/// The content/identity half of the oracle: each slot still holds **its own** loop identity and note.
inline void assertSlotContentSurvived(uint8_t slot, uint8_t note, const char* label) {
  Track& track = trackManager.getTrack(kTrack);
  char message[512];
  char restoreState[256];
  formatRestoreState(restoreState, sizeof(restoreState));
  snprintf(message, sizeof(message),
           "%s: committed=%u length=%lu loopId=%lu | %s", label,
           track.getLoop(slot).hasCommittedPasses() ? 1u : 0u,
           static_cast<unsigned long>(track.getLoop(slot).loopLengthTicks),
           static_cast<unsigned long>(track.getLoop(slot).loopId), restoreState);
  TEST_ASSERT_TRUE_MESSAGE(track.getLoop(slot).hasCommittedPasses(), message);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(kSeedLoopLengthTicks, track.getLoop(slot).loopLengthTicks, message);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(seededLoopId(kTrack, slot),
                                   static_cast<uint32_t>(track.getLoop(slot).loopId), message);
  TEST_ASSERT_TRUE_MESSAGE(committedSlotHasNote(track, slot, note), message);
  const uint8_t otherNote = (note == kNoteA) ? kNoteB : kNoteA;
  TEST_ASSERT_FALSE_MESSAGE(committedSlotHasNote(track, slot, otherNote),
                            "a slot must not carry the other slot's content");
}

/// The focus half of the oracle, with A and B named apart so a constant answer cannot pass.
inline void assertRestoredFocusIs(uint8_t expectedSlot, const char* label) {
  const WorkspaceFocus focus = readWorkspaceFocus();
  char message[224];
  snprintf(message, sizeof(message),
           "%s: restored selectedTrack=%u active=%u selected=%u (expected track %u slot %u)", label,
           static_cast<unsigned>(focus.selectedTrack), static_cast<unsigned>(focus.active[kTrack]),
           static_cast<unsigned>(focus.selected[kTrack]), static_cast<unsigned>(kTrack),
           static_cast<unsigned>(expectedSlot));
  TEST_ASSERT_TRUE_MESSAGE(focusOnTrackIs(focus, kTrack, expectedSlot), message);
}

}  // namespace S41Footer

/// **S4.1 / persist-1: the footer records the workspace focus at footer-write time.**
///
/// The worker is paused with A's stop-shaped, slot-keyed persistence admitted and the footer due but
/// unwritten. The A -> B handoff then happens through the production manager route, and the worker
/// resumes. The persisted image must keep the two identities apart: A's body under A's loop identity
/// (the interval that owed the save), and the **focus** on B (the selection when the footer was written).
/// The interval's own slot (A) is the counterexample this case rejects, which is the mechanical
/// substitution NEXT-STEPS.md warns against.
void test_s4_1_the_footer_records_the_focus_at_footer_write_time_not_the_interval_slot() {
  S41Footer::resetPersistenceSessionForS41();
  S41Footer::seedTwoCommittedSlots();
  S41Footer::establishDurableFocusOnA();

  S41Footer::admitSlotKeyedPersistForA();
  const uint32_t turnsToFooter = S41Footer::driveTurnsUntil(S41Footer::kTurnBudget,
                                                            S41Footer::workspaceFooterIsDue);
  char pauseMessage[256];
  snprintf(pauseMessage, sizeof(pauseMessage),
           "the worker must reach the footer stage within the turn budget (turns=%u stage=%u footerStage=%u cursor=%u)",
           static_cast<unsigned>(turnsToFooter),
           static_cast<unsigned>(storageSession.currentWorkspaceSave.stage),
           static_cast<unsigned>(storageSession.currentWorkspaceSave.footerWriteStage),
           static_cast<unsigned>(storageSession.currentWorkspaceSave.footerTrackCursor));
  TEST_ASSERT_TRUE_MESSAGE(S41Footer::workspaceFooterIsDue(), pauseMessage);
  TEST_ASSERT_TRUE_MESSAGE(SD.exists(CurrentSetStorage::kCurrentMetaTempPath),
                           "the paused bundle write must hold its temp file");
  const size_t sizeBeforeFooter = SD.files[CurrentSetStorage::kCurrentMetaTempPath].size();

  // The handoff, through the same route a slot gesture uses (selection + active slot together). The
  // note-edit session lifecycle the route drives is counted, so "the production route ran" is observed
  // rather than assumed.
  const uint32_t slotChangesBefore = DrainHarness::slotChangeNotifications();
  trackManager.setSelectedSlotIndex(S41Footer::kTrack, S41Footer::kSlotB, SyncPlayback::Yes);
  TEST_ASSERT_TRUE_MESSAGE(S41Footer::focusOnTrackIs(S41Footer::readWorkspaceFocus(), S41Footer::kTrack,
                                                     S41Footer::kSlotB),
                           "the production slot move must put the runtime focus on B");
  TEST_ASSERT_GREATER_THAN_MESSAGE(
      slotChangesBefore, DrainHarness::slotChangeNotifications(),
      "the handoff must go through the production slot-move route, not a hand-poked index");

  const uint32_t resumedTurns = S41Footer::driveWorkerToIdle();
  char state[512];
  S41Footer::formatWorkerState(state, sizeof(state));
  char idleMessage[768];
  snprintf(idleMessage, sizeof(idleMessage),
           "the worker must discharge the save after the handoff (turns=%u %s)",
           static_cast<unsigned>(resumedTurns), state);
  TEST_ASSERT_TRUE_MESSAGE(S41Footer::workerIsIdle(), idleMessage);
  TEST_ASSERT_TRUE_MESSAGE(storageSession.currentWorkspaceSave.lastCompletedOk,
                           "the resumed save must complete rather than fail and drop its obligation");
  TEST_ASSERT_TRUE_MESSAGE(SD.exists(CurrentSetStorage::kCurrentRuntimeBundlePath),
                           "the completed save must leave a runtime bundle on the card");
  TEST_ASSERT_GREATER_THAN_MESSAGE(sizeBeforeFooter,
                                   SD.files[CurrentSetStorage::kCurrentRuntimeBundlePath].size(),
                                   "the footer must have been appended after the pause point");
  // The dirty flag is cleared only by a completed, token-verified body write for that slot.
  TEST_ASSERT_FALSE_MESSAGE(currentSetLoopSlotDirty[S41Footer::kTrack][S41Footer::kSlotA],
                            "the slot-keyed admission must have been discharged by A's own body write");

  S41Footer::reloadWorkspaceFromCard();
  S41Footer::assertSlotContentSurvived(S41Footer::kSlotA, S41Footer::kNoteA, "A after handoff");
  S41Footer::assertSlotContentSurvived(S41Footer::kSlotB, S41Footer::kNoteB, "B after handoff");
  S41Footer::assertRestoredFocusIs(S41Footer::kSlotB, "focus after a handoff before footer emission");
}

/// **S4.1 / persist-1 control: without the handoff the same sequence restores A.**
///
/// This is what makes the case above discriminating: the assertion is not "the focus is always B", it is
/// "the restored focus is the selection the footer was written from".
void test_s4_1_control_the_no_switch_sequence_restores_the_unmoved_focus() {
  S41Footer::resetPersistenceSessionForS41();
  S41Footer::seedTwoCommittedSlots();
  S41Footer::establishDurableFocusOnA();

  S41Footer::admitSlotKeyedPersistForA();
  (void)S41Footer::driveTurnsUntil(S41Footer::kTurnBudget, S41Footer::workspaceFooterIsDue);
  TEST_ASSERT_TRUE_MESSAGE(S41Footer::workspaceFooterIsDue(),
                           "the control needs the same pause point as the handoff case");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(S41Footer::kSlotA,
                                  trackManager.getSelectedSlotIndex(S41Footer::kTrack),
                                  "the control must not move the focus");

  const uint32_t turns = S41Footer::driveWorkerToIdle();
  char state[512];
  S41Footer::formatWorkerState(state, sizeof(state));
  char idleMessage[768];
  snprintf(idleMessage, sizeof(idleMessage),
           "the control's save must discharge within the turn budget (turns=%u %s)",
           static_cast<unsigned>(turns), state);
  TEST_ASSERT_TRUE_MESSAGE(S41Footer::workerIsIdle(), idleMessage);
  TEST_ASSERT_TRUE_MESSAGE(storageSession.currentWorkspaceSave.lastCompletedOk,
                           "the control's save must complete rather than fail and drop its obligation");

  S41Footer::reloadWorkspaceFromCard();
  S41Footer::assertSlotContentSurvived(S41Footer::kSlotA, S41Footer::kNoteA, "A without a handoff");
  S41Footer::assertSlotContentSurvived(S41Footer::kSlotB, S41Footer::kNoteB, "B without a handoff");
  S41Footer::assertRestoredFocusIs(S41Footer::kSlotA, "focus without a handoff");
}

/// **S4.1 / persist-2: a handoff after the footer was durably written is still landed.**
///
/// The worker is paused once the bundle (footer included) is finalized on the card and the body write is
/// next. The handoff then happens, so the footer on the card names A and the runtime names B. The
/// selection route's own `requestWorkspaceFooterPersistWhenSafe` + `requestDeferredSaveState` must
/// requeue the footer, and the reload must show B -- otherwise a focus change made between two saves is
/// silently lost.
void test_s4_1_a_handoff_after_the_footer_was_written_is_requeued_and_lands() {
  S41Footer::resetPersistenceSessionForS41();
  S41Footer::seedTwoCommittedSlots();
  S41Footer::establishDurableFocusOnA();

  S41Footer::admitSlotKeyedPersistForA();
  // Pause *after* the footer reached the card: the bundle temp has been renamed onto the final path.
  const uint32_t turns = S41Footer::driveTurnsUntil(
      S41Footer::kTurnBudget, []() {
        return SD.exists(CurrentSetStorage::kCurrentRuntimeBundlePath) &&
               !SD.exists(CurrentSetStorage::kCurrentMetaTempPath);
      });
  char pauseMessage[256];
  snprintf(pauseMessage, sizeof(pauseMessage),
           "the worker must finalize the bundle (footer included) within the turn budget (turns=%u temp=%u final=%u)",
           static_cast<unsigned>(turns), SD.exists(CurrentSetStorage::kCurrentMetaTempPath) ? 1u : 0u,
           SD.exists(CurrentSetStorage::kCurrentRuntimeBundlePath) ? 1u : 0u);
  TEST_ASSERT_TRUE_MESSAGE(!SD.exists(CurrentSetStorage::kCurrentMetaTempPath), pauseMessage);
  TEST_ASSERT_TRUE_MESSAGE(SD.exists(CurrentSetStorage::kCurrentRuntimeBundlePath), pauseMessage);

  trackManager.setSelectedSlotIndex(S41Footer::kTrack, S41Footer::kSlotB, SyncPlayback::Yes);
  TEST_ASSERT_TRUE_MESSAGE(S41Footer::focusOnTrackIs(S41Footer::readWorkspaceFocus(), S41Footer::kTrack,
                                                     S41Footer::kSlotB),
                           "the production slot move must put the runtime focus on B");

  const uint32_t resumedTurns = S41Footer::driveWorkerToIdle();
  char state[512];
  S41Footer::formatWorkerState(state, sizeof(state));
  char idleMessage[768];
  snprintf(idleMessage, sizeof(idleMessage),
           "the handoff after the footer must requeue and discharge persistence (turns=%u %s)",
           static_cast<unsigned>(resumedTurns), state);
  TEST_ASSERT_TRUE_MESSAGE(S41Footer::workerIsIdle(), idleMessage);
  TEST_ASSERT_TRUE_MESSAGE(storageSession.currentWorkspaceSave.lastCompletedOk,
                           "the requeued write must complete rather than fail and drop its obligation");

  S41Footer::reloadWorkspaceFromCard();
  S41Footer::assertSlotContentSurvived(S41Footer::kSlotA, S41Footer::kNoteA, "A after late handoff");
  S41Footer::assertSlotContentSurvived(S41Footer::kSlotB, S41Footer::kNoteB, "B after late handoff");
  S41Footer::assertRestoredFocusIs(S41Footer::kSlotB, "focus after a handoff past the footer");
}

/// **S4.1 / persist-3: a failed footer write publishes nothing, and the retry lands the handoff.**
///
/// The handoff happens with the card refusing writes, so the aborted bundle must leave the previously
/// durable image byte-identical (no torn footer, no half-written bundle accepted by the loader). With the
/// card healthy again, the requeued write must land the post-handoff focus plus both bodies.
void test_s4_1_a_failed_footer_write_never_publishes_and_the_retry_lands_the_handoff() {
  S41Footer::resetPersistenceSessionForS41();
  S41Footer::seedTwoCommittedSlots();
  S41Footer::establishDurableFocusOnA();

  const std::vector<uint8_t> durableBundle =
      SD.files[CurrentSetStorage::kCurrentRuntimeBundlePath];
  TEST_ASSERT_TRUE_MESSAGE(!durableBundle.empty(),
                           "the case needs the pre-handoff durable image to compare against");

  S41Footer::admitSlotKeyedPersistForA();
  (void)S41Footer::driveTurnsUntil(S41Footer::kTurnBudget, S41Footer::workspaceFooterIsDue);
  TEST_ASSERT_TRUE_MESSAGE(S41Footer::workspaceFooterIsDue(),
                           "the failure case needs the same pause point as the handoff case");

  // The card starts refusing writes at the footer, and the handoff happens underneath the failed write.
  nativeSdWriteBudget = 0;
  trackManager.setSelectedSlotIndex(S41Footer::kTrack, S41Footer::kSlotB, SyncPlayback::Yes);
  const uint32_t failedTurns = S41Footer::driveTurnsUntil(256, []() { return false; });
  nativeSdWriteBudget = -1;

  char failedMessage[256];
  snprintf(failedMessage, sizeof(failedMessage),
           "the failed write must leave the durable bundle untouched (turns=%u equal=%u size=%u/%u)",
           static_cast<unsigned>(failedTurns),
           SD.files[CurrentSetStorage::kCurrentRuntimeBundlePath] == durableBundle ? 1u : 0u,
           static_cast<unsigned>(SD.files[CurrentSetStorage::kCurrentRuntimeBundlePath].size()),
           static_cast<unsigned>(durableBundle.size()));
  TEST_ASSERT_TRUE_MESSAGE(SD.files[CurrentSetStorage::kCurrentRuntimeBundlePath] == durableBundle,
                           failedMessage);
  TEST_ASSERT_FALSE_MESSAGE(SD.exists(CurrentSetStorage::kCurrentMetaTempPath) &&
                                CurrentSetStorage::verifySaveFileTokenAtPath(
                                    CurrentSetStorage::kCurrentMetaTempPath),
                            "an aborted footer write must not leave a token-valid temp bundle behind");

  const uint32_t retryTurns = S41Footer::driveWorkerToIdle();
  char retryMessage[256];
  snprintf(retryMessage, sizeof(retryMessage),
           "the retry must discharge the owed save (turns=%u pending=%u footerDeferred=%u)",
           static_cast<unsigned>(retryTurns),
           storageSession.currentWorkspaceSave.pending ? 1u : 0u,
           StorageManagerInternal::workspaceFooterPersistDeferred ? 1u : 0u);
  TEST_ASSERT_TRUE_MESSAGE(S41Footer::workerIsIdle(), retryMessage);

  S41Footer::reloadWorkspaceFromCard();
  S41Footer::assertSlotContentSurvived(S41Footer::kSlotA, S41Footer::kNoteA, "A after failed write");
  S41Footer::assertSlotContentSurvived(S41Footer::kSlotB, S41Footer::kNoteB, "B after failed write");
  S41Footer::assertRestoredFocusIs(S41Footer::kSlotB, "focus after the retry");
}

/// **F4 / DEC-091 — a failed workspace completion is discharged, not retried at the queue head.**
///
/// The completion's sub-steps (`patchLastActiveUnix`, the bundle CRC refresh, `workspace.bin`) are
/// deterministic, so a failure re-entered the same step forever: the item was requeued to the **head**, so
/// every other persistence item waited behind it and `hasDeferredSaveWork()` stayed true for the rest of the
/// boot -- which refuses the save pre-flight and the read-only slot-payload observation (`"busy"`). This
/// drives the real worker into that failure with the card refusing writes, and asserts the discharge, the
/// reported failure, the user-facing recovery, and that the owed work is still owed.
void test_f4_a_failed_completion_is_discharged_and_releases_the_queue() {
  S41Footer::resetPersistenceSessionForS41();
  S41Footer::seedTwoCommittedSlots();
  S41Footer::establishDurableFocusOnA();

  S41Footer::admitSlotKeyedPersistForA();
  // Run until the **completion** is the only work left, so the injected fault lands in its first sub-step
  // rather than in the bundle or the body write.
  const uint32_t turnsToCompletion = S41Footer::driveTurnsUntil(
      S41Footer::kTurnBudget, []() { return S41Footer::workspaceCompletionIsNext(); });
  char pauseMessage[256];
  snprintf(pauseMessage, sizeof(pauseMessage),
           "the worker must reach the completion within the turn budget (turns=%u %s)",
           static_cast<unsigned>(turnsToCompletion), []() -> const char* {
             static char state[512];
             S41Footer::formatWorkerState(state, sizeof(state));
             return state;
           }());
  TEST_ASSERT_TRUE_MESSAGE(S41Footer::workspaceCompletionIsNext(), pauseMessage);
  // The body is durable before the completion runs, and its dirty flag is discharged by its own write.
  TEST_ASSERT_FALSE_MESSAGE(currentSetLoopSlotDirty[S41Footer::kTrack][S41Footer::kSlotA],
                            "the slot's body write must have completed before the completion step");
  // A slot left materially dirty at failure time is the "owed work" the contract keeps for the retry.
  StorageManager::markLoopSlotMaterialDirty(S41Footer::kTrack, S41Footer::kSlotB);
  TEST_ASSERT_TRUE(currentSetLoopSlotDirty[S41Footer::kTrack][S41Footer::kSlotB]);

  // The card refuses writes from here on: the failure under test is a **persistent** one. (A single-turn
  // fault would self-heal on the retry, which is exactly what the front-requeue is for -- so it cannot
  // tell the two policies apart.)
  nativeSdWriteBudget = 0;
  S41Footer::workerTurn();

  // Reported, not silent.
  TEST_ASSERT_FALSE_MESSAGE(storageSession.currentWorkspaceSave.lastCompletedOk,
                            "a failed completion must not report success");
  TEST_ASSERT_TRUE_MESSAGE(storageSession.currentWorkspaceSave.failedAtMs != 0,
                           "a failed completion must record when it failed");

  // Discharged: no requeued head, no writing entry, nothing active.
  (void)S41Footer::driveTurnsUntil(64, S41Footer::workerIsIdle);
  char state[512];
  S41Footer::formatWorkerState(state, sizeof(state));
  char idleMessage[768];
  snprintf(idleMessage, sizeof(idleMessage),
           "the failed completion must be discharged, not left holding the queue (%s)", state);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, PersistenceWorkQueue::queueDepth(), idleMessage);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, PersistenceWorkQueue::writingWorkItemCount(), idleMessage);
  TEST_ASSERT_FALSE_MESSAGE(storageSession.persistenceWorkItem.itemActive, idleMessage);
  TEST_ASSERT_FALSE_MESSAGE(StorageManager::hasDeferredSaveWork(), idleMessage);

  // Heal the card for the recovery leg below.
  nativeSdWriteBudget = -1;

  // `hasDeferredSaveWork()` is the guard the two unrelated consumers read -- the save-new-set pre-flight
  // (`SavedSetIo`) and the read-only slot-payload observation, whose entry answers `PSTR("busy")` while it
  // is true (`LibraryObservationSession`). Those TUs are deliberately outside this suite's linked set
  // (A1.7's recorded scope decision), so the fixture asserts the guard itself, and that it stays false:
  // nothing may re-queue behind the discharged failure.
  for (int turn = 0; turn < 8; ++turn) {
    S41Footer::workerTurn();
  }
  TEST_ASSERT_FALSE_MESSAGE(StorageManager::hasDeferredSaveWork(),
                            "the guard the observation and save pre-flight read must clear and stay clear");

  // The owed work is still owed: the dirty slot was not swept by the failure...
  TEST_ASSERT_TRUE_MESSAGE(currentSetLoopSlotDirty[S41Footer::kTrack][S41Footer::kSlotB],
                           "a failed completion must leave the dirty set for the next save");
  // ...and the next save trigger completes once the card is healthy -- the documented retry.
  S41Footer::admitSlotKeyedPersistForA();
  const uint32_t retryTurns = S41Footer::driveWorkerToIdle();
  char retryState[512];
  S41Footer::formatWorkerState(retryState, sizeof(retryState));
  char retryMessage[768];
  snprintf(retryMessage, sizeof(retryMessage),
           "the next save trigger must complete after a discharged failure (turns=%u %s)",
           static_cast<unsigned>(retryTurns), retryState);
  TEST_ASSERT_TRUE_MESSAGE(S41Footer::workerIsIdle(), retryMessage);
  TEST_ASSERT_TRUE_MESSAGE(storageSession.currentWorkspaceSave.lastCompletedOk, retryMessage);
  TEST_ASSERT_TRUE_MESSAGE(SD.exists(CurrentSetStorage::kCurrentRuntimeBundlePath), retryMessage);
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
  // S2b.7 / D4 — the drain advances the restore it waits on.
  RUN_TEST(test_s2b7_the_drain_advances_a_session_holding_restore_and_names_the_commit_gate);
  RUN_TEST(test_s2b7_the_drain_advances_the_restore_to_completion_where_the_gate_admits);
  RUN_TEST(test_s2b7_the_drain_services_a_parked_instance_holding_the_session);
  RUN_TEST(test_s2b7_control_the_drain_completes_once_the_restore_releases_the_session);
  // S4.1 — persistence identity during handoff: the footer's identity and its write point.
  RUN_TEST(test_s4_1_the_footer_records_the_focus_at_footer_write_time_not_the_interval_slot);
  RUN_TEST(test_s4_1_control_the_no_switch_sequence_restores_the_unmoved_focus);
  RUN_TEST(test_s4_1_a_handoff_after_the_footer_was_written_is_requeued_and_lands);
  RUN_TEST(test_s4_1_a_failed_footer_write_never_publishes_and_the_retry_lands_the_handoff);
  // F4 / DEC-091 -- a failed workspace completion must not hold the queue.
  RUN_TEST(test_f4_a_failed_completion_is_discharged_and_releases_the_queue);
  return UNITY_END();
}
