//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>
#include <cstdio>
#include <map>
#include <random>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/EditManager/EditApply.cpp"
#include "../../src/Track/RuntimeNotePairLookup.cpp"
#include "../../src/Track/RuntimeCompanionOverlapWalk.cpp"
#include "../../src/Track/RuntimeCompanionSpanWalk.cpp"
#include "../../src/Track/RuntimeCompanionReconstruction.cpp"
#include "../../src/Track/RuntimeCompanionApply.cpp"
#include "../../src/Track/RuntimeCompanionBridgeMap.cpp"
#include "../../src/Track/RuntimeCompanionPasses.cpp"
#include "../../src/Track/RuntimeCanonicalPassInputs.cpp"
#include "../../src/Track/RuntimeCaptureCompanions.cpp"
#include "../../src/Track/RuntimePlaybackView.cpp"
#include "../../src/Track/RuntimeCaptureStaging.cpp"
#include "../../src/Track/RuntimePlaybackOrderBuilder.cpp"
#include "../../src/Loop/LoopPasses.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../test_support/MemoryMonitorNativeDeps.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/Loop.cpp"
#include "../test_support/LoopCaptureTestDeps.cpp"
#include "../test_support/NoteEditFocusTestDeps.cpp"
#include "../../src/EditManager/NoteEditSessionUndoStack.cpp"

#include "EditApply.h"
#include "EditPass.h"
#include "Loop.h"
#include "LoopPasses.h"
#include "LoopEventBuffer.h"
#include "PendingNoteChange.h"
#include "EditSession.h"
#include "OverlapNoteIdObservation.h"
#include "NoteEditCurrentState.h"
#include "NoteEditFocus.h"
#include "NoteEditSessionState.h"
#include "../test_support/NoteIdTestFixtures.h"
#include "../test_support/CommittedChunkIdTestHelpers.h"

namespace {

using namespace NoteIdTestFixtures;

RecordPass makeRecordPassWithNote(PassId id, uint32_t tick, uint8_t channel = 1) {
  resetNoteIdCounter();
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, tick, channel, 60, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(tick + 10, channel, 60, 0)));
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, committedChunkIds));
  RecordPass pass{};
  pass.id = id;
  pass.state = CapturePassState::Active;
  pass.committedChunkIds = std::move(committedChunkIds);
  return pass;
}

OverdubPass makeOverdubPassWithNote(PassId id, uint32_t tick, uint8_t pitch) {
  resetNoteIdCounter();
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, tick, 1, pitch, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(tick + 10, 1, pitch, 0)));
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, committedChunkIds));
  OverdubPass pass{};
  pass.id = id;
  pass.mergeSequence = 1;
  pass.state = CapturePassState::Active;
  pass.committedChunkIds = std::move(committedChunkIds);
  return pass;
}

int countNoteOns(const MidiEventVec& flat, uint8_t pitch) {
  int count = 0;
  for (const MidiEvent& evt : flat) {
    if (evt.isNoteOn() && evt.data.noteData.note == pitch) {
      ++count;
    }
  }
  return count;
}

RecordPass makeRecordPassWithTwoNotes(PassId id, uint32_t startA, uint32_t endA, uint32_t startB,
                                      uint32_t endB, uint8_t pitch) {
  resetNoteIdCounter();
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, startA, 1, pitch, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(endA, 1, pitch, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, startB, 1, pitch, 100, 2));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(endB, 1, pitch, 0)));
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, committedChunkIds));
  RecordPass pass{};
  pass.id = id;
  pass.state = CapturePassState::Active;
  pass.committedChunkIds = std::move(committedChunkIds);
  return pass;
}

static void appendFixtureNotePair(LoopEventStore& store, uint32_t onTick, uint32_t offTick,
                                  uint8_t channel, uint8_t pitch, NoteId noteId) {
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, onTick, channel, pitch, 100, noteId));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(offTick, channel, pitch, 0)));
}

RecordPass makeRecordPassWithUnidentifiedNote(PassId id, uint32_t start, uint32_t end, uint8_t pitch,
                                             uint8_t channel = 5) {
  LoopEventStore store;
  MidiEvent on = MidiEvent::NoteOn(start, channel, pitch, 100);
  on.noteId = kInvalidNoteId;
  TEST_ASSERT_TRUE(store.append(on));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(end, channel, pitch, 0)));
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, committedChunkIds));
  RecordPass pass{};
  pass.id = id;
  pass.state = CapturePassState::Active;
  pass.committedChunkIds = std::move(committedChunkIds);
  return pass;
}

NoteId noteIdAtPitchAndStart(const MidiEventVec& flat, uint8_t pitch, uint32_t startTick) {
  for (const MidiEvent& evt : flat) {
    if (evt.isNoteOn() && evt.data.noteData.note == pitch && evt.tick == startTick) {
      return evt.noteId;
    }
  }
  return kInvalidNoteId;
}

RecordPass makeRecordPassWithIdentifiedNote(PassId id, uint32_t start, uint32_t end, uint8_t pitch,
                                           NoteId noteId, uint8_t channel = 5) {
  resetNoteIdCounter();
  LoopEventStore store;
  appendFixtureNotePair(store, start, end, channel, pitch, noteId);
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, committedChunkIds));
  RecordPass pass{};
  pass.id = id;
  pass.state = CapturePassState::Active;
  pass.committedChunkIds = std::move(committedChunkIds);
  return pass;
}

RecordPass makeEditRecordFixtureRecordPass(PassId id) {
  resetNoteIdCounter();
  LoopEventStore store;
  appendFixtureNotePair(store, 8, 104, 1, 60, 1);
  appendFixtureNotePair(store, 201, 297, 1, 64, 2);
  appendFixtureNotePair(store, 392, 488, 1, 67, 3);
  appendFixtureNotePair(store, 584, 680, 1, 60, 4);
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, committedChunkIds));
  RecordPass pass{};
  pass.id = id;
  pass.state = CapturePassState::Active;
  pass.committedChunkIds = std::move(committedChunkIds);
  return pass;
}

RecordPass makeEditRecordFixtureRecordPassCh5(PassId id) {
  resetNoteIdCounter();
  LoopEventStore store;
  appendFixtureNotePair(store, 8, 104, 5, 60, 1);
  appendFixtureNotePair(store, 585, 680, 5, 60, 2);
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, committedChunkIds));
  RecordPass pass{};
  pass.id = id;
  pass.state = CapturePassState::Active;
  pass.committedChunkIds = std::move(committedChunkIds);
  return pass;
}

RecordPass makeLengthReplayLoopBoundaryRecordPass(PassId id) {
  resetNoteIdCounter();
  LoopEventStore store;
  appendFixtureNotePair(store, 0, 143, 1, 71, 1);
  appendFixtureNotePair(store, 720, 766, 1, 71, 2);
  appendFixtureNotePair(store, 1488, 1535, 1, 71, 3);
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 2256, 1, 71, 100, 14));
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, committedChunkIds));
  RecordPass pass{};
  pass.id = id;
  pass.state = CapturePassState::Active;
  pass.committedChunkIds = std::move(committedChunkIds);
  return pass;
}

RecordPass makeEditRecordFixtureRecordPassCh5_195830(PassId id) {
  resetNoteIdCounter();
  LoopEventStore store;
  appendFixtureNotePair(store, 40, 136, 5, 60, 1);
  appendFixtureNotePair(store, 232, 328, 5, 64, 2);
  appendFixtureNotePair(store, 424, 520, 5, 67, 3);
  appendFixtureNotePair(store, 616, 712, 5, 60, 4);
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, committedChunkIds));
  RecordPass pass{};
  pass.id = id;
  pass.state = CapturePassState::Active;
  pass.committedChunkIds = std::move(committedChunkIds);
  return pass;
}

EditPass makeDeleteRow(NoteId targetNoteId) {
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = EditActionType::Delete;
  row.propertyType = EditPropertyType::None;
  row.targetNoteId = targetNoteId;
  return row;
}

EditPass makeLengthRow(NoteId targetNoteId, uint32_t start, uint32_t end, uint32_t newEnd) {
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = EditActionType::Update;
  row.propertyType = EditPropertyType::Length;
  row.targetNoteId = targetNoteId;
  row.startTick = start;
  row.endTick = newEnd;
  return row;
}

EditPass makePitchRow(NoteId targetNoteId, uint32_t start, uint32_t end, uint8_t pitch) {
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = EditActionType::Update;
  row.propertyType = EditPropertyType::Pitch;
  row.targetNoteId = targetNoteId;
  row.startTick = start;
  row.endTick = end;
  row.pitch = pitch;
  return row;
}

EditPass makeVelocityRow(NoteId targetNoteId, uint32_t start, uint32_t end, uint8_t velocity) {
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = EditActionType::Update;
  row.propertyType = EditPropertyType::Velocity;
  row.targetNoteId = targetNoteId;
  row.startTick = start;
  row.endTick = end;
  row.velocity = velocity;
  return row;
}

EditPass makeNoteRangeRow(NoteId targetNoteId, uint32_t baseStart, uint32_t baseEnd,
                          uint32_t newStart, uint32_t newEnd) {
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = EditActionType::Update;
  row.propertyType = EditPropertyType::NoteRange;
  row.targetNoteId = targetNoteId;
  row.startTick = newStart;
  row.endTick = newEnd;
  return row;
}

void pushEditPassRow(LoopPasses& passes, EditPassId id, EditPass row) {
  row.id = id;
  row.editPassIndex = 0;
  row.state = EditPassState::Active;
  passes.editPasses.push_back(std::move(row));
}

void pushEditPassRows(LoopPasses& passes, EditPassId baseId, EditPassVec rows) {
  EditPassId id = baseId;
  for (EditPass row : rows) {
    row.id = id++;
    row.editPassIndex = 0;
    row.state = EditPassState::Active;
    passes.editPasses.push_back(std::move(row));
  }
}

template <typename Alloc>
int countMatching(const std::vector<MidiEvent, Alloc>& flat, bool wantOn, uint8_t pitch,
                  uint32_t tick) {
  int n = 0;
  for (const MidiEvent& e : flat) {
    const bool isOn = e.isNoteOn();
    if (isOn == wantOn && e.data.noteData.note == pitch && e.tick == tick) {
      n++;
    }
  }
  return n;
}

MidiEvent sequenceNoteOnWithId(uint32_t tick, uint8_t channel, uint8_t pitch, uint8_t velocity,
                               NoteId noteId) {
  MidiEvent evt = MidiEvent::NoteOn(tick, channel, pitch, velocity);
  evt.noteId = noteId;
  return evt;
}

EditPass makeCreateRow(NoteId noteId, uint32_t startTick, uint32_t endTick, uint8_t channel,
                       uint8_t pitch, uint8_t velocity) {
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = EditActionType::Create;
  row.propertyType = EditPropertyType::None;
  row.targetNoteId = noteId;
  row.addedEvents.push_back(sequenceNoteOnWithId(startTick, channel, pitch, velocity, noteId));
  row.addedEvents.push_back(MidiEvent::NoteOff(endTick, channel, pitch, 0));
  return row;
}

MidiEventVec makeSequenceParityFixtureEvents() {
  MidiEventVec events;
  events.push_back(sequenceNoteOnWithId(96, 1, 60, 90, 10));
  events.push_back(MidiEvent::NoteOff(192, 1, 60, 0));
  events.push_back(sequenceNoteOnWithId(256, 1, 62, 88, 20));
  events.push_back(MidiEvent::NoteOff(352, 1, 62, 0));
  events.push_back(sequenceNoteOnWithId(448, 1, 64, 86, 30));
  events.push_back(MidiEvent::NoteOff(560, 1, 64, 0));
  return events;
}

void applyNoteEditPassSequenceBaselineNoCollapse(MidiEventVec& events, const EditPassVec& rows,
                                                 uint32_t loopLengthTicks) {
  std::vector<NoteId> presentNoteIds;
  collectPresentNoteIds(events, presentNoteIds);
  for (const EditPass& row : rows) {
    if (row.actionType == EditActionType::Delete ||
        row.actionType == EditActionType::Update) {
      if (!containsPresentNoteId(presentNoteIds, row.targetNoteId)) {
        continue;
      }
    }
    applyNoteEditPass(events, row, loopLengthTicks);
    switch (row.actionType) {
      case EditActionType::Delete:
        erasePresentNoteId(presentNoteIds, row.targetNoteId);
        break;
      case EditActionType::Create:
        for (const MidiEvent& evt : row.addedEvents) {
          if (evt.isNoteOn()) {
            insertPresentNoteId(presentNoteIds, evt.noteId);
          }
        }
        break;
      case EditActionType::Update:
        break;
    }
  }
}

bool midiEventEquivalentForTest(const MidiEvent& a, const MidiEvent& b) {
  if (a.type != b.type || a.channel != b.channel || a.tick != b.tick || a.noteId != b.noteId) {
    return false;
  }
  if (a.isNoteOn() || a.isNoteOff()) {
    return a.data.noteData.note == b.data.noteData.note &&
           a.data.noteData.velocity == b.data.noteData.velocity;
  }
  if (a.type == midi::ControlChange) {
    return a.data.ccData.cc == b.data.ccData.cc &&
           a.data.ccData.value == b.data.ccData.value;
  }
  return true;
}

void assertMidiEventVecEquivalent(const MidiEventVec& expected, const MidiEventVec& actual,
                                  const char* context) {
  if (expected.size() != actual.size()) {
    char msg[192];
    snprintf(msg, sizeof(msg), "%s: size mismatch expected=%u actual=%u", context,
             static_cast<unsigned>(expected.size()), static_cast<unsigned>(actual.size()));
    TEST_FAIL_MESSAGE(msg);
  }
  for (size_t i = 0; i < expected.size(); ++i) {
    if (!midiEventEquivalentForTest(expected[i], actual[i])) {
      char msg[192];
      snprintf(msg, sizeof(msg), "%s: mismatch at index %u", context,
               static_cast<unsigned>(i));
      TEST_FAIL_MESSAGE(msg);
    }
  }
}

constexpr uint32_t kCompanionProjectionLoopLength = 1024;
constexpr NoteId kCompanionTargetNoteId = 101;
constexpr NoteId kCompanionOverlapNoteId = 102;
constexpr NoteId kCompanionDistantOverlapNoteId = 103;
constexpr NoteId kCompanionDifferentPitchNoteId = 104;
constexpr NoteId kCompanionWrapNoteId = 105;
constexpr NoteId kCompanionMixedTargetNoteId = 201;
constexpr NoteId kCompanionMixedOverlapNoteId = 202;
constexpr NoteId kCompanionMixedDifferentPitchNoteId = 203;
constexpr NoteId kCompanionMixedWrapNoteId = 204;

void appendCompanionProjectionNoteDetailed(MidiEventVec& events, NoteId noteId, uint32_t startTick,
                                           uint32_t endTick, uint8_t channel, uint8_t pitch,
                                           uint8_t velocity) {
  events.push_back(sequenceNoteOnWithId(startTick, channel, pitch, velocity, noteId));
  events.push_back(MidiEvent::NoteOff(endTick, channel, pitch, 0));
}

void appendCompanionProjectionNote(MidiEventVec& events, NoteId noteId, uint32_t startTick,
                                   uint32_t endTick, uint8_t pitch) {
  appendCompanionProjectionNoteDetailed(events, noteId, startTick, endTick, 1, pitch, 100);
}

MidiEventVec makeCompanionProjectionInitialEvents() {
  MidiEventVec events;
  appendCompanionProjectionNote(events, kCompanionTargetNoteId, 96, 320, 60);
  appendCompanionProjectionNote(events, kCompanionOverlapNoteId, 224, 448, 60);
  appendCompanionProjectionNote(events, kCompanionDistantOverlapNoteId, 400, 560, 60);
  appendCompanionProjectionNote(events, kCompanionDifferentPitchNoteId, 128, 352, 64);
  appendCompanionProjectionNote(events, kCompanionWrapNoteId, 880, 80, 60);
  return events;
}

MidiEventVec makeCompanionProjectionMixedChannelVelocityInitialEvents() {
  MidiEventVec events;
  appendCompanionProjectionNoteDetailed(events, kCompanionMixedTargetNoteId, 96, 320, 1, 60, 100);
  appendCompanionProjectionNoteDetailed(events, kCompanionMixedOverlapNoteId, 224, 448, 2, 60, 84);
  appendCompanionProjectionNoteDetailed(events, kCompanionMixedDifferentPitchNoteId, 128, 352, 3, 64,
                                        112);
  appendCompanionProjectionNoteDetailed(events, kCompanionMixedWrapNoteId, 880, 80, 2, 60, 72);
  return events;
}

EditPass makeCompanionDeleteRow(NoteId targetNoteId) {
  EditPass row = makeDeleteRow(targetNoteId);
  row.editPassIndex = kOverdubCompanionEditPassIndex;
  return row;
}

EditPass makeCompanionLengthRow(NoteId targetNoteId, uint32_t newEndTick) {
  EditPass row = makeLengthRow(targetNoteId, 0, 0, newEndTick);
  row.editPassIndex = kOverdubCompanionEditPassIndex;
  return row;
}

void assignCompanionHistoryIds(EditPassVec& rows, EditPassId firstId = 1000) {
  EditPassId nextId = firstId;
  for (EditPass& row : rows) {
    row.id = nextId++;
    row.editPassIndex = kOverdubCompanionEditPassIndex;
    row.state = EditPassState::Active;
  }
}

EditPassVec companionHistoryPrefix(const EditPassVec& history, size_t count) {
  EditPassVec prefix;
  prefix.reserve(count);
  for (size_t i = 0; i < count && i < history.size(); ++i) {
    prefix.push_back(history[i]);
  }
  return prefix;
}

EditPassVec makeCompanionRandomizedHistoryScenario(std::mt19937& rng, EditPassId firstId) {
  const NoteId targetIds[] = {
      kCompanionTargetNoteId,
      kCompanionOverlapNoteId,
      kCompanionDistantOverlapNoteId,
      kCompanionDifferentPitchNoteId,
      kCompanionWrapNoteId,
  };
  std::uniform_int_distribution<int> targetDist(0, 4);
  std::uniform_int_distribution<int> actionDist(0, 99);
  std::uniform_int_distribution<int> endTickDist(
      1, static_cast<int>(kCompanionProjectionLoopLength - 1));
  std::uniform_int_distribution<int> rowCountDist(8, 32);

  EditPassVec history;
  history.reserve(40);
  history.push_back(makeCompanionLengthRow(kCompanionTargetNoteId, 300));
  history.push_back(makeCompanionLengthRow(kCompanionTargetNoteId, 600));
  history.push_back(makeCompanionLengthRow(kCompanionTargetNoteId, 220));
  history.push_back(makeCompanionLengthRow(kCompanionWrapNoteId, 64));
  history.push_back(makeCompanionDeleteRow(kCompanionOverlapNoteId));
  history.push_back(makeCompanionLengthRow(kCompanionOverlapNoteId, 500));
  history.push_back(makeCompanionLengthRow(kCompanionDistantOverlapNoteId, 300));
  history.push_back(makeCompanionDeleteRow(kCompanionDistantOverlapNoteId));

  const int randomRowCount = rowCountDist(rng);
  for (int i = 0; i < randomRowCount; ++i) {
    const NoteId targetId = targetIds[targetDist(rng)];
    if (actionDist(rng) < 28) {
      history.push_back(makeCompanionDeleteRow(targetId));
    } else {
      history.push_back(
          makeCompanionLengthRow(targetId, static_cast<uint32_t>(endTickDist(rng))));
    }
  }

  assignCompanionHistoryIds(history, firstId);
  return history;
}

struct CompanionNoteState {
  bool present = false;
  uint8_t pitch = 0;
  uint8_t velocity = 0;
  uint32_t startTick = 0;
  uint32_t endTick = 0;
};

CompanionNoteState companionNoteState(const MidiEventVec& events, NoteId noteId) {
  CompanionNoteState state;
  const int onIndex = findNoteOnById(events, noteId);
  if (onIndex < 0) {
    return state;
  }
  state.present = true;
  const MidiEvent& on = events[static_cast<size_t>(onIndex)];
  state.pitch = on.data.noteData.note;
  state.velocity = on.data.noteData.velocity;
  state.startTick = on.tick;
  const int offIndex = findNoteOffForOnIndex(events, onIndex);
  state.endTick =
      (offIndex >= 0) ? events[static_cast<size_t>(offIndex)].tick : state.startTick;
  return state;
}

bool companionNoteStateEquals(const CompanionNoteState& expected,
                              const CompanionNoteState& actual) {
  return expected.present == actual.present && expected.pitch == actual.pitch &&
         expected.velocity == actual.velocity && expected.startTick == actual.startTick &&
         expected.endTick == actual.endTick;
}

void assertCompanionEffectiveStateEquivalent(const MidiEventVec& expected,
                                             const MidiEventVec& actual, uint32_t loopLengthTicks,
                                             const char* context) {
  assertMidiEventVecEquivalent(expected, actual, context);

  const NoteUtils::DisplayNoteVec expectedNotes =
      NoteUtils::reconstructDisplayNotes(expected, loopLengthTicks, false, false);
  const NoteUtils::DisplayNoteVec actualNotes =
      NoteUtils::reconstructDisplayNotes(actual, loopLengthTicks, false, false);
  if (expectedNotes.size() != actualNotes.size()) {
    char message[192];
    snprintf(message, sizeof(message), "%s: display geometry size mismatch expected=%u actual=%u",
             context, static_cast<unsigned>(expectedNotes.size()),
             static_cast<unsigned>(actualNotes.size()));
    TEST_FAIL_MESSAGE(message);
  }
  for (size_t i = 0; i < expectedNotes.size(); ++i) {
    const NoteUtils::DisplayNote& expectedNote = expectedNotes[i];
    const NoteUtils::DisplayNote& actualNote = actualNotes[i];
    if (expectedNote.noteId != actualNote.noteId || expectedNote.note != actualNote.note ||
        expectedNote.velocity != actualNote.velocity ||
        expectedNote.startTick != actualNote.startTick ||
        expectedNote.endTick != actualNote.endTick) {
      char message[192];
      snprintf(message, sizeof(message), "%s: display geometry mismatch at index %u", context,
               static_cast<unsigned>(i));
      TEST_FAIL_MESSAGE(message);
    }
  }
}

void assertCompanionHistoryIncrementalEquivalence(const MidiEventVec& initialEvents,
                                                  const EditPassVec& history,
                                                  uint32_t loopLengthTicks,
                                                  const char* context) {
  MidiEventVec incrementalState = initialEvents;
  for (size_t cursor = 0; cursor <= history.size(); ++cursor) {
    const EditPassVec prefix = companionHistoryPrefix(history, cursor);
    MidiEventVec historicalState = initialEvents;
    applyNoteEditPassSequence(historicalState, prefix, loopLengthTicks);

    if (cursor > 0) {
      applyNoteEditPass(incrementalState, history[cursor - 1], loopLengthTicks);
    }

    char stepContext[192];
    snprintf(stepContext, sizeof(stepContext), "%s generation=%u", context,
             static_cast<unsigned>(cursor));
    assertCompanionEffectiveStateEquivalent(historicalState, incrementalState, loopLengthTicks,
                                            stepContext);
  }

  // The incremental state is also retained at each generation so undo/redo can be
  // checked without changing the content rows or replay contract.
  std::vector<MidiEventVec> generationStates;
  generationStates.reserve(history.size() + 1);
  generationStates.push_back(initialEvents);
  for (const EditPass& row : history) {
    MidiEventVec nextState = generationStates.back();
    applyNoteEditPass(nextState, row, loopLengthTicks);
    generationStates.push_back(std::move(nextState));
  }

  for (size_t cursor = history.size(); cursor > 0; --cursor) {
    const EditPassVec prefix = companionHistoryPrefix(history, cursor - 1);
    MidiEventVec historicalState = initialEvents;
    applyNoteEditPassSequence(historicalState, prefix, loopLengthTicks);
    char stepContext[192];
    snprintf(stepContext, sizeof(stepContext), "%s undo-to-generation=%u", context,
             static_cast<unsigned>(cursor - 1));
    assertCompanionEffectiveStateEquivalent(historicalState, generationStates[cursor - 1],
                                            loopLengthTicks, stepContext);
  }

  for (size_t cursor = 0; cursor < history.size(); ++cursor) {
    const EditPassVec prefix = companionHistoryPrefix(history, cursor + 1);
    MidiEventVec historicalState = initialEvents;
    applyNoteEditPassSequence(historicalState, prefix, loopLengthTicks);
    char stepContext[192];
    snprintf(stepContext, sizeof(stepContext), "%s redo-to-generation=%u", context,
             static_cast<unsigned>(cursor + 1));
    assertCompanionEffectiveStateEquivalent(historicalState, generationStates[cursor + 1],
                                            loopLengthTicks, stepContext);
  }
}

void printCompanionAffectedGeometry(const char* operation, NoteId targetNoteId,
                                    const MidiEventVec& before, const MidiEventVec& after) {
  const NoteId noteIds[] = {
      kCompanionTargetNoteId,          kCompanionOverlapNoteId,
      kCompanionDistantOverlapNoteId,  kCompanionDifferentPitchNoteId,
      kCompanionWrapNoteId,
  };
  std::printf("companion_delta,operation=%s,target=%lu,affected=",
              operation, static_cast<unsigned long>(targetNoteId));
  bool first = true;
  for (const NoteId noteId : noteIds) {
    if (companionNoteStateEquals(companionNoteState(before, noteId),
                                 companionNoteState(after, noteId))) {
      continue;
    }
    if (!first) {
      std::printf("|");
    }
    std::printf("%lu", static_cast<unsigned long>(noteId));
    first = false;
  }
  std::printf("\n");
}

CompanionNoteState companionNoteState(const NoteUtils::DisplayNoteVec& notes, NoteId noteId) {
  CompanionNoteState state;
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.noteId != noteId) {
      continue;
    }
    state.present = true;
    state.pitch = note.note;
    state.velocity = note.velocity;
    state.startTick = note.startTick;
    state.endTick = note.endTick;
    break;
  }
  return state;
}

struct ReducedCompanionState {
  NoteUtils::DisplayNoteVec notes;

  static ReducedCompanionState fromEvents(const MidiEventVec& events, uint32_t loopLengthTicks) {
    ReducedCompanionState state;
    state.notes = NoteUtils::reconstructDisplayNotes(events, loopLengthTicks, false, false);
    return state;
  }

  MidiEventVec materializeEvents() const {
    MidiEventVec events;
    events.reserve(notes.size() * 2u);
    constexpr uint8_t kChannel = 1;
    for (const NoteUtils::DisplayNote& note : notes) {
      if (note.noteId == kInvalidNoteId) {
        continue;
      }
      events.push_back(
          sequenceNoteOnWithId(note.startTick, kChannel, note.note, note.velocity, note.noteId));
      events.push_back(MidiEvent::NoteOff(note.endTick, kChannel, note.note, 0));
    }
    return events;
  }

  void applyCompanionDelta(const EditPass& row, uint32_t loopLengthTicks) {
    if (row.actionType != EditActionType::Delete &&
        !(row.actionType == EditActionType::Update &&
          row.propertyType == EditPropertyType::Length)) {
      return;
    }
    MidiEventVec events = materializeEvents();
    applyNoteEditPass(events, row, loopLengthTicks);
    notes = NoteUtils::reconstructDisplayNotes(events, loopLengthTicks, false, false);
  }
};

NoteUtils::DisplayNoteVec canonicalCompanionGeometry(const NoteUtils::DisplayNoteVec& notes) {
  NoteUtils::DisplayNoteVec canonical;
  canonical.reserve(notes.size());
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.noteId == kInvalidNoteId) {
      continue;
    }
    canonical.push_back(note);
  }
  std::stable_sort(canonical.begin(), canonical.end(),
                   [](const NoteUtils::DisplayNote& a, const NoteUtils::DisplayNote& b) {
                     if (a.noteId != b.noteId) return a.noteId < b.noteId;
                     if (a.note != b.note) return a.note < b.note;
                     if (a.startTick != b.startTick) return a.startTick < b.startTick;
                     if (a.endTick != b.endTick) return a.endTick < b.endTick;
                     return a.velocity < b.velocity;
                   });
  return canonical;
}

bool reducedCompanionGeometryEquivalent(const MidiEventVec& expectedEvents,
                                        const ReducedCompanionState& reducedState,
                                        uint32_t loopLengthTicks) {
  const NoteUtils::DisplayNoteVec expected = canonicalCompanionGeometry(
      NoteUtils::reconstructDisplayNotes(expectedEvents, loopLengthTicks, false, false));
  const NoteUtils::DisplayNoteVec actual = canonicalCompanionGeometry(reducedState.notes);
  if (expected.size() != actual.size()) return false;
  for (size_t i = 0; i < expected.size(); ++i) {
    const NoteUtils::DisplayNote& expectedNote = expected[i];
    const NoteUtils::DisplayNote& actualNote = actual[i];
    if (expectedNote.noteId != actualNote.noteId || expectedNote.note != actualNote.note ||
        expectedNote.velocity != actualNote.velocity ||
        expectedNote.startTick != actualNote.startTick ||
        expectedNote.endTick != actualNote.endTick) {
      return false;
    }
  }
  return true;
}

bool reducedCompanionHistoryEquivalent(const MidiEventVec& initialEvents,
                                       const EditPassVec& history,
                                       uint32_t loopLengthTicks,
                                       uint32_t* mismatchGenerationOut = nullptr) {
  ReducedCompanionState reduced = ReducedCompanionState::fromEvents(initialEvents, loopLengthTicks);
  for (size_t cursor = 0; cursor <= history.size(); ++cursor) {
    const EditPassVec prefix = companionHistoryPrefix(history, cursor);
    MidiEventVec expectedEvents = initialEvents;
    applyNoteEditPassSequence(expectedEvents, prefix, loopLengthTicks);
    if (!reducedCompanionGeometryEquivalent(expectedEvents, reduced, loopLengthTicks)) {
      if (mismatchGenerationOut != nullptr) {
        *mismatchGenerationOut = static_cast<uint32_t>(cursor);
      }
      return false;
    }
    if (cursor < history.size()) {
      reduced.applyCompanionDelta(history[cursor], loopLengthTicks);
    }
  }

  std::vector<ReducedCompanionState> generationStates;
  generationStates.reserve(history.size() + 1);
  generationStates.push_back(ReducedCompanionState::fromEvents(initialEvents, loopLengthTicks));
  for (const EditPass& row : history) {
    ReducedCompanionState nextState = generationStates.back();
    nextState.applyCompanionDelta(row, loopLengthTicks);
    generationStates.push_back(std::move(nextState));
  }

  for (size_t cursor = history.size(); cursor > 0; --cursor) {
    const EditPassVec prefix = companionHistoryPrefix(history, cursor - 1);
    MidiEventVec expectedEvents = initialEvents;
    applyNoteEditPassSequence(expectedEvents, prefix, loopLengthTicks);
    if (!reducedCompanionGeometryEquivalent(expectedEvents, generationStates[cursor - 1],
                                            loopLengthTicks)) {
      if (mismatchGenerationOut != nullptr) {
        *mismatchGenerationOut = static_cast<uint32_t>(cursor - 1);
      }
      return false;
    }
  }

  for (size_t cursor = 0; cursor < history.size(); ++cursor) {
    const EditPassVec prefix = companionHistoryPrefix(history, cursor + 1);
    MidiEventVec expectedEvents = initialEvents;
    applyNoteEditPassSequence(expectedEvents, prefix, loopLengthTicks);
    if (!reducedCompanionGeometryEquivalent(expectedEvents, generationStates[cursor + 1],
                                            loopLengthTicks)) {
      if (mismatchGenerationOut != nullptr) {
        *mismatchGenerationOut = static_cast<uint32_t>(cursor + 1);
      }
      return false;
    }
  }
  return true;
}

struct CompanionEventSpan {
  NoteId noteId = kInvalidNoteId;
  uint8_t channel = 1;
  uint8_t pitch = 0;
  uint8_t velocity = 0;
  uint32_t startTick = 0;
  uint32_t endTick = 0;
};

struct PairedCompanionState {
  std::vector<CompanionEventSpan> spans;
  std::vector<uint32_t> eventOrder;

  static PairedCompanionState fromEvents(const MidiEventVec& events) {
    PairedCompanionState state;
    state.spans.reserve(events.size() / 2u);
    std::vector<int32_t> onToSpan(events.size(), -1);
    std::vector<int32_t> offToSpan(events.size(), -1);
    for (size_t i = 0; i < events.size(); ++i) {
      const MidiEvent& event = events[i];
      if (!event.isNoteOn()) {
        continue;
      }
      const int onIndex = static_cast<int>(i);
      const int offIndex = findNoteOffForOnIndex(events, onIndex);
      const MidiEvent* offEvent =
          (offIndex >= 0) ? &events[static_cast<size_t>(offIndex)] : nullptr;
      CompanionEventSpan span;
      span.noteId = event.noteId;
      span.channel = event.channel;
      span.pitch = event.data.noteData.note;
      span.velocity = event.data.noteData.velocity;
      span.startTick = event.tick;
      span.endTick = (offEvent != nullptr) ? offEvent->tick : event.tick;
      const int32_t spanIndex = static_cast<int32_t>(state.spans.size());
      state.spans.push_back(span);
      onToSpan[i] = spanIndex;
      if (offIndex >= 0) {
        offToSpan[static_cast<size_t>(offIndex)] = spanIndex;
      }
    }
    state.eventOrder.reserve(state.spans.size() * 2u);
    for (size_t i = 0; i < events.size(); ++i) {
      const int32_t onSpan = onToSpan[i];
      if (onSpan >= 0) {
        state.eventOrder.push_back(static_cast<uint32_t>(onSpan) * 2u);
        continue;
      }
      const int32_t offSpan = offToSpan[i];
      if (offSpan >= 0) {
        state.eventOrder.push_back(static_cast<uint32_t>(offSpan) * 2u + 1u);
      }
    }
    return state;
  }

  MidiEventVec materializeEvents() const {
    MidiEventVec events;
    const size_t maxEventCount = spans.size() * 2u;
    events.reserve(maxEventCount);
    if (!eventOrder.empty()) {
      for (const uint32_t token : eventOrder) {
        const size_t spanIndex = static_cast<size_t>(token / 2u);
        if (spanIndex >= spans.size()) {
          continue;
        }
        const CompanionEventSpan& span = spans[spanIndex];
        const bool isOff = (token & 1u) != 0u;
        if (isOff) {
          events.push_back(MidiEvent::NoteOff(span.endTick, span.channel, span.pitch, 0));
        } else {
          events.push_back(sequenceNoteOnWithId(span.startTick, span.channel, span.pitch,
                                                span.velocity, span.noteId));
        }
      }
    } else {
      for (const CompanionEventSpan& span : spans) {
        events.push_back(sequenceNoteOnWithId(span.startTick, span.channel, span.pitch,
                                              span.velocity, span.noteId));
        events.push_back(MidiEvent::NoteOff(span.endTick, span.channel, span.pitch, 0));
      }
    }
    return events;
  }

  void applyCompanionDelta(const EditPass& row, uint32_t loopLengthTicks) {
    if (row.actionType != EditActionType::Delete &&
        !(row.actionType == EditActionType::Update &&
          row.propertyType == EditPropertyType::Length)) {
      return;
    }
    MidiEventVec events = materializeEvents();
    applyNoteEditPass(events, row, loopLengthTicks);
    const PairedCompanionState nextState = PairedCompanionState::fromEvents(events);
    spans = nextState.spans;
    eventOrder = nextState.eventOrder;
  }
};

std::vector<CompanionEventSpan> canonicalCompanionEventSpans(
    const std::vector<CompanionEventSpan>& spans) {
  std::vector<CompanionEventSpan> canonical = spans;
  std::stable_sort(canonical.begin(), canonical.end(),
                   [](const CompanionEventSpan& a, const CompanionEventSpan& b) {
                     if (a.noteId != b.noteId) return a.noteId < b.noteId;
                     if (a.pitch != b.pitch) return a.pitch < b.pitch;
                     if (a.startTick != b.startTick) return a.startTick < b.startTick;
                     if (a.endTick != b.endTick) return a.endTick < b.endTick;
                     if (a.velocity != b.velocity) return a.velocity < b.velocity;
                     return a.channel < b.channel;
                   });
  return canonical;
}

bool companionEventSpanEqual(const CompanionEventSpan& a, const CompanionEventSpan& b) {
  return a.noteId == b.noteId && a.channel == b.channel && a.pitch == b.pitch &&
         a.velocity == b.velocity && a.startTick == b.startTick && a.endTick == b.endTick;
}

bool companionEventSpansEquivalent(const MidiEventVec& expectedEvents,
                                   const std::vector<CompanionEventSpan>& actualSpans) {
  const std::vector<CompanionEventSpan> expected =
      canonicalCompanionEventSpans(PairedCompanionState::fromEvents(expectedEvents).spans);
  const std::vector<CompanionEventSpan> actual =
      canonicalCompanionEventSpans(actualSpans);
  if (expected.size() != actual.size()) return false;
  for (size_t i = 0; i < expected.size(); ++i) {
    if (!companionEventSpanEqual(expected[i], actual[i])) {
      return false;
    }
  }
  return true;
}

bool pairedCompanionStateEquivalent(const MidiEventVec& expectedEvents,
                                    const PairedCompanionState& pairedState) {
  return companionEventSpansEquivalent(expectedEvents, pairedState.spans);
}

bool pairedCompanionHistoryEquivalent(const MidiEventVec& initialEvents,
                                      const EditPassVec& history,
                                      uint32_t loopLengthTicks,
                                      uint32_t* mismatchGenerationOut = nullptr) {
  PairedCompanionState paired = PairedCompanionState::fromEvents(initialEvents);
  for (size_t cursor = 0; cursor <= history.size(); ++cursor) {
    const EditPassVec prefix = companionHistoryPrefix(history, cursor);
    MidiEventVec expectedEvents = initialEvents;
    applyNoteEditPassSequence(expectedEvents, prefix, loopLengthTicks);
    if (!pairedCompanionStateEquivalent(expectedEvents, paired)) {
      if (mismatchGenerationOut != nullptr) {
        *mismatchGenerationOut = static_cast<uint32_t>(cursor);
      }
      return false;
    }
    if (cursor < history.size()) {
      paired.applyCompanionDelta(history[cursor], loopLengthTicks);
    }
  }

  std::vector<PairedCompanionState> generationStates;
  generationStates.reserve(history.size() + 1);
  generationStates.push_back(PairedCompanionState::fromEvents(initialEvents));
  for (const EditPass& row : history) {
    PairedCompanionState nextState = generationStates.back();
    nextState.applyCompanionDelta(row, loopLengthTicks);
    generationStates.push_back(std::move(nextState));
  }

  for (size_t cursor = history.size(); cursor > 0; --cursor) {
    const EditPassVec prefix = companionHistoryPrefix(history, cursor - 1);
    MidiEventVec expectedEvents = initialEvents;
    applyNoteEditPassSequence(expectedEvents, prefix, loopLengthTicks);
    if (!pairedCompanionStateEquivalent(expectedEvents, generationStates[cursor - 1])) {
      if (mismatchGenerationOut != nullptr) {
        *mismatchGenerationOut = static_cast<uint32_t>(cursor - 1);
      }
      return false;
    }
  }

  for (size_t cursor = 0; cursor < history.size(); ++cursor) {
    const EditPassVec prefix = companionHistoryPrefix(history, cursor + 1);
    MidiEventVec expectedEvents = initialEvents;
    applyNoteEditPassSequence(expectedEvents, prefix, loopLengthTicks);
    if (!pairedCompanionStateEquivalent(expectedEvents, generationStates[cursor + 1])) {
      if (mismatchGenerationOut != nullptr) {
        *mismatchGenerationOut = static_cast<uint32_t>(cursor + 1);
      }
      return false;
    }
  }
  return true;
}

struct SpanOnlyCompanionState {
  std::vector<CompanionEventSpan> spans;

  static SpanOnlyCompanionState fromEvents(const MidiEventVec& events) {
    SpanOnlyCompanionState state;
    state.spans = PairedCompanionState::fromEvents(events).spans;
    return state;
  }

  MidiEventVec materializeEvents() const {
    MidiEventVec events;
    events.reserve(spans.size() * 2u);
    for (const CompanionEventSpan& span : spans) {
      events.push_back(
          sequenceNoteOnWithId(span.startTick, span.channel, span.pitch, span.velocity, span.noteId));
      events.push_back(MidiEvent::NoteOff(span.endTick, span.channel, span.pitch, 0));
    }
    return events;
  }

  void applyCompanionDelta(const EditPass& row, uint32_t loopLengthTicks) {
    if (row.actionType != EditActionType::Delete &&
        !(row.actionType == EditActionType::Update &&
          row.propertyType == EditPropertyType::Length)) {
      return;
    }
    MidiEventVec events = materializeEvents();
    applyNoteEditPass(events, row, loopLengthTicks);
    spans = PairedCompanionState::fromEvents(events).spans;
  }
};

bool spanOnlyCompanionHistoryEquivalent(const MidiEventVec& initialEvents,
                                        const EditPassVec& history,
                                        uint32_t loopLengthTicks,
                                        uint32_t* mismatchGenerationOut = nullptr) {
  SpanOnlyCompanionState spanOnly = SpanOnlyCompanionState::fromEvents(initialEvents);
  for (size_t cursor = 0; cursor <= history.size(); ++cursor) {
    const EditPassVec prefix = companionHistoryPrefix(history, cursor);
    MidiEventVec expectedEvents = initialEvents;
    applyNoteEditPassSequence(expectedEvents, prefix, loopLengthTicks);
    if (!companionEventSpansEquivalent(expectedEvents, spanOnly.spans)) {
      if (mismatchGenerationOut != nullptr) {
        *mismatchGenerationOut = static_cast<uint32_t>(cursor);
      }
      return false;
    }
    if (cursor < history.size()) {
      spanOnly.applyCompanionDelta(history[cursor], loopLengthTicks);
    }
  }
  return true;
}

struct TickOrderedCompanionState {
  std::vector<CompanionEventSpan> spans;
  bool offBeforeOn = false;

  static TickOrderedCompanionState fromEvents(const MidiEventVec& events, bool offBeforeOnValue) {
    TickOrderedCompanionState state;
    state.spans = PairedCompanionState::fromEvents(events).spans;
    state.offBeforeOn = offBeforeOnValue;
    return state;
  }

  MidiEventVec materializeEvents() const {
    MidiEventVec events;
    events.reserve(spans.size() * 2u);
    for (const CompanionEventSpan& span : spans) {
      events.push_back(
          sequenceNoteOnWithId(span.startTick, span.channel, span.pitch, span.velocity, span.noteId));
      events.push_back(MidiEvent::NoteOff(span.endTick, span.channel, span.pitch, 0));
    }
    std::stable_sort(events.begin(), events.end(),
                     [this](const MidiEvent& a, const MidiEvent& b) {
                       if (a.tick != b.tick) {
                         return a.tick < b.tick;
                       }
                       const bool aOff = a.isNoteOff();
                       const bool bOff = b.isNoteOff();
                       if (aOff != bOff) {
                         return offBeforeOn ? aOff : !aOff;
                       }
                       return false;
                     });
    return events;
  }

  void applyCompanionDelta(const EditPass& row, uint32_t loopLengthTicks) {
    if (row.actionType != EditActionType::Delete &&
        !(row.actionType == EditActionType::Update &&
          row.propertyType == EditPropertyType::Length)) {
      return;
    }
    MidiEventVec events = materializeEvents();
    applyNoteEditPass(events, row, loopLengthTicks);
    TickOrderedCompanionState nextState =
        TickOrderedCompanionState::fromEvents(events, offBeforeOn);
    spans = std::move(nextState.spans);
  }
};

bool tickOrderedCompanionHistoryEquivalent(const MidiEventVec& initialEvents,
                                           const EditPassVec& history,
                                           uint32_t loopLengthTicks,
                                           bool offBeforeOn,
                                           uint32_t* mismatchGenerationOut = nullptr) {
  TickOrderedCompanionState candidate =
      TickOrderedCompanionState::fromEvents(initialEvents, offBeforeOn);
  for (size_t cursor = 0; cursor <= history.size(); ++cursor) {
    const EditPassVec prefix = companionHistoryPrefix(history, cursor);
    MidiEventVec expectedEvents = initialEvents;
    applyNoteEditPassSequence(expectedEvents, prefix, loopLengthTicks);
    if (!companionEventSpansEquivalent(expectedEvents, candidate.spans)) {
      if (mismatchGenerationOut != nullptr) {
        *mismatchGenerationOut = static_cast<uint32_t>(cursor);
      }
      return false;
    }
    if (cursor < history.size()) {
      candidate.applyCompanionDelta(history[cursor], loopLengthTicks);
    }
  }
  return true;
}

struct DerivedKeyOrderedCompanionState {
  std::vector<CompanionEventSpan> spans;

  static DerivedKeyOrderedCompanionState fromEvents(const MidiEventVec& events) {
    DerivedKeyOrderedCompanionState state;
    state.spans = PairedCompanionState::fromEvents(events).spans;
    return state;
  }

  MidiEventVec materializeEvents() const {
    MidiEventVec events;
    events.reserve(spans.size() * 2u);
    for (const CompanionEventSpan& span : spans) {
      events.push_back(
          sequenceNoteOnWithId(span.startTick, span.channel, span.pitch, span.velocity, span.noteId));
      events.push_back(MidiEvent::NoteOff(span.endTick, span.channel, span.pitch, 0));
    }

    std::stable_sort(events.begin(), events.end(), [](const MidiEvent& a, const MidiEvent& b) {
      if (a.tick != b.tick) return a.tick < b.tick;

      const bool aOff = a.isNoteOff();
      const bool bOff = b.isNoteOff();
      if (aOff != bOff) return !aOff;  // on before off at same tick

      const uint8_t aPitch = (a.isNoteOn() || a.isNoteOff()) ? a.data.noteData.note : 0;
      const uint8_t bPitch = (b.isNoteOn() || b.isNoteOff()) ? b.data.noteData.note : 0;
      if (aPitch != bPitch) return aPitch < bPitch;

      if (a.channel != b.channel) return a.channel < b.channel;

      const uint8_t aVelocity = (a.isNoteOn() || a.isNoteOff()) ? a.data.noteData.velocity : 0;
      const uint8_t bVelocity = (b.isNoteOn() || b.isNoteOff()) ? b.data.noteData.velocity : 0;
      if (aVelocity != bVelocity) return aVelocity < bVelocity;

      return a.noteId < b.noteId;
    });
    return events;
  }

  void applyCompanionDelta(const EditPass& row, uint32_t loopLengthTicks) {
    if (row.actionType != EditActionType::Delete &&
        !(row.actionType == EditActionType::Update &&
          row.propertyType == EditPropertyType::Length)) {
      return;
    }
    MidiEventVec events = materializeEvents();
    applyNoteEditPass(events, row, loopLengthTicks);
    spans = DerivedKeyOrderedCompanionState::fromEvents(events).spans;
  }
};

bool derivedKeyOrderedCompanionHistoryEquivalent(const MidiEventVec& initialEvents,
                                                 const EditPassVec& history,
                                                 uint32_t loopLengthTicks,
                                                 uint32_t* mismatchGenerationOut = nullptr) {
  DerivedKeyOrderedCompanionState candidate =
      DerivedKeyOrderedCompanionState::fromEvents(initialEvents);
  for (size_t cursor = 0; cursor <= history.size(); ++cursor) {
    const EditPassVec prefix = companionHistoryPrefix(history, cursor);
    MidiEventVec expectedEvents = initialEvents;
    applyNoteEditPassSequence(expectedEvents, prefix, loopLengthTicks);
    if (!companionEventSpansEquivalent(expectedEvents, candidate.spans)) {
      if (mismatchGenerationOut != nullptr) {
        *mismatchGenerationOut = static_cast<uint32_t>(cursor);
      }
      return false;
    }
    if (cursor < history.size()) {
      candidate.applyCompanionDelta(history[cursor], loopLengthTicks);
    }
  }
  return true;
}

struct SplitOrderCompanionState {
  std::vector<CompanionEventSpan> spans;
  std::vector<uint32_t> onOrder;
  std::vector<uint32_t> offOrder;

  static SplitOrderCompanionState fromEvents(const MidiEventVec& events) {
    SplitOrderCompanionState state;
    const PairedCompanionState paired = PairedCompanionState::fromEvents(events);
    state.spans = paired.spans;
    state.onOrder.reserve(state.spans.size());
    state.offOrder.reserve(state.spans.size());
    for (const uint32_t token : paired.eventOrder) {
      const uint32_t spanIndex = token / 2u;
      if ((token & 1u) != 0u) {
        state.offOrder.push_back(spanIndex);
      } else {
        state.onOrder.push_back(spanIndex);
      }
    }
    return state;
  }

  MidiEventVec materializeEvents() const {
    MidiEventVec onEvents;
    MidiEventVec offEvents;
    onEvents.reserve(onOrder.size());
    offEvents.reserve(offOrder.size());

    for (const uint32_t spanIndex : onOrder) {
      if (static_cast<size_t>(spanIndex) >= spans.size()) {
        continue;
      }
      const CompanionEventSpan& span = spans[static_cast<size_t>(spanIndex)];
      onEvents.push_back(
          sequenceNoteOnWithId(span.startTick, span.channel, span.pitch, span.velocity, span.noteId));
    }
    for (const uint32_t spanIndex : offOrder) {
      if (static_cast<size_t>(spanIndex) >= spans.size()) {
        continue;
      }
      const CompanionEventSpan& span = spans[static_cast<size_t>(spanIndex)];
      offEvents.push_back(MidiEvent::NoteOff(span.endTick, span.channel, span.pitch, 0));
    }

    MidiEventVec merged;
    merged.reserve(onEvents.size() + offEvents.size());
    size_t onIdx = 0;
    size_t offIdx = 0;
    while (onIdx < onEvents.size() || offIdx < offEvents.size()) {
      if (onIdx >= onEvents.size()) {
        merged.push_back(offEvents[offIdx++]);
        continue;
      }
      if (offIdx >= offEvents.size()) {
        merged.push_back(onEvents[onIdx++]);
        continue;
      }
      const MidiEvent& onEvent = onEvents[onIdx];
      const MidiEvent& offEvent = offEvents[offIdx];
      if (onEvent.tick <= offEvent.tick) {
        merged.push_back(onEvent);
        ++onIdx;
      } else {
        merged.push_back(offEvent);
        ++offIdx;
      }
    }
    return merged;
  }

  void applyCompanionDelta(const EditPass& row, uint32_t loopLengthTicks) {
    if (row.actionType != EditActionType::Delete &&
        !(row.actionType == EditActionType::Update &&
          row.propertyType == EditPropertyType::Length)) {
      return;
    }
    MidiEventVec events = materializeEvents();
    applyNoteEditPass(events, row, loopLengthTicks);
    SplitOrderCompanionState nextState = SplitOrderCompanionState::fromEvents(events);
    spans = std::move(nextState.spans);
    onOrder = std::move(nextState.onOrder);
    offOrder = std::move(nextState.offOrder);
  }
};

bool splitOrderCompanionHistoryEquivalent(const MidiEventVec& initialEvents,
                                          const EditPassVec& history,
                                          uint32_t loopLengthTicks,
                                          uint32_t* mismatchGenerationOut = nullptr) {
  SplitOrderCompanionState candidate = SplitOrderCompanionState::fromEvents(initialEvents);
  for (size_t cursor = 0; cursor <= history.size(); ++cursor) {
    const EditPassVec prefix = companionHistoryPrefix(history, cursor);
    MidiEventVec expectedEvents = initialEvents;
    applyNoteEditPassSequence(expectedEvents, prefix, loopLengthTicks);
    if (!companionEventSpansEquivalent(expectedEvents, candidate.spans)) {
      if (mismatchGenerationOut != nullptr) {
        *mismatchGenerationOut = static_cast<uint32_t>(cursor);
      }
      return false;
    }
    if (cursor < history.size()) {
      candidate.applyCompanionDelta(history[cursor], loopLengthTicks);
    }
  }
  return true;
}

struct TickBucketOrder {
  uint32_t tick = 0;
  std::vector<uint32_t> tokens;
};

struct SameTickOrderCompanionState {
  std::vector<CompanionEventSpan> spans;
  std::vector<TickBucketOrder> buckets;

  static uint32_t tokenTick(const CompanionEventSpan& span, uint32_t token) {
    return ((token & 1u) != 0u) ? span.endTick : span.startTick;
  }

  static SameTickOrderCompanionState fromEvents(const MidiEventVec& events) {
    SameTickOrderCompanionState state;
    const PairedCompanionState paired = PairedCompanionState::fromEvents(events);
    state.spans = paired.spans;

    std::map<uint32_t, size_t> tickToBucket;
    for (const uint32_t token : paired.eventOrder) {
      const size_t spanIndex = static_cast<size_t>(token / 2u);
      if (spanIndex >= state.spans.size()) {
        continue;
      }
      const uint32_t tick = tokenTick(state.spans[spanIndex], token);
      auto it = tickToBucket.find(tick);
      if (it == tickToBucket.end()) {
        const size_t newIndex = state.buckets.size();
        tickToBucket[tick] = newIndex;
        TickBucketOrder bucket;
        bucket.tick = tick;
        bucket.tokens.push_back(token);
        state.buckets.push_back(std::move(bucket));
      } else {
        state.buckets[it->second].tokens.push_back(token);
      }
    }
    std::sort(state.buckets.begin(), state.buckets.end(),
              [](const TickBucketOrder& a, const TickBucketOrder& b) {
                return a.tick < b.tick;
              });
    return state;
  }

  MidiEventVec materializeEvents() const {
    MidiEventVec events;
    size_t tokenCount = 0;
    for (const TickBucketOrder& bucket : buckets) tokenCount += bucket.tokens.size();
    events.reserve(tokenCount);

    for (const TickBucketOrder& bucket : buckets) {
      for (const uint32_t token : bucket.tokens) {
        const size_t spanIndex = static_cast<size_t>(token / 2u);
        if (spanIndex >= spans.size()) {
          continue;
        }
        const CompanionEventSpan& span = spans[spanIndex];
        if ((token & 1u) != 0u) {
          events.push_back(MidiEvent::NoteOff(span.endTick, span.channel, span.pitch, 0));
        } else {
          events.push_back(sequenceNoteOnWithId(span.startTick, span.channel, span.pitch,
                                                span.velocity, span.noteId));
        }
      }
    }
    return events;
  }

  void applyCompanionDelta(const EditPass& row, uint32_t loopLengthTicks) {
    if (row.actionType != EditActionType::Delete &&
        !(row.actionType == EditActionType::Update &&
          row.propertyType == EditPropertyType::Length)) {
      return;
    }
    MidiEventVec events = materializeEvents();
    applyNoteEditPass(events, row, loopLengthTicks);
    SameTickOrderCompanionState nextState = SameTickOrderCompanionState::fromEvents(events);
    spans = std::move(nextState.spans);
    buckets = std::move(nextState.buckets);
  }
};

bool sameTickOrderCompanionHistoryEquivalent(const MidiEventVec& initialEvents,
                                             const EditPassVec& history,
                                             uint32_t loopLengthTicks,
                                             uint32_t* mismatchGenerationOut = nullptr) {
  SameTickOrderCompanionState candidate = SameTickOrderCompanionState::fromEvents(initialEvents);
  for (size_t cursor = 0; cursor <= history.size(); ++cursor) {
    const EditPassVec prefix = companionHistoryPrefix(history, cursor);
    MidiEventVec expectedEvents = initialEvents;
    applyNoteEditPassSequence(expectedEvents, prefix, loopLengthTicks);
    if (!companionEventSpansEquivalent(expectedEvents, candidate.spans)) {
      if (mismatchGenerationOut != nullptr) {
        *mismatchGenerationOut = static_cast<uint32_t>(cursor);
      }
      return false;
    }
    if (cursor < history.size()) {
      candidate.applyCompanionDelta(history[cursor], loopLengthTicks);
    }
  }
  return true;
}

struct NoteIdAblatedCompanionState {
  std::vector<CompanionEventSpan> spans;
  std::vector<uint32_t> eventOrder;
  bool renumberAfterDelta = true;

  static void renumberSpans(std::vector<CompanionEventSpan>& spans, NoteId firstId = 50000) {
    NoteId nextId = firstId;
    for (CompanionEventSpan& span : spans) {
      span.noteId = nextId++;
    }
  }

  static NoteIdAblatedCompanionState fromEvents(const MidiEventVec& events, bool renumber) {
    NoteIdAblatedCompanionState state;
    const PairedCompanionState paired = PairedCompanionState::fromEvents(events);
    state.spans = paired.spans;
    state.eventOrder = paired.eventOrder;
    if (renumber) {
      renumberSpans(state.spans);
    }
    return state;
  }

  MidiEventVec materializeEvents() const {
    MidiEventVec events;
    events.reserve(spans.size() * 2u);
    for (const uint32_t token : eventOrder) {
      const size_t spanIndex = static_cast<size_t>(token / 2u);
      if (spanIndex >= spans.size()) {
        continue;
      }
      const CompanionEventSpan& span = spans[spanIndex];
      if ((token & 1u) != 0u) {
        events.push_back(MidiEvent::NoteOff(span.endTick, span.channel, span.pitch, 0));
      } else {
        events.push_back(sequenceNoteOnWithId(span.startTick, span.channel, span.pitch, span.velocity,
                                              span.noteId));
      }
    }
    return events;
  }

  void applyCompanionDelta(const EditPass& row, uint32_t loopLengthTicks) {
    if (row.actionType != EditActionType::Delete &&
        !(row.actionType == EditActionType::Update &&
          row.propertyType == EditPropertyType::Length)) {
      return;
    }
    MidiEventVec events = materializeEvents();
    applyNoteEditPass(events, row, loopLengthTicks);
    NoteIdAblatedCompanionState nextState =
        NoteIdAblatedCompanionState::fromEvents(events, renumberAfterDelta);
    spans = std::move(nextState.spans);
    eventOrder = std::move(nextState.eventOrder);
  }
};

bool noteIdAblatedCompanionHistoryEquivalent(const MidiEventVec& initialEvents,
                                             const EditPassVec& history,
                                             uint32_t loopLengthTicks,
                                             uint32_t* mismatchGenerationOut = nullptr) {
  NoteIdAblatedCompanionState candidate = NoteIdAblatedCompanionState::fromEvents(initialEvents, false);
  for (size_t cursor = 0; cursor <= history.size(); ++cursor) {
    const EditPassVec prefix = companionHistoryPrefix(history, cursor);
    MidiEventVec expectedEvents = initialEvents;
    applyNoteEditPassSequence(expectedEvents, prefix, loopLengthTicks);
    if (!companionEventSpansEquivalent(expectedEvents, candidate.spans)) {
      if (mismatchGenerationOut != nullptr) {
        *mismatchGenerationOut = static_cast<uint32_t>(cursor);
      }
      return false;
    }
    if (cursor < history.size()) {
      candidate.applyCompanionDelta(history[cursor], loopLengthTicks);
    }
  }
  return true;
}

struct ContextAblatedCompanionState {
  std::vector<CompanionEventSpan> spans;
  std::vector<uint32_t> eventOrder;
  bool keepChannel = true;
  bool keepVelocity = true;

  static void normalizeSpan(CompanionEventSpan& span, bool keepChannelValue,
                            bool keepVelocityValue) {
    if (!keepChannelValue) {
      span.channel = 1;
    }
    if (!keepVelocityValue) {
      span.velocity = 100;
    }
  }

  static ContextAblatedCompanionState fromEvents(const MidiEventVec& events, bool keepChannelValue,
                                                 bool keepVelocityValue) {
    ContextAblatedCompanionState state;
    state.keepChannel = keepChannelValue;
    state.keepVelocity = keepVelocityValue;
    const PairedCompanionState paired = PairedCompanionState::fromEvents(events);
    state.spans = paired.spans;
    state.eventOrder = paired.eventOrder;
    for (CompanionEventSpan& span : state.spans) {
      normalizeSpan(span, keepChannelValue, keepVelocityValue);
    }
    return state;
  }

  MidiEventVec materializeEvents() const {
    MidiEventVec events;
    events.reserve(spans.size() * 2u);
    for (const uint32_t token : eventOrder) {
      const size_t spanIndex = static_cast<size_t>(token / 2u);
      if (spanIndex >= spans.size()) {
        continue;
      }
      const CompanionEventSpan& span = spans[spanIndex];
      if ((token & 1u) != 0u) {
        events.push_back(MidiEvent::NoteOff(span.endTick, span.channel, span.pitch, 0));
      } else {
        events.push_back(sequenceNoteOnWithId(span.startTick, span.channel, span.pitch, span.velocity,
                                              span.noteId));
      }
    }
    return events;
  }

  void applyCompanionDelta(const EditPass& row, uint32_t loopLengthTicks) {
    if (row.actionType != EditActionType::Delete &&
        !(row.actionType == EditActionType::Update &&
          row.propertyType == EditPropertyType::Length)) {
      return;
    }
    MidiEventVec events = materializeEvents();
    applyNoteEditPass(events, row, loopLengthTicks);
    ContextAblatedCompanionState nextState =
        ContextAblatedCompanionState::fromEvents(events, keepChannel, keepVelocity);
    spans = std::move(nextState.spans);
    eventOrder = std::move(nextState.eventOrder);
  }
};

bool contextAblatedCompanionHistoryEquivalent(const MidiEventVec& initialEvents,
                                              const EditPassVec& history,
                                              uint32_t loopLengthTicks,
                                              bool keepChannel,
                                              bool keepVelocity,
                                              uint32_t* mismatchGenerationOut = nullptr) {
  ContextAblatedCompanionState candidate =
      ContextAblatedCompanionState::fromEvents(initialEvents, keepChannel, keepVelocity);
  for (size_t cursor = 0; cursor <= history.size(); ++cursor) {
    const EditPassVec prefix = companionHistoryPrefix(history, cursor);
    MidiEventVec expectedEvents = initialEvents;
    applyNoteEditPassSequence(expectedEvents, prefix, loopLengthTicks);
    if (!companionEventSpansEquivalent(expectedEvents, candidate.spans)) {
      if (mismatchGenerationOut != nullptr) {
        *mismatchGenerationOut = static_cast<uint32_t>(cursor);
      }
      return false;
    }
    if (cursor < history.size()) {
      candidate.applyCompanionDelta(history[cursor], loopLengthTicks);
    }
  }
  return true;
}

struct PitchAblatedCompanionState {
  std::vector<CompanionEventSpan> spans;
  std::vector<uint32_t> eventOrder;
  bool keepPitch = true;

  static void normalizeSpanPitch(CompanionEventSpan& span, bool keepPitchValue) {
    if (!keepPitchValue) {
      span.pitch = 60;
    }
  }

  static PitchAblatedCompanionState fromEvents(const MidiEventVec& events, bool keepPitchValue) {
    PitchAblatedCompanionState state;
    state.keepPitch = keepPitchValue;
    const PairedCompanionState paired = PairedCompanionState::fromEvents(events);
    state.spans = paired.spans;
    state.eventOrder = paired.eventOrder;
    for (CompanionEventSpan& span : state.spans) {
      normalizeSpanPitch(span, keepPitchValue);
    }
    return state;
  }

  MidiEventVec materializeEvents() const {
    MidiEventVec events;
    events.reserve(spans.size() * 2u);
    for (const uint32_t token : eventOrder) {
      const size_t spanIndex = static_cast<size_t>(token / 2u);
      if (spanIndex >= spans.size()) {
        continue;
      }
      const CompanionEventSpan& span = spans[spanIndex];
      if ((token & 1u) != 0u) {
        events.push_back(MidiEvent::NoteOff(span.endTick, span.channel, span.pitch, 0));
      } else {
        events.push_back(sequenceNoteOnWithId(span.startTick, span.channel, span.pitch, span.velocity,
                                              span.noteId));
      }
    }
    return events;
  }

  void applyCompanionDelta(const EditPass& row, uint32_t loopLengthTicks) {
    if (row.actionType != EditActionType::Delete &&
        !(row.actionType == EditActionType::Update &&
          row.propertyType == EditPropertyType::Length)) {
      return;
    }
    MidiEventVec events = materializeEvents();
    applyNoteEditPass(events, row, loopLengthTicks);
    PitchAblatedCompanionState nextState = PitchAblatedCompanionState::fromEvents(events, keepPitch);
    spans = std::move(nextState.spans);
    eventOrder = std::move(nextState.eventOrder);
  }
};

bool pitchAblatedCompanionHistoryEquivalent(const MidiEventVec& initialEvents,
                                            const EditPassVec& history,
                                            uint32_t loopLengthTicks,
                                            bool keepPitch,
                                            uint32_t* mismatchGenerationOut = nullptr) {
  PitchAblatedCompanionState candidate =
      PitchAblatedCompanionState::fromEvents(initialEvents, keepPitch);
  for (size_t cursor = 0; cursor <= history.size(); ++cursor) {
    const EditPassVec prefix = companionHistoryPrefix(history, cursor);
    MidiEventVec expectedEvents = initialEvents;
    applyNoteEditPassSequence(expectedEvents, prefix, loopLengthTicks);
    if (!companionEventSpansEquivalent(expectedEvents, candidate.spans)) {
      if (mismatchGenerationOut != nullptr) {
        *mismatchGenerationOut = static_cast<uint32_t>(cursor);
      }
      return false;
    }
    if (cursor < history.size()) {
      candidate.applyCompanionDelta(history[cursor], loopLengthTicks);
    }
  }
  return true;
}

struct TickAblatedCompanionState {
  std::vector<CompanionEventSpan> spans;
  std::vector<uint32_t> eventOrder;
  bool keepStartTick = true;
  bool keepEndTick = true;

  static void normalizeSpanTicks(CompanionEventSpan& span, bool keepStart, bool keepEnd) {
    if (!keepStart) {
      span.startTick = 0;
    }
    if (!keepEnd) {
      span.endTick = keepStart ? span.startTick : 1;
    }
  }

  static TickAblatedCompanionState fromEvents(const MidiEventVec& events, bool keepStart,
                                              bool keepEnd) {
    TickAblatedCompanionState state;
    state.keepStartTick = keepStart;
    state.keepEndTick = keepEnd;
    const PairedCompanionState paired = PairedCompanionState::fromEvents(events);
    state.spans = paired.spans;
    state.eventOrder = paired.eventOrder;
    for (CompanionEventSpan& span : state.spans) {
      normalizeSpanTicks(span, keepStart, keepEnd);
    }
    return state;
  }

  MidiEventVec materializeEvents() const {
    MidiEventVec events;
    events.reserve(spans.size() * 2u);
    for (const uint32_t token : eventOrder) {
      const size_t spanIndex = static_cast<size_t>(token / 2u);
      if (spanIndex >= spans.size()) {
        continue;
      }
      const CompanionEventSpan& span = spans[spanIndex];
      if ((token & 1u) != 0u) {
        events.push_back(MidiEvent::NoteOff(span.endTick, span.channel, span.pitch, 0));
      } else {
        events.push_back(sequenceNoteOnWithId(span.startTick, span.channel, span.pitch, span.velocity,
                                              span.noteId));
      }
    }
    return events;
  }

  void applyCompanionDelta(const EditPass& row, uint32_t loopLengthTicks) {
    if (row.actionType != EditActionType::Delete &&
        !(row.actionType == EditActionType::Update &&
          row.propertyType == EditPropertyType::Length)) {
      return;
    }
    MidiEventVec events = materializeEvents();
    applyNoteEditPass(events, row, loopLengthTicks);
    TickAblatedCompanionState nextState =
        TickAblatedCompanionState::fromEvents(events, keepStartTick, keepEndTick);
    spans = std::move(nextState.spans);
    eventOrder = std::move(nextState.eventOrder);
  }
};

bool tickAblatedCompanionHistoryEquivalent(const MidiEventVec& initialEvents,
                                           const EditPassVec& history,
                                           uint32_t loopLengthTicks,
                                           bool keepStartTick,
                                           bool keepEndTick,
                                           uint32_t* mismatchGenerationOut = nullptr) {
  TickAblatedCompanionState candidate =
      TickAblatedCompanionState::fromEvents(initialEvents, keepStartTick, keepEndTick);
  for (size_t cursor = 0; cursor <= history.size(); ++cursor) {
    const EditPassVec prefix = companionHistoryPrefix(history, cursor);
    MidiEventVec expectedEvents = initialEvents;
    applyNoteEditPassSequence(expectedEvents, prefix, loopLengthTicks);
    if (!companionEventSpansEquivalent(expectedEvents, candidate.spans)) {
      if (mismatchGenerationOut != nullptr) {
        *mismatchGenerationOut = static_cast<uint32_t>(cursor);
      }
      return false;
    }
    if (cursor < history.size()) {
      candidate.applyCompanionDelta(history[cursor], loopLengthTicks);
    }
  }
  return true;
}

std::vector<NoteId> collectChangedCompanionNoteIds(const ReducedCompanionState& before,
                                                   const ReducedCompanionState& after) {
  const NoteId ids[] = {
      kCompanionTargetNoteId,         kCompanionOverlapNoteId,
      kCompanionDistantOverlapNoteId, kCompanionDifferentPitchNoteId,
      kCompanionWrapNoteId,
  };
  std::vector<NoteId> changed;
  for (const NoteId noteId : ids) {
    if (!companionNoteStateEquals(companionNoteState(before.notes, noteId),
                                  companionNoteState(after.notes, noteId))) {
      changed.push_back(noteId);
    }
  }
  std::sort(changed.begin(), changed.end());
  return changed;
}

void assertChangedCompanionNoteIds(const std::vector<NoteId>& actual,
                                   std::initializer_list<NoteId> expected,
                                   const char* context) {
  std::vector<NoteId> expectedSorted(expected.begin(), expected.end());
  std::sort(expectedSorted.begin(), expectedSorted.end());
  if (actual.size() != expectedSorted.size()) {
    char message[192];
    snprintf(message, sizeof(message), "%s: changed-note count mismatch expected=%u actual=%u",
             context, static_cast<unsigned>(expectedSorted.size()),
             static_cast<unsigned>(actual.size()));
    TEST_FAIL_MESSAGE(message);
  }
  for (size_t i = 0; i < actual.size(); ++i) {
    if (actual[i] != expectedSorted[i]) {
      char message[192];
      snprintf(message, sizeof(message), "%s: changed-note mismatch index=%u", context,
               static_cast<unsigned>(i));
      TEST_FAIL_MESSAGE(message);
    }
  }
}

}  // namespace

// Regression: a note lengthened to share its end tick with another same-pitch note must
// not bleed a later pitch edit onto the neighbor (loop-end stretch from an orphaned note-on).
void test_change_pitch_on_lengthened_note_keeps_same_pitch_neighbor() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopPasses passes;
  // M0: on@8 off@104, P0: on@584 off@680, both pitch 60.
  passes.recordPass = makeRecordPassWithTwoNotes(1, 8, 104, 584, 680, 60);



  pushEditPassRow(passes, 1, makeLengthRow(1, 8, 104, 680));
  pushEditPassRow(passes, 2, makePitchRow(1, 8, 680, 67));

  constexpr uint32_t kLoopLength = 1536;
  MidiEventVec flat;
  passes.materializeToEventVector(flat, kLoopLength);
  const int movingOn = findNoteOnById(flat, 1);
  TEST_ASSERT_TRUE(movingOn >= 0);
  TEST_ASSERT_EQUAL_UINT32(8u, flat[static_cast<size_t>(movingOn)].tick);
  TEST_ASSERT_EQUAL_UINT8(67, flat[static_cast<size_t>(movingOn)].data.noteData.note);

  const std::vector<NoteUtils::DisplayNote> notes =
      NoteUtils::reconstructNotes(flat, kLoopLength, false);
  bool foundM0 = false;
  bool foundP0 = false;
  for (const NoteUtils::DisplayNote& dn : notes) {
    if (dn.noteId == 1) {
      foundM0 = true;
      TEST_ASSERT_EQUAL_UINT8(67, dn.note);
      TEST_ASSERT_EQUAL_UINT32(8u, dn.startTick);
      TEST_ASSERT_TRUE(dn.endTick > 104u);
    }
    if (dn.noteId == 2) {
      foundP0 = true;
      TEST_ASSERT_EQUAL_UINT8(60, dn.note);
      TEST_ASSERT_EQUAL_UINT32(584u, dn.startTick);
    }
  }
  TEST_ASSERT_TRUE(foundM0);
  TEST_ASSERT_TRUE(foundP0);
  TEST_ASSERT_EQUAL(1, countMatching(flat, true, 60, 584));
  TEST_ASSERT_EQUAL(0, countMatching(flat, true, 60, 8));
}

// Regression: after M0 moves to beat 2, lengthening it across P0 (beat 3, pitch 60) must not
// steal P0's note-off or stretch P0 to loop end (shared release tick at step 14 is OK).
void test_lengthen_after_move_keeps_p0_fixture_gate() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopPasses passes;
  // M0 @ step 4 (192-288 after +1 beat move); P0 @ step 12 (576-672, 2-step gate).
  passes.recordPass = makeRecordPassWithTwoNotes(1, 192, 288, 576, 672, 60);



  pushEditPassRow(passes, 1, makeLengthRow(1, 192, 288, 672));

  MidiEventVec flat;
  passes.materializeToEventVector(flat);

  TEST_ASSERT_EQUAL(1, countMatching(flat, true, 60, 192));
  TEST_ASSERT_EQUAL(1, countMatching(flat, true, 60, 576));
  TEST_ASSERT_EQUAL(0, countMatching(flat, true, 60, 8));
  int offAt672 = 0;
  for (const MidiEvent& e : flat) {
    if (e.isNoteOff() && e.data.noteData.note == 60 && e.tick == 672) {
      offAt672++;
    }
  }
  TEST_ASSERT_EQUAL(2, offAt672);
}

// Simulates commitAllPendingNoteEditActions rematerialize after move+lengthen (reselect path).
void test_change_length_rematerialize_keeps_p0_off_not_loop_end() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  constexpr uint32_t kLoopLength = 1536;
  LoopPasses passes;
  passes.recordPass = makeRecordPassWithTwoNotes(1, 192, 288, 576, 672, 60);


  pushEditPassRow(passes, 1, makeLengthRow(1, 192, 288, 672));

  MidiEventVec flat;
  passes.materializeToEventVector(flat, kLoopLength);

  const std::vector<NoteUtils::DisplayNote> notes =
      NoteUtils::reconstructNotes(flat, kLoopLength, true);
  bool foundP0 = false;
  for (const NoteUtils::DisplayNote& n : notes) {
    if (n.note == 60 && n.startTick == 576) {
      foundP0 = true;
      TEST_ASSERT_EQUAL(672, n.endTick);
      TEST_ASSERT_TRUE(n.endTick < kLoopLength - 1);
    }
    if (n.note == 60 && n.startTick == 576 && n.endTick >= kLoopLength - 2) {
      TEST_FAIL_MESSAGE("P0 stretched to loop end after ChangeLength rematerialize");
    }
  }
  TEST_ASSERT_TRUE(foundP0);
}

// session_20260813_203140: ChangeLength stub 14 2256–2304 must not wrap and
// shorten same-pitch neighbors to 2255, or delete the note at tick 0.
void test_length_replay_loop_boundary_does_not_shorten_same_pitch_neighbors_203140() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  constexpr uint32_t kLoopLength = 2304;
  constexpr uint8_t kPitch = 71;
  LoopPasses passes;
  passes.recordPass = makeLengthReplayLoopBoundaryRecordPass(1);
  pushEditPassRow(passes, 1, makeLengthRow(14, 2256, 2304, kLoopLength));

  MidiEventVec flat;
  passes.materializeToEventVector(flat, kLoopLength);

  TEST_ASSERT_EQUAL(0, countMatching(flat, false, kPitch, kLoopLength));

  const std::vector<NoteUtils::DisplayNote> notes =
      NoteUtils::reconstructNotes(flat, kLoopLength, false);
  bool foundTickZero = false;
  bool foundA = false;
  bool foundB = false;
  for (const NoteUtils::DisplayNote& note : notes) {
    TEST_ASSERT_FALSE(note.note == kPitch && note.endTick == 2255u);
    if (note.noteId == 1) {
      foundTickZero = true;
      TEST_ASSERT_EQUAL_UINT8(kPitch, note.note);
      TEST_ASSERT_EQUAL_UINT32(0u, note.startTick);
      TEST_ASSERT_EQUAL_UINT32(143u, note.endTick);
    }
    if (note.noteId == 2) {
      foundA = true;
      TEST_ASSERT_EQUAL_UINT8(kPitch, note.note);
      TEST_ASSERT_EQUAL_UINT32(720u, note.startTick);
      TEST_ASSERT_EQUAL_UINT32(766u, note.endTick);
    }
    if (note.noteId == 3) {
      foundB = true;
      TEST_ASSERT_EQUAL_UINT8(kPitch, note.note);
      TEST_ASSERT_EQUAL_UINT32(1488u, note.startTick);
      TEST_ASSERT_EQUAL_UINT32(1535u, note.endTick);
    }
  }
  TEST_ASSERT_TRUE(foundTickZero);
  TEST_ASSERT_TRUE(foundA);
  TEST_ASSERT_TRUE(foundB);
}

// session_20260813_210821: the host was moved to 801-1568 and pitched to 36 in an earlier
// session, so its pass list already holds a NoteRange row. The overlap shorten committed at
// 31.988 (Length 10 -> 1055) must survive replay instead of being rewritten to the earlier
// row's end, which reselect at 32.833 read back as the original 767 ticks.
void test_length_row_after_earlier_move_row_on_same_note_seals_210821() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  constexpr uint32_t kLoopLength = 2304;
  constexpr uint8_t kPitch = 36;
  LoopPasses passes;
  // note 1 = mover (18 in the capture), note 2 = host (10 in the capture), both pitch 26.
  passes.recordPass = makeRecordPassWithTwoNotes(1, 480, 575, 768, 863, 26);

  pushEditPassRows(passes, 1,
                   {makeNoteRangeRow(2, 768, 863, 801, 1568), makePitchRow(2, 801, 1568, kPitch),
                    makeLengthRow(2, 801, 1568, 1055), makeNoteRangeRow(1, 480, 575, 1056, 1151),
                    makePitchRow(1, 1056, 1151, kPitch)});

  MidiEventVec flat;
  passes.materializeToEventVector(flat, kLoopLength);

  const std::vector<NoteUtils::DisplayNote> notes =
      NoteUtils::reconstructNotes(flat, kLoopLength, false);
  bool foundHost = false;
  bool foundMover = false;
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.noteId == 2) {
      foundHost = true;
      TEST_ASSERT_EQUAL_UINT8(kPitch, note.note);
      TEST_ASSERT_EQUAL_UINT32(801u, note.startTick);
      TEST_ASSERT_EQUAL_UINT32(1055u, note.endTick);
    }
    if (note.noteId == 1) {
      foundMover = true;
      TEST_ASSERT_EQUAL_UINT8(kPitch, note.note);
      TEST_ASSERT_EQUAL_UINT32(1056u, note.startTick);
      TEST_ASSERT_EQUAL_UINT32(1151u, note.endTick);
    }
  }
  TEST_ASSERT_TRUE(foundHost);
  TEST_ASSERT_TRUE(foundMover);
}

// session_20260813_210945: the mover was committed to 960-1104 at 99.273, then the host was
// hidden and the mover moved to 768-912 at 115.986. The second NoteRange row must win instead
// of being rewritten back to the first row's span.
void test_second_move_row_on_same_note_wins_over_earlier_span_210945() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  constexpr uint32_t kLoopLength = 2304;
  constexpr uint8_t kPitch = 36;
  LoopPasses passes;
  // note 1 = mover (77 in the capture), note 2 = host (10 in the capture).
  passes.recordPass = makeRecordPassWithTwoNotes(1, 480, 575, 801, 1568, 26);

  pushEditPassRows(passes, 1,
                   {makeNoteRangeRow(1, 480, 575, 960, 1104), makePitchRow(1, 960, 1104, kPitch),
                    makeDeleteRow(2), makeNoteRangeRow(1, 960, 1104, 768, 912)});

  MidiEventVec flat;
  passes.materializeToEventVector(flat, kLoopLength);

  const std::vector<NoteUtils::DisplayNote> notes =
      NoteUtils::reconstructNotes(flat, kLoopLength, false);
  bool foundMover = false;
  for (const NoteUtils::DisplayNote& note : notes) {
    TEST_ASSERT_TRUE(note.noteId != 2);
    if (note.noteId == 1) {
      foundMover = true;
      TEST_ASSERT_EQUAL_UINT8(kPitch, note.note);
      TEST_ASSERT_EQUAL_UINT32(768u, note.startTick);
      TEST_ASSERT_EQUAL_UINT32(912u, note.endTick);
    }
  }
  TEST_ASSERT_TRUE(foundMover);
}

// session_20260816_145518 @23.402: deselect saved NoteRange 312-504 + Pitch 45 targeting
// noteId 280. Replay still had M24 888-1032. Pin apply when the store NoteId matches.
void test_145518_note_range_pitch_replay_moves_when_store_id_matches() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  constexpr uint32_t kLoopLength = 3072;
  constexpr NoteId kMoverId = 280;
  LoopPasses passes;
  passes.recordPass = makeRecordPassWithIdentifiedNote(1, 888, 1032, 24, kMoverId);

  pushEditPassRows(passes, 1,
                   {makeNoteRangeRow(kMoverId, 888, 1032, 312, 504),
                    makePitchRow(kMoverId, 312, 504, 45)});

  MidiEventVec flat;
  passes.materializeToEventVector(flat, kLoopLength);
  const std::vector<NoteUtils::DisplayNote> notes =
      NoteUtils::reconstructNotes(flat, kLoopLength, false);

  bool foundMoved = false;
  for (const NoteUtils::DisplayNote& note : notes) {
    TEST_ASSERT_FALSE(note.note == 24 && note.startTick == 888);
    if (note.noteId == kMoverId) {
      foundMoved = true;
      TEST_ASSERT_EQUAL_UINT8(45, note.note);
      TEST_ASSERT_EQUAL_UINT32(312u, note.startTick);
      TEST_ASSERT_EQUAL_UINT32(504u, note.endTick);
    }
  }
  TEST_ASSERT_TRUE(foundMoved);
}

// Same rows targeting 280 when the capture note at 888 is a different NoteId. Replay must
// leave M24 888-1032 — the 145518 take_only / replay_flat match.
void test_145518_note_range_pitch_replay_leaves_home_when_store_id_differs() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  constexpr uint32_t kLoopLength = 3072;
  constexpr NoteId kStoreId = 24;
  constexpr NoteId kRowTargetId = 280;
  LoopPasses passes;
  passes.recordPass = makeRecordPassWithIdentifiedNote(1, 888, 1032, 24, kStoreId);

  pushEditPassRows(passes, 1,
                   {makeNoteRangeRow(kRowTargetId, 888, 1032, 312, 504),
                    makePitchRow(kRowTargetId, 312, 504, 45)});

  MidiEventVec flat;
  passes.materializeToEventVector(flat, kLoopLength);
  const std::vector<NoteUtils::DisplayNote> notes =
      NoteUtils::reconstructNotes(flat, kLoopLength, false);

  bool foundHome = false;
  for (const NoteUtils::DisplayNote& note : notes) {
    TEST_ASSERT_FALSE(note.note == 45 && note.startTick == 312);
    if (note.noteId == kStoreId) {
      foundHome = true;
      TEST_ASSERT_EQUAL_UINT8(24, note.note);
      TEST_ASSERT_EQUAL_UINT32(888u, note.startTick);
      TEST_ASSERT_EQUAL_UINT32(1032u, note.endTick);
    }
  }
  TEST_ASSERT_TRUE(foundHome);
}

// session_20260816_145518: openNoteEditSession rematerializes, then
// assignMissingNoteIdsInStore(editSession.store) — session copy only. commitEditAction
// rematerializes from capture chunks, which never received that id. Rows target 280; home stays.
void test_145518_open_assigned_note_id_missing_from_pass_rematerialize() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  constexpr uint32_t kLoopLength = 3072;
  constexpr uint8_t kPitch = 24;
  constexpr uint32_t kHomeStart = 888;
  constexpr uint32_t kHomeEnd = 1032;
  constexpr NoteId kAssignedId = 280;

  Loop loop;
  loop.loopLengthTicks = kLoopLength;
  loop.nextNoteId_ = kAssignedId;
  loop.passes.recordPass = makeRecordPassWithUnidentifiedNote(1, kHomeStart, kHomeEnd, kPitch);

  MidiEventVec passFlat;
  loop.passes.materializeToEventVector(passFlat, kLoopLength);
  TEST_ASSERT_EQUAL(kInvalidNoteId, noteIdAtPitchAndStart(passFlat, kPitch, kHomeStart));

  LoopEventStore sessionStore;
  loop.rematerializeEditView(sessionStore);
  loop.assignMissingNoteIdsInStore(sessionStore);
  MidiEventVec sessionFlat;
  sessionStore.copyEventsTo(sessionFlat);
  TEST_ASSERT_EQUAL(kAssignedId, noteIdAtPitchAndStart(sessionFlat, kPitch, kHomeStart));

  passFlat.clear();
  loop.passes.materializeToEventVector(passFlat, kLoopLength);
  TEST_ASSERT_EQUAL(kInvalidNoteId, noteIdAtPitchAndStart(passFlat, kPitch, kHomeStart));
  TEST_ASSERT_EQUAL(-1, findNoteOnById(passFlat, kAssignedId));

  pushEditPassRows(loop.passes, 1,
                   {makeNoteRangeRow(kAssignedId, kHomeStart, kHomeEnd, 312, 504),
                    makePitchRow(kAssignedId, 312, 504, 45)});
  passFlat.clear();
  loop.passes.materializeToEventVector(passFlat, kLoopLength);
  const std::vector<NoteUtils::DisplayNote> notes =
      NoteUtils::reconstructNotes(passFlat, kLoopLength, false);
  bool foundHome = false;
  for (const NoteUtils::DisplayNote& note : notes) {
    TEST_ASSERT_FALSE(note.note == 45 && note.startTick == 312);
    if (note.note == kPitch && note.startTick == kHomeStart) {
      foundHome = true;
      TEST_ASSERT_EQUAL_UINT32(kHomeEnd, note.endTick);
    }
  }
  TEST_ASSERT_TRUE(foundHome);
}

// Same unidentified capture note, but assign on committed chunks before rematerialize.
// Session and pass flats share 280; NoteRange+Pitch then moves the home (145518 Layer B fix).
void test_145518_assign_on_committed_passes_makes_rematerialize_find_id() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  constexpr uint32_t kLoopLength = 3072;
  constexpr uint8_t kPitch = 24;
  constexpr uint32_t kHomeStart = 888;
  constexpr uint32_t kHomeEnd = 1032;
  constexpr NoteId kAssignedId = 280;

  Loop loop;
  loop.loopLengthTicks = kLoopLength;
  loop.nextNoteId_ = kAssignedId;
  loop.passes.recordPass = makeRecordPassWithUnidentifiedNote(1, kHomeStart, kHomeEnd, kPitch);

  loop.assignMissingNoteIdsInCommittedCapturePasses();

  MidiEventVec passFlat;
  loop.passes.materializeToEventVector(passFlat, kLoopLength);
  TEST_ASSERT_EQUAL(kAssignedId, noteIdAtPitchAndStart(passFlat, kPitch, kHomeStart));

  LoopEventStore sessionStore;
  loop.rematerializeEditView(sessionStore);
  MidiEventVec sessionFlat;
  sessionStore.copyEventsTo(sessionFlat);
  TEST_ASSERT_EQUAL(kAssignedId, noteIdAtPitchAndStart(sessionFlat, kPitch, kHomeStart));

  pushEditPassRows(loop.passes, 1,
                   {makeNoteRangeRow(kAssignedId, kHomeStart, kHomeEnd, 312, 504),
                    makePitchRow(kAssignedId, 312, 504, 45)});
  passFlat.clear();
  loop.passes.materializeToEventVector(passFlat, kLoopLength);
  const std::vector<NoteUtils::DisplayNote> notes =
      NoteUtils::reconstructNotes(passFlat, kLoopLength, false);
  bool foundMoved = false;
  for (const NoteUtils::DisplayNote& note : notes) {
    TEST_ASSERT_FALSE(note.note == kPitch && note.startTick == kHomeStart);
    if (note.noteId == kAssignedId) {
      foundMoved = true;
      TEST_ASSERT_EQUAL_UINT8(45, note.note);
      TEST_ASSERT_EQUAL_UINT32(312u, note.startTick);
      TEST_ASSERT_EQUAL_UINT32(504u, note.endTick);
    }
  }
  TEST_ASSERT_TRUE(foundMoved);
}

MidiEvent noteOnWithId(uint32_t tick, uint8_t channel, uint8_t pitch, uint8_t velocity,
                       NoteId noteId);

// Pin 121141 L2324 pitch-24 merged dump order (Off@743 is the 9th event, past the occupy cap).
MidiEventVec make121141Pitch24CombinedL2324() {
  MidiEventVec events;
  events.push_back(noteOnWithId(144, 4, 24, 100, 6079));
  events.push_back(noteOnWithId(168, 4, 24, 100, 6073));
  events.push_back(MidiEvent::NoteOff(168, 4, 24, 0));
  events.push_back(MidiEvent::NoteOff(264, 4, 24, 0));
  events.push_back(MidiEvent::NoteOff(360, 4, 24, 0));
  events.push_back(noteOnWithId(408, 4, 24, 100, 6033));
  events.push_back(noteOnWithId(504, 4, 24, 100, 6040));
  events.push_back(MidiEvent::NoteOff(600, 4, 24, 0));
  events.push_back(MidiEvent::NoteOff(743, 4, 24, 0));
  return events;
}

int countPitch24OffAt(const MidiEventVec& events, uint32_t tick) {
  return countMatching(events, false, 24, tick);
}

bool hasNoteOnId(const MidiEventVec& events, NoteId noteId) {
  return findNoteOnById(events, noteId) >= 0;
}

MidiEventVec applyOneLength(const MidiEventVec& input, NoteId targetNoteId, uint32_t newEnd) {
  MidiEventVec events = input;
  applyNoteEditPass(events, makeLengthRow(targetNoteId, 0, 0, newEnd), 768);
  return events;
}

MidiEventVec applyOneDelete(const MidiEventVec& input, NoteId targetNoteId) {
  MidiEventVec events = input;
  applyNoteEditPass(events, makeDeleteRow(targetNoteId), 768);
  return events;
}

// Gate 4 named the pre-5A bug (Length 6073 stole Off@168 on On-then-Off order).
// Gate 5A: findNoteOffForOnIndex walks Off-before-On, so dump order no longer matters.
void test_121141_gate5a_on_then_off_length_pairing() {
  const MidiEventVec input = make121141Pitch24CombinedL2324();
  TEST_ASSERT_EQUAL(1, countPitch24OffAt(input, 168));
  TEST_ASSERT_TRUE(hasNoteOnId(input, 6079));
  TEST_ASSERT_TRUE(hasNoteOnId(input, 6073));

  {
    const MidiEventVec out = applyOneLength(input, 6079, 167);
    TEST_ASSERT_EQUAL(0, countPitch24OffAt(out, 168));
    TEST_ASSERT_EQUAL(1, countPitch24OffAt(out, 167));
    TEST_ASSERT_EQUAL(1, countPitch24OffAt(out, 264));
    TEST_ASSERT_TRUE(hasNoteOnId(out, 6079));
    TEST_ASSERT_TRUE(hasNoteOnId(out, 6073));
    const std::vector<NoteUtils::DisplayNote> notes =
        NoteUtils::reconstructNotes(out, 768, false);
    bool foundA = false;
    bool foundB = false;
    for (const NoteUtils::DisplayNote& note : notes) {
      if (note.noteId == 6079 && note.note == 24) {
        foundA = true;
        TEST_ASSERT_EQUAL_UINT32(144u, note.startTick);
        TEST_ASSERT_EQUAL_UINT32(167u, note.endTick);
      }
      if (note.noteId == 6073 && note.note == 24) {
        foundB = true;
        TEST_ASSERT_EQUAL_UINT32(168u, note.startTick);
        TEST_ASSERT_EQUAL_UINT32(264u, note.endTick);
      }
    }
    TEST_ASSERT_TRUE(foundA);
    TEST_ASSERT_TRUE(foundB);
  }
  {
    const MidiEventVec out = applyOneLength(input, 6040, 551);
    TEST_ASSERT_EQUAL(1, countPitch24OffAt(out, 168));
    TEST_ASSERT_TRUE(hasNoteOnId(out, 6079));
    TEST_ASSERT_TRUE(hasNoteOnId(out, 6073));
  }
  {
    const MidiEventVec out = applyOneLength(input, 6033, 551);
    TEST_ASSERT_EQUAL(1, countPitch24OffAt(out, 168));
    TEST_ASSERT_TRUE(hasNoteOnId(out, 6079));
    TEST_ASSERT_TRUE(hasNoteOnId(out, 6073));
  }
  {
    const MidiEventVec out = applyOneDelete(input, 6079);
    TEST_ASSERT_EQUAL(0, countPitch24OffAt(out, 168));
    TEST_ASSERT_FALSE(hasNoteOnId(out, 6079));
    TEST_ASSERT_TRUE(hasNoteOnId(out, 6073));
  }
  {
    const MidiEventVec out = applyOneDelete(input, 6073);
    TEST_ASSERT_EQUAL(1, countPitch24OffAt(out, 168));
    TEST_ASSERT_FALSE(hasNoteOnId(out, 6073));
    TEST_ASSERT_TRUE(hasNoteOnId(out, 6079));
  }
  {
    const MidiEventVec out = applyOneLength(input, 6073, 167);
    TEST_ASSERT_EQUAL(1, countPitch24OffAt(out, 168));
    TEST_ASSERT_EQUAL(1, countPitch24OffAt(out, 167));
    TEST_ASSERT_EQUAL(0, countPitch24OffAt(out, 264));
    TEST_ASSERT_TRUE(hasNoteOnId(out, 6073));
    TEST_ASSERT_TRUE(hasNoteOnId(out, 6079));
  }
  {
    const MidiEventVec out = applyOneLength(input, 6073, 551);
    TEST_ASSERT_EQUAL(1, countPitch24OffAt(out, 168));
    TEST_ASSERT_EQUAL(1, countPitch24OffAt(out, 551));
    TEST_ASSERT_EQUAL(0, countPitch24OffAt(out, 264));
    TEST_ASSERT_EQUAL(1, countPitch24OffAt(out, 360));
    TEST_ASSERT_TRUE(hasNoteOnId(out, 6073));
    TEST_ASSERT_TRUE(hasNoteOnId(out, 6079));
  }
  {
    const MidiEventVec out = applyOneLength(input, 6073, 264);
    TEST_ASSERT_EQUAL(1, countPitch24OffAt(out, 168));
    TEST_ASSERT_EQUAL(1, countPitch24OffAt(out, 264));
    TEST_ASSERT_TRUE(hasNoteOnId(out, 6073));
    TEST_ASSERT_TRUE(hasNoteOnId(out, 6079));
  }
}

void test_121141_gate4_per_layer_6040_cannot_steal_off168_from_6073_layer() {
  MidiEventVec lateLayer;
  lateLayer.push_back(noteOnWithId(144, 4, 24, 100, 6079));
  lateLayer.push_back(noteOnWithId(168, 4, 24, 100, 6073));
  lateLayer.push_back(MidiEvent::NoteOff(168, 4, 24, 0));
  lateLayer.push_back(MidiEvent::NoteOff(264, 4, 24, 0));
  lateLayer.push_back(MidiEvent::NoteOff(360, 4, 24, 0));

  const MidiEventVec after6040 = applyOneLength(lateLayer, 6040, 551);
  TEST_ASSERT_EQUAL(1, countPitch24OffAt(after6040, 168));

  const MidiEventVec after6073 = applyOneLength(lateLayer, 6073, 551);
  TEST_ASSERT_EQUAL(1, countPitch24OffAt(after6073, 168));
  TEST_ASSERT_TRUE(hasNoteOnId(after6073, 6073));
  TEST_ASSERT_TRUE(hasNoteOnId(after6073, 6079));
}

void test_121141_gate4_tick_sorted_early_layer_length_6040_leaves_off168() {
  MidiEventVec earlyLayer;
  earlyLayer.push_back(MidiEvent::NoteOff(168, 4, 24, 0));
  earlyLayer.push_back(noteOnWithId(408, 4, 24, 100, 6033));
  earlyLayer.push_back(noteOnWithId(504, 4, 24, 100, 6040));
  earlyLayer.push_back(MidiEvent::NoteOff(600, 4, 24, 0));
  earlyLayer.push_back(MidiEvent::NoteOff(743, 4, 24, 0));

  const MidiEventVec after6040 = applyOneLength(earlyLayer, 6040, 551);
  TEST_ASSERT_EQUAL(1, countPitch24OffAt(after6040, 168));
}

// Gate 5A: Length 6073 551 still moves Off@264 after pairing; leftover Off@360 becomes B.
void test_121141_gate5_off_before_on_length_6073_551_leaves_off168_reconstruct_b_168_360() {
  MidiEventVec events = make121141Pitch24CombinedL2324();
  NoteUtils::sortMidiEventsChronologically(events);
  TEST_ASSERT_EQUAL(1, countPitch24OffAt(events, 168));
  const MidiEventVec out = applyOneLength(events, 6073, 551);
  TEST_ASSERT_EQUAL(1, countPitch24OffAt(out, 168));
  TEST_ASSERT_EQUAL(0, countPitch24OffAt(out, 264));
  TEST_ASSERT_EQUAL(1, countPitch24OffAt(out, 551));
  TEST_ASSERT_EQUAL(1, countPitch24OffAt(out, 360));
  TEST_ASSERT_TRUE(hasNoteOnId(out, 6079));
  TEST_ASSERT_TRUE(hasNoteOnId(out, 6073));
  const std::vector<NoteUtils::DisplayNote> notes =
      NoteUtils::reconstructNotes(out, 768, false);
  bool foundA = false;
  bool foundB = false;
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.noteId == 6079 && note.note == 24) {
      foundA = true;
      TEST_ASSERT_EQUAL_UINT32(144u, note.startTick);
      TEST_ASSERT_EQUAL_UINT32(168u, note.endTick);
      TEST_ASSERT_FALSE(OverlapNoteIdObservation::displayNotePresentAtHold(
          note.startTick, note.endTick, 312, 768));
    }
    if (note.noteId == 6073 && note.note == 24) {
      foundB = true;
      TEST_ASSERT_EQUAL_UINT32(168u, note.startTick);
      TEST_ASSERT_EQUAL_UINT32(360u, note.endTick);
      TEST_ASSERT_TRUE(OverlapNoteIdObservation::displayNotePresentAtHold(
          note.startTick, note.endTick, 312, 768));
    }
  }
  TEST_ASSERT_TRUE(foundA);
  TEST_ASSERT_TRUE(foundB);
}

// Gate 5A: Length 6079 167 owns Off@168 and reconstructs A 144–167 / B 168–264.
void test_121141_gate5_off_before_on_length_6079_167_shortens_a_off168() {
  MidiEventVec events = make121141Pitch24CombinedL2324();
  NoteUtils::sortMidiEventsChronologically(events);
  const MidiEventVec out = applyOneLength(events, 6079, 167);
  TEST_ASSERT_EQUAL(0, countPitch24OffAt(out, 168));
  TEST_ASSERT_EQUAL(1, countPitch24OffAt(out, 167));
  TEST_ASSERT_EQUAL(1, countPitch24OffAt(out, 264));
  TEST_ASSERT_TRUE(hasNoteOnId(out, 6079));
  TEST_ASSERT_TRUE(hasNoteOnId(out, 6073));
  const std::vector<NoteUtils::DisplayNote> notes =
      NoteUtils::reconstructNotes(out, 768, false);
  bool foundA = false;
  bool foundB = false;
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.noteId == 6079 && note.note == 24) {
      foundA = true;
      TEST_ASSERT_EQUAL_UINT32(144u, note.startTick);
      TEST_ASSERT_EQUAL_UINT32(167u, note.endTick);
      TEST_ASSERT_FALSE(OverlapNoteIdObservation::displayNotePresentAtHold(
          note.startTick, note.endTick, 312, 768));
    }
    if (note.noteId == 6073 && note.note == 24) {
      foundB = true;
      TEST_ASSERT_EQUAL_UINT32(168u, note.startTick);
      TEST_ASSERT_EQUAL_UINT32(264u, note.endTick);
      TEST_ASSERT_FALSE(OverlapNoteIdObservation::displayNotePresentAtHold(
          note.startTick, note.endTick, 312, 768));
    }
  }
  TEST_ASSERT_TRUE(foundA);
  TEST_ASSERT_TRUE(foundB);
}

MidiEvent noteOnWithId(uint32_t tick, uint8_t channel, uint8_t pitch, uint8_t velocity,
                       NoteId noteId) {
  MidiEvent evt = MidiEvent::NoteOn(tick, channel, pitch, velocity);
  evt.noteId = noteId;
  return evt;
}

// Nested same-pitch pair: On(A)@100, On(B)@150, Off(B)@180, Off(A)@300.
// findNoteOffForOnIndex must pair the outer note by LIFO, not break on the inner on.
MidiEventVec makeNestedSamePitchPair() {
  MidiEventVec events;
  events.push_back(noteOnWithId(100, 1, 60, 100, 1));
  events.push_back(noteOnWithId(150, 1, 60, 100, 2));
  events.push_back(MidiEvent::NoteOff(180, 1, 60, 0));
  events.push_back(MidiEvent::NoteOff(300, 1, 60, 0));
  return events;
}

void assertInnerNoteUnchanged(const MidiEventVec& events) {
  const int innerOn = findNoteOnById(events, 2);
  TEST_ASSERT_TRUE(innerOn >= 0);
  TEST_ASSERT_EQUAL_UINT32(150u, events[static_cast<size_t>(innerOn)].tick);
  TEST_ASSERT_EQUAL_UINT8(60, events[static_cast<size_t>(innerOn)].data.noteData.note);
  TEST_ASSERT_EQUAL(1, countMatching(events, false, 60, 180));
}

void test_nested_same_pitch_outer_apply_helpers_use_lifo_off() {
  constexpr uint32_t kLoopLength = 768;
  constexpr NoteId kOuterId = 1;

  {
    MidiEventVec events = makeNestedSamePitchPair();
    applyNoteEditPass(events, makeDeleteRow(kOuterId), kLoopLength);
    TEST_ASSERT_EQUAL(-1, findNoteOnById(events, kOuterId));
    TEST_ASSERT_EQUAL(0, countMatching(events, false, 60, 300));
    assertInnerNoteUnchanged(events);
  }

  {
    MidiEventVec events = makeNestedSamePitchPair();
    applyNoteEditPass(events, makeNoteRangeRow(kOuterId, 100, 300, 200, 400), kLoopLength);
    const int outerOn = findNoteOnById(events, kOuterId);
    TEST_ASSERT_TRUE(outerOn >= 0);
    TEST_ASSERT_EQUAL_UINT32(200u, events[static_cast<size_t>(outerOn)].tick);
    TEST_ASSERT_EQUAL(1, countMatching(events, false, 60, 400));
    TEST_ASSERT_EQUAL(0, countMatching(events, false, 60, 300));
    assertInnerNoteUnchanged(events);
  }

  {
    MidiEventVec events = makeNestedSamePitchPair();
    applyNoteEditPass(events, makePitchRow(kOuterId, 100, 300, 67), kLoopLength);
    const int outerOn = findNoteOnById(events, kOuterId);
    TEST_ASSERT_TRUE(outerOn >= 0);
    TEST_ASSERT_EQUAL_UINT8(67, events[static_cast<size_t>(outerOn)].data.noteData.note);
    TEST_ASSERT_EQUAL(1, countMatching(events, false, 67, 300));
    TEST_ASSERT_EQUAL(0, countMatching(events, false, 60, 300));
    assertInnerNoteUnchanged(events);
  }

  {
    MidiEventVec events = makeNestedSamePitchPair();
    applyNoteEditPass(events, makeLengthRow(kOuterId, 100, 300, 250), kLoopLength);
    const int outerOn = findNoteOnById(events, kOuterId);
    TEST_ASSERT_TRUE(outerOn >= 0);
    TEST_ASSERT_EQUAL_UINT32(100u, events[static_cast<size_t>(outerOn)].tick);
    TEST_ASSERT_EQUAL(1, countMatching(events, false, 60, 250));
    TEST_ASSERT_EQUAL(0, countMatching(events, false, 60, 300));
    int outerOffs = 0;
    for (const MidiEvent& evt : events) {
      if (evt.isNoteOff() && evt.data.noteData.note == 60 && evt.tick != 180) {
        ++outerOffs;
      }
    }
    TEST_ASSERT_EQUAL(1, outerOffs);
    assertInnerNoteUnchanged(events);
  }
}

// session_20260816_195941: wrap home Off@96 then On@2592. NoteRange must move that wrap off.
// Session storage is linear (2784+576=3360). A wrap row (2784–288) must keep length 576 too.
void test_195941_wrap_home_move_keeps_length() {
  constexpr uint32_t kLoopLength = 3072;
  constexpr uint8_t kPitch = 12;
  constexpr NoteId kId = 269;
  constexpr uint32_t kHomeOn = 2592;
  constexpr uint32_t kHomeWrapOff = 96;
  constexpr uint32_t kNewStart = 2784;
  constexpr uint32_t kNoteLen = 576;
  constexpr uint32_t kLinearOff = kNewStart + kNoteLen;
  constexpr uint32_t kWrapOff = kLinearOff % kLoopLength;

  const auto makeWrapHome = [&]() {
    MidiEventVec events;
    events.push_back(MidiEvent::NoteOff(kHomeWrapOff, 1, kPitch, 0));
    MidiEvent on = MidiEvent::NoteOn(kHomeOn, 1, kPitch, 100);
    on.noteId = kId;
    events.push_back(on);
    return events;
  };

  {
    MidiEventVec events = makeWrapHome();
    applyNoteEditPass(events, makeNoteRangeRow(kId, kHomeOn, kHomeWrapOff, kNewStart, kLinearOff),
                      kLoopLength);
    const int onIndex = findNoteOnById(events, kId);
    TEST_ASSERT_TRUE(onIndex >= 0);
    TEST_ASSERT_EQUAL_UINT32(kNewStart, events[static_cast<size_t>(onIndex)].tick);
    TEST_ASSERT_EQUAL(0, countMatching(events, true, kPitch, kHomeOn));
    TEST_ASSERT_EQUAL(1, countMatching(events, true, kPitch, kNewStart));
    TEST_ASSERT_EQUAL(0, countMatching(events, false, kPitch, kHomeWrapOff));
    TEST_ASSERT_EQUAL(1, countMatching(events, false, kPitch, kLinearOff));
  }

  {
    MidiEventVec events = makeWrapHome();
    applyNoteEditPass(events, makeNoteRangeRow(kId, kHomeOn, kHomeWrapOff, kNewStart, kWrapOff),
                      kLoopLength);
    const int onIndex = findNoteOnById(events, kId);
    TEST_ASSERT_TRUE(onIndex >= 0);
    TEST_ASSERT_EQUAL_UINT32(kNewStart, events[static_cast<size_t>(onIndex)].tick);
    TEST_ASSERT_EQUAL(0, countMatching(events, false, kPitch, kHomeWrapOff));
    TEST_ASSERT_EQUAL(1, countMatching(events, false, kPitch, kWrapOff));
  }
}

// session_20260816_201446: wrap home has Off@96 AND a false linear Off@2688. LIFO pairs the
// linear off and leaves the wrap head; deselect reconstructs start wrap-to-96 (length 336).
void test_201446_wrap_home_move_drops_false_linear_off() {
  constexpr uint32_t kLoopLength = 3072;
  constexpr uint8_t kPitch = 12;
  constexpr NoteId kId = 269;
  constexpr uint32_t kHomeOn = 2592;
  constexpr uint32_t kHomeWrapOff = 96;
  constexpr uint32_t kFalseLinearOff = 2688;
  constexpr uint32_t kNewStart = 2832;
  constexpr uint32_t kNoteLen = 576;
  constexpr uint32_t kLinearOff = kNewStart + kNoteLen;

  MidiEventVec events;
  events.push_back(MidiEvent::NoteOff(kHomeWrapOff, 1, kPitch, 0));
  MidiEvent on = MidiEvent::NoteOn(kHomeOn, 1, kPitch, 100);
  on.noteId = kId;
  events.push_back(on);
  events.push_back(MidiEvent::NoteOff(kFalseLinearOff, 1, kPitch, 0));

  applyNoteEditPass(events, makeNoteRangeRow(kId, kHomeOn, kFalseLinearOff, kNewStart, kLinearOff),
                    kLoopLength);

  const int onIndex = findNoteOnById(events, kId);
  TEST_ASSERT_TRUE(onIndex >= 0);
  TEST_ASSERT_EQUAL_UINT32(kNewStart, events[static_cast<size_t>(onIndex)].tick);
  TEST_ASSERT_EQUAL(0, countMatching(events, true, kPitch, kHomeOn));
  TEST_ASSERT_EQUAL(1, countMatching(events, true, kPitch, kNewStart));
  TEST_ASSERT_EQUAL(0, countMatching(events, false, kPitch, kHomeWrapOff));
  TEST_ASSERT_EQUAL(0, countMatching(events, false, kPitch, kFalseLinearOff));
  TEST_ASSERT_EQUAL(1, countMatching(events, false, kPitch, kLinearOff));
}

// Regression for the HITL overlap round-trip: M0 lengthened to 681 overlaps P0 (585..682)
// without nesting. A pitch edit on M0 must not relabel P0's note-off (tick 682).
void test_change_pitch_on_overlapping_note_keeps_neighbor_endtick() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopPasses passes;
  passes.recordPass = makeRecordPassWithTwoNotes(1, 9, 105, 585, 682, 60);



  pushEditPassRow(passes, 1, makeLengthRow(1, 9, 105, 681));
  pushEditPassRow(passes, 2, makePitchRow(1, 9, 681, 67));

  constexpr uint32_t kLoopLength = 1536;
  MidiEventVec flat;
  passes.materializeToEventVector(flat, kLoopLength);

  const int movingOn = findNoteOnById(flat, 1);
  TEST_ASSERT_TRUE(movingOn >= 0);
  TEST_ASSERT_EQUAL_UINT8(67, flat[static_cast<size_t>(movingOn)].data.noteData.note);
  const std::vector<NoteUtils::DisplayNote> notes =
      NoteUtils::reconstructNotes(flat, kLoopLength, false);
  bool foundM0 = false;
  bool foundP0 = false;
  for (const NoteUtils::DisplayNote& dn : notes) {
    if (dn.noteId == 1) {
      foundM0 = true;
      TEST_ASSERT_EQUAL_UINT8(67, dn.note);
      TEST_ASSERT_TRUE(dn.endTick > 105u);
    }
    if (dn.noteId == 2) {
      foundP0 = true;
      TEST_ASSERT_EQUAL_UINT8(60, dn.note);
      TEST_ASSERT_EQUAL_UINT32(585u, dn.startTick);
    }
  }
  TEST_ASSERT_TRUE(foundM0);
  TEST_ASSERT_TRUE(foundP0);
}

void test_apply_edits_delete_note() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopPasses passes;
  passes.recordPass = makeRecordPassWithNote(1, 10);

  pushEditPassRow(passes, 1, makeDeleteRow(1));

  LoopEventStore out;
  passes.materialize(out);
  TEST_ASSERT_TRUE(out.empty());
}

void test_save_edit_appends_without_collapsing_takes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.passes.recordPass = makeRecordPassWithNote(1, 10);
  {
    LoopEventStore odStore;
    TEST_ASSERT_TRUE(storeAppendNoteOn(odStore, 100, 1, 60, 100, 2));
    TEST_ASSERT_TRUE(odStore.append(MidiEvent::NoteOff(110, 1, 60, 0)));
    CommittedChunkIdList committedChunkIds;
    TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(odStore, committedChunkIds));
    OverdubPass overdub{};
    overdub.id = 2;
    overdub.mergeSequence = 1;
    overdub.state = CapturePassState::Active;
    overdub.committedChunkIds = std::move(committedChunkIds);
    loop.passes.overdubPasses.push_back(std::move(overdub));
  }
  loop.nextPassId_ = 3;

  const EditPassId id = loop.saveNoteEditPass(0, makeDeleteRow(1));
  TEST_ASSERT_EQUAL(3u, id);
  TEST_ASSERT_EQUAL(2u, loop.passes.capturePassCount());
  TEST_ASSERT_EQUAL(1u, loop.passes.editPasses.size());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPassType::Note),
                          static_cast<uint8_t>(loop.passes.editPasses[0].passType));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditActionType::Delete),
                          static_cast<uint8_t>(loop.passes.editPasses[0].actionType));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPropertyType::None),
                          static_cast<uint8_t>(loop.passes.editPasses[0].propertyType));

  MidiEventVec flat;
  loop.passes.materializeToEventVector(flat);
  TEST_ASSERT_EQUAL(2u, flat.size());
}

void test_save_note_edit_pass_sets_scoped_action_property_mapping() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;

  const EditPassId delId = loop.saveNoteEditPass(2, makeDeleteRow(1));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, delId);
  const EditPass& delPass = loop.passes.editPasses.back();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPassType::Note),
                          static_cast<uint8_t>(delPass.passType));
  TEST_ASSERT_EQUAL_UINT8(2u, delPass.editPassIndex);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditActionType::Delete),
                          static_cast<uint8_t>(delPass.actionType));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPropertyType::None),
                          static_cast<uint8_t>(delPass.propertyType));

  const EditPassId pitchId = loop.saveNoteEditPass(3, makePitchRow(1, 10, 20, 67));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, pitchId);
  const EditPass& pitchPass = loop.passes.editPasses.back();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditActionType::Update),
                          static_cast<uint8_t>(pitchPass.actionType));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPropertyType::Pitch),
                          static_cast<uint8_t>(pitchPass.propertyType));

  const EditPassId lengthId = loop.saveNoteEditPass(4, makeLengthRow(1, 10, 20, 28));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, lengthId);
  const EditPass& lengthPass = loop.passes.editPasses.back();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditActionType::Update),
                          static_cast<uint8_t>(lengthPass.actionType));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPropertyType::Length),
                          static_cast<uint8_t>(lengthPass.propertyType));

  const EditPassId moveStartId =
      loop.saveNoteEditPass(5, makeNoteRangeRow(1, 10, 20, 14, 24));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, moveStartId);
  const EditPass& moveStartPass = loop.passes.editPasses.back();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPropertyType::NoteRange),
                          static_cast<uint8_t>(moveStartPass.propertyType));

  const EditPassId moveEndId =
      loop.saveNoteEditPass(6, makeNoteRangeRow(1, 10, 20, 10, 24));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, moveEndId);
  const EditPass& moveEndPass = loop.passes.editPasses.back();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPropertyType::NoteRange),
                          static_cast<uint8_t>(moveEndPass.propertyType));
}

void test_edit_pass_state_toggle_does_not_append_edit_actions() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;

  const EditPassId id = loop.saveNoteEditPass(0, makeDeleteRow(1));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, id);
  TEST_ASSERT_EQUAL(1u, loop.passes.editPasses.size());
  const EditActionType beforeAction = loop.passes.editPasses[0].actionType;

  loop.disableEditPasses(EditPassIdList{id});
  TEST_ASSERT_EQUAL(1u, loop.passes.editPasses.size());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPassState::Disabled),
                          static_cast<uint8_t>(loop.passes.editPasses[0].state));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(beforeAction),
                          static_cast<uint8_t>(loop.passes.editPasses[0].actionType));

  loop.passes.editPasses[0].state = EditPassState::Active;
  TEST_ASSERT_EQUAL(1u, loop.passes.editPasses.size());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPassState::Active),
                          static_cast<uint8_t>(loop.passes.editPasses[0].state));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(beforeAction),
                          static_cast<uint8_t>(loop.passes.editPasses[0].actionType));
}

void test_disable_edits_restores_take_only_view() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.passes.recordPass = makeRecordPassWithNote(1, 10);
  const EditPassId id = loop.saveNoteEditPass(0, makeDeleteRow(1));
  loop.disableEditPasses(EditPassIdList{id});
  MidiEventVec flat;
  loop.passes.materializeToEventVector(flat);
  TEST_ASSERT_EQUAL(2u, flat.size());
}

void test_reset_take_timeline_clears_stale_edits() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.passes.recordPass = makeRecordPassWithNote(1, 8);
  loop.nextPassId_ = 2;

  loop.saveNoteEditPass(0, makePitchRow(1, 8, 18, 67));
  TEST_ASSERT_EQUAL(1u, loop.passes.editPasses.size());

  MidiEventVec withStaleEdit;
  loop.passes.materializeToEventVector(withStaleEdit);
  TEST_ASSERT_EQUAL(67, withStaleEdit[0].data.noteData.note);

  loop.resetPassTimeline();
  TEST_ASSERT_TRUE(loop.passes.editPasses.empty());
  TEST_ASSERT_EQUAL(1u, loop.nextPassId_);
  TEST_ASSERT_FALSE(loop.isEditStateDirty());

  loop.passes.recordPass = makeRecordPassWithNote(1, 8);
  MidiEventVec fresh;
  loop.passes.materializeToEventVector(fresh);
  TEST_ASSERT_EQUAL(2u, fresh.size());
  TEST_ASSERT_EQUAL(60, fresh[0].data.noteData.note);
  TEST_ASSERT_EQUAL(60, fresh[1].data.noteData.note);
}

void test_note_edit_session_undo_stack() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  NoteEditSessionUndoStack stack;
  SessionUndoEntry entry;
  entry.editRows.push_back({});
  TEST_ASSERT_TRUE(stack.pushEntry(entry));
  TEST_ASSERT_TRUE(stack.canUndo());
  const SessionUndoEntry* target = stack.popUndoTarget();
  TEST_ASSERT_NOT_NULL(target);
}

void test_add_note_rematerialize_session_store() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.passes.recordPass = makeRecordPassWithNote(1, 10);
  loop.loopLengthTicks = 768;
  loop.nextPassId_ = 2;

  EditPass add{};
  add.actionType = EditActionType::Create;
  add.addedEvents.push_back(noteOnWithNoteId(48, 5, 60, 80, 10));
  add.addedEvents.push_back(MidiEvent::NoteOff(72, 5, 60, 0));
  const EditPassId id = loop.saveNoteEditPass(0, add);
  TEST_ASSERT_EQUAL(2u, id);

  CowLoopEventStore session;
  loop.rematerializeEditView(session.mutStore());
  session.discardEventsCache();

  const auto& flat = session.readEvents();
  TEST_ASSERT_EQUAL(4u, flat.size());
  TEST_ASSERT_EQUAL(1, countMatching(flat, true, 60, 10));
  TEST_ASSERT_EQUAL(1, countMatching(flat, true, 60, 48));
}

void test_move_note_uses_track_channel() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopPasses passes;
  passes.recordPass = makeRecordPassWithNote(1, 10);
  passes.recordPass.committedChunkIds.clear();
  LoopEventStore store;
  resetNoteIdCounter();
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 10, 5, 60, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(106, 5, 60, 0)));
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, committedChunkIds));
  passes.recordPass.committedChunkIds = std::move(committedChunkIds);

  pushEditPassRow(passes, 1, makeNoteRangeRow(1, 10, 106, 58, 154));

  MidiEventVec flat;
  passes.materializeToEventVector(flat);
  TEST_ASSERT_EQUAL(1, countMatching(flat, true, 60, 58));
  TEST_ASSERT_EQUAL(0, countMatching(flat, true, 60, 10));

  LoopPasses badPasses = passes;
  badPasses.editPasses.clear();
  EditPass badRow = makeNoteRangeRow(1, 10, 106, 58, 154);
  badRow.targetNoteId = 99;
  pushEditPassRow(badPasses, 1, badRow);
  MidiEventVec flatBad;
  badPasses.materializeToEventVector(flatBad);
  TEST_ASSERT_EQUAL(1, countMatching(flatBad, true, 60, 10));
  TEST_ASSERT_EQUAL(0, countMatching(flatBad, true, 60, 58));
}

void test_lengthen_commit_rematerialize_hitl_fixture() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  constexpr uint32_t kLoopLength = 1536;
  Loop loop;
  loop.loopLengthTicks = kLoopLength;
  loop.passes.recordPass = makeEditRecordFixtureRecordPassCh5(1);
  loop.passes.recordPass.id = 1;
  {
    LoopEventStore store;
    resetNoteIdCounter();
    appendFixtureNotePair(store, 8, 104, 5, 60, 1);
    appendFixtureNotePair(store, 200, 296, 5, 64, 2);
    appendFixtureNotePair(store, 392, 488, 5, 67, 3);
    appendFixtureNotePair(store, 585, 680, 5, 60, 4);
    CommittedChunkIdList committedChunkIds;
    TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, committedChunkIds));
    loop.passes.recordPass.committedChunkIds = std::move(committedChunkIds);
  }

  const EditPassId id = loop.saveNoteEditPass(0, makeLengthRow(1, 8, 104, 680));
  TEST_ASSERT_EQUAL(1u, id);

  CowLoopEventStore session;
  loop.rematerializeEditView(session.mutStore());
  session.discardEventsCache();

  const NoteUtils::DisplayNoteVec notes =
      NoteUtils::reconstructDisplayNotes(session.readEvents(), kLoopLength, false);
  bool foundM0 = false;
  for (const NoteUtils::DisplayNote& n : notes) {
    if (n.note == 60 && n.startTick == 8) {
      foundM0 = true;
      TEST_ASSERT_EQUAL(680, n.endTick);
    }
  }
  TEST_ASSERT_TRUE(foundM0);
}

// HITL 195830: M0@40-136 lengthen to 712 with P0@616-712 (channel 5, loop 1536).
// Mirrors commitAllPendingNoteEditActions ChangeLength-only commit + rematerialize.
void test_change_length_rematerialize_hitl_195830_ticks() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  constexpr uint32_t kLoopLength = 1536;
  Loop loop;
  loop.loopLengthTicks = kLoopLength;
  loop.passes.recordPass = makeEditRecordFixtureRecordPassCh5_195830(1);

  const EditPassId id = loop.saveNoteEditPass(0, makeLengthRow(1, 40, 136, 712));
  TEST_ASSERT_EQUAL(1u, id);

  // commitEditAction: discard live flat, replay takes+edits, load session store (matches firmware).
  CowLoopEventStore session;
  session.discardEventsCache();
  MidiEventVec loopMidiEventsFromPasses;
  loop.passes.materializeToEventVector(loopMidiEventsFromPasses, kLoopLength);
  session.mutStore().loadFromEvents(loopMidiEventsFromPasses);
  session.discardEventsCache();

  const NoteUtils::DisplayNoteVec notes =
      NoteUtils::reconstructDisplayNotes(session.readEvents(), kLoopLength, false);
  bool foundM0Home = false;
  bool foundP0 = false;
  for (const NoteUtils::DisplayNote& n : notes) {
    if (n.note == 60 && n.startTick == 40) {
      foundM0Home = true;
      TEST_ASSERT_EQUAL(712, n.endTick);
    }
    if (n.note == 60 && n.startTick == 616) {
      foundP0 = true;
      TEST_ASSERT_EQUAL(712, n.endTick);
    }
  }
  TEST_ASSERT_TRUE(foundM0Home);
  TEST_ASSERT_TRUE(foundP0);

  int offAt712 = 0;
  for (const MidiEvent& e : session.readEvents()) {
    if (e.isNoteOff() && e.channel == 5 && e.data.noteData.note == 60 && e.tick == 712) {
      offAt712++;
    }
  }
  TEST_ASSERT_EQUAL(2, offAt712);
}

// PR4 / B1: lengthen → move-over (hidden P0) → pitch → home replay via ordered pre-commit edits.
void test_overlap_round_trip_replay_lengthen_delete_pitch() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  constexpr uint32_t kLoopLength = 1536;
  LoopPasses passes;
  passes.recordPass = makeEditRecordFixtureRecordPass(1);


  pushEditPassRow(passes, 1, makeLengthRow(1, 8, 104, 680));
  pushEditPassRow(passes, 2, makeDeleteRow(4));
  pushEditPassRow(passes, 3, makePitchRow(1, 8, 680, 67));

  MidiEventVec flat;
  passes.materializeToEventVector(flat, kLoopLength);

  TEST_ASSERT_EQUAL(1, countMatching(flat, true, 67, 8));
  TEST_ASSERT_EQUAL(1, countMatching(flat, false, 67, 680));
  TEST_ASSERT_EQUAL(0, countMatching(flat, true, 60, 584));
  TEST_ASSERT_EQUAL(1, countMatching(flat, true, 67, 392));
  TEST_ASSERT_EQUAL(1, countMatching(flat, false, 67, 488));
}

// Pre-commit order: overlap DeleteNote + ChangeLength before MoveNote / ChangePitch.
void test_pre_commit_order_overlap_changes_before_move_and_pitch() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  constexpr uint32_t kLoopLength = 1536;
  LoopPasses passes;
  passes.recordPass = makeRecordPassWithTwoNotes(1, 8, 680, 584, 680, 60);

  pushEditPassRow(passes, 1, makeLengthRow(2, 584, 680, 495));
  pushEditPassRow(passes, 2, makeNoteRangeRow(1, 8, 680, 496, 1168));
  pushEditPassRow(passes, 3, makePitchRow(1, 8, 680, 67));

  MidiEventVec flatOrdered;
  passes.materializeToEventVector(flatOrdered, kLoopLength);
  TEST_ASSERT_EQUAL(1, countMatching(flatOrdered, true, 67, 496));
  TEST_ASSERT_EQUAL(1, countMatching(flatOrdered, false, 67, 1168));
  TEST_ASSERT_EQUAL(1, countMatching(flatOrdered, true, 60, 584));
  TEST_ASSERT_EQUAL(1, countMatching(flatOrdered, false, 60, 495));

  LoopPasses passes2;
  passes2.recordPass = makeRecordPassWithTwoNotes(1, 8, 680, 584, 680, 60);
  pushEditPassRow(passes2, 1, makeNoteRangeRow(1, 8, 680, 496, 1168));
  pushEditPassRow(passes2, 2, makePitchRow(1, 8, 680, 67));
  MidiEventVec flatWrong;
  passes2.materializeToEventVector(flatWrong, kLoopLength);
  TEST_ASSERT_EQUAL(1, countMatching(flatWrong, true, 67, 496));
  TEST_ASSERT_EQUAL(0, countMatching(flatWrong, false, 60, 495));
  TEST_ASSERT_EQUAL(1, countMatching(flatWrong, false, 60, 680));
}

void test_pre_commit_delete_before_mover_change_length() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopPasses passes;
  passes.recordPass = makeRecordPassWithTwoNotes(1, 8, 104, 584, 680, 60);

  pushEditPassRow(passes, 1, makeDeleteRow(2));
  pushEditPassRow(passes, 2, makeLengthRow(1, 8, 104, 680));
  MidiEventVec flat;
  passes.materializeToEventVector(flat);
  TEST_ASSERT_EQUAL(1, countMatching(flat, true, 60, 8));
  TEST_ASSERT_EQUAL(1, countMatching(flat, false, 60, 680));
  TEST_ASSERT_EQUAL(0, countMatching(flat, true, 60, 584));
}

void test_playback_merge_includes_active_edit_passes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.passes.recordPass = makeRecordPassWithNote(1, 10);
  loop.loopLengthTicks = 768;
  loop.nextPassId_ = 10;

  const EditPassId lengthId = loop.saveNoteEditPass(0, makeLengthRow(1, 10, 20, 28));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, lengthId);

  auto noteOffTick = [](const MidiEventVec& flat) -> uint32_t {
    for (const MidiEvent& e : flat) {
      if (e.isNoteOff() && e.data.noteData.note == 60) {
        return e.tick;
      }
    }
    return UINT32_MAX;
  };

  MidiEventVec captureOnly;
  loop.mergeActiveCapturePasses(captureOnly);
  TEST_ASSERT_EQUAL_UINT32(20u, noteOffTick(captureOnly));

  MidiEventVec materializedPlayback;
  loop.copyEffectiveCommittedEventsWithCapture(materializedPlayback);
  TEST_ASSERT_EQUAL_UINT32(28u, noteOffTick(materializedPlayback));
}

void test_global_undo_overdub_pass_added_disables_overdub() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = 768;
  loop.passes.recordPass = makeRecordPassWithNote(1, 10);
  loop.passes.overdubPasses.push_back(makeOverdubPassWithNote(2, 100, 72));
  loop.nextPassId_ = 3;

  MidiEventVec withOverdub;
  loop.passes.materializeToEventVector(withOverdub, loop.loopLengthTicks);
  TEST_ASSERT_EQUAL(1, countNoteOns(withOverdub, 60));
  TEST_ASSERT_EQUAL(1, countNoteOns(withOverdub, 72));

  TEST_ASSERT_TRUE(loop.setCapturePassState(2, CapturePassState::Disabled));

  MidiEventVec afterUndo;
  loop.passes.materializeToEventVector(afterUndo, loop.loopLengthTicks);
  TEST_ASSERT_EQUAL(1, countNoteOns(afterUndo, 60));
  TEST_ASSERT_EQUAL(0, countNoteOns(afterUndo, 72));
}

void test_global_undo_note_edit_pass_closed_disables_edit_rows() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = 768;
  loop.passes.recordPass = makeRecordPassWithNote(1, 10);
  loop.nextPassId_ = 2;
  const EditPassId editId = loop.saveNoteEditPass(0, makePitchRow(1, 10, 20, 67));

  MidiEventVec withEdit;
  loop.passes.materializeToEventVector(withEdit, loop.loopLengthTicks);
  TEST_ASSERT_EQUAL(1, countNoteOns(withEdit, 67));

  loop.disableEditPasses(EditPassIdList{editId});

  MidiEventVec afterUndo;
  loop.passes.materializeToEventVector(afterUndo, loop.loopLengthTicks);
  TEST_ASSERT_EQUAL(1, countNoteOns(afterUndo, 60));
  TEST_ASSERT_EQUAL(0, countNoteOns(afterUndo, 67));
}

void test_global_undo_three_step_restores_record_baseline() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = 768;
  loop.passes.recordPass = makeRecordPassWithNote(1, 10);
  loop.nextPassId_ = 2;

  const EditPassId preOverdubEdit =
      loop.saveNoteEditPass(0, makePitchRow(1, 10, 20, 67));
  loop.passes.overdubPasses.push_back(makeOverdubPassWithNote(3, 100, 72));
  loop.nextPassId_ = 4;

  EditPass add{};
  add.passType = EditPassType::Note;
  add.actionType = EditActionType::Create;
  add.addedEvents.push_back(MidiEvent::NoteOn(200, 1, 48, 100));
  add.addedEvents.push_back(MidiEvent::NoteOff(210, 1, 48, 0));
  const EditPassId postOverdubEdit = loop.saveNoteEditPass(1, add);

  loop.disableEditPasses(EditPassIdList{postOverdubEdit});
  TEST_ASSERT_TRUE(loop.setCapturePassState(3, CapturePassState::Disabled));
  loop.disableEditPasses(EditPassIdList{preOverdubEdit});

  MidiEventVec baseline;
  loop.passes.materializeToEventVector(baseline, loop.loopLengthTicks);
  TEST_ASSERT_EQUAL(1, countNoteOns(baseline, 60));
  TEST_ASSERT_EQUAL(0, countNoteOns(baseline, 67));
  TEST_ASSERT_EQUAL(0, countNoteOns(baseline, 72));
  TEST_ASSERT_EQUAL(0, countNoteOns(baseline, 48));
}

void test_session_undo_move_back_insert_before_save_note_edit_pass() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 768;
  loop.passes.recordPass = makeEditRecordFixtureRecordPass(1);
  loop.nextPassId_ = 2;

  CowLoopEventStore session;
  loop.passes.materializeToEventVector(session.mutEvents(), loop.loopLengthTicks);
  session.syncEventsToStore();

  NoteEditFocus focus;
  focus.active = true;
  focus.commitBaseline = {60, 100, 8, 104};
  focus.last = focus.commitBaseline;
  focus.movingNoteId = 1;
  focus.movingNoteRange = {8, 104};

  const SessionUndoEntry entry =
      buildSessionUndoEntry(focus, EditorSelection{}, session.readEvents(), 1,
                            loop.loopLengthTicks, EditPassIdList{});

  focus.last.startTick = 496;
  focus.last.endTick = 1168;
  noteEditFocusApplyMoveEnd(focus, focus.last.startTick, focus.last.endTick);
  MidiEventVec& flat = session.mutEvents();
  for (MidiEvent& evt : flat) {
    if (evt.isNoteOn() && evt.channel == 1 && evt.data.noteData.note == 60 && evt.tick == 8) {
      evt.tick = 496;
    }
    if (evt.isNoteOff() && evt.channel == 1 && evt.data.noteData.note == 60 && evt.tick == 104) {
      evt.tick = 1168;
    }
  }
  session.syncEventsToStore();
  TEST_ASSERT_EQUAL(1, countMatching(session.readEvents(), true, 60, 496));

  SessionUndoEntry undoEntry = entry;
  SessionUndoEntry redoPayload =
      buildSessionUndoEntry(focus, EditorSelection{}, session.readEvents(), 1,
                            loop.loopLengthTicks, EditPassIdList{});
  undoEntry.redoEditRows = std::move(redoPayload.editRows);
  undoEntry.redoFocus = std::move(redoPayload.focus);
  undoEntry.redoSelection = redoPayload.selection;
  undoEntry.hasRedoPayload = true;

  applySessionUndoEntry(loop, session, undoEntry, loop.loopLengthTicks, 5, EditPassIdList{});
  TEST_ASSERT_EQUAL(1, countMatching(session.readEvents(), true, 60, 8));
  TEST_ASSERT_EQUAL(0, countMatching(session.readEvents(), true, 60, 496));

  applySessionRedoEntry(loop, session, undoEntry, loop.loopLengthTicks, 5, EditPassIdList{});
  TEST_ASSERT_EQUAL(1, countMatching(session.readEvents(), true, 60, 496));
}

// B2 — committed display base must match materialize(active edit passes). Geometry from
// session_20260816_161855 (home 888 → committed 1656). Do not treat "1656 visible" alone
// as the pin — compare the full reconstructed base.

constexpr uint32_t kB2LoopLength = 3072;
constexpr uint8_t kB2Pitch = 24;
constexpr uint8_t kB2Velocity = 100;
constexpr uint8_t kB2Channel = 5;
constexpr NoteId kB2MoverId = 280;
constexpr uint32_t kB2HomeStart = 888;
constexpr uint32_t kB2HomeEnd = 1032;
constexpr uint32_t kB2CommittedStart = 1656;
constexpr uint32_t kB2CommittedEnd = 1800;
constexpr uint32_t kB2UncommittedStart = 648;
constexpr uint32_t kB2UncommittedEnd = 792;

EditPassIdList activeEditPassIds(const Loop& loop) {
  EditPassIdList ids;
  for (const EditPass& editPass : loop.passes.editPasses) {
    if (editPass.state == EditPassState::Active) {
      ids.push_back(editPass.id);
    }
  }
  return ids;
}

NoteUtils::DisplayNoteVec committedBaseFromActivePasses(const Loop& loop) {
  MidiEventVec flat;
  loop.passes.materializeToEventVector(flat, loop.loopLengthTicks);
  return NoteUtils::reconstructDisplayNotes(flat, loop.loopLengthTicks, false, false);
}

const NoteUtils::DisplayNote* displayNoteById(const NoteUtils::DisplayNoteVec& notes, NoteId noteId) {
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.noteId == noteId) {
      return &note;
    }
  }
  return nullptr;
}

void assertDisplayNoteSpan(const NoteUtils::DisplayNoteVec& notes, NoteId noteId, uint8_t pitch,
                           uint32_t startTick, uint32_t endTick, const char* message) {
  const NoteUtils::DisplayNote* note = displayNoteById(notes, noteId);
  TEST_ASSERT_NOT_NULL_MESSAGE(note, message);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(pitch, note->note, message);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(startTick, note->startTick, message);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(endTick, note->endTick, message);
}

void assertCommittedBaseEqualsMaterializeActive(const Loop& loop, const char* message) {
  const NoteUtils::DisplayNoteVec committedBase = committedBaseFromActivePasses(loop);
  MidiEventVec flat;
  loop.passes.materializeToEventVector(flat, loop.loopLengthTicks);
  const NoteUtils::DisplayNoteVec fromFlat =
      NoteUtils::reconstructDisplayNotes(flat, loop.loopLengthTicks, false, false);
  TEST_ASSERT_EQUAL_MESSAGE(fromFlat.size(), committedBase.size(), message);
  for (const NoteUtils::DisplayNote& expected : fromFlat) {
    if (expected.noteId == kInvalidNoteId) {
      continue;
    }
    assertDisplayNoteSpan(committedBase, expected.noteId, expected.note, expected.startTick,
                          expected.endTick, message);
  }
}

void seedB2HomeLoop(Loop& loop) {
  loop.loopLengthTicks = kB2LoopLength;
  loop.passes.recordPass =
      makeRecordPassWithIdentifiedNote(1, kB2HomeStart, kB2HomeEnd, kB2Pitch, kB2MoverId, kB2Channel);
}

// commitEditAction appends saveNoteEditPass ids (Active). replaceNoteEditPass disables stale
// ids and returns new Active ids. sessionUndo assigns editPassIds = editPassIdsAtPush after
// disable — the session list does not retain Disabled ids. loop.passes still holds those rows.
// B2a must filter EditPassState::Active (and must not treat "ever created" as the gate).
void test_b2_edit_pass_ids_mean_active_committed_not_ever_created() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedB2HomeLoop(loop);
  EditPassIdList sessionEditPassIds;

  const EditPassId firstId =
      loop.saveNoteEditPass(0, makeNoteRangeRow(kB2MoverId, kB2HomeStart, kB2HomeEnd,
                                                kB2CommittedStart, kB2CommittedEnd));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, firstId);
  sessionEditPassIds.push_back(firstId);
  TEST_ASSERT_EQUAL(1u, sessionEditPassIds.size());
  TEST_ASSERT_EQUAL(1u, activeEditPassIds(loop).size());
  TEST_ASSERT_EQUAL(firstId, activeEditPassIds(loop)[0]);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPassState::Active),
                          static_cast<uint8_t>(loop.passes.editPasses[0].state));

  EditPassVec replacementRows;
  replacementRows.push_back(makeNoteRangeRow(kB2MoverId, kB2HomeStart, kB2HomeEnd,
                                             kB2CommittedStart, kB2CommittedEnd));
  const EditPassIdList replacementIds =
      loop.replaceNoteEditPass(0, sessionEditPassIds, std::move(replacementRows));
  TEST_ASSERT_EQUAL(1u, replacementIds.size());
  TEST_ASSERT_NOT_EQUAL(firstId, replacementIds[0]);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPassState::Disabled),
                          static_cast<uint8_t>(loop.passes.editPasses[0].state));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPassState::Active),
                          static_cast<uint8_t>(loop.passes.editPasses[1].state));
  sessionEditPassIds = replacementIds;
  TEST_ASSERT_EQUAL(1u, sessionEditPassIds.size());
  TEST_ASSERT_EQUAL(replacementIds[0], sessionEditPassIds[0]);
  TEST_ASSERT_EQUAL(1u, activeEditPassIds(loop).size());
  TEST_ASSERT_EQUAL(2u, loop.passes.editPasses.size());

  const EditPassIdList editPassIdsAtPush;
  loop.disableEditPasses(sessionEditPassIds);
  sessionEditPassIds = editPassIdsAtPush;
  TEST_ASSERT_TRUE(sessionEditPassIds.empty());
  TEST_ASSERT_TRUE(activeEditPassIds(loop).empty());
  TEST_ASSERT_EQUAL(2u, loop.passes.editPasses.size());
  TEST_ASSERT_FALSE(loop.passes.editPasses.empty());
}

void test_b2_committed_base_matches_materialize_active_edit_passes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedB2HomeLoop(loop);
  EditPassIdList sessionEditPassIds;

  // 1. Open — no committed edit pass. Oracle is takes-only (home).
  assertCommittedBaseEqualsMaterializeActive(loop, "1 open");
  assertDisplayNoteSpan(committedBaseFromActivePasses(loop), kB2MoverId, kB2Pitch, kB2HomeStart,
                        kB2HomeEnd, "1 open home");
  TEST_ASSERT_TRUE(sessionEditPassIds.empty());
  TEST_ASSERT_TRUE(activeEditPassIds(loop).empty());

  // 2. Commit NoteRange — session list holds the Active id. Oracle has the move.
  const EditPassId committedId =
      loop.saveNoteEditPass(0, makeNoteRangeRow(kB2MoverId, kB2HomeStart, kB2HomeEnd,
                                                kB2CommittedStart, kB2CommittedEnd));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, committedId);
  sessionEditPassIds.push_back(committedId);
  TEST_ASSERT_EQUAL(committedId, activeEditPassIds(loop)[0]);
  assertCommittedBaseEqualsMaterializeActive(loop, "2 commit");
  assertDisplayNoteSpan(committedBaseFromActivePasses(loop), kB2MoverId, kB2Pitch,
                        kB2CommittedStart, kB2CommittedEnd, "2 commit moved");

  // 3. Deselect — focus cleared. Materialized base still has the moved span (not home).
  NoteEditFocus deselectedFocus;
  NoteEditCurrentState committedState;
  committedState.upsertRow(kB2MoverId,
                           {kB2Pitch, kB2Velocity, kB2CommittedStart, kB2CommittedEnd},
                           {kB2Pitch, kB2Velocity, kB2CommittedStart, kB2CommittedEnd},
                           NoteEditPresenceType::Visible);
  MidiEventVec committedStore;
  committedState.projectToSessionStore(committedStore, kB2Channel);
  const NoteUtils::DisplayNoteVec committedBase = committedBaseFromActivePasses(loop);
  const NoteUtils::DisplayNoteVec deselectedPaint =
      projectNoteEditDisplayNotes(committedBase, committedStore, deselectedFocus, kB2Channel,
                                  kB2LoopLength, &committedState);
  assertDisplayNoteSpan(deselectedPaint, kB2MoverId, kB2Pitch, kB2CommittedStart, kB2CommittedEnd,
                        "3 deselect");

  // Stale visualCache that still carries the mover NoteId overlays currentState (1656).
  NoteUtils::DisplayNoteVec staleVisualCacheWithId;
  staleVisualCacheWithId.push_back(
      {kB2MoverId, kB2Pitch, kB2Velocity, kB2HomeStart, kB2HomeEnd});
  const NoteUtils::DisplayNoteVec staleIdPaint =
      projectNoteEditDisplayNotes(staleVisualCacheWithId, committedStore, deselectedFocus,
                                  kB2Channel, kB2LoopLength, &committedState);
  assertDisplayNoteSpan(staleIdPaint, kB2MoverId, kB2Pitch, kB2CommittedStart, kB2CommittedEnd,
                        "3 stale base with id overlays currentState");

  // visualCache rows can lack NoteId (161117). Cleared focus then keeps the home row —
  // currentState 1656 is not bound. B2a materialized base carries the id and the move.
  NoteUtils::DisplayNoteVec staleVisualCacheNoId;
  staleVisualCacheNoId.push_back(
      {kInvalidNoteId, kB2Pitch, kB2Velocity, kB2HomeStart, kB2HomeEnd});
  const NoteUtils::DisplayNoteVec staleNoIdPaint =
      projectNoteEditDisplayNotes(staleVisualCacheNoId, committedStore, deselectedFocus,
                                  kB2Channel, kB2LoopLength, &committedState);
  TEST_ASSERT_EQUAL(1u, staleNoIdPaint.size());
  TEST_ASSERT_EQUAL(kInvalidNoteId, staleNoIdPaint[0].noteId);
  TEST_ASSERT_EQUAL_UINT32(kB2HomeStart, staleNoIdPaint[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(kB2HomeEnd, staleNoIdPaint[0].endTick);

  // 4. Uncommitted move — session overlay wins over committed base.
  NoteEditFocus liveFocus;
  liveFocus.active = true;
  liveFocus.movingNoteId = kB2MoverId;
  liveFocus.commitBaseline = {kB2Pitch, kB2Velocity, kB2CommittedStart, kB2CommittedEnd};
  liveFocus.last = {kB2Pitch, kB2Velocity, kB2UncommittedStart, kB2UncommittedEnd};
  NoteEditCurrentState liveState;
  liveState.upsertRow(kB2MoverId, liveFocus.commitBaseline, liveFocus.last,
                      NoteEditPresenceType::Visible);
  MidiEventVec liveStore;
  liveState.projectToSessionStore(liveStore, kB2Channel);
  const NoteUtils::DisplayNoteVec livePaint =
      projectNoteEditDisplayNotes(committedBase, liveStore, liveFocus, kB2Channel, kB2LoopLength,
                                  &liveState);
  assertDisplayNoteSpan(livePaint, kB2MoverId, kB2Pitch, kB2UncommittedStart, kB2UncommittedEnd,
                        "4 uncommitted overlay");
  assertDisplayNoteSpan(committedBase, kB2MoverId, kB2Pitch, kB2CommittedStart, kB2CommittedEnd,
                        "4 committed base unchanged");

  // 5. Deselect again — committed geometry still the moved span.
  const NoteUtils::DisplayNoteVec deselectedAgain =
      projectNoteEditDisplayNotes(committedBase, committedStore, deselectedFocus, kB2Channel,
                                  kB2LoopLength, &committedState);
  assertDisplayNoteSpan(deselectedAgain, kB2MoverId, kB2Pitch, kB2CommittedStart, kB2CommittedEnd,
                        "5 deselect again");

  // 6. Exit bake / replace — in-session committed view and rematerialize stay the same span.
  EditPassVec exitRows;
  exitRows.push_back(makeNoteRangeRow(kB2MoverId, kB2HomeStart, kB2HomeEnd, kB2CommittedStart,
                                      kB2CommittedEnd));
  const EditPassIdList exitIds =
      loop.replaceNoteEditPass(0, sessionEditPassIds, std::move(exitRows));
  TEST_ASSERT_EQUAL(1u, exitIds.size());
  sessionEditPassIds = exitIds;
  assertCommittedBaseEqualsMaterializeActive(loop, "6 exit");
  assertDisplayNoteSpan(committedBaseFromActivePasses(loop), kB2MoverId, kB2Pitch,
                        kB2CommittedStart, kB2CommittedEnd, "6 exit moved");

  // 7. Disable committed pass (E:/U: undo). Active filter empty; oracle returns home.
  loop.disableEditPasses(sessionEditPassIds);
  sessionEditPassIds.clear();
  TEST_ASSERT_TRUE(activeEditPassIds(loop).empty());
  TEST_ASSERT_FALSE(loop.passes.editPasses.empty());
  assertCommittedBaseEqualsMaterializeActive(loop, "7 undo disable");
  assertDisplayNoteSpan(committedBaseFromActivePasses(loop), kB2MoverId, kB2Pitch, kB2HomeStart,
                        kB2HomeEnd, "7 undo home");
}

// Reselect calls ensureVisibleRowsForDisplayNotes(visualCache). When spans are already
// sealed equal, a stale home cache overwrites the synced move. B2a projection owner does
// not change that helper — pin the overwrite so the device reselect gate is not misread.
void test_b2_stale_visual_cache_overwrites_synced_current_state_spans() {
  NoteEditCurrentState state;
  state.upsertRow(kB2MoverId, {kB2Pitch, kB2Velocity, kB2CommittedStart, kB2CommittedEnd},
                  {kB2Pitch, kB2Velocity, kB2CommittedStart, kB2CommittedEnd},
                  NoteEditPresenceType::Visible);
  NoteUtils::DisplayNoteVec staleVisualCache;
  staleVisualCache.push_back({kB2MoverId, kB2Pitch, kB2Velocity, kB2HomeStart, kB2HomeEnd});
  state.ensureVisibleRowsForDisplayNotes(staleVisualCache);
  NoteBaseline span{};
  TEST_ASSERT_TRUE(state.readCurrentSpan(kB2MoverId, span));
  TEST_ASSERT_EQUAL_UINT32(kB2HomeStart, span.startTick);
  TEST_ASSERT_EQUAL_UINT32(kB2HomeEnd, span.endTick);
}

// session_20260816_173806 exit: session/display id 999, rematerialize 280 at 24@888.
// Commit-boundary reconcile retargets mover Update rows; overlap Delete stays.
void test_173806_reconcile_unique_match_retargets_mover_not_overlap_delete() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  constexpr uint32_t kLoopLength = 3072;
  constexpr NoteId kPersistId = 280;
  constexpr NoteId kSessionId = 999;
  constexpr NoteId kOverlapId = 526;
  constexpr uint8_t kHomePitch = 24;
  constexpr uint32_t kHomeStart = 888;
  constexpr uint32_t kHomeEnd = 1032;

  LoopPasses passes;
  passes.recordPass =
      makeRecordPassWithIdentifiedNote(1, kHomeStart, kHomeEnd, kHomePitch, kPersistId);

  MidiEventVec rematerialize;
  passes.materializeToEventVector(rematerialize, kLoopLength);
  TEST_ASSERT_EQUAL(kPersistId, noteIdAtPitchAndStart(rematerialize, kHomePitch, kHomeStart));
  TEST_ASSERT_EQUAL(-1, findNoteOnById(rematerialize, kSessionId));

  EditPassVec rows;
  rows.push_back(makeDeleteRow(kOverlapId));
  rows.push_back(makeNoteRangeRow(kSessionId, kHomeStart, kHomeEnd, 696, 888));
  rows.push_back(makePitchRow(kSessionId, 696, 888, 60));

  const PersistIdentityReconcileResult result = reconcileMoverPersistIdentity(
      rematerialize, kSessionId, kHomePitch, kHomeStart, rows);
  TEST_ASSERT_EQUAL(static_cast<int>(PersistIdentityReconcileStatus::Unique),
                    static_cast<int>(result.status));
  TEST_ASSERT_EQUAL(kPersistId, result.persistNoteId);
  TEST_ASSERT_EQUAL(kOverlapId, rows[0].targetNoteId);
  TEST_ASSERT_EQUAL(kPersistId, rows[1].targetNoteId);
  TEST_ASSERT_EQUAL(kPersistId, rows[2].targetNoteId);

  pushEditPassRows(passes, 1, rows);
  MidiEventVec flat;
  passes.materializeToEventVector(flat, kLoopLength);
  const std::vector<NoteUtils::DisplayNote> notes =
      NoteUtils::reconstructNotes(flat, kLoopLength, false);
  bool foundMoved = false;
  for (const NoteUtils::DisplayNote& note : notes) {
    TEST_ASSERT_FALSE(note.note == kHomePitch && note.startTick == kHomeStart);
    if (note.noteId == kPersistId) {
      foundMoved = true;
      TEST_ASSERT_EQUAL_UINT8(60, note.note);
      TEST_ASSERT_EQUAL_UINT32(696u, note.startTick);
      TEST_ASSERT_EQUAL_UINT32(888u, note.endTick);
    }
  }
  TEST_ASSERT_TRUE(foundMoved);
}

void test_reconcile_added_note_does_not_retarget() {
  MidiEventVec rematerialize;
  EditPassVec rows;
  rows.push_back(makeNoteRangeRow(999, 0, 192, 48, 240));
  const PersistIdentityReconcileResult result =
      reconcileMoverPersistIdentity(rematerialize, 999, 60, 0, rows);
  TEST_ASSERT_EQUAL(static_cast<int>(PersistIdentityReconcileStatus::Unresolved),
                    static_cast<int>(result.status));
  TEST_ASSERT_EQUAL_UINT32(0, result.matchCount);
  TEST_ASSERT_EQUAL(999, rows[0].targetNoteId);
}

void test_reconcile_zero_match_at_commit_baseline_does_not_retarget() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopPasses passes;
  passes.recordPass = makeRecordPassWithIdentifiedNote(1, 1000, 1144, 24, 280);
  MidiEventVec rematerialize;
  passes.materializeToEventVector(rematerialize, 3072);
  EditPassVec rows;
  rows.push_back(makeNoteRangeRow(999, 888, 1032, 696, 888));
  const PersistIdentityReconcileResult result =
      reconcileMoverPersistIdentity(rematerialize, 999, 24, 888, rows);
  TEST_ASSERT_EQUAL(static_cast<int>(PersistIdentityReconcileStatus::Unresolved),
                    static_cast<int>(result.status));
  TEST_ASSERT_EQUAL(999, rows[0].targetNoteId);
}

void test_reconcile_ambiguous_geometry_does_not_retarget() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;
  appendFixtureNotePair(store, 888, 1032, 5, 24, 280);
  appendFixtureNotePair(store, 888, 1032, 5, 24, 281);
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, committedChunkIds));
  LoopPasses passes;
  passes.recordPass.id = 1;
  passes.recordPass.state = CapturePassState::Active;
  passes.recordPass.committedChunkIds = std::move(committedChunkIds);

  MidiEventVec rematerialize;
  passes.materializeToEventVector(rematerialize, 3072);
  const PersistIdentityResolve resolved =
      resolvePersistIdentityForExistingNote(rematerialize, 24, 888);
  TEST_ASSERT_EQUAL(static_cast<int>(PersistIdentityResolveStatus::Ambiguous),
                    static_cast<int>(resolved.status));
  TEST_ASSERT_EQUAL_UINT32(2, resolved.matchCount);

  EditPassVec rows;
  rows.push_back(makeNoteRangeRow(999, 888, 1032, 696, 888));
  const PersistIdentityReconcileResult result =
      reconcileMoverPersistIdentity(rematerialize, 999, 24, 888, rows);
  TEST_ASSERT_EQUAL(static_cast<int>(PersistIdentityReconcileStatus::Ambiguous),
                    static_cast<int>(result.status));
  TEST_ASSERT_EQUAL(999, rows[0].targetNoteId);
}

void test_reconcile_already_present_session_id_does_not_retarget() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopPasses passes;
  passes.recordPass = makeRecordPassWithIdentifiedNote(1, 888, 1032, 24, 280);
  MidiEventVec rematerialize;
  passes.materializeToEventVector(rematerialize, 3072);
  EditPassVec rows;
  rows.push_back(makeNoteRangeRow(280, 888, 1032, 696, 888));
  const PersistIdentityReconcileResult result =
      reconcileMoverPersistIdentity(rematerialize, 280, 24, 888, rows);
  TEST_ASSERT_EQUAL(static_cast<int>(PersistIdentityReconcileStatus::AlreadyPresent),
                    static_cast<int>(result.status));
  TEST_ASSERT_EQUAL(280, rows[0].targetNoteId);
}

void test_sequence_phasea_collapse_contiguous_velocity_and_pitch_equivalence() {
  constexpr uint32_t kLoopLength = 3072;
  MidiEventVec baseline = makeSequenceParityFixtureEvents();
  MidiEventVec optimized = baseline;

  EditPassVec rows;
  rows.push_back(makeVelocityRow(10, 96, 192, 70));
  rows.push_back(makeVelocityRow(10, 96, 192, 80));
  rows.push_back(makeVelocityRow(10, 96, 192, 96));  // collapses into this row
  rows.push_back(makePitchRow(20, 256, 352, 67));    // barrier
  rows.push_back(makeVelocityRow(10, 96, 192, 64));  // must stay after barrier

  applyNoteEditPassSequenceBaselineNoCollapse(baseline, rows, kLoopLength);
  applyNoteEditPassSequence(optimized, rows, kLoopLength);

  assertMidiEventVecEquivalent(baseline, optimized,
                               "contiguous velocity/pitch collapse equivalence");
}

void test_sequence_phasea_barrier_with_length_keeps_equivalence() {
  constexpr uint32_t kLoopLength = 3072;
  MidiEventVec baseline = makeSequenceParityFixtureEvents();
  MidiEventVec optimized = baseline;

  EditPassVec rows;
  rows.push_back(makeVelocityRow(10, 96, 192, 72));
  rows.push_back(makeLengthRow(10, 96, 192, 288));   // barrier with overlap side effects
  rows.push_back(makeVelocityRow(10, 96, 288, 73));
  rows.push_back(makeVelocityRow(10, 96, 288, 74));  // collapses into this row only

  applyNoteEditPassSequenceBaselineNoCollapse(baseline, rows, kLoopLength);
  applyNoteEditPassSequence(optimized, rows, kLoopLength);

  assertMidiEventVecEquivalent(baseline, optimized, "length barrier equivalence");
}

void test_sequence_phasea_randomized_equivalence_against_baseline() {
  constexpr uint32_t kLoopLength = 3072;
  std::mt19937 rng(0xC0FFEEu);
  std::uniform_int_distribution<int> rowCountDist(10, 40);
  std::uniform_int_distribution<int> actionDist(0, 99);  // create/delete/update mix
  std::uniform_int_distribution<int> pitchDist(36, 84);
  std::uniform_int_distribution<int> velocityDist(1, 127);
  std::uniform_int_distribution<int> tickDist(0, static_cast<int>(kLoopLength - 32));
  std::uniform_int_distribution<int> lenDist(8, 192);
  std::uniform_int_distribution<int> propertyDist(0, 3);  // noteRange/length/pitch/velocity

  for (uint32_t scenario = 0; scenario < 128; ++scenario) {
    MidiEventVec baseline = makeSequenceParityFixtureEvents();
    MidiEventVec optimized = baseline;
    EditPassVec rows;
    rows.reserve(48);

    NoteId nextNoteId = 100;
    const int rowCount = rowCountDist(rng);
    for (int i = 0; i < rowCount; ++i) {
      const int actionPick = actionDist(rng);
      const NoteId targetNoteId =
          static_cast<NoteId>(1 + static_cast<int>(nextNoteId) + (actionDist(rng) % 8) - 4);
      if (actionPick < 20) {  // create
        const uint32_t startTick = static_cast<uint32_t>(tickDist(rng));
        const uint32_t endTick = std::min<uint32_t>(
            startTick + static_cast<uint32_t>(lenDist(rng)), kLoopLength - 1);
        rows.push_back(
            makeCreateRow(nextNoteId, startTick, endTick, 1, static_cast<uint8_t>(pitchDist(rng)),
                          static_cast<uint8_t>(velocityDist(rng))));
        ++nextNoteId;
      } else if (actionPick < 32) {  // delete
        rows.push_back(makeDeleteRow(targetNoteId));
      } else {  // update
        switch (propertyDist(rng)) {
          case 0: {
            const uint32_t startTick = static_cast<uint32_t>(tickDist(rng));
            const uint32_t endTick = std::min<uint32_t>(
                startTick + static_cast<uint32_t>(lenDist(rng)), kLoopLength - 1);
            rows.push_back(makeNoteRangeRow(targetNoteId, 0, 0, startTick, endTick));
            break;
          }
          case 1: {
            const uint32_t newEndTick = static_cast<uint32_t>(tickDist(rng));
            rows.push_back(makeLengthRow(targetNoteId, 0, 0, newEndTick));
            break;
          }
          case 2: {
            rows.push_back(makePitchRow(targetNoteId, 0, 0, static_cast<uint8_t>(pitchDist(rng))));
            break;
          }
          default: {
            rows.push_back(
                makeVelocityRow(targetNoteId, 0, 0, static_cast<uint8_t>(velocityDist(rng))));
            break;
          }
        }
      }
    }

    applyNoteEditPassSequenceBaselineNoCollapse(baseline, rows, kLoopLength);
    applyNoteEditPassSequence(optimized, rows, kLoopLength);

    char context[96];
    snprintf(context, sizeof(context), "randomized scenario %u", static_cast<unsigned>(scenario));
    assertMidiEventVecEquivalent(baseline, optimized, context);
  }
}

void test_sequence_phaseb_length_then_same_note_update_equivalence() {
  constexpr uint32_t kLoopLength = 3072;
  MidiEventVec baseline = makeSequenceParityFixtureEvents();
  MidiEventVec optimized = baseline;

  EditPassVec rows;
  rows.push_back(makeLengthRow(10, 96, 192, 288));    // structural mutation barrier
  rows.push_back(makePitchRow(10, 96, 288, 72));      // same note after length mutation
  rows.push_back(makeVelocityRow(10, 96, 288, 111));  // same note continued

  applyNoteEditPassSequenceBaselineNoCollapse(baseline, rows, kLoopLength);
  applyNoteEditPassSequence(optimized, rows, kLoopLength);

  assertMidiEventVecEquivalent(baseline, optimized,
                               "phaseb length then same-note updates equivalence");
}

void test_sequence_phaseb_length_with_interleaved_note_updates_equivalence() {
  constexpr uint32_t kLoopLength = 3072;
  MidiEventVec baseline = makeSequenceParityFixtureEvents();
  MidiEventVec optimized = baseline;

  EditPassVec rows;
  rows.push_back(makeLengthRow(10, 96, 192, 304));    // structural mutation barrier
  rows.push_back(makeVelocityRow(20, 256, 352, 109)); // another note
  rows.push_back(makePitchRow(10, 96, 304, 69));      // original note again
  rows.push_back(makeVelocityRow(10, 96, 304, 112));  // contiguous same target/property collapse

  applyNoteEditPassSequenceBaselineNoCollapse(baseline, rows, kLoopLength);
  applyNoteEditPassSequence(optimized, rows, kLoopLength);

  assertMidiEventVecEquivalent(baseline, optimized,
                               "phaseb length interleaved updates equivalence");
}

void test_incremental_companion_projection_operation_matrix() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  const MidiEventVec initialEvents = makeCompanionProjectionInitialEvents();

  const auto assertHistory = [&](const char* context,
                                 std::initializer_list<EditPass> rows) {
    EditPassVec history;
    history.reserve(rows.size());
    for (const EditPass& row : rows) {
      history.push_back(row);
    }
    assignCompanionHistoryIds(history);
    assertCompanionHistoryIncrementalEquivalence(
        initialEvents, history, kCompanionProjectionLoopLength, context);
  };

  assertHistory("single-hide", {makeCompanionDeleteRow(kCompanionTargetNoteId)});
  assertHistory("repeated-hide",
                {makeCompanionDeleteRow(kCompanionTargetNoteId),
                 makeCompanionDeleteRow(kCompanionTargetNoteId)});
  assertHistory("repeated-shorten-decreasing",
                {makeCompanionLengthRow(kCompanionTargetNoteId, 300),
                 makeCompanionLengthRow(kCompanionTargetNoteId, 240),
                 makeCompanionLengthRow(kCompanionTargetNoteId, 200)});
  assertHistory("shorten-increase-decrease",
                {makeCompanionLengthRow(kCompanionTargetNoteId, 240),
                 makeCompanionLengthRow(kCompanionTargetNoteId, 600),
                 makeCompanionLengthRow(kCompanionTargetNoteId, 220)});
  assertHistory("hide-after-shorten",
                {makeCompanionLengthRow(kCompanionTargetNoteId, 240),
                 makeCompanionDeleteRow(kCompanionTargetNoteId)});
  assertHistory("shorten-after-hide",
                {makeCompanionDeleteRow(kCompanionTargetNoteId),
                 makeCompanionLengthRow(kCompanionTargetNoteId, 240)});
  assertHistory("wrap-companion",
                {makeCompanionLengthRow(kCompanionWrapNoteId, 64),
                 makeCompanionLengthRow(kCompanionWrapNoteId, 128),
                 makeCompanionDeleteRow(kCompanionWrapNoteId)});
  assertHistory("multiple-overdub-generations",
                {makeCompanionLengthRow(kCompanionTargetNoteId, 240),
                 makeCompanionDeleteRow(kCompanionOverlapNoteId),
                 makeCompanionLengthRow(kCompanionDistantOverlapNoteId, 300),
                 makeCompanionDeleteRow(kCompanionDifferentPitchNoteId),
                 makeCompanionLengthRow(kCompanionWrapNoteId, 64),
                 makeCompanionLengthRow(kCompanionTargetNoteId, 600)});

  {
    MidiEventVec before = initialEvents;
    MidiEventVec after = before;
    applyNoteEditPass(after, makeCompanionLengthRow(kCompanionTargetNoteId, 600),
                      kCompanionProjectionLoopLength);
    printCompanionAffectedGeometry("lengthen", kCompanionTargetNoteId, before, after);
    TEST_ASSERT_FALSE(companionNoteStateEquals(companionNoteState(before, kCompanionTargetNoteId),
                                               companionNoteState(after, kCompanionTargetNoteId)));
    TEST_ASSERT_FALSE(companionNoteStateEquals(companionNoteState(before, kCompanionOverlapNoteId),
                                               companionNoteState(after, kCompanionOverlapNoteId)));
    TEST_ASSERT_FALSE(companionNoteStateEquals(
        companionNoteState(before, kCompanionDistantOverlapNoteId),
        companionNoteState(after, kCompanionDistantOverlapNoteId)));
    TEST_ASSERT_TRUE(companionNoteStateEquals(
        companionNoteState(before, kCompanionDifferentPitchNoteId),
        companionNoteState(after, kCompanionDifferentPitchNoteId)));
  }

  {
    MidiEventVec before = initialEvents;
    MidiEventVec after = before;
    applyNoteEditPass(after, makeCompanionLengthRow(kCompanionTargetNoteId, 240),
                      kCompanionProjectionLoopLength);
    printCompanionAffectedGeometry("shorten", kCompanionTargetNoteId, before, after);
    TEST_ASSERT_FALSE(companionNoteStateEquals(companionNoteState(before, kCompanionTargetNoteId),
                                               companionNoteState(after, kCompanionTargetNoteId)));
    TEST_ASSERT_TRUE(companionNoteStateEquals(
        companionNoteState(before, kCompanionOverlapNoteId),
        companionNoteState(after, kCompanionOverlapNoteId)));
    TEST_ASSERT_TRUE(companionNoteStateEquals(
        companionNoteState(before, kCompanionDistantOverlapNoteId),
        companionNoteState(after, kCompanionDistantOverlapNoteId)));
  }

  {
    MidiEventVec before = initialEvents;
    MidiEventVec after = before;
    applyNoteEditPass(after, makeCompanionDeleteRow(kCompanionTargetNoteId),
                      kCompanionProjectionLoopLength);
    printCompanionAffectedGeometry("hide", kCompanionTargetNoteId, before, after);
    TEST_ASSERT_FALSE(companionNoteStateEquals(companionNoteState(before, kCompanionTargetNoteId),
                                               companionNoteState(after, kCompanionTargetNoteId)));
    TEST_ASSERT_TRUE(companionNoteStateEquals(
        companionNoteState(before, kCompanionOverlapNoteId),
        companionNoteState(after, kCompanionOverlapNoteId)));
    TEST_ASSERT_TRUE(companionNoteStateEquals(
        companionNoteState(before, kCompanionDifferentPitchNoteId),
        companionNoteState(after, kCompanionDifferentPitchNoteId)));
  }

  {
    MidiEventVec before = initialEvents;
    applyNoteEditPass(before, makeCompanionLengthRow(kCompanionTargetNoteId, 240),
                      kCompanionProjectionLoopLength);
    MidiEventVec after = before;
    applyNoteEditPass(after, makeCompanionDeleteRow(kCompanionTargetNoteId),
                      kCompanionProjectionLoopLength);
    printCompanionAffectedGeometry("hide-after-shorten", kCompanionTargetNoteId, before, after);
  }

  {
    MidiEventVec before = initialEvents;
    applyNoteEditPass(before, makeCompanionDeleteRow(kCompanionTargetNoteId),
                      kCompanionProjectionLoopLength);
    MidiEventVec after = before;
    applyNoteEditPass(after, makeCompanionLengthRow(kCompanionTargetNoteId, 240),
                      kCompanionProjectionLoopLength);
    printCompanionAffectedGeometry("shorten-after-hide", kCompanionTargetNoteId, before, after);
    assertMidiEventVecEquivalent(before, after, "shorten after hide is a no-op");
  }
}

void test_incremental_companion_projection_randomized_history_and_undo_redo() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  const MidiEventVec initialEvents = makeCompanionProjectionInitialEvents();
  std::mt19937 rng(0xC0A11u);

  for (uint32_t scenario = 0; scenario < 128; ++scenario) {
    const EditPassVec history = makeCompanionRandomizedHistoryScenario(
        rng, static_cast<EditPassId>(2000 + scenario * 64));
    char context[96];
    snprintf(context, sizeof(context), "randomized companion history %u",
             static_cast<unsigned>(scenario));
    assertCompanionHistoryIncrementalEquivalence(
        initialEvents, history, kCompanionProjectionLoopLength, context);
  }
}

void test_incremental_companion_projection_reduced_state_minimal_geometry_diverges() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  const MidiEventVec initialEvents = makeCompanionProjectionInitialEvents();

  uint32_t mismatchCount = 0;
  uint32_t firstMismatchGeneration = UINT32_MAX;
  const auto observeHistory = [&](const char* context,
                                  std::initializer_list<EditPass> rows) {
    EditPassVec history;
    history.reserve(rows.size());
    for (const EditPass& row : rows) {
      history.push_back(row);
    }
    assignCompanionHistoryIds(history, 9000);
    uint32_t mismatchGeneration = UINT32_MAX;
    const bool equivalent = reducedCompanionHistoryEquivalent(
        initialEvents, history, kCompanionProjectionLoopLength, &mismatchGeneration);
    if (!equivalent) {
      ++mismatchCount;
      if (firstMismatchGeneration == UINT32_MAX) {
        firstMismatchGeneration = mismatchGeneration;
      }
      (void)context;
    }
  };

  observeHistory("reduced-single-hide", {makeCompanionDeleteRow(kCompanionTargetNoteId)});
  observeHistory("reduced-repeated-hide",
                 {makeCompanionDeleteRow(kCompanionTargetNoteId),
                  makeCompanionDeleteRow(kCompanionTargetNoteId)});
  observeHistory("reduced-repeated-shorten-decreasing",
                 {makeCompanionLengthRow(kCompanionTargetNoteId, 300),
                  makeCompanionLengthRow(kCompanionTargetNoteId, 240),
                  makeCompanionLengthRow(kCompanionTargetNoteId, 200)});
  observeHistory("reduced-shorten-increase-decrease",
                 {makeCompanionLengthRow(kCompanionTargetNoteId, 240),
                  makeCompanionLengthRow(kCompanionTargetNoteId, 600),
                  makeCompanionLengthRow(kCompanionTargetNoteId, 220)});
  observeHistory("reduced-hide-after-shorten",
                 {makeCompanionLengthRow(kCompanionTargetNoteId, 240),
                  makeCompanionDeleteRow(kCompanionTargetNoteId)});
  observeHistory("reduced-shorten-after-hide",
                 {makeCompanionDeleteRow(kCompanionTargetNoteId),
                  makeCompanionLengthRow(kCompanionTargetNoteId, 240)});
  observeHistory("reduced-wrap-companion",
                 {makeCompanionLengthRow(kCompanionWrapNoteId, 64),
                  makeCompanionLengthRow(kCompanionWrapNoteId, 128),
                  makeCompanionDeleteRow(kCompanionWrapNoteId)});
  observeHistory("reduced-multiple-generations",
                 {makeCompanionLengthRow(kCompanionTargetNoteId, 240),
                  makeCompanionDeleteRow(kCompanionOverlapNoteId),
                  makeCompanionLengthRow(kCompanionDistantOverlapNoteId, 300),
                  makeCompanionDeleteRow(kCompanionDifferentPitchNoteId),
                  makeCompanionLengthRow(kCompanionWrapNoteId, 64),
                  makeCompanionLengthRow(kCompanionTargetNoteId, 600)});

  TEST_ASSERT_TRUE(mismatchCount > 0u);
  TEST_ASSERT_EQUAL_UINT32(1u, firstMismatchGeneration);

  {
    ReducedCompanionState before = ReducedCompanionState::fromEvents(
        initialEvents, kCompanionProjectionLoopLength);
    ReducedCompanionState after = before;
    after.applyCompanionDelta(makeCompanionLengthRow(kCompanionTargetNoteId, 600),
                              kCompanionProjectionLoopLength);
    assertChangedCompanionNoteIds(collectChangedCompanionNoteIds(before, after),
                                  {kCompanionTargetNoteId, kCompanionOverlapNoteId,
                                   kCompanionDistantOverlapNoteId, kCompanionWrapNoteId},
                                  "reduced-lengthen-affected-region");
  }

  {
    ReducedCompanionState before = ReducedCompanionState::fromEvents(
        initialEvents, kCompanionProjectionLoopLength);
    ReducedCompanionState after = before;
    after.applyCompanionDelta(makeCompanionLengthRow(kCompanionTargetNoteId, 240),
                              kCompanionProjectionLoopLength);
    assertChangedCompanionNoteIds(collectChangedCompanionNoteIds(before, after),
                                  {kCompanionTargetNoteId, kCompanionWrapNoteId},
                                  "reduced-shorten-affected-region");
  }

  {
    ReducedCompanionState before = ReducedCompanionState::fromEvents(
        initialEvents, kCompanionProjectionLoopLength);
    ReducedCompanionState after = before;
    after.applyCompanionDelta(makeCompanionDeleteRow(kCompanionTargetNoteId),
                              kCompanionProjectionLoopLength);
    assertChangedCompanionNoteIds(collectChangedCompanionNoteIds(before, after),
                                  {kCompanionTargetNoteId, kCompanionWrapNoteId},
                                  "reduced-hide-affected-region");
  }

  {
    ReducedCompanionState before = ReducedCompanionState::fromEvents(
        initialEvents, kCompanionProjectionLoopLength);
    ReducedCompanionState after = before;
    after.applyCompanionDelta(makeCompanionLengthRow(kCompanionWrapNoteId, 192),
                              kCompanionProjectionLoopLength);
    assertChangedCompanionNoteIds(collectChangedCompanionNoteIds(before, after), {},
                                  "reduced-wrap-affected-region");
  }

  {
    ReducedCompanionState before = ReducedCompanionState::fromEvents(
        initialEvents, kCompanionProjectionLoopLength);
    before.applyCompanionDelta(makeCompanionDeleteRow(kCompanionTargetNoteId),
                               kCompanionProjectionLoopLength);
    ReducedCompanionState after = before;
    after.applyCompanionDelta(makeCompanionLengthRow(kCompanionTargetNoteId, 240),
                              kCompanionProjectionLoopLength);
    assertChangedCompanionNoteIds(collectChangedCompanionNoteIds(before, after), {},
                                  "reduced-shorten-after-hide-no-op");
  }
}

void test_incremental_companion_projection_reduced_state_randomized_divergence() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  const MidiEventVec initialEvents = makeCompanionProjectionInitialEvents();
  std::mt19937 rng(0x5EEDC0DEu);
  uint32_t mismatchScenarios = 0;
  uint32_t firstMismatchGeneration = UINT32_MAX;

  for (uint32_t scenario = 0; scenario < 128; ++scenario) {
    const EditPassVec history = makeCompanionRandomizedHistoryScenario(
        rng, static_cast<EditPassId>(12000 + scenario * 64));
    uint32_t mismatchGeneration = UINT32_MAX;
    if (!reducedCompanionHistoryEquivalent(initialEvents, history,
                                           kCompanionProjectionLoopLength,
                                           &mismatchGeneration)) {
      ++mismatchScenarios;
      if (firstMismatchGeneration == UINT32_MAX) {
        firstMismatchGeneration = mismatchGeneration;
      }
    }
  }

  TEST_ASSERT_TRUE(mismatchScenarios > 0u);
  TEST_ASSERT_EQUAL_UINT32(1u, firstMismatchGeneration);
}

void test_incremental_companion_projection_paired_state_operation_matrix() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  const MidiEventVec initialEvents = makeCompanionProjectionInitialEvents();

  const auto assertHistory = [&](const char* context,
                                 std::initializer_list<EditPass> rows) {
    EditPassVec history;
    history.reserve(rows.size());
    for (const EditPass& row : rows) {
      history.push_back(row);
    }
    assignCompanionHistoryIds(history, 15000);
    uint32_t mismatchGeneration = UINT32_MAX;
    const bool equivalent = pairedCompanionHistoryEquivalent(
        initialEvents, history, kCompanionProjectionLoopLength, &mismatchGeneration);
    if (!equivalent) {
      char message[160];
      snprintf(message, sizeof(message), "%s mismatch generation=%u", context,
               static_cast<unsigned>(mismatchGeneration));
      TEST_FAIL_MESSAGE(message);
    }
  };

  assertHistory("paired-single-hide", {makeCompanionDeleteRow(kCompanionTargetNoteId)});
  assertHistory("paired-repeated-hide",
                {makeCompanionDeleteRow(kCompanionTargetNoteId),
                 makeCompanionDeleteRow(kCompanionTargetNoteId)});
  assertHistory("paired-repeated-shorten-decreasing",
                {makeCompanionLengthRow(kCompanionTargetNoteId, 300),
                 makeCompanionLengthRow(kCompanionTargetNoteId, 240),
                 makeCompanionLengthRow(kCompanionTargetNoteId, 200)});
  assertHistory("paired-shorten-increase-decrease",
                {makeCompanionLengthRow(kCompanionTargetNoteId, 240),
                 makeCompanionLengthRow(kCompanionTargetNoteId, 600),
                 makeCompanionLengthRow(kCompanionTargetNoteId, 220)});
  assertHistory("paired-hide-after-shorten",
                {makeCompanionLengthRow(kCompanionTargetNoteId, 240),
                 makeCompanionDeleteRow(kCompanionTargetNoteId)});
  assertHistory("paired-shorten-after-hide",
                {makeCompanionDeleteRow(kCompanionTargetNoteId),
                 makeCompanionLengthRow(kCompanionTargetNoteId, 240)});
  assertHistory("paired-wrap-companion",
                {makeCompanionLengthRow(kCompanionWrapNoteId, 64),
                 makeCompanionLengthRow(kCompanionWrapNoteId, 128),
                 makeCompanionDeleteRow(kCompanionWrapNoteId)});
  assertHistory("paired-multiple-generations",
                {makeCompanionLengthRow(kCompanionTargetNoteId, 240),
                 makeCompanionDeleteRow(kCompanionOverlapNoteId),
                 makeCompanionLengthRow(kCompanionDistantOverlapNoteId, 300),
                 makeCompanionDeleteRow(kCompanionDifferentPitchNoteId),
                 makeCompanionLengthRow(kCompanionWrapNoteId, 64),
                 makeCompanionLengthRow(kCompanionTargetNoteId, 600)});
}

void test_incremental_companion_projection_paired_state_randomized_history_and_undo_redo() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  const MidiEventVec initialEvents = makeCompanionProjectionInitialEvents();
  std::mt19937 rng(0xA11CEu);

  for (uint32_t scenario = 0; scenario < 128; ++scenario) {
    const EditPassVec history = makeCompanionRandomizedHistoryScenario(
        rng, static_cast<EditPassId>(17000 + scenario * 64));
    uint32_t mismatchGeneration = UINT32_MAX;
    const bool equivalent = pairedCompanionHistoryEquivalent(
        initialEvents, history, kCompanionProjectionLoopLength, &mismatchGeneration);
    if (!equivalent) {
      char message[176];
      snprintf(message, sizeof(message),
               "paired randomized companion history %u mismatch generation=%u",
               static_cast<unsigned>(scenario),
               static_cast<unsigned>(mismatchGeneration));
      TEST_FAIL_MESSAGE(message);
    }
  }
}

void test_incremental_companion_projection_paired_state_scenario51_generation13_equivalent() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  const MidiEventVec initialEvents = makeCompanionProjectionInitialEvents();
  std::mt19937 rng(0xA11CEu);

  EditPassVec scenarioHistory;
  for (uint32_t scenario = 0; scenario <= 51; ++scenario) {
    scenarioHistory = makeCompanionRandomizedHistoryScenario(
        rng, static_cast<EditPassId>(17000 + scenario * 64));
  }

  TEST_ASSERT_TRUE(scenarioHistory.size() >= 13u);

  const EditPassVec prefix12 = companionHistoryPrefix(scenarioHistory, 12);
  uint32_t mismatchGeneration = UINT32_MAX;
  TEST_ASSERT_TRUE(pairedCompanionHistoryEquivalent(initialEvents, prefix12,
                                                    kCompanionProjectionLoopLength,
                                                    &mismatchGeneration));

  const EditPassVec prefix13 = companionHistoryPrefix(scenarioHistory, 13);
  mismatchGeneration = UINT32_MAX;
  TEST_ASSERT_TRUE(pairedCompanionHistoryEquivalent(initialEvents, prefix13,
                                                    kCompanionProjectionLoopLength,
                                                    &mismatchGeneration));
}

void test_incremental_companion_projection_span_only_randomized_divergence_boundary() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  const MidiEventVec initialEvents = makeCompanionProjectionInitialEvents();
  std::mt19937 rng(0xA11CEu);

  uint32_t mismatchScenarios = 0;
  uint32_t firstMismatchGeneration = UINT32_MAX;
  EditPassVec firstMismatchHistory;
  for (uint32_t scenario = 0; scenario < 128; ++scenario) {
    const EditPassVec history = makeCompanionRandomizedHistoryScenario(
        rng, static_cast<EditPassId>(19000 + scenario * 64));
    uint32_t mismatchGeneration = UINT32_MAX;
    if (!spanOnlyCompanionHistoryEquivalent(initialEvents, history,
                                            kCompanionProjectionLoopLength,
                                            &mismatchGeneration)) {
      ++mismatchScenarios;
      if (firstMismatchGeneration == UINT32_MAX) {
        firstMismatchGeneration = mismatchGeneration;
        firstMismatchHistory = history;
      }
    }
  }

  TEST_ASSERT_TRUE(mismatchScenarios > 0u);
  TEST_ASSERT_TRUE(firstMismatchGeneration > 0u);
  TEST_ASSERT_TRUE(!firstMismatchHistory.empty());

  uint32_t pairedMismatchGeneration = UINT32_MAX;
  TEST_ASSERT_TRUE(pairedCompanionHistoryEquivalent(initialEvents, firstMismatchHistory,
                                                    kCompanionProjectionLoopLength,
                                                    &pairedMismatchGeneration));

  const EditPassVec prefixBefore =
      companionHistoryPrefix(firstMismatchHistory, firstMismatchGeneration - 1u);
  uint32_t spanOnlyMismatch = UINT32_MAX;
  TEST_ASSERT_TRUE(spanOnlyCompanionHistoryEquivalent(initialEvents, prefixBefore,
                                                      kCompanionProjectionLoopLength,
                                                      &spanOnlyMismatch));

  const EditPassVec prefixAt =
      companionHistoryPrefix(firstMismatchHistory, firstMismatchGeneration);
  spanOnlyMismatch = UINT32_MAX;
  TEST_ASSERT_FALSE(spanOnlyCompanionHistoryEquivalent(initialEvents, prefixAt,
                                                       kCompanionProjectionLoopLength,
                                                       &spanOnlyMismatch));
  TEST_ASSERT_EQUAL_UINT32(firstMismatchGeneration, spanOnlyMismatch);
}

void test_incremental_companion_projection_context_ablation_channel_velocity_requirements() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  const MidiEventVec initialEvents = makeCompanionProjectionMixedChannelVelocityInitialEvents();

  EditPassVec history = {
      makeCompanionLengthRow(kCompanionMixedTargetNoteId, 600),
      makeCompanionDeleteRow(kCompanionMixedOverlapNoteId),
      makeCompanionLengthRow(kCompanionMixedWrapNoteId, 64),
      makeCompanionLengthRow(kCompanionMixedTargetNoteId, 220),
      makeCompanionDeleteRow(kCompanionMixedDifferentPitchNoteId),
  };
  assignCompanionHistoryIds(history, 21000);

  uint32_t mismatchGeneration = UINT32_MAX;
  TEST_ASSERT_TRUE(pairedCompanionHistoryEquivalent(initialEvents, history,
                                                    kCompanionProjectionLoopLength,
                                                    &mismatchGeneration));

  mismatchGeneration = UINT32_MAX;
  TEST_ASSERT_FALSE(contextAblatedCompanionHistoryEquivalent(initialEvents, history,
                                                             kCompanionProjectionLoopLength,
                                                             false, true,
                                                             &mismatchGeneration));
  TEST_ASSERT_EQUAL_UINT32(0u, mismatchGeneration);

  mismatchGeneration = UINT32_MAX;
  TEST_ASSERT_FALSE(contextAblatedCompanionHistoryEquivalent(initialEvents, history,
                                                             kCompanionProjectionLoopLength,
                                                             true, false,
                                                             &mismatchGeneration));
  TEST_ASSERT_EQUAL_UINT32(0u, mismatchGeneration);

  mismatchGeneration = UINT32_MAX;
  TEST_ASSERT_FALSE(contextAblatedCompanionHistoryEquivalent(initialEvents, history,
                                                             kCompanionProjectionLoopLength,
                                                             false, false,
                                                             &mismatchGeneration));
  TEST_ASSERT_EQUAL_UINT32(0u, mismatchGeneration);
}

void test_incremental_companion_projection_pitch_ablation_requirement() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  const MidiEventVec initialEvents = makeCompanionProjectionMixedChannelVelocityInitialEvents();

  EditPassVec history = {
      makeCompanionLengthRow(kCompanionMixedTargetNoteId, 600),
      makeCompanionDeleteRow(kCompanionMixedOverlapNoteId),
      makeCompanionLengthRow(kCompanionMixedWrapNoteId, 64),
      makeCompanionLengthRow(kCompanionMixedTargetNoteId, 220),
      makeCompanionDeleteRow(kCompanionMixedDifferentPitchNoteId),
  };
  assignCompanionHistoryIds(history, 22000);

  uint32_t mismatchGeneration = UINT32_MAX;
  TEST_ASSERT_TRUE(pairedCompanionHistoryEquivalent(initialEvents, history,
                                                    kCompanionProjectionLoopLength,
                                                    &mismatchGeneration));

  mismatchGeneration = UINT32_MAX;
  TEST_ASSERT_FALSE(pitchAblatedCompanionHistoryEquivalent(initialEvents, history,
                                                           kCompanionProjectionLoopLength,
                                                           false,
                                                           &mismatchGeneration));
  TEST_ASSERT_EQUAL_UINT32(0u, mismatchGeneration);
}

void test_incremental_companion_projection_start_tick_ablation_requirement() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  const MidiEventVec initialEvents = makeCompanionProjectionMixedChannelVelocityInitialEvents();

  EditPassVec history = {
      makeCompanionLengthRow(kCompanionMixedTargetNoteId, 600),
      makeCompanionDeleteRow(kCompanionMixedOverlapNoteId),
      makeCompanionLengthRow(kCompanionMixedWrapNoteId, 64),
      makeCompanionLengthRow(kCompanionMixedTargetNoteId, 220),
      makeCompanionDeleteRow(kCompanionMixedDifferentPitchNoteId),
  };
  assignCompanionHistoryIds(history, 23000);

  uint32_t mismatchGeneration = UINT32_MAX;
  TEST_ASSERT_TRUE(pairedCompanionHistoryEquivalent(initialEvents, history,
                                                    kCompanionProjectionLoopLength,
                                                    &mismatchGeneration));

  mismatchGeneration = UINT32_MAX;
  TEST_ASSERT_FALSE(tickAblatedCompanionHistoryEquivalent(initialEvents, history,
                                                          kCompanionProjectionLoopLength,
                                                          false, true,
                                                          &mismatchGeneration));
  TEST_ASSERT_EQUAL_UINT32(0u, mismatchGeneration);
}

void test_incremental_companion_projection_end_tick_ablation_requirement() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  const MidiEventVec initialEvents = makeCompanionProjectionMixedChannelVelocityInitialEvents();

  EditPassVec history = {
      makeCompanionLengthRow(kCompanionMixedTargetNoteId, 600),
      makeCompanionDeleteRow(kCompanionMixedOverlapNoteId),
      makeCompanionLengthRow(kCompanionMixedWrapNoteId, 64),
      makeCompanionLengthRow(kCompanionMixedTargetNoteId, 220),
      makeCompanionDeleteRow(kCompanionMixedDifferentPitchNoteId),
  };
  assignCompanionHistoryIds(history, 24000);

  uint32_t mismatchGeneration = UINT32_MAX;
  TEST_ASSERT_TRUE(pairedCompanionHistoryEquivalent(initialEvents, history,
                                                    kCompanionProjectionLoopLength,
                                                    &mismatchGeneration));

  mismatchGeneration = UINT32_MAX;
  TEST_ASSERT_FALSE(tickAblatedCompanionHistoryEquivalent(initialEvents, history,
                                                          kCompanionProjectionLoopLength,
                                                          true, false,
                                                          &mismatchGeneration));
  TEST_ASSERT_EQUAL_UINT32(0u, mismatchGeneration);
}

void test_incremental_companion_projection_tick_ordered_materialize_divergence_boundary() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  const MidiEventVec initialEvents = makeCompanionProjectionInitialEvents();
  std::mt19937 rng(0xA11CEu);

  uint32_t mismatchCountOn = 0;
  uint32_t mismatchCountOff = 0;
  uint32_t firstMismatchGeneration = UINT32_MAX;
  bool firstMismatchOffBeforeOn = false;
  EditPassVec firstHistory;
  for (uint32_t scenario = 0; scenario < 128; ++scenario) {
    const EditPassVec history = makeCompanionRandomizedHistoryScenario(
        rng, static_cast<EditPassId>(23000 + scenario * 64));

    uint32_t localOn = UINT32_MAX;
    const bool onEquivalent = tickOrderedCompanionHistoryEquivalent(
        initialEvents, history, kCompanionProjectionLoopLength, false, &localOn);
    uint32_t localOff = UINT32_MAX;
    const bool offEquivalent = tickOrderedCompanionHistoryEquivalent(
        initialEvents, history, kCompanionProjectionLoopLength, true, &localOff);

    if (!onEquivalent) {
      ++mismatchCountOn;
      if (firstHistory.empty()) {
        firstHistory = history;
        firstMismatchGeneration = localOn;
        firstMismatchOffBeforeOn = false;
      }
    }
    if (!offEquivalent) {
      ++mismatchCountOff;
      if (firstHistory.empty()) {
        firstHistory = history;
        firstMismatchGeneration = localOff;
        firstMismatchOffBeforeOn = true;
      }
    }
  }

  TEST_ASSERT_TRUE(!firstHistory.empty());
  TEST_ASSERT_TRUE(mismatchCountOn > 0u || mismatchCountOff > 0u);
  TEST_ASSERT_TRUE(firstMismatchGeneration != UINT32_MAX);
  uint32_t pairedMismatch = UINT32_MAX;
  TEST_ASSERT_TRUE(pairedCompanionHistoryEquivalent(initialEvents, firstHistory,
                                                    kCompanionProjectionLoopLength,
                                                    &pairedMismatch));

  if (firstMismatchGeneration > 0u) {
    const EditPassVec prefixBefore = companionHistoryPrefix(firstHistory, firstMismatchGeneration - 1u);
    uint32_t mismatchGeneration = UINT32_MAX;
    TEST_ASSERT_TRUE(tickOrderedCompanionHistoryEquivalent(initialEvents, prefixBefore,
                                                           kCompanionProjectionLoopLength,
                                                           firstMismatchOffBeforeOn,
                                                           &mismatchGeneration));
  }

  const EditPassVec prefixAt = companionHistoryPrefix(firstHistory, firstMismatchGeneration);
  uint32_t mismatchGeneration = UINT32_MAX;
  TEST_ASSERT_FALSE(tickOrderedCompanionHistoryEquivalent(initialEvents, prefixAt,
                                                          kCompanionProjectionLoopLength,
                                                          firstMismatchOffBeforeOn,
                                                          &mismatchGeneration));
  TEST_ASSERT_EQUAL_UINT32(firstMismatchGeneration, mismatchGeneration);
}

void test_incremental_companion_projection_note_id_ablation_divergence_boundary() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  const MidiEventVec initialEvents = makeCompanionProjectionInitialEvents();
  std::mt19937 rng(0xA11CEu);

  uint32_t firstMismatchGeneration = UINT32_MAX;
  EditPassVec firstMismatchHistory;
  for (uint32_t scenario = 0; scenario < 128; ++scenario) {
    const EditPassVec history = makeCompanionRandomizedHistoryScenario(
        rng, static_cast<EditPassId>(25000 + scenario * 64));
    uint32_t mismatchGeneration = UINT32_MAX;
    if (!noteIdAblatedCompanionHistoryEquivalent(initialEvents, history,
                                                 kCompanionProjectionLoopLength,
                                                 &mismatchGeneration)) {
      firstMismatchGeneration = mismatchGeneration;
      firstMismatchHistory = history;
      break;
    }
  }

  TEST_ASSERT_TRUE(!firstMismatchHistory.empty());
  TEST_ASSERT_TRUE(firstMismatchGeneration > 0u);

  uint32_t pairedMismatchGeneration = UINT32_MAX;
  TEST_ASSERT_TRUE(pairedCompanionHistoryEquivalent(initialEvents, firstMismatchHistory,
                                                    kCompanionProjectionLoopLength,
                                                    &pairedMismatchGeneration));

  const EditPassVec prefixBefore =
      companionHistoryPrefix(firstMismatchHistory, firstMismatchGeneration - 1u);
  uint32_t mismatchGeneration = UINT32_MAX;
  TEST_ASSERT_TRUE(noteIdAblatedCompanionHistoryEquivalent(initialEvents, prefixBefore,
                                                           kCompanionProjectionLoopLength,
                                                           &mismatchGeneration));

  const EditPassVec prefixAt = companionHistoryPrefix(firstMismatchHistory, firstMismatchGeneration);
  mismatchGeneration = UINT32_MAX;
  TEST_ASSERT_FALSE(noteIdAblatedCompanionHistoryEquivalent(initialEvents, prefixAt,
                                                            kCompanionProjectionLoopLength,
                                                            &mismatchGeneration));
  TEST_ASSERT_EQUAL_UINT32(firstMismatchGeneration, mismatchGeneration);
}

void test_incremental_companion_projection_derived_key_order_divergence_boundary() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  const MidiEventVec initialEvents = makeCompanionProjectionInitialEvents();
  std::mt19937 rng(0xA11CEu);

  uint32_t firstMismatchGeneration = UINT32_MAX;
  EditPassVec firstMismatchHistory;
  for (uint32_t scenario = 0; scenario < 128; ++scenario) {
    const EditPassVec history = makeCompanionRandomizedHistoryScenario(
        rng, static_cast<EditPassId>(27000 + scenario * 64));
    uint32_t mismatchGeneration = UINT32_MAX;
    if (!derivedKeyOrderedCompanionHistoryEquivalent(initialEvents, history,
                                                     kCompanionProjectionLoopLength,
                                                     &mismatchGeneration)) {
      firstMismatchGeneration = mismatchGeneration;
      firstMismatchHistory = history;
      break;
    }
  }

  TEST_ASSERT_TRUE(!firstMismatchHistory.empty());
  TEST_ASSERT_TRUE(firstMismatchGeneration > 0u);

  uint32_t pairedMismatchGeneration = UINT32_MAX;
  TEST_ASSERT_TRUE(pairedCompanionHistoryEquivalent(initialEvents, firstMismatchHistory,
                                                    kCompanionProjectionLoopLength,
                                                    &pairedMismatchGeneration));

  const EditPassVec prefixBefore =
      companionHistoryPrefix(firstMismatchHistory, firstMismatchGeneration - 1u);
  uint32_t mismatchGeneration = UINT32_MAX;
  TEST_ASSERT_TRUE(derivedKeyOrderedCompanionHistoryEquivalent(initialEvents, prefixBefore,
                                                               kCompanionProjectionLoopLength,
                                                               &mismatchGeneration));

  const EditPassVec prefixAt = companionHistoryPrefix(firstMismatchHistory, firstMismatchGeneration);
  mismatchGeneration = UINT32_MAX;
  TEST_ASSERT_FALSE(derivedKeyOrderedCompanionHistoryEquivalent(initialEvents, prefixAt,
                                                                kCompanionProjectionLoopLength,
                                                                &mismatchGeneration));
  TEST_ASSERT_EQUAL_UINT32(firstMismatchGeneration, mismatchGeneration);
}

void test_incremental_companion_projection_split_order_divergence_boundary() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  const MidiEventVec initialEvents = makeCompanionProjectionInitialEvents();
  std::mt19937 rng(0xA11CEu);

  uint32_t firstMismatchGeneration = UINT32_MAX;
  EditPassVec firstMismatchHistory;
  for (uint32_t scenario = 0; scenario < 128; ++scenario) {
    const EditPassVec history = makeCompanionRandomizedHistoryScenario(
        rng, static_cast<EditPassId>(29000 + scenario * 64));
    uint32_t mismatchGeneration = UINT32_MAX;
    if (!splitOrderCompanionHistoryEquivalent(initialEvents, history,
                                              kCompanionProjectionLoopLength,
                                              &mismatchGeneration)) {
      firstMismatchGeneration = mismatchGeneration;
      firstMismatchHistory = history;
      break;
    }
  }

  TEST_ASSERT_TRUE(!firstMismatchHistory.empty());
  TEST_ASSERT_TRUE(firstMismatchGeneration > 0u);

  uint32_t pairedMismatchGeneration = UINT32_MAX;
  TEST_ASSERT_TRUE(pairedCompanionHistoryEquivalent(initialEvents, firstMismatchHistory,
                                                    kCompanionProjectionLoopLength,
                                                    &pairedMismatchGeneration));

  const EditPassVec prefixBefore =
      companionHistoryPrefix(firstMismatchHistory, firstMismatchGeneration - 1u);
  uint32_t mismatchGeneration = UINT32_MAX;
  TEST_ASSERT_TRUE(splitOrderCompanionHistoryEquivalent(initialEvents, prefixBefore,
                                                        kCompanionProjectionLoopLength,
                                                        &mismatchGeneration));

  const EditPassVec prefixAt = companionHistoryPrefix(firstMismatchHistory, firstMismatchGeneration);
  mismatchGeneration = UINT32_MAX;
  TEST_ASSERT_FALSE(splitOrderCompanionHistoryEquivalent(initialEvents, prefixAt,
                                                         kCompanionProjectionLoopLength,
                                                         &mismatchGeneration));
  TEST_ASSERT_EQUAL_UINT32(firstMismatchGeneration, mismatchGeneration);
}

void test_incremental_companion_projection_same_tick_order_divergence_boundary() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  const MidiEventVec initialEvents = makeCompanionProjectionInitialEvents();
  std::mt19937 rng(0xA11CEu);

  uint32_t firstMismatchGeneration = UINT32_MAX;
  EditPassVec firstMismatchHistory;
  for (uint32_t scenario = 0; scenario < 128; ++scenario) {
    const EditPassVec history = makeCompanionRandomizedHistoryScenario(
        rng, static_cast<EditPassId>(31000 + scenario * 64));
    uint32_t mismatchGeneration = UINT32_MAX;
    if (!sameTickOrderCompanionHistoryEquivalent(initialEvents, history,
                                                 kCompanionProjectionLoopLength,
                                                 &mismatchGeneration)) {
      firstMismatchGeneration = mismatchGeneration;
      firstMismatchHistory = history;
      break;
    }
  }

  TEST_ASSERT_TRUE(!firstMismatchHistory.empty());
  TEST_ASSERT_TRUE(firstMismatchGeneration > 0u);

  uint32_t pairedMismatchGeneration = UINT32_MAX;
  TEST_ASSERT_TRUE(pairedCompanionHistoryEquivalent(initialEvents, firstMismatchHistory,
                                                    kCompanionProjectionLoopLength,
                                                    &pairedMismatchGeneration));

  const EditPassVec prefixBefore =
      companionHistoryPrefix(firstMismatchHistory, firstMismatchGeneration - 1u);
  uint32_t mismatchGeneration = UINT32_MAX;
  TEST_ASSERT_TRUE(sameTickOrderCompanionHistoryEquivalent(initialEvents, prefixBefore,
                                                           kCompanionProjectionLoopLength,
                                                           &mismatchGeneration));

  const EditPassVec prefixAt = companionHistoryPrefix(firstMismatchHistory, firstMismatchGeneration);
  mismatchGeneration = UINT32_MAX;
  TEST_ASSERT_FALSE(sameTickOrderCompanionHistoryEquivalent(initialEvents, prefixAt,
                                                            kCompanionProjectionLoopLength,
                                                            &mismatchGeneration));
  TEST_ASSERT_EQUAL_UINT32(firstMismatchGeneration, mismatchGeneration);
}

static int boundedCompanionPair(const MidiEventVec& events, size_t target, size_t quantum) {
  RuntimeNotePairLookup lookup;
  TEST_ASSERT_TRUE(lookup.prewarm(2048));
  TEST_ASSERT_TRUE(lookup.begin(events.data(), events.size(), target));
  const size_t bound = (6 * events.size() + quantum - 1) / quantum + 1;
  size_t turns = 0;
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  while (result == RuntimePlaybackOrderBuildResult::Pending && turns++ < bound) result = lookup.step(quantum);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  TEST_ASSERT_TRUE(turns <= bound);
  return lookup.result();
}

void test_companion_lane_run_counterexample_pairs_index_3_at_tick_96() {
  MidiEventVec events{MidiEvent::NoteOn(96, 1, 60, 90), MidiEvent::NoteOn(0, 1, 60, 90),
                      MidiEvent::NoteOff(96, 1, 60), MidiEvent::NoteOff(96, 1, 60),
                      MidiEvent::NoteOff(0, 1, 60)};
  TEST_ASSERT_EQUAL_INT(3, findNoteOffForOnIndex(events, 0));
  for (size_t q : {1u, 2u, 32u}) TEST_ASSERT_EQUAL_INT(3, boundedCompanionPair(events, 0, q));
  TEST_ASSERT_EQUAL_UINT32(96, events[3].tick);
}

void test_companion_runs_are_filtered_by_lane_and_stable_within_kind() {
  MidiEventVec events{MidiEvent::NoteOn(0, 1, 60, 90), MidiEvent::NoteOn(96, 1, 60, 80),
                      MidiEvent::ControlChange(48, 1, 1, 9), MidiEvent::NoteOff(48, 2, 60),
                      MidiEvent::NoteOff(48, 1, 62), MidiEvent::NoteOff(96, 1, 60),
                      MidiEvent::NoteOff(192, 1, 60)};
  TEST_ASSERT_EQUAL_INT(5, findNoteOffForOnIndex(events, 0));
  TEST_ASSERT_EQUAL_INT(6, findNoteOffForOnIndex(events, 1));
  TEST_ASSERT_EQUAL_INT(5, boundedCompanionPair(events, 0, 1));
  TEST_ASSERT_EQUAL_INT(6, boundedCompanionPair(events, 1, 1));
  events = {MidiEvent::NoteOn(0, 1, 60, 90), MidiEvent::NoteOn(0, 1, 60, 80),
            MidiEvent::NoteOff(96, 1, 60), MidiEvent::NoteOff(96, 1, 60)};
  TEST_ASSERT_EQUAL_INT(3, findNoteOffForOnIndex(events, 0));
  TEST_ASSERT_EQUAL_INT(2, findNoteOffForOnIndex(events, 1));
  TEST_ASSERT_EQUAL_INT(3, boundedCompanionPair(events, 0, 1));
  TEST_ASSERT_EQUAL_INT(2, boundedCompanionPair(events, 1, 1));
}

void test_bounded_companion_pair_preserves_tagged_and_unique_wrap_fallback() {
  MidiEventVec events{MidiEvent::NoteOff(10, 1, 60), MidiEvent::NoteOff(20, 1, 60),
                      MidiEvent::NoteOn(700, 1, 60, 90)};
  events[1].noteId = events[2].noteId = 42;
  TEST_ASSERT_EQUAL_INT(1, findNoteOffForOnIndex(events, 2));
  TEST_ASSERT_EQUAL_INT(1, boundedCompanionPair(events, 2, 1));
  events[1].noteId = kInvalidNoteId;
  TEST_ASSERT_EQUAL_INT(-1, findNoteOffForOnIndex(events, 2));
  TEST_ASSERT_EQUAL_INT(-1, boundedCompanionPair(events, 2, 1));
  events.erase(events.begin());
  TEST_ASSERT_EQUAL_INT(0, findNoteOffForOnIndex(events, 1));
  TEST_ASSERT_EQUAL_INT(0, boundedCompanionPair(events, 1, 1));
}

void test_bounded_companion_pair_matches_revised_reference_across_generated_lanes() {
  std::mt19937 rng(20260913);
  for (size_t fixture = 0; fixture < 500; ++fixture) {
    MidiEventVec events;
    const size_t count = 1 + rng() % 64;
    for (size_t i = 0; i < count; ++i) {
      const uint32_t tick = (rng() % 8) * 96;
      const uint8_t channel = 1 + rng() % 2;
      const uint8_t pitch = 60 + rng() % 3;
      auto event = rng() % 2 ? MidiEvent::NoteOn(tick, channel, pitch, 90) : MidiEvent::NoteOff(tick, channel, pitch);
      event.noteId = rng() % 5; // Includes repeated identities and untagged wrap heads.
      events.push_back(event);
    }
    for (size_t target = 0; target < events.size(); ++target) {
      if (!events[target].isNoteOn()) continue;
      const int expected = findNoteOffForOnIndex(events, target);
      TEST_ASSERT_EQUAL_INT(expected, boundedCompanionPair(events, target, 1 + fixture % 7));
    }
  }
  MidiEventVec maximum;
  for (size_t i = 0; i < 2048; ++i) maximum.push_back(MidiEvent::NoteOn(i, 1, 60, 90));
  TEST_ASSERT_EQUAL_INT(-1, boundedCompanionPair(maximum, 0, 32));
}

void test_bounded_wrap_head_lookup_matches_reference() {
  RuntimeNotePairLookup lookup;
  TEST_ASSERT_TRUE(lookup.prewarm(2048));
  std::mt19937 rng(20260914);
  for (size_t fixture = 0; fixture < 500; ++fixture) {
    const uint32_t length = fixture % 2 ? 768 : 3072;
    MidiEventVec events;
    for (size_t i = 0, count = 1 + rng() % 64; i < count; ++i) {
      const uint32_t tick = rng() % (2 * length + 1);
      const uint8_t channel = 1 + rng() % 2, pitch = 60 + rng() % 2;
      auto event = rng() % 2 ? MidiEvent::NoteOn(tick, channel, pitch, 90)
                            : MidiEvent::NoteOff(tick, channel, pitch);
      event.noteId = rng() % 5;
      events.push_back(event);
    }
    for (size_t target = 0; target < events.size(); ++target) {
      if (!events[target].isNoteOn()) continue;
      const size_t quantum = 1 + fixture % 7;
      TEST_ASSERT_TRUE(lookup.beginWrapHead(events.data(), events.size(), target, length));
      auto result = RuntimePlaybackOrderBuildResult::Pending;
      size_t turns = 0, bound = (2 * events.size() + quantum - 1) / quantum;
      while (result == RuntimePlaybackOrderBuildResult::Pending && turns++ < bound)
        result = lookup.step(quantum);
      TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
      TEST_ASSERT_EQUAL_INT(findWrapHeadOffForOnIndex(events, target, length), lookup.result());
    }
  }
}

void test_bounded_delete_selection_matches_actual_delete_with_retained_rows() {
  RuntimeNotePairLookup pair;
  RuntimeNoteDeleteLookup lookup;
  TEST_ASSERT_TRUE(pair.prewarm(2048));
  std::mt19937 rng(20260915);
  for (size_t fixture = 0; fixture < 500; ++fixture) {
    MidiEventVec events, expected;
    std::vector<uint8_t> retained;
    for (size_t i = 0, count = 1 + rng() % 64; i < count; ++i) {
      const uint32_t tick = rng() % 6145;
      const uint8_t channel = 1 + rng() % 2, pitch = 60 + rng() % 2;
      auto event = rng() % 2 ? MidiEvent::NoteOn(tick, channel, pitch, 90)
                            : MidiEvent::NoteOff(tick, channel, pitch);
      event.noteId = rng() % 5;
      events.push_back(event);
      retained.push_back(rng() % 4 != 0);
      if (retained.back()) expected.push_back(event);
    }
    const NoteId target = rng() % 6;
    const uint32_t length = fixture % 3 ? 3072 : 0;
    applyDeleteNoteById(expected, target, length, nullptr);
    TEST_ASSERT_TRUE(lookup.begin(pair, events.data(), events.size(), target, length, retained.data()));
    const size_t quantum = 1 + fixture % 7;
    const size_t bound = (events.size() + quantum - 1) / quantum +
                         (6 * events.size() + quantum - 1) / quantum +
                         (2 * events.size() + quantum - 1) / quantum;
    size_t turns = 0;
    auto result = RuntimePlaybackOrderBuildResult::Pending;
    while (result == RuntimePlaybackOrderBuildResult::Pending && turns++ < bound) {
      TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, lookup.step(0));
      result = lookup.step(quantum);
    }
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    MidiEventVec actual;
    for (size_t i = 0; i < events.size(); ++i) {
      if (retained[i] && static_cast<int>(i) != lookup.onIndex() &&
          static_cast<int>(i) != lookup.offIndex() && static_cast<int>(i) != lookup.wrapOffIndex())
        actual.push_back(events[i]);
    }
    TEST_ASSERT_EQUAL_UINT(expected.size(), actual.size());
    for (size_t i = 0; i < expected.size(); ++i) {
      TEST_ASSERT_EQUAL_UINT32(expected[i].tick, actual[i].tick);
      TEST_ASSERT_EQUAL_UINT(expected[i].noteId, actual[i].noteId);
      TEST_ASSERT_EQUAL_UINT(expected[i].channel, actual[i].channel);
      TEST_ASSERT_EQUAL_UINT(expected[i].type, actual[i].type);
      TEST_ASSERT_EQUAL_UINT(expected[i].data.noteData.note, actual[i].data.noteData.note);
    }
  }
}

void test_bounded_delete_selects_both_tail_and_wrap_off_and_preserves_source() {
  MidiEventVec events{MidiEvent::NoteOn(2900, 1, 60, 90), MidiEvent::NoteOff(3072, 1, 60),
                      MidiEvent::NoteOff(100, 1, 60)};
  events[0].noteId = events[2].noteId = 42;
  RuntimeNotePairLookup pair;
  RuntimeNoteDeleteLookup lookup;
  TEST_ASSERT_TRUE(pair.prewarm(2048));
  TEST_ASSERT_TRUE(lookup.begin(pair, events.data(), events.size(), 42, 3072));
  TEST_ASSERT_FALSE(lookup.begin(pair, events.data(), events.size(), 42, 3072));
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  for (size_t turn = 0; turn < 27 && result == RuntimePlaybackOrderBuildResult::Pending; ++turn)
    result = lookup.step(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  TEST_ASSERT_EQUAL_INT(0, lookup.onIndex());
  TEST_ASSERT_EQUAL_INT(1, lookup.offIndex());
  TEST_ASSERT_EQUAL_INT(2, lookup.wrapOffIndex());
  TEST_ASSERT_EQUAL_UINT(3, events.size());
  TEST_ASSERT_EQUAL_UINT32(3072, events[1].tick);
  lookup.discard();
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, lookup.step(1));
  TEST_ASSERT_TRUE(pair.available());
}

void test_bounded_delete_capacity_bound_and_discard() {
  RuntimeNotePairLookup pair;
  RuntimeNoteDeleteLookup lookup;
  TEST_ASSERT_TRUE(pair.prewarm(2048));
  MidiEventVec events;
  for (size_t i = 0; i < 2048; ++i) events.push_back(MidiEvent::NoteOn(2900, 1, 60, 90));
  events[0].noteId = 42;
  for (size_t quantum : {1u, 3u, 32u}) {
    TEST_ASSERT_TRUE(lookup.begin(pair, events.data(), events.size(), 42, 3072));
    TEST_ASSERT_FALSE(lookup.ready());
    const size_t bound = (2048 + quantum - 1) / quantum +
                         (6 * 2048 + quantum - 1) / quantum +
                         (2 * 2048 + quantum - 1) / quantum;
    auto result = RuntimePlaybackOrderBuildResult::Pending;
    size_t turns = 0;
    while (result == RuntimePlaybackOrderBuildResult::Pending && turns++ < bound)
      result = lookup.step(quantum);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    TEST_ASSERT_TRUE(lookup.ready());
    TEST_ASSERT_EQUAL_INT(0, lookup.onIndex());
    TEST_ASSERT_EQUAL_INT(-1, lookup.offIndex());
    TEST_ASSERT_EQUAL_INT(-1, lookup.wrapOffIndex());
    lookup.discard();
  }
  TEST_ASSERT_TRUE(lookup.begin(pair, events.data(), events.size(), 42, 3072));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, lookup.step(2048));
  TEST_ASSERT_FALSE(pair.available());
  lookup.discard();
  TEST_ASSERT_TRUE(pair.available());
  TEST_ASSERT_FALSE(lookup.ready());
  TEST_ASSERT_TRUE(pair.begin(events.data(), events.size(), 0));
  TEST_ASSERT_FALSE(lookup.begin(pair, events.data(), events.size(), 42, 3072));
  pair.discard();
  events.push_back(MidiEvent::NoteOff(3000, 1, 60));
  TEST_ASSERT_FALSE(lookup.begin(pair, events.data(), events.size(), 42, 3072));
}

void test_companion_overlap_walk_holds_actions_and_preserves_legacy_action_order() {
  const NoteUtils::DisplayNote notes[] = {
      {2, 60, 90, 120, 160}, // Delete first in source, but after all shortenings.
      {3, 60, 90, 50, 150},
      {4, 60, 90, 125, 300}, // Extends beyond replacement: leave alone.
      {5, 60, 90, 125, 200}, // Same ending inner note: leave alone.
      {6, 61, 90, 50, 150},
      {1, 60, 90, 100, 110},
      {7, 60, 90, 60, 180},
      {8, 60, 90, 150, 170}};
  RuntimeCompanionOverlapWalk walk;
  TEST_ASSERT_TRUE(walk.begin(notes, 8, 1, 60, 100, 200, 768));
  TEST_ASSERT_FALSE(walk.begin(notes, 8, 1, 60, 100, 200, 768));
  const NoteId ids[] = {3, 7, 2, 8};
  size_t actions = 0;
  auto result = RuntimeCompanionOverlapResult::Pending;
  for (size_t turns = 0; turns < 25 && result != RuntimeCompanionOverlapResult::Complete; ++turns) {
    result = walk.step(1);
    if (result != RuntimeCompanionOverlapResult::ActionReady) continue;
    TEST_ASSERT_TRUE(actions < 4);
    TEST_ASSERT_EQUAL_UINT(ids[actions], walk.action().noteId);
    TEST_ASSERT_EQUAL(actions < 2 ? RuntimeCompanionOverlapWalk::ActionType::Shorten
                                 : RuntimeCompanionOverlapWalk::ActionType::Delete, walk.action().kind);
    if (actions < 2) TEST_ASSERT_EQUAL_UINT32(99, walk.action().endTick);
    const size_t index = walk.action().sourceIndex;
    TEST_ASSERT_EQUAL(RuntimeCompanionOverlapResult::Denied, walk.step(0));
    TEST_ASSERT_EQUAL(RuntimeCompanionOverlapResult::ActionReady, walk.step(32));
    TEST_ASSERT_EQUAL_UINT(0, walk.lastStepInputCount());
    TEST_ASSERT_EQUAL_UINT(index, walk.action().sourceIndex);
    TEST_ASSERT_TRUE(walk.consumeAction());
    TEST_ASSERT_FALSE(walk.consumeAction());
    ++actions;
  }
  TEST_ASSERT_EQUAL_UINT(4, actions);
  TEST_ASSERT_EQUAL(RuntimeCompanionOverlapResult::Complete, result);
  TEST_ASSERT_EQUAL_UINT32(150, notes[1].endTick);
  walk.discard();
  TEST_ASSERT_EQUAL(RuntimeCompanionOverlapResult::Denied, walk.step(1));
}

void test_companion_overlap_walk_matches_actual_length_application() {
  std::mt19937 rng(20260916);
  for (size_t fixture = 0; fixture < 500; ++fixture) {
    const uint32_t length = fixture % 2 ? 768 : 3072;
    MidiEventVec initial;
    for (size_t i = 0, count = 2 + rng() % 16; i < count; ++i) {
      const uint32_t start = rng() % length;
      auto on = MidiEvent::NoteOn(start, 1 + rng() % 2, 60 + rng() % 2, 80 + rng() % 40);
      on.noteId = i + 1;
      initial.push_back(on);
      auto off = MidiEvent::NoteOff(rng() % (length + 1), on.channel, on.data.noteData.note);
      off.noteId = on.noteId;
      initial.push_back(off);
    }
    const auto target = initial[0];
    const int offIndex = findNoteOffForOnIndex(initial, 0);
    const uint32_t refEnd = offIndex >= 0 ? initial[offIndex].tick : target.tick;
    const uint32_t newEnd = refEnd + 1 + rng() % length;
    auto expected = initial;
    applyChangeLengthById(expected, target.noteId, newEnd, length, nullptr);
    auto actual = initial;
    const auto notes = reconstructNotesForPitch(initial, target.data.noteData.note, length);
    RuntimeCompanionOverlapWalk walk;
    TEST_ASSERT_TRUE(walk.begin(notes.data(), notes.size(), target.noteId,
                                target.data.noteData.note, target.tick, newEnd, length));
    const size_t quantum = 1 + fixture % 7;
    const size_t bound = (2 * notes.size() + quantum - 1) / quantum + 2 * notes.size() + 1;
    auto result = RuntimeCompanionOverlapResult::Pending;
    size_t turns = 0;
    while (result != RuntimeCompanionOverlapResult::Complete && turns++ < bound) {
      result = walk.step(quantum);
      TEST_ASSERT_TRUE(walk.lastStepInputCount() <= quantum);
      if (result == RuntimeCompanionOverlapResult::ActionReady) {
        const auto action = walk.action();
        if (action.kind == RuntimeCompanionOverlapWalk::ActionType::Shorten)
          shortenNoteEndById(actual, action.noteId, action.endTick, nullptr);
        else applyDeleteNoteById(actual, action.noteId, length, nullptr);
        TEST_ASSERT_TRUE(walk.consumeAction());
      }
    }
    TEST_ASSERT_EQUAL(RuntimeCompanionOverlapResult::Complete, result);
    const int refreshedOn = findNoteOnByIdLinear(actual, target.noteId);
    const int refreshedOff = findNoteOffForOnIndex(actual, refreshedOn);
    if (refreshedOff >= 0) actual[refreshedOff].tick = newEnd;
    else if (refreshedOn >= 0) actual.push_back(MidiEvent::NoteOff(newEnd,
                                    actual[refreshedOn].channel, actual[refreshedOn].data.noteData.note));
    NoteUtils::orderSamePitchNoteOffsForLifo(actual, target.channel, target.data.noteData.note);
    std::stable_sort(actual.begin(), actual.end(),
                     [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
    assertMidiEventVecEquivalent(expected, actual, "bounded overlap action order vs Length resolver");
  }
}

void test_companion_overlap_walk_maximum_input_and_denied_admission() {
  std::vector<NoteUtils::DisplayNote> notes(4096, {2, 60, 90, 120, 160});
  RuntimeCompanionOverlapWalk walk;
  TEST_ASSERT_FALSE(walk.begin(notes.data(), 4097, 1, 60, 100, 200, 768));
  TEST_ASSERT_FALSE(walk.begin(notes.data(), notes.size(), 1, 60, 100, 200, 0));
  for (size_t quantum : {1u, 3u, 32u}) {
    TEST_ASSERT_TRUE(walk.begin(notes.data(), notes.size(), 1, 60, 100, 200, 768));
    size_t actions = 0, turns = 0;
    const size_t bound = (2 * notes.size() + quantum - 1) / quantum + notes.size() + 1;
    auto result = RuntimeCompanionOverlapResult::Pending;
    while (result != RuntimeCompanionOverlapResult::Complete && turns++ < bound) {
      result = walk.step(quantum);
      TEST_ASSERT_TRUE(walk.lastStepInputCount() <= quantum);
      if (result == RuntimeCompanionOverlapResult::ActionReady) {
        TEST_ASSERT_EQUAL_UINT(actions, walk.action().sourceIndex);
        ++actions;
        TEST_ASSERT_TRUE(walk.consumeAction());
      }
    }
    TEST_ASSERT_EQUAL(RuntimeCompanionOverlapResult::Complete, result);
    TEST_ASSERT_EQUAL_UINT(notes.size(), actions);
  }
}

void test_reconstruction_wrap_lookup_matches_legacy_queries() {
  RuntimeNotePairLookup lookup;
  TEST_ASSERT_TRUE(lookup.prewarm(2048));
  std::mt19937 rng(20260917);
  for (size_t fixture = 0; fixture < 300; ++fixture) {
    const uint32_t length = fixture % 2 ? 768 : 3072;
    MidiEventVec source, filtered;
    std::vector<uint8_t> retained;
    std::vector<size_t> original;
    for (size_t i = 0, count = 1 + rng() % 48; i < count; ++i) {
      const uint32_t tick = (rng() % (length / 96 + 3)) * 96;
      const uint8_t channel = 1 + rng() % 2, pitch = 60 + rng() % 2;
      auto event = rng() % 2 ? MidiEvent::NoteOn(tick, channel, pitch, 1 + rng() % 127)
                            : MidiEvent::NoteOff(tick, channel, pitch);
      event.noteId = rng() % 5;
      source.push_back(event);
      retained.push_back(rng() % 5 != 0);
      if (retained.back()) { filtered.push_back(event); original.push_back(i); }
    }
    for (size_t off = 0; off < filtered.size(); ++off) {
      if (!filtered[off].isNoteOff()) continue;
      const auto& event = filtered[off];
      const uint32_t head = event.tick % length;
      for (size_t candidate = 0; candidate <= filtered.size(); ++candidate) {
        const bool automatic = candidate == filtered.size();
        if (!automatic && (!filtered[candidate].isNoteOn() ||
            filtered[candidate].data.noteData.note != event.data.noteData.note)) continue;
        uint32_t tick = automatic ? 0 : filtered[candidate].tick;
        uint8_t velocity = 0;
        const bool expected = automatic
          ? tryPairWrappedTailOn(filtered, head, event.data.noteData.note, event.channel,
                                 length, tick, velocity)
          : isPreferredWrapTailForHeadOffInSpanBuild(tick, head, filtered,
                                 event.data.noteData.note, event.channel, length);
        TEST_ASSERT_TRUE(lookup.beginReconstructionWrap(source.data(), source.size(),
                 original[off], length, automatic ? SIZE_MAX : original[candidate], retained.data()));
        const size_t quantum = 1 + fixture % 7;
        const size_t bound = (2 * source.size() + quantum - 1) / quantum;
        auto result = RuntimePlaybackOrderBuildResult::Pending;
        size_t turns = 0;
        while (result == RuntimePlaybackOrderBuildResult::Pending && turns++ < bound) {
          TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, lookup.step(0));
          result = lookup.step(quantum);
          TEST_ASSERT_TRUE(lookup.lastStepInputCount() <= quantum);
        }
        TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
        TEST_ASSERT_EQUAL(expected, lookup.result() >= 0);
        if (expected) {
          const auto& selected = source[lookup.result()];
          TEST_ASSERT_EQUAL_UINT32(tick, selected.tick);
          if (automatic) TEST_ASSERT_EQUAL_UINT(velocity, selected.data.noteData.velocity);
          else TEST_ASSERT_EQUAL_UINT(original[candidate], lookup.result());
          TEST_ASSERT_EQUAL_UINT32(occupiedWrapHeadOffTick(filtered, tick, head,
                     event.data.noteData.note, event.channel, length), lookup.reconstructedHeadTick());
        }
      }
    }
  }
}

void test_reconstruction_wrap_lookup_retains_reconstruct_specific_rules() {
  RuntimeNotePairLookup lookup;
  TEST_ASSERT_TRUE(lookup.prewarm(2048));
  MidiEventVec events{MidiEvent::NoteOn(2900, 1, 60, 90), MidiEvent::NoteOff(100, 1, 60),
                      MidiEvent::NoteOff(3000, 1, 60)};
  TEST_ASSERT_TRUE(lookup.beginReconstructionWrap(events.data(), events.size(), 1, 3072));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, lookup.step(32));
  TEST_ASSERT_EQUAL_INT(-1, lookup.result()); // A real linear closer defeats reconstruction wrap.
  events[2].tick = 3071; // A synthetic loop-end closer does not defeat it.
  TEST_ASSERT_TRUE(lookup.beginReconstructionWrap(events.data(), events.size(), 1, 3072));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, lookup.step(32));
  TEST_ASSERT_EQUAL_INT(0, lookup.result());
  events.push_back(MidiEvent::NoteOff(3172, 1, 60)); // Same head phase, another cycle.
  events.push_back(MidiEvent::NoteOn(48, 1, 60, 80));
  TEST_ASSERT_TRUE(lookup.beginReconstructionWrap(events.data(), events.size(), 1, 3072));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, lookup.step(32));
  TEST_ASSERT_EQUAL_UINT32(47, lookup.reconstructedHeadTick());
  events.push_back(MidiEvent::NoteOn(200, 1, 60, 80)); // Mid-loop On blocks automatic wrap.
  TEST_ASSERT_TRUE(lookup.beginReconstructionWrap(events.data(), events.size(), 1, 3072));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, lookup.step(32));
  TEST_ASSERT_EQUAL_INT(-1, lookup.result());
  TEST_ASSERT_TRUE(lookup.beginReconstructionWrap(events.data(), events.size(), 1, 3072, 0));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, lookup.step(32));
  TEST_ASSERT_EQUAL_INT(0, lookup.result()); // Explicit preferred-tail check has no mid-On gate.
}

void test_reconstruction_wrap_lookup_maximum_grain_bound_and_tie_order() {
  RuntimeNotePairLookup lookup;
  TEST_ASSERT_TRUE(lookup.prewarm(2048));
  MidiEventVec events(2048, MidiEvent::NoteOn(2900, 1, 60, 90));
  events[0].data.noteData.velocity = 77;
  events.back() = MidiEvent::NoteOff(100, 1, 60);
  for (size_t quantum : {1u, 3u, 32u}) {
    TEST_ASSERT_TRUE(lookup.beginReconstructionWrap(events.data(), events.size(), 2047, 3072));
    TEST_ASSERT_FALSE(lookup.beginReconstructionWrap(events.data(), events.size(), 2047, 3072));
    size_t turns = 0, visited = 0, bound = (4096 + quantum - 1) / quantum;
    auto result = RuntimePlaybackOrderBuildResult::Pending;
    while (result == RuntimePlaybackOrderBuildResult::Pending && turns++ < bound) {
      result = lookup.step(quantum);
      TEST_ASSERT_TRUE(lookup.lastStepInputCount() <= quantum);
      visited += lookup.lastStepInputCount();
    }
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    TEST_ASSERT_EQUAL_UINT(4096, visited);
    TEST_ASSERT_EQUAL_INT(0, lookup.result());
    TEST_ASSERT_EQUAL_UINT(77, events[lookup.result()].data.noteData.velocity);
  }
  TEST_ASSERT_FALSE(lookup.beginReconstructionWrap(events.data(), events.size(), 0, 3072));
  TEST_ASSERT_FALSE(lookup.beginReconstructionWrap(events.data(), events.size(), 2047, 0));
  lookup.discard();
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, lookup.step(1));
}

void assertBoundedCompanionSpans(const MidiEventVec& events, uint8_t pitch, uint32_t length,
                                 size_t quantum, const uint8_t* retained = nullptr) {
  MidiEventVec filtered;
  for (size_t i = 0; i < events.size(); ++i) {
    if ((retained == nullptr || retained[i]) && (events[i].isNoteOn() || events[i].isNoteOff()) &&
        events[i].data.noteData.note == pitch) filtered.push_back(events[i]);
  }
  const auto expected = buildCanonicalSpansFromMidi(filtered, length, false, true);
  RuntimeNotePairLookup pair;
  RuntimeCompanionSpanWalk walk;
  TEST_ASSERT_TRUE(pair.prewarm(2048));
  TEST_ASSERT_TRUE(walk.prewarm(2048));
  TEST_ASSERT_TRUE(walk.begin(pair, events.data(), events.size(), pitch, length, retained));
  auto result = RuntimeCompanionSpanResult::Pending;
  const size_t visitBound = 4 * events.size() + 6 * events.size() * events.size();
  const size_t turnBound = (visitBound + quantum - 1) / quantum + expected.size() + 1;
  size_t turns = 0, spans = 0, visits = 0;
  while (result != RuntimeCompanionSpanResult::Complete && turns++ < turnBound) {
    TEST_ASSERT_EQUAL(RuntimeCompanionSpanResult::Denied, walk.step(0));
    result = walk.step(quantum);
    TEST_ASSERT_TRUE(walk.lastStepInputCount() <= quantum);
    visits += walk.lastStepInputCount();
    if (result == RuntimeCompanionSpanResult::SpanReady) {
      TEST_ASSERT_TRUE(spans < expected.size());
      const auto& actual = walk.span();
      const auto& reference = expected[spans++];
      TEST_ASSERT_EQUAL_UINT(reference.noteId, actual.noteId);
      TEST_ASSERT_EQUAL_UINT(reference.pitch, actual.pitch);
      TEST_ASSERT_EQUAL_UINT(reference.velocity, actual.velocity);
      TEST_ASSERT_EQUAL_INT32(reference.interval.start, actual.interval.start);
      TEST_ASSERT_EQUAL_INT32(reference.interval.end, actual.interval.end);
      TEST_ASSERT_EQUAL(reference.isOpen, actual.isOpen);
      TEST_ASSERT_EQUAL(reference.splitHeadTail, actual.splitHeadTail);
      TEST_ASSERT_EQUAL(RuntimeCompanionSpanResult::SpanReady, walk.step(quantum));
      TEST_ASSERT_EQUAL_UINT(0, walk.lastStepInputCount());
      TEST_ASSERT_TRUE(walk.consumeSpan());
      TEST_ASSERT_FALSE(walk.consumeSpan());
    } else TEST_ASSERT_TRUE(result == RuntimeCompanionSpanResult::Pending ||
                            result == RuntimeCompanionSpanResult::Complete);
  }
  TEST_ASSERT_EQUAL(RuntimeCompanionSpanResult::Complete, result);
  TEST_ASSERT_EQUAL_UINT(expected.size(), spans);
  TEST_ASSERT_TRUE(visits <= visitBound);
  TEST_ASSERT_TRUE(pair.available());
}

void test_bounded_companion_spans_match_canonical_reconstruction() {
  std::mt19937 rng(20260918);
  for (size_t fixture = 0; fixture < 700; ++fixture) {
    const uint32_t length = fixture % 2 ? 768 : 3072;
    MidiEventVec events;
    std::vector<uint8_t> retained;
    for (size_t i = 0, count = rng() % 65; i < count; ++i) {
      uint32_t tick = (rng() % (length / 96 + 3)) * 96;
      if (rng() % 7 == 0) tick = length - 1;
      const uint8_t channel = 1 + rng() % 2, pitch = 60 + rng() % 2;
      auto event = rng() % 2 ? MidiEvent::NoteOn(tick, channel, pitch, rng() % 128)
                            : MidiEvent::NoteOff(tick, channel, pitch);
      event.noteId = rng() % 8;
      events.push_back(event);
      retained.push_back(rng() % 5 != 0);
    }
    assertBoundedCompanionSpans(events, 60, length, 1 + fixture % 7, retained.data());
  }
}

void test_bounded_companion_spans_cover_equal_tick_deferred_end_and_zero_wrap() {
  MidiEventVec events{MidiEvent::NoteOff(100, 1, 60), MidiEvent::NoteOn(100, 1, 60, 90),
                      MidiEvent::NoteOn(100, 2, 60, 80), MidiEvent::NoteOff(200, 1, 60),
                      MidiEvent::NoteOn(2900, 1, 60, 70), MidiEvent::NoteOff(3071, 1, 60),
                      MidiEvent::NoteOff(0, 1, 60)};
  for (size_t i = 0; i < events.size(); ++i) events[i].noteId = i + 1;
  for (size_t quantum : {1u, 3u, 32u}) assertBoundedCompanionSpans(events, 60, 3072, quantum);
  events = {MidiEvent::NoteOn(2900, 1, 60, 90), MidiEvent::NoteOff(3172, 1, 60),
            MidiEvent::NoteOff(100, 1, 60), MidiEvent::NoteOn(48, 1, 60, 80)};
  assertBoundedCompanionSpans(events, 60, 3072, 1);
  events.assign(256, MidiEvent::NoteOff(0, 1, 60));
  events.push_back(MidiEvent::NoteOn(2900, 1, 60, 90));
  assertBoundedCompanionSpans(events, 60, 3072, 3); // Preserve reference uint8 counter wrapping.
}

void test_bounded_companion_spans_maximum_capacity_and_discard() {
  MidiEventVec events(2048, MidiEvent::NoteOn(2900, 1, 60, 90));
  assertBoundedCompanionSpans(events, 60, 3072, 32);
  RuntimeNotePairLookup pair;
  RuntimeCompanionSpanWalk walk;
  TEST_ASSERT_TRUE(pair.prewarm(2048));
  TEST_ASSERT_TRUE(walk.prewarm(2048));
  TEST_ASSERT_FALSE(walk.begin(pair, events.data(), 2049, 60, 3072));
  TEST_ASSERT_TRUE(walk.begin(pair, events.data(), events.size(), 60, 3072));
  TEST_ASSERT_FALSE(walk.begin(pair, events.data(), events.size(), 60, 3072));
  TEST_ASSERT_EQUAL(RuntimeCompanionSpanResult::Pending, walk.step(1));
  walk.discard();
  TEST_ASSERT_EQUAL(RuntimeCompanionSpanResult::Denied, walk.step(1));
  TEST_ASSERT_TRUE(pair.available());
  TEST_ASSERT_TRUE(walk.begin(pair, events.data(), events.size(), 60, 3072));
  TEST_ASSERT_EQUAL_UINT(2048, events.size());
}

void assertBoundedCompanionReconstruction(const MidiEventVec& events, uint8_t pitch, uint32_t length,
                                           size_t quantum, const uint8_t* retained = nullptr) {
  MidiEventVec filtered;
  for (size_t i = 0; i < events.size(); ++i)
    if (retained == nullptr || retained[i]) filtered.push_back(events[i]);
  const auto expected = reconstructNotesForPitch(filtered, pitch, length);
  RuntimeNotePairLookup pair;
  RuntimeCompanionSpanWalk spans;
  RuntimeCompanionReconstruction build;
  TEST_ASSERT_TRUE(pair.prewarm(2048));
  TEST_ASSERT_TRUE(spans.prewarm(2048));
  TEST_ASSERT_TRUE(build.prewarm(4096));
  PlaybackViewSourceIdentity identity{1, 2, events.data()};
  TEST_ASSERT_TRUE(build.begin(spans, pair, events.data(), events.size(), pitch, length, identity, retained));
  TEST_ASSERT_NULL(build.data());
  TEST_ASSERT_EQUAL_UINT(0, build.size());
  const size_t bound = 10 * events.size() * events.size() + 7 * events.size();
  size_t visits = 0, turns = 0;
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  while (result == RuntimePlaybackOrderBuildResult::Pending && turns++ < (bound + quantum - 1) / quantum + 1) {
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, build.step(0));
    result = build.step(quantum);
    TEST_ASSERT_TRUE(build.lastStepInputCount() <= quantum);
    visits += build.lastStepInputCount();
    if (result == RuntimePlaybackOrderBuildResult::Pending) TEST_ASSERT_NULL(build.data());
  }
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  TEST_ASSERT_TRUE(visits <= bound);
  TEST_ASSERT_TRUE(build.ready(identity));
  TEST_ASSERT_EQUAL_UINT(expected.size(), build.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    const auto& actual = build.data()[i];
    TEST_ASSERT_EQUAL_UINT(expected[i].noteId, actual.noteId);
    TEST_ASSERT_EQUAL_UINT(expected[i].note, actual.note);
    TEST_ASSERT_EQUAL_UINT(expected[i].velocity, actual.velocity);
    TEST_ASSERT_EQUAL_UINT32(expected[i].startTick, actual.startTick);
    TEST_ASSERT_EQUAL_UINT32(expected[i].endTick, actual.endTick);
  }
}

void test_bounded_companion_reconstruction_matches_pitch_reference() {
  std::mt19937 rng(20260919);
  for (size_t fixture = 0; fixture < 700; ++fixture) {
    const uint32_t length = fixture % 2 ? 768 : 3072;
    MidiEventVec events;
    std::vector<uint8_t> retained;
    for (size_t i = 0, count = rng() % 65; i < count; ++i) {
      uint32_t tick = (rng() % (3 * length / 96 + 1)) * 96;
      if (rng() % 7 == 0) tick = length - 1;
      const uint8_t channel = 1 + rng() % 2, pitch = 60 + rng() % 2;
      auto event = rng() % 2 ? MidiEvent::NoteOn(tick, channel, pitch, rng() % 128)
                            : MidiEvent::NoteOff(tick, channel, pitch);
      event.noteId = rng() % 8;
      events.push_back(event);
      retained.push_back(rng() % 5 != 0);
    }
    assertBoundedCompanionReconstruction(events, 60, length, 1 + fixture % 7, retained.data());
  }
}

void test_bounded_companion_reconstruction_rejects_stale_and_partial_output() {
  MidiEventVec events{MidiEvent::NoteOn(100, 1, 60, 90), MidiEvent::NoteOff(200, 1, 60),
                      MidiEvent::NoteOn(300, 1, 60, 80), MidiEvent::NoteOff(400, 1, 60)};
  RuntimeNotePairLookup pair;
  RuntimeCompanionSpanWalk spans;
  RuntimeCompanionReconstruction build;
  TEST_ASSERT_TRUE(pair.prewarm(2048));
  TEST_ASSERT_TRUE(spans.prewarm(2048));
  TEST_ASSERT_TRUE(build.prewarm(1)); // Capacity failure must not expose first prepared note.
  PlaybackViewSourceIdentity source{1, 1, events.data()};
  TEST_ASSERT_TRUE(build.begin(spans, pair, events.data(), events.size(), 60, 768, source));
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  for (size_t turns = 0; turns < 200 && result == RuntimePlaybackOrderBuildResult::Pending; ++turns)
    result = build.step(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, result);
  TEST_ASSERT_NULL(build.data());
  TEST_ASSERT_FALSE(build.ready(source));
  TEST_ASSERT_TRUE(pair.available());
  build.discard();
  TEST_ASSERT_TRUE(build.begin(spans, pair, events.data(), events.size(), 60, 768, source));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, build.step(1));
  ++source.sourceGeneration;
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, build.step(1));
  TEST_ASSERT_NULL(build.data());
  --source.sourceGeneration;
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, build.step(1));
  TEST_ASSERT_FALSE(build.ready(source));
  build.discard();
  TEST_ASSERT_TRUE(build.begin(spans, pair, events.data(), events.size(), 61, 768, source));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, build.step(200));
  TEST_ASSERT_TRUE(build.ready(source));
  const auto* backing = build.data();
  ++source.playbackRevision;
  TEST_ASSERT_FALSE(build.ready(source));
  TEST_ASSERT_NULL(build.data());
  build.discard();
  TEST_ASSERT_TRUE(build.begin(spans, pair, events.data(), events.size(), 61, 768, source));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, build.step(200));
  TEST_ASSERT_EQUAL_PTR(backing, build.data());
}

void test_bounded_companion_reconstruction_maximum_unique_and_duplicate_geometry() {
  MidiEventVec events;
  for (size_t i = 0; i < 2048; ++i) {
    auto event = MidiEvent::NoteOn(i, 1, 60, 90);
    event.noteId = i + 1;
    events.push_back(event);
  }
  assertBoundedCompanionReconstruction(events, 60, 3072, 32);
  for (size_t i = 0; i < events.size(); ++i) {
    events[i].tick = 2900;
    events[i].data.noteData.velocity = 1 + i % 127;
  }
  assertBoundedCompanionReconstruction(events, 60, 3072, 3);
}

struct CompanionApplyFixture {
  RuntimeNotePairLookup pair;
  RuntimeNoteDeleteLookup deletion;
  RuntimeCompanionSpanWalk spans;
  RuntimeCompanionReconstruction reconstruction;
  RuntimeCompanionOverlapWalk overlaps;
  RuntimeCompanionApply apply;
  RuntimeCompanionApply::Work work{pair, deletion, spans, reconstruction, overlaps};
  void prewarm(size_t capacity) {
    TEST_ASSERT_TRUE(pair.prewarm(2048));
    TEST_ASSERT_TRUE(spans.prewarm(2048));
    TEST_ASSERT_TRUE(reconstruction.prewarm(4096));
    TEST_ASSERT_TRUE(apply.prewarm(capacity));
  }
};

void assertCompanionBridgeMapping(const RuntimeCompanionApply& apply, const MidiEventVec& base,
                                 const PlaybackViewSourceIdentity& source, size_t quantum) {
  RuntimeCompanionBridgeMap mapping;
  TEST_ASSERT_TRUE(mapping.prewarm(2048));
  TEST_ASSERT_TRUE(mapping.begin(apply, base.data(), base.size(), source));
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  size_t turns = 0;
  const size_t bound = (2 * base.size() + apply.size() + quantum - 1) / quantum + 2;
  while (result == RuntimePlaybackOrderBuildResult::Pending && turns++ < bound) {
    TEST_ASSERT_NULL(mapping.records()); TEST_ASSERT_NULL(mapping.baseRanks());
    TEST_ASSERT_NULL(mapping.recordRanks()); TEST_ASSERT_EQUAL_UINT32(0, mapping.size());
    TEST_ASSERT_FALSE(mapping.begin(apply, base.data(), base.size(), source));
    result = mapping.step(quantum);
    TEST_ASSERT_TRUE(mapping.lastStepInputCount() <= quantum);
  }
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  TEST_ASSERT_TRUE(mapping.ready(source));
  MidiEventVec reconstructed(apply.size());
  std::vector<bool> written(apply.size(), false), touched(base.size(), false);
  for (size_t i = 0; i < base.size(); ++i) {
    const uint16_t rank = mapping.baseRanks()[i];
    if (rank != UINT16_MAX) {
      TEST_ASSERT_TRUE(rank < apply.size()); TEST_ASSERT_FALSE(written[rank]);
      reconstructed[rank] = base[i]; written[rank] = true;
    }
  }
  for (size_t i = 0; i < mapping.size(); ++i) {
    const auto& record = mapping.records()[i];
    const auto rank = mapping.recordRanks()[i];
    if (record.operation != PlaybackBridgeOperation::Insert) {
      TEST_ASSERT_TRUE(record.eventProvenance < base.size());
      TEST_ASSERT_FALSE(touched[record.eventProvenance]); touched[record.eventProvenance] = true;
    }
    if (record.operation == PlaybackBridgeOperation::Suppress) {
      TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, rank);
      TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, mapping.baseRanks()[record.eventProvenance]);
    } else {
      TEST_ASSERT_TRUE(rank < apply.size());
      if (record.operation == PlaybackBridgeOperation::Insert) TEST_ASSERT_FALSE(written[rank]);
      else TEST_ASSERT_EQUAL_UINT16(rank, mapping.baseRanks()[record.eventProvenance]);
      reconstructed[rank] = record.event; written[rank] = true;
    }
  }
  for (bool row : written) TEST_ASSERT_TRUE(row);
  assertMidiEventVecEquivalent(MidiEventVec(apply.data(), apply.data() + apply.size()),
                               reconstructed, "ranked companion bridge mapping matches candidate");
}

void assertBoundedCompanionApply(const MidiEventVec& input, RuntimeCompanionApply::Type kind,
                                 NoteId target, uint32_t end, uint32_t length, size_t quantum) {
  auto expected = input;
  if (kind == RuntimeCompanionApply::Type::Delete) applyDeleteNoteById(expected, target, length, nullptr);
  else applyChangeLengthById(expected, target, end, length, nullptr);
  const auto frozen = input;
  CompanionApplyFixture fixture;
  const size_t capacity = std::min<size_t>(2048, std::max<size_t>(1, input.size() + 1));
  fixture.prewarm(capacity);
  PlaybackViewSourceIdentity source{1, 2, input.data()};
  TEST_ASSERT_TRUE(fixture.apply.begin(fixture.work, input.data(), input.size(), source, kind, target, end, length));
  const size_t bound = RuntimeCompanionApply::requiredGrainsUpperBound(capacity, quantum);
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  size_t turns = 0;
  while (result == RuntimePlaybackOrderBuildResult::Pending && turns++ < bound) {
    TEST_ASSERT_NULL(fixture.apply.data());
    TEST_ASSERT_NULL(fixture.apply.provenance());
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, fixture.apply.step(0));
    result = fixture.apply.step(quantum);
  }
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  TEST_ASSERT_TRUE(fixture.apply.ready(source));
  assertCompanionBridgeMapping(fixture.apply, input, source, quantum);
  const MidiEventVec actual(fixture.apply.data(), fixture.apply.data() + fixture.apply.size());
  assertMidiEventVecEquivalent(expected, actual, "bounded companion apply vs actual reference");
  assertMidiEventVecEquivalent(frozen, input, "source remains immutable during companion apply");
  for (size_t i = 0; i < actual.size(); ++i) {
    const uint32_t origin = fixture.apply.provenance()[i];
    if (origin == UINT32_MAX) {
      TEST_ASSERT_TRUE(actual[i].isNoteOff());
      TEST_ASSERT_EQUAL_UINT(kInvalidNoteId, actual[i].noteId);
      TEST_ASSERT_EQUAL_UINT32(end, actual[i].tick);
    } else {
      TEST_ASSERT_TRUE(origin < input.size());
      auto original = input[origin];
      original.tick = actual[i].tick;
      assertMidiEventVecEquivalent(MidiEventVec{original}, MidiEventVec{actual[i]}, "event provenance survives compaction and ordering");
    }
  }
}

void test_bounded_companion_apply_matches_delete_and_length_reference() {
  std::mt19937 rng(20260920);
  for (size_t fixture = 0; fixture < 900; ++fixture) {
    const uint32_t length = fixture % 3 == 0 ? 0 : (fixture % 2 ? 768 : 3072);
    MidiEventVec events;
    for (size_t i = 0, count = rng() % 33; i < count; ++i) {
      uint32_t tick = (rng() % 35) * 96;
      if (rng() % 7 == 0) tick = 3071;
      const uint8_t channel = 1 + rng() % 2, pitch = 60 + rng() % 2;
      auto event = rng() % 2 ? MidiEvent::NoteOn(tick, channel, pitch, rng() % 128)
                            : MidiEvent::NoteOff(tick, channel, pitch);
      if (rng() % 9 == 0) event = MidiEvent::ControlChange(tick, channel, 1, 20);
      event.noteId = rng() % 6;
      events.push_back(event);
    }
    const NoteId target = fixture % 6;
    const uint32_t end = (rng() % 36) * 96;
    assertBoundedCompanionApply(events, fixture % 4 ? RuntimeCompanionApply::Type::Length
               : RuntimeCompanionApply::Type::Delete, target, end, length, 1 + fixture % 7);
  }
}

void test_bounded_companion_apply_synthesizes_off_and_keeps_shortening_order() {
  MidiEventVec events{MidiEvent::NoteOn(600, 1, 60, 90), MidiEvent::NoteOn(100, 1, 61, 80),
                      MidiEvent::NoteOff(200, 1, 61)};
  events[0].noteId = 1; events[1].noteId = 2;
  for (size_t quantum : {1u, 3u, 32u}) {
    assertBoundedCompanionApply(events, RuntimeCompanionApply::Type::Length, 1, 700, 768, quantum);
    assertBoundedCompanionApply(events, RuntimeCompanionApply::Type::Length, 1, 768, 768, quantum);
    assertBoundedCompanionApply(events, RuntimeCompanionApply::Type::Length, 2, 150, 768, quantum);
    assertBoundedCompanionApply(events, RuntimeCompanionApply::Type::Delete, 2, 0, 768, quantum);
  }
}

void test_bounded_companion_apply_capacity_and_stale_source_hide_partial_candidate() {
  MidiEventVec events{MidiEvent::NoteOn(600, 1, 60, 90)};
  events[0].noteId = 1;
  CompanionApplyFixture fixture;
  fixture.prewarm(1);
  PlaybackViewSourceIdentity source{1, 1, events.data()};
  TEST_ASSERT_TRUE(fixture.apply.begin(fixture.work, events.data(), events.size(), source,
                                     RuntimeCompanionApply::Type::Length, 1, 700, 768));
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  for (size_t i = 0; i < 200 && result == RuntimePlaybackOrderBuildResult::Pending; ++i) result = fixture.apply.step(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, result);
  TEST_ASSERT_NULL(fixture.apply.data());
  TEST_ASSERT_EQUAL_UINT(1, events.size());
  TEST_ASSERT_TRUE(fixture.pair.available());
  TEST_ASSERT_TRUE(fixture.apply.begin(fixture.work, events.data(), events.size(), source,
                                     RuntimeCompanionApply::Type::Length, 1, 768, 768));
  TEST_ASSERT_FALSE(fixture.apply.begin(fixture.work, events.data(), events.size(), source,
                                      RuntimeCompanionApply::Type::Length, 1, 768, 768));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, fixture.apply.step(1));
  ++source.sourceGeneration;
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, fixture.apply.step(1));
  TEST_ASSERT_NULL(fixture.apply.data());
  --source.sourceGeneration;
  TEST_ASSERT_FALSE(fixture.apply.ready(source));
  TEST_ASSERT_TRUE(fixture.apply.begin(fixture.work, events.data(), events.size(), source,
                                     RuntimeCompanionApply::Type::Length, 1, 768, 768));
  result = RuntimePlaybackOrderBuildResult::Pending;
  for (size_t i = 0; i < 200 && result == RuntimePlaybackOrderBuildResult::Pending; ++i) result = fixture.apply.step(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  const auto* output = fixture.apply.data();
  TEST_ASSERT_FALSE(fixture.apply.begin(fixture.work, output, fixture.apply.size(), source,
                                      RuntimeCompanionApply::Type::Delete, 1, 0, 768));
  TEST_ASSERT_EQUAL_PTR(output, fixture.apply.data());
  ++source.playbackRevision;
  TEST_ASSERT_NULL(fixture.apply.data());
}

void test_bounded_companion_batch_matches_sequential_reference_and_base_provenance() {
  std::mt19937 rng(20260921);
  for (size_t test = 0; test < 400; ++test) {
    MidiEventVec events;
    for (size_t i = 0; i < 24; ++i) {
      auto event = rng() % 2 ? MidiEvent::NoteOn((rng() % 34) * 96, 1 + rng() % 2, 60 + rng() % 2, 90)
                            : MidiEvent::NoteOff((rng() % 34) * 96, 1 + rng() % 2, 60 + rng() % 2);
      event.noteId = rng() % 8;
      events.push_back(event);
    }
    const uint32_t length = test % 3 ? 3072 : 0;
    std::vector<EditPass> rows(8);
    auto expected = events;
    for (auto& row : rows) {
      row.passType = EditPassType::Note;
      row.targetNoteId = rng() % 8;
      row.endTick = (rng() % 38) * 96;
      row.actionType = rng() % 3 ? EditActionType::Update : EditActionType::Delete;
      row.propertyType = row.actionType == EditActionType::Update ? EditPropertyType::Length : EditPropertyType::None;
      if (row.actionType == EditActionType::Delete) applyDeleteNoteById(expected, row.targetNoteId, length, nullptr);
      else applyChangeLengthById(expected, row.targetNoteId, row.endTick, length, nullptr);
    }
    CompanionApplyFixture fixture;
    fixture.prewarm(64);
    PlaybackViewSourceIdentity source{1, 2, events.data()};
    TEST_ASSERT_TRUE(fixture.apply.beginBatch(fixture.work, events.data(), events.size(), source, rows.data(), rows.size(), length));
    const size_t quantum = 1 + test % 7;
    const size_t bound = (rows.size() + 1) * RuntimeCompanionApply::requiredGrainsUpperBound(64, quantum);
    auto result = RuntimePlaybackOrderBuildResult::Pending;
    size_t turns = 0;
    while (result == RuntimePlaybackOrderBuildResult::Pending && turns++ < bound) {
      TEST_ASSERT_NULL(fixture.apply.data());
      result = fixture.apply.step(quantum);
    }
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    TEST_ASSERT_TRUE(fixture.apply.ready(source));
    assertCompanionBridgeMapping(fixture.apply, events, source, quantum);
    MidiEventVec actual(fixture.apply.data(), fixture.apply.data() + fixture.apply.size());
    assertMidiEventVecEquivalent(expected, actual, "bounded companion batch vs sequential reference");
    for (size_t i = 0; i < actual.size(); ++i) {
      const auto origin = fixture.apply.provenance()[i];
      if (origin == UINT32_MAX) TEST_ASSERT_TRUE(actual[i].isNoteOff());
      else {
        TEST_ASSERT_TRUE(origin < events.size());
        auto original = events[origin]; original.tick = actual[i].tick;
        assertMidiEventVecEquivalent(MidiEventVec{original}, MidiEventVec{actual[i]}, "base provenance across companion batch");
      }
    }
  }
}

void test_create_history_preserves_equal_tick_append_order() {
  for (size_t existingCount : {size_t(0), size_t(12), size_t(25)}) {
    MidiEventVec events;
    EditPass row;
    row.actionType = EditActionType::Create;
    for (size_t i = 0; i < 25; ++i) {
      auto event = MidiEvent::NoteOn(i % 3, 1, 60, 90);
      event.noteId = i + 1;
      if (i < existingCount) events.push_back(event);
      else row.addedEvents.push_back(event);
    }
    applyNoteEditPassSequence(events, EditPassVec{row}, 768);
    TEST_ASSERT_EQUAL_UINT(25, events.size());
    size_t index = 0;
    for (size_t tick = 0; tick < 3; ++tick) {
      for (size_t originalIndex = tick; originalIndex < 25; originalIndex += 3) {
        TEST_ASSERT_EQUAL_UINT32(tick, events[index].tick);
        TEST_ASSERT_EQUAL_UINT32(originalIndex + 1, events[index].noteId);
        ++index;
      }
    }
  }
}

void test_bounded_history_updates_match_canonical_sequence() {
  std::mt19937 rng(20260924);
  for (size_t test = 0; test < 300; ++test) {
    MidiEventVec events;
    for (size_t i = 0; i < 24; ++i) {
      auto event = rng() % 2 ? MidiEvent::NoteOn((rng() % 34) * 96, 1 + rng() % 2, 60 + rng() % 3, 90)
                            : MidiEvent::NoteOff((rng() % 34) * 96, 1 + rng() % 2, 60 + rng() % 3);
      event.noteId = rng() % 8;
      events.push_back(event);
    }
    EditPassVec rows;
    for (size_t i = 0; i < 10; ++i) {
      EditPass row;
      row.targetNoteId = rng() % 8;
      row.actionType = EditActionType::Update;
      const EditPropertyType properties[] = {EditPropertyType::Pitch, EditPropertyType::Velocity,
          EditPropertyType::NoteRange, EditPropertyType::Length};
      row.propertyType = properties[rng() % 4];
      row.pitch = 60 + rng() % 3; row.velocity = rng() % 128;
      row.startTick = (rng() % 34) * 96; row.endTick = (rng() % 38) * 96;
      if (rng() % 7 == 0) row.actionType = EditActionType::Delete;
      if (rng() % 5 == 0) {
        row.actionType = EditActionType::Create;
        auto on = MidiEvent::NoteOn(row.startTick, 1, row.pitch, row.velocity);
        on.noteId = row.targetNoteId;
        row.addedEvents = {on, MidiEvent::NoteOff(row.endTick, 1, row.pitch),
                          MidiEvent::ControlChange(row.startTick, 1, 7, 64)};
      }
      rows.push_back(row);
      if (i % 3 == 0) { row.pitch = 62; row.velocity = 0; rows.push_back(row); }
    }
    const auto untouched = events;
    auto expected = events;
    const uint32_t length = test % 3 ? 3072 : 0;
    applyNoteEditPassSequence(expected, rows, length);
    CompanionApplyFixture fixture; fixture.prewarm(128);
    PlaybackViewSourceIdentity source{1, 2, events.data()};
    TEST_ASSERT_TRUE(fixture.apply.beginHistoryBatch(fixture.work, events.data(), events.size(),
        source, rows.data(), rows.size(), length));
    const size_t quantum = 1 + test % 7;
    const uint64_t bound = RuntimeCompanionApply::requiredHistoryGrainsUpperBound(128, rows.size(), quantum);
    auto result = RuntimePlaybackOrderBuildResult::Pending;
    size_t turns = 0;
    while (result == RuntimePlaybackOrderBuildResult::Pending && turns++ < bound) {
      TEST_ASSERT_NULL(fixture.apply.data());
      result = fixture.apply.step(quantum);
    }
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    MidiEventVec actual(fixture.apply.data(), fixture.apply.data() + fixture.apply.size());
    assertMidiEventVecEquivalent(expected, actual, "bounded history updates vs canonical sequence");
    assertMidiEventVecEquivalent(untouched, events, "history source is immutable");
    for (size_t i = 0; i < actual.size(); ++i) {
      const auto origin = fixture.apply.provenance()[i];
      if (origin != UINT32_MAX) {
        TEST_ASSERT_TRUE(origin < events.size());
        TEST_ASSERT_EQUAL(events[origin].noteId, actual[i].noteId);
        TEST_ASSERT_EQUAL(events[origin].channel, actual[i].channel);
        TEST_ASSERT_EQUAL(events[origin].type, actual[i].type);
      }
    }
  }
}

void assertBoundedCreateHistory(const MidiEventVec& input, const EditPassVec& rows, size_t capacity, size_t quantum) {
  auto expected = input;
  applyNoteEditPassSequence(expected, rows, 768);
  CompanionApplyFixture fixture; fixture.prewarm(capacity);
  PlaybackViewSourceIdentity source{1, 2, input.data()};
  TEST_ASSERT_TRUE(fixture.apply.beginHistoryBatch(fixture.work, input.data(), input.size(),
      source, rows.data(), rows.size(), 768));
  const uint64_t bound = RuntimeCompanionApply::requiredHistoryGrainsUpperBound(capacity, rows.size(), quantum);
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  uint64_t turns = 0;
  while (result == RuntimePlaybackOrderBuildResult::Pending && turns++ < bound) {
    TEST_ASSERT_NULL(fixture.apply.data());
    result = fixture.apply.step(quantum);
  }
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  assertMidiEventVecEquivalent(expected, MidiEventVec(fixture.apply.data(), fixture.apply.data() + fixture.apply.size()),
                              "bounded Create history vs DEC-061 reference");
}

void test_bounded_create_reactivates_identity_with_surviving_duplicate() {
  auto first = MidiEvent::NoteOn(0, 1, 60, 90); first.noteId = 1;
  auto duplicate = first; duplicate.tick = 20;
  MidiEventVec input{first, MidiEvent::NoteOff(10, 1, 60), duplicate, MidiEvent::NoteOff(30, 1, 60)};
  EditPass deletion; deletion.actionType = EditActionType::Delete; deletion.targetNoteId = 1;
  EditPass velocity; velocity.targetNoteId = 1; velocity.propertyType = EditPropertyType::Velocity; velocity.velocity = 55;
  EditPass zeroCreate; zeroCreate.actionType = EditActionType::Create;
  auto zeroOn = first; zeroOn.tick = 15; zeroOn.data.noteData.velocity = 0;
  zeroCreate.addedEvents = {zeroOn};
  EditPass create; create.actionType = EditActionType::Create;
  auto addedOn = first; addedOn.tick = 100; create.addedEvents = {addedOn};
  auto laterCreate = create; laterCreate.addedEvents[0].tick = 200;
  auto finalVelocity = velocity; finalVelocity.velocity = 77;
  EditPassVec rows{deletion, velocity, zeroCreate, velocity, create, velocity,
                   deletion, velocity, laterCreate, finalVelocity};
  auto expected = input;
  applyNoteEditPassSequence(expected, rows, 768);
  const int selected = findNoteOnById(expected, 1);
  TEST_ASSERT_TRUE(selected >= 0);
  TEST_ASSERT_EQUAL(100, expected[selected].tick);
  TEST_ASSERT_EQUAL(77, expected[selected].data.noteData.velocity);
  for (size_t quantum : {size_t(1), size_t(7), size_t(32)}) assertBoundedCreateHistory(input, rows, 16, quantum);
}

void test_bounded_create_empty_maximum_capacity_and_cancellation() {
  MidiEventVec input{MidiEvent::NoteOn(100, 1, 60, 90), MidiEvent::NoteOff(0, 1, 60)};
  input[0].noteId = 1;
  EditPass create; create.actionType = EditActionType::Create;
  assertBoundedCreateHistory(input, EditPassVec{create}, 2, 1);
  assertBoundedCreateHistory(MidiEventVec{}, EditPassVec{create}, 2, 1);
  for (size_t i = 0; i < 2048; ++i)
    create.addedEvents.push_back(MidiEvent::ControlChange(i % 3, 1, i % 128, i % 127));
  assertBoundedCreateHistory(MidiEventVec{}, EditPassVec{create}, 2048, 1);
  CompanionApplyFixture fixture; fixture.prewarm(2048);
  PlaybackViewSourceIdentity source{1, 2, input.data()};
  TEST_ASSERT_TRUE(fixture.apply.beginHistoryBatch(fixture.work, nullptr, 0, source, &create, 1, 768));
  // Copy -> next row -> collapse -> append, then cancel after a partial append.
  for (size_t i = 0; i < 4; ++i)
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, fixture.apply.step(1));
  ++source.sourceGeneration;
  TEST_ASSERT_NULL(fixture.apply.data());
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, fixture.apply.step(1));
  // Aggregate overflow denies instead of truncating an accepted Create payload.
  TEST_ASSERT_TRUE(fixture.apply.beginHistoryBatch(fixture.work, input.data(), input.size(), source, &create, 1, 768));
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  for (size_t i = 0; i < 20 && result == RuntimePlaybackOrderBuildResult::Pending; ++i)
    result = fixture.apply.step(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, result);
  TEST_ASSERT_NULL(fixture.apply.data());
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, RuntimeCompanionApply::requiredHistoryGrainsUpperBound(2048, 2049, 1));
  TEST_ASSERT_TRUE(RuntimeCompanionApply::requiredHistoryGrainsUpperBound(2048, 2048, 1) > UINT32_MAX);
}

void test_canonical_pass_inputs_feed_per_pass_companions() {
  std::mt19937 rng(20260925);
  for (size_t sample = 0; sample < 80; ++sample) {
    LoopEventStore::resetPoolForTests(); LoopEventStore::initPool();
    CaptureChunkIdList refs[2];
    MidiEventVec expectedPasses[2];
    for (size_t pass = 0; pass < 2; ++pass) {
      LoopEventStore store;
      for (size_t i = 0; i < 12; ++i) {
        auto on = MidiEvent::NoteOn(rng() % 700, 1, 60 + rng() % 3, 90); on.noteId = 1 + rng() % 5;
        auto off = MidiEvent::NoteOff(rng() % 768, 1, on.data.noteData.note);
        expectedPasses[pass].push_back(on); expectedPasses[pass].push_back(off);
        TEST_ASSERT_TRUE(store.append(on)); TEST_ASSERT_TRUE(store.append(off));
      }
      store.detachChunksTo(refs[pass]);
    }
    RuntimeCanonicalPassInputs::SealedPass descriptors[] = {{refs[0].data(), refs[0].size()}, {refs[1].data(), refs[1].size()}};
    EditPass pitch; pitch.targetNoteId = 1; pitch.propertyType = EditPropertyType::Pitch; pitch.pitch = 65;
    EditPass create; create.actionType = EditActionType::Create;
    auto added = MidiEvent::NoteOn(50, 1, 64, 80); added.noteId = 7;
    create.addedEvents = {added, MidiEvent::NoteOff(90, 1, 64)};
    EditPassVec history{pitch, create};
    CompanionApplyFixture fixture; fixture.prewarm(128);
    RuntimeCanonicalPassInputs inputs; TEST_ASSERT_TRUE(inputs.prewarm(128));
    PlaybackViewSourceIdentity source{1, 2, descriptors};
    TEST_ASSERT_TRUE(inputs.begin(fixture.apply, fixture.work, descriptors, 2, history.data(), history.size(), source, 768));
    auto result = RuntimePlaybackOrderBuildResult::Pending;
    size_t turns = 0;
    while (result == RuntimePlaybackOrderBuildResult::Pending && turns++ < 10000) {
      TEST_ASSERT_NULL(inputs.data()); TEST_ASSERT_EQUAL(0, inputs.eventCount());
      result = inputs.step(1 + sample % 7);
    }
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    TEST_ASSERT_EQUAL(2, inputs.passCount());
    for (size_t pass = 0; pass < 2; ++pass) {
      applyNoteEditPassSequence(expectedPasses[pass], history, 768);
      const auto& actual = inputs.data()[pass];
      assertMidiEventVecEquivalent(expectedPasses[pass], MidiEventVec(actual.events, actual.events + actual.count), "effective pass history");
    }
    EditPass deletion; deletion.actionType = EditActionType::Delete; deletion.targetNoteId = 1;
    EditPassVec companions{deletion};
    for (auto& events : expectedPasses) applyNoteEditPassSequence(events, companions, 768);
    MidiEventVec expected(expectedPasses[0].size() + expectedPasses[1].size());
    std::merge(expectedPasses[0].begin(), expectedPasses[0].end(), expectedPasses[1].begin(), expectedPasses[1].end(), expected.begin(),
               [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
    RuntimeCompanionPasses prepared; TEST_ASSERT_TRUE(prepared.prewarm(128));
    PlaybackViewSourceIdentity preparedSource{2, 3, descriptors};
    TEST_ASSERT_TRUE(prepared.begin(fixture.apply, fixture.work, inputs, companions.data(), companions.size(), preparedSource, 768));
    result = RuntimePlaybackOrderBuildResult::Pending; turns = 0;
    while (result == RuntimePlaybackOrderBuildResult::Pending && turns++ < 10000) result = prepared.step(3);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    assertMidiEventVecEquivalent(expected, MidiEventVec(prepared.data(), prepared.data() + prepared.size()), "history then independent companions");
    inputs.discard();
    TEST_ASSERT_FALSE(prepared.ready(preparedSource)); TEST_ASSERT_NULL(prepared.data());
    prepared.discard();
    for (auto& ids : refs) LoopEventStore::releaseChunkRefs(ids);
  }
}

void test_canonical_pass_inputs_capacity_stale_and_borrow_guards() {
  LoopEventStore::resetPoolForTests(); LoopEventStore::initPool();
  LoopEventStore store; TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(0, 1, 60, 90)));
  CaptureChunkIdList refs; store.detachChunksTo(refs);
  RuntimeCanonicalPassInputs::SealedPass descriptors[] = {{refs.data(), refs.size()}, {refs.data(), refs.size()}};
  CompanionApplyFixture fixture; fixture.prewarm(2);
  RuntimeCanonicalPassInputs inputs; TEST_ASSERT_TRUE(inputs.prewarm(2));
  PlaybackViewSourceIdentity source{1, 2, descriptors};
  EditPass create; create.actionType = EditActionType::Create;
  create.addedEvents = {MidiEvent::NoteOff(100, 1, 60)};
  TEST_ASSERT_TRUE(inputs.begin(fixture.apply, fixture.work, descriptors, 2, &create, 1, source, 768));
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  for (size_t i = 0; i < 200 && result == RuntimePlaybackOrderBuildResult::Pending; ++i) result = inputs.step(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, result); TEST_ASSERT_NULL(inputs.data());
  TEST_ASSERT_EQUAL(0, inputs.eventCount());
  EditPass guard; guard.targetNoteId = 1; guard.propertyType = EditPropertyType::Velocity;
  TEST_ASSERT_TRUE(inputs.begin(fixture.apply, fixture.work, descriptors, 1, &guard, 1, source, 768));
  TEST_ASSERT_FALSE(inputs.begin(fixture.apply, fixture.work, descriptors, 1, nullptr, 0, source, 768));
  // Start, read event, finish chunk and borrow apply; then reuse that worker.
  for (size_t i = 0; i < 3; ++i) TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, inputs.step(1));
  fixture.apply.discard();
  MidiEvent external = MidiEvent::NoteOn(10, 1, 61, 90);
  TEST_ASSERT_TRUE(fixture.apply.beginBatch(fixture.work, &external, 1, source, nullptr, 0, 768));
  const auto generation = fixture.apply.generation();
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, inputs.step(1));
  TEST_ASSERT_EQUAL_UINT64(generation, fixture.apply.generation());
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, fixture.apply.step(1));
  TEST_ASSERT_TRUE(inputs.begin(fixture.apply, fixture.work, descriptors, 1, nullptr, 0, source, 768));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, inputs.step(1));
  ++source.sourceGeneration;
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, inputs.step(1));
  TEST_ASSERT_NULL(inputs.data());
  inputs.discard(); LoopEventStore::releaseChunkRefs(refs);
  TEST_ASSERT_TRUE(inputs.begin(fixture.apply, fixture.work, nullptr, 0, nullptr, 0, source, 768));
  TEST_ASSERT_TRUE(inputs.ready()); TEST_ASSERT_EQUAL(0, inputs.passCount());
}

void test_canonical_pass_inputs_maximum_and_pending_consumer_invalidation() {
  LoopEventStore::resetPoolForTests(); LoopEventStore::initPool();
  CaptureChunkIdList refs[2];
  RuntimeCanonicalPassInputs::SealedPass passes[2];
  for (size_t p = 0; p < 2; ++p) {
    LoopEventStore store;
    for (size_t i = 0; i < 1024; ++i) TEST_ASSERT_TRUE(store.append(MidiEvent::ControlChange(i % 700, 1, p + 1, i % 128)));
    store.detachChunksTo(refs[p]); passes[p] = {refs[p].data(), refs[p].size()};
  }
  CompanionApplyFixture fixture; fixture.prewarm(2048);
  RuntimeCanonicalPassInputs inputs; TEST_ASSERT_TRUE(inputs.prewarm(2048));
  TEST_ASSERT_EQUAL_UINT64(2048 * (2 * sizeof(MidiEvent) + 2 * sizeof(RuntimeCompanionPasses::Pass) + sizeof(RuntimeCanonicalPassInputs::SealedPass) + 2 * sizeof(uint16_t) + sizeof(const EditPass*)), inputs.fixedExternalBytes());
  PlaybackViewSourceIdentity source{1, 2, passes};
  TEST_ASSERT_TRUE(inputs.begin(fixture.apply, fixture.work, passes, 2, nullptr, 0, source, 768));
  const auto bound = RuntimeCanonicalPassInputs::requiredGrainsUpperBound(2048, 2, 0, 7);
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  uint64_t turns = 0;
  while (result == RuntimePlaybackOrderBuildResult::Pending && turns++ < bound) result = inputs.step(7);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  TEST_ASSERT_EQUAL(2048, inputs.eventCount());
  for (size_t p = 0; p < 2; ++p) {
    TEST_ASSERT_EQUAL(1024, inputs.data()[p].count);
    for (size_t i = 0; i < 1024; ++i) {
      TEST_ASSERT_EQUAL(i % 700, inputs.data()[p].events[i].tick);
      TEST_ASSERT_EQUAL(p + 1, inputs.data()[p].events[i].data.ccData.cc);
    }
  }
  RuntimeCompanionPasses prepared; TEST_ASSERT_TRUE(prepared.prewarm(2048));
  TEST_ASSERT_FALSE(prepared.begin(fixture.apply, fixture.work, inputs, nullptr, 0, source, 1536));
  TEST_ASSERT_TRUE(prepared.begin(fixture.apply, fixture.work, inputs, nullptr, 0, source, 768));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, prepared.step(1));
  inputs.discard();
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, prepared.step(1));
  TEST_ASSERT_NULL(prepared.data());
  TEST_ASSERT_TRUE(fixture.apply.available());
  for (auto& ids : refs) LoopEventStore::releaseChunkRefs(ids);
}

void test_canonical_pass_selection_and_filtered_history_match_materialization() {
  for (size_t sample = 0; sample < 32; ++sample) {
    LoopEventStore::resetPoolForTests(); LoopEventStore::initPool();
    LoopPasses passes;
    const auto fill = [](CommittedChunkIdList& refs, uint8_t pitch) {
      LoopEventStore store;
      auto on = MidiEvent::NoteOn(10, 1, pitch, 90); on.noteId = 1;
      TEST_ASSERT_TRUE(store.append(on)); TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(50, 1, pitch)));
      TEST_ASSERT_TRUE(store.detachChunksToCommittedChunkIds(refs));
    };
    passes.recordPass.id = sample % 2 ? 1 : kInvalidPassId;
    passes.recordPass.state = sample % 3 ? CapturePassState::Active : CapturePassState::Disabled;
    fill(passes.recordPass.committedChunkIds, 60);
    passes.overdubPasses.resize(4);
    const uint32_t sequence[] = {30, 0, UINT32_MAX, 10};
    for (size_t i = 0; i < 4; ++i) {
      auto& pass = passes.overdubPasses[i]; pass.id = i + 2; pass.mergeSequence = sequence[i];
      pass.state = sample & (1u << i) ? CapturePassState::Active : CapturePassState::Disabled;
      if (i != 2 || sample % 2) fill(pass.committedChunkIds, 61 + i);
    }
    EditPass velocity; velocity.targetNoteId = 1; velocity.propertyType = EditPropertyType::Velocity; velocity.velocity = 0;
    // Canonical non-Create rows ignore leftover payloads; filtering must not copy or apply them.
    velocity.addedEvents = {MidiEvent::ControlChange(0, 1, 7, 99)};
    EditPass disabled; disabled.actionType = EditActionType::Delete; disabled.targetNoteId = 1; disabled.state = EditPassState::Disabled;
    EditPass ignored = disabled; ignored.state = EditPassState::Active; ignored.passType = EditPassType::ControlChange;
    auto lastVelocity = velocity; lastVelocity.velocity = 88;
    EditPass create; create.actionType = EditActionType::Create;
    auto added = MidiEvent::NoteOn(10, 1, 70, 80); added.noteId = 7;
    create.addedEvents = {added, MidiEvent::NoteOff(60, 1, 70)};
    passes.editPasses = {velocity, disabled, ignored, lastVelocity, create};
    if (sample % 4 == 0) {
      EditPass noOp; noOp.targetNoteId = 1; noOp.propertyType = EditPropertyType::None;
      passes.editPasses.insert(passes.editPasses.begin() + 3, noOp);
    }
    SessionMidiEventVec reference;
    passes.materializeToEventVector(reference, 768);
    CompanionApplyFixture fixture; fixture.prewarm(32);
    RuntimeCanonicalPassInputs inputs; TEST_ASSERT_TRUE(inputs.prewarm(32));
    PlaybackViewSourceIdentity source{1, 2, &passes};
    TEST_ASSERT_TRUE(inputs.begin(fixture.apply, fixture.work, passes, source, 768));
    const size_t quantum = 1 + sample % 7;
    const auto bound = RuntimeCanonicalPassInputs::requiredSelectedGrainsUpperBound(32, passes.editPasses.size(), quantum);
    auto result = RuntimePlaybackOrderBuildResult::Pending;
    uint64_t turns = 0;
    while (result == RuntimePlaybackOrderBuildResult::Pending && turns++ < bound) {
      TEST_ASSERT_NULL(inputs.data()); result = inputs.step(quantum);
    }
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    RuntimeCompanionPasses merged; TEST_ASSERT_TRUE(merged.prewarm(32));
    TEST_ASSERT_TRUE(merged.begin(fixture.apply, fixture.work, inputs, nullptr, 0, source, 768));
    result = RuntimePlaybackOrderBuildResult::Pending;
    for (size_t i = 0; i < 1000 && result == RuntimePlaybackOrderBuildResult::Pending; ++i) result = merged.step(quantum);
    TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
    assertMidiEventVecEquivalent(MidiEventVec(reference.begin(), reference.end()), MidiEventVec(merged.data(), merged.data() + merged.size()),
                                "direct LoopPasses selection and active-row filtering");
    merged.discard(); inputs.discard();
    LoopEventStore::releaseChunkRefs(passes.recordPass.committedChunkIds);
    for (auto& pass : passes.overdubPasses) LoopEventStore::releaseChunkRefs(pass.committedChunkIds);
  }
}

void test_canonical_selection_denies_ambiguous_order_and_stale_source() {
  LoopEventStore::resetPoolForTests(); LoopEventStore::initPool();
  LoopPasses passes; passes.overdubPasses.resize(3);
  const uint32_t sequences[] = {5, 3, 5};
  for (size_t i = 0; i < 3; ++i) {
    LoopEventStore store; TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(0, 1, 60, 90)));
    TEST_ASSERT_TRUE(store.detachChunksToCommittedChunkIds(passes.overdubPasses[i].committedChunkIds));
    passes.overdubPasses[i].mergeSequence = sequences[i];
  }
  CompanionApplyFixture fixture; fixture.prewarm(4);
  RuntimeCanonicalPassInputs inputs; TEST_ASSERT_TRUE(inputs.prewarm(4));
  PlaybackViewSourceIdentity source{1, 2, &passes};
  TEST_ASSERT_TRUE(inputs.begin(fixture.apply, fixture.work, passes, source, 768));
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  for (size_t i = 0; i < 100 && result == RuntimePlaybackOrderBuildResult::Pending; ++i) result = inputs.step(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, result);
  TEST_ASSERT_NULL(inputs.data()); TEST_ASSERT_EQUAL(0, inputs.eventCount());
  TEST_ASSERT_TRUE(inputs.begin(fixture.apply, fixture.work, passes, source, 768));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, inputs.step(1));
  ++source.sourceGeneration;
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, inputs.step(1));
  TEST_ASSERT_NULL(inputs.data()); inputs.discard();
  for (auto& pass : passes.overdubPasses) LoopEventStore::releaseChunkRefs(pass.committedChunkIds);
  const EditPass* badRows[] = {nullptr};
  TEST_ASSERT_TRUE(fixture.apply.beginSelectedHistoryBatch(fixture.work, nullptr, 0, source, badRows, 1, 768));
  result = RuntimePlaybackOrderBuildResult::Pending;
  for (size_t i = 0; i < 10 && result == RuntimePlaybackOrderBuildResult::Pending; ++i) result = fixture.apply.step(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, result);
}

void test_canonical_frozen_source_invalidates_without_producer_turn() {
  LoopEventStore::resetPoolForTests(); LoopEventStore::initPool();
  LoopPasses passes; passes.recordPass.id = 1;
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(10, 1, 60, 90)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(50, 1, 60)));
  TEST_ASSERT_TRUE(store.detachChunksToCommittedChunkIds(passes.recordPass.committedChunkIds));
  CompanionApplyFixture fixture; fixture.prewarm(4);
  RuntimeCanonicalPassInputs inputs; TEST_ASSERT_TRUE(inputs.prewarm(4));
  RuntimeCompanionPasses prepared; TEST_ASSERT_TRUE(prepared.prewarm(4));
  CaptureAdmissionStaging capture;
  uint32_t revision = 1, generation = 2, epoch = 3, length = 768;
  const PlaybackViewSourceIdentity source{1, 2, &passes};
  const RuntimeCanonicalPassInputs::FrozenSource live{revision, generation, epoch, length, capture};
  for (size_t change = 0; change < 8; ++change) {
    // Interrupt every early grain as well as completed normalization and both
    // pending/completed downstream consumers. The copied source never changes.
    for (size_t pause = 0; pause < 22; ++pause) {
      revision = 1; generation = 2; epoch = 3; length = 768;
      capture.captureGeneration = capture.frozenGeneration = 4;
      capture.frozen = true; capture.storage.eventCount = 0;
      TEST_ASSERT_TRUE(inputs.beginFrozen(fixture.apply, fixture.work, passes, source, live));
      auto result = RuntimePlaybackOrderBuildResult::Pending;
      for (size_t i = 0; i < pause && result == RuntimePlaybackOrderBuildResult::Pending; ++i)
        result = inputs.step(1);
      const bool consume = inputs.ready();
      if (consume) {
        TEST_ASSERT_TRUE(prepared.begin(fixture.apply, fixture.work, inputs, nullptr, 0, source, length));
        for (size_t i = 0; i < (pause % 2 ? 100u : 1u); ++i) {
          if (prepared.step(1) == RuntimePlaybackOrderBuildResult::Complete) break;
        }
      }
      switch (change) {
        case 0: ++revision; break;
        case 1: ++generation; break;
        case 2: ++epoch; break;
        case 3: ++length; break;
        case 4: capture.frozen = false; break;
        case 5: ++capture.captureGeneration; break;
        case 6: ++capture.frozenGeneration; break;
        case 7: ++capture.storage.eventCount; break;
      }
      TEST_ASSERT_FALSE(inputs.ready()); TEST_ASSERT_NULL(inputs.data());
      TEST_ASSERT_EQUAL(0, inputs.passCount()); TEST_ASSERT_EQUAL(0, inputs.eventCount());
      if (consume) {
        TEST_ASSERT_FALSE(prepared.ready(source)); TEST_ASSERT_NULL(prepared.data());
        TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, prepared.step(1));
      }
      TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, inputs.step(1));
      prepared.discard(); inputs.discard();
    }
  }
  LoopEventStore::releaseChunkRefs(passes.recordPass.committedChunkIds);
}

void test_canonical_frozen_source_refuses_unclosed_or_mismatched_admission() {
  CompanionApplyFixture fixture; fixture.prewarm(4);
  RuntimeCanonicalPassInputs inputs; TEST_ASSERT_TRUE(inputs.prewarm(4));
  LoopPasses passes; CaptureAdmissionStaging capture;
  uint32_t revision = 1, generation = 2, epoch = 3, length = 768;
  const PlaybackViewSourceIdentity source{1, 2, &passes};
  const RuntimeCanonicalPassInputs::FrozenSource live{revision, generation, epoch, length, capture};
  TEST_ASSERT_FALSE(inputs.beginFrozen(fixture.apply, fixture.work, passes, source, live));
  capture.frozen = true; capture.captureGeneration = capture.frozenGeneration = 4;
  capture.reservations[0].open = true;
  TEST_ASSERT_FALSE(inputs.beginFrozen(fixture.apply, fixture.work, passes, source, live));
  capture.reservations[0].open = false; ++revision;
  TEST_ASSERT_FALSE(inputs.beginFrozen(fixture.apply, fixture.work, passes, source, live));
  --revision; ++generation;
  TEST_ASSERT_FALSE(inputs.beginFrozen(fixture.apply, fixture.work, passes, source, live));
  --generation; ++capture.frozenGeneration;
  TEST_ASSERT_FALSE(inputs.beginFrozen(fixture.apply, fixture.work, passes, source, live));
  --capture.frozenGeneration;
  TEST_ASSERT_TRUE(inputs.beginFrozen(fixture.apply, fixture.work, passes, source, live));
  for (size_t i = 0; i < 3; ++i) inputs.step(1);
  TEST_ASSERT_TRUE(inputs.ready());
  inputs.discard();
}

void test_frozen_companion_owner_binds_per_pass_reference_and_reuse() {
  LoopEventStore::resetPoolForTests(); LoopEventStore::initPool();
  LoopPasses passes; passes.recordPass.id = 1; passes.overdubPasses.resize(1);
  MidiEventVec passEvents{MidiEvent::NoteOn(10, 1, 60, 90), MidiEvent::NoteOff(50, 1, 60),
                          MidiEvent::NoteOn(100, 1, 62, 80), MidiEvent::NoteOff(200, 1, 62)};
  passEvents[0].noteId = 1; passEvents[2].noteId = 2;
  for (auto* refs : {&passes.recordPass.committedChunkIds, &passes.overdubPasses[0].committedChunkIds}) {
    LoopEventStore store;
    for (const auto& event : passEvents) TEST_ASSERT_TRUE(store.append(event));
    TEST_ASSERT_TRUE(store.detachChunksToCommittedChunkIds(*refs));
  }
  CompanionApplyFixture fixture; fixture.prewarm(16);
  RuntimeCanonicalPassInputs inputs; TEST_ASSERT_TRUE(inputs.prewarm(16));
  RuntimeCompanionPasses prepared; TEST_ASSERT_TRUE(prepared.prewarm(16));
  RuntimeCaptureCompanions rows; TEST_ASSERT_TRUE(rows.prewarm(4));
  CaptureAdmissionStaging capture; capture.frozen = true;
  capture.captureGeneration = capture.frozenGeneration = 4;
  uint32_t revision = 1, generation = 2, epoch = 3, length = 768;
  const PlaybackViewSourceIdentity source{1, 2, &passes};
  const RuntimeCanonicalPassInputs::FrozenSource live{revision, generation, epoch, length, capture};
  PendingNoteChangeVec changes{
    {PendingNoteChangeKind::Add, 3, 1, 64, 90, 20, 30},
    {PendingNoteChangeKind::Hide, 1, 1, 60, 90, 10, 50},
    {PendingNoteChangeKind::Shorten, 2, 1, 62, 80, 100, 150}};
  EditPassVec referenceRows;
  for (const auto& change : changes) {
    EditPass row;
    if (preparePendingNoteCompanion(change, row)) referenceRows.push_back(row);
  }
  auto expectedPass = passEvents;
  applyNoteEditPassSequence(expectedPass, referenceRows, length);
  MidiEventVec expected;
  std::merge(expectedPass.begin(), expectedPass.end(), expectedPass.begin(), expectedPass.end(),
             std::back_inserter(expected), [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
  for (size_t fault = 0; fault < 4; ++fault) {
    for (size_t stage = 0; stage < 3; ++stage) {
      TEST_ASSERT_TRUE(inputs.beginFrozen(fixture.apply, fixture.work, passes, source, live));
      for (size_t i = 0; i < 500 && !inputs.ready(); ++i) inputs.step(1);
      TEST_ASSERT_TRUE(inputs.ready());
      TEST_ASSERT_TRUE(rows.begin(capture, 4, changes));
      TEST_ASSERT_NULL(rows.data(4));
      TEST_ASSERT_FALSE(prepared.begin(fixture.apply, fixture.work, inputs, rows, source, length));
      TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, rows.step(4));
      TEST_ASSERT_TRUE(prepared.begin(fixture.apply, fixture.work, inputs, rows, source, length));
      if (stage == 1) TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, prepared.step(1));
      if (stage == 2) {
        for (size_t i = 0; i < 1000 && !prepared.ready(source); ++i) prepared.step(1);
        TEST_ASSERT_TRUE(prepared.ready(source));
        assertMidiEventVecEquivalent(expected, MidiEventVec(prepared.data(), prepared.data() + prepared.size()),
                                    "typed frozen companions apply independently to repeated IDs");
      }
      if (fault == 0) rows.discard();
      if (fault == 1) {
        const auto oldGeneration = rows.preparationGeneration();
        rows.discard();
        TEST_ASSERT_TRUE(rows.begin(capture, 4, changes));
        TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, rows.step(4));
        TEST_ASSERT_TRUE(rows.ready(capture));
        TEST_ASSERT_TRUE(rows.preparationGeneration() != oldGeneration);
      }
      if (fault == 2) changes.push_back({PendingNoteChangeKind::Add, 4, 1, 66, 90, 20, 30});
      if (fault == 3) ++epoch;
      TEST_ASSERT_FALSE(prepared.ready(source)); TEST_ASSERT_NULL(prepared.data());
      TEST_ASSERT_NULL(prepared.origins()); TEST_ASSERT_NULL(prepared.inputEvent({0, 0}));
      TEST_ASSERT_EQUAL(0, prepared.size());
      TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, prepared.step(1));
      prepared.discard(); inputs.discard(); rows.discard();
      if (fault == 2) changes.pop_back();
    }
  }
  LoopEventStore::releaseChunkRefs(passes.recordPass.committedChunkIds);
  LoopEventStore::releaseChunkRefs(passes.overdubPasses[0].committedChunkIds);
}

void test_frozen_companion_owner_rejects_other_capture_and_unbound_inputs() {
  CompanionApplyFixture fixture; fixture.prewarm(4);
  RuntimeCanonicalPassInputs inputs; TEST_ASSERT_TRUE(inputs.prewarm(4));
  RuntimeCompanionPasses prepared; TEST_ASSERT_TRUE(prepared.prewarm(4));
  RuntimeCaptureCompanions rows; TEST_ASSERT_TRUE(rows.prewarm(4));
  LoopPasses passes; CaptureAdmissionStaging capture, other;
  capture.frozen = other.frozen = true;
  capture.captureGeneration = capture.frozenGeneration = other.captureGeneration = other.frozenGeneration = 4;
  uint32_t revision = 1, generation = 2, epoch = 3, length = 768;
  const PlaybackViewSourceIdentity source{1, 2, &passes};
  const RuntimeCanonicalPassInputs::FrozenSource live{revision, generation, epoch, length, capture};
  PendingNoteChangeVec changes;
  TEST_ASSERT_TRUE(rows.begin(other, 4, changes)); rows.step(1);
  TEST_ASSERT_TRUE(inputs.beginFrozen(fixture.apply, fixture.work, passes, source, live));
  for (size_t i = 0; i < 3; ++i) inputs.step(1);
  TEST_ASSERT_TRUE(inputs.ready());
  TEST_ASSERT_FALSE(prepared.begin(fixture.apply, fixture.work, inputs, rows, source, length));
  rows.discard(); TEST_ASSERT_TRUE(rows.begin(capture, 4, changes)); rows.step(1);
  TEST_ASSERT_TRUE(prepared.begin(fixture.apply, fixture.work, inputs, rows, source, length));
  TEST_ASSERT_TRUE(prepared.ready(source)); // Empty pass/row set is still owner-bound.
  prepared.discard(); inputs.discard();
  TEST_ASSERT_TRUE(inputs.begin(fixture.apply, fixture.work, passes, source, length));
  for (size_t i = 0; i < 3; ++i) inputs.step(1);
  TEST_ASSERT_TRUE(inputs.ready()); TEST_ASSERT_NULL(inputs.frozenCapture());
  TEST_ASSERT_FALSE(prepared.begin(fixture.apply, fixture.work, inputs, rows, source, length));
  inputs.discard(); rows.discard();
}

void test_frozen_overdub_inputs_match_committed_capture_reference() {
  for (size_t sample = 0; sample < 24; ++sample) {
    LoopEventStore::resetPoolForTests(); LoopEventStore::initPool();
    LoopPasses passes; passes.recordPass.id = 1;
    auto on = MidiEvent::NoteOn(10, 1, 60, 90); on.noteId = 1;
    LoopEventStore record;
    TEST_ASSERT_TRUE(record.append(on)); TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(70, 1, 60)));
    TEST_ASSERT_TRUE(record.detachChunksToCommittedChunkIds(passes.recordPass.committedChunkIds));
    CaptureAdmissionStaging capture; TEST_ASSERT_TRUE(capture.prewarm());
    auto captureOn = MidiEvent::NoteOn(10, 1, 64, 80); captureOn.noteId = 1;
    const MidiEvent raw[] = {MidiEvent::NoteOff(800, 1, 64), captureOn,
      MidiEvent::ControlChange(10, 1, 7, 80), MidiEvent::NoteOff(10, 1, 64),
      MidiEvent::ControlChange(5, 1, 8, 81)};
    for (const auto& event : raw) TEST_ASSERT_TRUE(capture.storage.append(event));
    if (sample % 3 == 0) capture.storage.discardEvent(4);
    if (sample % 5 == 0)
      for (size_t i = 0; i < capture.storage.eventCount; ++i) capture.storage.discardEvent(i);
    capture.captureGeneration = 4; TEST_ASSERT_TRUE(capture.freeze(4));
    EditPass velocity; velocity.targetNoteId = 1; velocity.propertyType = EditPropertyType::Velocity; velocity.velocity = 99;
    passes.editPasses.push_back(velocity);
    if (sample % 2) {
      EditPass create; create.actionType = EditActionType::Create;
      create.addedEvents = {MidiEvent::ControlChange(10, 1, 9, 82)};
      passes.editPasses.push_back(create);
    }
    PendingNoteChangeVec changes;
    if (sample % 4 == 0) changes.push_back({PendingNoteChangeKind::Hide, 1, 1, 64, 80, 10, 800});
    RuntimeCaptureCompanions rows; TEST_ASSERT_TRUE(rows.prewarm(4));
    TEST_ASSERT_TRUE(rows.begin(capture, 4, changes)); rows.step(4);
    MidiEventVec canonicalCapture;
    for (size_t i = 0; i < capture.storage.eventCount; ++i)
      if (capture.storage.eventRetained(i)) canonicalCapture.push_back(raw[i]);
    std::stable_sort(canonicalCapture.begin(), canonicalCapture.end(),
                     [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
    CommittedChunkIdList capturedRefs;
    if (!canonicalCapture.empty()) {
      LoopEventStore store;
      for (const auto& event : canonicalCapture) TEST_ASSERT_TRUE(store.append(event));
      TEST_ASSERT_TRUE(store.detachChunksToCommittedChunkIds(capturedRefs));
      passes.overdubPasses.emplace_back(); passes.overdubPasses.back().mergeSequence = 1;
      TEST_ASSERT_TRUE(LoopEventStore::tryCopyCommittedChunkIds(passes.overdubPasses.back().committedChunkIds, capturedRefs));
    }
    const size_t historyCount = passes.editPasses.size();
    for (size_t i = 0; i < rows.size(); ++i) passes.editPasses.push_back(rows.row(i));
    SessionMidiEventVec expected; passes.materializeToEventVector(expected, 768);
    passes.editPasses.resize(historyCount); passes.overdubPasses.clear();
    CompanionApplyFixture fixture; fixture.prewarm(32);
    RuntimeCanonicalPassInputs inputs; TEST_ASSERT_TRUE(inputs.prewarm(32));
    RuntimeCompanionPasses prepared; TEST_ASSERT_TRUE(prepared.prewarm(32));
    uint32_t revision = 1, generation = 2, epoch = 3, length = 768;
    const PlaybackViewSourceIdentity source{1, 2, &passes};
    const RuntimeCanonicalPassInputs::FrozenSource live{revision, generation, epoch, length, capture};
    TEST_ASSERT_TRUE(inputs.beginFrozenOverdub(fixture.apply, fixture.work, passes, source, live));
    const size_t quantum = 1 + sample % 7;
    const auto bound = RuntimeCanonicalPassInputs::requiredFrozenOverdubGrainsUpperBound(32, historyCount, quantum);
    uint64_t turns = 0;
    while (!inputs.ready() && turns++ < bound) {
      // Simulate reuse of all shared audible arrays between every input grain.
      for (size_t i = 0; i < capture.storage.eventCount; ++i) {
        capture.storage.order[i] = 123; capture.storage.mergeScratch[i] = 124; capture.storage.phases[i] = 125;
      }
      TEST_ASSERT_TRUE(inputs.step(quantum) != RuntimePlaybackOrderBuildResult::Denied);
      for (size_t i = 0; i < capture.storage.eventCount; ++i) {
        TEST_ASSERT_EQUAL(123, capture.storage.order[i]); TEST_ASSERT_EQUAL(124, capture.storage.mergeScratch[i]);
        TEST_ASSERT_EQUAL(125, capture.storage.phases[i]);
      }
    }
    TEST_ASSERT_TRUE(inputs.ready());
    TEST_ASSERT_EQUAL(canonicalCapture.empty() ? 1 : 2, inputs.passCount());
    TEST_ASSERT_TRUE(prepared.begin(fixture.apply, fixture.work, inputs, rows, source, length));
    for (size_t i = 0; i < 2000 && !prepared.ready(source); ++i) prepared.step(quantum);
    TEST_ASSERT_TRUE(prepared.ready(source));
    assertMidiEventVecEquivalent(MidiEventVec(expected.begin(), expected.end()),
      MidiEventVec(prepared.data(), prepared.data() + prepared.size()), "frozen capture canonical per-pass reference");
    assertMidiEventVecEquivalent(MidiEventVec(std::begin(raw), std::end(raw)),
      MidiEventVec(capture.storage.events, capture.storage.events + capture.storage.eventCount), "capture event source unchanged");
    prepared.discard(); inputs.discard(); rows.discard();
    LoopEventStore::releaseChunkRefs(capturedRefs); LoopEventStore::releaseChunkRefs(passes.recordPass.committedChunkIds);
  }
}

void test_frozen_overdub_inputs_maximum_capacity_and_stale_order() {
  CompanionApplyFixture fixture; fixture.prewarm(2048);
  RuntimeCanonicalPassInputs inputs; TEST_ASSERT_TRUE(inputs.prewarm(2048));
  CaptureAdmissionStaging capture; TEST_ASSERT_TRUE(capture.prewarm());
  for (size_t i = 0; i < 2048; ++i)
    TEST_ASSERT_TRUE(capture.storage.append(MidiEvent::ControlChange((2047 - i) % 700, 1, 7, i % 128)));
  capture.captureGeneration = 4; TEST_ASSERT_TRUE(capture.freeze(4));
  LoopPasses passes;
  uint32_t revision = 1, generation = 2, epoch = 3, length = 768;
  const PlaybackViewSourceIdentity source{1, 2, &passes};
  const RuntimeCanonicalPassInputs::FrozenSource live{revision, generation, epoch, length, capture};
  TEST_ASSERT_TRUE(inputs.beginFrozenOverdub(fixture.apply, fixture.work, passes, source, live));
  const auto bound = RuntimeCanonicalPassInputs::requiredFrozenOverdubGrainsUpperBound(2048, 0, 32);
  uint64_t turns = 0;
  while (!inputs.ready() && turns++ < bound)
    TEST_ASSERT_TRUE(inputs.step(32) != RuntimePlaybackOrderBuildResult::Denied);
  TEST_ASSERT_TRUE(inputs.ready()); TEST_ASSERT_EQUAL(1, inputs.passCount()); TEST_ASSERT_EQUAL(2048, inputs.eventCount());
  MidiEventVec expected(capture.storage.events, capture.storage.events + capture.storage.eventCount);
  std::stable_sort(expected.begin(), expected.end(), [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
  assertMidiEventVecEquivalent(expected, MidiEventVec(inputs.data()[0].events, inputs.data()[0].events + 2048), "maximum private capture order");
  inputs.discard();
  TEST_ASSERT_TRUE(inputs.beginFrozenOverdub(fixture.apply, fixture.work, passes, source, live));
  for (size_t i = 0; i < 8; ++i) TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, inputs.step(1));
  ++epoch; TEST_ASSERT_NULL(inputs.data());
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, inputs.step(1)); inputs.discard();
  RuntimeCanonicalPassInputs small; TEST_ASSERT_TRUE(small.prewarm(4));
  TEST_ASSERT_FALSE(small.beginFrozenOverdub(fixture.apply, fixture.work, passes, source, live));
  capture.storage.eventCount = 4;
  EditPass create; create.actionType = EditActionType::Create; create.addedEvents = {MidiEvent::ControlChange(1, 1, 8, 90)};
  passes.editPasses.push_back(create);
  TEST_ASSERT_TRUE(small.beginFrozenOverdub(fixture.apply, fixture.work, passes, source, live));
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  for (size_t i = 0; i < 1000 && result == RuntimePlaybackOrderBuildResult::Pending; ++i) result = small.step(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, result); TEST_ASSERT_NULL(small.data()); small.discard();
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, RuntimeCanonicalPassInputs::requiredFrozenOverdubGrainsUpperBound(0, 0, 32));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, RuntimeCanonicalPassInputs::requiredFrozenOverdubGrainsUpperBound(2048, 0, 0));
}

void test_companion_continuation_preserves_history_membership() {
  for (size_t sample = 0; sample < 16; ++sample) {
    LoopEventStore::resetPoolForTests(); LoopEventStore::initPool();
    LoopPasses passes; passes.recordPass.id = 1;
    auto first = MidiEvent::NoteOn(10, 1, 60, 90); first.noteId = 1;
    auto duplicate = MidiEvent::NoteOn(30, 1, 62, 80); duplicate.noteId = 1;
    MidiEventVec original{first, MidiEvent::NoteOff(20, 1, 60), duplicate, MidiEvent::NoteOff(40, 1, 62)};
    LoopEventStore store;
    for (const auto& event : original) TEST_ASSERT_TRUE(store.append(event));
    TEST_ASSERT_TRUE(store.detachChunksToCommittedChunkIds(passes.recordPass.committedChunkIds));
    EditPass deletion; deletion.actionType = EditActionType::Delete; deletion.targetNoteId = 1;
    passes.editPasses.push_back(deletion);
    if (sample % 4) {
      EditPass create; create.actionType = EditActionType::Create;
      auto added = MidiEvent::NoteOn(60, 1, 64, sample % 4 == 2 ? 0 : 70);
      added.noteId = sample % 4 == 3 ? 2 : 1;
      create.addedEvents = {added, MidiEvent::NoteOff(80, 1, 64)};
      passes.editPasses.push_back(create);
    }
    if (sample % 8 >= 4) passes.editPasses.push_back(deletion);
    EditPass companion = deletion;
    if (sample >= 8) {
      companion.actionType = EditActionType::Update; companion.propertyType = EditPropertyType::Length;
      companion.endTick = 35;
    }
    EditPassVec combined = passes.editPasses; combined.push_back(companion);
    auto expected = original; applyNoteEditPassSequence(expected, combined, 768);
    CompanionApplyFixture fixture; fixture.prewarm(16);
    RuntimeCanonicalPassInputs inputs; TEST_ASSERT_TRUE(inputs.prewarm(16));
    RuntimeCompanionPasses prepared; TEST_ASSERT_TRUE(prepared.prewarm(16));
    PlaybackViewSourceIdentity source{1, 2, &passes};
    RuntimeCanonicalPassInputs::SealedPass descriptor{passes.recordPass.committedChunkIds.data(), passes.recordPass.committedChunkIds.size()};
    if (sample % 2)
      TEST_ASSERT_TRUE(inputs.begin(fixture.apply, fixture.work, passes, source, 768));
    else
      TEST_ASSERT_TRUE(inputs.begin(fixture.apply, fixture.work, &descriptor, 1,
                                    passes.editPasses.data(), passes.editPasses.size(), source, 768));
    for (size_t i = 0; i < 2000 && !inputs.ready(); ++i) inputs.step(1);
    TEST_ASSERT_TRUE(inputs.ready());
    TEST_ASSERT_TRUE(prepared.begin(fixture.apply, fixture.work, inputs, &companion, 1, source, 768));
    for (size_t i = 0; i < 2000 && !prepared.ready(source); ++i) prepared.step(1);
    TEST_ASSERT_TRUE(prepared.ready(source));
    assertMidiEventVecEquivalent(expected, MidiEventVec(prepared.data(), prepared.data() + prepared.size()),
                                 "history membership survives the companion handoff");
    prepared.discard(); inputs.discard(); LoopEventStore::releaseChunkRefs(passes.recordPass.committedChunkIds);
  }
}

void test_companion_history_prefix_quantum_and_capacity_guards() {
  CompanionApplyFixture fixture; fixture.prewarm(2048);
  EditPassVec history(2048);
  for (auto& row : history) { row.actionType = EditActionType::Update; row.propertyType = EditPropertyType::None; }
  history.front().actionType = EditActionType::Delete; history.front().targetNoteId = 1;
  auto& create = history[1]; create.actionType = EditActionType::Create;
  for (size_t i = 0; i < 2047; ++i) create.addedEvents.push_back(MidiEvent::ControlChange(i, 1, 7, 90));
  auto on = MidiEvent::NoteOn(2047, 1, 60, 90); on.noteId = 1; create.addedEvents.push_back(on);
  EditPass companion; companion.actionType = EditActionType::Delete; companion.targetNoteId = 1;
  PlaybackViewSourceIdentity source{1, 2, &history};
  RuntimeCompanionApply::HistoryPrefix prefix{history.data(), nullptr, history.size()};
  TEST_ASSERT_TRUE(fixture.apply.beginContinuationBatch(fixture.work, create.addedEvents.data(), 2048,
      source, &companion, 1, 4096, prefix));
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  uint64_t turns = 0;
  const auto bound = RuntimeCompanionApply::requiredContinuationGrainsUpperBound(2048, 2048, 1, 1);
  while (result == RuntimePlaybackOrderBuildResult::Pending && turns++ < bound) result = fixture.apply.step(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  TEST_ASSERT_EQUAL(2047, fixture.apply.size());
  TEST_ASSERT_TRUE(turns >= 4096); // Neither prefix rows nor Create payload are an uncharged scan.
  fixture.apply.discard();
  const EditPass* bad[] = {nullptr};
  TEST_ASSERT_TRUE(fixture.apply.beginContinuationBatch(fixture.work, &on, 1, source, &companion, 1, 4096,
      {nullptr, bad, 1}));
  result = RuntimePlaybackOrderBuildResult::Pending;
  for (size_t i = 0; i < 10 && result == RuntimePlaybackOrderBuildResult::Pending; ++i) result = fixture.apply.step(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, result); TEST_ASSERT_NULL(fixture.apply.data());
  TEST_ASSERT_FALSE(fixture.apply.beginContinuationBatch(fixture.work, &on, 1, source, &companion, 1, 4096,
      {history.data(), bad, 1}));
  TEST_ASSERT_FALSE(fixture.apply.beginContinuationBatch(fixture.work, &on, 1, source, &companion, 1, 4096,
      {history.data(), nullptr, 2049}));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, RuntimeCompanionApply::requiredContinuationGrainsUpperBound(2048, 2049, 1, 32));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, RuntimeCompanionApply::requiredContinuationGrainsUpperBound(2048, 1, 1, 0));
}

void test_frozen_tail_reuse_guards_and_invalidates_old_consumers() {
  LoopEventStore::resetPoolForTests(); LoopEventStore::initPool();
  LoopPasses passes; passes.recordPass.id = 1;
  LoopEventStore store; TEST_ASSERT_TRUE(store.append(MidiEvent::ControlChange(10, 1, 7, 90)));
  TEST_ASSERT_TRUE(store.detachChunksToCommittedChunkIds(passes.recordPass.committedChunkIds));
  CompanionApplyFixture fixture; fixture.prewarm(4);
  RuntimeCanonicalPassInputs inputs; TEST_ASSERT_TRUE(inputs.prewarm(4));
  RuntimeCompanionPasses consumer; TEST_ASSERT_TRUE(consumer.prewarm(4));
  PlaybackViewSourceIdentity source{1, 2, &passes};
  CaptureAdmissionStaging capture; TEST_ASSERT_TRUE(capture.prewarm());
  TEST_ASSERT_TRUE(capture.storage.append(MidiEvent::ControlChange(20, 1, 8, 80)));
  uint32_t revision = 1, generation = 2, epoch = 3, length = 768;
  const RuntimeCanonicalPassInputs::FrozenSource live{revision, generation, epoch, length, capture};
  TEST_ASSERT_FALSE(inputs.appendFrozenOverdub(live));
  TEST_ASSERT_TRUE(inputs.begin(fixture.apply, fixture.work, passes, source, length));
  TEST_ASSERT_FALSE(inputs.appendFrozenOverdub(live));
  for (size_t i = 0; i < 100 && !inputs.ready(); ++i) inputs.step(1);
  TEST_ASSERT_TRUE(inputs.ready());
  const auto prefixGeneration = inputs.generation();
  TEST_ASSERT_FALSE(inputs.appendFrozenOverdub(live)); // Not frozen.
  capture.captureGeneration = 4; TEST_ASSERT_TRUE(capture.freeze(4));
  ++revision; TEST_ASSERT_FALSE(inputs.appendFrozenOverdub(live)); --revision;
  ++generation; TEST_ASSERT_FALSE(inputs.appendFrozenOverdub(live)); --generation;
  ++length; TEST_ASSERT_FALSE(inputs.appendFrozenOverdub(live)); --length;
  capture.reservations[0].open = true; TEST_ASSERT_FALSE(inputs.appendFrozenOverdub(live)); capture.reservations[0].open = false;
  TEST_ASSERT_EQUAL_UINT64(prefixGeneration, inputs.generation()); TEST_ASSERT_TRUE(inputs.ready());
  EditPass guard; guard.targetNoteId = 1; guard.propertyType = EditPropertyType::Length; guard.endTick = 50;
  TEST_ASSERT_TRUE(consumer.begin(fixture.apply, fixture.work, inputs, &guard, 1, source, length));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, consumer.step(1));
  TEST_ASSERT_FALSE(inputs.appendFrozenOverdub(live)); // Shared apply worker is borrowed.
  TEST_ASSERT_EQUAL_UINT64(prefixGeneration, inputs.generation()); consumer.discard();
  TEST_ASSERT_TRUE(consumer.begin(fixture.apply, fixture.work, inputs, nullptr, 0, source, length));
  for (size_t i = 0; i < 100 && !consumer.ready(source); ++i) consumer.step(1);
  TEST_ASSERT_TRUE(consumer.ready(source));
  TEST_ASSERT_TRUE(inputs.appendFrozenOverdub(live));
  TEST_ASSERT_FALSE(consumer.ready(source)); TEST_ASSERT_NULL(consumer.data());
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, consumer.step(1));
  TEST_ASSERT_FALSE(inputs.appendFrozenOverdub(live));
  const auto bound = RuntimeCanonicalPassInputs::requiredFrozenTailGrainsUpperBound(4, 0, 1);
  uint64_t turns = 0;
  while (!inputs.ready() && turns++ < bound) TEST_ASSERT_TRUE(inputs.step(1) != RuntimePlaybackOrderBuildResult::Denied);
  TEST_ASSERT_TRUE(inputs.ready()); TEST_ASSERT_EQUAL(2, inputs.passCount()); TEST_ASSERT_EQUAL(2, inputs.eventCount());
  TEST_ASSERT_EQUAL(10, inputs.data()[0].events[0].tick); TEST_ASSERT_EQUAL(20, inputs.data()[1].events[0].tick);
  TEST_ASSERT_FALSE(inputs.appendFrozenOverdub(live)); // Never append the same capture twice.
  inputs.discard(); consumer.discard();
  TEST_ASSERT_TRUE(inputs.begin(fixture.apply, fixture.work, passes, source, length));
  for (size_t i = 0; i < 100 && !inputs.ready(); ++i) inputs.step(1);
  capture.reset(); capture.captureGeneration = 5;
  for (size_t i = 0; i < 4; ++i) TEST_ASSERT_TRUE(capture.storage.append(MidiEvent::ControlChange(30+i, 1, 8, 80)));
  TEST_ASSERT_TRUE(capture.freeze(5));
  TEST_ASSERT_TRUE(inputs.appendFrozenOverdub(live));
  auto overflow = RuntimePlaybackOrderBuildResult::Pending;
  for (size_t i = 0; i < 200 && overflow == RuntimePlaybackOrderBuildResult::Pending; ++i) overflow = inputs.step(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, overflow); TEST_ASSERT_NULL(inputs.data());
  inputs.discard();
  TEST_ASSERT_TRUE(inputs.begin(fixture.apply, fixture.work, passes, source, length));
  for (size_t i = 0; i < 100 && !inputs.ready(); ++i) inputs.step(1);
  ++source.playbackRevision;
  TEST_ASSERT_FALSE(inputs.appendFrozenOverdub(live)); TEST_ASSERT_NULL(inputs.data());
  inputs.discard(); LoopEventStore::releaseChunkRefs(passes.recordPass.committedChunkIds);
  TEST_ASSERT_TRUE(RuntimeCanonicalPassInputs::requiredFrozenTailGrainsUpperBound(2048, 2048, 32) <
                   RuntimeCanonicalPassInputs::requiredFrozenOverdubGrainsUpperBound(2048, 2048, 32));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, RuntimeCanonicalPassInputs::requiredFrozenTailGrainsUpperBound(0, 0, 32));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, RuntimeCanonicalPassInputs::requiredFrozenTailGrainsUpperBound(4, 2049, 32));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, RuntimeCanonicalPassInputs::requiredFrozenTailGrainsUpperBound(4, 0, 0));
}

void test_empty_row_paths_do_not_borrow_or_copy_through_apply() {
  LoopEventStore::resetPoolForTests(); LoopEventStore::initPool();
  MidiEventVec events;
  for (size_t i = 0; i < 4; ++i) events.push_back(MidiEvent::ControlChange(i*10, 1, 7, 80+i));
  LoopPasses passes; passes.recordPass.id = 1; LoopEventStore store;
  for (const auto& event : events) TEST_ASSERT_TRUE(store.append(event));
  TEST_ASSERT_TRUE(store.detachChunksToCommittedChunkIds(passes.recordPass.committedChunkIds));
  CompanionApplyFixture fixture; fixture.prewarm(4);
  RuntimeCanonicalPassInputs inputs; TEST_ASSERT_TRUE(inputs.prewarm(4));
  RuntimeCompanionPasses prepared; TEST_ASSERT_TRUE(prepared.prewarm(4));
  RuntimeCompanionPasses::Pass descriptors[]{{events.data(), 2}, {events.data()+2, 2}};
  PlaybackViewSourceIdentity source{1, 2, &passes};
  TEST_ASSERT_TRUE(inputs.begin(fixture.apply, fixture.work, passes, source, 768));
  TEST_ASSERT_TRUE(prepared.begin(fixture.apply, fixture.work, descriptors, 2, nullptr, 0, source, 768));
  // A separate owner starts work after admission. Neither no-edit path needs it.
  TEST_ASSERT_TRUE(fixture.apply.beginBatch(fixture.work, events.data(), events.size(), source, nullptr, 0, 768));
  const auto borrowedGeneration = fixture.apply.generation();
  const auto bound = RuntimeCompanionPasses::requiredGrainsUpperBound(4, 2, 2048, 0, 1);
  size_t turns = 0;
  while (!prepared.ready(source) && turns++ < bound) {
    TEST_ASSERT_TRUE(prepared.step(1) != RuntimePlaybackOrderBuildResult::Denied);
    TEST_ASSERT_EQUAL_UINT64(borrowedGeneration, fixture.apply.generation());
    TEST_ASSERT_FALSE(fixture.apply.available());
  }
  TEST_ASSERT_TRUE(prepared.ready(source));
  for (size_t i = 0; i < 100 && !inputs.ready(); ++i) {
    TEST_ASSERT_TRUE(inputs.step(1) != RuntimePlaybackOrderBuildResult::Denied);
    TEST_ASSERT_EQUAL_UINT64(borrowedGeneration, fixture.apply.generation());
  }
  TEST_ASSERT_TRUE(inputs.ready());
  assertMidiEventVecEquivalent(events, MidiEventVec(prepared.data(), prepared.data()+prepared.size()), "direct no-companion merge");
  assertMidiEventVecEquivalent(events, MidiEventVec(inputs.data()[0].events, inputs.data()[0].events+4), "direct no-history copy");
  for (size_t i = 0; i < 4; ++i) {
    TEST_ASSERT_EQUAL(i/2, prepared.origins()[i].pass); TEST_ASSERT_EQUAL(i%2, prepared.origins()[i].event);
  }
  prepared.discard(); inputs.discard();
  TEST_ASSERT_EQUAL_UINT64(borrowedGeneration, fixture.apply.generation());
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, fixture.apply.step(4));
  TEST_ASSERT_EQUAL_UINT64(4, RuntimeCompanionApply::requiredHistoryGrainsUpperBound(4, 0, 1));
  TEST_ASSERT_EQUAL_UINT64(4, RuntimeCompanionApply::requiredContinuationGrainsUpperBound(4, 2048, 0, 1));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, RuntimeCompanionPasses::requiredGrainsUpperBound(4, 2049, 0, 0, 1));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, RuntimeCompanionPasses::requiredGrainsUpperBound(4, 1, 0, 0, 0));
  LoopEventStore::releaseChunkRefs(passes.recordPass.committedChunkIds);
}

void test_canonical_selection_maximum_metadata_horizons() {
  LoopEventStore::resetPoolForTests(); LoopEventStore::initPool();
  LoopEventStore store; TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(10, 1, 60, 90)));
  CommittedChunkIdList original; TEST_ASSERT_TRUE(store.detachChunksToCommittedChunkIds(original));
  LoopPasses passes; passes.overdubPasses.resize(2048);
  // These lists borrow the one reference retained by original. Copying IDs
  // does not increment the pool refcount; only original releases the chunk.
  for (size_t i = 0; i < 2048; ++i) {
    auto& pass = passes.overdubPasses[i]; pass.mergeSequence = 2047 - i;
    TEST_ASSERT_TRUE(LoopEventStore::tryCopyCommittedChunkIds(pass.committedChunkIds, original));
  }
  CompanionApplyFixture fixture; fixture.prewarm(2048);
  RuntimeCanonicalPassInputs inputs; TEST_ASSERT_TRUE(inputs.prewarm(2048));
  PlaybackViewSourceIdentity source{1, 2, &passes};
  TEST_ASSERT_TRUE(inputs.begin(fixture.apply, fixture.work, passes, source, 768));
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  uint64_t turns = 0;
  const auto bound = RuntimeCanonicalPassInputs::requiredSelectedGrainsUpperBound(2048, 0, 32);
  while (result == RuntimePlaybackOrderBuildResult::Pending && turns++ < bound) result = inputs.step(32);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  TEST_ASSERT_EQUAL(2048, inputs.passCount()); TEST_ASSERT_EQUAL(2048, inputs.eventCount());
  inputs.discard();
  passes.overdubPasses.clear();
  passes.recordPass.id = 1;
  TEST_ASSERT_TRUE(LoopEventStore::tryCopyCommittedChunkIds(passes.recordPass.committedChunkIds, original));
  passes.editPasses.resize(2048);
  for (auto& row : passes.editPasses) row.state = EditPassState::Disabled;
  auto& create = passes.editPasses.back(); create.state = EditPassState::Active; create.actionType = EditActionType::Create;
  create.addedEvents = {MidiEvent::NoteOff(50, 1, 60)};
  TEST_ASSERT_TRUE(inputs.begin(fixture.apply, fixture.work, passes, source, 768));
  result = RuntimePlaybackOrderBuildResult::Pending;
  for (size_t i = 0; i < 5000 && result == RuntimePlaybackOrderBuildResult::Pending; ++i) result = inputs.step(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  TEST_ASSERT_EQUAL(1, inputs.passCount()); TEST_ASSERT_EQUAL(2, inputs.eventCount());
  inputs.discard();
  passes.editPasses.emplace_back();
  TEST_ASSERT_FALSE(inputs.begin(fixture.apply, fixture.work, passes, source, 768));
  LoopEventStore::releaseChunkRefs(original);
}

void test_bounded_history_wrap_updates_and_unpaired_move() {
  for (const auto property : {EditPropertyType::Pitch, EditPropertyType::NoteRange}) {
    for (bool withOffs : {false, true}) {
      auto on = MidiEvent::NoteOn(2592, 1, 12, 100); on.noteId = 269;
      MidiEventVec events{on};
      if (withOffs) events = {MidiEvent::NoteOff(96, 1, 12), on, MidiEvent::NoteOff(2688, 1, 12)};
      EditPass row; row.targetNoteId = 269; row.propertyType = property;
      row.pitch = 65; row.startTick = 2832; row.endTick = 3408;
      auto expected = events;
      applyNoteEditPassSequence(expected, EditPassVec{row}, 3072);
      CompanionApplyFixture fixture; fixture.prewarm(4);
      PlaybackViewSourceIdentity source{1, 2, events.data()};
      TEST_ASSERT_TRUE(fixture.apply.beginHistoryBatch(fixture.work, events.data(), events.size(), source, &row, 1, 3072));
      auto result = RuntimePlaybackOrderBuildResult::Pending;
      for (size_t turns = 0; turns < 500 && result == RuntimePlaybackOrderBuildResult::Pending; ++turns)
        result = fixture.apply.step(1);
      TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
      assertMidiEventVecEquivalent(expected, MidiEventVec(fixture.apply.data(), fixture.apply.data() + fixture.apply.size()),
                                  "history wrap Off selection and unpaired synthesis");
      if (!withOffs && property == EditPropertyType::NoteRange)
        TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, fixture.apply.provenance()[1]);
    }
  }
}

void test_bounded_history_collapsing_capacity_and_stale_guards() {
  MidiEventVec events{MidiEvent::NoteOn(100, 1, 60, 90)};
  events[0].noteId = 1;
  EditPassVec rows(2048);
  for (auto& row : rows) {
    row.targetNoteId = 1; row.actionType = EditActionType::Update;
    row.propertyType = EditPropertyType::Velocity; row.velocity = 0;
  }
  rows.back().velocity = 99;
  CompanionApplyFixture fixture; fixture.prewarm(1);
  PlaybackViewSourceIdentity source{1, 2, events.data()};
  TEST_ASSERT_TRUE(fixture.apply.beginHistoryBatch(fixture.work, events.data(), events.size(),
      source, rows.data(), rows.size(), 768));
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  size_t turns = 0;
  while (result == RuntimePlaybackOrderBuildResult::Pending && turns++ < 5000)
    result = fixture.apply.step(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  TEST_ASSERT_EQUAL(99, fixture.apply.data()[0].data.noteData.velocity);
  TEST_ASSERT_EQUAL(0, fixture.apply.provenance()[0]);
  RuntimeCompanionBridgeMap mapping;
  TEST_ASSERT_TRUE(mapping.prewarm(4));
  // The companion mapper compares surviving ticks only, so history output
  // must first become the effective baseline, never a companion delta.
  TEST_ASSERT_FALSE(mapping.begin(fixture.apply, events.data(), events.size(), source));
  // Unpaired move needs an Off slot; exhaustion must not expose the moved On.
  rows.resize(1); rows[0].propertyType = EditPropertyType::NoteRange;
  rows[0].startTick = 200; rows[0].endTick = 300;
  TEST_ASSERT_TRUE(fixture.apply.beginHistoryBatch(fixture.work, events.data(), events.size(),
      source, rows.data(), rows.size(), 768));
  result = RuntimePlaybackOrderBuildResult::Pending;
  for (size_t i = 0; i < 100 && result == RuntimePlaybackOrderBuildResult::Pending; ++i)
    result = fixture.apply.step(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, result);
  TEST_ASSERT_NULL(fixture.apply.data());
  TEST_ASSERT_EQUAL(100, events[0].tick);
  rows[0].propertyType = EditPropertyType::Velocity;
  TEST_ASSERT_TRUE(fixture.apply.beginHistoryBatch(fixture.work, events.data(), events.size(),
      source, rows.data(), rows.size(), 768));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, fixture.apply.step(1));
  ++source.sourceGeneration;
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, fixture.apply.step(1));
  TEST_ASSERT_NULL(fixture.apply.data());
  // Input rows must have been filtered to active Note history.
  rows[0].actionType = EditActionType::Create;
  rows[0].state = EditPassState::Disabled;
  TEST_ASSERT_TRUE(fixture.apply.beginHistoryBatch(fixture.work, events.data(), events.size(),
      source, rows.data(), rows.size(), 768));
  result = RuntimePlaybackOrderBuildResult::Pending;
  for (size_t i = 0; i < 100 && result == RuntimePlaybackOrderBuildResult::Pending; ++i)
    result = fixture.apply.step(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, result);
  TEST_ASSERT_NULL(fixture.apply.data());
}

void test_bounded_companion_batch_denial_keeps_prior_rows_unpublished() {
  MidiEventVec events{MidiEvent::NoteOn(100, 1, 60, 90), MidiEvent::NoteOff(200, 1, 60)};
  events[0].noteId = 1;
  EditPass rows[2];
  rows[0].passType = rows[1].passType = EditPassType::Note;
  rows[0].targetNoteId = 1; rows[0].actionType = EditActionType::Delete;
  rows[1].actionType = EditActionType::Update; rows[1].propertyType = EditPropertyType::Pitch;
  CompanionApplyFixture fixture; fixture.prewarm(4);
  PlaybackViewSourceIdentity source{1, 2, events.data()};
  TEST_ASSERT_TRUE(fixture.apply.beginBatch(fixture.work, events.data(), events.size(), source, rows, 2, 768));
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  for (size_t turns = 0; turns < 300 && result == RuntimePlaybackOrderBuildResult::Pending; ++turns)
    result = fixture.apply.step(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, result);
  TEST_ASSERT_NULL(fixture.apply.data());
  TEST_ASSERT_EQUAL_UINT(0, fixture.apply.size());
  TEST_ASSERT_EQUAL_UINT(2, events.size());
  TEST_ASSERT_TRUE(fixture.apply.beginBatch(fixture.work, events.data(), events.size(), source, nullptr, 0, 768));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, fixture.apply.step(4));
  TEST_ASSERT_EQUAL_UINT(2, fixture.apply.size());
}

void test_bounded_companion_apply_maximum_candidate_order_and_helper_exclusion() {
  MidiEventVec events;
  for (int i = 1023; i >= 0; --i) {
    auto on = MidiEvent::NoteOn(2 * i, 1, 60, 90); on.noteId = i + 1;
    auto off = MidiEvent::NoteOff(2 * i + 1, 1, 60); off.noteId = i + 1;
    events.push_back(on); events.push_back(off);
  }
  assertBoundedCompanionApply(events, RuntimeCompanionApply::Type::Length, 1, 3, 3072, 32);
  assertBoundedCompanionApply(events, RuntimeCompanionApply::Type::Delete, 1, 0, 3072, 1);
  CompanionApplyFixture fixture; fixture.prewarm(2048);
  PlaybackViewSourceIdentity source{1, 1, events.data()};
  TEST_ASSERT_TRUE(fixture.spans.begin(fixture.pair, events.data(), events.size(), 60, 3072));
  TEST_ASSERT_FALSE(fixture.apply.begin(fixture.work, events.data(), events.size(), source,
                                      RuntimeCompanionApply::Type::Length, 1, 3, 3072));
  TEST_ASSERT_EQUAL(RuntimeCompanionSpanResult::Pending, fixture.spans.step(1));
  fixture.spans.discard();
}

// Characterization, not a new semantic oracle: these existing paths disagree.
void assertPerPassPreparation(const std::vector<MidiEventVec>& inputs, const EditPassVec& rows,
                              size_t quantum, size_t capacity = 64) {
  MidiEventVec expected;
  for (const auto& input : inputs) {
    auto layer = input;
    applyNoteEditPassSequence(layer, rows, 768);
    MidiEventVec merged;
    std::merge(expected.begin(), expected.end(), layer.begin(), layer.end(), std::back_inserter(merged),
               [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
    expected = std::move(merged);
  }
  const auto frozen = inputs;
  CompanionApplyFixture fixture; fixture.prewarm(capacity);
  RuntimeCompanionPasses preparation; TEST_ASSERT_TRUE(preparation.prewarm(capacity));
  std::vector<RuntimeCompanionPasses::Pass> passes;
  for (const auto& input : inputs) passes.push_back({input.data(), input.size()});
  PlaybackViewSourceIdentity source{1, 3, inputs.data()};
  TEST_ASSERT_TRUE(preparation.begin(fixture.apply, fixture.work, passes.data(), passes.size(),
                                     rows.data(), rows.size(), source, 768));
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  const size_t bound = 1 + passes.size() * ((rows.size()+1) *
      (RuntimeCompanionApply::requiredGrainsUpperBound(capacity, quantum) + (rows.size()+quantum-1)/quantum + 2) + (capacity+quantum-1)/quantum + 4);
  for (size_t turn = 0; turn < bound && result == RuntimePlaybackOrderBuildResult::Pending; ++turn) {
    if (!passes.empty()) { TEST_ASSERT_NULL(preparation.data()); TEST_ASSERT_NULL(preparation.origins()); }
    result = preparation.step(quantum);
  }
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  TEST_ASSERT_TRUE(preparation.ready(source));
  assertMidiEventVecEquivalent(expected, MidiEventVec(preparation.data(), preparation.data()+preparation.size()),
                               "DEC-060 per-pass preparation vs canonical merge");
  for (size_t i = 0; i < preparation.size(); ++i) {
    const auto origin = preparation.origins()[i];
    TEST_ASSERT_TRUE(origin.pass < inputs.size());
    if (origin.event == UINT16_MAX) {
      TEST_ASSERT_TRUE(preparation.data()[i].isNoteOff());
      TEST_ASSERT_EQUAL_UINT32(kInvalidNoteId, preparation.data()[i].noteId);
    } else {
      TEST_ASSERT_TRUE(origin.event < inputs[origin.pass].size());
      auto original = inputs[origin.pass][origin.event]; original.tick = preparation.data()[i].tick;
      assertMidiEventVecEquivalent(MidiEventVec{original}, MidiEventVec{preparation.data()[i]}, "pass/event origin");
    }
  }
  for (size_t i = 0; i < inputs.size(); ++i)
    assertMidiEventVecEquivalent(frozen[i], inputs[i], "immutable pass input");
}

void test_per_pass_companions_preserve_scope_and_repeated_identity() {
  auto first = MidiEvent::NoteOn(0, 1, 60, 100); first.noteId = 1;
  auto captured = MidiEvent::NoteOn(150, 1, 60, 100); captured.noteId = 2;
  std::vector<MidiEventVec> passes{{first, MidiEvent::NoteOff(10, 1, 60)},
                                 {captured, MidiEvent::NoteOff(160, 1, 60)}};
  assertPerPassPreparation(passes, EditPassVec{makeLengthRow(1, 0, 10, 250)}, 1);
  passes[1][0].noteId = 1;
  assertPerPassPreparation(passes, EditPassVec{makeDeleteRow(1)}, 1);
  assertPerPassPreparation({}, {}, 1);
  assertPerPassPreparation({{}, {}, {}}, {}, 1);
  MidiEventVec repeated{first, MidiEvent::NoteOff(10, 1, 60), first, MidiEvent::NoteOff(40, 1, 60)};
  repeated[2].tick = 30;
  assertPerPassPreparation({repeated, repeated}, EditPassVec{makeDeleteRow(1), makeDeleteRow(1)}, 1);
  assertPerPassPreparation({MidiEventVec{first}}, EditPassVec{makeLengthRow(1, 0, 0, 250)}, 1);

}

void test_per_pass_companions_match_generated_canonical_merges() {
  std::mt19937 rng(0x70617373);
  for (size_t test = 0; test < 220; ++test) {
    std::vector<MidiEventVec> passes(1 + rng()%4);
    for (auto& pass : passes) {
      for (size_t i = 0, count = rng()%10; i < count; ++i) {
        auto event = i%3 == 0 ? MidiEvent::ControlChange((rng()%17)*48, 1, 7, rng()%128)
          : i%2 ? MidiEvent::NoteOn((rng()%17)*48, 1+rng()%2, 60+rng()%3, 90)
                : MidiEvent::NoteOff((rng()%17)*48, 1+rng()%2, 60+rng()%3);
        if (event.isNoteOn()) event.noteId = 1+rng()%4;
        pass.push_back(event);
      }
      std::stable_sort(pass.begin(), pass.end(), [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
    }
    EditPassVec rows;
    for (size_t i = 0; i < 4; ++i) rows.push_back(rng()%3 ? makeLengthRow(1+rng()%4, 0, 0, (rng()%18)*48)
                                                        : makeDeleteRow(1+rng()%4));
    assertPerPassPreparation(passes, rows, 1+test%7);
  }
}

void test_per_pass_companions_maximum_aggregate_and_empty_pass_count() {
  std::vector<MidiEventVec> passes(2);
  for (size_t i = 0; i < 1024; ++i) {
    passes[0].push_back(MidiEvent::ControlChange(i * 2, 1, 7, i%128));
    passes[1].push_back(MidiEvent::ControlChange(i * 2 + 1, 2, 11, i%128));
  }
  assertPerPassPreparation(passes, {}, 1, 2048);
  assertPerPassPreparation(std::vector<MidiEventVec>(2048), {}, 32, 1);
}

void test_per_pass_companions_hide_overflow_and_stale_or_reused_work() {
  MidiEventVec input{MidiEvent::NoteOn(0, 1, 60, 90)}; input[0].noteId = 1;
  RuntimeCompanionPasses::Pass passes[]{{input.data(), input.size()}, {input.data(), input.size()}};
  PlaybackViewSourceIdentity source{1, 2, input.data()};
  CompanionApplyFixture fixture; fixture.prewarm(4);
  RuntimeCompanionPasses preparation; TEST_ASSERT_TRUE(preparation.prewarm(1));
  TEST_ASSERT_TRUE(preparation.begin(fixture.apply, fixture.work, passes, 2, nullptr, 0, source, 768));
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  for (size_t i = 0; i < 100 && result == RuntimePlaybackOrderBuildResult::Pending; ++i) result = preparation.step(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, result);
  TEST_ASSERT_NULL(preparation.data()); TEST_ASSERT_EQUAL_UINT32(0, preparation.size());
  TEST_ASSERT_TRUE(preparation.begin(fixture.apply, fixture.work, passes, 1, nullptr, 0, source, 768));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, preparation.step(1));
  ++source.sourceGeneration;
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, preparation.step(1));
  TEST_ASSERT_TRUE(fixture.apply.available());
  EditPass guard; guard.targetNoteId = 1; guard.propertyType = EditPropertyType::Length; guard.endTick = 50;
  TEST_ASSERT_TRUE(preparation.begin(fixture.apply, fixture.work, passes, 1, &guard, 1, source, 768));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, preparation.step(1));
  fixture.apply.discard();
  TEST_ASSERT_TRUE(fixture.apply.beginBatch(fixture.work, input.data(), input.size(), source, nullptr, 0, 768));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, preparation.step(1));
  TEST_ASSERT_FALSE(fixture.apply.available()); // Denial must not cancel another owner's work.
  fixture.apply.discard(); preparation.discard();
  TEST_ASSERT_NULL(preparation.origins());
}

void test_companion_pass_scope_differs_from_merged_capture_scope() {
  LoopEventStore::resetPoolForTests(); LoopEventStore::initPool();
  LoopPasses passes;
  passes.recordPass = makeRecordPassWithNote(1, 0); // NoteId 1: [0,10].
  LoopEventStore capture;
  TEST_ASSERT_TRUE(storeAppendNoteOn(capture, 150, 1, 60, 100, 2));
  TEST_ASSERT_TRUE(capture.append(MidiEvent::NoteOff(160, 1, 60)));
  OverdubPass overdub{}; overdub.id = 2; overdub.mergeSequence = 1;
  overdub.state = CapturePassState::Active;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(capture, overdub.committedChunkIds));
  passes.overdubPasses.push_back(std::move(overdub));
  pushEditPassRow(passes, 1, makeLengthRow(1, 0, 10, 250));

  MidiEventVec canonical;
  SessionMidiEventVec canonicalSession;
  passes.materializeToEventVector(canonical, 768);
  passes.materializeToEventVector(canonicalSession, 768);
  TEST_ASSERT_EQUAL_UINT32(4, canonical.size());
  TEST_ASSERT_EQUAL_UINT32(4, canonicalSession.size());
  TEST_ASSERT_TRUE(findNoteOnById(canonical, 2) >= 0);
  TEST_ASSERT_EQUAL_UINT32(250, canonical.back().tick);

  // Reproduce tryAdmitLastOverdubSealIntoMergedEvents's append-before-edit
  // sequence. All four rows are distinct, so its duplicate filter removes none.
  SessionMidiEventVec merged;
  LoopEventStore::appendChunkRefEvents(passes.recordPass.committedChunkIds, merged);
  LoopEventStore::appendChunkRefEvents(passes.overdubPasses[0].committedChunkIds, merged);
  applyNoteEditPassSequence(merged, passes.editPasses, 768);
  TEST_ASSERT_EQUAL_UINT32(2, merged.size());
  TEST_ASSERT_EQUAL_UINT32(1, merged[0].noteId);
  TEST_ASSERT_EQUAL_UINT32(250, merged[1].tick);
  printf("Companion scope reproducer: per-pass=4 events (capture preserved), merged=2 events (capture deleted)\n");

  // Repeated NoteIds also disagree without any Length/overlap geometry:
  // per-pass Delete visits both passes; flattened Delete targets the first On.
  LoopPasses repeated;
  repeated.recordPass = makeRecordPassWithNote(3, 0);
  repeated.overdubPasses.push_back(makeOverdubPassWithNote(4, 150, 60));
  pushEditPassRow(repeated, 2, makeDeleteRow(1));
  SessionMidiEventVec perPassDelete, mergedDelete;
  repeated.materializeToEventVector(perPassDelete, 768);
  TEST_ASSERT_TRUE(perPassDelete.empty());
  LoopEventStore::appendChunkRefEvents(repeated.recordPass.committedChunkIds, mergedDelete);
  LoopEventStore::appendChunkRefEvents(repeated.overdubPasses[0].committedChunkIds, mergedDelete);
  applyNoteEditPassSequence(mergedDelete, repeated.editPasses, 768);
  TEST_ASSERT_EQUAL_UINT32(2, mergedDelete.size());
  TEST_ASSERT_EQUAL_UINT32(1, mergedDelete[0].noteId);
  TEST_ASSERT_EQUAL_UINT32(150, mergedDelete[0].tick);
}

void test_companion_bridge_map_preserves_batch_order_separately_from_provenance() {
  MidiEventVec base{MidiEvent::NoteOn(0, 1, 60, 90), MidiEvent::NoteOff(300, 1, 60),
                    MidiEvent::NoteOn(10, 1, 61, 90), MidiEvent::NoteOff(200, 1, 61),
                    MidiEvent::NoteOn(20, 1, 62, 90), MidiEvent::NoteOff(350, 1, 62)};
  base[0].noteId = 1; base[2].noteId = 2; base[4].noteId = 3;
  EditPass rows[2];
  for (auto& row : rows) {
    row.passType = EditPassType::Note; row.actionType = EditActionType::Update;
    row.propertyType = EditPropertyType::Length;
  }
  rows[0].targetNoteId = 3; rows[0].endTick = 400;
  rows[1].targetNoteId = 1; rows[1].endTick = 200;
  CompanionApplyFixture fixture; fixture.prewarm(8);
  PlaybackViewSourceIdentity source{1, 2, base.data()};
  TEST_ASSERT_TRUE(fixture.apply.beginBatch(fixture.work, base.data(), base.size(), source, rows, 2, 768));
  auto result = RuntimePlaybackOrderBuildResult::Pending;
  for (size_t i = 0; i < 10000 && result == RuntimePlaybackOrderBuildResult::Pending; ++i)
    result = fixture.apply.step(1);
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Complete, result);
  assertCompanionBridgeMapping(fixture.apply, base, source, 1);
  RuntimeCompanionBridgeMap mapping; TEST_ASSERT_TRUE(mapping.prewarm(8));
  TEST_ASSERT_TRUE(mapping.begin(fixture.apply, base.data(), base.size(), source));
  while (mapping.step(1) == RuntimePlaybackOrderBuildResult::Pending) {}
  TEST_ASSERT_TRUE(mapping.ready(source));
  TEST_ASSERT_TRUE(mapping.baseRanks()[3] < mapping.baseRanks()[1]);
  TEST_ASSERT_EQUAL_UINT32(200, fixture.apply.data()[mapping.baseRanks()[1]].tick);
  // Restarting the same apply storage with the same source identity must still
  // invalidate a completed mapping (generation, not pointer/count, distinguishes it).
  TEST_ASSERT_TRUE(fixture.apply.beginBatch(fixture.work, base.data(), base.size(), source, nullptr, 0, 768));
  while (fixture.apply.step(32) == RuntimePlaybackOrderBuildResult::Pending) {}
  TEST_ASSERT_FALSE(mapping.ready(source)); TEST_ASSERT_NULL(mapping.records());
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, mapping.step(1));
}

void test_companion_bridge_map_guards_source_capacity_and_cancellation() {
  MidiEventVec base{MidiEvent::NoteOn(0, 1, 60, 90), MidiEvent::NoteOff(90, 1, 60)};
  base[0].noteId = 1;
  CompanionApplyFixture fixture; fixture.prewarm(4);
  PlaybackViewSourceIdentity source{1, 2, base.data()};
  RuntimeCompanionBridgeMap mapping; TEST_ASSERT_TRUE(mapping.prewarm(2));
  TEST_ASSERT_FALSE(mapping.prewarm(4));
  TEST_ASSERT_FALSE(mapping.begin(fixture.apply, base.data(), base.size(), source));
  TEST_ASSERT_TRUE(fixture.apply.begin(fixture.work, base.data(), base.size(), source,
                                     RuntimeCompanionApply::Type::Delete, 1, 0, 768));
  while (fixture.apply.step(32) == RuntimePlaybackOrderBuildResult::Pending) {}
  RuntimeCompanionBridgeMap tooSmall;
  TEST_ASSERT_FALSE(tooSmall.prewarm(0));
  TEST_ASSERT_FALSE(tooSmall.prewarm(2049));
  TEST_ASSERT_TRUE(tooSmall.prewarm(1));
  TEST_ASSERT_FALSE(tooSmall.begin(fixture.apply, base.data(), base.size(), source));
  TEST_ASSERT_NULL(tooSmall.records());
  auto copy = base;
  TEST_ASSERT_FALSE(mapping.begin(fixture.apply, copy.data(), copy.size(), source));
  TEST_ASSERT_FALSE(mapping.begin(fixture.apply, base.data(), 1, source));
  TEST_ASSERT_TRUE(mapping.begin(fixture.apply, base.data(), base.size(), source));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, mapping.step(0));
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Pending, mapping.step(1));
  ++source.sourceGeneration;
  TEST_ASSERT_EQUAL(RuntimePlaybackOrderBuildResult::Denied, mapping.step(1));
  TEST_ASSERT_NULL(mapping.records()); TEST_ASSERT_EQUAL_UINT32(0, mapping.size());
  mapping.discard();
  TEST_ASSERT_NULL(mapping.baseRanks());
}

void test_reference_same_tick_off_helper_preserves_event_bytes() {
  MidiEventVec events{MidiEvent::NoteOn(0, 1, 60, 90), MidiEvent::NoteOn(96, 1, 60, 80),
                      MidiEvent::NoteOff(192, 1, 60, 11), MidiEvent::NoteOff(192, 1, 60, 22)};
  for (size_t i = 0; i < events.size(); ++i) events[i].noteId = i + 1;
  const auto original = events;
  NoteUtils::orderSamePitchNoteOffsForLifo(events, 1, 60);
  assertMidiEventVecEquivalent(original, events, "legacy helper writes rows back to their own indices");
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_bounded_companion_apply_maximum_candidate_order_and_helper_exclusion);
  RUN_TEST(test_per_pass_companions_preserve_scope_and_repeated_identity);
  RUN_TEST(test_per_pass_companions_match_generated_canonical_merges);
  RUN_TEST(test_per_pass_companions_maximum_aggregate_and_empty_pass_count);
  RUN_TEST(test_per_pass_companions_hide_overflow_and_stale_or_reused_work);
  RUN_TEST(test_companion_pass_scope_differs_from_merged_capture_scope);
  RUN_TEST(test_companion_bridge_map_preserves_batch_order_separately_from_provenance);
  RUN_TEST(test_companion_bridge_map_guards_source_capacity_and_cancellation);
  RUN_TEST(test_reference_same_tick_off_helper_preserves_event_bytes);
  RUN_TEST(test_bounded_companion_batch_matches_sequential_reference_and_base_provenance);
  RUN_TEST(test_create_history_preserves_equal_tick_append_order);
  RUN_TEST(test_bounded_history_updates_match_canonical_sequence);
  RUN_TEST(test_bounded_create_reactivates_identity_with_surviving_duplicate);
  RUN_TEST(test_bounded_create_empty_maximum_capacity_and_cancellation);
  RUN_TEST(test_canonical_pass_inputs_feed_per_pass_companions);
  RUN_TEST(test_canonical_pass_inputs_capacity_stale_and_borrow_guards);
  RUN_TEST(test_canonical_pass_inputs_maximum_and_pending_consumer_invalidation);
  RUN_TEST(test_canonical_pass_selection_and_filtered_history_match_materialization);
  RUN_TEST(test_canonical_selection_denies_ambiguous_order_and_stale_source);
  RUN_TEST(test_canonical_frozen_source_invalidates_without_producer_turn);
  RUN_TEST(test_canonical_frozen_source_refuses_unclosed_or_mismatched_admission);
  RUN_TEST(test_frozen_companion_owner_binds_per_pass_reference_and_reuse);
  RUN_TEST(test_frozen_companion_owner_rejects_other_capture_and_unbound_inputs);
  RUN_TEST(test_frozen_overdub_inputs_match_committed_capture_reference);
  RUN_TEST(test_frozen_overdub_inputs_maximum_capacity_and_stale_order);
  RUN_TEST(test_companion_continuation_preserves_history_membership);
  RUN_TEST(test_companion_history_prefix_quantum_and_capacity_guards);
  RUN_TEST(test_frozen_tail_reuse_guards_and_invalidates_old_consumers);
  RUN_TEST(test_empty_row_paths_do_not_borrow_or_copy_through_apply);
  RUN_TEST(test_canonical_selection_maximum_metadata_horizons);
  RUN_TEST(test_bounded_history_collapsing_capacity_and_stale_guards);
  RUN_TEST(test_bounded_history_wrap_updates_and_unpaired_move);
  RUN_TEST(test_bounded_companion_batch_denial_keeps_prior_rows_unpublished);
  RUN_TEST(test_bounded_companion_apply_matches_delete_and_length_reference);
  RUN_TEST(test_bounded_companion_apply_synthesizes_off_and_keeps_shortening_order);
  RUN_TEST(test_bounded_companion_apply_capacity_and_stale_source_hide_partial_candidate);
  RUN_TEST(test_bounded_companion_reconstruction_matches_pitch_reference);
  RUN_TEST(test_bounded_companion_reconstruction_rejects_stale_and_partial_output);
  RUN_TEST(test_bounded_companion_reconstruction_maximum_unique_and_duplicate_geometry);
  RUN_TEST(test_bounded_companion_spans_match_canonical_reconstruction);
  RUN_TEST(test_bounded_companion_spans_cover_equal_tick_deferred_end_and_zero_wrap);
  RUN_TEST(test_bounded_companion_spans_maximum_capacity_and_discard);
  RUN_TEST(test_reconstruction_wrap_lookup_matches_legacy_queries);
  RUN_TEST(test_reconstruction_wrap_lookup_retains_reconstruct_specific_rules);
  RUN_TEST(test_reconstruction_wrap_lookup_maximum_grain_bound_and_tie_order);
  RUN_TEST(test_companion_overlap_walk_holds_actions_and_preserves_legacy_action_order);
  RUN_TEST(test_companion_overlap_walk_matches_actual_length_application);
  RUN_TEST(test_companion_overlap_walk_maximum_input_and_denied_admission);
  RUN_TEST(test_bounded_delete_capacity_bound_and_discard);
  RUN_TEST(test_bounded_wrap_head_lookup_matches_reference);
  RUN_TEST(test_bounded_delete_selection_matches_actual_delete_with_retained_rows);
  RUN_TEST(test_bounded_delete_selects_both_tail_and_wrap_off_and_preserves_source);
  RUN_TEST(test_companion_lane_run_counterexample_pairs_index_3_at_tick_96);
  RUN_TEST(test_companion_runs_are_filtered_by_lane_and_stable_within_kind);
  RUN_TEST(test_bounded_companion_pair_preserves_tagged_and_unique_wrap_fallback);
  RUN_TEST(test_bounded_companion_pair_matches_revised_reference_across_generated_lanes);
  RUN_TEST(test_121141_gate5a_on_then_off_length_pairing);
  RUN_TEST(test_121141_gate4_per_layer_6040_cannot_steal_off168_from_6073_layer);
  RUN_TEST(test_121141_gate4_tick_sorted_early_layer_length_6040_leaves_off168);
  RUN_TEST(test_121141_gate5_off_before_on_length_6073_551_leaves_off168_reconstruct_b_168_360);
  RUN_TEST(test_121141_gate5_off_before_on_length_6079_167_shortens_a_off168);
  RUN_TEST(test_change_pitch_on_lengthened_note_keeps_same_pitch_neighbor);
  RUN_TEST(test_lengthen_after_move_keeps_p0_fixture_gate);
  RUN_TEST(test_change_length_rematerialize_keeps_p0_off_not_loop_end);
  RUN_TEST(test_length_replay_loop_boundary_does_not_shorten_same_pitch_neighbors_203140);
  RUN_TEST(test_length_row_after_earlier_move_row_on_same_note_seals_210821);
  RUN_TEST(test_second_move_row_on_same_note_wins_over_earlier_span_210945);
  RUN_TEST(test_145518_note_range_pitch_replay_moves_when_store_id_matches);
  RUN_TEST(test_145518_note_range_pitch_replay_leaves_home_when_store_id_differs);
  RUN_TEST(test_145518_open_assigned_note_id_missing_from_pass_rematerialize);
  RUN_TEST(test_145518_assign_on_committed_passes_makes_rematerialize_find_id);
  RUN_TEST(test_nested_same_pitch_outer_apply_helpers_use_lifo_off);
  RUN_TEST(test_195941_wrap_home_move_keeps_length);
  RUN_TEST(test_201446_wrap_home_move_drops_false_linear_off);
  RUN_TEST(test_change_pitch_on_overlapping_note_keeps_neighbor_endtick);
  RUN_TEST(test_apply_edits_delete_note);
  RUN_TEST(test_save_edit_appends_without_collapsing_takes);
  RUN_TEST(test_save_note_edit_pass_sets_scoped_action_property_mapping);
  RUN_TEST(test_edit_pass_state_toggle_does_not_append_edit_actions);
  RUN_TEST(test_disable_edits_restores_take_only_view);
  RUN_TEST(test_reset_take_timeline_clears_stale_edits);
  RUN_TEST(test_note_edit_session_undo_stack);
  RUN_TEST(test_add_note_rematerialize_session_store);
  RUN_TEST(test_move_note_uses_track_channel);
  RUN_TEST(test_lengthen_commit_rematerialize_hitl_fixture);
  RUN_TEST(test_change_length_rematerialize_hitl_195830_ticks);
  RUN_TEST(test_overlap_round_trip_replay_lengthen_delete_pitch);
  RUN_TEST(test_pre_commit_order_overlap_changes_before_move_and_pitch);
  RUN_TEST(test_pre_commit_delete_before_mover_change_length);
  RUN_TEST(test_playback_merge_includes_active_edit_passes);
  RUN_TEST(test_global_undo_overdub_pass_added_disables_overdub);
  RUN_TEST(test_global_undo_note_edit_pass_closed_disables_edit_rows);
  RUN_TEST(test_global_undo_three_step_restores_record_baseline);
  RUN_TEST(test_session_undo_move_back_insert_before_save_note_edit_pass);
  RUN_TEST(test_b2_edit_pass_ids_mean_active_committed_not_ever_created);
  RUN_TEST(test_b2_committed_base_matches_materialize_active_edit_passes);
  RUN_TEST(test_b2_stale_visual_cache_overwrites_synced_current_state_spans);
  RUN_TEST(test_173806_reconcile_unique_match_retargets_mover_not_overlap_delete);
  RUN_TEST(test_reconcile_added_note_does_not_retarget);
  RUN_TEST(test_reconcile_zero_match_at_commit_baseline_does_not_retarget);
  RUN_TEST(test_reconcile_ambiguous_geometry_does_not_retarget);
  RUN_TEST(test_reconcile_already_present_session_id_does_not_retarget);
  RUN_TEST(test_sequence_phasea_collapse_contiguous_velocity_and_pitch_equivalence);
  RUN_TEST(test_sequence_phasea_barrier_with_length_keeps_equivalence);
  RUN_TEST(test_sequence_phasea_randomized_equivalence_against_baseline);
  RUN_TEST(test_sequence_phaseb_length_then_same_note_update_equivalence);
  RUN_TEST(test_sequence_phaseb_length_with_interleaved_note_updates_equivalence);
  RUN_TEST(test_incremental_companion_projection_operation_matrix);
  RUN_TEST(test_incremental_companion_projection_randomized_history_and_undo_redo);
  RUN_TEST(test_incremental_companion_projection_reduced_state_minimal_geometry_diverges);
  RUN_TEST(test_incremental_companion_projection_reduced_state_randomized_divergence);
  RUN_TEST(test_incremental_companion_projection_paired_state_operation_matrix);
  RUN_TEST(test_incremental_companion_projection_paired_state_randomized_history_and_undo_redo);
  RUN_TEST(test_incremental_companion_projection_paired_state_scenario51_generation13_equivalent);
  RUN_TEST(test_incremental_companion_projection_span_only_randomized_divergence_boundary);
  RUN_TEST(test_incremental_companion_projection_context_ablation_channel_velocity_requirements);
  RUN_TEST(test_incremental_companion_projection_pitch_ablation_requirement);
  RUN_TEST(test_incremental_companion_projection_start_tick_ablation_requirement);
  RUN_TEST(test_incremental_companion_projection_end_tick_ablation_requirement);
  RUN_TEST(test_incremental_companion_projection_tick_ordered_materialize_divergence_boundary);
  RUN_TEST(test_incremental_companion_projection_note_id_ablation_divergence_boundary);
  RUN_TEST(test_incremental_companion_projection_derived_key_order_divergence_boundary);
  RUN_TEST(test_incremental_companion_projection_split_order_divergence_boundary);
  RUN_TEST(test_incremental_companion_projection_same_tick_order_divergence_boundary);
  return UNITY_END();
}
