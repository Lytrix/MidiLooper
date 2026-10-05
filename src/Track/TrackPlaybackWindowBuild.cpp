#include "Utils/PlaybackLaneMeasurement.h"
//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackInternal.h"

#include <Arduino.h>
#include <algorithm>
#include <vector>
#include <cstring>

#include "ClockManager.h"
#include "EditManager.h"
#include "Globals.h"
#include "TickPhase.h"
#include "TrackManager.h"
#include "Utils/Diagnostics.h"
#include "Utils/IntervalProjection.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/TelemetryCadence.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/ExternalPoolAccounting.h"
#include "Utils/RuntimeTimingTelemetry.h"
#include "PlaybackMergedMidiEvents.h"
#include "Utils/CommittedMidiScratch.h"

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

size_t fixedPlaybackStreamSize(const void* ctx) {
  return static_cast<const FixedPlaybackStreamCtx*>(ctx)->storage->eventCount;
}

bool fixedPlaybackStreamValid(const void* ctx, uint16_t cursor) {
  const auto* stream = static_cast<const FixedPlaybackStreamCtx*>(ctx);
  if (static_cast<size_t>(cursor) >= stream->storage->eventCount) {
    return true;
  }
  return stream->storage->order[cursor] < stream->storage->eventCount;
}

const MidiEvent& fixedPlaybackStreamEventAt(const void* ctx, uint16_t cursor) {
  const auto* stream = static_cast<const FixedPlaybackStreamCtx*>(ctx);
  return stream->storage->events[stream->storage->order[cursor]];
}

/// Task 2.4 slice 2: the prepared pair is stored in emission order with an identity
/// `order`, so it is consumed through the fixed-stream adapter below. No new stream code,
/// and no member is derived at consume time.
size_t capturePlaybackStreamSize(const void* ctx) {
  return static_cast<const Loop*>(ctx)->capture.store.size();
}

const MidiEvent& capturePlaybackStreamEventAt(const void* ctx, uint16_t cursor) {
  return static_cast<const Loop*>(ctx)->capture.store.at(cursor);
}

uint32_t capturePlaybackStreamPhase(const MidiEvent& evt, const ProjectionContext& playbackContext) {
  return IntervalProjection::projectPlaybackEventPhase(evt.tick, playbackContext);
}

uint8_t playbackRebuildTrackIndex(const Track& track) {
  const uint8_t trackCount = trackManager.getTrackCount();
  for (uint8_t trackIndex = 0; trackIndex < trackCount; ++trackIndex) {
    if (&track == &trackManager.getTrack(trackIndex)) {
      return trackIndex;
    }
  }
  return 255;
}

uint8_t playbackRebuildSlotIndex(const Track& track, const Loop& loop) {
  if (!track.loopsAllocated()) {
    return 255;
  }
  for (uint8_t slotIndex = 0; slotIndex < Config::MAX_LOOPS_PER_TRACK; ++slotIndex) {
    if (&loop == &track.getLoop(slotIndex)) {
      return slotIndex;
    }
  }
  return 255;
}

bool anyTimingCriticalTrackActive() {
  const uint8_t trackCount = trackManager.getTrackCount();
  for (uint8_t trackIndex = 0; trackIndex < trackCount; ++trackIndex) {
    const Track& candidate = trackManager.getTrack(trackIndex);
    if (candidate.isRecording() || candidate.isAudiblePlaybackActive()) {
      return true;
    }
  }
  return false;
}

/// Task 2.4 instrumentation: a content-sized playback-order build ran while a track was
/// timing-critical. The active clock path must consume a prepared source instead; this
/// counter is what a native test asserts against, so a new synchronous fallback fails.
///
/// Placed in RAM2 (DMAMEM) on device: the tree sits one 32 KB FlexRAM bank / one DTCM
/// alignment edge from the RAM1 local allowance, and a 4-byte .bss addition here measured
/// 4 KB of lost headroom (locals 6,400 -> 2,304). It is read only by tests and diagnostics,
/// never on the per-event path.
#if defined(__IMXRT1062__)
#define PLAYBACK_ORDER_METRIC_MEM DMAMEM
#else
#define PLAYBACK_ORDER_METRIC_MEM
#endif
PLAYBACK_ORDER_METRIC_MEM uint32_t g_playbackOrderHotRebuildCount = 0;

inline void noteContentSizedPlaybackBuildOnActiveTransport() {
  if (anyTimingCriticalTrackActive()) {
    ++g_playbackOrderHotRebuildCount;
  }
}

}  // namespace

void Track::resetPlaybackOrderHotRebuildCount() { g_playbackOrderHotRebuildCount = 0; }

uint32_t Track::playbackOrderHotRebuildCount() { return g_playbackOrderHotRebuildCount; }

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
  noteContentSizedPlaybackBuildOnActiveTransport();
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

void reanchorFixedPlaybackIndex(Loop& loop, const FixedPlaybackEventStorage& storage,
                                const ProjectionContext& playbackContext) {
  if (loop.lastTickInLoop == UINT32_MAX) {
    loop.nextEventIndex = 0;
    return;
  }
  size_t idx = 0;
  while (idx < storage.eventCount) {
    const MidiEvent& event = storage.events[storage.order[idx]];
    const uint32_t eventPhase =
        IntervalProjection::playbackEventPhase(event.tick, playbackContext.loopLength);
    if (eventPhase > loop.lastTickInLoop) {
      break;
    }
    ++idx;
  }
  loop.nextEventIndex = static_cast<uint16_t>(idx);
}

bool ensurePlaybackMergedMidiEventsBuilt(Track& track, Loop& loop, LoopPlaybackRuntime& runtime,
                               bool /*allowHeavyBuild*/, uint32_t currentTick, bool prepareLaunch) {
  // Projection boundary (linear-loop-tick-storage): mergedEvents are read-only input to
  // playback order + MIDI send. Playback mergedMidiEvents is committed-only (224719):
  // live capture must not fold into this representation. NOTE_EDIT uses session store
  // (Tier 2) — full replace, no materialized underlay. Outside NOTE_EDIT, the published
  // LoopSnapshot is the committed source when a published root exists (current or
  // stale-plus-unpublished overlay); otherwise chunk-ref merge of committed passes
  // (DEC-016). Long loops use windowed gather — full-loop gather after
  // LoadLoopJob Commit hard-faults (session_20260718_210532 / 210001).
  static bool mergedMidiEventsBuildInProgress = false;
  if (mergedMidiEventsBuildInProgress) {
    return false;
  }
  const bool noteEditPreview = !prepareLaunch && editManager.isNoteEditActive() &&
                               &track == &trackManager.getSelectedTrack();
  const uint32_t windowRevision = noteEditPreview ? editManager.sessionPlaybackPreviewRevision()
                                                    : loop.playbackRevision;
  const void* snapshotIdentity =
      noteEditPreview ? nullptr : loop.publishedSnapshotIdentity();
  const bool longLoop = loop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks);
  uint32_t playhead = 0;
  if (!prepareLaunch && loop.loopLengthTicks > 0) {
    playhead = IntervalProjection::tickPhaseInProjectionCycle(
        currentTick, track.getProjectionCycleStartTick(), loop.loopLengthTicks);
  }

  if (!longLoop) {
    if (runtime.mergedMidiEvents.builtFromRevision == windowRevision &&
        runtime.mergedMidiEvents.builtFromSnapshotIdentity == snapshotIdentity) {
      DIAG_COUNTER_INC(PlaybackDeferredReuse);
      return false;
    }
  } else if (runtime.mergedMidiEvents.builtFromRevision == windowRevision &&
             runtime.mergedMidiEvents.builtFromSnapshotIdentity == snapshotIdentity &&
             (snapshotIdentity != nullptr || !runtime.mergedMidiEvents.empty()) &&
             runtime.mergedMidiEvents.windowLengthTicks > 0) {
    const uint32_t winStart = runtime.mergedMidiEvents.windowStartTick;
    const uint32_t winEnd = winStart + runtime.mergedMidiEvents.windowLengthTicks;
    const uint32_t margin = Config::TICKS_PER_BAR / 2;
    if ((prepareLaunch ? playhead >= winStart : playhead + margin >= winStart) &&
        playhead < winEnd) {
      DIAG_COUNTER_INC(PlaybackDeferredReuse);
      return false;
    }
  }

  if (!noteEditPreview && !longLoop &&
      loop.tryAdmitLastOverdubSealIntoMergedEvents(runtime.mergedMidiEvents)) {
    loop.playbackOrderDirty = true;
#if defined(SESSION_CAPTURE)
    DebugSessionCapture::playbackRebuild(
        0, runtime.mergedMidiEvents.windowStartTick, runtime.mergedMidiEvents.windowLengthTicks,
        windowRevision, playbackRebuildTrackIndex(track), playbackRebuildSlotIndex(track, loop), 1,
        "incremental");
#endif
    return false;
  }
  loop.clearLastOverdubSealAdmitReceipt();

  noteContentSizedPlaybackBuildOnActiveTransport();
  mergedMidiEventsBuildInProgress = true;
#if RUNTIME_TIMING_ENABLED || defined(SESSION_CAPTURE)
  const uint32_t playbackBuildStartUs = micros();
#endif
  DIAG_COUNTER_INC(PlaybackMergedMidiEventsRebuild);
  const void* usedSnapshotIdentity = nullptr;
  Loop::SnapshotWindowGatherPath gatherPath = Loop::SnapshotWindowGatherPath::None;
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
    if (loop.tryGatherPublishedSnapshotEventsInWindow(runtime.mergedMidiEvents.mergedEvents,
                                                      winStart, winLen, &gatherPath)) {
      usedSnapshotIdentity = snapshotIdentity;
    } else {
      runtime.mergedMidiEvents.mergedEvents.clear();
    }
    runtime.mergedMidiEvents.windowStartTick = winStart;
    runtime.mergedMidiEvents.windowLengthTicks = winLen;
  } else {
    if (loop.tryGatherPublishedSnapshotEventsInWindow(runtime.mergedMidiEvents.mergedEvents, 0,
                                                      loop.loopLengthTicks, &gatherPath)) {
      usedSnapshotIdentity = snapshotIdentity;
    } else {
      runtime.mergedMidiEvents.mergedEvents.clear();
    }
    runtime.mergedMidiEvents.windowStartTick = 0;
    runtime.mergedMidiEvents.windowLengthTicks = loop.loopLengthTicks;
  }
  runtime.mergedMidiEvents.builtFromRevision = windowRevision;
  runtime.mergedMidiEvents.builtFromSnapshotIdentity = usedSnapshotIdentity;
  loop.playbackOrderDirty = true;
#if RUNTIME_TIMING_ENABLED || defined(SESSION_CAPTURE)
  const uint32_t playbackBuildUs = micros() - playbackBuildStartUs;
#endif
  DIAG_TIMING_RECORD(PlaybackBuild, playbackBuildUs);
  RUNTIME_TIMING_RECORD_PLAYBACK_REBUILD(
      playbackBuildUs, runtime.mergedMidiEvents.windowStartTick,
      runtime.mergedMidiEvents.windowLengthTicks, windowRevision,
      playbackRebuildTrackIndex(track), playbackRebuildSlotIndex(track, loop),
      noteEditPreview
          ? RuntimeTimingTelemetry::PlaybackRebuildPath::NoteEditPreview
          : (gatherPath == Loop::SnapshotWindowGatherPath::Snapshot
                 ? RuntimeTimingTelemetry::PlaybackRebuildPath::PublishedSnapshot
                 : (gatherPath == Loop::SnapshotWindowGatherPath::Overlay
                        ? RuntimeTimingTelemetry::PlaybackRebuildPath::StaleRootOverlay
                        : RuntimeTimingTelemetry::PlaybackRebuildPath::
                              GatherMissHoldEmpty)),
      playbackBuildStartUs, loop.lastTickInLoop, playhead,
      static_cast<uint32_t>(runtime.mergedMidiEvents.mergedEvents.size()));
  mergedMidiEventsBuildInProgress = false;
  return true;
}

TRACK_INTERNAL_MEM __attribute__((noinline)) void reconcilePlaybackLedgerAfterFullLoopRebuild(
    Loop& loop, LoopPlaybackRuntime& runtime) {
  loop.replaceCommittedPlaybackNoteOnIdentities(runtime.mergedMidiEvents);
  if (!isFullLoopMergedPlaybackWindow(runtime.mergedMidiEvents, loop.loopLengthTicks)) {
    return;
  }
  runtime.ledger.eraseOpenNotesMissingFromCommittedNoteOns(
      runtime.mergedMidiEvents.mergedEvents.data(), runtime.mergedMidiEvents.mergedEvents.size());
}
TRACK_INTERNAL_MEM __attribute__((noinline)) void rebuildPlaybackOrder(
    Loop& loop, const SessionMidiEventVec& mergedEvents, const ProjectionContext& playbackContext) {
  noteContentSizedPlaybackBuildOnActiveTransport();
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

PlaybackEventStream makeFixedPlaybackStream(FixedPlaybackStreamCtx& ctx) {
  return PlaybackEventStream{&ctx, fixedPlaybackStreamSize, fixedPlaybackStreamValid,
                             fixedPlaybackStreamEventAt, mergedPlaybackStreamPhase};
}

const SessionMidiEventVec* buildMergedPlaybackFallbackCold(
    Track& track, Loop& loop, LoopPlaybackRuntime& runtime, uint32_t currentTick) {
  const bool rebuilt =
      ensurePlaybackMergedMidiEventsBuilt(track, loop, runtime, false, currentTick);
  if (rebuilt) {
    reconcilePlaybackLedgerAfterFullLoopRebuild(loop, runtime);
  }
  return &runtime.mergedMidiEvents.mergedEvents;
}

PreparedPairConsumeDecision resolvePreparedPairConsume(
    LoopPlaybackRuntime& runtime, const Loop& loop, uint32_t playbackRevision,
    uint32_t sourceGeneration, const void* snapshotIdentity, bool sealAdmissionPending,
    uint32_t tick) {
  PreparedPairConsumeDecision decision{};
  if (runtime.preparedPairIsCurrent(playbackRevision, sourceGeneration, snapshotIdentity,
                                    loop.loopLengthTicks) &&
      !sealAdmissionPending) {
    decision.pair = &runtime.preparedPair;
    return decision;
  }
  // The source changed under the running cursor. Request the replacement; never fold
  // accepted capture into the window this tick is emitting and never rebuild the order.
  runtime.requestPreparedPlaybackPair(playbackRevision, sourceGeneration, snapshotIdentity,
                                      loop.loopLengthTicks, tick);
  decision.requested = true;
  if (runtime.preparedPair.ready) {
    decision.pair = &runtime.preparedPair;
    decision.held = true;
  }
  return decision;
}

PlaybackEventStream makeCapturePlaybackStream(Loop& loop) {
  return PlaybackEventStream{&loop, capturePlaybackStreamSize, nullptr,
                             capturePlaybackStreamEventAt, capturePlaybackStreamPhase};
}
TRACK_COLD_MEM void Track::reanchorPlaybackCycle(uint32_t currentTick) {
  Loop& loop = getActiveLoop();
  if (LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(activeLoopIndex)) {
    runtime->bridgeStream.reset();
  }
  loop.nextEventIndex = 0;
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

TRACK_COLD_MEM void Track::resetPlaybackState(uint32_t currentTick) {
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

TRACK_COLD_MEM uint8_t Track::reportEnvelopeBreachIfAny(uint8_t slot, WorkingSetRole role) const {
  // S4.4: the declared caps are the measured envelope (design.md role classes). A role class held
  // beyond its declared cardinality is reported as a policy signal -- `reason=cap_reached`, distinct
  // from an allocation failure -- and the activation proceeds, because the resource model still
  // supports it. Enforcing here would reject supported multi-slot flows, and the policy is
  // accept-and-schedule (DEC-078 item 1), so this check reports and never refuses.
  //
  // It also *names* what it found: the census reads raw role bits, and a slot whose capture has already
  // stopped still holds `LiveCapture` until its working set is released, so the emitted holder is what
  // makes the signal diagnosable (and is what tells an operator that the case the contract requires --
  // an outgoing finalizer beside an incoming capture -- is being counted). Correcting that census is
  // S4b's role-transfer work.
  static constexpr WorkingSetLeaseCaps kDeclaredCaps{};
  if (slot >= Config::MAX_LOOPS_PER_TRACK) return kNoWorkingSetClassHolder;
  const WorkingSetRole held = playbackRuntime.workingSetRolesAt(slot);
  const WorkingSetRole added = role & static_cast<WorkingSetRole>(~static_cast<uint8_t>(held));
  if (added == WorkingSetRole::None) {
    return kNoWorkingSetClassHolder;  // idempotent re-admission is not a breach
  }
  for (uint8_t bit = 0; bit < 4; ++bit) {
    const WorkingSetRole single = static_cast<WorkingSetRole>(1u << bit);
    if (!hasRole(added, single)) continue;
    uint8_t heldElsewhere = 0;
    uint8_t holder = kNoWorkingSetClassHolder;
    for (uint8_t other = 0; other < Config::MAX_LOOPS_PER_TRACK; ++other) {
      if (other != slot && hasRole(playbackRuntime.workingSetRolesAt(other), single)) {
        // The first holder in slot order, so the report is deterministic when several hold the class.
        if (holder == kNoWorkingSetClassHolder) holder = other;
        ++heldElsewhere;
      }
    }
    if (heldElsewhere >= workingSetCapFor(kDeclaredCaps, single)) {
      MemoryMonitor::reportDeclaredCapDenied(MemoryMonitor::OperationKind::RecordStart, bit, holder);
      return holder;
    }
  }
  return kNoWorkingSetClassHolder;
}

namespace {
/// S4.1: a stable literal for the release outcome, so the capture row is readable and matchable rather
/// than a bare enum ordinal. Kept as literals for the same reason `reentryDecision` uses them.
const char* workingSetOutcomeLabel(WorkingSetOutcome outcome) {
  switch (outcome) {
    case WorkingSetOutcome::Released:
      return "released";
    case WorkingSetOutcome::RefusedBusy:
      return "refused_busy";
    case WorkingSetOutcome::StaleGeneration:
      return "stale_generation";
    case WorkingSetOutcome::NotHeld:
      return "not_held";
    default:
      return "other";
  }
}
}  // namespace

TRACK_COLD_MEM void Track::releaseEligiblePlaybackRuntimeSlots(bool preserveLedger) {
  // S4.1: the (slot, generation) release entry, evaluated through the liveness predicate. This
  // replaces the track-global `TrackPlaybackRuntime::resetAll`, which was predicate-free and
  // discarded every per-slot refusal -- a released slot could be reset underneath a live owner.
  for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
    if (!noLiveOwnerForSlot(slot)) {
      continue;
    }
    const uint16_t generation = playbackRuntime.slotGeneration(slot);
    const WorkingSetOutcome outcome = playbackRuntime.releaseSlot(slot, generation, preserveLedger);
    // The refusal is reported, not discarded: the pre-S4.1 sweep dropped it, which is how a release
    // refused by a live owner became invisible. `NotHeld` is the ordinary "nothing here" answer and is
    // deliberately silent, so a sweep over 56 mostly-idle slots does not flood the ring.
    if (outcome != WorkingSetOutcome::NotHeld) {
#if defined(SESSION_CAPTURE)
      DebugSessionCapture::workingSetLeaseReleased(slot, generation, workingSetOutcomeLabel(outcome));
#endif
    }
  }
}

TRACK_COLD_MEM void Track::invalidatePlaybackMergedMidiEvents(bool preserveLedger) {
  // Keep job-owned, generation-qualified scratch until terminal cleanup can
  // retry the reset. Overdub uses STOPPED recovery; Record retains its playback.
  if (hasPendingDeferredOverdubStop()) {
    // Record launches playback while its source/window is still job-owned. A
    // cache reset is not a transport Stop: preserve that job and its audible
    // destination until retirement. Explicit Stop retargets through its own entry.
    if (captureStopJob().mode == CapturePhase::Record) return;
    suppressMidiOutputForTransportStop();
    requestDeferredOverdubStopToStopped();
    return;
  }
  bumpPlaybackGeneration();
  releaseEligiblePlaybackRuntimeSlots(preserveLedger);
}
TRACK_COLD_MEM void Track::prewarmBootPlaybackRuntimeSlots() {
  ensureLoopsAllocated();
  const uint8_t trackIndex = resolveTrackIndexForPersistence(*this);
  for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
    if (!trackManager.isSlotEnabled(trackIndex, s) && !loopForSlot(s).hasCommittedPasses()) {
      continue;
    }
    // Boot reserves staging, not eight full view/bridge/companion bundles before SD load.
    // Explicit full prewarm remains separate.
    prewarmPlaybackForSlot(s, false);
  }
}

TRACK_COLD_MEM bool Track::prewarmPlaybackForSlot(uint8_t slotIndex, bool prepareCapture) {
  ensureLoopsAllocated();
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return false;
  }
  // `ok` is the capture-admission answer: every reserve that exists to keep the later
  // deferred stop grains allocation-free must have been established. Callers that are
  // admitting a take must act on it; the boot/load caller may ignore it.
  bool ok = true;
  // S4.4: surface a declared-envelope breach before the resources are taken. **Reported, not refused** --
  // the policy is accept-and-schedule (DEC-078 item 1), so the answer (the holder's slot, or 0xFF) is a
  // diagnostic and the admission proceeds below regardless of it.
  if (prepareCapture) {
    (void)reportEnvelopeBreachIfAny(slotIndex,
                                    WorkingSetRole::PlaybackWindow | WorkingSetRole::LiveCapture);
  }
  ExternalPoolAccounting::beginTag(ExternalPoolAccounting::TagCaptureStopJobs);
  ok = prewarmCaptureStopJobs() && ok;
  ExternalPoolAccounting::endTag();
  Loop& loop = loopForSlot(slotIndex);
  LoopPlaybackRuntime* runtime = playbackRuntime.trySlot(slotIndex);
  if (runtime != nullptr) {
    // This is a boot/load or pre-transport path. The hot MIDI path deliberately
    // has no equivalent allocation call.
    if (prepareCapture) {
      // Derived companion-row reserve for this slot (DEC-066): a loop whose canonical
      // payloads are sparse does not reserve the full row horizon, while a dense loop
      // still reaches the ceiling. The clamp lives in the owner.
      ExternalPoolAccounting::beginTag(ExternalPoolAccounting::TagViewPrewarm);
      // RC-MEM-2: reserve the whole-take representation only when this loop can actually
      // publish one. Past `kMaxDetailedWindowBars` the windowed policy is the only
      // authoritative representation, so the bridges and the prepared pair are dead
      // reservation; the three views and both capture stagings stay (capture admission and
      // the live-record/handoff lanes need them).
      ok = runtime->prewarmViews(loop.companionRowHorizonBound(),
                                 loop.fullTakePlaybackViewAdmissible()) && ok;
      ExternalPoolAccounting::endTag();
    }
    // Boot reserves no capture staging (DEC-065). Staging is provisioned by capture
    // admission - the record arm's `prewarmViews` above, or `Track::startOverdubbing`
    // for a loaded slot - so a slot that never records does not carry two
    // 2,048-event roles resident from boot.
    //
    // S2b.1 (invariant 34, DEC-078 item 8): boot/load is an **admission boundary**, so a
    // committed-but-inactive slot reserves no canonical-copy capacity either. It used to reserve
    // 8 pool chunks (41,056 B) per committed slot here, which is the RC-MEM-5 state: at ~60
    // committed slots that is 480/512 chunks before any activation. The canonical copy is now
    // acquired lazily at the two cold admission points that need it - the record arm
    // (`prepareCapture`, below) and `Track::startOverdubbing` - and the accepted stop's park
    // re-runs it in a deferred turn when it is absent. Boot still establishes the runtime slot,
    // the stop-descriptor ring and the canonical copy directory, which is what keeps admission
    // O(1) on the hot path.
    if (prepareCapture) {
      ExternalPoolAccounting::beginTag(ExternalPoolAccounting::TagCanonicalOutput);
      ok = runtime->prewarmCanonicalCaptureOutput() && ok;
      ExternalPoolAccounting::endTag();
    }
  } else if (prepareCapture) {
    // Without a runtime slot there is no staging and no canonical output, so the take
    // cannot be admitted on the terms this reserve exists to guarantee. `trySlot`
    // only returns null when the pool and the heap both refused the runtime itself.
    ok = false;
  }
#if defined(SESSION_CAPTURE)
  DebugSessionCapture::capturePrewarm(resolveTrackIndexForPersistence(*this), slotIndex,
      prepareCapture, runtime && runtime->canonicalCaptureOutputAdmitted(),
      runtime ? runtime->canonicalCaptureChunks.availableChunks() : 0,
      LoopEventStore::freeChunkCount(), MemoryMonitor::getInternalHeapFreeBytes());
#endif
  // Canonical ownership transfer is a pre-capture resource.  Explicit capture
  // preparation (the RECORDING entry point) must reserve the target loop's
  // horizon even when other tracks are already playing; otherwise the later
  // overdub command is rejected before it can enter the deferred stop path.
  //
  // S2b.1: the horizon is no longer admitted for a committed-but-inactive slot at boot. The old
  // `|| !anyTimingCriticalTrackActive()` clause existed to let the boot path reserve it for every
  // committed slot, which is the per-slot slope this stage removes. A loaded slot now acquires its
  // own horizon at `Track::startOverdubbing`'s cold admission (S2b.2), and the record arm keeps
  // this one.
  if (prepareCapture) {
    ExternalPoolAccounting::beginTag(ExternalPoolAccounting::TagTransferHorizon);
    ok = loop.prewarmActiveOverdubTransferHorizon() && ok;
    ExternalPoolAccounting::endTag();
    // Companion rows are part of the same deferred stop admission. Reserve
    // the complete fixed row horizon while the loop is cold so stop grains
    // never grow vectors or relocate canonical history.
    if (prepareCapture) {
      const size_t usedRows = std::min(loop.passes.editPasses.size(),
                                       RuntimePlaybackViewConfig::kRecordsPerBridge);
      // The grant is the smaller of the remaining row ceiling and this loop's derived
      // companion-row bound, so the preparation horizon never exceeds what this loop's
      // source notes can produce.
      const size_t companionCapacity =
          std::min(RuntimePlaybackViewConfig::kRecordsPerBridge - usedRows,
                   loop.companionRowHorizonBound());
      ExternalPoolAccounting::beginTag(ExternalPoolAccounting::TagCompanionHistory);
      ok = loop.prewarmPreparedCompanionHistory(companionCapacity) && ok;
      ExternalPoolAccounting::endTag();
    }
  }
  ExternalPoolAccounting::beginTag(ExternalPoolAccounting::TagPlaybackOrder);
  (void)loop.getPlaybackOrder();
  ExternalPoolAccounting::endTag();
  // S3.1: express the ownership this admission just established. `ok` is the admission answer, so
  // a lease is recorded only when the working set is actually usable; the record arm is the
  // admission caller and must act on a false answer. No resource moves, and the same reserves are
  // established exactly as before.
  if (ok && runtime != nullptr && prepareCapture) {
    runtime->acquireWorkingSetLease(WorkingSetRole::PlaybackWindow | WorkingSetRole::LiveCapture);
  }
  return ok;
}

TRACK_COLD_MEM bool Track::hasPlaybackOccupancyForSlot(uint8_t slotIndex) const {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) return false;
  return playbackRuntime.slotIfAllocated(slotIndex) != nullptr;
}

TRACK_COLD_MEM void Track::ensurePlaybackMergedEventsForSlot(uint8_t slotIndex) {
  ensureLoopsAllocated();
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return;
  }
  Loop& loop = loopForSlot(slotIndex);
  // S4b.6 (B5): **never** allocate here. This is reached from the switch commit on the clock path and from
  // a MIDI-input edge (`MidiButtonActions`), where taking the runtime is illegal; occupancy is the
  // provisioning owner's job (`TrackManager::provisionSlotForSwitch`, off-clock). A missing runtime is
  // therefore a no-op, and every caller that needs one has already been given it.
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return;
  }
  (void)loop.getPlaybackOrder();
  // Build destination merged MIDI before LoopEnd commit so launch is a cache hit.
  if (loop.hasCommittedPasses() && loop.loopLengthTicks > 0) {
    const uint32_t currentTick = clockManager.getCurrentTick();
    // The queued long target launches at phase zero; it has no transport projection
    // until commitQueuedPlaybackStart. Do not borrow the outgoing slot's position.
    const bool prepareLaunch = slotIndex != activeLoopIndex &&
        loop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks);
    const bool rebuilt =
        ensurePlaybackMergedMidiEventsBuilt(*this, loop, *runtime, true, currentTick, prepareLaunch);
    if (rebuilt) {
      reconcilePlaybackLedgerAfterFullLoopRebuild(loop, *runtime);
    }
    if (loop.playbackOrderDirty) {
      const ProjectionContext playbackContext = makePlaybackContext(*this, loop, currentTick);
      ::rebuildPlaybackOrder(loop, runtime->mergedMidiEvents.mergedEvents, playbackContext);
    }
    if (prepareLaunch && runtime->mergedMidiEvents.builtFromSnapshotIdentity != nullptr &&
        runtime->mergedMidiEvents.builtFromSnapshotIdentity == loop.publishedSnapshotIdentity()) {
      // Otherwise the first target tick resets the cache we just prepared.
      runtime->syncRevision(loop.playbackRevision, playbackGeneration);
    }
    // Task 2.4 slice 2: the working window just became coherent, so publish the
    // immutable pair the clock path consumes. Publication is the atomic authority
    // change; the clock path itself never builds or reanchors.
    (void)publishPreparedPlaybackPairFromWorkingWindow(slotIndex);
    // The full vector/order build is the STOPPED oracle until task 2.4 moves
    // the active clock path to the fixed view. Publish its completed window
    // only while every track is stopped; active transport must never turn the
    // oracle copy into a hidden rebuild on a different, parked track.
    if (!anyTimingCriticalTrackActive()) {
      const PlaybackCoverageInterval coverage{runtime->mergedMidiEvents.windowStartTick,
                                              runtime->mergedMidiEvents.windowLengthTicks,
                                              loop.loopLengthTicks};
      const PlaybackViewSourceIdentity sourceIdentity{
          runtime->mergedMidiEvents.builtFromRevision, playbackGeneration,
          runtime->mergedMidiEvents.builtFromSnapshotIdentity};
      const RuntimeCanonicalPassInputs::CurrentSource live{loop.playbackRevision, playbackGeneration,
          loop.loopLengthTicks, loop.canonicalStorageGeneration};
      if (!runtime->publishStoppedCanonicalOracleView(loop.passes, sourceIdentity, live,
              runtime->mergedMidiEvents.mergedEvents.data(), runtime->mergedMidiEvents.mergedEvents.size(), coverage)) {
        (void)runtime->publishStoppedOracleView(runtime->mergedMidiEvents.mergedEvents.data(),
                                                runtime->mergedMidiEvents.mergedEvents.size(),
                                                sourceIdentity, coverage);
      }
    }
  }
}

TRACK_COLD_MEM bool Track::publishPreparedPlaybackPairFromWorkingWindow(uint8_t slotIndex) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || !loopsAllocated()) {
    return false;
  }
  Loop& loop = loopForSlot(slotIndex);
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr || !loop.hasCommittedPasses() || loop.loopLengthTicks == 0) {
    return false;
  }
  PlaybackMergedMidiEvents& window = runtime->mergedMidiEvents;
  if (window.mergedEvents.empty() ||
      !isFullLoopMergedPlaybackWindow(window, loop.loopLengthTicks)) {
    // A long-loop windowed gather or an empty gather is not a full-loop pair; those
    // active paths keep their own handoff and are recorded as their own slice.
    return false;
  }
  const PlaybackOrderVec& order = loop.getPlaybackOrder();
  if (order.size() != window.mergedEvents.size()) {
    return false;
  }
  // The pair's identity is the live canonical source it represents. The seal-admit
  // path deliberately leaves the working window's `builtFromSnapshotIdentity` null
  // while still representing the current root plus the admitted pass, so the pair
  // records the live root identity that the clock path compares against.
  const PlaybackViewSourceIdentity source{loop.playbackRevision, playbackGeneration,
                                           loop.publishedSnapshotIdentity()};
  const PlaybackCoverageInterval coverage{0, loop.loopLengthTicks, loop.loopLengthTicks};
  const uint32_t publicationPhase =
      loop.lastTickInLoop == UINT32_MAX ? 0 : loop.lastTickInLoop;
  // The working order is a `size_t` permutation; the pair stores fixed uint16 rows.
  // Convert into a scratch view without allocating: the pair publisher copies the
  // events it needs, so a bounded stack conversion is not possible for 2048 rows
  // in ITCM. Reuse the pair buffer's merge scratch as the uint16 order source.
  uint16_t* converted = runtime->preparedPairStorage.mergeScratch;
  if (converted == nullptr || order.size() > runtime->preparedPairStorage.capacity) {
    return false;
  }
  for (size_t index = 0; index < order.size(); ++index) {
    if (order[index] > UINT16_MAX) {
      return false;
    }
    converted[index] = static_cast<uint16_t>(order[index]);
  }
  return runtime->publishPreparedPlaybackPair(
      window.mergedEvents.data(), window.mergedEvents.size(), converted, order.size(), source,
      coverage, publicationPhase);
}

TRACK_COLD_MEM bool Track::servicePendingPreparedPlaybackPair() {
  if (!loopsAllocated()) {
    return false;
  }
  for (uint8_t slotIndex = 0; slotIndex < Config::MAX_LOOPS_PER_TRACK; ++slotIndex) {
    const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
    if (runtime != nullptr && runtime->preparedPairRequestPending) {
      return servicePendingPreparedPlaybackPairSlot(slotIndex);
    }
  }
  return false;
}

TRACK_COLD_MEM bool Track::servicePendingPreparedPlaybackPairSlot(uint8_t slotIndex) {
  ensureLoopsAllocated();
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return false;
  }
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr || !runtime->preparedPairRequestPending) {
    return false;
  }
  Loop& loop = loopForSlot(slotIndex);
  if (!loop.hasCommittedPasses() || loop.loopLengthTicks == 0) {
    // Nothing to prepare: retire the request rather than retrying it every turn.
    runtime->preparedPairRequestPending = false;
    return false;
  }
  const uint32_t currentTick = clockManager.getCurrentTick();
  // Capture the request tick before publication clears it, so the measured latency is
  // request -> publish rather than publish -> publish.
  const uint32_t requestTick = runtime->preparedPairRequestTick;
  const bool rebuilt =
      ensurePlaybackMergedMidiEventsBuilt(*this, loop, *runtime, true, currentTick);
  if (rebuilt) {
    reconcilePlaybackLedgerAfterFullLoopRebuild(loop, *runtime);
  }
  if (loop.playbackOrderDirty || loop.getPlaybackOrder().size() !=
                                     runtime->mergedMidiEvents.mergedEvents.size()) {
    const ProjectionContext playbackContext = makePlaybackContext(*this, loop, currentTick);
    ::rebuildPlaybackOrder(loop, runtime->mergedMidiEvents.mergedEvents, playbackContext);
  }
  const bool published = publishPreparedPlaybackPairFromWorkingWindow(slotIndex);
  if (published) {
    const uint32_t latency = currentTick >= requestTick ? currentTick - requestTick : 0;
    if (latency > runtime->preparedPairMaxLatencyTicks) {
      runtime->preparedPairMaxLatencyTicks = latency;
    }
  } else {
    // The request is retired even on denial: an unrepresentable window (long loop,
    // empty gather) is not a pending pair, and retrying it would spin the owner.
    runtime->preparedPairRequestPending = false;
  }
  return published;
}

TRACK_COLD_MEM bool Track::hasPendingPreparedPlaybackPairRequest() const {
  if (!loopsAllocated()) {
    return false;
  }
  for (uint8_t slotIndex = 0; slotIndex < Config::MAX_LOOPS_PER_TRACK; ++slotIndex) {
    const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
    if (runtime != nullptr && runtime->preparedPairRequestPending) {
      return true;
    }
  }
  return false;
}

TRACK_COLD_MEM Track::PreparedPairMetrics Track::preparedPairMetrics(uint8_t slotIndex) const {
  PreparedPairMetrics metrics{};
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return metrics;
  }
  metrics.requestCount = runtime->preparedPairRequestCount;
  metrics.publishCount = runtime->preparedPairPublishCount;
  metrics.holdTicks = runtime->preparedPairHoldTicks;
  metrics.maxLatencyTicks = runtime->preparedPairMaxLatencyTicks;
  metrics.rebuildUnits = runtime->preparedPairRebuildUnits;
  metrics.publishedGeneration = runtime->preparedPair.generation;
  metrics.publishedEventCount = runtime->preparedPair.eventCount;
  return metrics;
}

TRACK_COLD_MEM void Track::resetPreparedPairMetrics() {
  if (!loopsAllocated()) {
    return;
  }
  for (uint8_t slotIndex = 0; slotIndex < Config::MAX_LOOPS_PER_TRACK; ++slotIndex) {
    LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
    if (runtime != nullptr) {
      runtime->resetPreparedPairCounters();
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
  const auto& window = runtime->mergedMidiEvents;
  if (loop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks)) {
    // Compatibility launch receipt only; immutable readiness has its own predicate.
    // A non-null gathered source distinguishes valid empty coverage from a gather miss.
    return !runtime->isStale(loop.playbackRevision, playbackGeneration) &&
        window.builtFromRevision == loop.playbackRevision &&
        window.builtFromSnapshotIdentity != nullptr &&
        window.builtFromSnapshotIdentity == loop.publishedSnapshotIdentity() &&
        window.windowStartTick == 0 &&
        window.windowLengthTicks == 2 * Config::TICKS_PER_BAR &&
        !loop.playbackOrderDirty;
  }
  return window.builtFromRevision == loop.playbackRevision && !window.mergedEvents.empty();
}

TRACK_COLD_MEM Track::PlaybackWindowReadiness Track::playbackWindowReadinessForSlot(
    uint8_t slotIndex) const {
  // AC-3 / task 3.5: readiness is the **completed publication**, and its failure names its holder. This is the
  // publication answer; the red-first body it replaced (readiness read from the legacy merged buffer) is the mutation
  // recorded in the slice's evidence.
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || !loopsAllocated()) {
    return PlaybackWindowReadiness::NoRuntime;
  }
  const Loop& loop = loopForSlot(slotIndex);
  if (!loop.hasCommittedPasses() || loop.loopLengthTicks == 0) {
    return PlaybackWindowReadiness::NotServable;
  }
  if (playbackRuntime.slotIfAllocated(slotIndex) == nullptr) {
    return PlaybackWindowReadiness::NoRuntime;
  }
  // AC-3: the answer is the **window this change published** -- never the live merged buffer -- so an interval that
  // legitimately holds no events is still ready. Currency (DEC-108 item 5) is `playbackRevision` and exactly it; the
  // one-pass-late allowance that would accept an older revision is task 3.6's rule.
  PlaybackWindowIdentity published{};
  if (!publishedPlaybackWindowIdentityForSlot(slotIndex, published)) {
    // Nothing is published. **Who owes it** turns on whether a build could succeed at all: the enumeration needs a
    // *current* published root (`requestPlaybackWindow` refuses `SourceUnavailable` / `SourceNotCurrent`), so a
    // rootless or not-current slot owes the **source**, and only a sourced one owes the window-build owner.
    return loop.publishedSnapshotIsCurrent() ? PlaybackWindowReadiness::OwesBuild
                                            : PlaybackWindowReadiness::OwesSource;
  }
  if (published.playbackRevision != loop.playbackRevision) {
    return PlaybackWindowReadiness::OwesCurrency;
  }
  // A complete, current window is ready; readiness does not count its events.
  return PlaybackWindowReadiness::Ready;
}

TRACK_COLD_MEM const char* Track::playbackWindowReadinessHolder(PlaybackWindowReadiness readiness) {
  // No name table and no `switch`: either is `.data` on this target, and a few `.data` bytes can snap the following
  // `ALIGN(1024)` and cost 4 KB of DTCM (the RAM1 rule's finding, measured for slice 3.1). The literals below are also
  // **not** free: this script places `.rodata` in DTCM, and the ~100 B they add crossed the `_VectorsRam`/periodic-table
  // alignment edge and cost 4 KB of the local allowance (`variables` 96,192 -> 100,288 with `code` byte-identical).
  // Slice 3.5's fix is the pattern the script already uses for the other two TUs that hit this: this TU's string
  // literals are routed to memory-mapped FLASH. So the names cost no DTCM here, and the whole TU's rows benefit.
  if (readiness == PlaybackWindowReadiness::Ready) {
    return "ready";
  }
  if (readiness == PlaybackWindowReadiness::OwesBuild) {
    return "window_build_owner";
  }
  if (readiness == PlaybackWindowReadiness::OwesSource) {
    return "published_source";
  }
  if (readiness == PlaybackWindowReadiness::OwesCurrency) {
    return "window_rebuild";
  }
  if (readiness == PlaybackWindowReadiness::NotServable) {
    return "not_servable";
  }
  if (readiness == PlaybackWindowReadiness::NoRuntime) {
    return "slot_occupancy_owner";
  }
  return "unknown";
}
TRACK_COLD_MEM Track::PlaybackSourceRetainResult Track::retainPlaybackSourceForSlot(uint8_t slotIndex,
                                                                                  uint32_t revision,
                                                                                  uint32_t generation) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || !loopsAllocated()) {
    return PlaybackSourceRetainResult::NoRuntime;
  }
  Loop& loop = loopForSlot(slotIndex);
  if (!loop.hasCommittedPasses() || loop.loopLengthTicks == 0) {
    return PlaybackSourceRetainResult::NotServable;
  }
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return PlaybackSourceRetainResult::NoRuntime;
  }
  RetainedPlaybackSource* entry = runtime->retainedSourceForRevision(revision);
  if (entry != nullptr) {
    // The identity is the revision: re-admitting refreshes the role (it is serving again) and the generation, and
    // never re-owns the bodies -- so a repeated admission cannot double the store's references.
    entry->generation = generation;
    entry->role = RetainedSourceRole::Serving;
    for (RetainedPlaybackSource& other : runtime->retainedSources) {
      if (&other != entry && other.role == RetainedSourceRole::Serving) {
        other.role = RetainedSourceRole::Retiring;
      }
    }
    return PlaybackSourceRetainResult::AlreadyRetained;
  }
  entry = runtime->freeRetainedSourceSlot();
  if (entry == nullptr) {
    return PlaybackSourceRetainResult::RoleCapacityReached;
  }
  // Two protections, each read by the consumer that would otherwise destroy the source. The descriptor pins are what
  // `Loop::reclaimUnreferencedDisabled*Passes` honours; the owned references are what stops the store's
  // `releaseChunkRefs` on the direct reset/replacement path from freeing or recycling the same bodies underneath a
  // retained window. Both describe one revision, and both are dropped by the one release predicate.
  pinPassesFromLoopPasses(loop.passes, entry->pins);
  entry->ownedChunks.clear();
  if (loop.passes.hasRecordPass()) {
    for (uint16_t id : loop.passes.recordPass.committedChunkIds) {
      entry->ownedChunks.push_back(id);
    }
  }
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    for (uint16_t id : pass.committedChunkIds) {
      entry->ownedChunks.push_back(id);
    }
  }
  LoopEventStore::retainChunkRefs(entry->ownedChunks);
  entry->revision = revision;
  entry->generation = generation;
  entry->role = RetainedSourceRole::Serving;
  for (RetainedPlaybackSource& other : runtime->retainedSources) {
    if (&other != entry && other.role == RetainedSourceRole::Serving) {
      other.role = RetainedSourceRole::Retiring;
    }
  }
  // Task 3.6c: the **index is published with the source revision** -- a rebuild per admitted revision covers both the
  // old and the new affected ranges by construction and never modifies an index a reader holds. A refusal here is not
  // fatal: the retention is the source of truth, and a caller that needs an interval answer gets a named result.
  (void)buildRetainedIntervalIndexForSlot(slotIndex);
  return PlaybackSourceRetainResult::Admitted;
}

TRACK_COLD_MEM bool Track::retainedPlaybackSourceForSlot(uint8_t slotIndex,
                                                         RetainedPlaybackSource& out) const {
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return false;
  }
  for (const RetainedPlaybackSource& entry : runtime->retainedSources) {
    if (entry.isRetained()) {
      out = entry;
      return true;
    }
  }
  return false;
}

TRACK_COLD_MEM bool Track::hasRetainedPlaybackSourceForSlot(uint8_t slotIndex) const {
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  return runtime != nullptr && runtime->hasRetainedPlaybackSource();
}

TRACK_COLD_MEM size_t Track::retainedPlaybackSourceCountForSlot(uint8_t slotIndex) const {
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return 0;
  }
  size_t count = 0;
  for (const RetainedPlaybackSource& entry : runtime->retainedSources) {
    if (entry.isRetained()) {
      ++count;
    }
  }
  return count;
}

TRACK_COLD_MEM bool Track::tryReleaseRetainedPlaybackSource(uint8_t slotIndex) {
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return false;
  }
  const Loop& loop = loopForSlot(slotIndex);
  bool released = false;
  for (RetainedPlaybackSource& entry : runtime->retainedSources) {
    if (!entry.isRetained()) {
      continue;
    }
    // **The one release predicate.** Every clause is a reason the source is still needed; the source is dropped only
    // when none holds. The builder clause is deliberately conservative for this slice: while a build is outstanding
    // its frozen source is not inspectable from here, so the sources are held rather than risk releasing one a
    // builder is reconstructing from. The finer per-builder term is named as owed, not assumed away.
    // The content the source describes can also be **gone** -- a clear/reset or a replacement load -- and then the
    // counter never advances past the retained revision, so it is released here too rather than held forever. Still
    // this one predicate, and still only when nothing references it.
    const bool contentGone = !loop.hasCommittedPasses() || loop.loopLengthTicks == 0;
    if (!contentGone && entry.revision >= loop.playbackRevision) {
      continue;
    }
    PlaybackWindowIdentity published{};
    if (publishedPlaybackWindowIdentityForSlot(slotIndex, published) &&
        published.playbackRevision == entry.revision) {
      continue;
    }
    if (runtime->coverageBuildNeedsService()) {
      continue;
    }
    if (runtime->ownsLiveFinalization()) {
      continue;
    }
    LoopEventStore::releaseChunkRefs(entry.ownedChunks);
    entry.clear();
    released = true;
  }
  return released;
}

TRACK_COLD_MEM bool Track::mergeRetainedPlaybackPinsInto(uint8_t slotIndex, SlotPassReferences& into) const {
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return false;
  }
  bool merged = false;
  for (const RetainedPlaybackSource& entry : runtime->retainedSources) {
    if (!entry.isRetained()) {
      continue;
    }
    for (PassId id : entry.pins.capturePassIds) {
      into.pinCapturePass(id);
      merged = true;
    }
    for (EditPassId id : entry.pins.editPassIds) {
      into.pinEditPass(id);
      merged = true;
    }
  }
  return merged;
}


TRACK_COLD_MEM PlaybackLagDecision Track::resolvePlaybackLag(bool servedIsCurrent, bool replacementReady,
                                                            uint8_t missedTraversals) {
  // The whole rule, and it is **total**: no branch waits, defers or blocks, which is what "a consumer is never blocked
  // by a missed deadline" means operationally.
  if (servedIsCurrent) {
    return PlaybackLagDecision::Current;
  }
  if (replacementReady) {
    // The replacement was published before this (following, eligible) traversal, so the boundary may serve it.
    return PlaybackLagDecision::Current;
  }
  // `missedTraversals` is the count *before* this traversal's miss: the first miss is the one-pass-late allowance, and
  // any later one is the named breach. Note what is deliberately absent: no branch resets the count because the
  // desired revision changed -- a further edit must not forgive an outstanding miss (AC-5).
  return missedTraversals >= 1 ? PlaybackLagDecision::BreachSecondMiss : PlaybackLagDecision::PreviousOnePass;
}

TRACK_COLD_MEM PlaybackLagDecision Track::advancePlaybackLagTraversal(uint8_t slotIndex, uint32_t intervalStart,
                                                                     uint32_t intervalLength,
                                                                     uint32_t desiredRevision,
                                                                     bool replacementReady) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || !loopsAllocated()) {
    return PlaybackLagDecision::Current;
  }
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return PlaybackLagDecision::Current;
  }
  PlaybackLagState& lag = runtime->lagState;
  if (!lag.coversInterval(intervalStart, intervalLength)) {
    // A different interval is a different episode: the allowance is per interval **and** per pass.
    lag.resetEpisode(intervalStart, intervalLength);
  }
  PlaybackWindowIdentity published{};
  const bool hasPublished = publishedPlaybackWindowIdentityForSlot(slotIndex, published);
  const bool servedIsCurrent = hasPublished && published.playbackRevision == desiredRevision;
  const PlaybackLagDecision decision = resolvePlaybackLag(servedIsCurrent, replacementReady, lag.missedTraversals);
  lag.servedRevision = hasPublished ? published.playbackRevision : 0;
  lag.desiredRevision = desiredRevision;
  if (decision == PlaybackLagDecision::Current) {
    // The boundary may serve the current revision, so the episode is over and the next one starts clean. Task 4.5's
    // fault clears **here** and nowhere else: an eligible boundary is the only thing that ends a latched fault, never a
    // further edit and never a silent reset.
    lag.missedTraversals = 0;
    lag.breachReported = false;
    runtime->deadlineContractFaultLatched = false;
    return decision;
  }
  // The **consequence** of one missed traversal lives in one place (`noteMissedTraversalForSlot`), because a missed
  // traversal now arrives from two independent judgements: the revision rule below and the **deadline** the admission
  // promised (blocker 3, half 2). What differs is the judgement; what a miss *means* does not.
  if (decision == PlaybackLagDecision::BreachSecondMiss || decision == PlaybackLagDecision::PreviousOnePass) {
    noteMissedTraversalForSlot(slotIndex, loopForSlot(slotIndex));
  }
  return decision;
}

/// DEC-108 §13a/§13(e) / task 3.10 (**blocker 3, half 2**): **what one missed traversal means** -- the one
/// implementation, reached from both judgements a miss can arrive from. The first miss of the episode is the allowance
/// the spec protects (`one_pass_late`: the previous revision stays authoritative for that traversal); the **second
/// consecutive** one is the named contract breach, latched so containment can read it (and cleared only at an eligible
/// boundary, in `advancePlaybackLagTraversal`). Nothing here publishes, adopts, rebuilds or stops the clock, and nothing
/// here ever extends a deadline: the deadline is never silently re-granted.
TRACK_COLD_MEM void Track::noteMissedTraversalForSlot(uint8_t slotIndex, Loop& loop) {
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return;
  }
  PlaybackLagState& lag = runtime->lagState;
  if (lag.missedTraversals < UINT8_MAX) {
    ++lag.missedTraversals;
  }
  if (lag.missedTraversals == 1) {
    // AC-5's *"the stale condition continues to be named"*: the allowance is named once per traversal, which is the rate
    // the condition itself is at.
    reportPlaybackViewMaintenanceRow(slotIndex, loop, "one_pass_late");
    return;
  }
  if (lag.missedTraversals >= 2 && !lag.breachReported) {
    // A **report**, not a decision (the S4b.7 hold terminal's shape): named once per episode, with the latch task 4.5
    // reads. The previous revision keeps being served and no partial content is published.
    lag.breachReported = true;
    ++lag.breachReports;
    runtime->deadlineContractFaultLatched = true;
    reportPlaybackViewMaintenanceRow(slotIndex, loop, "lag_breach");
  }
}

/// Bounded accounting for unfinished or withheld candidates. A completed, published
/// window owes no preparation deadline. Completion itself is judged synchronously
/// with a fresh clock, before publication, never against a later dispatch timestamp.
TRACK_COLD_MEM void Track::serviceAdmittedDeadlineMisses(PlaybackDeadlineClock clock) {
  if (clock.epoch == 0) {
    return;  // an unpositioned promise cannot be late
  }
  for (uint8_t slotIndex = 0; slotIndex < Config::MAX_LOOPS_PER_TRACK; ++slotIndex) {
    LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
    if (runtime == nullptr) {
      continue;
    }
    // Unadmitted obligations retain their deadline but cannot auto-requalify on
    // a later lap or by returning to an invalidated epoch. This is not an admitted
    // completion miss: report the blocked obligation separately from the lag rule.
    for (auto* pending : {&runtime->pendingViewRequest_, &runtime->frozenViewRequest}) {
      if (!pending->active || pending->sourceKind != PlaybackViewPendingRequest::SourceKind::CommittedSnapshot ||
          pending->operationGeneration != playbackGeneration || pending->positionInvalidated ||
          pending->clockEpoch == 0) continue;
      const auto invalid = pending->deadlineInvalidation(loopForSlot(slotIndex).loopLengthTicks, clock.epoch);
      if (invalid != PlaybackViewPendingReason::None || clock.tick < pending->work.admittedTick ||
          clock.tick >= pending->deadlineTick) {
        pending->positionInvalidated = true;
        pending->reason = invalid != PlaybackViewPendingReason::None ? invalid :
            clock.tick < pending->work.admittedTick ? PlaybackViewPendingReason::DeadlineInvalidatedEpoch :
            PlaybackViewPendingReason::DeadlineExpired;
        reportPlaybackViewMaintenanceRow(slotIndex, loopForSlot(slotIndex), "pending_position_invalidated");
      }
    }
    const bool inFlight = runtime->activeViewBuildWork() != nullptr;
    if (!inFlight && !runtime->completedPromisePendingJudgement) {
      continue;
    }
    const PlaybackViewAdmittedWork& work = runtime->viewBuildWork;
    if (work.clockEpoch == 0 || work.admittedLoopLength == 0) {
      continue;  // never positioned: nothing is claimed for it
    }
    if (work.clockEpoch != clock.epoch || clock.tick < work.admittedTick ||
        loopForSlot(slotIndex).loopLengthTicks != work.admittedLoopLength) {
      // DEC-108 §13(c): a changed clock invalidates the **promise**, not the content -- the request is retained, the valid
      // audible source continues, and the miss is **reported**. Named once per promise, and deliberately **not** a
      // traversal miss: the interval's history is untouched, so an epoch change can never mint a fresh allowance.
      if (!runtime->admittedPromiseInvalidated) {
        runtime->admittedPromiseInvalidated = true;
        runtime->invalidatedPromiseTick = work.admittedTick;
        reportPlaybackViewMaintenanceRow(slotIndex, loopForSlot(slotIndex), "promise_epoch_invalidated");
      }
      continue;
    }
    // An invalidated positional promise cannot become valid by returning an old stamp.
    if (runtime->admittedPromiseInvalidated || clock.tick < work.deadlineTick) {
      continue;
    }
    // How many traversals of the promised interval have passed? The deadline is *the next entry* into it, so every
    // further pass of the loop is one more missed traversal -- and the count saturates where the policy saturates.
    const uint64_t passed = 1u + (clock.tick - work.deadlineTick) / work.admittedLoopLength;
    const uint32_t target = passed > UINT16_MAX ? UINT16_MAX : static_cast<uint32_t>(passed);
    if (work.admittedTick != runtime->countedMissPromiseTick) {
      // A **new promise**: its own count starts at zero, and the episode belongs to the interval it names (the allowance
      // is per interval *and* per pass).
      runtime->countedMissPromiseTick = work.admittedTick;
      runtime->countedMissedTraversals = 0;
      // **Request deduplication is not fault history** (DEC-108 §13(a): *"a later edit never resets an earlier interval's
      // missed-deadline allowance"*). The per-promise traversal count starts at zero -- that is the dedup -- while the
      // **episode** is reset only when the *interval* changes, so a repeated request for the same interval cannot mint a
      // fresh first-miss allowance.
      if (!runtime->lagState.coversInterval(work.handoffPhase, work.admittedIntervalLength)) {
        runtime->lagState.resetEpisode(work.handoffPhase, work.admittedIntervalLength);
      }
    }
    if (target > runtime->countedMissedTraversals) {
      const uint16_t before = runtime->countedMissedTraversals;
      const uint32_t newlyCounted = static_cast<uint32_t>(target) - before;
      runtime->countedMissedTraversals = static_cast<uint16_t>(target);
      // Account **exactly** the traversals that just passed, and only those that still change the policy: the allowance,
      // then the breach. Beyond the second nothing new is named and nothing new is latched, so the work per turn stays
      // bounded -- and because the loop is bounded by `newlyCounted`, one missed traversal can never be accounted twice.
      for (uint32_t accounted = 0; accounted < newlyCounted; ++accounted) {
        if (before + accounted >= 2u) {
          break;
        }
        noteMissedTraversalForSlot(slotIndex, loopForSlot(slotIndex));
      }
    }
  }
}

TRACK_COLD_MEM Track::LastResortOutcome Track::applyLastResortForSlot(uint8_t slotIndex, uint32_t phase) {
  if (containmentForSlot(slotIndex, phase) != ContinuityContainment::LastResortTargetedSilence) {
    return LastResortOutcome::NotNeeded;
  }
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return LastResortOutcome::NotNeeded;
  }
  // The approved last resort, and **both halves are slot-scoped by construction**: the closures go out for this slot's
  // ledger keys through `silenceSlotMidiOutput` -- not the track-wide or the global entry point -- and the ledger is
  // cleared so the closed notes owe nothing further. Nothing here rebuilds anything, so this cannot become an unbounded
  // rebuild, and nothing here touches the transport, so it cannot become a global stop.
  silenceSlotMidiOutput(slotIndex);
  runtime->ledger.clear();
  reportPlaybackViewMaintenanceRow(slotIndex, loopForSlot(slotIndex), "last_resort_slot_silence");
  return LastResortOutcome::SlotSilenced;
}

TRACK_COLD_MEM ContinuityContainment Track::containmentForSlot(uint8_t slotIndex, uint32_t phase) const {
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return ContinuityContainment::Nominal;
  }
  // "Valid coverage remains" is a **coverage** question, never a content question: the served window must still be
  // readable and its interval must still cover the traversal's phase. An event-empty window still covers its interval,
  // which is exactly why an empty window means "no new events" rather than a reason to fall silent.
  PlaybackViewSourceIdentity identity{};
  PlaybackCoverageInterval coverage{};
  const bool validCoverageRemains =
      publishedPlaybackWindowForSlot(slotIndex, identity, coverage) && coverage.contains(phase);
  return resolveContinuityContainment(runtime->deadlineContractFaultLatched, validCoverageRemains);
}

TRACK_COLD_MEM bool Track::deadlineContractFaultLatchedForSlot(uint8_t slotIndex) const {
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  return runtime != nullptr && runtime->deadlineContractFaultLatched;
}

TRACK_COLD_MEM PlaybackLagDecision Track::playbackLagDecisionForSlot(uint8_t slotIndex) const {
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return PlaybackLagDecision::Current;
  }
  const uint8_t missed = runtime->lagState.missedTraversals;
  if (missed >= 2) {
    return PlaybackLagDecision::BreachSecondMiss;
  }
  return missed == 1 ? PlaybackLagDecision::PreviousOnePass : PlaybackLagDecision::Current;
}

TRACK_COLD_MEM uint8_t Track::playbackLagMissedTraversalsForSlot(uint8_t slotIndex) const {
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  return runtime == nullptr ? 0 : runtime->lagState.missedTraversals;
}

TRACK_COLD_MEM uint8_t Track::playbackLagBreachReportsForSlot(uint8_t slotIndex) const {
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  return runtime == nullptr ? 0 : runtime->lagState.breachReports;
}

TRACK_COLD_MEM bool Track::playbackLagBreachReportedForSlot(uint8_t slotIndex) const {
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  return runtime != nullptr && runtime->lagState.breachReported;
}

TRACK_COLD_MEM Track::RetainedIndexBuildResult Track::buildRetainedIntervalIndexForSlot(uint8_t slotIndex) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || !loopsAllocated()) {
    return RetainedIndexBuildResult::NoRuntime;
  }
  Loop& loop = loopForSlot(slotIndex);
  if (!loop.hasCommittedPasses() || loop.loopLengthTicks == 0) {
    return RetainedIndexBuildResult::NotServable;
  }
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return RetainedIndexBuildResult::NoRuntime;
  }
  const RetainedPlaybackSource* source = nullptr;
  for (const RetainedPlaybackSource& entry : runtime->retainedSources) {
    if (entry.isRetained()) {
      source = &entry;
      break;
    }
  }
  if (source == nullptr) {
    // No pinned bodies, so nothing to describe -- and no index is left behind from an earlier revision.
    runtime->retainedIndex.clear();
    return RetainedIndexBuildResult::NoRetainedSource;
  }
  const uint32_t bars = (loop.loopLengthTicks + Config::TICKS_PER_BAR - 1) / Config::TICKS_PER_BAR;
  if (bars == 0 || bars > kRetainedIndexMaxBars) {
    // Refused **by name**: an index that answered only part of the loop would present the complete scan as
    // window-bounded, which the spec forbids. The caller accounts the full scan instead.
    runtime->retainedIndex.clear();
    return RetainedIndexBuildResult::BarsUnsupported;
  }
  // Build a **candidate** and publish it with a single assignment: nothing a reader already holds is modified, and the
  // revision is stamped inside the candidate, so no reader can see one revision's bits under another's stamp.
  RetainedIntervalIndex candidate;
  candidate.bodyWords.assign((bars + 31u) / 32u, 0u);
  uint16_t spans = 0;
  const auto foldChunks = [&](const CommittedChunkIdList& chunks) {
    for (uint16_t id : chunks) {
      uint32_t firstTick = 0;
      uint32_t lastTick = 0;
      if (!LoopEventStore::chunkTickSpan(id, firstTick, lastTick)) {
        continue;
      }
      ++spans;
      const uint32_t firstBar = (firstTick / Config::TICKS_PER_BAR) % bars;
      uint32_t lastBar = (lastTick / Config::TICKS_PER_BAR) % bars;
      if (lastBar < firstBar) {
        lastBar = bars - 1;  // a span that wraps ends at the loop's last bar
      }
      for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
        candidate.bodyWords[bar / 32u] |= (1u << (bar % 32u));
      }
    }
  };
  if (loop.passes.hasRecordPass()) {
    foldChunks(loop.passes.recordPass.committedChunkIds);
  }
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    foldChunks(pass.committedChunkIds);
  }
  // The **edit** passes' bodies are where moved events live, so they are folded in as well: the index covers the edited
  // destinations, and applying them (ordering, effective content) stays the effective-content resolver's job.
  // An **edit** pass carries no chunk bodies -- a moved note's events still live in the capture bodies -- but its
  // *destination* ticks are where the effective content will be. Folding them is what stops the index skipping a bar
  // whose only effective content is a moved note: "lookup SHALL include edit-moved events at their effective
  // destinations". Over-marking is the safe direction here (it costs inspection, never a missed event).
  for (const EditPass& pass : loop.passes.editPasses) {
    if (pass.state != EditPassState::Active) {
      continue;
    }
    const uint32_t firstBar = (pass.startTick / Config::TICKS_PER_BAR) % bars;
    const uint32_t endBar = (pass.endTick / Config::TICKS_PER_BAR) % bars;
    uint32_t lastBar = firstBar;
    if (pass.endTick > pass.startTick) {
      lastBar = endBar < firstBar ? bars - 1 : endBar;
    }
    for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
      candidate.bodyWords[bar / 32u] |= (1u << (bar % 32u));
    }
    ++spans;
  }
  candidate.generation = source->generation;
  candidate.bars = bars;
  candidate.spansIndexed = spans;
  candidate.revision = source->revision;
  candidate.published = true;
  runtime->retainedIndex = std::move(candidate);
  return RetainedIndexBuildResult::Published;
}

TRACK_COLD_MEM Track::RetainedIntervalQueryResult Track::queryRetainedIntervalForSlot(
    uint8_t slotIndex, uint32_t startPhase, uint32_t lengthTicks, uint32_t expectedRevision,
    uint32_t& indexedBarsOut, uint32_t& skippedBarsOut) const {
  indexedBarsOut = 0;
  skippedBarsOut = 0;
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return RetainedIntervalQueryResult::NoIndex;
  }
  const RetainedIntervalIndex& index = runtime->retainedIndex;
  if (!index.published || index.bars == 0) {
    return RetainedIntervalQueryResult::NoIndex;
  }
  if (index.revision != expectedRevision) {
    // A named refusal, never a silently stale answer: the index belongs to the revision it was built for.
    return RetainedIntervalQueryResult::RevisionMismatch;
  }
  const uint32_t loopLength = loopForSlot(slotIndex).loopLengthTicks;
  if (loopLength == 0 || lengthTicks == 0) {
    return RetainedIntervalQueryResult::NoIndex;
  }
  const uint32_t start = startPhase % loopLength;
  const uint32_t spanEnd = start + lengthTicks;
  const uint32_t end = spanEnd <= loopLength ? spanEnd : loopLength;  // half-open, clamped into the loop
  const uint32_t firstBar = start / Config::TICKS_PER_BAR;
  const uint32_t lastBar = (end - 1) / Config::TICKS_PER_BAR;
  if (lastBar >= index.bars) {
    // A bar outside the indexed range: the honest answer is "not covered", counted as a refusal rather than skipped.
    return RetainedIntervalQueryResult::BarsUnsupported;
  }
  for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
    if (index.coversBar(bar)) {
      ++indexedBarsOut;  // the caller enumerates this bar's bodies
    } else {
      ++skippedBarsOut;  // and this one costs no chunk inspection at all
    }
  }
  return RetainedIntervalQueryResult::Indexed;
}

TRACK_COLD_MEM void Track::applyPlaybackViewTransition(uint8_t slotIndex, PlaybackViewTransition transition) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || !loopsAllocated()) {
    return;
  }
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return;
  }
  const PlaybackViewTransitionEffect effect = resolvePlaybackViewTransition(transition);
  RuntimePlaybackViewSet* set = runtime->viewSetIfPrewarmed();
  if (effect.candidateReleased) {
    // The candidate's resources are released here, and **only** its own: the served window and the retained source
    // belong to other owners and are untouched by this.
    runtime->discardCoverageBuild();
    runtime->viewBuildOperationGeneration = 0;
  }
  if (set != nullptr && effect.preparedSuccessorRetired &&
      set->lifecycle.ready < RuntimePlaybackViewConfig::kViewCount) {
    // A prepared successor must not be adopted: its content is dropped and its storage returns to the free roles.
    set->views[set->lifecycle.ready].resetMetadata();
  }
  if (effect.identityInvalidated) {
    // Generation-qualified identity (DEC-108 item 5 / §13d): a returning slot finds a *new* operation identity, so no
    // earlier operation can be revived or adopted, and a completion carrying the old generation is late by definition.
    bumpPlaybackGeneration();
  }
  // `servedWindowStaysReadable` is the invariant every row carries: this function never touches the served window. The
  // frozen source's hold is the retained source's business, released only by its one predicate (task 3.6a).
}

TRACK_COLD_MEM Track::PlaybackViewRoleState Track::playbackViewRoleStateForSlot(uint8_t slotIndex) const {
  PlaybackViewRoleState state{};
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return state;
  }
  PlaybackViewSourceIdentity identity{};
  PlaybackCoverageInterval coverage{};
  state.servedWindowReadable = publishedPlaybackWindowForSlot(slotIndex, identity, coverage);
  state.buildOwed = runtime->coverageBuildNeedsService();
  const RuntimePlaybackViewSet* set = runtime->viewSetIfPrewarmed();
  if (set != nullptr) {
    const uint8_t ready = set->lifecycle.ready;
    state.preparedSuccessorWaiting = ready < RuntimePlaybackViewConfig::kViewCount && set->views[ready].complete;
  }
  state.operationGeneration = runtime->viewBuildOperationGeneration;
  return state;
}

TRACK_COLD_MEM void Track::releasePlaybackMergedMidiEventsMemory() {
  releaseEligiblePlaybackRuntimeSlots(false);
}

TRACK_COLD_MEM bool Track::hasLiveCaptureStopJobForSlot(uint8_t slot) const {
  if (captureStopJobs_ == nullptr) {
    return false;
  }
  for (size_t index = 0; index < captureStopJobs_->count; ++index) {
    const size_t at = (captureStopJobs_->head + index) % CaptureStopJobs::kCapacity;
    if (captureStopJobs_->entries[at].slot == slot) {
      return true;
    }
  }
  return false;
}

TRACK_COLD_MEM bool Track::hasAcceptedInteractionForSlot(uint8_t slot) const {
  const PendingInteractionLatch* latch = interactionLatch();
  return latch != nullptr && latch->isSet() && latch->slot == slot;
}

TRACK_COLD_MEM bool Track::noLiveOwnerForSlot(uint8_t slot) const {
  // S3.2: the composite is a list of named owner kinds, so each term is testable on its own and a
  // new owner kind is added here by name rather than as another inline clause.
  if (hasLiveCaptureStopJobForSlot(slot)) return false;
  if (hasAcceptedInteractionForSlot(slot)) return false;
  // Task 3.6 (DEC-108 item 4): a **retained source revision** keeps this slot owned, by name. Its descriptors and
  // bodies are still needed to reconstruct the revision a window serves (or one still being prepared), so the slot
  // may not be released, cleared or replaced underneath it. The release side is the one predicate,
  // `tryReleaseRetainedPlaybackSource`.
  if (hasRetainedPlaybackSourceForSlot(slot)) return false;
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slot);
  if (runtime != nullptr) {
    if (runtime->ownsCoverageSource() || runtime->ownsLaunchPreparation()) return false;
    if (runtime->ownsLiveFinalization()) return false;
    if (runtime->ownsInFlightPreparedHandover()) return false;
  }
  // Not a term yet, and recorded rather than assumed: a job-owned undo root that outlives its ring
  // entry. The ring scan covers the job-owned roots while the entry lives; the protected undo
  // publication is track-level today and has no per-slot identity to test. S5.2a gives cold undo its
  // per-slot descriptor, which is where this term becomes expressible. Likewise the
  // deferred-reconstruction term arrives with S5.2e.
  return true;
}

TRACK_COLD_MEM bool Track::tryReleaseCanonicalCaptureReservation(uint8_t slot) {
  if (!noLiveOwnerForSlot(slot)) return false;
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slot);
  if (runtime == nullptr) return false;
  return runtime->releaseCanonicalCaptureReservation();
}

TRACK_COLD_MEM bool Track::tryReleasePlaybackMergedMidiEventsMemory() {
  if (isRecording() || isOverdubbing() || hasPendingDeferredOverdubStop() || getState() == TRACK_ARMED) {
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
    if (runtime == nullptr) {
      continue;
    }

    // S2b.4 (invariant 34): give this slot's canonical-copy reservation back once no live owner
    // remains. Placed before the window checks so a slot with no merged window still releases, and
    // guarded per slot -- a track-level "capture active" flag would release underneath a live job on
    // another slot. `releaseCanonicalCaptureReservation` refuses while the store is populated, so a
    // mid-copy slot keeps its credits.
    if (tryReleaseCanonicalCaptureReservation(slot)) {
      reclaimed = true;
    }

    if (runtime->mergedMidiEvents.empty()) {
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
    // The published pair cannot outlive the working window it was copied from:
    // reclaiming the window retires the pair so the tick cannot consume a pair whose
    // ledger/window cache has been dropped.
    runtime->clearPreparedPlaybackPair();
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
  CommittedMidiScratch::invalidate(committedMidiScratch_, committedMidiScratchRevision_,
                                   committedMidiScratchPublishedIdentity_);
  return true;
}

// ---------------------------------------------------------------------------
// DEC-108 item 1 / `ExecutionBounds.md` rows 27-29: the window-build owner
// (`Track::stepPlaybackViewMaintenance`).
//
// This slice lands the owner, its declared service contract and its proof surface. **No behaviour changes
// behind it**: nothing in production requests a window yet (the source resolution and the consumer-facing
// publication are task 3.2), and the owner never publishes, hands off or allocates. What it must be able to
// do before that request arrives is bounded, off-clock work: one declared grain per admitted turn, with the
// admission arithmetic fixed in advance.
// ---------------------------------------------------------------------------

// DEC-108 §13b / task 3.10 (the review's step 4): **the envelope is declared, and its activation is gated on the
// callers' clock wiring -- a blocker this step found by trying it.** The envelope itself is now supported (see
// `playbackViewCoverageContract`): the declared advance is the measured scheduling bound, the denial allowance is the
// H-track rotation, and the lead is judged on the window's **derived** cost, so the shape that used to be admitted on
// the placeholder counts is no longer admitted on theirs. What is *not* yet true is that every caller carries a
// **qualified clock stamp**: enforced admission fails closed on an unqualified one (increment 22's own rule, DEC-108
// §13c), so flipping this on refuses every legacy preparation that passes no stamp -- including every fixture in the
// suite that prepares a window without one. Measured: with the switch on, twelve fixtures that prepare an unqualified
// window are refused by name (`TrackInterferenceBudgetExceeded`), which is *correct behaviour* for an unqualified
// demand and *not* a supported production state, because the calling owners (the MIDI edge, the switch preparation and
// the preview lane) do not pass stamps yet. Activation therefore needs the clock-owner wiring, not a better constant:
// it is recorded as the step-4 remainder, and the enforced path stays exercised by the fixtures that turn it on.
// Production requests no window today, so neither state has a production-observable effect yet.
#if defined(__IMXRT1062__)
DMAMEM static bool sInterferenceBudgetEnforced = false;
#else
static bool sInterferenceBudgetEnforced = false;
#endif

/// The two-bar basis window this contract was written around (kept beside the derivation it names).
constexpr size_t kPlaybackViewContractBasisEvents = 144;

/// DEC-108 §13b / task 3.10 (the review's step 1, *"bound the work of each request"*): **the service requirement of
/// the work actually being admitted**, as the contract's own **single source of truth**. The window's own event count
/// is the whole input: fills are `ceil(events / kPlaybackViewFillQuantum)`, order grains are the builder's own
/// worst-case entry count (`RuntimePlaybackOrderBuilder::requiredGrains`, i.e.
/// `events x (1 + 2 x ceil(log2 events))` divided by the order quantum), plus one promotion grain for turning a
/// completed build into the ready view. Using the builder's helper means the admission, the retained work record and
/// the builder cannot disagree about what a window costs -- which is the defect this replaces: the declared constants
/// assumed a *linear* order pass and were therefore 17x too small for their own basis window.
///
/// The counts saturate rather than wrap (an absurd window refuses, never admits). An empty source derives **no order
/// work** (the helper returns 0 for an empty entry list) and **one fill grain plus the one promotion grain** -- the
/// fill grain is what establishes the emptiness, measured by the source-shape fixture after an earlier version of this
/// comment claimed an empty window derived no fill work at all.
TRACK_COLD_MEM void CoverageServiceContract::deriveGrainsFrom(size_t windowEventCount) {
  // **At least one fill grain**, because a window is filled by that grain even when it turns out to hold nothing.
  // Measured, not assumed: the source-shape fixture caught this -- an *empty* window spent one fill grain while
  // `ceil(0 / quantum)` is 0, so the bound under-cut the very work it exists to bound (task 3.10, review step 1).
  const size_t fills = windowEventCount == 0
                           ? 1
                           : (windowEventCount + Track::kPlaybackViewFillQuantum - 1) / Track::kPlaybackViewFillQuantum;
  const size_t orders =
      RuntimePlaybackOrderBuilder::requiredGrains(windowEventCount, Track::kPlaybackViewOrderQuantum);
  requiredFillGrains = fills > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(fills);
  requiredOrderGrains = orders > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(orders);
  // A completed build has to become the ready view, which is its own grain.
  requiredPromotionGrains = 1;
}

/// DEC-108 §13b / task 3.10 (**the review's step 4**): **the declared service envelope** -- the terms the admission
/// above is allowed to promise, stated once, with their derivation and with the policy that governs a miss.
///
/// The review's formula is `Delta-max x H x bounded per-track work`, where `bounded per-track work` is
/// `grains x (denied + 1)` read through the contract's own `canCompleteWithinLead`. Each term is now a *declared*
/// quantity rather than a placeholder:
///
/// * **`Delta-max` (the permitted phase advance between two of this owner's opportunities) = 64 ticks**, set from the
///   device measurement of the **scheduling** advance -- the gap across a turn whose predecessor was *eligible*
///   (schema-2's `eligiblePredecessorMax`), not an idle interval: internal clock p50 0 / p90 9 / p99 15 / **max 17**,
///   and at the fastest declared tempo (300 BPM, where 64 ticks is 66.7 ms) p50 8 / p90 16 / p99 **48** / **max 48**.
///   64 is at or above both maxima with a stated margin (1.33x the 300-BPM maximum, 3.8x the internal one). The first
///   declaration of 16 (2x the p50) was **falsified by that measurement** -- two internal rows and six 300-BPM rows
///   exceeded it -- which is why the value sits on the measured maximum. It bounds the *scheduling* advance: the
///   seconds-scale starvation episodes remain the overrun policy's below, because a bound on one turn cannot hold a
///   run of *lost* turns.
///   on the seconds-scale **starvation** episodes (2 138 / 4 355 / 8 520 ticks) that the same runs contain. Those are
///   *lost opportunities*, not one turn's advance, so no per-turn constant can hold them -- which is why the field
///   keeps its scheduling role (evidence/2026-10-03-s310n-... corrects exactly this conflation) and why the overrun
///   policy below exists. And it is a **declared permitted value, not a measured WCET**: it is the figure the firmware
///   claims, it is falsifiable, and the device leg can tighten it.
/// * **`H = 8` across tracks** (`maximumConsecutiveDeniedTurns = Config::NUM_TRACKS - 1`, the round-robin rotation:
///   every eligible track is served within H lane turns), and **`K` is charged as sibling *work*, never as a slot
///   multiplier** -- the same-track siblings' grains are added to the candidate through `additionalGrains`, so a track
///   with more owing slots pays more work, not a bigger constant.
/// * **The handoff margin (`leadTicks`) = one bar**, the demonstrated margin the spec's service contract requires: the
///   admission refuses below `leadTicks` regardless of the work, so a window is never promised with less than a bar of
///   room to publish.
/// * **The overrun policy -- what happens when the permitted gap is exceeded.** A window whose traversal deadline
///   passes is the **lag rule's** (DEC-108 §13a/§13e, task 3.6b): the previous revision serves that traversal once
///   (`one_pass_late`), the miss is *reported*, a **second** consecutive miss latches the named
///   `deadlineContractFaultLatched` fault, and the deadline is **never silently re-granted** and never compensated by
///   an inline rebuild. Containment after a miss is therefore *not* presented as passing normal-operation continuity:
///   the envelope says what a turn can advance, and the policy says what a miss is called.
///
/// The declared basis counts below stay the "basis window" declaration (they are the shape the derivation was written
/// against); they are no longer what the lead is judged on -- `requestPlaybackViewMaintenance` derives the *actual*
/// window's cost and judges the lead with it.
TRACK_COLD_MEM CoverageServiceContract Track::playbackViewCoverageContract() {
  CoverageServiceContract contract;
  contract.maximumPhaseAdvancePerTurn = kPlaybackViewPermittedAdvancePerTurn;
  contract.maximumConsecutiveDeniedTurns = Config::NUM_TRACKS - 1;
  contract.requiredFillGrains = 3;
  contract.requiredOrderGrains = 3;
  contract.requiredPromotionGrains = 1;
  contract.leadTicks = Config::TICKS_PER_BAR;
  return contract;
}

TRACK_COLD_MEM Track::PlaybackViewRequestResult Track::requestPlaybackViewMaintenance(
    uint8_t slotIndex, const MidiEvent* source, size_t sourceCount,
    const PlaybackViewSourceIdentity& sourceIdentity, const PlaybackCoverageInterval& coverage,
    const EmissionCursor& cursorAnchor, const LedgerBoundarySummary& ledgerBoundary,
    uint32_t currentPhase, uint32_t handoffPhase, PlaybackDeadlineClock clock) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return PlaybackViewRequestResult::NoRuntime;
  }
  // Never allocate here (S4b.6's rule): the runtime's owner is the provisioning path, and an absent runtime
  // owes nothing. The obligation is the runtime's own unfinished build, so a refused request changes nothing.
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return PlaybackViewRequestResult::NoRuntime;
  }
  if (runtime->viewBuild.active() || runtime->ownsCoverageSource() || runtime->completedPromisePendingJudgement) {
    // DEC-108 §13(c) / task 3.10 (increment 24): the slot already owes a build, so this demand **coalesces into that
    // obligation** instead of starting a second one -- and it is retained rather than dropped, because the build in
    // flight may be serving a different interval than the one this caller asked for.
    retainRefusedPlaybackViewRequest(slotIndex, runtime, sourceCount, sourceIdentity, coverage, currentPhase,
                                     handoffPhase, clock, PlaybackViewPendingReason::BuildInFlight);
    return PlaybackViewRequestResult::BuildInFlight;
  }
  {
    // One **prepared successor** at a time: a completed successor already waiting has to be adopted (or its
    // build role freed) before another can be prepared, so a third window cannot silently displace it. A window
    // merely *being served* is not a bound -- preparing its replacement is exactly the successor lane.
    const RuntimePlaybackViewSet* set = runtime->viewSetIfPrewarmed();
    if (set != nullptr && set->lifecycle.ready < RuntimePlaybackViewConfig::kViewCount &&
        set->views[set->lifecycle.ready].complete) {
      // DEC-108 §13(c) / task 3.10 (increment 24): a "not now" refusal retains the demand -- the successor has to
      // be adopted or released first, and the demand the caller asked for is still owed afterwards.
      retainRefusedPlaybackViewRequest(slotIndex, runtime, sourceCount, sourceIdentity, coverage, currentPhase,
                                       handoffPhase, clock, PlaybackViewPendingReason::SuccessorAlreadyPrepared);
      return PlaybackViewRequestResult::SuccessorAlreadyPrepared;
    }
  }
  CoverageServiceContract contract = playbackViewCoverageContract();
  if (!contract.isDeclared()) {
    return PlaybackViewRequestResult::ContractUndeclared;
  }
  // **Bound the work before admitting it** (the review's step 1, task 3.10). A window can only be prepared from what
  // its own destination view can retain, so the entry count is checked against the window's storage capacity first --
  // `RuntimePlaybackViewConfig::kWindowAdmissionLimit`, which *is* `kEventsPerView` and is `static_assert`ed as the
  // admission limit. A larger gather is **refused by name rather than truncated**: accepted content is never silently
  // dropped (spec *Accepted content is never truncated*).
  if (sourceCount > RuntimePlaybackViewConfig::kWindowAdmissionLimit) {
    return PlaybackViewRequestResult::WindowExceedsRetainedCapacity;
  }
  // The cost of that bounded work is **derived from the entries the fill must inspect**, by the contract's own
  // `deriveGrainsFrom` -- one helper for the admission, the retained record and the fixtures, so none of them can
  // disagree about what this window costs.
  //
  // **DEC-108 §13b / task 3.10, the review's step 4: the derived cost is now what the lead is judged against.** Until
  // this step the lead used the *declared* basis counts (3/3/1) while the retained record used the derived ones, which
  // is exactly the "declared versus derived accounting" split the review ordered fixed last: a 63-entry window costs
  // 20 grains, not 7, and admitting it on the smaller figure is what "admits work it cannot complete" means. The
  // derivation is therefore unconditional -- it is the *accounting*, not a gate -- and what remains gated is only the
  // sibling charge below (`sInterferenceBudgetEnforced`), so the envelope change and the interference rule stay
  // separately falsifiable.
  CoverageServiceContract derived = contract;
  derived.deriveGrainsFrom(sourceCount);
  contract = derived;
  PlaybackViewAdmittedWork admittedWork;
  admittedWork.sourceEntries = static_cast<uint32_t>(sourceCount);
  admittedWork.retainedEventBound = static_cast<uint32_t>(RuntimePlaybackViewConfig::kWindowAdmissionLimit);
  admittedWork.fillGrains = derived.requiredFillGrains;
  admittedWork.orderGrains = derived.requiredOrderGrains;
  admittedWork.promotionGrains = derived.requiredPromotionGrains;
  // Retain an absolute deadline. Missing clocks and addition overflow remain unqualified,
  // and can never pass enforced admission. Phase only determines the initial lead.
  admittedWork.handoffPhase = handoffPhase;
  admittedWork.admittedLoopLength = coverage.loopLengthTicks;
  admittedWork.admittedIntervalLength = coverage.lengthTicks;
  admittedWork.admittedTick = clock.tick;
  const uint32_t lead = handoffLeadTicks(currentPhase, handoffPhase, coverage.loopLengthTicks);
  if (clock.epoch != 0 && lead != 0 && clock.tick <= UINT64_MAX - lead) {
    admittedWork.deadlineTick = clock.tick + lead;
    admittedWork.clockEpoch = clock.epoch;
  }
  // **The interference budget is computed and proved, but deliberately not enforced yet** -- the review's own ordering
  // puts that after the preventing paths are bounded and `Delta-max` is chosen. Charging a sibling's outstanding work
  // against a new window's lead only becomes usable once the per-turn advance inside that charge is the **measured**
  // one: with the declared one bar per turn, the charge for a second window on a track exceeds most loops, so enforcing
  // it now would refuse shapes the current constants admit -- the capacity reduction the review says the envelope work
  // must not start with. `playbackViewAdmissionKeepsAdmittedDeadlines` *is* the bound; step 4's formula
  // (`Delta-max x H x bounded per-track work`) is what turns it on.
  if (sInterferenceBudgetEnforced &&
      (admittedWork.clockEpoch == 0 ||
       !playbackViewAdmissionKeepsAdmittedDeadlines(contract, currentPhase, coverage.loopLengthTicks,
                                                   static_cast<uint16_t>(admittedWork.requiredGrains()), clock))) {
    // DEC-108 §13(c) / task 3.10 (increment 24): **a refusal is a promise.** The demand is retained with its own
    // absolute deadline, so when the interference clears the owner can serve it with no further gesture -- deferred,
    // never dropped, and no existing window's deadline is traded away to make room for it.
    retainRefusedPlaybackViewRequest(slotIndex, runtime, sourceCount, sourceIdentity, coverage, currentPhase,
                                     handoffPhase, clock, PlaybackViewPendingReason::InterferenceBudget);
    return PlaybackViewRequestResult::TrackInterferenceBudgetExceeded;
  }
  // Named separately from the primitive's own answer: the declared gate is what refuses *here*, so the
  // acceptance surface can tell "the margin was never there" from "the build was refused underneath it". The
  // candidate's own lead is charged the siblings' outstanding work through the contract's own `additionalGrains`
  // term -- the same figure the budget above reads the other way round, and only while that budget is enforced.
  if (!contract.canCompleteBeforeHandoff(
          currentPhase, handoffPhase, coverage.loopLengthTicks,
          sInterferenceBudgetEnforced ? playbackViewOutstandingGrains(slotIndex) : 0)) {
    // DEC-108 §13(c) / task 3.10 (increment 24): the declared lead cannot cover this demand **now**, which is a
    // deferred obligation rather than a lost one -- the same retention the budget refusal above performs, so both
    // "not now" refusals leave the same promise behind.
    retainRefusedPlaybackViewRequest(slotIndex, runtime, sourceCount, sourceIdentity, coverage, currentPhase,
                                     handoffPhase, clock, PlaybackViewPendingReason::CannotCompleteBeforeHandoff);
    return PlaybackViewRequestResult::CannotCompleteBeforeHandoff;
  }
  if (!runtime->beginCoverageBuild(contract, currentPhase, handoffPhase, source, sourceCount, sourceIdentity,
                                   coverage, cursorAnchor, ledgerBoundary)) {
    return PlaybackViewRequestResult::AdmissionRefused;
  }
  // The admitted request's **bounded work**, recorded where the build begins -- and only once the build really began,
  // so a refused request records nothing (the record is read only while the build is active). A **new** promise
  // supersedes any retained completion (increment 28), so the retention is cleared here: only this promise's own
  // completion can set it, and a successor can never be judged as its predecessor.
  runtime->viewBuildWork = admittedWork;
  runtime->completedPromisePendingJudgement = false;
  runtime->admittedPromiseInvalidated = false;
  runtime->countedMissPromiseTick = admittedWork.admittedTick;
  runtime->countedMissedTraversals = 0;
  if (!runtime->lagState.coversInterval(handoffPhase, coverage.lengthTicks)) {
    runtime->lagState.resetEpisode(handoffPhase, coverage.lengthTicks);
  }
  // Task 3.11: this build's **operation identity**, recorded where the build begins: a completion that arrives after
  // the generation has moved is late, and the publication guard refuses it.
  runtime->viewBuildOperationGeneration = playbackGeneration;
  // DEC-108 §13(c) / task 3.10 (increment 24): **the demand is discharged.** The work it asked for is now owed by a
  // live build -- the admission's own bounded record just above -- so keeping a pending demand beside it would count
  // the same obligation twice, and a later reader would see a deadline for work that is already being done. A
  // discharged demand leaves no record at all (a later refusal starts a fresh one).
  // Admission owns request dedup state; it does not erase same-interval fault history.
  // Completion is judged later with a fresh ClockManager stamp before publication.
  runtime->clearViewPendingRequest();
  return PlaybackViewRequestResult::Admitted;
}

/// DEC-108 §13(c) / task 3.10 (increment 24): the admission path's own retention call. It is the **one** shape for
/// every "not now" refusal, so the deadline and the cost a retained demand carries cannot drift from the ones an
/// admitted window carries: the cost comes from the contract's own `deriveGrainsFrom` over the entries the caller
/// gathered, and the deadline from the same absolute-stamp arithmetic the admission uses below. A missing stamp or a
/// timestamp overflow leaves the demand **unqualified** (no position claimed) rather than promising one it cannot
/// compute -- the same fail-closed direction enforced admission takes.
TRACK_COLD_MEM void Track::retainRefusedPlaybackViewRequest(
    uint8_t slotIndex, LoopPlaybackRuntime* runtime, size_t sourceCount,
    const PlaybackViewSourceIdentity& sourceIdentity, const PlaybackCoverageInterval& coverage,
    uint32_t currentPhase, uint32_t handoffPhase, PlaybackDeadlineClock clock,
    PlaybackViewPendingReason reason) {
  if (runtime == nullptr || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return;
  }
  // The **declared** contract's derivation, deliberately not the enforced copy: the cost of the work is a property of
  // the work, not of the gate that refused it, so a demand retained while enforcement is off still carries the figure
  // it will be charged when enforcement turns on.
  CoverageServiceContract cost = playbackViewCoverageContract();
  cost.deriveGrainsFrom(sourceCount);
  PlaybackViewAdmittedWork work;
  work.sourceEntries = static_cast<uint32_t>(sourceCount);
  work.retainedEventBound = static_cast<uint32_t>(RuntimePlaybackViewConfig::kWindowAdmissionLimit);
  work.fillGrains = cost.requiredFillGrains;
  work.orderGrains = cost.requiredOrderGrains;
  work.promotionGrains = cost.requiredPromotionGrains;
  work.handoffPhase = handoffPhase;
  work.admittedLoopLength = coverage.loopLengthTicks;
  work.admittedIntervalLength = coverage.lengthTicks;
  work.admittedTick = clock.tick;
  uint64_t deadlineTick = 0;
  uint32_t clockEpoch = 0;
  const uint32_t lead = handoffLeadTicks(currentPhase, handoffPhase, coverage.loopLengthTicks);
  if (clock.epoch != 0 && lead != 0 && clock.tick <= UINT64_MAX - lead) {
    deadlineTick = clock.tick + lead;
    clockEpoch = clock.epoch;
    work.deadlineTick = deadlineTick;
    work.clockEpoch = clockEpoch;
  }
  runtime->retainViewPendingRequest(sourceIdentity, coverage, currentPhase, playbackGeneration, work, deadlineTick,
                                    clockEpoch, coverage.loopLengthTicks, reason);
}

/// DEC-108 §13(c) / task 3.10 (increment 24): the slot's retained demand, **generation-qualified** (DEC-108 §13(d)).
/// A record retained under an earlier operation generation is not this slot's demand and is not revived -- a reset
/// does not erase a pending obligation (DEC-108 §13(a)), so the record is left where it is rather than cleared, but
/// no reader may bind to it and a demand made under the new generation starts a fresh record. That is what makes
/// "returning to a slot never revives an earlier operation" true here rather than aspirational.
TRACK_COLD_MEM const PlaybackViewPendingRequest* Track::pendingViewRequest(uint8_t slotIndex) const {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return nullptr;
  }
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  const PlaybackViewPendingRequest* pending = runtime != nullptr ? runtime->pendingViewRequest() : nullptr;
  if (pending == nullptr || pending->operationGeneration != playbackGeneration) {
    return nullptr;
  }
  return pending;
}

/// DEC-108 §13(c) / task 3.10 (increment 24): whether the demand's retained **position** still applies. Only a
/// reading: an epoch change or a geometry change voids the promise's position, never the obligation, so a caller
/// (the re-attempt that is still owed) can tell "still promised for this tick" from "owed, but no longer positioned".
TRACK_COLD_MEM PlaybackViewPendingReason Track::pendingViewRequestInvalidation(uint8_t slotIndex,
                                                                             PlaybackDeadlineClock clock) const {
  const PlaybackViewPendingRequest* pending = pendingViewRequest(slotIndex);
  if (pending == nullptr) {
    return PlaybackViewPendingReason::None;
  }
  return pending->deadlineInvalidation(loopForSlot(slotIndex).loopLengthTicks, clock.epoch);
}

/// DEC-108 §13b / task 3.10 (the review's step 2): **the interference budget's enforcement switch.** It is a
/// **runtime** flag rather than a compile-time gate, precisely so the *enforced* admission path can be exercised by a
/// fixture with a controlled timing contract -- which is what the review asks for -- while production keeps it off until
/// the timing bound is supported. Placed like the service-gap sampler's state (`DMAMEM` on the target, plain on the
/// host) so a scalar never moves the approved RAM1 line.
TRACK_COLD_MEM uint16_t Track::playbackViewOutstandingGrains(uint8_t excludingSlot) const {
  uint32_t total = 0;
  for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
    if (slot == excludingSlot) {
      continue;
    }
    const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slot);
    const PlaybackViewAdmittedWork* work = runtime != nullptr ? runtime->activeViewBuildWork() : nullptr;
    if (work != nullptr) {
      total += work->requiredGrains();
    } else if (runtime && runtime->ownsCoverageSource()) {
      CoverageServiceContract preparation = playbackViewCoverageContract();
      preparation.deriveSnapshotGrainsFrom(LoopSnapshot::EventWindowCursor::kMaximumReadUnits,
          RuntimePlaybackViewConfig::kWindowAdmissionLimit, kPlaybackViewFillQuantum, kPlaybackViewOrderQuantum);
      total += preparation.requiredGrains() + 2u *
          ((LoopSnapshot::EventWindowCursor::kMaximumReadUnits + kPlaybackViewFillQuantum - 1) / kPlaybackViewFillQuantum) + 2u;
    }
  }
  // Saturate rather than wrap: an absurd budget refuses, never admits.
  return total > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(total);
}

TRACK_COLD_MEM bool Track::playbackViewAdmissionKeepsAdmittedDeadlines(
    const CoverageServiceContract& contract, uint32_t currentPhase, uint32_t loopLengthTicks,
    uint16_t candidateGrains, PlaybackDeadlineClock clock) const {
  if (clock.epoch == 0 || loopLengthTicks == 0 || currentPhase >= loopLengthTicks) return false;
  for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
    const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slot);
    const PlaybackViewAdmittedWork* work = runtime != nullptr ? runtime->activeViewBuildWork() : nullptr;
    if (work == nullptr) {
      continue;
    }
    // The window's own grains come from its **retained record** (not from the candidate's contract), while **all** the
    // other outstanding work -- every sibling, and the candidate itself -- is charged against that window's own lead.
    // This is the deliberate EDF-free rule the review asks for first: a sibling whose deadline is earlier may
    // legitimately take the lane's turns, so charging all of it is the promise-keeping bound. The arithmetic is the
    // contract's own `canCompleteBeforeHandoff`, reached through a per-slot copy, so the budget and the admission
    // cannot disagree about what a grain costs in turns.
    CoverageServiceContract forSlot = contract;
    forSlot.requiredFillGrains = static_cast<uint16_t>(work->fillGrains);
    forSlot.requiredOrderGrains = static_cast<uint16_t>(work->orderGrains);
    forSlot.requiredPromotionGrains = static_cast<uint16_t>(work->promotionGrains);
    const uint32_t others = static_cast<uint32_t>(playbackViewOutstandingGrains(slot)) + candidateGrains;
    const uint16_t charged = others > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(others);
    // Compare absolute time even if no check occurred for many complete loop traversals.
    // A reset/rebase, backwards clock or changed geometry invalidates the old promise.
    if (work->clockEpoch != clock.epoch || work->admittedLoopLength == 0 ||
        loopForSlot(slot).loopLengthTicks != work->admittedLoopLength ||
        clock.tick < work->admittedTick || clock.tick >= work->deadlineTick) {
      return false;
    }
    if (!forSlot.canCompleteWithinLead(work->deadlineTick - clock.tick, charged)) {
      return false;
    }
  }
  return true;
}

TRACK_COLD_MEM Track::PlaybackViewRequestResult Track::requestCommittedPlaybackWindow(
    uint8_t slotIndex, const PlaybackCoverageInterval& coverage,
    uint32_t currentPhase, PlaybackDeadlineClock clock) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) return PlaybackViewRequestResult::NoRuntime;
  auto* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (!runtime) return PlaybackViewRequestResult::NoRuntime;
  const auto& loop = loopForSlot(slotIndex);
  if (!coverage.isValid() || coverage.loopLengthTicks != loop.loopLengthTicks ||
      coverage.lengthTicks > LoopSnapshot::EventWindowCursor::kMaximumWindowTicks ||
      coverage.startPhase > coverage.loopLengthTicks - coverage.lengthTicks ||
      currentPhase >= coverage.loopLengthTicks) return PlaybackViewRequestResult::NotServable;
  const PlaybackViewSourceIdentity identity{loop.playbackRevision, playbackGeneration,
                                            loop.publishedSnapshotIdentity()};
  retainRefusedPlaybackViewRequest(slotIndex, runtime, RuntimePlaybackViewConfig::kWindowAdmissionLimit,
      identity, coverage, currentPhase, coverage.startPhase, clock,
      PlaybackViewPendingReason::AwaitingSourceQualification);
  runtime->pendingViewRequest_.sourceKind = PlaybackViewPendingRequest::SourceKind::CommittedSnapshot;
  return PlaybackViewRequestResult::DeferredSourceQualification;
}

TRACK_COLD_MEM Track::PlaybackViewRequestResult Track::requestCommittedPlaybackWindow(
    uint8_t slotIndex, const PlaybackCoverageInterval& coverage, PlaybackDeadlineClock clock) {
  if (slotIndex != getActiveLoopIndex() || clock.epoch == 0 || isJamPlaybackActive() || isJamming() ||
      getQueuedStartTick() != 0 || coverage.loopLengthTicks == 0) return PlaybackViewRequestResult::NotServable;
  const uint32_t effectiveTick = getEffectivePlaybackTick(static_cast<uint32_t>(clock.tick));
  const uint32_t phase = IntervalProjection::tickPhaseInProjectionCycle(
      effectiveTick, getProjectionCycleStartTick(), coverage.loopLengthTicks);
  return requestCommittedPlaybackWindow(slotIndex, coverage, phase, clock);
}

TRACK_COLD_MEM void Track::cancelPlaybackLaunchForSlot(uint8_t slotIndex, bool includeBound) {
  auto* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (!runtime || runtime->launchPreparation == LoopPlaybackRuntime::LaunchPreparation::None ||
      ((runtime->launchPreparation == LoopPlaybackRuntime::LaunchPreparation::Bound ||
        runtime->launchPreparation == LoopPlaybackRuntime::LaunchPreparation::FallbackBound) && !includeBound)) return;
  auto* set = runtime->viewSetIfPrewarmed();
  if (set && runtime->launchPreparation == LoopPlaybackRuntime::LaunchPreparation::Ready &&
      set->lifecycle.ready < set->views.size()) set->views[set->lifecycle.ready].resetMetadata();
  if (set && runtime->launchPreparation == LoopPlaybackRuntime::LaunchPreparation::Bound &&
      set->lifecycle.current < set->views.size()) set->views[set->lifecycle.current].resetMetadata();
  if (runtime->pendingViewRequest_.sourceKind == PlaybackViewPendingRequest::SourceKind::CommittedLaunch)
    runtime->clearViewPendingRequest();
  if (runtime->frozenViewRequest.sourceKind == PlaybackViewPendingRequest::SourceKind::CommittedLaunch)
    runtime->frozenViewRequest = {};
  // Completed worker may point at a now-published view. Only discard an active
  // launch worker; publication has already transferred a completed destination.
  if (runtime->launchPreparation == LoopPlaybackRuntime::LaunchPreparation::Requested ||
      runtime->launchPreparation == LoopPlaybackRuntime::LaunchPreparation::Building)
    runtime->discardCoverageBuild();
  else if (runtime->ownsCoverageSource()) {
    runtime->coverageSource.beginRelease();
    runtime->coveragePreparation = LoopPlaybackRuntime::CoveragePreparation::Retiring;
  }
  runtime->launchPreparation = LoopPlaybackRuntime::LaunchPreparation::None;
}

TRACK_COLD_MEM bool Track::preparePlaybackLaunchForSlot(uint8_t slotIndex) {
  // Every exit below used to return with no row at all, which is why five switch runs could not tell "nothing was
  // owed" from "the source was refused" (F-16). Each one now names itself through `reportPlaybackLaunchGateRow`,
  // **once per state change** of that target, and the flags that decided it travel with the name. Reporting observes:
  // no guard, no order and no fallback below is changed by it.
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || slotIndex == activeLoopIndex || !loopsAllocated()) {
    reportPlaybackLaunchGateRow(slotIndex, "launch_slot_not_eligible");
    return false;
  }
  auto* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (!runtime) {
    reportPlaybackLaunchGateRow(slotIndex, "launch_no_runtime");
    return false;
  }
  auto& loop = loopForSlot(slotIndex);
  if (!loop.hasCommittedPasses() || !loop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks)) {
    reportPlaybackLaunchGateRow(slotIndex, "launch_not_demoted_content");
    return false;
  }
  // A legacy pass-only restore has no root producer. Request only this launch
  // target through the existing on-demand publication owner, never bulk boot
  // hydration, and never restart a source job that already owns its progress.
  // The two guards are read separately so the row can say *why* nothing was requested: an already-current root
  // (`root=1`), a publish already in flight (`pend=1`), or a request this turn that the owner refused (`req=2`).
  const bool sourceCurrent = loop.publishedSnapshotIsCurrent();
  const bool sourcePublishPending = loop.hasPendingSnapshotPublish();
  const bool sourceRequestNeeded = !sourceCurrent && !sourcePublishPending;
  const bool sourceRequested = sourceRequestNeeded && loop.requestCommittedPassesRepublish();
  if (runtime->launchPreparation == LoopPlaybackRuntime::LaunchPreparation::Fallback) {
    if (isPlaybackMergedMidiEventsReadyForSlot(slotIndex)) return false;
    ensurePlaybackMergedEventsForSlot(slotIndex);
    return true;
  }
  if (runtime->pendingViewRequest_.sourceKind == PlaybackViewPendingRequest::SourceKind::CommittedLaunch &&
      runtime->pendingViewRequest_.reason == PlaybackViewPendingReason::WindowExceedsRetainedCapacity) {
    if (runtime->ownsCoverageSource()) {
      reportPlaybackLaunchGateRow(slotIndex, "launch_dense_blocked_by_source", sourceRequestNeeded, sourceRequested);
      return false;
    }
    reportPlaybackLaunchGateRow(slotIndex, "launch_fallback_dense", sourceRequestNeeded, sourceRequested);
    cancelPlaybackLaunchForSlot(slotIndex);
    runtime->launchPreparation = LoopPlaybackRuntime::LaunchPreparation::Fallback;
    reportPlaybackViewMaintenanceRow(slotIndex, loop, "launch_legacy_dense");
    ensurePlaybackMergedEventsForSlot(slotIndex);
    return true; // Legacy dense material is never truncated or silently made unlaunchable.
  }
  if (!runtime->prewarmRollingViews()) {
    reportPlaybackLaunchGateRow(slotIndex, "launch_prewarm_refused", sourceRequestNeeded, sourceRequested);
    return false;
  }
  if (runtime->viewBuild.active() && !runtime->viewBuild.coverageOwned()) {
    reportPlaybackLaunchGateRow(slotIndex, "launch_build_owned_by_capture", sourceRequestNeeded, sourceRequested);
    return false;
  }
  const auto matches = [&](const PlaybackViewSourceIdentity& source, const PlaybackCoverageInterval& coverage) {
    return source.playbackRevision == loop.playbackRevision && source.sourceGeneration == playbackGeneration &&
        source.origin == PlaybackWindowOrigin::Committed && coverage.startPhase == 0 &&
        coverage.lengthTicks == 2 * Config::TICKS_PER_BAR && coverage.loopLengthTicks == loop.loopLengthTicks;
  };
  if (runtime->launchPreparation == LoopPlaybackRuntime::LaunchPreparation::Ready && playbackLaunchReadyForSlot(slotIndex)) {
    reportPlaybackLaunchGateRow(slotIndex, "launch_ready", sourceRequestNeeded, sourceRequested);
    return false;
  }
  if (runtime->launchPreparation == LoopPlaybackRuntime::LaunchPreparation::Ready && runtime->ownsCoverageSource()) {
    reportPlaybackLaunchGateRow(slotIndex, "launch_ready_owns_source", sourceRequestNeeded, sourceRequested);
    return false;
  }
  if (runtime->launchPreparation == LoopPlaybackRuntime::LaunchPreparation::Building) {
    const auto* set = runtime->viewSetIfPrewarmed();
    if (set->lifecycle.build < set->views.size() && matches(set->views[set->lifecycle.build].source,
        set->views[set->lifecycle.build].coverage)) {
      reportPlaybackLaunchGateRow(slotIndex, "launch_building_match", sourceRequestNeeded, sourceRequested);
      return false;
    }
  }
  if (runtime->launchPreparation == LoopPlaybackRuntime::LaunchPreparation::Requested) {
    const auto& request = runtime->frozenViewRequest.active ? runtime->frozenViewRequest : runtime->pendingViewRequest_;
    if (request.active && matches(request.source, request.coverage)) {
      reportPlaybackLaunchGateRow(slotIndex, "launch_requested_match", sourceRequestNeeded, sourceRequested);
      return sourceRequested;
    }
  }
  if (runtime->launchPreparation != LoopPlaybackRuntime::LaunchPreparation::None)
    cancelPlaybackLaunchForSlot(slotIndex, true);
  // Do not overwrite an independent positioned demand or another prepared owner.
  if (runtime->pendingViewRequest_.active || (!runtime->ownsCoverageSource() && !runtime->coverageRoleAvailable())) {
    reportPlaybackLaunchGateRow(slotIndex, runtime->pendingViewRequest_.active ? "launch_pending_demand"
                                                                              : "launch_role_unavailable",
                                sourceRequestNeeded, sourceRequested);
    return false;
  }
  PlaybackViewPendingRequest request{};
  request.active = true;
  request.sourceKind = PlaybackViewPendingRequest::SourceKind::CommittedLaunch;
  request.source = {loop.playbackRevision, playbackGeneration, loop.publishedSnapshotIdentity()};
  request.coverage = {0, 2 * Config::TICKS_PER_BAR, loop.loopLengthTicks};
  request.operationGeneration = playbackGeneration;
  request.loopLengthTicks = loop.loopLengthTicks;
  request.work.admittedLoopLength = loop.loopLengthTicks;
  request.work.admittedIntervalLength = request.coverage.lengthTicks;
  request.work.retainedEventBound = RuntimePlaybackViewConfig::kWindowAdmissionLimit;
  request.reason = PlaybackViewPendingReason::AwaitingSourceQualification;
  reportPlaybackLaunchGateRow(slotIndex, "launch_requested", sourceRequestNeeded, sourceRequested);
  runtime->acquireWorkingSetLease(WorkingSetRole::PlaybackWindow);
  runtime->pendingViewRequest_ = request;
  runtime->launchPreparation = LoopPlaybackRuntime::LaunchPreparation::Requested;
  return true;
}

bool Track::playbackLaunchReadyForSlot(uint8_t slotIndex) const {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || !loopsAllocated()) return false;
  const auto* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime && runtime->launchPreparation == LoopPlaybackRuntime::LaunchPreparation::Fallback)
    return isPlaybackMergedMidiEventsReadyForSlot(slotIndex);
  if (!runtime || runtime->launchPreparation != LoopPlaybackRuntime::LaunchPreparation::Ready || runtime->ownsCoverageSource()) return false;
  const auto* set = runtime->viewSetIfPrewarmed();
  if (!set || set->lifecycle.ready >= set->views.size()) return false;
  const auto& view = set->views[set->lifecycle.ready];
  const auto& loop = loopForSlot(slotIndex);
  return loop.hasCommittedPasses() && loop.publishedSnapshotIsCurrent() && view.complete &&
      view.source.sourceGeneration == playbackGeneration && view.source.playbackRevision == loop.playbackRevision &&
      view.source.origin == PlaybackWindowOrigin::Committed && !view.rawCoveragePrepared &&
      view.coverage.loopLengthTicks == loop.loopLengthTicks && view.coverage.startPhase == 0 &&
      view.coverage.lengthTicks == 2 * Config::TICKS_PER_BAR;
}

bool Track::bindPlaybackLaunchForSlot(uint8_t slotIndex, uint32_t tick) {
  if (!playbackLaunchReadyForSlot(slotIndex)) return false;
  auto* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime->launchPreparation == LoopPlaybackRuntime::LaunchPreparation::Fallback) {
    runtime->launchPreparation = LoopPlaybackRuntime::LaunchPreparation::FallbackBound;
    return true;
  }
  auto* set = runtime->viewSetIfPrewarmed();
  if (!set->handoffReadyCoverage(PlaybackViewHandoffMode::Audible)) return false;
  runtime->launchBoundTick = tick;
  runtime->launchPreparation = LoopPlaybackRuntime::LaunchPreparation::Bound;
  runtime->syncRevision(loopForSlot(slotIndex).playbackRevision, playbackGeneration);
  return true;
}

#if PLAYBACK_WINDOW_ROLLING_ENABLED
namespace {
// Same one-core PRIMASK save/restore pattern as ClockManager. This section
// covers fixed-size metadata only; never source traversal, ordering or release.
class RollingMetadataGuard {
 public:
  RollingMetadataGuard() {
#if defined(__IMXRT1062__)
    __asm__ volatile("mrs %0, primask" : "=r"(saved_));
    __asm__ volatile("cpsid i" ::: "memory");
#endif
  }
  ~RollingMetadataGuard() {
#if defined(__IMXRT1062__)
    __asm__ volatile("dmb" ::: "memory");
    __asm__ volatile("msr primask, %0" :: "r"(saved_) : "memory");
#endif
  }
 private:
  uint32_t saved_ = 0;
};
TRACK_COLD_MEM __attribute__((noinline)) Track::RollingResolution rejectRollingFrame(
    LoopPlaybackRuntime& runtime, Loop& loop) {
  auto& frame = runtime.rolling;
  const auto* set = runtime.viewSetIfPrewarmed();
  if (set && set->lifecycle.current == frame.boundView && frame.boundView < set->views.size() &&
      set->views[frame.boundView].storageGeneration == frame.boundStorageGeneration)
    loop.nextEventIndex = 0;
  frame.phase = LoopPlaybackRuntime::RollingPhase::Blocked;
  return {Track::RollingOutcome::Rejected, nullptr};
}

// Boundary-only validation is flash-resident; steady serving stays in ITCM.
// Fixed metadata reads only, under RollingMetadataGuard. Device duration remains
// a qualification obligation; this placement is not a timing claim.
TRACK_COLD_MEM __attribute__((noinline)) bool rollingCandidateReadyAtBoundary(
    const LoopPlaybackRuntime& runtime, const Loop& loop, uint32_t phase) {
  const auto& frame = runtime.rolling;
  const auto* set = runtime.viewSetIfPrewarmed();
  if (!set || runtime.ownsCoverageSource() || runtime.completedPromisePendingJudgement ||
      runtime.admittedPromiseInvalidated || runtime.countedMissedTraversals != 0 ||
      loop.lastTickInLoop != (phase == 0 ? loop.loopLengthTicks - 1 : phase - 1) ||
      set->lifecycle.ready >= set->views.size()) return false;
  const auto& work = runtime.viewBuildWork;
  const auto& ready = set->views[set->lifecycle.ready];
  return work.clockEpoch == frame.epoch && work.deadlineTick == frame.deadlineTick &&
      runtime.viewBuildOperationGeneration == frame.generation && loop.publishedSnapshotIsCurrent() &&
      runtime.cachedLoopRevision == loop.playbackRevision && runtime.cachedTrackGeneration == frame.generation &&
      ready.complete && !ready.published && !ready.rawCoveragePrepared &&
      ready.source.origin == PlaybackWindowOrigin::Committed && ready.source.sourceGeneration == frame.generation &&
      ready.source.playbackRevision == loop.playbackRevision && ready.coverage.startPhase == frame.startPhase &&
      ready.coverage.lengthTicks == 2 * Config::TICKS_PER_BAR && ready.coverage.loopLengthTicks == frame.loopLength;
}
}
#endif

TRACK_COLD_MEM void Track::cancelRollingPlaybackWindow(uint8_t slot) {
#if PLAYBACK_WINDOW_ROLLING_ENABLED
  RollingMetadataGuard guard;
  auto* runtime = playbackRuntime.slotIfAllocated(slot);
  if (!runtime || runtime->rolling.phase == LoopPlaybackRuntime::RollingPhase::Idle) return;
  const auto frame = runtime->rolling;
  const auto matches = [&](const PlaybackViewPendingRequest& request) {
    return request.active && request.sourceKind == PlaybackViewPendingRequest::SourceKind::CommittedSnapshot &&
        request.operationGeneration == frame.generation && request.clockEpoch == frame.epoch &&
        request.deadlineTick == frame.deadlineTick && request.coverage.startPhase == frame.startPhase;
  };
  const bool frozenOwned = matches(runtime->frozenViewRequest);
  const bool admittedOwned = runtime->viewBuild.coverageOwned() &&
      runtime->viewBuildOperationGeneration == frame.generation &&
      runtime->viewBuildWork.clockEpoch == frame.epoch && runtime->viewBuildWork.deadlineTick == frame.deadlineTick;
  if (matches(runtime->pendingViewRequest_)) runtime->clearViewPendingRequest();
  if (frozenOwned) runtime->frozenViewRequest = {};
  if (frozenOwned || admittedOwned) runtime->discardCoverageBuild();
  auto* set = runtime->viewSetIfPrewarmed();
  if (set && loopsAllocated() && set->lifecycle.current == frame.boundView && frame.boundView < set->views.size() &&
      set->views[frame.boundView].storageGeneration == frame.boundStorageGeneration)
    loopForSlot(slot).nextEventIndex = 0;
  if (admittedOwned && set && set->lifecycle.ready < set->views.size()) {
    auto& ready = set->views[set->lifecycle.ready];
    if (!ready.published && ready.source.sourceGeneration == frame.generation &&
        ready.coverage.startPhase == frame.startPhase) ready.resetMetadata();
  }
  // Cancellation ends this operation, not its interval's lag/fault history.
  runtime->rolling = LoopPlaybackRuntime::RollingFrame{};
#else
  (void)slot;
#endif
}

TRACK_COLD_MEM Track::RollingOutcome Track::serviceRollingPlaybackWindow(PlaybackDeadlineClock clock) {
#if !PLAYBACK_WINDOW_ROLLING_ENABLED
  (void)clock;
  return RollingOutcome::Disabled;
#else
  RollingMetadataGuard guard;
  for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot)
    if (slot != activeLoopIndex) cancelRollingPlaybackWindow(slot);
  auto* runtime = playbackRuntime.slotIfAllocated(activeLoopIndex);
  if (!runtime || !loopsAllocated()) {
    reportPlaybackRollingGateRow("win_no_runtime", clock.epoch != 0);
    return RollingOutcome::Excluded;
  }
  auto& frame = runtime->rolling;
  auto& loop = getActiveLoop();
  if (!isPlaying() || editManager.isNoteEditActive() || hasPendingDeferredOverdubStop() || runtime->previewSourceActive ||
      runtime->hasBoundRecordReturnWindowForPendingJob()) {
    cancelRollingPlaybackWindow(activeLoopIndex);
    reportPlaybackRollingGateRow("win_track_not_playing", clock.epoch != 0);
    return RollingOutcome::Excluded;
  }
  if (hasQueuedPlaybackStart() || isJamPlaybackActive() || isJamming()) {
    if (frame.phase != LoopPlaybackRuntime::RollingPhase::Idle)
      frame.phase = LoopPlaybackRuntime::RollingPhase::Blocked;
    reportPlaybackRollingGateRow("win_reprojection_blocked", clock.epoch != 0);
    return RollingOutcome::Excluded; // Re-projection does not erase the positioned debt.
  }
  constexpr uint32_t span = 2 * Config::TICKS_PER_BAR;
  if (!clock.epoch || !loop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks) ||
      loop.loopLengthTicks % span != 0) {
    // The demand's **entry** decision, and the term the device question rests on: an unstamped deadline clock, a loop
    // below the demotion bound and an unaligned length all leave here, and the row tells them apart by `ep`/`av`/`al`
    // rather than by a token that would hide which one refused.
    reportPlaybackRollingGateRow("win_window_ineligible", clock.epoch != 0);
    return RollingOutcome::Excluded;
  }
  if (frame.phase != LoopPlaybackRuntime::RollingPhase::Idle) {
    if (frame.epoch != clock.epoch || frame.generation != playbackGeneration ||
        frame.loopLength != loop.loopLengthTicks ||
        (uint32_t(getProjectionCycleStartTick()) - uint32_t(frame.projectionStart)) % frame.loopLength != 0) {
      frame.phase = LoopPlaybackRuntime::RollingPhase::Blocked;
      reportPlaybackRollingGateRow("win_frame_invalidated", clock.epoch != 0);
      return RollingOutcome::Rejected; // Keep the original debt; do not grant a new clock frame.
    }
    if (frame.phase == LoopPlaybackRuntime::RollingPhase::Serving) {
      reportPlaybackRollingGateRow("win_serving", clock.epoch != 0);
      return RollingOutcome::Serving;
    }
    if (frame.phase == LoopPlaybackRuntime::RollingPhase::Blocked) {
      reportPlaybackRollingGateRow("win_blocked", clock.epoch != 0);
      return RollingOutcome::Rejected;
    }
    const auto* request = runtime->pendingViewRequest();
    if (request && (request->positionInvalidated ||
        request->reason == PlaybackViewPendingReason::CannotCompleteBeforeHandoff ||
        request->reason == PlaybackViewPendingReason::WindowExceedsRetainedCapacity)) {
      frame.phase = LoopPlaybackRuntime::RollingPhase::Blocked;
      reportPlaybackRollingGateRow("win_blocked_by_request", clock.epoch != 0);
      return RollingOutcome::Rejected;
    }
    if (frame.phase != LoopPlaybackRuntime::RollingPhase::Ready) {
      reportPlaybackRollingGateRow("win_waiting", clock.epoch != 0);
      return RollingOutcome::Waiting;
    }
    reportPlaybackRollingGateRow("win_ready", clock.epoch != 0);
    return RollingOutcome::Ready;
  }
  auto* set = runtime->viewSetIfPrewarmed();
  if (!set || set->lifecycle.current >= set->views.size() ||
      !set->views[set->lifecycle.current].complete || !set->views[set->lifecycle.current].published ||
      !loop.publishedSnapshotIsCurrent()) {
    reportPlaybackRollingGateRow("win_not_servable", clock.epoch != 0);
    return RollingOutcome::Excluded;
  }
  // Existing promises and shared owners take precedence. Never coalesce a new
  // rolling target over someone else's debt merely to gain a later deadline.
  if (runtime->pendingViewRequest_.active || runtime->frozenViewRequest.active ||
      !runtime->coverageRoleAvailable() || playbackViewOutstandingGrains(activeLoopIndex) != 0) {
    reportPlaybackRollingGateRow("win_busy", clock.epoch != 0);
    return RollingOutcome::Busy;
  }
  const uint32_t effective = getEffectivePlaybackTick(static_cast<uint32_t>(clock.tick));
  if (effective != static_cast<uint32_t>(clock.tick)) {
    reportPlaybackRollingGateRow("win_reprojected_tick", clock.epoch != 0, /*reprojectedTick=*/true);
    return RollingOutcome::Excluded;
  }
  const uint32_t phase = IntervalProjection::tickPhaseInProjectionCycle(
      effective, getProjectionCycleStartTick(), loop.loopLengthTicks);
  const auto contract = playbackViewCoverageContract();
  const uint64_t lookahead = uint64_t(contract.requiredGrains()) *
      (uint64_t(contract.maximumConsecutiveDeniedTurns) + 1) * contract.maximumPhaseAdvancePerTurn + contract.leadTicks;
  const uint64_t boundary = ((uint64_t(phase) + lookahead + span - 1) / span) * span;
  if (!lookahead || boundary <= phase || boundary - phase >= loop.loopLengthTicks ||
      clock.tick > UINT64_MAX - (boundary - phase)) {
    reportPlaybackRollingGateRow("win_boundary_rejected", clock.epoch != 0);
    return RollingOutcome::Rejected;
  }
  const PlaybackCoverageInterval coverage{static_cast<uint32_t>(boundary % loop.loopLengthTicks), span, loop.loopLengthTicks};
  const PlaybackViewRequestResult requested = requestCommittedPlaybackWindow(activeLoopIndex, coverage, phase, clock);
  if (requested != PlaybackViewRequestResult::DeferredSourceQualification) {
    reportPlaybackRollingGateRow("win_request_refused", clock.epoch != 0, /*reprojectedTick=*/false,
                                 static_cast<uint8_t>(requested));
    return RollingOutcome::Rejected;
  }
  const auto* pending = runtime->pendingViewRequest();
  if (!pending) {
    reportPlaybackRollingGateRow("win_request_dropped", clock.epoch != 0, /*reprojectedTick=*/false,
                                 static_cast<uint8_t>(requested));
    return RollingOutcome::Rejected;
  }
  frame.boundView = RuntimePlaybackViewLifecycle::kNoView;
  frame.boundStorageGeneration = 0;
  frame.epoch = pending->clockEpoch;
  frame.generation = pending->operationGeneration;
  frame.loopLength = loop.loopLengthTicks;
  frame.projectionStart = getProjectionCycleStartTick();
  frame.startPhase = coverage.startPhase;
  frame.deadlineTick = pending->deadlineTick;
  frame.lastTick = clock.tick;
  frame.phase = LoopPlaybackRuntime::RollingPhase::Requested; // Publish metadata last.
  reportPlaybackRollingGateRow("win_requested", clock.epoch != 0, /*reprojectedTick=*/false, static_cast<uint8_t>(requested));
  return RollingOutcome::Requested;
#endif
}

Track::RollingResolution Track::resolveRollingPlaybackWindow(
    uint8_t slot, uint32_t tick, PlaybackDeadlineClock clock) {
#if !PLAYBACK_WINDOW_ROLLING_ENABLED
  (void)slot; (void)tick; (void)clock;
  return {RollingOutcome::Disabled, nullptr};
#else
  RollingMetadataGuard guard;
  auto* runtime = playbackRuntime.slotIfAllocated(slot);
  if (!runtime || runtime->rolling.phase == LoopPlaybackRuntime::RollingPhase::Idle)
    return {RollingOutcome::Excluded, nullptr};
  auto& frame = runtime->rolling;
  auto& loop = loopForSlot(slot);
  auto* set = runtime->viewSetIfPrewarmed();
  if (frame.phase == LoopPlaybackRuntime::RollingPhase::Blocked) return {RollingOutcome::Rejected, nullptr};
  if (slot != activeLoopIndex || !isPlaying() || isJamPlaybackActive() || isJamming() ||
      hasQueuedPlaybackStart() || editManager.isNoteEditActive() || hasPendingDeferredOverdubStop()) return rejectRollingFrame(*runtime, loop);
  if (!clock.epoch || clock.epoch != frame.epoch || clock.tick < frame.lastTick ||
      uint32_t(clock.tick) != tick || playbackGeneration != frame.generation ||
      loop.loopLengthTicks != frame.loopLength || !frame.loopLength ||
      (uint32_t(getProjectionCycleStartTick()) - uint32_t(frame.projectionStart)) % frame.loopLength != 0)
    return rejectRollingFrame(*runtime, loop);
  frame.lastTick = clock.tick;
  if (frame.phase == LoopPlaybackRuntime::RollingPhase::Blocked) return {RollingOutcome::Rejected, nullptr};
  const uint32_t phase = IntervalProjection::tickPhaseInProjectionCycle(tick,
      getProjectionCycleStartTick(), loop.loopLengthTicks);
  if (!set) return rejectRollingFrame(*runtime, loop);
  constexpr uint32_t span = 2 * Config::TICKS_PER_BAR;
  if (frame.phase == LoopPlaybackRuntime::RollingPhase::Serving) {
    if (set->lifecycle.current >= set->views.size() || set->lifecycle.current != frame.boundView) return rejectRollingFrame(*runtime, loop);
    const auto& current = set->views[set->lifecycle.current];
    if (!current.complete || !current.published || current.storageGeneration != frame.boundStorageGeneration ||
        current.source.sourceGeneration != frame.generation || current.source.origin != PlaybackWindowOrigin::Committed ||
        current.coverage.startPhase != frame.startPhase || current.coverage.lengthTicks != span ||
        current.coverage.loopLengthTicks != frame.loopLength) return rejectRollingFrame(*runtime, loop);
    if (clock.tick >= frame.deadlineTick && clock.tick - frame.deadlineTick >= span) {
      frame.phase = LoopPlaybackRuntime::RollingPhase::Idle;
      loop.nextEventIndex = 0; // Fallback's crossing predicate excludes the already-served interval.
      return {RollingOutcome::Expired, nullptr};
    }
    if (!current.coverage.contains(phase)) return rejectRollingFrame(*runtime, loop);
    // The copied interval is immutable even if a newer content revision arrives.
    return {RollingOutcome::Serving, &current};
  }
  if (clock.tick < frame.deadlineTick) return {RollingOutcome::Waiting, nullptr};
  if (clock.tick != frame.deadlineTick || frame.phase != LoopPlaybackRuntime::RollingPhase::Ready ||
      phase != frame.startPhase || !rollingCandidateReadyAtBoundary(*runtime, loop, phase)) return rejectRollingFrame(*runtime, loop);
  if (!adoptPreparedPlaybackWindow(slot)) return rejectRollingFrame(*runtime, loop);
  loop.nextEventIndex = 0; // First event in the new half-open interval, no inline search/build.
  frame.boundView = set->lifecycle.current;
  frame.boundStorageGeneration = set->views[frame.boundView].storageGeneration;
  frame.phase = LoopPlaybackRuntime::RollingPhase::Serving;
  return {RollingOutcome::Serving, &set->views[set->lifecycle.current]};
#endif
}

TRACK_COLD_MEM bool Track::committedPlaybackRetryCanProgress(uint8_t slotIndex, PlaybackDeadlineClock clock) const {
  const auto* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (!runtime) return false;
  if (runtime->coverageSource.retiring()) return true;
  // A frozen reservation always gets a turn, including cancellation/invalidity.
  if (runtime->ownsCoverageSource()) return true;
  const auto* request = pendingViewRequest(slotIndex);
  const bool launch = request && request->sourceKind == PlaybackViewPendingRequest::SourceKind::CommittedLaunch;
  if (!request || (!launch && request->sourceKind != PlaybackViewPendingRequest::SourceKind::CommittedSnapshot) ||
      request->positionInvalidated || (!launch && (request->clockEpoch == 0 || clock.epoch != request->clockEpoch ||
      clock.tick < request->work.admittedTick || clock.tick >= request->deadlineTick)) ||
      request->reason == PlaybackViewPendingReason::WindowExceedsRetainedCapacity ||
      request->reason == PlaybackViewPendingReason::CannotCompleteBeforeHandoff ||
      !runtime->coverageRoleAvailable() || playbackViewOutstandingGrains(slotIndex) != 0) return false;
  const auto& loop = loopForSlot(slotIndex);
  return request->loopLengthTicks == loop.loopLengthTicks && loop.publishedSnapshotIsCurrent() &&
      request->source.playbackRevision == loop.playbackRevision;
}

TRACK_COLD_MEM Track::PlaybackViewTurnResult Track::stepCommittedPlaybackRetry(
    uint8_t slotIndex, PlaybackDeadlineClock clock) {
  auto* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (!runtime || !committedPlaybackRetryCanProgress(slotIndex, clock)) return PlaybackViewTurnResult::Idle;
  if (runtime->coverageSource.retiring()) {
    runtime->stepCoverageSourceRetirement(kPlaybackViewFillQuantum);
    return PlaybackViewTurnResult::GrainServed;
  }
  if (!runtime->ownsCoverageSource()) {
    // A temporary root reference cannot retire the root here: the Loop still owns
    // this current root throughout this non-interleaved main-loop turn.
    const auto snapshot = loopForSlot(slotIndex).publishedSnapshot();
    runtime->pendingViewRequest_.source.snapshotIdentity = snapshot.get();
    return snapshot && runtime->beginCoverageQualification(*snapshot)
        ? PlaybackViewTurnResult::GrainServed : PlaybackViewTurnResult::GrainDenied;
  }
  auto& request = runtime->frozenViewRequest;
  const auto& loop = loopForSlot(slotIndex);
  const bool launch = request.sourceKind == PlaybackViewPendingRequest::SourceKind::CommittedLaunch;
  if (!request.active || request.operationGeneration != playbackGeneration || request.positionInvalidated ||
      (!launch && (clock.epoch == 0 || request.clockEpoch != clock.epoch || clock.tick < request.work.admittedTick ||
      clock.tick >= request.deadlineTick)) || request.loopLengthTicks != loop.loopLengthTicks ||
      !loop.publishedSnapshotIsCurrent() || request.source.playbackRevision != loop.playbackRevision) {
    if (launch) {
      cancelPlaybackLaunchForSlot(slotIndex);
      return PlaybackViewTurnResult::GrainDenied;
    }
    if (request.active) {
      if (request.loopLengthTicks != loop.loopLengthTicks)
        request.reason = PlaybackViewPendingReason::DeadlineInvalidatedGeometry;
      else if (request.clockEpoch != clock.epoch || clock.tick < request.work.admittedTick)
        request.reason = PlaybackViewPendingReason::DeadlineInvalidatedEpoch;
      else if (clock.tick >= request.deadlineTick) request.reason = PlaybackViewPendingReason::DeadlineExpired;
      else request.reason = PlaybackViewPendingReason::SourceNotCurrent;
      // A newer content revision invalidates this source, not the clock frame.
      // Carry a still-valid older deadline into the newer coalesced demand.
      request.positionInvalidated = request.positionInvalidated ||
          request.loopLengthTicks != loop.loopLengthTicks || request.clockEpoch != clock.epoch ||
          clock.tick < request.work.admittedTick || clock.tick >= request.deadlineTick;
    }
    runtime->retireCoverageSource();
    return PlaybackViewTurnResult::GrainDenied;
  }
  if (runtime->coveragePreparation == LoopPlaybackRuntime::CoveragePreparation::Counting)
    return runtime->stepCoverageQualification(kPlaybackViewFillQuantum)
        ? PlaybackViewTurnResult::GrainServed : PlaybackViewTurnResult::GrainDenied;
  if (runtime->coveragePreparation != LoopPlaybackRuntime::CoveragePreparation::AwaitAdmission)
    return PlaybackViewTurnResult::Idle;
  auto contract = playbackViewCoverageContract();
  contract.deriveSnapshotGrainsFrom(runtime->coverageSourceUnits, runtime->coverageEventCount,
      kPlaybackViewFillQuantum, kPlaybackViewOrderQuantum);
  contract.requiredFillGrains = 0; // Already completed in private qualification; never reread.

  // Count qualification visited every page. Charge its pinned topology, including
  // all fixed child slots and empty pages, independent of today's sharing. Before
  // qualification the outstanding preparation budget still uses the global bound.
  const uint32_t releaseGrains = (runtime->coverageSource.qualifiedReleaseUnitBound() +
      kPlaybackViewFillQuantum - 1) / kPlaybackViewFillQuantum;
  contract.requiredPromotionGrains = static_cast<uint16_t>(1 + releaseGrains);
  // Launch has no deadline to expire. A sibling appearing after qualification
  // temporarily owns service; preserve the counted source and retry admission.
  if (launch && playbackViewOutstandingGrains(slotIndex) != 0)
    return PlaybackViewTurnResult::GrainDenied;
  if ((!launch && !contract.canCompleteWithinLead(request.deadlineTick - clock.tick, playbackViewOutstandingGrains(slotIndex))) ||
      (!launch && !playbackViewAdmissionKeepsAdmittedDeadlines(contract, request.requestedPhase, request.loopLengthTicks,
          contract.requiredGrains(), clock))) {
    request.reason = PlaybackViewPendingReason::CannotCompleteBeforeHandoff;
    runtime->retireCoverageSource();
    return PlaybackViewTurnResult::GrainDenied;
  }
  return runtime->beginQualifiedCoverageBuild(contract, clock)
      ? PlaybackViewTurnResult::GrainServed : PlaybackViewTurnResult::GrainDenied;
}

TRACK_COLD_MEM bool Track::hasPendingPlaybackViewMaintenance() const {
  for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
    const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slot);
    const bool runtimePresent = runtime != nullptr;
    if (MidiServiceDrain::playbackViewMaintenanceNeeded(
            runtimePresent, runtimePresent && (runtime->coverageBuildNeedsService() || runtime->ownsCoverageSource() ||
                (pendingViewRequest(slot) && pendingViewRequest(slot)->sourceKind !=
                    PlaybackViewPendingRequest::SourceKind::Borrowed)))) {
      return true;
    }
  }
  return false;
}

TRACK_COLD_MEM void Track::fillPlaybackHolders(uint32_t currentPhase,
                                               MidiServiceDrain::PlaybackHolder* holders, PlaybackDeadlineClock clock) const {
  // DEC-108 §13b / task 3.10: this track's **holder set** -- the slots that owe a grain -- each with its urgency.
  // The urgency is the tick distance to the handoff (`handoffLeadTicks`), the same quantity the admission judges, so
  // the scheduler's answer and the admission's margin cannot be two different numbers. Nothing is stored to get it:
  // an in-flight build already carries its interval on the view it fills
  // (`RuntimePlaybackViewFill::begin` sets `destination.coverage`).
  for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
    const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slot);
    if (runtime == nullptr) continue;
    const auto* pending = pendingViewRequest(slot);
    if (!runtime->coverageBuildNeedsService() && (runtime->ownsCoverageSource() ||
        (pending && (pending->sourceKind == PlaybackViewPendingRequest::SourceKind::CommittedSnapshot ||
                     pending->sourceKind == PlaybackViewPendingRequest::SourceKind::CommittedLaunch)))) {
      auto& holder = holders[slot];
      holder.eligible = true;
      holder.canProgress = committedPlaybackRetryCanProgress(slot, clock);
      if (runtime->coverageSource.retiring() || (runtime->ownsCoverageSource() && !runtime->frozenViewRequest.active)) {
        holder.hasDeadline = true; holder.handoffLeadTicks = 0; continue;
      }
      const auto& request = runtime->frozenViewRequest.active ? runtime->frozenViewRequest : *pending;
      holder.hasDeadline = request.clockEpoch != 0 && request.clockEpoch == clock.epoch;
      const uint64_t lead = request.deadlineTick > clock.tick ? request.deadlineTick - clock.tick : 0;
      holder.handoffLeadTicks = lead > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(lead);
      continue;
    }
    if (!runtime->coverageBuildNeedsService()) continue;
    const RuntimePlaybackViewSet* set = runtime->viewSetIfPrewarmed();
    if (set == nullptr || set->lifecycle.build >= RuntimePlaybackViewConfig::kViewCount) {
      continue;
    }
    const PlaybackCoverageInterval& interval = set->views[set->lifecycle.build].coverage;
    const Loop& loop = loopForSlot(slot);
    MidiServiceDrain::PlaybackHolder& holder = holders[slot];
    holder.eligible = true;
    holder.hasDeadline = interval.isValid() && loop.loopLengthTicks == interval.loopLengthTicks &&
                         loop.loopLengthTicks > 0;
    holder.handoffLeadTicks =
        holder.hasDeadline
            ? handoffLeadTicks(currentPhase % loop.loopLengthTicks, interval.startPhase, loop.loopLengthTicks)
            : 0;
    // A begun build's grains are bounded work, so an owing holder can always progress one -- the refusals live at
    // *request* time (`CannotCompleteBeforeHandoff`, `SourceNotCurrent`). The policy's blocked path is for a holder
    // whose source goes stale mid-build; that is named here rather than assumed away.
    holder.canProgress = true;
    if (runtime->launchPreparation == LoopPlaybackRuntime::LaunchPreparation::Building) holder.hasDeadline = false;
    const auto* work = runtime->activeViewBuildWork();
    if (work && work->clockEpoch && clock.epoch == work->clockEpoch) {
      holder.hasDeadline = true;
      const uint64_t lead = work->deadlineTick > clock.tick ? work->deadlineTick - clock.tick : 0;
      holder.handoffLeadTicks = lead > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(lead);
    }
  }
}

TRACK_COLD_MEM MidiServiceDrain::PlaybackHolder Track::playbackWindowServiceOffer(
    uint32_t currentPhase, PlaybackDeadlineClock clock) const {
  // The **most urgent** owing holder, read without serving: the cross-track counterpart of the turn below. The one
  // production caller builds every track's offer and lets the shared policy choose one track per turn, so a
  // lowest-index track whose interval is a whole pass away no longer monopolises the opportunity.
  MidiServiceDrain::PlaybackHolder holders[Config::MAX_LOOPS_PER_TRACK] = {};
  fillPlaybackHolders(currentPhase, holders, clock);
  MidiServiceDrain::PlaybackHolder offer{};
  for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
    const MidiServiceDrain::PlaybackHolder& holder = holders[slot];
    if (!holder.eligible) {
      continue;
    }
    if (!offer.eligible || (holder.canProgress && !offer.canProgress) ||
        (holder.canProgress == offer.canProgress && MidiServiceDrain::playbackHolderDeadlineIsEarlier(holder, offer))) {
      offer = holder;
    }
  }
  return offer;
}

#if defined(SESSION_CAPTURE) || defined(PIO_UNIT_TEST_NATIVE)
#if defined(__IMXRT1062__)
DMAMEM static PlaybackLaneMeasurement sLaneMeasurement;
#else
static PlaybackLaneMeasurement sLaneMeasurement;
#endif
#endif

TRACK_COLD_MEM void Track::resetPlaybackLaneMeasurement() {
#if defined(SESSION_CAPTURE) || defined(PIO_UNIT_TEST_NATIVE)
  sLaneMeasurement = PlaybackLaneMeasurement{};  // DMAMEM is NOLOAD on Teensy.
#endif
}

TRACK_COLD_MEM void Track::samplePlaybackLaneOpportunity(
    PlaybackDeadlineClock clock, bool running, bool eligible,
    PlaybackViewTurnResult result, uint32_t workerUs) {
#if defined(SESSION_CAPTURE) || defined(PIO_UNIT_TEST_NATIVE)
  // DEC-108 §13b / task 3.10 (blocker 3): the **declared permitted limit** is the comparison this sampler applies.
  // It is read from the declaration itself (not restated), so the bound and the observation cannot drift apart.
  sLaneMeasurement.observe(clock, running, eligible,
      result == PlaybackViewTurnResult::GrainServed || result == PlaybackViewTurnResult::BuildComplete,
      result == PlaybackViewTurnResult::GrainDenied, workerUs, kPlaybackViewPermittedAdvancePerTurn);
  // First row then every 256 dispatches, and on an eligible->idle completion.
  // Idle dispatches are now counted, so the former 16-turn cadence would flood capture.
  if (sLaneMeasurement.opportunities == 1 || sLaneMeasurement.opportunities % 256 == 0 ||
      result == PlaybackViewTurnResult::BuildComplete || result == PlaybackViewTurnResult::GrainDenied) {
    SC_WINDOW_SERVICE_GAP(sLaneMeasurement, static_cast<uint32_t>(clock.tick), running, clock.epoch);
    if (sLaneMeasurement.overruns > 0) {
      // Named **before** the block is cleared, and only when the block saw one: the limit's exceedance is an event in
      // the stream (blocker 3's "operationally reported"), not a number a reader has to derive from a maximum.
      SC_WINDOW_GAP_OVERRUN(kPlaybackViewPermittedAdvancePerTurn, sLaneMeasurement.overruns,
                            sLaneMeasurement.overrunMaxTicks, running, clock.epoch);
    }
    sLaneMeasurement.clearBlock();
  }
#else
  (void)clock; (void)running; (void)eligible; (void)result; (void)workerUs;
#endif
}

TRACK_COLD_MEM Track::PlaybackViewTurnResult Track::stepPlaybackViewMaintenance(
    uint32_t currentPhase, MidiServiceDrain::PlaybackHolderCursor* cursor, bool transportRunning,
    PlaybackDeadlineClock clock, PlaybackDeadlineClock (*completionClockReader)()) {
  (void)transportRunning;  // Compatibility argument; sampling belongs to the lane dispatcher.
  // DEC-108 §13a / task 3.10 (blocker 3, half 2): **the deadlines the admission promised are serviced first**, for every
  // slot holding a live promise -- not only the holder this turn happens to serve, and not only when a build completes.
  // The judgement never depends on the revision currency; the consequence is `noteMissedTraversalForSlot`'s.
  serviceAdmittedDeadlineMisses(clock);
  MidiServiceDrain::PlaybackHolder holders[Config::MAX_LOOPS_PER_TRACK] = {};
  fillPlaybackHolders(currentPhase, holders, clock);
  // The shared policy chooses which holder this turn serves (task 3.10): holders rotate, the earliest outstanding
  // handoff is served first, and the cursor advances even when the chosen holder cannot progress.
  MidiServiceDrain::PlaybackHolderCursor localCursor{};
  MidiServiceDrain::PlaybackHolderCursor& rotation = cursor != nullptr ? *cursor : localCursor;
  uint8_t chosen = MidiServiceDrain::kNoPlaybackHolder;
  if (MidiServiceDrain::selectPlaybackHolder(rotation, holders, Config::MAX_LOOPS_PER_TRACK, chosen) !=
      MidiServiceDrain::PlaybackHolderSelection::Service) {
    return PlaybackViewTurnResult::Idle;
  }
  const uint8_t slot = chosen;
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slot);
  if (runtime == nullptr) return PlaybackViewTurnResult::Idle;
  if (!runtime->coverageBuildNeedsService()) return stepCommittedPlaybackRetry(slot, clock);
  if (runtime->launchPreparation == LoopPlaybackRuntime::LaunchPreparation::Building) {
    const auto* set = runtime->viewSetIfPrewarmed();
    const auto& loop = loopForSlot(slot);
    if (!set || set->lifecycle.build >= set->views.size() ||
        set->views[set->lifecycle.build].source.playbackRevision != loop.playbackRevision ||
        set->views[set->lifecycle.build].source.sourceGeneration != playbackGeneration ||
        set->views[set->lifecycle.build].coverage.loopLengthTicks != loop.loopLengthTicks ||
        !loop.publishedSnapshotIsCurrent()) {
      cancelPlaybackLaunchForSlot(slot);
      return PlaybackViewTurnResult::GrainDenied;
    }
  }
  // One grain, and only one: the phase decides which, so a turn can never copy source entries *and* merge
  // order entries (the build contract's own rule), and the build's persistent cursor carries the progress
  // to the next turn.
  const RuntimePlaybackViewBuildGrain grain = runtime->nextCoverageBuildGrain();
  RuntimePlaybackViewBuildGrain result = RuntimePlaybackViewBuildGrain::Denied;
  if (grain == RuntimePlaybackViewBuildGrain::Fill) {
    result = runtime->stepCoverageBuildFill(kPlaybackViewFillQuantum);
  } else if (grain == RuntimePlaybackViewBuildGrain::Order) {
    result = runtime->stepCoverageBuildOrder(kPlaybackViewOrderQuantum);
  } else {
    // Only admitted coverage-owned Fill/Order work reaches this branch; source
    // qualification and retirement took the separate bounded retry branch above.
    return PlaybackViewTurnResult::Idle;
  }
  if (result == RuntimePlaybackViewBuildGrain::Complete) {
    // Preserve the candidate until its actual completion time is qualified. main's
    // reader uses ClockManager; native fixtures can advance it within the final grain.
    runtime->completedPromisePendingJudgement = runtime->viewBuildWork.clockEpoch != 0;
    if (runtime->completedPromisePendingJudgement) {
      const PlaybackDeadlineClock finished = completionClockReader != nullptr
          ? completionClockReader() : clockManager.getPlaybackDeadlineClock();
      serviceAdmittedDeadlineMisses(finished);
      const auto& work = runtime->viewBuildWork;
      if (!runtime->admittedPromiseInvalidated && finished.epoch == work.clockEpoch &&
          finished.epoch != 0 && finished.tick >= work.admittedTick &&
          finished.tick < work.deadlineTick && runtime->countedMissedTraversals == 0 &&
          (clock.epoch == 0 || (finished.epoch == clock.epoch && finished.tick >= clock.tick)) &&
          loopForSlot(slot).loopLengthTicks == work.admittedLoopLength) {
        runtime->completedPromisePendingJudgement = false;
      }
    }
    if (runtime->launchPreparation == LoopPlaybackRuntime::LaunchPreparation::Building) {
      auto* set = runtime->viewSetIfPrewarmed();
      if (set && set->lifecycle.publishCompletedBuildAsReadySuccessor(set->views))
        runtime->launchPreparation = LoopPlaybackRuntime::LaunchPreparation::Ready;
    }
#if PLAYBACK_WINDOW_ROLLING_ENABLED
    else if (runtime->rolling.phase == LoopPlaybackRuntime::RollingPhase::Requested &&
             runtime->viewBuildWork.deadlineTick == runtime->rolling.deadlineTick &&
             runtime->viewBuildWork.clockEpoch == runtime->rolling.epoch) {
      RollingMetadataGuard guard;
      auto* set = runtime->viewSetIfPrewarmed();
      if (runtime->rolling.generation != playbackGeneration || slot != activeLoopIndex || !isPlaying()) {
        cancelRollingPlaybackWindow(slot); // Ownership changed inside the final grain: publish nothing.
      } else if (runtime->rolling.phase == LoopPlaybackRuntime::RollingPhase::Requested &&
          !runtime->completedPromisePendingJudgement && !runtime->admittedPromiseInvalidated && set &&
          set->lifecycle.publishCompletedBuildAsReadySuccessor(set->views))
        runtime->rolling.phase = LoopPlaybackRuntime::RollingPhase::Ready;
    }
#endif
    else (void)publishCompletedPlaybackWindow(slot);
    if (runtime->ownsCoverageSource()) {
      runtime->frozenViewRequest = {};
      runtime->coverageSource.beginRelease();
      runtime->coveragePreparation = LoopPlaybackRuntime::CoveragePreparation::Retiring;
    }
  } else if (result == RuntimePlaybackViewBuildGrain::Denied && runtime->ownsCoverageSource()) {
    runtime->retireCoverageSource();
  }
  const PlaybackViewTurnResult turnResult = finishPlaybackViewGrain(slot, result);
  if (turnResult == PlaybackViewTurnResult::GrainDenied || turnResult == PlaybackViewTurnResult::BuildComplete) {
    // DEC-108 §13b / task 3.9: the **readiness receipt**, emitted where the outcome is reached -- a denied grain
    // (whose holder now owes the work) and a completion (whose window is ready) are the two events worth a row, not
    // every grain. Cadence-bounded inside the reporter, so it cannot evict what it explains.
    reportPlaybackWindowReadinessReceipt(slot, currentPhase);
  }
  return turnResult;
}

TRACK_COLD_MEM bool Track::playbackWindowInterval(uint32_t loopLengthTicks, bool demoted,
                                                  uint32_t playheadPhase, PlaybackCoverageInterval& out) {
  if (loopLengthTicks == 0) {
    return false;
  }
  if (!demoted) {
    // Whole-loop coverage: the interval is the loop the playhead is in.
    out = PlaybackCoverageInterval{0, loopLengthTicks, loopLengthTicks};
    return true;
  }
  // Two bars at the playhead, clamped into the loop -- the same shape the live build uses, kept identical so
  // the windowed representation is one policy rather than two.
  constexpr uint32_t kWindowBars = 2;
  uint32_t length = kWindowBars * Config::TICKS_PER_BAR;
  if (length > loopLengthTicks) {
    length = loopLengthTicks;
  }
  const uint32_t phase = playheadPhase % loopLengthTicks;
  uint32_t start = phase > (length / 2) ? phase - (length / 2) : 0;
  if (start + length > loopLengthTicks) {
    start = loopLengthTicks - length;
  }
  out = PlaybackCoverageInterval{start, length, loopLengthTicks};
  return true;
}

TRACK_COLD_MEM Track::PlaybackViewRequestResult Track::requestPlaybackWindow(uint8_t slotIndex,
                                                                           uint32_t playheadPhase,
                                                                           SessionMidiEventVec& scratch, PlaybackDeadlineClock clock) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return PlaybackViewRequestResult::NoRuntime;
  }
  ensureLoopsAllocated();
  Loop& loop = loopForSlot(slotIndex);
  if (!loop.hasCommittedPasses() || loop.loopLengthTicks == 0) {
    return PlaybackViewRequestResult::NotServable;
  }
  PlaybackCoverageInterval coverage{};
  if (!playbackWindowInterval(loop.loopLengthTicks, loop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks), playheadPhase,
                              coverage)) {
    return PlaybackViewRequestResult::NotServable;
  }
  const uint32_t phase = playheadPhase % loop.loopLengthTicks;
  const PlaybackViewSourceIdentity identity{loop.playbackRevision, playbackGeneration,
                                            loop.publishedSnapshotIdentity()};
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  const RuntimePlaybackViewSet* set = runtime != nullptr ? runtime->viewSetIfPrewarmed() : nullptr;
  const bool successorWaiting = set != nullptr &&
      set->lifecycle.ready < RuntimePlaybackViewConfig::kViewCount &&
      set->views[set->lifecycle.ready].complete;
  if (runtime != nullptr &&
      (runtime->viewBuild.active() || runtime->ownsCoverageSource() || runtime->completedPromisePendingJudgement || successorWaiting)) {
    // The current builder may still borrow scratch. Refuse BEFORE clear/gather,
    // using the admission owner's existing demand-retention policy. No source
    // has been enumerated: pending cost is the maximum admissible window cost,
    // not an observed count. A later attempt must gather and price its real work.
    return requestPlaybackViewMaintenance(slotIndex, nullptr,
        RuntimePlaybackViewConfig::kWindowAdmissionLimit, identity, coverage, {}, {},
        phase, coverage.startPhase, clock);
  }
  // Enumerate through the **effective-content resolver**: the selected `LoopSnapshot`'s window membership,
  // never raw committed chunks (spec *One pass late is the only stale authority*). Only the `Snapshot` branch is
  // the *current* published root; an `Overlay` carries unpublished session/pending passes and a rootless slot
  // has no content to resolve, so both refuse by name instead of serving stale content as current.
  Loop::SnapshotWindowGatherPath path = Loop::SnapshotWindowGatherPath::None;
  uint32_t inspectedEntries = 0;
  scratch.clear();
  if (!loop.tryGatherPublishedSnapshotEventsInWindow(scratch, coverage.startPhase, coverage.lengthTicks, &path,
                                                     &inspectedEntries)) {
    return PlaybackViewRequestResult::SourceUnavailable;
  }
  if (path != Loop::SnapshotWindowGatherPath::Snapshot) {
    return PlaybackViewRequestResult::SourceNotCurrent;
  }
  // The handoff is the phase at which the playhead next *enters this interval* -- "readiness is owed per affected
  // tick, before the playhead returns to it" -- so the distance is the rest of the loop plus the interval's own
  // start. `canCompleteBeforeHandoff` decides whether the declared contract can make it.
  return requestPlaybackViewMaintenance(slotIndex, scratch.data(), scratch.size(), identity, coverage, {}, {},
                                       phase, coverage.startPhase, clock);
}

TRACK_COLD_MEM Track::PlaybackViewRequestResult Track::requestPreviewWindow(uint8_t slotIndex,
                                                                           uint32_t previewRevision,
                                                                           const MidiEvent* source,
                                                                           size_t sourceCount,
                                                                           uint32_t currentPhase, PlaybackDeadlineClock clock) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return PlaybackViewRequestResult::NoRuntime;
  }
  ensureLoopsAllocated();
  if (source == nullptr || sourceCount == 0) {
    // A preview with no content is not a preview: publishing emptiness as preview would replace a served preview with
    // a window that claims "no events" for content the session is editing.
    return PlaybackViewRequestResult::NotServable;
  }
  Loop& loop = loopForSlot(slotIndex);
  if (loop.loopLengthTicks == 0) {
    return PlaybackViewRequestResult::NotServable;
  }
  // Preview covers the **whole loop**: the session's projection is the loop's edited state, and a session is not an
  // interval. `playbackWindowInterval` states that shape once (whole-loop coverage), so the preview lane does not
  // invent a second geometry rule.
  PlaybackCoverageInterval coverage{};
  if (!playbackWindowInterval(loop.loopLengthTicks, false, 0, coverage)) {
    return PlaybackViewRequestResult::NotServable;
  }
  // The **frozen source**: preview currency is the session's preview revision, and the origin marks the contract. The
  // snapshot identity is deliberately null -- this content has no published committed root, and the origin field is
  // what stops it from passing as committed currency.
  const PlaybackViewSourceIdentity identity{previewRevision, playbackGeneration, nullptr,
                                            PlaybackWindowOrigin::Preview};
  const uint32_t phase = currentPhase % loop.loopLengthTicks;
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  const PlaybackViewRequestResult result = requestPlaybackViewMaintenance(
      slotIndex, source, sourceCount, identity, coverage, {}, {}, phase, coverage.startPhase, clock);
  if (result == PlaybackViewRequestResult::Admitted && runtime != nullptr) {
    // The lane owns this build from here: the revision is its freeze token, and the flag is how a cancellation tells
    // the lane's own work from a committed build's.
    runtime->previewSourceRevision = previewRevision;
    runtime->previewSourceActive = true;
    runtime->previewHandoffOwed = false;
  }
  return result;
}

TRACK_COLD_MEM Track::PreviewCancelOutcome Track::cancelPreviewWindowForSlot(
    uint8_t slotIndex, PreviewWindowCancelReason reason) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || !loopsAllocated()) {
    return PreviewCancelOutcome::NothingToCancel;
  }
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return PreviewCancelOutcome::NothingToCancel;
  }
  if (!runtime->previewSourceActive) {
    // Nothing of the lane's own. A **repeated exit** is named rather than treated as a second handoff, so a caller
    // cannot make the committed lane re-request by repeating itself.
    if (reason == PreviewWindowCancelReason::SessionExited && runtime->previewHandoffOwed) {
      return PreviewCancelOutcome::AlreadyHandedOff;
    }
    return PreviewCancelOutcome::NothingToCancel;
  }
  // Only the preview lane's own in-flight build is cancelled, and only through the shared-view row that states the
  // shape: `ClearOrLoad` releases **only** the candidate's resources, retires a prepared successor and invalidates the
  // operation identity, while the served window stays readable (the invariant every row carries).
  const bool laneOwnsInFlightBuild =
      runtime->coverageBuildNeedsService() || runtime->completedPromisePendingJudgement;
  if (laneOwnsInFlightBuild) {
    applyPlaybackViewTransition(slotIndex, PlaybackViewTransition::ClearOrLoad);
  }
  runtime->previewSourceActive = false;
  runtime->previewSourceRevision = 0;
  if (reason == PreviewWindowCancelReason::SessionExited) {
    // The exit handoff: the preview source yields and the committed successor is owed. The preview window that is
    // serving right now keeps serving -- the handoff is a boundary, not a gap -- until a committed successor is
    // adopted.
    runtime->previewHandoffOwed = true;
    return laneOwnsInFlightBuild ? PreviewCancelOutcome::BuildCancelledAndHandoffOwed
                                 : PreviewCancelOutcome::HandoffOwed;
  }
  // A mutated or re-focused preview source: the in-flight build is cancelled above, and the served preview window is
  // left untouched -- it is stale **by its own revision**, which is what makes "stale preview is not served as current"
  // a currency answer rather than a teardown.
  return laneOwnsInFlightBuild ? PreviewCancelOutcome::BuildCancelled
                               : PreviewCancelOutcome::NothingToCancel;
}

TRACK_COLD_MEM bool Track::servedPlaybackWindowIsPreviewForSlot(uint8_t slotIndex) const {
  PlaybackWindowIdentity identity{};
  return publishedPlaybackWindowIdentityForSlot(slotIndex, identity) &&
         identity.origin == PlaybackWindowOrigin::Preview;
}

TRACK_COLD_MEM bool Track::previewWindowHandoffOwedForSlot(uint8_t slotIndex) const {
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  return runtime != nullptr && runtime->previewHandoffOwed;
}
TRACK_COLD_MEM bool Track::publishedPlaybackWindowIdentityForSlot(uint8_t slotIndex,
                                                                  PlaybackWindowIdentity& out) const {
  PlaybackViewSourceIdentity sourceIdentity{};
  PlaybackCoverageInterval coverage{};
  if (!publishedPlaybackWindowForSlot(slotIndex, sourceIdentity, coverage)) {
    return false;
  }
  out.slot = slotIndex;
  out.playbackGeneration = sourceIdentity.sourceGeneration;
  out.playbackRevision = sourceIdentity.playbackRevision;
  out.interval = coverage;
  // Task 3.13: the window's origin comes from the **source identity** it was built from, so a preview window is
  // identified as preview by construction rather than by a flag a caller has to remember to set.
  out.origin = sourceIdentity.origin;
  return true;
}

TRACK_COLD_MEM bool Track::adoptPreparedPlaybackWindow(uint8_t slotIndex) {
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return false;
  }
  RuntimePlaybackViewSet* set = runtime->viewSetIfPrewarmed();
  if (set == nullptr) {
    return false;
  }
  // The successor lane's adoption: one atomic handoff (`RuntimePlaybackViewLifecycle::handoff`), prepared-mode
  // because a window carries no audible-launch semantics of its own. It refuses unless a **complete** successor
  // is waiting, so a caller can poll it safely.
  if (!set->handoffReadyCoverage(PlaybackViewHandoffMode::Preparation)) {
    return false;
  }
  // Task 3.13: the handoff completes **here**, at the adoption boundary -- the preview window served up to this point
  // and the committed successor serves from it. Only a committed successor discharges the obligation; adopting another
  // preview window (a re-focus back into a session) leaves it owed.
  {
    const RuntimePlaybackView& served = set->views[set->lifecycle.current];
    if (served.source.origin == PlaybackWindowOrigin::Committed) {
      runtime->previewHandoffOwed = false;
    }
  }
  return true;
}

TRACK_COLD_MEM bool Track::revalidateCompletedPlaybackWindow(
    uint8_t slotIndex, uint32_t currentPhase, PlaybackDeadlineClock clock) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || clock.epoch == 0) return false;
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr || !runtime->completedPromisePendingJudgement ||
      runtime->coverageBuildNeedsService()) return false;
  RuntimePlaybackViewSet* set = runtime->viewSetIfPrewarmed();
  if (set == nullptr || set->lifecycle.build >= RuntimePlaybackViewConfig::kViewCount ||
      runtime->viewBuildOperationGeneration != playbackGeneration) return false;
  const RuntimePlaybackView& candidate = set->views[set->lifecycle.build];
  const Loop& loop = loopForSlot(slotIndex);
  const auto& work = runtime->viewBuildWork;
  if (!candidate.complete || !candidate.coverage.isValid() ||
      loop.loopLengthTicks != candidate.coverage.loopLengthTicks ||
      loop.loopLengthTicks != work.admittedLoopLength ||
      currentPhase >= loop.loopLengthTicks || currentPhase != candidate.coverage.startPhase) return false;
  if (candidate.source.origin == PlaybackWindowOrigin::Committed) {
    if (!loop.publishedSnapshotIsCurrent() || candidate.source.playbackRevision != loop.playbackRevision ||
        candidate.source.snapshotIdentity != loop.publishedSnapshotIdentity()) return false;
  } else if (!runtime->previewSourceActive ||
             candidate.source.playbackRevision != runtime->previewSourceRevision) {
    return false;
  }
  // In the original epoch a missed traversal remains fixed to its old revision.
  // Recovery may only use a following entry, never the just-missed one. A new
  // epoch has no positional relationship: the caller supplies its boundary phase.
  if (clock.epoch == work.clockEpoch) {
    if (runtime->admittedPromiseInvalidated || clock.tick < work.admittedTick ||
        work.deadlineTick > UINT64_MAX - work.admittedLoopLength ||
        clock.tick < work.deadlineTick + work.admittedLoopLength ||
        (clock.tick - work.deadlineTick) % work.admittedLoopLength != 0) return false;
  }
  runtime->completedPromisePendingJudgement = false;
  if (!publishCompletedPlaybackWindow(slotIndex)) {
    runtime->completedPromisePendingJudgement = true;
    return false;
  }
  // Recovery does not erase fault history; the boundary consumer owns adoption
  // and clearing a recovered episode. The admitted work record remains unchanged.
  return true;
}

TRACK_COLD_MEM bool Track::publishCompletedPlaybackWindow(uint8_t slotIndex) {
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr || runtime->completedPromisePendingJudgement) {
    return false;
  }
  RuntimePlaybackViewSet* set = runtime->viewSetIfPrewarmed();
  if (set == nullptr) {
    return false;
  }
  // Task 3.11 (DEC-108 §13d): a **late completion publishes nothing**. The build carries the operation generation it
  // was started under, so a completion arriving after that identity moved is refused here -- and it releases only its
  // own candidate: the served window, the prepared-successor role and the retained source are other owners' state.
  const uint8_t completedBuildRole = set->lifecycle.build;
  if (completedBuildRole < RuntimePlaybackViewConfig::kViewCount &&
      runtime->viewBuildOperationGeneration != playbackGeneration) {
    runtime->viewBuild.discard();
    set->views[completedBuildRole].resetMetadata();
    runtime->viewBuildOperationGeneration = 0;
    return false;
  }
  PlaybackWindowIdentity published{};
  if (publishedPlaybackWindowIdentityForSlot(slotIndex, published)) {
    // A window is being served: this completed build becomes its **ready successor**, and the served window is
    // left exactly as it was -- the replacement's swap is `adoptPreparedPlaybackWindow`'s single handoff, at an
    // eligible boundary (task 3.7). That is F-1's "a consumer read cannot straddle a replacement".
    return set->lifecycle.publishCompletedBuildAsReadySuccessor(set->views);
  }
  if (!runtime->previewSourceActive) {
    // Task 3.13: with **no** window being served, this committed publication becomes the served window itself, so the
    // preview exit handoff is discharged here. (When a preview window *is* served, the discharge belongs to the
    // adoption boundary instead: the preview window keeps serving until the committed successor replaces it.)
    runtime->previewHandoffOwed = false;
  }
  return set->lifecycle.publishCompletedBuildAsCurrentWindow(set->views);
}

namespace {
/// A bounded presence set for the incoming revision's note-ons: 16 channels x 128 notes as bits (256 B), so the ledger
/// walk below is O(open notes) with no search. A byte table would be 2 KB on a frame this repo also keeps small.
inline void notePresentSet(uint32_t (&bits)[16][4], uint8_t channel, uint8_t note) {
  if (channel == 0 || channel > 16 || note > 127) {
    return;
  }
  bits[channel - 1][note / 32] |= uint32_t(1) << (note % 32);
}
inline bool notePresentGet(const uint32_t (&bits)[16][4], uint8_t channel, uint8_t note) {
  if (channel == 0 || channel > 16 || note > 127) {
    return false;
  }
  return (bits[channel - 1][note / 32] & (uint32_t(1) << (note % 32))) != 0;
}
}  // namespace

TRACK_COLD_MEM ClosureSettlement Track::planClosureSettlementForSlot(uint8_t slotIndex, const MidiEvent* events,
                                                                    size_t eventCount,
                                                                    bool incomingIsWholeLoop) const {
  ClosureSettlement settlement;
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return settlement;
  }
  uint32_t present[16][4] = {};
  for (size_t i = 0; i < eventCount; ++i) {
    const MidiEvent& event = events[i];
    if (!event.isNoteOn()) {
      continue;
    }
    notePresentSet(present, event.channel, event.data.noteData.note);
  }
  settlement.eventEmptyWindow = eventCount == 0;
  const bool absenceMeansRemoval = handoffAbsenceMeansRemoval(incomingIsWholeLoop, settlement.eventEmptyWindow);
  runtime->ledger.forEachActive([&](uint8_t channel, uint8_t note, const ActiveNoteLedger::Entry&) {
    const uint16_t key = static_cast<uint16_t>((static_cast<uint16_t>(channel) << 8) | note);
    if (notePresentGet(present, channel, note)) {
      // The replacement re-issues this sounding note: its note-on must be suppressed, and the note must NOT be closed --
      // an unchanged note is neither retriggered nor silenced.
      if (settlement.suppressedRetriggerCount < ClosureSettlement::kMaxKeys) {
        settlement.suppressedRetriggers[settlement.suppressedRetriggerCount++] = key;
      }
      return;
    }
    if (absenceMeansRemoval) {
      if (settlement.closureCount < ClosureSettlement::kMaxKeys) {
        settlement.closures[settlement.closureCount++] = key;
      }
      return;
    }
    // Absence is not evidence here (a slice, or an empty window): the note is sustained across the handoff and is left
    // exactly as it is -- no closure and no retrigger.
    ++settlement.preservedCount;
  });
  return settlement;
}

TRACK_COLD_MEM size_t Track::applyClosureSettlementForSlot(uint8_t slotIndex, const ClosureSettlement& settlement) {
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return 0;
  }
  size_t emitted = 0;
  // Closures **first**, and only closures: the replacement's own note-ons are the caller's to emit after this returns,
  // which is the spec's "required note-offs SHALL be emitted before the replacement's note-ons" expressed as a calling
  // order rather than as a promise. The ledger is discharged through its own event path, so the obligation leaves the
  // same way it would have on the emission path. A preserved note is never touched.
  for (uint16_t i = 0; i < settlement.closureCount; ++i) {
    const uint16_t key = settlement.closures[i];
    const uint8_t channel = static_cast<uint8_t>(key >> 8);
    const uint8_t note = static_cast<uint8_t>(key & 0xFFu);
    const MidiEvent close = MidiEvent::NoteOff(0, channel, note, 0);
    midiHandler.sendMidiEvent(close);
    (void)runtime->ledger.applyPlaybackEvent(channel, close);
    ++emitted;
  }
  return emitted;
}

namespace {
/// The settlement core, shared by every identity change: classify each sounding note against the affected **presence
/// set** and write the plan. One walk of the ledger, O(open notes); the caller has already bounded the presence set.
ClosureSettlement settleLedgerAgainstPresence(const ActiveNoteLedger& ledger, const NotePresenceSet& affected,
                                              bool identitiesAreRemoved) {
  ClosureSettlement settlement;
  ledger.forEachActive([&](uint8_t channel, uint8_t note, const ActiveNoteLedger::Entry&) {
    if (!affected.has(channel, note)) {
      // A sounding note the change does not name is **preserved**: no early closure, no retrigger.
      ++settlement.preservedCount;
      return;
    }
    const uint16_t key = static_cast<uint16_t>((static_cast<uint16_t>(channel) << 8) | note);
    if (identitiesAreRemoved) {
      if (settlement.closureCount < ClosureSettlement::kMaxKeys) {
        settlement.closures[settlement.closureCount++] = key;
      }
      return;
    }
    // The identities were re-established while the note never stopped sounding: its note-on is suppressed rather than
    // retriggered, and it is not closed.
    if (settlement.suppressedRetriggerCount < ClosureSettlement::kMaxKeys) {
      settlement.suppressedRetriggers[settlement.suppressedRetriggerCount++] = key;
    }
  });
  return settlement;
}

/// Collect one committed pass's NoteOn identities from its **own bodies**, a row at a time: bounded by the pass's chunk
/// references, allocation-free. A disabled pass still holds its references (the timeline keeps them so a redo can
/// re-enable the pass), which is what lets an identity removal be settled against them at all.
void collectCommittedPassIdentities(const Loop& loop, PassId passId, NotePresenceSet& out) {
  const CommittedChunkIdList* refs = nullptr;
  if (loop.passes.recordPass.id == passId) {
    refs = &loop.passes.recordPass.committedChunkIds;
  } else {
    for (const OverdubPass& pass : loop.passes.overdubPasses) {
      if (pass.id == passId) {
        refs = &pass.committedChunkIds;
        break;
      }
    }
  }
  if (refs == nullptr) {
    return;
  }
  for (const uint16_t chunkId : *refs) {
    const uint16_t used = LoopEventStore::chunkUsedCount(chunkId);
    for (uint16_t index = 0; index < used; ++index) {
      MidiEvent event;
      bool atEnd = false;
      if (!LoopEventStore::readSealedChunkEvent(chunkId, index, event, atEnd) || atEnd) {
        break;
      }
      if (event.isNoteOn()) {
        out.add(event.channel, event.data.noteData.note);
      }
    }
  }
}
}  // namespace

TRACK_COLD_MEM ClosureSettlement Track::reconcileAfterCommittedPassStateChange(uint8_t slotIndex,
                                                                              const PassId* passIds, size_t passCount,
                                                                              bool identitiesAreRemoved) {
  ClosureSettlement settlement;
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return settlement;
  }
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    // No window lane on this slot: nothing to settle -- and nothing silenced.
    return settlement;
  }
  const Loop& loop = loopForSlot(slotIndex);
  NotePresenceSet affected;
  for (size_t i = 0; i < passCount; ++i) {
    if (passIds[i] != kInvalidPassId) {
      collectCommittedPassIdentities(loop, passIds[i], affected);
    }
  }
  settlement = settleLedgerAgainstPresence(runtime->ledger, affected, identitiesAreRemoved);
  (void)applyClosureSettlementForSlot(slotIndex, settlement);
  return settlement;
}

TRACK_COLD_MEM ClosureSettlement Track::reconcileAfterEditPassStateChange(uint8_t slotIndex,
                                                                          const EditPassIdList& passIds,
                                                                          bool identitiesAreRemoved) {
  ClosureSettlement settlement;
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return settlement;
  }
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    // No window lane on this slot: there is nothing to settle here -- and, the point of this task, **nothing is
    // silenced**. The blanket emitter was the only thing that acted in this case, and acting was the defect.
    return settlement;
  }
  const Loop& loop = loopForSlot(slotIndex);
  // The affected identities come from the edit passes' **own added events** (an edit pass carries no chunk bodies),
  // collected in one bounded pass; the presence set is 256 B and nothing allocates.
  NotePresenceSet affected;
  for (const EditPassId id : passIds) {
    for (const EditPass& pass : loop.passes.editPasses) {
      if (pass.id != id) {
        continue;
      }
      for (const MidiEvent& event : pass.addedEvents) {
        if (event.isNoteOn()) {
          affected.add(event.channel, event.data.noteData.note);
        }
      }
      break;
    }
  }
  settlement = settleLedgerAgainstPresence(runtime->ledger, affected, identitiesAreRemoved);
  (void)applyClosureSettlementForSlot(slotIndex, settlement);
  return settlement;
}

TRACK_COLD_MEM bool Track::publishedPlaybackWindowForSlot(uint8_t slotIndex,
                                                         PlaybackViewSourceIdentity& identityOut,
                                                         PlaybackCoverageInterval& coverageOut) const {
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return false;
  }
  const RuntimePlaybackViewSet* set = runtime->viewSetIfPrewarmed();
  if (set == nullptr) {
    return false;
  }
  const uint8_t current = set->lifecycle.current;
  if (current >= RuntimePlaybackViewConfig::kViewCount) {
    return false;
  }
  const RuntimePlaybackView& view = set->views[current];
  // Only a **completed, published** window with a valid interval is an answer: a shell view is not a window.
  if (!view.complete || !view.published || !view.coverage.isValid()) {
    return false;
  }
  identityOut = view.source;
  coverageOut = view.coverage;
  return true;
}

TRACK_COLD_MEM bool Track::playbackWindowEventAtForSlot(uint8_t slotIndex, uint16_t cursor,
                                                       MidiEvent& out) const {
  // Task 4.3: the served window's stream, in **emission order** -- the same rendering `makeFixedPlaybackStream`
  // feeds the emission path (`events[order[cursor]]`), so a parity proof reads what a consumer would play rather
  // than a differently-ordered view of it. The window must be the *served* one (complete, published, valid
  // interval): a prepared successor is deliberately not readable through this door.
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return false;
  }
  const RuntimePlaybackViewSet* set = runtime->viewSetIfPrewarmed();
  if (set == nullptr) {
    return false;
  }
  const uint8_t current = set->lifecycle.current;
  if (current >= RuntimePlaybackViewConfig::kViewCount) {
    return false;
  }
  const RuntimePlaybackView& view = set->views[current];
  if (!view.complete || !view.published || !view.coverage.isValid()) {
    return false;
  }
  const FixedPlaybackEventStorage& storage = view.storage;
  if (storage.events == nullptr || storage.order == nullptr || cursor >= storage.eventCount) {
    return false;
  }
  out = storage.events[storage.order[cursor]];
  return true;
}

TRACK_COLD_MEM Track::PlaybackViewTurnResult Track::finishPlaybackViewGrain(
    uint8_t slotIndex, RuntimePlaybackViewBuildGrain grain) {
  Loop& loop = loopForSlot(slotIndex);
  // If/else rather than a `switch`, for the same `.data` reason as the name tables above.
  if (grain == RuntimePlaybackViewBuildGrain::Denied) {
    // Non-destructive: the build stopped where it was, nothing was published, and the requesting owner
    // re-requests (task 3.2). Reported by name -- an unnamed denial is not an answer.
    reportPlaybackViewMaintenanceRow(slotIndex, loop, "grain_denied");
    return PlaybackViewTurnResult::GrainDenied;
  }
  if (grain == RuntimePlaybackViewBuildGrain::Complete) {
    reportPlaybackViewMaintenanceRow(slotIndex, loop, "build_complete");
    return PlaybackViewTurnResult::BuildComplete;
  }
  if (grain == RuntimePlaybackViewBuildGrain::Fill || grain == RuntimePlaybackViewBuildGrain::Order) {
    return PlaybackViewTurnResult::GrainServed;
  }
  return PlaybackViewTurnResult::Idle;
}

TRACK_COLD_MEM void Track::reportPlaybackViewMaintenanceRow(uint8_t slotIndex, Loop& loop,
                                                            const char* path) {
  // The lane's one row vehicle, reached through the substitutable seam (`SC_PLAYBACK_BUILD_ROW`): byte-for-byte the row a
  // capture build emits, and one a native suite can observe -- which is what makes a claim about *one of these rows*
  // (rather than about the state beside it) checkable. The `#if defined(SESSION_CAPTURE)` guard this used to carry now
  // lives inside the macro, which is `((void)0)` without a capture build; the casts below keep the parameters used in
  // that build too.
  (void)slotIndex;
  (void)loop;
  (void)path;
  SC_PLAYBACK_BUILD_ROW(playbackRebuildTrackIndex(*this), playbackRebuildSlotIndex(*this, loop), loop.loopLengthTicks,
                        loop.playbackRevision, path);
}

namespace {

/// The launch gate row's **last emitted state** for one (track, slot) target: the row is a *transition* record, so a
/// pending switch that sits on the same exit for hundreds of deferred turns costs one row while every move is still
/// named (a row per turn would evict the evidence it explains -- the reason `TelemetryCadence` exists for the readiness
/// receipt). `DMAMEM` on the target is RAM2: off-clock state, read by the deferred-idle turn alone, never DTCM.
struct LaunchGateLastState {
  const char* exit = nullptr;
  uint32_t loopLengthTicks = 0;
  uint16_t flags = 0;
  uint8_t launchPreparation = 0;
};

constexpr size_t kLaunchGateSlotCount = Config::NUM_TRACKS * Config::MAX_LOOPS_PER_TRACK;
#if defined(__IMXRT1062__)
DMAMEM LaunchGateLastState sLaunchGateLast[kLaunchGateSlotCount];
#else
LaunchGateLastState sLaunchGateLast[kLaunchGateSlotCount];
#endif

}  // namespace

TRACK_COLD_MEM void Track::reportPlaybackLaunchGateRow(uint8_t slotIndex, const char* exit, bool sourceRequestAttempted,
                                                      bool sourceRequestAccepted) {
  // The launch path's **state-change record**, assembled from this target slot's own state so that every otherwise
  // silent exit of `preparePlaybackLaunchForSlot` (and the decisions its caller takes before reaching it) names itself
  // once. Nothing here admits, refuses, retires or reorders: the guards above have already decided, and this reads the
  // same state they read. Everything below is **qualification-only** (`SC_QUALIFICATION_ROWS_ENABLED`: a capture build that
  // opens the rolling lane's gate, or a native suite) -- the other images carry neither the row nor this table.
#if SC_QUALIFICATION_ROWS_ENABLED
  if (exit == nullptr || slotIndex >= Config::MAX_LOOPS_PER_TRACK) return;
  const Loop& loop = loopForSlot(slotIndex);
  const auto* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  uint16_t flags = 0;
  if (loop.publishedSnapshotIsCurrent()) flags |= 1u << 0;
  if (loop.hasPendingSnapshotPublish()) flags |= 1u << 1;
  if (loop.hasCommittedPasses()) flags |= 1u << 9;
  if (loop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks)) flags |= 1u << 8;
  uint8_t preparation = 0;
  if (runtime) {
    if (runtime->coverageRoleAvailable()) flags |= 1u << 2;
    if (runtime->ownsCoverageSource()) flags |= 1u << 3;
    if (runtime->pendingViewRequest_.active) flags |= 1u << 4;
    if (runtime->viewBuild.active()) flags |= 1u << 5;
    if (runtime->viewBuild.coverageOwned()) flags |= 1u << 6;
    if (runtime->completedPromisePendingJudgement) flags |= 1u << 7;
    preparation = static_cast<uint8_t>(runtime->launchPreparation);
  }
  if (sourceRequestAttempted) flags |= 1u << 10;
  if (sourceRequestAccepted) flags |= 1u << 11;
  const uint8_t trackIndex = playbackRebuildTrackIndex(*this);
  LaunchGateLastState& last = sLaunchGateLast[static_cast<size_t>(trackIndex) * Config::MAX_LOOPS_PER_TRACK + slotIndex];
  // The token is compared by **content**, not by pointer: the same exit is named from more than one place, and literal
  // merging is not a contract. Bounded, off-clock work on a deferred turn.
  if (last.exit != nullptr && std::strcmp(last.exit, exit) == 0 && last.loopLengthTicks == loop.loopLengthTicks &&
      last.flags == flags && last.launchPreparation == preparation) {
    return;
  }
  last.exit = exit;
  last.loopLengthTicks = loop.loopLengthTicks;
  last.flags = flags;
  last.launchPreparation = preparation;
  SC_LAUNCH_GATE(trackIndex, slotIndex, exit, flags, loop.loopLengthTicks, preparation);
#else
  (void)slotIndex;
  (void)exit;
  (void)sourceRequestAttempted;
  (void)sourceRequestAccepted;
#endif
}
TRACK_COLD_MEM uint8_t Track::playbackSlotIndexForLoop(const Loop& loop) const {
  return playbackRebuildSlotIndex(*this, loop);
}
namespace {

/// The `Snapshot` owner's gate row, **last emitted state** per track: the packed verdicts, the selected and active slot
/// and the unit allowance. The row is a *transition* record -- a Snapshot turn comes around every
/// `kDeferredIdleOwnerCount` turns and a track's gate tuple rarely moves, so an unconditional row would be a flood --
/// and comparing the packed tuple is exact, with no formatting. `DMAMEM` is RAM2: off-clock state read by the
/// deferred-idle turn alone, never DTCM.
struct SnapshotGateLastState {
  uint16_t verdicts = 0;
  uint8_t selectedSlot = 0;
  uint8_t activeSlot = 0;
  uint8_t constructionUnits = 0;
  bool seen = false;
};

constexpr size_t kSnapshotGateTrackCount = Config::NUM_TRACKS;
#if defined(__IMXRT1062__)
DMAMEM SnapshotGateLastState sSnapshotGateLast[kSnapshotGateTrackCount];
#else
SnapshotGateLastState sSnapshotGateLast[kSnapshotGateTrackCount];
#endif

/// The row's bit order, in one place so the caller and the emitter cannot drift (the emitter prints one named field per
/// bit; the list is in the capture header's declaration).
uint16_t snapshotGateVerdicts(bool hasPendingPublish, bool sessionExitQueued, bool inFlightConstruction,
                              bool wrapJournal, bool havePublishedSnapshot, bool liveRecordPastFirstBar,
                              bool visualCacheNeedsFirstPaint, bool recording, bool playing, bool overdubbing,
                              bool stepAdmits, bool clauseAdmits, bool idleAllowed, bool eligible) {
  uint16_t bits = 0;
  if (hasPendingPublish) bits |= 1u << 0;
  if (sessionExitQueued) bits |= 1u << 1;
  if (inFlightConstruction) bits |= 1u << 2;
  if (wrapJournal) bits |= 1u << 3;
  if (havePublishedSnapshot) bits |= 1u << 4;
  if (liveRecordPastFirstBar) bits |= 1u << 5;
  if (visualCacheNeedsFirstPaint) bits |= 1u << 6;
  if (recording) bits |= 1u << 7;
  if (playing) bits |= 1u << 8;
  if (overdubbing) bits |= 1u << 9;
  if (stepAdmits) bits |= 1u << 10;
  if (clauseAdmits) bits |= 1u << 11;
  if (idleAllowed) bits |= 1u << 12;
  if (eligible) bits |= 1u << 13;
  return bits;
}

}  // namespace

TRACK_COLD_MEM void Track::reportSnapshotOwnerGateRow(uint8_t selectedSlot, uint8_t activeSlot,
                                                      uint8_t constructionUnits, bool liveRecordPastFirstBar,
                                                      bool visualCacheNeedsFirstPaint, bool stepAdmits, bool clauseAdmits,
                                                      bool idleAllowed, bool eligible) {
  // The selected loop's own pending terms are read here rather than passed, so the call site cannot disagree with the
  // loop it named. Everything below is **qualification-only** (`SC_QUALIFICATION_ROWS_ENABLED`); other images carry
  // neither the row nor this table.
#if SC_QUALIFICATION_ROWS_ENABLED
  const uint8_t trackIndex = playbackRebuildTrackIndex(*this);
  if (trackIndex >= kSnapshotGateTrackCount || selectedSlot >= Config::MAX_LOOPS_PER_TRACK) return;
  const Loop& selected = loopForSlot(selectedSlot);
  const uint16_t verdicts =
      snapshotGateVerdicts(selected.hasPendingSnapshotPublish(), selected.pendingSnapshotSessionExitQueued(),
                           selected.hasInFlightSnapshotConstruction(), selected.hasPendingWrapJournal(),
                           static_cast<bool>(selected.publishedSnapshot()), liveRecordPastFirstBar,
                           visualCacheNeedsFirstPaint, isRecording(), isPlaying(), isOverdubbing(), stepAdmits,
                           clauseAdmits, idleAllowed, eligible);
  SnapshotGateLastState& last = sSnapshotGateLast[trackIndex];
  if (last.seen && last.verdicts == verdicts && last.selectedSlot == selectedSlot && last.activeSlot == activeSlot &&
      last.constructionUnits == constructionUnits) {
    return;
  }
  last.verdicts = verdicts;
  last.selectedSlot = selectedSlot;
  last.activeSlot = activeSlot;
  last.constructionUnits = constructionUnits;
  last.seen = true;
  SC_SNAPSHOT_GATE(trackIndex, selectedSlot, activeSlot, verdicts, constructionUnits);
#else
  (void)selectedSlot;
  (void)activeSlot;
  (void)constructionUnits;
  (void)liveRecordPastFirstBar;
  (void)visualCacheNeedsFirstPaint;
  (void)stepAdmits;
  (void)clauseAdmits;
  (void)idleAllowed;
  (void)eligible;
#endif
}
TRACK_COLD_MEM void Track::reportPlaybackLaunchCallerExit(uint8_t slotIndex, PlaybackLaunchCallerExit exit) {
  // The caller's three exits, named here rather than at the call site: this TU's string literals are routed to FLASH by
  // the linker script, while the slot matrix's are not -- three tokens added there were enough to cross the 4 KB
  // `ALIGN(1024)` edge (measured: `variables` 96,192 -> 100,288 with the ITCM `code` figure byte-identical). An if/else
  // chain and no table, for the `.data` reason the readiness names below record.
  if (exit == PlaybackLaunchCallerExit::ProvisionRefused) {
    reportPlaybackLaunchGateRow(slotIndex, "launch_provision_refused");
    return;
  }
  if (exit == PlaybackLaunchCallerExit::NotReachedLegacy) {
    reportPlaybackLaunchGateRow(slotIndex, "launch_not_reached_legacy");
    return;
  }
  reportPlaybackLaunchGateRow(slotIndex, "launch_not_reached_occupied");
}

namespace {

/// The rolling demand path's gate row, **last emitted state** per track: the packed flags, the active slot, the length
/// the deciding terms read and the request's own ordinal. Same shape and same reasoning as the launch gate's table --
/// the row is a *transition* record, and the lane turn visits **every** track on every
/// `DeferredIdleOwnerCount` rotation, so an unconditional row would be a flood that evicts the evidence it explains.
/// `bit 14` (outstanding grains) is printed as context but is **not** part of the change key: it is a progress counter,
/// not a decision, and keying on it would turn one state into a row per grain. `DMAMEM` is RAM2: off-clock state read
/// by the lane turn alone, never DTCM.
struct RollingGateLastState {
  uint16_t key = 0;
  uint8_t slot = 0;
  uint32_t loopLengthTicks = 0;
  uint8_t requestResult = 0;
  const char* exit = nullptr;
  bool seen = false;
};

constexpr size_t kRollingGateTrackCount = Config::NUM_TRACKS;
/// The deciding bits of `sRollingGateLast.key`; see the header's declaration for the bit order.
constexpr uint16_t kRollingGateChangeKeyMask = 0xFFBFu;  // all bits but 14 (`gr`, a progress counter)
#if defined(__IMXRT1062__)
DMAMEM RollingGateLastState sRollingGateLast[kRollingGateTrackCount];
#else
RollingGateLastState sRollingGateLast[kRollingGateTrackCount];
#endif

/// The row's bit order, in one place so the caller and the emitter cannot drift (the emitter prints one named field per
/// bit; the list is in the capture header's declaration).
uint16_t rollingGateFlags(bool epochStamped, bool avoidFullRebuild, bool spanAligned, bool playing, bool noteEditActive,
                          bool pendingDeferredOverdubStop, bool previewSourceActive, bool recordReturnBound,
                          bool viewSetServable, bool rootCurrent, bool roleAvailable, bool ownsCoverageSource,
                          bool demandActive, bool builderActive, bool outstandingGrains) {
  uint16_t bits = 0;
  if (epochStamped) bits |= 1u << 0;
  if (avoidFullRebuild) bits |= 1u << 1;
  if (spanAligned) bits |= 1u << 2;
  if (playing) bits |= 1u << 3;
  if (noteEditActive) bits |= 1u << 4;
  if (pendingDeferredOverdubStop) bits |= 1u << 5;
  if (previewSourceActive) bits |= 1u << 6;
  if (recordReturnBound) bits |= 1u << 7;
  if (viewSetServable) bits |= 1u << 8;
  if (rootCurrent) bits |= 1u << 9;
  if (roleAvailable) bits |= 1u << 10;
  if (ownsCoverageSource) bits |= 1u << 11;
  if (demandActive) bits |= 1u << 12;
  if (builderActive) bits |= 1u << 13;
  if (outstandingGrains) bits |= 1u << 14;
  return bits;
}

}  // namespace

TRACK_COLD_MEM void Track::reportPlaybackRollingGateRow(const char* exit, bool clockEpochStamped, bool reprojectedTick,
                                                        uint8_t requestResult) {
  // The rolling demand path's **state-change record**: every exit of `serviceRollingPlaybackWindow` used to return a
  // typed outcome with no row, so on hardware "this track owed no window" and "the window was refused, and by which
  // term" were the same absence (F-16). Nothing here admits, refuses, reorders or retires: the guards have already
  // decided and this reads the same state they read. **Qualification-only** (`SC_QUALIFICATION_ROWS_ENABLED`): the
  // other images carry neither the row nor this table.
#if SC_QUALIFICATION_ROWS_ENABLED
  if (exit == nullptr || activeLoopIndex >= Config::MAX_LOOPS_PER_TRACK) return;
  const uint8_t slot = activeLoopIndex;
  const Loop& loop = loopForSlot(slot);
  const auto* runtime = playbackRuntime.slotIfAllocated(slot);
  constexpr uint32_t span = 2 * Config::TICKS_PER_BAR;
  const bool rootCurrent = loop.publishedSnapshotIsCurrent();
  bool viewSetServable = false;
  bool roleAvailable = false;
  bool ownsCoverageSource = false;
  bool demandActive = false;
  bool builderActive = false;
  bool previewSourceActive = false;
  bool recordReturnBound = false;
  if (runtime) {
    const auto* set = runtime->viewSetIfPrewarmed();
    viewSetServable = set != nullptr && set->lifecycle.current < set->views.size() &&
                      set->views[set->lifecycle.current].complete && set->views[set->lifecycle.current].published;
    roleAvailable = runtime->coverageRoleAvailable();
    ownsCoverageSource = runtime->ownsCoverageSource();
    demandActive = runtime->pendingViewRequest_.active || runtime->frozenViewRequest.active;
    builderActive = runtime->viewBuild.active();
    previewSourceActive = runtime->previewSourceActive;
    recordReturnBound = runtime->hasBoundRecordReturnWindowForPendingJob();
  }
  uint16_t flags =
      rollingGateFlags(clockEpochStamped, loop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks),
                       loop.loopLengthTicks % span == 0, isPlaying(), editManager.isNoteEditActive(),
                       hasPendingDeferredOverdubStop(), previewSourceActive, recordReturnBound, viewSetServable,
                       rootCurrent, roleAvailable, ownsCoverageSource, demandActive, builderActive,
                       playbackViewOutstandingGrains(slot) != 0);
  if (reprojectedTick) flags |= 1u << 15;
  const uint8_t trackIndex = playbackRebuildTrackIndex(*this);
  if (trackIndex >= kRollingGateTrackCount) return;
  RollingGateLastState& last = sRollingGateLast[trackIndex];
  const uint16_t key = static_cast<uint16_t>(flags & kRollingGateChangeKeyMask);
  // The token is compared by **content**, not by pointer, and the key is the *decision* (the exit token, the length the
  // deciding terms read, the request's ordinal and the deciding bits) rather than every printed field. Bounded,
  // off-clock work on the lane turn.
  if (last.seen && last.key == key && last.slot == slot && last.loopLengthTicks == loop.loopLengthTicks &&
      last.requestResult == requestResult && last.exit != nullptr && std::strcmp(last.exit, exit) == 0) {
    return;
  }
  last.key = key;
  last.slot = slot;
  last.loopLengthTicks = loop.loopLengthTicks;
  last.requestResult = requestResult;
  last.exit = exit;
  last.seen = true;
  SC_WIN_GATE(trackIndex, slot, exit, flags, loop.loopLengthTicks, requestResult);
#else
  (void)exit;
  (void)clockEpochStamped;
  (void)reprojectedTick;
  (void)requestResult;
#endif
}
TRACK_COLD_MEM const char* Track::playbackWindowReadinessLabel(PlaybackWindowReadiness readiness) {
  // No name table and no `switch`: either is `.data` on this target (the RAM1 rule's finding), and this TU's string
  // literals are routed to FLASH by the linker script -- the pattern `playbackWindowReadinessHolder` established.
  if (readiness == PlaybackWindowReadiness::Ready) {
    return "ready";
  }
  if (readiness == PlaybackWindowReadiness::OwesBuild) {
    return "owes_build";
  }
  if (readiness == PlaybackWindowReadiness::OwesSource) {
    return "owes_source";
  }
  if (readiness == PlaybackWindowReadiness::OwesCurrency) {
    return "owes_currency";
  }
  if (readiness == PlaybackWindowReadiness::NotServable) {
    return "not_servable";
  }
  if (readiness == PlaybackWindowReadiness::NoRuntime) {
    return "no_runtime";
  }
  return "unknown";
}

/// DEC-108 §13b / task 3.9: how many readiness outcomes pass before one is reported -- the declared cadence, so a
/// readiness state that repeats on every owning turn cannot evict the evidence it explains. Every phase still logs
/// (`shouldLogOccurrence` always emits the first), which is why a *transition* is never missed.
inline constexpr uint32_t kReadinessReceiptInterval = 16;
#if defined(__IMXRT1062__)
/// DEC-108 §13b's placement rule: an additional counter is `DMAMEM`/`FLASHMEM`, never DTCM. Off-clock, read by the
/// owner turn alone.
DMAMEM static uint32_t sReadinessReceiptCount = 0;
#else
static uint32_t sReadinessReceiptCount = 0;
#endif

TRACK_COLD_MEM void Track::reportPlaybackWindowReadinessReceipt(uint8_t slotIndex, uint32_t currentPhase) {
  // **Cadence first** (DEC-108 §13b / design D10): the receipt is event-shaped, but a readiness outcome can repeat on
  // every owning turn, so the row is bounded -- that bound is what keeps this diagnostic from evicting the evidence it
  // explains, which is the contract `TelemetryCadence` exists for.
  if (!TelemetryCadence::shouldLogOccurrence(sReadinessReceiptCount, kReadinessReceiptInterval)) {
    return;
  }
  const Loop& loop = loopForSlot(slotIndex);
  const PlaybackWindowReadiness readiness = playbackWindowReadinessForSlot(slotIndex);
  // The lead the receipt explains: the interval this slot is *serving*, measured from the phase this turn is in.
  // Zero when no window serves the slot yet -- a receipt about "no window" has no lead to report, and saying zero
  // there is honest rather than inventing urgency.
  uint32_t leadTicks = 0;
  PlaybackViewSourceIdentity identity{};
  PlaybackCoverageInterval coverage{};
  if (publishedPlaybackWindowForSlot(slotIndex, identity, coverage) && coverage.isValid() &&
      loop.loopLengthTicks == coverage.loopLengthTicks && loop.loopLengthTicks > 0) {
    leadTicks = handoffLeadTicks(currentPhase % loop.loopLengthTicks, coverage.startPhase, loop.loopLengthTicks);
  }
  SC_WINDOW_READINESS(playbackRebuildTrackIndex(*this), slotIndex, playbackWindowReadinessLabel(readiness),
                      playbackWindowReadinessHolder(readiness), loop.playbackRevision, leadTicks);
}
