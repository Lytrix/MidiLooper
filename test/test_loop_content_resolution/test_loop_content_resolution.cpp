//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// DEC-037 Stage 0 — canonical stress fixture + materialize oracle.

#include <unity.h>

#include <chrono>
#include <cstdio>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/Utils/IntervalProjection.cpp"
#include "../../src/Utils/DisplayWindowUtils.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/EditManager/EditApply.cpp"
#include "../../src/Loop/LoopPasses.cpp"
#include "../test_support/MemoryMonitorNativeDeps.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"

#include "../test_support/CommittedChunkIdTestHelpers.h"
#include "../test_support/NoteIdTestFixtures.h"
#include "CanonicalResolutionFixture.h"
#include "Utils/DisplayWindowUtils.h"

namespace {

using namespace NoteIdTestFixtures;
using Clock = std::chrono::steady_clock;

CommittedChunkIdList makeNoteSpan(uint32_t onTick, uint32_t offTick, uint8_t channel, uint8_t pitch,
                                  NoteId id) {
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, onTick, channel, pitch, 100, id));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(offTick, channel, pitch, 0)));
  CommittedChunkIdList ids;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, ids));
  return ids;
}

OverdubPass makeOverdub(PassId id, uint32_t mergeSequence, uint32_t onTick, uint32_t offTick,
                        uint8_t channel, uint8_t pitch, NoteId noteId) {
  OverdubPass pass{};
  pass.id = id;
  pass.mergeSequence = mergeSequence;
  pass.state = CapturePassState::Active;
  pass.committedChunkIds = makeNoteSpan(onTick, offTick, channel, pitch, noteId);
  return pass;
}

EditPass makeDelete(PassId id, NoteId target) {
  EditPass row{};
  row.id = id;
  row.passType = EditPassType::Note;
  row.actionType = EditActionType::Delete;
  row.state = EditPassState::Active;
  row.targetNoteId = target;
  return row;
}

EditPass makeLength(PassId id, NoteId target, uint32_t start, uint32_t end) {
  EditPass row{};
  row.id = id;
  row.passType = EditPassType::Note;
  row.actionType = EditActionType::Update;
  row.propertyType = EditPropertyType::Length;
  row.state = EditPassState::Active;
  row.targetNoteId = target;
  row.startTick = start;
  row.endTick = end;
  return row;
}

EditPass makeMove(PassId id, NoteId target, uint32_t start, uint32_t end, uint8_t pitch) {
  EditPass row{};
  row.id = id;
  row.passType = EditPassType::Note;
  row.actionType = EditActionType::Update;
  row.propertyType = EditPropertyType::NoteRange;
  row.state = EditPassState::Active;
  row.targetNoteId = target;
  row.startTick = start;
  row.endTick = end;
  row.pitch = pitch;
  return row;
}

}  // namespace

CanonicalResolutionFixture buildCanonicalResolutionFixture() {
  resetNoteIdCounter(1);
  CanonicalResolutionFixture fixture;
  fixture.loopLengthTicks = kCanonicalBars * Config::TICKS_PER_BAR;

  NoteId nextId = 1;
  PassId nextPassId = 1;

  fixture.overlapHostNoteId = nextId++;
  const NoteId recordSecondId = nextId++;
  fixture.wrapNoteId = nextId++;

  RecordPass record{};
  record.id = nextPassId++;
  record.state = CapturePassState::Active;
  {
    LoopEventStore store;
    TEST_ASSERT_TRUE(storeAppendNoteOn(store, 0, 1, 60, 100, fixture.overlapHostNoteId));
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(200, 1, 60, 0)));
    TEST_ASSERT_TRUE(storeAppendNoteOn(store, Config::TICKS_PER_BAR, 1, 62, 100, recordSecondId));
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(Config::TICKS_PER_BAR + 132, 1, 62, 0)));
    TEST_ASSERT_TRUE(storeAppendNoteOn(store, fixture.loopLengthTicks - 48, 1, 64, 100,
                                       fixture.wrapNoteId));
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(96, 1, 64, 0)));
    TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, record.committedChunkIds));
  }
  fixture.passes.recordPass = std::move(record);

  fixture.overlapIncomingNoteId = kInvalidNoteId;
  fixture.deletedNoteId = kInvalidNoteId;
  fixture.shortenedNoteId = kInvalidNoteId;
  fixture.movedNoteId = kInvalidNoteId;

  for (uint32_t i = 0; i < kCanonicalOverdubPasses; ++i) {
    const NoteId noteId = nextId++;
    uint32_t onTick = ((i * 3u) % kCanonicalBars) * Config::TICKS_PER_BAR + 24u;
    uint32_t offTick = onTick + 96u;
    uint8_t pitch = static_cast<uint8_t>(48u + (i % 24u));
    uint8_t channel = static_cast<uint8_t>(1u + (i % 2u));
    if (i == 5u) {
      onTick = 50;
      offTick = 250;
      pitch = 60;
      channel = 1;
      fixture.overlapIncomingNoteId = noteId;
    }
    if (i == 10u) {
      fixture.deletedNoteId = noteId;
    }
    if (i == 11u) {
      fixture.shortenedNoteId = noteId;
    }
    if (i == 12u) {
      fixture.movedNoteId = noteId;
    }
    fixture.passes.overdubPasses.push_back(makeOverdub(nextPassId++, i + 1, onTick, offTick, channel,
                                                       pitch, noteId));
  }

  TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, fixture.deletedNoteId);
  TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, fixture.shortenedNoteId);
  TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, fixture.movedNoteId);
  TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, fixture.overlapIncomingNoteId);

  fixture.passes.editPasses.push_back(makeDelete(nextPassId++, fixture.deletedNoteId));
  fixture.passes.editPasses.push_back(
      makeLength(nextPassId++, fixture.shortenedNoteId, 24, 48));
  fixture.passes.editPasses.push_back(
      makeMove(nextPassId++, fixture.movedNoteId, 400, 496, 70));

  return fixture;
}

void countWindowEvents(const SessionMidiEventVec& events, uint32_t loopLengthTicks,
                       uint32_t windowStart, uint32_t windowLength,
                       ResolutionCostCounters& counters) {
  SessionMidiEventVec window;
  DisplayWindowUtils::filterMidiEventsToWindow(events, window, windowStart, windowLength,
                                               loopLengthTicks);
  counters.eventsInQueryWindow = static_cast<uint32_t>(window.size());
  counters.candidateEvents = counters.eventsInQueryWindow;
}

void test_canonical_fixture_inventory() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  const CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();

  TEST_ASSERT_EQUAL_UINT32(kCanonicalBars * Config::TICKS_PER_BAR, fixture.loopLengthTicks);
  TEST_ASSERT_EQUAL_UINT32(1u + kCanonicalOverdubPasses, fixture.passes.capturePassCount());
  TEST_ASSERT_GREATER_OR_EQUAL(45u, fixture.passes.capturePassCount());
  TEST_ASSERT_EQUAL_UINT32(3u, fixture.passes.editPasses.size());
  TEST_ASSERT_TRUE(NoteUtils::isWrappedLoopNotePair(fixture.loopLengthTicks - 48, 96,
                                                    fixture.loopLengthTicks));
  TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, fixture.wrapNoteId);
  TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, fixture.overlapHostNoteId);
  TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, fixture.overlapIncomingNoteId);
}

void test_canonical_fixture_oracle_materialize() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  const CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();

  SessionMidiEventVec events;
  const auto start = Clock::now();
  fixture.passes.materializeToEventVector(events, fixture.loopLengthTicks);
  const NoteUtils::DisplayNoteVec notes =
      NoteUtils::reconstructDisplayNotes(events, fixture.loopLengthTicks, false);
  const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start);

  ResolutionCostCounters counters;
  counters.eventsInHistory = static_cast<uint32_t>(events.size());
  counters.passesInHistory = static_cast<uint32_t>(fixture.passes.capturePassCount() +
                                                   fixture.passes.editPasses.size());
  const uint32_t windowLength = kCanonicalQueryWindowBars * Config::TICKS_PER_BAR;
  countWindowEvents(events, fixture.loopLengthTicks, 0, windowLength, counters);
  counters.resolutionOperations = 0;
  counters.elapsedMicros = static_cast<uint64_t>(elapsed.count());

  std::printf(
      "canonical_oracle history_events=%u history_passes=%u window_events=%u candidates=%u "
      "resolve_ops=%u elapsed_us=%llu notes=%u\n",
      counters.eventsInHistory, counters.passesInHistory, counters.eventsInQueryWindow,
      counters.candidateEvents, counters.resolutionOperations,
      static_cast<unsigned long long>(counters.elapsedMicros),
      static_cast<unsigned>(notes.size()));

  TEST_ASSERT_GREATER_THAN(0u, counters.eventsInHistory);
  TEST_ASSERT_GREATER_OR_EQUAL(45u, counters.passesInHistory);
  TEST_ASSERT_LESS_THAN(counters.eventsInHistory, counters.eventsInQueryWindow);
  TEST_ASSERT_GREATER_THAN(0u, notes.size());
  TEST_ASSERT_FALSE(events.empty());
}

void test_canonical_fixture_wrap_and_edits_in_oracle() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  const CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();

  SessionMidiEventVec events;
  fixture.passes.materializeToEventVector(events, fixture.loopLengthTicks);

  bool sawWrapOn = false;
  bool sawWrapOff = false;
  bool sawDeletedOn = false;
  bool sawShortenedOn = false;
  bool sawMovedOn = false;
  for (const MidiEvent& event : events) {
    if (event.isNoteOn() && event.noteId == fixture.wrapNoteId &&
        event.tick == fixture.loopLengthTicks - 48) {
      sawWrapOn = true;
    }
    if (event.isNoteOff() && event.data.noteData.note == 64 && event.tick == 96) {
      sawWrapOff = true;
    }
    if (event.isNoteOn() && event.noteId == fixture.deletedNoteId) {
      sawDeletedOn = true;
    }
    if (event.isNoteOn() && event.noteId == fixture.shortenedNoteId) {
      sawShortenedOn = true;
    }
    if (event.isNoteOn() && event.noteId == fixture.movedNoteId) {
      sawMovedOn = true;
    }
  }
  TEST_ASSERT_TRUE(sawWrapOn);
  TEST_ASSERT_TRUE(sawWrapOff);
  TEST_ASSERT_FALSE(sawDeletedOn);
  TEST_ASSERT_TRUE(sawShortenedOn);
  TEST_ASSERT_TRUE(sawMovedOn);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_canonical_fixture_inventory);
  RUN_TEST(test_canonical_fixture_oracle_materialize);
  RUN_TEST(test_canonical_fixture_wrap_and_edits_in_oracle);
  return UNITY_END();
}
