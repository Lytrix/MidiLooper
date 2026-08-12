//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Phase 1 — overdubSourceView establish/clear, materialize-aware geometry, wrap-safe lookup.

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
#include "MidiEvent.h"
#include "Globals.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/NoteUtils.h"

namespace {

using namespace NoteIdTestFixtures;

constexpr uint32_t kLoopLen = Config::TICKS_PER_BAR * 8;

int countNoteOns(const SessionMidiEventVec& flat, uint8_t pitch) {
  int count = 0;
  for (const MidiEvent& evt : flat) {
    if (evt.isNoteOn() && evt.data.noteData.note == pitch) {
      ++count;
    }
  }
  return count;
}

bool hasNoteOnAt(const SessionMidiEventVec& flat, uint8_t pitch, uint32_t tick) {
  for (const MidiEvent& evt : flat) {
    if (evt.isNoteOn() && evt.data.noteData.note == pitch && evt.tick == tick) {
      return true;
    }
  }
  return false;
}

bool hasDisplayNote(const NoteUtils::DisplayNoteVec& notes, uint8_t pitch, uint32_t startTick) {
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.note == pitch && note.startTick == startTick) {
      return true;
    }
  }
  return false;
}

void seedRecordNote(Loop& loop, uint32_t onTick, uint32_t offTick, uint8_t pitch,
                    uint8_t channel = 1) {
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, onTick, channel, pitch, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(offTick, channel, pitch, 0)));
  loop.seedRecordPassFromStore(store);
  loop.loopLengthTicks = kLoopLen;
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

EditPass makeLengthRow(NoteId targetNoteId, uint32_t start, uint32_t end) {
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = EditActionType::Update;
  row.propertyType = EditPropertyType::Length;
  row.targetNoteId = targetNoteId;
  row.startTick = start;
  row.endTick = end;
  row.pitch = 0;
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

void filterNoteEventsByPitch(const SessionMidiEventVec& full, uint8_t pitch,
                             SessionMidiEventVec& out) {
  out.clear();
  for (const MidiEvent& evt : full) {
    if ((evt.isNoteOn() || evt.isNoteOff()) && evt.data.noteData.note == pitch) {
      out.push_back(evt);
    }
  }
}

void assertPitchQueryMatchesOracle(Loop& loop, uint8_t pitch) {
  Loop::resetCommittedPitchQueryWork();
  SessionMidiEventVec optimized;
  loop.gatherCommittedNoteEventsForPitch(pitch, optimized);
  TEST_ASSERT_EQUAL_UINT32(0, Loop::committedEventsFullMaterializeCount());

  SessionMidiEventVec full;
  loop.gatherCommittedEvents(full);
  SessionMidiEventVec oracle;
  filterNoteEventsByPitch(full, pitch, oracle);

  TEST_ASSERT_EQUAL(oracle.size(), optimized.size());
  for (size_t i = 0; i < oracle.size(); ++i) {
    TEST_ASSERT_EQUAL(static_cast<int>(oracle[i].type), static_cast<int>(optimized[i].type));
    TEST_ASSERT_EQUAL_UINT32(oracle[i].tick, optimized[i].tick);
    TEST_ASSERT_EQUAL_UINT8(oracle[i].data.noteData.note, optimized[i].data.noteData.note);
    if (oracle[i].isNoteOn()) {
      TEST_ASSERT_EQUAL(oracle[i].noteId, optimized[i].noteId);
    }
  }
}

}  // namespace

void test_overdub_start_establishes_source_view() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);

  TEST_ASSERT_FALSE(loop.hasOverdubSourceView());
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  TEST_ASSERT_EQUAL(kLoopLen, loop.overdubSourceViewLoopLengthTicks());
  TEST_ASSERT_TRUE(loop.overdubSourceViewEvents().empty());
  TEST_ASSERT_TRUE(loop.overdubSourceViewNotes().empty());

  SessionMidiEventVec pitchEvents;
  loop.gatherCommittedNoteEventsForPitch(60, pitchEvents);
  TEST_ASSERT_EQUAL(1, countNoteOns(pitchEvents, 60));
  const NoteUtils::DisplayNoteVec pitchNotes =
      NoteUtils::reconstructDisplayNotes(pitchEvents, kLoopLen, false);
  TEST_ASSERT_EQUAL(1u, pitchNotes.size());
  TEST_ASSERT_TRUE(hasDisplayNote(pitchNotes, 60, 10));
}

void test_record_start_does_not_keep_source_view() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());

  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_FALSE(loop.hasOverdubSourceView());
  TEST_ASSERT_TRUE(loop.overdubSourceViewNotes().empty());
}

void test_source_view_includes_edit_pass_geometry() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);
  (void)loop.saveNoteEditPass(0, makePitchRow(1, 10, 58, 67));

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());

  SessionMidiEventVec pitch60;
  loop.gatherCommittedNoteEventsForPitch(60, pitch60);
  SessionMidiEventVec pitch67;
  loop.gatherCommittedNoteEventsForPitch(67, pitch67);
  TEST_ASSERT_EQUAL(0, countNoteOns(pitch60, 60));
  TEST_ASSERT_EQUAL(1, countNoteOns(pitch67, 67));

  SessionMidiEventVec committed;
  loop.gatherCommittedEvents(committed);
  TEST_ASSERT_EQUAL(countNoteOns(committed, 67), countNoteOns(pitch67, 67));
}

void test_source_view_stable_across_capture_appends_and_wraps() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);
  loop.beginCapture(CapturePhase::Overdub);

  SessionMidiEventVec beforeCapture;
  loop.gatherCommittedNoteEventsForPitch(60, beforeCapture);
  TEST_ASSERT_TRUE(beforeCapture.size() >= 2u);

  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(kLoopLen - 20, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(kLoopLen - 5, 1, 72, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(5, 1, 74, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(40, 1, 74, 0)));

  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  SessionMidiEventVec afterCapture;
  loop.gatherCommittedNoteEventsForPitch(60, afterCapture);
  TEST_ASSERT_EQUAL(beforeCapture.size(), afterCapture.size());
  TEST_ASSERT_EQUAL(1, countNoteOns(afterCapture, 60));
  TEST_ASSERT_EQUAL(0, countNoteOns(afterCapture, 72));
  TEST_ASSERT_EQUAL(0, countNoteOns(afterCapture, 74));
  SessionMidiEventVec pitch72;
  loop.gatherCommittedNoteEventsForPitch(72, pitch72);
  TEST_ASSERT_EQUAL(0, countNoteOns(pitch72, 72));
}

void test_source_view_immutable_when_live_materialize_mutates() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);
  loop.beginCapture(CapturePhase::Overdub);

  SessionMidiEventVec snapshot;
  loop.gatherCommittedNoteEventsForPitch(60, snapshot);
  TEST_ASSERT_FALSE(snapshot.empty());

  loop.midiEvents().push_back(MidiEvent::NoteOn(100, 1, 80, 80));
  loop.midiEvents().push_back(MidiEvent::NoteOff(140, 1, 80, 0));
  loop.invalidateCaches();

  SessionMidiEventVec afterMutate;
  loop.gatherCommittedNoteEventsForPitch(60, afterMutate);
  TEST_ASSERT_EQUAL(snapshot.size(), afterMutate.size());
  TEST_ASSERT_EQUAL(0, countNoteOns(afterMutate, 80));
  TEST_ASSERT_TRUE(hasNoteOnAt(afterMutate, 60, 10));
}

void test_candidate_lookup_wrap_safe_high_then_low_capture_order() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;

  // Source notes near both ends of the loop.
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, kLoopLen - 40, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(kLoopLen - 10, 1, 60, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 8, 1, 62, 100, 2));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(48, 1, 62, 0)));
  loop.seedRecordPassFromStore(store);
  loop.loopLengthTicks = kLoopLen;

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(kLoopLen - 30, 1, 70, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(kLoopLen - 15, 1, 70, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(12, 1, 71, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(30, 1, 71, 0)));

  SessionMidiEventVec pitch60;
  loop.gatherCommittedNoteEventsForPitch(60, pitch60);
  SessionMidiEventVec pitch62;
  loop.gatherCommittedNoteEventsForPitch(62, pitch62);
  TEST_ASSERT_TRUE(hasNoteOnAt(pitch62, 62, 8));
  TEST_ASSERT_TRUE(hasNoteOnAt(pitch60, 60, kLoopLen - 40));
  TEST_ASSERT_FALSE(hasNoteOnAt(pitch62, 60, kLoopLen - 40));
  TEST_ASSERT_FALSE(hasNoteOnAt(pitch60, 62, 8));

  SessionMidiEventVec lowWindow;
  DisplayWindowUtils::filterMidiEventsToWindow(pitch62, lowWindow, 0, 64, kLoopLen);
  TEST_ASSERT_TRUE(hasNoteOnAt(lowWindow, 62, 8));

  SessionMidiEventVec highWindow;
  DisplayWindowUtils::filterMidiEventsToWindow(pitch60, highWindow, kLoopLen - 64, 64, kLoopLen);
  TEST_ASSERT_TRUE(hasNoteOnAt(highWindow, 60, kLoopLen - 40));

  const NoteUtils::DisplayNoteVec note62 =
      NoteUtils::reconstructDisplayNotes(pitch62, kLoopLen, false);
  TEST_ASSERT_TRUE(hasDisplayNote(note62, 62, 8));
  TEST_ASSERT_FALSE(hasDisplayNote(note62, 60, kLoopLen - 40));
}

void test_discard_and_commit_clear_source_view() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  TEST_ASSERT_TRUE(loop.overdubSourceViewNotes().empty());
  loop.discardCapture();
  TEST_ASSERT_FALSE(loop.hasOverdubSourceView());
  TEST_ASSERT_TRUE(loop.overdubSourceViewNotes().empty());

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(200, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(248, 1, 64, 0)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(0));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass());
  TEST_ASSERT_FALSE(loop.hasOverdubSourceView());
  TEST_ASSERT_TRUE(loop.overdubSourceViewNotes().empty());
}

void test_pitch_query_matches_oracle_shorten_hide_and_unrelated() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 10, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(80, 1, 60, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 20, 1, 62, 100, 2));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(90, 1, 62, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 30, 1, 64, 100, 3));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(100, 1, 64, 0)));
  loop.seedRecordPassFromStore(store);
  loop.loopLengthTicks = kLoopLen;

  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, loop.saveNoteEditPass(0, makeLengthRow(1, 10, 40)));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, loop.saveNoteEditPass(0, makeDeleteRow(3)));

  assertPitchQueryMatchesOracle(loop, 60);
  assertPitchQueryMatchesOracle(loop, 62);
  assertPitchQueryMatchesOracle(loop, 64);
}

void test_committed_pitch_mutation_c4_to_d4_both_queries() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, loop.saveNoteEditPass(0, makePitchRow(1, 10, 58, 62)));

  assertPitchQueryMatchesOracle(loop, 62);
  assertPitchQueryMatchesOracle(loop, 60);
  SessionMidiEventVec d4;
  loop.gatherCommittedNoteEventsForPitch(62, d4);
  TEST_ASSERT_EQUAL(1, countNoteOns(d4, 62));
  SessionMidiEventVec c4;
  loop.gatherCommittedNoteEventsForPitch(60, c4);
  TEST_ASSERT_EQUAL(0, countNoteOns(c4, 60));
}

void test_committed_pitch_chain_c4_d4_e4() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, loop.saveNoteEditPass(0, makePitchRow(1, 10, 58, 62)));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, loop.saveNoteEditPass(0, makePitchRow(1, 10, 58, 64)));

  assertPitchQueryMatchesOracle(loop, 60);
  assertPitchQueryMatchesOracle(loop, 62);
  assertPitchQueryMatchesOracle(loop, 64);
  SessionMidiEventVec e4;
  loop.gatherCommittedNoteEventsForPitch(64, e4);
  TEST_ASSERT_EQUAL(1, countNoteOns(e4, 64));
  SessionMidiEventVec d4;
  loop.gatherCommittedNoteEventsForPitch(62, d4);
  TEST_ASSERT_EQUAL(0, countNoteOns(d4, 62));
}

void test_edit_ordering_pitch_then_length_and_length_then_pitch() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 80, 60);
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, loop.saveNoteEditPass(0, makePitchRow(1, 10, 80, 62)));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, loop.saveNoteEditPass(0, makeLengthRow(1, 10, 40)));
  assertPitchQueryMatchesOracle(loop, 62);
  assertPitchQueryMatchesOracle(loop, 60);

  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop2;
  seedRecordNote(loop2, 10, 80, 60);
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, loop2.saveNoteEditPass(0, makeLengthRow(1, 10, 40)));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, loop2.saveNoteEditPass(0, makePitchRow(1, 10, 40, 62)));
  assertPitchQueryMatchesOracle(loop2, 62);
  assertPitchQueryMatchesOracle(loop2, 60);
}

void test_edit_ordering_pitch_then_delete_and_delete_then_pitch() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, loop.saveNoteEditPass(0, makePitchRow(1, 10, 58, 62)));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, loop.saveNoteEditPass(0, makeDeleteRow(1)));
  assertPitchQueryMatchesOracle(loop, 62);
  assertPitchQueryMatchesOracle(loop, 60);

  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop2;
  seedRecordNote(loop2, 10, 58, 60);
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, loop2.saveNoteEditPass(0, makeDeleteRow(1)));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, loop2.saveNoteEditPass(0, makePitchRow(1, 10, 58, 62)));
  assertPitchQueryMatchesOracle(loop2, 62);
  assertPitchQueryMatchesOracle(loop2, 60);
}

void test_many_unrelated_companion_rows_do_not_full_materialize() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  LoopEventStore store;
  constexpr int kNoteCount = 1665;
  constexpr uint8_t kQueryPitch = 60;
  for (int i = 0; i < kNoteCount; ++i) {
    const uint32_t onTick = static_cast<uint32_t>(i) * 16u;
    const uint32_t offTick = onTick + 8u;
    const NoteId id = static_cast<NoteId>(i + 1);
    uint8_t pitch = 64;
    if (i < 10) {
      pitch = kQueryPitch;
    } else if (i < 110) {
      pitch = 62;
    }
    TEST_ASSERT_TRUE(storeAppendNoteOn(store, onTick, 1, pitch, 100, id));
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(offTick, 1, pitch, 0)));
  }
  loop.seedRecordPassFromStore(store);
  loop.loopLengthTicks = Config::TICKS_PER_BAR * 64;

  for (int i = 0; i < 100; ++i) {
    const NoteId id = static_cast<NoteId>(11 + i);
    TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, loop.saveNoteEditPass(0, makeDeleteRow(id)));
  }

  Loop::resetCommittedPitchQueryWork();
  SessionMidiEventVec optimized;
  loop.gatherCommittedNoteEventsForPitch(kQueryPitch, optimized);
  TEST_ASSERT_EQUAL_UINT32(0, Loop::committedEventsFullMaterializeCount());
  TEST_ASSERT_EQUAL_UINT32(3330, Loop::committedPitchQuerySourceEventsScanned());
  TEST_ASSERT_EQUAL_UINT32(0, Loop::committedPitchQueryEditRowsApplied());
  TEST_ASSERT_EQUAL(10, countNoteOns(optimized, kQueryPitch));
  TEST_ASSERT_EQUAL_UINT32(20, Loop::committedPitchQueryCandidateEvents());

  assertPitchQueryMatchesOracle(loop, kQueryPitch);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_overdub_start_establishes_source_view);
  RUN_TEST(test_record_start_does_not_keep_source_view);
  RUN_TEST(test_source_view_includes_edit_pass_geometry);
  RUN_TEST(test_source_view_stable_across_capture_appends_and_wraps);
  RUN_TEST(test_source_view_immutable_when_live_materialize_mutates);
  RUN_TEST(test_candidate_lookup_wrap_safe_high_then_low_capture_order);
  RUN_TEST(test_discard_and_commit_clear_source_view);
  RUN_TEST(test_pitch_query_matches_oracle_shorten_hide_and_unrelated);
  RUN_TEST(test_committed_pitch_mutation_c4_to_d4_both_queries);
  RUN_TEST(test_committed_pitch_chain_c4_d4_e4);
  RUN_TEST(test_edit_ordering_pitch_then_length_and_length_then_pitch);
  RUN_TEST(test_edit_ordering_pitch_then_delete_and_delete_then_pitch);
  RUN_TEST(test_many_unrelated_companion_rows_do_not_full_materialize);
  return UNITY_END();
}
