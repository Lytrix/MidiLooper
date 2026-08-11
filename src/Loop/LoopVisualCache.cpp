//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"

#include "Globals.h"
#include "Utils/Diagnostics.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/IntervalProjection.h"
#include "Utils/LoopMem.h"
#include "Utils/NoteUtils.h"

#include <algorithm>

namespace {

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

uint32_t findNextDirtyBar(const VisualBarVec& dirtyBars, uint32_t priorityBar,
                          uint32_t maxBarDistanceFromPriority) {
  if (dirtyBars.empty()) {
    return UINT32_MAX;
  }
  const uint32_t totalBars = static_cast<uint32_t>(dirtyBars.size());
  const uint32_t start = priorityBar < totalBars ? priorityBar : 0;
  if (maxBarDistanceFromPriority == UINT32_MAX) {
    for (uint32_t offset = 0; offset < totalBars; ++offset) {
      const uint32_t bar = (start + offset) % totalBars;
      if (dirtyBars[bar] != 0) {
        return bar;
      }
    }
    return UINT32_MAX;
  }
  const uint32_t lo = (start > maxBarDistanceFromPriority) ? (start - maxBarDistanceFromPriority) : 0;
  const uint32_t hi = std::min(totalBars - 1, start + maxBarDistanceFromPriority);
  for (uint32_t bar = lo; bar <= hi; ++bar) {
    if (dirtyBars[bar] != 0) {
      return bar;
    }
  }
  return UINT32_MAX;
}

void removeDisplayNotesOverlappingBars(DisplayNoteVec& notes, uint32_t startBar, uint32_t endBar,
                                       uint32_t loopLengthTicks) {
  if (loopLengthTicks == 0) {
    return;
  }
  const uint32_t ticksPerBar = Config::TICKS_PER_BAR;
  const uint32_t rangeStart = startBar * ticksPerBar;
  const uint32_t rangeEndTick = std::min((endBar + 1) * ticksPerBar, loopLengthTicks);
  const uint32_t rangeLength = rangeEndTick > rangeStart ? rangeEndTick - rangeStart : 0;
  if (rangeLength == 0) {
    return;
  }
  notes.erase(std::remove_if(notes.begin(), notes.end(),
                             [&](const NoteUtils::DisplayNote& note) {
                               return DisplayWindowUtils::noteIntersectsWindow(
                                   note.startTick, note.endTick, rangeStart, rangeLength,
                                   loopLengthTicks);
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

size_t Loop::displayEventCountHint() const {
  size_t count = materializedEventCount_;
  if (captureActive()) {
    count += capture.store.size();
  }
  return count;
}

LOOP_COLD_MEM void Loop::rebuildVisualCacheIdleSlice(uint8_t maxBarsPerSlice, uint32_t priorityBar,
                                                     uint32_t maxBarDistanceFromPriority) {
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

  const uint32_t startBar =
      findNextDirtyBar(visualCache.dirtyBars, priorityBar, maxBarDistanceFromPriority);
  if (startBar == UINT32_MAX) {
    // Neighborhood-limited search found nothing — keep dirty for later full backfill.
    if (maxBarDistanceFromPriority != UINT32_MAX) {
      return;
    }
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
  gatherCommittedEventsInWindow(flat, windowStart, windowLength);
  const NoteUtils::DisplayNoteVec sliceNotes =
      NoteUtils::reconstructDisplayNotes(flat, loopLengthTicks, false);

  removeDisplayNotesOverlappingBars(visualCache.notes, startBar, endBar, loopLengthTicks);
  for (const NoteUtils::DisplayNote& note : sliceNotes) {
    const uint32_t endTick = note.endTick >= note.startTick ? note.endTick : note.startTick;
    const uint32_t noteStartBar = visualBarForTick(note.startTick, Config::TICKS_PER_BAR);
    const uint32_t noteEndBar = visualBarForTick(endTick, Config::TICKS_PER_BAR);
    if (noteStartBar <= endBar && noteEndBar >= startBar) {
      visualCache.notes.push_back(note);
    }
  }

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

LOOP_COLD_MEM void Loop::rebuildVisualCacheFromPasses() {
  DIAG_COUNTER_INC(DisplayFullRebuild);
  SessionMidiEventVec flat;
  gatherCommittedEvents(flat);
  materializedEventCount_ = flat.size();
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
  visualCache.notes.clear();
  visualCache.dirtyBars.clear();
  visualCacheDirty = true;
}
