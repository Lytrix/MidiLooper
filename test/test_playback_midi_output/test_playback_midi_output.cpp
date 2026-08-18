//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Muted playback still advances. MIDI send on the track channel/ports is suppressed.

#include <unity.h>

#include <cstdint>
#include <vector>

#include "ActiveNoteLedger.h"
#include "MidiEvent.h"
#include "Utils/IntervalProjection.h"
#include "Utils/PlaybackCursorAdvance.h"
#include "Utils/PlaybackMidiOutput.h"

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

struct EngineAndMidiLog {
  std::vector<uint32_t> enginePhases;
  std::vector<uint32_t> midiPhases;
  bool sendMidi = false;
};

void recordEngineThenMaybeMidi(void* ctx, const MidiEvent& evt, uint8_t /*slotIndex*/) {
  auto* log = static_cast<EngineAndMidiLog*>(ctx);
  log->enginePhases.push_back(evt.tick);
  if (PlaybackMidiOutput::shouldSend(log->sendMidi, false)) {
    log->midiPhases.push_back(evt.tick);
  }
}

PlaybackEventStream makeStream(DirectPlaybackStreamCtx& streamCtx) {
  return PlaybackEventStream{&streamCtx, directPlaybackStreamSize, nullptr,
                             directPlaybackStreamEventAt, directPlaybackStreamPhase};
}

PlaybackAdvanceResult advanceFrame(DirectPlaybackStreamCtx& streamCtx, uint16_t& cursor,
                                   uint32_t prevTick, uint32_t tickInLoop, bool atLoopStart,
                                   uint32_t loopLength, EngineAndMidiLog& log) {
  ProjectionContext playbackContext{};
  playbackContext.loopLength = loopLength;
  PlaybackTickFrame frame{&playbackContext, tickInLoop, prevTick, atLoopStart};
  PlaybackCursorAdvanceState advance{&cursor, nullptr};
  return advancePlaybackCursor(advance, frame, PlaybackEmitPolicy::LayeredSlot,
                               makeStream(streamCtx), recordEngineThenMaybeMidi, &log, 0, nullptr,
                               nullptr, 1);
}

}  // namespace

void test_engine_runs_when_slot_enabled_even_if_muted() {
  TEST_ASSERT_TRUE(PlaybackMidiOutput::engineShouldRun(true));
  TEST_ASSERT_FALSE(PlaybackMidiOutput::engineShouldRun(false));
}

void test_midi_send_requires_unmuted_track_and_slot() {
  TEST_ASSERT_TRUE(PlaybackMidiOutput::shouldSend(true, false));
  TEST_ASSERT_FALSE(PlaybackMidiOutput::shouldSend(false, false));
  TEST_ASSERT_FALSE(PlaybackMidiOutput::shouldSend(true, true));
  TEST_ASSERT_FALSE(PlaybackMidiOutput::shouldSend(false, true));
}

void test_ledger_stays_active_after_note_on() {
  ActiveNoteLedger ledger;
  ledger.noteOn(1, 60, 42, 100, 90);
  TEST_ASSERT_TRUE(ledger.isActive(1, 60));
  TEST_ASSERT_EQUAL_UINT32(42, ledger.noteId(1, 60));
  uint8_t seen = 0;
  ledger.forEachActive([&](uint8_t channel, uint8_t note, const ActiveNoteLedger::Entry& entry) {
    TEST_ASSERT_EQUAL_UINT8(1, channel);
    TEST_ASSERT_EQUAL_UINT8(60, note);
    TEST_ASSERT_EQUAL_UINT32(42, entry.noteId);
    TEST_ASSERT_EQUAL_UINT32(100, entry.startTick);
    ++seen;
  });
  TEST_ASSERT_EQUAL_UINT8(1, seen);
}

void test_ledger_note_on_overwrites_note_id() {
  ActiveNoteLedger ledger;
  ledger.noteOn(1, 60, 42, 100, 90);
  ledger.noteOn(1, 60, 99, 200, 80);
  TEST_ASSERT_TRUE(ledger.isActive(1, 60));
  TEST_ASSERT_EQUAL_UINT32(99, ledger.noteId(1, 60));
  ledger.noteOff(1, 60);
  TEST_ASSERT_FALSE(ledger.isActive(1, 60));
  TEST_ASSERT_EQUAL_UINT32(kInvalidNoteId, ledger.noteId(1, 60));
}

void test_ledger_apply_playback_event_before_emit() {
  ActiveNoteLedger ledger;
  MidiEvent on = MidiEvent::NoteOn(1000, 1, 60, 90);
  on.noteId = 42;
  TEST_ASSERT_TRUE(ledger.applyPlaybackEvent(1, on));
  TEST_ASSERT_TRUE(ledger.isActive(1, 60));
  TEST_ASSERT_EQUAL_UINT32(42, ledger.noteId(1, 60));

  MidiEvent orphanOff = MidiEvent::NoteOff(2000, 1, 61, 0);
  TEST_ASSERT_FALSE(ledger.applyPlaybackEvent(1, orphanOff));
  TEST_ASSERT_TRUE(ledger.isActive(1, 60));

  MidiEvent off = MidiEvent::NoteOff(9000, 1, 60, 0);
  TEST_ASSERT_TRUE(ledger.applyPlaybackEvent(1, off));
  TEST_ASSERT_FALSE(ledger.isActive(1, 60));
  TEST_ASSERT_EQUAL_UINT32(kInvalidNoteId, ledger.noteId(1, 60));
}

void test_all_notes_off_clears_ledger_mute_does_not() {
  ActiveNoteLedger ledger;
  ledger.noteOn(1, 60, 42, 100, 90);
  TEST_ASSERT_TRUE(ledger.isActive(1, 60));
  ledger.clear();
  TEST_ASSERT_FALSE(ledger.isActive(1, 60));
}

void test_cursor_advances_while_midi_send_suppressed() {
  DirectPlaybackStreamCtx streamCtx{{makePhaseEvent(10), makePhaseEvent(20), makePhaseEvent(30)}};
  uint16_t cursor = 0;
  EngineAndMidiLog log;
  log.sendMidi = false;
  TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed,
                    advanceFrame(streamCtx, cursor, 15, 25, false, 96, log));
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(log.enginePhases.size()));
  TEST_ASSERT_EQUAL_UINT32(20, log.enginePhases[0]);
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(log.midiPhases.size()));
  TEST_ASSERT_EQUAL_UINT16(2, cursor);
}

void test_unmute_does_not_resend_crossed_events() {
  DirectPlaybackStreamCtx streamCtx{{makePhaseEvent(10), makePhaseEvent(20), makePhaseEvent(30)}};
  uint16_t cursor = 0;
  EngineAndMidiLog log;
  log.sendMidi = false;
  TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed,
                    advanceFrame(streamCtx, cursor, 15, 25, false, 96, log));
  log.sendMidi = true;
  TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed,
                    advanceFrame(streamCtx, cursor, 25, 35, false, 96, log));
  TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(log.enginePhases.size()));
  TEST_ASSERT_EQUAL_UINT32(20, log.enginePhases[0]);
  TEST_ASSERT_EQUAL_UINT32(30, log.enginePhases[1]);
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(log.midiPhases.size()));
  TEST_ASSERT_EQUAL_UINT32(30, log.midiPhases[0]);
}

void test_wrap_committed_note_at_s_crosses_when_reanchored_at_prev() {
  // session_20260818_152745: wrap-committed 71 @ 656. Reanchor at prev=655 then
  // (655, 656] applies it. Reanchor after lastTick==S skips it; (656, 657] never crosses.
  constexpr uint32_t kS = 656;
  constexpr uint32_t kPrev = 655;
  constexpr uint32_t kLoop = 768;
  MidiEvent wrapOn = MidiEvent::NoteOn(kS, 1, 71, 100);
  wrapOn.noteId = 5;
  DirectPlaybackStreamCtx postWrap{{makePhaseEvent(528), wrapOn}};

  auto skipThrough = [](DirectPlaybackStreamCtx& stream, uint32_t lastTick) -> uint16_t {
    uint16_t cursor = 0;
    while (static_cast<size_t>(cursor) < stream.events.size() &&
           stream.events[cursor].tick <= lastTick) {
      ++cursor;
    }
    return cursor;
  };

  uint16_t cursorAtS = skipThrough(postWrap, kS);
  EngineAndMidiLog missLog;
  missLog.sendMidi = true;
  TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed,
                    advanceFrame(postWrap, cursorAtS, kS, kS + 1U, false, kLoop, missLog));
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(missLog.enginePhases.size()));

  uint16_t cursorAtPrev = skipThrough(postWrap, kPrev);
  EngineAndMidiLog wrapLog;
  wrapLog.sendMidi = true;
  TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed,
                    advanceFrame(postWrap, cursorAtPrev, kPrev, kS, false, kLoop, wrapLog));
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(wrapLog.enginePhases.size()));
  TEST_ASSERT_EQUAL_UINT32(kS, wrapLog.enginePhases[0]);

  ActiveNoteLedger ledger;
  TEST_ASSERT_TRUE(ledger.applyPlaybackEvent(1, wrapOn));
  TEST_ASSERT_EQUAL_UINT32(5, ledger.noteId(1, 71));
}

void test_wrap_advances_while_midi_send_suppressed() {
  DirectPlaybackStreamCtx streamCtx{{makePhaseEvent(5), makePhaseEvent(90)}};
  uint16_t cursor = 0;
  EngineAndMidiLog log;
  log.sendMidi = false;
  TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed,
                    advanceFrame(streamCtx, cursor, 85, 95, false, 96, log));
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(log.enginePhases.size()));
  TEST_ASSERT_EQUAL_UINT32(90, log.enginePhases[0]);
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(log.midiPhases.size()));

  cursor = 0;
  TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed,
                    advanceFrame(streamCtx, cursor, 95, 10, true, 96, log));
  TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(log.enginePhases.size()));
  TEST_ASSERT_EQUAL_UINT32(5, log.enginePhases[1]);
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(log.midiPhases.size()));

  log.sendMidi = true;
  TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed,
                    advanceFrame(streamCtx, cursor, 10, 20, false, 96, log));
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(log.midiPhases.size()));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_engine_runs_when_slot_enabled_even_if_muted);
  RUN_TEST(test_midi_send_requires_unmuted_track_and_slot);
  RUN_TEST(test_ledger_stays_active_after_note_on);
  RUN_TEST(test_ledger_note_on_overwrites_note_id);
  RUN_TEST(test_ledger_apply_playback_event_before_emit);
  RUN_TEST(test_all_notes_off_clears_ledger_mute_does_not);
  RUN_TEST(test_cursor_advances_while_midi_send_suppressed);
  RUN_TEST(test_unmute_does_not_resend_crossed_events);
  RUN_TEST(test_wrap_committed_note_at_s_crosses_when_reanchored_at_prev);
  RUN_TEST(test_wrap_advances_while_midi_send_suppressed);
  return UNITY_END();
}
