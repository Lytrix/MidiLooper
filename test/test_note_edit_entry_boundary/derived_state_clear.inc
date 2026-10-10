struct NoteEditDerivedStateClearTestAccess {
  static void scheduleWarm(EditManager& manager) {
    manager.scheduleKindBoundaryUndoWarm(); // existing producer admission; no injected payload
  }
  static size_t cacheBuffers(const EditManager& manager) {
    return (manager.noteEditFocusMaterializedLoopEvents_.capacity() ? 1u : 0u) +
        (manager.noteEditPaintDisplayCacheNotes_.capacity() ? 1u : 0u) +
        (manager.noteEditCommittedDisplayCacheNotes_.capacity() ? 1u : 0u) +
        (manager.noteEditSelectableDisplayCacheNotes_.capacity() ? 1u : 0u);
  }
  static size_t kindPayload(const EditManager& manager) {
    const auto& entry = manager.kindBoundaryUndoCache_;
    const auto mapUnits = [](const auto& map) {
      return map.size() + (map.bucket_count() > 2 ? 1u : 0u);
    };
    size_t units = mapUnits(entry.focus.baselineMap) + mapUnits(entry.focus.overlapNotes) +
        mapUnits(entry.redoFocus.baselineMap) + mapUnits(entry.redoFocus.overlapNotes) +
        mapUnits(entry.undoCurrentState.rows()) + mapUnits(entry.redoCurrentState.rows());
    units += entry.editRows.size() + entry.redoEditRows.size() +
        entry.selection.selectedNotes.size() + entry.redoSelection.selectedNotes.size() +
        entry.editPassIdsAtPush.size() + entry.redoEditPassIds.size();
    for (const auto& row : entry.editRows) units += row.addedEvents.size();
    for (const auto& row : entry.redoEditRows) units += row.addedEvents.size();
    return units;
  }
  static bool warmValid(const EditManager& manager) { return manager.kindBoundaryUndoCacheValid_; }
  static size_t cacheValues(const EditManager& manager) {
    return manager.noteEditFocusMaterializedLoopEvents_.size() +
        manager.noteEditPaintDisplayCacheNotes_.size() +
        manager.noteEditCommittedDisplayCacheNotes_.size() +
        manager.noteEditSelectableDisplayCacheNotes_.size();
  }
  static bool cleared(const EditManager& manager) {
    const auto& entry = manager.kindBoundaryUndoCache_;
    return cacheBuffers(manager) == 0 && kindPayload(manager) == 0 &&
        manager.sessionState.selection.selectedNotes.capacity() == 0 &&
        manager.selectionChangePrior_.selectedNotes.capacity() == 0 &&
        entry.editRows.capacity() == 0 && entry.redoEditRows.capacity() == 0 &&
        entry.selection.selectedNotes.capacity() == 0 &&
        !entry.hasUndoCurrentState && !entry.hasRedoCurrentState && !entry.hasRedoPayload;
  }
};

static Track& makeDerivedStatePayload() {
  Track& track = makeSelectionSource(4);
  editManager.openNoteEditSession(track);
  const auto notes = editManager.noteEditCommittedDisplayNotes(track);
  TEST_ASSERT_EQUAL(4u, notes.size());
  editManager.getEditSession().noteEditCurrentState.ensureVisibleRowsForDisplayNotes(notes);
  editManager.applySelectNav(track, notes[0].startTick, notes[0].noteId, false, false);
  TEST_ASSERT_TRUE(editManager.pushSessionUndoOnKindChange(track, NoteEditKind::Move));
  editManager.applySelectNav(track, notes[1].startTick, notes[1].noteId, false, false);
  NoteEditDerivedStateClearTestAccess::scheduleWarm(editManager);
  nativeMillisAdvance(80);
  editManager.processKindBoundaryUndoWarm(track);
  TEST_ASSERT_TRUE(NoteEditDerivedStateClearTestAccess::warmValid(editManager));
  editManager.materializedLoopEventsForNoteEditFocus(track);
  editManager.projectedNoteEditDisplayNotes(track);
  TEST_ASSERT_EQUAL(4u, NoteEditDerivedStateClearTestAccess::cacheBuffers(editManager));
  TEST_ASSERT_EQUAL(1u, editManager.getNoteEditSessionState().selection.selectedNotes.size());
  TEST_ASSERT_EQUAL(1u, editManager.selectionChangePrior().selectedNotes.size());
  return track;
}

static void test_derived_state_clear_production_payload_baseline() {
  Track& track = makeDerivedStatePayload();
  TEST_ASSERT_EQUAL(6u, NoteEditDerivedStateClearTestAccess::kindPayload(editManager));
  editManager.setState(nullptr, track);
  editManager.cycleEditSession(track);
}

using DerivedClearResult = EditManager::DerivedStateClearResult;

static EditSession::PayloadClearReceipt completeDerivedSessionPayload(EditManager& manager,
                                                                     Track& track) {
  manager.setState(nullptr, track); // real FSM departure, outside cleanup grants
  auto& session = manager.getEditSession();
  const auto receipt = session.beginPayloadClear();
  for (unsigned grant = 0; grant < 4096; ++grant) {
    if (session.stepPayloadClear(receipt) == EditSession::PayloadClearResult::Complete)
      return receipt;
  }
  TEST_FAIL_MESSAGE("session payload prerequisite exceeded4096 grants");
  return {};
}

static size_t drainDerivedState(EditManager& manager, EditManager::DerivedStateClear& cursor) {
  for (size_t grant = 1; grant <= 4096; ++grant) {
    const auto result = manager.stepNoteEditDerivedStateClear(cursor);
    if (result == DerivedClearResult::Complete) return grant;
    TEST_ASSERT_TRUE(result == DerivedClearResult::Progress);
  }
  TEST_FAIL_MESSAGE("derived state clear exceeded4096 grants");
  return 0;
}

static void test_derived_state_clear_withheld_exact_grants_and_preserved_source() {
  Track& track = makeDerivedStatePayload();
  const auto receipt = completeDerivedSessionPayload(editManager, track);
  auto root = track.getLoop(0).publishedSnapshot();
  const auto revision = track.getLoop(0).playbackRevision;
  const auto owners = root.use_count();
  const auto chunks = LoopEventStore::usedChunkCount();
  const auto chunkIds = track.getLoop(0).passes.recordPass.committedChunkIds;
  std::vector<uint16_t> chunkOwners;
  for (auto id : chunkIds) chunkOwners.push_back(LoopEventStore::chunkReferenceCount(id));
  resetOutboundObservation();
  EditManager::DerivedStateClear cursor;
  TEST_ASSERT_TRUE(editManager.beginNoteEditDerivedStateClear(track, receipt, cursor) ==
                   DerivedClearResult::Progress);
  for (unsigned query = 0; query < 8; ++query) {
    TEST_ASSERT_TRUE(editManager.noteEditDerivedStateClearResult(cursor) == DerivedClearResult::Progress);
    TEST_ASSERT_TRUE(editManager.beginNoteEditDerivedStateClear(track, receipt, cursor) ==
                     DerivedClearResult::Progress);
    TEST_ASSERT_EQUAL(20u, NoteEditDerivedStateClearTestAccess::cacheValues(editManager));
    TEST_ASSERT_EQUAL(4u, NoteEditDerivedStateClearTestAccess::cacheBuffers(editManager));
    TEST_ASSERT_EQUAL(6u, NoteEditDerivedStateClearTestAccess::kindPayload(editManager));
    TEST_ASSERT_EQUAL(1u, editManager.getNoteEditSessionState().selection.selectedNotes.size());
    TEST_ASSERT_EQUAL(1u, editManager.selectionChangePrior().selectedNotes.size());
  }
  TEST_ASSERT_EQUAL(kInvalidNoteId, editManager.getNoteEditSessionState().selection.primaryNote);
  TEST_ASSERT_FALSE(NoteEditDerivedStateClearTestAccess::warmValid(editManager));
  for (unsigned grant = 1; grant <= 23; ++grant) {
    const auto kindBefore = NoteEditDerivedStateClearTestAccess::kindPayload(editManager);
    const auto result = editManager.stepNoteEditDerivedStateClear(cursor);
    const auto kindAfter = NoteEditDerivedStateClearTestAccess::kindPayload(editManager);
    TEST_ASSERT_TRUE(kindAfter == kindBefore || kindAfter + 1 == kindBefore);
    if (grant <= 8) {
      TEST_ASSERT_EQUAL(4 - (grant + 1) / 2,
                       NoteEditDerivedStateClearTestAccess::cacheBuffers(editManager));
      TEST_ASSERT_EQUAL(6u, kindAfter);
    }
    TEST_ASSERT_TRUE(result == (grant == 23 ? DerivedClearResult::Complete : DerivedClearResult::Progress));
    TEST_ASSERT_TRUE(track.getLoop(0).publishedSnapshotIdentity() == root.get());
    TEST_ASSERT_EQUAL(revision, track.getLoop(0).playbackRevision);
    TEST_ASSERT_EQUAL(owners, root.use_count());
    TEST_ASSERT_EQUAL(chunks, LoopEventStore::usedChunkCount());
    for (size_t index = 0; index < chunkIds.size(); ++index)
      TEST_ASSERT_EQUAL(chunkOwners[index], LoopEventStore::chunkReferenceCount(chunkIds[index]));
    NoteUtils::DisplayNoteVec observed;
    LoopContentResolution::resolveDisplayNotes(*root, observed); // native observer, not a grant
    TEST_ASSERT_EQUAL(4u, observed.size());
    for (size_t row = 0; row < 4; ++row) {
      TEST_ASSERT_EQUAL(2000 + row, observed[row].noteId);
      TEST_ASSERT_EQUAL(48, observed[row].note);
      TEST_ASSERT_EQUAL(row * 768, observed[row].startTick);
      TEST_ASSERT_EQUAL(row * 768 + 48, observed[row].endTick);
    }
    TEST_ASSERT_EQUAL(0u, outboundObservation().requestCalls);
  }
  TEST_ASSERT_TRUE(NoteEditDerivedStateClearTestAccess::cleared(editManager));
  TEST_ASSERT_TRUE(editManager.stepNoteEditDerivedStateClear(cursor) == DerivedClearResult::Complete);
  TEST_ASSERT_TRUE(editManager.noteEditDerivedStateClearResult(cursor) == DerivedClearResult::Complete);
  editManager.sendEditSessionChange(EditSessionType::Loop, false);
}

static void test_derived_state_clear_admission_requires_completed_receipt_and_departed_fsm() {
  Track& track = makeDerivedStatePayload();
  EditManager::DerivedStateClear cursor;
  TEST_ASSERT_TRUE(editManager.beginNoteEditDerivedStateClear(track, {}, cursor) ==
                   DerivedClearResult::Superseded);
  const auto receipt = editManager.getEditSession().beginPayloadClear();
  TEST_ASSERT_TRUE(editManager.beginNoteEditDerivedStateClear(track, receipt, cursor) ==
                   DerivedClearResult::Unavailable);
  for (unsigned grant = 0; grant < 4096; ++grant)
    if (editManager.getEditSession().stepPayloadClear(receipt) == EditSession::PayloadClearResult::Complete)
      break;
  TEST_ASSERT_TRUE(editManager.getEditSession().payloadClearResult(receipt) ==
                   EditSession::PayloadClearResult::Complete);
  TEST_ASSERT_TRUE(editManager.beginNoteEditDerivedStateClear(track, receipt, cursor) ==
                   DerivedClearResult::Unavailable); // Select FSM still owns its boundary
  TEST_ASSERT_EQUAL(20u, NoteEditDerivedStateClearTestAccess::cacheValues(editManager));
  editManager.setState(nullptr, track);
  TEST_ASSERT_TRUE(editManager.beginNoteEditDerivedStateClear(track, receipt, cursor) ==
                   DerivedClearResult::Progress);
  TEST_ASSERT_EQUAL(23u, drainDerivedState(editManager, cursor));
  editManager.sendEditSessionChange(EditSessionType::Loop, false);
}

static void test_derived_state_clear_refocus_and_slot_supersession_are_sticky() {
  Track& track = makeDerivedStatePayload();
  const auto receipt = completeDerivedSessionPayload(editManager, track);
  for (unsigned change = 0; change < 2; ++change) {
    EditManager::DerivedStateClear cursor;
    TEST_ASSERT_TRUE(editManager.beginNoteEditDerivedStateClear(track, receipt, cursor) ==
                     DerivedClearResult::Progress);
    const auto values = NoteEditDerivedStateClearTestAccess::cacheValues(editManager);
    if (change == 0) trackManager.setSelectedTrack(1);
    else SlotAccess::focus(0, 1);
    TEST_ASSERT_TRUE(editManager.noteEditDerivedStateClearResult(cursor) == DerivedClearResult::Superseded);
    TEST_ASSERT_TRUE(editManager.stepNoteEditDerivedStateClear(cursor) == DerivedClearResult::Superseded);
    TEST_ASSERT_EQUAL(values, NoteEditDerivedStateClearTestAccess::cacheValues(editManager));
    SlotAccess::focus(0, 0);
    if (change == 0) trackManager.setSelectedTrack(0);
    TEST_ASSERT_TRUE(editManager.stepNoteEditDerivedStateClear(cursor) == DerivedClearResult::Superseded);
    TEST_ASSERT_EQUAL(values, NoteEditDerivedStateClearTestAccess::cacheValues(editManager));
  }
  EditManager::DerivedStateClear fresh;
  TEST_ASSERT_TRUE(editManager.beginNoteEditDerivedStateClear(track, receipt, fresh) == DerivedClearResult::Progress);
  drainDerivedState(editManager, fresh);
  editManager.sendEditSessionChange(EditSessionType::Loop, false);
}

static void test_derived_state_clear_legacy_reset_reopen_rejects_old_cursor() {
  Track& track = makeDerivedStatePayload();
  const auto receipt = completeDerivedSessionPayload(editManager, track);
  EditManager::DerivedStateClear old;
  TEST_ASSERT_TRUE(editManager.beginNoteEditDerivedStateClear(track, receipt, old) == DerivedClearResult::Progress);
  editManager.resetNoteEditSessionState();
  TEST_ASSERT_TRUE(editManager.getEditSession().payloadClearResult(receipt) ==
                   EditSession::PayloadClearResult::Superseded);
  editManager.reopenNoteEditSession(track);
  editManager.materializedLoopEventsForNoteEditFocus(track);
  editManager.projectedNoteEditDisplayNotes(track);
  const auto freshReceipt = completeDerivedSessionPayload(editManager, track);
  const auto values = NoteEditDerivedStateClearTestAccess::cacheValues(editManager);
  const auto selectedIds = editManager.getNoteEditSessionState().selection.selectedNotes;
  TEST_ASSERT_TRUE(editManager.stepNoteEditDerivedStateClear(old) == DerivedClearResult::Superseded);
  TEST_ASSERT_EQUAL(values, NoteEditDerivedStateClearTestAccess::cacheValues(editManager));
  TEST_ASSERT_TRUE(selectedIds == editManager.getNoteEditSessionState().selection.selectedNotes);
  EditManager::DerivedStateClear fresh;
  TEST_ASSERT_TRUE(editManager.beginNoteEditDerivedStateClear(track, freshReceipt, fresh) == DerivedClearResult::Progress);
  drainDerivedState(editManager, fresh);
  editManager.sendEditSessionChange(EditSessionType::Loop, false);
}

static void test_derived_state_clear_empty_local_owner_finishes_without_materializing() {
  EditManager local;
  Track& track = trackManager.getSelectedTrack();
  const auto receipt = completeDerivedSessionPayload(local, track);
  EditManager::DerivedStateClear cursor;
  TEST_ASSERT_TRUE(editManager.beginNoteEditDerivedStateClear(track, receipt, cursor) ==
                   DerivedClearResult::Superseded); // foreign session owner
  TEST_ASSERT_TRUE(local.beginNoteEditDerivedStateClear(track, receipt, cursor) == DerivedClearResult::Progress);
  TEST_ASSERT_EQUAL(9u, drainDerivedState(local, cursor));
  TEST_ASSERT_TRUE(NoteEditDerivedStateClearTestAccess::cleared(local));
  TEST_ASSERT_FALSE(local.getEditSession().store.hasEventsCacheForTest());
  TEST_ASSERT_TRUE(editManager.stepNoteEditDerivedStateClear(cursor) == DerivedClearResult::Superseded);
  TEST_ASSERT_TRUE(local.stepNoteEditDerivedStateClear(cursor) == DerivedClearResult::Complete);
  editManager.sendEditSessionChange(EditSessionType::Loop, false);
}
