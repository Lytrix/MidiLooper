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

TRACK_COLD_MEM void collectActiveChunkLists(const LoopPasses& passes,
                             std::vector<const CommittedChunkIdList*>& lists) {
  lists.clear();
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Active &&
      !passes.recordPass.committedChunkIds.empty()) {
    lists.push_back(&passes.recordPass.committedChunkIds);
  }
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.state == CapturePassState::Active && !pass.committedChunkIds.empty()) {
      lists.push_back(&pass.committedChunkIds);
    }
  }
}

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
  std::merge(base.begin(), base.end(), addition.begin(), addition.end(),
             std::back_inserter(merged),
             [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
  base = std::move(merged);
}

TRACK_COLD_MEM void appendActiveCapturePassesMerged(const LoopPasses& passes, SessionMidiEventVec& out) {
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
    SessionMidiEventVec layer;
    LoopEventStore::appendChunkRefEvents(pass->committedChunkIds, layer);
    mergeSortedMidiVectors(out, std::move(layer));
  }
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

TRACK_COLD_MEM bool noteSoundsAt(const NoteUtils::DisplayNote& note, uint32_t tick, uint32_t loopLengthTicks) {
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

TRACK_COLD_MEM void upsertSounding(SoundingNoteVec& sounding, const SoundingNote& note) {
  for (SoundingNote& existing : sounding) {
    if (note.noteId != kInvalidNoteId && existing.noteId == note.noteId) {
      existing = note;
      return;
    }
  }
  sounding.push_back(note);
}

TRACK_COLD_MEM void eraseSounding(SoundingNoteVec& sounding, const MidiEvent& off) {
  sounding.erase(std::remove_if(sounding.begin(), sounding.end(),
                                [&](const SoundingNote& note) {
                                  if (off.noteId != kInvalidNoteId) {
                                    return note.noteId == off.noteId;
                                  }
                                  return note.channel == off.channel &&
                                         note.pitch == off.data.noteData.note;
                                }),
                 sounding.end());
}

TRACK_COLD_MEM void applySpanBoundaryAtTick(
    SoundingNoteVec& sounding, const LoopContentResolution::StateCheckpoints::NoteSpan& span,
    uint32_t keyTick) {
  if (keyTick == span.startTick) {
    upsertSounding(sounding, span.note);
  }
  if (keyTick == span.endTick) {
    MidiEvent off = MidiEvent::NoteOff(span.endTick, span.note.channel, span.note.pitch, 0);
    off.noteId = span.note.noteId;
    eraseSounding(sounding, off);
  }
}

// Gather + edit in the same order as LoopPasses::materializeToEventVector. Tick-sort is only
// applied for resolveWindow's deterministic ResolvedEvent sequence (reconstruct is order-sensitive).
TRACK_COLD_MEM void gatherActiveResolvedEvents(const LoopPasses& passes, uint32_t loopLengthTicks,
                                uint32_t windowStart, uint32_t windowLength,
                                SessionMidiEventVec& out, ResolutionCostCounters* counters) {
  out.clear();
  if (loopLengthTicks == 0 || windowLength == 0) {
    return;
  }

  std::vector<const CommittedChunkIdList*> lists;
  collectActiveChunkLists(passes, lists);
  if (lists.empty()) {
    return;
  }

  const bool wholeLoop = windowLength >= loopLengthTicks;
  if (wholeLoop) {
    appendActiveCapturePassesMerged(passes, out);
  } else {
    // Stages 1–5 fallback. Indexed find is LoopContentResolution::resolveWindow(TickIndex, …).
    CommittedEventRange::inWindow(lists.data(), lists.size(), loopLengthTicks, windowStart,
                                  windowLength)
        .appendTo(out);
  }
  bumpOps(counters, static_cast<uint32_t>(out.size() + lists.size()));
  applyActiveEdits(out, passes, loopLengthTicks);
}

}  // namespace

struct EventRef {
  PassId passId = kInvalidPassId;
  uint32_t eventIndex = 0;
  bool operator<(const EventRef& other) const {
    return passId < other.passId || (passId == other.passId && eventIndex < other.eventIndex);
  }
};

TRACK_COLD_MEM void pairNotesInPassRange(LoopContentResolution::TickIndex::CapturePassEntry& pass,
                                         LoopContentResolution::TickIndex::ByNoteIdMap& byNoteId,
                                         uint32_t beginEvent, uint32_t endEventExclusive,
                                         std::map<uint8_t, std::vector<uint32_t>>& openOnByPitch) {
  const uint32_t limit = static_cast<uint32_t>(pass.events.size());
  if (beginEvent >= limit) {
    return;
  }
  if (endEventExclusive > limit) {
    endEventExclusive = limit;
  }
  for (uint32_t i = beginEvent; i < endEventExclusive; ++i) {
    const MidiEvent& event = pass.events[i];
    if (event.isNoteOn()) {
      openOnByPitch[event.data.noteData.note].push_back(i);
      if (event.noteId != kInvalidNoteId) {
        LoopContentResolution::TickIndex::NoteLocation loc;
        loc.passId = pass.id;
        loc.onIndex = i;
        loc.offIndex = -1;
        byNoteId[event.noteId] = loc;
      }
      continue;
    }
    if (!event.isNoteOff()) {
      continue;
    }
    std::vector<uint32_t>& stack = openOnByPitch[event.data.noteData.note];
    if (stack.empty()) {
      continue;
    }
    const uint32_t onIndex = stack.back();
    stack.pop_back();
    const NoteId noteId = pass.events[onIndex].noteId;
    if (noteId == kInvalidNoteId) {
      continue;
    }
    auto found = byNoteId.find(noteId);
    if (found != byNoteId.end() && found->second.passId == pass.id) {
      found->second.offIndex = static_cast<int32_t>(i);
    }
  }
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

TRACK_COLD_MEM void visitTickRange(const LoopContentResolution::TickIndex& index, uint32_t beginTick,
                    uint32_t endTickExclusive, std::set<EventRef>& refs,
                    ResolutionCostCounters* counters) {
  auto it = index.byTick.lower_bound(beginTick);
  const auto stop = index.byTick.lower_bound(endTickExclusive);
  for (; it != stop; ++it) {
    if (counters != nullptr) {
      counters->indexEntriesVisited += 1;
    }
    const PassId passId = it->second.first;
    const auto* pass = findPass(index, passId);
    if (pass == nullptr || pass->state != CapturePassState::Active) {
      continue;
    }
    EventRef ref;
    ref.passId = passId;
    ref.eventIndex = it->second.second;
    refs.insert(ref);
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
  const uint32_t limit = static_cast<uint32_t>(pass->events.size());
  if (beginEvent >= limit) {
    return;
  }
  if (endEventExclusive > limit) {
    endEventExclusive = limit;
  }
  for (uint32_t i = beginEvent; i < endEventExclusive; ++i) {
    byTick.emplace(pass->events[i].tick, std::make_pair(id, i));
  }
  if (counters != nullptr && endEventExclusive > beginEvent) {
    counters->resolutionOperations += endEventExclusive - beginEvent;
  }
}

TRACK_COLD_MEM void LoopContentResolution::TickIndex::pairCapturePassNotes(PassId id) {
  CapturePassEntry* pass = findPassMutable(*this, id);
  if (pass == nullptr) {
    return;
  }
  std::map<uint8_t, std::vector<uint32_t>> openOnByPitch;
  pairNotesInPassRange(*pass, byNoteId, 0, static_cast<uint32_t>(pass->events.size()),
                       openOnByPitch);
}

TRACK_COLD_MEM void LoopContentResolution::TickIndex::pairCapturePassEventRange(
    PassId id, uint32_t beginEvent, uint32_t endEventExclusive,
    std::map<uint8_t, std::vector<uint32_t>>& openOnByPitch, ResolutionCostCounters* counters) {
  CapturePassEntry* pass = findPassMutable(*this, id);
  if (pass == nullptr) {
    return;
  }
  const uint32_t before = beginEvent;
  pairNotesInPassRange(*pass, byNoteId, beginEvent, endEventExclusive, openOnByPitch);
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
  pairCapturePassNotes(id);
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
  out.clear();
  if (loopLengthTicks == 0 || windowLength == 0) {
    return;
  }
  std::set<EventRef> refs;
  if (windowLength >= loopLengthTicks) {
    visitTickRange(*this, 0, loopLengthTicks, refs, counters);
  } else {
    const uint32_t start = IntervalProjection::tickPhaseInLoop(windowStart, 0, loopLengthTicks);
    if (start + windowLength <= loopLengthTicks) {
      visitTickRange(*this, start, start + windowLength, refs, counters);
    } else {
      visitTickRange(*this, start, loopLengthTicks, refs, counters);
      visitTickRange(*this, 0, start + windowLength - loopLengthTicks, refs, counters);
    }
  }
  out.reserve(refs.size());
  for (const EventRef& ref : refs) {
    const auto* pass = findPass(*this, ref.passId);
    if (pass == nullptr || ref.eventIndex >= pass->events.size()) {
      continue;
    }
    out.push_back(pass->events[ref.eventIndex]);
  }
}

TRACK_COLD_MEM void LoopContentResolution::TickIndex::appendNoteEvents(NoteId noteId, SessionMidiEventVec& out) const {
  if (noteId == kInvalidNoteId) {
    return;
  }
  const auto found = byNoteId.find(noteId);
  if (found == byNoteId.end()) {
    return;
  }
  const auto* pass = findPass(*this, found->second.passId);
  if (pass == nullptr || pass->state != CapturePassState::Active) {
    return;
  }
  if (found->second.onIndex < pass->events.size()) {
    out.push_back(pass->events[found->second.onIndex]);
  }
  if (found->second.offIndex >= 0 &&
      static_cast<uint32_t>(found->second.offIndex) < pass->events.size()) {
    out.push_back(pass->events[static_cast<uint32_t>(found->second.offIndex)]);
  }
}

TRACK_COLD_MEM void LoopContentResolution::TickIndex::materializeActive(SessionMidiEventVec& out) const {
  out.clear();
  std::vector<const CapturePassEntry*> ordered;
  ordered.reserve(capturePasses.size());
  const CapturePassEntry* record = nullptr;
  for (const CapturePassEntry& pass : capturePasses) {
    if (pass.state != CapturePassState::Active || pass.events.empty()) {
      continue;
    }
    if (pass.mergeSequence == 0 && record == nullptr) {
      record = &pass;
      continue;
    }
    ordered.push_back(&pass);
  }
  if (record != nullptr) {
    out = record->events;
  }
  std::sort(ordered.begin(), ordered.end(),
            [](const CapturePassEntry* a, const CapturePassEntry* b) {
              return a->mergeSequence < b->mergeSequence;
            });
  for (const CapturePassEntry* pass : ordered) {
    SessionMidiEventVec layer = pass->events;
    mergeSortedMidiVectors(out, std::move(layer));
  }
}

TRACK_COLD_MEM uint32_t LoopContentResolution::TickIndex::indexedEventCount() const {
  return static_cast<uint32_t>(byTick.size());
}

TRACK_COLD_MEM uint32_t LoopContentResolution::TickIndex::indexedPassCount() const {
  return static_cast<uint32_t>(capturePasses.size());
}

TRACK_COLD_MEM void LoopContentResolution::resolveWindow(const TickIndex& index, const EditPassVec& editPasses,
                                          uint32_t loopLengthTicks, uint32_t windowStart,
                                          uint32_t windowLength, SessionMidiEventVec& out,
                                          ResolutionCostCounters* counters) {
  ElapsedTimer timer;
  SessionMidiEventVec working;
  index.findRawWindow(loopLengthTicks, windowStart, windowLength, working, counters);
  EditPassVec activeRows;
  for (const EditPass& editPass : editPasses) {
    if (editPass.state == EditPassState::Active && editPass.passType == EditPassType::Note) {
      activeRows.push_back(editPass);
      index.appendNoteEvents(editPass.targetNoteId, working);
    }
  }
  sortResolvedEvents(working);
  working.erase(std::unique(working.begin(), working.end(),
                            [](const MidiEvent& a, const MidiEvent& b) {
                              return a.tick == b.tick && a.type == b.type && a.channel == b.channel &&
                                     a.data.noteData.note == b.data.noteData.note &&
                                     a.noteId == b.noteId;
                            }),
                working.end());
  if (!activeRows.empty()) {
    applyNoteEditPassSequence(working, activeRows, loopLengthTicks);
  }
  DisplayWindowUtils::filterMidiEventsToWindow(working, out, windowStart, windowLength,
                                               loopLengthTicks);
  sortResolvedEvents(out);
  if (counters != nullptr) {
    counters->candidateEvents = static_cast<uint32_t>(working.size());
    counters->eventsInQueryWindow = static_cast<uint32_t>(out.size());
    counters->eventsInHistory = index.indexedEventCount();
    counters->passesInHistory = index.indexedPassCount();
    counters->elapsedMicros = timer.elapsed();
  }
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
                                         uint32_t tick, SoundingNoteVec& out,
                                         ResolutionCostCounters* counters) {
  out.clear();
  SessionMidiEventVec events;
  gatherActiveResolvedEvents(passes, loopLengthTicks, 0, loopLengthTicks, events, counters);
  const NoteUtils::DisplayNoteVec notes =
      NoteUtils::reconstructDisplayNotes(events, loopLengthTicks, false);
  bumpOps(counters, static_cast<uint32_t>(notes.size()));
  for (const NoteUtils::DisplayNote& note : notes) {
    if (!noteSoundsAt(note, tick, loopLengthTicks)) {
      continue;
    }
    SoundingNote sounding{};
    sounding.channel = channelForNoteId(events, note.noteId);
    sounding.pitch = note.note;
    sounding.noteId = note.noteId;
    sounding.onTick = note.startTick;
    out.push_back(sounding);
  }
}

TRACK_COLD_MEM bool LoopContentResolution::StateCheckpoints::prepareRebuildResolvedEvents(
    const TickIndex& index, const EditPassVec& editPasses, uint32_t loopLength,
    uint32_t checkpointIntervalTicks, SessionMidiEventVec& resolved,
    ResolutionCostCounters* counters) {
  intervalTicks = checkpointIntervalTicks;
  loopLengthTicks = loopLength;
  soundingAt.clear();
  spans.clear();
  startsByTick.clear();
  resolved.clear();
  if (loopLength == 0 || checkpointIntervalTicks == 0) {
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
  for (uint32_t i = begin; i < endExclusive; ++i) {
    const NoteUtils::DisplayNote& note = notes[i];
    NoteSpan span{};
    span.note.channel = channelForNoteId(resolved, note.noteId);
    span.note.pitch = note.note;
    span.note.noteId = note.noteId;
    span.note.onTick = note.startTick;
    span.startTick = note.startTick;
    span.endTick = note.endTick;
    const size_t spanIndex = spans.size();
    spans.push_back(span);
    startsByTick.emplace(span.startTick, spanIndex);
    startsByTick.emplace(span.endTick, spanIndex);
  }
  if (counters != nullptr) {
    counters->eventsInHistory = static_cast<uint32_t>(resolved.size());
  }
  return true;
}

TRACK_COLD_MEM bool LoopContentResolution::StateCheckpoints::finishRebuildSpansFromEvents(
    const SessionMidiEventVec& resolved, ResolutionCostCounters* counters) {
  spans.clear();
  startsByTick.clear();
  soundingAt.clear();
  if (loopLengthTicks == 0 || intervalTicks == 0) {
    return true;
  }
  const NoteUtils::DisplayNoteVec notes =
      NoteUtils::reconstructDisplayNotes(resolved, loopLengthTicks, false);
  if (!appendSpansFromNotes(resolved, notes, 0, static_cast<uint32_t>(notes.size()), counters)) {
    return false;
  }
  uint32_t count = loopLengthTicks / intervalTicks;
  if (count == 0) {
    count = 1;
  }
  soundingAt.resize(count);
  if (counters != nullptr) {
    counters->checkpointIntervalTicks = intervalTicks;
    counters->checkpointCount = static_cast<uint32_t>(soundingAt.size());
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
  if (intervalTicks == 0 || loopLengthTicks == 0 || soundingAt.empty()) {
    return true;
  }
  if (endIndexExclusive > soundingAt.size()) {
    endIndexExclusive = static_cast<uint32_t>(soundingAt.size());
  }
  for (uint32_t i = beginIndex; i < endIndexExclusive; ++i) {
    const uint32_t checkpointTick = i * intervalTicks;
    for (const NoteSpan& span : spans) {
      NoteUtils::DisplayNote probe{};
      probe.noteId = span.note.noteId;
      probe.note = span.note.pitch;
      probe.startTick = span.startTick;
      probe.endTick = span.endTick;
      if (!noteSoundsAt(probe, checkpointTick, loopLengthTicks)) {
        continue;
      }
      soundingAt[i].push_back(span.note);
    }
  }
  if (counters != nullptr) {
    counters->checkpointIntervalTicks = intervalTicks;
    counters->checkpointCount = static_cast<uint32_t>(soundingAt.size());
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
  fillCheckpointRange(0, static_cast<uint32_t>(soundingAt.size()), counters);
}

TRACK_COLD_MEM bool seedResolveStateFromCheckpoint(
    const LoopContentResolution::StateCheckpoints& checkpoints, uint32_t tick, SoundingNoteVec& out,
    uint32_t& replayStart) {
  out.clear();
  if (checkpoints.intervalTicks == 0 || checkpoints.loopLengthTicks == 0 ||
      checkpoints.soundingAt.empty()) {
    return false;
  }
  const uint32_t queryTick =
      IntervalProjection::tickPhaseInLoop(tick, 0, checkpoints.loopLengthTicks);
  replayStart = (queryTick / checkpoints.intervalTicks) * checkpoints.intervalTicks;
  uint32_t checkpointIndex = replayStart / checkpoints.intervalTicks;
  if (checkpointIndex >= checkpoints.soundingAt.size()) {
    checkpointIndex = static_cast<uint32_t>(checkpoints.soundingAt.size() - 1);
    replayStart = checkpointIndex * checkpoints.intervalTicks;
  }
  out = checkpoints.soundingAt[checkpointIndex];
  return true;
}

TRACK_COLD_MEM void writeResolveStateCounters(const LoopContentResolution::StateCheckpoints& checkpoints,
                                              uint32_t replayStart, uint32_t replayed,
                                              ResolutionCostCounters* counters) {
  if (counters == nullptr) {
    return;
  }
  counters->checkpointIntervalTicks = checkpoints.intervalTicks;
  counters->checkpointCount = static_cast<uint32_t>(checkpoints.soundingAt.size());
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

TRACK_COLD_MEM void LoopContentResolution::StateCheckpoints::resolveState(uint32_t tick,
                                                                         SoundingNoteVec& out,
                                                                         ResolutionCostCounters* counters) const {
  uint32_t replayStart = 0;
  if (!seedResolveStateFromCheckpoint(*this, tick, out, replayStart)) {
    return;
  }
  const uint32_t queryTick = IntervalProjection::tickPhaseInLoop(tick, 0, loopLengthTicks);
  uint32_t replayed = 0;
  auto applyRange = [&](uint32_t beginTick, uint32_t endTickExclusive) {
    auto it = startsByTick.lower_bound(beginTick);
    const auto stop = startsByTick.lower_bound(endTickExclusive);
    for (; it != stop; ++it) {
      replayed += 1;
      if (it->second >= spans.size()) {
        continue;
      }
      applySpanBoundaryAtTick(out, spans[it->second], it->first);
    }
  };
  applyRange(replayStart + 1, queryTick + 1);
  writeResolveStateCounters(*this, replayStart, replayed, counters);
}

TRACK_COLD_MEM void LoopContentResolution::StateCheckpoints::resolveStateFromSpanBoundaries(
    const SpanBoundaryEntryVec& entries, uint32_t tick, SoundingNoteVec& out,
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
                                         SoundingNoteVec& out, ResolutionCostCounters* counters) {
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
    reconSpansFinished = false;
    reconEventCursor = 0;
    reconProjectCursor = 0;
    reconProjected = NoteUtils::DisplayNoteVec{};
    reconBuild.clear();
    lastStepName = "idle";
    lastLoggedStep = nullptr;
    lastPhaseLogUs = 0;
    index = LoopContentResolution::TickIndex{};
    checkpoints = LoopContentResolution::StateCheckpoints{};
    rebuildEvents = SessionMidiEventVec{};
    rebuildNotes = NoteUtils::DisplayNoteVec{};
    sample_ = LoopContentResolution::DeviceGateSample{};
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
    phase = Phase::RebuildPrepare;
    lastStepName = "prep";
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
                               : static_cast<unsigned>(indexEventCursor);
    const unsigned notesLogged = (name == kProjStep)
                                     ? static_cast<unsigned>(reconProjected.size())
                                     : static_cast<unsigned>(rebuildNotes.size());
    snprintf(line, cap, "#CAP,%lu,DIAG,lcr,phase,%s,pass,%u,ev,%u,span,%u,notes,%u", stamp, name,
             static_cast<unsigned>(indexPassCursor), evLogged,
             static_cast<unsigned>(rebuildSpanCursor), notesLogged);
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
        if (indexPassCursor == 0 && countIndexCommitPasses(passes) == 0) {
          advancePastIndexCommit();
          return LoopContentResolution::DeviceGateSliceResult::Continue;
        }
        if (indexPassCursor >= countIndexCommitPasses(passes)) {
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
        index.indexCapturePassEventRange(passRef.id, indexEventCursor, end, &sample_.indexCommit);
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
        if (!checkpoints.prepareRebuildResolvedEvents(index, passes.editPasses, loopLengthTicks,
                                                      checkpointInterval, rebuildEvents,
                                                      &sample_.rebuild)) {
          reset();
          return LoopContentResolution::DeviceGateSliceResult::Inactive;
        }
        sample_.rebuild.elapsedMicros += timer.elapsed();
        lastStepName = "prep";
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
          checkpoints.startsByTick.clear();
          checkpoints.soundingAt.clear();
          rebuildNotes = NoteUtils::dedupeProjectedDisplayNotes(reconProjected);
          reconProjected = NoteUtils::DisplayNoteVec{};
          reconBuild.clear();
          rebuildSpanCursor = 0;
          rebuildNotesReady = true;
          lastStepName = kDedupStep;
          sample_.rebuild.elapsedMicros += timer.elapsed();
          return LoopContentResolution::DeviceGateSliceResult::Continue;
        }
        const uint32_t noteCount = static_cast<uint32_t>(rebuildNotes.size());
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
        }
        if (rebuildSpanCursor >= noteCount) {
          uint32_t count = checkpoints.loopLengthTicks / checkpoints.intervalTicks;
          if (count == 0) {
            count = 1;
          }
          checkpoints.soundingAt.resize(count);
          sample_.rebuild.checkpointIntervalTicks = checkpoints.intervalTicks;
          sample_.rebuild.checkpointCount = static_cast<uint32_t>(checkpoints.soundingAt.size());
          rebuildEvents = SessionMidiEventVec{};
          rebuildNotes = NoteUtils::DisplayNoteVec{};
          checkpointCursor = 0;
          phase = Phase::RebuildCheckpoints;
        }
        sample_.rebuild.elapsedMicros += timer.elapsed();
        return LoopContentResolution::DeviceGateSliceResult::Continue;
      }
      case Phase::RebuildCheckpoints: {
        constexpr uint32_t kCheckpointsPerSlice = 1;
        const uint32_t total = static_cast<uint32_t>(checkpoints.soundingAt.size());
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
        SoundingNoteVec sounding;
        LoopContentResolution::resolveState(checkpoints, highTick, sounding, &sample_.state);
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
  bool pairPassOpen = false;
  uint32_t pairEventCursor = 0;
  std::map<uint8_t, std::vector<uint32_t>> pairOpenOnByPitch;
  uint32_t checkpointCursor = 0;
  uint32_t rebuildSpanCursor = 0;
  bool rebuildNotesReady = false;
  bool reconSpansFinished = false;
  uint32_t reconEventCursor = 0;
  uint32_t reconProjectCursor = 0;
  NoteUtils::DisplayNoteVec reconProjected;
  NoteUtils::CanonicalSpanBuild reconBuild;
  const char* lastStepName = "idle";
  const char* lastLoggedStep = nullptr;
  uint32_t lastPhaseLogUs = 0;
  LoopContentResolution::TickIndex index;
  LoopContentResolution::StateCheckpoints checkpoints;
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
DeviceGateSession sDeviceGateSession;

}  // namespace

bool LoopContentResolution::deviceGateFinished() { return sDeviceGateFinished; }

bool LoopContentResolution::deviceGateActive() { return sDeviceGateSession.isActive(); }

void LoopContentResolution::deviceGateBegin(uint32_t loopLengthTicks) {
  sDeviceGateSession.begin(loopLengthTicks);
}

void LoopContentResolution::deviceGateReset() { sDeviceGateSession.reset(); }

void LoopContentResolution::deviceGateComplete() {
  sDeviceGateSession.reset();
  sDeviceGateFinished = true;
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
           "#CAP,%lu,DIAG,lcr,mat=%lu,win=%lu,reb=%lu,st=%lu,rep=%u,hist=%u,walk=%u", stamp,
           static_cast<unsigned long>(sample.materialize.elapsedMicros),
           static_cast<unsigned long>(sample.window.elapsedMicros),
           static_cast<unsigned long>(sample.rebuild.elapsedMicros),
           static_cast<unsigned long>(sample.state.elapsedMicros),
           static_cast<unsigned>(sample.state.eventsReplayed),
           static_cast<unsigned>(sample.state.eventsInHistory),
           static_cast<unsigned>(sample.window.passChunkListsWalked));
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
