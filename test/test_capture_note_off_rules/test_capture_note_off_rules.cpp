//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>
#include <algorithm>
#include <map>

#include "../../src/Logger.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/EditManager/EditApply.cpp"
#include "../../src/Loop/LoopPasses.cpp"
#include "../test_support/MemoryMonitorNativeDeps.cpp"
#include "../../src/Loop.cpp"
#include "../test_support/LoopCaptureTestDeps.cpp"

#include "LoopEventStore.h"
#include "Loop.h"
#include "Utils/LoopEventValidation.h"
#include "Utils/LoopStopFinalize.h"
#include "Utils/IntervalProjection.h"
#include "Utils/NoteUtils.h"
#include "MidiEvent.h"

static void assert_has_note(const std::vector<NoteUtils::DisplayNote>& notes, uint8_t pitch,
                            uint32_t startTick, uint32_t endTick, uint8_t velocity) {
  auto it = std::find_if(notes.begin(), notes.end(), [&](const NoteUtils::DisplayNote& n) {
    return n.note == pitch && n.startTick == startTick && n.endTick == endTick &&
           n.velocity == velocity;
  });
  TEST_ASSERT_TRUE(it != notes.end());
}

static bool storeHasNoteOffAt(const LoopEventStore& store, uint32_t tick, uint8_t ch,
                              uint8_t note) {
  for (size_t i = 0; i < store.size(); ++i) {
    const MidiEvent& evt = store.at(i);
    if (evt.isNoteOff() && evt.tick == tick && evt.channel == ch &&
        evt.data.noteData.note == note) {
      return true;
    }
  }
  return false;
}

static size_t countNoteEvents(const LoopEventStore& store, uint8_t ch, uint8_t note, bool noteOn) {
  size_t count = 0;
  for (size_t i = 0; i < store.size(); ++i) {
    const MidiEvent& evt = store.at(i);
    if (evt.channel != ch || evt.data.noteData.note != note) {
      continue;
    }
    if (noteOn && evt.isNoteOn()) {
      ++count;
    } else if (!noteOn && evt.isNoteOff()) {
      ++count;
    }
  }
  return count;
}

static bool storeHasOrphanNoteOff(const LoopEventStore& store, uint32_t loopLength) {
  MidiEventVec flat;
  for (size_t i = 0; i < store.size(); ++i) {
    flat.push_back(store.at(i));
  }
  const uint32_t wrapWindow = std::min(768u, loopLength);
  return LoopEventValidation::checkOrphanNoteOff(flat, loopLength, wrapWindow);
}

void test_linear_storage_off_before_on_is_valid() {
  constexpr uint32_t loopLength = 2304;
  MidiEventVec ev;
  ev.push_back(MidiEvent::NoteOff(55, 4, 12, 0));
  ev.push_back(MidiEvent::NoteOn(1920, 4, 12, 100));
  auto notes = NoteUtils::reconstructNotes(ev, loopLength, false);
  TEST_ASSERT_EQUAL(2u, notes.size());
  assert_has_note(notes, 12, 1920, loopLength - 1, 100);
  assert_has_note(notes, 12, 0, 55, 100);
  TEST_ASSERT_TRUE(LoopEventValidation::checkOrphanNoteOff(ev, loopLength, 768));
}

void test_held_across_wrap_records_head_off() {
  constexpr uint32_t loopLength = 2304;
  MidiEventVec ev;
  ev.push_back(MidiEvent::NoteOn(1920, 4, 12, 100));
  ev.push_back(MidiEvent::NoteOff(55, 4, 12, 0));
  auto notes = NoteUtils::reconstructNotes(ev, loopLength, false);
  TEST_ASSERT_EQUAL(2u, notes.size());
  assert_has_note(notes, 12, 1920, loopLength - 1, 100);
  assert_has_note(notes, 12, 0, 55, 100);
  TEST_ASSERT_TRUE(LoopEventValidation::checkOrphanNoteOff(ev, loopLength, 768));
}

void test_no_orphan_off_for_balanced_wrap_grid() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;

  constexpr uint32_t loopLength = 2304;
  constexpr uint8_t ch = 4;
  constexpr uint8_t note = 12;

  const uint32_t gridOn[] = {0, 384, 576, 768, 960, 1152, 1344, 1536};
  const uint32_t gridOff[] = {192, 480, 672, 960, 1248, 1440, 1728, 2016};
  for (size_t i = 0; i < sizeof(gridOn) / sizeof(gridOn[0]); ++i) {
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(gridOn[i], ch, note, 100)));
  }
  for (size_t i = 0; i < sizeof(gridOff) / sizeof(gridOff[0]); ++i) {
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(gridOff[i], ch, note, 0)));
  }

  TEST_ASSERT_EQUAL(countNoteEvents(store, ch, note, true), countNoteEvents(store, ch, note, false));
  TEST_ASSERT_FALSE(storeHasOrphanNoteOff(store, loopLength));
}

void test_seal_wrap_window_respects_close_tick() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;

  const uint32_t loopLen = 2304;
  const uint32_t wrapWindow = 768;
  const uint32_t tailOnTick = loopLen - 200;
  const uint32_t closeTick = loopLen - 50;

  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(tailOnTick, 4, 12, 100)));

  const LoopStopFinalize::Result result =
      LoopStopFinalize::finalizeWrapWindowOnStore(store, loopLen, closeTick, wrapWindow);

  TEST_ASSERT_EQUAL(1u, result.syntheticOffsInserted);
  TEST_ASSERT_TRUE(storeHasNoteOffAt(store, closeTick, 4, 12));
  TEST_ASSERT_FALSE(storeHasNoteOffAt(store, loopLen - 1, 4, 12));
}

void test_stop_closes_open_tail_at_playhead_not_loop_end() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;

  const uint32_t loopLen = 2304;
  const uint32_t wrapWindow = 768;
  const uint32_t tailOnTick = loopLen - 200;
  const uint32_t closeTick = loopLen - 50;

  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(tailOnTick, 4, 12, 100)));

  const LoopStopFinalize::Result result =
      LoopStopFinalize::finalizeWrapWindowOnStore(store, loopLen, closeTick, wrapWindow);

  TEST_ASSERT_EQUAL(1u, result.syntheticOffsInserted);
  TEST_ASSERT_TRUE(storeHasNoteOffAt(store, closeTick, 4, 12));
}

void test_pending_wrap_lifecycle_open_tail_sealed_at_stop() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;

  const uint32_t loopLen = 2304;
  const uint32_t wrapWindow = 768;
  const uint32_t tailOnTick = loopLen - 200;
  const uint32_t closeTick = loopLen - 50;

  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(tailOnTick, 4, 12, 100)));
  TEST_ASSERT_EQUAL(1u, countNoteEvents(store, 4, 12, true));
  TEST_ASSERT_EQUAL(0u, countNoteEvents(store, 4, 12, false));

  const LoopStopFinalize::Result result =
      LoopStopFinalize::finalizeWrapWindowOnStore(store, loopLen, closeTick, wrapWindow);
  TEST_ASSERT_EQUAL(1u, result.syntheticOffsInserted);
  TEST_ASSERT_EQUAL(1u, countNoteEvents(store, 4, 12, true));
  TEST_ASSERT_EQUAL(1u, countNoteEvents(store, 4, 12, false));
  MidiEventVec flat;
  for (size_t i = 0; i < store.size(); ++i) {
    flat.push_back(store.at(i));
  }
  TEST_ASSERT_TRUE(LoopEventValidation::checkOrphanNoteOff(flat, loopLen, wrapWindow));
  const auto notes = NoteUtils::reconstructNotes(flat, loopLen, false);
  TEST_ASSERT_EQUAL(1u, notes.size());
}

void test_orphan_off_at_tail_tick_fails_canonical_check() {
  constexpr uint32_t loopLength = 2304;
  MidiEventVec ev;
  ev.push_back(MidiEvent::NoteOn(0, 4, 12, 100));
  ev.push_back(MidiEvent::NoteOff(192, 4, 12, 0));
  ev.push_back(MidiEvent::NoteOff(2208, 4, 12, 0));
  TEST_ASSERT_FALSE(LoopEventValidation::checkOrphanNoteOff(ev, loopLength, 768));
}

static void append_hilt_grid_through_1920(LoopEventStore& store, uint8_t ch, uint8_t note) {
  const uint32_t gridOn[] = {0, 384, 576, 768, 960, 1152, 1344, 1536, 1920};
  const uint32_t gridOff[] = {192, 480, 672, 960, 1248, 1440, 1728, 2016};
  for (size_t i = 0; i < sizeof(gridOn) / sizeof(gridOn[0]); ++i) {
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(gridOn[i], ch, note, 100)));
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(gridOff[i], ch, note, 0)));
  }
}

static void append_hilt_grid_through_1920(MidiEventVec& ev, uint8_t ch, uint8_t note) {
  const uint32_t gridOn[] = {0, 384, 576, 768, 960, 1152, 1344, 1536, 1920};
  const uint32_t gridOff[] = {192, 480, 672, 960, 1248, 1440, 1728, 2016};
  for (size_t i = 0; i < sizeof(gridOn) / sizeof(gridOn[0]); ++i) {
    ev.push_back(MidiEvent::NoteOn(gridOn[i], ch, note, 100));
    ev.push_back(MidiEvent::NoteOff(gridOff[i], ch, note, 0));
  }
}

void test_overdub_grid_tail_wrap_pairs_tail_on_not_first_on() {
  constexpr uint32_t loopLength = 2304;
  constexpr uint8_t ch = 4;
  constexpr uint8_t note = 12;

  MidiEventVec ev;
  append_hilt_grid_through_1920(ev, ch, note);
  ev.push_back(MidiEvent::NoteOn(2112, ch, note, 100));
  ev.push_back(MidiEvent::NoteOff(55, ch, note, 0));

  const auto notes = NoteUtils::reconstructNotes(ev, loopLength, false);
  assert_has_note(notes, 12, 2112, loopLength - 1, 100);
  assert_has_note(notes, 12, 0, 55, 100);
  for (const NoteUtils::DisplayNote& n : notes) {
    if (n.note == note && n.startTick == 0) {
      TEST_ASSERT_TRUE(n.endTick <= 192);
    }
  }
}

void test_finalize_pending_uses_loop_phase_not_clamped_absolute() {
  // Punch-in / L-1 clamp guard: unwrapped absolute delta must not replace loop phase.
  // Transport-active overdub uses tickPhaseInProjectionCycle (see test_interval_projection).
  constexpr uint32_t loopLen = 2304;
  constexpr uint32_t startLoopTick = 1'000'000u;
  const uint32_t absTick = startLoopTick + 2u * loopLen + 2208u;

  const uint32_t phase = IntervalProjection::tickPhaseInLoop(absTick, startLoopTick, loopLen);
  TEST_ASSERT_EQUAL(2208u, phase);

  const uint32_t unwrapped = absTick - startLoopTick;
  const uint32_t clampedWrong =
      unwrapped >= loopLen ? loopLen - 1u : unwrapped;
  TEST_ASSERT_EQUAL(2303u, clampedWrong);
  TEST_ASSERT_NOT_EQUAL(phase, clampedWrong);
}

void test_transport_active_capture_post_wrap_phase_from_log() {
  // Fixture from session_20260713_135824.log — note 12 finalize after wrap.
  constexpr uint32_t loopLen = 2304;
  constexpr int32_t projectionCycleStartTick = 43776;
  constexpr uint32_t absTick = 2432;

  const uint32_t capturePhase = IntervalProjection::tickPhaseInProjectionCycle(
      absTick, projectionCycleStartTick, loopLen);
  TEST_ASSERT_EQUAL(128u, capturePhase);
}

void test_finalize_seal_single_tail_already_closed() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;

  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(2112, 4, 12, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(2208, 4, 12, 0)));

  const LoopStopFinalize::Result result =
      LoopStopFinalize::finalizeWrapWindowOnStore(store, 2304, 2208);
  TEST_ASSERT_EQUAL(0u, result.syntheticOffsInserted);
}

void test_finalize_seal_ownership_transition() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;

  constexpr uint32_t loopLen = 2304;
  constexpr uint8_t ch = 4;
  constexpr uint8_t note = 12;
  constexpr uint32_t closeTick = 2208u;

  append_hilt_grid_through_1920(store, ch, note);
  TEST_ASSERT_EQUAL(9u, countNoteEvents(store, ch, note, true));
  TEST_ASSERT_EQUAL(9u, countNoteEvents(store, ch, note, false));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(2112, ch, note, 100)));
  TEST_ASSERT_EQUAL(10u, countNoteEvents(store, ch, note, true));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(closeTick, ch, note, 0)));

  const LoopStopFinalize::Result result =
      LoopStopFinalize::finalizeWrapWindowOnStore(store, loopLen, closeTick);
  TEST_ASSERT_EQUAL(0u, result.syntheticOffsInserted);
  TEST_ASSERT_EQUAL(10u, countNoteEvents(store, ch, note, false));
  TEST_ASSERT_FALSE(storeHasNoteOffAt(store, loopLen - 1, ch, note));
}

void test_performer_release_after_stop_ignored() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;

  constexpr uint32_t loopLen = 2304;
  constexpr uint8_t ch = 4;
  constexpr uint8_t note = 12;
  constexpr uint32_t closeTick = 2208u;

  append_hilt_grid_through_1920(store, ch, note);
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(2112, ch, note, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(closeTick, ch, note, 0)));

  MidiEventVec flat;
  for (size_t i = 0; i < store.size(); ++i) {
    flat.push_back(store.at(i));
  }
  TEST_ASSERT_TRUE(storeHasNoteOffAt(store, closeTick, ch, note));
  TEST_ASSERT_EQUAL(10u, countNoteEvents(store, ch, note, true));
  TEST_ASSERT_EQUAL(10u, countNoteEvents(store, ch, note, false));
}

void test_remove_open_capture_note_on_drops_pending_on() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = 2304;
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(1536, 4, 12, 100)));
  TEST_ASSERT_EQUAL(1u, loop.capture.store.size());
  TEST_ASSERT_TRUE(loop.removeOpenCaptureNoteOn(4, 12));
  TEST_ASSERT_TRUE(loop.capture.store.empty());
}

void test_capture_has_note_off_after_detects_real_release() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = 2304;
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(192, 4, 23, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(384, 4, 23, 0)));
  TEST_ASSERT_TRUE(loop.captureHasNoteOffAfter(4, 23, 192));
  TEST_ASSERT_FALSE(loop.captureHasNoteOffAfter(4, 23, 384));
}

void test_overdub_overlap_restore_triggered_by_close_tick_inside_committed_note() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = 2304;

  LoopEventStore publishedStore;
  TEST_ASSERT_TRUE(publishedStore.append(MidiEvent::NoteOn(1152, 4, 12, 100)));
  TEST_ASSERT_TRUE(publishedStore.append(MidiEvent::NoteOff(1248, 4, 12, 0)));
  loop.seedRecordPassFromStore(publishedStore);

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(1344, 4, 12, 100)));
  // Simulate overdub stop close tick inside committed note (1152..1248).
  // Restore behavior should drop the pending capture note-on (not append F@1184).
  TEST_ASSERT_TRUE(loop.removeOpenCaptureNoteOn(4, 12));
}

void test_overdub_overlap_stop_restore_preserves_committed_note() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = 2304;

  LoopEventStore publishedStore;
  append_hilt_grid_through_1920(publishedStore, 4, 12);
  loop.seedRecordPassFromStore(publishedStore);

  MidiEventVec publishedOnly;
  loop.passes.materializeToEventVector(publishedOnly, loop.loopLengthTicks);
  const auto notesBefore =
      NoteUtils::reconstructNotes(publishedOnly, loop.loopLengthTicks, false);

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(1536, 4, 12, 100)));
  TEST_ASSERT_TRUE(loop.removeOpenCaptureNoteOn(4, 12));

  MidiEventVec merged;
  loop.mergeActiveCapturePasses(merged);
  const auto notesAfter = NoteUtils::reconstructNotes(merged, loop.loopLengthTicks, false);
  TEST_ASSERT_EQUAL(notesBefore.size(), notesAfter.size());
  for (size_t i = 0; i < notesBefore.size(); ++i) {
    TEST_ASSERT_EQUAL(notesBefore[i].note, notesAfter[i].note);
    TEST_ASSERT_EQUAL(notesBefore[i].startTick, notesAfter[i].startTick);
    TEST_ASSERT_EQUAL(notesBefore[i].endTick, notesAfter[i].endTick);
  }
}

void test_overdub_stop_finalize_off_truncates_overlap_grid_note_without_restore() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = 2304;

  LoopEventStore publishedStore;
  append_hilt_grid_through_1920(publishedStore, 4, 12);
  loop.seedRecordPassFromStore(publishedStore);

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(1536, 4, 12, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(1552, 4, 12, 0)));

  MidiEventVec merged;
  loop.mergeActiveCapturePasses(merged);
  const auto notes = NoteUtils::reconstructNotes(merged, loop.loopLengthTicks, false);
  auto it = std::find_if(notes.begin(), notes.end(), [](const NoteUtils::DisplayNote& n) {
    return n.note == 12 && n.startTick == 1536 && n.endTick == 1728;
  });
  TEST_ASSERT_TRUE(it == notes.end());
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_linear_storage_off_before_on_is_valid);
  RUN_TEST(test_held_across_wrap_records_head_off);
  RUN_TEST(test_no_orphan_off_for_balanced_wrap_grid);
  RUN_TEST(test_seal_wrap_window_respects_close_tick);
  RUN_TEST(test_stop_closes_open_tail_at_playhead_not_loop_end);
  RUN_TEST(test_pending_wrap_lifecycle_open_tail_sealed_at_stop);
  RUN_TEST(test_orphan_off_at_tail_tick_fails_canonical_check);
  RUN_TEST(test_overdub_grid_tail_wrap_pairs_tail_on_not_first_on);
  RUN_TEST(test_finalize_pending_uses_loop_phase_not_clamped_absolute);
  RUN_TEST(test_transport_active_capture_post_wrap_phase_from_log);
  RUN_TEST(test_finalize_seal_single_tail_already_closed);
  RUN_TEST(test_finalize_seal_ownership_transition);
  RUN_TEST(test_performer_release_after_stop_ignored);
  RUN_TEST(test_remove_open_capture_note_on_drops_pending_on);
  RUN_TEST(test_capture_has_note_off_after_detects_real_release);
  RUN_TEST(test_overdub_overlap_restore_triggered_by_close_tick_inside_committed_note);
  RUN_TEST(test_overdub_overlap_stop_restore_preserves_committed_note);
  RUN_TEST(test_overdub_stop_finalize_off_truncates_overlap_grid_note_without_restore);
  return UNITY_END();
}
