// Real Loop-owned committed identity preparation. Raw restored inputs are cold
// fixture I/O, not a duplicate worker. Record normalization normally fills IDs.
using IdResult = Loop::CommittedNoteIdPreparationResult;
using IdCursor = Loop::CommittedNoteIdPreparation;

static CommittedChunkIdList rawIdChunks(size_t count, uint32_t start = 0) {
  LoopEventStore store;
  for (size_t i = 0; i < count; ++i) {
    MidiEvent on = MidiEvent::NoteOn(start + static_cast<uint32_t>(i), 3, 60, 99);
    on.noteId = i == 0 ? 9000 : kInvalidNoteId;
    TEST_ASSERT_TRUE(store.append(on));
  }
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(start + count, 3, 60, 0)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::ControlChange(start + count + 1, 3, 7, 64)));
  CommittedChunkIdList refs;
  TEST_ASSERT_TRUE(store.detachChunksToCommittedChunkIds(refs));
  return refs;
}

static Loop& rawIdLoop(size_t count = LoopEventStoreConfig::CHUNK_CAPACITY + 1) {
  Track& track = trackManager.getTrack(0);
  track.ensureLoopsAllocated();
  Loop& loop = track.getLoop(0);
  loop.loopLengthTicks = Config::TICKS_PER_BAR * 4;
  loop.nextNoteId_ = 400;
  loop.passes.recordPass.id = 1;
  loop.passes.recordPass.committedChunkIds = rawIdChunks(count);
  return loop;
}

static MidiEvent rawIdRow(const CommittedChunkIdList& refs, size_t chunk, size_t row) {
  MidiEvent out;
  bool end = false;
  TEST_ASSERT_TRUE(LoopEventStore::readSealedChunkEvent(refs[chunk], row, out, end));
  TEST_ASSERT_FALSE(end);
  return out;
}

static void drainIdPreparation(Loop& loop, IdCursor& cursor, size_t limit = 64) {
  for (size_t turn = 0; turn < limit; ++turn) {
    const size_t before = cursor.inspectedChunks();
    const auto result = loop.stepCommittedNoteIdPreparation(cursor);
    TEST_ASSERT_TRUE(cursor.inspectedChunks() <= before + 1);
    if (result == IdResult::Complete) {
      TEST_ASSERT_TRUE(loop.committedNoteIdPreparationComplete(cursor));
      return;
    }
    TEST_ASSERT_EQUAL(static_cast<int>(IdResult::Progress), static_cast<int>(result));
    TEST_ASSERT_FALSE(loop.committedNoteIdPreparationComplete(cursor));
  }
  TEST_FAIL_MESSAGE("committed ID preparation exhausted pass/chunk fixture turn bound");
}

static void test_committed_id_preparation_acceptance_and_single_chunk_grants() {
  LoopEventStore::initPool();
  Loop& loop = rawIdLoop();
  const auto& refs = loop.passes.recordPass.committedChunkIds;
  TEST_ASSERT_EQUAL_UINT32(2, refs.size());
  const uint16_t used = LoopEventStore::usedChunkCount();
  const uint16_t owners = LoopEventStore::chunkReferenceCount(refs[0]);
  IdCursor cursor;
  loop.beginCommittedNoteIdPreparation(cursor);
  for (unsigned i = 0; i < 8; ++i) TEST_ASSERT_FALSE(loop.committedNoteIdPreparationComplete(cursor));
  TEST_ASSERT_EQUAL_UINT32(0, cursor.inspectedChunks());
  TEST_ASSERT_EQUAL_UINT32(0, cursor.assignedIds());
  TEST_ASSERT_EQUAL_UINT32(400, loop.peekNextNoteId());
  TEST_ASSERT_EQUAL_UINT32(kInvalidNoteId, rawIdRow(refs, 0, 1).noteId);
  TEST_ASSERT_EQUAL_UINT32(kInvalidNoteId, rawIdRow(refs, 1, 0).noteId);
  TEST_ASSERT_EQUAL(static_cast<int>(IdResult::Progress),
                    static_cast<int>(loop.stepCommittedNoteIdPreparation(cursor)));
  TEST_ASSERT_EQUAL_UINT32(1, cursor.inspectedChunks());
  TEST_ASSERT_EQUAL_UINT32(255, cursor.assignedIds());
  TEST_ASSERT_EQUAL_UINT32(9000, rawIdRow(refs, 0, 0).noteId);
  TEST_ASSERT_EQUAL_UINT32(400, rawIdRow(refs, 0, 1).noteId);
  TEST_ASSERT_EQUAL_UINT32(654, rawIdRow(refs, 0, 255).noteId);
  TEST_ASSERT_EQUAL_UINT32(kInvalidNoteId, rawIdRow(refs, 1, 0).noteId);
  TEST_ASSERT_FALSE(loop.committedNoteIdPreparationComplete(cursor));
  TEST_ASSERT_EQUAL(static_cast<int>(IdResult::Progress),
                    static_cast<int>(loop.stepCommittedNoteIdPreparation(cursor)));
  TEST_ASSERT_EQUAL_UINT32(655, rawIdRow(refs, 1, 0).noteId);
  TEST_ASSERT_EQUAL_UINT32(kInvalidNoteId, rawIdRow(refs, 1, 1).noteId); // Off
  TEST_ASSERT_EQUAL_UINT32(kInvalidNoteId, rawIdRow(refs, 1, 2).noteId); // CC
  TEST_ASSERT_FALSE(loop.committedNoteIdPreparationComplete(cursor));
  TEST_ASSERT_EQUAL(static_cast<int>(IdResult::Progress),
                    static_cast<int>(loop.stepCommittedNoteIdPreparation(cursor))); // record transition
  TEST_ASSERT_EQUAL(static_cast<int>(IdResult::Complete),
                    static_cast<int>(loop.stepCommittedNoteIdPreparation(cursor))); // terminal
  TEST_ASSERT_EQUAL_UINT32(2, cursor.inspectedChunks());
  TEST_ASSERT_EQUAL_UINT32(256, cursor.assignedIds());
  TEST_ASSERT_EQUAL_UINT16(used, LoopEventStore::usedChunkCount());
  TEST_ASSERT_EQUAL_UINT16(owners, LoopEventStore::chunkReferenceCount(refs[0]));
  TEST_ASSERT_FALSE(editManager.isNoteEditActive());
  TEST_ASSERT_EQUAL_UINT32(0, outboundObservation().requestCalls);
}

static void test_committed_id_preparation_pass_order_disabled_rows_and_legacy_parity() {
  LoopEventStore::initPool();
  Loop& loop = rawIdLoop(2);
  loop.passes.overdubPasses.emplace_back(); // empty active pass consumes a transition
  OverdubPass disabled;
  disabled.id = 2;
  disabled.state = CapturePassState::Disabled;
  disabled.committedChunkIds = rawIdChunks(2, 20);
  loop.passes.overdubPasses.push_back(std::move(disabled));
  OverdubPass active;
  active.id = 3;
  active.committedChunkIds = rawIdChunks(2, 30);
  loop.passes.overdubPasses.push_back(std::move(active));
  Loop legacy;
  legacy.nextNoteId_ = 400;
  legacy.passes.recordPass.id = 1;
  legacy.passes.recordPass.committedChunkIds = rawIdChunks(2);
  for (size_t i = 0; i < 3; ++i) {
    OverdubPass pass;
    pass.id = loop.passes.overdubPasses[i].id;
    pass.state = loop.passes.overdubPasses[i].state;
    if (i) pass.committedChunkIds = rawIdChunks(2, i == 1 ? 20 : 30);
    legacy.passes.overdubPasses.push_back(std::move(pass));
  }
  IdCursor cursor;
  loop.beginCommittedNoteIdPreparation(cursor);
  // Record scan, record end, empty pass, disabled pass, active scan, active
  // end, terminal. Empty/disabled passes may not be silently skipped in a grant.
  const size_t expectedChunks[] = {1, 1, 1, 1, 2, 2, 2};
  for (size_t turn = 0; turn < 7; ++turn) {
    const auto result = loop.stepCommittedNoteIdPreparation(cursor);
    TEST_ASSERT_EQUAL(static_cast<int>(turn == 6 ? IdResult::Complete : IdResult::Progress),
                      static_cast<int>(result));
    TEST_ASSERT_EQUAL_UINT32(expectedChunks[turn], cursor.inspectedChunks());
    TEST_ASSERT_EQUAL(turn == 6, loop.committedNoteIdPreparationComplete(cursor));
  }
  legacy.assignMissingNoteIdsInCommittedCapturePasses();
  TEST_ASSERT_EQUAL_UINT32(2, cursor.assignedIds());
  TEST_ASSERT_EQUAL_UINT32(2, cursor.inspectedChunks());
  TEST_ASSERT_EQUAL_UINT32(400, rawIdRow(loop.passes.recordPass.committedChunkIds, 0, 1).noteId);
  TEST_ASSERT_EQUAL_UINT32(kInvalidNoteId, rawIdRow(loop.passes.overdubPasses[1].committedChunkIds, 0, 1).noteId);
  TEST_ASSERT_EQUAL_UINT32(401, rawIdRow(loop.passes.overdubPasses[2].committedChunkIds, 0, 1).noteId);
  TEST_ASSERT_EQUAL_UINT32(legacy.peekNextNoteId(), loop.peekNextNoteId());
  for (size_t row = 0; row < 4; ++row) {
    TEST_ASSERT_EQUAL_UINT32(rawIdRow(legacy.passes.recordPass.committedChunkIds, 0, row).noteId,
                             rawIdRow(loop.passes.recordPass.committedChunkIds, 0, row).noteId);
    TEST_ASSERT_EQUAL_UINT32(rawIdRow(legacy.passes.overdubPasses[2].committedChunkIds, 0, row).noteId,
                             rawIdRow(loop.passes.overdubPasses[2].committedChunkIds, 0, row).noteId);
  }
}

static void test_committed_id_preparation_stale_revision_rejects_then_restarts_idempotently() {
  LoopEventStore::initPool();
  Loop& loop = rawIdLoop();
  IdCursor cursor;
  loop.beginCommittedNoteIdPreparation(cursor);
  TEST_ASSERT_EQUAL(static_cast<int>(IdResult::Progress),
                    static_cast<int>(loop.stepCommittedNoteIdPreparation(cursor)));
  ++loop.playbackRevision;
  TEST_ASSERT_EQUAL(static_cast<int>(IdResult::Superseded),
                    static_cast<int>(loop.stepCommittedNoteIdPreparation(cursor)));
  TEST_ASSERT_EQUAL_UINT32(655, loop.peekNextNoteId());
  TEST_ASSERT_EQUAL_UINT32(kInvalidNoteId, rawIdRow(loop.passes.recordPass.committedChunkIds, 1, 0).noteId);
  loop.beginCommittedNoteIdPreparation(cursor);
  drainIdPreparation(loop, cursor);
  TEST_ASSERT_EQUAL_UINT32(1, cursor.assignedIds());
  TEST_ASSERT_EQUAL_UINT32(400, rawIdRow(loop.passes.recordPass.committedChunkIds, 0, 1).noteId);
  TEST_ASSERT_EQUAL_UINT32(655, rawIdRow(loop.passes.recordPass.committedChunkIds, 1, 0).noteId);
  ++loop.canonicalStorageGeneration;
  TEST_ASSERT_FALSE(loop.committedNoteIdPreparationComplete(cursor));
  TEST_ASSERT_EQUAL(static_cast<int>(IdResult::Superseded),
                    static_cast<int>(loop.stepCommittedNoteIdPreparation(cursor)));
}

static void test_committed_id_preparation_reset_recycled_source_cannot_revive_coordinates() {
  LoopEventStore::initPool();
  Loop& loop = rawIdLoop();
  IdCursor cursor;
  loop.beginCommittedNoteIdPreparation(cursor);
  loop.stepCommittedNoteIdPreparation(cursor);
  const uint32_t oldRevision = loop.playbackRevision;
  loop.resetPassTimeline();
  loop.playbackRevision = oldRevision; // ABA: revision alone is not storage identity
  loop.passes.recordPass.id = 1;
  loop.passes.recordPass.committedChunkIds = rawIdChunks(LoopEventStoreConfig::CHUNK_CAPACITY + 1);
  TEST_ASSERT_EQUAL(static_cast<int>(IdResult::Superseded),
                    static_cast<int>(loop.stepCommittedNoteIdPreparation(cursor)));
  TEST_ASSERT_EQUAL_UINT32(kInvalidNoteId, rawIdRow(loop.passes.recordPass.committedChunkIds, 1, 0).noteId);
  TEST_ASSERT_EQUAL_UINT32(1, loop.peekNextNoteId());
}

static void test_committed_id_preparation_wrong_loop_capture_change_and_discard() {
  LoopEventStore::initPool();
  Loop& loop = rawIdLoop(2);
  Loop other;
  IdCursor cursor;
  loop.beginCommittedNoteIdPreparation(cursor);
  TEST_ASSERT_EQUAL(static_cast<int>(IdResult::Superseded),
                    static_cast<int>(other.stepCommittedNoteIdPreparation(cursor)));
  TEST_ASSERT_EQUAL_UINT32(400, loop.peekNextNoteId());
  loop.beginCommittedNoteIdPreparation(cursor);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(50, 3, 70, 100)));
  TEST_ASSERT_EQUAL(static_cast<int>(IdResult::Superseded),
                    static_cast<int>(loop.stepCommittedNoteIdPreparation(cursor)));
  TEST_ASSERT_EQUAL_UINT32(kInvalidNoteId, rawIdRow(loop.passes.recordPass.committedChunkIds, 0, 1).noteId);
  const NoteId next = loop.peekNextNoteId();
  cursor.discard();
  TEST_ASSERT_EQUAL(static_cast<int>(IdResult::Idle),
                    static_cast<int>(loop.stepCommittedNoteIdPreparation(cursor)));
  TEST_ASSERT_EQUAL_UINT32(next, loop.peekNextNoteId());
}

static void test_committed_id_preparation_invalid_chunk_fault_and_empty_completion() {
  LoopEventStore::initPool();
  Loop& loop = rawIdLoop(2);
  const uint16_t id = loop.passes.recordPass.committedChunkIds[0];
  loop.passes.recordPass.committedChunkIds[0] = LoopEventStoreConfig::POOL_CHUNK_COUNT;
  IdCursor cursor;
  loop.beginCommittedNoteIdPreparation(cursor);
  TEST_ASSERT_EQUAL(static_cast<int>(IdResult::Fault),
                    static_cast<int>(loop.stepCommittedNoteIdPreparation(cursor)));
  TEST_ASSERT_EQUAL_UINT32(400, loop.peekNextNoteId());
  TEST_ASSERT_FALSE(loop.committedNoteIdPreparationComplete(cursor));
  loop.passes.recordPass.committedChunkIds[0] = id;
  TEST_ASSERT_EQUAL(static_cast<int>(IdResult::Fault),
                    static_cast<int>(loop.stepCommittedNoteIdPreparation(cursor)));
  loop.beginCommittedNoteIdPreparation(cursor);
  drainIdPreparation(loop, cursor);
  Loop empty;
  empty.beginCommittedNoteIdPreparation(cursor);
  TEST_ASSERT_FALSE(empty.committedNoteIdPreparationComplete(cursor));
  TEST_ASSERT_EQUAL(static_cast<int>(IdResult::Progress),
                    static_cast<int>(empty.stepCommittedNoteIdPreparation(cursor)));
  TEST_ASSERT_EQUAL(static_cast<int>(IdResult::Complete),
                    static_cast<int>(empty.stepCommittedNoteIdPreparation(cursor)));
  TEST_ASSERT_EQUAL_UINT32(0, cursor.assignedIds());
}

static void test_committed_id_preparation_preserves_shared_payload_root_and_refcounts() {
  LoopEventStore::initPool();
  Loop& loop = rawIdLoop(2);
  auto& refs = loop.passes.recordPass.committedChunkIds;
  CommittedChunkIdList borrower;
  TEST_ASSERT_TRUE(LoopEventStore::tryCopyCommittedChunkIds(borrower, refs));
  LoopEventStore::retainChunkRefs(borrower);
  const uint16_t owners = LoopEventStore::chunkReferenceCount(refs[0]);
  const uint16_t used = LoopEventStore::usedChunkCount();
  // A real non-null predecessor root stays owned by Loop, not the cursor.
  loop.storePublishedSnapshotForTest(std::make_shared<const LoopSnapshot>(
      LoopSnapshot(loop.loopId, loop.loopLengthTicks, 0, 1, 1)), loop.playbackRevision);
  TEST_ASSERT_NOT_NULL(loop.publishedSnapshotIdentity());
  const uint32_t revision = loop.playbackRevision;
  MidiEventVec before;
  LoopEventStore::appendChunkRefEvents(refs, before);
  IdCursor cursor;
  loop.beginCommittedNoteIdPreparation(cursor);
  const auto retainedRoot = loop.publishedSnapshotIdentity();
  drainIdPreparation(loop, cursor);
  const NoteId next = loop.peekNextNoteId();
  for (unsigned i = 0; i < 4; ++i) {
    TEST_ASSERT_EQUAL(static_cast<int>(IdResult::Complete),
                      static_cast<int>(loop.stepCommittedNoteIdPreparation(cursor)));
    TEST_ASSERT_EQUAL_UINT32(1, cursor.assignedIds());
    TEST_ASSERT_EQUAL_UINT32(1, cursor.inspectedChunks());
    TEST_ASSERT_EQUAL_UINT32(next, loop.peekNextNoteId());
    TEST_ASSERT_EQUAL_PTR(retainedRoot, loop.publishedSnapshotIdentity());
  }
  MidiEventVec after;
  LoopEventStore::appendChunkRefEvents(borrower, after);
  TEST_ASSERT_EQUAL_UINT32(before.size(), after.size());
  for (size_t i = 0; i < before.size(); ++i) {
    TEST_ASSERT_EQUAL_UINT32(before[i].tick, after[i].tick);
    TEST_ASSERT_EQUAL(before[i].type, after[i].type);
    TEST_ASSERT_EQUAL_UINT8(before[i].channel, after[i].channel);
    if (before[i].isNoteOn() || before[i].isNoteOff()) {
      TEST_ASSERT_EQUAL_UINT8(before[i].data.noteData.note, after[i].data.noteData.note);
      TEST_ASSERT_EQUAL_UINT8(before[i].data.noteData.velocity, after[i].data.noteData.velocity);
    } else {
      TEST_ASSERT_EQUAL_UINT8(before[i].data.ccData.cc, after[i].data.ccData.cc);
      TEST_ASSERT_EQUAL_UINT8(before[i].data.ccData.value, after[i].data.ccData.value);
    }
  }
  TEST_ASSERT_EQUAL_UINT16(owners, LoopEventStore::chunkReferenceCount(refs[0]));
  TEST_ASSERT_EQUAL_UINT16(used, LoopEventStore::usedChunkCount());
  TEST_ASSERT_NOT_NULL(loop.publishedSnapshotIdentity());
  TEST_ASSERT_EQUAL_UINT32(revision, loop.playbackRevision);
  TEST_ASSERT_EQUAL_UINT32(400, after[1].noteId);
  LoopEventStore::releaseChunkRefs(borrower);
}

static void test_committed_id_preparation_valid_first_unsealed_second_is_not_drained() {
  LoopEventStore::initPool();
  Loop& loop = rawIdLoop(2);
  LoopEventStore live;
  TEST_ASSERT_TRUE(live.append(MidiEvent::NoteOn(40, 3, 61, 99)));
  auto& refs = loop.passes.recordPass.committedChunkIds;
  refs.push_back(live.chunkIds()[0]); // fault-injected directory; live retains ownership
  IdCursor cursor;
  loop.beginCommittedNoteIdPreparation(cursor);
  TEST_ASSERT_EQUAL(static_cast<int>(IdResult::Progress),
                    static_cast<int>(loop.stepCommittedNoteIdPreparation(cursor)));
  TEST_ASSERT_EQUAL_UINT32(1, cursor.assignedIds());
  TEST_ASSERT_EQUAL_UINT32(1, cursor.inspectedChunks());
  TEST_ASSERT_EQUAL_UINT32(kInvalidNoteId, live.at(0).noteId);
  TEST_ASSERT_EQUAL(static_cast<int>(IdResult::Fault),
                    static_cast<int>(loop.stepCommittedNoteIdPreparation(cursor)));
  TEST_ASSERT_EQUAL_UINT32(kInvalidNoteId, live.at(0).noteId);
  TEST_ASSERT_EQUAL_UINT32(401, loop.peekNextNoteId());
  TEST_ASSERT_FALSE(loop.committedNoteIdPreparationComplete(cursor));
  refs.pop_back(); // remove fault injection, without releasing the live owner's chunk
}
