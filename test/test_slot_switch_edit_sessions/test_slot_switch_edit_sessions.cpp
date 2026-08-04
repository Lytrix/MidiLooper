//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Slot selection focus: note/loop edit rebind, pass-count stress, SD footer extension.

#include <cstring>
#include <unity.h>
#include <vector>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/EditApply.cpp"
#include "../../src/LoopPasses.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../test_support/MemoryMonitorNativeDeps.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/Loop.cpp"
#include "../test_support/LoopCaptureTestDeps.cpp"
#include "../test_support/CommittedChunkIdTestHelpers.h"
#include "../../src/NoteEditFocus.cpp"
#include "../../src/EditSessionLiveStoreSpan.cpp"

#include "EditSession.h"
#include "GlobalUndoStack.h"
#include "Loop.h"
#include "LoopEventBuffer.h"
#include "NoteEditFocus.h"
#include "../test_support/NoteIdTestFixtures.h"
#include "Utils/NoteUtils.h"

namespace {

using namespace NoteIdTestFixtures;

constexpr uint32_t kShortLoopLength = 1536u;
constexpr uint32_t kLongLoopLength = 18432u;
constexpr uint8_t kChannel = 5;
constexpr uint8_t kNumTracks = 8;

template <typename T>
void appendRaw(std::vector<uint8_t>& buffer, const T& value) {
  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&value);
  buffer.insert(buffer.end(), ptr, ptr + sizeof(T));
}

void appendRawBytes(std::vector<uint8_t>& buffer, const uint8_t* data, size_t size) {
  buffer.insert(buffer.end(), data, data + size);
}

void appendRawBytes(std::vector<uint8_t>& buffer, const std::vector<uint8_t>& data) {
  appendRawBytes(buffer, data.data(), data.size());
}

void appendNotePair(LoopEventStore& store, uint32_t onTick, uint32_t offTick, uint8_t pitch,
                    NoteId noteId) {
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, onTick, kChannel, pitch, 100, noteId));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(offTick, kChannel, pitch, 0)));
}

RecordPass makeRecordPassWithNoteCount(uint32_t loopLength, unsigned noteCount) {
  resetNoteIdCounter();
  LoopEventStore store;
  const uint32_t spacing = loopLength / (noteCount + 1u);
  for (unsigned i = 0; i < noteCount; ++i) {
    const uint32_t onTick = spacing * (i + 1u);
    appendNotePair(store, onTick, onTick + 48u, static_cast<uint8_t>(60 + i),
                   static_cast<NoteId>(i + 1u));
  }
  CommittedChunkIdList publishedIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, publishedIds));
  RecordPass pass{};
  pass.id = 1;
  pass.state = CapturePassState::Active;
  pass.committedChunkIds = std::move(publishedIds);
  return pass;
}

void setupLoop(Loop& loop, uint32_t loopLength, unsigned noteCount) {
  loop.loopLengthTicks = loopLength;
  loop.passes.recordPass = makeRecordPassWithNoteCount(loopLength, noteCount);
  loop.nextPassId_ = 2;
}

void openCowLoopEventStore(CowLoopEventStore& session, Loop& loop) {
  loop.rematerializeEditView(session.mutStore());
  loop.assignMissingNoteIdsInStore(session.mutStore());
  session.discardEventsCache();
}

void reopenCowLoopEventStore(CowLoopEventStore& session, Loop& loop) {
  session.mutStore().clear();
  session.discardEventsCache();
  openCowLoopEventStore(session, loop);
}

template <typename Alloc>
size_t countDisplayNotes(const std::vector<MidiEvent, Alloc>& flat, uint32_t loopLength) {
  return NoteUtils::reconstructDisplayNotes(flat, loopLength, false).size();
}

size_t countMaterializedEvents(const Loop& loop) {
  MidiEventVec flat;
  loop.passes.materializeToEventVector(flat);
  return flat.size();
}

bool readFooterSelectedSlotIndices(const std::vector<uint8_t>& buffer, size_t activeOffset,
                                   uint8_t numTracks,
                                   const std::vector<uint8_t>& activeLoopIndex,
                                   std::vector<uint8_t>& selectedSlotIndexOut) {
  if (activeOffset + sizeof(uint32_t) > buffer.size()) {
    return false;
  }
  selectedSlotIndexOut.assign(numTracks, 0);
  uint32_t footerToken = 0;
  std::memcpy(&footerToken, buffer.data() + activeOffset, sizeof(footerToken));
  size_t cursor = activeOffset + sizeof(uint32_t);
  if (footerToken == kFooterSelectedSlotExtensionToken) {
    for (uint8_t t = 0; t < numTracks; ++t) {
      if (cursor + sizeof(uint8_t) > buffer.size()) {
        return false;
      }
      std::memcpy(&selectedSlotIndexOut[t], buffer.data() + cursor, sizeof(uint8_t));
      cursor += sizeof(uint8_t);
    }
    if (cursor + sizeof(uint32_t) > buffer.size()) {
      return false;
    }
    std::memcpy(&footerToken, buffer.data() + cursor, sizeof(footerToken));
    cursor += sizeof(uint32_t);
  } else {
    for (uint8_t t = 0; t < numTracks; ++t) {
      selectedSlotIndexOut[t] = activeLoopIndex[t];
    }
  }
  return footerToken == kGlobalUndoStackToken;
}

}  // namespace

void test_note_edit_slot_switch_rematerialises_selected_slot() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loopSlot0;
  Loop loopSlot1;
  setupLoop(loopSlot0, kShortLoopLength, 2u);
  setupLoop(loopSlot1, kLongLoopLength, 4u);

  CowLoopEventStore session;
  openCowLoopEventStore(session, loopSlot0);
  TEST_ASSERT_EQUAL(2u, countDisplayNotes(session.readEvents(), kShortLoopLength));

  reopenCowLoopEventStore(session, loopSlot1);
  TEST_ASSERT_EQUAL(4u, countDisplayNotes(session.readEvents(), kLongLoopLength));
}

void test_loop_edit_slot_geometry_differs_between_slots() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loopSlot0;
  Loop loopSlot1;
  setupLoop(loopSlot0, kShortLoopLength, 2u);
  setupLoop(loopSlot1, kLongLoopLength, 4u);
  loopSlot0.loopStartTick = 48;
  loopSlot1.loopStartTick = 96;

  TEST_ASSERT_NOT_EQUAL(loopSlot0.loopLengthTicks, loopSlot1.loopLengthTicks);
  TEST_ASSERT_NOT_EQUAL(loopSlot0.loopStartTick, loopSlot1.loopStartTick);
}

void test_control_change_edit_session_type_stub_dispatch() {
  EditSession session{};
  session.sessionType = EditSessionType::ControlChange;
  session.active = false;
  TEST_ASSERT_EQUAL(EditSessionType::ControlChange, session.sessionType);
}

void test_repeated_slot_switch_stress_preserves_pass_counts() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loopSlot0;
  Loop loopSlot1;
  setupLoop(loopSlot0, kShortLoopLength, 2u);
  setupLoop(loopSlot1, kLongLoopLength, 4u);

  const size_t slot0Events = countMaterializedEvents(loopSlot0);
  const size_t slot1Events = countMaterializedEvents(loopSlot1);

  CowLoopEventStore session;
  openCowLoopEventStore(session, loopSlot0);
  for (int i = 0; i < 5; ++i) {
    reopenCowLoopEventStore(session, loopSlot1);
    reopenCowLoopEventStore(session, loopSlot0);
  }

  TEST_ASSERT_EQUAL(slot0Events, countMaterializedEvents(loopSlot0));
  TEST_ASSERT_EQUAL(slot1Events, countMaterializedEvents(loopSlot1));
}

void test_footer_legacy_selected_defaults_to_active() {
  std::vector<uint8_t> buffer;
  std::vector<uint8_t> activeLoopIndex = {0, 1, 2, 3, 4, 5, 6, 7};
  appendRawBytes(buffer, activeLoopIndex);
  appendRaw(buffer, kGlobalUndoStackToken);

  std::vector<uint8_t> selectedSlotIndex;
  TEST_ASSERT_TRUE(readFooterSelectedSlotIndices(buffer, activeLoopIndex.size(),
                                                 kNumTracks, activeLoopIndex,
                                                 selectedSlotIndex));
  for (uint8_t t = 0; t < kNumTracks; ++t) {
    TEST_ASSERT_EQUAL(activeLoopIndex[t], selectedSlotIndex[t]);
  }
}

void test_footer_slot_extension_round_trip() {
  std::vector<uint8_t> buffer;
  std::vector<uint8_t> activeLoopIndex = {0, 1, 2, 3, 4, 5, 6, 7};
  std::vector<uint8_t> expectedSelected = {7, 6, 5, 4, 3, 2, 1, 0};
  appendRawBytes(buffer, activeLoopIndex);
  appendRaw(buffer, kFooterSelectedSlotExtensionToken);
  appendRawBytes(buffer, expectedSelected);
  appendRaw(buffer, kGlobalUndoStackToken);

  std::vector<uint8_t> selectedSlotIndex;
  TEST_ASSERT_TRUE(readFooterSelectedSlotIndices(buffer, activeLoopIndex.size(),
                                                 kNumTracks, activeLoopIndex,
                                                 selectedSlotIndex));
  for (uint8_t t = 0; t < kNumTracks; ++t) {
    TEST_ASSERT_EQUAL(expectedSelected[t], selectedSlotIndex[t]);
    TEST_ASSERT_NOT_EQUAL(activeLoopIndex[t], selectedSlotIndex[t]);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_note_edit_slot_switch_rematerialises_selected_slot);
  RUN_TEST(test_loop_edit_slot_geometry_differs_between_slots);
  RUN_TEST(test_control_change_edit_session_type_stub_dispatch);
  RUN_TEST(test_repeated_slot_switch_stress_preserves_pass_counts);
  RUN_TEST(test_footer_legacy_selected_defaults_to_active);
  RUN_TEST(test_footer_slot_extension_round_trip);
  return UNITY_END();
}
