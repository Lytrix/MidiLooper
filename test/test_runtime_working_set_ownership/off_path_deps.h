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
// Some suites link the real Track stop-telemetry TU (S4b needs the switch's persistence request), so
// these stop-path diagnostics must not also be defined here.
#if !defined(HARNESS_LINKS_REAL_STOP_TELEMETRY)
void emitOverdubStopDisplaySnapshot(Track&, uint8_t, uint32_t) {}
void logMemoryAfterOverdubStop(uint32_t, const Loop&) {}
void logOverdubStopStage(const Loop&, uint32_t, const char*, uint32_t, uint32_t, uint32_t, const char*, const StopPathStorageStats*, const PendingCapturePass*) {}
const char* commitResultLabel(CommitResult) { return "native"; }
void requestActiveLoopSlotPersist(Track&, uint8_t) { OFF_PATH(); }
#endif
MidiEventVec& EditManager::sessionMidiEvents() { OFF_PATH(); static MidiEventVec empty; return empty; }
#if !defined(A34A_LINKS_REAL_TRACK_TRANSPORT)
void EditManager::foldLiveCaptureIntoNoteEditSession(Track&) { OFF_PATH(); }
#endif
void EditManager::commitAllPendingNoteEditActions(Track&) { OFF_PATH(); }
#if !defined(A34A_LINKS_REAL_TRACK_TRANSPORT)
void Track::stopPlaying() { OFF_PATH(); }
void Track::startPlaying(uint32_t, bool) { OFF_PATH(); }
void Track::togglePlayStop() { OFF_PATH(); }
#endif
// The record-stop commit route is a **separate** boundary from the play/stop transport neighbours above: one
// fixture has to drive the production transport-stop route and assert that the resulting take *commits* with
// content, and that route lives in `TrackCaptureStopCommit.cpp` + `TrackManagerTransportTick.cpp`. Those TUs
// define `Track::stopRecording`, `Track::stopRecordingToStopped`, `Track::commitCaptureForStop` and
// `TrackManager::handleTransportStop`, so the four stubs move behind this macro. Default stays OFF_PATH: only
// a TU that defines the macro (and includes the real TUs) links them, and no fixture in the suite reached any
// of the four before this. `stopPlaying`/`startPlaying`/`togglePlayStop` deliberately stay stubbed -- the
// recording branch is the one under test, and the play branch must remain fail-closed.
#if !defined(HARNESS_LINKS_REAL_TRANSPORT_STOP)
void Track::stopRecording(uint32_t) { OFF_PATH(); }
void Track::stopRecordingToStopped(uint32_t) { OFF_PATH(); }
#endif
// Track::invalidateCaches and its committed-scratch neighbour are production code here
// (TrackMidiEventRouting.cpp / CommittedMidiScratch.cpp are linked by the test TU):
// record-arm admission runs on this suite's route and the admitted arm invalidates the
// slot's derived caches. Only the edit-session neighbours stay off-path.
void EditManager::bumpSessionPreviewRevision() { OFF_PATH(); }
void EditManager::bumpSessionPlaybackPreviewRevision() { OFF_PATH(); }
void EditManager::scheduleDeferredNoteEditDisplayRefresh() { OFF_PATH(); }
MidiEventVec& EditManager::editMidiEvents(Track&) { OFF_PATH(); static MidiEventVec empty; return empty; }
// `TrackManager::setSelectedTrack` (the production track-select route) departs the note-edit session before
// it moves focus. Note-edit departure is the edit suites' subject, but a **cross-track** successor fixture
// has to select the successor's track through the same route a user does, so these two neighbours are
// observable for it instead of failing closed. Neither is asserted on; the edit-session semantics stay in
// the edit suites.
#if defined(HARNESS_OBSERVABLE_TRACK_SELECT)
void EditManager::beforeSelectedTrackChange(Track&) {}
void EditManager::onTrackChanged(Track&) {}
#else
void EditManager::beforeSelectedTrackChange(Track&) { OFF_PATH(); }
void EditManager::onTrackChanged(Track&) { OFF_PATH(); }
#endif
// `Track::clear()` reverts any in-flight note-edit session for the loop it clears. A fixture that clears a
// slot through the production pass removal reaches this; it is a note-edit neighbour, so it is observable
// here (no-op) rather than failing closed. The note-edit suites remain its subject.
#if defined(HARNESS_OBSERVABLE_SLOT_CLEAR)
void EditManager::revertNoteEditSessionForLoopClear(Track&) {}
#else
void EditManager::revertNoteEditSessionForLoopClear(Track&) { OFF_PATH(); }
#endif
const MidiEventVec& EditManager::editMidiEvents(const Track&) const { OFF_PATH(); static MidiEventVec empty; return empty; }
#if !defined(HARNESS_LINKS_REAL_TRANSPORT_STOP)
CommitResult Track::commitCaptureForStop(CommitReason, uint32_t, uint32_t, uint8_t) { OFF_PATH(); return {}; }
#endif
// A physical output boundary, not an unrelated domain route: silencing is reachable from both stop
// destinations, so failing closed here left the whole `Stopped`-destination deferred drain without any
// native coverage (see B03). Counting it instead keeps it observable -- a fixture can assert the track
// was silenced -- without pretending the silence itself is under test. Reset in setUp.
void Track::silenceTrackMidiOutput() { ++g_nativeSilenceTrackCalls; }
#if !defined(A34A_LINKS_REAL_TRACK_TRANSPORT)
// `handleTransportStop` reaches these two, so a suite that drives that route makes them observable rather
// than fail-closed. Neither weakens the fixture that drives it: `clearQueuedPlaybackStart` is a reset of a
// queued start the transport-stop fixture never has, and `setMasterLoopLength` records the *track manager's*
// adopted length while that fixture asserts the loop's own `loopLengthTicks` (set by the commit itself).
// `queuePlaybackStartAtGrid`/`commitQueuedPlaybackStart` are NOT reachable from the route and stay
// fail-closed, as do the play/stop neighbours above.
#if defined(HARNESS_LINKS_REAL_TRANSPORT_STOP)
void Track::clearQueuedPlaybackStart() {}
// S4f.21: record the adopted value rather than dropping it, so a fixture can assert that the master
// length came from the interval's *published* geometry. The real `masterLoopLength` is private and
// this suite links the stop route, not the policy TU that owns its accessor, so the stub is the
// observable boundary (matching the `g_nativeSilenceTrackCalls` convention above).
void TrackManager::setMasterLoopLength(uint32_t length) {
  g_nativeAdoptedMasterLength = length;
  ++g_nativeAdoptedMasterLengthCalls;
}
#else
void Track::clearQueuedPlaybackStart() { OFF_PATH(); }
void TrackManager::setMasterLoopLength(uint32_t) { OFF_PATH(); }
#endif
void Track::queuePlaybackStartAtGrid(int32_t, uint32_t) { OFF_PATH(); }
void Track::commitQueuedPlaybackStart(uint32_t) { OFF_PATH(); }
// The record start ends by reclaiming disabled-pass storage. That reclaim is not the handoff under
// test, so suites that drive a start keep it observable.
#if defined(HARNESS_OBSERVABLE_START_RECLAIM)
void TrackManager::reclaimUnreferencedDisabledPasses(PassReclaimStats*, bool) {}
#else
void TrackManager::reclaimUnreferencedDisabledPasses(PassReclaimStats*, bool) { OFF_PATH(); }
#endif
#endif
#if !defined(A34A_LINKS_REAL_TRACK_TRANSPORT)
// These are the persistence requests a switch/selection makes. Storage behavior belongs to the load
// suites; suites driving the switch keep them observable so the role handoff is what is under test.
#if defined(HARNESS_OBSERVABLE_SD_REFRESH)
void StorageManager::markLoopSlotMaterialDirty(uint8_t, uint8_t) {}
void StorageManager::admitLoopSlotPersist(uint8_t, uint8_t) {}
void StorageManager::requestDeferredSaveState(const LooperState&, uint32_t, bool) {}
#else
void StorageManager::markLoopSlotMaterialDirty(uint8_t, uint8_t) { OFF_PATH(); }
void StorageManager::admitLoopSlotPersist(uint8_t, uint8_t) { OFF_PATH(); }
void StorageManager::requestDeferredSaveState(const LooperState&, uint32_t, bool) { OFF_PATH(); }
#endif
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
#if !defined(HARNESS_LINKS_REAL_STOP_TELEMETRY)
uint8_t resolveTrackIndexForPersistence(const Track& track) {
  for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i)
    if (&trackManager.getTrack(i) == &track) return i;
  return Config::INVALID_TRACK_INDEX;
}
#endif
#if !defined(A34A_LINKS_REAL_TRACK_TRANSPORT)
#if defined(HARNESS_OBSERVABLE_SLOT_CHANGE)
void EditManager::onSelectedSlotChanged(Track&, uint8_t) {}
#else
void EditManager::onSelectedSlotChanged(Track&, uint8_t) { OFF_PATH(); }
#endif
// The switch path commits any open edit session on depart. Linking the real depart TU pulls the whole
// note-edit closure (reopen/persist/display-refresh), which is out of scope for a role-handoff suite, so
// suites that drive the switch keep this observable instead of fail-closed. No edit session is open in
// those cases, so the observable form is behaviorally the production no-op.
#if defined(HARNESS_OBSERVABLE_SLOT_CHANGE)
void EditManager::beforeSelectedSlotChange(Track&) {}
#else
void EditManager::beforeSelectedSlotChange(Track&) { OFF_PATH(); }
#endif
#endif
// The switch path invalidates the display cache; suites that drive the switch keep this observable
// instead of fail-closed, because the display is not the behavior under test.
#if defined(HARNESS_OBSERVABLE_SLOT_CHANGE)
void DisplayManager::invalidateForSlotChange(uint8_t, uint8_t, uint8_t) {}
#else
void DisplayManager::invalidateForSlotChange(uint8_t, uint8_t, uint8_t) { OFF_PATH(); }
#endif
// A suite that links the real StorageManager load path (which defines these two) opts out with
// `HARNESS_LINKS_LOOP_SLOT_PAYLOAD_ON_SD`; everything else keeps the fail-closed route.
#if !defined(HARNESS_LINKS_LOOP_SLOT_PAYLOAD_ON_SD) && !defined(A34A_LINKS_REAL_TRACK_TRANSPORT)
// The switch asks whether the target slot has a payload on SD. The harness has no card, so the
// truthful observable answer is "no payload on SD"; the storage behavior is the load suites' subject.
#if defined(HARNESS_OBSERVABLE_SD_REFRESH)
bool StorageManager::hasLoopSlotPayloadOnSdInRam(uint8_t, uint8_t) { return false; }
#else
bool StorageManager::hasLoopSlotPayloadOnSdInRam(uint8_t, uint8_t) { OFF_PATH(); return false; }
#endif
// The switch path refreshes the target slot's payload from SD. The storage half of a switch belongs to
// the load/restore suites, so it is observable here rather than fail-closed: the behavior under test is
// the role handoff, not the SD read.
#if defined(HARNESS_OBSERVABLE_SD_REFRESH)
void StorageManager::refreshLoopSlotPayloadOnSdInRam(uint8_t, uint8_t) {}
#else
void StorageManager::refreshLoopSlotPayloadOnSdInRam(uint8_t, uint8_t) { OFF_PATH(); }
#endif
#endif
#if !defined(A34A_LINKS_REAL_TRACK_TRANSPORT)
// The selection/focus change asks storage to prioritize a restore. Storage is the load suites' subject;
// suites driving the switch keep this observable.
#if defined(HARNESS_OBSERVABLE_SD_REFRESH)
void StorageManager::prioritizeLoopSlotRestoreForFocus(uint8_t, uint8_t) {}
#else
void StorageManager::prioritizeLoopSlotRestoreForFocus(uint8_t, uint8_t) { OFF_PATH(); }
#endif
#endif
#if !defined(A34A_LINKS_REAL_TRACK_TRANSPORT)
#if defined(HARNESS_OBSERVABLE_SD_REFRESH)
void StorageManager::requestWorkspaceFooterPersistWhenSafe() {}
#else
void StorageManager::requestWorkspaceFooterPersistWhenSafe() { OFF_PATH(); }
#endif
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
#if !defined(HARNESS_LINKS_REAL_TRANSPORT_STOP)
void TrackManager::handleTransportStop() { OFF_PATH(); }
#endif
MidiFaderActions::MidiFaderActions() {}

// ---- the record-stop commit route's own boundaries (HARNESS_LINKS_REAL_TRANSPORT_STOP) ----------------
// Linking `TrackCaptureStopCommit.cpp` + `TrackManagerTransportTick.cpp` brings the real record-stop commit
// chain, and with it exactly these neighbours. They are the same classes of boundary the rest of this file
// already handles, so they get the same treatment rather than new semantics:
//  * display refresh after a record stop -- an empty body, mirroring `refreshViewportAfterRecordStop`'s
//    sibling `refreshViewportAfterOverdubStop` above;
//  * persistence queries -- false, so `handleTransportStop` never queues a workspace save, and the write
//    requests below fail closed if that ever changes;
//  * `Track::sendAllNotesOff` -- a physical output boundary, so it is observable for a suite that drives the
//    stop route (no-op) and fail-closed otherwise.
void DisplayManager::refreshViewportAfterRecordStop(Track&, uint8_t, uint32_t) {}
#if defined(HARNESS_OBSERVABLE_ALL_NOTES_OFF)
void Track::sendAllNotesOff() {}
#else
void Track::sendAllNotesOff() { OFF_PATH(); }
#endif
#if defined(HARNESS_LINKS_REAL_TRANSPORT_STOP)
// `handleTransportStop` calls this only when the save query below says yes; it is defined so the route links,
// and it fails closed if the route is ever entered.
void StorageManager::admitGlobalMeta() { OFF_PATH(); }
// The real `StorageManager::shouldQueueCurrentWorkspaceSave()` is already defined in the linked set and
// references these; they answer "nothing is dirty, nothing needs a full write, nothing to auto-save", which
// is the fail-closed answer for a suite that is not testing persistence.
namespace StorageManagerInternal {
CurrentSetStorage::AnchorFields currentSetAnchorFields;
bool forceCurrentSetFullLoopWrite = false;
bool anyAllocatedLoopEditStateDirty() { return false; }
bool anyCurrentSetLoopSlotDirty() { return false; }
}  // namespace StorageManagerInternal
bool CurrentSetStorage::shouldAutoSaveBeforeLoadIntoCurrent(const AnchorFields&) { return false; }
#endif
