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
//   * **the production callers** (the preview requester and the `EditManager` lifecycle hooks): still
//     owed, and *not* simulated here. A published window is consumed by the windowed serving path,
//     which production admits in task 3.10/3.7 (the merged fallback still serves NOTE_EDIT preview
//     today, and the rolling lane is behind `PLAYBACK_WINDOW_ROLLING_ENABLED`, default 0). This suite
//     asks and cancels the lane at the seams those callers will use — the session's revision bump
//     (the deferred-refresh flush), the focus departure and the session exit — so the wiring lands
//     against a pinned integration, not against a guess.
//
// The boundary stub surface is the runtime working-set suite's measured set with the note-edit
// neighbours **removed** (this suite defines them for real through `src/EditManager*.cpp`); everything
// else keeps the same treatment: physical outputs observable, unrelated domain routes fail closed.

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

namespace {

using WindowIdentity = PlaybackWindowIdentity;

/// 64 bars: `shouldAvoidFullVisualRebuild` is true above 16, so this is the **long** loop a windowed
/// (demoted) slot is, and the same length the 3.13 lane fixtures use.
constexpr uint32_t kLongLoopTicks = 64u * 768u;
constexpr uint8_t kChannel = 1;
constexpr uint32_t kPlayhead = 500;

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

/// The served window's stream **is** the session's projection — the clause that says the preview the
/// lane publishes is the session's content and nothing else (no mixed generation, no committed
/// underlay).
void assertServedWindowIsSessionProjection(Track& track, uint8_t slot, const char* reason) {
  const std::vector<EventKey> expected = projectionEventKeys(sessionProjection());
  TEST_ASSERT_FALSE_MESSAGE(expected.empty(), "the session's projection has content to serve");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(expected.size()),
                                   static_cast<uint32_t>(track.playbackWindowEventCountForSlot(slot)),
                                   reason);
  const std::vector<EventKey> served =
      servedEventKeys(track, slot, track.playbackWindowEventCountForSlot(slot));
  TEST_ASSERT_TRUE_MESSAGE(sameKeys(served, expected), reason);
}

/// Publish the session's preview at `revision` and adopt the successor when one is prepared.
uint32_t publishSessionPreviewAt(Track& track, uint8_t slot, uint32_t revision) {
  const MidiEventVec& projection = sessionProjection();
  TEST_ASSERT_FALSE_MESSAGE(projection.empty(), "the session has a projection to publish");
  // Task 3.7a (I3): the production seams now request the session's preview themselves (the open and every revision
  // advance), so the lane may already hold this revision -- as a prepared successor or a served window. The helper
  // drives the same state to a served window either way, and a request that *was* admitted is still admissible: what it
  // asserts is the state, not which caller asked first.
  const Track::PlaybackViewRequestResult requested = track.requestPreviewWindow(
      slot, revision, projection.data(), projection.size(), kPlayhead);
  TEST_ASSERT_TRUE_MESSAGE(requested == Track::PlaybackViewRequestResult::Admitted ||
                               requested == Track::PlaybackViewRequestResult::SuccessorAlreadyPrepared ||
                               requested == Track::PlaybackViewRequestResult::BuildInFlight,
                           "the preview lane admits the session's projection, or already holds it");
  drainLane(track, kPlayhead);
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

/// Publish the session's preview at the revision it currently carries.
uint32_t publishSessionPreview(Track& track, uint8_t slot) {
  return publishSessionPreviewAt(track, slot, editManager.sessionPlaybackPreviewRevision());
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
    changes.events.push_back(on);
    SnapshotEventChange off{};
    off.kind = SnapshotChangeKind::Add;
    off.next.loopId = loop.loopId;
    off.next.eventId = static_cast<EventId>(4001 + bar * 2u);
    off.next.canonicalOrder = bar * 2u + 1u;
    off.next.value = MidiEvent::NoteOff(bar * 768u + 48u, kChannel, 48, 0);
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

/// Fixture 2.12 (identity + currency clause), driven by the **real** session: the slot's preview source
/// is the session's own projection at the session's own revision, and nothing serves the slot before the
/// session exists (so "the window is the session's" cannot pass by accident).
static void test_the_session_owns_the_preview_source_and_its_revision_is_the_lane_currency() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  commitLongLoop(track, 0, kLongLoopTicks, 6);
  readyLongLoopPlaying(track, 0);

  TEST_ASSERT_FALSE_MESSAGE(track.playbackWindowServesSlot(0),
                            "control: no window serves the slot before the session's preview exists");
  TEST_ASSERT_EQUAL_UINT32(0u, editManager.sessionPlaybackPreviewRevision());

  editManager.openNoteEditSession(track);
  TEST_ASSERT_TRUE_MESSAGE(editManager.isNoteEditActive(), "the production session open is real");
  const uint32_t revision = editManager.sessionPlaybackPreviewRevision();
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0u, revision,
                                          "opening the session bumps the preview revision it freezes");
  TEST_ASSERT_FALSE_MESSAGE(sessionProjection().empty(),
                            "the session rematerialized the loop's content into its own store");

  const uint32_t published = publishSessionPreview(track, 0);
  TEST_ASSERT_EQUAL_UINT32(revision, published);

  WindowIdentity identity{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, identity));
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<uint8_t>(PlaybackWindowOrigin::Preview),
                                  static_cast<uint8_t>(identity.origin),
                                  "the published window is identified as preview");
  TEST_ASSERT_TRUE(track.servedPlaybackWindowIsPreviewForSlot(0));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(kLongLoopTicks, identity.interval.loopLengthTicks,
                                   "a preview covers the whole loop: a session is not an interval");
  assertServedWindowIsSessionProjection(track, 0, "the served preview is the session's own projection");
}

/// Fixture 2.12 (no mixed or stale publication), driven by the **real** mutation edge: a content change
/// that lands **between build grains** cancels the in-flight build and advances the revision, and the
/// stale completion publishes nothing — the served window stays the previous revision, field-for-field,
/// until the new revision is adopted.
static void test_a_mutation_between_build_grains_never_publishes_a_mixed_or_stale_preview() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  commitLongLoop(track, 0, kLongLoopTicks, 6);
  readyLongLoopPlaying(track, 0);
  editManager.openNoteEditSession(track);
  const uint32_t first = publishSessionPreview(track, 0);
  WindowIdentity servedFirst{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, servedFirst));

  // A second build is begun and one grain is taken, so the mutation is genuinely mid-build.
  const MidiEventVec beforeMutation = sessionProjection();
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                            static_cast<int>(track.requestPreviewWindow(0, first, beforeMutation.data(),
                                                                        beforeMutation.size(), kPlayhead)),
                            "the rebuild is admitted");
  laneTurn(track, kPlayhead);

  // The mutation lands, and the lane is cancelled exactly as the mutation edge does (the session's
  // frozen revision is gone).
  const uint32_t second = mutateSessionAndFlush(track, 3000, 72);
  const int cancelOutcome = static_cast<int>(track.cancelPreviewWindowForSlot(
      0, Track::PreviewWindowCancelReason::SourceMutated));
  TEST_ASSERT_TRUE_MESSAGE(cancelOutcome == static_cast<int>(Track::PreviewCancelOutcome::BuildCancelled) ||
                               cancelOutcome == static_cast<int>(Track::PreviewCancelOutcome::NothingToCancel),
                           "the mutation cancels (or finds nothing of) the preview lane's own build");
  drainLane(track, kPlayhead);

  // Nothing stale published: the served window is still the first revision **and** the old content.
  WindowIdentity afterStaleStep{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, afterStaleStep));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(first, afterStaleStep.playbackRevision,
                                   "a completion of the cancelled build publishes nothing");
  TEST_ASSERT_TRUE_MESSAGE(afterStaleStep == servedFirst,
                           "the served window is untouched by the cancelled build, field-for-field");
  const std::vector<EventKey> servedAfterStale =
      servedEventKeys(track, 0, track.playbackWindowEventCountForSlot(0));
  TEST_ASSERT_TRUE_MESSAGE(sameKeys(servedAfterStale, projectionEventKeys(beforeMutation)),
                           "and still holds the pre-mutation content — never a mix of the two");

  // The new revision publishes and is adopted; the pair (revision, content) is self-consistent.
  const MidiEventVec& mutated = sessionProjection();
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                            static_cast<int>(track.requestPreviewWindow(0, second, mutated.data(),
                                                                        mutated.size(), kPlayhead)),
                            "the new revision is admitted");
  drainLane(track, kPlayhead);
  // The replacement becomes the *ready successor* while the previous window keeps serving; the
  // production boundary (a traversal entry) is where it is swapped in, so the fixture takes it.
  TEST_ASSERT_TRUE_MESSAGE(track.adoptPreparedPlaybackWindow(0),
                           "the replacement publication is adopted at the boundary");
  WindowIdentity afterSecond{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, afterSecond));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(second, afterSecond.playbackRevision,
                                   "the published window carries the new revision");
  assertServedWindowIsSessionProjection(track, 0, "and the new revision's content, not the old one");
}

/// Fixture 2.12 (in-session undo/redo), driven by the **production** undo entries: `sessionUndo` /
/// `sessionRedo` on a real session push, and the served preview follows the session's content at the
/// new revision each time.
static void test_an_in_session_undo_and_redo_advance_the_preview_revision_and_the_served_content() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  commitLongLoop(track, 0, kLongLoopTicks, 6);
  readyLongLoopPlaying(track, 0);
  editManager.openNoteEditSession(track);

  // The session's own undo entry (the production push the geometry ops make), taken over the content
  // as it stands.
  TEST_ASSERT_TRUE_MESSAGE(editManager.pushSessionUndoOnKindChange(track, NoteEditKind::Move),
                           "the session pushes its own undo entry");
  const MidiEventVec contentBeforeEdit = sessionProjection();
  const uint32_t publishedBeforeEdit = publishSessionPreview(track, 0);

  const uint32_t afterEdit = mutateSessionAndFlush(track, 6000, 74);
  const MidiEventVec contentAfterEdit = sessionProjection();
  TEST_ASSERT_FALSE_MESSAGE(sameKeys(projectionEventKeys(contentAfterEdit),
                                     projectionEventKeys(contentBeforeEdit)),
                            "the edit really changed the session's content");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(afterEdit, publishSessionPreviewAt(track, 0, afterEdit),
                                   "the edited revision is published");
  assertServedWindowIsSessionProjection(track, 0, "the served preview is the edited session content");

  // Undo through the production entry, then the deferred refresh (which `sessionUndo` arms via
  // `Track::invalidateCaches`) — the revision advances and the served content reverts.
  TEST_ASSERT_TRUE_MESSAGE(editManager.sessionUndo(track), "the session's undo entry is applied");
  editManager.flushDeferredNoteEditDisplayRefresh(track);
  const uint32_t afterUndo = editManager.sessionPlaybackPreviewRevision();
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(afterEdit, afterUndo, "undo advances the preview revision");
  const MidiEventVec& undone = sessionProjection();
  TEST_ASSERT_TRUE_MESSAGE(sameKeys(projectionEventKeys(undone), projectionEventKeys(contentBeforeEdit)),
                           "the session's content is back to what the undo entry named");
  TEST_ASSERT_FALSE_MESSAGE(projectionHasNoteAt(undone, 6000, 74),
                            "and the edit the undo removed is gone");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(afterUndo, publishSessionPreviewAt(track, 0, afterUndo),
                                   "the undone revision is published");
  assertServedWindowIsSessionProjection(track, 0, "the served preview follows the undo");

  // Redo, same discipline at the preview boundary: the revision advances again and the served preview
  // is the session's own projection at that revision (never the undone one, never a mix).
  //
  // *Named limitation, not smoothed over:* this fixture's mutation writes its row straight into the
  // session store, while the session's **current-state model** — which the redo re-projects from — is
  // built from the rematerialized loop. So the redo restores the loop's own content rather than the
  // hand-pushed row, and this fixture therefore may not claim a byte-exact redo of its own edit. A
  // production *geometry* edit runs through the edit FSM, whose five state TUs cannot be linked in a
  // native suite at all (see the note below on `EditStates`), so that half of the redo clause belongs
  // with the production callers stage; the preview contract's own claim — the revision advances and
  // the served window is the session's projection, never stale content — is asserted here.
  TEST_ASSERT_TRUE_MESSAGE(editManager.sessionRedo(track), "the session's redo entry is applied");
  editManager.flushDeferredNoteEditDisplayRefresh(track);
  const uint32_t afterRedo = editManager.sessionPlaybackPreviewRevision();
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(afterUndo, afterRedo, "redo advances the preview revision again");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(afterRedo, publishSessionPreviewAt(track, 0, afterRedo),
                                   "the redone revision is published");
  assertServedWindowIsSessionProjection(track, 0, "the served preview follows the redo");
  TEST_ASSERT_EQUAL_UINT32(publishedBeforeEdit + 3u, editManager.sessionPlaybackPreviewRevision());
}

/// Fixture 2.12 (re-focus clause), driven by the **production focus route**: selecting another track
/// departs the note-edit session on the departing track and re-opens it on the new one, and the
/// departing track's preview is cancelled by *its own* lifecycle — a re-focus is not an exit, so no
/// committed handoff is owed and the served preview window is not torn.
static void test_a_selected_track_change_cancels_the_departing_tracks_preview_by_its_own_lifecycle() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  Track& other = trackManager.getTrack(1);
  commitLongLoop(track, 0, kLongLoopTicks, 6);
  commitLongLoop(other, 0, kLongLoopTicks, 6);
  readyLongLoopPlaying(track, 0);
  editManager.openNoteEditSession(track);
  publishSessionPreview(track, 0);
  WindowIdentity servedPreview{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, servedPreview));

  resetSlotChangeObservation();
  trackManager.setSelectedTrack(1);

  TEST_ASSERT_TRUE_MESSAGE(slotChangeObservation().count > 0u,
                           "the production focus route told the panel about the change");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(1u, slotChangeObservation().track, "and it named the incoming track");
  TEST_ASSERT_TRUE_MESSAGE(editManager.isNoteEditActive(),
                           "the real route re-opens the session on the new focus");
  TEST_ASSERT_TRUE_MESSAGE(&trackManager.getSelectedLoop(other) == &other.getLoop(0),
                           "the session now belongs to the incoming track's selected slot");

  // The departing track's preview is cancelled by the re-focus, and nothing else changes: the served
  // window is stale **by its own revision** rather than torn, and no handoff is owed.
  const int outcome = static_cast<int>(
      track.cancelPreviewWindowForSlot(0, Track::PreviewWindowCancelReason::SelectedTrackChanged));
  TEST_ASSERT_TRUE_MESSAGE(outcome == static_cast<int>(Track::PreviewCancelOutcome::NothingToCancel) ||
                               outcome == static_cast<int>(Track::PreviewCancelOutcome::BuildCancelled),
                           "a re-focus cancels the preview lane's own work, never a committed build");
  WindowIdentity afterRefocus{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, afterRefocus));
  TEST_ASSERT_TRUE_MESSAGE(afterRefocus == servedPreview,
                           "the served window is untouched field-for-field by the re-focus");
  TEST_ASSERT_FALSE_MESSAGE(track.previewWindowHandoffOwedForSlot(0),
                            "a re-focus is not an exit: no committed successor is owed");

  // Control: the incoming track has no preview of its own, so cancelling there is named as nothing.
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::PreviewCancelOutcome::NothingToCancel),
                            static_cast<int>(other.cancelPreviewWindowForSlot(
                                0, Track::PreviewWindowCancelReason::SelectedTrackChanged)),
                            "the incoming track's slot owns no preview yet");
}

/// Fixture 2.12 (exit and committed handoff), driven by the **production exit**: closing the session
/// makes the committed successor owed while the preview window keeps serving, a repeated exit is named
/// rather than re-requested, and the adopted committed window discharges the handoff — so preview is
/// never accepted as committed currency and the boundary is not a gap.
static void test_the_session_exit_owes_the_committed_handoff_and_the_preview_keeps_serving() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  Loop& loop = track.getLoop(0);
  commitLongLoop(track, 0, kLongLoopTicks, 6);
  readyLongLoopPlaying(track, 0);
  editManager.openNoteEditSession(track);
  publishSessionPreview(track, 0);
  WindowIdentity servedPreview{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, servedPreview));

  editManager.closeNoteEditSession(track);
  TEST_ASSERT_FALSE_MESSAGE(editManager.isNoteEditActive(), "the production exit closed the session");

  // Task 3.7a (I3): the exit now **performs** the handoff -- the session's owner calls the lane's cancel -- so the
  // obligation is asserted directly and the fixture's own call below is the *repeated* exit, which the lane names
  // rather than treating as a second handoff. (Re-pointed from the pre-I3 shape, where only this fixture's call made
  // the handoff owed; recorded in the slice's evidence.)
  TEST_ASSERT_TRUE_MESSAGE(track.previewWindowHandoffOwedForSlot(0),
                           "the production exit makes the committed successor owed");
  TEST_ASSERT_TRUE(track.previewWindowHandoffOwedForSlot(0));
  TEST_ASSERT_TRUE_MESSAGE(track.servedPlaybackWindowIsPreviewForSlot(0),
                           "the preview window keeps serving across the handoff boundary -- never a gap");
  WindowIdentity atHandoff{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, atHandoff));
  TEST_ASSERT_TRUE_MESSAGE(atHandoff == servedPreview, "and it is untouched field-for-field by the exit");
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::PreviewCancelOutcome::AlreadyHandedOff),
                            static_cast<int>(track.cancelPreviewWindowForSlot(
                                0, Track::PreviewWindowCancelReason::SessionExited)),
                            "a repeated exit is named rather than treated as a second handoff");

  // The committed successor needs a published root to gather from (the seam above); it is prepared
  // while the preview keeps serving, and adopted at the boundary.
  publishCommittedRootForSlot(loop, 64);
  SessionMidiEventVec scratch;
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                            static_cast<int>(track.requestPlaybackWindow(0, kPlayhead, scratch)),
                            "the committed lane admits the exit's successor");
  drainLane(track, kPlayhead);
  WindowIdentity beforeAdopt{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, beforeAdopt));
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<uint8_t>(PlaybackWindowOrigin::Preview),
                                  static_cast<uint8_t>(beforeAdopt.origin),
                                  "the committed successor is prepared, not yet serving");
  TEST_ASSERT_TRUE(track.previewWindowHandoffOwedForSlot(0));
  TEST_ASSERT_TRUE(track.adoptPreparedPlaybackWindow(0));
  WindowIdentity afterAdopt{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, afterAdopt));
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<uint8_t>(PlaybackWindowOrigin::Committed),
                                  static_cast<uint8_t>(afterAdopt.origin),
                                  "the committed successor serves once it is adopted");
  TEST_ASSERT_FALSE_MESSAGE(track.previewWindowHandoffOwedForSlot(0),
                            "and the handoff is discharged at that boundary");
  TEST_ASSERT_FALSE(track.servedPlaybackWindowIsPreviewForSlot(0));
}

/// Task 3.7a (I3) / task 3.13: **the maintenance turn is the requester**. The session's own revision is the lane's
/// currency, and the production focus-lane turn is what observes it: no fixture here calls `requestPreviewWindow`, so
/// a published preview window can only come from the firmware's own caller.
static void test_3_7a_i3_the_focus_lane_turn_requests_the_sessions_preview() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  commitLongLoop(track, 0, kLongLoopTicks, 6);
  readyLongLoopPlaying(track, 0);
  TEST_ASSERT_FALSE_MESSAGE(track.playbackWindowServesSlot(0), "control: nothing serves the slot before the session");

  editManager.openNoteEditSession(track);
  TEST_ASSERT_TRUE(editManager.isNoteEditActive());
  const uint32_t revision = editManager.sessionPlaybackPreviewRevision();
  TEST_ASSERT_GREATER_THAN_UINT32(0u, revision);

  // The seam did it: opening the session advances its preview revision, and the owner of that revision (the session
  // stack, at that seam) asked the lane for it. Nothing here calls the requester.
  drainLane(track, kPlayhead);

  WindowIdentity identity{};
  TEST_ASSERT_TRUE_MESSAGE(track.publishedPlaybackWindowIdentityForSlot(0, identity),
                           "the firmware's own turn requested and published the session's preview");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<uint8_t>(PlaybackWindowOrigin::Preview),
                                  static_cast<uint8_t>(identity.origin), "and it is identified as preview");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(revision, identity.playbackRevision,
                                   "at the session's own revision as the freeze token");
  TEST_ASSERT_EQUAL_UINT32(kLongLoopTicks, identity.interval.loopLengthTicks);
  assertServedWindowIsSessionProjection(track, 0, "the served preview is the session's projection");
  // Idempotent per revision: more turns request nothing further, so nothing is re-prepared.
  const uint32_t eventsServed = track.playbackWindowEventCountForSlot(0);
  drainLane(track, kPlayhead);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(eventsServed, track.playbackWindowEventCountForSlot(0),
                                   "a second drive of the same revision changes nothing");
}

/// Task 3.7a (I3): **the mutation edge is the firmware's**. A mutation advances the session's preview revision through
/// the production deferred-refresh flush, and the focus-lane turn cancels the superseded preparation by name and
/// requests the new revision -- the served preview is the new projection, never a mix of the two.
static void test_3_7a_i3_the_mutation_edge_requests_the_new_revision_through_the_turn() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  commitLongLoop(track, 0, kLongLoopTicks, 6);
  readyLongLoopPlaying(track, 0);
  editManager.openNoteEditSession(track);
  drainLane(track, kPlayhead);
  WindowIdentity first{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, first));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PlaybackWindowOrigin::Preview), static_cast<uint8_t>(first.origin));

  // **Two** mutations through the production refresh edge, with **no drain between them**: the first leaves a build in
  // flight for the revision it requested, and the second arrives while that build is still preparing. This is the shape
  // where the supersession cancel is load-bearing -- without it the first build completes and publishes, and the second
  // revision's request is then refused as a prepared successor, so the slot would end up serving stale content.
  const uint32_t intermediate = mutateSessionAndFlush(track, 3000, 72);
  const uint32_t next = mutateSessionAndFlush(track, 3100, 74);
  TEST_ASSERT_TRUE(intermediate != first.playbackRevision);
  TEST_ASSERT_TRUE(next != intermediate);

  // The flush seam observed each move: the superseded preparation is cancelled by name (nothing stale may publish) and
  // the newest revision is requested. The successor waits for its boundary, which the serving path owns.
  drainLane(track, kPlayhead);
  WindowIdentity afterTurn{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, afterTurn));
  if (afterTurn.playbackRevision != next) {
    TEST_ASSERT_TRUE_MESSAGE(track.adoptPreparedPlaybackWindow(0),
                             "the new revision is prepared and adopted at the boundary");
    TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, afterTurn));
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(next, afterTurn.playbackRevision,
                                   "the firmware requested the session's new revision");
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PlaybackWindowOrigin::Preview), static_cast<uint8_t>(afterTurn.origin));
  assertServedWindowIsSessionProjection(track, 0, "the served preview is the post-mutation projection, never a mix");
}

/// Task 3.7a (I3): **the exit through the production path**, with a preview window served: the session's owner yields
/// the source and owes the committed handoff, the served preview stays readable across the boundary, and the lane does
/// not re-request a preview afterwards. (The admission's committed-only read is asserted in the suite that links the
/// capture admission.)
static void test_3_7a_i3_the_exit_owes_the_handoff_and_never_re_requests_a_preview() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  commitLongLoop(track, 0, kLongLoopTicks, 6);
  readyLongLoopPlaying(track, 0);
  editManager.openNoteEditSession(track);
  drainLane(track, kPlayhead);
  TEST_ASSERT_TRUE(track.servedPlaybackWindowIsPreviewForSlot(0));
  TEST_ASSERT_TRUE(track.playbackWindowEventCountForSlot(0) > 0);
  // The admission's **committed-only** question is asserted where the capture admission is linked
  // (`test_runtime_working_set_ownership`: `test_3_7a_i3_the_admission_counts_committed_windows_only`), because this
  // suite's link set deliberately excludes the capture-admission boundary.

  // The production exit performs the handoff; the served preview keeps serving across the boundary.
  WindowIdentity servedPreview{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, servedPreview));
  editManager.closeNoteEditSession(track);
  TEST_ASSERT_FALSE(editManager.isNoteEditActive());
  TEST_ASSERT_TRUE_MESSAGE(track.previewWindowHandoffOwedForSlot(0),
                           "the session's owner made the committed successor owed");
  TEST_ASSERT_TRUE_MESSAGE(track.servedPlaybackWindowIsPreviewForSlot(0),
                           "and the preview window keeps serving: the handoff is a boundary, never a gap");
  WindowIdentity atHandoff{};
  TEST_ASSERT_TRUE(track.publishedPlaybackWindowIdentityForSlot(0, atHandoff));
  TEST_ASSERT_TRUE(atHandoff == servedPreview);
  // Turn-driven again: with the session gone the turn must not re-request a preview.
  drainLane(track, kPlayhead);
  TEST_ASSERT_TRUE(track.previewWindowHandoffOwedForSlot(0));
  TEST_ASSERT_TRUE(track.servedPlaybackWindowIsPreviewForSlot(0));
}

/// Task 3.7a (I3): **the re-focus hook**. Selecting another track yields the departing track's preview by its own
/// lifecycle event, and a re-focus is not an exit: nothing becomes owed.
static void test_3_7a_i3_a_refocus_yields_the_preview_without_owing_a_handoff() {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(0);
  commitLongLoop(track, 0, kLongLoopTicks, 6);
  readyLongLoopPlaying(track, 0);
  editManager.openNoteEditSession(track);
  drainLane(track, kPlayhead);
  TEST_ASSERT_TRUE(track.servedPlaybackWindowIsPreviewForSlot(0));
  // The focus change **itself** must yield the departing track's preview: asserted before anything else runs, so the
  // requester's own non-selected backstop cannot stand in for the hook (measured: a fixture that only drove the
  // requester passed with the hook removed, which is what made this read necessary).
  trackManager.setSelectedTrack(1);
  TEST_ASSERT_FALSE_MESSAGE(track.previewWindowSourceActiveForSlot(0),
                            "the focus hook yielded the departing track's preview");
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::PreviewCancelOutcome::NothingToCancel),
                            static_cast<int>(track.cancelPreviewWindowForSlot(
                                0, Track::PreviewWindowCancelReason::SelectedTrackChanged)),
                            "and a repeated re-focus is named, not treated as a second yield");
  TEST_ASSERT_FALSE_MESSAGE(track.previewWindowHandoffOwedForSlot(0),
                            "a re-focus is not an exit: nothing is owed");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_session_owns_the_preview_source_and_its_revision_is_the_lane_currency);
  RUN_TEST(test_a_mutation_between_build_grains_never_publishes_a_mixed_or_stale_preview);
  RUN_TEST(test_an_in_session_undo_and_redo_advance_the_preview_revision_and_the_served_content);
  RUN_TEST(test_a_selected_track_change_cancels_the_departing_tracks_preview_by_its_own_lifecycle);
  RUN_TEST(test_the_session_exit_owes_the_committed_handoff_and_the_preview_keeps_serving);
  // Task 3.7a (I3) / task 3.13's owed production callers: the firmware's own focus-lane turn requests and cancels the
  // session's preview (no fixture call to the requester), and the admission counts committed content only.
  RUN_TEST(test_3_7a_i3_the_focus_lane_turn_requests_the_sessions_preview);
  RUN_TEST(test_3_7a_i3_the_mutation_edge_requests_the_new_revision_through_the_turn);
  RUN_TEST(test_3_7a_i3_the_exit_owes_the_handoff_and_never_re_requests_a_preview);
  RUN_TEST(test_3_7a_i3_a_refocus_yields_the_preview_without_owing_a_handoff);
  return UNITY_END();
}
