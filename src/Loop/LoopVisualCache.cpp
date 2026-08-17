//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"

#include "EditApply.h"
#include "Globals.h"
#include "LoopContentResolution.h"
#include "LoopEventStore.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/Diagnostics.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/IntervalProjection.h"
#include "Utils/LoopMem.h"
#include "Utils/NoteUtils.h"
#include "Utils/RuntimeTimingTelemetry.h"

#include <algorithm>
#include <cstdio>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

namespace {

uint32_t totalVisualBarsForLoop(uint32_t loopLengthTicks) {
  if (loopLengthTicks == 0) {
    return 0;
  }
  return (loopLengthTicks + Config::TICKS_PER_BAR - 1) / Config::TICKS_PER_BAR;
}

// Coverage of the cached notes over the loop, for RC-E attribution. Bounds only, so no
// per-bar allocation on the commit path.
struct VisualCacheCoverage {
  uint32_t firstBar = UINT32_MAX;
  uint32_t lastBar = 0;
  uint32_t dirtyCount = 0;
};

VisualCacheCoverage measureVisualCacheCoverage(const VisualCache& cache) {
  VisualCacheCoverage coverage;
  for (const NoteUtils::DisplayNote& note : cache.notes) {
    const uint32_t endTick = note.endTick >= note.startTick ? note.endTick : note.startTick;
    const uint32_t startBar = visualBarForTick(note.startTick, Config::TICKS_PER_BAR);
    const uint32_t endBar = visualBarForTick(endTick, Config::TICKS_PER_BAR);
    if (startBar < coverage.firstBar) {
      coverage.firstBar = startBar;
    }
    if (endBar > coverage.lastBar) {
      coverage.lastBar = endBar;
    }
  }
  for (uint8_t flag : cache.dirtyBars) {
    if (flag != 0) {
      ++coverage.dirtyCount;
    }
  }
  return coverage;
}

void markAllVisualCacheBarsDirty(VisualCache& cache, uint32_t loopLengthTicks) {
  const uint32_t totalBars = totalVisualBarsForLoop(loopLengthTicks);
  if (totalBars == 0) {
    cache.dirtyBars.clear();
    return;
  }
  cache.dirtyBars.assign(totalBars, 1);
}

bool displayNoteGeometryPresent(const NoteUtils::DisplayNoteVec& notes,
                                const NoteUtils::DisplayNote& candidate) {
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.note == candidate.note && note.startTick == candidate.startTick &&
        note.endTick == candidate.endTick) {
      return true;
    }
  }
  return false;
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

LOOP_COLD_MEM void Loop::emitVisualCacheState(const char* phase, int32_t gatheredEvents) const {
  const VisualCacheCoverage coverage = measureVisualCacheCoverage(visualCache);
  SC_VCACHE(phase, gatheredEvents, static_cast<uint32_t>(visualCache.notes.size()),
            coverage.firstBar, coverage.lastBar, totalVisualBarsForLoop(loopLengthTicks),
            static_cast<uint32_t>(visualCache.dirtyBars.size()), coverage.dirtyCount,
            visualCacheDirty ? 1 : 0);
}

LOOP_COLD_MEM void Loop::adoptComposedDisplayNotesFromViewport(const DisplayNoteVec& notes) {
  adoptPartialVisualCacheNotes(visualCache, visualCacheDirty, notes, loopLengthTicks,
                               Config::TICKS_PER_BAR);
  emitVisualCacheState("adopt_partial", -1);
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
    emitVisualCacheState("slice_nodirty", -1);
    return;
  }

  const uint32_t maxEnd =
      startBar + std::min<uint32_t>(maxBarsPerSlice, totalBars - startBar) - 1;
  uint32_t endBar = startBar;
  while (endBar < maxEnd && visualCache.dirtyBars[endBar + 1] != 0) {
    ++endBar;
  }

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

#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_VCACHE_SLICE
  // Immediate Serial — ring SC_VCACHE is not flushed if this turn faults (152405).
  Serial.print(F("VCACHE,slice_enter,bars,"));
  Serial.println(totalBars);
#endif
  SessionMidiEventVec flat;
  ResolutionCostCounters windowCounters;
  const bool usedPrepared = LoopContentResolution::tryResolvePreparedWindow(
      passes.editPasses, loopLengthTicks, windowStart, windowLength, playbackRevision, flat,
      &windowCounters);
#if defined(SESSION_CAPTURE)
  uint32_t gatherStartUs = 0;
  if (!usedPrepared) {
    gatherStartUs = micros();
  }
#endif
  if (!usedPrepared) {
    gatherCommittedEventsInWindow(flat, windowStart, windowLength);
  }
#if defined(SESSION_CAPTURE)
  if (!usedPrepared) {
    RuntimeTimingTelemetry::recordIdleMaintChildRem(0, gatherStartUs);
  }
#endif
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_VCACHE_SLICE
  Serial.print(F("VCACHE,slice_gathered,ev,"));
  Serial.println(static_cast<unsigned>(flat.size()));
#endif
#if defined(SESSION_CAPTURE)
  const uint32_t reconstructStartUs = micros();
#endif
  NoteUtils::DisplayNoteVec sliceNotes =
      NoteUtils::reconstructDisplayNotes(flat, loopLengthTicks, false, false);
#if defined(SESSION_CAPTURE)
  RuntimeTimingTelemetry::recordIdleMaintChildRem(1, reconstructStartUs);
#endif
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_VCACHE_SLICE
  Serial.print(F("VCACHE,slice_recon,notes,"));
  Serial.println(static_cast<unsigned>(sliceNotes.size()));
#endif
  // Wrap-held overdub heads/tails are omitted by merged reconstruct when a later
  // same-pitch body exists (021218). Do not use wrap pairing on the merged flat
  // (015618 stretches record). Fill that gap only on slices that keep bar 0 or
  // the last bar. Interior slices — prepared or unprepared — use the window only.
  const bool appendOverdub = startBar == 0 || endBar + 1 >= totalBars;
#if defined(SESSION_CAPTURE)
  const uint32_t appendStartUs = micros();
#endif
  if (appendOverdub && !usedPrepared) {
    appendOverdubPassDisplayNotes(sliceNotes);
  }
#if defined(SESSION_CAPTURE)
  if (appendOverdub) {
    RuntimeTimingTelemetry::recordIdleMaintChildRem(2, appendStartUs);
  }
#endif
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
  const uint32_t reconstructUs = micros() - reconstructStartUs;
  if (usedPrepared) {
    const uint32_t windowUs = static_cast<uint32_t>(windowCounters.elapsedMicros);
    char line[192];
    snprintf(line, sizeof(line),
             "#CAP,%lu,DIAG,lcr,vch,win=%lu,proj=%lu,tot=%lu,ev=%u,notes=%u",
             static_cast<unsigned long>(micros()), static_cast<unsigned long>(windowUs),
             static_cast<unsigned long>(reconstructUs),
             static_cast<unsigned long>(windowUs + reconstructUs),
             static_cast<unsigned>(flat.size()), static_cast<unsigned>(sliceNotes.size()));
    DebugSessionCapture::appendCaptureTextLine(line);
  }
#endif

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
    emitVisualCacheState("slice_clean", -1);
  }
}

LOOP_COLD_MEM void Loop::appendOverdubPassDisplayNotes(NoteUtils::DisplayNoteVec& notes) const {
  if (loopLengthTicks == 0) {
    return;
  }
  EditPassVec activeEdits;
  for (const EditPass& editPass : passes.editPasses) {
    if (editPass.state == EditPassState::Active && editPass.passType == EditPassType::Note) {
      activeEdits.push_back(editPass);
    }
  }
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.state != CapturePassState::Active || pass.committedChunkIds.empty()) {
      continue;
    }
    SessionMidiEventVec overdubEvents;
    LoopEventStore::appendChunkRefEvents(pass.committedChunkIds, overdubEvents);
    if (!activeEdits.empty()) {
      applyNoteEditPassSequence(overdubEvents, activeEdits, loopLengthTicks);
    }
    const NoteUtils::DisplayNoteVec overdubNotes = NoteUtils::reconstructDisplayNotes(
        overdubEvents, loopLengthTicks, false, false, true);
    for (const NoteUtils::DisplayNote& note : overdubNotes) {
      if (!displayNoteGeometryPresent(notes, note)) {
        notes.push_back(note);
      }
    }
  }
}

LOOP_COLD_MEM void Loop::rebuildVisualCacheFromPasses() {
  DIAG_COUNTER_INC(DisplayFullRebuild);
  SessionMidiEventVec flat;
  gatherCommittedEvents(flat);
  materializedEventCount_ = flat.size();
  NoteUtils::DisplayNoteVec rebuiltNotes =
      NoteUtils::reconstructDisplayNotes(flat, loopLengthTicks, false, false);
  appendOverdubPassDisplayNotes(rebuiltNotes);
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
  emitVisualCacheState("full", static_cast<int32_t>(flat.size()));
}

LOOP_COLD_MEM void Loop::refreshVisualCacheAfterPassStateChange() {
  markDisplayCachesStale();
  const uint32_t totalBars = totalVisualBarsForLoop(loopLengthTicks);
  if (totalBars == 0) {
    return;
  }
  constexpr uint32_t kImmediateFillBarLimit = 16;
  if (totalBars > kImmediateFillBarLimit) {
    return;
  }
  constexpr uint8_t kBarsPerSlice = 4;
  const uint32_t maxSlices = (totalBars + kBarsPerSlice - 1) / kBarsPerSlice + 1;
  for (uint32_t slice = 0; slice < maxSlices && visualCacheDirty; ++slice) {
    rebuildVisualCacheIdleSlice(kBarsPerSlice, 0);
  }
}

LOOP_COLD_MEM void Loop::retireSupersededPitchDisplayNote(uint8_t previousPitch, uint32_t startTick,
                                                          uint32_t endTick, uint8_t settledPitch) {
  if (previousPitch == settledPitch) {
    return;
  }
  bool hasSettled = false;
  for (const NoteUtils::DisplayNote& note : visualCache.notes) {
    if (note.note == settledPitch && note.startTick == startTick && note.endTick == endTick) {
      hasSettled = true;
      break;
    }
  }
  if (!hasSettled) {
    return;
  }
  auto newEnd = std::remove_if(visualCache.notes.begin(), visualCache.notes.end(),
                               [&](const NoteUtils::DisplayNote& note) {
                                 return note.note == previousPitch && note.startTick == startTick &&
                                        note.endTick == endTick;
                               });
  visualCache.notes.erase(newEnd, visualCache.notes.end());
}

LOOP_COLD_MEM void Loop::ensureVisualCacheBuilt() {
  if (!visualCacheDirty) {
    return;
  }
  rebuildVisualCacheFromPasses();
}

LOOP_COLD_MEM void Loop::markDisplayCachesStale() {
  invalidatePlaybackCaches();
  visualCacheDirty = true;
  markAllVisualCacheBarsDirty(visualCache, loopLengthTicks);
  emitVisualCacheState("stale_all", -1);
}

namespace {

LOOP_COLD_MEM void ensureVisualCacheDirtyBarCapacity(VisualCache& cache, uint32_t loopLengthTicks) {
  const uint32_t totalBars = totalVisualBarsForLoop(loopLengthTicks);
  if (totalBars == 0) {
    return;
  }
  if (cache.dirtyBars.size() < totalBars) {
    cache.dirtyBars.resize(totalBars, 0);
  }
}

LOOP_COLD_MEM void markTickSpanDirty(VisualCache& cache, uint32_t startTick, uint32_t endTick,
                       uint32_t loopLengthTicks) {
  const uint32_t totalBars = totalVisualBarsForLoop(loopLengthTicks);
  if (totalBars == 0) {
    return;
  }
  ensureVisualCacheDirtyBarCapacity(cache, loopLengthTicks);
  const uint32_t startBar = visualBarForTick(startTick, Config::TICKS_PER_BAR);
  const uint32_t endBar = visualBarForTick(endTick, Config::TICKS_PER_BAR);
  auto markInclusive = [&](uint32_t loBar, uint32_t hiBar) {
    if (loBar >= totalBars) {
      return;
    }
    const uint32_t hi = std::min(hiBar, totalBars - 1);
    if (loBar > hi) {
      return;
    }
    for (uint32_t bar = loBar; bar <= hi; ++bar) {
      cache.markBarDirty(bar);
    }
  };
  if (endTick < startTick) {
    markInclusive(startBar, totalBars - 1);
    markInclusive(0, endBar);
    return;
  }
  markInclusive(startBar, endBar);
}

LOOP_COLD_MEM const CommittedChunkIdList* chunksForPassId(const LoopPasses& passes, PassId passId) {
  if (passes.hasRecordPass() && passes.recordPass.id == passId) {
    return &passes.recordPass.committedChunkIds;
  }
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.id == passId) {
      return &pass.committedChunkIds;
    }
  }
  return nullptr;
}

LOOP_COLD_MEM const EditPass* editPassById(const EditPassVec& editPasses, EditPassId id) {
  for (const EditPass& editPass : editPasses) {
    if (editPass.id == id) {
      return &editPass;
    }
  }
  return nullptr;
}

}  // namespace

LOOP_COLD_MEM void Loop::markAffectedDisplayCacheRanges(PassId committedPassId,
                                                        const EditPassIdList& companionIds) {
  invalidatePlaybackCaches();
  visualCacheDirty = true;
  const uint32_t totalBars = totalVisualBarsForLoop(loopLengthTicks);
  if (loopLengthTicks == 0 || totalBars == 0) {
    markDisplayCachesStale();
    return;
  }

  const CommittedChunkIdList* chunks = chunksForPassId(passes, committedPassId);
  if (chunks == nullptr && companionIds.empty()) {
    markDisplayCachesStale();
    return;
  }

  ensureVisualCacheDirtyBarCapacity(visualCache, loopLengthTicks);

  struct OpenOn {
    NoteId noteId = kInvalidNoteId;
    uint32_t tick = 0;
  };
  OpenOn openOns[128];
  uint8_t openCount = 0;
  SessionMidiEventVec chunkEvents;
  if (chunks != nullptr) {
    for (uint16_t chunkId : *chunks) {
      chunkEvents.clear();
      LoopEventStore::appendChunkRefEvent(chunkId, chunkEvents);
      for (const MidiEvent& event : chunkEvents) {
        visualCache.markBarDirty(visualBarForTick(event.tick, Config::TICKS_PER_BAR));
        if (event.isNoteOn()) {
          if (openCount < 128) {
            openOns[openCount].noteId = event.noteId;
            openOns[openCount].tick = event.tick;
            ++openCount;
          }
          continue;
        }
        if (!event.isNoteOff()) {
          continue;
        }
        for (uint8_t i = openCount; i > 0; --i) {
          const uint8_t idx = static_cast<uint8_t>(i - 1);
          if (openOns[idx].noteId != event.noteId) {
            continue;
          }
          markTickSpanDirty(visualCache, openOns[idx].tick, event.tick, loopLengthTicks);
          openOns[idx] = openOns[openCount - 1];
          --openCount;
          break;
        }
      }
    }
  }

  for (EditPassId companionId : companionIds) {
    const EditPass* row = editPassById(passes.editPasses, companionId);
    if (row == nullptr) {
      continue;
    }
    markTickSpanDirty(visualCache, row->startTick, row->endTick, loopLengthTicks);
    for (const NoteUtils::DisplayNote& note : visualCache.notes) {
      if (note.noteId != row->targetNoteId) {
        continue;
      }
      markTickSpanDirty(visualCache, note.startTick, note.endTick, loopLengthTicks);
      break;
    }
  }

  emitVisualCacheState("stale_range", -1);
}

LOOP_COLD_MEM void Loop::invalidateDisplayCaches() {
  if (noteCache_) {
    noteCache_->invalidate();
  }
  visualCache.notes.clear();
  visualCache.dirtyBars.clear();
  visualCacheDirty = true;
}
