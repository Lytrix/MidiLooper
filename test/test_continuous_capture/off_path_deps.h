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
void logOverdubStopStage(const Loop&, uint32_t, const char*, uint32_t, uint32_t, uint32_t, const char*, const StopPathStorageStats*) {}
const char* commitResultLabel(CommitResult) { return "native"; }
void requestActiveLoopSlotPersist(Track&) { OFF_PATH(); }
MidiEventVec& EditManager::sessionMidiEvents() { OFF_PATH(); static MidiEventVec empty; return empty; }
void EditManager::foldLiveCaptureIntoNoteEditSession(Track&) { OFF_PATH(); }
void EditManager::commitAllPendingNoteEditActions(Track&) { OFF_PATH(); }
void Track::stopPlaying() { OFF_PATH(); }
void Track::startPlaying(uint32_t, bool) { OFF_PATH(); }
void Track::stopRecording(uint32_t) { OFF_PATH(); }
void Track::stopRecordingToStopped(uint32_t) { OFF_PATH(); }
void Track::togglePlayStop() { OFF_PATH(); }
void Track::invalidateCaches(bool) { OFF_PATH(); }
CommitResult Track::commitCaptureForStop(CommitReason, uint32_t, uint32_t) { OFF_PATH(); return {}; }
void Track::silenceTrackMidiOutput() { OFF_PATH(); }
void Track::clearQueuedPlaybackStart() { OFF_PATH(); }
void Track::queuePlaybackStartAtGrid(int32_t, uint32_t) { OFF_PATH(); }
void Track::commitQueuedPlaybackStart(uint32_t) { OFF_PATH(); }
void TrackManager::setMasterLoopLength(uint32_t) { OFF_PATH(); }
void TrackManager::reclaimUnreferencedDisabledPasses(PassReclaimStats*, bool) { OFF_PATH(); }
void StorageManager::markLoopSlotMaterialDirty(uint8_t, uint8_t) { OFF_PATH(); }
void StorageManager::admitLoopSlotPersist(uint8_t, uint8_t) { OFF_PATH(); }
void StorageManager::requestDeferredSaveState(const LooperState&, uint32_t, bool) { OFF_PATH(); }
#define OFF_PATH_EDIT(Type) \
void Type::onEnter(EditManager&, Track&, uint32_t) { OFF_PATH(); } \
void Type::onExit(EditManager&, Track&) { OFF_PATH(); } \
void Type::onEncoderTurn(EditManager&, Track&, int) { OFF_PATH(); } \
void Type::onButtonPress(EditManager&, Track&) { OFF_PATH(); }
OFF_PATH_EDIT(EditNoteHomeState)
OFF_PATH_EDIT(EditSelectNoteState)
OFF_PATH_EDIT(EditStartNoteState)
OFF_PATH_EDIT(EditLengthNoteState)
OFF_PATH_EDIT(EditPitchNoteState)
#undef OFF_PATH_EDIT
uint8_t resolveTrackIndexForPersistence(const Track& track) {
  for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i)
    if (&trackManager.getTrack(i) == &track) return i;
  return Config::INVALID_TRACK_INDEX;
}
void EditManager::onSelectedSlotChanged(Track&, uint8_t) { OFF_PATH(); }
void EditManager::beforeSelectedSlotChange(Track&) { OFF_PATH(); }
void DisplayManager::invalidateForSlotChange(uint8_t, uint8_t, uint8_t) { OFF_PATH(); }
bool StorageManager::hasLoopSlotPayloadOnSdInRam(uint8_t, uint8_t) { OFF_PATH(); return false; }
void StorageManager::refreshLoopSlotPayloadOnSdInRam(uint8_t, uint8_t) { OFF_PATH(); }
void StorageManager::prioritizeLoopSlotRestoreForFocus(uint8_t, uint8_t) { OFF_PATH(); }
void StorageManager::requestWorkspaceFooterPersistWhenSafe() { OFF_PATH(); }
void Track::silenceSlotMidiOutput(uint8_t) { OFF_PATH(); }
#include "BarStepButtonHandler.h"
BarStepButtonHandler::BarStepButtonHandler() {}
BarStepButtonHandler barStepButtonHandler;
bool BarStepButtonHandler::isBarStepButtonNote(uint8_t channel, uint8_t) const {
  TEST_ASSERT_EQUAL_MESSAGE(1, channel, "fixture injects musical channel 1 only");
  return false;
}
void BarStepButtonHandler::handleMidiNote(uint8_t, uint8_t, uint8_t, bool) { OFF_PATH(); }
MidiButtonProcessor::MidiButtonProcessor() {}
MidiButtonManager::MidiButtonManager() {}
MidiButtonManager midiButtonManager;
void MidiButtonManager::handleMidiNote(uint8_t, uint8_t, uint8_t, bool) { OFF_PATH(); }
MidiFaderProcessor::MidiFaderProcessor() {}
MidiFaderManager::MidiFaderManager() {}
ControlSurfaceManager::ControlSurfaceManager() {}
ControlSurfaceManager controlSurfaceManager;
void ControlSurfaceManager::onEditEvent(EditEvent) { OFF_PATH(); }
void ControlSurfaceManager::handleMidiCC(uint8_t, uint8_t, uint8_t) { OFF_PATH(); }
void ControlSurfaceManager::handleMidiPitchbend(uint8_t, int16_t) { OFF_PATH(); }
void ClockManager::onMidiStart() { OFF_PATH(); }
void ClockManager::onMidiStop() { OFF_PATH(); }
void ClockManager::onMidiClockPulse() { OFF_PATH(); }
void ClockManager::requestTransitionTo(ClockSource) { OFF_PATH(); }
uint32_t ClockManager::setLastMidiClockTime(uint32_t) { OFF_PATH(); return 0; }
void TrackManager::handleTransportStop() { OFF_PATH(); }
MidiFaderActions::MidiFaderActions() {}
