//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../test_support/MemoryMonitorNativeDeps.cpp"

#include "Globals.h"
#include "LoopPasses.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/MemoryPressurePolicy.h"

namespace {

MemoryPressurePolicy::Inputs makeInputs(uint32_t heapFree, uint16_t chunksFree,
                                        bool appendLatch = false) {
  MemoryPressurePolicy::Inputs inputs{};
  inputs.heapFreeBytes = heapFree;
  inputs.chunksFree = chunksFree;
  inputs.captureAppendFailedLatch = appendLatch;
  return inputs;
}

constexpr uint32_t kNormalHeap = 70 * 1024;
constexpr uint32_t kLowHeap = 50 * 1024;
constexpr uint32_t kCriticalHeap = 28 * 1024;
constexpr uint16_t kHealthyChunks = 40;
constexpr uint16_t kCriticalChunks = PassConfig::CHUNK_RESERVE;
constexpr uint16_t kTightChunks = PassConfig::CHUNK_RESERVE + 4;

}  // namespace

void test_classify_raw_normal() {
  const auto inputs = makeInputs(kNormalHeap, kHealthyChunks);
  TEST_ASSERT_EQUAL(static_cast<int>(MemoryPressureLevel::Normal),
                    static_cast<int>(MemoryPressurePolicy::classifyRawLevel(inputs)));
}

void test_classify_raw_low_heap() {
  const auto inputs = makeInputs(kLowHeap, kHealthyChunks);
  TEST_ASSERT_EQUAL(static_cast<int>(MemoryPressureLevel::Low),
                    static_cast<int>(MemoryPressurePolicy::classifyRawLevel(inputs)));
}

void test_classify_raw_critical_heap() {
  const auto inputs = makeInputs(kCriticalHeap, kHealthyChunks);
  TEST_ASSERT_EQUAL(static_cast<int>(MemoryPressureLevel::Critical),
                    static_cast<int>(MemoryPressurePolicy::classifyRawLevel(inputs)));
}

void test_classify_raw_critical_chunks() {
  const auto inputs = makeInputs(kNormalHeap, kCriticalChunks);
  TEST_ASSERT_EQUAL(static_cast<int>(MemoryPressureLevel::Critical),
                    static_cast<int>(MemoryPressurePolicy::classifyRawLevel(inputs)));
}

void test_classify_raw_critical_append_latch() {
  const auto inputs = makeInputs(kNormalHeap, kHealthyChunks, true);
  TEST_ASSERT_EQUAL(static_cast<int>(MemoryPressureLevel::Critical),
                    static_cast<int>(MemoryPressurePolicy::classifyRawLevel(inputs)));
}

void test_step_escalates_immediately() {
  uint32_t stableSince = 0;
  const auto inputs = makeInputs(kCriticalHeap, kHealthyChunks);
  const MemoryPressureLevel next = MemoryPressurePolicy::stepLevel(
      MemoryPressureLevel::Normal, inputs, 1000, stableSince);
  TEST_ASSERT_EQUAL(static_cast<int>(MemoryPressureLevel::Critical),
                    static_cast<int>(next));
  TEST_ASSERT_EQUAL_UINT32(0, stableSince);
}

void test_step_low_to_normal_requires_hysteresis() {
  uint32_t stableSince = 0;
  const auto inputs = makeInputs(Config::HEAP_PRESSURE_LOW_EXIT_BYTES, kHealthyChunks);

  const MemoryPressureLevel first = MemoryPressurePolicy::stepLevel(
      MemoryPressureLevel::Low, inputs, 1000, stableSince);
  TEST_ASSERT_EQUAL(static_cast<int>(MemoryPressureLevel::Low), static_cast<int>(first));
  TEST_ASSERT_EQUAL_UINT32(1000, stableSince);

  const MemoryPressureLevel tooSoon = MemoryPressurePolicy::stepLevel(
      MemoryPressureLevel::Low, inputs, 1000 + Config::HEAP_PRESSURE_HYSTERESIS_MS - 1,
      stableSince);
  TEST_ASSERT_EQUAL(static_cast<int>(MemoryPressureLevel::Low), static_cast<int>(tooSoon));

  const MemoryPressureLevel recovered = MemoryPressurePolicy::stepLevel(
      MemoryPressureLevel::Low, inputs, 1000 + Config::HEAP_PRESSURE_HYSTERESIS_MS, stableSince);
  TEST_ASSERT_EQUAL(static_cast<int>(MemoryPressureLevel::Normal),
                    static_cast<int>(recovered));
}

void test_step_critical_to_low_requires_exit_band() {
  uint32_t stableSince = 0;
  const auto stillCritical = makeInputs(kCriticalHeap, kHealthyChunks);
  const MemoryPressureLevel held = MemoryPressurePolicy::stepLevel(
      MemoryPressureLevel::Critical, stillCritical, 5000, stableSince);
  TEST_ASSERT_EQUAL(static_cast<int>(MemoryPressureLevel::Critical),
                    static_cast<int>(held));

  const auto recoveredInputs = makeInputs(Config::HEAP_PRESSURE_CRITICAL_EXIT_BYTES, kHealthyChunks);
  const MemoryPressureLevel low = MemoryPressurePolicy::stepLevel(
      MemoryPressureLevel::Critical, recoveredInputs, 5000, stableSince);
  TEST_ASSERT_EQUAL(static_cast<int>(MemoryPressureLevel::Low), static_cast<int>(low));
}

void test_advisory_update_transitions_without_reclaim() {
  MemoryMonitor::resetNativeTestPressureInputs();
  MemoryMonitor::resetNativeTestFreeHeap();
  MemoryMonitor::setNativeTestFreeHeap(kNormalHeap);
  MemoryMonitor::setNativeTestChunksFree(kHealthyChunks);

  MemoryMonitor::updateAdvisoryPressureLevel(0);
  TEST_ASSERT_EQUAL(static_cast<int>(MemoryPressureLevel::Normal),
                    static_cast<int>(MemoryMonitor::getAdvisoryPressureLevel()));

  MemoryMonitor::setNativeTestFreeHeap(kLowHeap);
  MemoryMonitor::updateAdvisoryPressureLevel(100);
  TEST_ASSERT_EQUAL(static_cast<int>(MemoryPressureLevel::Low),
                    static_cast<int>(MemoryMonitor::getAdvisoryPressureLevel()));

  MemoryMonitor::setNativeTestFreeHeap(kCriticalHeap);
  MemoryMonitor::updateAdvisoryPressureLevel(200);
  TEST_ASSERT_EQUAL(static_cast<int>(MemoryPressureLevel::Critical),
                    static_cast<int>(MemoryMonitor::getAdvisoryPressureLevel()));

  MemoryMonitor::notifyCaptureAppendFailed(300);
  MemoryMonitor::setNativeTestFreeHeap(kNormalHeap);
  MemoryMonitor::setNativeTestChunksFree(kTightChunks);
  MemoryMonitor::updateAdvisoryPressureLevel(400);
  TEST_ASSERT_EQUAL(static_cast<int>(MemoryPressureLevel::Critical),
                    static_cast<int>(MemoryMonitor::getAdvisoryPressureLevel()));
}

// ---------------------------------------------------------------------------------------------
// **Task 4.20: the arena tail is not the same question as "can this be allocated".**
//
// `getInternalHeapFreeBytes()` is `&_heap_end - __brkval` — the *untouched* tail of the arena, which only ever
// shrinks because the Teensy core's `_sbrk` is a pure bump allocator. Freed blocks live in newlib's own free list
// below `__brkval` and are reusable, so a tail below the admission sum is a **lower bound** that does not prove a
// request fails. This pins the asymmetry the admission policy is built on, on the host, before any device time:
// the tail says 44 KB (below the 32 KB floor + 16 KB margin = 48 KB) while the 16 KB request the overdub start
// needs is servable, and the probe reports both facts separately instead of collapsing them into one verdict.
void test_heap_allocatability_probe_separates_tail_from_a_servable_request() {
  MemoryMonitor::resetNativeTestFreeHeap();
  MemoryMonitor::setNativeTestFreeHeap(45u * 1024u);
  const MemoryMonitor::HeapAllocatabilityProbe probe =
      MemoryMonitor::probeInternalHeapAllocatability(16u * 1024u);
  TEST_ASSERT_EQUAL_UINT32(45u * 1024u, probe.tailFreeBytes);
  TEST_ASSERT_EQUAL_UINT32(16u * 1024u, probe.requestedBytes);
  TEST_ASSERT_TRUE_MESSAGE(
      probe.internalMallocOk,
      "a 16 KB request is servable while the arena tail reads below the 48 KB admission sum");
  // The probe takes and returns the same block, so the tail it reports is unchanged by asking.
  TEST_ASSERT_EQUAL_UINT32(45u * 1024u, MemoryMonitor::getInternalHeapFreeBytes());
  // A zero request is not an allocation and must not be read as a refusal (a caller can ask "nothing needed").
  const MemoryMonitor::HeapAllocatabilityProbe empty =
      MemoryMonitor::probeInternalHeapAllocatability(0);
  TEST_ASSERT_FALSE(empty.internalMallocOk);
  TEST_ASSERT_FALSE(empty.externalPoolOk);
  MemoryMonitor::resetNativeTestFreeHeap();
}

// ---------------------------------------------------------------------------------------------
// **Task 4.20 (DEC-169): the false denial is fixed — the tail is a pre-filter, the free list is the verdict.**
//
// Device evidence (2026-10-27): at a 36,864 B tail `!HEAP_STATS` reported `largest_chunk=81320` (79.4 KB) — the gate
// denied a start whose `floor + margin` the allocator could serve. The earlier malloc-probe fix was withdrawn because
// it perturbed the arena; the read-only free-list walk (`largestInternalFreeChunkBytes`) does not. This fixture pins the
// fix: a gesture admission whose tail is low is resolved by the modeled largest free chunk, `SnapshotReserve` keeps the
// tail rule, and a tail that meets the sum needs no walk.
void test_tail_low_but_free_list_servable_admits_the_start() {
  MemoryMonitor::resetNativeTestFreeHeap();
  MemoryMonitor::resetNativeTestAllocatableCapacity();
  MemoryMonitor::setNativeForcedReserveDenied(false);
  constexpr uint32_t kFloor = 32u * 1024u;
  constexpr uint32_t kMargin = 16u * 1024u;
  // (a) The device's own state: tail 36 KB, largest free chunk 79 KB → the start is admitted.
  MemoryMonitor::setNativeTestFreeHeap(36u * 1024u);
  MemoryMonitor::setNativeTestAllocatableCapacity(79u * 1024u);
  TEST_ASSERT_TRUE_MESSAGE(
      MemoryMonitor::admitOperationMemory(MemoryMonitor::OperationKind::OverdubStart, 0),
      "a start whose floor+margin the free list can serve must be admitted despite a low tail");
  // (b) The block genuinely not servable → still refused (largest chunk 40 KB < 48 KB).
  MemoryMonitor::setNativeTestAllocatableCapacity(40u * 1024u);
  TEST_ASSERT_FALSE_MESSAGE(
      MemoryMonitor::admitOperationMemory(MemoryMonitor::OperationKind::OverdubStart, 0),
      "a start whose floor+margin no free chunk can serve must still be refused");
  // (c) A tail that meets the sum is admitted without any free-list consultation.
  MemoryMonitor::resetNativeTestAllocatableCapacity();
  MemoryMonitor::setNativeTestFreeHeap(kFloor + kMargin);
  TEST_ASSERT_TRUE(MemoryMonitor::admitOperationMemory(
      MemoryMonitor::OperationKind::OverdubStart, 0));
  // (d) `SnapshotReserve` keeps the tail rule with its own margin — the gate the deferred stop depends on, and the one
  // the withdrawn allocation probe was measured to starve.
  MemoryMonitor::setNativeTestFreeHeap(68u * 1024u);
  TEST_ASSERT_TRUE(MemoryMonitor::admitOperationMemory(
      MemoryMonitor::OperationKind::SnapshotReserve, 8u * 1024u));
  MemoryMonitor::setNativeTestFreeHeap(40u * 1024u - 1u);
  TEST_ASSERT_FALSE(MemoryMonitor::admitOperationMemory(
      MemoryMonitor::OperationKind::SnapshotReserve, 8u * 1024u));
  MemoryMonitor::resetNativeTestFreeHeap();
  MemoryMonitor::resetNativeTestAllocatableCapacity();
}

// The ladder is one-shot, descending, stops at the first success, and reports the rungs that fit — pinned on the host so
// the device row means what it says.
void test_denial_edge_capacity_ladder_reports_the_largest_servable_rung() {
  MemoryMonitor::resetNativeTestFreeHeap();
  MemoryMonitor::resetNativeTestAllocatableCapacity();
  MemoryMonitor::setNativeTestFreeHeap(36u * 1024u);
  MemoryMonitor::setNativeTestAllocatableCapacity(32u * 1024u);
  const MemoryMonitor::HeapCapacityLadder ladder =
      MemoryMonitor::probeHeapCapacityLadder(48u * 1024u);
  TEST_ASSERT_EQUAL_UINT32(36u * 1024u, ladder.tailFreeBytes);
  TEST_ASSERT_EQUAL_UINT32(48u * 1024u, ladder.needBytes);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(32u * 1024u, ladder.largestAllocatableBytes,
                                   "the biggest rung the capacity could serve");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x07u, ladder.servedMask,
                                  "16K, 24K and 32K fit; 48K and 64K do not");
  // Exact boundary: a capacity of 48 KB serves 48 KB.
  MemoryMonitor::setNativeTestAllocatableCapacity(48u * 1024u);
  const MemoryMonitor::HeapCapacityLadder wide =
      MemoryMonitor::probeHeapCapacityLadder(48u * 1024u);
  TEST_ASSERT_EQUAL_UINT32(48u * 1024u, wide.largestAllocatableBytes);
  TEST_ASSERT_EQUAL_UINT8(0x0Fu, wide.servedMask);
  MemoryMonitor::resetNativeTestFreeHeap();
  MemoryMonitor::resetNativeTestAllocatableCapacity();
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_classify_raw_normal);
  RUN_TEST(test_classify_raw_low_heap);
  RUN_TEST(test_classify_raw_critical_heap);
  RUN_TEST(test_classify_raw_critical_chunks);
  RUN_TEST(test_classify_raw_critical_append_latch);
  RUN_TEST(test_step_escalates_immediately);
  RUN_TEST(test_step_low_to_normal_requires_hysteresis);
  RUN_TEST(test_step_critical_to_low_requires_exit_band);
  RUN_TEST(test_advisory_update_transitions_without_reclaim);
  RUN_TEST(test_heap_allocatability_probe_separates_tail_from_a_servable_request);
  RUN_TEST(test_tail_low_but_free_list_servable_admits_the_start);
  RUN_TEST(test_denial_edge_capacity_ladder_reports_the_largest_servable_rung);
  return UNITY_END();
}
