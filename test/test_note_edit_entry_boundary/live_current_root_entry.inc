// LIVE entry lifecycle: cold setup drives the real committed Snapshot publisher.
static Track& liveReadySource(unsigned notes = 2, uint32_t tick = 0) {
  commitLoop(0, 0, kShortLoopTicks, notes);
  Track& track = trackManager.getTrack(0);
  Loop& loop = track.getLoop(0);
  loop.discardCapture();
  loop.clearPendingSnapshotPublishForTest(); // STOPPED fixture setup only.
  TEST_ASSERT_TRUE(loop.queueLoadedSnapshotPublish());
  for (unsigned i = 0; loop.hasPendingSnapshotPublish() && i < 10000; ++i)
    loop.stepPendingSnapshotPublish(1);
  TEST_ASSERT_FALSE(loop.hasPendingSnapshotPublish());
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_NOT_EQUAL(Loop::PendingSnapshotInventoryDestinationResult::Busy,
                       loop.pendingSnapshotInventoryDestinationResult());
  readySlotPlaying(track, 0, tick);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  return track;
}

static void test_live_ready_root_zero_grant_acceptance() {
  LoopEventStore::initPool();
  Track& track = liveReadySource();
  std::printf("LIVE fixed native continuation=%zu bytes, EditManager=%zu bytes\n",
              editManager.currentRootEntryBytesForTest(), sizeof(EditManager));
  const auto* root = track.getLoop(0).publishedSnapshotIdentity();
  pressEditModeButton();
  TEST_ASSERT_FALSE_MESSAGE(editManager.isNoteEditActive(),
                           "REAL settled publisher root must wait for an owner grant");
  TEST_ASSERT_NULL(editManager.getCurrentState());
  TEST_ASSERT_EQUAL(EditSessionType::Loop, editManager.getEditSessionType());
  TEST_ASSERT_EQUAL_UINT32(0, outboundObservation().requestCalls);
  TEST_ASSERT_EQUAL_PTR(root, track.getLoop(0).publishedSnapshotIdentity());
}

using LivePhase = EditManager::CurrentRootEntryPhase;
static void liveGrant() {
  midiHandler.handleMidiInput();
  editManager.stepCurrentRootNoteEditEntry();
  midiHandler.handleMidiInput();
}
static void liveReach(LivePhase phase) {
  for (unsigned i = 0; i < 100000 && editManager.currentRootNoteEditEntryPhase() != phase; ++i) {
    TEST_ASSERT_TRUE(editManager.hasPendingCurrentRootNoteEditEntry());
    liveGrant();
  }
  TEST_ASSERT_EQUAL(phase, editManager.currentRootNoteEditEntryPhase());
}
static void liveNoPublication() {
  TEST_ASSERT_FALSE(editManager.isNoteEditActive());
  TEST_ASSERT_NULL(editManager.getCurrentState());
  TEST_ASSERT_EQUAL(EditSessionType::Loop, editManager.getEditSessionType());
  TEST_ASSERT_EQUAL_UINT32(0, outboundObservation().requestCalls);
}

struct LiveFeedback : EditEventListener {
  std::vector<EditEvent> events;
  bool changeOnClose = false;
  bool restartOnClose = false;
  void onEditEvent(EditEvent event) override {
    events.push_back(event);
    if (event == EditEvent::SessionOpened || event == EditEvent::SessionClosed) {
      TEST_ASSERT_TRUE(editManager.isNoteEditActive());
      TEST_ASSERT_EQUAL_PTR(editManager.getSelectNoteState(), editManager.getCurrentState());
      const auto& s = editManager.getNoteEditSessionState().selection;
      TEST_ASSERT_EQUAL_UINT32(editManager.getLastFader1SelectNoteId(), s.primaryNote);
      TEST_ASSERT_EQUAL_UINT32(1, editManager.sessionPreviewRevision());
      TEST_ASSERT_EQUAL_UINT32(1, editManager.sessionPlaybackPreviewRevision());
      TEST_ASSERT_EQUAL_UINT32(s.primaryNote == kInvalidNoteId ? 0 : 1, s.selectedNotes.size());
    }
    controlSurfaceManager.onEditEvent(event); // real feedback request seam
    if (changeOnClose && event == EditEvent::SessionClosed)
      ++trackManager.getSelectedLoop(trackManager.getSelectedTrack()).playbackRevision;
    if (restartOnClose && event == EditEvent::SessionClosed) {
      restartOnClose = false;
      Track& track = trackManager.getSelectedTrack();
      editManager.closeNoteEditSession(track);
      editManager.setState(nullptr, track);
      TEST_ASSERT_EQUAL(EditManager::CurrentRootEntryAdmission::Accepted,
                       editManager.acceptCurrentRootNoteEditEntry(track));
    }
  }
};

static void test_live_one_grant_private_selection_and_exact_completion() {
  LoopEventStore::initPool();
  Track& track = liveReadySource(2, 50);
  LiveFeedback feedback;
  editManager.setEditEventListener(&feedback);
  pressEditModeButton();
  TEST_ASSERT_EQUAL(LivePhase::BeginPayload, editManager.currentRootNoteEditEntryPhase());
  liveGrant();
  TEST_ASSERT_EQUAL(LivePhase::Payload, editManager.currentRootNoteEditEntryPhase());
  liveNoPublication();
  clockManager.assignCurrentTickSilently(1200); // original acceptance tick, not completion tick
  liveReach(LivePhase::Publish);
  liveNoPublication();
  const auto* prepared = editManager.preparedDefaultNoteEditSelection(track);
  TEST_ASSERT_NOT_NULL(prepared);
  TEST_ASSERT_EQUAL_UINT32(512, prepared->selectedTick);
  TEST_ASSERT_EQUAL_UINT32(2, prepared->inventoryCount);
  const auto id = prepared->primaryNote;
  const auto before = editManager.getSelectNoteState()->entryReselectionsForTest();
  liveGrant();
  TEST_ASSERT_EQUAL(LivePhase::PreviewTail, editManager.currentRootNoteEditEntryPhase());
  TEST_ASSERT_TRUE(editManager.isNoteEditActive());
  TEST_ASSERT_EQUAL_UINT32(id, editManager.getNoteEditSessionState().selection.primaryNote);
  TEST_ASSERT_EQUAL_UINT32(512, editManager.getSelectedTick());
  TEST_ASSERT_EQUAL_INT(0, editManager.getSelectedNoteIdx());
  TEST_ASSERT_EQUAL_UINT32(10, editManager.getReferenceStep());
  TEST_ASSERT_EQUAL_UINT32(before, editManager.getSelectNoteState()->entryReselectionsForTest());
  TEST_ASSERT_FALSE(editManager.preparedSelectionPublicationActive());
  TEST_ASSERT_FALSE(editManager.getSelectNoteState()->lastCountMutatedCacheForTest());
  TEST_ASSERT_EQUAL_UINT32(0, outboundObservation().requestCalls);
  liveGrant();
  TEST_ASSERT_EQUAL(LivePhase::FeedbackTail, editManager.currentRootNoteEditEntryPhase());
  TEST_ASSERT_TRUE(feedback.events.empty());
  liveGrant();
  TEST_ASSERT_EQUAL(LivePhase::Complete, editManager.currentRootNoteEditEntryPhase());
  TEST_ASSERT_EQUAL_UINT32(2, feedback.events.size());
  TEST_ASSERT_EQUAL(EditEvent::SessionClosed, feedback.events[0]);
  TEST_ASSERT_EQUAL(EditEvent::SessionOpened, feedback.events[1]);
  TEST_ASSERT_TRUE(editManager.sessionOpenedIncludesMidi());
  TEST_ASSERT_TRUE(editManager.sessionOpenedIncludesFaderFeedback());
  TEST_ASSERT_EQUAL_UINT32(1, outboundObservation().requestCalls);
  TEST_ASSERT_EQUAL(NoteEditFaderOutbound::Trigger::SessionOpen, outboundObservation().lastTrigger);
  const auto& plan = outboundObservation().lastPlan;
  TEST_ASSERT_TRUE(plan.fader1); TEST_ASSERT_TRUE(plan.waitFader1Echo);
  TEST_ASSERT_TRUE(plan.coarse); TEST_ASSERT_TRUE(plan.fine); TEST_ASSERT_TRUE(plan.noteValue);
  TEST_ASSERT_EQUAL_UINT32(0, outboundObservation().nestedDrainCalls);
  liveGrant();
  TEST_ASSERT_EQUAL_UINT32(1, outboundObservation().requestCalls);
  editManager.setEditEventListener(&controlSurfaceManager);
}

static void test_live_short_wrapper_and_pending_inputs_do_not_run_immediate_tails() {
  LoopEventStore::initPool();
  Track& track = liveReadySource();
  controlSurfaceManager.handleCycleNoteEditType(track);
  TEST_ASSERT_TRUE(editManager.hasPendingCurrentRootNoteEditEntry());
  liveGrant();
  controlSurfaceManager.handleCreateNoteAtBracket(track);
  controlSurfaceManager.handleDeleteSelectedNote(track);
  controlSurfaceManager.handleDeleteOrCreateNoteAtBracket(track);
  editManager.setState(editManager.getStartNoteState(), track, 100);
  editManager.applyCycleEditKind(track);
  editManager.cycleNoteEditType(track);
  editManager.onEncoderTurn(track, 1);
  TEST_ASSERT_FALSE(editManager.beginGeometryMutation(track, NoteEditKind::Move, true));
  liveNoPublication();
  TEST_ASSERT_EQUAL(LivePhase::Payload, editManager.currentRootNoteEditEntryPhase());
  finishLiveEntry();
  TEST_ASSERT_TRUE(editManager.isNoteEditActive());
  TEST_ASSERT_EQUAL_UINT32(1, outboundObservation().requestCalls);
  TEST_ASSERT_EQUAL(NoteEditKind::Select, editManager.getNoteEditSessionState().kind);
}

static void test_live_pending_second_press_cancels_without_fake_session_events() {
  for (unsigned shortPress = 0; shortPress < 2; ++shortPress) {
    setUpHarness(); LoopEventStore::initPool(); Track& track = liveReadySource();
    pressEditModeButton(); liveGrant();
    if (shortPress) controlSurfaceManager.handleCycleNoteEditType(track); else pressEditModeButton();
    TEST_ASSERT_EQUAL(LivePhase::Cancelled, editManager.currentRootNoteEditEntryPhase());
    liveGrant(); liveNoPublication();
    pressEditModeButton(); finishLiveEntry();
    TEST_ASSERT_TRUE(editManager.isNoteEditActive());
    TEST_ASSERT_EQUAL_UINT32(1, outboundObservation().requestCalls);
  }
}

static void test_live_every_prepublication_phase_target_slot_and_source_cancel() {
  const LivePhase phases[] = {LivePhase::BeginPayload, LivePhase::Payload, LivePhase::BeginDerived,
      LivePhase::Derived, LivePhase::BeginIds, LivePhase::Ids, LivePhase::ReserveSelection,
      LivePhase::BeginSelection, LivePhase::Selection, LivePhase::Publish};
  for (auto phase : phases) for (unsigned mutation = 0; mutation < 6; ++mutation) {
    setUpHarness(); LoopEventStore::initPool(); Track& track = liveReadySource();
    pressEditModeButton(); liveReach(phase);
    if (mutation == 0) trackManager.setSelectedTrack(1);
    if (mutation == 1) SlotAccess::focus(0, 1);
    if (mutation == 2) ++track.getLoop(0).playbackRevision;
    if (mutation == 3) StorageManagerInternal::resetLoopSlotToEmpty(track.getLoop(0), 0);
    if (mutation == 4) editManager.rematerializeNoteEditSessionAfterWorkspaceReload(track);
    if (mutation == 5) editManager.resetNoteEditSessionState();
    liveGrant(); liveNoPublication();
    TEST_ASSERT_EQUAL(LivePhase::Cancelled, editManager.currentRootNoteEditEntryPhase());
  }
}

static void test_live_cleanup_reset_clear_reload_and_compatibility_supersede() {
  for (unsigned mutation = 0; mutation < 6; ++mutation) {
    setUpHarness(); LoopEventStore::initPool(); Track& track = liveReadySource();
    pressEditModeButton(); liveReach(LivePhase::Derived);
    if (mutation == 0) editManager.resetNoteEditSessionState();
    if (mutation == 1) track.clear();
    if (mutation == 2) editManager.rematerializeNoteEditSessionAfterWorkspaceReload(track);
    if (mutation == 3) editManager.revertNoteEditSessionForLoopClear(track);
    if (mutation == 4) editManager.closeNoteEditSession(track);
    if (mutation == 5) editManager.exitEditMode(track);
    liveGrant(); liveNoPublication();
    TEST_ASSERT_FALSE(editManager.hasPendingCurrentRootNoteEditEntry());
  }
}

static void test_live_obsolete_feedback_cancelled_after_publication_and_callback() {
  for (unsigned boundary = 0; boundary < 3; ++boundary) {
    setUpHarness(); LoopEventStore::initPool(); Track& track = liveReadySource();
    LiveFeedback feedback;
    editManager.setEditEventListener(&feedback);
    pressEditModeButton(); liveReach(boundary ? LivePhase::FeedbackTail : LivePhase::PreviewTail);
    if (boundary == 2) feedback.changeOnClose = true;
    else ++track.getLoop(0).playbackRevision;
    liveGrant();
    TEST_ASSERT_EQUAL_UINT32(0, outboundObservation().requestCalls);
    TEST_ASSERT_EQUAL_UINT32(boundary == 2 ? 1 : 0, feedback.events.size());
    TEST_ASSERT_FALSE(editManager.hasPendingCurrentRootNoteEditEntry());
    editManager.setEditEventListener(&controlSurfaceManager);
  }
}

static void test_live_dense_preparation_one_unit_and_peer_midi_unchanged() {
  LoopEventStore::initPool(); Track& track = liveReadySource(32);
  commitLoop(1, 0, kShortLoopTicks, 4, 70);
  Track& peer = trackManager.getTrack(1);
  Loop& peerLoop = peer.getLoop(0); peerLoop.discardCapture();
  peerLoop.clearPendingSnapshotPublishForTest();
  TEST_ASSERT_TRUE(peerLoop.queueLoadedSnapshotPublish());
  inventory_adoption_test::publish(peerLoop);
  readySlotPlaying(peer, 0, 0);
  trackManager.setSelectedTrack(0);
  const auto* root = track.getLoop(0).publishedSnapshotIdentity();
  pressEditModeButton(); liveReach(LivePhase::Selection);
  track.reanchorPlaybackProjection(0, false); peer.reanchorPlaybackProjection(0, false);
  track.clearSentPlaybackMidiForTest(); peer.clearSentPlaybackMidiForTest();
  unsigned tick = 0;
  while (editManager.currentRootNoteEditEntryPhase() == LivePhase::Selection) {
    const auto units = editManager.selectionPreparationUnitsForTest();
    liveGrant();
    TEST_ASSERT_EQUAL_UINT32(units + 1, editManager.selectionPreparationUnitsForTest());
    liveNoPublication();
    TEST_ASSERT_EQUAL_PTR(root, track.getLoop(0).publishedSnapshotIdentity());
    if (tick <= 1536) { track.playMidiEvents(tick, true); peer.playMidiEvents(tick, true); tick += 16; }
  }
  TEST_ASSERT_EQUAL_UINT32(64, track.sentPlaybackMidiCountForTest());
  TEST_ASSERT_EQUAL_UINT32(8, peer.sentPlaybackMidiCountForTest());
  for (unsigned i = 0; i < 4; ++i) {
    const auto& on = peer.sentPlaybackMidiAtForTest(i * 2).event;
    const auto& off = peer.sentPlaybackMidiAtForTest(i * 2 + 1).event;
    TEST_ASSERT_EQUAL_UINT32(307 * (i + 1), on.tick);
    TEST_ASSERT_EQUAL_UINT32(307 * (i + 1) + 48, off.tick);
    TEST_ASSERT_TRUE(on.isNoteOn()); TEST_ASSERT_TRUE(off.isNoteOff());
    TEST_ASSERT_EQUAL_UINT8(70 + i, on.data.noteData.note);
  }
  const auto* prepared = editManager.preparedDefaultNoteEditSelection(track);
  TEST_ASSERT_NOT_NULL(prepared); TEST_ASSERT_EQUAL_UINT32(32, prepared->inventoryCount);
  finishLiveEntry(); TEST_ASSERT_EQUAL_UINT32(1, outboundObservation().requestCalls);
}

static void test_live_populated_inactive_payload_and_derived_cleanup_precede_publication() {
  LoopEventStore::initPool(); Track& track = liveReadySource(4);
  editManager.openNoteEditSession(track); // existing synchronous producer, outside entry grants
  const auto notes = editManager.noteEditCommittedDisplayNotes(track);
  TEST_ASSERT_EQUAL_UINT32(4, notes.size());
  editManager.getEditSession().noteEditCurrentState.ensureVisibleRowsForDisplayNotes(notes);
  editManager.applySelectNav(track, notes[0].startTick, notes[0].noteId);
  TEST_ASSERT_TRUE(editManager.pushSessionUndoOnKindChange(track, NoteEditKind::Move));
  editManager.applySelectNav(track, notes[1].startTick, notes[1].noteId);
  NoteEditDerivedStateClearTestAccess::scheduleWarm(editManager);
  nativeMillisAdvance(80); editManager.processKindBoundaryUndoWarm(track);
  editManager.materializedLoopEventsForNoteEditFocus(track);
  editManager.projectedNoteEditDisplayNotes(track);
  // Controlled inactive-owner shape: retain real producer payloads, not an injected root.
  editManager.getEditSession().active = false;
  editManager.getEditSession().sessionType = EditSessionType::Loop;
  editManager.setState(nullptr, track);
  TEST_ASSERT_TRUE(editManager.getEditSession().undoStack.undoCount() > 0);
  TEST_ASSERT_TRUE(NoteEditDerivedStateClearTestAccess::cacheBuffers(editManager) > 0);
  resetOutboundObservation();
  const auto* root = track.getLoop(0).publishedSnapshotIdentity();
  pressEditModeButton();
  TEST_ASSERT_TRUE(editManager.hasPendingCurrentRootNoteEditEntry());
  liveReach(LivePhase::BeginIds);
  liveNoPublication();
  TEST_ASSERT_EQUAL_UINT32(0, editManager.getEditSession().undoStack.undoCount());
  TEST_ASSERT_TRUE(NoteEditDerivedStateClearTestAccess::cleared(editManager));
  TEST_ASSERT_EQUAL_PTR(root, track.getLoop(0).publishedSnapshotIdentity());
  finishLiveEntry();
  TEST_ASSERT_TRUE(editManager.isNoteEditActive());
  TEST_ASSERT_EQUAL_UINT32(1, outboundObservation().requestCalls);
}

static Track& liveLateSource() {
  Track& track = trackManager.getTrack(0); track.ensureLoopsAllocated();
  SlotAccess::select(track, 0); SlotAccess::focus(0, 0);
  Loop& loop = track.getLoop(0); loop.loopLengthTicks = 2 * 768;
  loop.beginCapture(CapturePhase::Record);
  auto on = MidiEvent::NoteOn(700, 1, 60, 100); on.noteId = 900;
  auto off = MidiEvent::NoteOff(720, 1, 60); off.noteId = 900;
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(on)); TEST_ASSERT_TRUE(loop.appendCaptureEvent(off));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(1536, CommitReason::RecordStop));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass()); loop.discardCapture();
  loop.clearPendingSnapshotPublishForTest();
  EditPass add; add.targetNoteId = 910; add.pitch = 50; add.velocity = 100;
  add.actionType = EditActionType::Create; add.startTick = 1500; add.endTick = 1520;
  auto lateOn = MidiEvent::NoteOn(1500, 1, 50, 100); lateOn.noteId = 910;
  auto lateOff = MidiEvent::NoteOff(1520, 1, 50); lateOff.noteId = 910;
  add.addedEvents.push_back(lateOn); add.addedEvents.push_back(lateOff);
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, loop.saveNoteEditPass(0, add, EditPassType::Note, false, true));
  TEST_ASSERT_TRUE(loop.requestCommittedPassesRepublish());
  inventory_adoption_test::publish(loop);
  readySlotPlaying(track, 0, 0);
  return track;
}

static void test_live_real_publisher_second_clamp_final_landing_and_late_cancel() {
  for (unsigned cancel = 0; cancel < 5; ++cancel) {
    setUpHarness(); LoopEventStore::initPool(); Track& track = liveLateSource();
    pressEditModeButton(); liveReach(LivePhase::Selection);
    while (!editManager.selectionPreparationIsReselectingForTest()) {
      TEST_ASSERT_EQUAL(LivePhase::Selection, editManager.currentRootNoteEditEntryPhase());
      liveGrant();
    }
    liveNoPublication();
    if (cancel == 1) { ++track.getLoop(0).canonicalStorageGeneration; liveGrant(); liveNoPublication(); continue; }
    if (cancel == 3) { trackManager.setSelectedTrack(1); liveGrant(); liveNoPublication(); continue; }
    if (cancel == 4) { SlotAccess::focus(0, 1); liveGrant(); liveNoPublication(); continue; }
    liveReach(LivePhase::Publish);
    const auto* prepared = editManager.preparedDefaultNoteEditSelection(track);
    TEST_ASSERT_NOT_NULL(prepared);
    TEST_ASSERT_EQUAL_UINT32(900, prepared->primaryNote);
    TEST_ASSERT_EQUAL_UINT32(700, prepared->selectedTick);
    TEST_ASSERT_EQUAL_INT(0, prepared->selectedNoteIdx);
    TEST_ASSERT_EQUAL_UINT32(2, prepared->inventoryCount);
    if (cancel == 2) { ++track.getLoop(0).canonicalStorageGeneration; liveGrant(); liveNoPublication(); continue; }
    const auto before = editManager.getSelectNoteState()->entryReselectionsForTest();
    liveGrant();
    TEST_ASSERT_EQUAL_UINT32(900, editManager.getLastFader1SelectNoteId());
    TEST_ASSERT_EQUAL_UINT32(700, editManager.getSelectedTick());
    TEST_ASSERT_EQUAL_INT(0, editManager.getSelectedNoteIdx());
    TEST_ASSERT_EQUAL_UINT32(1, editManager.getNoteEditSessionState().selection.selectedNotes.size());
    TEST_ASSERT_EQUAL_UINT32(900, editManager.getNoteEditSessionState().selection.selectedNotes[0]);
    TEST_ASSERT_EQUAL_UINT32(before, editManager.getSelectNoteState()->entryReselectionsForTest());
    finishLiveEntry(); TEST_ASSERT_EQUAL_UINT32(1, outboundObservation().requestCalls);
  }
}

static void test_live_compatibility_admission_is_explicit_and_nonmutating() {
  using Admission = EditManager::CurrentRootEntryAdmission;
  for (unsigned shape = 0; shape < 6; ++shape) {
    setUpHarness(); LoopEventStore::initPool(); Track& track = liveReadySource();
    Loop& loop = track.getLoop(0);
    if (shape == 0) ++loop.playbackRevision; // stale
    if (shape == 1) { loop.beginCapture(CapturePhase::Record); } // unsettled obligation
    if (shape == 2) editManager.openNoteEditSession(track); // active
    if (shape == 3) editManager.setState(editManager.getStartNoteState(), track); // non-null FSM
    if (shape == 4) { track.forceSetState(TRACK_STOPPED); loop.visualCacheDirty = true; }
    if (shape == 5) { loop.resetPassTimeline(); } // rootless
    const auto type = editManager.getEditSessionType(); const bool active = editManager.isNoteEditActive();
    const auto* state = editManager.getCurrentState(); const auto feedback = outboundObservation().requestCalls;
    TEST_ASSERT_EQUAL(Admission::Unavailable, editManager.acceptCurrentRootNoteEditEntry(track));
    TEST_ASSERT_FALSE(editManager.hasPendingCurrentRootNoteEditEntry());
    TEST_ASSERT_EQUAL(type, editManager.getEditSessionType()); TEST_ASSERT_EQUAL(active, editManager.isNoteEditActive());
    TEST_ASSERT_EQUAL_PTR(state, editManager.getCurrentState());
    TEST_ASSERT_EQUAL_UINT32(feedback, outboundObservation().requestCalls);
  }
}

static void test_live_real_storage_reset_before_source_reuse_and_geometry_cancel() {
  for (unsigned mutation = 0; mutation < 3; ++mutation) {
    setUpHarness(); LoopEventStore::initPool(); Track& track = liveReadySource();
    pressEditModeButton(); liveReach(LivePhase::Publish);
    if (mutation == 0) StorageManagerInternal::resetLoopSlotToEmpty(track.getLoop(0), 0);
    if (mutation == 1) loopEditManager.handleLoopLengthInput(10, track);
    if (mutation == 2) loopEditManager.handleLoopStartFaderInput(0, track);
    TEST_ASSERT_FALSE(editManager.hasPendingCurrentRootNoteEditEntry());
    liveGrant(); liveNoPublication();
  }
}

static void test_live_empty_ready_root_and_split_start_tie_inventory() {
  for (unsigned shape = 0; shape < 2; ++shape) {
    setUpHarness(); LoopEventStore::initPool();
    Track& track = trackManager.getTrack(0); track.ensureLoopsAllocated();
    SlotAccess::select(track, 0); SlotAccess::focus(0, 0);
    Loop& loop = track.getLoop(0); loop.loopLengthTicks = 2 * 768;
    loop.beginCapture(CapturePhase::Record);
    if (shape == 0) {
      TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::ControlChange(96, 1, 7, 64)));
    } else {
      // Two starts on one pitch; first hold crosses a bar and yields duplicate memberships.
      const uint32_t starts[] = {100, 200, 100};
      const uint32_t ends[] = {900, 300, 148};
      const NoteId ids[] = {42, 44, 43};
      for (unsigned n = 0; n < 3; ++n) {
        auto on = MidiEvent::NoteOn(starts[n], 1, n == 2 ? 62 : 60, 100); on.noteId = ids[n];
        auto off = MidiEvent::NoteOff(ends[n], 1, n == 2 ? 62 : 60); off.noteId = ids[n];
        TEST_ASSERT_TRUE(loop.appendCaptureEvent(on)); TEST_ASSERT_TRUE(loop.appendCaptureEvent(off));
      }
    }
    TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(1536, CommitReason::RecordStop));
    TEST_ASSERT_TRUE(loop.commitPendingCapturePass()); loop.discardCapture();
    loop.clearPendingSnapshotPublishForTest();
    TEST_ASSERT_TRUE(loop.queueLoadedSnapshotPublish()); inventory_adoption_test::publish(loop);
    if (shape) {
      const auto membership = loop.publishedSnapshot()->noteMembership(60, 0);
      TEST_ASSERT_NOT_NULL(membership.get());
      TEST_ASSERT_EQUAL_UINT32(44, membership->references[0]->noteId);
      TEST_ASSERT_EQUAL_UINT32(42, membership->references[1]->noteId);
    }
    readySlotPlaying(track, 0, 96);
    pressEditModeButton(); liveNoPublication(); liveReach(LivePhase::Publish);
    const auto* prepared = editManager.preparedDefaultNoteEditSelection(track);
    TEST_ASSERT_NOT_NULL(prepared);
    TEST_ASSERT_EQUAL_UINT32(shape ? 3 : 0, prepared->inventoryCount);
    TEST_ASSERT_EQUAL_UINT32(shape ? 42 : kInvalidNoteId, prepared->primaryNote);
    TEST_ASSERT_EQUAL_UINT32(shape ? 100 : 96, prepared->selectedTick);
    TEST_ASSERT_EQUAL_INT(shape ? 1 : -1, prepared->selectedNoteIdx); // real publisher membership order
    finishLiveEntry();
    TEST_ASSERT_EQUAL_UINT32(shape ? 42 : kInvalidNoteId, editManager.getLastFader1SelectNoteId());
    TEST_ASSERT_EQUAL_UINT32(shape ? 1 : 0, editManager.getNoteEditSessionState().selection.selectedNotes.size());
    TEST_ASSERT_EQUAL_UINT32(1, outboundObservation().requestCalls);
  }
}

static void test_live_dedup_and_accepted_capture_input_supersede_without_obsolete_feedback() {
  for (unsigned mutation = 0; mutation < 4; ++mutation) {
    setUpHarness(); LoopEventStore::initPool(); Track& track = liveReadySource(32);
    pressEditModeButton(); liveReach(LivePhase::Selection);
    for (unsigned i = 0; !editManager.selectionPreparationIsDeduplicatingForTest() && i < 100000; ++i) liveGrant();
    TEST_ASSERT_TRUE(editManager.selectionPreparationIsDeduplicatingForTest());
    if (mutation == 0) trackManager.setSelectedTrack(1);
    if (mutation == 1) SlotAccess::focus(0, 1);
    if (mutation == 2) ++track.getLoop(0).canonicalStorageGeneration;
    if (mutation == 3) {
      Loop& loop = track.getLoop(0);
      loop.beginCapture(CapturePhase::Overdub);
      TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(100, 1, 75, 100)));
    }
    liveGrant(); liveNoPublication();
    TEST_ASSERT_EQUAL(LivePhase::Cancelled, editManager.currentRootNoteEditEntryPhase());
    if (mutation == 3) {
      Loop& loop = track.getLoop(0);
      TEST_ASSERT_TRUE(loop.captureActive());
      TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(200, 1, 75)));
      TEST_ASSERT_EQUAL_UINT32(2, loop.capture.store.size()); // accepted musical input not dropped
    }
  }
}

static void test_live_callback_successor_keeps_its_new_request_without_old_open() {
  LoopEventStore::initPool(); Track& track = liveReadySource();
  LiveFeedback feedback; feedback.restartOnClose = true;
  editManager.setEditEventListener(&feedback);
  pressEditModeButton(); liveReach(LivePhase::FeedbackTail); liveGrant();
  TEST_ASSERT_EQUAL_UINT32(1, feedback.events.size());
  TEST_ASSERT_EQUAL(LivePhase::BeginPayload, editManager.currentRootNoteEditEntryPhase());
  liveNoPublication();
  finishLiveEntry();
  TEST_ASSERT_TRUE(editManager.isNoteEditActive());
  TEST_ASSERT_EQUAL_UINT32(3, feedback.events.size());
  TEST_ASSERT_EQUAL(EditEvent::SessionOpened, feedback.events[2]);
  TEST_ASSERT_EQUAL_UINT32(1, outboundObservation().requestCalls);
  editManager.setEditEventListener(&controlSurfaceManager);
}

// Repair oracle: postpublication tails are cancellable, but the published session
// must still receive the user's ordinary operation. Forward the real surface seam.
struct LiveRepairFeedback : EditEventListener {
  struct Row { EditEvent event; EditSessionType type; bool midi, fader; };
  std::vector<Row> rows;
  void onEditEvent(EditEvent event) override {
    rows.push_back({event, editManager.getEditSessionType(),
                   editManager.sessionOpenedIncludesMidi(), editManager.sessionOpenedIncludesFaderFeedback()});
    controlSurfaceManager.onEditEvent(event);
  }
};

static void liveRepairCycleAfterPublication(LivePhase phase) {
    setUpHarness(); LoopEventStore::initPool(); Track& track = liveReadySource();
    LiveRepairFeedback feedback; editManager.setEditEventListener(&feedback);
    pressEditModeButton(); liveReach(phase);
    TEST_ASSERT_TRUE(editManager.isNoteEditActive());
    TEST_ASSERT_EQUAL_PTR(editManager.getSelectNoteState(), editManager.getCurrentState());
    TEST_ASSERT_TRUE(feedback.rows.empty());
    pressEditModeButton();
    TEST_ASSERT_FALSE_MESSAGE(editManager.isNoteEditActive(), "postpublication cycle must close the published Note session");
    TEST_ASSERT_EQUAL(EditSessionType::Loop, editManager.getEditSessionType());
    TEST_ASSERT_NULL(editManager.getCurrentState());
    TEST_ASSERT_EQUAL_INT(-1, editManager.getSelectedNoteIdx());
    TEST_ASSERT_FALSE(editManager.hasPendingCurrentRootNoteEditEntry());
    // Existing cycle closes to Loop before sendEditSessionChange: one Loop-open,
    // not a fabricated deferred Note SessionClosed/Opened pair.
    TEST_ASSERT_EQUAL_UINT32(1, feedback.rows.size());
    TEST_ASSERT_EQUAL(EditEvent::SessionOpened, feedback.rows[0].event);
    TEST_ASSERT_EQUAL(EditSessionType::Loop, feedback.rows[0].type);
    TEST_ASSERT_TRUE(feedback.rows[0].midi); TEST_ASSERT_FALSE(feedback.rows[0].fader);
    TEST_ASSERT_EQUAL_UINT32(0, outboundObservation().requestCalls);
    liveGrant(); liveGrant();
    TEST_ASSERT_EQUAL_UINT32(1, feedback.rows.size());
    TEST_ASSERT_EQUAL_UINT32(0, outboundObservation().nestedDrainCalls);
    editManager.setEditEventListener(&controlSurfaceManager);
    (void)track;
}

static void liveRepairShortAfterPublication(LivePhase phase) {
    setUpHarness(); LoopEventStore::initPool(); Track& track = liveReadySource();
    LiveRepairFeedback feedback; editManager.setEditEventListener(&feedback);
    controlSurfaceManager.handleCycleNoteEditType(track); liveReach(phase);
    const auto id = editManager.getLastFader1SelectNoteId();
    controlSurfaceManager.handleCycleNoteEditType(track);
    TEST_ASSERT_EQUAL_MESSAGE(NoteEditKind::Move, editManager.getNoteEditSessionState().kind,
                              "postpublication short press must perform the ordinary Select-to-Move cycle");
    TEST_ASSERT_TRUE(editManager.isNoteEditActive());
    TEST_ASSERT_EQUAL(EditSessionType::Note, editManager.getEditSessionType());
    TEST_ASSERT_EQUAL_PTR(editManager.getStartNoteState(), editManager.getCurrentState());
    TEST_ASSERT_EQUAL_UINT32(id, editManager.getLastFader1SelectNoteId());
    TEST_ASSERT_FALSE(editManager.hasPendingCurrentRootNoteEditEntry());
    TEST_ASSERT_TRUE(feedback.rows.empty()); // normal kind cycle has no session-open event
    liveGrant(); liveGrant();
    TEST_ASSERT_TRUE(feedback.rows.empty());
    TEST_ASSERT_EQUAL_UINT32(0, outboundObservation().requestCalls);
    TEST_ASSERT_EQUAL_UINT32(0, outboundObservation().nestedDrainCalls);
    editManager.setEditEventListener(&controlSurfaceManager);
}

static void liveRepairExplicitSessionChangeAfterPublication(LivePhase phase) {
    for (auto type : {EditSessionType::Loop, EditSessionType::Note}) {
      setUpHarness(); LoopEventStore::initPool(); Track& track = liveReadySource();
      LiveRepairFeedback feedback; editManager.setEditEventListener(&feedback);
      pressEditModeButton(); liveReach(phase);
      editManager.sendEditSessionChange(type, true);
      TEST_ASSERT_FALSE(editManager.hasPendingCurrentRootNoteEditEntry());
      TEST_ASSERT_EQUAL(type, editManager.getEditSessionType());
      // Explicit sendEditSessionChange preserves existing payload/FSM semantics;
      // cycleEditSession, not this metadata API, owns closing Note payload.
      TEST_ASSERT_TRUE(editManager.isNoteEditActive());
      TEST_ASSERT_EQUAL_PTR(editManager.getSelectNoteState(), editManager.getCurrentState());
      TEST_ASSERT_EQUAL_UINT32(type == EditSessionType::Loop ? 2 : 1, feedback.rows.size());
      if (type == EditSessionType::Loop) TEST_ASSERT_EQUAL(EditEvent::SessionClosed, feedback.rows[0].event);
      const auto& opened = feedback.rows.back();
      TEST_ASSERT_EQUAL(EditEvent::SessionOpened, opened.event);
      TEST_ASSERT_EQUAL(type, opened.type); TEST_ASSERT_TRUE(opened.midi); TEST_ASSERT_FALSE(opened.fader);
      const auto count = feedback.rows.size();
      liveGrant(); liveGrant();
      TEST_ASSERT_EQUAL_UINT32(count, feedback.rows.size());
      TEST_ASSERT_EQUAL_UINT32(0, outboundObservation().requestCalls);
      editManager.setEditEventListener(&controlSurfaceManager);
      (void)track;
    }
}

static void test_live_repair_cycle_preview_tail() { liveRepairCycleAfterPublication(LivePhase::PreviewTail); }
static void test_live_repair_cycle_feedback_tail() { liveRepairCycleAfterPublication(LivePhase::FeedbackTail); }
static void test_live_repair_short_preview_tail() { liveRepairShortAfterPublication(LivePhase::PreviewTail); }
static void test_live_repair_short_feedback_tail() { liveRepairShortAfterPublication(LivePhase::FeedbackTail); }
static void test_live_repair_explicit_preview_tail() { liveRepairExplicitSessionChangeAfterPublication(LivePhase::PreviewTail); }
static void test_live_repair_explicit_feedback_tail() { liveRepairExplicitSessionChangeAfterPublication(LivePhase::FeedbackTail); }
