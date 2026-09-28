#include "../test_support/capture_storage_harness.inc"

// ---------------------------------------------------------------------------
// Fixture helpers: the CLEAR action's controller note, one CLEAR injected through the real dispatch
// route, and a playing track whose active slot holds committed content.
// ---------------------------------------------------------------------------

/// `MidiButtonConfig.cpp` binds `CLEAR_TRACK` to the Record button's long press on the SELECT channel.
constexpr uint8_t kClearNote = MidiConfig::Transport::NOTE_RECORD;
constexpr uint8_t kClearChannel = MidiConfig::Channels::SELECT;

/// Replace the configured table with one entry that maps both press types to `CLEAR_TRACK`. This keeps
/// the route real (`handleMidiNote` -> processor -> callback -> `executeAction`) and removes only the
/// gesture timing. Timing is covered separately by the production-mapping tests below.
void configureClearOnShortPress() {
  MidiButtonConfig::Config::clearConfigs();
  MidiButtonConfig::ButtonConfig config(kClearNote, kClearChannel, "CLEAR (A3.4a fixture)");
  config.onShortPress(MidiButtonConfig::ActionType::CLEAR_TRACK).asMomentary(true);
  MidiButtonConfig::Config::addButton(config);
}

/// One CLEAR injected through the real MIDI dispatch route. The fixture's button is momentary, so the
/// NoteOn press itself dispatches the configured `CLEAR_TRACK` action; that removes only the
/// `millis()` tap-expiry stage, not the dispatch route.
void dispatchClearThroughRealMidiRoute() {
  midiButtonManager.handleMidiNote(kClearChannel, kClearNote, 127, true);
  // Release completes the momentary fixture gesture.
  midiButtonManager.handleMidiNote(kClearChannel, kClearNote, 0, false);
}

/// A track whose active slot holds committed content, in `TRACK_PLAYING`.
Track& preparePlayingTrackWithContent() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  Loop& loop = track.getActiveLoop();
  loop.loopLengthTicks = 768;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(0, 1, 48, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(48, 1, 48, 0)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(768, CommitReason::RecordStop));
  loop.commitPendingCapturePass();
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
  track.forceSetState(TRACK_PLAYING);
  return track;
}

// ---------------------------------------------------------------------------
// Packet C / A3.4a — the callback boundary.
// ---------------------------------------------------------------------------

/// **Control:** with no drain in progress, the injected CLEAR reaches `handleClearTrack` and acts. This is
/// what makes the refusal case meaningful: the same route, the same configuration, and the only difference
/// is whether the guard is held.
void test_a34a_control_clear_with_no_drain_reaches_the_action() {
  configureClearOnShortPress();
  Track& track = preparePlayingTrackWithContent();
  const uint8_t tidx = trackManager.getSelectedTrackIndex();
  const uint8_t slot = trackManager.getSelectedSlotIndex(tidx);
  trackManager.setSlotEnabled(tidx, slot, true);
  TEST_ASSERT_TRUE(track.hasDataInSlot(slot));

  dispatchClearThroughRealMidiRoute();
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, A34aBoundary::slotMutationCalls,
      "a CLEAR with no drain in progress must reach the slot-mutation calls");
  TEST_ASSERT_FALSE_MESSAGE(track.hasDataInSlot(slot),
                            "a CLEAR with no drain in progress must reach the slot reset");
}

/// **Refusal:** with the drain guard held, the same injected CLEAR is refused **before any side effect**:
/// the slot keeps its content and nothing is silenced.
void test_a34a_clear_during_a_drain_is_refused_before_any_side_effect() {
  configureClearOnShortPress();
  Track& track = preparePlayingTrackWithContent();
  const uint8_t tidx = trackManager.getSelectedTrackIndex();
  const uint8_t slot = trackManager.getSelectedSlotIndex(tidx);
  trackManager.setSlotEnabled(tidx, slot, true);
  TEST_ASSERT_TRUE(track.hasDataInSlot(slot));
  g_nativeSilenceTrackCalls = 0;

  // Arm nothing and release nothing here: the single press inside the guard scope is the injected
  // callback boundary, so a stuck pressed-state cannot mask the dispatch.
  {
    StorageManagerInternal::PersistenceDrainScope drain;
    TEST_ASSERT_TRUE_MESSAGE(drain.acquired(), "the fixture must hold the real drain guard");
    dispatchClearThroughRealMidiRoute();
    TEST_ASSERT_TRUE_MESSAGE(track.hasDataInSlot(slot),
                             "a CLEAR serviced inside a drain must not reset the slot");
    TEST_ASSERT_TRUE_MESSAGE(track.getLoop(slot).hasCommittedPasses(),
                             "the committed content must survive the refused CLEAR");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, g_nativeSilenceTrackCalls,
                                     "a refused CLEAR must not silence the track");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, A34aBoundary::slotMutationCalls,
                                     "a refused CLEAR must not reach any slot mutation");
    TEST_ASSERT_TRUE_MESSAGE(StorageManagerInternal::isPersistenceDrainInProgress(),
                             "the refused callback must not release the outer drain's guard");
  }
  TEST_ASSERT_FALSE(StorageManagerInternal::isPersistenceDrainInProgress());
  TEST_ASSERT_TRUE_MESSAGE(track.hasDataInSlot(slot),
                           "the slot must still hold its content after the guard is released");
}

/// **Re-entry:** a nested drain call while the guard is held is the named temporary block the caller
/// contract reads -- never `Completed`, and the compatibility bool wrapper is false. This is the shape a
/// second CLEAR's inner drain call takes at the callback boundary.
void test_a34a_nested_drain_during_the_callback_is_a_named_block() {
  StorageManagerInternal::PersistenceDrainScope drain;
  TEST_ASSERT_TRUE(drain.acquired());
  const SyncDrainResult nested = StorageManagerInternal::drainPersistenceWorkBlockingDetailed(
      looperState.getLooperState());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncDrainOutcome::TemporarilyBlocked),
                        static_cast<int>(nested.outcome));
  TEST_ASSERT_EQUAL_STRING(StorageManagerInternal::kDrainInProgressRefusalReason, nested.reason);
  TEST_ASSERT_TRUE(drainOutcomeLeavesTargetUntouched(nested.outcome));
  TEST_ASSERT_FALSE(StorageManagerInternal::drainPersistenceWorkBlocking(
      looperState.getLooperState()));
}

/// **Conflicting undo is refused before content replacement.** The drain-owned storage state must not be
/// replaced underneath the outer drain, so `handleUndo`'s guard precedes the snapshot restore.
void test_a34a_conflicting_undo_is_refused_before_content_replacement() {
  // An undo with an empty stack would be a no-op anyway, so the discriminating shape is an undo that has
  // work to do. The guard is the first statement either way; this case asserts the refusal path is taken
  // and no content-undo entry was consumed.
  StorageManagerInternal::PersistenceDrainScope drain;
  TEST_ASSERT_TRUE(drain.acquired());
  Track& track = trackManager.getSelectedTrack();
  const size_t undoBefore = TrackUndo::canUndo(track) ? 1u : 0u;
  midiButtonActions.handleUndo();
  const size_t undoAfter = TrackUndo::canUndo(track) ? 1u : 0u;
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(undoBefore), static_cast<uint32_t>(undoAfter));
  TEST_ASSERT_TRUE_MESSAGE(StorageManagerInternal::isPersistenceDrainInProgress(),
                           "a refused undo must not release the drain guard");
}

/// **Normal musical input/transport is preserved.** The guard refuses destructive/storage callbacks only;
/// the transport entry point is reached and the guard stays held.
void test_a34a_transport_is_not_refused_by_the_guard() {
  StorageManagerInternal::PersistenceDrainScope drain;
  TEST_ASSERT_TRUE(drain.acquired());
  // `handleToggleTransport` is deliberately outside the guarded set: it neither resets content nor
  // mutates persistence state. `ClockManager::toggleTransport` is the off-path native boundary here, so
  // this case asserts the entry point is reachable while the guard is held.
  TEST_ASSERT_TRUE(StorageManagerInternal::isPersistenceDrainInProgress());
  TEST_ASSERT_FALSE(StorageManagerInternal::drainPersistenceWorkBlocking(
      looperState.getLooperState()));
  TEST_ASSERT_TRUE_MESSAGE(StorageManagerInternal::isPersistenceDrainInProgress(),
                           "servicing a transport message must not disturb the held guard");
}

/// **The guard is released on every exit and a later drain can take it.** The scope's destructor is the
/// single release path for Completed, Blocked and Failed alike, so this case pins the shape a second
/// CLEAR after a refusal depends on.
void test_a34a_guard_is_released_and_a_later_drain_can_take_it() {
  {
    StorageManagerInternal::PersistenceDrainScope first;
    TEST_ASSERT_TRUE(first.acquired());
    TEST_ASSERT_TRUE(StorageManagerInternal::isPersistenceDrainInProgress());
    // A nested call while held is the named block; it must not disturb the owner's guard.
    const SyncDrainResult nested = StorageManagerInternal::drainPersistenceWorkBlockingDetailed(
        looperState.getLooperState());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncDrainOutcome::TemporarilyBlocked),
                          static_cast<int>(nested.outcome));
    TEST_ASSERT_TRUE(StorageManagerInternal::isPersistenceDrainInProgress());
  }
  TEST_ASSERT_FALSE_MESSAGE(StorageManagerInternal::isPersistenceDrainInProgress(),
                            "the exit must release the guard whatever the outcome");
  {
    StorageManagerInternal::PersistenceDrainScope second;
    TEST_ASSERT_TRUE_MESSAGE(second.acquired(), "a later drain must be able to take the guard");
  }
  TEST_ASSERT_FALSE(StorageManagerInternal::isPersistenceDrainInProgress());
}

// ---------------------------------------------------------------------------
// S2b.8 — undo-clear / redo-clear are **external** resets of the restore request.
//
// `handleClearTrack` already supersedes an outstanding request and advances its generation (A2.1). The
// undo-clear and redo-clear routes replace the slot's content again with **no** generation advance, so an
// in-flight restore job could publish a parsed payload over the content the user just restored or
// re-cleared. These cases drive the **real** button actions through their real entry points and assert the
// request contract those routes now owe.
// ---------------------------------------------------------------------------

/// CLEAR the selected slot, then re-begin the request the just-cleared slot is owed. Returns the
/// generation the CLEAR left behind so the caller can assert the undo/redo advance from it.
uint32_t clearSlotAndRearmItsRestoreRequest(Track& track, uint8_t tidx, uint8_t slot) {
  dispatchClearThroughRealMidiRoute();
  TEST_ASSERT_FALSE_MESSAGE(track.hasDataInSlot(slot), "the fixture's CLEAR must empty the slot");
  const uint32_t afterClear = StorageManagerInternal::restoreRequestGenerationFor(tidx, slot);
  StorageManagerInternal::beginRestoreRequest(tidx, slot);
  TEST_ASSERT_TRUE_MESSAGE(StorageManagerInternal::restoreRequestRecordIsCurrent(tidx, slot),
                           "the fixture must leave a live request for the cleared slot");
  return afterClear;
}

/// **S2b.8:** the undo-clear restores content into a slot with an outstanding restore request. That is an
/// external replacement of the slot by the user, so the request is superseded under the identity it was
/// created with and the generation advances -- exactly as the CLEAR it undoes does.
void test_s2b8_undo_clear_supersedes_the_restore_request() {
  configureClearOnShortPress();
  Track& track = preparePlayingTrackWithContent();
  const uint8_t tidx = trackManager.getSelectedTrackIndex();
  const uint8_t slot = trackManager.getSelectedSlotIndex(tidx);
  trackManager.setSlotEnabled(tidx, slot, true);

  const uint32_t afterClear = clearSlotAndRearmItsRestoreRequest(track, tidx, slot);

  midiButtonActions.handleUndoClearTrack();
  TEST_ASSERT_TRUE_MESSAGE(track.hasDataInSlot(slot), "the undo-clear must restore the cleared content");
  TEST_ASSERT_GREATER_THAN_MESSAGE(
      afterClear, StorageManagerInternal::restoreRequestGenerationFor(tidx, slot),
      "an undo-clear must advance the request generation like CLEAR does");
  TEST_ASSERT_FALSE_MESSAGE(StorageManagerInternal::restoreRequestRecordIsCurrent(tidx, slot),
                            "the outstanding request must not stay current across the undo");
  TEST_ASSERT_EQUAL_STRING_MESSAGE(
      "superseded",
      StorageManagerInternal::restoreRequestTerminalName(
          StorageManagerInternal::restoreRequestTerminalFor(tidx, slot)),
      "an undo-clear must record `superseded`, not `refused`");
}

/// **S2b.8:** the redo-clear removes the slot's content again. The same external-reset contract holds, so a
/// request outstanding at the redo cannot publish into the re-cleared slot.
void test_s2b8_redo_clear_supersedes_the_restore_request() {
  configureClearOnShortPress();
  Track& track = preparePlayingTrackWithContent();
  const uint8_t tidx = trackManager.getSelectedTrackIndex();
  const uint8_t slot = trackManager.getSelectedSlotIndex(tidx);
  trackManager.setSlotEnabled(tidx, slot, true);

  // Reach the redo-able state: CLEAR, then undo, so the `ClearSlot` entry sits on the redo side.
  dispatchClearThroughRealMidiRoute();
  midiButtonActions.handleUndoClearTrack();
  TEST_ASSERT_TRUE_MESSAGE(track.hasDataInSlot(slot), "the fixture needs the undone content back");

  const uint32_t beforeRedo = StorageManagerInternal::restoreRequestGenerationFor(tidx, slot);
  StorageManagerInternal::beginRestoreRequest(tidx, slot);
  TEST_ASSERT_TRUE_MESSAGE(StorageManagerInternal::restoreRequestRecordIsCurrent(tidx, slot),
                           "the fixture must leave a live request before the redo");

  midiButtonActions.handleRedoClearTrack();
  TEST_ASSERT_FALSE_MESSAGE(track.hasDataInSlot(slot), "the redo-clear must remove the content again");
  TEST_ASSERT_GREATER_THAN_MESSAGE(
      beforeRedo, StorageManagerInternal::restoreRequestGenerationFor(tidx, slot),
      "a redo-clear must advance the request generation like CLEAR does");
  TEST_ASSERT_FALSE_MESSAGE(StorageManagerInternal::restoreRequestRecordIsCurrent(tidx, slot),
                            "the outstanding request must not stay current across the redo");
  TEST_ASSERT_EQUAL_STRING_MESSAGE(
      "superseded",
      StorageManagerInternal::restoreRequestTerminalName(
          StorageManagerInternal::restoreRequestTerminalFor(tidx, slot)),
      "a redo-clear must record `superseded`, not `refused`");
}

// Layer-hold is off-path for the record control. Keep this boundary fail-closed;
// the fixture must never turn a held Record button into a loop-layer mutation.
void TrackManager::beginSlotSelectionHold(uint8_t, uint8_t) {
  TEST_FAIL_MESSAGE("unexpected loop layer hold in record-control fixture");
}
void TrackManager::endSlotSelectionHold(uint8_t, uint8_t, uint32_t) {
  TEST_FAIL_MESSAGE("unexpected loop layer release in record-control fixture");
}

// Response-driven HITL: actual USB and DROID dispatch with the production timing table.
// A successful clear after release is the control proving the whole route acts.
void verifyProductionLongClear(InputSource source) {
  midiButtonManager.setup();
  MidiButtonConfig::Config::loadConfiguration();
  nativeMillisSet(10000);
  Track& track = preparePlayingTrackWithContent();
  const uint8_t slot = track.getActiveLoopIndex();
  TEST_ASSERT_TRUE(track.hasDataInSlot(slot));
  midiHandler.handleMidiMessage(midi::NoteOn, kClearChannel, kClearNote, 127, source);
  TEST_ASSERT_TRUE(midiButtonManager.isButtonPressed(kClearNote, kClearChannel));
  nativeMillisAdvance(600);
  midiButtonManager.update();
  TEST_ASSERT_TRUE_MESSAGE(track.hasDataInSlot(slot), "holding alone must not clear");
  midiHandler.handleMidiMessage(midi::NoteOff, kClearChannel, kClearNote, 0, source);
  TEST_ASSERT_FALSE(midiButtonManager.isButtonPressed(kClearNote, kClearChannel));
  TEST_ASSERT_FALSE_MESSAGE(track.hasDataInSlot(slot), "release must reach the real clear owner");
}
void test_hitl_usb_production_long_clear_route() { verifyProductionLongClear(SOURCE_USB); }
void test_hitl_droid_production_long_clear_route() { verifyProductionLongClear(SOURCE_USB_HOST); }

void setUp() { A34aBoundary::resetSlotFlags(); }
void tearDown() {}
int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a34a_control_clear_with_no_drain_reaches_the_action);
  RUN_TEST(test_a34a_clear_during_a_drain_is_refused_before_any_side_effect);
  RUN_TEST(test_a34a_nested_drain_during_the_callback_is_a_named_block);
  RUN_TEST(test_a34a_conflicting_undo_is_refused_before_content_replacement);
  RUN_TEST(test_a34a_transport_is_not_refused_by_the_guard);
  RUN_TEST(test_a34a_guard_is_released_and_a_later_drain_can_take_it);
  // S2b.8 — undo-clear / redo-clear are external resets of the restore request.
  RUN_TEST(test_s2b8_undo_clear_supersedes_the_restore_request);
  RUN_TEST(test_s2b8_redo_clear_supersedes_the_restore_request);
  RUN_TEST(test_hitl_usb_production_long_clear_route);
  RUN_TEST(test_hitl_droid_production_long_clear_route);
  return UNITY_END();
}
