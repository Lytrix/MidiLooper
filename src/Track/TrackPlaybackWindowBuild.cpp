//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackInternal.h"

#include <Arduino.h>
#include <algorithm>
#include <vector>

#include "ClockManager.h"
#include "EditManager.h"
#include "Globals.h"
#include "TickPhase.h"
#include "TrackManager.h"
#include "Utils/Diagnostics.h"
#include "Utils/IntervalProjection.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/RuntimeTimingTelemetry.h"

extern TrackManager trackManager;

namespace {

size_t mergedPlaybackStreamSize(const void* ctx) {
  return static_cast<const MergedPlaybackStreamCtx*>(ctx)->order->size();
}

bool mergedPlaybackStreamValid(const void* ctx, uint16_t cursor) {
  const auto* stream = static_cast<const MergedPlaybackStreamCtx*>(ctx);
  if (static_cast<size_t>(cursor) >= stream->order->size()) {
    return true;
  }
  return (*stream->order)[cursor] < stream->merged->size();
}

const MidiEvent& mergedPlaybackStreamEventAt(const void* ctx, uint16_t cursor) {
  const auto* stream = static_cast<const MergedPlaybackStreamCtx*>(ctx);
  return (*stream->merged)[(*stream->order)[cursor]];
}

uint32_t mergedPlaybackStreamPhase(const MidiEvent& evt, const ProjectionContext& playbackContext) {
  return IntervalProjection::playbackEventPhase(evt.tick, playbackContext.loopLength);
}

size_t capturePlaybackStreamSize(const void* ctx) {
  return static_cast<const Loop*>(ctx)->capture.store.size();
}

const MidiEvent& capturePlaybackStreamEventAt(const void* ctx, uint16_t cursor) {
  return static_cast<const Loop*>(ctx)->capture.store.at(cursor);
}

uint32_t capturePlaybackStreamPhase(const MidiEvent& evt, const ProjectionContext& playbackContext) {
  return IntervalProjection::projectPlaybackEventPhase(evt.tick, playbackContext);
}

}  // namespace

ProjectionContext makePlaybackContext(const Track& track, const Loop& loop, uint32_t currentTick) {
  return IntervalProjection::buildPlaybackProjectionContext(
      loop.loopLengthTicks,
      IntervalProjection::makeFullLoopPlaybackProjectionInterval(loop.loopLengthTicks),
      static_cast<int32_t>(currentTick), track.getProjectionCycleStartTick(),
      static_cast<int32_t>(loop.loopStartTick), track.hasQueuedPlaybackStart(),
      track.getQueuedStartTick());
}

/// After a mid-pass playback-order rebuild, point nextEventIndex at the current playhead.
void reanchorPlaybackIndex(Loop& loop, const SessionMidiEventVec& mergedEvents, const PlaybackOrderVec& order,
                           const ProjectionContext& playbackContext) {
  if (loop.lastTickInLoop == UINT32_MAX) {
    loop.nextEventIndex = 0;
    return;
  }
  size_t idx = 0;
  while (idx < order.size()) {
    const MidiEvent& e = mergedEvents[order[idx]];
    const uint32_t evPhase =
        IntervalProjection::playbackEventPhase(e.tick, playbackContext.loopLength);
    if (evPhase > loop.lastTickInLoop) break;
    ++idx;
  }
  loop.nextEventIndex = static_cast<uint16_t>(idx);
}

void ensurePlaybackMergedMidiEventsBuilt(Track& track, Loop& loop, LoopPlaybackRuntime& runtime,
                               bool /*allowHeavyBuild*/, uint32_t currentTick) {
  // Projection boundary (linear-loop-tick-storage): mergedEvents are read-only input to
  // playback order + MIDI send. Playback mergedMidiEvents is committed-only (224719):
  // live capture must not fold into this representation. NOTE_EDIT uses session store
  // (Tier 2) — full replace, no materialized underlay. Outside NOTE_EDIT, chunk-ref
  // merge of committed passes only (DEC-016). Long loops use windowed gather — full-loop
  // gather after LoadLoopJob Commit hard-faults (session_20260718_210532 / 210001).
  static bool mergedMidiEventsBuildInProgress = false;
  if (mergedMidiEventsBuildInProgress) {
    return;
  }
  const bool noteEditPreview = editManager.isNoteEditActive() &&
                               &track == &trackManager.getSelectedTrack();
  const uint32_t windowRevision = noteEditPreview ? editManager.sessionPlaybackPreviewRevision()
                                                    : loop.playbackRevision;
  const bool longLoop = loop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks);
  uint32_t playhead = 0;
  if (loop.loopLengthTicks > 0) {
    playhead = IntervalProjection::tickPhaseInProjectionCycle(
        currentTick, track.getProjectionCycleStartTick(), loop.loopLengthTicks);
  }

  if (!longLoop) {
    if (runtime.mergedMidiEvents.builtFromRevision == windowRevision) {
      DIAG_COUNTER_INC(PlaybackDeferredReuse);
      return;
    }
  } else if (runtime.mergedMidiEvents.builtFromRevision == windowRevision &&
             !runtime.mergedMidiEvents.empty() &&
             runtime.mergedMidiEvents.windowLengthTicks > 0) {
    const uint32_t winStart = runtime.mergedMidiEvents.windowStartTick;
    const uint32_t winEnd = winStart + runtime.mergedMidiEvents.windowLengthTicks;
    const uint32_t margin = Config::TICKS_PER_BAR / 2;
    if (playhead + margin >= winStart && playhead < winEnd) {
      DIAG_COUNTER_INC(PlaybackDeferredReuse);
      return;
    }
  }

  mergedMidiEventsBuildInProgress = true;
  const uint32_t playbackBuildStartUs = micros();
  DIAG_COUNTER_INC(PlaybackMergedMidiEventsRebuild);
  if (noteEditPreview) {
    const MidiEventVec& preview = editManager.sessionMidiEvents();
    runtime.mergedMidiEvents.mergedEvents.assign(preview.begin(), preview.end());
    runtime.mergedMidiEvents.windowStartTick = 0;
    runtime.mergedMidiEvents.windowLengthTicks = loop.loopLengthTicks;
  } else if (longLoop) {
    // Two bars centered on playhead — enough for LoopEnd launch + clock catch-up.
    constexpr uint32_t kMergedMidiEventsGatherBars = 2;
    const uint32_t winLen = kMergedMidiEventsGatherBars * Config::TICKS_PER_BAR;
    uint32_t winStart = playhead > (winLen / 2) ? playhead - (winLen / 2) : 0;
    if (winStart + winLen > loop.loopLengthTicks) {
      winStart = loop.loopLengthTicks > winLen ? loop.loopLengthTicks - winLen : 0;
    }
    loop.gatherCommittedEventsInWindow(runtime.mergedMidiEvents.mergedEvents, winStart, winLen);
    runtime.mergedMidiEvents.windowStartTick = winStart;
    runtime.mergedMidiEvents.windowLengthTicks = winLen;
  } else {
    loop.gatherCommittedEventsForDerivedView(runtime.mergedMidiEvents.mergedEvents);
    runtime.mergedMidiEvents.windowStartTick = 0;
    runtime.mergedMidiEvents.windowLengthTicks = loop.loopLengthTicks;
  }
  runtime.mergedMidiEvents.builtFromRevision = windowRevision;
  loop.playbackOrderDirty = true;
  const uint32_t playbackBuildUs = micros() - playbackBuildStartUs;
  DIAG_TIMING_RECORD(PlaybackBuild, playbackBuildUs);
  RuntimeTimingTelemetry::recordPlaybackRebuild(
      playbackBuildUs, runtime.mergedMidiEvents.windowStartTick,
      runtime.mergedMidiEvents.windowLengthTicks, windowRevision);
  mergedMidiEventsBuildInProgress = false;
}
TRACK_INTERNAL_MEM __attribute__((noinline)) void rebuildPlaybackOrder(
    Loop& loop, const SessionMidiEventVec& mergedEvents, const ProjectionContext& playbackContext) {
  PlaybackOrderVec& playbackOrder = loop.getPlaybackOrder();
  playbackOrder.resize(mergedEvents.size());
  for (size_t i = 0; i < mergedEvents.size(); i++) {
    playbackOrder[i] = i;
  }
  std::vector<uint32_t, ExternalMemoryFirstAllocator<uint32_t>> sortPhases(mergedEvents.size());
  for (size_t i = 0; i < mergedEvents.size(); ++i) {
    sortPhases[i] =
        IntervalProjection::playbackEventPhase(mergedEvents[i].tick, playbackContext.loopLength);
  }
  std::sort(playbackOrder.begin(), playbackOrder.end(), [&](size_t a, size_t b) {
    if (sortPhases[a] != sortPhases[b]) {
      return sortPhases[a] < sortPhases[b];
    }
    return mergedEvents[a].tick < mergedEvents[b].tick;
  });
  // Equal-phase Off before On — same keys as NoteUtils::sortMidiEventsChronologically.
  // Keep those keys out of the std::sort lambda so the ITCM sort instantiation does not grow.
  for (size_t i = 0; i < playbackOrder.size();) {
    size_t groupEnd = i + 1;
    while (groupEnd < playbackOrder.size() &&
           sortPhases[playbackOrder[groupEnd]] == sortPhases[playbackOrder[i]] &&
           mergedEvents[playbackOrder[groupEnd]].tick == mergedEvents[playbackOrder[i]].tick) {
      ++groupEnd;
    }
    for (size_t a = i + 1; a < groupEnd; ++a) {
      const size_t inserted = playbackOrder[a];
      const int insertedOrder =
          mergedEvents[inserted].isNoteOff() ? 0 : (mergedEvents[inserted].isNoteOn() ? 1 : 2);
      size_t k = a;
      while (k > i) {
        const size_t prev = playbackOrder[k - 1];
        const int prevOrder =
            mergedEvents[prev].isNoteOff() ? 0 : (mergedEvents[prev].isNoteOn() ? 1 : 2);
        if (prevOrder <= insertedOrder) {
          break;
        }
        playbackOrder[k] = prev;
        --k;
      }
      playbackOrder[k] = inserted;
    }
    i = groupEnd;
  }
  loop.playbackOrderDirty = false;
}
void reanchorCaptureIndex(Loop& loop) {
  if (loop.lastTickInLoop == UINT32_MAX) {
    loop.captureNextEventIndex = 0;
    return;
  }
  size_t idx = 0;
  while (idx < loop.capture.store.size()) {
    const MidiEvent& e = loop.capture.store.at(idx);
    if (e.tick >= loop.loopLengthTicks || e.tick > loop.lastTickInLoop) break;
    ++idx;
  }
  loop.captureNextEventIndex = static_cast<uint16_t>(idx);
}
PlaybackEventStream makeMergedPlaybackStream(MergedPlaybackStreamCtx& ctx) {
  return PlaybackEventStream{&ctx, mergedPlaybackStreamSize, mergedPlaybackStreamValid,
                             mergedPlaybackStreamEventAt, mergedPlaybackStreamPhase};
}

PlaybackEventStream makeCapturePlaybackStream(Loop& loop) {
  return PlaybackEventStream{&loop, capturePlaybackStreamSize, nullptr,
                             capturePlaybackStreamEventAt, capturePlaybackStreamPhase};
}
void Track::resetPlaybackState(uint32_t currentTick) {
  Loop& loop = getActiveLoop();
  loop.nextEventIndex = 0;
  invalidatePlaybackMergedMidiEvents(true);
  if (loop.loopLengthTicks == 0) {
    loop.lastTickInLoop = 0;
    projectionCycleStartTick = static_cast<int32_t>(currentTick);
    return;
  }
  const uint32_t phase =
      tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks);
  projectionCycleStartTick = static_cast<int32_t>(currentTick) - static_cast<int32_t>(phase);
  loop.lastTickInLoop = phase;
}

void Track::resetPlaybackStateForSlot(uint8_t slotIndex, uint32_t currentTick) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) return;
  Loop& loop = getLoop(slotIndex);
  if (loop.loopLengthTicks == 0) return;
  loop.nextEventIndex = 0;
  const uint32_t phase =
      tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks);
  if (slotIndex == activeLoopIndex) {
    projectionCycleStartTick = static_cast<int32_t>(currentTick) - static_cast<int32_t>(phase);
  }
  loop.lastTickInLoop = phase;
  invalidatePlaybackMergedMidiEvents(true);
}

void Track::invalidatePlaybackMergedMidiEvents(bool preserveLedger) {
  bumpPlaybackGeneration();
  playbackRuntime.resetAll(preserveLedger);
}
void Track::prewarmPlaybackForSlot(uint8_t slotIndex) {
  ensureLoopsAllocated();
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return;
  }
  Loop& loop = loopForSlot(slotIndex);
  (void)playbackRuntime.trySlot(slotIndex);
  (void)loop.getPlaybackOrder();
}

void Track::ensurePlaybackMergedEventsForSlot(uint8_t slotIndex) {
  ensureLoopsAllocated();
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return;
  }
  Loop& loop = loopForSlot(slotIndex);
  LoopPlaybackRuntime* runtime = playbackRuntime.trySlot(slotIndex);
  if (runtime == nullptr) {
    return;
  }
  (void)loop.getPlaybackOrder();
  // Build destination merged MIDI before LoopEnd commit so launch is a cache hit.
  if (loop.hasCommittedPasses() && loop.loopLengthTicks > 0) {
    const uint32_t currentTick = clockManager.getCurrentTick();
    ensurePlaybackMergedMidiEventsBuilt(*this, loop, *runtime, true, currentTick);
    if (loop.playbackOrderDirty) {
      const ProjectionContext playbackContext = makePlaybackContext(*this, loop, currentTick);
      ::rebuildPlaybackOrder(loop, runtime->mergedMidiEvents.mergedEvents, playbackContext);
    }
  }
}

bool Track::isPlaybackMergedMidiEventsReadyForSlot(uint8_t slotIndex) const {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || !loopsAllocated()) {
    return false;
  }
  const Loop& loop = loopForSlot(slotIndex);
  if (!loop.hasCommittedPasses() || loop.loopLengthTicks == 0) {
    return false;
  }
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return false;
  }
  return runtime->mergedMidiEvents.builtFromRevision == loop.playbackRevision &&
         !runtime->mergedMidiEvents.mergedEvents.empty();
}

void Track::releasePlaybackMergedMidiEventsMemory() {
  playbackRuntime.resetAll(false);
}

TRACK_COLD_MEM bool Track::tryReleasePlaybackMergedMidiEventsMemory() {
  if (isRecording() || isOverdubbing() || getState() == TRACK_ARMED) {
    return false;
  }

  const uint8_t trackIndex = resolveTrackIndexForPersistence(*this);
  const bool selected = trackManager.isSelectedTrack(*this);
  const bool noteEditActive = editManager.isNoteEditActive();
  const bool transportActive = isPlaying();
  bool reclaimed = false;

  for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
    Loop& loop = loopForSlot(slot);
    if (!trackManager.isSlotEnabled(trackIndex, slot) && !loop.hasCommittedPasses()) {
      continue;
    }

    // Never allocate here — under Low pressure after a 57KB LoadLoopJob parse, trySlot can
    // fail and slot() null-derefs (session_20260719_000205: silence after parse_leave before
    // apply_enter on the next frame's reclaim).
    LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slot);
    if (runtime == nullptr || runtime->mergedMidiEvents.empty()) {
      continue;
    }

    const bool noteEditPreview =
        noteEditActive && selected && slot == getActiveLoopIndex();
    if (noteEditPreview) {
      continue;
    }

    const uint32_t windowRevision = loop.playbackRevision;
    const bool windowStale = runtime->mergedMidiEvents.builtFromRevision != windowRevision;
    if (transportActive && !windowStale) {
      continue;
    }

    runtime->mergedMidiEvents.clear();
    reclaimed = true;
  }
  return reclaimed;
}

TRACK_COLD_MEM bool Track::tryClearCommittedMidiScratch() {
  if (editManager.isNoteEditActive() && trackManager.isSelectedTrack(*this)) {
    return false;
  }
  if (committedMidiScratch_.empty()) {
    return false;
  }
  committedMidiScratch_.clear();
  committedMidiScratchRevision_ = UINT32_MAX;
  return true;
}
