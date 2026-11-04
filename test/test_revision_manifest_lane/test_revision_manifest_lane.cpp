// C5.3 (DEC-246 amendments 19-22): the committed rolling-window lane of the designated track reads RevisionManifest.
//
// The shared finalization suite's real Track/Loop/storage closure is compiled here with the rolling reader enabled.
// Each case installs a host reader owner (`FixtureReader`): a RevisionStore with the production-equal importer and the
// C5.2 window cursor, i.e. what `RevisionShadow` is on the flag-1 probe image. Its owner work (import, edit index) runs
// only when a case calls `serviceOwner`, which stands in for the registered idle children; the lane never calls it.
//
// Only this suite's own cases are registered; the included suite's cases are dead-stripped from the run.
#include "manifest_lane_harness.h"
#include <map>
#include "RollingSuccessorProbe.h"
#include <cstdlib>

extern "C" void setUp() {
  gWithholdLaneTurn = {};
  sharedCaptureFinalizationSuiteSetUp();
  uninstallReader();
}
extern "C" void tearDown() {
  uninstallReader();
  sharedCaptureFinalizationSuiteTearDown();
}

// 1. Current-boot committed content: the routed slot is served from the manifest only. Every served committed window
//    carries the exact manifest identity it read and no root; the root's window cursor is never used; the steady-state
//    traversal plays the take exactly, as the snapshot lane does for the same take.
void test_c5_3_current_boot_content_is_served_from_the_manifest_only() {
  // Oracle first: the same take through the snapshot lane (no reader installed).
  PlaybackObservation snapshotLane;
  {
    Track& t = trackManager.getTrack(0);
    recordFourBarTake(t);
    startTransport(t);
    play(t, 0, 3 * kLength, snapshotLane, 0);
    TEST_ASSERT_EQUAL_UINT32(0, snapshotLane.manifestServedTicks);
    TEST_ASSERT_GREATER_THAN_UINT32(0, snapshotLane.snapshotServedTicks);
  }
  sharedCaptureFinalizationSuiteSetUp();
  installReader();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordFourBarTake(t);
  reader().serviceOwner(loop);  // the owner imported the current source before playback
  startTransport(t);
  PlaybackObservation manifestLane;
  play(t, 0, 3 * kLength, manifestLane);

  char detail[256];
  snprintf(detail, sizeof(detail), "served manifest=%u snapshot=%u cursor=%u begun=%u completed=%u refused=%u gated=%u",
           manifestLane.manifestServedTicks, manifestLane.snapshotServedTicks, manifestLane.snapshotCursorTicks,
           CommittedManifestLane::gCounters.windowsBegun, CommittedManifestLane::gCounters.windowsCompleted,
           CommittedManifestLane::gCounters.legacyRefused, CommittedManifestLane::gCounters.legacyGated);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, manifestLane.manifestServedTicks, detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, manifestLane.snapshotServedTicks, detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, manifestLane.snapshotCursorTicks, detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, manifestLane.wrongIdentityTicks, detail);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, CommittedManifestLane::gCounters.windowsBegun, detail);
  // The exact identity: the served window names the manifest the reader published, nothing else.
  PlaybackViewSourceIdentity served{};
  PlaybackCoverageInterval coverage{};
  TEST_ASSERT_TRUE(t.publishedPlaybackWindowForSlot(0, served, coverage));
  TEST_ASSERT_EQUAL_UINT32(reader().identity(), served.manifestIdentity);
  TEST_ASSERT_NULL(served.snapshotIdentity);
  TEST_ASSERT_EQUAL_UINT32(loop.playbackRevision, served.playbackRevision);
  // Inside every interval a manifest window served, the MIDI is the snapshot lane's for the same ticks, event for event.
  // Outside them the routed lane is silent by contract (amendment 23: lane-owned gaps are named misses, never merged).
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(kBars, noteOnsInTraversal(snapshotLane, 2).size(), "the oracle plays one note per bar");
  const auto servedAt = [&](uint32_t tick) {
    return std::binary_search(manifestLane.manifestTicks.begin(), manifestLane.manifestTicks.end(), tick);
  };
  std::vector<std::tuple<uint32_t, uint8_t, uint8_t>> expected, actual, outside;
  for (const auto& [tick, e] : snapshotLane.sent)
    if (servedAt(tick)) expected.emplace_back(tick, static_cast<uint8_t>(e.type), e.data.noteData.note);
  for (const auto& [tick, e] : manifestLane.sent)
    (servedAt(tick) ? actual : outside).emplace_back(tick, static_cast<uint8_t>(e.type), e.data.noteData.note);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, expected.size(), detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected.size(), actual.size(), detail);
  TEST_ASSERT_TRUE_MESSAGE(expected == actual, detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, outside.size(), "a routed-lane gap sounded: something other than a window played");
}

// 2. Manifest unavailable: the owner owes the import, the build gate names it, the tick sees no qualified window and
//    builds nothing -- no reader call, no window read, no merged (root) fallback. Once the owner imports, it is served.
void test_c5_3_owed_manifest_is_a_named_miss_and_the_tick_builds_nothing() {
  installReader();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordFourBarTake(t);
  startTransport(t);
  PlaybackObservation owed;
  play(t, 0, kLength, owed, /*ownerEvery=*/0);
  TEST_ASSERT_EQUAL_UINT32(0, reader().imports);
  TEST_ASSERT_EQUAL_UINT32(0, reader().beginCalls);
  TEST_ASSERT_EQUAL_UINT32(0, owed.sent.size());
  TEST_ASSERT_EQUAL_UINT32(0, owed.manifestServedTicks + owed.snapshotServedTicks + owed.snapshotCursorTicks);
  TEST_ASSERT_GREATER_THAN_UINT32(0, CommittedManifestLane::gCounters.legacyRefused);
  // Named: the servable state is the owed import, and no demand reached a build (the requester refuses an unservable
  // source before it positions one; a demand retained earlier would be refused by the gate's manifest term).
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(LaneState::ImportOwed), CommittedManifestLane::gServable.state);
  const PlaybackBuildRefusal gate = t.playbackBuildGateRefusal(0, clockManager.getPlaybackDeadlineClock());
  TEST_ASSERT_TRUE(gate == PlaybackBuildRefusal::NoDemand || gate == PlaybackBuildRefusal::ManifestImportOwed);
  // The interrupt tick alone (DEC-241): no reader call at all, and nothing is built or owed to the merged owner.
  const uint32_t calls = reader().currentCalls;
  const uint32_t misses = PlaybackTickContext::gState.misses;
  for (uint32_t tick = kLength; tick < kLength + 64; ++tick) {
    nativePlaybackDeadlineClock = {tick, 1};
    clockManager.assignCurrentTickSilently(tick);
    PlaybackTickContext::gState.inInterrupt = true;
    t.playMidiEvents(tick, true);
    PlaybackTickContext::gState.inInterrupt = false;
  }
  TEST_ASSERT_EQUAL_UINT32(calls, reader().currentCalls);
  TEST_ASSERT_EQUAL_UINT32(misses, PlaybackTickContext::gState.misses);
  TEST_ASSERT_EQUAL_UINT8(0, PlaybackTickContext::gState.owed[0]);
  TEST_ASSERT_EQUAL_UINT32(0, reader().beginCalls);
  // The owner prepares the manifest; the lane then serves it.
  PlaybackObservation served;
  play(t, kLength + 64, 4 * kLength, served);
  TEST_ASSERT_EQUAL_UINT32(1, reader().imports);
  TEST_ASSERT_GREATER_THAN_UINT32(0, served.manifestServedTicks);
  TEST_ASSERT_GREATER_THAN_UINT32(0, noteOnsOf(served).size());
  for (const auto& [phase, pitch] : noteOnsOf(served)) TEST_ASSERT_TRUE(isTakeNote(phase, pitch));
  (void)loop;
}

// 3. Content that is not committed yet (a slot whose restore has not applied is the same state: no passes) is
//    `NoCommittedSource`; once production commits it the source key moves, the owner imports, and the lane serves it.
void test_c5_3_no_committed_source_then_imported_content_is_served() {
  installReader();
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  TEST_ASSERT_EQUAL(LaneState::NoCommittedSource, t.refreshManifestLane(0));
  TEST_ASSERT_EQUAL(PlaybackBuildRefusal::NoDemand, t.playbackBuildGateRefusal(0, clockManager.getPlaybackDeadlineClock()));
  Loop& loop = recordFourBarTake(t);
  TEST_ASSERT_EQUAL(LaneState::ImportOwed, t.refreshManifestLane(0));
  reader().serviceOwner(loop);
  TEST_ASSERT_EQUAL(LaneState::Ready, t.refreshManifestLane(0));
  TEST_ASSERT_EQUAL_UINT32(reader().identity(), CommittedManifestLane::gServable.identity);
  startTransport(t);
  PlaybackObservation obs;
  play(t, 0, 3 * kLength, obs);
  TEST_ASSERT_GREATER_THAN_UINT32(0, obs.manifestServedTicks);
  TEST_ASSERT_EQUAL_UINT32(0, obs.snapshotServedTicks + obs.snapshotCursorTicks);
  TEST_ASSERT_GREATER_THAN_UINT32(0, noteOnsOf(obs).size());
  for (const auto& [phase, pitch] : noteOnsOf(obs)) TEST_ASSERT_TRUE(isTakeNote(phase, pitch));
}

// 4. The manifest moves while a window read is in progress: that read's candidate is retired by name and never
//    published; every window served afterwards carries the new identity.
void test_c5_3_a_manifest_move_during_preparation_never_publishes_the_stale_candidate() {
  installReader();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordFourBarTake(t);
  reader().serviceOwner(loop);
  const uint32_t first = reader().identity();
  startTransport(t);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  bool moved = false;
  PlaybackObservation obs;
  play(t, 0, 3 * kLength, obs, 1, [&](uint32_t) {
    if (moved || CommittedManifestLane::gWindowOwner != &runtime ||
        runtime.coveragePreparation != LoopPlaybackRuntime::CoveragePreparation::Counting) return;
    TEST_ASSERT_EQUAL_UINT32(first, runtime.frozenViewRequest.source.manifestIdentity);
    reader().imported = false;  // the owner re-imports the same source: a new manifest identity, same content
    reader().serviceOwner(loop);
    moved = true;
  });
  TEST_ASSERT_TRUE_MESSAGE(moved, "the fixture needs a read in progress");
  const uint32_t second = reader().identity();
  TEST_ASSERT_NOT_EQUAL_UINT32(first, second);
  TEST_ASSERT_GREATER_THAN_UINT32(0, obs.manifestServedTicks);
  for (uint32_t identity : obs.servedIdentities)
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(second, identity, "a window read from the moved manifest was served");
  TEST_ASSERT_EQUAL_UINT32(0, obs.wrongIdentityTicks);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(
      1, CommittedManifestLane::gCounters.owed[static_cast<uint8_t>(LaneState::SourceMoved)]);
}

// 5. The manifest has a hide and its edit index is owed: the lane names `IndexOwed`, reads nothing and never scans;
//    once the owner publishes the index the lane serves it, and the hidden note never sounds.
void test_c5_3_index_owed_then_ready_serves_the_hide_without_scanning() {
  installReader();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordFourBarTake(t);
  deleteTakeNote(loop, 62);
  reader().serviceOwner(loop);  // import only: the index is owed
  TEST_ASSERT_TRUE(reader().store.editIndexOwed());
  TEST_ASSERT_EQUAL(LaneState::IndexOwed, t.refreshManifestLane(0));
  startTransport(t);
  PlaybackObservation owed;
  play(t, 0, kLength, owed, /*ownerEvery=*/0);
  TEST_ASSERT_EQUAL_UINT32(0, reader().beginCalls);
  TEST_ASSERT_EQUAL_UINT32(0, owed.sent.size());
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(LaneState::IndexOwed), CommittedManifestLane::gServable.state);
  PlaybackObservation served;
  play(t, kLength, 5 * kLength, served);
  TEST_ASSERT_GREATER_THAN_UINT32(0, reader().indexSteps);
  TEST_ASSERT_TRUE(reader().store.editIndexReady(reader().store.current()));
  TEST_ASSERT_GREATER_THAN_UINT32(0, served.manifestServedTicks);
  TEST_ASSERT_GREATER_THAN_UINT32(0, noteOnsOf(served).size());
  for (const auto& [phase, pitch] : noteOnsOf(served)) {
    TEST_ASSERT_TRUE(isTakeNote(phase, pitch));
    TEST_ASSERT_NOT_EQUAL_UINT8(62, pitch);
  }
}

// 6. Both edit-index slots are held by pinned readers: the newest manifest's index is `IndexCapacityDenied`, named on
//    the lane as `CapacityDenied`, and neither pinned index is evicted. Releasing one pin lets the owner build it.
void test_c5_3_both_index_slots_pinned_is_capacity_denied_without_eviction() {
  installReader();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordFourBarTake(t);
  RevisionStore::Store<Cap>& store = reader().store;
  RevisionStore::ManifestId pinned[2] = {RevisionStore::kNoManifest, RevisionStore::kNoManifest};
  const uint8_t pitches[3] = {61, 62, 63};
  for (int i = 0; i < 3; ++i) {
    deleteTakeNote(loop, pitches[i]);
    reader().serviceOwner(loop);  // import
    if (i == 2) break;
    reader().serviceOwner(loop);  // index
    TEST_ASSERT_TRUE(store.editIndexReady(store.current()));
    pinned[i] = store.pin(store.current());
  }
  TEST_ASSERT_EQUAL(LaneState::CapacityDenied, t.refreshManifestLane(0));
  TEST_ASSERT_TRUE(store.editIndexReady(pinned[0]));
  TEST_ASSERT_TRUE(store.editIndexReady(pinned[1]));
  TEST_ASSERT_EQUAL_UINT32(0, reader().beginCalls);
  store.unpin(pinned[0]);
  reader().serviceOwner(loop);
  TEST_ASSERT_EQUAL(LaneState::Ready, t.refreshManifestLane(0));
  TEST_ASSERT_TRUE(store.editIndexReady(pinned[1]));
  store.unpin(pinned[1]);
}

// 7. Production commits a change while a manifest window serves. The served copy stays immutable to its end (the
//    rolling contract); the source move is named, no window is begun on the old identity afterwards, and every later
//    window carries the new manifest, without the deleted note.
void test_c5_3_a_source_move_while_serving_cannot_publish_a_stale_window() {
  installReader();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordFourBarTake(t);
  reader().serviceOwner(loop);
  const uint32_t first = reader().identity();
  startTransport(t);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  uint32_t movedAt = 0;
  size_t begunBefore = 0;
  PlaybackObservation obs;
  play(t, 0, 5 * kLength, obs, 1, [&](uint32_t tick) {
    if (movedAt != 0 || runtime.rolling.phase != LoopPlaybackRuntime::RollingPhase::Serving) return;
    deleteTakeNote(loop, 63);  // the key moves: ImportOwed until the owner's next turn imports it
    TEST_ASSERT_EQUAL(LaneState::ImportOwed, t.refreshManifestLane(0));
    begunBefore = reader().begunIdentities.size();
    movedAt = tick;
  });
  TEST_ASSERT_NOT_EQUAL_UINT32(0, movedAt);
  const uint32_t second = reader().identity();
  TEST_ASSERT_NOT_EQUAL_UINT32(first, second);
  TEST_ASSERT_GREATER_THAN_UINT32(begunBefore, reader().begunIdentities.size());
  for (size_t i = begunBefore; i < reader().begunIdentities.size(); ++i)
    TEST_ASSERT_EQUAL_UINT32(second, reader().begunIdentities[i]);
  TEST_ASSERT_EQUAL_UINT32(2, obs.servedIdentities.size());
  TEST_ASSERT_EQUAL_UINT32(first, obs.servedIdentities[0]);
  TEST_ASSERT_EQUAL_UINT32(second, obs.servedIdentities[1]);
  // Pitch 63 may sound only from the window that was already serving at the move (its immutable copy).
  for (const auto& [tick, e] : obs.sent) {
    if (!e.isNoteOn() || e.data.noteData.note != 63) continue;
    TEST_ASSERT_TRUE_MESSAGE(tick < movedAt + 2 * kBar, "the deleted note sounded from a window read after the move");
  }
}

// 8. A note the last served window left sounding, whose NoteOff falls in a routed-lane gap, is closed when the gap
//    begins (the gap is silent; its NoteOff is never emitted late or skipped).
void test_c5_3_a_note_sounding_into_a_gap_is_closed_not_left_hanging() {
  installReader();
  Track& t = trackManager.getTrack(0);
  constexpr uint32_t length = 5 * kBar;
  // The routed slot's windows follow the equal partition (DEC-246 amendment 27): 5 bars are 3 x 1,280 ticks, seams at
  // 1,280 and 2,560. Read from the partition itself, so the notes cross the seams the lane actually uses.
  const uint32_t seam1 = rollingWindowLengthAt(0, length, true);
  const uint32_t seam2 = seam1 + rollingWindowLengthAt(seam1, length, true);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60);
  t.noteOn(1, 71, 100, seam1 - 120);  // each crosses a window boundary, so a window ends with it sounding
  t.noteOff(1, 71, 0, seam1 + 150);
  t.noteOn(1, 70, 100, seam2 - 120);
  t.noteOff(1, 70, 0, seam2 + 150);
  t.noteOn(1, 72, 100, 4 * kBar + 300);  // content in bar 5, so the take keeps its 5-bar length
  t.noteOff(1, 72, 0, 4 * kBar + 330);
  clockManager.assignCurrentTickSilently(length);
  t.stopRecordingToStopped(length);
  driveStopToRetirement(t);
  Loop& loop = t.getLoop(0);
  TEST_ASSERT_EQUAL_UINT32(length, loop.loopLengthTicks);
  reader().serviceOwner(loop);
  startTransport(t);
  PlaybackObservation obs;
  play(t, 0, 6 * length, obs);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  int open70 = 0;  // both held notes, by pitch
  int open71 = 0;
  for (const auto& [tick, e] : obs.sent) {
    int* open = e.data.noteData.note == 70 ? &open70 : e.data.noteData.note == 71 ? &open71 : nullptr;
    if (open == nullptr) continue;
    if (e.isNoteOn()) ++*open;
    else if (e.isNoteOff() && *open > 0) --*open;
  }
  open70 += open71;
  TEST_ASSERT_GREATER_THAN_UINT32(0, obs.manifestServedTicks);
  int ons = 0, offs = 0;
  for (const auto& [tick, e] : obs.sent)
    if (e.data.noteData.note == 70 || e.data.noteData.note == 71) (e.isNoteOn() ? ons : offs) += 1;
  char counts[160];
  snprintf(counts, sizeof(counts), "ons=%d offs=%d open=%d closures=%u ledger=%zu", ons, offs, open70,
           CommittedManifestLane::gCounters.gapClosures, runtime.ledger.openCount());
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, open70, counts);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, CommittedManifestLane::gCounters.gapClosures, counts);
  // The run ends inside a gap or a window; either way nothing the lane emitted is left open past a gap entry.
  TEST_ASSERT_TRUE(runtime.ledger.openCount() <= 1);
}

// 9. A one-bar slot's cold full-loop candidate prepared from the root before a reader was installed: once the slot is
//    routed, that snapshot ready is stale and cancelled, and the lane re-prepares it from the manifest.
void test_c5_3_a_snapshot_cold_ready_is_cancelled_once_the_slot_is_routed() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60);
  clockManager.assignCurrentTickSilently(kBar);
  t.stopRecordingToStopped(kBar);
  driveStopToRetirement(t);
  Loop& loop = t.getLoop(0);
  TEST_ASSERT_EQUAL_UINT32(kBar, loop.loopLengthTicks);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  const auto readyView = [&]() -> const RuntimePlaybackView* {
    const auto* set = runtime.viewSetIfPrewarmed();
    if (set == nullptr || set->lifecycle.ready >= set->views.size()) return nullptr;
    const auto& v = set->views[set->lifecycle.ready];
    return v.complete && !v.published ? &v : nullptr;
  };
  nativePlaybackDeadlineClock = {0, 0};
  const auto ownerTurn = [&]() {  // the cold owner shape before the first audible edge
    (void)t.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock);
    (void)t.stepPlaybackViewMaintenance(0, nullptr, false, nativePlaybackDeadlineClock);
  };
  for (unsigned turn = 0; turn < 4096 && readyView() == nullptr; ++turn) ownerTurn();
  char state[160];
  snprintf(state, sizeof(state), "roll=%u ready=%u", unsigned(runtime.rolling.phase), readyView() != nullptr);
  TEST_ASSERT_NOT_NULL_MESSAGE(readyView(), state);
  TEST_ASSERT_NOT_NULL(readyView()->source.snapshotIdentity);  // prepared from the root
  installReader();
  reader().serviceOwner(loop);
  for (unsigned turn = 0; turn < 4096; ++turn) {
    ownerTurn();
    const RuntimePlaybackView* ready = readyView();
    if (ready != nullptr && ready->source.manifestIdentity != 0) break;
  }
  TEST_ASSERT_NOT_NULL(readyView());
  TEST_ASSERT_NULL(readyView()->source.snapshotIdentity);
  TEST_ASSERT_EQUAL_UINT32(reader().identity(), readyView()->source.manifestIdentity);
}

// 10. A dense take on the routed track. The routed slot never reads the root, so its first window is priced from the
//     manifest's own densest span (the reader owner's `densestWindowRows`), not the declared 7-grain floor that a dense
//     window (~14 grains) cannot meet -- which repositioned the frame twice and then blocked it for good
//     (`win_blocked_by_budget`). Served from the manifest only; inside every served interval the MIDI is the snapshot
//     lane's for the same take.
// The dense window's own priced lead (~55 grains after qualification) puts its first position about ten traversals
// out; the merged fallback covers the traversals in between on the snapshot lane.
constexpr uint32_t kDenseTraversals = 16;

void test_c5_3_a_dense_take_is_served_from_the_manifest() {
  PlaybackObservation snapshotLane;
  {
    Track& t = trackManager.getTrack(0);
    recordTake(t, kBars, 32);
    startTransport(t);
    play(t, 0, kDenseTraversals * kLength, snapshotLane, 0);
    TEST_ASSERT_GREATER_THAN_UINT32(0, snapshotLane.snapshotServedTicks);
  }
  sharedCaptureFinalizationSuiteSetUp();
  installReader();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordTake(t, kBars, 32);
  reader().serviceOwner(loop);
  startTransport(t);
  PlaybackObservation lane;
  play(t, 0, kDenseTraversals * kLength, lane);
  const auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  char detail[200];
  snprintf(detail, sizeof(detail), "served manifest=%u snapshot=%u cursor=%u phase=%u blockedBy=%u begun=%u",
           lane.manifestServedTicks, lane.snapshotServedTicks, lane.snapshotCursorTicks,
           unsigned(runtime.rolling.phase), unsigned(runtime.rolling.blockedBy),
           CommittedManifestLane::gCounters.windowsBegun);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, lane.manifestServedTicks, detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, lane.snapshotServedTicks, detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, lane.snapshotCursorTicks, detail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, lane.wrongIdentityTicks, detail);
  const auto servedAt = [&](uint32_t tick) {
    return std::binary_search(lane.manifestTicks.begin(), lane.manifestTicks.end(), tick);
  };
  std::vector<std::tuple<uint32_t, uint8_t, uint8_t>> expected, actual;
  for (const auto& [tick, e] : snapshotLane.sent)
    if (servedAt(tick)) expected.emplace_back(tick, static_cast<uint8_t>(e.type), e.data.noteData.note);
  for (const auto& [tick, e] : lane.sent)
    if (servedAt(tick)) actual.emplace_back(tick, static_cast<uint8_t>(e.type), e.data.noteData.note);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(32, expected.size(), detail);
  TEST_ASSERT_TRUE_MESSAGE(expected == actual, detail);
}

// Gate A (owner ruling 2026-11-03, device `captures/session_20261103_211405.log`): the owner's user-facing contract --
// a committed take stays audible on the routed slot through an overdub and its deferred stop, without waiting for a
// published root: every traversal plays every take note. RED today, and against amendment 23 as written (lane-owned
// gaps are silent named misses until C5.3b successor preparation lands): measured here, the routed lane plays bars
// 2-3 of every other traversal in steady state, and nothing in the traversal after the stop. Registered only with
// `ROUTED_AUDIBILITY_RED=1` until the owner rules on amendment 23 / C5.3b.
/// The probe image's successor requester (`RollingSuccessorProbe::installAtBoot`): one successor per served window with
/// only the DEC-147 lead price waived. The cold-start cases run with it, so steady state is continuous (as on the device)
/// and the only silences left are a chain's cold starts.
void enableProbeImageSuccessors() {
  RollingSuccessorProbe::gKnobs = {};
  RollingSuccessorProbe::gCounters = {};  // records are keyed by absolute deadline; every case restarts at tick 0
  RollingSuccessorProbe::gKnobs.enabled = true;
  RollingSuccessorProbe::gKnobs.bypassLeadPrice = true;
  RollingSuccessorProbe::gKnobs.requestLeadTicks = UINT32_MAX;
}

/// The traversals in [from, to) play every take note (`isTakeNote`: one per bar).
void assertEveryTakeNotePlayed(const PlaybackObservation& obs, uint32_t from, uint32_t to) {
  for (uint32_t pass = from; pass < to; ++pass) {
    const auto ons = noteOnsInTraversal(obs, pass);
    for (uint32_t bar = 0; bar < kBars; ++bar) {
      bool heard = false;
      for (const auto& [phase, pitch] : ons) heard |= isTakeNote(phase, pitch) && phase / kBar == bar;
      char msg[96];
      snprintf(msg, sizeof(msg), "traversal %u: the take's bar-%u note was not played", unsigned(pass), unsigned(bar));
      TEST_ASSERT_TRUE_MESSAGE(heard, msg);
    }
  }
}

// Cold start, transport start (owner ruling 2026-11-03, `2026-11-03-routed-cold-start-design.md`): the routed slot's
// take is audible from the first traversal, with the probe image's successors on. RED today: traversal 0 is silent and
// traversal 1 starts at bar 2 (the cold frame waits its DEC-147 lookahead, and no launch window is bound).
void test_c5_3_cold_start_transport_start_plays_the_first_traversal() {
  installReader();
  enableProbeImageSuccessors();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordFourBarTake(t);
  reader().serviceOwner(loop);
  startTransport(t);
  PlaybackObservation obs;
  play(t, 0, 3 * kLength, obs);
  RollingSuccessorProbe::gKnobs = {};
  assertEveryTakeNotePlayed(obs, 0, 3);
}

void test_c5_3_gate_a_take_stays_audible_through_an_overdub_stop() {
  installReader();
  enableProbeImageSuccessors();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordFourBarTake(t);
  reader().serviceOwner(loop);
  startTransport(t);
  PlaybackObservation obs;
  play(t, 0, 2 * kLength, obs);
  t.startOverdubbing(2 * kLength);
  TEST_ASSERT_TRUE(t.isOverdubbing());
  const auto stepStopJob = [&](uint32_t) {
    if (t.hasPendingDeferredOverdubStop()) TrackCaptureStopTestAccess::step(t);
  };
  play(t, 2 * kLength, 2 * kLength + 200, obs, 1, stepStopJob);
  t.noteOn(1, 80, 100, 2 * kLength + 200);
  play(t, 2 * kLength + 200, 2 * kLength + 260, obs, 1, stepStopJob);
  t.noteOff(1, 80, 0, 2 * kLength + 260);
  play(t, 2 * kLength + 260, 3 * kLength - 40, obs, 1, stepStopJob);
  clockManager.assignCurrentTickSilently(3 * kLength - 40);
  t.stopOverdubbing();
  play(t, 3 * kLength - 40, 7 * kLength, obs, 1, stepStopJob);
  RollingSuccessorProbe::gKnobs = {};
  assertEveryTakeNotePlayed(obs, 2, 7);
}

/// The first tick at which a complete window of the loop's current revision covering `phase` exists in any role,
/// once the stop job has retired (its commit is the last revision move), 0 when none.
struct CompletedWindowWitness {
  uint32_t phase = 0;
  uint32_t completedAt = 0;
};

/// One overdub (one note at bar 0 + 200 of traversal 2) on the routed take, stopped at loop tick `stopTick`; the
/// transport then plays through traversal 6 with the stop job serviced every tick. Production pricing: no probe knobs.
/// `witness`, when given, records when a complete current-revision window covering its phase first exists.
PlaybackObservation overdubThenStopAt(Track& t, Loop& loop, uint32_t stopTick,
                                      CompletedWindowWitness* witness = nullptr) {
  reader().serviceOwner(loop);
  startTransport(t);
  PlaybackObservation obs;
  const auto stepStopJob = [&](uint32_t) {
    if (t.hasPendingDeferredOverdubStop()) TrackCaptureStopTestAccess::step(t);
  };
  play(t, 0, 2 * kLength, obs);
  t.startOverdubbing(2 * kLength);
  play(t, 2 * kLength, 2 * kLength + 200, obs, 1, stepStopJob);
  t.noteOn(1, 80, 100, 2 * kLength + 200);
  play(t, 2 * kLength + 200, 2 * kLength + 260, obs, 1, stepStopJob);
  t.noteOff(1, 80, 0, 2 * kLength + 260);
  play(t, 2 * kLength + 260, stopTick, obs, 1, stepStopJob);
  clockManager.assignCurrentTickSilently(stopTick);
  t.stopOverdubbing();
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  play(t, stopTick, 7 * kLength, obs, 1, [&](uint32_t tick) {
    stepStopJob(tick);
    if (witness == nullptr || witness->completedAt != 0) return;
    const auto* set = runtime.viewSetIfPrewarmed();
    if (set == nullptr) return;
    for (const auto& v : set->views)
      if (v.complete && !t.hasPendingDeferredOverdubStop() && v.source.playbackRevision == loop.playbackRevision &&
          v.coverage.contains(witness->phase))
        witness->completedAt = tick;
  });
  return obs;
}

bool takeNotePlayed(const PlaybackObservation& obs, uint32_t pass, uint32_t bar) {
  for (const auto& [phase, pitch] : noteOnsInTraversal(obs, pass))
    if (isTakeNote(phase, pitch) && phase / kBar == bar) return true;
  return false;
}

/// Every role names a distinct physical view (`kNoView` excepted).
void assertRolesDistinct(const RuntimePlaybackViewLifecycle& lc) {
  const uint8_t roles[] = {lc.current, lc.ready, lc.build, lc.idle, lc.retained};
  for (size_t i = 0; i < 5; ++i)
    for (size_t j = i + 1; j < 5; ++j)
      if (roles[i] != RuntimePlaybackViewLifecycle::kNoView && roles[i] == roles[j] &&
          !(i == 0 && j == 4))  // `current` may legitimately be the retained view (installPreparedCurrent)
        TEST_FAIL_MESSAGE("two roles name the same physical view");
}

// Slice 1 RED (owner ruling 2026-11-03): an orphaned `retained` role. After a Record stop retires, the Record return
// window's view is emptied while its `retained` marker survives (every later preserving reset re-stamps it), so in
// routed steady state one of the three physical views is never usable again. A view may keep a role only while it has
// a legitimate owner or a qualified serving/return purpose: once its coverage and its Record-side owner are both gone,
// `retained` must not name it. Measured today: `retained` = view 1, complete=0, coverage 0+0, no owner, for the run.
void test_c5_3_orphaned_retained_role_is_released() {
  installReader();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordFourBarTake(t);
  reader().serviceOwner(loop);
  startTransport(t);
  PlaybackObservation obs;
  play(t, 0, 2 * kLength, obs);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  const auto* set = runtime.viewSetIfPrewarmed();
  TEST_ASSERT_NOT_NULL(set);
  TEST_ASSERT_FALSE(runtime.hasBoundRecordReturnWindowForPendingJob());  // no Record-side owner remains
  const uint8_t retained = set->lifecycle.retained;
  TEST_ASSERT_TRUE_MESSAGE(retained == RuntimePlaybackViewLifecycle::kNoView || set->views[retained].complete,
                           "a retained role names an empty view with no remaining owner");
  assertRolesDistinct(set->lifecycle);
}

// Slice 1 guard: a *valid* retained view stays protected. While a Record take is recording, the raw first bar is the
// retained view (complete, `rawCoveragePrepared`); no orphan release may touch it.
void test_c5_3_a_valid_retained_record_bar_stays_protected() {
  installReader();
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  bool sawRetainedBar = false;
  for (uint32_t tick = 1; tick < kBar + 200; ++tick) {
    clockManager.assignCurrentTickSilently(tick);
    nativePlaybackDeadlineClock = {tick, 1};
    TrackCaptureStopTestAccess::step(t);
    const auto* set = runtime.viewSetIfPrewarmed();
    if (set == nullptr || set->lifecycle.retained >= set->views.size()) continue;
    const auto& v = set->views[set->lifecycle.retained];
    TEST_ASSERT_TRUE_MESSAGE(v.complete || runtime.hasBoundRecordReturnWindowForPendingJob() || t.isRecording(),
                             "a retained view without coverage or owner during recording");
    sawRetainedBar |= v.complete;
    assertRolesDistinct(set->lifecycle);
  }
  TEST_ASSERT_TRUE_MESSAGE(sawRetainedBar, "no retained Record bar observed: the guard would be vacuous");
  TEST_ASSERT_TRUE(t.isRecording());
}

/// Record-stop liveness (owner ruling 2026-11-04): the device gesture on the routed slot -- arm while stopped, Start,
/// record the take with the transport running, stop **into PLAYING** on the grid -- then play on, with the Record stop
/// job and the Snapshot owner serviced the way the main loop services them.
struct RecordStopTrace {
  uint32_t stopTick = 0;
  uint32_t staleRetiredAt = 0;   // first tick the frame was Blocked for ServingViewSourceStale
  uint32_t readyOccupiedFrom = 0, readyOccupiedLongest = 0, readyOccupiedRun = 0;
};

PlaybackObservation recordIntoPlayingThenPlay(Track& t, uint32_t traversals, RecordStopTrace& trace,
                                              const std::function<void(uint32_t)>& perTick = {}) {
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  nativePlaybackDeadlineClock = {0, 1};
  clockManager.assignCurrentTickSilently(0);
  start(t, 0, false, 0, 60);
  PlaybackObservation obs;
  play(t, 1, kLength, obs, 1, [&](uint32_t tick) {
    if (tick % kBar == 12 && tick / kBar >= 1) t.noteOn(1, static_cast<uint8_t>(60 + tick / kBar), 100, tick);
    if (tick % kBar == 96 && tick / kBar >= 1) t.noteOff(1, static_cast<uint8_t>(60 + tick / kBar), 0, tick);
  });
  clockManager.assignCurrentTickSilently(kLength);
  nativePlaybackDeadlineClock = {kLength, 1};
  t.stopRecording(kLength);
  trace.stopTick = kLength;
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  play(t, kLength, (1 + traversals) * kLength, obs, 1, [&](uint32_t tick) {
    if (t.hasSnapshotPublicationBacklogOnAnySlot())
      t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
    if (t.hasStopInFlight() || TrackCaptureStopTestAccess::recordWorkPending(t)) TrackCaptureStopTestAccess::step(t);
    const auto& frame = runtime.rolling;
    if (trace.staleRetiredAt == 0 && frame.phase == LoopPlaybackRuntime::RollingPhase::Blocked &&
        frame.blockedBy == static_cast<uint8_t>(PlaybackTickContext::RouteReject::ServingViewSourceStale))
      trace.staleRetiredAt = tick;
    if (runtime.coverageRoleRefusal(false) == PlaybackCoverageRoleRefusal::ReadyRoleOccupied &&
        frame.phase == LoopPlaybackRuntime::RollingPhase::Idle) {
      if (trace.readyOccupiedRun++ == 0) trace.readyOccupiedFrom = tick;
      trace.readyOccupiedLongest = std::max(trace.readyOccupiedLongest, trace.readyOccupiedRun);
    } else {
      trace.readyOccupiedRun = 0;
    }
    if (perTick) perTick(tick);
  });
  return obs;
}

/// Per-tick ready-role judgement after the Record stop. A `ready` window is **late** when it became complete only after
/// the playhead had already consumed its start boundary (its own edge is gone until the next traversal); it is
/// **timely** when it was complete before its start.
struct ReadyLatenessWitness {
  uint8_t view = 0xFF;
  uint64_t storage = 0;
  uint32_t start = 0;
  bool late = false;
  uint32_t lateHeldFrom = 0;
  uint32_t lateHeldLongest = 0;   // the longest run a late ready held the role while a positioned demand was refused
  uint32_t lateSeen = 0;          // late readies observed (non-vacuity of the RED)
  uint32_t lateSourceCorrect = 0; // ... of them Committed at the loop's current revision and generation
  uint32_t timelyAdopted = 0;     // timely readies that later became `current` at their own start (guard)
  uint32_t timelyLost = 0;        // timely readies that left `ready` without becoming `current`
  // Record-return windows (Capture origin, the frozen Record reader's) after the stop: each one that became complete
  // must be served -- `current` while the playhead is inside it -- before its view is reused.
  struct ReturnWindow { uint64_t storage = 0; bool tracked = false; bool served = false; };
  ReturnWindow returnWindows[3];
  uint32_t returnWindowsSeen = 0;
  uint32_t returnWindowsServed = 0;
  uint32_t returnWindowsLostUnserved = 0;
  bool started = false;  // windows already present at the stop were serving before it: not judged here
};

void observeReadyLateness(LoopPlaybackRuntime& runtime, const Loop& loop, uint32_t generation, uint32_t tick,
                          uint32_t loopLength, ReadyLatenessWitness& w) {
  const auto* set = runtime.viewSetIfPrewarmed();
  if (set == nullptr || loopLength == 0) return;
  const auto& lc = set->lifecycle;
  const uint32_t phase = tick % loopLength;
  for (uint8_t i = 0; i < set->views.size() && i < 3; ++i) {
    const auto& v = set->views[i];
    auto& r = w.returnWindows[i];
    const bool isReturn = v.complete && v.source.origin == PlaybackWindowOrigin::Capture;
    if (r.tracked && (!isReturn || v.storageGeneration != r.storage)) {  // reused or reset
      (r.served ? w.returnWindowsServed : w.returnWindowsLostUnserved)++;
      r = {};
    }
    if (isReturn && !r.tracked) {
      r.tracked = true;
      r.storage = v.storageGeneration;
      r.served = !w.started;
      if (w.started) ++w.returnWindowsSeen;
    }
    if (r.tracked && lc.current == i && v.coverage.contains(phase)) r.served = true;
  }
  w.started = true;
  const bool hasReady = lc.ready < set->views.size() && set->views[lc.ready].complete && !set->views[lc.ready].published;
  const bool same = hasReady && lc.ready == w.view && set->views[lc.ready].storageGeneration == w.storage;
  if (w.view != 0xFF && !same) {  // the tracked ready left the role
    if (!w.late) {
      const bool adopted = lc.current == w.view && set->views[w.view].published &&
          set->views[w.view].storageGeneration == w.storage;
      (adopted ? w.timelyAdopted : w.timelyLost)++;
    }
    w.view = 0xFF;
    w.lateHeldFrom = 0;
  }
  if (hasReady && w.view == 0xFF) {  // a ready newly occupies the role: judge it once, at the tick it is first seen
    const auto& v = set->views[lc.ready];
    w.view = lc.ready;
    w.storage = v.storageGeneration;
    w.start = v.coverage.startPhase;
    // Consumed: the playhead is past the start and the window's whole interval is not still ahead of it.
    const uint32_t sinceStart = (phase + loopLength - w.start) % loopLength;
    w.late = sinceStart != 0 && sinceStart < loopLength / 2;
    if (w.late) {
      ++w.lateSeen;
      if (v.source.origin == PlaybackWindowOrigin::Committed && v.source.playbackRevision == loop.playbackRevision &&
          v.source.sourceGeneration == generation)
        ++w.lateSourceCorrect;
    }
  }
  if (w.view != 0xFF && w.late) {
    const bool refusing = runtime.coverageRoleRefusal(false) == PlaybackCoverageRoleRefusal::ReadyRoleOccupied;
    if (refusing) {
      if (w.lateHeldFrom == 0) w.lateHeldFrom = tick;
      w.lateHeldLongest = std::max(w.lateHeldLongest, tick - w.lateHeldFrom + 1);
    }
  }
}

// Record-stop liveness RED (owner ruling 2026-11-04, device `captures/session_20261104_004411.log`). The device gesture
// on the routed slot: Record with the transport running, stopped into PLAYING; then play on. A committed window of the
// **current** source (revision, manifest identity and generation all current -- source-correct, not superseded) is
// published into `ready` only after the playhead has consumed its start boundary. It then holds `ready` until that
// phase recurs, one traversal later, while the positioned committed demand for the playhead's interval is refused
// `ReadyRoleOccupied`. Required: a late ready never holds the role for longer than one span.
void test_c5_3_record_stop_a_late_ready_never_holds_the_role_for_a_traversal() {
  installReader();
  enableProbeImageSuccessors();
  Track& t = trackManager.getTrack(0);
  RecordStopTrace trace;
  ReadyLatenessWitness w;
  PlaybackObservation obs = recordIntoPlayingThenPlay(t, 5, trace, [&](uint32_t tick) {
    observeReadyLateness(TrackCaptureStopTestAccess::runtimeFor(t, 0), t.getLoop(0),
                         TrackCaptureStopTestAccess::playbackGenerationOf(t), tick, kLength, w);
  });
  RollingSuccessorProbe::gKnobs = {};
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0u, w.lateSeen, "no late ready occurred: the RED would be vacuous");
  // The device verdict (identity rows at the retirement edge): the late ready is source-correct, not superseded.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(w.lateSeen, w.lateSourceCorrect, "a late ready was not of the current source");
  char msg[128];
  snprintf(msg, sizeof(msg), "a late ready held the role %u ticks while the playhead's demand was refused",
           unsigned(w.lateHeldLongest));
  TEST_ASSERT_TRUE_MESSAGE(w.lateHeldLongest <= 2 * kBar, msg);
}

// Guard (owner ruling: preserve DEC-159's valid unpublished Record predecessor): every **timely** ready -- complete
// before its own start, including the bound Record-return window -- is adopted as `current` at that start and is never
// discarded. Non-vacuous: at least one timely ready, and the bound Record-return window among them, is observed.
// Exactly-once: no NoteOn is emitted twice in any traversal after the stop.
void test_c5_3_record_stop_a_timely_ready_is_adopted_and_nothing_plays_twice() {
  installReader();
  enableProbeImageSuccessors();
  Track& t = trackManager.getTrack(0);
  RecordStopTrace trace;
  ReadyLatenessWitness w;
  PlaybackObservation obs = recordIntoPlayingThenPlay(t, 5, trace, [&](uint32_t tick) {
    observeReadyLateness(TrackCaptureStopTestAccess::runtimeFor(t, 0), t.getLoop(0),
                         TrackCaptureStopTestAccess::playbackGenerationOf(t), tick, kLength, w);
  });
  RollingSuccessorProbe::gKnobs = {};
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0u, w.timelyAdopted, "no timely ready observed: the guard would be vacuous");
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0u, w.returnWindowsSeen, "no Record-return window completed after the stop");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, w.returnWindowsLostUnserved, "a Record-return window was reused before it served");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, w.timelyLost, "a timely ready left the role without being adopted");
  for (uint32_t pass = 1; pass < 6; ++pass) {
    std::map<std::pair<uint32_t, uint8_t>, unsigned> seen;
    for (const auto& on : noteOnsInTraversal(obs, pass)) ++seen[on];
    for (const auto& [key, count] : seen) TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, count, "a NoteOn was emitted twice");
  }
}

/// The Record-return handoff witness: every change of the served view's origin, and the take's notes paired.
struct RootHandoffWitness {
  uint32_t rootHandoffs = 0;        // `current` went from a Capture (frozen Record-return) view to a Committed one
  uint32_t captureAfterRoot = 0;    // a Capture view served again after the root took over
  uint32_t firstRootTick = 0;
  int lastOrigin = -1;
};

void observeRootHandoff(LoopPlaybackRuntime& runtime, uint32_t tick, RootHandoffWitness& w) {
  const auto* set = runtime.viewSetIfPrewarmed();
  if (set == nullptr || set->lifecycle.current >= set->views.size()) return;
  const auto& cur = set->views[set->lifecycle.current];
  if (!cur.complete || !cur.published) return;
  const int origin = static_cast<int>(cur.source.origin);
  if (w.lastOrigin == static_cast<int>(PlaybackWindowOrigin::Capture) &&
      origin == static_cast<int>(PlaybackWindowOrigin::Committed)) {
    if (w.rootHandoffs++ == 0) w.firstRootTick = tick;
  }
  if (w.rootHandoffs != 0 && origin == static_cast<int>(PlaybackWindowOrigin::Capture)) ++w.captureAfterRoot;
  w.lastOrigin = origin;
}

/// Traversals [from, to): every take note plays exactly once, and every NoteOn of a pitch is closed by exactly one
/// NoteOff before that pitch sounds again (no duplicate, missing or orphaned NoteOn/NoteOff).
void assertTakeNotesPairedExactlyOnce(const PlaybackObservation& obs, uint32_t from, uint32_t to) {
  assertEveryTakeNotePlayed(obs, from, to);
  for (uint32_t pass = from; pass < to; ++pass) {
    std::map<std::pair<uint32_t, uint8_t>, unsigned> seen;
    for (const auto& on : noteOnsInTraversal(obs, pass)) ++seen[on];
    for (const auto& [key, count] : seen) TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, count, "a NoteOn was emitted twice");
  }
  std::map<uint8_t, int> open;
  for (const auto& [tick, e] : obs.sent) {
    if (tick < from * kLength || tick >= to * kLength) continue;
    const uint8_t pitch = e.data.noteData.note;
    if (e.isNoteOn()) {
      TEST_ASSERT_EQUAL_INT_MESSAGE(0, open[pitch], "a NoteOn sounded over an unclosed NoteOn");
      open[pitch] = 1;
    } else if (e.isNoteOff() && open.count(pitch) != 0) {
      TEST_ASSERT_EQUAL_INT_MESSAGE(1, open[pitch], "a NoteOff without its NoteOn");
      open[pitch] = 0;
    }
  }
}

// DEC-136 Record-return ruling (2026-11-04), outcome 1: the root candidate qualifies before the frozen reader's next
// unconsumed handoff boundary and replaces the frozen reader **exactly once**; the Record-only stop stays audible across
// every traversal, with no duplicate or missing NoteOn/NoteOff.
void test_c5_3_record_return_on_time_root_replaces_frozen_exactly_once() {
  installReader();
  enableProbeImageSuccessors();
  Track& t = trackManager.getTrack(0);
  RecordStopTrace trace;
  RootHandoffWitness w;
  PlaybackObservation obs = recordIntoPlayingThenPlay(t, 5, trace, [&](uint32_t tick) {
    observeRootHandoff(TrackCaptureStopTestAccess::runtimeFor(t, 0), tick, w);
  });
  RollingSuccessorProbe::gKnobs = {};
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, w.rootHandoffs, "the root did not replace the frozen reader exactly once");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, w.captureAfterRoot, "a frozen view served again after the root handoff");
  TEST_ASSERT_TRUE_MESSAGE(w.firstRootTick < 2 * kLength, "the root handoff waited for a later traversal");
  assertTakeNotesPairedExactlyOnce(obs, 1, 6);
}

// Outcome 2, deliberately delayed root: every root qualification is withheld (its lane turns serve no one while it
// counts) for the first three bars after the stop. The root misses those boundaries; the frozen reader keeps preparing
// and serving at least two further bars without a gap, and once the delay lifts the root replaces it exactly once.
void test_c5_3_record_return_delayed_root_never_blocks_frozen_continuation() {
  installReader();
  enableProbeImageSuccessors();
  Track& t = trackManager.getTrack(0);
  RecordStopTrace trace;
  RootHandoffWitness w;
  const uint32_t releaseTick = kLength + 3 * kBar;
  uint32_t withheld = 0;
  gWithholdLaneTurn = [&](uint32_t tick) {
    if (tick >= releaseTick || tick < kLength) return false;
    const auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
    const bool rootCounting = runtime.coveragePreparation == LoopPlaybackRuntime::CoveragePreparation::Counting;
    if (rootCounting) ++withheld;
    return rootCounting;
  };
  PlaybackObservation obs = recordIntoPlayingThenPlay(t, 5, trace, [&](uint32_t tick) {
    observeRootHandoff(TrackCaptureStopTestAccess::runtimeFor(t, 0), tick, w);
  });
  gWithholdLaneTurn = {};
  RollingSuccessorProbe::gKnobs = {};
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0u, withheld, "no root qualification was delayed: the RED would be vacuous");
  TEST_ASSERT_TRUE_MESSAGE(w.rootHandoffs == 0 || w.firstRootTick >= releaseTick,
                           "the root took over while it was still being withheld");
  assertTakeNotesPairedExactlyOnce(obs, 1, 6);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, w.rootHandoffs, "the root did not replace the frozen reader exactly once");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, w.captureAfterRoot, "a frozen view served again after the root handoff");
}

/// DEC-136 option (a) ownership witness (owner ruling on `f871b67d`), sampled every tick after the Record stop.
struct RecordReturnOwnershipWitness {
  uint32_t gapTicks = 0;              // no complete, published `current` contains the playhead
  uint32_t generationMismatch = 0;    // the served view's source generation is not the slot's
  bool rootBesideFrozen = false;      // RED 1: frozen current + frozen ready + root building/complete in `build`
  // RED 2: the root parked complete in `build` behind the frozen ready, then published and adopted from that view.
  bool parked = false;
  uint8_t parkedView = 0xFF;
  uint64_t parkedStorage = 0;
  uint8_t frozenReadyView = 0xFF;
  uint64_t frozenReadyStorage = 0;
  uint32_t readyOverwritten = 0;      // the frozen ready's view was rewritten while the root was parked
  bool publishedFromPark = false;
  bool adoptedFromPark = false;
  // RED 4: frozen (Capture) builds begun after the root became current, and any that vanished before completing.
  uint8_t frozenBuildView = 0xFF;
  uint64_t frozenBuildStorage = 0;
  uint32_t frozenBuildStart = 0;
  uint32_t frozenBuildsStarted = 0;
  uint32_t frozenBuildsCompleted = 0;
  uint32_t frozenBuildsKilled = 0;
  // RED 6: a root build observed while a valid retained view holds the third physical view.
  uint32_t rootBuiltWhileRetained = 0;
};

void observeRecordReturnOwnership(Track& t, uint32_t tick, RecordReturnOwnershipWitness& w) {
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  const auto* set = runtime.viewSetIfPrewarmed();
  if (set == nullptr) return;
  const Loop& loop = t.getLoop(0);
  const auto& lc = set->lifecycle;
  const size_t n = set->views.size();
  const uint32_t phase = tick % kLength;
  const uint32_t generation = TrackCaptureStopTestAccess::playbackGenerationOf(t);
  const auto* cur = lc.current < n ? &set->views[lc.current] : nullptr;
  const auto* rdy = lc.ready < n ? &set->views[lc.ready] : nullptr;
  const auto* bld = lc.build < n ? &set->views[lc.build] : nullptr;
  const bool served = cur != nullptr && cur->complete && cur->published && cur->coverage.contains(phase);
  if (!served) ++w.gapTicks;
  if (cur != nullptr && cur->published && cur->source.sourceGeneration != generation) ++w.generationMismatch;
  const bool rootCurrent = loop.playbackRevision != 0;
  const bool frozenCur = served && cur->source.origin == PlaybackWindowOrigin::Capture;
  const bool frozenRdy = rdy != nullptr && rdy->complete && !rdy->published &&
      rdy->source.origin == PlaybackWindowOrigin::Capture;
  const bool rootInBuild = bld != nullptr && bld->source.origin == PlaybackWindowOrigin::Committed &&
      bld->coverage.lengthTicks != 0 && (bld->complete || runtime.coverageBuildNeedsService());
  if (rootCurrent && frozenCur && frozenRdy && rootInBuild) w.rootBesideFrozen = true;
  if (lc.retained < n && set->views[lc.retained].complete && rootInBuild) ++w.rootBuiltWhileRetained;
  // RED 2
  if (!w.parked && frozenRdy && bld != nullptr && bld->complete && !bld->published &&
      bld->source.origin == PlaybackWindowOrigin::Committed) {
    w.parked = true;
    w.parkedView = lc.build;
    w.parkedStorage = bld->storageGeneration;
    w.frozenReadyView = lc.ready;
    w.frozenReadyStorage = rdy->storageGeneration;
  }
  if (w.parked && !w.publishedFromPark && lc.ready == w.frozenReadyView && rdy != nullptr &&
      rdy->storageGeneration != w.frozenReadyStorage && lc.ready != w.parkedView)
    ++w.readyOverwritten;
  if (w.parked && lc.ready == w.parkedView && rdy != nullptr && rdy->storageGeneration == w.parkedStorage)
    w.publishedFromPark = true;
  if (w.parked && lc.current == w.parkedView && cur != nullptr && cur->storageGeneration == w.parkedStorage)
    w.adoptedFromPark = true;
  // RED 4
  if (w.frozenBuildView != 0xFF) {
    const auto& v = set->views[w.frozenBuildView];
    if (v.storageGeneration != w.frozenBuildStorage || v.coverage.startPhase != w.frozenBuildStart ||
        v.source.origin != PlaybackWindowOrigin::Capture) {
      ++w.frozenBuildsKilled;  // the view was reset or reused before it completed
      w.frozenBuildView = 0xFF;
    } else if (v.complete) {
      ++w.frozenBuildsCompleted;
      w.frozenBuildView = 0xFF;
    }
  }
  if (rootCurrent && w.frozenBuildView == 0xFF && runtime.frozenWindowBuildActive() && bld != nullptr &&
      !bld->complete && bld->source.origin == PlaybackWindowOrigin::Capture) {
    w.frozenBuildView = lc.build;
    w.frozenBuildStorage = bld->storageGeneration;
    w.frozenBuildStart = bld->coverage.startPhase;
    ++w.frozenBuildsStarted;
  }
}

/// Withhold the root's qualification turns (the deliberately delayed root) until `releaseTick`.
void delayRootQualificationUntil(Track& t, uint32_t releaseTick, uint32_t& withheld) {
  gWithholdLaneTurn = [&t, releaseTick, &withheld](uint32_t tick) {
    if (tick >= releaseTick || tick < kLength) return false;
    const bool counting = TrackCaptureStopTestAccess::runtimeFor(t, 0).coveragePreparation ==
        LoopPlaybackRuntime::CoveragePreparation::Counting;
    if (counting) ++withheld;
    return counting;
  };
}

// RED 1: the committed root-return candidate prepares in the third physical view while the frozen reader owns both
// `current` (serving the playhead) and `ready` (its next bar).
void test_c5_3_record_return_red1_root_prepares_in_build_beside_frozen_current_and_ready() {
  installReader();
  enableProbeImageSuccessors();
  Track& t = trackManager.getTrack(0);
  RecordStopTrace trace;
  RecordReturnOwnershipWitness w;
  PlaybackObservation obs = recordIntoPlayingThenPlay(t, 5, trace, [&](uint32_t tick) {
    observeRecordReturnOwnership(t, tick, w);
  });
  RollingSuccessorProbe::gKnobs = {};
  TEST_ASSERT_TRUE_MESSAGE(w.rootBesideFrozen, "the root never prepared in build beside frozen current and ready");
}

// RED 2: the root completes before the handoff, parks in `build` without overwriting the frozen `ready`, is
// published once `ready` frees, and is adopted from that same view.
void test_c5_3_record_return_red2_root_parks_then_publishes_once_ready_is_free() {
  installReader();
  enableProbeImageSuccessors();
  Track& t = trackManager.getTrack(0);
  RecordStopTrace trace;
  RecordReturnOwnershipWitness w;
  RootHandoffWitness h;
  PlaybackObservation obs = recordIntoPlayingThenPlay(t, 5, trace, [&](uint32_t tick) {
    observeRecordReturnOwnership(t, tick, w);
    observeRootHandoff(TrackCaptureStopTestAccess::runtimeFor(t, 0), tick, h);
  });
  RollingSuccessorProbe::gKnobs = {};
  TEST_ASSERT_TRUE_MESSAGE(w.parked, "the root never parked complete in build behind the frozen ready");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, w.readyOverwritten, "the parked root overwrote the frozen ready");
  TEST_ASSERT_TRUE_MESSAGE(w.publishedFromPark, "the parked root was not published once ready was free");
  TEST_ASSERT_TRUE_MESSAGE(w.adoptedFromPark, "the root adopted was not the parked view");
  TEST_ASSERT_EQUAL_UINT32(1u, h.rootHandoffs);
}

// RED 3 + 4: a deliberately delayed root yields the builder in time for the frozen reader to prepare its next bar,
// across at least two further bars; frozen builds owned by the Record-return job are never killed as
// `SourceSuperseded`; the Record-only stop has no gap and the root still replaces the frozen reader exactly once.
void test_c5_3_record_return_red3_red4_delayed_root_yields_and_frozen_builds_survive() {
  installReader();
  enableProbeImageSuccessors();
  Track& t = trackManager.getTrack(0);
  RecordStopTrace trace;
  RecordReturnOwnershipWitness w;
  RootHandoffWitness h;
  uint32_t withheld = 0;
  delayRootQualificationUntil(t, kLength + 3 * kBar, withheld);
  PlaybackObservation obs = recordIntoPlayingThenPlay(t, 5, trace, [&](uint32_t tick) {
    observeRecordReturnOwnership(t, tick, w);
    observeRootHandoff(TrackCaptureStopTestAccess::runtimeFor(t, 0), tick, h);
  });
  gWithholdLaneTurn = {};
  RollingSuccessorProbe::gKnobs = {};
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0u, withheld, "no root qualification was delayed: vacuous");
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(2u, w.frozenBuildsCompleted,
                                              "the frozen reader did not prepare two further bars after the root");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, w.frozenBuildsKilled, "a Record-return frozen build was killed");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, w.gapTicks, "the Record-only stop had an audible gap");
  TEST_ASSERT_EQUAL_UINT32(1u, h.rootHandoffs);
  assertTakeNotesPairedExactlyOnce(obs, 1, 6);
}

// RED 5: the root/frozen handoff happens exactly once; every served view carries the slot's source generation; the
// stop is audible with no gap across every traversal, and every note is paired exactly once.
void test_c5_3_record_return_red5_handoff_exactly_once_and_no_gap() {
  installReader();
  enableProbeImageSuccessors();
  Track& t = trackManager.getTrack(0);
  RecordStopTrace trace;
  RecordReturnOwnershipWitness w;
  RootHandoffWitness h;
  PlaybackObservation obs = recordIntoPlayingThenPlay(t, 5, trace, [&](uint32_t tick) {
    observeRecordReturnOwnership(t, tick, w);
    observeRootHandoff(TrackCaptureStopTestAccess::runtimeFor(t, 0), tick, h);
  });
  RollingSuccessorProbe::gKnobs = {};
  TEST_ASSERT_EQUAL_UINT32(1u, h.rootHandoffs);
  TEST_ASSERT_EQUAL_UINT32(0u, h.captureAfterRoot);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, w.generationMismatch, "a served view carried a foreign source generation");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, w.gapTicks, "the Record-only stop had an audible gap");
  assertTakeNotesPairedExactlyOnce(obs, 1, 6);
}

// RED 6: a legitimately retained third view refuses speculative root preparation, and frozen playback continues
// unaffected. The third view is made a complete retained window the moment the root becomes current.
void test_c5_3_record_return_red6_retained_third_view_refuses_root_and_frozen_plays_on() {
  installReader();
  enableProbeImageSuccessors();
  Track& t = trackManager.getTrack(0);
  RecordStopTrace trace;
  RecordReturnOwnershipWitness w;
  bool retainedInstalled = false;
  MidiEvent events[2] = {MidiEvent::NoteOn(12, 1, 90, 100), MidiEvent::NoteOff(96, 1, 90, 0)};
  events[0].noteId = static_cast<NoteId>(900);
  events[1].noteId = static_cast<NoteId>(900);
  PlaybackObservation obs = recordIntoPlayingThenPlay(t, 5, trace, [&](uint32_t tick) {
    auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
    auto* set = runtime.viewSetIfPrewarmed();
    if (!retainedInstalled && set != nullptr && t.getLoop(0).playbackRevision != 0 &&
        set->lifecycle.build < set->views.size() && !runtime.viewBuild.active() &&
        set->lifecycle.retained == RuntimePlaybackViewLifecycle::kNoView) {
      const uint8_t third = set->lifecycle.build;
      RuntimePlaybackViewBuild build;
      TEST_ASSERT_TRUE(build.begin(set->views[third], events, 2,
                                   PlaybackViewSourceIdentity{0, TrackCaptureStopTestAccess::playbackGenerationOf(t),
                                                              nullptr, PlaybackWindowOrigin::Committed},
                                   PlaybackCoverageInterval{0, kBar, kLength}, EmissionCursor{},
                                   LedgerBoundarySummary{}));
      for (unsigned g = 0; g < 64 && build.nextGrain() == RuntimePlaybackViewBuildGrain::Fill; ++g) (void)build.stepFill(1);
      for (unsigned g = 0; g < 64 && build.nextGrain() == RuntimePlaybackViewBuildGrain::Order; ++g) (void)build.stepOrder(1);
      set->lifecycle.build = RuntimePlaybackViewLifecycle::kNoView;
      set->lifecycle.retained = third;
      retainedInstalled = true;
    }
    observeRecordReturnOwnership(t, tick, w);
  });
  RollingSuccessorProbe::gKnobs = {};
  TEST_ASSERT_TRUE_MESSAGE(retainedInstalled, "the retained third view was never installed: vacuous");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, w.rootBuiltWhileRetained, "the root prepared over a retained third view");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, w.gapTicks, "frozen playback was affected by the refused root");
  assertEveryTakeNotePlayed(obs, 1, 3);
}

// Two-span requester, essential RED (owner ruling 2026-11-03; controlled eligibility: the probe image's successor
// knobs, DEC-147 price waived). While N-1 serves and N waits complete in `ready`, N+1 must be prepared in `build`
// (the depth-2 obligation): at some tick all three stages coexist -- current = the serving window, ready = its
// depth-1 successor, build = the depth-2 successor (building or complete). Each window is demanded exactly once (no
// second demand for the same deadline), and the steady traversals play every take note exactly once. RED today: the
// requester opens a successor only while `ready` is empty, and `coverageRoleRefusal` refuses any build while `ready`
// holds a complete view (`ReadyRoleOccupied`).
void test_c5_3_two_span_prepares_n_plus_1_while_n_waits_ready() {
  installReader();
  enableProbeImageSuccessors();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordFourBarTake(t);
  reader().serviceOwner(loop);
  startTransport(t);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  constexpr uint32_t kSpanTicks = 2 * kBar;
  bool threeStages = false;
  PlaybackObservation obs;
  play(t, 0, 6 * kLength, obs, 1, [&](uint32_t) {
    const auto& frame = runtime.rolling;
    const auto* set = runtime.viewSetIfPrewarmed();
    if (set == nullptr || frame.phase != LoopPlaybackRuntime::RollingPhase::Serving || frame.loopLength == 0) return;
    const auto& lc = set->lifecycle;
    if (lc.ready >= set->views.size() || lc.build >= set->views.size()) return;
    const auto& ready = set->views[lc.ready];
    const auto& build = set->views[lc.build];
    const uint32_t depth1 = (frame.startPhase + kSpanTicks) % frame.loopLength;
    const uint32_t depth2 = (frame.startPhase + 2 * kSpanTicks) % frame.loopLength;
    if (ready.complete && ready.coverage.startPhase == depth1 && build.coverage.lengthTicks == kSpanTicks &&
        build.coverage.startPhase == depth2 &&
        (build.complete || runtime.coveragePreparation == LoopPlaybackRuntime::CoveragePreparation::Building))
      threeStages = true;
  });
  const uint32_t requested = RollingSuccessorProbe::gCounters.requested;
  const uint32_t duplicates = RollingSuccessorProbe::gCounters.duplicateStarts;
  RollingSuccessorProbe::gKnobs = {};
  TEST_ASSERT_TRUE_MESSAGE(threeStages, "N+1 was never prepared while N waited in ready (no depth-2 obligation)");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, duplicates, "a window was demanded twice");
  TEST_ASSERT_GREATER_THAN_UINT32(0u, requested);
  assertEveryTakeNotePlayed(obs, 2, 6);
  for (uint32_t pass = 2; pass < 6; ++pass) {  // exactly once
    std::map<std::pair<uint32_t, uint8_t>, unsigned> seen;
    for (const auto& on : noteOnsInTraversal(obs, pass)) ++seen[on];
    for (const auto& [key, count] : seen) TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, count, "a NoteOn was emitted twice");
  }
}

/// Per-tick two-span witness on the active slot: physical-role uniqueness every tick, and the parked/published
/// identity of a depth-2 window (N+1 complete in `build` while N occupies `ready`).
struct TwoSpanWitness {
  bool parked = false;              // N+1 seen complete in build behind a complete ready N
  uint8_t parkedView = 0xFF;        // its physical view
  uint64_t parkedStorage = 0;       // its storage generation when parked
  uint32_t parkedStart = 0;
  bool publishedWithoutRebuild = false;  // later seen in ready: same view, same storage generation
  bool midBuildAtHandoff = false;   // a handoff happened while N+1 was still building
  bool midBuildSurvived = false;    // ... and the same physical view completed without a reset
  uint8_t midView = 0xFF;
  uint64_t midStorage = 0;
};

void observeTwoSpan(LoopPlaybackRuntime& runtime, TwoSpanWitness& w, uint32_t& lastStart) {
  const auto* set = runtime.viewSetIfPrewarmed();
  if (set == nullptr) return;
  assertRolesDistinct(set->lifecycle);
  const auto& frame = runtime.rolling;
  const auto& lc = set->lifecycle;
  const bool serving = frame.phase == LoopPlaybackRuntime::RollingPhase::Serving && frame.loopLength != 0;
  if (serving && lc.ready < set->views.size() && lc.build < set->views.size()) {
    const auto& ready = set->views[lc.ready];
    const auto& build = set->views[lc.build];
    const uint32_t span = 2 * kBar;
    const uint32_t depth1 = (frame.startPhase + span) % frame.loopLength;
    const uint32_t depth2 = (frame.startPhase + 2 * span) % frame.loopLength;
    if (!w.parked && ready.complete && ready.coverage.startPhase == depth1 && build.complete &&
        build.coverage.startPhase == depth2 && !build.published) {
      w.parked = true;
      w.parkedView = lc.build;
      w.parkedStorage = build.storageGeneration;
      w.parkedStart = depth2;
    }
  }
  // A handoff (the frame's start moved) while the build view is still being filled under coverage ownership.
  if (serving && !w.midBuildAtHandoff && lastStart != UINT32_MAX && frame.startPhase != lastStart &&
      lc.build < set->views.size() && !set->views[lc.build].complete &&
      runtime.coveragePreparation == LoopPlaybackRuntime::CoveragePreparation::Building &&
      set->views[lc.build].coverage.startPhase == (frame.startPhase + 2 * kBar) % frame.loopLength) {
    w.midBuildAtHandoff = true;
    w.midView = lc.build;
    w.midStorage = set->views[lc.build].storageGeneration;
  }
  if (w.parked && !w.publishedWithoutRebuild && lc.ready == w.parkedView && lc.ready < set->views.size() &&
      set->views[lc.ready].complete && set->views[lc.ready].storageGeneration == w.parkedStorage &&
      set->views[lc.ready].coverage.startPhase == w.parkedStart)
    w.publishedWithoutRebuild = true;
  if (w.midBuildAtHandoff && !w.midBuildSurvived && w.midView < set->views.size() &&
      set->views[w.midView].complete && set->views[w.midView].storageGeneration == w.midStorage)
    w.midBuildSurvived = true;
  lastStart = frame.startPhase;
}

// Two-span: N+1 completed **before** the N-1 -> N handoff, parked in `build` behind the occupied `ready`, then published
// as `ready` after N is promoted -- the same physical view and storage generation, i.e. without restarting work -- and
// with no second demand. Roles stay distinct every tick and every note plays exactly once.
void test_c5_3_two_span_parked_successor_is_published_without_rebuild() {
  installReader();
  enableProbeImageSuccessors();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordFourBarTake(t);
  reader().serviceOwner(loop);
  startTransport(t);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  TwoSpanWitness w;
  uint32_t lastStart = UINT32_MAX;
  PlaybackObservation obs;
  play(t, 0, 6 * kLength, obs, 1, [&](uint32_t) { observeTwoSpan(runtime, w, lastStart); });
  const uint32_t duplicates = RollingSuccessorProbe::gCounters.duplicateStarts;
  RollingSuccessorProbe::gKnobs = {};
  TEST_ASSERT_TRUE_MESSAGE(w.parked, "N+1 never completed while N occupied ready (the parked case did not occur)");
  TEST_ASSERT_TRUE_MESSAGE(w.publishedWithoutRebuild, "the parked N+1 was not published as ready without rebuilding");
  TEST_ASSERT_EQUAL_UINT32(0u, duplicates);
  assertEveryTakeNotePlayed(obs, 2, 6);
}

// Two-span: N+1 is still mid-build at the N-1 -> N handoff (its demand opened just before it) and completes in the
// same physical view, unreset, after the handoff; no traversal loses a take note.
constexpr uint32_t kMidBuildLeadTicks = 3;  // request -> Building in 2 ticks; the handoff lands inside the fill
void test_c5_3_two_span_mid_build_successor_survives_the_handoff() {
  installReader();
  enableProbeImageSuccessors();
  // The depth-2 demand opens a few ticks before the N-1 -> N handoff (its deadline is N.end, one span later), so its
  // qualification and fill straddle the handoff. (Withheld grants cannot do this: the bounded selector forces a
  // deadline successor after three denials.)
  RollingSuccessorProbe::gKnobs.requestLeadTicks = 2 * kBar + kMidBuildLeadTicks;
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordFourBarTake(t);
  reader().serviceOwner(loop);
  startTransport(t);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  TwoSpanWitness w;
  uint32_t lastStart = UINT32_MAX;
  PlaybackObservation obs;
  play(t, 0, 8 * kLength, obs, 1, [&](uint32_t) { observeTwoSpan(runtime, w, lastStart); });
  RollingSuccessorProbe::gKnobs = {};
  TEST_ASSERT_TRUE_MESSAGE(w.midBuildAtHandoff, "no handoff happened while N+1 was mid-build (case did not occur)");
  TEST_ASSERT_TRUE_MESSAGE(w.midBuildSurvived, "the mid-build N+1 did not complete in its own, unreset view");
  assertEveryTakeNotePlayed(obs, 2, 8);
}

// Two-span: a revision change while the depth-2 obligation is outstanding invalidates it -- it is never preserved into
// promotion, and the deleted note never sounds from a window read after the move.
void test_c5_3_two_span_revision_change_never_preserves_a_stale_obligation() {
  installReader();
  enableProbeImageSuccessors();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordFourBarTake(t);
  reader().serviceOwner(loop);
  startTransport(t);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  uint32_t movedAt = 0;
  bool sawDepth2 = false;
  PlaybackObservation obs;
  play(t, 0, 6 * kLength, obs, 1, [&](uint32_t tick) {
    if (movedAt != 0 || tick < 2 * kLength) return;
    if (runtime.rolling.successorState != 4) return;
    sawDepth2 = true;
    deleteTakeNote(loop, 63);  // the source moves while N+1 is owed
    movedAt = tick;
  });
  RollingSuccessorProbe::gKnobs = {};
  TEST_ASSERT_TRUE_MESSAGE(sawDepth2, "no depth-2 obligation was outstanding (case did not occur)");
  for (const auto& [tick, e] : obs.sent) {
    if (!e.isNoteOn() || e.data.noteData.note != 63) continue;
    TEST_ASSERT_TRUE_MESSAGE(tick < movedAt + 3 * 2 * kBar, "the deleted note sounded from a stale preserved window");
  }
}

// Steady-state continuity under production DEC-147 pricing (owner ruling 2026-11-03): once a routed chain is serving,
// every traversal plays every take note -- the successor must be requested, admitted and complete before the serving
// window expires. Traversals 0-1 are the chain's cold start (their own RED) and are excluded. RED today: with the price
// in force no successor at N's end is admitted, so steady state plays bars 2-3 of every other traversal.
void test_c5_3_steady_state_is_continuous_under_production_pricing() {
  installReader();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordFourBarTake(t);
  reader().serviceOwner(loop);
  startTransport(t);
  PlaybackObservation obs;
  play(t, 0, 6 * kLength, obs);
  assertEveryTakeNotePlayed(obs, 2, 6);
}

// Completion-aware placement (owner ruling 2026-11-03, `2026-11-03-routed-cold-start-design.md` section 10): the
// post-stop window for bars 2-3 is complete 56 ticks after an overdub stop at traversal 3's phase 0, about 1,464 ticks
// before this traversal's bar-2 boundary -- more than the contract's one-bar handoff margin -- yet it is held for the
// *next* traversal because the request-time DEC-147 price ruled the nearer boundary out. RED today. Only bars 2-3 are
// asserted: bars 0-1 after the stop belong to the separate cold-start RED and are not fixed by placement.
void test_c5_3_completion_placement_serves_the_nearest_eligible_boundary() {
  installReader();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordFourBarTake(t);
  const PlaybackObservation obs = overdubThenStopAt(t, loop, 3 * kLength - 40);
  TEST_ASSERT_TRUE_MESSAGE(takeNotePlayed(obs, 3, 2), "traversal 3: the completed bar-2 window was not served");
  TEST_ASSERT_TRUE_MESSAGE(takeNotePlayed(obs, 3, 3), "traversal 3: the completed bar-3 window was not served");
  for (uint32_t pass = 3; pass < 7; ++pass) {  // no duplicate: one NoteOn per take note per traversal
    std::map<std::pair<uint32_t, uint8_t>, unsigned> seen;
    for (const auto& on : noteOnsInTraversal(obs, pass)) ++seen[on];
    for (const auto& [key, count] : seen) TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, count, "a NoteOn was emitted twice");
  }
}

// Guard (negative): the boundary is already consumed. Stopped inside traversal 3's bars 2-3 (phase 2,000), the cold
// frame's first window is that very interval (phase 2,000 + the 4,480-tick price lands on a phase-1,536 boundary two
// traversals later), and it completes while traversal 3's bars 2-3 are still playing -- asserted, so the guard
// exercises a real completed window. Placement may not adopt it part-way into the in-progress interval, so the take's
// bar-3 note of traversal 3 is not played from it (nothing else serves a routed gap).
void test_c5_3_completion_placement_never_claims_a_consumed_boundary() {
  installReader();
  Track& t = trackManager.getTrack(0);
  Loop& loop = recordFourBarTake(t);
  CompletedWindowWitness witness{1536 + 800};
  const PlaybackObservation obs = overdubThenStopAt(t, loop, 3 * kLength + 2000, &witness);
  // Not vacuous: a complete window for this interval exists while traversal 3's bars 2-3 are still playing.
  TEST_ASSERT_NOT_EQUAL_UINT32_MESSAGE(0u, witness.completedAt, "no completed window: the guard would be vacuous");
  TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(4 * kLength, witness.completedAt, "the window completed after the interval");
  TEST_ASSERT_FALSE_MESSAGE(takeNotePlayed(obs, 3, 3), "a window was adopted part-way into a consumed interval");
}


int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_c5_3_current_boot_content_is_served_from_the_manifest_only);
  RUN_TEST(test_c5_3_owed_manifest_is_a_named_miss_and_the_tick_builds_nothing);
  RUN_TEST(test_c5_3_no_committed_source_then_imported_content_is_served);
  RUN_TEST(test_c5_3_a_manifest_move_during_preparation_never_publishes_the_stale_candidate);
  RUN_TEST(test_c5_3_index_owed_then_ready_serves_the_hide_without_scanning);
  RUN_TEST(test_c5_3_both_index_slots_pinned_is_capacity_denied_without_eviction);
  RUN_TEST(test_c5_3_a_source_move_while_serving_cannot_publish_a_stale_window);
  RUN_TEST(test_c5_3_a_note_sounding_into_a_gap_is_closed_not_left_hanging);
  RUN_TEST(test_c5_3_a_snapshot_cold_ready_is_cancelled_once_the_slot_is_routed);
  RUN_TEST(test_c5_3_a_dense_take_is_served_from_the_manifest);
  RUN_TEST(test_c5_3_completion_placement_never_claims_a_consumed_boundary);
  RUN_TEST(test_c5_3_a_valid_retained_record_bar_stays_protected);
  RUN_TEST(test_c5_3_orphaned_retained_role_is_released);
  RUN_TEST(test_c5_3_two_span_prepares_n_plus_1_while_n_waits_ready);
  RUN_TEST(test_c5_3_two_span_parked_successor_is_published_without_rebuild);
  RUN_TEST(test_c5_3_two_span_mid_build_successor_survives_the_handoff);
  RUN_TEST(test_c5_3_two_span_revision_change_never_preserves_a_stale_obligation);
  RUN_TEST(test_c5_3_record_stop_a_timely_ready_is_adopted_and_nothing_plays_twice);
  if (getenv("RECORD_RETURN_RED") != nullptr) {
    RUN_TEST(test_c5_3_record_return_on_time_root_replaces_frozen_exactly_once);
    RUN_TEST(test_c5_3_record_return_delayed_root_never_blocks_frozen_continuation);
    RUN_TEST(test_c5_3_record_return_red1_root_prepares_in_build_beside_frozen_current_and_ready);
    RUN_TEST(test_c5_3_record_return_red2_root_parks_then_publishes_once_ready_is_free);
    RUN_TEST(test_c5_3_record_return_red3_red4_delayed_root_yields_and_frozen_builds_survive);
    RUN_TEST(test_c5_3_record_return_red5_handoff_exactly_once_and_no_gap);
    RUN_TEST(test_c5_3_record_return_red6_retained_third_view_refuses_root_and_frozen_plays_on);
  }
  if (getenv("RECORD_STOP_LIVENESS_RED") != nullptr)
    RUN_TEST(test_c5_3_record_stop_a_late_ready_never_holds_the_role_for_a_traversal);
  if (getenv("COMPLETION_PLACEMENT_RED") != nullptr)
    RUN_TEST(test_c5_3_completion_placement_serves_the_nearest_eligible_boundary);
  if (getenv("STEADY_STATE_RED") != nullptr) RUN_TEST(test_c5_3_steady_state_is_continuous_under_production_pricing);
  if (getenv("ROUTED_AUDIBILITY_RED") != nullptr) {
    RUN_TEST(test_c5_3_cold_start_transport_start_plays_the_first_traversal);
    RUN_TEST(test_c5_3_gate_a_take_stays_audible_through_an_overdub_stop);
  }
  return UNITY_END();
}
