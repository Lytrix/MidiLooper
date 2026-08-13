//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Compact DisplayNote inventory: notes, unique ids, max same-pitch.

#include <unity.h>

#include "DisplayNoteCount.h"
#include "Utils/NoteUtils.h"

namespace {

NoteUtils::DisplayNote makeNote(NoteId id, uint32_t startTick, uint32_t endTick,
                                uint8_t pitch) {
  NoteUtils::DisplayNote note{};
  note.noteId = id;
  note.note = pitch;
  note.velocity = 100;
  note.startTick = startTick;
  note.endTick = endTick;
  return note;
}

}  // namespace

void test_empty_list_counts_zero() {
  NoteUtils::DisplayNoteVec notes;
  const DisplayNoteCount::Result result = DisplayNoteCount::countDisplayNotes(notes);
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(result.notes));
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(result.uniqueNoteIds));
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(result.maxSamePitch));
}

void test_invalid_id_and_zero_length_are_skipped() {
  NoteUtils::DisplayNoteVec notes;
  notes.push_back(makeNote(kInvalidNoteId, 0, 100, 60));
  notes.push_back(makeNote(1, 50, 50, 60));
  notes.push_back(makeNote(2, 10, 20, 60));
  const DisplayNoteCount::Result result = DisplayNoteCount::countDisplayNotes(notes);
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(result.notes));
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(result.uniqueNoteIds));
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(result.maxSamePitch));
}

void test_wrap_note_is_counted() {
  NoteUtils::DisplayNoteVec notes;
  notes.push_back(makeNote(1, 500, 20, 64));
  const DisplayNoteCount::Result result = DisplayNoteCount::countDisplayNotes(notes);
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(result.notes));
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(result.maxSamePitch));
}

void test_max_same_pitch_is_busiest_pitch() {
  NoteUtils::DisplayNoteVec notes;
  notes.push_back(makeNote(1, 0, 10, 60));
  notes.push_back(makeNote(2, 20, 30, 60));
  notes.push_back(makeNote(3, 40, 50, 60));
  notes.push_back(makeNote(4, 0, 10, 72));
  notes.push_back(makeNote(5, 20, 30, 72));
  const DisplayNoteCount::Result result = DisplayNoteCount::countDisplayNotes(notes);
  TEST_ASSERT_EQUAL_UINT32(5, static_cast<uint32_t>(result.notes));
  TEST_ASSERT_EQUAL_UINT32(5, static_cast<uint32_t>(result.uniqueNoteIds));
  TEST_ASSERT_EQUAL_UINT32(3, static_cast<uint32_t>(result.maxSamePitch));
}

void test_same_pitch_loop_reports_all_notes_as_max() {
  constexpr size_t kNoteCount = 3714;
  NoteUtils::DisplayNoteVec notes;
  notes.reserve(kNoteCount);
  for (size_t i = 0; i < kNoteCount; ++i) {
    const uint32_t start = static_cast<uint32_t>(i) * 8u;
    notes.push_back(makeNote(static_cast<NoteId>(i + 1), start, start + 4u, 60));
  }
  const DisplayNoteCount::Result result = DisplayNoteCount::countDisplayNotes(notes);
  TEST_ASSERT_EQUAL_UINT32(3714, static_cast<uint32_t>(result.notes));
  TEST_ASSERT_EQUAL_UINT32(3714, static_cast<uint32_t>(result.uniqueNoteIds));
  TEST_ASSERT_EQUAL_UINT32(3714, static_cast<uint32_t>(result.maxSamePitch));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_list_counts_zero);
  RUN_TEST(test_invalid_id_and_zero_length_are_skipped);
  RUN_TEST(test_wrap_note_is_counted);
  RUN_TEST(test_max_same_pitch_is_busiest_pitch);
  RUN_TEST(test_same_pitch_loop_reports_all_notes_as_max);
  return UNITY_END();
}
