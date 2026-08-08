//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>
#include <cstdint>

#include "ResolveConstrainedGeometry.h"
#include "EditSessionInteraction.h"
#include "MidiEvent.h"
#include "NoteEditFocus.h"
#include "NoteEditCurrentState.h"

#include "../../src/Logger.cpp"
#include "../../src/Utils/IntervalProjection.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/EditManager/EditSessionLiveStoreSpan.cpp"
#include "../../src/EditManager/NoteEditCurrentState.cpp"
#include "../../src/EditManager/EditSessionInteraction.cpp"
#include "../../src/EditManager/ResolveConstrainedGeometry.cpp"
#include "../../src/EditManager/ParticipatingNoteSession.cpp"

namespace {

EditSessionInteraction makeOverlapNoteOff(NoteId causingId, NoteId targetId, uint32_t causingStart) {
  EditSessionInteraction interaction{};
  interaction.type = InteractionType::OverlapNoteOff;
  interaction.causingNoteId = causingId;
  interaction.targetNoteId = targetId;
  interaction.baselineSpan = {60, 100, 50, 200};
  interaction.causingSpan = {60, 100, causingStart, causingStart + 100};
  return interaction;
}

EditSessionInteraction makeOverlapNoteOn(NoteId causingId, NoteId targetId) {
  EditSessionInteraction interaction{};
  interaction.type = InteractionType::OverlapNoteOn;
  interaction.causingNoteId = causingId;
  interaction.targetNoteId = targetId;
  interaction.baselineSpan = {60, 100, 150, 250};
  interaction.causingSpan = {60, 100, 100, 220};
  return interaction;
}

}  // namespace

void test_resolve_complete_hide_precedence_over_shorten() {
  const NoteBaseline baseline{60, 100, 50, 200};
  const std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>
      incoming = {makeOverlapNoteOff(20, 10, 120), makeOverlapNoteOn(30, 10)};
  const ConstrainedNoteGeometry geometry =
      resolveConstrainedGeometry(10, baseline, incoming, 1536, 12, true);
  TEST_ASSERT_FALSE(geometry.visible);
}

void test_resolve_restrictive_shorten_combine_takes_min_end() {
  const NoteBaseline baseline{60, 100, 50, 200};
  const std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>
      incoming = {makeOverlapNoteOff(20, 10, 140), makeOverlapNoteOff(30, 10, 120)};
  const ConstrainedNoteGeometry geometry =
      resolveConstrainedGeometry(10, baseline, incoming, 1536, 12, true);
  TEST_ASSERT_TRUE(geometry.visible);
  TEST_ASSERT_EQUAL_UINT32(50u, geometry.startTick);
  TEST_ASSERT_EQUAL_UINT32(119u, geometry.endTick);
}

void test_resolve_minimum_note_length_hide() {
  const NoteBaseline baseline{60, 100, 190, 200};
  const std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>
      incoming = {makeOverlapNoteOff(20, 10, 195)};
  const ConstrainedNoteGeometry geometry =
      resolveConstrainedGeometry(10, baseline, incoming, 1536, 12, true);
  TEST_ASSERT_FALSE(geometry.visible);
}

void test_resolve_baseline_equivalent_when_causing_gone() {
  const NoteBaseline baseline{60, 100, 100, 200};
  const std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>
      emptyIncoming;
  const ConstrainedNoteGeometry geometry =
      resolveConstrainedGeometry(10, baseline, emptyIncoming, 1536, 12, true);
  TEST_ASSERT_TRUE(geometry.visible);
  TEST_ASSERT_EQUAL_UINT32(100u, geometry.startTick);
  TEST_ASSERT_EQUAL_UINT32(200u, geometry.endTick);
}

void test_group_rebuild_when_one_causing_removed() {
  EditSessionInteraction fromB = makeOverlapNoteOff(20, 10, 120);
  EditSessionInteraction fromC = makeOverlapNoteOff(30, 10, 140);
  const std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>
      tickOne = {fromB, fromC};
  const EditSessionInteractionsByTarget groupedOne =
      groupEditSessionInteractionsByTarget(tickOne);
  const ConstrainedNoteGeometry both =
      resolveConstrainedGeometry(10, {60, 100, 50, 200}, groupedOne.groups[0].incoming, 1536, 12,
                                 true);
  TEST_ASSERT_EQUAL_UINT32(119u, both.endTick);

  const std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>
      tickTwo = {fromC};
  const EditSessionInteractionsByTarget groupedTwo =
      groupEditSessionInteractionsByTarget(tickTwo);
  TEST_ASSERT_EQUAL(1, static_cast<int>(groupedTwo.groups.size()));
  const ConstrainedNoteGeometry oneCausing =
      resolveConstrainedGeometry(10, {60, 100, 50, 200}, groupedTwo.groups[0].incoming, 1536, 12,
                                 true);
  TEST_ASSERT_EQUAL_UINT32(139u, oneCausing.endTick);
}

void test_determine_constrained_targets_includes_restore_candidate() {
  constexpr NoteId kTarget = 10;
  constexpr uint32_t kLoopLength = 1536;
  BaselineMap baseline;
  baseline[kTarget] = {60, 100, 100, 200};

  MidiEventVec liveStore;
  MidiEvent on = MidiEvent::NoteOn(100, 1, 60, 100);
  on.noteId = kTarget;
  liveStore.push_back(on);
  liveStore.push_back(MidiEvent::NoteOff(150, 1, 60, 0));

  EditorSelection selection{};
  selection.primaryNote = 1;
  selection.selectedNotes = {1};
  EditedGeometry edited{};
  EditedNoteSpan causing{};
  causing.noteId = 1;
  causing.span = {60, 100, 0, 100};
  edited.causingSpans.push_back(causing);

  const EditSessionInteractionsByTarget emptyGrouped;
  NoteEditFocus focus{};
  focus.active = true;
  focus.last = {60, 100, 0, 100};
  NoteEditCurrentState currentState;
  currentState.upsertRow(kTarget, {60, 100, 100, 200}, {60, 100, 100, 150},
                         NoteEditPresenceType::Visible);
  const auto targets = determineConstrainedGeometryTargetNoteIds(
      emptyGrouped, baseline, liveStore, 1, kLoopLength, selection, edited, focus, &currentState);
  TEST_ASSERT_EQUAL(1, static_cast<int>(targets.size()));
  TEST_ASSERT_EQUAL_UINT32(kTarget, targets[0]);
}

void test_determine_constrained_targets_excludes_selected_moved_causing_note() {
  // session_20260804_215203: after move, mover live span differs from select-time
  // baselineMap; restore-candidate scan must not snap the causing note back.
  constexpr NoteId kMover = 1;
  constexpr NoteId kOverlap = 2;
  constexpr uint32_t kLoopLength = 1536;

  BaselineMap baseline;
  baseline[kMover] = {64, 100, 906, 1055};
  baseline[kOverlap] = {64, 100, 906, 1001};

  MidiEventVec liveStore;
  MidiEvent moverOn = MidiEvent::NoteOn(1098, 1, 64, 100);
  moverOn.noteId = kMover;
  liveStore.push_back(moverOn);
  MidiEvent moverOff = MidiEvent::NoteOff(1247, 1, 64, 0);
  moverOff.noteId = kMover;
  liveStore.push_back(moverOff);

  EditorSelection selection{};
  selection.primaryNote = kMover;
  selection.selectedNotes = {kMover};
  EditedGeometry edited{};
  EditedNoteSpan causing{};
  causing.noteId = kMover;
  causing.span = {64, 100, 1098, 1247};
  edited.causingSpans.push_back(causing);

  const EditSessionInteractionsByTarget emptyGrouped;
  NoteEditFocus focus{};
  focus.active = true;
  focus.movingNoteId = kMover;
  focus.last = {64, 100, 1098, 1247};
  NoteEditCurrentState currentState;
  currentState.upsertRow(kOverlap, baseline[kOverlap], baseline[kOverlap],
                         NoteEditPresenceType::Hidden);
  const auto targets = determineConstrainedGeometryTargetNoteIds(
      emptyGrouped, baseline, liveStore, 1, kLoopLength, selection, edited, focus, &currentState);
  TEST_ASSERT_EQUAL(1, static_cast<int>(targets.size()));
  TEST_ASSERT_EQUAL_UINT32(kOverlap, targets[0]);
}

void test_determine_constrained_targets_excludes_unrelated_baseline_diff_153123() {
  // session_20260805_153123: lane-12 move with interactions=0 must not restore every
  // baseline-different note on the lane — only changed overlap participants.
  constexpr NoteId kMoverId = 23;
  constexpr NoteId kUnrelatedId = 31;
  constexpr uint32_t kLoopLength = 3072;
  constexpr uint8_t kChannel = 1;

  BaselineMap baseline;
  baseline[kMoverId] = {12, 100, 1632, 1823};
  baseline[kUnrelatedId] = {12, 100, 1920, 2016};

  MidiEventVec liveStore;
  MidiEvent moverOn = MidiEvent::NoteOn(1584, kChannel, 12, 100);
  moverOn.noteId = kMoverId;
  liveStore.push_back(moverOn);
  MidiEvent moverOff = MidiEvent::NoteOff(1775, kChannel, 12, 0);
  moverOff.noteId = kMoverId;
  liveStore.push_back(moverOff);
  MidiEvent unrelatedOn = MidiEvent::NoteOn(1920, kChannel, 12, 100);
  unrelatedOn.noteId = kUnrelatedId;
  liveStore.push_back(unrelatedOn);
  MidiEvent unrelatedOff = MidiEvent::NoteOff(1950, kChannel, 12, 0);
  unrelatedOff.noteId = kUnrelatedId;
  liveStore.push_back(unrelatedOff);

  EditorSelection selection{};
  selection.primaryNote = kMoverId;
  selection.selectedNotes.push_back(kMoverId);
  EditedGeometry edited{};
  edited.selection = selection;
  EditedNoteSpan causing{};
  causing.noteId = kMoverId;
  causing.span = {12, 100, 1584, 1775};
  edited.causingSpans.push_back(causing);

  const EditSessionInteractionsByTarget emptyGrouped;
  const NoteIdList noneChanged;
  NoteEditFocus focus{};
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.last = {12, 100, 1584, 1775};
  const auto targets = determineConstrainedGeometryTargetNoteIds(
      emptyGrouped, baseline, liveStore, kChannel, kLoopLength, selection, edited, focus);
  TEST_ASSERT_EQUAL(0, static_cast<int>(targets.size()));
}

void test_resolve_all_constrained_empty_without_interactions_or_changed_overlap_153123() {
  constexpr NoteId kMoverId = 23;
  constexpr NoteId kUnrelatedId = 31;
  constexpr uint32_t kLoopLength = 3072;
  constexpr uint8_t kChannel = 1;

  BaselineMap baseline;
  baseline[kMoverId] = {12, 100, 1632, 1823};
  baseline[kUnrelatedId] = {12, 100, 1920, 2016};

  MidiEventVec liveStore;
  MidiEvent moverOn = MidiEvent::NoteOn(1584, kChannel, 12, 100);
  moverOn.noteId = kMoverId;
  liveStore.push_back(moverOn);
  MidiEvent moverOff = MidiEvent::NoteOff(1775, kChannel, 12, 0);
  moverOff.noteId = kMoverId;
  liveStore.push_back(moverOff);
  MidiEvent unrelatedOn = MidiEvent::NoteOn(1920, kChannel, 12, 100);
  unrelatedOn.noteId = kUnrelatedId;
  liveStore.push_back(unrelatedOn);
  MidiEvent unrelatedOff = MidiEvent::NoteOff(1950, kChannel, 12, 0);
  unrelatedOff.noteId = kUnrelatedId;
  liveStore.push_back(unrelatedOff);

  EditorSelection selection{};
  selection.primaryNote = kMoverId;
  selection.selectedNotes.push_back(kMoverId);
  EditedGeometry edited{};
  edited.selection = selection;
  EditedNoteSpan causing{};
  causing.noteId = kMoverId;
  causing.span = {12, 100, 1584, 1775};
  edited.causingSpans.push_back(causing);

  const EditSessionInteractionsByTarget emptyGrouped;
  const NoteIdList noneChanged;
  NoteEditFocus emptyFocus{};
  NoteIdList leaveRestore;
  const auto constrained = resolveAllConstrainedGeometry(
      emptyGrouped, baseline, baseline, liveStore, kChannel, kLoopLength, 12, true, selection,
      edited, emptyFocus, leaveRestore);
  TEST_ASSERT_EQUAL(0, static_cast<int>(constrained.size()));
}

void test_resolve_start_abut_overlap_note_off_shortens_one_tick() {
  // session_20260804_225119: causingStart == baseline.end → Shorten to causingStart-1, not
  // full baseline Restore (+2 jump on 1-tick leave).
  const NoteBaseline baseline{60, 100, 0, 144};
  EditSessionInteraction startAbut{};
  startAbut.type = InteractionType::OverlapNoteOff;
  startAbut.causingNoteId = 20;
  startAbut.targetNoteId = 10;
  startAbut.baselineSpan = baseline;
  startAbut.causingSpan = {60, 100, 144, 240};
  const std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>
      incoming = {startAbut};
  const ConstrainedNoteGeometry geometry =
      resolveConstrainedGeometry(10, baseline, incoming, 1536, 12, true);
  TEST_ASSERT_TRUE(geometry.visible);
  TEST_ASSERT_EQUAL_UINT32(0u, geometry.startTick);
  TEST_ASSERT_EQUAL_UINT32(143u, geometry.endTick);
}

void test_resolve_inverted_shorten_end_hides_instead_of_emitting_inverted_span() {
  NoteBaseline baseline{60, 100, 100, 200};
  EditSessionInteraction off{};
  off.type = InteractionType::OverlapNoteOff;
  off.targetNoteId = 10;
  off.causingNoteId = 20;
  off.baselineSpan = baseline;
  // causingStart - 1 == 49, which is before baseline start 100 → inverted.
  off.causingSpan = {60, 100, 50, 150};

  const std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>
      incoming = {off};
  const ConstrainedNoteGeometry geometry =
      resolveConstrainedGeometry(10, baseline, incoming, 1536, 12, true);
  TEST_ASSERT_FALSE(geometry.visible);
}

void test_analyze_resolve_same_pitch_complete_cover_hide_omits_cross_pitch() {
  // Q14: same-pitch CompleteCover hides; cross-pitch tick overlap is omitted.
  constexpr uint32_t loopLength = 2304;
  constexpr NoteId kMoverId = 78;
  constexpr NoteId kSamePitchInner = 7;
  constexpr NoteId kCross93 = 4;
  constexpr NoteId kCross96 = 3;

  BaselineMap baseline;
  baseline[kMoverId] = {26, 100, 360, 576};
  baseline[kSamePitchInner] = {26, 100, 144, 192};
  baseline[kCross93] = {93, 100, 144, 192};
  baseline[kCross96] = {96, 100, 144, 192};

  EditorSelection selection{};
  selection.primaryNote = kMoverId;
  selection.selectedNotes.push_back(kMoverId);
  selection.selectedTick = 120;

  EditedGeometry edited{};
  edited.selection = selection;
  EditedNoteSpan causing{};
  causing.noteId = kMoverId;
  causing.span = {26, 100, 120, 336};
  edited.causingSpans.push_back(causing);

  std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> changed = {kMoverId};
  std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> targets = {
      kSamePitchInner, kCross93, kCross96};
  const auto pairs = determineEligiblePairs(selection, changed, targets);

  BaselineMap projectedBaseline;
  constexpr int32_t kOriginTick = 120;
  for (const auto& [noteId, span] : baseline) {
    projectedBaseline[noteId] =
        projectNoteBaselineForEditAnalysis(selection, span, noteId, loopLength, kOriginTick);
  }
  EditedGeometry projectedEdited = edited;
  projectedEdited.causingSpans[0].span = projectNoteBaselineForEditAnalysis(
      selection, edited.causingSpans[0].span, kMoverId, loopLength, kOriginTick);

  const auto interactions =
      analyzeEditSessionInteractions(pairs, projectedEdited, projectedBaseline);
  TEST_ASSERT_EQUAL(1, static_cast<int>(interactions.size()));
  TEST_ASSERT_EQUAL_UINT32(kSamePitchInner, interactions[0].targetNoteId);

  const EditSessionInteractionsByTarget grouped = groupEditSessionInteractionsByTarget(interactions);
  const std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>*
      incomingSame = nullptr;
  for (const TargetNoteInteractionGroup& group : grouped.groups) {
    if (group.targetNoteId == kSamePitchInner) {
      incomingSame = &group.incoming;
      break;
    }
  }
  TEST_ASSERT_NOT_NULL(incomingSame);
  const ConstrainedNoteGeometry hidden = resolveConstrainedGeometry(
      kSamePitchInner, baseline[kSamePitchInner], *incomingSame, loopLength, 12, true);
  TEST_ASSERT_FALSE(hidden.visible);
}

void test_resolve_overlap_note_on_hides_when_target_extends_past_causing_end() {
  // OpenSpec complete-hide precedence: OverlapNoteOn always hides (RC9c head-trim reverted).
  const NoteBaseline baseline{65, 100, 609, 959};
  EditSessionInteraction headOverlap{};
  headOverlap.type = InteractionType::OverlapNoteOn;
  headOverlap.causingSpan = {65, 100, 516, 617};
  headOverlap.baselineSpan = baseline;
  const std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>
      incoming = {headOverlap};
  const ConstrainedNoteGeometry geometry =
      resolveConstrainedGeometry(3, baseline, incoming, 1536, 12, true);
  TEST_ASSERT_FALSE(geometry.visible);
}

void test_resolve_overlap_note_off_inside_target_tail_shortens_or_min_length_hides() {
  // Causing inside target tail-shortens to causingStart-1; sub-min span hides.
  const NoteBaseline baseline{65, 100, 609, 959};
  EditSessionInteraction inside{};
  inside.type = InteractionType::OverlapNoteOff;
  inside.causingSpan = {65, 100, 612, 713};
  inside.baselineSpan = baseline;
  const std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>
      incoming = {inside};
  const ConstrainedNoteGeometry geometry =
      resolveConstrainedGeometry(3, baseline, incoming, 1536, 12, true);
  TEST_ASSERT_FALSE(geometry.visible);
}

void test_resolve_restore_candidate_uses_overlap_scratch_not_full_baseline() {
  constexpr NoteId kOuterId = 3;
  constexpr NoteId kMoverId = 99;
  constexpr uint8_t kChannel = 1;
  BaselineMap baseline;
  baseline[kOuterId] = {65, 100, 609, 959};

  MidiEventVec liveStore;
  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.last = {65, 100, 700, 750};
  focus.baselineMap = baseline;
  OverlapNote shortened{};
  shortened.noteId = kOuterId;
  shortened.baseline = {65, 100, 609, 659};
  shortened.state = OverlapNoteStoreState::Hidden;
  shortened.shortenedEndTick = 659;
  focus.overlapNotes[kOuterId] = shortened;
  NoteEditCurrentState currentState;
  currentState.upsertRow(kOuterId, {65, 100, 609, 659}, {65, 100, 609, 659},
                         NoteEditPresenceType::Hidden);

  const EditSessionInteractionsByTarget emptyGrouped;
  EditorSelection selection{};
  EditedGeometry edited{};
  EditedNoteSpan causing{};
  causing.noteId = kMoverId;
  causing.span = focus.last;
  edited.causingSpans.push_back(causing);
  NoteIdList leaveRestore;
  const auto constrained = resolveAllConstrainedGeometry(
      emptyGrouped, baseline, baseline, liveStore, kChannel, 1536, 12, true, selection, edited,
      focus, leaveRestore, &currentState);
  TEST_ASSERT_EQUAL(1, static_cast<int>(constrained.size()));
  TEST_ASSERT_EQUAL_UINT32(609u, constrained[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(659u, constrained[0].endTick);
}

void test_determine_constrained_targets_excludes_cross_pitch_changed_overlap() {
  // RC9g: interactions=0 leave-restore is same-pitch lane only (session_20260807_000657).
  constexpr NoteId kMover = 1;
  constexpr NoteId kCrossPitchOverlap = 2;
  constexpr uint32_t kLoopLength = 1536;

  BaselineMap baseline;
  baseline[kMover] = {94, 100, 1000, 1200};
  baseline[kCrossPitchOverlap] = {88, 100, 900, 1000};

  MidiEventVec liveStore;
  MidiEvent crossOn = MidiEvent::NoteOn(900, 1, 88, 100);
  crossOn.noteId = kCrossPitchOverlap;
  liveStore.push_back(crossOn);
  liveStore.push_back(MidiEvent::NoteOff(950, 1, 88, 0));

  EditorSelection selection{};
  selection.primaryNote = kMover;
  selection.selectedNotes = {kMover};
  EditedGeometry edited{};
  EditedNoteSpan causing{};
  causing.noteId = kMover;
  causing.span = {94, 100, 1100, 1300};
  edited.causingSpans.push_back(causing);

  NoteEditFocus focus{};
  focus.active = true;
  focus.movingNoteId = kMover;
  focus.last = {94, 100, 1100, 1300};

  const EditSessionInteractionsByTarget emptyGrouped;
  NoteIdList changed;
  changed.push_back(kCrossPitchOverlap);
  const auto targets = determineConstrainedGeometryTargetNoteIds(
      emptyGrouped, baseline, liveStore, 1, kLoopLength, selection, edited, focus);
  TEST_ASSERT_EQUAL(0, static_cast<int>(targets.size()));
}

void test_determine_targets_excludes_vacated_lane_on_pitch_change_011115() {
  // session_20260807_011115 @47.29s: pitch 88->87 must not leave-restore sticky pitch-88 overlap.
  constexpr NoteId kMover = 3;
  constexpr NoteId kOverlapOnOldLane = 10;
  constexpr uint32_t kLoopLength = 5376;

  BaselineMap baseline;
  baseline[kMover] = {88, 100, 2544, 2687};
  baseline[kOverlapOnOldLane] = {88, 100, 2640, 2783};

  MidiEventVec liveStore;
  MidiEvent overlapOn = MidiEvent::NoteOn(2688, 1, 88, 100);
  overlapOn.noteId = kOverlapOnOldLane;
  liveStore.push_back(overlapOn);
  liveStore.push_back(MidiEvent::NoteOff(2783, 1, 88, 0));

  EditorSelection selection{};
  selection.primaryNote = kMover;
  selection.selectedNotes = {kMover};
  EditedGeometry edited{};
  EditedNoteSpan causing{};
  causing.noteId = kMover;
  causing.span = {87, 100, 2544, 2687};
  edited.causingSpans.push_back(causing);

  NoteEditFocus focus{};
  focus.active = true;
  focus.movingNoteId = kMover;
  focus.last = {88, 100, 2544, 2687};
  focus.baselineMap = baseline;

  const EditSessionInteractionsByTarget emptyGrouped;
  NoteIdList changed;
  changed.push_back(kOverlapOnOldLane);
  const auto targets = determineConstrainedGeometryTargetNoteIds(
      emptyGrouped, baseline, liveStore, 1, kLoopLength, selection, edited, focus);
  TEST_ASSERT_EQUAL(0, static_cast<int>(targets.size()));
}

void test_resolve_leave_restore_head_trimmed_live_uses_storage_baseline_010415() {
  // session_20260807_010415: leave overlap must Restore full baselineMap span, not head-trimmed live.
  constexpr NoteId kTarget = 14;
  constexpr NoteId kMover = 1;
  constexpr uint8_t kChannel = 1;
  BaselineMap storageBaseline;
  storageBaseline[kMover] = {94, 100, 500, 600};
  storageBaseline[kTarget] = {94, 100, 2880, 3071};

  BaselineMap projectedBaseline = storageBaseline;
  projectedBaseline[kTarget] = {94, 100, 2881, 3071};

  MidiEventVec liveStore;
  MidiEvent on = MidiEvent::NoteOn(2881, kChannel, 94, 100);
  on.noteId = kTarget;
  liveStore.push_back(on);
  MidiEvent off = MidiEvent::NoteOff(3071, kChannel, 94, 0);
  off.noteId = kTarget;
  liveStore.push_back(off);

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMover;
  focus.last = {94, 100, 1000, 1100};
  focus.baselineMap = storageBaseline;

  NoteEditCurrentState currentState;
  currentState.upsertRow(kTarget, storageBaseline[kTarget], projectedBaseline[kTarget],
                         NoteEditPresenceType::Visible);

  const EditSessionInteractionsByTarget emptyGrouped;
  EditorSelection selection{};
  EditedGeometry edited{};
  NoteIdList leaveRestore;
  const auto constrained = resolveAllConstrainedGeometry(
      emptyGrouped, projectedBaseline, storageBaseline, liveStore, kChannel, 1536, 12, true,
      selection, edited, focus, leaveRestore, &currentState);
  TEST_ASSERT_EQUAL(1, static_cast<int>(constrained.size()));
  TEST_ASSERT_EQUAL_UINT32(2880u, constrained[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(3071u, constrained[0].endTick);
  TEST_ASSERT_EQUAL(1, static_cast<int>(leaveRestore.size()));
}

void test_determine_targets_excludes_session_moved_overlap_diff_020105() {
  // session_20260807_020105: prior mover at live 1248 with committed baseline 3600 is not leave-restore.
  constexpr NoteId kPriorMoverId = 17;
  constexpr NoteId kNewMoverId = 25;
  constexpr uint8_t kPitch = 88;
  constexpr uint8_t kChannel = 5;
  constexpr uint32_t kLoopLength = 5376;

  BaselineMap baseline;
  baseline[kPriorMoverId] = {kPitch, 100, 3600, 4127};
  baseline[kNewMoverId] = {kPitch, 100, 3504, 4031};

  MidiEventVec liveStore;
  MidiEvent priorOn = MidiEvent::NoteOn(1248, kChannel, kPitch, 100);
  priorOn.noteId = kPriorMoverId;
  liveStore.push_back(priorOn);
  MidiEvent priorOff = MidiEvent::NoteOff(1775, kChannel, kPitch, 0);
  priorOff.noteId = kPriorMoverId;
  liveStore.push_back(priorOff);

  EditorSelection selection{};
  selection.primaryNote = kNewMoverId;
  selection.selectedNotes.push_back(kNewMoverId);
  EditedGeometry edited{};
  EditedNoteSpan causing{};
  causing.noteId = kNewMoverId;
  causing.span = {kPitch, 100, 3744, 4175};
  edited.causingSpans.push_back(causing);

  NoteEditFocus focus{};
  focus.active = true;
  focus.movingNoteId = kNewMoverId;
  focus.baselineMap = baseline;

  NoteIdList changed;
  changed.push_back(kPriorMoverId);

  const EditSessionInteractionsByTarget emptyGrouped;
  const auto targets = determineConstrainedGeometryTargetNoteIds(
      emptyGrouped, baseline, liveStore, kChannel, kLoopLength, selection, edited, focus);
  TEST_ASSERT_EQUAL(0, static_cast<int>(targets.size()));
}

void test_determine_targets_excludes_session_moved_current_state_145011() {
  // session_20260807_145011: prior mover at live 1536 with committed baseline 3600 is not leave-restore
  // when NoteEditCurrentState is authoritative (RestoreNote type=0 regression).
  constexpr NoteId kPriorMoverId = 17;
  constexpr NoteId kNewMoverId = 10;
  constexpr uint8_t kPitch = 88;
  constexpr uint8_t kChannel = 5;
  constexpr uint32_t kLoopLength = 5376;

  BaselineMap baseline;
  baseline[kPriorMoverId] = {kPitch, 100, 3600, 4127};
  baseline[kNewMoverId] = {kPitch, 100, 2640, 2783};

  NoteEditCurrentState currentState;
  currentState.upsertRow(kPriorMoverId, baseline[kPriorMoverId], {kPitch, 100, 1536, 2063},
                         NoteEditPresenceType::Visible);
  currentState.upsertRow(kNewMoverId, baseline[kNewMoverId], {kPitch, 100, 2640, 2687},
                         NoteEditPresenceType::Visible);

  MidiEventVec liveStore;
  currentState.projectToSessionStore(liveStore, kChannel);

  EditorSelection selection{};
  selection.primaryNote = kNewMoverId;
  selection.selectedNotes.push_back(kNewMoverId);
  EditedGeometry edited{};
  EditedNoteSpan causing{};
  causing.noteId = kNewMoverId;
  causing.span = {kPitch, 100, 2640, 2687};
  edited.causingSpans.push_back(causing);

  NoteEditFocus focus{};
  focus.active = true;
  focus.movingNoteId = kNewMoverId;
  focus.baselineMap = baseline;

  NoteIdList changed;
  changed.push_back(kPriorMoverId);

  const EditSessionInteractionsByTarget emptyGrouped;
  const auto targets = determineConstrainedGeometryTargetNoteIds(
      emptyGrouped, baseline, liveStore, kChannel, kLoopLength, selection, edited, focus,
      &currentState);
  TEST_ASSERT_EQUAL(0, static_cast<int>(targets.size()));
}

void test_determine_targets_includes_selected_hidden_overlap_leave_restore_111955() {
  // session_20260807_111955: selected stationary sibling is leave-restore only once overlap
  // interaction is cleared (mover committed span does not overlap overlap committed span).
  constexpr NoteId kMover = 17;
  constexpr NoteId kSelectedOverlap = 7;
  constexpr uint8_t kPitch = 88;
  constexpr uint8_t kChannel = 5;
  constexpr uint32_t kLoopLength = 5376;

  BaselineMap baseline;
  baseline[kMover] = {kPitch, 100, 3600, 4127};
  baseline[kSelectedOverlap] = {kPitch, 100, 2064, 2111};

  NoteEditCurrentState currentState;
  currentState.upsertRow(kSelectedOverlap, baseline[kSelectedOverlap],
                         {kPitch, 100, 2064, 2063}, NoteEditPresenceType::Hidden);

  MidiEventVec liveStore;
  MidiEvent moverOn = MidiEvent::NoteOn(3600, kChannel, kPitch, 100);
  moverOn.noteId = kMover;
  liveStore.push_back(moverOn);
  MidiEvent moverOff = MidiEvent::NoteOff(4127, kChannel, kPitch, 0);
  moverOff.noteId = kMover;
  liveStore.push_back(moverOff);

  EditorSelection selection{};
  selection.primaryNote = kMover;
  selection.selectedNotes = {kMover, kSelectedOverlap};
  EditedGeometry edited{};
  EditedNoteSpan causing{};
  causing.noteId = kMover;
  causing.span = {kPitch, 100, 3600, 4127};
  edited.causingSpans.push_back(causing);

  NoteEditFocus focus{};
  focus.active = true;
  focus.movingNoteId = kMover;
  focus.baselineMap = baseline;

  NoteIdList changed;
  changed.push_back(kSelectedOverlap);

  const EditSessionInteractionsByTarget emptyGrouped;
  const auto targets = determineConstrainedGeometryTargetNoteIds(
      emptyGrouped, baseline, liveStore, kChannel, kLoopLength, selection, edited, focus,
      &currentState);
  TEST_ASSERT_EQUAL(1, static_cast<int>(targets.size()));
  TEST_ASSERT_EQUAL_UINT32(kSelectedOverlap, targets[0]);
}

void test_determine_targets_hidden_full_span_sticky_scope_113010() {
  // session_20260807_113010: Hide with currentSpan == baselineMap must still leave-restore (RC10a).
  // §11 step 5.4: non-participant neighbor stays Visible (matching spans) — Hidden Active is
  // participation, so a second Hidden row would also leave-restore.
  constexpr NoteId kMover = 17;
  constexpr NoteId kHiddenOverlap = 10;
  constexpr NoteId kNonParticipantNeighbor = 11;
  constexpr uint8_t kPitch = 88;
  constexpr uint8_t kChannel = 5;
  constexpr uint32_t kLoopLength = 5376;

  const NoteBaseline kOverlapBaseline{kPitch, 100, 2640, 2735};

  BaselineMap baseline;
  baseline[kMover] = {kPitch, 100, 3600, 4127};
  baseline[kHiddenOverlap] = kOverlapBaseline;
  baseline[kNonParticipantNeighbor] = {kPitch, 100, 2256, 2303};

  NoteEditCurrentState currentState;
  currentState.upsertRow(kHiddenOverlap, kOverlapBaseline, kOverlapBaseline,
                         NoteEditPresenceType::Hidden);
  currentState.upsertRow(kNonParticipantNeighbor, baseline[kNonParticipantNeighbor],
                         baseline[kNonParticipantNeighbor], NoteEditPresenceType::Visible);

  MidiEventVec liveStore;
  MidiEvent moverOn = MidiEvent::NoteOn(1680, kChannel, kPitch, 100);
  moverOn.noteId = kMover;
  liveStore.push_back(moverOn);
  MidiEvent moverOff = MidiEvent::NoteOff(2207, kChannel, kPitch, 0);
  moverOff.noteId = kMover;
  liveStore.push_back(moverOff);

  EditorSelection selection{};
  selection.primaryNote = kMover;
  selection.selectedNotes.push_back(kMover);
  EditedGeometry edited{};
  EditedNoteSpan causing{};
  causing.noteId = kMover;
  causing.span = {kPitch, 100, 1680, 2207};
  edited.causingSpans.push_back(causing);

  NoteEditFocus focus{};
  focus.active = true;
  focus.movingNoteId = kMover;
  focus.baselineMap = baseline;

  NoteIdList changed;
  changed.push_back(kHiddenOverlap);

  const EditSessionInteractionsByTarget emptyGrouped;
  const auto targets = determineConstrainedGeometryTargetNoteIds(
      emptyGrouped, baseline, liveStore, kChannel, kLoopLength, selection, edited, focus,
      &currentState);
  TEST_ASSERT_EQUAL(1, static_cast<int>(targets.size()));
  TEST_ASSERT_EQUAL_UINT32(kHiddenOverlap, targets[0]);
}

void test_leave_restore_constrained_uses_session_baseline_hidden_full_span_113010() {
  // resolveAllConstrainedGeometry: hidden row with currentSpan == baselineMap → restore geometry.
  constexpr NoteId kMover = 17;
  constexpr NoteId kHiddenOverlap = 10;
  constexpr uint8_t kPitch = 88;
  constexpr uint8_t kChannel = 5;
  constexpr uint32_t kLoopLength = 5376;

  const NoteBaseline kOverlapBaseline{kPitch, 100, 2640, 2735};

  NoteEditFocus focus{};
  focus.active = true;
  focus.movingNoteId = kMover;
  focus.baselineMap[kMover] = {kPitch, 100, 3600, 4127};
  focus.baselineMap[kHiddenOverlap] = kOverlapBaseline;

  NoteEditCurrentState currentState;
  currentState.upsertRow(kHiddenOverlap, kOverlapBaseline, kOverlapBaseline,
                         NoteEditPresenceType::Hidden);
  currentState.upsertRow(kMover, focus.baselineMap[kMover], focus.baselineMap[kMover],
                         NoteEditPresenceType::Visible);
  MidiEventVec liveStore;
  currentState.projectToSessionStore(liveStore, kChannel);

  EditorSelection selection{};
  selection.primaryNote = kMover;
  selection.selectedNotes.push_back(kMover);
  EditedGeometry edited{};
  EditedNoteSpan causing{};
  causing.noteId = kMover;
  causing.span = {kPitch, 100, 1680, 2207};
  edited.causingSpans.push_back(causing);

  NoteIdList changed;
  changed.push_back(kHiddenOverlap);

  const EditSessionInteractionsByTarget emptyGrouped;
  NoteIdList leaveRestore;
  const auto constrained = resolveAllConstrainedGeometry(
      emptyGrouped, focus.baselineMap, focus.baselineMap, liveStore, kChannel, kLoopLength, 12, true,
      selection, edited, focus, leaveRestore, &currentState);
  TEST_ASSERT_EQUAL(1, static_cast<int>(constrained.size()));
  TEST_ASSERT_EQUAL(1, static_cast<int>(leaveRestore.size()));
  TEST_ASSERT_EQUAL_UINT32(kHiddenOverlap, constrained[0].noteId);
  TEST_ASSERT_TRUE(constrained[0].visible);
  TEST_ASSERT_EQUAL_UINT32(kOverlapBaseline.startTick, constrained[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(kOverlapBaseline.endTick, constrained[0].endTick);
}

void test_leave_restore_hidden_live_stub_uses_committed_span_175858() {
  // session_20260807_175858: hidden overlap with live shortened stub must leave-restore committed span.
  constexpr NoteId kMover = 3;
  constexpr NoteId kHiddenOverlap = 17;
  constexpr uint8_t kPitch = 88;
  constexpr uint8_t kChannel = 5;
  constexpr uint32_t kLoopLength = 5376;

  const NoteBaseline kCommitted{kPitch, 100, 3216, 3743};

  NoteEditFocus focus{};
  focus.active = true;
  focus.movingNoteId = kMover;
  focus.baselineMap[kMover] = {kPitch, 100, 1488, 1535};
  focus.baselineMap[kHiddenOverlap] = kCommitted;

  NoteEditCurrentState currentState;
  currentState.upsertRow(kHiddenOverlap, kCommitted, {kPitch, 100, 3216, 3263},
                         NoteEditPresenceType::Hidden);
  currentState.upsertRow(kMover, focus.baselineMap[kMover], focus.baselineMap[kMover],
                         NoteEditPresenceType::Visible);

  MidiEventVec liveStore;
  MidiEvent overlapOn = MidiEvent::NoteOn(1488, kChannel, kPitch, 100);
  overlapOn.noteId = kHiddenOverlap;
  liveStore.push_back(overlapOn);
  MidiEvent overlapOff = MidiEvent::NoteOff(1535, kChannel, kPitch, 0);
  overlapOff.noteId = kHiddenOverlap;
  liveStore.push_back(overlapOff);
  MidiEvent moverOn = MidiEvent::NoteOn(1488, kChannel, kPitch, 100);
  moverOn.noteId = kMover;
  liveStore.push_back(moverOn);
  MidiEvent moverOff = MidiEvent::NoteOff(1535, kChannel, kPitch, 0);
  moverOff.noteId = kMover;
  liveStore.push_back(moverOff);

  EditorSelection selection{};
  selection.primaryNote = kMover;
  selection.selectedNotes.push_back(kMover);
  EditedGeometry edited{};
  EditedNoteSpan causing{};
  causing.noteId = kMover;
  causing.span = {kPitch, 100, 1440, 1487};
  edited.causingSpans.push_back(causing);

  NoteIdList changed;
  changed.push_back(kHiddenOverlap);

  const EditSessionInteractionsByTarget emptyGrouped;
  NoteIdList leaveRestore;
  const auto constrained = resolveAllConstrainedGeometry(
      emptyGrouped, focus.baselineMap, focus.baselineMap, liveStore, kChannel, kLoopLength, 12,
      true, selection, edited, focus, leaveRestore, &currentState);
  TEST_ASSERT_EQUAL(1, static_cast<int>(constrained.size()));
  TEST_ASSERT_EQUAL_UINT32(kCommitted.startTick, constrained[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(kCommitted.endTick, constrained[0].endTick);
}

void test_closure_active_suppresses_leave_restore_target_193632() {
  // session_20260807_193632 ~19.5s: mover inside overlap closure must not leave-restore hidden overlap.
  constexpr NoteId kMover = 9;
  constexpr NoteId kHiddenOverlap = 17;
  constexpr uint8_t kPitch = 88;
  constexpr uint8_t kChannel = 5;
  constexpr uint32_t kLoopLength = 5376;

  const NoteBaseline kOverlapCommitted{kPitch, 100, 1488, 1936};

  BaselineMap baseline;
  baseline[kMover] = {kPitch, 100, 1344, 1391};
  baseline[kHiddenOverlap] = kOverlapCommitted;

  NoteEditCurrentState currentState;
  currentState.upsertRow(kHiddenOverlap, kOverlapCommitted, kOverlapCommitted,
                         NoteEditPresenceType::Hidden);
  currentState.upsertRow(kMover, baseline[kMover], baseline[kMover], NoteEditPresenceType::Visible);

  MidiEventVec liveStore;
  currentState.projectToSessionStore(liveStore, kChannel);

  EditorSelection selection{};
  selection.primaryNote = kMover;
  selection.selectedNotes.push_back(kMover);
  EditedGeometry edited{};
  EditedNoteSpan causing{};
  causing.noteId = kMover;
  causing.span = {kPitch, 100, 1536, 1583};
  edited.causingSpans.push_back(causing);

  NoteEditFocus focus{};
  focus.active = true;
  focus.movingNoteId = kMover;
  focus.baselineMap = baseline;

  NoteIdList changed;
  changed.push_back(kHiddenOverlap);

  const EditSessionInteractionsByTarget emptyGrouped;
  const auto targets = determineConstrainedGeometryTargetNoteIds(
      emptyGrouped, baseline, liveStore, kChannel, kLoopLength, selection, edited, focus,
      &currentState);
  TEST_ASSERT_EQUAL(0, static_cast<int>(targets.size()));
}

void test_leave_restore_deferred_visible_overlap_tail_224633() {
  // session_20260807_224633: while mover still inside committed overlap closure and visible
  // same-start tail is growing — must not leave-restore to full committed span.
  constexpr NoteId kMover = 13;
  constexpr NoteId kOverlap = 9;
  constexpr uint8_t kPitch = 88;
  constexpr uint8_t kChannel = 5;
  constexpr uint32_t kLoopLength = 5376;

  const NoteBaseline kCommitted{kPitch, 100, 2544, 3078};
  const NoteBaseline kTail{kPitch, 100, 2544, 2591};

  BaselineMap baseline;
  baseline[kMover] = {kPitch, 100, 2592, 2639};
  baseline[kOverlap] = kCommitted;

  NoteEditCurrentState currentState;
  currentState.upsertRow(kOverlap, kCommitted, kTail, NoteEditPresenceType::Visible);
  currentState.upsertRow(kMover, baseline[kMover], baseline[kMover], NoteEditPresenceType::Visible);

  MidiEventVec liveStore;
  currentState.projectToSessionStore(liveStore, kChannel);

  EditorSelection selection{};
  selection.primaryNote = kMover;
  selection.selectedNotes.push_back(kMover);
  EditedGeometry edited{};
  EditedNoteSpan causing{};
  causing.noteId = kMover;
  causing.span = baseline[kMover];
  edited.causingSpans.push_back(causing);

  NoteEditFocus focus{};
  focus.active = true;
  focus.movingNoteId = kMover;
  focus.baselineMap = baseline;

  NoteIdList changed;
  changed.push_back(kOverlap);

  const EditSessionInteractionsByTarget emptyGrouped;
  const auto targets = determineConstrainedGeometryTargetNoteIds(
      emptyGrouped, baseline, liveStore, kChannel, kLoopLength, selection, edited, focus,
      &currentState);
  TEST_ASSERT_EQUAL(0, static_cast<int>(targets.size()));
}

void test_sealed_visible_shortened_leave_restore_after_mover_clears_234904() {
  // session_20260807_234904 ~65s: second overlap pass on macro-sealed note 9 — mover past overlap
  // must restore current stub to sealed committedSpan (2063), not pre-shorten storage baseline.
  constexpr NoteId kMover = 13;
  constexpr NoteId kOverlap = 9;
  constexpr uint8_t kPitch = 88;
  constexpr uint8_t kChannel = 5;
  constexpr uint32_t kLoopLength = 5376;

  const NoteBaseline kStorage{kPitch, 100, 1776, 3078};
  const NoteBaseline kSealed{kPitch, 100, 1776, 2063};
  const NoteBaseline kTail{kPitch, 100, 1776, 1823};

  BaselineMap baseline;
  baseline[kMover] = {kPitch, 100, 2112, 2159};
  baseline[kOverlap] = kStorage;

  NoteEditCurrentState currentState;
  currentState.upsertRow(kOverlap, kSealed, kTail, NoteEditPresenceType::Visible);
  currentState.upsertRow(kMover, baseline[kMover], baseline[kMover], NoteEditPresenceType::Visible);
  NoteEditCurrentNoteState* overlapRow = currentState.find(kOverlap);
  TEST_ASSERT_NOT_NULL(overlapRow);
  overlapRow->visibleOverlapShortenSealed = true;

  MidiEventVec liveStore;
  currentState.projectToSessionStore(liveStore, kChannel);

  EditorSelection selection{};
  selection.primaryNote = kMover;
  selection.selectedNotes.push_back(kMover);
  EditedGeometry edited{};
  EditedNoteSpan causing{};
  causing.noteId = kMover;
  causing.span = {kPitch, 100, 1344, 1391};
  edited.causingSpans.push_back(causing);

  NoteEditFocus focus{};
  focus.active = true;
  focus.movingNoteId = kMover;
  focus.baselineMap = baseline;

  NoteIdList changed;
  changed.push_back(kOverlap);

  const EditSessionInteractionsByTarget emptyGrouped;
  const auto targets = determineConstrainedGeometryTargetNoteIds(
      emptyGrouped, baseline, liveStore, kChannel, kLoopLength, selection, edited, focus,
      &currentState);
  TEST_ASSERT_EQUAL(1, static_cast<int>(targets.size()));
  TEST_ASSERT_EQUAL_UINT32(kOverlap, targets[0]);

  NoteIdList leaveRestore;
  const auto constrained = resolveAllConstrainedGeometry(
      emptyGrouped, baseline, baseline, liveStore, kChannel, kLoopLength, 12, true, selection,
      edited, focus, leaveRestore, &currentState);
  TEST_ASSERT_EQUAL(1, static_cast<int>(constrained.size()));
  TEST_ASSERT_EQUAL_UINT32(kSealed.endTick, constrained[0].endTick);
}

void test_sealed_visible_shortened_leave_restore_rtl_after_mover_exits_left_235724() {
  // session_20260807_235724 ~114s: macro-sealed note 9 (2016–2255) re-overlapped; mover 11 exits
  // left (1968–2015) — must restore stub to committed even when committed == storage baseline.
  constexpr NoteId kMover = 11;
  constexpr NoteId kOverlap = 9;
  constexpr uint8_t kPitch = 88;
  constexpr uint8_t kChannel = 5;
  constexpr uint32_t kLoopLength = 5376;

  const NoteBaseline kSealed{kPitch, 100, 2016, 2255};
  const NoteBaseline kTail{kPitch, 100, 2016, 2063};

  BaselineMap baseline;
  baseline[kMover] = {kPitch, 100, 1968, 2015};
  baseline[kOverlap] = kSealed;

  NoteEditCurrentState currentState;
  currentState.upsertRow(kOverlap, kSealed, kTail, NoteEditPresenceType::Visible);
  currentState.upsertRow(kMover, baseline[kMover], baseline[kMover], NoteEditPresenceType::Visible);
  NoteEditCurrentNoteState* overlapRow = currentState.find(kOverlap);
  TEST_ASSERT_NOT_NULL(overlapRow);
  overlapRow->visibleOverlapShortenSealed = true;

  MidiEventVec liveStore;
  currentState.projectToSessionStore(liveStore, kChannel);

  EditorSelection selection{};
  selection.primaryNote = kMover;
  selection.selectedNotes.push_back(kMover);
  EditedGeometry edited{};
  EditedNoteSpan causing{};
  causing.noteId = kMover;
  causing.span = {kPitch, 100, 1968, 2015};
  edited.causingSpans.push_back(causing);

  NoteEditFocus focus{};
  focus.active = true;
  focus.movingNoteId = kMover;
  focus.baselineMap = baseline;

  NoteIdList changed;
  changed.push_back(kOverlap);

  const EditSessionInteractionsByTarget emptyGrouped;
  const auto targets = determineConstrainedGeometryTargetNoteIds(
      emptyGrouped, baseline, liveStore, kChannel, kLoopLength, selection, edited, focus,
      &currentState);
  TEST_ASSERT_EQUAL(1, static_cast<int>(targets.size()));
  TEST_ASSERT_EQUAL_UINT32(kOverlap, targets[0]);

  NoteIdList leaveRestore;
  const auto constrained = resolveAllConstrainedGeometry(
      emptyGrouped, baseline, baseline, liveStore, kChannel, kLoopLength, 12, true, selection,
      edited, focus, leaveRestore, &currentState);
  TEST_ASSERT_EQUAL(1, static_cast<int>(constrained.size()));
  TEST_ASSERT_EQUAL_UINT32(kSealed.endTick, constrained[0].endTick);
}

void test_leave_restore_after_repositioned_macro_sealed_overlap_001226() {
  // session_20260808_001226 ~41.5s: note 9 macro-sealed, moved to 1680, second overlap exit must not
  // RestoreNote to frozen storage baseline 2640–3174.
  constexpr NoteId kMover = 11;
  constexpr NoteId kOverlap = 9;
  constexpr uint8_t kPitch = 88;
  constexpr uint8_t kChannel = 5;
  constexpr uint32_t kLoopLength = 5376;

  const NoteBaseline kStorage{kPitch, 100, 2640, 3174};
  const NoteBaseline kSealed{kPitch, 100, 1680, 2015};
  const NoteBaseline kTail{kPitch, 100, 1680, 1727};

  BaselineMap storageBaseline;
  storageBaseline[kMover] = {kPitch, 100, 1632, 1679};
  storageBaseline[kOverlap] = kStorage;

  BaselineMap analysisBaseline = storageBaseline;
  analysisBaseline[kOverlap] = kSealed;

  NoteEditCurrentState currentState;
  currentState.upsertRow(kOverlap, kSealed, kTail, NoteEditPresenceType::Visible);
  currentState.upsertRow(kMover, analysisBaseline[kMover], analysisBaseline[kMover],
                         NoteEditPresenceType::Visible);
  NoteEditCurrentNoteState* overlapRow = currentState.find(kOverlap);
  TEST_ASSERT_NOT_NULL(overlapRow);
  overlapRow->visibleOverlapShortenSealed = true;

  MidiEventVec liveStore;
  currentState.projectToSessionStore(liveStore, kChannel);

  EditorSelection selection{};
  selection.primaryNote = kMover;
  selection.selectedNotes.push_back(kMover);
  EditedGeometry edited{};
  EditedNoteSpan causing{};
  causing.noteId = kMover;
  causing.span = {kPitch, 100, 1632, 1679};
  edited.causingSpans.push_back(causing);

  NoteEditFocus focus{};
  focus.active = true;
  focus.movingNoteId = kMover;
  focus.baselineMap = storageBaseline;

  NoteIdList changed;
  changed.push_back(kOverlap);

  const EditSessionInteractionsByTarget emptyGrouped;
  NoteIdList leaveRestore;
  const auto constrained = resolveAllConstrainedGeometry(
      emptyGrouped, analysisBaseline, storageBaseline, liveStore, kChannel, kLoopLength, 12, true,
      selection, edited, focus, leaveRestore, &currentState);
  TEST_ASSERT_EQUAL(1, static_cast<int>(constrained.size()));
  TEST_ASSERT_EQUAL_UINT32(kSealed.startTick, constrained[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(kSealed.endTick, constrained[0].endTick);
  TEST_ASSERT_NOT_EQUAL_UINT32(kStorage.startTick, constrained[0].startTick);
}

void test_leave_restore_after_overlap_closure_cleared_224633() {
  // session_20260807_224633 @24.321s geometry: mover past committed end with Visible shortened
  // stub — leave-restore to committedSpan (Stage 7.5.D time-axis; was seal-owned, flipped by 022151).
  constexpr NoteId kMover = 13;
  constexpr NoteId kOverlap = 9;
  constexpr uint8_t kPitch = 88;
  constexpr uint8_t kChannel = 5;
  constexpr uint32_t kLoopLength = 5376;

  const NoteBaseline kCommitted{kPitch, 100, 2544, 3078};
  const NoteBaseline kTail{kPitch, 100, 2544, 3071};

  BaselineMap baseline;
  baseline[kMover] = {kPitch, 100, 3168, 3215};
  baseline[kOverlap] = kCommitted;

  NoteEditCurrentState currentState;
  currentState.upsertRow(kOverlap, kCommitted, kTail, NoteEditPresenceType::Visible);
  currentState.upsertRow(kMover, baseline[kMover], baseline[kMover], NoteEditPresenceType::Visible);

  MidiEventVec liveStore;
  currentState.projectToSessionStore(liveStore, kChannel);

  EditorSelection selection{};
  selection.primaryNote = kMover;
  selection.selectedNotes.push_back(kMover);
  EditedGeometry edited{};
  EditedNoteSpan causing{};
  causing.noteId = kMover;
  causing.span = baseline[kMover];
  edited.causingSpans.push_back(causing);

  NoteEditFocus focus{};
  focus.active = true;
  focus.movingNoteId = kMover;
  focus.baselineMap = baseline;

  NoteIdList changed;
  changed.push_back(kOverlap);

  const EditSessionInteractionsByTarget emptyGrouped;
  NoteIdList leaveRestore;
  const auto constrained = resolveAllConstrainedGeometry(
      emptyGrouped, baseline, baseline, liveStore, kChannel, kLoopLength, 12, true, selection,
      edited, focus, leaveRestore, &currentState);
  TEST_ASSERT_EQUAL(1, static_cast<int>(leaveRestore.size()));
  TEST_ASSERT_EQUAL_UINT32(kOverlap, leaveRestore[0]);
  TEST_ASSERT_EQUAL(1, static_cast<int>(constrained.size()));
  TEST_ASSERT_EQUAL_UINT32(kCommitted.endTick, constrained[0].endTick);
}

void test_ltr_time_axis_visible_shortened_leave_restore_022151() {
  // session_20260808_022151 @123.928→124.241: LTR ShortenNote then MoveNote past overlap —
  // must RestoreNote to committedSpan (not leave stub at 2868).
  constexpr NoteId kMover = 7;
  constexpr NoteId kOverlap = 9;
  constexpr uint8_t kPitch = 88;
  constexpr uint8_t kChannel = 5;
  constexpr uint32_t kLoopLength = 5376;

  const NoteBaseline kCommitted{kPitch, 100, 2640, 2934};
  const NoteBaseline kStub{kPitch, 100, 2640, 2868};
  const NoteBaseline kMoverPast{kPitch, 100, 2965, 3074};

  BaselineMap baseline;
  baseline[kMover] = {kPitch, 100, 2869, 2978};
  baseline[kOverlap] = kCommitted;

  NoteEditCurrentState currentState;
  currentState.upsertRow(kOverlap, kCommitted, kStub, NoteEditPresenceType::Visible);
  currentState.upsertRow(kMover, baseline[kMover], kMoverPast, NoteEditPresenceType::Visible);

  MidiEventVec liveStore;
  currentState.projectToSessionStore(liveStore, kChannel);

  EditorSelection selection{};
  selection.primaryNote = kMover;
  selection.selectedNotes.push_back(kMover);
  EditedGeometry edited{};
  EditedNoteSpan causing{};
  causing.noteId = kMover;
  causing.span = kMoverPast;
  edited.causingSpans.push_back(causing);

  NoteEditFocus focus{};
  focus.active = true;
  focus.movingNoteId = kMover;
  focus.last = kMoverPast;
  focus.baselineMap = baseline;

  NoteIdList changed;
  changed.push_back(kOverlap);

  const EditSessionInteractionsByTarget emptyGrouped;
  NoteIdList leaveRestore;
  const auto constrained = resolveAllConstrainedGeometry(
      emptyGrouped, baseline, baseline, liveStore, kChannel, kLoopLength, 12, true, selection,
      edited, focus, leaveRestore, &currentState);
  TEST_ASSERT_EQUAL(1, static_cast<int>(leaveRestore.size()));
  TEST_ASSERT_EQUAL_UINT32(kOverlap, leaveRestore[0]);
  TEST_ASSERT_EQUAL(1, static_cast<int>(constrained.size()));
  TEST_ASSERT_TRUE(constrained[0].visible);
  TEST_ASSERT_EQUAL_UINT32(kCommitted.endTick, constrained[0].endTick);
  TEST_ASSERT_NOT_EQUAL(kStub.endTick, constrained[0].endTick);
}

void test_pitch_vacated_lane_hidden_leave_restore_target_020050() {
  // session_20260808_020050 @75.758→76.380: HideNote on lane 88 then ChangePitch to 89 must
  // leave-restore the Hidden overlap on the vacated lane.
  constexpr NoteId kMover = 7;
  constexpr NoteId kOverlap = 9;
  constexpr uint8_t kVacatedPitch = 88;
  constexpr uint8_t kNewPitch = 89;
  constexpr uint8_t kChannel = 5;
  constexpr uint32_t kLoopLength = 5376;

  const NoteBaseline kOverlapCommitted{kVacatedPitch, 100, 2640, 2831};
  const NoteBaseline kMoverOnNewLane{kNewPitch, 100, 2592, 2701};

  BaselineMap baseline;
  baseline[kMover] = {kVacatedPitch, 100, 2592, 2701};
  baseline[kOverlap] = kOverlapCommitted;

  NoteEditCurrentState currentState;
  currentState.upsertRow(kOverlap, kOverlapCommitted, kOverlapCommitted,
                         NoteEditPresenceType::Hidden);
  currentState.upsertRow(kMover, baseline[kMover], kMoverOnNewLane, NoteEditPresenceType::Visible);

  MidiEventVec liveStore;
  currentState.projectToSessionStore(liveStore, kChannel);

  EditorSelection selection{};
  selection.primaryNote = kMover;
  selection.selectedNotes.push_back(kMover);
  EditedGeometry edited{};
  EditedNoteSpan causing{};
  causing.noteId = kMover;
  causing.span = kMoverOnNewLane;
  edited.causingSpans.push_back(causing);

  NoteEditFocus focus{};
  focus.active = true;
  focus.movingNoteId = kMover;
  focus.last = kMoverOnNewLane;
  focus.commitBaseline = {kVacatedPitch, 100, 2592, 2701};
  focus.baselineMap = baseline;

  NoteIdList changed;
  changed.push_back(kOverlap);

  const EditSessionInteractionsByTarget emptyGrouped;
  NoteIdList leaveRestore;
  const auto constrained = resolveAllConstrainedGeometry(
      emptyGrouped, baseline, baseline, liveStore, kChannel, kLoopLength, 12, true, selection,
      edited, focus, leaveRestore, &currentState);
  TEST_ASSERT_EQUAL(1, static_cast<int>(leaveRestore.size()));
  TEST_ASSERT_EQUAL_UINT32(kOverlap, leaveRestore[0]);
  TEST_ASSERT_EQUAL(1, static_cast<int>(constrained.size()));
  TEST_ASSERT_TRUE(constrained[0].visible);
  TEST_ASSERT_EQUAL_UINT32(kOverlapCommitted.startTick, constrained[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(kOverlapCommitted.endTick, constrained[0].endTick);
  TEST_ASSERT_EQUAL_UINT8(kVacatedPitch, constrained[0].pitch);
}

void test_pitch_vacated_lane_visible_shortened_leave_restore_021407() {
  // session_20260808_021407 @70.130→70.905: ShortenNote on lane 88 then ChangePitch to 89 must
  // leave-restore overlap length to committedSpan.
  constexpr NoteId kMover = 7;
  constexpr NoteId kOverlap = 9;
  constexpr uint8_t kVacatedPitch = 88;
  constexpr uint8_t kNewPitch = 89;
  constexpr uint8_t kChannel = 5;
  constexpr uint32_t kLoopLength = 5376;

  const NoteBaseline kCommitted{kVacatedPitch, 100, 2640, 2831};
  const NoteBaseline kStub{kVacatedPitch, 100, 2640, 2772};
  const NoteBaseline kMoverOnNewLane{kNewPitch, 100, 2773, 2882};

  BaselineMap baseline;
  baseline[kMover] = {kVacatedPitch, 100, 2773, 2882};
  baseline[kOverlap] = kCommitted;

  NoteEditCurrentState currentState;
  currentState.upsertRow(kOverlap, kCommitted, kStub, NoteEditPresenceType::Visible);
  currentState.upsertRow(kMover, baseline[kMover], kMoverOnNewLane, NoteEditPresenceType::Visible);

  MidiEventVec liveStore;
  currentState.projectToSessionStore(liveStore, kChannel);

  EditorSelection selection{};
  selection.primaryNote = kMover;
  selection.selectedNotes.push_back(kMover);
  EditedGeometry edited{};
  EditedNoteSpan causing{};
  causing.noteId = kMover;
  causing.span = kMoverOnNewLane;
  edited.causingSpans.push_back(causing);

  NoteEditFocus focus{};
  focus.active = true;
  focus.movingNoteId = kMover;
  focus.last = kMoverOnNewLane;
  focus.baselineMap = baseline;

  NoteIdList changed;
  changed.push_back(kOverlap);

  const EditSessionInteractionsByTarget emptyGrouped;
  NoteIdList leaveRestore;
  const auto constrained = resolveAllConstrainedGeometry(
      emptyGrouped, baseline, baseline, liveStore, kChannel, kLoopLength, 12, true, selection,
      edited, focus, leaveRestore, &currentState);
  TEST_ASSERT_EQUAL(1, static_cast<int>(leaveRestore.size()));
  TEST_ASSERT_EQUAL_UINT32(kOverlap, leaveRestore[0]);
  TEST_ASSERT_EQUAL(1, static_cast<int>(constrained.size()));
  TEST_ASSERT_TRUE(constrained[0].visible);
  TEST_ASSERT_EQUAL_UINT32(kCommitted.endTick, constrained[0].endTick);
  TEST_ASSERT_NOT_EQUAL(kStub.endTick, constrained[0].endTick);
}

void test_determine_targets_excludes_ended_participation_even_with_stale_latch() {
  // §11 step 5.4: Ended rows are not leave-restore targets even if Focus latch still lists them.
  constexpr NoteId kMover = 7;
  constexpr NoteId kOverlap = 9;
  constexpr uint8_t kPitch = 88;
  constexpr uint8_t kChannel = 5;
  constexpr uint32_t kLoopLength = 5376;

  const NoteBaseline kCommitted{kPitch, 100, 2640, 2831};
  const NoteBaseline kStub{kPitch, 100, 2640, 2772};
  const NoteBaseline kMoverPast{kPitch, 100, 1200, 1247};

  BaselineMap baseline;
  baseline[kMover] = kMoverPast;
  baseline[kOverlap] = kCommitted;

  NoteEditCurrentState currentState;
  currentState.upsertRow(kOverlap, kCommitted, kStub, NoteEditPresenceType::Visible);
  currentState.upsertRow(kMover, kMoverPast, kMoverPast, NoteEditPresenceType::Visible);
  currentState.markOverlapParticipationEnded(kOverlap);

  MidiEventVec liveStore;
  currentState.projectToSessionStore(liveStore, kChannel);

  EditorSelection selection{};
  selection.primaryNote = kMover;
  selection.selectedNotes.push_back(kMover);
  EditedGeometry edited{};
  EditedNoteSpan causing{};
  causing.noteId = kMover;
  causing.span = kMoverPast;
  edited.causingSpans.push_back(causing);

  NoteEditFocus focus{};
  focus.active = true;
  focus.movingNoteId = kMover;
  focus.last = kMoverPast;
  focus.baselineMap = baseline;

  NoteIdList staleLatch;
  staleLatch.push_back(kOverlap);

  const auto targets = determineConstrainedGeometryTargetNoteIds(
      EditSessionInteractionsByTarget{}, baseline, liveStore, kChannel, kLoopLength, selection,
      edited, focus, &currentState);
  TEST_ASSERT_EQUAL(0, static_cast<int>(targets.size()));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_resolve_complete_hide_precedence_over_shorten);
  RUN_TEST(test_resolve_restrictive_shorten_combine_takes_min_end);
  RUN_TEST(test_resolve_minimum_note_length_hide);
  RUN_TEST(test_resolve_baseline_equivalent_when_causing_gone);
  RUN_TEST(test_group_rebuild_when_one_causing_removed);
  RUN_TEST(test_determine_constrained_targets_includes_restore_candidate);
  RUN_TEST(test_determine_constrained_targets_excludes_selected_moved_causing_note);
  RUN_TEST(test_determine_constrained_targets_excludes_unrelated_baseline_diff_153123);
  RUN_TEST(test_resolve_all_constrained_empty_without_interactions_or_changed_overlap_153123);
  RUN_TEST(test_resolve_start_abut_overlap_note_off_shortens_one_tick);
  RUN_TEST(test_resolve_inverted_shorten_end_hides_instead_of_emitting_inverted_span);
  RUN_TEST(test_analyze_resolve_same_pitch_complete_cover_hide_omits_cross_pitch);
  RUN_TEST(test_resolve_overlap_note_on_hides_when_target_extends_past_causing_end);
  RUN_TEST(test_resolve_overlap_note_off_inside_target_tail_shortens_or_min_length_hides);
  RUN_TEST(test_resolve_restore_candidate_uses_overlap_scratch_not_full_baseline);
  RUN_TEST(test_determine_constrained_targets_excludes_cross_pitch_changed_overlap);
  RUN_TEST(test_determine_targets_excludes_vacated_lane_on_pitch_change_011115);
  RUN_TEST(test_resolve_leave_restore_head_trimmed_live_uses_storage_baseline_010415);
  RUN_TEST(test_determine_targets_excludes_session_moved_overlap_diff_020105);
  RUN_TEST(test_determine_targets_excludes_session_moved_current_state_145011);
  RUN_TEST(test_determine_targets_includes_selected_hidden_overlap_leave_restore_111955);
  RUN_TEST(test_determine_targets_hidden_full_span_sticky_scope_113010);
  RUN_TEST(test_leave_restore_constrained_uses_session_baseline_hidden_full_span_113010);
  RUN_TEST(test_leave_restore_hidden_live_stub_uses_committed_span_175858);
  RUN_TEST(test_closure_active_suppresses_leave_restore_target_193632);
  RUN_TEST(test_leave_restore_deferred_visible_overlap_tail_224633);
  RUN_TEST(test_sealed_visible_shortened_leave_restore_after_mover_clears_234904);
  RUN_TEST(test_sealed_visible_shortened_leave_restore_rtl_after_mover_exits_left_235724);
  RUN_TEST(test_leave_restore_after_repositioned_macro_sealed_overlap_001226);
  RUN_TEST(test_leave_restore_after_overlap_closure_cleared_224633);
  RUN_TEST(test_ltr_time_axis_visible_shortened_leave_restore_022151);
  RUN_TEST(test_pitch_vacated_lane_hidden_leave_restore_target_020050);
  RUN_TEST(test_pitch_vacated_lane_visible_shortened_leave_restore_021407);
  RUN_TEST(test_determine_targets_excludes_ended_participation_even_with_stale_latch);
  return UNITY_END();
}
