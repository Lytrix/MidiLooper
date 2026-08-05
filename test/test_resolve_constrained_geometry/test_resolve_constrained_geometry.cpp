//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>
#include <cstdint>

#include "ResolveConstrainedGeometry.h"
#include "EditSessionInteraction.h"
#include "MidiEvent.h"

#include "../../src/Logger.cpp"
#include "../../src/Utils/IntervalProjection.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/EditSessionLiveStoreSpan.cpp"
#include "../../src/EditSessionInteraction.cpp"
#include "../../src/ResolveConstrainedGeometry.cpp"

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
  const auto targets = determineConstrainedGeometryTargetNoteIds(
      emptyGrouped, baseline, liveStore, 1, kLoopLength, selection, edited);
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
  baseline[kOverlap] = {65, 100, 906, 1001};

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
  const auto targets = determineConstrainedGeometryTargetNoteIds(
      emptyGrouped, baseline, liveStore, 1, kLoopLength, selection, edited);
  TEST_ASSERT_EQUAL(1, static_cast<int>(targets.size()));
  TEST_ASSERT_EQUAL_UINT32(kOverlap, targets[0]);
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
  for (const auto& [noteId, span] : baseline) {
    projectedBaseline[noteId] =
        projectNoteBaselineForEditAnalysis(selection, span, noteId, loopLength);
  }
  EditedGeometry projectedEdited = edited;
  projectedEdited.causingSpans[0].span = projectNoteBaselineForEditAnalysis(
      selection, edited.causingSpans[0].span, kMoverId, loopLength);

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

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_resolve_complete_hide_precedence_over_shorten);
  RUN_TEST(test_resolve_restrictive_shorten_combine_takes_min_end);
  RUN_TEST(test_resolve_minimum_note_length_hide);
  RUN_TEST(test_resolve_baseline_equivalent_when_causing_gone);
  RUN_TEST(test_group_rebuild_when_one_causing_removed);
  RUN_TEST(test_determine_constrained_targets_includes_restore_candidate);
  RUN_TEST(test_determine_constrained_targets_excludes_selected_moved_causing_note);
  RUN_TEST(test_resolve_start_abut_overlap_note_off_shortens_one_tick);
  RUN_TEST(test_resolve_inverted_shorten_end_hides_instead_of_emitting_inverted_span);
  RUN_TEST(test_analyze_resolve_same_pitch_complete_cover_hide_omits_cross_pitch);
  return UNITY_END();
}
