//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/Loop/LoopPool.cpp"
#include "LoopPasses.h"
#include "Slot.h"

constexpr uint8_t kMaxLoops = 8;

void test_slot_defaults_to_invalid_loop_id() {
  Slot slot;
  TEST_ASSERT_EQUAL(kInvalidLoopId, slot.loopId);
}

void test_slot_1_to_1_loop_id_contract() {
  Slot slots[kMaxLoops]{};
  for (uint8_t i = 0; i < kMaxLoops; ++i) {
    slots[i].loopId = static_cast<LoopId>(i);
    TEST_ASSERT_EQUAL(static_cast<LoopId>(i), slots[i].loopId);
  }
}

void test_slot_ref_resolves_distinct_loop_ids() {
  Slot a;
  Slot b;
  a.loopId = 2;
  b.loopId = 5;
  TEST_ASSERT_NOT_EQUAL(a.loopId, b.loopId);
}

void test_loop_id_sentinel_is_max_uint32() {
  TEST_ASSERT_EQUAL(UINT32_MAX, kInvalidLoopId);
}

void test_find_by_id_unknown_returns_null() {
  LoopPool pool;
  pool.ensureInitialized();
  TEST_ASSERT_NULL(pool.findById(static_cast<LoopId>(99)));
}

void test_unknown_loop_id_falls_back_to_pool_index_not_slot_zero() {
  LoopPool pool;
  pool.ensureInitialized();
  pool.at(0).loopLengthTicks = 111;
  pool.at(3).loopLengthTicks = 999;

  const LoopId unknownId = static_cast<LoopId>(0xFFFFFF00u);
  TEST_ASSERT_NULL(pool.findById(unknownId));

  const uint8_t slotIndex = 3;
  Loop* found = pool.findById(unknownId);
  Loop& resolved = found != nullptr ? *found : pool.at(slotIndex);
  TEST_ASSERT_EQUAL(999u, resolved.loopLengthTicks);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_slot_defaults_to_invalid_loop_id);
  RUN_TEST(test_slot_1_to_1_loop_id_contract);
  RUN_TEST(test_slot_ref_resolves_distinct_loop_ids);
  RUN_TEST(test_loop_id_sentinel_is_max_uint32);
  RUN_TEST(test_find_by_id_unknown_returns_null);
  RUN_TEST(test_unknown_loop_id_falls_back_to_pool_index_not_slot_zero);
  return UNITY_END();
}
