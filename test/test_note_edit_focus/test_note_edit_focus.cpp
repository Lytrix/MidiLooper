//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/NoteEditFocus.cpp"
#include "../../src/EditApply.cpp"
#include "../../src/LoopPasses.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/Utils/MemoryMonitor.cpp"
#include "../../src/Loop.cpp"

#include "NoteEditFocus.h"
#include "EditApply.h"
#include "EditPass.h"
#include "LoopPasses.h"
#include "Loop.h"
#include "LoopEventBuffer.h"
#include "MidiEvent.h"
#include "Utils/NoteMovementWrap.h"

namespace {

MidiEventVec makeTwoNoteFlat(uint32_t startA, uint32_t endA, uint32_t startB, uint32_t endB,
                           uint8_t pitch) {
  MidiEventVec flat;
  flat.push_back(MidiEvent::NoteOn(startA, 1, pitch, 100));
  flat.push_back(MidiEvent::NoteOff(endA, 1, pitch, 0));
  flat.push_back(MidiEvent::NoteOn(startB, 1, pitch, 100));
  flat.push_back(MidiEvent::NoteOff(endB, 1, pitch, 0));
  return flat;
}

}  // namespace

void test_baseline_map_includes_all_store_notes_at_select() {
  NoteEditFocus focus;
  const MidiEventVec flat = makeTwoNoteFlat(8, 104, 584, 680, 60);
  rebuildNoteEditFocusFromStore(focus, flat, 1, 768, 0);

  TEST_ASSERT_TRUE(focus.active);
  TEST_ASSERT_EQUAL(2, static_cast<int>(focus.baselineMap.size()));
  TEST_ASSERT_EQUAL_UINT32(8, focus.commitBaseline.startTick);
  TEST_ASSERT_EQUAL_UINT32(104, focus.commitBaseline.endTick);
  TEST_ASSERT_EQUAL_UINT8(60, focus.commitBaseline.pitch);
  TEST_ASSERT_EQUAL_UINT32(8, focus.movingNoteRange.start);
  TEST_ASSERT_EQUAL_UINT32(104, focus.movingNoteRange.end);
  TEST_ASSERT_EQUAL(0, static_cast<int>(focus.overlapNotes.size()));
}

void test_a1_length_updates_moving_note_range_not_commit_baseline() {
  NoteEditFocus focus;
  focus.active = true;
  focus.commitBaseline = {60, 64, 100, 200};
  focus.movingNoteRange = {100, 200};
  focus.last = focus.commitBaseline;

  noteEditFocusApplyLengthEnd(focus, 300);

  TEST_ASSERT_EQUAL_UINT32(200, focus.commitBaseline.endTick);
  TEST_ASSERT_EQUAL_UINT32(300, focus.movingNoteRange.end);
  TEST_ASSERT_EQUAL_UINT32(300, focus.last.endTick);
  TEST_ASSERT_TRUE(noteEditFocusHasPendingLengthChange(focus));
}

void test_a1_no_pending_length_when_moving_note_range_matches_baseline() {
  NoteEditFocus focus;
  focus.active = true;
  focus.commitBaseline = {60, 64, 100, 200};
  focus.movingNoteRange = {100, 200};
  focus.last = focus.commitBaseline;

  TEST_ASSERT_FALSE(noteEditFocusHasPendingLengthChange(focus));
}

void test_inner_overlap_note_in_moving_note_range() {
  NoteEditFocus focus;
  focus.active = true;
  focus.commitBaseline = {60, 64, 496, 1168};
  focus.movingNoteRange = {496, 1168};
  focus.last = focus.commitBaseline;

  TEST_ASSERT_TRUE(isInnerOverlapNoteInMovingNoteRange(focus, 67, 520, 600, 1536));
  TEST_ASSERT_FALSE(isInnerOverlapNoteInMovingNoteRange(focus, 67, 403, 496, 1536));
  TEST_ASSERT_FALSE(isInnerOverlapNoteInMovingNoteRange(focus, 60, 592, 688, 1536));
}

void test_overlap_note_effective_end_shortened_vs_hidden() {
  OverlapNote shortened{};
  shortened.state = OverlapNoteStoreState::Shortened;
  shortened.baseline.endTick = 688;
  shortened.shortenedEndTick = 495;
  TEST_ASSERT_EQUAL_UINT32(495, overlapNoteEffectiveEnd(shortened));

  OverlapNote hidden{};
  hidden.state = OverlapNoteStoreState::Hidden;
  hidden.baseline.endTick = 688;
  TEST_ASSERT_EQUAL_UINT32(688, overlapNoteEffectiveEnd(hidden));
}

void test_shorten_under_49_ticks_classifies_as_hidden_candidate() {
  const uint32_t loopLength = 1536;
  const uint32_t moverStart = 410;
  const uint32_t neighborStart = 400;
  const uint32_t shortenedEnd = moverStart - 1;
  const uint32_t shortenedLength =
      NoteMovementUtils::calculateNoteLength(neighborStart, shortenedEnd, loopLength);
  TEST_ASSERT_TRUE(shortenedLength < 49);
}

void test_pre_commit_edit_change_order_delete_shorten_move_length_pitch() {
  NoteEditFocus focus;
  focus.active = true;
  focus.commitBaseline = {60, 64, 496, 1168};
  focus.last = {60, 64, 520, 1200};
  focus.moving = {1, 60, 496, 1168};

  const NoteRef overlapHidden{1, 60, 400, 688};
  const NoteRef overlapShort{1, 60, 592, 688};
  OverlapNote hiddenEntry{};
  hiddenEntry.ref = overlapHidden;
  hiddenEntry.baseline = {60, 64, 400, 688};
  hiddenEntry.state = OverlapNoteStoreState::Hidden;
  focus.overlapNotes[overlapHidden] = hiddenEntry;

  OverlapNote shortenedEntry{};
  shortenedEntry.ref = overlapShort;
  shortenedEntry.baseline = {60, 64, 592, 688};
  shortenedEntry.state = OverlapNoteStoreState::Shortened;
  shortenedEntry.shortenedEndTick = 495;
  focus.overlapNotes[overlapShort] = shortenedEntry;

  const EditPassVec rows = buildPreCommitEditPasses(focus, 1);
  TEST_ASSERT_EQUAL(3, static_cast<int>(rows.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(EditActionType::Delete),
                    static_cast<int>(rows[0].actionType));
  TEST_ASSERT_TRUE(noteRefEquals(rows[0].target, overlapHidden));
  TEST_ASSERT_EQUAL(static_cast<int>(EditActionType::Update),
                    static_cast<int>(rows[1].actionType));
  TEST_ASSERT_EQUAL(static_cast<int>(EditPropertyType::Length),
                    static_cast<int>(rows[1].propertyType));
  TEST_ASSERT_TRUE(noteRefEquals(rows[1].target, overlapShort));
  TEST_ASSERT_EQUAL_UINT32(495, rows[1].endTick);
  TEST_ASSERT_EQUAL(static_cast<int>(EditPropertyType::NoteRange),
                    static_cast<int>(rows[2].propertyType));
  TEST_ASSERT_EQUAL_UINT32(520, rows[2].startTick);
  TEST_ASSERT_EQUAL_UINT32(1200, rows[2].endTick);
}

void test_pre_commit_store_diff_subset_mover_and_overlap_notes() {
  const uint32_t loopLength = 1536;
  NoteEditFocus focus;
  MidiEventVec flat;
  flat.push_back(MidiEvent::NoteOn(100, 1, 67, 100));
  flat.push_back(MidiEvent::NoteOff(200, 1, 67, 0));
  flat.push_back(MidiEvent::NoteOn(400, 1, 60, 100));
  flat.push_back(MidiEvent::NoteOff(688, 1, 60, 0));
  flat.push_back(MidiEvent::NoteOn(496, 1, 60, 100));
  flat.push_back(MidiEvent::NoteOff(1200, 1, 60, 0));

  rebuildNoteEditFocusFromStore(focus, flat, 1, loopLength, 2);
  focus.last.endTick = 1200;
  focus.movingNoteRange.end = 1200;

  const NoteRef overlapShort{1, 60, 400, 688};
  OverlapNote shortenedEntry{};
  shortenedEntry.ref = overlapShort;
  shortenedEntry.baseline = {60, 64, 400, 688};
  shortenedEntry.state = OverlapNoteStoreState::Shortened;
  shortenedEntry.shortenedEndTick = 495;
  focus.overlapNotes[overlapShort] = shortenedEntry;

  const NoteRef overlapHidden{1, 60, 592, 688};
  OverlapNote hiddenEntry{};
  hiddenEntry.ref = overlapHidden;
  hiddenEntry.baseline = {60, 64, 592, 688};
  hiddenEntry.state = OverlapNoteStoreState::Hidden;
  focus.overlapNotes[overlapHidden] = hiddenEntry;

  flat.push_back(MidiEvent::NoteOn(592, 1, 60, 100));
  flat.push_back(MidiEvent::NoteOff(688, 1, 60, 0));

  resolveOverlapNotesForPreCommit(flat, focus, 1, loopLength);

  const std::vector<NoteUtils::DisplayNote> resolved =
      NoteUtils::reconstructNotes(flat, loopLength, false);

  for (const NoteUtils::DisplayNote& dn : resolved) {
    NoteRef matchedBaselineRef{};
    const NoteBaseline* baseline = nullptr;
    for (const auto& [ref, bl] : focus.baselineMap) {
      if (dn.note == bl.pitch && dn.startTick == bl.startTick) {
        matchedBaselineRef = ref;
        baseline = &bl;
        break;
      }
    }
    if (baseline == nullptr) {
      continue;
    }
    if (dn.endTick == baseline->endTick) {
      continue;
    }
    bool allowed = noteRefEquals(matchedBaselineRef, focus.moving);
    if (!allowed) {
      for (const auto& [ref, entry] : focus.overlapNotes) {
        (void)entry;
        if (noteRefEquals(ref, matchedBaselineRef)) {
          allowed = true;
          break;
        }
      }
    }
    TEST_ASSERT_TRUE_MESSAGE(allowed, "store diff outside mover + overlapNotes");
  }

  bool foundShortened = false;
  for (const NoteUtils::DisplayNote& dn : resolved) {
    if (dn.note == 60 && dn.startTick == 400 && dn.endTick == 495) {
      foundShortened = true;
    }
    if (dn.note == 60 && dn.startTick == 592) {
      TEST_FAIL_MESSAGE("hidden overlap note still in store after pre-commit resolve");
    }
  }
  TEST_ASSERT_TRUE(foundShortened);
}

void test_build_pre_commit_changes_replay_lengthen_delete_pitch() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  constexpr uint32_t kLoopLength = 1536;

  NoteEditFocus focus;
  focus.active = true;
  focus.commitBaseline = {60, 64, 8, 680};
  focus.last = {67, 64, 8, 680};
  focus.moving = {1, 60, 8, 680};
  focus.movingNoteRange = {8, 680};

  const NoteRef overlapHidden{1, 60, 584, 680};
  OverlapNote hidden{};
  hidden.ref = overlapHidden;
  hidden.baseline = {60, 64, 584, 680};
  hidden.state = OverlapNoteStoreState::Hidden;
  focus.overlapNotes[overlapHidden] = hidden;

  const EditPassVec rows = buildPreCommitEditPasses(focus, 1);
  TEST_ASSERT_EQUAL(2, static_cast<int>(rows.size()));

  LoopEventStore store;
  store.append(MidiEvent::NoteOn(8, 1, 60, 100));
  store.append(MidiEvent::NoteOff(104, 1, 60, 0));
  store.append(MidiEvent::NoteOn(392, 1, 67, 100));
  store.append(MidiEvent::NoteOff(488, 1, 67, 0));
  store.append(MidiEvent::NoteOn(584, 1, 60, 100));
  store.append(MidiEvent::NoteOff(680, 1, 60, 0));
  ChunkIdList refs;
  store.detachChunksTo(refs);
  RecordPass record{};
  record.id = 1;
  record.state = CapturePassState::Active;
  record.chunkRefs = std::move(refs);
  LoopPasses passes;
  passes.recordPass = std::move(record);

  EditPass pre{};
  pre.id = 1;
  pre.passType = EditPassType::Note;
  pre.actionType = EditActionType::Update;
  pre.propertyType = EditPropertyType::Length;
  pre.state = EditPassState::Active;
  pre.target = {1, 60, 8, 104};
  pre.startTick = 8;
  pre.endTick = 680;
  passes.editPasses.push_back(pre);
  EditPassId nextId = 2;
  for (const EditPass& row : rows) {
    EditPass post = row;
    post.id = nextId++;
    post.passType = EditPassType::Note;
    post.state = EditPassState::Active;
    passes.editPasses.push_back(post);
  }

  MidiEventVec flat;
  passes.materializeToEventVector(flat, kLoopLength);

  int m0HomeOn = 0;
  int m0HomeOff = 0;
  for (const MidiEvent& e : flat) {
    if (e.isNoteOn() && e.data.noteData.note == 67 && e.tick == 8) {
      m0HomeOn++;
    }
    if (e.isNoteOff() && e.data.noteData.note == 67 && e.tick == 680) {
      m0HomeOff++;
    }
  }
  TEST_ASSERT_EQUAL(1, m0HomeOn);
  TEST_ASSERT_EQUAL(1, m0HomeOff);
}

void test_reselect_keeps_commit_baseline_with_pending_length() {
  constexpr uint32_t kLoopLength = 1536;
  LoopPasses passes;
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(8, 5, 60, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(104, 5, 60, 0)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(585, 5, 60, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(680, 5, 60, 0)));
  ChunkIdList refs;
  store.detachChunksTo(refs);
  RecordPass record{};
  record.id = 1;
  record.state = CapturePassState::Active;
  record.chunkRefs = std::move(refs);
  passes.recordPass = std::move(record);

  MidiEventVec committed;
  passes.materializeToEventVector(committed, kLoopLength);

  NoteEditFocus focus;
  rebuildNoteEditFocusFromStore(focus, committed, 5, kLoopLength, 0);
  focus.last.endTick = 680;
  focus.movingNoteRange.end = 680;

  TEST_ASSERT_EQUAL(104u, focus.commitBaseline.endTick);
  TEST_ASSERT_EQUAL(680u, focus.last.endTick);

  EditPassVec rows = buildPreCommitEditPasses(focus, 5);
  TEST_ASSERT_EQUAL(1, static_cast<int>(rows.size()));
  TEST_ASSERT_EQUAL(EditPropertyType::Length, rows[0].propertyType);
  TEST_ASSERT_EQUAL(104u, rows[0].target.endTick);
  TEST_ASSERT_EQUAL(680u, rows[0].endTick);
}

void test_filter_excludes_hidden_overlap_note() {
  constexpr uint32_t kLoopLength = 1536;
  NoteEditFocus focus;
  focus.active = true;
  focus.commitBaseline = {67, 64, 8, 680};
  focus.movingNoteRange = {8, 680};

  MidiEventVec flat;
  flat.push_back(MidiEvent::NoteOn(8, 1, 67, 100));
  flat.push_back(MidiEvent::NoteOff(680, 1, 67, 0));
  flat.push_back(MidiEvent::NoteOn(200, 1, 64, 100));
  flat.push_back(MidiEvent::NoteOff(400, 1, 64, 0));
  flat.push_back(MidiEvent::NoteOn(584, 1, 60, 100));
  flat.push_back(MidiEvent::NoteOff(680, 1, 60, 0));

  rebuildNoteEditFocusFromStore(focus, flat, 1, kLoopLength, 0);

  const NoteRef hiddenRef{1, 60, 584, 680};
  OverlapNote hidden{};
  hidden.ref = hiddenRef;
  hidden.baseline = {60, 64, 584, 680};
  hidden.state = OverlapNoteStoreState::Hidden;
  focus.overlapNotes[hiddenRef] = hidden;

  const std::vector<NoteUtils::DisplayNote> filtered =
      filterSelectableDisplayNotes(flat, focus, 1, kLoopLength);

  TEST_ASSERT_EQUAL(2, static_cast<int>(filtered.size()));
  for (const NoteUtils::DisplayNote& dn : filtered) {
    TEST_ASSERT_FALSE(dn.note == 60 && dn.startTick == 584);
  }
}

void test_filter_includes_shortened_overlap_note() {
  constexpr uint32_t kLoopLength = 1536;
  NoteEditFocus focus;
  focus.active = true;

  MidiEventVec flat;
  flat.push_back(MidiEvent::NoteOn(496, 1, 60, 100));
  flat.push_back(MidiEvent::NoteOff(1200, 1, 60, 0));
  flat.push_back(MidiEvent::NoteOn(400, 1, 60, 100));
  flat.push_back(MidiEvent::NoteOff(495, 1, 60, 0));

  rebuildNoteEditFocusFromStore(focus, flat, 1, kLoopLength, 0);

  const NoteRef shortenedRef{1, 60, 400, 688};
  OverlapNote shortened{};
  shortened.ref = shortenedRef;
  shortened.baseline = {60, 64, 400, 688};
  shortened.state = OverlapNoteStoreState::Shortened;
  shortened.shortenedEndTick = 495;
  focus.overlapNotes[shortenedRef] = shortened;

  const std::vector<NoteUtils::DisplayNote> filtered =
      filterSelectableDisplayNotes(flat, focus, 1, kLoopLength);

  bool foundShortened = false;
  for (const NoteUtils::DisplayNote& dn : filtered) {
    if (dn.note == 60 && dn.startTick == 400 && dn.endTick == 495) {
      foundShortened = true;
    }
  }
  TEST_ASSERT_TRUE(foundShortened);
}

void test_filter_excludes_inner_under_moving_note() {
  constexpr uint32_t kLoopLength = 1536;
  NoteEditFocus focus;
  focus.active = true;
  focus.commitBaseline = {67, 64, 8, 680};
  focus.movingNoteRange = {8, 680};

  MidiEventVec flat;
  flat.push_back(MidiEvent::NoteOn(8, 1, 67, 100));
  flat.push_back(MidiEvent::NoteOff(680, 1, 67, 0));
  flat.push_back(MidiEvent::NoteOn(392, 1, 60, 100));
  flat.push_back(MidiEvent::NoteOff(488, 1, 60, 0));
  flat.push_back(MidiEvent::NoteOn(584, 1, 60, 100));
  flat.push_back(MidiEvent::NoteOff(680, 1, 60, 0));

  rebuildNoteEditFocusFromStore(focus, flat, 1, kLoopLength, 0);

  const NoteRef innerRef{1, 60, 584, 680};
  OverlapNote inner{};
  inner.ref = innerRef;
  inner.baseline = {60, 64, 584, 680};
  inner.state = OverlapNoteStoreState::Visible;
  inner.innerUnderMovingNote = true;
  focus.overlapNotes[innerRef] = inner;

  const std::vector<NoteUtils::DisplayNote> filtered =
      filterSelectableDisplayNotes(flat, focus, 1, kLoopLength);

  TEST_ASSERT_EQUAL(2, static_cast<int>(filtered.size()));
  for (const NoteUtils::DisplayNote& dn : filtered) {
    TEST_ASSERT_FALSE(dn.note == 60 && dn.startTick == 584);
  }
}

void test_filtered_display_note_index_for_note_ref() {
  constexpr uint32_t kLoopLength = 1536;
  NoteEditFocus focus;
  const MidiEventVec flat = makeTwoNoteFlat(8, 104, 584, 680, 60);
  rebuildNoteEditFocusFromStore(focus, flat, 1, kLoopLength, 0);

  const std::vector<NoteUtils::DisplayNote> filtered =
      filterSelectableDisplayNotes(flat, focus, 1, kLoopLength);
  TEST_ASSERT_EQUAL(2, static_cast<int>(filtered.size()));

  const NoteRef secondRef = noteRefFromFilteredDisplayNote(1, focus, filtered, 1);
  TEST_ASSERT_EQUAL(584u, secondRef.startTick);

  const int idx = filteredDisplayNoteIndexForNoteRef(1, focus, filtered, secondRef);
  TEST_ASSERT_EQUAL(1, idx);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_baseline_map_includes_all_store_notes_at_select);
  RUN_TEST(test_a1_length_updates_moving_note_range_not_commit_baseline);
  RUN_TEST(test_a1_no_pending_length_when_moving_note_range_matches_baseline);
  RUN_TEST(test_inner_overlap_note_in_moving_note_range);
  RUN_TEST(test_overlap_note_effective_end_shortened_vs_hidden);
  RUN_TEST(test_shorten_under_49_ticks_classifies_as_hidden_candidate);
  RUN_TEST(test_pre_commit_edit_change_order_delete_shorten_move_length_pitch);
  RUN_TEST(test_pre_commit_store_diff_subset_mover_and_overlap_notes);
  RUN_TEST(test_build_pre_commit_changes_replay_lengthen_delete_pitch);
  RUN_TEST(test_reselect_keeps_commit_baseline_with_pending_length);
  RUN_TEST(test_filter_excludes_hidden_overlap_note);
  RUN_TEST(test_filter_includes_shortened_overlap_note);
  RUN_TEST(test_filter_excludes_inner_under_moving_note);
  RUN_TEST(test_filtered_display_note_index_for_note_ref);
  return UNITY_END();
}
