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
#include "load_and_restore_harness.h"
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
#include "../../src/StorageManager/RestoreRequestState.cpp"
#include "../../src/StorageManager/LoopSlotRestoreQueue.cpp"
// The runtime's own explicit-request / background-fill entry points live beside the SD boot load, not
// in the load-job TU: without them the suite could only reach the queue through internal helpers.
#include "../../src/StorageManager.cpp"
// A3.4a: the single-owner drain guard has its own translation unit, so the guard's mechanical contract is
// verifiable here without the drain scheduler's dependency closure. The **real blocking drain** is linked
// by `test_storage_drain_restore`; this suite deliberately does not pull its scheduler closure in.
#include "../../src/StorageManager/Internal.cpp"
#include "../../src/StorageManager/PersistenceDrainGuard.cpp"

void setUp() {
  StorageManagerInternal::clearPendingLoopSlotRestoresAtBoot();
  StorageManagerInternal::resetAllLoopSlotRestoreAttempted();
  // The job instances are function-local statics in `LoadLoopJob.cpp` and are **not** cleared by the
  // queue resets above. Without this, an earlier fixture's parked job survives into the next fixture
  // and owns its request, so an outcome assertion reads a state this fixture never produced (measured:
  // a parked job left over from an earlier case made the C4 fixture read "owned" while its own request
  // was still queued). Each fixture starts from no restore work.
  StorageManagerInternal::clearLoadLoopJob();
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
  StorageManagerInternal::markLoopSlotRestoreAttempted(2, 3);
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
  StorageManagerInternal::markLoopSlotRestoreAttempted(1, 1);
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
  /// S4f.1's accepted-interaction latch, targeting one slot. `Track` declares this struct a friend,
  /// and `noLiveOwnerForSlot` counts the latch as a live owner, so a fixture can hold a slot through
  /// production state without admitting a capture interval (and its pool budget).
  static void latchAcceptedStopForSlot(Track& track, PendingStopDestination destination,
                                       uint8_t slot) {
    track.latchAcceptedStop(destination, slot, 0);
  }
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
  // The interval is opened through the production start, not by poking Loop state: a hand-built interval
  // admits a job that can never finalize, which the retirement half of this fixture would then never reach.
  track.startOverdubbing(96);
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

  // The other half of S2b.5: once the owner retires, the retained request is served exactly once. The
  // in-memory card holds no payload for this slot, so the correct post-retirement outcome is an attempt
  // that empties it -- allowed now precisely because no live owner remains.
  for (unsigned i = 0; i < 20000 && track.hasPendingDeferredOverdubStop(); ++i) {
    TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_TRUE_MESSAGE(track.noLiveOwnerForSlot(0),
                           "the retired slot has no live owner, so the load may proceed");
  TEST_ASSERT_EQUAL_UINT16(1, StorageManagerInternal::pendingLoopSlotRestores_.count);

  (void)StorageManager::selectSubmittedLoadJobs();
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, StorageManagerInternal::pendingLoopSlotRestores_.count,
                                   "the retained request is consumed once the owner retires");
  // The retired interval committed its content, so the queued restore is resolved as *unnecessary* rather
  // than served: the pop's done-test discards it, no load is begun, and the fresh committed content is left
  // alone. That is the correct post-retirement outcome, and it is why no attempt is marked.
  TEST_ASSERT_FALSE_MESSAGE(StorageManagerInternal::isLoopSlotRestoreAttempted(0, 0),
                            "a satisfied slot is resolved without a load attempt");
  TEST_ASSERT_TRUE_MESSAGE(track.getLoop(0).hasCommittedPasses(),
                           "nothing may reset the slot after the interval committed");
  // A second selection finds nothing left: exactly one request, one attempt.
  (void)StorageManager::selectSubmittedLoadJobs();
  TEST_ASSERT_EQUAL_UINT16(0, StorageManagerInternal::pendingLoopSlotRestores_.count);
}


// ---------------------------------------------------------------------------
// Packet A / A1.1 of `deferred-slot-restore-liveness` — the controllable restore harness.
//
// The fixtures below are the acceptance set for the restore-liveness change. Every one of them drives
// the **production** queue and load-job entry points (`selectSubmittedLoadJobs` +
// `stepSubmittedLoadJobs`) and asserts the **desired** behavior, so it is red on the current tree and
// green only once the change's mechanism lands. Old-state observations are kept as separate
// diagnostics, never as the acceptance oracle (tasks.md A1.11, design.md D7).
//
// The known-red set is excluded from the default gate behind `RESTORE_LIVENESS_RED` (a *test-read*
// environment variable, the `WORKING_SET_RED` precedent) and each case is promoted into the default
// gate in the same commit that turns it green.
// ---------------------------------------------------------------------------

/// The A1.1 smoke case: one queued restore request, driven to a committed slot through production
/// calls only. It proves the harness itself (payload writer + queue admission + selection + job steps)
/// reaches a real commit, which is the precondition every red fixture below relies on.
void test_restore_harness_smoke_commits_a_queued_request_through_production_path() {
  SD.reset();
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  // Explicit idle runtime: the harness's default track state is TRACK_PLAYING, which would gate the
  // background restore this case is about (see the harness header's state note).
  RestoreHarness::setRuntimeTransportIdle();

  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  TEST_ASSERT_FALSE(track.getLoop(2).hasCommittedPasses());
  TEST_ASSERT_TRUE(RestoreHarness::writePayloadWithOneNoteToSd(0, 2, 67));

  RestoreHarness::armRestoreRequestThroughProductionQueue(0, 2);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, StorageManagerInternal::pendingLoopSlotRestores_.count,
                                   "the payload-backed request must enter the real restore queue");

  const uint32_t turns = RestoreHarness::runLoadServiceTurns(64, RestoreHarness::restoreWorkIdle);
  char message[160];
  snprintf(message, sizeof(message),
           "the queued request did not commit through production turns (turns=%u, pending=%u, job=%u)",
           static_cast<unsigned>(turns),
           static_cast<unsigned>(StorageManagerInternal::pendingLoopSlotRestoreCount()),
           StorageManagerInternal::anyLoadLoopJobActive() ? 1u : 0u);
  TEST_ASSERT_TRUE_MESSAGE(track.getLoop(2).hasCommittedPasses(), message);
  TEST_ASSERT_EQUAL_UINT32(768, track.getLoop(2).loopLengthTicks);
  TEST_ASSERT_TRUE_MESSAGE(StorageManagerInternal::isLoopSlotRestoreAttempted(0, 2),
                           "a committed restore must mark its request consumed exactly once");
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, StorageManagerInternal::pendingLoopSlotRestores_.count,
                                   "the served request must leave the queue");
  TEST_ASSERT_FALSE_MESSAGE(StorageManagerInternal::anyLoadLoopJobActive(),
                            "the job retires after a completed commit");
}

/// **A1.2 [C1 — transport-gated background admission], desired behavior, red on current `src/`.**
///
/// Shape: the transport is running with one track audibly playing, a payload-bearing slot is queued
/// for background restore, and the focus sits on a different slot. Through the production admission
/// path the request must be either served or making measurable preparation progress within a bounded
/// number of turns — a request that is neither served nor progressing is an unexplained stall, which
/// is the defect.
///
/// Red today because `canRunBackgroundLoadLoopNow()` is false under transport, so the job is parked
/// and re-promoted without ever reading a byte (`design.md` C1/C2). Packet B's bounded background
/// preparation slice is what turns it green; the named waits that replace "progress" as the oracle
/// arrive in the same packet (A2.1/A2.2).
void test_c1_transport_gated_background_admission_desired() {
  SD.reset();
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  RestoreHarness::setTrackAudiblyPlaying(1);  // another track is sounding; focus track is stopped
  RestoreHarness::armBackgroundRestoreRequest(0, 2, 61);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, StorageManagerInternal::pendingLoopSlotRestores_.count,
                                   "the request must enter the real restore queue");

  const RestoreHarness::RestoreOutcomeObservation observation =
      RestoreHarness::observeRestoreOutcome(0, 2, 64);
  char message[256];
  snprintf(message, sizeof(message),
           "C1: the queued background restore was left unowned after %u turns "
           "(served=%u, ownedTurns=%u, pending=%u, active=%u, parked=%u, attempted=%u, committed=%u)",
           static_cast<unsigned>(observation.turnsRun), observation.served ? 1u : 0u,
           static_cast<unsigned>(observation.turnsOwned),
           static_cast<unsigned>(observation.pendingAtEnd), observation.jobActiveAtEnd ? 1u : 0u,
           observation.parkedAtEnd ? 1u : 0u, observation.attemptedAtEnd ? 1u : 0u,
           observation.committedAtEnd ? 1u : 0u);
  TEST_ASSERT_FALSE_MESSAGE(observation.unowned, message);
}

/// **A1.3 [C2 — parked job never re-serviced], desired behavior, red on current `src/`.**
///
/// Shape: the runtime is idle, a non-focus background job is begun and given one production service
/// turn, and only then does the transport start sounding. The job is therefore already *parked* when
/// playback begins — the state `stepSubmittedLoadJobsImpl` produces for a non-focus job under
/// transport (`parkActiveLoadLoopJob()`), and the state that keeps
/// `deferredSaveBlockedByActiveSlotLoadSd()` true for the rest of the playing session.
///
/// Desired behavior: the parked job receives further service, or the request is in a named wait.
/// "Parked, unchanged, with no service" across many turns is the defect — and it is what leaves the
/// blocking drain with nothing it can advance (C5).
///
/// Red today: the job cycles between the two instances (`ParkedIdleAction::PromoteParkedLow` in
/// selection re-promotes it, `!backgroundAllowed` re-parks it in the step) without reading a byte.
void test_c2_parked_job_is_serviced_or_named_after_transport_starts() {
  SD.reset();
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  RestoreHarness::setRuntimeTransportIdle();
  RestoreHarness::armBackgroundRestoreRequest(0, 2, 62);

  // Begin the job while idle: one production turn selects it and services it. The payload is small
  // enough to be read in that turn, so the job then sits in Parsing — the state transport parks.
  RestoreHarness::runSingleLoadServiceTurn();

  // Transport starts on another track while the job holds its slot-load session.
  RestoreHarness::setTrackAudiblyPlaying(1);
  const RestoreHarness::RestoreOutcomeObservation observation =
      RestoreHarness::observeRestoreOutcome(0, 2, 64);
  // C2's discriminator is stronger than C1's: a *parked* instance can legitimately own the request,
  // so "owned" alone is not enough. The desired behavior is that the request was **serviced** (the
  // active instance held it for at least one turn), or reached a terminal, or is in a named wait —
  // a named wait does not exist until Packet B, so Packet A's oracle is `served || activeOwned`.
  char message[352];
  snprintf(message, sizeof(message),
           "C2: the parked background job was never serviced after %u turns "
           "(parkedEnd=%u, parkedPhase=%u, ownedTurns=%u, activelyOwnedTurns=%u, "
           "pending=%u, attempted=%u, committed=%u)",
           static_cast<unsigned>(observation.turnsRun), observation.parkedAtEnd ? 1u : 0u,
           static_cast<unsigned>(observation.parkedPhaseAtEnd),
           static_cast<unsigned>(observation.turnsOwned),
           static_cast<unsigned>(observation.turnsActivelyOwned),
           static_cast<unsigned>(observation.pendingAtEnd), observation.attemptedAtEnd ? 1u : 0u,
           observation.committedAtEnd ? 1u : 0u);
  TEST_ASSERT_TRUE_MESSAGE(observation.served || observation.activeOwned, message);
}

/// **A1.5 [C4 — head-of-line blockage], desired behavior, red on current `src/`.**
///
/// Shape: the priority **head** is a request whose slot a live owner holds and which has **no
/// committed content**, and a lower-priority request for a free, uncommitted slot sits behind it.
///
/// Desired behavior: the startable lower-priority request begins, and the blocked head stays queued
/// with its reason observable. Queue order is a *selection* order, not a gate on service.
///
/// Red today: `ensureActiveLoadLoopJobSelected` calls `backgroundHeadMayBegin()` **before**
/// `popNextDeferredLoopSlotRestore`, and the pop has no track/slot filter. A blocked-but-not-done head
/// therefore means no entry is served at all, so the queue stalls behind it.
///
/// Shape facts measured while building this fixture (see `REVIEW.md` § A1.5):
/// * **The head must fail both tests.** `popNextDeferredLoopSlotRestore` drains *every* done entry from
///   the front (`isLoopSlotRestoreAttempted || hasCommittedPasses`), so a **committed** head is skipped
///   rather than blocking — an earlier draft built on a committed head was green on the untouched tree
///   and was discarded. The head must therefore have a live owner **and** no committed content.
/// * **A live owner without a commit** is exactly the capture-interval window: a slot that has a
///   playback runtime and an open overdub interval, reset to empty by the production reset (which does
///   not release the runtime's `captureStopOwner`). The interval route needs staging and canonical-copy
///   admission, so the fixture uses the production interval only to establish that owner and keeps the
///   S4f.1 accepted-interaction latch as the explicit owner of record.
/// * **The startable slot must be production-reset** — an earlier fixture in the same binary leaves
///   committed content at slot 2, which makes the outcome assertion read true before this fixture runs.
void test_c4_blocked_priority_head_does_not_stall_a_startable_request() {
  SD.reset();
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  RestoreHarness::setRuntimeTransportIdle();

  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();

  // Establish a genuine slot owner with no committed content: commit once to give slot 1 a playback
  // runtime, open an interval there, then production-reset the loop to empty. The reset clears content
  // but not the runtime's `captureStopOwner`, so `noLiveOwnerForSlot(1)` stays false while
  // `hasCommittedPasses()` is false — the exact pair C4 needs.
  commitRecordPassIntoSlot(track, 1);
  track.setActiveLoopIndex(1);
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  track.startOverdubbing(96);
  RestoreHarness::resetSlotToEmpty(0, 1);
  RestoreHarness::resetSlotToEmpty(0, 2);
  // The owner of record, so the fixture does not depend on the interval's internal state surviving the
  // reset. `Track::noLiveOwnerForSlot` counts the S4f.1 latch.
  TrackCaptureStopTestAccess::latchAcceptedStopForSlot(track, PendingStopDestination::Playing, 1);

  char setupMsg[288];
  snprintf(setupMsg, sizeof(setupMsg),
           "C4 setup: ownsSlot1=%u committed1=%u committed2=%u",
           track.noLiveOwnerForSlot(1) ? 0u : 1u,
           track.getLoop(1).hasCommittedPasses() ? 1u : 0u,
           track.getLoop(2).hasCommittedPasses() ? 1u : 0u);
  TEST_ASSERT_FALSE_MESSAGE(track.noLiveOwnerForSlot(1),
                            "the priority-head slot needs a live owner");
  TEST_ASSERT_FALSE_MESSAGE(track.getLoop(1).hasCommittedPasses(),
                            "the head must be uncommitted, or the pop drains it instead of blocking");
  TEST_ASSERT_FALSE_MESSAGE(track.getLoop(2).hasCommittedPasses(),
                            "the startable slot must start empty");

  TEST_ASSERT_TRUE_MESSAGE(RestoreHarness::writePayloadWithOneNoteToSd(0, 2, 63), setupMsg);
  // Both requests enter through the **forced** boot entry: this fixture is about queue ordering, so its
  // input queue is stated explicitly. A1.2/A1.3 are the payload-admission fixtures.
  TEST_ASSERT_TRUE(StorageManagerInternal::appendBootLoopSlotRestore(0, 1, 0));
  TEST_ASSERT_TRUE(StorageManagerInternal::appendBootLoopSlotRestore(0, 2, 1));
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(2, StorageManagerInternal::pendingLoopSlotRestores_.count,
                                   "both requests must be queued");
  StorageManagerInternal::sortPendingLoopSlotRestoreQueue();
  StorageManagerInternal::DeferredLoopSlotRestore head{};
  TEST_ASSERT_TRUE(StorageManagerInternal::peekFirstPendingLoopSlotRestore(head));
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, head.slot, "the blocked entry must be the priority head");

  const RestoreHarness::RestoreOutcomeObservation observation =
      RestoreHarness::observeRestoreOutcome(0, 2, 32);
  char message[384];
  snprintf(message, sizeof(message),
           "C4: the startable request behind a blocked head was never served or owned after %u turns "
           "(served=%u, unowned=%u, pending=%u, committed2=%u, attempted2=%u, active=%u, parked=%u, "
           "headStillQueued=%u, headOwned=%u)",
           static_cast<unsigned>(observation.turnsRun), observation.served ? 1u : 0u,
           observation.unowned ? 1u : 0u, static_cast<unsigned>(observation.pendingAtEnd),
           observation.committedAtEnd ? 1u : 0u, observation.attemptedAtEnd ? 1u : 0u,
           observation.jobActiveAtEnd ? 1u : 0u, observation.parkedAtEnd ? 1u : 0u,
           StorageManagerInternal::isDeferredLoopSlotRestoreQueued(0, 1) ? 1u : 0u,
           track.noLiveOwnerForSlot(1) ? 0u : 1u);
  TEST_ASSERT_TRUE_MESSAGE(RestoreHarness::restoreRequestHasOutcome(observation), message);
  TEST_ASSERT_TRUE_MESSAGE(
      StorageManagerInternal::isDeferredLoopSlotRestoreQueued(0, 1),
      "the blocked head must stay queued with its reason after a lower-priority request is served");
  TEST_ASSERT_FALSE_MESSAGE(track.noLiveOwnerForSlot(1),
                            "the fixture must not have cleared the head's owner by the end");
}

/// **A1.6 [C6 — attempt latch], desired behavior, red on current `src/`.**
///
/// Shape: a payload-backed restore whose **first** read attempt fails transiently (the slot file is
/// truncated so the payload read comes up short — the save token lives at the end of the file), then
/// the payload is healed and further production turns are driven with no external advancement of the
/// request.
///
/// Desired behavior: the request is retried and reaches `committed`; only a **persistent** failure
/// reaches a refusal, and only after a bounded attempt count.
///
/// Red today: every failure path calls `markLoopSlotRestoreAttempted`, and `loopSlotRestoreAttempted_`
/// is reset only by a full boot load, so one transient SD failure latches that `(track, slot)` for the
/// whole power cycle and the healed payload is never read again.
void test_c6_transient_failure_is_retried_not_latched() {
  SD.reset();
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  RestoreHarness::setRuntimeTransportIdle();
  RestoreHarness::armBackgroundRestoreRequest(0, 2, 64);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, StorageManagerInternal::pendingLoopSlotRestores_.count,
                                   "the request must enter the real restore queue");

  // Inject the transient failure and let the production path reach its failure terminal.
  const std::vector<uint8_t> goodPayload = RestoreHarness::snapshotSlotPayloadBytes(0, 2);
  TEST_ASSERT_FALSE(goodPayload.empty());
  RestoreHarness::truncateSlotPayload(0, 2, 3);
  RestoreHarness::observeRestoreOutcome(0, 2, 8);
  // Phase 1 asserts the failure was actually reached **and** that it is not a terminal latch: the
  // request is still owed (queued or in a named wait), which is the behavior A2.4 requires. Asserting
  // the latch here would re-encode the defect as the oracle.
  char failMsg[288];
  snprintf(failMsg, sizeof(failMsg),
           "C6: the injected transient failure must leave the request owed, not latched "
           "(attempted=%u, committed=%u, pending=%u, state=%u)",
           StorageManagerInternal::isLoopSlotRestoreAttempted(0, 2) ? 1u : 0u,
           trackManager.getSelectedTrack().getLoop(2).hasCommittedPasses() ? 1u : 0u,
           static_cast<unsigned>(StorageManagerInternal::pendingLoopSlotRestores_.count),
           static_cast<unsigned>(StorageManagerInternal::restoreRequestState(0, 2)));
  TEST_ASSERT_FALSE_MESSAGE(trackManager.getSelectedTrack().getLoop(2).hasCommittedPasses(), failMsg);
  TEST_ASSERT_TRUE_MESSAGE(
      StorageManagerInternal::pendingLoopSlotRestores_.count > 0 ||
          StorageManagerInternal::restoreRequestState(0, 2) ==
              StorageManagerInternal::RestoreRequestState::Waiting,
      failMsg);

  // Heal the payload and drive further turns with no external advancement of the request.
  RestoreHarness::replaceSlotPayloadBytes(0, 2, goodPayload);
  const RestoreHarness::RestoreOutcomeObservation retry = RestoreHarness::observeRestoreOutcome(0, 2, 32);
  char message[288];
  snprintf(message, sizeof(message),
           "C6: a healed payload was never retried after a transient failure (committed=%u, "
           "attempted=%u, pending=%u, active=%u, parked=%u)",
           retry.committedAtEnd ? 1u : 0u, retry.attemptedAtEnd ? 1u : 0u,
           static_cast<unsigned>(retry.pendingAtEnd), retry.jobActiveAtEnd ? 1u : 0u,
           retry.parkedAtEnd ? 1u : 0u);
  TEST_ASSERT_TRUE_MESSAGE(retry.committedAtEnd, message);
}

/// **A1.8 — the restore-state probe names the request/queue state (Packet A observability).**
///
/// The observable contract `design.md` D6 asks for, without a bisect: how many requests are queued,
/// whether the priority head can begin (and its identity when it cannot), which request each job
/// instance owns and in which phase, and whether the deferred save is held back by restore work. The
/// named-wait vocabulary itself arrives with Packet B; this probe reports the state that exists today,
/// which is the "waiting vs progressing vs terminated" distinction Packet B then refines.
void test_restore_state_probe_reports_queue_and_job_ownership() {
  SD.reset();
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  RestoreHarness::setRuntimeTransportIdle();

  // (1) Nothing queued, nothing in flight.
  {
    StorageManager::RestoreStateProbe probe{};
    StorageManager::probeRestoreState(probe);
    TEST_ASSERT_EQUAL_UINT16(0, probe.queuedCount);
    TEST_ASSERT_FALSE(probe.queueHeadPresent);
    TEST_ASSERT_FALSE(probe.activeJob);
    TEST_ASSERT_FALSE(probe.parkedJob);
    TEST_ASSERT_FALSE(probe.saveBlockedBySlotLoad);
  }

  // (2) A queued, startable request: the head is named and reported startable.
  RestoreHarness::armBackgroundRestoreRequest(0, 3, 65);
  {
    StorageManager::RestoreStateProbe probe{};
    StorageManager::probeRestoreState(probe);
    TEST_ASSERT_EQUAL_UINT16(1, probe.queuedCount);
    TEST_ASSERT_TRUE(probe.queueHeadPresent);
    TEST_ASSERT_EQUAL_UINT8(0, probe.queueHeadTrack);
    TEST_ASSERT_EQUAL_UINT8(3, probe.queueHeadSlot);
    TEST_ASSERT_TRUE_MESSAGE(probe.queueHeadStartable,
                             "a queued request on a free slot must report startable");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("queued", probe.queueHeadRequestState,
                                     "a queued request must report its `queued` record state");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("none", probe.queueHeadRequestWait,
                                     "a queued request must not be given a fictitious wait");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("none", probe.queueHeadRequestTerminal,
                                     "a queued request must not be given a fictitious terminal");
  }

  // (3) An in-flight job: the active instance owns the request and reports its phase and progress.
  RestoreHarness::runSingleLoadServiceTurn();
  {
    StorageManager::RestoreStateProbe probe{};
    StorageManager::probeRestoreState(probe);
    TEST_ASSERT_TRUE_MESSAGE(probe.activeJob || probe.parkedJob,
                             "the probe must see the request once a job instance owns it");
    const bool ownsSlot3 =
        (probe.activeJob && probe.activeJobTrack == 0 && probe.activeJobSlot == 3) ||
        (probe.parkedJob && probe.parkedJobTrack == 0 && probe.parkedJobSlot == 3);
    TEST_ASSERT_TRUE_MESSAGE(ownsSlot3, "the owning instance must name the request's (track, slot)");
    TEST_ASSERT_TRUE_MESSAGE(probe.activePayloadSize > 0 || probe.parkedPayloadSize > 0,
                             "an admitted job must report its payload size");
    const char* jobState =
        probe.activeJob ? probe.activeJobRequestState : probe.parkedJobRequestState;
    TEST_ASSERT_EQUAL_STRING_MESSAGE("running", jobState,
                                     "an admitted job's request is `running`, not a wait or terminal");
  }

  // (4) A blocked head on a slot with a live owner: named, and reported not startable.
  RestoreHarness::resetSlotToEmpty(0, 4);
  RestoreHarness::resetSlotToEmpty(0, 5);
  TrackCaptureStopTestAccess::latchAcceptedStopForSlot(trackManager.getSelectedTrack(),
                                                       PendingStopDestination::Playing, 4);
  TEST_ASSERT_FALSE(trackManager.getSelectedTrack().noLiveOwnerForSlot(4));
  StorageManagerInternal::clearPendingLoopSlotRestoresAtBoot();
  TEST_ASSERT_TRUE(StorageManagerInternal::appendBootLoopSlotRestore(0, 4, 0));
  TEST_ASSERT_TRUE(StorageManagerInternal::appendBootLoopSlotRestore(0, 5, 1));
  StorageManagerInternal::sortPendingLoopSlotRestoreQueue();
  {
    StorageManager::RestoreStateProbe probe{};
    StorageManager::probeRestoreState(probe);
    TEST_ASSERT_EQUAL_UINT16(2, probe.queuedCount);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(4, probe.queueHeadSlot,
                                    "the live-owned entry must be the reported priority head");
    TEST_ASSERT_FALSE_MESSAGE(probe.queueHeadStartable,
                              "a head whose slot has a live owner must report not startable");
  }
}

/// **A1.8 — the probe reports that the deferred save is held back by restore work.** This is the
/// observable side of C5: the drain's own gate reads the same predicate.
void test_restore_state_probe_reports_the_save_blocked_by_restore() {
  SD.reset();
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  RestoreHarness::setRuntimeTransportIdle();
  RestoreHarness::armBackgroundRestoreRequest(0, 6, 66);
  RestoreHarness::runSingleLoadServiceTurn();

  StorageManager::RestoreStateProbe probe{};
  StorageManager::probeRestoreState(probe);
  const bool hasSession = StorageManagerInternal::deferredSaveBlockedByActiveSlotLoadSd();
  TEST_ASSERT_EQUAL_MESSAGE(hasSession ? 1 : 0, probe.saveBlockedBySlotLoad ? 1 : 0,
                            "the probe and the drain's own gate must agree on the save blocker");
}

/// **A1.8 — the probe reports a prepare-complete wait by name.** Under active transport the job's
/// preparation is admitted (A2.2) but its commit is not, so the owning request must be reported
/// `waiting_commit_gate`: the state the drain's verdict and an operator both read.
void test_a18_probe_reports_prepare_complete_commit_gate_wait() {
  SD.reset();
  RestoreHarness::resetAllTracksAndPool();
  StorageManagerInternal::resetAllRestoreRequestState();
  RestoreHarness::setTrackAudiblyPlaying(1);  // transport active, focus track stopped

  RestoreHarness::armBackgroundRestoreRequest(0, 5, 68);
  bool commitPending = false;
  for (int turn = 0; turn < 32; ++turn) {
    RestoreHarness::runLoadServiceTurn();
    if (StorageManager::hasLoadLoopJobCommitPending()) {
      commitPending = true;
      break;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(commitPending, "the fixture needs a prepare-complete request");

  StorageManager::RestoreStateProbe probe{};
  StorageManager::probeRestoreState(probe);
  const char* wait = probe.activeJob ? probe.activeJobRequestWait : probe.parkedJobRequestWait;
  TEST_ASSERT_EQUAL_STRING_MESSAGE(
      "waiting_commit_gate", wait,
      "a prepare-complete request under transport must be reported in its named commit-gate wait");
}

/// **A1.8 — the probe reports a decided terminal.** A request that has published reports its terminal
/// (`committed`) rather than a wait, so "finished" and "still owed" are distinguishable from the probe.
void test_a18_probe_reports_a_decided_terminal() {
  SD.reset();
  RestoreHarness::resetAllTracksAndPool();
  StorageManagerInternal::resetAllRestoreRequestState();
  RestoreHarness::setRuntimeTransportIdle();

  RestoreHarness::armBackgroundRestoreRequest(0, 7, 67);
  bool published = false;
  for (int turn = 0; turn < 32 && !published; ++turn) {
    RestoreHarness::runLoadServiceTurn();
    published = trackManager.getSelectedTrack().getLoop(7).hasCommittedPasses();
  }
  TEST_ASSERT_TRUE_MESSAGE(published, "the fixture must reach publication");

  StorageManager::RestoreStateProbe probe{};
  StorageManager::probeRestoreState(probe);
  const char* terminal =
      probe.activeJob ? probe.activeJobRequestTerminal : probe.parkedJobRequestTerminal;
  TEST_ASSERT_EQUAL_STRING_MESSAGE(
      "committed", terminal,
      "the probe must report the decided terminal for the instance's request");
}

// ---------------------------------------------------------------------------
// Packet B / A2.1 — the restore request record: identity, state separation, exactly one terminal.
//
// These are **desired-behavior** cases over the A2.1 contract. The record and its API exist; what does
// not exist yet is the production wiring that drives every transition, so the cases below that require
// wiring are red and belong to the `RESTORE_LIVENESS_RED` set. The identity/state-separation cases that
// the record itself satisfies are default-gate.
// ---------------------------------------------------------------------------

/// **The production path drives the request state (A2.1 wiring).** A real queued restore begins
/// `queued`, is `running` while an admitted job owns it, and reaches its terminal on commit -- so the
/// state is the runtime's own, not a record only a fixture writes.
void test_restore_request_state_is_driven_by_the_production_path() {
  SD.reset();
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  StorageManagerInternal::resetAllRestoreRequestState();
  RestoreHarness::setRuntimeTransportIdle();

  RestoreHarness::armBackgroundRestoreRequest(0, 7, 68);
  TEST_ASSERT_EQUAL_INT_MESSAGE(
      static_cast<int>(StorageManagerInternal::RestoreRequestState::Queued),
      static_cast<int>(StorageManagerInternal::restoreRequestState(0, 7)),
      "the queue entry must be born `queued`");

  // One production turn: the request is selected into a job, which is `running`.
  RestoreHarness::runSingleLoadServiceTurn();
  const auto afterSelection = StorageManagerInternal::restoreRequestState(0, 7);
  TEST_ASSERT_TRUE_MESSAGE(
      afterSelection == StorageManagerInternal::RestoreRequestState::Running ||
          afterSelection == StorageManagerInternal::RestoreRequestState::Terminal,
      "a selected request must be `running` or already terminal, never still queued");

  // Drive to completion: the payload commits, so the request reaches its terminal.
  RestoreHarness::runLoadServiceTurns(64, RestoreHarness::restoreWorkIdle);
  TEST_ASSERT_TRUE_MESSAGE(trackManager.getSelectedTrack().getLoop(7).hasCommittedPasses(),
                           "the fixture must actually have restored the slot");
  TEST_ASSERT_EQUAL_INT_MESSAGE(
      static_cast<int>(StorageManagerInternal::RestoreRequestState::Terminal),
      static_cast<int>(StorageManagerInternal::restoreRequestState(0, 7)),
      "a committed restore must reach its terminal");
  TEST_ASSERT_EQUAL_STRING(
      "committed", StorageManagerInternal::restoreRequestTerminalName(
                       StorageManagerInternal::RestoreRequestTerminal::Committed));
}

/// **A denying gate names the wait (A2.1).** A queued request whose slot a live owner holds must be
/// reported as `waiting_slot_owned` -- a named wait, not a terminal and not an unexplained stall. This is
/// C4's shape, and the state is what the A1.8 probe and the drain's verdict will report.
void test_restore_request_slot_owned_denial_names_the_wait() {
  SD.reset();
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  StorageManagerInternal::resetAllRestoreRequestState();
  RestoreHarness::setRuntimeTransportIdle();

  // A live owner on slot 4, and a queued request for that uncommitted slot.
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  RestoreHarness::resetSlotToEmpty(0, 4);
  TrackCaptureStopTestAccess::latchAcceptedStopForSlot(track, PendingStopDestination::Playing, 4);
  TEST_ASSERT_FALSE(track.noLiveOwnerForSlot(4));
  TEST_ASSERT_TRUE(StorageManagerInternal::appendBootLoopSlotRestore(0, 4, 0));

  // One production selection turn: the slot is owned, so nothing may begin.
  RestoreHarness::runSingleLoadServiceTurn();
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, StorageManagerInternal::pendingLoopSlotRestores_.count,
                                   "the blocked request must stay queued");
  TEST_ASSERT_EQUAL_INT_MESSAGE(
      static_cast<int>(StorageManagerInternal::RestoreRequestState::Waiting),
      static_cast<int>(StorageManagerInternal::restoreRequestState(0, 4)),
      "a denial by a live owner must be a named wait, not a terminal and not still queued");
}

/// **A2.2 — a prepare-complete request under transport waits at the commit gate by name.** Preparation
/// is admitted while playback continues (A2.2), but applying it is not: the commit stop-gate is global
/// and bounded only by runtime-maintenance task 3.3, which this change does not absorb. The request must
/// therefore report `waiting_commit_gate`, not a terminal and not an unexplained stall.
void test_restore_request_prepare_complete_waits_at_commit_gate() {
  SD.reset();
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  StorageManagerInternal::resetAllRestoreRequestState();
  RestoreHarness::setTrackAudiblyPlaying(1);  // transport active, focus track stopped

  RestoreHarness::armBackgroundRestoreRequest(0, 2, 69);
  // Drive until the job has finished preparation. Under transport, commit is not admitted, so the
  // request must settle into the named commit-gate wait rather than committing or refusing.
  bool sawCommitPending = false;
  for (int turn = 0; turn < 32; ++turn) {
    RestoreHarness::runLoadServiceTurn();
    if (StorageManager::hasLoadLoopJobCommitPending()) {
      sawCommitPending = true;
      break;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(sawCommitPending,
                           "the fixture needs a prepare-complete (commit-pending) request");

  const auto state = StorageManagerInternal::restoreRequestState(0, 2);
  TEST_ASSERT_TRUE_MESSAGE(
      state == StorageManagerInternal::RestoreRequestState::Waiting ||
          state == StorageManagerInternal::RestoreRequestState::Terminal,
      "a prepare-complete request under transport must be waiting or terminal, never still queued");
  TEST_ASSERT_FALSE_MESSAGE(
      trackManager.getSelectedTrack().getLoop(2).hasCommittedPasses(),
      "the commit gate must not have been bypassed under active transport");
}

/// **A2.3 — priority order is preserved among startable requests, and a blocked entry is served once
/// its owner retires.** The startable pop walks the priority order and leaves the blocked entry queued,
/// so serving a lower-priority request must not reorder the rest.
void test_a23_startable_service_preserves_priority_order_and_releases_the_blocked_head() {
  SD.reset();
  RestoreHarness::resetAllTracksAndPool();
  StorageManagerInternal::resetAllRestoreRequestState();
  RestoreHarness::setRuntimeTransportIdle();

  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  for (uint8_t slot = 0; slot < 4; ++slot) {
    RestoreHarness::resetSlotToEmpty(0, slot);
  }
  // Slot 0 is blocked by a live owner; 1, 2 and 3 are startable, in that priority order.
  TrackCaptureStopTestAccess::latchAcceptedStopForSlot(track, PendingStopDestination::Playing, 0);
  for (uint8_t slot = 1; slot <= 3; ++slot) {
    RestoreHarness::resetSlotToEmpty(0, slot);
    TEST_ASSERT_TRUE(RestoreHarness::writePayloadWithOneNoteToSd(0, slot, static_cast<uint8_t>(70 + slot)));
    // The payload's note chunks are owned by the builder until the slot's snapshot releases them; this
    // fixture only needs the bytes on the card, so hand the pool back for the next slot.
    RestoreHarness::resetSlotToEmpty(0, slot);
  }
  TEST_ASSERT_TRUE(StorageManagerInternal::appendBootLoopSlotRestore(0, 0, 0));
  TEST_ASSERT_TRUE(StorageManagerInternal::appendBootLoopSlotRestore(0, 1, 1));
  TEST_ASSERT_TRUE(StorageManagerInternal::appendBootLoopSlotRestore(0, 2, 2));
  TEST_ASSERT_TRUE(StorageManagerInternal::appendBootLoopSlotRestore(0, 3, 3));
  StorageManagerInternal::sortPendingLoopSlotRestoreQueue();

  // The first service must take slot 1 (highest priority startable), not the blocked head.
  StorageManagerInternal::DeferredLoopSlotRestore first{};
  TEST_ASSERT_TRUE(StorageManagerInternal::popNextStartableDeferredLoopSlotRestore(first));
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, first.slot,
                                  "the first startable request in priority order must be selected");
  TEST_ASSERT_TRUE_MESSAGE(StorageManagerInternal::isDeferredLoopSlotRestoreQueued(0, 0),
                           "the blocked head must remain queued");
  StorageManagerInternal::DeferredLoopSlotRestore second{};
  TEST_ASSERT_TRUE(StorageManagerInternal::popNextStartableDeferredLoopSlotRestore(second));
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, second.slot, "priority order must be preserved among startable");
  StorageManagerInternal::DeferredLoopSlotRestore third{};
  TEST_ASSERT_TRUE(StorageManagerInternal::popNextStartableDeferredLoopSlotRestore(third));
  TEST_ASSERT_EQUAL_UINT8(3, third.slot);
  TEST_ASSERT_FALSE_MESSAGE(StorageManagerInternal::popNextStartableDeferredLoopSlotRestore(first),
                            "only the blocked head remains, so nothing is startable");
  TEST_ASSERT_TRUE(StorageManagerInternal::isDeferredLoopSlotRestoreQueued(0, 0));
}

/// **A2.3 non-claim, made observable (A2.2's `waiting_job_capacity`).** While one restore is
/// commit-pending, the commit gate is **global**: no other request's preparation advances either. That
/// state must be reported as the named job-capacity wait, and it is *not* described as queue starvation
/// — queue ordering cannot fix it.
void test_a23_second_request_reports_job_capacity_wait_while_one_is_commit_pending() {
  SD.reset();
  RestoreHarness::resetAllTracksAndPool();
  StorageManagerInternal::resetAllRestoreRequestState();
  RestoreHarness::setTrackAudiblyPlaying(1);

  // First request: drive under transport until its preparation completes and its commit is pending.
  RestoreHarness::armBackgroundRestoreRequest(0, 2, 71);
  bool commitPending = false;
  for (int turn = 0; turn < 32; ++turn) {
    RestoreHarness::runLoadServiceTurn();
    if (StorageManager::hasLoadLoopJobCommitPending()) {
      commitPending = true;
      break;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(commitPending, "the fixture needs one request commit-pending");

  // Second request: a separate startable slot. With the gate global, its preparation cannot advance.
  // The first request's parsed snapshot owns pool chunks, so hand the pool back before staging another.
  RestoreHarness::resetSlotToEmpty(0, 3);
  RestoreHarness::resetAllTracksAndPool();
  RestoreHarness::setTrackAudiblyPlaying(1);
  TEST_ASSERT_TRUE(RestoreHarness::writePayloadWithOneNoteToSd(0, 3, 72));
  TEST_ASSERT_TRUE(StorageManagerInternal::appendBootLoopSlotRestore(0, 3, 1));
  RestoreHarness::runSingleLoadServiceTurn();

  TEST_ASSERT_TRUE_MESSAGE(StorageManagerInternal::isDeferredLoopSlotRestoreQueued(0, 3),
                           "the second request must still be tracked, not dropped");
  // A2.1: the wait is **recorded on the blocked request**, not merely a name the enum can produce.
  TEST_ASSERT_EQUAL_INT_MESSAGE(
      static_cast<int>(StorageManagerInternal::RestoreRequestState::Waiting),
      static_cast<int>(StorageManagerInternal::restoreRequestState(0, 3)),
      "the sibling-commit denial must be a named wait, not still queued");
  TEST_ASSERT_EQUAL_STRING_MESSAGE(
      "waiting_job_capacity",
      StorageManagerInternal::restoreRequestWaitName(
          StorageManagerInternal::restoreRequestWaitFor(0, 3)),
      "the blocked sibling must report the global job-capacity gate");
}

/// A fresh request is **queued**, not terminal: "not yet served" and "finished" are different states,
/// which is the separation D1 requires.
void test_restore_request_starts_queued_and_is_not_a_terminal() {
  StorageManagerInternal::resetAllRestoreRequestState();
  StorageManagerInternal::beginRestoreRequest(0, 2);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StorageManagerInternal::RestoreRequestState::Queued),
                        static_cast<int>(StorageManagerInternal::restoreRequestState(0, 2)));
  TEST_ASSERT_TRUE(StorageManagerInternal::restoreRequestRecordIsCurrent(0, 2));
}

/// A running slice keeps the identity and is not given a fictitious wait reason.
void test_restore_request_running_keeps_identity_without_a_wait() {
  StorageManagerInternal::resetAllRestoreRequestState();
  StorageManagerInternal::beginRestoreRequest(0, 2);
  const uint32_t generation = StorageManagerInternal::restoreRequestGenerationFor(0, 2);
  StorageManagerInternal::markRestoreRequestRunning(0, 2);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StorageManagerInternal::RestoreRequestState::Running),
                        static_cast<int>(StorageManagerInternal::restoreRequestState(0, 2)));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(generation,
                                   StorageManagerInternal::restoreRequestGenerationFor(0, 2),
                                   "running work must retain the request identity");
}

/// A denying gate moves the request to **waiting** with a name, and that wait is not a terminal.
void test_restore_request_wait_is_named_and_not_terminal() {
  StorageManagerInternal::resetAllRestoreRequestState();
  StorageManagerInternal::beginRestoreRequest(0, 2);
  StorageManagerInternal::markRestoreRequestWaiting(0, 2,
                                                   StorageManagerInternal::RestoreRequestWait::CommitGate);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StorageManagerInternal::RestoreRequestState::Waiting),
                        static_cast<int>(StorageManagerInternal::restoreRequestState(0, 2)));
  TEST_ASSERT_EQUAL_STRING(
      "waiting_commit_gate",
      StorageManagerInternal::restoreRequestWaitName(
          StorageManagerInternal::RestoreRequestWait::CommitGate));
}

/// **Exactly one terminal per identity**: a second terminal for the same identity is refused, so the
/// first outcome is never overwritten by a later one.
void test_restore_request_records_exactly_one_terminal_per_identity() {
  StorageManagerInternal::resetAllRestoreRequestState();
  StorageManagerInternal::beginRestoreRequest(0, 2);
  TEST_ASSERT_TRUE(StorageManagerInternal::markRestoreRequestTerminal(
      0, 2, StorageManagerInternal::RestoreRequestTerminal::Committed));
  TEST_ASSERT_FALSE_MESSAGE(
      StorageManagerInternal::markRestoreRequestTerminal(
          0, 2, StorageManagerInternal::RestoreRequestTerminal::Superseded),
      "a second terminal for the same identity must be refused");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StorageManagerInternal::RestoreRequestState::Terminal),
                        static_cast<int>(StorageManagerInternal::restoreRequestState(0, 2)));
}

/// A reused `(track, slot)` is a **different request**: an external generation advance makes the old
/// record stale, so the earlier terminal cannot describe the new request.
void test_restore_request_reused_coordinates_are_a_new_request() {
  StorageManagerInternal::resetAllRestoreRequestState();
  StorageManagerInternal::beginRestoreRequest(0, 2);
  const uint32_t firstGeneration = StorageManagerInternal::restoreRequestGenerationFor(0, 2);
  TEST_ASSERT_TRUE(StorageManagerInternal::markRestoreRequestTerminal(
      0, 2, StorageManagerInternal::RestoreRequestTerminal::Committed));

  StorageManagerInternal::advanceRestoreRequestGeneration(0, 2);
  TEST_ASSERT_GREATER_THAN(firstGeneration,
                           StorageManagerInternal::restoreRequestGenerationFor(0, 2));
  TEST_ASSERT_FALSE_MESSAGE(StorageManagerInternal::restoreRequestRecordIsCurrent(0, 2),
                            "the earlier record must not describe the new request");
  // The done-test stays deliberately conservative after an external reset: a stale-but-decided record
  // still counts as "finished with", so a user CLEAR does not auto-reload the cleared slot (that route
  // reserves ~21.3 MB and trips the external-pool FATAL on device; recorded as a runtime-working-set
  // finding). A fresh explicit request still begins because `beginRestoreRequest` clears a stale record.
  TEST_ASSERT_TRUE_MESSAGE(StorageManagerInternal::isLoopSlotRestoreAttempted(0, 2),
                           "the done-test stays conservative for a stale terminal");
  StorageManagerInternal::beginRestoreRequest(0, 2);
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(StorageManagerInternal::RestoreRequestState::Queued),
                                static_cast<int>(StorageManagerInternal::restoreRequestState(0, 2)),
                                "the new request starts queued, not terminal");
}

/// A set change supersedes outstanding requests, and `superseded` is distinct from `refused`.
/// **S2b.8 strengthened:** the terminal is recorded under the identity each request was **created with**,
/// before the live epoch moves, so the outcome still describes that request rather than a fabricated one.
void test_restore_request_set_change_supersedes_outstanding() {
  StorageManagerInternal::resetAllRestoreRequestState();
  StorageManagerInternal::beginRestoreRequest(0, 2);
  StorageManagerInternal::beginRestoreRequest(1, 4);
  StorageManagerInternal::markRestoreRequestRunning(0, 2);
  const uint32_t originalEpoch = StorageManagerInternal::restoreSetEpoch();

  StorageManagerInternal::noteRestoreSetEpoch(7);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StorageManagerInternal::RestoreRequestState::Terminal),
                        static_cast<int>(StorageManagerInternal::restoreRequestState(0, 2)));
  TEST_ASSERT_EQUAL_STRING(
      "superseded", StorageManagerInternal::restoreRequestTerminalName(
                        StorageManagerInternal::RestoreRequestTerminal::Superseded));
  TEST_ASSERT_EQUAL_UINT32(7, StorageManagerInternal::restoreSetEpoch());
  // S2b.8: both the running request and the queued sibling reach `superseded`, and each record keeps the
  // epoch it was created under. The live epoch has moved, so neither record is current any more: an
  // in-flight job holding that identity is rejected before publication rather than publishing.
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StorageManagerInternal::RestoreRequestState::Terminal),
                        static_cast<int>(StorageManagerInternal::restoreRequestState(1, 4)));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(originalEpoch,
                                   StorageManagerInternal::restoreRequestEpochFor(0, 2),
                                   "the supersede must be recorded under the request's original epoch");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(originalEpoch,
                                   StorageManagerInternal::restoreRequestEpochFor(1, 4),
                                   "a queued sibling keeps its original epoch too");
  TEST_ASSERT_FALSE_MESSAGE(StorageManagerInternal::restoreRequestRecordIsCurrent(0, 2),
                            "the running request must not stay current across a set change");
  TEST_ASSERT_FALSE_MESSAGE(StorageManagerInternal::restoreRequestRecordIsCurrent(1, 4),
                            "the queued sibling must not stay current across a set change");
}

/// **S2b.8 — exactly once across a second set change.** A request already decided `superseded` must not
/// be re-decided by a later epoch, and its outcome must not be overwritten by a terminal the stale job
/// tries to record afterwards.
void test_s2b8_set_change_decides_a_request_exactly_once() {
  StorageManagerInternal::resetAllRestoreRequestState();
  StorageManagerInternal::beginRestoreRequest(0, 3);

  StorageManagerInternal::noteRestoreSetEpoch(7);
  StorageManagerInternal::noteRestoreSetEpoch(8);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(8, StorageManagerInternal::restoreSetEpoch(),
                                   "the second set change must still move the live epoch");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0,
                                   StorageManagerInternal::restoreRequestEpochFor(0, 3),
                                   "the decided record keeps the epoch it was created under");
  TEST_ASSERT_FALSE_MESSAGE(
      StorageManagerInternal::markRestoreRequestTerminal(
          0, 3, StorageManagerInternal::RestoreRequestTerminal::Committed),
      "a stale identity must not be able to record a second terminal");
  TEST_ASSERT_EQUAL_STRING_MESSAGE(
      "superseded",
      StorageManagerInternal::restoreRequestTerminalName(
          StorageManagerInternal::restoreRequestTerminalFor(0, 3)),
      "the first terminal must survive the later set change unchanged");
}

/// The restore path's **own** failure cleanup resets the slot but keeps the identity: the request goes to
/// its retry wait, never to `superseded`. This is the G2 tightening — a blind generation advance on every
/// reset would supersede the very request meant to retry.
void test_restore_request_own_failure_cleanup_keeps_identity_as_retry_wait() {
  StorageManagerInternal::resetAllRestoreRequestState();
  StorageManagerInternal::beginRestoreRequest(0, 2);
  const uint32_t generation = StorageManagerInternal::restoreRequestGenerationFor(0, 2);

  // The restore path's own cleanup: reset the slot, keep the identity, name the wait.
  StorageManagerInternal::resetLoopSlotToEmpty(trackManager.getSelectedTrack().getLoop(2), 2);
  StorageManagerInternal::markRestoreRequestWaiting(0, 2, StorageManagerInternal::RestoreRequestWait::Retry);

  TEST_ASSERT_EQUAL_UINT32_MESSAGE(generation,
                                   StorageManagerInternal::restoreRequestGenerationFor(0, 2),
                                   "the restore path's own cleanup must keep the identity");
  TEST_ASSERT_TRUE(StorageManagerInternal::restoreRequestRecordIsCurrent(0, 2));
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(StorageManagerInternal::RestoreRequestState::Waiting),
                                static_cast<int>(StorageManagerInternal::restoreRequestState(0, 2)),
                                "its own failed attempt is a retry wait, not superseded");
}

/// **A2.1 — recording names `waiting_capture`.** A queued background request whose preparation the
/// documented SD-session deferral denies must report that reason by name, not sit `queued` with no
/// explanation. This is the other half of the C1/C2 gate: transport alone no longer denies preparation
/// (A2.2), so capture is the remaining named denial.
void test_a21_recording_names_the_capture_wait() {
  SD.reset();
  RestoreHarness::resetAllTracksAndPool();
  StorageManagerInternal::resetAllRestoreRequestState();
  RestoreHarness::setRuntimeTransportIdle();

  // Arm the request while idle, then start recording: the request is owed and preparation is denied.
  RestoreHarness::armBackgroundRestoreRequest(0, 3, 74);
  RestoreHarness::setTrackRecording(1);
  RestoreHarness::runSingleLoadServiceTurn();

  TEST_ASSERT_TRUE_MESSAGE(StorageManagerInternal::isDeferredLoopSlotRestoreQueued(0, 3),
                           "the request must stay owed while capture defers it");
  TEST_ASSERT_EQUAL_INT_MESSAGE(
      static_cast<int>(StorageManagerInternal::RestoreRequestState::Waiting),
      static_cast<int>(StorageManagerInternal::restoreRequestState(0, 3)),
      "a capture denial must be a named wait, not a terminal and not still queued");
  TEST_ASSERT_EQUAL_STRING_MESSAGE(
      "waiting_capture",
      StorageManagerInternal::restoreRequestWaitName(
          StorageManagerInternal::restoreRequestWaitFor(0, 3)),
      "the recorded wait must name capture");
}

/// **A2.1 — the stale-identity rejection immediately before publication.** A job whose request an
/// external reset superseded while the parsed body was still in flight must publish **nothing**: the
/// slot may already hold a different owner's content, and the request it was serving has been decided.
/// The `superseded` terminal stays readable under the identity it was created with.
void test_a21_stale_identity_is_rejected_immediately_before_publication() {
  SD.reset();
  RestoreHarness::resetAllTracksAndPool();
  StorageManagerInternal::resetAllRestoreRequestState();
  RestoreHarness::setRuntimeTransportIdle();

  RestoreHarness::armBackgroundRestoreRequest(0, 6, 76);
  // Drive to the exact pre-publication window: the job is Committing (parsed, not yet applied).
  bool commitPending = false;
  for (int turn = 0; turn < 32; ++turn) {
    RestoreHarness::runLoadServiceTurn();
    if (StorageManager::hasLoadLoopJobCommitPending()) {
      commitPending = true;
      break;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(commitPending, "the fixture needs a prepared, unpublished body");
  TEST_ASSERT_FALSE_MESSAGE(trackManager.getSelectedTrack().getLoop(6).hasCommittedPasses(),
                            "the fixture must stop before publication");

  // An external reset of the coordinates: a user CLEAR / a different owner committing that slot.
  StorageManagerInternal::advanceRestoreRequestGeneration(0, 6);
  TEST_ASSERT_FALSE_MESSAGE(StorageManagerInternal::restoreRequestRecordIsCurrent(0, 6),
                            "the external reset must make the in-flight request stale");
  TEST_ASSERT_EQUAL_STRING_MESSAGE(
      "superseded",
      StorageManagerInternal::restoreRequestTerminalName(
          StorageManagerInternal::restoreRequestTerminalFor(0, 6)),
      "the external reset must record `superseded` under the request's own identity");

  // The commit step must reject the stale identity and publish nothing.
  RestoreHarness::runSingleLoadServiceTurn();
  TEST_ASSERT_FALSE_MESSAGE(trackManager.getSelectedTrack().getLoop(6).hasCommittedPasses(),
                            "a stale identity must publish NOTHING");
  TEST_ASSERT_FALSE_MESSAGE(StorageManager::hasLoadLoopJobCommitPending(),
                            "the rejected job must be released, not left commit-pending");
}

/// **S2b.8 — the set-change route rejects an in-flight publication too.** The same pre-publication window
/// as A2.1, invalidated by `noteRestoreSetEpoch` instead of a generation advance: a set change must also
/// publish nothing into the loaded slots, and the request must be readable as `superseded` under the
/// identity it was created with.
void test_s2b8_set_change_is_rejected_immediately_before_publication() {
  SD.reset();
  RestoreHarness::resetAllTracksAndPool();
  StorageManagerInternal::resetAllRestoreRequestState();
  RestoreHarness::setRuntimeTransportIdle();

  RestoreHarness::armBackgroundRestoreRequest(0, 7, 78);
  bool commitPending = false;
  for (int turn = 0; turn < 32; ++turn) {
    RestoreHarness::runLoadServiceTurn();
    if (StorageManager::hasLoadLoopJobCommitPending()) {
      commitPending = true;
      break;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(commitPending, "the fixture needs a prepared, unpublished body");
  TEST_ASSERT_FALSE_MESSAGE(trackManager.getSelectedTrack().getLoop(7).hasCommittedPasses(),
                            "the fixture must stop before publication");
  const uint32_t originalEpoch = StorageManagerInternal::restoreRequestEpochFor(0, 7);

  // The loaded current set changes while the parsed body is still in flight.
  StorageManagerInternal::noteRestoreSetEpoch(9);
  TEST_ASSERT_FALSE_MESSAGE(StorageManagerInternal::restoreRequestRecordIsCurrent(0, 7),
                            "the set change must make the in-flight request stale");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(originalEpoch,
                                   StorageManagerInternal::restoreRequestEpochFor(0, 7),
                                   "the terminal must be recorded under the request's original identity");
  TEST_ASSERT_EQUAL_STRING_MESSAGE(
      "superseded",
      StorageManagerInternal::restoreRequestTerminalName(
          StorageManagerInternal::restoreRequestTerminalFor(0, 7)),
      "a set change must record `superseded`, not `refused` or `committed`");

  RestoreHarness::runSingleLoadServiceTurn();
  TEST_ASSERT_FALSE_MESSAGE(trackManager.getSelectedTrack().getLoop(7).hasCommittedPasses(),
                            "a stale identity must publish NOTHING into a changed set");
  TEST_ASSERT_FALSE_MESSAGE(StorageManager::hasLoadLoopJobCommitPending(),
                            "the rejected job must be released, not left commit-pending");
}

// ---------------------------------------------------------------------------
// SD write-mode semantics (fix/sd-temp-rewrite, docs/Plans/sd_temp_write_truncation_refinement.md)
//
// The device's `SD.open(path, FILE_WRITE)` is `O_RDWR|O_CREAT|O_AT_END` -- it appends -- and
// `FILE_WRITE_BEGIN` is `O_RDWR|O_CREAT` with no `O_TRUNC`, so it only starts at offset 0. A temp
// file left behind by a failed write therefore corrupts the next one, which is how
// `MidiLooper/sets/index.bin` stopped reconciling on device. The in-memory card models both modes
// so this class is visible natively.
// ---------------------------------------------------------------------------

/// The fix: a rewrite must not inherit anything from a leftover body. This is the exact
/// `index.bin` mechanism -- `[nextSequence:u32][SAVE token:u32]`, with the token read from the END
/// of the file, so a surviving tail (or a stale prefix) makes the token land at the wrong offset.
void test_sd_rewrite_does_not_inherit_a_stale_temp_body() {
  const char* tempPath = "/MidiLooper/sets/index.bin.tmp";
  SD.reset();

  // A *failed* earlier write left the temp at a length that is not a whole record.
  SD.writeFile(tempPath, std::vector<uint8_t>{0xAA, 0xBB, 0xCC, 0xDD, 0xEE});
  TEST_ASSERT_EQUAL_UINT32(5, static_cast<uint32_t>(SD.files[tempPath].size()));

  {
    File file = CurrentSetStorage::openForRewrite(tempPath);
    TEST_ASSERT_TRUE(static_cast<bool>(file));
    const uint32_t nextSequence = 7;
    const uint32_t token = CurrentSetStorage::kSaveFileToken;
    TEST_ASSERT_EQUAL_UINT32(sizeof(nextSequence),
                             static_cast<uint32_t>(file.write(
                                 reinterpret_cast<const uint8_t*>(&nextSequence),
                                 sizeof(nextSequence))));
    TEST_ASSERT_EQUAL_UINT32(sizeof(token),
                             static_cast<uint32_t>(file.write(
                                 reinterpret_cast<const uint8_t*>(&token), sizeof(token))));
    file.close();
  }

  // Exactly one record: no stale prefix, no stale tail. Before the fix the mode appended (or left
  // the tail), so this failed at 13 and/or at the token check below.
  const size_t expectedSize = sizeof(uint32_t) * 2;
  char message[192];
  snprintf(message, sizeof(message),
           "a rewrite inherited the stale temp body: size=%u, expected %u",
           static_cast<unsigned>(SD.files[tempPath].size()),
           static_cast<unsigned>(expectedSize));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(expectedSize),
                           static_cast<uint32_t>(SD.files[tempPath].size()));
  TEST_ASSERT_TRUE_MESSAGE(CurrentSetStorage::verifySaveFileTokenAtPath(tempPath), message);

  // And the record still reads back as the value written.
  uint32_t readBack = 0;
  const std::vector<uint8_t>& body = SD.files[tempPath];
  std::memcpy(&readBack, body.data(), sizeof(readBack));
  TEST_ASSERT_EQUAL_UINT32(7, readBack);
}

/// The other half of the contract: the deliberate `FILE_WRITE` sites are **in-place patches** of an
/// existing file, so they must neither truncate nor append. This guards the fix from being widened
/// into them, and pins what `FILE_WRITE` has to mean for them to work.
void test_sd_in_place_patch_keeps_the_body_and_does_not_append() {
  const char* metaPath = "/MidiLooper/current/temp/runtime.bundle.bin";
  SD.reset();
  std::vector<uint8_t> body(CurrentSetStorage::kLastActiveUnixOffset + 64, 0x5A);
  SD.writeFile(metaPath, body);
  const size_t sizeBefore = SD.files[metaPath].size();

  TEST_ASSERT_TRUE(CurrentSetStorage::patchLastActiveUnix(metaPath, 0x11223344u));

  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(sizeBefore),
                           static_cast<uint32_t>(SD.files[metaPath].size()));
  uint32_t patched = 0;
  std::memcpy(&patched, SD.files[metaPath].data() + CurrentSetStorage::kLastActiveUnixOffset,
              sizeof(patched));
  TEST_ASSERT_EQUAL_UINT32(0x11223344u, patched);
  // The byte after the patch window is untouched: an append would have moved it.
  TEST_ASSERT_EQUAL_UINT8(0x5A, SD.files[metaPath][CurrentSetStorage::kLastActiveUnixOffset + 4]);
}


// ---------------------------------------------------------------------------
// Packet C / A3.4a — the single-owner drain guard (mechanical contract).
//
// The owner is a pure state holder, so this suite pins its **scope** contract directly: one owner at a
// time, a non-owning scope cannot release the owner, and the predicate reads false once the owner exits.
// The nested **drain entry point** refusal and the callback-boundary half (the real MIDI dispatch route)
// are pinned by `test_drain_reentry_callback_safety`, which links the drain owner.
// ---------------------------------------------------------------------------

/// One owner at a time: a nested scope must not acquire, and its destructor must not release the outer
/// scope's guard. This is the state a nested CLEAR's drain call sees.
void test_a34a_a_nested_scope_cannot_acquire_or_release_the_owner() {
  StorageManagerInternal::PersistenceDrainScope owner;
  TEST_ASSERT_TRUE_MESSAGE(owner.acquired(), "the first scope must take ownership");
  TEST_ASSERT_TRUE(StorageManagerInternal::isPersistenceDrainInProgress());
  {
    StorageManagerInternal::PersistenceDrainScope nested;
    TEST_ASSERT_FALSE_MESSAGE(nested.acquired(),
                              "a second scope must not take ownership while one is held");
  }
  TEST_ASSERT_TRUE_MESSAGE(StorageManagerInternal::isPersistenceDrainInProgress(),
                           "a non-owning scope's destructor must not release the outer guard");
}

/// The guard is released on every exit and a later drain can take it -- the shape a second CLEAR after a
/// refusal depends on.
void test_a34a_guard_is_released_and_a_later_drain_can_take_it() {
  TEST_ASSERT_FALSE(StorageManagerInternal::isPersistenceDrainInProgress());
  {
    StorageManagerInternal::PersistenceDrainScope first;
    TEST_ASSERT_TRUE(first.acquired());
    TEST_ASSERT_TRUE(StorageManagerInternal::isPersistenceDrainInProgress());
  }
  TEST_ASSERT_FALSE_MESSAGE(StorageManagerInternal::isPersistenceDrainInProgress(),
                            "the exit must release the guard whatever the outcome");
  {
    StorageManagerInternal::PersistenceDrainScope second;
    TEST_ASSERT_TRUE_MESSAGE(second.acquired(), "a later drain must be able to take the guard");
  }
  TEST_ASSERT_FALSE(StorageManagerInternal::isPersistenceDrainInProgress());
}

/// The predicate is false whenever no drain owns the state, which is what lets the newly guarded
/// destructive call sites fall through to their own logic instead of refusing everything.
void test_a34a_guard_predicate_is_false_without_an_owner() {
  TEST_ASSERT_FALSE(StorageManagerInternal::isPersistenceDrainInProgress());
  {
    StorageManagerInternal::PersistenceDrainScope owner;
    TEST_ASSERT_TRUE(owner.acquired());
    TEST_ASSERT_TRUE(StorageManagerInternal::isPersistenceDrainInProgress());
  }
  TEST_ASSERT_FALSE(StorageManagerInternal::isPersistenceDrainInProgress());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_restore_queue_orders_by_priority_and_peek_returns_the_head);
  RUN_TEST(test_restore_queue_peek_has_no_done_test_for_an_attempted_slot);
  RUN_TEST(test_restore_queue_forced_enqueue_bypasses_the_needs_slot_load_guard);
  RUN_TEST(test_b04_pop_then_requeue_loses_the_request_for_a_committed_slot);
  RUN_TEST(test_b04_real_load_selection_keeps_the_request_while_the_slot_is_owned);
  // A1.1 — the controllable restore harness smoke case (production path only).
  RUN_TEST(test_restore_harness_smoke_commits_a_queued_request_through_production_path);
  // A1.11's known-red acceptance set is now **empty**: each fixture was promoted into this default gate
  // in the commit that turned it green (A2.2 promoted C1/C2, A2.3 promoted C4, A2.4 promoted C6), so the
  // `RESTORE_LIVENESS_RED` flag is deleted. No red fixture was deleted, generalized or threshold-relaxed
  // to get here.
  RUN_TEST(test_c1_transport_gated_background_admission_desired);
  RUN_TEST(test_c2_parked_job_is_serviced_or_named_after_transport_starts);
  RUN_TEST(test_restore_request_prepare_complete_waits_at_commit_gate);
  // A2.3 promoted C4 in the slice that turned it green (A1.11). C6 is the last known-red fixture.
  RUN_TEST(test_c4_blocked_priority_head_does_not_stall_a_startable_request);
  RUN_TEST(test_a23_startable_service_preserves_priority_order_and_releases_the_blocked_head);
  RUN_TEST(test_a23_second_request_reports_job_capacity_wait_while_one_is_commit_pending);
  RUN_TEST(test_c6_transient_failure_is_retried_not_latched);
  // A1.8 — observability is default-gate: it is observation-only and passes on the current tree.
  RUN_TEST(test_restore_state_probe_reports_queue_and_job_ownership);
  RUN_TEST(test_restore_state_probe_reports_the_save_blocked_by_restore);
  RUN_TEST(test_a18_probe_reports_prepare_complete_commit_gate_wait);
  RUN_TEST(test_a18_probe_reports_a_decided_terminal);
  // A2.1 — the request record's identity/state contracts (default-gate; the record satisfies them).
  RUN_TEST(test_restore_request_state_is_driven_by_the_production_path);
  RUN_TEST(test_restore_request_slot_owned_denial_names_the_wait);
  RUN_TEST(test_restore_request_starts_queued_and_is_not_a_terminal);
  RUN_TEST(test_restore_request_running_keeps_identity_without_a_wait);
  RUN_TEST(test_restore_request_wait_is_named_and_not_terminal);
  RUN_TEST(test_restore_request_records_exactly_one_terminal_per_identity);
  RUN_TEST(test_restore_request_reused_coordinates_are_a_new_request);
  RUN_TEST(test_restore_request_set_change_supersedes_outstanding);
  RUN_TEST(test_s2b8_set_change_decides_a_request_exactly_once);
  RUN_TEST(test_restore_request_own_failure_cleanup_keeps_identity_as_retry_wait);
  RUN_TEST(test_a21_recording_names_the_capture_wait);
  RUN_TEST(test_a21_stale_identity_is_rejected_immediately_before_publication);
  RUN_TEST(test_s2b8_set_change_is_rejected_immediately_before_publication);
  // SD write-mode semantics: the stale-temp corruption fix, and the in-place patch contract.
  RUN_TEST(test_sd_rewrite_does_not_inherit_a_stale_temp_body);
  RUN_TEST(test_sd_in_place_patch_keeps_the_body_and_does_not_append);
  // Packet C / A3.4a — the single-owner drain guard (mechanical contract).
  RUN_TEST(test_a34a_a_nested_scope_cannot_acquire_or_release_the_owner);
  RUN_TEST(test_a34a_guard_is_released_and_a_later_drain_can_take_it);
  RUN_TEST(test_a34a_guard_predicate_is_false_without_an_owner);
  return UNITY_END();
}
