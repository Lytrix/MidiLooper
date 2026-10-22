//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// `windowed-playback-generation` **task 3.13 — fixture 2.12's integration half**.
//
// Fixture 2.12 asks for the preview source to be exercised *through the session that produces it*: edit
// while playing on a long loop, cross window boundaries, undo/redo inside the session, switch the
// selected track, mutate or exit between build grains, then save — and assert that the preview is
// identified as preview, is invalidated by its own lifecycle, publishes no mixed or stale content, and
// never counts as committed currency.
//
// The slice landed 2026-10-02 (`evidence/2026-10-02-s313-preview-contract/`) proves the lane's own
// contract (identity, freezing, cancellation, handoff) with the preview projection *supplied by the
// fixture*. Its two remaining halves were named as owed there:
//
//   * **this harness** — a suite that links the **window lane** (`track_harness.inc`) *and* the **real
//     note-edit owner** (`src/EditManager*.cpp`). The runtime working-set suite deliberately stubs the
//     edit session ("this suite never opens an edit session"), so driving a real one there would be a
//     new harness combination rather than a fixture. That is what this suite adds: the session's own
//     projection, the session's own revision, the production undo/redo entries and the production
//     focus route are the inputs, so the preview a fixture sees is the session's, not a hand-built
//     vector;
//   * **the production callers** (the preview requester and the `EditManager` lifecycle hooks) and, since
//     task 3.15d slice 2, **the clock edge's own adoption**: a published preview window is what NOTE_EDIT
//     playback emits while it is current, and the merged reader is the named compatibility fallback. The
//     edge selector is compiled only under `PLAYBACK_WINDOW_ROLLING_ENABLED` (opened below for this suite),
//     so the emission fixtures drive the real `playMidiEvents` path rather than a stand-in.
//
// The boundary stub surface is the runtime working-set suite's measured set with the note-edit
// neighbours **removed** (this suite defines them for real through `src/EditManager*.cpp`); everything
// else keeps the same treatment: physical outputs observable, unrelated domain routes fail closed.

#define ARDUINO 1
// Task 3.15d slice 2: the emission harness drives the clock edge's rolling selector, which is compiled only under this
// gate (`RuntimePlaybackView.h` admits native as a gate owner). The lane fixtures above the harness do not drive the
// edge, so opening the gate adds the selector without changing what they ask.
#define PLAYBACK_WINDOW_ROLLING_ENABLED 1

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

// The window lane plus the real Track/Loop/TrackManager owners. The real `EditManager` global and
// constructor come from `src/EditManager.cpp` below, so the harness must not define them.
#define HARNESS_LINKS_REAL_EDIT_MANAGER
// This suite drives no button route, and it links the real undo owner (`src/TrackUndo.cpp`), which
// defines the same `restorePlaybackAfterSlotClear` the button TU does -- the choice `track_harness.inc`
// documents for suites that link undo.
#define HARNESS_OMITS_MIDI_BUTTON_ACTIONS
#include "../test_support/track_harness.inc"

#include "LoopSnapshot.h"

/// The display/storage notifications `TrackManager::setSelectedTrack` makes are the same boundary this
/// harness always treats as observable (they report a focus change; they do not decide it), and the
/// slot identities the panel is told about are recorded because the focus route is one of the subject's
/// lifecycle events.
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

// The boundary types the stub surface defines members of must be declared first. This suite omits the
// button TU (it links the real undo owner instead), so it names them itself -- the include set
// `capture_storage_harness.inc` uses for the same reason.
#include "ControlSurfaceManager.h"
#include "MidiFaderActions.h"
#include "MidiFaderManager.h"
#include "MidiFaderProcessor.h"
#include "MidiButtonActions.h"

#define HARNESS_OBSERVABLE_SD_REFRESH
#define HARNESS_OBSERVABLE_SLOT_CHANGE
// The focus route's disabled-pass reclaim is not what the fixture asserts (it is storage bookkeeping
// beside the departure), so it is observable rather than fail-closed -- no note-edit suite asserts it.
#define HARNESS_OBSERVABLE_START_RECLAIM
#define HARNESS_LINKS_REAL_PLAYBACK_EDGE
#include "off_path_deps.h"

// --- the real note-edit owner (the subject of fixture 2.12) -------------------------------
// `src/Globals.cpp` cannot be linked here (the harness already defines its other symbols), so the one
// value the edit stack reads is supplied directly -- the same boundary `capture_storage_harness.inc` uses.
float bpm = 120.0f;

#include "../../src/MidiHandler.cpp"
#include "../../src/EditManager.cpp"
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
#include "../../src/EditManager/EditNoteStateCoordinator.cpp"
#include "../../src/EditManager/NoteEditCurrentState.cpp"
#include "../../src/EditManager/NoteEditSessionUndo.cpp"
#include "../../src/EditManager/NoteEditSessionUndoStack.cpp"
#include "../../src/EditManager/EditSessionStoreInvariant.cpp"
#include "../../src/EditManager/EditSessionLiveStoreSpan.cpp"
#include "../../src/EditManager/ParticipatingNoteSession.cpp"
#include "../../src/EditManager/ApplyOwnedEditPassRows.cpp"
#include "../../src/EditManager/ApplyEditSessionActions.cpp"
// `EditSessionActionBuilder.cpp` is deliberately absent: `EditSessionInteraction.cpp` (included below)
// already provides its `baselineSpansEqual` helper in this concatenated TU.
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
// The five `EditStates` TUs are **not** linked: on the host toolchain their narrowing initializers are
// ill-formed (`src/EditStates/EditStartNoteState.cpp:69` assigns `focus.last.startTick` to a `uint8_t`
// member), so the FSM's state entries are an observable boundary here (see `off_path_deps.h`). The
// session's content store, preview revision, undo/redo entries and lifecycle stay real.
#include "../../src/TrackManager/TrackManagerSlotMatrix.cpp"
#include "../../src/Loop/LoopPendingNoteChange.cpp"
#include "../../src/Utils/LoopTickNormalize.cpp"
#include "../../src/Utils/SelectNavigation.cpp"
// The real undo owner: the session's undo/redo entries reach it (`NoteEditSessionUndo.cpp`), and this
// suite has no button route, so the button TU is omitted exactly as `track_harness.inc` documents.
#include "../../src/TrackUndo.cpp"
// Task 3.13 / fixture 2.12's `then save` clause: the **storage harness**'s own entry points, linked here because the
// clause needs both halves at once -- the window lane (the preview being served) and the production save. Measured
// before the edit: these two TUs link with **no** additional stub in this suite (`off_path_deps.h` already stubs the
// SD path and the display routes the save touches), and the suite's nine fixtures kept passing.
#include "../../src/StorageLoopIo.cpp"
#include "../../src/StorageLoopIoBodyFormatV1.cpp"
#include "../../src/Track/TrackPlaybackHotPath.cpp"

struct TrackPlaybackTestAccess {
  static LoopPlaybackRuntime* runtime(Track& track, uint8_t slot) {
    return track.playbackRuntime.slotIfAllocated(slot);
  }
};

namespace {

using WindowIdentity = PlaybackWindowIdentity;

/// 64 bars: `shouldAvoidFullVisualRebuild` is true above 16, so this is the **long** loop a windowed
/// (demoted) slot is, and the same length the 3.13 lane fixtures use.
constexpr uint32_t kLongLoopTicks = 64u * 768u;
constexpr uint8_t kChannel = 1;
constexpr uint32_t kPlayhead = 500;
/// The **restored-shape** fixtures' playhead: the onset of the first committed note `commitLongLoop(kLongLoopTicks, 6)`
/// writes (49152 / 7 = 7021). Task 3.15d made the preview read an interval, so a playhead in empty bars would make
/// "the window is the session's content" a comparison against zero events -- these fixtures place it where the content
/// is, and the served interval is [6253, 7789).
constexpr uint32_t kContentPhase = 7021;

struct SlotAccess {
  static void select(Track& track, uint8_t slot) { track.setActiveLoopIndex(slot); }
};

void setUpHarness() {
  nativePlaybackDeadlineClock = {};
  nativeTransportRunning = true;
  g_nativeSilenceTrackCalls = 0;
  g_nativeAdoptedMasterLength = 0;
  g_nativeAdoptedMasterLengthCalls = 0;
  resetSlotChangeObservation();
  // Fixture isolation: `editManager` is a global and the loop storage survives between fixtures, so an
  // earlier fixture's session can still be open (and its window still serving). Close it through the
  // production exit and reset the session state, so every fixture starts from "no session, revision 0".
  if (editManager.isNoteEditActive()) {
    editManager.closeNoteEditSession(trackManager.getSelectedTrack());
  }
  editManager.resetNoteEditSessionState();
  trackManager.~TrackManager();
  LoopEventStore::resetPoolForTests();
  new (&trackManager) TrackManager();
}


/// Commit one record pass of `lengthTicks` with `noteCount` notes — the content a restored library has.
/// No published root is installed: the session that opens on this slot **rematerializes** from the
/// passes, which is the shape whose projection is the session's own (a root-current session opens on the
/// snapshot with an empty store, and then has no preview content at all).
void commitLongLoop(Track& track, uint8_t slot, uint32_t lengthTicks, unsigned noteCount) {
  track.ensureLoopsAllocated();
  SlotAccess::select(track, slot);
  Loop& loop = track.getLoop(slot);
  loop.loopLengthTicks = lengthTicks;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE_MESSAGE(loop.captureActive(), "the slot opens a record pass");
  for (unsigned i = 0; i < noteCount; ++i) {
    const uint32_t onTick = (lengthTicks / (noteCount + 1u)) * (i + 1u);
    const uint8_t pitch = static_cast<uint8_t>(60 + i);
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(onTick, kChannel, pitch, 100)));
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(onTick + 48u, kChannel, pitch, 0)));
  }
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(lengthTicks, CommitReason::RecordStop));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass());
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
}

/// The windowed working set admitted and the transport playing: the state a demoted long loop is served
/// in, and the state `Track::invalidateCaches(true)` arms its deferred refresh in.
void readyLongLoopPlaying(Track& track, uint8_t slot) {
  clockManager.assignCurrentTickSilently(kPlayhead);
  track.forceSetState(TRACK_PLAYING);
  TEST_ASSERT_TRUE_MESSAGE(track.prewarmPlaybackForSlot(slot, true), "the slot's working set is admitted");
}

/// One production deferred turn: at most one declared grain, off-clock (`src/main.cpp`'s caller shape).
void laneTurn(Track& track, uint32_t phase) {
  (void)track.stepPlaybackViewMaintenance(phase, nullptr, false);
}

void drainLane(Track& track, uint32_t phase, size_t maxTurns = 2048) {
  for (size_t turn = 0; turn < maxTurns && track.hasPendingPlaybackViewMaintenance(); ++turn) {
    laneTurn(track, phase);
  }
  TEST_ASSERT_FALSE_MESSAGE(track.hasPendingPlaybackViewMaintenance(), "the lane's admitted build completes");
}

/// Run deferred turns only until the **first** preview window is published, then stop: a lane that produced its first
/// interval and has not (yet) been asked for a successor. That is the late-cadence shape the seam fixture needs.
void publishFirstPreview(Track& track, uint8_t slot, uint32_t phase, size_t maxTurns = 64) {
  for (size_t turn = 0; turn < maxTurns; ++turn) {
    WindowIdentity identity{};
    if (track.publishedPlaybackWindowIdentityForSlot(slot, identity)) return;
    laneTurn(track, phase);
  }
  TEST_FAIL_MESSAGE("the first preview window publishes");
}

/// One production `PlaybackViewMaintenance` owner visit for this track, in `src/main.cpp`'s order: the session's
/// requester runs (**unconditionally** on the visit -- that is the cadence's whole point), then the lane serves the
/// work it admitted.
void serviceLaneVisit(Track& track, uint32_t phase) {
  track.servicePreviewWindowForNoteEditSession();
  drainLane(track, phase);
}

/// The session's own projection: `store.readEvents()` (the const overload), which is the content the
/// session serves and the source a preview request is made of. The non-const overload returns the
/// mutable store the edit ops write.
const MidiEventVec& sessionProjection() {
  const EditManager& session = editManager;
  return session.sessionMidiEvents();
}

/// The published window's served stream as a multiset of (tick, pitch, note-on/off): the projection's
/// order is the session's, the served window's is emission order, and equality of *content* is what
/// "no mixed or stale publication" is about.
struct EventKey {
  uint32_t tick;
  uint8_t pitch;
  bool on;
};

std::vector<EventKey> servedEventKeys(Track& track, uint8_t slot, size_t expectedCount) {
  std::vector<EventKey> keys;
  for (size_t i = 0; i < expectedCount; ++i) {
    MidiEvent event{};
    TEST_ASSERT_TRUE(track.playbackWindowEventAtForSlot(slot, static_cast<uint16_t>(i), event));
    EventKey key{};
    key.tick = event.tick;
    key.pitch = event.data.noteData.note;
    key.on = event.isNoteOn();
    keys.push_back(key);
  }
  return keys;
}

std::vector<EventKey> projectionEventKeys(const MidiEventVec& events) {
  std::vector<EventKey> keys;
  for (const MidiEvent& event : events) {
    if (!event.isNoteOn() && !event.isNoteOff()) {
      continue;
    }
    EventKey key{};
    key.tick = event.tick;
    key.pitch = event.data.noteData.note;
    key.on = event.isNoteOn();
    keys.push_back(key);
  }
  return keys;
}

/// The same keys, restricted to one window interval: what a preview window of that interval may serve.
std::vector<EventKey> projectionEventKeysIn(const MidiEventVec& events,
                                            const PlaybackCoverageInterval& coverage) {
  std::vector<EventKey> keys;
  for (const MidiEvent& event : events) {
    if (!event.isNoteOn() && !event.isNoteOff()) {
      continue;
    }
    if (!coverage.contains(event.tick)) {
      continue;
    }
    EventKey key{};
    key.tick = event.tick;
    key.pitch = event.data.noteData.note;
    key.on = event.isNoteOn();
    keys.push_back(key);
  }
  return keys;
}

bool sameKeys(std::vector<EventKey> a, std::vector<EventKey> b) {
  const auto less = [](const EventKey& l, const EventKey& r) {
    if (l.tick != r.tick) return l.tick < r.tick;
    if (l.pitch != r.pitch) return l.pitch < r.pitch;
    return static_cast<int>(l.on) < static_cast<int>(r.on);
  };
  std::sort(a.begin(), a.end(), less);
  std::sort(b.begin(), b.end(), less);
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].tick != b[i].tick || a[i].pitch != b[i].pitch || a[i].on != b[i].on) return false;
  }
  return true;
}

/// True when the session's projection holds a note at `tick` with `pitch` (either half of the pair):
/// the clause the undo/redo fixture asserts on the *edit* rather than on a byte-exact re-derivation.
bool projectionHasNoteAt(const MidiEventVec& events, uint32_t tick, uint8_t pitch) {
  for (const MidiEvent& event : events) {
    if (event.tick != tick) continue;
    if (event.data.noteData.note != pitch) continue;
    if (event.isNoteOn() || event.isNoteOff()) return true;
  }
  return false;
}

/// The interval the requester uses for this slot at `playhead` — the **same** policy call the production
/// requester makes (`Track::playbackWindowInterval` with the loop's own demoted test), so a fixture never
/// invents a second geometry rule.
PlaybackCoverageInterval playbackIntervalAt(Track& track, uint8_t slot, uint32_t playhead) {
  const Loop& loop = track.getLoop(slot);
  PlaybackCoverageInterval coverage{};
  TEST_ASSERT_TRUE(Track::playbackWindowInterval(
      loop.loopLengthTicks, loop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks), playhead, coverage));
  return coverage;
}

/// The session's projection **within** `coverage`. For the restored shape (no published root) the session
/// store already holds the effective content, so filtering it to the interval *is* the composition; the
/// current-root shape composes committed content with the delta through the firmware's own path instead.
MidiEventVec intervalProjection(const PlaybackCoverageInterval& coverage) {
  MidiEventVec out;
  for (const MidiEvent& event : sessionProjection()) {
    if (coverage.contains(event.tick)) {
      out.push_back(event);
    }
  }
  return out;
}

/// The interval-qualified source a fixture hands the requester **directly**. It has to outlive the lane's
/// grains (the build copies the caller's array across turns), so it is fixture-scope storage rather than a
/// temporary — the production path composes into the slot runtime's own resident scratch for the same reason.
MidiEventVec& previewSourceScratch() {
  static MidiEventVec scratch;
  return scratch;
}

/// The served window's stream **is** the session's projection within the window's own interval — the clause
/// that says the preview the lane publishes is the session's content for that interval and nothing else (no
/// mixed generation, no committed underlay outside the interval).
void assertServedWindowIsSessionProjection(Track& track, uint8_t slot, const char* reason) {
  WindowIdentity identity{};
  TEST_ASSERT_TRUE_MESSAGE(track.publishedPlaybackWindowIdentityForSlot(slot, identity),
                           "a window serves the slot");
  const std::vector<EventKey> expected = projectionEventKeysIn(sessionProjection(), identity.interval);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(expected.size()),
                                   static_cast<uint32_t>(track.playbackWindowEventCountForSlot(slot)),
                                   reason);
  const std::vector<EventKey> served =
      servedEventKeys(track, slot, track.playbackWindowEventCountForSlot(slot));
  TEST_ASSERT_TRUE_MESSAGE(sameKeys(served, expected), reason);
}

/// Publish the session's preview at `revision` and adopt the successor when one is prepared. `phase` is the
/// playhead the requester reads, so the fixture's source filter and the window's interval are one geometry.
uint32_t publishSessionPreviewAt(Track& track, uint8_t slot, uint32_t revision, uint32_t phase = kPlayhead) {
  const MidiEventVec& projection = sessionProjection();
  TEST_ASSERT_FALSE_MESSAGE(projection.empty(), "the session has a projection to publish");
  // Task 3.15d: the requester's source is the **interval-qualified** composition, so this direct caller
  // supplies the same shape the production composer does for the restored case -- the session's content
  // within the window's own interval. (A whole-loop projection here would hand the build content outside
  // the interval it publishes.)
  MidiEventVec& source = previewSourceScratch();
  source = intervalProjection(playbackIntervalAt(track, slot, phase));
  // Task 3.7a (I3): the production seams now request the session's preview themselves (the open and every revision
  // advance), so the lane may already hold this revision -- as a prepared successor or a served window. The helper
  // drives the same state to a served window either way, and a request that *was* admitted is still admissible: what it
  // asserts is the state, not which caller asked first.
  const Track::PlaybackViewRequestResult requested = track.requestPreviewWindow(
      slot, revision, source.data(), source.size(), phase, phase);
  TEST_ASSERT_TRUE_MESSAGE(requested == Track::PlaybackViewRequestResult::Admitted ||
                               requested == Track::PlaybackViewRequestResult::SuccessorAlreadyPrepared ||
                               requested == Track::PlaybackViewRequestResult::BuildInFlight,
                           "the preview lane admits the session's projection, or already holds it");
  drainLane(track, phase);
  WindowIdentity identity{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(slot, identity));
  if (identity.playbackRevision != revision) {
    // Something already served the slot, so this build completed as the **ready successor** and the
    // production traversal boundary is what swaps it in. (In this harness the loop storage survives
    // between fixtures, so a previous fixture's window can still be serving here -- which is exactly
    // the shape the successor lane exists for, and the fixture takes the boundary explicitly because
    // it drives a fixed playhead phase.)
    TEST_ASSERT_TRUE_MESSAGE(track.adoptPreparedPlaybackWindow(slot),
                             "the successor publication is adopted at the boundary");
    TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(slot, identity));
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(revision, identity.playbackRevision,
                                   "the published window carries the session's revision");
  return revision;
}

/// Publish the session's preview at the revision it currently carries, at `phase`.
uint32_t publishSessionPreview(Track& track, uint8_t slot, uint32_t phase = kPlayhead) {
  return publishSessionPreviewAt(track, slot, editManager.sessionPlaybackPreviewRevision(), phase);
}

/// The session's content changes and the **deferred refresh flushes** — the production edge that
/// advances the *playback* preview revision: `Track::invalidateCaches(true)` arms
/// `scheduleDeferredNoteEditDisplayRefresh` while playing, and the flush (on the deferred turn, or at
/// commit/close/depart) bumps the revision. Returns the revision after the flush.
uint32_t mutateSessionAndFlush(Track& track, uint32_t onTick, uint8_t pitch) {
  const uint32_t before = editManager.sessionPlaybackPreviewRevision();
  // A **paired**, **identified** note: paired so the lane has no open note to close (the closure reserve
  // is not this fixture's subject), and identified because the session's undo/redo works in note
  // identity -- production edits assign one, so a mutation without it could not be re-applied by the
  // redo entry.
  MidiEvent on = MidiEvent::NoteOn(onTick, kChannel, pitch, 100);
  on.noteId = static_cast<NoteId>(9000u + pitch);
  editManager.sessionMidiEvents().push_back(on);
  editManager.sessionMidiEvents().push_back(MidiEvent::NoteOff(onTick + 48u, kChannel, pitch, 0));
  track.invalidateCaches(true);                            // production: arms the deferred refresh (playing)
  editManager.flushDeferredNoteEditDisplayRefresh(track);   // production: the flush edge
  const uint32_t after = editManager.sessionPlaybackPreviewRevision();
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(before + 1u, after, "a mutation advances the preview revision once");
  return after;
}

/// Advance the session's preview revision through the production deferred-refresh edge **without touching the
/// store**: the fixture has already expressed its delta in `noteEditCurrentState` and projected it. This is the
/// seam a real edit reaches (`Track::invalidateCaches(true)` arms the refresh while playing; the flush bumps the
/// revision, invalidates the caches and runs the preview requester).
uint32_t flushSessionPreviewRevision(Track& track) {
  const uint32_t before = editManager.sessionPlaybackPreviewRevision();
  track.invalidateCaches(true);
  editManager.flushDeferredNoteEditDisplayRefresh(track);
  const uint32_t after = editManager.sessionPlaybackPreviewRevision();
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(before + 1u, after,
                                   "the deferred refresh advances the preview revision once");
  return after;
}

/// Drive the lane to the served window for `expectedRevision`, taking the production boundary when the newest
/// revision completed as the ready successor (a previously served window keeps serving until then).
WindowIdentity serveLatestPreview(Track& track, uint8_t slot, uint32_t expectedRevision) {
  WindowIdentity identity{};
  TEST_ASSERT_TRUE_MESSAGE(track.publishedPlaybackWindowIdentityForSlot(slot, identity),
                           "a window serves the slot");
  if (identity.playbackRevision != expectedRevision) {
    TEST_ASSERT_TRUE_MESSAGE(track.adoptPreparedPlaybackWindow(slot),
                             "the newest revision is prepared as the successor");
    TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(slot, identity));
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(expectedRevision, identity.playbackRevision,
                                   "the served window carries the newest revision");
  return identity;
}

/// Install a committed root for the slot (the seam `Loop::storePublishedSnapshotForTest`): the
/// **committed** lane gathers from a published root, so the exit handoff needs one. The lane's subject
/// here is the handoff, not how the root appeared.
void publishCommittedRootForSlot(Loop& loop, uint32_t bars) {
  auto empty = std::make_shared<const LoopSnapshot>(LoopSnapshot(loop.loopId, loop.loopLengthTicks, 0, 8, 8));
  LoopSnapshotChangeSet changes;
  for (uint32_t bar = 0; bar < bars; ++bar) {
    SnapshotNoteChange add{};
    add.kind = SnapshotChangeKind::Add;
    add.next.loopId = loop.loopId;
    add.next.noteId = static_cast<NoteId>(2000 + bar);
    add.next.pitch = 48;
    add.next.velocity = 100;
    add.next.startTick = bar * 768u;
    add.next.endTick = bar * 768u + 48u;
    changes.notes.push_back(add);
    SnapshotEventChange on{};
    on.kind = SnapshotChangeKind::Add;
    on.next.loopId = loop.loopId;
    on.next.eventId = static_cast<EventId>(4000 + bar * 2u);
    on.next.canonicalOrder = bar * 2u;
    on.next.value = MidiEvent::NoteOn(bar * 768u, kChannel, 48, 100);
    on.next.value.noteId = add.next.noteId;
    changes.events.push_back(on);
    SnapshotEventChange off{};
    off.kind = SnapshotChangeKind::Add;
    off.next.loopId = loop.loopId;
    off.next.eventId = static_cast<EventId>(4001 + bar * 2u);
    off.next.canonicalOrder = bar * 2u + 1u;
    off.next.value = MidiEvent::NoteOff(bar * 768u + 48u, kChannel, 48, 0);
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

}  // namespace

void setUp() { setUpHarness(); }

void tearDown() {}


// ---------------------------------------------------------------------------------------------
// Task 3.15e slice 3 — the live-overlay edge oracle. This suite compiles the rolling lane (1), links
// the real hot path and the real window build, and stages the captured two-bar world the finalization
// suite uses. The finalization suite itself runs with the gate closed (echo path), so the edge's own
// assertions live here.
// ---------------------------------------------------------------------------------------------
struct TrackCaptureStopTestAccess {
  static LoopPlaybackRuntime& runtimeFor(Track& t, uint8_t slot) {
    return *t.playbackRuntime.slotIfAllocated(slot);
  }
  static uint32_t playbackGenerationOf(const Track& t) { return t.playbackGeneration; }
  static void resetProjectionCycle(Track& t) { t.projectionCycleStartTick = 0; }
  static void unmute(Track& t) { t.muted = false; }
  static void selectSlot(Track& t, uint8_t slot) { t.activeLoopIndex = slot; }
};

namespace {

struct RootPair {
  uint8_t note;
  uint32_t on;
  uint32_t off;
};

void publishRootPairs(Loop& loop, const RootPair* pairs, size_t count) {
  auto empty = std::make_shared<const LoopSnapshot>(LoopSnapshot(loop.loopId, loop.loopLengthTicks, 0, 8, 8));
  LoopSnapshotChangeSet changes;
  EventId nextEventId = 7000;
  for (size_t i = 0; i < count; ++i) {
    const RootPair& pair = pairs[i];
    SnapshotNoteChange add{};
    add.kind = SnapshotChangeKind::Add;
    add.next.loopId = loop.loopId;
    add.next.noteId = static_cast<NoteId>(3000 + i);
    add.next.pitch = pair.note;
    add.next.velocity = 100;
    add.next.startTick = pair.on;
    add.next.endTick = pair.off;
    changes.notes.push_back(add);
    SnapshotEventChange on_change{};
    on_change.kind = SnapshotChangeKind::Add;
    on_change.next.loopId = loop.loopId;
    on_change.next.eventId = nextEventId++;
    on_change.next.canonicalOrder = nextEventId * 2;
    on_change.next.value = MidiEvent::NoteOn(pair.on, 1, pair.note, 100);
    changes.events.push_back(on_change);
    SnapshotEventChange off_change{};
    off_change.kind = SnapshotChangeKind::Add;
    off_change.next.loopId = loop.loopId;
    off_change.next.eventId = nextEventId++;
    off_change.next.canonicalOrder = nextEventId * 2 + 1;
    off_change.next.value = MidiEvent::NoteOff(pair.off, 1, pair.note, 0);
    changes.events.push_back(off_change);
  }
  LoopSnapshotConstruction construction(empty, changes);
  while (!construction.complete()) {
    TEST_ASSERT_TRUE(construction.step(128));
  }
  const ImmutableLoopSnapshotRef snapshot = construction.freeze();
  TEST_ASSERT_NOT_NULL(snapshot.get());
  loop.storePublishedSnapshotForTest(snapshot, loop.playbackRevision);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
}

void stageTwoBarWorld(Track& t, uint8_t slot, Loop*& loopOut) {
  constexpr uint32_t kBar = Config::TICKS_PER_BAR;
  constexpr uint32_t kLoopTicks = 2u * kBar;
  Loop& loop = t.getLoop(slot);
  loop.loopLengthTicks = kLoopTicks;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(12, 1, 48, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(96, 1, 48, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(kBar + 32, 1, 62, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(kBar + 112, 1, 62, 0)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(kLoopTicks, CommitReason::RecordStop));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass());
  const RootPair pairs[] = {{48, 12, 96}, {62, kBar + 32, kBar + 112}};
  publishRootPairs(loop, pairs, 2);
  TEST_ASSERT_TRUE_MESSAGE(t.prewarmPlaybackForSlot(slot, true),
                           "the slot must hold a playback runtime before the edge is driven");
  loopOut = &loop;
}

bool serviceCanonicalTurn(Track& t, uint8_t& nextTrack) {
  return MidiServiceDrain::serviceCanonicalPrefixTurn(
      MidiServiceDrain::DeferredIdleOwner::VisualCache, Config::NUM_TRACKS, nextTrack,
      [&](uint8_t index) {
        if (index != 0) return false;
        const Loop& active = t.getActiveLoop();
        return MidiServiceDrain::canonicalPreparationHasPriorityWork(
            t.isOverdubbing(), active.visualCacheDirty, active.hasDeferredVisualCacheWrapAppend(),
            active.overdubSourceSpanCacheReady());
      },
      [&](uint8_t index) { return index == 0 && trackManager.getTrack(0).processDeferredCanonicalPrefix(); });
}

}  // namespace

// **W1/W4/W5 — the live overlay window serves, the echo is suppressed, and the ledger splits by provenance.**
//
// A two-bar committed world, a live overdub with one unclosed note, the lane's candidate with a published frontier, then
// the production clock entry across the loop. The wire must carry the live row exactly once (a running echo would duplicate
// it) and the committed base once; the ledger must stay closed because the overlay row is emitted without committed-pass
// ownership. The test-only selection witness proves the edge route was taken rather than the echo fallback.
void test_the_live_overlay_window_serves_and_the_echo_is_suppressed() {
  static constexpr uint32_t kLoopTicks = 2u * Config::TICKS_PER_BAR;
  LoopEventStore::initPool();
  Track& t = trackManager.getTrack(0);
  t.ensureLoopsAllocated();
  Loop* loopPtr = nullptr;
  stageTwoBarWorld(t, 0, loopPtr);
  TEST_ASSERT_NOT_NULL(loopPtr);
  Loop& loop = *loopPtr;
  t.forceSetState(TRACK_PLAYING);
  TrackCaptureStopTestAccess::unmute(t);
  TrackCaptureStopTestAccess::selectSlot(t, 0);
  TrackCaptureStopTestAccess::resetProjectionCycle(t);
  loop.lastTickInLoop = 0;
  loop.nextEventIndex = 0;
  const auto position = [&](uint32_t tick) {
    clockManager.assignCurrentTickSilently(tick);
    nativePlaybackDeadlineClock = {tick, 1};
  };
  auto& rt = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  uint8_t nextTrack = 0;

  position(kLoopTicks + 1024);
  t.startOverdubbing(kLoopTicks + 1024);
  // One unclosed live note: if it were ledgered, `openCount` would be 1 after the traversal.
  t.noteOn(1, 70, 100, kLoopTicks + 1470);
  if (!loop.overdubSourceSpanCacheReady()) loop.rebuildOverdubSourceSpanCacheForTest(0, loop.loopLengthTicks);
  for (unsigned turn = 0; turn < 400 && !rt.canonicalPassInputs.ready(); ++turn)
    (void)serviceCanonicalTurn(t, nextTrack);
  TEST_ASSERT_TRUE_MESSAGE(rt.canonicalPassInputs.ready(),
                           "the committed owner must prepare the canonical base under overlay-only demand");
  const RuntimePlaybackView* candidate = nullptr;
  for (unsigned turn = 0; turn < 600 && candidate == nullptr; ++turn) {
    (void)t.serviceOverdubOverlayWindow();
    const RuntimePlaybackView* now = rt.overdubOverlayCandidate();
    if (now != nullptr && now->complete) candidate = now;
  }
  TEST_ASSERT_NOT_NULL_MESSAGE(candidate, "the borrowed base must complete into the combined candidate");
  TEST_ASSERT_GREATER_THAN_UINT16(0, candidate->readableEventCount);
  TEST_ASSERT_EQUAL_UINT32(0, rt.ledger.openCount());
  TEST_ASSERT_TRUE(t.isOverdubbing());
  TEST_ASSERT_FALSE(editManager.isNoteEditActive());

  const uint32_t selectionsBefore = Track::overlayLiveSelectionCount();
  MidiSendCapture::reset();
  for (uint32_t tick = 16; tick <= kLoopTicks; tick += 16) {
    position(kLoopTicks + tick);
    t.playMidiEvents(kLoopTicks + tick, true);
  }
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(selectionsBefore, Track::overlayLiveSelectionCount(),
                                          "the edge must bind the live overlay window");
  unsigned liveOns = 0, baseOns = 0, baseOffs = 0;
  for (size_t i = 0; i < MidiSendCapture::count(); ++i) {
    const MidiEvent& event = MidiSendCapture::at(i);
    if (event.isNoteOn() && event.data.noteData.note == 70) ++liveOns;
    if (event.data.noteData.note == 48) event.isNoteOn() ? ++baseOns : ++baseOffs;
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, liveOns,
                                   "the live row is served once: a running echo would duplicate it");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, baseOns, "the committed base is served from the same window");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, baseOffs, "the committed base pair is served once");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, rt.ledger.openCount(),
                                   "an overlay row is sounded without committed-pass ledger ownership");
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_the_live_overlay_window_serves_and_the_echo_is_suppressed);
  return UNITY_END();
}
