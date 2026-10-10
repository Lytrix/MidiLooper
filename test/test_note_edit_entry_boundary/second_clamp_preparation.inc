// Final landing preparation drives the real worker; source setup is cold fixture work.
static Track& makeSecondClampSource() {
  Track& track = makeRealSelectEntrySource();
  Loop& loop = track.getLoop(0);
  publishSelectionNotes(loop, {{loop.loopId, 910, 50, 100, 1500, 1520},
                               {loop.loopId, 900, 60, 100, 700, 720}});
  auto& session = editManager.getEditSession();
  session.active = true;
  session.sessionType = EditSessionType::Note;
  session.openedOnCurrentSnapshot = true;
  return track;
}

static void beginAndReachSecondClamp(Track& track) {
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Progress), static_cast<int>(
      editManager.beginDefaultNoteEditSelectionPreparation(track, 0)));
  for (size_t turn = 0; turn < 1000000; ++turn) {
    const size_t before = editManager.selectionPreparationUnitsForTest();
    TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Progress), static_cast<int>(
        editManager.stepDefaultNoteEditSelectionPreparation(track)));
    TEST_ASSERT_EQUAL_UINT32(before + 1, editManager.selectionPreparationUnitsForTest());
    TEST_ASSERT_NULL(editManager.preparedDefaultNoteEditSelection(track));
    if (editManager.selectionPreparationIsReselectingForTest()) return;
  }
  TEST_FAIL_MESSAGE("second-clamp transition was not reached within fixed turn limit");
}

static void test_second_clamp_withheld_pass_is_private_bounded_and_unpinned() {
  LoopEventStore::initPool();
  Track& track = makeSecondClampSource();
  auto root = track.getLoop(0).publishedSnapshot();
  const long owners = root.use_count();
  const auto nodes = snapshotAllocNodeCount();
  const auto bytes = snapshotAllocByteCount();
  const auto chunks = LoopEventStore::usedChunkCount();
  beginAndReachSecondClamp(track);
  const size_t units = editManager.selectionPreparationUnitsForTest();
  for (unsigned i = 0; i < 8; ++i) TEST_ASSERT_NULL(editManager.preparedDefaultNoteEditSelection(track));
  TEST_ASSERT_EQUAL_UINT32(units, editManager.selectionPreparationUnitsForTest());
  TEST_ASSERT_EQUAL_INT(owners, root.use_count());
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Progress), static_cast<int>(
      editManager.stepDefaultNoteEditSelectionPreparation(track)));
  TEST_ASSERT_EQUAL_UINT32(units + 1, editManager.selectionPreparationUnitsForTest());
  TEST_ASSERT_NULL(editManager.preparedDefaultNoteEditSelection(track));
  const auto prepared = drainSelectionPreparation(track);
  TEST_ASSERT_EQUAL_UINT32(900, prepared.primaryNote);
  TEST_ASSERT_EQUAL_UINT32(700, prepared.selectedTick);
  TEST_ASSERT_EQUAL_INT(0, prepared.selectedNoteIdx);
  TEST_ASSERT_EQUAL_UINT32(2, prepared.inventoryCount);
  TEST_ASSERT_EQUAL_UINT32(kInvalidNoteId, editManager.getNoteEditSessionState().selection.primaryNote);
  TEST_ASSERT_EQUAL_UINT32(0, outboundObservation().requestCalls);
  TEST_ASSERT_EQUAL_INT(owners, root.use_count());
  TEST_ASSERT_EQUAL_UINT32(nodes, snapshotAllocNodeCount());
  TEST_ASSERT_EQUAL_UINT32(bytes, snapshotAllocByteCount());
  TEST_ASSERT_EQUAL_UINT16(chunks, LoopEventStore::usedChunkCount());
}

static void test_second_clamp_source_replacement_restarts_first_pass() {
  LoopEventStore::initPool();
  Track& track = makeSecondClampSource();
  beginAndReachSecondClamp(track);
  Loop& loop = track.getLoop(0);
  publishSelectionNotes(loop, {{loop.loopId, 9000, 64, 100, 600, 620}});
  TEST_ASSERT_NULL(editManager.preparedDefaultNoteEditSelection(track));
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Progress), static_cast<int>(
      editManager.stepDefaultNoteEditSelectionPreparation(track)));
  TEST_ASSERT_FALSE(editManager.selectionPreparationIsReselectingForTest());
  const auto prepared = drainSelectionPreparation(track);
  TEST_ASSERT_EQUAL_UINT32(9000, prepared.primaryNote);
  TEST_ASSERT_EQUAL_UINT32(600, prepared.selectedTick);
  TEST_ASSERT_EQUAL_UINT32(1, prepared.inventoryCount);
}

static void test_second_clamp_fsm_only_change_supersedes_pending_and_completed() {
  LoopEventStore::initPool();
  Track& track = makeSecondClampSource();
  beginAndReachSecondClamp(track);
  const auto selection = editManager.getNoteEditSessionState().selection;
  editManager.setState(editManager.getStartNoteState(), track, 0); // other FSM is a named boundary
  TEST_ASSERT_EQUAL_UINT32(selection.primaryNote, editManager.getNoteEditSessionState().selection.primaryNote);
  TEST_ASSERT_EQUAL_UINT32(selection.selectedTick, editManager.getSelectedTick());
  TEST_ASSERT_NULL(editManager.preparedDefaultNoteEditSelection(track));
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Superseded), static_cast<int>(
      editManager.stepDefaultNoteEditSelectionPreparation(track)));
  editManager.setState(nullptr, track);
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Superseded), static_cast<int>(
      editManager.stepDefaultNoteEditSelectionPreparation(track)));
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Progress), static_cast<int>(
      editManager.beginDefaultNoteEditSelectionPreparation(track, 0)));
  TEST_ASSERT_EQUAL_UINT32(900, drainSelectionPreparation(track).primaryNote);
  editManager.setState(editManager.getStartNoteState(), track, 0);
  TEST_ASSERT_NULL(editManager.preparedDefaultNoteEditSelection(track));
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Superseded), static_cast<int>(
      editManager.stepDefaultNoteEditSelectionPreparation(track)));
}

static void test_second_clamp_already_select_keeps_first_landing_without_reentry() {
  LoopEventStore::initPool();
  Track& track = makeSecondClampSource();
  auto* select = editManager.getSelectNoteState();
  editManager.setState(select, track, 0);
  const size_t entries = select->entryReselectionsForTest();
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Progress), static_cast<int>(
      editManager.beginDefaultNoteEditSelectionPreparation(track, 0)));
  const auto prepared = drainSelectionPreparation(track);
  TEST_ASSERT_FALSE(editManager.selectionPreparationIsReselectingForTest());
  TEST_ASSERT_EQUAL_UINT32(910, prepared.primaryNote);
  TEST_ASSERT_EQUAL_UINT32(1500, prepared.selectedTick);
  TEST_ASSERT_EQUAL_INT(1, prepared.selectedNoteIdx);
  editManager.enterDefaultNoteEditSessionState(track, 0);
  TEST_ASSERT_EQUAL_UINT32(entries, select->entryReselectionsForTest());
  TEST_ASSERT_EQUAL_UINT32(910, editManager.getNoteEditSessionState().selection.primaryNote);
  TEST_ASSERT_EQUAL_UINT32(1500, editManager.getSelectedTick());
}

static void test_second_clamp_clean_visual_nearest_only_preserves_origin_and_tie_order() {
  LoopEventStore::initPool();
  Track& track = makeSecondClampSource();
  Loop& loop = track.getLoop(0);
  loop.loopStartTick = 1000;
  NoteUtils::DisplayNote exact{}, first{}, tied{};
  exact.noteId = 9200; exact.note = 50; exact.startTick = 700; exact.endTick = 720;
  first.noteId = 9100; first.note = 64; first.startTick = 164; first.endTick = 180;
  tied.noteId = 9300; tied.note = 60; tied.startTick = 204; tied.endTick = 220;
  loop.visualCache.notes = {exact, first, tied};
  ++loop.visualCache.revision;
  loop.visualCacheDirty = false;
  TEST_ASSERT_EQUAL_UINT32(768, editManager.noteEditSelectNavigationLengthTicks(track));
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Progress), static_cast<int>(
      editManager.beginDefaultNoteEditSelectionPreparation(track, 700)));
  const auto prepared = drainSelectionPreparation(track);
  TEST_ASSERT_TRUE(editManager.selectionPreparationIsReselectingForTest());
  // First exact landing is1236. Re-clamp target720; relative700/740 tie,
  // so visual ordinal1 wins, not original exact ordinal0 or lower pitch ordinal2.
  TEST_ASSERT_EQUAL_UINT32(9100, prepared.primaryNote);
  TEST_ASSERT_EQUAL_UINT32(700, prepared.selectedTick);
  TEST_ASSERT_EQUAL_INT(1, prepared.selectedNoteIdx);
  TEST_ASSERT_EQUAL_UINT32(3, prepared.inventoryCount);
  editManager.enterDefaultNoteEditSessionState(track, 700);
  TEST_ASSERT_EQUAL_UINT32(9100, editManager.getNoteEditSessionState().selection.primaryNote);
  TEST_ASSERT_EQUAL_UINT32(700, editManager.getSelectedTick());
  TEST_ASSERT_EQUAL_INT(1, editManager.getSelectedNoteIdx());
}
