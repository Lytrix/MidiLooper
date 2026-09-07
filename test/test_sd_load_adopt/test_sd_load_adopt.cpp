//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <cstring>
#include <unity.h>
#include <vector>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/EditManager/EditApply.cpp"
#include "../../src/Loop/LoopPasses.cpp"
#include "../../src/StorageLoopIo.cpp"
#include "../test_support/MemoryMonitorNativeDeps.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/Loop.cpp"
#include "../test_support/LoopCaptureTestDeps.cpp"

#include "../test_support/CommittedChunkIdTestHelpers.h"

#include "Loop.h"
#include "LoopEventStore.h"
#include "PersistenceQueue.h"
#include "StorageLoopIo.h"

namespace {

size_t countChunksInPasses(const LoopPasses& passes) {
  size_t total = 0;
  if (passes.hasRecordPass()) {
    total += passes.recordPass.committedChunkIds.size();
  }
  for (const OverdubPass& pass : passes.overdubPasses) {
    total += pass.committedChunkIds.size();
  }
  return total;
}

RecordPass makeMultiChunkRecordPass() {
  LoopEventStore capture;
  for (uint16_t i = 0; i < LoopEventStoreConfig::CHUNK_CAPACITY + 10; ++i) {
    TEST_ASSERT_TRUE(capture.append(MidiEvent::NoteOn(i, 1, 60, 100)));
  }
  CaptureChunkIdList captureIds;
  capture.detachChunksTo(captureIds);
  TEST_ASSERT_EQUAL(2u, captureIds.size());
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(LoopEventStore::transferCaptureChunkIdsToCommittedChunkIds(committedChunkIds, captureIds));

  RecordPass pass{};
  pass.id = 1;
  pass.state = CapturePassState::Active;
  pass.committedChunkIds = std::move(committedChunkIds);
  return pass;
}

}  // namespace

void test_apply_snapshot_adopts_chunks_without_duplicating_pool() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  PersistedLoopSnapshot snapshot{};
  snapshot.loopId = 1;
  snapshot.loopLengthTicks = 49152;
  snapshot.passes.recordPass = makeMultiChunkRecordPass();
  const size_t snapshotChunks = countChunksInPasses(snapshot.passes);

  const uint16_t usedBefore = LoopEventStore::usedChunkCount();
  TEST_ASSERT_EQUAL(static_cast<uint16_t>(snapshotChunks), usedBefore);

  Loop loop;
  applySnapshotToLoop(loop, snapshot);

  TEST_ASSERT_FALSE(snapshot.passes.hasRecordPass());
  TEST_ASSERT_EQUAL(0u, countChunksInPasses(snapshot.passes));
  TEST_ASSERT_TRUE(loop.passes.hasRecordPass());
  TEST_ASSERT_EQUAL(snapshotChunks, countChunksInPasses(loop.passes));
  TEST_ASSERT_EQUAL(static_cast<uint16_t>(snapshotChunks), LoopEventStore::usedChunkCount());
  TEST_ASSERT_TRUE(loop.hasPendingSnapshotPublish());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
}

void test_restore_passes_snapshot_still_deep_clones_for_undo() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  PersistedLoopSnapshot snapshot{};
  snapshot.loopLengthTicks = 768;
  snapshot.passes.recordPass = makeMultiChunkRecordPass();
  const CommittedChunkIdList snapshotRefs = snapshot.passes.recordPass.committedChunkIds;

  Loop loop;
  loop.restorePassesSnapshot(snapshot);

  TEST_ASSERT_TRUE(loop.passes.hasRecordPass());
  TEST_ASSERT_EQUAL(snapshotRefs.size(), loop.passes.recordPass.committedChunkIds.size());
  TEST_ASSERT_NOT_EQUAL(snapshotRefs[0], loop.passes.recordPass.committedChunkIds[0]);
  TEST_ASSERT_EQUAL(static_cast<uint16_t>(snapshotRefs.size() * 2u),
                    LoopEventStore::usedChunkCount());
  TEST_ASSERT_TRUE(loop.hasPendingSnapshotPublish());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
}

void test_sd_read_roundtrip_adopts_single_pool_copy() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  PersistedLoopSnapshot original{};
  original.loopId = 3;
  original.loopLengthTicks = 49152;
  original.nextPassId = 2;
  original.passes.recordPass = makeMultiChunkRecordPass();

  std::vector<uint8_t> buffer;
  struct MemoryStorageIo {
    std::vector<uint8_t>* buf;
    size_t readPos = 0;
    void resetRead() { readPos = 0; }
    StorageIo io() {
      return StorageIo{
          [this](const void* data, size_t size) -> bool {
            const auto* bytes = static_cast<const uint8_t*>(data);
            buf->insert(buf->end(), bytes, bytes + size);
            return true;
          },
          [this](void* data, size_t size) -> bool {
            if (readPos + size > buf->size()) {
              return false;
            }
            std::memcpy(data, buf->data() + readPos, size);
            readPos += size;
            return true;
          }};
    }
  } mem{&buffer};

  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(mem.io(), original));

  Loop sink;
  sink.adoptPersistedSnapshot(original);
  sink.resetPassTimeline();

  PersistedLoopSnapshot restored{};
  mem.resetRead();
  TEST_ASSERT_TRUE(readPersistedLoopSnapshot(mem.io(), restored));

  Loop loop;
  applySnapshotToLoop(loop, restored);

  TEST_ASSERT_FALSE(restored.passes.hasRecordPass());
  TEST_ASSERT_TRUE(loop.passes.hasRecordPass());
  TEST_ASSERT_EQUAL(2u, countChunksInPasses(loop.passes));
  TEST_ASSERT_EQUAL(2u, LoopEventStore::usedChunkCount());
  TEST_ASSERT_TRUE(loop.hasPendingSnapshotPublish());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
}

void test_sd_read_under_staging_does_not_enqueue_mid_pass() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  PersistedLoopSnapshot original{};
  original.loopId = 4;
  original.loopLengthTicks = 768;
  original.nextPassId = 2;
  original.passes.recordPass = makeMultiChunkRecordPass();

  std::vector<uint8_t> buffer;
  struct MemoryStorageIo {
    std::vector<uint8_t>* buf;
    size_t readPos = 0;
    void resetRead() { readPos = 0; }
    StorageIo io() {
      return StorageIo{
          [this](const void* data, size_t size) -> bool {
            const auto* bytes = static_cast<const uint8_t*>(data);
            buf->insert(buf->end(), bytes, bytes + size);
            return true;
          },
          [this](void* data, size_t size) -> bool {
            if (readPos + size > buf->size()) {
              return false;
            }
            std::memcpy(data, buf->data() + readPos, size);
            readPos += size;
            return true;
          }};
    }
  } mem{&buffer};

  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(mem.io(), original));

  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  TEST_ASSERT_EQUAL(0u, PersistenceQueue::queueDepth());

  PersistedLoopSnapshot restored{};
  mem.resetRead();
  LoopEventStore::enterSdLoadStaging();
  TEST_ASSERT_TRUE(readPersistedLoopSnapshot(mem.io(), restored));
  LoopEventStore::leaveSdLoadStaging();

  TEST_ASSERT_EQUAL(0u, PersistenceQueue::queueDepth());
  TEST_ASSERT_TRUE(restored.passes.hasRecordPass());
  for (uint16_t id : restored.passes.recordPass.committedChunkIds) {
    TEST_ASSERT_EQUAL(static_cast<int>(ChunkPersistenceState::Persisted),
                      static_cast<int>(PersistenceQueue::chunkState(id)));
  }
}

void drainPendingSnapshotPublish(Loop& loop) {
  size_t steps = 0;
  while (loop.hasPendingSnapshotPublish() && steps < 10000) {
    loop.stepPendingSnapshotPublish(1);
    ++steps;
  }
  TEST_ASSERT_FALSE(loop.hasPendingSnapshotPublish());
}

void test_adopt_queues_loaded_snapshot_publish_until_idle() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop source;
  source.loopId = 8;
  source.loopLengthTicks = 3072;
  source.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(source.appendCaptureEvent(MidiEvent::NoteOn(100, 1, 60, 100)));
  TEST_ASSERT_TRUE(source.appendCaptureEvent(MidiEvent::NoteOff(200, 1, 60, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    source.commitCapturePass(CommitReason::RecordStop, 200));
  drainPendingSnapshotPublish(source);
  TEST_ASSERT_TRUE(source.publishedSnapshotIsCurrent());

  LoopSnapshotRef shared = source.sharePassesSnapshot();
  TEST_ASSERT_NOT_NULL(shared.get());

  Loop dest;
  dest.adoptPersistedSnapshot(*shared);
  TEST_ASSERT_TRUE(dest.hasPendingSnapshotPublish());
  TEST_ASSERT_FALSE(dest.publishedSnapshotIsCurrent());
  TEST_ASSERT_EQUAL(0u, dest.publishedSnapshotRevision());

  drainPendingSnapshotPublish(dest);
  TEST_ASSERT_TRUE(dest.publishedSnapshotIsCurrent());
  TEST_ASSERT_NOT_EQUAL(0u, dest.publishedSnapshotRevision());

  dest.invalidateDisplayCaches();
  dest.rebuildVisualCacheIdleSlice(4, 0);
  TEST_ASSERT_TRUE(dest.lastVisualCacheIdleCopiedSnapshotNotes());
}

void test_restore_empty_does_not_queue_loaded_snapshot_publish() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  PersistedLoopSnapshot snapshot{};
  snapshot.loopLengthTicks = 768;
  snapshot.passes.recordPass = makeMultiChunkRecordPass();

  Loop loop;
  loop.restorePassesSnapshot(snapshot);
  TEST_ASSERT_TRUE(loop.hasPendingSnapshotPublish());

  PersistedLoopSnapshot empty{};
  loop.restorePassesSnapshot(empty);
  TEST_ASSERT_FALSE(loop.hasCommittedPasses());
  TEST_ASSERT_FALSE(loop.hasPendingSnapshotPublish());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_apply_snapshot_adopts_chunks_without_duplicating_pool);
  RUN_TEST(test_restore_passes_snapshot_still_deep_clones_for_undo);
  RUN_TEST(test_sd_read_roundtrip_adopts_single_pool_copy);
  RUN_TEST(test_sd_read_under_staging_does_not_enqueue_mid_pass);
  RUN_TEST(test_adopt_queues_loaded_snapshot_publish_until_idle);
  RUN_TEST(test_restore_empty_does_not_queue_loaded_snapshot_publish);
  return UNITY_END();
}
