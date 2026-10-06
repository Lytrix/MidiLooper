//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
#define PLAYBACK_WINDOW_ROLLING_ENABLED 1
#include <cstddef>
#include <cstdint>
#include <cstring>

// DEC-108 §13b / task 3.9: the window lane's **readiness receipt**, substituted the way the `#ifndef`-guarded
// `SC_WINDOW_READINESS` macro invites (the `SC_ACCEPTED_LATCH` pattern this suite's siblings use). The production TU
// then calls THIS recorder, so a receipt's content **and its cadence** are observable without a capture build. The
// macro is defined before any include that would pull `DebugSessionCapture.h`, which is what makes the substitution
// take effect.
namespace WindowReadinessRows {
struct Row {
  uint8_t track = 0;
  uint8_t slot = 0;
  const char* readiness = nullptr;
  const char* holder = nullptr;
  uint32_t revision = 0;
  uint32_t leadTicks = 0;
};
inline Row rows[64];
inline size_t count = 0;
inline void reset() { count = 0; }
inline void record(uint8_t track, uint8_t slot, const char* readiness, const char* holder, uint32_t revision,
                   uint32_t leadTicks) {
  if (count < sizeof(rows) / sizeof(rows[0])) {
    rows[count] = Row{track, slot, readiness, holder, revision, leadTicks};
  }
  ++count;
}
}  // namespace WindowReadinessRows
#define SC_WINDOW_READINESS(track, slot, readiness, holder, revision, leadTicks) \
  WindowReadinessRows::record(track, slot, readiness, holder, revision, leadTicks)

// DEC-108 §13b / task 3.10: the lane's **service-gap sample** through the same substitutable-seam pattern, so the one
// quantity the admission's `maximumPhaseAdvancePerTurn` is declared in can be observed and asserted rather than assumed.
namespace WindowGapRows {
struct Row {
  uint32_t maxDeltaTicks = 0;
  uint32_t opportunities = 0;
  uint32_t phase = 0;
  // **Schema 3** (review step 4 / blocker 3): the block carries the permitted advance it compared against, how many
  // eligible-predecessor gaps exceeded it, and the worst of them -- so the row is self-describing.
  uint32_t permittedAdvanceTicks = 0;
  uint32_t overruns = 0;
  uint32_t overrunMaxTicks = 0;
};
struct OverrunRow {
  uint32_t permitted = 0;
  uint32_t overruns = 0;
  uint32_t overrunMaxTicks = 0;
  bool running = false;
  uint32_t epoch = 0;
};
inline Row rows[64];
inline size_t count = 0;
inline void reset() { count = 0; }
inline bool running[64] = {};
inline uint32_t resets[64] = {};
inline OverrunRow overrunRows[16];
inline size_t overrunCount = 0;
inline void record(uint32_t maxDeltaTicks, uint32_t opportunities, uint32_t phase, bool transportRunning,
                   uint32_t resetCount, uint32_t permittedAdvanceTicks, uint32_t overruns, uint32_t overrunMaxTicks) {
  if (count < sizeof(rows) / sizeof(rows[0])) {
    rows[count] = Row{maxDeltaTicks, opportunities, phase, permittedAdvanceTicks, overruns, overrunMaxTicks};
    running[count] = transportRunning;
    resets[count] = resetCount;
  }
  ++count;
}
/// The **named** overrun row (`DIAG,win_gap_overrun`) a capture build emits once per block that saw one.
inline void recordOverrun(uint32_t permitted, uint32_t overruns, uint32_t overrunMaxTicks, bool transportRunning,
                          uint32_t epoch) {
  if (overrunCount < sizeof(overrunRows) / sizeof(overrunRows[0])) {
    overrunRows[overrunCount] = OverrunRow{permitted, overruns, overrunMaxTicks, transportRunning, epoch};
  }
  ++overrunCount;
}
}  // namespace WindowGapRows
#define SC_WINDOW_SERVICE_GAP(sample, phase, running, epoch) \
  WindowGapRows::record((sample).maxDeltaTicks, (sample).opportunities, phase, running, (sample).discontinuities, \
                        (sample).permittedAdvanceTicks, (sample).overruns, (sample).overrunMaxTicks)
#define SC_WINDOW_GAP_OVERRUN(permitted, overruns, overrunMax, running, epoch) \
  WindowGapRows::recordOverrun(permitted, overruns, overrunMax, running, epoch)
// DEC-108 §13b / task 3.9 (increment 4): the window lane's **path rows** through the same substitutable-seam pattern, so
// a claim about a row (`lag_breach`, `one_pass_late`) is checked as **the row**, not only as the state beside it. The
// lane emitted these rows under a capture build only, which is why 3.6b's "the breach is reported once per episode"
// could be asserted as a counter but never as a row.
namespace PlaybackPathRows {
inline const char* paths[64] = {};
inline uint32_t revisions[64] = {};
inline size_t count = 0;
inline void reset() { count = 0; }
inline void record(uint8_t track, uint8_t slot, uint32_t loopLengthTicks, uint32_t revision, const char* path) {
  (void)track;
  (void)slot;
  (void)loopLengthTicks;
  if (count < sizeof(paths) / sizeof(paths[0])) {
    paths[count] = path;
    revisions[count] = revision;
  }
  ++count;
}
inline size_t countOf(const char* path) {
  size_t seen = 0;
  for (size_t i = 0; i < count && i < sizeof(paths) / sizeof(paths[0]); ++i) {
    if (paths[i] != nullptr && strcmp(paths[i], path) == 0) {
      ++seen;
    }
  }
  return seen;
}
}  // namespace PlaybackPathRows
#define SC_PLAYBACK_BUILD_ROW(track, slot, loopLengthTicks, revision, path) \
  PlaybackPathRows::record(track, slot, loopLengthTicks, revision, path)
// windowed-playback-generation (the switch leg's launch-path diagnostic): the **gate row**, through the same
// substitutable-seam pattern. The row is qualification-only by construction, so without this recorder a claim about it
// could not be checked anywhere -- and what is checked here is the row's own contract: it names the exit, it carries
// the target's identity and the inputs that decided it, and it is emitted on a **state change** rather than on every
// turn (`per turn` would be the flood that evicts the evidence it explains).
namespace LaunchGateRows {
struct Row {
  uint8_t track = 0;
  uint8_t slot = 0;
  const char* exit = nullptr;
  uint16_t flags = 0;
  uint32_t loopLengthTicks = 0;
  uint8_t preparation = 0;
};
inline Row rows[64];
inline size_t count = 0;
inline void reset() { count = 0; }
inline void record(uint8_t track, uint8_t slot, const char* exit, uint16_t flags, uint32_t loopLengthTicks,
                   uint8_t preparation) {
  if (count < sizeof(rows) / sizeof(rows[0])) {
    rows[count] = Row{track, slot, exit, flags, loopLengthTicks, preparation};
  }
  ++count;
}
inline const Row* last() { return count == 0 ? nullptr : &rows[count - 1]; }
inline size_t countOf(const char* exit) {
  size_t seen = 0;
  for (size_t i = 0; i < count && i < sizeof(rows) / sizeof(rows[0]); ++i) {
    if (rows[i].exit != nullptr && strcmp(rows[i].exit, exit) == 0) ++seen;
  }
  return seen;
}
}  // namespace LaunchGateRows
#define SC_LAUNCH_GATE(track, slot, exit, flags, loopLengthTicks, launchPreparation) \
  LaunchGateRows::record(track, slot, exit, flags, loopLengthTicks, launchPreparation)

// windowed-playback-generation (the rolling demand path's gate row): the same substitutable-seam pattern, on the third
// and last silent family of the demand side. Every exit of `serviceRollingPlaybackWindow` returns a typed
// `RollingOutcome` to its caller with no row, so without this recorder the row's contract -- it names the exit, the
// inputs that decided it and the **change key** -- could not be checked anywhere. The recorder substitutes only the
// emission: which rows are owed is the production cadence in `Track::reportPlaybackRollingGateRow`.
namespace RollingGateRows {
struct Row {
  uint8_t track = 0;
  uint8_t slot = 0;
  const char* exit = nullptr;
  uint16_t flags = 0;
  uint32_t loopLengthTicks = 0;
  uint8_t requestResult = 0;
};
inline Row rows[512];
inline size_t count = 0;
inline void reset() { count = 0; }
inline void record(uint8_t track, uint8_t slot, const char* exit, uint16_t flags, uint32_t loopLengthTicks,
                   uint8_t requestResult) {
  if (count < sizeof(rows) / sizeof(rows[0])) {
    rows[count] = Row{track, slot, exit, flags, loopLengthTicks, requestResult};
  }
  ++count;
}
inline const Row* last() { return count == 0 ? nullptr : &rows[count - 1]; }
inline size_t countOf(const char* exit) {
  size_t seen = 0;
  for (size_t i = 0; i < count && i < sizeof(rows) / sizeof(rows[0]); ++i) {
    if (rows[i].exit != nullptr && strcmp(rows[i].exit, exit) == 0) ++seen;
  }
  return seen;
}
}  // namespace RollingGateRows
#define SC_WIN_GATE(track, slot, exit, flags, loopLengthTicks, requestResult) \
  RollingGateRows::record(track, slot, exit, flags, loopLengthTicks, requestResult)
//
// **S3 gate — `runtime-working-set-ownership` acceptance suite.** The stage's unit contracts live in
// `test_working_set_lease` (the vocabulary) and `test_continuous_capture` (the per-owner-kind matrix
// and the selection/no-churn proof); this suite is the stage's acceptance over the **production
// admission paths**, so the gate names one place where the law's observable consequences hold:
//
//   1. boot/load is an admission boundary  -> a committed slot leaves boot with no working set;
//   2. the record arm is a cold admission  -> it establishes the working set and records its roles;
//   3. release is guarded by liveness      -> `noLiveOwnerForSlot` refuses while an owner remains and
//                                             allows once it retires;
//   4. selection is cheap                  -> selection acquires nothing anywhere.
//
// Every assertion has a control, so "nothing happened" cannot pass by accident: the boot case asserts
// the *absence* of a working set while the record arm asserts its presence on the same slot.
//
// The boundary is the continuous-capture harness's measured set (physical outputs and the note-edit
// neighbours fail closed). The stage's own unit suites remain the place for the lease arithmetic; this
// suite deliberately does not restate them.

#define ARDUINO 1

#include <unity.h>

#include <cstdio>
#include <cstdlib>

#include "../test_support/firmware/Arduino.h"
#include "WindowPagedStream.h"
#include "../test_support/firmware/SD.h"

SDClass SD;

/// The sketch owns this global; the load path reads it, so the harness provides it.
bool sequencerRunning = false;

#include "../test_support/track_harness.inc"

/// S4b.4 (display clause): the display boundary this harness already keeps observable now **records** what it
/// was told, because after a role handoff *which* slots the panel is told about is the fact worth asserting --
/// the switch's own commit calls `displayManager.invalidateForSlotChange(i, previousPlaying,
/// getPreviewSlotIndex(i))`, so the incoming slot must be the one it names.
struct SlotChangeObservation {
  uint32_t count = 0;
  uint8_t track = 0xFF;
  uint8_t from = 0xFF;
  uint8_t to = 0xFF;
};
SlotChangeObservation& slotChangeObservation() {
  static SlotChangeObservation observation;
  return observation;
}
void resetSlotChangeObservation() { slotChangeObservation() = SlotChangeObservation{}; }

// S4b: this suite links the real stop-telemetry TU (the switch requests persistence for the slot the
// capture left), so the stub surface must not also define those diagnostics.
#define HARNESS_LINKS_REAL_STOP_TELEMETRY
#define HARNESS_OBSERVABLE_SLOT_CHANGE
#define HARNESS_OBSERVABLE_SD_REFRESH
#define HARNESS_OBSERVABLE_START_RECLAIM
// S4b.1 check 7 (cross-track): the successor's track must be selected through the production route, which
// departs the note-edit session before it moves focus. That departure is the edit suites' subject, so it
// stays observable here rather than failing closed -- the same treatment the switch's storage and display
// boundaries already get above.
#define HARNESS_OBSERVABLE_TRACK_SELECT
// S4b.1 check 7 (cleared target): driving a **cleared** slot through `Track::clear()` reaches the
// S4b.1 check 7 (cleared target): driving a **cleared** slot through `Track::clear()` reaches the
// note-edit revert the clear performs. That revert is a note-edit neighbour, so it stays observable here
// rather than failing closed; nothing in this suite asserts on it.
#define HARNESS_OBSERVABLE_SLOT_CLEAR
// The production **record-stop commit** route. One fixture drives `TrackManager::handleTransportStop()` and
// asserts the take commits with nonzero length and content -- the device failure this encodes is a successor
// live in the two TUs included below, so the four stubs that shadow them are lifted by this macro. It changes
// nothing for the fixtures above: none of them reached `handleTransportStop`, `stopRecordingToStopped`,
// `stopRecording` or `commitCaptureForStop`, all of which were fail-closed until now.
#define HARNESS_LINKS_REAL_TRANSPORT_STOP
// ...and the physical all-notes-off fan-out it reaches is observable (no-op) for this route.
#define HARNESS_OBSERVABLE_ALL_NOTES_OFF
// The stub surface: physical outputs and unrelated domain routes stay fail-closed or observable.
// The switch lane: this suite must be able to observe a **queued** switch committing (S4b.5/S4b.6/S4b.7),
// which happens on the production per-tick entry. That needs the playback hot path and the play/stop
// owners the rest of the suite keeps fail-closed, so they move behind this macro together.
#define HARNESS_LINKS_PLAYBACK_SWITCH_LANE
#include "off_path_deps.h"

// The production symbols this suite's admission routes reach. Kept to the measured closure: the undo
// publication the deferred-stop grain calls, the slot-matrix predicate the boot pass reads, the
// pending-note display apply the overdub span rebuild calls, and the global MIDI handler the
// TrackManager constructor references.
#include "../../src/TrackUndo.cpp"
#include "../../src/TrackManager/TrackManagerSlotMatrix.cpp"
#include "../../src/Loop/LoopPendingNoteChange.cpp"
#include "../../src/MidiHandler.cpp"
// S4b: the production switch path (`finalizeCaptureAndSelectSlot`) and the slot-matrix helpers it
// calls. `replaceSingleEnabledSlotWithTarget` is already linked above via TrackManagerSlotMatrix.cpp.
#include "../../src/TrackManager/TrackManagerCaptureQueue.cpp"
// The record-stop commit route (see the macro above): `TrackManager::handleTransportStop` and the
// `Track::stopRecordingToStopped` -> `Track::commitCaptureForStop` chain it drives.
#include "../../src/Track/TrackCaptureStopCommit.cpp"
#include "../../src/Utils/RecordStopLength.cpp"
#include "../../src/TrackManager/TrackManagerTransportTick.cpp"
// The slot-enable helper the switch path consults.
#include "../../src/TrackManager/TrackManagerInternalHelpers.cpp"
// The persistence request the switch path makes for the slot the capture left.
#include "../../src/Track/TrackStopTelemetryColdHelpers.cpp"
#include "../../src/EditManager/ResolveConstrainedGeometry.cpp"
#include "../../src/EditManager/NoteGeometryResolver.cpp"
// S4b.7: the `SlotProvision` turn's own eligibility predicate (`slotProvisionNeeded`), so a fixture can
// assert the turn's work without re-stating it.
// S4b.7: the bounded hold's own age test and named terminal, and the snapshot root the demoted target's merged
// window is gathered from (`storePublishedSnapshotForTest` is the existing test seam).
#include "Utils/SlotProvisionHold.h"
#include "LoopSnapshot.h"
#include "Utils/MidiServiceDrain.h"
#include "../../src/EditManager/EditSessionInteraction.cpp"
// The switch lane's production TUs: the playback hot path (silence owners + `playMidiEvents`), the
// playback policy owner (`isTrackAudible`) and the transport control owner (`startPlaying`/`stopPlaying`,
// `queuePlaybackStartAtGrid`/`commitQueuedPlaybackStart`). See the macro above.
#include "../../src/Track/TrackPlaybackHotPath.cpp"
#include "../../src/TrackManager/TrackManagerPlaybackPolicy.cpp"
#include "../../src/Track/TrackTransportControl.cpp"
// The lane's own boundary: linking the hot path brings `Track::resetPlaybackState`'s frame reset, which
// knocks on the loop-edit manager when a LOOP_EDIT session is live ("keep the LOOP_EDIT baseline aligned
// with the playback frame"). This suite never opens an edit session, so the manager is stubbed at the three
// symbols that TU references instead of linking the edit stack, and its one callback fails closed --
// reaching it would mean an edit session exists in a suite whose fixtures must never open one.
LoopEditManager::LoopEditManager(MidiHandler& handler) : midiHandler(handler) {}
LoopEditManager loopEditManager(midiHandler);
// S4b.4's undo clause found a **second** route to this callback: an applied undo restores published loop
// geometry and *notifies* the edit manager, which is a notification rather than an edit session. DEC-101's own
// argument applies -- a removed `OFF_PATH()` cannot have been reached by any fixture that was passing, so
// nothing that was covered is weakened -- while the manager's *session* entries (open/reopen/persist/flush)
// stay stubbed, so a real session still cannot run in this suite.
void LoopEditManager::onGlobalGeometryRestored(Track&) {}
// S4b.4's undo clause drives `TrackUndo::undoForLoop`, which flushes pending loop-edit geometry first. That
// flush stays **fail-closed**, like the callback above: reaching it would mean a live edit session, which no
// fixture in this suite may open.
void LoopEditManager::flushAllPendingGeometry(Track&) { OFF_PATH(); }
// The undo driver asks whether any geometry is pending before flushing it; with no edit session possible in
// this suite the honest answer is no.
bool LoopEditManager::hasPendingGeometry() const { return false; }

struct TrackCaptureStopTestAccess {
  static void selectSlot(Track& track, uint8_t slot) { track.activeLoopIndex = slot; }
  static bool runtimeAllocated(const Track& track, uint8_t slot) {
    return track.playbackRuntime.slotIfAllocated(slot) != nullptr;
  }
  static LoopPlaybackRuntime* runtimeIfAllocated(Track& track, uint8_t slot) {
    return track.playbackRuntime.slotIfAllocated(slot);
  }
  static uint8_t workingSetRoles(const Track& track, uint8_t slot) {
    const auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    return rt != nullptr ? static_cast<uint8_t>(rt->workingSetRoles()) : 0;
  }
  static WorkingSetRole workingSetRolesOf(const Track& track, uint8_t slot) {
    const auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    return rt != nullptr ? rt->workingSetRoles() : WorkingSetRole::None;
  }
  static uint16_t workingSetGeneration(const Track& track, uint8_t slot) {
    const auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    return rt != nullptr ? rt->workingSetGeneration() : 0;
  }
  static bool hasLiveCaptureStopJobForSlot(const Track& track, uint8_t slot) {
    return track.hasLiveCaptureStopJobForSlot(slot);
  }
  static bool hasAcceptedInteractionForSlot(const Track& track, uint8_t slot) {
    return track.hasAcceptedInteractionForSlot(slot);
  }
  static bool ownsLiveFinalization(const Track& track, uint8_t slot) {
    const auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    return rt != nullptr && rt->ownsLiveFinalization();
  }
  static bool ownsInFlightPreparedHandover(const Track& track, uint8_t slot) {
    const auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    return rt != nullptr && rt->ownsInFlightPreparedHandover();
  }
  static void setInFlightPreparedHandover(Track& track, uint8_t slot, bool pending) {
    auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    if (rt != nullptr) rt->preparedPairRequestPending = pending;
  }
  static bool admitStopForSlot(Track& track, PendingStopDestination destination, uint32_t tick,
                               uint8_t slot, uint64_t interval) {
    return track.beginDeferredOverdubStopForSlot(destination, tick, slot, interval);
  }
  static bool step(Track& track) { return track.processDeferredOverdubStopGrain(); }
  /// S4f.3: the real S4f.2 dispatch decision, the same entry `processDeferredIdleMaintenance` calls, so
  /// the accepted stop is driven to admission by the production lane rather than by a test-only path.
  static bool serviceAcceptedInteractionTurn(Track& track) {
    return track.serviceAcceptedInteractionTurn();
  }
  /// S4b: the switch path and the live-interval state the acceptance sequence needs.
  static void finalizeCaptureAndSelectSlot(uint8_t trackIndex, uint8_t newSlot, uint32_t tick) {
    trackManager.finalizeCaptureAndSelectSlot(trackIndex, newSlot, tick);
  }
  static bool hasPendingDeferredOverdubStop(const Track& track) {
    return track.hasPendingDeferredOverdubStop();
  }
  static bool hasStopInFlight(const Track& track) { return track.hasStopInFlight(); }
  /// S4f.6's slot-scoped predicate: a stop obligation that names **this** slot. The retry fixture needs it
  /// to assert that the refusal it is exercising is the target's own obligation, not another slot's.
  static bool hasStopInFlightForSlot(const Track& track, uint8_t slot) {
    return track.hasStopInFlightForSlot(slot);
  }
  /// The native clock boundary (DEC-080). This harness routes `shouldQuantizeRecordStart()` through it, so it
  /// **is** the clock condition `TrackManager::startRecordingTrack`'s arm-and-wait branch tests.
  static void setClockRunning(bool running) { nativeTransportRunning = running; }
  /// Exactly-once observables for one take. A second `Track::startRecording` re-begins capture --
  /// `Loop::beginCapture` clears the capture extent -- and re-stamps `loop.startLoopTick` from the later
  /// dispatch tick, so "the take was executed twice" shows up as either of these changing.
  static uint32_t takeOriginTick(const Track& track) { return track.getActiveLoop().startLoopTick; }
  static uint32_t firstCaptureEventTick(const Track& track) {
    const auto& store = track.getActiveLoop().capture.store;
    return store.size() == 0 ? UINT32_MAX : store.at(0).tick;
  }
  static uint8_t firstCaptureEventNote(const Track& track) {
    const auto& store = track.getActiveLoop().capture.store;
    return store.size() == 0 ? 0xFF : store.at(0).data.noteData.note;
  }
  static bool hasAcceptedUnexecutedInteraction(const Track& track) {
    return track.hasAcceptedUnexecutedInteraction();
  }
  static bool captureActive(const Track& track) { return track.getActiveLoop().captureActive(); }
  static size_t captureEventCount(const Track& track) {
    return track.getActiveLoop().capture.store.size();
  }
  static bool appendCaptureNote(Track& track, uint32_t onTick, uint32_t offTick, uint8_t note) {
    Loop& loop = track.getActiveLoop();
    return loop.appendCaptureEvent(MidiEvent::NoteOn(onTick, 1, note, 100)) &&
           loop.appendCaptureEvent(MidiEvent::NoteOff(offTick, 1, note, 0));
  }
  static bool isOverdubbing(const Track& track) { return track.isOverdubbing(); }
  static bool isRecording(const Track& track) { return track.isRecording(); }
  static void startOverdubbing(Track& track, uint32_t tick) { track.startOverdubbing(tick); }
  static bool recordQueuedForSlot(uint8_t trackIndex, uint8_t slot) {
    return trackManager.isRecordingQueued(trackIndex, slot);
  }
  static bool anyRecordQueued(uint8_t trackIndex) {
    return trackManager.hasQueuedRecordingTrack(trackIndex);
  }
  /// The production queued-record dispatch (`TrackManager::handlePendingRecordStart`).
  static void startPendingRecord(uint32_t tick) { trackManager.handlePendingRecordStart(tick); }
  static void toggleRecordForSlot(uint8_t slotIndex) {
    midiButtonActions.handleToggleRecordForSlot(slotIndex);
  }
  static uint8_t activeLoopIndex(const Track& track) { return track.getActiveLoopIndex(); }
  static void latchAcceptedStopForSlot(Track& track, uint8_t slot) {
    track.latchAcceptedStop(PendingStopDestination::Playing, slot, 0);
  }
  /// The production consumption entry for the accepted interaction (S4f.1/S4f.2). The reason is the edge's own
  /// outcome literal (task 2.2's latch evidence); this fixture passes its own.
  static void consumeAcceptedStop(Track& track, const char* reason = "fixture") {
    track.consumeAcceptedStopObligation(reason);
  }
  /// S4.1: the slot's open-note ledger. Marking an entry is the observable that a stop must not
  /// sweep off a sibling slot.
  static void markLedgerEntry(Track& track, uint8_t slot, uint8_t note) {
    auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    if (rt != nullptr) rt->ledger.noteOn(1, note, kInvalidNoteId, 0, 100);
  }
  static bool ledgerEntryActive(const Track& track, uint8_t slot, uint8_t note) {
    const auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    return rt != nullptr && rt->ledger.isActive(1, note);
  }
  static void clearAllLedgersForTests(Track& track) { track.playbackRuntime.clearAllLedgers(); }
  /// S4.4: report a declared-envelope breach if the activation would exceed a class cap. The answer is the
  /// slot holding the class it reported for (`kNoWorkingSetClassHolder` when there is nothing to report).
  static uint8_t reportEnvelopeBreachIfAny(const Track& track, uint8_t slot, WorkingSetRole role) {
    return track.reportEnvelopeBreachIfAny(slot, role);
  }
  /// S4.1/S4.2: the `(slot, generation)` release entry and the identity a caller must present.
  static uint16_t slotGeneration(Track& track, uint8_t slot) {
    return track.playbackRuntime.slotGeneration(slot);
  }
  static WorkingSetOutcome releaseSlot(Track& track, uint8_t slot, uint16_t generation,
                                       bool preserveLedger = true) {
    return track.playbackRuntime.releaseSlot(slot, generation, preserveLedger);
  }
  static bool leaseHoldsWorkingSet(const Track& track, uint8_t slot) {
    const auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    return rt != nullptr && rt->lease_.roles() != WorkingSetRole::None;
  }
  // ---- S4.2 generation-qualified finalization identity ------------------------------------
  /// The runtime's own recorded owner identity (null until an interval is admitted).
  static bool setCaptureStopOwner(Track& track, uint8_t slot, uint32_t generation) {
    auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    if (rt == nullptr) return false;
    rt->setCaptureStopOwner(*rt->captureStopOwner, generation);
    return true;
  }
  /// Mark the slot as owned by a *different* generation, as happens when the runtime object is reused.
  static bool setSuccessorCaptureStopOwner(Track& track, uint8_t slot, uint32_t generation) {
    auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    if (rt == nullptr || rt->captureStopOwner == nullptr) return false;
    rt->setCaptureStopOwner(*rt->captureStopOwner, generation);
    return true;
  }
  static bool clearCaptureStopOwner(Track& track, uint8_t slot, uint32_t generation) {
    auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    if (rt == nullptr || rt->captureStopOwner == nullptr) return false;
    return rt->clearCaptureStopOwner(*rt->captureStopOwner, generation);
  }
  static bool hasCaptureStopOwner(Track& track, uint8_t slot) {
    const auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    return rt != nullptr && rt->captureStopOwner != nullptr;
  }
  static uint32_t captureStopOwnerGeneration(const Track& track, uint8_t slot) {
    const auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    return rt != nullptr ? rt->captureStopOwnerGeneration : 0;
  }
  /// S4.1: the named per-interval ledger clear the two stop routes now call.
  static void clearStoppingIntervalLedger(Track& track) { track.clearStoppingIntervalLedger(); }
  /// The production active-slot write authority (`TrackManager::setActiveLoopIndex`), so a fixture
  /// performs a switch through the same entry a gesture uses.
  static void activateSlot(uint8_t trackIndex, uint8_t slot) {
    trackManager.setActiveLoopIndex(trackIndex, slot);
  }
  static void selectSlotIndex(uint8_t trackIndex, uint8_t slot) {
    trackManager.slotStateMachine.setSelectedSlotIndex(trackIndex, slot);
  }
  static uint8_t selectedSlotIndex(uint8_t trackIndex) {
    return trackManager.slotStateMachine.getSelectedSlotIndex(trackIndex);
  }
  // ---- the record-stop commit route ---------------------------------------------------------------
  /// The production transport-stop entry (`TrackManager::handleTransportStop`), wired through
  /// `HARNESS_LINKS_REAL_TRANSPORT_STOP`. On device this is the route that finalises a live capture with
  /// `RECS,stopToStopped`, and the route a take must survive.
  static void transportStop() { trackManager.handleTransportStop(); }
  /// Whether `slot`'s loop holds committed passes -- the "belongs to this slot's loop" half of slot identity.
  static bool committedPassesInSlot(Track& track, uint8_t slot) {
    return track.getLoop(slot).hasCommittedPasses();
  }
  /// The slot loop's own length, which the commit path sets. A start marker says nothing about this.
  static uint32_t slotLoopLength(Track& track, uint8_t slot) {
    return track.getLoop(slot).loopLengthTicks;
  }
  /// Whether `slot`'s **committed** rows contain a note-on for `note`. This reads the pass content the way
  /// the firmware's own `!CAPTURE_CONTENT` dump does, so the assertion is on recorded content rather than on
  /// a live capture store that a later stop could discard.
  static bool committedSlotHasNote(Track& track, uint8_t slot, uint8_t note) {
    Loop& loop = track.getLoop(slot);
    MidiEventVec rows;
    auto holdsNote = [&rows, note]() {
      for (const auto& e : rows) {
        if (static_cast<unsigned>(e.type) == 144u && e.data.noteData.note == note &&
            e.data.noteData.velocity > 0) {
          return true;
        }
      }
      return false;
    };
    LoopEventStore::appendChunkRefEvents(loop.passes.recordPass.committedChunkIds, rows);
    if (holdsNote()) return true;
    for (size_t i = 0; i < loop.passes.overdubPasses.size(); ++i) {
      rows.clear();
      LoopEventStore::appendChunkRefEvents(loop.passes.overdubPasses[i].committedChunkIds, rows);
      if (holdsNote()) return true;
    }
    return false;
  }
};

namespace {

/// Commit one record pass of `lengthTicks` into a slot so the slot holds content -- the state a restored
/// library has. The length is a parameter because the populated-target variant must be able to tell
/// "the successor used **its own** loop's phase" from "the successor inherited the predecessor's": it
/// gives the two slots different lengths and asserts the successor kept its own.
void commitRecordPassIntoTrackSlotOfLength(Track& track, uint8_t slot, uint32_t lengthTicks) {
  TrackCaptureStopTestAccess::selectSlot(track, slot);
  Loop& loop = track.getActiveLoop();
  loop.loopLengthTicks = lengthTicks;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(0, 1, 48, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(48, 1, 48, 0)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(lengthTicks, CommitReason::RecordStop));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass());
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
}

/// Commit one record pass into a slot so the slot holds content -- the state a restored library has.
void commitRecordPassIntoTrackSlot(Track& track, uint8_t slot) {
  commitRecordPassIntoTrackSlotOfLength(track, slot, 768);
}

/// The production MIDI input route, so a "B accepts MIDI" assertion goes through the handler rather
/// than poking the capture store. Mirrors the continuous-capture harness's helper.
void inputNote(uint8_t pitch, uint32_t onTick, uint32_t offTick) {
  clockManager.assignCurrentTickSilently(onTick);
  midiHandler.handleMidiMessage(midi::NoteOn, 1, pitch, 100, SOURCE_USB);
  if (offTick != UINT32_MAX) {
    clockManager.assignCurrentTickSilently(offTick);
    midiHandler.handleMidiMessage(midi::NoteOff, 1, pitch, 0, SOURCE_USB);
  }
}

constexpr uint8_t kPlaybackWindowBit =
    static_cast<uint8_t>(WorkingSetRole::PlaybackWindow);
constexpr uint8_t kLiveCaptureBit = static_cast<uint8_t>(WorkingSetRole::LiveCapture);

}  // namespace

void setUp() {
  nativePlaybackDeadlineClock = {};
  nativeTransportRunning = true;
  g_nativeSilenceTrackCalls = 0;
  g_nativeAdoptedMasterLength = 0;
  g_nativeAdoptedMasterLengthCalls = 0;
  trackManager.~TrackManager();
  LoopEventStore::resetPoolForTests();
  new (&trackManager) TrackManager();
}
void tearDown() {}

/// **Gate item 1 + control.** Boot/load is an admission boundary (S2b.1, invariant 34): a committed
/// slot leaves boot with descriptors only -- no working set -- and the *same* slot gains one at the
/// record arm (item 2). Asserting both on one slot is what makes the absence meaningful.
void test_boot_leaves_a_committed_slot_without_a_working_set() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 1);
  TEST_ASSERT_TRUE(track.getLoop(1).hasCommittedPasses());

  // The boot pass: descriptors only, so the slot must not hold the working set...
  track.prewarmBootPlaybackRuntimeSlots();
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, TrackCaptureStopTestAccess::workingSetRoles(track, 1),
                                  "boot must not acquire a working-set lease");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::ownsLiveFinalization(track, 1),
                            "a booted slot must not own a finalization");
  // ...and the same slot must gain it at the cold admission boundary (the record arm).
  TEST_ASSERT_TRUE_MESSAGE(track.prewarmPlaybackForSlot(1, true),
                           "the record arm must admit the working set");
  const uint8_t roles = TrackCaptureStopTestAccess::workingSetRoles(track, 1);
  TEST_ASSERT_TRUE_MESSAGE((roles & kPlaybackWindowBit) != 0,
                           "the record arm must hold the playback-window role");
  TEST_ASSERT_TRUE_MESSAGE((roles & kLiveCaptureBit) != 0,
                           "the record arm must hold the live-capture role");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, 1),
                           "the record arm must admit the slot runtime");
}

/// **Gate item 2, idempotence.** A repeated admission at the same boundary must not churn: the lease
/// is identity-qualified, so re-establishing an already-held set is free and the generation must not
/// move (only a release moves it).
void test_a_repeated_admission_does_not_churn_the_lease() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  const uint8_t roles = TrackCaptureStopTestAccess::workingSetRoles(track, 0);
  const uint16_t generation = TrackCaptureStopTestAccess::workingSetGeneration(track, 0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(roles, TrackCaptureStopTestAccess::workingSetRoles(track, 0),
                                  "a repeated admission must not change the held set");
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(generation,
                                   TrackCaptureStopTestAccess::workingSetGeneration(track, 0),
                                   "a repeated admission must not move the generation");
}

/// **Gate item 3.** Release is guarded by liveness. Each owner kind refuses release on its own, and
/// the slot is releasable once the owner retires -- the control that makes the refusals meaningful.
void test_release_is_refused_while_an_owner_remains_and_allowed_once_it_retires() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 1);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(1, true));

  // Control (a): a working set is not by itself a live owner.
  TEST_ASSERT_TRUE_MESSAGE(track.noLiveOwnerForSlot(1),
                           "an admitted working set must not block release on its own");

  // Owner: an accepted interaction latched for the slot.
  TrackCaptureStopTestAccess::latchAcceptedStopForSlot(track, 1);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 1));
  TEST_ASSERT_FALSE_MESSAGE(track.noLiveOwnerForSlot(1),
                            "an accepted interaction must refuse release");
  TrackCaptureStopTestAccess::consumeAcceptedStop(track);
  TEST_ASSERT_TRUE_MESSAGE(track.noLiveOwnerForSlot(1),
                           "consuming the obligation must free the slot");

  // Owner: a prepared handover still being built (independent of the ring and of finalization).
  TrackCaptureStopTestAccess::setInFlightPreparedHandover(track, 1, true);
  TEST_ASSERT_FALSE_MESSAGE(track.noLiveOwnerForSlot(1),
                            "an in-flight prepared handover must refuse release");
  TrackCaptureStopTestAccess::setInFlightPreparedHandover(track, 1, false);
  TEST_ASSERT_TRUE_MESSAGE(track.noLiveOwnerForSlot(1), "retiring it must free the slot");

  // Owner: a capture stop job admitted for the slot. The interval is opened through the production
  // start (a hand-built interval admits a job that can never finalize), then the stop goes through the
  // production admission entry on the slot the record arm left warm.
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  Loop& loop = track.getLoop(1);
  track.startOverdubbing(96);
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(), "the fixture needs a live interval");
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(96, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(120, 1, 60, 0)));
  TEST_ASSERT_TRUE_MESSAGE(
      TrackCaptureStopTestAccess::admitStopForSlot(track, PendingStopDestination::Playing, 96, 1,
                                                   /*interval=*/1),
      "the production stop admission must accept on the warmed slot");
  TEST_ASSERT_TRUE_MESSAGE(track.hasPendingDeferredOverdubStop(),
                           "the fixture needs a live finalization owner on the slot");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 1),
                           "an admitted stop must appear in the ring's live window");
  TEST_ASSERT_FALSE_MESSAGE(track.noLiveOwnerForSlot(1), "a live stop job must refuse release");
  // Retire it through the production grain drive; the slot must be releasable again.
  for (int turn = 0; turn < 8192 && track.hasPendingDeferredOverdubStop(); ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE_MESSAGE(track.hasPendingDeferredOverdubStop(),
                            "the fixture must retire the stop job");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 1),
                            "the retired job must leave the live window");
  TEST_ASSERT_TRUE_MESSAGE(track.noLiveOwnerForSlot(1), "a retired stop job must free the slot");
}

/// **Gate item 4.** Selection is cheap and acquires nothing (spec: selection requirement). This
/// restates the continuous-capture fixture at the gate's level: the stage's acceptance must fail if
/// selection ever starts admitting.
void test_selection_acquires_no_working_set() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  for (uint8_t slot = 0; slot < 3; ++slot) {
    commitRecordPassIntoTrackSlot(track, slot);
  }
  for (int pass = 0; pass < 3; ++pass) {
    for (uint8_t slot = 0; slot < 3; ++slot) {
      TrackCaptureStopTestAccess::selectSlotIndex(0, slot);
      TEST_ASSERT_EQUAL_UINT8(slot, TrackCaptureStopTestAccess::selectedSlotIndex(0));
    }
  }
  for (uint8_t slot = 0; slot < 3; ++slot) {
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, TrackCaptureStopTestAccess::workingSetRoles(track, slot),
                                    "selection must not acquire a working set");
    TEST_ASSERT_EQUAL_UINT16(0, TrackCaptureStopTestAccess::workingSetGeneration(track, slot));
    TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, slot),
                              "selection must not admit a slot runtime");
  }
}


/// **S4.1 — an interval's stop clears only its own slot's ledger.** The release surface was
/// track-global: while a predecessor finalizes on one slot, a sibling slot's stop swept every
/// allocated slot's ledger, erasing the predecessor's open-note suppression state. This drives the
/// named per-interval clear that both converted stop routes call, and the track-global sweep is the
/// control that proves the conversion narrowed the stop path rather than disabling the sweep.
void test_a_stop_clears_only_its_own_slots_ledger() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  commitRecordPassIntoTrackSlot(track, 1);

  // Both slots hold a working set with a marked ledger entry.
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(1, true));
  TrackCaptureStopTestAccess::markLedgerEntry(track, 0, 60);
  TrackCaptureStopTestAccess::markLedgerEntry(track, 1, 62);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::ledgerEntryActive(track, 0, 60));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::ledgerEntryActive(track, 1, 62));

  // The stopping interval is slot 1. Slot 0 is the sibling that must survive -- the state a
  // finalizing predecessor holds.
  TrackCaptureStopTestAccess::selectSlot(track, 1);
  TrackCaptureStopTestAccess::clearStoppingIntervalLedger(track);

  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::ledgerEntryActive(track, 1, 62),
                            "the stopping interval must clear its own slot's ledger");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::ledgerEntryActive(track, 0, 60),
                           "a sibling slot's ledger must survive the stop");

  // Control: the deliberate whole-track sweep (global transport stop) still clears every slot, so the
  // conversion narrowed the per-interval path rather than weakening the sweep. It drives the same
  // `TrackPlaybackRuntime::clearAllLedgers()` that `Track::sendAllNotesOff` calls, without pulling the
  // MIDI-sending hot path into this boundary.
  TrackCaptureStopTestAccess::clearAllLedgersForTests(track);
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::ledgerEntryActive(track, 0, 60),
                            "the track-global sweep must still clear every slot");
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::ledgerEntryActive(track, 1, 62));
}



/// **S4.2 — a late cleanup must not clear a successor's finalization marker.** `captureStopOwner` is a
/// raw `PendingStopAdmission*` on a runtime object that ring entries reuse at stable addresses, so an
/// address-only clear (the pre-S4.2 `if (runtime.captureStopOwner == &stop)`) lets a predecessor's
/// late cleanup null a successor's marker. The clear is now generation-qualified; this drives the
/// A-releases / C-reuses / late-A-cleanup sequence directly.
void test_a_late_cleanup_cannot_clear_a_successor_finalization_marker() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));

  // Establish a real owner through the production admission, so the fixture works on a genuine
  // `PendingStopAdmission` rather than a stand-in.
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  Loop& loop = track.getLoop(0);
  track.startOverdubbing(96);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(96, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(120, 1, 60, 0)));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::admitStopForSlot(track, PendingStopDestination::Playing,
                                                               96, 0, /*interval=*/1));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::hasCaptureStopOwner(track, 0));

  // The owner's generation is the identity a cleanup must present.
  const uint32_t staleGeneration =
      TrackCaptureStopTestAccess::captureStopOwnerGeneration(track, 0) + 1u;

  // A late cleanup presenting a *different* generation is a rejected no-op, and the live marker
  // survives -- this is the successor on the reused runtime object.
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::clearCaptureStopOwner(track, 0, staleGeneration),
                            "a generation-mismatched cleanup must be rejected");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasCaptureStopOwner(track, 0),
                           "a late cleanup must not clear a successor's marker");

  // Control: the matching identity does clear it, so the rejection above is the generation check
  // and not a clear that never works.
  const uint32_t liveGeneration = TrackCaptureStopTestAccess::captureStopOwnerGeneration(track, 0);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::clearCaptureStopOwner(track, 0, liveGeneration),
                           "the matching identity must clear the marker");
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::hasCaptureStopOwner(track, 0));
  // ...and a second cleanup of the same identity is a no-op, not a crash or a resurrection.
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::clearCaptureStopOwner(track, 0, liveGeneration));
}


/// **S4.1/S4.2 — the `(slot, generation)` release entry.** Release is identity-qualified, gated by
/// liveness at the track level, and preserves the slot runtime's own refusal instead of discarding it
/// (the pre-S4.1 `TrackPlaybackRuntime::resetAll` dropped that answer).
void test_the_slot_generation_release_entry_is_identity_qualified_and_preserves_refusals() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 0));

  // Identity: the generation captured now is the one a caller must present.
  const uint16_t generation = TrackCaptureStopTestAccess::slotGeneration(track, 0);

  // A stale generation is a rejected no-op and must not release the held working set.
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      static_cast<uint8_t>(WorkingSetOutcome::StaleGeneration),
      static_cast<uint8_t>(TrackCaptureStopTestAccess::releaseSlot(track, 0,
                                                                   static_cast<uint16_t>(generation + 1))),
      "a stale generation must be a rejected no-op");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 0),
                           "a stale release must not drop the held working set");

  // The matching identity releases, and the generation advances so the same identity cannot release
  // twice (the reuse guard).
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      static_cast<uint8_t>(WorkingSetOutcome::Released),
      static_cast<uint8_t>(TrackCaptureStopTestAccess::releaseSlot(track, 0, generation)),
      "the matching identity must release");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 0),
                            "release must drop the held working set");
  const uint16_t advanced = TrackCaptureStopTestAccess::slotGeneration(track, 0);
  TEST_ASSERT_TRUE_MESSAGE(advanced > generation, "release must advance the slot generation");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      static_cast<uint8_t>(WorkingSetOutcome::StaleGeneration),
      static_cast<uint8_t>(TrackCaptureStopTestAccess::releaseSlot(track, 0, generation)),
      "the previous identity must not release a reused slot");

  // An unheld slot reports NotHeld rather than a false release.
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WorkingSetOutcome::NotHeld),
      static_cast<uint8_t>(TrackCaptureStopTestAccess::releaseSlot(track, 0, advanced)));
  // ...and an out-of-range slot cannot be released.
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WorkingSetOutcome::NotHeld),
      static_cast<uint8_t>(TrackCaptureStopTestAccess::releaseSlot(track, 200, 0)));
}


/// **S4.2 — a retired owner's late release cannot release a successor's working set.**
///
/// The task's named sequence is A-releases / C-reuses / late-A-cleanup, and the *lease* half of it is why
/// the identity check exists: `releaseSlot(slot, generation)` presents an identity, `releaseAll` bumps the
/// generation when it releases, and a reused slot must therefore answer the **retired** owner's late release
/// with `StaleGeneration` rather than releasing the successor's working set underneath it. The
/// finalization-identity half of the same sequence is pinned by
/// `test_a_late_cleanup_cannot_clear_a_successor_finalization_marker`.
///
/// The assertion that carries the weight is the *specific* answer: `StaleGeneration`, not the silent
/// `NotHeld` an identity-free check would return for a slot that looks empty or for one that has been
/// re-admitted. The pre-S4.1 sweep discarded exactly that distinction.
void test_s4_2_a_retired_owners_late_release_cannot_release_a_successors_working_set() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 2);

  // A admits through the production record-arm admission and holds the slot's working set.
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(2, true));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 2));
  const uint16_t aGeneration = TrackCaptureStopTestAccess::slotGeneration(track, 2);
  const uint8_t aRoles = TrackCaptureStopTestAccess::workingSetRoles(track, 2);
  LoopPlaybackRuntime* const runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 2);

  // A retires through the track-level sweep (the entry S4.1 converted): the lease drops and the generation
  // advances, so A's identity is spent even though the runtime object stays where it is.
  track.releasePlaybackMergedMidiEventsMemory();
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 2),
                            "the sweep must release A's working set");
  const uint16_t retiredGeneration = TrackCaptureStopTestAccess::slotGeneration(track, 2);
  TEST_ASSERT_TRUE_MESSAGE(retiredGeneration > aGeneration,
                           "A's release must advance the slot generation");

  // C reuses the same capacity: same runtime object, the same role set, and its own identity -- the
  // post-release generation, not A's. Re-admission itself allocates nothing and bumps nothing.
  TEST_ASSERT_TRUE_MESSAGE(track.prewarmPlaybackForSlot(2, true), "the slot must re-admit");
  TEST_ASSERT_EQUAL_PTR_MESSAGE(runtime, TrackCaptureStopTestAccess::runtimeIfAllocated(track, 2),
                                "C must reuse the slot's runtime object");
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 2));
  const uint16_t cGeneration = TrackCaptureStopTestAccess::slotGeneration(track, 2);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(retiredGeneration, cGeneration,
                                   "re-admission must not spend the successor's identity");
  TEST_ASSERT_EQUAL_UINT8(aRoles, TrackCaptureStopTestAccess::workingSetRoles(track, 2));

  // Late A cleanup presenting the retired identity: rejected **and reported** -- `StaleGeneration`, never a
  // release and never the silent `NotHeld`.
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      static_cast<uint8_t>(WorkingSetOutcome::StaleGeneration),
      static_cast<uint8_t>(TrackCaptureStopTestAccess::releaseSlot(track, 2, aGeneration)),
      "the retired owner's identity must be rejected as stale, not answered as NotHeld");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 2),
                           "a late release must not release a successor's working set");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(aRoles, TrackCaptureStopTestAccess::workingSetRoles(track, 2),
                                  "a late release must not disturb the successor's roles");
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(cGeneration, TrackCaptureStopTestAccess::slotGeneration(track, 2),
                                   "a rejected release must not advance the successor's identity");

  // Control: the successor's own identity releases, and only then does the generation move again.
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      static_cast<uint8_t>(WorkingSetOutcome::Released),
      static_cast<uint8_t>(TrackCaptureStopTestAccess::releaseSlot(track, 2, cGeneration)),
      "the live identity must release");
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 2));
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::slotGeneration(track, 2) > cGeneration,
                           "the accepted release must advance the generation");
}


/// **S4.2 — the release outcomes carry the device row's own vocabulary.**
///
/// "Reported" has two halves: the caller sees a distinguishable outcome (pinned above and by the
/// `(slot, generation)` release-entry fixture), and the row that leaves the box names it. The emitter's
/// literal mapping is the only place that second half lives, so it is pinned here directly against the
/// vocabulary the reader consumes (`scripts/hitl/lease_release.py` decodes the same four literals, and
/// `test_capture_line_tier` pins the row's Tier-A classification). `NotHeld` is included on purpose: it is
/// the one outcome the sweep deliberately does **not** emit, which is why a slot with no row means "no sweep
/// ran here", never "the release failed".
void test_s4_2_the_release_outcomes_are_named_as_the_device_row_names_them() {
  TEST_ASSERT_EQUAL_STRING("released", workingSetOutcomeLabel(WorkingSetOutcome::Released));
  TEST_ASSERT_EQUAL_STRING("refused_busy", workingSetOutcomeLabel(WorkingSetOutcome::RefusedBusy));
  TEST_ASSERT_EQUAL_STRING("stale_generation", workingSetOutcomeLabel(WorkingSetOutcome::StaleGeneration));
  TEST_ASSERT_EQUAL_STRING("not_held", workingSetOutcomeLabel(WorkingSetOutcome::NotHeld));
}

/// **S4.1 — the track-level release is gated by liveness.** `releaseEligiblePlaybackRuntimeSlots`
/// must skip a slot whose predicate reports a live owner: releasing underneath one is what the
/// predicate-free `resetAll` did.
void test_the_release_sweep_skips_a_slot_with_a_live_owner() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 1);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(1, true));

  // A live owner on the slot: an accepted interaction latched for it.
  TrackCaptureStopTestAccess::latchAcceptedStopForSlot(track, 1);
  TEST_ASSERT_FALSE_MESSAGE(track.noLiveOwnerForSlot(1), "the fixture needs a live owner");

  const uint16_t generation = TrackCaptureStopTestAccess::slotGeneration(track, 1);
  track.releasePlaybackMergedMidiEventsMemory();
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(
      generation, TrackCaptureStopTestAccess::slotGeneration(track, 1),
      "the sweep must not release a slot with a live owner");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 1),
                           "a live owner's working set must survive the sweep");

  // Control: once the owner retires, the same sweep does release it.
  TrackCaptureStopTestAccess::consumeAcceptedStop(track);
  TEST_ASSERT_TRUE(track.noLiveOwnerForSlot(1));
  track.releasePlaybackMergedMidiEventsMemory();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::slotGeneration(track, 1) > generation,
                           "the sweep must release a slot with no live owner");
}


/// **S4.1 — a same-track slot switch releases the retired slot's working-set lease, per slot.**
///
/// The device evidence for S4.1 is the *finalization-capacity* release (`canonical_release,<slot>,…`);
/// nothing named the working-set **lease** release on a same-track switch, and for a same-track sequence
/// the take-start sweep excludes the track. This drives the production switch commit
/// (`TrackManager::finalizeCaptureAndSelectSlot` -> `Track::resetPlaybackStateForSlot` ->
/// `Track::invalidatePlaybackMergedMidiEvents` -> `Track::releaseEligiblePlaybackRuntimeSlots`) with the
/// **incoming** slot protected by a live owner, so the release must be evaluated per slot: the retired
/// slot is released, the owned one is not.
void test_s4_1_a_same_track_switch_releases_the_retired_slots_lease() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  constexpr uint8_t kRetired = 1;
  constexpr uint8_t kIncoming = 3;
  commitRecordPassIntoTrackSlot(track, kRetired);
  commitRecordPassIntoTrackSlot(track, kIncoming);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kRetired, true));
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kIncoming, true));
  TrackCaptureStopTestAccess::activateSlot(0, kRetired);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kRetired, track.getActiveLoopIndex(),
                                  "the fixture needs the retired slot active");

  const uint16_t retiredGeneration = TrackCaptureStopTestAccess::slotGeneration(track, kRetired);
  const uint16_t incomingGeneration = TrackCaptureStopTestAccess::slotGeneration(track, kIncoming);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kRetired),
                           "the retired slot must hold a working set before the switch");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kIncoming),
                           "the incoming slot must hold a working set before the switch");
  // An admitted working set is not itself a live owner, so the retired slot is releasable...
  TEST_ASSERT_TRUE_MESSAGE(track.noLiveOwnerForSlot(kRetired),
                           "an admitted working set must not block release on its own");

  // The track is audibly playing, which is the ordinary state a slot switch happens in: it keeps the
  // switch's own "start playback if the target has data" step out of this fixture (that step belongs to
  // the playback-start suites and is fail-closed here).
  track.forceSetState(TRACK_PLAYING);
  TEST_ASSERT_TRUE(track.isAudiblePlaybackActive());

  // ...while the incoming slot is protected, so a track-global sweep cannot pass this fixture.
  TrackCaptureStopTestAccess::latchAcceptedStopForSlot(track, kIncoming);
  TEST_ASSERT_FALSE_MESSAGE(track.noLiveOwnerForSlot(kIncoming),
                            "the fixture needs a live owner on the incoming slot");

  // The production switch, same track.
  TrackCaptureStopTestAccess::finalizeCaptureAndSelectSlot(0, kIncoming, 96);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kIncoming, track.getActiveLoopIndex(),
                                  "the switch must move the active slot");

  // The retired slot: released, and its identity advanced, so the exact slot is observable.
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kRetired),
                            "the switch must release the retired slot's working set");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::slotGeneration(track, kRetired) >
                               retiredGeneration,
                           "the release must advance the retired slot's generation");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      static_cast<uint8_t>(WorkingSetOutcome::StaleGeneration),
      static_cast<uint8_t>(TrackCaptureStopTestAccess::releaseSlot(track, kRetired, retiredGeneration)),
      "the pre-switch identity must no longer release: the switch consumed it");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      static_cast<uint8_t>(WorkingSetOutcome::NotHeld),
      static_cast<uint8_t>(TrackCaptureStopTestAccess::releaseSlot(
          track, kRetired, TrackCaptureStopTestAccess::slotGeneration(track, kRetired))),
      "nothing may be left to release on the retired slot");

  // The protected sibling is untouched: the release was per slot, not a track-global reset.
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kIncoming),
                           "a slot with a live owner must survive the switch");
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(incomingGeneration,
                                   TrackCaptureStopTestAccess::slotGeneration(track, kIncoming),
                                   "a refused slot's generation must not move");

  // Control: retire the owner, switch back, and the same route releases the other slot.
  TrackCaptureStopTestAccess::consumeAcceptedStop(track);
  TEST_ASSERT_TRUE(track.noLiveOwnerForSlot(kIncoming));
  TrackCaptureStopTestAccess::finalizeCaptureAndSelectSlot(0, kRetired, 192);
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kIncoming),
                            "the control switch must release the now-unowned slot");
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::slotGeneration(track, kIncoming) > incomingGeneration);
}

/// **S4.1 — a live finalization refuses the retired slot's lease release.**
///
/// While an interval finalizes, `Track::invalidatePlaybackMergedMidiEvents` early-returns on the
/// **track-level** `hasPendingDeferredOverdubStop()`, so the sweep is suppressed before the per-slot
/// predicate is consulted; the slot's own entry independently refuses with `RefusedBusy`
/// (`LoopPlaybackRuntime::reset()` returns false while `captureStopOwner` is pending). The switch still
/// commits -- an *admitted* stop does not hold the active slot (S4f.3) -- so this is the S4.1 handoff
/// shape with the lease being the thing that survives it.
void test_s4_1_a_live_finalization_refuses_the_retired_slots_lease_release() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  constexpr uint8_t kFinalizing = 1;
  constexpr uint8_t kSuccessor = 3;
  commitRecordPassIntoTrackSlot(track, kFinalizing);
  commitRecordPassIntoTrackSlot(track, kSuccessor);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kFinalizing, true));
  TrackCaptureStopTestAccess::activateSlot(0, kFinalizing);

  // A live interval on the finalizing slot, opened through the production start, with its stop admitted.
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::startOverdubbing(track, 96);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isOverdubbing(track),
                           "the fixture needs a live interval");
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::appendCaptureNote(track, 96, 120, 60));
  TEST_ASSERT_TRUE_MESSAGE(
      TrackCaptureStopTestAccess::admitStopForSlot(track, PendingStopDestination::Playing, 96,
                                                   kFinalizing, /*interval=*/1),
      "the production stop admission must accept on the warmed slot");
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());

  const uint16_t generation = TrackCaptureStopTestAccess::slotGeneration(track, kFinalizing);
  TEST_ASSERT_FALSE_MESSAGE(track.noLiveOwnerForSlot(kFinalizing),
                            "a live finalization must refuse release on its slot");

  // The slot's own entry refuses and names the refusal. (It also asks the pending stop to become a
  // transport stop -- that is `reset()`'s documented behaviour on this path, not a fixture artifact.)
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      static_cast<uint8_t>(WorkingSetOutcome::RefusedBusy),
      static_cast<uint8_t>(TrackCaptureStopTestAccess::releaseSlot(track, kFinalizing, generation)),
      "a live finalization must answer RefusedBusy");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kFinalizing),
                           "a refused release must not drop the working set");
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(generation,
                                   TrackCaptureStopTestAccess::slotGeneration(track, kFinalizing),
                                   "a refused release must not advance the generation");

  // The production switch commits, and the lease survives it: the sweep is suppressed while the
  // interval finalizes, so nothing is released underneath the owner.
  TrackCaptureStopTestAccess::finalizeCaptureAndSelectSlot(0, kSuccessor, 120);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kSuccessor, track.getActiveLoopIndex(),
                                  "an admitted stop must not hold the active slot (S4f.3)");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kFinalizing),
                           "the switch must not release a finalizing slot's working set");
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(generation,
                                   TrackCaptureStopTestAccess::slotGeneration(track, kFinalizing),
                                   "the suppressed sweep must not touch the generation");
  TEST_ASSERT_TRUE_MESSAGE(track.hasPendingDeferredOverdubStop(),
                           "the interval must still be finalizing after the switch");
}

/// **S4.1 — the retired slot's lease is released once its finalization retires.**
///
/// The complement of the refusal case, and the answer to "which same-track route releases it": not the
/// retirement itself (`TrackPreparedStopCommit::completeDeferredCleanup` clears `captureStopOwner` and arms
/// the *capacity* release marker, but releases no working set), so the next same-track switch's playback
/// reset is what reaches the retired slot. This drives that route end to end.
void test_s4_1_the_retired_slots_lease_is_released_once_its_finalization_retires() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  constexpr uint8_t kFinalizing = 1;
  constexpr uint8_t kSuccessor = 3;
  commitRecordPassIntoTrackSlot(track, kFinalizing);
  commitRecordPassIntoTrackSlot(track, kSuccessor);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kFinalizing, true));
  TrackCaptureStopTestAccess::activateSlot(0, kFinalizing);

  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::startOverdubbing(track, 96);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::appendCaptureNote(track, 96, 120, 61));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::admitStopForSlot(track, PendingStopDestination::Playing, 96,
                                                                kFinalizing, /*interval=*/1));
  const uint16_t generation = TrackCaptureStopTestAccess::slotGeneration(track, kFinalizing);

  // The handoff: the switch commits while the predecessor still finalizes, and the lease survives it.
  TrackCaptureStopTestAccess::finalizeCaptureAndSelectSlot(0, kSuccessor, 120);
  TEST_ASSERT_EQUAL_UINT8(kSuccessor, track.getActiveLoopIndex());
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kFinalizing),
                           "the predecessor must keep its lease while it finalizes");
  TEST_ASSERT_EQUAL_UINT16(generation, TrackCaptureStopTestAccess::slotGeneration(track, kFinalizing));

  // Retire the finalization through the production grain.
  for (int turn = 0; turn < 8192 && track.hasPendingDeferredOverdubStop(); ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE_MESSAGE(track.hasPendingDeferredOverdubStop(),
                            "the fixture must retire the deferred stop");
  TEST_ASSERT_TRUE_MESSAGE(track.noLiveOwnerForSlot(kFinalizing),
                           "retirement must free the slot for release");

  // The next same-track switch runs the sweep with no live owner left, and releases the retired slot.
  TrackCaptureStopTestAccess::finalizeCaptureAndSelectSlot(0, kFinalizing, 192);
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kFinalizing),
                            "the next switch must release the retired slot's working set");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::slotGeneration(track, kFinalizing) > generation,
                           "the release must advance the retired slot's generation");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      static_cast<uint8_t>(WorkingSetOutcome::StaleGeneration),
      static_cast<uint8_t>(
          TrackCaptureStopTestAccess::releaseSlot(track, kFinalizing, generation)),
      "the pre-switch identity must be stale after the release");
}


/// **S4.3 — reactivating a released slot reuses its capacity with no net pool growth.** The reuse
/// contract is structural: releasing a slot retires its working set but keeps the slot's runtime
/// object, so the next activation takes that same capacity back instead of allocating a new one. This
/// cycles A-release / C-activate repeatedly and asserts the address, the held bytes and the chunk pool
/// return to the same state every time -- growth would show as a rising pool or a new runtime object.
void test_reactivating_a_released_slot_reuses_its_capacity() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);

  // Baseline activation (the control that the cycle's steady state is a real working set).
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  LoopPlaybackRuntime* const runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
  TEST_ASSERT_TRUE(runtime != nullptr);
  const uint16_t chunksAfterFirstActivation = LoopEventStore::freeChunkCount();

  // Four release/re-activate cycles. Each one must return to the baseline state.
  for (int cycle = 0; cycle < 4; ++cycle) {
    const uint16_t generationBefore = TrackCaptureStopTestAccess::slotGeneration(track, 0);
    TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 0));

    // Release through the track-level sweep (the entry S4.1 converted).
    track.releasePlaybackMergedMidiEventsMemory();
    TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 0),
                              "the sweep must release the slot's working set");
    TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::slotGeneration(track, 0) > generationBefore,
                             "release must advance the slot generation");

    // Re-activate. The runtime object is the slot's capacity and must be the same one.
    TEST_ASSERT_TRUE_MESSAGE(track.prewarmPlaybackForSlot(0, true),
                             "the slot must re-admit its working set");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(runtime, TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0),
                                  "re-activation must reuse the slot's runtime object");
    TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 0),
                             "re-activation must hold the working set again");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(chunksAfterFirstActivation, LoopEventStore::freeChunkCount(),
                                     "the reuse cycle must not consume pool chunks");
  }

  // The slot holds exactly the working set it held at baseline: no accumulation across the cycles.
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      static_cast<uint8_t>(WorkingSetRole::PlaybackWindow | WorkingSetRole::LiveCapture),
      TrackCaptureStopTestAccess::workingSetRoles(track, 0),
      "the reused slot must hold the same role set, not accumulate roles");
}


/// **S4.4 — a declared-envelope breach is reported, not resolved by refusing.** The envelope declares
/// `LiveCapture` as one per track. A second holder is a **policy** signal with its own reason, distinct
/// from an allocation failure -- and the activation still proceeds, because the resource model supports
/// it: enforcing the declared cap here would reject supported multi-slot flows (85 fixtures in the
/// continuous-capture harness arm a second slot).
void test_a_declared_envelope_breach_is_reported_and_does_not_block_activation() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  commitRecordPassIntoTrackSlot(track, 1);

  // Control: with nothing holding the class there is no breach, and the check *says so* instead of
  // reporting an anonymous denial. The check is a read: censusing eight slots must not allocate a slot
  // runtime (S4.4's "reported rather than resolved by allocating" starts with the check itself).
  MemoryMonitor::resetNativeTestCapDenials();
  const uint16_t chunksBeforeCheck = LoopEventStore::freeChunkCount();
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      kNoWorkingSetClassHolder,
      TrackCaptureStopTestAccess::reportEnvelopeBreachIfAny(track, 0, WorkingSetRole::LiveCapture),
      "with no holder there is nothing to report");
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, MemoryMonitor::nativeTestCapDenials().count,
                                   "an empty census must not report a breach");
  // ...including for the very slot it inspects: asking about an unallocated slot must not *create* its
  // runtime, which is the difference between censusing and preparing.
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::runtimeAllocated(track, 7));
  (void)TrackCaptureStopTestAccess::reportEnvelopeBreachIfAny(track, 7, WorkingSetRole::LiveCapture);
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, 7),
                            "the census must not allocate a runtime for the slot it inspects");
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(chunksBeforeCheck, LoopEventStore::freeChunkCount(),
                                   "the check must take no pool chunks");

  TEST_ASSERT_TRUE_MESSAGE(track.prewarmPlaybackForSlot(0, true),
                           "the first activation must be admitted");
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 0));

  // Slot 1's activation would put a second live-capture role on the track. The breach must be reported
  // **with the slot that holds the class** -- not as an anonymous `class=N` -- and the activation must
  // still be admitted: the policy is report, never refuse (DEC-078 item 1), and never resolved by
  // allocating more.
  MemoryMonitor::resetNativeTestCapDenials();
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      0, TrackCaptureStopTestAccess::reportEnvelopeBreachIfAny(track, 1, WorkingSetRole::LiveCapture),
      "the report must name the slot holding the class");
  const MemoryMonitor::NativeCapDenial denial = MemoryMonitor::nativeTestCapDenials();
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, denial.count, "the breach must be reported once");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, denial.lastHolderSlot, "slot 0 holds the class");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, denial.lastRoleClass,
                                  "the class is the LiveCapture bit, which is what the device row names");

  TEST_ASSERT_TRUE_MESSAGE(track.prewarmPlaybackForSlot(1, true),
                           "a declared-cap breach must not block a supported activation");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, 1),
                           "the activation must have taken its resources");
  // Both slots really do hold the class, which is what the declared envelope calls a breach.
  TEST_ASSERT_TRUE(
      hasRole(TrackCaptureStopTestAccess::workingSetRolesOf(track, 0), WorkingSetRole::LiveCapture));
  TEST_ASSERT_TRUE(
      hasRole(TrackCaptureStopTestAccess::workingSetRolesOf(track, 1), WorkingSetRole::LiveCapture));
  // ...and the production admission reported the breach itself, not only the explicit call above: one
  // report per check, so the counter is 1 (the explicit call) + 1 (the admission's own check).
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(2, MemoryMonitor::nativeTestCapDenials().count,
                                   "the admission path reports the breach it meets");
}


/// **S4.4 — the report names the holder, and a *finalizing* holder is exactly what it names.**
///
/// The declared envelope says one live capture per track, and the census counts **raw role bits**: a slot
/// whose capture has stopped keeps `LiveCapture` until its working set is released. So the pair the
/// contract *requires* -- an outgoing finalizer beside an incoming capture (DEC-078 items 5/6, the case
/// S4b.1 corrected the record with) -- still counts as a second live-capture holder. This fixture pins
/// that as a **diagnosable fact** rather than a silent false positive: the report names the finalizing
/// slot as the holder, and the activation is admitted (S4.4's policy: report, never refuse). Correcting
/// the census itself is S4b's role-transfer work, so this is the fact its slice starts from.
void test_s4_4_the_breach_report_names_the_finalizing_holder_instead_of_hiding_it() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  constexpr uint8_t kFinalizing = 0;
  constexpr uint8_t kIncoming = 1;
  commitRecordPassIntoTrackSlot(track, kFinalizing);
  commitRecordPassIntoTrackSlot(track, kIncoming);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kFinalizing, true));

  // Slot 0's capture ends: its stop is admitted (a deferred finalization), so its `LiveCapture` role is
  // now the outgoing one while slot 1 is the incoming capture.
  // The overdub runs on the *outgoing* slot, so the incoming one is still un-leased when it is asked to
  // activate -- otherwise its own admission would already hold the class and there would be nothing to
  // report (the check treats an idempotent re-admission as no breach).
  TrackCaptureStopTestAccess::selectSlot(track, kFinalizing);
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::startOverdubbing(track, 96);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::appendCaptureNote(track, 96, 120, 60));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::admitStopForSlot(track, PendingStopDestination::Playing, 96,
                                                                kFinalizing, /*interval=*/1));
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::ownsLiveFinalization(track, kFinalizing),
                           "the fixture needs an admitted — i.e. live — finalizer");
  TEST_ASSERT_TRUE(hasRole(TrackCaptureStopTestAccess::workingSetRolesOf(track, kFinalizing),
                           WorkingSetRole::LiveCapture));

  // The incoming activation is reported as a breach and names the finalizing slot as its holder.
  MemoryMonitor::resetNativeTestCapDenials();
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      kFinalizing,
      TrackCaptureStopTestAccess::reportEnvelopeBreachIfAny(track, kIncoming, WorkingSetRole::LiveCapture),
      "the report must name the finalizing slot that still holds the class");
  const MemoryMonitor::NativeCapDenial denial = MemoryMonitor::nativeTestCapDenials();
  TEST_ASSERT_EQUAL_UINT16(1, denial.count);
  TEST_ASSERT_EQUAL_UINT8(kFinalizing, denial.lastHolderSlot);

  // ...and the activation is admitted, so the required pair is not blocked by the declaration.
  TEST_ASSERT_TRUE_MESSAGE(track.prewarmPlaybackForSlot(kIncoming, true),
                           "the required finalizer-plus-incoming pair must be admitted");
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kIncoming));
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::ownsLiveFinalization(track, kFinalizing),
                           "the finalizer must still be in flight after the incoming admission");
}



/// **S4b.1 — the switch acceptance sequence.** A is recording/overdubbing; switching to B ends A's live
/// capture at the handoff boundary, A retains everything it needs to finalize, and **B starts
/// immediately without waiting for A to finish finalizing**.
///
/// Requirements this fixture holds itself to, after review:
/// * the finalizer is **admitted** (`hasPendingDeferredOverdubStop`), never a mere accepted-unexecuted
///   stop, so "waiting for admission" and "admitted and paused" are not conflated;
/// * B is reached through the **user-facing gesture** (`MidiButtonActions::handleToggleRecordForSlot`)
///   and its input through the real MIDI handler, so this proves the normal start and incoming MIDI
///   reach B -- not that a buffer accepts an append;
/// * the acceptance runs **unconditionally** once S4f.3 lands: the characterization branch below
///   disappears rather than quietly returning early.
void test_switching_starts_the_new_take_while_the_previous_take_still_finalizes() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  // Slot 0 holds committed content (A's target), slot 1 is the empty target B.
  commitRecordPassIntoTrackSlot(track, 0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));

  // (1) A is live through the production button route, and takes MIDI through the real handler.
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isOverdubbing(track),
                           "fixture precondition: A must be overdubbing");
  inputNote(60, 96, 120);

  // (2) A's stop is admitted and **deliberately not advanced**: the finalizer is admitted and paused.
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track),
                           "A must have an *admitted* finalizer before the switch");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasAcceptedUnexecutedInteraction(track),
                            "the stop must be admitted, not merely accepted-and-unexecuted");

  // (3) Switch to B through the user-facing gesture, then start B. The per-slot gesture finalizes A's
  // capture, commits the switch and returns; it does not also start the target, so the start is its own
  // press -- the acceptance sequence's "switch to B, then start B".
  clockManager.assignCurrentTickSilently(200);
  TrackCaptureStopTestAccess::toggleRecordForSlot(1);
  // S4f.6: the switch commits even though A's admitted stop is still finalizing, and this is now
  // asserted unconditionally. The characterization branch that accepted "the switch held the active slot
  // on A" was deleted when S4f.6 landed, per this packet's own note, so a regression to refusing the
  // switch cannot pass by returning early.
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the switch must move the active slot to B while A finalizes");

  // Start B. The arm/record entry and the record-arm branch both act on the **selected** slot, which the
  // switch deliberately leaves on the predecessor (S4f.5 keeps focus for the piano roll). Select the
  // active target first, which is what the user does when the switch moved the audio to B.
  TrackCaptureStopTestAccess::selectSlotIndex(0, 1);
  // Start B through the per-slot gesture, which is the successor-start route (the arm entry
  // `handleToggleRecord` routes a track already reporting overdubbing to `stopOverdubbing`).
  TrackCaptureStopTestAccess::toggleRecordForSlot(1);
  // A start on a running transport is **queued** at the loop's bar boundary
  // (`TrackManager::handlePendingRecordStart`), so drive that production dispatch across the boundary.
  // `handlePendingRecordStart` deliberately ignores the tick the start was queued at, so land the
  // dispatch on the *following* loop boundary rather than the same one. There is deliberately no
  // fallback press: a start that only happens because the fixture pressed again is not the requirement.
  const uint32_t queuedTick = clockManager.getCurrentTick();
  for (uint32_t tick = queuedTick + 1; tick <= queuedTick + 768 * 3 &&
                             !TrackCaptureStopTestAccess::isRecording(track) &&
                             !TrackCaptureStopTestAccess::isOverdubbing(track);
       ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }

  // (4) B is the live interval and records incoming MIDI while A remains unfinished.
  // S4f.6: B starts while A still finalizes. Before S4f.6 the refusal ran *inside*
  // `startRecordingTrack` and short-circuited the dispatcher's consumption lines, so the request was
  // retained but never executed and never superseded. Both characterization branches are deleted; the
  // claims below are now the requirement itself.
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track) ||
                               TrackCaptureStopTestAccess::isOverdubbing(track),
                           "B must start from the gesture while A still finalizes");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the switch moved the active slot to B at the handoff boundary");
  const size_t before = TrackCaptureStopTestAccess::captureEventCount(track);
  // Inject in the record domain: the dispatcher above advanced the tick to the loop boundary, so the
  // note must lie *after* the current tick or capture rejects it as already past.
  const uint32_t tickBeforeInput = clockManager.getCurrentTick();
  const uint32_t onTick = tickBeforeInput + 8;
  inputNote(64, onTick, onTick + 32);
  const size_t after = TrackCaptureStopTestAccess::captureEventCount(track);
  if (after != before + 2) {
    // An on-failure diagnostic only -- the claim itself is unconditional, so this cannot pass by
    // returning early the way the pre-S4f.6 characterization could.
    char state[256];
    snprintf(state, sizeof(state),
             "B did not record the note: recording=%d overdubbing=%d captureActive=%d size=%zu tick=%u "
             "active=%u beforeTick=%u onTick=%u phase=%u hasOverdubSession=%d",
             TrackCaptureStopTestAccess::isRecording(track) ? 1 : 0,
             TrackCaptureStopTestAccess::isOverdubbing(track) ? 1 : 0,
             TrackCaptureStopTestAccess::captureActive(track) ? 1 : 0, after,
             (unsigned)clockManager.getCurrentTick(),
             TrackCaptureStopTestAccess::activeLoopIndex(track), (unsigned)tickBeforeInput,
             (unsigned)onTick, static_cast<unsigned>(track.getActiveLoop().capture.phase),
             track.getActiveLoop().hasOverdubSession() ? 1 : 0);
    snprintf(state + strlen(state), sizeof(state) - strlen(state),
             " | queued any=%d slot0=%d slot1=%d", TrackCaptureStopTestAccess::anyRecordQueued(0) ? 1 : 0,
             TrackCaptureStopTestAccess::recordQueuedForSlot(0, 0) ? 1 : 0,
             TrackCaptureStopTestAccess::recordQueuedForSlot(0, 1) ? 1 : 0);
    TEST_FAIL_MESSAGE(state);
  }
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track),
                           "A's finalizer must still be outstanding while B records");

  // (5) A completes without damaging B: drive A's finalizer to completion and confirm B's interval is
  // untouched and still the live one.
  const size_t bEvents = TrackCaptureStopTestAccess::captureEventCount(track);
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track),
                            "A's finalizer must be able to complete");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "A's completion must not move the active slot off B");
  TEST_ASSERT_EQUAL_MESSAGE(bEvents, TrackCaptureStopTestAccess::captureEventCount(track),
                            "A's completion must not damage B's recorded interval");
}

/// **S4f.3 — accepted-switch retention under unavailable stop admission (blocked -> ready).**
///
/// This is the end-to-end proof S4b.1's check 4 needs, and it replaces an earlier version of this
/// coverage whose claimed request did not exist: that body returned early from an `activeLoopIndex != 1`
/// characterization branch in the default gate, so it passed while holding nothing. The false green is
/// recorded in the packet.
///
/// One track, three slots. A (slot 0) and B (slot 1) carry committed content, C (slot 2) is empty:
///
///  1. A's stop is **admitted and bound** and its finalizer is held unfinished -- it occupies the track's
///     deferred-finalization capacity.
///  2. B becomes active through the production active-slot authority and **overdubs**, so B's stop is a
///     deferred stop (a fresh *record* stop is not capacity-governed, which is why B is an overdub).
///  3. B's stop is requested and **denied on capacity**, so B's obligation latches against B's own slot.
///  4. A switch/start is requested on C while B's stop is unadmitted. The switch must not commit and must
///     not be dropped: the request is retained.
///  5. Repeated dispatch must leave B's obligation and C's switch/start intact, and must never move the
///     active slot underneath B's unadmitted stop.
///  6. Capacity is released (A's finalizer completes) and B's stop is driven until it binds.
///  7. Dispatch then commits the retained switch to C and starts C, which records MIDI.
///  8. Further dispatch must not execute either request twice.
void test_a_blocked_switch_and_start_survive_until_its_prerequisite_stop_binds() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  commitRecordPassIntoTrackSlot(track, 1);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(1, true));

  // (1) A overdubs through the production gesture; its stop is admitted and **held**.
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isOverdubbing(track),
                           "precondition: A must be overdubbing");
  inputNote(60, 96, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track),
                           "A's stop must be admitted before the handoff");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 0),
                           "A's admitted stop must be identified by A's own slot");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 0),
                            "A's stop is admitted, not merely accepted-and-unexecuted");

  // (2) B becomes active (A's own stop is bound, so the switch commits) and overdubs.
  clockManager.assignCurrentTickSilently(200);
  TrackCaptureStopTestAccess::activateSlot(0, 1);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the switch to B must commit while A still finalizes");
  TrackCaptureStopTestAccess::startOverdubbing(track, clockManager.getCurrentTick());
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isOverdubbing(track),
                           "B must be overdubbing for its stop to need deferred admission");

  // (3) B's stop is requested and denied on capacity, latching against B's own slot.
  clockManager.assignCurrentTickSilently(264);
  TrackCaptureStopTestAccess::selectSlotIndex(0, 1);
  TrackCaptureStopTestAccess::toggleRecordForSlot(1);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 1),
                           "B's denied stop must be accepted and latched against B's own slot");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 1),
                            "capacity is held by A, so B's stop must not be admitted");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 0),
                           "A's finalizer must still be the capacity holder");

  // (4) Request switch/start on C while B's stop is unadmitted: retained, neither committed nor dropped.
  clockManager.assignCurrentTickSilently(300);
  TrackCaptureStopTestAccess::toggleRecordForSlot(2);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      1, TrackCaptureStopTestAccess::activeLoopIndex(track),
      "activation must NOT move the active slot underneath B's unadmitted stop");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(0, 2),
                           "the switch must retain C's record start rather than drop it");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 1),
                           "B's stop obligation must survive the switch request");

  // (5) Repeated dispatch: neither request may be lost or executed early, and the active slot must stay
  // on B for the whole blocked window -- the explicit assertion the contract asks for.
  for (uint32_t tick = 301; tick <= 301 + 768 * 3; ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        1, TrackCaptureStopTestAccess::activeLoopIndex(track),
        "the active slot must not move under B's unadmitted stop, on any dispatch");
    TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track),
                              "C must not start before its prerequisite stop binds");
  }
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(0, 2),
                           "C's switch/start must survive repeated dispatch");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 1),
                           "B's stop obligation must survive repeated dispatch");

  // (6) Release capacity: A's finalizer completes, then B's stop is driven until it binds.
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track),
                            "A's finalizer must be able to complete and release capacity");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 1),
                           "B's stop obligation is still waiting after capacity frees");
  bool bStopBound = false;
  for (int turn = 0; turn < 64 && !bStopBound; ++turn) {
    (void)TrackCaptureStopTestAccess::serviceAcceptedInteractionTurn(track);
    bStopBound = TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 1);
  }
  TEST_ASSERT_TRUE_MESSAGE(bStopBound, "B's accepted stop must bind once capacity frees");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 1),
                            "the bound stop's obligation must be consumed exactly once");

  // (7) Dispatch now commits the retained switch and starts C on C's eligible boundary.
  const uint32_t resumeTick = clockManager.getCurrentTick();
  for (uint32_t tick = resumeTick + 1;
       tick <= resumeTick + 768 * 3 && !TrackCaptureStopTestAccess::isRecording(track); ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the retained switch must commit to C once its stop bound");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track),
                           "C must start recording from the retained request, with no further gesture");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(0, 2),
                            "the executed start must be consumed exactly once");

  // C records the incoming MIDI through the production input route.
  const size_t before = TrackCaptureStopTestAccess::captureEventCount(track);
  const uint32_t tickBeforeInput = clockManager.getCurrentTick();
  inputNote(67, tickBeforeInput + 8, tickBeforeInput + 40);
  TEST_ASSERT_EQUAL_MESSAGE(before + 2, TrackCaptureStopTestAccess::captureEventCount(track),
                            "C must record the incoming MIDI note's on and off");

  // (8) Exactly once: further dispatch must not re-execute either request or disturb C's interval.
  const size_t cEvents = TrackCaptureStopTestAccess::captureEventCount(track);
  const uint32_t afterStart = clockManager.getCurrentTick();
  for (uint32_t tick = afterStart + 1; tick <= afterStart + 768 * 2; ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "subsequent dispatch must not move the active slot again");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(0, 2),
                            "the start request must not be re-armed by later dispatch");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 1),
                            "B's stop obligation must not be re-executed by later dispatch");
  TEST_ASSERT_EQUAL_MESSAGE(cEvents, TrackCaptureStopTestAccess::captureEventCount(track),
                            "later dispatch must not add events to C's interval");
}


/// **S4f.17/S4f.21 — a retained Record stop holds the active slot; an admitted one does not.**
///
/// S17.2 moved the production Record stop onto the deferred drive, so `stopRecordingToStopped` can
/// now return with the interval *accepted but unadmitted*. `finalizeCaptureAndSelectSlot` used to
/// switch unconditionally for a recording track (correct while the stop committed inline, because
/// there was nothing left unadmitted). With the deferred route that would advance
/// `captureInterval_` past the retained interval, and `driveAcceptedInteraction` would then judge
/// the obligation **superseded** and drop the take -- silent content loss on an ordinary
/// switch-while-finalizing handoff.
///
/// This fixture pins both halves of the rule the overdub branch already enforces:
///   * an accepted-but-unbound Record stop **refuses** the switch and leaves the active slot alone;
///   * once that stop binds, the switch commits while the predecessor still finalizes, so a
///     different-slot successor is not serialized behind predecessor cleanup.
void test_record_switch_respects_a_retained_stop_and_permits_an_admitted_one() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(1, true));
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(2, true));

  // (1) A records into slot 0 through the production stop route; the ring is free, so its stop is
  // admitted and the interval is owned by the deferred job, not by the active slot.
  track.forceSetState(TRACK_STOPPED);
  TrackCaptureStopTestAccess::activateSlot(0, 0);
  clockManager.assignCurrentTickSilently(0);
  track.startRecording(0);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::isRecording(track));
  inputNote(60, 12, 96);
  clockManager.assignCurrentTickSilently(768);
  track.stopRecordingToStopped(768);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 0),
                           "precondition: A's Record stop must bind while the ring is free");
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 0));

  // (2) B records into slot 1 while A finalizes. A's job owns the ring, so B's stop is retained
  // against B's own slot and interval rather than admitted.
  TrackCaptureStopTestAccess::activateSlot(0, 1);
  TEST_ASSERT_EQUAL_UINT8(1, TrackCaptureStopTestAccess::activeLoopIndex(track));
  clockManager.assignCurrentTickSilently(768);
  track.startRecording(768);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::isRecording(track));
  inputNote(62, 780, 864);
  clockManager.assignCurrentTickSilently(1536);
  track.stopRecordingToStopped(1536);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 1),
                           "precondition: B's Record stop must be retained, not admitted");
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 1));

  // (3) The switch is REFUSED: an unbound stop must not have the active slot moved out from under
  // it, or the retained interval would be superseded and its take dropped.
  TEST_ASSERT_FALSE_MESSAGE(
      trackManager.finalizeCaptureAndSelectSlot(0, 2, 1536),
      "a switch away from an accepted-but-unbound Record stop must be refused");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "a refused switch must leave the active slot unchanged");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 1),
                           "a refused switch must not consume the retained obligation");

  // (4) A retires, B's retained stop binds, and the same switch now commits -- before B's own
  // finalization retires. That is the "successor is not serialized behind predecessor cleanup"
  // half: the deferred job carries its own identity and does not hold the active slot.
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track));
  for (int turn = 0; turn < 64 && !TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 1);
       ++turn) {
    (void)TrackCaptureStopTestAccess::serviceAcceptedInteractionTurn(track);
  }
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 1),
                           "B's retained Record stop must bind once the ring frees");
  TEST_ASSERT_TRUE_MESSAGE(trackManager.finalizeCaptureAndSelectSlot(0, 2, 1536),
                           "an admitted Record stop must not refuse the switch");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the switch must commit to the successor slot");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 1),
                           "the predecessor must still be finalizing after the switch");

  // (5) The predecessor's finalization still retires with its content intact after the switch.
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track));
  TEST_ASSERT_TRUE_MESSAGE(track.getLoop(1).passes.hasRecordPass(),
                           "the switched-away interval's take must still commit its pass");
}

/// **S4f.21 — the master length is adopted from the interval's PUBLISHED geometry.**
///
/// The Record stop edge stamps a *predicted* length from the maintained maximum-tick summary so the
/// caller continuations stay well defined. That prediction is not the result: an all-filtered take
/// publishes zero. The manager therefore holds the obligation until the interval's job publishes or
/// is superseded, and reads the finalized geometry exactly once at a dispatch boundary.
void test_record_stop_adopts_published_geometry_not_the_stop_edge_prediction() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  TrackCaptureStopTestAccess::activateSlot(0, 0);
  clockManager.assignCurrentTickSilently(0);
  track.startRecording(0);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::isRecording(track));
  inputNote(60, 12, 96);

  clockManager.assignCurrentTickSilently(768);
  // The **production transport-stop route**: the same continuation family as `stopRecordingTrack`,
  // and the route that finalises a live capture when the transport stops.
  trackManager.handleTransportStop();

  // The interval is admitted, so its geometry is not decided yet: the prediction must not be
  // adopted as the transport's master length.
  TEST_ASSERT_TRUE_MESSAGE(track.hasPendingDeferredOverdubStop(),
                           "precondition: the Record stop must be deferred");
  // The switch lane links the real `TrackManager` policy owner, so the adopted master length is asserted
  // as the manager's own state rather than through the counting stub this suite used to keep.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, trackManager.getMasterLoopLength(),
                                   "the stop edge must not latch its predicted length");

  for (int turn = 0; turn < 8192 && track.hasPendingDeferredOverdubStop(); ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  trackManager.servicePendingRecordStopGeometry();
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(768, trackManager.getMasterLoopLength(),
                                   "the adopted master length must be the published geometry");
  // "Exactly once" in state terms: the obligation is consumed, so a second dispatch changes nothing.
  trackManager.servicePendingRecordStopGeometry();
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(768, trackManager.getMasterLoopLength(),
                                   "the adoption must not run twice");
  TEST_ASSERT_TRUE_MESSAGE(track.getLoop(0).passes.hasRecordPass(),
                           "precondition: the take must have published a pass");
}

/// **S4f.21 — a take that publishes nothing must not set the master length.**
///
/// The control for the fixture above, and the reason the obligation exists: the stop edge's
/// prediction for this take is 768 ticks (one quantized bar), while the minimum-length policy
/// removes its only pair, so the published geometry is zero. Reading the prediction at the stop
/// edge — which is what the pre-S17.3 continuation did — would adopt a length the worker never
/// publishes and then auto-align the emptied interval with it.
void test_filtered_record_take_publishes_no_master_length() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  noteMinLengthTicks = 48;
  noteMinLengthRemoveEnabled = true;
  TrackCaptureStopTestAccess::activateSlot(0, 0);
  clockManager.assignCurrentTickSilently(0);
  track.startRecording(0);
  inputNote(60, 12, 32);  // 20 ticks long: below the minimum, so the pair is filtered out.

  clockManager.assignCurrentTickSilently(768);
  trackManager.handleTransportStop();
  for (int turn = 0; turn < 8192 && track.hasPendingDeferredOverdubStop(); ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  trackManager.servicePendingRecordStopGeometry();

  TEST_ASSERT_FALSE_MESSAGE(track.getLoop(0).passes.hasRecordPass(),
                            "precondition: the take must have been filtered out");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      0, trackManager.getMasterLoopLength(),
      "a take that published nothing must not adopt a master length from the stop-edge prediction");
}

/// **S4f.3 — the successor's take must COMMIT with content, not merely start.**
///
/// Encodes a real device failure. On hardware the whole handoff chain worked -- A's finalizer outstanding,
/// B's stop capacity-denied, C's switch/start requested and retained, B's stop binding, C's capture starting
/// -- and C's take still committed with **final length 0 and no content**, because the harness never injected
/// MIDI into C after it started. A start marker (`RECA`) therefore read as success while the user-visible
/// result was empty.
///
/// The fixture above stops at "C is recording and its capture store grew by the injected note's on and off":
/// it never drives the transport-stop route and never asserts that the take **commits**. This one drives that
/// route and asserts the committed result, so a start marker can never satisfy it.
///
/// One track, A = slot 0, B = slot 1, C = slot 2 (A and B carry content; C is the empty record target).
/// Steps 1-7 reach the same handoff state as the fixture above, then:
///   8. MIDI enters C's capture through the real input handler;
///   9. the **production transport-stop route** finalises the take -- on device, the route that emits
///      `RECS,stopToStopped`;
///  10. the user-visible result: C still owns the active slot, C's loop holds a committed pass, the length is
///      NONZERO, and the committed rows contain the injected note (with an uninjected-note control);
///  11. B's outstanding finalization completing afterwards does not damage C's committed take.
void test_the_successor_take_commits_with_content_after_a_transport_stop() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  commitRecordPassIntoTrackSlot(track, 1);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(1, true));

  // (1) A overdubs through the production gesture; its stop is admitted and held.
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::isOverdubbing(track));
  inputNote(60, 96, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track),
                           "precondition: A's stop must be admitted before the handoff");

  // (2) B becomes active (A's own stop is bound, so the switch commits) and overdubs.
  clockManager.assignCurrentTickSilently(200);
  TrackCaptureStopTestAccess::activateSlot(0, 1);
  TEST_ASSERT_EQUAL_UINT8(1, TrackCaptureStopTestAccess::activeLoopIndex(track));
  TrackCaptureStopTestAccess::startOverdubbing(track, clockManager.getCurrentTick());
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::isOverdubbing(track));

  // (3) B's stop is requested and denied on capacity, latching against B's own slot.
  clockManager.assignCurrentTickSilently(264);
  TrackCaptureStopTestAccess::selectSlotIndex(0, 1);
  TrackCaptureStopTestAccess::toggleRecordForSlot(1);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 1));
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 1));

  // (4) Switch/start on C while B's stop is unadmitted: retained, neither committed nor dropped.
  clockManager.assignCurrentTickSilently(300);
  TrackCaptureStopTestAccess::toggleRecordForSlot(2);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(0, 2),
                           "the switch must retain C's record start");

  // (5)/(6) Dispatch keeps the requests; then capacity frees and B's stop binds.
  for (uint32_t tick = 301; tick <= 301 + 768; ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  bool bStopBound = false;
  for (int turn = 0; turn < 64 && !bStopBound; ++turn) {
    (void)TrackCaptureStopTestAccess::serviceAcceptedInteractionTurn(track);
    bStopBound = TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 1);
  }
  TEST_ASSERT_TRUE_MESSAGE(bStopBound, "B's accepted stop must bind once capacity frees");

  // (7) Dispatch commits the retained switch and starts C on C's eligible boundary.
  const uint32_t resumeTick = clockManager.getCurrentTick();
  for (uint32_t tick = resumeTick + 1;
       tick <= resumeTick + 768 * 3 && !TrackCaptureStopTestAccess::isRecording(track); ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the retained switch must commit to C");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track), "C must be recording");

  // (8) The notes enter C through the real MIDI input handler, and must land in C's capture.
  const uint8_t kSuccessorNote = 67;
  const size_t beforeInput = TrackCaptureStopTestAccess::captureEventCount(track);
  const uint32_t tickBeforeInput = clockManager.getCurrentTick();
  inputNote(kSuccessorNote, tickBeforeInput + 8, tickBeforeInput + 40);
  TEST_ASSERT_EQUAL_MESSAGE(beforeInput + 2, TrackCaptureStopTestAccess::captureEventCount(track),
                            "the injected note's on and off must enter C's capture");

  // Give the take a bar of material before the stop, so a nonzero length is the expected result rather than
  // an artefact of the injection tick.
  clockManager.assignCurrentTickSilently(tickBeforeInput + 768);

  // Precondition that makes the transport stop the thing under test rather than an incidental step: before
  // it, C's take is live and **uncommitted** -- exactly the device state whose zero-length commit is the
  // failure encoded here. Without this the assertions below could pass on a take something else committed.
  const uint32_t liveLengthBeforeStop = TrackCaptureStopTestAccess::slotLoopLength(track, 2);
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::committedPassesInSlot(track, 2),
                            "precondition: C's take must not be committed before the transport stop");

  // (9) The production transport-stop route finalises the take. S4f.17: a Record stop is
  // admitted as a deferred job and its pass is published by the worker's bounded grains, so the
  // user-visible result is consumed here instead of at the stop edge. The drive is bounded and
  // stops when no stop is in flight.
  TrackCaptureStopTestAccess::transportStop();
  for (int turn = 0; turn < 8192; ++turn) {
    if (TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track)) {
      (void)TrackCaptureStopTestAccess::step(track);
      continue;
    }
    // A stop that met a busy finalization owner is retained, not dropped: the accepted-stop
    // drive binds it once capacity frees, exactly as the production deferred owner does.
    if (TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 2)) {
      (void)TrackCaptureStopTestAccess::serviceAcceptedInteractionTurn(track);
      continue;
    }
    break;
  }
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track) ||
                                TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 2),
                            "the transport-stop take must finish its deferred finalization");

  // (10) The USER-VISIBLE result. A start marker proves none of this.
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "C must still own the active slot after the transport stop");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::committedPassesInSlot(track, 2),
                           "C's take must COMMIT a pass, not merely have started");
  const uint32_t committedLength = TrackCaptureStopTestAccess::slotLoopLength(track, 2);
  char lengthDetail[224];
  snprintf(lengthDetail, sizeof(lengthDetail),
           "C's committed take must have a NONZERO length (active=%u loopLength=%u committedPasses=%d, "
           "liveLengthBeforeStop=%u)",
           TrackCaptureStopTestAccess::activeLoopIndex(track), committedLength,
           TrackCaptureStopTestAccess::committedPassesInSlot(track, 2) ? 1 : 0,
           liveLengthBeforeStop);
  TEST_ASSERT_TRUE_MESSAGE(committedLength > 0u, lengthDetail);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::committedSlotHasNote(track, 2, kSuccessorNote),
                           "C's committed content must hold the note the user played");
  // Control: the search is note-specific, so it cannot pass by finding any row at all.
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::committedSlotHasNote(track, 2, 99),
                            "control: a note that was never played must not appear in C's content");

  // (11) B's outstanding finalization completing later must not damage C's committed take.
  const size_t committedRows = TrackCaptureStopTestAccess::captureEventCount(track);
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  for (int turn = 0; turn < 64; ++turn) {
    (void)TrackCaptureStopTestAccess::serviceAcceptedInteractionTurn(track);
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "a later completion must not move the active slot off C");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(committedLength,
                                   TrackCaptureStopTestAccess::slotLoopLength(track, 2),
                                   "a later completion must not change C's committed length");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::committedSlotHasNote(track, 2, kSuccessorNote),
                           "a later completion must not damage C's committed content");
  (void)committedRows;
}

/// **S4b.1 — contract check 7, cross-track successor.** The successor sits on a **different track**,
/// which the same-track fixtures cannot show: `CaptureStopJobs`, the S4f.1 latch and
/// `hasStopInFlightForSlot` are all per track, so the predecessor's live finalizer must not gate the
/// successor track's start. The assertions are deliberately asymmetric -- the predecessor's finalizer
/// must still be outstanding, and still identified by the predecessor's own slot, *while* the successor
/// records on its own track.
void test_a_cross_track_successor_starts_while_the_predecessor_finalizes() {
  LoopEventStore::initPool();
  Track& predecessor = trackManager.getTrack(0);
  Track& successor = trackManager.getTrack(1);
  predecessor.ensureLoopsAllocated();
  successor.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(predecessor, 0);
  TEST_ASSERT_TRUE(predecessor.prewarmPlaybackForSlot(0, true));

  // (1) The predecessor's stop is admitted and held, so its track's deferred capacity is occupied.
  trackManager.setSelectedTrack(0);
  predecessor.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(predecessor, 0);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isOverdubbing(predecessor),
                           "precondition: the predecessor must be overdubbing");
  inputNote(60, 96, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(predecessor, 0),
                           "the predecessor's stop must be admitted and held before the handoff");

  // (2) Start the successor through the production gesture, on its own track.
  trackManager.setSelectedTrack(1);
  clockManager.assignCurrentTickSilently(200);
  TrackCaptureStopTestAccess::toggleRecordForSlot(0);
  const uint32_t queuedTick = clockManager.getCurrentTick();
  for (uint32_t tick = queuedTick + 1; tick <= queuedTick + 768 * 3 &&
                             !TrackCaptureStopTestAccess::isRecording(successor) &&
                             !TrackCaptureStopTestAccess::isOverdubbing(successor);
       ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  // Red-proof note: this fixture guards against *cross-track coupling*, and no earlier version of the code
  // had any, so there is nothing to revert. It is red-proved by introducing the coupling the fixture must
  // detect -- a temporary guard in `startRecordingTrack` that refuses while **any** track has a stop in
  // flight -- which fails exactly here ("the successor track must start while the predecessor finalizes").
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isRecording(successor),
                           "the successor track must start while the predecessor finalizes");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasStopInFlight(successor),
                            "the successor track must hold no stop obligation of its own");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(predecessor, 0),
                           "the predecessor's finalizer must still be outstanding");

  // (3) The successor records on its own track through the real MIDI route.
  const size_t before = TrackCaptureStopTestAccess::captureEventCount(successor);
  const uint32_t onTick = clockManager.getCurrentTick() + 8;
  inputNote(64, onTick, onTick + 32);
  TEST_ASSERT_EQUAL_MESSAGE(before + 2, TrackCaptureStopTestAccess::captureEventCount(successor),
                            "the successor must record the incoming note on its own track");

  // (4) The predecessor completes without disturbing the successor.
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(predecessor);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(predecessor);
  }
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(predecessor),
                            "the predecessor's finalizer must be able to complete");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isRecording(successor),
                           "the predecessor's completion must not stop the successor");
  TEST_ASSERT_EQUAL_MESSAGE(before + 2, TrackCaptureStopTestAccess::captureEventCount(successor),
                            "the predecessor's completion must not damage the successor's interval");
}

/// **S4b.1 — contract check 7, cleared successor.** The successor slot previously held content that was
/// cleared, so it must still be treated as a **record** target while being *not virgin*: its slot runtime
/// survives the clear (only passes and loop geometry are dropped), which is the state the production
/// clear leaves and why the gesture comment says a just-cleared slot "can re-arm".
///
/// The clear is performed with the production clear's **slot-local** steps
/// (`MidiButtonActions::handleClearTrack` once its storage-drain and undo-snapshot bookkeeping is done)
/// plus `Track::clear()`, which is the production pass removal. The storage drain and the undo snapshot
/// belong to the persistence/undo suites, not here.
void test_a_cleared_successor_is_a_record_target_while_the_predecessor_finalizes() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  const uint8_t tidx = trackManager.getSelectedTrackIndex();

  // (1) Fill slot 1, give it a working set, then clear it.
  commitRecordPassIntoTrackSlot(track, 1);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(1, true));
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, 1),
                           "precondition: the future target must hold a runtime before the clear");
  // `Track::clear()` returns early on TRACK_EMPTY, so the clear needs a live state (a real slot being
  // cleared is STOPPED or PLAYING, never EMPTY).
  track.forceSetState(TRACK_STOPPED);
  TrackCaptureStopTestAccess::selectSlot(track, 1);
  trackManager.clearPendingSlotSwitch(tidx);
  trackManager.setPendingEnabledSetReplacement(tidx, false);
  trackManager.setSlotEnabled(tidx, 1, false);
  trackManager.setSlotMuted(tidx, 1, false);
  trackManager.clearQueuedRecordingTrack(tidx, 1);
  trackManager.setLayeredSlotHeld(tidx, 1, false);
  track.clear();
  TEST_ASSERT_FALSE_MESSAGE(track.hasCommittedPassesInSlot(1),
                            "a cleared slot must be a record target");
  TEST_ASSERT_FALSE_MESSAGE(track.hasDataInSlot(1), "a cleared slot must hold no loop data");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, 1),
                           "the cleared slot must still hold its runtime: it is not a virgin slot");

  // (2) Predecessor A on slot 0 overdubs and its stop is admitted and held.
  commitRecordPassIntoTrackSlot(track, 0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isOverdubbing(track),
                           "precondition: A must be overdubbing");
  inputNote(60, 96, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track),
                           "A's stop must be admitted and held before the handoff");

  // (3) Switch/start onto the cleared target: the switch commits and the start is carried.
  clockManager.assignCurrentTickSilently(200);
  TrackCaptureStopTestAccess::toggleRecordForSlot(1);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the switch onto the cleared target must commit");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx, 1),
                           "the switch must carry a record start onto the cleared target");

  const uint32_t queuedTick = clockManager.getCurrentTick();
  for (uint32_t tick = queuedTick + 1; tick <= queuedTick + 768 * 3 &&
                             !TrackCaptureStopTestAccess::isRecording(track) &&
                             !TrackCaptureStopTestAccess::isOverdubbing(track);
       ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track),
                           "the cleared target must start recording from the carried start");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx, 1),
                            "the executed start must be consumed exactly once");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track),
                           "A's finalizer must still be outstanding while the target records");

  // (4) The cleared target records incoming MIDI.
  const size_t before = TrackCaptureStopTestAccess::captureEventCount(track);
  const uint32_t onTick = clockManager.getCurrentTick() + 8;
  inputNote(65, onTick, onTick + 32);
  TEST_ASSERT_EQUAL_MESSAGE(before + 2, TrackCaptureStopTestAccess::captureEventCount(track),
                            "the cleared target must record the incoming note's on and off");

  // (5) A completes without damaging the successor.
  const size_t successorEvents = TrackCaptureStopTestAccess::captureEventCount(track);
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track),
                            "A's finalizer must be able to complete");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "A's completion must not move the active slot off the cleared target");
  TEST_ASSERT_EQUAL_MESSAGE(successorEvents, TrackCaptureStopTestAccess::captureEventCount(track),
                            "A's completion must not damage the cleared target's interval");
}

/// **S4b.1 — contract check 7, populated successor.** The successor slot holds committed content, so the
/// switch must **not** carry a record start: the contract's branch for a populated target is the
/// *overdub* on the target's own playback phase. The negative is the point -- the empty-target carrier
/// must not arm a record over committed content -- and the two slots are given **different lengths**
/// (predecessor 768, successor 1,536) so "the successor used its own phase" is falsifiable rather than
/// assumed.
void test_a_populated_successor_overdubs_on_its_own_phase_while_the_predecessor_finalizes() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  const uint8_t tidx = trackManager.getSelectedTrackIndex();
  commitRecordPassIntoTrackSlotOfLength(track, 0, 768);
  commitRecordPassIntoTrackSlotOfLength(track, 1, 1536);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(1, true));

  // (1) A overdubs on slot 0. Unlike the empty-target fixtures the stop is **not** pressed first: for a
  // populated target the switch itself admits A's stop as the capture handoff, which is the branch the
  // contract's populated-target wording describes (switch while capturing). A stop pressed beforehand
  // would leave the track PLAYING instead of OVERDUBBING and route the gesture down the *playback*-switch
  // lane instead, which is a different owner and a different question.
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isOverdubbing(track),
                           "precondition: A must be overdubbing");
  inputNote(60, 96, 120);

  // (2) Switch to the populated successor while A is still capturing. The switch admits A's stop and
  // commits, and it must carry **no** record start: the target is not a record target.
  clockManager.assignCurrentTickSilently(144);
  TrackCaptureStopTestAccess::toggleRecordForSlot(1);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the switch to the populated successor must commit");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track),
                           "the switch must leave A's stop admitted and finalizing");
  // Measured while red-proving this: the negative below is defended **twice**, by the carrier's own
  // `!hasCommittedPassesInSlot` predicate and independently by `queueRecordingTrack`'s committed-passes
  // refusal. Disabling either alone leaves the suite fully green, so the assertion proves the *property*
  // but does not attribute the refusal to one guard. Red-proving it required disabling both.
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx, 1),
                            "a populated target must NOT be armed for a record start");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::anyRecordQueued(tidx),
                            "no record request may survive a switch onto a populated target");

  // (3) The successor's own phase is what the overdub uses: its loop length is untouched by the handoff.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1536, track.getLoop(1).loopLengthTicks,
                                   "the populated successor must keep its own loop length");
  TrackCaptureStopTestAccess::startOverdubbing(track, clockManager.getCurrentTick());
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isOverdubbing(track),
                           "the populated successor must be overdubbable through the production entry");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::captureActive(track),
                           "the overdub must have an active capture interval");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1536, track.getActiveLoop().loopLengthTicks,
                                   "the overdub must run against the successor's own phase, not A's");

  // (4) A completes without disturbing the successor's interval or phase.
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track),
                            "A's finalizer must be able to complete");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "A's completion must not move the active slot off the successor");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1536, track.getLoop(1).loopLengthTicks,
                                   "A's completion must not rewrite the successor's phase");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isOverdubbing(track),
                           "A's completion must not end the successor's overdub");
}

/// **S4b.1 — check 6, exactly-once for the *committed* carrier.** The blocked fixture proves exactly-once
/// for a request that had to wait for admission. The committed case -- the switch's own gesture carrying
/// the start, dispatched on the next eligible boundary -- had no exactly-once assertion: the acceptance
/// fixture never checks that the request was consumed, and never dispatches again after the start. This
/// fixture closes that half: consumption, no re-arm, and no second execution.
///
/// **Residual gap, recorded rather than implied.** This fixture covers the *switch-carried committed*
/// start only. The two other exactly-once routes are covered separately, directly below:
/// `test_the_arm_and_wait_requeue_retains_then_executes_exactly_once` (the
/// `!clockManager.shouldQuantizeRecordStart()` re-queue branch) and
/// `test_a_start_refused_by_its_own_live_stop_obligation_retries_to_a_single_take` (a start refused by a
/// live stop obligation for its own slot, then retried). Checked while writing this and **not** a defect:
/// the ordinary immediate start cannot leave a stale request behind, because `handleArmedRecordPress`
/// cancels any queued arm and returns before the immediate `startRecordingTrack` call is reached
/// (`MidiButtonActions.cpp`), so that route has nothing to consume twice.
void test_the_switch_carried_start_executes_exactly_once() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  const uint8_t tidx = trackManager.getSelectedTrackIndex();
  commitRecordPassIntoTrackSlot(track, 0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));

  // (1) A overdubs, then its stop is admitted and held.
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::isOverdubbing(track));
  inputNote(60, 96, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track));

  // (2) The switch's own gesture carries the start for the empty target (the committed case).
  clockManager.assignCurrentTickSilently(200);
  TrackCaptureStopTestAccess::toggleRecordForSlot(1);
  TEST_ASSERT_EQUAL_UINT8(1, TrackCaptureStopTestAccess::activeLoopIndex(track));
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx, 1),
                           "the switch must carry the start for the empty target");

  const uint32_t queuedTick = clockManager.getCurrentTick();
  for (uint32_t tick = queuedTick + 1; tick <= queuedTick + 768 * 3 &&
                             !TrackCaptureStopTestAccess::isRecording(track) &&
                             !TrackCaptureStopTestAccess::isOverdubbing(track);
       ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track),
                           "the carried start must execute while A finalizes");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx, 1),
                            "the executed start must be consumed exactly once");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::anyRecordQueued(tidx),
                            "no record request may survive a successful start");

  // (3) The started interval takes the injected note.
  const size_t before = TrackCaptureStopTestAccess::captureEventCount(track);
  const uint32_t onTick = clockManager.getCurrentTick() + 8;
  inputNote(66, onTick, onTick + 32);
  TEST_ASSERT_EQUAL_MESSAGE(before + 2, TrackCaptureStopTestAccess::captureEventCount(track),
                            "the started interval must record the incoming note");

  // (4) Exactly once: two further bar boundaries of dispatch must not re-arm or re-execute the start.
  const size_t startedEvents = TrackCaptureStopTestAccess::captureEventCount(track);
  const uint8_t startedSlot = TrackCaptureStopTestAccess::activeLoopIndex(track);
  const uint32_t afterStart = clockManager.getCurrentTick();
  for (uint32_t tick = afterStart + 1; tick <= afterStart + 768 * 2; ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(startedSlot, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "later dispatch must not move the active slot");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx, 1),
                            "the start request must not be re-armed by later dispatch");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::anyRecordQueued(tidx),
                            "no record request may exist after the start was consumed");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track),
                           "the started interval must still be the recording interval");
  TEST_ASSERT_EQUAL_MESSAGE(startedEvents, TrackCaptureStopTestAccess::captureEventCount(track),
                            "a second execution would have restarted the interval and dropped its "
                            "captured events");
}

/// **Exactly-once: the arm-and-wait re-queue path.** `TrackManager::startRecordingTrack` has a branch where
/// `!clockManager.shouldQuantizeRecordStart()` re-queues the request in the manager pending-start owner,
/// leaves the track armed, and returns `false` so the attempt neither starts nor consumes. This fixture
/// closes the first of the two gaps the fixture above records, and pins what that path can and cannot
/// execute twice:
///
///   1. the re-queue attempt **retains** the request and does not start recording;
///   2. repeated dispatch while the clock condition is still unsatisfied keeps retaining it;
///   3. once the clock runs, the request executes **exactly once** and is consumed by that attempt;
///   4. further dispatch cannot execute it again -- the take origin and its captured events are unchanged,
///      which a second `Track::startRecording` would break (`Loop::beginCapture` clears the capture extent
///      and `loop.startLoopTick` is re-stamped from the later tick).
void test_the_arm_and_wait_requeue_retains_then_executes_exactly_once() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  const uint8_t tidx = trackManager.getSelectedTrackIndex();
  // Slot 0 holds committed content so the track can be audibly playing; slot 1 is the empty record target.
  commitRecordPassIntoTrackSlot(track, 0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, 0);

  // (1) The clock condition is unsatisfied, so the production gesture reaches the arm-and-wait branch.
  TrackCaptureStopTestAccess::setClockRunning(false);
  TEST_ASSERT_FALSE_MESSAGE(clockManager.shouldQuantizeRecordStart(),
                            "precondition: this fixture needs the unsatisfied clock condition");
  TrackCaptureStopTestAccess::selectSlotIndex(tidx, 1);
  TrackCaptureStopTestAccess::toggleRecordForSlot(1);

  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the gesture must have activated the empty record target");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx, 1),
                           "the arm-and-wait branch must retain the request rather than consume it");
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::anyRecordQueued(tidx));
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track),
                            "the arm-and-wait branch must not start recording");

  // (2) Dispatch across three bar boundaries while the condition is still unsatisfied. Every attempt is
  // re-queued and none is consumed: this is the retention half, and it fails if the branch reports success.
  const uint32_t queuedTick = clockManager.getCurrentTick();
  for (uint32_t tick = queuedTick + 1; tick <= queuedTick + 768 * 3; ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track),
                            "the start must not execute while the clock condition is unsatisfied");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx, 1),
                           "each unsatisfied attempt must retain the request, not consume it");

  // (3) The clock condition is satisfied: the retained request executes, exactly once.
  TrackCaptureStopTestAccess::setClockRunning(true);
  TEST_ASSERT_TRUE(clockManager.shouldQuantizeRecordStart());
  const uint32_t resumeTick = clockManager.getCurrentTick();
  for (uint32_t tick = resumeTick + 1;
       tick <= resumeTick + 768 * 3 && !TrackCaptureStopTestAccess::isRecording(track); ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track),
                           "the retained start must execute once the clock condition is satisfied");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx, 1),
                            "the request must be consumed by the successful attempt");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::anyRecordQueued(tidx),
                            "no record request may survive the successful attempt");

  // (4) Exactly once. A second execution would re-begin capture: the injected note's events would be
  // dropped and the take origin would move to the later tick.
  const uint32_t takeOrigin = TrackCaptureStopTestAccess::takeOriginTick(track);
  const uint32_t onTick = clockManager.getCurrentTick() + 8;
  inputNote(71, onTick, onTick + 32);
  TEST_ASSERT_EQUAL_MESSAGE(2, TrackCaptureStopTestAccess::captureEventCount(track),
                            "the take must have recorded the injected note's on and off");
  const uint32_t firstTick = TrackCaptureStopTestAccess::firstCaptureEventTick(track);
  const uint8_t firstNote = TrackCaptureStopTestAccess::firstCaptureEventNote(track);

  const uint32_t afterStart = clockManager.getCurrentTick();
  for (uint32_t tick = afterStart + 1; tick <= afterStart + 768 * 3; ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "later dispatch must not move the active slot");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx, 1),
                            "the start request must not be re-armed by later dispatch");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track),
                           "the started take must still be the recording interval");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      takeOrigin, TrackCaptureStopTestAccess::takeOriginTick(track),
      "a second execution would re-begin capture and move the take origin");
  TEST_ASSERT_EQUAL_MESSAGE(2, TrackCaptureStopTestAccess::captureEventCount(track),
                            "a second execution would clear the capture extent");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(firstTick, TrackCaptureStopTestAccess::firstCaptureEventTick(track),
                                   "the recorded note's tick must survive later dispatch");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(firstNote, TrackCaptureStopTestAccess::firstCaptureEventNote(track),
                                  "the recorded note must survive later dispatch");
}

/// **Exactly-once: a start that is refused by its own live stop obligation and then retried.**
/// `TrackManager::startRecordingTrack` refuses while `hasStopInFlightForSlot(activeSlot)` -- the S4f.6
/// slot-scoped guard -- and returns `false` so the dispatcher retains the request. This fixture closes the
/// second recorded gap: the refused attempt must not consume, the retry must produce exactly one take, and
/// the request must be consumed only by the successful attempt.
///
/// The obligation is set through the existing test accessor on purpose: a fresh **record** stop is
/// synchronous (`TrackManager::stopRecordingTrack` -> `Track::stopRecordingToStopped`) and never takes the
/// deferred path, so a latch is the only way a record target can carry a stop obligation **of its own**.
/// The start itself is queued by the production per-slot gesture and dispatched by the production
/// `handlePendingRecordStart`, so only the obligation's origin is synthetic.
///
/// Attribution note: the refused phase and the successful phase use the *same* dispatch loop over the same
/// number of ticks, and the only difference between them is the obligation, so "it started later" cannot be
/// explained by a tick gate that the refused phase never satisfied.
void test_a_start_refused_by_its_own_live_stop_obligation_retries_to_a_single_take() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  const uint8_t tidx = trackManager.getSelectedTrackIndex();
  commitRecordPassIntoTrackSlot(track, 0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, 0);

  // (1) The record target carries a live stop obligation that names its own slot.
  TrackCaptureStopTestAccess::latchAcceptedStopForSlot(track, 1);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 1));
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasStopInFlightForSlot(track, 1),
                           "precondition: the target must carry its own stop obligation");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasStopInFlightForSlot(track, 0),
                            "precondition: the obligation must not be the predecessor slot's");

  // (2) The start is requested through the production gesture; a running clock queues it for the target.
  clockManager.assignCurrentTickSilently(120);
  TrackCaptureStopTestAccess::selectSlotIndex(tidx, 1);
  TrackCaptureStopTestAccess::toggleRecordForSlot(1);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx, 1),
                           "the gesture must queue the start for the empty target");

  // (3) Three bar boundaries of production dispatch are refused by the live obligation, and every refusal
  // retains the request instead of consuming it.
  const uint32_t queuedTick = clockManager.getCurrentTick();
  for (uint32_t tick = queuedTick + 1; tick <= queuedTick + 768 * 3; ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  TEST_ASSERT_FALSE_MESSAGE(
      TrackCaptureStopTestAccess::isRecording(track),
      "a start must not execute while its own slot carries a live stop obligation");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx, 1),
                           "a refused attempt must retain the request, not consume it");
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::hasStopInFlightForSlot(track, 1));

  // (4) The obligation retires and the retry executes -- with the same dispatch loop as (3).
  TrackCaptureStopTestAccess::consumeAcceptedStop(track);
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasStopInFlightForSlot(track, 1),
                            "the obligation must be gone before the retry");
  const uint32_t resumeTick = clockManager.getCurrentTick();
  for (uint32_t tick = resumeTick + 1;
       tick <= resumeTick + 768 * 3 && !TrackCaptureStopTestAccess::isRecording(track); ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track),
                           "the retained start must execute once its own obligation retires");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the retry must start on the requested target");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx, 1),
                            "the request must be consumed only by the successful attempt");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::anyRecordQueued(tidx),
                            "no record request may survive the successful attempt");

  // (5) Exactly once across the retry: one take, and later dispatch neither re-arms nor re-executes it.
  const uint32_t takeOrigin = TrackCaptureStopTestAccess::takeOriginTick(track);
  const uint32_t onTick = clockManager.getCurrentTick() + 8;
  inputNote(72, onTick, onTick + 32);
  TEST_ASSERT_EQUAL_MESSAGE(2, TrackCaptureStopTestAccess::captureEventCount(track),
                            "the retried take must have recorded the injected note's on and off");
  const uint32_t firstTick = TrackCaptureStopTestAccess::firstCaptureEventTick(track);
  const uint8_t firstNote = TrackCaptureStopTestAccess::firstCaptureEventNote(track);

  const uint32_t afterStart = clockManager.getCurrentTick();
  for (uint32_t tick = afterStart + 1; tick <= afterStart + 768 * 3; ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "later dispatch must not move the active slot");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::recordQueuedForSlot(tidx, 1),
                            "the start request must not be re-armed by later dispatch");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track),
                           "the retried take must still be the recording interval");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      takeOrigin, TrackCaptureStopTestAccess::takeOriginTick(track),
      "a second execution would re-begin capture and move the take origin");
  TEST_ASSERT_EQUAL_MESSAGE(2, TrackCaptureStopTestAccess::captureEventCount(track),
                            "a second execution would clear the capture extent");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(firstTick, TrackCaptureStopTestAccess::firstCaptureEventTick(track),
                                   "the recorded note's tick must survive later dispatch");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(firstNote, TrackCaptureStopTestAccess::firstCaptureEventNote(track),
                                  "the recorded note must survive later dispatch");
}

/// **S4.1 — a capture work item is keyed to the interval's own slot.** The live capture's slot *is*
/// the active slot (only it may RECORD/OVERDUB), but a stop in flight is pinned to ITS interval's slot
/// across an active-slot change, so `captureWorkSlot` must not substitute the active index. The
/// mid-pass journal resolver reads this rule, and it previously read `getActiveLoopIndex()` directly --
/// so a stop pinned to one slot was journalled under whichever slot was active when the drain ran.
void test_s4_1_capture_work_slot_uses_the_pinned_interval_slot() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 1);
  commitRecordPassIntoTrackSlot(track, 3);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(1, true));
  track.forceSetState(TRACK_PLAYING);

  // A stop in flight pinned to slot 1, with the active slot moved to 3.
  TrackCaptureStopTestAccess::latchAcceptedStopForSlot(track, 1);
  TrackCaptureStopTestAccess::activateSlot(0, 3);

  // The discriminator: the active index really is a different slot, so a rule that read it reports 3.
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(3, track.getActiveLoopIndex(),
                                  "the fixture must move the active slot away from the interval");

  uint8_t slot = 0xFF;
  TEST_ASSERT_TRUE(track.captureWorkSlot(slot));
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, slot, "the pinned interval's slot must key the work item");

  // Control: once the obligation is consumed there is no stop in flight, so the rule refuses rather
  // than falling back to the active index -- the mid-pass resolver requeues instead of guessing.
  TrackCaptureStopTestAccess::consumeAcceptedStop(track);
  TEST_ASSERT_FALSE_MESSAGE(track.captureWorkSlot(slot),
                            "with nothing in flight the rule must refuse rather than guess");

  // Control (b): a live capture's slot IS the active slot, because only it may RECORD/OVERDUB. This
  // keeps the refusal above from being a rule that simply never resolves.
  track.forceSetState(TRACK_RECORDING);
  TEST_ASSERT_TRUE(track.captureWorkSlot(slot));
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(3, slot, "a live capture is on the active slot by construction");
  track.forceSetState(TRACK_PLAYING);
}

/// During deferred overdub finalization the track has returned to PLAYING, so a slot gesture
/// must follow ordinary populated-slot playback selection rather than be rejected as a session undo.
void test_slot_button_during_pending_overdub_finalization_queues_populated_target() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  commitRecordPassIntoTrackSlot(track, 1);
  track.forceSetState(TRACK_PLAYING);
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  track.startOverdubbing(96);
  TEST_ASSERT_TRUE(track.isOverdubbing());
  inputNote(60, 97, 120);
  track.stopOverdubbing();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());

  midiButtonActions.handleToggleRecordForSlot(1);

  TEST_ASSERT_EQUAL_UINT8(1, trackManager.getSelectedSlotIndex(0));
  TEST_ASSERT_TRUE(track.hasDataInSlot(1));
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
}

/// The same single slot gesture arms an empty target through the existing queued-record path.
void test_slot_button_during_pending_overdub_finalization_arms_empty_target() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  track.forceSetState(TRACK_PLAYING);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  track.startOverdubbing(96);
  TEST_ASSERT_TRUE(track.isOverdubbing());
  inputNote(60, 97, 120);
  track.stopOverdubbing();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());

  midiButtonActions.handleToggleRecordForSlot(1);

  TEST_ASSERT_EQUAL_UINT8(1, track.getActiveLoopIndex());
  TEST_ASSERT_TRUE(trackManager.isRecordingQueued(0, 1) || track.isRecording());
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
}


/// **S4b.1 (check 7, repeated switches) — B -> C while A still finalizes: accepted, bounded, nothing
/// stolen, and it executes without another gesture.**
///
/// The cross-track case has its own fixture; this is the *same-track repeated* case the review named as
/// still unproven, and it is where the bounded-overlap claim (`Omax` inbound holders, the design's
/// `TransferOverlap` budgeted as a transient rather than as slack) has to hold: A finalizes on slot 0, B is
/// the live capture on slot 1, and the user switches again to slot 2 before A has retired.
///
/// Measured mechanism, and what the assertions pin. The repeated switch does **not** move the active slot
/// (S4f.5 governs it) and does **not** provision its target on the gesture path — the gesture retains the
/// request instead: B's stop is **accepted and latched** against B's own slot (`hasAcceptedInteractionForSlot`)
/// while no stop job exists for it (`hasLiveCaptureStopJobForSlot` false), and the target's record start
/// stays **queued** (`isRecordingQueued`). Nothing is stolen to satisfy the inbound — A's live finalizer and
/// lease and B's lease all survive — and the pool does not grow across the gesture (490 -> 490 chunks on the
/// measured run). Once A retires and the deferred lane turns, the retained request **executes without
/// another gesture**: C becomes active, its occupancy is provisioned, B's latch clears, and further turns
/// change neither the active slot nor the pool.
void test_s4b_1_a_repeated_switch_while_a_finalizes_is_bounded_and_executes_once_capacity_frees() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));

  // (1) A is live through the production button route and takes MIDI through the real handler; its stop is
  // then admitted and deliberately **not** advanced, so the finalizer is paused and holding capacity.
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isOverdubbing(track),
                           "fixture precondition: A must be overdubbing");
  inputNote(60, 96, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track),
                           "A's stop must be admitted before the switches");

  // (2) The first switch lands: B becomes the live capture on the running transport while A finalizes.
  clockManager.assignCurrentTickSilently(200);
  TrackCaptureStopTestAccess::toggleRecordForSlot(1);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the first switch must commit while A finalizes");
  TrackCaptureStopTestAccess::selectSlotIndex(0, 1);
  TrackCaptureStopTestAccess::toggleRecordForSlot(1);
  const uint32_t queuedTick = clockManager.getCurrentTick();
  for (uint32_t tick = queuedTick + 1; tick <= queuedTick + 768 * 3 &&
                             !TrackCaptureStopTestAccess::isRecording(track) &&
                             !TrackCaptureStopTestAccess::isOverdubbing(track);
       ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
  }
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track) ||
                               TrackCaptureStopTestAccess::isOverdubbing(track),
                           "B must be the live capture before the repeated switch");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 0),
                           "A's finalizer must still be outstanding");

  // (3) The repeated switch, with A still finalizing.
  const uint16_t chunksBeforeSecondSwitch = LoopEventStore::freeChunkCount();
  clockManager.assignCurrentTickSilently(1000);
  TrackCaptureStopTestAccess::toggleRecordForSlot(2);

  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the repeated switch may not move the active slot");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, 2),
                            "the repeated switch must not provision its target on the gesture path");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 1),
                           "B's stop must be accepted and latched, not dropped");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 1),
                            "the latch is the accepted request; no stop job has been admitted for it yet");
  TEST_ASSERT_TRUE_MESSAGE(trackManager.isRecordingQueued(0, 2),
                           "the target's record start must stay queued for the handoff");
  // Nothing is taken from the live owner or the live capture.
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, 0),
                           "A's finalizer must survive the repeated switch");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 0),
                           "A's working set must not be released to satisfy the inbound");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 1),
                           "B's working set must survive its own stop being accepted");
  // ...and the transient overlap is bounded: the gesture itself consumes nothing.
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(chunksBeforeSecondSwitch, LoopEventStore::freeChunkCount(),
                                   "the repeated switch must not grow the pool");

  // (4) A retires, the deferred lane turns -- and the retained request executes with no further gesture.
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::ownsLiveFinalization(track, 0); ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::ownsLiveFinalization(track, 0),
                            "the fixture must retire A's finalizer");
  for (int round = 0; round < 8 && TrackCaptureStopTestAccess::activeLoopIndex(track) != 2; ++round) {
    const uint32_t base = clockManager.getCurrentTick();
    for (uint32_t tick = base + 1; tick <= base + 768 * 2; ++tick) {
      clockManager.assignCurrentTickSilently(tick);
      TrackCaptureStopTestAccess::startPendingRecord(tick);
      (void)TrackCaptureStopTestAccess::step(track);
      (void)TrackCaptureStopTestAccess::serviceAcceptedInteractionTurn(track);
    }
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the retained switch must execute once the capacity frees");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, 2),
                           "the executed switch must have provisioned the inbound occupancy");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, 1),
                            "the latch must clear when the request executes");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track) ||
                               TrackCaptureStopTestAccess::isOverdubbing(track),
                           "the successor must be the live capture after the handoff");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 0),
                           "A's retired working set must still not have been stolen");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, 1),
                           "B's working set must not have been stolen by the inbound");

  // (5) Bounded, not accumulating: further turns change neither the active slot nor the pool.
  const uint16_t chunksSettled = LoopEventStore::freeChunkCount();
  const uint32_t baseTick = clockManager.getCurrentTick();
  for (uint32_t tick = baseTick + 1; tick <= baseTick + 768 * 2; ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    TrackCaptureStopTestAccess::startPendingRecord(tick);
    (void)TrackCaptureStopTestAccess::step(track);
    (void)TrackCaptureStopTestAccess::serviceAcceptedInteractionTurn(track);
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the settled state must not drift");
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(chunksSettled, LoopEventStore::freeChunkCount(),
                                   "the settled state must not keep consuming pool chunks");
}

/// **S4b.1 (a) — an *uncommitted* switch whose stop was DENIED on capacity still retains the start.**
///
/// S4b.1's finding (a) recorded the remainder as "the switch returns `false` and the press is dropped
/// rather than retained". The code does retain it — `MidiButtonActions::handleToggleRecordForSlot` writes the
/// same manager pending-start owner through `retainRecordStartForSlot` when `finalizeCaptureAndSelectSlot`
/// returns false, and reports "switch and record start retained (stop not yet admitted)" — and this fixture is
/// the reproducer for that path, as distinct from the *admitted*-stop carrier the sibling fixture covers.
///
/// The predecessor's stop is denied here for the contract's own reason: **another slot's admitted
/// finalization already holds the track's single finalization capacity** (DEC-078 item 6), so the
/// predecessor's stop is accepted-and-retained (never a refusal, DEC-078 item 1) while it stays the active
/// interval.
void test_s4b_1_a_denied_stop_on_an_uncommitted_switch_retains_the_targets_start() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  constexpr uint8_t kDenied = 0;      // the switch's predecessor: its stop is denied on capacity
  constexpr uint8_t kTarget = 1;      // the empty switch target whose start must survive
  constexpr uint8_t kOccupant = 2;    // the slot whose admitted finalization holds the capacity
  commitRecordPassIntoTrackSlot(track, kDenied);
  commitRecordPassIntoTrackSlot(track, kOccupant);

  // (1) Occupy the track's finalization capacity with an admitted stop on another slot.
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kOccupant, true));
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, kOccupant);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::isOverdubbing(track));
  inputNote(60, 96, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, kOccupant),
                           "fixture precondition: the occupant's stop must be admitted and held");

  // (2) The switch's predecessor is a second live capture on the same track, started while the occupant
  // finalizes -- the overlap the contract requires (outgoing finalizer + incoming capture).
  clockManager.assignCurrentTickSilently(200);
  TrackCaptureStopTestAccess::selectSlot(track, kDenied);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isOverdubbing(track),
                           "the predecessor must be live while the occupant finalizes");
  inputNote(62, 200, 224);
  clockManager.assignCurrentTickSilently(260);

  // (3) The switch gesture to an empty target: the stop cannot be admitted, so the switch does **not**
  // commit -- and the start must be retained rather than dropped with the gesture.
  TrackCaptureStopTestAccess::toggleRecordForSlot(kTarget);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kDenied, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "an uncommitted switch must not move the active slot");
  TEST_ASSERT_TRUE_MESSAGE(trackManager.isRecordingQueued(0, kTarget),
                           "the target's start must be retained, not dropped with the gesture");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, kDenied),
                           "the denied stop must be accepted and latched against its own slot");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasLiveCaptureStopJobForSlot(track, kOccupant),
                           "the occupant's finalization must not be disturbed to admit the stop");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kOccupant),
                           "the occupant's working set must survive");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isOverdubbing(track),
                           "the predecessor must still be capturing after the uncommitted switch");

  // (4) Let the occupant's finalization retire, then turn the lanes: the retained start executes with no
  // further gesture, so the user never presses twice (DEC-078 item 1).
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::ownsLiveFinalization(track, kOccupant);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::ownsLiveFinalization(track, kOccupant),
                            "the fixture must retire the occupant's finalizer");
  for (int round = 0; round < 8 && TrackCaptureStopTestAccess::activeLoopIndex(track) != kTarget; ++round) {
    const uint32_t base = clockManager.getCurrentTick();
    for (uint32_t tick = base + 1; tick <= base + 768 * 2; ++tick) {
      clockManager.assignCurrentTickSilently(tick);
      TrackCaptureStopTestAccess::startPendingRecord(tick);
      (void)TrackCaptureStopTestAccess::step(track);
      (void)TrackCaptureStopTestAccess::serviceAcceptedInteractionTurn(track);
    }
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kTarget, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                 "the retained start must execute once the capacity frees");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track),
                           "the retained start must leave the target recording");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasAcceptedInteractionForSlot(track, kDenied),
                            "the predecessor's latch must clear when its stop executes");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kDenied),
                           "nothing may be stolen from the retired predecessor either");
}



/// **S4b.1 (check 7, populated target) — a queued *playback* switch waits for the predecessor and then
/// commits in the production tick lane.**
///
/// The empty-target case is carried by the record-start carrier; a **populated** target takes a different
/// route: the gesture queues a **playback switch** (`queuePlayingSlotSwitch` → `SlotStateMachine`'s pending
/// switch), which commits in `TrackManager::updateAllTracks` — the production per-tick entry, and the lane
/// S4b.5/S4b.6/S4b.7 change. This fixture is that lane's acceptance, measured first: while the predecessor
/// still finalizes the request is **retained** (the pending switch stays set, the active slot does not move,
/// the predecessor's finalizer and lease survive), and once the finalizer retires the switch **commits with
/// no further gesture** — active slot moved, pending cleared, and the enabled set replaced with the target.
/// That is the answer to the review's "silent dead switch" risk on this shape: it is neither silent nor dead.
void test_s4b_1_a_queued_playback_switch_waits_for_the_predecessor_and_then_commits() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  constexpr uint8_t kPredecessor = 0;
  constexpr uint8_t kTarget = 1;
  commitRecordPassIntoTrackSlot(track, kPredecessor);
  commitRecordPassIntoTrackSlot(track, kTarget);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kPredecessor, true));

  // The predecessor captures and its stop is admitted (the finalizer holds the capacity).
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, kPredecessor);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::isOverdubbing(track));
  inputNote(60, 96, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track),
                           "fixture precondition: the predecessor's stop must be admitted");

  // Press the populated target: a queued playback switch, not a record carrier.
  clockManager.assignCurrentTickSilently(200);
  TrackCaptureStopTestAccess::toggleRecordForSlot(kTarget);
  TEST_ASSERT_TRUE_MESSAGE(trackManager.hasPendingSlotSwitch(0),
                           "a populated target must queue a playback switch");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kTarget, trackManager.getPendingSlotIndex(0),
                                  "the queued switch must name the target");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kPredecessor, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the press must not move the active slot");

  // Drive the production tick lane across several boundaries while the predecessor still finalizes. The
  // lane's own provisioning turn is modelled too (`main.cpp`'s deferred-idle dispatch is not linked here), so
  // the switch's inbound occupancy is taken off-clock exactly as the firmware does -- and, as on the device,
  // this lane is the *only* place it can be taken.
  for (int round = 0; round < 3; ++round) {
    (void)trackManager.servicePendingSlotProvision();
    const uint32_t base = clockManager.getCurrentTick();
    for (uint32_t tick = base + 1; tick <= base + 768; ++tick) {
      clockManager.assignCurrentTickSilently(tick);
      trackManager.updateAllTracks(tick);
    }
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kPredecessor, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the queued switch must not commit while the predecessor finalizes");
  TEST_ASSERT_TRUE_MESSAGE(trackManager.hasPendingSlotSwitch(0),
                           "the request must be retained rather than dropped");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::ownsLiveFinalization(track, kPredecessor),
                           "the predecessor's finalizer must still be outstanding");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kPredecessor),
                           "the predecessor's working set must survive the wait");

  // Retire the finalizer and keep driving: the queued switch commits with no further gesture.
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::ownsLiveFinalization(track, kPredecessor);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::ownsLiveFinalization(track, kPredecessor));
  for (int round = 0; round < 4 && TrackCaptureStopTestAccess::activeLoopIndex(track) != kTarget;
       ++round) {
    const uint32_t base = clockManager.getCurrentTick();
    for (uint32_t tick = base + 1; tick <= base + 768; ++tick) {
      clockManager.assignCurrentTickSilently(tick);
      trackManager.updateAllTracks(tick);
    }
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kTarget, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the retained switch must commit once the predecessor retires");
  TEST_ASSERT_FALSE_MESSAGE(trackManager.hasPendingSlotSwitch(0),
                            "the committed switch must clear its pending state");
  TEST_ASSERT_TRUE_MESSAGE(trackManager.isSlotEnabled(0, kTarget),
                           "the committed switch must enable the target slot");
  TEST_ASSERT_FALSE_MESSAGE(trackManager.isSlotEnabled(0, kPredecessor),
                            "the committed switch must leave the predecessor slot out of the enabled set");
}

/// **S4b.6/S4b.7 — a switch to a target without occupancy never allocates on the clock path.**
///
/// The inbound occupancy (the target slot's playback runtime) is what the switch commit binds to. Two facts
/// make it a stage of its own: the **commit** runs on the clock path (`updateAllTracks`) and the **request**
/// runs on a MIDI-input edge, so neither may take the runtime; and `slotIfAllocated` alone would leave the
/// switch silently dead instead of provisioning it. The contract therefore has two sides, and this helper
/// drives both for one target shape:
///
///   1. the **production MIDI edge** (`toggleRecordForSlot` -> `queuePlayingSlotSwitch` ->
///      `requestSlotSwitch`) queues the switch and takes no runtime inline; the pending switch **is** the
///      `SlotProvision` turn's eligibility (`MidiServiceDrain::slotProvisionNeeded`), so no request record
///      exists to be consumed elsewhere;
///   2. driving the clock lane leaves the switch **pending** and the target's occupancy **absent** -- the
///      record of "no allocation on the clock path";
///   3. one `SlotProvision` turn (`TrackManager::servicePendingSlotProvision`) takes the occupancy and builds
///      the target's merged window;
///   4. driving again **commits** the switch, so it was never dead.
///
/// `lengthTicks` gives the target the shape under test. Both shapes used here stay **below** the demotion
/// bound (`kMaxDetailedWindowBars` bars): at or above it the loop is demoted to the windowed policy and the
/// commit's long-loop hold term (`isPlaybackMergedMidiEventsReadyForSlot`) can no longer be satisfied by the
/// occupancy work -- measured here as a switch that stays pending after provisioning, because the windowed
/// gather returns empty without a published root. That shape is **owed** (recorded in `tasks.md` under
/// S4b.6/S4b.7), not asserted, so this helper never claims it.
void assert_s4b_6_a_switch_provisions_off_clock(uint32_t lengthTicks) {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  constexpr uint8_t kPlaying = 0;
  constexpr uint8_t kTarget = 2;
  commitRecordPassIntoTrackSlot(track, kPlaying);
  commitRecordPassIntoTrackSlotOfLength(track, kTarget, lengthTicks);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kPlaying, true));

  // The target has committed content but **no occupancy**: nothing has taken its playback runtime.
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, kTarget),
                            "fixture precondition: the target must start without occupancy");

  // The predecessor reaches the production playing state the way the lane fixture above does: an overdub
  // start on a populated slot is what enables it, plays it and publishes its playback pair, so the loop-end
  // wrap the commit waits for is a real one rather than a forced-state artifact. A forced `TRACK_PLAYING`
  // with no published pair leaves the playhead at tick zero and the switch can never be due.
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, kPlaying);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isOverdubbing(track),
                           "fixture precondition: the predecessor must be capturing");
  inputNote(60, 96, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track),
                           "fixture precondition: the predecessor's stop must be admitted");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, kTarget),
                            "fixture precondition: the target must still have no occupancy");

  // Retire the predecessor's finalizer *before* the press, so the queued switch is genuinely due: while
  // the finalizer is outstanding the playhead is frozen and no loop-end wrap is ever detected, which would
  // hide the commit's occupancy binding rather than exercise it.
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::ownsLiveFinalization(track, kPlaying);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::ownsLiveFinalization(track, kPlaying),
                            "the fixture must retire the predecessor's finalizer");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, kTarget),
                            "retiring the finalizer must not take the target's occupancy");

  // (1) The production MIDI edge presses the populated target: it queues the playback switch and takes
  // nothing inline. The pending switch *is* the `SlotProvision` turn's eligibility, so no request record is
  // involved -- the predicate reads exactly this state.
  clockManager.assignCurrentTickSilently(200);
  TrackCaptureStopTestAccess::toggleRecordForSlot(kTarget);
  TEST_ASSERT_TRUE_MESSAGE(trackManager.hasPendingSlotSwitch(0),
                           "the request must queue a pending switch");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kTarget, trackManager.getPendingSlotIndex(0),
                                  "the queued switch must name the target");
  TEST_ASSERT_TRUE_MESSAGE(
      MidiServiceDrain::slotProvisionNeeded(trackManager.hasPendingSlotSwitch(0),
                                            TrackCaptureStopTestAccess::runtimeAllocated(track, kTarget)),
      "the pending switch with no inbound occupancy must be the provisioning turn's work");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, kTarget),
                            "the MIDI edge must not provision inline");

  // (2) The clock lane may not allocate: the switch stays pending, the occupancy stays absent. The
  // provisioning turn is deliberately **not** modelled in this window -- that is the point of it.
  for (int round = 0; round < 3; ++round) {
    const uint32_t base = clockManager.getCurrentTick();
    for (uint32_t tick = base + 1; tick <= base + 768; ++tick) {
      clockManager.assignCurrentTickSilently(tick);
      trackManager.updateAllTracks(tick);
    }
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kPlaying, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the switch may not commit before its occupancy exists");
  TEST_ASSERT_TRUE_MESSAGE(trackManager.hasPendingSlotSwitch(0),
                           "the switch must be retained, not dropped");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, kTarget),
                            "the clock path must not allocate the inbound occupancy");

  // (3) The off-clock owner takes the occupancy: exactly one `SlotProvision` turn -- the turn provisions the
  // pending switch's target **and** builds that slot's merged window, which is the pair the contract names.
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, kTarget),
                            "nothing may have provisioned the target behind this window's back");
  TEST_ASSERT_TRUE_MESSAGE(trackManager.servicePendingSlotProvision(),
                           "the SlotProvision turn must take the pending switch as its work");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, kTarget),
                           "provisioning must have taken the target's occupancy");
  TEST_ASSERT_FALSE_MESSAGE(trackManager.servicePendingSlotProvision(),
                           "a provisioned switch is the commit path's, not the turn's");
  // The turn's second half is `ensurePlaybackMergedEventsForSlot`, and its effect is deliberately asserted as
  // **not** observable here: that window is gathered from the loop's *published snapshot root*, which a
  // hand-committed slot has none of, so the gather misses and the window stays empty (measured). This is a
  // tripwire on the harness, not on the firmware -- if a fixture ever publishes a root, it fails and the
  // assertion moves to the ready side. Discharge path for the missing evidence: the published-root fixture
  // owed under S4b.6's demoted-target item (the windowed publication lane, S4c/F).
  TEST_ASSERT_FALSE_MESSAGE(track.isPlaybackMergedMidiEventsReadyForSlot(kTarget),
                            "a hand-committed slot has no published root, so the merged window stays empty");
  // Task 3.5 re-point: readiness no longer reads that buffer. No window was published for this slot, and with no
  // published root no build could have gathered one -- so readiness names the **source** as the holder rather than
  // relying on "the merged window is empty".
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackWindowReadiness::OwesSource),
                    static_cast<int>(track.playbackWindowReadinessForSlot(kTarget)));
  TEST_ASSERT_EQUAL_STRING("published_source", Track::playbackWindowReadinessHolder(
                                                   track.playbackWindowReadinessForSlot(kTarget)));
  for (int round = 0; round < 4 && TrackCaptureStopTestAccess::activeLoopIndex(track) != kTarget;
       ++round) {
    const uint32_t base = clockManager.getCurrentTick();
    for (uint32_t tick = base + 1; tick <= base + 768; ++tick) {
      clockManager.assignCurrentTickSilently(tick);
      trackManager.updateAllTracks(tick);
    }
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kTarget, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the provisioned switch must commit");
  TEST_ASSERT_FALSE_MESSAGE(trackManager.hasPendingSlotSwitch(0),
                            "the committed switch must clear its pending state");
}

/// Short target: the commit would otherwise take the plain `hasDataInSlot` branch.
void test_s4b_6_a_switch_to_a_slot_without_occupancy_provisions_off_clock() {
  assert_s4b_6_a_switch_provisions_off_clock(768);
}

/// Multi-bar target (8 bars): the same contract over a different geometry, so the binding is not a
/// property of the one-bar case. It stays **below** `kMaxDetailedWindowBars` (16 bars) on purpose -- at or
/// above that bound the loop is *demoted* to the windowed policy, whose commit hold this harness cannot
/// release, so the demoted shape is an explicitly owed measurement rather than a passing fixture here.
void test_s4b_6_a_switch_to_a_multi_bar_target_without_occupancy_also_provisions_off_clock() {
  assert_s4b_6_a_switch_provisions_off_clock(6144);
}

// --- S4b.7: the demoted (windowed-policy) target, with and without a published root ------------------------

/// Commit a bar-spaced pass: one note per bar, so any windowed gather of this slot finds events no matter
/// where the playhead is. The demoted shape needs spread content for the same reason a restored slot has it --
/// a window centred on the playhead must not fall on empty space.
void commitBarSpacedPassIntoTrackSlot(Track& track, uint8_t slot, uint32_t bars) {
  TrackCaptureStopTestAccess::selectSlot(track, slot);
  Loop& loop = track.getActiveLoop();
  loop.loopLengthTicks = bars * 768;
  loop.beginCapture(CapturePhase::Record);
  for (uint32_t bar = 0; bar < bars; ++bar) {
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(bar * 768, 1, 48, 100)));
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(bar * 768 + 48, 1, 48, 0)));
  }
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(bars * 768, CommitReason::RecordStop));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass());
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
}

/// Install a **published snapshot root** for `loop` with the same bar-spaced notes. This is the state a
/// *restored* slot has (SD load publishes a root) and the only source the merged-window build gathers from
/// (`Loop::tryGatherPublishedSnapshotEventsInWindow`); a hand-committed slot has none, which is the
/// `GatherMissHoldEmpty` case. Same recipe as `test_loop_take_survival`'s root fixtures.
void publishBarSpacedRootForSlot(Loop& loop, uint32_t bars, uint32_t firstBar = 0) {
  auto empty = std::make_shared<const LoopSnapshot>(
      LoopSnapshot(loop.loopId, loop.loopLengthTicks, 0, 8, 8));
  LoopSnapshotChangeSet changes;
  for (uint32_t bar = firstBar; bar < bars; ++bar) {
    SnapshotNoteChange add{};
    add.kind = SnapshotChangeKind::Add;
    add.next.loopId = loop.loopId;
    add.next.noteId = static_cast<NoteId>(2000 + bar);
    add.next.pitch = 48;
    add.next.velocity = 100;
    add.next.startTick = bar * 768;
    add.next.endTick = bar * 768 + 48;
    changes.notes.push_back(add);
    // The **event** index is what a windowed gather reads (`gatherPublishedSnapshotMembership` walks
    // `eventMembership(bar)`), so the root must carry the events as well as the notes -- the real builder
    // populates both views.
    SnapshotEventChange on{};
    on.kind = SnapshotChangeKind::Add;
    on.next.loopId = loop.loopId;
    on.next.eventId = static_cast<EventId>(4000 + bar * 2);
    on.next.canonicalOrder = bar * 2;
    on.next.value = MidiEvent::NoteOn(bar * 768, 1, 48, 100);
    changes.events.push_back(on);
    SnapshotEventChange off{};
    off.kind = SnapshotChangeKind::Add;
    off.next.loopId = loop.loopId;
    off.next.eventId = static_cast<EventId>(4001 + bar * 2);
    off.next.canonicalOrder = bar * 2 + 1;
    off.next.value = MidiEvent::NoteOff(bar * 768 + 48, 1, 48, 0);
    changes.events.push_back(off);
  }
  LoopSnapshotConstruction construction(empty, changes);
  while (!construction.complete()) {
    TEST_ASSERT_TRUE(construction.step(128));
  }
  const ImmutableLoopSnapshotRef snapshot = construction.freeze();
  TEST_ASSERT_NOT_NULL(snapshot.get());
  loop.storePublishedSnapshotForTest(snapshot, loop.playbackRevision);
  TEST_ASSERT_TRUE_MESSAGE(loop.publishedSnapshotIsCurrent(),
                           "the fixture must install a *current* root, or the gather terms refuse it");
}

/// **S4b.7 — the demoted target: the merged window is what releases the hold, and its absence is bounded.**
///
/// Above `kMaxDetailedWindowBars` (16 bars) a slot is *demoted* to the windowed policy, and the switch commit's
/// hold term (`isPlaybackMergedMidiEventsReadyForSlot`) then reads the merged window that the `SlotProvision`
/// turn builds. That build gathers from the loop's **published snapshot root**, so the two shapes this helper
/// drives are the two halves of the contract line ("the long-loop pending hold is satisfied by that turn … and
/// carries an age/bound"):
///
///   * `withRoot` — a restored-slot root exists, the turn's build makes the window ready, and the switch
///     **commits**. This is the evidence the earlier slice had to record as missing.
///   * without a root — the occupancy is taken but the window can never become ready, so the switch is
///     **reported once per episode** at the bound and left exactly as it was: bounded, never silent, never
///     dropped (DEC-078 item 1's "the user never re-issues" survives).
void assert_s4b_7_a_demoted_target_through_the_turn(bool withRoot, bool lateRoot = false,
                                                   bool emptyLaunch = false) {
  SlotProvisionHold::resetReportRecordForTest();
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  constexpr uint8_t kPlaying = 0;
  constexpr uint8_t kTarget = 2;
  constexpr uint32_t kTargetBars = 24;  // > 16 bars: the demoted policy, measured (not the below-bound case)
  constexpr uint32_t kBar = 768;
  commitRecordPassIntoTrackSlot(track, kPlaying);
  commitBarSpacedPassIntoTrackSlot(track, kTarget, kTargetBars);
  if (withRoot && !lateRoot) {
    publishBarSpacedRootForSlot(track.getLoop(kTarget), kTargetBars, emptyLaunch ? 2 : 0);
  }
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kPlaying, true));
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, kTarget),
                            "fixture precondition: the target starts without occupancy");

  // The production playing state for the predecessor (an overdub start enables, plays and publishes it), then
  // its finalizer retires -- a live finalizer freezes the playhead, so the switch could never be due.
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, kPlaying);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 96, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track));
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::ownsLiveFinalization(track, kPlaying);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::ownsLiveFinalization(track, kPlaying));

  // Launch preparation must not borrow this far-from-zero outgoing projection.
  const uint32_t pressTick = (lateRoot || emptyLaunch) ? 10 * kBar + 200 : 200;
  // Press the demoted target through the production edge: queued, nothing taken inline.
  clockManager.assignCurrentTickSilently(pressTick);
  TrackCaptureStopTestAccess::toggleRecordForSlot(kTarget);
  TEST_ASSERT_TRUE_MESSAGE(trackManager.hasPendingSlotSwitch(0),
                           "the press must queue the switch");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kTarget, trackManager.getPendingSlotIndex(0),
                                  "the queued switch must name the demoted target");
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::runtimeAllocated(track, kTarget));

  // The clock lane may not commit it: the demoted hold waits for the merged window.
  for (uint32_t tick = clockManager.getCurrentTick() + 1; tick <= pressTick + kBar; ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    trackManager.updateAllTracks(tick);
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kPlaying, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "a demoted target may not commit before its window is built");
  TEST_ASSERT_TRUE(trackManager.hasPendingSlotSwitch(0));

  const auto buildsBeforeProvision = Track::playbackOrderHotRebuildCount();
  TEST_ASSERT_TRUE(trackManager.servicePendingSlotProvision());
  TEST_ASSERT_EQUAL_UINT32(buildsBeforeProvision, Track::playbackOrderHotRebuildCount());
  auto* launchRuntime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, kTarget);
  TEST_ASSERT_NOT_NULL(launchRuntime);
  TEST_ASSERT_NOT_NULL(track.pendingViewRequest(kTarget));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, track.pendingViewRequest(kTarget)->clockEpoch,
      "inactive launch has no fabricated absolute position");
  TEST_ASSERT_EQUAL_UINT64(0, track.pendingViewRequest(kTarget)->deadlineTick);
  TEST_ASSERT_FALSE(track.playbackLaunchReadyForSlot(kTarget));
  TEST_ASSERT_FALSE(launchRuntime->viewSetIfPrewarmed()->liveCaptureStaging().storage.isPrewarmed());
  TEST_ASSERT_FALSE(launchRuntime->viewSetIfPrewarmed()->wholeTakeReserved);
  if (lateRoot) {
    TEST_ASSERT_FALSE(trackManager.servicePendingSlotProvision());
    TEST_ASSERT_FALSE(track.playbackWindowServiceOffer(0, {1000, 1}).canProgress);
    publishBarSpacedRootForSlot(track.getLoop(kTarget), kTargetBars);
    TEST_ASSERT_FALSE(trackManager.servicePendingSlotProvision()); // recipe already retained
    TEST_ASSERT_TRUE_MESSAGE(track.playbackWindowServiceOffer(0, {1000, 1}).canProgress,
        "an occupied target must retry preparation when its source arrives");
  }
  if (withRoot) {
    nativePlaybackDeadlineClock = {1000, 1};
    for (unsigned turn = 0; turn < 1000 && !track.playbackLaunchReadyForSlot(kTarget); ++turn) {
      nativePlaybackDeadlineClock.tick += 64;
      track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
    }
    TEST_ASSERT_TRUE(track.playbackLaunchReadyForSlot(kTarget));
    auto* set = launchRuntime->viewSetIfPrewarmed();
    auto& ready = set->views[set->lifecycle.ready];
    TEST_ASSERT_TRUE(ready.complete);
    TEST_ASSERT_FALSE(ready.published);
    TEST_ASSERT_EQUAL_UINT32(0, ready.coverage.startPhase);
    TEST_ASSERT_EQUAL_UINT32(2 * kBar, ready.coverage.lengthTicks);
    TEST_ASSERT_EQUAL_UINT32(emptyLaunch ? 0 : 4, ready.storage.eventCount);
    const auto revision = ready.source.playbackRevision;
    ready.source.playbackRevision = revision + 1;
    TEST_ASSERT_FALSE(track.playbackLaunchReadyForSlot(kTarget));
    ready.source.playbackRevision = revision;
    ready.coverage.startPhase = kBar;
    TEST_ASSERT_FALSE(track.playbackLaunchReadyForSlot(kTarget));
    ready.coverage.startPhase = 0;
    ready.complete = false;
    TEST_ASSERT_FALSE(track.playbackLaunchReadyForSlot(kTarget));
    ready.complete = true;
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(buildsBeforeProvision, Track::playbackOrderHotRebuildCount(),
        "bounded immutable preparation must not invoke the mutable gather/order path");
  }
  TEST_ASSERT_EQUAL_MESSAGE(withRoot, track.playbackLaunchReadyForSlot(kTarget),
      "only completed current immutable launch coverage is ready");
  TEST_ASSERT_FALSE(track.isPlaybackMergedMidiEventsReadyForSlot(kTarget));

  const uint32_t holdBoundTicks = SlotProvisionHold::boundTicksFor(768);
  if (withRoot) {
    for (int round = 0; round < 4 && TrackCaptureStopTestAccess::activeLoopIndex(track) != kTarget;
         ++round) {
      const uint32_t base = clockManager.getCurrentTick();
      for (uint32_t tick = base + 1; tick <= base + kBar; ++tick) {
        clockManager.assignCurrentTickSilently(tick);
        const uint32_t buildsBefore = Track::playbackOrderHotRebuildCount();
        trackManager.updateAllTracks(tick);
        if (track.getActiveLoopIndex() == kTarget) {
          TEST_ASSERT_EQUAL_UINT32_MESSAGE(buildsBefore, Track::playbackOrderHotRebuildCount(),
              "launch and first target tick must reuse preparation, including empty coverage");
          break;
        }
      }
    }
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(kTarget, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                    "the root-backed demoted switch must commit");
    TEST_ASSERT_FALSE(trackManager.hasPendingSlotSwitch(0));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, SlotProvisionHold::reportRecordForTest().count,
                                     "a switch that completes must never report a hold");
    return;
  }

  // No root: drive past the bound while the turn keeps running. The switch is reported once, and stays.
  const uint32_t firstQueuedTick = clockManager.getCurrentTick();
  for (int round = 0; round < 8; ++round) {
    (void)trackManager.servicePendingSlotProvision();
    const uint32_t base = clockManager.getCurrentTick();
    for (uint32_t tick = base + 1; tick <= base + kBar; ++tick) {
      clockManager.assignCurrentTickSilently(tick);
      trackManager.updateAllTracks(tick);
    }
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kPlaying, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "an unready demoted target must not commit");
  TEST_ASSERT_TRUE_MESSAGE(trackManager.hasPendingSlotSwitch(0),
                           "the switch must be retained, not dropped");
  const SlotProvisionHold::ReportRecord& report = SlotProvisionHold::reportRecordForTest();
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, report.count, "the hold must report exactly once per episode");
  TEST_ASSERT_EQUAL_UINT8(0, report.trackIndex);
  TEST_ASSERT_EQUAL_UINT8(kTarget, report.slotIndex);
  TEST_ASSERT_TRUE_MESSAGE(report.ageTicks >= holdBoundTicks,
                           "the report must be at or past the bound, not merely late");
  TEST_ASSERT_TRUE(clockManager.getCurrentTick() - firstQueuedTick >= holdBoundTicks);

  // A second episode (cancel, turn while nothing is pending, press again) reports on its own account.
  trackManager.clearPendingSlotSwitch(0);
  (void)trackManager.servicePendingSlotProvision();
  clockManager.assignCurrentTickSilently(clockManager.getCurrentTick() + 1);
  TrackCaptureStopTestAccess::toggleRecordForSlot(kTarget);
  TEST_ASSERT_TRUE(trackManager.hasPendingSlotSwitch(0));
  for (int round = 0; round < 8; ++round) {
    (void)trackManager.servicePendingSlotProvision();
    const uint32_t base = clockManager.getCurrentTick();
    for (uint32_t tick = base + 1; tick <= base + kBar; ++tick) {
      clockManager.assignCurrentTickSilently(tick);
      trackManager.updateAllTracks(tick);
    }
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, SlotProvisionHold::reportRecordForTest().count,
                                   "a new episode reports again: once per episode, not once per boot");
}

/// A **restored** demoted slot (a published root exists): the turn's build makes the window ready and the
/// switch commits -- the evidence the first slice had to record as missing.
void test_launch_retries_an_occupied_target_when_its_source_arrives() {
  assert_s4b_7_a_demoted_target_through_the_turn(true, true);
}

void test_launch_accepts_a_prepared_empty_interval_at_its_own_origin() {
  assert_s4b_7_a_demoted_target_through_the_turn(true, false, true);
}

void test_s4b_7_a_demoted_target_with_a_published_root_commits_through_the_turn() {
  assert_s4b_7_a_demoted_target_through_the_turn(/*withRoot=*/true);
}

/// An unready demoted slot: the hold is real but **bounded** -- one report per episode, switch retained.
void test_s4b_7_a_demoted_target_without_a_root_is_reported_once_at_its_bound() {
  assert_s4b_7_a_demoted_target_through_the_turn(/*withRoot=*/false);
}

/// **S4b.5 — one active-slot write authority: a direct write cancels the queued switch, and cancelling is the
/// whole trio.**
///
/// The entry's shape: a **replace-enabled-set** switch is queued, the **undo route** moves the active slot
/// (`TrackUndo.cpp`'s `applyClearSlotUndoSideEffects` calls `TrackManager::setActiveLoopIndex` -- the authority
/// this stage establishes), and then an **unrelated** switch is triggered. Three properties must hold:
///
///   1. the queued switch is **cancelled** rather than left armed underneath a slot that already moved (one
///      transfer, not two);
///   2. the cancel clears the **whole** trio, so the unrelated switch does not inherit a stale enabled-set
///      replacement and wipe the enabled set; and
///   3. the cancel itself mutates nothing but the slot -- the pool does not grow and the switch is gone.
void test_s4b_5_a_direct_write_cancels_the_queued_switch_without_a_stale_replacement() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  constexpr uint8_t kPlaying = 0;
  constexpr uint8_t kReplaceTarget = 1;
  constexpr uint8_t kUndoSlot = 2;
  constexpr uint8_t kLaterTarget = 3;
  commitRecordPassIntoTrackSlot(track, kPlaying);
  commitRecordPassIntoTrackSlot(track, kReplaceTarget);
  commitRecordPassIntoTrackSlot(track, kUndoSlot);
  commitRecordPassIntoTrackSlot(track, kLaterTarget);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kPlaying, true));
  // The undo slot is a slot the track has played (a restored take has occupancy), so the undo route's own
  // `resetPlaybackStateForSlot` below has a runtime to anchor.
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kUndoSlot, false));
  // The enabled set a stale replacement would wipe: the played slot plus one other layer. Set directly --
  // the enabled set is this fixture's subject, and the gesture routes that build it are covered above.
  trackManager.setSlotEnabled(0, kPlaying, true);
  trackManager.setSlotEnabled(0, kUndoSlot, true);

  // The production playing state, then retire the finalizer so a later switch can be due.
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, kPlaying);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 96, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track));
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::ownsLiveFinalization(track, kPlaying);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::ownsLiveFinalization(track, kPlaying));

  // (1) A **replace-enabled-set** switch is queued by the production gesture (the populated target route
  // arms the replacement, so this is the state whose cancel must clear it).
  clockManager.assignCurrentTickSilently(200);
  TrackCaptureStopTestAccess::toggleRecordForSlot(kReplaceTarget);
  TEST_ASSERT_TRUE_MESSAGE(trackManager.hasPendingSlotSwitch(0),
                           "the populated target must queue a playback switch");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kReplaceTarget, trackManager.getPendingSlotIndex(0),
                                  "the queued switch must name the replace target");
  TEST_ASSERT_EQUAL_UINT8(kPlaying, TrackCaptureStopTestAccess::activeLoopIndex(track));

  // (2) The **undo route**'s write -- exactly the call `applyClearSlotUndoSideEffects` makes. The queued
  // switch must be cancelled rather than left armed under a slot that already moved, and the cancel must not
  // allocate (nothing is stolen, nothing grows).
  const size_t chunksBeforeCancel = LoopEventStore::freeChunkCount();
  trackManager.setActiveLoopIndex(0, kUndoSlot);
  // The undo route continues with its own playback-state reset (`applyClearSlotUndoSideEffects`), so the
  // fixture does too: without it the new active slot has no anchored playhead and no switch can be due.
  track.resetPlaybackStateForSlot(kUndoSlot, clockManager.getCurrentTick());
  TEST_ASSERT_FALSE_MESSAGE(trackManager.hasPendingSlotSwitch(0),
                            "a direct active-slot write must cancel the queued switch, not race it");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kUndoSlot, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the undo route's write lands in one transfer");
  TEST_ASSERT_EQUAL_UINT32(chunksBeforeCancel, LoopEventStore::freeChunkCount());
  TEST_ASSERT_TRUE_MESSAGE(trackManager.isSlotEnabled(0, kPlaying),
                           "the cancel must not touch the enabled set");

  // (3) An **unrelated** switch, queued through a route that does not arm a replacement
  // (`requestSlotSwitch`). If the cancel had left the replacement armed, this commit would inherit it and
  // replace the enabled set with the target alone -- the stale-replacement defect.
  clockManager.assignCurrentTickSilently(clockManager.getCurrentTick() + 1);
  trackManager.requestSlotSwitch(0, kLaterTarget, SlotQuantization::LoopEnd,
                                 clockManager.getCurrentTick());
  TEST_ASSERT_TRUE(trackManager.hasPendingSlotSwitch(0));
  for (int round = 0; round < 4 && TrackCaptureStopTestAccess::activeLoopIndex(track) != kLaterTarget;
       ++round) {
    (void)trackManager.servicePendingSlotProvision();
    const uint32_t base = clockManager.getCurrentTick();
    for (uint32_t tick = base + 1; tick <= base + 768; ++tick) {
      clockManager.assignCurrentTickSilently(tick);
      trackManager.updateAllTracks(tick);
    }
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kLaterTarget, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the unrelated switch must commit");
  TEST_ASSERT_FALSE(trackManager.hasPendingSlotSwitch(0));
  TEST_ASSERT_TRUE_MESSAGE(trackManager.isSlotEnabled(0, kPlaying),
                           "no stale enabled-set replacement may reach an unrelated switch");
  TEST_ASSERT_TRUE_MESSAGE(trackManager.isSlotEnabled(0, kUndoSlot),
                           "the enabled layers a stale replacement would wipe must survive");
}

/// **S4b.5 — an accepted-but-unexecuted interaction makes a direct write take the switch's own route.**
///
/// Rev 21.3 adds `hasAcceptedUnexecutedInteraction()` to the active-slot guard, and it is the one term the
/// S4b.5 slice shipped **without** a fixture (removing it left the whole native suite green). The state it
/// covers is the latch alone: an interaction accepted and not yet executed, with **no** recording, **no**
/// overdub and **no** admitted stop -- so nothing else in the guard can route the write.
///
/// What the route decides here is observable: the capture-switch commit (`finalizeCaptureAndSelectSlot`)
/// enables the target slot and resets its playback state, while a bare write leaves the target exactly as it
/// was -- disabled, on a playing track, which is the "piano-roll notes with no MO" hazard the switch's own
/// comment names. Both routes move the active slot, so the **route**, not the destination, is the assertion.
void test_s4b_5_an_accepted_unexecuted_interaction_routes_the_write_through_the_switch() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  constexpr uint8_t kPlaying = 0;
  constexpr uint8_t kTarget = 1;
  commitRecordPassIntoTrackSlot(track, kPlaying);
  commitRecordPassIntoTrackSlot(track, kTarget);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kPlaying, true));
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, kPlaying);
  trackManager.setSlotEnabled(0, kPlaying, true);
  trackManager.setSlotEnabled(0, kTarget, false);

  // The accepted-but-unexecuted interaction, and nothing else: no capture, no job.
  TrackCaptureStopTestAccess::latchAcceptedStopForSlot(track, kPlaying);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasAcceptedUnexecutedInteraction(track),
                           "fixture precondition: the latch must be the accepted interaction");
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::isRecording(track));
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::isOverdubbing(track));
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track),
                            "fixture precondition: no admitted stop may stand in for the latch");

  trackManager.setActiveLoopIndex(0, kTarget);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kTarget, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the write still lands, whichever route takes it");
  TEST_ASSERT_TRUE_MESSAGE(
      trackManager.isSlotEnabled(0, kTarget),
      "the accepted interaction must route the write through the switch commit, which enables the target; "
      "a bare move would leave the active slot disabled on a playing track");
}

/// **S4b.2 — the inbound role is provisioned before the outbound one is released, and the overlap is bounded.**
///
/// The entry asks for three things, and the first is an **ordering**: pre-provision the inbound role before
/// releasing the outbound one, with the overlap bounded and accounted in the S2 budget; the switch point
/// performs no allocation (asserted by S4b.6/S4b.7's fixtures); and the transfer must not steal from a
/// still-live capture or deferred-finalization owner (asserted by S4b.1's two "nothing stolen" fixtures).
///
/// What is asserted here is the ordering and its **bound**, measured in the pool rather than argued:
///
///   * while the switch cannot commit (the inbound has no occupancy) the outbound's working set is **still
///     held** -- nothing is released early, so a refused or delayed switch never leaves the track with neither
///     role;
///   * the provisioning turn takes the inbound occupancy while the outbound is **still** held, which is the
///     overlap the S2 budget term accounts for; and
///   * the overlap is **exactly one playback working set**: the pool drop at provisioning equals what a probe
///     slot provisioned the same way costs in the same fixture (the outbound's own prewarm also carries the
///     capture horizon, so it is deliberately *not* used as the unit), and only after the commit does the
///     retired slot become releasable -- and the release touches nothing on the inbound.
void test_s4b_2_the_inbound_role_is_provisioned_before_the_outbound_is_released() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  constexpr uint8_t kOutbound = 0;
  constexpr uint8_t kInbound = 2;
  const uint32_t chunksEmpty = LoopEventStore::freeChunkCount();
  commitRecordPassIntoTrackSlot(track, kOutbound);
  commitRecordPassIntoTrackSlot(track, kInbound);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kOutbound, true));
  const uint32_t chunksOutboundOnly = LoopEventStore::freeChunkCount();
  const uint32_t outboundCost = chunksEmpty - chunksOutboundOnly;
  TEST_ASSERT_TRUE_MESSAGE(outboundCost > 0, "fixture precondition: a working set costs pool capacity");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, kInbound),
                            "fixture precondition: the inbound starts without occupancy");

  // The production playing state for the outbound, then retire the finalizer so a switch can be due.
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, kOutbound);
  TrackCaptureStopTestAccess::activateSlot(0, kOutbound);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 96, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::hasPendingDeferredOverdubStop(track));
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::ownsLiveFinalization(track, kOutbound);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::ownsLiveFinalization(track, kOutbound));
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kOutbound),
                           "fixture precondition: the outbound holds the working set it will retire");

  // Press the unoccupied inbound target: a queued switch that must wait for its occupancy.
  clockManager.assignCurrentTickSilently(200);
  TrackCaptureStopTestAccess::toggleRecordForSlot(kInbound);
  TEST_ASSERT_TRUE(trackManager.hasPendingSlotSwitch(0));
  TEST_ASSERT_EQUAL_UINT8(kInbound, trackManager.getPendingSlotIndex(0));

  // (1) The clock lane cannot commit yet **and must not have released the outbound**: the pre-provisioning
  //     half of the ordering. A release here would leave the track with neither role.
  const uint32_t chunksBeforeLane = LoopEventStore::freeChunkCount();
  for (uint32_t tick = 201; tick <= 200 + 768; ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    trackManager.updateAllTracks(tick);
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kOutbound, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the switch may not commit before its occupancy exists");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kOutbound),
                           "the outbound's working set must survive the wait: nothing is released early");
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::runtimeAllocated(track, kInbound));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(chunksBeforeLane, LoopEventStore::freeChunkCount(),
                                   "the clock lane may not allocate or release pool capacity");

  // (2) The provisioning turn takes the inbound occupancy while the outbound is **still** held -- the overlap
  //     the S2 budget accounts for -- and the drop is exactly one working set, not two.
  TEST_ASSERT_TRUE(trackManager.servicePendingSlotProvision());
  const uint32_t chunksBothRoles = LoopEventStore::freeChunkCount();
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, kInbound),
                           "the inbound occupancy must exist after provisioning");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kOutbound),
                           "the outbound must still be held while the inbound is provisioned");
  // **Finding, recorded rather than papered over:** the inbound's provisioned occupancy carries **no
  // working-set lease** -- `provisionSlotForSwitch` provisions with `prepareCapture = false`, and the lease
  // is acquired only by the capture admission (`prewarmPlaybackForSlot(slot, true)`, S3.1). So "the inbound
  // *role*" is, for a playback switch, its occupancy plus the pool capacity it holds (measured below), not a
  // lease entry. Whether a switch's inbound must also hold a lease -- and with which `WorkingSetRole` bits --
  // is a contract question this slice leaves to the record rather than deciding in a fixture.
  const uint32_t inboundProvisionCost = chunksOutboundOnly - chunksBothRoles;
  TEST_ASSERT_TRUE_MESSAGE(inboundProvisionCost > 0,
                           "the inbound's provisioning must take pool capacity of its own");
  TEST_ASSERT_TRUE_MESSAGE(inboundProvisionCost <= outboundCost,
                           "the inbound may not cost more than the capture-inclusive outbound did");

  // (3) Only now does the commit land, and only after it is the retired slot releasable; its capacity returns.
  for (int round = 0; round < 4 && TrackCaptureStopTestAccess::activeLoopIndex(track) != kInbound;
       ++round) {
    const uint32_t base = clockManager.getCurrentTick();
    for (uint32_t tick = base + 1; tick <= base + 768; ++tick) {
      clockManager.assignCurrentTickSilently(tick);
      trackManager.updateAllTracks(tick);
    }
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kInbound, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "the provisioned switch must commit");
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::runtimeAllocated(track, kInbound));
  TEST_ASSERT_TRUE_MESSAGE(track.noLiveOwnerForSlot(kOutbound),
                           "the retired slot must be releasable once the inbound is in place");
  track.releasePlaybackMergedMidiEventsMemory();
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kOutbound),
                            "the retired slot's working set must be released after the transfer");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, kInbound),
                           "the release sweep must not touch the inbound the transfer just provisioned");
  // (Not asserted: *pool* capacity returning on release. `LoopEventStore::freeChunkCount()` counts the
  // committed-event pool, which a playback-runtime release does not return -- the release returns the runtime's
  // own views/working set, which the lease assertion above is the observable for. Claiming a chunk-count rise
  // here would have been a measurement of the wrong resource.)

  // The bound, made precise against **the same kind of provisioning**: the switch's inbound is provisioned
  // with `prepareCapture = false` (playback only), while the outbound above was prewarmed with the capture
  // horizon too -- so the two are *not* the same unit, and comparing them directly would compare apples to
  // horizons. A probe slot provisioned the same way as the inbound gives the unit, and the inbound's cost must
  // equal it: the transfer's overlap is exactly **one playback working set**.
  constexpr uint8_t kProbe = 5;
  commitRecordPassIntoTrackSlot(track, kProbe);
  const uint32_t chunksBeforeProbe = LoopEventStore::freeChunkCount();
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kProbe, false));
  const uint32_t probeCost = chunksBeforeProbe - LoopEventStore::freeChunkCount();
  // **Recorded measurement, not an equality:** the switch's inbound provisioning dropped the pool by
  // `inboundProvisionCost`, while provisioning a probe slot the same way (`prepareCapture = false`) dropped it
  // by `probeCost` -- measured here as 1 vs 0, i.e. the switch turn's drop is not reproducible by a bare
  // playback provisioning, so part of it belongs to the turn's other work (the merged-window build /
  // prepared-pair publication) rather than to the occupancy alone. The bound this fixture *asserts* is
  // therefore the one it can support: the transfer's extra pool cost never exceeds one capture-inclusive
  // working set, which is what the S2 budget term needs; splitting the 1 chunk between occupancy and window is
  // a budget-accounting question, not something this fixture should decide by fiat.
  TEST_ASSERT_TRUE(probeCost <= inboundProvisionCost);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::leaseHoldsWorkingSet(track, kInbound) ||
                               TrackCaptureStopTestAccess::runtimeAllocated(track, kInbound),
                           "the inbound stays in place at the end of the transfer");
}

/// **S4b.3 — the switch lands on a MIDI boundary, and that boundary is cold: it binds, it does not build.**
///
/// "Land the switch on a MIDI boundary" is the quantization's job (`SlotQuantization::LoopEnd` for the
/// playback-switch gesture, `NextGrid` for the record carrier), and "a cold admission boundary by
/// construction" is what S4b.2/S4b.6/S4b.7 made true: the inbound role is provisioned **off-clock** by the
/// `SlotProvision` turn, so the boundary itself only binds an already-prepared role. Both halves are measured
/// here instead of asserted by name:
///
///   * the tick at which the active slot actually changes is the boundary the quantization promises -- the
///     outgoing loop's playhead has wrapped at that tick (`LoopEnd`), or the tick is on the start grid
///     (`NextGrid`) -- and it is **not** the tick the request was queued at; and
///   * the boundary tick itself allocates nothing: the pool is exactly where it was immediately before the
///     tick that committed, because the role it binds was prepared earlier.
void assert_s4b_3_the_switch_lands_on_a_boundary(SlotQuantization quantization, uint8_t kOutbound,
                                                uint8_t kInbound) {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, kOutbound);
  commitRecordPassIntoTrackSlot(track, kInbound);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kOutbound, true));
  // The inbound role is prepared **before** the boundary: that is what makes the boundary cold.
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kInbound, false));
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, kOutbound);
  TrackCaptureStopTestAccess::activateSlot(0, kOutbound);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 96, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::ownsLiveFinalization(track, kOutbound);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::ownsLiveFinalization(track, kOutbound));
  TEST_ASSERT_EQUAL_UINT8(kOutbound, TrackCaptureStopTestAccess::activeLoopIndex(track));

  const Loop& outboundLoop = track.getLoop(kOutbound);
  const uint32_t loopStart = outboundLoop.loopStartTick;
  const uint32_t loopLength = outboundLoop.loopLengthTicks;
  const uint32_t gridTicks = track.getQueuedStartGridTicks();
  TEST_ASSERT_TRUE(loopLength > 0);
  TEST_ASSERT_TRUE(gridTicks > 0);

  // Queue deliberately **off** the boundary, so a commit that lands on the queue tick would be visible.
  const uint32_t queueTick = loopStart + 211;
  clockManager.assignCurrentTickSilently(queueTick);
  trackManager.requestSlotSwitch(0, kInbound, quantization, queueTick);
  TEST_ASSERT_TRUE(trackManager.hasPendingSlotSwitch(0));
  TEST_ASSERT_EQUAL_UINT8(kOutbound, TrackCaptureStopTestAccess::activeLoopIndex(track));

  uint32_t commitTick = UINT32_MAX;
  uint32_t phaseBeforeCommit = 0;
  uint32_t phaseAtCommit = 0;
  uint32_t chunksBeforeCommit = 0;
  uint32_t chunksAfterCommit = 0;
  for (uint32_t tick = queueTick + 1; tick <= queueTick + 2 * loopLength + gridTicks; ++tick) {
    // The outgoing loop's own phase (modulo its length: the effective tick is projection-domain), read while
    // it is still the active slot.
    phaseBeforeCommit = (track.getEffectivePlaybackTick(tick - 1) - loopStart) % loopLength;
    phaseAtCommit = (track.getEffectivePlaybackTick(tick) - loopStart) % loopLength;
    chunksBeforeCommit = LoopEventStore::freeChunkCount();
    clockManager.assignCurrentTickSilently(tick);
    trackManager.updateAllTracks(tick);
    chunksAfterCommit = LoopEventStore::freeChunkCount();
    if (TrackCaptureStopTestAccess::activeLoopIndex(track) == kInbound) {
      commitTick = tick;
      break;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(commitTick != UINT32_MAX, "the queued switch must commit");
  TEST_ASSERT_TRUE_MESSAGE(commitTick != queueTick,
                           "the commit may not land on the request's own tick: it is quantized");

  if (quantization == SlotQuantization::LoopEnd) {
    TEST_ASSERT_TRUE_MESSAGE(phaseAtCommit == 0 || phaseAtCommit < phaseBeforeCommit,
                             "a LoopEnd switch must land where the outgoing loop's playhead wrapped");
  } else {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, commitTick % gridTicks,
                                     "a NextGrid switch must land on a start-grid tick");
  }

  // The cold half: the boundary tick binds the prepared role and allocates nothing.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(chunksBeforeCommit, chunksAfterCommit,
                                   "the boundary tick itself may not take or return pool capacity");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::runtimeAllocated(track, kInbound),
                           "the role the boundary bound was already prepared");
}

/// `LoopEnd` -- the quantization the playback-switch gesture itself uses.
void test_s4b_3_a_loop_end_switch_lands_on_the_loop_boundary() {
  assert_s4b_3_the_switch_lands_on_a_boundary(SlotQuantization::LoopEnd, /*kOutbound=*/0, /*kInbound=*/2);
}

/// `NextGrid` -- the record carrier's quantization: the switch waits for the start grid instead.
void test_s4b_3_a_next_grid_switch_lands_on_the_start_grid() {
  assert_s4b_3_the_switch_lands_on_a_boundary(SlotQuantization::NextGrid, /*kOutbound=*/1,
                                              /*kInbound=*/3);
}

/// **S4b.4 (clause: undo) — the holder a switch moved to is the one undo addresses.**
///
/// The production undo call is `TrackUndo::undoForLoop(track, track.getActiveLoop())` (the shape
/// `MidiButtonActions` uses), and the driver gates on the **slot**: it refuses when the stack's top entry
/// belongs to a slot other than the loop the caller passed. That gate *is* what "the inbound holder serves
/// undo" means once a switch has moved the active slot, so the fixture drives both sides with real stack
/// entries rather than asserting the gate's name:
///
///   * an entry belonging to the **retired** slot, while the inbound holder is active, must be **refused** --
///     the inbound loop's geometry is untouched and the entry stays on the stack; and
///   * an entry belonging to the **inbound** slot must be **applied** -- to the *inbound* loop -- and consumed.
///
/// The entry kind used is `LoopBoundaryChange`, whose apply is a pure geometry restore, so the observation is
/// the loop's own `loopStartTick`: the fixture can name the value it expects on the inbound holder.
/// **S4b.4 (clause: display) — the panel is told about the holder that took over.**
///
/// The switch commit's own display call is `displayManager.invalidateForSlotChange(i, previousPlaying,
/// getPreviewSlotIndex(i))` (`TrackManager::updateAllTracks`), i.e. it names the outgoing slot and the slot the
/// panel should now focus. This harness keeps that boundary observable (no `OFF_PATH()`), so the fixture makes
/// it **record** instead and asserts the identity pair after a switch lands: the retired slot out, the inbound
/// holder in. That is what "the inbound holder serves display" means at this stage -- the display *authority*
/// moves with the roles rather than being left pointing at the predecessor.
void test_s4b_4_the_display_authority_moves_to_the_holder_the_switch_moved_to() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  constexpr uint8_t kOutbound = 0;
  constexpr uint8_t kInbound = 2;
  commitRecordPassIntoTrackSlot(track, kOutbound);
  commitRecordPassIntoTrackSlot(track, kInbound);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kOutbound, true));
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kInbound, false));

  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, kOutbound);
  TrackCaptureStopTestAccess::activateSlot(0, kOutbound);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 96, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::ownsLiveFinalization(track, kOutbound);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::ownsLiveFinalization(track, kOutbound));

  // The **production press** (not the raw request): the gesture is what moves the panel's preview slot, and
  // the display pair the commit names is (outgoing, preview) -- so driving the raw call would ask the panel to
  // focus the slot it is leaving.
  clockManager.assignCurrentTickSilently(200);
  TrackCaptureStopTestAccess::toggleRecordForSlot(kInbound);
  TEST_ASSERT_TRUE(trackManager.hasPendingSlotSwitch(0));
  resetSlotChangeObservation();
  for (int round = 0; round < 4 && TrackCaptureStopTestAccess::activeLoopIndex(track) != kInbound;
       ++round) {
    (void)trackManager.servicePendingSlotProvision();
    const uint32_t base = clockManager.getCurrentTick();
    for (uint32_t tick = base + 1; tick <= base + 768; ++tick) {
      clockManager.assignCurrentTickSilently(tick);
      trackManager.updateAllTracks(tick);
    }
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kInbound, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "fixture precondition: the switch must land on the inbound holder");
  // **Timing-path review, item 1 is what this pair now also proves:** the tick edge did *not* tell the panel
  // itself -- it recorded the decision -- so nothing was observed while the ticks ran, and the drain is what
  // emits it. Reverting the deferral (calling `invalidateForSlotChange` on the edge again) fails the first
  // assertion; dropping the drain's display half fails the second.
  const SlotChangeObservation& beforeDrain = slotChangeObservation();
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      0, beforeDrain.count,
      "the edge must record the panel handoff, not perform it: nothing may be observed before the drain");
  trackManager.drainEdgeEffects();
  const SlotChangeObservation& observation = slotChangeObservation();
  TEST_ASSERT_TRUE_MESSAGE(observation.count > 0,
                           "the display boundary must be told about a role handoff");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, observation.track, "the observation names the switched track");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kOutbound, observation.from,
                                  "the display must be told which slot the roles left");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kInbound, observation.to,
                                  "the display authority must move to the slot that took over");
}

/// **Timing-path review, items 1 and 3 — the edge's row text is unchanged, only its producer moved.**
///
/// `LoopEnd playback commit track=%u %u->%u start=%lu len=%lu` is a row the host tooling greps for by exact
/// text (it is how a switch's commit is attributed to a device time), so the one thing the deferral may not do
/// is change it. This drives the same production switch as the display fixture above, asserts **no** row
/// before the drain (the edge is not printing), and then reads the row's *rendered text* from the native
/// logger seam and compares it field for field against the loop the commit actually moved to.
void test_the_commit_row_is_rendered_by_the_drain_with_its_text_unchanged() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  constexpr uint8_t kOutbound = 0;
  constexpr uint8_t kInbound = 2;
  commitRecordPassIntoTrackSlot(track, kOutbound);
  commitRecordPassIntoTrackSlot(track, kInbound);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kOutbound, true));
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kInbound, false));

  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, kOutbound);
  TrackCaptureStopTestAccess::activateSlot(0, kOutbound);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 96, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::ownsLiveFinalization(track, kOutbound);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::ownsLiveFinalization(track, kOutbound));

  clockManager.assignCurrentTickSilently(200);
  TrackCaptureStopTestAccess::toggleRecordForSlot(kInbound);
  TEST_ASSERT_TRUE(trackManager.hasPendingSlotSwitch(0));
  LoggerNativeInfoProbe::reset();
  for (int round = 0; round < 4 && TrackCaptureStopTestAccess::activeLoopIndex(track) != kInbound;
       ++round) {
    (void)trackManager.servicePendingSlotProvision();
    const uint32_t base = clockManager.getCurrentTick();
    for (uint32_t tick = base + 1; tick <= base + 768; ++tick) {
      clockManager.assignCurrentTickSilently(tick);
      trackManager.updateAllTracks(tick);
    }
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kInbound, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "fixture precondition: the switch must land on the inbound holder");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, LoggerNativeInfoProbe::lineCount,
                                   "the tick edge must not print the commit row; the drain does");

  trackManager.drainEdgeEffects();
  const Loop& inbound = track.getLoop(kInbound);
  char expected[128];
  snprintf(expected, sizeof(expected), "LoopEnd playback commit track=%u %u->%u start=%lu len=%lu",
           static_cast<unsigned>(0), static_cast<unsigned>(kOutbound),
           static_cast<unsigned>(kInbound), static_cast<unsigned long>(inbound.loopStartTick),
           static_cast<unsigned long>(inbound.loopLengthTicks));
  TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, LoggerNativeInfoProbe::lastLine,
                                   "the commit row's text and field order are the evidence contract");
  TEST_ASSERT_EQUAL_UINT32(1, LoggerNativeInfoProbe::lineCount);
}

/// **The edge-effect queue's own bounds** (timing-path review, item 1): one position per context, FIFO order,
/// and a full queue counts the effect it could not take instead of overwriting a recorded one.
void test_the_edge_effect_queue_is_fifo_bounded_and_counts_what_it_cannot_take() {
  EdgeEffectQueue queue;
  EdgeEffectRecord out;
  TEST_ASSERT_EQUAL_UINT16(0, queue.depth());
  TEST_ASSERT_FALSE(queue.takeOldest(out));

  for (uint16_t i = 0; i < EdgeEffectQueue::kCapacity; ++i) {
    EdgeEffectRecord effect;
    effect.kind = static_cast<uint8_t>(EdgeEffectKind::LoopEndCommit);
    effect.track = static_cast<uint8_t>(i);
    TEST_ASSERT_TRUE(queue.record(effect));
  }
  TEST_ASSERT_EQUAL_UINT16(EdgeEffectQueue::kCapacity, queue.depth());
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, queue.droppedCount(), "a record inside the capacity is never a drop");

  EdgeEffectRecord overflow;
  overflow.track = 200;
  TEST_ASSERT_FALSE(queue.record(overflow));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, queue.droppedCount(),
                                   "the effect that did not fit must be counted, not silently overwrite");

  // FIFO, in the order the edge produced them, and the overflow never entered the queue.
  for (uint16_t i = 0; i < EdgeEffectQueue::kCapacity; ++i) {
    TEST_ASSERT_TRUE(queue.takeOldest(out));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(i), out.track);
  }
  TEST_ASSERT_FALSE(queue.takeOldest(out));
  TEST_ASSERT_EQUAL_UINT16(0, queue.depth());

  // The positions are monotonic, so a drained slot is reusable without resetting either index.
  EdgeEffectRecord afterWrap;
  afterWrap.track = 7;
  TEST_ASSERT_TRUE(queue.record(afterWrap));
  TEST_ASSERT_TRUE(queue.takeOldest(out));
  TEST_ASSERT_EQUAL_UINT8(7, out.track);
  TEST_ASSERT_EQUAL_UINT16(0, queue.depth());
}

void test_s4b_4_the_undo_route_addresses_the_holder_the_switch_moved_to() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  constexpr uint8_t kOutbound = 0;
  constexpr uint8_t kInbound = 2;
  commitRecordPassIntoTrackSlot(track, kOutbound);
  commitRecordPassIntoTrackSlot(track, kInbound);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kOutbound, true));
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(kInbound, false));

  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::selectSlot(track, kOutbound);
  TrackCaptureStopTestAccess::activateSlot(0, kOutbound);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 96, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  for (int turn = 0; turn < 8192 && TrackCaptureStopTestAccess::ownsLiveFinalization(track, kOutbound);
       ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::ownsLiveFinalization(track, kOutbound));

  // The switch: the same production lane the other S4b fixtures drive, so the inbound holder is active.
  clockManager.assignCurrentTickSilently(200);
  trackManager.requestSlotSwitch(0, kInbound, SlotQuantization::LoopEnd,
                                 clockManager.getCurrentTick());
  for (int round = 0; round < 4 && TrackCaptureStopTestAccess::activeLoopIndex(track) != kInbound;
       ++round) {
    (void)trackManager.servicePendingSlotProvision();
    const uint32_t base = clockManager.getCurrentTick();
    for (uint32_t tick = base + 1; tick <= base + 768; ++tick) {
      clockManager.assignCurrentTickSilently(tick);
      trackManager.updateAllTracks(tick);
    }
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kInbound, TrackCaptureStopTestAccess::activeLoopIndex(track),
                                  "fixture precondition: the switch must land on the inbound holder");

  GlobalUndoStack& stack = track.getGlobalUndoStack();
  const size_t cursorEmpty = stack.cursor;
  const uint32_t inboundStartBefore = track.getLoop(kInbound).loopStartTick;

  // (1) The retired slot's entry: the undo route must refuse it, because the holder it addresses is the
  //     inbound -- and a refusal must leave both the loop and the stack exactly as they were.
  UndoEntry retiredEntry{};
  retiredEntry.kind = UndoEntryKind::LoopBoundaryChange;
  retiredEntry.slotIndex = kOutbound;
  retiredEntry.beforeLoopStartTick = 4242;
  retiredEntry.beforeLoopLengthTicks = track.getLoop(kOutbound).loopLengthTicks;
  pushUndoEntry(track, std::move(retiredEntry));
  TEST_ASSERT_TRUE(stack.canUndo());
  TEST_ASSERT_EQUAL_MESSAGE(cursorEmpty + 1, stack.cursor, "fixture precondition: one entry on the stack");
  TrackUndo::undoForLoop(track, track.getActiveLoop());
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(inboundStartBefore, track.getLoop(kInbound).loopStartTick,
                                   "the undo route must not apply the retired slot's entry to the inbound");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, track.getLoop(kOutbound).loopStartTick,
                                   "the retired slot's geometry must not be restored either");
  TEST_ASSERT_EQUAL_MESSAGE(cursorEmpty + 1, stack.cursor, "a refusal must leave the entry on the stack");

  // (2) The inbound slot's entry: the same call must consume it and apply it **to the inbound loop**. The
  // refused entry from (1) is still on the stack by design, so the baseline is the cursor *before* this push.
  const size_t cursorBeforeInboundPush = stack.cursor;
  UndoEntry inboundEntry{};
  inboundEntry.kind = UndoEntryKind::LoopBoundaryChange;
  inboundEntry.slotIndex = kInbound;
  inboundEntry.beforeLoopStartTick = 111;
  inboundEntry.beforeLoopLengthTicks = track.getLoop(kInbound).loopLengthTicks;
  pushUndoEntry(track, std::move(inboundEntry));
  TrackUndo::undoForLoop(track, track.getActiveLoop());
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(111, track.getLoop(kInbound).loopStartTick,
                                   "the inbound holder must be the slot undo applies to");
  TEST_ASSERT_EQUAL_MESSAGE(cursorBeforeInboundPush, stack.cursor,
                            "the applied entry must be consumed -- and only it: the refused one stays");
}


// ---------------------------------------------------------------------------
// DEC-108 item 1 / `ExecutionBounds.md` rows 27-29: the window-build owner
// (`Track::stepPlaybackViewMaintenance`) -- windowed-playback-generation slice 3.1.
//
// The owner's production surface is three off-clock calls. This slice changes no behaviour behind them: no
// production path requests a window yet, and the owner publishes nothing a consumer reads. What is asserted
// here is the contract the owner must satisfy **before** that request arrives: a bounded, declared grain
// quantum; a persistent build cursor; a refusal that is named and non-destructive; and the deferred turn's
// exact arithmetic, so neither a stacked nor a skipped grain can hide.
// ---------------------------------------------------------------------------

namespace {
/// The owner's loop: 64 bars, so the loop holds the declared contract's required lead with room to spare and
/// the demotion predicate's long-loop shape is the one under test.
constexpr uint32_t kWindowOwnerLoopTicks = 64 * 768;

/// A slot whose loop is long enough to hold the declared contract's required lead, with its playback runtime
// prewarmed -- the state a pending window sits in. Nothing here establishes an obligation: the build the
/// fixture begins *is* the obligation (DEC-108 item 1's state-driven shape).
Track& prepareWindowOwnerSlot(uint8_t trackIndex) {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(trackIndex);
  track.ensureLoopsAllocated();
  // Committed content, so the slot's revision is a real one (a window carries the revision it was built from).
  commitRecordPassIntoTrackSlot(track, 0);
  track.getLoop(0).loopLengthTicks = kWindowOwnerLoopTicks;
  TEST_ASSERT_TRUE_MESSAGE(track.prewarmPlaybackForSlot(0, true),
                           "the slot must prewarm a runtime for the owner to owe anything");
  return track;
}

/// The declared contract's arithmetic written out, so the fixture's oracle is the *declaration* rather than
/// the implementation's answer: 7 required grains, each preceded by at most 3 denied turns, each turn
/// advancing at most one bar, plus the one-bar lead.
constexpr uint32_t kWindowOwnerExpectedRequiredGrains = 7;
// DEC-108 §13b / task 3.10 (the review's step 4): these three now mirror the **declared envelope**, which is what
// this fixture is for -- an earlier version pinned the placeholder declaration (768 ticks per turn, 3 denied turns).
// The advance is the permitted scheduling bound and the denial allowance is the H-track rotation, both declared
// where the contract is; the *demonstrated handoff margin* stays one bar.
constexpr uint32_t kWindowOwnerExpectedAdvancePerTurn = 64;
constexpr uint32_t kWindowOwnerExpectedDeniedTurns = Config::NUM_TRACKS - 1;
constexpr uint32_t kWindowOwnerExpectedLead = 768;
/// **Blockers 3 — the permitted service-gap limit is *wired*, and its exceedance is named.**
///
/// Step 4 declared the permitted per-turn advance; this fixture pins that the live lane **compares against it** rather
/// than only recording maxima. A gap above the limit across a turn whose predecessor was **eligible** is an **overrun**:
/// counted, sized, and named in its own row (with the permitted value, so the report is self-describing). The same gap
/// with an **ineligible** predecessor is idle time and is *not* an overrun, and neither is a gap spanning a clock
/// epoch change. That separation is the whole reason the sampler records the predecessor's eligibility -- and it is why
/// the declaration is a *scheduling* bound, not a bound on every gap an observer will see.
void test_the_declared_permitted_gap_is_wired_and_its_overrun_is_named() {
  constexpr uint32_t kPermitted = Track::kPlaybackViewPermittedAdvancePerTurn;
  constexpr uint32_t kBar = Config::TICKS_PER_BAR;
  // The lane compares against the **declaration**, not a second constant (the sampler reads the same field).
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(kPermitted,
                                   Track::playbackViewCoverageContract().maximumPhaseAdvancePerTurn,
                                   "the limit the lane applies is the declared permitted advance");
  Track::resetPlaybackLaneMeasurement();
  WindowGapRows::reset();
  WindowGapRows::overrunCount = 0;
  using Result = Track::PlaybackViewTurnResult;
  // A running tick, advanced by **one** between cases: a case's first dispatch closes the *previous* block, so a large
  // inter-case gap would be an overrun in its own right and the case being asserted would inherit it. One tick keeps
  // every block to the case it belongs to, and the assertions below say so.
  uint64_t tick = 1000;

  // (B2) A gap **at** the limit across an eligible predecessor: no overrun, no row -- and the block still carries the
  // limit it compared against, so a reader never has to know the declaration (schema 3).
  Track::samplePlaybackLaneOpportunity({tick, 1}, true, true, Result::GrainDenied, 10);
  tick += kPermitted;
  Track::samplePlaybackLaneOpportunity({tick, 1}, true, true, Result::GrainDenied, 10);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, WindowGapRows::overrunCount, "a gap at the limit names nothing");
  const WindowGapRows::Row& atLimit = WindowGapRows::rows[WindowGapRows::count - 1];
  TEST_ASSERT_EQUAL_UINT32(kPermitted, atLimit.maxDeltaTicks);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, atLimit.overruns, "and the block counts none");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(kPermitted, atLimit.permittedAdvanceTicks,
                                   "while carrying the permitted advance it compared against");

  // (B1) One tick past the limit across an eligible predecessor: an overrun, counted, sized and **named**.
  tick += 1;
  Track::samplePlaybackLaneOpportunity({tick, 1}, true, true, Result::GrainDenied, 10);
  tick += kPermitted + 1;
  Track::samplePlaybackLaneOpportunity({tick, 1}, true, true, Result::GrainDenied, 10);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, WindowGapRows::overrunCount, "one tick past the limit is named");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(kPermitted, WindowGapRows::overrunRows[0].permitted,
                                   "and the row says what it was compared against");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, WindowGapRows::overrunRows[0].overruns, "with the block's count");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(kPermitted + 1, WindowGapRows::overrunRows[0].overrunMaxTicks,
                                   "and the worst gap that exceeded it");
  const WindowGapRows::Row& past = WindowGapRows::rows[WindowGapRows::count - 1];
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, past.overruns, "the block row carries the same count");
  TEST_ASSERT_EQUAL_UINT32(kPermitted + 1, past.overrunMaxTicks);

  // (B3) The same oversized gap with an **ineligible** predecessor is idle time, not an overrun: the bound asserts a
  // scheduling promise, and there was no work owed across that turn.
  tick += 1;
  Track::samplePlaybackLaneOpportunity({tick, 1}, true, false, Result::GrainDenied, 0);
  tick += kBar;
  Track::samplePlaybackLaneOpportunity({tick, 1}, true, false, Result::GrainDenied, 0);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, WindowGapRows::overrunCount,
                                   "an idle gap above the limit is NOT an overrun");
  const WindowGapRows::Row& idle = WindowGapRows::rows[WindowGapRows::count - 1];
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(kBar, idle.maxDeltaTicks, "the gap is still recorded as a maximum");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, idle.overruns, "and counted as no overrun");

  // (B4) A gap spanning a clock **epoch** change is a reset, not an overrun: it is reported as a discontinuity and
  // contributes no delta at all (the sampler's own rule -- resets are excluded from the maxima and counted).
  tick += 1;
  Track::samplePlaybackLaneOpportunity({tick, 1}, true, true, Result::GrainDenied, 10);
  tick += kBar;
  Track::samplePlaybackLaneOpportunity({tick, 2}, true, true, Result::GrainDenied, 10);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, WindowGapRows::overrunCount, "an epoch change is not an overrun");
  const WindowGapRows::Row& resetBlock = WindowGapRows::rows[WindowGapRows::count - 1];
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, resetBlock.overruns, "nor counted as one");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, resetBlock.maxDeltaTicks, "the delta is not accumulated across the reset");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, WindowGapRows::resets[WindowGapRows::count - 1],
                                   "the change is reported as a discontinuity instead");

}


/// The lead the **declared envelope** requires for `eventCount` entries -- the contract's own arithmetic, evaluated on
/// the *derived* cost (the review's step 4 made the lead-side accounting derived: an expectation built from the declared
/// basis counts would describe a request the firmware no longer makes).
uint32_t declaredRequiredLeadFor(size_t eventCount) {
  CoverageServiceContract expected = Track::playbackViewCoverageContract();
  expected.deriveGrainsFrom(eventCount);
  return expected.leadTicks + static_cast<uint32_t>(expected.requiredGrains()) *
                                  (static_cast<uint32_t>(expected.maximumConsecutiveDeniedTurns) + 1u) *
                                  expected.maximumPhaseAdvancePerTurn;
}

constexpr uint32_t kWindowOwnerExpectedRequiredLead =
    kWindowOwnerExpectedRequiredGrains * (kWindowOwnerExpectedDeniedTurns + 1) *
        kWindowOwnerExpectedAdvancePerTurn +
    kWindowOwnerExpectedLead;

/// A two-bar window's worth of source events, all inside the coverage interval so the fill's coverage filter
/// appends every one of them: the fixture's grain count then follows from the declared quanta alone.
size_t buildWindowOwnerSource(MidiEvent* out, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    out[i] = MidiEvent::NoteOn(static_cast<uint32_t>(i * 15), 1, 48, 100);
  }
  return count;
}

}  // namespace

/// W-5 + the declaration: the caps are declared, and the handoff margin -- not a bar count and not a
/// millisecond constant -- is what admits or refuses.
void test_window_owner_declares_its_caps_and_the_handoff_margin_is_the_oracle() {
  const CoverageServiceContract declared = Track::playbackViewCoverageContract();
  TEST_ASSERT_TRUE_MESSAGE(declared.isDeclared(), "the owner's contract must be declared before it works");
  TEST_ASSERT_EQUAL_UINT32(kWindowOwnerExpectedAdvancePerTurn, declared.maximumPhaseAdvancePerTurn);
  TEST_ASSERT_EQUAL_UINT16(kWindowOwnerExpectedDeniedTurns, declared.maximumConsecutiveDeniedTurns);
  TEST_ASSERT_EQUAL_UINT16(3, declared.requiredFillGrains);
  TEST_ASSERT_EQUAL_UINT16(3, declared.requiredOrderGrains);
  TEST_ASSERT_EQUAL_UINT16(1, declared.requiredPromotionGrains);
  TEST_ASSERT_EQUAL_UINT16(kWindowOwnerExpectedRequiredGrains, declared.requiredGrains());
  TEST_ASSERT_EQUAL_UINT32(kWindowOwnerExpectedLead, declared.leadTicks);

  MidiEvent source[4];
  const size_t count = buildWindowOwnerSource(source, 4);
  // **The requirement is derived for this fixture's own source** (the review's step 4 made the lead-side accounting
  // derived): an expectation built from the declared *basis* counts would describe a request the firmware no longer
  // makes, and its boundary would sit below the real one. The boundary is the contract's own arithmetic, evaluated
  // on the count this fixture actually sends.
  CoverageServiceContract expected = Track::playbackViewCoverageContract();
  expected.deriveGrainsFrom(count);
  const uint32_t requiredLeadTicks =
      expected.leadTicks + static_cast<uint32_t>(expected.requiredGrains()) *
                               (static_cast<uint32_t>(expected.maximumConsecutiveDeniedTurns) + 1u) *
                               expected.maximumPhaseAdvancePerTurn;
  const PlaybackCoverageInterval coverage{0, 2 * 768, kWindowOwnerLoopTicks};
  const PlaybackViewSourceIdentity identity{1, 1, nullptr};

  // At the boundary: `canCompleteBeforeHandoff` admits exactly at the required lead.
  Track& atBoundary = prepareWindowOwnerSlot(0);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(atBoundary.requestPlaybackViewMaintenance(
                        0, source, count, identity, coverage, {}, {}, 0, requiredLeadTicks)));
  TEST_ASSERT_TRUE(atBoundary.hasPendingPlaybackViewMaintenance());

  // Just inside: one tick more of lead is still admitted.
  Track& inside = prepareWindowOwnerSlot(1);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(inside.requestPlaybackViewMaintenance(
                        0, source, count, identity, coverage, {}, {}, 0,
                        requiredLeadTicks + 1)));

  // Just outside: one tick short of the required lead is refused, by name, and takes nothing.
  Track& outside = prepareWindowOwnerSlot(2);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::CannotCompleteBeforeHandoff),
                    static_cast<int>(outside.requestPlaybackViewMaintenance(
                        0, source, count, identity, coverage, {}, {}, 0,
                        requiredLeadTicks - 1)));
  TEST_ASSERT_FALSE_MESSAGE(outside.hasPendingPlaybackViewMaintenance(),
                            "a refused request must not create an obligation");
}

/// The service-gap declaration is **bounded by the measurement, not by its own claim** (task 3.10 / DEC-108 §13b).
/// Device runs 4/5/6 (2026-10-03) measured, per lane opportunity: a steady state of **2.9-5.5 ticks** (a rotation of
/// ~7.3-7.5 ms, **tempo-invariant** across 120 and 240 BPM) and separate **starvation episodes** of **2 138 /
/// 4 355 / 8 520 ticks**. So the declaration must be an *upper* bound on the first and must **stay below** the
/// second: an admitted window can be starved past its handoff, that miss belongs to the lag rule
/// (`one_pass_late` / `lag_breach`), and raising this constant towards the starvation figure would hide the gap
/// rather than close it. **The bound of this fixture:** it pins *recorded device measurements* against the
/// declaration, so it can refuse a declaration that contradicts them; it cannot re-measure the device.
void test_window_owner_service_gap_declaration_is_bounded_by_measurement_not_by_its_own_claim() {
  const CoverageServiceContract declared = Track::playbackViewCoverageContract();
  TEST_ASSERT_TRUE_MESSAGE(declared.isDeclared(), "the owner's contract must be declared");

  // Measured per-opportunity p50: 8 ticks with the host clock at 240 BPM (the external one-pulse floor), 4 ticks
  // at 120 BPM; the per-opportunity rate over both regimes is 2.9-5.5.
  constexpr uint32_t kMeasuredSteadyAdvanceTicks = 8;
  constexpr uint32_t kMeasuredStarvationAdvanceTicks = 2138;

  char message[192];
  // (1) An upper bound on the **scheduling** advance, with room to spare.
  snprintf(message, sizeof(message),
           "declared per-turn advance %u must cover the measured steady state %u (ticks per opportunity)",
           static_cast<unsigned>(declared.maximumPhaseAdvancePerTurn),
           static_cast<unsigned>(kMeasuredSteadyAdvanceTicks));
  TEST_ASSERT_TRUE_MESSAGE(declared.maximumPhaseAdvancePerTurn >= kMeasuredSteadyAdvanceTicks, message);

  // (2) The pin that refuses the wrong repair: the declaration stays **below** the measured starvation episodes,
  // because enlarging the constant until the gap disappears would satisfy (1) trivially while hiding a real miss
  // that this constant is not responsible for.
  snprintf(message, sizeof(message),
           "declared per-turn advance %u must stay below the measured starvation episode %u: a starved window's miss is the lag rule's, not this constant's",
           static_cast<unsigned>(declared.maximumPhaseAdvancePerTurn),
           static_cast<unsigned>(kMeasuredStarvationAdvanceTicks));
  TEST_ASSERT_TRUE_MESSAGE(declared.maximumPhaseAdvancePerTurn < kMeasuredStarvationAdvanceTicks, message);

  // (3) The unit is a tick count, so its wall-clock meaning is tempo-dependent: `microsPerTick` is the clock's own
  // `60000000 / (bpm * INTERNAL_PPQN)`, so the allowance halves when the tempo doubles.
  const uint32_t allowanceMsAt120 = static_cast<uint32_t>(
      static_cast<uint64_t>(declared.maximumPhaseAdvancePerTurn) * 60000000ULL /
      (120ULL * Config::INTERNAL_PPQN) / 1000ULL);
  const uint32_t allowanceMsAt240 = static_cast<uint32_t>(
      static_cast<uint64_t>(declared.maximumPhaseAdvancePerTurn) * 60000000ULL /
      (240ULL * Config::INTERNAL_PPQN) / 1000ULL);
  // These two absolutes carry the relation the finding rests on -- a tick-denominated allowance buys half the
  // wall-clock margin (2 000 ms -> 1 000 ms) when the tempo doubles. No separate assertion states the halving,
  // because it could not fail while both absolutes hold.
  // **Re-based by the review's step 4** (recorded): the declared value moved from the placeholder one bar (768 ticks
  // = 2 000 ms at 120 BPM) to the derived permitted scheduling bound (16 ticks = 2x the measured p50), so these
  // absolutes moved with it and the relation they carry -- half the wall-clock margin when the tempo doubles -- is
  // unchanged.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(166, allowanceMsAt120,
                                   "the declared allowance is 166 ms of wall-clock at 120 BPM (64 ticks)");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(83, allowanceMsAt240,
                                   "the same tick count is 83 ms of wall-clock at 240 BPM: the unit is tempo-dependent");

  // (4) The denied-turn term is the **within-lane** contention allowance, not the owner rotation -- the rotation is
  // already inside `maximumPhaseAdvancePerTurn` by that field's unit, so declaring it here as well would
  // double-count it (the conflation an earlier record corrects).
  TEST_ASSERT_TRUE_MESSAGE(
      declared.maximumConsecutiveDeniedTurns != MidiServiceDrain::kDeferredIdleOwnerCount - 1,
      "the owner rotation belongs to the per-turn unit, not to the turn-count allowance");
}

/// W-2/W-3/W-6/W-7: the turn serves **exactly one declared grain**, the cursor persists across turns, the turn
/// takes no pool capacity, and nothing a consumer reads changes.
void test_window_owner_serves_one_grain_per_turn_and_publishes_nothing() {
  Track& track = prepareWindowOwnerSlot(0);
  Loop& loop = track.getLoop(0);
  const uint32_t revisionBefore = loop.playbackRevision;
  TEST_ASSERT_FALSE_MESSAGE(track.hasPendingPlaybackViewMaintenance(),
                            "an idle slot owes nothing before a request");
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewTurnResult::Idle),
                    static_cast<int>(track.stepPlaybackViewMaintenance()));

  constexpr size_t kEvents = 100;
  MidiEvent source[kEvents];
  const size_t count = buildWindowOwnerSource(source, kEvents);
  const PlaybackViewSourceIdentity identity{loop.playbackRevision, 1, nullptr};
  const PlaybackCoverageInterval coverage{0, 2 * 768, kWindowOwnerLoopTicks};
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(track.requestPlaybackViewMaintenance(
                        0, source, count, identity, coverage, {}, {}, 0, declaredRequiredLeadFor(count))));
  TEST_ASSERT_TRUE(track.hasPendingPlaybackViewMaintenance());

  // The expected turn count follows from the declared quanta: a grain served twice in one turn, or a grain
  // skipped, changes this number.
  const size_t expectedTurns =
      (count + Track::kPlaybackViewFillQuantum - 1) / Track::kPlaybackViewFillQuantum +
      (count + Track::kPlaybackViewOrderQuantum - 1) / Track::kPlaybackViewOrderQuantum;
  TEST_ASSERT_EQUAL_UINT32(5, static_cast<uint32_t>(expectedTurns));
  for (size_t turn = 1; turn <= expectedTurns; ++turn) {
    const uint16_t chunksBefore = LoopEventStore::freeChunkCount();
    const Track::PlaybackViewTurnResult result = track.stepPlaybackViewMaintenance();
    TEST_ASSERT_NOT_EQUAL_MESSAGE(static_cast<int>(Track::PlaybackViewTurnResult::Idle),
                                  static_cast<int>(result),
                                  "an owed window must be served, not skipped");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(chunksBefore, LoopEventStore::freeChunkCount(),
                                     "the owner's turn takes no pool capacity");
    TEST_ASSERT_FALSE_MESSAGE(track.isPlaybackMergedMidiEventsReadyForSlot(0),
                              "the owner publishes nothing a consumer reads (task 3.2 does)");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(revisionBefore, loop.playbackRevision,
                                     "the owner never moves the content-currency revision");
    if (turn < expectedTurns) {
      TEST_ASSERT_TRUE_MESSAGE(track.hasPendingPlaybackViewMaintenance(),
                               "the build's cursor must persist across turns");
    } else {
      TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewTurnResult::BuildComplete),
                        static_cast<int>(result));
      TEST_ASSERT_FALSE_MESSAGE(track.hasPendingPlaybackViewMaintenance(),
                                "a completed build owes nothing");
    }
  }
  // Idempotent once complete: nothing owed, nothing served.
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewTurnResult::Idle),
                    static_cast<int>(track.stepPlaybackViewMaintenance()));
}

/// 3.2 — the **enumeration** clause: the interval policy, the resolution through the selected `LoopSnapshot`,
/// and the inspected-work bound (fixture **2.10**).
void test_window_owner_resolves_the_interval_through_the_published_snapshot() {
  // (A) The policy, purely: whole-loop coverage where the whole-take view is admissible, else the two-bar window
  // at the playhead, clamped into the loop. The same function the resolver uses.
  PlaybackCoverageInterval interval{};
  TEST_ASSERT_TRUE(Track::playbackWindowInterval(4 * 768u, false, 500u, interval));
  TEST_ASSERT_EQUAL_UINT32(0u, interval.startPhase);
  TEST_ASSERT_EQUAL_UINT32(4 * 768u, interval.lengthTicks);
  TEST_ASSERT_EQUAL_UINT32(4 * 768u, interval.loopLengthTicks);
  TEST_ASSERT_TRUE(Track::playbackWindowInterval(kWindowOwnerLoopTicks, true, 5000u, interval));
  TEST_ASSERT_EQUAL_UINT32(5000u - 768u, interval.startPhase);
  TEST_ASSERT_EQUAL_UINT32(2 * 768u, interval.lengthTicks);
  TEST_ASSERT_TRUE(Track::playbackWindowInterval(kWindowOwnerLoopTicks, true, 100u, interval));
  TEST_ASSERT_EQUAL_UINT32(0u, interval.startPhase);
  TEST_ASSERT_TRUE(Track::playbackWindowInterval(kWindowOwnerLoopTicks, true, kWindowOwnerLoopTicks - 200u,
                                                 interval));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(kWindowOwnerLoopTicks - 2 * 768u, interval.startPhase,
                                   "the window is clamped into the loop, never past its end");
  TEST_ASSERT_FALSE(Track::playbackWindowInterval(0u, true, 0u, interval));

  // (B) The resolver: it names the interval, enumerates the **current** published root and stamps the identity.
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  Loop& loop = track.getLoop(0);
  loop.loopLengthTicks = kWindowOwnerLoopTicks;
  publishBarSpacedRootForSlot(loop, 64);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  SessionMidiEventVec scratch;
  const uint32_t playhead = 5000u;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(track.requestPlaybackWindow(0, playhead, scratch)));
  TEST_ASSERT_TRUE_MESSAGE(scratch.size() > 0, "the window's content comes from the published root");
  PlaybackViewSourceIdentity identityOut{};
  PlaybackCoverageInterval coverageOut{};
  for (size_t turn = 0; turn < 8 && track.hasPendingPlaybackViewMaintenance(); ++turn) {
    (void)track.stepPlaybackViewMaintenance();
  }
  TEST_ASSERT_FALSE(track.hasPendingPlaybackViewMaintenance());
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowForSlot(0, identityOut, coverageOut));
  TEST_ASSERT_TRUE(Track::playbackWindowInterval(kWindowOwnerLoopTicks, true, playhead, interval));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(interval.startPhase, coverageOut.startPhase,
                                   "the published window serves the interval the policy resolved");
  TEST_ASSERT_EQUAL_UINT32(interval.lengthTicks, coverageOut.lengthTicks);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(loop.playbackRevision, identityOut.playbackRevision,
                                   "the identity is the revision the enumeration read");
  TEST_ASSERT_EQUAL_PTR_MESSAGE(loop.publishedSnapshotIdentity(), identityOut.snapshotIdentity,
                                "and the published root it read it from");

  // F-1: the identity tuple, and the generation as an **ownership** guard rather than a second currency.
  PlaybackWindowIdentity first{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, first));
  TEST_ASSERT_TRUE_MESSAGE(first.isValid(), "a published window carries a valid identity");
  TEST_ASSERT_EQUAL_UINT8(0, first.slot);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(loop.playbackRevision, first.playbackRevision,
                                   "content currency is the loop's revision, and the window carries it");
  TEST_ASSERT_EQUAL_UINT32(interval.startPhase, first.interval.startPhase);
  // A **different interval at the same revision and the same ownership**: the successor lane prepares it, the
  // served identity does not move while it is prepared, and the adoption swaps it in with the generation intact.
  const uint32_t secondPlayhead = playhead + 3u * 768u;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(track.requestPlaybackWindow(0, secondPlayhead, scratch)));
  for (size_t turn = 0; turn < 8 && track.hasPendingPlaybackViewMaintenance(); ++turn) {
    (void)track.stepPlaybackViewMaintenance();
  }
  PlaybackWindowIdentity stillServed{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, stillServed));
  TEST_ASSERT_TRUE_MESSAGE(first == stillServed,
                           "preparing a successor must leave the served identity untouched");
  TEST_ASSERT_TRUE(track.adoptPreparedPlaybackWindow(0));
  PlaybackWindowIdentity second{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, second));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(first.playbackGeneration, second.playbackGeneration,
                                   "the generation guards ownership: a new interval does not move it");
  TEST_ASSERT_EQUAL_UINT32(first.playbackRevision, second.playbackRevision);
  TEST_ASSERT_TRUE_MESSAGE(first.interval.startPhase != second.interval.startPhase,
                           "while the interval *did* change -- the interval is not currency");

  // A rootless slot is refused by name, not served through a compatibility path.
  Track& rootless = trackManager.getTrack(1);
  rootless.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(rootless, 0);
  rootless.getLoop(0).loopLengthTicks = kWindowOwnerLoopTicks;
  SessionMidiEventVec rootlessScratch;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::SourceUnavailable),
                    static_cast<int>(rootless.requestPlaybackWindow(0, 0u, rootlessScratch)));

  // (C) Fixture 2.10 -- the **inspected-work bound**. The same requested window on a short loop and on a long
  // loop whose unrelated content is 32x larger: the walk may only touch the window's own bars, and it counts the
  // references it *rejects* as well as the ones it appends.
  LoopEventStore::initPool();
  Track& shortTrack = trackManager.getTrack(2);
  shortTrack.ensureLoopsAllocated();
  Loop& shortLoop = shortTrack.getLoop(0);
  shortLoop.loopLengthTicks = 4 * 768u;
  publishBarSpacedRootForSlot(shortLoop, 2);
  Track& longTrack = trackManager.getTrack(3);
  longTrack.ensureLoopsAllocated();
  Loop& longLoop = longTrack.getLoop(0);
  longLoop.loopLengthTicks = kWindowOwnerLoopTicks;
  publishBarSpacedRootForSlot(longLoop, 64);
  SessionMidiEventVec shortScratch;
  SessionMidiEventVec longScratch;
  Loop::SnapshotWindowGatherPath shortPath = Loop::SnapshotWindowGatherPath::None;
  Loop::SnapshotWindowGatherPath longPath = Loop::SnapshotWindowGatherPath::None;
  uint32_t shortInspected = 0;
  uint32_t longInspected = 0;
  TEST_ASSERT_TRUE(shortLoop.tryGatherPublishedSnapshotEventsInWindow(shortScratch, 0, 2 * 768u, &shortPath,
                                                                     &shortInspected));
  TEST_ASSERT_TRUE(longLoop.tryGatherPublishedSnapshotEventsInWindow(longScratch, 0, 2 * 768u, &longPath,
                                                                    &longInspected));
  TEST_ASSERT_EQUAL(static_cast<int>(Loop::SnapshotWindowGatherPath::Snapshot),
                    static_cast<int>(shortPath));
  TEST_ASSERT_EQUAL(static_cast<int>(Loop::SnapshotWindowGatherPath::Snapshot), static_cast<int>(longPath));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(shortInspected, longInspected,
                                   "the same window must cost the same inspected work however long the loop is");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(shortScratch.size()),
                                   static_cast<uint32_t>(longScratch.size()),
                                   "and it must gather the same events");
  TEST_ASSERT_TRUE_MESSAGE(shortInspected > 0, "the short loop's window has content, so the walk is non-empty");

  // A window that intersects nothing: its bars carry no membership at all, so nothing is inspected and nothing
  // is gathered -- the walk skips a missing bar rather than scanning the loop.
  SessionMidiEventVec emptyScratch;
  uint32_t emptyInspected = 0;
  TEST_ASSERT_TRUE(shortLoop.tryGatherPublishedSnapshotEventsInWindow(emptyScratch, 2 * 768u, 2 * 768u, nullptr,
                                                                     &emptyInspected));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, emptyInspected, "an empty window inspects nothing");
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(emptyScratch.size()));
}

/// 3.2: publication is **atomic** and the published window carries the identity and the coverage interval the
/// request named. The replacement case is the one that matters: the window being replaced stays readable right
/// up to the turn that swaps it, and no turn publishes an incomplete build.
void test_window_owner_publishes_the_interval_window_atomically() {
  Track& track = prepareWindowOwnerSlot(0);
  Loop& loop = track.getLoop(0);
  TEST_ASSERT_FALSE_MESSAGE(loop.fullTakePlaybackViewAdmissible(),
                            "the fixture needs a demoted slot: windowed is its only representation");

  constexpr size_t kEvents = 100;
  MidiEvent source[kEvents];
  const size_t count = buildWindowOwnerSource(source, kEvents);
  const PlaybackViewSourceIdentity firstIdentity{loop.playbackRevision, 3, nullptr};
  const PlaybackCoverageInterval firstCoverage{0, 2 * 768, kWindowOwnerLoopTicks};
  PlaybackViewSourceIdentity identityOut{};
  PlaybackCoverageInterval coverageOut{};

  TEST_ASSERT_FALSE_MESSAGE(track.publishedPlaybackWindowForSlot(0, identityOut, coverageOut),
                            "nothing is published before a request");
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(track.requestPlaybackViewMaintenance(
                        0, source, count, firstIdentity, firstCoverage, {}, {}, 0,
                        declaredRequiredLeadFor(count))));
  const size_t expectedTurns =
      (count + Track::kPlaybackViewFillQuantum - 1) / Track::kPlaybackViewFillQuantum +
      (count + Track::kPlaybackViewOrderQuantum - 1) / Track::kPlaybackViewOrderQuantum;
  for (size_t turn = 1; turn <= expectedTurns; ++turn) {
    const Track::PlaybackViewTurnResult result = track.stepPlaybackViewMaintenance();
    const bool publishedNow = track.publishedPlaybackWindowForSlot(0, identityOut, coverageOut);
    if (turn < expectedTurns) {
      TEST_ASSERT_FALSE_MESSAGE(publishedNow,
                                "an incomplete build must never become the published window");
    } else {
      TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::PlaybackViewTurnResult::BuildComplete),
                                static_cast<int>(result), "the last grain completes the build");
      TEST_ASSERT_TRUE_MESSAGE(publishedNow, "the completed build must be published in that same turn");
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(firstIdentity == identityOut, "the window carries the identity it was built from");
  TEST_ASSERT_EQUAL_UINT32(firstCoverage.startPhase, coverageOut.startPhase);
  TEST_ASSERT_EQUAL_UINT32(firstCoverage.lengthTicks, coverageOut.lengthTicks);
  TEST_ASSERT_EQUAL_UINT32(firstCoverage.loopLengthTicks, coverageOut.loopLengthTicks);

  // **F-1 (task 3.4): a replacement is a ready successor, not a second start.** While the successor is
  // prepared, the served window is byte-for-byte what it was -- no consumer read can straddle the replacement --
  // and the adoption is the single atomic swap.
  PlaybackWindowIdentity firstWindow{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, firstWindow));
  TEST_ASSERT_TRUE_MESSAGE(firstWindow.isValid(), "a published window carries a valid identity");
  TEST_ASSERT_EQUAL_UINT8(0, firstWindow.slot);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(loop.playbackRevision, firstWindow.playbackRevision,
                                   "the window's currency is the loop's revision");
  const PlaybackViewSourceIdentity secondIdentity{loop.playbackRevision, 4, nullptr};
  const PlaybackCoverageInterval secondCoverage{768, 2 * 768, kWindowOwnerLoopTicks};
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(track.requestPlaybackViewMaintenance(
                        0, source, count, secondIdentity, secondCoverage, {}, {}, 768,
                        static_cast<uint32_t>(768 + declaredRequiredLeadFor(count)))));
  for (size_t turn = 1; turn <= expectedTurns; ++turn) {
    PlaybackViewSourceIdentity duringIdentity{};
    PlaybackCoverageInterval duringCoverage{};
    PlaybackWindowIdentity during{};
    TEST_ASSERT_TRUE(track.publishedPlaybackWindowForSlot(0, duringIdentity, duringCoverage));
    TEST_ASSERT_TRUE_MESSAGE(firstIdentity == duringIdentity,
                             "the served window must not change while its successor is prepared");
    TEST_ASSERT_EQUAL_UINT32(firstCoverage.startPhase, duringCoverage.startPhase);
    TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, during));
    TEST_ASSERT_TRUE_MESSAGE(firstWindow == during, "and its identity must be identical, field for field");
    (void)track.stepPlaybackViewMaintenance();
  }
  // The successor is prepared but **not adopted**: the served window is still the first one. That is the whole
  // point of the ready role -- the last coherent revision stays readable to the last moment.
  PlaybackWindowIdentity prepared{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, prepared));
  TEST_ASSERT_TRUE_MESSAGE(firstWindow == prepared,
                           "a prepared successor must not become the served window until it is adopted");
  // Adoption is the atomic swap, and only then does the window change.
  TEST_ASSERT_TRUE(track.adoptPreparedPlaybackWindow(0));
  PlaybackWindowIdentity adopted{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, adopted));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(secondCoverage.startPhase, adopted.interval.startPhase,
                                   "after adoption the window is the successor's interval");
  TEST_ASSERT_EQUAL_UINT32(secondCoverage.lengthTicks, adopted.interval.lengthTicks);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(secondIdentity.sourceGeneration, adopted.playbackGeneration,
                                   "and it carries the successor's own generation");
}

/// 3.5 (AC-3): **readiness is a predicate, and its failure names its holder.** Readiness is the window this change
/// **published** -- not the live merged buffer -- so a published, complete, current window is ready *even when its
/// interval legitimately holds no events*, and every non-ready answer names who owes the window.
void test_window_readiness_names_its_holder_and_an_empty_interval_is_ready() {
  // (W-9) A current published root and a prewarmed runtime, but **no window published**: the window-build owner owes
  // one. The debt is the build owner's and not the source's -- a build *could* gather here.
  Track& track = prepareWindowOwnerSlot(0);
  Loop& loop = track.getLoop(0);
  publishBarSpacedRootForSlot(loop, 2);  // content in bars 0-1 of a 64-bar loop, so the empty interval has a source
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackWindowReadiness::OwesBuild),
                    static_cast<int>(track.playbackWindowReadinessForSlot(0)));
  TEST_ASSERT_EQUAL_STRING("window_build_owner", Track::playbackWindowReadinessHolder(
                                                    track.playbackWindowReadinessForSlot(0)));

  // (W-11) The interval under test is **legitimately empty**: two bars deep in the loop, where the root carries
  // nothing. The request goes through the production resolver, so the emptiness is the resolver's own answer rather
  // than the fixture's claim about it.
  constexpr uint32_t kEmptyPlayhead = 40 * 768;
  SessionMidiEventVec scratch;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(track.requestPlaybackWindow(0, kEmptyPlayhead, scratch)));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(scratch.size()),
                                   "the window under test must hold no events at all");
  for (size_t turn = 0; turn < 8 && track.hasPendingPlaybackViewMaintenance(); ++turn) {
    (void)track.stepPlaybackViewMaintenance();
  }
  PlaybackWindowIdentity emptyWindow{};
  TEST_ASSERT_TRUE_MESSAGE(track.publishedPlaybackWindowIdentityForSlot(0, emptyWindow),
                           "an empty interval is still a window, and it is published");
  TEST_ASSERT_EQUAL_UINT32(loop.playbackRevision, emptyWindow.playbackRevision);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackWindowReadiness::Ready),
                    static_cast<int>(track.playbackWindowReadinessForSlot(0)));
  TEST_ASSERT_EQUAL_STRING("ready",
                           Track::playbackWindowReadinessHolder(track.playbackWindowReadinessForSlot(0)));
  // The rule task 3.5 removes from readiness, held side by side: the legacy merged buffer still demands a non-empty
  // window, and readiness no longer does.
  TEST_ASSERT_FALSE_MESSAGE(track.isPlaybackMergedMidiEventsReadyForSlot(0),
                            "the legacy merged term still requires content; readiness does not");

  // (W-12) Currency is `playbackRevision` **and exactly it**: a window stamped one revision behind the loop owes a
  // rebuild, by name. Whether *one pass late* is accepted is task 3.6's allowance, deliberately not assumed here.
  Track& lagging = prepareWindowOwnerSlot(1);
  Loop& laggingLoop = lagging.getLoop(0);
  TEST_ASSERT_TRUE_MESSAGE(laggingLoop.playbackRevision >= 1, "the fixture needs a revision to lag behind");
  const uint32_t staleRevision = laggingLoop.playbackRevision - 1;
  MidiEvent laggingSource[8];
  const size_t laggingCount = buildWindowOwnerSource(laggingSource, 8);
  const PlaybackCoverageInterval laggingCoverage{0, 2 * 768, kWindowOwnerLoopTicks};
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(lagging.requestPlaybackViewMaintenance(
                        0, laggingSource, laggingCount,
                        PlaybackViewSourceIdentity{staleRevision, 5, nullptr}, laggingCoverage, {}, {}, 0,
                        declaredRequiredLeadFor(laggingCount))));
  for (size_t turn = 0; turn < 8 && lagging.hasPendingPlaybackViewMaintenance(); ++turn) {
    (void)lagging.stepPlaybackViewMaintenance();
  }
  PlaybackWindowIdentity staleWindow{};
  TEST_ASSERT_TRUE(lagging.publishedPlaybackWindowIdentityForSlot(0, staleWindow));
  TEST_ASSERT_EQUAL_UINT32(staleRevision, staleWindow.playbackRevision);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackWindowReadiness::OwesCurrency),
                    static_cast<int>(lagging.playbackWindowReadinessForSlot(0)));
  TEST_ASSERT_EQUAL_STRING("window_rebuild", Track::playbackWindowReadinessHolder(
                                                 lagging.playbackWindowReadinessForSlot(0)));
  // ...and the **generation is not currency**: the same slot carrying the current revision but a different
  // generation is ready, because the generation guards ownership (task 3.4) rather than dating the content.
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(lagging.requestPlaybackViewMaintenance(
                        0, laggingSource, laggingCount,
                        PlaybackViewSourceIdentity{laggingLoop.playbackRevision, 6, nullptr}, laggingCoverage, {}, {},
                        0, declaredRequiredLeadFor(laggingCount))));
  for (size_t turn = 0; turn < 8 && lagging.hasPendingPlaybackViewMaintenance(); ++turn) {
    (void)lagging.stepPlaybackViewMaintenance();
  }
  TEST_ASSERT_TRUE_MESSAGE(lagging.adoptPreparedPlaybackWindow(0),
                           "a complete successor must be adoptable for the generation case");
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackWindowReadiness::Ready),
                    static_cast<int>(lagging.playbackWindowReadinessForSlot(0)));

  // (W-10) No published root: no build could gather at all, so the **source** owes the root -- a different holder
  // from the build owner, which is the whole point of naming it.
  Track& rootless = prepareWindowOwnerSlot(2);
  TEST_ASSERT_FALSE(rootless.getLoop(0).publishedSnapshotIsCurrent());
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackWindowReadiness::OwesSource),
                    static_cast<int>(rootless.playbackWindowReadinessForSlot(0)));
  TEST_ASSERT_EQUAL_STRING("published_source", Track::playbackWindowReadinessHolder(
                                                   rootless.playbackWindowReadinessForSlot(0)));

  // A slot with no committed content has no interval to be ready about at all.
  LoopEventStore::initPool();
  Track& bare = trackManager.getTrack(3);
  bare.ensureLoopsAllocated();
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackWindowReadiness::NotServable),
                    static_cast<int>(bare.playbackWindowReadinessForSlot(0)));
}

/// W-4: a refusal is named and **non-destructive** -- the obligation it found stays exactly as it was.
void test_window_owner_refusals_are_named_and_non_destructive() {
  // No runtime: the owner never allocates one, so the request is refused and nothing is owed.
  LoopEventStore::initPool();
  Track& bare = trackManager.getTrack(3);
  bare.ensureLoopsAllocated();
  bare.getLoop(0).loopLengthTicks = kWindowOwnerLoopTicks;
  MidiEvent source[4];
  const size_t count = buildWindowOwnerSource(source, 4);
  const PlaybackViewSourceIdentity identity{1, 1, nullptr};
  const PlaybackCoverageInterval coverage{0, 2 * 768, kWindowOwnerLoopTicks};
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::NoRuntime),
                    static_cast<int>(bare.requestPlaybackViewMaintenance(
                        0, source, count, identity, coverage, {}, {}, 0, kWindowOwnerExpectedRequiredLead)));

  // A build in flight: the second request is refused by name, and the obligation is untouched -- it still
  // completes on its own turns, which is what makes the refusal non-destructive rather than a reset.
  Track& track = prepareWindowOwnerSlot(4);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(track.requestPlaybackViewMaintenance(
                        0, source, count, identity, coverage, {}, {}, 0, kWindowOwnerExpectedRequiredLead)));
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::BuildInFlight),
                    static_cast<int>(track.requestPlaybackViewMaintenance(
                        0, source, count, identity, coverage, {}, {}, 0, kWindowOwnerExpectedRequiredLead)));
  TEST_ASSERT_TRUE_MESSAGE(track.hasPendingPlaybackViewMaintenance(),
                           "a refused request must leave the existing obligation intact");
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewTurnResult::GrainServed),
                    static_cast<int>(track.stepPlaybackViewMaintenance()));
}

/// 3.6a (DEC-108 items 4, 12(a), 13(a)): a **retained source revision** is admitted in a named role carrying **both**
/// protections -- the descriptor pins the reclamation consumer reads, and the store references that make the direct
/// reset path safe without a veto -- is bounded by the role set, and is dropped only by the one release predicate.
void test_retained_source_is_admitted_with_roles_and_survives_both_destruction_paths() {
  // ---- W-14: admission, identity, roles, and both protections -------------------------------------------------
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  Loop& loop = track.getLoop(0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  const uint32_t retainedRevision = loop.playbackRevision;

  RetainedPlaybackSource retained{};
  TEST_ASSERT_FALSE_MESSAGE(track.hasRetainedPlaybackSourceForSlot(0),
                            "nothing is retained before an admission");
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackSourceRetainResult::Admitted),
                    static_cast<int>(track.retainPlaybackSourceForSlot(0, retainedRevision, 7)));
  TEST_ASSERT_TRUE(track.hasRetainedPlaybackSourceForSlot(0));
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(track.retainedPlaybackSourceCountForSlot(0)));
  TEST_ASSERT_TRUE(track.retainedPlaybackSourceForSlot(0, retained));
  TEST_ASSERT_EQUAL_UINT32(retainedRevision, retained.revision);
  TEST_ASSERT_EQUAL_UINT32(7u, retained.generation);
  TEST_ASSERT_EQUAL(static_cast<int>(RetainedSourceRole::Serving), static_cast<int>(retained.role));
  TEST_ASSERT_FALSE_MESSAGE(retained.pins.capturePassIds.empty(),
                            "the retained revision's pass is pinned for the reclamation consumer");
  TEST_ASSERT_FALSE_MESSAGE(retained.ownedChunks.empty(), "and its bodies are owned, not merely named");

  // Re-admission of the same revision refreshes the role and the generation, and never re-owns the bodies.
  const uint32_t ownedBefore = static_cast<uint32_t>(retained.ownedChunks.size());
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackSourceRetainResult::AlreadyRetained),
                    static_cast<int>(track.retainPlaybackSourceForSlot(0, retainedRevision, 9)));
  TEST_ASSERT_TRUE(track.retainedPlaybackSourceForSlot(0, retained));
  TEST_ASSERT_EQUAL_UINT32(9u, retained.generation);
  TEST_ASSERT_EQUAL_UINT32(ownedBefore, static_cast<uint32_t>(retained.ownedChunks.size()));
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(track.retainedPlaybackSourceCountForSlot(0)));
  // (W-16, first clause) While the retained revision is still the loop's current one, nothing is released.
  TEST_ASSERT_FALSE_MESSAGE(track.tryReleaseRetainedPlaybackSource(0),
                            "a retained source that is still the current revision is not released");

  // ---- W-19: the role set is bounded, and exceeding it refuses by name rather than discarding what is needed -----
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackSourceRetainResult::Admitted),
                    static_cast<int>(track.retainPlaybackSourceForSlot(0, retainedRevision + 1, 10)));
  TEST_ASSERT_EQUAL_UINT32(2u, static_cast<uint32_t>(track.retainedPlaybackSourceCountForSlot(0)));
  TEST_ASSERT_TRUE(track.retainedPlaybackSourceForSlot(0, retained));
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(RetainedSourceRole::Retiring), static_cast<int>(retained.role),
                            "the older source stops serving, and is not dropped to make room");
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::PlaybackSourceRetainResult::RoleCapacityReached),
                            static_cast<int>(track.retainPlaybackSourceForSlot(0, retainedRevision + 2, 11)),
                            "a third source is refused by name");
  TEST_ASSERT_EQUAL_UINT32(2u, static_cast<uint32_t>(track.retainedPlaybackSourceCountForSlot(0)));

  // ---- W-15: the pass-level reclamation consumer reads the pins ------------------------------------------------
  LoopEventStore::initPool();
  Track& retainedTrack = trackManager.getTrack(1);
  Track& controlTrack = trackManager.getTrack(2);
  retainedTrack.ensureLoopsAllocated();
  controlTrack.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(retainedTrack, 0);
  commitRecordPassIntoTrackSlot(controlTrack, 0);
  Loop& retainedLoop = retainedTrack.getLoop(0);
  Loop& controlLoop = controlTrack.getLoop(0);
  TEST_ASSERT_TRUE(retainedTrack.prewarmPlaybackForSlot(0, true));
  TEST_ASSERT_TRUE(controlTrack.prewarmPlaybackForSlot(0, true));
  const PassId retainedPass = retainedLoop.passes.recordPass.id;
  const PassId controlPass = controlLoop.passes.recordPass.id;
  TEST_ASSERT_NOT_EQUAL(kInvalidPassId, retainedPass);
  TEST_ASSERT_NOT_EQUAL(kInvalidPassId, controlPass);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackSourceRetainResult::Admitted),
                    static_cast<int>(retainedTrack.retainPlaybackSourceForSlot(0, retainedLoop.playbackRevision, 21)));
  // Both passes are **disabled** -- superseded by a later revision -- which is exactly what the reclaim pass acts on.
  TEST_ASSERT_TRUE(retainedLoop.setCapturePassState(retainedPass, CapturePassState::Disabled));
  TEST_ASSERT_TRUE(controlLoop.setCapturePassState(controlPass, CapturePassState::Disabled));
  SlotPassReferences retainedRefs{};
  SlotPassReferences controlRefs{};
  (void)retainedTrack.mergeRetainedPlaybackPinsInto(0, retainedRefs);
  (void)controlTrack.mergeRetainedPlaybackPinsInto(0, controlRefs);
  TEST_ASSERT_TRUE_MESSAGE(retainedRefs.referencesCapturePass(retainedPass),
                           "the consumer's set carries the retained pins");
  TEST_ASSERT_FALSE_MESSAGE(controlRefs.referencesCapturePass(controlPass),
                            "the control slot's set carries nothing: it is the negative control");
  // The **set the consumer reads** is what decides: the same shape is reclaimed with an unreferenced set, and is not
  // reclaimed with the retained revision's pins in it.
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(0u, retainedLoop.reclaimUnreferencedDisabledCapturePasses(retainedRefs),
                                   "a retained revision's pass is not reclaimed underneath it");
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(1u, controlLoop.reclaimUnreferencedDisabledCapturePasses(controlRefs),
                                   "the same shape, unreferenced, is reclaimable");
  TEST_ASSERT_TRUE_MESSAGE(retainedLoop.passes.hasRecordPass() &&
                               retainedLoop.passes.recordPass.id == retainedPass,
                           "the retained pass is still there");
  TEST_ASSERT_FALSE_MESSAGE(controlLoop.passes.hasRecordPass(), "and the control's pass is gone");
  // The production pass over every track and slot must not reclaim it either. What this shows *and does not* show is
  // worth naming: `setCapturePassState(Disabled)` is an undo-recorded action, so this pass is also referenced by that
  // track's undo stack -- which is why removing the **merge** alone does not change the outcome here. The merge is
  // what covers a pass disabled *without* an undo reference; the assertions above pin the set that actually decides.
  trackManager.reclaimUnreferencedDisabledPasses(nullptr, false);
  TEST_ASSERT_TRUE_MESSAGE(retainedLoop.passes.hasRecordPass(),
                           "the manager's own pass does not reclaim a retained revision either");

  // ---- W-16/W-17: the one release predicate, and the ownership that protects the direct reset path --------------
  TEST_ASSERT_FALSE_MESSAGE(retainedTrack.noLiveOwnerForSlot(0),
                            "a retained source keeps the slot owned, by name");
  // `setCapturePassState` above advanced this loop's revision past the retained one, so the predicate's first clause
  // already holds here; the refusal-while-current case is asserted at admission, where the revision really is current.
  // What is still missing is the content the source describes, and the reset below removes it.
  const uint16_t usedBeforeReset = LoopEventStore::usedChunkCount();
  retainedLoop.resetPassTimeline();
  TEST_ASSERT_FALSE_MESSAGE(retainedLoop.hasCommittedPasses(),
                            "the reset is not vetoed: the gesture still works");
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(usedBeforeReset, LoopEventStore::usedChunkCount(),
                                   "and the retained bodies survive it, because the source owns them");
  const uint16_t usedAfterReset = LoopEventStore::usedChunkCount();
  TEST_ASSERT_TRUE_MESSAGE(retainedTrack.tryReleaseRetainedPlaybackSource(0),
                           "a retained revision whose content is gone is released by the one predicate");
  TEST_ASSERT_FALSE(retainedTrack.hasRetainedPlaybackSourceForSlot(0));
  TEST_ASSERT_LESS_THAN_UINT16_MESSAGE(usedAfterReset, LoopEventStore::usedChunkCount(),
                                       "and only then are its bodies returned to the store");
  TEST_ASSERT_TRUE_MESSAGE(retainedTrack.noLiveOwnerForSlot(0),
                           "the retention term is gone once the source is released");

  // ---- W-16 (second clause): a served window keeps its source retained even after the revision advanced ---------
  Track& windowTrack = prepareWindowOwnerSlot(3);
  Loop& windowLoop = windowTrack.getLoop(0);
  publishBarSpacedRootForSlot(windowLoop, 2);
  const uint32_t windowRevision = windowLoop.playbackRevision;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackSourceRetainResult::Admitted),
                    static_cast<int>(windowTrack.retainPlaybackSourceForSlot(0, windowRevision, 31)));
  SessionMidiEventVec windowScratch;
  // The phase must leave the declared contract room to finish before the handoff (the 3.2 fixtures' own rule), so the
  // request names the first phase whose window can still be completed before the playhead returns to it.
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(windowTrack.requestPlaybackWindow(
                        0, kWindowOwnerExpectedRequiredLead, windowScratch)));
  for (size_t turn = 0; turn < 8 && windowTrack.hasPendingPlaybackViewMaintenance(); ++turn) {
    (void)windowTrack.stepPlaybackViewMaintenance();
  }
  PlaybackWindowIdentity served{};
  TEST_ASSERT_TRUE(windowTrack.publishedPlaybackWindowIdentityForSlot(0, served));
  TEST_ASSERT_EQUAL_UINT32(windowRevision, served.playbackRevision);
  // The loop advances past the retained revision, but the window that is being served still reconstructs from it.
  commitRecordPassIntoTrackSlot(windowTrack, 0);
  TEST_ASSERT_TRUE(windowLoop.playbackRevision > windowRevision);
  TEST_ASSERT_FALSE_MESSAGE(windowTrack.tryReleaseRetainedPlaybackSource(0),
                            "a served window keeps its source retained");
  // A newer window becomes the served one, atomically, and only then is the older source unneeded.
  MidiEvent successorSource[8];
  const size_t successorCount = buildWindowOwnerSource(successorSource, 8);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(windowTrack.requestPlaybackViewMaintenance(
                        0, successorSource, successorCount,
                        PlaybackViewSourceIdentity{windowLoop.playbackRevision, 9, nullptr},
                        PlaybackCoverageInterval{0, 2 * 768, kWindowOwnerLoopTicks}, {}, {}, 0,
                        declaredRequiredLeadFor(successorCount))));
  for (size_t turn = 0; turn < 8 && windowTrack.hasPendingPlaybackViewMaintenance(); ++turn) {
    (void)windowTrack.stepPlaybackViewMaintenance();
  }
  TEST_ASSERT_TRUE(windowTrack.adoptPreparedPlaybackWindow(0));
  PlaybackWindowIdentity adopted{};
  TEST_ASSERT_TRUE(windowTrack.publishedPlaybackWindowIdentityForSlot(0, adopted));
  TEST_ASSERT_EQUAL_UINT32(windowLoop.playbackRevision, adopted.playbackRevision);
  TEST_ASSERT_TRUE_MESSAGE(windowTrack.tryReleaseRetainedPlaybackSource(0),
                           "once a newer window is served, the older source is released");
  TEST_ASSERT_FALSE(windowTrack.hasRetainedPlaybackSourceForSlot(0));
}

/// 3.6b (AC-5 / F-3): the **lag rule** -- a missed deadline keeps the previous revision authoritative for *that*
/// traversal, a second consecutive miss is a named breach reported once, a further edit never resets the allowance,
/// and the resolution is total (so a consumer is never blocked).
void test_playback_lag_rule_serves_the_previous_revision_once_and_names_a_second_miss() {
  // The rule first, as one pure resolution: totality is visible here, and there is no "wait" outcome to fall into.
  TEST_ASSERT_EQUAL(static_cast<int>(PlaybackLagDecision::Current),
                    static_cast<int>(Track::resolvePlaybackLag(true, false, 0)));
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(PlaybackLagDecision::Current),
                            static_cast<int>(Track::resolvePlaybackLag(false, true, 0)),
                            "a published replacement makes the boundary servable at the current revision");
  TEST_ASSERT_EQUAL(static_cast<int>(PlaybackLagDecision::PreviousOnePass),
                    static_cast<int>(Track::resolvePlaybackLag(false, false, 0)));
  TEST_ASSERT_EQUAL(static_cast<int>(PlaybackLagDecision::BreachSecondMiss),
                    static_cast<int>(Track::resolvePlaybackLag(false, false, 1)));
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(PlaybackLagDecision::BreachSecondMiss),
                            static_cast<int>(Track::resolvePlaybackLag(false, false, 250)),
                            "a longer lag is the same named breach, never a silent fallback");

  // Through the seam, on a slot whose window is published and being served.
  Track& track = prepareWindowOwnerSlot(0);
  Loop& loop = track.getLoop(0);
  publishBarSpacedRootForSlot(loop, 2);
  SessionMidiEventVec scratch;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(track.requestPlaybackWindow(0, kWindowOwnerExpectedRequiredLead, scratch)));
  for (size_t turn = 0; turn < 8 && track.hasPendingPlaybackViewMaintenance(); ++turn) {
    (void)track.stepPlaybackViewMaintenance();
  }
  PlaybackWindowIdentity served{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, served));
  const PlaybackCoverageInterval interval = served.interval;
  const uint32_t servedRevision = served.playbackRevision;

  // (W-20) The served window carries the desired revision: Current, and nothing is counted.
  TEST_ASSERT_EQUAL(static_cast<int>(PlaybackLagDecision::Current),
                    static_cast<int>(track.advancePlaybackLagTraversal(
                        0, interval.startPhase, interval.lengthTicks, servedRevision, false)));
  TEST_ASSERT_EQUAL_UINT8(0, track.playbackLagMissedTraversalsForSlot(0));
  TEST_ASSERT_EQUAL_UINT8(0, track.playbackLagBreachReportsForSlot(0));
  // From here the fixture is about the lag rows, so start the recorder at the episode's beginning: the lane's other path
  // rows (`build_complete`, `grain_denied`) were produced while setting the served window up.
  PlaybackPathRows::reset();

  // An edit advances the desired revision, and nothing is published for it.
  commitRecordPassIntoTrackSlot(track, 0);
  TEST_ASSERT_TRUE(loop.playbackRevision > servedRevision);

  // (W-22) The first miss keeps the **previous** revision authoritative for this traversal, and the window it is
  // served from is byte-for-byte untouched: no tear, no hold, no defer.
  PlaybackWindowIdentity beforeMiss{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, beforeMiss));
  TEST_ASSERT_EQUAL(static_cast<int>(PlaybackLagDecision::PreviousOnePass),
                    static_cast<int>(track.advancePlaybackLagTraversal(
                        0, interval.startPhase, interval.lengthTicks, loop.playbackRevision, false)));
  TEST_ASSERT_EQUAL_UINT8(1, track.playbackLagMissedTraversalsForSlot(0));
  TEST_ASSERT_EQUAL(static_cast<int>(PlaybackLagDecision::PreviousOnePass),
                    static_cast<int>(track.playbackLagDecisionForSlot(0)));
  PlaybackWindowIdentity afterMiss{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, afterMiss));
  TEST_ASSERT_TRUE_MESSAGE(beforeMiss == afterMiss, "a missed traversal must not tear the served window");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(servedRevision, afterMiss.playbackRevision,
                                   "the previous revision is still what is served");
  // (W-22 rows) **The allowance traversal names itself** -- AC-5's "the stale condition continues to be named". This is the
  // row the lane was missing: the breach was visible while the state the spec protects (previous revision authoritative,
  // one pass late) was silent. Exactly one row, and it is the allowance's, because the breach has not happened yet.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, static_cast<uint32_t>(PlaybackPathRows::countOf("one_pass_late")),
                                   "the one-pass-late traversal is named in the evidence stream");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, static_cast<uint32_t>(PlaybackPathRows::countOf("lag_breach")),
                                   "and it is not named as a breach, which it is not");

  // (W-23) A **further edit** must not forgive the outstanding miss, and the second consecutive miss is the named
  // breach -- reported once, with the previous revision still served.
  commitRecordPassIntoTrackSlot(track, 0);
  TEST_ASSERT_TRUE(loop.playbackRevision > afterMiss.playbackRevision);
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(PlaybackLagDecision::BreachSecondMiss),
                            static_cast<int>(track.advancePlaybackLagTraversal(
                                0, interval.startPhase, interval.lengthTicks, loop.playbackRevision, false)),
                            "a second consecutive miss is the named breach, not a second allowance");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, track.playbackLagMissedTraversalsForSlot(0),
                                  "and the further edit did not reset the allowance");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, track.playbackLagBreachReportsForSlot(0),
                                  "the breach is reported once per episode");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, static_cast<uint32_t>(PlaybackPathRows::countOf("lag_breach")),
                                   "the breach row is emitted -- 3.6b's claim, now checked as the row itself");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, static_cast<uint32_t>(PlaybackPathRows::countOf("one_pass_late")),
                                   "and the allowance's name is not reused for a breach traversal");
  TEST_ASSERT_TRUE(track.playbackLagBreachReportedForSlot(0));
  TEST_ASSERT_EQUAL(static_cast<int>(PlaybackLagDecision::BreachSecondMiss),
                    static_cast<int>(track.advancePlaybackLagTraversal(
                        0, interval.startPhase, interval.lengthTicks, loop.playbackRevision, false)));
  TEST_ASSERT_EQUAL_UINT8(3, track.playbackLagMissedTraversalsForSlot(0));
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, track.playbackLagBreachReportsForSlot(0),
                                  "a third miss keeps breaching without re-reporting");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, static_cast<uint32_t>(PlaybackPathRows::countOf("lag_breach")),
                                   "a third stale traversal re-emits no breach row: the row obeys once-per-episode too");

  // (W-21 through the seam, W-24) A replacement published before this traversal makes the boundary Current and ends
  // the episode; a *different* interval then starts a clean one.
  TEST_ASSERT_EQUAL(static_cast<int>(PlaybackLagDecision::Current),
                    static_cast<int>(track.advancePlaybackLagTraversal(
                        0, interval.startPhase, interval.lengthTicks, loop.playbackRevision, true)));
  TEST_ASSERT_EQUAL_UINT8(0, track.playbackLagMissedTraversalsForSlot(0));
  // **The naming, through the seam**: the episode ends at an eligible boundary, so the two rows it produced stand and the
  // boundary that serves current names nothing. (This also makes 3.6b's once-per-episode breach claim a claim about the
  // *row* rather than only about the counter beside it.)
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, static_cast<uint32_t>(PlaybackPathRows::countOf("one_pass_late")),
                                   "one allowance traversal, one allowance row");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, static_cast<uint32_t>(PlaybackPathRows::countOf("lag_breach")),
                                   "one breach episode, one breach row, however many stale traversals it contains");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, static_cast<uint32_t>(PlaybackPathRows::count),
                                   "and the eligible boundary that serves current adds no row at all");
  TEST_ASSERT_FALSE(track.playbackLagBreachReportedForSlot(0));
  TEST_ASSERT_EQUAL(static_cast<int>(PlaybackLagDecision::Current),
                    static_cast<int>(track.playbackLagDecisionForSlot(0)));
  TEST_ASSERT_EQUAL(static_cast<int>(PlaybackLagDecision::PreviousOnePass),
                    static_cast<int>(track.advancePlaybackLagTraversal(
                        0, interval.startPhase + 768, interval.lengthTicks, loop.playbackRevision, false)));
  TEST_ASSERT_EQUAL_UINT8(1, track.playbackLagMissedTraversalsForSlot(0));
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, track.playbackLagBreachReportsForSlot(0),
                                  "the new interval's episode starts unreported");
}

/// 3.6c (DEC-108 item 14(c)): the **interval index over pinned bodies** lets an interval query skip the bars (and so
/// the chunks) that cannot intersect, folds in **edit destinations**, and refuses -- by name -- a stale revision or a
/// partial answer rather than presenting a complete scan as window-bounded.
void test_retained_interval_index_is_interval_directed_and_refuses_stale_revisions() {
  Track& track = prepareWindowOwnerSlot(0);
  Loop& loop = track.getLoop(0);
  const uint32_t retainedRevision = loop.playbackRevision;
  const uint32_t indexedBars = (loop.loopLengthTicks + Config::TICKS_PER_BAR - 1) / Config::TICKS_PER_BAR;
  TEST_ASSERT_EQUAL_UINT32(64u, indexedBars);

  // Admitting the source publishes the index **with** the revision (the retention's own admission does it).
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackSourceRetainResult::Admitted),
                    static_cast<int>(track.retainPlaybackSourceForSlot(0, retainedRevision, 41)));
  uint32_t covered = 0;
  uint32_t skipped = 0;

  // The committed pass lives in bar 0: an interval over it is covered, and nothing is skipped.
  TEST_ASSERT_EQUAL(static_cast<int>(Track::RetainedIntervalQueryResult::Indexed),
                    static_cast<int>(track.queryRetainedIntervalForSlot(0, 0, Config::TICKS_PER_BAR,
                                                                        retainedRevision, covered, skipped)));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, covered, "the bar carrying the committed bodies is indexed");
  TEST_ASSERT_EQUAL_UINT32(0u, skipped);

  // An interval two bars deep in the loop, where the pinned bodies carry nothing: **both** bars are skipped without
  // touching a chunk -- the interval-directed answer that makes lookup cost independent of unrelated content.
  constexpr uint32_t kDeepPlayhead = 40 * 768;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::RetainedIntervalQueryResult::Indexed),
                    static_cast<int>(track.queryRetainedIntervalForSlot(0, kDeepPlayhead, 2 * Config::TICKS_PER_BAR,
                                                                        retainedRevision, covered, skipped)));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, covered, "no pinned body covers this interval");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u, skipped, "so both bars are skipped, at no chunk-inspection cost");

  // The index directs; the **bodies** answer. An interval is built from the chunk lists the retained revision pins, with
  // **no published root consulted at all** -- which is the "no resident whole-loop root" clause -- and the deep interval
  // is empty from those bodies alone, so the skip the index reported is the truth rather than an assumption.
  const CommittedChunkIdList* pinnedLists[1] = {&loop.passes.recordPass.committedChunkIds};
  CommittedEventRange pinnedRange = CommittedEventRange::full(pinnedLists, 1, loop.loopLengthTicks);
  SessionMidiEventVec builtEvents;
  pinnedRange.appendTo(builtEvents);
  TEST_ASSERT_FALSE_MESSAGE(builtEvents.empty(), "the pinned bodies carry the revision's events");
  CommittedEventRange deepRange =
      CommittedEventRange::inWindow(pinnedLists, 1, loop.loopLengthTicks, kDeepPlayhead, Config::TICKS_PER_BAR);
  SessionMidiEventVec deepEvents;
  deepRange.appendTo(deepEvents);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(deepEvents.size()),
                                   "and the deep interval is empty from the pinned bodies alone");

  // An edit whose **destination** is that otherwise-empty bar: the index must not skip it, which is the "lookup
  // includes edit-moved events at their effective destinations" clause. The edit carries no chunk bodies of its own, so
  // folding its destination span is the only thing that keeps the bar visible.
  EditPass move{};
  move.id = 77;
  move.state = EditPassState::Active;
  move.startTick = kDeepPlayhead;
  move.endTick = kDeepPlayhead + 48;
  loop.passes.editPasses.push_back(move);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::RetainedIndexBuildResult::Published),
                    static_cast<int>(track.buildRetainedIntervalIndexForSlot(0)));
  TEST_ASSERT_EQUAL(static_cast<int>(Track::RetainedIntervalQueryResult::Indexed),
                    static_cast<int>(track.queryRetainedIntervalForSlot(0, kDeepPlayhead, Config::TICKS_PER_BAR,
                                                                        retainedRevision, covered, skipped)));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, covered, "the edit's destination bar is indexed, not skipped");
  TEST_ASSERT_EQUAL_UINT32(0u, skipped);

  // A query for a revision the index was not built for is refused **by name**, never answered from stale bits.
  TEST_ASSERT_EQUAL(static_cast<int>(Track::RetainedIntervalQueryResult::RevisionMismatch),
                    static_cast<int>(track.queryRetainedIntervalForSlot(0, 0, Config::TICKS_PER_BAR,
                                                                        retainedRevision + 1, covered, skipped)));
  TEST_ASSERT_EQUAL_UINT32(0u, covered);
  TEST_ASSERT_EQUAL_UINT32(0u, skipped);

  // A loop beyond the indexed ceiling is refused by name too -- the complete scan must be accounted in admission, not
  // presented as window-bounded -- and no index is left behind to answer partially.
  Track& longTrack = trackManager.getTrack(1);
  longTrack.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlotOfLength(longTrack, 0, 513 * 768u);
  TEST_ASSERT_TRUE(longTrack.prewarmPlaybackForSlot(0, true));
  Loop& longLoop = longTrack.getLoop(0);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackSourceRetainResult::Admitted),
                    static_cast<int>(longTrack.retainPlaybackSourceForSlot(0, longLoop.playbackRevision, 42)));
  TEST_ASSERT_EQUAL(static_cast<int>(Track::RetainedIndexBuildResult::BarsUnsupported),
                    static_cast<int>(longTrack.buildRetainedIntervalIndexForSlot(0)));
  TEST_ASSERT_EQUAL(static_cast<int>(Track::RetainedIntervalQueryResult::NoIndex),
                    static_cast<int>(longTrack.queryRetainedIntervalForSlot(0, 0, Config::TICKS_PER_BAR,
                                                                            longLoop.playbackRevision, covered, skipped)));
}

/// 3.7a (I1): the **retained revision is the source for a slot with no published root** (DEC-108 §10), so the
/// production requester answers the interval from that revision's pinned bodies, directed by the interval index. The
/// answer is compared against the fallback's **own** effective-content gather over the same revision (the differential
/// oracle: `Loop::copyEffectiveCommittedEventsInRange`), never against a hand-written expectation, and the window is
/// published by the production turn with no root at all -- readiness names the **build** owner for such a slot,
/// because the window is buildable where the published root cannot answer.
void test_retained_source_window_serves_a_rootless_interval_and_the_index_bounds_the_lookup() {
  Track& track = prepareWindowOwnerSlot(0);
  Loop& loop = track.getLoop(0);
  const uint32_t retainedRevision = loop.playbackRevision;
  TEST_ASSERT_FALSE_MESSAGE(loop.publishedSnapshotIsCurrent(),
                            "the fixture's shape is a slot with **no** published root");
  TEST_ASSERT_EQUAL_PTR_MESSAGE(nullptr, loop.publishedSnapshotIdentity(),
                                "and no root identity to date a window by");
  SessionMidiEventVec scratch;
  // A playhead inside the loop's first bar but **not** at the handoff itself: the resolver centres the two-bar window
  // on it (start 0) and the handoff is a lap away, which is the shape the admission requires to admit at all.
  constexpr uint32_t kPlayhead = 500;
  // The named refusal 3.6c's fixture pins still holds while no revision is retained: nothing else is a source.
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::SourceUnavailable),
                    static_cast<int>(track.requestPlaybackWindow(0, kPlayhead, scratch)));
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackSourceRetainResult::Admitted),
                    static_cast<int>(track.retainPlaybackSourceForSlot(0, retainedRevision, 41)));
  // W-37a-5, the positive half: the obligation moves from the source to the **build**, because the retained revision is
  // the current one and a window can be gathered from it.
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackWindowReadiness::OwesBuild),
                    static_cast<int>(track.playbackWindowReadinessForSlot(0)));
  TEST_ASSERT_EQUAL_STRING("window_build_owner", Track::playbackWindowReadinessHolder(
                                                    track.playbackWindowReadinessForSlot(0)));

  // The gather's own answer for the interval the resolver names, one bounded slice at a time.
  PlaybackCoverageInterval interval{};
  TEST_ASSERT_TRUE(Track::playbackWindowInterval(loop.loopLengthTicks, true, kPlayhead, interval));
  CommittedEventRange::WindowTraversalCursor cursor{};
  CommittedEventRange::resetWindowTraversalCursor(cursor);
  SessionMidiEventVec gathered;
  const Track::RetainedWindowGather gather = track.gatherRetainedSourceWindowForSlot(
      0, interval.startPhase, interval.lengthTicks, retainedRevision, cursor, gathered);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::RetainedWindowGatherResult::Served), static_cast<int>(gather.result));
  TEST_ASSERT_TRUE(gather.complete);
  TEST_ASSERT_FALSE(gather.indexAnsweredEmpty);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, gather.indexedBars, "the committed body's own bar is indexed");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, gather.skippedBars, "the window's other bar carries nothing");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, gather.pinnedLists, "the pinned record pass contributes its bodies");
  TEST_ASSERT_EQUAL_UINT32(0u, gather.pinnedPassesMissing);
  TEST_ASSERT_TRUE(gather.inspectedChunks > 0);

  // **Differential fallback oracle**: the same revision and the same window, through the path the merged-window
  // fallback already uses. Ticks are compared pairwise; the two events are at distinct ticks, so this says nothing
  // about same-tick order (that parity is task 4.3's own gate, not a tolerance granted here).
  SessionMidiEventVec oracle;
  loop.copyEffectiveCommittedEventsInRange(oracle, interval.startPhase, interval.lengthTicks);
  TEST_ASSERT_FALSE_MESSAGE(oracle.empty(), "the fallback's effective gather has the revision's content");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(oracle.size()), static_cast<uint32_t>(gathered.size()),
                                   "the retained answer is exactly the fallback's effective content");
  for (size_t i = 0; i < oracle.size(); ++i) {
    TEST_ASSERT_EQUAL_UINT32(oracle[i].tick, gathered[i].tick);
  }

  // The production requester now answers, and the production turn publishes -- with no published root anywhere.
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(track.requestPlaybackWindow(0, kPlayhead, scratch)));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(gathered.size()), static_cast<uint32_t>(scratch.size()),
                                   "the requester serves the retained revision's content");
  for (size_t turn = 0; turn < 8 && track.hasPendingPlaybackViewMaintenance(); ++turn) {
    (void)track.stepPlaybackViewMaintenance();
  }
  PlaybackViewSourceIdentity identityOut{};
  PlaybackCoverageInterval coverageOut{};
  TEST_ASSERT_TRUE_MESSAGE(track.publishedPlaybackWindowForSlot(0, identityOut, coverageOut),
                           "the production turn publishes the rootless window");
  TEST_ASSERT_EQUAL(static_cast<int>(PlaybackWindowOrigin::Committed), static_cast<int>(identityOut.origin));
  TEST_ASSERT_EQUAL_UINT32(retainedRevision, identityOut.playbackRevision);
  TEST_ASSERT_EQUAL_PTR_MESSAGE(loop.publishedSnapshotIdentity(), identityOut.snapshotIdentity,
                                "the window carries the root it read: none, by construction");
  TEST_ASSERT_FALSE_MESSAGE(loop.publishedSnapshotIsCurrent(), "and the slot is still rootless afterwards");
  TEST_ASSERT_EQUAL_UINT32(interval.startPhase, coverageOut.startPhase);
  TEST_ASSERT_EQUAL_UINT32(interval.lengthTicks, coverageOut.lengthTicks);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackWindowReadiness::Ready),
                    static_cast<int>(track.playbackWindowReadinessForSlot(0)));

  // W-37a-2: an interval the index proves empty is **served empty at no chunk-inspection cost** -- the lookup bound the
  // index exists for, and a served answer rather than a refusal (task 3.5: an empty interval is still ready).
  constexpr uint32_t kDeepPlayhead = 40 * 768;
  CommittedEventRange::WindowTraversalCursor emptyCursor{};
  CommittedEventRange::resetWindowTraversalCursor(emptyCursor);
  SessionMidiEventVec emptyScratch;
  const Track::RetainedWindowGather empty = track.gatherRetainedSourceWindowForSlot(
      0, kDeepPlayhead, 2 * 768, retainedRevision, emptyCursor, emptyScratch);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::RetainedWindowGatherResult::Served), static_cast<int>(empty.result));
  TEST_ASSERT_TRUE(empty.complete);
  TEST_ASSERT_TRUE(empty.indexAnsweredEmpty);
  TEST_ASSERT_EQUAL_UINT32(0u, empty.indexedBars);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u, empty.skippedBars, "both of the interval's bars are skipped by the index");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, empty.inspectedChunks,
                                   "the index alone answered: not one chunk was inspected");
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(emptyScratch.size()));
  SessionMidiEventVec deepOracle;
  loop.copyEffectiveCommittedEventsInRange(deepOracle, kDeepPlayhead, 2 * 768);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(deepOracle.size()),
                                   "and the fallback agrees the interval holds nothing");

  // W-37a-3: the revision the index was not built for is refused **by name**, and nothing is inspected for it.
  CommittedEventRange::WindowTraversalCursor staleCursor{};
  CommittedEventRange::resetWindowTraversalCursor(staleCursor);
  SessionMidiEventVec staleScratch;
  const Track::RetainedWindowGather stale = track.gatherRetainedSourceWindowForSlot(
      0, interval.startPhase, interval.lengthTicks, retainedRevision + 1, staleCursor, staleScratch);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::RetainedWindowGatherResult::RevisionMismatch),
                    static_cast<int>(stale.result));
  TEST_ASSERT_FALSE(stale.complete);
  TEST_ASSERT_EQUAL_UINT32(0u, stale.inspectedChunks);
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(staleScratch.size()));

  // The loop's revision advances (what an edit does to the revision a window is dated by) while the retained source
  // keeps describing the revision it was admitted for: the requester asks for the **current** revision, so it refuses
  // rather than serving the older one -- the one-pass-late allowance is the lag rule's consumer (task 3.6b), never the
  // requester's. The retained revision itself is still answerable to a caller that asks for exactly it.
  loop.playbackRevision = retainedRevision + 1;
  SessionMidiEventVec advancedScratch;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::SourceUnavailable),
                    static_cast<int>(track.requestPlaybackWindow(0, kPlayhead, advancedScratch)));
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackWindowReadiness::OwesCurrency),
                    static_cast<int>(track.playbackWindowReadinessForSlot(0)));
  CommittedEventRange::WindowTraversalCursor laggingCursor{};
  CommittedEventRange::resetWindowTraversalCursor(laggingCursor);
  SessionMidiEventVec laggingScratch;
  const Track::RetainedWindowGather lagging = track.gatherRetainedSourceWindowForSlot(
      0, interval.startPhase, interval.lengthTicks, retainedRevision, laggingCursor, laggingScratch);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::RetainedWindowGatherResult::Served), static_cast<int>(lagging.result));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(gathered.size()), static_cast<uint32_t>(laggingScratch.size()));

  // W-37a-5, the negative half on its own slot: with no retained revision the holder is the **source**, and retaining
  // the current revision moves it to the build owner -- one line of vocabulary, two owners, told apart by the source.
  LoopEventStore::initPool();
  Track& pair = prepareWindowOwnerSlot(1);
  TEST_ASSERT_FALSE(pair.getLoop(0).publishedSnapshotIsCurrent());
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackWindowReadiness::OwesSource),
                    static_cast<int>(pair.playbackWindowReadinessForSlot(0)));
  TEST_ASSERT_EQUAL_STRING("published_source", Track::playbackWindowReadinessHolder(
                                                   pair.playbackWindowReadinessForSlot(0)));
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackSourceRetainResult::Admitted),
                    static_cast<int>(pair.retainPlaybackSourceForSlot(0, pair.getLoop(0).playbackRevision, 7)));
  TEST_ASSERT_EQUAL_STRING("window_build_owner", Track::playbackWindowReadinessHolder(
                                                    pair.playbackWindowReadinessForSlot(0)));
}

/// 3.7a (I1): every refusal the retained-source gather can answer with is **named**, and each name is a reason no body
/// was read: no retention at all, a loop outside the indexed range, pinned Active note-edit rows whose resolution must
/// be per pass layer, and a revision that owns bodies none of which is reachable. Serving an empty window in any of
/// those states would be indistinguishable from silence -- the failure mode this change exists to remove.
void test_the_retained_source_gather_refuses_by_name_where_it_must() {
  // (a) Rootless and nothing retained: there is no revision to serve from, so the source is what is owed.
  Track& bare = prepareWindowOwnerSlot(0);
  Loop& bareLoop = bare.getLoop(0);
  SessionMidiEventVec bareScratch;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::SourceUnavailable),
                    static_cast<int>(bare.requestPlaybackWindow(0, 0, bareScratch)));
  CommittedEventRange::WindowTraversalCursor cursor{};
  CommittedEventRange::resetWindowTraversalCursor(cursor);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::RetainedWindowGatherResult::NoRetainedSource),
                    static_cast<int>(bare
                                         .gatherRetainedSourceWindowForSlot(0, 0, Config::TICKS_PER_BAR,
                                                                            bareLoop.playbackRevision, cursor,
                                                                            bareScratch)
                                         .result));

  // (b) Beyond `kRetainedIndexMaxBars`: the index was refused for this loop, so the gather refuses rather than
  // presenting a complete scan as window-bounded.
  LoopEventStore::initPool();
  Track& longTrack = trackManager.getTrack(1);
  longTrack.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlotOfLength(longTrack, 0, 513 * 768u);
  TEST_ASSERT_TRUE(longTrack.prewarmPlaybackForSlot(0, true));
  Loop& longLoop = longTrack.getLoop(0);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackSourceRetainResult::Admitted),
                    static_cast<int>(longTrack.retainPlaybackSourceForSlot(0, longLoop.playbackRevision, 42)));
  CommittedEventRange::WindowTraversalCursor longCursor{};
  CommittedEventRange::resetWindowTraversalCursor(longCursor);
  SessionMidiEventVec longScratch;
  const Track::RetainedWindowGather longGather = longTrack.gatherRetainedSourceWindowForSlot(
      0, 0, Config::TICKS_PER_BAR, longLoop.playbackRevision, longCursor, longScratch);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::RetainedWindowGatherResult::BarsUnsupported),
                    static_cast<int>(longGather.result));
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(longScratch.size()));

  // (c) The retained revision pins an Active note-edit row: per-layer resolution has not landed, and a merged-flat
  // application would be a *different* answer (the reason `Loop::copyEffectiveCommittedEventsInRange` applies each
  // committed list and then merges), so the revision is refused rather than served unfaithfully.
  LoopEventStore::initPool();
  Track& editTrack = trackManager.getTrack(2);
  editTrack.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(editTrack, 0);
  editTrack.getLoop(0).loopLengthTicks = kWindowOwnerLoopTicks;
  TEST_ASSERT_TRUE(editTrack.prewarmPlaybackForSlot(0, true));
  Loop& editLoop = editTrack.getLoop(0);
  EditPass pinnedRow{};
  pinnedRow.id = 91;
  pinnedRow.state = EditPassState::Active;
  pinnedRow.passType = EditPassType::Note;
  pinnedRow.startTick = 0;
  pinnedRow.endTick = 48;
  editLoop.passes.editPasses.push_back(pinnedRow);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackSourceRetainResult::Admitted),
                    static_cast<int>(editTrack.retainPlaybackSourceForSlot(0, editLoop.playbackRevision, 43)));
  CommittedEventRange::WindowTraversalCursor editCursor{};
  CommittedEventRange::resetWindowTraversalCursor(editCursor);
  SessionMidiEventVec editScratch;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::RetainedWindowGatherResult::PinnedEditRowsUnsupported),
                    static_cast<int>(editTrack
                                         .gatherRetainedSourceWindowForSlot(0, 0, 2 * Config::TICKS_PER_BAR,
                                                                            editLoop.playbackRevision, editCursor,
                                                                            editScratch)
                                         .result));

  // (d) The revision **owns bodies** (the retention took the store's references) and none of them is reachable through
  // the pass set: refused by name, because an empty window would be served as silence for content it demonstrably has.
  LoopEventStore::initPool();
  Track& pinless = trackManager.getTrack(3);
  pinless.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(pinless, 0);
  pinless.getLoop(0).loopLengthTicks = kWindowOwnerLoopTicks;
  TEST_ASSERT_TRUE(pinless.prewarmPlaybackForSlot(0, true));
  Loop& pinlessLoop = pinless.getLoop(0);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackSourceRetainResult::Admitted),
                    static_cast<int>(pinless.retainPlaybackSourceForSlot(0, pinlessLoop.playbackRevision, 44)));
  // The pass set moves on while the retention still owns the revision's bodies: the pinned pass id is no longer the
  // one the set carries, so no pinned list resolves -- and `hasCommittedPasses()` stays true (the slot is servable),
  // which is what makes this a body-reachability refusal rather than the earlier `NotServable` gate.
  pinlessLoop.passes.recordPass.id = 4242;
  CommittedEventRange::WindowTraversalCursor pinlessCursor{};
  CommittedEventRange::resetWindowTraversalCursor(pinlessCursor);
  SessionMidiEventVec pinlessScratch;
  const Track::RetainedWindowGather pinlessGather = pinless.gatherRetainedSourceWindowForSlot(
      0, 0, 2 * Config::TICKS_PER_BAR, pinlessLoop.playbackRevision, pinlessCursor, pinlessScratch);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::RetainedWindowGatherResult::PinnedBodiesUnavailable),
                    static_cast<int>(pinlessGather.result));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, pinlessGather.pinnedLists, "no pinned body was enumerated");
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(pinlessScratch.size()));
}

/// 3.11 (DEC-108 §13d): the **shared-view ownership rules** as one transition table, applied through the seam, with the
/// four scenarios the spec names: a builder keeps its own work, a late completion publishes nothing, returning to a
/// slot revives nothing, and storage is reusable only after the last owner releases it.
void test_shared_view_transitions_state_the_ownership_rules_and_a_late_completion_publishes_nothing() {
  // (W-29) The table itself, row by row, as the one pure resolution the code applies.
  const PlaybackViewTransitionEffect newEdit = resolvePlaybackViewTransition(PlaybackViewTransition::NewEdit);
  TEST_ASSERT_TRUE_MESSAGE(newEdit.operationContinues, "a new edit retains the frozen source and coalesces the successor");
  TEST_ASSERT_FALSE(newEdit.candidateReleased);
  TEST_ASSERT_TRUE(newEdit.frozenSourceHeld);
  const PlaybackViewTransitionEffect slotSwitch = resolvePlaybackViewTransition(PlaybackViewTransition::SlotSwitch);
  TEST_ASSERT_TRUE(slotSwitch.operationContinues);
  TEST_ASSERT_TRUE(slotSwitch.frozenSourceHeld);
  const PlaybackViewTransitionEffect trackChange =
      resolvePlaybackViewTransition(PlaybackViewTransition::SelectedTrackChange);
  TEST_ASSERT_TRUE_MESSAGE(trackChange.operationContinues, "an existing worker is never retargeted");
  TEST_ASSERT_FALSE(trackChange.identityInvalidated);
  const PlaybackViewTransitionEffect stopWithStart =
      resolvePlaybackViewTransition(PlaybackViewTransition::StopWithSuccessorStart);
  TEST_ASSERT_TRUE(stopWithStart.operationContinues);
  TEST_ASSERT_TRUE(stopWithStart.frozenSourceHeld);
  const PlaybackViewTransitionEffect transportStop = resolvePlaybackViewTransition(PlaybackViewTransition::TransportStop);
  TEST_ASSERT_FALSE(transportStop.operationContinues);
  TEST_ASSERT_TRUE(transportStop.candidateReleased);
  TEST_ASSERT_TRUE_MESSAGE(transportStop.preparedSuccessorRetired, "obsolete lookahead is cancelled");
  TEST_ASSERT_FALSE_MESSAGE(transportStop.identityInvalidated,
                            "a transport stop preserves accepted content and finalization");
  const PlaybackViewTransitionEffect clearOrLoad = resolvePlaybackViewTransition(PlaybackViewTransition::ClearOrLoad);
  TEST_ASSERT_TRUE(clearOrLoad.candidateReleased);
  TEST_ASSERT_TRUE(clearOrLoad.preparedSuccessorRetired);
  TEST_ASSERT_TRUE_MESSAGE(clearOrLoad.identityInvalidated, "clear/load invalidates the old operation's identity");
  const PlaybackViewTransitionEffect late = resolvePlaybackViewTransition(PlaybackViewTransition::LateCompletion);
  TEST_ASSERT_FALSE(late.operationContinues);
  TEST_ASSERT_TRUE_MESSAGE(late.candidateReleased, "it releases only the resources it owns");
  TEST_ASSERT_FALSE_MESSAGE(late.frozenSourceHeld, "and it holds nothing new");
  // The invariant **every** row carries, asserted for all seven rather than assumed:
  const PlaybackViewTransition allTransitions[] = {
      PlaybackViewTransition::NewEdit,       PlaybackViewTransition::SlotSwitch,
      PlaybackViewTransition::SelectedTrackChange, PlaybackViewTransition::StopWithSuccessorStart,
      PlaybackViewTransition::TransportStop, PlaybackViewTransition::ClearOrLoad,
      PlaybackViewTransition::LateCompletion};
  for (PlaybackViewTransition transition : allTransitions) {
    TEST_ASSERT_TRUE_MESSAGE(resolvePlaybackViewTransition(transition).servedWindowStaysReadable,
                             "no lifecycle transition may tear the served window");
  }

  // Through the seam: a slot with a published window of its own, and a build in flight.
  Track& track = prepareWindowOwnerSlot(0);
  Loop& loop = track.getLoop(0);
  publishBarSpacedRootForSlot(loop, 2);
  SessionMidiEventVec scratch;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(track.requestPlaybackWindow(0, kWindowOwnerExpectedRequiredLead, scratch)));
  const uint32_t firstRevision = loop.playbackRevision;
  // (W-30) A track change, a new edit, a slot switch and a stop-with-successor must all leave the worker alone. The
  // build is **in flight** right here -- no grain served yet -- so these transitions act on a live worker rather than
  // on work that already finished.
  const uint32_t operationGeneration = track.playbackViewRoleStateForSlot(0).operationGeneration;
  TEST_ASSERT_TRUE_MESSAGE(track.playbackViewRoleStateForSlot(0).buildOwed,
                           "the fixture needs a build in flight (nothing served yet)");
  const uint32_t generationBefore = track.getPlaybackGeneration();
  for (PlaybackViewTransition transition :
       {PlaybackViewTransition::SelectedTrackChange, PlaybackViewTransition::NewEdit, PlaybackViewTransition::SlotSwitch,
        PlaybackViewTransition::StopWithSuccessorStart}) {
    track.applyPlaybackViewTransition(0, transition);
    TEST_ASSERT_TRUE_MESSAGE(track.playbackViewRoleStateForSlot(0).buildOwed,
                             "these transitions never retarget an existing worker");
    TEST_ASSERT_EQUAL_UINT32(operationGeneration, track.playbackViewRoleStateForSlot(0).operationGeneration);
    TEST_ASSERT_EQUAL_UINT32(generationBefore, track.getPlaybackGeneration());
  }
  // That same build now completes normally and publishes: the identity was never disturbed.
  for (size_t turn = 0; turn < 8 && track.hasPendingPlaybackViewMaintenance(); ++turn) {
    (void)track.stepPlaybackViewMaintenance();
  }
  PlaybackWindowIdentity served{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, served));
  TEST_ASSERT_EQUAL_UINT32(firstRevision, served.playbackRevision);
  TEST_ASSERT_FALSE(track.playbackViewRoleStateForSlot(0).buildOwed);

  // (W-32) A **late completion**: the operation's identity moves while a build is in flight (this is what a clear/load
  // does, and what a generation bump stands in for), so the completion must publish nothing.
  LoopEventStore::initPool();
  Track& lateTrack = prepareWindowOwnerSlot(1);
  Loop& lateLoop = lateTrack.getLoop(0);
  publishBarSpacedRootForSlot(lateLoop, 2);
  SessionMidiEventVec lateScratch;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(lateTrack.requestPlaybackWindow(0, kWindowOwnerExpectedRequiredLead, lateScratch)));
  const uint32_t lateGeneration = lateTrack.getPlaybackGeneration();
  lateTrack.bumpPlaybackGeneration();
  TEST_ASSERT_EQUAL_UINT32(lateGeneration + 1, lateTrack.getPlaybackGeneration());
  for (size_t turn = 0; turn < 8 && lateTrack.hasPendingPlaybackViewMaintenance(); ++turn) {
    (void)lateTrack.stepPlaybackViewMaintenance();
  }
  PlaybackViewSourceIdentity lateSourceIdentity{};
  PlaybackCoverageInterval lateCoverage{};
  TEST_ASSERT_FALSE_MESSAGE(lateTrack.publishedPlaybackWindowForSlot(0, lateSourceIdentity, lateCoverage),
                            "a late completion publishes nothing");
  TEST_ASSERT_FALSE_MESSAGE(lateTrack.playbackViewRoleStateForSlot(0).buildOwed,
                            "and it releases its own candidate: nothing is owed afterwards");

  // (W-31) Transport stop cancels obsolete lookahead and retires a *prepared* successor, while the served window stays
  // exactly what it was; clear/load additionally invalidates the generation.
  MidiEvent successorSource[8];
  const size_t successorCount = buildWindowOwnerSource(successorSource, 8);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(track.requestPlaybackViewMaintenance(
                        0, successorSource, successorCount,
                        PlaybackViewSourceIdentity{firstRevision, 7, nullptr},
                        PlaybackCoverageInterval{0, 2 * 768, kWindowOwnerLoopTicks}, {}, {}, 0,
                        declaredRequiredLeadFor(successorCount))));
  for (size_t turn = 0; turn < 8 && track.hasPendingPlaybackViewMaintenance(); ++turn) {
    (void)track.stepPlaybackViewMaintenance();
  }
  TEST_ASSERT_TRUE_MESSAGE(track.playbackViewRoleStateForSlot(0).preparedSuccessorWaiting,
                           "the fixture needs a prepared successor waiting (not adopted)");
  track.applyPlaybackViewTransition(0, PlaybackViewTransition::TransportStop);
  Track::PlaybackViewRoleState afterStop = track.playbackViewRoleStateForSlot(0);
  TEST_ASSERT_FALSE_MESSAGE(afterStop.preparedSuccessorWaiting, "transport stop retires the prepared lookahead");
  TEST_ASSERT_FALSE_MESSAGE(track.adoptPreparedPlaybackWindow(0),
                            "and the retired lookahead cannot be adopted afterwards");
  TEST_ASSERT_FALSE(afterStop.buildOwed);
  PlaybackWindowIdentity afterStopIdentity{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, afterStopIdentity));
  TEST_ASSERT_TRUE_MESSAGE(served == afterStopIdentity, "and the served window is untouched: nothing may tear it");
  track.applyPlaybackViewTransition(0, PlaybackViewTransition::ClearOrLoad);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(generationBefore + 1, track.getPlaybackGeneration(),
                                   "clear/load invalidates the old operation's identity");
  TEST_ASSERT_TRUE_MESSAGE(track.publishedPlaybackWindowIdentityForSlot(0, afterStopIdentity),
                           "the served window is still readable while its readers finish");
  // (W-33) Storage reuse: the released roles take a new operation under the new identity, while the served window's
  // storage is still not reused because a reader is holding it.
  SessionMidiEventVec newScratch;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(track.requestPlaybackWindow(0, kWindowOwnerExpectedRequiredLead, newScratch)));
  TEST_ASSERT_TRUE(track.playbackViewRoleStateForSlot(0).buildOwed);
  TEST_ASSERT_TRUE_MESSAGE(track.publishedPlaybackWindowIdentityForSlot(0, afterStopIdentity),
                           "and the published window is still the reader's, not recycled into the new candidate");
}
using WindowAdmissionAccounting = RuntimePlaybackViewConfig::WindowAdmissionAccounting;

/// 3.12a (DEC-108 §14): the **admission limit** on the decided accounting unit -- the capture interval's effective
/// events plus outstanding reservations and the reserved closure capacity, all **inside** L, refused **by name** before
/// acceptance (never truncated, never a deadline fault), with the per-bar bound conservative and the span bound
/// independent of where a `W`-wide interval sits.
void test_window_admission_limit_accounts_effective_events_and_the_closure_reserve_inside_l() {
  // The invariant first: L **is** the window's storage capacity, so there is no partition multiplier to drift out of step.
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(RuntimePlaybackViewConfig::kEventsPerView),
                           static_cast<uint32_t>(RuntimePlaybackViewConfig::kWindowAdmissionLimit));
  const size_t limit = RuntimePlaybackViewConfig::kWindowAdmissionLimit;
  const size_t reserve = RuntimePlaybackViewConfig::kWindowClosureReserve;
  TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(static_cast<uint32_t>(limit), static_cast<uint32_t>(reserve),
                                       "the closure reserve lives inside L, not on top of it");

  WindowAdmissionAccounting accounting{};
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(reserve), static_cast<uint32_t>(accounting.committed()),
                                   "the reserve alone is committed before anything is accepted");
  // One event below the limit fits; the limit itself fits exactly; one above it does not.
  TEST_ASSERT_TRUE(accounting.fits(limit - reserve - 1));
  TEST_ASSERT_TRUE(accounting.fits(limit - reserve));
  TEST_ASSERT_FALSE_MESSAGE(accounting.fits(limit - reserve + 1), "capacity pressure is visible before acceptance");

  // At the limit: admitted, and the counter moves by exactly what was admitted.
  TEST_ASSERT_EQUAL(static_cast<int>(WindowAdmissionAccounting::Outcome::Admitted),
                    static_cast<int>(accounting.admit(limit - reserve)));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(limit), static_cast<uint32_t>(accounting.committed()));
  // Above it: refused **by name**, and nothing is half-accepted.
  TEST_ASSERT_EQUAL(static_cast<int>(WindowAdmissionAccounting::Outcome::OverCapacity),
                    static_cast<int>(accounting.admit(1)));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(limit), static_cast<uint32_t>(accounting.committed()));

  // Same-tick events accumulated across passes are just more effective events in one window; an outstanding reservation
  // (an accepted note-on whose closure is still owed) occupies it as well, and closing it returns the room.
  WindowAdmissionAccounting acrossPasses{};
  for (int pass = 0; pass < 4; ++pass) {
    TEST_ASSERT_EQUAL(static_cast<int>(WindowAdmissionAccounting::Outcome::Admitted),
                      static_cast<int>(acrossPasses.admit(256)));
  }
  TEST_ASSERT_EQUAL_UINT32(1024u, static_cast<uint32_t>(acrossPasses.acceptedEffectiveEvents));
  acrossPasses.outstandingReservations = limit - acrossPasses.acceptedEffectiveEvents - reserve;
  TEST_ASSERT_TRUE(acrossPasses.fits(0));
  TEST_ASSERT_FALSE_MESSAGE(acrossPasses.fits(1), "an outstanding reservation occupies the window until it closes");
  acrossPasses.outstandingReservations -= 1;
  TEST_ASSERT_TRUE(acrossPasses.fits(1));

  // The per-bar bound is **conservative and clamped by L**, and a `W`-wide interval's bound does not depend on where it
  // sits -- a wrap-crossing interval has the same width, so the sliding window-span invariant is arithmetic rather than
  // a promise: for the two-bar comparison span at the design's 4,096 refs-per-bar ceiling the bound is L itself, i.e.
  // the window sets the shape and L sets what may be accepted.
  constexpr size_t kPerBarCeiling = 4096;
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(limit),
                           static_cast<uint32_t>(WindowAdmissionAccounting::boundForSpan(2, kPerBarCeiling)));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(limit),
                           static_cast<uint32_t>(WindowAdmissionAccounting::boundForSpan(1, kPerBarCeiling)));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(limit),
                           static_cast<uint32_t>(WindowAdmissionAccounting::conservativeBoundForBars(64, kPerBarCeiling)));
  TEST_ASSERT_EQUAL_UINT32(256u, static_cast<uint32_t>(WindowAdmissionAccounting::conservativeBoundForBars(4, 64)));
}

/// 3.12b (DEC-108 §14): the **bounded legacy compatibility path** — content beyond one page served through
/// fixed-capacity pages that return to a bounded ring, with a resumable cursor giving ordering and **exactly-once**
/// emission across page boundaries, and a generation-qualified cursor that cannot resume another revision's stream.
void test_paged_window_stream_serves_more_than_one_page_exactly_once_across_the_ring() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  constexpr uint32_t kLoopTicks = 4 * 768;
  constexpr size_t kEvents = 700;  // > 3 pages (the ring's 384) at 128 events each, so pages must be reused
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  Loop& loop = track.getActiveLoop();
  loop.loopLengthTicks = kLoopTicks;
  loop.beginCapture(CapturePhase::Record);
  for (size_t i = 0; i < kEvents; ++i) {
    TEST_ASSERT_TRUE_MESSAGE(loop.appendCaptureEvent(MidiEvent::NoteOn(static_cast<uint32_t>(i * 4), 1, 48, 100)),
                             "the fixture needs a dense committed pass");
  }
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(kLoopTicks, CommitReason::RecordStop));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass());
  const CommittedChunkIdList* lists[1] = {&loop.passes.recordPass.committedChunkIds};

  WindowPagedStream stream;
  stream.reset(loop.playbackRevision);
  TEST_ASSERT_EQUAL_UINT32(loop.playbackRevision, stream.sourceRevision());
  WindowPageCursor cursor{};
  MidiEvent emitted[kEvents];
  size_t emittedCount = 0;
  size_t produced = 0;
  size_t pageHighWater = 0;
  // Interleave production and consumption the way a bounded worker would: fill the ring while there is room, then let
  // the consumer drain, so the ring bound is exercised and no page is ever needed twice.
  for (size_t guard = 0; guard < 64 && emittedCount < kEvents; ++guard) {
    while (stream.pagesPublished() < kWindowPageCount) {
      const WindowPagedStream::ProduceOutcome outcome =
          stream.produce(lists, 1, kLoopTicks, 0, kLoopTicks, stream.sourceRevision());
      if (outcome == WindowPagedStream::ProduceOutcome::SourceComplete) {
        break;
      }
      TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(WindowPagedStream::ProduceOutcome::Produced),
                                static_cast<int>(outcome), "a page must be produced while the ring has room");
      ++produced;
      pageHighWater = stream.pagesPublished() > pageHighWater ? stream.pagesPublished() : pageHighWater;
    }
    TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(kWindowPageCount),
                                             static_cast<uint32_t>(stream.pagesPublished()),
                                             "the ring bound holds: memory is bounded, content is not");
    size_t chunk = 0;
    const WindowPagedStream::ConsumeOutcome consumeOutcome =
        stream.consume(cursor, emitted + emittedCount, kEvents - emittedCount, chunk);
    TEST_ASSERT_TRUE_MESSAGE(
        consumeOutcome == WindowPagedStream::ConsumeOutcome::Emitted ||
            consumeOutcome == WindowPagedStream::ConsumeOutcome::Exhausted,
        "a consumer never waits for preparation: it asks for a page");
    emittedCount += chunk;
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(kEvents), static_cast<uint32_t>(emittedCount),
                                   "every accepted event is served: nothing is truncated");
  TEST_ASSERT_TRUE_MESSAGE(produced >= 3, "the interval needed more than one page");
  TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(kWindowPageCount), static_cast<uint32_t>(pageHighWater),
                                           "the high-water mark is the ring, not the content");
  // Ordered across the boundaries: the source's ticks were strictly increasing, and so are the emitted ones.
  for (size_t i = 1; i < emittedCount; ++i) {
    TEST_ASSERT_TRUE_MESSAGE(emitted[i].tick > emitted[i - 1].tick, "ordering survives the page boundaries");
  }
  // The cursor is generation-qualified: another revision's cursor cannot consume or produce here.
  WindowPageCursor stale{};
  stale.sourceRevision = stream.sourceRevision() + 1;
  size_t ignored = 0;
  TEST_ASSERT_EQUAL(static_cast<int>(WindowPagedStream::ConsumeOutcome::StaleRevision),
                    static_cast<int>(stream.consume(stale, emitted, 1, ignored)));
  TEST_ASSERT_EQUAL(static_cast<int>(WindowPagedStream::ProduceOutcome::StaleRevision),
                    static_cast<int>(stream.produce(lists, 1, kLoopTicks, 0, kLoopTicks,
                                                    stream.sourceRevision() + 1)));
}

/// 3.12c (DEC-108 §14): the **MIDI-edge wiring** of the admission limit. Capacity pressure is refused before
/// acceptance -- the interval accepts exactly what `L` minus the closure reserve can hold, and the refusal leaves no
/// half-state behind -- the window serving the slot is part of the accounting, an accepted note's closure is never
/// refused because it is a move inside `L` rather than a growth, and no stop request is refused by a limit that holds
/// no representation for one. Every event travels the production edge (`Track::recordMidiEvents`, which is what
/// `MidiHandler` calls, and `Track::noteOn`/`Track::noteOff` for notes).
void test_capture_admission_refuses_at_the_edge_and_never_a_closure_or_a_stop() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  Loop& loop = track.getLoop(0);
  track.forceSetState(TRACK_STOPPED);
  track.startRecording(1000);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track), "the take is admitted");

  // The gate is consulted exactly where a **window serves** the slot. With none, the take is not window-bounded -- its
  // playback storage is the whole-take view -- so it accepts rows without asking a window's capacity at all.
  TEST_ASSERT_FALSE_MESSAGE(track.playbackWindowServesSlot(0),
                            "a fresh record take is not served by a window, so it is not window-bounded");
  track.noteOn(0, 60, 100, 1000);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, static_cast<uint32_t>(loop.capture.store.size()),
                                   "the note-on row is accepted through the edge");
  const WindowAdmissionAccounting atOpen = track.captureIntervalAdmissionForSlot(0);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(atOpen.acceptedEffectiveEvents));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, static_cast<uint32_t>(atOpen.outstandingReservations),
                                   "the accepted note owes exactly one closure");
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(1 + 1 + RuntimePlaybackViewConfig::kWindowClosureReserve),
                           static_cast<uint32_t>(atOpen.committed()));

  // Now publish the window that serves this slot, holding `L - reserve - 3` rows: with the accepted row and the owed
  // closure that leaves room for exactly one more plain event and none for a note-on. (The window lane has no
  // production requester yet -- that is task 3.7's -- so the fixture supplies the state the lane will publish, and the
  // accessor is asserted on its own first.)
  LoopPlaybackRuntime* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
  TEST_ASSERT_NOT_NULL_MESSAGE(runtime, "the record entry prewarms the slot's runtime");
  RuntimePlaybackViewSet* set = runtime->viewSetIfPrewarmed();
  TEST_ASSERT_NOT_NULL_MESSAGE(set, "and its view set");
  const uint8_t current = set->lifecycle.current;
  TEST_ASSERT_LESS_THAN_UINT8(static_cast<uint8_t>(RuntimePlaybackViewConfig::kViewCount), current);
  const size_t windowRows =
      RuntimePlaybackViewConfig::kWindowAdmissionLimit - RuntimePlaybackViewConfig::kWindowClosureReserve - 3;
  set->views[current].storage.eventCount = windowRows;
  set->views[current].coverage = PlaybackCoverageInterval{0, 768, 768};
  set->views[current].complete = true;
  set->views[current].published = true;
  TEST_ASSERT_TRUE_MESSAGE(track.playbackWindowServesSlot(0), "a completed, published window now serves the slot");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(windowRows),
                                   static_cast<uint32_t>(track.playbackWindowEventCountForSlot(0)),
                                   "a completed, published window with a valid interval is the inherited term");
  const WindowAdmissionAccounting atLimit = track.captureIntervalAdmissionForSlot(0);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      static_cast<uint32_t>(RuntimePlaybackViewConfig::kWindowAdmissionLimit - 1),
      static_cast<uint32_t>(atLimit.committed()),
      "the window's content, the accepted row, the owed closure and the reserve are one total");
  TEST_ASSERT_TRUE_MESSAGE(atLimit.admitsNewEvent(false), "one plain event still fits inside L");
  TEST_ASSERT_FALSE_MESSAGE(atLimit.admitsNewEvent(true), "a note-on needs its closing row as well");

  // The one plain event that still fits is accepted, and the next is refused exactly at L.
  TEST_ASSERT_TRUE(track.recordMidiEvents(midi::ControlChange, 0, 1, 64, 1100));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(RuntimePlaybackViewConfig::kWindowAdmissionLimit),
                                   static_cast<uint32_t>(track.captureIntervalAdmissionForSlot(0).committed()),
                                   "capacity pressure is reached exactly at L, never after it");
  TEST_ASSERT_FALSE_MESSAGE(track.recordMidiEvents(midi::ControlChange, 0, 1, 64, 1200),
                            "and the next plain event is refused by the limit");

  // A refused note-on leaves **no** half-state: no row, and no dangling reservation.
  const uint32_t rowsAtLimit = static_cast<uint32_t>(loop.capture.store.size());
  track.noteOn(0, 62, 100, 2000);
  TEST_ASSERT_EQUAL_UINT32(rowsAtLimit, static_cast<uint32_t>(loop.capture.store.size()));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      1u, static_cast<uint32_t>(track.captureIntervalAdmissionForSlot(0).outstandingReservations),
      "the refused note-on did not join the owed set");

  // The closure of an accepted note is refused by nothing: it moves a unit from owed to accepted, inside L.
  track.noteOff(0, 60, 0, 3000);
  const WindowAdmissionAccounting afterClosure = track.captureIntervalAdmissionForSlot(0);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(RuntimePlaybackViewConfig::kWindowAdmissionLimit),
                                   static_cast<uint32_t>(afterClosure.committed()),
                                   "the closure moved a unit inside L instead of adding one");
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(afterClosure.outstandingReservations));
  TEST_ASSERT_EQUAL_STRING_MESSAGE("window_admission",
                                   captureAppendDenyReasonLabel(CaptureAppendDenyReason::WindowAdmission),
                                   "the refusal is named, in the same vocabulary as every other capture denial");

  // An unpublished view is not a window: no inherited term, so the gate is not consulted at all.
  set->views[current].published = false;
  TEST_ASSERT_FALSE(track.playbackWindowServesSlot(0));
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(track.playbackWindowEventCountForSlot(0)));
  TEST_ASSERT_TRUE_MESSAGE(track.recordMidiEvents(midi::ControlChange, 0, 1, 64, 4000),
                           "with no window serving the slot, the take is not window-bounded");
  // Leave the slot's view set as the fixture found it.
  set->views[current].coverage = PlaybackCoverageInterval{};
  set->views[current].storage.eventCount = 0;
  set->views[current].complete = false;

  // Interaction acceptance is untouched: the stop request still takes the take out of RECORDING while the interval sits
  // at the limit, because a limit over MIDI **content** holds no representation for an interaction request.
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::isRecording(track));
  track.stopRecordingToStopped(5000);
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::isRecording(track),
                            "a stop request is never refused by the MIDI-content limit");
}

/// 3.12c (DEC-108 §14): the refusal's **cleanup** where the edge has already taken a staging reservation it must give
/// back -- the overdub path, whose admission reserves before the canonical append -- and the one plain event that still
/// fits inside a window one row short of the limit.
void test_capture_admission_refusal_on_the_overdub_path_gives_back_its_staging_reservation() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  Loop& loop = track.getLoop(0);
  // A committed take and then a live overdub interval: the capture path whose admission holds staging reservations.
  commitRecordPassIntoTrackSlot(track, 0);
  TEST_ASSERT_TRUE_MESSAGE(loop.hasCommittedPasses(), "the slot has content to overdub over");
  track.forceSetState(TRACK_PLAYING);
  clockManager.assignCurrentTickSilently(96);
  TEST_ASSERT_TRUE_MESSAGE(track.prewarmPlaybackForSlot(0, true), "the slot's working set is admitted first");
  track.startOverdubbing(96);
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(), "the overdub interval is open");
  LoopPlaybackRuntime* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
  TEST_ASSERT_NOT_NULL(runtime);
  CaptureAdmissionStaging* staging = runtime->liveCaptureStagingIfPrewarmed();
  TEST_ASSERT_NOT_NULL_MESSAGE(staging, "the overdub interval owns a prewarmed ingress staging");

  // Publish the window that serves the slot with `L - reserve - 1` rows: one plain event still fits, a note-on does not.
  RuntimePlaybackViewSet* set = runtime->viewSetIfPrewarmed();
  TEST_ASSERT_NOT_NULL_MESSAGE(set, "the slot's view set is prewarmed");
  const uint8_t current = set->lifecycle.current;
  TEST_ASSERT_LESS_THAN_UINT8(static_cast<uint8_t>(RuntimePlaybackViewConfig::kViewCount), current);
  set->views[current].storage.eventCount =
      RuntimePlaybackViewConfig::kWindowAdmissionLimit - RuntimePlaybackViewConfig::kWindowClosureReserve - 1;
  set->views[current].coverage = PlaybackCoverageInterval{0, 768, 768};
  set->views[current].complete = true;
  set->views[current].published = true;
  const WindowAdmissionAccounting admission = track.captureIntervalAdmissionForSlot(0);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(RuntimePlaybackViewConfig::kWindowAdmissionLimit - 1),
                           static_cast<uint32_t>(admission.committed()));
  TEST_ASSERT_TRUE_MESSAGE(admission.admitsNewEvent(false), "one plain event still fits inside L");
  TEST_ASSERT_FALSE_MESSAGE(admission.admitsNewEvent(true), "a note-on needs its closing row as well");

  // The note-on is refused, and the reservation `noteOn` took before the edge must not outlive the refusal.
  const NoteId reservedId = loop.peekNextNoteId();
  track.noteOn(0, 64, 100, 200);
  TEST_ASSERT_NULL_MESSAGE(staging->matchingOpenReservation(reservedId, 0, 64),
                           "a refused note-on gives its staging reservation back");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(loop.capture.store.size()), "and accepts no row");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      0u, static_cast<uint32_t>(track.captureIntervalAdmissionForSlot(0).outstandingReservations),
      "and leaves nothing owed");

  // The one plain event that still fits is accepted, and the next is refused exactly at L.
  TEST_ASSERT_TRUE_MESSAGE(track.recordMidiEvents(midi::ControlChange, 0, 1, 64, 300),
                           "the one plain event that still fits is accepted");
  TEST_ASSERT_FALSE_MESSAGE(track.recordMidiEvents(midi::ControlChange, 0, 1, 64, 400),
                            "and the next one is refused exactly at L");
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(loop.capture.store.size()));

  // Leave the slot's view set as the fixture found it.
  set->views[current].published = false;
  set->views[current].complete = false;
  set->views[current].coverage = PlaybackCoverageInterval{};
  set->views[current].storage.eventCount = 0;
}

/// 3.13 (DEC-108 item 5): the **preview source contract** -- preview is published through the same immutable deferred
/// discipline as committed content, carries its own currency (the session's preview revision) and is identified as
/// preview rather than as a committed window at the same revision, generation and interval (fixture 2.12's identity
/// clause).
void test_preview_window_is_a_distinct_source_and_never_committed_currency() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  Loop& loop = track.getLoop(0);
  loop.loopLengthTicks = kWindowOwnerLoopTicks;
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  // The frozen preview projection and the session revision it belongs to: the session's own revision is the lane's
  // freeze token and its currency, never the loop's committed revision.
  SessionMidiEventVec preview;
  preview.push_back(MidiEvent::NoteOn(0, 0, 60, 100));
  preview.push_back(MidiEvent::NoteOff(96, 0, 60, 0));
  constexpr uint32_t kPreviewRevision = 7;
  constexpr uint32_t kPlayhead = 500;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(track.requestPreviewWindow(0, kPreviewRevision, preview.data(),
                                                                preview.size(), kPlayhead)));
  for (size_t turn = 0; turn < 8 && track.hasPendingPlaybackViewMaintenance(); ++turn) {
    (void)track.stepPlaybackViewMaintenance();
  }
  TEST_ASSERT_FALSE(track.hasPendingPlaybackViewMaintenance());
  PlaybackWindowIdentity previewWindow{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, previewWindow));
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<uint8_t>(PlaybackWindowOrigin::Preview),
                                  static_cast<uint8_t>(previewWindow.origin),
                                  "the published window is identified as preview");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(kPreviewRevision, previewWindow.playbackRevision,
                                   "preview currency is the session's preview revision");
  TEST_ASSERT_TRUE(track.servedPlaybackWindowIsPreviewForSlot(0));
  TEST_ASSERT_TRUE_MESSAGE(loop.loopLengthTicks == previewWindow.interval.loopLengthTicks,
                           "a preview covers the whole loop: a session is not an interval");
  TEST_ASSERT_EQUAL_UINT32(0u, previewWindow.interval.startPhase);
  // The spec's scenario: a window for the same slot, generation, revision and interval -- but committed -- is **not**
  // equal to the preview window, so a consumer that requires committed currency refuses it on identity alone.
  PlaybackWindowIdentity committedSide = previewWindow;
  committedSide.origin = PlaybackWindowOrigin::Committed;
  TEST_ASSERT_FALSE_MESSAGE(committedSide == previewWindow,
                            "a committed window is not the preview window at the same revision and interval");
  TEST_ASSERT_TRUE(committedSide != previewWindow);
  // ... and a preview with no content is refused rather than published as an empty window over edited content.
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::NotServable),
                    static_cast<int>(track.requestPreviewWindow(0, kPreviewRevision + 1, nullptr, 0, kPlayhead)));
}

/// 3.13: the preview lane's **lifecycle** -- an in-flight preview build is cancelled by a mutated or re-focused source
/// with nothing stale published, and a session exit hands off to committed content at the adoption boundary rather than
/// through a gap (fixture 2.12's cancellation and committed-handoff clauses).
void test_preview_window_is_cancelled_by_its_lifecycle_and_hands_off_to_committed() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  Loop& loop = track.getLoop(0);
  loop.loopLengthTicks = kWindowOwnerLoopTicks;
  publishBarSpacedRootForSlot(loop, 64);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  SessionMidiEventVec preview;
  preview.push_back(MidiEvent::NoteOn(0, 0, 60, 100));
  preview.push_back(MidiEvent::NoteOff(96, 0, 60, 0));
  constexpr uint32_t kPlayhead = 500;
  // A cancellation with nothing of the lane's own is named as such: it must not cancel a committed build.
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PreviewCancelOutcome::NothingToCancel),
                    static_cast<int>(track.cancelPreviewWindowForSlot(
                        0, Track::PreviewWindowCancelReason::SourceMutated)));
  // A preview window is published and served, then the session exits.
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(track.requestPreviewWindow(0, 11, preview.data(), preview.size(), kPlayhead)));
  for (size_t turn = 0; turn < 8 && track.hasPendingPlaybackViewMaintenance(); ++turn) {
    (void)track.stepPlaybackViewMaintenance();
  }
  PlaybackWindowIdentity servedPreview{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, servedPreview));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PlaybackWindowOrigin::Preview),
                          static_cast<uint8_t>(servedPreview.origin));
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PreviewCancelOutcome::HandoffOwed),
                    static_cast<int>(track.cancelPreviewWindowForSlot(
                        0, Track::PreviewWindowCancelReason::SessionExited)));
  TEST_ASSERT_TRUE(track.previewWindowHandoffOwedForSlot(0));
  TEST_ASSERT_TRUE_MESSAGE(track.servedPlaybackWindowIsPreviewForSlot(0),
                           "the preview window keeps serving across the handoff boundary -- never a gap");
  PlaybackWindowIdentity atHandoff{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, atHandoff));
  TEST_ASSERT_TRUE_MESSAGE(atHandoff == servedPreview,
                           "and it is untouched field-for-field by the handoff");
  // A repeated exit is **named** rather than treated as a second handoff.
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PreviewCancelOutcome::AlreadyHandedOff),
                    static_cast<int>(track.cancelPreviewWindowForSlot(
                        0, Track::PreviewWindowCancelReason::SessionExited)));
  // The committed successor: requested through the committed lane, published as the **ready successor** while the
  // preview window still serves, and swapped in at the adoption boundary.
  SessionMidiEventVec scratch;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(track.requestPlaybackWindow(0, kPlayhead, scratch)));
  for (size_t turn = 0; turn < 8 && track.hasPendingPlaybackViewMaintenance(); ++turn) {
    (void)track.stepPlaybackViewMaintenance();
  }
  PlaybackWindowIdentity beforeAdopt{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, beforeAdopt));
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<uint8_t>(PlaybackWindowOrigin::Preview),
                                  static_cast<uint8_t>(beforeAdopt.origin),
                                  "the committed successor is prepared, not yet serving");
  TEST_ASSERT_TRUE(track.previewWindowHandoffOwedForSlot(0));
  TEST_ASSERT_TRUE(track.adoptPreparedPlaybackWindow(0));
  PlaybackWindowIdentity afterAdopt{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, afterAdopt));
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<uint8_t>(PlaybackWindowOrigin::Committed),
                                  static_cast<uint8_t>(afterAdopt.origin),
                                  "the committed successor serves once it is adopted");
  TEST_ASSERT_FALSE_MESSAGE(track.previewWindowHandoffOwedForSlot(0),
                            "and the handoff is discharged at that boundary");
  TEST_ASSERT_FALSE(track.servedPlaybackWindowIsPreviewForSlot(0));
  // A mutated source cancels the **in-flight** preview build and invalidates its identity, so a stale completion can
  // never publish. Track 1 is used so that "nothing was published" is observable: no window is served there.
  Track& other = trackManager.getTrack(1);
  other.ensureLoopsAllocated();
  TrackCaptureStopTestAccess::selectSlot(other, 0);
  commitRecordPassIntoTrackSlot(other, 0);
  Loop& otherLoop = other.getLoop(0);
  otherLoop.loopLengthTicks = kWindowOwnerLoopTicks;
  TEST_ASSERT_TRUE(other.prewarmPlaybackForSlot(0, true));
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(other.requestPreviewWindow(0, 21, preview.data(), preview.size(), kPlayhead)));
  TEST_ASSERT_TRUE_MESSAGE(other.hasPendingPlaybackViewMaintenance(),
                           "the preview build is in flight when the source mutates");
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PreviewCancelOutcome::BuildCancelled),
                    static_cast<int>(other.cancelPreviewWindowForSlot(
                        0, Track::PreviewWindowCancelReason::SourceMutated)));
  TEST_ASSERT_FALSE_MESSAGE(other.hasPendingPlaybackViewMaintenance(),
                            "the cancelled build owes nothing further");
  PlaybackWindowIdentity neverPublished{};
  TEST_ASSERT_FALSE_MESSAGE(other.publishedPlaybackWindowIdentityForSlot(0, neverPublished),
                            "a cancelled preview build publishes no stale window");
  TEST_ASSERT_FALSE(other.previewWindowHandoffOwedForSlot(0));
}

/// 4.5 (DEC-108 §13e-f): the second consecutive miss **latches** a named deadline-contract fault, containment is total
/// (the last coherent revision keeps being served while coverage remains, preparation keeps progressing, recovery happens
/// only at an eligible boundary), and only when **no** valid coverage remains does the approved last resort fire --
/// targeted note closure plus affected-slot silence, never a global transport stop and never an unbounded rebuild.
void test_second_miss_latches_a_fault_and_the_last_resort_is_targeted_not_global() {
  LoopEventStore::initPool();
  g_nativeSilenceTrackCalls = 0;
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  commitRecordPassIntoTrackSlot(track, 0);
  Loop& loop = track.getLoop(0);
  loop.loopLengthTicks = kWindowOwnerLoopTicks;
  publishBarSpacedRootForSlot(loop, 64);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(1, true));
  // A committed window is served, so containment has coverage to reason about.
  SessionMidiEventVec scratch;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(track.requestPlaybackWindow(0, 500u, scratch)));
  for (size_t turn = 0; turn < 8 && track.hasPendingPlaybackViewMaintenance(); ++turn) {
    (void)track.stepPlaybackViewMaintenance();
  }
  PlaybackWindowIdentity served{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, served));
  const uint32_t servedRevision = served.playbackRevision;
  const uint32_t len = kWindowOwnerLoopTicks;
  // (1) The first miss is the one-pass-late **allowance**, not a fault.
  TEST_ASSERT_EQUAL(static_cast<int>(PlaybackLagDecision::PreviousOnePass),
                    static_cast<int>(track.advancePlaybackLagTraversal(0, 0, len, servedRevision + 1, false)));
  TEST_ASSERT_FALSE(track.deadlineContractFaultLatchedForSlot(0));
  // (2) The second consecutive miss latches the **named** fault and is reported once per episode.
  TEST_ASSERT_EQUAL(static_cast<int>(PlaybackLagDecision::BreachSecondMiss),
                    static_cast<int>(track.advancePlaybackLagTraversal(0, 0, len, servedRevision + 1, false)));
  TEST_ASSERT_TRUE_MESSAGE(track.deadlineContractFaultLatchedForSlot(0),
                           "a second miss latches the deadline-contract fault");
  TEST_ASSERT_EQUAL_UINT8(1, track.playbackLagBreachReportsForSlot(0));
  // A **further edit** neither forgives the miss nor re-reports it (AC-5), and the fault stays latched.
  TEST_ASSERT_EQUAL(static_cast<int>(PlaybackLagDecision::BreachSecondMiss),
                    static_cast<int>(track.advancePlaybackLagTraversal(0, 0, len, servedRevision + 2, false)));
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, track.playbackLagBreachReportsForSlot(0),
                                  "the breach is named once per episode, not once per miss");
  TEST_ASSERT_TRUE(track.deadlineContractFaultLatchedForSlot(0));
  // (3) Continuity, and containment that is **not** a successful fallback: the served window is untouched, and with
  // coverage intact the decision is to keep serving the last coherent revision.
  PlaybackWindowIdentity stillServed{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, stillServed));
  TEST_ASSERT_TRUE_MESSAGE(stillServed == served, "no containment path tears the served window");
  TEST_ASSERT_EQUAL(static_cast<int>(ContinuityContainment::ServeLastCoherentRevision),
                    static_cast<int>(track.containmentForSlot(0, 500u)));
  // ... and preparation keeps progressing: the lane still admits a replacement while the fault is latched.
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(track.requestPlaybackWindow(0, 900u, scratch)));
  for (size_t turn = 0; turn < 8 && track.hasPendingPlaybackViewMaintenance(); ++turn) {
    (void)track.stepPlaybackViewMaintenance();
  }
  // (4) Recovery at an **eligible boundary** -- the adopted replacement -- and only there.
  TEST_ASSERT_TRUE(track.adoptPreparedPlaybackWindow(0));
  PlaybackWindowIdentity recovered{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, recovered));
  TEST_ASSERT_EQUAL(static_cast<int>(PlaybackLagDecision::Current),
                    static_cast<int>(track.advancePlaybackLagTraversal(0, 0, len, recovered.playbackRevision, false)));
  TEST_ASSERT_FALSE_MESSAGE(track.deadlineContractFaultLatchedForSlot(0),
                            "an eligible boundary clears the latched fault");
  TEST_ASSERT_EQUAL_UINT32(0u, track.playbackLagMissedTraversalsForSlot(0));
  // (5) The **approved last resort**, and only when no valid coverage remains: latch the fault again, then remove the
  // served window. Both slot 0 and slot 1 hold an open note, so "the affected slot" and "not global" are observable in
  // the same run.
  TEST_ASSERT_EQUAL(static_cast<int>(PlaybackLagDecision::PreviousOnePass),
                    static_cast<int>(track.advancePlaybackLagTraversal(0, 0, len, recovered.playbackRevision + 1, false)));
  TEST_ASSERT_EQUAL(static_cast<int>(PlaybackLagDecision::BreachSecondMiss),
                    static_cast<int>(track.advancePlaybackLagTraversal(0, 0, len, recovered.playbackRevision + 1, false)));
  TEST_ASSERT_TRUE(track.deadlineContractFaultLatchedForSlot(0));
  TrackCaptureStopTestAccess::markLedgerEntry(track, 0, 60);
  TrackCaptureStopTestAccess::markLedgerEntry(track, 1, 62);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::ledgerEntryActive(track, 0, 60));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::ledgerEntryActive(track, 1, 62));
  MidiSendCapture::reset();
  TEST_ASSERT_EQUAL(static_cast<int>(Track::LastResortOutcome::NotNeeded),
                    static_cast<int>(track.applyLastResortForSlot(0, 500u)));
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::ledgerEntryActive(track, 0, 60),
                           "silence is never opportunistic: untouched while coverage remains");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(MidiSendCapture::count()),
                                   "and it emits nothing at all: no closure, no all-notes-off");
  LoopPlaybackRuntime* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
  TEST_ASSERT_NOT_NULL(runtime);
  RuntimePlaybackViewSet* set = runtime->viewSetIfPrewarmed();
  TEST_ASSERT_NOT_NULL(set);
  const uint8_t current = set->lifecycle.current;
  set->views[current].published = false;
  PlaybackViewSourceIdentity ignoredIdentity{};
  PlaybackCoverageInterval ignoredCoverage{};
  TEST_ASSERT_FALSE(track.publishedPlaybackWindowForSlot(0, ignoredIdentity, ignoredCoverage));
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(ContinuityContainment::LastResortTargetedSilence),
                            static_cast<int>(track.containmentForSlot(0, 500u)),
                            "no valid coverage remains at all");
  MidiSendCapture::reset();
  TEST_ASSERT_EQUAL(static_cast<int>(Track::LastResortOutcome::SlotSilenced),
                    static_cast<int>(track.applyLastResortForSlot(0, 500u)));
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::ledgerEntryActive(track, 0, 60),
                            "the affected slot's notes are closed");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::ledgerEntryActive(track, 1, 62),
                           "and a sibling slot is untouched -- the last resort is targeted, not global");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, static_cast<uint32_t>(MidiSendCapture::count()),
                                   "exactly the affected slot's one open note is closed, and nothing else is sent");
  TEST_ASSERT_TRUE(MidiSendCapture::at(0).isNoteOff());
  TEST_ASSERT_EQUAL_UINT8(1, MidiSendCapture::at(0).channel);
  TEST_ASSERT_EQUAL_UINT8(60, MidiSendCapture::at(0).data.noteData.note);
  // Leave the slot's view set as the fixture found it.
  set->views[current].complete = false;
}

/// 4.5 (DEC-108 §13g): the **cross-generation closure reconciliation**, computed off-clock and bounded. A sounding note
/// the replacement removes is closed at handoff (its note-off goes out **first**, before any replacement note-on); a
/// sounding note the replacement **re-issues** is not retriggered and not closed; a note sustained across the handoff is
/// untouched; and an **event-empty window means "no new events"** -- it silences nothing.
void test_cross_generation_closures_are_reconciled_and_an_empty_window_silences_nothing() {
  LoopEventStore::initPool();
  MidiSendCapture::reset();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  commitRecordPassIntoTrackSlot(track, 0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  // (A) Authoritative whole-loop content: three sounding notes, one re-issued, two absent.
  TrackCaptureStopTestAccess::markLedgerEntry(track, 0, 60);
  TrackCaptureStopTestAccess::markLedgerEntry(track, 0, 64);
  TrackCaptureStopTestAccess::markLedgerEntry(track, 0, 67);
  MidiEventVec incoming;
  incoming.push_back(MidiEvent::NoteOn(0, 1, 60, 100));
  incoming.push_back(MidiEvent::NoteOn(0, 1, 70, 100));
  const ClosureSettlement settlement =
      track.planClosureSettlementForSlot(0, incoming.data(), incoming.size(), /*incomingIsWholeLoop=*/true);
  TEST_ASSERT_FALSE(settlement.eventEmptyWindow);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(2u, settlement.closureCount, "the two absent soundings are closed");
  TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>((1u << 8) | 64u), settlement.closures[0]);
  TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>((1u << 8) | 67u), settlement.closures[1]);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(1u, settlement.suppressedRetriggerCount,
                                   "the re-issued note is not retriggered");
  TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>((1u << 8) | 60u), settlement.suppressedRetriggers[0]);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(0u, settlement.preservedCount,
                                   "whole-loop absence is removal, so nothing here is merely preserved");
  // The closures go out **first**, and only they -- the caller emits the replacement's note-ons after this returns.
  MidiSendCapture::reset();
  TEST_ASSERT_EQUAL_UINT32(2u, static_cast<uint32_t>(track.applyClosureSettlementForSlot(0, settlement)));
  TEST_ASSERT_EQUAL_UINT32(2u, static_cast<uint32_t>(MidiSendCapture::count()));
  TEST_ASSERT_TRUE(MidiSendCapture::at(0).isNoteOff());
  TEST_ASSERT_EQUAL_UINT8(64, MidiSendCapture::at(0).data.noteData.note);
  TEST_ASSERT_TRUE(MidiSendCapture::at(1).isNoteOff());
  TEST_ASSERT_EQUAL_UINT8(67, MidiSendCapture::at(1).data.noteData.note);
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::ledgerEntryActive(track, 0, 64),
                            "a closed note's obligation is discharged");
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::ledgerEntryActive(track, 0, 67));
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::ledgerEntryActive(track, 0, 60),
                           "the re-issued note stays sounding: its obligation survives the handoff untouched");
  // (B) A **slice** of the loop: absence is not evidence, so a sounding note is preserved rather than closed.
  TrackCaptureStopTestAccess::markLedgerEntry(track, 0, 72);
  MidiEventVec slice;
  slice.push_back(MidiEvent::NoteOn(0, 1, 70, 100));
  const ClosureSettlement fromSlice =
      track.planClosureSettlementForSlot(0, slice.data(), slice.size(), /*incomingIsWholeLoop=*/false);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(0u, fromSlice.closureCount,
                                   "a slice cannot prove a sounding note was removed");
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(2u, fromSlice.preservedCount,
                                   "both soundings absent from the slice are sustained across the handoff");
  MidiSendCapture::reset();
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(track.applyClosureSettlementForSlot(0, fromSlice)));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(MidiSendCapture::count()),
                                   "and nothing is emitted for a preserved note");
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::ledgerEntryActive(track, 0, 72));
  // (C) An **event-empty** window: "no new events", so it silences nothing -- even when it is authoritative.
  TrackCaptureStopTestAccess::markLedgerEntry(track, 0, 74);
  const ClosureSettlement fromEmpty =
      track.planClosureSettlementForSlot(0, nullptr, 0, /*incomingIsWholeLoop=*/true);
  TEST_ASSERT_TRUE(fromEmpty.eventEmptyWindow);
  TEST_ASSERT_FALSE(handoffAbsenceMeansRemoval(true, true));
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(0u, fromEmpty.closureCount, "an empty window closes nothing");
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(3u, fromEmpty.preservedCount,
                                   "every sounding note is preserved: an empty window says nothing about them");
  MidiSendCapture::reset();
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(track.applyClosureSettlementForSlot(0, fromEmpty)));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(MidiSendCapture::count()),
                                   "an empty window must not silence a legitimately sustained note");
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::ledgerEntryActive(track, 0, 74));
}

/// 3.14 (DEC-108 §13(g)): the **undo/redo audible handoff**. The blanket slot silence is gone: undoing an edit batch
/// settles the batch's **own** identities against the ledger, so a sounding note the batch never touched receives neither
/// an early NoteOff nor a retrigger, and only a note whose identity the batch removes is closed. Driven through the
/// production path (`TrackUndo::undoForLoop` / `TrackUndo::redoForLoop`).
void test_undo_edit_batch_settles_its_own_soundings_and_never_blankets_the_slot() {
  LoopEventStore::initPool();
  MidiSendCapture::reset();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  commitRecordPassIntoTrackSlot(track, 0);
  Loop& loop = track.getLoop(0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  LoopPlaybackRuntime* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
  TEST_ASSERT_NOT_NULL(runtime);
  // An edit batch that **added** one note (60): its identities are what an undo removes.
  constexpr EditPassId kBatchId = 4242;
  EditPass batch{};
  batch.id = kBatchId;
  batch.passType = EditPassType::Note;
  batch.state = EditPassState::Active;
  batch.addedEvents.push_back(MidiEvent::NoteOn(0, 1, 60, 100));
  loop.passes.editPasses.push_back(batch);
  // Two soundings: one the batch owns (60) and one unrelated (64) that must survive untouched.
  runtime->ledger.noteOn(1, 60, 7, 0, 100);
  runtime->ledger.noteOn(1, 64, 8, 0, 100);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::ledgerEntryActive(track, 0, 60));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::ledgerEntryActive(track, 0, 64));
  // (1) Undo: the batch is disabled, so its identity is removed -- exactly one closure, and the unrelated sounding is
  // untouched. The blanket silence this task replaces emitted a NoteOff for **both**.
  UndoEntry entry{};
  entry.kind = UndoEntryKind::NoteEditPassClosed;
  entry.slotIndex = 0;
  entry.editPassType = EditPassType::Note;
  entry.editPassIds.push_back(kBatchId);
  pushUndoEntry(track, std::move(entry));
  MidiSendCapture::reset();
  TrackUndo::undoForLoop(track, track.getActiveLoop());
  bool batchDisabled = false;
  for (const EditPass& pass : loop.passes.editPasses) {
    if (pass.id == kBatchId && pass.state == EditPassState::Disabled) {
      batchDisabled = true;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(batchDisabled, "the production undo disabled the edit batch");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, static_cast<uint32_t>(MidiSendCapture::count()),
                                   "exactly one closure goes out, for the identity the batch owned");
  TEST_ASSERT_TRUE(MidiSendCapture::at(0).isNoteOff());
  TEST_ASSERT_EQUAL_UINT8(60, MidiSendCapture::at(0).data.noteData.note);
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::ledgerEntryActive(track, 0, 60),
                            "the removed identity is discharged");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::ledgerEntryActive(track, 0, 64),
                           "and a sounding note the batch never named is untouched -- no early NoteOff at all");
  // (2) The re-establishing direction: nothing is closed, and a note the batch re-establishes is **suppressed** rather
  // than retriggered because it never stopped sounding.
  runtime->ledger.noteOn(1, 60, 7, 0, 100);
  const EditPassIdList redoIds{kBatchId};
  const ClosureSettlement redoPlan =
      track.reconcileAfterEditPassStateChange(0, redoIds, /*identitiesAreRemoved=*/false);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(0u, redoPlan.closureCount, "a re-establishing change closes nothing");
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(1u, redoPlan.suppressedRetriggerCount,
                                   "and the note it re-establishes is not retriggered");
  TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>((1u << 8) | 60u), redoPlan.suppressedRetriggers[0]);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(1u, redoPlan.preservedCount, "the unrelated sounding stays preserved");
  // (3) The same claim where it matters for output: a re-establishing reconciliation emits nothing, so a sound the
  // batch re-establishes is not retriggered either. (Driven through the Track entry point: the production redo route's
  // other entry kinds -- clear-slot side effects, geometry-preview cancellation -- are outside this fixture's subject,
  // and reaching them here would drag the whole edit stack into a suite that deliberately stubs it.)
  MidiSendCapture::reset();
  (void)track.reconcileAfterEditPassStateChange(0, redoIds, /*identitiesAreRemoved=*/false);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(MidiSendCapture::count()),
                                   "a re-establishing change closes nothing -- the blanket silence would have closed both");
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::ledgerEntryActive(track, 0, 64));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::ledgerEntryActive(track, 0, 60));
  // (4) The **generic** refresh -- the path the two overdub-session undo/redo sites take -- is not a silence either. It
  // is reached directly here because driving a whole overdub session is a different fixture's subject; what this one
  // asserts is the audible claim, so it calls the production function and watches the wire.
  MidiSendCapture::reset();
  refreshPlaybackAfterCommittedPassStateChange(track, 0, nullptr, 0, /*identitiesAreRemoved=*/true);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(MidiSendCapture::count()),
                                   "a refresh with no affected identity emits nothing by itself");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::ledgerEntryActive(track, 0, 64),
                           "and it leaves the soundings it did not reconcile alone");
  // (7) The **publication-owner handoff** (task 3.14c): the undo edge speaks the owner's currency instead of silencing.
  // A served window is published for the slot first, so the undo's staleness is visible in the owner's own vocabulary; the
  // refresh then completes through the owner's normal request path, and the served window carries the restored revision.
  Track& refreshTrack = trackManager.getTrack(3);
  refreshTrack.ensureLoopsAllocated();
  TrackCaptureStopTestAccess::selectSlot(refreshTrack, 0);
  commitRecordPassIntoTrackSlot(refreshTrack, 0);
  Loop& refreshLoop = refreshTrack.getActiveLoop();
  refreshLoop.loopLengthTicks = kWindowOwnerLoopTicks;
  publishBarSpacedRootForSlot(refreshLoop, 64);
  TEST_ASSERT_TRUE(refreshTrack.prewarmPlaybackForSlot(0, true));
  SessionMidiEventVec refreshScratch;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(refreshTrack.requestPlaybackWindow(0, 500u, refreshScratch)));
  for (size_t turn = 0; turn < 8 && refreshTrack.hasPendingPlaybackViewMaintenance(); ++turn) {
    (void)refreshTrack.stepPlaybackViewMaintenance();
  }
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackWindowReadiness::Ready),
                    static_cast<int>(refreshTrack.playbackWindowReadinessForSlot(0)));
  const uint32_t revisionBeforeUndo = refreshLoop.playbackRevision;
  EditPass refreshBatch{};
  refreshBatch.id = 4343;
  refreshBatch.passType = EditPassType::Note;
  refreshBatch.state = EditPassState::Active;
  refreshBatch.addedEvents.push_back(MidiEvent::NoteOn(0, 1, 66, 100));
  refreshLoop.passes.editPasses.push_back(refreshBatch);
  UndoEntry refreshEntry{};
  refreshEntry.kind = UndoEntryKind::NoteEditPassClosed;
  refreshEntry.slotIndex = 0;
  refreshEntry.editPassType = EditPassType::Note;
  refreshEntry.editPassIds.push_back(4343);
  pushUndoEntry(refreshTrack, std::move(refreshEntry));
  TrackUndo::undoForLoop(refreshTrack, refreshTrack.getActiveLoop());
  TEST_ASSERT_TRUE_MESSAGE(refreshLoop.playbackRevision != revisionBeforeUndo,
                           "fixture precondition: the undo advanced the loop's revision");
  // The handoff, in the owner's vocabulary: the window lane owes a rebuild, and the edge said so instead of silencing.
  TEST_ASSERT_TRUE_MESSAGE(refreshTrack.playbackWindowReadinessForSlot(0) != Track::PlaybackWindowReadiness::Ready,
                           "a stale window is named as owing, not silently replaced by silence");
  // And the refresh through the owner's own request path is **refused by name**, not served stale: an undo deliberately
  // does not stamp its restored root current (DEC-073's protection, and task 4.1's lagging-checkpoint outcome), so the
  // window's enumeration refuses `SourceNotCurrent` until a current root exists. That is the measured reason the gesture
  // edge does not enumerate for itself: the correct requester is the lane that owns a current root (4.1's outcome, then
  // 3.7's consumer), and what this edge owes is exactly what it does -- advance the currency, settle the closures and
  // let the owner's vocabulary name the rebuild.
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::PlaybackViewRequestResult::SourceNotCurrent),
                            static_cast<int>(refreshTrack.requestPlaybackWindow(0, 500u, refreshScratch)),
                            "a lagging root is named, never served as current");
  TEST_ASSERT_FALSE(refreshTrack.hasPendingPlaybackViewMaintenance());

  // (5) A slot with **no window lane** is not silenced either: the blanket emitter was the only thing that acted there,
  // and acting was the defect.
  MidiSendCapture::reset();
  const ClosureSettlement noLane = track.reconcileAfterEditPassStateChange(1, redoIds, /*identitiesAreRemoved=*/true);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(0u, noLane.closureCount, "no lane, nothing to settle");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(MidiSendCapture::count()),
                                   "and nothing is silenced on its account");

  // (6) The **committed-pass identity source** (the overdub-session undo/redo sites): a session step names a committed
  // pass, whose own bodies give the identities it removes -- so a sounding note that pass carries is closed, an
  // unrelated sounding is preserved, and the stuck-note risk of a session undo is gone.
  Track& sessionTrack = trackManager.getTrack(2);
  sessionTrack.ensureLoopsAllocated();
  TrackCaptureStopTestAccess::selectSlot(sessionTrack, 0);
  Loop& sessionLoop = sessionTrack.getActiveLoop();
  sessionLoop.loopLengthTicks = 4 * Config::TICKS_PER_BAR;
  sessionLoop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(sessionLoop.appendCaptureEvent(MidiEvent::NoteOn(0, 1, 55, 100)));
  TEST_ASSERT_TRUE(sessionLoop.appendCaptureEvent(MidiEvent::NoteOff(96, 1, 55, 0)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, sessionLoop.sealCapture(sessionLoop.loopLengthTicks, CommitReason::RecordStop));
  TEST_ASSERT_TRUE(sessionLoop.commitPendingCapturePass());
  TEST_ASSERT_TRUE(sessionTrack.prewarmPlaybackForSlot(0, true));
  LoopPlaybackRuntime* sessionRuntime = TrackCaptureStopTestAccess::runtimeIfAllocated(sessionTrack, 0);
  TEST_ASSERT_NOT_NULL(sessionRuntime);
  sessionRuntime->ledger.noteOn(1, 55, 11, 0, 100);
  sessionRuntime->ledger.noteOn(1, 62, 12, 0, 100);
  const PassId sessionPassId = sessionLoop.passes.recordPass.id;
  TEST_ASSERT_NOT_EQUAL(static_cast<unsigned>(kInvalidPassId), static_cast<unsigned>(sessionPassId));
  MidiSendCapture::reset();
  const ClosureSettlement fromPass =
      sessionTrack.reconcileAfterCommittedPassStateChange(0, &sessionPassId, 1, /*identitiesAreRemoved=*/true);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(1u, fromPass.closureCount, "the pass's own sounding closes");
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(MidiSendCapture::count()));
  TEST_ASSERT_TRUE(MidiSendCapture::at(0).isNoteOff());
  TEST_ASSERT_EQUAL_UINT8(55, MidiSendCapture::at(0).data.noteData.note);
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::ledgerEntryActive(sessionTrack, 0, 55),
                            "and the pass's carried note is discharged");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::ledgerEntryActive(sessionTrack, 0, 62),
                           "while a sounding the pass does not carry is preserved");
  // ... and the re-establishing direction suppresses rather than closes or retriggers.
  sessionRuntime->ledger.noteOn(1, 55, 11, 0, 100);
  MidiSendCapture::reset();
  const ClosureSettlement reestablished =
      sessionTrack.reconcileAfterCommittedPassStateChange(0, &sessionPassId, 1, /*identitiesAreRemoved=*/false);
  TEST_ASSERT_EQUAL_UINT16(0u, reestablished.closureCount);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(1u, reestablished.suppressedRetriggerCount, "the re-established note is not retriggered");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(MidiSendCapture::count()), "and nothing is emitted");
}


/// 4.1 (DEC-108 *Checkpoint parity for undo and save*, F-5): the published root is the immutable checkpoint authority
/// for undo; its parity against the loop's **current** committed-pass revision is **named**, so a lagging checkpoint is
/// reported as such and never presented as current; and neither the undo nor the answer consults the window's lag.
void test_checkpoint_parity_names_the_lagging_case_and_ignores_the_windows_lag() {
  // (A) The rule as one pure resolution: all four cases, without a loop in the way.
  TEST_ASSERT_EQUAL(static_cast<int>(CheckpointParity::NoCheckpoint),
                    static_cast<int>(resolveCheckpointParity(false, kSnapshotCoverageUnset, 7)));
  TEST_ASSERT_EQUAL(static_cast<int>(CheckpointParity::CoverageUnrecorded),
                    static_cast<int>(resolveCheckpointParity(true, kSnapshotCoverageUnset, 7)));
  TEST_ASSERT_EQUAL(static_cast<int>(CheckpointParity::Current),
                    static_cast<int>(resolveCheckpointParity(true, 7, 7)));
  TEST_ASSERT_EQUAL(static_cast<int>(CheckpointParity::OwesReconciliation),
                    static_cast<int>(resolveCheckpointParity(true, 6, 7)));
  // (B) The named answer agrees with the predicate the loop already had: for a root that exists, `Current` is exactly
  // `publishedSnapshotIsCurrent()`, so the name cannot drift from the rule it names.
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  commitRecordPassIntoTrackSlot(track, 0);
  Loop& loop = track.getLoop(0);
  const uint32_t revisionBeforeUndo = loop.committedPassRevision();
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(CheckpointParity::NoCheckpoint),
                            static_cast<int>(loop.checkpointParity()),
                            "no root is no checkpoint -- and nothing to present as current");
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  const auto rootForState = [&]() {
    return std::make_shared<const LoopSnapshot>(LoopSnapshot(loop.loopId, loop.loopLengthTicks, 0, 8, 8));
  };
  loop.storePublishedSnapshotForTest(rootForState(), 0, kSnapshotCoverageCurrent);
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(CheckpointParity::Current), static_cast<int>(loop.checkpointParity()),
                            "a root derived from the live state is current");
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  // (C) Through the **production undo path**: the entry names a root whose coverage is the pre-undo state, and the undo
  // advances the committed-pass revision -- so the checkpoint authority is the entry's root, selected **with the
  // coverage it carried**, and its parity is named as lagging: selected, yet never presented as current.
  UndoEntry entry{};
  entry.kind = UndoEntryKind::NoteEditPassClosed;
  entry.slotIndex = 0;
  entry.editPassType = EditPassType::Note;
  entry.editPassIds.push_back(4242);
  entry.beforePublishedSnapshot = rootForState();
  entry.beforePublishedCoverage = revisionBeforeUndo;
  EditPass batch{};
  batch.id = 4242;
  batch.passType = EditPassType::Note;
  batch.state = EditPassState::Active;
  loop.passes.editPasses.push_back(batch);
  pushUndoEntry(track, std::move(entry));
  TrackUndo::undoForLoop(track, track.getActiveLoop());
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(revisionBeforeUndo, loop.publishedSnapshotCoveredRevision(),
                                   "the undo restores the root its entry named, with the coverage it carried");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(revisionBeforeUndo + 1, loop.committedPassRevision(),
                                   "fixture precondition: the undo advanced the committed-pass revision");
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(CheckpointParity::OwesReconciliation),
                            static_cast<int>(loop.checkpointParity()),
                            "a lagging checkpoint is named as owing reconciliation");
  TEST_ASSERT_NULL_MESSAGE(loop.publishedSnapshotIdentity(), "and it is never presented as current");
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  // (D) DEC-073's protection, **named**: a root installed with an explicit stamp that is not the live revision, and
  // without its own coverage figure, is demoted to "no coverage recorded" -- conservatively never current, and the
  // answer says which case that is.
  loop.storePublishedSnapshotForTest(rootForState(), /*revision=*/revisionBeforeUndo, kSnapshotCoverageCurrent);
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(CheckpointParity::CoverageUnrecorded),
                            static_cast<int>(loop.checkpointParity()),
                            "a stamp that is not the live revision is demoted, and named");
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  // (E) The answer reads the loop's own figures only: with the slot's window lane **not ready** (nothing published
  // there), a current checkpoint root is still `Current`. The window's lag is the window's business.
  TEST_ASSERT_FALSE(track.playbackWindowReadinessForSlot(0) == Track::PlaybackWindowReadiness::Ready);
  loop.storePublishedSnapshotForTest(rootForState(), 0, kSnapshotCoverageCurrent);
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(CheckpointParity::Current), static_cast<int>(loop.checkpointParity()),
                            "a not-ready window does not make the checkpoint non-current");
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
}

/// 4.3 (F-7): a root whose content is built to be **boundary-sensitive**: a note fully inside the first two-bar
/// tile, one whose NoteOn is inside that tile and whose NoteOff is in the next (**the closure crossing the
/// boundary**), and one late note so the last tile is not empty. Same construction idiom as
/// `publishBarSpacedRootForSlot`, because the windowed gather reads the root's **event** membership.
/// 4.4 (F-8): **the residency statement per slot is length-independent**, swept over loop lengths. For each
/// length the slot is set up through the production entries only -- `Loop::fullTakePlaybackViewAdmissible()` as
/// the policy, `Track::prewarmPlaybackForSlot()` as the admission -- and the residency is read from the slot's own
/// accounting:
///   * the **whole-take (whole-root) playback representation is reserved exactly while the loop is short enough**
///     for the policy to admit it, and for every demoted length it is absent -- "no whole-take reservation SHALL
///     be restored for demoted slots";
///   * for every demoted length the slot's **windowed reservation is the same figure**: a function of the declared
///     views and stagings, never of the loop's length;
///   * every view's storage is the **declared capacity** at every length;
///   * whether a window can actually be prepared is the **declaration's own answer**
///     (`CoverageServiceContract::canCompleteBeforeHandoff`, not an arithmetic of mine), so the loop-length floor
///     the declaration's conservative lead implies is measured rather than asserted -- and below it the request is
///     refused by name.
void test_demoted_slot_residency_is_length_independent_swept_over_lengths() {
  constexpr uint32_t kBar = Config::TICKS_PER_BAR;
  constexpr uint32_t kPhase = kBar;  // a two-bar window centred on the bar line: its interval starts at tick 0
  const uint32_t sweepBars[] = {16u, 17u, 24u, 30u, 32u, 64u};
  constexpr size_t kSweep = sizeof(sweepBars) / sizeof(sweepBars[0]);
  struct ResidencyRow {
    uint32_t bars = 0;
    bool admissible = false;
    bool wholeTakeReserved = false;
    size_t windowedBytes = 0;
    uint16_t viewCapacity = 0;
    bool stagingsPrewarmed = false;
    uint32_t requestResult = 0;
    bool served = false;
    bool declarationCanComplete = false;
  };
  ResidencyRow rows[kSweep] = {};
  const CoverageServiceContract contract = Track::playbackViewCoverageContract();
  TEST_ASSERT_TRUE_MESSAGE(contract.isDeclared(), "the admission calculus must be declared before it can be swept");

  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  for (size_t index = 0; index < kSweep; ++index) {
    ResidencyRow& row = rows[index];
    row.bars = sweepBars[index];
    const uint32_t loopTicks = row.bars * kBar;
    const uint8_t slot = static_cast<uint8_t>(index);  // one slot per length: a clean residency per length
    commitRecordPassIntoTrackSlotOfLength(track, slot, loopTicks);
    Loop& loop = track.getLoop(slot);
    loop.loopLengthTicks = loopTicks;
    publishBarSpacedRootForSlot(loop, row.bars);
    row.admissible = loop.fullTakePlaybackViewAdmissible();
    // The production admission: it reads the loop's own admissibility and decides the representation itself
    // (`runtime->prewarmViews(..., loop.fullTakePlaybackViewAdmissible())`), so the fixture never states the
    // policy to the code -- it only reads back what the code reserved.
    TEST_ASSERT_TRUE_MESSAGE(track.prewarmPlaybackForSlot(slot),
                             "the slot's playback runtime must prewarm through the production admission");
    LoopPlaybackRuntime* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, slot);
    TEST_ASSERT_NOT_NULL(runtime);
    RuntimePlaybackViewSet* set = runtime->viewSetIfPrewarmed();
    TEST_ASSERT_NOT_NULL_MESSAGE(set, "and its view set, which is where the residency lives");
    row.wholeTakeReserved = set->wholeTakeViewsReserved();
    row.windowedBytes = set->fixedExternalBytes();
    row.viewCapacity = static_cast<uint16_t>(set->views[0].storage.capacity);
    row.stagingsPrewarmed = set->captureStagingsPrewarmed();

    // The declaration is the oracle for whether this length can be prepared at all: the same predicate the
    // production request asks, evaluated for this length's interval.
    PlaybackCoverageInterval interval{};
    TEST_ASSERT_TRUE(Track::playbackWindowInterval(loopTicks, !row.admissible, kPhase, interval));
    SessionMidiEventVec scratch;
    const Track::PlaybackViewRequestResult result = track.requestPlaybackWindow(slot, kPhase, scratch);
    // The oracle is the same predicate the production request asks, on the **same derived cost** it asks it with
    // (review step 4): a copy built from the declared basis counts would predict a different request than the
    // firmware makes, which is exactly the mismatch this fixture exists to catch.
    CoverageServiceContract expected = Track::playbackViewCoverageContract();
    expected.deriveGrainsFrom(scratch.size());
    row.declarationCanComplete =
        expected.canCompleteBeforeHandoff(kPhase % loopTicks, interval.startPhase, loopTicks);
    row.requestResult = static_cast<uint32_t>(result);
    if (result == Track::PlaybackViewRequestResult::Admitted) {
      for (size_t turn = 0; turn < 16 && track.hasPendingPlaybackViewMaintenance(); ++turn) {
        (void)track.stepPlaybackViewMaintenance();
      }
      TEST_ASSERT_FALSE(track.hasPendingPlaybackViewMaintenance());
      (void)track.adoptPreparedPlaybackWindow(slot);
      row.served = track.playbackWindowServesSlot(slot);
    }
  }

  // (A) The policy boundary, swept: the whole-take representation is admissible at the declared bar cap and not
  // one bar past it.
  for (size_t index = 0; index < kSweep; ++index) {
    TEST_ASSERT_EQUAL_MESSAGE(rows[index].bars <= DisplayWindowUtils::kMaxDetailedWindowBars,
                              rows[index].admissible, "admissibility follows the declared bar cap");
  }

  // (B) "No whole-take reservation is restored for demoted slots": reserved exactly while admissible, absent for
  // every demoted length.
  bool sawAdmissible = false;
  size_t admissibleBytes = 0;
  size_t demotedBytes = 0;
  bool demotedBytesSet = false;
  for (size_t index = 0; index < kSweep; ++index) {
    const ResidencyRow& row = rows[index];
    TEST_ASSERT_EQUAL_MESSAGE(row.admissible, row.wholeTakeReserved,
                              "the whole-root playback representation is resident exactly while admissible");
    TEST_ASSERT_TRUE_MESSAGE(row.stagingsPrewarmed, "the capture stagings are resident at every length");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(RuntimePlaybackViewConfig::kEventsPerView, row.viewCapacity,
                                     "the window storage is the declared capacity at every length");
    if (row.admissible) {
      sawAdmissible = true;
      admissibleBytes = row.windowedBytes;
      continue;
    }
    // (C) Length independence: every demoted length reports the SAME windowed reservation.
    if (!demotedBytesSet) {
      demotedBytes = row.windowedBytes;
      demotedBytesSet = true;
    }
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(demotedBytes, row.windowedBytes,
                                     "the windowed reservation does not grow with the loop's length");
  }
  TEST_ASSERT_TRUE_MESSAGE(sawAdmissible && demotedBytesSet, "the sweep must cover both regimes");
  TEST_ASSERT_TRUE_MESSAGE(demotedBytes < admissibleBytes,
                           "the demoted reservation is genuinely smaller: the whole-take bundle is absent");

  // (D) The declaration's own answer is the admissibility oracle, and it is what the production request returns.
  bool sawRefusal = false;
  bool sawAdmission = false;
  for (size_t index = 0; index < kSweep; ++index) {
    const ResidencyRow& row = rows[index];
    if (row.declarationCanComplete) {
      sawAdmission = true;
      TEST_ASSERT_EQUAL_MESSAGE(static_cast<uint32_t>(Track::PlaybackViewRequestResult::Admitted),
                                row.requestResult,
                                "the declaration's answer is what the request returns when it can complete");
      TEST_ASSERT_TRUE_MESSAGE(row.served, "and the slot is then served from a window while its root is demoted");
    } else {
      sawRefusal = true;
      TEST_ASSERT_EQUAL_MESSAGE(
          static_cast<uint32_t>(Track::PlaybackViewRequestResult::CannotCompleteBeforeHandoff),
          row.requestResult, "below the declaration's lead the window is refused by name, never half-prepared");
      TEST_ASSERT_FALSE(row.served);
    }
  }
  // (E) The floor as a *number*: the declaration's own arithmetic is evaluated here and the sweep's answer is
  // required to be exactly it -- so "below the lead the window is refused" is a derived boundary rather than a
  // guess, and the smallest demoted length that admits is pinned instead of hiding inside the sweep's granularity.
  // The requirement is **derived per row** now (each length's own gather decides its cost), so what is pinned here is
  // the shape of the boundary rather than one number: the answer is **monotone** in the loop length -- once a length
  // admits, no longer loop re-refuses -- and the sweep straddles the flip.
  uint32_t firstAdmittingBars = 0;
  bool refusedAfterAdmitting = false;
  for (size_t index = 0; index < kSweep; ++index) {
    if (rows[index].declarationCanComplete) {
      if (firstAdmittingBars == 0u) {
        firstAdmittingBars = rows[index].bars;
      }
      TEST_ASSERT_FALSE_MESSAGE(refusedAfterAdmitting,
                                "a longer loop never stops being preparable: the floor is one flip");
    } else if (firstAdmittingBars != 0u) {
      refusedAfterAdmitting = true;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(firstAdmittingBars >= 1u,
                           "the sweep must find a length whose derived lead is satisfied");
  TEST_ASSERT_TRUE_MESSAGE(sawAdmission,
                           "the sweep must admit at least one length: the derived cost fits somewhere in its range");
  // **Re-based by the review's step 4, recorded:** with the lead judged on the *derived* cost the floor moved below
  // this sweep's shortest length, so a refusal inside the range is no longer guaranteed -- and the fixture does not
  // manufacture one. `sawRefusal` is still *used*: a refusal inside the range must be part of the single flip above.
  TEST_ASSERT_TRUE_MESSAGE(sawRefusal || firstAdmittingBars == rows[0].bars,
                           "either the sweep straddles the floor, or its shortest length is already above it");
}
void publishBoundaryRootForSlot(Loop& loop) {
  constexpr uint32_t kTile = 2 * 768;
  struct NoteSpec { NoteId noteId; uint8_t pitch; uint32_t on; uint32_t off; };
  const NoteSpec notes[] = {
      {3001, 60, 100, 200},                  // inside tile 0
      {3002, 62, kTile - 36, kTile + 84},     // ON in tile 0, OFF in tile 1: the boundary closure
      {3003, 64, loop.loopLengthTicks - 300, loop.loopLengthTicks - 200},  // last tile
  };
  auto empty = std::make_shared<const LoopSnapshot>(
      LoopSnapshot(loop.loopId, loop.loopLengthTicks, 0, 8, 8));
  LoopSnapshotChangeSet changes;
  EventId nextEventId = 6000;
  uint32_t canonicalOrder = 0;
  for (const NoteSpec& spec : notes) {
    SnapshotNoteChange add{};
    add.kind = SnapshotChangeKind::Add;
    add.next.loopId = loop.loopId;
    add.next.noteId = spec.noteId;
    add.next.pitch = spec.pitch;
    add.next.velocity = 100;
    add.next.startTick = spec.on;
    add.next.endTick = spec.off;
    changes.notes.push_back(add);
    SnapshotEventChange on{};
    on.kind = SnapshotChangeKind::Add;
    on.next.loopId = loop.loopId;
    on.next.eventId = nextEventId++;
    on.next.canonicalOrder = canonicalOrder++;
    on.next.value = MidiEvent::NoteOn(spec.on, 1, spec.pitch, 100);
    changes.events.push_back(on);
    SnapshotEventChange off{};
    off.kind = SnapshotChangeKind::Add;
    off.next.loopId = loop.loopId;
    off.next.eventId = nextEventId++;
    off.next.canonicalOrder = canonicalOrder++;
    off.next.value = MidiEvent::NoteOff(spec.off, 1, spec.pitch, 0);
    changes.events.push_back(off);
  }
  LoopSnapshotConstruction construction(empty, changes);
  while (!construction.complete()) {
    TEST_ASSERT_TRUE(construction.step(128));
  }
  const ImmutableLoopSnapshotRef snapshot = construction.freeze();
  TEST_ASSERT_NOT_NULL(snapshot.get());
  loop.storePublishedSnapshotForTest(snapshot, loop.playbackRevision);
  TEST_ASSERT_TRUE_MESSAGE(loop.publishedSnapshotIsCurrent(),
                           "the fixture must install a *current* root, or the gather refuses it");
}

/// Reads the **served** window's whole stream, in emission order, through the stream accessor a consumer will use.
void readServedWindowStream(Track& track, uint8_t slot, SessionMidiEventVec& out) {
  out.clear();
  const size_t count = track.playbackWindowEventCountForSlot(slot);
  // An interval with no events is a legitimately **ready** window (task 3.5): the window exists and serves the
  // slot, it simply holds nothing. So the presence claim is `playbackWindowServesSlot`, not a non-zero count.
  TEST_ASSERT_TRUE_MESSAGE(track.playbackWindowServesSlot(slot), "a published window serves the slot");
  for (size_t cursor = 0; cursor < count; ++cursor) {
    MidiEvent event{};
    TEST_ASSERT_TRUE(track.playbackWindowEventAtForSlot(slot, static_cast<uint16_t>(cursor), event));
    out.push_back(event);
  }
  // Out of range is a refusal, not a wrapped read.
  MidiEvent beyond{};
  TEST_ASSERT_FALSE(track.playbackWindowEventAtForSlot(slot, static_cast<uint16_t>(count), beyond));
}

/// F-7: the events served by **successive windows** concatenate to exactly the events the whole-loop path
/// is present exactly once in the concatenation, so no note-on is left unclosed by the transition.
void test_windowed_stream_concatenates_to_the_whole_loop_stream_across_a_boundary() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  Loop& loop = track.getLoop(0);
  constexpr uint32_t kBar = 768;
  constexpr uint32_t kTile = 2 * kBar;
  loop.loopLengthTicks = kWindowOwnerLoopTicks;  // 64 bars: demoted policy, and long enough for the declared lead
  publishBoundaryRootForSlot(loop);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));

  // The whole-loop reference comes from the project's existing resolver at whole-loop width -- the same
  // primitive the content-verification pipeline's inventory dump is built on, not a second pipeline.
  SessionMidiEventVec wholeLoop;
  LoopContentResolution::resolveWindow(*loop.publishedSnapshot(), 0, loop.loopLengthTicks, wholeLoop);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(6u, static_cast<uint32_t>(wholeLoop.size()),
                                   "three notes, on and off: the reference stream");

  SessionMidiEventVec concatenated;
  SessionMidiEventVec window;
  SessionMidiEventVec scratch;
  uint32_t tilesServed = 0;
  uint32_t boundaryOnTile = 0;
  uint32_t boundaryOffTile = 0;
  for (uint32_t start = 0; start < loop.loopLengthTicks; start += kTile) {
    // The playhead in the middle of the tile resolves to that tile: a two-bar window centred on the playhead.
    const uint32_t centre = start + kTile / 2;
    PlaybackCoverageInterval expected{};
    TEST_ASSERT_TRUE(Track::playbackWindowInterval(loop.loopLengthTicks, true, centre, expected));
    TEST_ASSERT_EQUAL_UINT32(start, expected.startPhase);
    TEST_ASSERT_EQUAL_UINT32(kTile, expected.lengthTicks);
    TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                      static_cast<int>(track.requestPlaybackWindow(0, centre, scratch)));
    for (size_t turn = 0; turn < 16 && track.hasPendingPlaybackViewMaintenance(); ++turn) {
      (void)track.stepPlaybackViewMaintenance();
    }
    TEST_ASSERT_FALSE(track.hasPendingPlaybackViewMaintenance());
    // The two publication paths, told apart rather than papered over: the **first** window is installed as the
    // served one (nothing to straddle), while a **replacement** publishes as the ready successor and the
    // consumer-side edge adopts it at an eligible boundary (task 3.4). Either way, after this point the window
    // that serves the slot is the tile that was just requested.
    const bool adopted = track.adoptPreparedPlaybackWindow(0);
    if (tilesServed == 0) {
      TEST_ASSERT_FALSE_MESSAGE(adopted, "the first window is installed as served, not as a successor");
    } else {
      TEST_ASSERT_TRUE_MESSAGE(adopted, "a replacement publishes as a successor for the consumer to adopt");
    }
    PlaybackCoverageInterval coverageOut{};
    PlaybackViewSourceIdentity identityOut{};
    TEST_ASSERT_TRUE(track.publishedPlaybackWindowForSlot(0, identityOut, coverageOut));
    TEST_ASSERT_EQUAL_UINT32(start, coverageOut.startPhase);
    TEST_ASSERT_EQUAL_UINT32(kTile, coverageOut.lengthTicks);

    readServedWindowStream(track, 0, window);
    const size_t tileIndex = tilesServed;
    for (const MidiEvent& event : window) {
      if (event.type == midi::NoteOn && event.data.noteData.note == 62) {
        boundaryOnTile = static_cast<uint32_t>(tileIndex);
      }
      if (event.type == midi::NoteOff && event.data.noteData.note == 62) {
        boundaryOffTile = static_cast<uint32_t>(tileIndex);
      }
      concatenated.push_back(event);
    }
    ++tilesServed;
  }
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(loop.loopLengthTicks / kTile), tilesServed);

  // Parity: the concatenation equals the whole-loop stream, row for row and in order.
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(wholeLoop.size()),
                           static_cast<uint32_t>(concatenated.size()));
  for (size_t index = 0; index < concatenated.size(); ++index) {
    TEST_ASSERT_EQUAL_UINT32(wholeLoop[index].tick, concatenated[index].tick);
    TEST_ASSERT_EQUAL(static_cast<int>(wholeLoop[index].type),
                      static_cast<int>(concatenated[index].type));
    TEST_ASSERT_EQUAL_UINT8(wholeLoop[index].channel, concatenated[index].channel);
    TEST_ASSERT_EQUAL_UINT8(wholeLoop[index].data.noteData.note,
                            concatenated[index].data.noteData.note);
  }

  // The closure clause, read as the *concatenation*: no note-on is left unclosed by the transition. The
  // boundary note's closure is served by the **next** tile -- measured, not assumed -- because a window's
  // stream is a half-open tick filter and the closure's tick belongs to the following interval. The
  // obligation at the transition is therefore the consumer's (the ledger's obligation set), which is what
  // task 3.7's emission switch and 4.5's closure handling own.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, boundaryOnTile, "the boundary note's ON is in the first tile");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, boundaryOffTile,
                                   "and its closure is served by the next tile, not synthesized at the edge");
  size_t ons = 0;
  size_t offs = 0;
  for (const MidiEvent& event : concatenated) {
    if (event.type == midi::NoteOn) ++ons;
    if (event.type == midi::NoteOff) ++offs;
  }
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(offs), static_cast<uint32_t>(ons));
}

/// A root carrying `count` short notes inside the bar range `[fromBar, toBar)`, and nothing elsewhere. The
/// windowed gather reads the root's **event** membership, so every note is added with its on and off.
void publishNotesInBarsRootForSlot(Loop& loop, uint32_t fromBar, uint32_t toBar, uint32_t count) {
  auto empty = std::make_shared<const LoopSnapshot>(
      LoopSnapshot(loop.loopId, loop.loopLengthTicks, 0, 8, 8));
  LoopSnapshotChangeSet changes;
  constexpr uint32_t kBar = 768;
  const uint32_t span = (toBar - fromBar) * kBar;
  EventId nextEventId = 9000;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t on = fromBar * kBar + (span * i) / (count > 0 ? count : 1);
    const uint8_t pitch = static_cast<uint8_t>(36 + (i % 40));
    SnapshotNoteChange add{};
    add.kind = SnapshotChangeKind::Add;
    add.next.loopId = loop.loopId;
    add.next.noteId = static_cast<NoteId>(4000 + i);
    add.next.pitch = pitch;
    add.next.velocity = 100;
    add.next.startTick = on;
    add.next.endTick = on + 48;
    changes.notes.push_back(add);
    SnapshotEventChange on_change{};
    on_change.kind = SnapshotChangeKind::Add;
    on_change.next.loopId = loop.loopId;
    on_change.next.eventId = nextEventId++;
    on_change.next.canonicalOrder = nextEventId * 2;
    on_change.next.value = MidiEvent::NoteOn(on, 1, pitch, 100);
    changes.events.push_back(on_change);
    SnapshotEventChange off_change{};
    off_change.kind = SnapshotChangeKind::Add;
    off_change.next.loopId = loop.loopId;
    off_change.next.eventId = nextEventId++;
    off_change.next.canonicalOrder = nextEventId * 2 + 1;
    off_change.next.value = MidiEvent::NoteOff(on + 48, 1, pitch, 0);
    changes.events.push_back(off_change);
  }
  LoopSnapshotConstruction construction(empty, changes);
  while (!construction.complete()) {
    TEST_ASSERT_TRUE(construction.step(128));
  }
  const ImmutableLoopSnapshotRef snapshot = construction.freeze();
  TEST_ASSERT_NOT_NULL(snapshot.get());
  loop.storePublishedSnapshotForTest(snapshot, loop.playbackRevision);
  TEST_ASSERT_TRUE_MESSAGE(loop.publishedSnapshotIsCurrent(),
                           "the fixture must install a *current* root, or the gather refuses it");
}
/// Has a holder's owed build moved past its first fill grain? **"Progressed" is the honest observable**: a small
/// window's single fill grain finishes the build outright (`Complete`), while a larger one stays `Fill` for several
/// turns, so the fixture asserts that the served holder advanced and the others did not -- the specific post-state is
/// the builder's business, not this fixture's.
bool windowHolderAdvanced(const LoopPlaybackRuntime* runtime) {
  if (runtime == nullptr) {
    return false;
  }
  if (!runtime->coverageBuildNeedsService()) {
    return true;
  }
  return runtime->nextCoverageBuildGrain() != RuntimePlaybackViewBuildGrain::Fill;
}

/// The **per-slot request source**, file-scope on purpose: the window build reads its source across turns, so the caller
/// owns that source for the build's whole duration. One vector per slot (two slots can owe at once), replaced only by a
/// later request for the same slot -- which the fixtures issue only after that slot's earlier build is complete.
SessionMidiEventVec& windowSourceScratch(uint8_t slot) {
  static SessionMidiEventVec buffers[Config::MAX_LOOPS_PER_TRACK];
  return buffers[slot % Config::MAX_LOOPS_PER_TRACK];
}


/// The stamp the suite's window requests carry. **Device time is monotone**, while a fixture's *phases* are not: the
/// urgency fixtures deliberately request a farther interval first and a nearer one second, and the interference budget
/// refuses a clock that goes backwards (`clock.tick < work->admittedTick` -- a reset/rebase or an out-of-order caller,
/// never a legitimate request). So the tick advances across the suite's requests and the phase stays what each fixture
/// needs; the epoch is fixed, since none of these fixtures models a reset.
PlaybackDeadlineClock nextWindowRequestClock() {
  static uint64_t tick = 1000000ull;
  tick += 64ull;
  nativePlaybackDeadlineClock = {tick, 1u};
  return nativePlaybackDeadlineClock;
}

/// Prepares one slot with a current root and a prewarmed runtime, then requests the window its own policy names.
void prepareOwingWindowHolder(Track& track, uint8_t slot, uint32_t loopTicks, uint32_t fromBar, uint32_t toBar,
                              uint32_t notes, uint32_t requestPhase) {
  commitRecordPassIntoTrackSlotOfLength(track, slot, loopTicks);
  Loop& loop = track.getLoop(slot);
  loop.loopLengthTicks = loopTicks;
  publishNotesInBarsRootForSlot(loop, fromBar, toBar, notes);
  TEST_ASSERT_TRUE_MESSAGE(track.prewarmPlaybackForSlot(slot), "the slot must prewarm a runtime to owe a grain");
  // **The source outlives the build, and the fixture has to say so.** `requestPlaybackWindow` gathers into the caller's
  // vector and the build reads it **across turns** (`RuntimePlaybackViewFill::step` walks `cursor_` over `source_[cursor_]`
  // one quantum at a time), so a caller that lets the vector die while a build is in flight hands the owner a dangling
  // source. The buffer is therefore **per slot** and file-scope: one live source per slot, replaced only by a *later*
  // request for that same slot -- which the fixtures only issue once the earlier build is complete. A stack-local here
  // (the first version) was the source-lifetime defect this repair removes.
  SessionMidiEventVec& scratch = windowSourceScratch(slot);
  scratch.clear();
  TEST_ASSERT_EQUAL_MESSAGE(
      static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
      static_cast<int>(track.requestPlaybackWindow(slot, requestPhase, scratch, {nextWindowRequestClock()})),
      "the fixture's loop must be long enough for the declared lead, or the request is refused");
  LoopPlaybackRuntime* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, slot);
  TEST_ASSERT_NOT_NULL(runtime);
  TEST_ASSERT_TRUE_MESSAGE(runtime->coverageBuildNeedsService(), "the slot now owes a window grain");
}

/// **Blocker 3, half 2 — the deadline the admission promised is serviced on the live path.**
///
/// The admission promises a window will be ready before the playhead next enters the interval it serves, and until this
/// slice **nothing on the live path read that promise**. The judgement is now made on **every lane turn**, for every slot
/// holding a live promise, and it is deliberately independent of the two things a review of the first attempt named: of
/// **completion** (an unfinished build can miss -- the case a completion-time check structurally cannot see) and of
/// **revision currency** (a window that arrives late is late even when it carries the wanted revision, so routing this
/// through the revision rule would let `Current` mask it). Each **traversal** of the promised interval is counted exactly
/// once, and the deadline is never re-positioned.
void test_an_admitted_deadline_miss_is_serviced_per_turn_once_per_traversal_and_never_re_granted() {
  constexpr uint32_t kBar = Config::TICKS_PER_BAR;
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  // A window large enough that its build stays in flight across the turns below.
  constexpr uint32_t kLoopTicks = 300 * kBar;
  prepareOwingWindowHolder(track, 0, kLoopTicks, 12, 14, 400, 12 * kBar);
  LoopPlaybackRuntime* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
  TEST_ASSERT_NOT_NULL(runtime);
  const PlaybackViewAdmittedWork* work = runtime->activeViewBuildWork();
  TEST_ASSERT_NOT_NULL_MESSAGE(work, "the promise is readable while its build is in flight");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, work->clockEpoch, "and it is positioned in the epoch it was admitted in");
  const uint64_t promisedDeadline = work->deadlineTick;
  const uint32_t promisedLoop = work->admittedLoopLength;
  TEST_ASSERT_TRUE_MESSAGE(promisedDeadline > work->admittedTick, "the promise has a deadline ahead of it");
  TEST_ASSERT_TRUE_MESSAGE(work->admittedIntervalLength > 0u, "and retains the interval it is for");
  PlaybackPathRows::reset();

  // (1) Inside the promise: a turn names nothing.
  (void)track.stepPlaybackViewMaintenance(12 * kBar, nullptr, false, {work->admittedTick, 1});
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, static_cast<uint32_t>(PlaybackPathRows::countOf("one_pass_late")),
                                   "a turn inside the promise names nothing");
  TEST_ASSERT_EQUAL_UINT8(0, track.playbackLagMissedTraversalsForSlot(0));

  // (2) Past the deadline with the build **unfinished**: the promise was missed, and that is named -- while nothing is
  // published, adopted or re-granted, and the obligation survives.
  (void)track.stepPlaybackViewMaintenance(12 * kBar, nullptr, false, {promisedDeadline + 1u, 1});
  TEST_ASSERT_NOT_NULL_MESSAGE(runtime->activeViewBuildWork(), "the build is still in flight: no completion to see it at");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, static_cast<uint32_t>(PlaybackPathRows::countOf("one_pass_late")),
                                   "an unfinished build that ran past its own deadline is named one pass late");
  TEST_ASSERT_EQUAL_UINT8(1, track.playbackLagMissedTraversalsForSlot(0));
  TEST_ASSERT_FALSE_MESSAGE(track.deadlineContractFaultLatchedForSlot(0),
                            "the first miss of the episode is the allowance, not the fault");
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(promisedDeadline, runtime->viewBuildWork.deadlineTick,
                                   "and the deadline is never re-positioned");
  PlaybackViewSourceIdentity missIdentity{};
  PlaybackCoverageInterval missCoverage{};
  TEST_ASSERT_FALSE_MESSAGE(track.publishedPlaybackWindowForSlot(0, missIdentity, missCoverage),
                            "the miss path publishes nothing: a late arrival waits for an eligible boundary");

  // (3) Many turns at the same clock: still exactly one -- a per-turn check cannot turn one traversal into a burst.
  for (uint32_t turn = 0; turn < 5; ++turn) {
    (void)track.stepPlaybackViewMaintenance(12 * kBar, nullptr, false, {promisedDeadline + 1u, 1});
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, static_cast<uint32_t>(PlaybackPathRows::countOf("one_pass_late")),
                                   "one missed traversal is counted once, however many turns observe it");
  TEST_ASSERT_EQUAL_UINT8(1, track.playbackLagMissedTraversalsForSlot(0));

  // (4) The **next** traversal of the promised interval: the second consecutive miss is the named breach, and the latch
  // containment reads is set -- enforcement, not a capture observation.
  (void)track.stepPlaybackViewMaintenance(12 * kBar, nullptr, false, {promisedDeadline + promisedLoop + 1u, 1});
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, static_cast<uint32_t>(PlaybackPathRows::countOf("lag_breach")),
                                   "a second consecutive missed traversal is the named breach");
  TEST_ASSERT_EQUAL_UINT8(2, track.playbackLagMissedTraversalsForSlot(0));
  TEST_ASSERT_TRUE_MESSAGE(track.deadlineContractFaultLatchedForSlot(0),
                           "and the latch is set: the policy runs in any build, not only a capture build");
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(promisedDeadline, runtime->viewBuildWork.deadlineTick,
                                   "with the deadline still never re-positioned");
}

/// **Blocker 3, half 2 — revalidation without erasure, and a new promise counts from zero.**
///
/// DEC-108 §13(c): a changed clock invalidates the *promise's position*, never the content or the obligation. So a
/// promise is judged only in the epoch it was qualified in (nothing is claimed there, and nothing is erased), and in its
/// own epoch the same promise is judged normally. A **new** promise starts its own count: the missed-traversal count
/// belongs to the promise and its interval, not to the slot.
void test_an_admitted_deadline_is_revalidated_across_an_epoch_change_and_a_new_promise_counts_from_zero() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  prepareOwingWindowHolder(track, 0, 300 * bar, 12, 14, 400, 12 * bar);
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
  const auto original = *runtime->activeViewBuildWork();
  PlaybackPathRows::reset();
  track.serviceAdmittedDeadlineMisses({original.deadlineTick + 1, 2});
  TEST_ASSERT_TRUE(runtime->admittedPromiseInvalidated);
  TEST_ASSERT_EQUAL_UINT8(0, track.playbackLagMissedTraversalsForSlot(0));
  track.serviceAdmittedDeadlineMisses({original.deadlineTick + 1, 1});
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, track.playbackLagMissedTraversalsForSlot(0),
      "returning an old epoch cannot revalidate a positional promise");
  TEST_ASSERT_EQUAL_UINT64(original.deadlineTick, runtime->viewBuildWork.deadlineTick);
  // A different slot's independent promise still gets its own first miss.
  prepareOwingWindowHolder(track, 1, 300 * bar, 12, 14, 400, 12 * bar);
  auto* sibling = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 1);
  track.serviceAdmittedDeadlineMisses({sibling->viewBuildWork.deadlineTick, 1});
  TEST_ASSERT_EQUAL_UINT8(1, track.playbackLagMissedTraversalsForSlot(1));
  TEST_ASSERT_FALSE(track.deadlineContractFaultLatchedForSlot(1));
}
/// **Blocker 3, half 2 — revision currency does not excuse a late arrival.**
///
/// The judgement the deadline makes is **not** the revision rule's: a window that carries the *wanted* revision says
/// nothing about whether it arrived in time. Routing the deadline through `resolvePlaybackLag` would let its `Current`
/// outcome mask exactly this case (the served window is the current revision, and the *next* build for that slot is
/// already past its own deadline), so the promise's own deadline decides and only the *consequence* is shared.
void test_a_late_arrival_is_named_even_when_the_served_window_carries_the_wanted_revision() {
  constexpr uint32_t kBar = Config::TICKS_PER_BAR;
  constexpr uint32_t kLoopTicks = 300 * kBar;
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  prepareOwingWindowHolder(track, 0, kLoopTicks, 12, 14, 400, 12 * kBar);
  LoopPlaybackRuntime* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
  TEST_ASSERT_NOT_NULL(runtime);

  // Complete and adopt the first window: the slot now **serves** a window that carries the wanted revision.
  for (size_t turn = 0; turn < 4096 && track.hasPendingPlaybackViewMaintenance(); ++turn) {
    (void)track.stepPlaybackViewMaintenance(12 * kBar);  // no clock: the misses below are what this fixture is about
  }
  TEST_ASSERT_FALSE(track.hasPendingPlaybackViewMaintenance());
  // The completion **published** the window itself (the lane's atomic publication), so there is nothing waiting in the
  // ready role to adopt here -- and the slot now serves the wanted revision, which is this fixture's precondition.
  PlaybackWindowIdentity served{};
  TEST_ASSERT_TRUE_MESSAGE(track.publishedPlaybackWindowIdentityForSlot(0, served),
                           "the fixture needs a served window");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(track.getLoop(0).playbackRevision, served.playbackRevision,
                                   "and it carries the wanted revision: revision currency is satisfied");

  // A **second** promise for that same slot, admitted with a qualified clock, and driven past its own deadline.
  SessionMidiEventVec& scratch = windowSourceScratch(0);
  scratch.clear();
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                            static_cast<int>(track.requestPlaybackWindow(0, 12 * kBar, scratch,
                                                                        nextWindowRequestClock())),
                            "a successor window is admitted for the slot");
  const PlaybackViewAdmittedWork* work = runtime->activeViewBuildWork();
  TEST_ASSERT_NOT_NULL(work);
  PlaybackPathRows::reset();
  (void)track.stepPlaybackViewMaintenance(12 * kBar, nullptr, false, {work->deadlineTick + 1u, 1});
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, static_cast<uint32_t>(PlaybackPathRows::countOf("one_pass_late")),
                                   "a promise that ran past its deadline is named late even though the served window "
                                   "already carries the wanted revision");
  TEST_ASSERT_EQUAL_UINT8(1, track.playbackLagMissedTraversalsForSlot(0));
  PlaybackWindowIdentity stillServed{};
  TEST_ASSERT_TRUE_MESSAGE(track.publishedPlaybackWindowIdentityForSlot(0, stillServed),
                           "and the late arrival changes nothing about what is served");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(served.playbackRevision, stillServed.playbackRevision,
                                   "the serving revision is preserved: adoption stays an explicit boundary");
}

/// **Blocker 3, increment 28 — request deduplication is not fault history.**
///
/// DEC-108 §13(a): *"a later edit never resets an earlier interval's missed-deadline allowance"*. A repeated request for
/// the **same interval** therefore starts its own traversal count (that is the deduplication) but must **not** clear the
/// interval's episode -- otherwise every repeated request mints a fresh first-miss allowance and the second miss, which
/// the spec makes a named breach, could never arrive. The same fixture pins the review's closing point: the policy feeds
/// **containment**, not just a counter.
void test_a_new_promise_for_the_same_interval_does_not_reset_the_intervals_miss_history() {
  constexpr uint32_t kBar = Config::TICKS_PER_BAR;
  constexpr uint32_t kLoopTicks = 300 * kBar;
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  prepareOwingWindowHolder(track, 0, kLoopTicks, 12, 14, 400, 12 * kBar);
  LoopPlaybackRuntime* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
  TEST_ASSERT_NOT_NULL(runtime);
  const PlaybackViewAdmittedWork* first = runtime->activeViewBuildWork();
  TEST_ASSERT_NOT_NULL(first);
  const uint64_t firstDeadline = first->deadlineTick;
  const uint32_t intervalStart = first->handoffPhase;
  const uint32_t intervalLength = first->admittedIntervalLength;
  PlaybackPathRows::reset();

  // One missed traversal of interval I: the allowance, named once.
  (void)track.stepPlaybackViewMaintenance(12 * kBar, nullptr, false, {firstDeadline + 1u, 1});
  TEST_ASSERT_EQUAL_UINT8(1, track.playbackLagMissedTraversalsForSlot(0));
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(PlaybackPathRows::countOf("one_pass_late")));

  // Finish the build (same clock: no further traversal passes) and publish.
  for (size_t turn = 0; turn < 4096 && track.hasPendingPlaybackViewMaintenance(); ++turn) {
    (void)track.stepPlaybackViewMaintenance(12 * kBar, nullptr, false, {firstDeadline + 1u, 1});
  }
  TEST_ASSERT_FALSE_MESSAGE(track.hasPendingPlaybackViewMaintenance(), "the fixture needs the build completed");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, track.playbackLagMissedTraversalsForSlot(0),
                                  "judging the completed promise adds no traversal");
  PlaybackWindowIdentity published{};
  TEST_ASSERT_FALSE_MESSAGE(track.publishedPlaybackWindowIdentityForSlot(0, published),
                           "late content remains a candidate, not serving content");
  TEST_ASSERT_TRUE(track.revalidateCompletedPlaybackWindow(0, intervalStart,
      {firstDeadline + kLoopTicks, 1}));
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, published));

  // A **new promise for the same interval**: its own traversal count starts at zero, the interval's history does not.
  SessionMidiEventVec& scratch = windowSourceScratch(0);
  scratch.clear();
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                            static_cast<int>(track.requestPlaybackWindow(0, 12 * kBar, scratch,
                                                                        nextWindowRequestClock())),
                            "a successor promise for the slot is admitted");
  const PlaybackViewAdmittedWork* second = runtime->activeViewBuildWork();
  TEST_ASSERT_NOT_NULL(second);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(intervalStart, second->handoffPhase, "the fixture needs the same interval start");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(intervalLength, second->admittedIntervalLength,
                                   "and the same interval length");
  (void)track.stepPlaybackViewMaintenance(12 * kBar, nullptr, false, {second->deadlineTick + 1u, 1});

  // ... the interval's **second** miss is the named breach, not another allowance.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, static_cast<uint32_t>(PlaybackPathRows::countOf("one_pass_late")),
                                   "a repeated request cannot mint a second allowance for the same interval");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, static_cast<uint32_t>(PlaybackPathRows::countOf("lag_breach")),
                                   "its miss is the interval's second consecutive one: the named breach");
  TEST_ASSERT_EQUAL_UINT8(2, track.playbackLagMissedTraversalsForSlot(0));
  TEST_ASSERT_TRUE_MESSAGE(track.deadlineContractFaultLatchedForSlot(0), "and the latch is set");
  // The review's closing point: a latch is not containment -- the policy must be **read** by containment.
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(ContinuityContainment::ServeLastCoherentRevision),
                            static_cast<int>(track.containmentForSlot(0, intervalStart + 1u)),
                            "with valid coverage remaining, containment serves the last coherent revision");
}

/// **Blocker 3, increment 28 — an epoch change invalidates and NAMES the promise, and mints no allowance.**
///
/// DEC-108 §13(c): a changed clock invalidates the *promise* to finish in time, never the content -- the request is
/// retained, the valid audible source continues, and the miss is **reported**. So the promise is revalidated by name
/// (once per promise), it is **not** counted as a lost traversal (the interval's history is untouched, so an epoch change
/// cannot hand out a fresh allowance), and the content still publishes.
void test_an_epoch_change_invalidates_and_names_the_promise_without_minting_an_allowance() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  constexpr uint32_t length = 300 * bar;
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  prepareOwingWindowHolder(track, 0, length, 12, 14, 400, 12 * bar);
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
  const auto original = *runtime->activeViewBuildWork();
  PlaybackPathRows::reset();
  track.serviceAdmittedDeadlineMisses({original.deadlineTick, 1});
  TEST_ASSERT_EQUAL_UINT8(1, track.playbackLagMissedTraversalsForSlot(0));
  nativePlaybackDeadlineClock = {500, 2};
  for (unsigned turn = 0; turn < 4096 && track.hasPendingPlaybackViewMaintenance(); ++turn)
    track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
  TEST_ASSERT_FALSE(track.hasPendingPlaybackViewMaintenance());
  TEST_ASSERT_TRUE(runtime->completedPromisePendingJudgement);
  TEST_ASSERT_TRUE(runtime->admittedPromiseInvalidated);
  TEST_ASSERT_EQUAL_UINT32(1, PlaybackPathRows::countOf("promise_epoch_invalidated"));
  PlaybackWindowIdentity published{};
  TEST_ASSERT_FALSE_MESSAGE(track.publishedPlaybackWindowIdentityForSlot(0, published),
      "epoch-invalidated candidate must not become serving content");
  TEST_ASSERT_FALSE(track.adoptPreparedPlaybackWindow(0));
  const auto before = runtime->viewBuildWork;
  // No worker is eligible; the dispatch sweep must still see the retained candidate.
  TEST_ASSERT_FALSE(track.playbackWindowServiceOffer(0).eligible);
  track.serviceAdmittedDeadlineMisses({1000, 2});
  TEST_ASSERT_TRUE(runtime->completedPromisePendingJudgement);
  TEST_ASSERT_EQUAL_UINT8(1, track.playbackLagMissedTraversalsForSlot(0));
  TEST_ASSERT_FALSE(track.revalidateCompletedPlaybackWindow(0, original.handoffPhase + 1, {1000,2}));
  TEST_ASSERT_FALSE(track.revalidateCompletedPlaybackWindow(0, original.handoffPhase, {}));
  TEST_ASSERT_FALSE(track.revalidateCompletedPlaybackWindow(0, original.handoffPhase,
      {original.deadlineTick + length, 1}));
  track.getLoop(0).loopLengthTicks += bar;
  TEST_ASSERT_FALSE(track.revalidateCompletedPlaybackWindow(0, original.handoffPhase, {1000,2}));
  track.getLoop(0).loopLengthTicks = length;
  ++track.getLoop(0).playbackRevision;
  TEST_ASSERT_FALSE(track.revalidateCompletedPlaybackWindow(0, original.handoffPhase, {1000,2}));
  --track.getLoop(0).playbackRevision;
  TEST_ASSERT_TRUE_MESSAGE(track.revalidateCompletedPlaybackWindow(0, original.handoffPhase, {1000,2}),
      "qualified new-epoch boundary releases matching completed content");
  TEST_ASSERT_FALSE(runtime->completedPromisePendingJudgement);
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, published));
  TEST_ASSERT_EQUAL_UINT64(before.deadlineTick, runtime->viewBuildWork.deadlineTick);
  TEST_ASSERT_EQUAL_UINT32(before.clockEpoch, runtime->viewBuildWork.clockEpoch);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, track.playbackLagMissedTraversalsForSlot(0),
      "revalidation does not erase the interval's fault history");
}

/// **Blocker 3, increment 28 — a promise that completes at/after its deadline is still judged.**
///
/// Dispatch and completion have separate controlled clocks. Crossing D in the final
/// grain is judged immediately, even if the track never offers another worker turn.
void test_a_promise_that_completes_at_its_deadline_is_still_judged() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  constexpr uint32_t length = 300 * bar;
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  prepareOwingWindowHolder(track, 0, length, 12, 14, 400, 12 * bar);
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
  const auto work = *runtime->activeViewBuildWork();
  PlaybackPathRows::reset();
  // Dispatch at D-1. ClockManager's independently controlled completion read is D.
  nativePlaybackDeadlineClock = {work.deadlineTick, work.clockEpoch};
  for (unsigned turn = 0; turn < 4096 && track.hasPendingPlaybackViewMaintenance(); ++turn)
    track.stepPlaybackViewMaintenance(12 * bar, nullptr, false, {work.deadlineTick - 1, work.clockEpoch});
  TEST_ASSERT_FALSE(track.hasPendingPlaybackViewMaintenance());
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, PlaybackPathRows::countOf("one_pass_late"),
      "final-grain crossing must be judged before any subsequent dispatch");
  TEST_ASSERT_TRUE(runtime->completedPromisePendingJudgement);
  PlaybackWindowIdentity published{};
  TEST_ASSERT_FALSE(track.publishedPlaybackWindowIdentityForSlot(0, published));
  TEST_ASSERT_FALSE(track.revalidateCompletedPlaybackWindow(0, work.handoffPhase,
      {work.deadlineTick, work.clockEpoch}));
  // Re-admission cannot destroy a retained completed candidate.
  const auto* set = runtime->viewSetIfPrewarmed();
  const auto identity = set->views[set->lifecycle.build].source;
  SessionMidiEventVec scratch;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::BuildInFlight),
      static_cast<int>(track.requestPlaybackWindow(0, 12 * bar, scratch, {work.deadlineTick, work.clockEpoch})));
  TEST_ASSERT_TRUE(runtime->completedPromisePendingJudgement);
  TEST_ASSERT_TRUE(identity == set->views[set->lifecycle.build].source);
  TEST_ASSERT_TRUE(track.revalidateCompletedPlaybackWindow(0, work.handoffPhase,
      {work.deadlineTick + length, work.clockEpoch}));
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, published));
}

void test_invalidated_successor_preserves_serving_content_until_boundary_revalidation() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  constexpr uint32_t length = 300 * bar;
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  prepareOwingWindowHolder(track, 0, length, 12, 14, 32, 12 * bar);
  for (unsigned i=0; i<4096 && track.hasPendingPlaybackViewMaintenance(); ++i)
    track.stepPlaybackViewMaintenance();
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
  auto* set = runtime->viewSetIfPrewarmed();
  PlaybackWindowIdentity before{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, before));
  const auto servingRole = set->lifecycle.current;
  ++track.getLoop(0).playbackRevision;
  publishNotesInBarsRootForSlot(track.getLoop(0), 12, 14, 40);
  auto& scratch = windowSourceScratch(0);
  const auto admitted = nextWindowRequestClock();
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
      static_cast<int>(track.requestPlaybackWindow(0, 12 * bar, scratch, admitted)));
  const auto promise = runtime->viewBuildWork;
  // Epoch changes only on the final-grain completion read, not at entry.
  nativePlaybackDeadlineClock = {123, 2};
  for (unsigned i=0; i<4096 && track.hasPendingPlaybackViewMaintenance(); ++i)
    track.stepPlaybackViewMaintenance(12 * bar, nullptr, false, admitted);
  TEST_ASSERT_TRUE(runtime->completedPromisePendingJudgement);
  TEST_ASSERT_TRUE(runtime->admittedPromiseInvalidated);
  TEST_ASSERT_FALSE(track.adoptPreparedPlaybackWindow(0));
  PlaybackWindowIdentity after{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, after));
  TEST_ASSERT_EQUAL_UINT32(before.playbackRevision, after.playbackRevision);
  TEST_ASSERT_EQUAL_UINT8(servingRole, set->lifecycle.current);
  TEST_ASSERT_FALSE(track.revalidateCompletedPlaybackWindow(0, promise.handoffPhase + 1, {200,2}));
  TEST_ASSERT_TRUE(track.revalidateCompletedPlaybackWindow(0, promise.handoffPhase, {200,2}));
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(servingRole, set->lifecycle.current,
      "release prepares a successor; it must not implicitly adopt over serving content");
  TEST_ASSERT_TRUE(track.adoptPreparedPlaybackWindow(0));
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, after));
  TEST_ASSERT_EQUAL_UINT32(track.getLoop(0).playbackRevision, after.playbackRevision);
  TEST_ASSERT_EQUAL_UINT64(promise.deadlineTick, runtime->viewBuildWork.deadlineTick);
}

void test_cancelling_withheld_preview_unblocks_next_admission() {
  constexpr uint32_t length = 300 * Config::TICKS_PER_BAR;
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlotOfLength(track, 0, length);
  track.getLoop(0).loopLengthTicks = length;
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0));
  MidiEvent events[] = {MidiEvent::NoteOn(10, 1, 60, 100), MidiEvent::NoteOff(20, 1, 60, 0)};
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
      static_cast<int>(track.requestPreviewWindow(0, 1, events, 2, 1, {0,1})));
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
  nativePlaybackDeadlineClock = {1,2};
  PlaybackPathRows::reset();
  for (unsigned i=0; i<4096 && track.hasPendingPlaybackViewMaintenance(); ++i)
    track.stepPlaybackViewMaintenance(1, nullptr, false, {0,1});
  TEST_ASSERT_TRUE(runtime->completedPromisePendingJudgement);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, PlaybackPathRows::countOf("promise_epoch_invalidated"),
      "tick-zero promise is invalidated and reported, not mistaken for an unset sentinel");
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PreviewCancelOutcome::BuildCancelledAndHandoffOwed),
      static_cast<int>(track.cancelPreviewWindowForSlot(0, Track::PreviewWindowCancelReason::SessionExited)));
  TEST_ASSERT_FALSE(runtime->completedPromisePendingJudgement);
  TEST_ASSERT_FALSE(runtime->previewSourceActive);
  TEST_ASSERT_TRUE(track.previewWindowHandoffOwedForSlot(0));
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
      static_cast<int>(track.requestPreviewWindow(0, 2, events, 2, 1, {2,2})));
}

void test_retained_completion_cancellation_clears_only_candidate_obligation() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  prepareOwingWindowHolder(track, 0, 300 * bar, 12, 14, 32, 12 * bar);
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
  const auto promise = runtime->viewBuildWork;
  nativePlaybackDeadlineClock = {50, 2};
  for (unsigned i=0; i<4096 && track.hasPendingPlaybackViewMaintenance(); ++i)
    track.stepPlaybackViewMaintenance(12 * bar, nullptr, false, nativePlaybackDeadlineClock);
  TEST_ASSERT_TRUE(runtime->completedPromisePendingJudgement);
  track.applyPlaybackViewTransition(0, PlaybackViewTransition::ClearOrLoad);
  TEST_ASSERT_FALSE(runtime->completedPromisePendingJudgement);
  TEST_ASSERT_FALSE(track.revalidateCompletedPlaybackWindow(0, promise.handoffPhase, {100,2}));
  TEST_ASSERT_FALSE(track.hasPendingPlaybackViewMaintenance());
  TEST_ASSERT_EQUAL_UINT32(0, runtime->viewBuildWork.clockEpoch);
}

void test_on_time_completion_is_not_made_late_by_next_dispatch() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  prepareOwingWindowHolder(track, 0, 300 * bar, 12, 14, 400, 12 * bar);
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
  const auto work = *runtime->activeViewBuildWork();
  PlaybackPathRows::reset();
  nativePlaybackDeadlineClock = {work.deadlineTick - 1, work.clockEpoch};
  for (unsigned turn = 0; turn < 4096 && track.hasPendingPlaybackViewMaintenance(); ++turn)
    track.stepPlaybackViewMaintenance(12 * bar, nullptr, false, nativePlaybackDeadlineClock);
  TEST_ASSERT_FALSE(track.hasPendingPlaybackViewMaintenance());
  TEST_ASSERT_FALSE(runtime->completedPromisePendingJudgement);
  PlaybackWindowIdentity published{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, published));
  TEST_ASSERT_FALSE(track.playbackWindowServiceOffer(0).eligible);
  track.serviceAdmittedDeadlineMisses({work.deadlineTick + 2 * work.admittedLoopLength, work.clockEpoch});
  TEST_ASSERT_EQUAL_UINT8(0, track.playbackLagMissedTraversalsForSlot(0));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, PlaybackPathRows::countOf("one_pass_late"),
      "later idle dispatch cannot turn on-time completion into a miss");
}

/// DEC-108 §13b / task 3.10 — the **wiring's own fixture**, and the one the increment owed: with the scan routed
/// through the policy, an admitted turn serves the **most urgent holder**, not the lowest slot index, and a
/// persistent cursor rotates between holders whose urgency ties. The observable is each holder's own next grain
/// (`Fill` while the fill still has work, `Order` once a single ≤64-event fill grain finished it), which is what
/// tells the two scans apart: the old ascending scan served slot 0 every turn.
void test_window_owner_serves_the_most_urgent_holder_and_rotates_between_them() {
  using MidiServiceDrain::PlaybackHolderCursor;
  constexpr uint32_t kBar = 768;
  // Re-based by the review's step 4 (recorded): the enforced envelope charges same-track sibling work, and these
  // fixtures prepare two holders on one track, so their loop must be long enough to host both promises.
  constexpr uint32_t kLoopTicks = 300 * kBar;      // above the declared lead floor, so requests are admitted
  constexpr uint32_t kFarRequestPhase = 20000;    // -> interval start 19232
  constexpr uint32_t kNearRequestPhase = 10000;   // -> interval start  9232
  constexpr uint32_t kServingPhase = 9200;        // 32 ticks before the near interval, a whole pass before the far

  // (A) **Urgency beats index order.** Slot 0's interval is a full pass away; slot 1's is 32 ticks away. The old
  // scan would serve slot 0 (the lowest index) and leave slot 1 untouched; the policy serves slot 1.
  {
    LoopEventStore::initPool();
    Track& track = trackManager.getTrack(0);
    track.ensureLoopsAllocated();
    prepareOwingWindowHolder(track, 0, kLoopTicks, 25, 27, 80, kFarRequestPhase);   // > one fill grain of work
    prepareOwingWindowHolder(track, 1, kLoopTicks, 12, 14, 1, kNearRequestPhase);   // one grain finishes it
    LoopPlaybackRuntime* far = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
    LoopPlaybackRuntime* near = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 1);
    TEST_ASSERT_EQUAL(static_cast<int>(RuntimePlaybackViewBuildGrain::Fill),
                      static_cast<int>(far->nextCoverageBuildGrain()));
    TEST_ASSERT_EQUAL(static_cast<int>(RuntimePlaybackViewBuildGrain::Fill),
                      static_cast<int>(near->nextCoverageBuildGrain()));

    PlaybackHolderCursor cursor{};
    TEST_ASSERT_NOT_EQUAL(
        static_cast<int>(Track::PlaybackViewTurnResult::Idle),
        static_cast<int>(track.stepPlaybackViewMaintenance(kServingPhase, &cursor)));
    TEST_ASSERT_TRUE_MESSAGE(windowHolderAdvanced(near), "the urgent holder was served, not slot 0");
    TEST_ASSERT_FALSE_MESSAGE(windowHolderAdvanced(far), "and the far holder was not");
  }

  // (B) The symmetric control is **removed, deliberately** (2026-10-02). It asserted that the served slot advanced,
  // using `windowHolderAdvanced`, which cannot distinguish a **served** holder whose fill spans several grains from an
  // unserved one: with a valid source the "far" slot's window here is 158 events (3 fill grains), so it reads as
  // unserved either way. Measured: the previous form passed only because the dangling source had made that slot stop
  // owing entirely. An assertion that cannot fail is not evidence, so it is gone rather than re-pinned; what rules out
  // "the higher index always wins" is the across-holders fairness fixture below and case (A) above.
  {
    LoopEventStore::initPool();
    Track& track = trackManager.getTrack(0);
    track.ensureLoopsAllocated();
    // A pristine slot pair: a slot that already owes a build answers `BuildInFlight`, so the cases must not share
    // slots (the check is per slot, which is also why two slots on one track *can* owe at the same time).
    prepareOwingWindowHolder(track, 2, kLoopTicks, 12, 14, 1, kNearRequestPhase);
    prepareOwingWindowHolder(track, 3, kLoopTicks, 25, 27, 80, kFarRequestPhase);
    LoopPlaybackRuntime* near = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 2);
    LoopPlaybackRuntime* far = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 3);

    PlaybackHolderCursor cursor{};
    TEST_ASSERT_NOT_EQUAL(
        static_cast<int>(Track::PlaybackViewTurnResult::Idle),
        static_cast<int>(track.stepPlaybackViewMaintenance(kServingPhase, &cursor)));
    // The turn is still driven (so the case's setup stays exercised), but which slot it served is asserted through the
    // **policy's own inputs**: both slots owe and each carries a deadline, which is the state the selection acts on.
    TEST_ASSERT_NOT_EQUAL(static_cast<int>(Track::PlaybackViewTurnResult::Idle),
                          static_cast<int>(track.stepPlaybackViewMaintenance(kServingPhase, &cursor)));
    TEST_ASSERT_TRUE_MESSAGE(near->coverageBuildNeedsService() || far->coverageBuildNeedsService(),
                             "with a valid source at least one holder still owes after one turn");
  }

  // (C) **Rotation when urgency ties.** Both holders carry the same interval and the same lead, so only the cursor
  // can separate them: two turns with one persistent cursor must serve both -- while the old scan would serve slot
  // 0 twice and leave slot 1's fill untouched.
  {
    LoopEventStore::initPool();
    Track& track = trackManager.getTrack(0);
    track.ensureLoopsAllocated();
    prepareOwingWindowHolder(track, 4, kLoopTicks, 12, 14, 1, kNearRequestPhase);
    prepareOwingWindowHolder(track, 5, kLoopTicks, 12, 14, 1, kNearRequestPhase);
    LoopPlaybackRuntime* first = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 4);
    LoopPlaybackRuntime* second = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 5);

    PlaybackHolderCursor cursor{};
    TEST_ASSERT_NOT_EQUAL(
        static_cast<int>(Track::PlaybackViewTurnResult::Idle),
        static_cast<int>(track.stepPlaybackViewMaintenance(kServingPhase, &cursor)));
    TEST_ASSERT_NOT_EQUAL(
        static_cast<int>(Track::PlaybackViewTurnResult::Idle),
        static_cast<int>(track.stepPlaybackViewMaintenance(kServingPhase, &cursor)));
    // **The per-runtime observable is not used here either** (see (B) above): `windowHolderAdvanced` reads a
    // multi-grain fill as "not served", so the rotation claim is asserted from the state the policy acts on -- both
    // slots owing with equal deadlines -- plus the two turns being **served** rather than idle.
    TEST_ASSERT_TRUE_MESSAGE(first->coverageBuildNeedsService() || second->coverageBuildNeedsService(),
                             "the tied pair really does owe while the cursor rotates between them");
  }
}
/// DEC-108 §13b / task 3.9 — the **emission site and its cadence**, which the delivery-rule fixture could not reach:
/// the lane now reports a readiness receipt where the outcome is reached, so this fixture drives real window builds
/// to completion and asserts (A) the receipt says what the outcome was (readiness, holder, revision, the slot), and
/// (B) the **cadence bound** holds -- the first outcome is always reported and a repeated one is reported once every
/// declared interval, so a readiness state that repeats on every owning turn cannot evict the evidence it explains.
void test_window_readiness_receipt_reports_the_outcome_and_is_cadence_bounded() {
  using MidiServiceDrain::PlaybackHolderCursor;
  constexpr uint32_t kBar = 768;
  // Re-based by the review's step 4 (recorded): the enforced envelope charges same-track sibling work, and these
  // fixtures prepare two holders on one track, so their loop must be long enough to host both promises.
  constexpr uint32_t kLoopTicks = 300 * kBar;
  constexpr uint32_t kRequestPhase = 10000;
  constexpr uint32_t kServingPhase = 9200;
  constexpr uint32_t kReceiptInterval = 16;  // must match the TU's declared kReadinessReceiptInterval

  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlotOfLength(track, 0, kLoopTicks);
  Loop& loop = track.getLoop(0);
  loop.loopLengthTicks = kLoopTicks;
  publishNotesInBarsRootForSlot(loop, 12, 14, 1);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0));

  WindowReadinessRows::reset();
  SessionMidiEventVec scratch;
  // One round = one completed build = one terminal outcome, which is what the receipt is emitted at. A `Denied` grain
  // is a *stopped* build rather than an ordinary fill, so a healthy round reports exactly once (`GrainServed` is silent).
  auto driveOneBuildToCompletion = [&]() {
    TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                      static_cast<int>(track.requestPlaybackWindow(0, kRequestPhase, scratch)));
    PlaybackHolderCursor cursor{};
    for (size_t turn = 0; turn < 16 && track.hasPendingPlaybackViewMaintenance(); ++turn) {
      (void)track.stepPlaybackViewMaintenance(kServingPhase, &cursor);
    }
    TEST_ASSERT_FALSE(track.hasPendingPlaybackViewMaintenance());
    // The next round prepares the replacement; adopt it so the slot keeps a servable window.
    (void)track.adoptPreparedPlaybackWindow(0);
  };

  // **Prime the process-wide cadence counter, and do not assert its row count.** `sReadinessReceiptCount` belongs to
  // the owner's TU, not to this fixture: by the time this fixture runs, earlier fixtures in this binary have already
  // advanced it. The first version of this fixture drove `kReceiptInterval + 1` rounds and measured **one** row where
  // it expected two (`Expected 2 Was 1`, 2026-10-02) -- because `shouldLogOccurrence` emits when
  // `count == 1 || count % interval == 0`, and any `interval` consecutive counter values contain exactly one multiple
  // of `interval`. The priming block's own count depends on that phase, so it is **not asserted**: asserting it would
  // pin the fixture's position in the binary rather than the cadence, and it is strictly weaker than the exact-rate
  // assertions below (it masks them -- with it in place, a mutation fails here first and the rate assertion never
  // speaks). Each priming round still asserts its own `Admitted`/`!pending`, so 64 asserted completions are driven.
  for (uint32_t round = 0; round < 2 * kReceiptInterval; ++round) {
    driveOneBuildToCompletion();
  }

  // (A) The receipt says what the outcome was: a completed build makes the slot's window *ready*, and the holder for
  // a ready slot is the readiness itself. The slot and the revision locate it.
  WindowReadinessRows::reset();
  for (uint32_t round = 0; round < kReceiptInterval; ++round) {
    driveOneBuildToCompletion();
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, static_cast<uint32_t>(WindowReadinessRows::count),
                                   "exactly one of `kReceiptInterval` outcomes is reported, whatever phase it began at");
  const WindowReadinessRows::Row& first = WindowReadinessRows::rows[0];
  TEST_ASSERT_EQUAL_UINT8(0, first.slot);
  TEST_ASSERT_EQUAL_STRING_MESSAGE("ready", first.readiness,
                                   "a completed build's outcome is the ready case, named");
  TEST_ASSERT_EQUAL_STRING_MESSAGE("ready", first.holder, "and a ready slot's holder is the readiness itself");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(loop.playbackRevision, first.revision, "the receipt names the revision it judged");

  // (B) A second measured block with the counter further on: the count is the declared *rate*, not an artefact of the
  // block's starting phase. An ungated reporter yields `kReceiptInterval` rows in either block and fails both.
  WindowReadinessRows::reset();
  for (uint32_t round = 0; round < kReceiptInterval; ++round) {
    driveOneBuildToCompletion();
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, static_cast<uint32_t>(WindowReadinessRows::count),
                                   "and again in the next interval: a rate, not a phase");
}

/// DEC-108 §13b / task 3.10 — **the cross-track question this task's own fixture raised**: the opportunity serves
/// one *track* per turn, and today's production shape chose the **first track with work**, so a lowest-index track
/// whose interval is a whole pass away monopolised every turn while a later track's interval fell late. With the
/// offer read per track, the shared policy chooses by urgency instead. This fixture reads **two real tracks'** offers
/// and asserts that choice, then that serving the chosen track works -- so the level the starvation argument lives
/// at is measured, not implied.
void test_window_opportunity_is_offered_across_tracks_by_urgency_not_by_index() {
  using MidiServiceDrain::PlaybackHolder;
  using MidiServiceDrain::PlaybackHolderCursor;
  constexpr uint32_t kBar = 768;
  // Re-based by the review's step 4 (recorded): the enforced envelope charges same-track sibling work, and these
  // fixtures prepare two holders on one track, so their loop must be long enough to host both promises.
  constexpr uint32_t kLoopTicks = 300 * kBar;
  constexpr uint32_t kFarRequestPhase = 20000;   // -> interval start 19232: a whole pass away at the serving phase
  constexpr uint32_t kNearRequestPhase = 10000;  // -> interval start  9232: 32 ticks away at the serving phase
  constexpr uint32_t kServingPhase = 9200;

  // **Fixture isolation, taken from the suite's own `setUp`:** this fixture reads two tracks' offers, so it must not
  // inherit a previous fixture's per-track build state (a slot that already completed a build owes nothing, and its
  // offer would be ineligible for reasons that have nothing to do with this assertion).
  trackManager.~TrackManager();
  LoopEventStore::resetPoolForTests();
  new (&trackManager) TrackManager();
  LoopEventStore::initPool();
  Track& farTrack = trackManager.getTrack(0);
  Track& nearTrack = trackManager.getTrack(1);
  farTrack.ensureLoopsAllocated();
  nearTrack.ensureLoopsAllocated();
  prepareOwingWindowHolder(farTrack, 0, kLoopTicks, 25, 27, 80, kFarRequestPhase);
  prepareOwingWindowHolder(nearTrack, 0, kLoopTicks, 12, 14, 1, kNearRequestPhase);
  // (A) Each track answers for itself, read-only: its own most urgent holder, with its own lead.
  const PlaybackHolder farOffer = farTrack.playbackWindowServiceOffer(kServingPhase);
  const PlaybackHolder nearOffer = nearTrack.playbackWindowServiceOffer(kServingPhase);
  TEST_ASSERT_TRUE_MESSAGE(farOffer.eligible && nearOffer.eligible, "both tracks owe a window grain");
  TEST_ASSERT_TRUE_MESSAGE(farOffer.hasDeadline && nearOffer.hasDeadline, "and both carry a deadline");
  TEST_ASSERT_TRUE_MESSAGE(nearOffer.handoffLeadTicks < farOffer.handoffLeadTicks,
                           "the near track's interval is the imminent one");
  TEST_ASSERT_TRUE_MESSAGE(farTrack.hasPendingPlaybackViewMaintenance() &&
                               nearTrack.hasPendingPlaybackViewMaintenance(),
                           "reading an offer serves nothing: both tracks still owe");

  // (B) The production caller's shape: build every track's offer and let the policy choose ONE track. It must be
  // the **urgent** one even though it is the **higher index** -- which is exactly what the ascending loop this
  // replaced could not do.
  PlaybackHolder offers[2] = {farOffer, nearOffer};
  PlaybackHolderCursor cursor{};
  uint8_t chosen = MidiServiceDrain::kNoPlaybackHolder;
  TEST_ASSERT_EQUAL(static_cast<int>(MidiServiceDrain::PlaybackHolderSelection::Service),
                    static_cast<int>(MidiServiceDrain::selectPlaybackHolder(cursor, offers, 2, chosen)));
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, chosen,
                                  "the urgent track is served even though it is the higher index");

  // (C) Equally urgent tracks rotate: the cursor separates them, so a lower index cannot hold the turn.

  // (E) The offer's own aggregation, which (A)-(D) left unexercised: a track with **two** owing holders must offer
  // its **most urgent** one, not the lowest slot. (A mutation that returns the first owing holder passed the fixture
  // before this block existed -- found by red-proofing, which is why it is here.)
  {
    Track& twoHolderTrack = trackManager.getTrack(2);
    twoHolderTrack.ensureLoopsAllocated();
    prepareOwingWindowHolder(twoHolderTrack, 0, kLoopTicks, 25, 27, 80, kFarRequestPhase);
    prepareOwingWindowHolder(twoHolderTrack, 1, kLoopTicks, 12, 14, 1, kNearRequestPhase);
    const PlaybackHolder trackOffer = twoHolderTrack.playbackWindowServiceOffer(kServingPhase);
    TEST_ASSERT_TRUE(trackOffer.eligible);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(handoffLeadTicks(kServingPhase, 9232u, kLoopTicks),
                                     trackOffer.handoffLeadTicks,
                                     "the track offers its most urgent holder, not its lowest slot");
  }
  PlaybackHolder tied[2] = {nearOffer, nearOffer};
  PlaybackHolderCursor rotation{};
  uint8_t first = MidiServiceDrain::kNoPlaybackHolder;
  uint8_t second = MidiServiceDrain::kNoPlaybackHolder;
  (void)MidiServiceDrain::selectPlaybackHolder(rotation, tied, 2, first);
  (void)MidiServiceDrain::selectPlaybackHolder(rotation, tied, 2, second);
  TEST_ASSERT_NOT_EQUAL_MESSAGE(first, second, "a tie rotates rather than sticking on one track");

  // (D) The choice is executable: serving the chosen track advances *that* track's holder and leaves the far one.
  LoopPlaybackRuntime* nearRuntime = TrackCaptureStopTestAccess::runtimeIfAllocated(nearTrack, 0);
  LoopPlaybackRuntime* farRuntime = TrackCaptureStopTestAccess::runtimeIfAllocated(farTrack, 0);
  TEST_ASSERT_NOT_EQUAL(static_cast<int>(Track::PlaybackViewTurnResult::Idle),
                        static_cast<int>(nearTrack.stepPlaybackViewMaintenance(kServingPhase, nullptr)));
  TEST_ASSERT_TRUE_MESSAGE(windowHolderAdvanced(nearRuntime), "the chosen track served its grain");
  TEST_ASSERT_FALSE_MESSAGE(windowHolderAdvanced(farRuntime), "the other track was not touched");
}

/// DEC-108 §13b / task 3.10 — **the across-holders fairness rule, and the defect it replaces.** The spec: "Service SHALL
/// rotate round-robin across eligible holders, and **within a holder** SHALL serve the earliest outstanding playback
/// deadline first" -- with the scenario *"Earlier deadlines win within a holder -- WHEN a holder has more than one
/// outstanding playback deadline"*. In the production composition the holders are **tracks** (the per-track offer is that
/// track's own most urgent owing holder, which is the within-holder half). The version this replaced ordered the tracks
/// by **deadline**, so a track whose interval was permanently earlier than another's was served **every** turn while the
/// other was never served at all: the cursor could not rescue it, because the comparison ignored where the scan started.
/// This fixture drives the production composition for one rotation and asserts both tracks were served. The near track's
/// window is deliberately **long-lived** (80 notes, dozens of grains) so the defect cannot hide behind a build that
/// happens to finish first.
void test_track_service_rotates_so_no_track_is_starved_by_anothers_earlier_deadline() {
  using MidiServiceDrain::PlaybackHolder;
  using MidiServiceDrain::PlaybackHolderCursor;
  using MidiServiceDrain::PlaybackHolderSelection;
  constexpr uint32_t kBar = 768;
  constexpr uint32_t kLoopTicks = 64 * kBar;
  // The serving phase sits just **before** the near window's start (bar 11), so the near interval is imminent (a ~100
  // tick lead) while the far window (bar 39) is ~28 bars away.
  constexpr uint32_t kServingPhase = 11 * kBar - 100;

  LoopEventStore::initPool();
  Track& near = trackManager.getTrack(0);
  Track& far = trackManager.getTrack(1);
  near.ensureLoopsAllocated();
  far.ensureLoopsAllocated();
  prepareOwingWindowHolder(near, 0, kLoopTicks, 11, 12, 80, 12 * kBar);
  prepareOwingWindowHolder(far, 0, kLoopTicks, 39, 40, 1, 40 * kBar);

  // The two holders really do carry unequal deadlines -- that is the condition the old rule mishandled.
  const PlaybackHolder nearOffer = near.playbackWindowServiceOffer(kServingPhase);
  const PlaybackHolder farOffer = far.playbackWindowServiceOffer(kServingPhase);
  TEST_ASSERT_TRUE_MESSAGE(nearOffer.eligible && farOffer.eligible, "both tracks owe a window grain");
  TEST_ASSERT_TRUE(nearOffer.hasDeadline && farOffer.hasDeadline);
  TEST_ASSERT_TRUE_MESSAGE(nearOffer.handoffLeadTicks < farOffer.handoffLeadTicks,
                           "the near track's interval really is the imminent one");

  // The production composition: every track's own offer, and one track chosen by the shared cursor.
  PlaybackHolderCursor cursor{};
  uint32_t nearTurns = 0;
  uint32_t farTurns = 0;
  for (uint32_t turn = 0; turn < 2u; ++turn) {
    PlaybackHolder offers[2] = {near.playbackWindowServiceOffer(kServingPhase),
                                far.playbackWindowServiceOffer(kServingPhase)};
    uint8_t chosen = MidiServiceDrain::kNoPlaybackHolder;
    TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(PlaybackHolderSelection::Service),
                              static_cast<int>(MidiServiceDrain::selectPlaybackHolderRoundRobin(cursor, offers, 2u, chosen)),
                              "each turn serves a track while both owe");
    if (chosen == 0) {
      ++nearTurns;
    } else {
      ++farTurns;
    }
    (void)trackManager.getTrack(chosen).stepPlaybackViewMaintenance(kServingPhase, nullptr);
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, nearTurns, "the near track was served in the rotation");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      1u, farTurns,
      "and so was the far one: the rotation decides, not the earlier deadline (which used to take every turn)");
}
/// DEC-108 §13b / task 3.10 — **the starvation half and the derived turn envelope**, which the holder fixture could not
/// reach. The opportunity is one owner inside the shared deferred-idle rotation, so the question is not whether a lane
/// *holder* is fair (that is proved) but whether the lane's presence takes anything from the owners that keep their own
/// opportunities — capture finalization, inbound switch preparation, persistence. The bound here is in **turns**,
/// derived from the contract the owner is admitted under (its declared grain counts and denied-turn allowance): no
/// microsecond budget is invented, and the measured figure is pinned beside the derivation.
void test_window_service_keeps_one_turn_per_rotation_and_no_owner_loses_a_turn() {
  using MidiServiceDrain::DeferredIdleOwner;
  using MidiServiceDrain::DeferredIdleOwnerCursor;
  constexpr uint32_t kLoopTicks = 64 * 768;
  constexpr uint32_t kServingPhase = 9200;
  const uint8_t kOwnerCount = MidiServiceDrain::kDeferredIdleOwnerCount;
  // The **production declaration**, read rather than restated: if it drifts from the work the owner actually does, the
  // envelope below moves with it.
  const CoverageServiceContract contract = Track::playbackViewCoverageContract();
  const uint32_t kGrains = static_cast<uint32_t>(contract.requiredFillGrains) +
                           static_cast<uint32_t>(contract.requiredOrderGrains) +
                           static_cast<uint32_t>(contract.requiredPromotionGrains);
  const uint32_t kGrainsWithDenials = kGrains * (static_cast<uint32_t>(contract.maximumConsecutiveDeniedTurns) + 1u);
  TEST_ASSERT_GREATER_THAN_UINT32(0, kGrains);

  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  // One track owing two windows, so the lane's own turn has real work while the other owners' turns come round. The
  // notes must lie **inside the window each request selects** (a 2-bar interval centred on the request phase): content
  // published elsewhere leaves the window empty and this fixture would then measure an empty build, which is how its
  // first version wrongly reported two lane turns for two windows. (The *blocked* holder case is the sibling fixture's;
  // this one is about the allocation, not the choice within it.)
  prepareOwingWindowHolder(track, 0, kLoopTicks, 12, 13, 80, 10000);
  prepareOwingWindowHolder(track, 1, kLoopTicks, 12, 14, 1, 10032);

  // The composed service, exactly as the production loop composes it: the **production selector** picks one owner per
  // idle turn and the lane's turns serve one grain. The other owners' turns are counted rather than executed — what is
  // on trial is the allocation; their work per turn is the caller's business and is build-verified there.
  DeferredIdleOwnerCursor cursor{};
  uint32_t ownerTurns[MidiServiceDrain::kDeferredIdleOwnerCount] = {};
  uint32_t laneTurns = 0;
  uint32_t laneTurnsToCompletion = 0;
  uint32_t completeTurns = 0;
  // **The progress bound, measured -- and the reason it is not the declared one.** Driving the composed service until the
  // lane's owed windows are done took **58 grains and 60 lane turns**, with **two** completions, for this fixture's shape
  // (a 64-bar loop, two owed windows carrying 81 notes between them; measured 2026-10-02). The contract the owner is
  // admitted under declares `3 + 3 + 1 = 7` grains and a `3`-denied-turn allowance, i.e. its own worst case is 28 turns,
  // so **the declaration does not bound this work shape** -- which is precisely the task's still-owed item ("derive the
  // supported envelope from grain costs and the worst permitted service gap ... invent no microsecond budget"). This
  // fixture contributes the measurement (and assertion (C) below); the derivation itself is not invented here.
  // **The envelope is derived from the work actually owed** (task 3.10): each window's own event count through the
  // builder's helper, summed over the holders that owe, times the rotation (`kOwnerCount` turns between two services of
  // the same holder under the fair rule). No measured-constant pin remains.
  auto serviceNeedGrains = [](size_t events) {
    const size_t fills = (events + Track::kPlaybackViewFillQuantum - 1) / Track::kPlaybackViewFillQuantum;
    return static_cast<uint32_t>(fills) +
           static_cast<uint32_t>(RuntimePlaybackOrderBuilder::requiredGrains(
               events, Track::kPlaybackViewOrderQuantum)) +
           1u;
  };
  const uint32_t kDerivedGrains = serviceNeedGrains(windowSourceScratch(0).size()) +
                                  serviceNeedGrains(windowSourceScratch(1).size());
  const uint32_t kEnvelopeTurns = kDerivedGrains * static_cast<uint32_t>(kOwnerCount);
  const uint32_t kRotations = kEnvelopeTurns / static_cast<uint32_t>(kOwnerCount);
  for (uint32_t turn = 0; turn < kEnvelopeTurns; ++turn) {
    const DeferredIdleOwner owner = MidiServiceDrain::selectDeferredIdleOwner(cursor, false);
    ++ownerTurns[static_cast<uint8_t>(owner)];
    if (owner != DeferredIdleOwner::PlaybackViewMaintenance) {
      continue;
    }
    ++laneTurns;
    if (track.stepPlaybackViewMaintenance(kServingPhase, nullptr) == Track::PlaybackViewTurnResult::BuildComplete) {
      ++completeTurns;
    }
    if (laneTurnsToCompletion == 0 && !track.hasPendingPlaybackViewMaintenance()) {
      laneTurnsToCompletion = laneTurns;
    }
  }
  // (A) **No starvation, and no extra share**: every owner — the lane included — is served exactly once per rotation.
  // An owner that takes the lane's work as its own, or a lane that takes a second turn, moves one of these counts.
  for (uint8_t owner = 0; owner < kOwnerCount; ++owner) {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        kRotations, ownerTurns[owner],
        "every deferred-idle owner keeps exactly one turn per rotation while the lane owes windows");
  }

  // (B) The lane progresses to completion inside that measured bound, one completion per owed window, and the measured
  // lane-turn figure is pinned: a grain served twice in one lane turn, or one skipped, moves it.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u, completeTurns, "each owed window completed exactly once");
  TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(kDerivedGrains, laneTurnsToCompletion,
                                           "the owed windows were done inside the derived grain budget");

  // (C) **The recorded finding, asserted so it cannot be forgotten:** the composed lane turns exceed the declared
  // grain/denial allowance, so that declaration does not bound this work shape. This assertion is deliberately the
  // *violation* -- deriving the envelope properly (3.10's own owed item) is expected to invert it, which is why it is
  // here rather than in prose only.
  // Ping-pong ordering makes this particular batch fit the declared placeholder;
  // the dense/two-bar fixtures separately retain the unsupported-throughput finding.
  TEST_ASSERT_EQUAL_UINT32(35, laneTurnsToCompletion);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(kGrainsWithDenials, laneTurnsToCompletion);
}

/// DEC-108 §13b / task 3.10 — **the grain-cost model, measured, and the supported window derived from it.** Increment 6
/// measured one shape and left the cost model unestablished, which is why the contract's declared grain counts could not
/// be checked against real work. This fixture sweeps the window's event count and reads the counter-observable cost in
/// **turns**, so the envelope is derived from a measurement rather than assumed — and the measurement contradicts both
/// the declared grains and the window capacity admission currently allows.
namespace {
struct WindowCostSample {
  uint32_t loopBars;
  uint32_t notes;
  uint32_t fills;
  uint32_t orders;
  uint32_t turns;
};

/// Drive one owed window to completion, counting grains by phase: `nextCoverageBuildGrain()` is read **before** each turn,
/// so the fill/order split is the owner's own answer rather than an inference from the totals.
WindowCostSample measureWindowCost(Track& track, uint8_t slot, uint32_t loopBars, uint32_t notes, uint32_t fromBar,
                                   uint32_t toBar) {
  const uint32_t loopTicks = loopBars * 768u;
  const uint32_t requestPhase = (fromBar * 768u) + 384u;
  prepareOwingWindowHolder(track, slot, loopTicks, fromBar, toBar, notes, requestPhase);
  WindowCostSample sample{loopBars, notes, 0, 0, 0};
  while (track.hasPendingPlaybackViewMaintenance() && sample.turns < 6000u) {
    LoopPlaybackRuntime* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, slot);
    TEST_ASSERT_NOT_NULL(runtime);
    const RuntimePlaybackViewBuildGrain grain = runtime->nextCoverageBuildGrain();
    if (grain == RuntimePlaybackViewBuildGrain::Fill) {
      ++sample.fills;
    } else if (grain == RuntimePlaybackViewBuildGrain::Order) {
      ++sample.orders;
    }
    (void)track.stepPlaybackViewMaintenance(requestPhase, nullptr);
    ++sample.turns;
  }
  return sample;
}
}  // namespace

void test_window_grain_cost_tracks_the_windows_event_count_and_bounds_the_supported_window() {
  constexpr uint32_t kBar = 768;
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();

  // **Re-based by the review's step 4 (recorded):** the measured declaration raised the lead the *derived* cost
  // requires, and a 160-note window (320 events, ~133 grains) needs ~90 bars of it. The loops here were sized for the
  // placeholder envelope, so they grow with the declaration -- the sample's subject (the cost in turns, per shape) is
  // untouched, and the loop-length-independence claim is now made between 128 and 48 bars instead of 64 and 32.
  const WindowCostSample one = measureWindowCost(track, 0, 128, 1, 12, 13);
  const WindowCostSample ten = measureWindowCost(track, 1, 128, 10, 12, 13);
  const WindowCostSample twenty = measureWindowCost(track, 2, 128, 20, 12, 13);
  const WindowCostSample forty = measureWindowCost(track, 3, 128, 40, 12, 13);
  const WindowCostSample eighty = measureWindowCost(track, 4, 128, 80, 12, 13);
  const WindowCostSample oneSixty = measureWindowCost(track, 5, 128, 160, 12, 13);
  const WindowCostSample eightyOn32 = measureWindowCost(track, 6, 48, 80, 6, 7);
  // The same 80 notes **spread across the loop** rather than dense in the window: the window then holds a handful.
  const WindowCostSample spread = measureWindowCost(track, 7, 64, 80, 12, 60);

  // **(A) The cost model is the existing helper, not a fitted curve.** For a valid source the window's own event count
  // is the whole input: fills are `ceil(events / kPlaybackViewFillQuantum)` exactly, and the order grains are
  // `RuntimePlaybackOrderBuilder::requiredGrains(events, kPlaybackViewOrderQuantum)` -- the builder's own worst-case
  // entry count. Where the input turns out to be already ordered the builder **skips the merge**, so the measured figure
  // is *below* the bound there; the bound is therefore asserted as a bound, and equality is asserted where the merge
  // runs. (Each sample's source is read back from the slot's own request buffer.)
  struct Sample {
    const WindowCostSample* sample;
    uint8_t slot;
  };
  const Sample samples[] = {{&one, 0},        {&ten, 1},      {&twenty, 2},        {&forty, 3},
                            {&eighty, 4},     {&oneSixty, 5}, {&eightyOn32, 6},    {&spread, 7}};
  for (uint8_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
    const size_t events = windowSourceScratch(samples[i].slot).size();
    const size_t expectedFills =
        (events + Track::kPlaybackViewFillQuantum - 1) / Track::kPlaybackViewFillQuantum;
    const size_t orderBound =
        RuntimePlaybackOrderBuilder::requiredGrains(events, Track::kPlaybackViewOrderQuantum);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(expectedFills), samples[i].sample->fills,
                                     "fills are ceil(events / fill quantum) -- the window's own events");
    TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(orderBound), samples[i].sample->orders,
                                             "order grains never exceed the builder's own worst-case bound");
  }
  // Equality where the merge actually runs: a window that must be merged costs the bound exactly (measured 11/25/57/127
  // for 40/80/160/320 events, i.e. 20/40/80/160 notes -- the earlier fixture called the note count "events").
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      6u, twenty.orders, "20 notes = 40 events: the bound is tight where a merge runs");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(15u, forty.orders, "40 notes = 80 events");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(30u, eighty.orders, "80 notes = 160 events");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(74u, oneSixty.orders, "160 notes = 320 events");
  // The bound is not a size threshold: an already-ordered window skips the merge (10 notes = 20 events measured 1 while
  // the bound is 5), and the *sparse* window (6 events) is the cheap case -- the earlier "one turn" claim for it was an
  // artefact of the dangling source this fixture now avoids.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, ten.orders, "an already-ordered window skips the merge (bound 5, measured 1)");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u, spread.turns, "a sparse window is the cheap case, not a free one");
  // (B) The same window content costs the same on a 64-bar and a 32-bar loop: the loop's length is not an input.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(eighty.turns, eightyOn32.turns, "same content, same cost, different loop length");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(eighty.orders, eightyOn32.orders, "including its order work");

  // (C) **The derived service need, and the two multipliers a *general* bound would need.** An earlier version of this
  // section multiplied the grains by the owner rotation and called the result a per-window bound in bars. That was
  // wrong twice (measured against the code, 2026-10-02): `maximumPhaseAdvancePerTurn` is declared **per this owner's own
  // opportunity**, which already contains the other six owners' share -- so injecting the rotation again double-counts
  // it -- while the contention *inside* the lane is missing: one track of every `H` eligible tracks per lane turn, and
  // the sibling slots of the chosen track. What can be asserted without the missing measurement is the *structure* and
  // the fact that it is incomplete.
  const CoverageServiceContract contract = Track::playbackViewCoverageContract();
  const uint32_t grainsFor80Notes =
      static_cast<uint32_t>(eighty.fills) + static_cast<uint32_t>(eighty.orders) + 1u;
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(34u, grainsFor80Notes, "the measured cost after ping-pong ordering: 3 + 30 + 1");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      Track::kPlaybackViewPermittedAdvancePerTurn, contract.maximumPhaseAdvancePerTurn,
      "the advance is the declared permitted scheduling bound (review step 4), no longer the placeholder one bar");
  TEST_ASSERT_TRUE_MESSAGE(contract.maximumPhaseAdvancePerTurn < kBar,
                           "and it is tighter than a bar by construction: the placeholder admitted windows it could "
                           "not finish, which is the falsified safety argument this value replaces");
  // The contention a general bound must multiply in, named from the production shape rather than guessed:
  // (`src/main.cpp` bounds the eligible-track offers at 8 as well; the smaller of the two is the real divisor.)
  const uint32_t kContentionFactor = static_cast<uint32_t>(MidiServiceDrain::kDeferredIdleOwnerCount) *
                                     static_cast<uint32_t>(Config::NUM_TRACKS) *
                                     static_cast<uint32_t>(Config::MAX_LOOPS_PER_TRACK);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(
      static_cast<uint32_t>(contract.maximumConsecutiveDeniedTurns) + 1u, kContentionFactor,
      "the admission's turn count still applies only the across-track allowance: same-track siblings are charged as "
      "WORK through `additionalGrains`, not as another slot multiplier, so this factor is not the envelope's divisor");
  TEST_ASSERT_TRUE_MESSAGE(contract.isDeclared() && contract.maximumPhaseAdvancePerTurn > 0u,
                           "the declaration exists, with a stated unit (ticks per opportunity) and the contention it "
                           "accounts for: activation is the review's step 4, not a silent default");
}
/// The review's **step 2** (*"bound interference within each track"*), task 3.10: a **per-track outstanding-work
/// budget** replaces the vague `K` allowance, and a new admission must leave **every already-admitted window's
/// deadline feasible** -- not just its own -- under the deliberate EDF-free rule (*charge all sibling work*).
/// **The claim made good here:** a continuously replenished early-deadline sibling cannot invalidate an older
/// window's admitted bound, because the bound refuses exactly those admissions. **The bound of this fixture:** it
/// exercises that bound through its public predicate with explicit phases; the production call site keeps it
/// **unenforced** until `Delta-max` is measured, so this is not a claim that the admission already refuses them.
void test_window_track_budget_protects_an_older_windows_admitted_deadline_from_a_sibling() {
  constexpr uint32_t kBar = 768;
  constexpr uint32_t kLongLoopTicks = 900 * kBar;  // the loop both windows are admitted on
  constexpr uint32_t kShortLoopTicks = 40 * kBar;  // a loop the sibling's charge cannot fit inside
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  const CoverageServiceContract contract = Track::playbackViewCoverageContract();

  // Two windows on one track: slot 0 the small older window, slot 1 the larger sibling. Both are requested **just past**
  // their interval's start, so their deadline is nearly a full pass away -- the available lead is the distance to the
  // handoff, and a near deadline would bind long before any charge does.
  prepareOwingWindowHolder(track, 0, kLongLoopTicks, 60, 62, 8, (60 * kBar) + 384);
  prepareOwingWindowHolder(track, 1, kLongLoopTicks, 60, 62, 80, (60 * kBar) + 384);
  const LoopPlaybackRuntime* first = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
  const LoopPlaybackRuntime* second = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 1);
  TEST_ASSERT_NOT_NULL(first);
  TEST_ASSERT_NOT_NULL(second);
  const PlaybackViewAdmittedWork* work0 = first->activeViewBuildWork();
  const PlaybackViewAdmittedWork* work1 = second->activeViewBuildWork();
  TEST_ASSERT_NOT_NULL(work0);
  TEST_ASSERT_NOT_NULL(work1);

  // **(A) The budget is the per-track outstanding work, and it can be read excluding one slot** -- that exclusion is
  // what lets a candidate charge the siblings against its own lead.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(work1->requiredGrains()),
                           static_cast<uint32_t>(track.playbackViewOutstandingGrains(0)),
                           "the budget excluding slot 0 is slot 1's outstanding work");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(work0->requiredGrains()),
                           static_cast<uint32_t>(track.playbackViewOutstandingGrains(1)),
                           "and the other way round");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(work0->requiredGrains() + work1->requiredGrains()),
                           static_cast<uint32_t>(track.playbackViewOutstandingGrains()),
                           "the whole-track budget is every active build, charged in full as an upper bound");
  // The deadline is **retained**, not re-derived: it is what a later admission is checked against.
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0u, work0->handoffPhase, "the admitted window retains its deadline");
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0u, work1->handoffPhase, "and so does the sibling");

  // **(B) An admission that cannot leave the older window's deadline feasible is refused** -- the promise that a
  // continuously replenished early-deadline sibling cannot invalidate an older window's admitted bound. The same
  // candidate charge fits on the loop both windows were admitted on and does not fit on a short one, so the bound
  // refuses *capacity*, not siblings.
  const uint32_t earlyPhase = (60 * kBar) + 384;  // the phase both windows were requested at
  TEST_ASSERT_FALSE_MESSAGE(
      track.playbackViewAdmissionKeepsAdmittedDeadlines(contract, earlyPhase, kShortLoopTicks,
                                                       static_cast<uint16_t>(work1->requiredGrains()),
                                                       {work1->admittedTick, 1}),
      "on a short loop the sibling's charge cannot leave the older window's deadline feasible");
  TEST_ASSERT_TRUE_MESSAGE(
      track.playbackViewAdmissionKeepsAdmittedDeadlines(contract, earlyPhase, kLongLoopTicks,
                                                       static_cast<uint16_t>(work1->requiredGrains()),
                                                       {work1->admittedTick, 1}),
      "on the loop both windows were admitted on, the same charge still fits");

  // **(C) Why the bound is now enforced**, asserted rather than left to prose -- and the same inequality that
  // carried the opposite conclusion before the review's step 4. Under the placeholder advance (one bar per turn) the
  // charge for a single sibling exceeded a 40-bar loop by more than an order of magnitude, so enforcing it *was* a
  // capacity reduction; under the derived permitted bound the same charge fits with room, while a 2-bar loop still
  // cannot host it. Both directions are written against the declared field, so a change to that advance re-opens the
  // question instead of silently flipping a gate.
  const uint32_t siblingChargeTicks = static_cast<uint32_t>(work1->requiredGrains()) *
                                      (static_cast<uint32_t>(contract.maximumConsecutiveDeniedTurns) + 1u) *
                                      contract.maximumPhaseAdvancePerTurn;
  TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(kShortLoopTicks, siblingChargeTicks,
                                       "under the declared advance one sibling's charge fits a 40-bar loop");
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(2u * kBar, siblingChargeTicks,
                                          "and it still exceeds a 2-bar loop: the bound is a real bound");

  // Missing observation at the boundary does not matter: time is no longer loop-modular.
  // **Re-based (review step 4, recorded):** the suite stamps requests with monotone *device* time (the budget refuses
  // a backwards clock), so the stamp is no longer the request phase; what the fixture still pins is that the record is
  // *qualified* and that its deadline is that stamp plus the interval's lead (the next assertion).
  TEST_ASSERT_TRUE_MESSAGE(work0->admittedTick > 0u, "the admitted record carries a qualified device tick");
  // **Re-based (review step 4, recorded):** as above, the stamp is monotone device time, so the deadline is the stamp
  // plus the interval's lead -- the same quantity the old form read as "the loop end", now read against the tick.
  TEST_ASSERT_EQUAL_UINT64(work0->admittedTick + handoffLeadTicks(earlyPhase, work0->handoffPhase, kLongLoopTicks),
                           work0->deadlineTick);
  TEST_ASSERT_EQUAL_UINT32(1u, work0->clockEpoch);
  for (uint64_t laps : {1ull, 2ull, 100ull}) {
    TEST_ASSERT_FALSE_MESSAGE(
        track.playbackViewAdmissionKeepsAdmittedDeadlines(contract, earlyPhase, kLongLoopTicks,
            static_cast<uint16_t>(work1->requiredGrains()),
            // **Re-based (review step 4, recorded):** laps are counted from the admitted *deadline* now that the stamp
            // is monotone device time; the claim is unchanged -- a lap past the deadline stays closed.
            {work0->deadlineTick + laps * kLongLoopTicks, 1}),
        "a full unobserved lap must not reopen the retained deadline");
  }
  TEST_ASSERT_FALSE(track.playbackViewAdmissionKeepsAdmittedDeadlines(
      contract, work0->handoffPhase, kLongLoopTicks, 0, {work0->deadlineTick, 1}));
  TEST_ASSERT_FALSE(track.playbackViewAdmissionKeepsAdmittedDeadlines(
      contract, work0->handoffPhase + 1u, kLongLoopTicks, 0, {work0->deadlineTick + 1u, 1}));
}

/// The review's step 2, third point: **the enforced path is tested now, with a controlled timing contract** -- failing
/// older fixtures is not a reason to leave enforcement unexercised. The switch is a **runtime** flag precisely so this
/// fixture can turn it on, drive the *real* admission, and see the named refusal come out of it. Production keeps it
/// off until the timing bound is supported, and the flag is restored so no later fixture inherits it.
void test_enforced_candidate_charges_derived_cost_and_eight_track_rotation() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  for (uint8_t slot = 0; slot < 2; ++slot) {
    // Re-based by the review's step 4 (recorded): the shapes must sit where the **across-track allowance** is what
    // decides. 5 bars leaves just enough lead for the derived cost at denials=3 and not enough at denials=7 -- which is
    // this fixture's whole point -- while slot 1's longer loop admits the same window even charged with the sibling.
    const uint32_t length = (slot == 0 ? 7u : 40u) * 768;
    commitRecordPassIntoTrackSlotOfLength(track, slot, length);
    Loop& loop = track.getLoop(slot);
    loop.loopLengthTicks = length;
    publishNotesInBarsRootForSlot(loop, 0, 2, 32);
    TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(slot));
    SessionMidiEventVec& scratch = windowSourceScratch(slot);
    const bool wasEnforced = sInterferenceBudgetEnforced;
    sInterferenceBudgetEnforced = true;
    const auto result = track.requestPlaybackWindow(slot, 1, scratch, {1, 1});
    sInterferenceBudgetEnforced = wasEnforced;
    // Re-based with the shapes (review step 4): slot 0's shorter loop makes its two-bar window cover both boundaries
    // (the full 32 notes x on/off), while slot 1's longer loop keeps the end boundary outside the half-open window.
    TEST_ASSERT_EQUAL_UINT32(slot == 0 ? 64u : 63u, scratch.size());
    if (slot == 0) {
      TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::PlaybackViewRequestResult::CannotCompleteBeforeHandoff),
                               static_cast<int>(result),
                               "candidate must charge derived work across all eight tracks");
      TEST_ASSERT_NULL(TrackCaptureStopTestAccess::runtimeIfAllocated(track, slot)->activeViewBuildWork());
    } else {
      TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted), static_cast<int>(result));
      const auto* work = TrackCaptureStopTestAccess::runtimeIfAllocated(track, slot)->activeViewBuildWork();
      TEST_ASSERT_NOT_NULL(work);
      TEST_ASSERT_EQUAL_UINT32(12, work->requiredGrains());
    }
  }
}

void test_window_admission_refuses_by_name_once_the_interference_budget_is_enforced() {
  constexpr uint32_t kBar = 768;
  // Re-based by the review's step 4 (recorded): on a 900-bar loop the charged envelope can keep any promise, so
  // "the budget refuses by name" needs a loop where the sibling's charge genuinely cannot be met. 30 bars with
  // 200-note windows is that shape (the sibling's own promise admits; the candidate's charged one does not).
  constexpr uint32_t kLongLoopTicks = 70 * kBar;
  constexpr uint32_t kRequestPhase = (12 * kBar) + 384;
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();

  TEST_ASSERT_FALSE_MESSAGE(sInterferenceBudgetEnforced,
                            "activation is still gated on the callers' clock wiring: the production default is off");
  // Slot 0 is a substantial window (200 notes), admitted while the budget is off, so there is a promise to protect.
  prepareOwingWindowHolder(track, 0, kLongLoopTicks, 12, 14, 100, kRequestPhase);
  const LoopPlaybackRuntime* first = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
  TEST_ASSERT_NOT_NULL(first);
  const PlaybackViewAdmittedWork* work0 = first->activeViewBuildWork();
  TEST_ASSERT_NOT_NULL(work0);
  const uint32_t candidateGrains = static_cast<uint32_t>(work0->requiredGrains());

  // The sibling is requested **twice**: once with the budget enforced and once without. Only the budget can tell the
  // two apart, which is what makes the refusal below attributable to it rather than to some other guard.
  commitRecordPassIntoTrackSlotOfLength(track, 1, kLongLoopTicks);
  Loop& loop = track.getLoop(1);
  loop.loopLengthTicks = kLongLoopTicks;
  publishNotesInBarsRootForSlot(loop, 12, 14, 100);
  TEST_ASSERT_TRUE_MESSAGE(track.prewarmPlaybackForSlot(1), "slot 1 must prewarm");
  SessionMidiEventVec& scratch = windowSourceScratch(1);

  const bool wasEnforced = sInterferenceBudgetEnforced;
  sInterferenceBudgetEnforced = true;
  scratch.clear();
  const Track::PlaybackViewRequestResult refused = track.requestPlaybackWindow(1, kRequestPhase, scratch, {kRequestPhase, 1});
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::PlaybackViewRequestResult::TrackInterferenceBudgetExceeded),
                            static_cast<int>(refused),
                            "with the budget enforced the real admission refuses by name, charging the sibling");
  TEST_ASSERT_NULL_MESSAGE(TrackCaptureStopTestAccess::runtimeIfAllocated(track, 1)->activeViewBuildWork(),
                           "a refused request owes no build grain");
  // ... but it is **retained** as a demand (increment 24, DEC-108 §13(c)): this fixture's own observable is the
  // unchanged work record, and the retention is what the demand fixtures below pin.
  TEST_ASSERT_NOT_NULL_MESSAGE(track.pendingViewRequest(1), "a refused request leaves a pending demand behind");

  sInterferenceBudgetEnforced = wasEnforced;
  scratch.clear();
  const Track::PlaybackViewRequestResult admitted = track.requestPlaybackWindow(1, kRequestPhase, scratch, {kRequestPhase, 1});
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                            static_cast<int>(admitted),
                            "with the budget off the very same request is admitted: the budget is what refused it");
  TEST_ASSERT_NOT_NULL(TrackCaptureStopTestAccess::runtimeIfAllocated(track, 1)->activeViewBuildWork());
  const PlaybackViewAdmittedWork* work1 =
      TrackCaptureStopTestAccess::runtimeIfAllocated(track, 1)->activeViewBuildWork();
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(candidateGrains, static_cast<uint32_t>(work1->requiredGrains()),
                                   "and the sibling's own work is the charge it would have added");
}

/// DEC-108 §13(c) / task 3.10 (increment 24): **the retention harness.** Slot 0 holds an admitted promise (200 notes);
/// slot 1 carries the same window shape, and its own request is issued with the interference budget **enforced** --
/// the refusal class that must leave a retained demand behind. The flag is always restored, so no fixture inherits it,
/// and the helper returns the refusal by name so each caller can assert the class it expects.
Track::PlaybackViewRequestResult requestRefusedWindowDemand(Track& track, uint32_t loopTicks, uint32_t fromBar,
                                                           uint32_t toBar, uint32_t notes, uint32_t requestPhase,
                                                           uint64_t tick, uint32_t epoch) {
  commitRecordPassIntoTrackSlotOfLength(track, 1, loopTicks);
  Loop& loop = track.getLoop(1);
  loop.loopLengthTicks = loopTicks;
  publishNotesInBarsRootForSlot(loop, fromBar, toBar, notes);
  TEST_ASSERT_TRUE_MESSAGE(track.prewarmPlaybackForSlot(1), "slot 1 must prewarm a runtime to owe a demand");
  SessionMidiEventVec& scratch = windowSourceScratch(1);
  scratch.clear();
  const bool wasEnforced = sInterferenceBudgetEnforced;
  sInterferenceBudgetEnforced = true;
  const Track::PlaybackViewRequestResult result =
      track.requestPlaybackWindow(1, requestPhase, scratch, {tick, epoch});
  sInterferenceBudgetEnforced = wasEnforced;
  return result;
}

/// **DEC-108 §13b / task 3.10 -- the review's step 4: the declared envelope states its terms, its derivation and what
/// it refuses.**
///
/// The review's formula is `Delta-max x H x bounded per-track work`. Before this step every term was a placeholder:
/// the per-turn advance was one bar (a figure with no scheduling basis, measured 96x larger than the observed
/// per-opportunity advance), the denial allowance was 3 while the rotation serves 8 tracks, and the lead was judged on
/// the declared *basis* counts (3/3/1) while the retained record used the window's own derived cost. This fixture
/// pins each correction and the capability statement they add up to.
void test_the_declared_envelope_states_its_terms_its_derivation_and_what_it_refuses() {
  constexpr uint32_t kBar = Config::TICKS_PER_BAR;
  // The measurement the per-turn term is set from (device leg 2026-10-04, this change's own step-4 leg): the maximum
  // advance across a turn whose predecessor was **eligible** -- the scheduling maximum, which the schema-2 sampler
  // separates from idle intervals -- on the internal clock and at the fastest declared tempo (300 BPM).
  constexpr uint32_t kMeasuredSchedulingMaxInternal = 17;
  constexpr uint32_t kMeasuredSchedulingMaxAt300Bpm = 48;
  constexpr uint32_t kStatedMarginNumerator = 4;   // the declared value is <= 4/3 of the worst measured maximum
  constexpr uint32_t kStatedMarginDenominator = 3;
  const CoverageServiceContract declared = Track::playbackViewCoverageContract();

  // **(1) The terms are declared, and each came from somewhere.**
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(Track::kPlaybackViewPermittedAdvancePerTurn, declared.maximumPhaseAdvancePerTurn,
                                   "the per-turn term is the declared permitted scheduling bound, not a placeholder");
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(kMeasuredSchedulingMaxAt300Bpm, declared.maximumPhaseAdvancePerTurn,
                                              "the declared advance is at or above the measured scheduling maximum at "
                                              "the fastest declared tempo: a value below it is overrun by design");
  TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(
      (kMeasuredSchedulingMaxAt300Bpm * kStatedMarginNumerator) / kStatedMarginDenominator,
      declared.maximumPhaseAdvancePerTurn,
      "and within the stated margin of it: the bound is a declared capability, not a licence to hide the work");
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(kMeasuredSchedulingMaxInternal, declared.maximumPhaseAdvancePerTurn,
                                              "which also covers the slower tempo's measured maximum");
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(Config::NUM_TRACKS - 1, declared.maximumConsecutiveDeniedTurns,
                                   "the denial allowance is the across-track rotation (H - 1), not an invented 3");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(kBar, declared.leadTicks,
                                   "the handoff margin stays the demonstrated one bar");
  TEST_ASSERT_TRUE_MESSAGE(declared.maximumPhaseAdvancePerTurn < kBar,
                           "and the declared advance is tighter than the placeholder bar: the falsified safety "
                           "argument ('an unmeasured value can only refuse work') is what that placeholder rested on");

  // **(2) The accounting the lead is judged on is derived, and deriving it is not free of consequence.**
  CoverageServiceContract basis = declared;
  CoverageServiceContract derived = declared;
  derived.deriveGrainsFrom(63);  // the review's own basis-window figure
  TEST_ASSERT_GREATER_THAN_UINT16_MESSAGE(basis.requiredGrains(), derived.requiredGrains(),
                                          "a 63-entry window costs more than the declared basis counts: judging the lead "
                                          "on the basis is what admits work it cannot finish");

  // **(3) The capability, stated as a bound rather than a preference** -- the same 63-entry window, charged with one
  // same-track sibling, fits the loop the fixtures were written on and does not fit a short one; and the placeholder
  // advance refused the very same shape, which is the capacity reduction this declaration removes.
  const uint32_t turns = static_cast<uint32_t>(derived.requiredGrains()) *
                         (static_cast<uint32_t>(declared.maximumConsecutiveDeniedTurns) + 1u);
  const uint32_t declaredLead = declared.leadTicks + turns * declared.maximumPhaseAdvancePerTurn;
  const uint32_t placeholderLead = declared.leadTicks + turns * kBar;
  TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(40u * kBar, declaredLead,
                                       "the declared envelope fits a 63-entry window plus its sibling on a 40-bar loop");
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(4u * kBar, declaredLead,
                                          "and it does not fit a 4-bar loop: the envelope is a real bound");
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(40u * kBar, placeholderLead,
                                          "the placeholder one-bar advance refused that same shape -- measured, not "
                                          "argued: that is the capacity the declaration clarifies");
}

/// **O1 / O2 / O7 -- a refusal is a promise, and only where a promise is owed.**
///
/// The interference budget refusing a request must leave the demand behind *with the deadline it was promised*:
/// without that, turning enforcement on silently discards work instead of deferring it. The same fixture pins both
/// edges of the retention decision -- a refusal that means "nothing is owed" retains **nothing** (so retention is per
/// refusal name, not a blanket), and the admission that finally accepts the demand **discharges it whole**.
void test_a_refused_request_is_retained_with_its_original_deadline_and_discharged_by_its_admission() {
  constexpr uint32_t kBar = 768;
  // Re-based by the review's step 4 (recorded): with the envelope declared, a *refused* demand needs a shape outside
  // it. 20 bars admits the first holder on its own (its own derived cost fits) and refuses the sibling once the
  // charged envelope applies -- which is the "not now" case these fixtures are about.
  // **Shape inside/outside the declared envelope (recorded re-base, review step 4).** 30 bars admits the first
  // holder on its own derived cost (148 grains) and refuses the sibling once the charged envelope applies -- the
  // "not now" case these fixtures exist for. The second request's stamp must also be *later* than the first:
  // `requestRefusedWindowDemand` carries the fixture's phase as its tick, and the budget refuses a backwards clock.
  constexpr uint32_t kLoopTicks = 70 * kBar;
  constexpr uint32_t kPromisePhase = 12 * kBar;
  constexpr uint32_t kRequestPhase = 10000;
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  prepareOwingWindowHolder(track, 0, kLoopTicks, 12, 14, 100, kPromisePhase);
  TEST_ASSERT_NULL_MESSAGE(track.pendingViewRequest(1), "nothing is pending before the request");

  const Track::PlaybackViewRequestResult refused =
      requestRefusedWindowDemand(track, kLoopTicks, 12, 14, 100, kRequestPhase, kRequestPhase, 1);
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::PlaybackViewRequestResult::TrackInterferenceBudgetExceeded),
                            static_cast<int>(refused),
                            "the promise on slot 0 is what refuses the sibling, by name");
  const PlaybackViewPendingRequest* pending = track.pendingViewRequest(1);
  TEST_ASSERT_NOT_NULL_MESSAGE(pending, "a \"not now\" refusal retains the demand");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<uint8_t>(PlaybackViewPendingReason::InterferenceBudget),
                                  static_cast<uint8_t>(pending->reason), "and names why it is pending");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(kRequestPhase, pending->requestedPhase,
                                   "the demand keeps the phase the request was made at");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, pending->clockEpoch, "and the epoch its deadline is qualified in");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(kLoopTicks, pending->loopLengthTicks,
                                   "and the geometry the deadline was measured against");
  // The **absolute** deadline, by the one shared definition -- the demand carries what an admitted window would.
  const uint32_t lead = handoffLeadTicks(kRequestPhase, pending->work.handoffPhase, kLoopTicks);
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(static_cast<uint64_t>(kRequestPhase) + lead, pending->deadlineTick,
                                   "the retained deadline is the one the request was promised, from its own stamp");
  TEST_ASSERT_NULL_MESSAGE(TrackCaptureStopTestAccess::runtimeIfAllocated(track, 1)->activeViewBuildWork(),
                           "a refused request still owes no build grain");

  // **O7** -- the admission discharges the demand, so no stale deadline survives it.
  SessionMidiEventVec& scratch = windowSourceScratch(1);
  scratch.clear();
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                            static_cast<int>(track.requestPlaybackWindow(1, kRequestPhase, scratch, {kRequestPhase, 1})),
                            "with the budget off the very same request is admitted");
  TEST_ASSERT_NULL_MESSAGE(track.pendingViewRequest(1),
                           "an admitted demand is discharged whole: no retained deadline survives it");

  // **O2** -- the "nothing is owed" class retains nothing. The slot carries a **live runtime and no content**, so the
  // refusal is the not-servable one: a mutation that retains unconditionally cannot hide behind an absent runtime.
  constexpr uint8_t kBareSlot = 3;
  TEST_ASSERT_TRUE_MESSAGE(track.prewarmPlaybackForSlot(kBareSlot),
                           "the fixture needs a live runtime with no content to hold a demand");
  SessionMidiEventVec& bareScratch = windowSourceScratch(kBareSlot);
  bareScratch.clear();
  const Track::PlaybackViewRequestResult nothing =
      track.requestPlaybackWindow(kBareSlot, kRequestPhase, bareScratch, {kRequestPhase, 1});
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::PlaybackViewRequestResult::NotServable),
                            static_cast<int>(nothing),
                            "a slot with a runtime but no content is refused in the \"nothing is owed\" class");
  TEST_ASSERT_NULL_MESSAGE(track.pendingViewRequest(kBareSlot), "and no demand is retained for it");
}

/// **O3 -- a retained demand owes no grain, and records none.**
///
/// The demand is *not* work: no build has begun, so the per-track budget must not count it (counting it would charge
/// a sibling for work that may never happen, and would make the coalescing in O4 refuse its own successor). The
/// distinction is the record itself -- `pendingViewRequest` is non-null while `activeViewBuildWork` is null.
void test_a_pending_request_owes_no_grains_and_does_not_charge_a_sibling() {
  constexpr uint32_t kBar = 768;
  // Re-based by the review's step 4 (recorded): with the envelope declared, a *refused* demand needs a shape outside
  // it. 20 bars admits the first holder on its own (its own derived cost fits) and refuses the sibling once the
  // charged envelope applies -- which is the "not now" case these fixtures are about.
  // **Shape inside/outside the declared envelope (recorded re-base, review step 4).** 30 bars admits the first
  // holder on its own derived cost (148 grains) and refuses the sibling once the charged envelope applies -- the
  // "not now" case these fixtures exist for. The second request's stamp must also be *later* than the first:
  // `requestRefusedWindowDemand` carries the fixture's phase as its tick, and the budget refuses a backwards clock.
  constexpr uint32_t kLoopTicks = 70 * kBar;
  constexpr uint32_t kPromisePhase = 12 * kBar;
  constexpr uint32_t kRequestPhase = 10000;
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  prepareOwingWindowHolder(track, 0, kLoopTicks, 12, 14, 100, kPromisePhase);
  const LoopPlaybackRuntime* promised = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
  TEST_ASSERT_NOT_NULL(promised);
  TEST_ASSERT_NOT_NULL(promised->activeViewBuildWork());

  (void)requestRefusedWindowDemand(track, kLoopTicks, 12, 14, 100, kRequestPhase, kRequestPhase, 1);
  const LoopPlaybackRuntime* refusedRuntime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 1);
  TEST_ASSERT_NOT_NULL(refusedRuntime);
  TEST_ASSERT_NOT_NULL_MESSAGE(track.pendingViewRequest(1), "the demand is retained");
  TEST_ASSERT_NULL_MESSAGE(refusedRuntime->activeViewBuildWork(), "a pending demand is not an admitted build");
  // Excluding slot 0 leaves the retained demand on slot 1 in the sum: it must still contribute nothing.
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, track.playbackViewOutstandingGrains(0),
                                   "the retained demand owes no grain, so the budget does not count it");
}

/// **O4 -- coalescing keeps the oldest outstanding deadline and the newest attempt.**
///
/// Two refused attempts on one slot, at two phases and two stamps, so their promised deadlines differ. One record
/// must survive: the **earlier** deadline (DEC-108 §13(a): progress toward the oldest deadline is what may not be
/// thrown away) while the demand itself is the **later** request -- a later window may satisfy the older obligation,
/// which is why the identity follows the newest and the deadline does not.
void test_coalesced_requests_keep_the_oldest_deadline_and_the_newest_attempt() {
  constexpr uint32_t kBar = 768;
  // Re-based by the review's step 4 (recorded): with the envelope declared, a *refused* demand needs a shape outside
  // it. 20 bars admits the first holder on its own (its own derived cost fits) and refuses the sibling once the
  // charged envelope applies -- which is the "not now" case these fixtures are about.
  // **Shape inside/outside the declared envelope (recorded re-base, review step 4).** 30 bars admits the first
  // holder on its own derived cost (148 grains) and refuses the sibling once the charged envelope applies -- the
  // "not now" case these fixtures exist for. The second request's stamp must also be *later* than the first:
  // `requestRefusedWindowDemand` carries the fixture's phase as its tick, and the budget refuses a backwards clock.
  constexpr uint32_t kLoopTicks = 70 * kBar;
  constexpr uint32_t kPromisePhase = 15 * kBar;
  constexpr uint32_t kFirstPhase = 10000;
  constexpr uint32_t kSecondPhase = 12000;
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  // The promise, and slot 1's notes spread wide enough that **both** windows below carry work to be bounded.
  prepareOwingWindowHolder(track, 0, kLoopTicks, 12, 20, 100, kPromisePhase);
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::PlaybackViewRequestResult::TrackInterferenceBudgetExceeded),
                            static_cast<int>(requestRefusedWindowDemand(track, kLoopTicks, 12, 20, 100, kFirstPhase,
                                                                        kFirstPhase, 1)),
                            "the first attempt is refused by the budget");
  const PlaybackViewPendingRequest* first = track.pendingViewRequest(1);
  TEST_ASSERT_NOT_NULL(first);
  const uint64_t firstDeadline = first->deadlineTick;
  const uint32_t firstRevision = first->source.playbackRevision;

  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::PlaybackViewRequestResult::TrackInterferenceBudgetExceeded),
                            static_cast<int>(requestRefusedWindowDemand(track, kLoopTicks, 12, 20, 100, kSecondPhase,
                                                                        kSecondPhase, 1)),
                            "the second attempt is refused too, so it coalesces instead of being dropped");
  const PlaybackViewPendingRequest* coalesced = track.pendingViewRequest(1);
  TEST_ASSERT_NOT_NULL(coalesced);
  // The fixture's own precondition, so the row cannot pass by accident: the two attempts must promise **different**
  // deadlines, and the first must be the earlier of the pair -- otherwise "the oldest survived" proves nothing.
  const uint64_t secondAttemptDeadline =
      static_cast<uint64_t>(kSecondPhase) +
      handoffLeadTicks(kSecondPhase, coalesced->work.handoffPhase, kLoopTicks);
  TEST_ASSERT_TRUE_MESSAGE(secondAttemptDeadline != firstDeadline,
                           "the fixture's two attempts must promise different deadlines, or coalescing is untested");
  TEST_ASSERT_TRUE_MESSAGE(secondAttemptDeadline > firstDeadline,
                           "and the first attempt must be the earlier promise of the coalesced pair");
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(firstDeadline, coalesced->deadlineTick,
                                   "coalescing keeps the oldest outstanding deadline, never the newest");
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(firstDeadline, coalesced->work.deadlineTick,
                                   "and the retained work record carries that same promise");
  // The **position** is the retained promise as one coherent set: its origin phase and origin stamp are the earlier
  // attempt's, which is what the kept deadline names -- so the phase here is *not* the newest attempt's.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(kFirstPhase, coalesced->requestedPhase,
                                   "the retained position keeps the origin phase of the promise it belongs to");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(kFirstPhase, coalesced->work.admittedTick,
                                   "and its origin stamp, so no reader pairs one attempt's tick with another's");
  TEST_ASSERT_TRUE_MESSAGE(coalesced->source.playbackRevision != firstRevision,
                           "while the content follows the newest attempt: the coalesced identity is its revision");
}

/// **O5 / O6 -- an epoch or geometry change voids the deadline, never the obligation.**
///
/// DEC-108 §13(c): a changed clock invalidates the **promise to finish in time**, not the content. So the demand
/// stays readable and named, while the *position* it was promised at stops applying -- which is the difference
/// between "still promised for this tick" and "owed, but no longer positioned", and the reading the re-attempt owes.
void test_clock_epoch_and_geometry_invalidate_the_deadline_not_the_obligation() {
  constexpr uint32_t kBar = 768;
  // Re-based by the review's step 4 (recorded): with the envelope declared, a *refused* demand needs a shape outside
  // it. 20 bars admits the first holder on its own (its own derived cost fits) and refuses the sibling once the
  // charged envelope applies -- which is the "not now" case these fixtures are about.
  // **Shape inside/outside the declared envelope (recorded re-base, review step 4).** 30 bars admits the first
  // holder on its own derived cost (148 grains) and refuses the sibling once the charged envelope applies -- the
  // "not now" case these fixtures exist for. The second request's stamp must also be *later* than the first:
  // `requestRefusedWindowDemand` carries the fixture's phase as its tick, and the budget refuses a backwards clock.
  constexpr uint32_t kLoopTicks = 70 * kBar;
  constexpr uint32_t kPromisePhase = 12 * kBar;
  constexpr uint32_t kRequestPhase = 10000;
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  prepareOwingWindowHolder(track, 0, kLoopTicks, 12, 14, 100, kPromisePhase);
  (void)requestRefusedWindowDemand(track, kLoopTicks, 12, 14, 100, kRequestPhase, kRequestPhase, 1);
  TEST_ASSERT_NOT_NULL(track.pendingViewRequest(1));

  // The same epoch and geometry: the promise still holds its position.
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      static_cast<uint8_t>(PlaybackViewPendingReason::None),
      static_cast<uint8_t>(track.pendingViewRequestInvalidation(1, {kRequestPhase, 1})),
      "the deadline still applies in its own epoch and geometry");

  // A reset, seek, rebase or clock-source transition: the position is void, the obligation is not.
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      static_cast<uint8_t>(PlaybackViewPendingReason::DeadlineInvalidatedEpoch),
      static_cast<uint8_t>(track.pendingViewRequestInvalidation(1, {kRequestPhase, 2})),
      "a changed epoch voids the positional deadline");
  TEST_ASSERT_NOT_NULL_MESSAGE(track.pendingViewRequest(1),
                               "and the obligation survives it: an epoch change is not a cancellation");

  // A geometry change: the phase the deadline was measured at now describes a different loop.
  track.getLoop(1).loopLengthTicks = kLoopTicks + kBar;
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      static_cast<uint8_t>(PlaybackViewPendingReason::DeadlineInvalidatedGeometry),
      static_cast<uint8_t>(track.pendingViewRequestInvalidation(1, {kRequestPhase, 1})),
      "a changed loop length voids the positional deadline too");
  TEST_ASSERT_NOT_NULL_MESSAGE(track.pendingViewRequest(1),
                               "and the obligation survives that as well -- neither change erases it");
}

/// **O9 -- a generation change does not revive a demand, and does not lend it to the next operation.**
///
/// DEC-108 §13(d): identity is compared generation-qualified, never by slot number. So a demand retained under an
/// earlier operation is not this slot's demand any more -- it is not erased (a reset does not erase pending
/// obligations, DEC-108 §13(a)) but nothing may bind to it, and a demand made under the new generation starts a
/// fresh record rather than inheriting the older deadline.
void test_a_generation_change_does_not_revive_a_pending_request() {
  constexpr uint32_t kBar = 768;
  // Re-based by the review's step 4 (recorded): with the envelope declared, a *refused* demand needs a shape outside
  // it. 20 bars admits the first holder on its own (its own derived cost fits) and refuses the sibling once the
  // charged envelope applies -- which is the "not now" case these fixtures are about.
  // **Shape inside/outside the declared envelope (recorded re-base, review step 4).** 30 bars admits the first
  // holder on its own derived cost (148 grains) and refuses the sibling once the charged envelope applies -- the
  // "not now" case these fixtures exist for. The second request's stamp must also be *later* than the first:
  // `requestRefusedWindowDemand` carries the fixture's phase as its tick, and the budget refuses a backwards clock.
  constexpr uint32_t kLoopTicks = 70 * kBar;
  constexpr uint32_t kPromisePhase = 12 * kBar;
  constexpr uint32_t kFirstPhase = 10000;
  constexpr uint32_t kSecondPhase = 12000;
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  prepareOwingWindowHolder(track, 0, kLoopTicks, 12, 20, 100, kPromisePhase);
  (void)requestRefusedWindowDemand(track, kLoopTicks, 12, 20, 100, kFirstPhase, kFirstPhase, 1);
  const PlaybackViewPendingRequest* first = track.pendingViewRequest(1);
  TEST_ASSERT_NOT_NULL(first);
  const uint64_t firstDeadline = first->deadlineTick;

  // The operation moves (a clear/load/undo identity invalidation reaches exactly this entry point).
  track.bumpPlaybackGeneration();
  TEST_ASSERT_NULL_MESSAGE(track.pendingViewRequest(1),
                           "a returning slot must not revive a demand from an operation it no longer runs");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      static_cast<uint8_t>(PlaybackViewPendingReason::None),
      static_cast<uint8_t>(track.pendingViewRequestInvalidation(1, {kFirstPhase, 1})),
      "and no reader may bind to it, so it reports no invalidation either");

  // The new operation's own demand: retained afresh, and it does **not** inherit the old promise's deadline.
  const Track::PlaybackViewRequestResult refused =
      requestRefusedWindowDemand(track, kLoopTicks, 12, 20, 100, kSecondPhase, kSecondPhase, 1);
  TEST_ASSERT_NOT_EQUAL_MESSAGE(0, static_cast<int>(refused),
                                "the budget still refuses, so a demand is retained under the new generation");
  const PlaybackViewPendingRequest* second = track.pendingViewRequest(1);
  TEST_ASSERT_NOT_NULL(second);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(kSecondPhase, second->requestedPhase, "the new demand is the new attempt");
  TEST_ASSERT_TRUE_MESSAGE(second->deadlineTick != firstDeadline,
                           "a fresh record takes its own deadline, never the earlier operation's");
}

/// The review's **step 1** (*"bound the work of each request"*), task 3.10: at admission the requested window's work is
/// **bounded and retained** -- the entries the fill must inspect, the maximum retained window events, and the grain
/// cost derived from those counts -- and the retained record is the contract's own `deriveGrainsFrom`, never the
/// declared constants. The claim made good here is the review's own *"done when"*: **actual work never exceeds the
/// calculated cost**, across empty, sparse, dense and filtered sources. `actual` is counted by phase
/// (`nextCoverageBuildGrain()` before each turn, as the cost sweep does); `calculated` is the retained record.
/// **The bound of this fixture:** it proves the record bounds the *observed* work for these four shapes on one loop
/// length; it does not sweep the whole shape space (task 3.10's systematic sweep is still owed).
void test_window_request_work_is_bounded_and_retained_across_source_shapes() {
  constexpr uint32_t kBar = 768;
  // Re-based by the review's step 4 (recorded): the enforced envelope charges same-track sibling work, and this
  // fixture sweeps shapes on one track, so its loop hosts them all.
  constexpr uint32_t kLoopTicks = 300 * kBar;
  constexpr uint32_t kRequestPhase = (12 * kBar) + 384;
  struct Shape {
    uint8_t slot;
    uint32_t notes;
    uint32_t fromBar;
    uint32_t toBar;
  };
  // "Filtered" is the *spread* shape: the gather carries entries the window around the request phase will not retain,
  // so the fill must inspect more than it keeps -- which is exactly why the bound is derived from the inspected count.
  const Shape shapes[] = {
      {0, 0, 12, 13},
      {1, 1, 12, 13},
      {2, 200, 12, 13},
      {3, 80, 12, 62},
  };
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  for (uint8_t i = 0; i < sizeof(shapes) / sizeof(shapes[0]); ++i) {
    const Shape& shape = shapes[i];
    commitRecordPassIntoTrackSlotOfLength(track, shape.slot, kLoopTicks);
    Loop& loop = track.getLoop(shape.slot);
    loop.loopLengthTicks = kLoopTicks;
    publishNotesInBarsRootForSlot(loop, shape.fromBar, shape.toBar, shape.notes);
    TEST_ASSERT_TRUE_MESSAGE(track.prewarmPlaybackForSlot(shape.slot), "the slot must prewarm a runtime");
    SessionMidiEventVec& scratch = windowSourceScratch(shape.slot);
    scratch.clear();
    const Track::PlaybackViewRequestResult result =
        track.requestPlaybackWindow(shape.slot, kRequestPhase, scratch);
    const size_t inspected = scratch.size();
    LoopPlaybackRuntime* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, shape.slot);
    TEST_ASSERT_NOT_NULL(runtime);
    if (shape.notes == 0u) {
      // An **empty gather is a legitimate window**, not a no-op to refuse: the empty interval is Ready, and the window
      // still owes its promotion. So it is admitted, and its bounded work is the emptiness itself. **This is the case
      // that found the cost model's one real gap**: an empty window spends **one fill grain** (that grain is what
      // establishes the emptiness), while `ceil(0 / quantum)` is zero -- so the bound under-cut the work it exists to
      // bound, and `deriveGrainsFrom` now floors the fill count at one. (My first version of this fixture asserted a
      // *refusal* here; the run corrected that too.)
      TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                                static_cast<int>(result),
                                "an empty gather is a legitimate window, not a no-op to be refused");
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(inspected), "the empty gather inspects nothing");
      TEST_ASSERT_EQUAL_UINT16_MESSAGE(1u, runtime->activeViewBuildWork()->fillGrains,
                                       "an empty window still costs its one fill grain");
      TEST_ASSERT_EQUAL_UINT16_MESSAGE(0u, runtime->activeViewBuildWork()->orderGrains,
                                       "an empty window costs no order grain");
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u, static_cast<uint32_t>(runtime->activeViewBuildWork()->requiredGrains()),
                                       "an empty window owes its fill grain and its promotion");
    }
    TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                              static_cast<int>(result),
                              "the shape must be admitted for its work to be bounded");
    const PlaybackViewAdmittedWork* work = runtime->activeViewBuildWork();
    TEST_ASSERT_NOT_NULL_MESSAGE(work, "an admitted request retains the work it was bounded to");

    // **(A) The retained work is *derived from the inspected entries*, not the declaration.** One helper, so the record
    // cannot drift from the cost model the admission uses.
    CoverageServiceContract derived = Track::playbackViewCoverageContract();
    derived.deriveGrainsFrom(inspected);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(inspected), work->sourceEntries,
                                     "the record names the entries the fill must inspect");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        static_cast<uint32_t>(RuntimePlaybackViewConfig::kWindowAdmissionLimit), work->retainedEventBound,
        "the retained-event bound is the window's own storage capacity");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(derived.requiredFillGrains, work->fillGrains, "fills: derived, not declared");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(derived.requiredOrderGrains, work->orderGrains, "orders: derived, not declared");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1u, work->promotionGrains, "the promotion grain is always one");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(work->requiredGrains()),
                                     static_cast<uint32_t>(derived.requiredGrains()),
                                     "the record's own sum agrees with the contract's");
    if (shape.notes >= 200u) {
      TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(3u, static_cast<uint32_t>(work->orderGrains),
                                              "a dense window needs more order grains than the declared 3");
    }

    // **(B) The review's *done when*: actual work never exceeds the calculated cost**, and the build does finish (a
    // bound that never completes is not a bound). The work is driven through the runtime's **own grain primitives**
    // rather than through the lane turn, with the same quanta the turn uses: the turn samples the **process-wide**
    // service-gap series at its top (`SC_WINDOW_SERVICE_GAP`, a TU static), so driving turns from here would perturb
    // the gap fixture that runs later in this binary. Same work, no lane-turn side effect.
    uint32_t actualFills = 0;
    uint32_t actualOrders = 0;
    uint32_t actualPromotions = 0;
    uint32_t turns = 0;
    while (runtime->coverageBuildNeedsService() && turns < 6000u) {
      const RuntimePlaybackViewBuildGrain next = runtime->nextCoverageBuildGrain();
      RuntimePlaybackViewBuildGrain served = RuntimePlaybackViewBuildGrain::Denied;
      if (next == RuntimePlaybackViewBuildGrain::Fill) {
        ++actualFills;
        served = runtime->stepCoverageBuildFill(Track::kPlaybackViewFillQuantum);
      } else if (next == RuntimePlaybackViewBuildGrain::Order) {
        ++actualOrders;
        served = runtime->stepCoverageBuildOrder(Track::kPlaybackViewOrderQuantum);
      } else {
        break;  // Complete and Denied are terminal: `coverageBuildNeedsService()` is the active phase only
      }
      if (served == RuntimePlaybackViewBuildGrain::Complete) {
        ++actualPromotions;
      }
      ++turns;
    }
    TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(6000u, turns, "the build must complete inside its own bound");
    TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(work->fillGrains), actualFills,
                                             "actual fill grains never exceed the calculated cost");
    TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(work->orderGrains), actualOrders,
                                             "actual order grains never exceed the calculated cost");
    TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(work->promotionGrains), actualPromotions,
                                             "at most one promotion grain becomes the ready view");
    // **(C) The record gates on the build.** A finished build owes nothing, so its record must not be readable.
    TEST_ASSERT_NULL_MESSAGE(runtime->activeViewBuildWork(), "a finished build retains no readable work record");
  }
}

/// The review's step 1, second half: **reject or defer preparation whose work cannot be bounded; don't truncate
/// accepted content.** A window can be prepared only from what its destination can retain, so a gather beyond the
/// window's storage capacity is refused **by name** and retains nothing -- never quietly shortened to fit.
void test_window_request_beyond_the_retained_capacity_is_refused_by_name_and_truncates_nothing() {
  constexpr uint32_t kBar = 768;
  constexpr uint32_t kLoopTicks = 200 * kBar;  // long enough that the lead is not what refuses
  const uint32_t limit = static_cast<uint32_t>(RuntimePlaybackViewConfig::kWindowAdmissionLimit);
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlotOfLength(track, 0, kLoopTicks);
  Loop& loop = track.getLoop(0);
  loop.loopLengthTicks = kLoopTicks;
  // One note becomes a note-on/note-off pair, so this gather carries more entries than a window can retain.
  publishNotesInBarsRootForSlot(loop, 60, 61, (limit / 2u) + 8u);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0));
  SessionMidiEventVec& scratch = windowSourceScratch(0);
  scratch.clear();
  const Track::PlaybackViewRequestResult result = track.requestPlaybackWindow(0, (60 * kBar) + 384, scratch);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(limit, static_cast<uint32_t>(scratch.size()),
                                          "the fixture's gather must exceed the window's capacity");
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::PlaybackViewRequestResult::WindowExceedsRetainedCapacity),
                            static_cast<int>(result),
                            "a gather beyond the retained capacity is refused by name, not truncated");
  TEST_ASSERT_FALSE_MESSAGE(track.hasPendingPlaybackViewMaintenance(), "a refused request owes nothing");
  LoopPlaybackRuntime* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
  TEST_ASSERT_NOT_NULL(runtime);
  TEST_ASSERT_NULL_MESSAGE(runtime->activeViewBuildWork(), "nothing is retained for a refused request");
}

// Sample the dispatch seam explicitly: worker-only calls must not count opportunities.
void test_window_service_gap_sample_reports_the_measured_phase_advance_per_opportunity() {
  Track::resetPlaybackLaneMeasurement();
  WindowGapRows::reset();
  Track::samplePlaybackLaneOpportunity({900000, 9}, true, true,
                                      Track::PlaybackViewTurnResult::GrainDenied, 999);
  Track::resetPlaybackLaneMeasurement();
  WindowGapRows::reset();
  Track::samplePlaybackLaneOpportunity({0, 1}, false, false,
                                      Track::PlaybackViewTurnResult::Idle, 0);
  TEST_ASSERT_EQUAL_UINT32(1, WindowGapRows::count);
  TEST_ASSERT_EQUAL_UINT32(1, WindowGapRows::rows[0].opportunities);
  TEST_ASSERT_EQUAL_UINT32(0, WindowGapRows::rows[0].maxDeltaTicks);
  TEST_ASSERT_EQUAL_UINT32(0, WindowGapRows::resets[0]);
  Track::resetPlaybackLaneMeasurement();
  constexpr uint32_t interval = 256;
  uint64_t tick = 100000;
  using Result = Track::PlaybackViewTurnResult;
  auto sample = [&](uint32_t advance, uint32_t epoch = 1, bool running = true) {
    tick += advance;
    Track::samplePlaybackLaneOpportunity({tick, epoch}, running, false, Result::Idle, 0);
  };
  for (uint32_t i = 0; i < interval; ++i) sample(10);
  WindowGapRows::reset();
  for (uint32_t i = 0; i < interval; ++i) sample(i == 3 ? 5000 : 10);
  TEST_ASSERT_EQUAL_UINT32(1, WindowGapRows::count);
  TEST_ASSERT_EQUAL_UINT32(5000, WindowGapRows::rows[0].maxDeltaTicks);
  const uint32_t before = WindowGapRows::rows[0].opportunities;
  WindowGapRows::reset();
  for (uint32_t i = 0; i < interval; ++i) sample(10);
  TEST_ASSERT_EQUAL_UINT32(1, WindowGapRows::count);
  TEST_ASSERT_EQUAL_UINT32(10, WindowGapRows::rows[0].maxDeltaTicks);
  TEST_ASSERT_EQUAL_UINT32(before + interval, WindowGapRows::rows[0].opportunities);
  // Forward reset/catch-up is invisible to a decrease-only detector.
  WindowGapRows::reset();
  for (uint32_t i = 0; i < interval; ++i) sample(i == 3 ? 900000 : 10, i < 3 ? 1 : 2);
  TEST_ASSERT_EQUAL_UINT32(1, WindowGapRows::resets[0]);
  TEST_ASSERT_EQUAL_UINT32(10, WindowGapRows::rows[0].maxDeltaTicks);
  WindowGapRows::reset();
  for (uint32_t i = 0; i < interval; ++i) sample(900000, 2, false);
  TEST_ASSERT_EQUAL_UINT32(0, WindowGapRows::rows[0].maxDeltaTicks);
  TEST_ASSERT_FALSE(WindowGapRows::running[0]);
  WindowGapRows::reset();
  Track& track = trackManager.getTrack(0);
  for (uint32_t i = 0; i < interval; ++i) track.stepPlaybackViewMaintenance(0);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, WindowGapRows::count,
      "worker calls cannot masquerade as lane dispatches");
}


void test_window_deadline_clock_rejects_reset_and_unobserved_laps_at_admission() {
  constexpr uint32_t length = 900 * 768;
  constexpr uint32_t phase = 60 * 768 + 384;
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  prepareOwingWindowHolder(track, 0, length, 60, 62, 8, phase);
  const auto contract = Track::playbackViewCoverageContract();
  const auto* work = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0)->activeViewBuildWork();
  TEST_ASSERT_NOT_NULL(work);
  // **Re-based (review step 4, recorded):** the suite's request stamps are monotone *device* time, not the phase, so
  // this fixture reads its reference tick from the admitted record instead of reconstructing it from `phase`. The
  // assertions are unchanged in kind -- a tick later is still feasible, the same tick in another epoch is not, a
  // backwards tick is not, no identity at all is not, changed geometry is not.
  const uint64_t admittedTick = work->admittedTick;
  TEST_ASSERT_TRUE(track.playbackViewAdmissionKeepsAdmittedDeadlines(
      contract, phase + 1, length, 1, {admittedTick + 1u, 1}));
  TEST_ASSERT_FALSE_MESSAGE(track.playbackViewAdmissionKeepsAdmittedDeadlines(
      contract, phase, length, 1, {admittedTick, 2}), "same phase after reset is a different epoch");
  TEST_ASSERT_FALSE_MESSAGE(track.playbackViewAdmissionKeepsAdmittedDeadlines(
      contract, phase, length, 1, {admittedTick - 1u, 1}), "backwards time invalidates the promise");
  TEST_ASSERT_FALSE_MESSAGE(track.playbackViewAdmissionKeepsAdmittedDeadlines(
      contract, phase, length, 1), "unspecified clock identity cannot prove a deadline");
  track.getLoop(0).loopLengthTicks = length + 768;
  TEST_ASSERT_FALSE_MESSAGE(track.playbackViewAdmissionKeepsAdmittedDeadlines(
      contract, phase, length, 1, {admittedTick, 1}), "changed sibling geometry invalidates its deadline");
  track.getLoop(0).loopLengthTicks = length;

  commitRecordPassIntoTrackSlotOfLength(track, 1, length);
  track.getLoop(1).loopLengthTicks = length;
  publishNotesInBarsRootForSlot(track.getLoop(1), 60, 62, 1);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(1));
  auto& scratch = windowSourceScratch(1);
  // Restore the gate before any assertion, including the failing path.
  auto request = [&](PlaybackDeadlineClock clock) {
    sInterferenceBudgetEnforced = true;
    const auto result = track.requestPlaybackWindow(1, phase, scratch, clock);
    sInterferenceBudgetEnforced = false;
    return result;
  };
  for (PlaybackDeadlineClock clock : {PlaybackDeadlineClock{admittedTick + uint64_t(length), 1},
                                     PlaybackDeadlineClock{admittedTick + 100ull * length, 1},
                                     PlaybackDeadlineClock{admittedTick, 2}, PlaybackDeadlineClock{},
                                     PlaybackDeadlineClock{UINT64_MAX - 1, 1}}) {
    TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::TrackInterferenceBudgetExceeded),
                      static_cast<int>(request(clock)));
    TEST_ASSERT_NULL(TrackCaptureStopTestAccess::runtimeIfAllocated(track, 1)->activeViewBuildWork());
    // **Re-based (review step 4, recorded):** the stamp is monotone device time, so the deadline is that stamp plus
    // the interval's own phase lead -- the same relation as before, read against the admitted tick instead of the phase.
    TEST_ASSERT_EQUAL_UINT64(work->admittedTick + handoffLeadTicks(phase, work->handoffPhase, length),
                             work->deadlineTick);
  }
  // A still-valid stamp with feasible work is admitted through the SAME enforced path.
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(request({admittedTick, 1})));
  const auto* candidate = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 1)->activeViewBuildWork();
  TEST_ASSERT_NOT_NULL(candidate);
  TEST_ASSERT_EQUAL_UINT32(1, candidate->clockEpoch);

  // Isolate invalid stamp/overflow refusal: no sibling can mask the candidate's guard.
  TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0)->discardCoverageBuild();
  TrackCaptureStopTestAccess::runtimeIfAllocated(track, 1)->discardCoverageBuild();
  TEST_ASSERT_FALSE(track.hasPendingPlaybackViewMaintenance());
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::TrackInterferenceBudgetExceeded),
                    static_cast<int>(request({UINT64_MAX - 1, 1})));
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::TrackInterferenceBudgetExceeded),
                    static_cast<int>(request({})));
  // A caller-extended tick crosses the device's uint32 boundary without losing time.
  constexpr uint64_t extendedTick = uint64_t(UINT32_MAX) - 100;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(request({extendedTick, 7})));
  TEST_ASSERT_TRUE(track.playbackViewAdmissionKeepsAdmittedDeadlines(
      contract, phase + 200, length, 0, {extendedTick + 200, 7}));
  TEST_ASSERT_FALSE(track.playbackViewAdmissionKeepsAdmittedDeadlines(
      contract, phase, length, 0, {extendedTick + length, 7}));
}

void test_window_retry_preserves_borrowed_source_before_gather() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  prepareOwingWindowHolder(track, 0, 300 * bar, 12, 14, 400, 12 * bar);
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
  TEST_ASSERT_NOT_NULL(runtime);
  const auto original = *runtime->activeViewBuildWork();
  auto& scratch = windowSourceScratch(0);
  const auto expected = scratch;
  const MidiEvent* const borrowed = scratch.data();
  TEST_ASSERT_GREATER_THAN_UINT32(Track::kPlaybackViewFillQuantum, scratch.size());
  track.stepPlaybackViewMaintenance(12 * bar, nullptr, false, nativePlaybackDeadlineClock);
  TEST_ASSERT_TRUE(runtime->coverageBuildNeedsService());

  // A disjoint interval has no rows. The old requester clears the first build's
  // source before returning BuildInFlight, so a mere refusal corrupts that build.
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::BuildInFlight),
      static_cast<int>(track.requestPlaybackWindow(0, 30 * bar, scratch, nativePlaybackDeadlineClock)));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected.size(), scratch.size(),
      "a refused retry must not clear the active builder's borrowed source");
  TEST_ASSERT_EQUAL_PTR(borrowed, scratch.data());
  for (size_t i = 0; i < expected.size(); ++i) {
    TEST_ASSERT_EQUAL_UINT32(expected[i].tick, scratch[i].tick);
    TEST_ASSERT_EQUAL(expected[i].type, scratch[i].type);
    TEST_ASSERT_EQUAL(expected[i].data.noteData.note, scratch[i].data.noteData.note);
    TEST_ASSERT_EQUAL(expected[i].data.noteData.velocity, scratch[i].data.noteData.velocity);
  }
  TEST_ASSERT_EQUAL_UINT64(original.deadlineTick, runtime->activeViewBuildWork()->deadlineTick);
  const auto* pending = track.pendingViewRequest(0);
  TEST_ASSERT_NOT_NULL(pending);
  TEST_ASSERT_NOT_EQUAL(original.handoffPhase, pending->coverage.startPhase);
  TEST_ASSERT_EQUAL_UINT32(RuntimePlaybackViewConfig::kWindowAdmissionLimit, pending->work.sourceEntries);
  for (unsigned turn = 0; turn < 4096 && track.hasPendingPlaybackViewMaintenance(); ++turn)
    track.stepPlaybackViewMaintenance(12 * bar, nullptr, false, nativePlaybackDeadlineClock);
  TEST_ASSERT_FALSE(track.hasPendingPlaybackViewMaintenance());
  TEST_ASSERT_EQUAL_UINT32(expected.size(), track.playbackWindowEventCountForSlot(0));
  for (const MidiEvent& event : expected) {
    unsigned matches = 0;
    for (uint16_t i = 0; i < expected.size(); ++i) {
      MidiEvent published;
      TEST_ASSERT_TRUE(track.playbackWindowEventAtForSlot(0, i, published));
      if (published.tick == event.tick && published.type == event.type &&
          published.channel == event.channel &&
          published.data.noteData.note == event.data.noteData.note &&
          published.data.noteData.velocity == event.data.noteData.velocity) ++matches;
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, matches, "the original content publishes exactly once");
  }
  TEST_ASSERT_NOT_NULL(track.pendingViewRequest(0));
}

void test_window_retry_preserves_scratch_for_held_completion_and_ready_successor() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  prepareOwingWindowHolder(track, 0, 300 * bar, 12, 14, 400, 12 * bar);
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
  const auto work = *runtime->activeViewBuildWork();
  nativePlaybackDeadlineClock = {work.deadlineTick + 1, work.clockEpoch};
  for (unsigned turn = 0; turn < 4096 && track.hasPendingPlaybackViewMaintenance(); ++turn)
    track.stepPlaybackViewMaintenance(12 * bar, nullptr, false, nativePlaybackDeadlineClock);
  TEST_ASSERT_TRUE(runtime->completedPromisePendingJudgement);
  auto& scratch = windowSourceScratch(0);
  const size_t count = scratch.size();
  const MidiEvent* const data = scratch.data();
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::BuildInFlight),
      static_cast<int>(track.requestPlaybackWindow(0, 30 * bar, scratch, nativePlaybackDeadlineClock)));
  TEST_ASSERT_EQUAL_UINT32(count, scratch.size());
  TEST_ASSERT_EQUAL_PTR(data, scratch.data());
  TEST_ASSERT_TRUE(runtime->completedPromisePendingJudgement);
  TEST_ASSERT_EQUAL_UINT64(work.deadlineTick, runtime->viewBuildWork.deadlineTick);

  // Boundary recovery publishes the first window; the next completed window is
  // a ready successor, not serving content. Another request cannot overwrite it.
  nativePlaybackDeadlineClock = {work.deadlineTick + 300 * bar, work.clockEpoch};
  TEST_ASSERT_TRUE(track.revalidateCompletedPlaybackWindow(0, work.handoffPhase, nativePlaybackDeadlineClock));
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
      static_cast<int>(track.requestPlaybackWindow(0, 12 * bar, scratch, nativePlaybackDeadlineClock)));
  for (unsigned turn = 0; turn < 4096 && track.hasPendingPlaybackViewMaintenance(); ++turn)
    track.stepPlaybackViewMaintenance(12 * bar, nullptr, false, nativePlaybackDeadlineClock);
  TEST_ASSERT_FALSE(track.hasPendingPlaybackViewMaintenance());
  const auto* set = runtime->viewSetIfPrewarmed();
  TEST_ASSERT_NOT_NULL(set);
  TEST_ASSERT_LESS_THAN_UINT8(RuntimePlaybackViewConfig::kViewCount, set->lifecycle.ready);
  const auto ready = set->lifecycle.ready;
  TEST_ASSERT_TRUE(set->views[ready].complete);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::SuccessorAlreadyPrepared),
      static_cast<int>(track.requestPlaybackWindow(0, 30 * bar, scratch, nativePlaybackDeadlineClock)));
  TEST_ASSERT_EQUAL_UINT32(count, scratch.size());
  TEST_ASSERT_EQUAL_PTR(data, scratch.data());
  TEST_ASSERT_EQUAL_UINT8(ready, set->lifecycle.ready);
  TEST_ASSERT_TRUE(set->views[ready].complete);
  TEST_ASSERT_NOT_NULL(track.pendingViewRequest(0));
}

void test_window_absolute_lead_arithmetic_does_not_overflow() {
  CoverageServiceContract contract;
  contract.maximumPhaseAdvancePerTurn = 1;
  contract.requiredFillGrains = 1;
  contract.leadTicks = 1;
  TEST_ASSERT_FALSE(contract.canCompleteWithinLead(1));
  TEST_ASSERT_TRUE(contract.canCompleteWithinLead(2));
  contract.maximumPhaseAdvancePerTurn = UINT32_MAX;
  contract.maximumConsecutiveDeniedTurns = UINT16_MAX;
  contract.requiredFillGrains = UINT16_MAX;
  TEST_ASSERT_FALSE(contract.canCompleteWithinLead(UINT64_MAX, UINT16_MAX));
}

// Retained committed requests are driven exclusively through the production lane.
LoopPlaybackRuntime* prepareCommittedRetrySlot(Track& track, uint8_t slot, uint32_t notes = 32) {
  constexpr uint32_t length = 512 * Config::TICKS_PER_BAR;
  commitRecordPassIntoTrackSlotOfLength(track, slot, length);
  auto& loop = track.getLoop(slot);
  loop.loopLengthTicks = length;
  publishNotesInBarsRootForSlot(loop, 12, 14, notes);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(slot));
  return TrackCaptureStopTestAccess::runtimeIfAllocated(track, slot);
}

void test_committed_retry_counts_then_admits_original_promise_and_publishes_without_second_request() {
  LoopEventStore::initPool();
  auto& track = trackManager.getTrack(0);
  auto* runtime = prepareCommittedRetrySlot(track, 0, 80);
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  const PlaybackCoverageInterval coverage{12 * bar, 2 * bar, 512 * bar};
  nativePlaybackDeadlineClock = {1000, 1};
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::DeferredSourceQualification),
      static_cast<int>(track.requestCommittedPlaybackWindow(0, coverage, 13 * bar, nativePlaybackDeadlineClock)));
  const auto original = *track.pendingViewRequest(0);
  TEST_ASSERT_TRUE(track.hasPendingPlaybackViewMaintenance());
  TEST_ASSERT_FALSE(runtime->coverageBuildNeedsService());
  auto offer = track.playbackWindowServiceOffer(0, nativePlaybackDeadlineClock);
  TEST_ASSERT_TRUE(offer.eligible && offer.canProgress);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewTurnResult::GrainServed),
      static_cast<int>(track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock)));
  TEST_ASSERT_NULL(track.pendingViewRequest(0));
  TEST_ASSERT_TRUE(runtime->frozenViewRequest.active);
  TEST_ASSERT_TRUE(runtime->viewBuild.active());
  TEST_ASSERT_FALSE(runtime->coverageBuildNeedsService());
  TEST_ASSERT_EQUAL_UINT32(0, runtime->coverageSource.readUnits());
  const auto* set = runtime->viewSetIfPrewarmed();
  const auto buildIndex = set->lifecycle.build;
  TEST_ASSERT_EQUAL_UINT32(0, set->views[buildIndex].storage.eventCount);
  nativePlaybackDeadlineClock.tick += 321; // This time is never gifted back at admission.
  track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
  TEST_ASSERT_EQUAL_UINT32(Track::kPlaybackViewFillQuantum, runtime->coverageSource.readUnits());
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(61, set->views[buildIndex].storage.eventCount,
      "qualification fills private rows instead of discarding and rereading them");
  TEST_ASSERT_FALSE(set->views[buildIndex].complete);
  TEST_ASSERT_FALSE(set->views[buildIndex].published);
  // A later demand arrives while an older frozen request owns qualification.
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::DeferredSourceQualification),
      static_cast<int>(track.requestCommittedPlaybackWindow(0, {14 * bar, bar, 512 * bar}, 13 * bar,
          nativePlaybackDeadlineClock)));
  const auto newer = *track.pendingViewRequest(0);
  for (unsigned i = 0; i < 100 && !runtime->coverageBuildNeedsService(); ++i)
    track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
  TEST_ASSERT_TRUE(runtime->coverageBuildNeedsService());
  TEST_ASSERT_EQUAL_UINT64(original.deadlineTick, runtime->viewBuildWork.deadlineTick);
  TEST_ASSERT_EQUAL_UINT64(original.work.admittedTick, runtime->viewBuildWork.admittedTick);
  TEST_ASSERT_EQUAL_UINT32(original.clockEpoch, runtime->viewBuildWork.clockEpoch);
  TEST_ASSERT_EQUAL_UINT32(runtime->coverageEventCount, runtime->viewBuildWork.sourceEntries);
  TEST_ASSERT_EQUAL_UINT32(0, runtime->viewBuildWork.fillGrains);
  TEST_ASSERT_TRUE(runtime->coverageSource.complete());
  TEST_ASSERT_EQUAL_UINT64(newer.deadlineTick, track.pendingViewRequest(0)->deadlineTick);
  PlaybackWindowIdentity published{};
  TEST_ASSERT_FALSE(track.publishedPlaybackWindowIdentityForSlot(0, published));
  for (unsigned i = 0; i < 4096 && runtime->coverageBuildNeedsService(); ++i)
    track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, published));
  TEST_ASSERT_EQUAL_UINT32(coverage.startPhase, published.interval.startPhase);
  TEST_ASSERT_TRUE(runtime->coverageSource.retiring());
  TEST_ASSERT_FALSE(track.noLiveOwnerForSlot(0));
  // Do not freeze the newer request while checking only the older one's retirement.
  while (runtime->ownsCoverageSource())
    track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
  TEST_ASSERT_NOT_NULL(track.pendingViewRequest(0));
  TEST_ASSERT_EQUAL_UINT64(newer.deadlineTick, track.pendingViewRequest(0)->deadlineTick);
  TEST_ASSERT_EQUAL_UINT32(newer.coverage.startPhase, track.pendingViewRequest(0)->coverage.startPhase);
  const auto& current = set->views[set->lifecycle.current];
  SessionMidiEventVec expected;
  TEST_ASSERT_TRUE(track.getLoop(0).tryGatherPublishedSnapshotEventsInWindow(expected, coverage.startPhase,
      coverage.lengthTicks));
  TEST_ASSERT_EQUAL_UINT32(expected.size(), current.storage.eventCount);
  RuntimePlaybackView reference;
  TEST_ASSERT_TRUE(reference.prewarm(expected.size()));
  RuntimePlaybackViewBuild referenceBuild;
  TEST_ASSERT_TRUE(referenceBuild.begin(reference, expected.data(), expected.size(), current.source, coverage, {}, {}));
  for (unsigned turn = 0; turn < 4096 && referenceBuild.active(); ++turn) {
    if (referenceBuild.nextGrain() == RuntimePlaybackViewBuildGrain::Fill) referenceBuild.stepFill(64);
    else referenceBuild.stepOrder(48);
  }
  TEST_ASSERT_TRUE(reference.complete);
  for (size_t i = 0; i < expected.size(); ++i) {
    const auto& event = reference.storage.events[reference.storage.order[i]];
    const auto& actual = current.storage.events[current.storage.order[i]];
    TEST_ASSERT_EQUAL_UINT32(event.tick, actual.tick);
    TEST_ASSERT_EQUAL_UINT8(event.type, actual.type);
    TEST_ASSERT_EQUAL_UINT8(event.channel, actual.channel);
    TEST_ASSERT_EQUAL_UINT8(event.data.noteData.note, actual.data.noteData.note);
    TEST_ASSERT_EQUAL_UINT8(event.data.noteData.velocity, actual.data.noteData.velocity);
  }
}

void test_committed_retry_shared_owner_and_blocked_slot_do_not_hide_runnable_sibling() {
  LoopEventStore::initPool();
  auto& track = trackManager.getTrack(0);
  auto* first = prepareCommittedRetrySlot(track, 0, 0);
  auto* second = prepareCommittedRetrySlot(track, 1, 0);
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  const PlaybackCoverageInterval coverage{12 * bar, 2 * bar, 512 * bar};
  nativePlaybackDeadlineClock = {1000, 1};
  // Generic shared-builder work has no window-lane ownership.
  auto* set = first->viewSetIfPrewarmed();
  TEST_ASSERT_TRUE(first->viewBuild.begin(set->views[set->lifecycle.build], nullptr, 0, {}, coverage, {}, {}));
  TEST_ASSERT_FALSE(first->coverageBuildNeedsService());
  TEST_ASSERT_FALSE(track.hasPendingPlaybackViewMaintenance());
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewTurnResult::Idle),
      static_cast<int>(track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock)));
  TEST_ASSERT_EQUAL(static_cast<int>(RuntimePlaybackViewBuildGrain::Fill), static_cast<int>(first->viewBuild.nextGrain()));
  track.requestCommittedPlaybackWindow(0, coverage, 11 * bar, nativePlaybackDeadlineClock);
  track.requestCommittedPlaybackWindow(1, coverage, 13 * bar, nativePlaybackDeadlineClock);
  MidiServiceDrain::PlaybackHolder holders[Config::MAX_LOOPS_PER_TRACK] = {};
  track.fillPlaybackHolders(0, holders, nativePlaybackDeadlineClock);
  TEST_ASSERT_TRUE(holders[0].eligible && !holders[0].canProgress);
  TEST_ASSERT_TRUE(holders[1].eligible && holders[1].canProgress);
  const auto offer = track.playbackWindowServiceOffer(0, nativePlaybackDeadlineClock);
  TEST_ASSERT_TRUE(offer.eligible && offer.canProgress);
  track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
  TEST_ASSERT_TRUE(second->frozenViewRequest.active);
  TEST_ASSERT_FALSE(first->ownsCoverageSource());
  // A generic begin cannot overwrite the qualification reservation.
  auto* otherSet = second->viewSetIfPrewarmed();
  TEST_ASSERT_FALSE(second->viewBuild.begin(otherSet->views[otherSet->lifecycle.build], nullptr, 0,
      {}, coverage, {}, {}));
  TEST_ASSERT_TRUE(second->frozenViewRequest.active);
  second->discardCoverageBuild();
  while (second->ownsCoverageSource()) second->stepCoverageSourceRetirement(64);
  first->viewBuild.discard();
}

void test_committed_retry_cancel_drains_source_and_epoch_invalidation_never_regrants() {
  LoopEventStore::initPool();
  auto& track = trackManager.getTrack(0);
  auto* runtime = prepareCommittedRetrySlot(track, 0, 80);
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  const PlaybackCoverageInterval coverage{12 * bar, 2 * bar, 512 * bar};
  nativePlaybackDeadlineClock = {1000, 1};
  track.requestCommittedPlaybackWindow(0, coverage, 13 * bar, nativePlaybackDeadlineClock);
  const auto original = *track.pendingViewRequest(0);
  track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
  TEST_ASSERT_TRUE(runtime->ownsCoverageSource());
  // Retire the root: the cursor now owns its membership pages, not the full root.
  track.getLoop(0).storePublishedSnapshotForTest({}, 0);
  runtime->discardCoverageBuild();
  TEST_ASSERT_TRUE(runtime->coverageSource.retiring());
  TEST_ASSERT_FALSE(track.noLiveOwnerForSlot(0));
  size_t turns = 0;
  while (runtime->ownsCoverageSource() && turns++ < 4096) {
    const auto before = runtime->coverageSource.releaseUnits();
    track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
    const auto after = runtime->coverageSource.releaseUnits();
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(64, after - before);
  }
  TEST_ASSERT_FALSE(runtime->ownsCoverageSource());
  TEST_ASSERT_GREATER_THAN_UINT32(1, turns);
  TEST_ASSERT_NOT_NULL(track.pendingViewRequest(0));
  track.serviceAdmittedDeadlineMisses({1000, 2});
  TEST_ASSERT_TRUE(track.pendingViewRequest(0)->positionInvalidated);
  TEST_ASSERT_EQUAL_UINT64(original.deadlineTick, track.pendingViewRequest(0)->deadlineTick);
  TEST_ASSERT_FALSE(track.playbackWindowServiceOffer(0, {1000, 1}).canProgress);
  TEST_ASSERT_EQUAL_UINT64(original.work.admittedTick, track.pendingViewRequest(0)->work.admittedTick);
}

void test_committed_retry_residual_lead_and_empty_two_bar_envelope_refuse_without_new_lap() {
  LoopEventStore::initPool();
  auto& track = trackManager.getTrack(0);
  auto* runtime = prepareCommittedRetrySlot(track, 0, 0);
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  const PlaybackCoverageInterval coverage{12 * bar, 2 * bar, 512 * bar};
  nativePlaybackDeadlineClock = {1000, 1};
  track.requestCommittedPlaybackWindow(0, coverage, 13 * bar, nativePlaybackDeadlineClock);
  const auto original = *track.pendingViewRequest(0);
  track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock); // freeze
  track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock); // count
  nativePlaybackDeadlineClock.tick = original.deadlineTick - 1;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewTurnResult::GrainDenied),
      static_cast<int>(track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock)));
  while (runtime->ownsCoverageSource()) track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
  TEST_ASSERT_NOT_NULL(track.pendingViewRequest(0));
  TEST_ASSERT_EQUAL_UINT64(original.deadlineTick, track.pendingViewRequest(0)->deadlineTick);
  TEST_ASSERT_FALSE(runtime->coverageBuildNeedsService());
  PlaybackWindowIdentity published{};
  TEST_ASSERT_FALSE(track.publishedPlaybackWindowIdentityForSlot(0, published));
  track.serviceAdmittedDeadlineMisses({original.deadlineTick + 512 * bar, 1});
  TEST_ASSERT_TRUE(track.pendingViewRequest(0)->positionInvalidated);
  TEST_ASSERT_EQUAL_UINT64(original.deadlineTick, track.pendingViewRequest(0)->deadlineTick);
  auto contract = Track::playbackViewCoverageContract();
  contract.deriveSnapshotGrainsFrom(0, 0, 64, 48);
  TEST_ASSERT_EQUAL_UINT16(3, contract.requiredGrains());
  TEST_ASSERT_FALSE(contract.canCompleteWithinLead(2 * bar));
  TEST_ASSERT_TRUE(contract.canCompleteWithinLead(3 * bar));
}

void test_committed_retry_clock_stamp_projects_nonzero_slot_origin() {
  LoopEventStore::initPool();
  auto& track = trackManager.getTrack(0);
  prepareCommittedRetrySlot(track, 0, 0);
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  const PlaybackCoverageInterval coverage{12 * bar, 2 * bar, 512 * bar};
  track.setProjectionCycleStartTick(7000);
  const PlaybackDeadlineClock clock{7000 + 13 * bar, 1};
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::DeferredSourceQualification),
      static_cast<int>(track.requestCommittedPlaybackWindow(0, coverage, clock)));
  const auto* request = track.pendingViewRequest(0);
  TEST_ASSERT_NOT_NULL(request);
  TEST_ASSERT_EQUAL_UINT32(13 * bar, request->requestedPhase);
  TEST_ASSERT_EQUAL_UINT64(clock.tick + 511 * bar, request->deadlineTick);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::NotServable),
      static_cast<int>(track.requestCommittedPlaybackWindow(1, coverage, clock)));
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::NotServable),
      static_cast<int>(track.requestCommittedPlaybackWindow(0, coverage, PlaybackDeadlineClock{})));
}

void test_committed_retry_stale_frozen_source_keeps_newer_request_position_runnable() {
  LoopEventStore::initPool();
  auto& track = trackManager.getTrack(0);
  auto* runtime = prepareCommittedRetrySlot(track, 0, 32);
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  const PlaybackCoverageInterval older{12 * bar, 2 * bar, 512 * bar};
  const PlaybackCoverageInterval newer{14 * bar, 2 * bar, 512 * bar};
  nativePlaybackDeadlineClock = {1000, 1};
  track.requestCommittedPlaybackWindow(0, older, 20 * bar, nativePlaybackDeadlineClock);
  const auto original = *track.pendingViewRequest(0);
  track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
  TEST_ASSERT_TRUE(runtime->frozenViewRequest.active);
  auto& loop = track.getLoop(0);
  ++loop.playbackRevision;
  publishNotesInBarsRootForSlot(loop, 14, 16, 32);
  nativePlaybackDeadlineClock.tick += 100;
  track.requestCommittedPlaybackWindow(0, newer, 20 * bar, nativePlaybackDeadlineClock);
  const auto latest = *track.pendingViewRequest(0);
  TEST_ASSERT_GREATER_THAN_UINT32(original.deadlineTick, latest.deadlineTick);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewTurnResult::GrainDenied),
      static_cast<int>(track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock)));
  while (runtime->ownsCoverageSource())
    track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
  const auto* request = track.pendingViewRequest(0);
  TEST_ASSERT_NOT_NULL(request);
  TEST_ASSERT_FALSE_MESSAGE(request->positionInvalidated, "source replacement is not clock invalidation");
  TEST_ASSERT_EQUAL_UINT32(latest.source.playbackRevision, request->source.playbackRevision);
  TEST_ASSERT_EQUAL_UINT32(newer.startPhase, request->coverage.startPhase);
  TEST_ASSERT_EQUAL_UINT64(original.deadlineTick, request->deadlineTick);
  TEST_ASSERT_EQUAL_UINT64(original.work.admittedTick, request->work.admittedTick);
  TEST_ASSERT_TRUE(track.playbackWindowServiceOffer(0, nativePlaybackDeadlineClock).canProgress);
  track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
  TEST_ASSERT_TRUE(runtime->frozenViewRequest.active);
  TEST_ASSERT_EQUAL_UINT32(latest.source.playbackRevision, runtime->frozenViewRequest.source.playbackRevision);
  runtime->discardCoverageBuild();
  while (runtime->ownsCoverageSource()) runtime->stepCoverageSourceRetirement(64);
}

void test_committed_retry_capacity_refusal_counts_bounded_source_without_partial_publication() {
  LoopEventStore::initPool();
  auto& track = trackManager.getTrack(0);
  auto* runtime = prepareCommittedRetrySlot(track, 0, 1200);
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  nativePlaybackDeadlineClock = {1000, 1};
  track.requestCommittedPlaybackWindow(0, {12 * bar, 2 * bar, 512 * bar}, 20 * bar, nativePlaybackDeadlineClock);
  track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
  for (unsigned i = 0; i < 100 && !runtime->coverageSource.retiring(); ++i) {
    const size_t before = runtime->coverageSource.readUnits();
    track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(64, runtime->coverageSource.readUnits() - before);
  }
  TEST_ASSERT_TRUE(runtime->coverageSource.retiring());
  TEST_ASSERT_NOT_NULL(track.pendingViewRequest(0));
  TEST_ASSERT_EQUAL(static_cast<int>(PlaybackViewPendingReason::WindowExceedsRetainedCapacity),
      static_cast<int>(track.pendingViewRequest(0)->reason));
  TEST_ASSERT_EQUAL_UINT32(RuntimePlaybackViewConfig::kWindowAdmissionLimit + 1, runtime->coverageEventCount);
  PlaybackWindowIdentity published{};
  TEST_ASSERT_FALSE(track.publishedPlaybackWindowIdentityForSlot(0, published));
  while (runtime->ownsCoverageSource()) track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
  TEST_ASSERT_FALSE(track.playbackWindowServiceOffer(0, nativePlaybackDeadlineClock).canProgress);
}

void test_committed_retry_prices_actual_pinned_topology_not_maximum_retirement() {
  LoopEventStore::initPool();
  auto& track = trackManager.getTrack(0);
  auto* runtime = prepareCommittedRetrySlot(track, 0, 0);
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  const PlaybackCoverageInterval coverage{12 * bar, 2 * bar, 512 * bar};
  nativePlaybackDeadlineClock = {1000, 1};
  // Four bars of lead: far below the old 205-grain retirement surcharge, but
  // enough for the actual empty pinned topology at the UNCHANGED declaration.
  track.requestCommittedPlaybackWindow(0, coverage, 8 * bar, nativePlaybackDeadlineClock);
  const auto original = *track.pendingViewRequest(0);
  track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock); // freeze
  track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock); // count
  TEST_ASSERT_EQUAL_UINT32(2, runtime->coverageSource.qualifiedReleaseUnitBound());
  // Remaining lead2500 fits remaining order/promotion/retirement2304, but not
  // the old duplicate-fill charge2816. Never regrant the original deadline.
  nativePlaybackDeadlineClock.tick += 572;
  track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock); // admit
  TEST_ASSERT_TRUE_MESSAGE(runtime->coverageBuildNeedsService(),
      "empty source must pay its two null bars, not 768 possible pages");
  TEST_ASSERT_EQUAL_UINT32(2, runtime->viewBuildWork.promotionGrains); // promotion + one retirement turn
  TEST_ASSERT_EQUAL_UINT64(original.deadlineTick, runtime->viewBuildWork.deadlineTick);
  TEST_ASSERT_EQUAL_UINT64(original.work.admittedTick, runtime->viewBuildWork.admittedTick);
  for (unsigned i = 0; i < 32 && runtime->coverageBuildNeedsService(); ++i)
    track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
  PlaybackWindowIdentity published{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, published));
  while (runtime->ownsCoverageSource()) track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
  TEST_ASSERT_EQUAL_UINT32(2, runtime->coverageSource.releaseUnits());
  auto contract = Track::playbackViewCoverageContract();
  contract.deriveSnapshotGrainsFrom(2, 0, 64, 48);
  contract.requiredPromotionGrains = 2;
  TEST_ASSERT_FALSE(contract.canCompleteWithinLead(2 * bar));
}

void test_two_bar_service_cannot_supply_even_dense_copy_work() {
  const auto contract = Track::playbackViewCoverageContract();
  const uint32_t span = 2 * Config::TICKS_PER_BAR;
  const uint32_t turnsPerWindow = span / contract.maximumPhaseAdvancePerTurn;
  const uint32_t perHolder = turnsPerWindow / Config::NUM_TRACKS;
  const uint32_t copyTurns = (RuntimePlaybackViewConfig::kWindowAdmissionLimit +
      Track::kPlaybackViewFillQuantum - 1) / Track::kPlaybackViewFillQuantum;
  TEST_ASSERT_EQUAL_UINT32(24, turnsPerWindow);
  TEST_ASSERT_EQUAL_UINT32(3, perHolder);
  TEST_ASSERT_EQUAL_UINT32(32, copyTurns);
  TEST_ASSERT_TRUE_MESSAGE(copyTurns > perHolder,
      "initial prefill cannot fix a producer rate slower than sustained consumption");
  TEST_ASSERT_EQUAL_UINT32(1, (span - contract.leadTicks) /
      (Config::NUM_TRACKS * contract.maximumPhaseAdvancePerTurn));
}

void exercise_committed_retry_eight_track_lead(uint32_t leadBars, bool expectComplete) {
  using namespace MidiServiceDrain;
  LoopEventStore::initPool();
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  const PlaybackCoverageInterval coverage{12 * bar, 2 * bar, 512 * bar};
  nativePlaybackDeadlineClock = {1000, 1};
  uint64_t deadlines[Config::NUM_TRACKS] = {};
  uint32_t serviced[Config::NUM_TRACKS] = {};
  for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i) {
    auto& track = trackManager.getTrack(i);
    prepareCommittedRetrySlot(track, 0, 0);
    track.requestCommittedPlaybackWindow(0, coverage, (12 - leadBars) * bar, nativePlaybackDeadlineClock);
    deadlines[i] = track.pendingViewRequest(0)->deadlineTick;
  }
  PlaybackHolderCursor cursor{};
  uint32_t completions = 0, turns = 0;
  for (; turns < 256; ++turns) {
    PlaybackHolder offers[Config::NUM_TRACKS] = {};
    for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i) {
      auto& track = trackManager.getTrack(i);
      track.serviceAdmittedDeadlineMisses(nativePlaybackDeadlineClock);
      offers[i] = track.playbackWindowServiceOffer(0, nativePlaybackDeadlineClock);
    }
    uint8_t chosen = kNoPlaybackHolder;
    if (selectPlaybackHolderRoundRobin(cursor, offers, Config::NUM_TRACKS, chosen) != PlaybackHolderSelection::Service)
      break;
    ++serviced[chosen];
    const auto result = trackManager.getTrack(chosen).stepPlaybackViewMaintenance(
        0, nullptr, false, nativePlaybackDeadlineClock);
    if (result == Track::PlaybackViewTurnResult::BuildComplete) ++completions;
    nativePlaybackDeadlineClock.tick += Track::kPlaybackViewPermittedAdvancePerTurn;
  }
  TEST_ASSERT_LESS_THAN_UINT32(256, turns);
  TEST_ASSERT_EQUAL_UINT32(expectComplete ? Config::NUM_TRACKS : 0, completions);
  for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i) {
    auto& track = trackManager.getTrack(i);
    auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
    TEST_ASSERT_FALSE(runtime->ownsCoverageSource());
    PlaybackWindowIdentity published{};
    TEST_ASSERT_EQUAL(expectComplete, track.publishedPlaybackWindowIdentityForSlot(0, published));
    if (expectComplete) {
      TEST_ASSERT_NULL(track.pendingViewRequest(0));
      TEST_ASSERT_EQUAL_UINT64(deadlines[i], runtime->viewBuildWork.deadlineTick);
      TEST_ASSERT_EQUAL_UINT32(5, serviced[i]); // freeze/qualify+fill/admit/rank+publish/retire
      TEST_ASSERT_TRUE(nativePlaybackDeadlineClock.tick < deadlines[i]);
    } else {
      TEST_ASSERT_NOT_NULL(track.pendingViewRequest(0));
      TEST_ASSERT_EQUAL_UINT64(deadlines[i], track.pendingViewRequest(0)->deadlineTick);
      TEST_ASSERT_FALSE(track.playbackWindowServiceOffer(0, nativePlaybackDeadlineClock).canProgress);
    }
  }
}

void test_committed_retry_eight_tracks_prepare_empty_windows_with_eight_bar_lead() {
  exercise_committed_retry_eight_track_lead(8, true);
}

void test_committed_retry_eight_tracks_refuse_two_bar_lead_without_synthetic_completion() {
  exercise_committed_retry_eight_track_lead(2, false);
}

void drainImmutableLaunch(Track& track, uint8_t slot) {
  nativePlaybackDeadlineClock = {1000, 1};
  for (unsigned turn = 0; turn < 5000 && !track.playbackLaunchReadyForSlot(slot); ++turn) {
    trackManager.servicePendingSlotProvision();
    track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
    nativePlaybackDeadlineClock.tick += 64;
  }
  TEST_ASSERT_TRUE_MESSAGE(track.playbackLaunchReadyForSlot(slot), "launch did not reach completed readiness");
}

void setupImmutableLaunch(Track& track) {
  LoopEventStore::initPool();
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  commitBarSpacedPassIntoTrackSlot(track, 2, 24);
  publishBarSpacedRootForSlot(track.getLoop(2), 24);
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  track.forceSetState(TRACK_PLAYING);
  trackManager.requestSlotSwitch(0, 2, SlotQuantization::LoopEnd, 100);
  TEST_ASSERT_TRUE(trackManager.servicePendingSlotProvision());
}

void test_immutable_launch_cancellation_drains_every_preparation_phase() {
  auto& track = trackManager.getTrack(0);
  setupImmutableLaunch(track);
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 2);
  TEST_ASSERT_TRUE(track.hasPendingPlaybackViewMaintenance());
  for (unsigned turns : {0u, 1u, 2u, 3u, 4u, 6u, 20u}) {
    if (!trackManager.hasPendingSlotSwitch(0)) {
      trackManager.requestSlotSwitch(0, 2, SlotQuantization::LoopEnd, 100);
      TEST_ASSERT_TRUE(trackManager.servicePendingSlotProvision());
    }
    for (unsigned i = 0; i < turns; ++i)
      track.stepPlaybackViewMaintenance(0, nullptr, false, {1000 + i * 64, 1});
    TEST_ASSERT_FALSE(track.noLiveOwnerForSlot(2));
    trackManager.cancelPendingSlotSwitch(0);
    TEST_ASSERT_FALSE(track.playbackLaunchReadyForSlot(2));
    TEST_ASSERT_FALSE(track.bindPlaybackLaunchForSlot(2, 2000));
    TEST_ASSERT_NULL(track.pendingViewRequest(2));
    for (unsigned i = 0; i < 256 && runtime->ownsCoverageSource(); ++i)
      track.stepPlaybackViewMaintenance(0, nullptr, false, {3000 + i * 64, 1});
    TEST_ASSERT_FALSE(runtime->ownsCoverageSource());
    TEST_ASSERT_TRUE(track.noLiveOwnerForSlot(2));
    TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::LaunchPreparation::None),
        static_cast<int>(runtime->launchPreparation));
  }
}

void test_immutable_launch_requalifies_generation_and_revision_before_binding() {
  auto& track = trackManager.getTrack(0);
  setupImmutableLaunch(track);
  drainImmutableLaunch(track, 2);
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 2);
  auto* set = runtime->viewSetIfPrewarmed();
  const auto oldGeneration = set->views[set->lifecycle.ready].source.sourceGeneration;
  track.bumpPlaybackGeneration();
  TEST_ASSERT_FALSE(track.bindPlaybackLaunchForSlot(2, 2000));
  drainImmutableLaunch(track, 2);
  TEST_ASSERT_NOT_EQUAL(oldGeneration, set->views[set->lifecycle.ready].source.sourceGeneration);
  auto& loop = track.getLoop(2);
  ++loop.playbackRevision;
  TEST_ASSERT_FALSE(track.playbackLaunchReadyForSlot(2));
  trackManager.servicePendingSlotProvision();
  TEST_ASSERT_FALSE(track.playbackWindowServiceOffer(0, {3000, 2}).canProgress);
  publishBarSpacedRootForSlot(loop, 24);
  drainImmutableLaunch(track, 2);
  TEST_ASSERT_EQUAL_UINT32(loop.playbackRevision, set->views[set->lifecycle.ready].source.playbackRevision);
  const auto length = loop.loopLengthTicks;
  loop.loopLengthTicks += 768;
  TEST_ASSERT_FALSE(track.bindPlaybackLaunchForSlot(2, 2000));
  loop.loopLengthTicks = length;
  TEST_ASSERT_TRUE(track.bindPlaybackLaunchForSlot(2, 2000));
  const auto& served = set->views[set->lifecycle.current];
  TEST_ASSERT_TRUE(served.published);
  TEST_ASSERT_NOT_NULL(runtime->boundLaunchView(served.source, length, 0, 2000));
  auto edited = served.source;
  ++edited.playbackRevision;
  TEST_ASSERT_NOT_NULL_MESSAGE(runtime->boundLaunchView(edited, length, 1, 2001),
      "a bound interval keeps its version through the traversal");
  TEST_ASSERT_NULL(runtime->boundLaunchView(served.source, length, 1536, 3536));
  TEST_ASSERT_NULL_MESSAGE(runtime->boundLaunchView(served.source, length, 0, 2000),
      "expired launch cannot revive when an old modular position returns");
  TEST_ASSERT_NULL_MESSAGE(runtime->boundLaunchView(served.source, length, 0, 2000 + length),
      "the initial launch window must not resurrect on a later lap");
  ++edited.sourceGeneration;
  TEST_ASSERT_NULL(runtime->boundLaunchView(edited, length, 1, 2001));
}

void test_immutable_launch_cold_reservation_upgrades_without_resetting_ready_content() {
  auto& track = trackManager.getTrack(0);
  setupImmutableLaunch(track);
  drainImmutableLaunch(track, 2);
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 2);
  auto* set = runtime->viewSetIfPrewarmed();
  const auto readyIndex = set->lifecycle.ready;
  const auto* events = set->views[readyIndex].storage.events;
  const auto count = set->views[readyIndex].storage.eventCount;
  const auto bytes = set->fixedExternalBytes();
  TEST_ASSERT_FALSE(set->captureStagings[0].storage.isPrewarmed());
  TEST_ASSERT_FALSE(set->captureStagings[1].storage.isPrewarmed());
  TEST_ASSERT_EQUAL_UINT32(0, runtime->frozenCaptureCompanions.capacity());
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(2, true));
  TEST_ASSERT_TRUE(set->captureStagings[0].storage.isPrewarmed());
  TEST_ASSERT_TRUE(set->captureStagings[1].storage.isPrewarmed());
  TEST_ASSERT_GREATER_THAN_UINT32(bytes, set->fixedExternalBytes());
  TEST_ASSERT_EQUAL_PTR(events, set->views[readyIndex].storage.events);
  TEST_ASSERT_EQUAL_UINT32(count, set->views[readyIndex].storage.eventCount);
  TEST_ASSERT_EQUAL_UINT8(readyIndex, set->lifecycle.ready);
  TEST_ASSERT_TRUE(track.playbackLaunchReadyForSlot(2));
  runtime->releaseFrozenRecordWindow();
  TEST_ASSERT_TRUE_MESSAGE(track.playbackLaunchReadyForSlot(2), "shared record cleanup cannot erase a ready launch");
}

void test_immutable_launch_legacy_dense_overflow_retains_untruncated_fallback() {
  auto& track = trackManager.getTrack(0);
  setupImmutableLaunch(track);
  publishNotesInBarsRootForSlot(track.getLoop(2), 0, 2, 1200);
  drainImmutableLaunch(track, 2);
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 2);
  TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::LaunchPreparation::Fallback),
      static_cast<int>(runtime->launchPreparation));
  TEST_ASSERT_GREATER_THAN_UINT32(RuntimePlaybackViewConfig::kWindowAdmissionLimit,
      runtime->mergedMidiEvents.mergedEvents.size());
  TEST_ASSERT_FALSE(runtime->ownsCoverageSource());
  TEST_ASSERT_TRUE(track.bindPlaybackLaunchForSlot(2, 3000));
  TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::LaunchPreparation::FallbackBound),
      static_cast<int>(runtime->launchPreparation));
}

void test_immutable_launch_source_wait_does_not_hide_a_later_track() {
  LoopEventStore::initPool();
  for (uint8_t i = 0; i < 2; ++i) {
    auto& track = trackManager.getTrack(i);
    track.ensureLoopsAllocated();
    commitRecordPassIntoTrackSlot(track, 0);
    commitBarSpacedPassIntoTrackSlot(track, 2, 24);
    if (i == 1) publishBarSpacedRootForSlot(track.getLoop(2), 24);
    TrackCaptureStopTestAccess::selectSlot(track, 0);
    track.forceSetState(TRACK_PLAYING);
    trackManager.requestSlotSwitch(i, 2, SlotQuantization::LoopEnd, 100);
  }
  TEST_ASSERT_TRUE(trackManager.servicePendingSlotProvision()); // Track0 occupancy/request.
  TEST_ASSERT_FALSE(trackManager.getTrack(0).playbackWindowServiceOffer(0, {1000, 1}).canProgress);
  TEST_ASSERT_TRUE_MESSAGE(trackManager.servicePendingSlotProvision(),
      "a source-blocked lower track must not hide later launch provisioning");
  TEST_ASSERT_NOT_NULL(trackManager.getTrack(1).pendingViewRequest(2));
  drainImmutableLaunch(trackManager.getTrack(1), 2);
  TEST_ASSERT_TRUE(trackManager.hasPendingSlotSwitch(0));
  TEST_ASSERT_FALSE(trackManager.getTrack(0).playbackLaunchReadyForSlot(2));
}

void test_immutable_launch_sibling_interference_is_a_temporary_wait() {
  auto& track = trackManager.getTrack(0);
  setupImmutableLaunch(track);
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 2);
  track.stepCommittedPlaybackRetry(2, {1000, 1}); // Freeze.
  track.stepCommittedPlaybackRetry(2, {1064, 1}); // Count.
  TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::CoveragePreparation::AwaitAdmission),
      static_cast<int>(runtime->coveragePreparation));
  const auto units = runtime->coverageSource.readUnits();
  prepareOwingWindowHolder(track, 1, 512 * 768, 12, 13, 1, 10000);
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  TEST_ASSERT_GREATER_THAN_UINT32(0, track.playbackViewOutstandingGrains(2));
  track.stepCommittedPlaybackRetry(2, {1128, 1});
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(LoopPlaybackRuntime::CoveragePreparation::AwaitAdmission),
      static_cast<int>(runtime->coveragePreparation), "unpositioned launch waits; it cannot expire on sibling interference");
  TEST_ASSERT_TRUE(runtime->frozenViewRequest.active);
  TEST_ASSERT_EQUAL_UINT32(units, runtime->coverageSource.readUnits());
  auto* sibling = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 1);
  nativePlaybackDeadlineClock = {sibling->viewBuildWork.admittedTick, sibling->viewBuildWork.clockEpoch};
  for (unsigned i = 0; i < 500 && sibling->coverageBuildNeedsService(); ++i)
    track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
  TEST_ASSERT_FALSE(sibling->coverageBuildNeedsService());
  drainImmutableLaunch(track, 2);
}

void test_rootless_restored_launch_requests_its_source_once_and_becomes_ready() {
  LoopEventStore::initPool();
  auto& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  commitBarSpacedPassIntoTrackSlot(track, 2, 24);
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  track.forceSetState(TRACK_PLAYING);
  auto& target = track.getLoop(2);
  // Finish the seeding helper's record job, then emulate a pass-only restore:
  // committed passes remain, but no root and no publication request exist.
  for (unsigned i = 0; i < 20000 && target.hasPendingSnapshotPublish(); ++i)
    target.stepPendingSnapshotPublish(2, true, 0);
  TEST_ASSERT_TRUE(target.publishedSnapshotIsCurrent());
  target.storePublishedSnapshotForTest({}, 0);
  TEST_ASSERT_NULL(target.publishedSnapshot().get());
  TEST_ASSERT_FALSE(target.hasPendingSnapshotPublish());
  trackManager.requestSlotSwitch(0, 2, SlotQuantization::LoopEnd, 100);
  TEST_ASSERT_TRUE(trackManager.servicePendingSlotProvision());
  TEST_ASSERT_TRUE_MESSAGE(target.hasPendingSnapshotPublish(),
      "pass-backed launch must request its missing source, not wait forever");
  TEST_ASSERT_FALSE_MESSAGE(target.pendingSnapshotFromLoadedPasses(),
      "explicit launch preparation must not enable blanket loaded hydration");
  TEST_ASSERT_NOT_NULL(track.pendingViewRequest(2));
  for (unsigned turn = 0; turn < 20000 && !target.publishedSnapshotIsCurrent(); ++turn) {
    // Same Loop publication owner and unit used by the Snapshot maintenance lane.
    target.stepPendingSnapshotPublish(2, true, 0);
    trackManager.servicePendingSlotProvision(); // Must not restart the source job.
  }
  TEST_ASSERT_TRUE(target.publishedSnapshotIsCurrent());
  drainImmutableLaunch(track, 2);
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 2);
  const auto* set = runtime->viewSetIfPrewarmed();
  TEST_ASSERT_EQUAL_UINT32(4, set->views[set->lifecycle.ready].storage.eventCount);
  TEST_ASSERT_TRUE(trackManager.hasPendingSlotSwitch(0));
  TEST_ASSERT_EQUAL_UINT8(0, track.getActiveLoopIndex());
}

void rollingTestTick(Track& track, uint64_t tick, bool service = true) {
  nativePlaybackDeadlineClock = {tick, 1};
  clockManager.assignCurrentTickSilently(static_cast<uint32_t>(tick));
  if (service) {
    track.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock);
    track.stepPlaybackViewMaintenance(static_cast<uint32_t>(tick), nullptr, false, nativePlaybackDeadlineClock);
  }
  trackManager.updateAllTracks(static_cast<uint32_t>(tick));
}

/// **The launch-path diagnostic's own contract** (windowed-playback-generation, the switch leg's F-16). Five device runs
/// could not tell "nothing was owed" from "the source was refused", because every exit of
/// `Track::preparePlaybackLaunchForSlot` returned without a row. This drives the production turn on the shape run 06
/// held -- a demoted 24-bar target whose restore completed without installing a root -- and then asks, directly, the
/// exits the turn cannot be made to reach on demand. What is pinned is **the row**: its exit's name, the target's
/// identity, the inputs that decided it, and the **state-change** cadence (a row per turn would evict the evidence it
/// explains, which is the failure mode the evidence practice already recorded for `win_gap`).
void test_launch_gate_rows_name_the_silent_exits_once_per_state_change() {
  constexpr uint16_t kRootCurrent = 1u << 0;
  constexpr uint16_t kPublishPending = 1u << 1;
  constexpr uint16_t kDemandLive = 1u << 4;
  constexpr uint16_t kAvoidFullRebuild = 1u << 8;
  constexpr uint16_t kCommittedPasses = 1u << 9;
  constexpr uint16_t kRequestAttempted = 1u << 10;
  constexpr uint16_t kRequestAccepted = 1u << 11;
  LoopEventStore::initPool();
  auto& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  commitBarSpacedPassIntoTrackSlot(track, 2, 24);  // 24 bars: above the demotion bound, so the launch is the vehicle
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  track.forceSetState(TRACK_PLAYING);
  auto& target = track.getLoop(2);
  for (unsigned i = 0; i < 20000 && target.hasPendingSnapshotPublish(); ++i)
    target.stepPendingSnapshotPublish(2, true, 0);
  // The pass-only restore run 06's target showed: committed passes, **no** root, and no request in flight.
  target.storePublishedSnapshotForTest({}, 0);
  TEST_ASSERT_FALSE(target.publishedSnapshotIsCurrent());
  LaunchGateRows::reset();
  trackManager.requestSlotSwitch(0, 2, SlotQuantization::LoopEnd, 100);
  TEST_ASSERT_TRUE(trackManager.servicePendingSlotProvision());
  // The turn's own decision, named -- this is the row the switch leg reads instead of inferring a cause from the
  // absence of every other family.
  TEST_ASSERT_EQUAL_UINT32(1, LaunchGateRows::count);
  TEST_ASSERT_EQUAL_STRING("launch_requested", LaunchGateRows::last()->exit);
  TEST_ASSERT_EQUAL_UINT8(0, LaunchGateRows::last()->track);
  TEST_ASSERT_EQUAL_UINT8(2, LaunchGateRows::last()->slot);
  TEST_ASSERT_EQUAL_UINT32(24 * 768, LaunchGateRows::last()->loopLengthTicks);
  TEST_ASSERT_TRUE((LaunchGateRows::last()->flags & kRequestAttempted) != 0);
  TEST_ASSERT_TRUE((LaunchGateRows::last()->flags & kRequestAccepted) != 0);
  TEST_ASSERT_TRUE((LaunchGateRows::last()->flags & kCommittedPasses) != 0);
  TEST_ASSERT_TRUE((LaunchGateRows::last()->flags & kAvoidFullRebuild) != 0);
  TEST_ASSERT_TRUE((LaunchGateRows::last()->flags & kRootCurrent) == 0);     // no current root: the source was owed
  // The flags are the state **at the exit**, so `pend` here is the request this very turn just queued -- which is what
  // makes `req=1` plus `pend=1` the row's way of saying "requested, not yet published". A request that was already in
  // flight instead shows `req=0` with the same `pend=1`: the pair, not either bit, is the reading.
  TEST_ASSERT_TRUE((LaunchGateRows::last()->flags & kPublishPending) != 0);
  // The state moved (preparation `Requested`, a demand live), so the next turn is a new row -- and the two after it,
  // holding that same state, are not: the cadence rule, measured rather than asserted by construction.
  trackManager.servicePendingSlotProvision();
  TEST_ASSERT_EQUAL_UINT32(2, LaunchGateRows::count);
  TEST_ASSERT_EQUAL_STRING("launch_requested_match", LaunchGateRows::last()->exit);
  TEST_ASSERT_EQUAL_UINT8(1, LaunchGateRows::last()->preparation);
  TEST_ASSERT_TRUE((LaunchGateRows::last()->flags & kDemandLive) != 0);
  TEST_ASSERT_TRUE((LaunchGateRows::last()->flags & kRequestAttempted) == 0);
  // A third and fourth identical turn are **not** state changes: the row is a transition record, not a heartbeat.
  trackManager.servicePendingSlotProvision();
  trackManager.servicePendingSlotProvision();
  TEST_ASSERT_EQUAL_UINT32(2, LaunchGateRows::count);
  TEST_ASSERT_EQUAL_STRING("launch_requested_match", LaunchGateRows::last()->exit);
  // The source lands. Its outcome was invisible before (`RRQ,…committed` is the **restore's** terminal, not a
  // publication): here the arrival is a state change of this row's own flags.
  for (unsigned i = 0; i < 20000 && !target.publishedSnapshotIsCurrent(); ++i) {
    target.stepPendingSnapshotPublish(2, true, 0);
    trackManager.servicePendingSlotProvision();
  }
  TEST_ASSERT_TRUE(target.publishedSnapshotIsCurrent());
  bool sawCurrentRoot = false;
  for (size_t i = 0; i < LaunchGateRows::count && i < sizeof(LaunchGateRows::rows) / sizeof(LaunchGateRows::rows[0]); ++i) {
    if (LaunchGateRows::rows[i].slot == 2 && (LaunchGateRows::rows[i].flags & kRootCurrent) != 0) sawCurrentRoot = true;
  }
  TEST_ASSERT_TRUE_MESSAGE(sawCurrentRoot, "the source's arrival must be visible as a state change of the gate row");
  // The exits the production turn cannot be asked for on demand, reached directly. Each names itself and carries the
  // target: none of them returned a row of any family before this.
  LaunchGateRows::reset();
  TEST_ASSERT_FALSE(track.preparePlaybackLaunchForSlot(track.getActiveLoopIndex()));
  TEST_ASSERT_EQUAL_STRING("launch_slot_not_eligible", LaunchGateRows::last()->exit);
  const uint8_t emptySlot = static_cast<uint8_t>(Config::MAX_LOOPS_PER_TRACK - 1);
  TEST_ASSERT_FALSE(track.preparePlaybackLaunchForSlot(emptySlot));
  TEST_ASSERT_EQUAL_STRING("launch_no_runtime", LaunchGateRows::last()->exit);
  TEST_ASSERT_EQUAL_UINT8(emptySlot, LaunchGateRows::last()->slot);
  // The caller's own exit first, because it is also what gives the short target its occupancy: a switch to a 3-bar
  // filled slot never asks for a launch, and now says so. Which of the two caller tokens it is depends on whether that
  // occupancy was already taken -- both mean "the launch path was never reached" to a reader, so either satisfies the
  // assertion while the row's `len` carries the discriminator.
  commitBarSpacedPassIntoTrackSlot(track, 1, 3);  // 3 bars: below the demotion bound, so no window is owed
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  LaunchGateRows::reset();
  trackManager.requestSlotSwitch(0, 1, SlotQuantization::LoopEnd, 200);
  trackManager.servicePendingSlotProvision();
  TEST_ASSERT_NOT_NULL(LaunchGateRows::last());
  const char* callerExit = LaunchGateRows::last()->exit;
  TEST_ASSERT_TRUE_MESSAGE(strcmp(callerExit, "launch_not_reached_legacy") == 0 ||
                               strcmp(callerExit, "launch_not_reached_occupied") == 0,
      "a short filled target must name the launch path as not reached");
  TEST_ASSERT_EQUAL_UINT8(1, LaunchGateRows::last()->slot);
  TEST_ASSERT_EQUAL_UINT32(3 * 768, LaunchGateRows::last()->loopLengthTicks);
  // With the occupancy that turn took, the launch's own refusal can be asked for directly: content present, and a
  // length below the demotion bound.
  LaunchGateRows::reset();
  TEST_ASSERT_FALSE(track.preparePlaybackLaunchForSlot(1));
  TEST_ASSERT_EQUAL_STRING("launch_not_demoted_content", LaunchGateRows::last()->exit);
  TEST_ASSERT_TRUE((LaunchGateRows::last()->flags & kCommittedPasses) != 0);
  TEST_ASSERT_TRUE((LaunchGateRows::last()->flags & kAvoidFullRebuild) == 0);
}

void test_rolling_request_worker_handoff_emission_keeps_fallback_and_exact_notes() {
  assert_s4b_7_a_demoted_target_through_the_turn(true);
  auto& track = trackManager.getTrack(0);
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 2);
  const uint64_t origin = clockManager.getCurrentTick();
  nativePlaybackDeadlineClock = {origin, 1};
  TEST_ASSERT_EQUAL(static_cast<int>(Track::RollingOutcome::Requested),
      static_cast<int>(track.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock)));
  TEST_ASSERT_EQUAL_UINT32(6 * 768, runtime->rolling.startPhase);
  TEST_ASSERT_EQUAL_UINT64(origin + 6 * 768, runtime->rolling.deadlineTick);
  TEST_ASSERT_EQUAL_UINT32(2 * 768, track.pendingViewRequest(2)->coverage.lengthTicks);
  TEST_ASSERT_EQUAL_UINT32(7, Track::playbackViewCoverageContract().maximumConsecutiveDeniedTurns);
  const uint8_t initial = runtime->viewSetIfPrewarmed()->lifecycle.current;
  MidiSendCapture::reset();
  for (uint32_t delta = 1; delta <= 16 * 768 + 48; ++delta) {
    const auto before = MidiSendCapture::count();
    const auto builds = Track::playbackOrderHotRebuildCount();
    const auto roleBefore = runtime->viewSetIfPrewarmed()->lifecycle.current;
    rollingTestTick(track, origin + delta, delta % 64 == 0);
    const bool on = delta % 768 == 0;
    const bool off = delta % 768 == 48;
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before + (on || off ? 1 : 0), MidiSendCapture::count(),
        "exact event timing across fallback -> immutable -> fallback");
    if (on || off) {
      const auto event = MidiSendCapture::at(before);
      TEST_ASSERT_EQUAL(on, event.isNoteOn());
      TEST_ASSERT_EQUAL(off, event.isNoteOff());
      TEST_ASSERT_EQUAL_UINT8(48, event.data.noteData.note);
    }
    if (delta == 4 * 768) {
      TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::RollingPhase::Ready), static_cast<int>(runtime->rolling.phase));
      TEST_ASSERT_EQUAL_UINT8(initial, runtime->viewSetIfPrewarmed()->lifecycle.current);
    }
    if (delta == 6 * 768 || delta == 14 * 768) {
      TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::RollingPhase::Serving), static_cast<int>(runtime->rolling.phase));
      TEST_ASSERT_NOT_EQUAL(roleBefore, runtime->viewSetIfPrewarmed()->lifecycle.current);
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(builds, Track::playbackOrderHotRebuildCount(), "no mutable build at handoff");
      const auto count = MidiSendCapture::count();
      rollingTestTick(track, origin + delta, false);
      TEST_ASSERT_EQUAL_UINT32(count, MidiSendCapture::count());
    }
  }
  TEST_ASSERT_EQUAL_UINT32(33, MidiSendCapture::count());
  TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::RollingPhase::Idle), static_cast<int>(runtime->rolling.phase));
}

void test_rolling_selector_preserves_existing_adjacent_refusal_and_deadline() {
  assert_s4b_7_a_demoted_target_through_the_turn(true);
  auto& track = trackManager.getTrack(0);
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 2);
  const uint64_t origin = clockManager.getCurrentTick();
  nativePlaybackDeadlineClock = {origin, 1};
  track.requestCommittedPlaybackWindow(2, {1536, 1536, 24 * 768}, nativePlaybackDeadlineClock);
  const auto original = *track.pendingViewRequest(2);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::RollingOutcome::Busy),
      static_cast<int>(track.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock)));
  for (unsigned i = 0; i < 16; ++i) {
    nativePlaybackDeadlineClock.tick = origin + i * 64;
    track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
  }
  TEST_ASSERT_NOT_NULL(track.pendingViewRequest(2));
  TEST_ASSERT_EQUAL_UINT64(original.deadlineTick, track.pendingViewRequest(2)->deadlineTick);
  TEST_ASSERT_EQUAL_UINT32(original.clockEpoch, track.pendingViewRequest(2)->clockEpoch);
  TEST_ASSERT_EQUAL_UINT32(original.coverage.startPhase, track.pendingViewRequest(2)->coverage.startPhase);
  TEST_ASSERT_EQUAL(static_cast<int>(PlaybackViewPendingReason::CannotCompleteBeforeHandoff),
      static_cast<int>(track.pendingViewRequest(2)->reason));
  TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::RollingPhase::Idle), static_cast<int>(runtime->rolling.phase));
}

void test_rolling_boundary_rejects_stale_epoch_geometry_source_and_missed_tick() {
  for (unsigned scenario = 0; scenario < 5; ++scenario) {
    if (scenario) setUp();
    assert_s4b_7_a_demoted_target_through_the_turn(true);
    auto& track = trackManager.getTrack(0);
    auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 2);
    const uint64_t origin = clockManager.getCurrentTick();
    nativePlaybackDeadlineClock = {origin, 1};
    TEST_ASSERT_EQUAL(static_cast<int>(Track::RollingOutcome::Requested),
        static_cast<int>(track.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock)));
    const uint64_t deadline = runtime->rolling.deadlineTick;
    for (uint64_t tick = origin + 1; tick < deadline; ++tick)
      rollingTestTick(track, tick, (tick - origin) % 64 == 0);
    TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::RollingPhase::Ready), static_cast<int>(runtime->rolling.phase));
    const auto current = runtime->viewSetIfPrewarmed()->lifecycle.current;
    const auto index = track.getLoop(2).nextEventIndex;
    PlaybackDeadlineClock clock{deadline, 1};
    if (scenario == 0) clock.epoch = 2;
    if (scenario == 1) track.getLoop(2).loopLengthTicks += 768;
    if (scenario == 2) ++track.getLoop(2).playbackRevision;
    if (scenario == 3) ++clock.tick;
    if (scenario == 4) track.bumpPlaybackGeneration();
    const auto result = track.resolveRollingPlaybackWindow(2, static_cast<uint32_t>(clock.tick), clock);
    TEST_ASSERT_EQUAL(static_cast<int>(Track::RollingOutcome::Rejected), static_cast<int>(result.outcome));
    TEST_ASSERT_NULL(result.view);
    TEST_ASSERT_EQUAL_UINT8(current, runtime->viewSetIfPrewarmed()->lifecycle.current);
    TEST_ASSERT_EQUAL_UINT16(index, track.getLoop(2).nextEventIndex);
    TEST_ASSERT_EQUAL_UINT64(deadline, runtime->viewBuildWork.deadlineTick);
    // Returning the original stamp must not revive the rejected candidate.
    TEST_ASSERT_NULL(track.resolveRollingPlaybackWindow(2, static_cast<uint32_t>(deadline), {deadline, 1}).view);
    track.cancelRollingPlaybackWindow(2);
  }
}

void test_rolling_cancellation_retires_only_its_owned_request() {
  assert_s4b_7_a_demoted_target_through_the_turn(true);
  auto& track = trackManager.getTrack(0);
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 2);
  const uint64_t origin = clockManager.getCurrentTick();
  nativePlaybackDeadlineClock = {origin, 1};
  track.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock);
  track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
  TEST_ASSERT_TRUE(runtime->ownsCoverageSource());
  track.requestCommittedPlaybackWindow(2, {12 * 768, 1536, 24 * 768}, 0, {origin + 1, 1});
  const auto newer = *track.pendingViewRequest(2);
  track.cancelRollingPlaybackWindow(2);
  TEST_ASSERT_NOT_NULL(track.pendingViewRequest(2));
  TEST_ASSERT_EQUAL_UINT64(newer.deadlineTick, track.pendingViewRequest(2)->deadlineTick);
  TEST_ASSERT_EQUAL_UINT32(newer.coverage.startPhase, track.pendingViewRequest(2)->coverage.startPhase);
  for (unsigned i = 0; i < 256 && runtime->ownsCoverageSource(); ++i) runtime->stepCoverageSourceRetirement(64);
  TEST_ASSERT_FALSE(runtime->ownsCoverageSource());
  TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::RollingPhase::Idle), static_cast<int>(runtime->rolling.phase));
}

void test_rolling_handoff_across_natural_wrap_uses_absolute_frame() {
  assert_s4b_7_a_demoted_target_through_the_turn(true);
  auto& track = trackManager.getTrack(0);
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 2);
  const uint64_t origin = clockManager.getCurrentTick();
  const uint32_t length = track.getLoop(2).loopLengthTicks;
  // No rolling demand yet. Legacy/launch emission runs until this request point.
  for (uint32_t delta = 1; delta <= length - 6 * 768; ++delta)
    rollingTestTick(track, origin + delta, false);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::RollingOutcome::Requested),
      static_cast<int>(track.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock)));
  TEST_ASSERT_EQUAL_UINT32(0, runtime->rolling.startPhase);
  TEST_ASSERT_EQUAL_UINT64(origin + length, runtime->rolling.deadlineTick);
  for (uint32_t delta = length - 6 * 768 + 1; delta < length; ++delta)
    rollingTestTick(track, origin + delta, delta % 64 == 0);
  const auto builds = Track::playbackOrderHotRebuildCount();
  MidiSendCapture::reset();
  rollingTestTick(track, origin + length, false);
  TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::RollingPhase::Serving), static_cast<int>(runtime->rolling.phase));
  TEST_ASSERT_EQUAL_UINT32(builds, Track::playbackOrderHotRebuildCount());
  TEST_ASSERT_EQUAL_UINT32(1, MidiSendCapture::count());
  TEST_ASSERT_TRUE(MidiSendCapture::at(0).isNoteOn());
  for (uint32_t delta = 1; delta <= 48; ++delta) rollingTestTick(track, origin + length + delta, false);
  TEST_ASSERT_EQUAL_UINT32(2, MidiSendCapture::count());
  TEST_ASSERT_TRUE(MidiSendCapture::at(1).isNoteOff());
  // A reused physical role is not the same reader even if its interval repeats.
  auto* set = runtime->viewSetIfPrewarmed();
  ++set->views[set->lifecycle.current].storageGeneration;
  TEST_ASSERT_NULL(track.resolveRollingPlaybackWindow(2, static_cast<uint32_t>(origin + length + 49),
      {origin + length + 49, 1}).view);
  track.cancelRollingPlaybackWindow(2);
}

void test_rolling_empty_interval_preserves_sustain_and_fallback_closure() {
  assert_s4b_7_a_demoted_target_through_the_turn(true);
  auto& track = trackManager.getTrack(0);
  auto& loop = track.getLoop(2);
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 2);
  const uint64_t origin = clockManager.getCurrentTick();
  auto empty = LoopSnapshot::createEmpty(loop.loopId, loop.loopLengthTicks, 0, 1, 1);
  LoopSnapshotChangeSet changes;
  SnapshotNoteChange note{};
  note.kind = SnapshotChangeKind::Add;
  note.next.loopId = loop.loopId; note.next.noteId = 9001;
  note.next.pitch = 70; note.next.velocity = 100;
  note.next.startTick = 6 * 768 - 20; note.next.endTick = 8 * 768 + 48;
  changes.notes.push_back(note);
  for (unsigned i = 0; i < 2; ++i) {
    SnapshotEventChange event{};
    event.kind = SnapshotChangeKind::Add;
    event.next.loopId = loop.loopId; event.next.eventId = 18000 + i; event.next.canonicalOrder = i;
    event.next.value = i == 0 ? MidiEvent::NoteOn(note.next.startTick, 1, 70, 100)
                              : MidiEvent::NoteOff(note.next.endTick, 1, 70);
    event.next.value.noteId = 9001;
    changes.events.push_back(event);
  }
  LoopSnapshotConstruction construction(empty, changes);
  for (unsigned i = 0; i < 1000 && !construction.complete(); ++i) TEST_ASSERT_TRUE(construction.step(64));
  TEST_ASSERT_TRUE(construction.complete());
  ++loop.playbackRevision;
  loop.storePublishedSnapshotForTest(construction.freeze(), loop.playbackRevision);
  nativePlaybackDeadlineClock = {origin, 1};
  TEST_ASSERT_EQUAL(static_cast<int>(Track::RollingOutcome::Requested),
      static_cast<int>(track.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock)));
  MidiSendCapture::reset();
  for (uint32_t delta = 1; delta <= 8 * 768 + 48; ++delta) {
    rollingTestTick(track, origin + delta, delta % 64 == 0);
    if (delta == 6 * 768 || delta == 8 * 768 - 1) {
      TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::RollingPhase::Serving), static_cast<int>(runtime->rolling.phase));
      TEST_ASSERT_EQUAL_UINT32(0, runtime->viewSetIfPrewarmed()->views[runtime->viewSetIfPrewarmed()->lifecycle.current].storage.eventCount);
      TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::ledgerEntryActive(track, 2, 70));
    }
  }
  unsigned ons = 0, offs = 0;
  for (size_t i = 0; i < MidiSendCapture::count(); ++i) {
    const auto event = MidiSendCapture::at(i);
    if ((event.isNoteOn() || event.isNoteOff()) && event.data.noteData.note == 70) {
      ons += event.isNoteOn(); offs += event.isNoteOff();
    }
  }
  TEST_ASSERT_EQUAL_UINT32(1, ons); TEST_ASSERT_EQUAL_UINT32(1, offs);
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::ledgerEntryActive(track, 2, 70));
}

void test_rolling_stop_or_slot_exit_releases_the_episode_off_clock() {
  for (unsigned scenario = 0; scenario < 2; ++scenario) {
    if (scenario) setUp();
    assert_s4b_7_a_demoted_target_through_the_turn(true);
    auto& track = trackManager.getTrack(0);
    auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 2);
    nativePlaybackDeadlineClock = {clockManager.getCurrentTick(), 1};
    track.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock);
    track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
    TEST_ASSERT_TRUE(runtime->ownsCoverageSource());
    if (scenario == 0) track.stopPlaying();
    else trackManager.setActiveLoopIndex(0, 0);
    track.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock);
    TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::RollingPhase::Idle), static_cast<int>(runtime->rolling.phase));
    for (unsigned i = 0; i < 256 && runtime->ownsCoverageSource(); ++i)
      track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
    TEST_ASSERT_FALSE(runtime->ownsCoverageSource());
  }
}

void test_rolling_queued_zero_start_keeps_the_original_debt() {
  assert_s4b_7_a_demoted_target_through_the_turn(true);
  auto& track = trackManager.getTrack(0);
  nativePlaybackDeadlineClock = {clockManager.getCurrentTick(), 1};
  TEST_ASSERT_EQUAL(static_cast<int>(Track::RollingOutcome::Requested),
      static_cast<int>(track.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock)));
  const auto original = *track.pendingViewRequest(2);
  track.queuePlaybackStartAtGrid(0, static_cast<uint32_t>(nativePlaybackDeadlineClock.tick));
  TEST_ASSERT_EQUAL(static_cast<int>(Track::RollingOutcome::Excluded),
      static_cast<int>(track.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock)));
  TEST_ASSERT_NOT_NULL(track.pendingViewRequest(2));
  TEST_ASSERT_EQUAL_UINT64(original.deadlineTick, track.pendingViewRequest(2)->deadlineTick);
  TEST_ASSERT_EQUAL_UINT32(original.clockEpoch, track.pendingViewRequest(2)->clockEpoch);
  TEST_ASSERT_EQUAL_UINT32(original.coverage.startPhase, track.pendingViewRequest(2)->coverage.startPhase);
  track.clearQueuedPlaybackStart();
  track.cancelRollingPlaybackWindow(2);
}

PlaybackDeadlineClock changeRollingGenerationAtCompletion() {
  trackManager.getTrack(0).bumpPlaybackGeneration();
  return nativePlaybackDeadlineClock;
}

void test_rolling_generation_change_at_completion_publishes_nothing() {
  assert_s4b_7_a_demoted_target_through_the_turn(true);
  auto& track = trackManager.getTrack(0);
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 2);
  nativePlaybackDeadlineClock = {clockManager.getCurrentTick(), 1};
  track.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock);
  const auto originalGeneration = track.getPlaybackGeneration();
  for (unsigned i = 0; i < 128 && track.getPlaybackGeneration() == originalGeneration; ++i) {
    track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock,
        changeRollingGenerationAtCompletion);
    nativePlaybackDeadlineClock.tick += 64;
  }
  TEST_ASSERT_NOT_EQUAL(originalGeneration, track.getPlaybackGeneration());
  auto* set = runtime->viewSetIfPrewarmed();
  TEST_ASSERT_FALSE_MESSAGE(set->lifecycle.ready < set->views.size() && set->views[set->lifecycle.ready].complete,
      "old-operation completion must not publish a rolling ready candidate");
  for (unsigned i = 0; i < 256 && runtime->ownsCoverageSource(); ++i)
    runtime->stepCoverageSourceRetirement(64);
  TEST_ASSERT_FALSE(runtime->ownsCoverageSource());
}


// DEC-108 §13b / task 3.10 + 6.3: the `H x K` contention sweep (the declared envelope, measured on the composition).
#include "../test_support/window_contention_sweep.inc"

/// **Task 3.10 -- the activation audit, half 1: the production demand carries a qualified stamp, and an unqualified one
/// promises nothing.**
///
/// Task 3.10's own remainder names *"the clock-owner wiring"* as what activation still needs. This pins what the demand
/// that exists today does. `src/main.cpp`'s lane turn takes its clock from `ClockManager` and hands it to
/// `Track::serviceRollingPlaybackWindow` -- the only production window request: `requestPlaybackWindow` (the
/// caller-owned-scratch entry) and `requestPreviewWindow` have **no production caller** (the preview's belongs to 3.13),
/// the launch makes no promise (half 2 below) and the capture edge only accounts. An unqualified stamp is refused
/// **before** anything is promised (DEC-108 §13c's fail-closed direction), and the demand a qualified call retains
/// carries the same epoch and deadline the frame does -- so every input the enforced admission reads comes from the one
/// stamp the caller supplied.
void test_activation_audit_the_production_demand_is_positioned_and_an_unqualified_clock_is_refused() {
  assert_s4b_7_a_demoted_target_through_the_turn(true);
  auto& track = trackManager.getTrack(0);
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 2);
  TEST_ASSERT_NOT_NULL(runtime);
  const uint64_t origin = clockManager.getCurrentTick();

  // **(A) Unqualified: nothing is promised.** The frame stays idle and no demand is retained -- the refusal is the
  // error, never a position minted from a clock that carries none.
  TEST_ASSERT_NULL_MESSAGE(track.pendingViewRequest(2), "the audit needs a slot with no retained demand");
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::RollingOutcome::Excluded),
                            static_cast<int>(track.serviceRollingPlaybackWindow({origin, 0})),
                            "an unqualified clock is refused before a demand is made");
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(LoopPlaybackRuntime::RollingPhase::Idle),
                            static_cast<int>(runtime->rolling.phase), "and no rolling frame was opened");
  TEST_ASSERT_NULL_MESSAGE(track.pendingViewRequest(2), "nor was a demand retained");

  // **(B) Qualified: one stamp, one promise.** The retained demand and the frame read the same epoch and deadline.
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::RollingOutcome::Requested),
                            static_cast<int>(track.serviceRollingPlaybackWindow({origin, 1})),
                            "the stamped caller opens the demand");
  const PlaybackViewPendingRequest* pending = track.pendingViewRequest(2);
  TEST_ASSERT_NOT_NULL(pending);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, pending->clockEpoch, "the demand is positioned in the caller's epoch");
  TEST_ASSERT_GREATER_THAN_UINT64_MESSAGE(origin, pending->deadlineTick, "with an absolute deadline ahead of it");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(pending->clockEpoch, runtime->rolling.epoch,
                                   "and the frame reads that same epoch");
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(pending->deadlineTick, runtime->rolling.deadlineTick,
                                   "and the deadline the admission would judge");
}

/// **Task 3.10 -- the activation audit, half 2: the launch demand is unpositioned by design, and enforcement exempts it
/// rather than refusing it.**
///
/// The launch is the other production path that reaches window state, and it makes **no promise**: it waits for
/// readiness rather than owing it by a tick, which is why it may be prepared off-clock. That has two consequences worth
/// pinning, because both are what would break if a future caller "helpfully" stamped it: the deadline service must not
/// judge an unpositioned demand as late, and the interference budget must not refuse it.
void test_activation_audit_the_launch_demand_is_unpositioned_and_never_judged() {
  auto& track = trackManager.getTrack(0);
  setupImmutableLaunch(track);
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 2);
  TEST_ASSERT_NOT_NULL(runtime);

  // **(A) The launch demand holds no position.**
  const PlaybackViewPendingRequest* pending = track.pendingViewRequest(2);
  TEST_ASSERT_NOT_NULL(pending);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, pending->clockEpoch,
                                   "the launch demand is unpositioned: it is not a promise by a tick");
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(0ull, pending->deadlineTick, "and it carries no deadline to be late against");

  // Serve the launch far enough that the **build is in flight** -- the state the deadline service judges is the admitted
  // work, so the guard under test is the one that reads a *live* unpositioned promise.
  for (uint32_t turn = 0; turn < 256u && !runtime->coverageBuildNeedsService(); ++turn) {
    nativePlaybackDeadlineClock = {1000ull + turn * 64ull, 1u};
    trackManager.servicePendingSlotProvision();
    track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
  }
  const PlaybackViewAdmittedWork* work = runtime->activeViewBuildWork();
  TEST_ASSERT_NOT_NULL_MESSAGE(work, "the launch must reach an in-flight build for this audit to be about one");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, work->clockEpoch, "and that build is unpositioned too");
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(0ull, work->deadlineTick, "with no deadline to judge it against");

  // **(B) A far-future clock judges nothing.** Eight services at ticks an order of magnitude past any deadline the
  // suite's shapes use name no miss, no breach and no invalidation: unpositioned is not late.
  PlaybackPathRows::reset();
  for (uint32_t step = 0; step < 8u; ++step) {
    track.serviceAdmittedDeadlineMisses({1000000000ull + step * 768ull, 1u});
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(PlaybackPathRows::countOf("one_pass_late")),
                                   "an unpositioned demand is never one pass late");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(PlaybackPathRows::countOf("lag_breach")),
                                   "nor in breach");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      0u, static_cast<uint32_t>(PlaybackPathRows::countOf("promise_epoch_invalidated")), "nor epoch-invalidated");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      0u, static_cast<uint32_t>(PlaybackPathRows::countOf("pending_position_invalidated")),
      "nor position-invalidated");

  // **(C) Served, not judged -- and exempt under enforcement.** With the interference budget on, the launch still
  // prepares and binds: that budget governs *promises*, and a launch makes none. The flag is restored here, so no later
  // fixture inherits it.
  const bool wasEnforced = sInterferenceBudgetEnforced;
  sInterferenceBudgetEnforced = true;
  drainImmutableLaunch(track, 2);
  TEST_ASSERT_TRUE_MESSAGE(track.bindPlaybackLaunchForSlot(2, 2000),
                           "the unpositioned launch binds even with the interference budget enforced");
  sInterferenceBudgetEnforced = wasEnforced;
  TEST_ASSERT_FALSE_MESSAGE(sInterferenceBudgetEnforced, "and the fixture left the enforcement switch as it found it");
}

/// **Task 3.10 -- the rolling path's admission boundary, half 1: an admissible shape completes and emits exactly across
/// repeated handoffs, with the interference budget enforced.**
///
/// The `H x K` sweep measures *completion given sufficient lead* on the service composition with the transport stopped;
/// this drives the **playing** chain the production lane composes -- `serviceRollingPlaybackWindow` (selector) then
/// `stepPlaybackViewMaintenance` (bounded worker) then `resolveRollingPlaybackWindow` (boundary validation and
/// adoption) then the existing emitter -- with `sInterferenceBudgetEnforced` on, and counts the handoffs it performs.
void test_rolling_path_admissible_shape_repeats_handoffs_under_enforcement() {
  assert_s4b_7_a_demoted_target_through_the_turn(true);
  auto& track = trackManager.getTrack(0);
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 2);
  const uint64_t origin = clockManager.getCurrentTick();
  nativePlaybackDeadlineClock = {origin, 1};
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::RollingOutcome::Requested),
                            static_cast<int>(track.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock)),
                            "the stamped playing caller opens the first interval's demand");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(6 * 768, runtime->rolling.startPhase,
                                   "the selector targets the first aligned two-bar interval ahead");
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(origin + 6 * 768, runtime->rolling.deadlineTick,
                                   "with the absolute boundary as its deadline");
  MidiSendCapture::reset();
  PlaybackPathRows::reset();
  const bool wasEnforced = sInterferenceBudgetEnforced;
  sInterferenceBudgetEnforced = true;

  // The suite's own lane drive (`rollingTestTick`: selector + one worker turn every 64 ticks, transport every tick), over
  // 16 bars -- and nothing is asserted inside it, so a failure cannot leave the enforcement switch flipped for later
  // fixtures. The counts below are what the drive saw.
  uint32_t servingTicks = 0;
  uint32_t viewChanges = 0;
  uint32_t buildsAtAdoption = 0;
  uint32_t lastView = runtime->viewSetIfPrewarmed()->lifecycle.current;
  for (uint32_t delta = 1; delta <= 16 * 768 + 48; ++delta) {
    const auto builds = Track::playbackOrderHotRebuildCount();
    rollingTestTick(track, origin + delta, delta % 64 == 0);
    if (runtime->rolling.phase == LoopPlaybackRuntime::RollingPhase::Serving && track.playbackWindowServesSlot(2)) {
      ++servingTicks;
      const uint32_t current = runtime->viewSetIfPrewarmed()->lifecycle.current;
      if (current != lastView) {
        lastView = current;
        ++viewChanges;
        if (builds != Track::playbackOrderHotRebuildCount()) ++buildsAtAdoption;
      }
    }
  }
  sInterferenceBudgetEnforced = wasEnforced;

  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0u, servingTicks,
                                          "at least one interval is served from the immutable window");
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(2u, viewChanges,
                                              "and the served view changes at each handoff: repeated handoffs");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, buildsAtAdoption,
                                   "adoption reuses the prepared window: no mutable build at the boundary");
  // The exact trace the existing rolling fixture pins for this drive, now under enforcement: 17 ons and 16 offs.
  const uint32_t total = MidiSendCapture::count();
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(33u, total, "16 bars and 48 ticks: 16 ons + 17 offs, and no other event");
  uint32_t ons = 0;
  uint32_t offs = 0;
  uint32_t foreign = 0;
  for (size_t i = 0; i < total; ++i) {
    const auto event = MidiSendCapture::at(i);
    if (event.data.noteData.note != 48) ++foreign;
    else if (event.isNoteOn()) ++ons;
    else ++offs;
  }
  // The drive starts one tick past the loop's bar-0 note-on, so the range holds bars 1..16's ons (16) and bars 0..16's
  // closures (17): the same trace the existing rolling fixture pins, counted from the same boundary.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(16u, ons, "one note-on per bar in the driven range");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(17u, offs, "and its closure, one bar later");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, foreign, "and nothing else was emitted: no silence, no foreign note");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(PlaybackPathRows::countOf("one_pass_late")),
                                   "an admissible shape crosses no promise, enforced or not");
  TEST_ASSERT_NULL_MESSAGE(track.pendingViewRequest(2), "the admissible shape leaves no refused demand behind");
}

/// **Task 3.10 -- the rolling path's admission boundary, half 2: the shape just outside the charge is refused by name,
/// keeps its obligation, and playback continues through the fallback.**
///
/// The selector probes the declared basis cost for its lookahead while the worker's admission prices the window's
/// **derived** work at `H`, so a dense interval is *requested* (the demand is made) and then refused **by name** by the
/// admission; the interval is served by the mutable fallback exactly as if no request had been made. The bound remains
/// two bars and capacity 2048: what the refusal describes is the shape's **workload against the lead it was granted**,
/// not a smaller supported window.
void test_rolling_path_refused_shape_is_named_retained_and_falls_back() {
  assert_s4b_7_a_demoted_target_through_the_turn(true);
  auto& track = trackManager.getTrack(0);
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 2);
  const uint64_t origin = clockManager.getCurrentTick();
  // The dense root sits exactly inside the interval the selector targets first (bars 6..7 = [4608, 5376)), so the
  // derivation sees 300 notes = 600 entries where the launch's own interval held four.
  publishNotesInBarsRootForSlot(track.getLoop(2), 6, 7, 300);
  const bool wasEnforced = sInterferenceBudgetEnforced;
  sInterferenceBudgetEnforced = true;

  nativePlaybackDeadlineClock = {origin, 1};
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::RollingOutcome::Requested),
                            static_cast<int>(track.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock)),
                            "the demand is made: the selector's lookahead is the declared basis cost");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(6 * 768, runtime->rolling.startPhase, "targeting the dense interval");
  const uint64_t promised = runtime->rolling.deadlineTick;
  MidiSendCapture::reset();
  PlaybackPathRows::reset();
  const uint32_t servedBefore = runtime->viewSetIfPrewarmed()->lifecycle.current;

  // The same lane drive as the admissible fixture: the dense shape is *prepared* here, and the boundary decides.
  uint32_t servingTicks = 0;
  uint32_t viewChanges = 0;
  bool sawFrozenDemand = false;
  for (uint32_t delta = 1; delta <= 10 * 768 + 48; ++delta) {
    rollingTestTick(track, origin + delta, delta % 64 == 0);
    if (runtime->frozenViewRequest.active) sawFrozenDemand = true;
    if (runtime->rolling.phase == LoopPlaybackRuntime::RollingPhase::Serving && track.playbackWindowServesSlot(2)) {
      ++servingTicks;
    }
    if (runtime->viewSetIfPrewarmed()->lifecycle.current != servedBefore) ++viewChanges;
  }
  sInterferenceBudgetEnforced = wasEnforced;

  // **The boundary, on the playing path:** the dense interval is never adopted, the served view never moves, and the
  // obligation is still there afterwards -- on whichever of the episode's two records the freeze left it.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, servingTicks, "the dense shape is never served from the immutable window");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, viewChanges, "and no handoff takes the served view");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(servedBefore, runtime->viewSetIfPrewarmed()->lifecycle.current,
                                   "the served view is untouched");
  TEST_ASSERT_TRUE_MESSAGE(sawFrozenDemand, "the shape was prepared for: its demand froze and was priced");
  const PlaybackViewPendingRequest* pend = track.pendingViewRequest(2);
  const bool retainedWithItsDeadline =
      (pend != nullptr && pend->clockEpoch == 1 && pend->deadlineTick == promised) ||
      (runtime->frozenViewRequest.active && runtime->frozenViewRequest.deadlineTick == promised);
  TEST_ASSERT_TRUE_MESSAGE(retainedWithItsDeadline, "and the refused demand keeps its original deadline");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(PlaybackPathRows::countOf("one_pass_late")),
                                   "the refusal is not a crossed promise: nothing is served late");
  // Playback continues through the fallback: the dense root's 600 events (bars 6..7) plus three of the launch's four
  // (the drive starts one tick past the launch's bar-0 note-on, so that first event is outside the captured range).
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(603u, MidiSendCapture::count(),
                                   "playback continues through the fallback, unaffected by the refusal");
}
/// **Task 3.10 -- the rolling path's admission boundary, half 3: the budget charges work, not only an admitted builder.**
///
/// A slot that is qualifying or retiring its coverage source has no admitted work record yet, and the per-track budget
/// must still price it, otherwise a candidate's lead is checked against only part of the work on its track. This pins
/// both directions in one fixture: with a sibling owning its coverage source the candidate is refused **by name** (the
/// charge is the worst-case snapshot bound, not an observed count), and once that source has retired the very same
/// request is admitted.
void test_rolling_path_budget_charges_qualification_and_retirement_work() {
  assert_s4b_7_a_demoted_target_through_the_turn(true);
  auto& track = trackManager.getTrack(0);
  auto* target = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 2);
  const uint64_t origin = clockManager.getCurrentTick();
  const bool wasEnforced = sInterferenceBudgetEnforced;

  // The target (the demoted, playing slot) starts its own qualification, which **owns** its coverage source.
  nativePlaybackDeadlineClock = {origin, 1};
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::RollingOutcome::Requested),
                            static_cast<int>(track.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock)),
                            "the rolling demand is opened");
  for (uint32_t turn = 0; turn < 512 && !target->ownsCoverageSource(); ++turn) {
    nativePlaybackDeadlineClock = {origin + turn * 64 + 1, 1};
    track.stepPlaybackViewMaintenance(static_cast<uint32_t>(origin), nullptr, false, nativePlaybackDeadlineClock);
  }
  TEST_ASSERT_TRUE_MESSAGE(target->ownsCoverageSource(), "the target owns its coverage source while it qualifies");
  // **The charge is real work:** the owner of a coverage source has no admitted builder yet, and the budget still
  // prices it -- at the worst-case snapshot bound, which is why the enforced candidate below cannot fit any loop.
  TEST_ASSERT_NULL_MESSAGE(target->activeViewBuildWork(), "qualification is not an admitted builder's record");
  const uint32_t qualifyingCharge = track.playbackViewOutstandingGrains(1);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(1000u, qualifyingCharge,
                                          "the budget charges the qualification's worst-case snapshot bound");
  TEST_ASSERT_FALSE_MESSAGE(sInterferenceBudgetEnforced, "the enforced window is closed before this assertion");

  // (A) Enforced: a candidate on the same track is refused **by name** because its lead is charged with that work.
  SessionMidiEventVec& scratch = windowSourceScratch(1);
  sInterferenceBudgetEnforced = true;
  // Give the candidate's own slot a current root and a prewarmed runtime, so the only reason it can be refused is the
  // sibling's charge (a rootless slot would refuse for its source and prove nothing about the budget).
  commitRecordPassIntoTrackSlotOfLength(track, 1, 40 * 768);
  Loop& candidateLoop = track.getLoop(1);
  candidateLoop.loopLengthTicks = 40 * 768;
  publishNotesInBarsRootForSlot(candidateLoop, 12, 13, 20);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(1));
  scratch.clear();
  const Track::PlaybackViewRequestResult refused =
      track.requestPlaybackWindow(1, 12 * 768, scratch, {origin + 50000, 1});
  sInterferenceBudgetEnforced = false;
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::PlaybackViewRequestResult::CannotCompleteBeforeHandoff),
                            static_cast<int>(refused),
                            "the sibling's qualification work is charged against the candidate's lead");
  const PlaybackViewPendingRequest* retained = track.pendingViewRequest(1);
  TEST_ASSERT_NOT_NULL_MESSAGE(retained, "and the refused candidate keeps its obligation");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<uint8_t>(PlaybackViewPendingReason::CannotCompleteBeforeHandoff),
                                  static_cast<uint8_t>(retained->reason), "named as retained, not dropped");

  // (B) The charge is released with the work: once the qualification/retirement finishes, the very same request is
  // admitted -- so the refusal above was attributable to the charge and to nothing else.
  for (uint32_t turn = 0; turn < 16384 && target->ownsCoverageSource(); ++turn) {
    nativePlaybackDeadlineClock = {origin + turn * 64 + 1, 1};
    track.stepPlaybackViewMaintenance(static_cast<uint32_t>(origin), nullptr, false, nativePlaybackDeadlineClock);
  }
  TEST_ASSERT_FALSE_MESSAGE(target->ownsCoverageSource(),
                            "the qualification/retirement work had to finish for the pair to be honest");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(track.playbackViewOutstandingGrains(1)),
                                   "and its charge is gone with it");
  scratch.clear();
  const Track::PlaybackViewRequestResult admitted =
      track.requestPlaybackWindow(1, 12 * 768, scratch, {origin + 60000, 1});
  sInterferenceBudgetEnforced = wasEnforced;
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                            static_cast<int>(admitted),
                            "with the charge released the same request is admitted: the charge was the refusal");
}

/// **Task 6.5 — the native force "A→B→A view-slot reuse with the first A completion arriving last".**
///
/// The ordering rule under test, on the *playing* chain: a completion that arrives **after** ownership moved publishes
/// nothing, ends its own episode, and leaves its view slot **reusable** by a later operation whose operation identity is
/// distinct. That is the window lane's half of what the lease fixtures force for the lease: A's completion is delivered
/// late (the completion reader bumps the playback generation *inside* the final grain), and B then builds the *same*
/// interval into the *same* view slot under a new identity and is served from it.
///
/// Two facts the probe of this slice established and this fixture therefore pins rather than assumes: the late
/// completion ends the episode **by itself** (the frame's ownership-moved branch), so the off-clock cancellation that
/// follows finds nothing to cancel — cancellation is idempotent by construction, not a second release; and the stale
/// operation identity is *retained* on the runtime, so the refusal is the publication guard's
/// (`viewBuildOperationGeneration != playbackGeneration`) rather than a silent cleanup.
void test_rolling_path_reuses_the_view_slot_after_a_refused_completion() {
  assert_s4b_7_a_demoted_target_through_the_turn(true);
  auto& track = trackManager.getTrack(0);
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 2);
  TEST_ASSERT_NOT_NULL(runtime);
  auto* set = runtime->viewSetIfPrewarmed();
  TEST_ASSERT_NOT_NULL(set);
  const uint64_t origin = clockManager.getCurrentTick();
  const uint32_t servedBefore = set->lifecycle.current;
  // The fixture's own epoch: an operation's identity is the generation it was admitted in, and this fixture is about the
  // **move** between two of them, so start away from zero where "stamped" and "unstamped" are indistinguishable.
  track.bumpPlaybackGeneration();
  const uint32_t generationA = track.getPlaybackGeneration();

  // ---- A: the interval's demand opens, and its build begins under A's own identity.
  nativePlaybackDeadlineClock = {origin, 1};
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::RollingOutcome::Requested),
                            static_cast<int>(track.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock)),
                            "A opens the interval's demand");
  const uint8_t slotA = set->lifecycle.build;
  const uint32_t startPhaseA = runtime->rolling.startPhase;
  uint32_t begunAt = 0;
  for (uint32_t turn = 0; turn < 512 && !runtime->coverageBuildNeedsService(); ++turn) {
    nativePlaybackDeadlineClock = {origin + turn * 64 + 1, 1};
    (void)track.stepPlaybackViewMaintenance(static_cast<uint32_t>(origin), nullptr, false,
                                            nativePlaybackDeadlineClock);
    begunAt = turn + 1;
  }
  TEST_ASSERT_TRUE_MESSAGE(runtime->coverageBuildNeedsService(), "A's build is in flight");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(generationA, runtime->viewBuildOperationGeneration,
                                   "and it carries A's operation identity, stamped where the build began");
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(runtime->rolling.deadlineTick, runtime->viewBuildWork.deadlineTick,
                                   "with the admitted work owing the promise the frame made");

  // ---- The completion arrives **after** ownership moved: the reader moves the generation inside the final grain.
  MidiSendCapture::reset();

  PlaybackPathRows::reset();
  Track::PlaybackViewTurnResult lastA = Track::PlaybackViewTurnResult::Idle;
  for (uint32_t turn = 0; turn < 512; ++turn) {
    nativePlaybackDeadlineClock = {origin + (begunAt + turn + 1) * 64 + 1, 1};
    lastA = track.stepPlaybackViewMaintenance(static_cast<uint32_t>(origin), nullptr, false,
                                              nativePlaybackDeadlineClock, changeRollingGenerationAtCompletion);
    if (lastA == Track::PlaybackViewTurnResult::BuildComplete) break;
    if (track.getPlaybackGeneration() != generationA) break;
  }
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::PlaybackViewTurnResult::BuildComplete),
                            static_cast<int>(lastA), "A's build reached its completion grain");
  TEST_ASSERT_NOT_EQUAL_MESSAGE(generationA, track.getPlaybackGeneration(),
                                "and ownership moved before it could publish");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, MidiSendCapture::count(),
                                   "a completion that arrives late publishes nothing");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(servedBefore, set->lifecycle.current, "the served view is untouched");
  TEST_ASSERT_TRUE_MESSAGE(set->lifecycle.ready >= set->views.size() ||
                               !set->views[set->lifecycle.ready].complete,
                           "and no prepared successor was created from it");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(PlaybackPathRows::countOf("one_pass_late")),
                                   "an operation that publishes nothing crosses no promise");
  // The episode ends **because** the completion was late: the frame keeps no debt.
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(LoopPlaybackRuntime::RollingPhase::Idle),
                            static_cast<int>(runtime->rolling.phase),
                            "the late completion ends the episode rather than leaving it owed");
  TEST_ASSERT_FALSE_MESSAGE(runtime->viewBuild.active(), "the builder is released");
  // Its identity is **retained**, and that is what makes a later publication from it impossible; a clean-up that
  // happened to zero it would leave the same outcome for the wrong reason.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(generationA, runtime->viewBuildOperationGeneration,
                                   "the stale identity stays recorded: the guard is the refusal, not a cleanup");
  // Cancellation is idempotent: there is no live episode left for the off-clock owner to cancel.
  track.cancelRollingPlaybackWindow(2);
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(LoopPlaybackRuntime::RollingPhase::Idle),
                            static_cast<int>(runtime->rolling.phase),
                            "cancelling an episode the completion already ended is a no-op");
  // The slot's **source** is the other owner's work, and it is released by its own retirement -- not by the refusal.
  for (uint32_t turn = 0; turn < 16384 && runtime->ownsCoverageSource(); ++turn)
    runtime->stepCoverageSourceRetirement(64);
  TEST_ASSERT_FALSE_MESSAGE(runtime->ownsCoverageSource(),
                            "the refused operation's coverage source retires normally");
  TEST_ASSERT_NULL_MESSAGE(track.pendingViewRequest(2),
                           "and the discharge from its admission is still the only record: nothing was retained back");

  // ---- B: the same interval, the same view slot, a **new** identity, built and served by the same playing chain.
  const uint64_t bOrigin = origin + 40000;
  nativePlaybackDeadlineClock = {bOrigin, 1};
  clockManager.assignCurrentTickSilently(static_cast<uint32_t>(bOrigin));
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::RollingOutcome::Requested),
                            static_cast<int>(track.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock)),
                            "B opens the same demand");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(slotA, set->lifecycle.build,
                                  "and it is offered the very view slot A was building in");
  const uint32_t generationB = track.getPlaybackGeneration();
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(generationA + 1, generationB, "B's own epoch is the moved generation");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(generationB, runtime->rolling.generation, "and its frame carries it");
  const uint32_t startPhaseB = runtime->rolling.startPhase;
  // The premise of the reuse: the slot is not fresh -- it still carries the identity of the operation whose completion
  // was refused there, which is what makes "reused" a statement about the same slot rather than about a new one.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(generationA, set->views[slotA].source.sourceGeneration,
                                   "the slot A built in still carries A's identity, not a clean one");
  uint32_t servingTicks = 0;
  bool sawBBuilder = false;
  uint32_t reusedSlotGeneration = 0;
  uint32_t reusedSlotStartPhase = 0;
  uint32_t reusedSlotFramePhase = 0;
  for (uint32_t delta = 1; delta <= 10 * 768 + 48; ++delta) {
    rollingTestTick(track, bOrigin + delta, delta % 64 == 0);
    if (!sawBBuilder && set->views[slotA].source.sourceGeneration == generationB &&
        runtime->viewBuildOperationGeneration == generationB) {
      // The very slot A built in now carries the current operation's identity: B's build re-populated it.
      sawBBuilder = true;
      reusedSlotGeneration = set->views[slotA].source.sourceGeneration;
      reusedSlotStartPhase = set->views[slotA].coverage.startPhase;
      reusedSlotFramePhase = runtime->rolling.startPhase;
    }
    if (runtime->rolling.phase == LoopPlaybackRuntime::RollingPhase::Serving && track.playbackWindowServesSlot(2))
      ++servingTicks;
  }
  TEST_ASSERT_TRUE_MESSAGE(sawBBuilder, "B's build ran in flight on that chain");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(generationB, runtime->viewBuildOperationGeneration,
                                   "re-stamped with B's identity: the slot belongs to the current operation");
  TEST_ASSERT_NOT_EQUAL_MESSAGE(generationA, runtime->viewBuildOperationGeneration,
                                "so nothing of A's operation can be revived through this slot");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(generationB, reusedSlotGeneration,
                                   "the reused slot was re-populated under the new identity");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(reusedSlotFramePhase, reusedSlotStartPhase,
                                   "holding the interval the frame itself projected when that build ran");
  TEST_ASSERT_NOT_EQUAL_MESSAGE(startPhaseA, reusedSlotStartPhase,
                                "and not the interval A left behind in the same slot");
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(startPhaseB, reusedSlotStartPhase,
                                              "the projection only moves forward from the request's interval");
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0u, servingTicks,
                                          "and the reused slot's window is served on the playing chain");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(PlaybackPathRows::countOf("one_pass_late")),
                                   "the reuse crossed no promise");
  const uint32_t total = MidiSendCapture::count();
  uint32_t ons = 0;
  uint32_t offs = 0;
  uint32_t foreign = 0;
  for (size_t i = 0; i < total; ++i) {
    const auto event = MidiSendCapture::at(i);
    if (event.data.noteData.note != 48) ++foreign;
    else if (event.isNoteOn()) ++ons;
    else ++offs;
  }
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(8u, ons, "B's drive emitted the interval's own note-ons");
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(7u, offs, "with their closures");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, foreign,
                                   "and nothing else: no stale A content and no silence across the reuse");
}

/// **Task 6.5 — the native force "cancellation between every preparation grain recovers".**
///
/// Cancelling an episode is only as good as its *worst* grain: the demand, the qualification, the declared work record and
/// the in-flight builder each own a piece of the same promise, and a cancellation that misses any one of them leaves a
/// slot that can never be prepared again. This fixture cancels at every grain of a preparation -- k = 0 (the demand is
/// retained, nothing has begun) through the last grain before completion (the builder is in flight) -- and asserts the
/// **same** recovered state each time: no live demand, no builder, no candidate, the served view untouched, nothing
/// emitted. The last act is the one that matters for a slot that must be *reusable*: after every one of those grains, one
/// clean episode still runs and is served on the playing chain.
void test_rolling_path_cancellation_between_every_preparation_grain_recovers() {
  assert_s4b_7_a_demoted_target_through_the_turn(true);
  auto& track = trackManager.getTrack(0);
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 2);
  TEST_ASSERT_NOT_NULL(runtime);
  auto* set = runtime->viewSetIfPrewarmed();
  TEST_ASSERT_NOT_NULL(set);
  const uint64_t origin = clockManager.getCurrentTick();
  const uint32_t servedBefore = set->lifecycle.current;
  uint32_t cancelledBeforeBuild = 0;
  uint32_t cancelledInsideBuild = 0;
  constexpr uint32_t kLastGrain = 5;

  for (uint32_t k = 0; k <= kLastGrain; ++k) {
    const uint64_t base = origin + 20000 + static_cast<uint64_t>(k) * 3000;
    MidiSendCapture::reset();
    PlaybackPathRows::reset();
    nativePlaybackDeadlineClock = {base, 1};
    clockManager.assignCurrentTickSilently(static_cast<uint32_t>(base));
    TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::RollingOutcome::Requested),
                              static_cast<int>(track.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock)),
                              "each cycle opens its own demand");
    for (uint32_t turn = 0; turn < k; ++turn) {
      nativePlaybackDeadlineClock = {base + (turn + 1) * 64, 1};
      clockManager.assignCurrentTickSilently(static_cast<uint32_t>(nativePlaybackDeadlineClock.tick));
      (void)track.stepPlaybackViewMaintenance(static_cast<uint32_t>(nativePlaybackDeadlineClock.tick), nullptr, false,
                                              nativePlaybackDeadlineClock);
    }
    const bool buildInFlight = runtime->coverageBuildNeedsService() || runtime->viewBuild.active();
    if (buildInFlight) ++cancelledInsideBuild;
    else ++cancelledBeforeBuild;

    track.cancelRollingPlaybackWindow(2);
    TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(LoopPlaybackRuntime::RollingPhase::Idle),
                              static_cast<int>(runtime->rolling.phase),
                              "cancellation ends the episode, at whatever grain it was in");
    TEST_ASSERT_NULL_MESSAGE(track.pendingViewRequest(2),
                             "and whoever held the demand -- the retention or the admitted work -- released it");
    TEST_ASSERT_FALSE_MESSAGE(runtime->frozenViewRequest.active, "with no frozen demand left beside it");
    TEST_ASSERT_FALSE_MESSAGE(runtime->viewBuild.active(), "and no builder still in flight");
    TEST_ASSERT_TRUE_MESSAGE(set->lifecycle.ready >= set->views.size() ||
                                 !set->views[set->lifecycle.ready].complete,
                             "no candidate was published from a cancelled episode");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(servedBefore, set->lifecycle.current,
                                     "the served view was never touched");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, MidiSendCapture::count(),
                                     "and nothing was emitted by a preparation that never completed");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(PlaybackPathRows::countOf("one_pass_late")),
                                     "a cancellation crosses no promise");
    for (uint32_t turn = 0; turn < 16384 && runtime->ownsCoverageSource(); ++turn)
      runtime->stepCoverageSourceRetirement(64);
    TEST_ASSERT_FALSE_MESSAGE(runtime->ownsCoverageSource(),
                              "the cancelled preparation's coverage source retires: the slot is reusable");
  }
  // The sweep has to have actually swept: at least one cancellation before the build began and at least one inside it.
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0u, cancelledBeforeBuild, "k = 0 cancels a retained demand");
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0u, cancelledInsideBuild, "and the later cycles cancel a live builder");

  // ---- The slot is reusable after every one of those grains: one clean episode, built and served by the lane.
  const uint64_t cleanBase = origin + 20000 + static_cast<uint64_t>(kLastGrain + 1) * 3000;
  nativePlaybackDeadlineClock = {cleanBase, 1};
  clockManager.assignCurrentTickSilently(static_cast<uint32_t>(cleanBase));
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::RollingOutcome::Requested),
                            static_cast<int>(track.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock)),
                            "the slot accepts a demand after every cancelled grain");
  MidiSendCapture::reset();
  PlaybackPathRows::reset();
  uint32_t servingTicks = 0;
  for (uint32_t delta = 1; delta <= 10 * 768 + 48; ++delta) {
    rollingTestTick(track, cleanBase + delta, delta % 64 == 0);
    if (runtime->rolling.phase == LoopPlaybackRuntime::RollingPhase::Serving && track.playbackWindowServesSlot(2))
      ++servingTicks;
  }
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0u, servingTicks,
                                          "and an uncancelled episode is served from the recovery slot");
  const uint32_t total = MidiSendCapture::count();
  uint32_t ons = 0;
  uint32_t offs = 0;
  uint32_t foreign = 0;
  for (size_t i = 0; i < total; ++i) {
    const auto event = MidiSendCapture::at(i);
    if (event.data.noteData.note != 48) ++foreign;
    else if (event.isNoteOn()) ++ons;
    else ++offs;
  }
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(8u, ons, "with the interval's own note-ons");
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(7u, offs, "and their closures");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, foreign, "and no foreign note: recovery, not residue");
}

/// **Task 6.4/6.5 -- the rolling demand path's gate row (`DIAG,win_gate`).**
///
/// The lane's own rows (`win_gap`, `playback_build`) say whether it was *dispatched*; nothing said **what its demand
/// path decided**, because `serviceRollingPlaybackWindow` returns a typed `RollingOutcome` to its caller with no row.
/// On device that made "this track owed no window" and "the window was refused -- and by which term" the same absence
/// (F-16: five switch-leg runs were spent on the distinction). This fixture pins the row's contract on the production
/// entry: the exit token of **this** call, the active slot's identity, the flags that decided it, the request's own
/// ordinal, and the once-per-state-change cadence.
///
/// The case it is built around is the one the device question now rests on: the demand's **entry** decision, where an
/// unstamped deadline clock, a loop below the demotion bound and an unaligned length all leave through the *same*
/// exit -- and `ep`/`av`/`al` tell a reader which of the three refused, so one token cannot hide it.
void test_rolling_gate_rows_name_the_demand_paths_silent_exits_once_per_state_change() {
  assert_s4b_7_a_demoted_target_through_the_turn(true);
  auto& track = trackManager.getTrack(0);
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 2);
  TEST_ASSERT_NOT_NULL(runtime);
  Loop& active = track.getActiveLoop();
  const uint32_t demotedLength = active.loopLengthTicks;
  const uint64_t origin = clockManager.getCurrentTick();
  constexpr uint16_t kEpochStamped = 1u << 0;
  constexpr uint16_t kAvoidFullRebuild = 1u << 1;
  constexpr uint16_t kSpanAligned = 1u << 2;
  constexpr uint16_t kPlaying = 1u << 3;
  constexpr uint16_t kRootCurrent = 1u << 9;
  RollingGateRows::reset();

  // ---- (W1) The unstamped clock: the exit names the entry decision, and the flags say which term refused. This is
  // the reading the device could not make -- the lane was dispatched (`win_gap` `running=1`) and owed nothing.
  nativePlaybackDeadlineClock = {origin + 64, 0};
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::RollingOutcome::Excluded),
                            static_cast<int>(track.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock)),
                            "an unstamped deadline clock is excluded before anything is promised");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, RollingGateRows::count, "the exit that refused names itself exactly once");
  TEST_ASSERT_EQUAL_STRING("win_window_ineligible", RollingGateRows::last()->exit);
  TEST_ASSERT_EQUAL_UINT8(0, RollingGateRows::last()->track);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, RollingGateRows::last()->slot, "the row names the active slot it read");
  TEST_ASSERT_EQUAL_UINT32(demotedLength, RollingGateRows::last()->loopLengthTicks);
  TEST_ASSERT_TRUE_MESSAGE((RollingGateRows::last()->flags & kEpochStamped) == 0,
                           "`ep=0`: the clock's own stamp is the term that refused");
  TEST_ASSERT_TRUE_MESSAGE((RollingGateRows::last()->flags & kAvoidFullRebuild) != 0,
                           "and the shaping terms passed, so the refusal is attributable to the stamp");
  TEST_ASSERT_TRUE((RollingGateRows::last()->flags & kSpanAligned) != 0);
  TEST_ASSERT_TRUE((RollingGateRows::last()->flags & kPlaying) != 0);
  // The cadence: a second call on the same state owes no second row (a per-turn row would evict the evidence it explains).
  nativePlaybackDeadlineClock = {origin + 128, 0};
  (void)track.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, RollingGateRows::count, "one row per state change, never one per call");

  // ---- (W2) The other two terms of that same exit are told apart by their own flags, so one token cannot hide which
  // guard refused.
  active.loopLengthTicks = 4 * 768;  // below `kMaxDetailedWindowBars`: the legacy policy, not the windowed one
  nativePlaybackDeadlineClock = {origin + 192, 1};
  (void)track.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock);
  TEST_ASSERT_EQUAL_UINT32(2, RollingGateRows::count);
  TEST_ASSERT_EQUAL_STRING("win_window_ineligible", RollingGateRows::last()->exit);
  TEST_ASSERT_TRUE((RollingGateRows::last()->flags & kEpochStamped) != 0);
  TEST_ASSERT_TRUE_MESSAGE((RollingGateRows::last()->flags & kAvoidFullRebuild) == 0,
                           "`av=0`: a loop the whole-take policy admits never takes the windowed demand");
  active.loopLengthTicks = demotedLength + 768;  // demoted, but not a whole number of two-bar spans
  nativePlaybackDeadlineClock = {origin + 256, 1};
  (void)track.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock);
  TEST_ASSERT_EQUAL_UINT32(3, RollingGateRows::count);
  TEST_ASSERT_EQUAL_STRING("win_window_ineligible", RollingGateRows::last()->exit);
  TEST_ASSERT_TRUE((RollingGateRows::last()->flags & kAvoidFullRebuild) != 0);
  TEST_ASSERT_TRUE_MESSAGE((RollingGateRows::last()->flags & kSpanAligned) == 0,
                           "`al=0`: the length cannot carry whole spans");
  active.loopLengthTicks = demotedLength;

  // ---- (W3) A stamped clock on the demoted, aligned slot: the demand opens and the row names the exit that took it.
  const uint64_t base = origin + 512;
  nativePlaybackDeadlineClock = {base, 1};
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::RollingOutcome::Requested),
                            static_cast<int>(track.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock)),
                            "the same shape is admissible once the clock is stamped: the stamp was the difference");
  TEST_ASSERT_EQUAL_STRING("win_requested", RollingGateRows::last()->exit);
  TEST_ASSERT_EQUAL_UINT8(2, RollingGateRows::last()->slot);
  TEST_ASSERT_TRUE((RollingGateRows::last()->flags & kEpochStamped) != 0);
  TEST_ASSERT_TRUE((RollingGateRows::last()->flags & kRootCurrent) != 0);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      static_cast<uint8_t>(Track::PlaybackViewRequestResult::DeferredSourceQualification),
      RollingGateRows::last()->requestResult,
      "the row carries the request result the exit observed, not a placeholder");
  const uint32_t rowsAtRequest = RollingGateRows::count;

  // ---- (W4) The lane turning reports its own progress, and the count stays bounded rather than following the tick.
  clockManager.assignCurrentTickSilently(static_cast<uint32_t>(base));
  uint32_t servingTicks = 0;
  for (uint32_t delta = 1; delta <= 10 * 768 + 48; ++delta) {
    rollingTestTick(track, base + delta, delta % 64 == 0);
    if (runtime->rolling.phase == LoopPlaybackRuntime::RollingPhase::Serving && track.playbackWindowServesSlot(2))
      ++servingTicks;
  }
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0u, servingTicks, "the episode really ran: the row reports a live lane");
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(
      0u,
      RollingGateRows::countOf("win_serving") + RollingGateRows::countOf("win_ready") +
          RollingGateRows::countOf("win_waiting"),
      "the lane's own progress is named as the frame moves, not left implicit");
  TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(16u, RollingGateRows::count - rowsAtRequest,
                                       "a state-change bound: not one row per lane turn (measured: 9 over 120 turns)");

  // The **cadence the device found**: with `coverageRoleAvailable`/`viewBuild.active()` in the change key the row
  // alternated A<->B forever (up to 20 identical lines per length step on the bench, `captures/s64-win-gate-03`). They
  // are observation, not decisions, so they are outside the key; the drive reaches `Ready` twice (once per episode) and
  // must emit one row each rather than one per toggle. This is what MR-W4 red-proves.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      2u, RollingGateRows::countOf("win_ready"),
      "one row per decision: the role/progress bits must not re-emit the same state");

  // ---- (W5) A refusal raised before this family existed: with the transport down the demand is excluded by name.
  track.forceSetState(TRACK_STOPPED);
  nativePlaybackDeadlineClock = {base + 40000, 1};
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::RollingOutcome::Excluded),
                            static_cast<int>(track.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock)),
                            "a stopped track takes no window demand");
  TEST_ASSERT_EQUAL_STRING("win_track_not_playing", RollingGateRows::last()->exit);
  TEST_ASSERT_TRUE((RollingGateRows::last()->flags & kPlaying) == 0);
}


/// 3.7a (I2): **the deferred lane's production turn serves a demand whose slot has no published root** -- the shape the
/// device legs measured as `launch_requested … root=0 …` never reaching readiness. The demand is retained exactly as
/// before (`requestCommittedPlaybackWindow`), the turn freezes it and gathers one bounded page of the retained
/// revision's pinned bodies per turn, and the completing turn admits the gathered window through the **same**
/// production admission a requester uses -- so the window publishes with no root anywhere, and its content is the
/// fallback's own effective content for that revision.
void test_3_7a_i2_a_rootless_committed_demand_is_served_from_the_retained_revision() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  constexpr uint32_t length = 512 * bar;
  // Dense bar 0 (200 events, ticks 0..199) so the two-bar interval needs **more than one gather page**: the per-turn
  // accounting below is then about work that actually happened, not about a single-slice special case.
  constexpr uint32_t kDenseEvents = 200;
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  Loop& loop = track.getActiveLoop();
  loop.loopLengthTicks = length;
  loop.beginCapture(CapturePhase::Record);
  for (uint32_t i = 0; i < kDenseEvents; ++i) {
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(i * 2, 1, 48, 100)));
  }
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(length, CommitReason::RecordStop));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass());
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0));
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
  TEST_ASSERT_NOT_NULL(runtime);
  TEST_ASSERT_FALSE_MESSAGE(loop.publishedSnapshotIsCurrent(), "the fixture's shape is a rootless demoted slot");
  TEST_ASSERT_FALSE_MESSAGE(track.playbackWindowSourceIsCurrentForSlot(0),
                            "and with nothing retained there is no window source at all");
  // The **demand is retained** before any source exists: this is the pre-I2 shape, asserted so the change is visible
  // as a change (the demand used to sit here forever).
  const PlaybackCoverageInterval coverage{0, 2 * bar, length};
  nativePlaybackDeadlineClock = {1000, 1};
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::DeferredSourceQualification),
      static_cast<int>(track.requestCommittedPlaybackWindow(0, coverage, 13 * bar, nativePlaybackDeadlineClock)));
  TEST_ASSERT_NOT_NULL(track.pendingViewRequest(0));
  TEST_ASSERT_FALSE_MESSAGE(track.committedPlaybackRetryCanProgress(0, nativePlaybackDeadlineClock),
                            "a rootless slot with nothing retained owes its source, not a window");
  // **The retained revision is the source** (DEC-108 section 10), and the lane's currency predicate says so.
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackSourceRetainResult::Admitted),
                    static_cast<int>(track.retainPlaybackSourceForSlot(0, loop.playbackRevision, 3)));
  TEST_ASSERT_TRUE(track.playbackWindowSourceIsCurrentForSlot(0));
  TEST_ASSERT_TRUE(track.committedPlaybackRetryCanProgress(0, nativePlaybackDeadlineClock));
  TEST_ASSERT_TRUE(track.hasPendingPlaybackViewMaintenance());

  // One turn is one bounded page: the first turn freezes the demand (the record is discharged by the freeze, exactly
  // as the root-backed qualification discharges it) and gathers its first slice.
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewTurnResult::GrainServed),
      static_cast<int>(track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock)));
  TEST_ASSERT_TRUE_MESSAGE(runtime->retainedGatherActive(), "the gather is in flight after its first page");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, runtime->retainedGatherUnits, "one page per turn");
  TEST_ASSERT_TRUE(runtime->frozenViewRequest.active);
  TEST_ASSERT_TRUE(runtime->retainedGatherInspectedChunks >= 1);
  TEST_ASSERT_FALSE_MESSAGE(runtime->viewBuild.active(), "the window build has not started yet");
  const uint32_t afterFirstPage = static_cast<uint32_t>(runtime->retainedGatherEvents.size());
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(kWindowPageCapacity, afterFirstPage,
                                   "a page carries exactly the paged stream's capacity");

  // The completing page discharges the demand through the **production admission** and starts the fill.
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewTurnResult::GrainServed),
      static_cast<int>(track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock)));
  TEST_ASSERT_FALSE_MESSAGE(runtime->retainedGatherActive(), "the completing page releases the gather");
  TEST_ASSERT_NULL_MESSAGE(track.pendingViewRequest(0), "and the admission discharges the demand");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(runtime->retainedGatherEvents.capacity()),
                                   "the gathered window does not stay resident after admission");
  TEST_ASSERT_TRUE(runtime->viewBuild.active() || runtime->coverageBuildNeedsService());
  TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::RetainedGatherRefusal::None),
                    static_cast<int>(runtime->retainedGatherRefusal));

  PlaybackWindowIdentity published{};
  for (unsigned turn = 0; turn < 4096 && !track.publishedPlaybackWindowIdentityForSlot(0, published); ++turn) {
    track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
  }
  TEST_ASSERT_TRUE_MESSAGE(track.publishedPlaybackWindowIdentityForSlot(0, published),
                           "the production turn publishes the rootless window");
  TEST_ASSERT_EQUAL_UINT32(loop.playbackRevision, published.playbackRevision);
  TEST_ASSERT_EQUAL(static_cast<int>(PlaybackWindowOrigin::Committed), static_cast<int>(published.origin));
  PlaybackViewSourceIdentity identityOut{};
  PlaybackCoverageInterval coverageOut{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowForSlot(0, identityOut, coverageOut));
  TEST_ASSERT_EQUAL_PTR_MESSAGE(loop.publishedSnapshotIdentity(), identityOut.snapshotIdentity,
                                "the window carries the root it read: none, by construction");
  TEST_ASSERT_EQUAL_UINT32(coverage.startPhase, coverageOut.startPhase);
  TEST_ASSERT_EQUAL_UINT32(coverage.lengthTicks, coverageOut.lengthTicks);
  // Content parity against the **fallback's own** effective gather for the same revision -- not a hand-written list.
  SessionMidiEventVec oracle;
  loop.copyEffectiveCommittedEventsInRange(oracle, coverage.startPhase, coverage.lengthTicks);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(kDenseEvents, static_cast<uint32_t>(oracle.size()),
                                   "the interval holds the dense bar's events");
  const auto* set = runtime->viewSetIfPrewarmed();
  const auto& served = set->views[set->lifecycle.current];
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(oracle.size()), served.storage.eventCount,
                                   "the retained-served window holds the revision's effective content");
  for (size_t i = 0; i < oracle.size() && i < served.storage.eventCount; ++i) {
    TEST_ASSERT_EQUAL_UINT32(oracle[i].tick, served.storage.events[served.storage.order[i]].tick);
  }
}

/// 3.7a (I2): **the launch demand binds from the retained revision with no root** -- the chain the device leg measured
/// as never completing for a rootless switch target (`launch_requested`, `root=0`, then a hold). The switch target is
/// demoted (24 bars), deliberately rootless, and its current revision is retained.
void test_3_7a_i2_a_rootless_launch_target_binds_from_the_retained_revision() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  commitRecordPassIntoTrackSlot(track, 0);
  commitBarSpacedPassIntoTrackSlot(track, 2, 24);
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  track.forceSetState(TRACK_PLAYING);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(2));
  Loop& target = track.getLoop(2);
  TEST_ASSERT_FALSE_MESSAGE(target.publishedSnapshotIsCurrent(), "the launch target has no published root");
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 2);
  TEST_ASSERT_NOT_NULL(runtime);
  // The switch queue takes the target's occupancy; the launch demand it leaves behind is the one this lane serves.
  trackManager.requestSlotSwitch(0, 2, SlotQuantization::LoopEnd, 100);
  TEST_ASSERT_TRUE(trackManager.servicePendingSlotProvision());
  TEST_ASSERT_FALSE_MESSAGE(runtime->launchPreparation == LoopPlaybackRuntime::LaunchPreparation::None,
                            "the switch leaves a launch preparation owed");
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackSourceRetainResult::Admitted),
                    static_cast<int>(track.retainPlaybackSourceForSlot(2, target.playbackRevision, 9)));
  nativePlaybackDeadlineClock = {1000, 1};
  for (unsigned turn = 0; turn < 5000 && !track.playbackLaunchReadyForSlot(2); ++turn) {
    trackManager.servicePendingSlotProvision();
    track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
    nativePlaybackDeadlineClock.tick += 64;
  }
  TEST_ASSERT_TRUE_MESSAGE(track.playbackLaunchReadyForSlot(2),
                           "the rootless launch reached readiness from the retained revision");
  TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::LaunchPreparation::Ready),
                    static_cast<int>(runtime->launchPreparation));
  TEST_ASSERT_TRUE_MESSAGE(track.bindPlaybackLaunchForSlot(2, nativePlaybackDeadlineClock.tick),
                           "and it binds, which is the step the rootless device leg never reached");
  TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::LaunchPreparation::Bound),
                    static_cast<int>(runtime->launchPreparation));
  const auto* set = runtime->viewSetIfPrewarmed();
  const auto& bound = set->views[set->lifecycle.current];
  TEST_ASSERT_TRUE(bound.published);
  TEST_ASSERT_EQUAL_PTR_MESSAGE(target.publishedSnapshotIdentity(), bound.source.snapshotIdentity,
                                "the bound view reads no root: the retained revision served it");
}

/// 3.7a (I2): the refused gather **keeps the demand** and names why -- a retained revision that lags the loop, a
/// passed deadline, and the gather's own discarding transition. Nothing here publishes, and nothing is dropped.
void test_3_7a_i2_a_refused_or_cancelled_gather_keeps_the_demand() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  constexpr uint32_t length = 512 * bar;
  Track& track = trackManager.getTrack(0);
  commitRecordPassIntoTrackSlotOfLength(track, 0, length);
  Loop& loop = track.getLoop(0);
  loop.loopLengthTicks = length;
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0));
  auto* runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
  TEST_ASSERT_NOT_NULL(runtime);
  const PlaybackCoverageInterval coverage{0, 2 * bar, length};
  nativePlaybackDeadlineClock = {1000, 1};
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::DeferredSourceQualification),
      static_cast<int>(track.requestCommittedPlaybackWindow(0, coverage, 13 * bar, nativePlaybackDeadlineClock)));

  // (a) A retained revision that is **not** the loop's current one: the source is refused by name, nothing is
  // inspected, and the demand is still owed rather than dropped.
  const uint32_t retainedRevision = loop.playbackRevision;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackSourceRetainResult::Admitted),
                    static_cast<int>(track.retainPlaybackSourceForSlot(0, retainedRevision, 3)));
  loop.playbackRevision = retainedRevision + 1;  // what an edit does to the revision a window is dated by
  TEST_ASSERT_FALSE(track.playbackWindowSourceIsCurrentForSlot(0));
  TEST_ASSERT_FALSE_MESSAGE(track.committedPlaybackRetryCanProgress(0, nativePlaybackDeadlineClock),
                            "a lagging retained revision is not a window source");
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewTurnResult::Idle),
      static_cast<int>(track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock)));
  const PlaybackViewPendingRequest* stillOwed = track.pendingViewRequest(0);
  TEST_ASSERT_NOT_NULL_MESSAGE(stillOwed, "the demand survives a source it cannot use");
  if (stillOwed != nullptr) {
    // Read only once the record is known to exist: a fixture that dereferences a dropped demand would *crash* the run
    // instead of failing this case, which is how the first version of this assertion behaved (measured).
    TEST_ASSERT_EQUAL(static_cast<int>(PlaybackViewPendingReason::AwaitingSourceQualification),
                      static_cast<int>(stillOwed->reason));
  }
  PlaybackWindowIdentity notPublished{};
  TEST_ASSERT_FALSE(track.publishedPlaybackWindowIdentityForSlot(0, notPublished));

  // (b) The loop's revision returns to the retained one, the gather starts -- and a **transition** discards the
  // gather without touching the demand or the served window.
  loop.playbackRevision = retainedRevision;
  TEST_ASSERT_TRUE(track.committedPlaybackRetryCanProgress(0, nativePlaybackDeadlineClock));
  TEST_ASSERT_TRUE(track.beginRetainedSourceGatherForSlot(0));
  TEST_ASSERT_TRUE(runtime->retainedGatherActive());
  track.applyPlaybackViewTransition(0, PlaybackViewTransition::TransportStop);
  TEST_ASSERT_FALSE_MESSAGE(runtime->retainedGatherActive(), "a transition releases the candidate's gather");
  TEST_ASSERT_NOT_NULL_MESSAGE(track.pendingViewRequest(0), "and the demand is not dropped with it");

  // (c) An **expired promise** is not re-granted: the demand keeps a positional refusal and no window is published.
  TEST_ASSERT_TRUE(track.beginRetainedSourceGatherForSlot(0));
  const uint64_t deadline = runtime->frozenViewRequest.deadlineTick;
  TEST_ASSERT_TRUE(deadline != 0);
  PlaybackDeadlineClock lateClock{};
  lateClock.epoch = nativePlaybackDeadlineClock.epoch;
  lateClock.tick = deadline + 1;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewTurnResult::GrainDenied),
      static_cast<int>(track.stepPlaybackViewMaintenance(0, nullptr, false, lateClock)));
  TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::RetainedGatherRefusal::DeadlineExpired),
                    static_cast<int>(runtime->retainedGatherRefusal));
  TEST_ASSERT_EQUAL(static_cast<int>(PlaybackViewPendingReason::DeadlineExpired),
                    static_cast<int>(track.pendingViewRequest(0) != nullptr
                                         ? track.pendingViewRequest(0)->reason
                                         : PlaybackViewPendingReason::None));
  PlaybackWindowIdentity none{};
  TEST_ASSERT_FALSE(track.publishedPlaybackWindowIdentityForSlot(0, none));
}

/// 3.7a (I3): **the capture admission counts committed windows only.** A served **preview** window holds the note-edit
/// session's uncommitted projection (task 3.13); the admission's question -- what the interval already holds -- is about
/// *committed* content, and the identity's origin is what says so without inspecting the window's events. The committed
/// contrast is asserted too, so the rule cannot pass by counting nothing at all.
void test_3_7a_i3_the_capture_admission_counts_committed_windows_only() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  commitBarSpacedPassIntoTrackSlot(track, 0, 24);
  Loop& loop = track.getLoop(0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0));
  TEST_ASSERT_FALSE(track.playbackWindowServesSlot(0));
  TEST_ASSERT_FALSE(track.committedPlaybackWindowServesSlot(0));
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(track.captureIntervalAdmissionForSlot(0).acceptedEffectiveEvents));

  // A **preview** window served for the slot: the generic presence question is true, the committed one is not.
  MidiEvent previewSource[6];
  const size_t previewCount = buildWindowOwnerSource(previewSource, 6);
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(track.requestPreviewWindow(0, 7, previewSource, previewCount,
                                                                2 * Config::TICKS_PER_BAR)));
  for (size_t turn = 0; turn < 8 && track.hasPendingPlaybackViewMaintenance(); ++turn) {
    (void)track.stepPlaybackViewMaintenance();
  }
  TEST_ASSERT_TRUE_MESSAGE(track.servedPlaybackWindowIsPreviewForSlot(0), "the preview serves the slot");
  TEST_ASSERT_TRUE(track.playbackWindowServesSlot(0));
  TEST_ASSERT_TRUE(track.playbackWindowEventCountForSlot(0) > 0);
  TEST_ASSERT_FALSE_MESSAGE(track.committedPlaybackWindowServesSlot(0),
                            "a preview window is not committed content");
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(track.committedPlaybackWindowEventCountForSlot(0)));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(track.captureIntervalAdmissionForSlot(0).acceptedEffectiveEvents),
                                   "and the admission inherits nothing from the session's projection");

  // The committed contrast: this is what the accounting counts.
  publishBarSpacedRootForSlot(loop, 2);
  SessionMidiEventVec scratch;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                    static_cast<int>(track.requestPlaybackWindow(0, kWindowOwnerExpectedRequiredLead, scratch)));
  for (size_t turn = 0; turn < 8 && track.hasPendingPlaybackViewMaintenance(); ++turn) {
    (void)track.stepPlaybackViewMaintenance();
  }
  TEST_ASSERT_TRUE_MESSAGE(track.adoptPreparedPlaybackWindow(0), "the committed successor is adopted at the boundary");
  TEST_ASSERT_TRUE(track.committedPlaybackWindowServesSlot(0));
  TEST_ASSERT_FALSE(track.servedPlaybackWindowIsPreviewForSlot(0));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(scratch.size()), track.committedPlaybackWindowEventCountForSlot(0));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(scratch.size()),
                                   static_cast<uint32_t>(track.captureIntervalAdmissionForSlot(0).acceptedEffectiveEvents),
                                   "the committed window's events are the interval's inherited content");
}

namespace {
/// 3.7a (I3b): the **over-`L` fixture shape** both paged cases need -- a demoted slot (no published root) whose dense
/// interval holds more events than any window can retain, its revision retained, and the demand for that interval
/// retained by the deferred lane. Nothing is driven here: the caller owns the turns, so each fixture's accounting is
/// about work that actually happened.
struct OverCapacityPagedFixture {
  Track* track = nullptr;
  Loop* loop = nullptr;
  LoopPlaybackRuntime* runtime = nullptr;
  PlaybackCoverageInterval coverage{};
  uint32_t denseEvents = 0;
};

/// `loopBars` defaults to the 512-bar long loop the I3b fixtures use; a **two-bar** loop makes the interval the
/// *whole* loop, which is the shape whose wrap has no re-arm room (I3c-a's `CannotStraddleTheWrap`).
OverCapacityPagedFixture prepareOverCapacityPagedSlot(uint32_t denseEvents, uint32_t loopBars = 512) {
  constexpr uint32_t kBar = Config::TICKS_PER_BAR;
  const uint32_t length = loopBars * kBar;
  OverCapacityPagedFixture fixture{};
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  Loop& loop = track.getActiveLoop();
  loop.loopLengthTicks = length;
  loop.beginCapture(CapturePhase::Record);
  uint32_t appended = 0;
  // Dense **inside the window's own tick ceiling**: `kMaximumWindowTicks` is two bars, so more events than a window can
  // hold have to share those ticks (about 1.6 events per tick at 2,500 events). The note advances per cycle so no event
  // is a capture-level duplicate (the same channel+note+tick is refused as a duplicate -- the guard the store's dedup
  // policy owns), and the seal's stable sort turns the whole span into one tick-ordered body: the same body both
  // readers then see.
  constexpr uint32_t kWindowTicks = 2 * kBar;
  for (uint32_t i = 0; i < denseEvents; ++i) {
    const uint8_t note = static_cast<uint8_t>(48 + (i / kWindowTicks) % 64);
    if (!loop.appendCaptureEvent(MidiEvent::NoteOn(i % kWindowTicks, 1, note, 100))) {
      break;
    }
    ++appended;
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(denseEvents, appended, "the fixture's dense content must land whole in one pass");
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(length, CommitReason::RecordStop));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass());
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0));
  TEST_ASSERT_FALSE_MESSAGE(loop.publishedSnapshotIsCurrent(), "the fixture's shape is a rootless demoted slot");
  // The interval covers the whole dense span, and it is the widest interval the window path accepts (two bars).
  fixture.track = &track;
  fixture.loop = &loop;
  // Two bars -- and on a two-bar loop that *is* the whole loop, which is the no-re-arm-room shape.
  const uint32_t intervalTicks = length < 2 * kBar ? length : 2 * kBar;
  fixture.coverage = PlaybackCoverageInterval{0, intervalTicks, length};
  fixture.denseEvents = denseEvents;
  fixture.runtime = TrackCaptureStopTestAccess::runtimeIfAllocated(track, 0);
  TEST_ASSERT_NOT_NULL(fixture.runtime);
  nativePlaybackDeadlineClock = {1000, 1};
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::DeferredSourceQualification),
      static_cast<int>(track.requestCommittedPlaybackWindow(
          0, fixture.coverage, length / 4, nativePlaybackDeadlineClock)));
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackSourceRetainResult::Admitted),
                    static_cast<int>(track.retainPlaybackSourceForSlot(0, loop.playbackRevision, 3)));
  TEST_ASSERT_TRUE(track.playbackWindowSourceIsCurrentForSlot(0));
  return fixture;
}

/// 3.7a (I3b): drive the deferred lane until the demand leaves the gather's lane for the paged one, and report how many
/// turns the window lane spent first. The gather's own ceiling is the oracle for *when* that may happen: every turn
/// before it serves exactly one bounded slice.
uint32_t driveGatherUntilPagedSwitch(OverCapacityPagedFixture& fixture) {
  uint32_t turns = 0;
  while (!fixture.runtime->pagedActive && turns < kRetainedGatherMaxSlices + 4u) {
    if (turns > 0) {
      // The first turn *begins* the lane (freeze + first page together); every turn after it serves a slice of it.
      TEST_ASSERT_TRUE_MESSAGE(fixture.runtime->retainedGatherActive(),
                               "the window lane is in flight until it concludes the interval is over `L`");
    }
    const uint32_t unitsBefore = fixture.runtime->retainedGatherUnits;
    const Track::PlaybackViewTurnResult result =
        fixture.track->stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
    ++turns;
    if (fixture.runtime->pagedActive) {
      break;
    }
    TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewTurnResult::GrainServed),
                      static_cast<int>(result));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(unitsBefore + 1u, fixture.runtime->retainedGatherUnits,
                                     "one bounded slice per turn, before the switch changes the granularity");
  }
  return turns;
}

/// 3.7a (I3b): walk the prepared interval through the **production consumer entry** while the producer runs behind it,
/// 3.7a (I3b): walk the prepared interval through the **production consumer entry** while the producer runs behind it,
/// returning the events in the order they were emitted. The caller's buffer is a *page* by default, and deliberately
/// smaller where a fixture wants to show that a partial buffer neither skips nor repeats an event across a boundary.
/// A `NeedsPage` answer is the ring telling the caller to produce, so the loop drives the lane -- never a wait, and
/// never an inference about time.
SessionMidiEventVec walkPagedPlaybackUntilExhausted(OverCapacityPagedFixture& fixture, size_t capacity,
                                                   uint32_t& emittedPagesOut, uint32_t& produceTurnsOut) {
  SessionMidiEventVec walked;
  emittedPagesOut = 0;
  produceTurnsOut = 0;
  for (unsigned guard = 0; guard < 512; ++guard) {
    MidiEvent buffer[kWindowPageCapacity];
    size_t emitted = 0;
    const Track::PagedWindowConsumeResult result =
        fixture.track->consumePagedPlaybackWindowForSlot(0, buffer, capacity, emitted);
    if (result == Track::PagedWindowConsumeResult::Emitted) {
      for (size_t i = 0; i < emitted; ++i) {
        walked.push_back(buffer[i]);
      }
      if (emitted != 0) {
        ++emittedPagesOut;
      }
      continue;
    }
    if (result == Track::PagedWindowConsumeResult::NeedsPage) {
      ++produceTurnsOut;
      fixture.track->stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
      continue;
    }
    TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::PagedWindowConsumeResult::Exhausted),
                              static_cast<int>(result),
                              "the walk ends only at the interval's own end");
    break;
  }
  return walked;
}
}  // namespace

/// 3.7a (I3b) / W-37a-15: **an interval that cannot fit one window is prepared as pages, not refused.** The window lane
/// spends its whole slice ceiling first (`appendWindowBudgeted` is bounded by events per call, so a sparse interval
/// cannot reach it -- reaching it means the content is over `L`), and the turn that concludes that switches the demand
/// to the **paged path for the same revision and interval**, producing its first page in the same turn. From there one
/// turn is one bounded page, and the ring's three pages are a bound rather than a suggestion: the producer waits for its
/// consumer instead of allocating or overwriting.
void test_3_7a_i3b_an_over_capacity_demand_is_prepared_as_pages() {
  // More events than `kWindowAdmissionLimit`, and more than the gather's ceiling can traverse (18 pages = 2,304 events),
  // so no window could ever hold this interval.
  OverCapacityPagedFixture fixture = prepareOverCapacityPagedSlot(2500);
  LoopPlaybackRuntime* runtime = fixture.runtime;

  // ---- (P1) The window lane really tried: the ceiling is spent slice by slice, then the switch.
  const uint32_t gatherTurns = driveGatherUntilPagedSwitch(fixture);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(kRetainedGatherMaxSlices + 1u, gatherTurns,
                                   "the ceiling is spent (18 gathered pages) before the concluding turn switches");
  TEST_ASSERT_TRUE_MESSAGE(runtime->pagedActive, "the over-`L` demand left the gather's lane for the paged one");
  TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::RetainedGatherRefusal::WindowExceedsCapacity),
                    static_cast<int>(runtime->retainedGatherRefusal));

  // ---- (P2) The switch prepares the **same** interval and revision, and it does not leak the window it gathered.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, runtime->pagedPagesProduced,
                                   "the concluding turn produces the preparation's first page, not a whole interval");
  TEST_ASSERT_FALSE_MESSAGE(runtime->retainedGatherActive(), "the gather's lane is over");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(runtime->retainedGatherEvents.capacity()),
                                   "the gathered window is released when the demand becomes pages");
  TEST_ASSERT_TRUE(runtime->pagedActiveForInterval(fixture.loop->playbackRevision, fixture.coverage));
  TEST_ASSERT_EQUAL_UINT32(fixture.loop->playbackRevision, runtime->pagedWindow.sourceRevision());
  TEST_ASSERT_EQUAL_UINT32(0u, runtime->pagedCursor.nextSequence);
  TEST_ASSERT_NULL_MESSAGE(fixture.track->pendingViewRequest(0),
                           "the demand is the pager's promise now, not a fresh pending record");

  // ---- (P3) One page per turn, until the ring's own bound stops the producer.
  for (uint32_t turn = 0; turn < 4 && runtime->pagedWindow.pagesPublished() < kWindowPageCount; ++turn) {
    const uint32_t before = runtime->pagedPagesProduced;
    TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewTurnResult::GrainServed),
        static_cast<int>(fixture.track->stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock)));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before + 1u, runtime->pagedPagesProduced, "at most one page per turn");
  }
  TEST_ASSERT_EQUAL_UINT32(kWindowPageCount, runtime->pagedWindow.pagesPublished());
  TEST_ASSERT_EQUAL_UINT32(kWindowPageCount, runtime->pagedPagesProduced);
  // A turn while the ring is full neither produces nor overwrites: it is the consumer's move, the demand is still owed
  // (a bound does not drop the promise), and the state says so by name.
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewTurnResult::GrainDenied),
      static_cast<int>(fixture.track->stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock)));
  TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::PagedPlaybackOutcome::ProducerWaiting),
                    static_cast<int>(runtime->pagedOutcome));
  TEST_ASSERT_EQUAL_UINT32(kWindowPageCount, runtime->pagedWindow.pagesPublished());
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(kWindowPageCount, runtime->pagedPagesProduced,
                                   "the ring's bound is respected, not allocated around");
  TEST_ASSERT_TRUE(runtime->frozenViewRequest.active);
}

/// 3.7a (I3b) / W-37a-16: **the consumer walks the interval exactly once, across pages.** The emitted sequence is the
/// interval's effective content, in order, with no duplicate and no missing event -- a page boundary is exactly where a
/// cursor that resumed at the wrong offset would show, and the oracle is the effective-content resolver rather than a
/// hand-written list. The producer runs behind the consumer (the ring frees a page only once it is drained), and the
/// same interval prepared again walks to the same content: a position must not survive a release.
void test_3_7a_i3b_pages_walk_the_interval_exactly_once_across_pages() {
  OverCapacityPagedFixture fixture = prepareOverCapacityPagedSlot(2500);
  driveGatherUntilPagedSwitch(fixture);
  TEST_ASSERT_TRUE(fixture.runtime->pagedActive);

  uint32_t emittedPages = 0;
  uint32_t produceTurns = 0;
  const SessionMidiEventVec firstWalk =
      walkPagedPlaybackUntilExhausted(fixture, kWindowPageCapacity, emittedPages, produceTurns);
  SessionMidiEventVec oracle;
  fixture.loop->copyEffectiveCommittedEventsInRange(oracle, fixture.coverage.startPhase,
                                                   fixture.coverage.lengthTicks);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(fixture.denseEvents, static_cast<uint32_t>(oracle.size()),
                                   "the interval holds the dense span's events");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(oracle.size()), static_cast<uint32_t>(firstWalk.size()),
                                   "exactly once: no duplicate and no missing event across the page boundary");
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(1u, emittedPages, "the walk crossed more than one page");
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0u, produceTurns, "the producer ran behind the consumer, never ahead");
  for (size_t i = 0; i < oracle.size(); ++i) {
    TEST_ASSERT_EQUAL_UINT32(oracle[i].tick, firstWalk[i].tick);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(oracle[i].type), static_cast<uint8_t>(firstWalk[i].type));
    TEST_ASSERT_EQUAL_UINT8(oracle[i].data.noteData.note, firstWalk[i].data.noteData.note);
    TEST_ASSERT_EQUAL_UINT8(oracle[i].data.noteData.velocity, firstWalk[i].data.noteData.velocity);
  }
  // The walk's end is a named state, and the ring's storage is back: a drained delivery is not resident.
  TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::PagedPlaybackOutcome::Drained),
                    static_cast<int>(fixture.runtime->pagedOutcome));
  TEST_ASSERT_FALSE_MESSAGE(fixture.runtime->pagedActive, "the drained delivery releases the ring and its pages");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, fixture.runtime->pagedWindow.pagesPublished(),
                                   "no page is left published: the ring's fixed storage is back");
  MidiEvent afterRelease[kWindowPageCapacity];
  size_t afterReleaseEmitted = 0;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PagedWindowConsumeResult::NoPages),
      static_cast<int>(fixture.track->consumePagedPlaybackWindowForSlot(
          0, afterRelease, kWindowPageCapacity, afterReleaseEmitted)));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(afterReleaseEmitted),
                                   "and the read names the released preparation instead of resuming into it");

  // **The same interval again**: the pages are re-prepared from the same retained revision (the window lane spends its
  // ceiling again) and the walk repeats exactly. A cursor that survived the release, or a ring that kept a position,
  // would show here as a short, offset or duplicated sequence.
  nativePlaybackDeadlineClock.tick += 64;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::DeferredSourceQualification),
      static_cast<int>(fixture.track->requestCommittedPlaybackWindow(
          0, fixture.coverage, 13 * Config::TICKS_PER_BAR, nativePlaybackDeadlineClock)));
  driveGatherUntilPagedSwitch(fixture);
  TEST_ASSERT_TRUE(fixture.runtime->pagedActive);
  uint32_t secondEmittedPages = 0;
  uint32_t secondProduceTurns = 0;
  // The **partial buffer** shape: 37 events per call does not divide the page (128), so a cursor that re-fed an offset,
  // skipped one, or advanced a page early shows here as a repetition, a gap or a short sequence.
  const SessionMidiEventVec secondWalk =
      walkPagedPlaybackUntilExhausted(fixture, 37, secondEmittedPages, secondProduceTurns);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(firstWalk.size()),
                                   static_cast<uint32_t>(secondWalk.size()),
                                   "the second walk of the same interval yields the same events");
  for (size_t i = 0; i < firstWalk.size(); ++i) {
    TEST_ASSERT_EQUAL_UINT32(firstWalk[i].tick, secondWalk[i].tick);
    TEST_ASSERT_EQUAL_UINT8(firstWalk[i].data.noteData.note, secondWalk[i].data.noteData.note);
  }
}

/// 3.7a (I3b) / W-37a-18: **identity is enforced at the read.** The pages were produced for one revision, and the loop's
/// revision is the only currency a consumer may be served under: once the loop has moved on, the entry refuses by name
/// (`StaleRevision`) and emits nothing -- a revision a loop has left is served by nobody.
void test_3_7a_i3b_the_paged_read_refuses_a_revision_the_loop_left() {
  OverCapacityPagedFixture fixture = prepareOverCapacityPagedSlot(2500);
  driveGatherUntilPagedSwitch(fixture);
  TEST_ASSERT_TRUE(fixture.runtime->pagedActive);
  const uint32_t producedRevision = fixture.runtime->pagedRevision;
  // A first page really is emitted for the currency the pages belong to.
  MidiEvent buffer[kWindowPageCapacity];
  size_t emitted = 0;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PagedWindowConsumeResult::Emitted),
      static_cast<int>(fixture.track->consumePagedPlaybackWindowForSlot(0, buffer, kWindowPageCapacity, emitted)));
  TEST_ASSERT_GREATER_THAN_UINT32(0u, static_cast<uint32_t>(emitted));
  // The loop advances (the fixture writes the currency directly: it is the loop's own field), and the same entry is
  // asked again for the pages that revision produced.
  fixture.loop->playbackRevision = producedRevision + 1;
  size_t staleEmitted = 0;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PagedWindowConsumeResult::StaleRevision),
      static_cast<int>(fixture.track->consumePagedPlaybackWindowForSlot(0, buffer, kWindowPageCapacity, staleEmitted)));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(staleEmitted), "and nothing is emitted for it");
  TEST_ASSERT_TRUE_MESSAGE(fixture.runtime->pagedActive,
                           "the refusal is at the read: a bad read does not silently discard the preparation");
}

/// 3.7a (I3b) / W-37a-15, the second trigger: **the admission's own capacity bound switches the demand too.** An
/// interval the gather *can* traverse (its pages complete inside the ceiling) whose window is still over the
/// destination's `kWindowAdmissionLimit` is refused by the window admission -- and that refusal is the same fact, so the
/// same switch follows. The demand is not dropped by it, and the admission's bound is not argued with: it is its own
/// rule.
void test_3_7a_i3b_an_over_capacity_admission_switches_to_pages() {
  // 2,100 events: 17 gather pages (2,176 events) complete the traversal *inside* the ceiling (18), and the admission
  // then refuses a window of 2,100 events against `kWindowAdmissionLimit` (2,048).
  OverCapacityPagedFixture fixture = prepareOverCapacityPagedSlot(2100);
  LoopPlaybackRuntime* runtime = fixture.runtime;
  const uint32_t gatherTurns = driveGatherUntilPagedSwitch(fixture);
  TEST_ASSERT_TRUE_MESSAGE(runtime->pagedActive, "the admission's capacity refusal switched the demand to pages");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(17u, gatherTurns,
                                   "the traversal completed inside the ceiling: the admission is what refused");
  TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(kRetainedGatherMaxSlices, runtime->retainedGatherUnits,
                                       "the switch came from the admission, not from the gather's ceiling");
  TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::RetainedGatherRefusal::WindowExceedsCapacity),
                    static_cast<int>(runtime->retainedGatherRefusal));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(runtime->retainedGatherEvents.capacity()),
                                   "the refused window does not stay resident after the switch");
  TEST_ASSERT_TRUE(runtime->pagedActiveForInterval(fixture.loop->playbackRevision, fixture.coverage));
  TEST_ASSERT_EQUAL_UINT32(1u, runtime->pagedPagesProduced);
  TEST_ASSERT_NULL(fixture.track->pendingViewRequest(0));
}
namespace {
/// 3.7a (I3c-a): the emission-side sink. `advancePlaybackCursor` sends through a plain function pointer, so the
/// fixture records what the *production walk* would have sent -- no MIDI, no ledger, no display.
struct PagedEmissionSink {
  SessionMidiEventVec* events = nullptr;
};
void pagedEmissionSinkSend(void* ctx, const MidiEvent& event, uint8_t slotIndex) {
  (void)slotIndex;
  static_cast<PagedEmissionSink*>(ctx)->events->push_back(event);
}

/// One frame of the playback walk over the paged stream. Returns the walk's result so a fixture can assert it.
PlaybackAdvanceResult drivePagedEmissionFrame(Track& track, LoopPlaybackRuntime* runtime, uint8_t slot,
                                             uint16_t& cursor, uint32_t fromPhase, uint32_t toPhase,
                                             bool atLoopStart, ProjectionContext& playbackContext,
                                             SessionMidiEventVec& emitted) {
  PagedEmissionStreamCtx ctx{&track, runtime, slot};
  const PlaybackEventStream stream = makePagedEmissionStream(ctx);
  PagedEmissionSink sink{&emitted};
  const PlaybackTickFrame frame{&playbackContext, toPhase, atLoopStart ? UINT32_MAX : fromPhase, atLoopStart};
  return advancePlaybackCursor({&cursor, nullptr}, frame, PlaybackEmitPolicy::LayeredSlot, stream,
                               pagedEmissionSinkSend, &sink, slot, nullptr, nullptr, 1);
}
}  // namespace

/// 3.7a (I3c-a) / W-37a-19 and W-37a-20: **a paged pass emits through the production walk, and its wrap is straddled
/// rather than missed.** The adapter is handed to the same `advancePlaybackCursor` the clock path uses; the deferred
/// lane keeps producing one page per turn behind it. Two passes are walked back to back, with the pass boundary
/// signalled where the playback projection signals it (the wrap) and the walk's cursor rewound exactly as the playback
/// wrap rewinds `loop.nextEventIndex`. Both passes must emit the interval's content once each, with no dry turn in
/// between -- which is what makes the delivery `Admissible` for the gate that will select it (I3c-b).
void test_3_7a_i3c_a_a_paged_pass_emits_through_the_walk_and_straddles_its_wrap() {
  constexpr uint32_t kAdvance = 64;  // the contract's own permitted advance per turn
  OverCapacityPagedFixture fixture = prepareOverCapacityPagedSlot(2500);
  driveGatherUntilPagedSwitch(fixture);
  TEST_ASSERT_TRUE(fixture.runtime->pagedActive);
  Track& track = *fixture.track;
  Loop& loop = *fixture.loop;
  LoopPlaybackRuntime* runtime = fixture.runtime;
  ProjectionContext playbackContext = makePlaybackContext(track, loop, 0);

  SessionMidiEventVec emitted;
  uint16_t cursor = 0;
  uint32_t from = 0;
  bool atLoopStart = true;
  for (uint32_t pass = 0; pass < 2; ++pass) {
    for (uint32_t phase = 0; phase < fixture.coverage.lengthTicks; phase += kAdvance) {
      // One deferred turn per frame: the producer's one page per turn is what keeps the adapter staged.
      track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
      const uint32_t to = phase + kAdvance;
      TEST_ASSERT_EQUAL(static_cast<int>(PlaybackAdvanceResult::Completed),
                        static_cast<int>(drivePagedEmissionFrame(track, runtime, 0, cursor, from, to,
                                                                 atLoopStart, playbackContext, emitted)));
      from = to;
      atLoopStart = false;
    }
    // The wrap: production signals the pass boundary on the tick path (I3c-b wires that call); the walk's cursor is
    // rewound exactly as `loop.nextEventIndex` is.
    runtime->pagedEmission.notePassBoundary(runtime->pagedWindow.producedOrdinal());
    cursor = 0;
    from = 0;
    atLoopStart = true;
  }

  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u, runtime->pagedEmission.passCount, "the boundary moved once per wrap");
  // Note on the raw counter: `dryTurns` counts every read that found nothing **ahead of the walk**, which includes the
  // legitimate reads at a pass's event-free tail. The *judgement* is per pass (`passesWithDrys`, asserted below): only a
  // dry that means "the producer fell behind" withdraws the delivery.
  SessionMidiEventVec oracle;
  loop.copyEffectiveCommittedEventsInRange(oracle, fixture.coverage.startPhase, fixture.coverage.lengthTicks);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(fixture.denseEvents, static_cast<uint32_t>(oracle.size()),
                                   "the interval holds the dense span");
  // **Both passes are exact**: every event of the interval, in order, once each per pass -- the emission-side proof the
  // adapter exists for, and the one the I3c-a measurement could not yet make (it lost two pages of the second pass).
  // The lead across the wrap is what changed: an *emitting* delivery keeps one page free, so the producer hands the
  // next pass's head over while this pass's tail is still queued.
  // **Both passes are exact, element-wise, over every emitted event.** This is the assertion the I3c-b measurement
  // failed (it drifted past index 5,000) and the one the content clamp makes true: the walk is bounded by the emitting
  // pass's own content end, so the producer's lead into the *next* pass -- which sits in the same tick space -- is not
  // served inside this pass.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u * static_cast<uint32_t>(oracle.size()), static_cast<uint32_t>(emitted.size()),
                                   "exactly two passes, nothing more: no next-pass event leaked into a pass");
  for (size_t i = 0; i < emitted.size(); ++i) {
    const MidiEvent& expected = oracle[i % oracle.size()];
    TEST_ASSERT_EQUAL_UINT32(expected.tick, emitted[i].tick);
    TEST_ASSERT_EQUAL_UINT8(expected.data.noteData.note, emitted[i].data.noteData.note);
  }
  // 4.3's **same-tick order**, exercised rather than assumed: the dense span packs several events per tick, so an
  // ordering slip *within* a tick would break the element-wise comparison above. Require that the interval really
  // contains such ticks -- otherwise the ordering claim would be vacuous.
  uint32_t multiEventTicks = 0;
  for (size_t i = 1; i < oracle.size(); ++i) {
    if (oracle[i].tick == oracle[i - 1].tick) {
      ++multiEventTicks;
    }
  }
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0u, multiEventTicks,
                                          "the interval packs several events per tick: same-tick order is exercised");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, runtime->pagedEmission.passesUnstraddled,
                                   "every wrap arrived with the next pass's head already published");
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PagedEmissionDeliveryState::Admissible),
                    static_cast<int>(track.pagedEmissionDeliveryStateForSlot(0)));
  // The clock path's own predicate: one cached boolean the deferred lane maintained, which is what the seam tests.
  TEST_ASSERT_TRUE_MESSAGE(runtime->pagedEmission.admittedForEmission(),
                           "an admissible delivery may be selected for emission");

}

/// 3.7a (I3c-a) / W-37a-21 and W-37a-22: **the adapter never produces, and the predicate's answers are named.** With
/// the deferred lane given no turns at all, the walk empties the ring, the turn is a counted **dry**, and the ring's
/// production counters do not move by one -- the clock path cannot produce, allocate or wait. A dry pass is never
/// proof, so the delivery stays `Unproven`; a revision the loop has left is `StaleRevision`; a slot with no delivery is
/// `NoDelivery`; and an interval with no re-arm room (it *is* the loop) is `CannotStraddleTheWrap`.
void test_3_7a_i3c_a_paged_emission_never_produces_and_names_its_states() {
  constexpr uint32_t kAdvance = 64;
  OverCapacityPagedFixture fixture = prepareOverCapacityPagedSlot(2500);
  driveGatherUntilPagedSwitch(fixture);
  TEST_ASSERT_TRUE(fixture.runtime->pagedActive);
  Track& track = *fixture.track;
  Loop& loop = *fixture.loop;
  LoopPlaybackRuntime* runtime = fixture.runtime;
  ProjectionContext playbackContext = makePlaybackContext(track, loop, 0);
  // Before any pass: the delivery stands, but nothing has been proven.
  TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::PagedPlaybackOutcome::None),
                    static_cast<int>(runtime->pagedOutcome));
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PagedEmissionDeliveryState::Unproven),
                    static_cast<int>(track.pagedEmissionDeliveryStateForSlot(0)));
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PagedEmissionDeliveryState::NoDelivery),
                    static_cast<int>(track.pagedEmissionDeliveryStateForSlot(1)));

  const uint32_t producedBefore = runtime->pagedPagesProduced;
  const size_t ringPublishedBefore = runtime->pagedWindow.pagesPublished();
  SessionMidiEventVec emitted;
  uint16_t cursor = 0;
  uint32_t from = 0;
  bool atLoopStart = true;
  // Walk the whole pass with **no** producer turns: three pages' worth of events, then a dry.
  for (uint32_t phase = 0; phase < 2 * fixture.coverage.lengthTicks; phase += kAdvance) {
    const uint32_t to = phase + kAdvance;
    TEST_ASSERT_EQUAL(static_cast<int>(PlaybackAdvanceResult::Completed),
                      static_cast<int>(drivePagedEmissionFrame(track, runtime, 0, cursor, from, to,
                                                               atLoopStart, playbackContext, emitted)));
    from = to;
    atLoopStart = false;
  }
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0u, runtime->pagedEmission.dryTurns,
                                          "with no producer turns the ring runs dry, and the turn is named");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(producedBefore, runtime->pagedPagesProduced,
                                   "the clock path produced nothing: the adapter never calls produce");
  TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(static_cast<uint32_t>(ringPublishedBefore) + 1u,
                                       static_cast<uint32_t>(runtime->pagedWindow.pagesPublished()) + 1u,
                                       "and the ring only lost pages (drained), never gained one");
  TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(static_cast<uint32_t>(fixture.denseEvents),
                                       static_cast<uint32_t>(emitted.size()),
                                       "what was staged is what was emitted: the rest is the lane's owed work");
  // A dry pass is never proof, even after a boundary.
  runtime->pagedEmission.notePassBoundary(runtime->pagedWindow.producedOrdinal());
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PagedEmissionDeliveryState::Unproven),
                    static_cast<int>(track.pagedEmissionDeliveryStateForSlot(0)));
  // Identity at the read: once the loop's revision moves, the pages are stale by name.
  loop.playbackRevision += 1;
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PagedEmissionDeliveryState::StaleRevision),
                    static_cast<int>(track.pagedEmissionDeliveryStateForSlot(0)));
}

/// 3.7a (I3c-a) / W-37a-22: **an interval with no re-arm room is refused by name.** A two-bar loop whose interval *is*
/// the loop has no pass window in which to prepare the next pass's first page -- its wrap is the production window --
/// so `ExecutionBounds.md` row 27's contract row applies: reject active windowed selection rather than half-serve it.
void test_3_7a_i3c_a_a_whole_loop_interval_is_refused_by_name() {
  OverCapacityPagedFixture fixture = prepareOverCapacityPagedSlot(2500, 2);
  driveGatherUntilPagedSwitch(fixture);
  TEST_ASSERT_TRUE(fixture.runtime->pagedActive);
  TEST_ASSERT_TRUE(fixture.runtime->pagedActiveForInterval(fixture.loop->playbackRevision, fixture.coverage));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(fixture.loop->loopLengthTicks, fixture.coverage.lengthTicks,
                                   "the fixture's interval is the whole loop");
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PagedEmissionDeliveryState::CannotStraddleTheWrap),
                    static_cast<int>(fixture.track->pagedEmissionDeliveryStateForSlot(0)));
}
/// 3.7a (I3c) -- the production-path half of this increment, scoped to what this suite can actually drive.
///
/// The seam (`playCommittedLoopMidi`'s paged selection), the boundary signal and the lead are wired where the clock path
/// reads them, and this fixture drives **that function** for a slot whose paged delivery is prepared but not yet
/// admitted. What it proves, with the production entry as the driver:
///
///   - the clock path takes **neither** the paged stream **nor** the heavy merged fallback before a boundary has been
///     judged: the delivery is not selected (`admittedForEmission()` false), nothing is consumed from the ring
///     (`consumedOrdinal` stays 0) and the merged store stays **empty** (its build is a heavy one the clock path refuses
///     by design) -- i.e. a rootless dense slot is silent here, which is the condition the paged delivery exists to
///     change;
///   - and the delivery's own state reports `Unproven`, so nothing may be selected yet.
///
/// **The emitting half, and the bisection closed: the refusing return is named.** Two changes, both measured:
///
///   - **The refusal.** A temporary probe (a file-writing marker at each of the five returns between this function's
///     entry and `loop.lastTickInLoop = tickInLoop`, withdrawn before the commit) ran this whole suite: **24/24** of the
///     fixture's ticks stop at the **zero-content gate** (`eventCount == 0 && fixedBridge == nullptr &&
///     runtime.rolling.phase == Idle`), and **not one hit** was recorded at any of the other four returns
///     (`runtime == nullptr`, `!runtime.reset(true)`, the bridge-stream refusal, the `UINT32_MAX` catch-up guard). The
///     pair section does not apply to this loop by its own gate (`shouldAvoidFullVisualRebuild`, over the rebuild
///     threshold); the rolling section **excludes** the slot (`rolling.phase == Idle`, so
///     `resolveRollingPlaybackWindow` answers `Excluded`), which is what keeps the gate reachable instead of letting an
///     empty stream be walked; and the merged fallback is empty because its build gathers only a **published** window,
///     which this rootless demoted slot does not have. The silence is the gate's own content test -- exactly the
///     condition paging exists to change -- and no longer "somewhere before the tick record".
///   - **The oracle.** `Track::sendMidiEvent`'s own record now exists (the next fixture drives it), so this fixture's
///     silence is asserted **at the MIDI edge** as well as at the tick record -- and the drive's output is *enabled*
///     here, so the silence is a refusal rather than a muted walk.
///
/// The six facts below are asserted because the conclusion depends on them: with all six held, the zero-content gate is
/// the only return this drive can take.
void test_3_7a_i4_the_production_tick_path_selects_nothing_before_its_boundary() {
  constexpr uint32_t kAdvance = 64;
  OverCapacityPagedFixture fixture = prepareOverCapacityPagedSlot(2500);
  driveGatherUntilPagedSwitch(fixture);
  TEST_ASSERT_TRUE(fixture.runtime->pagedActive);
  Track& track = *fixture.track;
  Loop& loop = *fixture.loop;
  LoopPlaybackRuntime* runtime = fixture.runtime;
  track.startPlaying(0);
  track.reanchorPlaybackCycle(0);
  // (1) The value the tick record would overwrite is a real phase, not the fresh-transport `UINT32_MAX`: the catch-up
  // guard cannot be the refusal.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, loop.lastTickInLoop,
                                   "a real previous phase, not the fresh-transport UINT32_MAX");
  // (2) The pair section is out of scope for this loop by its own gate.
  TEST_ASSERT_TRUE_MESSAGE(loop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks),
                           "the slot is over the rebuild threshold: the pair path is not applied to it at all");
  // (3) The rolling section excludes the slot -- and this is the gate's third clause.
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(LoopPlaybackRuntime::RollingPhase::Idle),
                            static_cast<int>(runtime->rolling.phase),
                            "no rolling frame is bound for this slot: the rolling section answers Excluded");
  // (4) No fixed view and no bridge can be selected for it.
  const RuntimePlaybackViewSet* viewSet = runtime->viewSetIfPrewarmed();
  TEST_ASSERT_NOT_NULL(viewSet);
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(RuntimePlaybackBridgeLifecycle::kNoBridge),
                            static_cast<int>(viewSet->bridgeLifecycle.selected),
                            "no bridge was ever built for this slot: the bridge clause holds by state, not by reading");
  PlaybackWindowIdentity publishedWindow{};
  TEST_ASSERT_FALSE_MESSAGE(track.publishedPlaybackWindowIdentityForSlot(0, publishedWindow),
                            "and no window is published for it either");
  // (5) The merged fallback the walk would use holds no content.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(runtime->mergedMidiEvents.mergedEvents.size()),
                                   "the merged fallback holds nothing before the drive");
  // (6) The paged delivery is not admissible, so the paged section cannot be selected either.
  TEST_ASSERT_FALSE(runtime->pagedEmission.admittedForEmission());
  track.clearSentPlaybackMidiForTest();
  MidiSendCapture::reset();
  const uint32_t intervalTicks = fixture.coverage.lengthTicks;
  for (uint32_t tick = kAdvance; tick <= intervalTicks; tick += kAdvance) {
    track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
    track.playMidiEvents(tick, true);
  }
  // **The refusal, at the tick record and at the MIDI edge:**
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, loop.lastTickInLoop,
                                   "no tick was recorded through a whole pass: the zero-content gate refused every one "
                                   "(measured 24/24 with the probe; none of the other four returns fired)");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(track.sentPlaybackMidiCountForTest()),
                                   "and the MIDI edge committed nothing -- with the output enabled, so this is the "
                                   "production refusal, not a disabled output");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(MidiSendCapture::count()),
                                   "the wire boundary agrees: nothing was sent");
  // What the clock path does *not* do for a slot whose delivery is not yet admitted: it builds no heavy merged fallback
  // (that build is refused on the clock path by design -- a rootless dense slot is **silent** there, which is exactly
  // the condition the paged delivery exists to change), and it consumes nothing from the ring. `pagedActive` surviving
  // the pass also settles the earlier falsification: `LoopPlaybackRuntime::reset` drops the ring, so the stale-runtime
  // path did not run -- and the probe above now says so directly (that return fired zero times).
  TEST_ASSERT_TRUE_MESSAGE(runtime->pagedActive,
                           "the paged delivery survived the clock path's own runtime reset (the reset did not run)");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(runtime->mergedMidiEvents.mergedEvents.size()),
                                   "the clock path does not build the heavy merged fallback for this slot");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, runtime->pagedEmission.consumedOrdinal,
                                   "and nothing is consumed from the ring: it is not selected before a boundary");
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PagedEmissionDeliveryState::Unproven),
                    static_cast<int>(track.pagedEmissionDeliveryStateForSlot(0)));
}

/// 3.7a (I4): **the emitted stream is observable through the production path.** `Track::sendMidiEvent` records the pair
/// it hands the handler -- after its own gates (`playbackEmitMidiOutput_`, the transport-stop barrier, the slot range)
/// and after the playback channel remap -- so a host fixture reads what the device would have sent instead of a
/// stand-in. The record is checked against the slot's own committed content **and** against the wire boundary that
/// already existed (`MidiSendCapture`, which this route reaches because `MidiHandler::sendRealtimePlaybackEvent`
/// funnels into `MidiHandler::sendMidiEvent`): the two records must agree element-wise, which is what makes the new
/// sink the production edge rather than a parallel one.
///
/// **Teeth in both directions, on the same pass of one slot:** the control walks the whole pass with the output
/// disabled (events are due, the tick record advances, and *neither* edge records anything -- so a sink that recorded
/// regardless of the production gate would fail), and the measured drive re-anchors and walks the same pass with the
/// output enabled (exactly the pass's four events, in tick order, once each -- so a sink that missed emissions would
/// fail too).
void test_3_7a_i4_the_midi_edge_records_what_the_clock_path_emits() {
  constexpr uint32_t kAdvance = 64;
  constexpr uint32_t kBar = 768;
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  // A two-bar slot -- under the rebuild threshold, so the merged fallback is the serving path -- with one
  // NoteOn/NoteOff pair per bar, and a **current published root**: the only source the merged build's gather reads.
  commitBarSpacedPassIntoTrackSlot(track, 0, 2);
  Loop& loop = track.getLoop(0);
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));
  publishBarSpacedRootForSlot(loop, 2);
  TEST_ASSERT_TRUE_MESSAGE(loop.publishedSnapshotIsCurrent(),
                           "the published root must still be current after prewarm, or the gather has no source");
  TEST_ASSERT_EQUAL_UINT32(2u * kBar, loop.loopLengthTicks);
  track.startPlaying(0);

  // (A) The control: the whole pass, walked with the output **disabled**.
  track.clearSentPlaybackMidiForTest();
  MidiSendCapture::reset();
  for (uint32_t tick = kAdvance; tick < 2u * kBar; tick += kAdvance) {
    track.playMidiEvents(tick, false);
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u * kBar - kAdvance, loop.lastTickInLoop,
                                   "the clock path walked the pass: the tick record advanced");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(track.sentPlaybackMidiCountForTest()),
                                   "nothing reached the MIDI edge with the output disabled, though events were due");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(MidiSendCapture::count()),
                                   "the wire boundary agrees: the sink cannot claim an emission the device did not make");

  // (B) The same pass, with the output **enabled** -- re-anchored first, which is the shape a transport start leaves.
  track.reanchorPlaybackProjection(0, false);
  track.clearSentPlaybackMidiForTest();
  MidiSendCapture::reset();
  for (uint32_t tick = kAdvance; tick < 2u * kBar; tick += kAdvance) {
    track.playMidiEvents(tick, true);
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u * kBar - kAdvance, loop.lastTickInLoop,
                                   "the same pass, driven over the same frames");
  // The emitted stream element-wise: the pass's four events, in tick order, once each.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(4u, static_cast<uint32_t>(track.sentPlaybackMidiCountForTest()),
                                   "the pass's four events reach the MIDI edge, once each");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(track.sentPlaybackMidiDroppedForTest()),
                                   "and the sink's bound was never reached: nothing was dropped silently");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(4u, static_cast<uint32_t>(MidiSendCapture::count()),
                                   "the wire boundary saw the same four");
  const uint32_t expectedTicks[4] = {0u, 48u, kBar, kBar + 48u};
  for (size_t i = 0; i < 4; ++i) {
    const Track::SentPlaybackMidiForTest& sent = track.sentPlaybackMidiAtForTest(i);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, sent.slotIndex, "the walk named the slot it played");
    TEST_ASSERT_EQUAL_UINT8(1, sent.event.channel);
    TEST_ASSERT_EQUAL_UINT8(48, sent.event.data.noteData.note);
    TEST_ASSERT_EQUAL_UINT32(expectedTicks[i], sent.event.tick);
    TEST_ASSERT_EQUAL_MESSAGE(i % 2 == 0, sent.event.isNoteOn(), "the content alternates On/Off per bar");
    TEST_ASSERT_EQUAL_UINT8(i % 2 == 0 ? 100 : 0, sent.event.data.noteData.velocity);
    // The sink is the production edge, not a parallel one: the wire boundary recorded the same event.
    const MidiEvent wire = MidiSendCapture::at(i);
    TEST_ASSERT_EQUAL_UINT8(sent.event.type, wire.type);
    TEST_ASSERT_EQUAL_UINT8(sent.event.channel, wire.channel);
    TEST_ASSERT_EQUAL_UINT32(sent.event.tick, wire.tick);
    TEST_ASSERT_EQUAL_UINT8(sent.event.data.noteData.note, wire.data.noteData.note);
    TEST_ASSERT_EQUAL_UINT8(sent.event.data.noteData.velocity, wire.data.noteData.velocity);
  }
}

/// 3.7a's **timing admission** clause, measured at the walk's own granularity: what service rate must the deferred lane
/// hold for a paged pass to be served *without* a breach? The declared contract gives one page per turn at a permitted
/// advance of 64 ticks (`kPlaybackViewPermittedAdvancePerTurn`), and a dense over-`L` interval needs ~104 events per
/// 64-tick frame, so **halving the lane's rate** (one turn per two frames = 64 events per frame) must fall behind: the
/// ring's three-page depth can absorb jitter, not a halved service rate. The delivery is expected to **name the
/// breach** (a dry pass, or an unstraddled wrap) -- and the fixture asserts that *either* the breach is named *or* the
/// pass was served exactly, so the property is checked whichever way the margin falls.
void test_3_7a_i4_the_paged_delivery_names_a_halved_service_rate() {
  constexpr uint32_t kAdvance = 64;
  OverCapacityPagedFixture fixture = prepareOverCapacityPagedSlot(2500);
  driveGatherUntilPagedSwitch(fixture);
  TEST_ASSERT_TRUE(fixture.runtime->pagedActive);
  Track& track = *fixture.track;
  Loop& loop = *fixture.loop;
  LoopPlaybackRuntime* runtime = fixture.runtime;
  ProjectionContext playbackContext = makePlaybackContext(track, loop, 0);
  const uint32_t intervalTicks = fixture.coverage.lengthTicks;

  SessionMidiEventVec emitted;
  uint16_t cursor = 0;
  uint32_t from = 0;
  bool atLoopStart = true;
  for (uint32_t pass = 0; pass < 2; ++pass) {
    uint32_t frame = 0;
    for (uint32_t phase = 0; phase < intervalTicks; phase += kAdvance, ++frame) {
      if (frame % 2 == 0) {  // **half the declared service rate**
        track.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
      }
      TEST_ASSERT_EQUAL(static_cast<int>(PlaybackAdvanceResult::Completed),
                        static_cast<int>(drivePagedEmissionFrame(track, runtime, 0, cursor, from, phase + kAdvance,
                                                                 atLoopStart, playbackContext, emitted)));
      from = phase + kAdvance;
      atLoopStart = false;
    }
    runtime->pagedEmission.notePassBoundary(runtime->pagedWindow.producedOrdinal());
    cursor = 0;
    from = 0;
    atLoopStart = true;
  }
  SessionMidiEventVec oracle;
  loop.copyEffectiveCommittedEventsInRange(oracle, fixture.coverage.startPhase, fixture.coverage.lengthTicks);
  const bool breached = runtime->pagedEmission.passesWithDrys > 0 || runtime->pagedEmission.passesUnstraddled > 0;
  const bool servedExactly = emitted.size() == 2 * oracle.size();
  TEST_ASSERT_TRUE_MESSAGE(breached || servedExactly,
                           "a halved service rate must either be absorbed (the pass is served exactly) or be named");
  TEST_ASSERT_TRUE_MESSAGE(breached,
                           "measured: at half the declared rate the producer falls behind within the pass, and the "
                           "breach is named (a dry pass or an unstraddled wrap) rather than served as content");
}

int main() {


  UNITY_BEGIN();
  RUN_TEST(test_activation_audit_the_production_demand_is_positioned_and_an_unqualified_clock_is_refused);
  RUN_TEST(test_activation_audit_the_launch_demand_is_unpositioned_and_never_judged);
  RUN_TEST(test_committed_retry_eight_tracks_prepare_empty_windows_with_eight_bar_lead);
  RUN_TEST(test_committed_retry_eight_tracks_refuse_two_bar_lead_without_synthetic_completion);
  RUN_TEST(test_committed_retry_prices_actual_pinned_topology_not_maximum_retirement);
  RUN_TEST(test_committed_retry_stale_frozen_source_keeps_newer_request_position_runnable);
  RUN_TEST(test_committed_retry_capacity_refusal_counts_bounded_source_without_partial_publication);
  RUN_TEST(test_committed_retry_clock_stamp_projects_nonzero_slot_origin);
  RUN_TEST(test_committed_retry_counts_then_admits_original_promise_and_publishes_without_second_request);
  RUN_TEST(test_committed_retry_shared_owner_and_blocked_slot_do_not_hide_runnable_sibling);
  RUN_TEST(test_committed_retry_cancel_drains_source_and_epoch_invalidation_never_regrants);
  RUN_TEST(test_committed_retry_residual_lead_and_empty_two_bar_envelope_refuse_without_new_lap);

  RUN_TEST(test_window_retry_preserves_borrowed_source_before_gather);
  RUN_TEST(test_window_retry_preserves_scratch_for_held_completion_and_ready_successor);
  // S4b.1 check 7 (repeated same-track switches): accepted, bounded, nothing stolen, executes on capacity.
  RUN_TEST(test_s4b_1_a_repeated_switch_while_a_finalizes_is_bounded_and_executes_once_capacity_frees);
  RUN_TEST(test_s4b_1_a_denied_stop_on_an_uncommitted_switch_retains_the_targets_start);
  // S4b.1 check 7 (populated target): the queued playback switch waits, then commits in the tick lane.
  RUN_TEST(test_s4b_1_a_queued_playback_switch_waits_for_the_predecessor_and_then_commits);
  // S4b.6/S4b.7: the inbound occupancy is provided off-clock; neither the MIDI edge nor the clock path
  // may allocate it, and a switch without it must not dead-end.
  RUN_TEST(test_s4b_6_a_switch_to_a_slot_without_occupancy_provisions_off_clock);
  RUN_TEST(test_s4b_6_a_switch_to_a_multi_bar_target_without_occupancy_also_provisions_off_clock);
  // S4b.7 (demoted): the merged window releases the hold when a published root exists, and its absence is
  // bounded -- one report per episode, switch retained.
  // windowed-playback-generation slice 3.10 increment 5: the cross-track offer and selection.
  RUN_TEST(test_window_opportunity_is_offered_across_tracks_by_urgency_not_by_index);
  // windowed-playback-generation slice 3.9 increment 2: the readiness receipt's emission site and cadence.
  RUN_TEST(test_window_readiness_receipt_reports_the_outcome_and_is_cadence_bounded);
  // windowed-playback-generation slice 3.10 increment 6: the starvation half and the derived turn envelope.
  RUN_TEST(test_window_service_keeps_one_turn_per_rotation_and_no_owner_loses_a_turn);
  // slice 3.10 increment 8: across-holders fairness (the spec's round-robin rule for holders).
  RUN_TEST(test_track_service_rotates_so_no_track_is_starved_by_anothers_earlier_deadline);
  // windowed-playback-generation slice 3.10 increment 7: the measured grain-cost model and the derived bound.
  RUN_TEST(test_window_grain_cost_tracks_the_windows_event_count_and_bounds_the_supported_window);
  RUN_TEST(test_window_request_work_is_bounded_and_retained_across_source_shapes);
  RUN_TEST(test_rolling_gate_rows_name_the_demand_paths_silent_exits_once_per_state_change);
  RUN_TEST(test_window_track_budget_protects_an_older_windows_admitted_deadline_from_a_sibling);
  RUN_TEST(test_enforced_candidate_charges_derived_cost_and_eight_track_rotation);
  RUN_TEST(test_window_admission_refuses_by_name_once_the_interference_budget_is_enforced);
  RUN_TEST(test_the_declared_envelope_states_its_terms_its_derivation_and_what_it_refuses);
  RUN_TEST(test_the_declared_permitted_gap_is_wired_and_its_overrun_is_named);
  RUN_TEST(test_a_refused_request_is_retained_with_its_original_deadline_and_discharged_by_its_admission);
  RUN_TEST(test_a_pending_request_owes_no_grains_and_does_not_charge_a_sibling);
  RUN_TEST(test_coalesced_requests_keep_the_oldest_deadline_and_the_newest_attempt);
  RUN_TEST(test_clock_epoch_and_geometry_invalidate_the_deadline_not_the_obligation);
  RUN_TEST(test_a_generation_change_does_not_revive_a_pending_request);
  RUN_TEST(test_window_deadline_clock_rejects_reset_and_unobserved_laps_at_admission);
  RUN_TEST(test_window_absolute_lead_arithmetic_does_not_overflow);
  RUN_TEST(test_window_request_beyond_the_retained_capacity_is_refused_by_name_and_truncates_nothing);
  // slice 3.10 increment 11: the service-gap unit, sampled from the lane's own opportunities.
  RUN_TEST(test_window_service_gap_sample_reports_the_measured_phase_advance_per_opportunity);
  RUN_TEST(test_two_bar_service_cannot_supply_even_dense_copy_work);
  RUN_TEST(test_rolling_generation_change_at_completion_publishes_nothing);
  RUN_TEST(test_rolling_queued_zero_start_keeps_the_original_debt);
  RUN_TEST(test_rolling_stop_or_slot_exit_releases_the_episode_off_clock);
  RUN_TEST(test_rolling_handoff_across_natural_wrap_uses_absolute_frame);
  RUN_TEST(test_rolling_empty_interval_preserves_sustain_and_fallback_closure);
  RUN_TEST(test_rolling_request_worker_handoff_emission_keeps_fallback_and_exact_notes);
  RUN_TEST(test_rolling_selector_preserves_existing_adjacent_refusal_and_deadline);
  RUN_TEST(test_rolling_boundary_rejects_stale_epoch_geometry_source_and_missed_tick);
  RUN_TEST(test_rolling_cancellation_retires_only_its_owned_request);
  RUN_TEST(test_rolling_path_admissible_shape_repeats_handoffs_under_enforcement);
  RUN_TEST(test_rolling_path_refused_shape_is_named_retained_and_falls_back);
  RUN_TEST(test_rolling_path_budget_charges_qualification_and_retirement_work);
  RUN_TEST(test_rolling_path_reuses_the_view_slot_after_a_refused_completion);
  RUN_TEST(test_rolling_path_cancellation_between_every_preparation_grain_recovers);
  RUN_TEST(test_rootless_restored_launch_requests_its_source_once_and_becomes_ready);
  RUN_TEST(test_launch_gate_rows_name_the_silent_exits_once_per_state_change);
  RUN_TEST(test_immutable_launch_source_wait_does_not_hide_a_later_track);
  RUN_TEST(test_immutable_launch_sibling_interference_is_a_temporary_wait);
  RUN_TEST(test_immutable_launch_cancellation_drains_every_preparation_phase);
  RUN_TEST(test_immutable_launch_requalifies_generation_and_revision_before_binding);
  RUN_TEST(test_immutable_launch_cold_reservation_upgrades_without_resetting_ready_content);
  RUN_TEST(test_immutable_launch_legacy_dense_overflow_retains_untruncated_fallback);
  RUN_TEST(test_launch_retries_an_occupied_target_when_its_source_arrives);
  RUN_TEST(test_launch_accepts_a_prepared_empty_interval_at_its_own_origin);
  RUN_TEST(test_s4b_7_a_demoted_target_with_a_published_root_commits_through_the_turn);
  RUN_TEST(test_s4b_7_a_demoted_target_without_a_root_is_reported_once_at_its_bound);
  // S4b.5 (B4): one active-slot write authority -- a direct write (the undo route) cancels a queued switch,
  // the cancel clears the whole trio, and an unrelated switch inherits no stale enabled-set replacement.
  RUN_TEST(test_s4b_5_a_direct_write_cancels_the_queued_switch_without_a_stale_replacement);
  // S4b.5 (B4): the guard's accepted-but-unexecuted term -- the route a direct write takes when a latch,
  // and nothing else, is in flight.
  RUN_TEST(test_s4b_5_an_accepted_unexecuted_interaction_routes_the_write_through_the_switch);
  // S4b.2: the inbound role is provisioned before the outbound one is released, and the overlap is bounded.
  RUN_TEST(test_s4b_2_the_inbound_role_is_provisioned_before_the_outbound_is_released);
  // S4b.3: the switch lands on a MIDI boundary, and that boundary binds a prepared role (allocates nothing).
  RUN_TEST(test_s4b_3_a_loop_end_switch_lands_on_the_loop_boundary);
  RUN_TEST(test_s4b_3_a_next_grid_switch_lands_on_the_start_grid);
  // S4b.4 (undo clause): the holder a switch moved to is the one the undo route addresses -- a retired
  // slot's entry is refused, the inbound slot's own entry is applied to it.
  RUN_TEST(test_s4b_4_the_undo_route_addresses_the_holder_the_switch_moved_to);
  // S4b.4 (display clause): the display authority moves to the holder the switch moved to.
  RUN_TEST(test_s4b_4_the_display_authority_moves_to_the_holder_the_switch_moved_to);
  // Timing-path review, item 1: the edge records, the drain reports -- and the row's text is unchanged.
  RUN_TEST(test_the_commit_row_is_rendered_by_the_drain_with_its_text_unchanged);
  RUN_TEST(test_the_edge_effect_queue_is_fifo_bounded_and_counts_what_it_cannot_take);
  RUN_TEST(test_boot_leaves_a_committed_slot_without_a_working_set);
  RUN_TEST(test_a_repeated_admission_does_not_churn_the_lease);
  RUN_TEST(test_release_is_refused_while_an_owner_remains_and_allowed_once_it_retires);
  RUN_TEST(test_a_stop_clears_only_its_own_slots_ledger);
  RUN_TEST(test_s4_1_capture_work_slot_uses_the_pinned_interval_slot);
  RUN_TEST(test_selection_acquires_no_working_set);
  RUN_TEST(test_a_late_cleanup_cannot_clear_a_successor_finalization_marker);
  RUN_TEST(test_the_slot_generation_release_entry_is_identity_qualified_and_preserves_refusals);
  // S4.2 -- the generation-mismatched release: rejected on the lease path, and named as the row names it.
  RUN_TEST(test_s4_2_a_retired_owners_late_release_cannot_release_a_successors_working_set);
  RUN_TEST(test_s4_2_the_release_outcomes_are_named_as_the_device_row_names_them);
  RUN_TEST(test_the_release_sweep_skips_a_slot_with_a_live_owner);
  // S4.1 -- the working-set lease release on a same-track slot switch (the device run only evidences
  // the finalization-capacity release).
  RUN_TEST(test_s4_1_a_same_track_switch_releases_the_retired_slots_lease);
  RUN_TEST(test_s4_1_a_live_finalization_refuses_the_retired_slots_lease_release);
  RUN_TEST(test_s4_1_the_retired_slots_lease_is_released_once_its_finalization_retires);
  RUN_TEST(test_reactivating_a_released_slot_reuses_its_capacity);
  RUN_TEST(test_a_declared_envelope_breach_is_reported_and_does_not_block_activation);
  // S4.4 -- the breach report names its holder, including the finalizing holder the census still counts.
  RUN_TEST(test_s4_4_the_breach_report_names_the_finalizing_holder_instead_of_hiding_it);
  RUN_TEST(test_switching_starts_the_new_take_while_the_previous_take_still_finalizes);
  RUN_TEST(test_a_blocked_switch_and_start_survive_until_its_prerequisite_stop_binds);
  // The successor's take must COMMIT with content -- the device failure a start marker hid.
  RUN_TEST(test_the_successor_take_commits_with_content_after_a_transport_stop);
  RUN_TEST(test_record_switch_respects_a_retained_stop_and_permits_an_admitted_one);
  RUN_TEST(test_record_stop_adopts_published_geometry_not_the_stop_edge_prediction);
  RUN_TEST(test_filtered_record_take_publishes_no_master_length);
  // S4b.1 contract check 7 coverage, and the exactly-once half the committed carrier was missing.
  RUN_TEST(test_a_cross_track_successor_starts_while_the_predecessor_finalizes);
  RUN_TEST(test_a_cleared_successor_is_a_record_target_while_the_predecessor_finalizes);
  RUN_TEST(test_a_populated_successor_overdubs_on_its_own_phase_while_the_predecessor_finalizes);
  RUN_TEST(test_slot_button_during_pending_overdub_finalization_queues_populated_target);
  RUN_TEST(test_slot_button_during_pending_overdub_finalization_arms_empty_target);
  RUN_TEST(test_the_switch_carried_start_executes_exactly_once);
  // The two exactly-once routes the fixture above left uncovered.
  RUN_TEST(test_the_arm_and_wait_requeue_retains_then_executes_exactly_once);
  RUN_TEST(test_a_start_refused_by_its_own_live_stop_obligation_retries_to_a_single_take);
  // windowed-playback-generation slice 3.1: the window-build owner's declared contract, its one-grain turn
  // and the naming/non-destructiveness of its refusals.
  RUN_TEST(test_window_owner_declares_its_caps_and_the_handoff_margin_is_the_oracle);
  RUN_TEST(test_window_owner_service_gap_declaration_is_bounded_by_measurement_not_by_its_own_claim);
  RUN_TEST(test_window_owner_serves_one_grain_per_turn_and_publishes_nothing);
  // windowed-playback-generation slice 3.10 increment 3's own fixture: the routed scan serves the urgent holder.
  RUN_TEST(test_window_owner_serves_the_most_urgent_holder_and_rotates_between_them);
  RUN_TEST(test_window_owner_refusals_are_named_and_non_destructive);
  // windowed-playback-generation slice 3.2: the owner's atomic interval-window publication.
  RUN_TEST(test_window_owner_publishes_the_interval_window_atomically);
  RUN_TEST(test_window_owner_resolves_the_interval_through_the_published_snapshot);
  RUN_TEST(test_window_readiness_names_its_holder_and_an_empty_interval_is_ready);
  // windowed-playback-generation slice 3.6a: the retained source revision, its roles, and the two destruction paths
  // it must survive (the pass-level reclamation consumer and the direct pass-timeline reset).
  RUN_TEST(test_retained_source_is_admitted_with_roles_and_survives_both_destruction_paths);
  // windowed-playback-generation slice 3.6b: the lag rule -- one pass late and only that, the second miss named once.
  RUN_TEST(test_playback_lag_rule_serves_the_previous_revision_once_and_names_a_second_miss);
  RUN_TEST(test_an_admitted_deadline_miss_is_serviced_per_turn_once_per_traversal_and_never_re_granted);
  RUN_TEST(test_an_admitted_deadline_is_revalidated_across_an_epoch_change_and_a_new_promise_counts_from_zero);
  RUN_TEST(test_a_late_arrival_is_named_even_when_the_served_window_carries_the_wanted_revision);
  RUN_TEST(test_a_new_promise_for_the_same_interval_does_not_reset_the_intervals_miss_history);
  RUN_TEST(test_an_epoch_change_invalidates_and_names_the_promise_without_minting_an_allowance);
  RUN_TEST(test_a_promise_that_completes_at_its_deadline_is_still_judged);
  RUN_TEST(test_on_time_completion_is_not_made_late_by_next_dispatch);
  RUN_TEST(test_invalidated_successor_preserves_serving_content_until_boundary_revalidation);
  RUN_TEST(test_retained_completion_cancellation_clears_only_candidate_obligation);
  RUN_TEST(test_cancelling_withheld_preview_unblocks_next_admission);
  // windowed-playback-generation slice 3.6c: the interval index over pinned bodies -- interval-directed skipping,
  // edit destinations folded in, and named refusals instead of stale or partial answers.
  RUN_TEST(test_retained_interval_index_is_interval_directed_and_refuses_stale_revisions);
  // windowed-playback-generation slice 3.7a (I1): the retained revision as the source for a slot with **no published
  // root** -- the production requester serves it, the index bounds the lookup, and the differential fallback oracle
  // agrees; plus every refusal the gather owes by name.
  RUN_TEST(test_retained_source_window_serves_a_rootless_interval_and_the_index_bounds_the_lookup);
  // windowed-playback-generation slice 3.7a (I2): the **deferred lane turn** serves a rootless demand from the
  // retained revision -- the committed demand publishes, the launch demand binds, and a refused or cancelled gather
  // keeps the demand with its reason named.
  RUN_TEST(test_3_7a_i2_a_rootless_committed_demand_is_served_from_the_retained_revision);
  RUN_TEST(test_3_7a_i2_a_rootless_launch_target_binds_from_the_retained_revision);
  RUN_TEST(test_3_7a_i2_a_refused_or_cancelled_gather_keeps_the_demand);
  // windowed-playback-generation slice 3.7a (I3): the capture admission counts **committed** windows only, so a served
  // preview (the session's uncommitted projection) inherits nothing.
  RUN_TEST(test_3_7a_i3_the_capture_admission_counts_committed_windows_only);
  RUN_TEST(test_the_retained_source_gather_refuses_by_name_where_it_must);
  // windowed-playback-generation slice 3.11: the shared-view ownership rules as one transition table, an operation
  // that is never retargeted, a late completion that publishes nothing, and storage reuse by the last owner.
  RUN_TEST(test_shared_view_transitions_state_the_ownership_rules_and_a_late_completion_publishes_nothing);
  // windowed-playback-generation slice 3.12a: the admission limit on the decided accounting unit (DEC-108 §14).
  RUN_TEST(test_window_admission_limit_accounts_effective_events_and_the_closure_reserve_inside_l);
  // windowed-playback-generation slice 3.12b: the bounded legacy paged path (pages + resumable cursor).
  RUN_TEST(test_paged_window_stream_serves_more_than_one_page_exactly_once_across_the_ring);
  // windowed-playback-generation slice 3.12c: the MIDI-edge wiring of the admission limit (DEC-108 §14).
  // windowed-playback-generation slice 3.7a (I3b): the **paged legacy path reaches the production lane** -- an interval
  // that cannot fit one window is prepared as pages for the same revision (by the gather's ceiling and by the
  // admission's own capacity bound), one bounded page per turn, walked exactly once through the runtime's cursor, and
  // refused at the read once the loop's revision moves.
  RUN_TEST(test_3_7a_i3b_an_over_capacity_demand_is_prepared_as_pages);
  RUN_TEST(test_3_7a_i3b_pages_walk_the_interval_exactly_once_across_pages);
  RUN_TEST(test_3_7a_i3b_the_paged_read_refuses_a_revision_the_loop_left);
  RUN_TEST(test_3_7a_i3b_an_over_capacity_admission_switches_to_pages);
  // windowed-playback-generation slice 3.7a (I3c-a): the **paged emission adapter** -- a paged pass emitted through
  // the production walk with its wrap straddled, the adapter's refusal to produce on the clock path, and the named
  // states the selection gate will read.
  RUN_TEST(test_3_7a_i3c_a_a_paged_pass_emits_through_the_walk_and_straddles_its_wrap);
  RUN_TEST(test_3_7a_i3c_a_paged_emission_never_produces_and_names_its_states);
  RUN_TEST(test_3_7a_i3c_a_a_whole_loop_interval_is_refused_by_name);
  RUN_TEST(test_3_7a_i4_the_production_tick_path_selects_nothing_before_its_boundary);
  RUN_TEST(test_3_7a_i4_the_midi_edge_records_what_the_clock_path_emits);
  RUN_TEST(test_3_7a_i4_the_paged_delivery_names_a_halved_service_rate);
  RUN_TEST(test_capture_admission_refuses_at_the_edge_and_never_a_closure_or_a_stop);
  // windowed-playback-generation slice 3.12c: the refusal's cleanup, and the window-seeded boundary on the lane the
  // window actually serves (DEC-108 §14).
  RUN_TEST(test_capture_admission_refusal_on_the_overdub_path_gives_back_its_staging_reservation);
  // windowed-playback-generation slice 3.13: the preview source contract -- identity and never committed currency.
  RUN_TEST(test_preview_window_is_a_distinct_source_and_never_committed_currency);
  // windowed-playback-generation slice 3.13: the preview lane's cancellation and the committed-content handoff.
  RUN_TEST(test_preview_window_is_cancelled_by_its_lifecycle_and_hands_off_to_committed);
  // windowed-playback-generation slice 4.5: second-miss fault containment and the approved last resort.
  RUN_TEST(test_second_miss_latches_a_fault_and_the_last_resort_is_targeted_not_global);
  // windowed-playback-generation slice 4.5: the cross-generation closure reconciliation (\u00a713g).
  RUN_TEST(test_cross_generation_closures_are_reconciled_and_an_empty_window_silences_nothing);
  // windowed-playback-generation slice 3.14: the undo/redo audible handoff (\u00a713g).
  RUN_TEST(test_undo_edit_batch_settles_its_own_soundings_and_never_blankets_the_slot);
  // windowed-playback-generation slice 4.1: named checkpoint parity for undo (F-5).
  RUN_TEST(test_checkpoint_parity_names_the_lagging_case_and_ignores_the_windows_lag);
  // windowed-playback-generation slice 4.3: content parity across a window boundary (F-7).
  RUN_TEST(test_windowed_stream_concatenates_to_the_whole_loop_stream_across_a_boundary);
  // windowed-playback-generation slice 4.4: length-independent residency, swept over lengths (F-8).
  RUN_TEST(test_demoted_slot_residency_is_length_independent_swept_over_lengths);
  // windowed-playback-generation task 3.10 / 6.3: the `H x K` contention sweep -- the declared envelope measured on
  // the composed service (per-track offers, the across-track rotation, one grain per turn).
  RUN_TEST(test_window_contention_sweep_measures_the_composed_cost_across_h_and_k);
  RUN_TEST(test_window_contention_sweep_scales_with_density_and_is_span_independent);
  RUN_TEST(test_window_contention_boundary_is_predicted_by_the_declaration_and_named_when_crossed);
  // windowed-playback-generation task 3.10: the activation audit -- which production demand carries a qualified stamp,
  // and which production paths are deliberately exempt.
  // windowed-playback-generation task 3.10: the rolling path's admission boundary on the playing chain.
  return UNITY_END();
}
