//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>
#include <cstdint>

#include "EditSessionActionBuilder.h"
#include "MidiEvent.h"

#include "../../src/EditManager/EditSessionActionBuilder.cpp"
#include "../../src/Logger.cpp"
#include "../../src/Utils/IntervalProjection.cpp"
#include "../test_support/NoteEditFocusTestDeps.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/Utils/NoteUtils.cpp"

namespace {

constexpr uint8_t kChannel = 1;
constexpr uint32_t kLoopLength = 1536;
NoteEditFocus kEmptyFocus{};

MidiEventVec makeLivePair(NoteId noteId, uint8_t pitch, uint32_t start, uint32_t end,
                          uint8_t velocity = 100) {
  MidiEventVec store;
  MidiEvent on = MidiEvent::NoteOn(start, kChannel, pitch, velocity);
  on.noteId = noteId;
  store.push_back(on);
  MidiEvent off = MidiEvent::NoteOff(end, kChannel, pitch, 0);
  off.noteId = noteId;
  store.push_back(off);
  return store;
}

EditedGeometry makeEditedGeometry(NoteId noteId, const NoteBaseline& span) {
  EditedGeometry geometry{};
  geometry.selection.primaryNote = noteId;
  geometry.selection.selectedNotes.push_back(noteId);
  EditedNoteSpan causing{};
  causing.noteId = noteId;
  causing.span = span;
  geometry.causingSpans.push_back(causing);
  return geometry;
}

}  // namespace

void test_builder_emits_restore_when_live_differs_from_baseline() {
  constexpr NoteId kTarget = 10;
  const NoteBaseline baseline{60, 100, 100, 200};
  ConstrainedNoteGeometry constrained{};
  constrained.noteId = kTarget;
  constrained.visible = true;
  constrained.startTick = baseline.startTick;
  constrained.endTick = baseline.endTick;
  constrained.pitch = baseline.pitch;

  MidiEventVec liveStore = makeLivePair(kTarget, 60, 100, 150);
  BaselineMap transactionBaseline;
  transactionBaseline[kTarget] = baseline;

  const EditSessionActions actions =
      buildEditSessionActions({constrained}, EditedGeometry{}, transactionBaseline, liveStore,
                              kChannel, kEmptyFocus, kLoopLength);
  TEST_ASSERT_EQUAL(1, static_cast<int>(actions.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(EditSessionActionType::RestoreNote),
                    static_cast<int>(actions[0].type));
  TEST_ASSERT_EQUAL_UINT32(kTarget, actions[0].targetNoteId);
}

void test_read_live_linear_span_ignores_nested_same_pitch_neighbor_off() {
  // Nearest-off-by-pitch would steal inner note's off@180; LIFO keeps outer end@300.
  constexpr NoteId kOuter = 1;
  constexpr NoteId kInner = 2;
  MidiEventVec liveStore;
  MidiEvent outerOn = MidiEvent::NoteOn(100, kChannel, 64, 100);
  outerOn.noteId = kOuter;
  liveStore.push_back(outerOn);
  MidiEvent innerOn = MidiEvent::NoteOn(150, kChannel, 64, 100);
  innerOn.noteId = kInner;
  liveStore.push_back(innerOn);
  liveStore.push_back(MidiEvent::NoteOff(180, kChannel, 64, 0));
  liveStore.push_back(MidiEvent::NoteOff(300, kChannel, 64, 0));

  NoteBaseline span{};
  TEST_ASSERT_TRUE(readLiveLinearSpan(liveStore, kOuter, kChannel, span));
  TEST_ASSERT_EQUAL_UINT32(100u, span.startTick);
  TEST_ASSERT_EQUAL_UINT32(300u, span.endTick);
}

void test_read_live_linear_span_resolves_across_store_channel() {
  constexpr NoteId kNoteId = 91;
  constexpr uint8_t kStoreChannel = 5;
  constexpr uint8_t kTrackChannel = 2;
  MidiEventVec liveStore;
  MidiEvent on = MidiEvent::NoteOn(912, kStoreChannel, 25, 100);
  on.noteId = kNoteId;
  liveStore.push_back(on);
  MidiEvent off = MidiEvent::NoteOff(1008, kStoreChannel, 25, 0);
  off.noteId = kNoteId;
  liveStore.push_back(off);

  NoteBaseline span{};
  TEST_ASSERT_TRUE(readLiveLinearSpan(liveStore, kNoteId, kTrackChannel, span));
  TEST_ASSERT_EQUAL_UINT32(912u, span.startTick);
  TEST_ASSERT_EQUAL_UINT32(1008u, span.endTick);
  TEST_ASSERT_EQUAL_UINT8(25u, span.pitch);
}

void test_builder_emits_hide_when_constrained_not_visible() {
  constexpr NoteId kTarget = 11;
  const NoteBaseline baseline{62, 100, 50, 150};
  ConstrainedNoteGeometry constrained{};
  constrained.noteId = kTarget;
  constrained.visible = false;
  constrained.pitch = baseline.pitch;

  MidiEventVec liveStore = makeLivePair(kTarget, 62, 50, 150);
  BaselineMap transactionBaseline;
  transactionBaseline[kTarget] = baseline;

  const EditSessionActions actions =
      buildEditSessionActions({constrained}, EditedGeometry{}, transactionBaseline, liveStore,
                              kChannel, kEmptyFocus, kLoopLength);
  TEST_ASSERT_EQUAL(1, static_cast<int>(actions.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(EditSessionActionType::HideNote),
                    static_cast<int>(actions[0].type));
}

void test_builder_omits_hide_when_live_already_absent() {
  constexpr NoteId kTarget = 12;
  const NoteBaseline baseline{64, 100, 80, 120};
  ConstrainedNoteGeometry constrained{};
  constrained.noteId = kTarget;
  constrained.visible = false;

  BaselineMap transactionBaseline;
  transactionBaseline[kTarget] = baseline;
  MidiEventVec emptyStore;

  const EditSessionActions actions =
      buildEditSessionActions({constrained}, EditedGeometry{}, transactionBaseline, emptyStore,
                              kChannel, kEmptyFocus, kLoopLength);
  TEST_ASSERT_EQUAL(0, static_cast<int>(actions.size()));
}

void test_builder_emits_shorten_when_constrained_end_shortened() {
  constexpr NoteId kTarget = 13;
  const NoteBaseline baseline{60, 100, 50, 200};
  ConstrainedNoteGeometry constrained{};
  constrained.noteId = kTarget;
  constrained.visible = true;
  constrained.startTick = 50;
  constrained.endTick = 119;
  constrained.pitch = 60;

  MidiEventVec liveStore = makeLivePair(kTarget, 60, 50, 200);
  BaselineMap transactionBaseline;
  transactionBaseline[kTarget] = baseline;

  const EditSessionActions actions =
      buildEditSessionActions({constrained}, EditedGeometry{}, transactionBaseline, liveStore,
                              kChannel, kEmptyFocus, kLoopLength);
  TEST_ASSERT_EQUAL(1, static_cast<int>(actions.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(EditSessionActionType::ShortenNote),
                    static_cast<int>(actions[0].type));
  TEST_ASSERT_EQUAL_UINT32(119u, actions[0].endTick);
}

void test_builder_reinserts_shortened_stub_when_live_absent_after_hide() {
  // session_20260804_220842: after CompleteCover Hide, L→R OverlapNoteOff must Restore
  // the ≥16th stub — ShortenNote alone no-ops when the pair is gone.
  constexpr NoteId kTarget = 14;
  const NoteBaseline baseline{65, 100, 906, 1055};
  ConstrainedNoteGeometry constrained{};
  constrained.noteId = kTarget;
  constrained.visible = true;
  constrained.startTick = 906;
  constrained.endTick = 953;
  constrained.pitch = 65;

  BaselineMap transactionBaseline;
  transactionBaseline[kTarget] = baseline;
  MidiEventVec emptyStore;

  // Causing span no longer covers baseline (partial OverlapNoteOff only).
  EditedGeometry edited = makeEditedGeometry(3, NoteBaseline{65, 100, 954, 1145});

  const EditSessionActions actions =
      buildEditSessionActions({constrained}, edited, transactionBaseline, emptyStore, kChannel,
                              kEmptyFocus, kLoopLength);
  TEST_ASSERT_EQUAL(1, static_cast<int>(actions.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(EditSessionActionType::RestoreNote),
                    static_cast<int>(actions[0].type));
  TEST_ASSERT_EQUAL_UINT32(906u, actions[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(953u, actions[0].endTick);
}

void test_builder_omits_restore_while_causing_still_completely_covers() {
  // session_20260804_223208: 100% same-length cover must keep the target removed.
  constexpr NoteId kTarget = 15;
  constexpr NoteId kMover = 16;
  const NoteBaseline baseline{65, 100, 1098, 1193};
  ConstrainedNoteGeometry constrained{};
  constrained.noteId = kTarget;
  constrained.visible = true;
  constrained.startTick = baseline.startTick;
  constrained.endTick = baseline.endTick;
  constrained.pitch = 65;

  BaselineMap transactionBaseline;
  transactionBaseline[kTarget] = baseline;
  MidiEventVec emptyStore;
  EditedGeometry edited =
      makeEditedGeometry(kMover, NoteBaseline{65, 100, 1098, 1193});

  const EditSessionActions actions =
      buildEditSessionActions({constrained}, edited, transactionBaseline, emptyStore, kChannel,
                              kEmptyFocus, kLoopLength);
  TEST_ASSERT_EQUAL(0, static_cast<int>(actions.size()));
}

void test_builder_emits_move_note_for_causing_start_change() {
  constexpr NoteId kMover = 20;
  const NoteBaseline edited{60, 100, 148, 248};
  const EditedGeometry geometry = makeEditedGeometry(kMover, edited);
  MidiEventVec liveStore = makeLivePair(kMover, 60, 100, 200);

  const EditSessionActions actions =
      buildEditSessionActions({}, geometry, BaselineMap{}, liveStore, kChannel, kEmptyFocus,
                              kLoopLength);
  TEST_ASSERT_EQUAL(1, static_cast<int>(actions.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(EditSessionActionType::MoveNote),
                    static_cast<int>(actions[0].type));
  TEST_ASSERT_EQUAL_UINT32(148u, actions[0].startTick);
}

void test_builder_emits_change_length_for_end_only_delta() {
  constexpr NoteId kMover = 21;
  const NoteBaseline edited{60, 100, 100, 220};
  const EditedGeometry geometry = makeEditedGeometry(kMover, edited);
  MidiEventVec liveStore = makeLivePair(kMover, 60, 100, 200);

  const EditSessionActions actions =
      buildEditSessionActions({}, geometry, BaselineMap{}, liveStore, kChannel, kEmptyFocus,
                              kLoopLength);
  TEST_ASSERT_EQUAL(1, static_cast<int>(actions.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(EditSessionActionType::ChangeLength),
                    static_cast<int>(actions[0].type));
  TEST_ASSERT_EQUAL_UINT32(220u, actions[0].endTick);
}

void test_builder_emits_change_pitch_for_pitch_delta() {
  constexpr NoteId kMover = 22;
  const NoteBaseline edited{65, 100, 100, 200};
  const EditedGeometry geometry = makeEditedGeometry(kMover, edited);
  MidiEventVec liveStore = makeLivePair(kMover, 60, 100, 200);

  const EditSessionActions actions =
      buildEditSessionActions({}, geometry, BaselineMap{}, liveStore, kChannel, kEmptyFocus,
                              kLoopLength);
  TEST_ASSERT_EQUAL(1, static_cast<int>(actions.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(EditSessionActionType::ChangePitch),
                    static_cast<int>(actions[0].type));
  TEST_ASSERT_EQUAL_UINT8(65, actions[0].pitch);
}

void test_builder_does_not_remap_hidden_overlap_target_to_mover() {
  // session_20260805_034926: hidden short target and moving long note can share pitch+start.
  // Target recovery must not resolve that live note to the selected mover and emit HideNote for it.
  constexpr NoteId kHiddenTarget = 84;
  constexpr NoteId kMover = 88;

  ConstrainedNoteGeometry hiddenTarget{};
  hiddenTarget.noteId = kHiddenTarget;
  hiddenTarget.visible = false;

  BaselineMap baseline;
  baseline[kHiddenTarget] = {30, 100, 1488, 1535};

  MidiEventVec liveStore = makeLivePair(kMover, 30, 1488, 1727);

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMover;
  focus.last = {30, 100, 1488, 1727};
  focus.commitBaseline = {30, 100, 1536, 1775};

  const NoteBaseline editedMover{30, 100, 1440, 1679};
  const EditedGeometry geometry = makeEditedGeometry(kMover, editedMover);

  const EditSessionActions actions =
      buildEditSessionActions({hiddenTarget}, geometry, baseline, liveStore, kChannel, focus,
                              kLoopLength);

  TEST_ASSERT_EQUAL(1, static_cast<int>(actions.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(EditSessionActionType::MoveNote),
                    static_cast<int>(actions[0].type));
  TEST_ASSERT_EQUAL_UINT32(kMover, actions[0].targetNoteId);
}

void test_builder_orders_restore_shorten_hide_before_causing_actions() {
  constexpr NoteId kRestore = 30;
  constexpr NoteId kShorten = 31;
  constexpr NoteId kHide = 32;
  constexpr NoteId kMover = 33;

  ConstrainedNoteGeometry restoreTarget{};
  restoreTarget.noteId = kRestore;
  restoreTarget.visible = true;
  restoreTarget.startTick = 100;
  restoreTarget.endTick = 200;
  restoreTarget.pitch = 60;

  ConstrainedNoteGeometry shortenTarget{};
  shortenTarget.noteId = kShorten;
  shortenTarget.visible = true;
  shortenTarget.startTick = 50;
  shortenTarget.endTick = 119;
  shortenTarget.pitch = 60;

  ConstrainedNoteGeometry hideTarget{};
  hideTarget.noteId = kHide;
  hideTarget.visible = false;

  BaselineMap baseline;
  baseline[kRestore] = {60, 100, 100, 200};
  baseline[kShorten] = {60, 100, 50, 200};
  baseline[kHide] = {62, 100, 300, 400};

  MidiEventVec liveStore;
  liveStore.push_back(MidiEvent::NoteOn(100, kChannel, 60, 100));
  liveStore.back().noteId = kRestore;
  liveStore.push_back(MidiEvent::NoteOff(150, kChannel, 60, 0));
  liveStore.back().noteId = kRestore;
  auto shortenPair = makeLivePair(kShorten, 60, 50, 200);
  liveStore.insert(liveStore.end(), shortenPair.begin(), shortenPair.end());
  auto hidePair = makeLivePair(kHide, 62, 300, 400);
  liveStore.insert(liveStore.end(), hidePair.begin(), hidePair.end());
  auto moverPair = makeLivePair(kMover, 60, 100, 200);
  liveStore.insert(liveStore.end(), moverPair.begin(), moverPair.end());

  const NoteBaseline editedMover{60, 100, 148, 248};
  const EditedGeometry geometry = makeEditedGeometry(kMover, editedMover);

  const EditSessionActions actions =
      buildEditSessionActions({restoreTarget, shortenTarget, hideTarget}, geometry, baseline,
                              liveStore, kChannel, kEmptyFocus, kLoopLength);
  TEST_ASSERT_EQUAL(4, static_cast<int>(actions.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(EditSessionActionType::RestoreNote),
                    static_cast<int>(actions[0].type));
  TEST_ASSERT_EQUAL(static_cast<int>(EditSessionActionType::ShortenNote),
                    static_cast<int>(actions[1].type));
  TEST_ASSERT_EQUAL(static_cast<int>(EditSessionActionType::HideNote),
                    static_cast<int>(actions[2].type));
  TEST_ASSERT_EQUAL(static_cast<int>(EditSessionActionType::MoveNote),
                    static_cast<int>(actions[3].type));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_builder_emits_restore_when_live_differs_from_baseline);
  RUN_TEST(test_read_live_linear_span_ignores_nested_same_pitch_neighbor_off);
  RUN_TEST(test_read_live_linear_span_resolves_across_store_channel);
  RUN_TEST(test_builder_emits_hide_when_constrained_not_visible);
  RUN_TEST(test_builder_omits_hide_when_live_already_absent);
  RUN_TEST(test_builder_emits_shorten_when_constrained_end_shortened);
  RUN_TEST(test_builder_reinserts_shortened_stub_when_live_absent_after_hide);
  RUN_TEST(test_builder_omits_restore_while_causing_still_completely_covers);
  RUN_TEST(test_builder_emits_move_note_for_causing_start_change);
  RUN_TEST(test_builder_emits_change_length_for_end_only_delta);
  RUN_TEST(test_builder_emits_change_pitch_for_pitch_delta);
  RUN_TEST(test_builder_does_not_remap_hidden_overlap_target_to_mover);
  RUN_TEST(test_builder_orders_restore_shorten_hide_before_causing_actions);
  return UNITY_END();
}
