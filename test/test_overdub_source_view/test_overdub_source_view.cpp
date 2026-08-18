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
#include "../../src/EditManager/EditSessionLiveStoreSpan.cpp"
#include "../../src/EditManager/NoteEditCurrentState.cpp"
#include "../../src/EditManager/EditSessionInteraction.cpp"
#include "../../src/EditManager/ResolveConstrainedGeometry.cpp"
#include "../../src/EditManager/ParticipatingNoteSession.cpp"
#include "../../src/Loop/LoopPendingNoteChange.cpp"
#include "../../src/Utils/RuntimeTimingTelemetry.cpp"

#include "Loop.h"
#include "GlobalUndoStack.h"
#include "LoopContentResolution.h"
#include "OverlapNoteIdSet.h"
#include "../test_support/CommittedChunkIdTestHelpers.h"
#include "../test_support/NoteIdTestFixtures.h"
#include "EditPass.h"
#include "PendingNoteChange.h"
#include "MidiEvent.h"
#include "Globals.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/NoteUtils.h"

#include <cstdio>
#include <initializer_list>

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

void seedTwoRecordNotes(Loop& loop, uint32_t on1, uint32_t off1, NoteId id1, uint32_t on2,
                        uint32_t off2, NoteId id2, uint8_t pitch) {
  loop.loopLengthTicks = kLoopLen;
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, on1, 1, pitch, 100, id1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(off1, 1, pitch, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, on2, 1, pitch, 100, id2));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(off2, 1, pitch, 0)));
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

int countDisplayNotesAtStartTick(const NoteUtils::DisplayNoteVec& notes, uint32_t startTick) {
  int count = 0;
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.startTick == startTick) {
      ++count;
    }
  }
  return count;
}

OverlapNoteIdSet overlapIds(std::initializer_list<NoteId> ids) {
  OverlapNoteIdSet out;
  for (NoteId id : ids) {
    (void)out.insert(id);
  }
  return out;
}

const OverdubPass* findOverdubPass(const Loop& loop, PassId id) {
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    if (pass.id == id) {
      return &pass;
    }
  }
  return nullptr;
}

void formatNoteIds(const OverlapNoteIdSet& ids, char* buf, size_t cap) {
  if (buf == nullptr || cap == 0) {
    return;
  }
  buf[0] = '\0';
  size_t used = 0;
  for (size_t i = 0; i < ids.size(); ++i) {
    const int n = snprintf(buf + used, cap - used, "%s%u", i == 0 ? "" : ",",
                           static_cast<unsigned>(ids.at(i)));
    if (n < 0 || static_cast<size_t>(n) >= cap - used) {
      return;
    }
    used += static_cast<size_t>(n);
  }
}

void collectHoldParticipantSets(Loop& loop, uint32_t tick, uint8_t pitch, OverlapNoteIdSet& sourceIds,
                                OverlapNoteIdSet& preparedIds) {
  loop.collectOverdubSourceHoldParticipantIds(tick, pitch, sourceIds);
  TEST_ASSERT_TRUE(LoopContentResolution::tryCollectPreparedPresentNoteIdsAtTick(
      tick, pitch, loop.playbackRevision, preparedIds));
}

EditPassIdList commitSamePitchWrapAndPublish(Loop& loop, NoteId wrapNoteId, uint32_t onTick,
                                             uint32_t offTick, const OverlapNoteIdSet& occupyIds) {
  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, onTick, offTick,
                                                                   wrapNoteId, occupyIds));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(onTick, 1, 60, 90, wrapNoteId)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(offTick, 1, 60, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubWrap, 0));
  const OverdubPass* wrap = findOverdubPass(loop, loop.lastCommittedPassId());
  TEST_ASSERT_NOT_NULL(wrap);
  const EditPassIdList companions = loop.sealPendingNoteChangesToEditPasses();
  LoopContentResolution::publishPreparedOverdubPass(*wrap, loop.playbackRevision,
                                                    loop.passes.editPasses, companions);
  loop.rebuildOverdubSourceView(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  return companions;
}

bool sourceViewHasNoteId(const Loop& loop, NoteId noteId) {
  for (const NoteUtils::DisplayNote& note : loop.overdubSourceViewNotes()) {
    if (note.noteId == noteId) {
      return true;
    }
  }
  return false;
}

bool editPassesHideTarget(const Loop& loop, NoteId noteId) {
  for (const EditPass& row : loop.passes.editPasses) {
    if (row.state == EditPassState::Active && row.passType == EditPassType::Note &&
        row.actionType == EditActionType::Delete && row.targetNoteId == noteId) {
      return true;
    }
  }
  return false;
}

}  // namespace

void test_pitch_edit_rebuild_shows_only_new_pitch_at_tick() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 64, 240, 60);
  loop.rebuildVisualCacheFromPasses();
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 60, 64));

  const EditPassId pitchEditId = loop.saveNoteEditPass(0, makePitchRow(1, 64, 240, 70));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, pitchEditId);
  loop.rebuildVisualCacheFromPasses();

  TEST_ASSERT_FALSE(hasDisplayNote(loop.visualCache.notes, 60, 64));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 70, 64));
  TEST_ASSERT_EQUAL(1, countDisplayNotesAtStartTick(loop.visualCache.notes, 64));
}

void test_disable_edit_pass_rebuild_restores_pre_edit_display() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 64, 240, 60);

  const EditPassId pitchEditId = loop.saveNoteEditPass(0, makePitchRow(1, 64, 240, 70));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, pitchEditId);
  loop.rebuildVisualCacheFromPasses();
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 70, 64));

  loop.disableEditPasses(EditPassIdList{pitchEditId});
  loop.rebuildVisualCacheFromPasses();

  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 60, 64));
  TEST_ASSERT_FALSE(hasDisplayNote(loop.visualCache.notes, 70, 64));
}

void test_overdub_start_establishes_source_view() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);

  TEST_ASSERT_FALSE(loop.hasOverdubSourceView());
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  TEST_ASSERT_EQUAL(kLoopLen, loop.overdubSourceViewLoopLengthTicks());
  TEST_ASSERT_FALSE(loop.overdubSourceViewEvents().empty());
  TEST_ASSERT_FALSE(loop.overdubSourceViewNotes().empty());
  TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 10));
}

void test_overdub_enter_rebuilds_source_view_not_visual_cache() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = kLoopLen;
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 10, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(58, 1, 60, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 80, 1, 72, 100, 2));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(120, 1, 72, 0)));
  loop.seedRecordPassFromStore(store);
  loop.rebuildVisualCacheFromPasses();
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 60, 10));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 72, 80));

  NoteUtils::DisplayNote kept{};
  bool foundKept = false;
  for (const NoteUtils::DisplayNote& note : loop.visualCache.notes) {
    if (note.note == 60 && note.startTick == 10) {
      kept = note;
      foundKept = true;
      break;
    }
  }
  TEST_ASSERT_TRUE(foundKept);
  loop.visualCache.notes.clear();
  loop.visualCache.notes.push_back(kept);
  loop.visualCacheDirty = false;
  loop.visualCache.dirtyBars.clear();
  TEST_ASSERT_TRUE(DisplayWindowUtils::committedDisplayVisualCacheAuthoritative(
      loop.visualCacheDirty, !loop.visualCache.notes.empty()));
  TEST_ASSERT_FALSE(hasDisplayNote(loop.visualCache.notes, 72, 80));

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 10));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), 72, 80));
}

void test_short_loop_stale_keeps_notes_for_overdub_stop_handoff() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kFourBars = Config::TICKS_PER_BAR * 4;
  loop.loopLengthTicks = kFourBars;
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 10, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(58, 1, 60, 0)));
  loop.seedRecordPassFromStore(store);
  TEST_ASSERT_FALSE(loop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks));
  loop.rebuildVisualCacheFromPasses();
  const size_t notesBefore = loop.visualCache.notes.size();
  TEST_ASSERT_TRUE(notesBefore > 0);
  TEST_ASSERT_FALSE(loop.visualCacheDirty);
  loop.markDisplayCachesStale();
  TEST_ASSERT_TRUE(loop.visualCacheDirty);
  TEST_ASSERT_EQUAL(notesBefore, loop.visualCache.notes.size());
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 60, 10));
}

void test_note_edit_idle_paint_consumes_stale_visual_cache() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kFourBars = Config::TICKS_PER_BAR * 4;
  loop.loopLengthTicks = kFourBars;
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 10, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(58, 1, 60, 0)));
  loop.seedRecordPassFromStore(store);
  TEST_ASSERT_FALSE(loop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks));
  loop.rebuildVisualCacheFromPasses();
  loop.markDisplayCachesStale();
  TEST_ASSERT_TRUE(loop.visualCacheDirty);
  TEST_ASSERT_FALSE(loop.visualCache.notes.empty());
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 60, 10));
}

void test_short_loop_idle_slice_cleans_without_full_rebuild() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kFourBars = Config::TICKS_PER_BAR * 4;
  loop.loopLengthTicks = kFourBars;
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 10, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(58, 1, 60, 0)));
  loop.seedRecordPassFromStore(store);
  TEST_ASSERT_FALSE(loop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks));
  loop.rebuildVisualCacheFromPasses();
  loop.markDisplayCachesStale();
  TEST_ASSERT_TRUE(loop.visualCacheDirty);
  loop.rebuildVisualCacheIdleSlice(4, 0);
  TEST_ASSERT_FALSE(loop.visualCacheDirty);
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 60, 10));
}

void test_overdub_begin_makes_restore_flatten_unreachable() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);
  Loop::resetCommittedPitchQueryWork();
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  TEST_ASSERT_EQUAL_UINT32(0, Loop::committedEventsFullMaterializeCount());
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
  TEST_ASSERT_FALSE(loop.overdubSourceViewNotes().empty());
  TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 10));
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

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(200, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(248, 1, 64, 0)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(0));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass());
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  TEST_ASSERT_FALSE(loop.overdubSourceViewNotes().empty());
  loop.closeOverdubSession();
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

void test_extract_keeps_tick0_wrap_held_pair_when_pitch_replays() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 0, 48, 60);
  loop.loopLengthTicks = 3072;
  loop.openOverdubSession(2904);
  loop.beginCapture(CapturePhase::Overdub, 2904);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(2976, 4, 30, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(0, 4, 30, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(672, 4, 30, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(768, 4, 30, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(2880, 4, 30, 100)));
  SessionMidiEventVec held;
  TEST_ASSERT_EQUAL(1u, loop.extractOpenCaptureNoteOns(held));
  TEST_ASSERT_EQUAL(2880u, held[0].tick);
  SessionMidiEventVec remaining;
  loop.capture.store.copyEventsTo(remaining);
  bool keptWrapOn = false;
  bool keptWrapOff = false;
  for (const MidiEvent& evt : remaining) {
    if (evt.isNoteOn() && evt.data.noteData.note == 30 && evt.tick == 2976u) {
      keptWrapOn = true;
    }
    if (evt.isNoteOff() && evt.data.noteData.note == 30 && evt.tick == 0u) {
      keptWrapOff = true;
    }
  }
  TEST_ASSERT_TRUE(keptWrapOn);
  TEST_ASSERT_TRUE(keptWrapOff);
}

void test_extract_keeps_head_off_wrap_held_pair_when_pitch_replays() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 0, 48, 60);
  loop.loopLengthTicks = 3072;
  loop.openOverdubSession(1152);
  loop.beginCapture(CapturePhase::Overdub, 1152);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(2976, 4, 12, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(96, 4, 12, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(288, 4, 12, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(384, 4, 12, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(1344, 4, 12, 100)));
  loop.ensureCaptureEventsSorted();
  SessionMidiEventVec held;
  TEST_ASSERT_EQUAL(1u, loop.extractOpenCaptureNoteOns(held));
  TEST_ASSERT_EQUAL(1344u, held[0].tick);
  SessionMidiEventVec remaining;
  loop.capture.store.copyEventsTo(remaining);
  bool keptWrapOn = false;
  bool keptWrapOff = false;
  for (const MidiEvent& evt : remaining) {
    if (evt.isNoteOn() && evt.data.noteData.note == 12 && evt.tick == 2976u) {
      keptWrapOn = true;
    }
    if (evt.isNoteOff() && evt.data.noteData.note == 12 && evt.tick == 96u) {
      keptWrapOff = true;
    }
  }
  TEST_ASSERT_TRUE(keptWrapOn);
  TEST_ASSERT_TRUE(keptWrapOff);
}

void test_visual_cache_keeps_overdub_wrap_held_without_stretching_record() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = 3072;
  LoopEventStore record;
  TEST_ASSERT_TRUE(storeAppendNoteOn(record, 672, 4, 12, 100, 1));
  TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(768, 4, 12, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(record, 2400, 4, 12, 100, 2));
  TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(2500, 4, 12, 0)));
  loop.seedRecordPassFromStore(record);
  loop.openOverdubSession(1152);
  loop.beginCapture(CapturePhase::Overdub, 1152);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(2976, 4, 12, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(96, 4, 12, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(288, 4, 12, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(384, 4, 12, 0)));
  SessionMidiEventVec held;
  TEST_ASSERT_EQUAL(0u, loop.extractOpenCaptureNoteOns(held));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 1152));
  loop.rebuildVisualCacheFromPasses();
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 12, 2976));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 12, 0));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 12, 288));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 12, 672));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 12, 2400));
  for (const NoteUtils::DisplayNote& note : loop.visualCache.notes) {
    if (note.note == 12 && note.startTick == 672u) {
      TEST_ASSERT_EQUAL(768u, note.endTick);
    }
    if (note.note == 12 && note.startTick == 2400u) {
      TEST_ASSERT_EQUAL(2500u, note.endTick);
    }
    if (note.note == 12 && note.startTick == 2976u) {
      TEST_ASSERT_EQUAL(3071u, note.endTick);
    }
    if (note.note == 12 && note.startTick == 0u) {
      TEST_ASSERT_EQUAL(96u, note.endTick);
    }
  }
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
  loop.rebuildOverdubSourceView(loop.playheadPhaseTick);
  const uint32_t previewRevisionBeforeBegin = loop.capturePreview.revision;
  const uint16_t displayRevisionBeforeBegin = loop.captureDisplayRevision;
  loop.beginCapture(CapturePhase::Overdub, loop.playheadPhaseTick);
  TEST_ASSERT_TRUE(loop.capturePreview.notes.empty());
  TEST_ASSERT_NOT_EQUAL(previewRevisionBeforeBegin, loop.capturePreview.revision);
  TEST_ASSERT_NOT_EQUAL(displayRevisionBeforeBegin, loop.captureDisplayRevision);
  for (const MidiEvent& evt : held) {
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(evt));
  }
  SessionMidiEventVec live;
  loop.capture.store.copyEventsTo(live);
  TEST_ASSERT_EQUAL(1u, live.size());
  TEST_ASSERT_EQUAL(60, live[0].data.noteData.note);
  TEST_ASSERT_EQUAL(1u, loop.capturePreview.notes.size());
  TEST_ASSERT_EQUAL(1u, loop.overdubSessionUndoDepth());
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

  PresentNoteVec presentNotes;
  TEST_ASSERT_TRUE(
      LoopContentResolution::tryResolvePreparedState(300, loop.playbackRevision, presentNotes, nullptr));
  bool sawWrap = false;
  for (const PresentNote& note : presentNotes) {
    if (note.pitch == 72) {
      sawWrap = true;
    }
  }
  TEST_ASSERT_TRUE(sawWrap);

  loop.pushOverdubSessionPass(wrapId, {});
  loop.beginCapture(CapturePhase::Overdub, 777);
  TEST_ASSERT_TRUE(loop.undoOverdubSession());
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));
  presentNotes.clear();
  TEST_ASSERT_TRUE(
      LoopContentResolution::tryResolvePreparedState(300, loop.playbackRevision, presentNotes, nullptr));
  for (const PresentNote& note : presentNotes) {
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

void test_overdub_session_undo_restores_companion_source_on_prepared_lcr() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  seedRecordNote(loop, 0, 480, 60);
  LoopContentResolution::DeviceGateSample sample;
  LoopContentResolution::measureDeviceGate(loop.passes, loop.loopLengthTicks, sample);
  LoopContentResolution::deviceGateComplete(loop.playbackRevision);
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(200, 1, 60, 90, 10)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(400, 1, 60, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubWrap, 0));
  const PassId wrapId = loop.lastCommittedPassId();
  const OverdubPass* wrap = nullptr;
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    if (pass.id == wrapId) {
      wrap = &pass;
      break;
    }
  }
  TEST_ASSERT_NOT_NULL(wrap);
  EditPass hide{};
  hide.passType = EditPassType::Note;
  hide.actionType = EditActionType::Delete;
  hide.targetNoteId = 1;
  const EditPassId hideId =
      loop.saveNoteEditPass(kOverdubCompanionEditPassIndex, std::move(hide), EditPassType::Note);
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, hideId);
  LoopContentResolution::publishPreparedOverdubPass(*wrap, loop.playbackRevision,
                                                    loop.passes.editPasses, EditPassIdList{hideId});
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));

  PresentNoteVec published;
  TEST_ASSERT_TRUE(LoopContentResolution::tryResolvePreparedState(300, loop.playbackRevision,
                                                                  published, nullptr));
  bool sawRecord = false;
  bool sawWrap = false;
  for (const PresentNote& note : published) {
    if (note.noteId == 1) {
      sawRecord = true;
    }
    if (note.noteId == 10) {
      sawWrap = true;
    }
  }
  TEST_ASSERT_FALSE(sawRecord);
  TEST_ASSERT_TRUE(sawWrap);
  OverlapNoteIdSet publishedIds;
  TEST_ASSERT_TRUE(LoopContentResolution::tryCollectPreparedPresentNoteIdsAtTick(
      300, 60, loop.playbackRevision, publishedIds));
  TEST_ASSERT_FALSE(publishedIds.contains(1));
  TEST_ASSERT_TRUE(publishedIds.contains(10));

  loop.pushOverdubSessionPass(wrapId, EditPassIdList{hideId});
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.undoOverdubSession());
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));
  PresentNoteVec undone;
  TEST_ASSERT_TRUE(
      LoopContentResolution::tryResolvePreparedState(300, loop.playbackRevision, undone, nullptr));
  sawRecord = false;
  sawWrap = false;
  for (const PresentNote& note : undone) {
    if (note.noteId == 1) {
      sawRecord = true;
    }
    if (note.noteId == 10) {
      sawWrap = true;
    }
  }
  TEST_ASSERT_TRUE(sawRecord);
  TEST_ASSERT_FALSE(sawWrap);
  OverlapNoteIdSet undoneIds;
  TEST_ASSERT_TRUE(LoopContentResolution::tryCollectPreparedPresentNoteIdsAtTick(
      300, 60, loop.playbackRevision, undoneIds));
  TEST_ASSERT_TRUE(undoneIds.contains(1));
  TEST_ASSERT_FALSE(undoneIds.contains(10));

  SessionMidiEventVec window;
  TEST_ASSERT_TRUE(LoopContentResolution::tryResolvePreparedWindow(
      loop.passes.editPasses, loop.loopLengthTicks, 0, loop.loopLengthTicks, loop.playbackRevision,
      window, nullptr));
  const NoteUtils::DisplayNoteVec notes =
      NoteUtils::reconstructDisplayNotes(window, loop.loopLengthTicks, false, false);
  TEST_ASSERT_TRUE(hasDisplayNote(notes, 60, 0));
  TEST_ASSERT_FALSE(hasDisplayNote(notes, 60, 200));

  TEST_ASSERT_TRUE(loop.redoOverdubSession());
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));
  PresentNoteVec redone;
  TEST_ASSERT_TRUE(
      LoopContentResolution::tryResolvePreparedState(300, loop.playbackRevision, redone, nullptr));
  sawRecord = false;
  sawWrap = false;
  for (const PresentNote& note : redone) {
    if (note.noteId == 1) {
      sawRecord = true;
    }
    if (note.noteId == 10) {
      sawWrap = true;
    }
  }
  TEST_ASSERT_FALSE(sawRecord);
  TEST_ASSERT_TRUE(sawWrap);
  LoopContentResolution::deviceGateReset();
}

void test_rebuild_overdub_source_view_after_publish_includes_wrap_add() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  seedRecordNote(loop, 0, 48, 60);
  LoopContentResolution::DeviceGateSample sample;
  LoopContentResolution::measureDeviceGate(loop.passes, loop.loopLengthTicks, sample);
  LoopContentResolution::deviceGateComplete(loop.playbackRevision);
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(64, 1, 60, 90, 10)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(240, 1, 60, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubWrap, 0));
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

  loop.rebuildOverdubSourceView(0);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  bool foundWrapAdd = false;
  for (const NoteUtils::DisplayNote& note : loop.overdubSourceViewNotes()) {
    if (note.noteId == 10 && note.startTick == 64 && note.endTick == 240) {
      foundWrapAdd = true;
    }
  }
  TEST_ASSERT_TRUE(foundWrapAdd);
  LoopContentResolution::deviceGateReset();
}

void test_overdub_session_undo_rebuilds_source_view_to_match_prepared() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  seedRecordNote(loop, 0, 480, 60);
  LoopContentResolution::DeviceGateSample sample;
  LoopContentResolution::measureDeviceGate(loop.passes, loop.loopLengthTicks, sample);
  LoopContentResolution::deviceGateComplete(loop.playbackRevision);
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  loop.establishOverdubSourceView(0);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(200, 1, 60, 90, 10)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(400, 1, 60, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubWrap, 0));
  const PassId wrapId = loop.lastCommittedPassId();
  const OverdubPass* wrap = nullptr;
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    if (pass.id == wrapId) {
      wrap = &pass;
      break;
    }
  }
  TEST_ASSERT_NOT_NULL(wrap);
  EditPass hide{};
  hide.passType = EditPassType::Note;
  hide.actionType = EditActionType::Delete;
  hide.targetNoteId = 1;
  const EditPassId hideId =
      loop.saveNoteEditPass(kOverdubCompanionEditPassIndex, std::move(hide), EditPassType::Note);
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, hideId);
  LoopContentResolution::publishPreparedOverdubPass(*wrap, loop.playbackRevision,
                                                    loop.passes.editPasses, EditPassIdList{hideId});
  loop.rebuildOverdubSourceView(0);
  loop.pushOverdubSessionPass(wrapId, EditPassIdList{hideId});
  loop.beginCapture(CapturePhase::Overdub, 0);

  auto sourceHas = [&loop](NoteId noteId) {
    for (const NoteUtils::DisplayNote& note : loop.overdubSourceViewNotes()) {
      if (note.noteId == noteId) {
        return true;
      }
    }
    return false;
  };
  auto preparedHas = [&loop](NoteId noteId) {
    PresentNoteVec presentNotes;
    if (!LoopContentResolution::tryResolvePreparedState(300, loop.playbackRevision, presentNotes,
                                                        nullptr)) {
      return false;
    }
    for (const PresentNote& note : presentNotes) {
      if (note.noteId == noteId) {
        return true;
      }
    }
    return false;
  };

  TEST_ASSERT_TRUE(sourceHas(10));
  TEST_ASSERT_FALSE(sourceHas(1));
  TEST_ASSERT_TRUE(preparedHas(10));
  TEST_ASSERT_FALSE(preparedHas(1));

  TEST_ASSERT_TRUE(loop.undoOverdubSession());
  TEST_ASSERT_TRUE(sourceHas(1));
  TEST_ASSERT_FALSE(sourceHas(10));
  TEST_ASSERT_TRUE(preparedHas(1));
  TEST_ASSERT_FALSE(preparedHas(10));

  TEST_ASSERT_TRUE(loop.redoOverdubSession());
  TEST_ASSERT_TRUE(sourceHas(10));
  TEST_ASSERT_FALSE(sourceHas(1));
  TEST_ASSERT_TRUE(preparedHas(10));
  TEST_ASSERT_FALSE(preparedHas(1));
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
  TEST_ASSERT_EQUAL(1u, loop.overdubSessionUndoDepth());
  TEST_ASSERT_TRUE(loop.canUndoOverdubSession());
  TEST_ASSERT_TRUE(loop.undoOverdubSession());
  TEST_ASSERT_FALSE(loop.capture.store.empty());
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
  SessionMidiEventVec liveAfterUndo;
  loop.capture.store.copyEventsTo(liveAfterUndo);
  TEST_ASSERT_EQUAL(2u, liveAfterUndo.size());
  TEST_ASSERT_EQUAL(64, liveAfterUndo[0].data.noteData.note);
  TEST_ASSERT_TRUE(loop.canRedoOverdubSession());
  TEST_ASSERT_TRUE(loop.redoOverdubSession());
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    if (pass.id == wrapId) {
      TEST_ASSERT_EQUAL(CapturePassState::Active, pass.state);
    }
  }
  TEST_ASSERT_EQUAL(1u, loop.overdubSessionUndoDepth());
}

void test_overdub_session_undo_depth_counts_sealed_wraps_only() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 0, 48, 60);
  loop.openOverdubSession(777);
  loop.beginCapture(CapturePhase::Overdub, 777);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(200, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(400, 1, 72, 0)));
  TEST_ASSERT_EQUAL(0u, loop.overdubSessionUndoDepth());
  TEST_ASSERT_EQUAL(1u, loop.overdubSessionDisplayDepth());
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 777));
  loop.pushOverdubSessionPass(loop.lastCommittedPassId(), {});
  loop.beginCapture(CapturePhase::Overdub, 777);
  TEST_ASSERT_EQUAL(1u, loop.overdubSessionUndoDepth());
  TEST_ASSERT_EQUAL(2u, loop.overdubSessionDisplayDepth());
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(500, 1, 64, 90)));
  TEST_ASSERT_EQUAL(1u, loop.overdubSessionUndoDepth());
  TEST_ASSERT_EQUAL(2u, loop.overdubSessionDisplayDepth());
  TEST_ASSERT_TRUE(loop.undoOverdubSession());
  TEST_ASSERT_FALSE(loop.capture.store.empty());
  TEST_ASSERT_EQUAL(0u, loop.overdubSessionUndoDepth());
  TEST_ASSERT_EQUAL(1u, loop.overdubSessionDisplayDepth());
  bool wrapDisabled = false;
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    if (pass.id == loop.lastCommittedPassId()) {
      wrapDisabled = pass.state == CapturePassState::Disabled;
    }
  }
  TEST_ASSERT_TRUE(wrapDisabled);
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
  TEST_ASSERT_EQUAL(0u, loop.overdubSessionUndoDepth());
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

void prepareLcrFromLoop(Loop& loop) {
  LoopContentResolution::deviceGateReset();
  LoopContentResolution::DeviceGateSample sample;
  LoopContentResolution::measureDeviceGate(loop.passes, loop.loopLengthTicks, sample);
  LoopContentResolution::deviceGateComplete(loop.playbackRevision);
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));
}

void preparedWindowNotes(Loop& loop, NoteUtils::DisplayNoteVec& lcrOnly,
                         NoteUtils::DisplayNoteVec& lcrPlusAppend) {
  SessionMidiEventVec flat;
  TEST_ASSERT_TRUE(LoopContentResolution::tryResolvePreparedWindow(
      loop.passes.editPasses, loop.loopLengthTicks, 0, loop.loopLengthTicks, loop.playbackRevision,
      flat, nullptr));
  lcrOnly = NoteUtils::reconstructDisplayNotes(flat, loop.loopLengthTicks, false, false);
  lcrPlusAppend = lcrOnly;
  loop.appendOverdubPassDisplayNotes(lcrPlusAppend);
}

bool sameDisplayGeometry(const NoteUtils::DisplayNoteVec& a, const NoteUtils::DisplayNoteVec& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (const NoteUtils::DisplayNote& note : a) {
    bool found = false;
    for (const NoteUtils::DisplayNote& other : b) {
      if (other.note == note.note && other.startTick == note.startTick &&
          other.endTick == note.endTick) {
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
  }
  return true;
}

void seedLinearOverdubLoop(Loop& loop) {
  seedRecordNote(loop, 10, 58, 60);
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(200, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(400, 1, 72, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubStop, 400));
}

void seedWrapHeldOverdubLoop(Loop& loop) {
  loop.loopLengthTicks = 3072;
  LoopEventStore record;
  TEST_ASSERT_TRUE(storeAppendNoteOn(record, 672, 4, 12, 100, 1));
  TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(768, 4, 12, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(record, 2400, 4, 12, 100, 2));
  TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(2500, 4, 12, 0)));
  loop.seedRecordPassFromStore(record);
  loop.openOverdubSession(1152);
  loop.beginCapture(CapturePhase::Overdub, 1152);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(2976, 4, 12, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(96, 4, 12, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(288, 4, 12, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(384, 4, 12, 0)));
  SessionMidiEventVec held;
  TEST_ASSERT_EQUAL(0u, loop.extractOpenCaptureNoteOns(held));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 1152));
}

void rebuildIdleVisualCache(Loop& loop) {
  loop.markDisplayCachesStale();
  uint8_t slices = 0;
  while (loop.visualCacheDirty && slices < 16) {
    loop.rebuildVisualCacheIdleSlice(4, 0);
    ++slices;
  }
  TEST_ASSERT_FALSE(loop.visualCacheDirty);
}

void test_prepared_linear_overdub_matches_lcr_plus_append() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLinearOverdubLoop(loop);
  prepareLcrFromLoop(loop);
  NoteUtils::DisplayNoteVec lcrOnly;
  NoteUtils::DisplayNoteVec lcrPlusAppend;
  preparedWindowNotes(loop, lcrOnly, lcrPlusAppend);
  TEST_ASSERT_TRUE(hasDisplayNote(lcrOnly, 60, 10));
  TEST_ASSERT_TRUE(hasDisplayNote(lcrOnly, 72, 200));
  TEST_ASSERT_TRUE(sameDisplayGeometry(lcrOnly, lcrPlusAppend));
  LoopContentResolution::deviceGateReset();
}

void test_prepared_wrap_held_overdub_lcr_append_delta() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedWrapHeldOverdubLoop(loop);
  prepareLcrFromLoop(loop);
  NoteUtils::DisplayNoteVec lcrOnly;
  NoteUtils::DisplayNoteVec lcrPlusAppend;
  preparedWindowNotes(loop, lcrOnly, lcrPlusAppend);
  TEST_ASSERT_TRUE(hasDisplayNote(lcrPlusAppend, 12, 2976));
  TEST_ASSERT_TRUE(hasDisplayNote(lcrPlusAppend, 12, 0));
  TEST_ASSERT_TRUE(hasDisplayNote(lcrPlusAppend, 12, 288));
  TEST_ASSERT_TRUE(hasDisplayNote(lcrPlusAppend, 12, 672));
  TEST_ASSERT_TRUE(hasDisplayNote(lcrPlusAppend, 12, 2400));
  TEST_ASSERT_TRUE(hasDisplayNote(lcrOnly, 12, 288));
  TEST_ASSERT_TRUE(hasDisplayNote(lcrOnly, 12, 672));
  TEST_ASSERT_TRUE(hasDisplayNote(lcrOnly, 12, 2400));
  TEST_ASSERT_FALSE(sameDisplayGeometry(lcrOnly, lcrPlusAppend));
  TEST_ASSERT_FALSE(hasDisplayNote(lcrOnly, 12, 2976));
  TEST_ASSERT_FALSE(hasDisplayNote(lcrOnly, 12, 0));
  LoopContentResolution::deviceGateReset();
}

void test_idle_slice_prepared_linear_matches_lcr_only() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLinearOverdubLoop(loop);
  prepareLcrFromLoop(loop);
  NoteUtils::DisplayNoteVec lcrOnly;
  NoteUtils::DisplayNoteVec lcrPlusAppend;
  preparedWindowNotes(loop, lcrOnly, lcrPlusAppend);
  rebuildIdleVisualCache(loop);
  TEST_ASSERT_TRUE(sameDisplayGeometry(loop.visualCache.notes, lcrOnly));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 72, 200));
  LoopContentResolution::deviceGateReset();
}

void test_idle_slice_prepared_interior_keeps_mid_loop_overdub() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = Config::TICKS_PER_BAR * 16;
  LoopEventStore record;
  TEST_ASSERT_TRUE(storeAppendNoteOn(record, 10, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(58, 1, 60, 0)));
  loop.seedRecordPassFromStore(record);
  const uint32_t midOn = Config::TICKS_PER_BAR * 8 + 20;
  const uint32_t midOff = midOn + 80;
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(midOn, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(midOff, 1, 72, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubStop, midOff));
  prepareLcrFromLoop(loop);
  NoteUtils::DisplayNoteVec lcrOnly;
  NoteUtils::DisplayNoteVec lcrPlusAppend;
  preparedWindowNotes(loop, lcrOnly, lcrPlusAppend);
  TEST_ASSERT_TRUE(sameDisplayGeometry(lcrOnly, lcrPlusAppend));
  rebuildIdleVisualCache(loop);
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 60, 10));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 72, midOn));
  LoopContentResolution::deviceGateReset();
}

void test_idle_slice_unprepared_interior_keeps_mid_loop_overdub() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  loop.loopLengthTicks = Config::TICKS_PER_BAR * 16;
  LoopEventStore record;
  TEST_ASSERT_TRUE(storeAppendNoteOn(record, 10, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(58, 1, 60, 0)));
  loop.seedRecordPassFromStore(record);
  const uint32_t midOn = Config::TICKS_PER_BAR * 8 + 20;
  const uint32_t midOff = midOn + 80;
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(midOn, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(midOff, 1, 72, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubStop, midOff));
  TEST_ASSERT_FALSE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));
  rebuildIdleVisualCache(loop);
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 60, 10));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 72, midOn));
  LoopContentResolution::deviceGateReset();
}

void test_idle_slice_unprepared_keeps_wrap_held() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  seedWrapHeldOverdubLoop(loop);
  TEST_ASSERT_FALSE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));
  rebuildIdleVisualCache(loop);
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 12, 2976));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 12, 0));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 12, 288));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 12, 672));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 12, 2400));
  for (const NoteUtils::DisplayNote& note : loop.visualCache.notes) {
    if (note.note == 12 && note.startTick == 672u) {
      TEST_ASSERT_EQUAL(768u, note.endTick);
    }
    if (note.note == 12 && note.startTick == 2400u) {
      TEST_ASSERT_EQUAL(2500u, note.endTick);
    }
    if (note.note == 12 && note.startTick == 2976u) {
      TEST_ASSERT_EQUAL(3071u, note.endTick);
    }
    if (note.note == 12 && note.startTick == 0u) {
      TEST_ASSERT_EQUAL(96u, note.endTick);
    }
  }
  LoopContentResolution::deviceGateReset();
}

void test_idle_slice_keeps_untouched_next_bar_note() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  loop.loopLengthTicks = Config::TICKS_PER_BAR * 16;
  LoopEventStore record;
  const uint32_t bar8On = Config::TICKS_PER_BAR * 8 + 10;
  const uint32_t bar9On = Config::TICKS_PER_BAR * 9 + 10;
  TEST_ASSERT_TRUE(storeAppendNoteOn(record, bar8On, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(bar8On + 40, 1, 60, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(record, bar9On, 1, 60, 100, 2));
  TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(bar9On + 80, 1, 60, 0)));
  loop.seedRecordPassFromStore(record);
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 60, bar9On));
  const uint32_t midOn = Config::TICKS_PER_BAR * 8 + 200;
  const uint32_t midOff = midOn + 80;
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(midOn, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(midOff, 1, 72, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubStop, midOff));
  loop.rebuildVisualCacheFromPasses();
  loop.visualCache.dirtyBars.clear();
  loop.visualCacheDirty = false;
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 60, bar9On));
  for (NoteUtils::DisplayNote& note : loop.visualCache.notes) {
    if (note.note == 60 && note.startTick == bar9On) {
      note.endTick = 12345;
    }
  }
  loop.markAffectedDisplayCacheRanges(loop.lastCommittedPassId(), EditPassIdList{});
  TEST_ASSERT_TRUE(loop.visualCacheDirty);
  TEST_ASSERT_EQUAL(1, loop.visualCache.dirtyBars[8]);
  TEST_ASSERT_EQUAL(0, loop.visualCache.dirtyBars[9]);
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 60, bar9On));
  uint8_t slices = 0;
  while (loop.visualCacheDirty && slices < 16) {
    loop.rebuildVisualCacheIdleSlice(4, 0);
    ++slices;
  }
  TEST_ASSERT_FALSE(loop.visualCacheDirty);
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 60, bar8On));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 60, bar9On));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 72, midOn));
  for (const NoteUtils::DisplayNote& note : loop.visualCache.notes) {
    if (note.note == 60 && note.startTick == bar9On) {
      TEST_ASSERT_EQUAL(12345u, note.endTick);
    }
  }
  LoopContentResolution::deviceGateReset();
}

void test_idle_slice_prepared_matches_lcr_without_append_wrap_held() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedWrapHeldOverdubLoop(loop);
  prepareLcrFromLoop(loop);
  NoteUtils::DisplayNoteVec lcrOnly;
  NoteUtils::DisplayNoteVec lcrPlusAppend;
  preparedWindowNotes(loop, lcrOnly, lcrPlusAppend);
  rebuildIdleVisualCache(loop);
  TEST_ASSERT_TRUE(sameDisplayGeometry(loop.visualCache.notes, lcrOnly));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 12, 672));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 12, 2400));
  TEST_ASSERT_FALSE(hasDisplayNote(loop.visualCache.notes, 12, 2976));
  TEST_ASSERT_FALSE(hasDisplayNote(loop.visualCache.notes, 12, 0));
  LoopContentResolution::deviceGateReset();
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

void test_retire_superseded_pitch_drops_home_when_settled_present() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = kLoopLen;
  NoteUtils::DisplayNote home{};
  home.note = 62;
  home.startTick = 296;
  home.endTick = 416;
  home.noteId = 352;
  NoteUtils::DisplayNote settled{};
  settled.note = 79;
  settled.startTick = 296;
  settled.endTick = 416;
  settled.noteId = 358;
  loop.visualCache.notes.push_back(home);
  loop.visualCache.notes.push_back(settled);
  loop.retireSupersededPitchDisplayNote(62, 296, 416, 79);
  TEST_ASSERT_FALSE(hasDisplayNote(loop.visualCache.notes, 62, 296));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 79, 296));
  TEST_ASSERT_EQUAL(1, countDisplayNotesAtStartTick(loop.visualCache.notes, 296));
}

void test_retire_superseded_pitch_keeps_same_start_sibling_end() {
  // 152627: two 60s at tick 64 (176 + 224). Persist rematerialized the mover home to 288.
  // Settled 72 is at 64 with a different end than home. Drop 288, keep sibling 224.
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = kLoopLen;
  NoteUtils::DisplayNote sibling{};
  sibling.note = 60;
  sibling.startTick = 64;
  sibling.endTick = 224;
  sibling.noteId = 403;
  NoteUtils::DisplayNote rematerializedHome{};
  rematerializedHome.note = 60;
  rematerializedHome.startTick = 64;
  rematerializedHome.endTick = 288;
  rematerializedHome.noteId = 404;
  NoteUtils::DisplayNote settled{};
  settled.note = 72;
  settled.startTick = 64;
  settled.endTick = 176;
  settled.noteId = 410;
  loop.visualCache.notes.push_back(sibling);
  loop.visualCache.notes.push_back(rematerializedHome);
  loop.visualCache.notes.push_back(settled);
  const uint32_t retainedEnds[] = {224};
  loop.retireSupersededPitchDisplayNote(60, 64, 176, 72, retainedEnds, 1);
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 72, 64));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 60, 64));
  TEST_ASSERT_EQUAL(2, countDisplayNotesAtStartTick(loop.visualCache.notes, 64));
  bool keptSibling = false;
  bool keptGhost = false;
  for (const NoteUtils::DisplayNote& note : loop.visualCache.notes) {
    if (note.note != 60 || note.startTick != 64) {
      continue;
    }
    if (note.endTick == 224) {
      keptSibling = true;
    }
    if (note.endTick == 288) {
      keptGhost = true;
    }
  }
  TEST_ASSERT_TRUE(keptSibling);
  TEST_ASSERT_FALSE(keptGhost);
}

void pinHoldSetsAfterWrap(int wrapIndex, Loop& loop, uint32_t holdTick, uint8_t pitch,
                          OverlapNoteIdSet& sourceIds, OverlapNoteIdSet& preparedIds,
                          OverlapNoteIdSet& onlyA, OverlapNoteIdSet& onlyB) {
  sourceIds.clear();
  preparedIds.clear();
  onlyA.clear();
  onlyB.clear();
  collectHoldParticipantSets(loop, holdTick, pitch, sourceIds, preparedIds);
  for (size_t i = 0; i < sourceIds.size(); ++i) {
    const NoteId id = sourceIds.at(i);
    if (!preparedIds.contains(id)) {
      (void)onlyA.insert(id);
    }
  }
  for (size_t i = 0; i < preparedIds.size(); ++i) {
    const NoteId id = preparedIds.at(i);
    if (!sourceIds.contains(id)) {
      (void)onlyB.insert(id);
    }
  }
  char aBuf[64];
  char bBuf[64];
  char aoBuf[64];
  char boBuf[64];
  formatNoteIds(sourceIds, aBuf, sizeof(aBuf));
  formatNoteIds(preparedIds, bBuf, sizeof(bBuf));
  formatNoteIds(onlyA, aoBuf, sizeof(aoBuf));
  formatNoteIds(onlyB, boBuf, sizeof(boBuf));
  printf("wrap %d hold=%u a=%u b=%u eq=%d ao=%u bo=%u A=[%s] B=[%s] onlyA=[%s] onlyB=[%s]\n",
         wrapIndex, static_cast<unsigned>(holdTick), static_cast<unsigned>(sourceIds.size()),
         static_cast<unsigned>(preparedIds.size()), sourceIds == preparedIds ? 1 : 0,
         static_cast<unsigned>(onlyA.size()), static_cast<unsigned>(onlyB.size()), aBuf, bBuf,
         aoBuf, boBuf);
}

// 013327 device holds grew B extras after wrap 2. This fixture is that occupy
// geometry (same-start longer Hide) on the wrap-publish path. It records the
// actual A/B NoteIds: hide bake finds wrap-N Add, so A IDs == B IDs after each
// wrap. No unbaked hide, wrap-local-only span, restore, or duplicate extra.
void test_prepared_hold_ids_pin_b_extras_after_same_pitch_wraps() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  seedRecordNote(loop, 64, 176, 60);
  LoopContentResolution::DeviceGateSample sample;
  LoopContentResolution::measureDeviceGate(loop.passes, loop.loopLengthTicks, sample);
  LoopContentResolution::deviceGateComplete(loop.playbackRevision);
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  loop.establishOverdubSourceView(0);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());

  constexpr uint32_t kHoldTick = 100;
  constexpr uint8_t kPitch = 60;
  OverlapNoteIdSet occupy;
  loop.collectOverdubSourceHoldParticipantIds(kHoldTick, kPitch, occupy);
  TEST_ASSERT_TRUE(occupy.contains(1));
  commitSamePitchWrapAndPublish(loop, 10, 64, 240, occupy);

  OverlapNoteIdSet a1;
  OverlapNoteIdSet b1;
  OverlapNoteIdSet ao1;
  OverlapNoteIdSet bo1;
  pinHoldSetsAfterWrap(1, loop, kHoldTick, kPitch, a1, b1, ao1, bo1);
  TEST_ASSERT_TRUE(a1 == b1);
  TEST_ASSERT_EQUAL(0u, ao1.size());
  TEST_ASSERT_EQUAL(0u, bo1.size());
  TEST_ASSERT_TRUE(a1.contains(10));
  TEST_ASSERT_FALSE(a1.contains(1));

  occupy.clear();
  loop.collectOverdubSourceHoldParticipantIds(kHoldTick, kPitch, occupy);
  TEST_ASSERT_TRUE(occupy.contains(10));
  commitSamePitchWrapAndPublish(loop, 11, 64, 288, occupy);

  OverlapNoteIdSet a2;
  OverlapNoteIdSet b2;
  OverlapNoteIdSet ao2;
  OverlapNoteIdSet bo2;
  pinHoldSetsAfterWrap(2, loop, kHoldTick, kPitch, a2, b2, ao2, bo2);
  TEST_ASSERT_TRUE(a2.contains(11));
  TEST_ASSERT_FALSE(a2.contains(10));
  TEST_ASSERT_TRUE(a2 == b2);
  TEST_ASSERT_EQUAL(0u, ao2.size());
  TEST_ASSERT_EQUAL(0u, bo2.size());
  TEST_ASSERT_FALSE(sourceViewHasNoteId(loop, 10));
  TEST_ASSERT_TRUE(editPassesHideTarget(loop, 10));

  occupy.clear();
  loop.collectOverdubSourceHoldParticipantIds(kHoldTick, kPitch, occupy);
  TEST_ASSERT_TRUE(occupy.contains(11));
  commitSamePitchWrapAndPublish(loop, 12, 64, 336, occupy);

  OverlapNoteIdSet a3;
  OverlapNoteIdSet b3;
  OverlapNoteIdSet ao3;
  OverlapNoteIdSet bo3;
  pinHoldSetsAfterWrap(3, loop, kHoldTick, kPitch, a3, b3, ao3, bo3);
  TEST_ASSERT_TRUE(a3.contains(12));
  TEST_ASSERT_FALSE(a3.contains(11));
  TEST_ASSERT_TRUE(a3 == b3);
  TEST_ASSERT_EQUAL(0u, ao3.size());
  TEST_ASSERT_EQUAL(0u, bo3.size());
  TEST_ASSERT_FALSE(sourceViewHasNoteId(loop, 11));
  TEST_ASSERT_TRUE(editPassesHideTarget(loop, 11));
  TEST_ASSERT_TRUE(editPassesHideTarget(loop, 10));
  LoopContentResolution::deviceGateReset();
}

// 013327 majority extras are a=0,b>0 at storage tick 64 after wrap undo.
// Same occupy geometry as the wrap-publish pin; collect at 64 after wrap 2 and
// after session undo. Records the actual A/B NoteIds.
void test_prepared_hold_ids_pin_b_extras_at_tick64_after_wrap_undo() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  seedRecordNote(loop, 64, 176, 60);
  LoopContentResolution::DeviceGateSample sample;
  LoopContentResolution::measureDeviceGate(loop.passes, loop.loopLengthTicks, sample);
  LoopContentResolution::deviceGateComplete(loop.playbackRevision);
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  loop.establishOverdubSourceView(0);

  constexpr uint32_t kTick64 = 64;
  constexpr uint32_t kTick100 = 100;
  constexpr uint8_t kPitch = 60;
  OverlapNoteIdSet occupy;
  loop.collectOverdubSourceHoldParticipantIds(kTick64, kPitch, occupy);
  TEST_ASSERT_TRUE(occupy.contains(1));
  const EditPassIdList wrap1Companions =
      commitSamePitchWrapAndPublish(loop, 10, 64, 240, occupy);
  loop.pushOverdubSessionPass(loop.lastCommittedPassId(), wrap1Companions);

  OverlapNoteIdSet a64w1;
  OverlapNoteIdSet b64w1;
  OverlapNoteIdSet ao64w1;
  OverlapNoteIdSet bo64w1;
  pinHoldSetsAfterWrap(1, loop, kTick64, kPitch, a64w1, b64w1, ao64w1, bo64w1);
  TEST_ASSERT_TRUE(a64w1 == b64w1);
  TEST_ASSERT_TRUE(a64w1.contains(10));
  TEST_ASSERT_EQUAL(0u, bo64w1.size());

  occupy.clear();
  loop.collectOverdubSourceHoldParticipantIds(kTick64, kPitch, occupy);
  TEST_ASSERT_TRUE(occupy.contains(10));
  const EditPassIdList wrap2Companions =
      commitSamePitchWrapAndPublish(loop, 11, 64, 288, occupy);
  loop.pushOverdubSessionPass(loop.lastCommittedPassId(), wrap2Companions);

  OverlapNoteIdSet a64w2;
  OverlapNoteIdSet b64w2;
  OverlapNoteIdSet ao64w2;
  OverlapNoteIdSet bo64w2;
  pinHoldSetsAfterWrap(2, loop, kTick64, kPitch, a64w2, b64w2, ao64w2, bo64w2);
  OverlapNoteIdSet a100w2;
  OverlapNoteIdSet b100w2;
  OverlapNoteIdSet ao100w2;
  OverlapNoteIdSet bo100w2;
  pinHoldSetsAfterWrap(2, loop, kTick100, kPitch, a100w2, b100w2, ao100w2, bo100w2);
  TEST_ASSERT_TRUE(a64w2 == b64w2);
  TEST_ASSERT_TRUE(a64w2.contains(11));
  TEST_ASSERT_EQUAL(0u, bo64w2.size());
  TEST_ASSERT_TRUE(a100w2 == b100w2);
  TEST_ASSERT_EQUAL(0u, bo100w2.size());

  TEST_ASSERT_TRUE(loop.undoOverdubSession());
  OverlapNoteIdSet a64u;
  OverlapNoteIdSet b64u;
  OverlapNoteIdSet ao64u;
  OverlapNoteIdSet bo64u;
  pinHoldSetsAfterWrap(0, loop, kTick64, kPitch, a64u, b64u, ao64u, bo64u);
  OverlapNoteIdSet a100u;
  OverlapNoteIdSet b100u;
  OverlapNoteIdSet ao100u;
  OverlapNoteIdSet bo100u;
  pinHoldSetsAfterWrap(0, loop, kTick100, kPitch, a100u, b100u, ao100u, bo100u);
  printf("undo tick64 A=[");
  char aBuf[64];
  char bBuf[64];
  formatNoteIds(a64u, aBuf, sizeof(aBuf));
  formatNoteIds(b64u, bBuf, sizeof(bBuf));
  printf("%s] B=[%s] ao=%u bo=%u\n", aBuf, bBuf, static_cast<unsigned>(ao64u.size()),
         static_cast<unsigned>(bo64u.size()));
  TEST_ASSERT_TRUE(a64u == b64u);
  TEST_ASSERT_TRUE(a100u == b100u);
  TEST_ASSERT_EQUAL(0u, ao64u.size());
  TEST_ASSERT_EQUAL(0u, bo64u.size());
  TEST_ASSERT_TRUE(a64u.contains(10));
  TEST_ASSERT_FALSE(a64u.contains(11));
  LoopContentResolution::deviceGateReset();
}

// 013327 a=0,b>0 at storage 64: two same-pitch notes cover 64; occupy only one
// (device max_ids=1). Wrap same-start longer. Pin leftover B ids at 64.
void test_prepared_hold_ids_pin_b_extra_when_occupy_misses_sibling_at_64() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  seedTwoRecordNotes(loop, 0, 80, 1, 64, 176, 2, 60);
  LoopContentResolution::DeviceGateSample sample;
  LoopContentResolution::measureDeviceGate(loop.passes, loop.loopLengthTicks, sample);
  LoopContentResolution::deviceGateComplete(loop.playbackRevision);
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  loop.establishOverdubSourceView(0);

  OverlapNoteIdSet before;
  loop.collectOverdubSourceHoldParticipantIds(64, 60, before);
  printf("sibling-at-64 before occupy a=%u ids=", static_cast<unsigned>(before.size()));
  char beforeBuf[64];
  formatNoteIds(before, beforeBuf, sizeof(beforeBuf));
  printf("%s\n", beforeBuf);

  const OverlapNoteIdSet occupyOne = overlapIds({1});
  (void)commitSamePitchWrapAndPublish(loop, 10, 64, 240, occupyOne);

  OverlapNoteIdSet a64;
  OverlapNoteIdSet b64;
  OverlapNoteIdSet ao64;
  OverlapNoteIdSet bo64;
  pinHoldSetsAfterWrap(1, loop, 64, 60, a64, b64, ao64, bo64);
  char aBuf[64];
  char bBuf[64];
  char boBuf[64];
  formatNoteIds(a64, aBuf, sizeof(aBuf));
  formatNoteIds(b64, bBuf, sizeof(bBuf));
  formatNoteIds(bo64, boBuf, sizeof(boBuf));
  printf("sibling-at-64 after wrap A=[%s] B=[%s] onlyB=[%s]\n", aBuf, bBuf, boBuf);

  TEST_ASSERT_EQUAL_UINT(0, ao64.size());
  TEST_ASSERT_EQUAL_UINT(1, a64.size());
  TEST_ASSERT_EQUAL_UINT(1, b64.size());
  TEST_ASSERT_TRUE(a64.contains(10));
  TEST_ASSERT_TRUE(b64.contains(10));
  TEST_ASSERT_EQUAL_UINT(0, bo64.size());
  LoopContentResolution::deviceGateReset();
}

// 013327 a=0 at storage 64. Wrap-local reconstruct of a wrap-crossing pair
// (on@2976 off@96) can cover 64; prepared-window reconstruct of record+wrap
// omits that pair (015618). Empty occupy — record does not cover 64.
void test_prepared_hold_ids_pin_b_extra_wrap_crossing_covers_64() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  loop.loopLengthTicks = 3072;
  LoopEventStore record;
  TEST_ASSERT_TRUE(storeAppendNoteOn(record, 672, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(768, 1, 60, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(record, 2400, 1, 60, 100, 2));
  TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(2500, 1, 60, 0)));
  loop.seedRecordPassFromStore(record);
  LoopContentResolution::DeviceGateSample sample;
  LoopContentResolution::measureDeviceGate(loop.passes, loop.loopLengthTicks, sample);
  LoopContentResolution::deviceGateComplete(loop.playbackRevision);
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  loop.establishOverdubSourceView(0);
  OverlapNoteIdSet occupy;
  loop.collectOverdubSourceHoldParticipantIds(64, 60, occupy);
  TEST_ASSERT_EQUAL(0u, occupy.size());

  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(2976, 1, 60, 90, 10)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(96, 1, 60, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubWrap, 0));
  const OverdubPass* wrap = findOverdubPass(loop, loop.lastCommittedPassId());
  TEST_ASSERT_NOT_NULL(wrap);
  LoopContentResolution::publishPreparedOverdubPass(*wrap, loop.playbackRevision,
                                                    loop.passes.editPasses, EditPassIdList{});
  loop.rebuildOverdubSourceView(0);

  OverlapNoteIdSet a64;
  OverlapNoteIdSet b64;
  OverlapNoteIdSet ao64;
  OverlapNoteIdSet bo64;
  pinHoldSetsAfterWrap(1, loop, 64, 60, a64, b64, ao64, bo64);

  TEST_ASSERT_EQUAL(0u, a64.size());
  TEST_ASSERT_EQUAL(0u, ao64.size());
  TEST_ASSERT_TRUE(a64 != b64);
  TEST_ASSERT_TRUE(bo64.contains(10));
  TEST_ASSERT_FALSE(sourceViewHasNoteId(loop, 10));

  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(3000, 1, 60, 90, 11)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(80, 1, 60, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubWrap, 0));
  const OverdubPass* wrap2 = findOverdubPass(loop, loop.lastCommittedPassId());
  TEST_ASSERT_NOT_NULL(wrap2);
  LoopContentResolution::publishPreparedOverdubPass(*wrap2, loop.playbackRevision,
                                                    loop.passes.editPasses, EditPassIdList{});
  loop.rebuildOverdubSourceView(0);

  OverlapNoteIdSet a64w2;
  OverlapNoteIdSet b64w2;
  OverlapNoteIdSet ao64w2;
  OverlapNoteIdSet bo64w2;
  pinHoldSetsAfterWrap(2, loop, 64, 60, a64w2, b64w2, ao64w2, bo64w2);
  TEST_ASSERT_EQUAL(0u, a64w2.size());
  TEST_ASSERT_EQUAL(0u, ao64w2.size());
  TEST_ASSERT_TRUE(bo64w2.contains(10));
  TEST_ASSERT_TRUE(bo64w2.contains(11));
  TEST_ASSERT_TRUE(bo64w2.size() > bo64.size());
  LoopContentResolution::deviceGateReset();
}

void test_undo_overdub_idle_refresh_restores_record_layer() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 64, 240, 60);
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(296, 1, 62, 100, 10)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(416, 1, 62, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubWrap, 0));
  const PassId overdubId = loop.lastCommittedPassId();
  loop.rebuildVisualCacheFromPasses();
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 60, 64));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 62, 296));

  TEST_ASSERT_TRUE(loop.setCapturePassState(overdubId, CapturePassState::Disabled));
  loop.refreshVisualCacheAfterPassStateChange();
  TEST_ASSERT_FALSE(loop.visualCacheDirty);
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 60, 64));
  TEST_ASSERT_FALSE(hasDisplayNote(loop.visualCache.notes, 62, 296));
  SessionMidiEventVec flat;
  loop.gatherCommittedEvents(flat);
  const NoteUtils::DisplayNoteVec materialized =
      NoteUtils::reconstructDisplayNotes(flat, loop.loopLengthTicks, false, false);
  TEST_ASSERT_EQUAL(materialized.size(), loop.visualCache.notes.size());
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_pitch_edit_rebuild_shows_only_new_pitch_at_tick);
  RUN_TEST(test_disable_edit_pass_rebuild_restores_pre_edit_display);
  RUN_TEST(test_overdub_start_establishes_source_view);
  RUN_TEST(test_overdub_enter_rebuilds_source_view_not_visual_cache);
  RUN_TEST(test_overdub_begin_makes_restore_flatten_unreachable);
  RUN_TEST(test_short_loop_stale_keeps_notes_for_overdub_stop_handoff);
  RUN_TEST(test_short_loop_idle_slice_cleans_without_full_rebuild);
  RUN_TEST(test_note_edit_idle_paint_consumes_stale_visual_cache);
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
  RUN_TEST(test_extract_keeps_tick0_wrap_held_pair_when_pitch_replays);
  RUN_TEST(test_extract_keeps_head_off_wrap_held_pair_when_pitch_replays);
  RUN_TEST(test_visual_cache_keeps_overdub_wrap_held_without_stretching_record);
  RUN_TEST(test_empty_wrap_does_not_commit_a_pass);
  RUN_TEST(test_wrap_commit_publishes_completed_pair_and_keeps_held);
  RUN_TEST(test_overdub_session_undo_hides_wrap_from_prepared_lcr);
  RUN_TEST(test_overdub_session_undo_restores_companion_source_on_prepared_lcr);
  RUN_TEST(test_rebuild_overdub_source_view_after_publish_includes_wrap_add);
  RUN_TEST(test_overdub_session_undo_rebuilds_source_view_to_match_prepared);
  RUN_TEST(test_overdub_session_undo_disables_sealed_wrap);
  RUN_TEST(test_overdub_session_undo_depth_counts_sealed_wraps_only);
  RUN_TEST(test_session_undo_skips_next_wrap_crossing);
  RUN_TEST(test_stop_collects_session_wraps_then_close_clears_stack);
  RUN_TEST(test_should_commit_overdub_wrap_after_leaving_start);
  RUN_TEST(test_retire_superseded_pitch_drops_home_when_settled_present);
  RUN_TEST(test_retire_superseded_pitch_keeps_same_start_sibling_end);
  RUN_TEST(test_undo_overdub_idle_refresh_restores_record_layer);
  RUN_TEST(test_prepared_hold_ids_pin_b_extras_after_same_pitch_wraps);
  RUN_TEST(test_prepared_hold_ids_pin_b_extras_at_tick64_after_wrap_undo);
  RUN_TEST(test_prepared_hold_ids_pin_b_extra_when_occupy_misses_sibling_at_64);
  RUN_TEST(test_prepared_hold_ids_pin_b_extra_wrap_crossing_covers_64);
  RUN_TEST(test_prepared_linear_overdub_matches_lcr_plus_append);
  RUN_TEST(test_prepared_wrap_held_overdub_lcr_append_delta);
  RUN_TEST(test_idle_slice_prepared_linear_matches_lcr_only);
  RUN_TEST(test_idle_slice_prepared_interior_keeps_mid_loop_overdub);
  RUN_TEST(test_idle_slice_prepared_matches_lcr_without_append_wrap_held);
  RUN_TEST(test_idle_slice_unprepared_interior_keeps_mid_loop_overdub);
  RUN_TEST(test_idle_slice_unprepared_keeps_wrap_held);
  RUN_TEST(test_idle_slice_keeps_untouched_next_bar_note);
  return UNITY_END();
}
