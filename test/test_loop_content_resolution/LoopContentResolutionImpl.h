//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Native prototype implementation (DEC-037 Stages 1–6). Included into the test TU.
// Not compiled into firmware yet.

#include "LoopContentResolution.h"

#include "CommittedEventRange.h"
#include "EditApply.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/IntervalProjection.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iterator>
#include <map>
#include <set>
#include <vector>

namespace {

void collectActiveChunkLists(const LoopPasses& passes,
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

void mergeSortedMidiVectors(SessionMidiEventVec& base, SessionMidiEventVec&& addition) {
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

void appendActiveCapturePassesMerged(const LoopPasses& passes, SessionMidiEventVec& out) {
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

void applyActiveEdits(SessionMidiEventVec& events, const LoopPasses& passes,
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

void bumpOps(ResolutionCostCounters* counters, uint32_t amount) {
  if (counters != nullptr) {
    counters->resolutionOperations += amount;
  }
}

bool resolvedEventLess(const MidiEvent& a, const MidiEvent& b) {
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

void sortResolvedEvents(SessionMidiEventVec& events) {
  std::sort(events.begin(), events.end(), resolvedEventLess);
}

bool noteSoundsAt(const NoteUtils::DisplayNote& note, uint32_t tick, uint32_t loopLengthTicks) {
  if (NoteUtils::isWrappedLoopNotePair(note.startTick, note.endTick, loopLengthTicks)) {
    return tick >= note.startTick || tick < note.endTick;
  }
  return tick >= note.startTick && tick < note.endTick;
}

uint8_t channelForNoteId(const SessionMidiEventVec& events, NoteId noteId) {
  for (const MidiEvent& event : events) {
    if (event.isNoteOn() && event.noteId == noteId) {
      return event.channel;
    }
  }
  return 0;
}

// Gather + edit in the same order as LoopPasses::materializeToEventVector. Tick-sort is only
// applied for resolveWindow's deterministic ResolvedEvent sequence (reconstruct is order-sensitive).
void gatherActiveResolvedEvents(const LoopPasses& passes, uint32_t loopLengthTicks,
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

void pairNotesInPass(LoopContentResolution::TickIndex::CapturePassEntry& pass,
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

const LoopContentResolution::TickIndex::CapturePassEntry* findPass(
    const LoopContentResolution::TickIndex& index, PassId id) {
  const auto found = index.passById.find(id);
  if (found == index.passById.end() || found->second >= index.capturePasses.size()) {
    return nullptr;
  }
  return &index.capturePasses[found->second];
}

void visitTickRange(const LoopContentResolution::TickIndex& index, uint32_t beginTick,
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

void LoopContentResolution::TickIndex::commitCapturePass(PassId id, const CommittedChunkIdList& chunks,
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

void LoopContentResolution::TickIndex::setCapturePassState(PassId id, CapturePassState state) {
  const auto found = passById.find(id);
  if (found == passById.end() || found->second >= capturePasses.size()) {
    return;
  }
  capturePasses[found->second].state = state;
}

void LoopContentResolution::TickIndex::findRawWindow(uint32_t loopLengthTicks, uint32_t windowStart,
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

void LoopContentResolution::TickIndex::appendNoteEvents(NoteId noteId, SessionMidiEventVec& out) const {
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

uint32_t LoopContentResolution::TickIndex::indexedEventCount() const {
  return static_cast<uint32_t>(byTick.size());
}

uint32_t LoopContentResolution::TickIndex::indexedPassCount() const {
  return static_cast<uint32_t>(capturePasses.size());
}

void LoopContentResolution::resolveWindow(const TickIndex& index, const EditPassVec& editPasses,
                                          uint32_t loopLengthTicks, uint32_t windowStart,
                                          uint32_t windowLength, SessionMidiEventVec& out,
                                          ResolutionCostCounters* counters) {
  const auto started = std::chrono::steady_clock::now();
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
    counters->elapsedMicros = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                                              started)
            .count());
  }
}

void LoopContentResolution::resolveWindow(const LoopPasses& passes, uint32_t loopLengthTicks,
                                          uint32_t windowStart, uint32_t windowLength,
                                          SessionMidiEventVec& out,
                                          ResolutionCostCounters* counters) {
  const auto started = std::chrono::steady_clock::now();
  gatherActiveResolvedEvents(passes, loopLengthTicks, windowStart, windowLength, out, counters);
  sortResolvedEvents(out);
  if (counters != nullptr) {
    counters->eventsInQueryWindow = static_cast<uint32_t>(out.size());
    counters->candidateEvents = counters->eventsInQueryWindow;
    counters->elapsedMicros = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                                              started)
            .count());
  }
}

void LoopContentResolution::resolveState(const LoopPasses& passes, uint32_t loopLengthTicks,
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

void LoopContentResolution::resolveNotes(const LoopPasses& passes, uint32_t loopLengthTicks,
                                         uint32_t windowStart, uint32_t windowLength,
                                         NoteUtils::DisplayNoteVec& out,
                                         ResolutionCostCounters* counters) {
  SessionMidiEventVec events;
  gatherActiveResolvedEvents(passes, loopLengthTicks, windowStart, windowLength, events, counters);
  out = NoteUtils::reconstructDisplayNotes(events, loopLengthTicks, false);
}
