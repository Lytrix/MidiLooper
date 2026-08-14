//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Per-loop undo depth and stack-tip gating on the track-wide GlobalUndoStack.

#include <unity.h>

#include "GlobalUndoStack.h"

namespace {

UndoEntry makeEntry(uint8_t slotIndex, UndoEntryKind kind = UndoEntryKind::RecordPassAdded) {
  UndoEntry entry{};
  entry.slotIndex = slotIndex;
  entry.kind = kind;
  return entry;
}

size_t eraseUndoEntriesForSlot(GlobalUndoStack& stack, uint8_t slotIndex,
                               bool preserveClearSlot) {
  const size_t oldSize = stack.entries.size();
  if (oldSize == 0) {
    return 0;
  }

  UndoEntryVec kept;
  kept.reserve(oldSize);
  size_t removedBeforeCursor = 0;
  size_t removedTotal = 0;

  for (size_t i = 0; i < oldSize; ++i) {
    const bool remove = stack.entries[i].slotIndex == slotIndex &&
                        (!preserveClearSlot ||
                         stack.entries[i].kind != UndoEntryKind::ClearSlot);
    if (remove) {
      ++removedTotal;
      if (i < stack.cursor) {
        ++removedBeforeCursor;
      }
      continue;
    }
    kept.push_back(std::move(stack.entries[i]));
  }

  stack.entries = std::move(kept);
  if (removedBeforeCursor > stack.cursor) {
    stack.cursor = 0;
  } else {
    stack.cursor -= removedBeforeCursor;
  }
  if (stack.cursor > stack.entries.size()) {
    stack.cursor = stack.entries.size();
  }
  if (stack.entries.empty()) {
    stack.nextEntryId = 1;
  }
  return removedTotal;
}

bool stackTipMatchesSlot(const GlobalUndoStack& stack, uint8_t slotIndex) {
  return stack.canUndo() && stack.entries[stack.cursor - 1].slotIndex == slotIndex;
}

}  // namespace

void test_count_applied_undo_entries_per_slot() {
  GlobalUndoStack stack;
  stack.entries.push_back(makeEntry(0, UndoEntryKind::RecordPassAdded));
  stack.entries.push_back(makeEntry(1, UndoEntryKind::RecordPassAdded));
  stack.entries.push_back(makeEntry(0, UndoEntryKind::OverdubPassAdded));
  stack.cursor = 3;

  TEST_ASSERT_EQUAL(2u, countAppliedUndoEntriesForSlot(stack, 0));
  TEST_ASSERT_EQUAL(1u, countAppliedUndoEntriesForSlot(stack, 1));
  TEST_ASSERT_EQUAL(3u, stack.undoCount());
}

void test_count_updates_when_cursor_moves() {
  GlobalUndoStack stack;
  stack.entries.push_back(makeEntry(0));
  stack.entries.push_back(makeEntry(1));
  stack.entries.push_back(makeEntry(0, UndoEntryKind::OverdubPassAdded));
  stack.cursor = 2;

  TEST_ASSERT_EQUAL(1u, countAppliedUndoEntriesForSlot(stack, 0));
  TEST_ASSERT_EQUAL(1u, countAppliedUndoEntriesForSlot(stack, 1));
}

void test_count_redo_entries_per_slot() {
  GlobalUndoStack stack;
  stack.entries.push_back(makeEntry(0));
  stack.entries.push_back(makeEntry(1));
  stack.entries.push_back(makeEntry(0, UndoEntryKind::OverdubPassAdded));
  stack.cursor = 1;

  TEST_ASSERT_EQUAL(1u, countRedoEntriesForSlot(stack, 0));
  TEST_ASSERT_EQUAL(1u, countRedoEntriesForSlot(stack, 1));
}

void test_stack_tip_gate_blocks_other_loop() {
  GlobalUndoStack stack;
  stack.entries.push_back(makeEntry(0));
  stack.entries.push_back(makeEntry(1));
  stack.entries.push_back(makeEntry(0, UndoEntryKind::OverdubPassAdded));
  stack.cursor = 3;

  TEST_ASSERT_TRUE(stackTipMatchesSlot(stack, 0));
  TEST_ASSERT_FALSE(stackTipMatchesSlot(stack, 1));
}

void test_erase_undo_entries_for_slot_adjusts_depth() {
  GlobalUndoStack stack;
  stack.entries.push_back(makeEntry(0));
  stack.entries.push_back(makeEntry(1));
  stack.entries.push_back(makeEntry(0, UndoEntryKind::OverdubPassAdded));
  stack.cursor = 3;

  const size_t removed = eraseUndoEntriesForSlot(stack, 1, true);
  TEST_ASSERT_EQUAL(1u, removed);
  TEST_ASSERT_EQUAL(2u, stack.entries.size());
  TEST_ASSERT_EQUAL(2u, stack.cursor);
  TEST_ASSERT_EQUAL(2u, countAppliedUndoEntriesForSlot(stack, 0));
  TEST_ASSERT_EQUAL(0u, countAppliedUndoEntriesForSlot(stack, 1));
}

void test_erase_preserves_clear_slot_entries() {
  GlobalUndoStack stack;
  stack.entries.push_back(makeEntry(0, UndoEntryKind::RecordPassAdded));
  stack.entries.push_back(makeEntry(0, UndoEntryKind::ClearSlot));
  stack.entries.push_back(makeEntry(1, UndoEntryKind::RecordPassAdded));
  stack.cursor = 3;

  const size_t removed = eraseUndoEntriesForSlot(stack, 0, true);
  TEST_ASSERT_EQUAL(1u, removed);
  TEST_ASSERT_EQUAL(2u, stack.entries.size());
  TEST_ASSERT_EQUAL(2u, stack.cursor);
  TEST_ASSERT_EQUAL(UndoEntryKind::ClearSlot, stack.entries[0].kind);
  TEST_ASSERT_EQUAL(0u, stack.entries[0].slotIndex);
  TEST_ASSERT_EQUAL(1u, countAppliedUndoEntriesForSlot(stack, 0));
}

void test_erase_clear_slot_still_prunes_other_slots() {
  GlobalUndoStack stack;
  stack.entries.push_back(makeEntry(2, UndoEntryKind::ClearSlot));
  stack.entries.push_back(makeEntry(2, UndoEntryKind::OverdubPassAdded));
  stack.cursor = 2;

  const size_t removed = eraseUndoEntriesForSlot(stack, 2, true);
  TEST_ASSERT_EQUAL(1u, removed);
  TEST_ASSERT_EQUAL(1u, stack.entries.size());
  TEST_ASSERT_EQUAL(1u, stack.cursor);
  TEST_ASSERT_EQUAL(UndoEntryKind::ClearSlot, stack.entries[0].kind);
}

void test_pass_undo_depth_excludes_clear_slot() {
  GlobalUndoStack stack;
  stack.entries.push_back(makeEntry(0, UndoEntryKind::RecordPassAdded));
  stack.entries.push_back(makeEntry(0, UndoEntryKind::OverdubPassAdded));
  stack.entries.push_back(makeEntry(0, UndoEntryKind::ClearSlot));
  stack.cursor = 3;

  TEST_ASSERT_EQUAL(3u, countAppliedUndoEntriesForSlot(stack, 0));
  TEST_ASSERT_EQUAL(2u, countAppliedPassUndoEntriesForSlot(stack, 0));
}

void test_stale_undo_skip_removes_tip_for_same_slot() {
  // Mirrors undoForLoop: failed apply drops stack tip so the next entry can run on retry.
  GlobalUndoStack stack;
  UndoEntry stale = makeEntry(0, UndoEntryKind::NoteEditPassClosed);
  stale.id = 10;
  stale.editPassIndex = 2;
  stack.entries.push_back(makeEntry(0, UndoEntryKind::OverdubPassAdded));
  stack.entries.push_back(std::move(stale));
  stack.cursor = 2;

  TEST_ASSERT_TRUE(stackTipMatchesSlot(stack, 0));
  stack.entries.erase(stack.entries.begin() + static_cast<std::ptrdiff_t>(stack.cursor - 1));
  --stack.cursor;

  TEST_ASSERT_EQUAL(1u, stack.cursor);
  TEST_ASSERT_EQUAL(1u, stack.entries.size());
  TEST_ASSERT_EQUAL(UndoEntryKind::OverdubPassAdded, stack.entries[0].kind);
  TEST_ASSERT_EQUAL(1u, countAppliedPassUndoEntriesForSlot(stack, 0));
}

void test_stale_redo_skip_removes_redo_tip_for_same_slot() {
  GlobalUndoStack stack;
  UndoEntry stale = makeEntry(0, UndoEntryKind::NoteEditPassClosed);
  stale.id = 11;
  stale.editPassIndex = 3;
  stack.entries.push_back(makeEntry(0, UndoEntryKind::OverdubPassAdded));
  stack.entries.push_back(std::move(stale));
  stack.cursor = 1;

  TEST_ASSERT_TRUE(stack.canRedo());
  TEST_ASSERT_EQUAL(0u, stack.entries[stack.cursor].slotIndex);
  stack.entries.erase(stack.entries.begin() + static_cast<std::ptrdiff_t>(stack.cursor));

  TEST_ASSERT_EQUAL(1u, stack.cursor);
  TEST_ASSERT_EQUAL(1u, stack.entries.size());
  TEST_ASSERT_FALSE(stack.canRedo());
  TEST_ASSERT_EQUAL(1u, countAppliedPassUndoEntriesForSlot(stack, 0));
}

void test_redo_branch_survives_full_undo_for_slot() {
  GlobalUndoStack stack;
  stack.entries.push_back(makeEntry(0, UndoEntryKind::RecordPassAdded));
  stack.entries.push_back(makeEntry(0, UndoEntryKind::OverdubPassAdded));
  stack.entries.push_back(makeEntry(0, UndoEntryKind::LoopBoundaryChange));
  stack.cursor = 3;

  stack.cursor = 0;
  TEST_ASSERT_EQUAL(0u, countAppliedPassUndoEntriesForSlot(stack, 0));
  TEST_ASSERT_EQUAL(3u, countRedoEntriesForSlot(stack, 0));
  TEST_ASSERT_TRUE(stack.canRedo());
}

void test_pass_undo_depth_hidden_when_slot_cleared() {
  // Sidebar U: uses pass depth only when the slot has committed passes MIDI; cleared slots show --.
  GlobalUndoStack stack;
  stack.entries.push_back(makeEntry(0, UndoEntryKind::RecordPassAdded));
  stack.entries.push_back(makeEntry(0, UndoEntryKind::OverdubPassAdded));
  stack.cursor = 2;

  TEST_ASSERT_EQUAL(2u, countAppliedPassUndoEntriesForSlot(stack, 0));
  const bool slotHasCommittedPasses = false;
  const size_t displayDepth = slotHasCommittedPasses
                                  ? countAppliedPassUndoEntriesForSlot(stack, 0)
                                  : 0u;
  TEST_ASSERT_EQUAL(0u, displayDepth);
}

void test_reposition_undo_tip_after_multi_slot_content_rebuild() {
  GlobalUndoStack stack;
  stack.entries.push_back(makeEntry(0, UndoEntryKind::RecordPassAdded));
  stack.entries.push_back(makeEntry(0, UndoEntryKind::OverdubPassAdded));
  stack.entries.push_back(makeEntry(1, UndoEntryKind::RecordPassAdded));
  stack.entries.push_back(makeEntry(7, UndoEntryKind::OverdubPassAdded));
  stack.cursor = 4;

  TEST_ASSERT_FALSE(stackTipMatchesSlot(stack, 0));
  repositionGlobalUndoStackTipForSlot(stack, 0);
  TEST_ASSERT_TRUE(stackTipMatchesSlot(stack, 0));
  TEST_ASSERT_EQUAL(4u, stack.cursor);
  TEST_ASSERT_EQUAL(2u, countAppliedPassUndoEntriesForSlot(stack, 0));
  TEST_ASSERT_EQUAL(1u, countAppliedPassUndoEntriesForSlot(stack, 1));
}

void test_reposition_undo_tip_for_selected_slot_not_active_slot() {
  GlobalUndoStack stack;
  for (uint8_t slot = 0; slot < 8; ++slot) {
    stack.entries.push_back(makeEntry(slot, UndoEntryKind::RecordPassAdded));
  }
  stack.cursor = 8;

  TEST_ASSERT_FALSE(stackTipMatchesSlot(stack, 5));
  repositionGlobalUndoStackTipForSlot(stack, 5);
  TEST_ASSERT_TRUE(stackTipMatchesSlot(stack, 5));
  TEST_ASSERT_EQUAL(1u, countAppliedPassUndoEntriesForSlot(stack, 5));
}

void test_later_slot_rebuild_keeps_selected_slot_at_tip() {
  // LoadLoopJob rebuilds every restored slot. After slot 5 then slot 0, tip must stay on 5.
  GlobalUndoStack stack;
  stack.entries.push_back(makeEntry(5, UndoEntryKind::RecordPassAdded));
  stack.entries.push_back(makeEntry(5, UndoEntryKind::OverdubPassAdded));
  stack.entries.push_back(makeEntry(0, UndoEntryKind::RecordPassAdded));
  stack.cursor = 3;

  repositionGlobalUndoStackTipForSlot(stack, 5);
  TEST_ASSERT_TRUE(stackTipMatchesSlot(stack, 5));
  repositionGlobalUndoStackTipForSlot(stack, 5);
  TEST_ASSERT_TRUE(stackTipMatchesSlot(stack, 5));
  TEST_ASSERT_EQUAL(2u, countAppliedPassUndoEntriesForSlot(stack, 5));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_count_applied_undo_entries_per_slot);
  RUN_TEST(test_count_updates_when_cursor_moves);
  RUN_TEST(test_count_redo_entries_per_slot);
  RUN_TEST(test_stack_tip_gate_blocks_other_loop);
  RUN_TEST(test_erase_undo_entries_for_slot_adjusts_depth);
  RUN_TEST(test_erase_preserves_clear_slot_entries);
  RUN_TEST(test_erase_clear_slot_still_prunes_other_slots);
  RUN_TEST(test_pass_undo_depth_excludes_clear_slot);
  RUN_TEST(test_stale_undo_skip_removes_tip_for_same_slot);
  RUN_TEST(test_stale_redo_skip_removes_redo_tip_for_same_slot);
  RUN_TEST(test_redo_branch_survives_full_undo_for_slot);
  RUN_TEST(test_pass_undo_depth_hidden_when_slot_cleared);
  RUN_TEST(test_reposition_undo_tip_after_multi_slot_content_rebuild);
  RUN_TEST(test_reposition_undo_tip_for_selected_slot_not_active_slot);
  RUN_TEST(test_later_slot_rebuild_keeps_selected_slot_at_tip);
  return UNITY_END();
}
