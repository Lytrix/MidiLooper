//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// DEC-036 D1 — incremental effective event store.

#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/EditManager/EditApply.cpp"
#include "../../src/Loop/LoopPasses.cpp"
#include "../test_support/MemoryMonitorNativeDeps.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/Loop.cpp"
#include "../test_support/LoopCaptureTestDeps.cpp"

#include "Loop.h"
#include "../test_support/CommittedChunkIdTestHelpers.h"
#include "../test_support/NoteIdTestFixtures.h"
#include "EditPass.h"

namespace {

using namespace NoteIdTestFixtures;

constexpr uint32_t kLoopLen = Config::TICKS_PER_BAR * 8;

void seedRecordNote(Loop& loop, uint32_t onTick, uint32_t offTick, uint8_t pitch,
                    uint8_t channel = 1) {
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, onTick, channel, pitch, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(offTick, channel, pitch, 0)));
  loop.seedRecordPassFromStore(store);
  loop.loopLengthTicks = kLoopLen;
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

bool eventsEqual(const SessionMidiEventVec& a, const SessionMidiEventVec& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].tick != b[i].tick || a[i].type != b[i].type ||
        a[i].data.noteData.note != b[i].data.noteData.note) {
      return false;
    }
  }
  return true;
}

}  // namespace

void test_effective_store_matches_materialize_after_seed() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);

  SessionMidiEventVec expected;
  loop.passes.materializeToEventVector(expected, kLoopLen);

  SessionMidiEventVec actual;
  loop.copyEffectiveCommittedEvents(actual);
  TEST_ASSERT_TRUE(eventsEqual(expected, actual));
  TEST_ASSERT_GREATER_THAN(0u, loop.effectiveEventStoreRevision());
}

void test_effective_store_updates_on_edit_pass_save() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);

  SessionMidiEventVec beforeEdit;
  loop.copyEffectiveCommittedEvents(beforeEdit);

  const EditPassId editId = loop.saveNoteEditPass(0, makePitchRow(1, 10, 58, 67));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, editId);

  SessionMidiEventVec expected;
  loop.passes.materializeToEventVector(expected, kLoopLen);
  SessionMidiEventVec actual;
  loop.copyEffectiveCommittedEvents(actual);
  TEST_ASSERT_TRUE(eventsEqual(expected, actual));
  TEST_ASSERT_FALSE(eventsEqual(beforeEdit, actual));
}

void test_overdub_entry_does_not_rebuild_effective_store() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);
  (void)loop.saveNoteEditPass(0, makePitchRow(1, 10, 58, 67));

  Loop::resetCommittedPitchQueryWork();
  const uint32_t revBefore = loop.effectiveEventStoreRevision();
  TEST_ASSERT_GREATER_THAN(0u, revBefore);

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_EQUAL(revBefore, loop.effectiveEventStoreRevision());
  TEST_ASSERT_EQUAL(0u, Loop::committedEventsFullMaterializeCount());
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  TEST_ASSERT_FALSE(loop.overdubSourceViewEvents().empty());
}

void test_ensure_effective_store_does_not_rebuild_when_fresh() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);
  (void)loop.saveNoteEditPass(0, makePitchRow(1, 10, 58, 67));

  SessionMidiEventVec warm;
  loop.copyEffectiveCommittedEvents(warm);
  const uint32_t revAfterWarm = loop.effectiveEventStoreRevision();
  TEST_ASSERT_GREATER_THAN(0u, revAfterWarm);

  loop.copyEffectiveCommittedEventsInRange(warm, 0, kLoopLen);
  TEST_ASSERT_EQUAL(revAfterWarm, loop.effectiveEventStoreRevision());
}

void test_overdub_entry_uses_windowed_source_on_long_loop() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t longLoopLen = Config::TICKS_PER_BAR * 68;
  loop.loopLengthTicks = longLoopLen;

  LoopEventStore store;
  for (uint32_t bar = 0; bar < 68; ++bar) {
    const uint32_t onTick = bar * Config::TICKS_PER_BAR + 10;
    const uint32_t offTick = onTick + 40;
    TEST_ASSERT_TRUE(storeAppendNoteOn(store, onTick, 1, static_cast<uint8_t>(60 + (bar % 12)), 100,
                                       static_cast<NoteId>(bar + 1)));
    TEST_ASSERT_TRUE(store.append(
        MidiEvent::NoteOff(offTick, 1, static_cast<uint8_t>(60 + (bar % 12)), 0)));
  }
  loop.seedRecordPassFromStore(store);

  SessionMidiEventVec fullFlat;
  loop.copyEffectiveCommittedEvents(fullFlat);
  TEST_ASSERT_GREATER_THAN(100u, fullFlat.size());

  const uint32_t playhead = 32u * Config::TICKS_PER_BAR;
  loop.beginCapture(CapturePhase::Overdub, playhead);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  TEST_ASSERT_LESS_THAN(fullFlat.size(), loop.overdubSourceViewEvents().size() + 1u);
  TEST_ASSERT_GREATER_THAN(0u, loop.overdubSourceViewEvents().size());
  TEST_ASSERT_FALSE(loop.overdubSourceViewNotes().empty());
}

void test_undo_pass_toggle_rebuilds_effective_store() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);
  const PassId recordId = loop.passes.recordPass.id;

  TEST_ASSERT_TRUE(loop.setCapturePassState(recordId, CapturePassState::Disabled));

  SessionMidiEventVec disabled;
  loop.copyEffectiveCommittedEvents(disabled);
  TEST_ASSERT_EQUAL(0u, disabled.size());

  TEST_ASSERT_TRUE(loop.setCapturePassState(recordId, CapturePassState::Active));
  SessionMidiEventVec expected;
  loop.passes.materializeToEventVector(expected, kLoopLen);
  SessionMidiEventVec restored;
  loop.copyEffectiveCommittedEvents(restored);
  TEST_ASSERT_TRUE(eventsEqual(expected, restored));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_effective_store_matches_materialize_after_seed);
  RUN_TEST(test_effective_store_updates_on_edit_pass_save);
  RUN_TEST(test_overdub_entry_does_not_rebuild_effective_store);
  RUN_TEST(test_ensure_effective_store_does_not_rebuild_when_fresh);
  RUN_TEST(test_overdub_entry_uses_windowed_source_on_long_loop);
  RUN_TEST(test_undo_pass_toggle_rebuilds_effective_store);
  return UNITY_END();
}
