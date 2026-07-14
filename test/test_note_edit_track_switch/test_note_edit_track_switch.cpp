//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Regression: NOTE_EDIT active on track A, user switches to track B — display must use
// track B loop length and session store (session_20260705_213626.log: 56 notes @ 18432
// from departing track 5 session until edit mode cycle).

#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/EditApply.cpp"
#include "../../src/LoopPasses.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../test_support/MemoryMonitorNativeDeps.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/Loop.cpp"
#include "../test_support/LoopCaptureTestDeps.cpp"
#include "../../src/NoteEditFocus.cpp"

#include "Loop.h"
#include "LoopEventBuffer.h"
#include "NoteEditFocus.h"
#include "../test_support/NoteIdTestFixtures.h"
#include "Utils/NoteUtils.h"

namespace {

using namespace NoteIdTestFixtures;

constexpr uint32_t kShortLoopLength = 1536u;
constexpr uint32_t kLongLoopLength = 18432u;
constexpr uint8_t kChannel = 5;

void appendNotePair(LoopEventStore& store, uint32_t onTick, uint32_t offTick, uint8_t pitch,
                    NoteId noteId) {
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, onTick, kChannel, pitch, 100, noteId));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(offTick, kChannel, pitch, 0)));
}

RecordPass makeRecordPassWithNoteCount(uint32_t loopLength, unsigned noteCount) {
  resetNoteIdCounter();
  LoopEventStore store;
  const uint32_t spacing = loopLength / (noteCount + 1u);
  for (unsigned i = 0; i < noteCount; ++i) {
    const uint32_t onTick = spacing * (i + 1u);
    appendNotePair(store, onTick, onTick + 48u, static_cast<uint8_t>(60 + i),
                   static_cast<NoteId>(i + 1u));
  }
  ChunkIdList refs;
  store.detachChunksTo(refs);
  RecordPass pass{};
  pass.id = 1;
  pass.state = CapturePassState::Active;
  pass.chunkRefs = std::move(refs);
  return pass;
}

void setupLoop(Loop& loop, uint32_t loopLength, unsigned noteCount) {
  loop.loopLengthTicks = loopLength;
  loop.passes.recordPass = makeRecordPassWithNoteCount(loopLength, noteCount);
  loop.nextPassId_ = 2;
}

void openCowLoopEventStore(CowLoopEventStore& session, Loop& loop) {
  loop.rematerializeEditView(session.mutStore());
  loop.assignMissingNoteIdsInStore(session.mutStore());
  session.discardFlatCache();
}

void reopenCowLoopEventStore(CowLoopEventStore& session, Loop& loop) {
  session.mutStore().clear();
  session.discardFlatCache();
  openCowLoopEventStore(session, loop);
}

template <typename Alloc>
size_t countDisplayNotes(const std::vector<MidiEvent, Alloc>& flat, uint32_t loopLength) {
  return NoteUtils::reconstructDisplayNotes(flat, loopLength, false).size();
}

MidiEventVec materializedFlat(const Loop& loop) {
  MidiEventVec flat;
  loop.passes.materializeToEventVector(flat);
  return flat;
}

}  // namespace

void test_stale_note_edit_session_wrong_display_after_track_switch() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loopShort;
  Loop loopLong;
  setupLoop(loopShort, kShortLoopLength, 2u);
  setupLoop(loopLong, kLongLoopLength, 4u);

  CowLoopEventStore session;
  openCowLoopEventStore(session, loopShort);

  TEST_ASSERT_EQUAL(2u, countDisplayNotes(session.readFlat(), kShortLoopLength));

  const size_t staleDisplayCount = countDisplayNotes(session.readFlat(), kLongLoopLength);
  const size_t expectedLongTrackCount = countDisplayNotes(materializedFlat(loopLong), kLongLoopLength);

  TEST_ASSERT_EQUAL(4u, expectedLongTrackCount);
  TEST_ASSERT_NOT_EQUAL(expectedLongTrackCount, staleDisplayCount);
}

void test_reopen_note_edit_session_store_matches_new_loop() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loopShort;
  Loop loopLong;
  setupLoop(loopShort, kShortLoopLength, 2u);
  setupLoop(loopLong, kLongLoopLength, 4u);

  CowLoopEventStore session;
  openCowLoopEventStore(session, loopShort);
  reopenCowLoopEventStore(session, loopLong);

  const MidiEventVec expectedFlat = materializedFlat(loopLong);
  TEST_ASSERT_EQUAL(expectedFlat.size(), session.readFlat().size());
  TEST_ASSERT_EQUAL(4u, countDisplayNotes(session.readFlat(), kLongLoopLength));

  for (size_t i = 0; i < expectedFlat.size(); ++i) {
    TEST_ASSERT_EQUAL(expectedFlat[i].tick, session.readFlat()[i].tick);
    TEST_ASSERT_EQUAL(expectedFlat[i].data.noteData.note, session.readFlat()[i].data.noteData.note);
  }
}

void test_filter_selectable_display_notes_after_track_switch_reopen() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loopShort;
  Loop loopLong;
  setupLoop(loopShort, kShortLoopLength, 2u);
  setupLoop(loopLong, kLongLoopLength, 4u);

  CowLoopEventStore session;
  openCowLoopEventStore(session, loopShort);
  reopenCowLoopEventStore(session, loopLong);

  NoteEditFocus focus{};
  const auto displayNotes =
      filterSelectableDisplayNotes(session.readFlat(), focus, kChannel, kLongLoopLength);
  TEST_ASSERT_EQUAL(4u, displayNotes.size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_stale_note_edit_session_wrong_display_after_track_switch);
  RUN_TEST(test_reopen_note_edit_session_store_matches_new_loop);
  RUN_TEST(test_filter_selectable_display_notes_after_track_switch_reopen);
  return UNITY_END();
}
