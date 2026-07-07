//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"
#include "Utils/LoopStopFinalize.h"
#include "Utils/CaptureIncrementalSanity.h"
#include "Utils/IntervalProjection.h"
#include "Globals.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/Diagnostics.h"
#include "Logger.h"
#include <algorithm>
#include <chrono>

namespace {

bool eventsEquivalent(const MidiEvent& a, const MidiEvent& b) {
  if (a.type != b.type || a.channel != b.channel || a.tick != b.tick) {
    return false;
  }
  if (a.type == midi::NoteOn || a.type == midi::NoteOff) {
    return a.data.noteData.note == b.data.noteData.note &&
           a.data.noteData.velocity == b.data.noteData.velocity;
  }
  if (a.type == midi::ControlChange) {
    return a.data.ccData.cc == b.data.ccData.cc &&
           a.data.ccData.value == b.data.ccData.value;
  }
  return true;
}

bool isDuplicateCaptureEvent(const Loop& loop, const MidiEvent& candidate) {
  const uint32_t lo = (candidate.tick > Config::DUPLICATE_TICK_TOLERANCE)
                          ? (candidate.tick - Config::DUPLICATE_TICK_TOLERANCE)
                          : 0;
  const uint32_t hi = candidate.tick + Config::DUPLICATE_TICK_TOLERANCE;

  const size_t captureCount = loop.capture.store.size();
  for (size_t i = captureCount; i > 0; --i) {
    const MidiEvent& evt = loop.capture.store.at(i - 1);
    if (evt.tick < lo) {
      break;
    }
    if (evt.tick > hi) {
      continue;
    }
    if (eventsEquivalent(evt, candidate)) {
      return true;
    }
  }
  return false;
}

const char* capturePhaseLabel(CapturePhase phase) {
  switch (phase) {
    case CapturePhase::Record:
      return "record";
    case CapturePhase::Overdub:
      return "overdub";
    default:
      return "none";
  }
}

void sortCaptureStoreByTick(LoopEventStore& store) {
  SessionMidiEventVec sorted;
  store.flatten(sorted);
  std::stable_sort(sorted.begin(), sorted.end(),
                   [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
  store.clear();
  store.loadFromFlat(sorted);
}

void markPreviewSpan(CapturePreview& preview, uint32_t startTick, uint32_t endTick, uint32_t ticksPerBar) {
  if (ticksPerBar == 0) {
    return;
  }
  const uint32_t startBar = startTick / ticksPerBar;
  const uint32_t endBar = endTick / ticksPerBar;
  for (uint32_t bar = startBar; bar <= endBar; ++bar) {
    preview.markBarDirty(bar);
  }
}

void applyCaptureEventToPreview(CapturePreview& preview, const MidiEvent& evt, uint32_t ticksPerBar) {
  if (evt.isNoteOn()) {
    NoteUtils::DisplayNote note{};
    note.note = evt.data.noteData.note;
    note.velocity = evt.data.noteData.velocity;
    note.startTick = evt.tick;
    note.endTick = evt.tick;
    preview.notes.push_back(note);
    markPreviewSpan(preview, evt.tick, evt.tick, ticksPerBar);
    ++preview.revision;
    return;
  }

  if (!evt.isNoteOff()) {
    return;
  }

  for (auto it = preview.notes.rbegin(); it != preview.notes.rend(); ++it) {
    if (it->note != evt.data.noteData.note) {
      continue;
    }
    if (it->endTick != it->startTick) {
      continue;
    }
    const uint32_t start = it->startTick;
    it->endTick = evt.tick;
    markPreviewSpan(preview, start, evt.tick, ticksPerBar);
    ++preview.revision;
    return;
  }
}

void rebuildCapturePreviewFromStore(Loop& loop) {
  MidiEventVec flat;
  loop.capture.store.flatten(flat);
  const NoteUtils::DisplayNoteVec rebuiltNotes =
      NoteUtils::reconstructDisplayNotes(flat, loop.loopLengthTicks, false);
  loop.capturePreview.notes.assign(rebuiltNotes.begin(), rebuiltNotes.end());
  loop.capturePreview.dirtyBars.clear();
  if (loop.loopLengthTicks > 0) {
    for (const auto& note : loop.capturePreview.notes) {
      const uint32_t endTick = note.endTick >= note.startTick ? note.endTick : note.startTick;
      markPreviewSpan(loop.capturePreview, note.startTick, endTick, Config::TICKS_PER_BAR);
    }
  }
  ++loop.capturePreview.revision;
}

ChunkIdList deepCloneChunkRefs(const ChunkIdList& refs) {
  if (refs.empty()) {
    return {};
  }
  SessionMidiEventVec flat;
  LoopEventStore::appendChunkRefEvents(refs, flat);
  LoopEventStore store;
  store.loadFromFlat(flat);
  ChunkIdList cloned;
  store.detachChunksTo(cloned);
  return cloned;
}

RecordPass deepCloneRecordPass(const RecordPass& pass) {
  RecordPass cloned = pass;
  cloned.chunkRefs = deepCloneChunkRefs(pass.chunkRefs);
  return cloned;
}

OverdubPass deepCloneOverdubPass(const OverdubPass& pass) {
  OverdubPass cloned = pass;
  cloned.chunkRefs = deepCloneChunkRefs(pass.chunkRefs);
  return cloned;
}

LoopPasses deepClonePasses(const LoopPasses& passes) {
  LoopPasses cloned;
  if (passes.hasRecordPass()) {
    cloned.recordPass = deepCloneRecordPass(passes.recordPass);
  }
  cloned.overdubPasses.reserve(passes.overdubPasses.size());
  for (const OverdubPass& pass : passes.overdubPasses) {
    cloned.overdubPasses.push_back(deepCloneOverdubPass(pass));
  }
  cloned.editPasses = passes.editPasses;
  return cloned;
}

size_t estimatedEditPassBytes(const EditPass& row) {
  size_t bytes = sizeof(EditPass);
  bytes += row.addedEvents.size() * sizeof(MidiEvent);
  return bytes;
}

bool canHeapAdmitEditPass(const EditPass& row) {
  const size_t needed = Config::HEAP_RESERVE_BYTES + estimatedEditPassBytes(row);
  return MemoryMonitor::getInternalHeapFreeBytes() >= needed;
}

const char* sealOutcomeLabel(SealOutcome outcome) {
  switch (outcome) {
    case SealOutcome::Ok:
      return "ok";
    case SealOutcome::SkippedEmpty:
      return "skipped_empty";
    case SealOutcome::FailedValidation:
      return "failed_validation";
    case SealOutcome::AlreadyPending:
      return "already_pending";
    case SealOutcome::PoolExhausted:
      return "pool_exhausted";
  }
  return "unknown";
}

size_t stopPathChunkRefCount(const Loop& loop) {
  size_t refs = 0;
  if (loop.passes.hasRecordPass() && loop.passes.recordPass.state == CapturePassState::Active) {
    refs += loop.passes.recordPass.chunkRefs.size();
  }
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    if (pass.state == CapturePassState::Active) {
      refs += pass.chunkRefs.size();
    }
  }
  if (loop.hasPendingCapturePass()) {
    refs += loop.pendingCapturePass().chunkRefs.size();
  }
  return refs;
}

size_t stopPathEventCount(const Loop& loop) {
  size_t events = 0;
  if (loop.passes.hasRecordPass() && loop.passes.recordPass.state == CapturePassState::Active) {
    events += LoopEventStore::countEventsInChunkIds(loop.passes.recordPass.chunkRefs);
  }
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    if (pass.state == CapturePassState::Active) {
      events += LoopEventStore::countEventsInChunkIds(pass.chunkRefs);
    }
  }
  if (loop.hasPendingCapturePass()) {
    events += LoopEventStore::countEventsInChunkIds(loop.pendingCapturePass().chunkRefs);
  }
  if (loop.captureActive()) {
    events += loop.capture.store.size();
  }
  return events;
}

uint32_t traceMicros() {
#if defined(ARDUINO)
  return micros();
#else
  using namespace std::chrono;
  return static_cast<uint32_t>(
      duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
#endif
}

}  // namespace

bool Loop::hasPublishedEvents() const {
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Active &&
      !passes.recordPass.chunkRefs.empty()) {
    return true;
  }
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.state == CapturePassState::Active && !pass.chunkRefs.empty()) {
      return true;
    }
  }
  return false;
}

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
      !passes.recordPass.chunkRefs.empty()) {
    LoopEventStore::appendChunkRefEvents(passes.recordPass.chunkRefs, out);
  }
  std::vector<const OverdubPass*> activeOverdubs;
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.state == CapturePassState::Active && !pass.chunkRefs.empty()) {
      activeOverdubs.push_back(&pass);
    }
  }
  std::sort(activeOverdubs.begin(), activeOverdubs.end(),
            [](const OverdubPass* a, const OverdubPass* b) {
              return a->mergeSequence < b->mergeSequence;
            });
  for (const OverdubPass* pass : activeOverdubs) {
    MidiEventVector layer;
    LoopEventStore::appendChunkRefEvents(pass->chunkRefs, layer);
    mergeSortedLoopCaptureLayers(out, std::move(layer));
  }
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
  const bool storeEmptyPublished =
      self->hasPublishedEvents() && self->passesMaterializedStore_.readStore().empty() &&
      self->passes.editPasses.empty();
  if (!passesMaterializedStoreStale_ && !storeEmptyPublished) {
    return;
  }
  passes.materialize(self->passesMaterializedStore_.mutStore(), self->loopLengthTicks);
  self->passesMaterializedStore_.discardFlatCache();
  self->passesMaterializedStoreStale_ = false;
}

void Loop::rematerializeEditView(LoopEventStore& store) const {
  passes.materialize(store, loopLengthTicks);
}

EditPassId Loop::saveNoteEditPass(uint8_t editPassIndex, EditPass row, EditPassType passType) {
  if (!canHeapAdmitEditPass(row)) {
    logger.log(CAT_TRACK, LOG_WARNING,
               "saveNoteEditPass rejected: heap below reserve (need=%u free=%u)",
               static_cast<unsigned>(Config::HEAP_RESERVE_BYTES + estimatedEditPassBytes(row)),
               static_cast<unsigned>(MemoryMonitor::getInternalHeapFreeBytes()));
    return kInvalidEditPassId;
  }
  row.id = nextPassId_++;
  row.passType = passType;
  row.editPassIndex = editPassIndex;
  row.state = EditPassState::Active;
  passes.editPasses.push_back(std::move(row));
  ++playbackRevision;
  editStateDirty_ = true;
  markPassDerivedStale();
  return passes.editPasses.back().id;
}

EditPassIdList Loop::replaceNoteEditPass(uint8_t editPassIndex,
                                         const EditPassIdList& staleEditPassIds,
                                         EditPassVec rows) {
  EditPassIdList replacementIds;
  if (staleEditPassIds.empty()) {
    return replacementIds;
  }
  if (rows.empty()) {
    disableEditPasses(staleEditPassIds);
    editStateDirty_ = true;
    return replacementIds;
  }

  for (EditPass& row : rows) {
    const EditPassId id = saveNoteEditPass(editPassIndex, std::move(row));
    if (id != kInvalidEditPassId) {
      replacementIds.push_back(id);
    }
  }
  if (replacementIds.empty()) {
    return replacementIds;
  }
  disableEditPasses(staleEditPassIds);
  return replacementIds;
}

void Loop::disableEditPasses(const EditPassIdList& ids) {
  for (const EditPassId id : ids) {
    for (EditPass& editPass : passes.editPasses) {
      if (editPass.id == id) {
        editPass.state = EditPassState::Disabled;
      }
    }
  }
  ++playbackRevision;
  markPassDerivedStale();
}

void Loop::enableEditPasses(const EditPassIdList& ids) {
  for (const EditPassId id : ids) {
    for (EditPass& editPass : passes.editPasses) {
      if (editPass.id == id) {
        editPass.state = EditPassState::Active;
      }
    }
  }
  ++playbackRevision;
  markPassDerivedStale();
}

void Loop::materializeExcludingEditPassIds(const EditPassIdList& excludeIds,
                                           SessionMidiEventVec& out) const {
  LoopPasses scopedPasses = passes;
  for (EditPass& editPass : scopedPasses.editPasses) {
    for (const EditPassId id : excludeIds) {
      if (editPass.id == id) {
        editPass.state = EditPassState::Disabled;
        break;
      }
    }
  }
  scopedPasses.materializeToEventVector(out, loopLengthTicks);
}

void Loop::materializeExcludingEditPassIds(const EditPassIdList& excludeIds,
                                           MidiEventVec& out) const {
  LoopPasses scopedPasses = passes;
  for (EditPass& editPass : scopedPasses.editPasses) {
    for (const EditPassId id : excludeIds) {
      if (editPass.id == id) {
        editPass.state = EditPassState::Disabled;
        break;
      }
    }
  }
  scopedPasses.materializeToEventVector(out, loopLengthTicks);
}

void Loop::freeActiveCapturePassChunks() {
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Active) {
    LoopEventStore staging;
    staging.adoptChunkIds(passes.recordPass.chunkRefs);
    staging.clear();
    passes.recordPass.chunkRefs.clear();
    passes.recordPass.id = kInvalidPassId;
  }
  for (OverdubPass& pass : passes.overdubPasses) {
    if (pass.state != CapturePassState::Active) {
      continue;
    }
    LoopEventStore staging;
    staging.adoptChunkIds(pass.chunkRefs);
    staging.clear();
    pass.chunkRefs.clear();
  }
  passes.overdubPasses.erase(
      std::remove_if(passes.overdubPasses.begin(), passes.overdubPasses.end(),
                     [](const OverdubPass& pass) {
                       return pass.state == CapturePassState::Active;
                     }),
      passes.overdubPasses.end());
}

bool Loop::reclaimDisabledCapturePass(PassId id) {
  if (id == kInvalidPassId) {
    return false;
  }
  if (passes.hasRecordPass() && passes.recordPass.id == id &&
      passes.recordPass.state == CapturePassState::Disabled) {
    LoopEventStore staging;
    staging.adoptChunkIds(passes.recordPass.chunkRefs);
    staging.clear();
    passes.recordPass.chunkRefs.clear();
    passes.recordPass.id = kInvalidPassId;
    ++playbackRevision;
    markPassDerivedStale();
    return true;
  }
  for (auto it = passes.overdubPasses.begin(); it != passes.overdubPasses.end(); ++it) {
    if (it->id != id || it->state != CapturePassState::Disabled) {
      continue;
    }
    LoopEventStore staging;
    staging.adoptChunkIds(it->chunkRefs);
    staging.clear();
    passes.overdubPasses.erase(it);
    ++playbackRevision;
    markPassDerivedStale();
    return true;
  }
  return false;
}

void Loop::reclaimUnreferencedDisabledCapturePasses(const SlotPassReferences& refs) {
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Disabled &&
      !refs.referencesCapturePass(passes.recordPass.id)) {
    reclaimDisabledCapturePass(passes.recordPass.id);
  }
  for (size_t i = passes.overdubPasses.size(); i > 0; --i) {
    const OverdubPass& pass = passes.overdubPasses[i - 1];
    if (pass.state == CapturePassState::Disabled && !refs.referencesCapturePass(pass.id)) {
      reclaimDisabledCapturePass(pass.id);
    }
  }
}

void Loop::reclaimUnreferencedDisabledEditPasses(const SlotPassReferences& refs) {
  passes.editPasses.erase(
      std::remove_if(passes.editPasses.begin(), passes.editPasses.end(),
                     [&](const EditPass& editPass) {
                       return editPass.state == EditPassState::Disabled &&
                              !refs.referencesEditPass(editPass.id);
                     }),
      passes.editPasses.end());
}

void Loop::reclaimUnreferencedDisabledPasses(const SlotPassReferences& refs) {
  reclaimUnreferencedDisabledCapturePasses(refs);
  reclaimUnreferencedDisabledEditPasses(refs);
}

void Loop::markPassDerivedStale() {
  passesMaterializedStoreStale_ = true;
  playbackOrderDirty = true;
  visualCacheDirty = true;
  invalidatePlaybackCaches();
}

SessionMidiEventVec& Loop::midiEvents() {
  materializeEditViewFromPasses();
  return passesMaterializedStore_.mutFlat();
}

const SessionMidiEventVec& Loop::midiEvents() const {
  materializeEditViewFromPasses();
  return passesMaterializedStore_.readFlat();
}

LoopSnapshotRef Loop::sharePassesSnapshot() const {
  auto snapshot = std::make_shared<PersistedLoopSnapshot>();
  snapshot->loopId = loopId;
  snapshot->startLoopTick = startLoopTick;
  snapshot->loopLengthTicks = loopLengthTicks;
  snapshot->loopStartTick = loopStartTick;
  snapshot->nextPassId = nextPassId_;
  snapshot->nextNoteId = nextNoteId_;
  snapshot->nextMergeSequence = nextMergeSequence_;
  snapshot->lastPublishedPassId = lastPublishedPassId_;
  snapshot->passes = deepClonePasses(passes);
  return snapshot;
}

void Loop::restorePassesSnapshot(const PersistedLoopSnapshot& snapshot) {
  discardPendingCapturePass();
  discardCapture();
  resetPassTimeline();
  loopId = snapshot.loopId;
  startLoopTick = snapshot.startLoopTick;
  loopLengthTicks = snapshot.loopLengthTicks;
  loopStartTick = snapshot.loopStartTick;
  nextPassId_ = snapshot.nextPassId == 0 ? 1 : snapshot.nextPassId;
  nextNoteId_ = snapshot.nextNoteId == 0 ? 1 : snapshot.nextNoteId;
  nextMergeSequence_ = snapshot.nextMergeSequence;
  lastPublishedPassId_ = snapshot.lastPublishedPassId;
  lastTickInLoop = 0;
  nextEventIndex = 0;
  playbackOrderDirty = true;
  passes = deepClonePasses(snapshot.passes);
  ++playbackRevision;
  discardPassesMaterializedCache();
  markDisplayCachesStale();
}

void Loop::discardPassesMaterializedCache() {
  passesMaterializedStore_.mutStore().clear();
  passesMaterializedStore_.discardFlatCache();
  passesMaterializedStoreStale_ = true;
}

void Loop::commitStopFinalizeFromStore(LoopEventStore& merged) {
  const PassId preserveId = lastPublishedPassId_;
  CapturePassPhase preservePhase = CapturePassPhase::Overdub;
  uint32_t preserveMergeSeq = 0;
  if (passes.hasRecordPass() && passes.recordPass.id == preserveId) {
    preservePhase = CapturePassPhase::Record;
  } else {
    for (const OverdubPass& pass : passes.overdubPasses) {
      if (pass.state == CapturePassState::Active && pass.id == preserveId) {
        preserveMergeSeq = pass.mergeSequence;
        break;
      }
    }
  }

  ChunkIdList refs;
  merged.detachChunksTo(refs);
  if (refs.empty()) {
    return;
  }

  freeActiveCapturePassChunks();

  if (preservePhase == CapturePassPhase::Record) {
    RecordPass rebuilt{};
    rebuilt.id = (preserveId != kInvalidPassId) ? preserveId : nextPassId_++;
    if (rebuilt.id >= nextPassId_) {
      nextPassId_ = rebuilt.id + 1;
    }
    rebuilt.state = CapturePassState::Active;
    rebuilt.chunkRefs = std::move(refs);
    passes.recordPass = rebuilt;
    lastPublishedPassId_ = rebuilt.id;
  } else {
    OverdubPass rebuilt{};
    rebuilt.id = (preserveId != kInvalidPassId) ? preserveId : nextPassId_++;
    if (rebuilt.id >= nextPassId_) {
      nextPassId_ = rebuilt.id + 1;
    }
    rebuilt.mergeSequence = preserveMergeSeq;
    rebuilt.state = CapturePassState::Active;
    rebuilt.chunkRefs = std::move(refs);
    passes.overdubPasses.push_back(rebuilt);
    lastPublishedPassId_ = rebuilt.id;
  }

  ++playbackRevision;
  discardPassesMaterializedCache();
  markDisplayCachesStale();
}

void Loop::seedRecordPassFromStore(LoopEventStore& store) {
  resetPassTimeline();
  if (store.empty()) {
    discardPassesMaterializedCache();
    return;
  }
  ChunkIdList refs;
  store.detachChunksTo(refs);
  if (refs.empty()) {
    discardPassesMaterializedCache();
    return;
  }
  RecordPass record{};
  record.id = nextPassId_++;
  record.state = CapturePassState::Active;
  record.chunkRefs = std::move(refs);
  passes.recordPass = std::move(record);
  lastPublishedPassId_ = passes.recordPass.id;
  ++playbackRevision;
  markPassDerivedStale();
  discardPassesMaterializedCache();
  rebuildVisualCacheFromPasses();
}

void Loop::shiftActiveCapturePassTicks(int64_t delta) {
  if (delta == 0 || !hasPublishedEvents()) {
    return;
  }
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Active &&
      !passes.recordPass.chunkRefs.empty()) {
    MidiEventVec flat;
    LoopEventStore::appendChunkRefEvents(passes.recordPass.chunkRefs, flat);
    if (!flat.empty()) {
      LoopEventStore staging;
      staging.loadFromFlat(flat);
      staging.shiftAllTicks(delta);
      LoopEventStore temp;
      temp.adoptAll(staging);
      passes.recordPass.chunkRefs.clear();
      temp.detachChunksTo(passes.recordPass.chunkRefs);
    }
  }
  for (OverdubPass& pass : passes.overdubPasses) {
    if (pass.state != CapturePassState::Active || pass.chunkRefs.empty()) {
      continue;
    }
    MidiEventVec flat;
    LoopEventStore::appendChunkRefEvents(pass.chunkRefs, flat);
    if (flat.empty()) {
      continue;
    }
    LoopEventStore staging;
    staging.loadFromFlat(flat);
    staging.shiftAllTicks(delta);
    LoopEventStore temp;
    temp.adoptAll(staging);
    pass.chunkRefs.clear();
    temp.detachChunksTo(pass.chunkRefs);
  }
  for (EditPass& editPass : passes.editPasses) {
    if (editPass.state != EditPassState::Active) {
      continue;
    }
    if (editPass.passType != EditPassType::Note) {
      continue;
    }
    editPass.startTick =
        static_cast<uint32_t>(static_cast<int64_t>(editPass.startTick) + delta);
    editPass.endTick =
        static_cast<uint32_t>(static_cast<int64_t>(editPass.endTick) + delta);
    for (MidiEvent& evt : editPass.addedEvents) {
      evt.tick = static_cast<uint32_t>(static_cast<int64_t>(evt.tick) + delta);
    }
  }
  ++playbackRevision;
  markPassDerivedStale();
}

void Loop::beginCapture(CapturePhase phase) {
  discardPendingCapturePass();
  capture.phase = phase;
  capture.store.clear();
  capturePreview.clear();
  captureNextEventIndex = 0;
  captureEventsSortDirty = false;
  captureDedupEventsDropped_ = 0;
  ++captureDisplayRevision;
}

void Loop::discardCapture() {
  capture.store.clear();
  capture.phase = CapturePhase::None;
  captureNextEventIndex = 0;
  captureEventsSortDirty = false;
  capturePreview.clear();
  captureDedupEventsDropped_ = 0;
}

bool Loop::appendCaptureEvent(const MidiEvent& evt) {
  if (capture.phase == CapturePhase::None) {
    return false;
  }
  if (hasPendingCapturePass_) {
    return false;
  }
  if (isDuplicateCaptureEvent(*this, evt)) {
    ++captureDedupEventsDropped_;
    return false;
  }
  if (!capture.store.append(evt)) {
    return false;
  }
  captureEventsSortDirty = true;
  applyCaptureEventToPreview(capturePreview, evt, Config::TICKS_PER_BAR);
  ++captureDisplayRevision;
  return true;
}

size_t Loop::liveEventCount() const {
  MidiEventVec flat;
  passes.materializeToEventVector(flat, loopLengthTicks);
  size_t count = flat.size();
  if (captureActive()) {
    count += capture.store.size();
  }
  return count;
}

size_t Loop::displayEventCountHint() const {
  size_t count = publishedMaterializedEventCount_;
  if (captureActive()) {
    count += capture.store.size();
  }
  return count;
}

bool Loop::captureActive() const {
  return capture.phase != CapturePhase::None;
}

bool Loop::ensureCaptureEventsSorted() {
  if (!captureEventsSortDirty) {
    return false;
  }
  sortCaptureStoreByTick(capture.store);
  captureEventsSortDirty = false;
  return true;
}

void Loop::mergeMaterializedPassesWithCapture(MidiEventVec& out) const {
  passes.materializeToEventVector(out, loopLengthTicks);
  if (!captureActive() || capture.store.empty()) {
    return;
  }
  const_cast<Loop*>(this)->ensureCaptureEventsSorted();

  MidiEventVec captureFlat;
  capture.store.flatten(captureFlat);
  if (out.empty()) {
    out = std::move(captureFlat);
    return;
  }
  if (captureFlat.empty()) {
    return;
  }

  MidiEventVec merged;
  merged.reserve(out.size() + captureFlat.size());
  std::merge(out.begin(), out.end(), captureFlat.begin(), captureFlat.end(),
             std::back_inserter(merged),
             [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
  out = std::move(merged);
}

void Loop::mergeMaterializedPassesWithCapture(SessionMidiEventVec& out) const {
  passes.materializeToEventVector(out, loopLengthTicks);
  if (!captureActive() || capture.store.empty()) {
    return;
  }
  const_cast<Loop*>(this)->ensureCaptureEventsSorted();

  SessionMidiEventVec captureFlat;
  capture.store.flatten(captureFlat);
  if (out.empty()) {
    out = std::move(captureFlat);
    return;
  }
  if (captureFlat.empty()) {
    return;
  }

  SessionMidiEventVec merged;
  merged.reserve(out.size() + captureFlat.size());
  std::merge(out.begin(), out.end(), captureFlat.begin(), captureFlat.end(),
             std::back_inserter(merged),
             [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
  out = std::move(merged);
}

void Loop::removeCaptureNoteOffAt(uint8_t channel, uint8_t note, uint32_t tick) {
  if (capture.store.empty()) {
    return;
  }

  MidiEventVec flat;
  capture.store.flatten(flat);
  bool removed = false;
  for (auto it = flat.begin(); it != flat.end(); ++it) {
    if (!it->isNoteOff() || it->channel != channel || it->data.noteData.note != note ||
        it->tick != tick) {
      continue;
    }
    flat.erase(it);
    removed = true;
    break;
  }

  if (!removed) {
    return;
  }

  capture.store.clear();
  if (!flat.empty()) {
    capture.store.loadFromFlat(flat);
  }
  captureEventsSortDirty = false;
  ++captureDisplayRevision;
  rebuildCapturePreviewFromStore(*this);
}

NoteId Loop::allocateNoteId() {
  return nextNoteId_++;
}

void Loop::assignMissingNoteIds(SessionMidiEventVec& events) {
  for (MidiEvent& evt : events) {
    if (evt.isNoteOn() && evt.noteId == kInvalidNoteId) {
      evt.noteId = allocateNoteId();
      logger.log(CAT_TRACK, LOG_WARNING,
                 "assignMissingNoteIds: assigned noteId=%lu tick=%lu pitch=%u",
                 static_cast<unsigned long>(evt.noteId), static_cast<unsigned long>(evt.tick),
                 static_cast<unsigned>(evt.data.noteData.note));
    }
  }
}

void Loop::assignMissingNoteIds(MidiEventVec& events) {
  for (MidiEvent& evt : events) {
    if (evt.isNoteOn() && evt.noteId == kInvalidNoteId) {
      evt.noteId = allocateNoteId();
      logger.log(CAT_TRACK, LOG_WARNING,
                 "assignMissingNoteIds: assigned noteId=%lu tick=%lu pitch=%u",
                 static_cast<unsigned long>(evt.noteId), static_cast<unsigned long>(evt.tick),
                 static_cast<unsigned>(evt.data.noteData.note));
    }
  }
}

void Loop::assignMissingNoteIdsInStore(LoopEventStore& store) {
  SessionMidiEventVec flat;
  store.flatten(flat);
  assignMissingNoteIds(flat);
  store.clear();
  store.loadFromFlat(flat);
}

void Loop::resetPassTimeline() {
  discardPendingCapturePass();
  if (passes.hasRecordPass()) {
    LoopEventStore staging;
    staging.adoptChunkIds(passes.recordPass.chunkRefs);
    staging.clear();
    passes.recordPass = RecordPass{};
  }
  for (OverdubPass& pass : passes.overdubPasses) {
    LoopEventStore staging;
    staging.adoptChunkIds(pass.chunkRefs);
    staging.clear();
  }
  passes.overdubPasses.clear();
  passes.editPasses.clear();
  nextPassId_ = 1;
  nextNoteId_ = 1;
  nextMergeSequence_ = 0;
  lastPublishedPassId_ = kInvalidPassId;
  playbackRevision = 0;
  editStateDirty_ = false;
  visualCache.clear();
  capturePreview.clear();
  pendingVisualDelta.clear();
  visualCacheDirty = true;
  passesMaterializedStore_.mutStore().clear();
  passesMaterializedStore_.discardFlatCache();
  passesMaterializedStoreStale_ = true;
}

bool Loop::setCapturePassState(PassId id, CapturePassState state) {
  if (passes.hasRecordPass() && passes.recordPass.id == id) {
    if (passes.recordPass.state == state) {
      return true;
    }
    passes.recordPass.state = state;
    ++playbackRevision;
    markPassDerivedStale();
    return true;
  }
  for (OverdubPass& pass : passes.overdubPasses) {
    if (pass.id != id) {
      continue;
    }
    if (pass.state == state) {
      return true;
    }
    pass.state = state;
    ++playbackRevision;
    markPassDerivedStale();
    return true;
  }
  return false;
}

CommitResult Loop::commitCapturePass(CommitReason reason, uint32_t sealedAtTick) {
  const bool emitStopStage = reason == CommitReason::RecordStop ||
                             reason == CommitReason::RecordStopToStopped;
  const uint32_t commitStartUs = traceMicros();
  const size_t stopStageEventCount = stopPathEventCount(*this);
  const size_t stopStageChunkRefCount = stopPathChunkRefCount(*this);
  auto emitStage = [&](const char* stage, uint32_t durationUs,
                       uint32_t heapBefore, uint32_t heapAfter, const char* outcome) {
    if (!emitStopStage) {
      return;
    }
    const uint32_t elapsedUs = traceMicros() - commitStartUs;
    SC_REC_STOP_STAGE(stage, elapsedUs, durationUs, heapBefore, heapAfter,
                      stopStageEventCount, stopStageChunkRefCount, outcome);
  };

  if (capture.store.empty()) {
    const uint32_t heap = MemoryMonitor::getInternalHeapFreeBytes();
    emitStage("seal", 0, heap, heap, "skipped_empty");
    emitStage("publish", 0, heap, heap, "not_run");
    discardCapture();
    return CommitResult::Skipped;
  }

  const uint32_t sealHeapBefore = MemoryMonitor::getInternalHeapFreeBytes();
  const uint32_t sealStartUs = traceMicros();
  const SealOutcome seal = sealCapture(sealedAtTick);
  const uint32_t sealDurationUs = traceMicros() - sealStartUs;
  const uint32_t sealHeapAfter = MemoryMonitor::getInternalHeapFreeBytes();
  emitStage("seal", sealDurationUs, sealHeapBefore, sealHeapAfter, sealOutcomeLabel(seal));
  if (seal != SealOutcome::Ok) {
    emitStage("publish", 0, sealHeapAfter, sealHeapAfter, "not_run");
    return CommitResult::SealFailed;
  }

  const uint32_t publishHeapBefore = MemoryMonitor::getInternalHeapFreeBytes();
  const uint32_t publishStartUs = traceMicros();
  if (!publishPendingCapturePass()) {
    const uint32_t publishDurationUs = traceMicros() - publishStartUs;
    const uint32_t publishHeapAfter = MemoryMonitor::getInternalHeapFreeBytes();
    emitStage("publish", publishDurationUs, publishHeapBefore, publishHeapAfter, "failed");
    return CommitResult::SealFailed;
  }
  const uint32_t publishDurationUs = traceMicros() - publishStartUs;
  const uint32_t publishHeapAfter = MemoryMonitor::getInternalHeapFreeBytes();
  emitStage("publish", publishDurationUs, publishHeapBefore, publishHeapAfter, "ok");

  markPassDerivedStale();
  return CommitResult::Published;
}

size_t Loop::activeCapturePassCount() const {
  size_t count = 0;
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Active) {
    ++count;
  }
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.state == CapturePassState::Active) {
      ++count;
    }
  }
  return count;
}

void Loop::discardPendingCapturePass() {
  if (!hasPendingCapturePass_) {
    return;
  }
  LoopEventStore staging;
  staging.adoptChunkIds(pendingCapturePass_.chunkRefs);
  staging.clear();
  pendingCapturePass_ = PendingCapturePass{};
  hasPendingCapturePass_ = false;
  pendingVisualDelta.clear();
}

namespace {

bool hasActiveEditPasses(const LoopPasses& passes) {
  for (const EditPass& editPass : passes.editPasses) {
    if (editPass.state == EditPassState::Active) {
      return true;
    }
  }
  return false;
}

// Phase B: one materialize per playbackRevision — display reconstruct reads seeded flat when
// fresh; chunk-ref merge only when store has not been seeded yet (sync callers before idle).
template <typename MidiEventVector>
void gatherPublishedFlatForDerivedView(const Loop& loop, MidiEventVector& flat) {
  if (hasActiveEditPasses(loop.passes)) {
    const SessionMidiEventVec& materialized = loop.midiEvents();
    flat.assign(materialized.begin(), materialized.end());
    return;
  }
  if (loop.isPassesMaterializedStoreFresh()) {
    loop.passes.materializeToEventVector(flat, loop.loopLengthTicks);
    return;
  }
  loop.mergeActiveCapturePasses(flat);
}

// PLAYING idle slices: materialize to extmem when published store is fresh.
template <typename MidiEventVector>
void gatherChunkFlatForDisplaySlice(const Loop& loop, MidiEventVector& flat) {
  if (hasActiveEditPasses(loop.passes)) {
    const SessionMidiEventVec& materialized = loop.midiEvents();
    flat.assign(materialized.begin(), materialized.end());
    return;
  }
  if (loop.isPassesMaterializedStoreFresh()) {
    loop.passes.materializeToEventVector(flat, loop.loopLengthTicks);
    return;
  }
  loop.mergeActiveCapturePasses(flat);
}

uint32_t totalVisualBarsForLoop(uint32_t loopLengthTicks) {
  if (loopLengthTicks == 0) {
    return 0;
  }
  return (loopLengthTicks + Config::TICKS_PER_BAR - 1) / Config::TICKS_PER_BAR;
}

void markAllVisualCacheBarsDirty(VisualCache& cache, uint32_t loopLengthTicks) {
  const uint32_t totalBars = totalVisualBarsForLoop(loopLengthTicks);
  if (totalBars == 0) {
    cache.dirtyBars.clear();
    return;
  }
  cache.dirtyBars.assign(totalBars, 1);
}

uint32_t findNextDirtyBar(const VisualBarVec& dirtyBars, uint32_t priorityBar) {
  if (dirtyBars.empty()) {
    return UINT32_MAX;
  }
  const uint32_t totalBars = static_cast<uint32_t>(dirtyBars.size());
  const uint32_t start = priorityBar < totalBars ? priorityBar : 0;
  for (uint32_t offset = 0; offset < totalBars; ++offset) {
    const uint32_t bar = (start + offset) % totalBars;
    if (dirtyBars[bar] != 0) {
      return bar;
    }
  }
  return UINT32_MAX;
}

void removeDisplayNotesOverlappingBars(DisplayNoteVec& notes, uint32_t startBar, uint32_t endBar) {
  notes.erase(std::remove_if(notes.begin(), notes.end(),
                             [&](const NoteUtils::DisplayNote& note) {
                               const uint32_t endTick =
                                   note.endTick >= note.startTick ? note.endTick : note.startTick;
                               const uint32_t noteStartBar =
                                   visualBarForTick(note.startTick, Config::TICKS_PER_BAR);
                               const uint32_t noteEndBar =
                                   visualBarForTick(endTick, Config::TICKS_PER_BAR);
                               return noteStartBar <= endBar && noteEndBar >= startBar;
                             }),
                  notes.end());
}

template <typename MidiEventVector>
void filterMidiEventsToTickWindow(const MidiEventVector& events, MidiEventVector& out,
                                  uint32_t windowStart, uint32_t windowLength,
                                  uint32_t loopLength) {
  out.clear();
  if (events.empty() || loopLength == 0 || windowLength == 0) {
    return;
  }
  out.reserve(events.size());
  for (const MidiEvent& evt : events) {
    const uint32_t rel = IntervalProjection::tickPhaseInLoop(evt.tick, 0, loopLength);
    const uint32_t start = IntervalProjection::tickPhaseInLoop(windowStart, 0, loopLength);
    const uint32_t end =
        IntervalProjection::tickPhaseInLoop(start + windowLength, 0, loopLength);
    bool inWindow = false;
    if (windowLength >= loopLength) {
      inWindow = true;
    } else if (start < end) {
      inWindow = rel >= start && rel < end;
    } else {
      inWindow = rel >= start || rel < end;
    }
    if (inWindow) {
      out.push_back(evt);
    }
  }
}

}  // namespace

void Loop::rebuildVisualCacheIdleSlice(uint8_t maxBarsPerSlice, uint32_t priorityBar) {
  if (!visualCacheDirty || loopLengthTicks == 0 || maxBarsPerSlice == 0) {
    return;
  }

  const uint32_t totalBars = totalVisualBarsForLoop(loopLengthTicks);
  if (totalBars == 0) {
    visualCacheDirty = false;
    visualCache.dirtyBars.clear();
    return;
  }
  if (visualCache.dirtyBars.size() < totalBars) {
    markAllVisualCacheBarsDirty(visualCache, loopLengthTicks);
  }

  const uint32_t startBar = findNextDirtyBar(visualCache.dirtyBars, priorityBar);
  if (startBar == UINT32_MAX) {
    visualCacheDirty = false;
    visualCache.dirtyBars.clear();
    return;
  }

  const uint32_t barsThisSlice =
      std::min<uint32_t>(maxBarsPerSlice, totalBars - startBar);
  const uint32_t endBar = startBar + barsThisSlice - 1;

  constexpr uint32_t kPadBars = 1;
  const uint32_t eventStartBar = startBar > kPadBars ? startBar - kPadBars : 0;
  const uint32_t eventEndBar = std::min(endBar + kPadBars, totalBars - 1);
  const uint32_t windowStart = eventStartBar * Config::TICKS_PER_BAR;
  const uint32_t windowEndTick =
      std::min((eventEndBar + 1) * Config::TICKS_PER_BAR, loopLengthTicks);
  const uint32_t windowLength = windowEndTick > windowStart ? windowEndTick - windowStart : 0;
  if (windowLength == 0) {
    for (uint32_t bar = startBar; bar <= endBar; ++bar) {
      visualCache.dirtyBars[bar] = 0;
    }
    return;
  }

  SessionMidiEventVec flat;
  gatherChunkFlatForDisplaySlice(*this, flat);
  SessionMidiEventVec windowEvents;
  filterMidiEventsToTickWindow(flat, windowEvents, windowStart, windowLength, loopLengthTicks);
  const NoteUtils::DisplayNoteVec sliceNotes =
      NoteUtils::reconstructDisplayNotes(windowEvents, loopLengthTicks, false);

  removeDisplayNotesOverlappingBars(visualCache.notes, startBar, endBar);
  for (const NoteUtils::DisplayNote& note : sliceNotes) {
    const uint32_t endTick = note.endTick >= note.startTick ? note.endTick : note.startTick;
    const uint32_t noteStartBar = visualBarForTick(note.startTick, Config::TICKS_PER_BAR);
    const uint32_t noteEndBar = visualBarForTick(endTick, Config::TICKS_PER_BAR);
    if (noteStartBar <= endBar && noteEndBar >= startBar) {
      visualCache.notes.push_back(note);
    }
  }
  publishedMaterializedEventCount_ = flat.size();

  for (uint32_t bar = startBar; bar <= endBar; ++bar) {
    visualCache.dirtyBars[bar] = 0;
  }

  bool anyDirty = false;
  for (uint8_t flag : visualCache.dirtyBars) {
    if (flag != 0) {
      anyDirty = true;
      break;
    }
  }
  if (!anyDirty) {
    visualCacheDirty = false;
    visualCache.dirtyBars.clear();
    ++visualCache.revision;
  }
}

void Loop::rebuildVisualCacheFromPasses() {
  DIAG_COUNTER_INC(VisualCacheRebuild);
  SessionMidiEventVec flat;
  gatherPublishedFlatForDerivedView(*this, flat);
  publishedMaterializedEventCount_ = flat.size();
  const NoteUtils::DisplayNoteVec rebuiltNotes =
      NoteUtils::reconstructDisplayNotes(flat, loopLengthTicks, false);
  visualCache.notes.assign(rebuiltNotes.begin(), rebuiltNotes.end());
  visualCache.dirtyBars.clear();
  for (const auto& n : visualCache.notes) {
    const uint32_t endTick = n.endTick >= n.startTick ? n.endTick : n.startTick;
    const uint32_t startBar = visualBarForTick(n.startTick, Config::TICKS_PER_BAR);
    const uint32_t endBar = visualBarForTick(endTick, Config::TICKS_PER_BAR);
    for (uint32_t bar = startBar; bar <= endBar; ++bar) {
      visualCache.markBarDirty(bar);
    }
  }
  ++visualCache.revision;
  visualCacheDirty = false;
}

void Loop::ensureVisualCacheBuilt() {
  if (!visualCacheDirty) {
    return;
  }
  rebuildVisualCacheFromPasses();
}

void Loop::markDisplayCachesStale() {
  invalidatePlaybackCaches();
  visualCacheDirty = true;
  markAllVisualCacheBarsDirty(visualCache, loopLengthTicks);
}

void Loop::invalidateDisplayCaches() {
  if (noteCache_) {
    noteCache_->invalidate();
  }
  visualCacheDirty = true;
}

SealOutcome Loop::sealCapture(uint32_t sealedAtTick) {
  if (hasPendingCapturePass_) {
    return SealOutcome::AlreadyPending;
  }
  if (capture.store.empty()) {
    return SealOutcome::SkippedEmpty;
  }
  if (!LoopEventStore::canAllocChunkWithReserve()) {
    return SealOutcome::PoolExhausted;
  }

  ensureCaptureEventsSorted();
  assignMissingNoteIdsInStore(capture.store);

  const char* phaseLabel = capturePhaseLabel(capture.phase);
  uint32_t minLenPairsRemoved = 0;
  uint32_t wrapSyntheticOffs = 0;

  if (loopLengthTicks > 0 &&
      (capture.phase == CapturePhase::Record || capture.phase == CapturePhase::Overdub)) {
    const LoopStopFinalize::Result fin =
        LoopStopFinalize::finalizeWrapWindowOnStore(capture.store, loopLengthTicks);
    wrapSyntheticOffs = static_cast<uint32_t>(fin.syntheticOffsInserted);
    minLenPairsRemoved = static_cast<uint32_t>(
        CaptureIncrementalSanity::removePairsShorterThanNoteMinLength(
            capture.store, loopLengthTicks, noteMinLengthTicks, noteMinLengthRemoveEnabled));
    CaptureIncrementalSanity::verifyCaptureHotStop(capture.store, loopLengthTicks);
    if (capture.store.empty()) {
      return SealOutcome::FailedValidation;
    }
  }

  SC_CAPTURE_CLEANUP(phaseLabel, "dedup", captureDedupEventsDropped_);
  SC_CAPTURE_CLEANUP(phaseLabel, "minlen", minLenPairsRemoved);
  SC_CAPTURE_CLEANUP(phaseLabel, "wrap_synth", wrapSyntheticOffs);
  captureDedupEventsDropped_ = 0;

  const CapturePassPhase phase =
      effectiveCapturePassPhase(capture.phase, passes.hasRecordPass());

  pendingCapturePass_ = PendingCapturePass{};
  pendingCapturePass_.id = nextPassId_++;
  pendingCapturePass_.mergeSequence = nextMergeSequence_++;
  pendingCapturePass_.phase = phase;
  pendingCapturePass_.sealedAtTick = sealedAtTick;
  capture.store.detachChunksTo(pendingCapturePass_.chunkRefs);

  if (pendingCapturePass_.chunkRefs.empty()) {
    pendingCapturePass_ = PendingCapturePass{};
    return SealOutcome::FailedValidation;
  }

  pendingVisualDelta.clear();
  hasPendingCapturePass_ = true;
  return SealOutcome::Ok;
}

bool Loop::publishPendingCapturePass() {
  if (!hasPendingCapturePass_) {
    return false;
  }

  PendingCapturePass published = std::move(pendingCapturePass_);
  if (published.phase == CapturePassPhase::Record) {
    RecordPass record{};
    record.id = published.id;
    record.state = CapturePassState::Active;
    record.sealedAtTick = published.sealedAtTick;
    record.chunkRefs = std::move(published.chunkRefs);
    passes.recordPass = std::move(record);
  } else {
    OverdubPass overdub{};
    overdub.id = published.id;
    overdub.mergeSequence = published.mergeSequence;
    overdub.state = CapturePassState::Active;
    overdub.sealedAtTick = published.sealedAtTick;
    overdub.chunkRefs = std::move(published.chunkRefs);
    passes.overdubPasses.push_back(std::move(overdub));
  }
  lastPublishedPassId_ = published.id;

  pendingCapturePass_ = PendingCapturePass{};
  hasPendingCapturePass_ = false;

  ++playbackRevision;
  pendingVisualDelta.clear();

  capture.store.clear();
  capture.phase = CapturePhase::None;
  captureNextEventIndex = 0;
  captureEventsSortDirty = false;
  capturePreview.clear();

  return true;
}

void Loop::invalidateCaches() {
  if (noteCache_) {
    noteCache_->invalidate();
  }
  eventIndexValid = false;
  visualCacheDirty = true;
  playbackOrderDirty = true;
}

void Loop::invalidatePlaybackCaches() {
  playbackOrderDirty = true;
  passesMaterializedStore_.discardFlatCache();
  if (noteCache_) {
    noteCache_->invalidate();
  }
  eventIndexValid = false;
}

void Loop::clearCaptureOnNewPass() {
  discardCapture();
}
