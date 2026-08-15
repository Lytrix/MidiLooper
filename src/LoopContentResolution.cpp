//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// DEC-037 LoopContentResolution implementation.
// Native tests include this TU. Teensy links via linker/imxrt1062_t41_lcr.ld when referenced.

#include "LoopContentResolution.h"

#include "CommittedEventRange.h"
#include "EditApply.h"
#include "Globals.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/IntervalProjection.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/MemoryPressureLevel.h"
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

TRACK_COLD_MEM void pairNotesInPass(LoopContentResolution::TickIndex::CapturePassEntry& pass,
                     std::unordered_map<NoteId, LoopContentResolution::TickIndex::NoteLocation>& byNoteId) {
  std::map<uint8_t, std::vector<uint32_t>> openOnByPitch;
  for (uint32_t i = 0; i < static_cast<uint32_t>(pass.events.size()); ++i) {
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

TRACK_COLD_MEM void LoopContentResolution::TickIndex::commitCapturePass(PassId id, const CommittedChunkIdList& chunks,
                                                         CapturePassState state, uint32_t mergeSequence,
                                                         ResolutionCostCounters* counters) {
  if (id == kInvalidPassId) {
    return;
  }
  CapturePassEntry pass;
  pass.id = id;
  pass.mergeSequence = mergeSequence;
  pass.state = state;
  LoopEventStore::appendChunkRefEvents(chunks, pass.events);
  if (counters != nullptr) {
    counters->passChunkListsWalked += 1;
    counters->resolutionOperations += static_cast<uint32_t>(pass.events.size());
  }

  pairNotesInPass(pass, byNoteId);
  for (uint32_t i = 0; i < static_cast<uint32_t>(pass.events.size()); ++i) {
    byTick.emplace(pass.events[i].tick, std::make_pair(id, i));
  }

  passById[id] = capturePasses.size();
  capturePasses.push_back(std::move(pass));
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

TRACK_COLD_MEM void LoopContentResolution::StateCheckpoints::prepareRebuildSpans(
    const TickIndex& index, const EditPassVec& editPasses, uint32_t loopLength,
    uint32_t checkpointIntervalTicks, ResolutionCostCounters* counters) {
  intervalTicks = checkpointIntervalTicks;
  loopLengthTicks = loopLength;
  soundingAt.clear();
  spans.clear();
  startsByTick.clear();
  if (loopLength == 0 || checkpointIntervalTicks == 0) {
    return;
  }
  SessionMidiEventVec resolved;
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
  }
  const NoteUtils::DisplayNoteVec notes =
      NoteUtils::reconstructDisplayNotes(resolved, loopLength, false);
  spans.reserve(notes.size());
  for (const NoteUtils::DisplayNote& note : notes) {
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
  const uint32_t count = loopLength / checkpointIntervalTicks;
  soundingAt.resize(count);
  if (counters != nullptr) {
    counters->checkpointIntervalTicks = intervalTicks;
    counters->checkpointCount = static_cast<uint32_t>(soundingAt.size());
    counters->eventsInHistory = static_cast<uint32_t>(resolved.size());
  }
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
    if (MemoryMonitor::getAdvisoryPressureLevel() >= MemoryPressureLevel::Low) {
      soundingAt[i].clear();
      return false;
    }
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

TRACK_COLD_MEM void LoopContentResolution::StateCheckpoints::resolveState(uint32_t tick, SoundingNoteVec& out,
                                                           ResolutionCostCounters* counters) const {
  out.clear();
  if (intervalTicks == 0 || loopLengthTicks == 0 || soundingAt.empty()) {
    return;
  }
  const uint32_t queryTick = IntervalProjection::tickPhaseInLoop(tick, 0, loopLengthTicks);
  uint32_t replayStart = (queryTick / intervalTicks) * intervalTicks;
  uint32_t checkpointIndex = replayStart / intervalTicks;
  if (checkpointIndex >= soundingAt.size()) {
    checkpointIndex = static_cast<uint32_t>(soundingAt.size() - 1);
    replayStart = checkpointIndex * intervalTicks;
  }
  out = soundingAt[checkpointIndex];
  uint32_t replayed = 0;
  auto applyRange = [&](uint32_t beginTick, uint32_t endTickExclusive) {
    auto it = startsByTick.lower_bound(beginTick);
    const auto stop = startsByTick.lower_bound(endTickExclusive);
    for (; it != stop; ++it) {
      replayed += 1;
      if (it->second >= spans.size()) {
        continue;
      }
      const NoteSpan& span = spans[it->second];
      if (it->first == span.startTick) {
        upsertSounding(out, span.note);
      }
      if (it->first == span.endTick) {
        MidiEvent off = MidiEvent::NoteOff(span.endTick, span.note.channel, span.note.pitch, 0);
        off.noteId = span.note.noteId;
        eraseSounding(out, off);
      }
    }
  };
  applyRange(replayStart + 1, queryTick + 1);
  if (counters != nullptr) {
    counters->checkpointIntervalTicks = intervalTicks;
    counters->checkpointCount = static_cast<uint32_t>(soundingAt.size());
    counters->replayStartTick = replayStart;
    counters->eventsReplayed = replayed;
    counters->eventsInHistory = static_cast<uint32_t>(spans.size());
    counters->passChunkListsWalked = 0;
  }
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
TRACK_COLD_MEM bool commitIndexPassAtCursor(LoopContentResolution::TickIndex& index,
                                            const LoopPasses& passes, size_t cursor,
                                            ResolutionCostCounters* counters);

struct DeviceGateSession {
  enum class Phase : uint8_t {
    Idle,
    IndexCommit,
    Materialize,
    Window,
    RebuildPrepare,
    RebuildCheckpoints,
    State,
    Done,
  };

  bool isActive() const { return phase != Phase::Idle && phase != Phase::Done; }

  void reset() {
    phase = Phase::Idle;
    loopLengthTicks = 0;
    indexPassCursor = 0;
    checkpointCursor = 0;
    index = LoopContentResolution::TickIndex{};
    checkpoints = LoopContentResolution::StateCheckpoints{};
    sample_ = LoopContentResolution::DeviceGateSample{};
  }

  void begin(uint32_t ticks) {
    reset();
    if (ticks == 0) {
      return;
    }
    loopLengthTicks = ticks;
    phase = Phase::IndexCommit;
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
#if defined(ARDUINO)
          phase = Phase::RebuildPrepare;
#else
          phase = Phase::Materialize;
#endif
          return LoopContentResolution::DeviceGateSliceResult::Continue;
        }
        ElapsedTimer timer;
        if (!commitIndexPassAtCursor(index, passes, indexPassCursor, &sample_.indexCommit)) {
          reset();
          return LoopContentResolution::DeviceGateSliceResult::Inactive;
        }
      sample_.indexCommit.elapsedMicros += timer.elapsed();
      indexPassCursor += 1;
      if (indexPassCursor >= countIndexCommitPasses(passes)) {
#if defined(ARDUINO)
        // Device idle gate: skip full materialize/window oracle phases — each blocks seconds on
        // 64+ bar loops and stalls MIDI/OLED (OpenSpec device worst-case gate).
        phase = Phase::RebuildPrepare;
#else
        phase = Phase::Materialize;
#endif
      }
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
        checkpoints.prepareRebuildSpans(index, passes.editPasses, loopLengthTicks,
                                        checkpointInterval, &sample_.rebuild);
        sample_.rebuild.elapsedMicros += timer.elapsed();
        checkpointCursor = 0;
        phase = Phase::RebuildCheckpoints;
        return LoopContentResolution::DeviceGateSliceResult::Continue;
      }
      case Phase::RebuildCheckpoints: {
        constexpr uint32_t kCheckpointsPerSlice = 1;
        const uint32_t total = static_cast<uint32_t>(checkpoints.soundingAt.size());
        if (checkpointCursor >= total) {
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
  uint32_t checkpointCursor = 0;
  LoopContentResolution::TickIndex index;
  LoopContentResolution::StateCheckpoints checkpoints;
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

TRACK_COLD_MEM bool commitIndexPassAtCursor(LoopContentResolution::TickIndex& index,
                                            const LoopPasses& passes, size_t cursor,
                                            ResolutionCostCounters* counters) {
  size_t seen = 0;
  if (passes.hasRecordPass() && !passes.recordPass.committedChunkIds.empty()) {
    if (cursor == seen) {
      index.commitCapturePass(passes.recordPass.id, passes.recordPass.committedChunkIds,
                              passes.recordPass.state, 0, counters);
      return true;
    }
    seen += 1;
  }
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (cursor == seen) {
      index.commitCapturePass(pass.id, pass.committedChunkIds, pass.state, pass.mergeSequence,
                              counters);
      return true;
    }
    seen += 1;
  }
  return false;
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
