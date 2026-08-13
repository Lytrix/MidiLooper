//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Empty overlap candidates skip span lookup. No gather or reconstruct.

#include <unity.h>

#include "OverlapCandidateLookup.h"
#include "OverlapNoteIdSet.h"
#include "Utils/NoteUtils.h"

namespace {

NoteUtils::DisplayNote makeNote(NoteId id, uint32_t startTick, uint32_t endTick) {
  NoteUtils::DisplayNote note{};
  note.noteId = id;
  note.note = 60;
  note.velocity = 100;
  note.startTick = startTick;
  note.endTick = endTick;
  return note;
}

}  // namespace

void test_empty_candidates_do_not_lookup_spans() {
  OverlapNoteIdSet ids;
  TEST_ASSERT_FALSE(OverlapCandidateLookup::shouldLookupSpans(ids));
}

void test_empty_candidates_append_no_notes() {
  NoteUtils::DisplayNoteVec source;
  source.push_back(makeNote(1, 50, 200));
  source.push_back(makeNote(2, 300, 400));
  OverlapNoteIdSet ids;
  NoteUtils::DisplayNoteVec out;
  out.push_back(makeNote(99, 0, 1));
  OverlapCandidateLookup::appendNotesForIds(source, ids, out);
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(out.size()));
}

void test_nonempty_candidates_copy_matching_notes_only() {
  NoteUtils::DisplayNoteVec source;
  source.push_back(makeNote(1, 50, 200));
  source.push_back(makeNote(2, 300, 400));
  source.push_back(makeNote(3, 500, 600));
  OverlapNoteIdSet ids;
  TEST_ASSERT_TRUE(ids.insert(2));
  TEST_ASSERT_TRUE(ids.insert(3));
  TEST_ASSERT_TRUE(OverlapCandidateLookup::shouldLookupSpans(ids));
  NoteUtils::DisplayNoteVec out;
  OverlapCandidateLookup::appendNotesForIds(source, ids, out);
  TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(out.size()));
  TEST_ASSERT_EQUAL_UINT32(2, out[0].noteId);
  TEST_ASSERT_EQUAL_UINT32(3, out[1].noteId);
}

void test_lookup_examines_through_last_match_not_candidates_times_loop() {
  constexpr size_t kNoteCount = 3714;
  NoteUtils::DisplayNoteVec source;
  source.reserve(kNoteCount);
  for (size_t i = 0; i < kNoteCount; ++i) {
    const NoteId id = static_cast<NoteId>(i + 1);
    const uint32_t start = static_cast<uint32_t>(i) * 8u;
    source.push_back(makeNote(id, start, start + 4u));
  }
  OverlapNoteIdSet ids;
  TEST_ASSERT_TRUE(ids.insert(10));
  TEST_ASSERT_TRUE(ids.insert(20));
  TEST_ASSERT_TRUE(ids.insert(30));
  NoteUtils::DisplayNoteVec out;
  size_t examined = 0;
  OverlapCandidateLookup::appendNotesForIds(source, ids, out, &examined);
  TEST_ASSERT_EQUAL_UINT32(3, static_cast<uint32_t>(out.size()));
  TEST_ASSERT_EQUAL_UINT32(30, static_cast<uint32_t>(examined));
  TEST_ASSERT_TRUE(examined < kNoteCount);
  TEST_ASSERT_TRUE(examined < kNoteCount * ids.size());
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_candidates_do_not_lookup_spans);
  RUN_TEST(test_empty_candidates_append_no_notes);
  RUN_TEST(test_nonempty_candidates_copy_matching_notes_only);
  RUN_TEST(test_lookup_examines_through_last_match_not_candidates_times_loop);
  return UNITY_END();
}
