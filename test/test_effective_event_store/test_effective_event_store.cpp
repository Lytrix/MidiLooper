//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// DEC-036 D1 — incremental effective event store.

#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/EditManager/EditApply.cpp"
#include "../../src/Loop/LoopPasses.cpp"
#include "../test_support/MemoryMonitorNativeDeps.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/Loop.cpp"
#include "../test_support/LoopCaptureTestDeps.cpp"

#include "Loop.h"
#include "../test_support/CommittedChunkIdTestHelpers.h"
#include "../test_support/NoteIdTestFixtures.h"
#include "EditPass.h"
#include "LoopPasses.h"
#include "MidiEvent.h"

namespace {

using namespace NoteIdTestFixtures;

constexpr uint32_t kLoopLen = Config::TICKS_PER_BAR * 8;

void seedRecordNote(Loop& loop, uint32_t onTick, uint32_t offTick, uint8_t pitch,
                    uint8_t channel = 1) {
  loop.loopLengthTicks = kLoopLen;
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, onTick, channel, pitch, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(offTick, channel, pitch, 0)));
  loop.seedRecordPassFromStore(store);
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

EditPass makeLengthRow(NoteId targetNoteId, uint32_t start, uint32_t newEnd) {
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = EditActionType::Update;
  row.propertyType = EditPropertyType::Length;
  row.targetNoteId = targetNoteId;
  row.startTick = start;
  row.endTick = newEnd;
  return row;
}

EditPass makeDeleteRow(NoteId targetNoteId) {
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = EditActionType::Delete;
  row.propertyType = EditPropertyType::None;
  row.targetNoteId = targetNoteId;
  return row;
}

bool eventsEqual(const SessionMidiEventVec& a, const SessionMidiEventVec& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].tick != b[i].tick || a[i].type != b[i].type ||
        a[i].data.noteData.note != b[i].data.noteData.note) {
      return false;
    }
  }
  return true;
}

bool eventsStrictEqual(const SessionMidiEventVec& a, const SessionMidiEventVec& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].tick != b[i].tick || a[i].type != b[i].type || a[i].channel != b[i].channel ||
        a[i].noteId != b[i].noteId ||
        a[i].data.noteData.note != b[i].data.noteData.note ||
        a[i].data.noteData.velocity != b[i].data.noteData.velocity) {
      return false;
    }
  }
  return true;
}

void gatherLegacyWindowReplay(const Loop& loop, uint32_t windowStart, uint32_t windowLength,
                              SessionMidiEventVec& out) {
  loop.copyEffectiveCommittedEventsInRange(out, windowStart, windowLength, false);
  EditPassVec activeRows;
  for (const EditPass& editPass : loop.passes.editPasses) {
    if (editPass.state == EditPassState::Active && editPass.passType == EditPassType::Note) {
      activeRows.push_back(editPass);
    }
  }
  if (!activeRows.empty()) {
    applyNoteEditPassSequence(out, activeRows, loop.loopLengthTicks);
  }
}

}  // namespace

void test_effective_store_matches_materialize_after_seed() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);

  SessionMidiEventVec expected;
  loop.passes.materializeToEventVector(expected, kLoopLen);

  SessionMidiEventVec actual;
  loop.copyEffectiveCommittedEvents(actual);
  TEST_ASSERT_TRUE(eventsEqual(expected, actual));
  TEST_ASSERT_GREATER_THAN(0u, loop.effectiveEventStoreRevision());
}

void test_effective_store_updates_on_edit_pass_save() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);

  SessionMidiEventVec beforeEdit;
  loop.copyEffectiveCommittedEvents(beforeEdit);

  const EditPassId editId = loop.saveNoteEditPass(0, makePitchRow(1, 10, 58, 67));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, editId);

  SessionMidiEventVec expected;
  loop.passes.materializeToEventVector(expected, kLoopLen);
  SessionMidiEventVec actual;
  loop.copyEffectiveCommittedEvents(actual);
  TEST_ASSERT_TRUE(eventsEqual(expected, actual));
  TEST_ASSERT_FALSE(eventsEqual(beforeEdit, actual));
}

void test_overdub_entry_does_not_rebuild_effective_store() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);
  (void)loop.saveNoteEditPass(0, makePitchRow(1, 10, 58, 67));

  Loop::resetCommittedPitchQueryWork();
  const uint32_t revBefore = loop.effectiveEventStoreRevision();

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_EQUAL(revBefore, loop.effectiveEventStoreRevision());
  TEST_ASSERT_EQUAL(0u, Loop::committedEventsFullMaterializeCount());
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  TEST_ASSERT_EQUAL(0u, Loop::committedEventsFullMaterializeCount());
}

void test_ensure_effective_store_does_not_rebuild_when_fresh() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);
  (void)loop.saveNoteEditPass(0, makePitchRow(1, 10, 58, 67));

  SessionMidiEventVec warm;
  loop.copyEffectiveCommittedEvents(warm);
  const uint32_t revAfterWarm = loop.effectiveEventStoreRevision();
  TEST_ASSERT_GREATER_THAN(0u, revAfterWarm);

  loop.copyEffectiveCommittedEventsInRange(warm, 0, kLoopLen);
  TEST_ASSERT_EQUAL(revAfterWarm, loop.effectiveEventStoreRevision());
}

void test_overdub_entry_uses_windowed_source_on_long_loop() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t longLoopLen = Config::TICKS_PER_BAR * 68;
  loop.loopLengthTicks = longLoopLen;

  LoopEventStore store;
  for (uint32_t bar = 0; bar < 68; ++bar) {
    const uint32_t onTick = bar * Config::TICKS_PER_BAR + 10;
    const uint32_t offTick = onTick + 40;
    TEST_ASSERT_TRUE(storeAppendNoteOn(store, onTick, 1, static_cast<uint8_t>(60 + (bar % 12)), 100,
                                       static_cast<NoteId>(bar + 1)));
    TEST_ASSERT_TRUE(store.append(
        MidiEvent::NoteOff(offTick, 1, static_cast<uint8_t>(60 + (bar % 12)), 0)));
  }
  loop.seedRecordPassFromStore(store);

  SessionMidiEventVec fullFlat;
  loop.copyEffectiveCommittedEvents(fullFlat);
  TEST_ASSERT_GREATER_THAN(100u, fullFlat.size());

  const uint32_t playhead = 32u * Config::TICKS_PER_BAR;
  loop.beginCapture(CapturePhase::Overdub, playhead);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  TEST_ASSERT_FALSE(loop.overdubSourceViewEvents().empty());
  TEST_ASSERT_FALSE(loop.overdubSourceViewNotes().empty());
  TEST_ASSERT_LESS_THAN(68u, loop.overdubSourceViewNotes().size());
  const uint32_t playheadOn = playhead + 10;
  const uint8_t playheadPitch = static_cast<uint8_t>(60 + (32 % 12));
  bool foundPlayhead = false;
  bool foundBar0 = false;
  for (const NoteUtils::DisplayNote& note : loop.overdubSourceViewNotes()) {
    if (note.startTick == playheadOn && note.note == playheadPitch) {
      foundPlayhead = true;
    }
    if (note.startTick == 10u && note.note == 60) {
      foundBar0 = true;
    }
  }
  TEST_ASSERT_TRUE(foundPlayhead);
  TEST_ASSERT_FALSE(foundBar0);
}

uint32_t visualCacheDirtyBarCount(const VisualBarVec& dirtyBars) {
  uint32_t count = 0;
  for (uint8_t flag : dirtyBars) {
    if (flag != 0) {
      ++count;
    }
  }
  return count;
}

void test_mark_affected_display_cache_ranges_dirties_sparse_bars() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = kLoopLen;

  LoopEventStore recordStore;
  for (uint32_t bar = 0; bar < 8; ++bar) {
    const uint32_t onTick = bar * Config::TICKS_PER_BAR + 10;
    TEST_ASSERT_TRUE(NoteIdTestFixtures::storeAppendNoteOn(
        recordStore, onTick, 1, static_cast<uint8_t>(60 + bar), 100,
        static_cast<NoteId>(bar + 1)));
    TEST_ASSERT_TRUE(recordStore.append(
        MidiEvent::NoteOff(onTick + 40, 1, static_cast<uint8_t>(60 + bar), 0)));
  }
  loop.seedRecordPassFromStore(recordStore);
  TEST_ASSERT_EQUAL(8u, loop.visualCache.notes.size());
  // slice_clean leaves dirtyBars empty with the notes still in cache.
  loop.visualCache.dirtyBars.clear();
  loop.visualCacheDirty = false;
  const size_t notesBefore = loop.visualCache.notes.size();

  OverdubPass overdub{};
  overdub.id = 50;
  overdub.state = CapturePassState::Active;
  LoopEventStore overdubStore;
  const uint32_t overdubOn = 3 * Config::TICKS_PER_BAR + 10;
  TEST_ASSERT_TRUE(NoteIdTestFixtures::storeAppendNoteOn(overdubStore, overdubOn, 1, 72, 90, 99));
  MidiEvent overdubOff = MidiEvent::NoteOff(overdubOn + 40, 1, 72, 0);
  overdubOff.noteId = 99;
  TEST_ASSERT_TRUE(overdubStore.append(overdubOff));
  TEST_ASSERT_TRUE(
      transferCaptureStoreToCommittedChunkIds(overdubStore, overdub.committedChunkIds));
  loop.passes.overdubPasses.push_back(std::move(overdub));

  loop.markAffectedDisplayCacheRanges(50, EditPassIdList{});
  TEST_ASSERT_TRUE(loop.visualCacheDirty);
  TEST_ASSERT_EQUAL(notesBefore, loop.visualCache.notes.size());
  TEST_ASSERT_EQUAL(8u, loop.visualCache.dirtyBars.size());
  const uint32_t overdubDirty = visualCacheDirtyBarCount(loop.visualCache.dirtyBars);
  TEST_ASSERT_GREATER_THAN(0u, overdubDirty);
  TEST_ASSERT_LESS_THAN(8u, overdubDirty);
  TEST_ASSERT_EQUAL(1, loop.visualCache.dirtyBars[3]);
  TEST_ASSERT_EQUAL(0, loop.visualCache.dirtyBars[2]);
  TEST_ASSERT_EQUAL(0, loop.visualCache.dirtyBars[4]);
  TEST_ASSERT_EQUAL(0, loop.visualCache.dirtyBars[0]);
  TEST_ASSERT_EQUAL(0, loop.visualCache.dirtyBars[7]);
  TEST_ASSERT_EQUAL(1u, overdubDirty);

  EditPass hide{};
  hide.passType = EditPassType::Note;
  hide.actionType = EditActionType::Delete;
  hide.propertyType = EditPropertyType::None;
  hide.targetNoteId = 1;
  hide.startTick = 10;
  hide.endTick = 50;
  const EditPassId hideId = loop.saveNoteEditPass(0, hide);
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, hideId);
  loop.visualCache.dirtyBars.clear();
  loop.visualCacheDirty = false;

  EditPassIdList companions;
  companions.push_back(hideId);
  loop.markAffectedDisplayCacheRanges(kInvalidPassId, companions);
  TEST_ASSERT_TRUE(loop.visualCacheDirty);
  TEST_ASSERT_EQUAL(8u, loop.visualCache.dirtyBars.size());
  TEST_ASSERT_EQUAL(1, loop.visualCache.dirtyBars[0]);
  TEST_ASSERT_EQUAL(0, loop.visualCache.dirtyBars[7]);
  TEST_ASSERT_LESS_THAN(8u, visualCacheDirtyBarCount(loop.visualCache.dirtyBars));
}

void test_undo_pass_toggle_rebuilds_effective_store() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);
  const PassId recordId = loop.passes.recordPass.id;

  TEST_ASSERT_TRUE(loop.setCapturePassState(recordId, CapturePassState::Disabled));

  SessionMidiEventVec disabled;
  loop.copyEffectiveCommittedEvents(disabled);
  TEST_ASSERT_EQUAL(0u, disabled.size());

  TEST_ASSERT_TRUE(loop.setCapturePassState(recordId, CapturePassState::Active));
  SessionMidiEventVec expected;
  loop.passes.materializeToEventVector(expected, kLoopLen);
  SessionMidiEventVec restored;
  loop.copyEffectiveCommittedEvents(restored);
  TEST_ASSERT_TRUE(eventsEqual(expected, restored));
}

void test_companion_projection_window_matches_legacy_replay() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = kLoopLen;

  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 10, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(58, 1, 60, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 120, 1, 62, 90, 2));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(180, 1, 62, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 250, 1, 60, 80, 3));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(320, 1, 60, 0)));
  loop.seedRecordPassFromStore(store);

  const EditPassId companionDeleteId =
      loop.saveNoteEditPass(kOverdubCompanionEditPassIndex, makeDeleteRow(2));
  const EditPassId companionLengthId = loop.saveNoteEditPass(
      kOverdubCompanionEditPassIndex, makeLengthRow(3, 250, 286));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, companionDeleteId);
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, companionLengthId);

  SessionMidiEventVec expected;
  gatherLegacyWindowReplay(loop, 0, kLoopLen, expected);

  SessionMidiEventVec stopped;
  loop.gatherCommittedEventsInWindow(stopped, 0, kLoopLen, true);
  TEST_ASSERT_TRUE(eventsStrictEqual(expected, stopped));

  loop.beginCapture(CapturePhase::Overdub);
  SessionMidiEventVec actual;
  loop.gatherCommittedEventsInWindow(actual, 0, kLoopLen, true);
  TEST_ASSERT_TRUE(eventsStrictEqual(expected, actual));

  SessionMidiEventVec replayed;
  loop.gatherCommittedEventsInWindow(replayed, 0, kLoopLen, true);
  TEST_ASSERT_TRUE(eventsStrictEqual(expected, replayed));

  const uint32_t windowAStart = 0;
  const uint32_t windowALen = 2u * Config::TICKS_PER_BAR;
  SessionMidiEventVec expectedWindowA;
  SessionMidiEventVec actualWindowA;
  gatherLegacyWindowReplay(loop, windowAStart, windowALen, expectedWindowA);
  loop.gatherCommittedEventsInWindow(actualWindowA, windowAStart, windowALen, true);
  TEST_ASSERT_TRUE(eventsStrictEqual(expectedWindowA, actualWindowA));

  const uint32_t windowBStart = 7u * Config::TICKS_PER_BAR;
  const uint32_t windowBLen = 2u * Config::TICKS_PER_BAR;
  SessionMidiEventVec expectedWindowB;
  SessionMidiEventVec actualWindowB;
  gatherLegacyWindowReplay(loop, windowBStart, windowBLen, expectedWindowB);
  loop.gatherCommittedEventsInWindow(actualWindowB, windowBStart, windowBLen, true);
  TEST_ASSERT_TRUE(eventsStrictEqual(expectedWindowB, actualWindowB));
}

void test_companion_projection_empty_cache_uses_legacy_window_gather() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = kLoopLen;

  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 10, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(58, 1, 60, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 120, 1, 62, 90, 2));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(180, 1, 62, 0)));
  loop.seedRecordPassFromStore(store);

  const EditPassId companionDeleteId =
      loop.saveNoteEditPass(kOverdubCompanionEditPassIndex, makeDeleteRow(2));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, companionDeleteId);

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_FALSE(loop.companionProjectionCacheReadyForTest());

  SessionMidiEventVec expected;
  gatherLegacyWindowReplay(loop, 0, kLoopLen, expected);
  SessionMidiEventVec actual;
  loop.gatherCommittedEventsInWindow(actual, 0, kLoopLen, true);
  TEST_ASSERT_TRUE(eventsStrictEqual(expected, actual));
  TEST_ASSERT_FALSE(loop.companionProjectionCacheReadyForTest());
}

void test_companion_projection_clock_path_window_does_not_rebuild() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = kLoopLen;

  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 10, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(58, 1, 60, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 120, 1, 62, 90, 2));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(180, 1, 62, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 250, 1, 60, 80, 3));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(320, 1, 60, 0)));
  loop.seedRecordPassFromStore(store);

  const EditPassId companionDeleteId =
      loop.saveNoteEditPass(kOverdubCompanionEditPassIndex, makeDeleteRow(2));
  const EditPassId companionLengthId = loop.saveNoteEditPass(
      kOverdubCompanionEditPassIndex, makeLengthRow(3, 250, 286));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, companionDeleteId);
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, companionLengthId);

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_FALSE(loop.companionProjectionCacheReadyForTest());

  const uint32_t windowStart = 0;
  const uint32_t windowLength = 2u * Config::TICKS_PER_BAR;
  SessionMidiEventVec expected;
  gatherLegacyWindowReplay(loop, windowStart, windowLength, expected);
  SessionMidiEventVec actual;
  loop.gatherCommittedEventsInWindow(actual, windowStart, windowLength, true);
  TEST_ASSERT_TRUE(eventsStrictEqual(expected, actual));
  TEST_ASSERT_FALSE(loop.companionProjectionCacheReadyForTest());
}

void test_companion_projection_falls_back_with_non_companion_note_rows() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = kLoopLen;

  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 20, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(80, 1, 60, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 140, 1, 64, 100, 2));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(220, 1, 64, 0)));
  loop.seedRecordPassFromStore(store);

  const EditPassId companionDeleteId =
      loop.saveNoteEditPass(kOverdubCompanionEditPassIndex, makeDeleteRow(2));
  const EditPassId noteEditPitchId = loop.saveNoteEditPass(0, makePitchRow(1, 20, 80, 67));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, companionDeleteId);
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, noteEditPitchId);

  SessionMidiEventVec expected;
  gatherLegacyWindowReplay(loop, 0, kLoopLen, expected);

  SessionMidiEventVec actual;
  loop.gatherCommittedEventsInWindow(actual, 0, kLoopLen, true);
  TEST_ASSERT_TRUE(eventsStrictEqual(expected, actual));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_effective_store_matches_materialize_after_seed);
  RUN_TEST(test_effective_store_updates_on_edit_pass_save);
  RUN_TEST(test_overdub_entry_does_not_rebuild_effective_store);
  RUN_TEST(test_ensure_effective_store_does_not_rebuild_when_fresh);
  RUN_TEST(test_overdub_entry_uses_windowed_source_on_long_loop);
  RUN_TEST(test_mark_affected_display_cache_ranges_dirties_sparse_bars);
  RUN_TEST(test_undo_pass_toggle_rebuilds_effective_store);
  RUN_TEST(test_companion_projection_window_matches_legacy_replay);
  RUN_TEST(test_companion_projection_empty_cache_uses_legacy_window_gather);
  RUN_TEST(test_companion_projection_clock_path_window_does_not_rebuild);
  RUN_TEST(test_companion_projection_falls_back_with_non_companion_note_rows);
  return UNITY_END();
}
