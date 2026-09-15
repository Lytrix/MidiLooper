// Track state-machine invariants for deferred overdub finalization.

#include <unity.h>

#include "../../src/Track/TrackStateMachine.cpp"

void test_overdub_can_enter_deferred_stop() {
  TEST_ASSERT_TRUE(TrackStateMachine::isValidTransition(
      TRACK_OVERDUBBING, TRACK_OVERDUB_STOPPING));
  TEST_ASSERT_EQUAL_STRING("OVERDUB_STOPPING",
                           TrackStateMachine::toString(TRACK_OVERDUB_STOPPING));
}

void test_deferred_stop_allows_only_requested_transport_destinations() {
  TEST_ASSERT_TRUE(TrackStateMachine::isValidTransition(
      TRACK_OVERDUB_STOPPING, TRACK_PLAYING));
  TEST_ASSERT_TRUE(TrackStateMachine::isValidTransition(
      TRACK_OVERDUB_STOPPING, TRACK_STOPPED));
  TEST_ASSERT_FALSE(TrackStateMachine::isValidTransition(
      TRACK_OVERDUB_STOPPING, TRACK_OVERDUBBING));
  TEST_ASSERT_FALSE(TrackStateMachine::isValidTransition(
      TRACK_OVERDUB_STOPPING, TRACK_RECORDING));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_overdub_can_enter_deferred_stop);
  RUN_TEST(test_deferred_stop_allows_only_requested_transport_destinations);
  return UNITY_END();
}
