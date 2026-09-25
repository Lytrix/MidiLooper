//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// `restored-library-stability` Packet A / task 1.3 — the bounded on-demand library observation,
// exercised through the **actual service hook** (`StorageManager::serviceLibraryObservation()`) against
// the production request records, payload mirror and load admission, on the in-memory SD card.
//
// Oracle: `openspec/changes/restored-library-stability/ARCHITECTURE-REVIEW.md` § Packet for task 1.3
// (O01–O08). The per-invocation bounds (2 slots, 2 SD probes, 2 emitted records) are asserted directly,
// and the emitted `LIBOBS` records are captured by substituting the observation's three capture emitters
// before `Utils/DebugSessionCapture.h` is reached.

#define ARDUINO 1

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace LibObsCapture {

/// The wire form is compact (codes, not names) because the capture text cap is 192 bytes; see
/// `DebugSessionCapture::libraryObservationSlot`.
struct SlotRecord {
  uint8_t track = 0;
  uint8_t slot = 0;
  uint8_t stateMask = 0;
  char cause = 'u';
  uint8_t payload = 0;
  uint8_t offered = 0;
  uint8_t request = 0;
  uint8_t wait = 0;
  uint8_t terminal = 0;
  uint32_t generation = 0;
  uint32_t epoch = 0;
  uint8_t hasCommittedContent = 0;
  uint8_t hasPassContent = 0;
  uint8_t hasRootContent = 0;
  uint8_t rootIsCurrent = 0;
  uint8_t requiredForInitialPlayback = 0;
  uint8_t committedTerminalInWindow = 0;
  uint8_t invalidated = 0;
};

struct EndRecord {
  uint32_t startUs = 0;
  uint32_t endUs = 0;
  uint16_t turns = 0;
  uint16_t restarts = 0;
  uint16_t slotsObserved = 0;
  uint16_t payloadInventory = 0;
  uint16_t initialPlaybackRequired = 0;
  uint16_t observedRestoreCompletions = 0;
  uint16_t committedAtCheckpoint = 0;
  uint16_t passBackedAtCheckpoint = 0;
  uint16_t rootBackedAtCheckpoint = 0;
  uint8_t admissionComplete = 0;
  uint8_t everyInitialPlaybackSlotObserved = 0;
  uint8_t consistentView = 0;
  uint8_t complete = 0;
};

inline std::vector<SlotRecord>& slots() {
  static std::vector<SlotRecord> records;
  return records;
}

inline std::vector<EndRecord>& ends() {
  static std::vector<EndRecord> records;
  return records;
}

inline uint32_t& beginCount() {
  static uint32_t count = 0;
  return count;
}

inline void reset() {
  slots().clear();
  ends().clear();
  beginCount() = 0;
}

inline void begin(uint32_t, uint32_t, uint16_t) { ++beginCount(); }

inline void slotRecord(uint8_t track, uint8_t slotIndex, uint8_t stateMask, char cause,
                       uint8_t payload, uint8_t offered, uint8_t request, uint8_t wait,
                       uint8_t terminal, uint32_t generation, uint32_t epoch,
                       uint8_t hasCommittedContent, uint8_t hasPassContent, uint8_t hasRootContent,
                       uint8_t rootIsCurrent, uint16_t, uint32_t, uint32_t, uint32_t,
                       uint8_t requiredForInitialPlayback, uint8_t committedTerminalInWindow,
                       uint8_t invalidated) {
  SlotRecord record;
  record.track = track;
  record.slot = slotIndex;
  record.stateMask = stateMask;
  record.cause = cause;
  record.payload = payload;
  record.offered = offered;
  record.request = request;
  record.terminal = terminal;
  record.wait = wait;
  record.generation = generation;
  record.epoch = epoch;
  record.hasCommittedContent = hasCommittedContent;
  record.hasPassContent = hasPassContent;
  record.hasRootContent = hasRootContent;
  record.rootIsCurrent = rootIsCurrent;
  record.requiredForInitialPlayback = requiredForInitialPlayback;
  record.committedTerminalInWindow = committedTerminalInWindow;
  record.invalidated = invalidated;
  slots().push_back(record);
}

inline void end(uint32_t startUs, uint32_t endUs, uint16_t turns, uint16_t restarts,
                uint16_t slotsObserved, uint16_t payloadInventory, uint16_t initialPlaybackRequired,
                uint16_t observedRestoreCompletions, uint16_t committedAtCheckpoint,
                uint16_t passBackedAtCheckpoint, uint16_t rootBackedAtCheckpoint,
                uint8_t admissionComplete, uint8_t everyInitialPlaybackSlotObserved,
                uint8_t consistentView, uint8_t complete) {
  EndRecord record;
  record.startUs = startUs;
  record.endUs = endUs;
  record.turns = turns;
  record.restarts = restarts;
  record.slotsObserved = slotsObserved;
  record.payloadInventory = payloadInventory;
  record.initialPlaybackRequired = initialPlaybackRequired;
  record.observedRestoreCompletions = observedRestoreCompletions;
  record.committedAtCheckpoint = committedAtCheckpoint;
  record.passBackedAtCheckpoint = passBackedAtCheckpoint;
  record.rootBackedAtCheckpoint = rootBackedAtCheckpoint;
  record.admissionComplete = admissionComplete;
  record.everyInitialPlaybackSlotObserved = everyInitialPlaybackSlotObserved;
  record.consistentView = consistentView;
  record.complete = complete;
  ends().push_back(record);
}

inline const SlotRecord* findSlot(uint8_t track, uint8_t slotIndex) {
  for (const SlotRecord& record : slots()) {
    if (record.track == track && record.slot == slotIndex) {
      return &record;
    }
  }
  return nullptr;
}

}  // namespace LibObsCapture

// Substitute the observation's capture emitters before `Utils/DebugSessionCapture.h` is reached.
#define SC_LIBRARY_OBSERVATION_BEGIN(windowStartUs, setEpoch, slotCount) \
  LibObsCapture::begin(windowStartUs, setEpoch, slotCount)
#define SC_LIBRARY_OBSERVATION_SLOT(track, slot, state, cause, payload, offered, request, wait, \
                                    terminal, requestGeneration, setEpoch, hasCommittedContent, \
                                    hasPassContent, hasRootContent, rootIsCurrent,                 \
                                    committedPassCount, highestCommittedPassId, loopLengthTicks,  \
                                    committedEventCount, requiredForInitialPlayback,              \
                                    committedTerminalInWindow, invalidated)                       \
  LibObsCapture::slotRecord(track, slot, state, cause, payload, offered, request, wait, terminal,  \
                            requestGeneration, setEpoch, hasCommittedContent, hasPassContent,     \
                            hasRootContent, rootIsCurrent, committedPassCount,                     \
                            highestCommittedPassId, loopLengthTicks, committedEventCount,          \
                            requiredForInitialPlayback, committedTerminalInWindow, invalidated)
#define SC_LIBRARY_OBSERVATION_END(windowStartUs, windowEndUs, turns, restarts, slotsObserved, \
                                   payloadInventory, initialPlaybackRequired,                  \
                                   observedRestoreCompletions, committedAtCheckpoint,          \
                                   passBackedAtCheckpoint, rootBackedAtCheckpoint,             \
                                   admissionComplete, everyInitialPlaybackSlotObserved,        \
                                   consistentView, complete)                                   \
  LibObsCapture::end(windowStartUs, windowEndUs, turns, restarts, slotsObserved, payloadInventory, \
                     initialPlaybackRequired, observedRestoreCompletions, committedAtCheckpoint,   \
                     passBackedAtCheckpoint, rootBackedAtCheckpoint, admissionComplete,           \
                     everyInitialPlaybackSlotObserved, consistentView, complete)

#include "../test_support/firmware/Arduino.h"
#include "../test_support/firmware/SD.h"

SDClass SD;

/// The sketch owns this global; the load path reads it, so the harness provides it.
bool sequencerRunning = false;

#define HARNESS_OMITS_MIDI_BUTTON_ACTIONS
#include "../test_support/track_harness.inc"
#include "../test_storage_manager_load/load_harness_stubs.h"
#include "../test_storage_manager_load/load_and_restore_harness.h"
#include "../../src/TrackUndo.cpp"
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
#include "../../src/StorageManager.cpp"
#include "../../src/StorageManager/Internal.cpp"
#include "../../src/StorageManager/PersistenceDrainGuard.cpp"
#include "../../src/StorageManager/LibraryObservation.cpp"
#include "../../src/StorageManager/PersistenceWorkQueue.cpp"
#include "../../src/StorageManager/LibraryObservationSession.cpp"

namespace {

constexpr uint16_t kObservationSlotCount = Config::NUM_TRACKS * Config::MAX_LOOPS_PER_TRACK;

void resetObservationWorld() {
  SD.reset();
  // Release every track's committed content before rebuilding the pool; otherwise a commit left by an
  // earlier fixture makes a "no committed content" assertion read true before this fixture does anything.
  RestoreHarness::resetAllTracksAndPool();
  StorageManagerInternal::clearPendingLoopSlotRestoresAtBoot();
  StorageManagerInternal::resetAllLoopSlotRestoreAttempted();
  StorageManagerInternal::resetAllRestoreRequestState();
  StorageManagerInternal::clearLoadLoopJob();
  StorageManagerInternal::resetLibraryObservationState();
  LibObsCapture::reset();
}

/// Run the observation through the production service hook until it reports, bounded.
uint32_t runObservationToCompletion(uint32_t maxTurns = 4 * kObservationSlotCount) {
  uint32_t turns = 0;
  for (; turns < maxTurns; ++turns) {
    StorageManager::serviceLibraryObservation();
    if (StorageManagerInternal::libraryObservationView().reportReady) {
      return turns + 1;
    }
  }
  TEST_FAIL_MESSAGE("the observation did not report within its bounded turn budget");
  return turns;
}

/// Drive a submission to a prepare-complete, commit-pending job — the state `main` denies the load child
/// in. Returns true when the commit gate is holding.
bool reachCommitPendingLoadJob(uint8_t trackIndex, uint8_t slotIndex, uint8_t note) {
  RestoreHarness::armBackgroundRestoreRequest(trackIndex, slotIndex, note);
  for (int turn = 0; turn < 32; ++turn) {
    RestoreHarness::runLoadServiceTurn();
    if (StorageManager::hasLoadLoopJobCommitPending()) {
      return true;
    }
  }
  return false;
}

/// Step a snapshot construction to completion. Local to this suite so the fixture depends only on the
/// production construction API (the same loop `test_loop_event_store` uses).
bool drainSnapshotConstruction(LoopSnapshotConstruction& construction) {
  size_t steps = 0;
  while (!construction.complete() && !construction.failed() && steps < 10000) {
    if (!construction.step(1, 0)) {
      break;
    }
    ++steps;
  }
  return construction.complete() && !construction.failed();
}

/// Build a one-note immutable root for a loop, the shape the LSV1 restore installs. Uses the same
/// production construction pipeline the other Loop snapshot suites use (`LoopSnapshotConstruction` +
/// `drainSnapshotConstruction`), not a hand-built object.
ImmutableLoopSnapshotRef buildRootWithOneNote(LoopId loopId, uint32_t loopLength, uint8_t pitch) {
  auto empty = std::make_shared<const LoopSnapshot>(LoopSnapshot(loopId, loopLength, 0, 100, 100));
  LoopSnapshotChangeSet changes;
  SnapshotNoteChange add{};
  add.kind = SnapshotChangeKind::Add;
  add.next.loopId = loopId;
  add.next.noteId = 1;
  add.next.pitch = pitch;
  add.next.velocity = 100;
  add.next.startTick = 0;
  add.next.endTick = 100;
  changes.notes.push_back(add);
  LoopSnapshotConstruction construction(empty, changes);
  TEST_ASSERT_TRUE(drainSnapshotConstruction(construction));
  const ImmutableLoopSnapshotRef snapshot = construction.freeze();
  TEST_ASSERT_TRUE(static_cast<bool>(snapshot));
  return snapshot;
}

}  // namespace

void setUp() { resetObservationWorld(); }
void tearDown() {}

/// O02 — the observation advances at its declared service point while normal load admission is blocked.
void test_o02_observation_advances_while_load_admission_is_blocked() {
  RestoreHarness::setTrackAudiblyPlaying(1);  // transport active, focus track stopped
  TEST_ASSERT_TRUE_MESSAGE(reachCommitPendingLoadJob(0, 2, 69),
                           "the fixture needs a prepare-complete (commit-pending) request");
  TEST_ASSERT_TRUE(StorageManager::hasLoadLoopJobCommitPending());

  StorageManager::requestLibraryObservation();
  const StorageManagerInternal::LibraryObservationView before =
      StorageManagerInternal::libraryObservationView();
  TEST_ASSERT_EQUAL_UINT16(0, before.collected);

  StorageManager::serviceLibraryObservation();

  const StorageManagerInternal::LibraryObservationView after =
      StorageManagerInternal::libraryObservationView();
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(2, after.collected,
                                   "one invocation observes exactly the declared per-invocation bound");
  TEST_ASSERT_EQUAL_UINT8(2, after.lastInvocationSlots);
  TEST_ASSERT_TRUE_MESSAGE(StorageManager::hasLoadLoopJobCommitPending(),
                           "the load child is still denied: the observation did not relax the gate");
  TEST_ASSERT_TRUE(after.lastInvocationSlots > 0);
}

/// O01 — a permanently waiting request still yields a diagnostic, while the settled census cannot fire.
void test_o01_permanently_waiting_request_still_yields_a_diagnostic() {
  RestoreHarness::setTrackAudiblyPlaying(1);
  TEST_ASSERT_TRUE(reachCommitPendingLoadJob(0, 2, 69));

  StorageManager::requestLibraryObservation();
  runObservationToCompletion();

  TEST_ASSERT_TRUE(StorageManagerInternal::libraryObservationView().reportReady);
  // The settled census checkpoint's own readiness condition is false, which is exactly why the
  // on-demand observation exists.
  TEST_ASSERT_FALSE_MESSAGE(StorageManager::bootInteractiveReady(),
                            "the settled census cannot fire while the request is outstanding");

  const LibraryObservation::SlotResult& waiting =
      StorageManagerInternal::libraryObservationResultAt(0, 2);
  TEST_ASSERT_FALSE_MESSAGE(waiting.invalidated, "a named wait is not a generation change");
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LibraryObservation::Request::Waiting),
                          static_cast<uint8_t>(waiting.request));
  TEST_ASSERT_TRUE_MESSAGE(waiting.requestWait != 0, "the wait reason is named");
  TEST_ASSERT_TRUE_MESSAGE(
      (waiting.currentState & LibraryObservation::kStatePending) != 0,
      "the waiting slot's current state includes pending");
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(LibraryObservation::Terminal::None),
      static_cast<uint8_t>(waiting.terminal));

  // The report is emitted as its own record family.
  TEST_ASSERT_EQUAL_UINT32(1, LibObsCapture::beginCount());
  TEST_ASSERT_EQUAL_UINT32(1, LibObsCapture::ends().size());
  TEST_ASSERT_EQUAL_UINT16(kObservationSlotCount, LibObsCapture::ends()[0].slotsObserved);
}

/// O03 — the collection is incremental and bounded, and the partial result is marked with its turns.
void test_o03_collection_is_incremental_and_bounded() {
  RestoreHarness::setRuntimeTransportIdle();
  StorageManager::requestLibraryObservation();

  StorageManager::serviceLibraryObservation();
  const StorageManagerInternal::LibraryObservationView first =
      StorageManagerInternal::libraryObservationView();
  TEST_ASSERT_EQUAL_UINT16(2, first.slotsPerInvocation);
  TEST_ASSERT_EQUAL_UINT16(2, first.sdProbesPerInvocation);
  TEST_ASSERT_EQUAL_UINT16(2, first.collected);
  TEST_ASSERT_TRUE(first.lastInvocationSlots <= first.slotsPerInvocation);
  TEST_ASSERT_TRUE(first.lastInvocationSdProbes <= first.sdProbesPerInvocation);
  TEST_ASSERT_FALSE_MESSAGE(first.reportReady, "2 of 64 slots is a partial observation");

  runObservationToCompletion();
  const StorageManagerInternal::LibraryObservationView done =
      StorageManagerInternal::libraryObservationView();
  TEST_ASSERT_EQUAL_UINT16(kObservationSlotCount, done.collected);
  TEST_ASSERT_EQUAL_UINT16(kObservationSlotCount, done.emittedSlots);
  TEST_ASSERT_TRUE(done.reportReady);
  TEST_ASSERT_EQUAL_UINT32(1, LibObsCapture::ends().size());
  TEST_ASSERT_EQUAL_UINT16(kObservationSlotCount, LibObsCapture::ends()[0].slotsObserved);
  TEST_ASSERT_EQUAL_UINT32(kObservationSlotCount, LibObsCapture::slots().size());
}

/// O04 — a request-generation advance between slices invalidates that slot, with no mixed record.
void test_o04_generation_change_between_slices_invalidates_the_slot() {
  RestoreHarness::setRuntimeTransportIdle();
  TEST_ASSERT_TRUE(RestoreHarness::writePayloadWithOneNoteToSd(0, 0, 60));
  StorageManager::refreshLoopSlotPayloadOnSdInRam(0, 0);

  StorageManager::requestLibraryObservation();
  StorageManager::serviceLibraryObservation();  // observes (0,0) and (0,1)

  const LibraryObservation::SlotResult observed =
      StorageManagerInternal::libraryObservationResultAt(0, 0);
  TEST_ASSERT_FALSE(observed.invalidated);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(LibraryObservation::Payload::PresentVerified),
      static_cast<uint8_t>(observed.payload));

  StorageManagerInternal::advanceRestoreRequestGeneration(0, 0);
  runObservationToCompletion();

  const LibraryObservation::SlotResult& after =
      StorageManagerInternal::libraryObservationResultAt(0, 0);
  TEST_ASSERT_TRUE_MESSAGE(after.invalidated, "a generation advance invalidates the affected slot");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, after.currentState,
                                  "an invalidated record claims no observed state");
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(LibraryObservation::Payload::Unknown),
      static_cast<uint8_t>(after.payload));
  const LibObsCapture::SlotRecord* emitted = LibObsCapture::findSlot(0, 0);
  TEST_ASSERT_NOT_NULL(emitted);
  TEST_ASSERT_EQUAL_UINT8(1, emitted->invalidated);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, emitted->stateMask,
                                  "an invalidated record is emitted with an empty state set");
}

/// O05 — a set-epoch change invalidates the whole observation and restarts it under the new epoch.
void test_o05_set_epoch_change_restarts_the_whole_observation() {
  RestoreHarness::setRuntimeTransportIdle();
  StorageManager::requestLibraryObservation();
  StorageManager::serviceLibraryObservation();
  TEST_ASSERT_EQUAL_UINT16(2, StorageManagerInternal::libraryObservationView().collected);

  const uint32_t newEpoch = StorageManagerInternal::restoreSetEpoch() + 7;
  StorageManagerInternal::noteRestoreSetEpoch(newEpoch);
  StorageManager::serviceLibraryObservation();

  const StorageManagerInternal::LibraryObservationView restarted =
      StorageManagerInternal::libraryObservationView();
  TEST_ASSERT_EQUAL_UINT16(1, restarted.restarts);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(newEpoch, restarted.setEpochAtStart,
                                   "the restarted observation carries the new epoch");
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, restarted.collected,
                                   "a restart discards the records collected under the old epoch, so no "
                                   "slot record can mix fields from the two epochs");

  runObservationToCompletion();
  TEST_ASSERT_TRUE(StorageManagerInternal::libraryObservationView().reportReady);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(newEpoch,
                                   StorageManagerInternal::libraryObservationView().setEpochAtStart,
                                   "the completed observation is the one restarted under the new epoch");
  TEST_ASSERT_EQUAL_UINT32(1, LibObsCapture::ends().size());
  TEST_ASSERT_EQUAL_UINT16(1, LibObsCapture::ends()[0].restarts);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(kObservationSlotCount, LibObsCapture::ends()[0].slotsObserved,
                                   "the whole library was re-collected after the restart");
}

/// O06 — a publication between slices changes the content revision identity under an unchanged request
/// identity, and invalidates that slot.
void test_o06_publication_between_slices_invalidates_without_a_generation_change() {
  RestoreHarness::setRuntimeTransportIdle();
  TEST_ASSERT_TRUE(RestoreHarness::writePayloadWithOneNoteToSd(0, 0, 64));
  StorageManager::refreshLoopSlotPayloadOnSdInRam(0, 0);

  StorageManager::requestLibraryObservation();
  StorageManager::serviceLibraryObservation();  // observes (0,0) with no committed content

  const uint32_t generationBefore = StorageManagerInternal::restoreRequestGenerationFor(0, 0);
  const uint32_t epochBefore = StorageManagerInternal::restoreSetEpoch();
  const uint32_t revisionBefore =
      StorageManagerInternal::libraryObservationResultAt(0, 0).hasCommittedContent
          ? 1u
          : 0u;
  TEST_ASSERT_EQUAL_UINT32(0, revisionBefore);

  // Publish through the production restore path: committed content appears with **no** generation or
  // epoch change.
  RestoreHarness::armRestoreRequestThroughProductionQueue(0, 0);
  for (int turn = 0; turn < 64; ++turn) {
    RestoreHarness::runLoadServiceTurn();
    if (trackManager.getTrack(0).getLoop(0).hasCommittedPasses()) {
      break;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(trackManager.getTrack(0).getLoop(0).hasCommittedPasses(),
                           "the fixture needs the publication to land");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(generationBefore,
                                   StorageManagerInternal::restoreRequestGenerationFor(0, 0),
                                   "a publication must not advance the request generation");
  TEST_ASSERT_EQUAL_UINT32(epochBefore, StorageManagerInternal::restoreSetEpoch());

  runObservationToCompletion();

  const LibraryObservation::SlotResult& after =
      StorageManagerInternal::libraryObservationResultAt(0, 0);
  TEST_ASSERT_TRUE_MESSAGE(after.invalidated,
                           "the content revision changed between slices, so the record is invalidated");
  TEST_ASSERT_FALSE_MESSAGE(after.hasCommittedContent,
                            "no record combines pre-publication fields with post-publication content");
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(LibraryObservation::Request::Unknown),
      static_cast<uint8_t>(after.request));
  const LibObsCapture::SlotRecord* emitted = LibObsCapture::findSlot(0, 0);
  TEST_ASSERT_NOT_NULL(emitted);
  TEST_ASSERT_EQUAL_UINT8(1, emitted->invalidated);
}

/// O07 — denied SD admission degrades the SD-requiring fields to `unknown`, not the report.
void test_o07_denied_sd_admission_degrades_fields_not_the_report() {
  RestoreHarness::setRuntimeTransportIdle();
  TEST_ASSERT_TRUE(RestoreHarness::writePayloadWithOneNoteToSd(0, 0, 60));
  StorageManager::refreshLoopSlotPayloadOnSdInRam(0, 0);
  // Capture excludes the SD session, so the token cannot be verified.
  RestoreHarness::setTrackRecording(1);

  StorageManager::requestLibraryObservation();
  runObservationToCompletion();

  TEST_ASSERT_TRUE_MESSAGE(StorageManagerInternal::libraryObservationView().reportReady,
                           "the report is still produced");
  const LibraryObservation::SlotResult& degraded =
      StorageManagerInternal::libraryObservationResultAt(0, 0);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      static_cast<uint8_t>(LibraryObservation::Payload::Unknown),
      static_cast<uint8_t>(degraded.payload),
      "a present payload whose token cannot be verified is unknown, not absent or corrupt");
  TEST_ASSERT_FALSE_MESSAGE(
      degraded.evidence.payloadValidationFailed,
      "a denied read is not evidence of a validation failure");
  TEST_ASSERT_NOT_EQUAL(
      static_cast<uint8_t>(LibraryObservation::Cause::ContentLoss),
      static_cast<uint8_t>(degraded.cause));
  TEST_ASSERT_EQUAL_UINT32(1, LibObsCapture::ends().size());
}

/// O08 — the observation is a distinct record family and is never the settled census measurement.
void test_o08_observation_is_not_the_settled_census() {
  RestoreHarness::setRuntimeTransportIdle();
  StorageManager::requestLibraryObservation();
  runObservationToCompletion();

  // The declared four quantities are separate fields of its own `end` record.
  TEST_ASSERT_EQUAL_UINT32(1, LibObsCapture::beginCount());
  TEST_ASSERT_EQUAL_UINT32(1, LibObsCapture::ends().size());
  const LibObsCapture::EndRecord& end = LibObsCapture::ends()[0];
  // The native clock is host-driven, so the window is a pair of readings, not a positive duration; the
  // framing is asserted through its turns and completeness fields instead.
  TEST_ASSERT_EQUAL_UINT32(0, end.endUs < end.startUs ? 1 : 0);
  TEST_ASSERT_TRUE(end.turns >= 1);
  // A transport-idle fixture has no initial-playback payloads and no committed content.
  TEST_ASSERT_EQUAL_UINT16(0, end.payloadInventory);
  TEST_ASSERT_EQUAL_UINT16(0, end.observedRestoreCompletions);
  TEST_ASSERT_EQUAL_UINT16(0, end.committedAtCheckpoint);
  // The settled census' own gates are irrelevant to this observation: it reports even though the
  // background admission scan has not completed in this fixture.
  TEST_ASSERT_EQUAL_UINT8(0, end.admissionComplete);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, end.complete,
                                  "an incomplete window is reported incomplete, never settled");
}

/// O09 — a **published-root-backed** slot is content, and replacing that root between slices
/// invalidates the record under an unchanged request identity **and** an unchanged pass identity.
///
/// This is the representation gap's own case: the LSV1 restore installs the content as a published root
/// and leaves `Loop::passes` empty, so before the correction the slot reported `has_committed_content=0`
/// and a root replacement was invisible to the pass-derived content revision.
void test_o09_root_content_and_root_replacement_between_slices() {
  RestoreHarness::setRuntimeTransportIdle();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  Loop& loop = track.getLoop(0);

  const ImmutableLoopSnapshotRef firstRoot =
      buildRootWithOneNote(loop.loopId, 7680, 60);
  loop.storePublishedSnapshotForTest(firstRoot, 0);
  TEST_ASSERT_TRUE_MESSAGE(loop.publishedSnapshotIsCurrent(),
                           "the fixture needs a current published root");
  TEST_ASSERT_FALSE_MESSAGE(loop.hasCommittedPasses(),
                            "an LSV1 root leaves the pass representation empty");

  StorageManager::requestLibraryObservation();
  StorageManager::serviceLibraryObservation();  // observes (0,0) and (0,1)

  const LibraryObservation::SlotResult observed =
      StorageManagerInternal::libraryObservationResultAt(0, 0);
  TEST_ASSERT_FALSE_MESSAGE(observed.invalidated, "the observation was clean at this slice");
  TEST_ASSERT_TRUE_MESSAGE(observed.hasRootContent,
                           "a current published root is content and is reported as root-backed");
  TEST_ASSERT_TRUE_MESSAGE(observed.rootIsCurrent, "the fixture's root covers the live pass state");
  TEST_ASSERT_FALSE(observed.hasPassContent);
  TEST_ASSERT_TRUE_MESSAGE(observed.hasCommittedContent,
                           "the union content signal includes the root representation");
  TEST_ASSERT_TRUE_MESSAGE((observed.currentState & LibraryObservation::kStatePopulated) != 0,
                           "a root-backed slot is populated, not empty");

  // Replace the root without touching the pass representation or the request identity.
  const ImmutableLoopSnapshotRef secondRoot =
      buildRootWithOneNote(loop.loopId, 7680, 67);
  loop.storePublishedSnapshotForTest(secondRoot, loop.playbackRevision);
  TEST_ASSERT_NOT_EQUAL(firstRoot.get(), secondRoot.get());
  TEST_ASSERT_FALSE_MESSAGE(loop.hasCommittedPasses(), "the pass identity is still empty");

  runObservationToCompletion();

  const LibraryObservation::SlotResult& after =
      StorageManagerInternal::libraryObservationResultAt(0, 0);
  TEST_ASSERT_TRUE_MESSAGE(after.invalidated,
                           "the published-root identity changed, so the record is invalidated");
  TEST_ASSERT_FALSE_MESSAGE(after.hasCommittedContent,
                            "no record combines the pre-replacement fields with the new root");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, after.currentState,
                                  "an invalidated record claims no observed state");
  const LibObsCapture::SlotRecord* emitted = LibObsCapture::findSlot(0, 0);
  TEST_ASSERT_NOT_NULL(emitted);
  TEST_ASSERT_EQUAL_UINT8(1, emitted->invalidated);
  TEST_ASSERT_EQUAL_UINT8(0, emitted->hasRootContent);
}

/// O10 — the end record reports the loaded population as the **union** of the two representations,
/// beside both of them separately. One pass-backed slot and one root-backed slot yield a union of two
/// and one of each representation, so the union is the loaded count and neither representation is
/// mistaken for it.
void test_o10_end_record_reports_union_and_both_representations() {
  RestoreHarness::setRuntimeTransportIdle();
  trackManager.getTrack(0).ensureLoopsAllocated();

  // (0,0): pass-backed, through the production load path.
  TEST_ASSERT_TRUE(RestoreHarness::writePayloadWithOneNoteToSd(0, 0, 60));
  StorageManager::refreshLoopSlotPayloadOnSdInRam(0, 0);
  StorageManager::requestLoopSlotRestoreFromSd(0, 0);
  for (int turn = 0; turn < 64; ++turn) {
    RestoreHarness::runLoadServiceTurn();
    if (trackManager.getTrack(0).getLoop(0).hasCommittedPasses()) {
      break;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(trackManager.getTrack(0).getLoop(0).hasCommittedPasses(),
                           "the fixture needs the legacy payload to publish passes");

  // (0,1): root-backed, the LSV1 representation.
  Loop& rootOnly = trackManager.getTrack(0).getLoop(1);
  rootOnly.storePublishedSnapshotForTest(buildRootWithOneNote(rootOnly.loopId, 3072, 62), 0);
  TEST_ASSERT_TRUE(rootOnly.publishedSnapshotIsCurrent());
  TEST_ASSERT_FALSE(rootOnly.hasCommittedPasses());

  // (0,2): an **installed but not current** root — a stale root is still content (playback reads it
  // through the stale-root overlay), and it must still make the slot populated.
  Loop& staleRoot = trackManager.getTrack(0).getLoop(2);
  staleRoot.storePublishedSnapshotForTest(buildRootWithOneNote(staleRoot.loopId, 3072, 65), 0,
                                          kSnapshotCoverageUnset);
  TEST_ASSERT_FALSE_MESSAGE(staleRoot.publishedSnapshotIsCurrent(),
                            "the fixture needs a root that does not cover the live pass state");
  TEST_ASSERT_TRUE(static_cast<bool>(staleRoot.publishedSnapshot()));

  StorageManager::requestLibraryObservation();
  runObservationToCompletion();

  // Read the three slots the fixture controls by name, so the expected union is exact.
  const LibraryObservation::SlotResult& passBacked =
      StorageManagerInternal::libraryObservationResultAt(0, 0);
  const LibraryObservation::SlotResult& currentRoot =
      StorageManagerInternal::libraryObservationResultAt(0, 1);
  const LibraryObservation::SlotResult& staleRootResult =
      StorageManagerInternal::libraryObservationResultAt(0, 2);

  TEST_ASSERT_TRUE(passBacked.hasPassContent);
  TEST_ASSERT_FALSE(passBacked.hasRootContent);

  TEST_ASSERT_TRUE_MESSAGE(currentRoot.hasRootContent, "an installed root is root-backed content");
  TEST_ASSERT_TRUE_MESSAGE(currentRoot.rootIsCurrent, "this root covers the live pass state");
  TEST_ASSERT_FALSE(currentRoot.hasPassContent);

  TEST_ASSERT_TRUE_MESSAGE(staleRootResult.hasRootContent,
                           "an installed-but-stale root is still content, not an empty slot");
  TEST_ASSERT_FALSE_MESSAGE(staleRootResult.rootIsCurrent,
                            "the stale root is reported as not current, beside the content bit");
  TEST_ASSERT_TRUE_MESSAGE(
      (staleRootResult.currentState & LibraryObservation::kStatePopulated) != 0,
      "a stale-root slot is populated, not empty");

  TEST_ASSERT_EQUAL_UINT32(1, LibObsCapture::ends().size());
  const LibObsCapture::EndRecord& end = LibObsCapture::ends()[0];
  // This fixture produces exactly three content-bearing slots: one pass-backed, two root-backed
  // (one of which overlaps nothing), so the union is three and each representation is one/two.
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(3, end.committedAtCheckpoint,
                                   "the loaded count is the union of the two representations");
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, end.passBackedAtCheckpoint,
                                   "the pass-backed representation is reported separately");
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(2, end.rootBackedAtCheckpoint,
                                   "the published-root representation is reported separately");
  TEST_ASSERT_EQUAL_UINT16(end.committedAtCheckpoint,
                           end.passBackedAtCheckpoint + end.rootBackedAtCheckpoint);
}

void test_slot_payload_pages_are_bounded_read_only_and_admitted() {
  nativeTransportRunning = false;
  TEST_ASSERT_TRUE(RestoreHarness::writePayloadWithOneNoteToSd(0, 0, 60));
  char path[64];
  TEST_ASSERT_TRUE(CurrentSetStorage::formatLoopSlotPath(path, sizeof(path), 0, 0));
  const auto original = SD.files[path];
  const size_t chunksBefore = LoopEventStore::freeChunkCount();
  StorageManager::SlotPayloadPage page;
  std::vector<uint8_t> bytes;
  for (uint32_t offset = 0; offset < original.size();) {
    TEST_ASSERT_EQUAL_STRING("ok", StorageManager::readSlotPayloadPageForHitl(0, 0, offset, page));
    TEST_ASSERT_TRUE(page.count > 0 && page.count <= 64);
    TEST_ASSERT_EQUAL(original.size(), page.fileSize);
    bytes.insert(bytes.end(), page.bytes, page.bytes + page.count);
    offset += page.count;
  }
  TEST_ASSERT_EQUAL_MEMORY(original.data(), bytes.data(), original.size());
  TEST_ASSERT_EQUAL_MEMORY(original.data(), SD.files[path].data(), original.size());
  TEST_ASSERT_EQUAL(chunksBefore, LoopEventStore::freeChunkCount());
  TEST_ASSERT_FALSE(trackManager.getTrack(0).getLoop(0).hasCommittedPasses());
  TEST_ASSERT_EQUAL_STRING("range", StorageManager::readSlotPayloadPageForHitl(0, 0, 0xffffffffu, page));
  TEST_ASSERT_EQUAL_STRING("invalid_slot", StorageManager::readSlotPayloadPageForHitl(8, 0, 0, page));
  TEST_ASSERT_EQUAL_STRING("missing", StorageManager::readSlotPayloadPageForHitl(0, 7, 0, page));
  nativeTransportRunning = true;
  TEST_ASSERT_EQUAL_STRING("transport_active", StorageManager::readSlotPayloadPageForHitl(0, 0, 0, page));
  nativeTransportRunning = false;
  { SlotLoadSession load(0, 1);
    TEST_ASSERT_EQUAL_STRING("busy", StorageManager::readSlotPayloadPageForHitl(0, 0, 0, page)); }
  TEST_ASSERT_TRUE(PersistenceWorkQueue::admitWork(PersistWorkType::LoopPersist, persistKeyForSlot(0, 0)));
  TEST_ASSERT_EQUAL_STRING("busy", StorageManager::readSlotPayloadPageForHitl(0, 0, 0, page));
  PersistenceWorkQueue::resetForTests();
  SD.files[path].back() ^= 1;
  TEST_ASSERT_EQUAL_STRING("bad_token", StorageManager::readSlotPayloadPageForHitl(0, 0, 0, page));
  TEST_ASSERT_FALSE(trackManager.getTrack(0).getLoop(0).hasCommittedPasses());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_slot_payload_pages_are_bounded_read_only_and_admitted);
  RUN_TEST(test_o02_observation_advances_while_load_admission_is_blocked);
  RUN_TEST(test_o01_permanently_waiting_request_still_yields_a_diagnostic);
  RUN_TEST(test_o03_collection_is_incremental_and_bounded);
  RUN_TEST(test_o04_generation_change_between_slices_invalidates_the_slot);
  RUN_TEST(test_o05_set_epoch_change_restarts_the_whole_observation);
  RUN_TEST(test_o06_publication_between_slices_invalidates_without_a_generation_change);
  RUN_TEST(test_o07_denied_sd_admission_degrades_fields_not_the_report);
  RUN_TEST(test_o08_observation_is_not_the_settled_census);
  RUN_TEST(test_o09_root_content_and_root_replacement_between_slices);
  RUN_TEST(test_o10_end_record_reports_union_and_both_representations);
  return UNITY_END();
}
