//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>
#include <cstdint>

#include "EditSessionActionBuilder.h"
#include "MidiEvent.h"
#include "NoteEditCurrentState.h"

#include "../../src/EditManager/EditSessionActionBuilder.cpp"
#include "../test_support/NoteEditFocusTestDeps.cpp"
#include "../../src/Logger.cpp"
#include "../../src/Utils/IntervalProjection.cpp"
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

bool actionsContainTypeForNote(const EditSessionActions& actions, EditSessionActionType type,
                               NoteId noteId) {
  for (const EditSessionAction& action : actions) {
    if (action.type == type && action.targetNoteId == noteId) {
      return true;
    }
  }
  return false;
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
      buildEditSessionActions({constrained}, EditedGeometry{}, transactionBaseline, transactionBaseline, NoteIdList{}, liveStore,
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
      buildEditSessionActions({constrained}, EditedGeometry{}, transactionBaseline, transactionBaseline, NoteIdList{}, liveStore,
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
      buildEditSessionActions({constrained}, EditedGeometry{}, transactionBaseline, transactionBaseline, NoteIdList{}, emptyStore,
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
      buildEditSessionActions({constrained}, EditedGeometry{}, transactionBaseline, transactionBaseline, NoteIdList{}, liveStore,
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
      buildEditSessionActions({constrained}, edited, transactionBaseline, transactionBaseline, NoteIdList{}, emptyStore, kChannel,
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
      buildEditSessionActions({constrained}, edited, transactionBaseline, transactionBaseline, NoteIdList{}, emptyStore, kChannel,
                              kEmptyFocus, kLoopLength);
  TEST_ASSERT_EQUAL(0, static_cast<int>(actions.size()));
}

void test_builder_emits_move_note_for_causing_start_change() {
  constexpr NoteId kMover = 20;
  const NoteBaseline edited{60, 100, 148, 248};
  const EditedGeometry geometry = makeEditedGeometry(kMover, edited);
  MidiEventVec liveStore = makeLivePair(kMover, 60, 100, 200);

  const EditSessionActions actions =
      buildEditSessionActions({}, geometry, BaselineMap{}, BaselineMap{}, NoteIdList{}, liveStore, kChannel, kEmptyFocus,
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
      buildEditSessionActions({}, geometry, BaselineMap{}, BaselineMap{}, NoteIdList{}, liveStore, kChannel, kEmptyFocus,
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
      buildEditSessionActions({}, geometry, BaselineMap{}, BaselineMap{}, NoteIdList{}, liveStore, kChannel, kEmptyFocus,
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
      buildEditSessionActions({hiddenTarget}, geometry, baseline, baseline, NoteIdList{}, liveStore, kChannel, focus,
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
                              baseline, NoteIdList{}, liveStore, kChannel, kEmptyFocus, kLoopLength);
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

void test_builder_emits_hide_for_overlap_note_on_constrained_geometry() {
  constexpr NoteId kOuterId = 3;
  const NoteBaseline baseline{65, 100, 609, 959};
  ConstrainedNoteGeometry constrained{};
  constrained.noteId = kOuterId;
  constrained.visible = false;
  constrained.pitch = 65;

  BaselineMap transactionBaseline;
  transactionBaseline[kOuterId] = baseline;
  MidiEventVec liveStore = makeLivePair(kOuterId, 65, 609, 959);

  const EditSessionActions actions =
      buildEditSessionActions({constrained}, EditedGeometry{}, transactionBaseline, transactionBaseline, NoteIdList{}, liveStore,
                              kChannel, kEmptyFocus, kLoopLength);
  TEST_ASSERT_EQUAL(1, static_cast<int>(actions.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(EditSessionActionType::HideNote),
                    static_cast<int>(actions[0].type));
}

void test_builder_leave_restore_emits_restore_not_move_010415() {
  // session_20260807_010415: leave-restore uses storage baselineMap, not projected/shortened span.
  constexpr NoteId kTarget = 14;
  const NoteBaseline storageBaselineSpan{94, 100, 2880, 3071};
  ConstrainedNoteGeometry constrained{};
  constrained.noteId = kTarget;
  constrained.visible = true;
  constrained.startTick = 2880;
  constrained.endTick = 3071;
  constrained.pitch = 94;

  BaselineMap storageBaseline;
  storageBaseline[kTarget] = storageBaselineSpan;
  BaselineMap projectedBaseline;
  projectedBaseline[kTarget] = {94, 100, 2881, 3071};

  MidiEventVec liveStore = makeLivePair(kTarget, 94, 2881, 3071);
  NoteIdList leaveRestore;
  leaveRestore.push_back(kTarget);

  const EditSessionActions actions =
      buildEditSessionActions({constrained}, EditedGeometry{}, projectedBaseline, storageBaseline,
                              leaveRestore, liveStore, kChannel, kEmptyFocus, 1536);
  TEST_ASSERT_EQUAL(1, static_cast<int>(actions.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(EditSessionActionType::RestoreNote),
                    static_cast<int>(actions[0].type));
  TEST_ASSERT_EQUAL_UINT32(2880u, actions[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(3071u, actions[0].endTick);
}

void test_builder_reads_current_span_for_overlap_shorten_021939() {
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kPriorId = 17;
  constexpr NoteId kMoverId = 25;
  constexpr uint8_t kPitch = 88;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.baselineMap[kPriorId] = {kPitch, 100, 3600, 4127};
  focus.baselineMap[kMoverId] = {kPitch, 100, 3504, 4031};

  NoteEditCurrentState currentState;
  currentState.upsertRow(kPriorId, focus.baselineMap[kPriorId], {kPitch, 100, 1392, 1919},
                         NoteEditPresenceType::Visible);
  currentState.upsertRow(kMoverId, focus.baselineMap[kMoverId], focus.baselineMap[kMoverId],
                         NoteEditPresenceType::Visible);

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  ConstrainedNoteGeometry constrained{};
  constrained.noteId = kPriorId;
  constrained.visible = true;
  constrained.pitch = kPitch;
  constrained.startTick = 1392;
  constrained.endTick = 1399;

  EditedGeometry edited{};
  edited.selection.primaryNote = kMoverId;
  edited.selection.selectedNotes.push_back(kMoverId);
  EditedNoteSpan causing{};
  causing.noteId = kMoverId;
  causing.span = {kPitch, 100, 1400, 2000};
  edited.causingSpans.push_back(causing);

  const EditSessionActions actions =
      buildEditSessionActions({constrained}, edited, focus.baselineMap, focus.baselineMap,
                              NoteIdList{}, store, kChannel, focus, kLoopLength, &currentState);
  TEST_ASSERT_TRUE(actionsContainTypeForNote(actions, EditSessionActionType::ShortenNote, kPriorId));
  TEST_ASSERT_FALSE(actionsContainTypeForNote(actions, EditSessionActionType::RestoreNote, kPriorId));
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
  RUN_TEST(test_builder_emits_hide_for_overlap_note_on_constrained_geometry);
  RUN_TEST(test_builder_leave_restore_emits_restore_not_move_010415);
  RUN_TEST(test_builder_reads_current_span_for_overlap_shorten_021939);
  return UNITY_END();
}
