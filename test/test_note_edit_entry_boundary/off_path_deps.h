// Boundary stub surface for the S7.5b.8a entry/focus/feedback oracle.
//
// Adapted from the windowed-preview integration suite's measured set (`../test_windowed_preview_integration/
// off_path_deps.h`), which is the closest existing combination: this suite also links the **real**
// note-edit owner (`src/EditManager*.cpp`) on top of the window lane, so it keeps that file's treatment
// of the display, storage and edit-state boundaries.
//
// What this suite links **real** on top of that set (the subject of S7.5b.8a):
//   * `src/ControlSurface/EditEventFeedback.cpp` -- `ControlSurfaceManager::cycleEditSession`,
//     `prepareNoteEditSessionOpen`, `sendNoteEditSessionFaderFeedback`, `onEditEvent` and the
//     SessionOpened/Closed/Selection/Geometry/Length handlers;
//   * `src/MidiButtonActions.cpp` -- the production button entry (`handleCycleEditMode`) a momentary
//     edit-mode press reaches.
//
// Declared boundaries of this suite (named, never silently deciding an asserted outcome):
//   * the **feedback send pipeline**: `requestFaderOutbound` records the production request (trigger +
//     plan) instead of sending; `processFaderOutbound` / `drainFaderOutboundUntilIdle` /
//     `ControlSurfaceManager::update` are stand-ins, so **no assertion here may depend on what they do**
//     -- the fixtures assert the production request and the production call sites around them
//     (`drainFaderOutboundUntilIdle`'s call is exactly the poll-around-work shape the slice records).
//     The motor send steps are S7.5b.8c's subject with its own fixture;
//   * the five `EditStates` FSM TUs (host ill-formed, the same boundary the windowed-preview suite
//     documents) -- production's `EditSelectNoteState::onEnter` runs a *second* `selectClosestNote`,
//     so the selection this suite asserts is the one `EditManager::enterDefaultNoteEditSessionState`
//     itself builds;
//   * storage/persistence requests, display projections and the transport start/stop/record-stop routes
//     stay observable or fail closed.
//
// Fail closed when the fixture accidentally enters an unrelated route.
#define OFF_PATH() TEST_FAIL_MESSAGE(__PRETTY_FUNCTION__)
DisplayManager::DisplayManager() {}
DisplayManager displayManager;
void ClockManager::toggleTransport() { OFF_PATH(); }
void TrackManager::forceMidiLedUpdate(uint32_t) {}
void DisplayManager::emitDisplayCaptureSnapshot(const Track&, uint8_t, uint32_t) {}
void DisplayManager::refreshViewportAfterOverdubStop(Track&, uint8_t, uint32_t) {}
void emitOverdubStopDisplaySnapshot(Track&, uint8_t, uint32_t) {}
void logMemoryAfterOverdubStop(uint32_t, const Loop&) {}
void logOverdubStopStage(const Loop&, uint32_t, const char*, uint32_t, uint32_t, uint32_t, const char*, const StopPathStorageStats*, const PendingCapturePass*) {}
const char* commitResultLabel(CommitResult) { return "native"; }
void requestActiveLoopSlotPersist(Track&, uint8_t) { OFF_PATH(); }
// The slot-matrix focus route's own persistence request (the redirect's save-state call). Storage is the
// load/save suites' subject; here the request is observable, because it *reports* the slot move and
// decides nothing about the session/focus identity this suite asserts.
TRACK_INTERNAL_MEM void requestLoopSlotPersistAndSaveState(Track&, uint8_t, uint32_t) {}
// The play/stop transport neighbours are not this suite's subject: the edit-mode entry never starts or
// stops the transport, so they stay fail-closed (a fixture that reached one would be testing another
// route). The record-stop commit route is likewise out of scope here.
void Track::stopPlaying() { OFF_PATH(); }
void Track::startPlaying(uint32_t, bool) { OFF_PATH(); }
void Track::togglePlayStop() { OFF_PATH(); }
void Track::stopRecording(uint32_t) { OFF_PATH(); }
void Track::stopRecordingToStopped(uint32_t) { OFF_PATH(); }
CommitResult Track::commitCaptureForStop(CommitReason, uint32_t, uint32_t, uint8_t) { OFF_PATH(); return {}; }
void Track::clearQueuedPlaybackStart() { OFF_PATH(); }
void TrackManager::setMasterLoopLength(uint32_t) { OFF_PATH(); }
void Track::queuePlaybackStartAtGrid(int32_t, uint32_t) { OFF_PATH(); }
void Track::commitQueuedPlaybackStart(uint32_t) { OFF_PATH(); }
// The edit-entry route reclaims un-referenced disabled passes on a session boundary; that reclaim is not
// this suite's subject, so it is observable rather than fail-closed.
void TrackManager::reclaimUnreferencedDisabledPasses(PassReclaimStats*, bool) {}
// Persistence/display requests a focus or slot change makes: observable, because they *report* the move.
void StorageManager::markLoopSlotMaterialDirty(uint8_t, uint8_t) {}
void StorageManager::admitLoopSlotPersist(uint8_t, uint8_t) {}
void StorageManager::requestDeferredSaveState(const LooperState&, uint32_t, bool) {}
void StorageManager::admitSlotMeta(uint8_t, uint8_t) {}
void StorageManager::requestUrgentEditSave() {}
// The note-edit **state FSM** (see the header note): entries are observable no-ops.
#define OBSERVABLE_EDIT_STATE(Type) \
void Type::onEnter(EditManager&, Track&, uint32_t) {} \
void Type::onExit(EditManager&, Track&) {} \
void Type::onEncoderTurn(EditManager&, Track&, int) {} \
void Type::onButtonPress(EditManager&, Track&) {}
OBSERVABLE_EDIT_STATE(EditNoteHomeState)
OBSERVABLE_EDIT_STATE(EditSelectNoteState)
OBSERVABLE_EDIT_STATE(EditStartNoteState)
OBSERVABLE_EDIT_STATE(EditLengthNoteState)
OBSERVABLE_EDIT_STATE(EditPitchNoteState)
#undef OBSERVABLE_EDIT_STATE
uint8_t resolveTrackIndexForPersistence(const Track& track) {
  for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i)
    if (&trackManager.getTrack(i) == &track) return i;
  return Config::INVALID_TRACK_INDEX;
}
// The focus route's display invalidation is recorded (the slot identities the panel is told about are
// part of the focus contract this suite asserts).
void DisplayManager::invalidateForSlotChange(uint8_t track, uint8_t from, uint8_t to) {
  SlotChangeObservation& observation = slotChangeObservation();
  ++observation.count;
  observation.track = track;
  observation.from = from;
  observation.to = to;
}
bool StorageManager::hasLoopSlotPayloadOnSdInRam(uint8_t, uint8_t) { return false; }
void StorageManager::refreshLoopSlotPayloadOnSdInRam(uint8_t, uint8_t) {}
void StorageManager::prioritizeLoopSlotRestoreForFocus(uint8_t, uint8_t) {}
void StorageManager::requestWorkspaceFooterPersistWhenSafe() {}
#include "BarStepButtonHandler.h"
BarStepButtonHandler::BarStepButtonHandler() {}
BarStepButtonHandler barStepButtonHandler;
bool BarStepButtonHandler::isBarStepButtonNote(uint8_t channel, uint8_t) const {
  TEST_ASSERT_EQUAL_MESSAGE(1, channel, "fixture injects musical channel 1 only");
  return false;
}
void BarStepButtonHandler::handleMidiNote(uint8_t, uint8_t, uint8_t, bool) { OFF_PATH(); }
// The button route is real **here** (`src/MidiButtonActions.cpp` is linked below); only the two
// manager/processor types it delegates through stay boundary types on this suite's route, because the
// momentary edit-mode gesture is dispatched by calling the production action entry directly.
MidiButtonProcessor::MidiButtonProcessor() {}
MidiButtonManager::MidiButtonManager() {}
MidiButtonManager midiButtonManager;
void MidiButtonManager::handleMidiNote(uint8_t, uint8_t, uint8_t, bool) { OFF_PATH(); }
MidiFaderProcessor::MidiFaderProcessor() {}
MidiFaderManager::MidiFaderManager() {}
ControlSurfaceManager::ControlSurfaceManager() {}
ControlSurfaceManager controlSurfaceManager;
void ControlSurfaceManager::handleMidiCC(uint8_t, uint8_t, uint8_t) { OFF_PATH(); }
void ControlSurfaceManager::handleMidiPitchbend(uint8_t, int16_t) { OFF_PATH(); }
void ClockManager::onMidiStart() { OFF_PATH(); }
void ClockManager::onMidiStop() { OFF_PATH(); }
void ClockManager::onMidiClockPulse() { OFF_PATH(); }
void ClockManager::requestTransitionTo(ClockSource) { OFF_PATH(); }
uint32_t ClockManager::setLastMidiClockTime(uint32_t) { OFF_PATH(); return 0; }
void TrackManager::handleTransportStop() { OFF_PATH(); }
MidiFaderActions::MidiFaderActions() {}

// ---- the feedback-outbound boundary of the entry route (see the header note) ---------------------------
void ControlSurfaceManager::requestFaderOutbound(NoteEditFaderOutbound::Trigger trigger,
                                                  const NoteEditFaderOutbound::PlanFlags* planOverride) {
  OutboundObservation& observation = outboundObservation();
  ++observation.requestCalls;
  observation.lastTrigger = trigger;
  observation.lastPlan = planOverride != nullptr ? *planOverride
                                                 : NoteEditFaderOutbound::planForTrigger(trigger);
}
bool ControlSurfaceManager::isFaderOutboundActive() const { return false; }
void ControlSurfaceManager::processFaderOutbound() {}
void ControlSurfaceManager::drainFaderOutboundUntilIdle() { ++outboundObservation().nestedDrainCalls; }
// The main-loop continuation entry, declared here so the linked closure resolves. It is **not** asserted
// on: which children `ControlSurfaceManager::update()` runs belongs to the send-pipeline half.
void ControlSurfaceManager::update() {}
void ControlSurfaceManager::releaseEditedNoteAudition() { ++outboundObservation().auditionReleases; }
void ControlSurfaceManager::releasePendingMotorNotegates() {}
void ControlSurfaceManager::processPendingMotorNotegates() {}
void ControlSurfaceManager::clearPendingSelectDependentMotorSync() {}
void ControlSurfaceManager::clearPendingGeometryDriverMotorSync() {}
void ControlSurfaceManager::resetSelectNavSlotApplyState() {}
void ControlSurfaceManager::refreshEditingActivity() {}
void ControlSurfaceManager::armSelectFaderFeedbackIgnore(uint32_t, uint32_t) {}
void ControlSurfaceManager::armCoarseFaderFeedbackIgnore(uint32_t) {}
void ControlSurfaceManager::armChannel15CcFaderFeedbackIgnore(uint32_t) {}
void ControlSurfaceManager::armChannel15FaderFeedbackIgnore(uint32_t) {}
void ControlSurfaceManager::scheduleSelectDependentMotorSync(Track&, const EditorSelection&,
                                                             const EditorSelection&, bool) {}
void DisplayManager::requestNoteInfoRefresh(Track&) {}
void DisplayManager::invalidateLiveDisplayCache(bool) {}
DetailedWindowContext DisplayManager::resolveDetailedWindow(const Track&, uint8_t, uint32_t) const {
  OFF_PATH();
  return DetailedWindowContext{};
}
