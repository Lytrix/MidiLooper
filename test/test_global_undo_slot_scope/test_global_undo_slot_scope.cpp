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

size_t eraseUndoEntriesForSlot(GlobalUndoStack& stack, uint8_t slotIndex) {
  const size_t oldSize = stack.entries.size();
  if (oldSize == 0) {
    return 0;
  }

  UndoEntryVec kept;
  kept.reserve(oldSize);
  size_t removedBeforeCursor = 0;
  size_t removedTotal = 0;

  for (size_t i = 0; i < oldSize; ++i) {
    const bool remove = stack.entries[i].slotIndex == slotIndex;
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

  const size_t removed = eraseUndoEntriesForSlot(stack, 1);
  TEST_ASSERT_EQUAL(1u, removed);
  TEST_ASSERT_EQUAL(2u, stack.entries.size());
  TEST_ASSERT_EQUAL(2u, stack.cursor);
  TEST_ASSERT_EQUAL(2u, countAppliedUndoEntriesForSlot(stack, 0));
  TEST_ASSERT_EQUAL(0u, countAppliedUndoEntriesForSlot(stack, 1));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_count_applied_undo_entries_per_slot);
  RUN_TEST(test_count_updates_when_cursor_moves);
  RUN_TEST(test_count_redo_entries_per_slot);
  RUN_TEST(test_stack_tip_gate_blocks_other_loop);
  RUN_TEST(test_erase_undo_entries_for_slot_adjusts_depth);
  return UNITY_END();
}
