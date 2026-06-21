//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "Globals.h"
#include "Utils/DeferredValidatePolicy.h"

void test_deferred_validate_waits_until_delay_elapsed() {
  const uint32_t queuedAt = 1000u;
  const uint32_t before = queuedAt + Config::deferredValidateMaxDelayMs - 1u;
  TEST_ASSERT_FALSE(DeferredValidatePolicy::shouldRunDeferredFullValidate(true, false, false,
                                                                           queuedAt, before));
  const uint32_t after = queuedAt + Config::deferredValidateMaxDelayMs;
  TEST_ASSERT_TRUE(DeferredValidatePolicy::shouldRunDeferredFullValidate(true, false, false,
                                                                          queuedAt, after));
}

void test_deferred_validate_blocked_during_capture() {
  const uint32_t queuedAt = 0u;
  const uint32_t now = Config::deferredValidateMaxDelayMs + 1000u;
  TEST_ASSERT_FALSE(
      DeferredValidatePolicy::shouldRunDeferredFullValidate(true, true, false, queuedAt, now));
  TEST_ASSERT_FALSE(
      DeferredValidatePolicy::shouldRunDeferredFullValidate(true, false, true, queuedAt, now));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_deferred_validate_waits_until_delay_elapsed);
  RUN_TEST(test_deferred_validate_blocked_during_capture);
  return UNITY_END();
}
