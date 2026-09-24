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
                       uint8_t hasCommittedContent, uint16_t, uint32_t, uint32_t, uint32_t,
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
  record.requiredForInitialPlayback = requiredForInitialPlayback;
  record.committedTerminalInWindow = committedTerminalInWindow;
  record.invalidated = invalidated;
  slots().push_back(record);
}

inline void end(uint32_t startUs, uint32_t endUs, uint16_t turns, uint16_t restarts,
                uint16_t slotsObserved, uint16_t payloadInventory, uint16_t initialPlaybackRequired,
                uint16_t observedRestoreCompletions, uint16_t committedAtCheckpoint,
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
                                    committedPassCount, highestCommittedPassId, loopLengthTicks, \
                                    committedEventCount, requiredForInitialPlayback, \
                                    committedTerminalInWindow, invalidated) \
  LibObsCapture::slotRecord(track, slot, state, cause, payload, offered, request, wait, terminal, \
                            requestGeneration, setEpoch, hasCommittedContent, committedPassCount, \
                            highestCommittedPassId, loopLengthTicks, committedEventCount, \
                            requiredForInitialPlayback, committedTerminalInWindow, invalidated)
#define SC_LIBRARY_OBSERVATION_END(windowStartUs, windowEndUs, turns, restarts, slotsObserved, \
                                   payloadInventory, initialPlaybackRequired,                  \
                                   observedRestoreCompletions, committedAtCheckpoint,          \
                                   admissionComplete, everyInitialPlaybackSlotObserved,        \
                                   consistentView, complete)                                   \
  LibObsCapture::end(windowStartUs, windowEndUs, turns, restarts, slotsObserved, payloadInventory, \
                     initialPlaybackRequired, observedRestoreCompletions, committedAtCheckpoint, \
                     admissionComplete, everyInitialPlaybackSlotObserved, consistentView, complete)

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

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_o02_observation_advances_while_load_admission_is_blocked);
  RUN_TEST(test_o01_permanently_waiting_request_still_yields_a_diagnostic);
  RUN_TEST(test_o03_collection_is_incremental_and_bounded);
  RUN_TEST(test_o04_generation_change_between_slices_invalidates_the_slot);
  RUN_TEST(test_o05_set_epoch_change_restarts_the_whole_observation);
  RUN_TEST(test_o06_publication_between_slices_invalidates_without_a_generation_change);
  RUN_TEST(test_o07_denied_sd_admission_degrades_fields_not_the_report);
  RUN_TEST(test_o08_observation_is_not_the_settled_census);
  return UNITY_END();
}
