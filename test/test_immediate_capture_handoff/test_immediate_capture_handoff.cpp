// P1 proof only: default controls characterize the installed behavior, not acceptance.
// Set IMMEDIATE_HANDOFF_CASE=start|stop|source|empty to run desired behavior independently.
// These acceptance assertions intentionally remain RED until the implementing slice lands.
#define PLAYBACK_WINDOW_ROLLING_ENABLED 1
#define main borrowedSharedFinalizationMain
#include "../test_shared_capture_finalization/test_shared_capture_finalization.cpp"
#undef main

namespace {
constexpr uint32_t kBar = Config::TICKS_PER_BAR;
constexpr unsigned kDrainLimit = 20000;

SessionMidiEventVec readExtent(const LoopEventStore::CaptureRange& range) {
  SessionMidiEventVec rows;
  const auto generation = range.generation();
  for (size_t chunk = 0; chunk < range.chunkCount(); ++chunk) {
    for (uint16_t index = 0; index < LoopEventStoreConfig::CHUNK_CAPACITY; ++index) {
      MidiEvent row{};
      bool end = false;
      TEST_ASSERT_TRUE(range.read(generation, chunk, index, row, end));
      if (end) break;
      rows.push_back(row);
    }
  }
  TEST_ASSERT_EQUAL_UINT32(range.size(), rows.size());
  return rows;
}

void assertRows(const SessionMidiEventVec& expected, const SessionMidiEventVec& actual) {
  TEST_ASSERT_EQUAL_UINT32(expected.size(), actual.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    TEST_ASSERT_EQUAL_UINT32(expected[i].tick, actual[i].tick);
    TEST_ASSERT_EQUAL_UINT8(expected[i].type, actual[i].type);
    TEST_ASSERT_EQUAL_UINT8(expected[i].channel, actual[i].channel);
    TEST_ASSERT_EQUAL_UINT32(expected[i].noteId, actual[i].noteId);
    TEST_ASSERT_EQUAL_UINT8(expected[i].data.noteData.note, actual[i].data.noteData.note);
    TEST_ASSERT_EQUAL_UINT8(expected[i].data.noteData.velocity, actual[i].data.noteData.velocity);
  }
}

void report(const char* edge, Track& track) {
  const auto* presentation = TrackCaptureStopTestAccess::presentation(track, 0);
  const auto& loop = track.getActiveLoop();
  printf("P1,%s,state=%u,interval=%llu,jobs=%zu,raw=%zu,rawgen=%llu,producer=%zu,"
         "committed=%u,source_notes=%zu,changes=%zu,undo=%zu,length=%lu\n", edge,
      unsigned(track.getState()),
      static_cast<unsigned long long>(TrackCaptureStopTestAccess::interval(track)),
      TrackCaptureStopTestAccess::jobs(track), presentation ? presentation->rawRange.size() : 0,
      static_cast<unsigned long long>(presentation ? presentation->rawRange.generation() : 0),
      loop.capture.store.size(), unsigned(loop.hasCommittedPasses()),
      loop.overdubSourceViewNotes().size(), loop.pendingNoteChanges().size(),
      track.getGlobalUndoStack().undoCount(), static_cast<unsigned long>(loop.loopLengthTicks));
}

Track& privateRecord() {
  Track& track = preparePrivateRecordForOverdubPress(); // Real clear/start/input/button-stop.
  auto* presentation = TrackCaptureStopTestAccess::presentation(track, 0);
  TEST_ASSERT_NOT_NULL(presentation);
  TEST_ASSERT_TRUE(presentation->rawRange.ready());
  TEST_ASSERT_TRUE(track.getActiveLoop().capture.store.empty());
  TEST_ASSERT_FALSE(track.getActiveLoop().hasCommittedPasses());
  report("A_sealed", track);
  return track;
}

void startDirectB(Track& track) {
  // Actual alternate production entry; bypasses only the separately tested manager retention gate.
  const uint32_t length = track.getActiveLoop().loopLengthTicks;
  const uint32_t now = clockManager.getCurrentTick();
  const uint32_t phase = IntervalProjection::tickPhaseInProjectionCycle(
      now, track.getProjectionCycleStartTick(), length);
  clockManager.assignCurrentTickSilently(now + (length + 48 - phase) % length);
  TEST_ASSERT_EQUAL_UINT32(48, IntervalProjection::tickPhaseInProjectionCycle(
      clockManager.getCurrentTick(), track.getProjectionCycleStartTick(), length));
  track.startOverdubbing(clockManager.getCurrentTick());
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(), "direct live admission must reach B before Stop is tested");
  TEST_ASSERT_EQUAL(CapturePhase::Overdub, track.getActiveLoop().capture.phase);
  TEST_ASSERT_FALSE(track.getActiveLoop().hasCommittedPasses());
  report("B_direct_start", track);
}

void acceptB(Track& track, uint8_t pitch = 72) {
  const uint32_t started = clockManager.getCurrentTick();
  clockManager.assignCurrentTickSilently(started + 16);
  track.noteOn(1, pitch, 91, clockManager.getCurrentTick());
  clockManager.assignCurrentTickSilently(started + 40);
  track.noteOff(1, pitch, 0, clockManager.getCurrentTick());
  TEST_ASSERT_EQUAL_UINT32(2, track.getActiveLoop().capture.store.size());
}


// Tasks 4.7/4.8: the worker is the session source's only writer. Fixtures that need the committed
// source after a generation change drive the same worker the deferred owner drives, then cut the view.
void driveSourceCoverage(Loop& loop, unsigned maxTurns = 4096) {
  for (unsigned turn = 0; turn < maxTurns && loop.overdubCoverageOwedCountForTest() > 0; ++turn) {
    if (loop.serviceOverdubCoverageTurn(CoveragePrerequisite::kGrainEventsPerTurn) ==
        Loop::OverdubCoverageTurn::Idle) {
      break;
    }
  }
}

void drainRecordPublication(Track& track) {
  unsigned turn = 0;
  for (; turn < kDrainLimit && TrackCaptureStopTestAccess::jobs(track) != 0; ++turn)
    TrackCaptureStopTestAccess::step(track);
  report("A_worker_drain", track);
  TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(kDrainLimit, turn, "Record worker drain exhausted");
  TEST_ASSERT_EQUAL_UINT32(0, TrackCaptureStopTestAccess::jobs(track));
}

// Task 4.7 — touched coverage is marked on the **production** capture path: a real overdub session
// accepts a real note and the loop's coverage requires the pressed tile, with nothing prepared (no worker
// exists yet). This is the mark-not-derive contract at the entry point that used to do source work.
void test_riao4_7_live_overdub_marks_touched_coverage() {
  Track& track = privateRecord();
  startDirectB(track);
  Loop& loop = track.getActiveLoop();
  TEST_ASSERT_TRUE_MESSAGE(loop.overdubCoverageSessionActive(), "the session opened its coverage state");

  acceptB(track, 72);

  const uint32_t pressed = loop.playheadPhaseTick == UINT32_MAX ? 0 : loop.playheadPhaseTick;
  for (unsigned turn = 0; turn < 64 && loop.overdubCoverageOwedCountForTest() > 0; ++turn) {
    track.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  }
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, loop.overdubCoverageOwedCountForTest(),
                                  "the deferred owner prepared every owed tile");
  TEST_ASSERT_TRUE_MESSAGE(loop.overdubCoveragePhasePrepared(pressed),
                           "the accepted note's pressed tile is prepared");
  TEST_ASSERT_GREATER_THAN_UINT16_MESSAGE(0, loop.overdubCoveragePreparedCountForTest(),
                                          "a prepared tile is published");
}

void test_control_record_extent_is_immutable_and_live_admission_is_separate() {
  Track& track = privateRecord();
  auto* presentation = TrackCaptureStopTestAccess::presentation(track, 0);
  const auto generation = presentation->rawRange.generation();
  const auto expected = readExtent(presentation->rawRange);
  TEST_ASSERT_EQUAL_UINT32(64, expected.size());
  for (size_t bar = 0; bar < 32; ++bar) {
    TEST_ASSERT_TRUE(expected[2 * bar].isNoteOn());
    TEST_ASSERT_TRUE(expected[2 * bar + 1].isNoteOff());
    TEST_ASSERT_EQUAL_UINT8(60, expected[2 * bar].data.noteData.note);
  }
  startDirectB(track);
  acceptB(track);
  TEST_ASSERT_EQUAL_UINT64(generation, presentation->rawRange.generation());
  assertRows(expected, readExtent(presentation->rawRange));
  TEST_ASSERT_EQUAL_UINT32(1, TrackCaptureStopTestAccess::jobs(track));
  report("control_independent_live", track);
}

void test_control_late_record_publication_preserves_live_successor_rows() {
  Track& track = privateRecord();
  startDirectB(track);
  acceptB(track);
  const auto interval = TrackCaptureStopTestAccess::interval(track);
  SessionMidiEventVec before;
  for (size_t i = 0; i < track.getActiveLoop().capture.store.size(); ++i)
    before.push_back(track.getActiveLoop().capture.store.at(i));
  const auto raw = readExtent(TrackCaptureStopTestAccess::presentation(track, 0)->rawRange);
  const size_t undoBefore = track.getGlobalUndoStack().undoCount();
  drainRecordPublication(track);
  TEST_ASSERT_TRUE(track.isOverdubbing());
  TEST_ASSERT_EQUAL_UINT64(interval, TrackCaptureStopTestAccess::interval(track));
  SessionMidiEventVec after;
  for (size_t i = 0; i < track.getActiveLoop().capture.store.size(); ++i)
    after.push_back(track.getActiveLoop().capture.store.at(i));
  assertRows(before, after);
  TEST_ASSERT_TRUE(track.getActiveLoop().hasCommittedPasses());
  const auto* presentation = TrackCaptureStopTestAccess::presentation(track, 0);
  TEST_ASSERT_NOT_NULL(presentation);
  assertRows(raw, readExtent(presentation->rawRange));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(track, 0));
  TEST_ASSERT_EQUAL_UINT32(undoBefore + 1, track.getGlobalUndoStack().undoCount());
  TEST_ASSERT_EQUAL(UndoEntryKind::RecordPassAdded, track.getGlobalUndoStack().entries.back().kind);
  TEST_ASSERT_TRUE(StorageManagerInternal::currentSetLoopSlotDirty[0][0]);
  const size_t undoAfter = track.getGlobalUndoStack().undoCount();
  for (unsigned i = 0; i < 8; ++i) TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL_UINT32(undoAfter, track.getGlobalUndoStack().undoCount());
  report("control_late_publication", track);
}

void test_immediate_start_accepts_input_before_record_publication() {
  Track& track = privateRecord();
  const auto interval = TrackCaptureStopTestAccess::interval(track);
  midiButtonActions.handleToggleRecord();
  clockManager.assignCurrentTickSilently(32 * kBar + 224);
  track.noteOn(1, 72, 91, clockManager.getCurrentTick());
  report("desired_start_before_publication", track);
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(), "B ingress waits for private Record publication");
  TEST_ASSERT_EQUAL_UINT64(interval + 1, TrackCaptureStopTestAccess::interval(track));
  TEST_ASSERT_EQUAL_UINT32(1, track.getActiveLoop().capture.store.size());
  TEST_ASSERT_FALSE(track.getActiveLoop().hasCommittedPasses());
}

void test_button_starts_later_overdubs_while_record_is_still_private() {
  Track& track = privateRecord();
  const auto A = readExtent(TrackCaptureStopTestAccess::presentation(track, 0)->rawRange);
  for (unsigned pass = 0; pass < 3; ++pass) {
    const auto before = TrackCaptureStopTestAccess::interval(track);
    midiButtonActions.handleToggleRecord();
    TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(),
        "each button Start must admit a live successor while the same Record remains private");
    TEST_ASSERT_EQUAL_UINT64(before + 1, TrackCaptureStopTestAccess::interval(track));
    acceptB(track, static_cast<uint8_t>(72 + pass));
    midiButtonActions.handleToggleRecord();
    TEST_ASSERT_FALSE(track.isOverdubbing());
    TEST_ASSERT_FALSE(track.getActiveLoop().hasCommittedPasses());
    assertRows(A, readExtent(TrackCaptureStopTestAccess::presentation(track, 0)->rawRange));
  }
  TEST_ASSERT_EQUAL_UINT32(4, TrackCaptureStopTestAccess::jobs(track));
}

void test_early_stop_ends_B_without_reusing_A_custody() {
  Track& track = privateRecord();
  const auto A = readExtent(TrackCaptureStopTestAccess::presentation(track, 0)->rawRange);
  const auto aInterval = TrackCaptureStopTestAccess::presentation(track, 0)->interval;
  auto* aReceipt = TrackCaptureStopTestAccess::custody(track, aInterval);
  auto* aWorker = TrackCaptureStopTestAccess::runtimeFor(track, 0).captureStopOwner;
  startDirectB(track);
  acceptB(track);
  const auto B = TrackCaptureStopTestAccess::interval(track);
  clockManager.assignCurrentTickSilently(clockManager.getCurrentTick() + 32);
  const uint32_t stopped = clockManager.getCurrentTick();
  track.stopOverdubbing();
  report("B_stop_requested", track);
  // Deliberately send input after the requested endpoint to discriminate a retained live stop.
  clockManager.assignCurrentTickSilently(stopped + 24);
  track.noteOn(1, 74, 82, clockManager.getCurrentTick());
  report("input_after_B_stop", track);
  assertRows(A, readExtent(TrackCaptureStopTestAccess::presentation(track, 0)->rawRange));
  TEST_ASSERT_FALSE_MESSAGE(track.isOverdubbing(), "B remains live/appends after its requested Stop while A finalizes");
  TEST_ASSERT_EQUAL_UINT64(B + 1, TrackCaptureStopTestAccess::interval(track));
  auto* bReceipt = TrackCaptureStopTestAccess::custody(track, B);
  TEST_ASSERT_NOT_NULL(bReceipt);
  TEST_ASSERT_NOT_EQUAL(aReceipt, bReceipt);
  const auto bRows = readExtent(bReceipt->rawRange);
  TEST_ASSERT_EQUAL_UINT32(2, bRows.size());
  TEST_ASSERT_TRUE(track.getActiveLoop().capture.store.empty());
  TEST_ASSERT_EQUAL_PTR(aWorker, TrackCaptureStopTestAccess::runtimeFor(track, 0).captureStopOwner);
  // C trace becomes reachable only after the preceding custody/Stop assertion passes.
  clockManager.assignCurrentTickSilently(stopped + 40);
  track.startOverdubbing(clockManager.getCurrentTick());
  TEST_ASSERT_TRUE(track.isOverdubbing());
  const auto cInterval = TrackCaptureStopTestAccess::interval(track);
  track.noteOn(1, 76, 88, stopped + 56);
  track.noteOff(1, 76, 0, stopped + 72);
  clockManager.assignCurrentTickSilently(stopped + 88);
  track.stopOverdubbing();
  TEST_ASSERT_FALSE(track.isOverdubbing());
  TEST_ASSERT_EQUAL_UINT64(cInterval + 1, TrackCaptureStopTestAccess::interval(track));
  auto* cReceipt = TrackCaptureStopTestAccess::custody(track, cInterval);
  TEST_ASSERT_NOT_NULL(cReceipt);
  TEST_ASSERT_NOT_EQUAL(aReceipt, cReceipt);
  TEST_ASSERT_NOT_EQUAL(bReceipt, cReceipt);
  const auto cRows = readExtent(cReceipt->rawRange);
  TEST_ASSERT_EQUAL_UINT32(2, cRows.size());
  TEST_ASSERT_EQUAL_UINT8(76, cRows.front().data.noteData.note);
  TEST_ASSERT_EQUAL_UINT32(3, TrackCaptureStopTestAccess::jobs(track));
  TEST_ASSERT_EQUAL_UINT32(3, TrackCaptureStopTestAccess::custodyCount(track));
  auto* bJob = TrackCaptureStopTestAccess::jobFor(track, B);
  auto* cJob = TrackCaptureStopTestAccess::jobFor(track, cInterval);
  TEST_ASSERT_NOT_NULL(bJob); TEST_ASSERT_NOT_NULL(cJob);
  TEST_ASSERT_NOT_EQUAL(bJob->frozenStagingIndex, cJob->frozenStagingIndex);
  assertRows(A, readExtent(aReceipt->rawRange));
  assertRows(bRows, readExtent(bReceipt->rawRange));
  // Real serialized worker turns, not manufactured sealed state or phase transitions.
  unsigned turns = 0;
  for (; turns < kDrainLimit && TrackCaptureStopTestAccess::jobs(track); ++turns) {
    if (!track.getActiveLoop().hasCommittedPasses()) {
      assertRows(A, readExtent(aReceipt->rawRange));
      assertRows(bRows, readExtent(bReceipt->rawRange));
      assertRows(cRows, readExtent(cReceipt->rawRange));
    }
    TrackCaptureStopTestAccess::step(track);
  }
  report("ABC_serial_drain", track);
  printf("PA2,turns=%u,phase=%d\n", turns, TrackCaptureStopTestAccess::headStopPhase(track));
  TEST_ASSERT_LESS_THAN_UINT32(kDrainLimit, turns);
  TEST_ASSERT_EQUAL_UINT32(0, TrackCaptureStopTestAccess::jobs(track));
  const auto& loop = track.getActiveLoop();
  TEST_ASSERT_EQUAL_UINT32(2, loop.passes.overdubPasses.size());
  SessionMidiEventVec bCommitted, cCommitted;
  LoopEventStore::appendChunkRefEvents(loop.passes.overdubPasses[0].committedChunkIds, bCommitted);
  LoopEventStore::appendChunkRefEvents(loop.passes.overdubPasses[1].committedChunkIds, cCommitted);
  assertRows(bRows, bCommitted);
  assertRows(cRows, cCommitted);
  TEST_ASSERT_EQUAL_UINT32(3, track.getGlobalUndoStack().undoCount());
  TEST_ASSERT_TRUE(StorageManagerInternal::currentSetLoopSlotDirty[0][0]);
  const auto undoAfter = track.getGlobalUndoStack().undoCount();
  for (unsigned i = 0; i < 16; ++i) TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL_UINT32(undoAfter, track.getGlobalUndoStack().undoCount());
  TEST_ASSERT_NULL(TrackCaptureStopTestAccess::runtimeFor(track, 0).captureStopOwner);
}

void test_frozen_closure_is_interval_owned_while_a_later_interval_is_live() {
  Track& track = privateRecord();
  startDirectB(track);
  const auto b = TrackCaptureStopTestAccess::interval(track);
  const auto now = clockManager.getCurrentTick();
  track.noteOn(1, 72, 91, now + 12);
  clockManager.assignCurrentTickSilently(now + 64);
  track.stopOverdubbing();
  auto* bReceipt = TrackCaptureStopTestAccess::custody(track, b);
  TEST_ASSERT_NOT_NULL(bReceipt);
  const auto bRaw = readExtent(bReceipt->rawRange);
  TEST_ASSERT_EQUAL_UINT32(1, bRaw.size());
  track.startOverdubbing(now + 80);
  TEST_ASSERT_TRUE(track.isOverdubbing());
  track.noteOn(1, 77, 83, now + 96);
  const auto cInterval = TrackCaptureStopTestAccess::interval(track);
  const auto cRow = track.getActiveLoop().capture.store.at(0);
  unsigned turns = 0;
  for (; turns < kDrainLimit; ++turns) {
    auto* job = TrackCaptureStopTestAccess::jobFor(track, b);
    if (job && job->admission.remainingClosureGrains == 0) break;
    TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_LESS_THAN_UINT32(kDrainLimit, turns);
  TEST_ASSERT_TRUE(track.isOverdubbing());
  TEST_ASSERT_EQUAL_UINT64(cInterval, TrackCaptureStopTestAccess::interval(track));
  TEST_ASSERT_EQUAL_UINT32(1, track.getActiveLoop().capture.store.size());
  TEST_ASSERT_EQUAL_UINT32(cRow.noteId, track.getActiveLoop().capture.store.at(0).noteId);
  assertRows(bRaw, readExtent(bReceipt->rawRange));
  const auto* frozen = TrackCaptureStopTestAccess::runtimeFor(track, 0).captureStagingIfPrewarmed();
  TEST_ASSERT_NOT_NULL(frozen);
  TEST_ASSERT_EQUAL_UINT32(2, frozen->storage.eventCount);
  TEST_ASSERT_TRUE(frozen->storage.events[1].isNoteOff());
  TEST_ASSERT_EQUAL_UINT32(bRaw[0].noteId, frozen->storage.events[1].noteId);
  track.noteOff(1, 77, 0, now + 120);
  clockManager.assignCurrentTickSilently(now + 144);
  track.stopOverdubbing();
  drainRecordPublication(track);
  SessionMidiEventVec committed;
  LoopEventStore::appendChunkRefEvents(track.getActiveLoop().passes.overdubPasses[0].committedChunkIds, committed);
  TEST_ASSERT_EQUAL_UINT32(2, committed.size());
  TEST_ASSERT_TRUE(committed[1].isNoteOff());
  TEST_ASSERT_EQUAL_UINT32(bRaw[0].noteId, committed[1].noteId);
}

void test_nine_real_custodies_cap_and_clear_without_reinterpreting_stale_handles() {
  Track& track = privateRecord();
  auto* a = TrackCaptureStopTestAccess::presentation(track, 0);
  const auto aInterval = a->interval;
  const auto aIndex = a->custodyIndex;
  const auto aRows = readExtent(a->rawRange);
  drainRecordPublication(track); // A is genuinely published, with its reader still outstanding.
  TEST_ASSERT_TRUE(a->retirementActive);
  TEST_ASSERT_NOT_NULL(TrackCaptureStopTestAccess::qualifiedCustody(track, aIndex, aInterval));
  TEST_ASSERT_NULL(TrackCaptureStopTestAccess::qualifiedCustody(track, aIndex, aInterval + (uint64_t{1} << 32)));
  // Fill the admitted envelope without hardcoding which bound binds first: the FIFO holds
  // 8 jobs and the custody slab holds 9 receipts (A retiring + eight successors).
  uint8_t sealed = 0;
  while (sealed < 9 && TrackCaptureStopTestAccess::jobs(track) < 8) {
    const auto tick = clockManager.getCurrentTick() + 96;
    clockManager.assignCurrentTickSilently(tick);
    track.startOverdubbing(tick);
    if (!track.isOverdubbing()) break;
    acceptB(track, static_cast<uint8_t>(72 + sealed));
    clockManager.assignCurrentTickSilently(tick + 64);
    track.stopOverdubbing();
    if (track.isOverdubbing()) break; // a named denial, not a silent seal
    ++sealed;
    TEST_ASSERT_EQUAL_UINT32(sealed + 1, TrackCaptureStopTestAccess::custodyCount(track));
    assertRows(aRows, readExtent(a->rawRange));
  }
  // FIFO (8), custody (9) and the session-exit receipt ring (8) now agree: eight successors seal
  // while A is still retiring, so the envelope is the declared one rather than a hidden ring.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(9, TrackCaptureStopTestAccess::custodyCount(track),
                                   "envelope must reach K=9 (A retiring + eight successors)");
  TEST_ASSERT_EQUAL_UINT32(8, TrackCaptureStopTestAccess::jobs(track));
  const auto tick = clockManager.getCurrentTick() + 96;
  track.startOverdubbing(tick);
  TEST_ASSERT_TRUE(track.isOverdubbing());
  acceptB(track, 79);
  const auto sixth = TrackCaptureStopTestAccess::interval(track);
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::admitIntervalStop(track, 0, sixth));
  TEST_ASSERT_TRUE(track.isOverdubbing());
  TEST_ASSERT_EQUAL_UINT32(2, track.getActiveLoop().capture.store.size());
  TEST_ASSERT_EQUAL_UINT32(9, TrackCaptureStopTestAccess::custodyCount(track));
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::admitIntervalStop(track, 0, sixth - 1));
  track.clear(); // production cancellation, including queued intervals and A's retiring reader
  for (unsigned turn = 0; turn < kDrainLimit &&
       (TrackCaptureStopTestAccess::jobs(track) || TrackCaptureStopTestAccess::custodyCount(track)); ++turn)
    TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL_UINT32(0, TrackCaptureStopTestAccess::jobs(track));
  TEST_ASSERT_EQUAL_UINT32(0, TrackCaptureStopTestAccess::custodyCount(track));
  TEST_ASSERT_NULL(TrackCaptureStopTestAccess::qualifiedCustody(track, aIndex, aInterval));
  TEST_ASSERT_FALSE(track.getActiveLoop().hasCommittedPasses());
}

void test_empty_successor_stop_takes_no_custody_extent() {
  Track& track = privateRecord();
  startDirectB(track);
  const auto b = TrackCaptureStopTestAccess::interval(track);
  track.stopOverdubbing();
  TEST_ASSERT_FALSE(track.isOverdubbing());
  TEST_ASSERT_EQUAL_UINT64(b + 1, TrackCaptureStopTestAccess::interval(track));
  TEST_ASSERT_EQUAL_UINT32(1, TrackCaptureStopTestAccess::custodyCount(track));
  TEST_ASSERT_NULL(TrackCaptureStopTestAccess::custody(track, b));
  TEST_ASSERT_EQUAL_UINT32(2, TrackCaptureStopTestAccess::jobs(track));
  drainRecordPublication(track);
}

// ---------------------------------------------------------------------------------------------
// CONTRACTS.md C3 (P-B). One non-committed source origin, one bounded grain, one deadline.
// ---------------------------------------------------------------------------------------------
struct SourceDecision {
  NoteId noteId = kInvalidNoteId;
  PendingNoteChangeKind kind = PendingNoteChangeKind::Add;
  uint32_t startTick = 0;
  uint32_t endTick = 0;
};
using SourceDecisionVec = std::vector<SourceDecision>;

/// The overlap decisions this session actually holds: the Shorten/Hide rows that become companion
/// edit passes. An `Add` is this session's own note, not a decision. Read from the **live** set --
/// an admitted predecessor job's borrowed view belongs to a different interval, and a two-session
/// fixture must not read one interval's decision as another's.
SourceDecisionVec sourceOverlapDecisions(const Loop& loop) {
  SourceDecisionVec rows;
  for (const PendingNoteChange& change : loop.livePendingNoteChangesForTest()) {
    if (change.kind != PendingNoteChangeKind::Shorten &&
        change.kind != PendingNoteChangeKind::Hide) {
      continue;
    }
    rows.push_back(SourceDecision{change.noteId, change.kind, change.startTick, change.endTick});
  }
  return rows;
}

/// The decision this set holds for one target identity, if any.
bool findDecision(const SourceDecisionVec& rows, NoteId noteId, SourceDecision& out) {
  for (const SourceDecision& row : rows) {
    if (row.noteId != noteId) {
      continue;
    }
    out = row;
    return true;
  }
  return false;
}

/// One target's decision must be the same in both runs: kind and both geometry ends.
void assertSameDecisionFor(const SourceDecisionVec& expected, const SourceDecisionVec& actual,
                           NoteId noteId, const char* what) {
  SourceDecision want{};
  SourceDecision got{};
  TEST_ASSERT_TRUE_MESSAGE(findDecision(expected, noteId, want), what);
  TEST_ASSERT_TRUE_MESSAGE(findDecision(actual, noteId, got), what);
  TEST_ASSERT_EQUAL_MESSAGE(want.kind, got.kind, what);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(want.startTick, got.startTick, what);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(want.endTick, got.endTick, what);
}

/// One production maintenance turn -- the derived-view owner's own lane, exactly as the shared
/// finalization harness drives it. Never a direct call into the cursor's step.
void privateSourceTurn(Track& track) {
  track.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
}

/// B's accepted pair opens at `startDirectB`'s phase 48 + `acceptB`'s +16 offset.
constexpr uint32_t kHoldTick = 64;

/// P-B-4: every grain is checked against the declared quantum while the source is drained.
unsigned drainPrivateSource(Track& track, unsigned turnLimit = 64) {
  unsigned turns = 0;
  // The Loop is re-resolved every turn: the fixture's pool can be re-built between fixtures, and a
  // cached reference would read a retired object.
  while (turns < turnLimit && track.getActiveLoop().privatePredecessorSourcePending()) {
    const size_t rowsBefore = track.getActiveLoop().privateSourceStagedRows();
    const size_t notesBefore = track.getActiveLoop().privateSourceStagedNotes();
    privateSourceTurn(track);
    const Loop& loop = track.getActiveLoop();
    TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(Loop::kPrivateSourceQuantum,
                                             loop.privateSourceLastUnits(),
                                             "one grain admitted more than kPrivateSourceQuantum units");
    TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(
        Loop::kPrivateSourceQuantum, loop.privateSourceStagedRows() - rowsBefore,
        "one grain read more sealed rows than kPrivateSourceQuantum units");
    // Per-grain touch bound: a grain can touch at most one note per row it read, and never more than
    // the fixed open-pair table (review finding 3).
    TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(
        loop.privateSourceStagedRows() - rowsBefore, loop.privateSourceStagedNotes() - notesBefore,
        "a grain touched more notes than the rows it read");
    TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(
        Loop::kPrivateSourceOpenPairCapacity, loop.privateSourceStagedNotes() - notesBefore,
        "a grain touched more pairs than the fixed open-pair table");
    ++turns;
  }
  return turns;
}

/// Drive the deferred hold materialization to completion. After a supersession the cursor is unbound,
/// so `drainPrivateSource` would stop immediately while intents are still owed: this keeps asking the
/// production lane until the Loop says it owes nothing.
unsigned drainPrivateHolds(Track& track, unsigned turnLimit = 32) {
  unsigned turns = 0;
  while (turns < turnLimit && track.getActiveLoop().privateSourceDeferredHolds() > 0) {
    privateSourceTurn(track);
    ++turns;
  }
  return turns;
}

/// The same, but honest about the lane: after a predecessor publishes, its frozen-reader presentation
/// owns every deferred turn until it retires (`serviceCaptureStopPresentations` returns before the
/// derived-view branch). Production has those turns; this drives the production retirement driver and
/// then the derived-view lane, so a retained hold is materialized as soon as it is reachable.
unsigned drainPresentationThenHolds(Track& track, unsigned turnLimit = 4000) {
  unsigned turns = 0;
  while (turns < turnLimit &&
         (TrackCaptureStopTestAccess::recordWorkPending(track) ||
          track.getActiveLoop().privateSourceDeferredHolds() > 0)) {
    if (TrackCaptureStopTestAccess::recordWorkPending(track)) {
      (void)TrackCaptureStopTestAccess::retirementGrain(track);
    }
    privateSourceTurn(track);
    ++turns;
  }
  return turns;
}

/// The decisions the interval currently carries as *seen by the transfer* (the borrowed job set while
/// a job owns it, the live set otherwise).
SourceDecisionVec visibleOverlapDecisions(const Loop& loop) {
  SourceDecisionVec rows;
  for (const PendingNoteChange& change : loop.pendingNoteChanges()) {
    if (change.kind != PendingNoteChangeKind::Shorten &&
        change.kind != PendingNoteChangeKind::Hide) {
      continue;
    }
    rows.push_back(SourceDecision{change.noteId, change.kind, change.startTick, change.endTick});
  }
  return rows;
}

/// The companion edit pass `targetNoteId` was sealed with, if any: the transform the session decided
/// on has to reach the pass set, not just the pending set.
bool findCompanionFor(const Loop& loop, NoteId targetNoteId, uint32_t expectedEnd,
                      PendingNoteChangeKind* kindOut = nullptr) {
  for (const EditPass& row : loop.passes.editPasses) {
    if (row.state != EditPassState::Active || row.passType != EditPassType::Note ||
        row.targetNoteId != targetNoteId) {
      continue;
    }
    if (row.propertyType != EditPropertyType::NoteRange &&
        row.propertyType != EditPropertyType::Length) {
      continue;
    }
    if (row.actionType != EditActionType::Update) {
      continue;
    }
    if (row.endTick != expectedEnd) {
      continue;
    }
    if (kindOut != nullptr) *kindOut = PendingNoteChangeKind::Shorten;
    return true;
  }
  return false;
}

/// The same private-hold query the note-on path performs, at the tick `acceptB` opens its pair on.
void collectHoldParticipants(Track& track, uint8_t pitch, OverlapNoteIdSet& out) {
  track.getActiveLoop().collectOverdubSourceHoldParticipantIds(kHoldTick, pitch, out);
}
/// Captured by P-B-1 and compared against the sequential canonical run in the control below. The two
/// sessions share the production entry points, so the comparison is a differential over the same
/// pitch set rather than a second copy of the resolver. Each fixture needs its own test function
/// because a private Record predecessor owns its Track's deferred lane until it retires.
struct PrivateOracleExpectation {
  bool valid = false;
  NoteId noteId = kInvalidNoteId;
  SourceDecisionVec decisions;
  OverlapNoteIdSet participants;
};
PrivateOracleExpectation gPrivateOracle;

struct SourceSessionResult {
  SourceDecisionVec decisions;
  OverlapNoteIdSet participants;
  NoteId predecessorNoteId = kInvalidNoteId;
  Loop::SourceOrigin origin = Loop::SourceOrigin::None;
  uint32_t publishedRevision = 0;
  const void* publishedIdentity = nullptr;
};


/// Settle the fixture so the next run starts from the same place this one did: end the live session
/// and retire the interval that was sealed. Without it a second `preparePrivateRecordForOverdubPress`
/// in the same test would start while the previous interval still owns its deferred turn.
void settleHandoffFixture(Track& track) {
  if (track.isOverdubbing()) {
    clockManager.assignCurrentTickSilently(clockManager.getCurrentTick() + 32);
    track.stopOverdubbing();
  }
  drainRecordPublication(track);
}

SourceSessionResult runSamePitchOverlapSession(bool publishPredecessorFirst, uint8_t pitch) {
  SourceSessionResult result;
  Track& track = privateRecord();
  Loop& loop = track.getActiveLoop();
  result.predecessorNoteId =
      readExtent(TrackCaptureStopTestAccess::presentation(track, 0)->rawRange).front().noteId;
  startDirectB(track);
  if (publishPredecessorFirst) {
    drainRecordPublication(track);
    loop.rebuildOverdubSourceView(48, "native_oracle_canonical");
  }
  acceptB(track, pitch);
  drainPrivateSource(track);
  collectHoldParticipants(track, pitch, result.participants);
  result.decisions = sourceOverlapDecisions(loop);
  result.origin = loop.overdubSourceOrigin();
  result.publishedRevision = loop.overdubSourceSpanCachePublishedRevisionForTest();
  result.publishedIdentity = loop.overdubSourceSpanCachePublishedIdentityForTest();
  settleHandoffFixture(track);
  return result;
}
// P-B-1 / C3.6: the RED this packet exists for. A is a sealed private Record extent that has not
// published; B overdubs the same pitch over it. The participant set must contain A's accepted
// NoteId, the resulting overlap decision must equal the sequential canonical run's decision (the
// control below, which runs this same session after A publishes), and the private origin must stay
// visibly non-committed.
void test_private_record_is_the_same_pitch_overlap_source() {
  Track& track = privateRecord();
  const auto A = readExtent(TrackCaptureStopTestAccess::presentation(track, 0)->rawRange);
  TEST_ASSERT_TRUE(A.front().isNoteOn());
  TEST_ASSERT_EQUAL_UINT8(60, A.front().data.noteData.note);
  TEST_ASSERT_EQUAL_UINT32(12, A[0].tick);
  TEST_ASSERT_EQUAL_UINT32(96, A[1].tick);
  const NoteId expected = A.front().noteId;
  TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, expected);
  startDirectB(track); // phase48 lies strictly inside A's accepted12..96 pair.
  TEST_ASSERT_TRUE_MESSAGE(track.getActiveLoop().privatePredecessorSourcePending(),
                           "the session must qualify A's private source at the overdub start edge");
  acceptB(track, 60);  // The same pitch, inside A's hold: this is the decision under test.
  const unsigned turns = drainPrivateSource(track);
  TEST_ASSERT_GREATER_THAN_UINT32(0, turns);
  Loop& loop = track.getActiveLoop();
  report("source_hold_phase48", track);
  OverlapNoteIdSet participants;
  collectHoldParticipants(track, 60, participants);
  const SourceDecisionVec privateDecisions = sourceOverlapDecisions(loop);
  printf("P1,source_expected,id=%lu,start=12,end=96,pitch=60,participants=%zu,decisions=%zu,"
         "origin=%s,turns=%u,holds=%zu,step=%u,staged=%zu,notes=%zu\n",
         static_cast<unsigned long>(expected), participants.size(), privateDecisions.size(),
         Loop::sourceOriginName(loop.overdubSourceOrigin()), turns,
         loop.privateSourceDeferredHolds(), static_cast<unsigned>(loop.privateSourceLastStep()),
         loop.privateSourceStagedRows(), loop.privateSourceStagedNotes());
  TEST_ASSERT_TRUE_MESSAGE(participants.contains(expected),
      "private predecessor A is absent from B's same-pitch hold source");
  TEST_ASSERT_FALSE(track.getActiveLoop().hasCommittedPasses());
  // C3.1: the origin is the private one, and it never stamps committed attribution.
  TEST_ASSERT_EQUAL_MESSAGE(Loop::SourceOrigin::PrivatePredecessor, loop.overdubSourceOrigin(),
                            "the private extent must enter as its own qualified origin");
  TEST_ASSERT_EQUAL_UINT32(0, loop.overdubSourceSpanCachePublishedRevisionForTest());
  TEST_ASSERT_NULL(loop.overdubSourceSpanCachePublishedIdentityForTest());
  // The decision. Its expected shape for A's accepted pair is asserted here; the whole set is
  // compared against the independent sequential canonical run by the control below.
  SourceDecision target{};
  bool foundTarget = false;
  for (const SourceDecision& decision : privateDecisions) {
    if (decision.noteId != expected) {
      continue;
    }
    target = decision;
    foundTarget = true;
  }
  TEST_ASSERT_TRUE_MESSAGE(foundTarget, "the private hold must decide A's accepted pair");
  TEST_ASSERT_EQUAL_UINT32(12, target.startTick);
  TEST_ASSERT_EQUAL_UINT32(kHoldTick - 1, target.endTick);
  gPrivateOracle.valid = true;
  gPrivateOracle.noteId = expected;
  gPrivateOracle.decisions = privateDecisions;
  gPrivateOracle.participants = participants;
}
// ---------------------------------------------------------------------------------------------
// P-B-2: two sessions over one private predecessor. B and C accept distinct same-pitch pairs at
// distinct holds; neither decision may leak into the other interval, and both must equal the
// independent sequential canonical run over the same pitch set.
// ---------------------------------------------------------------------------------------------
void acceptPairAtPhase(Track& track, uint8_t pitch, uint32_t onPhase, uint32_t offPhase) {
  Loop& loop = track.getActiveLoop();
  const uint32_t length = loop.loopLengthTicks;
  const uint32_t now = clockManager.getCurrentTick();
  const uint32_t phase = IntervalProjection::tickPhaseInProjectionCycle(
      now, track.getProjectionCycleStartTick(), length);
  clockManager.assignCurrentTickSilently(now + (length + onPhase - phase) % length);
  TEST_ASSERT_EQUAL_UINT32(onPhase, IntervalProjection::tickPhaseInProjectionCycle(
      clockManager.getCurrentTick(), track.getProjectionCycleStartTick(), length));
  const uint32_t started = clockManager.getCurrentTick();
  track.noteOn(1, pitch, 91, started);
  clockManager.assignCurrentTickSilently(started + (offPhase - onPhase));
  track.noteOff(1, pitch, 0, clockManager.getCurrentTick());
  TEST_ASSERT_EQUAL_UINT32(2, track.getActiveLoop().capture.store.size());
}

struct TwoSessionResult {
  SourceDecisionVec b;
  SourceDecisionVec c;
  size_t bStagedRows = 0;
  size_t cStagedRowsAtOpen = 0;
  size_t cStagedRows = 0;
  size_t cStagedNotes = 0;
  Loop::SourceOrigin cOrigin = Loop::SourceOrigin::None;
};

TwoSessionResult runTwoSessionDifferential(bool publishPredecessorFirst) {
  TwoSessionResult result;
  Track& track = privateRecord();
  Loop& loop = track.getActiveLoop();
  startDirectB(track);
  if (publishPredecessorFirst) {
    drainRecordPublication(track);
    loop.rebuildOverdubSourceView(48, "native_oracle_two_session");
  }
  acceptPairAtPhase(track, 60, kHoldTick, kHoldTick + 24);  // inside A's first pair (12..96)
  drainPrivateSource(track);
  result.b = sourceOverlapDecisions(loop);
  result.bStagedRows = loop.privateSourceStagedRows();
  clockManager.assignCurrentTickSilently(clockManager.getCurrentTick() + 64);
  track.stopOverdubbing();
  TEST_ASSERT_FALSE(track.isOverdubbing());
  startDirectB(track);
  result.cStagedRowsAtOpen = loop.privateSourceStagedRows();
  acceptPairAtPhase(track, 60, 812, 852);  // inside A's second pair (780..864), a distinct hold
  drainPrivateSource(track);
  result.c = sourceOverlapDecisions(loop);
  result.cStagedRows = loop.privateSourceStagedRows();
  result.cStagedNotes = loop.privateSourceStagedNotes();
  result.cOrigin = loop.overdubSourceOrigin();
  settleHandoffFixture(track);
  return result;
}

void test_two_sessions_over_one_private_predecessor_do_not_leak_intervals() {
  // B's session is the same one P-B-1 ran, so its decision must be the same decision; that is the
  // differential, and the canonical control re-derives the same expectation after publication.
  TEST_ASSERT_TRUE_MESSAGE(gPrivateOracle.valid, "P-B-1 must run before this differential");
  const TwoSessionResult privateRun = runTwoSessionDifferential(false);
  assertSameDecisionFor(gPrivateOracle.decisions, privateRun.b, gPrivateOracle.noteId,
                        "B's decision must equal the same-pitch session's decision");
  TEST_ASSERT_EQUAL_UINT32(12, privateRun.b[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(kHoldTick - 1, privateRun.b[0].endTick);
  // A new session starts from a cancelled cursor: no row of B's prefix survives into C.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, privateRun.cStagedRowsAtOpen,
                                   "a new session must not inherit the previous interval prefix");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(privateRun.bStagedRows, privateRun.cStagedRows,
                                   "C must re-read the predecessor, not append to B's prefix");
  TEST_ASSERT_EQUAL_UINT32(privateRun.bStagedRows / 2, privateRun.cStagedNotes);
  // C's decisions are its own hold's: the A pair present under C's hold, shortened to the hold.
  // B's were shortened at B's hold, so no row may carry B's end tick.
  // The clean 32-bar predecessor has exactly one pair under C's hold: A's bar-1 note (780..864),
  // shortened to 811. The previous 31-bar trim wrapped the 32nd bar's orphan NoteOn into tick 12 and
  // surfaced it here as a phantom [12..811] pair; that pair is the corruption this change removes, not an
  // expectation to preserve.
  {
    char detail[500];
    size_t used = 0;
    used += static_cast<size_t>(std::snprintf(detail + used, sizeof(detail) - used, "c=%zu",
                                              privateRun.c.size()));
    for (const SourceDecision& decision : privateRun.c) {
      if (used >= sizeof(detail)) break;
      used += static_cast<size_t>(std::snprintf(detail + used, sizeof(detail) - used,
          " [%u..%u kind=%u]", decision.startTick, decision.endTick,
          static_cast<unsigned>(decision.kind)));
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, privateRun.c.size(), detail);
  }
  TEST_ASSERT_EQUAL_UINT32(780, privateRun.c[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(811, privateRun.c[0].endTick);
  bool foundSecondPair = false;
  for (const SourceDecision& decision : privateRun.c) {
    TEST_ASSERT_EQUAL_MESSAGE(PendingNoteChangeKind::Shorten, decision.kind,
                              "C's decisions must be the hold's Shorten rows");
    TEST_ASSERT_TRUE_MESSAGE(decision.endTick != (kHoldTick - 1),
                             "B's decision geometry must not leak into C's interval");
    if (decision.startTick == 780 && decision.endTick == 811) foundSecondPair = true;
  }
  TEST_ASSERT_TRUE_MESSAGE(foundSecondPair, "C's own hold must shorten A's second pair");
  TEST_ASSERT_EQUAL(Loop::SourceOrigin::PrivatePredecessor, privateRun.cOrigin);
  printf("P1,PB2,bStaged=%zu,cStagedAtOpen=%zu,cStaged=%zu,cNotes=%zu,bDec=%zu,cDec=%zu\n",
         privateRun.bStagedRows, privateRun.cStagedRowsAtOpen, privateRun.cStagedRows,
         privateRun.cStagedNotes, privateRun.b.size(), privateRun.c.size());
}
// ---------------------------------------------------------------------------------------------
// P-B-3: the predecessor publishes while the cursor is mid-scan. The private partial is discarded
// by identity, the committed origin answers, and no private prefix is completed by a committed
// suffix.
// ---------------------------------------------------------------------------------------------
void test_predecessor_publication_mid_scan_discards_the_private_prefix() {
  Track& track = privateRecord();
  // The Loop is re-resolved at each step: the fixture's pool can be rebuilt under a longer trace.
  const NoteId aId =
      readExtent(TrackCaptureStopTestAccess::presentation(track, 0)->rawRange).front().noteId;
  startDirectB(track);
  // A hold arrives while the private source is still being built: its decision is deferred, not lost.
  acceptB(track, 60);
  TEST_ASSERT_EQUAL_UINT32(1, track.getActiveLoop().privateSourceDeferredHolds());
  privateSourceTurn(track);  // exactly one grain: a partial prefix, never a complete source
  TEST_ASSERT_TRUE(track.getActiveLoop().privatePredecessorSourcePending());
  const size_t partialRows = track.getActiveLoop().privateSourceStagedRows();
  TEST_ASSERT_GREATER_THAN_UINT32(0, partialRows);
  TEST_ASSERT_LESS_THAN_UINT32(64, partialRows);
  TEST_ASSERT_EQUAL(Loop::SourceOrigin::PrivatePredecessor,
                    track.getActiveLoop().overdubSourceOrigin());
  // RIAO-3.5 (scan ownership): while the private scan owns the cache the worker yields, so no committed
  // tile can be published over the private prefix and the tile stays owed.
  {
    Loop& ownerLoop = track.getActiveLoop();
    const size_t cacheBeforeWorker = ownerLoop.overdubSourceSpanCacheNotesForTest().size();
    const uint8_t owedBeforeWorker = ownerLoop.overdubCoverageOwedCountForTest();
    TEST_ASSERT_EQUAL(Loop::OverdubCoverageTurn::Idle,
                      ownerLoop.serviceOverdubCoverageTurn(
                          CoveragePrerequisite::kGrainEventsPerTurn));
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(cacheBeforeWorker),
                             static_cast<uint32_t>(
                                 ownerLoop.overdubSourceSpanCacheNotesForTest().size()));
    TEST_ASSERT_EQUAL_UINT8(owedBeforeWorker, ownerLoop.overdubCoverageOwedCountForTest());
  }
  // Publish A. The Snapshot owner's turn advances the record job and nothing else, so the scan stays
  // exactly where that one grain left it until publication supersedes it.
  unsigned turns = 0;
  for (; turns < kDrainLimit && TrackCaptureStopTestAccess::jobs(track) != 0; ++turns)
    track.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
  TEST_ASSERT_LESS_THAN_UINT32(kDrainLimit, turns);
  TEST_ASSERT_EQUAL_UINT32(0, TrackCaptureStopTestAccess::jobs(track));
  TEST_ASSERT_TRUE(track.getActiveLoop().hasCommittedPasses());
  TEST_ASSERT_FALSE_MESSAGE(track.getActiveLoop().privatePredecessorSourcePending(),
                            "the predecessor publication must supersede the private cursor");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, track.getActiveLoop().privateSourceStagedRows(),
                                   "the private partial must be discarded, not continued");
  TEST_ASSERT_NOT_EQUAL(Loop::SourceOrigin::PrivatePredecessor,
                        track.getActiveLoop().overdubSourceOrigin());
  driveSourceCoverage(track.getActiveLoop());
  track.getActiveLoop().rebuildOverdubSourceView(48, "native_after_publication");
  TEST_ASSERT_NOT_EQUAL_MESSAGE(Loop::SourceOrigin::PrivatePredecessor,
                                track.getActiveLoop().overdubSourceOrigin(),
                                "a committed revision must not be served from the private origin");
  OverlapNoteIdSet participants;
  collectHoldParticipants(track, 60, participants);
  TEST_ASSERT_TRUE_MESSAGE(participants.contains(aId),
                           "the committed origin must answer for the published predecessor");
  // Review findings 1 and 2: a retained hold is owed at the seal even though the scan is gone, and it
  // is never materialized from a pre-publication derivation. While B stays live its own session owns
  // the derived-view lane, so the reachable progress here is bounded: give the production lane turns,
  // then require that whatever decisions exist are the post-publication ones.
  TEST_ASSERT_TRUE_MESSAGE(track.getActiveLoop().privateSourceOwedForSeal(),
                           "a retained hold is owed at the seal even when the scan is superseded");
  const unsigned holdTurns = drainPresentationThenHolds(track, 64);
  TEST_ASSERT_EQUAL_UINT32(64, holdTurns);
  const SourceDecisionVec afterPublication = sourceOverlapDecisions(track.getActiveLoop());
  if (gPrivateOracle.valid) {
    for (const SourceDecision& decision : afterPublication) {
      assertSameDecisionFor(gPrivateOracle.decisions, afterPublication, decision.noteId,
                            "every deferred decision must equal the post-publication canonical one");
    }
  }
  // The deadline proper: B stops. The transform must reach the pass set, or the stop admission must
  // have materialized it into the interval's own pending set, or the seal must have named the denial
  // and kept the intent -- never a silent stop without A's Shorten/Hide.
  clockManager.assignCurrentTickSilently(clockManager.getCurrentTick() + 32);
  track.stopOverdubbing();
  const SourceDecisionVec atAdmission = visibleOverlapDecisions(track.getActiveLoop());
  const bool materializedAtAdmission = !atAdmission.empty();
  if (gPrivateOracle.valid && materializedAtAdmission) {
    assertSameDecisionFor(gPrivateOracle.decisions, atAdmission, aId,
                          "the admission deadline must decide against the committed origin");
  }
  drainRecordPublication(track);
  const bool companion = findCompanionFor(track.getActiveLoop(), aId, kHoldTick - 1);
  const bool sealDenied = track.getActiveLoop().privateSourceSealDenied();
  const size_t holdsAfterSeal = track.getActiveLoop().privateSourceDeferredHolds();
  printf("P1,PB3,partialRows=%zu,turns=%u,origin=%s,participants=%zu,holdTurns=%u,"
         "decisions=%zu,companion=%u,sealDenied=%u,holds=%zu\n",
         partialRows, turns, Loop::sourceOriginName(track.getActiveLoop().overdubSourceOrigin()),
         participants.size(), holdTurns, afterPublication.size(), unsigned(companion),
         unsigned(sealDenied), holdsAfterSeal);
  printf("P1,PB3b,atAdmission=%zu\n", atAdmission.size());
  TEST_ASSERT_TRUE_MESSAGE(companion || materializedAtAdmission ||
                               (sealDenied && holdsAfterSeal > 0),
                           "a retained hold must reach the pass set, the interval's own pending "
                           "set, or a named denial");
}

// ---------------------------------------------------------------------------------------------
// P-B-4: bounded units. A dense predecessor cannot be prepared in one grain, and no grain admits
// more than kDeferredStopPreparationQuantum (32) units.
// ---------------------------------------------------------------------------------------------
void test_private_source_grain_is_bounded_by_the_declared_quantum() {
  Track& track = privateRecord();
  Loop& loop = track.getActiveLoop();
  startDirectB(track);
  unsigned turns = 0;
  size_t maxUnits = 0;
  size_t maxRows = 0;
  size_t previousRows = 0;
  while (turns < 64 && loop.privatePredecessorSourcePending()) {
    privateSourceTurn(track);
    const size_t units = loop.privateSourceLastUnits();
    const size_t rows = loop.privateSourceStagedRows() - previousRows;
    previousRows = loop.privateSourceStagedRows();
    if (units > maxUnits) maxUnits = units;
    if (rows > maxRows) maxRows = rows;
    TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(Loop::kPrivateSourceQuantum, units,
                                             "a grain admitted more units than the quantum");
    ++turns;
  }
  TEST_ASSERT_FALSE(loop.privatePredecessorSourcePending());
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(64, turns);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(1, turns,
                                          "64 sealed rows cannot be staged inside one 32-unit grain");
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(Loop::kPrivateSourceQuantum, maxUnits);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(Loop::kPrivateSourceQuantum, maxRows);
  TEST_ASSERT_EQUAL_UINT32(64, loop.privateSourceStagedRows());
  TEST_ASSERT_EQUAL_UINT32(32, loop.privateSourceStagedNotes());
  printf("P1,PB4,turns=%u,maxUnits=%zu,maxRows=%zu,rows=%zu,notes=%zu\n", turns, maxUnits, maxRows,
         loop.privateSourceStagedRows(), loop.privateSourceStagedNotes());
}

// ---------------------------------------------------------------------------------------------
// P-B-5: pressure. The staging increment is refused, so the row is not consumed, the position is
// exact and no partial transform is applied. Releasing the pressure resumes the same cursor.
// ---------------------------------------------------------------------------------------------
void test_private_source_pressure_leaves_the_row_unconsumed() {
  Track& track = privateRecord();
  Loop& loop = track.getActiveLoop();
  startDirectB(track);
  acceptB(track, 60);
  TEST_ASSERT_EQUAL_UINT32(0, loop.privateSourceStagedRows());
  loop.resetOverdubSourceSpanCacheCapacityForTest();
  MemoryMonitor::setNativeForcedReserveDenied(true);
  privateSourceTurn(track);
  MemoryMonitor::setNativeForcedReserveDenied(false);
  TEST_ASSERT_EQUAL_MESSAGE(Loop::PrivateSourceStep::Pressure, loop.privateSourceLastStep(),
                            "a refused staging increment must be a retryable Pressure");
  TEST_ASSERT_EQUAL_MESSAGE(Loop::PrivateSourceDeny::OutputUnavailable, loop.privateSourceDeny(),
                            "the denial must be named");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, loop.privateSourceStagedRows(),
                                   "the refused row must stay unconsumed");
  TEST_ASSERT_EQUAL_UINT32(0, loop.privateSourceLastUnits());
  TEST_ASSERT_TRUE(loop.privatePredecessorSourcePending());
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, sourceOverlapDecisions(loop).size(),
                                   "no partial transform may be applied under pressure");
  // The intent is retained and the same cursor resumes: every A note present at the hold is decided
  // exactly once, with the hold's own geometry.
  drainPrivateSource(track);
  TEST_ASSERT_EQUAL_UINT32(64, loop.privateSourceStagedRows());
  const SourceDecisionVec decisions = sourceOverlapDecisions(loop);
  TEST_ASSERT_TRUE(!decisions.empty());
  for (const SourceDecision& decision : decisions) {
    TEST_ASSERT_EQUAL_UINT32(kHoldTick - 1, decision.endTick);
  }
}
// ---------------------------------------------------------------------------------------------
// Review finding 3: a pair decision is one unit of fixed cost, and every view append is charged.
// ---------------------------------------------------------------------------------------------
void test_private_source_pair_decisions_and_view_growth_are_bounded() {
  Track& track = privateRecord();
  startDirectB(track);
  privateSourceTurn(track);  // one grain: the prefix has rows and both stores have grown once
  const size_t rowsAfterFirst = track.getActiveLoop().privateSourceStagedRows();
  const size_t notesAfterFirst = track.getActiveLoop().privateSourceStagedNotes();
  TEST_ASSERT_GREATER_THAN_UINT32(0, rowsAfterFirst);
  // Per-grain touch bound: a grain can stage at most one note per row it read, and never more than
  // the fixed open-pair table.
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(rowsAfterFirst, notesAfterFirst);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(16, notesAfterFirst);
  // The view's own staging increment is charged: with its capacity released and the reserve refused,
  // the grain must refuse (`Pressure`, named) rather than grow, and it must not consume the row.
  track.getActiveLoop().resetOverdubSourceViewCapacityForTest();
  const size_t viewBefore = track.getActiveLoop().overdubSourceViewNotes().size();
  const size_t rowsBefore = track.getActiveLoop().privateSourceStagedRows();
  MemoryMonitor::setNativeForcedReserveDenied(true);
  privateSourceTurn(track);
  MemoryMonitor::setNativeForcedReserveDenied(false);
  TEST_ASSERT_EQUAL_MESSAGE(Loop::PrivateSourceStep::Pressure,
                            track.getActiveLoop().privateSourceLastStep(),
                            "a refused view increment must be a retryable Pressure");
  TEST_ASSERT_EQUAL_MESSAGE(Loop::PrivateSourceDeny::OutputUnavailable,
                            track.getActiveLoop().privateSourceDeny(),
                            "the view refusal must be named");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(rowsBefore, track.getActiveLoop().privateSourceStagedRows(),
                                   "a refused view slot must leave the row unconsumed");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(viewBefore,
                                   track.getActiveLoop().overdubSourceViewNotes().size(),
                                   "a refused view slot must not grow the view");
  TEST_ASSERT_EQUAL_UINT32(0, track.getActiveLoop().privateSourceLastUnits());
  // Releasing the pressure resumes the same position and completes the extent.
  drainPrivateSource(track);
  TEST_ASSERT_EQUAL_UINT32(64, track.getActiveLoop().privateSourceStagedRows());
  TEST_ASSERT_EQUAL_UINT32(32, track.getActiveLoop().privateSourceStagedNotes());
}

// ---------------------------------------------------------------------------------------------
// Review finding 4: a committed revision move that does NOT notify still supersedes the scan.
// `publishPreparedCompanionHistory` was exactly that shape; the cursor may only trust the revision
// it was qualified with, so ANY move is an identity change.
// ---------------------------------------------------------------------------------------------
void test_a_revision_move_without_a_notify_supersedes_the_scan() {
  Track& track = privateRecord();
  startDirectB(track);
  privateSourceTurn(track);
  TEST_ASSERT_TRUE(track.getActiveLoop().privatePredecessorSourcePending());
  TEST_ASSERT_GREATER_THAN_UINT32(0, track.getActiveLoop().privateSourceStagedRows());
  // The class of bump the finding names: a committed revision move that bypassed the notification.
  track.getActiveLoop().bumpPlaybackRevisionWithoutNotifyForTest();
  // The publication-shaped presentation of the same edge: no private source is owed any more.
  track.getActiveLoop().stepPrivatePredecessorSource(Loop::PrivatePredecessorBinding{});
  TEST_ASSERT_EQUAL_MESSAGE(Loop::PrivateSourceDeny::IdentityChanged,
                            track.getActiveLoop().privateSourceDeny(),
                            "a revision move must be reported as an identity change");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, track.getActiveLoop().privateSourceStagedRows(),
                                   "a superseded prefix must never be continued");
  TEST_ASSERT_FALSE(track.getActiveLoop().privatePredecessorSourcePending());
  TEST_ASSERT_NOT_EQUAL(Loop::SourceOrigin::PrivatePredecessor,
                        track.getActiveLoop().overdubSourceOrigin());
}

// ---------------------------------------------------------------------------------------------
// Review finding 5: a hold that cannot be retained is a visible, named denial -- never a silent drop.
// ---------------------------------------------------------------------------------------------
void test_five_holds_are_bounded_by_the_capacity_and_denied_by_name() {
  Track& track = privateRecord();
  const NoteId aId =
      readExtent(TrackCaptureStopTestAccess::presentation(track, 0)->rawRange).front().noteId;
  startDirectB(track);
  // Five distinct holds through the production occupancy entry the note-on path calls, all inside A's
  // first pair (12..96). The capture store is not used here: this case is about the *retention* bound,
  // and the fixture's staging budget is shared with the other cases in this process.
  for (uint32_t i = 0; i < 5; ++i) {
    OverlapNoteIdSet noLedgerCandidates;
    track.getActiveLoop().resolveOccupancyAtNoteOn(1, 60, 91, noLedgerCandidates, 64 + i * 6,
                                                   nullptr);
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(Loop::kPrivateSourceHoldCapacity,
                                   track.getActiveLoop().privateSourceDeferredHolds(),
                                   "the retained intents are bounded by the declared capacity");
  TEST_ASSERT_TRUE_MESSAGE(track.getActiveLoop().privateSourceSealDenied(),
                           "the hold that could not be retained must be a sticky, named denial");
  TEST_ASSERT_EQUAL_MESSAGE(Loop::PrivateSourceDeny::HoldCapacity,
                            track.getActiveLoop().privateSourceDeny(),
                            "the denial names the capacity");
  // The retained intents still materialize exactly once each against the extent.
  drainPrivateSource(track);
  drainPrivateHolds(track);
  TEST_ASSERT_EQUAL_UINT32(0, track.getActiveLoop().privateSourceDeferredHolds());
  const SourceDecisionVec decisions = sourceOverlapDecisions(track.getActiveLoop());
  TEST_ASSERT_TRUE(!decisions.empty());
  for (const SourceDecision& decision : decisions) {
    TEST_ASSERT_TRUE(decision.noteId == aId || decision.startTick >= 12);
    TEST_ASSERT_TRUE(decision.endTick < 96);
  }
  TEST_ASSERT_EQUAL_UINT32(kHoldTick - 1, decisions[0].endTick);  // the strictest retained hold
}

// ---------------------------------------------------------------------------------------------
// Review finding 6: a cursor bound to a previous Loop identity is cancelled, not left pending.
// ---------------------------------------------------------------------------------------------
void test_a_loop_identity_mismatch_cancels_the_scan() {
  Track& track = privateRecord();
  auto* presentation = TrackCaptureStopTestAccess::presentation(track, 0);
  TEST_ASSERT_NOT_NULL(presentation);
  startDirectB(track);
  privateSourceTurn(track);
  TEST_ASSERT_GREATER_THAN_UINT32(0, track.getActiveLoop().privateSourceStagedRows());
  // A load replaces the Loop's identity under the cursor (same receipt, different Loop identity).
  const LoopId originalLoopId = track.getActiveLoop().loopId;
  track.getActiveLoop().loopId = originalLoopId + 1;
  Loop::PrivatePredecessorBinding binding{};
  binding.range = &presentation->rawRange;
  binding.receipt = presentation;
  binding.loopId = originalLoopId;
  binding.interval = presentation->interval;
  binding.captureContentGeneration = presentation->frozenCaptureContentGeneration;
  track.getActiveLoop().stepPrivatePredecessorSource(binding);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, track.getActiveLoop().privateSourceStagedRows(),
                                   "an identity mismatch must discard the staged rows");
  TEST_ASSERT_FALSE_MESSAGE(track.getActiveLoop().privatePredecessorSourcePending(),
                            "an identity mismatch must not leave the cursor pending");
  TEST_ASSERT_EQUAL_MESSAGE(Loop::PrivateSourceDeny::IdentityChanged,
                            track.getActiveLoop().privateSourceDeny(),
                            "the mismatch is reported as an identity change");
  TEST_ASSERT_EQUAL_UINT32(0, track.getActiveLoop().privateSourceDeferredHolds());
  track.getActiveLoop().loopId = originalLoopId;
}

void test_filtered_empty_record_does_not_clear_live_B() {
  Track& track = trackManager.getSelectedTrack();
  prepareSlot(track, 0, false);
  TEST_ASSERT_TRUE(track.startRecording(768));
  noteMinLengthRemoveEnabled = true;
  noteMinLengthTicks = 48;
  track.noteOn(1, 60, 100, 780);
  track.noteOff(1, 60, 0, 800); // Known production empty-result recipe.
  clockManager.assignCurrentTickSilently(1536);
  track.stopRecording(1536);
  TEST_ASSERT_TRUE(track.isPlaying());
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  track.startOverdubbing(1540);
  TEST_ASSERT_TRUE(track.isOverdubbing());
  track.noteOn(1, 72, 91, 1550);
  track.noteOff(1, 72, 0, 1614); // Long enough to distinguish B from filtered A.
  TEST_ASSERT_EQUAL_UINT32(2, track.getActiveLoop().capture.store.size());
  const auto B = TrackCaptureStopTestAccess::interval(track);
  const auto geometry = track.getActiveLoop().loopLengthTicks;
  report("empty_A_B_live", track);
  drainRecordPublication(track);
  report("empty_A_late_cleanup", track);
  TEST_ASSERT_EQUAL_UINT64(B, TrackCaptureStopTestAccess::interval(track));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, track.getActiveLoop().capture.store.size(), "empty A cleanup erased accepted B");
  TEST_ASSERT_EQUAL_MESSAGE(CapturePhase::Overdub, track.getActiveLoop().capture.phase,
      "empty A cleanup reset B Capture phase");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(geometry, track.getActiveLoop().loopLengthTicks,
      "empty A cleanup reset geometry still needed by accepted B");
}
void test_control_canonical_predecessor_is_visible_to_same_source_query() {
  Track& track = privateRecord();
  const auto A = readExtent(TrackCaptureStopTestAccess::presentation(track, 0)->rawRange);
  TEST_ASSERT_EQUAL_UINT32(12, A[0].tick);
  TEST_ASSERT_EQUAL_UINT32(96, A[1].tick);
  const NoteId expected = A[0].noteId;
  startDirectB(track);
  drainRecordPublication(track);
  Loop& loop = track.getActiveLoop();
  driveSourceCoverage(loop);
  loop.rebuildOverdubSourceView(48, "native_canonical_control");
  OverlapNoteIdSet participants;
  loop.collectOverdubSourceHoldParticipantIds(48, 60, participants);
  TEST_ASSERT_TRUE_MESSAGE(participants.contains(expected), "canonical control must see accepted A12..96 at48");
  // P-B-1's independent oracle: the same session, over the same pitch set, with the predecessor
  // published. Its overlap decision must be the one the private source produced.
  acceptB(track, 60);
  const SourceDecisionVec canonicalDecisions = sourceOverlapDecisions(loop);
  TEST_ASSERT_TRUE_MESSAGE(!canonicalDecisions.empty(),
                           "the canonical control must decide A's accepted pair");
  if (gPrivateOracle.valid) {
    // The pair's own decision, over the same pitch set, after real publication: same target, same
    // kind, same span. The whole canonical set is deliberately not compared -- the committed source
    // reports every same-pitch bar note as present at the hold, which is that origin's own breadth,
    // not this contract's subject.
    assertSameDecisionFor(gPrivateOracle.decisions, canonicalDecisions, gPrivateOracle.noteId,
                          "the canonical decision must equal the private source's decision");
  }
  TEST_ASSERT_NOT_EQUAL(Loop::SourceOrigin::PrivatePredecessor, loop.overdubSourceOrigin());
  report("control_canonical_source", track);
}

void test_control_clear_supersedes_A_and_rejects_stale_range_receipt() {
  Track& track = privateRecord();
  auto* presentation = TrackCaptureStopTestAccess::presentation(track, 0);
  const auto generation = presentation->rawRange.generation();
  track.clear();
  drainRecordPublication(track);
  TEST_ASSERT_FALSE(track.getActiveLoop().hasCommittedPasses());
  MidiEvent row{};
  bool end = false;
  TEST_ASSERT_FALSE(presentation->rawRange.read(generation, 0, 0, row, end));
  TEST_ASSERT_TRUE(track.startRecording(clockManager.getCurrentTick() + 1));
  track.noteOn(1, 79, 87, clockManager.getCurrentTick() + 12);
  TEST_ASSERT_EQUAL_UINT32(1, track.getActiveLoop().capture.store.size());
  for (unsigned i = 0; i < 8; ++i) TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_TRUE(track.isRecording());
  TEST_ASSERT_EQUAL_UINT32(1, track.getActiveLoop().capture.store.size());
  TEST_ASSERT_FALSE(track.getActiveLoop().hasCommittedPasses());
  TEST_ASSERT_EQUAL_UINT8(79, track.getActiveLoop().capture.store.at(0).data.noteData.note);
}

// P-C-1 second half (DEC-160 item 1): the empty-take geometry policy is deferred, not dropped.
// When B also ends empty, the deferral must still settle to the shipped empty-slot result
// (geometry 0, EMPTY) instead of leaving a silent non-zero loop playing.
void test_both_empty_intervals_settle_to_the_empty_geometry() {
  Track& track = trackManager.getSelectedTrack();
  prepareSlot(track, 0, false);
  TEST_ASSERT_TRUE(track.startRecording(768));
  noteMinLengthRemoveEnabled = true;
  noteMinLengthTicks = 48;
  track.noteOn(1, 60, 100, 780);
  track.noteOff(1, 60, 0, 800);
  clockManager.assignCurrentTickSilently(1536);
  track.stopRecording(1536);
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  track.startOverdubbing(1540);
  TEST_ASSERT_TRUE(track.isOverdubbing());
  const auto B = TrackCaptureStopTestAccess::interval(track);
  track.stopOverdubbing();                 // B accepts nothing: it is empty too.
  TEST_ASSERT_FALSE(track.isOverdubbing());
  TEST_ASSERT_EQUAL_UINT64(B + 1, TrackCaptureStopTestAccess::interval(track));
  drainRecordPublication(track);           // both empty results retire
  // The geometry policy is a debt settled on the first deferred turn that owes nobody; the
  // drain stops as soon as the ring empties, so give it the idle turns production has.
  for (unsigned i = 0; i < 8; ++i) TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL_UINT32(0, TrackCaptureStopTestAccess::jobs(track));
  TEST_ASSERT_FALSE_MESSAGE(track.getActiveLoop().hasCommittedPasses(),
                            "neither empty interval may commit a pass");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, track.getActiveLoop().loopLengthTicks,
                                   "both-empty handoff must settle to the empty geometry");
  TEST_ASSERT_EQUAL(TRACK_EMPTY, track.getState());
}

// P-C-2 / P-C-3 (DEC-160 items 1 and 3): undo attachment is causally scoped. A predecessor
// that publishes late must not erase a successor's checkpoint, while the shipped supersede
// rule for a *later* Record take on the same slot still holds.
void test_successor_undo_checkpoint_survives_a_late_predecessor_publication() {
  Track& track = privateRecord();
  const uint64_t intervalA = TrackCaptureStopTestAccess::interval(track);
  Loop& loop = track.getLoop(0);
  const LoopId loopId = loop.loopId;
  auto& stack = track.getGlobalUndoStack();
  TEST_ASSERT_EQUAL_UINT32(0, stack.undoCount());
  const UndoLoopGeometry geometry{loop.loopLengthTicks, loop.startLoopTick, loop.loopStartTick};
  // (1) The successor publishes first, so its checkpoint is already on the stack when the
  // predecessor's empty/publication grain runs.
  TrackUndo::pushRecordPassAdded(track, 0, 7001, loopId, geometry, geometry, intervalA + 1);
  TEST_ASSERT_EQUAL_UINT32(1, stack.undoCount());
  // (2) The predecessor publishes late. Only entries at or before its own interval may be
  // pruned, so the successor's entry survives and stack order stays causal order.
  TrackUndo::pushRecordPassAdded(track, 0, 7002, loopId, geometry, geometry, intervalA);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, stack.undoCount(),
                                   "a late predecessor must not erase a successor undo checkpoint");
  TEST_ASSERT_EQUAL_UINT64(intervalA + 1, stack.entries[0].interval);
  TEST_ASSERT_EQUAL_UINT64(intervalA, stack.entries[1].interval);
  TEST_ASSERT_EQUAL_UINT32(7001, stack.entries[0].passId);
  TEST_ASSERT_EQUAL_UINT32(7002, stack.entries[1].passId);
  // (3) The supersede rule survives: a Record take at a later interval prunes the checkpoints
  // it supersedes, and only those.
  TrackUndo::pushRecordPassAdded(track, 0, 7003, loopId, geometry, geometry, intervalA + 2);
  TEST_ASSERT_EQUAL_UINT32(1, stack.undoCount());
  TEST_ASSERT_EQUAL_UINT64(intervalA + 2, stack.entries[0].interval);
  TEST_ASSERT_EQUAL_UINT32(7003, stack.entries[0].passId);
}

} // namespace

int main() {
  UNITY_BEGIN();
  const char* profile = getenv("IMMEDIATE_HANDOFF_CASE");
  if (profile == nullptr) {
    RUN_TEST(test_immediate_start_accepts_input_before_record_publication);
    RUN_TEST(test_button_starts_later_overdubs_while_record_is_still_private);
    RUN_TEST(test_control_record_extent_is_immutable_and_live_admission_is_separate);
    RUN_TEST(test_riao4_7_live_overdub_marks_touched_coverage);
    RUN_TEST(test_control_late_record_publication_preserves_live_successor_rows);
    RUN_TEST(test_control_clear_supersedes_A_and_rejects_stale_range_receipt);
    RUN_TEST(test_early_stop_ends_B_without_reusing_A_custody);
    RUN_TEST(test_frozen_closure_is_interval_owned_while_a_later_interval_is_live);
    RUN_TEST(test_nine_real_custodies_cap_and_clear_without_reinterpreting_stale_handles);
    RUN_TEST(test_empty_successor_stop_takes_no_custody_extent);
    RUN_TEST(test_successor_undo_checkpoint_survives_a_late_predecessor_publication);
    // CONTRACTS.md C3 (P-B): the private-predecessor source origin, its bound, its deadline.
    RUN_TEST(test_private_record_is_the_same_pitch_overlap_source);
    // The canonical control consumes P-B-1's captured expectation, so it must run after it.
    RUN_TEST(test_control_canonical_predecessor_is_visible_to_same_source_query);
    RUN_TEST(test_two_sessions_over_one_private_predecessor_do_not_leak_intervals);
    RUN_TEST(test_predecessor_publication_mid_scan_discards_the_private_prefix);
    RUN_TEST(test_private_source_grain_is_bounded_by_the_declared_quantum);
    RUN_TEST(test_private_source_pressure_leaves_the_row_unconsumed);
    // Review round 1: the six findings' regression cases.
    RUN_TEST(test_private_source_pair_decisions_and_view_growth_are_bounded);
    RUN_TEST(test_a_revision_move_without_a_notify_supersedes_the_scan);
    RUN_TEST(test_five_holds_are_bounded_by_the_capacity_and_denied_by_name);
    RUN_TEST(test_a_loop_identity_mismatch_cancels_the_scan);
  } else if (strcmp(profile, "start") == 0) {
    RUN_TEST(test_immediate_start_accepts_input_before_record_publication);
  } else if (strcmp(profile, "stop") == 0) {
    RUN_TEST(test_early_stop_ends_B_without_reusing_A_custody);
    RUN_TEST(test_frozen_closure_is_interval_owned_while_a_later_interval_is_live);
    RUN_TEST(test_nine_real_custodies_cap_and_clear_without_reinterpreting_stale_handles);
    RUN_TEST(test_empty_successor_stop_takes_no_custody_extent);
  } else if (strcmp(profile, "source") == 0) {
    RUN_TEST(test_private_record_is_the_same_pitch_overlap_source);
    RUN_TEST(test_two_sessions_over_one_private_predecessor_do_not_leak_intervals);
    RUN_TEST(test_predecessor_publication_mid_scan_discards_the_private_prefix);
    RUN_TEST(test_private_source_grain_is_bounded_by_the_declared_quantum);
    RUN_TEST(test_private_source_pressure_leaves_the_row_unconsumed);
    // Review round 1: the six findings' regression cases.
    RUN_TEST(test_private_source_pair_decisions_and_view_growth_are_bounded);
    RUN_TEST(test_a_revision_move_without_a_notify_supersedes_the_scan);
    RUN_TEST(test_five_holds_are_bounded_by_the_capacity_and_denied_by_name);
    RUN_TEST(test_a_loop_identity_mismatch_cancels_the_scan);
    RUN_TEST(test_control_canonical_predecessor_is_visible_to_same_source_query);
  } else if (strcmp(profile, "empty") == 0) {
    RUN_TEST(test_filtered_empty_record_does_not_clear_live_B);
    RUN_TEST(test_successor_undo_checkpoint_survives_a_late_predecessor_publication);
    RUN_TEST(test_both_empty_intervals_settle_to_the_empty_geometry);
  } else {
    // A typo'd profile must reject the run, not abort: Unity's TEST_FAIL_MESSAGE longjmps to a
    // frame that only RUN_TEST establishes, so calling it here segfaults instead of failing.
    std::fprintf(stderr, "unknown IMMEDIATE_HANDOFF_CASE=%s (start|stop|source|empty)\n", profile);
    return 2;
  }
  return UNITY_END();
}
