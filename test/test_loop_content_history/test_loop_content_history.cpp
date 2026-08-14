//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// DEC-035 Stage 1 — content-only undo-unit derivation audit.

#include <unity.h>
#include <vector>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/Utils/IntervalProjection.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/EditManager/EditApply.cpp"
#include "../../src/Loop/LoopPasses.cpp"
#include "../../src/Loop/LoopContentHistory.cpp"
#include "../test_support/MemoryMonitorNativeDeps.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"

#include "../test_support/CommittedChunkIdTestHelpers.h"
#include "EditPass.h"
#include "LoopContentHistory.h"
#include "LoopPasses.h"
#include "PendingNoteChange.h"

CommittedChunkIdList makeNoteChunk(uint32_t tick, uint8_t pitch) {
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(tick, 1, pitch, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(tick + 24, 1, pitch, 0)));
  CommittedChunkIdList ids;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, ids));
  return ids;
}

EditPass makeNoteEdit(PassId id, uint8_t editPassIndex, NoteId target, uint32_t start, uint32_t end) {
  EditPass row;
  row.id = id;
  row.passType = EditPassType::Note;
  row.editPassIndex = editPassIndex;
  row.actionType = EditActionType::Update;
  row.propertyType = EditPropertyType::Length;
  row.state = EditPassState::Active;
  row.targetNoteId = target;
  row.startTick = start;
  row.endTick = end;
  row.pitch = 60;
  row.velocity = 100;
  return row;
}

void collectTicks(const LoopPasses& passes, uint32_t loopLength, std::vector<uint32_t>& ticks) {
  MidiEventVec events;
  passes.materializeToEventVector(events, loopLength);
  ticks.clear();
  for (const MidiEvent& event : events) {
    ticks.push_back(event.tick);
  }
}

void test_inventory_kinds_not_all_in_content() {
  TEST_ASSERT_EQUAL(255, kOverdubCompanionEditPassIndex);
  TEST_ASSERT_TRUE(static_cast<uint8_t>(ContentUndoUnitKind::RecordPassAdded) == 0);
  TEST_ASSERT_TRUE(static_cast<uint8_t>(ContentUndoUnitKind::OverdubPassAdded) == 1);
  TEST_ASSERT_TRUE(static_cast<uint8_t>(ContentUndoUnitKind::NoteEditPassClosed) == 2);
  TEST_ASSERT_TRUE(static_cast<uint8_t>(ContentUndoUnitKind::ControlChangeEditPassClosed) == 3);
  TEST_ASSERT_TRUE(static_cast<uint8_t>(ContentUndoUnitKind::LoopBoundaryChange) == 4);
}

void test_abc_record_overdub_edit_three_units() {
  LoopPasses passes;
  passes.recordPass.id = 1;
  passes.recordPass.state = CapturePassState::Active;
  OverdubPass overdub;
  overdub.id = 2;
  overdub.mergeSequence = 1;
  overdub.state = CapturePassState::Active;
  passes.overdubPasses.push_back(overdub);
  passes.editPasses.push_back(makeNoteEdit(3, 0, 1, 0, 96));

  std::vector<ContentUndoUnit> units;
  deriveContentUndoUnits(passes, units);
  TEST_ASSERT_EQUAL(3u, units.size());
  TEST_ASSERT_EQUAL(static_cast<int>(ContentUndoUnitKind::RecordPassAdded),
                    static_cast<int>(units[0].kind));
  TEST_ASSERT_EQUAL(1u, units[0].primaryPassId);
  TEST_ASSERT_EQUAL(static_cast<int>(ContentUndoUnitKind::OverdubPassAdded),
                    static_cast<int>(units[1].kind));
  TEST_ASSERT_EQUAL(2u, units[1].primaryPassId);
  TEST_ASSERT_TRUE(units[1].editPassIds.empty());
  TEST_ASSERT_EQUAL(static_cast<int>(ContentUndoUnitKind::NoteEditPassClosed),
                    static_cast<int>(units[2].kind));
  TEST_ASSERT_EQUAL(1u, units[2].editPassIds.size());
  TEST_ASSERT_EQUAL(3u, units[2].editPassIds[0]);
}

void test_grouped_edit_rows_share_edit_pass_index() {
  LoopPasses passes;
  passes.recordPass.id = 1;
  passes.editPasses.push_back(makeNoteEdit(2, 0, 1, 0, 96));
  passes.editPasses.push_back(makeNoteEdit(3, 0, 2, 96, 192));
  passes.editPasses.push_back(makeNoteEdit(4, 1, 3, 192, 288));

  std::vector<ContentUndoUnit> units;
  deriveContentUndoUnits(passes, units);
  TEST_ASSERT_EQUAL(3u, units.size());
  TEST_ASSERT_EQUAL(2u, units[1].editPassIds.size());
  TEST_ASSERT_EQUAL(2u, units[1].editPassIds[0]);
  TEST_ASSERT_EQUAL(3u, units[1].editPassIds[1]);
  TEST_ASSERT_EQUAL(1u, units[2].editPassIds.size());
  TEST_ASSERT_EQUAL(4u, units[2].editPassIds[0]);
}

void test_overdub_companions_follow_overdub_by_reserved_index() {
  LoopPasses passes;
  passes.recordPass.id = 1;
  OverdubPass overdub;
  overdub.id = 2;
  overdub.mergeSequence = 1;
  passes.overdubPasses.push_back(overdub);
  passes.editPasses.push_back(makeNoteEdit(3, kOverdubCompanionEditPassIndex, 1, 0, 48));
  passes.editPasses.push_back(makeNoteEdit(4, kOverdubCompanionEditPassIndex, 2, 48, 96));
  passes.editPasses.push_back(makeNoteEdit(5, 0, 3, 96, 192));

  std::vector<ContentUndoUnit> units;
  deriveContentUndoUnits(passes, units);
  TEST_ASSERT_EQUAL(3u, units.size());
  TEST_ASSERT_EQUAL(static_cast<int>(ContentUndoUnitKind::OverdubPassAdded),
                    static_cast<int>(units[1].kind));
  TEST_ASSERT_EQUAL(2u, units[1].editPassIds.size());
  TEST_ASSERT_EQUAL(3u, units[1].editPassIds[0]);
  TEST_ASSERT_EQUAL(4u, units[1].editPassIds[1]);
  TEST_ASSERT_EQUAL(static_cast<int>(ContentUndoUnitKind::NoteEditPassClosed),
                    static_cast<int>(units[2].kind));
  TEST_ASSERT_EQUAL(5u, units[2].editPassIds[0]);
}

void test_undo_undo_new_work_drops_suffix_units() {
  LoopPasses passes;
  passes.recordPass.id = 1;
  OverdubPass first;
  first.id = 2;
  first.mergeSequence = 1;
  first.state = CapturePassState::Disabled;
  passes.overdubPasses.push_back(first);
  passes.editPasses.push_back(makeNoteEdit(3, 0, 1, 0, 96));
  passes.editPasses.back().state = EditPassState::Disabled;
  OverdubPass replacement;
  replacement.id = 4;
  replacement.mergeSequence = 2;
  replacement.state = CapturePassState::Active;
  passes.overdubPasses.push_back(replacement);

  LoopPasses prefix;
  prefix.recordPass.id = 1;
  prefix.overdubPasses.push_back(replacement);

  std::vector<ContentUndoUnit> allUnits;
  deriveContentUndoUnits(passes, allUnits);
  TEST_ASSERT_EQUAL(4u, allUnits.size());

  std::vector<ContentUndoUnit> prefixUnits;
  deriveContentUndoUnits(prefix, prefixUnits);
  TEST_ASSERT_EQUAL(2u, prefixUnits.size());
  TEST_ASSERT_EQUAL(1u, prefixUnits[0].primaryPassId);
  TEST_ASSERT_EQUAL(4u, prefixUnits[1].primaryPassId);
}

void test_active_prefix_materialize_matches_omitted_suffix() {
  constexpr uint32_t kLoopLen = 1536;
  LoopPasses full;
  full.recordPass.id = 1;
  full.recordPass.state = CapturePassState::Active;
  full.recordPass.committedChunkIds = makeNoteChunk(0, 60);
  OverdubPass overdub;
  overdub.id = 2;
  overdub.mergeSequence = 1;
  overdub.state = CapturePassState::Disabled;
  overdub.committedChunkIds = makeNoteChunk(96, 62);
  full.overdubPasses.push_back(overdub);

  LoopPasses prefix;
  prefix.recordPass = full.recordPass;

  std::vector<uint32_t> fullTicks;
  std::vector<uint32_t> prefixTicks;
  collectTicks(full, kLoopLen, fullTicks);
  collectTicks(prefix, kLoopLen, prefixTicks);
  TEST_ASSERT_EQUAL(fullTicks.size(), prefixTicks.size());
  TEST_ASSERT_EQUAL(2u, prefixTicks.size());
  TEST_ASSERT_EQUAL(0u, prefixTicks[0]);
  TEST_ASSERT_EQUAL(24u, prefixTicks[1]);
}

void test_session_reused_edit_pass_index_collapses_two_undo_units() {
  LoopPasses passes;
  passes.recordPass.id = 1;
  passes.editPasses.push_back(makeNoteEdit(2, 0, 1, 0, 96));
  passes.editPasses.push_back(makeNoteEdit(3, 0, 2, 96, 192));

  std::vector<ContentUndoUnit> units;
  deriveContentUndoUnits(passes, units);
  TEST_ASSERT_EQUAL(2u, units.size());
  TEST_ASSERT_EQUAL_MESSAGE(2u, units[1].editPassIds.size(),
                            "same-index adjacent edit rows are one committed noteEditPass");
}

void test_loop_boundary_change_absent_without_geometry_record() {
  LoopPasses passes;
  passes.recordPass.id = 1;
  std::vector<ContentUndoUnit> units;
  deriveContentUndoUnits(passes, units);
  TEST_ASSERT_EQUAL(1u, units.size());
  TEST_ASSERT_EQUAL(static_cast<int>(ContentUndoUnitKind::RecordPassAdded),
                    static_cast<int>(units[0].kind));
}

void test_loop_geometry_is_one_undo_unit() {
  LoopPasses passes;
  passes.recordPass.id = 1;
  LoopGeometry geometry;
  geometry.id = 2;
  geometry.loopStartTick = 0;
  geometry.loopLengthTicks = 1536;
  geometry.startLoopTick = 0;
  geometry.state = LoopGeometryState::Active;
  passes.loopGeometries.push_back(geometry);

  std::vector<ContentUndoUnit> units;
  deriveContentUndoUnits(passes, units);
  TEST_ASSERT_EQUAL(2u, units.size());
  TEST_ASSERT_EQUAL(static_cast<int>(ContentUndoUnitKind::RecordPassAdded),
                    static_cast<int>(units[0].kind));
  TEST_ASSERT_EQUAL(static_cast<int>(ContentUndoUnitKind::LoopBoundaryChange),
                    static_cast<int>(units[1].kind));
  TEST_ASSERT_EQUAL(2u, units[1].primaryPassId);
}

void test_effective_units_omit_disabled_records() {
  LoopPasses passes;
  passes.recordPass.id = 1;
  passes.recordPass.state = CapturePassState::Active;
  OverdubPass disabled;
  disabled.id = 2;
  disabled.state = CapturePassState::Disabled;
  passes.overdubPasses.push_back(disabled);
  OverdubPass active;
  active.id = 4;
  active.state = CapturePassState::Active;
  passes.overdubPasses.push_back(active);
  passes.editPasses.push_back(makeNoteEdit(3, 0, 1, 0, 96));
  passes.editPasses.back().state = EditPassState::Disabled;

  std::vector<ContentUndoUnit> allUnits;
  deriveContentUndoUnits(passes, allUnits);
  TEST_ASSERT_EQUAL(4u, allUnits.size());

  std::vector<ContentUndoUnit> effective;
  deriveEffectiveContentUndoUnits(passes, effective);
  TEST_ASSERT_EQUAL(2u, effective.size());
  TEST_ASSERT_EQUAL(1u, effective[0].primaryPassId);
  TEST_ASSERT_EQUAL(4u, effective[1].primaryPassId);
}

void test_build_content_undo_entries_at_tip() {
  LoopPasses passes;
  passes.recordPass.id = 1;
  passes.recordPass.state = CapturePassState::Active;
  OverdubPass overdub;
  overdub.id = 2;
  overdub.state = CapturePassState::Active;
  passes.overdubPasses.push_back(overdub);
  passes.editPasses.push_back(makeNoteEdit(3, 0, 1, 0, 96));
  LoopGeometry geometry;
  geometry.id = 4;
  geometry.loopStartTick = 0;
  geometry.loopLengthTicks = 1536;
  geometry.beforeLoopLengthTicks = 768;
  geometry.state = LoopGeometryState::Active;
  passes.loopGeometries.push_back(geometry);

  UndoEntryVec entries;
  buildContentUndoEntries(passes, 1, 99, entries);
  TEST_ASSERT_EQUAL(4u, entries.size());
  TEST_ASSERT_EQUAL(static_cast<int>(UndoEntryKind::RecordPassAdded),
                    static_cast<int>(entries[0].kind));
  TEST_ASSERT_EQUAL(static_cast<int>(UndoEntryKind::OverdubPassAdded),
                    static_cast<int>(entries[1].kind));
  TEST_ASSERT_EQUAL(static_cast<int>(UndoEntryKind::NoteEditPassClosed),
                    static_cast<int>(entries[2].kind));
  TEST_ASSERT_EQUAL(1u, entries[2].editPassIds.size());
  TEST_ASSERT_EQUAL(3u, entries[2].editPassIds[0]);
  TEST_ASSERT_EQUAL(static_cast<int>(UndoEntryKind::LoopBoundaryChange),
                    static_cast<int>(entries[3].kind));
  TEST_ASSERT_EQUAL(4u, entries[3].passId);
  TEST_ASSERT_EQUAL(768u, entries[3].beforeLoopLengthTicks);
  TEST_ASSERT_EQUAL(1536u, entries[3].afterLoopLengthTicks);
  TEST_ASSERT_EQUAL(1u, entries[0].slotIndex);
  TEST_ASSERT_EQUAL(99u, entries[0].loopId);
}

void test_walk_effective_units_to_empty() {
  LoopPasses passes;
  passes.recordPass.id = 1;
  passes.recordPass.state = CapturePassState::Active;
  OverdubPass overdub;
  overdub.id = 2;
  overdub.state = CapturePassState::Active;
  passes.overdubPasses.push_back(overdub);
  passes.editPasses.push_back(makeNoteEdit(3, 0, 1, 0, 96));

  std::vector<ContentUndoUnit> units;
  deriveEffectiveContentUndoUnits(passes, units);
  TEST_ASSERT_EQUAL(3u, units.size());

  passes.editPasses[0].state = EditPassState::Disabled;
  deriveEffectiveContentUndoUnits(passes, units);
  TEST_ASSERT_EQUAL(2u, units.size());

  passes.overdubPasses[0].state = CapturePassState::Disabled;
  deriveEffectiveContentUndoUnits(passes, units);
  TEST_ASSERT_EQUAL(1u, units.size());
  TEST_ASSERT_EQUAL(1u, units[0].primaryPassId);

  passes.recordPass.state = CapturePassState::Disabled;
  deriveEffectiveContentUndoUnits(passes, units);
  TEST_ASSERT_EQUAL(0u, units.size());
}

void test_two_loops_derive_independently() {
  LoopPasses slot0;
  slot0.recordPass.id = 1;
  OverdubPass overdub;
  overdub.id = 2;
  slot0.overdubPasses.push_back(overdub);

  LoopPasses slot1;
  slot1.recordPass.id = 10;
  slot1.editPasses.push_back(makeNoteEdit(11, 0, 1, 0, 48));
  slot1.editPasses.push_back(makeNoteEdit(12, 0, 2, 48, 96));

  std::vector<ContentUndoUnit> units0;
  std::vector<ContentUndoUnit> units1;
  deriveContentUndoUnits(slot0, units0);
  deriveContentUndoUnits(slot1, units1);
  TEST_ASSERT_EQUAL(2u, units0.size());
  TEST_ASSERT_EQUAL(2u, units1.size());
  TEST_ASSERT_EQUAL(1u, units0[0].primaryPassId);
  TEST_ASSERT_EQUAL(10u, units1[0].primaryPassId);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_inventory_kinds_not_all_in_content);
  RUN_TEST(test_abc_record_overdub_edit_three_units);
  RUN_TEST(test_grouped_edit_rows_share_edit_pass_index);
  RUN_TEST(test_overdub_companions_follow_overdub_by_reserved_index);
  RUN_TEST(test_undo_undo_new_work_drops_suffix_units);
  RUN_TEST(test_active_prefix_materialize_matches_omitted_suffix);
  RUN_TEST(test_session_reused_edit_pass_index_collapses_two_undo_units);
  RUN_TEST(test_loop_boundary_change_absent_without_geometry_record);
  RUN_TEST(test_loop_geometry_is_one_undo_unit);
  RUN_TEST(test_effective_units_omit_disabled_records);
  RUN_TEST(test_build_content_undo_entries_at_tip);
  RUN_TEST(test_walk_effective_units_to_empty);
  RUN_TEST(test_two_loops_derive_independently);
  return UNITY_END();
}
