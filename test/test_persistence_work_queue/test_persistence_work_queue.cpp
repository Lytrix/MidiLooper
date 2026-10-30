//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/StorageManager/PersistenceWorkQueue.cpp"

#include "StorageManagerInternal/PersistenceWorkQueue.h"
#include "Utils/PersistenceDiagnostics.h"

namespace {

void resetQueue() { PersistenceWorkQueue::resetForTests(); }

PersistWorkItem loopPersistItem(LoopId loopId) {
  return PersistWorkItem{PersistWorkType::LoopPersist, persistKeyForLoop(loopId)};
}

PersistWorkItem trackMetaItem(uint8_t trackIndex) {
  return PersistWorkItem{PersistWorkType::TrackMeta, persistKeyForTrack(trackIndex)};
}

}  // namespace

// Task 3.3 — a single step is named only when it breaches the active budget or the declared bar. The row
// is report-only: this pins the decision, and the scheduler call sites pass each indivisible step.
void test_persistence_step_is_named_only_on_a_breach() {
  namespace PD = PersistenceDiagnostics;
  TEST_ASSERT_FALSE(PD::persistenceStepBreachExceeded(300, 300));      // at budget, under the bar
  TEST_ASSERT_FALSE(PD::persistenceStepBreachExceeded(3000, 3000));    // at budget, under the bar
  TEST_ASSERT_TRUE(PD::persistenceStepBreachExceeded(301, 300));       // over the active budget
  TEST_ASSERT_TRUE(PD::persistenceStepBreachExceeded(3001, 3000));     // over the active budget
  TEST_ASSERT_FALSE(PD::persistenceStepBreachExceeded(49999, 50000));  // under the declared bar
  TEST_ASSERT_TRUE(PD::persistenceStepBreachExceeded(50001, 50000));   // over the declared bar
  TEST_ASSERT_TRUE(PD::persistenceStepBreachExceeded(213500, 300));    // the measured 213 ms step

  // Cadence: one row per step per 5 s, not per step; a within-budget step never emits.
  uint32_t last[4] = {};
  TEST_ASSERT_FALSE(PD::persistenceStepShouldEmit(100, 300, 1000, last[0]));   // within budget
  TEST_ASSERT_TRUE(PD::persistenceStepShouldEmit(213500, 300, 1000, last[0])); // first breach
  TEST_ASSERT_FALSE(PD::persistenceStepShouldEmit(213500, 300, 2000, last[0])); // same interval
  TEST_ASSERT_FALSE(PD::persistenceStepShouldEmit(213500, 300, 5999, last[0])); // same interval
  TEST_ASSERT_TRUE(PD::persistenceStepShouldEmit(213500, 300, 6000, last[0]));  // next interval
  // A second step has its own interval.
  TEST_ASSERT_TRUE(PD::persistenceStepShouldEmit(51000, 3000, 1000, last[1]));
  TEST_ASSERT_FALSE(PD::persistenceStepShouldEmit(51000, 3000, 2000, last[1]));
}

void test_persist_key_equality() {
  TEST_ASSERT_TRUE(persistKeysEqual(persistKeySingleton(), persistKeySingleton()));
  TEST_ASSERT_TRUE(persistKeysEqual(persistKeyForLoop(3), persistKeyForLoop(3)));
  TEST_ASSERT_FALSE(persistKeysEqual(persistKeyForLoop(3), persistKeyForLoop(4)));
  TEST_ASSERT_TRUE(persistKeysEqual(persistKeyForTrack(2), persistKeyForTrack(2)));
  TEST_ASSERT_FALSE(persistKeysEqual(persistKeyForTrack(2), persistKeyForTrack(3)));
  TEST_ASSERT_TRUE(persistKeysEqual(persistKeyForSlot(1, 4), persistKeyForSlot(1, 4)));
  TEST_ASSERT_FALSE(persistKeysEqual(persistKeyForSlot(1, 4), persistKeyForSlot(1, 5)));
}

void test_admit_dedup_while_queued_or_writing() {
  resetQueue();
  const PersistKey loopKey = persistKeyForLoop(7);

  TEST_ASSERT_TRUE(PersistenceWorkQueue::admitWork(PersistWorkType::LoopPersist, loopKey));
  TEST_ASSERT_FALSE(PersistenceWorkQueue::admitWork(PersistWorkType::LoopPersist, loopKey));
  TEST_ASSERT_EQUAL(1u, PersistenceWorkQueue::queueDepth());
  TEST_ASSERT_EQUAL(static_cast<int>(PersistWorkState::Queued),
                    static_cast<int>(PersistenceWorkQueue::workState(PersistWorkType::LoopPersist, loopKey)));

  PersistWorkItem writingItem{};
  TEST_ASSERT_TRUE(PersistenceWorkQueue::beginWriteQueuedItem(writingItem));
  TEST_ASSERT_FALSE(PersistenceWorkQueue::admitWork(PersistWorkType::LoopPersist, loopKey));
  TEST_ASSERT_EQUAL(static_cast<int>(PersistWorkState::Writing),
                    static_cast<int>(PersistenceWorkQueue::workState(PersistWorkType::LoopPersist, loopKey)));
}

void test_fifo_re_admit_does_not_reorder() {
  resetQueue();

  TEST_ASSERT_TRUE(PersistenceWorkQueue::admitWork(PersistWorkType::LoopPersist, persistKeyForLoop(1)));
  TEST_ASSERT_TRUE(PersistenceWorkQueue::admitWork(PersistWorkType::TrackMeta, persistKeyForTrack(2)));
  TEST_ASSERT_TRUE(PersistenceWorkQueue::admitWork(PersistWorkType::LoopPersist, persistKeyForLoop(3)));
  TEST_ASSERT_FALSE(PersistenceWorkQueue::admitWork(PersistWorkType::LoopPersist, persistKeyForLoop(1)));

  PersistWorkItem queued[3] = {};
  TEST_ASSERT_EQUAL(3u, PersistenceWorkQueue::queuedWorkItems(queued, 3));
  TEST_ASSERT_TRUE(persistWorkItemsEqual(queued[0], loopPersistItem(1)));
  TEST_ASSERT_TRUE(persistWorkItemsEqual(queued[1], trackMetaItem(2)));
  TEST_ASSERT_TRUE(persistWorkItemsEqual(queued[2], loopPersistItem(3)));

  PersistWorkItem drained[3] = {};
  for (size_t i = 0; i < 3; ++i) {
    TEST_ASSERT_TRUE(PersistenceWorkQueue::beginWriteQueuedItem(drained[i]));
    PersistenceWorkQueue::markItemPersisted(drained[i]);
  }
  TEST_ASSERT_TRUE(persistWorkItemsEqual(drained[0], loopPersistItem(1)));
  TEST_ASSERT_TRUE(persistWorkItemsEqual(drained[1], trackMetaItem(2)));
  TEST_ASSERT_TRUE(persistWorkItemsEqual(drained[2], loopPersistItem(3)));
}

void test_lifecycle_not_scheduled_to_persisted() {
  resetQueue();
  const PersistKey key = persistKeyForLoop(9);
  TEST_ASSERT_EQUAL(static_cast<int>(PersistWorkState::NotScheduled),
                    static_cast<int>(PersistenceWorkQueue::workState(PersistWorkType::LoopPersist, key)));

  TEST_ASSERT_TRUE(PersistenceWorkQueue::admitWork(PersistWorkType::LoopPersist, key));
  TEST_ASSERT_EQUAL(static_cast<int>(PersistWorkState::Queued),
                    static_cast<int>(PersistenceWorkQueue::workState(PersistWorkType::LoopPersist, key)));

  PersistWorkItem item{};
  TEST_ASSERT_TRUE(PersistenceWorkQueue::beginWriteQueuedItem(item));
  TEST_ASSERT_EQUAL(static_cast<int>(PersistWorkState::Writing),
                    static_cast<int>(PersistenceWorkQueue::workState(PersistWorkType::LoopPersist, key)));

  PersistenceWorkQueue::markItemPersisted(item);
  TEST_ASSERT_EQUAL(static_cast<int>(PersistWorkState::Persisted),
                    static_cast<int>(PersistenceWorkQueue::workState(PersistWorkType::LoopPersist, key)));
  TEST_ASSERT_EQUAL(0u, PersistenceWorkQueue::queueDepth());
}

void test_re_admit_after_persisted_requeues_at_tail() {
  resetQueue();
  const PersistKey keyA = persistKeyForLoop(1);
  const PersistKey keyB = persistKeyForLoop(2);

  TEST_ASSERT_TRUE(PersistenceWorkQueue::admitWork(PersistWorkType::LoopPersist, keyA));
  PersistWorkItem item{};
  TEST_ASSERT_TRUE(PersistenceWorkQueue::beginWriteQueuedItem(item));
  PersistenceWorkQueue::markItemPersisted(item);

  TEST_ASSERT_TRUE(PersistenceWorkQueue::admitWork(PersistWorkType::LoopPersist, keyB));
  TEST_ASSERT_TRUE(PersistenceWorkQueue::admitWork(PersistWorkType::LoopPersist, keyA));

  PersistWorkItem queued[2] = {};
  TEST_ASSERT_EQUAL(2u, PersistenceWorkQueue::queuedWorkItems(queued, 2));
  TEST_ASSERT_TRUE(persistWorkItemsEqual(queued[0], loopPersistItem(2)));
  TEST_ASSERT_TRUE(persistWorkItemsEqual(queued[1], loopPersistItem(1)));
}

void test_requeue_writing_item_returns_to_head() {
  resetQueue();
  TEST_ASSERT_TRUE(PersistenceWorkQueue::admitWork(PersistWorkType::LoopPersist, persistKeyForLoop(5)));
  TEST_ASSERT_TRUE(PersistenceWorkQueue::admitWork(PersistWorkType::TrackMeta, persistKeyForTrack(1)));

  PersistWorkItem writingItem{};
  TEST_ASSERT_TRUE(PersistenceWorkQueue::beginWriteQueuedItem(writingItem));
  PersistenceWorkQueue::requeueWritingItem(writingItem);

  PersistWorkItem queued[2] = {};
  TEST_ASSERT_EQUAL(2u, PersistenceWorkQueue::queuedWorkItems(queued, 2));
  TEST_ASSERT_TRUE(persistWorkItemsEqual(queued[0], writingItem));
  TEST_ASSERT_TRUE(persistWorkItemsEqual(queued[1], trackMetaItem(1)));
}

void test_writing_work_item_count_tracks_active_writer() {
  resetQueue();
  TEST_ASSERT_EQUAL(0u, PersistenceWorkQueue::writingWorkItemCount());

  TEST_ASSERT_TRUE(PersistenceWorkQueue::admitWork(PersistWorkType::LoopPersist, persistKeyForLoop(2)));
  PersistWorkItem item{};
  TEST_ASSERT_TRUE(PersistenceWorkQueue::beginWriteQueuedItem(item));
  TEST_ASSERT_EQUAL(1u, PersistenceWorkQueue::writingWorkItemCount());

  PersistenceWorkQueue::markItemPersisted(item);
  TEST_ASSERT_EQUAL(0u, PersistenceWorkQueue::writingWorkItemCount());
}

void test_loop_persist_slot_key_differs_from_loop_id_key() {
  const PersistKey loopIdZero = persistKeyForLoop(0);
  const PersistKey trackFourSlotZero = persistKeyForSlot(4, 0);

  TEST_ASSERT_EQUAL(static_cast<int>(PersistKeyKind::LoopId), static_cast<int>(loopIdZero.kind));
  TEST_ASSERT_EQUAL(static_cast<int>(PersistKeyKind::Slot), static_cast<int>(trackFourSlotZero.kind));
  TEST_ASSERT_FALSE(persistKeysEqual(loopIdZero, trackFourSlotZero));
}

void test_loop_persist_slot_key_is_distinct_per_track() {
  resetQueue();
  const PersistKey trackFiveSlotZero = persistKeyForSlot(5, 0);
  const PersistKey trackZeroSlotZero = persistKeyForSlot(0, 0);

  TEST_ASSERT_TRUE(PersistenceWorkQueue::admitWork(PersistWorkType::LoopPersist, trackFiveSlotZero));
  TEST_ASSERT_TRUE(PersistenceWorkQueue::admitWork(PersistWorkType::LoopPersist, trackZeroSlotZero));
  TEST_ASSERT_EQUAL(2u, PersistenceWorkQueue::queueDepth());
  TEST_ASSERT_FALSE(persistKeysEqual(trackFiveSlotZero, trackZeroSlotZero));
}

void test_recover_orphaned_writing_item_requeues_at_head() {
  resetQueue();
  TEST_ASSERT_TRUE(PersistenceWorkQueue::admitWork(PersistWorkType::SlotMeta, persistKeyForSlot(4, 0)));
  TEST_ASSERT_TRUE(PersistenceWorkQueue::admitWork(PersistWorkType::TrackMeta, persistKeyForTrack(1)));

  PersistWorkItem writingItem{};
  TEST_ASSERT_TRUE(PersistenceWorkQueue::beginWriteQueuedItem(writingItem));
  TEST_ASSERT_EQUAL(1u, PersistenceWorkQueue::writingWorkItemCount());
  TEST_ASSERT_EQUAL(1u, PersistenceWorkQueue::queueDepth());

  TEST_ASSERT_TRUE(PersistenceWorkQueue::recoverOrphanedWritingItem());
  TEST_ASSERT_EQUAL(0u, PersistenceWorkQueue::writingWorkItemCount());
  TEST_ASSERT_EQUAL(2u, PersistenceWorkQueue::queueDepth());
  TEST_ASSERT_EQUAL(static_cast<int>(PersistWorkState::Queued),
                    static_cast<int>(PersistenceWorkQueue::workState(PersistWorkType::SlotMeta,
                                                                     persistKeyForSlot(4, 0))));

  PersistWorkItem queued[2] = {};
  TEST_ASSERT_EQUAL(2u, PersistenceWorkQueue::queuedWorkItems(queued, 2));
  TEST_ASSERT_TRUE(persistWorkItemsEqual(queued[0], writingItem));
}

void test_recover_orphaned_writing_unblocks_begin_write() {
  resetQueue();
  TEST_ASSERT_TRUE(PersistenceWorkQueue::admitWork(PersistWorkType::SlotMeta, persistKeyForSlot(4, 0)));

  PersistWorkItem writingItem{};
  TEST_ASSERT_TRUE(PersistenceWorkQueue::beginWriteQueuedItem(writingItem));
  TEST_ASSERT_FALSE(PersistenceWorkQueue::beginWriteQueuedItem(writingItem));

  TEST_ASSERT_TRUE(PersistenceWorkQueue::recoverOrphanedWritingItem());

  PersistWorkItem resumed{};
  TEST_ASSERT_TRUE(PersistenceWorkQueue::beginWriteQueuedItem(resumed));
  TEST_ASSERT_TRUE(persistWorkItemsEqual(resumed, writingItem));
  TEST_ASSERT_EQUAL(1u, PersistenceWorkQueue::writingWorkItemCount());
}

void test_try_get_writing_item_reports_active_writer() {
  resetQueue();
  PersistWorkItem item{};
  TEST_ASSERT_FALSE(PersistenceWorkQueue::tryGetWritingItem(item));

  TEST_ASSERT_TRUE(PersistenceWorkQueue::admitWork(PersistWorkType::SlotMeta, persistKeyForSlot(4, 0)));
  TEST_ASSERT_TRUE(PersistenceWorkQueue::beginWriteQueuedItem(item));

  PersistWorkItem observed{};
  TEST_ASSERT_TRUE(PersistenceWorkQueue::tryGetWritingItem(observed));
  TEST_ASSERT_TRUE(persistWorkItemsEqual(observed, item));
}

/// A `Persisted` entry is terminal, `LoopPersist` is keyed by `LoopId` (fresh for each new loop), and
/// no path ever returns an entry to `NotScheduled` -- so one clear/record cycle permanently consumes
/// one of the 256 slots. Exhausting the table must not make every later admission fail: the workspace
/// finalize is the item that clears `currentWorkspaceSave.pending`, and with nothing admittable the
/// blocking drain has nothing to step and refuses the save (2026-09-18:
/// `Persistence drain stuck after 33 iterations (max 33, expected ~1 slice steps, ~0 SD bytes)`).
void test_exhausted_entry_table_still_admits_the_finalize() {
  resetQueue();
  for (uint32_t i = 0; i < kMaxWorkEntries; ++i) {
    const PersistKey key = persistKeyForLoop(static_cast<LoopId>(100 + i));
    TEST_ASSERT_TRUE(PersistenceWorkQueue::admitWork(PersistWorkType::LoopPersist, key));
    PersistWorkItem item{};
    TEST_ASSERT_TRUE(PersistenceWorkQueue::beginWriteQueuedItem(item));
    PersistenceWorkQueue::markItemPersisted(item);
  }
  TEST_ASSERT_EQUAL(0u, PersistenceWorkQueue::queueDepth());
  TEST_ASSERT_EQUAL(0u, PersistenceWorkQueue::writingWorkItemCount());

  TEST_ASSERT_TRUE(
      PersistenceWorkQueue::admitWork(PersistWorkType::FinalizeWorkspace, persistKeySingleton()));
  TEST_ASSERT_EQUAL(static_cast<int>(PersistWorkState::Queued),
                    static_cast<int>(PersistenceWorkQueue::workState(
                        PersistWorkType::FinalizeWorkspace, persistKeySingleton())));
}

/// The reclaim that keeps the table admittable must take only terminal entries: a Queued or Writing
/// item still owes an SD write, so stealing its slot would drop that work. Which tombstone it takes
/// is not observable (a reclaimed key is re-admitted as a fresh entry), so the assertions below name
/// the expected victims only because entries are allocated in index order.
void test_full_table_reclaims_a_tombstone_and_never_a_queued_item() {
  resetQueue();
  const LoopId oldestLoopId = 1;
  for (uint32_t i = 0; i < kMaxWorkEntries; ++i) {
    const PersistKey key = persistKeyForLoop(static_cast<LoopId>(oldestLoopId + i));
    TEST_ASSERT_TRUE(PersistenceWorkQueue::admitWork(PersistWorkType::LoopPersist, key));
    PersistWorkItem item{};
    TEST_ASSERT_TRUE(PersistenceWorkQueue::beginWriteQueuedItem(item));
    PersistenceWorkQueue::markItemPersisted(item);
  }

  // The table is full. This admission gets a reclaimed tombstone and stays Queued ...
  const PersistKey protectedKey = persistKeyForTrack(3);
  TEST_ASSERT_TRUE(PersistenceWorkQueue::admitWork(PersistWorkType::TrackMeta, protectedKey));
  TEST_ASSERT_EQUAL(static_cast<int>(PersistWorkState::NotScheduled),
                    static_cast<int>(PersistenceWorkQueue::workState(PersistWorkType::LoopPersist,
                                                                     persistKeyForLoop(oldestLoopId))));

  // ... so the next one must reclaim a different tombstone, not the Queued entry above.
  const PersistKey freshKey = persistKeyForLoop(9000);
  TEST_ASSERT_TRUE(PersistenceWorkQueue::admitWork(PersistWorkType::LoopPersist, freshKey));
  TEST_ASSERT_EQUAL(static_cast<int>(PersistWorkState::Queued),
                    static_cast<int>(PersistenceWorkQueue::workState(PersistWorkType::LoopPersist,
                                                                     freshKey)));
  TEST_ASSERT_EQUAL(static_cast<int>(PersistWorkState::Queued),
                    static_cast<int>(PersistenceWorkQueue::workState(PersistWorkType::TrackMeta,
                                                                     protectedKey)));
  TEST_ASSERT_EQUAL(static_cast<int>(PersistWorkState::NotScheduled),
                    static_cast<int>(PersistenceWorkQueue::workState(
                        PersistWorkType::LoopPersist, persistKeyForLoop(oldestLoopId + 1))));
  TEST_ASSERT_EQUAL(2u, PersistenceWorkQueue::queueDepth());
}

void test_loop_undo_history_slot_key_is_distinct_per_track() {
  resetQueue();
  const PersistKey trackFiveSlotZero = persistKeyForSlot(5, 0);
  const PersistKey trackZeroSlotZero = persistKeyForSlot(0, 0);

  TEST_ASSERT_TRUE(
      PersistenceWorkQueue::admitWork(PersistWorkType::LoopUndoHistory, trackFiveSlotZero));
  TEST_ASSERT_TRUE(
      PersistenceWorkQueue::admitWork(PersistWorkType::LoopUndoHistory, trackZeroSlotZero));
  TEST_ASSERT_EQUAL(2u, PersistenceWorkQueue::queueDepth());
  TEST_ASSERT_FALSE(persistKeysEqual(trackFiveSlotZero, trackZeroSlotZero));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_persistence_step_is_named_only_on_a_breach);
  RUN_TEST(test_persist_key_equality);
  RUN_TEST(test_admit_dedup_while_queued_or_writing);
  RUN_TEST(test_fifo_re_admit_does_not_reorder);
  RUN_TEST(test_lifecycle_not_scheduled_to_persisted);
  RUN_TEST(test_re_admit_after_persisted_requeues_at_tail);
  RUN_TEST(test_requeue_writing_item_returns_to_head);
  RUN_TEST(test_writing_work_item_count_tracks_active_writer);
  RUN_TEST(test_loop_persist_slot_key_differs_from_loop_id_key);
  RUN_TEST(test_loop_persist_slot_key_is_distinct_per_track);
  RUN_TEST(test_exhausted_entry_table_still_admits_the_finalize);
  RUN_TEST(test_full_table_reclaims_a_tombstone_and_never_a_queued_item);
  RUN_TEST(test_loop_undo_history_slot_key_is_distinct_per_track);
  RUN_TEST(test_recover_orphaned_writing_item_requeues_at_head);
  RUN_TEST(test_recover_orphaned_writing_unblocks_begin_write);
  RUN_TEST(test_try_get_writing_item_reports_active_writer);
  return UNITY_END();
}
