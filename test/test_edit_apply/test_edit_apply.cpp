//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/EditApply.cpp"
#include "../../src/LoopPasses.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/Utils/MemoryMonitor.cpp"
#include "../../src/Loop.cpp"
#include "../../src/NoteEditFocus.cpp"
#include "../../src/NoteEditSessionUndo.cpp"

#include "EditApply.h"
#include "EditPass.h"
#include "Loop.h"
#include "LoopPasses.h"
#include "LoopEventBuffer.h"
#include "EditSession.h"

namespace {

RecordPass makeRecordPassWithNote(PassId id, uint32_t tick) {
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(tick, 1, 60, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(tick + 10, 1, 60, 0)));
  ChunkIdList refs;
  store.detachChunksTo(refs);
  RecordPass pass{};
  pass.id = id;
  pass.state = CapturePassState::Active;
  pass.chunkRefs = std::move(refs);
  return pass;
}

OverdubPass makeOverdubPassWithNote(PassId id, uint32_t tick, uint8_t pitch) {
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(tick, 1, pitch, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(tick + 10, 1, pitch, 0)));
  ChunkIdList refs;
  store.detachChunksTo(refs);
  OverdubPass pass{};
  pass.id = id;
  pass.mergeSequence = 1;
  pass.state = CapturePassState::Active;
  pass.chunkRefs = std::move(refs);
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
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(startA, 1, pitch, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(endA, 1, pitch, 0)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(startB, 1, pitch, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(endB, 1, pitch, 0)));
  ChunkIdList refs;
  store.detachChunksTo(refs);
  RecordPass pass{};
  pass.id = id;
  pass.state = CapturePassState::Active;
  pass.chunkRefs = std::move(refs);
  return pass;
}

RecordPass makeEditRecordFixtureRecordPass(PassId id) {
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(8, 1, 60, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(104, 1, 60, 0)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(201, 1, 64, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(297, 1, 64, 0)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(392, 1, 67, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(488, 1, 67, 0)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(584, 1, 60, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(680, 1, 60, 0)));
  ChunkIdList refs;
  store.detachChunksTo(refs);
  RecordPass pass{};
  pass.id = id;
  pass.state = CapturePassState::Active;
  pass.chunkRefs = std::move(refs);
  return pass;
}

RecordPass makeEditRecordFixtureRecordPassCh5(PassId id) {
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(8, 5, 60, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(104, 5, 60, 0)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(585, 5, 60, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(680, 5, 60, 0)));
  ChunkIdList refs;
  store.detachChunksTo(refs);
  RecordPass pass{};
  pass.id = id;
  pass.state = CapturePassState::Active;
  pass.chunkRefs = std::move(refs);
  return pass;
}

RecordPass makeEditRecordFixtureRecordPassCh5_195830(PassId id) {
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(40, 5, 60, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(136, 5, 60, 0)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(232, 5, 64, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(328, 5, 64, 0)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(424, 5, 67, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(520, 5, 67, 0)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(616, 5, 60, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(712, 5, 60, 0)));
  ChunkIdList refs;
  store.detachChunksTo(refs);
  RecordPass pass{};
  pass.id = id;
  pass.state = CapturePassState::Active;
  pass.chunkRefs = std::move(refs);
  return pass;
}

EditPass makeDeleteRow(uint8_t ch, uint8_t note, uint32_t start, uint32_t end) {
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = EditActionType::Delete;
  row.propertyType = EditPropertyType::None;
  row.target = {ch, note, start, end};
  return row;
}

EditPass makeLengthRow(uint8_t ch, uint8_t note, uint32_t start, uint32_t end, uint32_t newEnd) {
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = EditActionType::Update;
  row.propertyType = EditPropertyType::Length;
  row.target = {ch, note, start, end};
  row.startTick = start;
  row.endTick = newEnd;
  return row;
}

EditPass makePitchRow(uint8_t ch, uint8_t note, uint32_t start, uint32_t end, uint8_t pitch) {
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = EditActionType::Update;
  row.propertyType = EditPropertyType::Pitch;
  row.target = {ch, note, start, end};
  row.pitch = pitch;
  return row;
}

EditPass makeNoteRangeRow(uint8_t ch, uint8_t note, uint32_t baseStart, uint32_t baseEnd,
                          uint32_t newStart, uint32_t newEnd) {
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = EditActionType::Update;
  row.propertyType = EditPropertyType::NoteRange;
  row.target = {ch, note, baseStart, baseEnd};
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

int countMatching(const MidiEventVec& flat, bool wantOn, uint8_t pitch, uint32_t tick) {
  int n = 0;
  for (const MidiEvent& e : flat) {
    const bool isOn = e.isNoteOn();
    if (isOn == wantOn && e.data.noteData.note == pitch && e.tick == tick) {
      n++;
    }
  }
  return n;
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



  pushEditPassRow(passes, 1, makeLengthRow(1, 60, 8, 104, 680));
  pushEditPassRow(passes, 2, makePitchRow(1, 60, 8, 680, 67));

  MidiEventVec flat;
  passes.materializeToEventVector(flat);

  // M0 became pitch 67 spanning 8..680.
  TEST_ASSERT_EQUAL(1, countMatching(flat, true, 67, 8));
  TEST_ASSERT_EQUAL(1, countMatching(flat, false, 67, 680));
  // P0 (the same-pitch neighbor) stays pitch 60, end intact — no orphaned note-on.
  TEST_ASSERT_EQUAL(1, countMatching(flat, true, 60, 584));
  TEST_ASSERT_EQUAL(1, countMatching(flat, false, 60, 680));
  // No stray pitch-60 note-on left at M0's old start.
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



  pushEditPassRow(passes, 1, makeLengthRow(1, 60, 192, 288, 672));

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


  pushEditPassRow(passes, 1, makeLengthRow(1, 60, 192, 288, 672));

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

// Regression for the HITL overlap round-trip: M0 lengthened to 681 overlaps P0 (585..682)
// without nesting. A pitch edit on M0 must not relabel P0's note-off (tick 682).
void test_change_pitch_on_overlapping_note_keeps_neighbor_endtick() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopPasses passes;
  passes.recordPass = makeRecordPassWithTwoNotes(1, 9, 105, 585, 682, 60);



  pushEditPassRow(passes, 1, makeLengthRow(1, 60, 9, 105, 681));
  pushEditPassRow(passes, 2, makePitchRow(1, 60, 9, 681, 67));

  MidiEventVec flat;
  passes.materializeToEventVector(flat);

  TEST_ASSERT_EQUAL(1, countMatching(flat, true, 67, 9));
  TEST_ASSERT_EQUAL(1, countMatching(flat, false, 67, 681));
  // P0 untouched: still pitch 60, end 682 (no orphaned note-on → no loop-end stretch).
  TEST_ASSERT_EQUAL(1, countMatching(flat, true, 60, 585));
  TEST_ASSERT_EQUAL(1, countMatching(flat, false, 60, 682));
  TEST_ASSERT_EQUAL(0, countMatching(flat, false, 67, 682));
}

void test_apply_edits_delete_note() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopPasses passes;
  passes.recordPass = makeRecordPassWithNote(1, 10);

  pushEditPassRow(passes, 1, makeDeleteRow(1, 60, 10, 20));

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
    TEST_ASSERT_TRUE(odStore.append(MidiEvent::NoteOn(100, 1, 60, 100)));
    TEST_ASSERT_TRUE(odStore.append(MidiEvent::NoteOff(110, 1, 60, 0)));
    ChunkIdList refs;
    odStore.detachChunksTo(refs);
    OverdubPass overdub{};
    overdub.id = 2;
    overdub.mergeSequence = 1;
    overdub.state = CapturePassState::Active;
    overdub.chunkRefs = std::move(refs);
    loop.passes.overdubPasses.push_back(std::move(overdub));
  }
  loop.nextPassId_ = 3;

  const EditPassId id = loop.saveNoteEditPass(0, makeDeleteRow(1, 60, 10, 20));
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

  const EditPassId delId = loop.saveNoteEditPass(2, makeDeleteRow(1, 60, 10, 20));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, delId);
  const EditPass& delPass = loop.passes.editPasses.back();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPassType::Note),
                          static_cast<uint8_t>(delPass.passType));
  TEST_ASSERT_EQUAL_UINT8(2u, delPass.editPassIndex);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditActionType::Delete),
                          static_cast<uint8_t>(delPass.actionType));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPropertyType::None),
                          static_cast<uint8_t>(delPass.propertyType));

  const EditPassId pitchId = loop.saveNoteEditPass(3, makePitchRow(1, 60, 10, 20, 67));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, pitchId);
  const EditPass& pitchPass = loop.passes.editPasses.back();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditActionType::Update),
                          static_cast<uint8_t>(pitchPass.actionType));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPropertyType::Pitch),
                          static_cast<uint8_t>(pitchPass.propertyType));

  const EditPassId lengthId = loop.saveNoteEditPass(4, makeLengthRow(1, 60, 10, 20, 28));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, lengthId);
  const EditPass& lengthPass = loop.passes.editPasses.back();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditActionType::Update),
                          static_cast<uint8_t>(lengthPass.actionType));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPropertyType::Length),
                          static_cast<uint8_t>(lengthPass.propertyType));

  const EditPassId moveStartId =
      loop.saveNoteEditPass(5, makeNoteRangeRow(1, 60, 10, 20, 14, 24));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, moveStartId);
  const EditPass& moveStartPass = loop.passes.editPasses.back();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPropertyType::NoteRange),
                          static_cast<uint8_t>(moveStartPass.propertyType));

  const EditPassId moveEndId =
      loop.saveNoteEditPass(6, makeNoteRangeRow(1, 60, 10, 20, 10, 24));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, moveEndId);
  const EditPass& moveEndPass = loop.passes.editPasses.back();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPropertyType::NoteRange),
                          static_cast<uint8_t>(moveEndPass.propertyType));
}

void test_edit_pass_state_toggle_does_not_append_edit_actions() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;

  const EditPassId id = loop.saveNoteEditPass(0, makeDeleteRow(1, 60, 10, 20));
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
  const EditPassId id = loop.saveNoteEditPass(0, makeDeleteRow(1, 60, 10, 20));
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

  loop.saveNoteEditPass(0, makePitchRow(1, 60, 8, 18, 67));
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
  add.addedEvents.push_back(MidiEvent::NoteOn(48, 5, 60, 80));
  add.addedEvents.push_back(MidiEvent::NoteOff(72, 5, 60, 0));
  const EditPassId id = loop.saveNoteEditPass(0, add);
  TEST_ASSERT_EQUAL(2u, id);

  CowLoopEventStore session;
  loop.rematerializeEditView(session.mutStore());
  session.discardFlatCache();

  const auto& flat = session.readFlat();
  TEST_ASSERT_EQUAL(4u, flat.size());
  TEST_ASSERT_EQUAL(1, countMatching(flat, true, 60, 10));
  TEST_ASSERT_EQUAL(1, countMatching(flat, true, 60, 48));
}

void test_move_note_uses_track_channel() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopPasses passes;
  passes.recordPass = makeRecordPassWithNote(1, 10);
  passes.recordPass.chunkRefs.clear();
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(10, 5, 60, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(106, 5, 60, 0)));
  store.detachChunksTo(passes.recordPass.chunkRefs);

  pushEditPassRow(passes, 1, makeNoteRangeRow(5, 60, 10, 106, 58, 154));

  MidiEventVec flat;
  passes.materializeToEventVector(flat);
  TEST_ASSERT_EQUAL(1, countMatching(flat, true, 60, 58));
  TEST_ASSERT_EQUAL(0, countMatching(flat, true, 60, 10));

  LoopPasses badPasses = passes;
  badPasses.editPasses.clear();
  EditPass badRow = makeNoteRangeRow(5, 60, 10, 106, 58, 154);
  badRow.target.channel = 1;
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
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(8, 5, 60, 100)));
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(104, 5, 60, 0)));
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(200, 5, 64, 100)));
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(296, 5, 64, 0)));
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(392, 5, 67, 100)));
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(488, 5, 67, 0)));
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(585, 5, 60, 100)));
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(680, 5, 60, 0)));
    store.detachChunksTo(loop.passes.recordPass.chunkRefs);
  }

  const EditPassId id = loop.saveNoteEditPass(0, makeLengthRow(5, 60, 8, 104, 680));
  TEST_ASSERT_EQUAL(1u, id);

  CowLoopEventStore session;
  loop.rematerializeEditView(session.mutStore());
  session.discardFlatCache();

  const std::vector<NoteUtils::DisplayNote> notes =
      NoteUtils::reconstructNotes(session.readFlat(), kLoopLength, false);
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

  const EditPassId id = loop.saveNoteEditPass(0, makeLengthRow(5, 60, 40, 136, 712));
  TEST_ASSERT_EQUAL(1u, id);

  // commitEditAction: discard live flat, replay takes+edits, load session store (matches firmware).
  CowLoopEventStore session;
  session.discardFlatCache();
  MidiEventVec loopMidiEventsFromTakesAndEdits;
  loop.passes.materializeToEventVector(loopMidiEventsFromTakesAndEdits, kLoopLength);
  session.mutStore().loadFromFlat(loopMidiEventsFromTakesAndEdits);
  session.discardFlatCache();

  const std::vector<NoteUtils::DisplayNote> notes =
      NoteUtils::reconstructNotes(session.readFlat(), kLoopLength, false);
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
  for (const MidiEvent& e : session.readFlat()) {
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


  pushEditPassRow(passes, 1, makeLengthRow(1, 60, 8, 104, 680));
  pushEditPassRow(passes, 2, makeDeleteRow(1, 60, 584, 680));
  pushEditPassRow(passes, 3, makePitchRow(1, 60, 8, 680, 67));

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

  pushEditPassRow(passes, 1, makeLengthRow(1, 60, 584, 680, 495));
  pushEditPassRow(passes, 2, makeNoteRangeRow(1, 60, 8, 680, 496, 1168));
  pushEditPassRow(passes, 3, makePitchRow(1, 60, 8, 680, 67));

  MidiEventVec flatOrdered;
  passes.materializeToEventVector(flatOrdered, kLoopLength);
  TEST_ASSERT_EQUAL(1, countMatching(flatOrdered, true, 67, 496));
  TEST_ASSERT_EQUAL(1, countMatching(flatOrdered, false, 67, 1168));
  TEST_ASSERT_EQUAL(1, countMatching(flatOrdered, true, 60, 584));
  TEST_ASSERT_EQUAL(1, countMatching(flatOrdered, false, 60, 495));

  LoopPasses passes2;
  passes2.recordPass = makeRecordPassWithTwoNotes(1, 8, 680, 584, 680, 60);
  pushEditPassRow(passes2, 1, makeNoteRangeRow(1, 60, 8, 680, 496, 1168));
  pushEditPassRow(passes2, 2, makePitchRow(1, 60, 8, 680, 67));
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

  pushEditPassRow(passes, 1, makeDeleteRow(1, 60, 584, 680));
  pushEditPassRow(passes, 2, makeLengthRow(1, 60, 8, 104, 680));
  MidiEventVec flat;
  passes.materializeToEventVector(flat);
  TEST_ASSERT_EQUAL(1, countMatching(flat, true, 60, 8));
  TEST_ASSERT_EQUAL(1, countMatching(flat, false, 60, 680));
  TEST_ASSERT_EQUAL(0, countMatching(flat, true, 60, 584));
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
  const EditPassId editId = loop.saveNoteEditPass(0, makePitchRow(1, 60, 10, 20, 67));

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
      loop.saveNoteEditPass(0, makePitchRow(1, 60, 10, 20, 67));
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
  loop.passes.materializeToEventVector(session.mutFlat(), loop.loopLengthTicks);
  session.syncFlatToStore();

  NoteEditFocus focus;
  focus.active = true;
  focus.commitBaseline = {60, 100, 8, 104};
  focus.last = focus.commitBaseline;
  focus.moving = {1, 60, 8, 104};
  focus.movingNoteRange = {8, 104};

  const SessionUndoEntry entry =
      buildSessionUndoEntry(focus, NoteEditSelection{}, session.readFlat(), 1,
                            loop.loopLengthTicks, EditPassIdList{});

  focus.last.startTick = 496;
  focus.last.endTick = 1168;
  noteEditFocusApplyMoveEnd(focus, focus.last.startTick, focus.last.endTick);
  MidiEventVec& flat = session.mutFlat();
  for (MidiEvent& evt : flat) {
    if (evt.isNoteOn() && evt.channel == 1 && evt.data.noteData.note == 60 && evt.tick == 8) {
      evt.tick = 496;
    }
    if (evt.isNoteOff() && evt.channel == 1 && evt.data.noteData.note == 60 && evt.tick == 104) {
      evt.tick = 1168;
    }
  }
  session.syncFlatToStore();
  TEST_ASSERT_EQUAL(1, countMatching(session.readFlat(), true, 60, 496));

  SessionUndoEntry undoEntry = entry;
  SessionUndoEntry redoPayload =
      buildSessionUndoEntry(focus, NoteEditSelection{}, session.readFlat(), 1,
                            loop.loopLengthTicks, EditPassIdList{});
  undoEntry.redoEditRows = std::move(redoPayload.editRows);
  undoEntry.redoFocus = std::move(redoPayload.focus);
  undoEntry.redoSelection = redoPayload.selection;
  undoEntry.hasRedoPayload = true;

  applySessionUndoEntry(loop, session, undoEntry, loop.loopLengthTicks, EditPassIdList{});
  TEST_ASSERT_EQUAL(1, countMatching(session.readFlat(), true, 60, 8));
  TEST_ASSERT_EQUAL(0, countMatching(session.readFlat(), true, 60, 496));

  applySessionRedoEntry(loop, session, undoEntry, loop.loopLengthTicks, EditPassIdList{});
  TEST_ASSERT_EQUAL(1, countMatching(session.readFlat(), true, 60, 496));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_change_pitch_on_lengthened_note_keeps_same_pitch_neighbor);
  RUN_TEST(test_lengthen_after_move_keeps_p0_fixture_gate);
  RUN_TEST(test_change_length_rematerialize_keeps_p0_off_not_loop_end);
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
  RUN_TEST(test_global_undo_overdub_pass_added_disables_overdub);
  RUN_TEST(test_global_undo_note_edit_pass_closed_disables_edit_rows);
  RUN_TEST(test_global_undo_three_step_restores_record_baseline);
  RUN_TEST(test_session_undo_move_back_insert_before_save_note_edit_pass);
  return UNITY_END();
}
