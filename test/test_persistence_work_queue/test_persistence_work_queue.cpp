//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/PersistenceWorkQueue.cpp"

#include "StorageManagerInternal/PersistenceWorkQueue.h"

namespace {

void resetQueue() { PersistenceWorkQueue::resetForTests(); }

PersistWorkItem loopPersistItem(LoopId loopId) {
  return PersistWorkItem{PersistWorkType::LoopPersist, persistKeyForLoop(loopId)};
}

PersistWorkItem trackMetaItem(uint8_t trackIndex) {
  return PersistWorkItem{PersistWorkType::TrackMeta, persistKeyForTrack(trackIndex)};
}

}  // namespace

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

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_persist_key_equality);
  RUN_TEST(test_admit_dedup_while_queued_or_writing);
  RUN_TEST(test_fifo_re_admit_does_not_reorder);
  RUN_TEST(test_lifecycle_not_scheduled_to_persisted);
  RUN_TEST(test_re_admit_after_persisted_requeues_at_tail);
  RUN_TEST(test_requeue_writing_item_returns_to_head);
  RUN_TEST(test_writing_work_item_count_tracks_active_writer);
  return UNITY_END();
}
