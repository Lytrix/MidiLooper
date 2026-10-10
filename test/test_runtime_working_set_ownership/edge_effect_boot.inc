// Boot storage deliberately bypasses native static zero initialization, matching
// Teensy's NOLOAD DMAMEM. This harness includes the real queue/owner TU.
void test_edge_effect_boot_setup_discards_unpublished_stale_storage() {
  constexpr size_t payloadBytes = EdgeEffectQueue::kCapacity * sizeof(EdgeEffectRecord);
  static_assert(sizeof(EdgeEffectRecord) == 16, "fixture pins the target record layout");
  static_assert(sizeof(EdgeEffectQueue) == payloadBytes + 8, "fixture pins queue metadata layout");
  static_assert(std::is_trivially_copyable<EdgeEffectQueue>::value, "byte-image fixture requires trivial copy");
  for (uint16_t staleRead : {uint16_t{1}, uint16_t{65535}, uint16_t{0}}) {
    unsigned char stale[sizeof(EdgeEffectQueue)];
    memset(stale, 0xa5, sizeof(stale));
    const uint16_t staleWrite = 0;
    memcpy(stale + payloadBytes, &staleWrite, sizeof(staleWrite));
    memcpy(stale + payloadBytes + 2, &staleRead, sizeof(staleRead));
    memcpy(&gEdgeEffectQueue, stale, sizeof(stale));
    gReportedDroppedEdgeEffects = 123;

    trackManager.setup();  // Real firmware boot entry, not a test-only reset.
    const uint16_t depth = gEdgeEffectQueue.depth();
    const uint32_t dropped = gEdgeEffectQueue.droppedCount();
    const uint32_t reported = gReportedDroppedEdgeEffects;
    unsigned char after[sizeof(EdgeEffectQueue)];
    memcpy(after, &gEdgeEffectQueue, sizeof(after));
    EdgeEffectRecord out;
    const bool tookStale = gEdgeEffectQueue.takeOldest(out);

    // Restore before any failing assertion, so the RED cannot poison later cases.
    const EdgeEffectQueue empty;
    memcpy(&gEdgeEffectQueue, &empty, sizeof(empty));
    gReportedDroppedEdgeEffects = 0;
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, depth, "boot must not treat retained positions as published work");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, dropped, "boot must reset the retained drop count");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, reported, "boot must reset the retained report baseline");
    TEST_ASSERT_FALSE_MESSAGE(tookStale, "no retained payload may escape boot setup");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(stale, after, payloadBytes, "boot need not clear unreachable payload");
  }
}

void test_edge_effect_boot_preserves_current_boot_deferred_effects() {
  trackManager.setup();
  LoggerNativeInfoProbe::reset();
  trackManager.drainEdgeEffects();
  TEST_ASSERT_EQUAL_UINT32(0, LoggerNativeInfoProbe::lineCount);
  trackManager.recordEdgeEffect(EdgeEffectKind::SlotSwitchCancelledNoRamData, 7, 0, 3, 0, 0,
                                kEdgeEffectLog);
  TEST_ASSERT_EQUAL_UINT32(0, LoggerNativeInfoProbe::lineCount);
  trackManager.drainEdgeEffects();
  TEST_ASSERT_EQUAL_UINT32(1, LoggerNativeInfoProbe::lineCount);
  TEST_ASSERT_EQUAL_STRING("LoopEnd playback commit cancelled track=7 target=3 (no RAM data)",
                           LoggerNativeInfoProbe::lastLine);
  trackManager.drainEdgeEffects();
  TEST_ASSERT_EQUAL_UINT32(1, LoggerNativeInfoProbe::lineCount);
}

void test_edge_effect_queue_preserves_fifo_and_full_rejection_across_counter_rollover() {
  EdgeEffectQueue queue;
  EdgeEffectRecord out;
  constexpr uint32_t batches = 4097;  // 65,552 records, crossing uint16 rollover.
  for (uint32_t batch = 0; batch < batches; ++batch) {
    for (uint16_t i = 0; i < EdgeEffectQueue::kCapacity; ++i) {
      EdgeEffectRecord row;
      row.kind = static_cast<uint8_t>(EdgeEffectKind::LoopEndCommit);
      row.work = kEdgeEffectLog;
      row.track = i % 8;
      row.toSlot = (i + 1) % 8;
      row.loopStartTick = batch * EdgeEffectQueue::kCapacity + i;
      row.loopLengthTicks = 768;
      TEST_ASSERT_TRUE(queue.record(row));
    }
    TEST_ASSERT_EQUAL_UINT16(EdgeEffectQueue::kCapacity, queue.depth());
    TEST_ASSERT_FALSE(queue.record(EdgeEffectRecord{}));
    TEST_ASSERT_EQUAL_UINT32(batch + 1, queue.droppedCount());
    for (uint16_t i = 0; i < EdgeEffectQueue::kCapacity; ++i) {
      TEST_ASSERT_TRUE(queue.takeOldest(out));
      TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EdgeEffectKind::LoopEndCommit), out.kind);
      TEST_ASSERT_EQUAL_UINT8(kEdgeEffectLog, out.work);
      TEST_ASSERT_EQUAL_UINT8(i % 8, out.track);
      TEST_ASSERT_EQUAL_UINT8((i + 1) % 8, out.toSlot);
      TEST_ASSERT_EQUAL_UINT32(batch * EdgeEffectQueue::kCapacity + i, out.loopStartTick);
      TEST_ASSERT_EQUAL_UINT32(768, out.loopLengthTicks);
    }
    TEST_ASSERT_EQUAL_UINT16(0, queue.depth());
    TEST_ASSERT_FALSE(queue.takeOldest(out));
  }
}
