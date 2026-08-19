//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>
#include <cstdint>
#include <vector>

#include "MidiEvent.h"
#include "Utils/IntervalProjection.h"
#include "Utils/PlaybackCursorAdvance.h"
#include "ActiveNoteLedger.h"

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

void test_active_committed_duplicate_off_applies_ledger_without_second_midi() {
  // session_20260819_103234 L6683: two Off@71. Wire keeps one Off; ledger must pop both.
  MidiEvent on24 = MidiEvent::NoteOn(24, 1, 24, 100);
  on24.noteId = 5568;
  MidiEvent on48 = MidiEvent::NoteOn(48, 1, 24, 100);
  on48.noteId = 5570;
  MidiEvent off71a = MidiEvent::NoteOff(71, 1, 24, 0);
  MidiEvent off71b = MidiEvent::NoteOff(71, 1, 24, 0);
  DirectPlaybackStreamCtx streamCtx{{on24, on48, off71a, off71b}};
  ProjectionContext playbackContext{};
  playbackContext.loopLength = 768;
  PlaybackTickFrame frame{&playbackContext, 71U, 0U, false};
  uint16_t cursor = 0;
  PlaybackCursorAdvanceState advance{&cursor, nullptr};

  struct LedgerSendLog {
    ActiveNoteLedger ledger;
    unsigned midiOffs = 0;
  };
  LedgerSendLog log;
  auto send = [](void* ctx, const MidiEvent& evt, uint8_t) {
    auto* state = static_cast<LedgerSendLog*>(ctx);
    (void)state->ledger.applyPlaybackEvent(1, evt);
    if (evt.isNoteOff()) {
      ++state->midiOffs;
    }
  };
  auto applyLedgerOnly = [](void* ctx, const MidiEvent& evt, uint8_t) {
    auto* state = static_cast<LedgerSendLog*>(ctx);
    (void)state->ledger.applyPlaybackEvent(1, evt);
  };

  const PlaybackEventStream stream{&streamCtx, directPlaybackStreamSize, nullptr,
                                   directPlaybackStreamEventAt, directPlaybackStreamPhase};
  const PlaybackAdvanceResult result = advancePlaybackCursor(
      advance, frame, PlaybackEmitPolicy::ActiveCommitted, stream, send, &log, 0, nullptr, nullptr,
      1, applyLedgerOnly);
  TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed, result);
  TEST_ASSERT_EQUAL_UINT32(1, log.midiOffs);
  TEST_ASSERT_FALSE(log.ledger.isActive(1, 24));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_playback_cursor_advance_mid_interval);
  RUN_TEST(test_playback_cursor_advance_loop_start_catch_up);
  RUN_TEST(test_active_committed_duplicate_off_applies_ledger_without_second_midi);
  return UNITY_END();
}
