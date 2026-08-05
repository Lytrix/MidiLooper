//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/NoteEditSessionState.cpp"

#include "NoteEditSessionState.h"
#include "MidiEvent.h"
#include "LoopPasses.h"

void test_advance_encoder_cycle_order() {
  TEST_ASSERT_EQUAL(static_cast<int>(NoteEditKind::Move),
                    static_cast<int>(advanceEncoderCycleKind(NoteEditKind::Select)));
  TEST_ASSERT_EQUAL(static_cast<int>(NoteEditKind::Pitch),
                    static_cast<int>(advanceEncoderCycleKind(NoteEditKind::Move)));
  TEST_ASSERT_EQUAL(static_cast<int>(NoteEditKind::Length),
                    static_cast<int>(advanceEncoderCycleKind(NoteEditKind::Pitch)));
  TEST_ASSERT_EQUAL(static_cast<int>(NoteEditKind::Select),
                    static_cast<int>(advanceEncoderCycleKind(NoteEditKind::Length)));
}

void test_fader_anchor_first_cycle_press() {
  TEST_ASSERT_EQUAL(static_cast<int>(NoteEditKind::Move),
                    static_cast<int>(resolveEncoderCyclePress(NoteEditKind::Move, true)));
  TEST_ASSERT_EQUAL(static_cast<int>(NoteEditKind::Pitch),
                    static_cast<int>(resolveEncoderCyclePress(NoteEditKind::Move, false)));
}

void test_geometry_kind_detection() {
  TEST_ASSERT_TRUE(isGeometryEditKind(NoteEditKind::Move));
  TEST_ASSERT_TRUE(isGeometryEditKind(NoteEditKind::Add));
  TEST_ASSERT_FALSE(isGeometryEditKind(NoteEditKind::Select));
}

void test_geometry_undo_push_matrix() {
  TEST_ASSERT_TRUE(shouldPushGeometryKindUndo(NoteEditKind::Select, NoteEditKind::Move));
  TEST_ASSERT_FALSE(shouldPushGeometryKindUndo(NoteEditKind::Move, NoteEditKind::Move));
  TEST_ASSERT_TRUE(shouldPushGeometryKindUndo(NoteEditKind::Move, NoteEditKind::Pitch));
  TEST_ASSERT_TRUE(shouldPushGeometryKindUndo(NoteEditKind::Add, NoteEditKind::Move));
  TEST_ASSERT_FALSE(shouldPushGeometryKindUndo(NoteEditKind::Select, NoteEditKind::Select));
}

void test_should_not_cycle_when_overlay_inactive() {
  TEST_ASSERT_FALSE(shouldCycleNoteEditTypeOnShortPress(false));
}

void test_should_cycle_when_overlay_active() {
  TEST_ASSERT_TRUE(shouldCycleNoteEditTypeOnShortPress(true));
}

void test_reset_geometry_undo_on_note_target_change() {
  EditorSelection prior{};
  prior.primaryNote = 1;
  prior.primaryNote = 1;
  constexpr NoteId noteB = 2;
  TEST_ASSERT_TRUE(shouldResetGeometryKindUndoOnSelectChange(prior, noteB));
  TEST_ASSERT_FALSE(shouldResetGeometryKindUndoOnSelectChange(prior, prior.primaryNote));
  TEST_ASSERT_TRUE(shouldResetGeometryKindUndoOnSelectChange(prior, kInvalidNoteId));
  EditorSelection none{};
  TEST_ASSERT_FALSE(shouldResetGeometryKindUndoOnSelectChange(none, noteB));
}

void test_empty_step_deselect_is_selection_target_change() {
  EditorSelection prior{};
  prior.primaryNote = 36;
  prior.selectedTick = 1248;
  prior.selectedNotes.push_back(36);

  TEST_ASSERT_TRUE(editorSelectionTargetChanged(prior, 1248, kInvalidNoteId));
}

void test_entity_id_invalid_sentinels() {
  TEST_ASSERT_EQUAL(0u, kInvalidNoteId);
  TEST_ASSERT_EQUAL(UINT32_MAX, kInvalidTrackId);
  TEST_ASSERT_EQUAL(kInvalidLoopId, kInvalidTrackId);
}

void test_move_after_reselect_pushes_fresh_geometry_undo() {
  TEST_ASSERT_FALSE(shouldPushGeometryKindUndo(NoteEditKind::Move, NoteEditKind::Move));
  TEST_ASSERT_TRUE(shouldPushGeometryKindUndo(NoteEditKind::Select, NoteEditKind::Move));
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_advance_encoder_cycle_order);
  RUN_TEST(test_fader_anchor_first_cycle_press);
  RUN_TEST(test_geometry_kind_detection);
  RUN_TEST(test_geometry_undo_push_matrix);
  RUN_TEST(test_should_not_cycle_when_overlay_inactive);
  RUN_TEST(test_should_cycle_when_overlay_active);
  RUN_TEST(test_reset_geometry_undo_on_note_target_change);
  RUN_TEST(test_empty_step_deselect_is_selection_target_change);
  RUN_TEST(test_entity_id_invalid_sentinels);
  RUN_TEST(test_move_after_reselect_pushes_fresh_geometry_undo);
  return UNITY_END();
}
