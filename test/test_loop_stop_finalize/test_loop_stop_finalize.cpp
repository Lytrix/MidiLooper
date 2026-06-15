//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>
#include <algorithm>

#include "Utils/LoopStopFinalize.h"
#include "MidiEvent.h"

static bool hasNoteOffAt(const MidiEventVec& events, uint32_t tick, uint8_t ch, uint8_t note) {
  for (const MidiEvent& evt : events) {
    if (evt.isNoteOff() && evt.tick == tick && evt.channel == ch &&
        evt.data.noteData.note == note) {
      return true;
    }
  }
  return false;
}

void test_finalize_inserts_off_for_open_tail_note_on() {
  const uint32_t loopLen = 768 * 32;
  const uint32_t wrapWindow = 768;
  const uint32_t tailOnTick = loopLen - 100;

  MidiEventVec events;
  events.push_back(MidiEvent::NoteOn(tailOnTick, 5, 60, 100));

  const LoopStopFinalize::Result result =
      LoopStopFinalize::finalizeWrapWindow(events, loopLen, UINT32_MAX, wrapWindow);

  TEST_ASSERT_EQUAL(1u, result.syntheticOffsInserted);
  TEST_ASSERT_TRUE(hasNoteOffAt(events, loopLen - 1, 5, 60));
}

void test_finalize_skips_wrapped_tail_on_head_off_pair() {
  const uint32_t loopLen = 768 * 32;
  const uint32_t wrapWindow = 768;
  const uint32_t tailOnTick = loopLen - 50;

  MidiEventVec events;
  events.push_back(MidiEvent::NoteOff(40, 5, 60, 0));
  events.push_back(MidiEvent::NoteOn(tailOnTick, 5, 60, 100));

  const LoopStopFinalize::Result result =
      LoopStopFinalize::finalizeWrapWindow(events, loopLen, UINT32_MAX, wrapWindow);

  TEST_ASSERT_EQUAL(0u, result.syntheticOffsInserted);
  TEST_ASSERT_EQUAL(2u, events.size());
}

void test_finalize_uses_playhead_close_tick_on_overdub_stop() {
  const uint32_t loopLen = 768 * 8;
  const uint32_t wrapWindow = 768;
  const uint32_t tailOnTick = loopLen - 200;
  const uint32_t closeTick = loopLen - 50;

  MidiEventVec events;
  events.push_back(MidiEvent::NoteOn(tailOnTick, 5, 72, 90));

  const LoopStopFinalize::Result result =
      LoopStopFinalize::finalizeWrapWindow(events, loopLen, closeTick, wrapWindow);

  TEST_ASSERT_EQUAL(1u, result.syntheticOffsInserted);
  TEST_ASSERT_TRUE(hasNoteOffAt(events, closeTick, 5, 72));
}

void test_finalize_noop_when_loop_empty() {
  MidiEventVec events;
  const LoopStopFinalize::Result result =
      LoopStopFinalize::finalizeWrapWindow(events, 768, UINT32_MAX, 768);
  TEST_ASSERT_EQUAL(0u, result.syntheticOffsInserted);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_finalize_inserts_off_for_open_tail_note_on);
  RUN_TEST(test_finalize_skips_wrapped_tail_on_head_off_pair);
  RUN_TEST(test_finalize_uses_playhead_close_tick_on_overdub_stop);
  RUN_TEST(test_finalize_noop_when_loop_empty);
  return UNITY_END();
}
