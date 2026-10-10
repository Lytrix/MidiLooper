// Real Loop producers; no payload injection. Caller excludes publisher/readers for retirement.
struct PendingSnapshotRetirementTestAccess {
  static const PendingSnapshotPublishBox* observe(const Loop& loop) {
    return loop.pendingSnapshotPublishBox_.get();
  }
  static uint64_t generation(const Loop& loop) { return loop.pendingSnapshotCompanionGeneration_; }
};
namespace pending_box_test {
using Box = PendingSnapshotPublishBox;
using Result = Box::PayloadReleaseResult;
using Policy = Box::CompanionStoragePolicy;
Box& box(Loop& loop) {
  auto* observed = PendingSnapshotRetirementTestAccess::observe(loop);
  TEST_ASSERT_NOT_NULL(observed);
  return *const_cast<Box*>(observed); // Mutate only through the owner retirement API.
}
void commit(Loop& loop, unsigned notes = 2) {
  loop.loopLengthTicks = Config::TICKS_PER_BAR;
  loop.beginCapture(CapturePhase::Record);
  for (unsigned i = 0; i < notes; ++i) {
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(10 + i * 20, 1, 60 + i, 90)));
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(30 + i * 20, 1, 60 + i)));
  }
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::ControlChange(300, 1, 7, 80)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(Config::TICKS_PER_BAR, CommitReason::RecordStop));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass());
  TEST_ASSERT_TRUE(loop.queueLoadedSnapshotPublish());
  loop.setPendingSessionExitUndoAttachEntryId(77);
}
std::vector<uint64_t> metadata(const Box& owner) {
  std::vector<uint64_t> out;
  auto add = [&](auto value) { out.push_back(static_cast<uint64_t>(value)); };
  add(owner.sessionExitQueued); add(owner.coveredPassRevision); add(owner.fromLoadedPasses);
  add(owner.loadedHydrationPhase); add(owner.companionsApplied); add(owner.applyingCompanions);
  add(owner.wrapJournalInFlight); add(owner.sessionExitReason); add(owner.sessionExitUndoAttachEntryId);
  add(owner.fragmentProgressValid); add(owner.consecutiveFragmentFailures); add(owner.fragmentBlocked);
  add(owner.fragmentBlockTransient); add(owner.fragmentParkSawDenial); add(owner.constructionPassId);
  add(owner.appliedLiveRecordEventIndex); add(owner.appliedLiveRecordCompletedTicks);
  for (const auto* progress : {&owner.passChangeProgress, &owner.fragmentProgress}) {
    add(progress->passId); add(progress->cursor.listIndex); add(progress->cursor.chunkIndex);
    add(progress->cursor.chunkEventOffset); add(progress->cursor.complete);
    add(progress->batchIndex); add(progress->nextEventId); add(progress->nextCanonicalOrder);
    add(progress->openNoteCount); add(progress->active); add(progress->fromCaptureStore);
    add(progress->storeWindowComplete); add(progress->nextStoreIndex); add(progress->includeUntil);
    add(progress->windowStartTick); add(progress->windowLengthTicks);
    for (const auto& note : progress->openNotes) {
      add(note.noteId); add(note.pitch); add(note.velocity); add(note.startTick);
      add(note.endTick); add(note.hasStart); add(note.hasEnd);
    }
  }
  return out;
}
size_t drain(Box& owner, Box::PayloadReleaseCursor& cursor) {
  size_t grants = 0;
  const auto beforeMetadata = metadata(owner);
  while (owner.payloadReleaseResult(cursor) == Result::Progress && grants < 20000) {
    const auto before = cursor.completedUnitCount();
    owner.stepPayloadRelease(cursor, 1);
    TEST_ASSERT_EQUAL_UINT64(before + 1, cursor.completedUnitCount());
    owner.persistLiveRecordCursor();
    TEST_ASSERT_TRUE(beforeMetadata == metadata(owner));
    ++grants;
  }
  TEST_ASSERT_EQUAL(Result::Complete, owner.payloadReleaseResult(cursor));
  TEST_ASSERT_FALSE(owner.construction || owner.unpublishedCandidate);
  TEST_ASSERT_TRUE(owner.passChangeProgress.batch.empty() && owner.fragmentProgress.batch.empty());
  TEST_ASSERT_TRUE(owner.queuedPassIds.empty() && owner.appliedPassIds.empty() && owner.journaledPassIds.empty());
  TEST_ASSERT_TRUE(owner.queuedWrapJournals.empty());
  TEST_ASSERT_EQUAL_UINT64(0, owner.passChangeProgress.batch.capacity());
  TEST_ASSERT_EQUAL_UINT64(0, owner.fragmentProgress.batch.capacity());
  return grants;
}
}
void test_pending_box_admission_qualification_and_row_grains() {
  using namespace pending_box_test;
  Loop loop, foreign;
  commit(loop); commit(foreign);
  auto& owner = box(loop);
  Box::PayloadReleaseCursor cursor, overlap;
  const auto id = owner.queuedPassIds.back();
  const auto data = owner.queuedPassIds.data();
  const auto coverage = owner.coveredPassRevision;
  const auto hydration = owner.loadedHydrationPhase;
  TEST_ASSERT_EQUAL(Result::Idle, owner.payloadReleaseResult(cursor));
  TEST_ASSERT_EQUAL(Result::Progress, owner.beginPayloadRelease(cursor, Policy::Release));
  TEST_ASSERT_EQUAL_PTR(data, owner.queuedPassIds.data());
  TEST_ASSERT_EQUAL_UINT32(id, owner.queuedPassIds.back());
  TEST_ASSERT_EQUAL_UINT64(0, cursor.completedUnitCount());
  TEST_ASSERT_EQUAL(Result::Progress, owner.stepPayloadRelease(cursor, 0));
  TEST_ASSERT_EQUAL(Result::Progress, owner.beginPayloadRelease(cursor, Policy::Release));
  TEST_ASSERT_EQUAL(Result::Rejected, owner.beginPayloadRelease(cursor, Policy::Retain));
  TEST_ASSERT_EQUAL(Result::Rejected, owner.beginPayloadRelease(overlap, Policy::Release));
  TEST_ASSERT_EQUAL(Result::Rejected, box(foreign).beginPayloadRelease(cursor, Policy::Release));
  TEST_ASSERT_EQUAL(Result::Rejected, box(foreign).stepPayloadRelease(cursor, 1));
  TEST_ASSERT_EQUAL(Result::Rejected, box(foreign).payloadReleaseResult(cursor));
  TEST_ASSERT_EQUAL(Result::Rejected, owner.stepPayloadRelease(overlap, 1));
  TEST_ASSERT_EQUAL(Result::Progress, owner.payloadReleaseResult(cursor));
  TEST_ASSERT_EQUAL_UINT64(0, cursor.completedUnitCount());
  // Empty construction admission/drain/shell, two empty batches, companion buffer = six turns.
  for (unsigned i = 0; i < 6; ++i) owner.stepPayloadRelease(cursor, 1);
  TEST_ASSERT_EQUAL_UINT64(1, owner.queuedPassIds.size());
  owner.stepPayloadRelease(cursor, 1);
  TEST_ASSERT_TRUE(owner.queuedPassIds.empty());
  TEST_ASSERT_EQUAL_PTR(data, owner.queuedPassIds.data()); // row != buffer retirement
  owner.stepPayloadRelease(cursor, 1);
  TEST_ASSERT_EQUAL_UINT64(0, owner.queuedPassIds.capacity());
  const auto tail = drain(owner, cursor);
  TEST_ASSERT_EQUAL_UINT64(5, tail);
  TEST_ASSERT_EQUAL_UINT64(13, cursor.completedUnitCount());
  TEST_ASSERT_EQUAL_UINT32(coverage, owner.coveredPassRevision);
  TEST_ASSERT_EQUAL(hydration, owner.loadedHydrationPhase);
  TEST_ASSERT_TRUE(owner.sessionExitQueued && owner.fromLoadedPasses);
  TEST_ASSERT_EQUAL(Result::Complete, owner.stepPayloadRelease(cursor, 99));
  TEST_ASSERT_EQUAL(Result::Complete, owner.beginPayloadRelease(cursor, Policy::Release));
  TEST_ASSERT_EQUAL(Result::Rejected, owner.beginPayloadRelease(overlap, Policy::Release));
  TEST_ASSERT_EQUAL_UINT64(13, cursor.completedUnitCount());
  TEST_ASSERT_EQUAL(Result::Rejected, box(foreign).beginPayloadRelease(cursor, Policy::Release));
}
void test_pending_box_real_prewarm_retain_and_empty_release() {
  using namespace pending_box_test;
  Loop retained;
  TEST_ASSERT_TRUE(retained.prewarmPreparedCompanionHistory(4));
  auto& owner = box(retained);
  auto* data = owner.companionIds.data();
  const auto capacity = owner.companionIds.capacity();
  TEST_ASSERT_TRUE(capacity >= 4);
  Box::PayloadReleaseCursor cursor;
  TEST_ASSERT_EQUAL(Result::Progress, owner.beginPayloadRelease(cursor, Policy::Retain));
  TEST_ASSERT_EQUAL_UINT64(12, drain(owner, cursor));
  TEST_ASSERT_EQUAL_PTR(data, owner.companionIds.data());
  TEST_ASSERT_EQUAL_UINT64(capacity, owner.companionIds.capacity());
  TEST_ASSERT_EQUAL(Result::Rejected, owner.beginPayloadRelease(cursor, Policy::Release));
  Loop released;
  TEST_ASSERT_TRUE(released.prewarmPreparedCompanionHistory(4));
  Box::PayloadReleaseCursor second;
  auto& other = box(released);
  other.beginPayloadRelease(second, Policy::Release);
  TEST_ASSERT_EQUAL_UINT64(12, drain(other, second));
  TEST_ASSERT_EQUAL_UINT64(0, other.companionIds.capacity());
}
void test_pending_box_partial_construction_and_progress_batches() {
  using namespace pending_box_test;
  Loop loop;
  commit(loop, 20);
  loop.stepPendingSnapshotPublish(1);
  loop.stepPendingSnapshotPublish(3); // Real partial workspace, not just the constructor.
  auto& owner = box(loop);
  TEST_ASSERT_NOT_NULL(owner.construction.get());
  TEST_ASSERT_EQUAL_UINT64(3, owner.construction->completedUnitCount());
  TEST_ASSERT_TRUE(!owner.fragmentProgress.batch.empty());
  const auto rows = owner.fragmentProgress.batch.size();
  const auto metadata = owner.fragmentProgress.nextEventId;
  Box::PayloadReleaseCursor cursor;
  owner.beginPayloadRelease(cursor, Policy::Release);
  TEST_ASSERT_NOT_NULL(owner.construction.get());
  TEST_ASSERT_EQUAL_UINT64(rows, owner.fragmentProgress.batch.size());
  drain(owner, cursor);
  TEST_ASSERT_EQUAL_UINT32(metadata, owner.fragmentProgress.nextEventId);
  TEST_ASSERT_TRUE(owner.fragmentProgressValid);
}

void test_pending_box_both_invalid_batches_and_exclusive_root() {
  using namespace pending_box_test;
  Loop loop;
  loop.loopLengthTicks = Config::TICKS_PER_BAR;
  loop.beginCapture(CapturePhase::Record);
  for (unsigned i = 0; i < 80; ++i)
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::ControlChange(i * 4, 1, 7, i)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(Config::TICKS_PER_BAR, CommitReason::RecordStop));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass());
  TEST_ASSERT_TRUE(loop.queueLoadedSnapshotPublish());
  for (unsigned i = 0; i < 10000; ++i) {
    loop.stepPendingSnapshotPublish(1);
    const auto* observed = PendingSnapshotRetirementTestAccess::observe(loop);
    TEST_ASSERT_NOT_NULL(observed);
    if (!observed->fragmentProgressValid && !observed->passChangeProgress.batch.empty() &&
        !observed->fragmentProgress.batch.empty() && observed->unpublishedCandidate) break;
  }
  auto& owner = box(loop);
  TEST_ASSERT_FALSE(owner.fragmentProgressValid);
  TEST_ASSERT_EQUAL_UINT64(32, owner.passChangeProgress.batch.size());
  TEST_ASSERT_EQUAL_UINT64(32, owner.fragmentProgress.batch.size());
  TEST_ASSERT_TRUE(owner.unpublishedCandidate && !owner.construction);
  TEST_ASSERT_EQUAL_UINT64(1, owner.unpublishedCandidate.use_count());
  std::weak_ptr<const LoopSnapshot> weak = owner.unpublishedCandidate;
  std::weak_ptr<const LoopSnapshotEvent> leaf;
  {
    auto membership = owner.unpublishedCandidate->eventMembership(0);
    TEST_ASSERT_NOT_NULL(membership.get());
    leaf = membership->references[0];
  }
  const auto eventId = owner.passChangeProgress.nextEventId;
  const auto batchIndex = owner.passChangeProgress.batchIndex;
  const auto applied = owner.appliedLiveRecordEventIndex;
  Box::PayloadReleaseCursor cursor;
  owner.beginPayloadRelease(cursor, Policy::Release);
  for (unsigned i = 0; i < 3; ++i) owner.stepPayloadRelease(cursor, 1);
  const auto* data = owner.passChangeProgress.batch.data();
  for (unsigned remaining = 32; remaining > 0; --remaining) {
    TEST_ASSERT_EQUAL_UINT64(remaining, owner.passChangeProgress.batch.size());
    owner.stepPayloadRelease(cursor, 1);
    TEST_ASSERT_EQUAL_UINT64(remaining - 1, owner.passChangeProgress.batch.size());
    TEST_ASSERT_EQUAL_PTR(data, owner.passChangeProgress.batch.data());
    TEST_ASSERT_EQUAL_UINT64(32, owner.fragmentProgress.batch.size());
  }
  owner.stepPayloadRelease(cursor, 1); // separate authoritative allocation
  TEST_ASSERT_EQUAL_UINT64(0, owner.passChangeProgress.batch.capacity());
  data = owner.fragmentProgress.batch.data();
  for (unsigned remaining = 32; remaining > 0; --remaining) {
    owner.stepPayloadRelease(cursor, 1);
    TEST_ASSERT_EQUAL_UINT64(remaining - 1, owner.fragmentProgress.batch.size());
    TEST_ASSERT_EQUAL_PTR(data, owner.fragmentProgress.batch.data());
  }
  owner.stepPayloadRelease(cursor, 1);
  TEST_ASSERT_EQUAL_UINT64(0, owner.fragmentProgress.batch.capacity());
  TEST_ASSERT_FALSE(weak.expired());
  drain(owner, cursor);
  // Empty span array129 + two event membership pages34 + null tail1 +
  // other bar slots31 + bar shell1 + event shell1 + immutable shell1 =198.
  // Box12 + two batches64 + graph extra197 =273 (source ID already dequeued).
  TEST_ASSERT_EQUAL_UINT64(273, cursor.completedUnitCount());
  TEST_ASSERT_TRUE(weak.expired() && leaf.expired());
  TEST_ASSERT_EQUAL_UINT32(eventId, owner.passChangeProgress.nextEventId);
  TEST_ASSERT_EQUAL_UINT64(batchIndex, owner.passChangeProgress.batchIndex);
  owner.persistLiveRecordCursor();
  TEST_ASSERT_EQUAL_UINT64(applied, owner.appliedLiveRecordEventIndex);
  TEST_ASSERT_FALSE(owner.fragmentProgressValid);
}

void test_pending_box_failed_child_and_shared_republish_parent() {
  using namespace pending_box_test;
  Loop loop;
  commit(loop);
  for (unsigned i = 0; loop.hasPendingSnapshotPublish() && i < 10000; ++i)
    loop.stepPendingSnapshotPublish(1);
  auto root = loop.publishedSnapshot();
  TEST_ASSERT_NOT_NULL(root.get());
  auto note = root->noteMembership(60, 0)->references[0];
  auto event = root->eventMembership(0)->references[0];
  const auto revision = loop.publishedSnapshotRevision();
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(400, 1, 70, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(450, 1, 70)));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubWrap, Config::TICKS_PER_BAR));
  TEST_ASSERT_TRUE(loop.requestCommittedPassesRepublish());
  for (unsigned i = 0; i < 1000 && !box(loop).construction; ++i) loop.stepPendingSnapshotPublish(1);
  auto& owner = box(loop);
  TEST_ASSERT_NOT_NULL(owner.construction.get());
  owner.construction->failAfterUnitsForTest(0);
  owner.construction->step(1);
  TEST_ASSERT_TRUE(owner.construction->failed());
  const auto* construction = owner.construction.get();
  Box::PayloadReleaseCursor cursor;
  owner.beginPayloadRelease(cursor, Policy::Release);
  TEST_ASSERT_EQUAL_PTR(construction, owner.construction.get());
  owner.stepPayloadRelease(cursor, 1); // child scalar admission retains original instance
  TEST_ASSERT_EQUAL_PTR(construction, owner.construction.get());
  TEST_ASSERT_FALSE(owner.construction->freeze());
  drain(owner, cursor);
  TEST_ASSERT_EQUAL_PTR(root.get(), loop.publishedSnapshot().get());
  TEST_ASSERT_EQUAL_UINT32(revision, loop.publishedSnapshotRevision());
  TEST_ASSERT_EQUAL_PTR(note.get(), root->noteMembership(60, 0)->references[0].get());
  TEST_ASSERT_EQUAL_PTR(event.get(), root->eventMembership(0)->references[0].get());
}


void test_pending_box_live_capture_frontiers_remain_metadata() {
  using namespace pending_box_test;
  Loop loop;
  loop.loopLengthTicks = Config::TICKS_PER_BAR * 4;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(100, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.stepPendingSnapshotPublish(1, true, 0, Config::TICKS_PER_BAR));
  auto& owner = box(loop);
  TEST_ASSERT_TRUE(owner.fragmentProgress.fromCaptureStore);
  const auto before = metadata(owner);
  Box::PayloadReleaseCursor cursor;
  owner.beginPayloadRelease(cursor, Policy::Release);
  drain(owner, cursor);
  TEST_ASSERT_TRUE(before == metadata(owner));
  TEST_ASSERT_TRUE(loop.captureActive()); // No cancellation or canonical release.
}

#if defined(PENDING_BOX_WRAP_HARNESS)
void test_pending_box_real_wrap_edges_shells_and_companion_rows() {
  using namespace pending_box_test;
  Loop loop;
  commit(loop);
  for (unsigned i = 0; loop.hasPendingSnapshotPublish() && i < 10000; ++i)
    loop.stepPendingSnapshotPublish(1);
  auto root = loop.publishedSnapshot();
  TEST_ASSERT_NOT_NULL(root.get());
  NoteUtils::DisplayNoteVec source;
  TEST_ASSERT_TRUE(loop.tryCopyCurrentSnapshotDisplayNotesInWindow(source, 0, Config::TICKS_PER_BAR));
  TEST_ASSERT_EQUAL_UINT64(2, source.size());
  TEST_ASSERT_TRUE(loop.prewarmPreparedCompanionHistory(4));
  // Two real overlap transformations and real wrap commits create two nested rows.
  for (unsigned i = 0; i < 2; ++i) {
    loop.beginCapture(CapturePhase::Overdub);
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(1, 1, 60 + i, 100)));
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(70 + i * 100, 1, 60 + i)));
    loop.accumulatePendingNoteChangesFromSourceNotes(source, 1, 60 + i, 100,
        1, 70 + i * 100, loop.allocateNoteId());
    TEST_ASSERT_TRUE(loop.hasPendingNoteChanges());
    TEST_ASSERT_EQUAL(CommitResult::Committed,
        loop.commitCapturePass(CommitReason::OverdubWrap, Config::TICKS_PER_BAR));
    TEST_ASSERT_TRUE(loop.queueOverdubWrapSnapshotJournal(loop.lastCommittedPassId()));
    auto ids = loop.sealPendingNoteChangesToEditPasses(false);
    TEST_ASSERT_EQUAL_UINT64(1, ids.size());
  }
  auto& owner = box(loop);
  TEST_ASSERT_EQUAL_UINT64(2, owner.queuedWrapJournals.size());
  TEST_ASSERT_EQUAL_UINT64(2, owner.journaledPassIds.size());
  TEST_ASSERT_EQUAL_UINT64(2, owner.companionIds.size());
  TEST_ASSERT_EQUAL_UINT64(1, owner.queuedWrapJournals.back().changes.notes.size());
  TEST_ASSERT_EQUAL_UINT64(2, owner.queuedWrapJournals.back().changes.events.size());
  TEST_ASSERT_TRUE(owner.queuedWrapJournals.back().changes.notes.back().previous);
  TEST_ASSERT_TRUE(owner.queuedWrapJournals.back().changes.events.back().previous);
  const auto coverage = owner.coveredPassRevision;
  const auto* allocation = owner.companionIds.data();
  const auto capacity = owner.companionIds.capacity();
  Box::PayloadReleaseCursor cursor;
  owner.beginPayloadRelease(cursor, Policy::Retain);
  // 3 construction + 2 batches + 3 companion turns + queued/applied buffers +
  // 2 journal IDs and buffer = 13 grants before the first nested note edge.
  for (unsigned i = 0; i < 13; ++i) owner.stepPayloadRelease(cursor, 1);
  auto& changes = owner.queuedWrapJournals.back().changes;
  TEST_ASSERT_TRUE(changes.notes.back().previous);
  owner.stepPayloadRelease(cursor, 1); // retained previous note edge
  TEST_ASSERT_FALSE(changes.notes.back().previous);
  TEST_ASSERT_EQUAL_UINT64(1, changes.notes.size());
  owner.stepPayloadRelease(cursor, 1); // row, separately
  TEST_ASSERT_TRUE(changes.notes.empty());
  TEST_ASSERT_TRUE(changes.notes.capacity() > 0);
  owner.stepPayloadRelease(cursor, 1); // empty nested note buffer
  TEST_ASSERT_EQUAL_UINT64(0, changes.notes.capacity());
  for (unsigned remaining = 2; remaining > 0; --remaining) {
    owner.stepPayloadRelease(cursor, 1);
    TEST_ASSERT_FALSE(changes.events.back().previous);
    TEST_ASSERT_EQUAL_UINT64(remaining, changes.events.size());
    owner.stepPayloadRelease(cursor, 1);
    TEST_ASSERT_EQUAL_UINT64(remaining - 1, changes.events.size());
  }
  owner.stepPayloadRelease(cursor, 1); // empty nested event allocation
  TEST_ASSERT_EQUAL_UINT64(0, changes.events.capacity());
  TEST_ASSERT_NOT_NULL(owner.queuedWrapJournals.back().parent.get());
  owner.stepPayloadRelease(cursor, 1); // shared in-place parent edge
  TEST_ASSERT_FALSE(owner.queuedWrapJournals.back().parent);
  TEST_ASSERT_EQUAL_UINT64(2, owner.queuedWrapJournals.size());
  owner.stepPayloadRelease(cursor, 1); // empty fragment shell
  TEST_ASSERT_EQUAL_UINT64(1, owner.queuedWrapJournals.size());
  TEST_ASSERT_EQUAL_UINT64(13, drain(owner, cursor));
  TEST_ASSERT_EQUAL_UINT64(36, cursor.completedUnitCount());
  TEST_ASSERT_EQUAL_PTR(allocation, owner.companionIds.data());
  TEST_ASSERT_EQUAL_UINT64(capacity, owner.companionIds.capacity());
  TEST_ASSERT_EQUAL_UINT32(coverage, owner.coveredPassRevision);
  TEST_ASSERT_EQUAL_PTR(root.get(), loop.publishedSnapshot().get());
  TEST_ASSERT_EQUAL_UINT32(10, root->noteMembership(60, 0)->references[0]->startTick);
}
#endif

void test_pending_box_applied_inventory_literal_rows_and_buffer() {
  using namespace pending_box_test;
  Loop loop;
  commit(loop);
  for (unsigned pass = 0; pass < 2; ++pass) {
    loop.beginCapture(CapturePhase::Overdub);
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(400 + pass * 100, 1, 70 + pass, 100)));
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(450 + pass * 100, 1, 70 + pass)));
    TEST_ASSERT_EQUAL(CommitResult::Committed,
        loop.commitCapturePass(CommitReason::OverdubWrap, Config::TICKS_PER_BAR));
  }
  TEST_ASSERT_TRUE(loop.queueLoadedSnapshotPublish());
  for (unsigned i = 0; i < 10000 && box(loop).appliedPassIds.empty(); ++i)
    loop.stepPendingSnapshotPublish(1);
  auto& owner = box(loop);
  TEST_ASSERT_EQUAL_UINT64(1, owner.appliedPassIds.size());
  TEST_ASSERT_EQUAL_UINT64(2, owner.queuedPassIds.size());
  auto* queuedData = owner.queuedPassIds.data();
  auto* appliedData = owner.appliedPassIds.data();
  Box::PayloadReleaseCursor cursor;
  owner.beginPayloadRelease(cursor, Policy::Release);
  // Three construction phases + two batch allocations + companion allocation.
  // First pass complete reset the authoritative batch; transactional retains five values.
  TEST_ASSERT_EQUAL_UINT64(5, owner.fragmentProgress.batch.size());
  for (unsigned i = 0; i < 11; ++i) owner.stepPayloadRelease(cursor, 1);
  TEST_ASSERT_EQUAL_UINT64(2, owner.queuedPassIds.size());
  owner.stepPayloadRelease(cursor, 1);
  TEST_ASSERT_EQUAL_UINT64(1, owner.queuedPassIds.size());
  owner.stepPayloadRelease(cursor, 1);
  TEST_ASSERT_TRUE(owner.queuedPassIds.empty());
  TEST_ASSERT_EQUAL_PTR(queuedData, owner.queuedPassIds.data());
  owner.stepPayloadRelease(cursor, 1);
  TEST_ASSERT_EQUAL_UINT64(0, owner.queuedPassIds.capacity());
  TEST_ASSERT_EQUAL_UINT64(1, owner.appliedPassIds.size());
  owner.stepPayloadRelease(cursor, 1);
  TEST_ASSERT_TRUE(owner.appliedPassIds.empty());
  TEST_ASSERT_EQUAL_PTR(appliedData, owner.appliedPassIds.data());
  owner.stepPayloadRelease(cursor, 1);
  TEST_ASSERT_EQUAL_UINT64(0, owner.appliedPassIds.capacity());
  drain(owner, cursor);
}
