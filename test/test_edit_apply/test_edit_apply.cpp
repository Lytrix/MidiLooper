//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/EditApply.cpp"
#include "../../src/LoopPasses.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/Loop.cpp"

#include "EditApply.h"
#include "EditPass.h"
#include "Loop.h"
#include "LoopPasses.h"
#include "LoopEventBuffer.h"
#include "NoteEditSession.h"

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

void pushEditPassChange(LoopPasses& passes, EditPassId id, EditChangeList changes) {
  EditPass editPass{};
  editPass.id = id;
  editPass.kind = EditPassKind::NoteEdit;
  editPass.state = EditPassState::Active;
  editPass.changes = std::move(changes);
  passes.editPasses.push_back(editPass);
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



  EditPass lengthen{};
  lengthen.kind = EditPassKind::NoteEdit;
  lengthen.id = 1;
  lengthen.state = EditPassState::Active;
  EditChange grow;
  grow.type = EditChangeType::ChangeLength;
  grow.target = {1, 60, 8, 104};  // M0 (original end)
  grow.newEndTick = 680;          // now shares end tick with P0
  lengthen.changes.push_back(grow);
  passes.editPasses.push_back(lengthen);

  EditPass repitch{};
  repitch.kind = EditPassKind::NoteEdit;
  repitch.id = 2;
  repitch.state = EditPassState::Active;
  EditChange pitch;
  pitch.type = EditChangeType::ChangePitch;
  pitch.target = {1, 60, 8, 680};  // M0 ref end advanced after the length commit
  pitch.newPitch = 67;
  repitch.changes.push_back(pitch);
  passes.editPasses.push_back(repitch);

  MidiEventVec flat;
  passes.materializeToFlat(flat);

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



  EditPass lengthen{};
  lengthen.kind = EditPassKind::NoteEdit;
  lengthen.id = 1;
  lengthen.state = EditPassState::Active;
  EditChange grow;
  grow.type = EditChangeType::ChangeLength;
  grow.target = {1, 60, 192, 288};
  grow.newEndTick = 672;  // fixture step 14 — shares release tick with P0
  lengthen.changes.push_back(grow);
  passes.editPasses.push_back(lengthen);

  MidiEventVec flat;
  passes.materializeToFlat(flat);

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


  EditPass lengthen{};
  lengthen.kind = EditPassKind::NoteEdit;
  lengthen.id = 1;
  lengthen.state = EditPassState::Active;
  EditChange grow;
  grow.type = EditChangeType::ChangeLength;
  grow.target = {1, 60, 192, 288};
  grow.newEndTick = 672;
  lengthen.changes.push_back(grow);
  passes.editPasses.push_back(lengthen);

  MidiEventVec flat;
  passes.materializeToFlat(flat, kLoopLength);

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



  EditPass lengthen{};
  lengthen.kind = EditPassKind::NoteEdit;
  lengthen.id = 1;
  lengthen.state = EditPassState::Active;
  EditChange grow;
  grow.type = EditChangeType::ChangeLength;
  grow.target = {1, 60, 9, 105};
  grow.newEndTick = 681;  // overlaps P0 (585..682) but ends one tick earlier
  lengthen.changes.push_back(grow);
  passes.editPasses.push_back(lengthen);

  EditPass repitch{};
  repitch.kind = EditPassKind::NoteEdit;
  repitch.id = 2;
  repitch.state = EditPassState::Active;
  EditChange pitch;
  pitch.type = EditChangeType::ChangePitch;
  pitch.target = {1, 60, 9, 681};
  pitch.newPitch = 67;
  repitch.changes.push_back(pitch);
  passes.editPasses.push_back(repitch);

  MidiEventVec flat;
  passes.materializeToFlat(flat);

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

  EditPass editPass{};
  editPass.id = 1;
  editPass.kind = EditPassKind::NoteEdit;
  editPass.state = EditPassState::Active;
  EditChange del;
  del.type = EditChangeType::DeleteNote;
  del.target = {1, 60, 10, 20};
  editPass.changes.push_back(del);
  passes.editPasses.push_back(editPass);

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

  EditChange del;
  del.type = EditChangeType::DeleteNote;
  del.target = {1, 60, 10, 20};
  const EditPassId id = loop.saveNoteEditPass(0, EditChangeList{del});
  TEST_ASSERT_EQUAL(3u, id);
  TEST_ASSERT_EQUAL(2u, loop.passes.capturePassCount());
  TEST_ASSERT_EQUAL(1u, loop.passes.editPasses.size());

  MidiEventVec flat;
  loop.passes.materializeToFlat(flat);
  TEST_ASSERT_EQUAL(2u, flat.size());
}

void test_disable_edits_restores_take_only_view() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.passes.recordPass = makeRecordPassWithNote(1, 10);
  EditChange del;
  del.type = EditChangeType::DeleteNote;
  del.target = {1, 60, 10, 20};
  const EditPassId id = loop.saveNoteEditPass(0, EditChangeList{del});
  loop.disableEditPasses(EditPassIdList{id});
  MidiEventVec flat;
  loop.passes.materializeToFlat(flat);
  TEST_ASSERT_EQUAL(2u, flat.size());
}

void test_reset_take_timeline_clears_stale_edits() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.passes.recordPass = makeRecordPassWithNote(1, 8);
  loop.nextPassId_ = 2;

  EditChange pitch;
  pitch.type = EditChangeType::ChangePitch;
  pitch.target = {1, 60, 8, 18};
  pitch.newPitch = 67;
  loop.saveNoteEditPass(0, EditChangeList{pitch});
  TEST_ASSERT_EQUAL(1u, loop.passes.editPasses.size());

  MidiEventVec withStaleEdit;
  loop.passes.materializeToFlat(withStaleEdit);
  TEST_ASSERT_EQUAL(67, withStaleEdit[0].data.noteData.note);

  loop.resetPassTimeline();
  TEST_ASSERT_TRUE(loop.passes.editPasses.empty());
  TEST_ASSERT_EQUAL(1u, loop.nextPassId_);
  TEST_ASSERT_FALSE(loop.isEditStateDirty());

  loop.passes.recordPass = makeRecordPassWithNote(1, 8);
  MidiEventVec fresh;
  loop.passes.materializeToFlat(fresh);
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
  loop.passes.recordPass = makeRecordPassWithNote(1, 10);
  loop.nextPassId_ = 2;

  EditChange add;
  add.type = EditChangeType::AddNote;
  add.addedEvents.push_back(MidiEvent::NoteOn(48, 5, 60, 80));
  add.addedEvents.push_back(MidiEvent::NoteOff(72, 5, 60, 0));
  const EditPassId id = loop.saveNoteEditPass(0, EditChangeList{add});
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

  EditChange ch;
  ch.type = EditChangeType::MoveNote;
  ch.target = {5, 60, 10, 106};
  ch.newStartTick = 58;
  ch.newEndTick = 154;
  pushEditPassChange(passes, 1, EditChangeList{ch});

  MidiEventVec flat;
  passes.materializeToFlat(flat);
  TEST_ASSERT_EQUAL(1, countMatching(flat, true, 60, 58));
  TEST_ASSERT_EQUAL(0, countMatching(flat, true, 60, 10));

  EditChange badCh = ch;
  badCh.target.channel = 1;
  LoopPasses badPasses = passes;
  badPasses.editPasses.clear();
  pushEditPassChange(badPasses, 1, EditChangeList{badCh});
  MidiEventVec flatBad;
  badPasses.materializeToFlat(flatBad);
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

  EditChange lengthen;
  lengthen.type = EditChangeType::ChangeLength;
  lengthen.target = {5, 60, 8, 104};
  lengthen.newEndTick = 680;
  const EditPassId id = loop.saveNoteEditPass(0, EditChangeList{lengthen});
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

  EditChange lengthen;
  lengthen.type = EditChangeType::ChangeLength;
  lengthen.target = {5, 60, 40, 136};
  lengthen.newEndTick = 712;
  const EditPassId id = loop.saveNoteEditPass(0, EditChangeList{lengthen});
  TEST_ASSERT_EQUAL(1u, id);

  // commitEditAction: discard live flat, replay takes+edits, load session store (matches firmware).
  CowLoopEventStore session;
  session.discardFlatCache();
  MidiEventVec loopMidiEventsFromTakesAndEdits;
  loop.passes.materializeToFlat(loopMidiEventsFromTakesAndEdits, kLoopLength);
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


  EditChange lengthen;
  lengthen.type = EditChangeType::ChangeLength;
  lengthen.target = {1, 60, 8, 104};
  lengthen.newEndTick = 680;
  pushEditPassChange(passes, 1, EditChangeList{lengthen});

  EditChangeList boundary;
  EditChange del;
  del.type = EditChangeType::DeleteNote;
  del.target = {1, 60, 584, 680};
  boundary.push_back(del);
  EditChange pitch;
  pitch.type = EditChangeType::ChangePitch;
  pitch.target = {1, 60, 8, 680};
  pitch.newPitch = 67;
  boundary.push_back(pitch);
  pushEditPassChange(passes, 2, std::move(boundary));

  MidiEventVec flat;
  passes.materializeToFlat(flat, kLoopLength);

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

  EditChangeList ordered;
  EditChange shorten;
  shorten.type = EditChangeType::ChangeLength;
  shorten.target = {1, 60, 584, 680};
  shorten.newEndTick = 495;
  ordered.push_back(shorten);
  EditChange move;
  move.type = EditChangeType::MoveNote;
  move.target = {1, 60, 8, 680};
  move.newStartTick = 496;
  move.newEndTick = 1168;
  ordered.push_back(move);
  EditChange pitch;
  pitch.type = EditChangeType::ChangePitch;
  pitch.target = {1, 60, 8, 680};
  pitch.newPitch = 67;
  ordered.push_back(pitch);


  pushEditPassChange(passes, 1, std::move(ordered));

  MidiEventVec flatOrdered;
  passes.materializeToFlat(flatOrdered, kLoopLength);
  TEST_ASSERT_EQUAL(1, countMatching(flatOrdered, true, 67, 496));
  TEST_ASSERT_EQUAL(1, countMatching(flatOrdered, false, 67, 1168));
  TEST_ASSERT_EQUAL(1, countMatching(flatOrdered, true, 60, 584));
  TEST_ASSERT_EQUAL(1, countMatching(flatOrdered, false, 60, 495));

  LoopPasses passes2;
  passes2.recordPass = makeRecordPassWithTwoNotes(1, 8, 680, 584, 680, 60);
  EditChangeList wrongOrder;
  EditChange move2 = move;
  EditChange pitch2 = pitch;
  wrongOrder.push_back(move2);
  wrongOrder.push_back(pitch2);
  pushEditPassChange(passes2, 1, std::move(wrongOrder));
  MidiEventVec flatWrong;
  passes2.materializeToFlat(flatWrong, kLoopLength);
  TEST_ASSERT_EQUAL(1, countMatching(flatWrong, true, 67, 496));
  TEST_ASSERT_EQUAL(0, countMatching(flatWrong, false, 60, 495));
  TEST_ASSERT_EQUAL(1, countMatching(flatWrong, false, 60, 680));
}

void test_pre_commit_delete_before_mover_change_length() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopPasses passes;
  passes.recordPass = makeRecordPassWithTwoNotes(1, 8, 104, 584, 680, 60);

  EditChangeList preCommit;
  EditChange del;
  del.type = EditChangeType::DeleteNote;
  del.target = {1, 60, 584, 680};
  preCommit.push_back(del);
  EditChange lengthen;
  lengthen.type = EditChangeType::ChangeLength;
  lengthen.target = {1, 60, 8, 104};
  lengthen.newEndTick = 680;
  preCommit.push_back(lengthen);


  pushEditPassChange(passes, 1, std::move(preCommit));
  MidiEventVec flat;
  passes.materializeToFlat(flat);
  TEST_ASSERT_EQUAL(1, countMatching(flat, true, 60, 8));
  TEST_ASSERT_EQUAL(1, countMatching(flat, false, 60, 680));
  TEST_ASSERT_EQUAL(0, countMatching(flat, true, 60, 584));
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
  RUN_TEST(test_lengthen_commit_rematerialize_hitl_fixture);
  RUN_TEST(test_change_length_rematerialize_hitl_195830_ticks);
  RUN_TEST(test_overlap_round_trip_replay_lengthen_delete_pitch);
  RUN_TEST(test_pre_commit_order_overlap_changes_before_move_and_pitch);
  RUN_TEST(test_pre_commit_delete_before_mover_change_length);
  return UNITY_END();
}
