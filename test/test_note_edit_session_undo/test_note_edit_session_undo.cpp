//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/EditApply.cpp"
#include "../../src/Utils/MemoryMonitor.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/LoopPasses.cpp"
#include "../../src/Loop.cpp"
#include "../../src/NoteEditFocus.cpp"
#include "../../src/NoteEditSessionUndo.cpp"

#include "EditPass.h"
#include "Globals.h"
#include "Loop.h"
#include "LoopEventStore.h"
#include "NoteEditSession.h"
#include "NoteEditSessionUndo.h"

namespace {

RecordPass makeRecordPassWithNote(uint8_t channel, uint32_t startTick) {
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(startTick, channel, 60, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(startTick + 48, channel, 60, 0)));
  ChunkIdList refs;
  store.detachChunksTo(refs);
  RecordPass pass{};
  pass.id = 1;
  pass.state = CapturePassState::Active;
  pass.chunkRefs = std::move(refs);
  return pass;
}

void applyMoveToSession(NoteEditFocus& focus, MidiEventVec& flat, uint8_t channel,
                        uint32_t newStart) {
  const uint32_t len = focus.last.endTick - focus.last.startTick;
  focus.last.startTick = newStart;
  focus.last.endTick = newStart + len;
  noteEditFocusApplyMoveEnd(focus, focus.last.startTick, focus.last.endTick);
  for (MidiEvent& evt : flat) {
    if (evt.isNoteOn() && evt.channel == channel && evt.data.noteData.note == focus.last.pitch &&
        evt.tick == focus.commitBaseline.startTick) {
      evt.tick = focus.last.startTick;
    }
    if (evt.isNoteOff() && evt.channel == channel && evt.data.noteData.note == focus.last.pitch &&
        evt.tick == focus.commitBaseline.endTick) {
      evt.tick = focus.last.endTick;
    }
  }
}

}  // namespace

void test_session_undo_stack_push_entry() {
  NoteEditSessionUndoStack stack;
  SessionUndoEntry entry;
  entry.selection.hasNote = true;
  TEST_ASSERT_TRUE(stack.pushEntry(entry));
  TEST_ASSERT_TRUE(stack.canUndo());
  const SessionUndoEntry* target = stack.popUndoTarget();
  TEST_ASSERT_NOT_NULL(target);
}

void test_session_undo_entry_matches_clone_restore() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 768;
  loop.passes.recordPass = makeRecordPassWithNote(5, 10);
  loop.nextPassId_ = 2;

  CowLoopEventStore session;
  loop.rematerializeEditView(session.mutStore());
  session.discardFlatCache();

  NoteEditFocus focus;
  rebuildNoteEditFocusFromStore(focus, session.readFlat(), 5, loop.loopLengthTicks, 0);
  focus.last = focus.commitBaseline;

  const auto cloneSnap = session.readStore().cloneShared();
  const SessionUndoEntry entry =
      buildSessionUndoEntry(focus, NoteEditSelection{}, session.readFlat(), 5, loop.loopLengthTicks);

  MidiEventVec& flat = session.mutFlat();
  applyMoveToSession(focus, flat, 5, 58);
  session.syncFlatToStore();

  CowLoopEventStore viaClone;
  viaClone.restoreFromSnapshot(cloneSnap);

  CowLoopEventStore viaEntry;
  applySessionUndoEntry(loop, viaEntry, entry, loop.loopLengthTicks);

  TEST_ASSERT_TRUE(sessionUndoStoresMatch(viaClone.readStore(), viaEntry.readStore()));
}

void test_session_undo_four_kind_steps_bounded_entries() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = Config::TICKS_PER_BAR * 128;
  loop.passes.recordPass = makeRecordPassWithNote(5, 10);
  loop.nextPassId_ = 2;

  NoteEditSessionUndoStack stack;
  NoteEditFocus focus;
  CowLoopEventStore session;
  loop.rematerializeEditView(session.mutStore());
  session.discardFlatCache();
  rebuildNoteEditFocusFromStore(focus, session.readFlat(), 5, loop.loopLengthTicks, 0);
  focus.last = focus.commitBaseline;

  const NoteEditKind kinds[] = {NoteEditKind::Move, NoteEditKind::Pitch, NoteEditKind::Length,
                                NoteEditKind::Move};
  size_t totalEntryBytes = 0;
  for (NoteEditKind kind : kinds) {
    const SessionUndoEntry entry = buildSessionUndoEntry(
        focus, NoteEditSelection{}, session.readFlat(), 5, loop.loopLengthTicks);
    totalEntryBytes += estimatedSessionUndoEntryBytes(entry);
    TEST_ASSERT_TRUE(stack.pushEntry(entry));
    if (kind == NoteEditKind::Move) {
      MidiEventVec& flat = session.mutFlat();
      applyMoveToSession(focus, flat, 5, focus.last.startTick + 48);
      session.syncFlatToStore();
    } else if (kind == NoteEditKind::Pitch) {
      focus.last.pitch = 67;
      noteEditFocusApplyPitch(focus, 67, focus.last.startTick, focus.last.endTick,
                              loop.loopLengthTicks);
    } else if (kind == NoteEditKind::Length) {
      focus.last.endTick += 24;
      noteEditFocusApplyLengthEnd(focus, focus.last.endTick);
    }
  }

  TEST_ASSERT_EQUAL(4u, stack.undoCount());
  MidiEventVec materialized;
  loop.passes.materializeToEventVector(materialized, loop.loopLengthTicks);
  const size_t oneCloneBytes = materialized.size() * sizeof(MidiEvent);
  TEST_ASSERT_LESS_THAN(totalEntryBytes, oneCloneBytes * stack.undoCount());
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_session_undo_stack_push_entry);
  RUN_TEST(test_session_undo_entry_matches_clone_restore);
  RUN_TEST(test_session_undo_four_kind_steps_bounded_entries);
  return UNITY_END();
}
