//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "Utils/MidiButtonGesturePolicy.h"

void test_orphan_note_off_does_not_dispatch_short_press() {
  TEST_ASSERT_FALSE(MidiButtonGesturePolicy::kDispatchOrphanNoteOffAsShortPress);
  TEST_ASSERT_FALSE(MidiButtonGesturePolicy::mayDispatchActionFromOrphanNoteOff());
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_orphan_note_off_does_not_dispatch_short_press);
  return UNITY_END();
}
