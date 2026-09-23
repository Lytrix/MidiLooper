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
#include "../../src/StorageManager/LoopSlotRestoreQueue.cpp"
// The runtime's own explicit-request / background-fill entry points live beside the SD boot load, not
// in the load-job TU: without them the suite could only reach the queue through internal helpers.
#include "../../src/StorageManager.cpp"

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

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_restore_queue_orders_by_priority_and_peek_returns_the_head);
  RUN_TEST(test_restore_queue_peek_has_no_done_test_for_an_attempted_slot);
  RUN_TEST(test_restore_queue_forced_enqueue_bypasses_the_needs_slot_load_guard);
  RUN_TEST(test_b04_pop_then_requeue_loses_the_request_for_a_committed_slot);
  RUN_TEST(test_b04_real_load_selection_keeps_the_request_while_the_slot_is_owned);
  // A1.1 — the controllable restore harness smoke case (production path only).
  RUN_TEST(test_restore_harness_smoke_commits_a_queued_request_through_production_path);
  // A1.11 — the known-red desired-behavior acceptance set. Registered unconditionally so a run with
  // `RESTORE_LIVENESS_RED=1` shows exactly these cases red; excluded from the default gate until the
  // slice that turns each one green promotes it (the `WORKING_SET_RED` precedent).
  if (getenv("RESTORE_LIVENESS_RED") != nullptr) {
    RUN_TEST(test_c1_transport_gated_background_admission_desired);
    RUN_TEST(test_c2_parked_job_is_serviced_or_named_after_transport_starts);
  }
  // SD write-mode semantics: the stale-temp corruption fix, and the in-place patch contract.
  RUN_TEST(test_sd_rewrite_does_not_inherit_a_stale_temp_body);
  RUN_TEST(test_sd_in_place_patch_keeps_the_body_and_does_not_append);
  return UNITY_END();
}
