//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Native prototype implementation (DEC-037 Stages 1–5). Included into the test TU.
// Not compiled into firmware yet.

#include "LoopContentResolution.h"

#include "CommittedEventRange.h"
#include "EditApply.h"

#include <algorithm>
#include <chrono>
#include <iterator>
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
    // Stage 1–5: windowed chunk walk. Stage 6 must replace this with a tick index so
    // find does not iterate every pass list.
    CommittedEventRange::inWindow(lists.data(), lists.size(), loopLengthTicks, windowStart,
                                  windowLength)
        .appendTo(out);
  }
  bumpOps(counters, static_cast<uint32_t>(out.size() + lists.size()));
  applyActiveEdits(out, passes, loopLengthTicks);
}

}  // namespace

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
