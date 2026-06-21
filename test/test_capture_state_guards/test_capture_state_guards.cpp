//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/EditApply.cpp"
#include "../../src/LoopPasses.cpp"
#include "../../src/Loop.cpp"
#include "Loop.h"
#include "MidiEvent.h"

void test_second_overdub_begin_capture_skipped_preserves_store() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 768;
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(10, 1, 60, 100)));
  const size_t before = loop.capture.store.size();

  const bool alreadyOverdubbing = loop.capture.phase == CapturePhase::Overdub;
  if (!alreadyOverdubbing) {
    loop.beginCapture(CapturePhase::Overdub);
  }

  TEST_ASSERT_EQUAL(before, loop.capture.store.size());
  TEST_ASSERT_EQUAL(1u, loop.capture.store.size());
}

void test_first_overdub_begin_capture_clears_store() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(5, 1, 64, 90)));
  TEST_ASSERT_EQUAL(1u, loop.capture.store.size());

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_EQUAL(0u, loop.capture.store.size());
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(CapturePhase::Overdub),
                    static_cast<uint8_t>(loop.capture.phase));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_second_overdub_begin_capture_skipped_preserves_store);
  RUN_TEST(test_first_overdub_begin_capture_clears_store);
  return UNITY_END();
}
