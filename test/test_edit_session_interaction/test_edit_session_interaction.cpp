//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>
#include <cstdint>
#include <unordered_map>

#include "Utils/NoteEditMem.h"
#include "EditSessionInteraction.h"
#include "NoteEditCurrentState.h"
#include "NoteEditFocus.h"
#include "NoteEditSessionState.h"
#include "ResolveConstrainedGeometry.h"

#include "../../src/EditManager/EditSessionInteraction.cpp"
#include "../../src/EditManager/NoteEditCurrentState.cpp"
#include "../../src/EditManager/EditSessionLiveStoreSpan.cpp"
#include "../../src/EditManager/ResolveConstrainedGeometry.cpp"
#include "../../src/EditManager/ParticipatingNoteSession.cpp"
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

void test_orchestrator_skips_intra_selection_pair_when_co_moving() {
  EditorSelection selection{};
  selection.selectedNotes = {10, 20};
  TEST_ASSERT_TRUE(isIntraSelectionPair(10, 20, selection));
  TEST_ASSERT_FALSE(isIntraSelectionPair(10, 30, selection));

  const std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> changedBoth = {10, 20};
  const std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> targets = {20, 30};
  const auto coMovePairs = determineEligiblePairs(selection, changedBoth, targets);
  TEST_ASSERT_EQUAL(2, static_cast<int>(coMovePairs.size()));
  for (const CausingTargetPair& pair : coMovePairs) {
    TEST_ASSERT_FALSE(pair.causingNoteId == 10u && pair.targetNoteId == 20u);
    TEST_ASSERT_FALSE(pair.causingNoteId == 20u && pair.targetNoteId == 10u);
  }
}

void test_orchestrator_includes_stationary_selected_sibling_overlap_111955() {
  // session_20260807_111955: notes 7+17 selected; coarse fader moves 17 only over 7 at 2064.
  constexpr NoteId kMover = 17;
  constexpr NoteId kStationarySibling = 7;
  constexpr uint8_t kPitch = 88;

  EditorSelection selection{};
  selection.primaryNote = kMover;
  selection.selectedNotes = {kMover, kStationarySibling};

  EditedGeometry geometry{};
  geometry.selection = selection;
  EditedNoteSpan causing{};
  causing.noteId = kMover;
  causing.span = {kPitch, 100, 2064, 2591};
  geometry.causingSpans.push_back(causing);

  BaselineMap baseline;
  baseline[kStationarySibling] = {kPitch, 100, 2064, 2111};

  const std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> changed = {kMover};
  const std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> targets = {kStationarySibling};
  const auto pairs = determineEligiblePairs(selection, changed, targets);
  TEST_ASSERT_EQUAL(1, static_cast<int>(pairs.size()));
  TEST_ASSERT_EQUAL_UINT32(kMover, pairs[0].causingNoteId);
  TEST_ASSERT_EQUAL_UINT32(kStationarySibling, pairs[0].targetNoteId);

  const auto interactions = analyzeEditSessionInteractions(pairs, geometry, baseline);
  TEST_ASSERT_EQUAL(1, static_cast<int>(interactions.size()));
  TEST_ASSERT_EQUAL(InteractionType::CompleteCover, interactions[0].type);
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
      projectNoteBaselineForEditAnalysis(selection, wrapped, 42, kLoopLength, 900);
  TEST_ASSERT_EQUAL_UINT32(900u, linear.startTick);
  TEST_ASSERT_EQUAL_UINT32(1080u, linear.endTick);
}

namespace {

MidiEvent taggedNoteOn(uint32_t tick, uint8_t channel, uint8_t pitch, NoteId noteId) {
  MidiEvent evt = MidiEvent::NoteOn(tick, channel, pitch, 100);
  evt.noteId = noteId;
  return evt;
}

bool scopeContains(const NoteIdList& scope, NoteId noteId) {
  return std::find(scope.begin(), scope.end(), noteId) != scope.end();
}

}  // namespace

/// Evaluation scope keeps only the mover's lane. Cross-lane notes cost nothing per tick and
/// can never produce a Hide or Shorten anyway (Q14).
void test_evaluation_scope_excludes_cross_lane_notes() {
  constexpr uint8_t kChannel = 5;
  constexpr NoteId kMoverId = 9;
  constexpr NoteId kSameLaneId = 2;
  constexpr NoteId kCrossLaneId = 3;

  BaselineMap baseline;
  baseline[kMoverId] = {13, 100, 480, 528};
  baseline[kSameLaneId] = {13, 100, 144, 192};
  baseline[kCrossLaneId] = {85, 100, 144, 192};

  MidiEventVec liveStore;
  liveStore.push_back(taggedNoteOn(480, kChannel, 13, kMoverId));
  liveStore.push_back(taggedNoteOn(144, kChannel, 13, kSameLaneId));
  liveStore.push_back(taggedNoteOn(144, kChannel, 85, kCrossLaneId));

  const NoteIdList noneChanged;
  const NoteIdList scope =
      collectEvaluationScopeNoteIds(baseline, liveStore, noneChanged, kMoverId, 13);
  TEST_ASSERT_EQUAL(1, static_cast<int>(scope.size()));
  TEST_ASSERT_TRUE(scopeContains(scope, kSameLaneId));
  TEST_ASSERT_FALSE(scopeContains(scope, kCrossLaneId));
  TEST_ASSERT_FALSE(scopeContains(scope, kMoverId));
}

/// Without a lane the scope is every note — pre-lane behaviour is preserved.
void test_evaluation_scope_without_lane_includes_all_notes() {
  constexpr uint8_t kChannel = 5;
  constexpr NoteId kMoverId = 9;

  BaselineMap baseline;
  baseline[kMoverId] = {13, 100, 480, 528};
  baseline[2] = {13, 100, 144, 192};
  baseline[3] = {85, 100, 144, 192};

  MidiEventVec liveStore;
  const NoteIdList noneChanged;
  const NoteIdList scope =
      collectEvaluationScopeNoteIds(baseline, liveStore, noneChanged, kMoverId, std::nullopt);
  TEST_ASSERT_EQUAL(2, static_cast<int>(scope.size()));
  TEST_ASSERT_TRUE(scopeContains(scope, 2));
  TEST_ASSERT_TRUE(scopeContains(scope, 3));
}

/// Source-lane restore after a pitch change: the note hidden on lane 13 is absent from the live
/// store and off the new lane 16, so only its changedOverlapNoteIds membership keeps it in
/// scope. Without the sticky rule the restore action could never be generated.
void test_evaluation_scope_keeps_hidden_source_lane_note_after_pitch_change() {
  constexpr uint8_t kChannel = 5;
  constexpr NoteId kMoverId = 9;
  constexpr NoteId kHiddenOnSourceLane = 2;
  constexpr NoteId kDestLaneId = 4;

  BaselineMap baseline;
  baseline[kMoverId] = {13, 100, 144, 336};
  baseline[kHiddenOnSourceLane] = {13, 100, 192, 240};
  baseline[kDestLaneId] = {16, 100, 600, 700};

  // Hidden note is gone from the live store; mover now sounds on lane 16.
  MidiEventVec liveStore;
  liveStore.push_back(taggedNoteOn(144, kChannel, 16, kMoverId));
  liveStore.push_back(taggedNoteOn(600, kChannel, 16, kDestLaneId));

  NoteIdList changed;
  changed.push_back(kHiddenOnSourceLane);

  const NoteIdList scope =
      collectEvaluationScopeNoteIds(baseline, liveStore, changed, kMoverId, 16);
  TEST_ASSERT_TRUE(scopeContains(scope, kDestLaneId));
  TEST_ASSERT_TRUE(scopeContains(scope, kHiddenOnSourceLane));

  // The projected baseline must carry both the sticky note and the mover, or resolve and build
  // cannot emit the restore.
  EditorSelection selection{};
  selection.primaryNote = kMoverId;
  selection.selectedNotes.push_back(kMoverId);
  const BaselineMap projected = projectTransactionBaselineForEvaluationScope(
      selection, baseline, scope, kMoverId, 2304, 144);
  TEST_ASSERT_EQUAL(3, static_cast<int>(projected.size()));
  TEST_ASSERT_TRUE(projected.count(kMoverId) > 0);
  TEST_ASSERT_TRUE(projected.count(kHiddenOnSourceLane) > 0);
  TEST_ASSERT_TRUE(projected.count(kDestLaneId) > 0);
}

/// Materialized record/overdub passes carry the MIDI channel played at record time, which need
/// not equal the track's output channel. Scope membership is NoteId + pitch lane, so a same-pitch
/// overlap stays visible no matter which channel its events carry (session_20260805_030517:
/// candidates=0 hid every overlap while the store held 70 notes).
void test_evaluation_scope_ignores_store_channel() {
  constexpr NoteId kMoverId = 9;
  constexpr NoteId kSameLaneId = 2;

  BaselineMap baseline;
  baseline[kMoverId] = {26, 100, 1968, 2160};
  baseline[kSameLaneId] = {26, 100, 2016, 2112};

  // Store events recorded on channel 5; the track now reports channel 2.
  MidiEventVec liveStore;
  liveStore.push_back(taggedNoteOn(1968, 5, 26, kMoverId));
  liveStore.push_back(taggedNoteOn(2016, 5, 26, kSameLaneId));

  const NoteIdList noneChanged;
  const NoteIdList scope =
      collectEvaluationScopeNoteIds(baseline, liveStore, noneChanged, kMoverId, 26);
  TEST_ASSERT_EQUAL(1, static_cast<int>(scope.size()));
  TEST_ASSERT_TRUE(scopeContains(scope, kSameLaneId));
}

/// The projected baseline drops out-of-scope notes but always keeps the mover.
void test_projected_baseline_drops_cross_lane_keeps_mover() {
  constexpr NoteId kMoverId = 9;
  BaselineMap baseline;
  baseline[kMoverId] = {13, 100, 480, 528};
  baseline[2] = {13, 100, 144, 192};
  baseline[3] = {85, 100, 144, 192};

  NoteIdList scope;
  scope.push_back(2);

  EditorSelection selection{};
  selection.primaryNote = kMoverId;
  selection.selectedNotes.push_back(kMoverId);
  const BaselineMap projected =
      projectTransactionBaselineForEvaluationScope(selection, baseline, scope, kMoverId, 2304, 480);
  TEST_ASSERT_EQUAL(2, static_cast<int>(projected.size()));
  TEST_ASSERT_TRUE(projected.count(kMoverId) > 0);
  TEST_ASSERT_TRUE(projected.count(2) > 0);
  TEST_ASSERT_EQUAL(0, static_cast<int>(projected.count(3)));
}

/// session_20260805_183125: lane targets in live store but missing from baselineMap produced
/// pairs>0 interactions=0 until ensure fills spans from the live store.
void test_ensure_baseline_fills_scope_gap_enables_pitch_lane_interactions() {
  constexpr uint8_t kChannel = 2;
  constexpr NoteId kMoverId = 25;
  constexpr NoteId kLaneTarget = 26;
  constexpr uint8_t kLane = 23;

  MidiEvent moverOn = taggedNoteOn(1152, kChannel, kLane, kMoverId);
  MidiEvent moverOff = MidiEvent::NoteOff(1252, kChannel, kLane, 0);
  moverOff.noteId = kMoverId;
  MidiEvent targetOn = taggedNoteOn(1152, kChannel, kLane, kLaneTarget);
  MidiEvent targetOff = MidiEvent::NoteOff(1252, kChannel, kLane, 0);
  targetOff.noteId = kLaneTarget;

  MidiEventVec liveStore;
  liveStore.push_back(moverOn);
  liveStore.push_back(moverOff);
  liveStore.push_back(targetOn);
  liveStore.push_back(targetOff);

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.baselineMap[kMoverId] = {18, 100, 1152, 1252};

  const NoteIdList noneChanged;
  const NoteIdList scope =
      collectEvaluationScopeNoteIds(focus.baselineMap, liveStore, noneChanged, kMoverId, kLane);
  TEST_ASSERT_TRUE(scopeContains(scope, kLaneTarget));
  TEST_ASSERT_EQUAL(0, static_cast<int>(focus.baselineMap.count(kLaneTarget)));

  ensureBaselineMapEntriesForEvaluationScope(focus, scope, liveStore, kChannel);
  TEST_ASSERT_EQUAL(1, static_cast<int>(focus.baselineMap.count(kLaneTarget)));

  EditedGeometry geometry{};
  geometry.selection.primaryNote = kMoverId;
  geometry.selection.selectedNotes.push_back(kMoverId);
  EditedNoteSpan causing{};
  causing.noteId = kMoverId;
  causing.span = {kLane, 100, 1152, 1252};
  geometry.causingSpans.push_back(causing);

  const BaselineMap projected = projectTransactionBaselineForEvaluationScope(
      geometry.selection, focus.baselineMap, scope, kMoverId, 2304, 1152);
  TEST_ASSERT_TRUE(projected.count(kLaneTarget) > 0);

  const std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> changed = {kMoverId};
  const auto pairs = determineEligiblePairs(geometry.selection, changed, scope);
  TEST_ASSERT_EQUAL(1, static_cast<int>(pairs.size()));

  const auto interactions = analyzeEditSessionInteractions(pairs, geometry, projected);
  TEST_ASSERT_EQUAL(1, static_cast<int>(interactions.size()));
  TEST_ASSERT_EQUAL_UINT32(kLaneTarget, interactions[0].targetNoteId);
  TEST_ASSERT_EQUAL(static_cast<int>(InteractionType::CompleteCover),
                    static_cast<int>(interactions[0].type));
}

/// Add (Create): new selected note is causing input — same-pitch overlap uses standard classify.
void test_add_note_same_pitch_overlap_complete_cover() {
  constexpr NoteId kNewNoteId = 50;
  constexpr NoteId kNeighborId = 2;

  EditorSelection selection{};
  selection.primaryNote = kNewNoteId;
  selection.selectedNotes.push_back(kNewNoteId);

  EditedGeometry geometry{};
  geometry.selection = selection;
  EditedNoteSpan causing{};
  causing.noteId = kNewNoteId;
  causing.span = {60, 80, 100, 200};
  geometry.causingSpans.push_back(causing);

  BaselineMap baseline;
  baseline[kNeighborId] = {60, 100, 120, 180};

  const std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> changed = {kNewNoteId};
  const std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> targets = {kNeighborId};
  const auto pairs = determineEligiblePairs(selection, changed, targets);
  const auto interactions = analyzeEditSessionInteractions(pairs, geometry, baseline);
  TEST_ASSERT_EQUAL(1, static_cast<int>(interactions.size()));
  TEST_ASSERT_EQUAL(InteractionType::CompleteCover, interactions[0].type);
  TEST_ASSERT_EQUAL_UINT32(kNeighborId, interactions[0].targetNoteId);
}

/// Delete: causing note omitted from edited geometry — hidden overlap is a restore candidate.
void test_delete_causing_restore_candidate_without_incoming() {
  constexpr uint8_t kChannel = 5;
  constexpr NoteId kDeletedCausingId = 9;
  constexpr NoteId kHiddenNeighborId = 2;
  constexpr uint32_t loopLength = 384;

  NoteEditFocus focus{};
  focus.active = true;
  focus.movingNoteId = kDeletedCausingId;
  focus.last = {60, 100, 100, 200};
  focus.baselineMap[kDeletedCausingId] = {60, 100, 100, 200};
  focus.baselineMap[kHiddenNeighborId] = {60, 100, 120, 180};
  focus.changedOverlapNoteIds.push_back(kHiddenNeighborId);

  EditorSelection selection{};
  selection.primaryNote = kDeletedCausingId;
  selection.selectedNotes.push_back(kDeletedCausingId);

  EditedGeometry editedGeometry{};
  editedGeometry.selection = selection;

  EditSessionInteractionsByTarget grouped{};
  MidiEventVec liveStore;
  // Causing deleted; neighbor still hidden from prior Hide.

  const std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> targetIds =
      determineConstrainedGeometryTargetNoteIds(grouped, focus.baselineMap, liveStore, kChannel,
                                                loopLength, selection, editedGeometry,
                                                focus.changedOverlapNoteIds, focus);
  TEST_ASSERT_EQUAL(1, static_cast<int>(targetIds.size()));
  TEST_ASSERT_EQUAL_UINT32(kHiddenNeighborId, targetIds[0]);

  const std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>
      emptyIncoming;
  const ConstrainedNoteGeometry constrained = resolveConstrainedGeometry(
      kHiddenNeighborId, focus.baselineMap[kHiddenNeighborId], emptyIncoming, loopLength, 0, false);
  TEST_ASSERT_TRUE(constrained.visible);
  TEST_ASSERT_EQUAL_UINT32(120u, constrained.startTick);
  TEST_ASSERT_EQUAL_UINT32(180u, constrained.endTick);
}

/// session_20260807_004127: shared edit projection originTick aligns wrapped overlap target
/// with causing note so classification is OverlapNoteOff (tail shorten) not spurious OverlapNoteOn.
void test_shared_edit_projection_origin_tail_shorten_only_004127() {
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kMoverId = 3;
  constexpr NoteId kTargetId = 14;

  EditorSelection selection{};
  selection.primaryNote = kMoverId;
  selection.selectedTick = 3888;
  selection.selectedNotes.push_back(kMoverId);

  const NoteBaseline causingSpan{94, 100, 3888, 4031};
  const NoteBaseline wrappedTarget{94, 100, 2880, 4000};

  const int32_t bootstrapOrigin = static_cast<int32_t>(causingSpan.startTick);
  const int32_t sharedOrigin =
      static_cast<int32_t>(
          projectNoteBaselineForEditAnalysis(selection, causingSpan, kMoverId, kLoopLength,
                                             bootstrapOrigin)
              .startTick);

  const NoteBaseline sharedTarget =
      projectNoteBaselineForEditAnalysis(selection, wrappedTarget, kTargetId, kLoopLength,
                                         sharedOrigin);
  const NoteBaseline sharedCausing =
      projectNoteBaselineForEditAnalysis(selection, causingSpan, kMoverId, kLoopLength,
                                         sharedOrigin);

  TEST_ASSERT_EQUAL(static_cast<int>(InteractionType::OverlapNoteOff),
                    static_cast<int>(
                        classifyEditSessionInteraction(sharedCausing.startTick, sharedCausing.endTick,
                                                       sharedTarget.startTick, sharedTarget.endTick)));

  BaselineMap sharedBaseline;
  sharedBaseline[kTargetId] = sharedTarget;
  EditedGeometry sharedEdited{};
  sharedEdited.selection = selection;
  EditedNoteSpan causing{};
  causing.noteId = kMoverId;
  causing.span = sharedCausing;
  sharedEdited.causingSpans.push_back(causing);

  const std::vector<CausingTargetPair, InternalHeapFirstAllocator<CausingTargetPair>> pairs = {
      {kMoverId, kTargetId}};
  const auto interactions = analyzeEditSessionInteractions(pairs, sharedEdited, sharedBaseline);
  TEST_ASSERT_EQUAL(1, static_cast<int>(interactions.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(InteractionType::OverlapNoteOff),
                    static_cast<int>(interactions[0].type));

  const EditSessionInteractionsByTarget grouped =
      groupEditSessionInteractionsByTarget(interactions);
  const ConstrainedNoteGeometry constrained =
      resolveConstrainedGeometry(kTargetId, sharedBaseline[kTargetId], grouped.groups[0].incoming,
                                 kLoopLength, 12, true);
  TEST_ASSERT_TRUE(constrained.visible);
  TEST_ASSERT_EQUAL_UINT32(sharedTarget.startTick, constrained.startTick);
  TEST_ASSERT_EQUAL_UINT32(3887u, constrained.endTick);
}

/// session_20260807_011618: geometry pipeline classifies on storage baselineMap — local L→R overlap
/// must not wait on projected k-shift copies.
void test_storage_overlap_analyze_tail_shorten_only_004127() {
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kMoverId = 3;
  constexpr NoteId kTargetId = 14;

  EditorSelection selection{};
  selection.primaryNote = kMoverId;
  selection.selectedTick = 3888;
  selection.selectedNotes.push_back(kMoverId);

  const NoteBaseline causingSpan{94, 100, 3888, 4031};
  const NoteBaseline wrappedTarget{94, 100, 2880, 4000};

  BaselineMap storageBaseline;
  storageBaseline[kTargetId] = wrappedTarget;

  EditedGeometry edited{};
  edited.selection = selection;
  EditedNoteSpan causing{};
  causing.noteId = kMoverId;
  causing.span = causingSpan;
  edited.causingSpans.push_back(causing);

  const std::vector<CausingTargetPair, InternalHeapFirstAllocator<CausingTargetPair>> pairs = {
      {kMoverId, kTargetId}};
  const auto interactions = analyzeEditSessionInteractions(pairs, edited, storageBaseline);
  TEST_ASSERT_EQUAL(1, static_cast<int>(interactions.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(InteractionType::OverlapNoteOff),
                    static_cast<int>(interactions[0].type));

  const EditSessionInteractionsByTarget grouped =
      groupEditSessionInteractionsByTarget(interactions);
  const ConstrainedNoteGeometry constrained =
      resolveConstrainedGeometry(kTargetId, storageBaseline[kTargetId], grouped.groups[0].incoming,
                                 kLoopLength, 12, true);
  TEST_ASSERT_TRUE(constrained.visible);
  TEST_ASSERT_EQUAL_UINT32(wrappedTarget.startTick, constrained.startTick);
  TEST_ASSERT_EQUAL_UINT32(3887u, constrained.endTick);
}

void test_storage_overlap_analyze_ltr_complete_cover_hides_short_target() {
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kMoverId = 3;
  constexpr NoteId kTargetId = 4;

  BaselineMap baseline;
  baseline[kTargetId] = {94, 100, 1200, 1247};

  EditedGeometry edited{};
  edited.selection.primaryNote = kMoverId;
  edited.selection.selectedNotes.push_back(kMoverId);
  EditedNoteSpan causing{};
  causing.noteId = kMoverId;
  causing.span = {94, 100, 1104, 1247};
  edited.causingSpans.push_back(causing);

  const std::vector<CausingTargetPair, InternalHeapFirstAllocator<CausingTargetPair>> pairs = {
      {kMoverId, kTargetId}};
  const auto interactions = analyzeEditSessionInteractions(pairs, edited, baseline);
  TEST_ASSERT_EQUAL(1, static_cast<int>(interactions.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(InteractionType::CompleteCover),
                    static_cast<int>(interactions[0].type));

  const EditSessionInteractionsByTarget grouped =
      groupEditSessionInteractionsByTarget(interactions);
  const ConstrainedNoteGeometry constrained =
      resolveConstrainedGeometry(kTargetId, baseline[kTargetId], grouped.groups[0].incoming,
                                 kLoopLength, 12, true);
  TEST_ASSERT_FALSE(constrained.visible);
}

void test_analyze_ignores_session_moved_overlap_at_live_span_020105() {
  // session_20260807_020105: note 17 moved 3600→1248; later same-pitch move must not shorten 17 at 3600.
  constexpr NoteId kPriorMoverId = 17;
  constexpr NoteId kNewMoverId = 25;
  constexpr uint8_t kPitch = 88;
  constexpr uint8_t kChannel = 5;

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

  NoteIdList changedOverlap;
  changedOverlap.push_back(kPriorMoverId);

  const BaselineMap analysis =
      overlayAnalysisBaselineForSessionMovedOverlaps(baseline, kNewMoverId, liveStore, kChannel,
                                                     5376u, nullptr, nullptr);
  const auto analysisIt = analysis.find(kPriorMoverId);
  TEST_ASSERT_TRUE(analysisIt != analysis.end());
  TEST_ASSERT_EQUAL_UINT32(1248u, analysisIt->second.startTick);

  EditorSelection selection{};
  selection.primaryNote = kNewMoverId;
  selection.selectedNotes.push_back(kNewMoverId);
  EditedGeometry geometry{};
  geometry.selection = selection;
  EditedNoteSpan causing{};
  causing.noteId = kNewMoverId;
  causing.span = {kPitch, 100, 3744, 4175};
  geometry.causingSpans.push_back(causing);

  const std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> changed = {kNewMoverId};
  const std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> scope = {kPriorMoverId, kNewMoverId};
  const auto pairs = determineEligiblePairs(selection, changed, scope);
  TEST_ASSERT_EQUAL(1, static_cast<int>(pairs.size()));

  const auto staleInteractions = analyzeEditSessionInteractions(pairs, geometry, baseline);
  TEST_ASSERT_EQUAL(1, static_cast<int>(staleInteractions.size()));
  TEST_ASSERT_EQUAL_UINT32(kPriorMoverId, staleInteractions[0].targetNoteId);

  const auto liveInteractions = analyzeEditSessionInteractions(pairs, geometry, analysis);
  TEST_ASSERT_EQUAL(0, static_cast<int>(liveInteractions.size()));
}

void test_analyze_ignores_session_moved_baseline_without_changed_overlap_id_020600() {
  // session_20260807_020600: note 17 at 1488 after move; not in changedOverlapNoteIds on reselect.
  constexpr NoteId kPriorMoverId = 17;
  constexpr NoteId kNewMoverId = 25;
  constexpr uint8_t kPitch = 88;
  constexpr uint8_t kChannel = 5;

  BaselineMap baseline;
  baseline[kPriorMoverId] = {kPitch, 100, 3600, 4127};
  baseline[kNewMoverId] = {kPitch, 100, 3504, 4031};

  MidiEventVec liveStore;
  MidiEvent priorOn = MidiEvent::NoteOn(1488, kChannel, kPitch, 100);
  priorOn.noteId = kPriorMoverId;
  liveStore.push_back(priorOn);
  MidiEvent priorOff = MidiEvent::NoteOff(2063, kChannel, kPitch, 0);
  priorOff.noteId = kPriorMoverId;
  liveStore.push_back(priorOff);

  const BaselineMap analysis =
      overlayAnalysisBaselineForSessionMovedOverlaps(baseline, kNewMoverId, liveStore, kChannel,
                                                     5376u, nullptr, nullptr);
  const auto analysisIt = analysis.find(kPriorMoverId);
  TEST_ASSERT_TRUE(analysisIt != analysis.end());
  TEST_ASSERT_EQUAL_UINT32(1488u, analysisIt->second.startTick);

  EditorSelection selection{};
  selection.primaryNote = kNewMoverId;
  selection.selectedNotes.push_back(kNewMoverId);
  EditedGeometry geometry{};
  geometry.selection = selection;
  EditedNoteSpan causing{};
  causing.noteId = kNewMoverId;
  causing.span = {kPitch, 100, 3600, 4031};
  geometry.causingSpans.push_back(causing);

  const std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> changed = {kNewMoverId};
  const std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> scope = {kPriorMoverId, kNewMoverId};
  const auto pairs = determineEligiblePairs(selection, changed, scope);

  const auto staleInteractions = analyzeEditSessionInteractions(pairs, geometry, baseline);
  TEST_ASSERT_EQUAL(1, static_cast<int>(staleInteractions.size()));

  const auto liveInteractions = analyzeEditSessionInteractions(pairs, geometry, analysis);
  TEST_ASSERT_EQUAL(0, static_cast<int>(liveInteractions.size()));
}

void test_overlap_analyze_uses_current_span_not_stale_baseline_021939() {
  // session_20260807_021939: mover 25 overlaps note 17 at current span 1392, not stale 3600.
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kPriorId = 17;
  constexpr NoteId kMoverId = 25;
  constexpr uint8_t kPitch = 88;
  constexpr uint8_t kChannel = 5;

  BaselineMap baseline;
  baseline[kPriorId] = {kPitch, 100, 3600, 4127};
  baseline[kMoverId] = {kPitch, 100, 3504, 4031};

  NoteEditCurrentState currentState;
  currentState.upsertRow(kPriorId, baseline[kPriorId], {kPitch, 100, 1392, 1919},
                         NoteEditPresenceType::Visible);
  currentState.upsertRow(kMoverId, baseline[kMoverId], baseline[kMoverId],
                         NoteEditPresenceType::Visible);

  MidiEventVec liveStore;
  currentState.projectToSessionStore(liveStore, kChannel);

  NoteIdList changedOverlap;
  changedOverlap.push_back(kPriorId);

  const BaselineMap analysis =
      overlayAnalysisBaselineForSessionMovedOverlaps(baseline, kMoverId, liveStore, kChannel,
                                                     kLoopLength, &currentState, nullptr);
  const auto analysisIt = analysis.find(kPriorId);
  TEST_ASSERT_TRUE(analysisIt != analysis.end());
  TEST_ASSERT_EQUAL_UINT32(1392u, analysisIt->second.startTick);

  EditorSelection selection{};
  selection.primaryNote = kMoverId;
  selection.selectedNotes.push_back(kMoverId);
  EditedGeometry geometry{};
  geometry.selection = selection;
  EditedNoteSpan causing{};
  causing.noteId = kMoverId;
  causing.span = {kPitch, 100, 1400, 2000};
  geometry.causingSpans.push_back(causing);

  const std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> changed = {kMoverId};
  const NoteIdList scope =
      collectEvaluationScopeNoteIds(baseline, liveStore, changedOverlap, kMoverId, kPitch,
                                    &currentState);
  const auto pairs = determineEligiblePairs(selection, changed, scope);
  TEST_ASSERT_EQUAL(1, static_cast<int>(pairs.size()));

  const auto interactions = analyzeEditSessionInteractions(pairs, geometry, analysis);
  TEST_ASSERT_EQUAL(1, static_cast<int>(interactions.size()));
  TEST_ASSERT_EQUAL_UINT32(kPriorId, interactions[0].targetNoteId);
}

void test_overlay_committed_span_shortened_stub_ltr_overlap_181859() {
  // session_20260807_181859: overlap note 9 shortened to stub 2152–2207; L→R mover start 2256 must
  // still classify OverlapNoteOff against committed 2152–2543 (not BoundaryTouch on stub end).
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kOverlapId = 9;
  constexpr NoteId kMoverId = 26;
  constexpr uint8_t kPitch = 88;
  constexpr uint8_t kChannel = 5;

  const NoteBaseline committed{kPitch, 100, 2152, 2543};
  const NoteBaseline stub{kPitch, 100, 2152, 2207};
  const NoteBaseline moverCommitted{kPitch, 100, 2016, 2111};

  BaselineMap baseline;
  baseline[kOverlapId] = committed;
  baseline[kMoverId] = moverCommitted;

  NoteEditCurrentState currentState;
  currentState.upsertRow(kOverlapId, committed, stub, NoteEditPresenceType::Visible);
  currentState.upsertRow(kMoverId, moverCommitted, moverCommitted, NoteEditPresenceType::Visible);

  MidiEventVec liveStore;
  currentState.projectToSessionStore(liveStore, kChannel);

  EditedNoteSpan causingSpan{};
  causingSpan.noteId = kMoverId;
  causingSpan.span = {kPitch, 100, 2256, 2351};

  const BaselineMap analysis =
      overlayAnalysisBaselineForSessionMovedOverlaps(baseline, kMoverId, liveStore, kChannel,
                                                     kLoopLength, &currentState, &causingSpan.span);
  const auto analysisIt = analysis.find(kOverlapId);
  TEST_ASSERT_TRUE(analysisIt != analysis.end());
  TEST_ASSERT_EQUAL_UINT32(committed.startTick, analysisIt->second.startTick);
  TEST_ASSERT_EQUAL_UINT32(committed.endTick, analysisIt->second.endTick);

  EditorSelection selection{};
  selection.primaryNote = kMoverId;
  selection.selectedNotes.push_back(kMoverId);
  EditedGeometry geometry{};
  geometry.selection = selection;
  geometry.causingSpans.push_back(causingSpan);

  NoteIdList changedOverlap;
  changedOverlap.push_back(kOverlapId);

  const std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> changed = {kMoverId};
  const NoteIdList scope =
      collectEvaluationScopeNoteIds(baseline, liveStore, changedOverlap, kMoverId, kPitch,
                                    &currentState);
  const auto pairs = determineEligiblePairs(selection, changed, scope);
  TEST_ASSERT_EQUAL(1, static_cast<int>(pairs.size()));

  BaselineMap stubOverlay = baseline;
  stubOverlay[kOverlapId] = stub;
  const auto stubInteractions = analyzeEditSessionInteractions(pairs, geometry, stubOverlay);
  TEST_ASSERT_EQUAL(0, static_cast<int>(stubInteractions.size()));

  const auto interactions = analyzeEditSessionInteractions(pairs, geometry, analysis);
  TEST_ASSERT_EQUAL(1, static_cast<int>(interactions.size()));
  TEST_ASSERT_EQUAL_UINT32(kOverlapId, interactions[0].targetNoteId);
  TEST_ASSERT_EQUAL(static_cast<int>(InteractionType::OverlapNoteOff),
                    static_cast<int>(interactions[0].type));
}

void test_evaluation_scope_excludes_sealed_deleted_022849() {
  // Stage 7.5.E2: Deleted rows must not re-enter evaluation scope after deselect seal.
  constexpr NoteId kMoverId = 7;
  constexpr NoteId kDeletedId = 11;
  constexpr uint8_t kPitch = 88;
  constexpr uint8_t kChannel = 5;

  BaselineMap baseline;
  baseline[kMoverId] = {kPitch, 100, 1621, 1984};

  NoteEditCurrentState currentState;
  currentState.upsertRow(kDeletedId, {kPitch, 100, 1584, 1631}, {kPitch, 100, 1584, 1631},
                         NoteEditPresenceType::Deleted);
  currentState.upsertRow(kMoverId, baseline[kMoverId], baseline[kMoverId],
                         NoteEditPresenceType::Visible);

  MidiEventVec liveStore;
  currentState.projectToSessionStore(liveStore, kChannel);
  NoteIdList noneChanged;
  const NoteIdList scope =
      collectEvaluationScopeNoteIds(baseline, liveStore, noneChanged, kMoverId, kPitch,
                                    &currentState);
  TEST_ASSERT_TRUE(std::find(scope.begin(), scope.end(), kDeletedId) == scope.end());
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_orchestrator_skips_intra_selection_pair_when_co_moving);
  RUN_TEST(test_orchestrator_includes_stationary_selected_sibling_overlap_111955);
  RUN_TEST(test_evaluation_scope_excludes_cross_lane_notes);
  RUN_TEST(test_evaluation_scope_without_lane_includes_all_notes);
  RUN_TEST(test_evaluation_scope_keeps_hidden_source_lane_note_after_pitch_change);
  RUN_TEST(test_evaluation_scope_ignores_store_channel);
  RUN_TEST(test_projected_baseline_drops_cross_lane_keeps_mover);
  RUN_TEST(test_ensure_baseline_fills_scope_gap_enables_pitch_lane_interactions);
  RUN_TEST(test_add_note_same_pitch_overlap_complete_cover);
  RUN_TEST(test_delete_causing_restore_candidate_without_incoming);
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
  RUN_TEST(test_shared_edit_projection_origin_tail_shorten_only_004127);
  RUN_TEST(test_storage_overlap_analyze_tail_shorten_only_004127);
  RUN_TEST(test_storage_overlap_analyze_ltr_complete_cover_hides_short_target);
  RUN_TEST(test_analyze_ignores_session_moved_overlap_at_live_span_020105);
  RUN_TEST(test_analyze_ignores_session_moved_baseline_without_changed_overlap_id_020600);
  RUN_TEST(test_overlap_analyze_uses_current_span_not_stale_baseline_021939);
  RUN_TEST(test_overlay_committed_span_shortened_stub_ltr_overlap_181859);
  RUN_TEST(test_evaluation_scope_excludes_sealed_deleted_022849);
  return UNITY_END();
}
