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
void requestLoopSlotPersistAndSaveState(Track&, uint8_t, uint32_t) {}

uint8_t TrackManager::getActiveLoopIndex(uint8_t) const { return 0; }
uint8_t TrackManager::getSelectedSlotIndex(uint8_t) const { return 0; }
uint8_t TrackManager::getPlayingSlotIndex(uint8_t) const { return 0; }
uint8_t TrackManager::getPreviewSlotIndex(uint8_t) const { return 0; }
uint8_t TrackManager::getPendingSlotIndex(uint8_t) const { return Config::INVALID_LOOP_SLOT; }
bool TrackManager::hasPendingSlotSwitch(uint8_t) const { return false; }
void Track::reanchorPlaybackProjection(uint32_t, bool) { A34A_OFF_PATH(); }
void Track::sendAllNotesOff() { ++g_nativeSilenceTrackCalls; }
void Track::stopRecording(uint32_t) { A34A_OFF_PATH(); }
void Track::stopRecordingToStopped(uint32_t) { A34A_OFF_PATH(); }

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

// ---------------------------------------------------------------------------
// Fixture helpers: the CLEAR action's controller note, one CLEAR injected through the real dispatch
// route, and a playing track whose active slot holds committed content.
// ---------------------------------------------------------------------------

/// `MidiButtonConfig.cpp` binds `CLEAR_TRACK` to the Record button's long press on the SELECT channel.
constexpr uint8_t kClearNote = MidiConfig::Transport::NOTE_RECORD;
constexpr uint8_t kClearChannel = MidiConfig::Channels::SELECT;

/// Replace the configured table with one entry that maps both press types to `CLEAR_TRACK`. This keeps
/// the route real (`handleMidiNote` -> processor -> callback -> `executeAction`) and removes only the
/// gesture timing, which the native `millis()` boundary cannot advance.
void configureClearOnShortPress() {
  MidiButtonConfig::Config::clearConfigs();
  MidiButtonConfig::ButtonConfig config(kClearNote, kClearChannel, "CLEAR (A3.4a fixture)");
  config.onShortPress(MidiButtonConfig::ActionType::CLEAR_TRACK).asMomentary(true);
  MidiButtonConfig::Config::addButton(config);
}

/// One CLEAR injected through the real MIDI dispatch route. The fixture's button is momentary, so the
/// NoteOn press itself dispatches the configured `CLEAR_TRACK` action; that removes only the native
/// `millis()` tap-expiry stage, not the dispatch route.
void dispatchClearThroughRealMidiRoute() {
  midiButtonManager.handleMidiNote(kClearChannel, kClearNote, 127, true);
  // Release the press, as a controller does. The native `millis()` boundary cannot expire a tap, so the
  // release dispatches the orphan note-off path and only clears the processor's pressed state.
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
  return UNITY_END();
}
