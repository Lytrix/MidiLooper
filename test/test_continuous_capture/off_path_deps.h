// Fail closed when the fixture accidentally enters an unrelated route.
#define OFF_PATH() TEST_FAIL_MESSAGE(__PRETTY_FUNCTION__)
DisplayManager::DisplayManager() {}
DisplayManager displayManager;
void ClockManager::toggleTransport() { OFF_PATH(); }
// ---------------------------------------------------------------------------------------------
// Boundary families. This header's stand-ins were originally gated by ONE macro
// (`A34A_LINKS_REAL_TRACK_TRANSPORT`), which turned out to couple three independent concerns:
// a suite that needed the real **track transport** to test a stop route was forced to also link
// the real **storage-persistence** requests and the real **edit-session** handlers. Measured
// 2026-09-27: enabling that single macro pulled in ~30 storage TUs plus `EditManager.cpp` and
// `NoteEditSessionLifecycle.cpp`, which is not what a transport fixture is asking for.
//
// Each family now has its own gate, and the umbrella below defines all of them so every existing
// suite keeps exactly its current behaviour:
//
//   A34A_LINKS_REAL_TRACK_TRANSPORT      the Track transport/playback owners and the LED update
//   A34A_LINKS_REAL_TRACK_STOP_COMMIT    the synchronous stop-commit owners alone (a subset of the
//                                        transport family: what a stop-route fixture actually needs)
//   A34A_LINKS_REAL_STORAGE_PERSIST      the loop-slot dirty/admit/save requests
//   A34A_LINKS_REAL_EDIT_SESSION         the live-capture fold and the edit-session depart handlers
//   A34A_LINKS_REAL_TRACK_STOP_TELEMETRY the stop-path telemetry helpers, which also own the real
//                                        `resetActiveLoopAfterEmptyCapture` state reset
//
// A suite enabling only the transport family gets the real stop routes and none of the storage,
// edit-session or telemetry closure. A suite that wants the historical "all of it" meaning defines
// `A34A_LINKS_REAL_TRACK_TRANSPORT_ALL` instead.
#if defined(A34A_LINKS_REAL_TRACK_TRANSPORT_ALL)
#define A34A_LINKS_REAL_TRACK_TRANSPORT
#define A34A_LINKS_REAL_STORAGE_PERSIST
#define A34A_LINKS_REAL_EDIT_SESSION
#define A34A_LINKS_REAL_TRACK_STOP_TELEMETRY
#endif
#if !defined(A34A_LINKS_REAL_TRACK_TRANSPORT) && !defined(A34A_LINKS_REAL_TRACK_STOP_COMMIT)
void TrackManager::forceMidiLedUpdate(uint32_t) {}
#endif
void DisplayManager::emitDisplayCaptureSnapshot(const Track&, uint8_t, uint32_t) {}
void DisplayManager::refreshViewportAfterOverdubStop(Track&, uint8_t, uint32_t) {}
// Display is a boundary, never the behaviour under test. The real stop TU reaches this one, so it is a
// stand-in like its overdub sibling above rather than a fail-closed route.
void DisplayManager::refreshViewportAfterRecordStop(Track&, uint8_t, uint32_t) {}
#if !defined(A34A_LINKS_REAL_TRACK_STOP_TELEMETRY)
void emitOverdubStopDisplaySnapshot(Track&, uint8_t, uint32_t) {}
void logMemoryAfterOverdubStop(uint32_t, const Loop&) {}
void logOverdubStopStage(const Loop&, uint32_t, const char*, uint32_t, uint32_t, uint32_t, const char*, const StopPathStorageStats*, const PendingCapturePass*) {}
const char* commitResultLabel(CommitResult) { return "native"; }
void requestActiveLoopSlotPersist(Track&, uint8_t) { OFF_PATH(); }
#endif
MidiEventVec& EditManager::sessionMidiEvents() { OFF_PATH(); static MidiEventVec empty; return empty; }
#if !defined(A34A_LINKS_REAL_EDIT_SESSION) && !defined(A34A_LINKS_REAL_TRACK_TRANSPORT)
void EditManager::foldLiveCaptureIntoNoteEditSession(Track&) { OFF_PATH(); }
#endif
void EditManager::commitAllPendingNoteEditActions(Track&) { OFF_PATH(); }
// The transport family has two halves, and a stop-route fixture needs only the second:
//   * the play/stop and queued-playback machinery (`TrackTransportControl.cpp`), whose real owners reach the
//     playback hot path -- including the two MIDI-silence boundaries B03 counts, so linking it changes an
//     existing fixture's instrumentation;
//   * the STOP COMMIT owners (`Track::stopRecording` / `stopRecordingToStopped` / `commitCaptureForStop`,
//     all in `TrackCaptureStopCommit.cpp`), which is what a stop-route fixture is about.
// `A34A_LINKS_REAL_TRACK_STOP_COMMIT` gates only the second, so a stop fixture gets the real stop route
// without the playback closure.
#if !defined(A34A_LINKS_REAL_TRACK_TRANSPORT)
void Track::stopPlaying() { OFF_PATH(); }
void Track::startPlaying(uint32_t, bool) { OFF_PATH(); }
void Track::togglePlayStop() { OFF_PATH(); }
#endif
#if !defined(A34A_LINKS_REAL_TRACK_STOP_COMMIT) && !defined(A34A_LINKS_REAL_TRACK_TRANSPORT)
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
const MidiEventVec& EditManager::editMidiEvents(const Track&) const { OFF_PATH(); static MidiEventVec empty; return empty; }
#if !defined(A34A_LINKS_REAL_TRACK_STOP_COMMIT)
CommitResult Track::commitCaptureForStop(CommitReason, uint32_t, uint32_t, uint8_t) { OFF_PATH(); return {}; }
#endif
// A physical output boundary, not an unrelated domain route: silencing is reachable from both stop
// destinations, so failing closed here left the whole `Stopped`-destination deferred drain without any
// native coverage (see B03). Counting it instead keeps it observable -- a fixture can assert the track
// was silenced -- without pretending the silence itself is under test. Reset in setUp.
void Track::silenceTrackMidiOutput() { ++g_nativeSilenceTrackCalls; }
#if !defined(A34A_LINKS_REAL_TRACK_TRANSPORT)
void Track::clearQueuedPlaybackStart() { OFF_PATH(); }
void Track::queuePlaybackStartAtGrid(int32_t, uint32_t) { OFF_PATH(); }
void Track::commitQueuedPlaybackStart(uint32_t) { OFF_PATH(); }
#endif
// These two TrackManager owners are reached by the stop-commit closure, so they belong to that family:
// `setMasterLoopLength` from `stopRecordingTrack`/`finalizeCaptureAndSelectSlot`, and
// `reclaimUnreferencedDisabledPasses` from `startRecordingTrack`.
#if !defined(A34A_LINKS_REAL_TRACK_TRANSPORT) && !defined(A34A_LINKS_REAL_TRACK_STOP_COMMIT)
void TrackManager::setMasterLoopLength(uint32_t) { OFF_PATH(); }
void TrackManager::reclaimUnreferencedDisabledPasses(PassReclaimStats*, bool) { OFF_PATH(); }
#endif
// Storage-persistence requests. The broad transport macro keeps its historical meaning here too: a suite
// that enables it links the real `PersistenceAdmit.cpp` (the drain suite does), so these stand-ins stay off.
#if !defined(A34A_LINKS_REAL_STORAGE_PERSIST) && !defined(A34A_LINKS_REAL_TRACK_TRANSPORT) \
    && !defined(A34A_LINKS_REAL_TRACK_STOP_COMMIT)
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
// Emitted unless the telemetry TU itself is linked. Deliberately NOT suppressed by the broad transport
// macro, even though the stop-commit family also needs this symbol: a suite on that macro
// (`test_drain_reentry_callback_safety`) relies on this out-of-line definition, because its own
// `load_harness_stubs.h` declares an `inline` copy that `TRACK_INTERNAL_MEM` (`FLASHMEM`) prevents from being
// emitted as an external symbol. The stop-commit suite links the real telemetry owners instead.
#if !defined(A34A_LINKS_REAL_TRACK_STOP_TELEMETRY)
uint8_t resolveTrackIndexForPersistence(const Track& track) {
  for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i)
    if (&trackManager.getTrack(i) == &track) return i;
  return Config::INVALID_TRACK_INDEX;
}
#endif
#if !defined(A34A_LINKS_REAL_EDIT_SESSION)
void EditManager::onSelectedSlotChanged(Track&, uint8_t) { OFF_PATH(); }
void EditManager::beforeSelectedSlotChange(Track&) { OFF_PATH(); }
#endif
// A switch that commits invalidates the display cache. The display is not the behavior under test, so
// suites that assert a committed switch keep this observable.
#if defined(HARNESS_OBSERVABLE_SLOT_CHANGE)
void DisplayManager::invalidateForSlotChange(uint8_t, uint8_t, uint8_t) {}
#else
void DisplayManager::invalidateForSlotChange(uint8_t, uint8_t, uint8_t) { OFF_PATH(); }
#endif
// A suite that links the real StorageManager load path (which defines these two) opts out with
// `HARNESS_LINKS_LOOP_SLOT_PAYLOAD_ON_SD`; everything else keeps the fail-closed route.
#if !defined(HARNESS_LINKS_LOOP_SLOT_PAYLOAD_ON_SD) && !defined(A34A_LINKS_REAL_STORAGE_PERSIST) \
    && !defined(A34A_LINKS_REAL_TRACK_TRANSPORT)
bool StorageManager::hasLoopSlotPayloadOnSdInRam(uint8_t, uint8_t) { OFF_PATH(); return false; }
void StorageManager::refreshLoopSlotPayloadOnSdInRam(uint8_t, uint8_t) { OFF_PATH(); }
#endif
#if !defined(A34A_LINKS_REAL_STORAGE_PERSIST) && !defined(A34A_LINKS_REAL_TRACK_TRANSPORT) \
    && !defined(A34A_LINKS_REAL_TRACK_STOP_COMMIT)
void StorageManager::prioritizeLoopSlotRestoreForFocus(uint8_t, uint8_t) { OFF_PATH(); }
#endif
#if !defined(A34A_LINKS_REAL_STORAGE_PERSIST) && !defined(A34A_LINKS_REAL_TRACK_TRANSPORT) \
    && !defined(A34A_LINKS_REAL_TRACK_STOP_COMMIT)
void StorageManager::requestWorkspaceFooterPersistWhenSafe() { OFF_PATH(); }
#endif
// The stop-commit route silences the wire through this call. It lives in `TrackPlaybackHotPath.cpp` beside
// the MIDI-silence boundary above, and this suite deliberately keeps that whole TU out -- its silence owners
// are what B03 counts. The stand-in is therefore part of the existing silencing boundary: the wire side is
// not under test here. Its per-slot ledger sweep is what the stand-in omits, so a fixture that depends on the
// ledger being cleared by a stop would have to link the playback TU and rework B03. Gated on the stop-commit
// family (not the transport one) because that is the family that newly makes this call reachable; a suite on
// the broad transport macro supplies its own, as the drain suite does.
// Ungated at HEAD and kept that way: every suite gets this definition, because the playback TU that owns it
// is not linked anywhere in this boundary set.
void Track::silenceSlotMidiOutput(uint8_t) { OFF_PATH(); }
// New with the stop-commit family: the real stop-commit route reaches the wire-silence call, and its owner
// lives in `TrackPlaybackHotPath.cpp` beside the MIDI-silence boundary above -- whose silence owners are what
// B03 counts. Linking that TU would replace them, so this is an inert stand-in and the wire side is not under
// test. What it omits is precisely the per-slot ledger/LED clearing: a fixture that depends on *that* would
// have to link the playback TU and rework B03's instrumentation.
#if defined(A34A_LINKS_REAL_TRACK_STOP_COMMIT)
void Track::sendAllNotesOff() {}
// The real stop route persists the slot it just committed. These three are **inert by configuration**, not
// by stubbing: `src/StorageManager/PersistenceAdmit.cpp` compiles each of them to an immediate `return` when
// `BYPASS_STOP_UNDO_SAVE` is set, and the native env sets it (`platformio.ini`, the diagnostic flag that
// skips the deferred runtime save and the undo snapshot copy on a record/overdub stop). So these match the
// production behaviour this suite runs under, and enabling the stop-commit family does not make this suite
// exercise the persistence path.
void StorageManager::markLoopSlotMaterialDirty(uint8_t, uint8_t) {}
void StorageManager::admitLoopSlotPersist(uint8_t, uint8_t) {}
void StorageManager::requestDeferredSaveState(const LooperState&, uint32_t, bool) {}
// The selection route asks storage to prioritize the focused slot's restore, and the switch path asks for a
// workspace footer persist. Both are inert here for the same reason as the three above: their owners are not
// linked in this boundary set, and the fixture is about the dispatcher's same-tick guard, not about restore
// priority or footer persistence.
void StorageManager::prioritizeLoopSlotRestoreForFocus(uint8_t, uint8_t) {}
void StorageManager::requestWorkspaceFooterPersistWhenSafe() {}
#endif
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
