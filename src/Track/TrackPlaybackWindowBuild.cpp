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
                               bool /*allowHeavyBuild*/, uint32_t currentTick) {
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
  const bool noteEditPreview = editManager.isNoteEditActive() &&
                               &track == &trackManager.getSelectedTrack();
  const uint32_t windowRevision = noteEditPreview ? editManager.sessionPlaybackPreviewRevision()
                                                    : loop.playbackRevision;
  const void* snapshotIdentity =
      noteEditPreview ? nullptr : loop.publishedSnapshotIdentity();
  const bool longLoop = loop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks);
  uint32_t playhead = 0;
  if (loop.loopLengthTicks > 0) {
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
             !runtime.mergedMidiEvents.empty() &&
             runtime.mergedMidiEvents.windowLengthTicks > 0) {
    const uint32_t winStart = runtime.mergedMidiEvents.windowStartTick;
    const uint32_t winEnd = winStart + runtime.mergedMidiEvents.windowLengthTicks;
    const uint32_t margin = Config::TICKS_PER_BAR / 2;
    if (playhead + margin >= winStart && playhead < winEnd) {
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
    const bool rebuilt =
        ensurePlaybackMergedMidiEventsBuilt(*this, loop, *runtime, true, currentTick);
    if (rebuilt) {
      reconcilePlaybackLedgerAfterFullLoopRebuild(loop, *runtime);
    }
    if (loop.playbackOrderDirty) {
      const ProjectionContext playbackContext = makePlaybackContext(*this, loop, currentTick);
      ::rebuildPlaybackOrder(loop, runtime->mergedMidiEvents.mergedEvents, playbackContext);
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
  return runtime->mergedMidiEvents.builtFromRevision == loop.playbackRevision &&
         !runtime->mergedMidiEvents.mergedEvents.empty();
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
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slot);
  if (runtime != nullptr) {
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

TRACK_COLD_MEM CoverageServiceContract Track::playbackViewCoverageContract() {
  // The **declared** caps (DEC-108 item 1; `ExecutionBounds.md` row 27 asks the code to declare exactly
  // these before active windowed playback may be admitted at all). This is deliberately conservative, and
  // one term is explicitly **owed a measurement**:
  //
  // - `maximumPhaseAdvancePerTurn` = one bar (768 ticks). The playhead's real advance between two owner
  //   turns is NOT measured yet (`ExecutionBounds` row 27's "maximum phase advance"), so the declaration
  //   carries the conservative whole-bar bound. The direction is the safe one: `canCompleteBeforeHandoff`
  //   turns a larger advance into a *larger required lead*, so an unmeasured value can only refuse work,
  //   never admit too much. The measured figure is owed (task 6.3's sweeps and task 3.10's derived
  //   envelope), and until it lands this declaration admits only a loop long enough to hold the lead below.
  // - `maximumConsecutiveDeniedTurns` = 3: the allowance the admission calculus reserves, because each
  //   required grain may be preceded by turns that could not advance (that is the term's purpose -- see
  //   `canCompleteBeforeHandoff`'s `grains * (denied + 1)`).
  // - grain counts: the declared window is **two bars at the design's ~72 events/bar typical density** =
  //   144 events (`runtime-working-set-ownership/S2C1-PLAYBACK-WINDOW-LEASE.md`), which is 3 fill grains at
  //   `kPlaybackViewFillQuantum` = 64 and 3 order grains at `kPlaybackViewOrderQuantum` = 48, plus one
  //   promotion grain to make a completed build the ready view. The 2,048-event ceiling case needs more
  //   grains than this and is therefore **refused**, not served -- the safe direction, and the reason the
  //   ceiling stays a capacity limit rather than a promise (spec *Accepted content is never truncated*).
  // - `leadTicks` = one bar: preparation runs ahead of the handoff (DEC-108 item 8's "prepare ahead of the
  //   playhead"), so a window is owed for the interval the playhead is about to use.
  CoverageServiceContract contract;
  contract.maximumPhaseAdvancePerTurn = Config::TICKS_PER_BAR;
  contract.maximumConsecutiveDeniedTurns = 3;
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
    uint32_t currentPhase, uint32_t handoffPhase) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return PlaybackViewRequestResult::NoRuntime;
  }
  // Never allocate here (S4b.6's rule): the runtime's owner is the provisioning path, and an absent runtime
  // owes nothing. The obligation is the runtime's own unfinished build, so a refused request changes nothing.
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return PlaybackViewRequestResult::NoRuntime;
  }
  if (runtime->coverageBuildNeedsService()) {
    return PlaybackViewRequestResult::BuildInFlight;
  }
  const CoverageServiceContract contract = playbackViewCoverageContract();
  if (!contract.isDeclared()) {
    return PlaybackViewRequestResult::ContractUndeclared;
  }
  // Named separately from the primitive's own answer: the declared gate is what refuses *here*, so the
  // acceptance surface can tell "the margin was never there" from "the build was refused underneath it".
  if (!contract.canCompleteBeforeHandoff(currentPhase, handoffPhase, coverage.loopLengthTicks)) {
    return PlaybackViewRequestResult::CannotCompleteBeforeHandoff;
  }
  if (!runtime->beginCoverageBuild(contract, currentPhase, handoffPhase, source, sourceCount, sourceIdentity,
                                   coverage, cursorAnchor, ledgerBoundary)) {
    return PlaybackViewRequestResult::AdmissionRefused;
  }
  return PlaybackViewRequestResult::Admitted;
}

TRACK_COLD_MEM bool Track::hasPendingPlaybackViewMaintenance() const {
  for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
    const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slot);
    const bool runtimePresent = runtime != nullptr;
    if (MidiServiceDrain::playbackViewMaintenanceNeeded(
            runtimePresent, runtimePresent && runtime->coverageBuildNeedsService())) {
      return true;
    }
  }
  return false;
}

TRACK_COLD_MEM Track::PlaybackViewTurnResult Track::stepPlaybackViewMaintenance() {
  for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
    LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slot);
    if (runtime == nullptr || !runtime->coverageBuildNeedsService()) {
      continue;
    }
    // One grain, and only one: the phase decides which, so a turn can never copy source entries *and* merge
    // order entries (the build contract's own rule), and the build's persistent cursor carries the progress
    // to the next turn.
    const RuntimePlaybackViewBuildGrain grain = runtime->nextCoverageBuildGrain();
    if (grain == RuntimePlaybackViewBuildGrain::Fill) {
      return finishPlaybackViewGrain(slot, runtime->stepCoverageBuildFill(kPlaybackViewFillQuantum));
    }
    if (grain == RuntimePlaybackViewBuildGrain::Order) {
      return finishPlaybackViewGrain(slot, runtime->stepCoverageBuildOrder(kPlaybackViewOrderQuantum));
    }
    // `coverageBuildNeedsService()` is `viewBuild.active()`, which is Fill or Order only -- Complete and
    // Denied are both terminal and owe nothing.
    return PlaybackViewTurnResult::Idle;
  }
  return PlaybackViewTurnResult::Idle;
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
#if defined(SESSION_CAPTURE)
  DebugSessionCapture::playbackRebuild(0, loop.loopLengthTicks, 0, loop.playbackRevision,
                                       playbackRebuildTrackIndex(*this),
                                       playbackRebuildSlotIndex(*this, loop), 0, path);
#else
  (void)slotIndex;
  (void)loop;
  (void)path;
#endif
}
