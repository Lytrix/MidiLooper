//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "Globals.h"
#include "Utils/DeferredValidatePolicy.h"

void test_deferred_validate_waits_until_delay_elapsed() {
  const uint32_t queuedAt = 1000u;
  const uint32_t before = queuedAt + Config::deferredValidateMaxDelayMs - 1u;
  TEST_ASSERT_FALSE(DeferredValidatePolicy::shouldRunDeferredFullValidate(true, false, false, false,
                                                                          queuedAt, before));
  const uint32_t after = queuedAt + Config::deferredValidateMaxDelayMs;
  TEST_ASSERT_TRUE(DeferredValidatePolicy::shouldRunDeferredFullValidate(true, false, false, false,
                                                                         queuedAt, after));
}

void test_deferred_validate_blocked_during_playback_or_capture() {
  const uint32_t queuedAt = 1000u;
  const uint32_t now = Config::deferredValidateMaxDelayMs + 1000u;
  TEST_ASSERT_FALSE(
      DeferredValidatePolicy::shouldRunDeferredFullValidate(true, true, false, false, queuedAt, now));
  TEST_ASSERT_FALSE(
      DeferredValidatePolicy::shouldRunDeferredFullValidate(true, false, true, false, queuedAt, now));
  TEST_ASSERT_FALSE(
      DeferredValidatePolicy::shouldRunDeferredFullValidate(true, false, false, true, queuedAt, now));
}

void test_deferred_validate_not_triggered_when_now_is_before_queue_timestamp() {
  const uint32_t queuedAt = 10000u;
  const uint32_t now = 9990u;  // Same-loop stale now snapshot before queue assignment.
  TEST_ASSERT_FALSE(DeferredValidatePolicy::shouldRunDeferredFullValidate(true, false, false, false,
                                                                          queuedAt, now));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_deferred_validate_waits_until_delay_elapsed);
  RUN_TEST(test_deferred_validate_blocked_during_playback_or_capture);
  RUN_TEST(test_deferred_validate_not_triggered_when_now_is_before_queue_timestamp);
  return UNITY_END();
}
