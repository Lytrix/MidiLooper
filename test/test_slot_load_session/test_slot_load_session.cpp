//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/StorageManager/SlotLoadSession.cpp"
#include "../test_support/MemoryMonitorNativeDeps.cpp"

#include "SlotLoadSession.h"

void test_slot_load_session_advance_to_completed() {
  TEST_ASSERT_FALSE(SlotLoadSession::isActive());
  {
    SlotLoadSession session(1, 3);
    TEST_ASSERT_TRUE(SlotLoadSession::isActive());
    TEST_ASSERT_TRUE(SlotLoadSession::isActiveFor(1, 3));
    TEST_ASSERT_EQUAL(static_cast<int>(SlotLoadSessionState::Dequeued),
                      static_cast<int>(session.state()));

    TEST_ASSERT_EQUAL(static_cast<int>(SlotLoadAdvanceResult::MoreWork),
                      static_cast<int>(session.advanceAfterPhaseWork()));
    TEST_ASSERT_EQUAL(static_cast<int>(SlotLoadSessionState::Reading),
                      static_cast<int>(session.state()));

    TEST_ASSERT_EQUAL(static_cast<int>(SlotLoadAdvanceResult::MoreWork),
                      static_cast<int>(session.advanceAfterPhaseWork()));
    TEST_ASSERT_EQUAL(static_cast<int>(SlotLoadSessionState::Validating),
                      static_cast<int>(session.state()));

    TEST_ASSERT_EQUAL(static_cast<int>(SlotLoadAdvanceResult::MoreWork),
                      static_cast<int>(session.advanceAfterPhaseWork()));
    TEST_ASSERT_EQUAL(static_cast<int>(SlotLoadSessionState::Committing),
                      static_cast<int>(session.state()));

    TEST_ASSERT_EQUAL(static_cast<int>(SlotLoadAdvanceResult::Completed),
                      static_cast<int>(session.advanceAfterPhaseWork()));
    TEST_ASSERT_TRUE(session.isTerminal());
    TEST_ASSERT_EQUAL(static_cast<int>(SlotLoadSessionState::Completed),
                      static_cast<int>(session.state()));
  }
  TEST_ASSERT_FALSE(SlotLoadSession::isActive());
}

void test_slot_load_session_fail_is_terminal() {
  SlotLoadSession session(0, 1);
  session.setState(SlotLoadSessionState::Reading);
  session.fail();
  TEST_ASSERT_TRUE(session.isTerminal());
  TEST_ASSERT_EQUAL(static_cast<int>(SlotLoadAdvanceResult::Failed),
                    static_cast<int>(session.advanceAfterPhaseWork()));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_slot_load_session_advance_to_completed);
  RUN_TEST(test_slot_load_session_fail_is_terminal);
  return UNITY_END();
}
