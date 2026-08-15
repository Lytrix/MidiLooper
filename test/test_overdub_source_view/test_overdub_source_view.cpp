//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// overdubSourceView establish/clear, materialize-aware geometry, wrap-safe lookup.

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
#include "GlobalUndoStack.h"
#include "LoopContentResolution.h"
#include "../test_support/CommittedChunkIdTestHelpers.h"
#include "../test_support/NoteIdTestFixtures.h"
#include "EditPass.h"
#include "MidiEvent.h"
#include "Globals.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/NoteUtils.h"

namespace {

using namespace NoteIdTestFixtures;

constexpr uint32_t kLoopLen = Config::TICKS_PER_BAR * 8;

int countNoteOns(const SessionMidiEventVec& flat, uint8_t pitch) {
  int count = 0;
  for (const MidiEvent& evt : flat) {
    if (evt.isNoteOn() && evt.data.noteData.note == pitch) {
      ++count;
    }
  }
  return count;
}

bool hasDisplayNote(const NoteUtils::DisplayNoteVec& notes, uint8_t pitch, uint32_t startTick) {
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.note == pitch && note.startTick == startTick) {
      return true;
    }
  }
  return false;
}

void seedRecordNote(Loop& loop, uint32_t onTick, uint32_t offTick, uint8_t pitch,
                    uint8_t channel = 1) {
  loop.loopLengthTicks = kLoopLen;
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, onTick, channel, pitch, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(offTick, channel, pitch, 0)));
  loop.seedRecordPassFromStore(store);
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

}  // namespace

void test_overdub_start_establishes_source_view() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);

  TEST_ASSERT_FALSE(loop.hasOverdubSourceView());
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  TEST_ASSERT_EQUAL(kLoopLen, loop.overdubSourceViewLoopLengthTicks());
  TEST_ASSERT_TRUE(loop.overdubSourceViewEvents().empty());
  TEST_ASSERT_FALSE(loop.overdubSourceViewNotes().empty());
  TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 10));
}

void test_record_start_does_not_keep_source_view() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());

  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_FALSE(loop.hasOverdubSourceView());
  TEST_ASSERT_TRUE(loop.overdubSourceViewNotes().empty());
}

void test_source_view_includes_edit_pass_geometry() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);
  (void)loop.saveNoteEditPass(0, makePitchRow(1, 10, 58, 67));

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  TEST_ASSERT_EQUAL(0, countNoteOns(loop.overdubSourceViewEvents(), 60));
  TEST_ASSERT_EQUAL(1, countNoteOns(loop.overdubSourceViewEvents(), 67));
}

void test_source_view_stable_across_capture_appends_and_wraps() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);
  loop.beginCapture(CapturePhase::Overdub);
  const size_t beforeNotes = loop.overdubSourceViewNotes().size();
  TEST_ASSERT_TRUE(beforeNotes >= 1u);

  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(kLoopLen - 20, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(kLoopLen - 5, 1, 72, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(5, 1, 74, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(40, 1, 74, 0)));

  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  TEST_ASSERT_EQUAL(beforeNotes, loop.overdubSourceViewNotes().size());
  TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 10));
  TEST_ASSERT_FALSE(hasDisplayNote(loop.overdubSourceViewNotes(), 72, kLoopLen - 20));
  TEST_ASSERT_FALSE(hasDisplayNote(loop.overdubSourceViewNotes(), 74, 5));
}

void test_source_view_immutable_when_live_materialize_mutates() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 10));

  loop.midiEvents().push_back(MidiEvent::NoteOn(100, 1, 80, 80));
  loop.midiEvents().push_back(MidiEvent::NoteOff(140, 1, 80, 0));
  loop.invalidateCaches();

  TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 10));
  TEST_ASSERT_FALSE(hasDisplayNote(loop.overdubSourceViewNotes(), 80, 100));
}

void test_source_view_wrap_safe_high_then_low_capture_order() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;

  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, kLoopLen - 40, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(kLoopLen - 10, 1, 60, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 8, 1, 62, 100, 2));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(48, 1, 62, 0)));
  loop.loopLengthTicks = kLoopLen;
  loop.seedRecordPassFromStore(store);

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(kLoopLen - 30, 1, 70, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(kLoopLen - 15, 1, 70, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(12, 1, 71, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(30, 1, 71, 0)));

  TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), 62, 8));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, kLoopLen - 40));
  TEST_ASSERT_FALSE(hasDisplayNote(loop.overdubSourceViewNotes(), 70, kLoopLen - 30));
  TEST_ASSERT_FALSE(hasDisplayNote(loop.overdubSourceViewNotes(), 71, 12));
}

void test_source_view_prepared_window_omits_unpaired_open_tails() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 10, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(58, 1, 60, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 80, 1, 72, 90, 2));
  loop.loopLengthTicks = kLoopLen;
  loop.seedRecordPassFromStore(store);
  loop.markDisplayCachesStale();

  LoopContentResolution::DeviceGateSample sample;
  LoopContentResolution::measureDeviceGate(loop.passes, loop.loopLengthTicks, sample);
  LoopContentResolution::deviceGateComplete(loop.playbackRevision);
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 10));
  TEST_ASSERT_FALSE(hasDisplayNote(loop.overdubSourceViewNotes(), 72, 80));
  for (const NoteUtils::DisplayNote& note : loop.overdubSourceViewNotes()) {
    TEST_ASSERT_FALSE(note.note == 72 && note.endTick == kLoopLen - 1);
  }
  LoopContentResolution::deviceGateReset();
}

void test_source_view_consumes_prepared_lcr_when_cache_dirty() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);
  loop.markDisplayCachesStale();
  TEST_ASSERT_FALSE(DisplayWindowUtils::committedDisplayVisualCacheAuthoritative(
      loop.visualCacheDirty, !loop.visualCache.notes.empty()));

  LoopContentResolution::DeviceGateSample sample;
  LoopContentResolution::measureDeviceGate(loop.passes, loop.loopLengthTicks, sample);
  LoopContentResolution::deviceGateComplete(loop.playbackRevision);
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  TEST_ASSERT_FALSE(loop.overdubSourceViewEvents().empty());
  TEST_ASSERT_FALSE(loop.overdubSourceViewNotes().empty());
  TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 10));
  LoopContentResolution::deviceGateReset();
}

void test_source_view_skips_stale_prepared_lcr_on_stamp_mismatch() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);
  loop.markDisplayCachesStale();

  LoopContentResolution::DeviceGateSample sample;
  LoopContentResolution::measureDeviceGate(loop.passes, loop.loopLengthTicks, sample);
  LoopContentResolution::deviceGateComplete(loop.playbackRevision);
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));
  ++loop.playbackRevision;
  TEST_ASSERT_FALSE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  TEST_ASSERT_FALSE(loop.overdubSourceViewEvents().empty());
  TEST_ASSERT_TRUE(loop.overdubSourceViewNotes().empty());
  TEST_ASSERT_EQUAL(1, countNoteOns(loop.overdubSourceViewEvents(), 60));
  LoopContentResolution::deviceGateReset();
}

void test_discard_and_commit_clear_source_view() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  TEST_ASSERT_FALSE(loop.overdubSourceViewNotes().empty());
  loop.discardCapture();
  TEST_ASSERT_FALSE(loop.hasOverdubSourceView());
  TEST_ASSERT_TRUE(loop.overdubSourceViewNotes().empty());

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(200, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(248, 1, 64, 0)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(0));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass());
  TEST_ASSERT_FALSE(loop.hasOverdubSourceView());
  TEST_ASSERT_TRUE(loop.overdubSourceViewNotes().empty());
}

void test_extract_open_note_ons_leaves_completed_pairs() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 0, 48, 60);
  loop.openOverdubSession(777);
  loop.beginCapture(CapturePhase::Overdub, 777);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(200, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(400, 1, 72, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(500, 1, 60, 90)));
  SessionMidiEventVec held;
  TEST_ASSERT_EQUAL(1u, loop.extractOpenCaptureNoteOns(held));
  TEST_ASSERT_EQUAL(1u, held.size());
  TEST_ASSERT_TRUE(held[0].isNoteOn());
  TEST_ASSERT_EQUAL(60, held[0].data.noteData.note);
  TEST_ASSERT_EQUAL(500u, held[0].tick);
  SessionMidiEventVec remaining;
  loop.capture.store.copyEventsTo(remaining);
  TEST_ASSERT_EQUAL(2u, remaining.size());
}

void test_extract_open_note_ons_keeps_same_tick_completed_pair() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 0, 48, 60);
  loop.loopLengthTicks = 3072;
  loop.openOverdubSession(2904);
  loop.beginCapture(CapturePhase::Overdub, 2904);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(2880, 4, 30, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(2976, 4, 30, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(2880, 4, 30, 100)));
  SessionMidiEventVec held;
  TEST_ASSERT_EQUAL(1u, loop.extractOpenCaptureNoteOns(held));
  TEST_ASSERT_EQUAL(1u, held.size());
  TEST_ASSERT_TRUE(held[0].isNoteOn());
  TEST_ASSERT_EQUAL(30, held[0].data.noteData.note);
  TEST_ASSERT_EQUAL(2880u, held[0].tick);
  SessionMidiEventVec remaining;
  loop.capture.store.copyEventsTo(remaining);
  TEST_ASSERT_EQUAL(2u, remaining.size());
  TEST_ASSERT_TRUE(remaining[0].isNoteOn());
  TEST_ASSERT_TRUE(remaining[1].isNoteOff());
  TEST_ASSERT_EQUAL(30, remaining[0].data.noteData.note);
  TEST_ASSERT_EQUAL(30, remaining[1].data.noteData.note);
  TEST_ASSERT_EQUAL(2880u, remaining[0].tick);
  TEST_ASSERT_EQUAL(2976u, remaining[1].tick);
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 2904));
  loop.beginCapture(CapturePhase::Overdub, 2904);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(held[0]));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(2976, 4, 30, 0)));
  SessionMidiEventVec live;
  loop.capture.store.copyEventsTo(live);
  TEST_ASSERT_EQUAL(2u, live.size());
  TEST_ASSERT_EQUAL(1, countNoteOns(live, 30));
}

void test_empty_wrap_does_not_commit_a_pass() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 0, 48, 60);
  const PassId before = loop.lastCommittedPassId();
  loop.openOverdubSession(777);
  loop.beginCapture(CapturePhase::Overdub, 777);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(500, 1, 60, 90)));
  SessionMidiEventVec held;
  TEST_ASSERT_EQUAL(1u, loop.extractOpenCaptureNoteOns(held));
  TEST_ASSERT_TRUE(loop.capture.store.empty());
  TEST_ASSERT_EQUAL(before, loop.lastCommittedPassId());
  TEST_ASSERT_TRUE(loop.hasOverdubSession());
  TEST_ASSERT_EQUAL(777u, loop.playheadPhaseTick);
  for (const MidiEvent& evt : held) {
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(evt));
  }
  TEST_ASSERT_FALSE(loop.capture.store.empty());
}

void test_wrap_commit_publishes_completed_pair_and_keeps_held() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 0, 48, 60);
  loop.openOverdubSession(777);
  loop.beginCapture(CapturePhase::Overdub, 777);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(200, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(400, 1, 72, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(500, 1, 60, 90)));
  SessionMidiEventVec held;
  loop.extractOpenCaptureNoteOns(held);
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 777));
  const PassId wrapId = loop.lastCommittedPassId();
  TEST_ASSERT_NOT_EQUAL(kInvalidPassId, wrapId);
  loop.pushOverdubSessionPass(wrapId, {});
  TEST_ASSERT_TRUE(loop.hasOverdubSession());
  TEST_ASSERT_EQUAL(777u, loop.playheadPhaseTick);
  loop.beginCapture(CapturePhase::Overdub, loop.playheadPhaseTick);
  for (const MidiEvent& evt : held) {
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(evt));
  }
  SessionMidiEventVec live;
  loop.capture.store.copyEventsTo(live);
  TEST_ASSERT_EQUAL(1u, live.size());
  TEST_ASSERT_EQUAL(60, live[0].data.noteData.note);
  TEST_ASSERT_EQUAL(2u, loop.overdubSessionUndoDepth());
}

void test_overdub_session_undo_hides_wrap_from_prepared_lcr() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  seedRecordNote(loop, 0, 48, 60);
  LoopContentResolution::DeviceGateSample sample;
  LoopContentResolution::measureDeviceGate(loop.passes, loop.loopLengthTicks, sample);
  LoopContentResolution::deviceGateComplete(loop.playbackRevision);
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));

  loop.openOverdubSession(777);
  loop.beginCapture(CapturePhase::Overdub, 777);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(200, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(400, 1, 72, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 777));
  const PassId wrapId = loop.lastCommittedPassId();
  const OverdubPass* wrap = nullptr;
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    if (pass.id == wrapId) {
      wrap = &pass;
      break;
    }
  }
  TEST_ASSERT_NOT_NULL(wrap);
  LoopContentResolution::publishPreparedOverdubPass(*wrap, loop.playbackRevision);
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));

  SoundingNoteVec sounding;
  TEST_ASSERT_TRUE(
      LoopContentResolution::tryResolvePreparedState(300, loop.playbackRevision, sounding, nullptr));
  bool sawWrap = false;
  for (const SoundingNote& note : sounding) {
    if (note.pitch == 72) {
      sawWrap = true;
    }
  }
  TEST_ASSERT_TRUE(sawWrap);

  loop.pushOverdubSessionPass(wrapId, {});
  loop.beginCapture(CapturePhase::Overdub, 777);
  TEST_ASSERT_TRUE(loop.undoOverdubSession());
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));
  sounding.clear();
  TEST_ASSERT_TRUE(
      LoopContentResolution::tryResolvePreparedState(300, loop.playbackRevision, sounding, nullptr));
  for (const SoundingNote& note : sounding) {
    TEST_ASSERT_FALSE(note.pitch == 72);
  }
  SessionMidiEventVec window;
  TEST_ASSERT_TRUE(LoopContentResolution::tryResolvePreparedWindow(
      loop.passes.editPasses, loop.loopLengthTicks, 0, loop.loopLengthTicks, loop.playbackRevision,
      window, nullptr));
  const NoteUtils::DisplayNoteVec notes =
      NoteUtils::reconstructDisplayNotes(window, loop.loopLengthTicks, false, false);
  TEST_ASSERT_FALSE(hasDisplayNote(notes, 72, 200));
  LoopContentResolution::deviceGateReset();
}

void test_overdub_session_undo_disables_sealed_wrap() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 0, 48, 60);
  loop.openOverdubSession(777);
  loop.beginCapture(CapturePhase::Overdub, 777);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(200, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(400, 1, 72, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 777));
  const PassId wrapId = loop.lastCommittedPassId();
  loop.pushOverdubSessionPass(wrapId, {});
  loop.beginCapture(CapturePhase::Overdub, 777);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(500, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(600, 1, 64, 0)));
  TEST_ASSERT_EQUAL(2u, loop.overdubSessionUndoDepth());
  TEST_ASSERT_TRUE(loop.canUndoOverdubSession());
  TEST_ASSERT_TRUE(loop.undoOverdubSession());
  TEST_ASSERT_TRUE(loop.capture.store.empty());
  TEST_ASSERT_EQUAL(1u, loop.overdubSessionUndoDepth());
  bool disabledAfterLive = false;
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    if (pass.id == wrapId) {
      disabledAfterLive = pass.state == CapturePassState::Disabled;
    }
  }
  TEST_ASSERT_FALSE(disabledAfterLive);
  TEST_ASSERT_TRUE(loop.undoOverdubSession());
  TEST_ASSERT_EQUAL(0u, loop.overdubSessionUndoDepth());
  bool disabled = false;
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    if (pass.id == wrapId) {
      disabled = pass.state == CapturePassState::Disabled;
    }
  }
  TEST_ASSERT_TRUE(disabled);
  loop.invalidateCaches();
  SessionMidiEventVec afterUndo;
  loop.gatherCommittedEvents(afterUndo);
  TEST_ASSERT_EQUAL(0, countNoteOns(afterUndo, 72));
  TEST_ASSERT_EQUAL(1, countNoteOns(afterUndo, 60));
  TEST_ASSERT_TRUE(loop.canRedoOverdubSession());
  TEST_ASSERT_TRUE(loop.redoOverdubSession());
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    if (pass.id == wrapId) {
      TEST_ASSERT_EQUAL(CapturePassState::Active, pass.state);
    }
  }
  TEST_ASSERT_TRUE(loop.capture.store.empty());
  TEST_ASSERT_TRUE(loop.redoOverdubSession());
  SessionMidiEventVec restoredLive;
  loop.capture.store.copyEventsTo(restoredLive);
  TEST_ASSERT_EQUAL(2u, restoredLive.size());
  TEST_ASSERT_EQUAL(64, restoredLive[0].data.noteData.note);
}

void test_overdub_session_undo_depth_adds_live_after_first_wrap() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 0, 48, 60);
  loop.openOverdubSession(777);
  loop.beginCapture(CapturePhase::Overdub, 777);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(200, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(400, 1, 72, 0)));
  TEST_ASSERT_EQUAL(1u, loop.overdubSessionUndoDepth());
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 777));
  loop.pushOverdubSessionPass(loop.lastCommittedPassId(), {});
  loop.beginCapture(CapturePhase::Overdub, 777);
  TEST_ASSERT_EQUAL(1u, loop.overdubSessionUndoDepth());
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(500, 1, 64, 90)));
  TEST_ASSERT_EQUAL(2u, loop.overdubSessionUndoDepth());
  TEST_ASSERT_TRUE(loop.undoOverdubSession());
  TEST_ASSERT_TRUE(loop.capture.store.empty());
  TEST_ASSERT_EQUAL(1u, loop.overdubSessionUndoDepth());
  bool wrapDisabled = false;
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    if (pass.id == loop.lastCommittedPassId()) {
      wrapDisabled = pass.state == CapturePassState::Disabled;
    }
  }
  TEST_ASSERT_FALSE(wrapDisabled);
}

void test_session_undo_skips_next_wrap_crossing() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 0, 48, 60);
  loop.openOverdubSession(777);
  loop.beginCapture(CapturePhase::Overdub, 777);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(200, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(400, 1, 72, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 777));
  loop.pushOverdubSessionPass(loop.lastCommittedPassId(), {});
  loop.beginCapture(CapturePhase::Overdub, 777);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(500, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.undoOverdubSession());
  TEST_ASSERT_TRUE(loop.capture.store.empty());
  loop.armOverdubWrapAfterLeavingStart(778);
  TEST_ASSERT_FALSE(loop.shouldCommitOverdubWrap(776, 777));
  TEST_ASSERT_TRUE(loop.consumeSuppressedOverdubWrapCrossing(776, 777));
  loop.armOverdubWrapAfterLeavingStart(778);
  TEST_ASSERT_TRUE(loop.shouldCommitOverdubWrap(776, 777));
}

void test_stop_collects_session_wraps_then_close_clears_stack() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 0, 48, 60);
  loop.openOverdubSession(777);
  loop.beginCapture(CapturePhase::Overdub, 777);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(200, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(400, 1, 72, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 777));
  const PassId wrap1 = loop.lastCommittedPassId();
  EditPassIdList companions1;
  companions1.push_back(11);
  loop.pushOverdubSessionPass(wrap1, companions1);
  loop.beginCapture(CapturePhase::Overdub, 777);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(500, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(600, 1, 64, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 777));
  const PassId wrap2 = loop.lastCommittedPassId();
  EditPassIdList companions2;
  companions2.push_back(12);
  loop.pushOverdubSessionPass(wrap2, companions2);

  PassIdList passIds;
  EditPassIdList companions;
  loop.collectOverdubSessionUndoPasses(passIds, companions);
  TEST_ASSERT_EQUAL(2u, passIds.size());
  TEST_ASSERT_EQUAL(wrap1, passIds[0]);
  TEST_ASSERT_EQUAL(wrap2, passIds[1]);
  TEST_ASSERT_EQUAL(2u, companions.size());
  TEST_ASSERT_EQUAL(11u, companions[0]);
  TEST_ASSERT_EQUAL(12u, companions[1]);

  TEST_ASSERT_TRUE(loop.undoOverdubSession());
  PassIdList afterUndo;
  EditPassIdList companionsAfterUndo;
  loop.collectOverdubSessionUndoPasses(afterUndo, companionsAfterUndo);
  TEST_ASSERT_EQUAL(1u, afterUndo.size());
  TEST_ASSERT_EQUAL(wrap1, afterUndo[0]);
  TEST_ASSERT_EQUAL(1u, companionsAfterUndo.size());
  TEST_ASSERT_EQUAL(11u, companionsAfterUndo[0]);
  TEST_ASSERT_TRUE(loop.redoOverdubSession());
  loop.collectOverdubSessionUndoPasses(passIds, companions);
  TEST_ASSERT_EQUAL(2u, passIds.size());
  TEST_ASSERT_EQUAL(wrap2, passIds[1]);

  UndoEntry entry{};
  entry.kind = UndoEntryKind::OverdubPassAdded;
  entry.passId = passIds.back();
  entry.passIds = passIds;
  PassIdList visited;
  appendOverdubCapturePassIds(entry, visited);
  TEST_ASSERT_EQUAL(2u, visited.size());
  for (const PassId id : visited) {
    TEST_ASSERT_TRUE(loop.setCapturePassState(id, CapturePassState::Disabled));
  }
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    if (pass.id == wrap1 || pass.id == wrap2) {
      TEST_ASSERT_EQUAL(CapturePassState::Disabled, pass.state);
    }
  }

  loop.closeOverdubSession();
  TEST_ASSERT_FALSE(loop.hasOverdubSession());
  PassIdList afterClose;
  EditPassIdList companionsAfterClose;
  loop.collectOverdubSessionUndoPasses(afterClose, companionsAfterClose);
  TEST_ASSERT_TRUE(afterClose.empty());
  TEST_ASSERT_TRUE(companionsAfterClose.empty());
}

void test_should_commit_overdub_wrap_after_leaving_start() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = kLoopLen;
  loop.openOverdubSession(777);
  TEST_ASSERT_FALSE(loop.shouldCommitOverdubWrap(776, 777));
  loop.armOverdubWrapAfterLeavingStart(778);
  TEST_ASSERT_TRUE(loop.shouldCommitOverdubWrap(776, 777));
  loop.noteOverdubWrapCommitted();
  TEST_ASSERT_FALSE(loop.shouldCommitOverdubWrap(776, 777));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_overdub_start_establishes_source_view);
  RUN_TEST(test_record_start_does_not_keep_source_view);
  RUN_TEST(test_source_view_includes_edit_pass_geometry);
  RUN_TEST(test_source_view_stable_across_capture_appends_and_wraps);
  RUN_TEST(test_source_view_immutable_when_live_materialize_mutates);
  RUN_TEST(test_source_view_wrap_safe_high_then_low_capture_order);
  RUN_TEST(test_source_view_prepared_window_omits_unpaired_open_tails);
  RUN_TEST(test_source_view_consumes_prepared_lcr_when_cache_dirty);
  RUN_TEST(test_source_view_skips_stale_prepared_lcr_on_stamp_mismatch);
  RUN_TEST(test_discard_and_commit_clear_source_view);
  RUN_TEST(test_extract_open_note_ons_leaves_completed_pairs);
  RUN_TEST(test_extract_open_note_ons_keeps_same_tick_completed_pair);
  RUN_TEST(test_empty_wrap_does_not_commit_a_pass);
  RUN_TEST(test_wrap_commit_publishes_completed_pair_and_keeps_held);
  RUN_TEST(test_overdub_session_undo_hides_wrap_from_prepared_lcr);
  RUN_TEST(test_overdub_session_undo_disables_sealed_wrap);
  RUN_TEST(test_overdub_session_undo_depth_adds_live_after_first_wrap);
  RUN_TEST(test_session_undo_skips_next_wrap_crossing);
  RUN_TEST(test_stop_collects_session_wraps_then_close_clears_stack);
  RUN_TEST(test_should_commit_overdub_wrap_after_leaving_start);
  return UNITY_END();
}
