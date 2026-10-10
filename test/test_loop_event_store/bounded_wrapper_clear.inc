namespace {
template <typename Cache>
void appendClearRows(Cache& cache) {
  auto& store = cache.mutStore();
  for (uint32_t i = 0; i < 513; ++i)
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(i, 1, 60, 100)));
}
template <typename Cache>
void drainClear(Cache& cache) {
  for (unsigned turn = 0; turn < 10; ++turn)
    if (cache.stepClear() == Cache::ClearResult::Complete) return;
  TEST_FAIL_MESSAGE("wrapper clear did not complete in ten grants");
}
}

void test_wrapper_clear_unique_one_chunk_and_separate_header_terminal() {
  LoopEventStore::resetPoolForTests();
  CowLoopEventStore cache;
  appendClearRows(cache);
  cache.readStore().barFirstIndices();
  const auto ids = cache.readStore().chunkIds();
  const auto* header = &cache.readStore();
  TEST_ASSERT_EQUAL(513u, cache.size());
  TEST_ASSERT_EQUAL(3u, ids.size());
  const size_t counts[] = {512, 256, 0};
  for (size_t turn = 0; turn < 3; ++turn) {
    TEST_ASSERT_TRUE(cache.stepClear() == CowLoopEventStore::ClearResult::Progress);
    TEST_ASSERT_EQUAL(counts[turn], cache.size());
    TEST_ASSERT_EQUAL(2 - turn, cache.readStore().chunkIds().size());
    TEST_ASSERT_EQUAL(0, LoopEventStore::chunkReferenceCount(ids[2 - turn]));
    TEST_ASSERT_TRUE(header == &cache.readStore());
    TEST_ASSERT_FALSE(cache.hasEventsCacheForTest());
  }
  TEST_ASSERT_TRUE(cache.stepClear() == CowLoopEventStore::ClearResult::Progress);
  TEST_ASSERT_FALSE(header == &cache.readStore());
  TEST_ASSERT_TRUE(cache.stepClear() == CowLoopEventStore::ClearResult::Complete);
  header = &cache.readStore();
  TEST_ASSERT_TRUE(cache.stepClear() == CowLoopEventStore::ClearResult::Complete);
  TEST_ASSERT_TRUE(header == &cache.readStore());
  TEST_ASSERT_TRUE(cache.empty());
}

void test_wrapper_clear_shared_detaches_without_clone_or_source_mutation() {
  LoopEventStore::resetPoolForTests();
  CowLoopEventStore source;
  appendClearRows(source);
  CowLoopEventStore departing = source;
  auto snapshot = source.shareForSnapshot();
  const auto ids = snapshot->chunkIds();
  const auto free = LoopEventStore::freeChunkCount();
  TEST_ASSERT_EQUAL(3, snapshot.use_count());
  TEST_ASSERT_TRUE(departing.stepClear() == CowLoopEventStore::ClearResult::Progress);
  TEST_ASSERT_EQUAL(2, snapshot.use_count());
  TEST_ASSERT_TRUE(&source.readStore() == snapshot.get());
  TEST_ASSERT_EQUAL(513u, snapshot->size());
  TEST_ASSERT_EQUAL(0u, snapshot->at(0).tick);
  TEST_ASSERT_EQUAL(512u, snapshot->at(512).tick);
  TEST_ASSERT_EQUAL(free, LoopEventStore::freeChunkCount());
  for (auto id : ids) TEST_ASSERT_EQUAL(1, LoopEventStore::chunkReferenceCount(id));
  TEST_ASSERT_FALSE(departing.hasEventsCacheForTest());
  TEST_ASSERT_TRUE(departing.stepClear() == CowLoopEventStore::ClearResult::Complete);
  snapshot.reset();
  drainClear(source);
}

void test_wrapper_clear_dirty_cache_discards_without_sync() {
  LoopEventStore::resetPoolForTests();
  NoteEditSessionStore cache;
  appendClearRows(cache);
  cache.mutEvents().push_back(MidiEvent::NoteOn(999, 1, 70, 90));
  const auto ids = cache.readStore().chunkIds();
  TEST_ASSERT_EQUAL(514u, cache.eventValueCount());
  TEST_ASSERT_TRUE(cache.stepClear() == NoteEditSessionStore::ClearResult::Progress);
  TEST_ASSERT_FALSE(cache.hasEventsCacheForTest());
  TEST_ASSERT_FALSE(cache.isEventsDirty());
  TEST_ASSERT_EQUAL(513u, cache.size());
  TEST_ASSERT_EQUAL(3u, cache.readStore().chunkIds().size());
  TEST_ASSERT_EQUAL(512u, cache.at(512).tick);
  for (auto id : ids) TEST_ASSERT_EQUAL(1, LoopEventStore::chunkReferenceCount(id));
  TEST_ASSERT_TRUE(cache.stepClear() == NoteEditSessionStore::ClearResult::Progress);
  TEST_ASSERT_EQUAL(512u, cache.size());
  drainClear(cache);
}

void test_wrapper_clear_empty_and_empty_dirty_cache() {
  LoopEventStore::resetPoolForTests();
  NoteEditSessionStore cache;
  const auto* header = &cache.readStore();
  TEST_ASSERT_TRUE(cache.stepClear() == NoteEditSessionStore::ClearResult::Complete);
  TEST_ASSERT_TRUE(header == &cache.readStore());
  TEST_ASSERT_FALSE(cache.hasEventsCacheForTest());
  cache.mutEvents();
  TEST_ASSERT_TRUE(cache.isEventsDirty());
  TEST_ASSERT_TRUE(cache.stepClear() == NoteEditSessionStore::ClearResult::Progress);
  TEST_ASSERT_TRUE(cache.stepClear() == NoteEditSessionStore::ClearResult::Complete);
  TEST_ASSERT_TRUE(header == &cache.readStore());
}

void test_wrapper_clear_stamped_pool_reset_preserves_recycled_chunk() {
  LoopEventStore::resetPoolForTests();
  CowLoopEventStore old;
  TEST_ASSERT_TRUE(old.mutStore().reserveCaptureTail());
  appendClearRows(old);
  LoopEventStore::resetPoolForTests();
  CowLoopEventStore current;
  TEST_ASSERT_TRUE(current.mutStore().append(MidiEvent::NoteOn(42, 1, 72, 99)));
  const auto id = current.readStore().chunkIds().front();
  for (size_t turn = 0; turn < 3; ++turn) {
    TEST_ASSERT_TRUE(old.stepClear() == CowLoopEventStore::ClearResult::Progress);
    TEST_ASSERT_EQUAL(2 - turn, old.readStore().chunkIds().size());
    TEST_ASSERT_EQUAL(1, LoopEventStore::chunkReferenceCount(id));
    TEST_ASSERT_EQUAL(42u, current.at(0).tick);
  }
  TEST_ASSERT_TRUE(old.stepClear() == CowLoopEventStore::ClearResult::Progress);
  TEST_ASSERT_TRUE(old.stepClear() == CowLoopEventStore::ClearResult::Complete);
  TEST_ASSERT_EQUAL(1u, current.size());
  drainClear(current);
}
