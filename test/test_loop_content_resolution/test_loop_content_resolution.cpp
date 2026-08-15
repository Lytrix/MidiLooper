//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// DEC-037 Stage 0 — canonical stress fixture + materialize oracle.

#include <unity.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/Utils/IntervalProjection.cpp"
#include "../../src/Utils/DisplayWindowUtils.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/EditManager/EditApply.cpp"
#include "../../src/Loop/LoopPasses.cpp"
#include "../test_support/MemoryMonitorNativeDeps.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/CommittedEventRange.cpp"

#include "../test_support/CommittedChunkIdTestHelpers.h"
#include "../test_support/NoteIdTestFixtures.h"
#include "CanonicalResolutionFixture.h"
#include "LoopContentResolution.h"
#include "../../src/LoopContentResolution.cpp"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/IntervalProjection.h"

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

bool oracleEventLess(const MidiEvent& a, const MidiEvent& b) {
  if (a.tick != b.tick) {
    return a.tick < b.tick;
  }
  if (a.type != b.type) {
    return static_cast<uint8_t>(a.type) < static_cast<uint8_t>(b.type);
  }
  if (a.channel != b.channel) {
    return a.channel < b.channel;
  }
  if (a.data.noteData.note != b.data.noteData.note) {
    return a.data.noteData.note < b.data.noteData.note;
  }
  return a.noteId < b.noteId;
}

void sortOracleEvents(SessionMidiEventVec& events) {
  std::sort(events.begin(), events.end(), oracleEventLess);
}

void assertResolvedEventsMatch(const SessionMidiEventVec& expected,
                               const SessionMidiEventVec& actual) {
  TEST_ASSERT_EQUAL(expected.size(), actual.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    TEST_ASSERT_EQUAL_UINT32(expected[i].tick, actual[i].tick);
    TEST_ASSERT_EQUAL(expected[i].type, actual[i].type);
    TEST_ASSERT_EQUAL(expected[i].channel, actual[i].channel);
    TEST_ASSERT_EQUAL(expected[i].data.noteData.note, actual[i].data.noteData.note);
    TEST_ASSERT_EQUAL(expected[i].noteId, actual[i].noteId);
  }
}

void materializeSorted(const LoopPasses& passes, uint32_t loopLengthTicks,
                       SessionMidiEventVec& out) {
  passes.materializeToEventVector(out, loopLengthTicks);
  sortOracleEvents(out);
}

bool hasNoteIdOn(const SessionMidiEventVec& events, NoteId id) {
  for (const MidiEvent& event : events) {
    if (event.isNoteOn() && event.noteId == id) {
      return true;
    }
  }
  return false;
}

const NoteUtils::DisplayNote* findNote(const NoteUtils::DisplayNoteVec& notes, NoteId id) {
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.noteId == id) {
      return &note;
    }
  }
  return nullptr;
}

LoopPasses recordOnly(const CanonicalResolutionFixture& full) {
  LoopPasses onePass;
  onePass.recordPass = full.passes.recordPass;
  return onePass;
}

LoopPasses recordPlusOverlap(const CanonicalResolutionFixture& full) {
  LoopPasses twoPass = recordOnly(full);
  TEST_ASSERT_EQUAL(kCanonicalOverdubPasses, full.passes.overdubPasses.size());
  twoPass.overdubPasses.push_back(full.passes.overdubPasses[5]);
  return twoPass;
}

void printCounters(const char* label, const ResolutionCostCounters& counters) {
  std::printf(
      "%s history_events=%u history_passes=%u window_events=%u candidates=%u resolve_ops=%u "
      "elapsed_us=%llu\n",
      label, counters.eventsInHistory, counters.passesInHistory, counters.eventsInQueryWindow,
      counters.candidateEvents, counters.resolutionOperations,
      static_cast<unsigned long long>(counters.elapsedMicros));
}

void commitFixtureIndex(const CanonicalResolutionFixture& fixture,
                        LoopContentResolution::TickIndex& index) {
  ResolutionCostCounters recordCounters;
  index.commitCapturePass(fixture.passes.recordPass.id, fixture.passes.recordPass.committedChunkIds,
                          fixture.passes.recordPass.state, 0, &recordCounters);
  TEST_ASSERT_EQUAL_UINT32(1u, recordCounters.passChunkListsWalked);
  for (const OverdubPass& pass : fixture.passes.overdubPasses) {
    ResolutionCostCounters commitCounters;
    index.commitCapturePass(pass.id, pass.committedChunkIds, pass.state, pass.mergeSequence,
                            &commitCounters);
    TEST_ASSERT_EQUAL_UINT32(1u, commitCounters.passChunkListsWalked);
  }
}

void oracleWindowEvents(const LoopPasses& passes, uint32_t loopLengthTicks, uint32_t windowStart,
                        uint32_t windowLength, SessionMidiEventVec& out) {
  SessionMidiEventVec materialized;
  passes.materializeToEventVector(materialized, loopLengthTicks);
  DisplayWindowUtils::filterMidiEventsToWindow(materialized, out, windowStart, windowLength,
                                               loopLengthTicks);
  sortOracleEvents(out);
}

void sortSounding(SoundingNoteVec& notes) {
  std::sort(notes.begin(), notes.end(), [](const SoundingNote& a, const SoundingNote& b) {
    if (a.noteId != b.noteId) {
      return a.noteId < b.noteId;
    }
    return a.pitch < b.pitch;
  });
}

void assertSoundingMatch(SoundingNoteVec expected, SoundingNoteVec actual) {
  sortSounding(expected);
  sortSounding(actual);
  TEST_ASSERT_EQUAL(expected.size(), actual.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    TEST_ASSERT_EQUAL(expected[i].noteId, actual[i].noteId);
    TEST_ASSERT_EQUAL(expected[i].pitch, actual[i].pitch);
    TEST_ASSERT_EQUAL_UINT32(expected[i].onTick, actual[i].onTick);
  }
}

bool hasSoundingNoteId(const SoundingNoteVec& notes, NoteId id) {
  for (const SoundingNote& note : notes) {
    if (note.noteId == id) {
      return true;
    }
  }
  return false;
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
  uint32_t shortenedOnTick = 0;

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
      shortenedOnTick = onTick;
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
      makeLength(nextPassId++, fixture.shortenedNoteId, shortenedOnTick, shortenedOnTick + 24u));
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

void test_stage1_one_pass_window_matches_materialize() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture full = buildCanonicalResolutionFixture();
  const LoopPasses onePass = recordOnly(full);

  SessionMidiEventVec expected;
  materializeSorted(onePass, full.loopLengthTicks, expected);

  SessionMidiEventVec actual;
  ResolutionCostCounters counters;
  LoopContentResolution::resolveWindow(onePass, full.loopLengthTicks, 0, full.loopLengthTicks,
                                       actual, &counters);
  printCounters("stage1_window", counters);
  assertResolvedEventsMatch(expected, actual);
  TEST_ASSERT_GREATER_THAN(0u, counters.resolutionOperations);
}

void test_stage1_resolve_state_during_host_note() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture full = buildCanonicalResolutionFixture();
  const LoopPasses onePass = recordOnly(full);

  SoundingNoteVec sounding;
  LoopContentResolution::resolveState(onePass, full.loopLengthTicks, 100, sounding);
  bool foundHost = false;
  for (const SoundingNote& note : sounding) {
    if (note.noteId == full.overlapHostNoteId && note.pitch == 60) {
      foundHost = true;
    }
  }
  TEST_ASSERT_TRUE(foundHost);

  SoundingNoteVec after;
  LoopContentResolution::resolveState(onePass, full.loopLengthTicks, 201, after);
  for (const SoundingNote& note : after) {
    TEST_ASSERT_FALSE(note.noteId == full.overlapHostNoteId);
  }
}

void test_stage1_resolve_state_wrap_note() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture full = buildCanonicalResolutionFixture();
  const LoopPasses onePass = recordOnly(full);

  auto hasWrap = [&](uint32_t tick) {
    SoundingNoteVec sounding;
    LoopContentResolution::resolveState(onePass, full.loopLengthTicks, tick, sounding);
    for (const SoundingNote& note : sounding) {
      if (note.noteId == full.wrapNoteId) {
        return true;
      }
    }
    return false;
  };

  TEST_ASSERT_TRUE(hasWrap(10));
  TEST_ASSERT_TRUE(hasWrap(full.loopLengthTicks - 24));
  TEST_ASSERT_FALSE(hasWrap(97));
}

void test_stage1_resolve_notes_is_projection_of_window() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture full = buildCanonicalResolutionFixture();
  const LoopPasses onePass = recordOnly(full);

  SessionMidiEventVec materialized;
  onePass.materializeToEventVector(materialized, full.loopLengthTicks);
  const NoteUtils::DisplayNoteVec fromMaterialize =
      NoteUtils::reconstructDisplayNotes(materialized, full.loopLengthTicks, false);
  NoteUtils::DisplayNoteVec fromResolve;
  LoopContentResolution::resolveNotes(onePass, full.loopLengthTicks, 0, full.loopLengthTicks,
                                      fromResolve);
  TEST_ASSERT_EQUAL(fromMaterialize.size(), fromResolve.size());
}

void test_stage2_overlapping_same_pitch_matches_materialize() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture full = buildCanonicalResolutionFixture();
  const LoopPasses twoPass = recordPlusOverlap(full);

  SessionMidiEventVec expected;
  materializeSorted(twoPass, full.loopLengthTicks, expected);
  SessionMidiEventVec actual;
  ResolutionCostCounters counters;
  LoopContentResolution::resolveWindow(twoPass, full.loopLengthTicks, 0, full.loopLengthTicks,
                                       actual, &counters);
  printCounters("stage2_overlap_window", counters);
  assertResolvedEventsMatch(expected, actual);
  TEST_ASSERT_TRUE(hasNoteIdOn(actual, full.overlapHostNoteId));
  TEST_ASSERT_TRUE(hasNoteIdOn(actual, full.overlapIncomingNoteId));

  // DEC-031/032 overlap is a committed EditPass delta, not a query-time re-resolve.
  // This fixture stores both raw spans; sounding state matches materialize (both On).
  SoundingNoteVec atOverlap;
  LoopContentResolution::resolveState(twoPass, full.loopLengthTicks, 100, atOverlap);
  bool host = false;
  bool incoming = false;
  for (const SoundingNote& note : atOverlap) {
    if (note.noteId == full.overlapHostNoteId) {
      host = true;
    }
    if (note.noteId == full.overlapIncomingNoteId) {
      incoming = true;
    }
  }
  TEST_ASSERT_TRUE(host);
  TEST_ASSERT_TRUE(incoming);
}

void test_stage3_delete_matches_materialize() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();

  SessionMidiEventVec expected;
  materializeSorted(fixture.passes, fixture.loopLengthTicks, expected);
  SessionMidiEventVec actual;
  ResolutionCostCounters counters;
  LoopContentResolution::resolveWindow(fixture.passes, fixture.loopLengthTicks, 0,
                                       fixture.loopLengthTicks, actual, &counters);
  printCounters("stage3_delete_window", counters);
  assertResolvedEventsMatch(expected, actual);
  TEST_ASSERT_FALSE(hasNoteIdOn(actual, fixture.deletedNoteId));
}

void test_stage4_shorten_matches_reconstruct() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();

  SessionMidiEventVec expectedEvents;
  fixture.passes.materializeToEventVector(expectedEvents, fixture.loopLengthTicks);
  const NoteUtils::DisplayNoteVec expectedNotes =
      NoteUtils::reconstructDisplayNotes(expectedEvents, fixture.loopLengthTicks, false);
  NoteUtils::DisplayNoteVec actualNotes;
  ResolutionCostCounters counters;
  LoopContentResolution::resolveNotes(fixture.passes, fixture.loopLengthTicks, 0,
                                      fixture.loopLengthTicks, actualNotes, &counters);
  printCounters("stage4_shorten_notes", counters);

  const NoteUtils::DisplayNote* expected = findNote(expectedNotes, fixture.shortenedNoteId);
  const NoteUtils::DisplayNote* actual = findNote(actualNotes, fixture.shortenedNoteId);
  TEST_ASSERT_NOT_NULL(expected);
  TEST_ASSERT_NOT_NULL(actual);
  TEST_ASSERT_EQUAL_UINT32(expected->startTick, actual->startTick);
  TEST_ASSERT_EQUAL_UINT32(expected->endTick, actual->endTick);
  TEST_ASSERT_EQUAL(expected->note, actual->note);
  TEST_ASSERT_EQUAL_UINT32(24u, actual->endTick - actual->startTick);
}

void test_stage4_extend_matches_reconstruct() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();
  fixture.passes.editPasses.push_back(
      makeLength(901, fixture.overlapHostNoteId, 0, 300));

  SessionMidiEventVec expectedEvents;
  fixture.passes.materializeToEventVector(expectedEvents, fixture.loopLengthTicks);
  const NoteUtils::DisplayNoteVec expectedNotes =
      NoteUtils::reconstructDisplayNotes(expectedEvents, fixture.loopLengthTicks, false);
  NoteUtils::DisplayNoteVec actualNotes;
  LoopContentResolution::resolveNotes(fixture.passes, fixture.loopLengthTicks, 0,
                                      fixture.loopLengthTicks, actualNotes);

  const NoteUtils::DisplayNote* expected = findNote(expectedNotes, fixture.overlapHostNoteId);
  const NoteUtils::DisplayNote* actual = findNote(actualNotes, fixture.overlapHostNoteId);
  TEST_ASSERT_NOT_NULL(expected);
  TEST_ASSERT_NOT_NULL(actual);
  TEST_ASSERT_EQUAL_UINT32(expected->startTick, actual->startTick);
  TEST_ASSERT_EQUAL_UINT32(expected->endTick, actual->endTick);
  TEST_ASSERT_EQUAL_UINT32(300u, actual->endTick);
}

void test_stage5_move_matches_reconstruct() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();
  // NoteRange apply moves ticks only. Pitch MOVE is EditPropertyType::Pitch (EditApply).
  EditPass pitchRow{};
  pitchRow.id = 900;
  pitchRow.passType = EditPassType::Note;
  pitchRow.actionType = EditActionType::Update;
  pitchRow.propertyType = EditPropertyType::Pitch;
  pitchRow.state = EditPassState::Active;
  pitchRow.targetNoteId = fixture.movedNoteId;
  pitchRow.pitch = 70;
  fixture.passes.editPasses.push_back(pitchRow);

  SessionMidiEventVec expectedEvents;
  fixture.passes.materializeToEventVector(expectedEvents, fixture.loopLengthTicks);
  const NoteUtils::DisplayNoteVec expectedNotes =
      NoteUtils::reconstructDisplayNotes(expectedEvents, fixture.loopLengthTicks, false);
  NoteUtils::DisplayNoteVec actualNotes;
  ResolutionCostCounters counters;
  LoopContentResolution::resolveNotes(fixture.passes, fixture.loopLengthTicks, 0,
                                      fixture.loopLengthTicks, actualNotes, &counters);
  printCounters("stage5_move_notes", counters);

  const NoteUtils::DisplayNote* expected = findNote(expectedNotes, fixture.movedNoteId);
  const NoteUtils::DisplayNote* actual = findNote(actualNotes, fixture.movedNoteId);
  TEST_ASSERT_NOT_NULL(expected);
  TEST_ASSERT_NOT_NULL(actual);
  TEST_ASSERT_EQUAL_UINT32(expected->startTick, actual->startTick);
  TEST_ASSERT_EQUAL_UINT32(expected->endTick, actual->endTick);
  TEST_ASSERT_EQUAL(expected->note, actual->note);
  TEST_ASSERT_EQUAL(70, actual->note);
  TEST_ASSERT_EQUAL_UINT32(400u, actual->startTick);
}

void test_stage_disabled_pass_excluded() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();
  TEST_ASSERT_FALSE(fixture.passes.overdubPasses.empty());
  fixture.passes.overdubPasses.back().state = CapturePassState::Disabled;

  SessionMidiEventVec expected;
  materializeSorted(fixture.passes, fixture.loopLengthTicks, expected);
  SessionMidiEventVec actual;
  LoopContentResolution::resolveWindow(fixture.passes, fixture.loopLengthTicks, 0,
                                       fixture.loopLengthTicks, actual);
  assertResolvedEventsMatch(expected, actual);
}

void test_stage_determinism_cold_warm() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();

  SessionMidiEventVec first;
  SessionMidiEventVec second;
  LoopContentResolution::resolveWindow(fixture.passes, fixture.loopLengthTicks, 0,
                                       fixture.loopLengthTicks, first);
  LoopContentResolution::resolveWindow(fixture.passes, fixture.loopLengthTicks,
                                       16u * Config::TICKS_PER_BAR, 16u * Config::TICKS_PER_BAR,
                                       second);
  second.clear();
  LoopContentResolution::resolveWindow(fixture.passes, fixture.loopLengthTicks, 0,
                                       fixture.loopLengthTicks, second);
  assertResolvedEventsMatch(first, second);
}

void test_stage6_commit_does_not_scan_prior_passes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();
  LoopContentResolution::TickIndex index;
  commitFixtureIndex(fixture, index);
  TEST_ASSERT_EQUAL_UINT32(1u + kCanonicalOverdubPasses, index.indexedPassCount());
  TEST_ASSERT_GREATER_THAN(0u, index.indexedEventCount());
}

void test_stage6_window_find_matches_materialize_filter() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();
  LoopContentResolution::TickIndex index;
  commitFixtureIndex(fixture, index);

  const uint32_t windowLength = kCanonicalQueryWindowBars * Config::TICKS_PER_BAR;
  SessionMidiEventVec expected;
  oracleWindowEvents(fixture.passes, fixture.loopLengthTicks, 0, windowLength, expected);

  ResolutionCostCounters counters;
  SessionMidiEventVec actual;
  LoopContentResolution::resolveWindow(index, fixture.passes.editPasses, fixture.loopLengthTicks, 0,
                                       windowLength, actual, &counters);
  printCounters("stage6_window", counters);
  TEST_ASSERT_EQUAL_UINT32(0u, counters.passChunkListsWalked);
  TEST_ASSERT_GREATER_THAN(0u, counters.indexEntriesVisited);
  TEST_ASSERT_LESS_THAN(counters.eventsInHistory, counters.indexEntriesVisited);
  assertResolvedEventsMatch(expected, actual);
  TEST_ASSERT_TRUE(hasNoteIdOn(actual, fixture.movedNoteId));
}

void test_stage6_wrap_window_matches_oracle() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();
  LoopContentResolution::TickIndex index;
  commitFixtureIndex(fixture, index);

  const uint32_t windowLength = kCanonicalQueryWindowBars * Config::TICKS_PER_BAR;
  const uint32_t windowStart = fixture.loopLengthTicks - (windowLength / 2u);
  SessionMidiEventVec expected;
  oracleWindowEvents(fixture.passes, fixture.loopLengthTicks, windowStart, windowLength, expected);

  ResolutionCostCounters counters;
  SessionMidiEventVec actual;
  LoopContentResolution::resolveWindow(index, fixture.passes.editPasses, fixture.loopLengthTicks,
                                       windowStart, windowLength, actual, &counters);
  printCounters("stage6_wrap_window", counters);
  TEST_ASSERT_EQUAL_UINT32(0u, counters.passChunkListsWalked);
  assertResolvedEventsMatch(expected, actual);
  TEST_ASSERT_TRUE(hasNoteIdOn(actual, fixture.wrapNoteId));
}

void test_stage6_disable_without_walking_pass_lists() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();
  LoopContentResolution::TickIndex index;
  commitFixtureIndex(fixture, index);
  TEST_ASSERT_FALSE(fixture.passes.overdubPasses.empty());
  index.setCapturePassState(fixture.passes.overdubPasses.back().id, CapturePassState::Disabled);
  fixture.passes.overdubPasses.back().state = CapturePassState::Disabled;

  SessionMidiEventVec expected;
  oracleWindowEvents(fixture.passes, fixture.loopLengthTicks, 0, fixture.loopLengthTicks, expected);
  ResolutionCostCounters counters;
  SessionMidiEventVec actual;
  LoopContentResolution::resolveWindow(index, fixture.passes.editPasses, fixture.loopLengthTicks, 0,
                                       fixture.loopLengthTicks, actual, &counters);
  TEST_ASSERT_EQUAL_UINT32(0u, counters.passChunkListsWalked);
  assertResolvedEventsMatch(expected, actual);
}

void test_stage6_note_spanning_two_chunks() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  resetNoteIdCounter(1);
  const uint32_t loopLength = 4u * Config::TICKS_PER_BAR;
  const NoteId spanId = 99;
  LoopEventStore store;
  const uint32_t fillerNotes = (LoopEventStoreConfig::CHUNK_CAPACITY - 1u) / 2u;
  for (uint32_t i = 0; i < fillerNotes; ++i) {
    const NoteId fillerId = static_cast<NoteId>(1000u + i);
    TEST_ASSERT_TRUE(storeAppendNoteOn(store, 2000u + i, 1, 10, 100, fillerId));
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(2100u + i, 1, 10, 0)));
  }
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(1, 1, 1, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 40, 1, 72, 100, spanId));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(80, 1, 72, 0)));
  CommittedChunkIdList ids;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, ids));
  TEST_ASSERT_GREATER_THAN(1u, ids.size());

  LoopPasses passes;
  passes.recordPass.id = 1;
  passes.recordPass.state = CapturePassState::Active;
  passes.recordPass.committedChunkIds = ids;

  LoopContentResolution::TickIndex index;
  ResolutionCostCounters commitCounters;
  index.commitCapturePass(1, ids, CapturePassState::Active, 0, &commitCounters);
  TEST_ASSERT_EQUAL_UINT32(1u, commitCounters.passChunkListsWalked);

  SessionMidiEventVec expected;
  oracleWindowEvents(passes, loopLength, 0, loopLength, expected);
  ResolutionCostCounters findCounters;
  SessionMidiEventVec actual;
  LoopContentResolution::resolveWindow(index, passes.editPasses, loopLength, 0, loopLength, actual,
                                       &findCounters);
  TEST_ASSERT_EQUAL_UINT32(0u, findCounters.passChunkListsWalked);
  assertResolvedEventsMatch(expected, actual);
  TEST_ASSERT_TRUE(hasNoteIdOn(actual, spanId));
}

void test_stage7_checkpoints_measured_interval() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();
  LoopContentResolution::TickIndex index;
  commitFixtureIndex(fixture, index);

  const uint32_t interval = Config::TICKS_PER_BAR;
  LoopContentResolution::StateCheckpoints checkpoints;
  ResolutionCostCounters counters;
  checkpoints.rebuild(index, fixture.passes.editPasses, fixture.loopLengthTicks, interval, &counters);
  printCounters("stage7_checkpoint_rebuild", counters);
  std::printf("stage7 interval_ticks=%u checkpoints=%u history_events=%u\n",
              counters.checkpointIntervalTicks, counters.checkpointCount, counters.eventsInHistory);
  TEST_ASSERT_EQUAL_UINT32(0u, counters.passChunkListsWalked);
  TEST_ASSERT_EQUAL_UINT32(interval, checkpoints.intervalTicks);
  TEST_ASSERT_EQUAL_UINT32(kCanonicalBars, counters.checkpointCount);
}

void test_stage7_resolve_state_from_checkpoint_not_tick_zero() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();
  LoopContentResolution::TickIndex index;
  commitFixtureIndex(fixture, index);

  const uint32_t interval = Config::TICKS_PER_BAR;
  LoopContentResolution::StateCheckpoints checkpoints;
  checkpoints.rebuild(index, fixture.passes.editPasses, fixture.loopLengthTicks, interval);

  const uint32_t highTick = fixture.loopLengthTicks - 24u;
  SoundingNoteVec expected;
  LoopContentResolution::resolveState(fixture.passes, fixture.loopLengthTicks, highTick, expected);
  ResolutionCostCounters counters;
  SoundingNoteVec actual;
  LoopContentResolution::resolveState(checkpoints, highTick, actual, &counters);
  printCounters("stage7_high_tick", counters);
  std::printf("stage7 replay_start=%u events_replayed=%u history_events=%u\n",
              counters.replayStartTick, counters.eventsReplayed, counters.eventsInHistory);
  TEST_ASSERT_EQUAL_UINT32(0u, counters.passChunkListsWalked);
  TEST_ASSERT_GREATER_THAN(0u, counters.replayStartTick);
  TEST_ASSERT_TRUE(highTick - counters.replayStartTick < interval);
  TEST_ASSERT_LESS_THAN(counters.eventsInHistory, counters.eventsReplayed);
  assertSoundingMatch(expected, actual);
  TEST_ASSERT_TRUE(hasSoundingNoteId(actual, fixture.wrapNoteId));
}

void test_stage8_loop_switch_high_tick_bounded_replay() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  const uint32_t interval = Config::TICKS_PER_BAR;
  const uint32_t playingLength = 4u * Config::TICKS_PER_BAR;
  const NoteId playingNoteId = 9000;
  LoopPasses playingPasses;
  playingPasses.recordPass.id = 100;
  playingPasses.recordPass.state = CapturePassState::Active;
  playingPasses.recordPass.committedChunkIds = makeNoteSpan(0, 200, 1, 50, playingNoteId);
  LoopContentResolution::TickIndex playingIndex;
  ResolutionCostCounters playingCommit;
  playingIndex.commitCapturePass(playingPasses.recordPass.id, playingPasses.recordPass.committedChunkIds,
                                 playingPasses.recordPass.state, 0, &playingCommit);
  TEST_ASSERT_EQUAL_UINT32(1u, playingCommit.passChunkListsWalked);
  LoopContentResolution::StateCheckpoints playingCheckpoints;
  playingCheckpoints.rebuild(playingIndex, playingPasses.editPasses, playingLength, interval);

  CanonicalResolutionFixture destination = buildCanonicalResolutionFixture();
  LoopContentResolution::TickIndex destinationIndex;
  commitFixtureIndex(destination, destinationIndex);
  LoopContentResolution::StateCheckpoints destinationCheckpoints;
  destinationCheckpoints.rebuild(destinationIndex, destination.passes.editPasses,
                                 destination.loopLengthTicks, interval);
  const uint32_t destCheckpointCount =
      static_cast<uint32_t>(destinationCheckpoints.soundingAt.size());
  const size_t destSpanCount = destinationCheckpoints.spans.size();
  TEST_ASSERT_EQUAL_UINT32(kCanonicalBars, destCheckpointCount);

  const uint32_t playheadTick = destination.loopLengthTicks - 24u;
  const uint32_t playingPhase =
      IntervalProjection::tickPhaseInLoop(playheadTick, 0, playingLength);
  const uint32_t destinationPhase =
      IntervalProjection::tickPhaseInLoop(playheadTick, 0, destination.loopLengthTicks);

  SoundingNoteVec playingExpected;
  SoundingNoteVec playingActual;
  LoopContentResolution::resolveState(playingPasses, playingLength, playingPhase, playingExpected);
  ResolutionCostCounters playingCounters;
  LoopContentResolution::resolveState(playingCheckpoints, playingPhase, playingActual,
                                      &playingCounters);
  TEST_ASSERT_EQUAL_UINT32(0u, playingCounters.passChunkListsWalked);
  assertSoundingMatch(playingExpected, playingActual);

  SoundingNoteVec destExpected;
  SoundingNoteVec destActual;
  LoopContentResolution::resolveState(destination.passes, destination.loopLengthTicks,
                                      destinationPhase, destExpected);
  ResolutionCostCounters switchCounters;
  LoopContentResolution::resolveState(destinationCheckpoints, destinationPhase, destActual,
                                      &switchCounters);
  printCounters("stage8_switch_to_destination", switchCounters);
  std::printf("stage8 replay_start=%u events_replayed=%u history_events=%u dest_phase=%u\n",
              switchCounters.replayStartTick, switchCounters.eventsReplayed,
              switchCounters.eventsInHistory, destinationPhase);
  TEST_ASSERT_EQUAL_UINT32(0u, switchCounters.passChunkListsWalked);
  TEST_ASSERT_GREATER_THAN(0u, switchCounters.replayStartTick);
  TEST_ASSERT_TRUE(destinationPhase - switchCounters.replayStartTick < interval);
  TEST_ASSERT_LESS_THAN(switchCounters.eventsInHistory, switchCounters.eventsReplayed);
  assertSoundingMatch(destExpected, destActual);
  TEST_ASSERT_TRUE(hasSoundingNoteId(destActual, destination.wrapNoteId));
  TEST_ASSERT_EQUAL_UINT32(destCheckpointCount,
                           static_cast<uint32_t>(destinationCheckpoints.soundingAt.size()));
  TEST_ASSERT_EQUAL(destSpanCount, destinationCheckpoints.spans.size());

  const uint32_t windowLength = kCanonicalQueryWindowBars * Config::TICKS_PER_BAR;
  const uint32_t windowStart = destination.loopLengthTicks - (windowLength / 2u);
  SessionMidiEventVec windowExpected;
  oracleWindowEvents(destination.passes, destination.loopLengthTicks, windowStart, windowLength,
                     windowExpected);
  ResolutionCostCounters windowCounters;
  SessionMidiEventVec windowActual;
  LoopContentResolution::resolveWindow(destinationIndex, destination.passes.editPasses,
                                       destination.loopLengthTicks, windowStart, windowLength,
                                       windowActual, &windowCounters);
  TEST_ASSERT_EQUAL_UINT32(0u, windowCounters.passChunkListsWalked);
  assertResolvedEventsMatch(windowExpected, windowActual);

  SoundingNoteVec backExpected;
  SoundingNoteVec backActual;
  LoopContentResolution::resolveState(playingPasses, playingLength, playingPhase, backExpected);
  ResolutionCostCounters backCounters;
  LoopContentResolution::resolveState(playingCheckpoints, playingPhase, backActual, &backCounters);
  TEST_ASSERT_EQUAL_UINT32(0u, backCounters.passChunkListsWalked);
  assertSoundingMatch(backExpected, backActual);
}

void test_stage7_resolve_state_matches_oracle_mid_and_wrap() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();
  LoopContentResolution::TickIndex index;
  commitFixtureIndex(fixture, index);
  LoopContentResolution::StateCheckpoints checkpoints;
  checkpoints.rebuild(index, fixture.passes.editPasses, fixture.loopLengthTicks,
                      Config::TICKS_PER_BAR);

  const uint32_t ticks[] = {10u, 100u, 201u, fixture.loopLengthTicks - 24u};
  for (uint32_t tick : ticks) {
    SoundingNoteVec expected;
    SoundingNoteVec actual;
    LoopContentResolution::resolveState(fixture.passes, fixture.loopLengthTicks, tick, expected);
    LoopContentResolution::resolveState(checkpoints, tick, actual);
    assertSoundingMatch(expected, actual);
  }
}

void test_stage7_sparse_checkpoints_agree_with_dense() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();
  LoopContentResolution::TickIndex index;
  commitFixtureIndex(fixture, index);

  const uint32_t denseInterval =
      Config::TICKS_PER_BAR * LoopContentResolution::kNativeCheckpointBarStride;
  const uint32_t sparseInterval =
      Config::TICKS_PER_BAR * LoopContentResolution::kDeviceCheckpointBarStride;
  LoopContentResolution::StateCheckpoints dense;
  LoopContentResolution::StateCheckpoints sparse;
  dense.rebuild(index, fixture.passes.editPasses, fixture.loopLengthTicks, denseInterval);
  sparse.rebuild(index, fixture.passes.editPasses, fixture.loopLengthTicks, sparseInterval);

  TEST_ASSERT_EQUAL_UINT32(kCanonicalBars, static_cast<uint32_t>(dense.soundingAt.size()));
  TEST_ASSERT_EQUAL_UINT32(kCanonicalBars / LoopContentResolution::kDeviceCheckpointBarStride,
                           static_cast<uint32_t>(sparse.soundingAt.size()));
  TEST_ASSERT_TRUE(sparse.soundingAt.size() < dense.soundingAt.size());
  TEST_ASSERT_EQUAL(dense.spans.size(), sparse.spans.size());

  const uint32_t ticks[] = {10u, 100u, 201u, fixture.loopLengthTicks / 2u,
                            fixture.loopLengthTicks - 24u};
  for (uint32_t tick : ticks) {
    SoundingNoteVec fromDense;
    SoundingNoteVec fromSparse;
    SoundingNoteVec fromOracle;
    LoopContentResolution::resolveState(dense, tick, fromDense);
    LoopContentResolution::resolveState(sparse, tick, fromSparse);
    LoopContentResolution::resolveState(fixture.passes, fixture.loopLengthTicks, tick, fromOracle);
    assertSoundingMatch(fromOracle, fromDense);
    assertSoundingMatch(fromOracle, fromSparse);
  }
}

void test_stage7_loop_shorter_than_device_stride_keeps_one_checkpoint() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  const uint32_t loopLength = 4u * Config::TICKS_PER_BAR;
  const uint32_t interval =
      Config::TICKS_PER_BAR * LoopContentResolution::kDeviceCheckpointBarStride;
  TEST_ASSERT_TRUE(loopLength < interval);

  LoopPasses passes;
  passes.recordPass.id = 1;
  passes.recordPass.state = CapturePassState::Active;
  passes.recordPass.committedChunkIds = makeNoteSpan(96, 192, 0, 60, 1);

  LoopContentResolution::TickIndex index;
  index.commitCapturePass(passes.recordPass.id, passes.recordPass.committedChunkIds,
                          passes.recordPass.state, 0, nullptr);

  LoopContentResolution::StateCheckpoints checkpoints;
  checkpoints.rebuild(index, passes.editPasses, loopLength, interval);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(checkpoints.soundingAt.size()));

  const uint32_t ticks[] = {0u, 100u, 150u, loopLength - 1u};
  for (uint32_t tick : ticks) {
    SoundingNoteVec expected;
    SoundingNoteVec actual;
    LoopContentResolution::resolveState(passes, loopLength, tick, expected);
    LoopContentResolution::resolveState(checkpoints, tick, actual);
    assertSoundingMatch(expected, actual);
  }
}

void commitPassOneEventPerSlice(LoopContentResolution::TickIndex& index, PassId id,
                                const CommittedChunkIdList& chunks, CapturePassState state,
                                uint32_t mergeSequence) {
  index.beginCapturePass(id, state, mergeSequence);
  for (uint16_t chunkId : chunks) {
    index.appendCapturePassChunk(id, chunkId);
  }
  TEST_ASSERT_FALSE(index.capturePasses.empty());
  const uint32_t eventCount = static_cast<uint32_t>(index.capturePasses.back().events.size());
  for (uint32_t i = 0; i < eventCount; ++i) {
    index.indexCapturePassEventRange(id, i, i + 1, nullptr);
  }
  std::map<uint8_t, std::vector<uint32_t>> openOnByPitch;
  for (uint32_t i = 0; i < eventCount; ++i) {
    index.pairCapturePassEventRange(id, i, i + 1, openOnByPitch, nullptr);
  }
}

void test_stage9_sliced_index_commit_matches_full_commit() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();

  LoopContentResolution::TickIndex full;
  commitFixtureIndex(fixture, full);

  LoopContentResolution::TickIndex sliced;
  commitPassOneEventPerSlice(sliced, fixture.passes.recordPass.id,
                             fixture.passes.recordPass.committedChunkIds,
                             fixture.passes.recordPass.state, 0);
  for (const OverdubPass& pass : fixture.passes.overdubPasses) {
    commitPassOneEventPerSlice(sliced, pass.id, pass.committedChunkIds, pass.state,
                               pass.mergeSequence);
  }

  TEST_ASSERT_EQUAL_UINT32(full.indexedEventCount(), sliced.indexedEventCount());
  TEST_ASSERT_EQUAL_UINT32(full.indexedPassCount(), sliced.indexedPassCount());

  const uint32_t windowLength = kCanonicalQueryWindowBars * Config::TICKS_PER_BAR;
  SessionMidiEventVec fromFull;
  SessionMidiEventVec fromSliced;
  LoopContentResolution::resolveWindow(full, fixture.passes.editPasses, fixture.loopLengthTicks, 0,
                                       windowLength, fromFull, nullptr);
  LoopContentResolution::resolveWindow(sliced, fixture.passes.editPasses, fixture.loopLengthTicks, 0,
                                       windowLength, fromSliced, nullptr);
  assertResolvedEventsMatch(fromFull, fromSliced);
}

void test_stage9_sliced_spans_match_full_rebuild() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();
  LoopContentResolution::TickIndex index;
  commitFixtureIndex(fixture, index);

  const uint32_t interval =
      Config::TICKS_PER_BAR * LoopContentResolution::kNativeCheckpointBarStride;
  LoopContentResolution::StateCheckpoints full;
  full.rebuild(index, fixture.passes.editPasses, fixture.loopLengthTicks, interval);

  LoopContentResolution::StateCheckpoints sliced;
  SessionMidiEventVec resolved;
  TEST_ASSERT_TRUE(sliced.prepareRebuildResolvedEvents(
      index, fixture.passes.editPasses, fixture.loopLengthTicks, interval, resolved, nullptr));
  const NoteUtils::DisplayNoteVec notes =
      NoteUtils::reconstructDisplayNotes(resolved, fixture.loopLengthTicks, false);
  sliced.spans.clear();
  sliced.startsByTick.clear();
  sliced.soundingAt.clear();
  for (uint32_t i = 0; i < static_cast<uint32_t>(notes.size()); ++i) {
    TEST_ASSERT_TRUE(sliced.appendSpansFromNotes(resolved, notes, i, i + 1, nullptr));
  }
  uint32_t count = sliced.loopLengthTicks / sliced.intervalTicks;
  if (count == 0) {
    count = 1;
  }
  sliced.soundingAt.resize(count);
  TEST_ASSERT_TRUE(sliced.fillCheckpointRange(0, count, nullptr));
  TEST_ASSERT_EQUAL(full.spans.size(), sliced.spans.size());

  const uint32_t ticks[] = {10u, 100u, 201u, fixture.loopLengthTicks - 24u};
  for (uint32_t tick : ticks) {
    SoundingNoteVec fromFull;
    SoundingNoteVec fromSliced;
    LoopContentResolution::resolveState(full, tick, fromFull);
    LoopContentResolution::resolveState(sliced, tick, fromSliced);
    assertSoundingMatch(fromFull, fromSliced);
  }
}

void commitPassEventsPerSlice(LoopContentResolution::TickIndex& index, PassId id,
                              const CommittedChunkIdList& chunks, CapturePassState state,
                              uint32_t mergeSequence) {
  index.beginCapturePass(id, state, mergeSequence);
  for (uint16_t chunkId : chunks) {
    index.appendCapturePassChunk(id, chunkId);
  }
  TEST_ASSERT_FALSE(index.capturePasses.empty());
  const uint32_t eventCount = static_cast<uint32_t>(index.capturePasses.back().events.size());
  const uint32_t step = LoopContentResolution::kDeviceGateEventsPerSlice;
  for (uint32_t i = 0; i < eventCount; i += step) {
    const uint32_t end = std::min(i + step, eventCount);
    index.indexCapturePassEventRange(id, i, end, nullptr);
  }
  std::map<uint8_t, std::vector<uint32_t>> openOnByPitch;
  for (uint32_t i = 0; i < eventCount; i += step) {
    const uint32_t end = std::min(i + step, eventCount);
    index.pairCapturePassEventRange(id, i, end, openOnByPitch, nullptr);
  }
}

void assertNoteLocationsMatch(const LoopContentResolution::TickIndex& expected,
                              const LoopContentResolution::TickIndex& actual) {
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(expected.byNoteId.size()),
                           static_cast<uint32_t>(actual.byNoteId.size()));
  for (const auto& entry : expected.byNoteId) {
    const auto found = actual.byNoteId.find(entry.first);
    TEST_ASSERT_TRUE(found != actual.byNoteId.end());
    TEST_ASSERT_EQUAL_UINT32(entry.second.passId, found->second.passId);
    TEST_ASSERT_EQUAL_UINT32(entry.second.onIndex, found->second.onIndex);
    TEST_ASSERT_EQUAL_INT32(entry.second.offIndex, found->second.offIndex);
  }
}

void test_stage9_range_pair_matches_full_pair() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();

  LoopContentResolution::TickIndex full;
  commitFixtureIndex(fixture, full);

  LoopContentResolution::TickIndex sliced;
  commitPassEventsPerSlice(sliced, fixture.passes.recordPass.id,
                           fixture.passes.recordPass.committedChunkIds,
                           fixture.passes.recordPass.state, 0);
  for (const OverdubPass& pass : fixture.passes.overdubPasses) {
    commitPassEventsPerSlice(sliced, pass.id, pass.committedChunkIds, pass.state,
                             pass.mergeSequence);
  }
  assertNoteLocationsMatch(full, sliced);
}

void test_stage9_range_recon_matches_full() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();
  SessionMidiEventVec events;
  fixture.passes.materializeToEventVector(events, fixture.loopLengthTicks);
  const NoteUtils::DisplayNoteVec full =
      NoteUtils::reconstructDisplayNotes(events, fixture.loopLengthTicks, false);

  NoteUtils::CanonicalSpanBuild build;
  const uint32_t eventCount = static_cast<uint32_t>(events.size());
  const uint32_t step = LoopContentResolution::kDeviceGateEventsPerSlice;
  for (uint32_t i = 0; i < eventCount; i += step) {
    const uint32_t end = std::min(i + step, eventCount);
    NoteUtils::appendCanonicalSpansFromMidi(events, fixture.loopLengthTicks, i, end, build);
  }
  NoteUtils::finishCanonicalSpansFromMidi(fixture.loopLengthTicks, build);
  const NoteUtils::DisplayNoteVec sliced =
      NoteUtils::displayNotesFromCanonicalSpans(build, fixture.loopLengthTicks);

  TEST_ASSERT_EQUAL(full.size(), sliced.size());
  for (size_t i = 0; i < full.size(); ++i) {
    TEST_ASSERT_EQUAL_UINT8(full[i].note, sliced[i].note);
    TEST_ASSERT_EQUAL_UINT32(full[i].startTick, sliced[i].startTick);
    TEST_ASSERT_EQUAL_UINT32(full[i].endTick, sliced[i].endTick);
    TEST_ASSERT_EQUAL_UINT32(full[i].noteId, sliced[i].noteId);
  }
}

void test_stage9_range_proj_matches_full() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();
  SessionMidiEventVec events;
  fixture.passes.materializeToEventVector(events, fixture.loopLengthTicks);
  const NoteUtils::DisplayNoteVec full =
      NoteUtils::reconstructDisplayNotes(events, fixture.loopLengthTicks, false);

  NoteUtils::CanonicalSpanBuild build;
  const uint32_t eventCount = static_cast<uint32_t>(events.size());
  const uint32_t step = LoopContentResolution::kDeviceGateEventsPerSlice;
  for (uint32_t i = 0; i < eventCount; i += step) {
    const uint32_t end = std::min(i + step, eventCount);
    NoteUtils::appendCanonicalSpansFromMidi(events, fixture.loopLengthTicks, i, end, build);
  }
  NoteUtils::finishCanonicalSpansFromMidi(fixture.loopLengthTicks, build);

  NoteUtils::DisplayNoteVec projected;
  const uint32_t spanCount = build.spanCount();
  for (uint32_t i = 0; i < spanCount; i += step) {
    const uint32_t end = std::min(i + step, spanCount);
    NoteUtils::appendProjectedDisplayNotes(build, fixture.loopLengthTicks, i, end, projected);
  }
  const NoteUtils::DisplayNoteVec sliced = NoteUtils::dedupeProjectedDisplayNotes(projected);

  TEST_ASSERT_EQUAL(full.size(), sliced.size());
  for (size_t i = 0; i < full.size(); ++i) {
    TEST_ASSERT_EQUAL_UINT8(full[i].note, sliced[i].note);
    TEST_ASSERT_EQUAL_UINT32(full[i].startTick, sliced[i].startTick);
    TEST_ASSERT_EQUAL_UINT32(full[i].endTick, sliced[i].endTick);
    TEST_ASSERT_EQUAL_UINT32(full[i].noteId, sliced[i].noteId);
  }
}

void test_stage9_range_index_matches_one_event() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();

  LoopContentResolution::TickIndex oneEvent;
  commitPassOneEventPerSlice(oneEvent, fixture.passes.recordPass.id,
                             fixture.passes.recordPass.committedChunkIds,
                             fixture.passes.recordPass.state, 0);
  for (const OverdubPass& pass : fixture.passes.overdubPasses) {
    commitPassOneEventPerSlice(oneEvent, pass.id, pass.committedChunkIds, pass.state,
                               pass.mergeSequence);
  }

  LoopContentResolution::TickIndex batched;
  commitPassEventsPerSlice(batched, fixture.passes.recordPass.id,
                           fixture.passes.recordPass.committedChunkIds,
                           fixture.passes.recordPass.state, 0);
  for (const OverdubPass& pass : fixture.passes.overdubPasses) {
    commitPassEventsPerSlice(batched, pass.id, pass.committedChunkIds, pass.state,
                             pass.mergeSequence);
  }

  TEST_ASSERT_EQUAL_UINT32(oneEvent.indexedEventCount(), batched.indexedEventCount());
  TEST_ASSERT_EQUAL_UINT32(oneEvent.indexedPassCount(), batched.indexedPassCount());

  const uint32_t windowLength = kCanonicalQueryWindowBars * Config::TICKS_PER_BAR;
  SessionMidiEventVec fromOne;
  SessionMidiEventVec fromBatched;
  LoopContentResolution::resolveWindow(oneEvent, fixture.passes.editPasses, fixture.loopLengthTicks,
                                       0, windowLength, fromOne, nullptr);
  LoopContentResolution::resolveWindow(batched, fixture.passes.editPasses, fixture.loopLengthTicks,
                                       0, windowLength, fromBatched, nullptr);
  assertResolvedEventsMatch(fromOne, fromBatched);
}

void test_stage9_range_spans_match_one_span() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();
  LoopContentResolution::TickIndex index;
  commitFixtureIndex(fixture, index);

  const uint32_t interval =
      Config::TICKS_PER_BAR * LoopContentResolution::kNativeCheckpointBarStride;
  SessionMidiEventVec resolved;
  LoopContentResolution::StateCheckpoints oneSpan;
  TEST_ASSERT_TRUE(oneSpan.prepareRebuildResolvedEvents(
      index, fixture.passes.editPasses, fixture.loopLengthTicks, interval, resolved, nullptr));
  const NoteUtils::DisplayNoteVec notes =
      NoteUtils::reconstructDisplayNotes(resolved, fixture.loopLengthTicks, false);
  oneSpan.spans.clear();
  oneSpan.startsByTick.clear();
  oneSpan.soundingAt.clear();
  for (uint32_t i = 0; i < static_cast<uint32_t>(notes.size()); ++i) {
    TEST_ASSERT_TRUE(oneSpan.appendSpansFromNotes(resolved, notes, i, i + 1, nullptr));
  }

  LoopContentResolution::StateCheckpoints batched;
  TEST_ASSERT_TRUE(batched.prepareRebuildResolvedEvents(
      index, fixture.passes.editPasses, fixture.loopLengthTicks, interval, resolved, nullptr));
  batched.spans.clear();
  batched.startsByTick.clear();
  batched.soundingAt.clear();
  const uint32_t step = LoopContentResolution::kDeviceGateEventsPerSlice;
  const uint32_t noteCount = static_cast<uint32_t>(notes.size());
  for (uint32_t i = 0; i < noteCount; i += step) {
    const uint32_t end = std::min(i + step, noteCount);
    TEST_ASSERT_TRUE(batched.appendSpansFromNotes(resolved, notes, i, end, nullptr));
  }
  TEST_ASSERT_EQUAL(oneSpan.spans.size(), batched.spans.size());
}

void test_stage9_device_gate_slice_budget_matches_idle_maint_bar() {
  TEST_ASSERT_EQUAL_UINT32(50000u, LoopContentResolution::kDeviceGateSliceBudgetUs);
  TEST_ASSERT_EQUAL_UINT32(8u, LoopContentResolution::kDeviceGateEventsPerSlice);
  TEST_ASSERT_EQUAL_UINT32(1000000u, LoopContentResolution::kDeviceGatePhaseLogIntervalUs);
}

void test_stage9_phase_line_on_change_not_every_slice() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();
  LoopContentResolution::deviceGateReset();
  LoopContentResolution::deviceGateBegin(fixture.loopLengthTicks);
  char line[128];
  TEST_ASSERT_TRUE(LoopContentResolution::deviceGateFormatPhaseLine(line, sizeof(line)));
  TEST_ASSERT_NOT_NULL(std::strstr(line, "DIAG,lcr,phase,idx"));
  TEST_ASSERT_FALSE(LoopContentResolution::deviceGateFormatPhaseLine(line, sizeof(line)));
  LoopContentResolution::deviceGateReset();
}

void test_stage9_native_worst_case_micros() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();
  LoopContentResolution::DeviceGateSample sample;
  LoopContentResolution::measureDeviceGate(fixture.passes, fixture.loopLengthTicks, sample);
  printCounters("stage9_index_commit", sample.indexCommit);
  printCounters("stage9_materialize", sample.materialize);
  printCounters("stage9_window", sample.window);
  printCounters("stage9_rebuild", sample.rebuild);
  printCounters("stage9_state", sample.state);
  std::printf(
      "stage9 us materialize=%llu window=%llu rebuild=%llu state=%llu replay_start=%u "
      "replayed=%u history=%u\n",
      static_cast<unsigned long long>(sample.materialize.elapsedMicros),
      static_cast<unsigned long long>(sample.window.elapsedMicros),
      static_cast<unsigned long long>(sample.rebuild.elapsedMicros),
      static_cast<unsigned long long>(sample.state.elapsedMicros), sample.state.replayStartTick,
      sample.state.eventsReplayed, sample.state.eventsInHistory);
  TEST_ASSERT_EQUAL_UINT32(0u, sample.window.passChunkListsWalked);
  TEST_ASSERT_EQUAL_UINT32(0u, sample.state.passChunkListsWalked);
  TEST_ASSERT_GREATER_THAN(0u, sample.state.replayStartTick);
  TEST_ASSERT_LESS_THAN(sample.state.eventsInHistory, sample.state.eventsReplayed);
  TEST_ASSERT_GREATER_THAN(0u, sample.materialize.elapsedMicros);
  TEST_ASSERT_GREATER_THAN(0u, sample.window.elapsedMicros);
}

uint64_t elapsedMicrosSince(Clock::time_point start) {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count());
}

void rebuildStartsByTickFromSpans(LoopContentResolution::StateCheckpoints& checkpoints) {
  checkpoints.startsByTick.clear();
  for (size_t i = 0; i < checkpoints.spans.size(); ++i) {
    checkpoints.startsByTick.emplace(checkpoints.spans[i].startTick, i);
    checkpoints.startsByTick.emplace(checkpoints.spans[i].endTick, i);
  }
}

uint32_t countEqualTickPairs(
    const LoopContentResolution::StateCheckpoints::SpanBoundaryEntryVec& entries) {
  uint32_t pairs = 0;
  for (size_t i = 1; i < entries.size(); ++i) {
    if (entries[i].tick == entries[i - 1].tick) {
      pairs += 1;
    }
  }
  return pairs;
}

void test_stage515b_equal_tick_boundary_order() {
  LoopContentResolution::StateCheckpoints checkpoints;
  checkpoints.intervalTicks = Config::TICKS_PER_BAR;
  checkpoints.loopLengthTicks = 4u * Config::TICKS_PER_BAR;
  checkpoints.soundingAt.resize(4);

  LoopContentResolution::StateCheckpoints::NoteSpan ending{};
  ending.note.channel = 1;
  ending.note.pitch = 60;
  ending.note.noteId = 7;
  ending.note.onTick = 0;
  ending.startTick = 0;
  ending.endTick = 100;

  LoopContentResolution::StateCheckpoints::NoteSpan starting{};
  starting.note.channel = 1;
  starting.note.pitch = 61;
  starting.note.noteId = 7;
  starting.note.onTick = 100;
  starting.startTick = 100;
  starting.endTick = 200;

  checkpoints.spans.push_back(ending);
  checkpoints.spans.push_back(starting);
  checkpoints.soundingAt[0].push_back(ending.note);
  rebuildStartsByTickFromSpans(checkpoints);

  LoopContentResolution::StateCheckpoints::SpanBoundaryEntryVec flat;
  LoopContentResolution::StateCheckpoints::appendSpanBoundaryEntries(
      checkpoints.spans, 0, static_cast<uint32_t>(checkpoints.spans.size()), flat);
  TEST_ASSERT_EQUAL_UINT32(4u, static_cast<uint32_t>(flat.size()));
  TEST_ASSERT_EQUAL_UINT32(100u, flat[1].tick);
  TEST_ASSERT_EQUAL(0u, flat[1].spanIndex);
  TEST_ASSERT_EQUAL_UINT32(100u, flat[2].tick);
  TEST_ASSERT_EQUAL(1u, flat[2].spanIndex);
  LoopContentResolution::StateCheckpoints::sortSpanBoundaryEntriesByTick(flat);
  TEST_ASSERT_EQUAL_UINT32(1u, countEqualTickPairs(flat));
  TEST_ASSERT_EQUAL(0u, flat[1].spanIndex);
  TEST_ASSERT_EQUAL(1u, flat[2].spanIndex);

  const uint32_t ticks[] = {99u, 100u, 101u};
  for (uint32_t tick : ticks) {
    SoundingNoteVec fromMap;
    SoundingNoteVec fromFlat;
    ResolutionCostCounters mapCounters;
    ResolutionCostCounters flatCounters;
    checkpoints.resolveState(tick, fromMap, &mapCounters);
    checkpoints.resolveStateFromSpanBoundaries(flat, tick, fromFlat, &flatCounters);
    TEST_ASSERT_EQUAL_UINT32(0u, mapCounters.passChunkListsWalked);
    TEST_ASSERT_EQUAL_UINT32(0u, flatCounters.passChunkListsWalked);
    TEST_ASSERT_EQUAL_UINT32(mapCounters.eventsReplayed, flatCounters.eventsReplayed);
    assertSoundingMatch(fromMap, fromFlat);
  }

  SoundingNoteVec atJoin;
  checkpoints.resolveStateFromSpanBoundaries(flat, 100u, atJoin, nullptr);
  TEST_ASSERT_EQUAL(1u, atJoin.size());
  TEST_ASSERT_EQUAL(starting.note.noteId, atJoin[0].noteId);
  TEST_ASSERT_EQUAL(starting.note.pitch, atJoin[0].pitch);
}

void test_stage515b_flat_span_boundaries_match_map() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();
  LoopContentResolution::TickIndex index;
  commitFixtureIndex(fixture, index);
  LoopContentResolution::StateCheckpoints checkpoints;
  checkpoints.rebuild(index, fixture.passes.editPasses, fixture.loopLengthTicks,
                      Config::TICKS_PER_BAR);
  TEST_ASSERT_TRUE(checkpoints.spans.size() > 0);
  TEST_ASSERT_EQUAL(checkpoints.startsByTick.size(), checkpoints.spans.size() * 2u);

  const Clock::time_point cStart = Clock::now();
  rebuildStartsByTickFromSpans(checkpoints);
  const uint64_t cEmplaceUs = elapsedMicrosSince(cStart);

  LoopContentResolution::StateCheckpoints::SpanBoundaryEntryVec flat;
  const Clock::time_point appendStart = Clock::now();
  LoopContentResolution::StateCheckpoints::appendSpanBoundaryEntries(
      checkpoints.spans, 0, static_cast<uint32_t>(checkpoints.spans.size()), flat);
  const uint64_t appendUs = elapsedMicrosSince(appendStart);
  const Clock::time_point sortStart = Clock::now();
  LoopContentResolution::StateCheckpoints::sortSpanBoundaryEntriesByTick(flat);
  const uint64_t sortUs = elapsedMicrosSince(sortStart);
  const uint64_t indexTotalUs = appendUs + sortUs;

  TEST_ASSERT_EQUAL(checkpoints.startsByTick.size(), flat.size());
  const uint32_t equalTickPairs = countEqualTickPairs(flat);
  TEST_ASSERT_GREATER_THAN(0u, equalTickPairs);

  const uint32_t ticks[] = {10u, 100u, 201u, fixture.loopLengthTicks / 2u,
                            fixture.loopLengthTicks - 24u};
  uint64_t mapResolveUs = 0;
  uint64_t flatResolveUs = 0;
  for (uint32_t tick : ticks) {
    SoundingNoteVec fromMap;
    SoundingNoteVec fromFlat;
    SoundingNoteVec fromOracle;
    ResolutionCostCounters mapCounters;
    ResolutionCostCounters flatCounters;
    const Clock::time_point mapStart = Clock::now();
    checkpoints.resolveState(tick, fromMap, &mapCounters);
    mapResolveUs += elapsedMicrosSince(mapStart);
    const Clock::time_point flatStart = Clock::now();
    checkpoints.resolveStateFromSpanBoundaries(flat, tick, fromFlat, &flatCounters);
    flatResolveUs += elapsedMicrosSince(flatStart);
    LoopContentResolution::resolveState(fixture.passes, fixture.loopLengthTicks, tick, fromOracle);
    TEST_ASSERT_EQUAL_UINT32(0u, mapCounters.passChunkListsWalked);
    TEST_ASSERT_EQUAL_UINT32(0u, flatCounters.passChunkListsWalked);
    TEST_ASSERT_EQUAL_UINT32(mapCounters.eventsReplayed, flatCounters.eventsReplayed);
    assertSoundingMatch(fromOracle, fromMap);
    assertSoundingMatch(fromMap, fromFlat);
  }

  std::printf(
      "stage515b C_emplace_us=%llu A_append_us=%llu A_sort_us=%llu A_index_total_us=%llu "
      "C_resolve_us=%llu A_resolve_us=%llu entries=%u equal_tick_pairs=%u spans=%u\n",
      static_cast<unsigned long long>(cEmplaceUs), static_cast<unsigned long long>(appendUs),
      static_cast<unsigned long long>(sortUs), static_cast<unsigned long long>(indexTotalUs),
      static_cast<unsigned long long>(mapResolveUs), static_cast<unsigned long long>(flatResolveUs),
      static_cast<unsigned>(flat.size()), equalTickPairs,
      static_cast<unsigned>(checkpoints.spans.size()));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_canonical_fixture_inventory);
  RUN_TEST(test_canonical_fixture_oracle_materialize);
  RUN_TEST(test_canonical_fixture_wrap_and_edits_in_oracle);
  RUN_TEST(test_stage1_one_pass_window_matches_materialize);
  RUN_TEST(test_stage1_resolve_state_during_host_note);
  RUN_TEST(test_stage1_resolve_state_wrap_note);
  RUN_TEST(test_stage1_resolve_notes_is_projection_of_window);
  RUN_TEST(test_stage2_overlapping_same_pitch_matches_materialize);
  RUN_TEST(test_stage3_delete_matches_materialize);
  RUN_TEST(test_stage4_shorten_matches_reconstruct);
  RUN_TEST(test_stage4_extend_matches_reconstruct);
  RUN_TEST(test_stage5_move_matches_reconstruct);
  RUN_TEST(test_stage_disabled_pass_excluded);
  RUN_TEST(test_stage_determinism_cold_warm);
  RUN_TEST(test_stage6_commit_does_not_scan_prior_passes);
  RUN_TEST(test_stage6_window_find_matches_materialize_filter);
  RUN_TEST(test_stage6_wrap_window_matches_oracle);
  RUN_TEST(test_stage6_disable_without_walking_pass_lists);
  RUN_TEST(test_stage6_note_spanning_two_chunks);
  RUN_TEST(test_stage7_checkpoints_measured_interval);
  RUN_TEST(test_stage7_resolve_state_from_checkpoint_not_tick_zero);
  RUN_TEST(test_stage7_resolve_state_matches_oracle_mid_and_wrap);
  RUN_TEST(test_stage7_sparse_checkpoints_agree_with_dense);
  RUN_TEST(test_stage7_loop_shorter_than_device_stride_keeps_one_checkpoint);
  RUN_TEST(test_stage8_loop_switch_high_tick_bounded_replay);
  RUN_TEST(test_stage9_sliced_index_commit_matches_full_commit);
  RUN_TEST(test_stage9_sliced_spans_match_full_rebuild);
  RUN_TEST(test_stage9_range_index_matches_one_event);
  RUN_TEST(test_stage9_range_pair_matches_full_pair);
  RUN_TEST(test_stage9_range_recon_matches_full);
  RUN_TEST(test_stage9_range_proj_matches_full);
  RUN_TEST(test_stage9_range_spans_match_one_span);
  RUN_TEST(test_stage9_device_gate_slice_budget_matches_idle_maint_bar);
  RUN_TEST(test_stage9_phase_line_on_change_not_every_slice);
  RUN_TEST(test_stage9_native_worst_case_micros);
  RUN_TEST(test_stage515b_equal_tick_boundary_order);
  RUN_TEST(test_stage515b_flat_span_boundaries_match_map);
  return UNITY_END();
}
