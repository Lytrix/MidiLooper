// Fail closed when the fixture accidentally enters an unrelated route.
#define OFF_PATH() TEST_FAIL_MESSAGE(__PRETTY_FUNCTION__)
DisplayManager::DisplayManager() {}
DisplayManager displayManager;
void ClockManager::toggleTransport() { OFF_PATH(); }
#if !defined(A34A_LINKS_REAL_TRACK_TRANSPORT)
void TrackManager::forceMidiLedUpdate(uint32_t) {}
#endif
void DisplayManager::emitDisplayCaptureSnapshot(const Track&, uint8_t, uint32_t) {}
void DisplayManager::refreshViewportAfterOverdubStop(Track&, uint8_t, uint32_t) {}
void emitOverdubStopDisplaySnapshot(Track&, uint8_t, uint32_t) {}
void logMemoryAfterOverdubStop(uint32_t, const Loop&) {}
void logOverdubStopStage(const Loop&, uint32_t, const char*, uint32_t, uint32_t, uint32_t, const char*, const StopPathStorageStats*, const PendingCapturePass*) {}
const char* commitResultLabel(CommitResult) { return "native"; }
void requestActiveLoopSlotPersist(Track&, uint8_t) { OFF_PATH(); }
MidiEventVec& EditManager::sessionMidiEvents() { OFF_PATH(); static MidiEventVec empty; return empty; }
#if !defined(A34A_LINKS_REAL_TRACK_TRANSPORT)
void EditManager::foldLiveCaptureIntoNoteEditSession(Track&) { OFF_PATH(); }
#endif
void EditManager::commitAllPendingNoteEditActions(Track&) { OFF_PATH(); }
#if !defined(A34A_LINKS_REAL_TRACK_TRANSPORT)
void Track::stopPlaying() { OFF_PATH(); }
void Track::startPlaying(uint32_t, bool) { OFF_PATH(); }
void Track::stopRecording(uint32_t) { OFF_PATH(); }
void Track::stopRecordingToStopped(uint32_t) { OFF_PATH(); }
void Track::togglePlayStop() { OFF_PATH(); }
#endif
// Track::invalidateCaches and its committed-scratch neighbour are production code here
// (TrackMidiEventRouting.cpp / CommittedMidiScratch.cpp are linked by the test TU):
// record-arm admission runs on this suite's route and the admitted arm invalidates the
// slot's derived caches. Only the edit-session neighbours stay off-path.
void EditManager::bumpSessionPreviewRevision() { OFF_PATH(); }
void EditManager::bumpSessionPlaybackPreviewRevision() { OFF_PATH(); }
void EditManager::scheduleDeferredNoteEditDisplayRefresh() { OFF_PATH(); }
MidiEventVec& EditManager::editMidiEvents(Track&) { OFF_PATH(); static MidiEventVec empty; return empty; }
const MidiEventVec& EditManager::editMidiEvents(const Track&) const { OFF_PATH(); static MidiEventVec empty; return empty; }
CommitResult Track::commitCaptureForStop(CommitReason, uint32_t, uint32_t, uint8_t) { OFF_PATH(); return {}; }
// A physical output boundary, not an unrelated domain route: silencing is reachable from both stop
// destinations, so failing closed here left the whole `Stopped`-destination deferred drain without any
// native coverage (see B03). Counting it instead keeps it observable -- a fixture can assert the track
// was silenced -- without pretending the silence itself is under test. Reset in setUp.
void Track::silenceTrackMidiOutput() { ++g_nativeSilenceTrackCalls; }
#if !defined(A34A_LINKS_REAL_TRACK_TRANSPORT)
void Track::clearQueuedPlaybackStart() { OFF_PATH(); }
void Track::queuePlaybackStartAtGrid(int32_t, uint32_t) { OFF_PATH(); }
void Track::commitQueuedPlaybackStart(uint32_t) { OFF_PATH(); }
void TrackManager::setMasterLoopLength(uint32_t) { OFF_PATH(); }
void TrackManager::reclaimUnreferencedDisabledPasses(PassReclaimStats*, bool) { OFF_PATH(); }
#endif
#if !defined(A34A_LINKS_REAL_TRACK_TRANSPORT)
void StorageManager::markLoopSlotMaterialDirty(uint8_t, uint8_t) { OFF_PATH(); }
void StorageManager::admitLoopSlotPersist(uint8_t, uint8_t) { OFF_PATH(); }
void StorageManager::requestDeferredSaveState(const LooperState&, uint32_t, bool) { OFF_PATH(); }
#endif
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
#if !defined(A34A_LINKS_REAL_TRACK_TRANSPORT)
void EditManager::onSelectedSlotChanged(Track&, uint8_t) { OFF_PATH(); }
void EditManager::beforeSelectedSlotChange(Track&) { OFF_PATH(); }
#endif
void DisplayManager::invalidateForSlotChange(uint8_t, uint8_t, uint8_t) { OFF_PATH(); }
// A suite that links the real StorageManager load path (which defines these two) opts out with
// `HARNESS_LINKS_LOOP_SLOT_PAYLOAD_ON_SD`; everything else keeps the fail-closed route.
#if !defined(HARNESS_LINKS_LOOP_SLOT_PAYLOAD_ON_SD) && !defined(A34A_LINKS_REAL_TRACK_TRANSPORT)
bool StorageManager::hasLoopSlotPayloadOnSdInRam(uint8_t, uint8_t) { OFF_PATH(); return false; }
void StorageManager::refreshLoopSlotPayloadOnSdInRam(uint8_t, uint8_t) { OFF_PATH(); }
#endif
#if !defined(A34A_LINKS_REAL_TRACK_TRANSPORT)
void StorageManager::prioritizeLoopSlotRestoreForFocus(uint8_t, uint8_t) { OFF_PATH(); }
#endif
#if !defined(A34A_LINKS_REAL_TRACK_TRANSPORT)
void StorageManager::requestWorkspaceFooterPersistWhenSafe() { OFF_PATH(); }
#endif
void Track::silenceSlotMidiOutput(uint8_t) { OFF_PATH(); }
#include "BarStepButtonHandler.h"
BarStepButtonHandler::BarStepButtonHandler() {}
BarStepButtonHandler barStepButtonHandler;
bool BarStepButtonHandler::isBarStepButtonNote(uint8_t channel, uint8_t) const {
  TEST_ASSERT_EQUAL_MESSAGE(1, channel, "fixture injects musical channel 1 only");
  return false;
}
void BarStepButtonHandler::handleMidiNote(uint8_t, uint8_t, uint8_t, bool) { OFF_PATH(); }
#if !defined(A34A_LINKS_REAL_MIDI_BUTTONS)
MidiButtonProcessor::MidiButtonProcessor() {}
MidiButtonManager::MidiButtonManager() {}
MidiButtonManager midiButtonManager;
void MidiButtonManager::handleMidiNote(uint8_t, uint8_t, uint8_t, bool) { OFF_PATH(); }
#endif
MidiFaderProcessor::MidiFaderProcessor() {}
MidiFaderManager::MidiFaderManager() {}
ControlSurfaceManager::ControlSurfaceManager() {}
ControlSurfaceManager controlSurfaceManager;
#if !defined(A34A_LINKS_REAL_TRACK_TRANSPORT)
void ControlSurfaceManager::onEditEvent(EditEvent) { OFF_PATH(); }
#endif
void ControlSurfaceManager::handleMidiCC(uint8_t, uint8_t, uint8_t) { OFF_PATH(); }
void ControlSurfaceManager::handleMidiPitchbend(uint8_t, int16_t) { OFF_PATH(); }
void ClockManager::onMidiStart() { OFF_PATH(); }
void ClockManager::onMidiStop() { OFF_PATH(); }
void ClockManager::onMidiClockPulse() { OFF_PATH(); }
void ClockManager::requestTransitionTo(ClockSource) { OFF_PATH(); }
uint32_t ClockManager::setLastMidiClockTime(uint32_t) { OFF_PATH(); return 0; }
void TrackManager::handleTransportStop() { OFF_PATH(); }
MidiFaderActions::MidiFaderActions() {}
