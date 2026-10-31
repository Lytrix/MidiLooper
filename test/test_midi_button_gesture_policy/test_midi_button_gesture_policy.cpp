//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "Utils/MidiButtonGesturePolicy.h"

void test_orphan_note_off_does_not_dispatch_short_press() {
  TEST_ASSERT_FALSE(MidiButtonGesturePolicy::kDispatchOrphanNoteOffAsShortPress);
  TEST_ASSERT_FALSE(MidiButtonGesturePolicy::mayDispatchActionFromOrphanNoteOff());
}

// The HITL verified-clear path retries a CLEAR until the device names an outcome, and it accepts an
// already-empty slot only from this row. A silent return on that shape made a successful clear of an
// already-cleared slot look like a refusal (`midifile-chain-43`, 2026-10-30).
void test_clear_slot_outcome_names_already_empty() {
  using MidiButtonGesturePolicy::ClearSlotOutcome;
  using MidiButtonGesturePolicy::classifyClearSlotOutcome;
  TEST_ASSERT_EQUAL(ClearSlotOutcome::ClearRam, classifyClearSlotOutcome(true, true));
  TEST_ASSERT_EQUAL(ClearSlotOutcome::ClearRam, classifyClearSlotOutcome(true, false));
  TEST_ASSERT_EQUAL(ClearSlotOutcome::RemovePersistedPayload,
                    classifyClearSlotOutcome(false, true));
  TEST_ASSERT_EQUAL(ClearSlotOutcome::AlreadyEmpty, classifyClearSlotOutcome(false, false));

  // Pin the exact text `scripts/hitl_midifile_chain.py::_classify_clear` matches:
  //   `Clear ignored .*selected slot is empty \(track \d+ slot (\d+)\)`
  TEST_ASSERT_EQUAL_STRING(
      "Clear ignored — selected slot is empty (track %u slot %u)",
      MidiButtonGesturePolicy::kClearIgnoredEmptySlotLogFormat);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_orphan_note_off_does_not_dispatch_short_press);
  RUN_TEST(test_clear_slot_outcome_names_already_empty);
  return UNITY_END();
}
