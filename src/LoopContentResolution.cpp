//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// DEC-037 LoopContentResolution implementation.
// Native tests include this TU. Teensy links via linker/imxrt1062_t41_lcr.ld when referenced.

#include "LoopContentResolution.h"

#include "CommittedEventRange.h"
#include "EditApply.h"
#include "Globals.h"
#include "LoopEventStore.h"
#include "OverlapNoteIdObservation.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/IntervalProjection.h"
#include "Utils/TrackMem.h"

#if defined(ARDUINO)
#include <Arduino.h>
#endif

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

#if !defined(ARDUINO)
#include <chrono>
#endif

namespace {

struct ElapsedTimer {
#if defined(ARDUINO)
  uint32_t start = micros();
  uint64_t elapsed() const { return static_cast<uint64_t>(micros() - start); }
#else
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  uint64_t elapsed() const {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                     std::chrono::steady_clock::now() - start)
                                     .count());
  }
#endif
};

TRACK_COLD_MEM void mergeSortedMidiVectors(SessionMidiEventVec& base, SessionMidiEventVec&& addition) {
  if (addition.empty()) {
    return;
  }
  if (base.empty()) {
    base = std::move(addition);
    return;
  }
  SessionMidiEventVec merged;
  merged.reserve(base.size() + addition.size());
  uint32_t baseCursor = 0;
  uint32_t addCursor = 0;
  LoopContentResolution::TickIndex::mergeSortedMidiEventRange(
      base, baseCursor, addition, addCursor, merged,
      static_cast<uint32_t>(base.size() + addition.size()));
  base = std::move(merged);
}

TRACK_COLD_MEM void appendCapturePassLayer(const CommittedChunkIdList& chunks, bool wholeLoop,
                                           uint32_t loopLengthTicks, uint32_t windowStart,
                                           uint32_t windowLength, SessionMidiEventVec& layer) {
  layer.clear();
  if (chunks.empty()) {
    return;
  }
  if (wholeLoop) {
    LoopEventStore::appendChunkRefEvents(chunks, layer);
    return;
  }
  const CommittedChunkIdList* list = &chunks;
  CommittedEventRange::inWindow(&list, 1, loopLengthTicks, windowStart, windowLength)
      .appendTo(layer);
}

TRACK_COLD_MEM void applyActiveEdits(SessionMidiEventVec& events, const LoopPasses& passes,
                      uint32_t loopLengthTicks) {
  EditPassVec activeRows;
  for (const EditPass& editPass : passes.editPasses) {
    if (editPass.state == EditPassState::Active && editPass.passType == EditPassType::Note) {
      activeRows.push_back(editPass);
    }
  }
  if (!activeRows.empty()) {
    applyNoteEditPassSequence(events, activeRows, loopLengthTicks);
  }
}

TRACK_COLD_MEM void bumpOps(ResolutionCostCounters* counters, uint32_t amount) {
  if (counters != nullptr) {
    counters->resolutionOperations += amount;
  }
}

TRACK_COLD_MEM bool resolvedEventLess(const MidiEvent& a, const MidiEvent& b) {
  if (a.tick != b.tick) {
    return a.tick < b.tick;
  }
  if (a.type != b.type) {
    return static_cast<uint8_t>(a.type) < static_cast<uint8_t>(b.type);
  }
  if (a.channel != b.channel) {
    return a.channel < b.channel;
  }
  if (a.data.noteData.note != b.data.noteData.note) {
    return a.data.noteData.note < b.data.noteData.note;
  }
  return a.noteId < b.noteId;
}

TRACK_COLD_MEM void sortResolvedEvents(SessionMidiEventVec& events) {
  std::sort(events.begin(), events.end(), resolvedEventLess);
}

// appendNoteEvents can re-add a target already in the window. NoteId is ON-only
// (kInvalidNoteId on NoteOff), so same-tick same-pitch offs are not the same event.
TRACK_COLD_MEM bool sameIdentifiedResolvedEvent(const MidiEvent& a, const MidiEvent& b) {
  return a.noteId != kInvalidNoteId && a.noteId == b.noteId && a.tick == b.tick &&
         a.type == b.type && a.channel == b.channel &&
         a.data.noteData.note == b.data.noteData.note;
}

TRACK_COLD_MEM void uniqueIdentifiedResolvedEvents(SessionMidiEventVec& events) {
  events.erase(std::unique(events.begin(), events.end(), sameIdentifiedResolvedEvent),
               events.end());
}

TRACK_COLD_MEM bool workingHasNoteOnId(const SessionMidiEventVec& events, NoteId noteId) {
  if (noteId == kInvalidNoteId) {
    return false;
  }
  for (const MidiEvent& event : events) {
    if (event.isNoteOn() && event.noteId == noteId) {
      return true;
    }
  }
  return false;
}

TRACK_COLD_MEM bool notePresentAt(const NoteUtils::DisplayNote& note, uint32_t tick, uint32_t loopLengthTicks) {
  if (NoteUtils::isWrappedLoopNotePair(note.startTick, note.endTick, loopLengthTicks)) {
    return tick >= note.startTick || tick < note.endTick;
  }
  return tick >= note.startTick && tick < note.endTick;
}

TRACK_COLD_MEM uint8_t channelForNoteId(const SessionMidiEventVec& events, NoteId noteId) {
  for (const MidiEvent& event : events) {
    if (event.isNoteOn() && event.noteId == noteId) {
      return event.channel;
    }
  }
  return 0;
}

TRACK_COLD_MEM void upsertPresentNote(PresentNoteVec& presentNotes, const PresentNote& note) {
  for (PresentNote& existing : presentNotes) {
    if (note.noteId != kInvalidNoteId && existing.noteId == note.noteId) {
      existing = note;
      return;
    }
  }
  presentNotes.push_back(note);
}

TRACK_COLD_MEM void erasePresentNote(PresentNoteVec& presentNotes, const MidiEvent& off) {
  presentNotes.erase(std::remove_if(presentNotes.begin(), presentNotes.end(),
                                    [&](const PresentNote& note) {
                                      if (off.noteId != kInvalidNoteId) {
                                        return note.noteId == off.noteId;
                                      }
                                      return note.channel == off.channel &&
                                             note.pitch == off.data.noteData.note;
                                    }),
                     presentNotes.end());
}

TRACK_COLD_MEM void applySpanBoundaryAtTick(
    PresentNoteVec& presentNotes, const LoopContentResolution::StateCheckpoints::NoteSpan& span,
    uint32_t keyTick) {
  if (keyTick == span.startTick) {
    upsertPresentNote(presentNotes, span.note);
  }
  if (keyTick == span.endTick) {
    MidiEvent off = MidiEvent::NoteOff(span.endTick, span.note.channel, span.note.pitch, 0);
    off.noteId = span.note.noteId;
    erasePresentNote(presentNotes, off);
  }
}

// Apply note edits on each capture pass, then merge — same order as
// LoopPasses::materializeToEventVector. Tick-sort is only applied for resolveWindow's
// deterministic ResolvedEvent sequence (reconstruct is order-sensitive).
TRACK_COLD_MEM void gatherActiveResolvedEvents(const LoopPasses& passes, uint32_t loopLengthTicks,
                                uint32_t windowStart, uint32_t windowLength,
                                SessionMidiEventVec& out, ResolutionCostCounters* counters) {
  out.clear();
  if (loopLengthTicks == 0 || windowLength == 0) {
    return;
  }

  const bool wholeLoop = windowLength >= loopLengthTicks;
  uint32_t layerCount = 0;
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Active &&
      !passes.recordPass.committedChunkIds.empty()) {
    SessionMidiEventVec layer;
    appendCapturePassLayer(passes.recordPass.committedChunkIds, wholeLoop, loopLengthTicks,
                           windowStart, windowLength, layer);
    applyActiveEdits(layer, passes, loopLengthTicks);
    mergeSortedMidiVectors(out, std::move(layer));
    layerCount += 1;
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
    SessionMidiEventVec layer;
    appendCapturePassLayer(pass->committedChunkIds, wholeLoop, loopLengthTicks, windowStart,
                           windowLength, layer);
    applyActiveEdits(layer, passes, loopLengthTicks);
    mergeSortedMidiVectors(out, std::move(layer));
    layerCount += 1;
  }
  bumpOps(counters, static_cast<uint32_t>(out.size() + layerCount));
}

}  // namespace

struct EventRef {
  PassId passId = kInvalidPassId;
  uint32_t eventIndex = 0;
  bool operator<(const EventRef& other) const {
    return passId < other.passId || (passId == other.passId && eventIndex < other.eventIndex);
  }
};

struct PreparedCompanion {
  EditPassId id = kInvalidEditPassId;
  EditPassState state = EditPassState::Active;
  PresentNote note{};
  uint32_t startTick = 0;
  uint32_t endTick = 0;
};
using PreparedCompanionVec =
    std::vector<PreparedCompanion, ExternalMemoryFirstAllocator<PreparedCompanion>>;

TRACK_COLD_MEM void pairNotesInPassRange(LoopContentResolution::TickIndex::CapturePassEntry& pass,
                                         LoopContentResolution::TickIndex& index,
                                         uint32_t beginEvent, uint32_t endEventExclusive,
                                         std::map<uint8_t, std::vector<uint32_t>>& openOnByPitch,
                                         ResolutionCostCounters* counters) {
  const uint32_t limit = static_cast<uint32_t>(pass.events.size());
  if (beginEvent >= limit) {
    return;
  }
  if (endEventExclusive > limit) {
    endEventExclusive = limit;
  }
  const uint32_t remaining = limit - beginEvent;
  if (index.byNoteId.capacity() < index.byNoteId.size() + remaining) {
    index.byNoteId.reserve(index.byNoteId.size() + remaining);
  }
  ElapsedTimer totalTimer;
  uint64_t byNoteIdMicros = 0;
  uint64_t openOnMicros = 0;
  uint64_t lookupMicros = 0;
  for (uint32_t i = beginEvent; i < endEventExclusive; ++i) {
    const MidiEvent& event = pass.events[i];
    if (event.isNoteOn()) {
      ElapsedTimer openTimer;
      const size_t keysBefore = openOnByPitch.size();
      std::vector<uint32_t>& stack = openOnByPitch[event.data.noteData.note];
      const size_t capBefore = stack.capacity();
      stack.push_back(i);
      openOnMicros += openTimer.elapsed();
      if (counters != nullptr) {
        counters->pairOpenOnPushes += 1;
        if (openOnByPitch.size() > keysBefore) {
          counters->pairOpenOnAllocations += 1;
        }
        if (stack.capacity() > capBefore) {
          counters->pairOpenOnAllocations += 1;
          counters->pairOpenOnHeapBytes +=
              (stack.capacity() - capBefore) * sizeof(uint32_t);
        }
        if (stack.size() > counters->pairOpenOnPeakDepth) {
          counters->pairOpenOnPeakDepth = static_cast<uint32_t>(stack.size());
        }
      }
      if (event.noteId != kInvalidNoteId) {
        ElapsedTimer byNoteTimer;
        LoopContentResolution::TickIndex::appendByNoteIdEntry(index.byNoteId, event.noteId, pass.id,
                                                              i);
        index.byNoteIdSorted = false;
        byNoteIdMicros += byNoteTimer.elapsed();
        if (counters != nullptr) {
          counters->pairByNoteIdInserts += 1;
        }
      }
      continue;
    }
    if (!event.isNoteOff()) {
      continue;
    }
    ElapsedTimer openTimer;
    const size_t keysBefore = openOnByPitch.size();
    std::vector<uint32_t>& stack = openOnByPitch[event.data.noteData.note];
    if (counters != nullptr && openOnByPitch.size() > keysBefore) {
      counters->pairOpenOnAllocations += 1;
    }
    if (stack.empty()) {
      openOnMicros += openTimer.elapsed();
      continue;
    }
    const uint32_t onIndex = stack.back();
    stack.pop_back();
    openOnMicros += openTimer.elapsed();
    if (counters != nullptr) {
      counters->pairOpenOnPops += 1;
    }
    const NoteId noteId = pass.events[onIndex].noteId;
    if (noteId == kInvalidNoteId) {
      continue;
    }
    ElapsedTimer lookupTimer;
    LoopContentResolution::TickIndex::ByNoteIdEntry* found =
        LoopContentResolution::TickIndex::findByNoteIdEntryMutable(index.byNoteId, noteId);
    lookupMicros += lookupTimer.elapsed();
    if (counters != nullptr) {
      counters->pairByNoteIdLookups += 1;
    }
    if (found != nullptr && found->loc.passId == pass.id) {
      found->loc.offIndex = static_cast<int32_t>(i);
    }
  }
  if (counters == nullptr) {
    return;
  }
  const uint64_t total = totalTimer.elapsed();
  const uint64_t accounted = byNoteIdMicros + openOnMicros + lookupMicros;
  counters->pairTotalMicros += total;
  counters->pairByNoteIdMicros += byNoteIdMicros;
  counters->pairOpenOnByPitchMicros += openOnMicros;
  counters->pairLookupMicros += lookupMicros;
  if (total > accounted) {
    counters->pairOtherMicros += total - accounted;
  }
  counters->pairByNoteIdEntries = static_cast<uint32_t>(index.byNoteId.size());
  counters->pairOpenOnPitchKeys = static_cast<uint32_t>(openOnByPitch.size());
}

TRACK_COLD_MEM const LoopContentResolution::TickIndex::CapturePassEntry* findPass(
    const LoopContentResolution::TickIndex& index, PassId id) {
  const auto found = index.passById.find(id);
  if (found == index.passById.end() || found->second >= index.capturePasses.size()) {
    return nullptr;
  }
  return &index.capturePasses[found->second];
}

TRACK_COLD_MEM LoopContentResolution::TickIndex::CapturePassEntry* findPassMutable(
    LoopContentResolution::TickIndex& index, PassId id) {
  const auto found = index.passById.find(id);
  if (found == index.passById.end() || found->second >= index.capturePasses.size()) {
    return nullptr;
  }
  return &index.capturePasses[found->second];
}

TRACK_COLD_MEM void collectTickRangeRefs(const LoopContentResolution::TickIndex& index, PassId passId,
                          uint32_t eventIndex, std::set<EventRef>& refs,
                          ResolutionCostCounters* counters) {
  if (counters != nullptr) {
    counters->indexEntriesVisited += 1;
  }
  const auto* pass = findPass(index, passId);
  if (pass == nullptr || pass->state != CapturePassState::Active) {
    return;
  }
  EventRef ref;
  ref.passId = passId;
  ref.eventIndex = eventIndex;
  refs.insert(ref);
}

TRACK_COLD_MEM void visitTickEventRange(const LoopContentResolution::TickIndex& index,
                         const LoopContentResolution::TickIndex::TickEventEntryVec& entries,
                         uint32_t beginTick, uint32_t endTickExclusive, std::set<EventRef>& refs,
                         ResolutionCostCounters* counters) {
  const auto lessTick = [](const LoopContentResolution::TickIndex::TickEventEntry& entry,
                           uint32_t tick) { return entry.tick < tick; };
  auto it = std::lower_bound(entries.begin(), entries.end(), beginTick, lessTick);
  const auto stop = std::lower_bound(entries.begin(), entries.end(), endTickExclusive, lessTick);
  for (; it != stop; ++it) {
    collectTickRangeRefs(index, it->passId, it->eventIndex, refs, counters);
  }
}

TRACK_COLD_MEM void visitTickEventWindow(const LoopContentResolution::TickIndex& index,
                          const LoopContentResolution::TickIndex::TickEventEntryVec& entries,
                          uint32_t loopLengthTicks, uint32_t windowStart, uint32_t windowLength,
                          std::set<EventRef>& refs, ResolutionCostCounters* counters) {
  if (windowLength >= loopLengthTicks) {
    visitTickEventRange(index, entries, 0, loopLengthTicks, refs, counters);
    return;
  }
  const uint32_t start = IntervalProjection::tickPhaseInLoop(windowStart, 0, loopLengthTicks);
  if (start + windowLength <= loopLengthTicks) {
    visitTickEventRange(index, entries, start, start + windowLength, refs, counters);
    return;
  }
  visitTickEventRange(index, entries, start, loopLengthTicks, refs, counters);
  visitTickEventRange(index, entries, 0, start + windowLength - loopLengthTicks, refs, counters);
}

TRACK_COLD_MEM void emitTickEventRefs(const LoopContentResolution::TickIndex& index,
                       const std::set<EventRef>& refs, SessionMidiEventVec& out) {
  out.reserve(refs.size());
  for (const EventRef& ref : refs) {
    const auto* pass = findPass(index, ref.passId);
    if (pass == nullptr || ref.eventIndex >= pass->events.size()) {
      continue;
    }
    out.push_back(pass->events[ref.eventIndex]);
  }
}

TRACK_COLD_MEM void collectActiveEditRows(const EditPassVec& editPasses, EditPassVec& activeRows) {
  activeRows.clear();
  for (const EditPass& editPass : editPasses) {
    if (editPass.state == EditPassState::Active && editPass.passType == EditPassType::Note) {
      activeRows.push_back(editPass);
    }
  }
}

// Apply edits on each capture pass, then merge. Delete of a shorter same-start Add must
// not LIFO-pair a later pass's Off (003204).
TRACK_COLD_MEM void emitApplyEditsPerCapturePass(const LoopContentResolution::TickIndex& index,
                                                 const std::set<EventRef>& refs,
                                                 const EditPassVec& editPasses,
                                                 uint32_t loopLengthTicks,
                                                 SessionMidiEventVec& out) {
  out.clear();
  EditPassVec activeRows;
  collectActiveEditRows(editPasses, activeRows);

  std::vector<const LoopContentResolution::TickIndex::CapturePassEntry*> ordered;
  index.collectActiveMaterializePasses(ordered);
  for (const LoopContentResolution::TickIndex::CapturePassEntry* pass : ordered) {
    SessionMidiEventVec layer;
    for (const EventRef& ref : refs) {
      if (ref.passId != pass->id || ref.eventIndex >= pass->events.size()) {
        continue;
      }
      layer.push_back(pass->events[ref.eventIndex]);
    }
    for (const EditPass& editPass : activeRows) {
      if (workingHasNoteOnId(layer, editPass.targetNoteId)) {
        continue;
      }
      const auto* found = index.findByNoteId(editPass.targetNoteId);
      if (found != nullptr && found->loc.passId == pass->id) {
        index.appendNoteEvents(editPass.targetNoteId, layer);
      }
    }
    sortResolvedEvents(layer);
    uniqueIdentifiedResolvedEvents(layer);
    if (!activeRows.empty()) {
      applyNoteEditPassSequence(layer, activeRows, loopLengthTicks);
    }
    mergeSortedMidiVectors(out, std::move(layer));
  }
}

TRACK_COLD_MEM void LoopContentResolution::TickIndex::beginCapturePass(PassId id, CapturePassState state,
                                                                      uint32_t mergeSequence) {
  if (id == kInvalidPassId) {
    return;
  }
  CapturePassEntry pass;
  pass.id = id;
  pass.mergeSequence = mergeSequence;
  pass.state = state;
  passById[id] = capturePasses.size();
  capturePasses.push_back(std::move(pass));
}

TRACK_COLD_MEM void LoopContentResolution::TickIndex::appendCapturePassChunk(PassId id, uint16_t chunkId) {
  CapturePassEntry* pass = findPassMutable(*this, id);
  if (pass == nullptr) {
    return;
  }
  LoopEventStore::appendChunkRefEvent(chunkId, pass->events);
}

TRACK_COLD_MEM void LoopContentResolution::TickIndex::indexCapturePassEventRange(
    PassId id, uint32_t beginEvent, uint32_t endEventExclusive, ResolutionCostCounters* counters) {
  CapturePassEntry* pass = findPassMutable(*this, id);
  if (pass == nullptr) {
    return;
  }
  const uint32_t before = static_cast<uint32_t>(tickEvents.size());
  appendTickEventEntries(*pass, beginEvent, endEventExclusive, tickEvents);
  if (counters != nullptr) {
    const uint32_t added = static_cast<uint32_t>(tickEvents.size()) - before;
    if (added > 0) {
      counters->resolutionOperations += added;
    }
  }
}

TRACK_COLD_MEM void LoopContentResolution::TickIndex::pairCapturePassNotes(PassId id) {
  CapturePassEntry* pass = findPassMutable(*this, id);
  if (pass == nullptr) {
    return;
  }
  std::map<uint8_t, std::vector<uint32_t>> openOnByPitch;
  pairNotesInPassRange(*pass, *this, 0, static_cast<uint32_t>(pass->events.size()),
                       openOnByPitch, nullptr);
}

TRACK_COLD_MEM void LoopContentResolution::TickIndex::pairCapturePassEventRange(
    PassId id, uint32_t beginEvent, uint32_t endEventExclusive,
    std::map<uint8_t, std::vector<uint32_t>>& openOnByPitch, ResolutionCostCounters* counters) {
  CapturePassEntry* pass = findPassMutable(*this, id);
  if (pass == nullptr) {
    return;
  }
  const uint32_t before = beginEvent;
  pairNotesInPassRange(*pass, *this, beginEvent, endEventExclusive, openOnByPitch, counters);
  if (counters != nullptr) {
    const uint32_t limit = static_cast<uint32_t>(pass->events.size());
    uint32_t end = endEventExclusive;
    if (end > limit) {
      end = limit;
    }
    if (end > before) {
      counters->resolutionOperations += end - before;
    }
  }
}

TRACK_COLD_MEM void LoopContentResolution::TickIndex::commitCapturePass(PassId id, const CommittedChunkIdList& chunks,
                                                         CapturePassState state, uint32_t mergeSequence,
                                                         ResolutionCostCounters* counters) {
  if (id == kInvalidPassId) {
    return;
  }
  beginCapturePass(id, state, mergeSequence);
  for (uint16_t chunkId : chunks) {
    appendCapturePassChunk(id, chunkId);
  }
  const CapturePassEntry* pass = findPass(*this, id);
  const uint32_t eventCount = (pass != nullptr) ? static_cast<uint32_t>(pass->events.size()) : 0;
  if (counters != nullptr) {
    counters->passChunkListsWalked += 1;
    counters->resolutionOperations += eventCount;
  }
  indexCapturePassEventRange(id, 0, eventCount, nullptr);
  sortTickEventEntriesByTick(tickEvents);
  pairCapturePassNotes(id);
  sortAndUniqueByNoteId(counters);
}

TRACK_COLD_MEM void LoopContentResolution::TickIndex::commitLoopPasses(const LoopPasses& passes,
                                                        ResolutionCostCounters* counters) {
  if (passes.hasRecordPass() && !passes.recordPass.committedChunkIds.empty()) {
    commitCapturePass(passes.recordPass.id, passes.recordPass.committedChunkIds,
                      passes.recordPass.state, 0, counters);
  }
  for (const OverdubPass& pass : passes.overdubPasses) {
    commitCapturePass(pass.id, pass.committedChunkIds, pass.state, pass.mergeSequence, counters);
  }
}

TRACK_COLD_MEM void LoopContentResolution::TickIndex::setCapturePassState(PassId id, CapturePassState state) {
  const auto found = passById.find(id);
  if (found == passById.end() || found->second >= capturePasses.size()) {
    return;
  }
  capturePasses[found->second].state = state;
}

TRACK_COLD_MEM void LoopContentResolution::TickIndex::findRawWindow(uint32_t loopLengthTicks, uint32_t windowStart,
                                                     uint32_t windowLength, SessionMidiEventVec& out,
                                                     ResolutionCostCounters* counters) const {
  findRawWindowFromTickEvents(tickEvents, loopLengthTicks, windowStart, windowLength, out,
                              counters);
}

TRACK_COLD_MEM void LoopContentResolution::TickIndex::appendTickEventEntries(
    const CapturePassEntry& pass, uint32_t begin, uint32_t endExclusive, TickEventEntryVec& out) {
  const uint32_t limit = static_cast<uint32_t>(pass.events.size());
  if (begin >= limit) {
    return;
  }
  if (endExclusive > limit) {
    endExclusive = limit;
  }
  const uint32_t remaining = limit - begin;
  if (out.capacity() < out.size() + remaining) {
    out.reserve(out.size() + remaining);
  }
  for (uint32_t i = begin; i < endExclusive; ++i) {
    TickEventEntry entry;
    entry.tick = pass.events[i].tick;
    entry.passId = pass.id;
    entry.eventIndex = i;
    out.push_back(entry);
  }
}

TRACK_COLD_MEM void LoopContentResolution::TickIndex::sortTickEventEntriesByTick(TickEventEntryVec& entries) {
  std::stable_sort(entries.begin(), entries.end(),
                   [](const TickEventEntry& a, const TickEventEntry& b) { return a.tick < b.tick; });
}

TRACK_COLD_MEM void LoopContentResolution::TickIndex::appendByNoteIdEntry(ByNoteIdEntryVec& entries,
                                                                         NoteId noteId, PassId passId,
                                                                         uint32_t onIndex) {
  ByNoteIdEntry entry;
  entry.noteId = noteId;
  entry.loc.passId = passId;
  entry.loc.onIndex = onIndex;
  entry.loc.offIndex = -1;
  entries.push_back(entry);
}

TRACK_COLD_MEM void LoopContentResolution::TickIndex::sortAndUniqueByNoteIdEntries(
    ByNoteIdEntryVec& entries) {
  std::stable_sort(entries.begin(), entries.end(),
                   [](const ByNoteIdEntry& a, const ByNoteIdEntry& b) {
                     return a.noteId < b.noteId;
                   });
  size_t out = 0;
  size_t i = 0;
  const size_t n = entries.size();
  while (i < n) {
    size_t j = i + 1;
    while (j < n && entries[j].noteId == entries[i].noteId) {
      j += 1;
    }
    entries[out] = entries[j - 1];
    out += 1;
    i = j;
  }
  entries.resize(out);
}

TRACK_COLD_MEM const LoopContentResolution::TickIndex::ByNoteIdEntry*
LoopContentResolution::TickIndex::findByNoteIdEntry(const ByNoteIdEntryVec& entries, NoteId noteId,
                                                    bool sorted) {
  if (noteId == kInvalidNoteId || entries.empty()) {
    return nullptr;
  }
  if (sorted) {
    auto it = std::lower_bound(entries.begin(), entries.end(), noteId,
                               [](const ByNoteIdEntry& entry, NoteId id) {
                                 return entry.noteId < id;
                               });
    if (it != entries.end() && it->noteId == noteId) {
      return &*it;
    }
    return nullptr;
  }
  for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
    if (it->noteId == noteId) {
      return &*it;
    }
  }
  return nullptr;
}

TRACK_COLD_MEM LoopContentResolution::TickIndex::ByNoteIdEntry*
LoopContentResolution::TickIndex::findByNoteIdEntryMutable(ByNoteIdEntryVec& entries, NoteId noteId) {
  return const_cast<ByNoteIdEntry*>(findByNoteIdEntry(entries, noteId, false));
}

TRACK_COLD_MEM void LoopContentResolution::TickIndex::sortAndUniqueByNoteId(
    ResolutionCostCounters* counters) {
  ElapsedTimer sortTimer;
  const size_t before = byNoteId.size();
  sortAndUniqueByNoteIdEntries(byNoteId);
  byNoteIdSorted = true;
  if (counters != nullptr) {
    counters->pairByNoteIdSortMicros += sortTimer.elapsed();
    const size_t after = byNoteId.size();
    if (before > after) {
      counters->pairByNoteIdOverwrites += static_cast<uint32_t>(before - after);
    }
    counters->pairByNoteIdEntries = static_cast<uint32_t>(after);
  }
}

TRACK_COLD_MEM void LoopContentResolution::TickIndex::findRawWindowFromTickEvents(
    const TickEventEntryVec& entries, uint32_t loopLengthTicks, uint32_t windowStart,
    uint32_t windowLength, SessionMidiEventVec& out, ResolutionCostCounters* counters) const {
  out.clear();
  if (loopLengthTicks == 0 || windowLength == 0) {
    return;
  }
  std::set<EventRef> refs;
  visitTickEventWindow(*this, entries, loopLengthTicks, windowStart, windowLength, refs, counters);
  emitTickEventRefs(*this, refs, out);
}

TRACK_COLD_MEM void LoopContentResolution::TickIndex::findRawWindowFromTickEvents(
    const TickEventEntryVec& history, const TickEventEntryVec& delta, uint32_t loopLengthTicks,
    uint32_t windowStart, uint32_t windowLength, SessionMidiEventVec& out,
    ResolutionCostCounters* counters) const {
  out.clear();
  if (loopLengthTicks == 0 || windowLength == 0) {
    return;
  }
  std::set<EventRef> refs;
  const uint32_t beforeHistory = (counters != nullptr) ? counters->indexEntriesVisited : 0;
  visitTickEventWindow(*this, history, loopLengthTicks, windowStart, windowLength, refs, counters);
  if (counters != nullptr) {
    counters->indexHistoryEntriesVisited = counters->indexEntriesVisited - beforeHistory;
  }
  const uint32_t beforeDelta = (counters != nullptr) ? counters->indexEntriesVisited : 0;
  visitTickEventWindow(*this, delta, loopLengthTicks, windowStart, windowLength, refs, counters);
  if (counters != nullptr) {
    counters->indexDeltaEntriesVisited = counters->indexEntriesVisited - beforeDelta;
  }
  emitTickEventRefs(*this, refs, out);
}

TRACK_COLD_MEM void LoopContentResolution::TickIndex::appendNoteEvents(NoteId noteId, SessionMidiEventVec& out) const {
  if (noteId == kInvalidNoteId) {
    return;
  }
  const ByNoteIdEntry* found = findByNoteId(noteId);
  if (found == nullptr) {
    return;
  }
  const auto* pass = findPass(*this, found->loc.passId);
  if (pass == nullptr || pass->state != CapturePassState::Active) {
    return;
  }
  if (found->loc.onIndex < pass->events.size()) {
    out.push_back(pass->events[found->loc.onIndex]);
  }
  if (found->loc.offIndex >= 0 &&
      static_cast<uint32_t>(found->loc.offIndex) < pass->events.size()) {
    out.push_back(pass->events[static_cast<uint32_t>(found->loc.offIndex)]);
  }
}

TRACK_COLD_MEM void LoopContentResolution::TickIndex::collectActiveMaterializePasses(
    std::vector<const CapturePassEntry*>& ordered) const {
  ordered.clear();
  const CapturePassEntry* record = nullptr;
  std::vector<const CapturePassEntry*> overdubs;
  overdubs.reserve(capturePasses.size());
  for (const CapturePassEntry& pass : capturePasses) {
    if (pass.state != CapturePassState::Active || pass.events.empty()) {
      continue;
    }
    if (pass.mergeSequence == 0 && record == nullptr) {
      record = &pass;
      continue;
    }
    overdubs.push_back(&pass);
  }
  std::sort(overdubs.begin(), overdubs.end(),
            [](const CapturePassEntry* a, const CapturePassEntry* b) {
              return a->mergeSequence < b->mergeSequence;
            });
  if (record != nullptr) {
    ordered.push_back(record);
  }
  ordered.insert(ordered.end(), overdubs.begin(), overdubs.end());
}

TRACK_COLD_MEM void LoopContentResolution::TickIndex::appendMaterializePassEvents(
    const CapturePassEntry& pass, uint32_t begin, uint32_t endExclusive,
    SessionMidiEventVec& out) const {
  const uint32_t limit = static_cast<uint32_t>(pass.events.size());
  if (begin >= limit) {
    return;
  }
  if (endExclusive > limit) {
    endExclusive = limit;
  }
  if (out.capacity() < pass.events.size()) {
    out.reserve(pass.events.size());
  }
  for (uint32_t i = begin; i < endExclusive; ++i) {
    out.push_back(pass.events[i]);
  }
}

TRACK_COLD_MEM uint32_t LoopContentResolution::TickIndex::mergeSortedMidiEventRange(
    const SessionMidiEventVec& base, uint32_t& baseCursor, const SessionMidiEventVec& addition,
    uint32_t& addCursor, SessionMidiEventVec& merged, uint32_t maxEvents) {
  uint32_t produced = 0;
  const uint32_t baseSize = static_cast<uint32_t>(base.size());
  const uint32_t addSize = static_cast<uint32_t>(addition.size());
  while (produced < maxEvents && (baseCursor < baseSize || addCursor < addSize)) {
    if (addCursor >= addSize ||
        (baseCursor < baseSize && !(addition[addCursor].tick < base[baseCursor].tick))) {
      merged.push_back(base[baseCursor]);
      baseCursor += 1;
    } else {
      merged.push_back(addition[addCursor]);
      addCursor += 1;
    }
    produced += 1;
  }
  return produced;
}

TRACK_COLD_MEM void LoopContentResolution::TickIndex::materializeActive(SessionMidiEventVec& out) const {
  out.clear();
  std::vector<const CapturePassEntry*> ordered;
  collectActiveMaterializePasses(ordered);
  if (ordered.empty()) {
    return;
  }
  out = ordered[0]->events;
  for (size_t i = 1; i < ordered.size(); ++i) {
    SessionMidiEventVec layer = ordered[i]->events;
    mergeSortedMidiVectors(out, std::move(layer));
  }
}

TRACK_COLD_MEM uint32_t LoopContentResolution::TickIndex::indexedEventCount() const {
  return static_cast<uint32_t>(tickEvents.size());
}

TRACK_COLD_MEM uint32_t LoopContentResolution::TickIndex::indexedPassCount() const {
  return static_cast<uint32_t>(capturePasses.size());
}

TRACK_COLD_MEM void finishResolveWindow(const LoopContentResolution::TickIndex& index,
                                        const std::set<EventRef>& refs,
                                        const EditPassVec& editPasses, uint32_t loopLengthTicks,
                                        uint32_t windowStart, uint32_t windowLength,
                                        SessionMidiEventVec& out, ResolutionCostCounters* counters,
                                        const ElapsedTimer& timer, uint32_t eventsInHistory) {
  SessionMidiEventVec working;
  emitApplyEditsPerCapturePass(index, refs, editPasses, loopLengthTicks, working);
  DisplayWindowUtils::filterMidiEventsToWindow(working, out, windowStart, windowLength,
                                               loopLengthTicks);
  sortResolvedEvents(out);
  if (counters != nullptr) {
    counters->candidateEvents = static_cast<uint32_t>(working.size());
    counters->eventsInQueryWindow = static_cast<uint32_t>(out.size());
    counters->eventsInHistory = eventsInHistory;
    counters->passesInHistory = index.indexedPassCount();
    counters->elapsedMicros = timer.elapsed();
  }
}

TRACK_COLD_MEM void LoopContentResolution::resolveWindow(const TickIndex& index, const EditPassVec& editPasses,
                                          uint32_t loopLengthTicks, uint32_t windowStart,
                                          uint32_t windowLength, SessionMidiEventVec& out,
                                          ResolutionCostCounters* counters) {
  ElapsedTimer timer;
  std::set<EventRef> refs;
  visitTickEventWindow(index, index.tickEvents, loopLengthTicks, windowStart, windowLength, refs,
                       counters);
  finishResolveWindow(index, refs, editPasses, loopLengthTicks, windowStart, windowLength, out,
                      counters, timer, index.indexedEventCount());
}

TRACK_COLD_MEM void LoopContentResolution::resolveWindow(const TickIndex& index,
                                          const TickIndex::TickEventEntryVec& tickEvents,
                                          const EditPassVec& editPasses, uint32_t loopLengthTicks,
                                          uint32_t windowStart, uint32_t windowLength,
                                          SessionMidiEventVec& out,
                                          ResolutionCostCounters* counters) {
  ElapsedTimer timer;
  std::set<EventRef> refs;
  visitTickEventWindow(index, tickEvents, loopLengthTicks, windowStart, windowLength, refs,
                       counters);
  finishResolveWindow(index, refs, editPasses, loopLengthTicks, windowStart, windowLength, out,
                      counters, timer, static_cast<uint32_t>(tickEvents.size()));
}

TRACK_COLD_MEM void LoopContentResolution::resolveWindow(const TickIndex& index,
                                          const TickIndex::TickEventEntryVec& history,
                                          const TickIndex::TickEventEntryVec& delta,
                                          const EditPassVec& editPasses, uint32_t loopLengthTicks,
                                          uint32_t windowStart, uint32_t windowLength,
                                          SessionMidiEventVec& out,
                                          ResolutionCostCounters* counters) {
  ElapsedTimer timer;
  std::set<EventRef> refs;
  const uint32_t beforeHistory = (counters != nullptr) ? counters->indexEntriesVisited : 0;
  visitTickEventWindow(index, history, loopLengthTicks, windowStart, windowLength, refs, counters);
  if (counters != nullptr) {
    counters->indexHistoryEntriesVisited = counters->indexEntriesVisited - beforeHistory;
  }
  const uint32_t beforeDelta = (counters != nullptr) ? counters->indexEntriesVisited : 0;
  visitTickEventWindow(index, delta, loopLengthTicks, windowStart, windowLength, refs, counters);
  if (counters != nullptr) {
    counters->indexDeltaEntriesVisited = counters->indexEntriesVisited - beforeDelta;
  }
  finishResolveWindow(index, refs, editPasses, loopLengthTicks, windowStart, windowLength, out,
                      counters, timer, static_cast<uint32_t>(history.size()));
}

TRACK_COLD_MEM void LoopContentResolution::resolveWindow(const LoopPasses& passes, uint32_t loopLengthTicks,
                                          uint32_t windowStart, uint32_t windowLength,
                                          SessionMidiEventVec& out,
                                          ResolutionCostCounters* counters) {
  ElapsedTimer timer;
  gatherActiveResolvedEvents(passes, loopLengthTicks, windowStart, windowLength, out, counters);
  sortResolvedEvents(out);
  if (counters != nullptr) {
    counters->eventsInQueryWindow = static_cast<uint32_t>(out.size());
    counters->candidateEvents = counters->eventsInQueryWindow;
    counters->elapsedMicros = timer.elapsed();
  }
}

TRACK_COLD_MEM void LoopContentResolution::resolveState(const LoopPasses& passes, uint32_t loopLengthTicks,
                                         uint32_t tick, PresentNoteVec& out,
                                         ResolutionCostCounters* counters) {
  out.clear();
  SessionMidiEventVec events;
  gatherActiveResolvedEvents(passes, loopLengthTicks, 0, loopLengthTicks, events, counters);
  const NoteUtils::DisplayNoteVec notes =
      NoteUtils::reconstructDisplayNotes(events, loopLengthTicks, false);
  bumpOps(counters, static_cast<uint32_t>(notes.size()));
  for (const NoteUtils::DisplayNote& note : notes) {
    if (!notePresentAt(note, tick, loopLengthTicks)) {
      continue;
    }
    PresentNote presentNote{};
    presentNote.channel = channelForNoteId(events, note.noteId);
    presentNote.pitch = note.note;
    presentNote.noteId = note.noteId;
    presentNote.onTick = note.startTick;
    out.push_back(presentNote);
  }
}

TRACK_COLD_MEM bool LoopContentResolution::StateCheckpoints::beginRebuildResolvedEvents(
    uint32_t loopLength, uint32_t checkpointIntervalTicks, SessionMidiEventVec& resolved) {
  intervalTicks = checkpointIntervalTicks;
  loopLengthTicks = loopLength;
  presentAt.clear();
  spans.clear();
  spanBoundaries.clear();
  channelByNoteId.clear();
  resolved.clear();
  return loopLength != 0 && checkpointIntervalTicks != 0;
}

TRACK_COLD_MEM bool LoopContentResolution::StateCheckpoints::prepareRebuildResolvedEvents(
    const TickIndex& index, const EditPassVec& editPasses, uint32_t loopLength,
    uint32_t checkpointIntervalTicks, SessionMidiEventVec& resolved,
    ResolutionCostCounters* counters) {
  if (!beginRebuildResolvedEvents(loopLength, checkpointIntervalTicks, resolved)) {
    return true;
  }
  index.materializeActive(resolved);
  EditPassVec activeRows;
  for (const EditPass& editPass : editPasses) {
    if (editPass.state == EditPassState::Active && editPass.passType == EditPassType::Note) {
      activeRows.push_back(editPass);
    }
  }
  if (!activeRows.empty()) {
    applyNoteEditPassSequence(resolved, activeRows, loopLength);
  }
  if (counters != nullptr) {
    counters->passChunkListsWalked = 0;
    counters->eventsInHistory = static_cast<uint32_t>(resolved.size());
  }
  return true;
}

TRACK_COLD_MEM bool LoopContentResolution::StateCheckpoints::appendSpansFromNotes(
    const SessionMidiEventVec& resolved, const NoteUtils::DisplayNoteVec& notes, uint32_t begin,
    uint32_t endExclusive, ResolutionCostCounters* counters) {
  if (loopLengthTicks == 0 || intervalTicks == 0) {
    return true;
  }
  const uint32_t limit = static_cast<uint32_t>(notes.size());
  if (begin >= limit) {
    return true;
  }
  if (endExclusive > limit) {
    endExclusive = limit;
  }
  if (spans.capacity() < notes.size()) {
    spans.reserve(notes.size());
  }
  const size_t boundaryNeed = notes.size() * 2u;
  if (spanBoundaries.capacity() < boundaryNeed) {
    spanBoundaries.reserve(boundaryNeed);
  }
  const uint32_t spanBegin = static_cast<uint32_t>(spans.size());
  for (uint32_t i = begin; i < endExclusive; ++i) {
    const NoteUtils::DisplayNote& note = notes[i];
    NoteSpan span{};
    span.note.channel = findChannelByNoteId(channelByNoteId, note.noteId);
    span.note.pitch = note.note;
    span.note.noteId = note.noteId;
    span.note.onTick = note.startTick;
    span.startTick = note.startTick;
    span.endTick = note.endTick;
    spans.push_back(span);
  }
  ElapsedTimer appendTimer;
  appendSpanBoundaryEntries(spans, spanBegin, static_cast<uint32_t>(spans.size()), spanBoundaries);
  if (counters != nullptr) {
    counters->spanBoundaryAppendMicros += appendTimer.elapsed();
    counters->eventsInHistory = static_cast<uint32_t>(resolved.size());
  }
  return true;
}

TRACK_COLD_MEM void LoopContentResolution::StateCheckpoints::sortSpanBoundaries(
    ResolutionCostCounters* counters) {
  ElapsedTimer sortTimer;
  sortSpanBoundaryEntriesByTick(spanBoundaries);
  if (counters != nullptr) {
    counters->spanBoundarySortMicros += sortTimer.elapsed();
  }
}

TRACK_COLD_MEM bool LoopContentResolution::StateCheckpoints::appendChannelByNoteIdRange(
    const SessionMidiEventVec& resolved, uint32_t begin, uint32_t endExclusive,
    ResolutionCostCounters* counters) {
  ElapsedTimer appendTimer;
  appendChannelByNoteIdEntries(resolved, begin, endExclusive, channelByNoteId);
  if (counters != nullptr) {
    counters->channelByNoteIdAppendMicros += appendTimer.elapsed();
    counters->eventsInHistory = static_cast<uint32_t>(resolved.size());
  }
  return true;
}

TRACK_COLD_MEM void LoopContentResolution::StateCheckpoints::sortChannelByNoteId(
    ResolutionCostCounters* counters) {
  ElapsedTimer sortTimer;
  sortAndUniqueChannelByNoteIdEntries(channelByNoteId);
  if (counters != nullptr) {
    counters->channelByNoteIdSortMicros += sortTimer.elapsed();
  }
}

TRACK_COLD_MEM bool LoopContentResolution::StateCheckpoints::finishRebuildSpansFromEvents(
    const SessionMidiEventVec& resolved, ResolutionCostCounters* counters) {
  spans.clear();
  spanBoundaries.clear();
  presentAt.clear();
  channelByNoteId.clear();
  if (loopLengthTicks == 0 || intervalTicks == 0) {
    return true;
  }
  if (!appendChannelByNoteIdRange(resolved, 0, static_cast<uint32_t>(resolved.size()), counters)) {
    return false;
  }
  sortChannelByNoteId(counters);
  const NoteUtils::DisplayNoteVec notes =
      NoteUtils::reconstructDisplayNotes(resolved, loopLengthTicks, false);
  if (!appendSpansFromNotes(resolved, notes, 0, static_cast<uint32_t>(notes.size()), counters)) {
    return false;
  }
  sortSpanBoundaries(counters);
  uint32_t count = loopLengthTicks / intervalTicks;
  if (count == 0) {
    count = 1;
  }
  presentAt.resize(count);
  if (counters != nullptr) {
    counters->checkpointIntervalTicks = intervalTicks;
    counters->checkpointCount = static_cast<uint32_t>(presentAt.size());
    counters->eventsInHistory = static_cast<uint32_t>(resolved.size());
  }
  return true;
}

TRACK_COLD_MEM void LoopContentResolution::StateCheckpoints::prepareRebuildSpans(
    const TickIndex& index, const EditPassVec& editPasses, uint32_t loopLength,
    uint32_t checkpointIntervalTicks, ResolutionCostCounters* counters) {
  SessionMidiEventVec resolved;
  if (!prepareRebuildResolvedEvents(index, editPasses, loopLength, checkpointIntervalTicks,
                                    resolved, counters)) {
    return;
  }
  (void)finishRebuildSpansFromEvents(resolved, counters);
}

TRACK_COLD_MEM bool LoopContentResolution::StateCheckpoints::fillCheckpointRange(
    uint32_t beginIndex, uint32_t endIndexExclusive, ResolutionCostCounters* counters) {
  if (intervalTicks == 0 || loopLengthTicks == 0 || presentAt.empty()) {
    return true;
  }
  if (endIndexExclusive > presentAt.size()) {
    endIndexExclusive = static_cast<uint32_t>(presentAt.size());
  }
  for (uint32_t i = beginIndex; i < endIndexExclusive; ++i) {
    const uint32_t checkpointTick = i * intervalTicks;
    for (const NoteSpan& span : spans) {
      NoteUtils::DisplayNote probe{};
      probe.noteId = span.note.noteId;
      probe.note = span.note.pitch;
      probe.startTick = span.startTick;
      probe.endTick = span.endTick;
      if (!notePresentAt(probe, checkpointTick, loopLengthTicks)) {
        continue;
      }
      presentAt[i].push_back(span.note);
    }
  }
  if (counters != nullptr) {
    counters->checkpointIntervalTicks = intervalTicks;
    counters->checkpointCount = static_cast<uint32_t>(presentAt.size());
    counters->eventsInHistory = static_cast<uint32_t>(spans.size());
  }
  return true;
}

TRACK_COLD_MEM void LoopContentResolution::StateCheckpoints::rebuild(const TickIndex& index,
                                                      const EditPassVec& editPasses,
                                                      uint32_t loopLength,
                                                      uint32_t checkpointIntervalTicks,
                                                      ResolutionCostCounters* counters) {
  prepareRebuildSpans(index, editPasses, loopLength, checkpointIntervalTicks, counters);
  fillCheckpointRange(0, static_cast<uint32_t>(presentAt.size()), counters);
}

TRACK_COLD_MEM bool seedResolveStateFromCheckpoint(
    const LoopContentResolution::StateCheckpoints& checkpoints, uint32_t tick, PresentNoteVec& out,
    uint32_t& replayStart) {
  out.clear();
  if (checkpoints.intervalTicks == 0 || checkpoints.loopLengthTicks == 0 ||
      checkpoints.presentAt.empty()) {
    return false;
  }
  const uint32_t queryTick =
      IntervalProjection::tickPhaseInLoop(tick, 0, checkpoints.loopLengthTicks);
  replayStart = (queryTick / checkpoints.intervalTicks) * checkpoints.intervalTicks;
  uint32_t checkpointIndex = replayStart / checkpoints.intervalTicks;
  if (checkpointIndex >= checkpoints.presentAt.size()) {
    checkpointIndex = static_cast<uint32_t>(checkpoints.presentAt.size() - 1);
    replayStart = checkpointIndex * checkpoints.intervalTicks;
  }
  out = checkpoints.presentAt[checkpointIndex];
  return true;
}

TRACK_COLD_MEM void writeResolveStateCounters(const LoopContentResolution::StateCheckpoints& checkpoints,
                                              uint32_t replayStart, uint32_t replayed,
                                              ResolutionCostCounters* counters) {
  if (counters == nullptr) {
    return;
  }
  counters->checkpointIntervalTicks = checkpoints.intervalTicks;
  counters->checkpointCount = static_cast<uint32_t>(checkpoints.presentAt.size());
  counters->replayStartTick = replayStart;
  counters->eventsReplayed = replayed;
  counters->eventsInHistory = static_cast<uint32_t>(checkpoints.spans.size());
  counters->passChunkListsWalked = 0;
}

TRACK_COLD_MEM void LoopContentResolution::StateCheckpoints::appendSpanBoundaryEntries(
    const NoteSpanVec& spans, uint32_t begin, uint32_t endExclusive, SpanBoundaryEntryVec& out) {
  const uint32_t limit = static_cast<uint32_t>(spans.size());
  if (begin >= limit) {
    return;
  }
  if (endExclusive > limit) {
    endExclusive = limit;
  }
  const size_t added = static_cast<size_t>(endExclusive - begin) * 2u;
  if (out.capacity() < out.size() + added) {
    out.reserve(out.size() + added);
  }
  for (uint32_t i = begin; i < endExclusive; ++i) {
    out.push_back({spans[i].startTick, i});
    out.push_back({spans[i].endTick, i});
  }
}

TRACK_COLD_MEM void LoopContentResolution::StateCheckpoints::sortSpanBoundaryEntriesByTick(
    SpanBoundaryEntryVec& entries) {
  std::stable_sort(entries.begin(), entries.end(),
                   [](const SpanBoundaryEntry& a, const SpanBoundaryEntry& b) {
                     return a.tick < b.tick;
                   });
}

TRACK_COLD_MEM void mergeSortedSpanBoundaryEntries(
    const LoopContentResolution::StateCheckpoints::SpanBoundaryEntryVec& left,
    const LoopContentResolution::StateCheckpoints::SpanBoundaryEntryVec& right,
    LoopContentResolution::StateCheckpoints::SpanBoundaryEntryVec& out) {
  out.clear();
  out.resize(left.size() + right.size());
  std::merge(left.begin(), left.end(), right.begin(), right.end(), out.begin(),
             [](const LoopContentResolution::StateCheckpoints::SpanBoundaryEntry& a,
                const LoopContentResolution::StateCheckpoints::SpanBoundaryEntry& b) {
               return a.tick < b.tick;
             });
}

TRACK_COLD_MEM void removeSpanBoundaries(
    LoopContentResolution::StateCheckpoints::SpanBoundaryEntryVec& entries, size_t spanIndex) {
  entries.erase(std::remove_if(entries.begin(), entries.end(),
                               [spanIndex](const LoopContentResolution::StateCheckpoints::SpanBoundaryEntry&
                                               entry) { return entry.spanIndex == spanIndex; }),
                entries.end());
}

TRACK_COLD_MEM void insertSpanBoundarySorted(
    LoopContentResolution::StateCheckpoints::SpanBoundaryEntryVec& entries, uint32_t tick,
    size_t spanIndex) {
  LoopContentResolution::StateCheckpoints::SpanBoundaryEntry entry;
  entry.tick = tick;
  entry.spanIndex = spanIndex;
  auto it = std::lower_bound(entries.begin(), entries.end(), tick,
                             [](const LoopContentResolution::StateCheckpoints::SpanBoundaryEntry& existing,
                                uint32_t bound) { return existing.tick < bound; });
  entries.insert(it, entry);
}

TRACK_COLD_MEM void erasePresentNoteId(PresentNoteVec& presentNotes, NoteId noteId) {
  if (noteId == kInvalidNoteId) {
    return;
  }
  presentNotes.erase(std::remove_if(presentNotes.begin(), presentNotes.end(),
                                    [noteId](const PresentNote& note) { return note.noteId == noteId; }),
                     presentNotes.end());
}

TRACK_COLD_MEM void refreshPresentAtForSpan(LoopContentResolution::StateCheckpoints& checkpoints,
                                            size_t spanIndex) {
  if (spanIndex >= checkpoints.spans.size() || checkpoints.intervalTicks == 0) {
    return;
  }
  const LoopContentResolution::StateCheckpoints::NoteSpan& span = checkpoints.spans[spanIndex];
  NoteUtils::DisplayNote probe{};
  probe.noteId = span.note.noteId;
  probe.note = span.note.pitch;
  probe.startTick = span.startTick;
  probe.endTick = span.endTick;
  for (uint32_t c = 0; c < static_cast<uint32_t>(checkpoints.presentAt.size()); ++c) {
    const bool nowPresent =
        notePresentAt(probe, c * checkpoints.intervalTicks, checkpoints.loopLengthTicks);
    bool wasPresent = false;
    for (const PresentNote& note : checkpoints.presentAt[c]) {
      if (note.noteId == span.note.noteId) {
        wasPresent = true;
        break;
      }
    }
    if (wasPresent && !nowPresent) {
      erasePresentNoteId(checkpoints.presentAt[c], span.note.noteId);
    } else if (!wasPresent && nowPresent) {
      checkpoints.presentAt[c].push_back(span.note);
    }
  }
}

TRACK_COLD_MEM const EditPass* findEditPassById(const EditPassVec& editPasses, EditPassId id) {
  if (id == kInvalidEditPassId) {
    return nullptr;
  }
  for (const EditPass& row : editPasses) {
    if (row.id == id) {
      return &row;
    }
  }
  return nullptr;
}

TRACK_COLD_MEM void projectSealedCompanionsOntoCheckpoints(
    LoopContentResolution::StateCheckpoints& checkpoints, const EditPassVec& editPasses,
    const EditPassIdList& companionIds, PreparedCompanionVec& recorded) {
  for (const EditPassId id : companionIds) {
    const EditPass* row = findEditPassById(editPasses, id);
    if (row == nullptr || row->state != EditPassState::Active ||
        row->passType != EditPassType::Note) {
      continue;
    }
    const bool hide = row->actionType == EditActionType::Delete;
    const bool shorten =
        row->actionType == EditActionType::Update && row->propertyType == EditPropertyType::Length;
    if (!hide && !shorten) {
      continue;
    }
    bool recordedCompanion = false;
    // Wrap head+tail share a NoteId. First-match Hide left the tail sounding (030958).
    for (size_t spanIndex = 0; spanIndex < checkpoints.spans.size(); ++spanIndex) {
      LoopContentResolution::StateCheckpoints::NoteSpan& span = checkpoints.spans[spanIndex];
      if (span.note.noteId != row->targetNoteId) {
        continue;
      }
      if (!recordedCompanion) {
        PreparedCompanion recordedRow{};
        recordedRow.id = id;
        recordedRow.state = EditPassState::Active;
        recordedRow.note = span.note;
        recordedRow.startTick = span.startTick;
        recordedRow.endTick = span.endTick;
        recorded.push_back(recordedRow);
        recordedCompanion = true;
      }
      removeSpanBoundaries(checkpoints.spanBoundaries, spanIndex);
      if (hide) {
        span.endTick = span.startTick;
      } else {
        span.endTick = row->endTick;
        if (span.endTick != span.startTick) {
          insertSpanBoundarySorted(checkpoints.spanBoundaries, span.startTick, spanIndex);
          insertSpanBoundarySorted(checkpoints.spanBoundaries, span.endTick, spanIndex);
        }
      }
      refreshPresentAtForSpan(checkpoints, spanIndex);
    }
  }
}

TRACK_COLD_MEM void eraseDisabledSounding(const LoopContentResolution::TickIndex& index,
                                          PresentNoteVec& out) {
  auto isDisabled = [&](const PresentNote& note) {
    const LoopContentResolution::TickIndex::ByNoteIdEntry* found = index.findByNoteId(note.noteId);
    if (found == nullptr) {
      return false;
    }
    const LoopContentResolution::TickIndex::CapturePassEntry* pass = findPass(index, found->loc.passId);
    return pass != nullptr && pass->state != CapturePassState::Active;
  };
  out.erase(std::remove_if(out.begin(), out.end(), isDisabled), out.end());
}

TRACK_COLD_MEM void LoopContentResolution::StateCheckpoints::appendChannelByNoteIdEntries(
    const SessionMidiEventVec& resolved, uint32_t begin, uint32_t endExclusive,
    ChannelByNoteIdEntryVec& out) {
  const uint32_t limit = static_cast<uint32_t>(resolved.size());
  if (begin >= limit) {
    return;
  }
  if (endExclusive > limit) {
    endExclusive = limit;
  }
  const uint32_t remaining = limit - begin;
  if (out.capacity() < out.size() + remaining) {
    out.reserve(out.size() + remaining);
  }
  for (uint32_t i = begin; i < endExclusive; ++i) {
    const MidiEvent& event = resolved[i];
    if (!event.isNoteOn() || event.noteId == kInvalidNoteId) {
      continue;
    }
    ChannelByNoteIdEntry entry;
    entry.noteId = event.noteId;
    entry.channel = event.channel;
    out.push_back(entry);
  }
}

TRACK_COLD_MEM void LoopContentResolution::StateCheckpoints::sortAndUniqueChannelByNoteIdEntries(
    ChannelByNoteIdEntryVec& entries) {
  std::stable_sort(entries.begin(), entries.end(),
                   [](const ChannelByNoteIdEntry& a, const ChannelByNoteIdEntry& b) {
                     return a.noteId < b.noteId;
                   });
  entries.erase(std::unique(entries.begin(), entries.end(),
                            [](const ChannelByNoteIdEntry& a, const ChannelByNoteIdEntry& b) {
                              return a.noteId == b.noteId;
                            }),
                entries.end());
}

TRACK_COLD_MEM uint8_t LoopContentResolution::StateCheckpoints::findChannelByNoteId(
    const ChannelByNoteIdEntryVec& entries, NoteId noteId) {
  if (noteId == kInvalidNoteId || entries.empty()) {
    return 0;
  }
  auto it = std::lower_bound(entries.begin(), entries.end(), noteId,
                             [](const ChannelByNoteIdEntry& entry, NoteId id) {
                               return entry.noteId < id;
                             });
  if (it != entries.end() && it->noteId == noteId) {
    return it->channel;
  }
  return 0;
}

TRACK_COLD_MEM void LoopContentResolution::StateCheckpoints::resolveState(uint32_t tick,
                                                                         PresentNoteVec& out,
                                                                         ResolutionCostCounters* counters) const {
  resolveStateFromSpanBoundaries(spanBoundaries, tick, out, counters);
}

TRACK_COLD_MEM void LoopContentResolution::StateCheckpoints::resolveStateFromSpanBoundaries(
    const SpanBoundaryEntryVec& entries, uint32_t tick, PresentNoteVec& out,
    ResolutionCostCounters* counters) const {
  uint32_t replayStart = 0;
  if (!seedResolveStateFromCheckpoint(*this, tick, out, replayStart)) {
    return;
  }
  const uint32_t queryTick = IntervalProjection::tickPhaseInLoop(tick, 0, loopLengthTicks);
  uint32_t replayed = 0;
  auto applyRange = [&](uint32_t beginTick, uint32_t endTickExclusive) {
    auto it = std::lower_bound(entries.begin(), entries.end(), beginTick,
                               [](const SpanBoundaryEntry& entry, uint32_t bound) {
                                 return entry.tick < bound;
                               });
    for (; it != entries.end() && it->tick < endTickExclusive; ++it) {
      replayed += 1;
      if (it->spanIndex >= spans.size()) {
        continue;
      }
      applySpanBoundaryAtTick(out, spans[it->spanIndex], it->tick);
    }
  };
  applyRange(replayStart + 1, queryTick + 1);
  writeResolveStateCounters(*this, replayStart, replayed, counters);
}

TRACK_COLD_MEM void LoopContentResolution::resolveState(const StateCheckpoints& checkpoints, uint32_t tick,
                                         PresentNoteVec& out, ResolutionCostCounters* counters) {
  checkpoints.resolveState(tick, out, counters);
}

TRACK_COLD_MEM void LoopContentResolution::resolveNotes(const LoopPasses& passes, uint32_t loopLengthTicks,
                                         uint32_t windowStart, uint32_t windowLength,
                                         NoteUtils::DisplayNoteVec& out,
                                         ResolutionCostCounters* counters) {
  SessionMidiEventVec events;
  gatherActiveResolvedEvents(passes, loopLengthTicks, windowStart, windowLength, events, counters);
  out = NoteUtils::reconstructDisplayNotes(events, loopLengthTicks, false);
}

namespace {

TRACK_COLD_MEM size_t countIndexCommitPasses(const LoopPasses& passes);

struct IndexPassRef {
  PassId id = kInvalidPassId;
  CapturePassState state = CapturePassState::Active;
  uint32_t mergeSequence = 0;
  const CommittedChunkIdList* chunks = nullptr;
};

TRACK_COLD_MEM bool indexPassRefAtCursor(const LoopPasses& passes, size_t cursor, IndexPassRef& out) {
  size_t seen = 0;
  if (passes.hasRecordPass() && !passes.recordPass.committedChunkIds.empty()) {
    if (cursor == seen) {
      out.id = passes.recordPass.id;
      out.state = passes.recordPass.state;
      out.mergeSequence = 0;
      out.chunks = &passes.recordPass.committedChunkIds;
      return true;
    }
    seen += 1;
  }
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (cursor == seen) {
      out.id = pass.id;
      out.state = pass.state;
      out.mergeSequence = pass.mergeSequence;
      out.chunks = &pass.committedChunkIds;
      return true;
    }
    seen += 1;
  }
  return false;
}

struct DeviceGateSession {
  enum class Phase : uint8_t {
    Idle,
    IndexCommit,
    Materialize,
    Window,
    RebuildPrepare,
    RebuildSpans,
    RebuildCheckpoints,
    State,
    Done,
  };

  bool isActive() const { return phase != Phase::Idle && phase != Phase::Done; }

  void reset() {
    phase = Phase::Idle;
    loopLengthTicks = 0;
    indexPassCursor = 0;
    indexChunkCursor = 0;
    indexEventCursor = 0;
    indexPassOpen = false;
    pairPassOpen = false;
    pairEventCursor = 0;
    pairOpenOnByPitch.clear();
    checkpointCursor = 0;
    rebuildSpanCursor = 0;
    rebuildNotesReady = false;
    spanBoundariesSorted = false;
    channelByNoteIdCursor = 0;
    channelByNoteIdReady = false;
    tickEventsSorted = false;
    reconSpansFinished = false;
    reconEventCursor = 0;
    reconProjectCursor = 0;
    reconProjected = NoteUtils::DisplayNoteVec{};
    reconBuild.clear();
    prepReady = false;
    prepMaterializeDone = false;
    prepMergeOpen = false;
    prepPassCursor = 0;
    prepEventCursor = 0;
    prepBaseCursor = 0;
    prepAddCursor = 0;
    prepPasses.clear();
    prepEditRows.clear();
    prepMerged = SessionMidiEventVec{};
    lastStepName = "idle";
    lastLoggedStep = nullptr;
    lastPhaseLogUs = 0;
    preparedIndexKept = false;
    preparedPlaybackRevision = 0;
    delta = LoopContentResolution::TickIndex::TickEventEntryVec{};
    index = LoopContentResolution::TickIndex{};
    checkpoints = LoopContentResolution::StateCheckpoints{};
    preparedCompanions.clear();
    rebuildEvents = SessionMidiEventVec{};
    rebuildNotes = NoteUtils::DisplayNoteVec{};
    sample_ = LoopContentResolution::DeviceGateSample{};
  }

  void keepSparseSoundingAt() {
    const uint32_t keepInterval =
        Config::TICKS_PER_BAR * LoopContentResolution::kDeviceCheckpointBarStride;
    if (checkpoints.intervalTicks == 0 || keepInterval <= checkpoints.intervalTicks) {
      return;
    }
    if ((keepInterval % checkpoints.intervalTicks) != 0) {
      return;
    }
    const uint32_t factor = keepInterval / checkpoints.intervalTicks;
    std::vector<PresentNoteVec, ExternalMemoryFirstAllocator<PresentNoteVec>> kept;
    for (uint32_t i = 0; i < static_cast<uint32_t>(checkpoints.presentAt.size()); i += factor) {
      kept.push_back(std::move(checkpoints.presentAt[i]));
    }
    if (kept.empty() && !checkpoints.presentAt.empty()) {
      kept.push_back(std::move(checkpoints.presentAt.front()));
    }
    checkpoints.presentAt.swap(kept);
    checkpoints.intervalTicks = keepInterval;
  }

  void dropWorkingBuffers() {
    indexPassCursor = 0;
    indexChunkCursor = 0;
    indexEventCursor = 0;
    indexPassOpen = false;
    pairPassOpen = false;
    pairEventCursor = 0;
    pairOpenOnByPitch.clear();
    checkpointCursor = 0;
    rebuildSpanCursor = 0;
    rebuildNotesReady = false;
    spanBoundariesSorted = false;
    channelByNoteIdCursor = 0;
    channelByNoteIdReady = false;
    tickEventsSorted = false;
    reconSpansFinished = false;
    reconEventCursor = 0;
    reconProjectCursor = 0;
    reconProjected = NoteUtils::DisplayNoteVec{};
    reconBuild.clear();
    prepReady = false;
    prepMaterializeDone = false;
    prepMergeOpen = false;
    prepPassCursor = 0;
    prepEventCursor = 0;
    prepBaseCursor = 0;
    prepAddCursor = 0;
    prepPasses.clear();
    prepEditRows.clear();
    prepMerged = SessionMidiEventVec{};
    keepSparseSoundingAt();
    rebuildEvents = SessionMidiEventVec{};
    rebuildNotes = NoteUtils::DisplayNoteVec{};
    delta = LoopContentResolution::TickIndex::TickEventEntryVec{};
  }

  void keepPreparedIndex(uint32_t playbackRevision) {
    preparedPlaybackRevision = playbackRevision;
    preparedIndexKept = index.indexedEventCount() > 0;
    dropWorkingBuffers();
    phase = Phase::Done;
    lastStepName = "done";
  }

  void begin(uint32_t ticks) {
    reset();
    if (ticks == 0) {
      return;
    }
    loopLengthTicks = ticks;
    phase = Phase::IndexCommit;
    lastStepName = "idx";
  }

  void advancePastIndexCommit() {
#if defined(ARDUINO)
    phase = Phase::Window;
    lastStepName = "win";
#else
    phase = Phase::Materialize;
    lastStepName = "mat";
#endif
  }

  bool formatPhaseLine(char* line, size_t cap) {
    if (line == nullptr || cap == 0) {
      return false;
    }
    const char* name = lastStepName != nullptr ? lastStepName : "idle";
#if defined(ARDUINO)
    const unsigned long stamp = static_cast<unsigned long>(micros());
    const bool intervalElapsed =
        lastPhaseLogUs == 0 ||
        (stamp - lastPhaseLogUs) >= LoopContentResolution::kDeviceGatePhaseLogIntervalUs;
#else
    const unsigned long stamp = 0UL;
    const bool intervalElapsed = false;
#endif
    const bool stepChanged = lastLoggedStep == nullptr || lastLoggedStep != name;
    if (!stepChanged && !intervalElapsed) {
      return false;
    }
    lastLoggedStep = name;
#if defined(ARDUINO)
    lastPhaseLogUs = static_cast<uint32_t>(stamp);
#endif
    const unsigned evLogged =
        (name == kPairStep)    ? static_cast<unsigned>(pairEventCursor)
        : (name == kReconStep) ? static_cast<unsigned>(reconEventCursor)
        : (name == kProjStep)  ? static_cast<unsigned>(reconProjectCursor)
        : (name == kChannelStep)     ? static_cast<unsigned>(channelByNoteIdCursor)
        : (name == kChannelSortStep) ? static_cast<unsigned>(checkpoints.channelByNoteId.size())
        : (name == kSortStep)        ? static_cast<unsigned>(checkpoints.spanBoundaries.size())
        : (name == kIndexSortStep)   ? static_cast<unsigned>(index.tickEvents.size())
        : (name == kByNoteIdSortStep) ? static_cast<unsigned>(index.byNoteId.size())
        : (name == kPrepStep)        ? (prepMergeOpen ? static_cast<unsigned>(prepMerged.size())
                                                      : static_cast<unsigned>(rebuildEvents.size()))
                                     : static_cast<unsigned>(indexEventCursor);
    const unsigned notesLogged = (name == kProjStep)
                                     ? static_cast<unsigned>(reconProjected.size())
                                     : static_cast<unsigned>(rebuildNotes.size());
    const unsigned passLogged =
        (name == kPrepStep) ? static_cast<unsigned>(prepPassCursor)
                            : static_cast<unsigned>(indexPassCursor);
    snprintf(line, cap, "#CAP,%lu,DIAG,lcr,phase,%s,pass,%u,ev,%u,span,%u,notes,%u,bn=%lu,op=%lu,lk=%lu,pk=%u",
             stamp, name, passLogged, evLogged, static_cast<unsigned>(rebuildSpanCursor),
             notesLogged, static_cast<unsigned long>(sample_.indexCommit.pairByNoteIdMicros),
             static_cast<unsigned long>(sample_.indexCommit.pairOpenOnByPitchMicros),
             static_cast<unsigned long>(sample_.indexCommit.pairLookupMicros),
             static_cast<unsigned>(sample_.indexCommit.pairOpenOnPeakDepth));
    return true;
  }

  LoopContentResolution::DeviceGateSliceResult runOneSlice(const LoopPasses& passes,
                                                           uint32_t ticks) {
    if (phase == Phase::Idle || phase == Phase::Done) {
      return LoopContentResolution::DeviceGateSliceResult::Inactive;
    }
    if (ticks == 0 || ticks != loopLengthTicks) {
      reset();
      return LoopContentResolution::DeviceGateSliceResult::Inactive;
    }

    switch (phase) {
      case Phase::IndexCommit: {
        if (indexPassCursor >= countIndexCommitPasses(passes)) {
          if (!index.byNoteIdSorted) {
            ElapsedTimer sortTimer;
            lastStepName = kByNoteIdSortStep;
            index.sortAndUniqueByNoteId(&sample_.indexCommit);
            sample_.indexCommit.elapsedMicros += sortTimer.elapsed();
            return LoopContentResolution::DeviceGateSliceResult::Continue;
          }
          if (!tickEventsSorted) {
            ElapsedTimer sortTimer;
            lastStepName = kIndexSortStep;
            index.sortTickEventEntriesByTick(index.tickEvents);
            sample_.indexCommit.tickEventSortMicros += sortTimer.elapsed();
            sample_.indexCommit.elapsedMicros += sortTimer.elapsed();
            tickEventsSorted = true;
            return LoopContentResolution::DeviceGateSliceResult::Continue;
          }
          advancePastIndexCommit();
          return LoopContentResolution::DeviceGateSliceResult::Continue;
        }
        IndexPassRef passRef;
        if (!indexPassRefAtCursor(passes, indexPassCursor, passRef) || passRef.chunks == nullptr) {
          reset();
          return LoopContentResolution::DeviceGateSliceResult::Inactive;
        }
        ElapsedTimer timer;
        lastStepName = "idx";
        if (!indexPassOpen) {
          index.beginCapturePass(passRef.id, passRef.state, passRef.mergeSequence);
          sample_.indexCommit.passChunkListsWalked += 1;
          indexPassOpen = true;
          indexChunkCursor = 0;
          indexEventCursor = 0;
          pairPassOpen = false;
          pairEventCursor = 0;
          pairOpenOnByPitch.clear();
          sample_.indexCommit.elapsedMicros += timer.elapsed();
          return LoopContentResolution::DeviceGateSliceResult::Continue;
        }
        const LoopContentResolution::TickIndex::CapturePassEntry* pass = findPass(index, passRef.id);
        if (pass == nullptr) {
          reset();
          return LoopContentResolution::DeviceGateSliceResult::Inactive;
        }
        if (indexEventCursor >= static_cast<uint32_t>(pass->events.size())) {
          if (indexChunkCursor < passRef.chunks->size()) {
            index.appendCapturePassChunk(passRef.id, (*passRef.chunks)[indexChunkCursor]);
            indexChunkCursor += 1;
            sample_.indexCommit.elapsedMicros += timer.elapsed();
            return LoopContentResolution::DeviceGateSliceResult::Continue;
          }
          const uint32_t eventCount = static_cast<uint32_t>(pass->events.size());
          if (!pairPassOpen) {
            pairPassOpen = true;
            pairEventCursor = 0;
            pairOpenOnByPitch.clear();
          }
          if (pairEventCursor < eventCount) {
            const uint32_t end = std::min(
                pairEventCursor + LoopContentResolution::kDeviceGateEventsPerSlice, eventCount);
            index.pairCapturePassEventRange(passRef.id, pairEventCursor, end, pairOpenOnByPitch,
                                            &sample_.indexCommit);
            pairEventCursor = end;
            lastStepName = kPairStep;
            sample_.indexCommit.elapsedMicros += timer.elapsed();
            return LoopContentResolution::DeviceGateSliceResult::Continue;
          }
          indexPassCursor += 1;
          indexPassOpen = false;
          pairPassOpen = false;
          pairEventCursor = 0;
          pairOpenOnByPitch.clear();
          indexChunkCursor = 0;
          indexEventCursor = 0;
          sample_.indexCommit.elapsedMicros += timer.elapsed();
          return LoopContentResolution::DeviceGateSliceResult::Continue;
        }
        const uint32_t eventCount = static_cast<uint32_t>(pass->events.size());
        const uint32_t end = std::min(
            indexEventCursor + LoopContentResolution::kDeviceGateEventsPerSlice, eventCount);
        ElapsedTimer appendTimer;
        index.indexCapturePassEventRange(passRef.id, indexEventCursor, end, &sample_.indexCommit);
        sample_.indexCommit.tickEventAppendMicros += appendTimer.elapsed();
        indexEventCursor = end;
        sample_.indexCommit.elapsedMicros += timer.elapsed();
        return LoopContentResolution::DeviceGateSliceResult::Continue;
      }
    case Phase::Materialize: {
        ElapsedTimer timer;
        SessionMidiEventVec materialized;
        passes.materializeToEventVector(materialized, loopLengthTicks);
        (void)NoteUtils::reconstructDisplayNotes(materialized, loopLengthTicks, false);
        sample_.materialize.elapsedMicros += timer.elapsed();
        sample_.materialize.eventsInHistory = static_cast<uint32_t>(materialized.size());
        sample_.materialize.passesInHistory = index.indexedPassCount();
        lastStepName = "mat";
        phase = Phase::Window;
        return LoopContentResolution::DeviceGateSliceResult::Continue;
      }
      case Phase::Window: {
        uint32_t windowLength = DisplayWindowUtils::kMaxDetailedWindowBars * Config::TICKS_PER_BAR;
        if (windowLength > loopLengthTicks) {
          windowLength = loopLengthTicks;
        }
        uint32_t windowStart = 0;
        if (loopLengthTicks > windowLength / 2u) {
          windowStart = loopLengthTicks - windowLength / 2u;
        }
        SessionMidiEventVec window;
        LoopContentResolution::resolveWindow(index, passes.editPasses, loopLengthTicks, windowStart,
                                             windowLength, window, &sample_.window);
        lastStepName = "win";
        phase = Phase::RebuildPrepare;
        return LoopContentResolution::DeviceGateSliceResult::Continue;
      }
      case Phase::RebuildPrepare: {
        ElapsedTimer timer;
#if defined(ARDUINO)
        const uint32_t checkpointInterval =
            Config::TICKS_PER_BAR * LoopContentResolution::kDeviceCheckpointBarStride;
#else
        const uint32_t checkpointInterval =
            Config::TICKS_PER_BAR * LoopContentResolution::kNativeCheckpointBarStride;
#endif
        lastStepName = kPrepStep;
        if (!prepReady) {
          if (!checkpoints.beginRebuildResolvedEvents(loopLengthTicks, checkpointInterval,
                                                      rebuildEvents)) {
            sample_.rebuild.elapsedMicros += timer.elapsed();
            phase = Phase::RebuildSpans;
            return LoopContentResolution::DeviceGateSliceResult::Continue;
          }
          index.collectActiveMaterializePasses(prepPasses);
          prepEditRows.clear();
          for (const EditPass& editPass : passes.editPasses) {
            if (editPass.state == EditPassState::Active && editPass.passType == EditPassType::Note) {
              prepEditRows.push_back(editPass);
            }
          }
          prepReady = true;
          prepMaterializeDone = prepPasses.empty();
          prepMergeOpen = false;
          prepPassCursor = 0;
          prepEventCursor = 0;
          sample_.rebuild.elapsedMicros += timer.elapsed();
          return LoopContentResolution::DeviceGateSliceResult::Continue;
        }
        if (!prepMaterializeDone) {
          if (prepPassCursor >= prepPasses.size()) {
            prepMaterializeDone = true;
          } else {
            const LoopContentResolution::TickIndex::CapturePassEntry* pass =
                prepPasses[prepPassCursor];
            if (pass == nullptr) {
              reset();
              return LoopContentResolution::DeviceGateSliceResult::Inactive;
            }
            const uint32_t eventCount = static_cast<uint32_t>(pass->events.size());
            const bool appendOnly = (prepPassCursor == 0) || rebuildEvents.empty();
            if (appendOnly) {
              if (prepEventCursor >= eventCount) {
                prepPassCursor += 1;
                prepEventCursor = 0;
                sample_.rebuild.elapsedMicros += timer.elapsed();
                return LoopContentResolution::DeviceGateSliceResult::Continue;
              }
              const uint32_t end = std::min(
                  prepEventCursor + LoopContentResolution::kDeviceGateEventsPerSlice, eventCount);
              index.appendMaterializePassEvents(*pass, prepEventCursor, end, rebuildEvents);
              prepEventCursor = end;
              sample_.rebuild.elapsedMicros += timer.elapsed();
              return LoopContentResolution::DeviceGateSliceResult::Continue;
            }
            if (!prepMergeOpen) {
              prepMerged.clear();
              prepMerged.reserve(rebuildEvents.size() + pass->events.size());
              prepBaseCursor = 0;
              prepAddCursor = 0;
              prepMergeOpen = true;
            }
            const uint32_t produced = LoopContentResolution::TickIndex::mergeSortedMidiEventRange(
                rebuildEvents, prepBaseCursor, pass->events, prepAddCursor, prepMerged,
                LoopContentResolution::kDeviceGateEventsPerSlice);
            if (produced == 0) {
              rebuildEvents = std::move(prepMerged);
              prepMerged = SessionMidiEventVec{};
              prepMergeOpen = false;
              prepPassCursor += 1;
              prepEventCursor = 0;
            }
            sample_.rebuild.elapsedMicros += timer.elapsed();
            return LoopContentResolution::DeviceGateSliceResult::Continue;
          }
        }
        if (!prepEditRows.empty()) {
          applyNoteEditPassSequence(rebuildEvents, prepEditRows, loopLengthTicks);
          prepEditRows.clear();
          sample_.rebuild.passChunkListsWalked = 0;
          sample_.rebuild.eventsInHistory = static_cast<uint32_t>(rebuildEvents.size());
          sample_.rebuild.elapsedMicros += timer.elapsed();
          phase = Phase::RebuildSpans;
          return LoopContentResolution::DeviceGateSliceResult::Continue;
        }
        sample_.rebuild.passChunkListsWalked = 0;
        sample_.rebuild.eventsInHistory = static_cast<uint32_t>(rebuildEvents.size());
        sample_.rebuild.elapsedMicros += timer.elapsed();
        phase = Phase::RebuildSpans;
        return LoopContentResolution::DeviceGateSliceResult::Continue;
      }
      case Phase::RebuildSpans: {
        ElapsedTimer timer;
        if (!reconSpansFinished) {
          const uint32_t eventCount = static_cast<uint32_t>(rebuildEvents.size());
          if (reconEventCursor < eventCount) {
            const uint32_t end = std::min(
                reconEventCursor + LoopContentResolution::kDeviceGateEventsPerSlice, eventCount);
            NoteUtils::appendCanonicalSpansFromMidi(rebuildEvents, loopLengthTicks, reconEventCursor,
                                                    end, reconBuild);
            reconEventCursor = end;
            lastStepName = kReconStep;
            sample_.rebuild.elapsedMicros += timer.elapsed();
            return LoopContentResolution::DeviceGateSliceResult::Continue;
          }
          NoteUtils::finishCanonicalSpansFromMidi(loopLengthTicks, reconBuild);
          reconSpansFinished = true;
          lastStepName = kReconStep;
          sample_.rebuild.elapsedMicros += timer.elapsed();
          return LoopContentResolution::DeviceGateSliceResult::Continue;
        }
        if (!rebuildNotesReady) {
          const uint32_t spanCount = reconBuild.spanCount();
          if (reconProjectCursor < spanCount) {
            const uint32_t end = std::min(
                reconProjectCursor + LoopContentResolution::kDeviceGateEventsPerSlice, spanCount);
            NoteUtils::appendProjectedDisplayNotes(reconBuild, loopLengthTicks, reconProjectCursor,
                                                   end, reconProjected);
            reconProjectCursor = end;
            lastStepName = kProjStep;
            sample_.rebuild.elapsedMicros += timer.elapsed();
            return LoopContentResolution::DeviceGateSliceResult::Continue;
          }
          checkpoints.spans.clear();
          checkpoints.spanBoundaries.clear();
          checkpoints.presentAt.clear();
          checkpoints.channelByNoteId.clear();
          rebuildNotes = NoteUtils::dedupeProjectedDisplayNotes(reconProjected);
          reconProjected = NoteUtils::DisplayNoteVec{};
          reconBuild.clear();
          rebuildSpanCursor = 0;
          channelByNoteIdCursor = 0;
          channelByNoteIdReady = false;
          rebuildNotesReady = true;
          spanBoundariesSorted = false;
          lastStepName = kDedupStep;
          sample_.rebuild.elapsedMicros += timer.elapsed();
          return LoopContentResolution::DeviceGateSliceResult::Continue;
        }
        const uint32_t noteCount = static_cast<uint32_t>(rebuildNotes.size());
        if (!channelByNoteIdReady) {
          const uint32_t eventCount = static_cast<uint32_t>(rebuildEvents.size());
          if (channelByNoteIdCursor < eventCount) {
            const uint32_t end = std::min(
                channelByNoteIdCursor + LoopContentResolution::kDeviceGateEventsPerSlice, eventCount);
            if (!checkpoints.appendChannelByNoteIdRange(rebuildEvents, channelByNoteIdCursor, end,
                                                        &sample_.rebuild)) {
              reset();
              return LoopContentResolution::DeviceGateSliceResult::Inactive;
            }
            channelByNoteIdCursor = end;
            lastStepName = kChannelStep;
            sample_.rebuild.elapsedMicros += timer.elapsed();
            return LoopContentResolution::DeviceGateSliceResult::Continue;
          }
          checkpoints.sortChannelByNoteId(&sample_.rebuild);
          channelByNoteIdReady = true;
          lastStepName = kChannelSortStep;
          sample_.rebuild.elapsedMicros += timer.elapsed();
          return LoopContentResolution::DeviceGateSliceResult::Continue;
        }
        if (rebuildSpanCursor < noteCount) {
          const uint32_t end = std::min(
              rebuildSpanCursor + LoopContentResolution::kDeviceGateEventsPerSlice, noteCount);
          if (!checkpoints.appendSpansFromNotes(rebuildEvents, rebuildNotes, rebuildSpanCursor, end,
                                                &sample_.rebuild)) {
            reset();
            return LoopContentResolution::DeviceGateSliceResult::Inactive;
          }
          rebuildSpanCursor = end;
          lastStepName = "spans";
          sample_.rebuild.elapsedMicros += timer.elapsed();
          return LoopContentResolution::DeviceGateSliceResult::Continue;
        }
        if (!spanBoundariesSorted) {
          checkpoints.sortSpanBoundaries(&sample_.rebuild);
          spanBoundariesSorted = true;
          lastStepName = kSortStep;
          sample_.rebuild.elapsedMicros += timer.elapsed();
          return LoopContentResolution::DeviceGateSliceResult::Continue;
        }
        uint32_t count = checkpoints.loopLengthTicks / checkpoints.intervalTicks;
        if (count == 0) {
          count = 1;
        }
        checkpoints.presentAt.resize(count);
        sample_.rebuild.checkpointIntervalTicks = checkpoints.intervalTicks;
        sample_.rebuild.checkpointCount = static_cast<uint32_t>(checkpoints.presentAt.size());
        rebuildEvents = SessionMidiEventVec{};
        rebuildNotes = NoteUtils::DisplayNoteVec{};
        checkpointCursor = 0;
        phase = Phase::RebuildCheckpoints;
        sample_.rebuild.elapsedMicros += timer.elapsed();
        return LoopContentResolution::DeviceGateSliceResult::Continue;
      }
      case Phase::RebuildCheckpoints: {
        constexpr uint32_t kCheckpointsPerSlice = 1;
        const uint32_t total = static_cast<uint32_t>(checkpoints.presentAt.size());
        if (checkpointCursor >= total) {
          lastStepName = "state";
          phase = Phase::State;
          return LoopContentResolution::DeviceGateSliceResult::Continue;
        }
        ElapsedTimer timer;
        const uint32_t end = std::min(checkpointCursor + kCheckpointsPerSlice, total);
        if (!checkpoints.fillCheckpointRange(checkpointCursor, end, &sample_.rebuild)) {
          reset();
          return LoopContentResolution::DeviceGateSliceResult::Inactive;
        }
        sample_.rebuild.elapsedMicros += timer.elapsed();
        checkpointCursor = end;
        lastStepName = "ckpt";
        if (checkpointCursor >= total) {
          phase = Phase::State;
        }
        return LoopContentResolution::DeviceGateSliceResult::Continue;
      }
      case Phase::State: {
        const uint32_t highTick = loopLengthTicks > 24u ? loopLengthTicks - 24u : 0u;
        ElapsedTimer timer;
        PresentNoteVec presentNotes;
        LoopContentResolution::resolveState(checkpoints, highTick, presentNotes, &sample_.state);
        sample_.state.elapsedMicros += timer.elapsed();
        lastStepName = "state";
        phase = Phase::Done;
        return LoopContentResolution::DeviceGateSliceResult::Complete;
      }
      case Phase::Idle:
      case Phase::Done:
        break;
    }
    reset();
    return LoopContentResolution::DeviceGateSliceResult::Inactive;
  }

  Phase phase = Phase::Idle;
  uint32_t loopLengthTicks = 0;
  size_t indexPassCursor = 0;
  uint32_t indexChunkCursor = 0;
  uint32_t indexEventCursor = 0;
  bool indexPassOpen = false;
  static constexpr const char* kPairStep = "pair";
  static constexpr const char* kReconStep = "recon";
  static constexpr const char* kProjStep = "proj";
  static constexpr const char* kDedupStep = "dedup";
  static constexpr const char* kChannelStep = "chan";
  static constexpr const char* kChannelSortStep = "csort";
  static constexpr const char* kSortStep = "sort";
  static constexpr const char* kIndexSortStep = "isort";
  static constexpr const char* kByNoteIdSortStep = "nsort";
  static constexpr const char* kPrepStep = "prep";
  bool prepReady = false;
  bool prepMaterializeDone = false;
  bool prepMergeOpen = false;
  uint32_t prepPassCursor = 0;
  uint32_t prepEventCursor = 0;
  uint32_t prepBaseCursor = 0;
  uint32_t prepAddCursor = 0;
  std::vector<const LoopContentResolution::TickIndex::CapturePassEntry*> prepPasses;
  EditPassVec prepEditRows;
  SessionMidiEventVec prepMerged;
  bool pairPassOpen = false;
  uint32_t pairEventCursor = 0;
  std::map<uint8_t, std::vector<uint32_t>> pairOpenOnByPitch;
  uint32_t checkpointCursor = 0;
  uint32_t rebuildSpanCursor = 0;
  bool rebuildNotesReady = false;
  bool spanBoundariesSorted = false;
  uint32_t channelByNoteIdCursor = 0;
  bool channelByNoteIdReady = false;
  bool tickEventsSorted = false;
  bool reconSpansFinished = false;
  uint32_t reconEventCursor = 0;
  uint32_t reconProjectCursor = 0;
  NoteUtils::DisplayNoteVec reconProjected;
  NoteUtils::CanonicalSpanBuild reconBuild;
  const char* lastStepName = "idle";
  const char* lastLoggedStep = nullptr;
  uint32_t lastPhaseLogUs = 0;
  bool preparedIndexKept = false;
  uint32_t preparedPlaybackRevision = 0;
  LoopContentResolution::TickIndex index;
  /// 6D.4: committed overdub rows since the last full prepare. Not a TickIndex member.
  LoopContentResolution::TickIndex::TickEventEntryVec delta;
  LoopContentResolution::StateCheckpoints checkpoints;
  PreparedCompanionVec preparedCompanions;
  SessionMidiEventVec rebuildEvents;
  NoteUtils::DisplayNoteVec rebuildNotes;
  LoopContentResolution::DeviceGateSample sample_;
};

TRACK_COLD_MEM size_t countIndexCommitPasses(const LoopPasses& passes) {
  size_t count = 0;
  if (passes.hasRecordPass() && !passes.recordPass.committedChunkIds.empty()) {
    count += 1;
  }
  count += passes.overdubPasses.size();
  return count;
}

bool sDeviceGateFinished = false;
#if defined(__IMXRT1062__)
DMAMEM DeviceGateSession sDeviceGateSession;
#else
DeviceGateSession sDeviceGateSession;
#endif

}  // namespace

bool LoopContentResolution::deviceGateFinished() { return sDeviceGateFinished; }

bool LoopContentResolution::deviceGateActive() { return sDeviceGateSession.isActive(); }

void LoopContentResolution::deviceGateBegin(uint32_t loopLengthTicks) {
  sDeviceGateSession.begin(loopLengthTicks);
}

TRACK_COLD_MEM void LoopContentResolution::deviceGateReset() {
  sDeviceGateSession.reset();
  sDeviceGateFinished = false;
}

TRACK_COLD_MEM void LoopContentResolution::deviceGateComplete(uint32_t playbackRevision) {
  sDeviceGateSession.keepPreparedIndex(playbackRevision);
  sDeviceGateFinished = true;
}

TRACK_COLD_MEM bool LoopContentResolution::preparedWindowReady(uint32_t playbackRevision) {
  return sDeviceGateFinished && sDeviceGateSession.preparedIndexKept &&
         sDeviceGateSession.preparedPlaybackRevision == playbackRevision;
}

TRACK_COLD_MEM void LoopContentResolution::publishPreparedOverdubPass(
    const OverdubPass& pass, uint32_t playbackRevision, const EditPassVec& editPasses,
    const EditPassIdList& companionIds) {
  if (!sDeviceGateFinished || !sDeviceGateSession.preparedIndexKept ||
      pass.id == kInvalidPassId) {
    return;
  }
  TickIndex& index = sDeviceGateSession.index;
  if (findPass(index, pass.id) != nullptr) {
    return;
  }
  index.beginCapturePass(pass.id, pass.state, pass.mergeSequence);
  for (uint16_t chunkId : pass.committedChunkIds) {
    index.appendCapturePassChunk(pass.id, chunkId);
  }
  const TickIndex::CapturePassEntry* entry = findPass(index, pass.id);
  if (entry == nullptr) {
    return;
  }
  TickIndex::appendTickEventEntries(*entry, 0, static_cast<uint32_t>(entry->events.size()),
                                    sDeviceGateSession.delta);
  TickIndex::sortTickEventEntriesByTick(sDeviceGateSession.delta);
  index.pairCapturePassNotes(pass.id);
  index.sortAndUniqueByNoteId(nullptr);

  StateCheckpoints& checkpoints = sDeviceGateSession.checkpoints;
  const uint32_t spanBegin = static_cast<uint32_t>(checkpoints.spans.size());
  const size_t boundBegin = checkpoints.spanBoundaries.size();
  checkpoints.appendChannelByNoteIdRange(entry->events, 0, static_cast<uint32_t>(entry->events.size()),
                                         nullptr);
  checkpoints.sortChannelByNoteId(nullptr);
  const NoteUtils::DisplayNoteVec notes =
      NoteUtils::reconstructDisplayNotes(entry->events, sDeviceGateSession.loopLengthTicks, false,
                                        false);
  checkpoints.appendSpansFromNotes(entry->events, notes, 0, static_cast<uint32_t>(notes.size()),
                                   nullptr);
  StateCheckpoints::SpanBoundaryEntryVec added(checkpoints.spanBoundaries.begin() +
                                                   static_cast<std::ptrdiff_t>(boundBegin),
                                               checkpoints.spanBoundaries.end());
  checkpoints.spanBoundaries.resize(boundBegin);
  StateCheckpoints::sortSpanBoundaryEntriesByTick(added);
  StateCheckpoints::SpanBoundaryEntryVec merged;
  mergeSortedSpanBoundaryEntries(checkpoints.spanBoundaries, added, merged);
  checkpoints.spanBoundaries.swap(merged);
  for (uint32_t i = spanBegin; i < static_cast<uint32_t>(checkpoints.spans.size()); ++i) {
    NoteUtils::DisplayNote probe{};
    probe.noteId = checkpoints.spans[i].note.noteId;
    probe.note = checkpoints.spans[i].note.pitch;
    probe.startTick = checkpoints.spans[i].startTick;
    probe.endTick = checkpoints.spans[i].endTick;
    for (uint32_t c = 0; c < static_cast<uint32_t>(checkpoints.presentAt.size()); ++c) {
      if (notePresentAt(probe, c * checkpoints.intervalTicks, checkpoints.loopLengthTicks)) {
        checkpoints.presentAt[c].push_back(checkpoints.spans[i].note);
      }
    }
  }
  projectSealedCompanionsOntoCheckpoints(checkpoints, editPasses, companionIds,
                                         sDeviceGateSession.preparedCompanions);
  sDeviceGateSession.preparedPlaybackRevision = playbackRevision;
}

TRACK_COLD_MEM void LoopContentResolution::setPreparedCapturePassState(PassId id,
                                                                      CapturePassState state) {
  if (!sDeviceGateFinished || !sDeviceGateSession.preparedIndexKept || id == kInvalidPassId) {
    return;
  }
  sDeviceGateSession.index.setCapturePassState(id, state);
}

TRACK_COLD_MEM void LoopContentResolution::setPreparedEditPassState(EditPassId id,
                                                                   EditPassState state) {
  if (!sDeviceGateFinished || !sDeviceGateSession.preparedIndexKept ||
      id == kInvalidEditPassId) {
    return;
  }
  for (PreparedCompanion& row : sDeviceGateSession.preparedCompanions) {
    if (row.id == id) {
      row.state = state;
    }
  }
}

TRACK_COLD_MEM void restoreDisabledCompanions(uint32_t tick, PresentNoteVec& out) {
  const uint32_t loopLength = sDeviceGateSession.checkpoints.loopLengthTicks;
  if (loopLength == 0) {
    return;
  }
  const uint32_t queryTick = IntervalProjection::tickPhaseInLoop(tick, 0, loopLength);
  for (const PreparedCompanion& row : sDeviceGateSession.preparedCompanions) {
    if (row.state != EditPassState::Disabled || row.note.noteId == kInvalidNoteId) {
      continue;
    }
    NoteUtils::DisplayNote probe{};
    probe.noteId = row.note.noteId;
    probe.note = row.note.pitch;
    probe.startTick = row.startTick;
    probe.endTick = row.endTick;
    if (!notePresentAt(probe, queryTick, loopLength)) {
      continue;
    }
    upsertPresentNote(out, row.note);
  }
}

TRACK_COLD_MEM void LoopContentResolution::restampPreparedPlaybackRevision(uint32_t playbackRevision) {
  if (!sDeviceGateFinished || !sDeviceGateSession.preparedIndexKept) {
    return;
  }
  sDeviceGateSession.preparedPlaybackRevision = playbackRevision;
}

TRACK_COLD_MEM bool LoopContentResolution::tryResolvePreparedWindow(
    const EditPassVec& editPasses, uint32_t loopLengthTicks, uint32_t windowStart,
    uint32_t windowLength, uint32_t playbackRevision, SessionMidiEventVec& out,
    ResolutionCostCounters* counters) {
  if (!preparedWindowReady(playbackRevision) ||
      loopLengthTicks == 0 || loopLengthTicks != sDeviceGateSession.loopLengthTicks) {
    return false;
  }
  if (!sDeviceGateSession.delta.empty()) {
    resolveWindow(sDeviceGateSession.index, sDeviceGateSession.index.tickEvents,
                  sDeviceGateSession.delta, editPasses, loopLengthTicks, windowStart, windowLength,
                  out, counters);
    return true;
  }
  resolveWindow(sDeviceGateSession.index, editPasses, loopLengthTicks, windowStart, windowLength,
                out, counters);
  return true;
}

TRACK_COLD_MEM bool LoopContentResolution::tryResolvePreparedState(uint32_t tick,
                                                                  uint32_t playbackRevision,
                                                                  PresentNoteVec& out,
                                                                  ResolutionCostCounters* counters) {
  out.clear();
  if (!preparedWindowReady(playbackRevision) ||
      sDeviceGateSession.checkpoints.spans.empty() ||
      sDeviceGateSession.checkpoints.spanBoundaries.empty() ||
      sDeviceGateSession.checkpoints.presentAt.empty()) {
    return false;
  }
  resolveState(sDeviceGateSession.checkpoints, tick, out, counters);
  eraseDisabledSounding(sDeviceGateSession.index, out);
  restoreDisabledCompanions(tick, out);
  return true;
}

TRACK_COLD_MEM bool LoopContentResolution::tryCollectPreparedPresentNoteIdsAtTick(
    uint32_t tick, uint8_t pitch, uint32_t playbackRevision, OverlapNoteIdSet& out) {
  out.clear();
  if (!preparedWindowReady(playbackRevision) ||
      sDeviceGateSession.checkpoints.spans.empty() ||
      sDeviceGateSession.checkpoints.loopLengthTicks == 0) {
    return false;
  }
  const uint32_t loopLength = sDeviceGateSession.checkpoints.loopLengthTicks;
  const TickIndex& index = sDeviceGateSession.index;
  for (const StateCheckpoints::NoteSpan& span : sDeviceGateSession.checkpoints.spans) {
    if (span.note.pitch != pitch || span.note.noteId == kInvalidNoteId) {
      continue;
    }
    const TickIndex::ByNoteIdEntry* found = index.findByNoteId(span.note.noteId);
    if (found != nullptr) {
      const TickIndex::CapturePassEntry* pass = findPass(index, found->loc.passId);
      if (pass != nullptr && pass->state != CapturePassState::Active) {
        continue;
      }
    }
    if (!OverlapNoteIdObservation::displayNotePresentAtHold(span.startTick, span.endTick, tick,
                                                            loopLength)) {
      continue;
    }
    (void)out.insert(span.note.noteId);
  }
  for (const PreparedCompanion& row : sDeviceGateSession.preparedCompanions) {
    if (row.state != EditPassState::Disabled || row.note.noteId == kInvalidNoteId ||
        row.note.pitch != pitch) {
      continue;
    }
    if (!OverlapNoteIdObservation::displayNotePresentAtHold(row.startTick, row.endTick, tick,
                                                            loopLength)) {
      continue;
    }
    (void)out.insert(row.note.noteId);
  }
  return true;
}

TRACK_COLD_MEM bool LoopContentResolution::tryCopyPreparedSpansToDisplayNotes(
    uint32_t playbackRevision, NoteUtils::DisplayNoteVec& out,
    const SessionMidiEventVec* windowEvents, uint32_t loopLengthTicks) {
  out.clear();
  if (!preparedWindowReady(playbackRevision) ||
      sDeviceGateSession.checkpoints.spans.empty() ||
      sDeviceGateSession.checkpoints.loopLengthTicks == 0) {
    return false;
  }
  if (loopLengthTicks != 0 &&
      loopLengthTicks != sDeviceGateSession.checkpoints.loopLengthTicks) {
    return false;
  }
  OverlapNoteIdSet windowIds;
  if (windowEvents != nullptr) {
    for (const MidiEvent& evt : *windowEvents) {
      if (!evt.isNoteOn() || evt.noteId == kInvalidNoteId) {
        continue;
      }
      if (!windowIds.insert(evt.noteId)) {
        return false;
      }
    }
    if (windowIds.size() == 0) {
      return false;
    }
  }
  const TickIndex& index = sDeviceGateSession.index;
  auto inWindow = [&](NoteId noteId) {
    return windowEvents == nullptr || windowIds.contains(noteId);
  };
  auto appendSpan = [&out](NoteId noteId, uint8_t pitch, uint32_t startTick, uint32_t endTick) {
    if (noteId == kInvalidNoteId || startTick == endTick) {
      return;
    }
    NoteUtils::DisplayNote note{};
    note.noteId = noteId;
    note.note = pitch;
    note.velocity = 0;
    note.startTick = startTick;
    note.endTick = endTick;
    out.push_back(note);
  };
  for (const StateCheckpoints::NoteSpan& span : sDeviceGateSession.checkpoints.spans) {
    if (span.note.noteId == kInvalidNoteId || !inWindow(span.note.noteId)) {
      continue;
    }
    const TickIndex::ByNoteIdEntry* found = index.findByNoteId(span.note.noteId);
    if (found == nullptr) {
      continue;
    }
    const TickIndex::CapturePassEntry* pass = findPass(index, found->loc.passId);
    if (pass != nullptr && pass->state != CapturePassState::Active) {
      continue;
    }
    appendSpan(span.note.noteId, span.note.pitch, span.startTick, span.endTick);
  }
  for (const PreparedCompanion& row : sDeviceGateSession.preparedCompanions) {
    if (row.state != EditPassState::Disabled || row.note.noteId == kInvalidNoteId ||
        !inWindow(row.note.noteId)) {
      continue;
    }
    appendSpan(row.note.noteId, row.note.pitch, row.startTick, row.endTick);
  }
  return !out.empty();
}

void LoopContentResolution::deviceGateFormatCaptureLine(char* line, size_t cap) {
  if (line == nullptr || cap == 0) {
    return;
  }
  const DeviceGateSample& sample = sDeviceGateSession.sample_;
#if defined(ARDUINO)
  const unsigned long stamp = static_cast<unsigned long>(micros());
#else
  const unsigned long stamp = 0UL;
#endif
  snprintf(line, cap,
           "#CAP,%lu,DIAG,lcr,mat=%lu,win=%lu,reb=%lu,st=%lu,rep=%u,hist=%u,walk=%u,app=%lu,sort=%lu,"
           "iapp=%lu,isort=%lu,capp=%lu,csort=%lu",
           stamp, static_cast<unsigned long>(sample.materialize.elapsedMicros),
           static_cast<unsigned long>(sample.window.elapsedMicros),
           static_cast<unsigned long>(sample.rebuild.elapsedMicros),
           static_cast<unsigned long>(sample.state.elapsedMicros),
           static_cast<unsigned>(sample.state.eventsReplayed),
           static_cast<unsigned>(sample.state.eventsInHistory),
           static_cast<unsigned>(sample.window.passChunkListsWalked),
           static_cast<unsigned long>(sample.rebuild.spanBoundaryAppendMicros),
           static_cast<unsigned long>(sample.rebuild.spanBoundarySortMicros),
           static_cast<unsigned long>(sample.indexCommit.tickEventAppendMicros),
           static_cast<unsigned long>(sample.indexCommit.tickEventSortMicros),
           static_cast<unsigned long>(sample.rebuild.channelByNoteIdAppendMicros),
           static_cast<unsigned long>(sample.rebuild.channelByNoteIdSortMicros));
}

void LoopContentResolution::deviceGateFormatPairLine(char* line, size_t cap) {
  if (line == nullptr || cap == 0) {
    return;
  }
  const ResolutionCostCounters& pair = sDeviceGateSession.sample_.indexCommit;
#if defined(ARDUINO)
  const unsigned long stamp = static_cast<unsigned long>(micros());
#else
  const unsigned long stamp = 0UL;
#endif
  snprintf(line, cap,
           "#CAP,%lu,DIAG,lcr,pair,tot=%lu,bn=%lu,op=%lu,lk=%lu,oth=%lu,ent=%u,ins=%u,ow=%u,"
           "pu=%u,po=%u,pk=%u,oa=%u,hb=%lu,nsort=%lu",
           stamp, static_cast<unsigned long>(pair.pairTotalMicros),
           static_cast<unsigned long>(pair.pairByNoteIdMicros),
           static_cast<unsigned long>(pair.pairOpenOnByPitchMicros),
           static_cast<unsigned long>(pair.pairLookupMicros),
           static_cast<unsigned long>(pair.pairOtherMicros),
           static_cast<unsigned>(pair.pairByNoteIdEntries),
           static_cast<unsigned>(pair.pairByNoteIdInserts),
           static_cast<unsigned>(pair.pairByNoteIdOverwrites),
           static_cast<unsigned>(pair.pairOpenOnPushes),
           static_cast<unsigned>(pair.pairOpenOnPops),
           static_cast<unsigned>(pair.pairOpenOnPeakDepth),
           static_cast<unsigned>(pair.pairOpenOnAllocations),
           static_cast<unsigned long>(pair.pairOpenOnHeapBytes),
           static_cast<unsigned long>(pair.pairByNoteIdSortMicros));
}

bool LoopContentResolution::deviceGateFormatPhaseLine(char* line, size_t cap) {
  return sDeviceGateSession.formatPhaseLine(line, cap);
}

LoopContentResolution::DeviceGateSliceResult LoopContentResolution::deviceGateRunOneSlice(
    const LoopPasses& passes, uint32_t loopLengthTicks) {
  return sDeviceGateSession.runOneSlice(passes, loopLengthTicks);
}

TRACK_COLD_MEM void LoopContentResolution::measureDeviceGate(const LoopPasses& passes,
                                                             uint32_t loopLengthTicks,
                                                             DeviceGateSample& out) {
  out = DeviceGateSample{};
  if (loopLengthTicks == 0) {
    return;
  }
  deviceGateBegin(loopLengthTicks);
  while (deviceGateRunOneSlice(passes, loopLengthTicks) == DeviceGateSliceResult::Continue) {
  }
  out = sDeviceGateSession.sample_;
}
