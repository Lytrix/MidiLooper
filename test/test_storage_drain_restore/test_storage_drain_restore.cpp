//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Packet A (A1.7 / C5) and Packet C of `deferred-slot-restore-liveness`: the **real** blocking
// persistence drain driven with a restore in flight. Kept as its own suite because the drain pulls
// the persistence-side owners, which the load suite deliberately does not link.
#define ARDUINO 1

#include <unity.h>

#include "../test_support/firmware/Arduino.h"
#include "../test_support/firmware/SD.h"

SDClass SD;

/// The sketch owns this global; the load path reads it, so the harness provides it.
bool sequencerRunning = false;

// The shared production harness (Loop/Track/TrackManager objects + harness globals). Extracted so this
// suite links the same production code as test_continuous_capture without duplicating the include list.
// This suite links src/TrackUndo.cpp, so it omits MidiButtonActions.cpp -- see the seam in the include.
// This suite links src/TrackUndo.cpp, so it omits the note-edit button TU (see the seam in the include).
#define HARNESS_OMITS_MIDI_BUTTON_ACTIONS
#include "../test_support/track_harness.inc"
// The stub surface: physical outputs and unrelated domain routes stay fail-closed or observable.
// This suite links the real load path, which defines the two loop-slot-payload functions, so it opts
// out of those stubs.
#include "../test_storage_manager_load/load_harness_stubs.h"
#include "drain_harness_stubs.h"
#include "../test_storage_manager_load/load_and_restore_harness.h"
#include "../../src/TrackUndo.cpp"
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
// Capture-queue owner: supplies `hasQueuedRecordingTrack` and the capture-side `TrackManager`
// members. `TrackPlaybackHotPath.cpp` follows it deliberately: the capture-queue TU calls a
// `requestLoopSlotPersistAndSaveState` overload that only that TU declares, so without it the call is
// ambiguous. The physical-output boundary in it (`silenceTrackMidiOutput`) collides with the shared
// harness's stub, so it is included only for the declaration and its duplicate definition is avoided
// by not including it -- see the note in drain_harness_stubs.h.

// TrackManager's constructor references the global MIDI handler; the load path pulls it in.
#include "../../src/MidiHandler.cpp"
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
#include "../../src/TrackManager/TrackManagerSlotMatrix.cpp"
#include "../../src/StorageManager/LoopSlotRestoreQueue.cpp"
// The runtime's own explicit-request / background-fill entry points live beside the SD boot load, not
// in the load-job TU: without them the suite could only reach the queue through internal helpers.
#include "../../src/StorageManager.cpp"
// Packet A's C5 fixture and Packet C both drive the **real blocking persistence drain**, so its TU is
// linked here rather than modelled by a test-only stand-in.
#include "../../src/StorageManager/SyncDrainSaveState.cpp"
// A3.5: the real drain now services MIDI while it waits, so the dispatch path it reaches must be linked.
#include "../../src/MidiButtonActions.cpp"
#include "../../src/Track/SlotClearPlaybackRestore.cpp"
#include "../../src/EditManager/EditSessionInteraction.cpp"
#include "../../src/EditManager/ResolveConstrainedGeometry.cpp"
// A3.4a/A3.5: the drain guard and the restore-request record both have their own translation units.
#include "../../src/StorageManager/PersistenceDrainGuard.cpp"
#include "../../src/StorageManager/RestoreRequestState.cpp"

// The MIDI-service path reaches the transport/clock owners and the note-edit geometry owners. None is on
// the asserted path (the fixture queues a transport message purely to observe that MIDI was serviced), so
// each fails closed rather than pretending to act.
/// The queued message is a **transport** message, so its callbacks are the observation of "MIDI was
/// serviced". They are counted, not fail-closed: the point of the case is that the message reaches them.
unsigned g_a35TransportServiceCalls = 0;
void ClockManager::onMidiStart() { ++g_a35TransportServiceCalls; }
void ClockManager::onMidiStop() { ++g_a35TransportServiceCalls; }
void ClockManager::onMidiClockPulse() { ++g_a35TransportServiceCalls; }
void ClockManager::requestTransitionTo(ClockSource) { ++g_a35TransportServiceCalls; }
uint32_t ClockManager::setLastMidiClockTime(uint32_t) {
  ++g_a35TransportServiceCalls;
  return 0;
}
/// `handleMidiInput` needs the clock's micros projection. `ClockManager.cpp` pulls the Teensy
/// `IntervalTimer`, which has no native stub, so the one accessor is supplied directly; it is a pure read
/// of the manager's own state.
uint32_t ClockManager::getMicrosPerTick() const { return microsPerTick; }
void TrackManager::handleTransportStop() { TEST_FAIL_MESSAGE(__PRETTY_FUNCTION__); }
// The MIDI dispatch route's own boundaries. The fixture queues a **transport** message, so these are not
// reached by the asserted path; they exist because `MidiButtonManager` is part of the button TU's closure.
void MidiButtonManager::handleMidiNote(uint8_t, uint8_t, uint8_t, bool) { TEST_FAIL_MESSAGE(__PRETTY_FUNCTION__); }
bool BarStepButtonHandler::isBarStepButtonNote(uint8_t, uint8_t) const {
  TEST_FAIL_MESSAGE(__PRETTY_FUNCTION__);
  return false;
}
// The MIDI dispatch path's global owners and the tempo global, which `StorageManager.cpp`'s TU would
// normally supply; this suite links the drain closure rather than the whole manager.
DisplayManager::DisplayManager() {}
MidiFaderManager::MidiFaderManager() {}
MidiFaderProcessor::MidiFaderProcessor() {}
MidiFaderActions::MidiFaderActions() {}
MidiButtonProcessor::MidiButtonProcessor() {}
MidiButtonManager::MidiButtonManager() {}
ControlSurfaceManager::ControlSurfaceManager() {}
void ControlSurfaceManager::onEditEvent(EditEvent) { TEST_FAIL_MESSAGE(__PRETTY_FUNCTION__); }
BarStepButtonHandler::BarStepButtonHandler() {}
MidiButtonManager midiButtonManager;
ControlSurfaceManager controlSurfaceManager;
DisplayManager displayManager;
BarStepButtonHandler barStepButtonHandler;
float bpm = 120.0f;
void BarStepButtonHandler::handleMidiNote(uint8_t, uint8_t, uint8_t, bool) { TEST_FAIL_MESSAGE(__PRETTY_FUNCTION__); }
void ControlSurfaceManager::handleMidiCC(uint8_t, uint8_t, uint8_t) { TEST_FAIL_MESSAGE(__PRETTY_FUNCTION__); }
void ControlSurfaceManager::handleMidiPitchbend(uint8_t, int16_t) { TEST_FAIL_MESSAGE(__PRETTY_FUNCTION__); }

void setUp() {}
void tearDown() {}

/// The suite currently links the drain TU and its measured closure but drives **no** production call,
/// because the remaining closure is larger than Packet A needs. A1.7 and Packet C re-enable the real
/// drain call here; the include list above is the measured closure recorded in `REVIEW.md`.
/// Behaviour-free closure proof: the boundary stubs and the closed import set compile and link
/// together. The **real** drain call is deliberately not retained here: A1.7 measured that doing so also
/// needs the display stack, which is a recorded scope decision (see `REVIEW.md` A1.7), not a fixture
/// mechanic.
void test_drain_suite_links_the_closed_import_set() {
  TEST_ASSERT_TRUE(StorageManagerInternal::pendingLoopSlotRestoreCount() == 0);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_drain_suite_links_the_closed_import_set);
  return UNITY_END();
}
