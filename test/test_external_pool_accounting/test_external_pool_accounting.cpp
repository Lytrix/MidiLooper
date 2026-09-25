#include <unity.h>
#define EXTERNAL_POOL_ACCOUNTING_NATIVE_TEST
#define EXTERNAL_POOL_WRAP
#include "../../src/Utils/ExternalPoolAccounting.cpp"

static void* nextPointer;
static void* freedPointer;
extern "C" void* __real_extmem_malloc(size_t) { return nextPointer; }
extern "C" void __real_extmem_free(void* pointer) { freedPointer = pointer; }
using namespace ExternalPoolAccounting;
static void* external(size_t i) { return reinterpret_cast<void*>(0x70001000u + i * 16); }
void setUp() { reset(); nextPointer = nullptr; freedPointer = nullptr; }
void tearDown() {}

void test_wrapper_balances_original_tag() {
  nextPointer = external(0);
  beginTag(TagViewPrewarm);
  TEST_ASSERT_EQUAL_PTR(nextPointer, __wrap_extmem_malloc(8192));
  endTag();
  TEST_ASSERT_EQUAL_UINT32(8192, counters().liveBytes);
  beginTag(TagCaptureStaging);
  __wrap_extmem_free(nextPointer);
  endTag();
  TEST_ASSERT_EQUAL_PTR(nextPointer, freedPointer);
  TEST_ASSERT_EQUAL_UINT32(0, counters().liveBytes);
  TEST_ASSERT_EQUAL_UINT32(0, counters().tagBytes[TagViewPrewarm]);
  TEST_ASSERT_EQUAL_UINT32(0, counters().unknownFrees);
}
void test_internal_fallback_is_not_pool_occupancy() {
  nextPointer = reinterpret_cast<void*>(0x20210000u);
  TEST_ASSERT_EQUAL_PTR(nextPointer, __wrap_extmem_malloc(8192));
  // Firmware allocator may call plain free here: no wrapped retirement follows.
  TEST_ASSERT_EQUAL_UINT32(0, counters().liveBytes);
  TEST_ASSERT_EQUAL_UINT32(0, counters().trackedBlocks);
  // Core also permits extmem_free for that internal pointer.
  __wrap_extmem_free(nextPointer);
  TEST_ASSERT_EQUAL_PTR(nextPointer, freedPointer);
  TEST_ASSERT_EQUAL_UINT32(0, counters().unknownFrees);
}
void test_same_address_retires_previous_tag() {
  beginTag(TagViewPrewarm);
  recordPoolAllocation(external(0), 1024);
  endTag();
  beginTag(TagCaptureStaging);
  recordPoolAllocation(external(0), 8192);
  endTag();
  TEST_ASSERT_EQUAL_UINT32(0, counters().tagBytes[TagViewPrewarm]);
  TEST_ASSERT_EQUAL_UINT32(0, counters().tagBlocks[TagViewPrewarm]);
  TEST_ASSERT_EQUAL_UINT32(8192, counters().tagBytes[TagCaptureStaging]);
  TEST_ASSERT_EQUAL_UINT32(1, counters().liveBlocks);
  recordPoolFree(external(0));
  TEST_ASSERT_EQUAL_UINT32(0, counters().tagBytes[TagCaptureStaging]);
  TEST_ASSERT_EQUAL_UINT32(0, counters().liveBytesByClass[1]);
  TEST_ASSERT_EQUAL_UINT32(0, counters().liveBytesByClass[2]);
}
void test_saturated_table_reuses_tombstone() {
  for (size_t i = 0; i < 8192; ++i) recordPoolAllocation(external(i), 64);
  TEST_ASSERT_EQUAL_UINT32(8192, counters().liveBlocks);
  recordPoolAllocation(external(8192), 64);
  TEST_ASSERT_EQUAL_UINT32(1, counters().untrackedBlocks);
  for (size_t i = 0; i < 8192; ++i) recordPoolFree(external(i));
  recordPoolAllocation(external(8193), 128);
  TEST_ASSERT_EQUAL_UINT32(1, counters().untrackedBlocks);
  TEST_ASSERT_EQUAL_UINT32(128, counters().liveBytes);
  recordPoolFree(external(8193));
  TEST_ASSERT_EQUAL_UINT32(0, counters().liveBytes);
}
void test_null_refusal_preserves_result() {
  TEST_ASSERT_NULL(__wrap_extmem_malloc(8192));
  TEST_ASSERT_EQUAL_UINT32(1, counters().poolRefusedCount);
  TEST_ASSERT_EQUAL_UINT32(8192, counters().poolRefusedLargestBytes);
  TEST_ASSERT_EQUAL_UINT32(0, counters().liveBytes);
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_wrapper_balances_original_tag);
  RUN_TEST(test_internal_fallback_is_not_pool_occupancy);
  RUN_TEST(test_same_address_retires_previous_tag);
  RUN_TEST(test_saturated_table_reuses_tombstone);
  RUN_TEST(test_null_refusal_preserves_result);
  return UNITY_END();
}
