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

bool actionsContainTypeForNote(const EditSessionActions& actions, EditSessionActionType type,
                               NoteId noteId) {
  for (const EditSessionAction& action : actions) {
    if (action.type == type && action.targetNoteId == noteId) {
      return true;
    }
  }
  return false;
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
      buildEditSessionActions(constrained, edited, baseline, store, kChannel, focus, loopLength);

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

void test_apply_restore_extends_shortened_neighbor_end() {
  // Spec: RestoreNote extends baseline span when the live pair is still present but shortened.
  constexpr uint32_t loopLength = 2400;
  constexpr NoteId kNeighborId = 3;
  constexpr NoteId kMoverId = 4;

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(666, kChannel, 64, 100, kNeighborId));
  MidiEvent shortenedOff = MidiEvent::NoteOff(713, kChannel, 64, 0);
  shortenedOff.noteId = kNeighborId;
  store.push_back(shortenedOff);
  store.push_back(noteOnWithNoteId(714, kChannel, 64, 100, kMoverId));
  MidiEvent moverOff = MidiEvent::NoteOff(863, kChannel, 64, 0);
  moverOff.noteId = kMoverId;
  store.push_back(moverOff);

  NoteEditFocus focus = makeMovingFocus(kMoverId, 64, 714, 863);

  EditSessionActions actions;
  EditSessionAction restore{};
  restore.type = EditSessionActionType::RestoreNote;
  restore.targetNoteId = kNeighborId;
  restore.startTick = 666;
  restore.endTick = 815;
  restore.pitch = 64;
  restore.velocity = 100;
  actions.push_back(restore);

  applyEditSessionActions(actions, store, focus, kChannel, loopLength);

  TEST_ASSERT_TRUE(hasNoteOnAt(store, 666, kChannel, 64));
  TEST_ASSERT_TRUE(hasNoteOffAt(store, 815, kChannel, 64));
  TEST_ASSERT_FALSE(hasNoteOffAt(store, 713, kChannel, 64));
}

void test_apply_shorten_uses_baseline_end_when_mover_overlaps() {
  // session_20260804_212156: overlapping same-pitch mover must not steal neighbor shorten target.
  constexpr uint32_t loopLength = 2400;
  constexpr NoteId kNeighborId = 3;
  constexpr NoteId kMoverId = 4;

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(666, kChannel, 64, 100, kNeighborId));
  store.push_back(MidiEvent::NoteOff(815, kChannel, 64, 0));  // no noteId on off
  store.push_back(noteOnWithNoteId(714, kChannel, 64, 100, kMoverId));
  store.push_back(MidiEvent::NoteOff(863, kChannel, 64, 0));

  NoteEditFocus focus = makeMovingFocus(kMoverId, 64, 714, 863);
  focus.baselineMap[kNeighborId] = {64, 100, 666, 815};
  focus.baselineMap[kMoverId] = {64, 100, 714, 863};

  EditSessionActions actions;
  EditSessionAction shorten{};
  shorten.type = EditSessionActionType::ShortenNote;
  shorten.targetNoteId = kNeighborId;
  shorten.startTick = 666;
  shorten.endTick = 713;
  shorten.pitch = 64;
  shorten.velocity = 100;
  actions.push_back(shorten);

  applyEditSessionActions(actions, store, focus, kChannel, loopLength);

  TEST_ASSERT_TRUE(hasNoteOffAt(store, 713, kChannel, 64));
  TEST_ASSERT_TRUE(hasNoteOffAt(store, 863, kChannel, 64));
  TEST_ASSERT_FALSE(hasNoteOffAt(store, 815, kChannel, 64));
}

void test_apply_restore_left_neighbor_does_not_steal_mover_off_ltr() {
  // session_20260804_215743: L→R leave-left Restore picked farthest same-pitch untagged
  // off (mover end) and wrote the left baseline end onto the selected note.
  constexpr uint32_t loopLength = 1536;
  constexpr NoteId kLeftId = 2;
  constexpr NoteId kMoverId = 3;
  constexpr NoteId kRightId = 4;

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(700, kChannel, 65, 100, kLeftId));
  store.push_back(MidiEvent::NoteOff(899, kChannel, 65, 0));  // shortened; untagged
  store.push_back(noteOnWithNoteId(900, kChannel, 65, 100, kMoverId));
  store.push_back(MidiEvent::NoteOff(1049, kChannel, 65, 0));  // mover; untagged
  store.push_back(noteOnWithNoteId(1100, kChannel, 65, 100, kRightId));
  store.push_back(MidiEvent::NoteOff(1200, kChannel, 65, 0));

  NoteEditFocus focus = makeMovingFocus(kMoverId, 65, 900, 1049);
  focus.baselineMap[kLeftId] = {65, 100, 700, 950};
  focus.baselineMap[kMoverId] = {65, 100, 850, 999};
  focus.baselineMap[kRightId] = {65, 100, 1100, 1200};

  EditSessionActions actions;
  EditSessionAction restore{};
  restore.type = EditSessionActionType::RestoreNote;
  restore.targetNoteId = kLeftId;
  restore.startTick = 700;
  restore.endTick = 950;
  restore.pitch = 65;
  restore.velocity = 100;
  actions.push_back(restore);

  EditSessionAction hide{};
  hide.type = EditSessionActionType::HideNote;
  hide.targetNoteId = kRightId;
  hide.startTick = 1100;
  hide.endTick = 1200;
  hide.pitch = 65;
  hide.velocity = 100;
  actions.push_back(hide);

  EditSessionAction move{};
  move.type = EditSessionActionType::MoveNote;
  move.targetNoteId = kMoverId;
  move.startTick = 948;
  move.endTick = 1097;
  move.pitch = 65;
  move.velocity = 100;
  actions.push_back(move);

  applyEditSessionActions(actions, store, focus, kChannel, loopLength);

  TEST_ASSERT_TRUE(hasNoteOnAt(store, 700, kChannel, 65));
  TEST_ASSERT_TRUE(hasNoteOffAt(store, 950, kChannel, 65));
  TEST_ASSERT_TRUE(hasNoteOnAt(store, 948, kChannel, 65));
  TEST_ASSERT_TRUE(hasNoteOffAt(store, 1097, kChannel, 65));
  TEST_ASSERT_FALSE(hasNoteOnAt(store, 1100, kChannel, 65));
  TEST_ASSERT_EQUAL_UINT32(948u, focus.last.startTick);
  TEST_ASSERT_EQUAL_UINT32(1097u, focus.last.endTick);
}

void test_apply_reinsert_shortened_left_stub_after_complete_cover_hide_ltr() {
  // After CompleteCover removed left, L→R OverlapNoteOff restores stub then moves.
  constexpr uint32_t loopLength = 1536;
  constexpr NoteId kLeftId = 2;
  constexpr NoteId kMoverId = 3;

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(954, kChannel, 65, 100, kMoverId));
  store.push_back(MidiEvent::NoteOff(1145, kChannel, 65, 0));

  NoteEditFocus focus = makeMovingFocus(kMoverId, 65, 954, 1145);
  focus.baselineMap[kLeftId] = {65, 100, 906, 1055};
  focus.baselineMap[kMoverId] = {65, 100, 1098, 1290};

  EditSessionActions actions;
  EditSessionAction restore{};
  restore.type = EditSessionActionType::RestoreNote;
  restore.targetNoteId = kLeftId;
  restore.startTick = 906;
  restore.endTick = 953;
  restore.pitch = 65;
  restore.velocity = 100;
  actions.push_back(restore);

  EditSessionAction move{};
  move.type = EditSessionActionType::MoveNote;
  move.targetNoteId = kMoverId;
  move.startTick = 1002;
  move.endTick = 1193;
  move.pitch = 65;
  move.velocity = 100;
  actions.push_back(move);

  applyEditSessionActions(actions, store, focus, kChannel, loopLength);

  TEST_ASSERT_TRUE(hasNoteOnAt(store, 906, kChannel, 65));
  TEST_ASSERT_TRUE(hasNoteOffAt(store, 953, kChannel, 65));
  TEST_ASSERT_TRUE(hasNoteOnAt(store, 1002, kChannel, 65));
  TEST_ASSERT_TRUE(hasNoteOffAt(store, 1193, kChannel, 65));
  TEST_ASSERT_EQUAL_UINT32(1002u, focus.last.startTick);
  TEST_ASSERT_EQUAL_UINT32(1193u, focus.last.endTick);
}

void test_apply_hide_removes_untagged_off_so_mover_keeps_own_end() {
  // session_20260804_222039: Hide left (CompleteCover) left an untagged orphan off at the
  // left end; sync/display then paired the mover to that end while also touching the right
  // note-on.
  constexpr uint32_t loopLength = 1536;
  constexpr NoteId kLeftId = 2;
  constexpr NoteId kMoverId = 3;
  constexpr NoteId kRightId = 4;

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(810, kChannel, 65, 100, kLeftId));
  store.push_back(MidiEvent::NoteOff(959, kChannel, 65, 0));  // untagged
  store.push_back(noteOnWithNoteId(900, kChannel, 65, 100, kMoverId));
  store.push_back(MidiEvent::NoteOff(1049, kChannel, 65, 0));  // untagged mover
  store.push_back(noteOnWithNoteId(1050, kChannel, 65, 100, kRightId));
  store.push_back(MidiEvent::NoteOff(1200, kChannel, 65, 0));

  NoteEditFocus focus = makeMovingFocus(kMoverId, 65, 900, 1049);
  focus.baselineMap[kLeftId] = {65, 100, 810, 959};
  focus.baselineMap[kMoverId] = {65, 100, 810, 959};
  focus.baselineMap[kRightId] = {65, 100, 1050, 1200};

  EditSessionActions actions;
  EditSessionAction hideLeft{};
  hideLeft.type = EditSessionActionType::HideNote;
  hideLeft.targetNoteId = kLeftId;
  hideLeft.startTick = 810;
  hideLeft.endTick = 959;
  hideLeft.pitch = 65;
  hideLeft.velocity = 100;
  actions.push_back(hideLeft);

  EditSessionAction hideRight{};
  hideRight.type = EditSessionActionType::HideNote;
  hideRight.targetNoteId = kRightId;
  hideRight.startTick = 1050;
  hideRight.endTick = 1200;
  hideRight.pitch = 65;
  hideRight.velocity = 100;
  actions.push_back(hideRight);

  EditSessionAction move{};
  move.type = EditSessionActionType::MoveNote;
  move.targetNoteId = kMoverId;
  move.startTick = 948;
  move.endTick = 1097;
  move.pitch = 65;
  move.velocity = 100;
  actions.push_back(move);

  applyEditSessionActions(actions, store, focus, kChannel, loopLength);

  TEST_ASSERT_FALSE(hasNoteOnAt(store, 810, kChannel, 65));
  TEST_ASSERT_FALSE(hasNoteOffAt(store, 959, kChannel, 65));
  TEST_ASSERT_FALSE(hasNoteOnAt(store, 1050, kChannel, 65));
  TEST_ASSERT_TRUE(hasNoteOnAt(store, 948, kChannel, 65));
  TEST_ASSERT_TRUE(hasNoteOffAt(store, 1097, kChannel, 65));
  TEST_ASSERT_EQUAL_UINT32(948u, focus.last.startTick);
  TEST_ASSERT_EQUAL_UINT32(1097u, focus.last.endTick);
}

void test_apply_restore_orphan_on_does_not_duplicate_open_to_loop_end() {
  // session_20260804_224309: orphan overlap note-on + RestoreNote previously pushed a second
  // on/off pair. The leftover open on reconstructs to endTick = loopLength - 1.
  constexpr uint32_t loopLength = 1536;
  constexpr NoteId kLeftId = 2;
  constexpr NoteId kMoverId = 3;

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(1002, kChannel, 65, 100, kLeftId));  // orphan on
  store.push_back(noteOnWithNoteId(1098, kChannel, 65, 100, kMoverId));
  store.push_back(MidiEvent::NoteOff(1193, kChannel, 65, 0));

  NoteEditFocus focus = makeMovingFocus(kMoverId, 65, 1098, 1193);
  focus.baselineMap[kLeftId] = {65, 100, 1002, 1097};

  EditSessionActions actions;
  EditSessionAction restore{};
  restore.type = EditSessionActionType::RestoreNote;
  restore.targetNoteId = kLeftId;
  restore.startTick = 1002;
  restore.endTick = 1097;
  restore.pitch = 65;
  restore.velocity = 100;
  actions.push_back(restore);

  applyEditSessionActions(actions, store, focus, kChannel, loopLength);

  size_t leftOns = 0;
  size_t leftOffs = 0;
  for (const MidiEvent& evt : store) {
    if (evt.channel != kChannel || evt.data.noteData.note != 65) {
      continue;
    }
    if (evt.noteId == kLeftId && evt.isNoteOn() && evt.data.noteData.velocity > 0) {
      ++leftOns;
    }
    if ((evt.noteId == kLeftId || evt.noteId == kInvalidNoteId) && evt.isNoteOff() &&
        evt.tick == 1097) {
      ++leftOffs;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(1u, leftOns);
  TEST_ASSERT_TRUE(leftOffs >= 1u);
  TEST_ASSERT_TRUE(hasNoteOnAt(store, 1002, kChannel, 65));
  TEST_ASSERT_TRUE(hasNoteOffAt(store, 1097, kChannel, 65));

  NoteBaseline leftSpan{};
  TEST_ASSERT_TRUE(readLiveLinearSpan(store, kLeftId, kChannel, leftSpan));
  TEST_ASSERT_EQUAL_UINT32(1002u, leftSpan.startTick);
  TEST_ASSERT_EQUAL_UINT32(1097u, leftSpan.endTick);
  TEST_ASSERT_TRUE(leftSpan.endTick != loopLength - 1);

  const auto display = NoteUtils::reconstructNotes(store, loopLength, false);
  for (const NoteUtils::DisplayNote& note : display) {
    if (note.note != 65 || note.startTick != 1002) {
      continue;
    }
    TEST_ASSERT_TRUE(note.endTick != loopLength - 1);
    TEST_ASSERT_EQUAL_UINT32(1097u, note.endTick);
  }
}

void test_apply_move_wrap_head_does_not_steal_left_neighbor_off() {
  // Move fallback must not relocate a left neighbor's earlier off when the mover has no
  // linear off — that leaves the neighbor open to loopLength-1.
  constexpr uint32_t loopLength = 1536;
  constexpr NoteId kLeftId = 2;
  constexpr NoteId kMoverId = 3;

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(1002, kChannel, 65, 100, kLeftId));
  store.push_back(MidiEvent::NoteOff(1097, kChannel, 65, 0));  // left untagged off
  store.push_back(noteOnWithNoteId(1098, kChannel, 65, 100, kMoverId));
  // mover intentionally has no off (resolve fails → wrap-head path)

  NoteEditFocus focus = makeMovingFocus(kMoverId, 65, 1098, 1193);

  EditSessionActions actions;
  EditSessionAction move{};
  move.type = EditSessionActionType::MoveNote;
  move.targetNoteId = kMoverId;
  move.startTick = 1100;
  move.endTick = 1195;
  move.pitch = 65;
  move.velocity = 100;
  actions.push_back(move);

  applyEditSessionActions(actions, store, focus, kChannel, loopLength);

  TEST_ASSERT_TRUE(hasNoteOnAt(store, 1002, kChannel, 65));
  TEST_ASSERT_TRUE(hasNoteOffAt(store, 1097, kChannel, 65));
  TEST_ASSERT_TRUE(hasNoteOnAt(store, 1100, kChannel, 65));
  TEST_ASSERT_TRUE(hasNoteOffAt(store, 1195, kChannel, 65));

  NoteBaseline leftSpan{};
  TEST_ASSERT_TRUE(readLiveLinearSpan(store, kLeftId, kChannel, leftSpan));
  TEST_ASSERT_EQUAL_UINT32(1097u, leftSpan.endTick);
}

void test_apply_shorten_left_does_not_steal_mover_off_start_abut() {
  // session_20260804_230007: start-abut OverlapNoteOff Shorten LIFO-claimed the mover's
  // untagged off → selected shortened to left end; Move rewrote that off → left/right open
  // to loopLength-1.
  constexpr uint32_t loopLength = 1536;
  constexpr NoteId kLeftId = 2;
  constexpr NoteId kMoverId = 3;
  constexpr NoteId kRightId = 4;

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(1002, kChannel, 65, 100, kLeftId));
  // left off missing — Shorten would LIFO to mover off without the fix
  store.push_back(noteOnWithNoteId(1098, kChannel, 65, 100, kMoverId));
  store.push_back(MidiEvent::NoteOff(1241, kChannel, 65, 0));  // mover untagged
  store.push_back(noteOnWithNoteId(1242, kChannel, 65, 100, kRightId));
  store.push_back(MidiEvent::NoteOff(1337, kChannel, 65, 0));

  NoteEditFocus focus = makeMovingFocus(kMoverId, 65, 1098, 1241);
  focus.baselineMap[kLeftId] = {65, 100, 1002, 1097};
  focus.baselineMap[kRightId] = {65, 100, 1242, 1337};

  EditSessionActions actions;
  EditSessionAction shorten{};
  shorten.type = EditSessionActionType::ShortenNote;
  shorten.targetNoteId = kLeftId;
  shorten.startTick = 1002;
  shorten.endTick = 1097;
  shorten.pitch = 65;
  shorten.velocity = 100;
  actions.push_back(shorten);

  EditSessionAction hide{};
  hide.type = EditSessionActionType::HideNote;
  hide.targetNoteId = kRightId;
  hide.startTick = 1242;
  hide.endTick = 1337;
  hide.pitch = 65;
  hide.velocity = 100;
  actions.push_back(hide);

  EditSessionAction move{};
  move.type = EditSessionActionType::MoveNote;
  move.targetNoteId = kMoverId;
  move.startTick = 1098;
  move.endTick = 1241;
  move.pitch = 65;
  move.velocity = 100;
  actions.push_back(move);

  applyEditSessionActions(actions, store, focus, kChannel, loopLength);

  NoteBaseline leftSpan{};
  TEST_ASSERT_TRUE(readLiveLinearSpan(store, kLeftId, kChannel, leftSpan));
  TEST_ASSERT_EQUAL_UINT32(1002u, leftSpan.startTick);
  TEST_ASSERT_EQUAL_UINT32(1097u, leftSpan.endTick);
  TEST_ASSERT_TRUE(leftSpan.endTick != loopLength - 1);

  NoteBaseline moverSpan{};
  TEST_ASSERT_TRUE(readLiveLinearSpan(store, kMoverId, kChannel, moverSpan));
  TEST_ASSERT_EQUAL_UINT32(1098u, moverSpan.startTick);
  TEST_ASSERT_EQUAL_UINT32(1241u, moverSpan.endTick);
  TEST_ASSERT_EQUAL_UINT32(1098u, focus.last.startTick);
  TEST_ASSERT_EQUAL_UINT32(1241u, focus.last.endTick);

  TEST_ASSERT_FALSE(hasNoteOnAt(store, 1242, kChannel, 65));
  for (const MidiEvent& evt : store) {
    if (evt.channel != kChannel || evt.data.noteData.note != 65 || !evt.isNoteOn() ||
        evt.data.noteData.velocity == 0) {
      continue;
    }
    NoteBaseline span{};
    if (evt.noteId != kInvalidNoteId) {
      TEST_ASSERT_TRUE(readLiveLinearSpan(store, evt.noteId, kChannel, span));
      TEST_ASSERT_TRUE(span.endTick != loopLength - 1);
    }
  }
}

void test_apply_shorten_reuses_target_tagged_off_even_when_lifo_pairs_mover() {
  // session_20260804_230904: left tagged off was also mover's LIFO off; Shorten rejected it
  // and appended every tick → lag / stuck start=480.
  constexpr uint32_t loopLength = 1536;
  constexpr NoteId kLeftId = 2;
  constexpr NoteId kMoverId = 3;

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(400, kChannel, 65, 100, kLeftId));
  store.push_back(noteOnWithNoteId(480, kChannel, 65, 100, kMoverId));
  MidiEvent leftOff = MidiEvent::NoteOff(575, kChannel, 65, 0);
  leftOff.noteId = kLeftId;
  store.push_back(leftOff);
  store.push_back(MidiEvent::NoteOff(576, kChannel, 65, 0));  // mover untagged

  NoteEditFocus focus = makeMovingFocus(kMoverId, 65, 480, 576);
  focus.baselineMap[kLeftId] = {65, 100, 400, 575};

  EditSessionActions shortenOnly;
  EditSessionAction shorten{};
  shorten.type = EditSessionActionType::ShortenNote;
  shorten.targetNoteId = kLeftId;
  shorten.startTick = 400;
  shorten.endTick = 479;
  shorten.pitch = 65;
  shorten.velocity = 100;
  shortenOnly.push_back(shorten);

  const size_t sizeBefore = store.size();
  applyEditSessionActions(shortenOnly, store, focus, kChannel, loopLength);
  applyEditSessionActions(shortenOnly, store, focus, kChannel, loopLength);
  applyEditSessionActions(shortenOnly, store, focus, kChannel, loopLength);
  TEST_ASSERT_EQUAL(sizeBefore, store.size());

  NoteBaseline leftSpan{};
  TEST_ASSERT_TRUE(readLiveLinearSpan(store, kLeftId, kChannel, leftSpan));
  TEST_ASSERT_EQUAL_UINT32(479u, leftSpan.endTick);

  EditSessionAction move{};
  move.type = EditSessionActionType::MoveNote;
  move.targetNoteId = kMoverId;
  move.startTick = 500;
  move.endTick = 596;
  move.pitch = 65;
  move.velocity = 100;
  EditSessionActions moveOnly;
  moveOnly.push_back(move);
  applyEditSessionActions(moveOnly, store, focus, kChannel, loopLength);

  TEST_ASSERT_EQUAL_UINT32(500u, focus.last.startTick);
  TEST_ASSERT_EQUAL_UINT32(596u, focus.last.endTick);
  TEST_ASSERT_TRUE(hasNoteOnAt(store, 500, kChannel, 65));
  TEST_ASSERT_TRUE(hasNoteOffAt(store, 596, kChannel, 65));
}

void test_builder_emits_move_when_mover_on_has_no_off_but_focus_last_does() {
  // session_20260804_231426: readLiveLinearSpan needs a paired off; pitch only needs note-on.
  // buildEditSessionActions must still emit MoveNote via focus.last.
  constexpr uint32_t loopLength = 1536;
  constexpr NoteId kMoverId = 9;

  MidiEventVec store;
  MidiEvent on = MidiEvent::NoteOn(192, kChannel, 23, 100);
  on.noteId = kMoverId;
  store.push_back(on);

  NoteEditFocus focus = makeMovingFocus(kMoverId, 23, 192, 384);

  EditedGeometry edited{};
  edited.selection.primaryNote = kMoverId;
  edited.selection.selectedNotes.push_back(kMoverId);
  EditedNoteSpan causing{};
  causing.noteId = kMoverId;
  causing.span = {23, 100, 240, 432};
  edited.causingSpans.push_back(causing);

  const EditSessionActions actions =
      buildEditSessionActions({}, edited, BaselineMap{}, store, kChannel, focus, loopLength);
  TEST_ASSERT_EQUAL(1, static_cast<int>(actions.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(EditSessionActionType::MoveNote),
                    static_cast<int>(actions[0].type));

  applyEditSessionActions(actions, store, focus, kChannel, loopLength);
  TEST_ASSERT_EQUAL_UINT32(240u, focus.last.startTick);
  TEST_ASSERT_EQUAL_UINT32(432u, focus.last.endTick);
  TEST_ASSERT_TRUE(hasNoteOnAt(store, 240, kChannel, 23));
  TEST_ASSERT_TRUE(hasNoteOffAt(store, 432, kChannel, 23));
}

void test_builder_emits_change_length_when_store_shortened_but_focus_matches_causing() {
  // session_20260804_232321: store pair can be shortened while focus.last keeps full length.
  // Must not skip causing actions when the live store span still differs.
  constexpr uint32_t loopLength = 2304;
  constexpr NoteId kMoverId = 9;

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(144, kChannel, 23, 100, kMoverId));
  MidiEvent off = MidiEvent::NoteOff(192, kChannel, 23, 0);
  off.noteId = kMoverId;
  store.push_back(off);

  NoteEditFocus focus = makeMovingFocus(kMoverId, 23, 144, 336);

  EditedGeometry edited{};
  edited.selection.primaryNote = kMoverId;
  edited.selection.selectedNotes.push_back(kMoverId);
  EditedNoteSpan causing{};
  causing.noteId = kMoverId;
  causing.span = {23, 100, 144, 336};
  edited.causingSpans.push_back(causing);

  const EditSessionActions actions =
      buildEditSessionActions({}, edited, BaselineMap{}, store, kChannel, focus, loopLength);
  TEST_ASSERT_EQUAL(1, static_cast<int>(actions.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(EditSessionActionType::ChangeLength),
                    static_cast<int>(actions[0].type));
  TEST_ASSERT_EQUAL_UINT32(336u, actions[0].endTick);

  applyEditSessionActions(actions, store, focus, kChannel, loopLength);
  NoteBaseline moverSpan{};
  TEST_ASSERT_TRUE(
      findLinearNoteSpanForNoteId(store, kMoverId, kChannel, moverSpan, 144, loopLength));
  TEST_ASSERT_EQUAL_UINT32(336u, moverSpan.endTick);
}

void test_move_over_inner_overlap_keeps_mover_length_in_store() {
  constexpr uint32_t loopLength = 2304;
  constexpr NoteId kInnerId = 1;
  constexpr NoteId kMoverId = 9;

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(144, kChannel, 93, 100, kInnerId));
  MidiEvent innerOff = MidiEvent::NoteOff(192, kChannel, 93, 0);
  innerOff.noteId = kInnerId;
  store.push_back(innerOff);
  store.push_back(noteOnWithNoteId(192, kChannel, 23, 100, kMoverId));
  MidiEvent moverOff = MidiEvent::NoteOff(384, kChannel, 23, 0);
  moverOff.noteId = kMoverId;
  store.push_back(moverOff);

  NoteEditFocus focus = makeMovingFocus(kMoverId, 23, 192, 384);
  focus.baselineMap[kInnerId] = {93, 100, 144, 192};
  focus.baselineMap[kMoverId] = {23, 100, 192, 384};

  ConstrainedNoteGeometry inner{};
  inner.noteId = kInnerId;
  inner.visible = false;
  inner.pitch = 93;
  inner.startTick = 144;
  inner.endTick = 192;

  EditedGeometry edited{};
  edited.selection.primaryNote = kMoverId;
  edited.selection.selectedNotes.push_back(kMoverId);
  EditedNoteSpan mover{};
  mover.noteId = kMoverId;
  mover.span = {23, 100, 144, 336};
  edited.causingSpans.push_back(mover);

  const EditSessionActions actions = buildEditSessionActions(
      {inner}, edited, focus.baselineMap, store, kChannel, focus, loopLength);
  applyEditSessionActions(actions, store, focus, kChannel, loopLength);

  NoteBaseline moverSpan{};
  TEST_ASSERT_TRUE(
      findLinearNoteSpanForNoteId(store, kMoverId, kChannel, moverSpan, 144, loopLength));
  TEST_ASSERT_EQUAL_UINT32(144u, moverSpan.startTick);
  TEST_ASSERT_EQUAL_UINT32(336u, moverSpan.endTick);

  const NoteUtils::DisplayNoteVec display =
      NoteUtils::reconstructDisplayNotes(store, loopLength, false);
  bool foundMover = false;
  for (const NoteUtils::DisplayNote& dn : display) {
    if (dn.noteId != kMoverId) {
      continue;
    }
    foundMover = true;
    TEST_ASSERT_EQUAL_UINT32(144u, dn.startTick);
    const uint32_t endExclusive =
        (dn.endTick == loopLength - 1) ? loopLength : dn.endTick;
    TEST_ASSERT_EQUAL_UINT32(192u, endExclusive - dn.startTick);
  }
  TEST_ASSERT_TRUE(foundMover);
}

void test_same_pitch_complete_cover_hide_and_restore_on_leave() {
  // Q14: CompleteCover Hide/Restore on the mover's pitch lane only.
  constexpr uint32_t loopLength = 2304;
  constexpr NoteId kInnerId = 1;
  constexpr NoteId kMoverId = 9;

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(144, kChannel, 23, 100, kInnerId));
  MidiEvent innerOff = MidiEvent::NoteOff(192, kChannel, 23, 0);
  innerOff.noteId = kInnerId;
  store.push_back(innerOff);
  store.push_back(noteOnWithNoteId(192, kChannel, 23, 100, kMoverId));
  MidiEvent moverOff = MidiEvent::NoteOff(384, kChannel, 23, 0);
  moverOff.noteId = kMoverId;
  store.push_back(moverOff);

  NoteEditFocus focus = makeMovingFocus(kMoverId, 23, 192, 384);
  focus.baselineMap[kInnerId] = {23, 100, 144, 192};
  focus.baselineMap[kMoverId] = {23, 100, 192, 384};

  EditedGeometry overInner{};
  overInner.selection.primaryNote = kMoverId;
  overInner.selection.selectedNotes.push_back(kMoverId);
  EditedNoteSpan causingOver{};
  causingOver.noteId = kMoverId;
  causingOver.span = {23, 100, 144, 336};
  overInner.causingSpans.push_back(causingOver);

  ConstrainedNoteGeometry innerHidden{};
  innerHidden.noteId = kInnerId;
  innerHidden.visible = false;
  innerHidden.pitch = 23;
  innerHidden.startTick = 144;
  innerHidden.endTick = 192;

  const EditSessionActions overActions =
      buildEditSessionActions({innerHidden}, overInner, focus.baselineMap, store, kChannel, focus,
                              loopLength);
  TEST_ASSERT_TRUE(
      actionsContainTypeForNote(overActions, EditSessionActionType::HideNote, kInnerId));
  applyEditSessionActions(overActions, store, focus, kChannel, loopLength);
  TEST_ASSERT_FALSE(liveStoreHasNotePair(store, kInnerId, kChannel));
  TEST_ASSERT_TRUE(focus.baselineMap.count(kInnerId) > 0);

  // Relocate mover off the inner's baseline start so pitch+start resolve cannot confuse
  // the hidden noteId with the mover pair.
  for (MidiEvent& evt : store) {
    if (evt.noteId != kMoverId) {
      continue;
    }
    if (evt.isNoteOn() && evt.data.noteData.velocity > 0) {
      evt.tick = 400;
    } else if (evt.isNoteOff()) {
      evt.tick = 592;
    }
  }
  focus.last = {23, 100, 400, 592};

  EditedGeometry leaveInner{};
  leaveInner.selection = overInner.selection;
  EditedNoteSpan causingLeave{};
  causingLeave.noteId = kMoverId;
  causingLeave.span = {23, 100, 400, 592};
  leaveInner.causingSpans.push_back(causingLeave);

  ConstrainedNoteGeometry innerRestore{};
  innerRestore.noteId = kInnerId;
  innerRestore.visible = true;
  innerRestore.pitch = 23;
  innerRestore.startTick = 144;
  innerRestore.endTick = 192;

  const EditSessionActions leaveActions =
      buildEditSessionActions({innerRestore}, leaveInner, focus.baselineMap, store, kChannel,
                              focus, loopLength);
  TEST_ASSERT_TRUE(
      actionsContainTypeForNote(leaveActions, EditSessionActionType::RestoreNote, kInnerId));
  applyEditSessionActions(leaveActions, store, focus, kChannel, loopLength);

  NoteBaseline innerSpan{};
  TEST_ASSERT_TRUE(
      findLinearNoteSpanForNoteId(store, kInnerId, kChannel, innerSpan, 144, loopLength));
  TEST_ASSERT_EQUAL_UINT32(144u, innerSpan.startTick);
  TEST_ASSERT_EQUAL_UINT32(192u, innerSpan.endTick);
}

void test_log_scenario_same_pitch_hide_when_moving_right() {
  // Q14: Hide only same pitch lane. Mover covers head neighbor on pitch 12.
  constexpr uint32_t loopLength = 384;
  constexpr NoteId kHead12 = 77;
  constexpr NoteId kMoverId = 79;
  constexpr NoteId kCross93 = 3;

  MidiEventVec committed;
  committed.push_back(noteOnWithNoteId(0, kChannel, 12, 100, kHead12));
  committed.push_back(MidiEvent::NoteOff(192, kChannel, 12, 0));
  committed.push_back(noteOnWithNoteId(96, kChannel, 12, 100, kMoverId));
  committed.push_back(MidiEvent::NoteOff(192, kChannel, 12, 0));
  committed.push_back(noteOnWithNoteId(144, kChannel, 93, 100, kCross93));
  committed.push_back(MidiEvent::NoteOff(192, kChannel, 93, 0));

  MidiEventVec store = committed;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {12, 100, 96, 192};
  focus.last = focus.commitBaseline;
  focus.movingNoteRange = {96, 192};
  focus.baselineMap[kMoverId] = focus.commitBaseline;
  focus.baselineMap[kHead12] = {12, 100, 0, 192};
  focus.baselineMap[kCross93] = {93, 100, 144, 192};

  populateBaselineMapForEditClosure(focus, committed, store, kChannel, loopLength);
  TEST_ASSERT_TRUE(focus.baselineMap.count(kHead12) > 0);
  TEST_ASSERT_TRUE(focus.baselineMap.count(kCross93) > 0);

  EditorSelection selection{};
  selection.primaryNote = kMoverId;
  selection.selectedNotes.push_back(kMoverId);

  EditedGeometry edited{};
  edited.selection = selection;
  EditedNoteSpan causing{};
  causing.noteId = kMoverId;
  causing.span = {12, 100, 0, 240};
  edited.causingSpans.push_back(causing);

  ConstrainedNoteGeometry hiddenHead{};
  hiddenHead.noteId = kHead12;
  hiddenHead.visible = false;
  hiddenHead.pitch = 12;
  hiddenHead.startTick = 0;
  hiddenHead.endTick = 192;
  // Cross-pitch constrained geometry must not be produced by resolve (Q14); builder would
  // hide if fed one — verify baseline still holds cross-pitch for restore path only.
  ConstrainedNoteGeometry crossVisible{};
  crossVisible.noteId = kCross93;
  crossVisible.visible = true;
  crossVisible.pitch = 93;
  crossVisible.startTick = 144;
  crossVisible.endTick = 192;

  const EditSessionActions actions =
      buildEditSessionActions({hiddenHead, crossVisible}, edited, focus.baselineMap, store,
                              kChannel, focus, loopLength);
  TEST_ASSERT_TRUE(
      actionsContainTypeForNote(actions, EditSessionActionType::HideNote, kHead12));
  TEST_ASSERT_FALSE(
      actionsContainTypeForNote(actions, EditSessionActionType::HideNote, kCross93));
  TEST_ASSERT_TRUE(
      actionsContainTypeForNote(actions, EditSessionActionType::MoveNote, kMoverId));

  applyEditSessionActions(actions, store, focus, kChannel, loopLength);
  TEST_ASSERT_FALSE(liveStoreHasNotePair(store, kHead12, kChannel));
  TEST_ASSERT_TRUE(liveStoreHasNotePair(store, kCross93, kChannel));
  TEST_ASSERT_TRUE(focus.baselineMap.count(kHead12) > 0);
  TEST_ASSERT_TRUE(liveStoreHasNotePair(store, kMoverId, kChannel));

  NoteBaseline moverSpan{};
  TEST_ASSERT_TRUE(
      findLinearNoteSpanForNoteId(store, kMoverId, kChannel, moverSpan, 0, loopLength));
  TEST_ASSERT_EQUAL_UINT32(0u, moverSpan.startTick);
  TEST_ASSERT_EQUAL_UINT32(240u, moverSpan.endTick);
}

void test_same_pitch_left_neighbor_shorten_and_restore_on_leave() {
  constexpr uint32_t loopLength = 2304;
  constexpr NoteId kLeftId = 2;
  constexpr NoteId kMoverId = 9;

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(576, kChannel, 12, 100, kLeftId));
  MidiEvent leftOff = MidiEvent::NoteOff(720, kChannel, 12, 0);
  leftOff.noteId = kLeftId;
  store.push_back(leftOff);
  store.push_back(noteOnWithNoteId(720, kChannel, 12, 100, kMoverId));
  MidiEvent moverOff = MidiEvent::NoteOff(864, kChannel, 12, 0);
  moverOff.noteId = kMoverId;
  store.push_back(moverOff);

  NoteEditFocus focus = makeMovingFocus(kMoverId, 12, 720, 864);
  focus.baselineMap[kLeftId] = {12, 100, 576, 720};
  focus.baselineMap[kMoverId] = {12, 100, 720, 864};

  EditedGeometry ontoLeft{};
  ontoLeft.selection.primaryNote = kMoverId;
  ontoLeft.selection.selectedNotes.push_back(kMoverId);
  EditedNoteSpan causingOnto{};
  causingOnto.noteId = kMoverId;
  causingOnto.span = {12, 100, 672, 816};
  ontoLeft.causingSpans.push_back(causingOnto);

  ConstrainedNoteGeometry leftShortened{};
  leftShortened.noteId = kLeftId;
  leftShortened.visible = true;
  leftShortened.pitch = 12;
  leftShortened.startTick = 576;
  leftShortened.endTick = 671;

  const EditSessionActions ontoActions =
      buildEditSessionActions({leftShortened}, ontoLeft, focus.baselineMap, store, kChannel, focus,
                              loopLength);
  TEST_ASSERT_TRUE(
      actionsContainTypeForNote(ontoActions, EditSessionActionType::ShortenNote, kLeftId));
  applyEditSessionActions(ontoActions, store, focus, kChannel, loopLength);

  NoteBaseline leftSpan{};
  TEST_ASSERT_TRUE(
      findLinearNoteSpanForNoteId(store, kLeftId, kChannel, leftSpan, 576, loopLength));
  TEST_ASSERT_EQUAL_UINT32(671u, leftSpan.endTick);

  EditedGeometry leaveLeft{};
  leaveLeft.selection = ontoLeft.selection;
  EditedNoteSpan causingLeave{};
  causingLeave.noteId = kMoverId;
  causingLeave.span = {12, 100, 672, 816};
  leaveLeft.causingSpans.push_back(causingLeave);

  ConstrainedNoteGeometry leftRestore{};
  leftRestore.noteId = kLeftId;
  leftRestore.visible = true;
  leftRestore.pitch = 12;
  leftRestore.startTick = 576;
  leftRestore.endTick = 720;

  const EditSessionActions leaveActions =
      buildEditSessionActions({leftRestore}, leaveLeft, focus.baselineMap, store, kChannel, focus,
                              loopLength);
  TEST_ASSERT_TRUE(
      actionsContainTypeForNote(leaveActions, EditSessionActionType::RestoreNote, kLeftId));
  applyEditSessionActions(leaveActions, store, focus, kChannel, loopLength);
  TEST_ASSERT_TRUE(
      findLinearNoteSpanForNoteId(store, kLeftId, kChannel, leftSpan, 576, loopLength));
  TEST_ASSERT_EQUAL_UINT32(720u, leftSpan.endTick);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_apply_restore_hidden_neighbor_144458);
  RUN_TEST(test_apply_loop_seam_move_152335);
  RUN_TEST(test_apply_hide_shorten_restore_combo);
  RUN_TEST(test_apply_boundary_split_same_tick);
  RUN_TEST(test_apply_syncs_focus_last_after_move);
  RUN_TEST(test_apply_restore_extends_shortened_neighbor_end);
  RUN_TEST(test_apply_shorten_uses_baseline_end_when_mover_overlaps);
  RUN_TEST(test_apply_restore_left_neighbor_does_not_steal_mover_off_ltr);
  RUN_TEST(test_apply_reinsert_shortened_left_stub_after_complete_cover_hide_ltr);
  RUN_TEST(test_apply_hide_removes_untagged_off_so_mover_keeps_own_end);
  RUN_TEST(test_apply_restore_orphan_on_does_not_duplicate_open_to_loop_end);
  RUN_TEST(test_apply_move_wrap_head_does_not_steal_left_neighbor_off);
  RUN_TEST(test_apply_shorten_left_does_not_steal_mover_off_start_abut);
  RUN_TEST(test_apply_shorten_reuses_target_tagged_off_even_when_lifo_pairs_mover);
  RUN_TEST(test_builder_emits_move_when_mover_on_has_no_off_but_focus_last_does);
  RUN_TEST(test_builder_emits_change_length_when_store_shortened_but_focus_matches_causing);
  RUN_TEST(test_move_over_inner_overlap_keeps_mover_length_in_store);
  RUN_TEST(test_same_pitch_complete_cover_hide_and_restore_on_leave);
  RUN_TEST(test_log_scenario_same_pitch_hide_when_moving_right);
  RUN_TEST(test_same_pitch_left_neighbor_shorten_and_restore_on_leave);
  return UNITY_END();
}
