//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>
#include <cstdio>
#include <random>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/EditManager/EditApply.cpp"
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

void appendCompanionProjectionNote(MidiEventVec& events, NoteId noteId, uint32_t startTick,
                                   uint32_t endTick, uint8_t pitch) {
  events.push_back(sequenceNoteOnWithId(startTick, 1, pitch, 100, noteId));
  events.push_back(MidiEvent::NoteOff(endTick, 1, pitch, 0));
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
  loop.mergeMaterializedPassesWithCapture(materializedPlayback);
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
  const NoteId targetIds[] = {
      kCompanionTargetNoteId,
      kCompanionOverlapNoteId,
      kCompanionDistantOverlapNoteId,
      kCompanionDifferentPitchNoteId,
      kCompanionWrapNoteId,
  };
  std::mt19937 rng(0xC0A11u);
  std::uniform_int_distribution<int> targetDist(0, 4);
  std::uniform_int_distribution<int> actionDist(0, 99);
  std::uniform_int_distribution<int> endTickDist(
      1, static_cast<int>(kCompanionProjectionLoopLength - 1));
  std::uniform_int_distribution<int> rowCountDist(8, 32);

  for (uint32_t scenario = 0; scenario < 128; ++scenario) {
    EditPassVec history;
    history.reserve(40);

    // Keep the first generations deterministic so every randomized scenario
    // contains overlap, wrap, Hide/Shorten ordering, and repeated targets.
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

    assignCompanionHistoryIds(history, 2000 + scenario * 64);
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
  const NoteId targetIds[] = {
      kCompanionTargetNoteId,
      kCompanionOverlapNoteId,
      kCompanionDistantOverlapNoteId,
      kCompanionDifferentPitchNoteId,
      kCompanionWrapNoteId,
  };
  std::mt19937 rng(0x5EEDC0DEu);
  std::uniform_int_distribution<int> targetDist(0, 4);
  std::uniform_int_distribution<int> actionDist(0, 99);
  std::uniform_int_distribution<int> endTickDist(
      1, static_cast<int>(kCompanionProjectionLoopLength - 1));
  std::uniform_int_distribution<int> rowCountDist(8, 32);
  uint32_t mismatchScenarios = 0;
  uint32_t firstMismatchGeneration = UINT32_MAX;

  for (uint32_t scenario = 0; scenario < 128; ++scenario) {
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

    assignCompanionHistoryIds(history, 12000 + scenario * 64);
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

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
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
  return UNITY_END();
}
