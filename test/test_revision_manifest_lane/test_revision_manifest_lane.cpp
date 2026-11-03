// C5.3 (DEC-246 amendments 19-22): the committed rolling-window lane of the designated track reads RevisionManifest.
//
// The shared finalization suite's real Track/Loop/storage closure is compiled here with the rolling reader enabled.
// Each case installs a host reader owner (`FixtureReader`): a RevisionStore with the production-equal importer and the
// C5.2 window cursor, i.e. what `RevisionShadow` is on the flag-1 probe image. Its owner work (import, edit index) runs
// only when a case calls `serviceOwner`, which stands in for the registered idle children; the lane never calls it.
//
// Only this suite's own cases are registered; the included suite's cases are dead-stripped from the run.
#include "manifest_lane_harness.h"

extern "C" void setUp() {
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
void test_c5_3_gate_a_take_stays_audible_through_an_overdub_stop() {
  installReader();
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
  for (uint32_t pass = 2; pass < 7; ++pass) {
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
  if (getenv("ROUTED_AUDIBILITY_RED") != nullptr) RUN_TEST(test_c5_3_gate_a_take_stays_audible_through_an_overdub_stop);
  return UNITY_END();
}
