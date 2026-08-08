//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Global undo stack scoped-edit serialization + durable checkpoint id batches.

#include <cstring>
#include <unity.h>
#include <vector>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/EditManager/EditApply.cpp"
#include "../../src/Loop/LoopPasses.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../test_support/MemoryMonitorNativeDeps.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/Loop.cpp"
#include "../test_support/LoopCaptureTestDeps.cpp"
#include "../test_support/CommittedChunkIdTestHelpers.h"

#include "GlobalUndoStack.h"
#include "EditPass.h"
#include "Loop.h"

namespace {

template <typename T>
void appendRaw(std::vector<uint8_t>& buffer, const T& value) {
  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&value);
  buffer.insert(buffer.end(), ptr, ptr + sizeof(T));
}

void appendNullSnapshotFlag(std::vector<uint8_t>& buffer) {
  const bool hasSnapshot = false;
  appendRaw(buffer, hasSnapshot);
}

void appendUndoEntryTail(std::vector<uint8_t>& buffer, const UndoEntry& entry) {
  appendRaw(buffer, entry.beforeGeometry);
  appendRaw(buffer, entry.afterGeometry);
  appendRaw(buffer, entry.beforeLoopStartTick);
  appendRaw(buffer, entry.beforeLoopLengthTicks);
  appendRaw(buffer, entry.afterLoopStartTick);
  appendRaw(buffer, entry.afterLoopLengthTicks);
  const uint32_t beforeTrackState = static_cast<uint32_t>(entry.beforeTrackState);
  const uint32_t afterTrackState = static_cast<uint32_t>(entry.afterTrackState);
  appendRaw(buffer, beforeTrackState);
  appendRaw(buffer, afterTrackState);
  appendRaw(buffer, entry.hasTrackState);
  appendRaw(buffer, entry.hasRedoPayload);
}

void appendScopedEditExtension(std::vector<uint8_t>& buffer, const UndoEntry& entry,
                               bool scopedEditExtension) {
  if (!scopedEditExtension) {
    return;
  }
  switch (entry.kind) {
    case UndoEntryKind::NoteEditPassClosed:
    case UndoEntryKind::ControlChangeEditPassClosed: {
      appendRaw(buffer, entry.editPassIndex);
      const uint8_t editPassTypeRaw = static_cast<uint8_t>(entry.editPassType);
      appendRaw(buffer, editPassTypeRaw);
      const uint16_t count = static_cast<uint16_t>(entry.editPassIds.size());
      appendRaw(buffer, count);
      for (const EditPassId id : entry.editPassIds) {
        appendRaw(buffer, id);
      }
      break;
    }
    default:
      break;
  }
}

void writeGlobalUndoStackToBuffer(std::vector<uint8_t>& buffer, const GlobalUndoStack& stack) {
  const uint32_t entryCount = static_cast<uint32_t>(stack.entries.size());
  const uint32_t cursor = static_cast<uint32_t>(stack.cursor);
  appendRaw(buffer, entryCount);
  appendRaw(buffer, cursor);
  appendRaw(buffer, stack.nextEntryId);
  appendRaw(buffer, kGlobalUndoStackScopedEditExtensionToken);

  for (const UndoEntry& entry : stack.entries) {
    appendRaw(buffer, entry.id);
    const uint8_t kind = static_cast<uint8_t>(entry.kind);
    appendRaw(buffer, kind);
    appendRaw(buffer, entry.slotIndex);
    appendRaw(buffer, entry.loopId);
    appendRaw(buffer, entry.passId);
    appendNullSnapshotFlag(buffer);
    appendNullSnapshotFlag(buffer);
    appendUndoEntryTail(buffer, entry);
    appendScopedEditExtension(buffer, entry, true);
  }
}

bool readScopedEditExtensionFromBuffer(const std::vector<uint8_t>& buffer, size_t& cursor,
                                       UndoEntry& entry, bool scopedEditExtension) {
  if (!scopedEditExtension) {
    return true;
  }
  switch (entry.kind) {
    case UndoEntryKind::NoteEditPassClosed:
    case UndoEntryKind::ControlChangeEditPassClosed: {
      if (cursor + sizeof(entry.editPassIndex) + sizeof(uint8_t) + sizeof(uint16_t) >
          buffer.size()) {
        return false;
      }
      std::memcpy(&entry.editPassIndex, buffer.data() + cursor, sizeof(entry.editPassIndex));
      cursor += sizeof(entry.editPassIndex);
      uint8_t editPassTypeRaw = 0;
      std::memcpy(&editPassTypeRaw, buffer.data() + cursor, sizeof(editPassTypeRaw));
      cursor += sizeof(editPassTypeRaw);
      entry.editPassType = static_cast<EditPassType>(editPassTypeRaw);
      uint16_t count = 0;
      std::memcpy(&count, buffer.data() + cursor, sizeof(count));
      cursor += sizeof(count);
      entry.editPassIds.clear();
      entry.editPassIds.reserve(count);
      for (uint16_t i = 0; i < count; ++i) {
        if (cursor + sizeof(EditPassId) > buffer.size()) {
          return false;
        }
        EditPassId id = kInvalidEditPassId;
        std::memcpy(&id, buffer.data() + cursor, sizeof(id));
        cursor += sizeof(id);
        entry.editPassIds.push_back(id);
      }
      return true;
    }
    default:
      return true;
  }
}

bool readGlobalUndoStackFromBuffer(const std::vector<uint8_t>& buffer, GlobalUndoStack& stack) {
  size_t cursor = 0;
  if (cursor + sizeof(uint32_t) * 3 > buffer.size()) {
    return false;
  }
  uint32_t entryCount = 0;
  uint32_t stackCursor = 0;
  uint32_t nextEntryId = 1;
  std::memcpy(&entryCount, buffer.data() + cursor, sizeof(entryCount));
  cursor += sizeof(entryCount);
  std::memcpy(&stackCursor, buffer.data() + cursor, sizeof(stackCursor));
  cursor += sizeof(stackCursor);
  std::memcpy(&nextEntryId, buffer.data() + cursor, sizeof(nextEntryId));
  cursor += sizeof(nextEntryId);

  bool scopedEditExtension = false;
  if (cursor + sizeof(uint32_t) <= buffer.size()) {
    uint32_t maybeToken = 0;
    std::memcpy(&maybeToken, buffer.data() + cursor, sizeof(maybeToken));
    if (maybeToken == kGlobalUndoStackScopedEditExtensionToken) {
      scopedEditExtension = true;
      cursor += sizeof(maybeToken);
    }
  }

  stack.clear();
  stack.nextEntryId = nextEntryId;
  stack.entries.reserve(entryCount);

  for (uint32_t i = 0; i < entryCount; ++i) {
    UndoEntry entry{};
    if (cursor + sizeof(entry.id) + sizeof(uint8_t) + sizeof(entry.slotIndex) +
            sizeof(entry.loopId) + sizeof(entry.passId) >
        buffer.size()) {
      return false;
    }
    std::memcpy(&entry.id, buffer.data() + cursor, sizeof(entry.id));
    cursor += sizeof(entry.id);
    uint8_t kindRaw = 0;
    std::memcpy(&kindRaw, buffer.data() + cursor, sizeof(kindRaw));
    cursor += sizeof(kindRaw);
    entry.kind = static_cast<UndoEntryKind>(kindRaw);
    std::memcpy(&entry.slotIndex, buffer.data() + cursor, sizeof(entry.slotIndex));
    cursor += sizeof(entry.slotIndex);
    std::memcpy(&entry.loopId, buffer.data() + cursor, sizeof(entry.loopId));
    cursor += sizeof(entry.loopId);
    std::memcpy(&entry.passId, buffer.data() + cursor, sizeof(entry.passId));
    cursor += sizeof(entry.passId);

    bool hasSnapshot = false;
    if (cursor + sizeof(hasSnapshot) > buffer.size()) {
      return false;
    }
    std::memcpy(&hasSnapshot, buffer.data() + cursor, sizeof(hasSnapshot));
    cursor += sizeof(hasSnapshot);
    if (hasSnapshot) {
      return false;
    }
    std::memcpy(&hasSnapshot, buffer.data() + cursor, sizeof(hasSnapshot));
    cursor += sizeof(hasSnapshot);
    if (hasSnapshot) {
      return false;
    }

    if (cursor + sizeof(entry.beforeGeometry) + sizeof(entry.afterGeometry) +
            sizeof(uint32_t) * 4 + sizeof(uint32_t) * 2 + sizeof(bool) * 2 >
        buffer.size()) {
      return false;
    }
    std::memcpy(&entry.beforeGeometry, buffer.data() + cursor, sizeof(entry.beforeGeometry));
    cursor += sizeof(entry.beforeGeometry);
    std::memcpy(&entry.afterGeometry, buffer.data() + cursor, sizeof(entry.afterGeometry));
    cursor += sizeof(entry.afterGeometry);
    cursor += sizeof(uint32_t) * 4;
    cursor += sizeof(uint32_t) * 2;
    cursor += sizeof(bool) * 2;

    if (!readScopedEditExtensionFromBuffer(buffer, cursor, entry, scopedEditExtension)) {
      return false;
    }

    stack.entries.push_back(std::move(entry));
  }

  stack.cursor = (stackCursor <= stack.entries.size()) ? stackCursor : stack.entries.size();
  return true;
}

EditPassIdList collectPendingDurableCheckpointIds(const EditPassIdList& currentIds,
                                                  const EditPassIdList& checkpointedIds) {
  EditPassIdList pending;
  for (const EditPassId id : currentIds) {
    bool alreadyCheckpointed = false;
    for (const EditPassId checkpointed : checkpointedIds) {
      if (checkpointed == id) {
        alreadyCheckpointed = true;
        break;
      }
    }
    if (!alreadyCheckpointed) {
      pending.push_back(id);
    }
  }
  return pending;
}

bool disableEditPasses(Loop& loop, const EditPassIdList& ids, EditPassType passType) {
  if (ids.empty()) {
    return false;
  }
  bool touched = false;
  for (const EditPassId id : ids) {
    for (EditPass& editPass : loop.passes.editPasses) {
      if (editPass.id != id || editPass.passType != passType) {
        continue;
      }
      if (editPass.state != EditPassState::Disabled) {
        editPass.state = EditPassState::Disabled;
        touched = true;
      }
    }
  }
  return touched;
}

RecordPass makeRecordPass() {
  LoopEventStore capture;
  TEST_ASSERT_TRUE(capture.append(MidiEvent::NoteOn(0, 1, 60, 100)));
  TEST_ASSERT_TRUE(capture.append(MidiEvent::NoteOff(48, 1, 60, 0)));
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(capture, committedChunkIds));
  RecordPass pass{};
  pass.id = 1;
  pass.state = CapturePassState::Active;
  pass.committedChunkIds = std::move(committedChunkIds);
  return pass;
}

EditPass makeUpdateRow(NoteId noteId, uint32_t startTick, uint32_t endTick) {
  EditPass row{};
  row.actionType = EditActionType::Update;
  row.propertyType = EditPropertyType::Length;
  row.targetNoteId = noteId;
  row.startTick = startTick;
  row.endTick = endTick;
  row.state = EditPassState::Active;
  return row;
}

void pushNoteEditPassClosedEntry(GlobalUndoStack& stack, uint8_t slotIndex, uint8_t editPassIndex,
                                 const EditPassIdList& ids) {
  UndoEntry entry{};
  entry.id = static_cast<UndoEntryId>(stack.nextEntryId++);
  entry.kind = UndoEntryKind::NoteEditPassClosed;
  entry.slotIndex = slotIndex;
  entry.loopId = 1;
  entry.editPassIndex = editPassIndex;
  entry.editPassType = EditPassType::Note;
  entry.editPassIds = ids;
  if (stack.cursor < stack.entries.size()) {
    stack.entries.erase(stack.entries.begin() + static_cast<std::ptrdiff_t>(stack.cursor),
                        stack.entries.end());
  }
  stack.entries.push_back(std::move(entry));
  stack.cursor = stack.entries.size();
}

}  // namespace

void test_scoped_edit_undo_round_trip_preserves_ids() {
  GlobalUndoStack stack;
  UndoEntry entry{};
  entry.id = 7;
  entry.kind = UndoEntryKind::NoteEditPassClosed;
  entry.slotIndex = 2;
  entry.editPassIndex = 3;
  entry.editPassType = EditPassType::Note;
  entry.editPassIds.push_back(11);
  entry.editPassIds.push_back(12);
  stack.entries.push_back(entry);
  stack.cursor = 1;
  stack.nextEntryId = 8;

  std::vector<uint8_t> buffer;
  writeGlobalUndoStackToBuffer(buffer, stack);

  GlobalUndoStack loaded;
  TEST_ASSERT_TRUE(readGlobalUndoStackFromBuffer(buffer, loaded));
  TEST_ASSERT_EQUAL(1u, loaded.entries.size());
  TEST_ASSERT_EQUAL(UndoEntryKind::NoteEditPassClosed, loaded.entries[0].kind);
  TEST_ASSERT_EQUAL_UINT8(3, loaded.entries[0].editPassIndex);
  TEST_ASSERT_EQUAL(2u, loaded.entries[0].editPassIds.size());
  TEST_ASSERT_EQUAL(11u, loaded.entries[0].editPassIds[0]);
  TEST_ASSERT_EQUAL(12u, loaded.entries[0].editPassIds[1]);
}

void test_legacy_stack_read_leaves_scoped_ids_empty() {
  std::vector<uint8_t> buffer;
  const uint32_t entryCount = 1;
  const uint32_t cursor = 1;
  const uint32_t nextEntryId = 2;
  appendRaw(buffer, entryCount);
  appendRaw(buffer, cursor);
  appendRaw(buffer, nextEntryId);

  UndoEntry entry{};
  entry.id = 5;
  entry.kind = UndoEntryKind::NoteEditPassClosed;
  entry.slotIndex = 0;
  entry.editPassIndex = 1;
  appendRaw(buffer, entry.id);
  const uint8_t kind = static_cast<uint8_t>(entry.kind);
  appendRaw(buffer, kind);
  appendRaw(buffer, entry.slotIndex);
  appendRaw(buffer, entry.loopId);
  appendRaw(buffer, entry.passId);
  appendNullSnapshotFlag(buffer);
  appendNullSnapshotFlag(buffer);
  appendUndoEntryTail(buffer, entry);

  GlobalUndoStack loaded;
  TEST_ASSERT_TRUE(readGlobalUndoStackFromBuffer(buffer, loaded));
  TEST_ASSERT_TRUE(loaded.entries[0].editPassIds.empty());
}

void test_round_trip_undo_disables_edit_pass_rows() {
  Loop loop;
  loop.loopLengthTicks = 1536;
  loop.passes.recordPass = makeRecordPass();

  const EditPassId id =
      loop.saveNoteEditPass(0, makeUpdateRow(1, 0, 96), EditPassType::Note);
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, id);

  GlobalUndoStack stack;
  pushNoteEditPassClosedEntry(stack, 0, 0, EditPassIdList{id});

  std::vector<uint8_t> buffer;
  writeGlobalUndoStackToBuffer(buffer, stack);
  GlobalUndoStack loaded;
  TEST_ASSERT_TRUE(readGlobalUndoStackFromBuffer(buffer, loaded));

  const UndoEntry& tip = loaded.entries[loaded.cursor - 1];
  TEST_ASSERT_TRUE(disableEditPasses(loop, tip.editPassIds, tip.editPassType));
  for (const EditPass& row : loop.passes.editPasses) {
    if (row.id == id) {
      TEST_ASSERT_EQUAL(static_cast<int>(EditPassState::Disabled), static_cast<int>(row.state));
    }
  }
}

void test_repeated_durable_checkpoint_stack_order_abc() {
  GlobalUndoStack stack;
  EditPassIdList currentIds;
  EditPassIdList checkpointedIds;
  bool durableCheckpointCreated = false;

  currentIds.push_back(1);
  EditPassIdList pending =
      collectPendingDurableCheckpointIds(currentIds, checkpointedIds);
  TEST_ASSERT_EQUAL(1u, pending.size());
  pushNoteEditPassClosedEntry(stack, 0, 0, pending);
  for (const EditPassId id : pending) {
    checkpointedIds.push_back(id);
  }
  durableCheckpointCreated = true;

  pending = collectPendingDurableCheckpointIds(currentIds, checkpointedIds);
  TEST_ASSERT_EQUAL(0u, pending.size());

  currentIds.push_back(2);
  durableCheckpointCreated = false;
  pending = collectPendingDurableCheckpointIds(currentIds, checkpointedIds);
  TEST_ASSERT_EQUAL(1u, pending.size());
  TEST_ASSERT_EQUAL(2u, pending[0]);
  pushNoteEditPassClosedEntry(stack, 0, 0, pending);
  for (const EditPassId id : pending) {
    checkpointedIds.push_back(id);
  }
  durableCheckpointCreated = true;

  currentIds.push_back(3);
  durableCheckpointCreated = false;
  pending = collectPendingDurableCheckpointIds(currentIds, checkpointedIds);
  TEST_ASSERT_EQUAL(1u, pending.size());
  TEST_ASSERT_EQUAL(3u, pending[0]);
  pushNoteEditPassClosedEntry(stack, 0, 0, pending);

  TEST_ASSERT_EQUAL(3u, stack.undoCount());
  TEST_ASSERT_EQUAL(UndoEntryKind::NoteEditPassClosed, stack.entries[2].kind);
  TEST_ASSERT_EQUAL(1u, stack.entries[2].editPassIds.size());
  TEST_ASSERT_EQUAL(3u, stack.entries[2].editPassIds[0]);
  TEST_ASSERT_EQUAL(1u, stack.entries[1].editPassIds.size());
  TEST_ASSERT_EQUAL(2u, stack.entries[1].editPassIds[0]);
  TEST_ASSERT_EQUAL(1u, stack.entries[0].editPassIds.size());
  TEST_ASSERT_EQUAL(1u, stack.entries[0].editPassIds[0]);
  (void)durableCheckpointCreated;
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_scoped_edit_undo_round_trip_preserves_ids);
  RUN_TEST(test_legacy_stack_read_leaves_scoped_ids_empty);
  RUN_TEST(test_round_trip_undo_disables_edit_pass_rows);
  RUN_TEST(test_repeated_durable_checkpoint_stack_order_abc);
  return UNITY_END();
}
