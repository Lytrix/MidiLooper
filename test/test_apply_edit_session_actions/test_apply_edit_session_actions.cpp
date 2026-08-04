//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <algorithm>
#include <unity.h>
#include <cstdint>

#include "ApplyEditSessionActions.h"
#include "EditSessionActionBuilder.h"
#include "EditSessionLiveStoreSpan.h"
#include "MidiEvent.h"
#include "Utils/LoopEventValidation.h"
#include "Utils/NoteMovementWrap.h"

#include "../../src/ApplyEditSessionActions.cpp"
#include "../../src/EditSessionActionBuilder.cpp"
#include "../../src/EditSessionLiveStoreSpan.cpp"
#include "../../src/Logger.cpp"
#include "../../src/NoteEditFocus.cpp"
#include "../../src/Utils/IntervalProjection.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/Utils/NoteUtils.cpp"

namespace {

constexpr uint8_t kChannel = 1;

MidiEvent noteOnWithNoteId(uint32_t tick, uint8_t channel, uint8_t pitch, uint8_t velocity,
                           NoteId noteId) {
  MidiEvent evt = MidiEvent::NoteOn(tick, channel, pitch, velocity);
  evt.noteId = noteId;
  return evt;
}

bool hasNoteOnAt(const MidiEventVec& events, uint32_t tick, uint8_t channel, uint8_t pitch) {
  for (const MidiEvent& evt : events) {
    if (evt.isNoteOn() && evt.data.noteData.velocity > 0 && evt.channel == channel &&
        evt.data.noteData.note == pitch && evt.tick == tick) {
      return true;
    }
  }
  return false;
}

bool hasNoteOffAt(const MidiEventVec& events, uint32_t tick, uint8_t channel, uint8_t pitch) {
  for (const MidiEvent& evt : events) {
    if (evt.isNoteOff() && evt.channel == channel && evt.data.noteData.note == pitch &&
        evt.tick == tick) {
      return true;
    }
  }
  return false;
}

NoteEditFocus makeMovingFocus(NoteId movingId, uint8_t pitch, uint32_t start, uint32_t end) {
  NoteEditFocus focus{};
  focus.active = true;
  focus.movingNoteId = movingId;
  focus.last = {pitch, 100, start, end};
  focus.commitBaseline = focus.last;
  focus.movingNoteRange = {start, end};
  return focus;
}

}  // namespace

void test_apply_restore_hidden_neighbor_144458() {
  constexpr uint32_t loopLength = 2304;
  constexpr NoteId kNeighborId = 1;
  constexpr NoteId kMoverId = 3;

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(0, kChannel, 60, 100, kNeighborId));
  store.push_back(MidiEvent::NoteOff(144, kChannel, 60, 0));
  store.push_back(noteOnWithNoteId(384, kChannel, 60, 100, kMoverId));
  store.push_back(MidiEvent::NoteOff(480, kChannel, 60, 0));

  store.erase(std::remove_if(store.begin(), store.end(),
                             [&](const MidiEvent& evt) {
                               return evt.data.noteData.note == 60 &&
                                      ((evt.isNoteOn() && evt.tick == 0) ||
                                       (evt.isNoteOff() && evt.tick == 144));
                             }),
                store.end());

  NoteEditFocus focus = makeMovingFocus(kMoverId, 60, 384, 480);

  EditSessionActions actions;
  EditSessionAction restore{};
  restore.type = EditSessionActionType::RestoreNote;
  restore.targetNoteId = kNeighborId;
  restore.startTick = 0;
  restore.endTick = 144;
  restore.pitch = 60;
  restore.velocity = 100;
  actions.push_back(restore);

  applyEditSessionActions(actions, store, focus, kChannel, loopLength);

  TEST_ASSERT_TRUE(hasNoteOnAt(store, 0, kChannel, 60));
  TEST_ASSERT_TRUE(hasNoteOffAt(store, 144, kChannel, 60));
}

void test_apply_loop_seam_move_152335() {
  constexpr uint32_t loopLength = 1536;
  constexpr uint32_t noteLen = 191;
  constexpr NoteId kNoteId = 42;

  MidiEventVec store;
  store.push_back(MidiEvent::NoteOff(50, kChannel, 60, 0));
  MidiEvent on = MidiEvent::NoteOn(1344, kChannel, 60, 100);
  on.noteId = kNoteId;
  store.push_back(on);

  NoteEditFocus focus = makeMovingFocus(kNoteId, 60, 1344, 1344 + noteLen);

  EditSessionActions actions;
  EditSessionAction move{};
  move.type = EditSessionActionType::MoveNote;
  move.targetNoteId = kNoteId;
  move.startTick = 1345;
  move.endTick = NoteMovementUtils::linearStorageOffTickForSpanEnd(1345, noteLen);
  move.pitch = 60;
  move.velocity = 100;
  actions.push_back(move);

  applyEditSessionActions(actions, store, focus, kChannel, loopLength);

  TEST_ASSERT_TRUE(hasNoteOnAt(store, 1345, kChannel, 60));
  TEST_ASSERT_TRUE(hasNoteOffAt(store, 1536, kChannel, 60));
  TEST_ASSERT_FALSE(hasNoteOffAt(store, 0, kChannel, 60));

  std::sort(store.begin(), store.end(),
            [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
  const auto validation = LoopEventValidation::validateLoopEvents(
      store, loopLength, LoopEventValidation::kClosureLinearGeometryMask);
  TEST_ASSERT_TRUE(validation.passed);
}

void test_apply_hide_shorten_restore_combo() {
  constexpr uint32_t loopLength = 1536;
  constexpr NoteId kHeadId = 10;
  constexpr NoteId kTailId = 20;
  constexpr NoteId kMoverId = 30;

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(100, kChannel, 60, 100, kHeadId));
  store.push_back(MidiEvent::NoteOff(200, kChannel, 60, 0));
  store.push_back(noteOnWithNoteId(150, kChannel, 60, 100, kTailId));
  store.push_back(MidiEvent::NoteOff(300, kChannel, 60, 0));
  store.push_back(noteOnWithNoteId(250, kChannel, 60, 100, kMoverId));
  store.push_back(MidiEvent::NoteOff(400, kChannel, 60, 0));

  NoteEditFocus focus = makeMovingFocus(kMoverId, 60, 250, 400);

  BaselineMap baseline;
  baseline[kHeadId] = {60, 100, 100, 200};
  baseline[kTailId] = {60, 100, 150, 300};
  baseline[kMoverId] = {60, 100, 250, 400};

  std::vector<ConstrainedNoteGeometry, InternalHeapFirstAllocator<ConstrainedNoteGeometry>>
      constrained;
  ConstrainedNoteGeometry head{};
  head.noteId = kHeadId;
  head.visible = true;
  head.startTick = 100;
  head.endTick = 149;
  head.pitch = 60;
  constrained.push_back(head);

  ConstrainedNoteGeometry tail{};
  tail.noteId = kTailId;
  tail.visible = false;
  tail.startTick = 150;
  tail.endTick = 300;
  tail.pitch = 60;
  constrained.push_back(tail);

  EditedGeometry edited{};
  edited.selection.primaryNote = kMoverId;
  edited.selection.selectedNotes.push_back(kMoverId);
  EditedNoteSpan moverSpan{};
  moverSpan.noteId = kMoverId;
  moverSpan.span = {60, 100, 200, 350};
  edited.causingSpans.push_back(moverSpan);

  const EditSessionActions actions =
      buildEditSessionActions(constrained, edited, baseline, store, kChannel);

  applyEditSessionActions(actions, store, focus, kChannel, loopLength);

  TEST_ASSERT_TRUE(hasNoteOffAt(store, 149, kChannel, 60));
  TEST_ASSERT_FALSE(liveStoreHasNotePair(store, kTailId, kChannel));
  TEST_ASSERT_TRUE(hasNoteOnAt(store, 200, kChannel, 60));
}

void test_apply_boundary_split_same_tick() {
  MidiEventVec store;
  store.push_back(noteOnWithNoteId(100, kChannel, 60, 100, 1));
  store.push_back(MidiEvent::NoteOff(200, kChannel, 60, 0));
  store.push_back(noteOnWithNoteId(200, kChannel, 60, 100, 2));

  applyBoundarySplitForEditSession(store, kChannel);

  TEST_ASSERT_TRUE(hasNoteOffAt(store, 199, kChannel, 60));
  TEST_ASSERT_TRUE(hasNoteOnAt(store, 200, kChannel, 60));
}

void test_apply_syncs_focus_last_after_move() {
  constexpr uint32_t loopLength = 1536;
  constexpr NoteId kNoteId = 5;

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(100, kChannel, 60, 100, kNoteId));
  store.push_back(MidiEvent::NoteOff(200, kChannel, 60, 0));

  NoteEditFocus focus = makeMovingFocus(kNoteId, 60, 100, 200);

  EditSessionActions actions;
  EditSessionAction move{};
  move.type = EditSessionActionType::MoveNote;
  move.targetNoteId = kNoteId;
  move.startTick = 120;
  move.endTick = 220;
  move.pitch = 60;
  actions.push_back(move);

  applyEditSessionActions(actions, store, focus, kChannel, loopLength);

  TEST_ASSERT_EQUAL_UINT32(120u, focus.last.startTick);
  TEST_ASSERT_EQUAL_UINT32(220u, focus.last.endTick);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_apply_restore_hidden_neighbor_144458);
  RUN_TEST(test_apply_loop_seam_move_152335);
  RUN_TEST(test_apply_hide_shorten_restore_combo);
  RUN_TEST(test_apply_boundary_split_same_tick);
  RUN_TEST(test_apply_syncs_focus_last_after_move);
  return UNITY_END();
}
