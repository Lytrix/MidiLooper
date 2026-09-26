#pragma once
// The fixture is stopped, outside note/loop edit, with no live capture or clear
// gesture. These transitive routes must not be exercised by recovered-pass undo.
#define RECOVERY_OFF_PATH() TEST_FAIL_MESSAGE(__PRETTY_FUNCTION__)
const char* commitResultLabel(CommitResult) { RECOVERY_OFF_PATH(); return "off_path"; }
void logOverdubStopStage(const Loop&,uint32_t,const char*,uint32_t,uint32_t,uint32_t,const char*,const StopPathStorageStats*,const PendingCapturePass*) { RECOVERY_OFF_PATH(); }
void logMemoryAfterOverdubStop(uint32_t,const Loop&) { RECOVERY_OFF_PATH(); }
void emitOverdubStopDisplaySnapshot(Track&,uint8_t,uint32_t) { RECOVERY_OFF_PATH(); }
void requestActiveLoopSlotPersist(Track&,uint8_t) { RECOVERY_OFF_PATH(); }
void requestLoopSlotPersistAndSaveState(Track&,uint8_t,uint32_t) { RECOVERY_OFF_PATH(); }
#include "../../src/TrackManager/TrackManagerInternalHelpers.cpp"
void EditManager::beforeSelectedSlotChange(Track&) { RECOVERY_OFF_PATH(); }
void EditManager::onSelectedSlotChanged(Track&,uint8_t) { RECOVERY_OFF_PATH(); }
void EditManager::bumpSessionPreviewRevision() { RECOVERY_OFF_PATH(); }
void EditManager::bumpSessionPlaybackPreviewRevision() { RECOVERY_OFF_PATH(); }
void EditManager::scheduleDeferredNoteEditDisplayRefresh() { RECOVERY_OFF_PATH(); }
void DisplayManager::emitDisplayCaptureSnapshot(const Track&,uint8_t,uint32_t) { RECOVERY_OFF_PATH(); }
void DisplayManager::invalidateLiveDisplayCache(bool) { RECOVERY_OFF_PATH(); }
void DisplayManager::refreshViewportAfterOverdubStop(Track&,uint8_t,uint32_t) { RECOVERY_OFF_PATH(); }
void Track::stopPlaying() { RECOVERY_OFF_PATH(); }
void Track::startPlaying(uint32_t,bool) { RECOVERY_OFF_PATH(); }
void Track::sendAllNotesOff() { RECOVERY_OFF_PATH(); }
void Track::stopRecordingToStopped(uint32_t) { RECOVERY_OFF_PATH(); }
CommitResult Track::commitCaptureForStop(CommitReason,uint32_t,uint32_t,uint8_t) { RECOVERY_OFF_PATH(); return CommitResult::SealFailed; }
// The UI owns no pending geometry in this fixture. Geometry restore's callback
// is a no-op in production outside loop-edit mode; assert that precondition.
LoopEditManager::LoopEditManager(MidiHandler& handler):midiHandler(handler) {}
LoopEditManager loopEditManager(midiHandler);
bool LoopEditManager::hasPendingGeometry() const {
  return hasPendingLoopStartTick_ || loopStartSettleUntilMs_ != 0 ||
         pendingLoopLengthTicks_ != 0 || loopLengthSettleUntilMs_ != 0;
}
void LoopEditManager::flushAllPendingGeometry(Track&) { RECOVERY_OFF_PATH(); }
void LoopEditManager::cancelPendingGeometryPreview(Track&) { RECOVERY_OFF_PATH(); }
void LoopEditManager::onGlobalGeometryRestored(Track&) {
  TEST_ASSERT_EQUAL_UINT8(255,sessionSlot_);
}
#undef RECOVERY_OFF_PATH
