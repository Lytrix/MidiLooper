//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/Utils/MemoryPressurePolicy.cpp"
#include "../../src/Utils/MemoryMonitor.cpp"

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
  return UNITY_END();
}
