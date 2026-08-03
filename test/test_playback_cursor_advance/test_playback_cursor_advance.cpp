//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>
#include <cstdint>
#include <vector>

#include "MidiEvent.h"
#include "Utils/IntervalProjection.h"
#include "Utils/PlaybackCursorAdvance.h"

#include "../../src/Logger.cpp"
#include "../../src/Utils/IntervalProjection.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/Utils/PlaybackCursorAdvance.cpp"

namespace {

struct DirectPlaybackStreamCtx {
  std::vector<MidiEvent> events;
};

size_t directPlaybackStreamSize(const void* ctx) {
  return static_cast<const DirectPlaybackStreamCtx*>(ctx)->events.size();
}

const MidiEvent& directPlaybackStreamEventAt(const void* ctx, uint16_t cursor) {
  return static_cast<const DirectPlaybackStreamCtx*>(ctx)->events[cursor];
}

uint32_t directPlaybackStreamPhase(const MidiEvent& evt, const ProjectionContext& /*playbackContext*/) {
  return evt.tick;
}

MidiEvent makePhaseEvent(uint32_t phase) {
  MidiEvent evt;
  evt.tick = phase;
  evt.type = midi::NoteOn;
  evt.channel = 1;
  evt.data.noteData.note = static_cast<uint8_t>(phase % 128);
  evt.data.noteData.velocity = 100;
  return evt;
}

struct SendLog {
  std::vector<uint32_t> sentPhases;
};

void recordSend(void* ctx, const MidiEvent& evt, uint8_t /*slotIndex*/) {
  static_cast<SendLog*>(ctx)->sentPhases.push_back(evt.tick);
}

}  // namespace

void test_playback_cursor_advance_mid_interval() {
  DirectPlaybackStreamCtx streamCtx{{makePhaseEvent(10), makePhaseEvent(20), makePhaseEvent(30)}};
  ProjectionContext playbackContext{};
  playbackContext.loopLength = 96;
  PlaybackTickFrame frame{&playbackContext, 25U, 15U, false};
  uint16_t cursor = 0;
  PlaybackCursorAdvanceState advance{&cursor, nullptr};
  SendLog sendLog;
  const PlaybackEventStream stream{&streamCtx, directPlaybackStreamSize, nullptr,
                                   directPlaybackStreamEventAt, directPlaybackStreamPhase};
  const PlaybackAdvanceResult result = advancePlaybackCursor(
      advance, frame, PlaybackEmitPolicy::LayeredSlot, stream, recordSend, &sendLog, 0, nullptr,
      nullptr, 1);
  TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed, result);
  TEST_ASSERT_EQUAL_UINT32(1, sendLog.sentPhases.size());
  TEST_ASSERT_EQUAL_UINT32(20U, sendLog.sentPhases[0]);
  TEST_ASSERT_EQUAL_UINT16(2, cursor);
}

void test_playback_cursor_advance_loop_start_catch_up() {
  DirectPlaybackStreamCtx streamCtx{
      {makePhaseEvent(0), makePhaseEvent(2), makePhaseEvent(5), makePhaseEvent(6)}};
  ProjectionContext playbackContext{};
  playbackContext.loopLength = 96;
  PlaybackTickFrame frame{&playbackContext, 5U, 95U, true};
  uint16_t cursor = 0;
  PlaybackCursorAdvanceState advance{&cursor, nullptr};
  SendLog sendLog;
  const PlaybackEventStream stream{&streamCtx, directPlaybackStreamSize, nullptr,
                                   directPlaybackStreamEventAt, directPlaybackStreamPhase};
  const PlaybackAdvanceResult result = advancePlaybackCursor(
      advance, frame, PlaybackEmitPolicy::LayeredSlot, stream, recordSend, &sendLog, 0, nullptr,
      nullptr, 1);
  TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed, result);
  TEST_ASSERT_EQUAL_UINT32(3, sendLog.sentPhases.size());
  TEST_ASSERT_EQUAL_UINT32(0U, sendLog.sentPhases[0]);
  TEST_ASSERT_EQUAL_UINT32(2U, sendLog.sentPhases[1]);
  TEST_ASSERT_EQUAL_UINT32(5U, sendLog.sentPhases[2]);
  TEST_ASSERT_EQUAL_UINT16(3, cursor);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_playback_cursor_advance_mid_interval);
  RUN_TEST(test_playback_cursor_advance_loop_start_catch_up);
  return UNITY_END();
}
