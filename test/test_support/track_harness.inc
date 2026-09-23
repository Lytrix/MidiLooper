// Shared native harness include block: the production objects the Track/Loop/TrackManager fixtures need.
//
// Extracted verbatim from `test_continuous_capture` so the StorageManager load harness
// (`test_storage_manager_load`) can link the same production code without duplicating this list. The
// include order is significant (a `#define` for LoopCaptureTestDeps, and the runtime owners before the
// objects they serve), so it is moved as a block rather than reorganised.
//
// Suites including this also need: the ClockManager/EditManager global definitions, `off_path_deps.h`,
// and `test/test_support/firmware/{Arduino,SD}.h` on the include path (see scripts/native_test_link.py).

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/EditManager/EditApply.cpp"
#include "../../src/Loop/LoopPasses.cpp"
#include "../test_support/MemoryMonitorNativeDeps.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#define LOOP_PENDING_NOTE_CHANGE_PROVIDES_DISPLAY_APPLY
#include "../test_support/LoopCaptureTestDeps.cpp"
#include "../../src/Loop.cpp"
#include "../../src/Track/RuntimePlaybackView.cpp"
#include "../../src/Track/RuntimePlaybackOrderBuilder.cpp"
#include "../../src/Track/RuntimePlaybackBridge.cpp"
#include "../../src/Track/RuntimeCaptureCompanions.cpp"
#include "../../src/Loop/LoopPreparedCompanionHistory.cpp"
#include "../../src/Track/RuntimeNotePairLookup.cpp"
#include "../../src/Track/RuntimeCompanionOverlapWalk.cpp"
#include "../../src/Track/RuntimeCompanionSpanWalk.cpp"
#include "../../src/Track/RuntimeCompanionReconstruction.cpp"
#include "../../src/Track/RuntimeCompanionApply.cpp"
#include "../../src/Track/RuntimeCompanionBridgeMap.cpp"
#include "../../src/Track/RuntimeCompanionPasses.cpp"
#include "../../src/Track/RuntimeCanonicalPassInputs.cpp"
#include "../../src/Utils/PlaybackCursorAdvance.cpp"
#include "../../src/Track/RuntimePlaybackViewFill.cpp"
#include "../../src/Track/RuntimePlaybackViewBuild.cpp"
#include "../../src/Track/TrackPlaybackRuntime.cpp"
#include "../../src/StorageManager/PersistedLoopSnapshotRelease.cpp"
#include "../../src/PassReclaim.cpp"
#include "../../src/Track/TrackUndoRetirement.cpp"
#include "../../src/Track/TrackUndoPublication.cpp"
#include "../../src/Track/TrackPreparedStopCommit.cpp"
#include "../../src/Track/TrackPreparedUndo.cpp"
#include "Loop.h"
#include "TrackPlaybackRuntime.h"
#include "TrackUndoPublication.h"
#include "Utils/MidiServiceDrain.h"


#include "../../src/Loop/LoopPool.cpp"
#include "../../src/SlotStateMachine.cpp"
#include "../../src/MidiLedManager.cpp"
#include "../../src/CaptureAppendResult.cpp"
#include "../../src/Track.cpp"
#include "../../src/Track/TrackStateMachine.cpp"
#include "../../src/Track/TrackStateFacade.cpp"
#include "../../src/Track/TrackSlotPool.cpp"
#include "../../src/Track/TrackCaptureInput.cpp"
#include "../../src/Track/TrackPlaybackWindowBuild.cpp"
#include "../../src/Track/TrackLoopJamGeometry.cpp"
#include "../../src/Track/TrackOverdubLifecycle.cpp"
#include "../../src/Track/TrackOverdubWrap.cpp"
#include "../../src/Track/TrackDeferredMaintenance.cpp"
#include "../../src/Utils/CommittedMidiScratch.cpp"
// Record-arm admission now runs on this suite's route, so the derived-cache
// invalidation the admitted arm performs is production code here rather than an
// off-path stub (see off_path_deps.h).
#include "../../src/Track/TrackMidiEventRouting.cpp"
#include "../../src/TrackManager.cpp"

#include "../../src/MidiButtonActions.cpp"

#include "../../src/Utils/RuntimeTimingTelemetry.cpp"
#include "../../src/Utils/CommittedPlaybackLedgerCatchUp.cpp"

// --- harness globals and the native clock boundary -------------------------------
// Shared by every suite that includes this file. `nativeTransportRunning` is a test-controlled
// transport boundary (DEC-080) so a case can stop the transport and then drive the *production*
// retarget entry rather than only moving the fake clock; `setUp` in each suite resets it.
// `g_nativeSilenceTrackCalls` counts `Track::silenceTrackMidiOutput()`, which `off_path_deps.h`
// defines as an observable output boundary instead of a fail-closed stub.
ClockManager::ClockManager() : pendingStart(false) {}
ClockManager clockManager;
EditManager::EditManager() {}
EditManager editManager;
LooperStateManager looperState;
uint32_t ClockManager::getCurrentTick() const { return currentTick; }
void ClockManager::assignCurrentTickSilently(uint32_t tick) { currentTick = tick; }
// P1 control (DEC-080): the transport-running boundary is test-controlled instead of
// hardcoded true, so a case can stop the transport and then drive the *production*
// retarget/epoch entry rather than only moving the fake clock. Reset in setUp.
bool nativeTransportRunning = true;
/// Counts `Track::silenceTrackMidiOutput()` (defined in `off_path_deps.h`, which is included much
/// later). Declared here so `setUp` can reset it.
unsigned g_nativeSilenceTrackCalls = 0;
bool ClockManager::isTransportRunning() const { return nativeTransportRunning; }
bool ClockManager::shouldQuantizeRecordStart() const { return true; }
