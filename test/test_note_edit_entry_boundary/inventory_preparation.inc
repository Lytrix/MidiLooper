// Frozen oracle: SNAPSHOT-INVENTORY-PREPARATION.md. Real Loop metadata owner.
using InventoryCursor = Loop::CommittedSnapshotInventoryPreparation;
using InventoryResult = Loop::CommittedSnapshotInventoryPreparationResult;

static void seedInventory(Loop& loop) {
  loop.loopLengthTicks = Config::TICKS_PER_BAR * 4;
  loop.passes.recordPass.id = 11;
  loop.passes.recordPass.committedChunkIds = rawIdChunks(2);
  const PassId ids[] = {21, 22, 23, 0, 24, 24};
  for (size_t i = 0; i < 6; ++i) {
    OverdubPass pass;
    pass.id = ids[i];
    if (i != 2) pass.committedChunkIds = rawIdChunks(2, 20 + i * 10);
    if (i == 1) pass.state = CapturePassState::Disabled;
    loop.passes.overdubPasses.push_back(std::move(pass));
  }
  const EditPassId edits[] = {31, 32, 0, 33};
  for (size_t i = 0; i < 4; ++i) {
    EditPass pass;
    pass.id = edits[i];
    pass.passType = i == 3 ? EditPassType::ControlChange : EditPassType::Note;
    if (i == 1) pass.state = EditPassState::Disabled;
    loop.passes.editPasses.push_back(std::move(pass));
  }
}

static void assertInventoryIds(const std::vector<PassId>& captures,
                               const EditPassIdList& companions) {
  const PassId expected[] = {11, 21, 24, 24};
  const EditPassId edits[] = {31, 33};
  TEST_ASSERT_EQUAL_UINT32(4, captures.size());
  TEST_ASSERT_EQUAL_UINT32(2, companions.size());
  for (size_t i = 0; i < 4; ++i) TEST_ASSERT_EQUAL_UINT32(expected[i], captures[i]);
  for (size_t i = 0; i < 2; ++i) TEST_ASSERT_EQUAL_UINT32(edits[i], companions[i]);
}

static void assertInventoryResult(InventoryResult expected, InventoryResult actual) {
  TEST_ASSERT_EQUAL(static_cast<int>(expected), static_cast<int>(actual));
}

static void drainInventory(Loop& loop, InventoryCursor& cursor,
                           std::vector<PassId>& captures, EditPassIdList& companions,
                           size_t expectedTurns) {
  for (size_t turn = 1; turn <= expectedTurns; ++turn) {
    const auto before = cursor.inspectedRows();
    const auto result = loop.stepCommittedSnapshotInventoryPreparation(cursor, captures, companions);
    TEST_ASSERT_TRUE(cursor.inspectedRows() <= before + 1);
    assertInventoryResult(turn == expectedTurns ? InventoryResult::Complete : InventoryResult::Progress, result);
  }
  assertInventoryResult(InventoryResult::Complete,
      loop.committedSnapshotInventoryPreparationResult(cursor, captures, companions));
}

static void test_inventory_withheld_exact_grants_and_literal_ids() {
  LoopEventStore::initPool();
  Loop loop;
  seedInventory(loop);
  std::vector<PassId> captures;
  EditPassIdList companions;
  InventoryCursor cursor;
  loop.beginCommittedSnapshotInventoryPreparation(cursor, captures, companions);
  for (size_t i = 0; i < 8; ++i)
    assertInventoryResult(InventoryResult::Progress,
        loop.committedSnapshotInventoryPreparationResult(cursor, captures, companions));
  TEST_ASSERT_EQUAL_UINT32(0, cursor.inspectedRows());
  TEST_ASSERT_EQUAL_UINT32(0, captures.capacity());
  TEST_ASSERT_EQUAL_UINT32(0, companions.capacity());
  for (size_t turn = 1; turn <= 28; ++turn) {
    const auto before = cursor.inspectedRows();
    assertInventoryResult(turn == 28 ? InventoryResult::Complete : InventoryResult::Progress,
        loop.stepCommittedSnapshotInventoryPreparation(cursor, captures, companions));
    TEST_ASSERT_TRUE(cursor.inspectedRows() <= before + 1);
    if (turn <= 13) {
      TEST_ASSERT_EQUAL_UINT32(0, captures.capacity());
      TEST_ASSERT_EQUAL_UINT32(0, companions.capacity());
    }
    if (turn == 14) {
      TEST_ASSERT_TRUE(captures.capacity() >= 4);
      TEST_ASSERT_EQUAL_UINT32(0, companions.capacity());
    }
    if (turn >= 15) TEST_ASSERT_TRUE(companions.capacity() >= 2);
    const size_t captureSizes[] = {1, 2, 2, 2, 2, 3, 4}; // turns16–22
    const size_t expectedCaptures = turn <= 15 ? 0 : turn <= 22 ? captureSizes[turn - 16] : 4;
    const size_t expectedCompanions = turn < 24 ? 0 : turn < 27 ? 1 : 2;
    TEST_ASSERT_EQUAL_UINT32(expectedCaptures, captures.size());
    TEST_ASSERT_EQUAL_UINT32(expectedCompanions, companions.size());
  }
  TEST_ASSERT_EQUAL_UINT32(22, cursor.inspectedRows());
  TEST_ASSERT_EQUAL_UINT32(loop.committedPassRevision(), cursor.coveredPassRevision());
  assertInventoryIds(captures, companions);
  for (size_t i = 0; i < 8; ++i) {
    assertInventoryResult(InventoryResult::Complete,
        loop.committedSnapshotInventoryPreparationResult(cursor, captures, companions));
    assertInventoryResult(InventoryResult::Complete,
        loop.stepCommittedSnapshotInventoryPreparation(cursor, captures, companions));
  }
  TEST_ASSERT_EQUAL_UINT32(22, cursor.inspectedRows());
}

static void test_inventory_empty_and_existing_capacity_no_fabrication() {
  Loop loop;
  std::vector<PassId> captures;
  EditPassIdList companions;
  InventoryCursor cursor;
  loop.beginCommittedSnapshotInventoryPreparation(cursor, captures, companions);
  drainInventory(loop, cursor, captures, companions, 8);
  TEST_ASSERT_EQUAL_UINT32(2, cursor.inspectedRows());
  TEST_ASSERT_EQUAL_UINT32(0, captures.capacity());
  TEST_ASSERT_EQUAL_UINT32(0, companions.capacity());
  TEST_ASSERT_NULL(loop.publishedSnapshotIdentity());
  captures.reserve(12);
  companions.reserve(9);
  const auto* captureData = captures.data();
  const auto* companionData = companions.data();
  loop.beginCommittedSnapshotInventoryPreparation(cursor, captures, companions);
  drainInventory(loop, cursor, captures, companions, 8);
  TEST_ASSERT_EQUAL_PTR(captureData, captures.data());
  TEST_ASSERT_EQUAL_PTR(companionData, companions.data());
  TEST_ASSERT_TRUE(captures.empty());
  TEST_ASSERT_TRUE(companions.empty());
}

static void test_inventory_nonempty_admission_and_discard_do_not_release_outputs() {
  Loop loop;
  std::vector<PassId> captures{99};
  EditPassIdList companions;
  InventoryCursor cursor;
  loop.beginCommittedSnapshotInventoryPreparation(cursor, captures, companions);
  assertInventoryResult(InventoryResult::Fault,
      loop.stepCommittedSnapshotInventoryPreparation(cursor, captures, companions));
  TEST_ASSERT_EQUAL_UINT32(99, captures[0]);
  captures.clear();
  companions.push_back(77);
  loop.beginCommittedSnapshotInventoryPreparation(cursor, captures, companions);
  assertInventoryResult(InventoryResult::Fault,
      loop.committedSnapshotInventoryPreparationResult(cursor, captures, companions));
  cursor.discard();
  assertInventoryResult(InventoryResult::Idle,
      loop.stepCommittedSnapshotInventoryPreparation(cursor, captures, companions));
  TEST_ASSERT_EQUAL_UINT32(77, companions[0]);
}

static void test_inventory_revision_and_completed_receipt_supersession_sticky() {
  LoopEventStore::initPool();
  Loop loop;
  seedInventory(loop);
  std::vector<PassId> captures;
  EditPassIdList companions;
  InventoryCursor cursor;
  loop.beginCommittedSnapshotInventoryPreparation(cursor, captures, companions);
  loop.stepCommittedSnapshotInventoryPreparation(cursor, captures, companions);
  const auto revision = loop.playbackRevision;
  ++loop.playbackRevision;
  assertInventoryResult(InventoryResult::Superseded,
      loop.stepCommittedSnapshotInventoryPreparation(cursor, captures, companions));
  TEST_ASSERT_EQUAL_UINT32(1, cursor.inspectedRows());
  loop.playbackRevision = revision;
  assertInventoryResult(InventoryResult::Superseded,
      loop.stepCommittedSnapshotInventoryPreparation(cursor, captures, companions));
  loop.beginCommittedSnapshotInventoryPreparation(cursor, captures, companions);
  drainInventory(loop, cursor, captures, companions, 28);
  ++loop.playbackRevision;
  assertInventoryResult(InventoryResult::Superseded,
      loop.committedSnapshotInventoryPreparationResult(cursor, captures, companions));
  assertInventoryResult(InventoryResult::Superseded,
      loop.stepCommittedSnapshotInventoryPreparation(cursor, captures, companions));
  assertInventoryIds(captures, companions);
}

static void test_inventory_same_shape_reset_reseed_generation_rejects() {
  LoopEventStore::initPool();
  Loop loop;
  seedInventory(loop);
  std::vector<PassId> captures;
  EditPassIdList companions;
  InventoryCursor cursor;
  loop.beginCommittedSnapshotInventoryPreparation(cursor, captures, companions);
  loop.stepCommittedSnapshotInventoryPreparation(cursor, captures, companions);
  const auto revision = loop.playbackRevision;
  loop.resetPassTimeline();
  seedInventory(loop); // identical shape/IDs, safe even under guard-removal control
  loop.playbackRevision = revision;
  assertInventoryResult(InventoryResult::Superseded,
      loop.stepCommittedSnapshotInventoryPreparation(cursor, captures, companions));
  TEST_ASSERT_EQUAL_UINT32(1, cursor.inspectedRows());
  TEST_ASSERT_TRUE(captures.empty());
  TEST_ASSERT_TRUE(companions.empty());
}

static void test_inventory_foreign_loop_and_scalar_topology_guards() {
  Loop loop, other;
  std::vector<PassId> captures;
  EditPassIdList companions;
  InventoryCursor cursor;
  loop.beginCommittedSnapshotInventoryPreparation(cursor, captures, companions);
  assertInventoryResult(InventoryResult::Superseded,
      other.stepCommittedSnapshotInventoryPreparation(cursor, captures, companions));
  for (size_t kind = 0; kind < 5; ++kind) {
    loop.beginCommittedSnapshotInventoryPreparation(cursor, captures, companions);
    if (kind == 0) ++loop.loopId;
    if (kind == 1) ++loop.loopLengthTicks;
    if (kind == 2) ++loop.passes.recordPass.id;
    if (kind == 3) loop.passes.overdubPasses.emplace_back();
    if (kind == 4) loop.passes.editPasses.emplace_back();
    assertInventoryResult(InventoryResult::Superseded,
        loop.stepCommittedSnapshotInventoryPreparation(cursor, captures, companions));
    TEST_ASSERT_EQUAL_UINT32(0, cursor.inspectedRows());
  }
}

static void test_inventory_output_headers_sizes_and_storage_are_qualified() {
  Loop loop;
  std::vector<PassId> captures, foreignCaptures;
  EditPassIdList companions, foreignCompanions;
  InventoryCursor cursor;
  loop.beginCommittedSnapshotInventoryPreparation(cursor, captures, companions);
  assertInventoryResult(InventoryResult::Superseded,
      loop.stepCommittedSnapshotInventoryPreparation(cursor, foreignCaptures, companions));
  loop.beginCommittedSnapshotInventoryPreparation(cursor, captures, companions);
  assertInventoryResult(InventoryResult::Superseded,
      loop.stepCommittedSnapshotInventoryPreparation(cursor, captures, foreignCompanions));
  loop.beginCommittedSnapshotInventoryPreparation(cursor, captures, companions);
  captures.push_back(89);
  assertInventoryResult(InventoryResult::Superseded,
      loop.stepCommittedSnapshotInventoryPreparation(cursor, captures, companions));
  captures.clear();
  loop.beginCommittedSnapshotInventoryPreparation(cursor, captures, companions);
  companions.reserve(10); // same size, changed storage
  assertInventoryResult(InventoryResult::Superseded,
      loop.stepCommittedSnapshotInventoryPreparation(cursor, captures, companions));
  loop.beginCommittedSnapshotInventoryPreparation(cursor, captures, companions);
  drainInventory(loop, cursor, captures, companions, 8);
  companions.push_back(71);
  assertInventoryResult(InventoryResult::Superseded,
      loop.committedSnapshotInventoryPreparationResult(cursor, captures, companions));
  TEST_ASSERT_EQUAL_UINT32(71, companions[0]);
}

static void test_inventory_preserves_root_pending_chunks_and_legacy_parity() {
  LoopEventStore::initPool();
  Loop loop;
  seedInventory(loop);
  loop.storePublishedSnapshotForTest(std::make_shared<const LoopSnapshot>(
      LoopSnapshot(loop.loopId, loop.loopLengthTicks, 0, 1, 1)), loop.playbackRevision);
  TEST_ASSERT_TRUE(loop.requestCommittedPassesRepublish());
  const auto* identity = loop.publishedSnapshotIdentity();
  const auto publishedRevision = loop.publishedSnapshotRevision();
  const auto revision = loop.playbackRevision;
  const auto queued = loop.pendingSnapshotQueuedPassCount();
  const auto* pendingCompanions = loop.pendingSnapshotCompanionIds();
  TEST_ASSERT_NOT_NULL(pendingCompanions);
  const auto companionCount = pendingCompanions->size();
  const auto used = LoopEventStore::usedChunkCount();
  const auto id = loop.passes.recordPass.committedChunkIds[0];
  const auto refs = LoopEventStore::chunkReferenceCount(id);
  const auto row = rawIdRow(loop.passes.recordPass.committedChunkIds, 0, 1);
  std::vector<PassId> captures;
  EditPassIdList companions;
  InventoryCursor cursor;
  loop.beginCommittedSnapshotInventoryPreparation(cursor, captures, companions);
  drainInventory(loop, cursor, captures, companions, 28);
  assertInventoryIds(captures, companions);
  TEST_ASSERT_EQUAL_PTR(identity, loop.publishedSnapshotIdentity());
  TEST_ASSERT_EQUAL_UINT32(publishedRevision, loop.publishedSnapshotRevision());
  TEST_ASSERT_EQUAL_UINT32(revision, loop.playbackRevision);
  TEST_ASSERT_EQUAL_UINT32(queued, loop.pendingSnapshotQueuedPassCount());
  TEST_ASSERT_EQUAL_PTR(pendingCompanions, loop.pendingSnapshotCompanionIds());
  TEST_ASSERT_EQUAL_UINT32(companionCount, loop.pendingSnapshotCompanionIds()->size());
  TEST_ASSERT_EQUAL_UINT16(used, LoopEventStore::usedChunkCount());
  TEST_ASSERT_EQUAL_UINT16(refs, LoopEventStore::chunkReferenceCount(id));
  TEST_ASSERT_EQUAL_UINT32(row.noteId, rawIdRow(loop.passes.recordPass.committedChunkIds, 0, 1).noteId);
  PendingSnapshotPublishBox legacy;
  TEST_ASSERT_TRUE(queueAllCommittedPassesInto(loop, legacy));
  assertInventoryIds(legacy.queuedPassIds, legacy.companionIds);
  cursor.discard();
  assertInventoryIds(captures, companions);
}
