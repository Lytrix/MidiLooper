using UndoClearResult = NoteEditSessionUndoStack::ClearResult;

static SessionUndoEntry makeNestedClearEntry() {
  SessionUndoEntry entry;
  for (NoteId id = 1; id <= 4; ++id) {
    NoteBaseline span{60, 100, id * 100, id * 100 + 48};
    entry.focus.baselineMap[id] = entry.redoFocus.baselineMap[id] = span;
    OverlapNote overlap{}; overlap.noteId = id; overlap.baseline = span;
    entry.focus.overlapNotes[id] = entry.redoFocus.overlapNotes[id] = overlap;
    entry.undoCurrentState.upsertRow(id, span, span, NoteEditPresenceType::Visible);
    entry.redoCurrentState.upsertRow(id, span, span, NoteEditPresenceType::Visible);
    entry.selection.selectedNotes.push_back(id);
    entry.redoSelection.selectedNotes.push_back(id);
    entry.editPassIdsAtPush.push_back(id);
    entry.redoEditPassIds.push_back(id);
  }
  for (unsigned row = 0; row < 2; ++row) {
    EditPass edit{}; edit.targetNoteId = row + 1;
    for (unsigned value = 0; value < 3; ++value)
      edit.addedEvents.push_back(MidiEvent::ControlChange(value, 1, 7, 64));
    entry.editRows.push_back(edit);
    entry.redoEditRows.push_back(std::move(edit));
  }
  entry.hasUndoCurrentState = entry.hasRedoCurrentState = entry.hasRedoPayload = true;
  return entry;
}

static size_t drainUndoClear(NoteEditSessionUndoStack& stack, uint32_t token) {
  for (size_t turns = 1; turns <= 4096; ++turns) {
    const size_t before = stack.clearInventoryForTest();
    const auto result = stack.stepClear(token);
    if (result == UndoClearResult::Complete) {
      TEST_ASSERT_EQUAL_UINT32(0, before); // terminal is a separate grant
      TEST_ASSERT_TRUE(stack.clearComplete(token));
      return turns;
    }
    TEST_ASSERT_EQUAL(static_cast<int>(UndoClearResult::Progress), static_cast<int>(result));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before - 1, stack.clearInventoryForTest(),
                                     "one grant retires exactly one leaf, bucket table or empty shell");
    TEST_ASSERT_FALSE(stack.clearComplete(token));
  }
  TEST_FAIL_MESSAGE("undo clear exceeded fixed4096-turn fixture limit");
  return 0;
}

static void test_bounded_undo_clear_withheld_and_exact_nested_grants() {
  NoteEditSessionUndoStack stack;
  TEST_ASSERT_TRUE(stack.pushEntry(makeNestedClearEntry()));
  TEST_ASSERT_EQUAL_UINT32(63, stack.clearInventoryForTest()); //62 payload +1 shell
  const uint32_t token = stack.beginClear();
  TEST_ASSERT_NOT_EQUAL(0, token);
  for (unsigned query = 0; query < 8; ++query) {
    TEST_ASSERT_FALSE(stack.clearComplete(token));
    TEST_ASSERT_EQUAL_UINT32(63, stack.clearInventoryForTest());
  }
  TEST_ASSERT_FALSE(stack.canUndo()); TEST_ASSERT_FALSE(stack.canRedo());
  TEST_ASSERT_EQUAL_UINT32(0, stack.undoCount()); TEST_ASSERT_EQUAL_UINT32(0, stack.redoCount());
  TEST_ASSERT_NULL(stack.popUndoTarget()); TEST_ASSERT_NULL(stack.popRedoTarget());
  TEST_ASSERT_NULL(stack.peekRedoTarget());
  stack.advanceRedoCursor(); stack.dropRedoBranch();
  TEST_ASSERT_EQUAL_UINT32(63, stack.clearInventoryForTest());
  SessionUndoEntry denied = makeNestedClearEntry();
  TEST_ASSERT_FALSE(stack.pushEntry(denied));
  TEST_ASSERT_FALSE(stack.pushEntry(std::move(denied)));
  TEST_ASSERT_EQUAL_UINT32(4, denied.focus.baselineMap.size());
  TEST_ASSERT_EQUAL_UINT32(2, denied.editRows.size());
  TEST_ASSERT_EQUAL_UINT32(3, denied.editRows.back().addedEvents.size());
  TEST_ASSERT_EQUAL_UINT32(63, stack.clearInventoryForTest());
  TEST_ASSERT_EQUAL_UINT32(token, stack.beginClear());
  TEST_ASSERT_EQUAL_UINT32(64, drainUndoClear(stack, token));
  TEST_ASSERT_EQUAL(static_cast<int>(UndoClearResult::Complete), static_cast<int>(stack.stepClear(token)));
  TEST_ASSERT_EQUAL_UINT32(0, stack.clearEntryCountForTest());
}

static void test_bounded_undo_clear_empty_and_redo_history() {
  NoteEditSessionUndoStack stack;
  const uint32_t emptyToken = stack.beginClear();
  TEST_ASSERT_FALSE(stack.clearComplete(emptyToken));
  TEST_ASSERT_EQUAL_UINT32(1, drainUndoClear(stack, emptyToken));
  TEST_ASSERT_TRUE(stack.pushEntry(makeNestedClearEntry()));
  TEST_ASSERT_FALSE(stack.clearComplete(emptyToken));
  TEST_ASSERT_TRUE(stack.pushEntry(SessionUndoEntry{}));
  TEST_ASSERT_NOT_NULL(stack.popUndoTarget()); // newest empty row becomes redo
  TEST_ASSERT_EQUAL_UINT32(1, stack.undoCount());
  TEST_ASSERT_EQUAL_UINT32(1, stack.redoCount());
  const uint32_t token = stack.beginClear();
  TEST_ASSERT_NOT_EQUAL(emptyToken, token);
  TEST_ASSERT_EQUAL_UINT32(64, stack.clearInventoryForTest());
  TEST_ASSERT_EQUAL(static_cast<int>(UndoClearResult::Progress), static_cast<int>(stack.stepClear(token)));
  TEST_ASSERT_EQUAL_UINT32(1, stack.clearEntryCountForTest());
  TEST_ASSERT_EQUAL_UINT32(63, stack.clearInventoryForTest()); //older nested entry untouched
  TEST_ASSERT_EQUAL_UINT32(64, drainUndoClear(stack, token));
  TEST_ASSERT_TRUE(stack.pushEntry(SessionUndoEntry{}));
  TEST_ASSERT_TRUE(stack.canUndo());
  TEST_ASSERT_NOT_NULL(stack.popUndoTarget());
  TEST_ASSERT_TRUE(stack.canRedo());
  stack.clear();
  TEST_ASSERT_FALSE(stack.canUndo()); TEST_ASSERT_FALSE(stack.canRedo());
}

static void test_bounded_undo_clear_bucket_only_is_separate_from_shell() {
  NoteEditSessionUndoStack stack;
  SessionUndoEntry entry;
  entry.focus.baselineMap.rehash(1024);
  TEST_ASSERT_TRUE(entry.focus.baselineMap.bucket_count() >= 1024);
  TEST_ASSERT_TRUE(stack.pushEntry(std::move(entry)));
  TEST_ASSERT_EQUAL_UINT32(2, stack.clearInventoryForTest()); //bucket +shell
  const auto token = stack.beginClear();
  TEST_ASSERT_EQUAL_UINT32(2, stack.clearInventoryForTest());
  TEST_ASSERT_EQUAL(static_cast<int>(UndoClearResult::Progress), static_cast<int>(stack.stepClear(token)));
  TEST_ASSERT_EQUAL_UINT32(1, stack.clearInventoryForTest());
  TEST_ASSERT_EQUAL_UINT32(1, stack.clearEntryCountForTest());
  TEST_ASSERT_EQUAL(static_cast<int>(UndoClearResult::Progress), static_cast<int>(stack.stepClear(token)));
  TEST_ASSERT_EQUAL_UINT32(0, stack.clearEntryCountForTest());
  TEST_ASSERT_FALSE(stack.clearComplete(token));
  TEST_ASSERT_EQUAL(static_cast<int>(UndoClearResult::Complete), static_cast<int>(stack.stepClear(token)));
}

static void test_bounded_undo_clear_rejects_stale_grants_after_synchronous_reuse() {
  NoteEditSessionUndoStack stack;
  TEST_ASSERT_TRUE(stack.pushEntry(makeNestedClearEntry()));
  const auto old = stack.beginClear();
  TEST_ASSERT_EQUAL(static_cast<int>(UndoClearResult::Progress), static_cast<int>(stack.stepClear(old)));
  stack.clear();
  TEST_ASSERT_FALSE(stack.clearComplete(old));
  TEST_ASSERT_TRUE(stack.pushEntry(makeNestedClearEntry()));
  TEST_ASSERT_EQUAL(static_cast<int>(UndoClearResult::Superseded), static_cast<int>(stack.stepClear(old)));
  TEST_ASSERT_EQUAL_UINT32(63, stack.clearInventoryForTest());
  TEST_ASSERT_TRUE(stack.canUndo());
  const auto successor = stack.beginClear();
  TEST_ASSERT_NOT_EQUAL(old, successor);
  TEST_ASSERT_EQUAL(static_cast<int>(UndoClearResult::Superseded), static_cast<int>(stack.stepClear(old)));
  TEST_ASSERT_EQUAL(static_cast<int>(UndoClearResult::Superseded), static_cast<int>(stack.stepClear(0)));
  TEST_ASSERT_EQUAL_UINT32(63, stack.clearInventoryForTest());
  TEST_ASSERT_FALSE(stack.clearComplete(old));
  TEST_ASSERT_EQUAL_UINT32(64, drainUndoClear(stack, successor));
}

static void test_bounded_undo_clear_collisions_and_live_content_are_independent() {
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = 1536;
  loop.passes.recordPass = makeRecordPassWithNote(1, 100);
  auto empty = LoopSnapshot::createEmpty(loop.loopId, 1536, 0, 2, 3);
  LoopSnapshotChangeSet changes;
  SnapshotNoteChange note{}; note.kind = SnapshotChangeKind::Add;
  note.next = {loop.loopId, 1, 60, 100, 100, 148};
  changes.notes.push_back(note);
  for (unsigned edge = 0; edge < 2; ++edge) {
    SnapshotEventChange event{}; event.kind = SnapshotChangeKind::Add;
    auto value = edge == 0 ? MidiEvent::NoteOn(100, 1, 60, 100) : MidiEvent::NoteOff(148, 1, 60);
    value.noteId = 1;
    event.next = {loop.loopId, edge + 1, edge, value};
    changes.events.push_back(event);
  }
  LoopSnapshotConstruction construction(empty, changes);
  for (unsigned turn = 0; !construction.complete() && turn < 1024; ++turn)
    TEST_ASSERT_TRUE(construction.step(128));
  TEST_ASSERT_TRUE(construction.complete());
  auto root = construction.freeze();
  TEST_ASSERT_NOT_NULL(root.get());
  loop.storePublishedSnapshotForTest(root, loop.playbackRevision);
  const long owners = root.use_count();
  const auto revision = loop.publishedSnapshotRevision();
  const auto nodes = snapshotAllocNodeCount();
  const auto chunks = LoopEventStore::usedChunkCount();
  NoteEditFocus live;
  const NoteBaseline span{60, 100, 100, 148};
  live.baselineMap[1] = span;
  NoteEditCurrentState liveCurrent;
  liveCurrent.upsertRow(1, span, span, NoteEditPresenceType::Visible);
  NoteEditSessionUndoStack stack;
  SessionUndoEntry entry;
  entry.focus.baselineMap.rehash(64);
  const size_t buckets = entry.focus.baselineMap.bucket_count();
  for (unsigned i = 1; i <= 8; ++i) entry.focus.baselineMap[i * buckets] = span;
  TEST_ASSERT_EQUAL_UINT32(8, entry.focus.baselineMap.bucket_size(entry.focus.baselineMap.bucket(buckets)));
  TEST_ASSERT_TRUE(stack.pushEntry(std::move(entry)));
  const auto token = stack.beginClear();
  TEST_ASSERT_EQUAL_UINT32(11, drainUndoClear(stack, token)); //8nodes+bucket+shell+terminal
  TEST_ASSERT_EQUAL_UINT16(chunks, LoopEventStore::usedChunkCount());
  TEST_ASSERT_EQUAL_PTR(root.get(), loop.publishedSnapshotIdentity());
  TEST_ASSERT_EQUAL_INT(owners, root.use_count());
  TEST_ASSERT_EQUAL_UINT32(revision, loop.publishedSnapshotRevision());
  TEST_ASSERT_EQUAL_UINT32(nodes, snapshotAllocNodeCount());
  const auto noteAfter = root->findNoteAtPitch(1, 60);
  TEST_ASSERT_NOT_NULL(noteAfter.get());
  TEST_ASSERT_EQUAL_UINT32(100, noteAfter->startTick);
  TEST_ASSERT_EQUAL_UINT32(148, noteAfter->endTick);
  TEST_ASSERT_EQUAL_UINT32(1, live.baselineMap.size());
  TEST_ASSERT_EQUAL_UINT32(1, liveCurrent.size());
  TEST_ASSERT_EQUAL_UINT32(100, liveCurrent.find(1)->currentSpan.startTick);
  LoopEventStore events;
  loop.passes.materialize(events, loop.loopLengthTicks);
  TEST_ASSERT_EQUAL_UINT32(2, events.size());
  TEST_ASSERT_TRUE(events.at(0).isNoteOn()); TEST_ASSERT_EQUAL_UINT32(100, events.at(0).tick);
  TEST_ASSERT_TRUE(events.at(1).isNoteOff()); TEST_ASSERT_EQUAL_UINT32(148, events.at(1).tick);
}
