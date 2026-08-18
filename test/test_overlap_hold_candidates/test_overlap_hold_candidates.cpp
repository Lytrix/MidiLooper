//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Hold-duration candidate collection. Offs do not erase. Selection is not this helper.

#include <unity.h>

#include "OverlapHoldCandidates.h"
#include "OverlapNoteIdSet.h"
#include "Utils/NoteUtils.h"

#include "../../src/Logger.cpp"
#include "../../src/Utils/IntervalProjection.cpp"
#include "../../src/Utils/NoteUtils.cpp"

namespace {

constexpr uint32_t kLoopLen = 768;
constexpr uint8_t kPitch = 60;

NoteUtils::DisplayNote makeNote(NoteId id, uint32_t startTick, uint32_t endTick,
                                uint8_t pitch = kPitch) {
  NoteUtils::DisplayNote note{};
  note.noteId = id;
  note.note = pitch;
  note.velocity = 100;
  note.startTick = startTick;
  note.endTick = endTick;
  return note;
}

}  // namespace

void test_snapshot_keeps_already_sounding_same_pitch() {
  NoteUtils::DisplayNoteVec notes;
  notes.push_back(makeNote(1, 10, 80));
  notes.push_back(makeNote(2, 50, 90));
  notes.push_back(makeNote(3, 10, 80, 72));
  OverlapNoteIdSet ids;
  OverlapHoldCandidates::snapshotSoundingAtHoldStart(notes, kPitch, 40, kLoopLen, ids);
  TEST_ASSERT_TRUE(ids.contains(1));
  TEST_ASSERT_FALSE(ids.contains(2));
  TEST_ASSERT_FALSE(ids.contains(3));
}

void test_snapshot_includes_note_starting_at_hold_start() {
  NoteUtils::DisplayNoteVec notes;
  notes.push_back(makeNote(1, 40, 80));
  notes.push_back(makeNote(2, 10, 40));
  notes.push_back(makeNote(3, 40, 80, 72));
  OverlapNoteIdSet ids;
  OverlapHoldCandidates::snapshotSoundingAtHoldStart(notes, kPitch, 40, kLoopLen, ids);
  TEST_ASSERT_TRUE(ids.contains(1));
  TEST_ASSERT_FALSE(ids.contains(2));
  TEST_ASSERT_FALSE(ids.contains(3));
}

void test_playback_on_inserts_same_pitch_and_off_does_not_erase() {
  OverlapNoteIdSet ids;
  TEST_ASSERT_TRUE(OverlapHoldCandidates::considerPlaybackNoteOn(ids, 10, kPitch, kPitch));
  TEST_ASSERT_TRUE(ids.contains(10));
  TEST_ASSERT_FALSE(OverlapHoldCandidates::considerPlaybackNoteOn(ids, 11, 72, kPitch));
  TEST_ASSERT_FALSE(ids.contains(11));
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(ids.size()));
}

void test_invalid_id_rejected_and_overflow_does_not_grow() {
  OverlapNoteIdSet ids;
  TEST_ASSERT_FALSE(OverlapHoldCandidates::considerPlaybackNoteOn(ids, kInvalidNoteId, kPitch, kPitch));
  for (size_t i = 0; i < kOverlapNoteIdSetCapacity; ++i) {
    TEST_ASSERT_TRUE(OverlapHoldCandidates::considerPlaybackNoteOn(
        ids, static_cast<NoteId>(i + 1), kPitch, kPitch));
  }
  TEST_ASSERT_FALSE(OverlapHoldCandidates::considerPlaybackNoteOn(
      ids, static_cast<NoteId>(kOverlapNoteIdSetCapacity + 1), kPitch, kPitch));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(kOverlapNoteIdSetCapacity),
                           static_cast<uint32_t>(ids.size()));
  TEST_ASSERT_TRUE(ids.overflowed());
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_snapshot_keeps_already_sounding_same_pitch);
  RUN_TEST(test_snapshot_includes_note_starting_at_hold_start);
  RUN_TEST(test_playback_on_inserts_same_pitch_and_off_does_not_erase);
  RUN_TEST(test_invalid_id_rejected_and_overflow_does_not_grow);
  return UNITY_END();
}
