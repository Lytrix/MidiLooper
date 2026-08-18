//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include <chrono>
#include <cstdio>
#include <utility>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/EditManager/EditApply.cpp"
#include "../test_support/MemoryMonitorNativeDeps.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/Loop/LoopPasses.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/Loop.cpp"
#include "../test_support/LoopCaptureTestDeps.cpp"
#include "../test_support/NoteEditFocusTestDeps.cpp"
#include "../../src/EditManager/NoteEditSessionUndoStack.cpp"

#include "EditPass.h"
#include "Globals.h"
#include "Loop.h"
#include "LoopEventStore.h"
#include "EditSession.h"
#include "NoteEditCurrentState.h"
#include "NoteEditSessionState.h"
#include "NoteEditSessionUndo.h"
#include "../test_support/NoteIdTestFixtures.h"
#include "../test_support/CommittedChunkIdTestHelpers.h"
#include "MidiEvent.h"

namespace {

using namespace NoteIdTestFixtures;

struct KindBoundaryUndoState {
  NoteEditKind lastPushed = NoteEditKind::Select;
};

bool pushKindBoundaryUndo(NoteEditSessionUndoStack& stack, KindBoundaryUndoState& state,
                          NoteEditFocus& focus, const EditorSelection& selection,
                          CowLoopEventStore& session, uint8_t channel, uint32_t loopLength,
                          const EditPassIdList& editPassIds, NoteEditKind kind) {
  if (!shouldPushGeometryKindUndo(state.lastPushed, kind)) {
    return false;
  }
  if (session.isEventsDirty()) {
    session.syncEventsToStore();
  }
  const NoteEditCurrentState currentState =
      NoteEditCurrentState::buildFromSessionStore(session.readEvents(), channel);
  const SessionUndoEntry entry =
      buildSessionUndoEntry(focus, selection, session.readEvents(), channel, loopLength,
                            editPassIds, &currentState);
  if (!stack.pushEntry(entry)) {
    return false;
  }
  state.lastPushed = kind;
  return true;
}


RecordPass makeRecordPassWithNote(uint8_t channel, uint32_t startTick) {
  resetNoteIdCounter();
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, startTick, channel, 60, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(startTick + 48, channel, 60, 0)));
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, committedChunkIds));
  RecordPass pass{};
  pass.id = 1;
  pass.state = CapturePassState::Active;
  pass.committedChunkIds = std::move(committedChunkIds);
  return pass;
}

OverdubPass makeOverdubPassWithNote(uint8_t channel, uint32_t startTick, uint8_t pitch,
                                    PassId id) {
  resetNoteIdCounter(2);
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, startTick, channel, pitch, 100, 2));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(startTick + 48, channel, pitch, 0)));
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, committedChunkIds));
  OverdubPass pass{};
  pass.id = id;
  pass.mergeSequence = 1;
  pass.state = CapturePassState::Active;
  pass.committedChunkIds = std::move(committedChunkIds);
  return pass;
}

template <typename Alloc>
void applyMoveToSession(NoteEditFocus& focus, std::vector<MidiEvent, Alloc>& flat, uint8_t channel,
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

template <typename Alloc>
bool hasDisplayNote(const std::vector<MidiEvent, Alloc>& flat, uint32_t loopLength, uint8_t pitch,
                    uint32_t startTick, uint32_t endTick) {
  const NoteUtils::DisplayNoteVec notes =
      NoteUtils::reconstructDisplayNotes(flat, loopLength, false);
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.note == pitch && note.startTick == startTick && note.endTick == endTick) {
      return true;
    }
  }
  return false;
}

}  // namespace

void test_session_undo_stack_push_entry() {
  NoteEditSessionUndoStack stack;
  SessionUndoEntry entry;
  entry.selection.primaryNote = 1;
  TEST_ASSERT_TRUE(stack.pushEntry(entry));
  TEST_ASSERT_TRUE(stack.canUndo());
  const SessionUndoEntry* target = stack.popUndoTarget();
  TEST_ASSERT_NOT_NULL(target);
}

void test_session_three_step_undo_redo_chain() {
  NoteEditSessionUndoStack stack;
  for (int i = 0; i < 3; ++i) {
    SessionUndoEntry entry;
    entry.selection.primaryNote = 1;
    TEST_ASSERT_TRUE(stack.pushEntry(entry));
  }
  TEST_ASSERT_EQUAL(3u, stack.undoCount());
  TEST_ASSERT_EQUAL(0u, stack.redoCount());

  for (int i = 0; i < 3; ++i) {
    SessionUndoEntry* undoTarget = stack.popUndoTarget();
    TEST_ASSERT_NOT_NULL(undoTarget);
  }
  TEST_ASSERT_EQUAL(0u, stack.undoCount());
  TEST_ASSERT_EQUAL(3u, stack.redoCount());

  for (int i = 0; i < 3; ++i) {
    TEST_ASSERT_TRUE(stack.canRedo());
    SessionUndoEntry* redoTarget = stack.peekRedoTarget();
    TEST_ASSERT_NOT_NULL(redoTarget);
    stack.advanceRedoCursor();
  }
  TEST_ASSERT_EQUAL(3u, stack.undoCount());
  TEST_ASSERT_EQUAL(0u, stack.redoCount());

  SessionUndoEntry fresh;
  fresh.selection.primaryNote = 1;
  TEST_ASSERT_TRUE(stack.pushEntry(fresh));
  TEST_ASSERT_EQUAL(4u, stack.undoCount());
  TEST_ASSERT_EQUAL(0u, stack.redoCount());
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
  session.discardEventsCache();

  NoteEditFocus focus;
  rebuildNoteEditFocusFromStore(focus, session.readEvents(), 5, loop.loopLengthTicks, 0);
  focus.last = focus.commitBaseline;

  const auto cloneSnap = session.readStore().cloneShared();
  const EditPassIdList noEditPasses{};
  const NoteEditCurrentState currentState =
      NoteEditCurrentState::buildFromSessionStore(session.readEvents(), 5);
  const SessionUndoEntry entry =
      buildSessionUndoEntry(focus, EditorSelection{}, session.readEvents(), 5, loop.loopLengthTicks,
                            noEditPasses, &currentState);

  MidiEventVec& flat = session.mutEvents();
  applyMoveToSession(focus, flat, 5, 58);
  session.syncEventsToStore();

  CowLoopEventStore viaClone;
  viaClone.restoreFromSnapshot(cloneSnap);

  CowLoopEventStore viaEntry;
  applySessionUndoEntry(loop, viaEntry, entry, loop.loopLengthTicks, 5, noEditPasses);

  TEST_ASSERT_TRUE(sessionUndoStoresMatch(viaClone.readStore(), viaEntry.readStore()));
}

void test_session_redo_entry_restores_after_state() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 768;
  loop.passes.recordPass = makeRecordPassWithNote(5, 10);
  loop.nextPassId_ = 2;

  CowLoopEventStore session;
  loop.rematerializeEditView(session.mutStore());
  session.discardEventsCache();

  NoteEditFocus focus;
  rebuildNoteEditFocusFromStore(focus, session.readEvents(), 5, loop.loopLengthTicks, 0);
  focus.last = focus.commitBaseline;
  const EditPassIdList noEditPasses{};
  const SessionUndoEntry beforeEntry =
      buildSessionUndoEntry(focus, EditorSelection{}, session.readEvents(), 5, loop.loopLengthTicks,
                            noEditPasses);

  MidiEventVec& flat = session.mutEvents();
  applyMoveToSession(focus, flat, 5, 58);
  session.syncEventsToStore();
  const auto movedSnap = session.readStore().cloneShared();

  SessionUndoEntry undoEntry = beforeEntry;
  SessionUndoEntry redoPayload =
      buildSessionUndoEntry(focus, EditorSelection{}, session.readEvents(), 5, loop.loopLengthTicks,
                            noEditPasses);
  undoEntry.redoEditRows = std::move(redoPayload.editRows);
  undoEntry.redoFocus = std::move(redoPayload.focus);
  undoEntry.redoSelection = redoPayload.selection;
  undoEntry.redoEditPassIds = noEditPasses;
  undoEntry.hasRedoPayload = true;

  applySessionUndoEntry(loop, session, undoEntry, loop.loopLengthTicks, 5, noEditPasses);
  CowLoopEventStore baseline;
  loop.rematerializeEditView(baseline.mutStore());
  TEST_ASSERT_TRUE(sessionUndoStoresMatch(baseline.readStore(), session.readStore()));

  applySessionRedoEntry(loop, session, undoEntry, loop.loopLengthTicks, 5, noEditPasses);
  CowLoopEventStore moved;
  moved.restoreFromSnapshot(movedSnap);
  TEST_ASSERT_TRUE(sessionUndoStoresMatch(moved.readStore(), session.readStore()));
}

void test_replace_note_edit_pass_uses_final_session_store() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 768;
  loop.passes.recordPass = makeRecordPassWithNote(5, 10);
  loop.nextPassId_ = 2;

  EditPass staleMove{};
  staleMove.actionType = EditActionType::Update;
  staleMove.propertyType = EditPropertyType::NoteRange;
  staleMove.targetNoteId = 1;
  staleMove.startTick = 58;
  staleMove.endTick = 106;
  const EditPassId staleId = loop.saveNoteEditPass(0, staleMove);
  TEST_ASSERT_EQUAL(2u, staleId);

  MidiEventVec baselineEvents;
  loop.mergeActiveCapturePasses(baselineEvents);
  MidiEventVec finalSessionEvents = baselineEvents;
  for (MidiEvent& evt : finalSessionEvents) {
    if (evt.isNoteOn() && evt.channel == 5 && evt.data.noteData.note == 60 && evt.tick == 10) {
      evt.tick = 106;
    }
    if (evt.isNoteOff() && evt.channel == 5 && evt.data.noteData.note == 60 && evt.tick == 58) {
      evt.tick = 154;
    }
  }

  EditPassVec replacement =
      buildSessionStoreEditPasses(baselineEvents, finalSessionEvents, 5, loop.loopLengthTicks);
  TEST_ASSERT_FALSE(replacement.empty());
  const EditPassIdList replacementIds =
      loop.replaceNoteEditPass(0, EditPassIdList{staleId}, std::move(replacement));
  const EditPassId replacementId = replacementIds.empty() ? kInvalidEditPassId : replacementIds.front();
  TEST_ASSERT_EQUAL(3u, replacementId);

  MidiEventVec materialized;
  loop.passes.materializeToEventVector(materialized, loop.loopLengthTicks);
  TEST_ASSERT_TRUE(hasDisplayNote(materialized, loop.loopLengthTicks, 60, 106, 154));
  TEST_ASSERT_FALSE(hasDisplayNote(materialized, loop.loopLengthTicks, 60, 58, 106));
}

void test_replace_note_edit_pass_disables_stale_rows_when_final_store_matches_baseline() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 768;
  loop.passes.recordPass = makeRecordPassWithNote(5, 10);
  loop.nextPassId_ = 2;

  EditPass staleMove{};
  staleMove.actionType = EditActionType::Update;
  staleMove.propertyType = EditPropertyType::NoteRange;
  staleMove.targetNoteId = 1;
  staleMove.startTick = 58;
  staleMove.endTick = 106;
  const EditPassId staleId = loop.saveNoteEditPass(0, staleMove);
  TEST_ASSERT_EQUAL(2u, staleId);

  MidiEventVec baselineEvents;
  loop.mergeActiveCapturePasses(baselineEvents);
  const EditPassVec replacement =
      buildSessionStoreEditPasses(baselineEvents, baselineEvents, 5, loop.loopLengthTicks);
  TEST_ASSERT_TRUE(replacement.empty());
  const EditPassIdList replacementIds =
      loop.replaceNoteEditPass(0, EditPassIdList{staleId}, EditPassVec{});
  const EditPassId replacementId = replacementIds.empty() ? kInvalidEditPassId : replacementIds.front();
  TEST_ASSERT_EQUAL(kInvalidEditPassId, replacementId);

  MidiEventVec materialized;
  loop.passes.materializeToEventVector(materialized, loop.loopLengthTicks);
  TEST_ASSERT_TRUE(hasDisplayNote(materialized, loop.loopLengthTicks, 60, 10, 58));
  TEST_ASSERT_FALSE(hasDisplayNote(materialized, loop.loopLengthTicks, 60, 58, 106));
}

void test_visual_cache_reflects_active_edit_passes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 768;
  loop.passes.recordPass = makeRecordPassWithNote(5, 10);
  loop.nextPassId_ = 2;

  EditPass move{};
  move.actionType = EditActionType::Update;
  move.propertyType = EditPropertyType::NoteRange;
  move.targetNoteId = 1;
  move.startTick = 106;
  move.endTick = 154;
  const EditPassId editPassId = loop.saveNoteEditPass(0, move);
  TEST_ASSERT_EQUAL(2u, editPassId);

  loop.rebuildVisualCacheFromPasses();
  TEST_ASSERT_FALSE(loop.visualCache.notes.empty());
  TEST_ASSERT_TRUE(
      hasDisplayNote(loop.midiEvents(), loop.loopLengthTicks, 60, 106, 154));
  TEST_ASSERT_FALSE(
      hasDisplayNote(loop.midiEvents(), loop.loopLengthTicks, 60, 10, 58));

  bool foundMovedInVisualCache = false;
  for (const NoteUtils::DisplayNote& note : loop.visualCache.notes) {
    if (note.note == 60 && note.startTick == 106 && note.endTick == 154) {
      foundMovedInVisualCache = true;
      break;
    }
  }
  TEST_ASSERT_TRUE(foundMovedInVisualCache);
}

void test_session_undo_move_after_add_committed_restores_insert_position() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 768;
  loop.passes.recordPass = makeRecordPassWithNote(5, 10);
  loop.nextPassId_ = 2;

  EditPass add{};
  add.actionType = EditActionType::Create;
  add.addedEvents.push_back(noteOnWithNoteId(48, 5, 72, 100, 2));
  add.addedEvents.push_back(MidiEvent::NoteOff(96, 5, 72, 0));
  const EditPassId addId = loop.saveNoteEditPass(0, add);
  TEST_ASSERT_EQUAL(2u, addId);

  CowLoopEventStore session;
  loop.passes.materializeToEventVector(session.mutEvents(), loop.loopLengthTicks);
  session.syncEventsToStore();

  NoteEditFocus focus;
  focus.active = true;
  focus.commitBaseline = {72, 100, 48, 96};
  focus.last = focus.commitBaseline;
  focus.movingNoteId = 2;
  focus.movingNoteRange = {48, 96};

  const EditPassIdList idsAtPush{addId};
  const SessionUndoEntry entry =
      buildSessionUndoEntry(focus, EditorSelection{}, session.readEvents(), 5, loop.loopLengthTicks,
                            idsAtPush);

  EditPass move{};
  move.actionType = EditActionType::Update;
  move.propertyType = EditPropertyType::NoteRange;
  move.targetNoteId = 2;
  move.startTick = 106;
  move.endTick = 154;
  const EditPassId moveId = loop.saveNoteEditPass(0, move);
  TEST_ASSERT_EQUAL(3u, moveId);

  loop.passes.materializeToEventVector(session.mutEvents(), loop.loopLengthTicks);
  session.syncEventsToStore();
  TEST_ASSERT_TRUE(hasDisplayNote(session.readEvents(), loop.loopLengthTicks, 72, 106, 154));

  const EditPassIdList currentIds{addId, moveId};
  applySessionUndoEntry(loop, session, entry, loop.loopLengthTicks, 5, currentIds);
  TEST_ASSERT_TRUE(hasDisplayNote(session.readEvents(), loop.loopLengthTicks, 72, 48, 96));
  TEST_ASSERT_FALSE(hasDisplayNote(session.readEvents(), loop.loopLengthTicks, 72, 106, 154));
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
  session.discardEventsCache();
  rebuildNoteEditFocusFromStore(focus, session.readEvents(), 5, loop.loopLengthTicks, 0);
  focus.last = focus.commitBaseline;

  const NoteEditKind kinds[] = {NoteEditKind::Move, NoteEditKind::Pitch, NoteEditKind::Length,
                                NoteEditKind::Move};
  size_t totalEntryBytes = 0;
  for (NoteEditKind kind : kinds) {
    const SessionUndoEntry entry = buildSessionUndoEntry(
        focus, EditorSelection{}, session.readEvents(), 5, loop.loopLengthTicks, EditPassIdList{});
    totalEntryBytes += estimatedSessionUndoEntryBytes(entry);
    TEST_ASSERT_TRUE(stack.pushEntry(entry));
    if (kind == NoteEditKind::Move) {
      MidiEventVec& flat = session.mutEvents();
      applyMoveToSession(focus, flat, 5, focus.last.startTick + 48);
      session.syncEventsToStore();
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

void test_session_undo_live_capture_during_note_edit() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 768;
  loop.passes.recordPass = makeRecordPassWithNote(5, 10);
  loop.nextPassId_ = 2;

  CowLoopEventStore session;
  loop.rematerializeEditView(session.mutStore());
  session.discardEventsCache();
  TEST_ASSERT_TRUE(hasDisplayNote(session.readEvents(), loop.loopLengthTicks, 60, 10, 58));

  const MidiEventVec baselineEvents = session.readEvents();
  MidiEventVec& flat = session.mutEvents();
  flat.push_back(MidiEvent::NoteOn(100, 5, 72, 100));
  flat.push_back(MidiEvent::NoteOff(148, 5, 72, 0));
  session.syncEventsToStore();
  TEST_ASSERT_TRUE(hasDisplayNote(session.readEvents(), loop.loopLengthTicks, 72, 100, 148));

  SessionUndoEntry entry;
  entry.redoEditRows =
      buildSessionStoreEditPasses(baselineEvents, session.readEvents(), 5, loop.loopLengthTicks);
  entry.hasRedoPayload = true;
  TEST_ASSERT_FALSE(entry.redoEditRows.empty());

  applySessionUndoEntry(loop, session, entry, loop.loopLengthTicks, 5, EditPassIdList{});
  TEST_ASSERT_TRUE(hasDisplayNote(session.readEvents(), loop.loopLengthTicks, 60, 10, 58));
  TEST_ASSERT_FALSE(hasDisplayNote(session.readEvents(), loop.loopLengthTicks, 72, 100, 148));

  applySessionRedoEntry(loop, session, entry, loop.loopLengthTicks, 5, EditPassIdList{});
  TEST_ASSERT_TRUE(hasDisplayNote(session.readEvents(), loop.loopLengthTicks, 72, 100, 148));
}

void test_session_live_capture_survives_pass_replay() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 768;
  loop.passes.recordPass = makeRecordPassWithNote(5, 10);
  loop.nextPassId_ = 2;

  CowLoopEventStore session;
  loop.rematerializeEditView(session.mutStore());
  session.discardEventsCache();
  MidiEventVec& flat = session.mutEvents();
  flat.push_back(MidiEvent::NoteOn(100, 5, 72, 100));
  flat.push_back(MidiEvent::NoteOff(148, 5, 72, 0));
  session.syncEventsToStore();
  const MidiEventVec sessionSnapshot = session.readEvents();

  MidiEventVec loopMidiEventsFromPasses;
  loop.passes.materializeToEventVector(loopMidiEventsFromPasses, loop.loopLengthTicks);
  const EditPassVec sessionOverlay =
      buildSessionStoreEditPasses(loopMidiEventsFromPasses, sessionSnapshot, 5,
                                  loop.loopLengthTicks);
  TEST_ASSERT_FALSE(sessionOverlay.empty());
  applyNoteEditPassSequence(loopMidiEventsFromPasses, sessionOverlay, loop.loopLengthTicks);

  TEST_ASSERT_TRUE(hasDisplayNote(loopMidiEventsFromPasses, loop.loopLengthTicks, 60, 10, 58));
  TEST_ASSERT_TRUE(hasDisplayNote(loopMidiEventsFromPasses, loop.loopLengthTicks, 72, 100, 148));
}

void test_kind_boundary_move_twice_one_undo_entry() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 768;
  loop.passes.recordPass = makeRecordPassWithNote(5, 10);
  loop.nextPassId_ = 2;

  NoteEditSessionUndoStack stack;
  KindBoundaryUndoState state;
  CowLoopEventStore session;
  loop.rematerializeEditView(session.mutStore());
  session.discardEventsCache();
  NoteEditFocus focus;
  rebuildNoteEditFocusFromStore(focus, session.readEvents(), 5, loop.loopLengthTicks, 0);
  focus.last = focus.commitBaseline;

  TEST_ASSERT_TRUE(
      pushKindBoundaryUndo(stack, state, focus, EditorSelection{}, session, 5,
                           loop.loopLengthTicks, EditPassIdList{}, NoteEditKind::Move));
  MidiEventVec& flat = session.mutEvents();
  applyMoveToSession(focus, flat, 5, 58);
  session.syncEventsToStore();

  TEST_ASSERT_FALSE(
      pushKindBoundaryUndo(stack, state, focus, EditorSelection{}, session, 5,
                           loop.loopLengthTicks, EditPassIdList{}, NoteEditKind::Move));
  TEST_ASSERT_EQUAL(1u, stack.undoCount());
}

void test_kind_boundary_pitch_with_active_focus_one_undo_entry() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 768;
  loop.passes.recordPass = makeRecordPassWithNote(5, 10);
  loop.nextPassId_ = 2;

  NoteEditSessionUndoStack stack;
  KindBoundaryUndoState state;
  CowLoopEventStore session;
  loop.rematerializeEditView(session.mutStore());
  session.discardEventsCache();
  NoteEditFocus focus;
  rebuildNoteEditFocusFromStore(focus, session.readEvents(), 5, loop.loopLengthTicks, 0);
  focus.last = focus.commitBaseline;
  TEST_ASSERT_TRUE(focus.active);

  TEST_ASSERT_TRUE(
      pushKindBoundaryUndo(stack, state, focus, EditorSelection{}, session, 5,
                           loop.loopLengthTicks, EditPassIdList{}, NoteEditKind::Pitch));
  noteEditFocusApplyPitch(focus, 67, focus.last.startTick, focus.last.endTick,
                            loop.loopLengthTicks);

  TEST_ASSERT_FALSE(
      pushKindBoundaryUndo(stack, state, focus, EditorSelection{}, session, 5,
                           loop.loopLengthTicks, EditPassIdList{}, NoteEditKind::Pitch));
  TEST_ASSERT_EQUAL(1u, stack.undoCount());
}

void test_kind_boundary_add_then_move_two_entries() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 768;
  loop.passes.recordPass = makeRecordPassWithNote(5, 10);
  loop.nextPassId_ = 2;

  NoteEditSessionUndoStack stack;
  KindBoundaryUndoState state;
  CowLoopEventStore session;
  loop.rematerializeEditView(session.mutStore());
  session.discardEventsCache();
  NoteEditFocus focus;
  rebuildNoteEditFocusFromStore(focus, session.readEvents(), 5, loop.loopLengthTicks, 0);
  focus.last = focus.commitBaseline;

  TEST_ASSERT_TRUE(
      pushKindBoundaryUndo(stack, state, focus, EditorSelection{}, session, 5,
                           loop.loopLengthTicks, EditPassIdList{}, NoteEditKind::Add));
  TEST_ASSERT_TRUE(
      pushKindBoundaryUndo(stack, state, focus, EditorSelection{}, session, 5,
                           loop.loopLengthTicks, EditPassIdList{}, NoteEditKind::Move));
  TEST_ASSERT_EQUAL(2u, stack.undoCount());
}

void test_kind_boundary_reselect_move_pushes_again() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 768;
  loop.passes.recordPass = makeRecordPassWithNote(5, 10);
  loop.nextPassId_ = 2;

  NoteEditSessionUndoStack stack;
  KindBoundaryUndoState state;
  CowLoopEventStore session;
  loop.rematerializeEditView(session.mutStore());
  session.discardEventsCache();
  NoteEditFocus focus;
  rebuildNoteEditFocusFromStore(focus, session.readEvents(), 5, loop.loopLengthTicks, 0);
  focus.last = focus.commitBaseline;
  EditorSelection selection{};

  TEST_ASSERT_TRUE(pushKindBoundaryUndo(stack, state, focus, selection, session, 5,
                                        loop.loopLengthTicks, EditPassIdList{},
                                        NoteEditKind::Move));
  applyMoveToSession(focus, session.mutEvents(), 5, 58);
  session.syncEventsToStore();

  EditorSelection priorSelection = selection;
  priorSelection.primaryNote = focus.movingNoteId;
  selection.primaryNote = 2;
  focus.movingNoteId = 2;
  if (shouldResetGeometryKindUndoOnSelectChange(priorSelection, selection.primaryNote)) {
    state.lastPushed = NoteEditKind::Select;
  }

  TEST_ASSERT_TRUE(pushKindBoundaryUndo(stack, state, focus, selection, session, 5,
                                        loop.loopLengthTicks, EditPassIdList{},
                                        NoteEditKind::Move));
  TEST_ASSERT_EQUAL(2u, stack.undoCount());
}

void test_kind_boundary_select_nav_no_push() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 768;
  loop.passes.recordPass = makeRecordPassWithNote(5, 10);
  loop.nextPassId_ = 2;

  NoteEditSessionUndoStack stack;
  KindBoundaryUndoState state;
  CowLoopEventStore session;
  loop.rematerializeEditView(session.mutStore());
  session.discardEventsCache();
  NoteEditFocus focus;
  rebuildNoteEditFocusFromStore(focus, session.readEvents(), 5, loop.loopLengthTicks, 0);
  focus.last = focus.commitBaseline;

  TEST_ASSERT_FALSE(
      pushKindBoundaryUndo(stack, state, focus, EditorSelection{}, session, 5,
                           loop.loopLengthTicks, EditPassIdList{}, NoteEditKind::Select));
  TEST_ASSERT_EQUAL(0u, stack.undoCount());
}

void test_overdub_survives_exit_bake_after_note_range_commit() {
  // session_20260814_014553: NoteRange commit then exit bake replaced 1 row with 19 and
  // dropped overdub (DISP 48→32). Session rematerialized from full passes, then one move.
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 1536;
  loop.passes.recordPass = makeRecordPassWithNote(5, 10);
  loop.passes.overdubPasses.push_back(makeOverdubPassWithNote(5, 240, 91, 2));
  loop.nextPassId_ = 3;

  CowLoopEventStore session;
  loop.rematerializeEditView(session.mutStore());
  session.discardEventsCache();
  TEST_ASSERT_TRUE(hasDisplayNote(session.readEvents(), loop.loopLengthTicks, 60, 10, 58));
  TEST_ASSERT_TRUE(hasDisplayNote(session.readEvents(), loop.loopLengthTicks, 91, 240, 288));

  MidiEventVec& flat = session.mutEvents();
  for (MidiEvent& evt : flat) {
    if (evt.isNoteOn() && evt.data.noteData.note == 60 && evt.tick == 10) {
      evt.tick = 58;
    }
    if (evt.isNoteOff() && evt.data.noteData.note == 60 && evt.tick == 58) {
      evt.tick = 106;
    }
  }
  session.syncEventsToStore();

  EditPass move{};
  move.actionType = EditActionType::Update;
  move.propertyType = EditPropertyType::NoteRange;
  move.targetNoteId = 1;
  move.startTick = 58;
  move.endTick = 106;
  const EditPassId staleId = loop.saveNoteEditPass(0, move);
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, staleId);

  MidiEventVec baselineExcludingSessionEdits;
  loop.materializeExcludingEditPassIds(EditPassIdList{staleId}, baselineExcludingSessionEdits);
  EditPassVec replacement = buildSessionStoreEditPasses(
      baselineExcludingSessionEdits, session.readEvents(), 5, loop.loopLengthTicks);
  const EditPassIdList replacementIds =
      loop.replaceNoteEditPass(0, EditPassIdList{staleId}, std::move(replacement));
  TEST_ASSERT_FALSE(replacementIds.empty());

  MidiEventVec materialized;
  loop.passes.materializeToEventVector(materialized, loop.loopLengthTicks);
  TEST_ASSERT_TRUE(hasDisplayNote(materialized, loop.loopLengthTicks, 60, 58, 106));
  TEST_ASSERT_TRUE(hasDisplayNote(materialized, loop.loopLengthTicks, 91, 240, 288));
}

void test_stale_record_only_session_bake_must_not_delete_overdub() {
  // 014553 destroy path: session store is record-only; bake vs full baseline emits Deletes.
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 1536;
  loop.passes.recordPass = makeRecordPassWithNote(5, 10);
  loop.passes.overdubPasses.push_back(makeOverdubPassWithNote(5, 240, 91, 2));
  loop.nextPassId_ = 3;

  MidiEventVec fullBaseline;
  loop.passes.materializeToEventVector(fullBaseline, loop.loopLengthTicks);
  MidiEventVec recordOnlySession;
  RecordPass recordOnly = makeRecordPassWithNote(5, 10);
  LoopPasses recordPasses;
  recordPasses.recordPass = std::move(recordOnly);
  recordPasses.materializeToEventVector(recordOnlySession, loop.loopLengthTicks);

  EditPassVec bakeRows =
      buildSessionStoreEditPasses(fullBaseline, recordOnlySession, 5, loop.loopLengthTicks);
  const NoteEditCurrentState currentState =
      NoteEditCurrentState::buildFromSessionStore(recordOnlySession, 5);
  dropUnrequestedSessionStoreDeletes(bakeRows, currentState);
  size_t deleteRows = 0;
  for (const EditPass& row : bakeRows) {
    if (row.actionType == EditActionType::Delete) {
      ++deleteRows;
    }
  }
  TEST_ASSERT_EQUAL(0u, deleteRows);
  TEST_ASSERT_TRUE(hasDisplayNote(fullBaseline, loop.loopLengthTicks, 91, 240, 288));
}

void test_live_capture_baked_on_close_without_prior_edit_passes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 768;
  loop.passes.recordPass = makeRecordPassWithNote(5, 10);
  loop.passes.overdubPasses.push_back(makeOverdubPassWithNote(5, 100, 24, 2));
  loop.nextPassId_ = 3;

  MidiEventVec baselineEvents;
  loop.passes.materializeToEventVector(baselineEvents, loop.loopLengthTicks);
  MidiEventVec sessionEvents = baselineEvents;
  sessionEvents.push_back(MidiEvent::NoteOn(200, 5, 12, 100));
  sessionEvents.push_back(MidiEvent::NoteOff(248, 5, 12, 0));

  const EditPassVec bakeRows =
      buildSessionStoreEditPasses(baselineEvents, sessionEvents, 5, loop.loopLengthTicks);
  TEST_ASSERT_FALSE(bakeRows.empty());
  for (const EditPass& row : bakeRows) {
    EditPass copy = row;
    const EditPassId id = loop.saveNoteEditPass(0, std::move(copy));
    TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, id);
  }

  MidiEventVec materialized;
  loop.passes.materializeToEventVector(materialized, loop.loopLengthTicks);
  TEST_ASSERT_TRUE(hasDisplayNote(materialized, loop.loopLengthTicks, 12, 200, 248));
}

void test_session_undo_entry_trims_baseline_map_to_overlap_closure() {
  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = 1;
  focus.commitBaseline = {36, 100, 48, 96};
  focus.last = focus.commitBaseline;
  for (NoteId noteId = 1; noteId <= 40; ++noteId) {
    focus.baselineMap[noteId] = {36, 100, 48, 96};
  }
  OverlapNote overlap;
  overlap.noteId = 17;
  overlap.baseline = {40, 100, 96, 144};
  focus.overlapNotes[17] = overlap;

  const NoteEditFocus snap = snapshotFocusForSessionUndo(focus);
  TEST_ASSERT_EQUAL(2u, snap.baselineMap.size());
  TEST_ASSERT_TRUE(snap.baselineMap.count(1) > 0);
  TEST_ASSERT_TRUE(snap.baselineMap.count(17) > 0);
  TEST_ASSERT_EQUAL(40u, focus.baselineMap.size());
}

void test_undo_warm_143144_focus_snap_copies_full_baseline_then_trims() {
  constexpr uint8_t kCh = 5;
  constexpr uint32_t kLoopLen = 3072;
  constexpr size_t kNotes = 110;
  constexpr size_t kExtraCc = 9;

  MidiEventVec sessionFlat;
  sessionFlat.reserve(kNotes * 2 + kExtraCc);
  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = 1;
  focus.commitBaseline = {60, 100, 10, 58};
  focus.last = focus.commitBaseline;
  for (size_t i = 0; i < kNotes; ++i) {
    const NoteId id = static_cast<NoteId>(i + 1);
    const uint32_t start = static_cast<uint32_t>(10 + i * 24);
    const uint32_t end = start + 48;
    const uint8_t pitch = static_cast<uint8_t>(24 + (i % 48));
    MidiEvent on = MidiEvent::NoteOn(start, kCh, pitch, 100);
    on.noteId = id;
    MidiEvent off = MidiEvent::NoteOff(end, kCh, pitch, 0);
    off.noteId = id;
    sessionFlat.push_back(on);
    sessionFlat.push_back(off);
    focus.baselineMap[id] = {pitch, 100, start, end};
  }
  for (size_t i = 0; i < kExtraCc; ++i) {
    sessionFlat.push_back(MidiEvent::ControlChange(static_cast<uint32_t>(i), kCh, 1, 0));
  }
  TEST_ASSERT_EQUAL(229u, sessionFlat.size());
  TEST_ASSERT_EQUAL(110u, focus.baselineMap.size());

  const NoteEditCurrentState currentState =
      NoteEditCurrentState::buildFromSessionStore(sessionFlat, kCh);
  TEST_ASSERT_EQUAL(110u, currentState.size());

  const auto nowUs = []() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  };

  const int64_t copyStart = nowUs();
  const NoteEditFocus focusCopy = focus;
  const int64_t copyUs = nowUs() - copyStart;
  TEST_ASSERT_EQUAL(110u, focusCopy.baselineMap.size());

  const int64_t snapStart = nowUs();
  const NoteEditFocus snap = snapshotFocusForSessionUndo(focus);
  const int64_t snapUs = nowUs() - snapStart;
  TEST_ASSERT_EQUAL(1u, snap.baselineMap.size());
  TEST_ASSERT_TRUE(snap.baselineMap.count(1) > 0);
  TEST_ASSERT_EQUAL(110u, focus.baselineMap.size());

  const int64_t cloneStart = nowUs();
  const NoteEditCurrentState cloned = currentState.clone();
  const int64_t cloneUs = nowUs() - cloneStart;
  TEST_ASSERT_EQUAL(110u, cloned.size());

  const int64_t buildStart = nowUs();
  const SessionUndoEntry entry =
      buildSessionUndoEntry(focus, EditorSelection{}, sessionFlat, kCh, kLoopLen,
                            EditPassIdList{}, &currentState);
  const int64_t buildUs = nowUs() - buildStart;
  TEST_ASSERT_TRUE(entry.hasUndoCurrentState);
  TEST_ASSERT_EQUAL(1u, entry.focus.baselineMap.size());
  TEST_ASSERT_EQUAL(110u, entry.undoCurrentState.size());
  TEST_ASSERT_EQUAL(0u, entry.editRows.size());
  TEST_ASSERT_EQUAL(110u, focus.baselineMap.size());
  TEST_ASSERT_EQUAL(110u, currentState.size());

  std::printf("143144 UNDO_WARM host pin us: copy=%lld snap=%lld clone=%lld build=%lld\n",
              static_cast<long long>(copyUs), static_cast<long long>(snapUs),
              static_cast<long long>(cloneUs), static_cast<long long>(buildUs));
}

void test_current_state_undo_restore_parity_with_clone() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 768;
  loop.passes.recordPass = makeRecordPassWithNote(5, 10);
  loop.nextPassId_ = 2;

  CowLoopEventStore session;
  loop.rematerializeEditView(session.mutStore());
  session.discardEventsCache();

  NoteEditFocus focus;
  rebuildNoteEditFocusFromStore(focus, session.readEvents(), 5, loop.loopLengthTicks, 0);
  focus.last = focus.commitBaseline;

  NoteEditCurrentState currentState =
      NoteEditCurrentState::buildFromSessionStore(session.readEvents(), 5);
  const auto cloneSnap = session.readStore().cloneShared();
  const SessionUndoEntry entry =
      buildSessionUndoEntry(focus, EditorSelection{}, session.readEvents(), 5, loop.loopLengthTicks,
                            EditPassIdList{}, &currentState);
  TEST_ASSERT_TRUE(entry.hasUndoCurrentState);

  NoteEditCurrentNoteState* row = currentState.find(focus.movingNoteId);
  TEST_ASSERT_NOT_NULL(row);
  row->currentSpan.startTick = 58;
  row->currentSpan.endTick = 106;
  currentState.projectToSessionStore(session.mutEvents(), 5);
  session.syncEventsToStore();

  CowLoopEventStore viaClone;
  viaClone.restoreFromSnapshot(cloneSnap);

  CowLoopEventStore viaCurrentState;
  applySessionUndoEntry(loop, viaCurrentState, entry, loop.loopLengthTicks, 5, EditPassIdList{});
  TEST_ASSERT_TRUE(sessionUndoStoresMatch(viaClone.readStore(), viaCurrentState.readStore()));
}

void test_current_state_live_capture_undo_redo() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 768;
  loop.passes.recordPass = makeRecordPassWithNote(5, 10);
  loop.nextPassId_ = 2;

  CowLoopEventStore session;
  loop.rematerializeEditView(session.mutStore());
  session.discardEventsCache();

  NoteEditCurrentState currentState =
      NoteEditCurrentState::buildFromSessionStore(session.readEvents(), 5);
  const NoteEditCurrentState beforeFold = currentState.clone();
  currentState.projectToSessionStore(session.mutEvents(), 5);
  session.syncEventsToStore();

  MidiEventVec captureFlat;
  MidiEvent captureOn = MidiEvent::NoteOn(100, 5, 72, 100);
  captureOn.noteId = 3;
  captureFlat.push_back(captureOn);
  MidiEvent captureOff = MidiEvent::NoteOff(148, 5, 72, 0);
  captureOff.noteId = 3;
  captureFlat.push_back(captureOff);
  currentState.mergeCaptureNotesAsAdded(captureFlat, 5, loop.loopLengthTicks);
  currentState.projectToSessionStore(session.mutEvents(), 5);
  session.syncEventsToStore();
  TEST_ASSERT_TRUE(hasDisplayNote(session.readEvents(), loop.loopLengthTicks, 72, 100, 148));

  SessionUndoEntry entry;
  entry.undoCurrentState = beforeFold;
  entry.hasUndoCurrentState = true;
  entry.redoCurrentState = currentState.clone();
  entry.hasRedoCurrentState = true;
  entry.hasRedoPayload = true;

  applySessionUndoEntry(loop, session, entry, loop.loopLengthTicks, 5, EditPassIdList{});
  TEST_ASSERT_TRUE(hasDisplayNote(session.readEvents(), loop.loopLengthTicks, 60, 10, 58));
  TEST_ASSERT_FALSE(hasDisplayNote(session.readEvents(), loop.loopLengthTicks, 72, 100, 148));

  applySessionRedoEntry(loop, session, entry, loop.loopLengthTicks, 5, EditPassIdList{});
  TEST_ASSERT_TRUE(hasDisplayNote(session.readEvents(), loop.loopLengthTicks, 72, 100, 148));
}

void test_current_state_move_ab_undo_redo_chain() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  constexpr NoteId kNoteA = 1;
  constexpr NoteId kNoteB = 2;
  constexpr uint8_t kPitch = 88;
  constexpr uint8_t kCh = 5;
  constexpr uint32_t kLoopLen = 768;

  MidiEventVec storeFlat;
  MidiEvent onA = MidiEvent::NoteOn(100, kCh, kPitch, 100);
  onA.noteId = kNoteA;
  storeFlat.push_back(onA);
  MidiEvent offA = MidiEvent::NoteOff(148, kCh, kPitch, 0);
  offA.noteId = kNoteA;
  storeFlat.push_back(offA);
  MidiEvent onB = MidiEvent::NoteOn(200, kCh, kPitch, 100);
  onB.noteId = kNoteB;
  storeFlat.push_back(onB);
  MidiEvent offB = MidiEvent::NoteOff(248, kCh, kPitch, 0);
  offB.noteId = kNoteB;
  storeFlat.push_back(offB);

  Loop loop;
  loop.loopLengthTicks = kLoopLen;

  CowLoopEventStore session;
  session.mutEvents() = storeFlat;
  session.syncEventsToStore();

  NoteEditCurrentState currentState =
      NoteEditCurrentState::buildFromSessionStore(session.readEvents(), kCh);

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kNoteA;
  focus.commitBaseline = {kPitch, 100, 100, 148};
  focus.last = focus.commitBaseline;

  SessionUndoEntry entryA =
      buildSessionUndoEntry(focus, EditorSelection{}, session.readEvents(), kCh, kLoopLen,
                            EditPassIdList{}, &currentState);

  NoteEditCurrentNoteState* rowA = currentState.find(kNoteA);
  TEST_ASSERT_NOT_NULL(rowA);
  rowA->currentSpan.startTick = 300;
  rowA->currentSpan.endTick = 348;
  currentState.projectToSessionStore(session.mutEvents(), kCh);
  session.syncEventsToStore();

  focus.movingNoteId = kNoteB;
  focus.commitBaseline = {kPitch, 100, 200, 248};
  focus.last = focus.commitBaseline;
  SessionUndoEntry entryB =
      buildSessionUndoEntry(focus, EditorSelection{}, session.readEvents(), kCh, kLoopLen,
                            EditPassIdList{}, &currentState);

  NoteEditCurrentNoteState* rowB = currentState.find(kNoteB);
  TEST_ASSERT_NOT_NULL(rowB);
  rowB->currentSpan.startTick = 50;
  rowB->currentSpan.endTick = 98;
  currentState.projectToSessionStore(session.mutEvents(), kCh);
  session.syncEventsToStore();

  rowA->currentSpan.startTick = 100;
  rowA->currentSpan.endTick = 148;
  currentState.projectToSessionStore(session.mutEvents(), kCh);
  session.syncEventsToStore();

  SessionUndoEntry entryA2 =
      buildSessionUndoEntry(focus, EditorSelection{}, session.readEvents(), kCh, kLoopLen,
                            EditPassIdList{}, &currentState);

  rowB->currentSpan.startTick = 200;
  rowB->currentSpan.endTick = 248;
  currentState.projectToSessionStore(session.mutEvents(), kCh);
  session.syncEventsToStore();

  focus.movingNoteId = kNoteB;
  SessionUndoEntry entryB2 =
      buildSessionUndoEntry(focus, EditorSelection{}, session.readEvents(), kCh, kLoopLen,
                            EditPassIdList{}, &currentState);

  rowB->currentSpan.startTick = 60;
  rowB->currentSpan.endTick = 108;
  currentState.projectToSessionStore(session.mutEvents(), kCh);
  session.syncEventsToStore();

  applySessionUndoEntry(loop, session, entryB2, kLoopLen, kCh, EditPassIdList{});
  TEST_ASSERT_TRUE(hasDisplayNote(session.readEvents(), kLoopLen, kPitch, 200, 248));

  entryB2.redoCurrentState = currentState.clone();
  entryB2.hasRedoCurrentState = true;
  entryB2.hasRedoPayload = true;
  applySessionRedoEntry(loop, session, entryB2, kLoopLen, kCh, EditPassIdList{});
  TEST_ASSERT_TRUE(hasDisplayNote(session.readEvents(), kLoopLen, kPitch, 60, 108));

  applySessionUndoEntry(loop, session, entryA2, kLoopLen, kCh, EditPassIdList{});
  TEST_ASSERT_TRUE(hasDisplayNote(session.readEvents(), kLoopLen, kPitch, 100, 148));
  TEST_ASSERT_TRUE(hasDisplayNote(session.readEvents(), kLoopLen, kPitch, 50, 98));
  (void)entryA;
  (void)entryB;
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_session_undo_stack_push_entry);
  RUN_TEST(test_session_three_step_undo_redo_chain);
  RUN_TEST(test_session_undo_entry_matches_clone_restore);
  RUN_TEST(test_session_redo_entry_restores_after_state);
  RUN_TEST(test_replace_note_edit_pass_uses_final_session_store);
  RUN_TEST(test_replace_note_edit_pass_disables_stale_rows_when_final_store_matches_baseline);
  RUN_TEST(test_visual_cache_reflects_active_edit_passes);
  RUN_TEST(test_session_undo_move_after_add_committed_restores_insert_position);
  RUN_TEST(test_session_undo_four_kind_steps_bounded_entries);
  RUN_TEST(test_session_undo_live_capture_during_note_edit);
  RUN_TEST(test_session_live_capture_survives_pass_replay);
  RUN_TEST(test_kind_boundary_move_twice_one_undo_entry);
  RUN_TEST(test_kind_boundary_pitch_with_active_focus_one_undo_entry);
  RUN_TEST(test_kind_boundary_add_then_move_two_entries);
  RUN_TEST(test_kind_boundary_reselect_move_pushes_again);
  RUN_TEST(test_kind_boundary_select_nav_no_push);
  RUN_TEST(test_session_undo_entry_trims_baseline_map_to_overlap_closure);
  RUN_TEST(test_undo_warm_143144_focus_snap_copies_full_baseline_then_trims);
  RUN_TEST(test_overdub_survives_exit_bake_after_note_range_commit);
  RUN_TEST(test_stale_record_only_session_bake_must_not_delete_overdub);
  RUN_TEST(test_live_capture_baked_on_close_without_prior_edit_passes);
  RUN_TEST(test_current_state_undo_restore_parity_with_clone);
  RUN_TEST(test_current_state_live_capture_undo_redo);
  RUN_TEST(test_current_state_move_ab_undo_redo_chain);
  return UNITY_END();
}
