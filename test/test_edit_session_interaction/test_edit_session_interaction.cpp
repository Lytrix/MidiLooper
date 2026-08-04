//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>
#include <cstdint>
#include <unordered_map>

#include "EditSessionInteraction.h"
#include "NoteEditSessionState.h"

#include "../../src/EditSessionInteraction.cpp"
#include "../../src/EditSessionLiveStoreSpan.cpp"
#include "../../src/Logger.cpp"
#include "../../src/Utils/IntervalProjection.cpp"
#include "../../src/Utils/NoteUtils.cpp"

namespace {

EditedGeometry makeEditedGeometry(NoteId causingId, uint32_t start, uint32_t end, uint8_t pitch) {
  EditedGeometry geometry{};
  geometry.selection.primaryNote = causingId;
  geometry.selection.selectedNotes.push_back(causingId);
  EditedNoteSpan span{};
  span.noteId = causingId;
  span.span = {pitch, 100, start, end};
  geometry.causingSpans.push_back(span);
  return geometry;
}

BaselineMap makeTargetBaseline(NoteId targetId, uint32_t start, uint32_t end, uint8_t pitch) {
  BaselineMap baseline;
  baseline[targetId] = {pitch, 100, start, end};
  return baseline;
}

}  // namespace

void test_orchestrator_skips_intra_selection_pair() {
  EditorSelection selection{};
  selection.selectedNotes = {10, 20};
  TEST_ASSERT_TRUE(isIntraSelectionPair(10, 20, selection));
  TEST_ASSERT_FALSE(isIntraSelectionPair(10, 30, selection));

  const std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> changed = {10};
  const std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> targets = {20, 30};
  const auto pairs = determineEligiblePairs(selection, changed, targets);
  TEST_ASSERT_EQUAL(1, static_cast<int>(pairs.size()));
  TEST_ASSERT_EQUAL_UINT32(10u, pairs[0].causingNoteId);
  TEST_ASSERT_EQUAL_UINT32(30u, pairs[0].targetNoteId);
}

void test_geometry_changed_this_tick_detects_span_delta() {
  const NoteBaseline prior{60, 100, 100, 200};
  const NoteBaseline same{60, 100, 100, 200};
  const NoteBaseline moved{60, 100, 148, 248};
  TEST_ASSERT_FALSE(geometryChangedThisTick(1, prior, same));
  TEST_ASSERT_TRUE(geometryChangedThisTick(1, prior, moved));
}

void test_determine_changed_causing_notes_uses_prior_latch() {
  EditorSelection selection{};
  selection.selectedNotes = {5};
  EditedGeometry geometry = makeEditedGeometry(5, 148, 248, 60);
  std::unordered_map<NoteId, NoteBaseline, NoteIdHash> prior;
  prior[5] = {60, 100, 100, 200};
  const auto changed = determineChangedCausingNotes(selection, geometry, prior);
  TEST_ASSERT_EQUAL(1, static_cast<int>(changed.size()));
  TEST_ASSERT_EQUAL_UINT32(5u, changed[0]);
}

void test_determine_changed_causing_notes_skips_unselected_causing() {
  EditorSelection selection{};
  selection.primaryNote = 99;
  selection.selectedNotes = {99};
  EditedGeometry geometry = makeEditedGeometry(5, 120, 336, 26);
  std::unordered_map<NoteId, NoteBaseline, NoteIdHash> prior;
  prior[5] = {26, 100, 360, 576};
  const auto changed = determineChangedCausingNotes(selection, geometry, prior);
  TEST_ASSERT_EQUAL(0, static_cast<int>(changed.size()));
}

void test_analyze_complete_cover_internal_swallow() {
  constexpr NoteId kCausing = 1;
  constexpr NoteId kTarget = 2;
  const EditedGeometry geometry = makeEditedGeometry(kCausing, 100, 300, 60);
  const BaselineMap baseline = makeTargetBaseline(kTarget, 150, 200, 60);
  const std::vector<CausingTargetPair, InternalHeapFirstAllocator<CausingTargetPair>> pairs = {
      {kCausing, kTarget}};
  const auto interactions = analyzeEditSessionInteractions(pairs, geometry, baseline);
  TEST_ASSERT_EQUAL(1, static_cast<int>(interactions.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(InteractionType::CompleteCover),
                    static_cast<int>(interactions[0].type));
}

void test_analyze_overlap_note_off_head_trim() {
  constexpr NoteId kCausing = 1;
  constexpr NoteId kTarget = 2;
  const EditedGeometry geometry = makeEditedGeometry(kCausing, 100, 300, 60);
  const BaselineMap baseline = makeTargetBaseline(kTarget, 50, 150, 60);
  const std::vector<CausingTargetPair, InternalHeapFirstAllocator<CausingTargetPair>> pairs = {
      {kCausing, kTarget}};
  const auto interactions = analyzeEditSessionInteractions(pairs, geometry, baseline);
  TEST_ASSERT_EQUAL(1, static_cast<int>(interactions.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(InteractionType::OverlapNoteOff),
                    static_cast<int>(interactions[0].type));
}

void test_analyze_overlap_note_on_tail_hide() {
  constexpr NoteId kCausing = 1;
  constexpr NoteId kTarget = 2;
  const EditedGeometry geometry = makeEditedGeometry(kCausing, 100, 200, 60);
  const BaselineMap baseline = makeTargetBaseline(kTarget, 150, 250, 60);
  const std::vector<CausingTargetPair, InternalHeapFirstAllocator<CausingTargetPair>> pairs = {
      {kCausing, kTarget}};
  const auto interactions = analyzeEditSessionInteractions(pairs, geometry, baseline);
  TEST_ASSERT_EQUAL(1, static_cast<int>(interactions.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(InteractionType::OverlapNoteOn),
                    static_cast<int>(interactions[0].type));
}

void test_analyze_omits_non_overlapping_pair() {
  constexpr NoteId kCausing = 1;
  constexpr NoteId kTarget = 2;
  const EditedGeometry geometry = makeEditedGeometry(kCausing, 100, 200, 60);
  const BaselineMap baseline = makeTargetBaseline(kTarget, 250, 350, 60);
  const std::vector<CausingTargetPair, InternalHeapFirstAllocator<CausingTargetPair>> pairs = {
      {kCausing, kTarget}};
  const auto interactions = analyzeEditSessionInteractions(pairs, geometry, baseline);
  TEST_ASSERT_EQUAL(0, static_cast<int>(interactions.size()));
}

void test_analyze_boundary_touch_adjacent_prefix() {
  // Fully clear of the left baseline (gap ≥ 1): no inclusive overlap, no BoundaryTouch edge.
  constexpr NoteId kCausing = 1;
  constexpr NoteId kTarget = 2;
  const EditedGeometry geometry = makeEditedGeometry(kCausing, 145, 241, 60);
  const BaselineMap baseline = makeTargetBaseline(kTarget, 0, 144, 60);
  const std::vector<CausingTargetPair, InternalHeapFirstAllocator<CausingTargetPair>> pairs = {
      {kCausing, kTarget}};
  const auto interactions = analyzeEditSessionInteractions(pairs, geometry, baseline);
  TEST_ASSERT_EQUAL(0, static_cast<int>(interactions.size()));
}

void test_analyze_start_abut_is_overlap_note_off_not_boundary() {
  // session_20260804_225119: causingStart == left baseline end must stay OverlapNoteOff so
  // Shorten ends at causingStart-1. BoundaryTouch+full Restore jumped +2 on a 1-tick leave.
  constexpr NoteId kCausing = 1;
  constexpr NoteId kTarget = 2;
  const EditedGeometry geometry = makeEditedGeometry(kCausing, 144, 240, 60);
  const BaselineMap baseline = makeTargetBaseline(kTarget, 0, 144, 60);
  const std::vector<CausingTargetPair, InternalHeapFirstAllocator<CausingTargetPair>> pairs = {
      {kCausing, kTarget}};
  const auto interactions = analyzeEditSessionInteractions(pairs, geometry, baseline);
  TEST_ASSERT_EQUAL(1, static_cast<int>(interactions.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(InteractionType::OverlapNoteOff),
                    static_cast<int>(interactions[0].type));
  TEST_ASSERT_EQUAL_UINT32(143u, computeShortenedEndTick(interactions[0], 1536));
}

void test_analyze_end_touch_is_overlap_note_on_not_boundary() {
  // session_20260804_223208: packed same-length notes abut end|start — must Hide, not
  // BoundaryTouch + boundary-split death spiral.
  constexpr NoteId kCausing = 1;
  constexpr NoteId kTarget = 2;
  const EditedGeometry geometry = makeEditedGeometry(kCausing, 1098, 1194, 65);
  const BaselineMap baseline = makeTargetBaseline(kTarget, 1194, 1289, 65);
  const std::vector<CausingTargetPair, InternalHeapFirstAllocator<CausingTargetPair>> pairs = {
      {kCausing, kTarget}};
  const auto interactions = analyzeEditSessionInteractions(pairs, geometry, baseline);
  TEST_ASSERT_EQUAL(1, static_cast<int>(interactions.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(InteractionType::OverlapNoteOn),
                    static_cast<int>(interactions[0].type));
}

void test_analyze_exact_same_span_is_complete_cover() {
  constexpr NoteId kCausing = 1;
  constexpr NoteId kTarget = 2;
  const EditedGeometry geometry = makeEditedGeometry(kCausing, 1098, 1193, 65);
  const BaselineMap baseline = makeTargetBaseline(kTarget, 1098, 1193, 65);
  const std::vector<CausingTargetPair, InternalHeapFirstAllocator<CausingTargetPair>> pairs = {
      {kCausing, kTarget}};
  const auto interactions = analyzeEditSessionInteractions(pairs, geometry, baseline);
  TEST_ASSERT_EQUAL(1, static_cast<int>(interactions.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(InteractionType::CompleteCover),
                    static_cast<int>(interactions[0].type));
}

void test_group_interactions_by_target_orders_deterministically() {
  EditSessionInteraction fromB{};
  fromB.type = InteractionType::OverlapNoteOff;
  fromB.causingNoteId = 20;
  fromB.targetNoteId = 10;
  fromB.baselineSpan = {60, 100, 50, 150};
  fromB.causingSpan = {60, 100, 100, 200};

  EditSessionInteraction fromC{};
  fromC.type = InteractionType::OverlapNoteOn;
  fromC.causingNoteId = 30;
  fromC.targetNoteId = 10;
  fromC.baselineSpan = {60, 100, 50, 150};
  fromC.causingSpan = {60, 100, 120, 220};

  const std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>
      interactions = {fromC, fromB};
  const EditSessionInteractionsByTarget grouped =
      groupEditSessionInteractionsByTarget(interactions);
  TEST_ASSERT_EQUAL(1, static_cast<int>(grouped.groups.size()));
  TEST_ASSERT_EQUAL_UINT32(10u, grouped.groups[0].targetNoteId);
  TEST_ASSERT_EQUAL(2, static_cast<int>(grouped.groups[0].incoming.size()));
  TEST_ASSERT_EQUAL_UINT32(20u, grouped.groups[0].incoming[0].causingNoteId);
  TEST_ASSERT_EQUAL_UINT32(30u, grouped.groups[0].incoming[1].causingNoteId);
}

void test_compute_shortened_end_tick_uses_causing_start_minus_one() {
  EditSessionInteraction interaction{};
  interaction.causingSpan = {60, 100, 100, 200};
  TEST_ASSERT_EQUAL_UINT32(99u, computeShortenedEndTick(interaction, 1536));
  interaction.causingSpan.startTick = 0;
  TEST_ASSERT_EQUAL_UINT32(1535u, computeShortenedEndTick(interaction, 1536));
}

void test_analyze_cross_pitch_inner_is_omitted() {
  constexpr NoteId kCausing = 9;
  constexpr NoteId kInner = 1;
  EditedGeometry geometry{};
  geometry.selection.primaryNote = kCausing;
  geometry.selection.selectedNotes.push_back(kCausing);
  EditedNoteSpan causing{};
  causing.noteId = kCausing;
  causing.span = {23, 100, 144, 336};
  geometry.causingSpans.push_back(causing);

  BaselineMap baseline;
  baseline[kInner] = {93, 100, 144, 192};

  const std::vector<CausingTargetPair, InternalHeapFirstAllocator<CausingTargetPair>> pairs = {
      {kCausing, kInner}};
  const auto interactions = analyzeEditSessionInteractions(pairs, geometry, baseline);
  TEST_ASSERT_EQUAL(0, static_cast<int>(interactions.size()));
}

void test_analyze_pitch_change_destination_lane_in_scope() {
  constexpr NoteId kCausing = 9;
  constexpr NoteId kDestLane = 2;
  constexpr NoteId kSourceLane = 1;
  EditedGeometry geometry{};
  geometry.selection.primaryNote = kCausing;
  geometry.selection.selectedNotes.push_back(kCausing);
  EditedNoteSpan causing{};
  causing.noteId = kCausing;
  // Pitch change: edited span is now pitch 16; destination-lane note at 16 is in scope.
  causing.span = {16, 100, 144, 336};
  geometry.causingSpans.push_back(causing);

  BaselineMap baseline;
  baseline[kDestLane] = {16, 100, 144, 192};
  baseline[kSourceLane] = {13, 100, 144, 192};

  const std::vector<CausingTargetPair, InternalHeapFirstAllocator<CausingTargetPair>> pairs = {
      {kCausing, kDestLane}, {kCausing, kSourceLane}};
  const auto interactions = analyzeEditSessionInteractions(pairs, geometry, baseline);
  TEST_ASSERT_EQUAL(1, static_cast<int>(interactions.size()));
  TEST_ASSERT_EQUAL_UINT32(kDestLane, interactions[0].targetNoteId);
  TEST_ASSERT_EQUAL(static_cast<int>(InteractionType::CompleteCover),
                    static_cast<int>(interactions[0].type));
}

void test_project_note_baseline_for_edit_analysis_wrap_parity() {
  constexpr uint32_t kLoopLength = 960;
  EditorSelection selection{};
  selection.primaryNote = 42;
  selection.selectedTick = 950;
  selection.selectedNotes.push_back(42);

  const NoteBaseline wrapped{60, 100, 900, 1080};
  const NoteBaseline linear =
      projectNoteBaselineForEditAnalysis(selection, wrapped, 42, kLoopLength);
  TEST_ASSERT_EQUAL_UINT32(900u, linear.startTick);
  TEST_ASSERT_EQUAL_UINT32(1080u, linear.endTick);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_orchestrator_skips_intra_selection_pair);
  RUN_TEST(test_geometry_changed_this_tick_detects_span_delta);
  RUN_TEST(test_determine_changed_causing_notes_uses_prior_latch);
  RUN_TEST(test_determine_changed_causing_notes_skips_unselected_causing);
  RUN_TEST(test_analyze_complete_cover_internal_swallow);
  RUN_TEST(test_analyze_overlap_note_off_head_trim);
  RUN_TEST(test_analyze_overlap_note_on_tail_hide);
  RUN_TEST(test_analyze_omits_non_overlapping_pair);
  RUN_TEST(test_analyze_boundary_touch_adjacent_prefix);
  RUN_TEST(test_analyze_start_abut_is_overlap_note_off_not_boundary);
  RUN_TEST(test_analyze_end_touch_is_overlap_note_on_not_boundary);
  RUN_TEST(test_analyze_exact_same_span_is_complete_cover);
  RUN_TEST(test_analyze_cross_pitch_inner_is_omitted);
  RUN_TEST(test_analyze_pitch_change_destination_lane_in_scope);
  RUN_TEST(test_group_interactions_by_target_orders_deterministically);
  RUN_TEST(test_compute_shortened_end_tick_uses_causing_start_minus_one);
  RUN_TEST(test_project_note_baseline_for_edit_analysis_wrap_parity);
  return UNITY_END();
}
