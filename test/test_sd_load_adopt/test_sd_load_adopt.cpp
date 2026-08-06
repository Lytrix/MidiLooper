//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <cstring>
#include <unity.h>
#include <vector>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/EditManager/EditApply.cpp"
#include "../../src/LoopPasses.cpp"
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
  CommittedChunkIdList publishedIds;
  TEST_ASSERT_TRUE(LoopEventStore::transferCaptureChunkIdsToCommittedChunkIds(publishedIds, captureIds));

  RecordPass pass{};
  pass.id = 1;
  pass.state = CapturePassState::Active;
  pass.committedChunkIds = std::move(publishedIds);
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

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_apply_snapshot_adopts_chunks_without_duplicating_pool);
  RUN_TEST(test_restore_passes_snapshot_still_deep_clones_for_undo);
  RUN_TEST(test_sd_read_roundtrip_adopts_single_pool_copy);
  RUN_TEST(test_sd_read_under_staging_does_not_enqueue_mid_pass);
  return UNITY_END();
}
