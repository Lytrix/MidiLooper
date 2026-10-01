#pragma once
// Shared real Track/undo/storage closure, extracted from the drain re-entry suite.
//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Packet C / A3.4a of `deferred-slot-restore-liveness`: **drain re-entry and callback safety**, driven
// through the **real** MIDI/controller dispatch route.
//
// The blocking drain services MIDI while it waits (design D4 rule 8, with rule 9 as its precondition),
// and MIDI service is a callback boundary, not a passive delay: a controller message reaches
// `MidiButtonProcessor` and executes a `MidiButtonActions` entry point immediately. This suite injects a
// CLEAR through that real route
// (`midiButtonManager.handleMidiNote` -> processor -> callback -> `executeAction` -> `handleClearTrack`)
// while the drain guard is held, and asserts the callback is refused **before any side effect**: no
// silence, no slot reset, the save still owed, and the guard still owned by its outer holder.
//
// The guard is the production owner (`StorageManagerInternal::PersistenceDrainScope`, the same scope the
// real drain acquires) and the callback is the real production button action. Every refusal assertion has
// a control case with the guard free, so "nothing happened" is demonstrably the guard's doing rather than
// a fail-closed route.
//
// The storage closure is the drain suite's measured set (the real blocking drain, the real slot reset and
// the real undo path). The `TrackManager` slot surface and the note-edit neighbours are boundary stubs:
// the slot flags are real state this fixture controls, and every other stub refuses loudly rather than
// masking a decision.
#define ARDUINO 1

#include <unity.h>

#include "../test_support/firmware/Arduino.h"
#include "../test_support/firmware/SD.h"

SDClass SD;

/// The sketch owns this global; the storage paths read it, so the harness provides it.
bool sequencerRunning = false;

// The shared harness. It omits `MidiButtonActions.cpp` (this suite includes it explicitly below) and the
// real TrackManager slot owners (this suite supplies the controllable slot surface).
#define HARNESS_OMITS_MIDI_BUTTON_ACTIONS
#include "../test_support/track_harness.inc"
// The boundary stub surface: physical outputs, the display/note-edit neighbours and the drain's cold
// boundaries stay fail-closed or observable. `off_path_deps.h` is the continuous-capture suite's measured
// set; the A34A_LINKS_* macros below replace only the stand-ins this suite needs real.
#define A34A_LINKS_REAL_MIDI_BUTTONS
#define A34A_LINKS_REAL_TRACK_TRANSPORT
#if defined(HARNESS_REAL_CAPTURE_STOP)
#define A34A_LINKS_REAL_TRACK_STOP_COMMIT
#define A34A_LINKS_REAL_STORAGE_PERSIST
#define A34A_LINKS_REAL_TRACK_STOP_TELEMETRY
#endif
// `off_path_deps.h` defines members of these types, so their declarations must be complete first: the
// continuous-capture suite gets them from the harness, which omits the button TU here.
#include "ControlSurfaceManager.h"
#include "MidiFaderActions.h"
#include "MidiFaderManager.h"
#include "MidiFaderProcessor.h"
#include "MidiButtonActions.h"
#include "../test_continuous_capture/off_path_deps.h"
#include "../test_storage_manager_load/load_and_restore_harness.h"

// `src/LoopEditManager.cpp` uses Arduino's `map()`, which the native Arduino stub does not provide.
inline long map(long x, long inMin, long inMax, long outMin, long outMax) {
    if (inMax == inMin) return outMin;
    return (x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}

// The real undo path, the real button actions and the real MIDI button route.
#include "../../src/TrackUndo.cpp"
#include "../../src/MidiButtonActions.cpp"
#include "../../src/MidiButtonProcessor.cpp"
#include "../../src/MidiButtonManager.cpp"
#include "../../src/Utils/MidiButtonConfig.cpp"
// The real Track transport and the real TrackManager slot/solo/LED owners: a refused CLEAR must be
// distinguishable from a CLEAR that could not act.
#include "../../src/Track/TrackTransportControl.cpp"
#include "../../src/Track/SlotClearPlaybackRestore.cpp"
#include "../../src/StorageManager/PersistenceDrainGuard.cpp"
// The real edit-owner methods the CLEAR/undo routes call.

// The drain suite's measured storage closure: the real blocking drain is the guard's owner.
#include "../../src/PersistenceSyncDrainBudget.cpp"
#include "../../src/StorageManager/PersistenceWorkQueue.cpp"
#include "../../src/StorageManager/DeferredSaveScheduler.cpp"
#include "../../src/PersistenceBudget.cpp"
#include "../../src/RevisionLoadPolicy.cpp"
#include "../../src/StorageManager/Internal.cpp"
#include "../../src/StorageManager/PersistenceAdmit.cpp"
#include "../../src/PersistenceFailurePolicy.cpp"
#include "../../src/StorageManager/RevisionLoad.cpp"
#include "../../src/StorageManager/RevisionCommit.cpp"
#include "../../src/StorageManager/MidPassChunkPersist.cpp"
#include "../../src/StorageManager/WallClockSdSync.cpp"
#include "../../src/StorageManager/PersistenceWorkItemPersist.cpp"
#include "../../src/StorageManager/SyncDrainBudget.cpp"
#include "../../src/PersistenceSchema.cpp"
#include "../../src/RevisionPackedBlob.cpp"
#include "../../src/SetRevisionCatalog.cpp"
#include "../../src/SavedSetCatalog.cpp"
#include "../../src/StorageManager/DeferredSaveJobStages.cpp"
#include "../../src/StorageManager/WorkspaceSave.cpp"
#include "../../src/StorageManager/RuntimeBundleFooter.cpp"
#include "../../src/StorageManager/CurrentSetBootLoad.cpp"
#include "../../src/StorageManager/LegacyMonolithLoad.cpp"
#include "../../src/StorageManager/SavedSetIo.cpp"
#include "../../src/Loop/LoopContentHistory.cpp"
#include "../../src/StorageManager/LoadedTrackStateApply.cpp"
#include "../../src/Track/TrackDisplayState.cpp"
#include "../../src/TrackManager/TrackManagerMidiLedFeedback.cpp"
#include "../../src/TrackManager/TrackManagerMemoryPressure.cpp"
#include "../../src/TrackManager/TrackManagerPlaybackPolicy.cpp"
#include "../../src/TrackManager/TrackManagerCaptureQueue.cpp"
#include "../../src/MidiHandler.cpp"
// `MidiHandler::handleMidiInput()` (A3.5's MIDI service during the holdoff wait) needs the clock's micros
// projection, which the continuous-capture boundary set does not stub.
// `ClockManager.cpp` pulls the Teensy `IntervalTimer`, which has no native stub; the one accessor
// `handleMidiInput` needs is a pure read of the manager's own state, so it is supplied directly.
uint32_t ClockManager::getMicrosPerTick() const { return microsPerTick; }
#include "../../src/StorageManager/CurrentSetStorage.cpp"
#include "../../src/StorageManager/FileIo.cpp"
#include "../../src/StorageManager/SlotReset.cpp"
#include "../../src/StorageManager/LoadLoopBudget.cpp"
#include "../../src/StorageLoopIo.cpp"
#include "../../src/StorageLoopIoBodyFormatV1.cpp"
#include "../../src/StorageManager/CurrentWorkspaceStorage.cpp"
#include "../../src/StorageManager/LoadLoopSelectionPolicy.cpp"
#include "../../src/StorageManager/SlotLoadAdmission.cpp"
#include "../../src/StorageManager/SlotLoadSession.cpp"
#include "../../src/StorageManager/LoadLoopJob.cpp"
#include "../../src/Loop/LoopPendingNoteChange.cpp"
#include "../../src/StorageManager/LoopSlotRestoreQueue.cpp"
#include "../../src/StorageManager/RestoreRequestState.cpp"
#include "../../src/TrackManager/TrackManagerInternalHelpers.cpp"
#include "../../src/EditManager/EditSessionInteraction.cpp"
// `LoopEditManager.cpp` defines `resolveTrackIndex` at namespace scope, which collides with the
// `TrackManagerInternalHelpers.cpp` copy already in this concatenated TU; rename this inclusion's copy.
#define resolveTrackIndex resolveTrackIndexA34aLoopEdit
#include "../../src/LoopEditManager.cpp"
#undef resolveTrackIndex
#include "../../src/EditManager/ResolveConstrainedGeometry.cpp"
#include "../../src/StorageManager.cpp"
#include "../../src/SetBrowserOverlayPolicy.cpp"
#include "../../src/ControlSurface/EditEventFeedback.cpp"
#include "../../src/StorageManager/SyncDrainSaveState.cpp"
// S2b.7: the blocking drain now services the restore it waits on, through the same scheduler entry the
// deferred-load frame uses, so the real drain needs that entry linked here.
#include "../../src/DeferredJobScheduler.cpp"

// The loop-edit geometry session is not on the CLEAR path; it fails closed if a fixture enters it.
#define A34A_OFF_PATH() TEST_FAIL_MESSAGE(__PRETTY_FUNCTION__)

// ---------------------------------------------------------------------------------
// Controllable boundary stubs for the two manager surfaces the CLEAR route calls. The slot flags are
// **real state this fixture owns**, so a non-refused CLEAR's effect on them is observable; the other
// methods are off the asserted path and fail closed if a fixture enters them.
// ---------------------------------------------------------------------------------
namespace A34aBoundary {
bool slotEnabled[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK] = {};
bool slotMuted[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK] = {};
/// Counts the CLEAR route's slot-mutation calls after the guard. A refused CLEAR must leave all of them
/// at zero: "the slot kept its content" is weaker than "the mutation calls never happened".
uint32_t slotMutationCalls = 0;
void resetSlotFlags() {
  slotMutationCalls = 0;
  for (uint8_t t = 0; t < Config::NUM_TRACKS; ++t) {
    for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
      slotEnabled[t][s] = false;
      slotMuted[t][s] = false;
    }
  }
}
}  // namespace A34aBoundary

bool TrackManager::isSlotEnabled(uint8_t trackIndex, uint8_t slotIndex) const {
  return A34aBoundary::slotEnabled[trackIndex][slotIndex];
}
bool TrackManager::isSlotMuted(uint8_t trackIndex, uint8_t slotIndex) const {
  return A34aBoundary::slotMuted[trackIndex][slotIndex];
}
void TrackManager::setSlotEnabled(uint8_t trackIndex, uint8_t slotIndex, bool enabled) {
  A34aBoundary::slotEnabled[trackIndex][slotIndex] = enabled;
}
void TrackManager::setSlotMuted(uint8_t trackIndex, uint8_t slotIndex, bool muted) {
  A34aBoundary::slotMuted[trackIndex][slotIndex] = muted;
}
void TrackManager::toggleSlotMuted(uint8_t trackIndex, uint8_t slotIndex) {
  A34aBoundary::slotMuted[trackIndex][slotIndex] = !A34aBoundary::slotMuted[trackIndex][slotIndex];
}
void TrackManager::setActiveLoopIndex(uint8_t, uint8_t) { ++A34aBoundary::slotMutationCalls; }
void TrackManager::setLayeredSlotHeld(uint8_t, uint8_t, bool) { ++A34aBoundary::slotMutationCalls; }
void TrackManager::cancelSlotSelectionHold(uint8_t) { ++A34aBoundary::slotMutationCalls; }
// S4b.5: the guarded entry's write half and the one cancel helper. The write is counted exactly as
// `setActiveLoopIndex` was (this harness asserts on `slotMutationCalls`), and the cancel is expressed through
// the three counted stubs below so a cancel still counts the same three slot-mutation calls it did before --
// the helper is one call site, not one mutation.
void TrackManager::applyActiveLoopIndex(uint8_t, uint8_t) { ++A34aBoundary::slotMutationCalls; }
void TrackManager::cancelPendingSlotSwitch(uint8_t trackIndex) {
  clearPendingSlotSwitch(trackIndex);
  setPendingEnabledSetReplacement(trackIndex, false);
  cancelSlotSelectionHold(trackIndex);
}
void TrackManager::clearPendingSlotSwitch(uint8_t) { ++A34aBoundary::slotMutationCalls; }
void TrackManager::refreshPreviewSlotFocus(uint8_t, uint8_t) { A34A_OFF_PATH(); }
void TrackManager::requestSlotSwitch(uint8_t, uint8_t, SlotQuantization, uint32_t) { A34A_OFF_PATH(); }
bool TrackManager::slotHasLoopContent(uint8_t, uint8_t, bool) { A34A_OFF_PATH(); return false; }

bool EditManager::sessionUndo(Track&) { A34A_OFF_PATH(); return false; }
bool EditManager::sessionRedo(Track&) { A34A_OFF_PATH(); return false; }
void EditManager::emitEditEvent(EditEvent) { A34A_OFF_PATH(); }
void EditManager::reopenNoteEditSession(Track&) { A34A_OFF_PATH(); }
void EditManager::persistActiveNoteEditSession(Track&) { A34A_OFF_PATH(); }
void EditManager::flushDeferredNoteEditDisplayRefresh(Track&) { A34A_OFF_PATH(); }
void EditManager::onTrackChanged(Track&) { A34A_OFF_PATH(); }
void EditManager::beforeSelectedTrackChange(Track&) { A34A_OFF_PATH(); }
void EditManager::revertNoteEditSessionForLoopClear(Track&) { ++A34aBoundary::slotMutationCalls; }
void ClockManager::resetToLoopStart() { A34A_OFF_PATH(); }

void TrackManager::setSelectedSlotIndex(uint8_t, uint8_t, SyncPlayback) { A34A_OFF_PATH(); }
void TrackManager::loadTransportSlotIndices(uint8_t, uint8_t, uint8_t) { A34A_OFF_PATH(); }
void TrackManager::setPendingEnabledSetReplacement(uint8_t, bool) { ++A34aBoundary::slotMutationCalls; }
void EditManager::foldLiveCaptureIntoNoteEditSession(Track&) { A34A_OFF_PATH(); }
void DisplayManager::handleLoadSaveOverlayPress(DisplayManager::LoadSaveOverlayPressType) { A34A_OFF_PATH(); }
void DisplayManager::invalidateLiveDisplayCache(bool) { A34A_OFF_PATH(); }
void DisplayManager::adjustLoadSaveListSelection(int) { A34A_OFF_PATH(); }
void DisplayManager::centerDetailedWindowOnPlayhead(Track&, uint8_t, uint32_t) { A34A_OFF_PATH(); }
void LooperStateManager::exitLoadSaveMode() { A34A_OFF_PATH(); }
void LooperStateManager::enterLoadSaveMode() { A34A_OFF_PATH(); }
void ControlSurfaceManager::handleExitEditMode(Track&) { A34A_OFF_PATH(); }
void ControlSurfaceManager::handleDeleteSelectedNote(Track&) { A34A_OFF_PATH(); }
void ControlSurfaceManager::handleCreateNoteAtBracket(Track&) { A34A_OFF_PATH(); }
void ControlSurfaceManager::handleDeleteOrCreateNoteAtBracket(Track&) { A34A_OFF_PATH(); }
void ControlSurfaceManager::handleCycleNoteEditType(Track&) { A34A_OFF_PATH(); }
// The capture-finalize route's persist request: not part of the CLEAR guard contract, so it is
// observable-but-inert rather than a hidden success.
#if !defined(HARNESS_REAL_CAPTURE_STOP)
void requestLoopSlotPersistAndSaveState(Track&, uint8_t, uint32_t) {}
#endif

uint8_t TrackManager::getActiveLoopIndex(uint8_t) const { return 0; }
uint8_t TrackManager::getSelectedSlotIndex(uint8_t) const { return 0; }
uint8_t TrackManager::getPlayingSlotIndex(uint8_t) const { return 0; }
uint8_t TrackManager::getPreviewSlotIndex(uint8_t) const { return 0; }
uint8_t TrackManager::getPendingSlotIndex(uint8_t) const { return Config::INVALID_LOOP_SLOT; }
bool TrackManager::hasPendingSlotSwitch(uint8_t) const { return false; }
#if defined(HARNESS_REAL_PLAYBACK_HOT_PATH)
#include "../../src/Track/TrackPlaybackHotPath.cpp"
#else
void Track::reanchorPlaybackProjection(uint32_t, bool) { A34A_OFF_PATH(); }
void Track::sendAllNotesOff() { ++g_nativeSilenceTrackCalls; }
#endif
#if !defined(HARNESS_REAL_CAPTURE_STOP)
void Track::stopRecording(uint32_t) { A34A_OFF_PATH(); }
void Track::stopRecordingToStopped(uint32_t) { A34A_OFF_PATH(); }
#endif

Loop& TrackManager::getSelectedLoop(Track& track) { return track.getActiveLoop(); }
const Loop& TrackManager::getSelectedLoop(const Track& track) const { return track.getActiveLoop(); }

/// `src/Globals.cpp` cannot be linked here (the harness already defines its other symbols), so the two
/// values this fixture needs are supplied directly. The RTC boundary is the same native stand-in the
/// drain suite uses (`RtcTime` reads imxrt registers, which do not exist on the host).
float bpm = 120.0f;
namespace {
uint32_t g_a34aWallClockSeconds = 1700000000u;
}  // namespace
namespace RtcTime {
uint32_t getUnixTime() { return g_a34aWallClockSeconds; }
void raiseWallClockToAtLeast(uint32_t value) {
  if (value > g_a34aWallClockSeconds) g_a34aWallClockSeconds = value;
}
}  // namespace RtcTime

// The remaining ControlSurface/EditManager members the real `EditEventFeedback` route reaches. None is
// on the CLEAR guard path; each fails closed if a fixture enters it.
void ControlSurfaceManager::requestFaderOutbound(NoteEditFaderOutbound::Trigger,
                                                 const NoteEditFaderOutbound::PlanFlags*) { A34A_OFF_PATH(); }
void ControlSurfaceManager::releaseEditedNoteAudition() { A34A_OFF_PATH(); }
void ControlSurfaceManager::drainFaderOutboundUntilIdle() { A34A_OFF_PATH(); }
void ControlSurfaceManager::releasePendingMotorNotegates() { A34A_OFF_PATH(); }
void ControlSurfaceManager::resetSelectNavSlotApplyState() { A34A_OFF_PATH(); }
void ControlSurfaceManager::clearPendingGeometryDriverMotorSync() { A34A_OFF_PATH(); }
void ControlSurfaceManager::clearPendingSelectDependentMotorSync() { A34A_OFF_PATH(); }
void ControlSurfaceManager::scheduleSelectDependentMotorSync(Track&, const EditorSelection&,
                                                             const EditorSelection&, bool) { A34A_OFF_PATH(); }
void EditManager::cycleEditSession(Track&) { A34A_OFF_PATH(); }
void EditManager::toggleLengthEditMode(Track&) { A34A_OFF_PATH(); }
void EditManager::clearLengthEditingModeOnNoteSelect() { A34A_OFF_PATH(); }

std::vector<NoteUtils::DisplayNote> EditManager::selectableDisplayNotesForEditUi(const Track&) const {
  A34A_OFF_PATH();
  return {};
}


#if defined(HARNESS_REAL_CAPTURE_STOP)
#include "../../src/Track/TrackCaptureStopCommit.cpp"
#include "../../src/Track/TrackStopTelemetryColdHelpers.cpp"
#include "../../src/Utils/RecordStopLength.cpp"
#endif
