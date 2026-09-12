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
#define LOOP_PENDING_NOTE_CHANGE_PROVIDES_DISPLAY_APPLY
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
#include "NoteEditCurrentState.h"
#include "PendingNoteChange.h"
#include "MidiEvent.h"
#include "Globals.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/NoteUtils.h"
#include "Utils/SnapshotNoteConversion.h"
#include "OverlapNoteIdObservation.h"
#include "LoopSnapshot.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <array>

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

bool hasDuplicatePitchStartNoteId(const NoteUtils::DisplayNoteVec& notes) {
  for (size_t i = 0; i < notes.size(); ++i) {
    for (size_t j = i + 1; j < notes.size(); ++j) {
      if (notes[i].note == notes[j].note && notes[i].startTick == notes[j].startTick &&
          notes[i].noteId == notes[j].noteId) {
        return true;
      }
    }
  }
  return false;
}

uint32_t countVisualDirtyBars(const VisualBarVec& dirtyBars) {
  uint32_t count = 0;
  for (uint8_t flag : dirtyBars) {
    if (flag != 0) {
      ++count;
    }
  }
  return count;
}

const NoteUtils::DisplayNote* findDisplayNoteById(const NoteUtils::DisplayNoteVec& notes,
                                                  NoteId noteId) {
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.noteId == noteId) {
      return &note;
    }
  }
  return nullptr;
}

void seedDenseWindowNotes(Loop& loop, uint32_t noteCount, uint32_t loopLength) {
  loop.loopLengthTicks = loopLength;
  LoopEventStore store;
  for (uint32_t i = 0; i < noteCount; ++i) {
    const uint32_t onTick = i * 80u;
    const NoteId id = static_cast<NoteId>(i + 1);
    TEST_ASSERT_TRUE(storeAppendNoteOn(store, onTick, 1, 60, 100, id));
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(onTick + 40u, 1, 60, 0)));
  }
  loop.seedRecordPassFromStore(store);
  loop.nextNoteId_ = static_cast<NoteId>(noteCount + 1);
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
      tick, pitch, loop.playbackRevision, loop.loopLengthTicks, preparedIds));
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
                                                    loop.loopLengthTicks, loop.passes.editPasses,
                                                    companions);
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

void test_session_open_without_prewarm_falls_back_when_visual_cache_empty() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);
  loop.visualCache.notes.clear();
  loop.visualCache.dirtyBars.clear();
  loop.visualCacheDirty = true;

  TEST_ASSERT_FALSE(loop.overdubSourceSpanCacheReadyForTest());
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.overdubSourceSpanCacheReadyForTest());
  TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 10));
}

void test_session_open_without_prewarm_uses_clean_visual_cache_seed() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);
  loop.rebuildVisualCacheFromPasses();
  TEST_ASSERT_FALSE(loop.visualCacheDirty);
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 60, 10));

  TEST_ASSERT_FALSE(loop.overdubSourceSpanCacheReadyForTest());
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.overdubSourceSpanCacheReadyForTest());
  TEST_ASSERT_FALSE(loop.overdubSourceViewNotes().empty());
  TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 10));
}

void test_session_open_without_prewarm_falls_back_from_dirty_visual_cache() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  seedTwoRecordNotes(loop, 10, 58, 1, 100, 148, 2, 60);
  loop.rebuildVisualCacheFromPasses();
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 60, 10));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 60, 100));
  loop.markDisplayCachesStale();
  TEST_ASSERT_TRUE(loop.visualCacheDirty);
  for (auto it = loop.visualCache.notes.begin(); it != loop.visualCache.notes.end(); ++it) {
    if (it->startTick == 100) {
      loop.visualCache.notes.erase(it);
      break;
    }
  }
  TEST_ASSERT_FALSE(hasDisplayNote(loop.visualCache.notes, 60, 100));

  TEST_ASSERT_FALSE(loop.overdubSourceSpanCacheReadyForTest());
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.overdubSourceSpanCacheReadyForTest());
  TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 10));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 100));
  LoopContentResolution::deviceGateReset();
}

void test_source_view_rejects_partial_prior_span_cache() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  constexpr uint32_t kBar = Config::TICKS_PER_BAR;
  constexpr uint32_t kLongLoop = kBar * 20;
  Loop loop;
  loop.loopLengthTicks = kLongLoop;
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, kBar, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(kBar + 48, 1, 60, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, kBar * 15, 1, 60, 100, 2));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(kBar * 15 + 48, 1, 60, 0)));
  loop.seedRecordPassFromStore(store);

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  NoteUtils::DisplayNoteVec partial;
  NoteUtils::DisplayNote partialNote{};
  partialNote.noteId = 1;
  partialNote.note = 60;
  partialNote.startTick = kBar;
  partialNote.endTick = kBar + 48;
  partial.push_back(partialNote);
  loop.replaceOverdubSourceViewNotesForTest(std::move(partial));
  ++loop.playbackRevision;

  loop.rebuildOverdubSourceView(kBar * 14);
  TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, kBar * 15));
  LoopContentResolution::deviceGateReset();
}

void test_overdub_restamp_miss_reopens_from_visual_cache() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);
  loop.rebuildVisualCacheFromPasses();
  TEST_ASSERT_FALSE(loop.visualCacheDirty);
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);

  constexpr uint32_t kDenseNoteCount = 270u;  // > restamp event budget when paired
  constexpr uint32_t kDenseStartTick = 200u;
  constexpr uint32_t kDenseStepTicks = 8u;
  constexpr uint32_t kDenseLengthTicks = 2u;
  NoteId nextNoteId = 10u;
  for (uint32_t i = 0; i < kDenseNoteCount; ++i) {
    const uint32_t onTick = kDenseStartTick + (i * kDenseStepTicks);
    const uint8_t pitch = static_cast<uint8_t>(64u + (i % 12u));
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(onTick, 1, pitch, 90, nextNoteId++)));
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(onTick + kDenseLengthTicks, 1, pitch, 0)));
  }
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubWrap, 0));
  loop.rebuildVisualCacheFromPasses();
  TEST_ASSERT_FALSE(loop.visualCacheDirty);
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 64, kDenseStartTick));

  loop.clearOverdubSourceView();
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_FALSE(loop.overdubSourceViewNotes().empty());
  TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), 64, kDenseStartTick));
}

void test_prewarm_span_cache_then_session_open_has_source_notes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);

  TEST_ASSERT_FALSE(loop.visualCacheDirty);
  TEST_ASSERT_FALSE(loop.overdubSourceSpanCacheReadyForTest());
  loop.prewarmOverdubSourceSpanCache();
  TEST_ASSERT_TRUE(loop.overdubSourceSpanCacheReadyForTest());

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_FALSE(loop.overdubSourceViewNotes().empty());
  TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 10));
}

void test_note_edit_shorten_clips_overdub_source_view_134549() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = Config::TICKS_PER_BAR * 3;
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 2102, 1, 48, 100, 58));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(2212, 1, 48, 0)));
  loop.seedRecordPassFromStore(store);

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  const NoteUtils::DisplayNote* before = findDisplayNoteById(loop.overdubSourceViewNotes(), 58);
  TEST_ASSERT_NOT_NULL(before);
  TEST_ASSERT_EQUAL_UINT32(2102u, before->startTick);
  TEST_ASSERT_EQUAL_UINT32(2212u, before->endTick);

  NoteEditCurrentState currentState;
  currentState.upsertRow(58, {48, 100, 2102, 2212}, {48, 100, 2102, 2111},
                         NoteEditPresenceType::Visible);
  loop.applyNoteEditCurrentStateToOverdubSourceView(currentState);

  const NoteUtils::DisplayNote* after = findDisplayNoteById(loop.overdubSourceViewNotes(), 58);
  TEST_ASSERT_NOT_NULL(after);
  TEST_ASSERT_EQUAL_UINT32(2102u, after->startTick);
  TEST_ASSERT_EQUAL_UINT32(2111u, after->endTick);
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

void test_idle_empty_dirty_bars_keeps_notes_without_marking_all() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kSixteenBars = Config::TICKS_PER_BAR * 16;
  loop.loopLengthTicks = kSixteenBars;
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 10, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(58, 1, 60, 0)));
  const uint32_t bar7On = Config::TICKS_PER_BAR * 7 + 10;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, bar7On, 1, 62, 100, 2));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(bar7On + 40, 1, 62, 0)));
  loop.seedRecordPassFromStore(store);
  TEST_ASSERT_FALSE(loop.visualCache.notes.empty());
  loop.visualCache.dirtyBars.clear();
  loop.visualCacheDirty = true;
  const size_t notesBefore = loop.visualCache.notes.size();

  loop.rebuildVisualCacheIdleSlice(4, 0, UINT32_MAX, false);

  TEST_ASSERT_EQUAL(notesBefore, loop.visualCache.notes.size());
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 60, 10));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 62, bar7On));
  TEST_ASSERT_TRUE(loop.visualCache.dirtyBars.empty());
  TEST_ASSERT_FALSE(loop.visualCacheDirty);
}

void test_overdub_wrap_keeps_clean_cache_notes_and_marks_wrap_span() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kSixteenBars = Config::TICKS_PER_BAR * 16;
  loop.loopLengthTicks = kSixteenBars;
  LoopEventStore record;
  TEST_ASSERT_TRUE(storeAppendNoteOn(record, 10, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(58, 1, 60, 0)));
  const uint32_t bar7On = Config::TICKS_PER_BAR * 7 + 10;
  TEST_ASSERT_TRUE(storeAppendNoteOn(record, bar7On, 1, 62, 100, 2));
  TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(bar7On + 40, 1, 62, 0)));
  const uint32_t bar15On = Config::TICKS_PER_BAR * 15 + 10;
  TEST_ASSERT_TRUE(storeAppendNoteOn(record, bar15On, 1, 64, 100, 3));
  TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(bar15On + 40, 1, 64, 0)));
  loop.seedRecordPassFromStore(record);
  loop.rebuildVisualCacheFromPasses();
  loop.visualCache.dirtyBars.clear();
  loop.visualCacheDirty = false;
  const size_t notesBefore = loop.visualCache.notes.size();
  TEST_ASSERT_EQUAL(3u, notesBefore);

  const uint32_t wrapOn = Config::TICKS_PER_BAR * 2 + 10;
  const uint32_t wrapOff = wrapOn + 40;
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(wrapOn, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(wrapOff, 1, 72, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubWrap, 0));
  loop.beginCapture(CapturePhase::Overdub, 0);

  TEST_ASSERT_EQUAL(notesBefore, loop.visualCache.notes.size());
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 60, 10));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 62, bar7On));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 64, bar15On));
  TEST_ASSERT_TRUE(loop.visualCacheDirty);
  TEST_ASSERT_EQUAL(16u, loop.visualCache.dirtyBars.size());
  const uint32_t wrapDirty = countVisualDirtyBars(loop.visualCache.dirtyBars);
  TEST_ASSERT_GREATER_THAN(0u, wrapDirty);
  TEST_ASSERT_LESS_THAN(16u, wrapDirty);
  TEST_ASSERT_EQUAL(1, loop.visualCache.dirtyBars[2]);
  TEST_ASSERT_EQUAL(0, loop.visualCache.dirtyBars[0]);
  TEST_ASSERT_EQUAL(0, loop.visualCache.dirtyBars[7]);
  TEST_ASSERT_EQUAL(0, loop.visualCache.dirtyBars[15]);

  loop.appendLastCommittedOverdubPassToSourceView();
  TEST_ASSERT_TRUE(loop.overdubSourceSpanCacheReadyForTest());
  TEST_ASSERT_EQUAL(notesBefore, loop.visualCache.notes.size());
  TEST_ASSERT_FALSE(hasDisplayNote(loop.visualCache.notes, 72, wrapOn));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldAssignGrownVisualCacheOverOverdubHold(
      loop.visualCache.notes.size(), notesBefore));
  loop.appendOverdubPassDisplayNotes(loop.visualCache.notes);
  TEST_ASSERT_FALSE(hasDisplayNote(loop.visualCache.notes, 72, wrapOn));
  TEST_ASSERT_EQUAL(notesBefore, loop.visualCache.notes.size());
  loop.rebuildVisualCacheIdleSlice(4, 2, UINT32_MAX, false);
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 72, wrapOn));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 60, 10));

  loop.markDisplayCachesStale();
  TEST_ASSERT_TRUE(loop.visualCacheDirty);
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 60, 10));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 72, wrapOn));
  TEST_ASSERT_EQUAL(16u, loop.visualCache.dirtyBars.size());
  TEST_ASSERT_EQUAL(16u, countVisualDirtyBars(loop.visualCache.dirtyBars));
}

void test_wrap_held_span_does_not_dirty_unchanged_interior_bars() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = kLoopLen;
  LoopEventStore record;
  const uint32_t midOn = Config::TICKS_PER_BAR * 3 + 10;
  TEST_ASSERT_TRUE(storeAppendNoteOn(record, 10, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(58, 1, 60, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(record, midOn, 1, 62, 100, 2));
  TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(midOn + 40, 1, 62, 0)));
  loop.seedRecordPassFromStore(record);
  loop.rebuildVisualCacheFromPasses();
  loop.visualCache.dirtyBars.clear();
  loop.visualCacheDirty = false;

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(192, 1, 72, 90, 10)));
  MidiEvent wrapOff = MidiEvent::NoteOff(16, 1, 72, 0);
  wrapOff.noteId = 10;
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(wrapOff));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubWrap, 0));

  TEST_ASSERT_TRUE(loop.visualCacheDirty);
  TEST_ASSERT_EQUAL(8u, loop.visualCache.dirtyBars.size());
  TEST_ASSERT_EQUAL(1, loop.visualCache.dirtyBars[0]);
  TEST_ASSERT_EQUAL(0, loop.visualCache.dirtyBars[1]);
  TEST_ASSERT_EQUAL(0, loop.visualCache.dirtyBars[3]);
  TEST_ASSERT_EQUAL(0, loop.visualCache.dirtyBars[7]);
  TEST_ASSERT_EQUAL(1u, countVisualDirtyBars(loop.visualCache.dirtyBars));
}

void test_wrap_held_on_last_bar_dirties_endpoints_only() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = kLoopLen;
  LoopEventStore record;
  const uint32_t midOn = Config::TICKS_PER_BAR * 3 + 10;
  TEST_ASSERT_TRUE(storeAppendNoteOn(record, midOn, 1, 62, 100, 2));
  TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(midOn + 40, 1, 62, 0)));
  loop.seedRecordPassFromStore(record);
  loop.rebuildVisualCacheFromPasses();
  loop.visualCache.dirtyBars.clear();
  loop.visualCacheDirty = false;

  const uint32_t wrapOn = kLoopLen - 40;
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(wrapOn, 1, 72, 90, 10)));
  MidiEvent wrapOff = MidiEvent::NoteOff(80, 1, 72, 0);
  wrapOff.noteId = 10;
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(wrapOff));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubWrap, 0));

  TEST_ASSERT_TRUE(loop.visualCacheDirty);
  TEST_ASSERT_EQUAL(8u, loop.visualCache.dirtyBars.size());
  TEST_ASSERT_EQUAL(1, loop.visualCache.dirtyBars[0]);
  TEST_ASSERT_EQUAL(1, loop.visualCache.dirtyBars[7]);
  TEST_ASSERT_EQUAL(0, loop.visualCache.dirtyBars[1]);
  TEST_ASSERT_EQUAL(0, loop.visualCache.dirtyBars[3]);
  TEST_ASSERT_EQUAL(0, loop.visualCache.dirtyBars[6]);
  TEST_ASSERT_EQUAL(2u, countVisualDirtyBars(loop.visualCache.dirtyBars));
}

void test_wrap_append_restamps_span_cache_after_wrap_pair_restamp_fail() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);
  loop.rebuildVisualCacheFromPasses();
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.overdubSourceSpanCacheReadyForTest());
  TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 10));

  const uint32_t wrapOn = kLoopLen - 40;
  const uint32_t wrapOff = 80;
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(wrapOn, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(wrapOff, 1, 72, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubWrap, 0));
  TEST_ASSERT_FALSE(loop.overdubSourceSpanCacheReadyForTest());

  loop.appendLastCommittedOverdubPassToSourceView();
  TEST_ASSERT_TRUE(loop.overdubSourceSpanCacheReadyForTest());
  TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 10));
  TEST_ASSERT_EQUAL(1u, loop.visualCache.notes.size());
}

/// `141717` sequence: the session opens one bar in, the held pitch lands on every
/// bar line of each pass, and occupancy resolves at each NoteOn. On device the second
/// pass found a participant at 768, 1536 and 2304 but never at phase 0.
void test_second_pass_hold_at_phase_zero_finds_prior_pass_participant() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);
  loop.rebuildVisualCacheFromPasses();

  const uint32_t bar = Config::TICKS_PER_BAR;
  const uint32_t sessionPhase = bar;
  const uint32_t holdPhases[] = {bar, bar * 2, bar * 3, 0};
  NoteId nextNoteId = 20;

  loop.openOverdubSession(sessionPhase);
  loop.beginCapture(CapturePhase::Overdub, sessionPhase);

  auto runPass = [&](bool expectParticipants) {
    for (uint32_t holdPhase : holdPhases) {
      if (expectParticipants) {
        OverlapNoteIdSet present;
        loop.collectOverdubSourceHoldParticipantIds(holdPhase, 64, present);
        char message[80];
        snprintf(message, sizeof(message), "no participant at hold phase %lu",
                 static_cast<unsigned long>(holdPhase));
        TEST_ASSERT_TRUE_MESSAGE(present.size() > 0, message);
      }
      TEST_ASSERT_TRUE(
          loop.appendCaptureEvent(noteOnWithNoteId(holdPhase, 1, 64, 90, nextNoteId)));
      OverlapNoteIdSet ledgerIds;
      OverlapNoteIdSet resolved;
      loop.resolveOccupancyAtNoteOn(1, 64, 90, ledgerIds, holdPhase, &resolved);
      TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(holdPhase + 144, 1, 64, 0)));
      ++nextNoteId;
    }
  };

  runPass(false);
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, sessionPhase));
  const PassId passA = loop.lastCommittedPassId();
  const EditPassIdList companionIds = loop.sealPendingNoteChangesToEditPasses();
  loop.pushOverdubSessionPass(passA, companionIds);
  loop.beginCapture(CapturePhase::Overdub, sessionPhase);
  loop.appendLastCommittedOverdubPassToSourceView();

  runPass(true);
}

void test_wrap_append_does_not_lengthen_same_pitch_body_in_visual_cache() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);
  loop.rebuildVisualCacheFromPasses();
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(192, 1, 39, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(240, 1, 39, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(448, 1, 39, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(496, 1, 39, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(kLoopLen - 40, 1, 39, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(80, 1, 39, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubWrap, 0));
  const size_t notesAtWrap = loop.visualCache.notes.size();
  loop.appendLastCommittedOverdubPassToSourceView();
  TEST_ASSERT_EQUAL(notesAtWrap, loop.visualCache.notes.size());
  loop.rebuildVisualCacheFromPasses();
  SessionMidiEventVec flat;
  loop.copyEffectiveCommittedEvents(flat);
  const NoteUtils::DisplayNoteVec linearPassNotes =
      NoteUtils::reconstructDisplayNotes(flat, loop.loopLengthTicks, false, false, false);

  NoteUtils::DisplayNoteVec longerSiblings;
  for (const NoteUtils::DisplayNote& linearNote : linearPassNotes) {
    if (linearNote.note == 39 && linearNote.startTick == 240u) {
      NoteUtils::DisplayNote longer = linearNote;
      longer.endTick = linearNote.endTick + 48;
      longerSiblings.push_back(longer);
    }
  }
  TEST_ASSERT_EQUAL(1u, longerSiblings.size());
  loop.mergeDisplayNotesIntoVisualCache(longerSiblings);
  loop.appendOverdubPassDisplayNotes(loop.visualCache.notes);

  for (const NoteUtils::DisplayNote& linearNote : linearPassNotes) {
    if (linearNote.note != 39) {
      continue;
    }
    uint32_t count = 0;
    uint32_t endTick = 0;
    for (const NoteUtils::DisplayNote& note : loop.visualCache.notes) {
      if (note.note == 39 && note.startTick == linearNote.startTick) {
        ++count;
        endTick = note.endTick;
      }
    }
    TEST_ASSERT_EQUAL(1u, count);
    TEST_ASSERT_EQUAL(linearNote.endTick, endTick);
  }
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 39, kLoopLen - 40));
  const uint32_t tailStart = NoteUtils::wrapTailStartTick(loop.loopLengthTicks);
  for (const NoteUtils::DisplayNote& note : loop.visualCache.notes) {
    if (note.note != 39) {
      continue;
    }
    bool inLinear = false;
    for (const NoteUtils::DisplayNote& linearNote : linearPassNotes) {
      if (linearNote.note == 39 && linearNote.startTick == note.startTick) {
        inLinear = true;
        break;
      }
    }
    const bool wrapHeld = note.startTick == 0 || note.startTick >= tailStart;
    TEST_ASSERT_TRUE(inLinear || wrapHeld);
  }
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

  SessionMidiEventVec gathered;
  loop.copyEffectiveCommittedEvents(gathered);
  gathered.push_back(MidiEvent::NoteOn(100, 1, 80, 80));
  gathered.push_back(MidiEvent::NoteOff(140, 1, 80, 0));
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

void test_source_view_prepared_window_includes_unpaired_open_tails() {
  // Prepared path copies finished opens (projected endTick == loopLength-1).
  // Unprepared MIDI reconstruct (finishOpenNotes=false) must still omit them.
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
  TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), 72, 80));
  bool finishedOpen = false;
  for (const NoteUtils::DisplayNote& note : loop.overdubSourceViewNotes()) {
    if (note.note == 72 && note.startTick == 80u && note.endTick == kLoopLen - 1) {
      finishedOpen = true;
    }
  }
  TEST_ASSERT_TRUE(finishedOpen);

  ++loop.playbackRevision;
  loop.rebuildOverdubSourceView(0);
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

// 024225: why=open from=span notes=0 while window ev=2. RC12 paints source view, so
// display dropped the record layer until STOPPED used visual cache. Empty span copy
// is a miss — rebuild falls back to MIDI reconstruct of the gathered window.
void test_source_view_falls_back_when_prepared_span_copy_is_empty() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  seedRecordNote(loop, 10, 58, 60);
  LoopContentResolution::DeviceGateSample sample;
  LoopContentResolution::measureDeviceGate(loop.passes, loop.loopLengthTicks, sample);
  LoopContentResolution::deviceGateComplete(loop.playbackRevision);
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));

  NoteUtils::DisplayNoteVec copied;
  TEST_ASSERT_TRUE(
      LoopContentResolution::tryCopyPreparedSpansToDisplayNotes(loop.playbackRevision, copied));
  TEST_ASSERT_TRUE(hasDisplayNote(copied, 60, 10));

  const PassId recordId = loop.lastCommittedPassId();
  TEST_ASSERT_TRUE(loop.setCapturePassState(recordId, CapturePassState::Disabled));
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));
  copied.clear();
  TEST_ASSERT_FALSE(
      LoopContentResolution::tryCopyPreparedSpansToDisplayNotes(loop.playbackRevision, copied));
  TEST_ASSERT_EQUAL(0u, copied.size());

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  TEST_ASSERT_FALSE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 10));
  LoopContentResolution::deviceGateReset();
}

// 030219: wrap from=span notes=34 while window ev=8 reconstructs 4. Leftover
// checkpoint spans whose noteIds are not in the gathered window must not copy.
void test_source_view_span_copy_keeps_only_window_note_ids() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  seedTwoRecordNotes(loop, 10, 58, 1, 80, 120, 2, 60);
  LoopContentResolution::DeviceGateSample sample;
  LoopContentResolution::measureDeviceGate(loop.passes, loop.loopLengthTicks, sample);
  LoopContentResolution::deviceGateComplete(loop.playbackRevision);
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));

  NoteUtils::DisplayNoteVec all;
  TEST_ASSERT_TRUE(
      LoopContentResolution::tryCopyPreparedSpansToDisplayNotes(loop.playbackRevision, all));
  TEST_ASSERT_TRUE(hasDisplayNote(all, 60, 10));
  TEST_ASSERT_TRUE(hasDisplayNote(all, 60, 80));

  SessionMidiEventVec window;
  MidiEvent keep = MidiEvent::NoteOn(10, 1, 60, 100);
  keep.noteId = 1;
  window.push_back(keep);
  NoteUtils::DisplayNoteVec filtered;
  TEST_ASSERT_TRUE(LoopContentResolution::tryCopyPreparedSpansToDisplayNotes(
      loop.playbackRevision, filtered, &window, loop.loopLengthTicks));
  TEST_ASSERT_TRUE(hasDisplayNote(filtered, 60, 10));
  TEST_ASSERT_FALSE(hasDisplayNote(filtered, 60, 80));
  LoopContentResolution::deviceGateReset();
}

// 122848 / 123803: >128 window NoteOns must not abort span copy via OverlapNoteIdSet.
void test_source_view_span_copy_keeps_window_note_on_ids_past_occupy_set_capacity() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  constexpr uint32_t kBar = Config::TICKS_PER_BAR;
  constexpr uint32_t kLongLoop = kBar * 64;
  constexpr uint32_t kNoteCount = kOverlapNoteIdSetCapacity + 1;
  seedDenseWindowNotes(loop, kNoteCount, kLongLoop);
  LoopContentResolution::DeviceGateSample sample;
  LoopContentResolution::measureDeviceGate(loop.passes, loop.loopLengthTicks, sample);
  LoopContentResolution::deviceGateComplete(loop.playbackRevision);
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));

  NoteUtils::DisplayNoteVec prepared;
  TEST_ASSERT_TRUE(
      LoopContentResolution::tryCopyPreparedSpansToDisplayNotes(loop.playbackRevision, prepared));
  TEST_ASSERT_EQUAL(kNoteCount, prepared.size());

  SessionMidiEventVec window;
  for (uint32_t i = 0; i < kNoteCount; ++i) {
    MidiEvent on = MidiEvent::NoteOn(i * 80u, 1, 60, 100);
    on.noteId = static_cast<NoteId>(i + 1);
    window.push_back(on);
  }
  TEST_ASSERT_TRUE(window.size() > kOverlapNoteIdSetCapacity);

  NoteUtils::DisplayNoteVec copied;
  TEST_ASSERT_TRUE(LoopContentResolution::tryCopyPreparedSpansToDisplayNotes(
      loop.playbackRevision, copied, &window, loop.loopLengthTicks));
  TEST_ASSERT_EQUAL(kNoteCount, copied.size());
  for (const NoteUtils::DisplayNote& row : copied) {
    TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, row.noteId);
    bool inWindow = false;
    for (const MidiEvent& evt : window) {
      if (evt.isNoteOn() && evt.noteId == row.noteId) {
        inWindow = true;
        break;
      }
    }
    TEST_ASSERT_TRUE(inWindow);
    const NoteUtils::DisplayNote* preparedRow = findDisplayNoteById(prepared, row.noteId);
    TEST_ASSERT_NOT_NULL(preparedRow);
    TEST_ASSERT_EQUAL(preparedRow->note, row.note);
    TEST_ASSERT_EQUAL_UINT32(preparedRow->startTick, row.startTick);
    TEST_ASSERT_EQUAL_UINT32(preparedRow->endTick, row.endTick);
    TEST_ASSERT_EQUAL(0, row.velocity);
  }
  LoopContentResolution::deviceGateReset();
}

void test_source_view_rebuild_uses_prepared_spans_past_occupy_set_capacity() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  constexpr uint32_t kBar = Config::TICKS_PER_BAR;
  constexpr uint32_t kLongLoop = kBar * 64;
  constexpr uint32_t kNoteCount = kOverlapNoteIdSetCapacity + 1;
  seedDenseWindowNotes(loop, kNoteCount, kLongLoop);
  LoopContentResolution::DeviceGateSample sample;
  LoopContentResolution::measureDeviceGate(loop.passes, loop.loopLengthTicks, sample);
  LoopContentResolution::deviceGateComplete(loop.playbackRevision);
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));

  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  TEST_ASSERT_EQUAL(kNoteCount, loop.overdubSourceViewNotes().size());
  for (const NoteUtils::DisplayNote& row : loop.overdubSourceViewNotes()) {
    TEST_ASSERT_EQUAL(0, row.velocity);
  }

  NoteUtils::DisplayNoteVec copied;
  TEST_ASSERT_TRUE(LoopContentResolution::tryCopyPreparedSpansToDisplayNotes(
      loop.playbackRevision, copied, &loop.overdubSourceViewEvents(), loop.loopLengthTicks));
  TEST_ASSERT_EQUAL(copied.size(), loop.overdubSourceViewNotes().size());
  for (const NoteUtils::DisplayNote& row : loop.overdubSourceViewNotes()) {
    const NoteUtils::DisplayNote* copiedRow = findDisplayNoteById(copied, row.noteId);
    TEST_ASSERT_NOT_NULL(copiedRow);
    TEST_ASSERT_EQUAL(copiedRow->note, row.note);
    TEST_ASSERT_EQUAL_UINT32(copiedRow->startTick, row.startTick);
    TEST_ASSERT_EQUAL_UINT32(copiedRow->endTick, row.endTick);
  }

  OverlapNoteIdSet sourceIds;
  loop.collectOverdubSourceHoldParticipantIds(20, 60, sourceIds);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(sourceIds.size()));
  TEST_ASSERT_TRUE(sourceIds.contains(1));
  LoopContentResolution::deviceGateReset();
}

void test_prepared_session_length_mismatch_misses_resolve_copy_collect() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  constexpr uint32_t kBar = Config::TICKS_PER_BAR;
  constexpr uint32_t kShortLoop = kBar;
  constexpr uint32_t kLongLoop = kBar * 66;

  Loop shortLoop;
  seedDenseWindowNotes(shortLoop, 4, kShortLoop);
  LoopContentResolution::DeviceGateSample sample;
  LoopContentResolution::measureDeviceGate(shortLoop.passes, shortLoop.loopLengthTicks, sample);
  LoopContentResolution::deviceGateComplete(shortLoop.playbackRevision);
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(shortLoop.playbackRevision));
  TEST_ASSERT_EQUAL_UINT32(kShortLoop, LoopContentResolution::deviceGateLoopLengthTicks());

  Loop longLoop;
  seedDenseWindowNotes(longLoop, 8, kLongLoop);
  LoopContentResolution::restampPreparedPlaybackRevision(longLoop.playbackRevision);
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(longLoop.playbackRevision));

  SessionMidiEventVec window;
  TEST_ASSERT_FALSE(LoopContentResolution::tryResolvePreparedWindow(
      longLoop.passes.editPasses, longLoop.loopLengthTicks, 0, longLoop.loopLengthTicks,
      longLoop.playbackRevision, window, nullptr));
  NoteUtils::DisplayNoteVec copied;
  TEST_ASSERT_FALSE(LoopContentResolution::tryCopyPreparedSpansToDisplayNotes(
      longLoop.playbackRevision, copied, nullptr, longLoop.loopLengthTicks));
  OverlapNoteIdSet preparedIds;
  TEST_ASSERT_FALSE(LoopContentResolution::tryCollectPreparedPresentNoteIdsAtTick(
      20, 60, longLoop.playbackRevision, longLoop.loopLengthTicks, preparedIds));

  longLoop.rebuildOverdubSourceView(0);
  TEST_ASSERT_FALSE(longLoop.overdubSourceViewNotes().empty());
  TEST_ASSERT_EQUAL(100, longLoop.overdubSourceViewNotes().front().velocity);
  LoopContentResolution::deviceGateReset();
}

void test_publish_prepared_overdub_pass_ignores_loop_length_mismatch() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  constexpr uint32_t kBar = Config::TICKS_PER_BAR;
  constexpr uint32_t kShortLoop = kBar;
  constexpr uint32_t kLongLoop = kBar * 66;

  Loop shortLoop;
  seedDenseWindowNotes(shortLoop, 4, kShortLoop);
  LoopContentResolution::DeviceGateSample sample;
  LoopContentResolution::measureDeviceGate(shortLoop.passes, shortLoop.loopLengthTicks, sample);
  LoopContentResolution::deviceGateComplete(shortLoop.playbackRevision);
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(shortLoop.playbackRevision));

  Loop longLoop;
  seedDenseWindowNotes(longLoop, 8, kLongLoop);
  LoopContentResolution::restampPreparedPlaybackRevision(longLoop.playbackRevision);
  const uint32_t restampedRevision = longLoop.playbackRevision;

  longLoop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(longLoop.appendCaptureEvent(noteOnWithNoteId(200, 1, 72, 90, 9001)));
  TEST_ASSERT_TRUE(longLoop.appendCaptureEvent(MidiEvent::NoteOff(400, 1, 72, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    longLoop.commitCapturePass(CommitReason::OverdubWrap, 0));
  const OverdubPass* wrap = findOverdubPass(longLoop, longLoop.lastCommittedPassId());
  TEST_ASSERT_NOT_NULL(wrap);
  TEST_ASSERT_NOT_EQUAL(restampedRevision, longLoop.playbackRevision);
  LoopContentResolution::publishPreparedOverdubPass(*wrap, longLoop.playbackRevision,
                                                    longLoop.loopLengthTicks);
  TEST_ASSERT_FALSE(LoopContentResolution::preparedWindowReady(longLoop.playbackRevision));
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(restampedRevision));
  OverlapNoteIdSet afterPublish;
  TEST_ASSERT_FALSE(LoopContentResolution::tryCollectPreparedPresentNoteIdsAtTick(
      20, 60, longLoop.playbackRevision, longLoop.loopLengthTicks, afterPublish));
  LoopContentResolution::deviceGateReset();
}

void test_prepared_session_remeasure_after_reset_copies_long_loop() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  constexpr uint32_t kBar = Config::TICKS_PER_BAR;
  constexpr uint32_t kShortLoop = kBar;
  constexpr uint32_t kLongLoop = kBar * 66;
  constexpr uint32_t kNoteCount = kOverlapNoteIdSetCapacity + 1;

  Loop shortLoop;
  seedDenseWindowNotes(shortLoop, 4, kShortLoop);
  LoopContentResolution::DeviceGateSample sample;
  LoopContentResolution::measureDeviceGate(shortLoop.passes, shortLoop.loopLengthTicks, sample);
  LoopContentResolution::deviceGateComplete(shortLoop.playbackRevision);
  TEST_ASSERT_EQUAL_UINT32(kShortLoop, LoopContentResolution::deviceGateLoopLengthTicks());

  Loop longLoop;
  seedDenseWindowNotes(longLoop, kNoteCount, kLongLoop);
  LoopContentResolution::deviceGateReset();
  LoopContentResolution::measureDeviceGate(longLoop.passes, longLoop.loopLengthTicks, sample);
  LoopContentResolution::deviceGateComplete(longLoop.playbackRevision);
  TEST_ASSERT_EQUAL_UINT32(kLongLoop, LoopContentResolution::deviceGateLoopLengthTicks());
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(longLoop.playbackRevision));

  longLoop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(longLoop.hasOverdubSourceView());
  TEST_ASSERT_EQUAL(kNoteCount, longLoop.overdubSourceViewNotes().size());
  for (const NoteUtils::DisplayNote& row : longLoop.overdubSourceViewNotes()) {
    TEST_ASSERT_EQUAL(0, row.velocity);
  }
  NoteUtils::DisplayNoteVec copied;
  TEST_ASSERT_TRUE(LoopContentResolution::tryCopyPreparedSpansToDisplayNotes(
      longLoop.playbackRevision, copied, &longLoop.overdubSourceViewEvents(),
      longLoop.loopLengthTicks));
  TEST_ASSERT_EQUAL(copied.size(), longLoop.overdubSourceViewNotes().size());
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
  loop.nextNoteId_ = 3;
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
  LoopContentResolution::publishPreparedOverdubPass(*wrap, loop.playbackRevision,
                                                    loop.loopLengthTicks);
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
                                                    loop.loopLengthTicks, loop.passes.editPasses,
                                                    EditPassIdList{hideId});
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
      300, 60, loop.playbackRevision, loop.loopLengthTicks, publishedIds));
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
      300, 60, loop.playbackRevision, loop.loopLengthTicks, undoneIds));
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
  LoopContentResolution::publishPreparedOverdubPass(*wrap, loop.playbackRevision,
                                                    loop.loopLengthTicks);
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

// Lane D1 (161349): span copy after wrap must keep record-pass noteId, not only the new overdub id.
void test_wrap_rebuild_retains_record_pass_note_id_after_publish() {
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
  const OverdubPass* wrap = findOverdubPass(loop, loop.lastCommittedPassId());
  TEST_ASSERT_NOT_NULL(wrap);
  LoopContentResolution::publishPreparedOverdubPass(*wrap, loop.playbackRevision,
                                                    loop.loopLengthTicks);
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));

  loop.rebuildOverdubSourceView(0);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 0));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 64));
  TEST_ASSERT_TRUE(sourceViewHasNoteId(loop, 1));
  TEST_ASSERT_TRUE(sourceViewHasNoteId(loop, 10));
  LoopContentResolution::deviceGateReset();
}

// Lane D1/D2-A: sealed Hide removes record from RC12 source view (expected resolver output).
void test_wrap_rebuild_with_hide_companion_drops_record_from_source_view() {
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
  const OverdubPass* wrap = findOverdubPass(loop, loop.lastCommittedPassId());
  TEST_ASSERT_NOT_NULL(wrap);
  EditPass hide{};
  hide.passType = EditPassType::Note;
  hide.actionType = EditActionType::Delete;
  hide.targetNoteId = 1;
  const EditPassId hideId =
      loop.saveNoteEditPass(kOverdubCompanionEditPassIndex, std::move(hide), EditPassType::Note);
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, hideId);
  LoopContentResolution::publishPreparedOverdubPass(*wrap, loop.playbackRevision,
                                                    loop.loopLengthTicks, loop.passes.editPasses,
                                                    EditPassIdList{hideId});
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));

  loop.rebuildOverdubSourceView(0);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  TEST_ASSERT_FALSE(sourceViewHasNoteId(loop, 1));
  TEST_ASSERT_TRUE(sourceViewHasNoteId(loop, 10));
  LoopContentResolution::deviceGateReset();
}

// Lane D1/D2: multi-wrap membership — untouched survives, hidden absent, crossed earlier note shortened.
void test_multi_wrap_source_view_membership_three_classes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  loop.loopLengthTicks = kLoopLen;
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 64, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(400, 1, 60, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 100, 1, 62, 100, 2));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(300, 1, 62, 0)));
  loop.seedRecordPassFromStore(store);
  LoopContentResolution::DeviceGateSample sample;
  LoopContentResolution::measureDeviceGate(loop.passes, loop.loopLengthTicks, sample);
  LoopContentResolution::deviceGateComplete(loop.playbackRevision);
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  loop.establishOverdubSourceView(0);

  constexpr uint8_t kPitchHide = 60;
  constexpr uint8_t kPitchUntouched = 62;
  OverlapNoteIdSet occupy;
  loop.collectOverdubSourceHoldParticipantIds(100, kPitchHide, occupy);
  TEST_ASSERT_TRUE(occupy.contains(1));
  const EditPassIdList wrap1Companions =
      commitSamePitchWrapAndPublish(loop, 10, 64, 240, occupy);

  TEST_ASSERT_TRUE(sourceViewHasNoteId(loop, 2));
  TEST_ASSERT_TRUE(sourceViewHasNoteId(loop, 10));
  TEST_ASSERT_TRUE(sourceViewHasNoteId(loop, 1));
  TEST_ASSERT_FALSE(editPassesHideTarget(loop, 1));
  const NoteUtils::DisplayNote* wrap1Remnant =
      findDisplayNoteById(loop.overdubSourceViewNotes(), 1);
  TEST_ASSERT_NOT_NULL(wrap1Remnant);
  TEST_ASSERT_EQUAL_UINT32(64u, wrap1Remnant->startTick);
  TEST_ASSERT_EQUAL_UINT32(239u, wrap1Remnant->endTick);

  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(150, 1, 61, 90, 11)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(200, 1, 61, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubWrap, 0));
  const OverdubPass* wrap2 = findOverdubPass(loop, loop.lastCommittedPassId());
  TEST_ASSERT_NOT_NULL(wrap2);
  LoopContentResolution::publishPreparedOverdubPass(*wrap2, loop.playbackRevision,
                                                    loop.loopLengthTicks, loop.passes.editPasses,
                                                    wrap1Companions);
  loop.rebuildOverdubSourceView(0);
  loop.beginCapture(CapturePhase::Overdub, 0);

  TEST_ASSERT_TRUE(sourceViewHasNoteId(loop, 2));
  TEST_ASSERT_TRUE(sourceViewHasNoteId(loop, 10));
  TEST_ASSERT_TRUE(sourceViewHasNoteId(loop, 11));
  TEST_ASSERT_TRUE(sourceViewHasNoteId(loop, 1));

  occupy.clear();
  loop.collectOverdubSourceHoldParticipantIds(230, kPitchHide, occupy);
  TEST_ASSERT_TRUE(occupy.contains(10));
  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, kPitchHide, 90, 224, 280, 12,
                                                                   overlapIds({10})));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(224, 1, kPitchHide, 90, 12)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(280, 1, kPitchHide, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubWrap, 0));
  const OverdubPass* wrap3 = findOverdubPass(loop, loop.lastCommittedPassId());
  TEST_ASSERT_NOT_NULL(wrap3);
  const EditPassIdList wrap3Companions = loop.sealPendingNoteChangesToEditPasses();
  EditPassIdList allCompanions = wrap1Companions;
  for (EditPassId id : wrap3Companions) {
    allCompanions.push_back(id);
  }
  LoopContentResolution::publishPreparedOverdubPass(*wrap3, loop.playbackRevision,
                                                    loop.loopLengthTicks, loop.passes.editPasses,
                                                    allCompanions);
  loop.rebuildOverdubSourceView(0);
  loop.beginCapture(CapturePhase::Overdub, 0);

  TEST_ASSERT_TRUE(sourceViewHasNoteId(loop, 2));
  TEST_ASSERT_TRUE(sourceViewHasNoteId(loop, 11));
  TEST_ASSERT_TRUE(sourceViewHasNoteId(loop, 12));
  TEST_ASSERT_TRUE(sourceViewHasNoteId(loop, 1));
  const NoteUtils::DisplayNote* wrap3Remnant =
      findDisplayNoteById(loop.overdubSourceViewNotes(), 1);
  TEST_ASSERT_NOT_NULL(wrap3Remnant);
  TEST_ASSERT_EQUAL_UINT32(64u, wrap3Remnant->startTick);
  TEST_ASSERT_EQUAL_UINT32(223u, wrap3Remnant->endTick);
  const NoteUtils::DisplayNote* shortened =
      findDisplayNoteById(loop.overdubSourceViewNotes(), 10);
  TEST_ASSERT_NOT_NULL(shortened);
  TEST_ASSERT_EQUAL_UINT32(64u, shortened->startTick);
  TEST_ASSERT_EQUAL_UINT32(223u, shortened->endTick);
  TEST_ASSERT_EQUAL_UINT8(kPitchHide, shortened->note);
  const NoteUtils::DisplayNote* untouched =
      findDisplayNoteById(loop.overdubSourceViewNotes(), 2);
  TEST_ASSERT_NOT_NULL(untouched);
  TEST_ASSERT_EQUAL_UINT8(kPitchUntouched, untouched->note);
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
                                                    loop.loopLengthTicks, loop.passes.editPasses,
                                                    EditPassIdList{hideId});
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
  loop.copyEffectiveCommittedEvents(afterUndo);
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
  TEST_ASSERT_FALSE(loop.consumeSuppressedOverdubWrapCrossing(776, 777));
  TEST_ASSERT_TRUE(loop.consumeSuppressedOverdubWrapCrossing(kLoopLen - 1, 0));
  loop.armOverdubWrapAfterLeavingStart(1);
  TEST_ASSERT_TRUE(loop.shouldCommitOverdubWrap(kLoopLen - 1, 0));
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

void test_overdub_session_index_stamps_wraps_and_groups_after_reload() {
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
  const uint8_t session1 = loop.passes.overdubPasses.back().overdubSessionIndex;
  TEST_ASSERT_TRUE(session1 != kUngroupedOverdubSessionIndex);
  loop.beginCapture(CapturePhase::Overdub, 777);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(500, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(600, 1, 64, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubStop, 777));
  TEST_ASSERT_EQUAL(2u, loop.passes.overdubPasses.size());
  TEST_ASSERT_EQUAL_UINT8(session1, loop.passes.overdubPasses[0].overdubSessionIndex);
  TEST_ASSERT_EQUAL_UINT8(session1, loop.passes.overdubPasses[1].overdubSessionIndex);
  loop.closeOverdubSession();

  loop.openOverdubSession(777);
  loop.beginCapture(CapturePhase::Overdub, 777);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(100, 1, 67, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(200, 1, 67, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubStop, 777));
  TEST_ASSERT_EQUAL(3u, loop.passes.overdubPasses.size());
  const uint8_t session2 = loop.passes.overdubPasses.back().overdubSessionIndex;
  TEST_ASSERT_TRUE(session2 != kUngroupedOverdubSessionIndex);
  TEST_ASSERT_TRUE(session2 != session1);
  loop.closeOverdubSession();
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
  loop.nextNoteId_ = 3;
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
  loop.nextNoteId_ = 2;
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

void drainPendingSnapshotPublish(Loop& loop) {
  size_t steps = 0;
  while (loop.hasPendingSnapshotPublish() && steps < 10000) {
    loop.stepPendingSnapshotPublish(1);
    ++steps;
  }
  TEST_ASSERT_FALSE(loop.hasPendingSnapshotPublish());
}

void recordAndPublishNote(Loop& loop, uint32_t onTick, uint32_t offTick, uint8_t pitch) {
  loop.loopLengthTicks = kLoopLen;
  loop.loopId = 1;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(onTick, 1, pitch, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(offTick, 1, pitch, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::RecordStop, offTick));
  drainPendingSnapshotPublish(loop);
  TEST_ASSERT_NOT_NULL(loop.publishedSnapshot().get());
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
}

uint32_t countNotesAtPitchStart(const NoteUtils::DisplayNoteVec& notes, uint8_t pitch,
                                uint32_t startTick) {
  uint32_t count = 0;
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.note == pitch && note.startTick == startTick) {
      ++count;
    }
  }
  return count;
}

uint32_t noteEndAtPitchStart(const NoteUtils::DisplayNoteVec& notes, uint8_t pitch,
                             uint32_t startTick) {
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.note == pitch && note.startTick == startTick) {
      return note.endTick;
    }
  }
  return UINT32_MAX;
}

uint32_t displayNoteExclusiveLength(const NoteUtils::DisplayNoteVec& notes, uint8_t pitch,
                                    uint32_t startTick, uint32_t loopLength) {
  const uint32_t endTick = noteEndAtPitchStart(notes, pitch, startTick);
  if (endTick == UINT32_MAX || loopLength == 0 || endTick < startTick) {
    return UINT32_MAX;
  }
  const uint32_t endExclusive = (endTick == loopLength - 1) ? loopLength : endTick;
  return endExclusive > startTick ? (endExclusive - startTick) : 0;
}

void appendOnOff(Loop& loop, uint32_t onTick, uint32_t offTick, uint8_t pitch, NoteId noteId) {
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(onTick, 1, pitch, 100, noteId)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(offTick, 1, pitch, 0)));
}

EditPassId saveHideCompanion(Loop& loop, NoteId targetNoteId, uint32_t startTick, uint32_t endTick) {
  EditPass hide{};
  hide.passType = EditPassType::Note;
  hide.actionType = EditActionType::Delete;
  hide.propertyType = EditPropertyType::None;
  hide.targetNoteId = targetNoteId;
  hide.pitch = 48;
  hide.startTick = startTick;
  hide.endTick = endTick;
  const EditPassId id =
      loop.saveNoteEditPass(kOverdubCompanionEditPassIndex, std::move(hide), EditPassType::Note);
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, id);
  return id;
}

uint32_t countPitchOnsAtTick(const SessionMidiEventVec& events, uint8_t pitch, uint32_t tick) {
  uint32_t count = 0;
  for (const MidiEvent& evt : events) {
    if (evt.isNoteOn() && evt.data.noteData.note == pitch && evt.tick == tick) {
      ++count;
    }
  }
  return count;
}

uint32_t countPitchOffsAtTick(const SessionMidiEventVec& events, uint8_t pitch, uint32_t tick) {
  uint32_t count = 0;
  for (const MidiEvent& evt : events) {
    if (evt.isNoteOff() && evt.data.noteData.note == pitch && evt.tick == tick) {
      ++count;
    }
  }
  return count;
}

void seedCommittedOverdubPass(Loop& loop, LoopEventStore& store, PassId passId,
                              uint32_t mergeSequence) {
  OverdubPass pass{};
  pass.id = passId;
  pass.mergeSequence = mergeSequence;
  pass.state = CapturePassState::Active;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, pass.committedChunkIds));
  loop.passes.overdubPasses.push_back(std::move(pass));
}

bool pendingWritesExclusiveEnd(const NoteUtils::DisplayNoteVec& notes, uint8_t pitch,
                               uint32_t startTick, uint32_t exclusiveEnd) {
  return noteEndAtPitchStart(notes, pitch, startTick) == exclusiveEnd;
}

bool noteIntersectsBar(const NoteUtils::DisplayNote& note, uint32_t bar, uint32_t loopLength) {
  const uint32_t barStart = bar * Config::TICKS_PER_BAR;
  return DisplayWindowUtils::noteIntersectsWindow(note.startTick, note.endTick, barStart,
                                                  Config::TICKS_PER_BAR, loopLength);
}

void appendSnapshotNotesForBar(const LoopSnapshot& snapshot, uint32_t bar, uint8_t pitchLow,
                               uint8_t pitchHigh, NoteUtils::DisplayNoteVec& out) {
  const uint8_t lastPitch =
      pitchHigh < kLoopSnapshotPitchCount ? pitchHigh
                                          : static_cast<uint8_t>(kLoopSnapshotPitchCount - 1);
  for (uint16_t pitch = pitchLow; pitch <= lastPitch; ++pitch) {
    const auto membership = snapshot.noteMembership(static_cast<uint8_t>(pitch), bar);
    forEachSnapshotMembershipReference(membership.get(), [&out](const LoopSnapshotNoteRef& reference) {
      if (!reference) {
        return;
      }
      appendDisplayNoteUniqueById(out, displayNoteFromSnapshotNote(*reference));
    });
  }
}

void appendNotesIntersectingBar(const NoteUtils::DisplayNoteVec& source, uint32_t bar,
                                uint32_t loopLength, NoteUtils::DisplayNoteVec& out) {
  for (const NoteUtils::DisplayNote& note : source) {
    if (noteIntersectsBar(note, bar, loopLength)) {
      appendDisplayNoteUniqueById(out, note);
    }
  }
}

/// Same committed prefix as `gatherCommittedDrawSourceBars`. No sealed/preview union,
/// no pending overlay. Host clone — not a firmware hook.
void gatherDrawSourcePrefix(const Loop& loop, uint32_t firstBar, uint32_t lastBar, uint8_t pitchLow,
                            uint8_t pitchHigh, NoteUtils::DisplayNoteVec& out) {
  out.clear();
  const uint32_t loopLength = loop.loopLengthTicks;
  if (loopLength == 0 || firstBar > lastBar) {
    return;
  }
  const ImmutableLoopSnapshotRef snapshot = loop.publishedSnapshot();
  const uint32_t windowStart = firstBar * Config::TICKS_PER_BAR;
  const uint32_t windowEnd = (lastBar + 1u) * Config::TICKS_PER_BAR;
  const uint32_t windowLength = windowEnd > windowStart ? windowEnd - windowStart : 0;
  const bool coversWindow =
      visualCacheCoversWindow(loop.visualCacheDirty, loop.visualCache.dirtyBars, windowStart,
                              windowLength, loopLength, Config::TICKS_PER_BAR);
  const bool uncoveredSealed =
      (loop.captureActive() || loop.hasOverdubSession()) && loop.hasSealedCapturePreview() &&
      !loop.sealedCapturePreviewIsCovered();
  const bool cacheReusable = DisplayWindowUtils::visualCacheReusableAsCommittedDrawSource(
      loop.visualCacheDirty, !loop.visualCache.notes.empty(), coversWindow, uncoveredSealed);
  const bool usePublishedSnapshot =
      DisplayWindowUtils::shouldGatherPublishedSnapshotForCompleteDrawSource(
          static_cast<bool>(snapshot), loop.captureActive(), loop.hasOverdubSession(),
          loop.publishedSnapshotIsCurrent(), cacheReusable, true);
  const uint8_t resolvedPitchHigh = pitchHigh < pitchLow ? pitchLow : pitchHigh;
  for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
    if (usePublishedSnapshot && snapshot) {
      appendSnapshotNotesForBar(*snapshot, bar, pitchLow, resolvedPitchHigh, out);
    } else {
      appendNotesIntersectingBar(loop.visualCache.notes, bar, loopLength, out);
    }
  }
}

void applyDrawSourceSealedAndPreview(const Loop& loop, uint32_t firstBar, uint32_t lastBar,
                                     NoteUtils::DisplayNoteVec& out, size_t committedEnd) {
  const uint32_t loopLength = loop.loopLengthTicks;
  const bool captureActive = loop.captureActive();
  if (DisplayWindowUtils::shouldUnionSealedCapturePreviewForDrawSource(
          loop.hasSealedCapturePreview(), loop.sealedCapturePreviewIsCovered(), captureActive,
          loop.hasOverdubSession())) {
    for (const NoteUtils::DisplayNote& sealedRow : loop.sealedCapturePreviewNotes()) {
      for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
        if (!noteIntersectsBar(sealedRow, bar, loopLength)) {
          continue;
        }
        DisplayWindowUtils::upsertDisplayNoteAtPitchStartFrom(out, 0, sealedRow, captureActive);
        break;
      }
    }
  }
  if (!captureActive) {
    return;
  }
  for (const NoteUtils::DisplayNote& previewNote : loop.capturePreview.notes) {
    bool intersectsWindow = false;
    for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
      if (noteIntersectsBar(previewNote, bar, loopLength)) {
        intersectsWindow = true;
        break;
      }
    }
    if (!intersectsWindow) {
      continue;
    }
    DisplayWindowUtils::upsertDisplayNoteAtPitchStartFrom(out, committedEnd, previewNote, true);
  }
}

bool lengthCompanionTargetsEnd(const Loop& loop, NoteId noteId, uint32_t endTick) {
  for (const EditPass& row : loop.passes.editPasses) {
    if (row.state == EditPassState::Active && row.passType == EditPassType::Note &&
        row.actionType == EditActionType::Update &&
        row.propertyType == EditPropertyType::Length && row.targetNoteId == noteId &&
        row.endTick == endTick) {
      return true;
    }
  }
  return false;
}

bool pendingShortenTargetsEnd(const Loop& loop, NoteId noteId, uint32_t endTick) {
  for (const PendingNoteChange& change : loop.pendingNoteChanges()) {
    if (change.kind == PendingNoteChangeKind::Shorten && change.noteId == noteId &&
        change.endTick == endTick) {
      return true;
    }
  }
  return false;
}

void test_idle_slice_current_snapshot_is_accepted() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  recordAndPublishNote(loop, 192, 256, 39);
  loop.markDisplayCachesStale();
  loop.rebuildVisualCacheIdleSlice(4, 0, UINT32_MAX, false);
  TEST_ASSERT_FALSE(loop.lastVisualCacheIdleOverlayRejected());
  TEST_ASSERT_EQUAL(static_cast<int>(Loop::VisualCacheIdleGatherSource::Snapshot),
                    static_cast<int>(loop.lastVisualCacheIdleGatherSource()));
  TEST_ASSERT_EQUAL(1u, countNotesAtPitchStart(loop.visualCache.notes, 39, 192));
  TEST_ASSERT_EQUAL(256u, noteEndAtPitchStart(loop.visualCache.notes, 39, 192));
  LoopContentResolution::deviceGateReset();
}

void test_idle_slice_stale_overlay_rejects_and_uses_committed_fallback() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  loop.loopLengthTicks = kLoopLen;
  loop.loopId = 1;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(192, 1, 39, 100, 1)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(256, 1, 39, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::RecordStop, 256));
  drainPendingSnapshotPublish(loop);
  TEST_ASSERT_NOT_NULL(loop.publishedSnapshot().get());
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  loop.rebuildVisualCacheFromPasses();
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(192, 1, 39, 90, 10)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(448, 1, 39, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubWrap, 0));
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  EditPass hide{};
  hide.passType = EditPassType::Note;
  hide.actionType = EditActionType::Delete;
  hide.targetNoteId = 1;
  const EditPassId hideId =
      loop.saveNoteEditPass(kOverdubCompanionEditPassIndex, std::move(hide), EditPassType::Note);
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, hideId);
  TEST_ASSERT_TRUE(editPassesHideTarget(loop, 1));
  TEST_ASSERT_TRUE(loop.visualCacheDirty);

  Loop::SnapshotWindowGatherPath overlayPath = Loop::SnapshotWindowGatherPath::None;
  SessionMidiEventVec overlayFlat;
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(overlayFlat, 0, kLoopLen,
                                                                &overlayPath));
  TEST_ASSERT_EQUAL(static_cast<int>(Loop::SnapshotWindowGatherPath::Overlay),
                    static_cast<int>(overlayPath));
  const NoteUtils::DisplayNoteVec overlayNotes =
      NoteUtils::reconstructDisplayNotes(overlayFlat, loop.loopLengthTicks, false, false);

  SessionMidiEventVec committed;
  loop.copyEffectiveCommittedEventsInRange(committed, 0, kLoopLen, true);
  const NoteUtils::DisplayNoteVec oracle =
      NoteUtils::reconstructDisplayNotes(committed, loop.loopLengthTicks, false, false);
  TEST_ASSERT_EQUAL(1u, countNotesAtPitchStart(oracle, 39, 192));
  // RC1c: Hide of record On@192 applies on the record layer only, so the
  // overdub Off@448 stays. Window gather matches per-layer materialize.
  TEST_ASSERT_EQUAL(448u, noteEndAtPitchStart(oracle, 39, 192));

  loop.rebuildVisualCacheIdleSlice(4, 0, UINT32_MAX, false, false, 192);
  TEST_ASSERT_TRUE(loop.lastVisualCacheIdleOverlayRejected());
  const Loop::VisualCacheIdleGatherSource source = loop.lastVisualCacheIdleGatherSource();
  TEST_ASSERT_TRUE(source == Loop::VisualCacheIdleGatherSource::PassesResolve ||
                   source == Loop::VisualCacheIdleGatherSource::Prepared);
  TEST_ASSERT_EQUAL(1u, countNotesAtPitchStart(loop.visualCache.notes, 39, 192));
  // Budgeted idle fallback still applies Delete on the merged session
  // (`advanceFallbackGatherSession`). That remaining owner can keep 256.
  // RC1c does not extend that path.
  LoopContentResolution::deviceGateReset();
}

void test_paint_last_resort_stale_overlay_uses_passes_resolve() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  loop.loopLengthTicks = kLoopLen;
  loop.loopId = 1;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(192, 1, 39, 100, 1)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(256, 1, 39, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::RecordStop, 256));
  drainPendingSnapshotPublish(loop);
  TEST_ASSERT_NOT_NULL(loop.publishedSnapshot().get());
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  loop.rebuildVisualCacheFromPasses();
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(192, 1, 39, 90, 10)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(448, 1, 39, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubWrap, 0));
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  EditPass hide{};
  hide.passType = EditPassType::Note;
  hide.actionType = EditActionType::Delete;
  hide.targetNoteId = 1;
  const EditPassId hideId =
      loop.saveNoteEditPass(kOverdubCompanionEditPassIndex, std::move(hide), EditPassType::Note);
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, hideId);
  TEST_ASSERT_TRUE(editPassesHideTarget(loop, 1));

  Loop::SnapshotWindowGatherPath overlayPath = Loop::SnapshotWindowGatherPath::None;
  SessionMidiEventVec overlayFlat;
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(overlayFlat, 0, kLoopLen,
                                                                &overlayPath));
  TEST_ASSERT_EQUAL(static_cast<int>(Loop::SnapshotWindowGatherPath::Overlay),
                    static_cast<int>(overlayPath));
  const NoteUtils::DisplayNoteVec overlayNotes =
      NoteUtils::reconstructDisplayNotes(overlayFlat, loop.loopLengthTicks, false, false);

  SessionMidiEventVec committed;
  loop.copyEffectiveCommittedEvents(committed);
  const NoteUtils::DisplayNoteVec oracle =
      NoteUtils::reconstructDisplayNotes(committed, loop.loopLengthTicks, false, false);
  TEST_ASSERT_EQUAL(1u, countNotesAtPitchStart(oracle, 39, 192));
  TEST_ASSERT_TRUE(noteEndAtPitchStart(overlayNotes, 39, 192) !=
                   noteEndAtPitchStart(oracle, 39, 192));

  NoteUtils::DisplayNoteVec paintNotes;
  LoopContentResolution::resolveDisplayNotes(loop.passes, loop.loopLengthTicks, paintNotes);
  TEST_ASSERT_EQUAL(countNotesAtPitchStart(oracle, 39, 192),
                    countNotesAtPitchStart(paintNotes, 39, 192));
  TEST_ASSERT_EQUAL(noteEndAtPitchStart(oracle, 39, 192), noteEndAtPitchStart(paintNotes, 39, 192));
  TEST_ASSERT_EQUAL(1u, countNotesAtPitchStart(paintNotes, 39, 192));
  LoopContentResolution::deviceGateReset();
}

void test_idle_slice_without_snapshot_uses_passes_resolve() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  seedRecordNote(loop, 192, 256, 39);
  TEST_ASSERT_NULL(loop.publishedSnapshot().get());
  loop.markDisplayCachesStale();
  loop.rebuildVisualCacheIdleSlice(4, 0, UINT32_MAX, false);
  TEST_ASSERT_FALSE(loop.lastVisualCacheIdleOverlayRejected());
  TEST_ASSERT_EQUAL(static_cast<int>(Loop::VisualCacheIdleGatherSource::PassesResolve),
                    static_cast<int>(loop.lastVisualCacheIdleGatherSource()));
  TEST_ASSERT_EQUAL(1u, countNotesAtPitchStart(loop.visualCache.notes, 39, 192));
  TEST_ASSERT_EQUAL(256u, noteEndAtPitchStart(loop.visualCache.notes, 39, 192));
  LoopContentResolution::deviceGateReset();
}

void test_idle_slice_incomplete_fallback_budget_keeps_dirty() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  loop.loopLengthTicks = kLoopLen;
  loop.loopId = 1;
  loop.beginCapture(CapturePhase::Record);
  for (uint32_t i = 0; i < 8; ++i) {
    const uint32_t onTick = i * 80u;
    TEST_ASSERT_TRUE(
        loop.appendCaptureEvent(noteOnWithNoteId(onTick, 1, 60, 100, static_cast<NoteId>(i + 1))));
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(onTick + 40u, 1, 60, 0)));
  }
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::RecordStop, 640));
  TEST_ASSERT_NULL(loop.publishedSnapshot().get());
  TEST_ASSERT_TRUE(loop.isPlaybackSessionExitPublicationGap());
  loop.markDisplayCachesStale();
  const bool dirtyBefore = loop.visualCacheDirty;
  TEST_ASSERT_TRUE(dirtyBefore);
  loop.rebuildVisualCacheIdleSlice(4, 0, UINT32_MAX, false, false, 1);
  TEST_ASSERT_TRUE(loop.visualCacheDirty);
  TEST_ASSERT_EQUAL(static_cast<int>(Loop::VisualCacheIdleGatherSource::None),
                    static_cast<int>(loop.lastVisualCacheIdleGatherSource()));
  TEST_ASSERT_FALSE(loop.visualCache.dirtyBars.empty());
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
  loop.nextNoteId_ = 3;
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
  TEST_ASSERT_FALSE(loop.shouldCommitOverdubWrap(kLoopLen - 1, 0));
  loop.armOverdubWrapAfterLeavingStart(778);
  TEST_ASSERT_TRUE(loop.shouldCommitOverdubWrap(kLoopLen - 1, 0));
  TEST_ASSERT_FALSE(loop.shouldCommitOverdubWrap(776, 777));
  loop.noteOverdubWrapCommitted();
  TEST_ASSERT_FALSE(loop.shouldCommitOverdubWrap(kLoopLen - 1, 0));
}

void test_should_commit_overdub_wrap_at_phase_0_not_session_start() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kFourBar = 3072;
  constexpr uint32_t kSessionStart = 640;
  loop.loopLengthTicks = kFourBar;
  loop.openOverdubSession(kSessionStart);
  TEST_ASSERT_FALSE(loop.shouldCommitOverdubWrap(kFourBar - 1, 0));
  loop.armOverdubWrapAfterLeavingStart(kSessionStart + 1);
  TEST_ASSERT_TRUE(loop.shouldCommitOverdubWrap(kFourBar - 1, 0));
  TEST_ASSERT_FALSE(loop.shouldCommitOverdubWrap(0, 1));
  TEST_ASSERT_FALSE(loop.shouldCommitOverdubWrap(kSessionStart - 1, kSessionStart));
}

void test_should_commit_overdub_wrap_at_zero_session_start() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = 3072;
  loop.openOverdubSession(0);
  TEST_ASSERT_FALSE(loop.shouldCommitOverdubWrap(3071, 0));
  loop.armOverdubWrapAfterLeavingStart(1);
  TEST_ASSERT_FALSE(loop.shouldCommitOverdubWrap(0, 1));
  TEST_ASSERT_TRUE(loop.shouldCommitOverdubWrap(3071, 0));
}

void test_first_wrap_seal_at_phase_0_keeps_tail_linear() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kFourBar = 3072;
  constexpr uint32_t kSessionStart = 640;
  seedRecordNote(loop, 0, 48, 60);
  loop.loopLengthTicks = kFourBar;
  loop.openOverdubSession(kSessionStart);
  loop.beginCapture(CapturePhase::Overdub, kSessionStart);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(2304, 4, 63, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(2880, 4, 63, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(2800, 4, 70, 100)));
  loop.armOverdubWrapAfterLeavingStart(kSessionStart + 1);
  TEST_ASSERT_FALSE(loop.shouldCommitOverdubWrap(kSessionStart - 1, kSessionStart));
  TEST_ASSERT_TRUE(loop.shouldCommitOverdubWrap(kFourBar - 1, 0));

  SessionMidiEventVec held;
  TEST_ASSERT_EQUAL(1u, loop.extractOpenCaptureNoteOns(held));
  TEST_ASSERT_EQUAL(2800u, held[0].tick);
  TEST_ASSERT_EQUAL(70, held[0].data.noteData.note);

  SessionMidiEventVec remaining;
  loop.capture.store.copyEventsTo(remaining);
  TEST_ASSERT_EQUAL(2u, remaining.size());
  TEST_ASSERT_EQUAL(2304u, remaining[0].tick);
  TEST_ASSERT_EQUAL(2880u, remaining[1].tick);
  TEST_ASSERT_FALSE(NoteUtils::isWrappedLoopNotePair(remaining[0].tick, remaining[1].tick,
                                                     kFourBar));

  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 0));
  bool foundSeal = false;
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    if (pass.id != loop.lastCommittedPassId()) {
      continue;
    }
    foundSeal = true;
    TEST_ASSERT_EQUAL(0u, pass.sealedAtTick);
    SessionMidiEventVec sealed;
    LoopEventStore::appendChunkRefEvents(pass.committedChunkIds, sealed);
    TEST_ASSERT_EQUAL(2u, sealed.size());
    TEST_ASSERT_EQUAL(2304u, sealed[0].tick);
    TEST_ASSERT_EQUAL(2880u, sealed[1].tick);
    for (const MidiEvent& evt : sealed) {
      TEST_ASSERT_TRUE(evt.tick >= kSessionStart);
      TEST_ASSERT_TRUE(evt.tick < kFourBar);
    }
  }
  TEST_ASSERT_TRUE(foundSeal);

  loop.beginCapture(CapturePhase::Overdub, loop.playheadPhaseTick);
  TEST_ASSERT_EQUAL(kSessionStart, loop.playheadPhaseTick);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(held[0]));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(200, 4, 70, 0)));
  SessionMidiEventVec wrap2;
  loop.capture.store.copyEventsTo(wrap2);
  TEST_ASSERT_EQUAL(2u, wrap2.size());
  TEST_ASSERT_EQUAL(2800u, wrap2[0].tick);
  TEST_ASSERT_EQUAL(200u, wrap2[1].tick);
  bool inventedOffAtZero = false;
  for (const MidiEvent& evt : wrap2) {
    if (evt.isNoteOff() && evt.tick == 0) {
      inventedOffAtZero = true;
    }
  }
  TEST_ASSERT_FALSE(inventedOffAtZero);
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

// 032228 wrap 5 a=0,b=1 after undo to record layer; wrap 6 a=1,b=2 after re-occupy.
// B restored Disabled wrap-layer companion ids that A/vch already dropped.
void test_prepared_hold_ids_pin_undo_to_record_then_rewrap_a_equals_b() {
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

  constexpr uint32_t kHoldTick = 100;
  constexpr uint8_t kPitch = 60;
  OverlapNoteIdSet occupy;
  loop.collectOverdubSourceHoldParticipantIds(kHoldTick, kPitch, occupy);
  TEST_ASSERT_TRUE(occupy.contains(1));
  const EditPassIdList wrap1Companions =
      commitSamePitchWrapAndPublish(loop, 10, 64, 240, occupy);
  loop.pushOverdubSessionPass(loop.lastCommittedPassId(), wrap1Companions);

  occupy.clear();
  loop.collectOverdubSourceHoldParticipantIds(kHoldTick, kPitch, occupy);
  TEST_ASSERT_TRUE(occupy.contains(10));
  const EditPassIdList wrap2Companions =
      commitSamePitchWrapAndPublish(loop, 11, 64, 288, occupy);
  loop.pushOverdubSessionPass(loop.lastCommittedPassId(), wrap2Companions);

  TEST_ASSERT_TRUE(loop.undoOverdubSession());
  TEST_ASSERT_TRUE(loop.undoOverdubSession());

  OverlapNoteIdSet aRecord;
  OverlapNoteIdSet bRecord;
  OverlapNoteIdSet aoRecord;
  OverlapNoteIdSet boRecord;
  pinHoldSetsAfterWrap(0, loop, kHoldTick, kPitch, aRecord, bRecord, aoRecord, boRecord);
  TEST_ASSERT_TRUE(aRecord == bRecord);
  TEST_ASSERT_TRUE(aRecord.contains(1));
  TEST_ASSERT_FALSE(aRecord.contains(10));
  TEST_ASSERT_FALSE(aRecord.contains(11));
  TEST_ASSERT_FALSE(bRecord.contains(10));
  TEST_ASSERT_FALSE(bRecord.contains(11));
  TEST_ASSERT_EQUAL(0u, aoRecord.size());
  TEST_ASSERT_EQUAL(0u, boRecord.size());
  TEST_ASSERT_TRUE(sourceViewHasNoteId(loop, 1));
  TEST_ASSERT_FALSE(sourceViewHasNoteId(loop, 10));
  TEST_ASSERT_FALSE(sourceViewHasNoteId(loop, 11));

  occupy.clear();
  loop.collectOverdubSourceHoldParticipantIds(kHoldTick, kPitch, occupy);
  TEST_ASSERT_TRUE(occupy.contains(1));
  TEST_ASSERT_FALSE(occupy.contains(10));
  (void)commitSamePitchWrapAndPublish(loop, 12, 64, 240, occupy);

  OverlapNoteIdSet aRewrap;
  OverlapNoteIdSet bRewrap;
  OverlapNoteIdSet aoRewrap;
  OverlapNoteIdSet boRewrap;
  pinHoldSetsAfterWrap(1, loop, kHoldTick, kPitch, aRewrap, bRewrap, aoRewrap, boRewrap);
  TEST_ASSERT_TRUE(aRewrap == bRewrap);
  TEST_ASSERT_TRUE(aRewrap.contains(12));
  TEST_ASSERT_FALSE(aRewrap.contains(10));
  TEST_ASSERT_FALSE(bRewrap.contains(10));
  TEST_ASSERT_FALSE(bRewrap.contains(11));
  TEST_ASSERT_EQUAL(0u, aoRewrap.size());
  TEST_ASSERT_EQUAL(0u, boRewrap.size());
  TEST_ASSERT_FALSE(sourceViewHasNoteId(loop, 1));
  TEST_ASSERT_TRUE(sourceViewHasNoteId(loop, 12));
  TEST_ASSERT_FALSE(sourceViewHasNoteId(loop, 10));
  TEST_ASSERT_FALSE(sourceViewHasNoteId(loop, 11));
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

// 013327 a=0 at storage 64. Wrap-crossing on@2976 off@96 (id 10) then
// on@3000 off@80 (id 11). Merged reconstruct omits the pair (015618).
// Source-view rebuild fills via appendOverdubPassWrapPairedNotes so A==B.
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
                                                    loop.loopLengthTicks, loop.passes.editPasses,
                                                    EditPassIdList{});
  loop.rebuildOverdubSourceView(0);

  OverlapNoteIdSet a64;
  OverlapNoteIdSet b64;
  OverlapNoteIdSet ao64;
  OverlapNoteIdSet bo64;
  pinHoldSetsAfterWrap(1, loop, 64, 60, a64, b64, ao64, bo64);

  TEST_ASSERT_TRUE(a64 == b64);
  TEST_ASSERT_TRUE(a64.contains(10));
  TEST_ASSERT_EQUAL(0u, ao64.size());
  TEST_ASSERT_EQUAL(0u, bo64.size());
  TEST_ASSERT_TRUE(sourceViewHasNoteId(loop, 10));

  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(3000, 1, 60, 90, 11)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(80, 1, 60, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubWrap, 0));
  const OverdubPass* wrap2 = findOverdubPass(loop, loop.lastCommittedPassId());
  TEST_ASSERT_NOT_NULL(wrap2);
  LoopContentResolution::publishPreparedOverdubPass(*wrap2, loop.playbackRevision,
                                                    loop.loopLengthTicks, loop.passes.editPasses,
                                                    EditPassIdList{});
  loop.rebuildOverdubSourceView(0);

  OverlapNoteIdSet a64w2;
  OverlapNoteIdSet b64w2;
  OverlapNoteIdSet ao64w2;
  OverlapNoteIdSet bo64w2;
  pinHoldSetsAfterWrap(2, loop, 64, 60, a64w2, b64w2, ao64w2, bo64w2);
  TEST_ASSERT_TRUE(a64w2 == b64w2);
  TEST_ASSERT_TRUE(a64w2.contains(10));
  TEST_ASSERT_TRUE(a64w2.contains(11));
  TEST_ASSERT_EQUAL(0u, ao64w2.size());
  TEST_ASSERT_EQUAL(0u, bo64w2.size());
  LoopContentResolution::deviceGateReset();
}

// 021716 pre-wrap: prepared checkpoints finish unpaired ONs; source-view MIDI
// reconstruct does not (finishOpenNotes=false). 1-bar loop. Hold at 64 before wrap.
void test_prepared_hold_ids_pin_finished_open_covering_64() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  loop.loopLengthTicks = Config::TICKS_PER_BAR;
  LoopEventStore record;
  TEST_ASSERT_TRUE(storeAppendNoteOn(record, 200, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(300, 1, 60, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(record, 10, 1, 60, 100, 2));
  loop.seedRecordPassFromStore(record);
  LoopContentResolution::DeviceGateSample sample;
  LoopContentResolution::measureDeviceGate(loop.passes, loop.loopLengthTicks, sample);
  LoopContentResolution::deviceGateComplete(loop.playbackRevision);
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  loop.establishOverdubSourceView(0);

  OverlapNoteIdSet a64;
  OverlapNoteIdSet b64;
  OverlapNoteIdSet ao64;
  OverlapNoteIdSet bo64;
  pinHoldSetsAfterWrap(0, loop, 64, 60, a64, b64, ao64, bo64);
  TEST_ASSERT_TRUE(a64 == b64);
  TEST_ASSERT_TRUE(a64.contains(2));
  TEST_ASSERT_EQUAL(0u, ao64.size());
  TEST_ASSERT_EQUAL(0u, bo64.size());
  TEST_ASSERT_TRUE(sourceViewHasNoteId(loop, 2));
  LoopContentResolution::deviceGateReset();
}

// 1-bar tailStart=0: complete wrap-crossing record pair is not the finished-open class.
void test_prepared_hold_ids_pin_complete_wrap_pair_covering_64() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  loop.loopLengthTicks = Config::TICKS_PER_BAR;
  LoopEventStore record;
  TEST_ASSERT_TRUE(storeAppendNoteOn(record, 720, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(96, 1, 60, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(record, 200, 1, 60, 100, 2));
  TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(300, 1, 60, 0)));
  loop.seedRecordPassFromStore(record);
  LoopContentResolution::DeviceGateSample sample;
  LoopContentResolution::measureDeviceGate(loop.passes, loop.loopLengthTicks, sample);
  LoopContentResolution::deviceGateComplete(loop.playbackRevision);
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  loop.establishOverdubSourceView(0);

  OverlapNoteIdSet a64;
  OverlapNoteIdSet b64;
  OverlapNoteIdSet ao64;
  OverlapNoteIdSet bo64;
  pinHoldSetsAfterWrap(0, loop, 64, 60, a64, b64, ao64, bo64);
  TEST_ASSERT_TRUE(a64 == b64);
  TEST_ASSERT_TRUE(a64.contains(1));
  TEST_ASSERT_EQUAL(0u, ao64.size());
  TEST_ASSERT_EQUAL(0u, bo64.size());
  LoopContentResolution::deviceGateReset();
}

// 030958: wrap head+tail share NoteId 1. Hide must collapse both so B at 64
// does not keep the 0–96 tail after the window-NoteOn filter drops A.
void test_prepared_hold_ids_pin_wrap_pair_hide_drops_tail_at_64() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  loop.loopLengthTicks = Config::TICKS_PER_BAR;
  LoopEventStore record;
  TEST_ASSERT_TRUE(storeAppendNoteOn(record, 720, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(96, 1, 60, 0)));
  loop.seedRecordPassFromStore(record);
  LoopContentResolution::DeviceGateSample sample;
  LoopContentResolution::measureDeviceGate(loop.passes, loop.loopLengthTicks, sample);
  LoopContentResolution::deviceGateComplete(loop.playbackRevision);
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  loop.establishOverdubSourceView(0);

  OverlapNoteIdSet aBefore;
  OverlapNoteIdSet bBefore;
  OverlapNoteIdSet aoBefore;
  OverlapNoteIdSet boBefore;
  pinHoldSetsAfterWrap(0, loop, 64, 60, aBefore, bBefore, aoBefore, boBefore);
  TEST_ASSERT_TRUE(aBefore.contains(1));
  TEST_ASSERT_TRUE(bBefore.contains(1));

  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(200, 1, 72, 90, 10)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(400, 1, 72, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubWrap, 0));
  const OverdubPass* wrap = findOverdubPass(loop, loop.lastCommittedPassId());
  TEST_ASSERT_NOT_NULL(wrap);
  EditPass hide{};
  hide.passType = EditPassType::Note;
  hide.actionType = EditActionType::Delete;
  hide.targetNoteId = 1;
  const EditPassId hideId =
      loop.saveNoteEditPass(kOverdubCompanionEditPassIndex, std::move(hide), EditPassType::Note);
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, hideId);
  LoopContentResolution::publishPreparedOverdubPass(*wrap, loop.playbackRevision,
                                                    loop.loopLengthTicks, loop.passes.editPasses,
                                                    EditPassIdList{hideId});
  loop.rebuildOverdubSourceView(0);

  OverlapNoteIdSet a64;
  OverlapNoteIdSet b64;
  OverlapNoteIdSet ao64;
  OverlapNoteIdSet bo64;
  pinHoldSetsAfterWrap(1, loop, 64, 60, a64, b64, ao64, bo64);
  TEST_ASSERT_TRUE(a64 == b64);
  TEST_ASSERT_FALSE(a64.contains(1));
  TEST_ASSERT_FALSE(b64.contains(1));
  TEST_ASSERT_EQUAL(0u, ao64.size());
  TEST_ASSERT_EQUAL(0u, bo64.size());
  LoopContentResolution::deviceGateReset();
}

// Seven unpaired ONs covering 64 when checkpoints finish them. Not one open note.
void test_source_view_includes_seven_finished_opens_covering_64() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  loop.loopLengthTicks = Config::TICKS_PER_BAR;
  LoopEventStore record;
  for (uint8_t i = 0; i < 7; ++i) {
    TEST_ASSERT_TRUE(storeAppendNoteOn(record, static_cast<uint32_t>(10 + i * 4), 1,
                                       static_cast<uint8_t>(60 + i), 100,
                                       static_cast<NoteId>(i + 1)));
  }
  loop.seedRecordPassFromStore(record);
  loop.rebuildVisualCacheFromPasses();
  const size_t visualCount = loop.visualCache.notes.size();
  LoopContentResolution::DeviceGateSample sample;
  LoopContentResolution::measureDeviceGate(loop.passes, loop.loopLengthTicks, sample);
  LoopContentResolution::deviceGateComplete(loop.playbackRevision);
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  const size_t sourceCount = loop.overdubSourceViewNotes().size();
  printf("seven-finished-opens visual=%u source=%u\n", static_cast<unsigned>(visualCount),
         static_cast<unsigned>(sourceCount));
  TEST_ASSERT_EQUAL(7u, sourceCount);
  (void)visualCount;
  for (uint8_t i = 0; i < 7; ++i) {
    OverlapNoteIdSet a64;
    OverlapNoteIdSet b64;
    OverlapNoteIdSet ao64;
    OverlapNoteIdSet bo64;
    pinHoldSetsAfterWrap(0, loop, 64, static_cast<uint8_t>(60 + i), a64, b64, ao64, bo64);
    TEST_ASSERT_TRUE(a64 == b64);
    TEST_ASSERT_TRUE(a64.contains(static_cast<NoteId>(i + 1)));
    TEST_ASSERT_EQUAL(0u, ao64.size());
    TEST_ASSERT_EQUAL(0u, bo64.size());
  }
  LoopContentResolution::deviceGateReset();
}

void test_long_loop_pass_state_change_keeps_notes_and_stays_dirty() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kSixtyEightBars = Config::TICKS_PER_BAR * 68;
  loop.loopLengthTicks = kSixtyEightBars;
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 10, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(58, 1, 60, 0)));
  loop.seedRecordPassFromStore(store);
  loop.rebuildVisualCacheFromPasses();
  const size_t notesBefore = loop.visualCache.notes.size();
  TEST_ASSERT_TRUE(notesBefore > 0);
  TEST_ASSERT_FALSE(loop.visualCacheDirty);
  loop.refreshVisualCacheAfterPassStateChange();
  TEST_ASSERT_TRUE(loop.visualCacheDirty);
  TEST_ASSERT_EQUAL(notesBefore, loop.visualCache.notes.size());
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 60, 10));
}

void test_budgeted_idle_slice_defers_wrap_append_then_resumes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  seedWrapHeldOverdubLoop(loop);
  TEST_ASSERT_FALSE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));
  loop.markDisplayCachesStale();
  uint8_t slices = 0;
  while (loop.visualCacheDirty && slices < 16) {
    loop.rebuildVisualCacheIdleSlice(4, 0, UINT32_MAX, false, false, 192);
    ++slices;
  }
  TEST_ASSERT_FALSE(loop.visualCacheDirty);
  TEST_ASSERT_TRUE(loop.hasDeferredVisualCacheWrapAppend());
  TEST_ASSERT_FALSE(hasDisplayNote(loop.visualCache.notes, 12, 2976));
  TEST_ASSERT_FALSE(hasDisplayNote(loop.visualCache.notes, 12, 0));
  loop.applyDeferredVisualCacheWrapAppend();
  TEST_ASSERT_FALSE(loop.hasDeferredVisualCacheWrapAppend());
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 12, 2976));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 12, 0));
  LoopContentResolution::deviceGateReset();
}

void drainBudgetedIdleSlicesUntilDeferredWrapAppend(Loop& loop) {
  loop.markDisplayCachesStale();
  uint8_t slices = 0;
  while (loop.visualCacheDirty && slices < 16) {
    loop.rebuildVisualCacheIdleSlice(4, 0, UINT32_MAX, false, false, 192);
    ++slices;
  }
  TEST_ASSERT_FALSE(loop.visualCacheDirty);
  TEST_ASSERT_TRUE(loop.hasDeferredVisualCacheWrapAppend());
}

void test_deferred_wrap_append_skips_when_current_source_already_built() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  seedWrapHeldOverdubLoop(loop);
  drainBudgetedIdleSlicesUntilDeferredWrapAppend(loop);
  TEST_ASSERT_FALSE(hasDisplayNote(loop.visualCache.notes, 12, 2976));
  TEST_ASSERT_FALSE(hasDisplayNote(loop.visualCache.notes, 12, 0));

  loop.playbackRevision += 1;
  // Snapshot-current skip requires a non-null matching identity (OpenSpec 4.6).
  loop.storePublishedSnapshotForTest(
      LoopSnapshot::createEmpty(loop.loopId, loop.loopLengthTicks, loop.loopStartTick, 1, 1),
      loop.playbackRevision);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_NOT_NULL(loop.publishedSnapshotIdentity());
  NoteUtils::DisplayNote currentSource{};
  currentSource.noteId = 99;
  currentSource.note = 64;
  currentSource.velocity = 100;
  currentSource.startTick = 100;
  currentSource.endTick = 200;
  loop.visualCache.notes.clear();
  loop.visualCache.notes.push_back(currentSource);
  loop.visualCache.builtFromRevision = loop.playbackRevision;
  loop.visualCache.builtFromSnapshotIdentity = loop.publishedSnapshotIdentity();
  loop.visualCacheDirty = false;

  loop.applyDeferredVisualCacheWrapAppend();
  TEST_ASSERT_FALSE(loop.hasDeferredVisualCacheWrapAppend());
  TEST_ASSERT_EQUAL(1u, loop.visualCache.notes.size());
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 64, 100));
  TEST_ASSERT_FALSE(hasDisplayNote(loop.visualCache.notes, 12, 2976));
  TEST_ASSERT_FALSE(hasDisplayNote(loop.visualCache.notes, 12, 0));
  TEST_ASSERT_FALSE(hasDuplicatePitchStartNoteId(loop.visualCache.notes));
  LoopContentResolution::deviceGateReset();
}

void test_deferred_wrap_append_rebuilds_when_source_revision_moved() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  seedWrapHeldOverdubLoop(loop);
  drainBudgetedIdleSlicesUntilDeferredWrapAppend(loop);
  TEST_ASSERT_FALSE(hasDisplayNote(loop.visualCache.notes, 12, 2976));
  TEST_ASSERT_FALSE(hasDisplayNote(loop.visualCache.notes, 12, 0));

  loop.playbackRevision += 1;
  TEST_ASSERT_NOT_EQUAL(loop.playbackRevision, loop.visualCache.builtFromRevision);

  loop.applyDeferredVisualCacheWrapAppend();
  TEST_ASSERT_FALSE(loop.hasDeferredVisualCacheWrapAppend());
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 12, 2976));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, 12, 0));
  TEST_ASSERT_FALSE(hasDuplicatePitchStartNoteId(loop.visualCache.notes));
  TEST_ASSERT_EQUAL_UINT32(loop.playbackRevision, loop.visualCache.builtFromRevision);
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
  loop.copyEffectiveCommittedEvents(flat);
  const NoteUtils::DisplayNoteVec materialized =
      NoteUtils::reconstructDisplayNotes(flat, loop.loopLengthTicks, false, false);
  TEST_ASSERT_EQUAL(materialized.size(), loop.visualCache.notes.size());
}

bool drainSnapshotConstruction(LoopSnapshotConstruction& construction) {
  size_t steps = 0;
  while (!construction.complete() && !construction.failed() && steps < 10000) {
    construction.step(1);
    ++steps;
  }
  return construction.complete() && !construction.failed();
}

struct SnapshotNoteSpec {
  NoteId noteId;
  uint8_t pitch;
  uint32_t startTick;
  uint32_t endTick;
};

ImmutableLoopSnapshotRef buildSnapshotWithNotes(LoopId loopId, uint32_t loopLength,
                                                const SnapshotNoteSpec* specs, size_t count) {
  auto empty = std::make_shared<const LoopSnapshot>(LoopSnapshot(loopId, loopLength, 0, 100, 100));
  LoopSnapshotChangeSet changes;
  for (size_t i = 0; i < count; ++i) {
    SnapshotNoteChange add{};
    add.kind = SnapshotChangeKind::Add;
    add.next.loopId = loopId;
    add.next.noteId = specs[i].noteId;
    add.next.pitch = specs[i].pitch;
    add.next.velocity = 100;
    add.next.startTick = specs[i].startTick;
    add.next.endTick = specs[i].endTick;
    changes.notes.push_back(add);
  }
  LoopSnapshotConstruction construction(empty, changes);
  TEST_ASSERT_TRUE(drainSnapshotConstruction(construction));
  const ImmutableLoopSnapshotRef snapshot = construction.freeze();
  TEST_ASSERT_TRUE(static_cast<bool>(snapshot));
  for (size_t i = 0; i < count; ++i) {
    TEST_ASSERT_TRUE(static_cast<bool>(snapshot->findNote(specs[i].noteId, specs[i].pitch)));
  }
  return snapshot;
}

ImmutableLoopSnapshotRef buildOccupySnapshot(LoopId loopId, uint32_t loopLength, NoteId noteId,
                                             uint8_t pitch, uint32_t startTick, uint32_t endTick) {
  const SnapshotNoteSpec spec{noteId, pitch, startTick, endTick};
  return buildSnapshotWithNotes(loopId, loopLength, &spec, 1);
}

void plantVisualGeometry(Loop& loop, NoteId noteId, uint8_t pitch, uint32_t startTick,
                         uint32_t endTick) {
  NoteUtils::DisplayNote note{};
  note.noteId = noteId;
  note.note = pitch;
  note.velocity = 100;
  note.startTick = startTick;
  note.endTick = endTick;
  loop.visualCache.notes.clear();
  loop.visualCache.notes.push_back(note);
  loop.visualCache.dirtyBars.clear();
  loop.visualCacheDirty = false;
}

const NoteUtils::DisplayNote* findSpanCacheNote(const Loop& loop, NoteId noteId) {
  for (const NoteUtils::DisplayNote& note : loop.overdubSourceSpanCacheNotesForTest()) {
    if (note.noteId == noteId) {
      return &note;
    }
  }
  return nullptr;
}

void seedOccupyIdentityTriple(Loop& loop) {
  LoopContentResolution::deviceGateReset();
  loop.loopId = 1;
  seedRecordNote(loop, 10, 300, 60);
  loop.nextNoteId_ = 2;
  LoopContentResolution::DeviceGateSample sample;
  LoopContentResolution::measureDeviceGate(loop.passes, loop.loopLengthTicks, sample);
  LoopContentResolution::deviceGateComplete(loop.playbackRevision);
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));
  plantVisualGeometry(loop, 1, 60, 10, 200);
}

/// `212700`: current snapshot membership omits NoteId 84 while VisualCache
/// still has it. Gather reports old→0. Recovery uses committed geometry, then
/// pending Shorten paints the shortened end. Hide is not recovered here.
void test_draw_source_snapshot_miss_recovers_pending_shorten_212700() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 1;
  loop.loopLengthTicks = Config::TICKS_PER_BAR * 32;
  plantVisualGeometry(loop, 84, 84, 4560, 4608);
  // Dirty cache is not a reusable draw source, so gather still selects the
  // current empty snapshot and recovery must restore NoteId 84.
  loop.visualCacheDirty = true;
  loop.storePublishedSnapshotForTest(
      LoopSnapshot::createEmpty(loop.loopId, loop.loopLengthTicks, loop.loopStartTick, 1, 1),
      loop.playbackRevision);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  loop.openOverdubSession(0);
  TEST_ASSERT_TRUE(DisplayWindowUtils::shouldGatherPublishedSnapshotForDrawSource(
      static_cast<bool>(loop.publishedSnapshot()), loop.captureActive(), loop.hasOverdubSession(),
      !loop.visualCache.notes.empty(), loop.visualCacheDirty, loop.publishedSnapshotIsCurrent()));

  NoteUtils::DisplayNoteVec compose;
  const uint32_t bar = 4560u / Config::TICKS_PER_BAR;
  gatherDrawSourcePrefix(loop, bar, bar, 84, 84, compose);
  TEST_ASSERT_FALSE(DisplayWindowUtils::displayNotesContainNoteId(compose, 84));

  NoteUtils::DisplayNoteVec authoritative;
  NoteUtils::DisplayNote committed{};
  committed.noteId = 84;
  committed.note = 84;
  committed.velocity = 100;
  committed.startTick = 4560;
  committed.endTick = 4608;
  authoritative.push_back(committed);
  const NoteId shortenIds[] = {84};
  const DisplayWindowUtils::PendingShortenRecoveryCounts recovered =
      DisplayWindowUtils::recoverMissingPendingShortenRows(
          compose, loop.visualCache.notes, shortenIds, 1, authoritative);
  TEST_ASSERT_EQUAL_UINT32(1u, recovered.recovered);
  TEST_ASSERT_TRUE(DisplayWindowUtils::displayNotesContainNoteId(compose, 84));
  TEST_ASSERT_EQUAL_UINT32(4608u, compose[0].endTick);

  PendingNoteChange shorten{};
  shorten.kind = PendingNoteChangeKind::Shorten;
  shorten.noteId = 84;
  shorten.pitch = 84;
  shorten.startTick = 4560;
  shorten.endTick = 4575;
  loop.pushPendingNoteChangeForTest(shorten);
  NoteUtils::DisplayNoteVec painted = compose;
  loop.applyPendingNoteChangesToDisplayNotes(painted, painted.size());
  TEST_ASSERT_EQUAL_UINT32(4608u, compose[0].endTick);
  const DisplayWindowUtils::PaintedPendingShorten described =
      DisplayWindowUtils::describePaintedPendingShorten(compose, painted, 84, 4575);
  TEST_ASSERT_TRUE(described.composeHasTarget);
  TEST_ASSERT_EQUAL_UINT32(4608u, described.composeEndTick);
  TEST_ASSERT_TRUE(described.paintHasShortenedEnd);
  TEST_ASSERT_EQUAL_UINT32(4575u, described.paintEndTick);
}

void test_incomplete_visual_cache_does_not_hide_committed_occupy_identity_174151() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  loop.loopId = 1;
  loop.loopLengthTicks = 5376;
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 720, 1, 69, 100, 16));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(768, 1, 69, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 5328, 1, 69, 100, 112));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(5375, 1, 69, 0)));
  loop.seedRecordPassFromStore(store);
  loop.nextNoteId_ = 113;
  const SnapshotNoteSpec publishedNotes[] = {
      {16, 69, 720, 768},
      {112, 69, 5328, 5375},
  };
  loop.storePublishedSnapshotForTest(
      buildSnapshotWithNotes(loop.loopId, loop.loopLengthTicks, publishedNotes, 2),
      loop.playbackRevision);
  plantVisualGeometry(loop, 16, 69, 720, 768);
  TEST_ASSERT_NULL(findSpanCacheNote(loop, 112));
  loop.openOverdubSession(0);
  const char* from = loop.rebuildOverdubSourceSpanCacheForTest();
  TEST_ASSERT_TRUE(std::strcmp(from, "empty") != 0);
  const NoteUtils::DisplayNote* omitted = findSpanCacheNote(loop, 112);
  TEST_ASSERT_NOT_NULL_MESSAGE(omitted,
                               "incomplete visualCache must not hide committed id=112");
  TEST_ASSERT_EQUAL_UINT32(5328u, omitted->startTick);
  TEST_ASSERT_EQUAL_UINT32(5375u, omitted->endTick);
  TEST_ASSERT_NOT_NULL(findSpanCacheNote(loop, 16));
}

/// After membership RC-A + Stage 5.5 unwind, `from=snapshot` trusts published
/// NoteRefs and does not mergeOmitted over an incomplete planted root.
void test_snapshot_span_cache_trusts_published_membership_without_merge_omitted() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 1;
  loop.loopLengthTicks = 2304;
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 1452, 1, 73, 100, 26));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(1500, 1, 73, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 1500, 1, 74, 100, 27));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(1554, 1, 74, 0)));
  loop.seedRecordPassFromStore(store);
  loop.nextNoteId_ = 28;

  loop.storePublishedSnapshotForTest(
      buildOccupySnapshot(loop.loopId, loop.loopLengthTicks, 26, 73, 1452, 1500),
      loop.playbackRevision);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());

  NoteUtils::DisplayNoteVec fromSnap;
  appendSnapshotNotesForBarRange(*loop.publishedSnapshot(), 0, 2, fromSnap);
  TEST_ASSERT_NOT_NULL(findDisplayNoteById(fromSnap, 26));
  TEST_ASSERT_NULL(findDisplayNoteById(fromSnap, 27));

  loop.openOverdubSession(0);
  TEST_ASSERT_EQUAL_STRING("snapshot", loop.rebuildOverdubSourceSpanCacheForTest());
  TEST_ASSERT_NOT_NULL(findSpanCacheNote(loop, 26));
  TEST_ASSERT_NULL_MESSAGE(findSpanCacheNote(loop, 27),
                           "from=snapshot must not mergeOmitted omitted id=27");
}

/// After membership RC-A, visual rebuild trusts published snapshot NoteRefs.
/// An artificially incomplete snapshot must not be healed here (consumer
/// Stage 3 unwind).
void test_visual_cache_trusts_published_snapshot_membership_without_merge_omitted() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 1;
  loop.loopLengthTicks = 2304;
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 1452, 1, 73, 100, 26));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(1500, 1, 73, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 1500, 1, 74, 100, 27));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(1554, 1, 74, 0)));
  loop.seedRecordPassFromStore(store);
  loop.nextNoteId_ = 28;

  // Incomplete published root (pre-RC-A shape): only id=26.
  loop.storePublishedSnapshotForTest(
      buildOccupySnapshot(loop.loopId, loop.loopLengthTicks, 26, 73, 1452, 1500),
      loop.playbackRevision);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());

  loop.rebuildVisualCacheFromPasses();
  TEST_ASSERT_NOT_NULL(findDisplayNoteById(loop.visualCache.notes, 26));
  TEST_ASSERT_NULL_MESSAGE(findDisplayNoteById(loop.visualCache.notes, 27),
                           "visual must not mergeOmitted over incomplete snapshot");

  NoteUtils::DisplayNoteVec composed;
  NoteUtils::DisplayNote keep{};
  keep.noteId = 26;
  keep.note = 73;
  keep.velocity = 100;
  keep.startTick = 1452;
  keep.endTick = 1500;
  composed.push_back(keep);
  loop.adoptComposedDisplayNotesFromViewport(composed);
  TEST_ASSERT_NOT_NULL(findDisplayNoteById(loop.visualCache.notes, 26));
  TEST_ASSERT_NULL_MESSAGE(findDisplayNoteById(loop.visualCache.notes, 27),
                           "adopt must not mergeOmitted omitted id=27");
}

/// `202713`: sealed Hide must still apply on `from=snapshot` without event merge
/// (Stage 4 KEEP). Complete published membership supplies id=27 — not mergeOmitted.
void test_snapshot_span_cache_keeps_sealed_hide_without_merge_omitted_202713() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 1;
  loop.loopLengthTicks = 2304;
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 1452, 1, 73, 100, 26));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(1500, 1, 73, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 1500, 1, 74, 100, 27));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(1554, 1, 74, 0)));
  loop.seedRecordPassFromStore(store);
  loop.nextNoteId_ = 28;

  auto empty = std::make_shared<const LoopSnapshot>(
      LoopSnapshot(loop.loopId, loop.loopLengthTicks, 0, 100, 100));
  LoopSnapshotChangeSet changes;
  for (const auto& geometry :
       {std::tuple<NoteId, uint8_t, uint32_t, uint32_t>{26, 73, 1452, 1500},
        std::tuple<NoteId, uint8_t, uint32_t, uint32_t>{27, 74, 1500, 1554}}) {
    SnapshotNoteChange add{};
    add.kind = SnapshotChangeKind::Add;
    add.next.loopId = loop.loopId;
    add.next.noteId = std::get<0>(geometry);
    add.next.pitch = std::get<1>(geometry);
    add.next.velocity = 100;
    add.next.startTick = std::get<2>(geometry);
    add.next.endTick = std::get<3>(geometry);
    changes.notes.push_back(add);
  }
  LoopSnapshotConstruction construction(empty, changes);
  TEST_ASSERT_TRUE(drainSnapshotConstruction(construction));
  const ImmutableLoopSnapshotRef completeSnap = construction.freeze();
  TEST_ASSERT_TRUE(static_cast<bool>(completeSnap));
  loop.storePublishedSnapshotForTest(completeSnap, loop.playbackRevision);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());

  EditPass hide{};
  hide.passType = EditPassType::Note;
  hide.actionType = EditActionType::Delete;
  hide.targetNoteId = 26;
  hide.pitch = 73;
  hide.startTick = 1452;
  hide.endTick = 1500;
  const EditPassId hideId =
      loop.saveNoteEditPass(kOverdubCompanionEditPassIndex, std::move(hide), EditPassType::Note);
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, hideId);
  EditPassIdList companions;
  companions.push_back(hideId);
  loop.recordLastOverdubSeal(1, companions, loop.playbackRevision);
  TEST_ASSERT_TRUE(loop.hasSealedCompanionOverlay());
  // Seal must not drop snapshot currency or the test falls to passes+merge.
  loop.storePublishedSnapshotForTest(completeSnap, loop.playbackRevision);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());

  loop.openOverdubSession(0);
  const char* from = loop.rebuildOverdubSourceSpanCacheForTest();
  TEST_ASSERT_EQUAL_STRING("snapshot", from);
  TEST_ASSERT_NULL_MESSAGE(findSpanCacheNote(loop, 26),
                           "sealed Hide must win on snapshot span cache");
  const NoteUtils::DisplayNote* kept = findSpanCacheNote(loop, 27);
  TEST_ASSERT_NOT_NULL_MESSAGE(kept, "complete snapshot NoteRef id=27 must remain");
  TEST_ASSERT_EQUAL_UINT32(74u, kept->note);
}

void test_occupy_predicate_true_uses_snapshot_not_visual_or_prepared() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedOccupyIdentityTriple(loop);
  loop.storePublishedSnapshotForTest(buildOccupySnapshot(loop.loopId, loop.loopLengthTicks, 1, 60, 10, 128),
                                     loop.playbackRevision);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  loop.openOverdubSession(0);
  TEST_ASSERT_TRUE(DisplayWindowUtils::shouldGatherPublishedSnapshotForDrawSource(
      static_cast<bool>(loop.publishedSnapshot()), loop.captureActive(), loop.hasOverdubSession(),
      !loop.visualCache.notes.empty(), loop.visualCacheDirty, loop.publishedSnapshotIsCurrent()));
  const char* from = loop.rebuildOverdubSourceSpanCacheForTest();
  TEST_ASSERT_EQUAL_STRING("snapshot", from);
  const NoteUtils::DisplayNote* occupy = findSpanCacheNote(loop, 1);
  TEST_ASSERT_NOT_NULL(occupy);
  TEST_ASSERT_EQUAL_UINT32(10u, occupy->startTick);
  TEST_ASSERT_EQUAL_UINT32(128u, occupy->endTick);
  TEST_ASSERT_EQUAL(1u, loop.overdubSourceSpanCacheNotesForTest().size());
  LoopContentResolution::deviceGateReset();
}

void test_snapshot_source_uses_bounded_overdub_window() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 1;
  loop.loopLengthTicks = Config::TICKS_PER_BAR * 32u;

  auto empty =
      std::make_shared<const LoopSnapshot>(
          LoopSnapshot(loop.loopId, loop.loopLengthTicks, 0, 100, 100));
  LoopSnapshotChangeSet changes;
  for (const auto& geometry : {
           std::array<uint32_t, 3>{1u, 100u, 140u},
           std::array<uint32_t, 3>{2u, Config::TICKS_PER_BAR * 20u + 100u,
                                   Config::TICKS_PER_BAR * 20u + 140u},
       }) {
    SnapshotNoteChange add{};
    add.kind = SnapshotChangeKind::Add;
    add.next.loopId = loop.loopId;
    add.next.noteId = geometry[0];
    add.next.pitch = 60;
    add.next.velocity = 100;
    add.next.startTick = geometry[1];
    add.next.endTick = geometry[2];
    changes.notes.push_back(add);
  }
  LoopSnapshotConstruction construction(empty, changes);
  TEST_ASSERT_TRUE(drainSnapshotConstruction(construction));
  loop.storePublishedSnapshotForTest(construction.freeze(), loop.playbackRevision);
  loop.openOverdubSession(0);

  const uint32_t sourceWindowLength = Config::TICKS_PER_BAR * 16u;
  TEST_ASSERT_EQUAL_STRING(
      "snapshot",
      loop.rebuildOverdubSourceSpanCacheForTest(0, sourceWindowLength));
  TEST_ASSERT_NOT_NULL(findSpanCacheNote(loop, 1));
  TEST_ASSERT_NULL(findSpanCacheNote(loop, 2));
  TEST_ASSERT_FALSE(loop.overdubSourceSpanCacheReadyForTest());
}

void test_ready_fallback_cache_does_not_override_current_snapshot() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 1;
  seedRecordNote(loop, 10, 58, 60);
  TEST_ASSERT_EQUAL_STRING("passes", loop.rebuildOverdubSourceSpanCacheForTest());
  TEST_ASSERT_TRUE(loop.overdubSourceSpanCacheReadyForTest());
  TEST_ASSERT_EQUAL_UINT32(0u, loop.overdubSourceSpanCachePublishedRevisionForTest());
  TEST_ASSERT_NULL(loop.overdubSourceSpanCachePublishedIdentityForTest());
  const NoteUtils::DisplayNote* fallback = findSpanCacheNote(loop, 1);
  TEST_ASSERT_NOT_NULL(fallback);
  TEST_ASSERT_EQUAL_UINT32(58u, fallback->endTick);

  loop.storePublishedSnapshotForTest(
      buildOccupySnapshot(loop.loopId, loop.loopLengthTicks, 1, 60, 10, 128),
      loop.playbackRevision);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_TRUE(loop.overdubSourceSpanCacheReadyForTest());
  TEST_ASSERT_FALSE(loop.overdubSourceSpanCacheReusableForTest());

  loop.openOverdubSession(0);
  loop.establishOverdubSourceView(0);
  const NoteUtils::DisplayNote* occupy = findSpanCacheNote(loop, 1);
  TEST_ASSERT_NOT_NULL(occupy);
  TEST_ASSERT_EQUAL_UINT32(128u, occupy->endTick);
  TEST_ASSERT_EQUAL_UINT32(loop.publishedSnapshotRevision(),
                           loop.overdubSourceSpanCachePublishedRevisionForTest());
  TEST_ASSERT_TRUE(loop.overdubSourceSpanCacheReusableForTest());
}

void test_snapshot_stamped_span_cache_is_reused() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 1;
  seedRecordNote(loop, 10, 58, 60);
  loop.storePublishedSnapshotForTest(
      buildOccupySnapshot(loop.loopId, loop.loopLengthTicks, 1, 60, 10, 128),
      loop.playbackRevision);
  TEST_ASSERT_EQUAL_STRING("snapshot", loop.rebuildOverdubSourceSpanCacheForTest());
  TEST_ASSERT_TRUE(loop.overdubSourceSpanCacheReusableForTest());
  TEST_ASSERT_EQUAL_UINT32(loop.publishedSnapshotRevision(),
                           loop.overdubSourceSpanCachePublishedRevisionForTest());
  TEST_ASSERT_EQUAL_PTR(loop.publishedSnapshotIdentity(),
                        loop.overdubSourceSpanCachePublishedIdentityForTest());

  loop.openOverdubSession(0);
  loop.establishOverdubSourceView(0);
  const NoteUtils::DisplayNote* occupy = findSpanCacheNote(loop, 1);
  TEST_ASSERT_NOT_NULL(occupy);
  TEST_ASSERT_EQUAL_UINT32(128u, occupy->endTick);
  TEST_ASSERT_EQUAL_UINT32(loop.publishedSnapshotRevision(),
                           loop.overdubSourceSpanCachePublishedRevisionForTest());
  TEST_ASSERT_EQUAL_PTR(loop.publishedSnapshotIdentity(),
                        loop.overdubSourceSpanCachePublishedIdentityForTest());
}

/// OpenSpec 4.6 Stage 3: same playback/published revision with a replaced root
/// must not reuse a span cache stamped from identity A.
void test_span_cache_identity_stale_when_published_root_replaced_same_revision() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 1;
  seedRecordNote(loop, 10, 58, 60);
  const uint32_t revisionR = loop.playbackRevision;
  const ImmutableLoopSnapshotRef snapshotA =
      buildOccupySnapshot(loop.loopId, loop.loopLengthTicks, 1, 60, 10, 128);
  loop.storePublishedSnapshotForTest(snapshotA, revisionR);
  TEST_ASSERT_EQUAL_STRING("snapshot", loop.rebuildOverdubSourceSpanCacheForTest());
  TEST_ASSERT_TRUE(loop.overdubSourceSpanCacheReusableForTest());
  const void* identityA = loop.publishedSnapshotIdentity();
  TEST_ASSERT_EQUAL_PTR(identityA, loop.overdubSourceSpanCachePublishedIdentityForTest());
  TEST_ASSERT_EQUAL_UINT32(128u, findSpanCacheNote(loop, 1)->endTick);

  const ImmutableLoopSnapshotRef snapshotB =
      buildOccupySnapshot(loop.loopId, loop.loopLengthTicks, 1, 60, 10, 176);
  TEST_ASSERT_TRUE(snapshotB.get() != snapshotA.get());
  loop.storePublishedSnapshotForTest(snapshotB, revisionR);
  TEST_ASSERT_EQUAL_UINT32(revisionR, loop.playbackRevision);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_TRUE(loop.publishedSnapshotIdentity() != identityA);
  TEST_ASSERT_TRUE(loop.overdubSourceSpanCacheReadyForTest());
  TEST_ASSERT_FALSE(loop.overdubSourceSpanCacheReusableForTest());

  TEST_ASSERT_EQUAL_STRING("snapshot", loop.rebuildOverdubSourceSpanCacheForTest());
  TEST_ASSERT_TRUE(loop.overdubSourceSpanCacheReusableForTest());
  TEST_ASSERT_EQUAL_PTR(loop.publishedSnapshotIdentity(),
                        loop.overdubSourceSpanCachePublishedIdentityForTest());
  TEST_ASSERT_EQUAL_UINT32(176u, findSpanCacheNote(loop, 1)->endTick);
}

void test_occupy_predicate_false_uses_prepared_not_snapshot() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedOccupyIdentityTriple(loop);
  loop.storePublishedSnapshotForTest(buildOccupySnapshot(loop.loopId, loop.loopLengthTicks, 1, 60, 10, 128),
                                     loop.playbackRevision);
  ++loop.playbackRevision;
  LoopContentResolution::deviceGateComplete(loop.playbackRevision);
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));
  loop.openOverdubSession(0);
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldGatherPublishedSnapshotForDrawSource(
      static_cast<bool>(loop.publishedSnapshot()), loop.captureActive(), loop.hasOverdubSession(),
      !loop.visualCache.notes.empty(), loop.visualCacheDirty, loop.publishedSnapshotIsCurrent()));
  const char* from = loop.rebuildOverdubSourceSpanCacheForTest();
  TEST_ASSERT_EQUAL_STRING("prepared", from);
  const NoteUtils::DisplayNote* occupy = findSpanCacheNote(loop, 1);
  TEST_ASSERT_NOT_NULL(occupy);
  TEST_ASSERT_EQUAL_UINT32(10u, occupy->startTick);
  TEST_ASSERT_EQUAL_UINT32(300u, occupy->endTick);
  TEST_ASSERT_NOT_EQUAL(128u, occupy->endTick);
  LoopContentResolution::deviceGateReset();
}

void test_occupy_predicate_false_stale_snapshot_uses_visual_not_snapshot() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  loop.loopId = 1;
  seedRecordNote(loop, 10, 300, 60);
  loop.storePublishedSnapshotForTest(buildOccupySnapshot(loop.loopId, loop.loopLengthTicks, 1, 60, 10, 128),
                                     loop.playbackRevision);
  ++loop.playbackRevision;
  plantVisualGeometry(loop, 1, 60, 10, 200);
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_FALSE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));
  loop.openOverdubSession(0);
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldGatherPublishedSnapshotForDrawSource(
      static_cast<bool>(loop.publishedSnapshot()), loop.captureActive(), loop.hasOverdubSession(),
      !loop.visualCache.notes.empty(), loop.visualCacheDirty, loop.publishedSnapshotIsCurrent()));
  const char* from = loop.rebuildOverdubSourceSpanCacheForTest();
  TEST_ASSERT_EQUAL_STRING("visual", from);
  const NoteUtils::DisplayNote* occupy = findSpanCacheNote(loop, 1);
  TEST_ASSERT_NOT_NULL(occupy);
  TEST_ASSERT_EQUAL_UINT32(10u, occupy->startTick);
  TEST_ASSERT_EQUAL_UINT32(200u, occupy->endTick);
  TEST_ASSERT_NOT_EQUAL(128u, occupy->endTick);
  LoopContentResolution::deviceGateReset();
}

void test_occupy_follows_snapshot_change_while_visual_stale() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedOccupyIdentityTriple(loop);
  loop.storePublishedSnapshotForTest(buildOccupySnapshot(loop.loopId, loop.loopLengthTicks, 1, 60, 10, 128),
                                     loop.playbackRevision);
  loop.openOverdubSession(0);
  TEST_ASSERT_EQUAL_STRING("snapshot", loop.rebuildOverdubSourceSpanCacheForTest());
  TEST_ASSERT_EQUAL_UINT32(128u, findSpanCacheNote(loop, 1)->endTick);

  plantVisualGeometry(loop, 1, 60, 10, 200);
  loop.storePublishedSnapshotForTest(buildOccupySnapshot(loop.loopId, loop.loopLengthTicks, 1, 60, 10, 176),
                                     loop.playbackRevision);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_EQUAL_STRING("snapshot", loop.rebuildOverdubSourceSpanCacheForTest());
  const NoteUtils::DisplayNote* occupy = findSpanCacheNote(loop, 1);
  TEST_ASSERT_NOT_NULL(occupy);
  TEST_ASSERT_EQUAL_UINT32(10u, occupy->startTick);
  TEST_ASSERT_EQUAL_UINT32(176u, occupy->endTick);
  TEST_ASSERT_NOT_EQUAL(200u, occupy->endTick);
  TEST_ASSERT_NOT_EQUAL(300u, occupy->endTick);
  LoopContentResolution::deviceGateReset();
}

void test_occupy_144743_48_616_materialize_vs_window_vs_pending() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();

  constexpr uint32_t kOccupyLoopLen = 3072;
  constexpr uint8_t kPitch = 48;
  constexpr NoteId kSource205 = 5;
  constexpr NoteId kSource885 = 9;
  constexpr NoteId kOccupy616 = 22;
  constexpr NoteId kOccupy763 = 23;
  constexpr NoteId kOccupy1061 = 25;
  constexpr PassId kOd1PassId = 2;
  constexpr PassId kOd2PassId = 3;

  Loop loop;
  loop.loopId = 1;
  loop.loopLengthTicks = kOccupyLoopLen;

  LoopEventStore recordStore;
  TEST_ASSERT_TRUE(storeAppendNoteOn(recordStore, 83, 1, 72, 100, 1));
  TEST_ASSERT_TRUE(recordStore.append(MidiEvent::NoteOff(470, 1, 72, 0)));
  loop.seedRecordPassFromStore(recordStore);
  loop.loopLengthTicks = kOccupyLoopLen;

  LoopEventStore od1Store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(od1Store, 205, 1, kPitch, 100, kSource205));
  TEST_ASSERT_TRUE(od1Store.append(MidiEvent::NoteOff(584, 1, kPitch, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(od1Store, 885, 1, kPitch, 100, kSource885));
  TEST_ASSERT_TRUE(od1Store.append(MidiEvent::NoteOff(1285, 1, kPitch, 0)));
  seedCommittedOverdubPass(loop, od1Store, kOd1PassId, 1);

  LoopEventStore od2Store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(od2Store, 616, 1, kPitch, 100, kOccupy616));
  TEST_ASSERT_TRUE(od2Store.append(MidiEvent::NoteOff(1016, 1, kPitch, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(od2Store, 763, 1, kPitch, 100, kOccupy763));
  TEST_ASSERT_TRUE(od2Store.append(MidiEvent::NoteOff(867, 1, kPitch, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(od2Store, 1061, 1, kPitch, 100, kOccupy1061));
  TEST_ASSERT_TRUE(storeAppendNoteOn(od2Store, 1399, 1, kPitch, 100, 28));
  TEST_ASSERT_TRUE(od2Store.append(MidiEvent::NoteOff(1445, 1, kPitch, 0)));
  TEST_ASSERT_TRUE(od2Store.append(MidiEvent::NoteOff(2908, 1, kPitch, 0)));
  seedCommittedOverdubPass(loop, od2Store, kOd2PassId, 2);

  SessionMidiEventVec rawEvents;
  loop.mergeActiveCapturePasses(rawEvents);
  TEST_ASSERT_EQUAL_UINT32(1u, countPitchOnsAtTick(rawEvents, kPitch, 885));
  TEST_ASSERT_EQUAL_UINT32(1u, countPitchOnsAtTick(rawEvents, kPitch, 763));
  const NoteUtils::DisplayNoteVec rawNotes =
      NoteUtils::reconstructDisplayNotes(rawEvents, kOccupyLoopLen, false, false);
  TEST_ASSERT_EQUAL_UINT32(2292u, displayNoteExclusiveLength(rawNotes, kPitch, 616, kOccupyLoopLen));

  const EditPassId hide763 = saveHideCompanion(loop, kOccupy763, 763, 867);
  const EditPassId hide885 = saveHideCompanion(loop, kSource885, 885, 1285);
  TEST_ASSERT_TRUE(editPassesHideTarget(loop, kOccupy763));
  TEST_ASSERT_TRUE(editPassesHideTarget(loop, kSource885));

  SessionMidiEventVec materialized;
  loop.passes.materializeToEventVector(materialized, kOccupyLoopLen);
  TEST_ASSERT_EQUAL_UINT32(0u, countPitchOnsAtTick(materialized, kPitch, 885));
  TEST_ASSERT_EQUAL_UINT32(0u, countPitchOnsAtTick(materialized, kPitch, 763));
  TEST_ASSERT_EQUAL_UINT32(1u, countPitchOffsAtTick(materialized, kPitch, 1016));
  const NoteUtils::DisplayNoteVec materializeNotes =
      NoteUtils::reconstructDisplayNotes(materialized, kOccupyLoopLen, false, false);
  TEST_ASSERT_EQUAL_UINT32(
      400u, displayNoteExclusiveLength(materializeNotes, kPitch, 616, kOccupyLoopLen));

  SessionMidiEventVec effectiveEvents;
  loop.copyEffectiveCommittedEvents(effectiveEvents);
  const NoteUtils::DisplayNoteVec effectiveNotes =
      NoteUtils::reconstructDisplayNotes(effectiveEvents, kOccupyLoopLen, false, false);
  TEST_ASSERT_EQUAL_UINT32(
      400u, displayNoteExclusiveLength(effectiveNotes, kPitch, 616, kOccupyLoopLen));

  SessionMidiEventVec windowEvents;
  loop.copyEffectiveCommittedEventsInRange(windowEvents, 0, kOccupyLoopLen, true);
  TEST_ASSERT_EQUAL_UINT32(0u, countPitchOnsAtTick(windowEvents, kPitch, 885));
  TEST_ASSERT_EQUAL_UINT32(1u, countPitchOffsAtTick(windowEvents, kPitch, 1016));
  const NoteUtils::DisplayNoteVec windowNotes =
      NoteUtils::reconstructDisplayNotes(windowEvents, kOccupyLoopLen, false, false);
  const uint32_t windowLength =
      displayNoteExclusiveLength(windowNotes, kPitch, 616, kOccupyLoopLen);
  TEST_ASSERT_EQUAL_UINT32(400u, windowLength);

  loop.openOverdubSession(0);
  EditPassIdList companions;
  companions.push_back(hide763);
  companions.push_back(hide885);
  loop.recordLastOverdubSeal(kOd2PassId, companions, loop.playbackRevision);
  loop.closeOverdubSession();

  TEST_ASSERT_FALSE(loop.hasPendingNoteChanges());
  TEST_ASSERT_TRUE(loop.hasSealedCompanionOverlay());

  NoteUtils::DisplayNoteVec afterPending = materializeNotes;
  loop.applyPendingNoteChangesToDisplayNotes(afterPending, afterPending.size());
  const uint32_t afterPendingLength =
      displayNoteExclusiveLength(afterPending, kPitch, 616, kOccupyLoopLen);
  TEST_ASSERT_FALSE(pendingWritesExclusiveEnd(afterPending, kPitch, 616, 884));

  NoteUtils::DisplayNoteVec rawAfterPending = rawNotes;
  loop.applyPendingNoteChangesToDisplayNotes(rawAfterPending, rawAfterPending.size());
  TEST_ASSERT_FALSE(pendingWritesExclusiveEnd(rawAfterPending, kPitch, 616, 884));

  // Fixture table (144743 48@616): per-layer materialize, copyEffectiveCommittedEvents,
  // and window gather are 400 (Off@1016 kept, On@885 absent). After OD2 close,
  // pending is empty and overlay apply does not write end 884.
  TEST_ASSERT_EQUAL_UINT32(400u, windowLength);
  TEST_ASSERT_EQUAL_UINT32(400u, afterPendingLength);
  TEST_ASSERT_EQUAL_UINT32(
      2292u, displayNoteExclusiveLength(rawAfterPending, kPitch, 616, kOccupyLoopLen));

  LoopContentResolution::deviceGateReset();
}

/// RC1d pin ([`153848`] 52@2172 f=58 e=59). Host Loop occupy/commit/seal plus
/// draw-source clone (no DisplayManager hook). Effective Off@2231 / dump 59.
/// Prefix, sealed union, and companion overlay all stay inclusive 2231.
/// Not RC1d-a (stale draw-source prefix) and not RC1d-b (Length on the Add).
/// First 2230 is not host-reachable without a new firmware hook — missing seam.
void test_occupy_153848_52_2172_first_inclusive_2230_pin() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();

  constexpr uint32_t kOccupyLoopLen = 3072;
  constexpr uint8_t kPitch = 52;
  constexpr NoteId kSource1847 = 15;
  constexpr NoteId kOccupy2172 = 40;
  constexpr uint32_t kLastBar = (kOccupyLoopLen / Config::TICKS_PER_BAR) - 1u;

  Loop loop;
  loop.loopId = 1;
  loop.loopLengthTicks = kOccupyLoopLen;

  LoopEventStore recordStore;
  TEST_ASSERT_TRUE(storeAppendNoteOn(recordStore, 80, 1, 72, 100, 1));
  TEST_ASSERT_TRUE(recordStore.append(MidiEvent::NoteOff(460, 1, 72, 0)));
  loop.seedRecordPassFromStore(recordStore);
  loop.loopLengthTicks = kOccupyLoopLen;
  loop.rebuildVisualCacheFromPasses();

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(1847, 1, kPitch, 100, kSource1847)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(2230, 1, kPitch, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubStop, 2230));
  (void)loop.sealPendingNoteChangesToEditPasses();
  loop.closeOverdubSession();
  loop.rebuildVisualCacheFromPasses();
  TEST_ASSERT_TRUE(hasDisplayNote(loop.visualCache.notes, kPitch, 1847));

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  loop.rebuildOverdubSourceView(0);
  TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), kPitch, 1847));

  OverlapNoteIdSet ledger = overlapIds({kSource1847});
  OverlapNoteIdSet resolved;
  TEST_ASSERT_TRUE(
      loop.appendCaptureEvent(noteOnWithNoteId(2172, 1, kPitch, 100, kOccupy2172)));
  loop.resolveOccupancyAtNoteOn(1, kPitch, 100, ledger, 2172, &resolved);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(2231, 1, kPitch, 0)));
  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(
      1, kPitch, 100, 2172, 2231, kOccupy2172, ledger, &resolved));
  loop.applyPendingNoteChangesToOverdubSourceView();

  TEST_ASSERT_FALSE(pendingShortenTargetsEnd(loop, kOccupy2172, 2230));

  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubStop, 2231));
  const PassId od2PassId = loop.lastCommittedPassId();
  const EditPassIdList companions = loop.sealPendingNoteChangesToEditPasses();
  loop.recordLastOverdubSeal(od2PassId, companions, loop.playbackRevision);
  loop.closeOverdubSession();
  loop.rebuildVisualCacheFromPasses();

  TEST_ASSERT_FALSE(loop.hasPendingNoteChanges());
  TEST_ASSERT_FALSE(lengthCompanionTargetsEnd(loop, kOccupy2172, 2230));
  TEST_ASSERT_FALSE(pendingShortenTargetsEnd(loop, kOccupy2172, 2230));

  SessionMidiEventVec effectiveEvents;
  loop.copyEffectiveCommittedEvents(effectiveEvents);
  TEST_ASSERT_EQUAL_UINT32(1u, countPitchOnsAtTick(effectiveEvents, kPitch, 2172));
  TEST_ASSERT_EQUAL_UINT32(1u, countPitchOffsAtTick(effectiveEvents, kPitch, 2231));
  const NoteUtils::DisplayNoteVec effectiveNotes =
      NoteUtils::reconstructDisplayNotes(effectiveEvents, kOccupyLoopLen, false, false);
  TEST_ASSERT_EQUAL_UINT32(2231u, noteEndAtPitchStart(effectiveNotes, kPitch, 2172));
  TEST_ASSERT_EQUAL_UINT32(
      59u, displayNoteExclusiveLength(effectiveNotes, kPitch, 2172, kOccupyLoopLen));

  NoteUtils::DisplayNoteVec beforeOverlay;
  gatherDrawSourcePrefix(loop, 0, kLastBar, kPitch, kPitch, beforeOverlay);
  const uint32_t beforeOverlayEnd = noteEndAtPitchStart(beforeOverlay, kPitch, 2172);
  const size_t prefixEnd = beforeOverlay.size();
  applyDrawSourceSealedAndPreview(loop, 0, kLastBar, beforeOverlay, prefixEnd);
  const uint32_t afterSealedEnd = noteEndAtPitchStart(beforeOverlay, kPitch, 2172);

  NoteUtils::DisplayNoteVec afterOverlay = beforeOverlay;
  loop.applyPendingNoteChangesToDisplayNotes(afterOverlay, prefixEnd);
  const uint32_t afterOverlayEnd = noteEndAtPitchStart(afterOverlay, kPitch, 2172);

  char pin[192];
  snprintf(pin, sizeof(pin),
           "RC1d pin 52@2172 ends: effective=%lu prefix=%lu sealed=%lu overlay=%lu",
           static_cast<unsigned long>(noteEndAtPitchStart(effectiveNotes, kPitch, 2172)),
           static_cast<unsigned long>(beforeOverlayEnd),
           static_cast<unsigned long>(afterSealedEnd),
           static_cast<unsigned long>(afterOverlayEnd));

  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2231u, beforeOverlayEnd, pin);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2231u, afterSealedEnd, pin);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2231u, afterOverlayEnd, pin);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      59u, displayNoteExclusiveLength(afterOverlay, kPitch, 2172, kOccupyLoopLen), pin);

  LoopContentResolution::deviceGateReset();
}

/// session_20260905_162652: startLoopTick=247, loopLength=3072.
/// Occupy On must resolve at storage phase (`capturePhaseTick`), not origin 0.
/// Source 56@1339–1745 covers storage hold 1360; Shorten lands at 1359.
/// Origin-0 hold 1607 covers the same row but Shortens to 1606 (wrong frame).
/// Track call-site correction is HITL-gated; this pins the Loop geometry contract.
void test_occupy_nonzero_start_loop_shortens_at_storage_hold() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();

  constexpr uint32_t kOccupyLoopLen = 3072;
  constexpr uint32_t kStartLoopTick = 247;
  constexpr uint8_t kPitch = 56;
  constexpr NoteId kSourceId = 10;
  constexpr uint32_t kSourceStart = 1339;
  constexpr uint32_t kSourceEnd = 1745;
  constexpr uint32_t kStorageHold = 1360;
  constexpr uint32_t kOriginZeroHold = 1607;

  TEST_ASSERT_EQUAL_UINT32(kStartLoopTick, kOriginZeroHold - kStorageHold);
  TEST_ASSERT_TRUE(OverlapNoteIdObservation::displayNotePresentAtHold(
      kSourceStart, kSourceEnd, kStorageHold, kOccupyLoopLen));
  TEST_ASSERT_TRUE(OverlapNoteIdObservation::displayNotePresentAtHold(
      kSourceStart, kSourceEnd, kOriginZeroHold, kOccupyLoopLen));
  TEST_ASSERT_TRUE(
      OverlapNoteIdObservation::displayNotePresentAtHold(83, 469, 296, kOccupyLoopLen));
  TEST_ASSERT_FALSE(
      OverlapNoteIdObservation::displayNotePresentAtHold(83, 469, 543, kOccupyLoopLen));

  auto seedOccupiedSource = [&](Loop& loop) {
    loop.loopId = 1;
    loop.loopLengthTicks = kOccupyLoopLen;
    loop.startLoopTick = kStartLoopTick;
    LoopEventStore recordStore;
    TEST_ASSERT_TRUE(storeAppendNoteOn(recordStore, kSourceStart, 1, kPitch, 100, kSourceId));
    TEST_ASSERT_TRUE(recordStore.append(MidiEvent::NoteOff(kSourceEnd, 1, kPitch, 0)));
    loop.seedRecordPassFromStore(recordStore);
    loop.loopLengthTicks = kOccupyLoopLen;
    loop.startLoopTick = kStartLoopTick;
    loop.rebuildVisualCacheFromPasses();
    loop.openOverdubSession(0);
    loop.beginCapture(CapturePhase::Overdub, 0);
    loop.rebuildOverdubSourceView(0);
    TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), kPitch, kSourceStart));
  };

  {
    Loop loop;
    seedOccupiedSource(loop);
    OverlapNoteIdSet ledger = overlapIds({kSourceId});
    OverlapNoteIdSet resolved;
    loop.resolveOccupancyAtNoteOn(1, kPitch, 100, ledger, kStorageHold, &resolved);
    TEST_ASSERT_TRUE(pendingShortenTargetsEnd(loop, kSourceId, kStorageHold - 1u));
    TEST_ASSERT_FALSE(pendingShortenTargetsEnd(loop, kSourceId, kOriginZeroHold - 1u));
  }

  {
    Loop loop;
    seedOccupiedSource(loop);
    OverlapNoteIdSet ledger = overlapIds({kSourceId});
    OverlapNoteIdSet resolved;
    loop.resolveOccupancyAtNoteOn(1, kPitch, 100, ledger, kOriginZeroHold, &resolved);
    TEST_ASSERT_TRUE(pendingShortenTargetsEnd(loop, kSourceId, kOriginZeroHold - 1u));
    TEST_ASSERT_FALSE(pendingShortenTargetsEnd(loop, kSourceId, kStorageHold - 1u));
  }

  LoopContentResolution::deviceGateReset();
}

void test_source_view_keeps_same_start_a_identities_183043() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = kLoopLen;
  LoopEventStore record;
  TEST_ASSERT_TRUE(storeAppendNoteOn(record, 48, 1, 69, 100, 32));
  TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(88, 1, 69, 0)));
  loop.seedRecordPassFromStore(record);
  LoopEventStore overdub;
  TEST_ASSERT_TRUE(storeAppendNoteOn(overdub, 48, 1, 69, 100, 86));
  TEST_ASSERT_TRUE(overdub.append(MidiEvent::NoteOff(88, 1, 69, 0)));
  seedCommittedOverdubPass(loop, overdub, 2, 1);
  loop.nextNoteId_ = 87;
  const SnapshotNoteSpec publishedNotes[] = {
      {32, 69, 48, 88},
      {86, 69, 48, 88},
  };
  loop.storePublishedSnapshotForTest(
      buildSnapshotWithNotes(loop.loopId, loop.loopLengthTicks, publishedNotes, 2),
      loop.playbackRevision);
  loop.openOverdubSession(48);
  loop.establishOverdubSourceView(48);
  TEST_ASSERT_TRUE(sourceViewHasNoteId(loop, 32));
  TEST_ASSERT_TRUE(sourceViewHasNoteId(loop, 86));
  const NoteUtils::DisplayNote* first = findDisplayNoteById(loop.overdubSourceViewNotes(), 32);
  const NoteUtils::DisplayNote* second = findDisplayNoteById(loop.overdubSourceViewNotes(), 86);
  TEST_ASSERT_NOT_NULL(first);
  TEST_ASSERT_NOT_NULL(second);
  TEST_ASSERT_EQUAL_UINT32(48u, first->startTick);
  TEST_ASSERT_EQUAL_UINT32(88u, first->endTick);
  TEST_ASSERT_EQUAL_UINT32(48u, second->startTick);
  TEST_ASSERT_EQUAL_UINT32(88u, second->endTick);
}

void test_source_view_keeps_dsharp_committed_offs_before_pending_183043() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = 3072;
  LoopEventStore record;
  TEST_ASSERT_TRUE(storeAppendNoteOn(record, 768, 1, 63, 100, 107));
  TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(1440, 1, 63, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(record, 1536, 1, 63, 100, 109));
  TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(2208, 1, 63, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(record, 2304, 1, 63, 100, 111));
  TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(2976, 1, 63, 0)));
  loop.seedRecordPassFromStore(record);
  LoopEventStore overdub;
  TEST_ASSERT_TRUE(storeAppendNoteOn(overdub, 1152, 1, 63, 100, 200));
  TEST_ASSERT_TRUE(overdub.append(MidiEvent::NoteOff(1872, 1, 63, 0)));
  seedCommittedOverdubPass(loop, overdub, 2, 1);
  loop.loopLengthTicks = 3072;
  loop.nextNoteId_ = 201;
  loop.openOverdubSession(48);
  loop.establishOverdubSourceView(48);
  const NoteUtils::DisplayNote* first = findDisplayNoteById(loop.overdubSourceViewNotes(), 107);
  const NoteUtils::DisplayNote* second = findDisplayNoteById(loop.overdubSourceViewNotes(), 109);
  const NoteUtils::DisplayNote* third = findDisplayNoteById(loop.overdubSourceViewNotes(), 111);
  TEST_ASSERT_NOT_NULL(first);
  TEST_ASSERT_NOT_NULL(second);
  TEST_ASSERT_NOT_NULL(third);
  TEST_ASSERT_EQUAL_UINT32(1440u, first->endTick);
  TEST_ASSERT_EQUAL_UINT32(2208u, second->endTick);
  TEST_ASSERT_EQUAL_UINT32(2976u, third->endTick);
}

void test_visual_cache_keeps_dsharp_committed_offs_after_rebuild_211440() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLoopLength = 4608;
  loop.loopLengthTicks = kLoopLength;
  LoopEventStore record;
  TEST_ASSERT_TRUE(storeAppendNoteOn(record, 0, 1, 63, 100, 50));
  TEST_ASSERT_TRUE(storeAppendNoteOn(record, 768, 1, 63, 100, 107));
  TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(1440, 1, 63, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(record, 1536, 1, 63, 100, 109));
  TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(2208, 1, 63, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(record, 2304, 1, 63, 100, 111));
  TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(2976, 1, 63, 0)));
  loop.seedRecordPassFromStore(record);
  LoopEventStore overdub;
  TEST_ASSERT_TRUE(storeAppendNoteOn(overdub, 1152, 1, 63, 100, 200));
  TEST_ASSERT_TRUE(overdub.append(MidiEvent::NoteOff(1872, 1, 63, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(overdub, 1920, 1, 63, 100, 201));
  TEST_ASSERT_TRUE(overdub.append(MidiEvent::NoteOff(2639, 1, 63, 0)));
  seedCommittedOverdubPass(loop, overdub, 2, 1);
  loop.loopLengthTicks = kLoopLength;

  loop.rebuildVisualCacheFromPasses();
  const NoteUtils::DisplayNote* first = findDisplayNoteById(loop.visualCache.notes, 107);
  const NoteUtils::DisplayNote* second = findDisplayNoteById(loop.visualCache.notes, 109);
  const NoteUtils::DisplayNote* third = findDisplayNoteById(loop.visualCache.notes, 111);
  TEST_ASSERT_NOT_NULL(first);
  TEST_ASSERT_NOT_NULL(second);
  TEST_ASSERT_NOT_NULL(third);
  TEST_ASSERT_EQUAL_UINT32(768u, first->startTick);
  TEST_ASSERT_EQUAL_UINT32(1440u, first->endTick);
  TEST_ASSERT_EQUAL_UINT32(1536u, second->startTick);
  TEST_ASSERT_EQUAL_UINT32(2208u, second->endTick);
  TEST_ASSERT_EQUAL_UINT32(2304u, third->startTick);
  TEST_ASSERT_EQUAL_UINT32(2976u, third->endTick);
  const NoteUtils::DisplayNote* orphan = findDisplayNoteById(loop.visualCache.notes, 50);
  if (orphan != nullptr) {
    TEST_ASSERT_TRUE(orphan->endTick == orphan->startTick || orphan->endTick != 1440u);
    TEST_ASSERT_TRUE((orphan->endTick - orphan->startTick) != kLoopLength);
  }
  for (const NoteUtils::DisplayNote& note : loop.visualCache.notes) {
    TEST_ASSERT_FALSE(note.note == 63 && note.startTick == 0 &&
                      note.endTick >= kLoopLength);
  }

  loop.markDisplayCachesStale();
  loop.rebuildVisualCacheIdleSlice(4, 0);
  loop.rebuildVisualCacheIdleSlice(4, 0);
  const NoteUtils::DisplayNote* idleFirst = findDisplayNoteById(loop.visualCache.notes, 107);
  const NoteUtils::DisplayNote* idleSecond = findDisplayNoteById(loop.visualCache.notes, 109);
  const NoteUtils::DisplayNote* idleThird = findDisplayNoteById(loop.visualCache.notes, 111);
  TEST_ASSERT_NOT_NULL(idleFirst);
  TEST_ASSERT_NOT_NULL(idleSecond);
  TEST_ASSERT_NOT_NULL(idleThird);
  TEST_ASSERT_EQUAL_UINT32(1440u, idleFirst->endTick);
  TEST_ASSERT_EQUAL_UINT32(2208u, idleSecond->endTick);
  TEST_ASSERT_EQUAL_UINT32(2976u, idleThird->endTick);
}

void test_visual_cache_keeps_same_start_a_identities_183043() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = kLoopLen;
  LoopEventStore record;
  TEST_ASSERT_TRUE(storeAppendNoteOn(record, 1488, 1, 69, 100, 32));
  TEST_ASSERT_TRUE(record.append(MidiEvent::NoteOff(1528, 1, 69, 0)));
  loop.seedRecordPassFromStore(record);
  LoopEventStore overdub;
  TEST_ASSERT_TRUE(storeAppendNoteOn(overdub, 1488, 1, 69, 100, 86));
  TEST_ASSERT_TRUE(overdub.append(MidiEvent::NoteOff(1528, 1, 69, 0)));
  seedCommittedOverdubPass(loop, overdub, 2, 1);
  loop.rebuildVisualCacheFromPasses();
  NoteUtils::DisplayNoteVec paired;
  LoopContentResolution::resolveDisplayNotes(loop.passes, loop.loopLengthTicks, paired);
  loop.mergeDisplayNotesIntoVisualCache(paired);
  DisplayWindowUtils::keepShortestEndAtPitchStart(loop.visualCache.notes);
  const NoteUtils::DisplayNote* first = findDisplayNoteById(loop.visualCache.notes, 32);
  const NoteUtils::DisplayNote* second = findDisplayNoteById(loop.visualCache.notes, 86);
  TEST_ASSERT_NOT_NULL(first);
  TEST_ASSERT_NOT_NULL(second);
  TEST_ASSERT_EQUAL_UINT32(1488u, first->startTick);
  TEST_ASSERT_EQUAL_UINT32(1528u, first->endTick);
  TEST_ASSERT_EQUAL_UINT32(1488u, second->startTick);
  TEST_ASSERT_EQUAL_UINT32(1528u, second->endTick);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_pitch_edit_rebuild_shows_only_new_pitch_at_tick);
  RUN_TEST(test_disable_edit_pass_rebuild_restores_pre_edit_display);
  RUN_TEST(test_session_open_without_prewarm_falls_back_when_visual_cache_empty);
  RUN_TEST(test_session_open_without_prewarm_uses_clean_visual_cache_seed);
  RUN_TEST(test_session_open_without_prewarm_falls_back_from_dirty_visual_cache);
  RUN_TEST(test_source_view_rejects_partial_prior_span_cache);
  RUN_TEST(test_overdub_restamp_miss_reopens_from_visual_cache);
  RUN_TEST(test_prewarm_span_cache_then_session_open_has_source_notes);
  RUN_TEST(test_note_edit_shorten_clips_overdub_source_view_134549);
  RUN_TEST(test_overdub_start_establishes_source_view);
  RUN_TEST(test_overdub_enter_rebuilds_source_view_not_visual_cache);
  RUN_TEST(test_overdub_begin_makes_restore_flatten_unreachable);
  RUN_TEST(test_idle_empty_dirty_bars_keeps_notes_without_marking_all);
  RUN_TEST(test_overdub_wrap_keeps_clean_cache_notes_and_marks_wrap_span);
  RUN_TEST(test_wrap_held_span_does_not_dirty_unchanged_interior_bars);
  RUN_TEST(test_wrap_held_on_last_bar_dirties_endpoints_only);
  RUN_TEST(test_wrap_append_restamps_span_cache_after_wrap_pair_restamp_fail);
  RUN_TEST(test_second_pass_hold_at_phase_zero_finds_prior_pass_participant);
  RUN_TEST(test_wrap_append_does_not_lengthen_same_pitch_body_in_visual_cache);
  RUN_TEST(test_short_loop_stale_keeps_notes_for_overdub_stop_handoff);
  RUN_TEST(test_short_loop_idle_slice_cleans_without_full_rebuild);
  RUN_TEST(test_note_edit_idle_paint_consumes_stale_visual_cache);
  RUN_TEST(test_record_start_does_not_keep_source_view);
  RUN_TEST(test_source_view_includes_edit_pass_geometry);
  RUN_TEST(test_source_view_stable_across_capture_appends_and_wraps);
  RUN_TEST(test_source_view_immutable_when_live_materialize_mutates);
  RUN_TEST(test_source_view_wrap_safe_high_then_low_capture_order);
  RUN_TEST(test_source_view_prepared_window_includes_unpaired_open_tails);
  RUN_TEST(test_source_view_consumes_prepared_lcr_when_cache_dirty);
  RUN_TEST(test_source_view_skips_stale_prepared_lcr_on_stamp_mismatch);
  RUN_TEST(test_source_view_falls_back_when_prepared_span_copy_is_empty);
  RUN_TEST(test_source_view_span_copy_keeps_only_window_note_ids);
  RUN_TEST(test_source_view_span_copy_keeps_window_note_on_ids_past_occupy_set_capacity);
  RUN_TEST(test_source_view_rebuild_uses_prepared_spans_past_occupy_set_capacity);
  RUN_TEST(test_prepared_session_length_mismatch_misses_resolve_copy_collect);
  RUN_TEST(test_publish_prepared_overdub_pass_ignores_loop_length_mismatch);
  RUN_TEST(test_prepared_session_remeasure_after_reset_copies_long_loop);
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
  RUN_TEST(test_wrap_rebuild_retains_record_pass_note_id_after_publish);
  RUN_TEST(test_wrap_rebuild_with_hide_companion_drops_record_from_source_view);
  RUN_TEST(test_multi_wrap_source_view_membership_three_classes);
  RUN_TEST(test_overdub_session_undo_rebuilds_source_view_to_match_prepared);
  RUN_TEST(test_overdub_session_undo_disables_sealed_wrap);
  RUN_TEST(test_overdub_session_undo_depth_counts_sealed_wraps_only);
  RUN_TEST(test_session_undo_skips_next_wrap_crossing);
  RUN_TEST(test_stop_collects_session_wraps_then_close_clears_stack);
  RUN_TEST(test_overdub_session_index_stamps_wraps_and_groups_after_reload);
  RUN_TEST(test_should_commit_overdub_wrap_after_leaving_start);
  RUN_TEST(test_should_commit_overdub_wrap_at_phase_0_not_session_start);
  RUN_TEST(test_should_commit_overdub_wrap_at_zero_session_start);
  RUN_TEST(test_first_wrap_seal_at_phase_0_keeps_tail_linear);
  RUN_TEST(test_retire_superseded_pitch_drops_home_when_settled_present);
  RUN_TEST(test_retire_superseded_pitch_keeps_same_start_sibling_end);
  RUN_TEST(test_long_loop_pass_state_change_keeps_notes_and_stays_dirty);
  RUN_TEST(test_budgeted_idle_slice_defers_wrap_append_then_resumes);
  RUN_TEST(test_deferred_wrap_append_skips_when_current_source_already_built);
  RUN_TEST(test_deferred_wrap_append_rebuilds_when_source_revision_moved);
  RUN_TEST(test_undo_overdub_idle_refresh_restores_record_layer);
  RUN_TEST(test_prepared_hold_ids_pin_b_extras_after_same_pitch_wraps);
  RUN_TEST(test_prepared_hold_ids_pin_b_extras_at_tick64_after_wrap_undo);
  RUN_TEST(test_prepared_hold_ids_pin_undo_to_record_then_rewrap_a_equals_b);
  RUN_TEST(test_prepared_hold_ids_pin_b_extra_when_occupy_misses_sibling_at_64);
  RUN_TEST(test_prepared_hold_ids_pin_b_extra_wrap_crossing_covers_64);
  RUN_TEST(test_prepared_hold_ids_pin_finished_open_covering_64);
  RUN_TEST(test_prepared_hold_ids_pin_complete_wrap_pair_covering_64);
  RUN_TEST(test_prepared_hold_ids_pin_wrap_pair_hide_drops_tail_at_64);
  RUN_TEST(test_source_view_includes_seven_finished_opens_covering_64);
  RUN_TEST(test_prepared_linear_overdub_matches_lcr_plus_append);
  RUN_TEST(test_prepared_wrap_held_overdub_lcr_append_delta);
  RUN_TEST(test_idle_slice_prepared_linear_matches_lcr_only);
  RUN_TEST(test_idle_slice_prepared_interior_keeps_mid_loop_overdub);
  RUN_TEST(test_idle_slice_prepared_matches_lcr_without_append_wrap_held);
  RUN_TEST(test_idle_slice_unprepared_interior_keeps_mid_loop_overdub);
  RUN_TEST(test_idle_slice_unprepared_keeps_wrap_held);
  RUN_TEST(test_idle_slice_current_snapshot_is_accepted);
  RUN_TEST(test_idle_slice_stale_overlay_rejects_and_uses_committed_fallback);
  RUN_TEST(test_paint_last_resort_stale_overlay_uses_passes_resolve);
  RUN_TEST(test_idle_slice_without_snapshot_uses_passes_resolve);
  RUN_TEST(test_idle_slice_incomplete_fallback_budget_keeps_dirty);
  RUN_TEST(test_idle_slice_keeps_untouched_next_bar_note);
  RUN_TEST(test_draw_source_snapshot_miss_recovers_pending_shorten_212700);
  RUN_TEST(test_incomplete_visual_cache_does_not_hide_committed_occupy_identity_174151);
  RUN_TEST(test_snapshot_span_cache_trusts_published_membership_without_merge_omitted);
  RUN_TEST(test_visual_cache_trusts_published_snapshot_membership_without_merge_omitted);
  RUN_TEST(test_snapshot_span_cache_keeps_sealed_hide_without_merge_omitted_202713);
  RUN_TEST(test_occupy_predicate_true_uses_snapshot_not_visual_or_prepared);
  RUN_TEST(test_snapshot_source_uses_bounded_overdub_window);
  RUN_TEST(test_ready_fallback_cache_does_not_override_current_snapshot);
  RUN_TEST(test_snapshot_stamped_span_cache_is_reused);
  RUN_TEST(test_span_cache_identity_stale_when_published_root_replaced_same_revision);
  RUN_TEST(test_occupy_predicate_false_uses_prepared_not_snapshot);
  RUN_TEST(test_occupy_predicate_false_stale_snapshot_uses_visual_not_snapshot);
  RUN_TEST(test_occupy_follows_snapshot_change_while_visual_stale);
  RUN_TEST(test_occupy_144743_48_616_materialize_vs_window_vs_pending);
  RUN_TEST(test_occupy_153848_52_2172_first_inclusive_2230_pin);
  RUN_TEST(test_occupy_nonzero_start_loop_shortens_at_storage_hold);
  RUN_TEST(test_source_view_keeps_same_start_a_identities_183043);
  RUN_TEST(test_source_view_keeps_dsharp_committed_offs_before_pending_183043);
  RUN_TEST(test_visual_cache_keeps_dsharp_committed_offs_after_rebuild_211440);
  RUN_TEST(test_visual_cache_keeps_same_start_a_identities_183043);
  return UNITY_END();
}
