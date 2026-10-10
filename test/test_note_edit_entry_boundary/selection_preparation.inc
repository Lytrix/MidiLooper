// Bounded default-selection preparation; source publication is a fixture boundary,
// preparation itself runs the real EditManager worker. No copied worker state machine.
using SelectionResult = EditManager::SelectionPreparationResult;

static void publishSelectionNotes(Loop& loop, const std::vector<LoopSnapshotNote>& notes) {
  auto parent = std::make_shared<const LoopSnapshot>(
      LoopSnapshot(loop.loopId, loop.loopLengthTicks, 0, 8, 8));
  LoopSnapshotChangeSet changes;
  for (const auto& note : notes) {
    SnapshotNoteChange change{};
    change.kind = SnapshotChangeKind::Add;
    change.next = note;
    changes.notes.push_back(change);
  }
  LoopSnapshotConstruction construction(parent, changes);
  const size_t maximumTurns = (notes.size() + 1) *
      (kSnapshotMembershipMaxPages + kLoopSnapshotPitchCount + kLoopSnapshotBarsPerPage + 1);
  for (size_t turns = 0; !construction.complete() && turns < maximumTurns; ++turns) {
    TEST_ASSERT_TRUE(construction.step(128)); // cold source setup, not a selection unit
  }
  TEST_ASSERT_TRUE(construction.complete());
  loop.storePublishedSnapshotForTest(construction.freeze(), loop.playbackRevision);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  loop.visualCacheDirty = true;
}

static Track& makeSelectionSource(uint32_t bars = 2) {
  Track& track = trackManager.getTrack(0);
  commitLoop(0, 0, bars * Config::TICKS_PER_BAR, 1);
  readySlotPlaying(track, 0, 0);
  publishCommittedRootForSlot(track.getLoop(0), bars);
  track.getLoop(0).visualCacheDirty = true;
  return track;
}

static EditManager::PreparedNoteEditSelection drainSelectionPreparation(Track& track) {
  for (size_t turns = 0; turns < 1000000; ++turns) {
    const size_t before = editManager.selectionPreparationUnitsForTest();
    const auto result = editManager.stepDefaultNoteEditSelectionPreparation(track);
    const size_t after = editManager.selectionPreparationUnitsForTest();
    TEST_ASSERT_TRUE_MESSAGE(after <= before + 1,
                             "one grant cannot execute multiple selection units");
    if (result == SelectionResult::Complete) {
      const auto* prepared = editManager.preparedDefaultNoteEditSelection(track);
      TEST_ASSERT_NOT_NULL(prepared);
      return *prepared;
    }
    TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(SelectionResult::Progress),
                              static_cast<int>(result), "ready source must make bounded progress");
    TEST_ASSERT_NULL_MESSAGE(editManager.preparedDefaultNoteEditSelection(track),
                             "partial selection remains private");
  }
  TEST_FAIL_MESSAGE("selection drain exhausted its fixed fixture limit");
  return {};
}

static void test_selection_preparation_withheld_units_keep_result_private_and_source_unpinned() {
  LoopEventStore::initPool();
  Track& track = makeSelectionSource(64);
  auto root = track.getLoop(0).publishedSnapshot();
  const uint16_t chunksBefore = LoopEventStore::usedChunkCount();
  const uint32_t nodesBefore = snapshotAllocNodeCount();
  const uint32_t bytesBefore = snapshotAllocByteCount();
  const long owners = root.use_count();
  const auto beforeSelection = editManager.getNoteEditSessionState().selection;
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Progress), static_cast<int>(
      editManager.beginDefaultNoteEditSelectionPreparation(track, 0)));
  TEST_ASSERT_EQUAL_UINT32(0u, editManager.selectionPreparationUnitsForTest());
  for (unsigned query = 0; query < 8; ++query) {
    TEST_ASSERT_NULL(editManager.preparedDefaultNoteEditSelection(track));
  }
  TEST_ASSERT_EQUAL_UINT32(0u, editManager.selectionPreparationUnitsForTest());
  TEST_ASSERT_FALSE(editManager.isNoteEditActive());
  TEST_ASSERT_EQUAL_UINT32(beforeSelection.primaryNote,
      editManager.getNoteEditSessionState().selection.primaryNote);
  TEST_ASSERT_EQUAL_UINT32(0u, outboundObservation().requestCalls);
  TEST_ASSERT_EQUAL_INT(owners, root.use_count());
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Progress), static_cast<int>(
      editManager.stepDefaultNoteEditSelectionPreparation(track)));
  TEST_ASSERT_EQUAL_UINT32(1u, editManager.selectionPreparationUnitsForTest());
  const auto prepared = drainSelectionPreparation(track);
  TEST_ASSERT_EQUAL_UINT32(2000u, prepared.primaryNote);
  TEST_ASSERT_EQUAL_UINT32(0u, prepared.selectedTick);
  TEST_ASSERT_EQUAL_INT(0, prepared.selectedNoteIdx);
  TEST_ASSERT_EQUAL_UINT32(64u, prepared.inventoryCount);
  TEST_ASSERT_EQUAL_INT(owners, root.use_count());
  TEST_ASSERT_FALSE(editManager.isNoteEditActive());
  TEST_ASSERT_EQUAL_UINT16(chunksBefore, LoopEventStore::usedChunkCount());
  TEST_ASSERT_EQUAL_UINT32(nodesBefore, snapshotAllocNodeCount());
  TEST_ASSERT_EQUAL_UINT32(bytesBefore, snapshotAllocByteCount());
  TEST_ASSERT_EQUAL_UINT32(0u, outboundObservation().requestCalls);
  const size_t units = editManager.selectionPreparationUnitsForTest();
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Complete), static_cast<int>(
      editManager.stepDefaultNoteEditSelectionPreparation(track)));
  TEST_ASSERT_EQUAL_UINT32(units, editManager.selectionPreparationUnitsForTest());
}

static void test_selection_preparation_preserves_inventory_ties_and_nonzero_origin_clamp() {
  LoopEventStore::initPool();
  Track& track = makeSelectionSource(4);
  Loop& loop = track.getLoop(0);
  publishSelectionNotes(loop, {{loop.loopId, 910, 50, 100, 80, 100},
                               {loop.loopId, 900, 70, 100, 20, 40}});
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Progress), static_cast<int>(
      editManager.beginDefaultNoteEditSelectionPreparation(track, 50)));
  auto prepared = drainSelectionPreparation(track);
  // Equal-distance ties follow pitch/membership inventory order, not earlier start.
  TEST_ASSERT_EQUAL_UINT32(910u, prepared.primaryNote);
  TEST_ASSERT_EQUAL_UINT32(80u, prepared.selectedTick);
  TEST_ASSERT_EQUAL_INT(0, prepared.selectedNoteIdx);

  loop.loopStartTick = 100;
  loop.startLoopTick = 700;
  NoteUtils::DisplayNote visual{};
  visual.noteId = 99;
  visual.note = 60;
  visual.startTick = 1500;
  visual.endTick = 1550;
  loop.visualCache.notes.assign(1, visual);
  ++loop.visualCache.revision;
  publishSelectionNotes(loop, {{loop.loopId, 3333, 60, 100, 1502, 1540}});
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Progress), static_cast<int>(
      editManager.beginDefaultNoteEditSelectionPreparation(track, 2500)));
  prepared = drainSelectionPreparation(track);
  // Phase1800 -> clamp1502 (1550 - 48) -> origin-relative bracket1402.
  TEST_ASSERT_EQUAL_UINT32(3333u, prepared.primaryNote);
  TEST_ASSERT_EQUAL_UINT32(1402u, prepared.selectedTick);
  TEST_ASSERT_EQUAL_INT(0, prepared.selectedNoteIdx);
}

static void test_selection_preparation_exact_snapshot_dedup_across_pages_bars_and_wrap() {
  LoopEventStore::initPool();
  Track& track = makeSelectionSource(4);
  Loop& loop = track.getLoop(0);
  NoteUtils::DisplayNote wrapTail{};
  wrapTail.noteId = 4000;
  wrapTail.note = 62;
  wrapTail.startTick = 3000;
  wrapTail.endTick = loop.loopLengthTicks;
  loop.visualCache.notes.assign(1, wrapTail);
  ++loop.visualCache.revision;
  std::vector<LoopSnapshotNote> notes;
  for (unsigned i = 0; i < 18; ++i) {
    notes.push_back({loop.loopId, static_cast<NoteId>(3000 + i), 60, 100,
                     i * 4u, 3u * 768u + 100u});
  }
  notes.push_back({loop.loopId, 4000, 62, 100, 3000, 20});
  publishSelectionNotes(loop, notes);
  NoteUtils::DisplayNoteVec expected;
  appendSnapshotNotesForBarRange(*loop.publishedSnapshot(), 0, 3, expected);
  TEST_ASSERT_EQUAL_UINT32(19u, expected.size());
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Progress), static_cast<int>(
      editManager.beginDefaultNoteEditSelectionPreparation(track, 3000)));
  const auto prepared = drainSelectionPreparation(track);
  TEST_ASSERT_EQUAL_UINT32(19u, prepared.inventoryCount);
  TEST_ASSERT_EQUAL_UINT32(4000u, prepared.primaryNote);
  TEST_ASSERT_EQUAL_INT(18, prepared.selectedNoteIdx);
  TEST_ASSERT_EQUAL_UINT32(3000u, prepared.selectedTick);
}

static void test_selection_preparation_clean_visual_keeps_split_starts_and_invalid_identity() {
  LoopEventStore::initPool();
  Track& track = makeSelectionSource(2);
  Loop& loop = track.getLoop(0);
  NoteUtils::DisplayNote tail{}, head{}, invalid{};
  tail.noteId = head.noteId = 777;
  tail.note = head.note = 60;
  tail.startTick = 1400; tail.endTick = 1536;
  head.startTick = 0; head.endTick = 40;
  invalid.startTick = 30; invalid.endTick = 80; invalid.note = 70;
  loop.visualCache.notes = {invalid, tail, head};
  ++loop.visualCache.revision;
  loop.visualCacheDirty = false;
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Progress), static_cast<int>(
      editManager.beginDefaultNoteEditSelectionPreparation(track, 1400)));
  auto prepared = drainSelectionPreparation(track);
  TEST_ASSERT_EQUAL_UINT32(3u, prepared.inventoryCount);
  TEST_ASSERT_EQUAL_UINT32(777u, prepared.primaryNote);
  TEST_ASSERT_EQUAL_INT(1, prepared.selectedNoteIdx);
  TEST_ASSERT_EQUAL_UINT32(1400u, prepared.selectedTick);
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Progress), static_cast<int>(
      editManager.beginDefaultNoteEditSelectionPreparation(track, 30)));
  prepared = drainSelectionPreparation(track);
  TEST_ASSERT_EQUAL_UINT32(kInvalidNoteId, prepared.primaryNote);
  TEST_ASSERT_EQUAL_INT(-1, prepared.selectedNoteIdx);
  TEST_ASSERT_EQUAL_UINT32(30u, prepared.selectedTick);
}

static void test_selection_preparation_rejects_stale_result_and_restarts_replaced_source() {
  LoopEventStore::initPool();
  Track& track = makeSelectionSource(2);
  Loop& loop = track.getLoop(0);
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Progress), static_cast<int>(
      editManager.beginDefaultNoteEditSelectionPreparation(track, 50)));
  const auto first = drainSelectionPreparation(track);
  TEST_ASSERT_EQUAL_UINT32(2000u, first.primaryNote);
  publishSelectionNotes(loop, {{loop.loopId, 9999, 55, 100, 55, 80}});
  const size_t beforeQuery = editManager.selectionPreparationUnitsForTest();
  TEST_ASSERT_NULL(editManager.preparedDefaultNoteEditSelection(track));
  TEST_ASSERT_EQUAL_UINT32(beforeQuery, editManager.selectionPreparationUnitsForTest());
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Progress), static_cast<int>(
      editManager.stepDefaultNoteEditSelectionPreparation(track)));
  const auto replacement = drainSelectionPreparation(track);
  TEST_ASSERT_EQUAL_UINT32(9999u, replacement.primaryNote);
  TEST_ASSERT_EQUAL_UINT32(55u, replacement.selectedTick);
  TEST_ASSERT_EQUAL_UINT32(1u, replacement.inventoryCount);
  TEST_ASSERT_FALSE(editManager.isNoteEditActive());
}

static void test_selection_preparation_refocus_and_reset_supersede_only_private_work() {
  LoopEventStore::initPool();
  Track& track = makeSelectionSource(2);
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Progress), static_cast<int>(
      editManager.beginDefaultNoteEditSelectionPreparation(track, 50)));
  for (unsigned i = 0; i < 10; ++i) {
    TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Progress), static_cast<int>(
        editManager.stepDefaultNoteEditSelectionPreparation(track)));
  }
  trackManager.setSelectedTrack(1);
  TEST_ASSERT_NULL(editManager.preparedDefaultNoteEditSelection(track));
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Superseded), static_cast<int>(
      editManager.stepDefaultNoteEditSelectionPreparation(track)));
  TEST_ASSERT_FALSE(editManager.isNoteEditActive());
  TEST_ASSERT_EQUAL_UINT32(0u, outboundObservation().requestCalls);
  editManager.resetNoteEditSessionState();
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Idle), static_cast<int>(
      editManager.stepDefaultNoteEditSelectionPreparation(trackManager.getSelectedTrack())));
}

static void test_selection_preparation_empty_source_has_no_fabricated_identity() {
  LoopEventStore::initPool();
  Track& track = makeSelectionSource(2);
  publishSelectionNotes(track.getLoop(0), {});
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Progress), static_cast<int>(
      editManager.beginDefaultNoteEditSelectionPreparation(track, 50)));
  const auto prepared = drainSelectionPreparation(track);
  TEST_ASSERT_EQUAL_UINT32(0u, prepared.inventoryCount);
  TEST_ASSERT_EQUAL_UINT32(kInvalidNoteId, prepared.primaryNote);
  TEST_ASSERT_EQUAL_INT(-1, prepared.selectedNoteIdx);
  TEST_ASSERT_EQUAL_UINT32(50u, prepared.selectedTick);
}

static void test_selection_preparation_waits_for_real_snapshot_owner_without_losing_request() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  commitLoop(0, 0, 2u * 768u, 4);
  readySlotPlaying(track, 0, 0);
  Loop& loop = track.getLoop(0);
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::WaitingForSource), static_cast<int>(
      editManager.beginDefaultNoteEditSelectionPreparation(track, 50)));
  for (unsigned turn = 0; turn < 8; ++turn) {
    TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::WaitingForSource), static_cast<int>(
        editManager.stepDefaultNoteEditSelectionPreparation(track)));
    TEST_ASSERT_NULL(editManager.preparedDefaultNoteEditSelection(track));
    TEST_ASSERT_FALSE(editManager.isNoteEditActive());
  }
  TEST_ASSERT_TRUE(track.hasAudiblePlaybackSource(0));
  TEST_ASSERT_EQUAL_UINT32(0u, outboundObservation().requestCalls);
  TEST_ASSERT_TRUE(loop.requestCommittedPassesRepublish());
  clockManager.assignCurrentTickSilently(500);
  for (unsigned owner = 0; !loop.publishedSnapshotIsCurrent() && owner < 10000; ++owner) {
    (void)loop.stepPendingSnapshotPublish(1);
  }
  TEST_ASSERT_TRUE_MESSAGE(loop.publishedSnapshotIsCurrent(), "real Snapshot owner must publish");
  const auto prepared = drainSelectionPreparation(track);
  TEST_ASSERT_EQUAL_UINT32(4u, prepared.inventoryCount);
  TEST_ASSERT_EQUAL_UINT32(307u, prepared.selectedTick);
  TEST_ASSERT_TRUE(prepared.primaryNote != kInvalidNoteId);
  TEST_ASSERT_FALSE(editManager.isNoteEditActive());
}

static void test_selection_preparation_source_replacement_during_dedup_discards_old_coordinates() {
  LoopEventStore::initPool();
  Track& track = makeSelectionSource(2);
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Progress), static_cast<int>(
      editManager.beginDefaultNoteEditSelectionPreparation(track, 50)));
  for (unsigned turn = 0; !editManager.selectionPreparationIsDeduplicatingForTest() && turn < 10000; ++turn) {
    TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Progress), static_cast<int>(
        editManager.stepDefaultNoteEditSelectionPreparation(track)));
  }
  TEST_ASSERT_TRUE(editManager.selectionPreparationIsDeduplicatingForTest());
  TEST_ASSERT_NULL(editManager.preparedDefaultNoteEditSelection(track));
  publishSelectionNotes(track.getLoop(0), {{0, 8888, 55, 100, 55, 80}});
  const auto prepared = drainSelectionPreparation(track);
  TEST_ASSERT_EQUAL_UINT32(8888u, prepared.primaryNote);
  TEST_ASSERT_EQUAL_UINT32(55u, prepared.selectedTick);
  TEST_ASSERT_EQUAL_UINT32(1u, prepared.inventoryCount);
}

static void test_selection_preparation_slot_and_selection_supersession_are_qualified() {
  LoopEventStore::initPool();
  Track& track = makeSelectionSource(2);
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Progress), static_cast<int>(
      editManager.beginDefaultNoteEditSelectionPreparation(track, 50)));
  SlotAccess::focus(0, 1);
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Superseded), static_cast<int>(
      editManager.stepDefaultNoteEditSelectionPreparation(track)));
  TEST_ASSERT_NULL(editManager.preparedDefaultNoteEditSelection(track));
  SlotAccess::focus(0, 0);
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Progress), static_cast<int>(
      editManager.beginDefaultNoteEditSelectionPreparation(track, 50)));
  editManager.bumpSessionPreviewRevision();
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Superseded), static_cast<int>(
      editManager.stepDefaultNoteEditSelectionPreparation(track)));
  TEST_ASSERT_NULL(editManager.preparedDefaultNoteEditSelection(track));
  TEST_ASSERT_EQUAL_UINT32(0u, outboundObservation().requestCalls);
}

static void test_selection_preparation_unsupported_overlay_and_stopped_dirty_source_are_explicit() {
  LoopEventStore::initPool();
  Track& track = makeSelectionSource(2);
  track.forceSetState(TRACK_STOPPED);
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Unsupported), static_cast<int>(
      editManager.beginDefaultNoteEditSelectionPreparation(track, 50)));
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Unsupported), static_cast<int>(
      editManager.stepDefaultNoteEditSelectionPreparation(track)));
  TEST_ASSERT_NULL(editManager.preparedDefaultNoteEditSelection(track));
  track.forceSetState(TRACK_PLAYING);
  const NoteBaseline span{60, 100, 12, 60};
  editManager.noteEditCurrentStateMut().upsertRow(666, span, span, NoteEditPresenceType::Visible);
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Unsupported), static_cast<int>(
      editManager.beginDefaultNoteEditSelectionPreparation(track, 50)));
  TEST_ASSERT_NOT_NULL(editManager.noteEditCurrentState().find(666));
  TEST_ASSERT_FALSE(editManager.isNoteEditActive());
  editManager.noteEditCurrentStateMut().clear();
}

static void test_selection_preparation_matches_legacy_selection_on_ready_source() {
  LoopEventStore::initPool();
  Track& track = makeSelectionSource(4);
  Loop& loop = track.getLoop(0);
  publishSelectionNotes(loop, {{loop.loopId, 910, 50, 100, 80, 100},
                               {loop.loopId, 900, 70, 100, 20, 40}});
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Progress), static_cast<int>(
      editManager.beginDefaultNoteEditSelectionPreparation(track, 50)));
  const auto prepared = drainSelectionPreparation(track);
  clockManager.assignCurrentTickSilently(50);
  pressEditModeButton();
  TEST_ASSERT_TRUE(editManager.isNoteEditActive());
  TEST_ASSERT_EQUAL_UINT32(prepared.primaryNote,
      editManager.getNoteEditSessionState().selection.primaryNote);
  TEST_ASSERT_EQUAL_UINT32(prepared.selectedTick, editManager.getSelectedTick());
  TEST_ASSERT_EQUAL_INT(prepared.selectedNoteIdx, editManager.getSelectedNoteIdx());
}

static void test_selection_preparation_pending_work_preserves_real_current_root_midi() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  commitLoop(0, 0, 2u * 768u, 4);
  readySlotPlaying(track, 0, 0);
  Loop& loop = track.getLoop(0);
  TEST_ASSERT_TRUE(loop.requestCommittedPassesRepublish());
  for (unsigned owner = 0; !loop.publishedSnapshotIsCurrent() && owner < 10000; ++owner) {
    (void)loop.stepPendingSnapshotPublish(1);
  }
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  clockManager.assignCurrentTickSilently(50);
  pressEditModeButton();
  finishLiveEntry(); // migrated real settled root: unchanged final oracle
  TEST_ASSERT_TRUE(editManager.isNoteEditActive());
  TEST_ASSERT_TRUE(editManager.noteEditCurrentState().empty());
  const unsigned feedbackBefore = outboundObservation().requestCalls;
  const auto selectionBefore = editManager.getNoteEditSessionState().selection;
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Progress), static_cast<int>(
      editManager.beginDefaultNoteEditSelectionPreparation(track, 50)));
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Progress), static_cast<int>(
      editManager.stepDefaultNoteEditSelectionPreparation(track)));
  TEST_ASSERT_NULL(editManager.preparedDefaultNoteEditSelection(track));
  playCommittedWindow(track, 0, 320);
  TEST_ASSERT_EQUAL_UINT32(1u, track.sentPlaybackMidiCountForTest());
  auto* runtime = TrackPlaybackTestAccess::runtime(track, 0);
  TEST_ASSERT_NOT_NULL(runtime);
  TEST_ASSERT_TRUE(TrackPlaybackTestAccess::ledgerHolds(*runtime, selectionBefore.primaryNote));
  track.playMidiEvents(384u, true);
  TEST_ASSERT_FALSE(TrackPlaybackTestAccess::ledgerHolds(*runtime, selectionBefore.primaryNote));
  TEST_ASSERT_EQUAL_UINT32(2u, track.sentPlaybackMidiCountForTest());
  TEST_ASSERT_EQUAL_UINT32(307u, track.sentPlaybackMidiAtForTest(0).event.tick);
  TEST_ASSERT_TRUE(track.sentPlaybackMidiAtForTest(0).event.isNoteOn());
  TEST_ASSERT_EQUAL_UINT8(100u, track.sentPlaybackMidiAtForTest(0).event.data.noteData.velocity);
  TEST_ASSERT_EQUAL_UINT32(355u, track.sentPlaybackMidiAtForTest(1).event.tick);
  TEST_ASSERT_TRUE(track.sentPlaybackMidiAtForTest(1).event.isNoteOff());
  TEST_ASSERT_EQUAL_UINT32(2u, MidiSendCapture::count());
  const auto prepared = drainSelectionPreparation(track);
  TEST_ASSERT_EQUAL_UINT32(selectionBefore.primaryNote, prepared.primaryNote);
  TEST_ASSERT_EQUAL_UINT32(selectionBefore.primaryNote,
      editManager.getNoteEditSessionState().selection.primaryNote);
  TEST_ASSERT_EQUAL_UINT32(feedbackBefore, outboundObservation().requestCalls);
}

static void test_selection_preparation_distinguishes_empty_dirty_cache_from_pending_event_values() {
  LoopEventStore::initPool();
  Track& track = makeSelectionSource(2);
  TEST_ASSERT_TRUE(editManager.sessionMidiEvents().empty());
  auto& store = editManager.getEditSession().store;
  TEST_ASSERT_TRUE(store.isEventsDirty());
  TEST_ASSERT_FALSE(store.hasDirtyEventValues());
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Progress), static_cast<int>(
      editManager.beginDefaultNoteEditSelectionPreparation(track, 50)));
  (void)drainSelectionPreparation(track);
  editManager.sessionMidiEvents().push_back(MidiEvent::NoteOn(100, 1, 80, 90));
  TEST_ASSERT_TRUE(store.hasDirtyEventValues());
  TEST_ASSERT_NULL(editManager.preparedDefaultNoteEditSelection(track));
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Unsupported), static_cast<int>(
      editManager.stepDefaultNoteEditSelectionPreparation(track)));
  TEST_ASSERT_EQUAL_UINT32(1u, editManager.sessionMidiEvents().size());
  TEST_ASSERT_EQUAL_UINT8(80u, editManager.sessionMidiEvents()[0].data.noteData.note);
  store.discardEventsCache(); // fixture cleanup only; the worker never discards input.
}

// I/O-boundary adapter to the actual read unit. It does not replace the owner
// request/worker or manufacture its pending state; full traversal is covered above.
struct NoteEditSelectionPreparationTestAccess {
  static bool readLastPage(const LoopSnapshot& root, NoteUtils::DisplayNote& out) {
    EditManager::SelectionSnapshotPosition position{};
    position.pitch = 60;
    position.pageIndex = kSnapshotMembershipMaxPages - 1;
    position.entry = kSnapshotMembershipPageCapacity - 1;
    position.loaded = true;
    return EditManager::stepSelectionSnapshotPosition(root, position, out) ==
        EditManager::SelectionSnapshotRead::Row;
  }
  static bool rejectsOverCap(const LoopSnapshot& root) {
    EditManager::SelectionSnapshotPosition position{};
    position.pitch = 60;
    position.pageIndex = kSnapshotMembershipMaxPages;
    position.loaded = true;
    NoteUtils::DisplayNote unused{};
    return EditManager::stepSelectionSnapshotPosition(root, position, unused) ==
        EditManager::SelectionSnapshotRead::Unsupported;
  }
};

static void test_selection_preparation_snapshot_read_unit_accepts_maximum_page_and_rejects_over_cap() {
  LoopEventStore::initPool();
  Track& track = makeSelectionSource(1);
  Loop& loop = track.getLoop(0);
  std::vector<LoopSnapshotNote> notes;
  notes.reserve(kSnapshotMembershipMaxRefsPerBar);
  for (size_t i = 0; i < kSnapshotMembershipMaxRefsPerBar; ++i) {
    notes.push_back({loop.loopId, static_cast<NoteId>(10000 + i), 60, 100, 0, 48});
  }
  publishSelectionNotes(loop, notes);
  const auto root = loop.publishedSnapshot();
  const auto head = root->noteMembership(60, 0);
  size_t pages = 0, references = 0;
  for (auto page = head; page; page = page->next) {
    ++pages;
    references += page->count;
  }
  TEST_ASSERT_EQUAL_UINT32(256u, pages);
  TEST_ASSERT_EQUAL_UINT32(4096u, references);
  const uint32_t nodesBefore = snapshotAllocNodeCount();
  NoteUtils::DisplayNote last{};
  TEST_ASSERT_TRUE(NoteEditSelectionPreparationTestAccess::readLastPage(*root, last));
  TEST_ASSERT_EQUAL_UINT32(14095u, last.noteId);
  TEST_ASSERT_EQUAL_UINT32(0u, last.startTick);
  TEST_ASSERT_EQUAL_UINT32(48u, last.endTick);
  TEST_ASSERT_TRUE(NoteEditSelectionPreparationTestAccess::rejectsOverCap(*root));
  TEST_ASSERT_EQUAL_UINT32(nodesBefore, snapshotAllocNodeCount());
}

static void test_selection_preparation_snapshot_dedup_preserves_same_identity_different_starts() {
  LoopEventStore::initPool();
  Track& track = makeSelectionSource(2);
  Loop& loop = track.getLoop(0);
  NoteUtils::DisplayNote tail{};
  tail.noteId = 777;
  tail.note = 60;
  tail.startTick = 1400;
  tail.endTick = 1536;
  loop.visualCache.notes.assign(1, tail);
  ++loop.visualCache.revision;
  publishSelectionNotes(loop, {{loop.loopId, 777, 60, 100, 1400, 1536},
                               {loop.loopId, 777, 60, 100, 0, 40}});
  NoteUtils::DisplayNoteVec expected;
  appendSnapshotNotesForBarRange(*loop.publishedSnapshot(), 0, 1, expected);
  TEST_ASSERT_EQUAL_UINT32(2u, expected.size());
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Progress), static_cast<int>(
      editManager.beginDefaultNoteEditSelectionPreparation(track, 0)));
  auto prepared = drainSelectionPreparation(track);
  TEST_ASSERT_EQUAL_UINT32(2u, prepared.inventoryCount);
  TEST_ASSERT_EQUAL_UINT32(777u, prepared.primaryNote);
  TEST_ASSERT_EQUAL_UINT32(0u, prepared.selectedTick);
  TEST_ASSERT_EQUAL_INT(0, prepared.selectedNoteIdx);
  TEST_ASSERT_EQUAL(static_cast<int>(SelectionResult::Progress), static_cast<int>(
      editManager.beginDefaultNoteEditSelectionPreparation(track, 1400)));
  prepared = drainSelectionPreparation(track);
  TEST_ASSERT_EQUAL_UINT32(2u, prepared.inventoryCount);
  TEST_ASSERT_EQUAL_UINT32(777u, prepared.primaryNote);
  TEST_ASSERT_EQUAL_UINT32(1400u, prepared.selectedTick);
  TEST_ASSERT_EQUAL_INT(1, prepared.selectedNoteIdx);
}
