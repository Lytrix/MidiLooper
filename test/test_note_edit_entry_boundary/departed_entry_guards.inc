// Direct production entry guards; no copied entry implementation.
static Track& makeDepartedEntryTargets(bool allocateDeparted = true) {
  if (allocateDeparted) commitLoop(0, 0, kShortLoopTicks, 4, 60);
  commitLoop(1, 1, kShortLoopTicks, 4, 70);
  Track& current = trackManager.getTrack(1);
  prewarmSlot(current, 1, 0);
  SlotAccess::focus(1, 1);
  trackManager.setSelectedTrack(1);
  return current;
}

static void assertCurrentEntryProjection() {
  const auto& events = sessionProjection();
  TEST_ASSERT_EQUAL_UINT32(8, events.size());
  for (unsigned note = 0; note < 4; ++note) {
    unsigned ons = 0, offs = 0;
    const uint32_t onTick = (kShortLoopTicks / 5) * (note + 1);
    for (const auto& event : events) {
      if (event.data.noteData.note != 70 + note) continue;
      if (event.isNoteOn()) {
        ++ons;
        TEST_ASSERT_EQUAL_UINT32(onTick, event.tick);
        TEST_ASSERT_EQUAL_UINT8(100, event.data.noteData.velocity);
      } else {
        TEST_ASSERT_TRUE(event.isNoteOff());
        ++offs;
        TEST_ASSERT_EQUAL_UINT32(onTick + 48, event.tick);
      }
      TEST_ASSERT_EQUAL_UINT8(kChannel, event.channel);
    }
    TEST_ASSERT_EQUAL_UINT32(1, ons);
    TEST_ASSERT_EQUAL_UINT32(1, offs);
  }
}

static void test_departed_direct_open_preserves_inactive_selected_target() {
  LoopEventStore::initPool();
  Track& current = makeDepartedEntryTargets();
  Track& departed = trackManager.getTrack(0);
  const auto type = editManager.getEditSessionType();
  const auto selection = editManager.getNoteEditSessionState().selection;
  auto* state = editManager.getCurrentState();
  const auto preview = editManager.sessionPreviewRevision();
  const auto playbackPreview = editManager.sessionPlaybackPreviewRevision();
  const auto chunks = LoopEventStore::usedChunkCount();
  const auto currentRevision = current.getLoop(1).playbackRevision;
  const auto departedRevision = departed.getLoop(0).playbackRevision;
  const auto requests = outboundObservation().requestCalls;
  editManager.openNoteEditSession(departed);
  TEST_ASSERT_FALSE_MESSAGE(editManager.isNoteEditActive(), "departed open must not activate current session");
  TEST_ASSERT_EQUAL(static_cast<int>(type), static_cast<int>(editManager.getEditSessionType()));
  TEST_ASSERT_EQUAL_UINT32(selection.primaryNote, editManager.getNoteEditSessionState().selection.primaryNote);
  TEST_ASSERT_EQUAL_UINT32(selection.selectedTick, editManager.getSelectedTick());
  TEST_ASSERT_EQUAL_PTR(state, editManager.getCurrentState());
  TEST_ASSERT_EQUAL_UINT32(preview, editManager.sessionPreviewRevision());
  TEST_ASSERT_EQUAL_UINT32(playbackPreview, editManager.sessionPlaybackPreviewRevision());
  TEST_ASSERT_EQUAL_UINT16(chunks, LoopEventStore::usedChunkCount());
  TEST_ASSERT_EQUAL_UINT32(currentRevision, current.getLoop(1).playbackRevision);
  TEST_ASSERT_EQUAL_UINT32(departedRevision, departed.getLoop(0).playbackRevision);
  TEST_ASSERT_EQUAL_UINT32(requests, outboundObservation().requestCalls);
  editManager.openNoteEditSession(current); // rejection must not poison a valid direct entry
  TEST_ASSERT_TRUE(editManager.isNoteEditActive());
  TEST_ASSERT_EQUAL_UINT32(1, editManager.getNoteEditSessionState().selection.trackId);
  TEST_ASSERT_EQUAL_UINT32(current.getLoop(1).loopId, editManager.getNoteEditSessionState().selection.loopId);
  assertCurrentEntryProjection();
}

static void test_departed_default_selection_preserves_active_current_identity() {
  LoopEventStore::initPool();
  Track& current = makeDepartedEntryTargets();
  Track& departed = trackManager.getTrack(0);
  pressEditModeButton();
  TEST_ASSERT_TRUE(editManager.isNoteEditActive());
  assertCurrentEntryProjection();
  const auto selection = editManager.getNoteEditSessionState().selection;
  const auto index = editManager.getSelectedNoteIdx();
  auto* state = editManager.getCurrentState();
  const auto reference = editManager.getReferenceStep();
  const auto preview = editManager.sessionPreviewRevision();
  const auto playbackPreview = editManager.sessionPlaybackPreviewRevision();
  const auto requests = outboundObservation().requestCalls;
  editManager.enterDefaultNoteEditSessionState(departed, kShortLoopTicks / 2);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(current.getLoop(1).loopId,
      editManager.getNoteEditSessionState().selection.loopId, "departed default entry must not publish mixed identity");
  TEST_ASSERT_EQUAL_UINT32(1, editManager.getNoteEditSessionState().selection.trackId);
  TEST_ASSERT_EQUAL_UINT32(selection.primaryNote, editManager.getNoteEditSessionState().selection.primaryNote);
  TEST_ASSERT_EQUAL_UINT32(selection.selectedTick, editManager.getSelectedTick());
  TEST_ASSERT_EQUAL_INT(index, editManager.getSelectedNoteIdx());
  TEST_ASSERT_EQUAL_PTR(state, editManager.getCurrentState());
  TEST_ASSERT_EQUAL_UINT32(reference, editManager.getReferenceStep());
  TEST_ASSERT_EQUAL_UINT32(preview, editManager.sessionPreviewRevision());
  TEST_ASSERT_EQUAL_UINT32(playbackPreview, editManager.sessionPlaybackPreviewRevision());
  TEST_ASSERT_EQUAL_UINT32(requests, outboundObservation().requestCalls);
  assertCurrentEntryProjection();
}

static void test_departed_direct_open_does_not_allocate_cold_track() {
  LoopEventStore::initPool();
  Track& current = makeDepartedEntryTargets(false);
  Track& departed = trackManager.getTrack(0);
  TEST_ASSERT_FALSE(departed.loopsAllocated());
  const auto chunks = LoopEventStore::usedChunkCount();
  editManager.openNoteEditSession(departed);
  TEST_ASSERT_FALSE(departed.loopsAllocated());
  TEST_ASSERT_FALSE(editManager.isNoteEditActive());
  TEST_ASSERT_EQUAL_UINT16(chunks, LoopEventStore::usedChunkCount());
  editManager.openNoteEditSession(current);
  TEST_ASSERT_TRUE(editManager.isNoteEditActive());
  TEST_ASSERT_EQUAL_UINT32(current.getLoop(1).loopId, editManager.getNoteEditSessionState().selection.loopId);
  assertCurrentEntryProjection();
}

static void test_departed_default_selection_does_not_read_cold_track() {
  LoopEventStore::initPool();
  Track& current = makeDepartedEntryTargets(false);
  Track& departed = trackManager.getTrack(2); // untouched cold peer, not setup's prior focus
  editManager.openNoteEditSession(current);
  TEST_ASSERT_TRUE(editManager.isNoteEditActive());
  TEST_ASSERT_FALSE(departed.loopsAllocated());
  const auto selection = editManager.getNoteEditSessionState().selection;
  const auto index = editManager.getSelectedNoteIdx();
  const auto requests = outboundObservation().requestCalls;
  editManager.enterDefaultNoteEditSessionState(departed, kShortLoopTicks / 2);
  TEST_ASSERT_FALSE(departed.loopsAllocated());
  TEST_ASSERT_EQUAL_UINT32(current.getLoop(1).loopId, editManager.getNoteEditSessionState().selection.loopId);
  TEST_ASSERT_EQUAL_UINT32(selection.primaryNote, editManager.getNoteEditSessionState().selection.primaryNote);
  TEST_ASSERT_EQUAL_UINT32(selection.selectedTick, editManager.getSelectedTick());
  TEST_ASSERT_EQUAL_INT(index, editManager.getSelectedNoteIdx());
  TEST_ASSERT_EQUAL_UINT32(requests, outboundObservation().requestCalls);
  assertCurrentEntryProjection();
}
