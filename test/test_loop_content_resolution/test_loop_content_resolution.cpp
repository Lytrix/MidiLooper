//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// DEC-037 Stage 0 — canonical stress fixture + materialize oracle.

#include <unity.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
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
#include "EditApply.h"
#include "EditSessionInteraction.h"
#include "Globals.h"
#include "LoopContentResolution.h"
#include "PendingNoteChange.h"
#include "ResolveConstrainedGeometry.h"
#include "../../src/LoopContentResolution.cpp"
#include "../../src/EditManager/EditSessionLiveStoreSpan.cpp"
#include "../../src/EditManager/NoteEditCurrentState.cpp"
#include "../../src/EditManager/EditSessionInteraction.cpp"
#include "../../src/EditManager/ResolveConstrainedGeometry.cpp"
#include "../../src/EditManager/ParticipatingNoteSession.cpp"
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

void sortDisplayNotes(NoteUtils::DisplayNoteVec& notes) {
  std::sort(notes.begin(), notes.end(), [](const NoteUtils::DisplayNote& a,
                                           const NoteUtils::DisplayNote& b) {
    if (a.noteId != b.noteId) {
      return a.noteId < b.noteId;
    }
    if (a.startTick != b.startTick) {
      return a.startTick < b.startTick;
    }
    return a.endTick < b.endTick;
  });
}

void assertDisplayNotesMatch(NoteUtils::DisplayNoteVec expected, NoteUtils::DisplayNoteVec actual) {
  sortDisplayNotes(expected);
  sortDisplayNotes(actual);
  TEST_ASSERT_EQUAL(expected.size(), actual.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    TEST_ASSERT_EQUAL(expected[i].noteId, actual[i].noteId);
    TEST_ASSERT_EQUAL(expected[i].note, actual[i].note);
    TEST_ASSERT_EQUAL_UINT32(expected[i].startTick, actual[i].startTick);
    TEST_ASSERT_EQUAL_UINT32(expected[i].endTick, actual[i].endTick);
  }
}

void oracleWindowNotes(const LoopPasses& passes, uint32_t loopLengthTicks, uint32_t windowStart,
                       uint32_t windowLength, NoteUtils::DisplayNoteVec& out) {
  SessionMidiEventVec events;
  oracleWindowEvents(passes, loopLengthTicks, windowStart, windowLength, events);
  out = NoteUtils::reconstructDisplayNotes(events, loopLengthTicks, false);
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

void fillChannelByNoteIdIndex(LoopContentResolution::StateCheckpoints& checkpoints,
                              const SessionMidiEventVec& resolved) {
  const uint32_t limit = static_cast<uint32_t>(resolved.size());
  const uint32_t step = LoopContentResolution::kDeviceGateEventsPerSlice;
  for (uint32_t i = 0; i < limit; i += step) {
    const uint32_t end = std::min(i + step, limit);
    LoopContentResolution::StateCheckpoints::appendChannelByNoteIdEntries(
        resolved, i, end, checkpoints.channelByNoteId);
  }
  LoopContentResolution::StateCheckpoints::sortAndUniqueChannelByNoteIdEntries(
      checkpoints.channelByNoteId);
}

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

void resolveWindowNotes(const LoopContentResolution::TickIndex& index, const LoopPasses& passes,
                        uint32_t loopLengthTicks, uint32_t windowStart, uint32_t windowLength,
                        NoteUtils::DisplayNoteVec& out, ResolutionCostCounters* counters) {
  SessionMidiEventVec events;
  LoopContentResolution::resolveWindow(index, passes.editPasses, loopLengthTicks, windowStart,
                                       windowLength, events, counters);
  out = NoteUtils::reconstructDisplayNotes(events, loopLengthTicks, false);
}

void test_stage6a_one_bar_notes_match_oracle() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();
  LoopContentResolution::TickIndex index;
  commitFixtureIndex(fixture, index);

  const uint32_t barTicks = Config::TICKS_PER_BAR;
  const uint32_t windowLength = 2u * barTicks;
  NoteUtils::DisplayNoteVec expected;
  oracleWindowNotes(fixture.passes, fixture.loopLengthTicks, 0, windowLength, expected);
  ResolutionCostCounters counters;
  NoteUtils::DisplayNoteVec actual;
  resolveWindowNotes(index, fixture.passes, fixture.loopLengthTicks, 0, windowLength, actual,
                     &counters);
  printCounters("stage6a_one_bar", counters);
  TEST_ASSERT_EQUAL_UINT32(0u, counters.passChunkListsWalked);
  TEST_ASSERT_GREATER_THAN(0u, expected.size());
  assertDisplayNotesMatch(expected, actual);

  const uint32_t wrapStart = (kCanonicalBars - 2u) * barTicks;
  NoteUtils::DisplayNoteVec wrapExpected;
  oracleWindowNotes(fixture.passes, fixture.loopLengthTicks, wrapStart, windowLength, wrapExpected);
  ResolutionCostCounters wrapCounters;
  NoteUtils::DisplayNoteVec wrapActual;
  resolveWindowNotes(index, fixture.passes, fixture.loopLengthTicks, wrapStart, windowLength,
                     wrapActual, &wrapCounters);
  printCounters("stage6a_wrap_bar", wrapCounters);
  TEST_ASSERT_EQUAL_UINT32(0u, wrapCounters.passChunkListsWalked);
  TEST_ASSERT_GREATER_THAN(0u, wrapExpected.size());
  assertDisplayNotesMatch(wrapExpected, wrapActual);
}

void test_stage6a_prepared_window_after_complete() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();
  LoopContentResolution::deviceGateReset();
  LoopContentResolution::DeviceGateSample sample;
  LoopContentResolution::measureDeviceGate(fixture.passes, fixture.loopLengthTicks, sample);
  constexpr uint32_t kRevision = 7;
  TEST_ASSERT_FALSE(LoopContentResolution::preparedWindowReady(kRevision));
  LoopContentResolution::deviceGateComplete(kRevision);
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(kRevision));
  TEST_ASSERT_FALSE(LoopContentResolution::preparedWindowReady(kRevision + 1u));

  const uint32_t windowLength = 2u * Config::TICKS_PER_BAR;
  SessionMidiEventVec prepared;
  ResolutionCostCounters counters;
  TEST_ASSERT_TRUE(LoopContentResolution::tryResolvePreparedWindow(
      fixture.passes.editPasses, fixture.loopLengthTicks, 0, windowLength, kRevision, prepared,
      &counters));
  SessionMidiEventVec rejected;
  TEST_ASSERT_FALSE(LoopContentResolution::tryResolvePreparedWindow(
      fixture.passes.editPasses, fixture.loopLengthTicks, 0, windowLength, kRevision + 1u, rejected,
      nullptr));
  TEST_ASSERT_TRUE(rejected.empty());

  NoteUtils::DisplayNoteVec expected;
  oracleWindowNotes(fixture.passes, fixture.loopLengthTicks, 0, windowLength, expected);
  const NoteUtils::DisplayNoteVec actual =
      NoteUtils::reconstructDisplayNotes(prepared, fixture.loopLengthTicks, false);
  printCounters("stage6a_prepared", counters);
  TEST_ASSERT_EQUAL_UINT32(0u, counters.passChunkListsWalked);
  TEST_ASSERT_GREATER_THAN(0u, expected.size());
  assertDisplayNotesMatch(expected, actual);
  LoopContentResolution::deviceGateReset();
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
  LoopContentResolution::TickIndex::sortTickEventEntriesByTick(index.tickEvents);
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
  sliced.spanBoundaries.clear();
  sliced.soundingAt.clear();
  sliced.channelByNoteId.clear();
  fillChannelByNoteIdIndex(sliced, resolved);
  for (uint32_t i = 0; i < static_cast<uint32_t>(notes.size()); ++i) {
    TEST_ASSERT_TRUE(sliced.appendSpansFromNotes(resolved, notes, i, i + 1, nullptr));
  }
  sliced.sortSpanBoundaries();
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
  LoopContentResolution::TickIndex::sortTickEventEntriesByTick(index.tickEvents);
  std::map<uint8_t, std::vector<uint32_t>> openOnByPitch;
  for (uint32_t i = 0; i < eventCount; i += step) {
    const uint32_t end = std::min(i + step, eventCount);
    index.pairCapturePassEventRange(id, i, end, openOnByPitch, nullptr);
  }
  index.sortAndUniqueByNoteId();
}

void assertNoteLocationsMatch(const LoopContentResolution::TickIndex& expected,
                              const LoopContentResolution::TickIndex& actual) {
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(expected.byNoteId.size()),
                           static_cast<uint32_t>(actual.byNoteId.size()));
  for (const auto& entry : expected.byNoteId) {
    const auto* found = actual.findByNoteId(entry.noteId);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_UINT32(entry.loc.passId, found->loc.passId);
    TEST_ASSERT_EQUAL_UINT32(entry.loc.onIndex, found->loc.onIndex);
    TEST_ASSERT_EQUAL_INT32(entry.loc.offIndex, found->loc.offIndex);
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
  oneSpan.spanBoundaries.clear();
  oneSpan.soundingAt.clear();
  oneSpan.channelByNoteId.clear();
  fillChannelByNoteIdIndex(oneSpan, resolved);
  for (uint32_t i = 0; i < static_cast<uint32_t>(notes.size()); ++i) {
    TEST_ASSERT_TRUE(oneSpan.appendSpansFromNotes(resolved, notes, i, i + 1, nullptr));
  }
  oneSpan.sortSpanBoundaries();

  LoopContentResolution::StateCheckpoints batched;
  TEST_ASSERT_TRUE(batched.prepareRebuildResolvedEvents(
      index, fixture.passes.editPasses, fixture.loopLengthTicks, interval, resolved, nullptr));
  batched.spans.clear();
  batched.spanBoundaries.clear();
  batched.soundingAt.clear();
  batched.channelByNoteId.clear();
  fillChannelByNoteIdIndex(batched, resolved);
  const uint32_t step = LoopContentResolution::kDeviceGateEventsPerSlice;
  const uint32_t noteCount = static_cast<uint32_t>(notes.size());
  for (uint32_t i = 0; i < noteCount; i += step) {
    const uint32_t end = std::min(i + step, noteCount);
    TEST_ASSERT_TRUE(batched.appendSpansFromNotes(resolved, notes, i, end, nullptr));
  }
  batched.sortSpanBoundaries();
  TEST_ASSERT_EQUAL(oneSpan.spans.size(), batched.spans.size());
  TEST_ASSERT_EQUAL(oneSpan.spanBoundaries.size(), batched.spanBoundaries.size());
}

void test_stage57_span_boundaries_reserve_final_size() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();
  LoopContentResolution::TickIndex index;
  commitFixtureIndex(fixture, index);

  const uint32_t interval =
      Config::TICKS_PER_BAR * LoopContentResolution::kNativeCheckpointBarStride;
  LoopContentResolution::StateCheckpoints checkpoints;
  SessionMidiEventVec resolved;
  TEST_ASSERT_TRUE(checkpoints.prepareRebuildResolvedEvents(
      index, fixture.passes.editPasses, fixture.loopLengthTicks, interval, resolved, nullptr));
  const NoteUtils::DisplayNoteVec notes =
      NoteUtils::reconstructDisplayNotes(resolved, fixture.loopLengthTicks, false);
  TEST_ASSERT_TRUE(notes.size() > LoopContentResolution::kDeviceGateEventsPerSlice);
  checkpoints.spans.clear();
  checkpoints.spanBoundaries.clear();
  checkpoints.soundingAt.clear();
  checkpoints.channelByNoteId.clear();
  fillChannelByNoteIdIndex(checkpoints, resolved);
  TEST_ASSERT_TRUE(checkpoints.appendSpansFromNotes(
      resolved, notes, 0, LoopContentResolution::kDeviceGateEventsPerSlice, nullptr));
  TEST_ASSERT_EQUAL_UINT32(LoopContentResolution::kDeviceGateEventsPerSlice,
                           static_cast<uint32_t>(checkpoints.spans.size()));
  TEST_ASSERT_TRUE(checkpoints.spanBoundaries.capacity() >= notes.size() * 2u);
}

void test_stage57_tick_events_reserve_pass_remainder() {
  LoopContentResolution::TickIndex::CapturePassEntry pass;
  pass.id = 1;
  pass.state = CapturePassState::Active;
  pass.events.resize(32);
  for (uint32_t i = 0; i < static_cast<uint32_t>(pass.events.size()); ++i) {
    pass.events[i].tick = i * 12u;
  }
  LoopContentResolution::TickIndex::TickEventEntryVec out;
  LoopContentResolution::TickIndex::appendTickEventEntries(
      pass, 0, LoopContentResolution::kDeviceGateEventsPerSlice, out);
  TEST_ASSERT_EQUAL_UINT32(LoopContentResolution::kDeviceGateEventsPerSlice,
                           static_cast<uint32_t>(out.size()));
  TEST_ASSERT_TRUE(out.capacity() >= pass.events.size());
}

MidiEvent makeResolvedNoteOn(uint32_t tick, uint8_t channel, uint8_t pitch, NoteId id) {
  MidiEvent event = MidiEvent::NoteOn(tick, channel, pitch, 100);
  event.noteId = id;
  return event;
}

MidiEvent makeResolvedNoteOff(uint32_t tick, uint8_t channel, uint8_t pitch) {
  return MidiEvent::NoteOff(tick, channel, pitch, 0);
}

SessionMidiEventVec makeOpenNoteAcrossSliceEvents() {
  SessionMidiEventVec events;
  events.push_back(makeResolvedNoteOn(0, 1, 60, 1));
  events.push_back(makeResolvedNoteOff(12, 1, 60));
  events.push_back(makeResolvedNoteOn(12, 2, 61, 2));
  events.push_back(makeResolvedNoteOff(24, 2, 61));
  events.push_back(makeResolvedNoteOn(24, 3, 62, 3));
  events.push_back(makeResolvedNoteOff(36, 3, 62));
  events.push_back(makeResolvedNoteOn(48, 5, 70, 10));
  events.push_back(makeResolvedNoteOn(60, 6, 71, 11));
  events.push_back(makeResolvedNoteOff(72, 5, 70));
  events.push_back(makeResolvedNoteOff(84, 6, 71));
  events.push_back(makeResolvedNoteOn(96, 9, 80, 12));
  return events;
}

void test_stage57_recon_keeps_open_note_across_event_slice() {
  const uint32_t loopLength = 4u * Config::TICKS_PER_BAR;
  const SessionMidiEventVec events = makeOpenNoteAcrossSliceEvents();
  TEST_ASSERT_EQUAL_UINT32(11u, static_cast<uint32_t>(events.size()));
  TEST_ASSERT_EQUAL_UINT32(8u, LoopContentResolution::kDeviceGateEventsPerSlice);

  const NoteUtils::DisplayNoteVec full =
      NoteUtils::reconstructDisplayNotes(events, loopLength, false);

  NoteUtils::CanonicalSpanBuild build;
  NoteUtils::appendCanonicalSpansFromMidi(events, loopLength, 0,
                                          LoopContentResolution::kDeviceGateEventsPerSlice, build);
  const uint32_t spansAfterFirstSlice = build.spanCount();
  NoteUtils::appendCanonicalSpansFromMidi(events, loopLength,
                                          LoopContentResolution::kDeviceGateEventsPerSlice,
                                          static_cast<uint32_t>(events.size()), build);
  NoteUtils::finishCanonicalSpansFromMidi(loopLength, build);
  const NoteUtils::DisplayNoteVec sliced =
      NoteUtils::displayNotesFromCanonicalSpans(build, loopLength);

  TEST_ASSERT_TRUE(spansAfterFirstSlice < static_cast<uint32_t>(full.size()));
  TEST_ASSERT_EQUAL(full.size(), sliced.size());
  bool sawCrossed = false;
  bool sawOpen = false;
  for (size_t i = 0; i < full.size(); ++i) {
    TEST_ASSERT_EQUAL_UINT32(full[i].noteId, sliced[i].noteId);
    TEST_ASSERT_EQUAL_UINT8(full[i].note, sliced[i].note);
    TEST_ASSERT_EQUAL_UINT32(full[i].startTick, sliced[i].startTick);
    TEST_ASSERT_EQUAL_UINT32(full[i].endTick, sliced[i].endTick);
    if (full[i].noteId == 10) {
      sawCrossed = true;
      TEST_ASSERT_EQUAL_UINT32(48u, full[i].startTick);
    }
    if (full[i].noteId == 12) {
      sawOpen = true;
      TEST_ASSERT_EQUAL_UINT32(96u, full[i].startTick);
      TEST_ASSERT_TRUE(full[i].endTick > full[i].startTick);
    }
  }
  TEST_ASSERT_TRUE(sawCrossed);
  TEST_ASSERT_TRUE(sawOpen);
}

void test_stage57_pair_keeps_open_note_across_event_slice() {
  LoopContentResolution::TickIndex index;
  const PassId id = 21;
  index.beginCapturePass(id, CapturePassState::Active, 0);
  TEST_ASSERT_EQUAL(1u, index.capturePasses.size());
  index.capturePasses.back().events = makeOpenNoteAcrossSliceEvents();
  std::map<uint8_t, std::vector<uint32_t>> openOnByPitch;
  index.pairCapturePassEventRange(id, 0, LoopContentResolution::kDeviceGateEventsPerSlice,
                                  openOnByPitch, nullptr);
  const auto* crossedAfterFirst = index.findByNoteId(10);
  TEST_ASSERT_NOT_NULL(crossedAfterFirst);
  TEST_ASSERT_EQUAL_UINT32(6u, crossedAfterFirst->loc.onIndex);
  TEST_ASSERT_EQUAL_INT32(-1, crossedAfterFirst->loc.offIndex);
  const auto* neighborAfterFirst = index.findByNoteId(11);
  TEST_ASSERT_NOT_NULL(neighborAfterFirst);
  TEST_ASSERT_EQUAL_INT32(-1, neighborAfterFirst->loc.offIndex);
  TEST_ASSERT_NULL(index.findByNoteId(12));

  index.pairCapturePassEventRange(id, LoopContentResolution::kDeviceGateEventsPerSlice,
                                  static_cast<uint32_t>(index.capturePasses.back().events.size()),
                                  openOnByPitch, nullptr);
  const auto* crossed = index.findByNoteId(10);
  TEST_ASSERT_NOT_NULL(crossed);
  TEST_ASSERT_EQUAL_INT32(8, crossed->loc.offIndex);
  const auto* neighbor = index.findByNoteId(11);
  TEST_ASSERT_NOT_NULL(neighbor);
  TEST_ASSERT_EQUAL_INT32(9, neighbor->loc.offIndex);
  const auto* open = index.findByNoteId(12);
  TEST_ASSERT_NOT_NULL(open);
  TEST_ASSERT_EQUAL_UINT32(10u, open->loc.onIndex);
  TEST_ASSERT_EQUAL_INT32(-1, open->loc.offIndex);
}

void test_stage518a_pair_by_note_id_last_wins() {
  LoopContentResolution::TickIndex index;
  const PassId id = 31;
  index.beginCapturePass(id, CapturePassState::Active, 0);
  SessionMidiEventVec events;
  events.push_back(makeResolvedNoteOn(0, 1, 60, 7));
  events.push_back(makeResolvedNoteOn(12, 1, 60, 7));
  events.push_back(makeResolvedNoteOff(24, 1, 60));
  index.capturePasses.back().events = events;
  std::map<uint8_t, std::vector<uint32_t>> openOnByPitch;
  ResolutionCostCounters counters;
  index.pairCapturePassEventRange(id, 0, static_cast<uint32_t>(events.size()), openOnByPitch,
                                  &counters);
  TEST_ASSERT_EQUAL_UINT32(2u, counters.pairByNoteIdInserts);
  TEST_ASSERT_EQUAL_UINT32(0u, counters.pairByNoteIdOverwrites);
  TEST_ASSERT_EQUAL_UINT32(2u, counters.pairByNoteIdEntries);
  const auto* beforeUnique = index.findByNoteId(7);
  TEST_ASSERT_NOT_NULL(beforeUnique);
  TEST_ASSERT_EQUAL_UINT32(1u, beforeUnique->loc.onIndex);
  TEST_ASSERT_EQUAL_INT32(2, beforeUnique->loc.offIndex);

  index.sortAndUniqueByNoteId(&counters);
  TEST_ASSERT_TRUE(index.byNoteIdSorted);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(index.byNoteId.size()));
  TEST_ASSERT_EQUAL_UINT32(1u, counters.pairByNoteIdOverwrites);
  TEST_ASSERT_EQUAL_UINT32(1u, counters.pairByNoteIdEntries);
  const auto* found = index.findByNoteId(7);
  TEST_ASSERT_NOT_NULL(found);
  TEST_ASSERT_EQUAL_UINT32(1u, found->loc.onIndex);
  TEST_ASSERT_EQUAL_INT32(2, found->loc.offIndex);
}

void test_stage518b_pair_by_note_id_unique_keep_last() {
  LoopContentResolution::TickIndex::ByNoteIdEntryVec entries;
  LoopContentResolution::TickIndex::appendByNoteIdEntry(entries, 7, 1, 0);
  LoopContentResolution::TickIndex::appendByNoteIdEntry(entries, 7, 1, 1);
  entries[0].loc.offIndex = -1;
  entries[1].loc.offIndex = 2;
  LoopContentResolution::TickIndex::appendByNoteIdEntry(entries, 8, 1, 3);
  LoopContentResolution::TickIndex::sortAndUniqueByNoteIdEntries(entries);
  TEST_ASSERT_EQUAL_UINT32(2u, static_cast<uint32_t>(entries.size()));
  const auto* lastSeven = LoopContentResolution::TickIndex::findByNoteIdEntry(entries, 7, true);
  TEST_ASSERT_NOT_NULL(lastSeven);
  TEST_ASSERT_EQUAL_UINT32(1u, lastSeven->loc.onIndex);
  TEST_ASSERT_EQUAL_INT32(2, lastSeven->loc.offIndex);
  const auto* eight = LoopContentResolution::TickIndex::findByNoteIdEntry(entries, 8, true);
  TEST_ASSERT_NOT_NULL(eight);
  TEST_ASSERT_EQUAL_UINT32(3u, eight->loc.onIndex);
}

void test_stage518a_pair_open_on_peak_depth() {
  LoopContentResolution::TickIndex index;
  const PassId id = 32;
  index.beginCapturePass(id, CapturePassState::Active, 0);
  SessionMidiEventVec events;
  events.push_back(makeResolvedNoteOn(0, 1, 60, 1));
  events.push_back(makeResolvedNoteOn(12, 1, 60, 2));
  events.push_back(makeResolvedNoteOff(24, 1, 60));
  events.push_back(makeResolvedNoteOff(36, 1, 60));
  index.capturePasses.back().events = events;
  std::map<uint8_t, std::vector<uint32_t>> openOnByPitch;
  ResolutionCostCounters counters;
  index.pairCapturePassEventRange(id, 0, static_cast<uint32_t>(events.size()), openOnByPitch,
                                  &counters);
  TEST_ASSERT_EQUAL_UINT32(2u, counters.pairOpenOnPeakDepth);
  TEST_ASSERT_EQUAL_UINT32(2u, counters.pairOpenOnPushes);
  TEST_ASSERT_EQUAL_UINT32(2u, counters.pairOpenOnPops);
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(openOnByPitch[60].size()));
}

void test_stage518a_pair_counters_split_owners() {
  LoopContentResolution::TickIndex index;
  const PassId id = 33;
  index.beginCapturePass(id, CapturePassState::Active, 0);
  index.capturePasses.back().events = makeOpenNoteAcrossSliceEvents();
  std::map<uint8_t, std::vector<uint32_t>> openOnByPitch;
  ResolutionCostCounters counters;
  index.pairCapturePassEventRange(id, 0, static_cast<uint32_t>(index.capturePasses.back().events.size()),
                                  openOnByPitch, &counters);
  TEST_ASSERT_EQUAL_UINT32(6u, counters.pairOpenOnPushes);
  TEST_ASSERT_EQUAL_UINT32(5u, counters.pairOpenOnPops);
  TEST_ASSERT_EQUAL_UINT32(6u, counters.pairByNoteIdInserts);
  TEST_ASSERT_EQUAL_UINT32(0u, counters.pairByNoteIdOverwrites);
  TEST_ASSERT_EQUAL_UINT32(5u, counters.pairByNoteIdLookups);
  TEST_ASSERT_EQUAL_UINT32(6u, counters.pairByNoteIdEntries);
  TEST_ASSERT_TRUE(counters.pairByNoteIdMicros + counters.pairOpenOnByPitchMicros +
                       counters.pairLookupMicros + counters.pairOtherMicros <=
                   counters.pairTotalMicros ||
                   counters.pairTotalMicros == 0);
}

void test_stage57_span_channel_uses_full_resolved_not_note_slice() {
  const uint32_t loopLength = 4u * Config::TICKS_PER_BAR;
  const SessionMidiEventVec events = makeOpenNoteAcrossSliceEvents();
  const NoteUtils::DisplayNoteVec notes =
      NoteUtils::reconstructDisplayNotes(events, loopLength, false);
  TEST_ASSERT_TRUE(notes.size() >= 5u);

  LoopContentResolution::StateCheckpoints checkpoints;
  checkpoints.intervalTicks = Config::TICKS_PER_BAR;
  checkpoints.loopLengthTicks = loopLength;
  fillChannelByNoteIdIndex(checkpoints, events);
  const uint32_t step = LoopContentResolution::kDeviceGateEventsPerSlice;
  const uint32_t noteCount = static_cast<uint32_t>(notes.size());
  for (uint32_t i = 0; i < noteCount; i += step) {
    const uint32_t end = std::min(i + step, noteCount);
    TEST_ASSERT_TRUE(checkpoints.appendSpansFromNotes(events, notes, i, end, nullptr));
  }
  TEST_ASSERT_EQUAL(notes.size(), checkpoints.spans.size());
  bool sawCrossed = false;
  bool sawOpen = false;
  for (const auto& span : checkpoints.spans) {
    TEST_ASSERT_EQUAL_UINT8(channelForNoteId(events, span.note.noteId), span.note.channel);
    if (span.note.noteId == 10) {
      sawCrossed = true;
      TEST_ASSERT_EQUAL_UINT8(5u, span.note.channel);
    }
    if (span.note.noteId == 12) {
      sawOpen = true;
      TEST_ASSERT_EQUAL_UINT8(9u, span.note.channel);
    }
  }
  TEST_ASSERT_TRUE(sawCrossed);
  TEST_ASSERT_TRUE(sawOpen);
}

void test_stage57c_channel_index_first_wins_note_id() {
  SessionMidiEventVec events;
  events.push_back(makeResolvedNoteOn(0, 4, 60, 1));
  events.push_back(makeResolvedNoteOn(12, 7, 60, 1));
  events.push_back(makeResolvedNoteOff(24, 4, 60));
  events.push_back(makeResolvedNoteOn(36, 5, 62, 2));
  events.push_back(makeResolvedNoteOff(48, 5, 62));
  LoopContentResolution::StateCheckpoints::ChannelByNoteIdEntryVec entries;
  LoopContentResolution::StateCheckpoints::appendChannelByNoteIdEntries(events, 0, 8, entries);
  LoopContentResolution::StateCheckpoints::sortAndUniqueChannelByNoteIdEntries(entries);
  TEST_ASSERT_EQUAL_UINT32(2u, static_cast<uint32_t>(entries.size()));
  TEST_ASSERT_EQUAL_UINT8(
      4u, LoopContentResolution::StateCheckpoints::findChannelByNoteId(entries, 1));
  TEST_ASSERT_EQUAL_UINT8(
      5u, LoopContentResolution::StateCheckpoints::findChannelByNoteId(entries, 2));
  TEST_ASSERT_EQUAL_UINT8(
      0u, LoopContentResolution::StateCheckpoints::findChannelByNoteId(entries, 99));
}

void test_stage57c_channel_index_sliced_append_then_unique() {
  const SessionMidiEventVec events = makeOpenNoteAcrossSliceEvents();
  TEST_ASSERT_TRUE(events.size() > LoopContentResolution::kDeviceGateEventsPerSlice);
  LoopContentResolution::StateCheckpoints::ChannelByNoteIdEntryVec entries;
  const uint32_t step = LoopContentResolution::kDeviceGateEventsPerSlice;
  const uint32_t limit = static_cast<uint32_t>(events.size());
  LoopContentResolution::StateCheckpoints::appendChannelByNoteIdEntries(events, 0, step, entries);
  bool sawOpenNote = false;
  for (const auto& entry : entries) {
    if (entry.noteId == 12) {
      sawOpenNote = true;
    }
  }
  TEST_ASSERT_FALSE(sawOpenNote);
  for (uint32_t i = step; i < limit; i += step) {
    const uint32_t end = std::min(i + step, limit);
    LoopContentResolution::StateCheckpoints::appendChannelByNoteIdEntries(events, i, end, entries);
  }
  LoopContentResolution::StateCheckpoints::sortAndUniqueChannelByNoteIdEntries(entries);
  TEST_ASSERT_EQUAL_UINT8(
      5u, LoopContentResolution::StateCheckpoints::findChannelByNoteId(entries, 10));
  TEST_ASSERT_EQUAL_UINT8(
      6u, LoopContentResolution::StateCheckpoints::findChannelByNoteId(entries, 11));
  TEST_ASSERT_EQUAL_UINT8(
      9u, LoopContentResolution::StateCheckpoints::findChannelByNoteId(entries, 12));
  TEST_ASSERT_EQUAL_UINT8(
      channelForNoteId(events, 10),
      LoopContentResolution::StateCheckpoints::findChannelByNoteId(entries, 10));
}

void test_stage57c_channel_index_reserves_remaining_events() {
  SessionMidiEventVec events;
  events.resize(32);
  for (uint32_t i = 0; i < 32; ++i) {
    events[i] = makeResolvedNoteOn(i * 12u, 1, 60, static_cast<NoteId>(i + 1));
  }
  LoopContentResolution::StateCheckpoints::ChannelByNoteIdEntryVec out;
  LoopContentResolution::StateCheckpoints::appendChannelByNoteIdEntries(
      events, 0, LoopContentResolution::kDeviceGateEventsPerSlice, out);
  TEST_ASSERT_EQUAL_UINT32(LoopContentResolution::kDeviceGateEventsPerSlice,
                           static_cast<uint32_t>(out.size()));
  TEST_ASSERT_TRUE(out.capacity() >= events.size());
}

void test_stage9_range_prep_matches_full_prepare() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();
  LoopContentResolution::TickIndex index;
  commitFixtureIndex(fixture, index);

  const uint32_t interval =
      Config::TICKS_PER_BAR * LoopContentResolution::kNativeCheckpointBarStride;
  SessionMidiEventVec full;
  LoopContentResolution::StateCheckpoints fullCheckpoints;
  TEST_ASSERT_TRUE(fullCheckpoints.prepareRebuildResolvedEvents(
      index, fixture.passes.editPasses, fixture.loopLengthTicks, interval, full, nullptr));

  SessionMidiEventVec sliced;
  LoopContentResolution::StateCheckpoints slicedCheckpoints;
  TEST_ASSERT_TRUE(slicedCheckpoints.beginRebuildResolvedEvents(fixture.loopLengthTicks, interval,
                                                               sliced));
  std::vector<const LoopContentResolution::TickIndex::CapturePassEntry*> ordered;
  index.collectActiveMaterializePasses(ordered);
  TEST_ASSERT_TRUE(ordered.size() >= 1);
  const uint32_t step = LoopContentResolution::kDeviceGateEventsPerSlice;
  for (size_t passIndex = 0; passIndex < ordered.size(); ++passIndex) {
    const LoopContentResolution::TickIndex::CapturePassEntry* pass = ordered[passIndex];
    TEST_ASSERT_NOT_NULL(pass);
    const uint32_t eventCount = static_cast<uint32_t>(pass->events.size());
    if (passIndex == 0 || sliced.empty()) {
      for (uint32_t i = 0; i < eventCount; i += step) {
        const uint32_t end = std::min(i + step, eventCount);
        index.appendMaterializePassEvents(*pass, i, end, sliced);
      }
      continue;
    }
    SessionMidiEventVec merged;
    merged.reserve(sliced.size() + pass->events.size());
    uint32_t baseCursor = 0;
    uint32_t addCursor = 0;
    for (;;) {
      const uint32_t produced = LoopContentResolution::TickIndex::mergeSortedMidiEventRange(
          sliced, baseCursor, pass->events, addCursor, merged, step);
      if (produced == 0) {
        break;
      }
    }
    sliced = std::move(merged);
  }
  EditPassVec activeRows;
  for (const EditPass& editPass : fixture.passes.editPasses) {
    if (editPass.state == EditPassState::Active && editPass.passType == EditPassType::Note) {
      activeRows.push_back(editPass);
    }
  }
  if (!activeRows.empty()) {
    applyNoteEditPassSequence(sliced, activeRows, fixture.loopLengthTicks);
  }
  assertResolvedEventsMatch(full, sliced);
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
  char line[192];
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
  TEST_ASSERT_GREATER_THAN(0u, sample.indexCommit.resolutionOperations);
  std::printf("stage515c app=%llu sort=%llu\n",
              static_cast<unsigned long long>(sample.rebuild.spanBoundaryAppendMicros),
              static_cast<unsigned long long>(sample.rebuild.spanBoundarySortMicros));
  std::printf("stage517d iapp=%llu isort=%llu win=%llu\n",
              static_cast<unsigned long long>(sample.indexCommit.tickEventAppendMicros),
              static_cast<unsigned long long>(sample.indexCommit.tickEventSortMicros),
              static_cast<unsigned long long>(sample.window.elapsedMicros));
  std::printf("stage57c capp=%llu csort=%llu\n",
              static_cast<unsigned long long>(sample.rebuild.channelByNoteIdAppendMicros),
              static_cast<unsigned long long>(sample.rebuild.channelByNoteIdSortMicros));
  std::printf(
      "stage518b pair tot=%llu bn=%llu nsort=%llu op=%llu lk=%llu oth=%llu ent=%u ins=%u ow=%u "
      "pu=%u po=%u pk=%u oa=%u hb=%llu\n",
      static_cast<unsigned long long>(sample.indexCommit.pairTotalMicros),
      static_cast<unsigned long long>(sample.indexCommit.pairByNoteIdMicros),
      static_cast<unsigned long long>(sample.indexCommit.pairByNoteIdSortMicros),
      static_cast<unsigned long long>(sample.indexCommit.pairOpenOnByPitchMicros),
      static_cast<unsigned long long>(sample.indexCommit.pairLookupMicros),
      static_cast<unsigned long long>(sample.indexCommit.pairOtherMicros),
      sample.indexCommit.pairByNoteIdEntries, sample.indexCommit.pairByNoteIdInserts,
      sample.indexCommit.pairByNoteIdOverwrites,
      sample.indexCommit.pairOpenOnPushes, sample.indexCommit.pairOpenOnPops,
      sample.indexCommit.pairOpenOnPeakDepth, sample.indexCommit.pairOpenOnAllocations,
      static_cast<unsigned long long>(sample.indexCommit.pairOpenOnHeapBytes));
  char pairLine[320];
  LoopContentResolution::deviceGateFormatPairLine(pairLine, sizeof(pairLine));
  TEST_ASSERT_NOT_NULL(std::strstr(pairLine, "DIAG,lcr,pair,tot="));
  TEST_ASSERT_NOT_NULL(std::strstr(pairLine, ",bn="));
  TEST_ASSERT_NOT_NULL(std::strstr(pairLine, ",op="));
  TEST_ASSERT_NOT_NULL(std::strstr(pairLine, ",nsort="));
  TEST_ASSERT_GREATER_THAN(0u, sample.indexCommit.pairTotalMicros);
  TEST_ASSERT_GREATER_THAN(0u, sample.indexCommit.pairByNoteIdInserts);
}

uint64_t elapsedMicrosSince(Clock::time_point start) {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count());
}

void rebuildMapFromSpans(
    const LoopContentResolution::StateCheckpoints::NoteSpanVec& spans,
    std::multimap<uint32_t, size_t>& out) {
  out.clear();
  for (size_t i = 0; i < spans.size(); ++i) {
    out.emplace(spans[i].startTick, i);
    out.emplace(spans[i].endTick, i);
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
  LoopContentResolution::StateCheckpoints::appendSpanBoundaryEntries(
      checkpoints.spans, 0, static_cast<uint32_t>(checkpoints.spans.size()),
      checkpoints.spanBoundaries);
  TEST_ASSERT_EQUAL_UINT32(4u, static_cast<uint32_t>(checkpoints.spanBoundaries.size()));
  TEST_ASSERT_EQUAL_UINT32(100u, checkpoints.spanBoundaries[1].tick);
  TEST_ASSERT_EQUAL(0u, checkpoints.spanBoundaries[1].spanIndex);
  TEST_ASSERT_EQUAL_UINT32(100u, checkpoints.spanBoundaries[2].tick);
  TEST_ASSERT_EQUAL(1u, checkpoints.spanBoundaries[2].spanIndex);
  checkpoints.sortSpanBoundaries();
  TEST_ASSERT_EQUAL_UINT32(1u, countEqualTickPairs(checkpoints.spanBoundaries));
  TEST_ASSERT_EQUAL(0u, checkpoints.spanBoundaries[1].spanIndex);
  TEST_ASSERT_EQUAL(1u, checkpoints.spanBoundaries[2].spanIndex);

  const uint32_t ticks[] = {99u, 100u, 101u};
  for (uint32_t tick : ticks) {
    SoundingNoteVec actual;
    ResolutionCostCounters counters;
    checkpoints.resolveState(tick, actual, &counters);
    TEST_ASSERT_EQUAL_UINT32(0u, counters.passChunkListsWalked);
  }

  SoundingNoteVec atJoin;
  checkpoints.resolveState(100u, atJoin, nullptr);
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
  TEST_ASSERT_EQUAL(checkpoints.spanBoundaries.size(), checkpoints.spans.size() * 2u);

  std::multimap<uint32_t, size_t> mapIndex;
  const Clock::time_point cStart = Clock::now();
  rebuildMapFromSpans(checkpoints.spans, mapIndex);
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

  TEST_ASSERT_EQUAL(checkpoints.spanBoundaries.size(), flat.size());
  TEST_ASSERT_EQUAL(mapIndex.size(), flat.size());
  const uint32_t equalTickPairs = countEqualTickPairs(flat);
  TEST_ASSERT_GREATER_THAN(0u, equalTickPairs);

  const uint32_t ticks[] = {10u, 100u, 201u, fixture.loopLengthTicks / 2u,
                            fixture.loopLengthTicks - 24u};
  uint64_t productionResolveUs = 0;
  uint64_t flatResolveUs = 0;
  for (uint32_t tick : ticks) {
    SoundingNoteVec fromProduction;
    SoundingNoteVec fromFlat;
    SoundingNoteVec fromOracle;
    ResolutionCostCounters productionCounters;
    ResolutionCostCounters flatCounters;
    const Clock::time_point productionStart = Clock::now();
    checkpoints.resolveState(tick, fromProduction, &productionCounters);
    productionResolveUs += elapsedMicrosSince(productionStart);
    const Clock::time_point flatStart = Clock::now();
    checkpoints.resolveStateFromSpanBoundaries(flat, tick, fromFlat, &flatCounters);
    flatResolveUs += elapsedMicrosSince(flatStart);
    LoopContentResolution::resolveState(fixture.passes, fixture.loopLengthTicks, tick, fromOracle);
    TEST_ASSERT_EQUAL_UINT32(0u, productionCounters.passChunkListsWalked);
    TEST_ASSERT_EQUAL_UINT32(0u, flatCounters.passChunkListsWalked);
    TEST_ASSERT_EQUAL_UINT32(productionCounters.eventsReplayed, flatCounters.eventsReplayed);
    assertSoundingMatch(fromOracle, fromProduction);
    assertSoundingMatch(fromProduction, fromFlat);
  }

  std::printf(
      "stage515b C_emplace_us=%llu A_append_us=%llu A_sort_us=%llu A_index_total_us=%llu "
      "A_resolve_us=%llu rebuild_resolve_us=%llu entries=%u equal_tick_pairs=%u spans=%u\n",
      static_cast<unsigned long long>(cEmplaceUs), static_cast<unsigned long long>(appendUs),
      static_cast<unsigned long long>(sortUs), static_cast<unsigned long long>(indexTotalUs),
      static_cast<unsigned long long>(flatResolveUs),
      static_cast<unsigned long long>(productionResolveUs), static_cast<unsigned>(flat.size()),
      equalTickPairs, static_cast<unsigned>(checkpoints.spans.size()));
}

void buildFlatTickEvents(const LoopContentResolution::TickIndex& index,
                         LoopContentResolution::TickIndex::TickEventEntryVec& out) {
  out.clear();
  for (const auto& pass : index.capturePasses) {
    LoopContentResolution::TickIndex::appendTickEventEntries(
        pass, 0, static_cast<uint32_t>(pass.events.size()), out);
  }
}

uint32_t countEqualTickEventPairs(const LoopContentResolution::TickIndex::TickEventEntryVec& entries) {
  uint32_t pairs = 0;
  for (size_t i = 1; i < entries.size(); ++i) {
    if (entries[i].tick == entries[i - 1].tick) {
      pairs += 1;
    }
  }
  return pairs;
}

void test_stage517b_equal_tick_event_order() {
  LoopContentResolution::TickIndex index;
  const PassId id = 11;
  index.beginCapturePass(id, CapturePassState::Active, 0);
  TEST_ASSERT_EQUAL(1u, index.capturePasses.size());
  LoopContentResolution::TickIndex::CapturePassEntry& pass = index.capturePasses.back();
  MidiEvent off = MidiEvent::NoteOff(192, 1, 60, 0);
  off.noteId = 1;
  MidiEvent on = MidiEvent::NoteOn(192, 1, 61, 100);
  on.noteId = 2;
  pass.events.push_back(off);
  pass.events.push_back(on);
  index.indexCapturePassEventRange(id, 0, 2, nullptr);
  LoopContentResolution::TickIndex::sortTickEventEntriesByTick(index.tickEvents);
  TEST_ASSERT_EQUAL_UINT32(2u, static_cast<uint32_t>(index.tickEvents.size()));
  TEST_ASSERT_EQUAL_UINT32(192u, index.tickEvents[0].tick);
  TEST_ASSERT_EQUAL_UINT32(0u, index.tickEvents[0].eventIndex);
  TEST_ASSERT_EQUAL_UINT32(192u, index.tickEvents[1].tick);
  TEST_ASSERT_EQUAL_UINT32(1u, index.tickEvents[1].eventIndex);

  LoopContentResolution::TickIndex::TickEventEntryVec flat;
  LoopContentResolution::TickIndex::appendTickEventEntries(pass, 0, 2, flat);
  TEST_ASSERT_EQUAL_UINT32(2u, static_cast<uint32_t>(flat.size()));
  TEST_ASSERT_EQUAL_UINT32(0u, flat[0].eventIndex);
  TEST_ASSERT_EQUAL_UINT32(1u, flat[1].eventIndex);
  LoopContentResolution::TickIndex::sortTickEventEntriesByTick(flat);
  TEST_ASSERT_EQUAL_UINT32(0u, flat[0].eventIndex);
  TEST_ASSERT_EQUAL_UINT32(1u, flat[1].eventIndex);
}

void test_stage517b_flat_tick_events_match_map() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();
  LoopContentResolution::TickIndex index;
  commitFixtureIndex(fixture, index);

  std::multimap<uint32_t, std::pair<PassId, uint32_t>> mapIndex;
  const Clock::time_point cStart = Clock::now();
  for (const auto& pass : index.capturePasses) {
    for (uint32_t i = 0; i < static_cast<uint32_t>(pass.events.size()); ++i) {
      mapIndex.emplace(pass.events[i].tick, std::make_pair(pass.id, i));
    }
  }
  const uint64_t cEmplaceUs = elapsedMicrosSince(cStart);

  LoopContentResolution::TickIndex::TickEventEntryVec flat;
  const Clock::time_point appendStart = Clock::now();
  buildFlatTickEvents(index, flat);
  const uint64_t appendUs = elapsedMicrosSince(appendStart);
  const Clock::time_point sortStart = Clock::now();
  LoopContentResolution::TickIndex::sortTickEventEntriesByTick(flat);
  const uint64_t sortUs = elapsedMicrosSince(sortStart);
  const uint64_t indexTotalUs = appendUs + sortUs;

  TEST_ASSERT_EQUAL(index.tickEvents.size(), flat.size());
  TEST_ASSERT_EQUAL(mapIndex.size(), flat.size());
  size_t cursor = 0;
  for (const auto& entry : mapIndex) {
    TEST_ASSERT_EQUAL_UINT32(entry.first, index.tickEvents[cursor].tick);
    TEST_ASSERT_EQUAL(entry.second.first, index.tickEvents[cursor].passId);
    TEST_ASSERT_EQUAL_UINT32(entry.second.second, index.tickEvents[cursor].eventIndex);
    TEST_ASSERT_EQUAL_UINT32(entry.first, flat[cursor].tick);
    TEST_ASSERT_EQUAL(entry.second.first, flat[cursor].passId);
    TEST_ASSERT_EQUAL_UINT32(entry.second.second, flat[cursor].eventIndex);
    cursor += 1;
  }
  const uint32_t equalTickPairs = countEqualTickEventPairs(flat);

  const uint32_t windowLength = kCanonicalQueryWindowBars * Config::TICKS_PER_BAR;
  const uint32_t wrapStart = fixture.loopLengthTicks - (windowLength / 2u);
  const uint32_t windows[][2] = {
      {0u, fixture.loopLengthTicks},
      {0u, windowLength},
      {wrapStart, windowLength},
  };
  uint64_t cQueryUs = 0;
  uint64_t aQueryUs = 0;
  for (const auto& window : windows) {
    SessionMidiEventVec fromC;
    SessionMidiEventVec fromA;
    SessionMidiEventVec fromOracle;
    ResolutionCostCounters cCounters;
    ResolutionCostCounters aCounters;
    const Clock::time_point cQueryStart = Clock::now();
    LoopContentResolution::resolveWindow(index, fixture.passes.editPasses, fixture.loopLengthTicks,
                                         window[0], window[1], fromC, &cCounters);
    cQueryUs += elapsedMicrosSince(cQueryStart);
    const Clock::time_point aQueryStart = Clock::now();
    LoopContentResolution::resolveWindow(index, flat, fixture.passes.editPasses,
                                         fixture.loopLengthTicks, window[0], window[1], fromA,
                                         &aCounters);
    aQueryUs += elapsedMicrosSince(aQueryStart);
    oracleWindowEvents(fixture.passes, fixture.loopLengthTicks, window[0], window[1], fromOracle);
    TEST_ASSERT_EQUAL_UINT32(0u, cCounters.passChunkListsWalked);
    TEST_ASSERT_EQUAL_UINT32(0u, aCounters.passChunkListsWalked);
    assertResolvedEventsMatch(fromC, fromA);
    assertResolvedEventsMatch(fromOracle, fromA);
  }

  TEST_ASSERT_FALSE(fixture.passes.overdubPasses.empty());
  index.setCapturePassState(fixture.passes.overdubPasses.back().id, CapturePassState::Disabled);
  fixture.passes.overdubPasses.back().state = CapturePassState::Disabled;
  SessionMidiEventVec disabledC;
  SessionMidiEventVec disabledA;
  SessionMidiEventVec disabledOracle;
  index.findRawWindow(fixture.loopLengthTicks, 0, fixture.loopLengthTicks, disabledC, nullptr);
  index.findRawWindowFromTickEvents(flat, fixture.loopLengthTicks, 0, fixture.loopLengthTicks,
                                    disabledA, nullptr);
  oracleWindowEvents(fixture.passes, fixture.loopLengthTicks, 0, fixture.loopLengthTicks,
                     disabledOracle);
  assertResolvedEventsMatch(disabledC, disabledA);

  std::printf(
      "stage517b C_emplace_us=%llu A_append_us=%llu A_sort_us=%llu A_index_total_us=%llu "
      "C_query_us=%llu A_query_us=%llu entries=%u equal_tick_pairs=%u\n",
      static_cast<unsigned long long>(cEmplaceUs), static_cast<unsigned long long>(appendUs),
      static_cast<unsigned long long>(sortUs), static_cast<unsigned long long>(indexTotalUs),
      static_cast<unsigned long long>(cQueryUs), static_cast<unsigned long long>(aQueryUs),
      static_cast<unsigned>(flat.size()), equalTickPairs);
}

using TickEventEntry = LoopContentResolution::TickIndex::TickEventEntry;
using TickEventEntryVec = LoopContentResolution::TickIndex::TickEventEntryVec;

bool tickEventTickLess(const TickEventEntry& a, const TickEventEntry& b) { return a.tick < b.tick; }

void mergeSortedTickEvents(const TickEventEntryVec& left, const TickEventEntryVec& right,
                           TickEventEntryVec& out) {
  out.clear();
  out.resize(left.size() + right.size());
  std::merge(left.begin(), left.end(), right.begin(), right.end(), out.begin(), tickEventTickLess);
}

void appendOverdubPassAndMergeTickEvents(LoopContentResolution::TickIndex& index,
                                         const OverdubPass& pass) {
  index.beginCapturePass(pass.id, pass.state, pass.mergeSequence);
  for (uint16_t chunkId : pass.committedChunkIds) {
    index.appendCapturePassChunk(pass.id, chunkId);
  }
  const LoopContentResolution::TickIndex::CapturePassEntry& entry = index.capturePasses.back();
  TickEventEntryVec delta;
  LoopContentResolution::TickIndex::appendTickEventEntries(
      entry, 0, static_cast<uint32_t>(entry.events.size()), delta);
  LoopContentResolution::TickIndex::sortTickEventEntriesByTick(delta);
  TickEventEntryVec merged;
  mergeSortedTickEvents(index.tickEvents, delta, merged);
  index.tickEvents.swap(merged);
}

void fillSyntheticPassEvents(LoopContentResolution::TickIndex::CapturePassEntry& pass,
                             uint32_t count, uint32_t tickBase, uint32_t tickStep, NoteId idBase) {
  pass.events.clear();
  pass.events.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    MidiEvent on = MidiEvent::NoteOn(tickBase + i * tickStep, 1, 60, 100);
    on.noteId = idBase + i;
    pass.events.push_back(on);
  }
}

void buildSortedHistoryTickEvents(uint32_t historyEvents, TickEventEntryVec& out) {
  LoopContentResolution::TickIndex index;
  const PassId historyId = 1;
  index.beginCapturePass(historyId, CapturePassState::Active, 0);
  fillSyntheticPassEvents(index.capturePasses.back(), historyEvents, 0, 2, 1);
  LoopContentResolution::TickIndex::appendTickEventEntries(
      index.capturePasses.back(), 0, historyEvents, out);
  LoopContentResolution::TickIndex::sortTickEventEntriesByTick(out);
}

void buildSortedDeltaTickEvents(uint32_t deltaEvents, TickEventEntryVec& out) {
  LoopContentResolution::TickIndex index;
  const PassId deltaId = 2;
  index.beginCapturePass(deltaId, CapturePassState::Active, 1);
  fillSyntheticPassEvents(index.capturePasses.back(), deltaEvents, 1, 2, 100000);
  LoopContentResolution::TickIndex::appendTickEventEntries(index.capturePasses.back(), 0,
                                                           deltaEvents, out);
  LoopContentResolution::TickIndex::sortTickEventEntriesByTick(out);
}

uint64_t minMicrosOverRunsSort(uint32_t runs, const TickEventEntryVec& history,
                               const TickEventEntryVec& delta) {
  uint64_t best = UINT64_MAX;
  for (uint32_t i = 0; i < runs; ++i) {
    TickEventEntryVec working = history;
    working.insert(working.end(), delta.begin(), delta.end());
    const Clock::time_point start = Clock::now();
    LoopContentResolution::TickIndex::sortTickEventEntriesByTick(working);
    const uint64_t sample = elapsedMicrosSince(start);
    if (sample < best) {
      best = sample;
    }
  }
  return best;
}

uint64_t minMicrosOverRunsMerge(uint32_t runs, const TickEventEntryVec& history,
                                const TickEventEntryVec& delta) {
  uint64_t best = UINT64_MAX;
  for (uint32_t i = 0; i < runs; ++i) {
    TickEventEntryVec merged;
    const Clock::time_point start = Clock::now();
    mergeSortedTickEvents(history, delta, merged);
    const uint64_t sample = elapsedMicrosSince(start);
    if (sample < best) {
      best = sample;
    }
  }
  return best;
}

void printStage6d1Sample(const char* treatment, uint32_t historyEvents, uint32_t deltaEvents,
                         uint64_t appendUs, uint64_t orderUs) {
  const uint64_t totalUs = appendUs + orderUs;
  const uint64_t nsPerHistory = (historyEvents == 0) ? 0 : ((orderUs * 1000ull) / historyEvents);
  const uint64_t nsPerDelta = (deltaEvents == 0) ? 0 : ((orderUs * 1000ull) / deltaEvents);
  std::printf(
      "stage6d1 %s history_events=%u commit_delta_events=%u index_append_us=%llu "
      "index_order_us=%llu total_maintenance_us=%llu order_ns_per_history=%llu "
      "order_ns_per_delta=%llu\n",
      treatment, historyEvents, deltaEvents, static_cast<unsigned long long>(appendUs),
      static_cast<unsigned long long>(orderUs), static_cast<unsigned long long>(totalUs),
      static_cast<unsigned long long>(nsPerHistory), static_cast<unsigned long long>(nsPerDelta));
}

void test_stage6d1_overdub_pass_merge_matches_oracle() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();
  TEST_ASSERT_GREATER_OR_EQUAL(3u, fixture.passes.overdubPasses.size());

  const uint32_t preparedOverdubs =
      static_cast<uint32_t>(fixture.passes.overdubPasses.size()) - 3u;
  LoopContentResolution::TickIndex incremental;
  incremental.commitCapturePass(fixture.passes.recordPass.id,
                                fixture.passes.recordPass.committedChunkIds,
                                fixture.passes.recordPass.state, 0, nullptr);
  for (uint32_t i = 0; i < preparedOverdubs; ++i) {
    const OverdubPass& pass = fixture.passes.overdubPasses[i];
    incremental.commitCapturePass(pass.id, pass.committedChunkIds, pass.state, pass.mergeSequence,
                                  nullptr);
  }

  uint32_t preparedRevision = 1;
  const uint32_t windowLength = kCanonicalQueryWindowBars * Config::TICKS_PER_BAR;
  for (uint32_t step = 0; step < 3u; ++step) {
    const OverdubPass& pass = fixture.passes.overdubPasses[preparedOverdubs + step];
    const uint32_t historyEvents = static_cast<uint32_t>(incremental.tickEvents.size());
    const Clock::time_point appendStart = Clock::now();
    appendOverdubPassAndMergeTickEvents(incremental, pass);
    const uint64_t maintenanceUs = elapsedMicrosSince(appendStart);
    const uint32_t deltaEvents =
        static_cast<uint32_t>(incremental.capturePasses.back().events.size());
    preparedRevision += 1;
    printStage6d1Sample("merge_canonical", historyEvents, deltaEvents, 0, maintenanceUs);
    TEST_ASSERT_EQUAL_UINT32(1u + step + 1u, preparedRevision);

    LoopContentResolution::TickIndex cold;
    cold.commitCapturePass(fixture.passes.recordPass.id, fixture.passes.recordPass.committedChunkIds,
                           fixture.passes.recordPass.state, 0, nullptr);
    for (uint32_t i = 0; i <= preparedOverdubs + step; ++i) {
      const OverdubPass& committed = fixture.passes.overdubPasses[i];
      cold.commitCapturePass(committed.id, committed.committedChunkIds, committed.state,
                             committed.mergeSequence, nullptr);
    }

    SessionMidiEventVec fromIncremental;
    SessionMidiEventVec fromCold;
    SessionMidiEventVec fromOracle;
    LoopContentResolution::resolveWindow(incremental, fixture.passes.editPasses,
                                         fixture.loopLengthTicks, 0, windowLength, fromIncremental);
    LoopContentResolution::resolveWindow(cold, fixture.passes.editPasses, fixture.loopLengthTicks, 0,
                                         windowLength, fromCold);
    oracleWindowEvents(fixture.passes, fixture.loopLengthTicks, 0, windowLength, fromOracle);
    assertResolvedEventsMatch(fromCold, fromIncremental);
    if (step == 2u) {
      assertResolvedEventsMatch(fromOracle, fromIncremental);
    }
  }
}

void test_stage6d1_tick_events_order_scales_with_history() {
  constexpr uint32_t kDeltaEvents = 128;
  constexpr uint32_t kSmallHistory = 8192;
  constexpr uint32_t kLargeHistory = 32768;
  constexpr uint32_t kRuns = 5;

  TickEventEntryVec smallHistory;
  TickEventEntryVec largeHistory;
  TickEventEntryVec delta;
  buildSortedHistoryTickEvents(kSmallHistory, smallHistory);
  buildSortedHistoryTickEvents(kLargeHistory, largeHistory);
  buildSortedDeltaTickEvents(kDeltaEvents, delta);
  TEST_ASSERT_EQUAL_UINT32(kSmallHistory, static_cast<uint32_t>(smallHistory.size()));
  TEST_ASSERT_EQUAL_UINT32(kLargeHistory, static_cast<uint32_t>(largeHistory.size()));
  TEST_ASSERT_EQUAL_UINT32(kDeltaEvents, static_cast<uint32_t>(delta.size()));

  const uint64_t sortSmallUs = minMicrosOverRunsSort(kRuns, smallHistory, delta);
  const uint64_t sortLargeUs = minMicrosOverRunsSort(kRuns, largeHistory, delta);
  const uint64_t mergeSmallUs = minMicrosOverRunsMerge(kRuns, smallHistory, delta);
  const uint64_t mergeLargeUs = minMicrosOverRunsMerge(kRuns, largeHistory, delta);

  printStage6d1Sample("sort", kSmallHistory, kDeltaEvents, 0, sortSmallUs);
  printStage6d1Sample("sort", kLargeHistory, kDeltaEvents, 0, sortLargeUs);
  printStage6d1Sample("merge", kSmallHistory, kDeltaEvents, 0, mergeSmallUs);
  printStage6d1Sample("merge", kLargeHistory, kDeltaEvents, 0, mergeLargeUs);

  const bool sortGrowsWithHistory = sortLargeUs > sortSmallUs;
  const bool mergeGrowsWithHistory = mergeLargeUs > mergeSmallUs;
  std::printf(
      "stage6d1 scaling sort_grows_with_history=%d merge_grows_with_history=%d "
      "(full sort FAIL if grows even when < 50 ms; merge PASS only if it does not grow)\n",
      sortGrowsWithHistory ? 1 : 0, mergeGrowsWithHistory ? 1 : 0);

  TEST_ASSERT_GREATER_THAN(0u, sortLargeUs);
  TEST_ASSERT_TRUE(sortGrowsWithHistory);
}

void appendOverdubPassAsDelta(LoopContentResolution::TickIndex& index, const OverdubPass& pass,
                              TickEventEntryVec& delta) {
  index.beginCapturePass(pass.id, pass.state, pass.mergeSequence);
  for (uint16_t chunkId : pass.committedChunkIds) {
    index.appendCapturePassChunk(pass.id, chunkId);
  }
  const LoopContentResolution::TickIndex::CapturePassEntry& entry = index.capturePasses.back();
  TickEventEntryVec added;
  LoopContentResolution::TickIndex::appendTickEventEntries(
      entry, 0, static_cast<uint32_t>(entry.events.size()), added);
  delta.insert(delta.end(), added.begin(), added.end());
  LoopContentResolution::TickIndex::sortTickEventEntriesByTick(delta);
}

void buildSplitHistoryAndDelta(uint32_t historyEvents, uint32_t deltaEvents,
                               LoopContentResolution::TickIndex& index, TickEventEntryVec& history,
                               TickEventEntryVec& unsortedDelta) {
  const PassId historyId = 1;
  const PassId deltaId = 2;
  index.beginCapturePass(historyId, CapturePassState::Active, 0);
  fillSyntheticPassEvents(index.capturePasses.back(), historyEvents, 0, 2, 1);
  LoopContentResolution::TickIndex::appendTickEventEntries(
      index.capturePasses.back(), 0, historyEvents, history);
  LoopContentResolution::TickIndex::sortTickEventEntriesByTick(history);
  index.tickEvents = history;

  if (deltaEvents == 0) {
    return;
  }
  const uint32_t deltaTickBase = 2u * historyEvents + 100u;
  index.beginCapturePass(deltaId, CapturePassState::Active, 1);
  fillSyntheticPassEvents(index.capturePasses.back(), deltaEvents, deltaTickBase, 2, 100000);
  LoopContentResolution::TickIndex::appendTickEventEntries(
      index.capturePasses.back(), 0, deltaEvents, unsortedDelta);
}

uint64_t minMicrosOverRunsDeltaSort(uint32_t runs, const TickEventEntryVec& unsortedDelta) {
  uint64_t best = UINT64_MAX;
  for (uint32_t i = 0; i < runs; ++i) {
    TickEventEntryVec working = unsortedDelta;
    std::reverse(working.begin(), working.end());
    const Clock::time_point start = Clock::now();
    LoopContentResolution::TickIndex::sortTickEventEntriesByTick(working);
    const uint64_t sample = elapsedMicrosSince(start);
    if (sample < best) {
      best = sample;
    }
  }
  return best;
}

void printStage6d2Sample(const char* windowName, uint32_t historyEvents, uint32_t deltaEvents,
                         uint64_t commitUs, uint64_t queryUs, uint32_t candidateHistory,
                         uint32_t candidateDelta, uint32_t resolutionOps) {
  std::printf(
      "stage6d2 window=%s history_events=%u commit_delta_events=%u commit_us=%llu query_us=%llu "
      "candidate_history=%u candidate_delta=%u resolution_ops=%u\n",
      windowName, historyEvents, deltaEvents, static_cast<unsigned long long>(commitUs),
      static_cast<unsigned long long>(queryUs), candidateHistory, candidateDelta, resolutionOps);
}

void test_stage6d2_split_query_matches_merged_oracle() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();
  TEST_ASSERT_GREATER_OR_EQUAL(3u, fixture.passes.overdubPasses.size());

  const uint32_t preparedOverdubs =
      static_cast<uint32_t>(fixture.passes.overdubPasses.size()) - 3u;
  LoopContentResolution::TickIndex incremental;
  incremental.commitCapturePass(fixture.passes.recordPass.id,
                                fixture.passes.recordPass.committedChunkIds,
                                fixture.passes.recordPass.state, 0, nullptr);
  for (uint32_t i = 0; i < preparedOverdubs; ++i) {
    const OverdubPass& pass = fixture.passes.overdubPasses[i];
    incremental.commitCapturePass(pass.id, pass.committedChunkIds, pass.state, pass.mergeSequence,
                                  nullptr);
  }

  const uint32_t frozenHistoryEvents = static_cast<uint32_t>(incremental.tickEvents.size());
  TickEventEntryVec delta;
  const uint32_t windowLength = kCanonicalQueryWindowBars * Config::TICKS_PER_BAR;
  for (uint32_t step = 0; step < 3u; ++step) {
    const OverdubPass& pass = fixture.passes.overdubPasses[preparedOverdubs + step];
    appendOverdubPassAsDelta(incremental, pass, delta);
    TEST_ASSERT_EQUAL_UINT32(frozenHistoryEvents,
                             static_cast<uint32_t>(incremental.tickEvents.size()));

    LoopContentResolution::TickIndex cold;
    cold.commitCapturePass(fixture.passes.recordPass.id, fixture.passes.recordPass.committedChunkIds,
                           fixture.passes.recordPass.state, 0, nullptr);
    for (uint32_t i = 0; i <= preparedOverdubs + step; ++i) {
      const OverdubPass& committed = fixture.passes.overdubPasses[i];
      cold.commitCapturePass(committed.id, committed.committedChunkIds, committed.state,
                             committed.mergeSequence, nullptr);
    }

    TickEventEntryVec merged;
    mergeSortedTickEvents(incremental.tickEvents, delta, merged);

    SessionMidiEventVec fromSplit;
    SessionMidiEventVec fromMerged;
    SessionMidiEventVec fromCold;
    SessionMidiEventVec fromOracle;
    LoopContentResolution::resolveWindow(incremental, incremental.tickEvents, delta,
                                         fixture.passes.editPasses, fixture.loopLengthTicks, 0,
                                         windowLength, fromSplit);
    LoopContentResolution::resolveWindow(incremental, merged, fixture.passes.editPasses,
                                         fixture.loopLengthTicks, 0, windowLength, fromMerged);
    LoopContentResolution::resolveWindow(cold, fixture.passes.editPasses, fixture.loopLengthTicks, 0,
                                         windowLength, fromCold);
    oracleWindowEvents(fixture.passes, fixture.loopLengthTicks, 0, windowLength, fromOracle);
    assertResolvedEventsMatch(fromMerged, fromSplit);
    assertResolvedEventsMatch(fromCold, fromSplit);
    if (step == 2u) {
      assertResolvedEventsMatch(fromOracle, fromSplit);
    }
  }
}

void test_stage6d2_split_history_delta_scales() {
  constexpr uint32_t kDeltaEvents = 8;
  constexpr uint32_t kRuns = 5;
  constexpr uint32_t kHistories[] = {8192u, 16384u, 32768u, 65536u};
  constexpr uint32_t kHistoryCount = sizeof(kHistories) / sizeof(kHistories[0]);

  uint64_t commitUs[kHistoryCount] = {};
  uint64_t queryNoDeltaUs[kHistoryCount] = {};
  uint32_t noDeltaHistoryVisited[kHistoryCount] = {};

  for (uint32_t h = 0; h < kHistoryCount; ++h) {
    const uint32_t historyEvents = kHistories[h];
    LoopContentResolution::TickIndex index;
    TickEventEntryVec history;
    TickEventEntryVec unsortedDelta;
    buildSplitHistoryAndDelta(historyEvents, kDeltaEvents, index, history, unsortedDelta);
    TEST_ASSERT_EQUAL_UINT32(historyEvents, static_cast<uint32_t>(history.size()));
    TEST_ASSERT_EQUAL_UINT32(kDeltaEvents, static_cast<uint32_t>(unsortedDelta.size()));

    const uint32_t historyTickBefore = history.front().tick;
    const uint32_t historyTickAfter = history.back().tick;
    commitUs[h] = minMicrosOverRunsDeltaSort(kRuns, unsortedDelta);
    TEST_ASSERT_EQUAL_UINT32(historyEvents, static_cast<uint32_t>(history.size()));
    TEST_ASSERT_EQUAL_UINT32(historyTickBefore, history.front().tick);
    TEST_ASSERT_EQUAL_UINT32(historyTickAfter, history.back().tick);

    TickEventEntryVec delta = unsortedDelta;
    LoopContentResolution::TickIndex::sortTickEventEntriesByTick(delta);

    TickEventEntryVec merged;
    mergeSortedTickEvents(history, delta, merged);

    const uint32_t loopLengthTicks = 2u * historyEvents + 256u;
    const uint32_t noDeltaStart = 0;
    const uint32_t noDeltaLength = 16;
    const uint32_t deltaStart = 2u * historyEvents + 100u;
    const uint32_t deltaLength = 16;
    const uint32_t overlapStart = 2u * (historyEvents - 4u);
    const uint32_t overlapLength = 124;

    struct WindowCase {
      const char* name;
      uint32_t start;
      uint32_t length;
      uint32_t expectHistory;
      uint32_t expectDelta;
    };
    const WindowCase windows[] = {
        {"no_delta", noDeltaStart, noDeltaLength, 8u, 0u},
        {"delta", deltaStart, deltaLength, 0u, 8u},
        {"overlap", overlapStart, overlapLength, 4u, 8u},
    };

    for (const WindowCase& window : windows) {
      uint64_t bestQueryUs = UINT64_MAX;
      uint32_t candidateHistory = 0;
      uint32_t candidateDelta = 0;
      uint32_t resolutionOps = 0;
      SessionMidiEventVec fromSplit;
      for (uint32_t run = 0; run < kRuns; ++run) {
        ResolutionCostCounters counters;
        SessionMidiEventVec working;
        const Clock::time_point start = Clock::now();
        index.findRawWindowFromTickEvents(history, delta, loopLengthTicks, window.start,
                                          window.length, working, &counters);
        const uint64_t sample = elapsedMicrosSince(start);
        if (sample < bestQueryUs) {
          bestQueryUs = sample;
          fromSplit.swap(working);
          candidateHistory = counters.indexHistoryEntriesVisited;
          candidateDelta = counters.indexDeltaEntriesVisited;
          resolutionOps = counters.indexEntriesVisited;
        }
      }

      SessionMidiEventVec fromMerged;
      index.findRawWindowFromTickEvents(merged, loopLengthTicks, window.start, window.length,
                                        fromMerged, nullptr);
      assertResolvedEventsMatch(fromMerged, fromSplit);
      TEST_ASSERT_EQUAL_UINT32(window.expectHistory, candidateHistory);
      TEST_ASSERT_EQUAL_UINT32(window.expectDelta, candidateDelta);
      TEST_ASSERT_EQUAL_UINT32(window.expectHistory + window.expectDelta, resolutionOps);
      printStage6d2Sample(window.name, historyEvents, kDeltaEvents, commitUs[h], bestQueryUs,
                          candidateHistory, candidateDelta, resolutionOps);
      if (std::strcmp(window.name, "no_delta") == 0) {
        queryNoDeltaUs[h] = bestQueryUs;
        noDeltaHistoryVisited[h] = candidateHistory;
      }
    }
  }

  TEST_ASSERT_EQUAL_UINT32(8u, noDeltaHistoryVisited[0]);
  TEST_ASSERT_EQUAL_UINT32(8u, noDeltaHistoryVisited[kHistoryCount - 1]);
  const bool commitTracksHistory =
      commitUs[kHistoryCount - 1] > 50u && commitUs[kHistoryCount - 1] > (commitUs[0] * 4u + 50u);
  const bool queryTracksHistory = queryNoDeltaUs[kHistoryCount - 1] > 100u &&
                                  queryNoDeltaUs[kHistoryCount - 1] > (queryNoDeltaUs[0] * 4u);
  std::printf(
      "stage6d2 scaling commit_tracks_history=%d query_no_delta_tracks_history=%d "
      "(FAIL if either is 1; then stop incremental overdub LCR and keep 3b)\n",
      commitTracksHistory ? 1 : 0, queryTracksHistory ? 1 : 0);
  TEST_ASSERT_FALSE(commitTracksHistory);
  TEST_ASSERT_FALSE(queryTracksHistory);
}

void appendSyntheticOverdubAsDelta(LoopContentResolution::TickIndex& index, PassId passId,
                                   uint32_t mergeSequence, uint32_t count, uint32_t tickBase,
                                   NoteId idBase, TickEventEntryVec& added) {
  added.clear();
  index.beginCapturePass(passId, CapturePassState::Active, mergeSequence);
  fillSyntheticPassEvents(index.capturePasses.back(), count, tickBase, 2, idBase);
  LoopContentResolution::TickIndex::appendTickEventEntries(
      index.capturePasses.back(), 0, count, added);
}

uint64_t minMicrosOverRunsAppendAndSortDelta(uint32_t runs, const TickEventEntryVec& existingDelta,
                                             const TickEventEntryVec& added) {
  uint64_t best = UINT64_MAX;
  for (uint32_t i = 0; i < runs; ++i) {
    TickEventEntryVec working = existingDelta;
    const Clock::time_point start = Clock::now();
    working.insert(working.end(), added.begin(), added.end());
    LoopContentResolution::TickIndex::sortTickEventEntriesByTick(working);
    const uint64_t sample = elapsedMicrosSince(start);
    if (sample < best) {
      best = sample;
    }
  }
  return best;
}

void printStage6d3Sample(uint32_t historyEvents, uint32_t overdubCount, uint32_t accumulatedDelta,
                         uint64_t commitUs, uint64_t queryNoDeltaUs, uint32_t noDeltaHistory,
                         uint32_t noDeltaDelta, uint64_t queryAllDeltaUs, uint32_t allDeltaHistory,
                         uint32_t allDeltaDelta) {
  std::printf(
      "stage6d3 history_events=%u overdub_count=%u accumulated_delta=%u commit_us=%llu "
      "query_no_delta_us=%llu candidate_history=%u candidate_delta=%u query_all_delta_us=%llu "
      "all_delta_candidate_history=%u all_delta_candidate_delta=%u\n",
      historyEvents, overdubCount, accumulatedDelta, static_cast<unsigned long long>(commitUs),
      static_cast<unsigned long long>(queryNoDeltaUs), noDeltaHistory, noDeltaDelta,
      static_cast<unsigned long long>(queryAllDeltaUs), allDeltaHistory, allDeltaDelta);
}

void test_stage6d3_repeated_overdub_matches_oracle() {
  constexpr uint32_t kHistoryEvents = 256;
  constexpr uint32_t kDeltaEvents = 8;
  constexpr uint32_t kOverdubs = 8;
  LoopContentResolution::TickIndex index;
  TickEventEntryVec history;
  TickEventEntryVec unusedFirstDelta;
  buildSplitHistoryAndDelta(kHistoryEvents, 0, index, history, unusedFirstDelta);
  TEST_ASSERT_EQUAL_UINT32(kHistoryEvents, static_cast<uint32_t>(history.size()));
  TEST_ASSERT_EQUAL_UINT32(kHistoryEvents, static_cast<uint32_t>(index.tickEvents.size()));

  TickEventEntryVec delta;
  const uint32_t loopLengthTicks = 2u * kHistoryEvents + 512u;
  for (uint32_t n = 0; n < kOverdubs; ++n) {
    TickEventEntryVec added;
    const uint32_t tickBase = 2u * kHistoryEvents + 100u + n * 16u;
    appendSyntheticOverdubAsDelta(index, static_cast<PassId>(n + 2u), n + 1u, kDeltaEvents, tickBase,
                                  static_cast<NoteId>(200000u + n * 100u), added);
    delta.insert(delta.end(), added.begin(), added.end());
    LoopContentResolution::TickIndex::sortTickEventEntriesByTick(delta);
    TEST_ASSERT_EQUAL_UINT32(kHistoryEvents, static_cast<uint32_t>(index.tickEvents.size()));
    TEST_ASSERT_EQUAL_UINT32(kHistoryEvents, static_cast<uint32_t>(history.size()));
    TEST_ASSERT_EQUAL_UINT32((n + 1u) * kDeltaEvents, static_cast<uint32_t>(delta.size()));

    TickEventEntryVec merged;
    mergeSortedTickEvents(history, delta, merged);
    SessionMidiEventVec fromSplit;
    SessionMidiEventVec fromMerged;
    index.findRawWindowFromTickEvents(history, delta, loopLengthTicks, 0, loopLengthTicks,
                                      fromSplit, nullptr);
    index.findRawWindowFromTickEvents(merged, loopLengthTicks, 0, loopLengthTicks, fromMerged,
                                      nullptr);
    assertResolvedEventsMatch(fromMerged, fromSplit);
  }
}

void test_stage6d3_repeated_overdub_scales() {
  constexpr uint32_t kDeltaEvents = 8;
  constexpr uint32_t kRuns = 5;
  constexpr uint32_t kHistories[] = {8192u, 32768u};
  constexpr uint32_t kHistoryCount = sizeof(kHistories) / sizeof(kHistories[0]);
  constexpr uint32_t kOverdubCounts[] = {1u, 4u, 16u};
  constexpr uint32_t kOverdubCountN = sizeof(kOverdubCounts) / sizeof(kOverdubCounts[0]);

  uint64_t commitAtMaxN[kHistoryCount] = {};
  uint64_t queryNoDeltaAtMaxN[kHistoryCount] = {};
  uint64_t queryNoDeltaAtFirstN[kHistoryCount] = {};

  for (uint32_t h = 0; h < kHistoryCount; ++h) {
    const uint32_t historyEvents = kHistories[h];
    LoopContentResolution::TickIndex index;
    TickEventEntryVec history;
    TickEventEntryVec unusedFirstDelta;
    buildSplitHistoryAndDelta(historyEvents, 0, index, history, unusedFirstDelta);
    TickEventEntryVec delta;
    const uint32_t historyTickBefore = history.front().tick;
    const uint32_t historyTickAfter = history.back().tick;
    const uint32_t loopLengthTicks = 2u * historyEvents + 512u;
    uint32_t nextOverdub = 0;

    for (uint32_t o = 0; o < kOverdubCountN; ++o) {
      const uint32_t targetCount = kOverdubCounts[o];
      uint64_t lastCommitUs = 0;
      while (nextOverdub < targetCount) {
        TickEventEntryVec added;
        const uint32_t tickBase = 2u * historyEvents + 100u + nextOverdub * 16u;
        appendSyntheticOverdubAsDelta(index, static_cast<PassId>(nextOverdub + 2u), nextOverdub + 1u,
                                      kDeltaEvents, tickBase,
                                      static_cast<NoteId>(200000u + nextOverdub * 100u), added);
        lastCommitUs = minMicrosOverRunsAppendAndSortDelta(kRuns, delta, added);
        delta.insert(delta.end(), added.begin(), added.end());
        LoopContentResolution::TickIndex::sortTickEventEntriesByTick(delta);
        TEST_ASSERT_EQUAL_UINT32(historyEvents, static_cast<uint32_t>(index.tickEvents.size()));
        TEST_ASSERT_EQUAL_UINT32(historyTickBefore, history.front().tick);
        TEST_ASSERT_EQUAL_UINT32(historyTickAfter, history.back().tick);
        nextOverdub += 1;
      }

      const uint32_t accumulatedDelta = targetCount * kDeltaEvents;
      TEST_ASSERT_EQUAL_UINT32(accumulatedDelta, static_cast<uint32_t>(delta.size()));

      const uint32_t noDeltaStart = 0;
      const uint32_t noDeltaLength = 16;
      const uint32_t allDeltaStart = 2u * historyEvents + 100u;
      const uint32_t allDeltaLength = targetCount * 16u;

      uint64_t bestNoDeltaUs = UINT64_MAX;
      uint32_t noDeltaHistory = 0;
      uint32_t noDeltaDelta = 0;
      SessionMidiEventVec noDeltaSplit;
      for (uint32_t run = 0; run < kRuns; ++run) {
        ResolutionCostCounters counters;
        SessionMidiEventVec working;
        const Clock::time_point start = Clock::now();
        index.findRawWindowFromTickEvents(history, delta, loopLengthTicks, noDeltaStart,
                                          noDeltaLength, working, &counters);
        const uint64_t sample = elapsedMicrosSince(start);
        if (sample < bestNoDeltaUs) {
          bestNoDeltaUs = sample;
          noDeltaSplit.swap(working);
          noDeltaHistory = counters.indexHistoryEntriesVisited;
          noDeltaDelta = counters.indexDeltaEntriesVisited;
        }
      }

      uint64_t bestAllDeltaUs = UINT64_MAX;
      uint32_t allDeltaHistory = 0;
      uint32_t allDeltaDelta = 0;
      SessionMidiEventVec allDeltaSplit;
      for (uint32_t run = 0; run < kRuns; ++run) {
        ResolutionCostCounters counters;
        SessionMidiEventVec working;
        const Clock::time_point start = Clock::now();
        index.findRawWindowFromTickEvents(history, delta, loopLengthTicks, allDeltaStart,
                                          allDeltaLength, working, &counters);
        const uint64_t sample = elapsedMicrosSince(start);
        if (sample < bestAllDeltaUs) {
          bestAllDeltaUs = sample;
          allDeltaSplit.swap(working);
          allDeltaHistory = counters.indexHistoryEntriesVisited;
          allDeltaDelta = counters.indexDeltaEntriesVisited;
        }
      }

      TickEventEntryVec merged;
      mergeSortedTickEvents(history, delta, merged);
      SessionMidiEventVec noDeltaMerged;
      SessionMidiEventVec allDeltaMerged;
      index.findRawWindowFromTickEvents(merged, loopLengthTicks, noDeltaStart, noDeltaLength,
                                        noDeltaMerged, nullptr);
      index.findRawWindowFromTickEvents(merged, loopLengthTicks, allDeltaStart, allDeltaLength,
                                        allDeltaMerged, nullptr);
      assertResolvedEventsMatch(noDeltaMerged, noDeltaSplit);
      assertResolvedEventsMatch(allDeltaMerged, allDeltaSplit);
      TEST_ASSERT_EQUAL_UINT32(8u, noDeltaHistory);
      TEST_ASSERT_EQUAL_UINT32(0u, noDeltaDelta);
      TEST_ASSERT_EQUAL_UINT32(0u, allDeltaHistory);
      TEST_ASSERT_EQUAL_UINT32(accumulatedDelta, allDeltaDelta);
      printStage6d3Sample(historyEvents, targetCount, accumulatedDelta, lastCommitUs, bestNoDeltaUs,
                          noDeltaHistory, noDeltaDelta, bestAllDeltaUs, allDeltaHistory,
                          allDeltaDelta);

      if (o == 0) {
        queryNoDeltaAtFirstN[h] = bestNoDeltaUs;
      }
      if (o + 1u == kOverdubCountN) {
        commitAtMaxN[h] = lastCommitUs;
        queryNoDeltaAtMaxN[h] = bestNoDeltaUs;
      }
    }
  }

  const bool commitTracksHistory =
      commitAtMaxN[kHistoryCount - 1] > 50u &&
      commitAtMaxN[kHistoryCount - 1] > (commitAtMaxN[0] * 4u + 50u);
  const bool queryTracksHistory = queryNoDeltaAtMaxN[kHistoryCount - 1] > 100u &&
                                  queryNoDeltaAtMaxN[kHistoryCount - 1] > (queryNoDeltaAtMaxN[0] * 4u);
  const bool queryTracksAccumulatedDelta =
      queryNoDeltaAtMaxN[0] > 100u && queryNoDeltaAtMaxN[0] > (queryNoDeltaAtFirstN[0] * 4u);
  std::printf(
      "stage6d3 scaling commit_tracks_history=%d query_no_delta_tracks_history=%d "
      "query_no_delta_tracks_accumulated_delta=%d "
      "(FAIL if any is 1; then stop incremental overdub LCR and keep 3b)\n",
      commitTracksHistory ? 1 : 0, queryTracksHistory ? 1 : 0,
      queryTracksAccumulatedDelta ? 1 : 0);
  TEST_ASSERT_FALSE(commitTracksHistory);
  TEST_ASSERT_FALSE(queryTracksHistory);
  TEST_ASSERT_FALSE(queryTracksAccumulatedDelta);
}

namespace {

constexpr uint32_t kStage6e1LoopBars = 8;
constexpr uint32_t kStage6e1LoopLen = kStage6e1LoopBars * Config::TICKS_PER_BAR;

struct Stage6e1SourceSpan {
  NoteId id = kInvalidNoteId;
  uint8_t pitch = 0;
  uint32_t onTick = 0;
  uint32_t offTick = 0;
};

enum class Stage6e1ExpectedTransform : uint8_t {
  CandidatesOnly = 0,
  None = 1,
  Shorten = 2,
  Hide = 3,
};

struct Stage6e1OverlapCase {
  const char* name = "";
  Stage6e1SourceSpan sources[4]{};
  uint8_t sourceCount = 0;
  uint8_t incomingPitch = 0;
  uint32_t incomingStart = 0;
  uint32_t incomingEnd = 0;
  Stage6e1ExpectedTransform expected = Stage6e1ExpectedTransform::CandidatesOnly;
  uint32_t expectedStart = 0;
  uint32_t expectedEnd = 0;
  uint8_t expectedTransformCount = 0;
  uint32_t loopLength = kStage6e1LoopLen;
  uint32_t sessionStart = 0;
};

bool stage6e1LinearSoundingSpan(uint32_t startTick, uint32_t endTick, uint32_t loopLength,
                                uint32_t& linearStart, uint32_t& linearEnd) {
  if (loopLength == 0) {
    return false;
  }
  linearStart = IntervalProjection::tickPhaseInLoop(startTick, 0, loopLength);
  linearEnd = IntervalProjection::tickPhaseInLoop(endTick, 0, loopLength);
  if (linearEnd == linearStart) {
    return false;
  }
  if (linearEnd < linearStart) {
    linearEnd += loopLength;
  }
  return linearStart < linearEnd;
}

bool stage6e1ExistingOverlapsHold(uint32_t existingStart, uint32_t existingEnd,
                                  uint32_t incomingStart, uint32_t incomingEnd,
                                  uint32_t loopLength) {
  uint32_t existingLinearStart = 0;
  uint32_t existingLinearEnd = 0;
  uint32_t incomingLinearStart = 0;
  uint32_t incomingLinearEnd = 0;
  if (!stage6e1LinearSoundingSpan(existingStart, existingEnd, loopLength, existingLinearStart,
                                  existingLinearEnd)) {
    return false;
  }
  if (!stage6e1LinearSoundingSpan(incomingStart, incomingEnd, loopLength, incomingLinearStart,
                                  incomingLinearEnd)) {
    return false;
  }
  const bool direct =
      existingLinearStart < incomingLinearEnd && existingLinearEnd > incomingLinearStart;
  const bool existingShifted = existingLinearStart + loopLength < incomingLinearEnd &&
                               existingLinearEnd + loopLength > incomingLinearStart;
  const bool incomingShifted = existingLinearStart < incomingLinearEnd + loopLength &&
                               existingLinearEnd > incomingLinearStart + loopLength;
  return direct || existingShifted || incomingShifted;
}

uint32_t stage6e1SessionPhase(uint32_t tick, uint32_t sessionStart, uint32_t loopLength) {
  if (loopLength == 0) {
    return 0;
  }
  return (tick + loopLength - (sessionStart % loopLength)) % loopLength;
}

void stage6e1ConsumeHold(uint32_t startTick, uint32_t endTick, uint32_t loopLength,
                         uint32_t sessionStart, uint32_t& consumeStart, uint32_t& consumeEnd) {
  consumeStart = startTick;
  consumeEnd = endTick;
  const uint32_t startPhase = stage6e1SessionPhase(startTick, sessionStart, loopLength);
  const uint32_t endPhase = stage6e1SessionPhase(endTick, sessionStart, loopLength);
  if (endPhase < startPhase) {
    consumeStart = startTick;
    if (sessionStart != 0 && startTick < sessionStart) {
      consumeEnd = sessionStart;
    } else {
      consumeEnd = loopLength;
    }
  }
}

void stage6e1AddUniqueNoteId(NoteId* ids, uint8_t& count, uint8_t cap, NoteId id) {
  if (id == kInvalidNoteId) {
    return;
  }
  for (uint8_t i = 0; i < count; ++i) {
    if (ids[i] == id) {
      return;
    }
  }
  TEST_ASSERT_TRUE(count < cap);
  ids[count++] = id;
}

NoteUtils::DisplayNote stage6e1NoteFromEvents(const SessionMidiEventVec& events,
                                              uint32_t loopLength) {
  const NoteUtils::DisplayNoteVec notes =
      NoteUtils::reconstructDisplayNotes(events, loopLength, false);
  TEST_ASSERT_EQUAL(1u, notes.size());
  return notes[0];
}

void stage6e1CollectOracleNotes(const LoopPasses& passes, uint32_t loopLength, uint8_t pitch,
                                uint32_t consumeStart, uint32_t consumeEnd,
                                NoteUtils::DisplayNoteVec& out) {
  out.clear();
  SessionMidiEventVec events;
  passes.materializeToEventVector(events, loopLength);
  const NoteUtils::DisplayNoteVec all =
      NoteUtils::reconstructDisplayNotes(events, loopLength, false);
  for (const NoteUtils::DisplayNote& note : all) {
    if (note.note != pitch || note.noteId == kInvalidNoteId) {
      continue;
    }
    if (stage6e1ExistingOverlapsHold(note.startTick, note.endTick, consumeStart, consumeEnd,
                                     loopLength)) {
      out.push_back(note);
    }
  }
}

void stage6e1CollectTreatmentNotes(const LoopContentResolution::TickIndex& index,
                                   const LoopContentResolution::StateCheckpoints& checkpoints,
                                   const EditPassVec& editPasses, uint32_t loopLength,
                                   uint8_t pitch, uint32_t consumeStart, uint32_t consumeEnd,
                                   NoteUtils::DisplayNoteVec& out) {
  out.clear();
  SoundingNoteVec sounding;
  LoopContentResolution::resolveState(checkpoints, consumeStart, sounding);
  SessionMidiEventVec window;
  TEST_ASSERT_TRUE(consumeEnd > consumeStart);
  LoopContentResolution::resolveWindow(index, editPasses, loopLength, consumeStart,
                                       consumeEnd - consumeStart, window);

  NoteId ids[8]{};
  uint8_t idCount = 0;
  for (const SoundingNote& note : sounding) {
    if (note.pitch == pitch) {
      stage6e1AddUniqueNoteId(ids, idCount, 8, note.noteId);
    }
  }
  for (const MidiEvent& event : window) {
    if (event.isNoteOn() && event.data.noteData.note == pitch) {
      stage6e1AddUniqueNoteId(ids, idCount, 8, event.noteId);
    }
  }

  for (uint8_t i = 0; i < idCount; ++i) {
    SessionMidiEventVec pair;
    index.appendNoteEvents(ids[i], pair);
    TEST_ASSERT_FALSE(pair.empty());
    const NoteUtils::DisplayNote note = stage6e1NoteFromEvents(pair, loopLength);
    if (stage6e1ExistingOverlapsHold(note.startTick, note.endTick, consumeStart, consumeEnd,
                                     loopLength)) {
      out.push_back(note);
    }
  }
}

void stage6e1ApplyGeometry(const NoteUtils::DisplayNoteVec& sourceNotes, uint8_t pitch,
                           uint32_t incomingStart, uint32_t incomingEnd, uint32_t loopLength,
                           PendingNoteChangeVec& out) {
  out.clear();
  BaselineMap baseline;
  EditedGeometry edited{};
  EditedNoteSpan causingSpan{};
  causingSpan.noteId = 99;
  causingSpan.span = NoteBaseline{pitch, 100, incomingStart, incomingEnd};
  edited.causingSpans.push_back(causingSpan);

  std::vector<CausingTargetPair, InternalHeapFirstAllocator<CausingTargetPair>> pairs;
  for (const NoteUtils::DisplayNote& note : sourceNotes) {
    if (note.note != pitch || note.noteId == kInvalidNoteId || note.noteId == 99) {
      continue;
    }
    if (!stage6e1ExistingOverlapsHold(note.startTick, note.endTick, incomingStart, incomingEnd,
                                      loopLength)) {
      continue;
    }
    pairs.push_back(CausingTargetPair{99, note.noteId});
    baseline[note.noteId] = NoteBaseline{note.note, note.velocity, note.startTick, note.endTick};
  }
  if (pairs.empty()) {
    return;
  }

  const auto interactions = analyzeEditSessionInteractions(pairs, edited, baseline);
  const EditSessionInteractionsByTarget grouped =
      groupEditSessionInteractionsByTarget(interactions);
  for (const TargetNoteInteractionGroup& group : grouped.groups) {
    const auto baselineIt = baseline.find(group.targetNoteId);
    TEST_ASSERT_TRUE(baselineIt != baseline.end());
    const NoteBaseline& sourceBaseline = baselineIt->second;
    const ConstrainedNoteGeometry geometry = resolveConstrainedGeometry(
        group.targetNoteId, sourceBaseline, group.incoming, loopLength,
        Config::DEFAULT_NOTE_MIN_LENGTH_TICKS, Config::DEFAULT_NOTE_MIN_LENGTH_REMOVE_ENABLED);

    PendingNoteChange transform{};
    transform.noteId = group.targetNoteId;
    transform.pitch = sourceBaseline.pitch;
    transform.velocity = sourceBaseline.velocity;
    transform.startTick = geometry.startTick;
    transform.endTick = geometry.endTick;
    if (!geometry.visible) {
      transform.kind = PendingNoteChangeKind::Hide;
      transform.startTick = sourceBaseline.startTick;
      transform.endTick = sourceBaseline.endTick;
      out.push_back(transform);
      continue;
    }
    if (geometry.startTick != sourceBaseline.startTick ||
        geometry.endTick != sourceBaseline.endTick) {
      transform.kind = PendingNoteChangeKind::Shorten;
      out.push_back(transform);
    }
  }
}

void stage6e1AssertExpectedTransform(const Stage6e1OverlapCase& overlapCase,
                                     const PendingNoteChangeVec& transforms) {
  if (overlapCase.expected == Stage6e1ExpectedTransform::CandidatesOnly) {
    return;
  }
  if (overlapCase.expected == Stage6e1ExpectedTransform::None) {
    TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(transforms.size()));
    return;
  }

  uint8_t matchCount = 0;
  for (const PendingNoteChange& change : transforms) {
    if (overlapCase.expected == Stage6e1ExpectedTransform::Shorten &&
        change.kind == PendingNoteChangeKind::Shorten) {
      TEST_ASSERT_EQUAL_UINT32(overlapCase.expectedStart, change.startTick);
      TEST_ASSERT_EQUAL_UINT32(overlapCase.expectedEnd, change.endTick);
      ++matchCount;
    } else if (overlapCase.expected == Stage6e1ExpectedTransform::Hide &&
               change.kind == PendingNoteChangeKind::Hide) {
      ++matchCount;
    }
  }
  char detail[160];
  std::snprintf(detail, sizeof(detail), "%s transforms=%u match=%u firstKind=%d",
                overlapCase.name, static_cast<unsigned>(transforms.size()),
                static_cast<unsigned>(matchCount),
                transforms.empty() ? -1 : static_cast<int>(transforms[0].kind));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(overlapCase.expectedTransformCount, matchCount, detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(overlapCase.expectedTransformCount,
                                   static_cast<uint32_t>(transforms.size()), detail);
}

void stage6e1RunCase(const Stage6e1OverlapCase& overlapCase) {
  TEST_MESSAGE(overlapCase.name);
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  const uint32_t loopLength =
      overlapCase.loopLength != 0 ? overlapCase.loopLength : kStage6e1LoopLen;
  const uint32_t incomingStart =
      IntervalProjection::tickPhaseInLoop(overlapCase.incomingStart, 0, loopLength);
  const uint32_t incomingEnd =
      IntervalProjection::tickPhaseInLoop(overlapCase.incomingEnd, 0, loopLength);

  LoopEventStore store;
  for (uint8_t i = 0; i < overlapCase.sourceCount; ++i) {
    const Stage6e1SourceSpan& span = overlapCase.sources[i];
    TEST_ASSERT_TRUE(storeAppendNoteOn(store, span.onTick, 1, span.pitch, 100, span.id));
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(span.offTick, 1, span.pitch, 0)));
  }
  LoopPasses passes;
  passes.recordPass.id = 1;
  passes.recordPass.state = CapturePassState::Active;
  TEST_ASSERT_TRUE(
      transferCaptureStoreToCommittedChunkIds(store, passes.recordPass.committedChunkIds));

  LoopContentResolution::TickIndex index;
  index.commitLoopPasses(passes);
  LoopContentResolution::StateCheckpoints checkpoints;
  checkpoints.rebuild(index, passes.editPasses, loopLength, Config::TICKS_PER_BAR);

  uint32_t consumeStart = 0;
  uint32_t consumeEnd = 0;
  stage6e1ConsumeHold(incomingStart, incomingEnd, loopLength, overlapCase.sessionStart,
                      consumeStart, consumeEnd);
  TEST_ASSERT_TRUE(consumeEnd > consumeStart);

  NoteUtils::DisplayNoteVec oracle;
  stage6e1CollectOracleNotes(passes, loopLength, overlapCase.incomingPitch, consumeStart,
                             consumeEnd, oracle);
  NoteUtils::DisplayNoteVec treatment;
  stage6e1CollectTreatmentNotes(index, checkpoints, passes.editPasses, loopLength,
                                overlapCase.incomingPitch, consumeStart, consumeEnd, treatment);
  assertDisplayNotesMatch(oracle, treatment);

  if (overlapCase.expected == Stage6e1ExpectedTransform::CandidatesOnly) {
    return;
  }
  if (overlapCase.expected == Stage6e1ExpectedTransform::Shorten ||
      overlapCase.expected == Stage6e1ExpectedTransform::Hide) {
    TEST_ASSERT_MESSAGE(!treatment.empty(), overlapCase.name);
  }
  PendingNoteChangeVec transforms;
  stage6e1ApplyGeometry(treatment, overlapCase.incomingPitch, consumeStart, consumeEnd, loopLength,
                        transforms);
  stage6e1AssertExpectedTransform(overlapCase, transforms);
}

uint32_t stage6e1RotateTick(uint32_t tick, uint32_t sessionStart, uint32_t loopLength) {
  if (loopLength == 0) {
    return 0;
  }
  return (tick + sessionStart) % loopLength;
}

bool stage6e1RotationPreservesLinearSpans(const Stage6e1OverlapCase& overlapCase,
                                          uint32_t sessionStart) {
  const uint32_t loopLength =
      overlapCase.loopLength != 0 ? overlapCase.loopLength : kStage6e1LoopLen;
  auto staysLinear = [&](uint32_t onTick, uint32_t offTick) {
    if (offTick < onTick) {
      return true;
    }
    const uint32_t rotatedOn = stage6e1RotateTick(onTick, sessionStart, loopLength);
    const uint32_t rotatedOff = stage6e1RotateTick(offTick, sessionStart, loopLength);
    return rotatedOff > rotatedOn;
  };
  for (uint8_t i = 0; i < overlapCase.sourceCount; ++i) {
    if (!staysLinear(overlapCase.sources[i].onTick, overlapCase.sources[i].offTick)) {
      return false;
    }
  }
  if (overlapCase.incomingEnd > overlapCase.incomingStart &&
      !staysLinear(overlapCase.incomingStart, overlapCase.incomingEnd)) {
    return false;
  }
  return true;
}

Stage6e1OverlapCase stage6e1RotateCase(const Stage6e1OverlapCase& overlapCase,
                                       uint32_t sessionStart) {
  Stage6e1OverlapCase rotated = overlapCase;
  const uint32_t loopLength =
      overlapCase.loopLength != 0 ? overlapCase.loopLength : kStage6e1LoopLen;
  rotated.sessionStart = sessionStart;
  for (uint8_t i = 0; i < rotated.sourceCount; ++i) {
    rotated.sources[i].onTick =
        stage6e1RotateTick(overlapCase.sources[i].onTick, sessionStart, loopLength);
    rotated.sources[i].offTick =
        stage6e1RotateTick(overlapCase.sources[i].offTick, sessionStart, loopLength);
  }
  rotated.incomingStart =
      stage6e1RotateTick(overlapCase.incomingStart, sessionStart, loopLength);
  rotated.incomingEnd = stage6e1RotateTick(overlapCase.incomingEnd, sessionStart, loopLength);
  rotated.expectedStart =
      stage6e1RotateTick(overlapCase.expectedStart, sessionStart, loopLength);
  rotated.expectedEnd = stage6e1RotateTick(overlapCase.expectedEnd, sessionStart, loopLength);
  return rotated;
}

uint32_t stage6e1LoadCases(Stage6e1OverlapCase* out, uint32_t cap) {
  const uint32_t loopLen = kStage6e1LoopLen;
  const Stage6e1OverlapCase cases[] = {
      {"user_example_note31_still_sounding",
       {{1, 31, 100, 200}, {2, 32, 50, 150}},
       2,
       31,
       125,
       175},
      {"user_example_note32_off_at_150",
       {{1, 31, 100, 200}, {2, 32, 50, 150}},
       2,
       32,
       125,
       175},
      {"pending_shorten_long_source",
       {{1, 60, 50, 200}},
       1,
       60,
       120,
       160,
       Stage6e1ExpectedTransform::Shorten,
       50,
       119,
       1},
      {"pending_hide_when_covered",
       {{1, 60, 10, 40}, {2, 60, 50, 80}, {3, 60, 90, 120}},
       3,
       60,
       5,
       130,
       Stage6e1ExpectedTransform::Hide,
       0,
       0,
       3},
      {"pending_add_only_other_pitch",
       {{1, 60, 10, 58}},
       1,
       72,
       200,
       240,
       Stage6e1ExpectedTransform::None,
       0,
       0,
       0},
      {"pending_wrap_crossing_tail_shorten",
       {{1, 60, loopLen - 80, loopLen - 10}},
       1,
       60,
       loopLen - 40,
       20,
       Stage6e1ExpectedTransform::Shorten,
       loopLen - 80,
       loopLen - 41,
       1},
      {"pending_wrap_crossing_skips_head",
       {{1, 60, 8, 40}},
       1,
       60,
       loopLen - 40,
       50,
       Stage6e1ExpectedTransform::None,
       0,
       0,
       0},
      {"user_long_source_contained_shorten",
       {{1, 60, 0, 5000}},
       1,
       60,
       4000,
       4200,
       Stage6e1ExpectedTransform::Shorten,
       0,
       3999,
       1},
      {"user_long_source_wrap_incoming_hide_loop_4000",
       {{1, 60, 0, 3999}},
       1,
       60,
       4000,
       200,
       Stage6e1ExpectedTransform::Hide,
       0,
       3999,
       1,
       4000},
      {"user_long_source_wrap_incoming_loop_4100",
       {{1, 60, 0, 4099}},
       1,
       60,
       4000,
       200,
       Stage6e1ExpectedTransform::Shorten,
       0,
       3999,
       1,
       4100},
  };
  const uint32_t count = static_cast<uint32_t>(sizeof(cases) / sizeof(cases[0]));
  TEST_ASSERT_TRUE(count <= cap);
  for (uint32_t i = 0; i < count; ++i) {
    out[i] = cases[i];
  }
  return count;
}

}  // namespace

void test_stage6e1_resolve_state_candidates_match_note_map_oracle() {
  Stage6e1OverlapCase cases[16]{};
  const uint32_t count = stage6e1LoadCases(cases, 16);
  TEST_ASSERT_GREATER_THAN(0u, count);
  for (uint32_t i = 0; i < count; ++i) {
    stage6e1RunCase(cases[i]);
  }
}

void test_stage6e1b_session_start_is_wrap_origin() {
  constexpr uint32_t kSessionStart = 777;
  Stage6e1OverlapCase cases[16]{};
  const uint32_t count = stage6e1LoadCases(cases, 16);
  TEST_ASSERT_GREATER_THAN(0u, count);
  uint32_t rotatedCount = 0;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t loopLength =
        cases[i].loopLength != 0 ? cases[i].loopLength : kStage6e1LoopLen;
    TEST_ASSERT_TRUE(kSessionStart < loopLength);
    TEST_ASSERT_TRUE(kSessionStart != cases[i].incomingStart);
    TEST_ASSERT_TRUE(kSessionStart != cases[i].incomingEnd);
    if (!stage6e1RotationPreservesLinearSpans(cases[i], kSessionStart)) {
      continue;
    }
    char name[96];
    std::snprintf(name, sizeof(name), "%s_session_%u", cases[i].name, kSessionStart);
    Stage6e1OverlapCase rotated = stage6e1RotateCase(cases[i], kSessionStart);
    rotated.name = name;
    stage6e1RunCase(rotated);
    ++rotatedCount;
  }
  TEST_ASSERT_GREATER_THAN(0u, rotatedCount);

  Stage6e1OverlapCase absolute{};
  absolute.name = "absolute_source_wrap_at_session_start";
  absolute.sources[0] = {1, 60, 0, 5000};
  absolute.sourceCount = 1;
  absolute.incomingPitch = 60;
  absolute.incomingStart = stage6e1RotateTick(4000, kSessionStart, kStage6e1LoopLen);
  absolute.incomingEnd = stage6e1RotateTick(200, kSessionStart, kStage6e1LoopLen);
  absolute.expected = Stage6e1ExpectedTransform::Shorten;
  absolute.expectedStart = 0;
  absolute.expectedEnd = absolute.incomingStart - 1;
  absolute.expectedTransformCount = 1;
  absolute.loopLength = kStage6e1LoopLen;
  absolute.sessionStart = kSessionStart;
  stage6e1RunCase(absolute);
}

void test_stage6d4_publish_restamps_without_device_gate_complete() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CanonicalResolutionFixture fixture = buildCanonicalResolutionFixture();
  TEST_ASSERT_GREATER_OR_EQUAL(3u, fixture.passes.overdubPasses.size());

  const uint32_t publishCount = 3;
  LoopPasses preparedPasses = fixture.passes;
  const uint32_t preparedOverdubs =
      static_cast<uint32_t>(preparedPasses.overdubPasses.size()) - publishCount;
  preparedPasses.overdubPasses.resize(preparedOverdubs);

  LoopContentResolution::deviceGateReset();
  LoopContentResolution::DeviceGateSample sample;
  LoopContentResolution::measureDeviceGate(preparedPasses, fixture.loopLengthTicks, sample);
  constexpr uint32_t kPreparedRevision = 1;
  TEST_ASSERT_FALSE(LoopContentResolution::preparedWindowReady(kPreparedRevision));
  LoopContentResolution::deviceGateComplete(kPreparedRevision);
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(kPreparedRevision));

  const uint32_t windowLength = kCanonicalQueryWindowBars * Config::TICKS_PER_BAR;
  SessionMidiEventVec baseline;
  ResolutionCostCounters baselineCounters;
  TEST_ASSERT_TRUE(LoopContentResolution::tryResolvePreparedWindow(
      preparedPasses.editPasses, fixture.loopLengthTicks, 0, windowLength, kPreparedRevision,
      baseline, &baselineCounters));
  const uint32_t historyEvents = baselineCounters.eventsInHistory;
  TEST_ASSERT_GREATER_THAN(0u, historyEvents);

  uint32_t revision = kPreparedRevision;
  LoopPasses livePasses = preparedPasses;
  for (uint32_t i = 0; i < publishCount; ++i) {
    const OverdubPass& pass = fixture.passes.overdubPasses[preparedOverdubs + i];
    revision += 1;
    LoopContentResolution::publishPreparedOverdubPass(pass, revision);
    TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(revision));
    TEST_ASSERT_FALSE(LoopContentResolution::preparedWindowReady(revision - 1u));
    livePasses.overdubPasses.push_back(pass);

    SessionMidiEventVec fromPrepared;
    ResolutionCostCounters counters;
    TEST_ASSERT_TRUE(LoopContentResolution::tryResolvePreparedWindow(
        livePasses.editPasses, fixture.loopLengthTicks, 0, windowLength, revision, fromPrepared,
        &counters));
    TEST_ASSERT_EQUAL_UINT32(historyEvents, counters.eventsInHistory);

    SessionMidiEventVec fromOracle;
    oracleWindowEvents(livePasses, fixture.loopLengthTicks, 0, windowLength, fromOracle);
    assertResolvedEventsMatch(fromOracle, fromPrepared);
  }

  SessionMidiEventVec stale;
  TEST_ASSERT_FALSE(LoopContentResolution::tryResolvePreparedWindow(
      livePasses.editPasses, fixture.loopLengthTicks, 0, windowLength, revision + 1u, stale,
      nullptr));
  TEST_ASSERT_TRUE(stale.empty());

  LoopContentResolution::deviceGateReset();
  LoopContentResolution::publishPreparedOverdubPass(fixture.passes.overdubPasses.back(),
                                                    revision + 2u);
  TEST_ASSERT_FALSE(LoopContentResolution::preparedWindowReady(revision + 2u));
  SessionMidiEventVec unprepared;
  TEST_ASSERT_FALSE(LoopContentResolution::tryResolvePreparedWindow(
      fixture.passes.editPasses, fixture.loopLengthTicks, 0, windowLength, revision + 2u,
      unprepared, nullptr));
  TEST_ASSERT_TRUE(unprepared.empty());
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
  RUN_TEST(test_stage6a_one_bar_notes_match_oracle);
  RUN_TEST(test_stage6a_prepared_window_after_complete);
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
  RUN_TEST(test_stage57_span_boundaries_reserve_final_size);
  RUN_TEST(test_stage57_tick_events_reserve_pass_remainder);
  RUN_TEST(test_stage57_recon_keeps_open_note_across_event_slice);
  RUN_TEST(test_stage57_pair_keeps_open_note_across_event_slice);
  RUN_TEST(test_stage518a_pair_by_note_id_last_wins);
  RUN_TEST(test_stage518b_pair_by_note_id_unique_keep_last);
  RUN_TEST(test_stage518a_pair_open_on_peak_depth);
  RUN_TEST(test_stage518a_pair_counters_split_owners);
  RUN_TEST(test_stage57_span_channel_uses_full_resolved_not_note_slice);
  RUN_TEST(test_stage57c_channel_index_first_wins_note_id);
  RUN_TEST(test_stage57c_channel_index_sliced_append_then_unique);
  RUN_TEST(test_stage57c_channel_index_reserves_remaining_events);
  RUN_TEST(test_stage9_range_prep_matches_full_prepare);
  RUN_TEST(test_stage9_device_gate_slice_budget_matches_idle_maint_bar);
  RUN_TEST(test_stage9_phase_line_on_change_not_every_slice);
  RUN_TEST(test_stage9_native_worst_case_micros);
  RUN_TEST(test_stage515b_equal_tick_boundary_order);
  RUN_TEST(test_stage515b_flat_span_boundaries_match_map);
  RUN_TEST(test_stage517b_equal_tick_event_order);
  RUN_TEST(test_stage517b_flat_tick_events_match_map);
  RUN_TEST(test_stage6d1_overdub_pass_merge_matches_oracle);
  RUN_TEST(test_stage6d1_tick_events_order_scales_with_history);
  RUN_TEST(test_stage6d2_split_query_matches_merged_oracle);
  RUN_TEST(test_stage6d2_split_history_delta_scales);
  RUN_TEST(test_stage6d3_repeated_overdub_matches_oracle);
  RUN_TEST(test_stage6d3_repeated_overdub_scales);
  RUN_TEST(test_stage6d4_publish_restamps_without_device_gate_complete);
  RUN_TEST(test_stage6e1_resolve_state_candidates_match_note_map_oracle);
  RUN_TEST(test_stage6e1b_session_start_is_wrap_origin);
  return UNITY_END();
}
