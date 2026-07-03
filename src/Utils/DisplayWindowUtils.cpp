//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/DisplayWindowUtils.h"
#include "Globals.h"

namespace DisplayWindowUtils {

using DisplayNoteVec = NoteUtils::DisplayNoteVec;

namespace {

uint32_t normalizeTick(uint32_t tick, uint32_t loopLength) {
  if (loopLength == 0) {
    return 0;
  }
  return tick % loopLength;
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

DisplayNoteVec filterDisplayNotesToWindow(const DisplayNoteVec& notes, uint32_t windowStart,
                                          uint32_t windowLength, uint32_t loopLength) {
  DisplayNoteVec filtered;
  if (loopLength == 0 || windowLength == 0) {
    return filtered;
  }
  filtered.reserve(notes.size());
  const uint32_t windowStartNorm = normalizeTick(windowStart, loopLength);
  for (const NoteUtils::DisplayNote& note : notes) {
    if (!noteIntersectsWindow(note.startTick, note.endTick, windowStart, windowLength,
                              loopLength)) {
      continue;
    }
    NoteUtils::DisplayNote mapped = note;
    uint32_t relStart = normalizeTick(note.startTick, loopLength);
    if (relStart < windowStartNorm) {
      relStart += loopLength;
    }
    mapped.startTick = relStart - windowStartNorm;
    uint32_t relEnd = normalizeTick(note.endTick, loopLength);
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

DisplayNoteVec filterDisplayNotesByWindowInclusion(const DisplayNoteVec& notes,
                                                   uint32_t windowStart, uint32_t windowLength,
                                                   uint32_t loopLength) {
  DisplayNoteVec filtered;
  if (loopLength == 0 || windowLength == 0) {
    return filtered;
  }
  filtered.reserve(notes.size());
  for (const NoteUtils::DisplayNote& note : notes) {
    if (!noteIntersectsWindow(note.startTick, note.endTick, windowStart, windowLength,
                              loopLength)) {
      continue;
    }
    filtered.push_back(note);
  }
  return filtered;
}

uint32_t resolveCenteredWindowStart(uint32_t playheadTick, uint32_t windowLength,
                                    uint32_t loopLength) {
  if (loopLength == 0 || windowLength == 0) {
    return 0;
  }
  if (windowLength >= loopLength) {
    return 0;
  }
  const uint32_t playhead = playheadTick % loopLength;
  uint32_t start =
      playhead >= windowLength / 2 ? playhead - windowLength / 2 : 0;
  if (start + windowLength > loopLength) {
    start = loopLength - windowLength;
  }
  return start;
}

bool segmentHasNotes(const DisplayNoteVec& notes, uint32_t loopLength, uint32_t segStartTick,
                     uint32_t segEndTick) {
  if (loopLength == 0 || segEndTick <= segStartTick) {
    return false;
  }
  for (const NoteUtils::DisplayNote& note : notes) {
    if (noteIntersectsWindow(note.startTick, note.endTick, segStartTick,
                             segEndTick - segStartTick, loopLength)) {
      return true;
    }
  }
  return false;
}

}  // namespace DisplayWindowUtils
