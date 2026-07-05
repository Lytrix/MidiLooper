//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/Utils/CaptureIncrementalSanity.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/Utils/IntervalProjection.cpp"
#include "../../src/Utils/NoteUtils.cpp"

#include "Globals.h"
#include "Utils/CaptureIncrementalSanity.h"
#include "Utils/LoopEventValidation.h"
#include "MidiEvent.h"

namespace {

constexpr uint32_t kLoopLen = Config::TICKS_PER_BAR * 2;

void setupStore() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
}

}  // namespace

void test_remove_pairs_shorter_than_min_length_removes_11t() {
  setupStore();
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(100, 1, 60, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(111, 1, 60, 0)));

  const size_t removed =
      CaptureIncrementalSanity::removePairsShorterThanNoteMinLength(store, kLoopLen, 12, true);
  TEST_ASSERT_EQUAL(1u, removed);
  TEST_ASSERT_TRUE(store.empty());
}

void test_remove_pairs_shorter_than_min_length_keeps_12t() {
  setupStore();
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(100, 1, 60, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(112, 1, 60, 0)));

  const size_t removed =
      CaptureIncrementalSanity::removePairsShorterThanNoteMinLength(store, kLoopLen, 12, true);
  TEST_ASSERT_EQUAL(0u, removed);
  TEST_ASSERT_EQUAL(2u, store.size());
}

void test_remove_pairs_shorter_than_min_length_skipped_when_disabled() {
  setupStore();
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(100, 1, 60, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(111, 1, 60, 0)));

  const size_t removed =
      CaptureIncrementalSanity::removePairsShorterThanNoteMinLength(store, kLoopLen, 12, false);
  TEST_ASSERT_EQUAL(0u, removed);
  TEST_ASSERT_EQUAL(2u, store.size());
}

void test_verify_capture_hot_stop_passes_canonical_pair() {
  setupStore();
  LoopEventStore store;
  MidiEvent on = MidiEvent::NoteOn(100, 1, 60, 100);
  on.noteId = 1;
  TEST_ASSERT_TRUE(store.append(on));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(200, 1, 60, 0)));

  TEST_ASSERT_TRUE(CaptureIncrementalSanity::verifyCaptureHotStop(store, kLoopLen));
}

void test_verify_capture_hot_stop_warns_on_note_on_at_loop_length() {
  setupStore();
  LoopEventStore store;
  MidiEvent on = MidiEvent::NoteOn(kLoopLen, 1, 60, 100);
  on.noteId = 1;
  TEST_ASSERT_TRUE(store.append(on));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(kLoopLen + 100, 1, 60, 0)));

  const bool passed = CaptureIncrementalSanity::verifyCaptureHotStop(store, kLoopLen);
  TEST_ASSERT_FALSE(passed);
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_remove_pairs_shorter_than_min_length_removes_11t);
  RUN_TEST(test_remove_pairs_shorter_than_min_length_keeps_12t);
  RUN_TEST(test_remove_pairs_shorter_than_min_length_skipped_when_disabled);
  RUN_TEST(test_verify_capture_hot_stop_passes_canonical_pair);
  RUN_TEST(test_verify_capture_hot_stop_warns_on_note_on_at_loop_length);
  return UNITY_END();
}
