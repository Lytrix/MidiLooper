// Real Select FSM entry, not the old no-op boundary.
static Track& makeRealSelectEntrySource() {
  Track& track = makeSelectionSource(2);
  editManager.setState(nullptr, track);
  return track;
}

static void test_real_select_default_entry_consumes_completed_selection_once() {
  LoopEventStore::initPool();
  Track& track = makeRealSelectEntrySource();
  auto* select = editManager.getSelectNoteState();
  const size_t before = select->entryReselectionsForTest();
  auto& store = editManager.getEditSession().store;
  TEST_ASSERT_FALSE(store.hasEventsCacheForTest());
  clockManager.assignCurrentTickSilently(50);
  pressEditModeButton();
  TEST_ASSERT_TRUE(editManager.isNoteEditActive());
  TEST_ASSERT_EQUAL_PTR(select, editManager.getCurrentState());
  TEST_ASSERT_EQUAL_UINT32(2000, editManager.getNoteEditSessionState().selection.primaryNote);
  TEST_ASSERT_EQUAL_UINT32(0, editManager.getSelectedTick());
  TEST_ASSERT_EQUAL_INT(0, editManager.getSelectedNoteIdx());
  TEST_ASSERT_EQUAL_UINT32(0, editManager.getReferenceStep());
  TEST_ASSERT_EQUAL_UINT32(before, select->entryReselectionsForTest());
}

static void test_real_select_default_entry_does_not_create_or_dirty_event_cache() {
  LoopEventStore::initPool();
  Track& track = makeRealSelectEntrySource();
  auto& store = editManager.getEditSession().store;
  TEST_ASSERT_FALSE(store.hasEventsCacheForTest());
  TEST_ASSERT_FALSE(store.isEventsDirty());
  clockManager.assignCurrentTickSilently(50);
  pressEditModeButton();
  TEST_ASSERT_EQUAL_UINT32(0, editManager.getSelectNoteState()->lastEventCountForTest());
  TEST_ASSERT_FALSE(editManager.getSelectNoteState()->lastCountMutatedCacheForTest());
  TEST_ASSERT_EQUAL_UINT32(0, store.size());
  TEST_ASSERT_EQUAL_UINT32(1, outboundObservation().requestCalls);
  TEST_ASSERT_TRUE(track.getLoop(0).publishedSnapshotIsCurrent());
}

static void test_real_select_empty_default_entry_keeps_bracket_without_reselection() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  commitControllerOnlyLoop(0, 0, kShortLoopTicks);
  readySlotPlaying(track, 0, 0);
  Loop& loop = track.getLoop(0);
  for (size_t turn = 0; !loop.publishedSnapshotIsCurrent() && turn < 10000; ++turn)
    (void)loop.stepPendingSnapshotPublish(1);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  editManager.setState(nullptr, track);
  auto* select = editManager.getSelectNoteState();
  const size_t before = select->entryReselectionsForTest();
  clockManager.assignCurrentTickSilently(96);
  pressEditModeButton();
  finishLiveEntry(); // controller-only real settled root now completes on owner grants
  TEST_ASSERT_EQUAL_PTR(select, editManager.getCurrentState());
  TEST_ASSERT_EQUAL_UINT32(kInvalidNoteId, editManager.getNoteEditSessionState().selection.primaryNote);
  TEST_ASSERT_EQUAL_UINT32(96, editManager.getSelectedTick());
  TEST_ASSERT_EQUAL_INT(-1, editManager.getSelectedNoteIdx());
  TEST_ASSERT_EQUAL_UINT32(before, select->entryReselectionsForTest());
  TEST_ASSERT_FALSE(select->lastCountMutatedCacheForTest());
}

static void test_real_select_direct_entry_still_resolves_and_handoff_does_not_leak() {
  LoopEventStore::initPool();
  Track& track = makeRealSelectEntrySource();
  clockManager.assignCurrentTickSilently(50);
  pressEditModeButton();
  auto* select = editManager.getSelectNoteState();
  editManager.setState(nullptr, track);
  const size_t before = select->entryReselectionsForTest();
  editManager.setState(select, track, 740);
  TEST_ASSERT_EQUAL_UINT32(before + 1, select->entryReselectionsForTest());
  TEST_ASSERT_EQUAL_UINT32(2001, editManager.getNoteEditSessionState().selection.primaryNote);
  TEST_ASSERT_EQUAL_UINT32(768, editManager.getSelectedTick());
  TEST_ASSERT_EQUAL_INT(1, editManager.getSelectedNoteIdx());
}

static void test_real_select_entry_observes_canonical_count_without_materializing_values() {
  LoopEventStore::initPool();
  Track& track = makeRealSelectEntrySource();
  auto& session = editManager.getEditSession();
  session.active = true;
  session.sessionType = EditSessionType::Note;
  session.openedOnCurrentSnapshot = true;
  TEST_ASSERT_TRUE(session.store.mutStore().append(MidiEvent::ControlChange(10, 1, 7, 64)));
  TEST_ASSERT_TRUE(session.store.mutStore().append(MidiEvent::ControlChange(20, 1, 7, 65)));
  TEST_ASSERT_FALSE(session.store.hasEventsCacheForTest());
  TEST_ASSERT_EQUAL_UINT32(2, session.store.eventValueCount());
  TEST_ASSERT_FALSE(session.store.hasEventsCacheForTest());
  TEST_ASSERT_FALSE(session.store.isEventsDirty());
  editManager.setState(editManager.getSelectNoteState(), track, 0);
  TEST_ASSERT_EQUAL_UINT32(2, editManager.getSelectNoteState()->lastEventCountForTest());
  TEST_ASSERT_EQUAL_UINT32(2, session.store.size());
  TEST_ASSERT_FALSE(editManager.getSelectNoteState()->lastCountMutatedCacheForTest());
}

static void test_real_select_entry_observes_dirty_value_count_without_sync_or_discard() {
  LoopEventStore::initPool();
  Track& track = makeRealSelectEntrySource();
  auto& session = editManager.getEditSession();
  session.active = true;
  session.sessionType = EditSessionType::Note;
  session.openedOnCurrentSnapshot = true;
  TEST_ASSERT_TRUE(session.store.mutStore().append(MidiEvent::ControlChange(10, 1, 7, 64)));
  TEST_ASSERT_TRUE(session.store.mutStore().append(MidiEvent::ControlChange(20, 1, 7, 65)));
  session.store.mutEvents().push_back(MidiEvent::ControlChange(30, 1, 7, 66));
  TEST_ASSERT_EQUAL_UINT32(2, session.store.size());
  TEST_ASSERT_TRUE(session.store.isEventsDirty());
  editManager.setState(editManager.getSelectNoteState(), track, 0);
  TEST_ASSERT_EQUAL_UINT32(3, session.store.eventValueCount());
  TEST_ASSERT_EQUAL_UINT32(2, session.store.size());
  TEST_ASSERT_EQUAL_UINT32(3, editManager.getSelectNoteState()->lastEventCountForTest());
  TEST_ASSERT_EQUAL_UINT32(2, session.store.size());
  TEST_ASSERT_TRUE(session.store.isEventsDirty());
  TEST_ASSERT_EQUAL_UINT32(3, session.store.readEvents().size());
  TEST_ASSERT_EQUAL_UINT8(66, session.store.readEvents()[2].data.ccData.value);
  session.store.discardEventsCache(); // teardown only; entry never drops pending values
}

static void test_real_select_zero_length_default_return_preserves_direct_entry_behavior() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  editManager.setState(nullptr, track);
  auto& session = editManager.getEditSession();
  session.active = true;
  session.sessionType = EditSessionType::Note;
  auto* select = editManager.getSelectNoteState();
  const size_t before = select->entryReselectionsForTest();
  editManager.enterDefaultNoteEditSessionState(track, 700);
  TEST_ASSERT_NULL(editManager.getCurrentState());
  TEST_ASSERT_EQUAL_UINT32(before, select->entryReselectionsForTest());
  TEST_ASSERT_EQUAL_INT(-1, editManager.getSelectedNoteIdx());
  TEST_ASSERT_FALSE(session.store.hasEventsCacheForTest());
  editManager.setState(select, track, 700);
  TEST_ASSERT_EQUAL_UINT32(before + 1, select->entryReselectionsForTest());
}

static void test_real_select_default_entry_preserves_legacy_second_clamp_landing() {
  LoopEventStore::initPool();
  Track& track = makeRealSelectEntrySource();
  Loop& loop = track.getLoop(0);
  publishSelectionNotes(loop, {{loop.loopId, 910, 50, 100, 1500, 1520},
                               {loop.loopId, 900, 60, 100, 700, 720}});
  auto& session = editManager.getEditSession();
  session.active = true;
  session.sessionType = EditSessionType::Note;
  session.openedOnCurrentSnapshot = true;
  TEST_ASSERT_EQUAL_UINT32(768, editManager.noteEditSelectNavigationLengthTicks(track));
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Progress), static_cast<int>(
      editManager.beginDefaultNoteEditSelectionPreparation(track, 0)));
  const auto finalCandidate = drainSelectionPreparation(track);
  TEST_ASSERT_EQUAL_UINT32(900, finalCandidate.primaryNote);
  TEST_ASSERT_EQUAL_UINT32(700, finalCandidate.selectedTick);
  TEST_ASSERT_EQUAL_INT(0, finalCandidate.selectedNoteIdx);
  TEST_ASSERT_EQUAL_UINT32(2, finalCandidate.inventoryCount);
  TEST_ASSERT_EQUAL_UINT32(kInvalidNoteId,
      editManager.getNoteEditSessionState().selection.primaryNote);
  editManager.enterDefaultNoteEditSessionState(track, 0);
  TEST_ASSERT_NULL(editManager.preparedDefaultNoteEditSelection(track));
  // First nearest is wrap-adjacent NoteId910@1500. The real legacy FSM then
  // clamps1500 to720 and lands on NoteId900@700; do not erase that semantic pass.
  TEST_ASSERT_EQUAL_UINT32(900, editManager.getNoteEditSessionState().selection.primaryNote);
  TEST_ASSERT_EQUAL_UINT32(700, editManager.getSelectedTick());
  TEST_ASSERT_EQUAL_INT(0, editManager.getSelectedNoteIdx()); // bar-zero row precedes bar-one row
}
