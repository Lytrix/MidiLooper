//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/EditManager/EditApply.cpp"
#include "../../src/Loop/LoopPasses.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../test_support/MemoryMonitorNativeDeps.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/Loop.cpp"
#include "../test_support/LoopCaptureTestDeps.cpp"
#include "../test_support/NoteEditFocusTestDeps.cpp"
#include "../../src/EditManager/NoteEditSessionUndoStack.cpp"

#include "EditApply.h"
#include "EditPass.h"
#include "Loop.h"
#include "LoopPasses.h"
#include "LoopEventBuffer.h"
#include "EditSession.h"
#include "NoteEditSessionState.h"
#include "../test_support/NoteIdTestFixtures.h"
#include "../test_support/CommittedChunkIdTestHelpers.h"

namespace {

using namespace NoteIdTestFixtures;

RecordPass makeRecordPassWithNote(PassId id, uint32_t tick, uint8_t channel = 1) {
  resetNoteIdCounter();
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, tick, channel, 60, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(tick + 10, channel, 60, 0)));
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, committedChunkIds));
  RecordPass pass{};
  pass.id = id;
  pass.state = CapturePassState::Active;
  pass.committedChunkIds = std::move(committedChunkIds);
  return pass;
}

OverdubPass makeOverdubPassWithNote(PassId id, uint32_t tick, uint8_t pitch) {
  resetNoteIdCounter();
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, tick, 1, pitch, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(tick + 10, 1, pitch, 0)));
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, committedChunkIds));
  OverdubPass pass{};
  pass.id = id;
  pass.mergeSequence = 1;
  pass.state = CapturePassState::Active;
  pass.committedChunkIds = std::move(committedChunkIds);
  return pass;
}

int countNoteOns(const MidiEventVec& flat, uint8_t pitch) {
  int count = 0;
  for (const MidiEvent& evt : flat) {
    if (evt.isNoteOn() && evt.data.noteData.note == pitch) {
      ++count;
    }
  }
  return count;
}

RecordPass makeRecordPassWithTwoNotes(PassId id, uint32_t startA, uint32_t endA, uint32_t startB,
                                      uint32_t endB, uint8_t pitch) {
  resetNoteIdCounter();
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, startA, 1, pitch, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(endA, 1, pitch, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, startB, 1, pitch, 100, 2));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(endB, 1, pitch, 0)));
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, committedChunkIds));
  RecordPass pass{};
  pass.id = id;
  pass.state = CapturePassState::Active;
  pass.committedChunkIds = std::move(committedChunkIds);
  return pass;
}

static void appendFixtureNotePair(LoopEventStore& store, uint32_t onTick, uint32_t offTick,
                                  uint8_t channel, uint8_t pitch, NoteId noteId) {
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, onTick, channel, pitch, 100, noteId));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(offTick, channel, pitch, 0)));
}

RecordPass makeRecordPassWithUnidentifiedNote(PassId id, uint32_t start, uint32_t end, uint8_t pitch,
                                             uint8_t channel = 5) {
  LoopEventStore store;
  MidiEvent on = MidiEvent::NoteOn(start, channel, pitch, 100);
  on.noteId = kInvalidNoteId;
  TEST_ASSERT_TRUE(store.append(on));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(end, channel, pitch, 0)));
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, committedChunkIds));
  RecordPass pass{};
  pass.id = id;
  pass.state = CapturePassState::Active;
  pass.committedChunkIds = std::move(committedChunkIds);
  return pass;
}

NoteId noteIdAtPitchAndStart(const MidiEventVec& flat, uint8_t pitch, uint32_t startTick) {
  for (const MidiEvent& evt : flat) {
    if (evt.isNoteOn() && evt.data.noteData.note == pitch && evt.tick == startTick) {
      return evt.noteId;
    }
  }
  return kInvalidNoteId;
}

RecordPass makeRecordPassWithIdentifiedNote(PassId id, uint32_t start, uint32_t end, uint8_t pitch,
                                           NoteId noteId, uint8_t channel = 5) {
  resetNoteIdCounter();
  LoopEventStore store;
  appendFixtureNotePair(store, start, end, channel, pitch, noteId);
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, committedChunkIds));
  RecordPass pass{};
  pass.id = id;
  pass.state = CapturePassState::Active;
  pass.committedChunkIds = std::move(committedChunkIds);
  return pass;
}

RecordPass makeEditRecordFixtureRecordPass(PassId id) {
  resetNoteIdCounter();
  LoopEventStore store;
  appendFixtureNotePair(store, 8, 104, 1, 60, 1);
  appendFixtureNotePair(store, 201, 297, 1, 64, 2);
  appendFixtureNotePair(store, 392, 488, 1, 67, 3);
  appendFixtureNotePair(store, 584, 680, 1, 60, 4);
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, committedChunkIds));
  RecordPass pass{};
  pass.id = id;
  pass.state = CapturePassState::Active;
  pass.committedChunkIds = std::move(committedChunkIds);
  return pass;
}

RecordPass makeEditRecordFixtureRecordPassCh5(PassId id) {
  resetNoteIdCounter();
  LoopEventStore store;
  appendFixtureNotePair(store, 8, 104, 5, 60, 1);
  appendFixtureNotePair(store, 585, 680, 5, 60, 2);
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, committedChunkIds));
  RecordPass pass{};
  pass.id = id;
  pass.state = CapturePassState::Active;
  pass.committedChunkIds = std::move(committedChunkIds);
  return pass;
}

RecordPass makeLengthReplayLoopBoundaryRecordPass(PassId id) {
  resetNoteIdCounter();
  LoopEventStore store;
  appendFixtureNotePair(store, 0, 143, 1, 71, 1);
  appendFixtureNotePair(store, 720, 766, 1, 71, 2);
  appendFixtureNotePair(store, 1488, 1535, 1, 71, 3);
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 2256, 1, 71, 100, 14));
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, committedChunkIds));
  RecordPass pass{};
  pass.id = id;
  pass.state = CapturePassState::Active;
  pass.committedChunkIds = std::move(committedChunkIds);
  return pass;
}

RecordPass makeEditRecordFixtureRecordPassCh5_195830(PassId id) {
  resetNoteIdCounter();
  LoopEventStore store;
  appendFixtureNotePair(store, 40, 136, 5, 60, 1);
  appendFixtureNotePair(store, 232, 328, 5, 64, 2);
  appendFixtureNotePair(store, 424, 520, 5, 67, 3);
  appendFixtureNotePair(store, 616, 712, 5, 60, 4);
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, committedChunkIds));
  RecordPass pass{};
  pass.id = id;
  pass.state = CapturePassState::Active;
  pass.committedChunkIds = std::move(committedChunkIds);
  return pass;
}

EditPass makeDeleteRow(NoteId targetNoteId) {
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = EditActionType::Delete;
  row.propertyType = EditPropertyType::None;
  row.targetNoteId = targetNoteId;
  return row;
}

EditPass makeLengthRow(NoteId targetNoteId, uint32_t start, uint32_t end, uint32_t newEnd) {
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = EditActionType::Update;
  row.propertyType = EditPropertyType::Length;
  row.targetNoteId = targetNoteId;
  row.startTick = start;
  row.endTick = newEnd;
  return row;
}

EditPass makePitchRow(NoteId targetNoteId, uint32_t start, uint32_t end, uint8_t pitch) {
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = EditActionType::Update;
  row.propertyType = EditPropertyType::Pitch;
  row.targetNoteId = targetNoteId;
  row.startTick = start;
  row.endTick = end;
  row.pitch = pitch;
  return row;
}

EditPass makeNoteRangeRow(NoteId targetNoteId, uint32_t baseStart, uint32_t baseEnd,
                          uint32_t newStart, uint32_t newEnd) {
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = EditActionType::Update;
  row.propertyType = EditPropertyType::NoteRange;
  row.targetNoteId = targetNoteId;
  row.startTick = newStart;
  row.endTick = newEnd;
  return row;
}

void pushEditPassRow(LoopPasses& passes, EditPassId id, EditPass row) {
  row.id = id;
  row.editPassIndex = 0;
  row.state = EditPassState::Active;
  passes.editPasses.push_back(std::move(row));
}

void pushEditPassRows(LoopPasses& passes, EditPassId baseId, EditPassVec rows) {
  EditPassId id = baseId;
  for (EditPass row : rows) {
    row.id = id++;
    row.editPassIndex = 0;
    row.state = EditPassState::Active;
    passes.editPasses.push_back(std::move(row));
  }
}

template <typename Alloc>
int countMatching(const std::vector<MidiEvent, Alloc>& flat, bool wantOn, uint8_t pitch,
                  uint32_t tick) {
  int n = 0;
  for (const MidiEvent& e : flat) {
    const bool isOn = e.isNoteOn();
    if (isOn == wantOn && e.data.noteData.note == pitch && e.tick == tick) {
      n++;
    }
  }
  return n;
}

}  // namespace

// Regression: a note lengthened to share its end tick with another same-pitch note must
// not bleed a later pitch edit onto the neighbor (loop-end stretch from an orphaned note-on).
void test_change_pitch_on_lengthened_note_keeps_same_pitch_neighbor() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopPasses passes;
  // M0: on@8 off@104, P0: on@584 off@680, both pitch 60.
  passes.recordPass = makeRecordPassWithTwoNotes(1, 8, 104, 584, 680, 60);



  pushEditPassRow(passes, 1, makeLengthRow(1, 8, 104, 680));
  pushEditPassRow(passes, 2, makePitchRow(1, 8, 680, 67));

  constexpr uint32_t kLoopLength = 1536;
  MidiEventVec flat;
  passes.materializeToEventVector(flat, kLoopLength);
  const int movingOn = findNoteOnById(flat, 1);
  TEST_ASSERT_TRUE(movingOn >= 0);
  TEST_ASSERT_EQUAL_UINT32(8u, flat[static_cast<size_t>(movingOn)].tick);
  TEST_ASSERT_EQUAL_UINT8(67, flat[static_cast<size_t>(movingOn)].data.noteData.note);

  const std::vector<NoteUtils::DisplayNote> notes =
      NoteUtils::reconstructNotes(flat, kLoopLength, false);
  bool foundM0 = false;
  bool foundP0 = false;
  for (const NoteUtils::DisplayNote& dn : notes) {
    if (dn.noteId == 1) {
      foundM0 = true;
      TEST_ASSERT_EQUAL_UINT8(67, dn.note);
      TEST_ASSERT_EQUAL_UINT32(8u, dn.startTick);
      TEST_ASSERT_TRUE(dn.endTick > 104u);
    }
    if (dn.noteId == 2) {
      foundP0 = true;
      TEST_ASSERT_EQUAL_UINT8(60, dn.note);
      TEST_ASSERT_EQUAL_UINT32(584u, dn.startTick);
    }
  }
  TEST_ASSERT_TRUE(foundM0);
  TEST_ASSERT_TRUE(foundP0);
  TEST_ASSERT_EQUAL(1, countMatching(flat, true, 60, 584));
  TEST_ASSERT_EQUAL(0, countMatching(flat, true, 60, 8));
}

// Regression: after M0 moves to beat 2, lengthening it across P0 (beat 3, pitch 60) must not
// steal P0's note-off or stretch P0 to loop end (shared release tick at step 14 is OK).
void test_lengthen_after_move_keeps_p0_fixture_gate() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopPasses passes;
  // M0 @ step 4 (192-288 after +1 beat move); P0 @ step 12 (576-672, 2-step gate).
  passes.recordPass = makeRecordPassWithTwoNotes(1, 192, 288, 576, 672, 60);



  pushEditPassRow(passes, 1, makeLengthRow(1, 192, 288, 672));

  MidiEventVec flat;
  passes.materializeToEventVector(flat);

  TEST_ASSERT_EQUAL(1, countMatching(flat, true, 60, 192));
  TEST_ASSERT_EQUAL(1, countMatching(flat, true, 60, 576));
  TEST_ASSERT_EQUAL(0, countMatching(flat, true, 60, 8));
  int offAt672 = 0;
  for (const MidiEvent& e : flat) {
    if (e.isNoteOff() && e.data.noteData.note == 60 && e.tick == 672) {
      offAt672++;
    }
  }
  TEST_ASSERT_EQUAL(2, offAt672);
}

// Simulates commitAllPendingNoteEditActions rematerialize after move+lengthen (reselect path).
void test_change_length_rematerialize_keeps_p0_off_not_loop_end() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  constexpr uint32_t kLoopLength = 1536;
  LoopPasses passes;
  passes.recordPass = makeRecordPassWithTwoNotes(1, 192, 288, 576, 672, 60);


  pushEditPassRow(passes, 1, makeLengthRow(1, 192, 288, 672));

  MidiEventVec flat;
  passes.materializeToEventVector(flat, kLoopLength);

  const std::vector<NoteUtils::DisplayNote> notes =
      NoteUtils::reconstructNotes(flat, kLoopLength, true);
  bool foundP0 = false;
  for (const NoteUtils::DisplayNote& n : notes) {
    if (n.note == 60 && n.startTick == 576) {
      foundP0 = true;
      TEST_ASSERT_EQUAL(672, n.endTick);
      TEST_ASSERT_TRUE(n.endTick < kLoopLength - 1);
    }
    if (n.note == 60 && n.startTick == 576 && n.endTick >= kLoopLength - 2) {
      TEST_FAIL_MESSAGE("P0 stretched to loop end after ChangeLength rematerialize");
    }
  }
  TEST_ASSERT_TRUE(foundP0);
}

// session_20260813_203140: ChangeLength stub 14 2256–2304 must not wrap and
// shorten same-pitch neighbors to 2255, or delete the note at tick 0.
void test_length_replay_loop_boundary_does_not_shorten_same_pitch_neighbors_203140() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  constexpr uint32_t kLoopLength = 2304;
  constexpr uint8_t kPitch = 71;
  LoopPasses passes;
  passes.recordPass = makeLengthReplayLoopBoundaryRecordPass(1);
  pushEditPassRow(passes, 1, makeLengthRow(14, 2256, 2304, kLoopLength));

  MidiEventVec flat;
  passes.materializeToEventVector(flat, kLoopLength);

  TEST_ASSERT_EQUAL(0, countMatching(flat, false, kPitch, kLoopLength));

  const std::vector<NoteUtils::DisplayNote> notes =
      NoteUtils::reconstructNotes(flat, kLoopLength, false);
  bool foundTickZero = false;
  bool foundA = false;
  bool foundB = false;
  for (const NoteUtils::DisplayNote& note : notes) {
    TEST_ASSERT_FALSE(note.note == kPitch && note.endTick == 2255u);
    if (note.noteId == 1) {
      foundTickZero = true;
      TEST_ASSERT_EQUAL_UINT8(kPitch, note.note);
      TEST_ASSERT_EQUAL_UINT32(0u, note.startTick);
      TEST_ASSERT_EQUAL_UINT32(143u, note.endTick);
    }
    if (note.noteId == 2) {
      foundA = true;
      TEST_ASSERT_EQUAL_UINT8(kPitch, note.note);
      TEST_ASSERT_EQUAL_UINT32(720u, note.startTick);
      TEST_ASSERT_EQUAL_UINT32(766u, note.endTick);
    }
    if (note.noteId == 3) {
      foundB = true;
      TEST_ASSERT_EQUAL_UINT8(kPitch, note.note);
      TEST_ASSERT_EQUAL_UINT32(1488u, note.startTick);
      TEST_ASSERT_EQUAL_UINT32(1535u, note.endTick);
    }
  }
  TEST_ASSERT_TRUE(foundTickZero);
  TEST_ASSERT_TRUE(foundA);
  TEST_ASSERT_TRUE(foundB);
}

// session_20260813_210821: the host was moved to 801-1568 and pitched to 36 in an earlier
// session, so its pass list already holds a NoteRange row. The overlap shorten committed at
// 31.988 (Length 10 -> 1055) must survive replay instead of being rewritten to the earlier
// row's end, which reselect at 32.833 read back as the original 767 ticks.
void test_length_row_after_earlier_move_row_on_same_note_seals_210821() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  constexpr uint32_t kLoopLength = 2304;
  constexpr uint8_t kPitch = 36;
  LoopPasses passes;
  // note 1 = mover (18 in the capture), note 2 = host (10 in the capture), both pitch 26.
  passes.recordPass = makeRecordPassWithTwoNotes(1, 480, 575, 768, 863, 26);

  pushEditPassRows(passes, 1,
                   {makeNoteRangeRow(2, 768, 863, 801, 1568), makePitchRow(2, 801, 1568, kPitch),
                    makeLengthRow(2, 801, 1568, 1055), makeNoteRangeRow(1, 480, 575, 1056, 1151),
                    makePitchRow(1, 1056, 1151, kPitch)});

  MidiEventVec flat;
  passes.materializeToEventVector(flat, kLoopLength);

  const std::vector<NoteUtils::DisplayNote> notes =
      NoteUtils::reconstructNotes(flat, kLoopLength, false);
  bool foundHost = false;
  bool foundMover = false;
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.noteId == 2) {
      foundHost = true;
      TEST_ASSERT_EQUAL_UINT8(kPitch, note.note);
      TEST_ASSERT_EQUAL_UINT32(801u, note.startTick);
      TEST_ASSERT_EQUAL_UINT32(1055u, note.endTick);
    }
    if (note.noteId == 1) {
      foundMover = true;
      TEST_ASSERT_EQUAL_UINT8(kPitch, note.note);
      TEST_ASSERT_EQUAL_UINT32(1056u, note.startTick);
      TEST_ASSERT_EQUAL_UINT32(1151u, note.endTick);
    }
  }
  TEST_ASSERT_TRUE(foundHost);
  TEST_ASSERT_TRUE(foundMover);
}

// session_20260813_210945: the mover was committed to 960-1104 at 99.273, then the host was
// hidden and the mover moved to 768-912 at 115.986. The second NoteRange row must win instead
// of being rewritten back to the first row's span.
void test_second_move_row_on_same_note_wins_over_earlier_span_210945() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  constexpr uint32_t kLoopLength = 2304;
  constexpr uint8_t kPitch = 36;
  LoopPasses passes;
  // note 1 = mover (77 in the capture), note 2 = host (10 in the capture).
  passes.recordPass = makeRecordPassWithTwoNotes(1, 480, 575, 801, 1568, 26);

  pushEditPassRows(passes, 1,
                   {makeNoteRangeRow(1, 480, 575, 960, 1104), makePitchRow(1, 960, 1104, kPitch),
                    makeDeleteRow(2), makeNoteRangeRow(1, 960, 1104, 768, 912)});

  MidiEventVec flat;
  passes.materializeToEventVector(flat, kLoopLength);

  const std::vector<NoteUtils::DisplayNote> notes =
      NoteUtils::reconstructNotes(flat, kLoopLength, false);
  bool foundMover = false;
  for (const NoteUtils::DisplayNote& note : notes) {
    TEST_ASSERT_TRUE(note.noteId != 2);
    if (note.noteId == 1) {
      foundMover = true;
      TEST_ASSERT_EQUAL_UINT8(kPitch, note.note);
      TEST_ASSERT_EQUAL_UINT32(768u, note.startTick);
      TEST_ASSERT_EQUAL_UINT32(912u, note.endTick);
    }
  }
  TEST_ASSERT_TRUE(foundMover);
}

// session_20260816_145518 @23.402: deselect saved NoteRange 312-504 + Pitch 45 targeting
// noteId 280. Replay still had M24 888-1032. Pin apply when the store NoteId matches.
void test_145518_note_range_pitch_replay_moves_when_store_id_matches() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  constexpr uint32_t kLoopLength = 3072;
  constexpr NoteId kMoverId = 280;
  LoopPasses passes;
  passes.recordPass = makeRecordPassWithIdentifiedNote(1, 888, 1032, 24, kMoverId);

  pushEditPassRows(passes, 1,
                   {makeNoteRangeRow(kMoverId, 888, 1032, 312, 504),
                    makePitchRow(kMoverId, 312, 504, 45)});

  MidiEventVec flat;
  passes.materializeToEventVector(flat, kLoopLength);
  const std::vector<NoteUtils::DisplayNote> notes =
      NoteUtils::reconstructNotes(flat, kLoopLength, false);

  bool foundMoved = false;
  for (const NoteUtils::DisplayNote& note : notes) {
    TEST_ASSERT_FALSE(note.note == 24 && note.startTick == 888);
    if (note.noteId == kMoverId) {
      foundMoved = true;
      TEST_ASSERT_EQUAL_UINT8(45, note.note);
      TEST_ASSERT_EQUAL_UINT32(312u, note.startTick);
      TEST_ASSERT_EQUAL_UINT32(504u, note.endTick);
    }
  }
  TEST_ASSERT_TRUE(foundMoved);
}

// Same rows targeting 280 when the capture note at 888 is a different NoteId. Replay must
// leave M24 888-1032 — the 145518 take_only / replay_flat match.
void test_145518_note_range_pitch_replay_leaves_home_when_store_id_differs() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  constexpr uint32_t kLoopLength = 3072;
  constexpr NoteId kStoreId = 24;
  constexpr NoteId kRowTargetId = 280;
  LoopPasses passes;
  passes.recordPass = makeRecordPassWithIdentifiedNote(1, 888, 1032, 24, kStoreId);

  pushEditPassRows(passes, 1,
                   {makeNoteRangeRow(kRowTargetId, 888, 1032, 312, 504),
                    makePitchRow(kRowTargetId, 312, 504, 45)});

  MidiEventVec flat;
  passes.materializeToEventVector(flat, kLoopLength);
  const std::vector<NoteUtils::DisplayNote> notes =
      NoteUtils::reconstructNotes(flat, kLoopLength, false);

  bool foundHome = false;
  for (const NoteUtils::DisplayNote& note : notes) {
    TEST_ASSERT_FALSE(note.note == 45 && note.startTick == 312);
    if (note.noteId == kStoreId) {
      foundHome = true;
      TEST_ASSERT_EQUAL_UINT8(24, note.note);
      TEST_ASSERT_EQUAL_UINT32(888u, note.startTick);
      TEST_ASSERT_EQUAL_UINT32(1032u, note.endTick);
    }
  }
  TEST_ASSERT_TRUE(foundHome);
}

// session_20260816_145518: openNoteEditSession rematerializes, then
// assignMissingNoteIdsInStore(editSession.store) — session copy only. commitEditAction
// rematerializes from capture chunks, which never received that id. Rows target 280; home stays.
void test_145518_open_assigned_note_id_missing_from_pass_rematerialize() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  constexpr uint32_t kLoopLength = 3072;
  constexpr uint8_t kPitch = 24;
  constexpr uint32_t kHomeStart = 888;
  constexpr uint32_t kHomeEnd = 1032;
  constexpr NoteId kAssignedId = 280;

  Loop loop;
  loop.loopLengthTicks = kLoopLength;
  loop.nextNoteId_ = kAssignedId;
  loop.passes.recordPass = makeRecordPassWithUnidentifiedNote(1, kHomeStart, kHomeEnd, kPitch);

  MidiEventVec passFlat;
  loop.passes.materializeToEventVector(passFlat, kLoopLength);
  TEST_ASSERT_EQUAL(kInvalidNoteId, noteIdAtPitchAndStart(passFlat, kPitch, kHomeStart));

  LoopEventStore sessionStore;
  loop.rematerializeEditView(sessionStore);
  loop.assignMissingNoteIdsInStore(sessionStore);
  MidiEventVec sessionFlat;
  sessionStore.copyEventsTo(sessionFlat);
  TEST_ASSERT_EQUAL(kAssignedId, noteIdAtPitchAndStart(sessionFlat, kPitch, kHomeStart));

  passFlat.clear();
  loop.passes.materializeToEventVector(passFlat, kLoopLength);
  TEST_ASSERT_EQUAL(kInvalidNoteId, noteIdAtPitchAndStart(passFlat, kPitch, kHomeStart));
  TEST_ASSERT_EQUAL(-1, findNoteOnById(passFlat, kAssignedId));

  pushEditPassRows(loop.passes, 1,
                   {makeNoteRangeRow(kAssignedId, kHomeStart, kHomeEnd, 312, 504),
                    makePitchRow(kAssignedId, 312, 504, 45)});
  passFlat.clear();
  loop.passes.materializeToEventVector(passFlat, kLoopLength);
  const std::vector<NoteUtils::DisplayNote> notes =
      NoteUtils::reconstructNotes(passFlat, kLoopLength, false);
  bool foundHome = false;
  for (const NoteUtils::DisplayNote& note : notes) {
    TEST_ASSERT_FALSE(note.note == 45 && note.startTick == 312);
    if (note.note == kPitch && note.startTick == kHomeStart) {
      foundHome = true;
      TEST_ASSERT_EQUAL_UINT32(kHomeEnd, note.endTick);
    }
  }
  TEST_ASSERT_TRUE(foundHome);
}

// Same unidentified capture note, but assign on committed chunks before rematerialize.
// Session and pass flats share 280; NoteRange+Pitch then moves the home (145518 Layer B fix).
void test_145518_assign_on_committed_passes_makes_rematerialize_find_id() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  constexpr uint32_t kLoopLength = 3072;
  constexpr uint8_t kPitch = 24;
  constexpr uint32_t kHomeStart = 888;
  constexpr uint32_t kHomeEnd = 1032;
  constexpr NoteId kAssignedId = 280;

  Loop loop;
  loop.loopLengthTicks = kLoopLength;
  loop.nextNoteId_ = kAssignedId;
  loop.passes.recordPass = makeRecordPassWithUnidentifiedNote(1, kHomeStart, kHomeEnd, kPitch);

  loop.assignMissingNoteIdsInCommittedCapturePasses();

  MidiEventVec passFlat;
  loop.passes.materializeToEventVector(passFlat, kLoopLength);
  TEST_ASSERT_EQUAL(kAssignedId, noteIdAtPitchAndStart(passFlat, kPitch, kHomeStart));

  LoopEventStore sessionStore;
  loop.rematerializeEditView(sessionStore);
  MidiEventVec sessionFlat;
  sessionStore.copyEventsTo(sessionFlat);
  TEST_ASSERT_EQUAL(kAssignedId, noteIdAtPitchAndStart(sessionFlat, kPitch, kHomeStart));

  pushEditPassRows(loop.passes, 1,
                   {makeNoteRangeRow(kAssignedId, kHomeStart, kHomeEnd, 312, 504),
                    makePitchRow(kAssignedId, 312, 504, 45)});
  passFlat.clear();
  loop.passes.materializeToEventVector(passFlat, kLoopLength);
  const std::vector<NoteUtils::DisplayNote> notes =
      NoteUtils::reconstructNotes(passFlat, kLoopLength, false);
  bool foundMoved = false;
  for (const NoteUtils::DisplayNote& note : notes) {
    TEST_ASSERT_FALSE(note.note == kPitch && note.startTick == kHomeStart);
    if (note.noteId == kAssignedId) {
      foundMoved = true;
      TEST_ASSERT_EQUAL_UINT8(45, note.note);
      TEST_ASSERT_EQUAL_UINT32(312u, note.startTick);
      TEST_ASSERT_EQUAL_UINT32(504u, note.endTick);
    }
  }
  TEST_ASSERT_TRUE(foundMoved);
}

MidiEvent noteOnWithId(uint32_t tick, uint8_t channel, uint8_t pitch, uint8_t velocity,
                       NoteId noteId) {
  MidiEvent evt = MidiEvent::NoteOn(tick, channel, pitch, velocity);
  evt.noteId = noteId;
  return evt;
}

// Nested same-pitch pair: On(A)@100, On(B)@150, Off(B)@180, Off(A)@300.
// findNoteOffForOnIndex must pair the outer note by LIFO, not break on the inner on.
MidiEventVec makeNestedSamePitchPair() {
  MidiEventVec events;
  events.push_back(noteOnWithId(100, 1, 60, 100, 1));
  events.push_back(noteOnWithId(150, 1, 60, 100, 2));
  events.push_back(MidiEvent::NoteOff(180, 1, 60, 0));
  events.push_back(MidiEvent::NoteOff(300, 1, 60, 0));
  return events;
}

void assertInnerNoteUnchanged(const MidiEventVec& events) {
  const int innerOn = findNoteOnById(events, 2);
  TEST_ASSERT_TRUE(innerOn >= 0);
  TEST_ASSERT_EQUAL_UINT32(150u, events[static_cast<size_t>(innerOn)].tick);
  TEST_ASSERT_EQUAL_UINT8(60, events[static_cast<size_t>(innerOn)].data.noteData.note);
  TEST_ASSERT_EQUAL(1, countMatching(events, false, 60, 180));
}

void test_nested_same_pitch_outer_apply_helpers_use_lifo_off() {
  constexpr uint32_t kLoopLength = 768;
  constexpr NoteId kOuterId = 1;

  {
    MidiEventVec events = makeNestedSamePitchPair();
    applyNoteEditPass(events, makeDeleteRow(kOuterId), kLoopLength);
    TEST_ASSERT_EQUAL(-1, findNoteOnById(events, kOuterId));
    TEST_ASSERT_EQUAL(0, countMatching(events, false, 60, 300));
    assertInnerNoteUnchanged(events);
  }

  {
    MidiEventVec events = makeNestedSamePitchPair();
    applyNoteEditPass(events, makeNoteRangeRow(kOuterId, 100, 300, 200, 400), kLoopLength);
    const int outerOn = findNoteOnById(events, kOuterId);
    TEST_ASSERT_TRUE(outerOn >= 0);
    TEST_ASSERT_EQUAL_UINT32(200u, events[static_cast<size_t>(outerOn)].tick);
    TEST_ASSERT_EQUAL(1, countMatching(events, false, 60, 400));
    TEST_ASSERT_EQUAL(0, countMatching(events, false, 60, 300));
    assertInnerNoteUnchanged(events);
  }

  {
    MidiEventVec events = makeNestedSamePitchPair();
    applyNoteEditPass(events, makePitchRow(kOuterId, 100, 300, 67), kLoopLength);
    const int outerOn = findNoteOnById(events, kOuterId);
    TEST_ASSERT_TRUE(outerOn >= 0);
    TEST_ASSERT_EQUAL_UINT8(67, events[static_cast<size_t>(outerOn)].data.noteData.note);
    TEST_ASSERT_EQUAL(1, countMatching(events, false, 67, 300));
    TEST_ASSERT_EQUAL(0, countMatching(events, false, 60, 300));
    assertInnerNoteUnchanged(events);
  }

  {
    MidiEventVec events = makeNestedSamePitchPair();
    applyNoteEditPass(events, makeLengthRow(kOuterId, 100, 300, 250), kLoopLength);
    const int outerOn = findNoteOnById(events, kOuterId);
    TEST_ASSERT_TRUE(outerOn >= 0);
    TEST_ASSERT_EQUAL_UINT32(100u, events[static_cast<size_t>(outerOn)].tick);
    TEST_ASSERT_EQUAL(1, countMatching(events, false, 60, 250));
    TEST_ASSERT_EQUAL(0, countMatching(events, false, 60, 300));
    int outerOffs = 0;
    for (const MidiEvent& evt : events) {
      if (evt.isNoteOff() && evt.data.noteData.note == 60 && evt.tick != 180) {
        ++outerOffs;
      }
    }
    TEST_ASSERT_EQUAL(1, outerOffs);
    assertInnerNoteUnchanged(events);
  }
}

// Regression for the HITL overlap round-trip: M0 lengthened to 681 overlaps P0 (585..682)
// without nesting. A pitch edit on M0 must not relabel P0's note-off (tick 682).
void test_change_pitch_on_overlapping_note_keeps_neighbor_endtick() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopPasses passes;
  passes.recordPass = makeRecordPassWithTwoNotes(1, 9, 105, 585, 682, 60);



  pushEditPassRow(passes, 1, makeLengthRow(1, 9, 105, 681));
  pushEditPassRow(passes, 2, makePitchRow(1, 9, 681, 67));

  constexpr uint32_t kLoopLength = 1536;
  MidiEventVec flat;
  passes.materializeToEventVector(flat, kLoopLength);

  const int movingOn = findNoteOnById(flat, 1);
  TEST_ASSERT_TRUE(movingOn >= 0);
  TEST_ASSERT_EQUAL_UINT8(67, flat[static_cast<size_t>(movingOn)].data.noteData.note);
  const std::vector<NoteUtils::DisplayNote> notes =
      NoteUtils::reconstructNotes(flat, kLoopLength, false);
  bool foundM0 = false;
  bool foundP0 = false;
  for (const NoteUtils::DisplayNote& dn : notes) {
    if (dn.noteId == 1) {
      foundM0 = true;
      TEST_ASSERT_EQUAL_UINT8(67, dn.note);
      TEST_ASSERT_TRUE(dn.endTick > 105u);
    }
    if (dn.noteId == 2) {
      foundP0 = true;
      TEST_ASSERT_EQUAL_UINT8(60, dn.note);
      TEST_ASSERT_EQUAL_UINT32(585u, dn.startTick);
    }
  }
  TEST_ASSERT_TRUE(foundM0);
  TEST_ASSERT_TRUE(foundP0);
}

void test_apply_edits_delete_note() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopPasses passes;
  passes.recordPass = makeRecordPassWithNote(1, 10);

  pushEditPassRow(passes, 1, makeDeleteRow(1));

  LoopEventStore out;
  passes.materialize(out);
  TEST_ASSERT_TRUE(out.empty());
}

void test_save_edit_appends_without_collapsing_takes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.passes.recordPass = makeRecordPassWithNote(1, 10);
  {
    LoopEventStore odStore;
    TEST_ASSERT_TRUE(storeAppendNoteOn(odStore, 100, 1, 60, 100, 2));
    TEST_ASSERT_TRUE(odStore.append(MidiEvent::NoteOff(110, 1, 60, 0)));
    CommittedChunkIdList committedChunkIds;
    TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(odStore, committedChunkIds));
    OverdubPass overdub{};
    overdub.id = 2;
    overdub.mergeSequence = 1;
    overdub.state = CapturePassState::Active;
    overdub.committedChunkIds = std::move(committedChunkIds);
    loop.passes.overdubPasses.push_back(std::move(overdub));
  }
  loop.nextPassId_ = 3;

  const EditPassId id = loop.saveNoteEditPass(0, makeDeleteRow(1));
  TEST_ASSERT_EQUAL(3u, id);
  TEST_ASSERT_EQUAL(2u, loop.passes.capturePassCount());
  TEST_ASSERT_EQUAL(1u, loop.passes.editPasses.size());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPassType::Note),
                          static_cast<uint8_t>(loop.passes.editPasses[0].passType));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditActionType::Delete),
                          static_cast<uint8_t>(loop.passes.editPasses[0].actionType));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPropertyType::None),
                          static_cast<uint8_t>(loop.passes.editPasses[0].propertyType));

  MidiEventVec flat;
  loop.passes.materializeToEventVector(flat);
  TEST_ASSERT_EQUAL(2u, flat.size());
}

void test_save_note_edit_pass_sets_scoped_action_property_mapping() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;

  const EditPassId delId = loop.saveNoteEditPass(2, makeDeleteRow(1));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, delId);
  const EditPass& delPass = loop.passes.editPasses.back();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPassType::Note),
                          static_cast<uint8_t>(delPass.passType));
  TEST_ASSERT_EQUAL_UINT8(2u, delPass.editPassIndex);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditActionType::Delete),
                          static_cast<uint8_t>(delPass.actionType));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPropertyType::None),
                          static_cast<uint8_t>(delPass.propertyType));

  const EditPassId pitchId = loop.saveNoteEditPass(3, makePitchRow(1, 10, 20, 67));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, pitchId);
  const EditPass& pitchPass = loop.passes.editPasses.back();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditActionType::Update),
                          static_cast<uint8_t>(pitchPass.actionType));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPropertyType::Pitch),
                          static_cast<uint8_t>(pitchPass.propertyType));

  const EditPassId lengthId = loop.saveNoteEditPass(4, makeLengthRow(1, 10, 20, 28));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, lengthId);
  const EditPass& lengthPass = loop.passes.editPasses.back();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditActionType::Update),
                          static_cast<uint8_t>(lengthPass.actionType));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPropertyType::Length),
                          static_cast<uint8_t>(lengthPass.propertyType));

  const EditPassId moveStartId =
      loop.saveNoteEditPass(5, makeNoteRangeRow(1, 10, 20, 14, 24));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, moveStartId);
  const EditPass& moveStartPass = loop.passes.editPasses.back();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPropertyType::NoteRange),
                          static_cast<uint8_t>(moveStartPass.propertyType));

  const EditPassId moveEndId =
      loop.saveNoteEditPass(6, makeNoteRangeRow(1, 10, 20, 10, 24));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, moveEndId);
  const EditPass& moveEndPass = loop.passes.editPasses.back();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPropertyType::NoteRange),
                          static_cast<uint8_t>(moveEndPass.propertyType));
}

void test_edit_pass_state_toggle_does_not_append_edit_actions() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;

  const EditPassId id = loop.saveNoteEditPass(0, makeDeleteRow(1));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, id);
  TEST_ASSERT_EQUAL(1u, loop.passes.editPasses.size());
  const EditActionType beforeAction = loop.passes.editPasses[0].actionType;

  loop.disableEditPasses(EditPassIdList{id});
  TEST_ASSERT_EQUAL(1u, loop.passes.editPasses.size());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPassState::Disabled),
                          static_cast<uint8_t>(loop.passes.editPasses[0].state));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(beforeAction),
                          static_cast<uint8_t>(loop.passes.editPasses[0].actionType));

  loop.passes.editPasses[0].state = EditPassState::Active;
  TEST_ASSERT_EQUAL(1u, loop.passes.editPasses.size());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPassState::Active),
                          static_cast<uint8_t>(loop.passes.editPasses[0].state));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(beforeAction),
                          static_cast<uint8_t>(loop.passes.editPasses[0].actionType));
}

void test_disable_edits_restores_take_only_view() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.passes.recordPass = makeRecordPassWithNote(1, 10);
  const EditPassId id = loop.saveNoteEditPass(0, makeDeleteRow(1));
  loop.disableEditPasses(EditPassIdList{id});
  MidiEventVec flat;
  loop.passes.materializeToEventVector(flat);
  TEST_ASSERT_EQUAL(2u, flat.size());
}

void test_reset_take_timeline_clears_stale_edits() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.passes.recordPass = makeRecordPassWithNote(1, 8);
  loop.nextPassId_ = 2;

  loop.saveNoteEditPass(0, makePitchRow(1, 8, 18, 67));
  TEST_ASSERT_EQUAL(1u, loop.passes.editPasses.size());

  MidiEventVec withStaleEdit;
  loop.passes.materializeToEventVector(withStaleEdit);
  TEST_ASSERT_EQUAL(67, withStaleEdit[0].data.noteData.note);

  loop.resetPassTimeline();
  TEST_ASSERT_TRUE(loop.passes.editPasses.empty());
  TEST_ASSERT_EQUAL(1u, loop.nextPassId_);
  TEST_ASSERT_FALSE(loop.isEditStateDirty());

  loop.passes.recordPass = makeRecordPassWithNote(1, 8);
  MidiEventVec fresh;
  loop.passes.materializeToEventVector(fresh);
  TEST_ASSERT_EQUAL(2u, fresh.size());
  TEST_ASSERT_EQUAL(60, fresh[0].data.noteData.note);
  TEST_ASSERT_EQUAL(60, fresh[1].data.noteData.note);
}

void test_note_edit_session_undo_stack() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  NoteEditSessionUndoStack stack;
  SessionUndoEntry entry;
  entry.editRows.push_back({});
  TEST_ASSERT_TRUE(stack.pushEntry(entry));
  TEST_ASSERT_TRUE(stack.canUndo());
  const SessionUndoEntry* target = stack.popUndoTarget();
  TEST_ASSERT_NOT_NULL(target);
}

void test_add_note_rematerialize_session_store() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.passes.recordPass = makeRecordPassWithNote(1, 10);
  loop.nextPassId_ = 2;

  EditPass add{};
  add.actionType = EditActionType::Create;
  add.addedEvents.push_back(noteOnWithNoteId(48, 5, 60, 80, 10));
  add.addedEvents.push_back(MidiEvent::NoteOff(72, 5, 60, 0));
  const EditPassId id = loop.saveNoteEditPass(0, add);
  TEST_ASSERT_EQUAL(2u, id);

  CowLoopEventStore session;
  loop.rematerializeEditView(session.mutStore());
  session.discardEventsCache();

  const auto& flat = session.readEvents();
  TEST_ASSERT_EQUAL(4u, flat.size());
  TEST_ASSERT_EQUAL(1, countMatching(flat, true, 60, 10));
  TEST_ASSERT_EQUAL(1, countMatching(flat, true, 60, 48));
}

void test_move_note_uses_track_channel() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopPasses passes;
  passes.recordPass = makeRecordPassWithNote(1, 10);
  passes.recordPass.committedChunkIds.clear();
  LoopEventStore store;
  resetNoteIdCounter();
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 10, 5, 60, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(106, 5, 60, 0)));
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, committedChunkIds));
  passes.recordPass.committedChunkIds = std::move(committedChunkIds);

  pushEditPassRow(passes, 1, makeNoteRangeRow(1, 10, 106, 58, 154));

  MidiEventVec flat;
  passes.materializeToEventVector(flat);
  TEST_ASSERT_EQUAL(1, countMatching(flat, true, 60, 58));
  TEST_ASSERT_EQUAL(0, countMatching(flat, true, 60, 10));

  LoopPasses badPasses = passes;
  badPasses.editPasses.clear();
  EditPass badRow = makeNoteRangeRow(1, 10, 106, 58, 154);
  badRow.targetNoteId = 99;
  pushEditPassRow(badPasses, 1, badRow);
  MidiEventVec flatBad;
  badPasses.materializeToEventVector(flatBad);
  TEST_ASSERT_EQUAL(1, countMatching(flatBad, true, 60, 10));
  TEST_ASSERT_EQUAL(0, countMatching(flatBad, true, 60, 58));
}

void test_lengthen_commit_rematerialize_hitl_fixture() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  constexpr uint32_t kLoopLength = 1536;
  Loop loop;
  loop.loopLengthTicks = kLoopLength;
  loop.passes.recordPass = makeEditRecordFixtureRecordPassCh5(1);
  loop.passes.recordPass.id = 1;
  {
    LoopEventStore store;
    resetNoteIdCounter();
    appendFixtureNotePair(store, 8, 104, 5, 60, 1);
    appendFixtureNotePair(store, 200, 296, 5, 64, 2);
    appendFixtureNotePair(store, 392, 488, 5, 67, 3);
    appendFixtureNotePair(store, 585, 680, 5, 60, 4);
    CommittedChunkIdList committedChunkIds;
    TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, committedChunkIds));
    loop.passes.recordPass.committedChunkIds = std::move(committedChunkIds);
  }

  const EditPassId id = loop.saveNoteEditPass(0, makeLengthRow(1, 8, 104, 680));
  TEST_ASSERT_EQUAL(1u, id);

  CowLoopEventStore session;
  loop.rematerializeEditView(session.mutStore());
  session.discardEventsCache();

  const NoteUtils::DisplayNoteVec notes =
      NoteUtils::reconstructDisplayNotes(session.readEvents(), kLoopLength, false);
  bool foundM0 = false;
  for (const NoteUtils::DisplayNote& n : notes) {
    if (n.note == 60 && n.startTick == 8) {
      foundM0 = true;
      TEST_ASSERT_EQUAL(680, n.endTick);
    }
  }
  TEST_ASSERT_TRUE(foundM0);
}

// HITL 195830: M0@40-136 lengthen to 712 with P0@616-712 (channel 5, loop 1536).
// Mirrors commitAllPendingNoteEditActions ChangeLength-only commit + rematerialize.
void test_change_length_rematerialize_hitl_195830_ticks() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  constexpr uint32_t kLoopLength = 1536;
  Loop loop;
  loop.loopLengthTicks = kLoopLength;
  loop.passes.recordPass = makeEditRecordFixtureRecordPassCh5_195830(1);

  const EditPassId id = loop.saveNoteEditPass(0, makeLengthRow(1, 40, 136, 712));
  TEST_ASSERT_EQUAL(1u, id);

  // commitEditAction: discard live flat, replay takes+edits, load session store (matches firmware).
  CowLoopEventStore session;
  session.discardEventsCache();
  MidiEventVec loopMidiEventsFromPasses;
  loop.passes.materializeToEventVector(loopMidiEventsFromPasses, kLoopLength);
  session.mutStore().loadFromEvents(loopMidiEventsFromPasses);
  session.discardEventsCache();

  const NoteUtils::DisplayNoteVec notes =
      NoteUtils::reconstructDisplayNotes(session.readEvents(), kLoopLength, false);
  bool foundM0Home = false;
  bool foundP0 = false;
  for (const NoteUtils::DisplayNote& n : notes) {
    if (n.note == 60 && n.startTick == 40) {
      foundM0Home = true;
      TEST_ASSERT_EQUAL(712, n.endTick);
    }
    if (n.note == 60 && n.startTick == 616) {
      foundP0 = true;
      TEST_ASSERT_EQUAL(712, n.endTick);
    }
  }
  TEST_ASSERT_TRUE(foundM0Home);
  TEST_ASSERT_TRUE(foundP0);

  int offAt712 = 0;
  for (const MidiEvent& e : session.readEvents()) {
    if (e.isNoteOff() && e.channel == 5 && e.data.noteData.note == 60 && e.tick == 712) {
      offAt712++;
    }
  }
  TEST_ASSERT_EQUAL(2, offAt712);
}

// PR4 / B1: lengthen → move-over (hidden P0) → pitch → home replay via ordered pre-commit edits.
void test_overlap_round_trip_replay_lengthen_delete_pitch() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  constexpr uint32_t kLoopLength = 1536;
  LoopPasses passes;
  passes.recordPass = makeEditRecordFixtureRecordPass(1);


  pushEditPassRow(passes, 1, makeLengthRow(1, 8, 104, 680));
  pushEditPassRow(passes, 2, makeDeleteRow(4));
  pushEditPassRow(passes, 3, makePitchRow(1, 8, 680, 67));

  MidiEventVec flat;
  passes.materializeToEventVector(flat, kLoopLength);

  TEST_ASSERT_EQUAL(1, countMatching(flat, true, 67, 8));
  TEST_ASSERT_EQUAL(1, countMatching(flat, false, 67, 680));
  TEST_ASSERT_EQUAL(0, countMatching(flat, true, 60, 584));
  TEST_ASSERT_EQUAL(1, countMatching(flat, true, 67, 392));
  TEST_ASSERT_EQUAL(1, countMatching(flat, false, 67, 488));
}

// Pre-commit order: overlap DeleteNote + ChangeLength before MoveNote / ChangePitch.
void test_pre_commit_order_overlap_changes_before_move_and_pitch() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  constexpr uint32_t kLoopLength = 1536;
  LoopPasses passes;
  passes.recordPass = makeRecordPassWithTwoNotes(1, 8, 680, 584, 680, 60);

  pushEditPassRow(passes, 1, makeLengthRow(2, 584, 680, 495));
  pushEditPassRow(passes, 2, makeNoteRangeRow(1, 8, 680, 496, 1168));
  pushEditPassRow(passes, 3, makePitchRow(1, 8, 680, 67));

  MidiEventVec flatOrdered;
  passes.materializeToEventVector(flatOrdered, kLoopLength);
  TEST_ASSERT_EQUAL(1, countMatching(flatOrdered, true, 67, 496));
  TEST_ASSERT_EQUAL(1, countMatching(flatOrdered, false, 67, 1168));
  TEST_ASSERT_EQUAL(1, countMatching(flatOrdered, true, 60, 584));
  TEST_ASSERT_EQUAL(1, countMatching(flatOrdered, false, 60, 495));

  LoopPasses passes2;
  passes2.recordPass = makeRecordPassWithTwoNotes(1, 8, 680, 584, 680, 60);
  pushEditPassRow(passes2, 1, makeNoteRangeRow(1, 8, 680, 496, 1168));
  pushEditPassRow(passes2, 2, makePitchRow(1, 8, 680, 67));
  MidiEventVec flatWrong;
  passes2.materializeToEventVector(flatWrong, kLoopLength);
  TEST_ASSERT_EQUAL(1, countMatching(flatWrong, true, 67, 496));
  TEST_ASSERT_EQUAL(0, countMatching(flatWrong, false, 60, 495));
  TEST_ASSERT_EQUAL(1, countMatching(flatWrong, false, 60, 680));
}

void test_pre_commit_delete_before_mover_change_length() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopPasses passes;
  passes.recordPass = makeRecordPassWithTwoNotes(1, 8, 104, 584, 680, 60);

  pushEditPassRow(passes, 1, makeDeleteRow(2));
  pushEditPassRow(passes, 2, makeLengthRow(1, 8, 104, 680));
  MidiEventVec flat;
  passes.materializeToEventVector(flat);
  TEST_ASSERT_EQUAL(1, countMatching(flat, true, 60, 8));
  TEST_ASSERT_EQUAL(1, countMatching(flat, false, 60, 680));
  TEST_ASSERT_EQUAL(0, countMatching(flat, true, 60, 584));
}

void test_playback_merge_includes_active_edit_passes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.passes.recordPass = makeRecordPassWithNote(1, 10);
  loop.loopLengthTicks = 768;
  loop.nextPassId_ = 10;

  const EditPassId lengthId = loop.saveNoteEditPass(0, makeLengthRow(1, 10, 20, 28));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, lengthId);

  auto noteOffTick = [](const MidiEventVec& flat) -> uint32_t {
    for (const MidiEvent& e : flat) {
      if (e.isNoteOff() && e.data.noteData.note == 60) {
        return e.tick;
      }
    }
    return UINT32_MAX;
  };

  MidiEventVec captureOnly;
  loop.mergeActiveCapturePasses(captureOnly);
  TEST_ASSERT_EQUAL_UINT32(20u, noteOffTick(captureOnly));

  MidiEventVec materializedPlayback;
  loop.mergeMaterializedPassesWithCapture(materializedPlayback);
  TEST_ASSERT_EQUAL_UINT32(28u, noteOffTick(materializedPlayback));
}

void test_global_undo_overdub_pass_added_disables_overdub() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = 768;
  loop.passes.recordPass = makeRecordPassWithNote(1, 10);
  loop.passes.overdubPasses.push_back(makeOverdubPassWithNote(2, 100, 72));
  loop.nextPassId_ = 3;

  MidiEventVec withOverdub;
  loop.passes.materializeToEventVector(withOverdub, loop.loopLengthTicks);
  TEST_ASSERT_EQUAL(1, countNoteOns(withOverdub, 60));
  TEST_ASSERT_EQUAL(1, countNoteOns(withOverdub, 72));

  TEST_ASSERT_TRUE(loop.setCapturePassState(2, CapturePassState::Disabled));

  MidiEventVec afterUndo;
  loop.passes.materializeToEventVector(afterUndo, loop.loopLengthTicks);
  TEST_ASSERT_EQUAL(1, countNoteOns(afterUndo, 60));
  TEST_ASSERT_EQUAL(0, countNoteOns(afterUndo, 72));
}

void test_global_undo_note_edit_pass_closed_disables_edit_rows() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = 768;
  loop.passes.recordPass = makeRecordPassWithNote(1, 10);
  loop.nextPassId_ = 2;
  const EditPassId editId = loop.saveNoteEditPass(0, makePitchRow(1, 10, 20, 67));

  MidiEventVec withEdit;
  loop.passes.materializeToEventVector(withEdit, loop.loopLengthTicks);
  TEST_ASSERT_EQUAL(1, countNoteOns(withEdit, 67));

  loop.disableEditPasses(EditPassIdList{editId});

  MidiEventVec afterUndo;
  loop.passes.materializeToEventVector(afterUndo, loop.loopLengthTicks);
  TEST_ASSERT_EQUAL(1, countNoteOns(afterUndo, 60));
  TEST_ASSERT_EQUAL(0, countNoteOns(afterUndo, 67));
}

void test_global_undo_three_step_restores_record_baseline() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = 768;
  loop.passes.recordPass = makeRecordPassWithNote(1, 10);
  loop.nextPassId_ = 2;

  const EditPassId preOverdubEdit =
      loop.saveNoteEditPass(0, makePitchRow(1, 10, 20, 67));
  loop.passes.overdubPasses.push_back(makeOverdubPassWithNote(3, 100, 72));
  loop.nextPassId_ = 4;

  EditPass add{};
  add.passType = EditPassType::Note;
  add.actionType = EditActionType::Create;
  add.addedEvents.push_back(MidiEvent::NoteOn(200, 1, 48, 100));
  add.addedEvents.push_back(MidiEvent::NoteOff(210, 1, 48, 0));
  const EditPassId postOverdubEdit = loop.saveNoteEditPass(1, add);

  loop.disableEditPasses(EditPassIdList{postOverdubEdit});
  TEST_ASSERT_TRUE(loop.setCapturePassState(3, CapturePassState::Disabled));
  loop.disableEditPasses(EditPassIdList{preOverdubEdit});

  MidiEventVec baseline;
  loop.passes.materializeToEventVector(baseline, loop.loopLengthTicks);
  TEST_ASSERT_EQUAL(1, countNoteOns(baseline, 60));
  TEST_ASSERT_EQUAL(0, countNoteOns(baseline, 67));
  TEST_ASSERT_EQUAL(0, countNoteOns(baseline, 72));
  TEST_ASSERT_EQUAL(0, countNoteOns(baseline, 48));
}

void test_session_undo_move_back_insert_before_save_note_edit_pass() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 768;
  loop.passes.recordPass = makeEditRecordFixtureRecordPass(1);
  loop.nextPassId_ = 2;

  CowLoopEventStore session;
  loop.passes.materializeToEventVector(session.mutEvents(), loop.loopLengthTicks);
  session.syncEventsToStore();

  NoteEditFocus focus;
  focus.active = true;
  focus.commitBaseline = {60, 100, 8, 104};
  focus.last = focus.commitBaseline;
  focus.movingNoteId = 1;
  focus.movingNoteRange = {8, 104};

  const SessionUndoEntry entry =
      buildSessionUndoEntry(focus, EditorSelection{}, session.readEvents(), 1,
                            loop.loopLengthTicks, EditPassIdList{});

  focus.last.startTick = 496;
  focus.last.endTick = 1168;
  noteEditFocusApplyMoveEnd(focus, focus.last.startTick, focus.last.endTick);
  MidiEventVec& flat = session.mutEvents();
  for (MidiEvent& evt : flat) {
    if (evt.isNoteOn() && evt.channel == 1 && evt.data.noteData.note == 60 && evt.tick == 8) {
      evt.tick = 496;
    }
    if (evt.isNoteOff() && evt.channel == 1 && evt.data.noteData.note == 60 && evt.tick == 104) {
      evt.tick = 1168;
    }
  }
  session.syncEventsToStore();
  TEST_ASSERT_EQUAL(1, countMatching(session.readEvents(), true, 60, 496));

  SessionUndoEntry undoEntry = entry;
  SessionUndoEntry redoPayload =
      buildSessionUndoEntry(focus, EditorSelection{}, session.readEvents(), 1,
                            loop.loopLengthTicks, EditPassIdList{});
  undoEntry.redoEditRows = std::move(redoPayload.editRows);
  undoEntry.redoFocus = std::move(redoPayload.focus);
  undoEntry.redoSelection = redoPayload.selection;
  undoEntry.hasRedoPayload = true;

  applySessionUndoEntry(loop, session, undoEntry, loop.loopLengthTicks, 5, EditPassIdList{});
  TEST_ASSERT_EQUAL(1, countMatching(session.readEvents(), true, 60, 8));
  TEST_ASSERT_EQUAL(0, countMatching(session.readEvents(), true, 60, 496));

  applySessionRedoEntry(loop, session, undoEntry, loop.loopLengthTicks, 5, EditPassIdList{});
  TEST_ASSERT_EQUAL(1, countMatching(session.readEvents(), true, 60, 496));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_change_pitch_on_lengthened_note_keeps_same_pitch_neighbor);
  RUN_TEST(test_lengthen_after_move_keeps_p0_fixture_gate);
  RUN_TEST(test_change_length_rematerialize_keeps_p0_off_not_loop_end);
  RUN_TEST(test_length_replay_loop_boundary_does_not_shorten_same_pitch_neighbors_203140);
  RUN_TEST(test_length_row_after_earlier_move_row_on_same_note_seals_210821);
  RUN_TEST(test_second_move_row_on_same_note_wins_over_earlier_span_210945);
  RUN_TEST(test_145518_note_range_pitch_replay_moves_when_store_id_matches);
  RUN_TEST(test_145518_note_range_pitch_replay_leaves_home_when_store_id_differs);
  RUN_TEST(test_145518_open_assigned_note_id_missing_from_pass_rematerialize);
  RUN_TEST(test_145518_assign_on_committed_passes_makes_rematerialize_find_id);
  RUN_TEST(test_nested_same_pitch_outer_apply_helpers_use_lifo_off);
  RUN_TEST(test_change_pitch_on_overlapping_note_keeps_neighbor_endtick);
  RUN_TEST(test_apply_edits_delete_note);
  RUN_TEST(test_save_edit_appends_without_collapsing_takes);
  RUN_TEST(test_save_note_edit_pass_sets_scoped_action_property_mapping);
  RUN_TEST(test_edit_pass_state_toggle_does_not_append_edit_actions);
  RUN_TEST(test_disable_edits_restores_take_only_view);
  RUN_TEST(test_reset_take_timeline_clears_stale_edits);
  RUN_TEST(test_note_edit_session_undo_stack);
  RUN_TEST(test_add_note_rematerialize_session_store);
  RUN_TEST(test_move_note_uses_track_channel);
  RUN_TEST(test_lengthen_commit_rematerialize_hitl_fixture);
  RUN_TEST(test_change_length_rematerialize_hitl_195830_ticks);
  RUN_TEST(test_overlap_round_trip_replay_lengthen_delete_pitch);
  RUN_TEST(test_pre_commit_order_overlap_changes_before_move_and_pitch);
  RUN_TEST(test_pre_commit_delete_before_mover_change_length);
  RUN_TEST(test_playback_merge_includes_active_edit_passes);
  RUN_TEST(test_global_undo_overdub_pass_added_disables_overdub);
  RUN_TEST(test_global_undo_note_edit_pass_closed_disables_edit_rows);
  RUN_TEST(test_global_undo_three_step_restores_record_baseline);
  RUN_TEST(test_session_undo_move_back_insert_before_save_note_edit_pass);
  return UNITY_END();
}
