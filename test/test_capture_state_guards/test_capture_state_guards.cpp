//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/EditManager/EditApply.cpp"
#include "../../src/Loop/LoopPasses.cpp"
#include "../test_support/MemoryMonitorNativeDeps.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/Loop.cpp"
#include "../test_support/LoopCaptureTestDeps.cpp"
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

void test_capture_preview_updates_revision_once_and_closes_changed_note() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 1536;
  loop.beginCapture(CapturePhase::Overdub);
  const uint32_t startRevision = loop.captureDisplayRevision;

  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(192, 4, 60, 100)));
  TEST_ASSERT_EQUAL_UINT32(startRevision + 1, loop.captureDisplayRevision);
  TEST_ASSERT_EQUAL(1u, loop.capturePreview.notes.size());
  TEST_ASSERT_EQUAL(1u, loop.capturePreview.noteStates.size());
  TEST_ASSERT_EQUAL(1u, loop.capturePreview.openNoteIndices.size());

  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(384, 4, 60, 0)));
  TEST_ASSERT_EQUAL_UINT32(startRevision + 2, loop.captureDisplayRevision);
  TEST_ASSERT_EQUAL_UINT32(384, loop.capturePreview.notes[0].endTick);
  TEST_ASSERT_FALSE(loop.capturePreview.noteStates[0].open);
  TEST_ASSERT_EQUAL(0u, loop.capturePreview.openNoteIndices.size());
  TEST_ASSERT_EQUAL_UINT32(0, loop.capturePreview.changedNoteIndices.back());
}

void test_capture_preview_wrap_head_uses_latest_same_channel_tail() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 1536;
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(1300, 4, 60, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(1400, 4, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(100, 4, 60, 0)));

  TEST_ASSERT_TRUE(loop.capturePreview.noteStates[0].open);
  TEST_ASSERT_FALSE(loop.capturePreview.noteStates[0].wrapHeld);
  TEST_ASSERT_FALSE(loop.capturePreview.noteStates[1].open);
  TEST_ASSERT_TRUE(loop.capturePreview.noteStates[1].wrapHeld);
  TEST_ASSERT_TRUE(loop.capturePreview.noteStates[1].hasPreferredHeadOff);
  TEST_ASSERT_EQUAL_UINT32(100, loop.capturePreview.noteStates[1].preferredHeadOffTick);
  TEST_ASSERT_EQUAL_UINT32(1535, loop.capturePreview.notes[1].endTick);
  TEST_ASSERT_EQUAL(3u, loop.capturePreview.notes.size());
  TEST_ASSERT_EQUAL_UINT32(0, loop.capturePreview.notes[2].startTick);
  TEST_ASSERT_EQUAL_UINT32(100, loop.capturePreview.notes[2].endTick);
}

void test_capture_preview_wrap_head_does_not_close_other_channel_tail() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 1536;
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(1400, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(100, 2, 60, 0)));

  TEST_ASSERT_TRUE(loop.capturePreview.noteStates[0].open);
  TEST_ASSERT_FALSE(loop.capturePreview.noteStates[0].wrapHeld);
  TEST_ASSERT_EQUAL_UINT32(1400, loop.capturePreview.notes[0].endTick);
  TEST_ASSERT_EQUAL(1u, loop.capturePreview.openNoteIndices.size());
}

void test_capture_preview_wrap_head_zero_keeps_tail_only() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 1536;
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(1400, 4, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(0, 4, 60, 0)));

  TEST_ASSERT_TRUE(loop.capturePreview.noteStates[0].wrapHeld);
  TEST_ASSERT_TRUE(loop.capturePreview.noteStates[0].hasPreferredHeadOff);
  TEST_ASSERT_EQUAL_UINT32(0, loop.capturePreview.noteStates[0].preferredHeadOffTick);
  TEST_ASSERT_EQUAL_UINT32(1535, loop.capturePreview.notes[0].endTick);
  TEST_ASSERT_EQUAL(1u, loop.capturePreview.notes.size());
}

void test_capture_preview_head_off_pairs_after_loop_end_close() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 1536;
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(1400, 4, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(1535, 4, 60, 0)));
  TEST_ASSERT_FALSE(loop.capturePreview.noteStates[0].open);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(100, 4, 60, 0)));

  TEST_ASSERT_TRUE(loop.capturePreview.noteStates[0].wrapHeld);
  TEST_ASSERT_TRUE(loop.capturePreview.noteStates[0].hasPreferredHeadOff);
  TEST_ASSERT_EQUAL_UINT32(100, loop.capturePreview.noteStates[0].preferredHeadOffTick);
  TEST_ASSERT_EQUAL(2u, loop.capturePreview.notes.size());
  TEST_ASSERT_EQUAL_UINT32(0, loop.capturePreview.notes[1].startTick);
  TEST_ASSERT_EQUAL_UINT32(100, loop.capturePreview.notes[1].endTick);
}

void test_capture_preview_tracks_multiple_open_notes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 1536;
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(192, 4, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(240, 4, 64, 90)));

  TEST_ASSERT_EQUAL(2u, loop.capturePreview.openNoteIndices.size());
  TEST_ASSERT_TRUE(loop.capturePreview.noteStates[0].open);
  TEST_ASSERT_TRUE(loop.capturePreview.noteStates[1].open);
}

void test_capture_preview_orphan_off_does_not_stretch_closed_tick_zero_note() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(0, 4, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(0, 4, 60, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(480, 4, 60, 0)));

  TEST_ASSERT_EQUAL(1u, loop.capturePreview.notes.size());
  TEST_ASSERT_EQUAL_UINT32(0, loop.capturePreview.notes[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(0, loop.capturePreview.notes[0].endTick);
  TEST_ASSERT_FALSE(loop.capturePreview.noteStates[0].open);
  TEST_ASSERT_EQUAL(0u, loop.capturePreview.openNoteIndices.size());
}

void test_capture_preview_orphan_off_does_not_stretch_repeated_closed_note() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(0, 4, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(0, 4, 60, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(48, 4, 60, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(48, 4, 60, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(96, 4, 60, 0)));

  TEST_ASSERT_EQUAL(2u, loop.capturePreview.notes.size());
  TEST_ASSERT_EQUAL_UINT32(0, loop.capturePreview.notes[0].endTick);
  TEST_ASSERT_EQUAL_UINT32(48, loop.capturePreview.notes[1].endTick);
  TEST_ASSERT_FALSE(loop.capturePreview.noteStates[0].open);
  TEST_ASSERT_FALSE(loop.capturePreview.noteStates[1].open);
  TEST_ASSERT_EQUAL(0u, loop.capturePreview.openNoteIndices.size());
}

void test_capture_preview_cold_rebuild_open_state_accepts_later_note_off() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 1536;
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(192, 4, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(240, 4, 64, 90)));

  rebuildCapturePreviewFromStore(loop);
  TEST_ASSERT_EQUAL(2u, loop.capturePreview.noteStates.size());
  TEST_ASSERT_EQUAL(2u, loop.capturePreview.openNoteIndices.size());
  TEST_ASSERT_TRUE(loop.capturePreview.noteStates[0].open);
  TEST_ASSERT_TRUE(loop.capturePreview.noteStates[1].open);

  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(384, 4, 60, 0)));
  TEST_ASSERT_FALSE(loop.capturePreview.noteStates[0].open);
  TEST_ASSERT_TRUE(loop.capturePreview.noteStates[1].open);
  TEST_ASSERT_EQUAL_UINT32(384, loop.capturePreview.notes[0].endTick);
  TEST_ASSERT_EQUAL(1u, loop.capturePreview.openNoteIndices.size());
}

void test_capture_preview_cold_rebuild_restores_preferred_wrap_head() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 1536;
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(1400, 4, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(1535, 4, 60, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(100, 4, 60, 0)));

  loop.ensureCaptureEventsSorted();
  rebuildCapturePreviewFromStore(loop);

  bool foundWrapTail = false;
  for (size_t noteIndex = 0; noteIndex < loop.capturePreview.notes.size();
       ++noteIndex) {
    const auto& note = loop.capturePreview.notes[noteIndex];
    const auto& state = loop.capturePreview.noteStates[noteIndex];
    if (note.startTick != 1400) {
      continue;
    }
    foundWrapTail = true;
    TEST_ASSERT_FALSE(state.open);
    TEST_ASSERT_TRUE(state.wrapHeld);
    TEST_ASSERT_TRUE(state.hasPreferredHeadOff);
    TEST_ASSERT_EQUAL_UINT32(100, state.preferredHeadOffTick);
  }
  TEST_ASSERT_TRUE(foundWrapTail);
  TEST_ASSERT_EQUAL(loop.capturePreview.notes.size(),
                    loop.capturePreview.noteStates.size());
  TEST_ASSERT_EQUAL(0u, loop.capturePreview.openNoteIndices.size());
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_second_overdub_begin_capture_skipped_preserves_store);
  RUN_TEST(test_first_overdub_begin_capture_clears_store);
  RUN_TEST(test_capture_preview_updates_revision_once_and_closes_changed_note);
  RUN_TEST(test_capture_preview_wrap_head_uses_latest_same_channel_tail);
  RUN_TEST(test_capture_preview_wrap_head_does_not_close_other_channel_tail);
  RUN_TEST(test_capture_preview_wrap_head_zero_keeps_tail_only);
  RUN_TEST(test_capture_preview_head_off_pairs_after_loop_end_close);
  RUN_TEST(test_capture_preview_tracks_multiple_open_notes);
  RUN_TEST(test_capture_preview_orphan_off_does_not_stretch_closed_tick_zero_note);
  RUN_TEST(test_capture_preview_orphan_off_does_not_stretch_repeated_closed_note);
  RUN_TEST(test_capture_preview_cold_rebuild_open_state_accepts_later_note_off);
  RUN_TEST(test_capture_preview_cold_rebuild_restores_preferred_wrap_head);
  return UNITY_END();
}
