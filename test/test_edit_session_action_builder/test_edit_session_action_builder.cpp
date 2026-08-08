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

void test_builder_ltr_shorten_shortened_overlap_stub_181859() {
  // session_20260807_181859: constrained shorten on overlap stub must emit ShortenNote, not RestoreNote.
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kOverlapId = 9;
  constexpr NoteId kMoverId = 26;
  constexpr uint8_t kPitch = 88;

  const NoteBaseline committed{kPitch, 100, 2152, 2543};
  const NoteBaseline stub{kPitch, 100, 2152, 2207};

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.baselineMap[kOverlapId] = committed;
  focus.baselineMap[kMoverId] = {kPitch, 100, 2016, 2111};

  NoteEditCurrentState currentState;
  currentState.upsertRow(kOverlapId, committed, stub, NoteEditPresenceType::Visible);
  currentState.upsertRow(kMoverId, focus.baselineMap[kMoverId], focus.baselineMap[kMoverId],
                         NoteEditPresenceType::Visible);

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  ConstrainedNoteGeometry constrained{};
  constrained.noteId = kOverlapId;
  constrained.visible = true;
  constrained.pitch = kPitch;
  constrained.startTick = committed.startTick;
  constrained.endTick = 2255;

  EditedGeometry edited{};
  edited.selection.primaryNote = kMoverId;
  edited.selection.selectedNotes.push_back(kMoverId);
  EditedNoteSpan causing{};
  causing.noteId = kMoverId;
  causing.span = {kPitch, 100, 2256, 2351};
  edited.causingSpans.push_back(causing);

  const EditSessionActions actions =
      buildEditSessionActions({constrained}, edited, focus.baselineMap, focus.baselineMap,
                              NoteIdList{}, store, kChannel, focus, kLoopLength, &currentState);
  TEST_ASSERT_TRUE(actionsContainTypeForNote(actions, EditSessionActionType::ShortenNote, kOverlapId));
  TEST_ASSERT_FALSE(actionsContainTypeForNote(actions, EditSessionActionType::RestoreNote, kOverlapId));
  TEST_ASSERT_EQUAL_UINT32(2255u, actions[0].endTick);
}

void test_builder_overlay_baseline_blocks_prior_mover_baseline_snap_141920() {
  // session_20260807_141920: second mover must not emit ShortenNote on prior mover at 3600–4127.
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kPriorId = 17;
  constexpr NoteId kMoverId = 10;
  constexpr uint8_t kPitch = 88;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.baselineMap[kPriorId] = {kPitch, 100, 3600, 4127};
  focus.baselineMap[kMoverId] = {kPitch, 100, 2640, 2783};

  NoteEditCurrentState currentState;
  currentState.upsertRow(kPriorId, focus.baselineMap[kPriorId], {kPitch, 100, 3264, 3791},
                         NoteEditPresenceType::Visible);
  currentState.upsertRow(kMoverId, focus.baselineMap[kMoverId], {kPitch, 100, 3552, 3695},
                         NoteEditPresenceType::Visible);

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  BaselineMap projected = focus.baselineMap;
  projected[kPriorId] = {kPitch, 100, 3264, 3791};

  ConstrainedNoteGeometry constrained{};
  constrained.noteId = kPriorId;
  constrained.visible = true;
  constrained.pitch = kPitch;
  constrained.startTick = 3600;
  constrained.endTick = 4127;

  EditedGeometry edited{};
  edited.selection.primaryNote = kMoverId;
  edited.selection.selectedNotes.push_back(kMoverId);
  EditedNoteSpan causing{};
  causing.noteId = kMoverId;
  causing.span = {kPitch, 100, 3552, 3695};
  edited.causingSpans.push_back(causing);

  const EditSessionActions actions =
      buildEditSessionActions({constrained}, edited, projected, focus.baselineMap, NoteIdList{},
                              store, kChannel, focus, kLoopLength, &currentState);
  TEST_ASSERT_FALSE(
      actionsContainTypeForNote(actions, EditSessionActionType::ShortenNote, kPriorId));
  TEST_ASSERT_FALSE(
      actionsContainTypeForNote(actions, EditSessionActionType::RestoreNote, kPriorId));
}

void test_builder_closure_active_shortened_hides_stub_on_invisible_constrained() {
  // session_20260807_202538 + session_20260808_110111: partial-cover OverlapNoteOn Hide must
  // HideNote with live stub span — not skip (no action) and not re-inflate to committed length.
  constexpr NoteId kOverlapId = 13;
  constexpr NoteId kMoverId = 9;
  constexpr uint8_t kPitch = 88;
  constexpr uint32_t kLoopLength = 5376;

  const NoteBaseline committed{kPitch, 100, 1728, 2364};
  const NoteBaseline stub{kPitch, 100, 1728, 1775};

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.baselineMap[kOverlapId] = committed;
  focus.baselineMap[kMoverId] = {kPitch, 100, 1776, 1823};

  NoteEditCurrentState currentState;
  currentState.upsertRow(kOverlapId, committed, stub, NoteEditPresenceType::Visible);
  currentState.upsertRow(kMoverId, focus.baselineMap[kMoverId], focus.baselineMap[kMoverId],
                         NoteEditPresenceType::Visible);

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  ConstrainedNoteGeometry constrained{};
  constrained.noteId = kOverlapId;
  constrained.visible = false;
  constrained.pitch = kPitch;
  constrained.startTick = committed.startTick;
  constrained.endTick = committed.endTick;

  EditedGeometry edited{};
  edited.selection.primaryNote = kMoverId;
  edited.selection.selectedNotes.push_back(kMoverId);
  EditedNoteSpan causing{};
  causing.noteId = kMoverId;
  causing.span = focus.baselineMap[kMoverId];
  edited.causingSpans.push_back(causing);

  const EditSessionActions actions =
      buildEditSessionActions({constrained}, edited, focus.baselineMap, focus.baselineMap,
                              NoteIdList{}, store, kChannel, focus, kLoopLength, &currentState);
  TEST_ASSERT_TRUE(
      actionsContainTypeForNote(actions, EditSessionActionType::HideNote, kOverlapId));
  for (const EditSessionAction& action : actions) {
    if (action.type == EditSessionActionType::HideNote && action.targetNoteId == kOverlapId) {
      TEST_ASSERT_EQUAL_UINT32(stub.endTick, action.endTick);
      TEST_ASSERT_NOT_EQUAL(committed.endTick, action.endTick);
    }
  }
}

void test_builder_partial_cover_hides_shortened_stub_110111() {
  // session_20260808_110111 @44.425: pitch onto lane 89, then overlap back over visible stub.
  constexpr NoteId kOverlapId = 9;
  constexpr NoteId kMoverId = 7;
  constexpr uint8_t kPitch = 89;
  constexpr uint32_t kLoopLength = 5376;

  const NoteBaseline committed{kPitch, 100, 1008, 1151};
  const NoteBaseline stub{kPitch, 100, 1008, 1044};
  const NoteBaseline kMoverSpan{kPitch, 100, 997, 1103};

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.baselineMap[kOverlapId] = committed;
  focus.baselineMap[kMoverId] = kMoverSpan;
  focus.last = kMoverSpan;

  NoteEditCurrentState currentState;
  currentState.upsertRow(kOverlapId, committed, stub, NoteEditPresenceType::Visible);
  currentState.upsertRow(kMoverId, kMoverSpan, kMoverSpan, NoteEditPresenceType::Visible);

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  ConstrainedNoteGeometry constrained{};
  constrained.noteId = kOverlapId;
  constrained.visible = false;
  constrained.pitch = kPitch;
  constrained.startTick = committed.startTick;
  constrained.endTick = committed.endTick;

  EditedGeometry edited{};
  edited.selection.primaryNote = kMoverId;
  edited.selection.selectedNotes.push_back(kMoverId);
  EditedNoteSpan causing{};
  causing.noteId = kMoverId;
  causing.span = kMoverSpan;
  edited.causingSpans.push_back(causing);

  const EditSessionActions actions =
      buildEditSessionActions({constrained}, edited, focus.baselineMap, focus.baselineMap,
                              NoteIdList{}, store, kChannel, focus, kLoopLength, &currentState);
  TEST_ASSERT_TRUE(
      actionsContainTypeForNote(actions, EditSessionActionType::HideNote, kOverlapId));
  for (const EditSessionAction& action : actions) {
    if (action.type == EditSessionActionType::HideNote && action.targetNoteId == kOverlapId) {
      TEST_ASSERT_EQUAL_UINT32(stub.startTick, action.startTick);
      TEST_ASSERT_EQUAL_UINT32(stub.endTick, action.endTick);
      TEST_ASSERT_EQUAL_UINT8(kPitch, action.pitch);
    }
  }
}

void test_builder_advance_with_overlap_closure_shorten_not_restore_193632() {
  // session_20260807_193632: active overlap closure → ShortenNote on hidden overlap, not RestoreNote.
  constexpr NoteId kOverlapId = 17;
  constexpr NoteId kMoverId = 9;
  constexpr uint8_t kPitch = 88;
  constexpr uint32_t kLoopLength = 5376;

  const NoteBaseline committed{kPitch, 100, 1488, 1936};
  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.baselineMap[kOverlapId] = committed;
  focus.baselineMap[kMoverId] = {kPitch, 100, 1344, 1391};

  NoteEditCurrentState currentState;
  currentState.upsertRow(kOverlapId, committed, committed, NoteEditPresenceType::Hidden);
  currentState.upsertRow(kMoverId, focus.baselineMap[kMoverId], focus.baselineMap[kMoverId],
                         NoteEditPresenceType::Visible);

  MidiEventVec store;
  MidiEvent moverOn = MidiEvent::NoteOn(1632, kChannel, kPitch, 100);
  moverOn.noteId = kMoverId;
  store.push_back(moverOn);
  MidiEvent moverOff = MidiEvent::NoteOff(1679, kChannel, kPitch, 0);
  moverOff.noteId = kMoverId;
  store.push_back(moverOff);

  ConstrainedNoteGeometry constrained{};
  constrained.noteId = kOverlapId;
  constrained.visible = true;
  constrained.pitch = kPitch;
  constrained.startTick = 1488;
  constrained.endTick = 1631;

  EditedGeometry edited{};
  edited.selection.primaryNote = kMoverId;
  edited.selection.selectedNotes.push_back(kMoverId);
  EditedNoteSpan causing{};
  causing.noteId = kMoverId;
  causing.span = {kPitch, 100, 1632, 1679};
  edited.causingSpans.push_back(causing);

  const EditSessionActions actions =
      buildEditSessionActions({constrained}, edited, focus.baselineMap, focus.baselineMap,
                              NoteIdList{}, store, kChannel, focus, kLoopLength, &currentState);
  TEST_ASSERT_TRUE(
      actionsContainTypeForNote(actions, EditSessionActionType::ShortenNote, kOverlapId));
  TEST_ASSERT_FALSE(
      actionsContainTypeForNote(actions, EditSessionActionType::RestoreNote, kOverlapId));
}

void test_builder_skips_restore_visible_overlap_tail_224633() {
  // session_20260807_224633 @24.321s: restore candidate while visible same-start tail in progress.
  constexpr NoteId kOverlapId = 9;
  constexpr NoteId kMoverId = 13;
  constexpr uint8_t kPitch = 88;
  constexpr uint32_t kLoopLength = 5376;

  const NoteBaseline kCommitted{kPitch, 100, 2544, 3078};
  const NoteBaseline kTail{kPitch, 100, 2544, 2591};

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.baselineMap[kOverlapId] = kCommitted;
  focus.baselineMap[kMoverId] = {kPitch, 100, 2592, 2639};

  NoteEditCurrentState currentState;
  currentState.upsertRow(kOverlapId, kCommitted, kTail, NoteEditPresenceType::Visible);
  currentState.upsertRow(kMoverId, focus.baselineMap[kMoverId], focus.baselineMap[kMoverId],
                         NoteEditPresenceType::Visible);

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  ConstrainedNoteGeometry constrained{};
  constrained.noteId = kOverlapId;
  constrained.visible = true;
  constrained.pitch = kPitch;
  constrained.startTick = kCommitted.startTick;
  constrained.endTick = kCommitted.endTick;

  EditedGeometry edited{};
  edited.selection.primaryNote = kMoverId;
  edited.selection.selectedNotes.push_back(kMoverId);
  EditedNoteSpan causing{};
  causing.noteId = kMoverId;
  causing.span = focus.baselineMap[kMoverId];
  edited.causingSpans.push_back(causing);

  NoteIdList leaveRestore;
  leaveRestore.push_back(kOverlapId);

  const EditSessionActions actions =
      buildEditSessionActions({constrained}, edited, focus.baselineMap, focus.baselineMap,
                              leaveRestore, store, kChannel, focus, kLoopLength, &currentState);
  TEST_ASSERT_FALSE(
      actionsContainTypeForNote(actions, EditSessionActionType::RestoreNote, kOverlapId));
}

void test_builder_emits_restore_for_pitch_vacated_hidden_020050() {
  // session_20260808_020050: leave-restore geometry for Hidden overlap on vacated pitch → RestoreNote.
  constexpr NoteId kOverlapId = 9;
  constexpr NoteId kMoverId = 7;
  constexpr uint8_t kVacatedPitch = 88;
  constexpr uint8_t kNewPitch = 89;
  constexpr uint32_t kLoopLength = 5376;

  const NoteBaseline kCommitted{kVacatedPitch, 100, 2640, 2831};
  const NoteBaseline kMoverSpan{kNewPitch, 100, 2592, 2701};

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.baselineMap[kOverlapId] = kCommitted;
  focus.baselineMap[kMoverId] = {kVacatedPitch, 100, 2592, 2701};
  focus.last = kMoverSpan;

  NoteEditCurrentState currentState;
  currentState.upsertRow(kOverlapId, kCommitted, kCommitted, NoteEditPresenceType::Hidden);
  currentState.upsertRow(kMoverId, focus.baselineMap[kMoverId], kMoverSpan,
                         NoteEditPresenceType::Visible);

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  ConstrainedNoteGeometry constrained{};
  constrained.noteId = kOverlapId;
  constrained.visible = true;
  constrained.pitch = kVacatedPitch;
  constrained.startTick = kCommitted.startTick;
  constrained.endTick = kCommitted.endTick;

  EditedGeometry edited{};
  edited.selection.primaryNote = kMoverId;
  edited.selection.selectedNotes.push_back(kMoverId);
  EditedNoteSpan causing{};
  causing.noteId = kMoverId;
  causing.span = kMoverSpan;
  edited.causingSpans.push_back(causing);

  NoteIdList leaveRestore;
  leaveRestore.push_back(kOverlapId);

  const EditSessionActions actions =
      buildEditSessionActions({constrained}, edited, focus.baselineMap, focus.baselineMap,
                              leaveRestore, store, kChannel, focus, kLoopLength, &currentState);
  TEST_ASSERT_TRUE(
      actionsContainTypeForNote(actions, EditSessionActionType::RestoreNote, kOverlapId));
}

void test_builder_emits_restore_for_pitch_vacated_shortened_021407() {
  // session_20260808_021407: Visible shortened on vacated pitch → RestoreNote to committed end.
  constexpr NoteId kOverlapId = 9;
  constexpr NoteId kMoverId = 7;
  constexpr uint8_t kVacatedPitch = 88;
  constexpr uint8_t kNewPitch = 89;
  constexpr uint32_t kLoopLength = 5376;

  const NoteBaseline kCommitted{kVacatedPitch, 100, 2640, 2831};
  const NoteBaseline kStub{kVacatedPitch, 100, 2640, 2772};
  const NoteBaseline kMoverSpan{kNewPitch, 100, 2773, 2882};

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.baselineMap[kOverlapId] = kCommitted;
  focus.baselineMap[kMoverId] = {kVacatedPitch, 100, 2773, 2882};
  focus.last = kMoverSpan;

  NoteEditCurrentState currentState;
  currentState.upsertRow(kOverlapId, kCommitted, kStub, NoteEditPresenceType::Visible);
  currentState.upsertRow(kMoverId, focus.baselineMap[kMoverId], kMoverSpan,
                         NoteEditPresenceType::Visible);

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  ConstrainedNoteGeometry constrained{};
  constrained.noteId = kOverlapId;
  constrained.visible = true;
  constrained.pitch = kVacatedPitch;
  constrained.startTick = kCommitted.startTick;
  constrained.endTick = kCommitted.endTick;

  EditedGeometry edited{};
  edited.selection.primaryNote = kMoverId;
  edited.selection.selectedNotes.push_back(kMoverId);
  EditedNoteSpan causing{};
  causing.noteId = kMoverId;
  causing.span = kMoverSpan;
  edited.causingSpans.push_back(causing);

  NoteIdList leaveRestore;
  leaveRestore.push_back(kOverlapId);

  const EditSessionActions actions =
      buildEditSessionActions({constrained}, edited, focus.baselineMap, focus.baselineMap,
                              leaveRestore, store, kChannel, focus, kLoopLength, &currentState);
  TEST_ASSERT_TRUE(
      actionsContainTypeForNote(actions, EditSessionActionType::RestoreNote, kOverlapId));
  for (const EditSessionAction& action : actions) {
    if (action.type == EditSessionActionType::RestoreNote && action.targetNoteId == kOverlapId) {
      TEST_ASSERT_EQUAL_UINT32(kCommitted.endTick, action.endTick);
      TEST_ASSERT_NOT_EQUAL(kStub.endTick, action.endTick);
    }
  }
}

void test_builder_emits_restore_for_ltr_time_axis_shortened_022151() {
  // session_20260808_022151 @124.241: LTR leave after Shorten → RestoreNote to committed end.
  constexpr NoteId kOverlapId = 9;
  constexpr NoteId kMoverId = 7;
  constexpr uint8_t kPitch = 88;
  constexpr uint32_t kLoopLength = 5376;

  const NoteBaseline kCommitted{kPitch, 100, 2640, 2934};
  const NoteBaseline kStub{kPitch, 100, 2640, 2868};
  const NoteBaseline kMoverPast{kPitch, 100, 2965, 3074};

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.baselineMap[kOverlapId] = kCommitted;
  focus.baselineMap[kMoverId] = {kPitch, 100, 2869, 2978};
  focus.last = kMoverPast;

  NoteEditCurrentState currentState;
  currentState.upsertRow(kOverlapId, kCommitted, kStub, NoteEditPresenceType::Visible);
  currentState.upsertRow(kMoverId, focus.baselineMap[kMoverId], kMoverPast,
                         NoteEditPresenceType::Visible);

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  ConstrainedNoteGeometry constrained{};
  constrained.noteId = kOverlapId;
  constrained.visible = true;
  constrained.pitch = kPitch;
  constrained.startTick = kCommitted.startTick;
  constrained.endTick = kCommitted.endTick;

  EditedGeometry edited{};
  edited.selection.primaryNote = kMoverId;
  edited.selection.selectedNotes.push_back(kMoverId);
  EditedNoteSpan causing{};
  causing.noteId = kMoverId;
  causing.span = kMoverPast;
  edited.causingSpans.push_back(causing);

  NoteIdList leaveRestore;
  leaveRestore.push_back(kOverlapId);

  const EditSessionActions actions =
      buildEditSessionActions({constrained}, edited, focus.baselineMap, focus.baselineMap,
                              leaveRestore, store, kChannel, focus, kLoopLength, &currentState);
  TEST_ASSERT_TRUE(
      actionsContainTypeForNote(actions, EditSessionActionType::RestoreNote, kOverlapId));
  for (const EditSessionAction& action : actions) {
    if (action.type == EditSessionActionType::RestoreNote && action.targetNoteId == kOverlapId) {
      TEST_ASSERT_EQUAL_UINT32(kCommitted.endTick, action.endTick);
    }
  }
}

void test_builder_complete_cover_hides_two_shortened_stubs_022849() {
  // session_20260808_022849 @70.446: elongated mover CompleteCover of two already-shortened
  // Visible stubs must emit HideNote for each (not swallow to actions=1 MoveNote only).
  constexpr NoteId kOverlapA = 11;
  constexpr NoteId kOverlapB = 12;
  constexpr NoteId kMoverId = 7;
  constexpr uint8_t kPitch = 88;
  constexpr uint32_t kLoopLength = 5376;

  const NoteBaseline kCommittedA{kPitch, 100, 1584, 1631};
  const NoteBaseline kStubA{kPitch, 100, 1584, 1620};
  const NoteBaseline kCommittedB{kPitch, 100, 1392, 1439};
  const NoteBaseline kStubB{kPitch, 100, 1392, 1428};
  const NoteBaseline kMoverCover{kPitch, 100, 1381, 1744};

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.baselineMap[kOverlapA] = kCommittedA;
  focus.baselineMap[kOverlapB] = kCommittedB;
  focus.baselineMap[kMoverId] = kMoverCover;
  focus.last = kMoverCover;

  NoteEditCurrentState currentState;
  currentState.upsertRow(kOverlapA, kCommittedA, kStubA, NoteEditPresenceType::Visible);
  currentState.upsertRow(kOverlapB, kCommittedB, kStubB, NoteEditPresenceType::Visible);
  currentState.upsertRow(kMoverId, kMoverCover, kMoverCover, NoteEditPresenceType::Visible);

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  ConstrainedNoteGeometry constrainedA{};
  constrainedA.noteId = kOverlapA;
  constrainedA.visible = false;
  constrainedA.pitch = kPitch;
  constrainedA.startTick = kCommittedA.startTick;
  constrainedA.endTick = kCommittedA.endTick;

  ConstrainedNoteGeometry constrainedB{};
  constrainedB.noteId = kOverlapB;
  constrainedB.visible = false;
  constrainedB.pitch = kPitch;
  constrainedB.startTick = kCommittedB.startTick;
  constrainedB.endTick = kCommittedB.endTick;

  EditedGeometry edited{};
  edited.selection.primaryNote = kMoverId;
  edited.selection.selectedNotes.push_back(kMoverId);
  EditedNoteSpan causing{};
  causing.noteId = kMoverId;
  causing.span = kMoverCover;
  edited.causingSpans.push_back(causing);

  const EditSessionActions actions = buildEditSessionActions(
      {constrainedA, constrainedB}, edited, focus.baselineMap, focus.baselineMap, NoteIdList{},
      store, kChannel, focus, kLoopLength, &currentState);
  TEST_ASSERT_TRUE(
      actionsContainTypeForNote(actions, EditSessionActionType::HideNote, kOverlapA));
  TEST_ASSERT_TRUE(
      actionsContainTypeForNote(actions, EditSessionActionType::HideNote, kOverlapB));
}

void test_builder_skips_reinsert_for_sealed_deleted_022849() {
  // session_20260808_022849 @92.045: after deselect sealed Delete, reselect must not ShortenNote /
  // RestoreNote reinsert of absent participant.
  constexpr NoteId kOverlapId = 11;
  constexpr NoteId kMoverId = 7;
  constexpr uint8_t kPitch = 88;
  constexpr uint32_t kLoopLength = 5376;

  const NoteBaseline kCommitted{kPitch, 100, 1584, 1631};
  const NoteBaseline kMoverSpan{kPitch, 100, 1621, 1984};

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.baselineMap[kOverlapId] = kCommitted;
  focus.baselineMap[kMoverId] = kMoverSpan;
  focus.last = kMoverSpan;

  NoteEditCurrentState currentState;
  currentState.upsertRow(kOverlapId, kCommitted, kCommitted, NoteEditPresenceType::Deleted);
  currentState.upsertRow(kMoverId, kMoverSpan, kMoverSpan, NoteEditPresenceType::Visible);

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  ConstrainedNoteGeometry constrained{};
  constrained.noteId = kOverlapId;
  constrained.visible = true;
  constrained.pitch = kPitch;
  constrained.startTick = kCommitted.startTick;
  constrained.endTick = 1620;

  EditedGeometry edited{};
  edited.selection.primaryNote = kMoverId;
  edited.selection.selectedNotes.push_back(kMoverId);
  EditedNoteSpan causing{};
  causing.noteId = kMoverId;
  causing.span = kMoverSpan;
  edited.causingSpans.push_back(causing);

  const EditSessionActions actions =
      buildEditSessionActions({constrained}, edited, focus.baselineMap, focus.baselineMap,
                              NoteIdList{}, store, kChannel, focus, kLoopLength, &currentState);
  TEST_ASSERT_FALSE(
      actionsContainTypeForNote(actions, EditSessionActionType::ShortenNote, kOverlapId));
  TEST_ASSERT_FALSE(
      actionsContainTypeForNote(actions, EditSessionActionType::RestoreNote, kOverlapId));
}

void test_builder_causing_skip_and_emit_follow_current_state_not_store() {
  constexpr NoteId kMover = 40;
  const NoteBaseline committedSpan{60, 100, 100, 200};
  const NoteBaseline currentSpan{60, 100, 148, 248};

  NoteEditCurrentState currentState;
  currentState.upsertRow(kMover, committedSpan, currentSpan, NoteEditPresenceType::Visible);

  MidiEventVec liveStore = makeLivePair(kMover, 60, 100, 200);

  const EditedGeometry matchingCurrent = makeEditedGeometry(kMover, currentSpan);
  const EditSessionActions skipActions =
      buildEditSessionActions({}, matchingCurrent, BaselineMap{}, BaselineMap{}, NoteIdList{},
                              liveStore, kChannel, kEmptyFocus, kLoopLength, &currentState);
  TEST_ASSERT_EQUAL(0, static_cast<int>(skipActions.size()));

  const NoteBaseline furtherEdited{60, 100, 160, 280};
  const EditedGeometry furtherMove = makeEditedGeometry(kMover, furtherEdited);
  const EditSessionActions moveActions =
      buildEditSessionActions({}, furtherMove, BaselineMap{}, BaselineMap{}, NoteIdList{}, liveStore,
                              kChannel, kEmptyFocus, kLoopLength, &currentState);
  TEST_ASSERT_EQUAL(1, static_cast<int>(moveActions.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(EditSessionActionType::MoveNote),
                    static_cast<int>(moveActions[0].type));
  TEST_ASSERT_EQUAL_UINT32(160u, moveActions[0].startTick);
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
  RUN_TEST(test_builder_ltr_shorten_shortened_overlap_stub_181859);
  RUN_TEST(test_builder_overlay_baseline_blocks_prior_mover_baseline_snap_141920);
  RUN_TEST(test_builder_closure_active_shortened_hides_stub_on_invisible_constrained);
  RUN_TEST(test_builder_partial_cover_hides_shortened_stub_110111);
  RUN_TEST(test_builder_advance_with_overlap_closure_shorten_not_restore_193632);
  RUN_TEST(test_builder_skips_restore_visible_overlap_tail_224633);
  RUN_TEST(test_builder_emits_restore_for_pitch_vacated_hidden_020050);
  RUN_TEST(test_builder_emits_restore_for_pitch_vacated_shortened_021407);
  RUN_TEST(test_builder_emits_restore_for_ltr_time_axis_shortened_022151);
  RUN_TEST(test_builder_complete_cover_hides_two_shortened_stubs_022849);
  RUN_TEST(test_builder_skips_reinsert_for_sealed_deleted_022849);
  RUN_TEST(test_builder_causing_skip_and_emit_follow_current_state_not_store);
  return UNITY_END();
}
