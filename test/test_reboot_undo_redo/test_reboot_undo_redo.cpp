// Saved-slot integration: use the existing production display/storage/undo/playback closure.
// Only raster/DMA, host time and in-memory SD are I/O boundaries.
#define HARNESS_REAL_STORAGE_LOOP_UNDO
#define main liveCaptureDisplayBoundaryMain
#include "../test_live_capture_display_boundary/test_live_capture_display_boundary.cpp"
#undef main

// Existing native observation seam: no mutation or replacement of the source owner.
struct LoopEventStoreSourceTestAccess {
  static uint64_t aliasGeneration() { return LoopEventStore::aliasGeneration_; }
};

namespace RebootUndoFixture {
constexpr uint32_t kLength = 8 * Config::TICKS_PER_BAR;
constexpr uint8_t kPitches[] = {48, 60, 72};
constexpr uint32_t kStarts[] = {96, 288, 480};

struct BodyIo {
  std::vector<uint8_t> bytes;
  size_t cursor = 0;
  StorageIo io() {
    return {
        [this](const void* data, size_t count) {
          const auto* begin = static_cast<const uint8_t*>(data);
          bytes.insert(bytes.end(), begin, begin + count);
          return true;
        },
        [this](void* data, size_t count) {
          if (cursor + count > bytes.size()) return false;
          std::memcpy(data, bytes.data() + cursor, count);
          cursor += count;
          return true;
        },
        [this](void* data, size_t count) {
          if (cursor + count > bytes.size()) return false;
          std::memcpy(data, bytes.data() + cursor, count);
          return true;
        }};
  }
};

static void finishPublication(Loop& loop) {
  for (uint32_t grain = 0; grain < 4096 && loop.hasPendingSnapshotPublish(); ++grain)
    loop.stepPendingSnapshotPublish(1);
  TEST_ASSERT_FALSE_MESSAGE(loop.hasPendingSnapshotPublish(), "snapshot drain exhausted 4096 grains");
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
}

static void appendPass(Loop& loop, unsigned index, uint8_t channel = 1,
                       uint32_t start = UINT32_MAX, uint32_t end = UINT32_MAX) {
  if (start == UINT32_MAX) start = kStarts[index];
  if (end == UINT32_MAX) end = start + 48;
  loop.beginCapture(index == 0 ? CapturePhase::Record : CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(start, channel, kPitches[index], 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(end, channel, kPitches[index], 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(index == 0 ? CommitReason::RecordStop : CommitReason::OverdubStop,
                                           start + 48));
}

static Track& restore(bool withRoot, unsigned passCount = 3, uint8_t trackIndex = 0,
                       uint32_t length = kLength, uint32_t recordStart = kStarts[0],
                       bool wrappedOverdub = false, uint8_t slotIndex = 0) {
  LoopEventStore::initPool();
  Track& track = trackManager.getTrack(trackIndex);
  track.setMidiChannel(trackIndex + 1);
  track.ensureLoopsAllocated();
  track.setActiveLoopIndex(slotIndex);
  BodyIo body;
  {
    Loop source;
    // The body belongs to this slot's persisted identity. An arbitrary body LoopId would
    // make Track's slot descriptor reject a later stop before its first worker grain.
    source.loopId = track.loopIdForSlot(slotIndex);
    source.loopLengthTicks = length;
    for (unsigned index = 0; index < passCount; ++index) {
      if (wrappedOverdub && index > 0) source.openOverdubSession(0, true);
      appendPass(source, index, trackIndex + 1,
                 index == 0 ? recordStart : (wrappedOverdub && index == 1 ? length - 24 : kStarts[index]),
                 wrappedOverdub && index == 1 ? 24 : UINT32_MAX);
      if (wrappedOverdub && index > 0) source.closeOverdubSession();
      if (withRoot) {
        TEST_ASSERT_TRUE(source.requestCommittedPassesRebuild());
        finishPublication(source);
      }
    }
    TEST_ASSERT_TRUE(writeLoopPersisted(body.io(), source));
  } // Original passes, root and pending publication have no surviving owner.
  Loop& loaded = track.getActiveLoop();
  {
    PersistedLoopSnapshot staging;
    ImmutableLoopSnapshotRef root;
    if (withRoot) {
      LoopSnapshotBodyFormatV1ReadResult parsed;
      TEST_ASSERT_TRUE(readLoopSnapshotBodyFormatV1(body.bytes.data(), body.bytes.size(), parsed));
      TEST_ASSERT_TRUE(parsed.hasPersistedOracle);
      TEST_ASSERT_NOT_NULL(parsed.loopSnapshot.get());
      staging = std::move(parsed.persistedOracle);
      root = std::move(parsed.loopSnapshot);
    } else {
      TEST_ASSERT_TRUE(readPersistedLoopSnapshot(body.io(), staging));
    }
    TEST_ASSERT_EQUAL(LoopSlotBodyApply::Applied,
                      applyLoadedLoopSlotBodyCommit(loaded, track, slotIndex, withRoot, staging, std::move(root)));
    // The actual loader drops the moved shell. Its cleanup must not free adopted chunks.
    releasePersistedLoopSnapshotChunks(staging);
  }
  TEST_ASSERT_EQUAL_UINT32(length, loaded.loopLengthTicks);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(track.loopIdForSlot(slotIndex), loaded.loopId,
                                  "restored body must match its Track-owned slot identity");
  TEST_ASSERT_EQUAL_UINT32(passCount, loaded.passes.capturePassCount());
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(passCount, TrackUndo::undoDepthForLoop(track, loaded),
                                  "restore must reconstruct actual global undo, not its native counter");
  TEST_ASSERT_EQUAL(withRoot, loaded.publishedSnapshotIsCurrent());
  track.forceSetState(TRACK_PLAYING);
  A34aBoundary::slotEnabled[trackIndex][slotIndex] = true;
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(slotIndex, false));
  loaded.refreshVisualCacheAfterPassStateChange();
  if (length <= 16 * Config::TICKS_PER_BAR) TEST_ASSERT_FALSE(loaded.visualCacheDirty);
  return track;
}

static void assertPayload(const Loop& loop, unsigned passCount = 3) {
  TEST_ASSERT_EQUAL_UINT32(passCount, loop.passes.capturePassCount());
  const auto check = [](const CommittedChunkIdList& ids, unsigned index) {
    TEST_ASSERT_FALSE(ids.empty());
    for (auto id : ids) TEST_ASSERT_GREATER_THAN_UINT32(0, LoopEventStore::chunkReferenceCount(id));
    MidiEventVec events;
    for (auto id : ids) LoopEventStore::appendChunkRefEvent(id, events);
    TEST_ASSERT_EQUAL_UINT32(2, events.size());
    TEST_ASSERT_EQUAL_UINT32(kStarts[index], events[0].tick);
    TEST_ASSERT_TRUE(events[0].isNoteOn());
    TEST_ASSERT_EQUAL_UINT8(kPitches[index], events[0].data.noteData.note);
    TEST_ASSERT_EQUAL_UINT32(kStarts[index] + 48, events[1].tick);
    TEST_ASSERT_TRUE(events[1].isNoteOff());
  };
  check(loop.passes.recordPass.committedChunkIds, 0);
  for (unsigned i = 1; i < passCount; ++i) check(loop.passes.overdubPasses[i - 1].committedChunkIds, i);
}

static void traceSource(const Track& track, const char* edge) {
  const auto& loop = track.getActiveLoop();
  printf("reboot source=%s rev=%u alias=%llu roles=%zu\n", edge, loop.playbackRevision,
         static_cast<unsigned long long>(LoopEventStoreSourceTestAccess::aliasGeneration()),
         track.retainedPlaybackSourceCountForSlot(0));
  const auto trace = [](const CommittedChunkIdList& ids) {
    for (auto id : ids)
      printf("reboot chunk=%u refs=%u lifecycle=%u\n", id, LoopEventStore::chunkReferenceCount(id),
             static_cast<unsigned>(LoopEventStore::chunkLifecycleState(id)));
  };
  trace(loop.passes.recordPass.committedChunkIds);
  for (const auto& pass : loop.passes.overdubPasses) trace(pass.committedChunkIds);
}

static void playCycle(Track& track, uint32_t cycle, unsigned expectedPasses, Track* control = nullptr) {
  Loop& loop = track.getActiveLoop();
  const uint32_t origin = cycle * kLength;
  MidiSendCapture::reset();
  for (uint32_t phase = 0; phase < kLength; ++phase) {
    const uint32_t tick = origin + phase;
    nativePlaybackDeadlineClock = {tick, 1};
    clockManager.assignCurrentTickSilently(tick);
    track.ensurePlaybackMergedEventsForSlot(0);
    track.playMidiEvents(tick, true);
    if (control != nullptr) {
      control->ensurePlaybackMergedEventsForSlot(0);
      control->playMidiEvents(tick, true);
    }
  }
  printf("reboot cycle=%u rev=%u roles=%zu midi=%zu root=%u\n", cycle, loop.playbackRevision,
         track.retainedPlaybackSourceCountForSlot(0), MidiSendCapture::count(),
         loop.publishedSnapshotIsCurrent());
  assertPayload(loop);
  MidiEventVec targetEvents, controlEvents;
  for (size_t i = 0; i < MidiSendCapture::count(); ++i) {
    const auto event = MidiSendCapture::at(i);
    if (event.channel == 1) targetEvents.push_back(event);
    else if (event.channel == 2) controlEvents.push_back(event);
    else TEST_FAIL_MESSAGE("foreign MIDI channel in restored playback");
  }
  if (control != nullptr) {
    assertPayload(control->getActiveLoop());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(6, controlEvents.size(), "unrelated sounding slot must keep playing");
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2 * expectedPasses, targetEvents.size(),
                                  "restored playback must emit each expected pair, never silence the slot");
  for (unsigned i = 0; i < expectedPasses; ++i) {
    const auto on = targetEvents[2 * i];
    const auto off = targetEvents[2 * i + 1];
    TEST_ASSERT_TRUE(on.isNoteOn()); TEST_ASSERT_TRUE(off.isNoteOff());
    TEST_ASSERT_EQUAL_UINT8(kPitches[i], on.data.noteData.note);
    TEST_ASSERT_EQUAL_UINT8(kPitches[i], off.data.noteData.note);
    TEST_ASSERT_EQUAL_UINT8(1, on.channel);
    TEST_ASSERT_EQUAL_UINT8(100, on.data.noteData.velocity);
    TEST_ASSERT_EQUAL_UINT32(i + 1, on.noteId);
    TEST_ASSERT_EQUAL_UINT32(i + 1, off.noteId);
    TEST_ASSERT_EQUAL_UINT32(kStarts[i], on.tick);
    TEST_ASSERT_EQUAL_UINT32(kStarts[i] + 48, off.tick);
  }
}

static void assertPaint(unsigned expectedPasses) {
  submitFrame(clockManager.getCurrentTick());
  completeFrame();
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(expectedPasses, completedFrame.notes.size(),
                                  "completed piano-roll frame must match post-undo committed content");
  for (unsigned i = 0; i < expectedPasses; ++i) {
    unsigned found = 0;
    for (const auto& note : completedFrame.notes) {
      if (note.note != kPitches[i]) continue;
      ++found;
      TEST_ASSERT_EQUAL_UINT32(kStarts[i], note.startTick);
      TEST_ASSERT_EQUAL_UINT32(kStarts[i] + 48, note.endTick);
    }
    TEST_ASSERT_EQUAL_UINT32(1, found);
  }
}
} // namespace RebootUndoFixture

static void test_saved_passes_first_cycle_survives_source_and_staging_destruction() {
  auto& track = RebootUndoFixture::restore(false);
  RebootUndoFixture::playCycle(track, 0, 3);
}
static void test_saved_root_first_cycle_survives_source_and_staging_destruction() {
  auto& track = RebootUndoFixture::restore(true);
  RebootUndoFixture::playCycle(track, 0, 3);
}
static void test_saved_slot_undo_replaces_same_window_before_snapshot_completion() {
  auto& track = RebootUndoFixture::restore(false);
  auto& loop = track.getActiveLoop();
  RebootUndoFixture::assertPaint(3);
  TrackUndo::undoForLoop(track, loop);
  TEST_ASSERT_EQUAL_UINT32(2, loop.visualCache.notes.size());
  TEST_ASSERT_FALSE(loop.visualCacheDirty);
  TEST_ASSERT_TRUE(loop.hasPendingSnapshotPublish());
  printf("reboot display rev=%u builtFrom=%u notes=%zu\n", loop.playbackRevision,
         loop.visualCache.builtFromRevision, loop.visualCache.notes.size());
  RebootUndoFixture::assertPayload(loop);
  RebootUndoFixture::assertPaint(2);
  TEST_ASSERT_NULL(loop.publishedSnapshotIdentity());
  TrackUndo::redoForLoop(track, loop);
  // Redo may owe another source; the contract only adopts a complete current representation.
  RebootUndoFixture::finishPublication(loop);
  loop.refreshVisualCacheAfterPassStateChange();
  RebootUndoFixture::assertPaint(3);
}
static void test_complete_rootless_pass_state_refresh_certifies_its_revision() {
  auto& track = RebootUndoFixture::restore(false);
  auto& loop = track.getActiveLoop();
  TrackUndo::undoForLoop(track, loop);
  TEST_ASSERT_FALSE(loop.visualCacheDirty);
  TEST_ASSERT_EQUAL_UINT32(2, loop.visualCache.notes.size());
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(loop.playbackRevision, loop.visualCache.builtFromRevision,
                                  "complete rootless pass-state refresh must establish content currency");
}
static void repeatedUndoRedo(bool withRoot) {
  auto& track = RebootUndoFixture::restore(withRoot);
  auto& control = RebootUndoFixture::restore(true, 3, 1);
  auto& loop = track.getActiveLoop();
  RebootUndoFixture::playCycle(track, 0, 3, &control);
  uint64_t stableAlias = LoopEventStoreSourceTestAccess::aliasGeneration();
  for (unsigned cycle = 0; cycle < 8; ++cycle) {
    RebootUndoFixture::traceSource(track, "before_undo");
    TrackUndo::undoForLoop(track, loop);
    RebootUndoFixture::traceSource(track, "after_undo");
    TEST_ASSERT_EQUAL_UINT32(2, TrackUndo::getUndoCount(track));
    RebootUndoFixture::playCycle(track, 2 * cycle + 1, 2, &control);
    TrackUndo::redoForLoop(track, loop);
    RebootUndoFixture::traceSource(track, "after_redo");
    TEST_ASSERT_EQUAL_UINT32(3, TrackUndo::getUndoCount(track));
    RebootUndoFixture::playCycle(track, 2 * cycle + 2, 3, &control);
    TEST_ASSERT_EQUAL_UINT32(2, track.retainedPlaybackSourceCountForSlot(0));
    TEST_ASSERT_EQUAL_UINT64(stableAlias, LoopEventStoreSourceTestAccess::aliasGeneration());
    for (auto chunk : loop.passes.recordPass.committedChunkIds)
      TEST_ASSERT_EQUAL_UINT32(3, LoopEventStore::chunkReferenceCount(chunk));
    for (const auto& pass : loop.passes.overdubPasses)
      for (auto chunk : pass.committedChunkIds) TEST_ASSERT_EQUAL_UINT32(3, LoopEventStore::chunkReferenceCount(chunk));
    TEST_ASSERT_EQUAL_UINT32(loop.playbackRevision, track.servingRetainedRevisionForSlot(0));
  }
}
static void test_saved_slot_repeated_undo_redo_keeps_playing() { repeatedUndoRedo(false); }
static void test_saved_root_repeated_undo_redo_keeps_playing() { repeatedUndoRedo(true); }

static void test_loaded_retiring_source_release_preserves_live_pass_payloads() {
  auto& track = RebootUndoFixture::restore(false);
  auto& loop = track.getActiveLoop();
  RebootUndoFixture::playCycle(track, 0, 3);
  TrackUndo::undoForLoop(track, loop);
  RebootUndoFixture::playCycle(track, 1, 2);
  const auto aliasBefore = LoopEventStoreSourceTestAccess::aliasGeneration();
  TEST_ASSERT_EQUAL_UINT32(2, track.retainedPlaybackSourceCountForSlot(0));
  // Characterize the existing retirement operation explicitly, outside source admission.
  // This is not production wiring or permission to reapply the rejected repair.
  TEST_ASSERT_TRUE(track.tryReleaseRetainedPlaybackSource(0));
  RebootUndoFixture::traceSource(track, "after_explicit_retirement");
  RebootUndoFixture::assertPayload(loop);
  TEST_ASSERT_EQUAL_UINT32(1, track.retainedPlaybackSourceCountForSlot(0));
  TEST_ASSERT_GREATER_THAN_UINT64(aliasBefore, LoopEventStoreSourceTestAccess::aliasGeneration());
  TrackUndo::redoForLoop(track, loop);
  RebootUndoFixture::playCycle(track, 2, 3);
}

static Track& prepareDescriptorReuse() {
  auto& track = RebootUndoFixture::restore(false);
  RebootUndoFixture::playCycle(track, 0, 3);
  TrackUndo::undoForLoop(track, track.getActiveLoop());
  RebootUndoFixture::playCycle(track, 1, 2);
  TrackUndo::redoForLoop(track, track.getActiveLoop());
  TEST_ASSERT_EQUAL_UINT32(2, track.retainedPlaybackSourceCountForSlot(0));
  return track;
}

static void assertReuseRefused(Track& track) {
  const auto revision = track.servingRetainedRevisionForSlot(0);
  const auto alias = LoopEventStoreSourceTestAccess::aliasGeneration();
  TEST_ASSERT_EQUAL(Track::PlaybackSourceRetainResult::RoleCapacityReached,
                    track.retainPlaybackSourceForSlot(0, track.getActiveLoop().playbackRevision,
                                                       track.getPlaybackGeneration()));
  TEST_ASSERT_EQUAL_UINT32(revision, track.servingRetainedRevisionForSlot(0));
  TEST_ASSERT_EQUAL_UINT64(alias, LoopEventStoreSourceTestAccess::aliasGeneration());
  TEST_ASSERT_EQUAL_UINT32(2, track.retainedPlaybackSourceCountForSlot(0));
}

static void test_quiescent_descriptor_reuse_keeps_chunk_ownership_and_updates_index() {
  auto& track = prepareDescriptorReuse();
  auto& loop = track.getActiveLoop();
  const auto alias = LoopEventStoreSourceTestAccess::aliasGeneration();
  std::vector<uint16_t> references;
  for (const auto& pass : loop.passes.overdubPasses)
    for (auto chunk : pass.committedChunkIds) references.push_back(LoopEventStore::chunkReferenceCount(chunk));
  TEST_ASSERT_EQUAL(Track::PlaybackSourceRetainResult::Admitted,
                    track.retainPlaybackSourceForSlot(0, loop.playbackRevision, track.getPlaybackGeneration()));
  TEST_ASSERT_EQUAL_UINT64(alias, LoopEventStoreSourceTestAccess::aliasGeneration());
  size_t index = 0;
  for (const auto& pass : loop.passes.overdubPasses)
    for (auto chunk : pass.committedChunkIds)
      TEST_ASSERT_EQUAL_UINT32(references[index++], LoopEventStore::chunkReferenceCount(chunk));
  TEST_ASSERT_EQUAL_UINT32(loop.playbackRevision, track.servingRetainedRevisionForSlot(0));
  auto* runtime = TrackCaptureStopTestAccess::runtime(track);
  TEST_ASSERT_EQUAL_UINT32(loop.playbackRevision, runtime->retainedIndex.revision);
  TEST_ASSERT_TRUE(runtime->retainedIndex.published);
  RebootUndoFixture::assertPayload(loop);
}

static void test_descriptor_reuse_refuses_capture_finalization_and_retained_gather() {
  auto& track = prepareDescriptorReuse();
  auto& loop = track.getActiveLoop();
  auto* runtime = TrackCaptureStopTestAccess::runtime(track);
  loop.beginCapture(CapturePhase::Overdub);
  assertReuseRefused(track);
  loop.discardCapture();
  loop.openOverdubSession(0);
  assertReuseRefused(track);
  loop.closeOverdubSession();
  PendingStopAdmission stop;
  runtime->setCaptureStopOwner(stop, 77);
  assertReuseRefused(track);
  TEST_ASSERT_TRUE(runtime->clearCaptureStopOwner(stop, 77));
  // A controlled reader boundary: only refusal is asserted; this cannot manufacture acceptance.
  runtime->retainedGatherPhase = LoopPlaybackRuntime::RetainedGatherPhase::Gathering;
  assertReuseRefused(track);
  runtime->discardRetainedGather();
  TEST_ASSERT_EQUAL(Track::PlaybackSourceRetainResult::Admitted,
                    track.retainPlaybackSourceForSlot(0, loop.playbackRevision, track.getPlaybackGeneration()));
}

static void test_descriptor_reuse_refuses_inflight_build_and_ready_or_published_reader() {
  auto& track = prepareDescriptorReuse();
  auto& loop = track.getActiveLoop();
  auto* runtime = TrackCaptureStopTestAccess::runtime(track);
  TEST_ASSERT_TRUE(runtime->prewarmViews());
  auto* views = runtime->viewSetIfPrewarmed();
  TEST_ASSERT_NOT_NULL(views);
  const auto oldRevision = runtime->retainedSources[0].revision;
  MidiEvent rows[] = {MidiEvent::NoteOn(96, 1, 48, 100), MidiEvent::NoteOff(144, 1, 48, 0)};
  auto& candidate = views->views[views->lifecycle.build];
  TEST_ASSERT_TRUE(runtime->viewBuild.begin(candidate, rows, 2,
      PlaybackViewSourceIdentity{oldRevision, track.getPlaybackGeneration(), nullptr},
      PlaybackCoverageInterval{0, loop.loopLengthTicks, loop.loopLengthTicks}, {}, {}));
  assertReuseRefused(track);
  for (unsigned grain = 0; grain < 32 && runtime->viewBuild.active(); ++grain) {
    if (runtime->viewBuild.nextGrain() == RuntimePlaybackViewBuildGrain::Fill)
      runtime->viewBuild.stepFill(64);
    else runtime->viewBuild.stepOrder(64);
  }
  TEST_ASSERT_FALSE(runtime->viewBuild.active());
  TEST_ASSERT_TRUE(candidate.complete);
  TEST_ASSERT_TRUE(views->lifecycle.publishCompletedBuildAsReadySuccessor(views->views));
  assertReuseRefused(track);
  TEST_ASSERT_TRUE(views->lifecycle.handoff(views->views));
  assertReuseRefused(track);
}

static void test_descriptor_reuse_refuses_new_capture_pass_membership() {
  auto& track = prepareDescriptorReuse();
  RebootUndoFixture::appendPass(track.getActiveLoop(), 2);
  assertReuseRefused(track);
}

static void test_descriptor_reuse_refuses_new_edit_pass_membership() {
  auto& track = prepareDescriptorReuse();
  EditPass row;
  row.propertyType = EditPropertyType::Velocity;
  row.targetNoteId = 1;
  row.velocity = 85;
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, track.getActiveLoop().saveNoteEditPass(0, row));
  assertReuseRefused(track);
}

static void test_descriptor_reuse_refuses_a_noncurrent_request_identity() {
  auto& track = prepareDescriptorReuse();
  auto& loop = track.getActiveLoop();
  TEST_ASSERT_EQUAL(Track::PlaybackSourceRetainResult::RoleCapacityReached,
                    track.retainPlaybackSourceForSlot(0, loop.playbackRevision + 1, track.getPlaybackGeneration()));
  TEST_ASSERT_EQUAL(Track::PlaybackSourceRetainResult::RoleCapacityReached,
                    track.retainPlaybackSourceForSlot(0, loop.playbackRevision, track.getPlaybackGeneration() + 1));
  TEST_ASSERT_EQUAL(Track::PlaybackSourceRetainResult::Admitted,
                    track.retainPlaybackSourceForSlot(0, loop.playbackRevision, track.getPlaybackGeneration()));
}

static void test_descriptor_reuse_refuses_replaced_bodies_with_the_same_pass_ids() {
  auto& track = prepareDescriptorReuse();
  auto& loop = track.getActiveLoop();
  auto snapshot = loop.sharePassesSnapshot();
  TEST_ASSERT_NOT_NULL(snapshot.get());
  loop.restorePassesSnapshot(*snapshot);
  releasePersistedLoopSnapshotChunks(*snapshot);
  const auto id = loop.passes.overdubPasses.back().id;
  TEST_ASSERT_TRUE(loop.setCapturePassState(id, CapturePassState::Disabled));
  TEST_ASSERT_TRUE(loop.setCapturePassState(id, CapturePassState::Active));
  // Both old revisions remain retained; equal logical IDs do not make newly cloned chunks identical.
  RebootUndoFixture::assertPayload(loop);
  assertReuseRefused(track);
}

static void test_fresh_record_and_successor_overdub_keep_their_accepted_rows() {
  auto& track = beginRecord();
  const auto recordId = acceptPair(track, 84, 12, 80);
  growRecordGeometry(track, Config::TICKS_PER_BAR);
  track.stopRecording(Config::TICKS_PER_BAR);
  for (unsigned turn = 0; turn < 4096 && track.hasStopInFlightForSlot(0); ++turn) {
    track.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
    track.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
  }
  TEST_ASSERT_FALSE(track.hasStopInFlightForSlot(0));
  auto& loop = track.getActiveLoop();
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
  RebootUndoFixture::finishPublication(loop);
  clockManager.assignCurrentTickSilently(800);
  nativePlaybackDeadlineClock = {800, 1};
  track.startOverdubbing(800);
  TEST_ASSERT_TRUE(track.isOverdubbing());
  const auto overdubId = acceptPair(track, 85, 816, 880);
  clockManager.assignCurrentTickSilently(896);
  nativePlaybackDeadlineClock = {896, 1};
  track.stopOverdubbing();
  for (unsigned turn = 0; turn < 4096 && track.hasStopInFlightForSlot(0); ++turn) {
    track.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
    track.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
  }
  TEST_ASSERT_FALSE(track.hasStopInFlightForSlot(0));
  RebootUndoFixture::finishPublication(loop);
  SessionMidiEventVec events;
  loop.copyEffectiveCommittedEvents(events);
  TEST_ASSERT_EQUAL_UINT32(4, events.size());
  unsigned recordRows = 0, overdubRows = 0;
  for (const auto& event : events) {
    if (event.noteId == recordId && event.data.noteData.note == 84) ++recordRows;
    if (event.noteId == overdubId && event.data.noteData.note == 85) ++overdubRows;
  }
  TEST_ASSERT_EQUAL_UINT32(2, recordRows);
  TEST_ASSERT_EQUAL_UINT32(2, overdubRows);
}

static void test_scrolled_long_loop_keeps_interval_identity_until_root_handoff() {
  constexpr auto bar = Config::TICKS_PER_BAR;
  auto& track = RebootUndoFixture::restore(false, 3, 0, 24 * bar, 18 * bar + 96);
  auto& loop = track.getActiveLoop();
  for (unsigned grain = 0; grain < 32 && loop.visualCacheDirty; ++grain)
    loop.rebuildVisualCacheIdleSlice(4, 0);
  TEST_ASSERT_FALSE(loop.visualCacheDirty);
  DisplayCaptureTestAccess::setDetailedWindowBars(0, 4);
  submitFrame(18 * bar + 200); completeFrame();
  TEST_ASSERT_EQUAL_UINT32(1, completedFrame.notes.size());
  TEST_ASSERT_EQUAL_UINT8(48, completedFrame.notes[0].note);
  const auto oldKey = completedFrame.source;
  TrackUndo::undoForLoop(track, loop);
  TEST_ASSERT_TRUE(loop.visualCacheDirty);
  nativePlaybackDeadlineClock = {0, 2}; // A seek changes position, never the retained frame's coverage.
  submitFrame(0); completeFrame();
  TEST_ASSERT_EQUAL_UINT32(oldKey.playbackRevision, completedFrame.source.playbackRevision);
  TEST_ASSERT_EQUAL_UINT32(oldKey.windowStart, completedFrame.source.windowStart);
  RebootUndoFixture::finishPublication(loop);
  submitFrame(0); completeFrame();
  TEST_ASSERT_EQUAL_UINT32(loop.playbackRevision, completedFrame.source.playbackRevision);
  TEST_ASSERT_EQUAL_UINT32(0, completedFrame.source.windowStart);
  TEST_ASSERT_EQUAL_UINT32(1, completedFrame.notes.size());
  TEST_ASSERT_EQUAL_UINT8(60, completedFrame.notes[0].note);
}

static void test_actual_slot_switch_and_body_replacement_reject_prior_frame_identity() {
  auto& track = RebootUndoFixture::restore(false);
  RebootUndoFixture::assertPaint(3);
  TrackUndo::undoForLoop(track, track.getActiveLoop());
  RebootUndoFixture::assertPaint(2);
  RestoreHarness::setRuntimeTransportIdle();
  nativeTransportRunning = false;
  RebootUndoFixture::restore(true, 3, 0, RebootUndoFixture::kLength,
                             RebootUndoFixture::kStarts[0], false, 1);
  TEST_ASSERT_EQUAL_UINT8(1, track.getActiveLoopIndex());
  TEST_ASSERT_EQUAL_UINT32(3, DisplayCaptureTestAccess::resolveCommitted(track, 1, 0).size());
  TEST_ASSERT_EQUAL_UINT8(1, DisplayCaptureTestAccess::key().slot);
  track.setActiveLoopIndex(0);
  TEST_ASSERT_EQUAL_UINT32(2, DisplayCaptureTestAccess::resolveCommitted(track, 0, 0).size());
  TEST_ASSERT_EQUAL_UINT8(0, DisplayCaptureTestAccess::key().slot);
  // A real loaded replacement, while stopped, has its own current root/key even on the same physical slot.
  RestoreHarness::setRuntimeTransportIdle();
  TrackUndo::clearUndoHistoryForSlot(track, 0);
  RebootUndoFixture::restore(true);
  TEST_ASSERT_EQUAL_UINT32(3, DisplayCaptureTestAccess::resolveCommitted(track, 0, 0).size());
  TEST_ASSERT_EQUAL_PTR(track.getActiveLoop().publishedSnapshotIdentity(),
                        DisplayCaptureTestAccess::key().publishedSnapshotIdentity);
}

static void test_paint_does_not_advance_or_reset_an_active_save_item() {
  using namespace StorageManagerInternal;
  resetDeferredSaveJobState();
  resetPersistenceWorkItemJobState();
  PersistenceWorkQueue::resetForTests();
  storageSession.currentWorkspaceSave.pending = false;
  workspaceFooterPersistDeferred = false;
  armPostLoadCommitSaveHoldoff(0);
  auto& track = RebootUndoFixture::restore(false);
  RebootUndoFixture::assertPaint(3);
  TrackUndo::undoForLoop(track, track.getActiveLoop());
  RestoreHarness::setRuntimeTransportIdle();
  nativeTransportRunning = false;
  for (unsigned grain = 0; grain < 32 && !StorageManager::hasActivePersistenceWorkItem(); ++grain)
    stepPersistenceWorkItem(LOOPER_IDLE); // The scheduler's existing work-item owner, one controlled call.
  TEST_ASSERT_TRUE(StorageManager::hasActivePersistenceWorkItem());
  const auto slices = storageSession.persistenceWorkItem.workItemSliceCount;
  const auto writerCount = PersistenceWorkQueue::writingWorkItemCount();
  const auto files = SD.files;
  RebootUndoFixture::assertPaint(2);
  const bool stillActive = StorageManager::hasActivePersistenceWorkItem();
  const auto afterSlices = storageSession.persistenceWorkItem.workItemSliceCount;
  const auto afterWriterCount = PersistenceWorkQueue::writingWorkItemCount();
  const bool cardUnchanged = files == SD.files;
  resetPersistenceWorkItemJobState();
  resetDeferredSaveJobState();
  PersistenceWorkQueue::resetForTests();
  TEST_ASSERT_TRUE(stillActive);
  TEST_ASSERT_EQUAL_UINT32(slices, afterSlices);
  TEST_ASSERT_EQUAL_UINT32(writerCount, afterWriterCount);
  TEST_ASSERT_TRUE(cardUnchanged);
}

static void test_repeated_undo_redo_repaints_while_snapshot_publication_stays_pending() {
  auto& track = RebootUndoFixture::restore(false);
  auto& loop = track.getActiveLoop();
  RebootUndoFixture::assertPaint(3);
  constexpr unsigned expected[] = {2, 3, 2, 1, 2, 3};
  constexpr bool undo[] = {true, false, true, true, false, false};
  for (unsigned round = 0; round < 8; ++round) {
    for (unsigned edge = 0; edge < 6; ++edge) {
      const auto tick = 64u * (round * 6 + edge + 1);
      clockManager.assignCurrentTickSilently(tick);
      nativePlaybackDeadlineClock = {tick, 1};
      if (undo[edge]) TrackUndo::undoForLoop(track, loop);
      else TrackUndo::redoForLoop(track, loop);
      TEST_ASSERT_TRUE(loop.hasPendingSnapshotPublish());
      const auto queued = loop.pendingSnapshotQueuedPassCount();
      const auto applied = loop.pendingSnapshotAppliedPassCount();
      // No snapshot worker, finishPublication, injected root, or transport restart between gestures.
      RebootUndoFixture::assertPaint(expected[edge]);
      for (unsigned i = 0; i < expected[edge]; ++i)
        assertNote(completedFrame.notes, i + 1, RebootUndoFixture::kPitches[i],
                   RebootUndoFixture::kStarts[i], RebootUndoFixture::kStarts[i] + 48);
      TEST_ASSERT_FALSE(loop.visualCacheDirty);
      TEST_ASSERT_EQUAL_UINT32(loop.playbackRevision, loop.visualCache.builtFromRevision);
      TEST_ASSERT_EQUAL_UINT32(loop.playbackRevision, completedFrame.source.playbackRevision);
      TEST_ASSERT_NULL(loop.publishedSnapshotIdentity());
      TEST_ASSERT_TRUE(loop.hasPendingSnapshotPublish());
      TEST_ASSERT_EQUAL_UINT32(queued, loop.pendingSnapshotQueuedPassCount());
      TEST_ASSERT_EQUAL_UINT32(applied, loop.pendingSnapshotAppliedPassCount());
      RebootUndoFixture::assertPayload(loop);
    }
  }
}

static void test_ordinary_idle_preserves_the_pending_publication_hold() {
  auto& track = RebootUndoFixture::restore(false);
  auto& loop = track.getActiveLoop();
  TrackUndo::undoForLoop(track, loop);
  TEST_ASSERT_TRUE(loop.hasPendingSnapshotPublish());
  loop.markDisplayCachesStale();
  const auto revision = loop.visualCache.revision;
  const auto count = loop.visualCache.notes.size();
  loop.rebuildVisualCacheIdleSlice(4, 0);
  TEST_ASSERT_TRUE(loop.visualCacheDirty);
  TEST_ASSERT_EQUAL_UINT32(revision, loop.visualCache.revision);
  TEST_ASSERT_EQUAL_UINT32(count, loop.visualCache.notes.size());
}

static void test_explicit_gap_refresh_refuses_live_session_and_sealed_capture_borrow() {
  auto& track = RebootUndoFixture::restore(false);
  auto& loop = track.getActiveLoop();
  TrackUndo::undoForLoop(track, loop);
  TEST_ASSERT_TRUE(loop.hasPendingSnapshotPublish());
  loop.beginCapture(CapturePhase::Overdub);
  loop.refreshVisualCacheAfterPassStateChange();
  TEST_ASSERT_TRUE(loop.visualCacheDirty);
  TEST_ASSERT_EQUAL_UINT32(2, loop.visualCache.notes.size());
  loop.discardCapture();
  loop.openOverdubSession(0);
  loop.refreshVisualCacheAfterPassStateChange();
  TEST_ASSERT_TRUE(loop.visualCacheDirty);
  TEST_ASSERT_EQUAL_UINT32(2, loop.visualCache.notes.size());
  loop.closeOverdubSession();

  LoopEventStore privateStore;
  LoopEventStore::CaptureRange sealed;
  TEST_ASSERT_TRUE(sealed.reserveTail());
  TEST_ASSERT_TRUE(privateStore.append(MidiEvent::NoteOn(12, 1, 84, 100)));
  TEST_ASSERT_TRUE(privateStore.append(MidiEvent::NoteOff(80, 1, 84, 0)));
  TEST_ASSERT_TRUE(privateStore.sealCaptureRange(sealed));
  TEST_ASSERT_TRUE(loop.borrowSealedCaptureRange(sealed, &sealed));
  TEST_ASSERT_FALSE(loop.captureActive());
  TEST_ASSERT_FALSE(loop.hasOverdubSession());
  TEST_ASSERT_FALSE(loop.hasPendingCapturePass());
  loop.refreshVisualCacheAfterPassStateChange();
  const bool dirty = loop.visualCacheDirty;
  const auto count = loop.visualCache.notes.size();
  TEST_ASSERT_TRUE(loop.releaseSealedCaptureRangeBorrow(&sealed));
  TEST_ASSERT_TRUE(dirty);
  TEST_ASSERT_EQUAL_UINT32(2, count);
}

static void test_redo_projection_refusal_during_gap_recovers_without_publishing_root() {
  auto& track = RebootUndoFixture::restore(false);
  auto& loop = track.getActiveLoop();
  RebootUndoFixture::assertPaint(3);
  TrackUndo::undoForLoop(track, loop);
  RebootUndoFixture::assertPaint(2);
  const auto oldKey = completedFrame.source;
  MemoryMonitor::setNativeForcedReserveDenied(true);
  TrackUndo::redoForLoop(track, loop);
  MemoryMonitor::setNativeForcedReserveDenied(false);
  TEST_ASSERT_TRUE(loop.visualCacheDirty);
  RebootUndoFixture::assertPaint(2);
  TEST_ASSERT_EQUAL_UINT32(oldKey.playbackRevision, completedFrame.source.playbackRevision);
  loop.refreshVisualCacheAfterPassStateChange(); // Retry the same explicit owner after resource recovery.
  RebootUndoFixture::assertPaint(3);
  TEST_ASSERT_FALSE(loop.visualCacheDirty);
  TEST_ASSERT_NULL(loop.publishedSnapshotIdentity());
  TEST_ASSERT_TRUE(loop.hasPendingSnapshotPublish());
}

static void test_current_empty_window_is_success_but_stale_or_invalid_is_not() {
  auto& track = RebootUndoFixture::restore(false);
  auto& loop = track.getActiveLoop();
  TrackUndo::undoForLoop(track, loop);
  DisplayNoteVec notes;
  const uint32_t bar = Config::TICKS_PER_BAR;
  TEST_ASSERT_TRUE(loop.tryCopyCurrentVisualCacheDisplayNotesInWindow(4 * bar, bar, notes));
  TEST_ASSERT_TRUE(notes.empty());
  TEST_ASSERT_FALSE(loop.tryCopyCurrentVisualCacheDisplayNotesInWindow(8 * bar, bar, notes));
  TEST_ASSERT_FALSE(loop.tryCopyCurrentVisualCacheDisplayNotesInWindow(7 * bar, 2 * bar, notes));
  TEST_ASSERT_FALSE(loop.tryCopyCurrentVisualCacheDisplayNotesInWindow(0, 0, notes));
  loop.markDisplayCachesStale();
  TEST_ASSERT_FALSE(loop.tryCopyCurrentVisualCacheDisplayNotesInWindow(4 * bar, bar, notes));
  TEST_ASSERT_TRUE(notes.empty());
}

static void test_same_window_undo_paints_an_authoritative_empty_interval() {
  const uint32_t bar = Config::TICKS_PER_BAR;
  auto& track = RebootUndoFixture::restore(false, 2, 0, RebootUndoFixture::kLength, 7 * bar + 96);
  auto& loop = track.getActiveLoop();
  DisplayCaptureTestAccess::setDetailedWindowBars(0, 4);
  submitFrame(0); completeFrame();
  TEST_ASSERT_EQUAL_UINT32(1, completedFrame.notes.size());
  TEST_ASSERT_EQUAL_UINT8(60, completedFrame.notes[0].note);
  TrackUndo::undoForLoop(track, loop);
  TEST_ASSERT_EQUAL_UINT32(loop.playbackRevision, loop.visualCache.builtFromRevision);
  submitFrame(0); completeFrame();
  TEST_ASSERT_TRUE_MESSAGE(completedFrame.notes.empty(), "a valid empty viewport must replace the held note");
  TEST_ASSERT_EQUAL_UINT32(loop.playbackRevision, completedFrame.source.playbackRevision);
}

static void test_partial_long_loop_refresh_does_not_certify_unbuilt_content() {
  const uint32_t bar = Config::TICKS_PER_BAR;
  auto& track = RebootUndoFixture::restore(false, 3, 0, 24 * bar);
  auto& loop = track.getActiveLoop();
  TrackUndo::undoForLoop(track, loop);
  TEST_ASSERT_TRUE(loop.visualCacheDirty);
  DisplayNoteVec notes;
  TEST_ASSERT_FALSE(loop.tryCopyCurrentVisualCacheDisplayNotesInWindow(18 * bar, 4 * bar, notes));
  TEST_ASSERT_TRUE(notes.empty());
  TEST_ASSERT_NOT_EQUAL(loop.playbackRevision, loop.visualCache.builtFromRevision);
}

static void test_projection_refusal_preserves_frame_and_key_then_recovers() {
  auto& track = RebootUndoFixture::restore(false);
  auto& loop = track.getActiveLoop();
  RebootUndoFixture::assertPaint(3);
  const auto before = DisplayCaptureTestAccess::key();
  TrackUndo::undoForLoop(track, loop);
  MemoryMonitor::setNativeForcedReserveDenied(true);
  const auto heldCount = DisplayCaptureTestAccess::resolveCommitted(track, 0, 0).size();
  const auto heldKey = DisplayCaptureTestAccess::key();
  MemoryMonitor::setNativeForcedReserveDenied(false);
  TEST_ASSERT_EQUAL_UINT32(3, heldCount);
  TEST_ASSERT_EQUAL_UINT32(before.playbackRevision, heldKey.playbackRevision);
  TEST_ASSERT_EQUAL_UINT32(2, DisplayCaptureTestAccess::resolveCommitted(track, 0, 0).size());
  TEST_ASSERT_EQUAL_UINT32(loop.playbackRevision, DisplayCaptureTestAccess::key().playbackRevision);
  RebootUndoFixture::assertPaint(2);
}

static void test_denied_pass_refresh_does_not_certify_an_empty_projection() {
  auto& track = RebootUndoFixture::restore(false);
  auto& loop = track.getActiveLoop();
  const auto before = loop.visualCache.builtFromRevision;
  MemoryMonitor::setNativeForcedReserveDenied(true);
  TrackUndo::undoForLoop(track, loop);
  MemoryMonitor::setNativeForcedReserveDenied(false);
  TEST_ASSERT_TRUE(loop.visualCacheDirty);
  TEST_ASSERT_EQUAL_UINT32(before, loop.visualCache.builtFromRevision);
  TEST_ASSERT_NOT_EQUAL(loop.playbackRevision, loop.visualCache.builtFromRevision);
  RebootUndoFixture::assertPayload(loop);
}

static void test_wrap_append_refusal_keeps_slice_retryable_and_frame_unqualified() {
  const uint32_t bar = Config::TICKS_PER_BAR;
  auto& track = RebootUndoFixture::restore(false, 3, 0, RebootUndoFixture::kLength,
                                          RebootUndoFixture::kStarts[0], true);
  auto& loop = track.getActiveLoop();
  const auto lastPass = loop.passes.overdubPasses.back().id;
  // Calibrate only the allocator boundary, not expected geometry: count the successful window
  // projection's reserve attempts for the exact post-undo inventory, before the wrap append.
  TEST_ASSERT_TRUE(loop.setCapturePassState(lastPass, CapturePassState::Disabled));
  DisplayNoteVec window;
  const auto beforeAttempts = MemoryMonitor::nativeProjectionReserveAttemptCount();
  LoopContentResolution::resolveDisplayNotesInWindow(loop.passes, loop.loopLengthTicks, 0, 5 * bar, window);
  const auto windowAttempts = MemoryMonitor::nativeProjectionReserveAttemptCount() - beforeAttempts;
  TEST_ASSERT_TRUE(loop.setCapturePassState(lastPass, CapturePassState::Active));
  loop.refreshVisualCacheAfterPassStateChange();
  submitFrame(0); completeFrame();
  const auto beforeFrame = completedFrame;
  const auto beforeRevision = loop.visualCache.builtFromRevision;
  // One additional admitted reserve copies the resolved window; refusal begins in wrap completion.
  MemoryMonitor::setNativeProjectionReserveDenialAfter(windowAttempts + 1);
  const auto beforeRefusals = MemoryMonitor::displayProjectionRefusalCount();
  TrackUndo::undoForLoop(track, loop);
  MemoryMonitor::setNativeProjectionReserveDenialAfter(UINT32_MAX);
  TEST_ASSERT_GREATER_THAN_UINT32(beforeRefusals, MemoryMonitor::displayProjectionRefusalCount());
  TEST_ASSERT_TRUE_MESSAGE(loop.visualCacheDirty, "denied wrap completion must leave its slice retryable");
  TEST_ASSERT_EQUAL_UINT32(beforeRevision, loop.visualCache.builtFromRevision);
  submitFrame(0); completeFrame();
  TEST_ASSERT_EQUAL_UINT32(beforeFrame.notes.size(), completedFrame.notes.size());
  TEST_ASSERT_EQUAL_UINT32(beforeFrame.source.playbackRevision, completedFrame.source.playbackRevision);
  // Finish the pending owner, then rebuild normally. Snapshot-backed DisplayNotes carry the
  // canonical wrapping span (start > end); the paint projection splits its head and tail.
  RebootUndoFixture::finishPublication(loop);
  loop.refreshVisualCacheAfterPassStateChange();
  submitFrame(0); completeFrame();
  unsigned wrappedCount = 0;
  for (const auto& note : completedFrame.notes) {
    if (note.note != 60) continue;
    ++wrappedCount;
    TEST_ASSERT_EQUAL_UINT32(loop.loopLengthTicks - 24, note.startTick);
    TEST_ASSERT_EQUAL_UINT32(24, note.endTick);
  }
  TEST_ASSERT_EQUAL_UINT32(1, wrappedCount);
  TEST_ASSERT_EQUAL_UINT32(loop.playbackRevision, completedFrame.source.playbackRevision);
}

static void test_rejected_snapshot_overlay_does_not_certify_fallback_cache() {
  auto& track = RebootUndoFixture::restore(true);
  auto& loop = track.getActiveLoop();
  const auto before = loop.visualCache.builtFromRevision;
  TrackUndo::undoForLoop(track, loop);
  TEST_ASSERT_TRUE(loop.lastVisualCacheIdleOverlayRejected());
  TEST_ASSERT_EQUAL_UINT32(before, loop.visualCache.builtFromRevision);
  TEST_ASSERT_NOT_EQUAL(loop.playbackRevision, loop.visualCache.builtFromRevision);
  DisplayNoteVec notes;
  TEST_ASSERT_FALSE(loop.tryCopyCurrentVisualCacheDisplayNotesInWindow(0, loop.loopLengthTicks, notes));
}

static void test_undo_during_resolve_keeps_inflight_frame_then_paints_new_revision() {
  auto& track = RebootUndoFixture::restore(false);
  auto& loop = track.getActiveLoop();
  serviceDisplay(0); // Existing frame was resolved before the undo edge.
  const auto before = DisplayCaptureTestAccess::key();
  TrackUndo::undoForLoop(track, loop);
  serviceDisplay(0); serviceDisplay(0);
  completeFrame();
  TEST_ASSERT_EQUAL_UINT32(3, completedFrame.notes.size());
  TEST_ASSERT_EQUAL_UINT32(before.playbackRevision, completedFrame.source.playbackRevision);
  RebootUndoFixture::assertPaint(2);
  TEST_ASSERT_EQUAL_UINT32(loop.playbackRevision, completedFrame.source.playbackRevision);
}

static void test_switch_and_clear_cannot_reuse_the_prior_tracks_undo_frame() {
  auto& track = RebootUndoFixture::restore(false);
  auto& other = RebootUndoFixture::restore(true, 3, 1);
  auto& loop = track.getActiveLoop();
  RebootUndoFixture::assertPaint(3);
  TrackUndo::undoForLoop(track, loop);
  TEST_ASSERT_EQUAL_UINT32(2, DisplayCaptureTestAccess::resolveCommitted(track, 0, 0).size());
  TEST_ASSERT_EQUAL_UINT32(3, DisplayCaptureTestAccess::resolveCommitted(other, 0, 0).size());
  TEST_ASSERT_EQUAL_UINT8(1, DisplayCaptureTestAccess::key().track);
  TEST_ASSERT_EQUAL_UINT32(2, DisplayCaptureTestAccess::resolveCommitted(track, 0, 0).size());
  TEST_ASSERT_EQUAL_UINT8(0, DisplayCaptureTestAccess::key().track);
  StorageManagerInternal::resetLoopSlotToEmpty(loop, 0);
  TEST_ASSERT_TRUE(DisplayCaptureTestAccess::resolveCommitted(track, 0, 0).empty());
  TEST_ASSERT_FALSE(DisplayCaptureTestAccess::key().valid);
}

static void test_clock_stop_restart_and_pending_save_do_not_restore_old_painted_notes() {
  auto& track = RebootUndoFixture::restore(false);
  auto& loop = track.getActiveLoop();
  RebootUndoFixture::assertPaint(3);
  TrackUndo::undoForLoop(track, loop);
  TEST_ASSERT_TRUE(StorageManager::hasDeferredSaveWork());
  const auto filesBefore = SD.files;
  nativeTransportRunning = false;
  track.forceSetState(TRACK_STOPPED);
  RebootUndoFixture::assertPaint(2);
  nativeTransportRunning = true;
  nativePlaybackDeadlineClock = {RebootUndoFixture::kLength, 2};
  track.forceSetState(TRACK_PLAYING);
  RebootUndoFixture::assertPaint(2);
  TEST_ASSERT_TRUE(StorageManager::hasDeferredSaveWork());
  TEST_ASSERT_TRUE_MESSAGE(filesBefore == SD.files, "display may not drain or reset the queued save");
}

static void recordReplacement(bool truncated) {
  auto& track = RebootUndoFixture::restore(true);
  RebootUndoFixture::playCycle(track, 0, 3);
  constexpr uint32_t origin = RebootUndoFixture::kLength;
  clockManager.assignCurrentTickSilently(origin);
  nativePlaybackDeadlineClock = {origin, 1};
  TEST_ASSERT_TRUE(track.startRecording(origin)); // Explicit replacement, not source maintenance.
  TEST_ASSERT_TRUE(track.isRecording());
  const auto id = acceptPair(track, 84, origin + 12, origin + 80);
  const uint32_t stopTick = origin + (truncated ? 2u : 1u) * Config::TICKS_PER_BAR;
  growRecordGeometry(track, stopTick);
  TEST_ASSERT_TRUE(track.isRecording());
  printf("reboot record before_stop capture=%zu length=%u state=%u\n",
         track.getActiveLoop().capture.store.size(), track.getActiveLoop().loopLengthTicks,
         static_cast<unsigned>(track.getState()));
  for (size_t i = 0; i < track.getActiveLoop().capture.store.size(); ++i) {
    const auto& event = track.getActiveLoop().capture.store.at(i);
    printf("reboot raw tick=%u pitch=%u on=%u id=%u\n", event.tick,
           event.data.noteData.note, event.isNoteOn(), event.noteId);
  }
  track.stopRecording(stopTick);
  printf("reboot record after_stop jobs=%zu retained=%u capture=%zu length=%u state=%u\n",
         TrackCaptureStopTestAccess::jobs(track), track.hasStopInFlightForSlot(0),
         track.getActiveLoop().capture.store.size(), track.getActiveLoop().loopLengthTicks,
         static_cast<unsigned>(track.getState()));
  unsigned priorPhase = UINT32_MAX;
  for (unsigned turn = 0; turn < 4096 && !track.getActiveLoop().hasCommittedPasses(); ++turn) {
    const auto census = track.stallCensus();
    if (priorPhase != census.stopPhase) {
      auto* runtime = TrackCaptureStopTestAccess::runtime(track);
      printf("reboot record grain=%u pending=%u phase=%u output=%zu min=%u remove=%u\n", turn,
             census.stopPending, census.stopPhase, runtime->canonicalCaptureOutput.store.size(),
             noteMinLengthTicks, noteMinLengthRemoveEnabled);
      for (size_t i = 0; i < runtime->canonicalCaptureOutput.store.size(); ++i) {
        const auto& event = runtime->canonicalCaptureOutput.store.at(i);
        printf("reboot staged tick=%u pitch=%u on=%u id=%u\n", event.tick,
               event.data.noteData.note, event.isNoteOn(), event.noteId);
      }
      priorPhase = census.stopPhase;
    }
    track.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
    track.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  }
  TEST_ASSERT_FALSE(track.hasStopInFlightForSlot(0));
  auto& loop = track.getActiveLoop();
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
  MidiEventVec stored;
  for (auto chunk : loop.passes.recordPass.committedChunkIds)
    LoopEventStore::appendChunkRefEvent(chunk, stored);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, stored.size(), "the replacement Record pass must own both accepted rows");
  TEST_ASSERT_EQUAL_UINT32(12, stored[0].tick);
  TEST_ASSERT_EQUAL_UINT32(80, stored[1].tick);
  TEST_ASSERT_EQUAL_UINT32(id, stored[0].noteId);
  TEST_ASSERT_EQUAL_UINT32(id, stored[1].noteId);
  // The committed pass exists before its root is published. Effective-query readiness is a
  // separate handoff, not evidence that a pass was erased; drive that actual owner before reading it.
  RebootUndoFixture::finishPublication(loop);
  SessionMidiEventVec events;
  loop.copyEffectiveCommittedEvents(events);
  unsigned matched = 0;
  for (const auto& event : events) {
    if (event.noteId != id) continue;
    ++matched;
    TEST_ASSERT_EQUAL_UINT8(84, event.data.noteData.note);
  }
  TEST_ASSERT_EQUAL_UINT32(2, matched);
}
static void test_restored_record_replacement_commits_the_new_take_without_source_maintenance_loss() {
  recordReplacement(false);
}
static void test_restored_truncated_record_replacement_preserves_the_accepted_note() {
  recordReplacement(true);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_saved_passes_first_cycle_survives_source_and_staging_destruction);
  RUN_TEST(test_saved_root_first_cycle_survives_source_and_staging_destruction);
  RUN_TEST(test_saved_slot_undo_replaces_same_window_before_snapshot_completion);
  RUN_TEST(test_complete_rootless_pass_state_refresh_certifies_its_revision);
  RUN_TEST(test_current_empty_window_is_success_but_stale_or_invalid_is_not);
  RUN_TEST(test_same_window_undo_paints_an_authoritative_empty_interval);
  RUN_TEST(test_partial_long_loop_refresh_does_not_certify_unbuilt_content);
  RUN_TEST(test_projection_refusal_preserves_frame_and_key_then_recovers);
  RUN_TEST(test_denied_pass_refresh_does_not_certify_an_empty_projection);
  RUN_TEST(test_wrap_append_refusal_keeps_slice_retryable_and_frame_unqualified);
  RUN_TEST(test_rejected_snapshot_overlay_does_not_certify_fallback_cache);
  RUN_TEST(test_undo_during_resolve_keeps_inflight_frame_then_paints_new_revision);
  RUN_TEST(test_switch_and_clear_cannot_reuse_the_prior_tracks_undo_frame);
  RUN_TEST(test_clock_stop_restart_and_pending_save_do_not_restore_old_painted_notes);
  RUN_TEST(test_restored_record_replacement_commits_the_new_take_without_source_maintenance_loss);
  RUN_TEST(test_restored_truncated_record_replacement_preserves_the_accepted_note);
  RUN_TEST(test_saved_slot_repeated_undo_redo_keeps_playing);
  RUN_TEST(test_saved_root_repeated_undo_redo_keeps_playing);
  RUN_TEST(test_loaded_retiring_source_release_preserves_live_pass_payloads);
  RUN_TEST(test_quiescent_descriptor_reuse_keeps_chunk_ownership_and_updates_index);
  RUN_TEST(test_descriptor_reuse_refuses_capture_finalization_and_retained_gather);
  RUN_TEST(test_descriptor_reuse_refuses_inflight_build_and_ready_or_published_reader);
  RUN_TEST(test_descriptor_reuse_refuses_new_capture_pass_membership);
  RUN_TEST(test_descriptor_reuse_refuses_new_edit_pass_membership);
  RUN_TEST(test_descriptor_reuse_refuses_a_noncurrent_request_identity);
  RUN_TEST(test_descriptor_reuse_refuses_replaced_bodies_with_the_same_pass_ids);
  RUN_TEST(test_fresh_record_and_successor_overdub_keep_their_accepted_rows);
  RUN_TEST(test_scrolled_long_loop_keeps_interval_identity_until_root_handoff);
  RUN_TEST(test_actual_slot_switch_and_body_replacement_reject_prior_frame_identity);
  RUN_TEST(test_paint_does_not_advance_or_reset_an_active_save_item);
  RUN_TEST(test_repeated_undo_redo_repaints_while_snapshot_publication_stays_pending);
  RUN_TEST(test_ordinary_idle_preserves_the_pending_publication_hold);
  RUN_TEST(test_explicit_gap_refresh_refuses_live_session_and_sealed_capture_borrow);
  RUN_TEST(test_redo_projection_refusal_during_gap_recovers_without_publishing_root);
  return UNITY_END();
}
