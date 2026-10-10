//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// S7.5b.8b: LIVE settled-current-root entry through real cycle/short callers,
// cleanup, private selection, real Select publication and qualified feedback tails.
// Other FSMs/outbound sends remain named boundaries; no hardware timing claim.
// S75B8_ENTRY_BOUNDARY_ORACLE retains the unchanged COLD/rootless withholding
// failure: source expansion and full entry/feedback qualification remain open.
// Packet: openspec/changes/runtime-working-set-ownership/evidence/
// 2026-10-09-s75b8-note-edit-entry-boundary/LIVE-CURRENT-ROOT-ENTRY.md.

#define ARDUINO 1

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "../test_support/firmware/Arduino.h"
#include "../test_support/firmware/SD.h"

SDClass SD;

/// The sketch owns this global; the load path reads it, so the harness provides it.
bool sequencerRunning = false;

// The window lane plus the real Track/Loop/TrackManager owners and the real note-edit owner.
#define HARNESS_LINKS_REAL_EDIT_MANAGER
// The button TU is linked explicitly below (this suite's entry *is* the button action), and the real
// undo owner is linked too (`TrackUndo.cpp`), which is why the harness must not include it first.
#define HARNESS_OMITS_MIDI_BUTTON_ACTIONS
#include "../test_support/track_harness.inc"

#include "LoopSnapshot.h"

/// The focus route reports a slot change; the identities the panel is told about are part of the focus
/// contract this suite asserts.
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

/// The feedback-outbound boundary of the entry route (see `off_path_deps.h`): the production request call
/// site and the entry's nested poll call site are recorded. Nothing here models the send pipeline.
// The boundary types the outbound observation and the stub surface name must be declared first.
#include "Utils/NoteEditFaderOutboundPlan.h"

struct OutboundObservation {
  unsigned requestCalls = 0;
  NoteEditFaderOutbound::Trigger lastTrigger = NoteEditFaderOutbound::Trigger::None;
  NoteEditFaderOutbound::PlanFlags lastPlan{};
  /// How many times the entry *itself* polled the send pipeline inside the callback.
  unsigned nestedDrainCalls = 0;
  /// Departed-session audition releases the entry performed.
  unsigned auditionReleases = 0;
};
OutboundObservation& outboundObservation() {
  static OutboundObservation observation;
  return observation;
}
void resetOutboundObservation() { outboundObservation() = OutboundObservation{}; }

// The boundary types the stub surface defines members of must be declared first.
#include "ControlSurfaceManager.h"
#include "EditManager.h"
#include "MidiFaderActions.h"
#include "MidiFaderManager.h"
#include "MidiFaderProcessor.h"
#include "MidiButtonActions.h"
#include "Utils/NoteEditFaderOutboundPlan.h"

#define HARNESS_OBSERVABLE_SD_REFRESH
#define HARNESS_OBSERVABLE_SLOT_CHANGE
#define HARNESS_OBSERVABLE_START_RECLAIM
#define HARNESS_LINKS_REAL_PLAYBACK_EDGE
#include "off_path_deps.h"

// --- the real note-edit owner (the subject), plus the button entry and the session/feedback seam ---
// `src/Globals.cpp` cannot be linked here (the harness already defines its other symbols), so the one
// value the edit stack reads is supplied directly -- the same boundary the windowed-preview suite uses.
float bpm = 120.0f;

#include "../../src/MidiHandler.cpp"
#include "../../src/EditManager.cpp"
#include "../../src/NoteEditSessionState.cpp"
// `src/LoopEditManager.cpp` uses Arduino's `map()`, which the native Arduino stub does not provide.
inline long map(long x, long inMin, long inMax, long outMin, long outMax) {
  if (inMax == inMin) return outMin;
  return (x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}
#include "../../src/EditManager/EditSessionInteraction.cpp"
// The real undo owner is linked below and defines the same `resolveTrackIndex` this slot-matrix TU
// calls; `capture_storage_harness.inc` renames `LoopEditManager.cpp`'s copy for the same collision.
#define resolveTrackIndex resolveTrackIndexLoopEdit
#include "../../src/LoopEditManager.cpp"
#undef resolveTrackIndex
#include "../../src/TrackManager/TrackManagerInternalHelpers.cpp"
#include "../../src/EditManager/NoteEditSessionLifecycle.cpp"
#include "../../src/EditManager/EditSessionDepart.cpp"
#include "../../src/EditManager/NoteEditDisplayProjection.cpp"
#include "../../src/EditManager/NoteEditSessionCommit.cpp"
#include "../../src/EditManager/NoteEditCommitColdHelpers.cpp"
#include "../../src/EditManager/NoteEditSelection.cpp"
#include "../../src/EditManager/NoteEditSelectionPreparation.cpp"
#include "../../src/EditManager/EditNoteStateCoordinator.cpp"
#include "../../src/EditManager/NoteEditCurrentState.cpp"
#include "../../src/EditManager/NoteEditSessionUndo.cpp"
#include "../../src/EditManager/NoteEditSessionUndoStack.cpp"
#include "../../src/EditManager/NoteEditSessionPayloadRetirement.cpp"
#include "../../src/EditManager/NoteEditDerivedStateRetirement.cpp"
#include "../../src/EditManager/EditSessionStoreInvariant.cpp"
#include "../../src/EditManager/EditSessionLiveStoreSpan.cpp"
#include "../../src/EditManager/ParticipatingNoteSession.cpp"
#include "../../src/EditManager/ApplyOwnedEditPassRows.cpp"
#include "../../src/EditManager/ApplyEditSessionActions.cpp"
// The real Select virtual button method references the real geometry action builder.
// Rename only its private helper to avoid a concatenated-TU collision.
#define baselineSpansEqual baselineSpansEqualActionBuilder
#include "../../src/EditManager/EditSessionActionBuilder.cpp"
#undef baselineSpansEqual
#include "../../src/EditManager/NoteEditFocusBaseline.cpp"
#include "../../src/EditManager/NoteEditFocusRebuild.cpp"
#include "../../src/EditManager/NoteEditFocusState.cpp"
#include "../../src/EditManager/NoteEditFocusLinearSpan.cpp"
#include "../../src/EditManager/NoteEditFocusOverlap.cpp"
#include "../../src/EditManager/NoteEditFocusPreCommit.cpp"
#include "../../src/EditManager/NoteEditFocusDisplayProjection.cpp"
#include "../../src/EditManager/NoteEditPairResolve.cpp"
#include "../../src/EditManager/NoteEditWrapHeadAdjust.cpp"
#include "../../src/EditManager/NoteEditGeometryOps.cpp"
#include "../../src/EditManager/NoteEditGeometryApply.cpp"
#include "../../src/EditManager/NoteEditGeometryApplyMutate.cpp"
#include "../../src/EditManager/NoteEditGeometryApplyInternalHelpers.cpp"
#include "../../src/EditManager/NoteGeometryResolver.cpp"
#include "../../src/EditManager/ResolveConstrainedGeometry.cpp"
// Arduino platform boundary needed by the real Select motor conversion, not domain logic.
template <typename T> T constrain(T value, T low, T high) {
  return std::max(low, std::min(value, high));
}
// The real Select FSM is part of the entry contract; other states remain named boundaries.
#include "../../src/EditStates/EditSelectNoteState.cpp"
#include "../../src/TrackManager/TrackManagerSlotMatrix.cpp"
// The slot-matrix focus route reaches the capture-queue finalization step, so the real TrackManager
// capture-queue owner is linked rather than stubbed: a slot move must not be able to skip the
// in-flight-capture decision.
#include "../../src/TrackManager/TrackManagerCaptureQueue.cpp"
#include "../../src/Loop/LoopPendingNoteChange.cpp"
#include "../../src/Utils/LoopTickNormalize.cpp"
#include "../../src/Utils/SelectNavigation.cpp"
#include "../../src/MidiButtonActions.cpp"
// The real undo owner: the session's undo/redo entries reach it (`NoteEditSessionUndo.cpp`).
#include "../../src/TrackUndo.cpp"
#include "../../src/Track/TrackPlaybackHotPath.cpp"
// --- S7.5b.8a's subject: the real button entry and the real session/feedback seam. ---
#include "../../src/ControlSurface/EditEventFeedback.cpp"
#include "../../src/ControlSurface/NoteEditButtonInput.cpp"
#include "../../src/StorageManager/SlotReset.cpp"
/// `Track`'s own friend (`include/Track.h`): test-side access to the slot's playback runtime so the
/// fixtures can read the production active-note ledger. Must be declared at namespace scope.
struct TrackPlaybackTestAccess {
  static LoopPlaybackRuntime* runtime(Track& track, uint8_t slot) {
    return track.playbackRuntime.slotIfAllocated(slot);
  }
  static bool ledgerHolds(LoopPlaybackRuntime& runtime, NoteId noteId) {
    bool held = false;
    runtime.ledger.forEachActive([&](uint8_t, uint8_t, const ActiveNoteLedger::Entry& entry) {
      held = held || entry.noteId == noteId;
    });
    return held;
  }
};

namespace {
constexpr uint8_t kChannel = 1;
/// 2 bars: the **short** shape.
constexpr uint32_t kShortLoopTicks = 2u * 768u;
/// 64 bars: the **long** shape (the shape whose entry is content-sized).
constexpr uint32_t kLongLoopTicks = 64u * 768u;

/// The `(tick, pitch)` of the n-th committed note pair this suite records. The fixture's own content is
/// the independent expectation: the session's projection and the served MIDI must both equal it.
uint32_t committedOnTick(uint32_t lengthTicks, unsigned noteCount, unsigned index) {
  return (lengthTicks / (noteCount + 1u)) * (index + 1u);
}
uint8_t committedPitch(unsigned index, uint8_t firstPitch = 60) {
  return static_cast<uint8_t>(firstPitch + index);
}
struct SlotAccess {
  static void select(Track& track, uint8_t slot) { track.setActiveLoopIndex(slot); }
  /// The production *focus* route for a slot: the identity `EditManager` publishes is the selected
  /// slot's, so a fixture that commits into a slot selects it too (the same call the slot-matrix button
  /// handlers make). `SyncPlayback::No` leaves the active slot to the fixture.
  static void focus(uint8_t trackIndex, uint8_t slot) {
    trackManager.setSelectedSlotIndex(trackIndex, slot, SyncPlayback::No);
  }
};

void setUpHarness() {
  nativePlaybackDeadlineClock = {};
  nativeTransportRunning = true;
  g_nativeSilenceTrackCalls = 0;
  resetSlotChangeObservation();
  resetOutboundObservation();
  MidiSendCapture::reset();
  // Fixture isolation: `editManager` is a global and the loop storage survives between fixtures, so an
  // earlier fixture's session can still be open. Close it through the production exit and reset the
  // Reset the real FSM before recycling Track storage; other four entries are
  // named boundaries, while Select entry now participates in this oracle.
  if (editManager.isNoteEditActive()) {
    editManager.closeNoteEditSession(trackManager.getSelectedTrack());
  }
  editManager.setState(nullptr, trackManager.getSelectedTrack());
  editManager.resetNoteEditSessionState();
  trackManager.~TrackManager();
  LoopEventStore::resetPoolForTests();
  new (&trackManager) TrackManager();
  // Production registers the control surface as the edit-event listener (`src/main.cpp`); without it no
  // session-opened event reaches the feedback seam and the entry oracle would be vacuous.
  editManager.setEditEventListener(&controlSurfaceManager);
}

/// Commit one record pass of `lengthTicks` with `noteCount` paired notes into `trackIndex`'s `slot` -- the
/// content a restored library has -- and select that slot through the production focus route. No published
/// root is installed: this is the **rootless** shape.
void commitLoop(uint8_t trackIndex, uint8_t slot, uint32_t lengthTicks, unsigned noteCount,
                uint8_t firstPitch = 60) {
  Track& track = trackManager.getTrack(trackIndex);
  track.ensureLoopsAllocated();
  SlotAccess::select(track, slot);
  SlotAccess::focus(trackIndex, slot);
  Loop& loop = track.getLoop(slot);
  loop.loopLengthTicks = lengthTicks;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE_MESSAGE(loop.captureActive(), "the slot opens a record pass");
  for (unsigned i = 0; i < noteCount; ++i) {
    const uint32_t onTick = committedOnTick(lengthTicks, noteCount, i);
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(
        MidiEvent::NoteOn(onTick, kChannel, committedPitch(i, firstPitch), 100)));
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(
        MidiEvent::NoteOff(onTick + 48u, kChannel, committedPitch(i, firstPitch), 0)));
  }
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(lengthTicks, CommitReason::RecordStop));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass());
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
}

/// A committed take with **no notes** (controller-only content): the shape whose selection is empty even
/// though the slot is committed.
void commitControllerOnlyLoop(uint8_t trackIndex, uint8_t slot, uint32_t lengthTicks) {
  Track& track = trackManager.getTrack(trackIndex);
  track.ensureLoopsAllocated();
  SlotAccess::select(track, slot);
  SlotAccess::focus(trackIndex, slot);
  Loop& loop = track.getLoop(slot);
  loop.loopLengthTicks = lengthTicks;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::ControlChange(96u, kChannel, 7u, 64u)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(lengthTicks, CommitReason::RecordStop));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass());
}

/// The slot's working set admitted. The track is left **stopped**: a stopped slot's committed content is
/// the shape whose selectable inventory the entry can build (`Track::getVisualNotesForSlot` builds the
/// loop's visual cache only while the track is not playing).
void prewarmSlot(Track& track, uint8_t slot, uint32_t playhead) {
  clockManager.assignCurrentTickSilently(playhead);
  TEST_ASSERT_TRUE_MESSAGE(track.prewarmPlaybackForSlot(slot, true),
                           "the slot's working set is admitted");
}

/// The sounding shape: the transport playing and the slot's working set admitted (the current-root
/// fixture's shape, where a published root provides the playback and the selectable inventory).
void readySlotPlaying(Track& track, uint8_t slot, uint32_t playhead) {
  clockManager.assignCurrentTickSilently(playhead);
  track.forceSetState(TRACK_PLAYING);
  TEST_ASSERT_TRUE_MESSAGE(track.prewarmPlaybackForSlot(slot, true),
                           "the slot's working set is admitted");
}

/// Install a **current** published root for the slot: the `current-root` source shape, in which
/// `openNoteEditSession` does not rematerialize the whole loop (DEC-055 / S7.5b.2's committed base).
/// `bars` note pairs are installed, all at `pitch`, On at each bar start and Off 48 ticks later, with the
/// note identity `2000 + bar`.
void publishCommittedRootForSlot(Loop& loop, uint32_t bars, uint8_t pitch = 48) {
  auto empty =
      std::make_shared<const LoopSnapshot>(LoopSnapshot(loop.loopId, loop.loopLengthTicks, 0, 8, 8));
  LoopSnapshotChangeSet changes;
  for (uint32_t bar = 0; bar < bars; ++bar) {
    SnapshotNoteChange add{};
    add.kind = SnapshotChangeKind::Add;
    add.next.loopId = loop.loopId;
    add.next.noteId = static_cast<NoteId>(2000 + bar);
    add.next.pitch = pitch;
    add.next.velocity = 100;
    add.next.startTick = bar * 768u;
    add.next.endTick = bar * 768u + 48u;
    changes.notes.push_back(add);
    SnapshotEventChange on{};
    on.kind = SnapshotChangeKind::Add;
    on.next.loopId = loop.loopId;
    on.next.eventId = static_cast<EventId>(4000 + bar * 2u);
    on.next.canonicalOrder = bar * 2u;
    on.next.value = MidiEvent::NoteOn(bar * 768u, kChannel, pitch, 100);
    on.next.value.noteId = add.next.noteId;
    changes.events.push_back(on);
    SnapshotEventChange off{};
    off.kind = SnapshotChangeKind::Add;
    off.next.loopId = loop.loopId;
    off.next.eventId = static_cast<EventId>(4001 + bar * 2u);
    off.next.canonicalOrder = bar * 2u + 1u;
    off.next.value = MidiEvent::NoteOff(bar * 768u + 48u, kChannel, pitch, 0);
    off.next.value.noteId = add.next.noteId;
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

/// The production entry the momentary edit-mode gesture performs.
void pressEditModeButton() { midiButtonActions.handleCycleEditMode(); }
void finishLiveEntry() {
  for (unsigned i = 0; editManager.hasPendingCurrentRootNoteEditEntry() && i < 100000; ++i) {
    midiHandler.handleMidiInput();
    editManager.stepCurrentRootNoteEditEntry();
    midiHandler.handleMidiInput();
  }
  TEST_ASSERT_FALSE(editManager.hasPendingCurrentRootNoteEditEntry());
}

/// The session's own projection: `store.readEvents()` (the const overload), the content the session
/// serves. The non-const overload returns the mutable store the edit ops write.
const MidiEventVec& sessionProjection() {
  const EditManager& session = editManager;
  return session.sessionMidiEvents();
}

void playCommittedWindow(Track& track, uint32_t firstTick, uint32_t lastTick) {
  track.reanchorPlaybackProjection(firstTick, false);
  track.clearSentPlaybackMidiForTest();
  MidiSendCapture::reset();
  for (uint32_t tick = firstTick; tick <= lastTick; tick += 64u) {
    track.playMidiEvents(tick, true);
  }
}

}  // namespace

void setUp() { setUpHarness(); }

void tearDown() {}

// ---------------------------------------------------------------------------------------------------
// Family 1 -- the baseline oracle (green today): entry publishes one identity-qualified session, keeps
// the committed source exact and requests the session-open feedback exactly once.
// ---------------------------------------------------------------------------------------------------

/// The entry publishes a **complete, identity-qualified** session on a long current-root loop, and the
/// committed interval keeps sounding exactly through the production Track MIDI edge and its ledger
/// (DEC-055 / S7.5b.2's committed base).
static void test_button_entry_publishes_an_identity_qualified_session_on_a_current_root_source() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  commitLoop(0, 0, kLongLoopTicks, 6);
  readySlotPlaying(track, 0, 0);
  Loop& loop = track.getLoop(0);
  loop.rebuildVisualCacheFromPasses();
  publishCommittedRootForSlot(loop, 64);

  pressEditModeButton();

  TEST_ASSERT_TRUE_MESSAGE(editManager.isNoteEditActive(), "the button entry opens the note-edit session");
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(EditSessionType::Note),
                            static_cast<int>(editManager.getEditSessionType()),
                            "the cycled session type is NOTE_EDIT");
  const NoteEditSessionState& state = editManager.getNoteEditSessionState();
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(state.selection.trackId),
                                   "the published selection carries the selected track identity");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(loop.loopId),
                                   static_cast<uint32_t>(state.selection.loopId),
                                   "the published selection carries the selected loop identity");
  TEST_ASSERT_TRUE_MESSAGE(state.selection.primaryNote != kInvalidNoteId,
                           "the current-root open selects a committed identity at the bracket");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, static_cast<uint32_t>(state.selection.selectedNotes.size()),
                                   "exactly one identity is primary");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(state.selection.primaryNote),
                                   static_cast<uint32_t>(state.selection.selectedNotes[0]),
                                   "the primary identity is the selected one");
  TEST_ASSERT_TRUE(editManager.getSelectedNoteIdx() >= 0);
  TEST_ASSERT_TRUE_MESSAGE(sessionProjection().empty(),
                           "a current-root open begins with an empty edit delta");

  // The committed identity is in the active-note ledger while it sounds and is released at its Off: the
  // closure half of "unchanged committed soundings". The On is due at tick 0, its Off 48 ticks later.
  LoopPlaybackRuntime* runtime = TrackPlaybackTestAccess::runtime(track, 0);
  TEST_ASSERT_NOT_NULL(runtime);
  track.reanchorPlaybackProjection(0, false);
  track.clearSentPlaybackMidiForTest();
  MidiSendCapture::reset();
  track.playMidiEvents(16u, true);
  TEST_ASSERT_TRUE_MESSAGE(TrackPlaybackTestAccess::ledgerHolds(*runtime, 2000),
                           "the committed identity is in the ledger while it sounds");
  track.playMidiEvents(48u, true);
  TEST_ASSERT_FALSE_MESSAGE(TrackPlaybackTestAccess::ledgerHolds(*runtime, 2000),
                            "the committed identity's closure is exact");

  // The committed interval still sounds, exactly: the root's own content -- On at each bar start, Off 48
  // ticks later, pitch 48, velocity 100 -- read off the production edge, and the Track send sink agrees
  // with the handler boundary.
  playCommittedWindow(track, 64u, 2u * 768u);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(4u, static_cast<uint32_t>(track.sentPlaybackMidiCountForTest()),
                                   "two committed bars sound as two On/Off pairs");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(track.sentPlaybackMidiCountForTest()),
                                   static_cast<uint32_t>(MidiSendCapture::count()),
                                   "the handler boundary agrees with the Track send sink");
  const uint32_t expectedTicks[4] = {0u, 48u, 768u, 816u};
  for (size_t i = 0; i < 4; ++i) {
    const MidiEvent& event = track.sentPlaybackMidiAtForTest(i).event;
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(expectedTicks[i], event.tick,
                                     "the committed row keeps its exact tick");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(48u, event.data.noteData.note,
                                    "only the committed root's identity sounds");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(i % 2u == 0u ? 100u : 0u, event.data.noteData.velocity,
                                    "the committed row keeps its velocity");
    TEST_ASSERT_EQUAL_MESSAGE(i % 2u == 0u ? 1 : 0, event.isNoteOn() ? 1 : 0,
                              "each committed pair is On then Off");
  }

  // The session-open feedback is requested exactly once, and the plan is the normative SessionOpen plan
  // (F1 bracket, F1 echo wait, F2 coarse, F3 fine, F4 note value) -- pinned as literals, not read back
  // from the same helper the production request uses.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, outboundObservation().requestCalls,
                                   "the entry requests one feedback unit, not a send burst");
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(NoteEditFaderOutbound::Trigger::SessionOpen),
                            static_cast<int>(outboundObservation().lastTrigger),
                            "the requested unit is the session-open feedback");
  const NoteEditFaderOutbound::PlanFlags& planned = outboundObservation().lastPlan;
  TEST_ASSERT_TRUE_MESSAGE(planned.fader1, "the session-open plan includes the F1 bracket");
  TEST_ASSERT_TRUE_MESSAGE(planned.waitFader1Echo, "the session-open plan waits for the F1 echo");
  TEST_ASSERT_TRUE_MESSAGE(planned.coarse, "the session-open plan includes the F2 coarse stage");
  TEST_ASSERT_TRUE_MESSAGE(planned.fine, "the session-open plan includes the F3 fine stage");
  TEST_ASSERT_TRUE_MESSAGE(planned.noteValue, "the session-open plan includes the F4 note-value stage");
}

/// The entry on a **rootless / unpublished-root source** (`Loop::isPlaybackSessionExitPublicationGap` is
/// true: the committed take's session-exit snapshot is still queued). The entry must accept the gesture,
/// project the committed content exactly, keep the committed content as the audible source and request
/// exactly one feedback unit -- and it must **not** publish a partial selection: the selectable inventory
/// comes from the loop's visual cache, which the full rebuild deliberately skips while that publication
/// gap is open (S7.5b.6 — display frame continuity), so "no note is selected" is the complete-ready
/// answer here and not a fabricated identity.
static void test_button_entry_on_a_rootless_source_keeps_the_audible_source_and_no_partial_selection() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  constexpr unsigned kNotes = 4;
  commitLoop(0, 0, kShortLoopTicks, kNotes);
  prewarmSlot(track, 0, 0);
  Loop& loop = track.getLoop(0);
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_TRUE_MESSAGE(loop.isPlaybackSessionExitPublicationGap(),
                           "the rootless shape is the open publication gap");
  TEST_ASSERT_TRUE_MESSAGE(loop.visualCache.notes.empty(),
                           "no visual cache exists yet: the inventory is not ready before the entry");

  pressEditModeButton();

  TEST_ASSERT_TRUE(editManager.isNoteEditActive());
  TEST_ASSERT_TRUE_MESSAGE(loop.visualCache.notes.empty(),
                           "the entry's rebuild attempt is suppressed while the gap is open");
  const NoteEditSessionState& state = editManager.getNoteEditSessionState();
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(loop.loopId),
                           static_cast<uint32_t>(state.selection.loopId));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(0u), static_cast<uint32_t>(state.selection.trackId));
  TEST_ASSERT_TRUE_MESSAGE(state.selection.primaryNote == kInvalidNoteId,
                           "an unready inventory publishes no partial selection identity");
  TEST_ASSERT_TRUE(state.selection.selectedNotes.empty());
  TEST_ASSERT_EQUAL_MESSAGE(-1, editManager.getSelectedNoteIdx(),
                            "no note identity is presented as ready");
  // The rematerialized projection is exactly the committed content: both halves per note pair.
  const MidiEventVec& projection = sessionProjection();
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(2u * kNotes),
                                   static_cast<uint32_t>(projection.size()),
                                   "the session's projection is the committed content");
  for (unsigned i = 0; i < kNotes; ++i) {
    const uint32_t onTick = committedOnTick(kShortLoopTicks, kNotes, i);
    const uint8_t pitch = committedPitch(i);
    bool sawOn = false;
    bool sawOff = false;
    for (const MidiEvent& event : projection) {
      if (event.data.noteData.note != pitch) continue;
      if (event.isNoteOn() && event.tick == onTick) sawOn = true;
      if (event.isNoteOff() && event.tick == onTick + 48u) sawOff = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(sawOn, "the projection carries the committed note-on");
    TEST_ASSERT_TRUE_MESSAGE(sawOff, "the projection carries the committed note-off");
  }
  // The committed content is still the audible source, and the entry silences nothing.
  TEST_ASSERT_TRUE_MESSAGE(track.hasAudiblePlaybackSource(0),
                           "the committed content stays the audible source");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, outboundObservation().requestCalls,
                                   "one feedback unit for one session open");
}

/// Dense content on the same stale/unpublished-root source: the exactness above is not a small-content
/// artefact, and the "no partial selection" answer does not depend on content size either.
static void test_button_entry_on_dense_content_keeps_the_exact_committed_inventory() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  constexpr unsigned kNotes = 32;
  commitLoop(0, 0, kShortLoopTicks, kNotes);
  prewarmSlot(track, 0, 0);

  pressEditModeButton();

  TEST_ASSERT_TRUE(editManager.isNoteEditActive());
  const MidiEventVec& projection = sessionProjection();
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(2u * kNotes),
                                   static_cast<uint32_t>(projection.size()),
                                   "dense content is projected exactly, both halves per pair");
  uint32_t onRows = 0;
  uint32_t offRows = 0;
  for (const MidiEvent& event : projection) {
    if (event.isNoteOn()) ++onRows;
    if (event.isNoteOff()) ++offRows;
  }
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(kNotes), onRows);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(kNotes), offRows);
  TEST_ASSERT_TRUE(editManager.getNoteEditSessionState().selection.selectedNotes.empty());
  TEST_ASSERT_EQUAL_MESSAGE(-1, editManager.getSelectedNoteIdx(),
                            "dense content does not fabricate a selection either");
  (void)track;
}

/// Empty selection: the entry still publishes a coherent session and requests the session-open feedback
/// exactly once; no selection-dependent unit is invented for an empty pick.
static void test_button_entry_with_an_empty_selection_publishes_no_selection() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  commitControllerOnlyLoop(0, 0, kShortLoopTicks);
  readySlotPlaying(track, 0, 0);

  pressEditModeButton();

  TEST_ASSERT_TRUE_MESSAGE(editManager.isNoteEditActive(),
                           "an empty slot still opens a coherent session");
  TEST_ASSERT_EQUAL_MESSAGE(-1, editManager.getSelectedNoteIdx(), "no note is selected");
  const NoteEditSessionState& state = editManager.getNoteEditSessionState();
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(kInvalidNoteId),
                           static_cast<uint32_t>(state.selection.primaryNote));
  TEST_ASSERT_TRUE(state.selection.selectedNotes.empty());
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, outboundObservation().requestCalls,
                                   "the session-open feedback is requested once");
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(NoteEditFaderOutbound::Trigger::SessionOpen),
                            static_cast<int>(outboundObservation().lastTrigger),
                            "the requested unit is the session-open feedback");
}

/// A second press returns to LOOP_EDIT, closes the note-edit identity and releases the audition: the
/// entry is a bounded toggle that invents no further session-open unit.
static void test_button_entry_second_press_returns_to_loop_edit_and_releases_the_session() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  commitLoop(0, 0, kShortLoopTicks, 4);
  prewarmSlot(track, 0, 0);

  pressEditModeButton();
  TEST_ASSERT_TRUE(editManager.isNoteEditActive());
  const unsigned requestsAfterOpen = outboundObservation().requestCalls;
  TEST_ASSERT_EQUAL_UINT32(1u, requestsAfterOpen);
  const unsigned releasesBefore = outboundObservation().auditionReleases;

  pressEditModeButton();

  TEST_ASSERT_FALSE_MESSAGE(editManager.isNoteEditActive(), "the second press leaves NOTE_EDIT");
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(EditSessionType::Loop),
                            static_cast<int>(editManager.getEditSessionType()),
                            "the cycled session type is LOOP_EDIT");
  TEST_ASSERT_EQUAL_MESSAGE(-1, editManager.getSelectedNoteIdx(), "the selection identity is cleared");
  TEST_ASSERT_TRUE_MESSAGE(outboundObservation().auditionReleases > releasesBefore,
                           "leaving NOTE_EDIT releases the audition through the production seam");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(requestsAfterOpen, outboundObservation().requestCalls,
                                   "the closing press invents no further session-open unit");
}

/// Exit/reopen keeps exactly one live identity: the reopen publishes a fresh session identity and requests
/// its own feedback, and the closed session cannot publish afterwards.
static void test_session_exit_and_reopen_publishes_exactly_one_fresh_identity() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  constexpr unsigned kNotes = 4;
  commitLoop(0, 0, kShortLoopTicks, kNotes);
  prewarmSlot(track, 0, 0);

  pressEditModeButton();  // open
  TEST_ASSERT_TRUE(editManager.isNoteEditActive());

  pressEditModeButton();  // back to LOOP_EDIT
  TEST_ASSERT_FALSE(editManager.isNoteEditActive());

  pressEditModeButton();  // reopen
  TEST_ASSERT_TRUE(editManager.isNoteEditActive());
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(EditSessionType::Note),
                            static_cast<int>(editManager.getEditSessionType()),
                            "the reopen publishes NOTE_EDIT again");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(track.getLoop(0).loopId),
                                   static_cast<uint32_t>(
                                       editManager.getNoteEditSessionState().selection.loopId),
                                   "the reopened identity is the current slot's, not the departed one's");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u, outboundObservation().requestCalls,
                                   "each session open owns exactly one feedback unit");
  // The reopen composes the committed base with the pass the exit baked, so the fixture pins that every
  // committed identity is still served instead of a row count (exact entry content is pinned above).
  const MidiEventVec& reopened = sessionProjection();
  TEST_ASSERT_FALSE_MESSAGE(reopened.empty(),
                            "the reopened session serves the committed content, not a stale delta");
  for (unsigned i = 0; i < kNotes; ++i) {
    bool present = false;
    for (const MidiEvent& event : reopened) {
      present = present || event.data.noteData.note == committedPitch(i);
    }
    TEST_ASSERT_TRUE_MESSAGE(present, "the reopened projection carries every committed identity");
  }
}

/// A **track** change between units revalidates by identity: the new target's session is published (with
/// the new track's own slot identity and content) and its own feedback requested, and the departed
/// target's identity is not republished. The new target is slot 1, so the published slot identity
/// discriminates the two targets (`LoopId` is the slot index) as well as the content does.
static void test_focus_change_between_units_revalidates_the_session_identity() {
  LoopEventStore::initPool();
  Track& track1 = trackManager.getTrack(1);
  commitLoop(0, 0, kShortLoopTicks, 4, 60);
  commitLoop(1, 1, kShortLoopTicks, 4, 70);
  prewarmSlot(trackManager.getTrack(0), 0, 0);
  SlotAccess::focus(0, 0);

  pressEditModeButton();
  TEST_ASSERT_TRUE(editManager.isNoteEditActive());
  TEST_ASSERT_EQUAL_UINT32(1u, outboundObservation().requestCalls);

  trackManager.setSelectedTrack(1);

  const NoteEditSessionState& state = editManager.getNoteEditSessionState();
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, static_cast<uint32_t>(state.selection.trackId),
                                   "the published selection follows the new focus");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(track1.getLoop(1).loopId),
                                   static_cast<uint32_t>(state.selection.loopId),
                                   "the published identity is the new target's slot, not the departed one's");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u, outboundObservation().requestCalls,
                                   "exactly one feedback unit for the re-entered focus");
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(NoteEditFaderOutbound::Trigger::SessionOpen),
                            static_cast<int>(outboundObservation().lastTrigger),
                            "the focus-change unit is the session-open feedback, not a burst");
  const MidiEventVec& projection = sessionProjection();
  TEST_ASSERT_FALSE_MESSAGE(projection.empty(), "the re-entered session has the new slot's content");
  for (const MidiEvent& event : projection) {
    TEST_ASSERT_TRUE_MESSAGE(event.data.noteData.note >= 70u && event.data.noteData.note <= 73u,
                             "the re-entered session serves the new slot's identities only");
  }
}

/// A **slot** change between units on the same track revalidates the same way: the session follows the
/// new slot's identity and content, and owns exactly one further feedback unit.
static void test_slot_change_between_units_revalidates_the_session_identity() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  commitLoop(0, 0, kShortLoopTicks, 4, 60);
  commitLoop(0, 1, kShortLoopTicks, 4, 80);
  prewarmSlot(track, 0, 0);
  SlotAccess::focus(0, 0);

  pressEditModeButton();
  TEST_ASSERT_TRUE(editManager.isNoteEditActive());
  TEST_ASSERT_EQUAL_UINT32(1u, outboundObservation().requestCalls);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(track.getLoop(0).loopId),
                           static_cast<uint32_t>(
                               editManager.getNoteEditSessionState().selection.loopId));

  SlotAccess::focus(0, 1);  // the production slot-focus route (slot-matrix selection)

  const NoteEditSessionState& state = editManager.getNoteEditSessionState();
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(track.getLoop(1).loopId),
                                   static_cast<uint32_t>(state.selection.loopId),
                                   "the session follows the newly focused slot");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u, outboundObservation().requestCalls,
                                   "exactly one feedback unit for the re-entered slot");
  const MidiEventVec& projection = sessionProjection();
  TEST_ASSERT_FALSE_MESSAGE(projection.empty(), "the re-entered session has the new slot's content");
  bool sawNewSlotPitch = false;
  for (const MidiEvent& event : projection) {
    TEST_ASSERT_TRUE_MESSAGE(event.data.noteData.note >= 80u && event.data.noteData.note <= 83u,
                             "the re-entered session serves the new slot's identities only");
    sawNewSlotPitch = sawNewSlotPitch || event.data.noteData.note == 80u;
  }
  TEST_ASSERT_TRUE(sawNewSlotPitch);
}

/// A session **mutation** between units advances the session's preview revision and keeps the published
/// identity and the feedback accounting exactly as they were: the mutation edge does not re-open the
/// session and does not invent a session-open unit.
static void test_mutation_between_units_keeps_the_identity_and_requests_no_feedback() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  commitLoop(0, 0, kShortLoopTicks, 4);
  readySlotPlaying(track, 0, 0);

  pressEditModeButton();
  TEST_ASSERT_TRUE(editManager.isNoteEditActive());
  const NoteEditSessionState before = editManager.getNoteEditSessionState();
  const uint32_t revisionBefore = editManager.sessionPlaybackPreviewRevision();
  TEST_ASSERT_EQUAL_UINT32(1u, outboundObservation().requestCalls);

  // A **paired, identified** session edit through the production mutation edge.
  MidiEvent on = MidiEvent::NoteOn(100u, kChannel, 90, 100);
  on.noteId = static_cast<NoteId>(9001);
  editManager.sessionMidiEvents().push_back(on);
  editManager.sessionMidiEvents().push_back(MidiEvent::NoteOff(148u, kChannel, 90, 0));
  track.invalidateCaches(true);                            // production: arms the deferred refresh
  editManager.flushDeferredNoteEditDisplayRefresh(track);   // production: the flush edge

  TEST_ASSERT_EQUAL_UINT32_MESSAGE(revisionBefore + 1u, editManager.sessionPlaybackPreviewRevision(),
                                   "a mutation advances the preview revision once");
  const NoteEditSessionState after = editManager.getNoteEditSessionState();
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(before.selection.trackId),
                           static_cast<uint32_t>(after.selection.trackId));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(before.selection.loopId),
                           static_cast<uint32_t>(after.selection.loopId));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(before.selection.primaryNote),
                           static_cast<uint32_t>(after.selection.primaryNote));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, outboundObservation().requestCalls,
                                   "a mutation does not re-request the session-open feedback");
}

/// A **workspace reload** between units rebinds the session to the loaded content without inventing a
/// feedback unit and without resurrecting the pre-load identity.
static void test_workspace_reload_between_units_rebinds_the_session_without_feedback() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  commitLoop(0, 0, kShortLoopTicks, 4, 60);
  prewarmSlot(track, 0, 0);

  pressEditModeButton();
  TEST_ASSERT_TRUE(editManager.isNoteEditActive());
  TEST_ASSERT_EQUAL_UINT32(1u, outboundObservation().requestCalls);

  // The load route's own re-entry (`EditManager::rematerializeNoteEditSessionAfterWorkspaceReload`).
  editManager.rematerializeNoteEditSessionAfterWorkspaceReload(track);

  TEST_ASSERT_TRUE(editManager.isNoteEditActive());
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(track.getLoop(0).loopId),
                                   static_cast<uint32_t>(
                                       editManager.getNoteEditSessionState().selection.loopId),
                                   "the session is rebound to the loaded slot identity");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, outboundObservation().requestCalls,
                                   "the reload path owns no session-open feedback unit");
  const MidiEventVec& projection = sessionProjection();
  TEST_ASSERT_FALSE_MESSAGE(projection.empty(), "the rebind serves the loaded content");
  for (const MidiEvent& event : projection) {
    TEST_ASSERT_TRUE_MESSAGE(event.data.noteData.note >= 60u && event.data.noteData.note <= 63u,
                             "the rebind serves the loaded slot's identities only");
  }
}

/// Clear between units invalidates the pending identities: the production clear entry closes the session
/// and issues no further feedback unit.
static void test_clear_between_units_invalidates_the_pending_session_identities() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  commitLoop(0, 0, kShortLoopTicks, 4);
  prewarmSlot(track, 0, 0);

  pressEditModeButton();
  TEST_ASSERT_TRUE(editManager.isNoteEditActive());
  const unsigned requestsAfterOpen = outboundObservation().requestCalls;
  TEST_ASSERT_EQUAL_UINT32(1u, requestsAfterOpen);

  editManager.revertNoteEditSessionForLoopClear(track);

  TEST_ASSERT_FALSE_MESSAGE(editManager.isNoteEditActive(),
                            "the clear invalidates the open session identity");
  TEST_ASSERT_EQUAL_MESSAGE(-1, editManager.getSelectedNoteIdx(), "no note identity is published");
  TEST_ASSERT_TRUE_MESSAGE(editManager.getNoteEditSessionState().selection.selectedNotes.empty(),
                           "no selected identity survives the clear");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(requestsAfterOpen, outboundObservation().requestCalls,
                                   "the cleared session issues no further feedback unit");
}

/// A competing **sounding** track keeps its exact content while the entry runs on another track: the
/// entry does not silence, rebuild or re-target the other track's playback, and issues no feedback for it.
static void test_entry_while_a_competing_track_plays_keeps_both_tracks_exact() {
  LoopEventStore::initPool();
  Track& entryTrack = trackManager.getTrack(0);
  Track& competingTrack = trackManager.getTrack(1);
  commitLoop(0, 0, kShortLoopTicks, 4, 60);
  commitLoop(1, 0, kShortLoopTicks, 4, 60);
  // Both slots sound from their own current root, with **different** content identities (pitch 48 for the
  // entry track, 50 for the competing one), so a cross-track publication is visible in the output.
  entryTrack.getLoop(0).rebuildVisualCacheFromPasses();
  competingTrack.getLoop(0).rebuildVisualCacheFromPasses();
  publishCommittedRootForSlot(entryTrack.getLoop(0), 2, 48);
  publishCommittedRootForSlot(competingTrack.getLoop(0), 2, 50);
  readySlotPlaying(entryTrack, 0, 0);
  readySlotPlaying(competingTrack, 0, 0);

  pressEditModeButton();
  TEST_ASSERT_TRUE(editManager.isNoteEditActive());
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, outboundObservation().requestCalls,
                                   "the entry owns one session-open feedback unit");

  playCommittedWindow(competingTrack, 64u, 2u * 768u);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(4u, static_cast<uint32_t>(competingTrack.sentPlaybackMidiCountForTest()),
                                   "the competing track still sounds both of its committed bars");
  for (size_t i = 0; i < competingTrack.sentPlaybackMidiCountForTest(); ++i) {
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        50u, competingTrack.sentPlaybackMidiAtForTest(i).event.data.noteData.note,
        "only the competing track's own committed identity sounds");
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, outboundObservation().requestCalls,
                                   "another track's playback work issues no entry feedback");
}

// ---------------------------------------------------------------------------------------------------
// Family 2 -- the boundary oracle. RED today by construction; S7.5b.8b must turn it green.
// ---------------------------------------------------------------------------------------------------


/// **Documented missing-yield case.** The entry must return to MIDI input between admitted units: the
/// feedback preparation is one unit, the send pipeline belongs to the existing main-loop continuation,
/// and the entry must not poll that pipeline to idle inside its own callback. RED today:
/// `ControlSurfaceManager::handleSessionOpenedEvent` calls `drainFaderOutboundUntilIdle()` inside the
/// button callback, so the callback returns with the pipeline already polled and no input-service
/// boundary in between. The assertion is about the production **call site**; the drain's own body is a
/// declared boundary of this suite.
static void test_entry_returns_to_midi_input_between_admitted_units() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  commitLoop(0, 0, kLongLoopTicks, 24);
  prewarmSlot(track, 0, 0);

  pressEditModeButton();

  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      0u, outboundObservation().nestedDrainCalls,
      "the entry must not poll the send pipeline to idle inside the callback (no polls around work)");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, outboundObservation().requestCalls,
                                   "the entry's own unit is the single session-open feedback request");
}

#if defined(S75B8_ENTRY_BOUNDARY_ORACLE)
/// **Withheld-grant control.** With the owner's next turn withheld, the accepted gesture must stay
/// pending: acceptance is bounded metadata, and the content-sized preparation waits for its unit. RED
/// today: the whole entry (content rematerialize + focus build + feedback request) runs inside the button
/// callback, so the session is already complete when the callback returns.
static void test_entry_keeps_the_accepted_gesture_pending_when_the_owner_turn_is_withheld() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  commitLoop(0, 0, kLongLoopTicks, 24);
  prewarmSlot(track, 0, 0);

  pressEditModeButton();
  // No owner turn is granted between acceptance and this assertion.
  TEST_ASSERT_FALSE_MESSAGE(
      editManager.isNoteEditActive(),
      "acceptance is bounded metadata: a withheld owner turn must not already publish the session");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, outboundObservation().requestCalls,
                                   "no feedback unit is requested before its owner turn");
  TEST_ASSERT_TRUE_MESSAGE(track.hasAudiblePlaybackSource(0),
                           "the accepted interaction keeps the audible source pending, not silent");
}

#endif  // S75B8_ENTRY_BOUNDARY_ORACLE: pending-entry implementation remains open.

/// **Identity-rejection control.** A completion that carries an identity superseded *before* it ran must
/// not publish: only a complete session/slot/`NoteId`/source-qualified result becomes ready, and a late
/// completion cannot revive departed state. The fixture feeds the **production reopen entry** the
/// superseded track reference after the selection moved (the fixture-side control for "identity rejection
/// disabled"): the departed track's selected slot is 0 while the current target is track 1 slot 1, so a
/// mixed publication differs in both the slot identity and the content. RED today: the entry publishes the
/// new track id with the departed slot's loop and the departed slot's content.
static void test_entry_refuses_a_late_completion_for_a_superseded_track_reference() {
  LoopEventStore::initPool();
  Track& departed = trackManager.getTrack(0);
  Track& current = trackManager.getTrack(1);
  commitLoop(0, 0, kShortLoopTicks, 4, 60);   // the departed target: track 0, slot 0
  commitLoop(1, 1, kShortLoopTicks, 4, 70);   // the new target: track 1, slot 1
  prewarmSlot(departed, 0, 0);
  SlotAccess::focus(0, 0);

  pressEditModeButton();                 // the gesture that was accepted for track 0 / slot 0
  trackManager.setSelectedTrack(1);      // the target changes before the completion runs
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(
                                   editManager.getNoteEditSessionState().selection.trackId));

  const NoteId primaryBefore = editManager.getNoteEditSessionState().selection.primaryNote;
  const int selectedBefore = editManager.getSelectedNoteIdx();
  const uint32_t previewBefore = editManager.sessionPreviewRevision();
  const uint32_t playbackPreviewBefore = editManager.sessionPlaybackPreviewRevision();
  // A late completion that still carries the departed reference must be refused.
  editManager.reopenNoteEditSession(departed);

  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u, outboundObservation().requestCalls,
                                   "a rejected reopen must not issue another feedback request");
  TEST_ASSERT_EQUAL_UINT32(primaryBefore,
      editManager.getNoteEditSessionState().selection.primaryNote);
  TEST_ASSERT_EQUAL_INT(selectedBefore, editManager.getSelectedNoteIdx());
  TEST_ASSERT_EQUAL_UINT32(previewBefore, editManager.sessionPreviewRevision());
  TEST_ASSERT_EQUAL_UINT32(playbackPreviewBefore, editManager.sessionPlaybackPreviewRevision());
  const NoteEditSessionState& state = editManager.getNoteEditSessionState();
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      1u, static_cast<uint32_t>(state.selection.trackId),
      "a completion carrying a superseded identity must not publish for the departed target");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      static_cast<uint32_t>(current.getLoop(1).loopId), static_cast<uint32_t>(state.selection.loopId),
      "only the current target's slot identity may be published (no mixed track/slot pair)");
  const MidiEventVec& projection = sessionProjection();
  TEST_ASSERT_FALSE_MESSAGE(projection.empty(),
                            "the late completion published something for the departed target");
  for (const MidiEvent& event : projection) {
    TEST_ASSERT_TRUE_MESSAGE(event.data.noteData.note >= 70u && event.data.noteData.note <= 73u,
                             "the completion must not revive the departed slot's identities");
  }
}


#include "selection_preparation.inc"
#include "committed_id_preparation.inc"
#include "inventory_preparation.inc"
#include "select_state_handoff.inc"
#include "second_clamp_preparation.inc"
#include "departed_entry_guards.inc"

#include "derived_state_clear.inc"

// Real pending-note overlap and seal owner is linked here, unlike prewarm.
#define PENDING_BOX_WRAP_HARNESS 1
#include "../test_playback_prewarm/pending_box_release.inc"
#define INVENTORY_ADOPTION_METADATA_HARNESS 1
#include "inventory_adoption.inc"
#include "live_current_root_entry.inc"

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_live_repair_cycle_preview_tail);
  RUN_TEST(test_live_repair_cycle_feedback_tail);
  RUN_TEST(test_live_repair_short_preview_tail);
  RUN_TEST(test_live_repair_short_feedback_tail);
  RUN_TEST(test_live_repair_explicit_preview_tail);
  RUN_TEST(test_live_repair_explicit_feedback_tail);
  RUN_TEST(test_live_ready_root_zero_grant_acceptance);
  RUN_TEST(test_live_callback_successor_keeps_its_new_request_without_old_open);
  RUN_TEST(test_live_dedup_and_accepted_capture_input_supersede_without_obsolete_feedback);
  RUN_TEST(test_live_empty_ready_root_and_split_start_tie_inventory);
  RUN_TEST(test_live_compatibility_admission_is_explicit_and_nonmutating);
  RUN_TEST(test_live_real_storage_reset_before_source_reuse_and_geometry_cancel);
  RUN_TEST(test_live_populated_inactive_payload_and_derived_cleanup_precede_publication);
  RUN_TEST(test_live_real_publisher_second_clamp_final_landing_and_late_cancel);
  RUN_TEST(test_live_one_grant_private_selection_and_exact_completion);
  RUN_TEST(test_live_short_wrapper_and_pending_inputs_do_not_run_immediate_tails);
  RUN_TEST(test_live_pending_second_press_cancels_without_fake_session_events);
  RUN_TEST(test_live_every_prepublication_phase_target_slot_and_source_cancel);
  RUN_TEST(test_live_cleanup_reset_clear_reload_and_compatibility_supersede);
  RUN_TEST(test_live_obsolete_feedback_cancelled_after_publication_and_callback);
  RUN_TEST(test_live_dense_preparation_one_unit_and_peer_midi_unchanged);
  RUN_TEST(test_adoption_queries_header_grant_and_consumed_swap);
  RUN_TEST(test_adoption_real_borrowers_withhold_header_and_adoption);
  RUN_TEST(test_adoption_real_prewarm_history_staleness_preserves_grant);
  RUN_TEST(test_adoption_real_wrap_journal_refuses_without_mutation);
  RUN_TEST(test_adoption_delete_companion_literal_inventory_and_publisher_parity);
  RUN_TEST(test_adoption_both_retained_invalid_batches_refuse);
  RUN_TEST(test_adoption_partial_foreign_fault_and_stale_receipts);
  RUN_TEST(test_adoption_empty_invalid_mode_and_zero_geometry);
  RUN_TEST(test_adoption_loaded_root_refusal_republish_preserves_stale_root_and_parity);
  RUN_TEST(test_adoption_reservation_shortfall_exact_and_full_horizon);
  RUN_TEST(test_adoption_live_staged_undo_and_queued_destinations_refuse);
  RUN_TEST(test_adoption_retirement_progress_and_complete_are_never_fresh);
  RUN_TEST(test_adoption_imported_metadata_order_is_observation_only);
  RUN_TEST(test_pending_box_real_wrap_edges_shells_and_companion_rows);
  RUN_TEST(test_inventory_withheld_exact_grants_and_literal_ids);
  RUN_TEST(test_inventory_empty_and_existing_capacity_no_fabrication);
  RUN_TEST(test_inventory_nonempty_admission_and_discard_do_not_release_outputs);
  RUN_TEST(test_inventory_revision_and_completed_receipt_supersession_sticky);
  RUN_TEST(test_inventory_same_shape_reset_reseed_generation_rejects);
  RUN_TEST(test_inventory_foreign_loop_and_scalar_topology_guards);
  RUN_TEST(test_inventory_output_headers_sizes_and_storage_are_qualified);
  RUN_TEST(test_inventory_preserves_root_pending_chunks_and_legacy_parity);
  RUN_TEST(test_derived_state_clear_production_payload_baseline);
  RUN_TEST(test_derived_state_clear_withheld_exact_grants_and_preserved_source);
  RUN_TEST(test_derived_state_clear_admission_requires_completed_receipt_and_departed_fsm);
  RUN_TEST(test_derived_state_clear_refocus_and_slot_supersession_are_sticky);
  RUN_TEST(test_derived_state_clear_legacy_reset_reopen_rejects_old_cursor);
  RUN_TEST(test_derived_state_clear_empty_local_owner_finishes_without_materializing);
  // Family 1 -- the baseline oracle frozen by S7.5b.8a.
  RUN_TEST(test_button_entry_publishes_an_identity_qualified_session_on_a_current_root_source);
  RUN_TEST(test_button_entry_on_a_rootless_source_keeps_the_audible_source_and_no_partial_selection);
  RUN_TEST(test_button_entry_on_dense_content_keeps_the_exact_committed_inventory);
  RUN_TEST(test_button_entry_with_an_empty_selection_publishes_no_selection);
  RUN_TEST(test_button_entry_second_press_returns_to_loop_edit_and_releases_the_session);
  RUN_TEST(test_session_exit_and_reopen_publishes_exactly_one_fresh_identity);
  RUN_TEST(test_focus_change_between_units_revalidates_the_session_identity);
  RUN_TEST(test_slot_change_between_units_revalidates_the_session_identity);
  RUN_TEST(test_mutation_between_units_keeps_the_identity_and_requests_no_feedback);
  RUN_TEST(test_workspace_reload_between_units_rebinds_the_session_without_feedback);
  RUN_TEST(test_clear_between_units_invalidates_the_pending_session_identities);
  RUN_TEST(test_entry_while_a_competing_track_plays_keeps_both_tracks_exact);
  // Landed boundary predicates remain mandatory in the ordinary native suite.
  RUN_TEST(test_entry_returns_to_midi_input_between_admitted_units);
  RUN_TEST(test_entry_refuses_a_late_completion_for_a_superseded_track_reference);
  RUN_TEST(test_selection_preparation_withheld_units_keep_result_private_and_source_unpinned);
  RUN_TEST(test_selection_preparation_preserves_inventory_ties_and_nonzero_origin_clamp);
  RUN_TEST(test_selection_preparation_exact_snapshot_dedup_across_pages_bars_and_wrap);
  RUN_TEST(test_selection_preparation_clean_visual_keeps_split_starts_and_invalid_identity);
  RUN_TEST(test_selection_preparation_rejects_stale_result_and_restarts_replaced_source);
  RUN_TEST(test_selection_preparation_refocus_and_reset_supersede_only_private_work);
  RUN_TEST(test_selection_preparation_empty_source_has_no_fabricated_identity);
  RUN_TEST(test_selection_preparation_waits_for_real_snapshot_owner_without_losing_request);
  RUN_TEST(test_selection_preparation_source_replacement_during_dedup_discards_old_coordinates);
  RUN_TEST(test_selection_preparation_slot_and_selection_supersession_are_qualified);
  RUN_TEST(test_selection_preparation_unsupported_overlay_and_stopped_dirty_source_are_explicit);
  RUN_TEST(test_selection_preparation_matches_legacy_selection_on_ready_source);
  RUN_TEST(test_selection_preparation_pending_work_preserves_real_current_root_midi);
  RUN_TEST(test_selection_preparation_distinguishes_empty_dirty_cache_from_pending_event_values);
  RUN_TEST(test_selection_preparation_snapshot_read_unit_accepts_maximum_page_and_rejects_over_cap);
  RUN_TEST(test_selection_preparation_snapshot_dedup_preserves_same_identity_different_starts);
  RUN_TEST(test_committed_id_preparation_acceptance_and_single_chunk_grants);
  RUN_TEST(test_committed_id_preparation_pass_order_disabled_rows_and_legacy_parity);
  RUN_TEST(test_committed_id_preparation_stale_revision_rejects_then_restarts_idempotently);
  RUN_TEST(test_committed_id_preparation_reset_recycled_source_cannot_revive_coordinates);
  RUN_TEST(test_committed_id_preparation_wrong_loop_capture_change_and_discard);
  RUN_TEST(test_committed_id_preparation_invalid_chunk_fault_and_empty_completion);
  RUN_TEST(test_committed_id_preparation_preserves_shared_payload_root_and_refcounts);
  RUN_TEST(test_committed_id_preparation_valid_first_unsealed_second_is_not_drained);
  RUN_TEST(test_real_select_default_entry_consumes_completed_selection_once);
  RUN_TEST(test_real_select_default_entry_does_not_create_or_dirty_event_cache);
  RUN_TEST(test_real_select_empty_default_entry_keeps_bracket_without_reselection);
  RUN_TEST(test_real_select_direct_entry_still_resolves_and_handoff_does_not_leak);
  RUN_TEST(test_real_select_entry_observes_canonical_count_without_materializing_values);
  RUN_TEST(test_real_select_entry_observes_dirty_value_count_without_sync_or_discard);
  RUN_TEST(test_real_select_zero_length_default_return_preserves_direct_entry_behavior);
  RUN_TEST(test_real_select_default_entry_preserves_legacy_second_clamp_landing);
  RUN_TEST(test_second_clamp_withheld_pass_is_private_bounded_and_unpinned);
  RUN_TEST(test_second_clamp_source_replacement_restarts_first_pass);
  RUN_TEST(test_second_clamp_fsm_only_change_supersedes_pending_and_completed);
  RUN_TEST(test_second_clamp_already_select_keeps_first_landing_without_reentry);
  RUN_TEST(test_second_clamp_clean_visual_nearest_only_preserves_origin_and_tie_order);
  RUN_TEST(test_departed_direct_open_preserves_inactive_selected_target);
  RUN_TEST(test_departed_default_selection_preserves_active_current_identity);
  RUN_TEST(test_departed_direct_open_does_not_allocate_cold_track);
  RUN_TEST(test_departed_default_selection_does_not_read_cold_track);
#if defined(S75B8_ENTRY_BOUNDARY_ORACLE)
  RUN_TEST(test_entry_keeps_the_accepted_gesture_pending_when_the_owner_turn_is_withheld);
#endif
  return UNITY_END();
}
