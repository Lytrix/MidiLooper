//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/EditApply.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/Loop.cpp"

#include "EditApply.h"
#include "Edit.h"
#include "Loop.h"
#include "LoopEventBuffer.h"
#include "NoteEditSession.h"

namespace {

Take makeTakeWithNote(TakeId id, uint32_t tick) {
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(tick, 1, 60, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(tick + 10, 1, 60, 0)));
  ChunkIdList refs;
  store.detachChunksTo(refs);
  Take take{};
  take.id = id;
  take.mergeSequence = 0;
  take.state = TakeState::Active;
  take.type = TakeType::Record;
  take.chunkRefs = std::move(refs);
  return take;
}

Take makeTakeWithTwoNotes(TakeId id, uint32_t startA, uint32_t endA, uint32_t startB,
                          uint32_t endB, uint8_t pitch) {
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(startA, 1, pitch, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(endA, 1, pitch, 0)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(startB, 1, pitch, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(endB, 1, pitch, 0)));
  ChunkIdList refs;
  store.detachChunksTo(refs);
  Take take{};
  take.id = id;
  take.mergeSequence = 0;
  take.state = TakeState::Active;
  take.type = TakeType::Record;
  take.chunkRefs = std::move(refs);
  return take;
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
  TakeVec takes;
  // M0: on@8 off@104, P0: on@584 off@680, both pitch 60.
  takes.push_back(makeTakeWithTwoNotes(1, 8, 104, 584, 680, 60));

  EditVec edits;

  Edit lengthen{};
  lengthen.id = 1;
  lengthen.state = EditState::Active;
  EditChange grow;
  grow.type = EditChangeType::ChangeLength;
  grow.target = {1, 60, 8, 104};  // M0 (original end)
  grow.newEndTick = 680;          // now shares end tick with P0
  lengthen.changes.push_back(grow);
  edits.push_back(lengthen);

  Edit repitch{};
  repitch.id = 2;
  repitch.state = EditState::Active;
  EditChange pitch;
  pitch.type = EditChangeType::ChangePitch;
  pitch.target = {1, 60, 8, 680};  // M0 ref end advanced after the length commit
  pitch.newPitch = 67;
  repitch.changes.push_back(pitch);
  edits.push_back(repitch);

  MidiEventVec flat;
  applyEditsToFlat(takes, edits, flat);

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
  TakeVec takes;
  // M0 @ step 4 (192-288 after +1 beat move); P0 @ step 12 (576-672, 2-step gate).
  takes.push_back(makeTakeWithTwoNotes(1, 192, 288, 576, 672, 60));

  EditVec edits;

  Edit lengthen{};
  lengthen.id = 1;
  lengthen.state = EditState::Active;
  EditChange grow;
  grow.type = EditChangeType::ChangeLength;
  grow.target = {1, 60, 192, 288};
  grow.newEndTick = 672;  // fixture step 14 — shares release tick with P0
  lengthen.changes.push_back(grow);
  edits.push_back(lengthen);

  MidiEventVec flat;
  applyEditsToFlat(takes, edits, flat);

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

// Simulates commitPendingLengthAction rematerialize after move+lengthen (reselect path).
void test_change_length_rematerialize_keeps_p0_off_not_loop_end() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  constexpr uint32_t kLoopLength = 1536;
  TakeVec takes;
  takes.push_back(makeTakeWithTwoNotes(1, 192, 288, 576, 672, 60));

  EditVec edits;
  Edit lengthen{};
  lengthen.id = 1;
  lengthen.state = EditState::Active;
  EditChange grow;
  grow.type = EditChangeType::ChangeLength;
  grow.target = {1, 60, 192, 288};
  grow.newEndTick = 672;
  lengthen.changes.push_back(grow);
  edits.push_back(lengthen);

  MidiEventVec flat;
  applyEditsToFlat(takes, edits, flat, kLoopLength);

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
  TakeVec takes;
  takes.push_back(makeTakeWithTwoNotes(1, 9, 105, 585, 682, 60));

  EditVec edits;

  Edit lengthen{};
  lengthen.id = 1;
  lengthen.state = EditState::Active;
  EditChange grow;
  grow.type = EditChangeType::ChangeLength;
  grow.target = {1, 60, 9, 105};
  grow.newEndTick = 681;  // overlaps P0 (585..682) but ends one tick earlier
  lengthen.changes.push_back(grow);
  edits.push_back(lengthen);

  Edit repitch{};
  repitch.id = 2;
  repitch.state = EditState::Active;
  EditChange pitch;
  pitch.type = EditChangeType::ChangePitch;
  pitch.target = {1, 60, 9, 681};
  pitch.newPitch = 67;
  repitch.changes.push_back(pitch);
  edits.push_back(repitch);

  MidiEventVec flat;
  applyEditsToFlat(takes, edits, flat);

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
  TakeVec takes;
  takes.push_back(makeTakeWithNote(1, 10));
  EditVec edits;
  Edit edit{};
  edit.id = 1;
  edit.state = EditState::Active;
  EditChange del;
  del.type = EditChangeType::DeleteNote;
  del.target = {1, 60, 10, 20};
  edit.changes.push_back(del);
  edits.push_back(edit);

  LoopEventStore out;
  applyEdits(takes, edits, out);
  TEST_ASSERT_TRUE(out.empty());
}

void test_save_edit_appends_without_collapsing_takes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.takes.push_back(makeTakeWithNote(1, 10));
  loop.takes.push_back(makeTakeWithNote(2, 100));
  loop.nextTakeId_ = 3;

  EditChange del;
  del.type = EditChangeType::DeleteNote;
  del.target = {1, 60, 10, 20};
  const EditId id = loop.saveEdit(0, EditChangeList{del});
  TEST_ASSERT_EQUAL(1u, id);
  TEST_ASSERT_EQUAL(2u, loop.takes.size());
  TEST_ASSERT_EQUAL(1u, loop.edits.size());

  MidiEventVec flat;
  applyEditsToFlat(loop.takes, loop.edits, flat);
  TEST_ASSERT_EQUAL(2u, flat.size());
}

void test_disable_edits_restores_take_only_view() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.takes.push_back(makeTakeWithNote(1, 10));
  EditChange del;
  del.type = EditChangeType::DeleteNote;
  del.target = {1, 60, 10, 20};
  const EditId id = loop.saveEdit(0, EditChangeList{del});
  loop.disableEdits(EditIdList{id});
  MidiEventVec flat;
  applyEditsToFlat(loop.takes, loop.edits, flat);
  TEST_ASSERT_EQUAL(2u, flat.size());
}

void test_reset_take_timeline_clears_stale_edits() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.takes.push_back(makeTakeWithNote(1, 8));
  loop.nextTakeId_ = 2;

  EditChange pitch;
  pitch.type = EditChangeType::ChangePitch;
  pitch.target = {1, 60, 8, 18};
  pitch.newPitch = 67;
  loop.saveEdit(0, EditChangeList{pitch});
  TEST_ASSERT_EQUAL(1u, loop.edits.size());

  MidiEventVec withStaleEdit;
  applyEditsToFlat(loop.takes, loop.edits, withStaleEdit);
  TEST_ASSERT_EQUAL(67, withStaleEdit[0].data.noteData.note);

  loop.resetTakeTimeline();
  TEST_ASSERT_TRUE(loop.edits.empty());
  TEST_ASSERT_EQUAL(1u, loop.nextEditId_);
  TEST_ASSERT_FALSE(loop.isEditStateDirty());

  loop.takes.push_back(makeTakeWithNote(1, 8));
  MidiEventVec fresh;
  applyEditsToFlat(loop.takes, loop.edits, fresh);
  TEST_ASSERT_EQUAL(2u, fresh.size());
  TEST_ASSERT_EQUAL(60, fresh[0].data.noteData.note);
  TEST_ASSERT_EQUAL(60, fresh[1].data.noteData.note);
}

void test_note_edit_session_undo_stack() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  NoteEditSessionUndoStack stack;
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(1, 1, 60, 100)));
  stack.pushBeforeMutation(store);
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(10, 1, 60, 0)));
  TEST_ASSERT_TRUE(stack.canUndo());
  const auto snap = stack.popUndoSnapshot();
  TEST_ASSERT_NOT_NULL(snap.get());
  TEST_ASSERT_EQUAL(1u, snap->size());
}

void test_add_note_rematerialize_session_store() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.takes.push_back(makeTakeWithNote(1, 10));
  loop.nextTakeId_ = 2;

  EditChange add;
  add.type = EditChangeType::AddNote;
  add.addedEvents.push_back(MidiEvent::NoteOn(48, 5, 60, 80));
  add.addedEvents.push_back(MidiEvent::NoteOff(72, 5, 60, 0));
  const EditId id = loop.saveEdit(0, EditChangeList{add});
  TEST_ASSERT_EQUAL(1u, id);

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
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(10, 5, 60, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(106, 5, 60, 0)));
  ChunkIdList refs;
  store.detachChunksTo(refs);
  Take take{};
  take.id = 1;
  take.mergeSequence = 0;
  take.state = TakeState::Active;
  take.type = TakeType::Record;
  take.chunkRefs = std::move(refs);
  TakeVec takes;
  takes.push_back(take);

  Edit move{};
  move.id = 1;
  move.state = EditState::Active;
  EditChange ch;
  ch.type = EditChangeType::MoveNote;
  ch.target = {5, 60, 10, 106};
  ch.newStartTick = 58;
  ch.newEndTick = 154;
  move.changes.push_back(ch);
  EditVec edits;
  edits.push_back(move);

  MidiEventVec flat;
  applyEditsToFlat(takes, edits, flat);
  TEST_ASSERT_EQUAL(1, countMatching(flat, true, 60, 58));
  TEST_ASSERT_EQUAL(0, countMatching(flat, true, 60, 10));

  Edit badMove = move;
  badMove.changes[0].target.channel = 1;
  EditVec badEdits;
  badEdits.push_back(badMove);
  MidiEventVec flatBad;
  applyEditsToFlat(takes, badEdits, flatBad);
  TEST_ASSERT_EQUAL(1, countMatching(flatBad, true, 60, 10));
  TEST_ASSERT_EQUAL(0, countMatching(flatBad, true, 60, 58));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_change_pitch_on_lengthened_note_keeps_same_pitch_neighbor);
  RUN_TEST(test_lengthen_after_move_keeps_p0_fixture_gate);
  RUN_TEST(test_change_length_rematerialize_keeps_p0_off_not_loop_end);
  RUN_TEST(test_change_pitch_on_overlapping_note_keeps_neighbor_endtick);
  RUN_TEST(test_apply_edits_delete_note);
  RUN_TEST(test_save_edit_appends_without_collapsing_takes);
  RUN_TEST(test_disable_edits_restores_take_only_view);
  RUN_TEST(test_reset_take_timeline_clears_stale_edits);
  RUN_TEST(test_note_edit_session_undo_stack);
  RUN_TEST(test_add_note_rematerialize_session_store);
  RUN_TEST(test_move_note_uses_track_channel);
  return UNITY_END();
}
