//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"

#include "CommittedEventRange.h"
#include "EditApply.h"
#include "Globals.h"
#include "LoopInternal.h"
#include "Utils/Diagnostics.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/LoopMem.h"

#include <algorithm>
#include <vector>

namespace {

struct CommittedPitchQueryWork {
  uint32_t sourceEventsScanned = 0;
  uint32_t candidateEvents = 0;
  uint32_t editRowsApplied = 0;
  uint32_t fullMaterializeCount = 0;
};

}  // namespace

static CommittedPitchQueryWork g_committedPitchQueryWork;

namespace {

template <typename MidiEventVector>
void mergeSortedLoopCaptureLayers(MidiEventVector& base, MidiEventVector&& addition) {
  if (addition.empty()) {
    return;
  }
  if (base.empty()) {
    base = std::move(addition);
    return;
  }
  MidiEventVector merged;
  merged.reserve(base.size() + addition.size());
  std::merge(base.begin(), base.end(), addition.begin(), addition.end(),
             std::back_inserter(merged),
             [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
  base = std::move(merged);
}

template <typename MidiEventVector>
void mergeActiveCapturePassesInto(const LoopPasses& passes, MidiEventVector& out) {
  out.clear();
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Active &&
      !passes.recordPass.committedChunkIds.empty()) {
    LoopEventStore::appendChunkRefEvents(passes.recordPass.committedChunkIds, out);
  }
  std::vector<const OverdubPass*> activeOverdubs;
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.state == CapturePassState::Active && !pass.committedChunkIds.empty()) {
      activeOverdubs.push_back(&pass);
    }
  }
  std::sort(activeOverdubs.begin(), activeOverdubs.end(),
            [](const OverdubPass* a, const OverdubPass* b) {
              return a->mergeSequence < b->mergeSequence;
            });
  for (const OverdubPass* pass : activeOverdubs) {
    MidiEventVector layer;
    LoopEventStore::appendChunkRefEvents(pass->committedChunkIds, layer);
    mergeSortedLoopCaptureLayers(out, std::move(layer));
  }
}

template <typename MidiEventVector>
void mergeCaptureStoreIntoMaterializedEvents(const Loop& loop, MidiEventVector& out) {
  if (!loop.captureActive() || loop.capture.store.empty()) {
    return;
  }
  const_cast<Loop&>(loop).ensureCaptureEventsSorted();

  MidiEventVector captureFlat;
  loop.capture.store.copyEventsTo(captureFlat);
  if (out.empty()) {
    out = std::move(captureFlat);
    return;
  }
  if (captureFlat.empty()) {
    return;
  }

  MidiEventVector merged;
  merged.reserve(out.size() + captureFlat.size());
  std::merge(out.begin(), out.end(), captureFlat.begin(), captureFlat.end(),
             std::back_inserter(merged),
             [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
  out = std::move(merged);
}

bool hasActiveEditPasses(const LoopPasses& passes) {
  for (const EditPass& editPass : passes.editPasses) {
    if (editPass.state == EditPassState::Active) {
      return true;
    }
  }
  return false;
}

LOOP_COLD_MEM bool containsNoteId(const std::vector<NoteId>& ids, NoteId noteId) {
  if (noteId == kInvalidNoteId) {
    return false;
  }
  for (NoteId id : ids) {
    if (id == noteId) {
      return true;
    }
  }
  return false;
}

LOOP_COLD_MEM void collectNoteIdsRetargetedToPitch(const EditPassVec& editPasses, uint8_t pitch,
                                     std::vector<NoteId>& out) {
  std::vector<NoteId> ids;
  std::vector<uint8_t> dest;
  for (const EditPass& row : editPasses) {
    if (row.state != EditPassState::Active || row.passType != EditPassType::Note) {
      continue;
    }
    if (row.actionType != EditActionType::Update || row.propertyType != EditPropertyType::Pitch) {
      continue;
    }
    if (row.targetNoteId == kInvalidNoteId) {
      continue;
    }
    bool found = false;
    for (size_t i = 0; i < ids.size(); ++i) {
      if (ids[i] == row.targetNoteId) {
        dest[i] = row.pitch;
        found = true;
        break;
      }
    }
    if (!found) {
      ids.push_back(row.targetNoteId);
      dest.push_back(row.pitch);
    }
  }
  for (size_t i = 0; i < ids.size(); ++i) {
    if (dest[i] == pitch) {
      out.push_back(ids[i]);
    }
  }
}

LOOP_COLD_MEM bool createRowContributesToPitch(const EditPass& row, uint8_t pitch,
                                 const std::vector<NoteId>& retargetedNoteIds) {
  if (row.actionType != EditActionType::Create) {
    return false;
  }
  for (const MidiEvent& evt : row.addedEvents) {
    if (!evt.isNoteOn() && !evt.isNoteOff()) {
      continue;
    }
    if (evt.data.noteData.note == pitch) {
      return true;
    }
    if (containsNoteId(retargetedNoteIds, evt.noteId)) {
      return true;
    }
  }
  return false;
}

struct CommittedNoteEventWalkCtx {
  SessionMidiEventVec* out = nullptr;
};

LOOP_COLD_MEM void appendCommittedNoteEvent(const MidiEvent& evt, void* raw) {
  ++g_committedPitchQueryWork.sourceEventsScanned;
  auto* ctx = static_cast<CommittedNoteEventWalkCtx*>(raw);
  if (ctx == nullptr || ctx->out == nullptr) {
    return;
  }
  if (evt.isNoteOn() || evt.isNoteOff()) {
    ctx->out->push_back(evt);
  }
}

// Same pairing as EditApply findNoteOffForOnIndex: LIFO off after this on, same channel+pitch.
LOOP_COLD_MEM int indexOfPairedNoteOff(const SessionMidiEventVec& events, int onIndex) {
  if (onIndex < 0 || static_cast<size_t>(onIndex) >= events.size()) {
    return -1;
  }
  const MidiEvent& onEvt = events[static_cast<size_t>(onIndex)];
  const uint8_t channel = onEvt.channel;
  const uint8_t note = onEvt.data.noteData.note;
  const uint32_t startTick = onEvt.tick;
  for (size_t i = static_cast<size_t>(onIndex) + 1; i < events.size(); ++i) {
    const MidiEvent& evt = events[i];
    if (evt.isNoteOn() && evt.channel == channel && evt.data.noteData.note == note &&
        evt.tick > startTick) {
      break;
    }
    if (evt.isNoteOff() && evt.channel == channel && evt.data.noteData.note == note &&
        evt.tick >= startTick) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void collectActiveCommittedChunkLists(const LoopPasses& passes,
                                      std::vector<const CommittedChunkIdList*>& lists) {
  lists.clear();
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Active &&
      !passes.recordPass.committedChunkIds.empty()) {
    lists.push_back(&passes.recordPass.committedChunkIds);
  }
  std::vector<const OverdubPass*> activeOverdubs;
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.state == CapturePassState::Active && !pass.committedChunkIds.empty()) {
      activeOverdubs.push_back(&pass);
    }
  }
  std::sort(activeOverdubs.begin(), activeOverdubs.end(),
            [](const OverdubPass* a, const OverdubPass* b) {
              return a->mergeSequence < b->mergeSequence;
            });
  for (const OverdubPass* pass : activeOverdubs) {
    lists.push_back(&pass->committedChunkIds);
  }
}

template <typename MidiEventVector>
void sortMidiEventsByTick(MidiEventVector& events) {
  std::sort(events.begin(), events.end(),
            [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
}

}  // namespace

void Loop::mergeActiveCapturePasses(MidiEventVec& out) const {
  mergeActiveCapturePassesInto(passes, out);
}

void Loop::mergeActiveCapturePasses(SessionMidiEventVec& out) const {
  mergeActiveCapturePassesInto(passes, out);
}

void Loop::materializeEditViewFromPasses() const {
  Loop* self = const_cast<Loop*>(this);
  const bool storeEmptyMaterialized =
      self->hasCommittedPasses() && self->passesMaterializedStore_.readStore().empty() &&
      self->passes.editPasses.empty();
  if (!passesMaterializedStoreStale_ && !storeEmptyMaterialized) {
    return;
  }
  LoopEventStore::enterEphemeralSeal();
  passes.materialize(self->passesMaterializedStore_.mutStore(), self->loopLengthTicks);
  LoopEventStore::leaveEphemeralSeal();
  self->passesMaterializedStore_.discardEventsCache();
  self->passesMaterializedStoreStale_ = false;
}

void Loop::rematerializeEditView(LoopEventStore& store) const {
  LoopEventStore::enterEphemeralSeal();
  passes.materialize(store, loopLengthTicks);
  LoopEventStore::leaveEphemeralSeal();
}

SessionMidiEventVec& Loop::midiEvents() {
  materializeEditViewFromPasses();
  return passesMaterializedStore_.mutEvents();
}

const SessionMidiEventVec& Loop::midiEvents() const {
  materializeEditViewFromPasses();
  return passesMaterializedStore_.readEvents();
}

void Loop::discardPassesMaterializedCache() {
  passesMaterializedStore_.mutStore().clear();
  passesMaterializedStore_.discardEventsCache();
  passesMaterializedStoreStale_ = true;
}

LOOP_COLD_MEM bool Loop::tryDiscardPassesMaterializedCache() {
  if (!passesMaterializedStoreStale_) {
    return false;
  }
  if (captureActive()) {
    return false;
  }
  const bool hadStore = !passesMaterializedStore_.empty();
  discardPassesMaterializedCache();
  return hadStore;
}

void Loop::mergeMaterializedPassesWithCapture(MidiEventVec& out) const {
  gatherCommittedEventsWithCapture(out);
}

void Loop::mergeMaterializedPassesWithCapture(SessionMidiEventVec& out) const {
  gatherCommittedEventsWithCapture(out);
}

LOOP_COLD_MEM void Loop::gatherCommittedEvents(SessionMidiEventVec& out) const {
  if (hasActiveEditPasses(passes)) {
    DIAG_COUNTER_INC(LegacyMidiEvents);
    DIAG_COUNTER_INC(PlaybackFullMaterialize);
    ++g_committedPitchQueryWork.fullMaterializeCount;
    const SessionMidiEventVec& materialized = midiEvents();
    out.assign(materialized.begin(), materialized.end());
    return;
  }
  if (isPassesMaterializedStoreFresh()) {
    passes.materializeToEventVector(out, loopLengthTicks);
    return;
  }
  std::vector<const CommittedChunkIdList*> lists;
  collectActiveCommittedChunkLists(passes, lists);
  if (lists.empty()) {
    out.clear();
    return;
  }
  CommittedEventRange::full(lists.data(), lists.size(), loopLengthTicks).appendTo(out);
  sortMidiEventsByTick(out);
}

LOOP_COLD_MEM void Loop::resetCommittedPitchQueryWork() {
  g_committedPitchQueryWork = CommittedPitchQueryWork{};
}

LOOP_COLD_MEM uint32_t Loop::committedPitchQuerySourceEventsScanned() {
  return g_committedPitchQueryWork.sourceEventsScanned;
}

LOOP_COLD_MEM uint32_t Loop::committedPitchQueryCandidateEvents() {
  return g_committedPitchQueryWork.candidateEvents;
}

LOOP_COLD_MEM uint32_t Loop::committedPitchQueryEditRowsApplied() {
  return g_committedPitchQueryWork.editRowsApplied;
}

LOOP_COLD_MEM uint32_t Loop::committedEventsFullMaterializeCount() {
  return g_committedPitchQueryWork.fullMaterializeCount;
}

LOOP_COLD_MEM void Loop::gatherCommittedNoteEventsForPitch(uint8_t pitch, SessionMidiEventVec& out) const {
  out.clear();
  std::vector<const CommittedChunkIdList*> lists;
  collectActiveCommittedChunkLists(passes, lists);

  if (!hasActiveEditPasses(passes)) {
    for (const CommittedChunkIdList* list : lists) {
      if (list == nullptr) {
        continue;
      }
      for (uint16_t chunkId : *list) {
        LoopEventStore::appendChunkRefNoteEventsForPitch(chunkId, pitch, out);
      }
    }
    sortMidiEventsByTick(out);
    g_committedPitchQueryWork.candidateEvents = static_cast<uint32_t>(out.size());
    return;
  }

  std::vector<NoteId> retargetedNoteIds;
  collectNoteIdsRetargetedToPitch(passes.editPasses, pitch, retargetedNoteIds);

  SessionMidiEventVec committedNotes;
  CommittedNoteEventWalkCtx walk{};
  walk.out = &committedNotes;
  for (const CommittedChunkIdList* list : lists) {
    if (list == nullptr) {
      continue;
    }
    for (uint16_t chunkId : *list) {
      LoopEventStore::forEachChunkEvent(chunkId, appendCommittedNoteEvent, &walk);
    }
  }

  std::vector<uint8_t> keep(committedNotes.size(), 0);
  for (size_t i = 0; i < committedNotes.size(); ++i) {
    const MidiEvent& evt = committedNotes[i];
    if (evt.data.noteData.note == pitch) {
      keep[i] = 1;
      continue;
    }
    if (!evt.isNoteOn() || !containsNoteId(retargetedNoteIds, evt.noteId)) {
      continue;
    }
    keep[i] = 1;
    const int offIndex = indexOfPairedNoteOff(committedNotes, static_cast<int>(i));
    if (offIndex >= 0) {
      keep[static_cast<size_t>(offIndex)] = 1;
    }
  }
  for (size_t i = 0; i < committedNotes.size(); ++i) {
    if (keep[i] != 0) {
      out.push_back(committedNotes[i]);
    }
  }

  std::vector<NoteId> candidateIds = retargetedNoteIds;
  for (const MidiEvent& evt : out) {
    if (evt.noteId != kInvalidNoteId && !containsNoteId(candidateIds, evt.noteId)) {
      candidateIds.push_back(evt.noteId);
    }
  }

  EditPassVec relevantRows;
  for (const EditPass& row : passes.editPasses) {
    if (row.state != EditPassState::Active || row.passType != EditPassType::Note) {
      continue;
    }
    if (createRowContributesToPitch(row, pitch, retargetedNoteIds)) {
      relevantRows.push_back(row);
      continue;
    }
    if (row.actionType == EditActionType::Create) {
      continue;
    }
    if (containsNoteId(candidateIds, row.targetNoteId)) {
      relevantRows.push_back(row);
    }
  }

  g_committedPitchQueryWork.editRowsApplied = static_cast<uint32_t>(relevantRows.size());
  if (!relevantRows.empty()) {
    applyNoteEditPassSequence(out, relevantRows, loopLengthTicks);
  }

  SessionMidiEventVec filtered;
  filtered.reserve(out.size());
  for (const MidiEvent& evt : out) {
    if ((evt.isNoteOn() || evt.isNoteOff()) && evt.data.noteData.note == pitch) {
      filtered.push_back(evt);
    }
  }
  out = std::move(filtered);
  sortMidiEventsByTick(out);
  g_committedPitchQueryWork.candidateEvents = static_cast<uint32_t>(out.size());
}

LOOP_COLD_MEM void Loop::gatherCommittedEvents(MidiEventVec& out) const {
  SessionMidiEventVec extmemFlat;
  gatherCommittedEvents(extmemFlat);
  out.assign(extmemFlat.begin(), extmemFlat.end());
}

LOOP_COLD_MEM void Loop::gatherCommittedEventsInWindow(SessionMidiEventVec& out, uint32_t windowStart,
                                                       uint32_t windowLength) const {
  if (loopLengthTicks == 0 || windowLength == 0) {
    out.clear();
    return;
  }
  if (hasActiveEditPasses(passes) && !shouldAvoidFullVisualRebuild(loopLengthTicks)) {
    SessionMidiEventVec full;
    gatherCommittedEvents(full);
    DisplayWindowUtils::filterMidiEventsToWindow(full, out, windowStart, windowLength,
                                                 loopLengthTicks);
    return;
  }
  std::vector<const CommittedChunkIdList*> lists;
  collectActiveCommittedChunkLists(passes, lists);
  if (lists.empty()) {
    out.clear();
    return;
  }
  CommittedEventRange::inWindow(lists.data(), lists.size(), loopLengthTicks, windowStart,
                                windowLength)
      .appendTo(out);
  sortMidiEventsByTick(out);
}

LOOP_COLD_MEM void Loop::gatherCommittedEventsInWindow(MidiEventVec& out, uint32_t windowStart,
                                                       uint32_t windowLength) const {
  SessionMidiEventVec extmemFlat;
  gatherCommittedEventsInWindow(extmemFlat, windowStart, windowLength);
  out.assign(extmemFlat.begin(), extmemFlat.end());
}

LOOP_COLD_MEM void Loop::gatherCommittedEventsInWindowWithCapture(SessionMidiEventVec& out,
                                                                  uint32_t windowStart,
                                                                  uint32_t windowLength) const {
  gatherCommittedEventsInWindow(out, windowStart, windowLength);
  if (!captureActive() || capture.store.empty()) {
    return;
  }
  SessionMidiEventVec captureFlat;
  const_cast<Loop*>(this)->ensureCaptureEventsSorted();
  capture.store.copyEventsTo(captureFlat);
  SessionMidiEventVec captureWindow;
  DisplayWindowUtils::filterMidiEventsToWindow(captureFlat, captureWindow, windowStart, windowLength,
                                               loopLengthTicks);
  if (captureWindow.empty()) {
    return;
  }
  if (out.empty()) {
    out = std::move(captureWindow);
    return;
  }
  SessionMidiEventVec merged;
  merged.reserve(out.size() + captureWindow.size());
  std::merge(out.begin(), out.end(), captureWindow.begin(), captureWindow.end(),
             std::back_inserter(merged),
             [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
  out = std::move(merged);
}

LOOP_COLD_MEM bool Loop::shouldAvoidFullVisualRebuild(uint32_t loopLength) const {
  const uint32_t boundedThreshold =
      DisplayWindowUtils::kMaxDetailedWindowBars * Config::TICKS_PER_BAR;
  return loopLength > boundedThreshold;
}

LOOP_COLD_MEM void Loop::gatherCommittedEventsForDerivedView(SessionMidiEventVec& flat) const {
  gatherCommittedEvents(flat);
}

LOOP_COLD_MEM void Loop::gatherCommittedEventsForDerivedView(MidiEventVec& flat) const {
  gatherCommittedEvents(flat);
}

LOOP_COLD_MEM void Loop::gatherCommittedEventsWithCapture(SessionMidiEventVec& flat) const {
  gatherCommittedEvents(flat);
  mergeCaptureStoreIntoMaterializedEvents(*this, flat);
}

LOOP_COLD_MEM void Loop::gatherCommittedEventsWithCapture(MidiEventVec& flat) const {
  gatherCommittedEvents(flat);
  mergeCaptureStoreIntoMaterializedEvents(*this, flat);
}
