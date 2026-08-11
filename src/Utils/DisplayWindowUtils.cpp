//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/DisplayWindowUtils.h"
#include "Globals.h"
#include "Utils/IntervalProjection.h"

#include <cstdio>

namespace DisplayWindowUtils {

using DisplayNoteVec = NoteUtils::DisplayNoteVec;

namespace {

uint32_t normalizeTick(uint32_t tick, uint32_t loopLength) {
  return IntervalProjection::tickPhaseInLoop(tick, 0, loopLength);
}

TickInterval displayNoteSpanInLoop(const NoteUtils::DisplayNote& note, uint32_t loopLength) {
  if (loopLength == 0) {
    return TickInterval{};
  }
  const uint32_t noteStart = normalizeTick(note.startTick, loopLength);
  uint32_t noteEndInclusive = normalizeTick(note.endTick, loopLength);
  int32_t exclusiveEnd = static_cast<int32_t>(noteEndInclusive) + 1;
  if (noteEndInclusive <= noteStart) {
    exclusiveEnd += static_cast<int32_t>(loopLength);
  }
  return TickInterval{static_cast<int32_t>(noteStart), exclusiveEnd};
}

TickInterval viewportInLoop(const TickInterval& viewport, uint32_t loopLength) {
  if (loopLength == 0 || viewport.length() <= 0) {
    return TickInterval{};
  }
  const uint32_t windowLength = static_cast<uint32_t>(viewport.length());
  if (windowLength >= loopLength) {
    return TickInterval{0, static_cast<int32_t>(loopLength)};
  }
  const uint32_t start = normalizeTick(static_cast<uint32_t>(viewport.start), loopLength);
  const uint32_t end = normalizeTick(start + windowLength, loopLength);
  if (start < end) {
    return TickInterval{static_cast<int32_t>(start), static_cast<int32_t>(end)};
  }
  return TickInterval{static_cast<int32_t>(start), static_cast<int32_t>(start + windowLength)};
}

bool tickInHalfOpenWindow(uint32_t tick, uint32_t windowStart, uint32_t windowLength,
                          uint32_t loopLength) {
  if (loopLength == 0 || windowLength == 0) {
    return false;
  }
  const uint32_t rel = normalizeTick(tick, loopLength);
  const uint32_t start = normalizeTick(windowStart, loopLength);
  const uint32_t end = normalizeTick(start + windowLength, loopLength);
  if (windowLength >= loopLength) {
    return true;
  }
  if (start < end) {
    return rel >= start && rel < end;
  }
  return rel >= start || rel < end;
}

}  // namespace

TickInterval makeViewportInterval(uint32_t windowStart, uint32_t windowLength) {
  return TickInterval{static_cast<int32_t>(windowStart),
                      static_cast<int32_t>(windowStart + windowLength)};
}

uint32_t chooseBarsPerSegment(uint32_t loopBars, uint32_t segmentCount) {
  if (loopBars == 0 || segmentCount == 0) {
    return 1;
  }
  static constexpr uint32_t kCandidates[] = {1, 2, 4, 8, 16, 32, 64};
  for (uint32_t bars : kCandidates) {
    if ((loopBars + bars - 1) / bars <= segmentCount) {
      return bars;
    }
  }
  return 64;
}

bool noteIntersectsWindow(const TickInterval& noteSpan, const TickInterval& viewport,
                          uint32_t loopLength) {
  if (loopLength == 0 || viewport.length() <= 0) {
    return false;
  }
  const TickInterval loopViewport = viewportInLoop(viewport, loopLength);
  if (loopViewport.length() <= 0) {
    return false;
  }
  if (static_cast<uint32_t>(loopViewport.length()) >= loopLength) {
    return true;
  }
  const uint32_t noteStart = normalizeTick(static_cast<uint32_t>(noteSpan.start), loopLength);
  uint32_t noteEndInclusive = normalizeTick(static_cast<uint32_t>(noteSpan.end - 1), loopLength);
  if (noteSpan.end <= noteSpan.start) {
    noteEndInclusive = normalizeTick(static_cast<uint32_t>(noteSpan.end), loopLength);
  }
  return noteIntersectsWindow(noteStart, noteEndInclusive, static_cast<uint32_t>(loopViewport.start),
                              static_cast<uint32_t>(loopViewport.length()), loopLength);
}

bool noteIntersectsWindow(uint32_t startTick, uint32_t endTick, uint32_t windowStart,
                          uint32_t windowLength, uint32_t loopLength) {
  if (loopLength == 0 || windowLength == 0) {
    return false;
  }
  const uint32_t noteStart = normalizeTick(startTick, loopLength);
  uint32_t noteEnd = normalizeTick(endTick, loopLength);
  if (noteEnd <= noteStart) {
    noteEnd += loopLength;
  }
  for (uint32_t probe = noteStart; probe < noteEnd; probe += Config::TICKS_PER_16TH_STEP) {
    if (tickInHalfOpenWindow(probe, windowStart, windowLength, loopLength)) {
      return true;
    }
  }
  return tickInHalfOpenWindow(noteStart, windowStart, windowLength, loopLength) ||
         tickInHalfOpenWindow(endTick, windowStart, windowLength, loopLength);
}

DisplayNoteVec filterDisplayNotesToWindow(const DisplayNoteVec& notes, const TickInterval& viewport,
                                          uint32_t loopLength) {
  DisplayNoteVec filtered;
  if (loopLength == 0 || viewport.length() <= 0) {
    return filtered;
  }
  filtered.reserve(notes.size());
  const uint32_t windowStartNorm =
      normalizeTick(static_cast<uint32_t>(viewport.start), loopLength);
  const uint32_t windowLength = static_cast<uint32_t>(viewport.length());
  for (const NoteUtils::DisplayNote& note : notes) {
    // Clamp frontier overflow before normalizeTick — startTick==loopLength maps to 0.
    NoteUtils::DisplayNote paintNote = note;
    clampNonWrapDisplayNoteBarTicks(paintNote.startTick, paintNote.endTick, loopLength);
    if (!noteIntersectsWindow(displayNoteSpanInLoop(paintNote, loopLength), viewport,
                              loopLength)) {
      continue;
    }
    NoteUtils::DisplayNote mapped = paintNote;
    uint32_t relStart = normalizeTick(paintNote.startTick, loopLength);
    if (relStart < windowStartNorm) {
      relStart += loopLength;
    }
    mapped.startTick = relStart - windowStartNorm;
    uint32_t relEnd = normalizeTick(paintNote.endTick, loopLength);
    if (relEnd < windowStartNorm) {
      relEnd += loopLength;
    }
    if (relEnd < relStart) {
      relEnd += loopLength;
    }
    mapped.endTick = relEnd - windowStartNorm;
    if (mapped.endTick > windowLength) {
      mapped.endTick = windowLength;
    }
    filtered.push_back(mapped);
  }
  return filtered;
}

DisplayNoteVec filterDisplayNotesToWindow(const DisplayNoteVec& notes, uint32_t windowStart,
                                          uint32_t windowLength, uint32_t loopLength) {
  return filterDisplayNotesToWindow(notes, makeViewportInterval(windowStart, windowLength),
                                  loopLength);
}

DisplayNoteVec filterDisplayNotesByWindowInclusion(const DisplayNoteVec& notes,
                                                   const TickInterval& viewport,
                                                   uint32_t loopLength) {
  DisplayNoteVec filtered;
  if (loopLength == 0 || viewport.length() <= 0) {
    return filtered;
  }
  filtered.reserve(notes.size());
  for (const NoteUtils::DisplayNote& note : notes) {
    if (!noteIntersectsWindow(displayNoteSpanInLoop(note, loopLength), viewport, loopLength)) {
      continue;
    }
    filtered.push_back(note);
  }
  return filtered;
}

DisplayNoteVec filterDisplayNotesByWindowInclusion(const DisplayNoteVec& notes,
                                                   uint32_t windowStart, uint32_t windowLength,
                                                   uint32_t loopLength) {
  return filterDisplayNotesByWindowInclusion(notes, makeViewportInterval(windowStart, windowLength),
                                             loopLength);
}

uint32_t resolveCenteredWindowStart(uint32_t playheadTick, uint32_t windowLength,
                                    uint32_t loopLength) {
  if (loopLength == 0 || windowLength == 0) {
    return 0;
  }
  if (windowLength >= loopLength) {
    return 0;
  }
  const uint32_t playhead = IntervalProjection::tickPhaseInLoop(playheadTick, 0, loopLength);
  uint32_t start =
      playhead >= windowLength / 2 ? playhead - windowLength / 2 : 0;
  if (start + windowLength > loopLength) {
    start = loopLength - windowLength;
  }
  return start;
}

bool segmentHasNotes(const DisplayNoteVec& notes, uint32_t loopLength,
                     const TickInterval& segment) {
  if (loopLength == 0 || segment.length() <= 0) {
    return false;
  }
  for (const NoteUtils::DisplayNote& note : notes) {
    if (noteIntersectsWindow(displayNoteSpanInLoop(note, loopLength), segment, loopLength)) {
      return true;
    }
  }
  return false;
}

bool segmentHasNotes(const DisplayNoteVec& notes, uint32_t loopLength, uint32_t segStartTick,
                     uint32_t segEndTick) {
  return segmentHasNotes(notes, loopLength, TickInterval{static_cast<int32_t>(segStartTick),
                                                         static_cast<int32_t>(segEndTick)});
}

void filterMidiEventsToWindow(const MidiEventVec& events, MidiEventVec& out, uint32_t windowStart,
                              uint32_t windowLength, uint32_t loopLength) {
  out.clear();
  if (events.empty() || loopLength == 0 || windowLength == 0) {
    return;
  }
  out.reserve(events.size());
  for (const MidiEvent& evt : events) {
    if (tickInHalfOpenWindow(evt.tick, windowStart, windowLength, loopLength)) {
      out.push_back(evt);
    }
  }
}

void filterMidiEventsToWindow(const SessionMidiEventVec& events, SessionMidiEventVec& out,
                              uint32_t windowStart, uint32_t windowLength, uint32_t loopLength) {
  out.clear();
  if (events.empty() || loopLength == 0 || windowLength == 0) {
    return;
  }
  out.reserve(events.size());
  for (const MidiEvent& evt : events) {
    if (tickInHalfOpenWindow(evt.tick, windowStart, windowLength, loopLength)) {
      out.push_back(evt);
    }
  }
}

void formatLoopLengthBars(char* out, size_t outSize, uint32_t loopLengthTicks,
                          uint32_t ticksPerBar) {
  if (out == nullptr || outSize == 0) {
    return;
  }
  if (loopLengthTicks == 0 || ticksPerBar == 0) {
    snprintf(out, outSize, " --");
    return;
  }
  uint32_t bars = loopLengthTicks / ticksPerBar;
  if (bars > 999UL) {
    bars = 999UL;
  }
  snprintf(out, outSize, "%3lu", bars);
}

}  // namespace DisplayWindowUtils
