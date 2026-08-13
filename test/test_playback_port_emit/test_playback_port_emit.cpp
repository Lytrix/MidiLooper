//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Gate 2 — enabled slots run the playback engine; mute/solo/slot-mute gate the port.

#include <unity.h>

#include <cstdint>
#include <vector>

#include "ActiveNoteLedger.h"
#include "MidiEvent.h"
#include "Utils/IntervalProjection.h"
#include "Utils/PlaybackCursorAdvance.h"
#include "Utils/PlaybackPortEmit.h"

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

uint32_t directPlaybackStreamPhase(const MidiEvent& evt,
                                   const ProjectionContext& /*playbackContext*/) {
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

struct EngineAndPortLog {
  std::vector<uint32_t> enginePhases;
  std::vector<uint32_t> portPhases;
  bool emitToPort = false;
};

void recordEngineThenMaybePort(void* ctx, const MidiEvent& evt, uint8_t /*slotIndex*/) {
  auto* log = static_cast<EngineAndPortLog*>(ctx);
  log->enginePhases.push_back(evt.tick);
  if (PlaybackPortEmit::portShouldEmit(log->emitToPort, false)) {
    log->portPhases.push_back(evt.tick);
  }
}

PlaybackEventStream makeStream(DirectPlaybackStreamCtx& streamCtx) {
  return PlaybackEventStream{&streamCtx, directPlaybackStreamSize, nullptr,
                             directPlaybackStreamEventAt, directPlaybackStreamPhase};
}

PlaybackAdvanceResult advanceFrame(DirectPlaybackStreamCtx& streamCtx, uint16_t& cursor,
                                   uint32_t prevTick, uint32_t tickInLoop, bool atLoopStart,
                                   uint32_t loopLength, EngineAndPortLog& log) {
  ProjectionContext playbackContext{};
  playbackContext.loopLength = loopLength;
  PlaybackTickFrame frame{&playbackContext, tickInLoop, prevTick, atLoopStart};
  PlaybackCursorAdvanceState advance{&cursor, nullptr};
  return advancePlaybackCursor(advance, frame, PlaybackEmitPolicy::LayeredSlot,
                               makeStream(streamCtx), recordEngineThenMaybePort, &log, 0, nullptr,
                               nullptr, 1);
}

}  // namespace

void test_engine_runs_when_slot_enabled_even_if_muted() {
  TEST_ASSERT_TRUE(PlaybackPortEmit::engineShouldRun(true));
  TEST_ASSERT_FALSE(PlaybackPortEmit::engineShouldRun(false));
}

void test_port_emit_requires_audible_and_unmuted_slot() {
  TEST_ASSERT_TRUE(PlaybackPortEmit::portShouldEmit(true, false));
  TEST_ASSERT_FALSE(PlaybackPortEmit::portShouldEmit(false, false));
  TEST_ASSERT_FALSE(PlaybackPortEmit::portShouldEmit(true, true));
  TEST_ASSERT_FALSE(PlaybackPortEmit::portShouldEmit(false, true));
}

void test_ledger_stays_active_after_note_on() {
  ActiveNoteLedger ledger;
  ledger.noteOn(1, 60, 100, 90);
  TEST_ASSERT_TRUE(ledger.isActive(1, 60));
  uint8_t seen = 0;
  ledger.forEachActive([&](uint8_t channel, uint8_t note, const ActiveNoteLedger::Entry& entry) {
    TEST_ASSERT_EQUAL_UINT8(1, channel);
    TEST_ASSERT_EQUAL_UINT8(60, note);
    TEST_ASSERT_EQUAL_UINT32(100, entry.startTick);
    ++seen;
  });
  TEST_ASSERT_EQUAL_UINT8(1, seen);
}

void test_gate2_all_notes_off_clears_ledger_mute_does_not() {
  ActiveNoteLedger ledger;
  ledger.noteOn(1, 60, 100, 90);
  TEST_ASSERT_TRUE(ledger.isActive(1, 60));
  // Mute silence is port-only: ledger stays so playback observation can continue.
  TEST_ASSERT_TRUE(ledger.isActive(1, 60));
  ledger.clear();
  TEST_ASSERT_FALSE(ledger.isActive(1, 60));
}

void test_gate2_cursor_advances_while_port_suppressed() {
  DirectPlaybackStreamCtx streamCtx{{makePhaseEvent(10), makePhaseEvent(20), makePhaseEvent(30)}};
  uint16_t cursor = 0;
  EngineAndPortLog log;
  log.emitToPort = false;
  TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed,
                    advanceFrame(streamCtx, cursor, 15, 25, false, 96, log));
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(log.enginePhases.size()));
  TEST_ASSERT_EQUAL_UINT32(20, log.enginePhases[0]);
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(log.portPhases.size()));
  TEST_ASSERT_EQUAL_UINT16(2, cursor);
}

void test_gate2_unmute_does_not_resend_crossed_events() {
  DirectPlaybackStreamCtx streamCtx{{makePhaseEvent(10), makePhaseEvent(20), makePhaseEvent(30)}};
  uint16_t cursor = 0;
  EngineAndPortLog log;
  log.emitToPort = false;
  TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed,
                    advanceFrame(streamCtx, cursor, 15, 25, false, 96, log));
  log.emitToPort = true;
  TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed,
                    advanceFrame(streamCtx, cursor, 25, 35, false, 96, log));
  TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(log.enginePhases.size()));
  TEST_ASSERT_EQUAL_UINT32(20, log.enginePhases[0]);
  TEST_ASSERT_EQUAL_UINT32(30, log.enginePhases[1]);
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(log.portPhases.size()));
  TEST_ASSERT_EQUAL_UINT32(30, log.portPhases[0]);
}

void test_gate2_wrap_advances_while_port_suppressed() {
  DirectPlaybackStreamCtx streamCtx{{makePhaseEvent(5), makePhaseEvent(90)}};
  uint16_t cursor = 0;
  EngineAndPortLog log;
  log.emitToPort = false;
  TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed,
                    advanceFrame(streamCtx, cursor, 85, 95, false, 96, log));
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(log.enginePhases.size()));
  TEST_ASSERT_EQUAL_UINT32(90, log.enginePhases[0]);
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(log.portPhases.size()));

  cursor = 0;
  TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed,
                    advanceFrame(streamCtx, cursor, 95, 10, true, 96, log));
  TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(log.enginePhases.size()));
  TEST_ASSERT_EQUAL_UINT32(5, log.enginePhases[1]);
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(log.portPhases.size()));

  log.emitToPort = true;
  TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed,
                    advanceFrame(streamCtx, cursor, 10, 20, false, 96, log));
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(log.portPhases.size()));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_engine_runs_when_slot_enabled_even_if_muted);
  RUN_TEST(test_port_emit_requires_audible_and_unmuted_slot);
  RUN_TEST(test_ledger_stays_active_after_note_on);
  RUN_TEST(test_gate2_all_notes_off_clears_ledger_mute_does_not);
  RUN_TEST(test_gate2_cursor_advances_while_port_suppressed);
  RUN_TEST(test_gate2_unmute_does_not_resend_crossed_events);
  RUN_TEST(test_gate2_wrap_advances_while_port_suppressed);
  return UNITY_END();
}
