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

  const EditSessionInteractionsByTarget emptyGrouped;
  const auto targets = determineConstrainedGeometryTargetNoteIds(emptyGrouped, baseline, liveStore,
                                                                 1, kLoopLength);
  TEST_ASSERT_EQUAL(1, static_cast<int>(targets.size()));
  TEST_ASSERT_EQUAL_UINT32(kTarget, targets[0]);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_resolve_complete_hide_precedence_over_shorten);
  RUN_TEST(test_resolve_restrictive_shorten_combine_takes_min_end);
  RUN_TEST(test_resolve_minimum_note_length_hide);
  RUN_TEST(test_resolve_baseline_equivalent_when_causing_gone);
  RUN_TEST(test_group_rebuild_when_one_causing_removed);
  RUN_TEST(test_determine_constrained_targets_includes_restore_candidate);
  return UNITY_END();
}
