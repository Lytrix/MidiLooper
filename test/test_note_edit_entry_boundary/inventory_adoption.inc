// Frozen fresh-only adoption oracle. All readiness/lifetime setup uses real owners.
namespace inventory_adoption_test {
using Cursor = Loop::CommittedSnapshotInventoryPreparation;
using Source = Loop::CommittedSnapshotInventoryPreparationResult;
using Result = Loop::CommittedSnapshotInventoryAdoptionResult;
using Destination = Loop::PendingSnapshotInventoryDestinationResult;
using Mode = Loop::CommittedSnapshotInventoryAdoptionMode;
const PendingSnapshotPublishBox* observe(const Loop& loop) {
  return PendingSnapshotRetirementTestAccess::observe(loop);
}
void record(Loop& loop) {
  loop.loopLengthTicks = Config::TICKS_PER_BAR;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(10, 1, 60, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(30, 1, 60)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(Config::TICKS_PER_BAR, CommitReason::RecordStop));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass());
  TEST_ASSERT_EQUAL_UINT32(1, loop.passes.recordPass.id);
  loop.discardCapture(); // Real stopped capture cleanup; committed chunks remain canonical.
  loop.clearPendingSnapshotPublishForTest(); // Legacy Record-stop queue cleanup, STOPPED setup.
}
void complete(Loop& loop, Cursor& cursor, std::vector<PassId>& captures, EditPassIdList& companions) {
  loop.beginCommittedSnapshotInventoryPreparation(cursor, captures, companions);
  const size_t turns = 8 + 2 * (loop.passes.overdubPasses.size() + loop.passes.editPasses.size());
  for (size_t i = 0; i < turns; ++i) loop.stepCommittedSnapshotInventoryPreparation(cursor, captures, companions);
  TEST_ASSERT_EQUAL(Source::Complete, loop.committedSnapshotInventoryPreparationResult(cursor, captures, companions));
}
void publish(Loop& loop) {
  for (unsigned i = 0; loop.hasPendingSnapshotPublish() && i < 10000; ++i) loop.stepPendingSnapshotPublish(1);
  TEST_ASSERT_FALSE(loop.hasPendingSnapshotPublish());
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
}
EditPassId companion(Loop& loop, bool hide) {
  EditPass row;
  row.targetNoteId = 1;
  row.pitch = 60; row.velocity = 90;
  row.actionType = hide ? EditActionType::Delete : EditActionType::Update;
  row.startTick = 10;
  row.propertyType = EditPropertyType::Length;
  row.endTick = hide ? 30 : 70;
  return loop.saveNoteEditPass(kOverdubCompanionEditPassIndex, row, EditPassType::Note, false, true);
}
void reserved(Loop& loop, size_t grant, bool queueBeforePrewarm = true) {
  record(loop);
  TEST_ASSERT_EQUAL_UINT32(2, companion(loop, false));
  if (queueBeforePrewarm) TEST_ASSERT_TRUE(loop.requestCommittedPassesRepublish());
  else loop.clearPendingSnapshotPublishForTest();
  TEST_ASSERT_TRUE(loop.prewarmPreparedCompanionHistory(grant));
  loop.clearPendingSnapshotPublishForTest();
  // Existing STOPPED legacy cleanup above is fixture setup only.
  TEST_ASSERT_NOT_NULL(observe(loop));
}
void refuse(Loop& loop, Cursor& cursor, std::vector<PassId>& captures, EditPassIdList& companions,
            Result expected, Mode mode = Mode::Republish) {
  const auto* box = observe(loop);
  const auto* captureData = captures.data(); const auto* companionData = companions.data();
  const auto captureCopy = captures; const auto companionCopy = companions;
  const auto root = loop.publishedSnapshot().get();
  const auto revision = cursor.coveredPassRevision(); const auto rows = cursor.inspectedRows();
  const auto source = loop.committedSnapshotInventoryPreparationResult(cursor, captures, companions);
  const auto generation = PendingSnapshotRetirementTestAccess::generation(loop);
  const auto metadata = box ? pending_box_test::metadata(*box) : std::vector<uint64_t>{};
  const auto boxCaptures = box ? box->queuedPassIds : std::vector<PassId>{};
  const auto boxCompanions = box ? box->companionIds : EditPassIdList{};
  TEST_ASSERT_EQUAL(expected, loop.adoptCommittedSnapshotInventory(cursor, captures, companions, mode));
  TEST_ASSERT_EQUAL_PTR(box, observe(loop)); TEST_ASSERT_EQUAL_PTR(root, loop.publishedSnapshot().get());
  TEST_ASSERT_EQUAL_PTR(captureData, captures.data()); TEST_ASSERT_EQUAL_PTR(companionData, companions.data());
  TEST_ASSERT_TRUE(captureCopy == captures && companionCopy == companions);
  TEST_ASSERT_EQUAL_UINT32(revision, cursor.coveredPassRevision()); TEST_ASSERT_EQUAL_UINT64(rows, cursor.inspectedRows());
  TEST_ASSERT_EQUAL(source, loop.committedSnapshotInventoryPreparationResult(cursor, captures, companions));
  TEST_ASSERT_EQUAL_UINT64(generation, PendingSnapshotRetirementTestAccess::generation(loop));
  if (box) {
    TEST_ASSERT_TRUE(metadata == pending_box_test::metadata(*box));
    TEST_ASSERT_TRUE(boxCaptures == box->queuedPassIds && boxCompanions == box->companionIds);
  }
}
void adopted(Loop& loop, Cursor& cursor, std::vector<PassId>& captures, EditPassIdList& companions, Mode mode) {
  const auto* box = observe(loop); TEST_ASSERT_NOT_NULL(box);
  const auto* oldCaptures = box->queuedPassIds.data(); const auto* oldCompanions = box->companionIds.data();
  const auto oldCaptureCapacity = box->queuedPassIds.capacity(); const auto oldCompanionCapacity = box->companionIds.capacity();
  const auto* incomingCaptures = captures.data(); const auto* incomingCompanions = companions.data();
  const auto captureCapacity = captures.capacity(); const auto companionCapacity = companions.capacity();
  const auto expectedCaptures = captures; const auto expectedCompanions = companions;
  const auto coverage = cursor.coveredPassRevision();
  const auto generation = PendingSnapshotRetirementTestAccess::generation(loop);
  TEST_ASSERT_EQUAL(Result::Adopted, loop.adoptCommittedSnapshotInventory(cursor, captures, companions, mode));
  TEST_ASSERT_EQUAL_PTR(box, observe(loop));
  TEST_ASSERT_EQUAL_PTR(incomingCaptures, box->queuedPassIds.data()); TEST_ASSERT_EQUAL_PTR(incomingCompanions, box->companionIds.data());
  TEST_ASSERT_EQUAL_PTR(oldCaptures, captures.data()); TEST_ASSERT_EQUAL_PTR(oldCompanions, companions.data());
  TEST_ASSERT_EQUAL_UINT64(captureCapacity, box->queuedPassIds.capacity()); TEST_ASSERT_EQUAL_UINT64(companionCapacity, box->companionIds.capacity());
  TEST_ASSERT_EQUAL_UINT64(oldCaptureCapacity, captures.capacity()); TEST_ASSERT_EQUAL_UINT64(oldCompanionCapacity, companions.capacity());
  TEST_ASSERT_TRUE(expectedCaptures == box->queuedPassIds && expectedCompanions == box->companionIds);
  TEST_ASSERT_TRUE(captures.empty() && companions.empty());
  TEST_ASSERT_EQUAL_UINT64(generation + 1, PendingSnapshotRetirementTestAccess::generation(loop));
  TEST_ASSERT_EQUAL_UINT32(coverage, box->coveredPassRevision);
  TEST_ASSERT_TRUE(box->sessionExitQueued); TEST_ASSERT_EQUAL(CommitReason::RecordStop, box->sessionExitReason);
  TEST_ASSERT_EQUAL(mode == Mode::Loaded, box->fromLoadedPasses);
  TEST_ASSERT_EQUAL(mode == Mode::Loaded ? LoadedSnapshotHydrationPhase::Queued : LoadedSnapshotHydrationPhase::None, box->loadedHydrationPhase);
  TEST_ASSERT_EQUAL(expectedCompanions.empty(), box->companionsApplied);
  TEST_ASSERT_EQUAL(Source::Idle, loop.committedSnapshotInventoryPreparationResult(cursor, captures, companions));
  refuse(loop, cursor, captures, companions, Result::SourceNotReady, mode);
}
}
void test_adoption_queries_header_grant_and_consumed_swap() {
  using namespace inventory_adoption_test;
  Loop loop; record(loop); Cursor cursor; std::vector<PassId> captures; EditPassIdList companions;
  complete(loop, cursor, captures, companions);
  for (unsigned i = 0; i < 4; ++i) {
    TEST_ASSERT_EQUAL(Destination::NeedsHeader, loop.pendingSnapshotInventoryDestinationResult());
    TEST_ASSERT_NULL(observe(loop));
    refuse(loop, cursor, captures, companions, Result::NeedsHeader);
  }
  TEST_ASSERT_EQUAL(Destination::Ready, loop.prepareFreshPendingSnapshotInventoryDestination());
  TEST_ASSERT_EQUAL(Destination::Ready, loop.prepareFreshPendingSnapshotInventoryDestination());
  TEST_ASSERT_EQUAL_UINT32(1, captures.size()); TEST_ASSERT_EQUAL_UINT32(1, captures[0]);
  adopted(loop, cursor, captures, companions, Mode::Loaded);
  publish(loop);
  const auto membership = loop.publishedSnapshot()->noteMembership(60, 0);
  TEST_ASSERT_NOT_NULL(membership);
  TEST_ASSERT_EQUAL_UINT32(1, membership->references[0]->noteId);
  TEST_ASSERT_EQUAL_UINT32(10, membership->references[0]->startTick);
}
void test_adoption_partial_foreign_fault_and_stale_receipts() {
  using namespace inventory_adoption_test;
  for (unsigned kind = 0; kind < 10; ++kind) {
    Loop loop, foreign; record(loop); Cursor cursor; std::vector<PassId> captures; EditPassIdList companions;
    TEST_ASSERT_EQUAL(Destination::Ready, loop.prepareFreshPendingSnapshotInventoryDestination());
    if (kind == 0) {
      loop.beginCommittedSnapshotInventoryPreparation(cursor, captures, companions);
      refuse(loop, cursor, captures, companions, Result::SourceNotReady); continue;
    }
    if (kind == 1) {
      captures.push_back(9); loop.beginCommittedSnapshotInventoryPreparation(cursor, captures, companions);
      refuse(loop, cursor, captures, companions, Result::SourceFault); continue;
    }
    complete(loop, cursor, captures, companions);
    if (kind == 2) { refuse(foreign, cursor, captures, companions, Result::SourceSuperseded); continue; }
    if (kind == 3) ++loop.playbackRevision;
    if (kind == 4) ++loop.canonicalStorageGeneration;
    if (kind == 5) ++loop.loopLengthTicks;
    if (kind == 6) loop.passes.overdubPasses.emplace_back();
    if (kind == 7) companions.reserve(30);
    if (kind == 8) { std::vector<PassId> other = captures; refuse(loop, cursor, other, companions, Result::SourceSuperseded); continue; }
    if (kind == 9) { EditPassIdList other = companions; refuse(loop, cursor, captures, other, Result::SourceSuperseded); continue; }
    refuse(loop, cursor, captures, companions, Result::SourceSuperseded);
  }
}
void test_adoption_empty_invalid_mode_and_zero_geometry() {
  using namespace inventory_adoption_test;
  Loop loop; Cursor cursor; std::vector<PassId> captures; EditPassIdList companions;
  TEST_ASSERT_EQUAL(Destination::Ready, loop.prepareFreshPendingSnapshotInventoryDestination());
  complete(loop, cursor, captures, companions); refuse(loop, cursor, captures, companions, Result::EmptyInventory);
  record(loop); complete(loop, cursor, captures, companions);
  refuse(loop, cursor, captures, companions, Result::InvalidMode, static_cast<Mode>(255));
  cursor.discard(); captures.clear(); loop.loopLengthTicks = 0;
  complete(loop, cursor, captures, companions); refuse(loop, cursor, captures, companions, Result::InvalidGeometry);
}
void test_adoption_loaded_root_refusal_republish_preserves_stale_root_and_parity() {
  using namespace inventory_adoption_test;
  Loop loop; record(loop); TEST_ASSERT_TRUE(loop.queueLoadedSnapshotPublish()); publish(loop);
  const auto* root = loop.publishedSnapshot().get(); const auto revision = loop.publishedSnapshotRevision();
  const auto coverage = loop.publishedSnapshotCoveredRevision();
  TEST_ASSERT_EQUAL_UINT32(2, companion(loop, false));
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_TRUE(loop.prewarmPreparedCompanionHistory(4)); loop.clearPendingSnapshotPublishForTest();
  // Snew + C is deliberately not granted by that prewarm. Requeue then reserve the real horizon.
  TEST_ASSERT_TRUE(loop.requestCommittedPassesRepublish()); TEST_ASSERT_TRUE(loop.prewarmPreparedCompanionHistory(4)); loop.clearPendingSnapshotPublishForTest();
  Cursor cursor; std::vector<PassId> captures; EditPassIdList companions; companions.reserve(5);
  complete(loop, cursor, captures, companions);
  TEST_ASSERT_EQUAL_UINT32(1, captures[0]); TEST_ASSERT_EQUAL_UINT32(2, companions[0]);
  refuse(loop, cursor, captures, companions, Result::DestinationBusy, Mode::Loaded);
  adopted(loop, cursor, captures, companions, Mode::Republish);
  TEST_ASSERT_EQUAL_PTR(root, loop.publishedSnapshot().get()); TEST_ASSERT_EQUAL_UINT32(revision, loop.publishedSnapshotRevision());
  TEST_ASSERT_EQUAL_UINT32(coverage, loop.publishedSnapshotCoveredRevision());
  publish(loop); auto membership = loop.publishedSnapshot()->noteMembership(60, 0);
  TEST_ASSERT_NOT_NULL(membership); TEST_ASSERT_EQUAL_UINT32(70, membership->references[0]->endTick);
  TEST_ASSERT_TRUE(loop.requestCommittedPassesRepublish()); publish(loop);
  membership = loop.publishedSnapshot()->noteMembership(60, 0);
  TEST_ASSERT_EQUAL_UINT32(10, membership->references[0]->startTick); TEST_ASSERT_EQUAL_UINT32(70, membership->references[0]->endTick);
}
void test_adoption_reservation_shortfall_exact_and_full_horizon() {
  using namespace inventory_adoption_test;
  for (unsigned kind = 0; kind < 3; ++kind) {
    Loop loop; reserved(loop, 4, kind != 0);
    const auto rowLimit = loop.preparedCompanionHistory_.rowLimit; const auto snapshotLimit = loop.preparedCompanionHistory_.snapshotLimit;
    const auto* workIds = loop.preparedCompanionHistory_.snapshotIds.data();
    Cursor cursor; std::vector<PassId> captures; EditPassIdList companions;
    companions.reserve(kind == 1 ? 1 : 5); complete(loop, cursor, captures, companions);
    if (kind != 2) { refuse(loop, cursor, captures, companions, Result::NeedsCompanionCapacity); continue; }
    TEST_ASSERT_EQUAL_UINT32(1, captures[0]); TEST_ASSERT_EQUAL_UINT32(2, companions[0]);
    adopted(loop, cursor, captures, companions, Mode::Republish);
    TEST_ASSERT_EQUAL_UINT64(4, loop.preparedCompanionHistory_.companionCapacity);
    TEST_ASSERT_EQUAL_UINT64(rowLimit, loop.preparedCompanionHistory_.rowLimit); TEST_ASSERT_EQUAL_UINT64(snapshotLimit, loop.preparedCompanionHistory_.snapshotLimit);
    TEST_ASSERT_EQUAL_PTR(workIds, loop.preparedCompanionHistory_.snapshotIds.data());
    publish(loop);
  }
}
void test_adoption_live_staged_undo_and_queued_destinations_refuse() {
  using namespace inventory_adoption_test;
  for (unsigned kind = 0; kind < 5; ++kind) {
    Loop loop; record(loop); Cursor cursor; std::vector<PassId> captures; EditPassIdList companions;
    TEST_ASSERT_EQUAL(Destination::Ready, loop.prepareFreshPendingSnapshotInventoryDestination());
    complete(loop, cursor, captures, companions);
    if (kind == 0) loop.beginCapture(CapturePhase::Overdub);
    if (kind == 1) { loop.beginCapture(CapturePhase::Overdub); loop.appendCaptureEvent(MidiEvent::NoteOn(50, 1, 64, 90)); loop.sealCapture(100, CommitReason::OverdubStop); }
    if (kind == 2) { TEST_ASSERT_TRUE(loop.requestCommittedPassesRepublish()); loop.setPendingSessionExitUndoAttachEntryId(12); }
    if (kind == 3) TEST_ASSERT_TRUE(loop.requestCommittedPassesRepublish());
    if (kind == 4) loop.openOverdubSession(0);
    TEST_ASSERT_EQUAL(Destination::Busy, loop.pendingSnapshotInventoryDestinationResult());
    TEST_ASSERT_EQUAL(Destination::Busy, loop.prepareFreshPendingSnapshotInventoryDestination());
    refuse(loop, cursor, captures, companions, Result::DestinationBusy);
  }
}
void test_adoption_retirement_progress_and_complete_are_never_fresh() {
  using namespace inventory_adoption_test;
  Loop loop; reserved(loop, 4); Cursor cursor; std::vector<PassId> captures; EditPassIdList companions; companions.reserve(5);
  complete(loop, cursor, captures, companions);
  auto& owner = pending_box_test::box(loop); PendingSnapshotPublishBox::PayloadReleaseCursor retirement;
  TEST_ASSERT_EQUAL(PendingSnapshotPublishBox::PayloadReleaseResult::Progress,
      owner.beginPayloadRelease(retirement, PendingSnapshotPublishBox::CompanionStoragePolicy::Retain));
  TEST_ASSERT_EQUAL(Destination::Busy, loop.pendingSnapshotInventoryDestinationResult());
  refuse(loop, cursor, captures, companions, Result::DestinationBusy);
  pending_box_test::drain(owner, retirement);
  TEST_ASSERT_EQUAL(Destination::Busy, loop.prepareFreshPendingSnapshotInventoryDestination());
  refuse(loop, cursor, captures, companions, Result::DestinationBusy);
}
#if defined(INVENTORY_ADOPTION_METADATA_HARNESS)
void test_adoption_imported_metadata_order_is_observation_only() {
  using namespace inventory_adoption_test;
  Loop loop; seedInventory(loop); Cursor cursor; std::vector<PassId> captures; EditPassIdList companions;
  complete(loop, cursor, captures, companions); assertInventoryIds(captures, companions);
  TEST_ASSERT_EQUAL(Destination::Ready, loop.prepareFreshPendingSnapshotInventoryDestination());
  adopted(loop, cursor, captures, companions, Mode::Loaded);
  // Do not publish imported chunk descriptors: this is solely ordering observation.
  assertInventoryIds(observe(loop)->queuedPassIds, observe(loop)->companionIds);
}
#endif

void test_adoption_both_retained_invalid_batches_refuse() {
  using namespace inventory_adoption_test;
  Loop loop; loop.loopLengthTicks = Config::TICKS_PER_BAR;
  loop.beginCapture(CapturePhase::Record);
  for (unsigned i = 0; i < 80; ++i) TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::ControlChange(i * 4, 1, 7, i)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(Config::TICKS_PER_BAR, CommitReason::RecordStop));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass()); loop.discardCapture();
  TEST_ASSERT_TRUE(loop.queueLoadedSnapshotPublish());
  for (unsigned i = 0; i < 10000; ++i) {
    loop.stepPendingSnapshotPublish(1);
    const auto* box = observe(loop); TEST_ASSERT_NOT_NULL(box);
    if (!box->fragmentProgressValid && !box->passChangeProgress.batch.empty() &&
        !box->fragmentProgress.batch.empty() && box->unpublishedCandidate) break;
  }
  const auto* box = observe(loop);
  TEST_ASSERT_FALSE(box->fragmentProgressValid);
  TEST_ASSERT_EQUAL_UINT64(32, box->passChangeProgress.batch.size());
  TEST_ASSERT_EQUAL_UINT64(32, box->fragmentProgress.batch.size());
  Cursor cursor; std::vector<PassId> captures; EditPassIdList companions; complete(loop, cursor, captures, companions);
  TEST_ASSERT_EQUAL(Destination::Busy, loop.prepareFreshPendingSnapshotInventoryDestination());
  refuse(loop, cursor, captures, companions, Result::DestinationBusy);
}

#if defined(INVENTORY_ADOPTION_WORKER_HARNESS)
void test_adoption_prepared_real_ready_committed_recycle_owner_refuse() {
  using namespace inventory_adoption_test;
  Loop loop; reserved(loop, 4);
  CanonicalPrefixFixture f; prepareUnsortedFrozenTailCapture(f.runtime);
  auto& capture = f.runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
  auto& rows = f.runtime.frozenCaptureCompanions;
  PendingNoteChangeVec changes{{PendingNoteChangeKind::Hide, 2, 1, 62, 80, 20, 60}};
  TEST_ASSERT_TRUE(rows.begin(capture, 4, changes));
  for (unsigned i = 0; rows.step(1) == RuntimePlaybackOrderBuildResult::Pending && i < 32; ++i) {}
  TEST_ASSERT_TRUE(rows.ready(capture));
  TEST_ASSERT_TRUE(loop.beginPreparedCompanionHistory(rows, capture, nullptr, 71));
  for (unsigned i = 0; !loop.preparedCompanionHistoryReady(71) && i < 32; ++i) TEST_ASSERT_TRUE(loop.stepPreparedCompanionHistory(1, 71));
  TEST_ASSERT_TRUE(loop.preparedCompanionHistoryReady(71));
  Cursor cursor; std::vector<PassId> captures; EditPassIdList companions; companions.reserve(8); complete(loop, cursor, captures, companions);
  TEST_ASSERT_EQUAL(Destination::Busy, loop.pendingSnapshotInventoryDestinationResult());
  refuse(loop, cursor, captures, companions, Result::DestinationBusy);
  TEST_ASSERT_TRUE(loop.commitPreparedCompanionHistoryAtomic(71));
  cursor.discard(); captures.clear(); companions.clear(); complete(loop, cursor, captures, companions);
  TEST_ASSERT_EQUAL(Destination::Busy, loop.pendingSnapshotInventoryDestinationResult());
  refuse(loop, cursor, captures, companions, Result::DestinationBusy);
  TEST_ASSERT_TRUE(loop.beginRecyclePreparedCompanionHistory(71));
  TEST_ASSERT_EQUAL(Destination::Busy, loop.pendingSnapshotInventoryDestinationResult());
  refuse(loop, cursor, captures, companions, Result::DestinationBusy);
}
void test_adoption_preserved_full_grant_future_worker_begin_commit() {
  using namespace inventory_adoption_test;
  Loop loop; reserved(loop, 4);
  Cursor cursor; std::vector<PassId> captures; EditPassIdList companions; companions.reserve(5); complete(loop, cursor, captures, companions);
  adopted(loop, cursor, captures, companions, Mode::Republish);
  // Normal publisher cleanup settles the adopted request before a future interval.
  publish(loop);
  TEST_ASSERT_EQUAL_UINT64(5, observe(loop)->companionIds.capacity());
  TEST_ASSERT_EQUAL_UINT64(5, loop.preparedCompanionHistory_.snapshotIds.capacity());
  CanonicalPrefixFixture f; prepareUnsortedFrozenTailCapture(f.runtime);
  auto& capture = f.runtime.viewSetIfPrewarmed()->frozenCaptureStaging();
  auto& rows = f.runtime.frozenCaptureCompanions;
  PendingNoteChangeVec changes{{PendingNoteChangeKind::Hide, 1, 1, 60, 90, 10, 70},
      {PendingNoteChangeKind::Hide, 2, 1, 61, 90, 20, 80},
      {PendingNoteChangeKind::Hide, 3, 1, 62, 90, 30, 90},
      {PendingNoteChangeKind::Hide, 4, 1, 63, 90, 40, 100}};
  TEST_ASSERT_TRUE(rows.begin(capture, 4, changes));
  for (unsigned i = 0; rows.step(1) == RuntimePlaybackOrderBuildResult::Pending && i < 64; ++i) {}
  TEST_ASSERT_TRUE(rows.ready(capture)); TEST_ASSERT_EQUAL_UINT64(4, rows.size());
  TEST_ASSERT_TRUE(loop.beginPreparedCompanionHistory(rows, capture));
  for (unsigned i = 0; !loop.preparedCompanionHistoryReady() && i < 64; ++i) TEST_ASSERT_TRUE(loop.stepPreparedCompanionHistory(1));
  TEST_ASSERT_TRUE(loop.preparedCompanionHistoryReady()); TEST_ASSERT_TRUE(loop.commitPreparedCompanionHistoryAtomic());
  TEST_ASSERT_EQUAL_UINT64(5, loop.passes.editPasses.size());
  TEST_ASSERT_EQUAL_UINT64(4, observe(loop)->companionIds.size());
  TEST_ASSERT_TRUE(observe(loop)->companionIds.capacity() >= 5);
  TEST_ASSERT_TRUE(loop.preparedCompanionHistory_.snapshotIds.capacity() >= 5);
  TEST_ASSERT_EQUAL_UINT64(4, loop.preparedCompanionHistory_.companionCapacity);
}
#endif

#if defined(INVENTORY_ADOPTION_METADATA_HARNESS)
void test_adoption_real_wrap_journal_refuses_without_mutation() {
  using namespace inventory_adoption_test;
  Loop loop; record(loop); TEST_ASSERT_TRUE(loop.queueLoadedSnapshotPublish()); publish(loop);
  NoteUtils::DisplayNoteVec source;
  TEST_ASSERT_TRUE(loop.tryCopyCurrentSnapshotDisplayNotesInWindow(source, 0, Config::TICKS_PER_BAR));
  TEST_ASSERT_TRUE(loop.prewarmPreparedCompanionHistory(4));
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(1, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(70, 1, 60)));
  loop.accumulatePendingNoteChangesFromSourceNotes(source, 1, 60, 100, 1, 70, loop.allocateNoteId());
  TEST_ASSERT_TRUE(loop.hasPendingNoteChanges());
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubWrap, Config::TICKS_PER_BAR));
  TEST_ASSERT_TRUE(loop.queueOverdubWrapSnapshotJournal(loop.lastCommittedPassId()));
  const auto ids = loop.sealPendingNoteChangesToEditPasses(false);
  TEST_ASSERT_EQUAL_UINT64(1, ids.size());
  loop.discardCapture();
  TEST_ASSERT_EQUAL_UINT64(1, observe(loop)->queuedWrapJournals.size());
  Cursor cursor; std::vector<PassId> captures; EditPassIdList companions; companions.reserve(8); complete(loop, cursor, captures, companions);
  TEST_ASSERT_EQUAL(Destination::Busy, loop.prepareFreshPendingSnapshotInventoryDestination());
  refuse(loop, cursor, captures, companions, Result::DestinationBusy);
}
void test_adoption_delete_companion_literal_inventory_and_publisher_parity() {
  using namespace inventory_adoption_test;
  Loop loop; record(loop); TEST_ASSERT_TRUE(loop.queueLoadedSnapshotPublish()); publish(loop);
  TEST_ASSERT_EQUAL_UINT32(2, companion(loop, true));
  TEST_ASSERT_TRUE(loop.requestCommittedPassesRepublish());
  TEST_ASSERT_TRUE(loop.prewarmPreparedCompanionHistory(4)); loop.clearPendingSnapshotPublishForTest();
  Cursor cursor; std::vector<PassId> captures; EditPassIdList companions; companions.reserve(5); complete(loop, cursor, captures, companions);
  TEST_ASSERT_EQUAL_UINT32(1, captures[0]); TEST_ASSERT_EQUAL_UINT32(2, companions[0]);
  adopted(loop, cursor, captures, companions, Mode::Republish); publish(loop);
  const auto notes = loop.publishedSnapshot()->noteMembership(60, 0);
  TEST_ASSERT_TRUE(!notes || !notes->references[0]);
  // Independent legacy producer/queue, not adoption output as its own oracle.
  Loop legacy; record(legacy); TEST_ASSERT_TRUE(legacy.queueLoadedSnapshotPublish()); publish(legacy);
  TEST_ASSERT_EQUAL_UINT32(2, companion(legacy, true));
  TEST_ASSERT_TRUE(legacy.requestCommittedPassesRepublish()); publish(legacy);
  const auto expected = legacy.publishedSnapshot()->noteMembership(60, 0);
  TEST_ASSERT_TRUE(!expected || !expected->references[0]);
}
#endif

void test_adoption_real_borrowers_withhold_header_and_adoption() {
  using namespace inventory_adoption_test;
  Loop loop; record(loop); Cursor cursor; std::vector<PassId> captures; EditPassIdList companions;
  complete(loop, cursor, captures, companions);
  PendingNoteChangeVec changes;
  CapturePreview preview;
  const int owner = 1;
  TEST_ASSERT_TRUE(loop.borrowPendingNoteChanges(changes, &owner));
  TEST_ASSERT_EQUAL(Destination::Busy, loop.prepareFreshPendingSnapshotInventoryDestination());
  TEST_ASSERT_NULL(observe(loop));
  refuse(loop, cursor, captures, companions, Result::DestinationBusy);
  TEST_ASSERT_TRUE(loop.releasePendingNoteChangesBorrow(&owner));
  TEST_ASSERT_EQUAL(Destination::Ready, loop.prepareFreshPendingSnapshotInventoryDestination());
  TEST_ASSERT_TRUE(loop.borrowCapturePreview(preview, &owner));
  refuse(loop, cursor, captures, companions, Result::DestinationBusy);
  TEST_ASSERT_TRUE(loop.releaseCapturePreviewBorrow(&owner));
  TEST_ASSERT_EQUAL(Destination::Ready, loop.pendingSnapshotInventoryDestinationResult());
}
void test_adoption_real_prewarm_history_staleness_preserves_grant() {
  using namespace inventory_adoption_test;
  Loop loop; reserved(loop, 4);
  TEST_ASSERT_EQUAL_UINT32(3, companion(loop, true));
  loop.clearPendingSnapshotPublishForTest();
  Cursor cursor; std::vector<PassId> captures; EditPassIdList companions; companions.reserve(6);
  complete(loop, cursor, captures, companions);
  TEST_ASSERT_EQUAL(Destination::Ready, loop.pendingSnapshotInventoryDestinationResult());
  refuse(loop, cursor, captures, companions, Result::NeedsCompanionCapacity);
  TEST_ASSERT_EQUAL_UINT64(4, loop.preparedCompanionHistory_.companionCapacity);
  TEST_ASSERT_EQUAL_UINT64(5, loop.preparedCompanionHistory_.snapshotLimit);
}
