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
#include "Utils/GateRowLastState.h"
#include "Utils/PlaybackSourceReadGuard.h"
// Task 3.7a (I7): the retained revision's own Active Note note-edit rows are resolved per layer, through the shipped
// owner of that rule (`applyNoteEditPassSequence`), never re-stated here.
#include "EditApply.h"
#include "Utils/ExternalPoolAccounting.h"
#include "Utils/RuntimeTimingTelemetry.h"
#include "Utils/CommittedMidiScratch.h"
#include "WindowPagedStream.h"  // task 3.7a (I1): the paged stream's page capacity is the gather's slice size
#include "PlaybackMergedMidiEvents.h"
#include "Utils/RuntimeProfiler.h"
#include "Utils/PlaybackTickContext.h"
// Phase 2's reason row seam: harnesses that stub the capture header compile the row out.
#ifndef SC_ROUTE_REJECT
#define SC_ROUTE_REJECT(track, slot, reason, cause, prevReason, prevTicks, sinceTick, changes, loopLengthTicks, exit) \
  ((void)0)
#endif

namespace {
/// **DEC-203** -- the two-bar span the merged-window policy clamps to. File-local because both callers live here, so
/// the builder and the readiness predicate cannot be handed different constants.
constexpr uint32_t kMergedWindowTicks = 2u * Config::TICKS_PER_BAR;
}  // namespace

void playbackCursorAdvanceSend(void* ctx, const MidiEvent& evt, uint8_t slotIndex);
void playbackCursorAdvanceApplyLedger(void* ctx, const MidiEvent& evt, uint8_t slotIndex);

extern TrackManager trackManager;
#if PLAYBACK_WINDOW_ROLLING_ENABLED && (defined(PIO_UNIT_TEST_NATIVE) || \
    (defined(SESSION_CAPTURE) && PLAYBACK_WINDOW_ROLLING_QUALIFICATION))
TRACK_COLD_MEM Track::RollingRecordStatus Track::readRollingRecordStatus() {
  RollingRecordStatus status;
  PlaybackSourceReadGuard guard;
  status.track = trackManager.getSelectedTrackIndex();
  const Track& track = trackManager.getTrack(status.track);
  status.slot = track.activeLoopIndex;
  status.state = static_cast<uint8_t>(track.trackState);
  status.recording = track.isRecording();
  const auto clock = clockManager.getPlaybackDeadlineClock();
  status.clockTick = clock.tick;
  status.epoch = clock.epoch;
  // Never allocate a runtime/Loop or ask a borrowed source for its currency.
  if (!track.loopsAllocated() || status.slot >= Config::MAX_LOOPS_PER_TRACK) return status;
  const Loop& loop = track.loopForSlot(status.slot);
  status.liveCaptureToken = loop.captureContentGeneration();
  status.rawCount = static_cast<uint32_t>(loop.capture.store.size());
  status.rawMax = loop.captureMaximumTick();
  status.cursor = loop.nextEventIndex;
  const auto* runtime = track.playbackRuntime.slotIfAllocated(status.slot);
  if (!runtime) return status;
  status.rollingPhase = static_cast<uint8_t>(runtime->rolling.phase);
  status.frameOrigin = runtime->rolling.projectionStart;
  status.framePeriod = runtime->rolling.loopLength;
  status.boundRole = runtime->rolling.boundView;
  status.boundStorage = runtime->rolling.boundStorageGeneration;
  status.ledgerCount = static_cast<uint32_t>(runtime->ledger.openCount());
  status.stopResponsibility = runtime->hasBoundRecordReturnWindowForPendingJob();
  status.frameEpoch = runtime->rolling.epoch;
  status.requestEpoch = runtime->pendingViewRequest_.clockEpoch;
  status.requestActive = runtime->pendingViewRequest_.active;
  status.requestInvalidated = runtime->pendingViewRequest_.positionInvalidated;
  const auto* set = runtime->viewSetIfPrewarmed();
  if (!set) return status;
  status.readyRole = set->lifecycle.ready;
  status.buildRole = set->lifecycle.build;
  // Fixed metadata only: no nextGrain/source-currency query in this receipt.
  if (status.readyRole < set->views.size()) {
    const auto& ready = set->views[status.readyRole];
    status.readyFlags = uint8_t(ready.complete) | (uint8_t(ready.published) << 1) |
        (uint8_t(ready.rawCoveragePrepared) << 2) |
        (uint8_t(ready.source == runtime->pendingViewRequest_.source) << 3);
  }
  if (status.buildRole < set->views.size()) {
    const auto& build = set->views[status.buildRole];
    status.buildFlags = uint8_t(build.complete) | (uint8_t(build.published) << 1) |
        (uint8_t(build.rawCoveragePrepared) << 2) |
        (uint8_t(build.source == runtime->pendingViewRequest_.source) << 3);
  }
  status.workerFlags = uint8_t(runtime->viewBuild.active()) |
      (uint8_t(runtime->viewBuild.ownsLiveCaptureBorrow()) << 1);
  status.currentRole = set->lifecycle.current;
  if (status.currentRole >= set->views.size()) return status;
  const auto& current = set->views[status.currentRole];
  status.valid = true;
  status.currentStorage = current.storageGeneration;
  status.published = current.published;
  status.complete = current.complete;
  status.sourceOrigin = static_cast<uint8_t>(current.source.origin);
  status.sourceGeneration = current.source.sourceGeneration;
  status.captureContentToken = current.source.captureContentGeneration;
  status.coverageStart = current.coverage.startPhase;
  status.coverageLength = current.coverage.lengthTicks;
  status.coveragePeriod = current.coverage.loopLengthTicks;
  status.rows = static_cast<uint32_t>(current.storage.eventCount);
  // Exactly two fixed row copies, only from complete immutable current.
  // Empty/unpublished/unavailable endpoints retain explicit sentinels.
  if (current.complete && current.published && status.rows &&
      current.storage.isPrewarmed() && status.rows <= current.storage.capacity) {
    const auto first = current.storage.order[0];
    const auto last = current.storage.order[status.rows - 1];
    if (first < status.rows && last < status.rows) {
      const MidiEvent firstEvent = current.storage.events[first];
      const MidiEvent lastEvent = current.storage.events[last];
      status.firstTick = firstEvent.tick;
      status.firstNoteId = firstEvent.noteId;
      status.lastTick = lastEvent.tick;
      status.lastNoteId = lastEvent.noteId;
    }
  }
  return status;
}

#if defined(SESSION_CAPTURE) && PLAYBACK_WINDOW_ROLLING_QUALIFICATION
TRACK_COLD_MEM void Track::printRollingRecordStatus(uint32_t requestId) {
  const auto s = readRollingRecordStatus(); // Guard restored before formatting/USB.
  Serial.printf("#CAP,%lu,DIAG,rolling_record,%lu,%u,%u,%u,%llu,%lu,%u,%ld,%lu,%u,%llu,"
                "%u,%llu,%u,%u,%u,%lu,%llu,%llu,%lu,%lu,%lu,%lu,%lu,%lu,"
                "%lu,%lu,%lu,%lu,%lu,%lu,%u,%u,%u,%lu,%lu,%u,%u,%u,%u,%u,%u,%u\r\n",
      static_cast<unsigned long>(micros()), static_cast<unsigned long>(requestId),
      s.track, s.slot, s.state, static_cast<unsigned long long>(s.clockTick),
      static_cast<unsigned long>(s.epoch), s.rollingPhase, static_cast<long>(s.frameOrigin),
      static_cast<unsigned long>(s.framePeriod), s.boundRole, static_cast<unsigned long long>(s.boundStorage),
      s.currentRole, static_cast<unsigned long long>(s.currentStorage), s.published, s.complete, s.sourceOrigin,
      static_cast<unsigned long>(s.sourceGeneration), static_cast<unsigned long long>(s.captureContentToken),
      static_cast<unsigned long long>(s.liveCaptureToken), static_cast<unsigned long>(s.coverageStart),
      static_cast<unsigned long>(s.coverageLength), static_cast<unsigned long>(s.coveragePeriod),
      static_cast<unsigned long>(s.rows), static_cast<unsigned long>(s.cursor), static_cast<unsigned long>(s.ledgerCount),
      static_cast<unsigned long>(s.firstTick), static_cast<unsigned long>(s.firstNoteId),
      static_cast<unsigned long>(s.lastTick), static_cast<unsigned long>(s.lastNoteId),
      static_cast<unsigned long>(s.rawCount), static_cast<unsigned long>(s.rawMax),
      s.recording, s.stopResponsibility, s.valid,
      static_cast<unsigned long>(s.frameEpoch), static_cast<unsigned long>(s.requestEpoch),
      s.requestActive, s.requestInvalidated, s.readyRole, s.buildRole, s.readyFlags, s.buildFlags, s.workerFlags);
}
#endif
#endif


namespace {
#if PLAYBACK_WINDOW_ROLLING_ENABLED
bool coldRollingRequest(const LoopPlaybackRuntime&, const PlaybackViewPendingRequest&);
bool coldRollingShape(const LoopPlaybackRuntime&, const Loop&, uint32_t);
// C3 (DEC-116) — the slot-qualified cold one-bar route's two readings, defined beside `coldRollingShape`.
bool slotMayOwnColdFullLoopWindow(uint8_t slot, uint8_t activeLoopIndex, bool loopsAllocated,
                                  bool slotEnabled, const Loop& loop);
bool coldLayeredSlotFrameForEmission(const LoopPlaybackRuntime* runtime, const Loop& loop, uint8_t slot,
                                     uint8_t activeLoopIndex);
#endif
#if PLAYBACK_WINDOW_ROLLING_ENABLED
// Live demand belongs to the retained slot/operation, never to its raw shell.
TRACK_COLD_MEM bool liveRecordCaptureSource(const Loop& loop, const LoopPlaybackRuntime& runtime) {
  return runtime.viewSetIfPrewarmed() && loop.capture.phase == CapturePhase::Record &&
      !loop.hasCommittedPasses() && !loop.publishedSnapshotIsCurrent() &&
      loop.captureContentGeneration() != 0 && loop.capture.phase.generation() != 0;
}

TRACK_COLD_MEM bool liveRecordRecipeMatches(const PlaybackViewPendingRequest& request, const Loop& loop,
    const LoopPlaybackRuntime& runtime, uint32_t operation, int32_t origin) {
  auto projection = loop.liveCaptureProjectionIdentity();
  // Running Record's predicted length grows for presentation. This recipe
  // copies raw rows into its explicit coverage; length is not its projection.
  if (projection.phase == CapturePhase::Record)
    projection.loopLengthTicks = request.liveCaptureProjection.loopLengthTicks;
  return request.sourceKind == PlaybackViewPendingRequest::SourceKind::LiveCapture &&
      request.operationGeneration == operation &&
      request.liveLeaseGeneration == runtime.workingSetGeneration() &&
      request.liveProjectionStart == origin &&
      request.liveCaptureProjection == projection;
}
// Entry-operation accounting only, not physical deadline admission. Frozen
// source extents and the existing order bound charge suffix work explicitly.
TRACK_COLD_MEM void priceLiveRecordWork(PlaybackViewAdmittedWork& work, size_t extent, size_t frontier,
    size_t sortedPrefix, size_t capacity) {
  const size_t rows = extent >= frontier ? extent - frontier : extent;
  const size_t fillUnits = rows + rows / LoopEventStoreConfig::CHUNK_CAPACITY + 1;
  const size_t orderUnits = sortedPrefix
      ? RuntimePlaybackOrderBuilder::extensionWorkEntriesUpperBound(sortedPrefix, rows)
      : RuntimePlaybackOrderBuilder::workEntriesUpperBound(std::min(extent, capacity));
  work.sourceEntries = static_cast<uint32_t>(extent);
  work.retainedEventBound = static_cast<uint32_t>(capacity);
  work.fillGrains = uint16_t(std::min<size_t>(UINT16_MAX,
      (fillUnits + Track::kPlaybackViewFillQuantum - 1) / Track::kPlaybackViewFillQuantum));
  work.orderGrains = uint16_t(std::min<size_t>(UINT16_MAX,
      orderUnits / Track::kPlaybackViewOrderQuantum + (orderUnits % Track::kPlaybackViewOrderQuantum != 0)));
  work.promotionGrains = 1;
}


TRACK_COLD_MEM void retainLiveRecordDemand(LoopPlaybackRuntime& runtime, const Loop& loop,
    uint32_t operation, int32_t origin, PlaybackDeadlineClock clock) {
  auto& request = runtime.pendingViewRequest_;
  auto* set = runtime.viewSetIfPrewarmed();
  if (!liveRecordCaptureSource(loop, runtime)) return;
  const bool same = liveRecordRecipeMatches(request, loop, runtime, operation, origin);
  if (same && request.positionInvalidated) return; // Rejection is sticky for this interval.
  if (request.sourceKind == PlaybackViewPendingRequest::SourceKind::LiveCapture &&
      request.operationGeneration == operation && !same) {
    request.positionInvalidated = true;
    request.reason = PlaybackViewPendingReason::DeadlineInvalidatedGeometry;
    return;
  }
  if (request.active && !same) return; // Another owner's obligation is not ours to overwrite.
  const auto& current = set->views[set->lifecycle.current];
  const auto& frame = runtime.rolling;
  const bool serving = frame.phase == LoopPlaybackRuntime::RollingPhase::Serving;
  // Initial publication is already authoritative. The clock reader must bind
  // its frame before main can derive a successor's nonzero boundary deadline.
  // Re-admitting an initial request here can strand a complete ready forever.
  if (!serving && current.complete && current.published && !current.rawCoveragePrepared &&
      current.source.origin == PlaybackWindowOrigin::Capture &&
      current.source.sourceGeneration == operation) return;
  const uint32_t period = serving ? frame.loopLength : Config::TICKS_PER_BAR;
  const uint64_t wanted = (uint64_t(loop.captureMaximumTick()) / Config::TICKS_PER_BAR + 1) *
      Config::TICKS_PER_BAR;
  const uint32_t desired = wanted <= UINT32_MAX ? uint32_t(wanted) : period;
  if (!request.active && same && serving && period == Config::TICKS_PER_BAR &&
      desired <= period && current.source.captureContentGeneration == loop.captureContentGeneration()) return;
  if (!same || !request.active) {
    request = {};
    request.sourceKind = PlaybackViewPendingRequest::SourceKind::LiveCapture;
    request.operationGeneration = operation;
    request.liveLeaseGeneration = runtime.workingSetGeneration();
    request.liveCaptureProjection = loop.liveCaptureProjectionIdentity();
    request.liveProjectionStart = origin;
    request.coverage = {0, Config::TICKS_PER_BAR, period};
    request.clockEpoch = clock.epoch;
    request.work.admittedTick = clock.tick;
    request.requestedPhase = IntervalProjection::tickPhaseInProjectionCycle(
        uint32_t(clock.tick), origin, period);
    if (serving) {
      // The physical current names the oldest next distinct interval, even if
      // service has already passed its edge. New input cannot steal that debt.
      const uint32_t next = current.coverage.startPhase + Config::TICKS_PER_BAR;
      request.coverage.startPhase = next < period ? next : 0;
      const uint64_t cycle = (frame.lastTick - uint32_t(frame.projectionStart)) / period;
      request.deadlineTick = uint32_t(frame.projectionStart) + cycle * period + next;
      if (next >= period) request.coverage.loopLengthTicks = std::max(period, desired);
    }
  }
  // Growth can coalesce only a wrap obligation; never replace an owed bar.
  if (serving && request.coverage.startPhase == 0)
    request.coverage.loopLengthTicks = std::max(request.coverage.loopLengthTicks, desired);
  request.loopLengthTicks = request.coverage.loopLengthTicks;
  request.active = true;
  request.liveMaximumTick = loop.captureMaximumTick();
  request.source = {loop.playbackRevision, operation, nullptr,
                    PlaybackWindowOrigin::Capture, loop.captureContentGeneration()};
  priceLiveRecordWork(request.work, loop.capture.store.size(), 0, 0,
                      RuntimePlaybackViewConfig::kWindowAdmissionLimit);
  request.reason = PlaybackViewPendingReason::AwaitingSourceQualification;
}
#endif

const NoteEditCurrentState& noteEditCurrentStateForPlayback() {
#if defined(PIO_UNIT_TEST_NATIVE) && !defined(HARNESS_LINKS_REAL_EDIT_MANAGER)
  // These production-TU harnesses intentionally stub the entire edit owner. They exercise
  // unrelated playback paths and must not pull in NoteEditCurrentState's implementation.
  static const NoteEditCurrentState emptyState{};
  return emptyState;
#else
  return editManager.noteEditCurrentState();
#endif
}

/// Task 3.15d: **apply the session's identity-qualified delta to a committed interval window**, in place. A committed
/// identity the session changed (moved, re-pitched, re-velocitied, hidden or deleted) is dropped from the base; the
/// session's own rows for the interval are appended for identities that override committed content. One rule, called by
/// both readers -- the merged NOTE_EDIT fallback and the preview lane's interval composition -- so the two cannot drift
/// while the switch (task 3.7b) is still ahead. `TRACK_COLD_MEM` because both callers are rebuild-time, and an ITCM copy
/// of this rule cost 400 B of the pinned budget (392,616 + 600 padding vs 392,216 + 1,000 at the approved pin).
///
/// **The current state is the edit authority; the session projection (the store) is derived.** The display turn
/// refreshes the store, but a reader can run before that turn (measured on the device: the live movement exists in the
/// current state while `session_events=0`). The store's events are appended only on the current span, and any current-
/// state override the store has not carried yet is filled per half (On at the current start, Off at the current end),
/// so the served interval reflects the edit without waiting for a display turn and without duplicating a projected one.
TRACK_COLD_MEM void applyNoteEditDeltaToCommittedWindow(SessionMidiEventVec& committed,
                                                       const PlaybackCoverageInterval& coverage,
                                                       uint8_t channel) {
  const NoteEditCurrentState& currentState = noteEditCurrentStateForPlayback();
  const auto& rows = currentState.rows();
  const auto stateRow = [&rows](NoteId noteId) -> const NoteEditCurrentNoteState* {
    const auto found = rows.find(noteId);
    return found != rows.end() ? &found->second : nullptr;
  };
  const auto rowOverridesCommitted = [](const NoteEditCurrentNoteState& row) {
    return row.presence == NoteEditPresenceType::Hidden ||
           row.presence == NoteEditPresenceType::Deleted ||
           row.presence == NoteEditPresenceType::Added ||
           row.currentSpan.pitch != row.committedSpan.pitch ||
           row.currentSpan.velocity != row.committedSpan.velocity ||
           row.currentSpan.startTick != row.committedSpan.startTick ||
           row.currentSpan.endTick != row.committedSpan.endTick;
  };
  const auto rowIsSuppressed = [](const NoteEditCurrentNoteState& row) {
    return row.presence == NoteEditPresenceType::Hidden || row.presence == NoteEditPresenceType::Deleted;
  };
  /// A committed source event is still the note's own geometry only when it sits at the row's live tick. `MoveNote`
  /// carries `committedSpan` with the current span for a macro-sealed row, so `rowOverridesCommitted` alone would
  /// keep serving the source pair at its old tick; the source tick is the identity-qualified check.
  const auto committedEventSitsAtLiveGeometry = [](const NoteEditCurrentNoteState& row, const MidiEvent& event) {
    if (event.isNoteOn()) {
      return event.tick == row.currentSpan.startTick;
    }
    if (event.isNoteOff()) {
      return event.tick == row.currentSpan.endTick;
    }
    return true;
  };
  size_t retained = 0;
  for (size_t i = 0; i < committed.size(); ++i) {
    const MidiEvent& event = committed[i];
    const NoteEditCurrentNoteState* row = event.noteId != kInvalidNoteId ? stateRow(event.noteId) : nullptr;
    if ((event.isNoteOn() || event.isNoteOff()) && row != nullptr &&
        (rowOverridesCommitted(*row) || !committedEventSitsAtLiveGeometry(*row, event))) {
      continue;
    }
    if (retained != i) committed[retained] = event;
    ++retained;
  }
  committed.resize(retained);

  for (const MidiEvent& event : editManager.sessionMidiEvents()) {
    if (!event.isNoteOn() && !event.isNoteOff()) continue;
    if (!coverage.contains(event.tick)) continue;
    const NoteEditCurrentNoteState* row = event.noteId != kInvalidNoteId ? stateRow(event.noteId) : nullptr;
    if (row == nullptr || rowIsSuppressed(*row)) {
      continue;
    }
    // A stale projection's old span is not this interval's delta; only the current span is.
    if (event.tick != row->currentSpan.startTick && event.tick != row->currentSpan.endTick) continue;
    // The live store is the authority for an edited row; an untouched row's identical source event stays once.
    bool alreadyServed = false;
    for (const MidiEvent& served : committed) {
      if (served.noteId == event.noteId && served.tick == event.tick &&
          served.isNoteOn() == event.isNoteOn()) {
        alreadyServed = true;
        break;
      }
    }
    if (alreadyServed) continue;
    committed.push_back(event);
  }

  for (const auto& entry : rows) {
    const NoteId noteId = entry.first;
    const NoteEditCurrentNoteState& row = entry.second;
    if (rowIsSuppressed(row)) continue;
    const bool wantOn = coverage.contains(row.currentSpan.startTick);
    const bool wantOff = coverage.contains(row.currentSpan.endTick);
    if (!wantOn && !wantOff) continue;
    bool hasOn = false;
    bool hasOff = false;
    for (const MidiEvent& event : committed) {
      if (event.noteId != noteId) continue;
      if (event.isNoteOn() && event.tick == row.currentSpan.startTick) hasOn = true;
      if (event.isNoteOff() && event.tick == row.currentSpan.endTick) hasOff = true;
      if (hasOn && hasOff) break;
    }
    if (wantOn && !hasOn) {
      MidiEvent on = MidiEvent::NoteOn(row.currentSpan.startTick, channel, row.currentSpan.pitch,
                                       static_cast<uint8_t>(row.currentSpan.velocity));
      on.noteId = noteId;
      committed.push_back(on);
    }
    if (wantOff && !hasOff) {
      MidiEvent off = MidiEvent::NoteOff(row.currentSpan.endTick, channel, row.currentSpan.pitch, 0);
      off.noteId = noteId;
      committed.push_back(off);
    }
  }
}

/// Task 3.7a (I1): one bounded slice of a retained interval is **one page** -- the same capacity the legacy paged
/// stream produces with (`kWindowPageCapacity`), so the retained-source gather and the page producer cannot answer
/// with two different granularities, and a caller's per-turn inspected work is a page rather than the interval.
constexpr size_t kRetainedGatherSliceEvents = kWindowPageCapacity;

/// The request path's own bound on that primitive: a window cannot hold more than the admission limit, so one page per
/// slice covers it with margin -- and an interval that still is not traversed is refused rather than truncated.
constexpr uint32_t kRetainedGatherMaxSlices =
    static_cast<uint32_t>(RuntimePlaybackViewConfig::kWindowAdmissionLimit / kWindowPageCapacity) + 2u;

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

// Task 3.15e slice 3: the live window's frontier-limited size. The provenance `eventAt` (C5) sits above.
TRACK_INTERNAL_MEM size_t overlayPlaybackStreamSize(const void* ctx) {
  const auto* stream = static_cast<const FixedPlaybackStreamCtx*>(ctx);
  const size_t retained = stream->storage->eventCount;
  if (stream->readableCount == 0 || stream->readableCount >= retained) return retained;
  return stream->readableCount;
}

// Task 3.15e slice 3/C5 — **the provenance-recording adapter**, flash-resident and used only by the overdub overlay route.
// A separate `eventAt` keeps every other fixed route byte-identical (the shared one is untouched) and keeps this store out
// of ITCM; the walk reaches it through the stream's function pointer, so no veneer is emitted.
TRACK_INTERNAL_MEM const MidiEvent& overlayPlaybackStreamEventAt(const void* ctx, uint16_t cursor) {
  auto* stream = const_cast<FixedPlaybackStreamCtx*>(static_cast<const FixedPlaybackStreamCtx*>(ctx));
  const uint16_t row = stream->storage->order[cursor];
  if (stream->passOrigins != nullptr) stream->lastOrigin = stream->passOrigins[row].pass;
  return stream->storage->events[row];
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

// Cold: every caller is a FLASH helper (readiness/build rows and the witness), so this stays
// out of ITCM. Emitting it there measured 64 B of qualification padding on the witness image.
TRACK_COLD_MEM uint8_t playbackRebuildTrackIndex(const Track& track) {
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

namespace { bool mergedMidiEventsBuildInProgress = false; }

// DEC-241: the internal-clock ISR tick is consume-only (Utils/PlaybackTickContext.h). RAM2, cleared at boot.
#ifndef DMAMEM
#define DMAMEM
#endif
DMAMEM PlaybackTickContext::State PlaybackTickContext::gState;
static_assert(Config::NUM_TRACKS <= 8 && Config::MAX_LOOPS_PER_TRACK <= 8, "owed bits: one byte per track");
TRACK_COLD_MEM void PlaybackTickContext::initAtBoot() {
  gState.inInterrupt = false;
  gState.servicing = false;
  for (auto& bits : gState.owed) bits = 0;
  gState.misses = 0;
  gState.missesExternal = 0;
  for (auto& count : gState.route) count = 0;
  for (auto& count : gState.emittedRoute) count = 0;
  gState.emittedMissInterrupt = 0;
  gState.emittedMissExternal = 0;
  gState.turnReject = 0;
  gState.turnCause = 0;
  for (auto& row : gState.reject)
    for (auto& slot : row) slot = SlotRouteReject{};
  for (auto& exit : gState.producerExit) exit = nullptr;
}
/// playback-tick-consume-only phase 2: files this turn's first failed predicate under the slot. Reached only from
/// `selectMergedPlaybackFallbackForTick`, i.e. only when the merged fallback was selected; a served tick never runs it.
TRACK_COLD_MEM void PlaybackTickContext::recordMergedRouteReject(uint8_t trackIndex, uint8_t slotIndex, uint32_t tick) {
  if (trackIndex >= 8 || slotIndex >= 8) return;
#if PLAYBACK_WINDOW_ROLLING_ENABLED
  const uint8_t reason = gState.turnReject;
  const uint8_t cause = reason == static_cast<uint8_t>(RouteReject::FrameBlocked) ? gState.turnCause : 0;
#else
  const uint8_t reason = static_cast<uint8_t>(RouteReject::RollingDisabled);
  const uint8_t cause = 0;
#endif
  if (reason == static_cast<uint8_t>(RouteReject::Served)) return;
  SlotRouteReject& s = gState.reject[trackIndex][slotIndex];
  if (s.reason != reason || s.cause != cause || s.changes == 0) {
    s.prevReason = s.reason;
    s.prevTicks = s.ticks;
    s.reason = reason;
    s.cause = cause;
    s.sinceTick = tick;
    s.ticks = 0;
    s.changes = s.changes + 1u;
  }
  s.ticks = s.ticks + 1u;
}
TRACK_COLD_MEM PlaybackTickContext::RouteCounts PlaybackTickContext::takeRouteWindow() {
  const RouteCounts now = routeCounts();
  RouteCounts delta{};
  for (int i = 0; i < static_cast<int>(TickRoute::Count); ++i) {
    delta.route[i] = now.route[i] - gState.emittedRoute[i];
    gState.emittedRoute[i] = now.route[i];
  }
  delta.missInterrupt = now.missInterrupt - gState.emittedMissInterrupt;
  delta.missExternal = now.missExternal - gState.emittedMissExternal;
  gState.emittedMissInterrupt = now.missInterrupt;
  gState.emittedMissExternal = now.missExternal;
  return delta;
}
TRACK_COLD_MEM PlaybackTickContext::RouteCounts PlaybackTickContext::routeCounts() {
  RouteCounts out{};
  for (int i = 0; i < static_cast<int>(TickRoute::Count); ++i) out.route[i] = gState.route[i];
  out.missInterrupt = gState.misses;
  out.missExternal = gState.missesExternal;
  return out;
}
#if defined(PIO_UNIT_TEST_NATIVE)
bool mergedFallbackOwedForTest(uint8_t trackIndex, uint8_t slotIndex) {
  return trackIndex < Config::NUM_TRACKS && slotIndex < 8 &&
         (PlaybackTickContext::gState.owed[trackIndex] & (1u << slotIndex)) != 0;
}
uint32_t mergedFallbackTickMissesForTest() { return PlaybackTickContext::gState.misses; }
#endif
#if defined(PIO_UNIT_TEST_NATIVE)
// Test-only witness: MIDI turns that selected the live overlay window. No target path reads it.
namespace { unsigned sOverlayLiveSelectionsForTest = 0; bool sOverlayEdgeDisabledForTest = false; }
unsigned Track::overlayLiveSelectionCount() { return sOverlayLiveSelectionsForTest; }
void Track::setOverlayEdgeDisabledForTest(bool disabled) { sOverlayEdgeDisabledForTest = disabled; }
#endif

#if defined(PIO_UNIT_TEST_NATIVE)
/// DEC-143's cost criterion, measurable from a native suite: how many times the merged fallback actually rebuilt. A
/// maintenance turn that finds the same published identity must not rebuild, so a run of turns costs at most the
/// transitions, never one rebuild per turn. Test-only; no target code path reads it.
namespace { unsigned sMergedRebuildsForTest = 0; }
unsigned playbackMergedRebuildCountForTest() { return sMergedRebuildsForTest; }
void resetPlaybackMergedRebuildCountForTest() { sMergedRebuildsForTest = 0; }
#endif

namespace {
/// The merged fallback's one reuse rule: same identity **and** the window still covers the playhead (DEC-197). Shared by
/// the build (`ensurePlaybackMergedMidiEventsBuilt`) and the interrupt tick's consume check (DEC-241), so the two can
/// never disagree about whether a built fallback is current.
TRACK_COLD_MEM bool mergedFallbackReuseHolds(const LoopPlaybackRuntime& runtime, uint32_t windowRevision,
                                             const void* buildIdentity, uint32_t playhead, bool prepareLaunch) {
  if (runtime.mergedMidiEvents.builtFromRevision != windowRevision ||
      runtime.mergedMidiEvents.builtFromSnapshotIdentity != buildIdentity ||
      (buildIdentity == nullptr && runtime.mergedMidiEvents.empty()) ||
      runtime.mergedMidiEvents.windowLengthTicks == 0) {
    return false;
  }
  const uint32_t winStart = runtime.mergedMidiEvents.windowStartTick;
  const uint32_t winEnd = winStart + runtime.mergedMidiEvents.windowLengthTicks;
  const uint32_t margin = Config::TICKS_PER_BAR / 2;
  return (prepareLaunch ? playhead >= winStart : playhead + margin >= winStart) && playhead < winEnd;
}

struct MergedFallbackKey {
  uint32_t windowRevision;
  const void* buildIdentity;
  uint32_t playhead;
};

/// The same key `ensurePlaybackMergedMidiEventsBuilt` derives for a tick build (`prepareLaunch == false`).
TRACK_COLD_MEM MergedFallbackKey mergedFallbackTickKey(Track& track, Loop& loop, uint32_t currentTick) {
  const bool noteEditPreview = editManager.isNoteEditActive() && &track == &trackManager.getSelectedTrack();
  MergedFallbackKey key{};
  key.windowRevision = noteEditPreview ? editManager.sessionPlaybackPreviewRevision() : loop.playbackRevision;
  const void* snapshotIdentity = noteEditPreview ? nullptr : loop.publishedSnapshotIdentity();
  if (loop.loopLengthTicks > 0) {
    key.playhead = IntervalProjection::tickPhaseInProjectionCycle(currentTick, track.getProjectionCycleStartTick(),
                                                                  loop.loopLengthTicks);
  }
  const uint8_t recordOverlaySlot = playbackRebuildSlotIndex(track, loop);
  const bool publishedRecordOverlay = !noteEditPreview && loop.hasSealedCaptureBorrow() &&
                                      track.hasPublishedRecordPresentationForSlot(recordOverlaySlot);
  const void* const recordOverlayIdentity =
      publishedRecordOverlay ? static_cast<const void*>(loop.captureReadSource().finalizing) : nullptr;
  key.buildIdentity = snapshotIdentity != nullptr ? snapshotIdentity : recordOverlayIdentity;
  return key;
}

}  // namespace

bool ensurePlaybackMergedMidiEventsBuilt(Track& track, Loop& loop, LoopPlaybackRuntime& runtime,
                               bool /*allowHeavyBuild*/, uint32_t currentTick, bool prepareLaunch) {
  // Projection boundary (linear-loop-tick-storage): mergedEvents are read-only input to
  // playback order + MIDI send. Playback mergedMidiEvents is committed-only (224719):
  // live capture must not fold into this representation. NOTE_EDIT uses session store
  // (Tier 2) — full replace, no materialized underlay.
  //
  // **One rule for every loop length.** The window is `min(2 bars, loopLength)` ticks starting at the playhead, and the
  // loop only ever *bounds* it; nothing here branches on how long the loop is. The window is filled from one source at
  // a time: the published root while it is current, otherwise the committed passes (the same authority the display
  // resolves), otherwise chunk-ref merge (DEC-016). Never a full-loop gather outside that window — the full-loop gather
  // after LoadLoopJob Commit hard-faults (session_20260718_210532 / 210001).
  if (mergedMidiEventsBuildInProgress) {
    return false;
  }
  const bool noteEditPreview = !prepareLaunch && editManager.isNoteEditActive() &&
                               &track == &trackManager.getSelectedTrack();
  const uint32_t windowRevision = noteEditPreview ? editManager.sessionPlaybackPreviewRevision()
                                                    : loop.playbackRevision;
  const void* snapshotIdentity =
      noteEditPreview ? nullptr : loop.publishedSnapshotIdentity();
  // One window, one centring rule, one reuse rule — for a 1-bar loop and a 40-bar loop alike. The only place length is
  // consulted is the clamp itself.
  const uint32_t playbackWindowTicks =
      mergedPlaybackWindowLengthTicks(loop.loopLengthTicks,
                                      loop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks),
                                      kMergedWindowTicks);
  uint32_t playhead = 0;
  if (!prepareLaunch && loop.loopLengthTicks > 0) {
    playhead = IntervalProjection::tickPhaseInProjectionCycle(
        currentTick, track.getProjectionCycleStartTick(), loop.loopLengthTicks);
  }
  // **DEC-143 — the temporary published-record overlay, sourced from the sealed extent.** The Loop fills the window
  // from the extent it already holds a read borrow on (the slot's `CaptureStopPresentation::rawRange`), so what the
  // Track contributes is only the **publication edge**: `hasPublishedRecordPresentationForSlot` is `retirementActive`
  // plus the runtime's presentation owner, i.e. exactly the take that crossed DEC-142's immutable `RecordPublished`
  // boundary. A still-mutable (pre-publication) take never reaches the branch, and a pass's chunk-id list is not a
  // source at all -- undo/redo re-materialises the pass while the presentation is still the active reader, which is
  // what withdrew that form on the device (`captures/i5-dec143-overlay-base-02`). The extent's own address is the
  // build identity below, so the reuse guard cannot keep serving the empty build it cached before the edge.
  const uint8_t recordOverlaySlot = playbackRebuildSlotIndex(track, loop);
  const bool publishedRecordOverlay = !noteEditPreview && loop.hasSealedCaptureBorrow() &&
                                      track.hasPublishedRecordPresentationForSlot(recordOverlaySlot);
  // The identity is the **extent's own address** (the slot-owned presentation record's sealed range), which is stable
  // for the whole borrow lifetime and changes with the record, so the reuse guard cannot keep serving a build it made
  // before the edge -- the failure the withdrawn form hit. It is reached through the Loop's read borrow, which is the
  // same object the gather reads, so the key and the source cannot disagree.
  const void* const recordOverlayIdentity =
      publishedRecordOverlay ? static_cast<const void*>(loop.captureReadSource().finalizing) : nullptr;
  const void* const buildIdentity =
      snapshotIdentity != nullptr ? snapshotIdentity : recordOverlayIdentity;

  // One reuse rule: same identity **and** the window still covers the playhead. The coverage half is what lets the
  // windowed path move; without it a window is frozen at wherever it was first built (DEC-197).
  if (mergedFallbackReuseHolds(runtime, windowRevision, buildIdentity, playhead, prepareLaunch)) {
    DIAG_COUNTER_INC(PlaybackDeferredReuse);
    return false;
  }
  // DEC-239 profile scope (long: window-shaped but content-sized, no hard event cap; synchronous). After the
  // reuse return above: on device that cheap path was ~32,000 calls per chain at ~17 us, i.e. the poll.
  PROFILE_SCOPE_LONG(profileScope, RuntimeProfiler::ProfileId::PlaybackWindow_build);

  // The admission decides by **coverage** (`isFullLoopMergedPlaybackWindow` inside), never by length: a seal that fits
  // the window being served is folded in; one that does not takes the ordinary rebuild below.
  if (!noteEditPreview && loop.tryAdmitLastOverdubSealIntoMergedEvents(runtime.mergedMidiEvents)) {
    // The increment extends **this** window, so it keeps this window's source identity. The Loop's admit stamps a null
    // identity because it cannot see which source served the window; while the DEC-143 overlay serves it, that null
    // would make the next maintenance turn rebuild from the sealed extent alone and silently drop the wrap just
    // admitted (the record take is still pre-root, so a rebuild can only serve the extent). Keep the identity exactly
    // in that window and leave the root-backed path's stamp untouched.
    if (recordOverlayIdentity != nullptr) {
      runtime.mergedMidiEvents.builtFromSnapshotIdentity = buildIdentity;
    }
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
#if defined(PIO_UNIT_TEST_NATIVE)
  ++sMergedRebuildsForTest;
#endif
#if RUNTIME_TIMING_ENABLED || defined(SESSION_CAPTURE)
  const uint32_t playbackBuildStartUs = micros();
#endif
  DIAG_COUNTER_INC(PlaybackMergedMidiEventsRebuild);
  const void* usedSnapshotIdentity = nullptr;
  Loop::SnapshotWindowGatherPath gatherPath = Loop::SnapshotWindowGatherPath::None;
  bool servedFromCommittedPasses = false;
  if (noteEditPreview && loop.publishedSnapshotIsCurrent()) {
    // A current-root NoteEditSession stores only its edit delta. Keep the committed
    // interval as playback base and replace just identities the session changed.
    // DEC-198: the same window as every other source -- the session delta is applied to the served coverage.
    const uint32_t winLen = playbackWindowTicks;
    uint32_t winStart = 0;
    if (loop.loopLengthTicks > winLen) {
      winStart = playhead > (winLen / 2) ? playhead - (winLen / 2) : 0;
      if (winStart + winLen > loop.loopLengthTicks) winStart = loop.loopLengthTicks - winLen;
    }
    runtime.mergedMidiEvents.mergedEvents.clear();
    if (loop.tryGatherPublishedSnapshotEventsInWindow(
            runtime.mergedMidiEvents.mergedEvents, winStart, winLen, &gatherPath) &&
        gatherPath == Loop::SnapshotWindowGatherPath::Snapshot) {
      applyNoteEditDeltaToCommittedWindow(runtime.mergedMidiEvents.mergedEvents,
                                          PlaybackCoverageInterval{winStart, winLen, loop.loopLengthTicks},
                                          track.getMidiChannel());
    } else {
      // A current source is required for composition. Keep the failure visible as
      // an empty build rather than mislabeling a stale root as current.
      runtime.mergedMidiEvents.mergedEvents.clear();
    }
    runtime.mergedMidiEvents.windowStartTick = winStart;
    runtime.mergedMidiEvents.windowLengthTicks = winLen;
  } else if (noteEditPreview) {
    // Legacy/stale-root NOTE_EDIT opens still own a materialized session projection.
    const MidiEventVec& preview = editManager.sessionMidiEvents();
    runtime.mergedMidiEvents.mergedEvents.assign(preview.begin(), preview.end());
    runtime.mergedMidiEvents.windowStartTick = 0;
    runtime.mergedMidiEvents.windowLengthTicks = loop.loopLengthTicks;
  } else {
    // **One path for every length** (DEC-198): one window length, one centring rule, one reuse rule above. The loop
    // bounds the window; it never selects a different policy.
    const uint32_t winLen = playbackWindowTicks;
    uint32_t winStart = 0;
    if (loop.loopLengthTicks > winLen) {
      winStart = playhead > (winLen / 2) ? playhead - (winLen / 2) : 0;
      if (winStart + winLen > loop.loopLengthTicks) winStart = loop.loopLengthTicks - winLen;
    }
    // **One source rule for every loop length.** The window is filled from exactly one source, chosen by *currency*,
    // never by length:
    //   * the published root, while it is **current** — an immutable membership, so it is only ever right when it
    //     represents the committed inventory we are about to play;
    //   * otherwise the **committed passes** — they are the authority an undo/redo/disable just moved, filtered by pass
    //     state, bounded by the same window and never by the pass inventory. A stale root is a cached answer, not a
    //     content authority: serving it kept an undone pass sounding for 72.4 s on the device
    //     (`captures/session_20261029_121423.log`, 31-bar slot) because a root can only gain rows, never lose them;
    //   * the DEC-143 sealed Record extent, for a take that has no root yet — it owns the source while it is sealed, so
    //     it is never mixed with the committed-pass set.
    const auto committedPassesServeWindow = [&]() -> bool {
      const uint8_t committedSlot = playbackRebuildSlotIndex(track, loop);
      if (committedSlot >= Config::MAX_LOOPS_PER_TRACK || !loop.hasCommittedPasses() ||
          runtime.pagedActive ||
          runtime.pagedOutcome != LoopPlaybackRuntime::PagedPlaybackOutcome::None) {
        // An over-capacity interval is the pager's answer (refused by name), never a reason to serve a pass copy.
        return false;
      }
      // The fallback establishes the source it is about to read (DEC-194), for this slot and revision only, instead of
      // requiring another lane to have retained it first. Retention is pass/chunk pins, idempotent for a repeat attempt.
      if (track.playbackWindowSourceIsCurrentForSlot(committedSlot)) {
        return true;
      }
      return track.retainPlaybackSourceForSlot(committedSlot, loop.playbackRevision,
                                              track.getPlaybackGeneration()) ==
          Track::PlaybackSourceRetainResult::Admitted;
    };
    // A root that exists but is not current is the **stale** case (an undo/redo/enable/disable moved the inventory
    // under it). Presence has to be read from the coverage stamp, not from `publishedSnapshotIdentity()` -- that
    // accessor deliberately answers null while a root is stale, which is exactly the state this rule is about. A slot
    // with no root at all keeps the DEC-143 sealed-extent and gather-miss paths below.
    const bool stalePublishedRoot =
        !loop.publishedSnapshotIsCurrent() &&
        loop.publishedSnapshotCoveredRevision() != kSnapshotCoverageUnset;
    if (stalePublishedRoot && committedPassesServeWindow()) {
      runtime.mergedMidiEvents.mergedEvents.clear();
      loop.copyEffectiveCommittedEventsInRange(runtime.mergedMidiEvents.mergedEvents, winStart, winLen);
      usedSnapshotIdentity = buildIdentity;
      servedFromCommittedPasses = true;
    } else if (loop.tryGatherPublishedSnapshotEventsInWindow(runtime.mergedMidiEvents.mergedEvents, winStart, winLen,
                                                              &gatherPath, nullptr, publishedRecordOverlay)) {
      usedSnapshotIdentity = buildIdentity;
    } else if (committedPassesServeWindow()) {
      // **The committed passes serve the window; a root is not a precondition.** Same copy as above: bounded by the
      // window, filtered by pass state.
      runtime.mergedMidiEvents.mergedEvents.clear();
      loop.copyEffectiveCommittedEventsInRange(runtime.mergedMidiEvents.mergedEvents, winStart, winLen);
      usedSnapshotIdentity = buildIdentity;
      servedFromCommittedPasses = true;
    } else {
      // An attempt that cannot serve is still the same identity: stamping it keeps the next maintenance turn from
      // re-walking the extent (DEC-143 bounded work). A root publication changes the identity, never this stamp.
      usedSnapshotIdentity = recordOverlayIdentity;
    }
    runtime.mergedMidiEvents.windowStartTick = winStart;
    runtime.mergedMidiEvents.windowLengthTicks = winLen;
  }
  runtime.mergedMidiEvents.builtFromRevision = windowRevision;
  // DEC-143: the identity is the published root when one supplies the window, and the **sealed extent** while the
  // published-record overlay serves it. Both are immutable objects, so an unchanged key means an unchanged window.
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
          : servedFromCommittedPasses
                ? RuntimeTimingTelemetry::PlaybackRebuildPath::CompatibilityPasses
                : (gatherPath == Loop::SnapshotWindowGatherPath::Snapshot
                 ? RuntimeTimingTelemetry::PlaybackRebuildPath::PublishedSnapshot
                 : (gatherPath == Loop::SnapshotWindowGatherPath::Overlay
                        ? RuntimeTimingTelemetry::PlaybackRebuildPath::StaleRootOverlay
                        : (gatherPath == Loop::SnapshotWindowGatherPath::RecordOverlay
                               ? RuntimeTimingTelemetry::PlaybackRebuildPath::PublishedRecordOverlay
                               : RuntimeTimingTelemetry::PlaybackRebuildPath::GatherMissHoldEmpty))),
      playbackBuildStartUs, loop.lastTickInLoop, playhead,
      static_cast<uint32_t>(runtime.mergedMidiEvents.mergedEvents.size()));
  mergedMidiEventsBuildInProgress = false;
  return true;
}

/// The handoff's **wire half**, FLASH-placed with the reconcile that calls it, in the order the existing closure owner
/// uses (`applyClosureSettlementForSlot`): the NoteOff goes to the immediate-send edge first (`midiHandler.sendMidiEvent`
/// — the owner `silenceSlotMidiOutput` and `applyClosureSettlementForSlot` already close through), and only then is the
/// obligation discharged through the ledger's **own** event path, the identity-exact resolution, so the obligation leaves
/// the same way it would have on the emission path. Discharging first would lose the obligation before the wire had its
/// chance; and because the discharge is what removes the Entry, a closure is never fired twice -- a later plan cannot
/// contain it, and a later note-off carrying that erased identity is refused as an orphan
/// ("closures that became obsolete SHALL be suppressed"). The ledger's channel is already the track's remapped playback
/// channel, which is why the closure needs no remap of its own.
TRACK_COLD_MEM size_t closePlannedHandoffClosures(
    ActiveNoteLedger& ledger, const ActiveNoteLedger::HandoffClosurePlan& plan, uint32_t tick) {
  size_t emitted = 0;
  for (size_t index = 0; index < plan.count; ++index) {
    const ActiveNoteLedger::HandoffClosurePlan::Entry& entry = plan.entries[index];
    MidiEvent close = MidiEvent::NoteOff(tick, entry.channel, entry.note, 0);
    close.noteId = entry.noteId;
    midiHandler.sendMidiEvent(close);
    ++emitted;
    (void)ledger.applyPlaybackEvent(entry.channel, close);
  }
  return emitted;
}

/// NOTE_EDIT owns only changed identities when opened on a current root. A window is not
/// whole-loop removal evidence, so close only ledger entries whose own EditManager row
/// is explicitly Hidden/Deleted; unrelated sustained notes remain open.
TRACK_INTERNAL_MEM __attribute__((noinline)) void reconcileDeletedNoteEditPreviewSoundings(
    LoopPlaybackRuntime& runtime, uint32_t currentTick) {
  const NoteEditCurrentState& state = noteEditCurrentStateForPlayback();
  const auto& rows = state.rows();
  ActiveNoteLedger::HandoffClosurePlan plan{};
  runtime.ledger.forEachActive([&](uint8_t channel, uint8_t note,
                                  const ActiveNoteLedger::Entry& entry) {
    if (entry.noteId == kInvalidNoteId || plan.count >= plan.entries.size()) return;
    const auto found = rows.find(entry.noteId);
    const NoteEditCurrentNoteState* row = found != rows.end() ? &found->second : nullptr;
    if (row == nullptr || (row->presence != NoteEditPresenceType::Hidden &&
                           row->presence != NoteEditPresenceType::Deleted)) return;
    auto& closure = plan.entries[plan.count++];
    closure.channel = channel;
    closure.note = note;
    closure.noteId = entry.noteId;
  });
  (void)closePlannedHandoffClosures(runtime.ledger, plan, currentTick);
}

/// The version handoff's ledger reconciliation, and the half of it that is audible. What this used to do was drop the
/// invalidated obligations **silently** (`eraseOpenNotesMissingFromCommittedNoteOns`): a note whose content the incoming
/// revision had removed stayed sounding on the wire, and the ledger no longer knew it, so nothing could ever close it.
/// The replacement's own note-ons are not the repair -- their identities are the *new* ones, and the ledger's
/// identity-exact NoteOff resolution refuses an unknown identity as an orphan. Task 4.5's other half is the same
/// predicate one step further: an obligation the incoming stream cannot discharge **before this pass ends** because its
/// own note-off was moved behind `servedThroughPhase` is closed here too, at the handoff's tick, instead of being left
/// to a walk that can no longer reach it and a wrap that would re-articulate it.
///
/// Ordering is structural, not a promise: every caller runs this **before** the walk that serves the incoming revision's
/// note-ons, so the required note-offs precede the replacement's note-ons by construction.
TRACK_INTERNAL_MEM __attribute__((noinline)) void reconcilePlaybackLedgerAfterFullLoopRebuild(
    Loop& loop, LoopPlaybackRuntime& runtime, uint32_t currentTick, uint32_t servedThroughPhase) {
  loop.replaceCommittedPlaybackNoteOnIdentities(runtime.mergedMidiEvents);
  if (!isFullLoopMergedPlaybackWindow(runtime.mergedMidiEvents, loop.loopLengthTicks)) {
    // A slice of the loop: its absence is not evidence, so nothing is closed and nothing is dropped.
    return;
  }
  const SessionMidiEventVec& incoming = runtime.mergedMidiEvents.mergedEvents;
  // The reconciliation's own **interpretive** rule comes first, and it is the rule the settlement already owns: absence
  // from the incoming content is evidence of removal only when that content is authoritative for the whole loop -- and an
  // **event-empty** window says only "no new events", so it closes nothing and drops nothing. The obligations stay
  // alive (the spec's "closure obligations SHALL stay alive"), and a boundary that does discharge them -- a stop, a
  // clear, or a later handoff that carries content -- closes them then, rather than the handoff forgetting them here.
  if (!handoffAbsenceMeansRemoval(/*incomingIsWholeLoop=*/true, /*incomingIsEmpty=*/incoming.empty())) {
    return;
  }
  const ActiveNoteLedger::HandoffClosurePlan plan = runtime.ledger.planHandoffClosures(
      incoming.data(), incoming.size(), servedThroughPhase, loop.loopLengthTicks);
  (void)closePlannedHandoffClosures(runtime.ledger, plan, currentTick);
}

/// Task 4.5 (the moving closure): **the phase this pass has already served on `loop`** — the floor a closure must clear
/// to still be reachable, expressed in the walk's own terms so that the handoff cannot decide reachability differently
/// from `advancePlaybackCursor`. `reanchorPlaybackIndex` points the cursor at the first event past `loop.lastTickInLoop`
/// (and the prepared pair resolves the identical term as its `publicationPhase`), so an event at or behind that phase is
/// skipped for the rest of the pass. `UINT32_MAX` means **nothing has been served yet** — there is no position, or the
/// playhead wrapped backward, so the walk serves the whole pass again and every closure is still reachable.
TRACK_INTERNAL_MEM __attribute__((noinline)) uint32_t playbackClosureServeFloor(
    const Track& track, const Loop& loop, uint32_t currentTick) {
  if (loop.lastTickInLoop == UINT32_MAX || loop.loopLengthTicks == 0) {
    return UINT32_MAX;
  }
  const uint32_t turnPhase = IntervalProjection::tickPhaseInProjectionCycle(
      currentTick, track.getProjectionCycleStartTick(), loop.loopLengthTicks);
  if (IntervalProjection::didDisplayPlayheadWrapBackward(turnPhase, loop.lastTickInLoop,
                                                         loop.loopStartTick, loop.loopLengthTicks)) {
    return UINT32_MAX;
  }
  return loop.lastTickInLoop;
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

PlaybackEventStream makeOverlayPlaybackStream(FixedPlaybackStreamCtx& ctx) {
  return PlaybackEventStream{&ctx, overlayPlaybackStreamSize, fixedPlaybackStreamValid,
                             overlayPlaybackStreamEventAt, mergedPlaybackStreamPhase};
}

const SessionMidiEventVec* buildMergedPlaybackFallbackCold(
    Track& track, Loop& loop, LoopPlaybackRuntime& runtime, uint32_t currentTick) {
  const bool rebuilt =
      ensurePlaybackMergedMidiEventsBuilt(track, loop, runtime, false, currentTick);
  if (rebuilt) {
    if (editManager.isNoteEditActive() && trackManager.isSelectedTrack(track) &&
        loop.publishedSnapshotIsCurrent()) {
      reconcileDeletedNoteEditPreviewSoundings(runtime, currentTick);
    } else {
      reconcilePlaybackLedgerAfterFullLoopRebuild(loop, runtime, currentTick,
                                                  playbackClosureServeFloor(track, loop, currentTick));
    }
  }
#if SC_PLAYBACK_READ_WITNESS_ENABLED
  track.observePlaybackSnapshotAvailability(playbackRebuildSlotIndex(track, loop), currentTick);
#endif
  return &runtime.mergedMidiEvents.mergedEvents;
}

/// DEC-241: the tick's merged-fallback route. On the main-loop tick (external clock) it is the cold build above,
/// unchanged. In the internal-clock **interrupt** it is consume-only: a current, order-clean fallback that nobody is
/// rebuilding is served; anything else is owed to `TrackManager::serviceOwedTickPlaybackFallbacks` and the tick gets no
/// merged source (the caller contains the turn). No build, allocation, incremental admit, order rebuild or publication.
TRACK_COLD_MEM const SessionMidiEventVec* selectMergedPlaybackFallbackForTick(
    Track& track, Loop& loop, LoopPlaybackRuntime& runtime, uint8_t slotIndex, uint32_t currentTick) {
  PlaybackTickContext::recordMergedRouteReject(playbackRebuildTrackIndex(track), slotIndex, currentTick);
  if (!PlaybackTickContext::inInterrupt()) {
    return buildMergedPlaybackFallbackCold(track, loop, runtime, currentTick);
  }
  bool consumable = !mergedMidiEventsBuildInProgress && !PlaybackTickContext::gState.servicing && !loop.playbackOrderDirty;
  if (consumable) {
    const MergedFallbackKey key = mergedFallbackTickKey(track, loop, currentTick);
    consumable = mergedFallbackReuseHolds(runtime, key.windowRevision, key.buildIdentity, key.playhead, false);
  }
  if (consumable) {
    return &runtime.mergedMidiEvents.mergedEvents;
  }
  const uint8_t trackIndex = playbackRebuildTrackIndex(track);
  if (trackIndex < Config::NUM_TRACKS && slotIndex < 8) {
    PlaybackTickContext::gState.owed[trackIndex] =
        static_cast<uint8_t>(PlaybackTickContext::gState.owed[trackIndex] | (1u << slotIndex));
  }
  PlaybackTickContext::gState.misses = PlaybackTickContext::gState.misses + 1u;
  return nullptr;
}

PlaybackEventStream makeCapturePlaybackStream(Loop& loop) {
  return PlaybackEventStream{&loop, capturePlaybackStreamSize, nullptr,
                             capturePlaybackStreamEventAt, capturePlaybackStreamPhase};
}

/// Task 3.7a (I3c-a): **the paged emission adapter.** `advancePlaybackCursor` reads `size` **once per turn** and then
/// walks `eventAt`/`consume` per index, so this adapter answers all three from the ring's **own ordinal space** -- no
/// staging copy, no per-slot buffer, no allocation, and nothing produced, waited for or inferred:
///
///   - `passBaseOrdinal + cursor` is the ring ordinal the walk is asking for (the walk's index restarts every pass,
///     the ring's ordinal does not), which is the "stream index -> page base" mapping the paged path needed;
///   - `size` is the ring's **published** count minus that pass base: the walk is bounded by what the producer has
///     handed over, so a ring that is momentarily empty simply ends the turn and the deferred lane's next turn is what
///     continues it (`dryTurns` counts those turns);
///   - `consume` advances the emitted position and **retires the pages behind it** (`retireThroughOrdinal`), which is
///     retirement by emission position rather than by a copy-out call.
TRACK_COLD_MEM size_t pagedEmissionStreamSize(const void* ctx) {
  const auto* stream = static_cast<const PagedEmissionStreamCtx*>(ctx);
  if (stream == nullptr || stream->runtime == nullptr) {
    return 0;
  }
  LoopPlaybackRuntime::PagedEmissionState& state =
      const_cast<LoopPlaybackRuntime::PagedEmissionState&>(stream->runtime->pagedEmission);
  if (!state.bound) {
    return 0;
  }
  // The emission is what this delivery now serves: from here the producer re-arms a pass instead of completing once.
  state.emitting = true;
  ++state.reads;
  const WindowPagedStream& ring = stream->runtime->pagedWindow;
  const uint32_t published = ring.producedOrdinal();
  // Task 3.7a (I3c-b/c): **the emitting pass's own content is the bound.** The producer runs ahead into the *next* pass
  // (that is the lead across the wrap), and every pass has the same tick space -- so serving "everything published"
  // would emit the next pass's events inside this pass's window. When the producer has recorded an end beyond this
  // pass's base, that end is this pass's content end and the walk is bounded by it. Otherwise no end is known yet (the
  // producer is still producing this pass), and what is published *is* this pass.
  const uint32_t passEnd = state.passContentOrdinal > state.passBaseOrdinal
      ? state.passContentOrdinal
      : published;
  const uint32_t servable = published < passEnd ? published : passEnd;
  if (servable <= state.passBaseOrdinal) {
    ++state.dryTurns;
    return 0;
  }
  // A read that finds the ring empty **ahead of the walk** (everything published is already consumed) is the dry the
  // delivery's proof cares about: the frame it serves had nothing to hand over.
  if (servable <= state.consumedOrdinal) {
    ++state.dryTurns;
  }
  return servable - state.passBaseOrdinal;
}

TRACK_COLD_MEM bool pagedEmissionStreamValid(const void* ctx, uint16_t cursor) {
  const auto* stream = static_cast<const PagedEmissionStreamCtx*>(ctx);
  if (stream == nullptr || stream->runtime == nullptr) {
    return false;
  }
  (void)cursor;
  return stream->runtime->pagedEmission.bound;
}

TRACK_COLD_MEM const MidiEvent& pagedEmissionStreamEventAt(const void* ctx, uint16_t cursor) {
  const auto* stream = static_cast<const PagedEmissionStreamCtx*>(ctx);
  const LoopPlaybackRuntime::PagedEmissionState& state = stream->runtime->pagedEmission;
  const MidiEvent* event =
      stream->runtime->pagedWindow.eventAtOrdinal(state.passBaseOrdinal + static_cast<uint32_t>(cursor));
  if (event != nullptr) {
    return *event;
  }
  // Unreachable while `size` bounds the walk to the ring's own published ordinals, so this is a **guard, not a path**:
  // it returns an `InvalidType` event the walk cannot mistake for music, and it costs no RAM1 (the fallback lives in
  // the runtime's own external state).
  return const_cast<LoopPlaybackRuntime::PagedEmissionState&>(state).unreachableFallback;
}

TRACK_COLD_MEM void pagedEmissionStreamConsume(const void* ctx) {
  const auto* stream = static_cast<const PagedEmissionStreamCtx*>(ctx);
  if (stream == nullptr || stream->runtime == nullptr) {
    return;
  }
  LoopPlaybackRuntime::PagedEmissionState& state =
      const_cast<LoopPlaybackRuntime::PagedEmissionState&>(stream->runtime->pagedEmission);
  ++state.consumedOrdinal;
  // Retirement by emission position: the pages the walk has passed return to the ring, so the producer can refill
  // them with the next pass's content -- bounded by the ring's page count.
  const_cast<WindowPagedStream&>(stream->runtime->pagedWindow).retireThroughOrdinal(state.consumedOrdinal);
}

TRACK_COLD_MEM PlaybackEventStream makePagedEmissionStream(PagedEmissionStreamCtx& ctx) {
  return PlaybackEventStream{&ctx, pagedEmissionStreamSize, pagedEmissionStreamValid,
                             pagedEmissionStreamEventAt, mergedPlaybackStreamPhase,
                             pagedEmissionStreamConsume};
}

/// Task 3.7a (I3c-a): **is this slot's paged delivery fit to be selected as the emission source?** Every answer is
/// named, and the two that are not `Admissible` are the honest ones:
///
/// - `CannotStraddleTheWrap`: the interval spans the whole loop, so its wrap *is* the production window and no pass
///   can be prepared while the playhead is elsewhere. `ExecutionBounds.md` row 27's contract row is explicit -- "if
///   not provable, **reject active windowed selection**" -- so this is refused rather than half-served, and the merged
///   fallback keeps playing the slot exactly as it does today.
/// - `Unproven`: the delivery stands, but no **dry-free pass** has been observed yet. A dry pass is the producer
///   falling behind the playhead, and a late page loses the events the frame already passed -- so probation, not
///   optimism, decides. The deferred lane's own turns are what complete a pass; the gate reads this same predicate.
TRACK_COLD_MEM void Track::servicePagedPassBoundaryForSlot(uint8_t slotIndex, bool wrappedBackward,
                                                             bool atLoopStart, uint32_t prevTickInLoop,
                                                             uint32_t tickInLoop) {
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr || !runtime->pagedEmission.bound) {
    return;
  }
  // The interval's own start phase is the pass boundary (or the loop's wrap, when the interval starts at the loop's
  // start). Judged **whenever a delivery is bound**, not only when it is selected: the judgement is the admission's
  // input, so gating it on the admission would be circular.
  const uint32_t intervalStart = runtime->pagedInterval.startPhase;
  const bool passBoundary = intervalStart == 0
      ? (wrappedBackward || atLoopStart)
      : IntervalProjection::didPlaybackEventCross(false, prevTickInLoop, intervalStart, tickInLoop);
  if (passBoundary) {
    runtime->pagedEmission.notePassBoundary(runtime->pagedWindow.producedOrdinal());
  }
}

TRACK_COLD_MEM void Track::servicePagedPassBoundaryForTurn(uint8_t slotIndex, uint32_t currentTick) {
  // Task 3.7a (I5): **the admission's input, computed off-ITCM.** The turn's own projection is computed again by its
  // caller at the place it always was, so nothing here is a value the clock path must keep live across the merged build
  // (keeping them live was the +256 B the guard refused). Nothing is judged unless a delivery is bound, so every other
  // tick pays one load and one call -- and a delivery's own pass boundary is judged **before** the selection that reads
  // the admission it produces.
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr || !runtime->pagedEmission.bound) {
    return;
  }
  Loop& loop = getLoop(slotIndex);
  const uint32_t tickInLoop = IntervalProjection::tickPhaseInProjectionCycle(
      currentTick, projectionCycleStartTick, loop.loopLengthTicks);
  const bool wrappedBackward = IntervalProjection::didDisplayPlayheadWrapBackward(
      tickInLoop, loop.lastTickInLoop, loop.loopStartTick, loop.loopLengthTicks);
  const uint32_t prevTickInLoop = loop.lastTickInLoop;
  const bool atLoopStart = IntervalProjection::isPlaybackCatchUpWindow(prevTickInLoop, tickInLoop);
  servicePagedPassBoundaryForSlot(slotIndex, wrappedBackward, atLoopStart, prevTickInLoop, tickInLoop);
}


TRACK_COLD_MEM void Track::seekBridgeWindowPastServicedInterval(const RuntimePlaybackView* window,
                                                               uint32_t prevTickInLoop,
                                                               uint32_t tickInLoop, Loop& loop) {
  if (window == nullptr) {
    return;
  }
  const auto& storage = window->storage;
  size_t first = 0;
  size_t end = storage.eventCount;

  if (prevTickInLoop != UINT32_MAX && prevTickInLoop <= tickInLoop) {
    while (first < end) {
      const size_t middle = first + (end - first) / 2;
      if (storage.phases[storage.order[middle]] <= prevTickInLoop) {
        first = middle + 1;
      } else {
        end = middle;
      }
    }
  }


  loop.nextEventIndex = first;
}

TRACK_COLD_MEM void Track::resolveFixedPlaybackSourceForTurn(
    uint8_t slotIndex, const Loop& loop, bool eligible, PlaybackViewSourceIdentity& sourceOut,
    const FixedPlaybackBridgeStorage*& bridgeOut, const RuntimePlaybackView*& viewOut) {
  sourceOut = PlaybackViewSourceIdentity{loop.playbackRevision, playbackGeneration,
                                         loop.publishedSnapshotIdentity()};
  bridgeOut = nullptr;
  viewOut = nullptr;
  if (!eligible) {
    return;
  }
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return;
  }
  bridgeOut = runtime->selectedFullLoopBridge(sourceOut, loop.loopLengthTicks);
  viewOut = bridgeOut != nullptr ? runtime->fullLoopBridgeBase(*bridgeOut, loop.loopLengthTicks)
                                 : runtime->publishedFullLoopView(sourceOut, loop.loopLengthTicks);
}
#if PLAYBACK_WINDOW_ROLLING_ENABLED
// This qualification-only selector runs on the clock path but is deliberately placed in FLASH to meet the RAM1 bank
// budget. Its placement is observable in timing, so no timing equivalence/qualification is claimed here.
FLASHMEM void Track::resolveQualificationPlaybackSourceForTurn(
    uint8_t slotIndex, Loop& loop, bool eligible, uint32_t currentTick,
    PlaybackViewSourceIdentity& sourceOut, const FixedPlaybackBridgeStorage*& bridgeOut,
    const RuntimePlaybackView*& viewOut, bool& rollingViewSelectedOut, bool& overlayLiveSelectedOut) {
  rollingViewSelectedOut = false;
  overlayLiveSelectedOut = false;
  // playback-tick-consume-only phase 2: this turn's first failed predicate starts unset (two byte stores).
  PlaybackTickContext::gState.turnReject = static_cast<uint8_t>(PlaybackTickContext::RouteReject::NotEvaluated);
  PlaybackTickContext::gState.turnCause = 0;
  // Task 3.15d (slice 2): **NOTE_EDIT is served by the preview's own interval** -- see
  // `resolveNoteEditPreviewSourceForTurn` for the contract and the hazards. One out-of-line FLASH-resident call keeps
  // the ITCM resolver's own edge bounded: the caller only takes the committed reader's frame-bound `return`.
  if (resolveNoteEditPreviewSourceForTurn(slotIndex, loop, currentTick, sourceOut, bridgeOut, viewOut)) {
    rollingViewSelectedOut = true;
    return;
  }
  // Task 3.15e slice 3 (C2/C4): the live overdub window, bound as a **live source**. Only [0, readableEventCount) is
  // exposed; the storage may still be extended off-clock; no role is taken. The base identity is the stable selector
  // (the capture generation drifts per append). The shared cursor is anchored to the last processed phase, O(log n).
  if (slotIndex == activeLoopIndex && isOverdubbing() && !isJamPlaybackActive() && !isJamming() &&
      !editManager.isNoteEditActive() &&
#if defined(PIO_UNIT_TEST_NATIVE)
      !sOverlayEdgeDisabledForTest
#else
      true
#endif
      ) {
    LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
    const RuntimePlaybackView* live = runtime != nullptr ? runtime->overdubOverlayCandidate() : nullptr;
    const uint32_t livePhase = IntervalProjection::tickPhaseInProjectionCycle(
        currentTick, getProjectionCycleStartTick(), loop.loopLengthTicks);
    if (live != nullptr && live->readableEventCount != 0 && live->passOriginsReady &&
        !live->canonicalRanksReady && live->source.origin == PlaybackWindowOrigin::Capture &&
        live->source.playbackRevision == loop.playbackRevision &&
        live->source.sourceGeneration == playbackGeneration &&
        live->coverage.isValid() && live->coverage.loopLengthTicks == loop.loopLengthTicks &&
        live->coverage.contains(livePhase)) {
      if (loop.lastTickInLoop == UINT32_MAX) {
        loop.nextEventIndex = 0;
      } else {
        size_t first = 0, last = live->readableEventCount;
        while (first < last) {
          const size_t middle = first + (last - first) / 2;
          const auto& event = live->storage.events[live->storage.order[middle]];
          const uint32_t eventPhase =
              IntervalProjection::playbackEventPhase(event.tick, loop.loopLengthTicks);
          if (eventPhase <= loop.lastTickInLoop) first = middle + 1;
          else last = middle;
        }
        loop.nextEventIndex = static_cast<uint16_t>(first);
      }
      sourceOut = live->source;
      bridgeOut = nullptr;
      viewOut = live;
      rollingViewSelectedOut = true;
      overlayLiveSelectedOut = true;
#if defined(PIO_UNIT_TEST_NATIVE)
      ++sOverlayLiveSelectionsForTest;
#endif
      return;
    }
  }
  // A qualified live interval is the same immutable fixed-storage reader as
  // committed playback. Input changes only private demand, never served bytes.
  if (slotIndex == activeLoopIndex && isRecording() && !isJamPlaybackActive() &&
      !isJamming() && !hasQueuedPlaybackStart() && !editManager.isNoteEditActive() &&
      loop.capture.phase == CapturePhase::Record && !loop.hasCommittedPasses()) {
    auto* runtime = playbackRuntime.slotIfAllocated(slotIndex);
    auto* set = runtime ? runtime->viewSetIfPrewarmed() : nullptr;
    const auto clock = clockManager.getPlaybackDeadlineClock();
    if (set && clock.epoch && clock.tick == currentTick && set->lifecycle.current < set->views.size() &&
        liveRecordRecipeMatches(runtime->pendingViewRequest_, loop, *runtime,
                                playbackGeneration, getProjectionCycleStartTick())) {
      bool missed = false;
      {
      PlaybackSourceReadGuard readerGuard;
      auto& request = runtime->pendingViewRequest_;
      auto& frame = runtime->rolling;
      const auto valid = [&](const RuntimePlaybackView& view) {
        return view.complete && !view.rawCoveragePrepared && !view.firstRecordReturnWindow &&
            view.storageGeneration && view.source.origin == PlaybackWindowOrigin::Capture &&
            view.source.sourceGeneration == playbackGeneration && !view.source.snapshotIdentity &&
            view.source.captureContentGeneration && view.coverage.isValid() &&
            view.coverage.startPhase % Config::TICKS_PER_BAR == 0 &&
            view.coverage.lengthTicks == Config::TICKS_PER_BAR &&
            view.coverage.loopLengthTicks % Config::TICKS_PER_BAR == 0;
      };
      const auto& old = set->views[set->lifecycle.current];
      const bool initial = frame.phase == LoopPlaybackRuntime::RollingPhase::Idle &&
          (!request.clockEpoch || request.clockEpoch == clock.epoch) && !request.positionInvalidated;
      const bool serving = frame.phase == LoopPlaybackRuntime::RollingPhase::Serving &&
          frame.epoch == clock.epoch && frame.generation == playbackGeneration &&
          frame.loopLength == old.coverage.loopLengthTicks && frame.boundView == set->lifecycle.current &&
          frame.boundStorageGeneration == old.storageGeneration && currentTick >= frame.lastTick &&
          frame.projectionStart == getProjectionCycleStartTick();
      if (valid(old) && old.published && (initial || serving)) {
        uint32_t phase = IntervalProjection::tickPhaseInProjectionCycle(
            currentTick, getProjectionCycleStartTick(), old.coverage.loopLengthTicks);
        const bool boundary = serving && phase % Config::TICKS_PER_BAR == 0 && currentTick > frame.lastTick;
        bool adopted = false;
        if (boundary && request.active && !request.positionInvalidated &&
            request.clockEpoch == clock.epoch && request.deadlineTick && clock.tick >= request.deadlineTick &&
            phase == request.coverage.startPhase &&
            (request.coverage.loopLengthTicks == frame.loopLength || phase == 0) &&
            set->lifecycle.ready < set->views.size()) {
          const auto& ready = set->views[set->lifecycle.ready];
          const auto& frozen = runtime->frozenViewRequest;
          if (valid(ready) && !ready.published && ready.source == request.source &&
              ready.source.captureContentGeneration == loop.captureContentGeneration() &&
              ready.coverage.startPhase == request.coverage.startPhase &&
              ready.coverage.lengthTicks == request.coverage.lengthTicks &&
              ready.coverage.loopLengthTicks == request.coverage.loopLengthTicks &&
              frozen.coverage.startPhase == request.coverage.startPhase &&
              frozen.coverage.loopLengthTicks == request.coverage.loopLengthTicks &&
              liveRecordRecipeMatches(frozen, loop, *runtime, playbackGeneration, getProjectionCycleStartTick()) &&
              frozen.work.admittedTick < clock.tick && adoptPreparedPlaybackWindow(slotIndex)) {
            if (ready.coverage.loopLengthTicks != frame.loopLength) {
              // New geometry starts at this actual edge, not at an incongruent
              // former cycle anchor. Raw Record coordinates are untouched.
              projectionCycleStartTick = static_cast<int32_t>(currentTick);
              request.liveProjectionStart = projectionCycleStartTick;
              phase = 0;
            }
            getLoop(slotIndex).nextEventIndex = 0;
            request.active = false;
            runtime->frozenViewRequest = {};
            runtime->lagState.missedTraversals = 0;
            runtime->lagState.breachReported = false;
            runtime->deadlineContractFaultLatched = false;
            adopted = true;
          }
        }
        if (boundary && request.active && request.deadlineTick && clock.tick >= request.deadlineTick && !adopted) {
          const uint64_t misses = 1 + (clock.tick - request.deadlineTick) / frame.loopLength;
          if (misses > request.countedMisses) {
            request.countedMisses = uint16_t(std::min<uint64_t>(UINT16_MAX, misses));
            missed = true;
          }
        }
        const auto& current = set->views[set->lifecycle.current];
        frame.boundView = set->lifecycle.current;
        frame.boundStorageGeneration = current.storageGeneration;
        frame.epoch = clock.epoch;
        frame.generation = playbackGeneration;
        frame.loopLength = current.coverage.loopLengthTicks;
        frame.projectionStart = getProjectionCycleStartTick();
        frame.startPhase = current.coverage.startPhase;
        frame.deadlineTick = 0;
        frame.phase = LoopPlaybackRuntime::RollingPhase::Serving;
        frame.lastTick = currentTick;
        if (current.coverage.contains(phase)) {
          sourceOut = current.source;
          bridgeOut = nullptr;
          viewOut = &current;
          rollingViewSelectedOut = true;
        }
      }
      } // Fixed reader metadata only; miss reporting remains interruptible.
      if (missed) noteMissedTraversalForSlot(slotIndex, getLoop(slotIndex));
      // Retain the next distinct interval at the reader edge itself. Main may
      // not run before that interval is due; late service must inherit its debt.
      if (runtime->rolling.phase == LoopPlaybackRuntime::RollingPhase::Serving)
        retainLiveRecordDemand(*runtime, loop, playbackGeneration, getProjectionCycleStartTick(), clock);
      if (rollingViewSelectedOut) return;
    }
  }
  // E2 qualification: a stopped Record job may own a complete frozen Capture
  // successor while the whole checkpoint is still private. This is the same
  // per-slot window set and MIDI cursor as the committed rolling route; no
  // source read or build runs on this edge. The raw first bar remains the
  // compatibility source until the exact completed successor is eligible.
  // A live successor overdub still reads the published Record base. Its prepared frozen successor
  // must remain adoptable at the same boundary; OVERDUBBING changes capture ownership, not this reader.
  if (slotIndex == activeLoopIndex &&
      (eligible || (isOverdubbing() && !editManager.isNoteEditActive())) &&
      (isPlaying() || isOverdubbing()) &&
      !isJamPlaybackActive() && !isJamming() && !hasQueuedPlaybackStart()) {
    LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
    const CaptureStopPresentation* presentation = captureStopPresentationForSlot(slotIndex);
    if (runtime && presentation != nullptr && hasRecordReaderOwnerForSlot(slotIndex) &&
        presentation->slot == slotIndex && runtime->hasBoundRecordReturnWindowForPendingJob()) {
      RuntimePlaybackViewSource frozen{};
      PlaybackViewSourceIdentity receipt{};
      const PlaybackDeadlineClock clock = clockManager.getPlaybackDeadlineClock();
      if (clock.epoch != 0 && clock.tick == currentTick &&
          qualifiedFrozenRecordCaptureSourceForSlot(slotIndex, presentation->interval, frozen, receipt)) {
        auto* set = runtime->viewSetIfPrewarmed();
        const uint32_t phase = IntervalProjection::tickPhaseInProjectionCycle(
            currentTick, getProjectionCycleStartTick(), loop.loopLengthTicks);
        auto& frame = runtime->rolling;
        // Root publication may advance playbackRevision while the frozen job
        // still owns this copied interval. Preserve the view's own revision:
        // equality to today's committed revision would silently drop a valid
        // pre-publication reader before the root-backed successor is ready.
        const auto fromThisFrozenJob = [&](const RuntimePlaybackView& view) {
          return view.source.origin == PlaybackWindowOrigin::Capture &&
              view.source.sourceGeneration == receipt.sourceGeneration &&
              view.source.captureContentGeneration == receipt.captureContentGeneration &&
              view.source.snapshotIdentity == nullptr &&
              view.coverage.loopLengthTicks == loop.loopLengthTicks;
        };
        // Task 3.15g: the root-return **adoption diagnostics**, carried on the existing `win_frozen_readiness` receipt
        // (one family, one Tier-A prefix, no new format). The mask/answer computation lives in a cold (flash) helper so
        // this clock-path TU costs no ITCM; emission is transition-cadenced inside the helper.
        const auto reportRootReturnAdoption = [&](bool attempted, bool adopted, uint8_t why) {
          reportRootReturnAdoptionForSlot(slotIndex, loop, phase, receipt.captureContentGeneration, attempted, adopted,
                                          why);
        };
        // The decision **as it stands**, before any attempt: the stalled state never reaches an attempt, so this row is
        // what names the standing predicate (transition-cadenced in the helper).
        reportRootReturnAdoption(false, false, 0);
        const auto attemptRootReturnAdoption = [&](uint8_t slot) {
          uint8_t adoptWhy = 0;
          const bool adopted = adoptPreparedPlaybackWindow(slot, &adoptWhy);
          reportRootReturnAdoption(true, adopted, adoptWhy);
          return adopted;
        };
        if (set && frame.phase == LoopPlaybackRuntime::RollingPhase::Serving &&
            frame.epoch == clock.epoch && frame.generation == playbackGeneration &&
            frame.loopLength == loop.loopLengthTicks && frame.loopLength != 0 &&
            (uint32_t(getProjectionCycleStartTick()) - uint32_t(frame.projectionStart)) % frame.loopLength == 0 &&
            frame.boundView == set->lifecycle.current && frame.boundView < set->views.size() &&
            currentTick >= frame.lastTick) {
          const auto& current = set->views[frame.boundView];
          // The matching live handoff can cover a whole period. It must still
          // yield at a real boundary to the completed current-root successor;
          // root publication alone cannot retire the job's source responsibility.
          if (currentTick > frame.lastTick && loop.publishedSnapshotIsCurrent() &&
              current.complete && current.published && fromThisFrozenJob(current) &&
              set->lifecycle.ready < set->views.size()) {
            const auto& ready = set->views[set->lifecycle.ready];
            if (ready.complete && !ready.published && !ready.rawCoveragePrepared &&
                ready.source.origin == PlaybackWindowOrigin::Committed &&
                ready.source.playbackRevision == loop.playbackRevision &&
                ready.source.sourceGeneration == playbackGeneration &&
                ready.source.snapshotIdentity == loop.publishedSnapshotIdentity() &&
                ready.coverage.loopLengthTicks == loop.loopLengthTicks &&
                ready.coverage.startPhase == phase && attemptRootReturnAdoption(slotIndex)) {
              getLoop(slotIndex).nextEventIndex = 0;
              frame.boundView = set->lifecycle.current;
              const auto& adopted = set->views[frame.boundView];
              frame.boundStorageGeneration = adopted.storageGeneration;
              frame.lastTick = currentTick;
              sourceOut = adopted.source;
              bridgeOut = nullptr;
              viewOut = &adopted;
              rollingViewSelectedOut = true;
              return;
            }
          }
          if (current.complete && current.published && !current.rawCoveragePrepared &&
              current.storageGeneration == frame.boundStorageGeneration &&
              fromThisFrozenJob(current) && current.coverage.contains(phase)) {
            frame.lastTick = currentTick;
            sourceOut = current.source;
            bridgeOut = nullptr;
            viewOut = &current;
            rollingViewSelectedOut = true;
            return;
          }
        }
        const bool servingSuccessorBoundary = set &&
            frame.phase == LoopPlaybackRuntime::RollingPhase::Serving &&
            frame.epoch == clock.epoch && frame.generation == playbackGeneration &&
            frame.loopLength == loop.loopLengthTicks && frame.loopLength != 0 &&
            (uint32_t(getProjectionCycleStartTick()) - uint32_t(frame.projectionStart)) % frame.loopLength == 0 &&
            frame.boundView == set->lifecycle.current && frame.boundView < set->views.size() &&
            currentTick >= frame.lastTick &&
            set->views[frame.boundView].complete && set->views[frame.boundView].published &&
            set->views[frame.boundView].storageGeneration == frame.boundStorageGeneration &&
            fromThisFrozenJob(set->views[frame.boundView]) &&
            !set->views[frame.boundView].coverage.contains(phase);
        if (set && (frame.phase == LoopPlaybackRuntime::RollingPhase::Idle || servingSuccessorBoundary) &&
            set->lifecycle.ready < set->views.size()) {
          const auto& ready = set->views[set->lifecycle.ready];
          if (ready.complete && !ready.published && !ready.rawCoveragePrepared &&
              ready.storageGeneration != 0 && fromThisFrozenJob(ready) &&
              ready.coverage.loopLengthTicks == loop.loopLengthTicks &&
              ready.coverage.startPhase == phase && ready.coverage.contains(phase) &&
              attemptRootReturnAdoption(slotIndex)) {
            getLoop(slotIndex).nextEventIndex = 0;
            frame.boundView = set->lifecycle.current;
            frame.boundStorageGeneration = set->views[frame.boundView].storageGeneration;
            frame.epoch = clock.epoch;
            frame.generation = playbackGeneration;
            frame.loopLength = loop.loopLengthTicks;
            frame.projectionStart = getProjectionCycleStartTick();
            frame.startPhase = phase;
            frame.deadlineTick = 0;
            frame.lastTick = currentTick;
            frame.phase = LoopPlaybackRuntime::RollingPhase::Serving;
            sourceOut = set->views[frame.boundView].source;
            bridgeOut = nullptr;
            viewOut = &set->views[frame.boundView];
            rollingViewSelectedOut = true;
            return;
          }
        }
      }
    }
  }
  // C3 (DEC-116) layered qualification: the active slot's leg is `eligible`'s and is unchanged. A
  // **non-active** slot is read from its own completed cold one-bar frame when it holds one, so the second
  // sounding slot gets the same rolling reader instead of only the compatibility emitter. The frame is
  // slot-owned and the enablement gate that admitted this turn is the caller's own
  // (`PlaybackMidiOutput::engineShouldRun(slotEnabled[...])`), so no track-index lookup, no new authority and
  // nothing new on the clock edge is added here.
  const bool rollingReaderEligible =
      eligible || coldLayeredSlotFrameForEmission(playbackRuntime.slotIfAllocated(slotIndex), loop, slotIndex,
                                                  activeLoopIndex);
  if (rollingReaderEligible && loop.loopLengthTicks == Config::TICKS_PER_BAR) {
    const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
    if (runtime && runtime->rolling.deadlineTick == 0 &&
        (runtime->rolling.phase == LoopPlaybackRuntime::RollingPhase::Ready ||
         runtime->rolling.phase == LoopPlaybackRuntime::RollingPhase::Serving)) {
      viewOut = resolveRollingPlaybackWindow(slotIndex, currentTick,
          clockManager.getPlaybackDeadlineClock()).view;
      if (viewOut) {
        sourceOut = viewOut->source;
        bridgeOut = nullptr;
        rollingViewSelectedOut = true;
        return;
      }
    }
  }
  resolveFixedPlaybackSourceForTurn(slotIndex, loop, eligible, sourceOut, bridgeOut, viewOut);
  if (!eligible && PlaybackTickContext::gState.turnReject ==
                       static_cast<uint8_t>(PlaybackTickContext::RouteReject::NotEvaluated)) {
    // Phase 2 (report-only): why the committed reader was not eligible, when no rolling predicate named it.
    using PlaybackTickContext::RouteReject;
    PlaybackTickContext::noteTurnReject(slotIndex != activeLoopIndex ? RouteReject::InactiveSlot
                                        : editManager.isNoteEditActive() ? RouteReject::NoteEditActive
                                        : isOverdubbing() ? RouteReject::OverdubNoLiveWindow
                                                          : RouteReject::NotPlaying);
  }
  if (bridgeOut != nullptr) PlaybackTickContext::noteTurnReject(PlaybackTickContext::RouteReject::Served);
  if (viewOut == nullptr && eligible) {
    const RuntimePlaybackView* rollingView =
        resolveRollingPlaybackWindow(slotIndex, currentTick, clockManager.getPlaybackDeadlineClock()).view;
    if (rollingView != nullptr) {
      // Transfer the whole reader tuple: a positioned interval can retain an older revision.
      sourceOut = rollingView->source;
      bridgeOut = nullptr;
      viewOut = rollingView;
      rollingViewSelectedOut = true;
    }
  }
}

/// Task 3.15d (slice 2): **NOTE_EDIT is served by the preview's own interval.** Slice 1 composed and published a
/// `Preview`-origin window for the interval the committed route reads; the audible route still served the merged
/// reader, so the preparation never reached the wire. This is the committed reader's own shape -- a frame-bound view,
/// a coverage test, and **one** cursor reset per handoff -- with the three terms the session owns:
///
///   * currency is the **session's** revision, not the lane's frozen one: a window whose revision the session has left
///     must not sound even though it stays readable for display (the lane's retention contract);
///   * adoption is at a coverage boundary: a successor that covers the playhead, or the session's first publication,
///     which already covers it;
///   * the adoption runs the NOTE_EDIT selective closure, because the preview replaced the merged reader's rebuild,
///     which is where a removed identity was closed.
///
/// The merged reader stays the named compatibility fallback: when no current `Preview` window covers the playhead this
/// returns false and the turn resolves exactly as it did before the seam existed.
///
/// `ledgerBoundary` is deliberately **not** an admission gate here. A summary is exact (an empty one means "no open
/// note", never "unconstrained") and is captured at build time, so requiring a match would refuse every adoption after
/// the playhead moved; the closure obligation is carried by the incoming interval's own NoteOffs, by the merged
/// reader's next traversal, and by the adoption reconcile below.
///
/// **Out of line and FLASH-resident on purpose**: it runs once per MIDI turn while a session is open, and the ITCM
/// resolver that calls it is the one the RAM1 budget measures.
#if defined(PIO_UNIT_TEST_NATIVE)
static Track::PreviewAdoptionWitness sPreviewAdoptionWitness;
Track::PreviewAdoptionWitness Track::previewAdoptionWitnessForTest() { return sPreviewAdoptionWitness; }

/// Evaluate the adoption terms in the same order as the production condition, record the witness, and perform the
/// single adoption call. Used **only** by the native fixtures; the target keeps the original condition.
static bool previewAdoptionWitnessAndAdopt(Track& track, uint8_t slotIndex, LoopPlaybackRuntime* runtime,
                                           RuntimePlaybackViewSet* set, const RuntimePlaybackView& ready,
                                           uint32_t previewRevision, const Loop& loop, uint32_t phase,
                                           bool positionedHandoffReached) {
  auto& w = sPreviewAdoptionWitness;
  w = {};
  w.current = set->lifecycle.current;
  w.ready = set->lifecycle.ready;
  w.previewRevision = previewRevision;
  w.readyRevision = ready.source.playbackRevision;
  w.phase = phase;
  w.loopLength = loop.loopLengthTicks;
  w.coverageStart = ready.coverage.startPhase;
  w.coverageLength = ready.coverage.lengthTicks;
  w.deadline = runtime->viewBuildWork.deadlineTick;
  w.admitted = runtime->viewBuildWork.admittedTick;
  w.workEpoch = runtime->viewBuildWork.clockEpoch;
  w.positionedLater = runtime->viewBuildWork.positionedLater;
  w.readyRaw = ready.rawCoveragePrepared;
  if (ready.published) w.firstFalse = 1;
  else if (!ready.complete) w.firstFalse = 2;
  else if (!positionedHandoffReached) w.firstFalse = 3;
  else if (ready.source.origin != PlaybackWindowOrigin::Preview) w.firstFalse = 4;
  else if (ready.source.playbackRevision != previewRevision) w.firstFalse = 5;
  else if (!ready.coverage.isValid()) w.firstFalse = 6;
  else if (ready.coverage.loopLengthTicks != loop.loopLengthTicks) w.firstFalse = 7;
  else if (!ready.coverage.contains(phase)) w.firstFalse = 8;
  else {
    uint8_t refusal = 0;
    if (!track.adoptPreparedPlaybackWindow(slotIndex, &refusal)) {
      w.firstFalse = 9;
      w.refusal = refusal;
    } else if (set->lifecycle.current >= set->views.size()) {
      w.firstFalse = 10;
    }
  }
  if (w.firstFalse == 0) w.current = set->lifecycle.current;
  return w.firstFalse == 0;
}
#endif

FLASHMEM bool Track::resolveNoteEditPreviewSourceForTurn(
    uint8_t slotIndex, const Loop& loop, uint32_t currentTick, PlaybackViewSourceIdentity& sourceOut,
    const FixedPlaybackBridgeStorage*& bridgeOut, const RuntimePlaybackView*& viewOut) {
  const bool sessionSlot = slotIndex == activeLoopIndex && editManager.isNoteEditActive() && isPlaying() &&
                           !isJamPlaybackActive() && !isJamming() && !hasQueuedPlaybackStart() &&
                           !hasPendingDeferredOverdubStop() && loop.loopLengthTicks != 0;
  const auto clock = clockManager.getPlaybackDeadlineClock();
  const uint32_t previewRevision = editManager.sessionPlaybackPreviewRevision();
  LoopPlaybackRuntime* runtime = sessionSlot ? playbackRuntime.slotIfAllocated(slotIndex) : nullptr;
  RuntimePlaybackViewSet* set = runtime != nullptr ? runtime->viewSetIfPrewarmed() : nullptr;
  if (set == nullptr || clock.epoch == 0 || clock.tick != currentTick || previewRevision == 0) {
    return false;
  }
  const uint32_t phase = IntervalProjection::tickPhaseInProjectionCycle(
      currentTick, getProjectionCycleStartTick(), loop.loopLengthTicks);
  // Written out at each use rather than captured in a lambda: this object file's `*(.text*)` is routed to ITCM, so every
  // lambda here would cost ITCM bytes the RAM1 budget does not have.
  // **3.15f / C:** a positioned handoff is an absolute tick, not merely a phase match. A ready that finished early waits
  // until the clock reaches the occurrence the admission priced; the served frame keeps serving meanwhile.
  const bool positionedHandoffReached =
      !runtime->viewBuildWork.positionedLater || runtime->viewBuildWork.clockEpoch == 0 ||
      (clock.epoch == runtime->viewBuildWork.clockEpoch && clock.tick >= runtime->viewBuildWork.deadlineTick);
  auto& frame = runtime->rolling;
  if (frame.phase == LoopPlaybackRuntime::RollingPhase::Serving) {
    // **Only a preview frame is this seam's business.** A frame the committed or capture lane bound to its own
    // published window is that lane's traversal: ending it here would tear a reader this seam does not own (and the
    // owned frames it cannot validate must not survive as a `Serving` claim either, because the committed reader
    // rejects a bound view that is not a published `Committed` one and a rejected frame is `Blocked`).
    const bool boundToOtherLanesWindow =
        frame.boundView < set->views.size() && frame.boundView == set->lifecycle.current &&
        set->views[frame.boundView].complete && set->views[frame.boundView].published &&
        set->views[frame.boundView].source.origin != PlaybackWindowOrigin::Preview &&
        frame.boundStorageGeneration == set->views[frame.boundView].storageGeneration;
    // **3.15f / C — a prepared preview successor at its positioned handoff is this seam's own business.** The committed
    // reader's frame is bound to the committed current on the device; deferring to it would leave the session's
    // positioned replacement unadopted forever (measured: the preview builds, the committed stream keeps serving). At
    // the positioned handoff the session's successor outranks that binding; before it, the committed frame still owns
    // the traversal.
    bool previewSuccessorDue = false;
    if (set->lifecycle.ready < set->views.size()) {
      const RuntimePlaybackView& candidate = set->views[set->lifecycle.ready];
      previewSuccessorDue = positionedHandoffReached && candidate.complete && !candidate.published &&
          candidate.source.origin == PlaybackWindowOrigin::Preview &&
          candidate.source.playbackRevision == previewRevision && candidate.coverage.isValid() &&
          candidate.coverage.loopLengthTicks == loop.loopLengthTicks && candidate.coverage.contains(phase);
    }
    if (boundToOtherLanesWindow && !previewSuccessorDue) {
      return false;  // the resolver continues into that lane's own frame handling
    }
    if (frame.epoch == clock.epoch && frame.generation == playbackGeneration &&
        frame.loopLength == loop.loopLengthTicks && frame.boundView == set->lifecycle.current &&
        frame.boundView < set->views.size()) {
      const RuntimePlaybackView& current = set->views[frame.boundView];
      // `published` and the storage generation are the committed reader's own terms (`origin`, currency, coverage and
      // role come first): the frame is a claim over **one storage instance**, so a republished role at the same
      // revision must not inherit it.
      if (current.complete && current.published && current.source.origin == PlaybackWindowOrigin::Preview &&
          current.source.playbackRevision == previewRevision && current.coverage.isValid() &&
          current.coverage.loopLengthTicks == loop.loopLengthTicks && current.coverage.contains(phase) &&
          frame.boundStorageGeneration == current.storageGeneration) {
        frame.lastTick = currentTick;
        sourceOut = current.source;
        bridgeOut = nullptr;
        viewOut = &current;
        return true;
      }
    }
    // Served out, superseded, or the view role moved: hand the traversal back to the merged reader with a clean
    // cursor, exactly as the committed reader expires. The frame is Idle from here, so the reset happens **once** per
    // handoff rather than on every turn (a per-turn reset would re-walk the merged store each turn).
    frame.phase = LoopPlaybackRuntime::RollingPhase::Idle;
    getLoop(slotIndex).nextEventIndex = 0;
  }
  // Adoption has **one** site: a prepared successor that covers the playhead is adopted first, and the current role is
  // otherwise offered as-is (the session's first publication already covers the playhead).
  const RuntimePlaybackView* adopted = nullptr;
  // `positionedHandoffReached` was resolved above, where it also decides whether a bound committed frame may defer.
  if (set->lifecycle.ready < set->views.size()) {
    const RuntimePlaybackView& ready = set->views[set->lifecycle.ready];
#if defined(PIO_UNIT_TEST_NATIVE)
    const bool readyAdopted = previewAdoptionWitnessAndAdopt(*this, slotIndex, runtime, set, ready, previewRevision,
                                                             loop, phase, positionedHandoffReached);
    if (readyAdopted && set->lifecycle.current < set->views.size()) {
      adopted = &set->views[set->lifecycle.current];
    }
#else
    if (!ready.published && ready.complete && positionedHandoffReached &&
        ready.source.origin == PlaybackWindowOrigin::Preview &&
        ready.source.playbackRevision == previewRevision && ready.coverage.isValid() &&
        ready.coverage.loopLengthTicks == loop.loopLengthTicks && ready.coverage.contains(phase) &&
        adoptPreparedPlaybackWindow(slotIndex) && set->lifecycle.current < set->views.size()) {
      adopted = &set->views[set->lifecycle.current];
    }
#endif
  }
  if (adopted == nullptr && set->lifecycle.current < set->views.size()) {
    const RuntimePlaybackView& current = set->views[set->lifecycle.current];
    if (current.complete && current.source.origin == PlaybackWindowOrigin::Preview &&
        current.source.playbackRevision == previewRevision && current.coverage.isValid() &&
        current.coverage.loopLengthTicks == loop.loopLengthTicks && current.coverage.contains(phase)) {
      adopted = &current;
    }
  }
  if (adopted == nullptr) {
    return false;
  }
  // The merged reader closes a removed identity on its own rebuild (`reconcileDeletedNoteEditPreviewSoundings`); when
  // the preview serves, that rebuild does not run, so adoption is where the obligation is discharged. A crossing note
  // (its Off beyond this interval) is not in the rows and stays open for the route that carries its Off -- which is
  // what the harness pins.
  reconcileDeletedNoteEditPreviewSoundings(*runtime, currentTick);
  getLoop(slotIndex).nextEventIndex = 0;
  frame.phase = LoopPlaybackRuntime::RollingPhase::Serving;
  frame.boundView = set->lifecycle.current;
  frame.boundStorageGeneration = set->views[frame.boundView].storageGeneration;
  frame.epoch = clock.epoch;
  frame.generation = playbackGeneration;
  frame.loopLength = adopted->coverage.loopLengthTicks;
  frame.projectionStart = getProjectionCycleStartTick();
  frame.startPhase = adopted->coverage.startPhase;
  frame.deadlineTick = 0;
  frame.lastTick = currentTick;
  sourceOut = adopted->source;
  bridgeOut = nullptr;
  viewOut = adopted;
  return true;
}
#endif
TRACK_COLD_MEM Track::PagedEmissionDeliveryState Track::pagedEmissionDeliveryStateForSlot(
    uint8_t slotIndex) const {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || !loopsAllocated()) {
    return PagedEmissionDeliveryState::NoDelivery;
  }
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr || !runtime->pagedActive) {
    // Task 3.12 / 5.5: the **named refusal** is an answer of this gate, not silence. A delivery the lane refused for its
    // content's closure high-water has no ring and no interval, so the state above cannot carry it; the name is kept on
    // the demand that is still owed, and it is reported only while that demand is the loop's own current revision (a
    // revision the loop has left is nobody's answer: identity, not currency).
    const Loop& current = loopForSlot(slotIndex);
    if (runtime != nullptr &&
        runtime->pagedOutcome == LoopPlaybackRuntime::PagedPlaybackOutcome::ClosuresExceedDeclaredCapacity &&
        runtime->pendingViewRequest_.active &&
        runtime->pendingViewRequest_.source.playbackRevision == current.playbackRevision) {
      return PagedEmissionDeliveryState::ClosuresExceedDeclaredCapacity;
    }
    return PagedEmissionDeliveryState::NoDelivery;
  }
  const Loop& loop = loopForSlot(slotIndex);
  if (runtime->pagedRevision != loop.playbackRevision) {
    return PagedEmissionDeliveryState::StaleRevision;
  }
  if (runtime->pagedInterval.lengthTicks >= loop.loopLengthTicks) {
    return PagedEmissionDeliveryState::CannotStraddleTheWrap;
  }
  // The **measurement** the delivery reports is separate from the **selection** switch: this predicate answers what
  // the emission has observed (a bound delivery whose last boundary was straddled cleanly), while
  // `admittedForEmission()` carries the closed selection switch and is what the hot path may test.
  if (!runtime->pagedEmission.bound || runtime->pagedEmission.passCount == 0 ||
      runtime->pagedEmission.passesUnstraddled != 0) {
    return PagedEmissionDeliveryState::Unproven;
  }
  return PagedEmissionDeliveryState::Admissible;
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

TRACK_COLD_MEM bool Track::prewarmPlaybackForSlot(uint8_t slotIndex, bool prepareCapture,
                                                  bool restamp, bool skipViews) {
  // DEC-239 profile scope (long: seven reserves, unbounded at boot/load; synchronous, no wait). The
  // `capture_prewarm_split` row emitted at the end of this body is part of the measured interval.
  PROFILE_SCOPE_LONG(profileScope, RuntimeProfiler::ProfileId::PlaybackPrewarm_slot);
  profileScope.setContext(RuntimeProfiler::kNoContext, slotIndex);
  ensureLoopsAllocated();
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return false;
  }
  // Local cycle/us counters, emitted as **one** row after the whole call: serial work inside the
  // prewarm would perturb the very pause being measured. `other` is the non-reserve remainder
  // (lease, accounting tags, the existing mid-call `capture_prewarm` row).
  const uint32_t prewarmStartUs = micros();
  uint32_t stopJobsUs = 0;
  uint32_t viewsUs = 0;
  uint32_t canonicalUs = 0;
  uint32_t horizonUs = 0;
  uint32_t companionUs = 0;
  uint32_t orderUs = 0;
  // Resource-completeness bits for the eventual prepared-state adoption: bit0 stop-jobs, bit1 views,
  // bit2 canonical output, bit3 transfer horizon, bit4 companion history. A false reserve leaves its
  // bit clear; the Record edge must then stay armed rather than re-running the reserve synchronously.
  uint8_t okMask = 0;
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
  const uint32_t stopJobsStartUs = micros();
  const bool stopJobsOk = prewarmCaptureStopJobs(
      prepareCapture ? slotIndex : Config::MAX_LOOPS_PER_TRACK);
  stopJobsUs = micros() - stopJobsStartUs;
  ok = stopJobsOk && ok;
  if (stopJobsOk) okMask = static_cast<uint8_t>(okMask | 0x01u);
  ExternalPoolAccounting::endTag();
  Loop& loop = loopForSlot(slotIndex);
  LoopPlaybackRuntime* runtime = playbackRuntime.trySlot(slotIndex);
  if (runtime != nullptr) {
    // This is a boot/load or pre-transport path. The hot MIDI path deliberately
    // has no equivalent allocation call.
    if (prepareCapture && !skipViews) {
      // Derived companion-row reserve for this slot (DEC-066): a loop whose canonical
      // payloads are sparse does not reserve the full row horizon, while a dense loop
      // still reaches the ceiling. The clamp lives in the owner.
      ExternalPoolAccounting::beginTag(ExternalPoolAccounting::TagViewPrewarm);
      const uint32_t viewsStartUs = micros();
      // RC-MEM-2: reserve the whole-take representation only when this loop can actually
      // publish one. Past `kMaxDetailedWindowBars` the windowed policy is the only
      // authoritative representation, so the bridges and the prepared pair are dead
      // reservation; the three views and both capture stagings stay (capture admission and
      // the live-record/handoff lanes need them).
      const bool viewsOk = runtime->prewarmViews(loop.companionRowHorizonBound(),
                                                 loop.fullTakePlaybackViewAdmissible());
      viewsUs = micros() - viewsStartUs;
      ok = viewsOk && ok;
      if (viewsOk) okMask = static_cast<uint8_t>(okMask | 0x02u);
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
      const uint32_t canonicalStartUs = micros();
      const bool canonicalOk = runtime->prewarmCanonicalCaptureOutput();
      canonicalUs = micros() - canonicalStartUs;
      ok = canonicalOk && ok;
      if (canonicalOk) okMask = static_cast<uint8_t>(okMask | 0x04u);
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
    const uint32_t horizonStartUs = micros();
    const bool horizonOk = loop.prewarmActiveOverdubTransferHorizon();
    horizonUs = micros() - horizonStartUs;
    ok = horizonOk && ok;
    if (horizonOk) okMask = static_cast<uint8_t>(okMask | 0x08u);
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
      const uint32_t companionStartUs = micros();
      const bool companionOk = loop.prewarmPreparedCompanionHistory(companionCapacity);
      companionUs = micros() - companionStartUs;
      ok = companionOk && ok;
      if (companionOk) okMask = static_cast<uint8_t>(okMask | 0x10u);
      ExternalPoolAccounting::endTag();
    }
  }
  ExternalPoolAccounting::beginTag(ExternalPoolAccounting::TagPlaybackOrder);
  const uint32_t orderStartUs = micros();
  (void)loop.getPlaybackOrder();
  orderUs = micros() - orderStartUs;
  ExternalPoolAccounting::endTag();
  // S3.1: express the ownership this admission just established. `ok` is the admission answer, so
  // a lease is recorded only when the working set is actually usable; the record arm is the
  // admission caller and must act on a false answer. No resource moves, and the same reserves are
  // established exactly as before.
  if (ok && runtime != nullptr && prepareCapture) {
    runtime->acquireWorkingSetLease(WorkingSetRole::PlaybackWindow | WorkingSetRole::LiveCapture);
  }
  if (runtime != nullptr && prepareCapture) {
    // Preserve the view bit owned by the resumable state machine; this call owns the four cheap
    // reserves. The Record edge admits only when all five bits are present.
    runtime->viewPrewarmResourceMask = static_cast<uint8_t>(
        (runtime->viewPrewarmResourceMask & 0x02u) | (okMask & static_cast<uint8_t>(~0x02u)));
  }
#if defined(SESSION_CAPTURE)
  {
    const uint32_t totalUs = micros() - prewarmStartUs;
    const uint32_t accountedUs = stopJobsUs + viewsUs + canonicalUs + horizonUs + companionUs +
                                 orderUs;
    const uint32_t otherUs = totalUs > accountedUs ? totalUs - accountedUs : 0;
    const uint8_t reportMask =
        (runtime != nullptr && prepareCapture) ? runtime->viewPrewarmResourceMask : okMask;
    DebugSessionCapture::capturePrewarmSplit(
        resolveTrackIndexForPersistence(*this), slotIndex, restamp, reportMask, viewsUs, canonicalUs,
        horizonUs, stopJobsUs, companionUs, orderUs, otherUs, totalUs);
  }
#endif
  return ok;
}

TRACK_COLD_MEM uint32_t Track::viewPrewarmIdentityForSlot(uint8_t slotIndex) const {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return 0;
  }
  const Loop& loop = getLoop(slotIndex);
  // Capacity identity, not content identity: the storage reserve depends on the slot incarnation,
  // the loop geometry, whole-take admissibility and the companion-row bound. Playback/capture content
  // revisions do not change the allocation, so they are deliberately not mixed in.
  uint32_t hash = 2166136261u;
  const auto mix = [&hash](uint32_t value) {
    hash ^= value;
    hash *= 16777619u;
  };
  mix(static_cast<uint32_t>(loop.loopId));
  mix(loop.loopLengthTicks);
  mix(loop.startLoopTick);
  mix(loop.loopStartTick);
  mix(loop.fullTakePlaybackViewAdmissible() ? 1u : 0u);
  mix(static_cast<uint32_t>(loop.companionRowHorizonBound()));
  return hash;
}

TRACK_COLD_MEM bool Track::viewPrewarmReadyForSlot(uint8_t slotIndex) {
  const auto* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  return runtime != nullptr && runtime->viewPrewarmReady(viewPrewarmIdentityForSlot(slotIndex));
}

TRACK_COLD_MEM void Track::requestViewPrewarmForSlot(uint8_t slotIndex) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return;
  }
  auto* runtime = playbackRuntime.trySlot(slotIndex);
  if (runtime != nullptr) {
    runtime->requestViewPrewarm(viewPrewarmIdentityForSlot(slotIndex));
  }
#if defined(SESSION_CAPTURE)
  DebugSessionCapture::viewPrewarmRequest(
      resolveTrackIndexForPersistence(*this), slotIndex, runtime != nullptr,
      runtime != nullptr ? static_cast<uint8_t>(runtime->viewPrewarmPhase) : 0xFFu);
#endif
}

TRACK_COLD_MEM bool Track::stepViewPrewarmForSlot(uint8_t slotIndex, uint8_t maxUnits, uint32_t budgetUs) {
  auto* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return false;
  }
  const Loop& loop = getLoop(slotIndex);
  const uint32_t startUs = micros();
  bool ready;
  {
    // DEC-239 profile scope (long: one unit is one external allocation, measured up to ~17.7 ms, and
    // the time guard is secondary -- no hard cap; synchronous, no wait). Closes before
    // `viewPrewarmStep` so the profiler and the row bracket comparable intervals (scope <= row).
    PROFILE_SCOPE_LONG(profileScope, RuntimeProfiler::ProfileId::ViewPrewarm_unit);
    profileScope.setContext(RuntimeProfiler::kNoContext, slotIndex);
    ready = runtime->stepViewPrewarm(
        viewPrewarmIdentityForSlot(slotIndex), loop.fullTakePlaybackViewAdmissible(),
        loop.companionRowHorizonBound(), maxUnits, budgetUs);
    profileScope.setResult(ready ? 1u : 0u);
  }
#if defined(SESSION_CAPTURE)
  DebugSessionCapture::viewPrewarmStep(
      resolveTrackIndexForPersistence(*this), slotIndex, runtime->viewPrewarmProgress(),
      micros() - startUs, ready);
#endif
  return ready;
}

TRACK_COLD_MEM TRACK_COLD_MEM bool Track::hasViewPrewarmPending() const {
  for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
    const auto* runtime = playbackRuntime.slotIfAllocated(slot);
    if (runtime != nullptr && runtime->viewPrewarmPreparing()) {
      return true;
    }
  }
  return false;
}

TRACK_COLD_MEM bool Track::stepViewPrewarmForIdle() {
  for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
    auto* runtime = playbackRuntime.slotIfAllocated(slot);
    if (runtime != nullptr && runtime->viewPrewarmPreparing()) {
      (void)stepViewPrewarmForSlot(slot, 1, 5000);
      return true;
    }
  }
  return false;
}

TRACK_COLD_MEM uint16_t Track::viewPrewarmProgressForSlot(uint8_t slotIndex) const {
  const auto* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  return runtime != nullptr ? runtime->viewPrewarmProgress() : 0;
}

TRACK_COLD_MEM bool Track::hasPlaybackOccupancyForSlot(uint8_t slotIndex) const {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) return false;
  return playbackRuntime.slotIfAllocated(slotIndex) != nullptr;
}

TRACK_COLD_MEM void Track::serviceOwedTickPlaybackFallback(uint8_t slotIndex) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || !loopsAllocated()) return;
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) return;
  Loop& loop = loopForSlot(slotIndex);
  const uint32_t currentTick = clockManager.getCurrentTick();
  // The interrupt tick sees `servicing` and owes again instead of reading a half-built fallback.
  PlaybackTickContext::gState.servicing = true;
  (void)buildMergedPlaybackFallbackCold(*this, loop, *runtime, currentTick);  // the exact body the tick would have run
  if (loop.playbackOrderDirty) {
    const ProjectionContext playbackContext = makePlaybackContext(*this, loop, currentTick);
    ::rebuildPlaybackOrder(loop, runtime->mergedMidiEvents.mergedEvents, playbackContext);
    reanchorPlaybackIndex(loop, runtime->mergedMidiEvents.mergedEvents, loop.getPlaybackOrder(), playbackContext);
  }
  PlaybackTickContext::gState.servicing = false;
}

TRACK_COLD_MEM void TrackManager::serviceOwedTickPlaybackFallbacks() {
  for (uint8_t trackIndex = 0; trackIndex < Config::NUM_TRACKS && trackIndex < getTrackCount(); ++trackIndex) {
    const uint8_t owed = PlaybackTickContext::gState.owed[trackIndex];
    if (owed == 0) continue;
    // Clear before building: an interrupt tick that lands during the build owes again, and the next turn finds the
    // fallback current (a cheap reuse) instead of losing the request.
    PlaybackTickContext::gState.owed[trackIndex] = 0;
    for (uint8_t slot = 0; slot < 8; ++slot) {
      if ((owed & (1u << slot)) != 0) getTrack(trackIndex).serviceOwedTickPlaybackFallback(slot);
    }
  }
}

TRACK_COLD_MEM void TrackManager::reportPlaybackRouteRejects(uint32_t nowMs) {
  for (uint8_t trackIndex = 0; trackIndex < Config::NUM_TRACKS && trackIndex < getTrackCount(); ++trackIndex) {
    for (uint8_t slot = 0; slot < 8; ++slot) {
      PlaybackTickContext::SlotRouteReject& s = PlaybackTickContext::gState.reject[trackIndex][slot];
      const uint32_t changes = s.changes;
      if (changes == s.emittedChanges) continue;
      const uint8_t reason = s.reason;
      const uint8_t cause = s.cause;
      if (reason == s.emittedReason && cause == s.emittedCause && s.emittedChanges != 0) {
        s.emittedChanges = changes;  // flapped back to the reported reason: nothing new to name
        continue;
      }
      if (s.emittedChanges != 0 && nowMs - s.emittedMs < PlaybackTickContext::kRouteRejectRowSpacingMs) continue;
      Track& track = getTrack(trackIndex);
      const uint32_t loopLength = track.loopsAllocated() ? track.getLoop(slot).loopLengthTicks : 0;
      SC_ROUTE_REJECT(trackIndex, slot, reason, cause, s.prevReason, s.prevTicks, s.sinceTick,
                      changes - s.emittedChanges, loopLength, PlaybackTickContext::gState.producerExit[trackIndex]);
      (void)loopLength;
      s.emittedReason = reason;
      s.emittedCause = cause;
      s.emittedChanges = changes;
      s.emittedMs = nowMs;
    }
  }
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
      reconcilePlaybackLedgerAfterFullLoopRebuild(loop, *runtime, currentTick,
                                                  playbackClosureServeFloor(*this, loop, currentTick));
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
    // 2.1: the prepared pair is no longer published. The supported (demoted) launch/switch routes serve the
    // unified window; the full-take legacy route uses the merged fallback until 2.5.
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
      // 3.15e/2b-ii-b: a retained overdub-overlay candidate owns the borrowed companion instance **by identity**, so
      // the stopped oracle asks the named decision before it builds. A stale candidate is retired first; a current one
      // refuses this turn. Either way, the bytes must never be reinterpreted as oracle output underneath it.
      const uint32_t overlayPhase = IntervalProjection::tickPhaseInProjectionCycle(
          currentTick, getProjectionCycleStartTick(), loop.loopLengthTicks);
      OverdubOverlayLane::OracleRebuildDecision overlayDecision = OverdubOverlayLane::OracleRebuildDecision::Allowed;
      if (!runtime->publishStoppedCanonicalOracleView(loop.passes, sourceIdentity, live,
              runtime->mergedMidiEvents.mergedEvents.data(), runtime->mergedMidiEvents.mergedEvents.size(), coverage,
              overdubOverlayRetentionForSlot(slotIndex, overlayPhase), &overlayDecision)) {
        // A live candidate owns this turn's boundary; publishing the raw fallback under it would report a boundary the
        // candidate still answers. Every other refusal keeps the existing fallback exactly as it was.
        if (overlayDecision != OverdubOverlayLane::OracleRebuildDecision::RefusedRetainedCandidate) {
          (void)runtime->publishStoppedOracleView(runtime->mergedMidiEvents.mergedEvents.data(),
                                                  runtime->mergedMidiEvents.mergedEvents.size(),
                                                  sourceIdentity, coverage);
        }
      }
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
  // **One readiness rule for every length** (DEC-200). It answers the same question the builder answers -- "is the
  // window I would build the window I hold?" -- so it cannot drift from it: same revision, a real source (root or
  // committed passes), the builder's window span, and its order built. The former short-loop branch accepted *any*
  // span and the former long-loop branch carried a staleness term the other did not, which is two policies for one
  // question. Length appears only as the clamp inside `expectedWindow`, exactly as in the builder.
  const bool rootBacked = window.builtFromSnapshotIdentity != nullptr &&
      window.builtFromSnapshotIdentity == loop.publishedSnapshotIdentity();
  const bool committedBacked = !loop.publishedSnapshotIsCurrent() && !window.mergedEvents.empty();
  const uint32_t expectedWindow =
      mergedPlaybackWindowLengthTicks(loop.loopLengthTicks,
                                      loop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks),
                                      kMergedWindowTicks);
  return window.builtFromRevision == loop.playbackRevision &&
      (rootBacked || committedBacked) &&
      window.windowLengthTicks == expectedWindow &&
      !loop.playbackOrderDirty;
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
    // Nothing is published. **Who owes it** turns on whether a build could succeed at all, and a slot has *two* source
    // authorities: the published whole-loop root and -- task 3.6/3.7a -- the retained revision, which is what a demoted
    // slot's window is built from when no root is resident (DEC-108 §10). So a rootless slot whose retained source is
    // the **current** revision owes the **build**, not the source: naming `published_source` there would name an owner
    // with nothing to do. A rootless slot with no retained revision (or a lagging one) owes the source, unchanged.
    if (loop.publishedSnapshotIsCurrent()) {
      return PlaybackWindowReadiness::OwesBuild;
    }
    RetainedPlaybackSource retained{};
    // Task 3.12/I9: the **serving** source is what "the retained source is current" means -- a retiring entry beside
    // it would make this name `OwesSource` for a slot whose revision is retained and whose window is owed.
    if (servingRetainedRevisionForSlot(slotIndex) == loop.playbackRevision) {
      return PlaybackWindowReadiness::OwesBuild;
    }
    return PlaybackWindowReadiness::OwesSource;
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
namespace {
TRACK_COLD_MEM bool retainedSourceMatchesPassInventory(const RetainedPlaybackSource& source,
                                                       const LoopPasses& passes) {
  size_t passIndex = 0;
  size_t chunkIndex = 0;
  const auto matchesCapture = [&](PassId id, const CommittedChunkIdList& chunks) {
    if (passIndex >= source.pins.capturePassIds.size() || source.pins.capturePassIds[passIndex++] != id)
      return false;
    for (uint16_t chunk : chunks) {
      if (chunkIndex >= source.ownedChunks.size() || source.ownedChunks[chunkIndex++] != chunk)
        return false;
    }
    return true;
  };
  if (passes.hasRecordPass() && !matchesCapture(passes.recordPass.id, passes.recordPass.committedChunkIds))
    return false;
  for (const auto& pass : passes.overdubPasses)
    if (!matchesCapture(pass.id, pass.committedChunkIds)) return false;
  if (passIndex != source.pins.capturePassIds.size() || chunkIndex != source.ownedChunks.size() ||
      passes.editPasses.size() != source.pins.editPassIds.size()) return false;
  for (size_t i = 0; i < passes.editPasses.size(); ++i)
    if (passes.editPasses[i].id != source.pins.editPassIds[i]) return false;
  return true;
}
}  // namespace

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
  bool reuseOwnedSource = false;
  if (entry == nullptr && revision == loop.playbackRevision && generation == playbackGeneration &&
      loop.loopId == loopIdForSlot(slotIndex) &&
      !isRecording() && !isOverdubbing() && !loop.captureActive() && !loop.hasOverdubSession() &&
      !runtime->viewBuild.active() && !runtime->bridgeWindowBuild.active() &&
      !runtime->retainedGatherActive() && !runtime->ownsCoverageSource()) {
    // State-only undo/redo keeps the same immutable bodies. Reuse an unobserved retired
    // descriptor without dropping/reacquiring even one reference (or advancing alias tokens).
    for (auto& candidate : runtime->retainedSources) {
      if (candidate.role != RetainedSourceRole::Retiring ||
          !canRetireRetainedPlaybackSource(slotIndex, candidate)) continue;
      bool viewNeedsRevision = false;
      if (const auto* views = runtime->viewSetIfPrewarmed()) {
        for (uint8_t role : {views->lifecycle.current, views->lifecycle.ready, views->lifecycle.retained}) {
          if (role < views->views.size() && views->views[role].complete &&
              views->views[role].source.playbackRevision == candidate.revision) {
            viewNeedsRevision = true;
            break;
          }
        }
      }
      if (viewNeedsRevision || !retainedSourceMatchesPassInventory(candidate, loop.passes)) continue;
      entry = &candidate;
      reuseOwnedSource = true;
      break;
    }
  }
  if (entry == nullptr) {
    return PlaybackSourceRetainResult::RoleCapacityReached;
  }
  // Two protections, each read by the consumer that would otherwise destroy the source. The descriptor pins are what
  // `Loop::reclaimUnreferencedDisabled*Passes` honours; the owned references are what stops the store's
  // `releaseChunkRefs` on the direct reset/replacement path from freeing or recycling the same bodies underneath a
  // retained window. Both describe one revision, and both are dropped by the one release predicate.
  if (!reuseOwnedSource) {
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
  }
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
  // Task 3.12/I9: **the first retained entry, unchanged** -- W-19 (task 3.6a) reads the *older* source through this
  // accessor to watch its role become `Retiring`, so its meaning is "some retained entry" and stays that way. A reader
  // that needs the revision the lane serves uses `servingRetainedRevisionForSlot` (the role array's order must not
  // must not decide an answer), and the header's "Read the **serving** retained source" line is a doc/behaviour
  // mismatch this increment found and recorded rather than changed under 3.6a's fixture.
  for (const RetainedPlaybackSource& entry : runtime->retainedSources) {
    if (entry.isRetained()) {
      out = entry;
      return true;
    }
  }
  return false;
}

/// Task 3.12/I9: the **serving** revision for this slot -- the one the lane serves right now, or 0 when no role serves.
/// A reader that only compares revisions must not copy a source (its pins and owned bodies are vectors), and a slot can
/// hold a *retiring* source beside the serving one (3.6a's roles, bounded at two), so "the first retained entry" can
/// date an answer by the previous revision: the window lane refused the current revision with `RevisionMismatch` for
/// exactly that reason, and a slot whose window is being rebuilt would report `OwesSource` instead of `OwesBuild`.
TRACK_COLD_MEM uint32_t Track::servingRetainedRevisionForSlot(uint8_t slotIndex) const {
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return 0;
  }
  const RetainedPlaybackSource* source = runtime->servingRetainedSource();
  return source == nullptr ? 0u : source->revision;
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

TRACK_COLD_MEM bool Track::canRetireRetainedPlaybackSource(
    uint8_t slotIndex, const RetainedPlaybackSource& source) const {
  const auto* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr || !loopsAllocated() || !source.isRetained()) return false;
  const Loop& loop = loopForSlot(slotIndex);
  const bool contentGone = !loop.hasCommittedPasses() || loop.loopLengthTicks == 0;
  if (!contentGone && source.revision >= loop.playbackRevision) return false;
  PlaybackWindowIdentity published{};
  if (publishedPlaybackWindowIdentityForSlot(slotIndex, published) &&
      published.playbackRevision == source.revision) return false;
  return !runtime->coverageBuildNeedsService() && !runtime->ownsLiveFinalization();
}

TRACK_COLD_MEM bool Track::tryReleaseRetainedPlaybackSource(uint8_t slotIndex) {
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return false;
  }
  bool released = false;
  for (RetainedPlaybackSource& entry : runtime->retainedSources) {
    // **The one release predicate.** Every clause is a reason the source is still needed; the source is dropped only
    // when none holds. The builder clause is deliberately conservative for this slice: while a build is outstanding
    // its frozen source is not inspectable from here, so the sources are held rather than risk releasing one a
    // builder is reconstructing from. The finer per-builder term is named as owed, not assumed away.
    // The content the source describes can also be **gone** -- a clear/reset or a replacement load -- and then the
    // counter never advances past the retained revision, so it is released here too rather than held forever. Still
    // this one predicate, and still only when nothing references it.
    if (!canRetireRetainedPlaybackSource(slotIndex, entry)) continue;
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
#if PLAYBACK_WINDOW_ROLLING_ENABLED
    // Task 4.5: the latch is set **only where a boundary exists to end it**. Every clear site is the lane's adoption
    // boundary (`:1157` inside this same flag, and `:2210` reached only from the serving resolver the flag compiles in),
    // so the default image -- which compiles the lane out and serves through the retained/merged routes -- could latch a
    // fault that nothing there could ever clear. Nothing in that image reads the latch either (`containmentForSlot` and
    // `deadlineContractFaultLatchedForSlot` have no production caller), so this is the honest capability boundary, not a
    // behaviour change: the breach row, the episode counters and the decision are untouched in both images.
    runtime->deadlineContractFaultLatched = true;
#endif
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

TRACK_COLD_MEM Track::LastResortOutcome Track::containPlaybackTurnIfNoRouteForSlot(
    uint8_t slotIndex, uint32_t currentTick, bool isActiveSlot) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || !loopsAllocated()) {
    return LastResortOutcome::NotNeeded;
  }
  Loop& loop = loopForSlot(slotIndex);
  // A short loop's merged store is the whole loop: it is a route that covers every phase, so nothing to contain.
  if (!loop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks)) {
    return LastResortOutcome::NotNeeded;
  }
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return LastResortOutcome::NotNeeded;
  }
  const uint32_t phase =
      isActiveSlot
          ? static_cast<uint32_t>(IntervalProjection::tickPhaseInProjectionCycle(
                currentTick, projectionCycleStartTick, loop.loopLengthTicks))
          : static_cast<uint32_t>(IntervalProjection::tickPhaseInLoop(
                currentTick, loop.startLoopTick, loop.loopLengthTicks));
  // The demoted slot's only remaining candidate is the merged catch-all's own bounded window. Coverage, not freshness:
  // a window from the previous revision still *serves* (that is what "last coherent revision" means), so a stale window
  // that covers keeps the slot out of containment exactly like a current one.
  const auto& mergedWindow = runtime->mergedMidiEvents;
  const bool mergedCovers = mergedWindow.windowLengthTicks > 0 &&
                            phase >= mergedWindow.windowStartTick &&
                            phase - mergedWindow.windowStartTick < mergedWindow.windowLengthTicks;
  if (mergedCovers) {
    return LastResortOutcome::NotNeeded;
  }
  return applyLastResortForSlot(slotIndex, phase);
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
  // Task 3.12/I9: the index describes the **serving** revision -- the one the retention just admitted. The first
  // retained entry may be the retiring one, and an index stamped with the previous revision would refuse the current
  // one by name (`RevisionMismatch`) while claiming a window-bounded answer exists for it.
  const RetainedPlaybackSource* source = runtime->servingRetainedSource();
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

/// Task 3.7a (I1/I3b): the retained revision's pinned capture bodies, in pass-set order (record first, then overdubs);
/// declared here because both readers of one revision's membership use it -- the window gather below and the paged
/// producer in this same unit -- and a second enumeration is exactly how two readers would come to disagree.
static void collectPinnedCaptureLists(const Loop& loop, const RetainedPlaybackSource& source,
                                     std::vector<const CommittedChunkIdList*>& listsOut, uint32_t& missingOut);
/// Task 3.7a (I7): the retained revision's own **Active Note rows**, in pass order, filtered to the passes the
/// revision pinned. A small dedicated flash helper rather than an inline loop in the gather: the gather is a large
/// function, and inlining the container's growth path into it is what puts `std::vector<EditPass>`'s machinery into
/// ITCM (measured: +320 B when the loop lived there). It states the same filter the effective-content owner applies.
static TRACK_COLD_MEM void collectPinnedActiveEditRows(const Loop& loop, const RetainedPlaybackSource& source,
                                                       EditPassVec& out) {
  out.clear();
  for (const EditPass& row : loop.passes.editPasses) {
    if (row.state != EditPassState::Active || row.passType != EditPassType::Note) {
      continue;
    }
    for (const EditPassId pinnedId : source.pins.editPassIds) {
      if (row.id == pinnedId) {
        out.push_back(row);
        break;
      }
    }
  }
}

/// Task 3.7a (I7): **does this revision's pinned membership carry an Active Note row?** The question the gather itself
/// asks -- and the only thing it may ask, because it decides *which* path the interval takes; the rows themselves never
/// enter the gather's frame. A plain scan, so nothing is instantiated into ITCM by the answer.
static TRACK_COLD_MEM bool retainedRevisionHasActiveEditRows(const Loop& loop,
                                                            const RetainedPlaybackSource& source) {
  for (const EditPass& row : loop.passes.editPasses) {
    if (row.state != EditPassState::Active || row.passType != EditPassType::Note) {
      continue;
    }
    for (const EditPassId pinnedId : source.pins.editPassIds) {
      if (row.id == pinnedId) {
        return true;
      }
    }
  }
  return false;
}

/// Task 3.7a (I7): the owner's own **layer order** -- tick sort, then the Active Note rows, then merge. Stated by hand
/// (`std::sort`'s instantiation would be placed in this flash-resident TU: I1 measured it at 1,280 B against a 1,640 B
/// headroom), and stable on equal ticks, which is the order the layer's own traversal produced.
static TRACK_COLD_MEM void sortRetainedWindowLayer(SessionMidiEventVec& layer) {
  for (size_t i = 1; i < layer.size(); ++i) {
    const MidiEvent key = layer[i];
    size_t j = i;
    while (j > 0 && layer[j - 1].tick > key.tick) {
      layer[j] = layer[j - 1];
      --j;
    }
    layer[j] = key;
  }
}

/// Task 3.7a (I7): the owner's own **layer merge** (`Loop`'s `mergeSortedLoopCaptureLayers`), reproduced in place:
/// `std::merge` with `a.tick < b.tick` takes from the **accumulated** side on equal ticks, so the head keeps its
/// position and the new layer follows it. Merged backwards into the room `resize` just made, so no second buffer and no
/// second allocation are needed.
static TRACK_COLD_MEM void mergeRetainedWindowLayer(SessionMidiEventVec& out, const SessionMidiEventVec& layer) {
  if (layer.empty()) {
    return;
  }
  const size_t head = out.size();
  out.resize(head + layer.size());
  size_t headIndex = head;
  size_t layerIndex = layer.size();
  size_t destination = out.size();
  while (headIndex > 0 && layerIndex > 0) {
    if (out[headIndex - 1].tick > layer[layerIndex - 1].tick) {
      out[--destination] = out[headIndex - 1];
      --headIndex;
    } else {
      out[--destination] = layer[layerIndex - 1];
      --layerIndex;
    }
  }
  while (layerIndex > 0) {
    out[--destination] = layer[layerIndex - 1];
    --layerIndex;
  }
}

/// Task 3.7a (I7): **the per-layer half of the gather**, one layer per call -- the granularity the slice bound and both
/// callers' ceilings are expressed in. The traversal primitive is bounded to the layers already resolved plus the
/// current one, so a call can never cross a layer boundary: a layer whose traversal completes is one whose events the
/// caller's scratch holds **on their own**, which is what the owner's order needs. A layer with no event in the window
/// costs no slice, and the traversal advances to the next layer inside the same call, so the number of calls a demand
/// needs is driven by its content rather than by its pass count.
static TRACK_COLD_MEM void gatherRetainedWindowLayers(
    const Loop& loop, const RetainedPlaybackSource& source, const std::vector<const CommittedChunkIdList*>& lists,
    uint32_t windowStart, uint32_t windowLength, CommittedEventRange::WindowTraversalCursor& cursor,
    SessionMidiEventVec& out, SessionMidiEventVec& layerScratch, Track::RetainedWindowGather& gather) {
  // The revision's own Active Note rows, collected **here** rather than in the gather that calls this: this helper is
  // small, so the container's machinery inlines into it, while the gather is large enough that the compiler emits it
  // out of line -- measured as ITCM (task 3.7a/I7's budget finding).
  EditPassVec activeRows;
  collectPinnedActiveEditRows(loop, source, activeRows);
  gather.result = Track::RetainedWindowGatherResult::Served;
  while (true) {
    const size_t layerIndex = cursor.listIndex;
    if (layerIndex >= lists.size()) {
      // Every layer is resolved: the destination is this revision's effective content in this window.
      cursor.complete = true;
      gather.complete = true;
      return;
    }
    const CommittedEventRange::WindowTraversalResult slice = CommittedEventRange::appendWindowBudgeted(
        lists.data(), layerIndex + 1, loop.loopLengthTicks, windowStart, windowLength, kRetainedGatherSliceEvents,
        cursor, layerScratch);
    gather.inspectedChunks += static_cast<uint32_t>(slice.chunksCopied);
    gather.eventsAppended += static_cast<uint32_t>(slice.eventsAppended);
    gather.complete = false;
    if (!cursor.complete) {
      return;  // this layer is still being traversed: the next call resumes it where it stopped
    }
    // The layer is complete in the caller's scratch: the owner's order, then the merge. The rows are applied even to a
    // layer carrying no event in this window, exactly as the owner does (a `Create` row adds its own events).
    sortRetainedWindowLayer(layerScratch);
    applyNoteEditPassSequence(layerScratch, activeRows, loop.loopLengthTicks);
    mergeRetainedWindowLayer(out, layerScratch);
    layerScratch.clear();
    cursor.listIndex = layerIndex + 1;
    cursor.chunkIndex = 0;
    cursor.chunkEventOffset = 0;
    if (cursor.listIndex >= lists.size()) {
      // The **last** layer is resolved: this call is the demand's completion, exactly as the single-traversal path's
      // completing slice is -- no extra call is spent on reporting a fact this one already established.
      cursor.complete = true;
      gather.complete = true;
      return;
    }
    cursor.complete = false;
    if (slice.eventsAppended > 0) {
      return;  // one slice of gathering per call, exactly as the single-traversal path
    }
  }
}

/// Task 3.7a (I1): the interval answered from the **retained revision's pinned bodies**. The index answers first (how
/// many bars the pinned bodies cover and how many cost no inspection at all), and only a covered interval is
/// enumerated -- through the same resumable body primitive `WindowPagedStream` already uses, one bounded slice per
/// call, so the inspected work is the slice's own and a caller can resume across deferred turns.
///
/// The retained revision's own **Active Note note-edit rows** (task 3.7a/I7) are resolved **per pass layer**, which is
/// the effective-content owner's rule and the reason they are not applied flat: `Loop::copyEffectiveCommittedEventsInRange`
/// applies each committed list separately and then merges, because a merged-then-apply pass lets a LIFO delete pair a
/// hidden On with another pass's Off. Each layer is traversed, tick-sorted, edited and merged in that same order -- one
/// layer per call, through the helper above -- so the destination is the owner's own answer for this revision, which is
/// the differential oracle. The rows applied are the **pinned** ones (this revision's own membership), so a row created
/// after the retention cannot leak into an earlier revision's window. `editRowsOut` is the caller's scratch for the
/// layer being resolved: it travels with the caller because two callers may gather for the same slot, so this primitive
/// may not hold it.
TRACK_COLD_MEM Track::RetainedWindowGather Track::gatherRetainedSourceWindowForSlot(
    uint8_t slotIndex, uint32_t startPhase, uint32_t lengthTicks, uint32_t expectedRevision,
    CommittedEventRange::WindowTraversalCursor& cursor, SessionMidiEventVec& out, SessionMidiEventVec& editRowsOut) {
  RetainedWindowGather gather{};
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || !loopsAllocated()) {
    gather.result = RetainedWindowGatherResult::NoRuntime;
    return gather;
  }
  const Loop& loop = loopForSlot(slotIndex);
  if (!loop.hasCommittedPasses() || loop.loopLengthTicks == 0 || lengthTicks == 0) {
    gather.result = RetainedWindowGatherResult::NotServable;
    return gather;
  }
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  const RetainedPlaybackSource* source = nullptr;
  if (runtime != nullptr) {
    // Task 3.12/I9: **the revision asked for decides first.** A slot can hold a *retiring* source beside the serving
    // one (3.6a's roles, bounded at two), and a caller may ask for either -- the one-pass-late allowance is the
    // caller's rule (task 3.6b). Taking "the first retained entry" dated the answer by whichever revision happened to
    // occupy the first role slot, so a slot serving the **new** revision refused it with `RevisionMismatch`.
    for (const RetainedPlaybackSource& entry : runtime->retainedSources) {
      if (entry.isRetained() && entry.revision == expectedRevision) {
        source = &entry;
        break;
      }
    }
    if (source == nullptr) {
      source = runtime->servingRetainedSource();
    }
  }
  if (source == nullptr) {
    // No revision is retained: there is nothing to serve from, and the **source** is what the slot owes.
    gather.result = RetainedWindowGatherResult::NoRetainedSource;
    return gather;
  }
  if (source->revision != expectedRevision) {
    // Answered by name: the one-pass-late allowance is the caller's rule, and no reader may bind this revision to
    // another revision's bodies.
    gather.result = RetainedWindowGatherResult::RevisionMismatch;
    return gather;
  }
  const uint32_t bars = (loop.loopLengthTicks + Config::TICKS_PER_BAR - 1) / Config::TICKS_PER_BAR;
  if (bars == 0 || bars > kRetainedIndexMaxBars) {
    // Outside the indexed range by construction: the index was refused for this loop, so claiming a window-directed
    // answer here would present a complete scan as bounded.
    gather.result = RetainedWindowGatherResult::BarsUnsupported;
    return gather;
  }
  uint32_t coveredBars = 0;
  uint32_t skippedBars = 0;
  const RetainedIntervalQueryResult query = queryRetainedIntervalForSlot(
      slotIndex, startPhase, lengthTicks, expectedRevision, coveredBars, skippedBars);
  gather.indexedBars = coveredBars;
  gather.skippedBars = skippedBars;
  if (query == RetainedIntervalQueryResult::RevisionMismatch) {
    gather.result = RetainedWindowGatherResult::RevisionMismatch;
    return gather;
  }
  if (query == RetainedIntervalQueryResult::BarsUnsupported) {
    gather.result = RetainedWindowGatherResult::BarsUnsupported;
    return gather;
  }
  if (query == RetainedIntervalQueryResult::NoIndex) {
    // No published index for this revision: the skip cannot be claimed, and a full scan is not an interval-directed
    // answer. (A 513-bar loop reaches here *and* the ceiling above; both refuse by name, and the caller accounts the
    // scan it now owes.)
    gather.result = RetainedWindowGatherResult::NoIndex;
    return gather;
  }
  // The retained revision's own Active Note note-edit rows (below) and the index's empty answer are both decided after
  // the **pinned list set** is known: per-layer resolution needs the layers, and the index alone may only answer an
  // interval as empty when no edit row would add events to it (a `Create` row carries its own events).
  // The pinned passes are the revision's **membership**, read from the pin rather than from the live states: a pass a
  // later revision disabled is exactly the lag the retention exists to serve. A pin whose body is no longer reachable
  // is counted, and serving an empty window in that state would be indistinguishable from silence -- so it is refused.
  std::vector<const CommittedChunkIdList*> lists;
  uint32_t missing = 0;
  collectPinnedCaptureLists(loop, *source, lists, missing);
  gather.pinnedPassesMissing = missing;
  gather.pinnedLists = static_cast<uint32_t>(lists.size());
  if (lists.empty() && !source->ownedChunks.empty()) {
    // The revision **owns bodies** (the retention took the store's references for them) and yet none is reachable
    // through the pass set: an empty window here would be served as silence for content the revision demonstrably has,
    // so it is refused by name instead. `ownedChunks` empty means the revision genuinely carried no bodies.
    gather.result = RetainedWindowGatherResult::PinnedBodiesUnavailable;
    gather.complete = cursor.complete;
    return gather;
  }
  // the passes this revision pinned (a row created after the retention belongs to a later revision, and the retained
  // revision's own membership is what a window for it may serve).
  const bool editedRevision = retainedRevisionHasActiveEditRows(loop, *source);
  if (!editedRevision && coveredBars == 0) {
    // **The index alone answers**: every bar of the interval is one the pinned bodies do not cover, so the interval is
    // served *empty* at no chunk-inspection cost. This is the lookup bound the index exists for, and it is a served
    // answer rather than a refusal -- an interval that legitimately holds no events is still ready (task 3.5). With an
    // Active Note row pinned this shortcut is **not** taken: a `Create` row adds events this interval's committed
    // bodies do not carry, so the owner's per-layer answer is not "empty".
    out.clear();
    editRowsOut.clear();
    gather.result = RetainedWindowGatherResult::Served;
    gather.complete = true;
    gather.indexAnsweredEmpty = true;
    return gather;
  }
  // The oracle's own clamping: a window at least as long as the loop *is* the loop.
  uint32_t windowStart = startPhase;
  uint32_t windowLength = lengthTicks;
  if (windowLength >= loop.loopLengthTicks) {
    windowStart = 0;
    windowLength = loop.loopLengthTicks;
  } else {
    windowStart = startPhase % loop.loopLengthTicks;
  }
  const bool freshCursor = cursor.listIndex == 0 && cursor.chunkIndex == 0 && cursor.chunkEventOffset == 0 &&
                           !cursor.complete;
  if (freshCursor) {
    // A fresh traversal owns its destination and the caller's layer scratch: the interval's events are appended to
    // them, never onto a previous answer.
    out.clear();
    editRowsOut.clear();
  }
  if (editedRevision) {
    // **The per-layer path** (task 3.7a/I7): the revision pins Active Note rows, so this revision's effective content is
    // the owner's per-layer resolution -- gathered one layer per call, never merged flat. `indexAnsweredEmpty` stays
    // false: the answer is the resolution's own, not the index's.
    gatherRetainedWindowLayers(loop, *source, lists, windowStart, windowLength, cursor, out, editRowsOut, gather);
    return gather;
  }
  if (lists.empty()) {
    gather.result = RetainedWindowGatherResult::Served;
    gather.complete = true;
    return gather;
  }
  const CommittedEventRange::WindowTraversalResult slice = CommittedEventRange::appendWindowBudgeted(
      lists.data(), lists.size(), loop.loopLengthTicks, windowStart, windowLength, kRetainedGatherSliceEvents,
      cursor, out);
  gather.result = RetainedWindowGatherResult::Served;
  gather.complete = slice.complete;
  gather.inspectedChunks = static_cast<uint32_t>(slice.chunksCopied);
  gather.eventsAppended = static_cast<uint32_t>(slice.eventsAppended);
  if (slice.complete) {
    // Tick order, which is the shape the fallback's own effective-content gather produces for the same revision --
    // **by hand** rather than through `std::sort`: that instantiation (introsort, its insertion sorts and the
    // long-branch veneers from this flash-resident TU) measured 1,280 B of ITCM against a 1,640 B headroom, and the
    // window is bounded by `kWindowAdmissionLimit` events. The ordering bound is the completion boundary's business
    // when this gather is driven per turn; here it is one off-clock call.
    for (size_t i = 1; i < out.size(); ++i) {
      const MidiEvent key = out[i];
      size_t j = i;
      while (j > 0 && out[j - 1].tick > key.tick) {
        out[j] = out[j - 1];
        --j;
      }
      out[j] = key;

    }
  }
  return gather;
}

/// Task 3.7a (I1/I3b): **the retained revision's pinned capture bodies**, in pass-set order (record first, then
/// overdubs), with the count of pinned passes whose descriptor is no longer reachable. One implementation, because the
/// window gather and the paged legacy producer must enumerate the *same* content for the same revision -- two
/// enumerations could disagree, and then the window and the pages would serve different loops.
static void collectPinnedCaptureLists(
    const Loop& loop, const RetainedPlaybackSource& source,
    std::vector<const CommittedChunkIdList*>& listsOut, uint32_t& missingOut) {
  listsOut.clear();
  listsOut.reserve(source.pins.capturePassIds.size());
  missingOut = 0;
  for (const PassId pinnedId : source.pins.capturePassIds) {
    const CommittedChunkIdList* list = nullptr;
    if (loop.passes.hasRecordPass() && loop.passes.recordPass.id == pinnedId) {
      list = &loop.passes.recordPass.committedChunkIds;
    } else {
      for (const OverdubPass& pass : loop.passes.overdubPasses) {
        if (pass.id == pinnedId) {
          list = &pass.committedChunkIds;
          break;
        }
      }
    }
    if (list == nullptr) {
      ++missingOut;
      continue;
    }
    if (!list->empty()) {
      listsOut.push_back(list);
    }
  }
}

/// Task 3.7a (I2): the gather's own refusal name, as the runtime records it. One mapping, so the reason a demand is
/// still owed and the primitive's answer cannot drift apart.
static LoopPlaybackRuntime::RetainedGatherRefusal retainedRefusalForGatherResult(
    Track::RetainedWindowGatherResult result) {
  if (result == Track::RetainedWindowGatherResult::NoRetainedSource) {
    return LoopPlaybackRuntime::RetainedGatherRefusal::NoSource;
  }
  if (result == Track::RetainedWindowGatherResult::RevisionMismatch) {
    return LoopPlaybackRuntime::RetainedGatherRefusal::RevisionMismatch;
  }
  if (result == Track::RetainedWindowGatherResult::BarsUnsupported) {
    return LoopPlaybackRuntime::RetainedGatherRefusal::BarsUnsupported;
  }
  if (result == Track::RetainedWindowGatherResult::NoIndex) {
    return LoopPlaybackRuntime::RetainedGatherRefusal::NoIndex;
  }
  if (result == Track::RetainedWindowGatherResult::PinnedBodiesUnavailable) {
    return LoopPlaybackRuntime::RetainedGatherRefusal::BodiesUnavailable;
  }
  return LoopPlaybackRuntime::RetainedGatherRefusal::NoSource;
}

/// Task 3.7a (I2): the window lane's **source currency**. A window may be built from the current published root, or --
/// for a slot whose root is not resident, which is the demoted shape this change exists for -- from the retained
/// revision the slot admitted for exactly the revision it now carries (DEC-108 §10). The generation is not currency,
/// and the retained *role* is irrelevant here: what matters is that a revision's bodies are held and that revision is
/// the loop's current one.
TRACK_COLD_MEM bool Track::playbackWindowSourceIsCurrentForSlot(uint8_t slotIndex) const {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || !loopsAllocated()) {
    return false;
  }
  const Loop& loop = loopForSlot(slotIndex);
  if (loop.publishedSnapshotIsCurrent()) {
    return true;
  }
  return servingRetainedRevisionForSlot(slotIndex) == loop.playbackRevision;
}

/// Task 3.7a (I2): **begin the retained-source gather** for this slot's pending demand. The preconditions and the freeze
/// are the root-backed qualification's own (`LoopPlaybackRuntime::beginCoverageQualification`): a free build role, a
/// committed demand, and a record discharged by the freeze rather than kept beside the frozen copy. What differs is the
/// source: the interval is enumerated from the retained revision's pinned bodies, one bounded page per turn, instead of
/// from a root's window cursor.
TRACK_COLD_MEM bool Track::beginRetainedSourceGatherForSlot(uint8_t slotIndex) {
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr || !runtime->coverageRoleAvailable() || !runtime->pendingViewRequest_.active) {
    return false;
  }
  const auto kind = runtime->pendingViewRequest_.sourceKind;
  if (kind != PlaybackViewPendingRequest::SourceKind::CommittedSnapshot &&
      kind != PlaybackViewPendingRequest::SourceKind::CommittedLaunch) {
    return false;
  }
  if (!playbackWindowSourceIsCurrentForSlot(slotIndex)) {
    // Named, and nothing is read: this demand is owed a *source*, not a window.
    runtime->retainedGatherRefusal = LoopPlaybackRuntime::RetainedGatherRefusal::NoSource;
    return false;
  }
  runtime->discardRetainedGather();
  runtime->retainedGatherRefusal = LoopPlaybackRuntime::RetainedGatherRefusal::None;
  runtime->retainedGatherPhase = LoopPlaybackRuntime::RetainedGatherPhase::Gathering;
  runtime->retainedGatherUnits = 0;
  runtime->retainedGatherInspectedChunks = 0;
  // The freeze is the qualification's own rule: the frozen copy owns the promise, the record is discharged, and a later
  // demand starts a fresh record rather than sharing this one's deadline.
  runtime->frozenViewRequest = runtime->pendingViewRequest_;
  runtime->clearViewPendingRequest();
  return true;
}

/// Task 3.7a (I2): one turn of that gather. The demand's **own** promise is validated before anything is served (a
/// geometry or epoch change voids its position and a passed deadline expires it: none is re-granted here), then one
/// bounded slice is gathered, and the completing slice admits the window through `requestPlaybackViewMaintenance` --
/// the same admission a requester uses, so the derived cost, the retained work record, the deadline arithmetic and the
/// discharge rule cannot differ between the two callers.
///
/// A refusal never drops the demand: the record is coalesced back for the admission path to read (the oldest deadline
/// of the same operation survives -- `retainViewPendingRequest`'s own rule), and the gather's window is released.
TRACK_COLD_MEM Track::PlaybackViewTurnResult Track::stepRetainedSourceGatherForSlot(
    uint8_t slotIndex, PlaybackDeadlineClock clock) {
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr || !runtime->retainedGatherActive() || !runtime->frozenViewRequest.active) {
    if (runtime != nullptr) runtime->discardRetainedGather();
    return PlaybackViewTurnResult::GrainDenied;
  }
  const Loop& loop = loopForSlot(slotIndex);
  PlaybackViewPendingRequest& request = runtime->frozenViewRequest;
  const bool launch = request.sourceKind == PlaybackViewPendingRequest::SourceKind::CommittedLaunch;
#if PLAYBACK_WINDOW_ROLLING_ENABLED
  const bool cold = coldRollingRequest(*runtime, request);
#else
  const bool cold = false;
#endif
  const bool geometryChanged = request.loopLengthTicks != loop.loopLengthTicks;
  const bool epochChanged = !cold && !launch && request.clockEpoch != 0 &&
      clock.epoch != request.clockEpoch;
  const bool expired = !cold && !launch && request.deadlineTick != 0 && clock.tick >= request.deadlineTick;
  const bool sourceMoved = request.source.playbackRevision != loop.playbackRevision ||
                           !playbackWindowSourceIsCurrentForSlot(slotIndex);
  if (geometryChanged || epochChanged || expired || sourceMoved || request.positionInvalidated) {
    runtime->retainedGatherRefusal = geometryChanged
        ? LoopPlaybackRuntime::RetainedGatherRefusal::GeometryInvalidated
        : (epochChanged ? LoopPlaybackRuntime::RetainedGatherRefusal::EpochInvalidated
                        : (expired ? LoopPlaybackRuntime::RetainedGatherRefusal::DeadlineExpired
                                   : LoopPlaybackRuntime::RetainedGatherRefusal::NoSource));
    if (launch) {
      // A launch has no positional promise to keep: its preparation is cancelled by name, and the switch that wanted
      // it keeps the state it had.
      runtime->frozenViewRequest = {};
      runtime->discardRetainedGather();
      cancelPlaybackLaunchForSlot(slotIndex);
      return PlaybackViewTurnResult::GrainDenied;
    }
    request.positionInvalidated = true;
    request.reason = geometryChanged ? PlaybackViewPendingReason::DeadlineInvalidatedGeometry
        : (epochChanged ? PlaybackViewPendingReason::DeadlineInvalidatedEpoch
                        : PlaybackViewPendingReason::DeadlineExpired);
    if (!runtime->pendingViewRequest_.active) {
      runtime->pendingViewRequest_ = request;
    }
    runtime->frozenViewRequest = {};
    runtime->discardRetainedGather();
    return PlaybackViewTurnResult::GrainDenied;
  }
  if (runtime->retainedGatherUnits >= kRetainedGatherMaxSlices) {
    // Task 3.7a (I3b) / DEC-108 §14(c): **this demand does not fit one window.** `appendWindowBudgeted` is bounded by
    // events per call, so a sparse interval completes in a handful of slices and cannot reach this ceiling; reaching it
    // means the interval's own content is over `L`. The honest answer is therefore no longer only a refusal: the *same*
    // retained bodies are prepared as pages for the same revision (a bounded ring, while the content stays unbounded),
    // and the turn immediately produces that preparation's first page -- the switch itself does no gathering.
    if (!launch && beginPagedPlaybackForSlot(slotIndex)) {
      runtime->discardRetainedGather();
      return stepPagedPlaybackForSlot(slotIndex, clock);
    }
    // Task 3.12 / 5.5 and task 3.7a / I7: and this is the second place the switch can be **refused by name** -- the slice
    // ceiling route measures the interval's closure high-water over the traversal this turn completed, which is a
    // *prefix* of the interval, so a refusal here is never an over-refusal (a prefix's high-water can only under-report
    // the whole's); and a revision whose pins carry Active Note rows is not the pager's to serve at all.
    request.reason = runtime->pagedOutcome == LoopPlaybackRuntime::PagedPlaybackOutcome::ClosuresExceedDeclaredCapacity
        ? PlaybackViewPendingReason::ClosuresExceedDeclaredCapacity
        : (runtime->pagedOutcome == LoopPlaybackRuntime::PagedPlaybackOutcome::PinnedEditRowsUnresolved
               ? PlaybackViewPendingReason::PinnedEditRowsUnresolved
               : PlaybackViewPendingReason::WindowExceedsRetainedCapacity);
    runtime->retainedGatherRefusal = LoopPlaybackRuntime::RetainedGatherRefusal::WindowExceedsCapacity;
    if (!runtime->pendingViewRequest_.active) {
      runtime->pendingViewRequest_ = request;
    }
    runtime->frozenViewRequest = {};
    runtime->discardRetainedGather();
    return PlaybackViewTurnResult::GrainDenied;
  }
  const RetainedWindowGather gathered = gatherRetainedSourceWindowForSlot(
      slotIndex, request.coverage.startPhase, request.coverage.lengthTicks, request.source.playbackRevision,
      runtime->retainedGatherCursor, runtime->retainedGatherEvents, runtime->retainedGatherEditRows);
  if (gathered.result != RetainedWindowGatherResult::Served) {
    runtime->retainedGatherRefusal = retainedRefusalForGatherResult(gathered.result);
    request.reason = PlaybackViewPendingReason::SourceNotCurrent;
    if (!runtime->pendingViewRequest_.active) {
      runtime->pendingViewRequest_ = request;
    }
    runtime->frozenViewRequest = {};
    runtime->discardRetainedGather();
    return PlaybackViewTurnResult::GrainDenied;
  }
  ++runtime->retainedGatherUnits;
  runtime->retainedGatherInspectedChunks += gathered.inspectedChunks;
  if (!gathered.complete) {
    return PlaybackViewTurnResult::GrainServed;
  }
  // The completing slice: admit the gathered window. The phase the clock names **now** is what the admission may
  // promise a lead from (a launch has no positional promise: its own requested phase is the only one it carries), and
  // the interval and identity are the demand's own.
  // The completing slice: admit the gathered window. The phase a **qualified** clock names now is the admission's own
  // input (the launch carries no positional promise, so an unqualified clock falls back to the phase the demand was
  // made at), and the interval and identity are the demand's own.
  uint32_t currentPhase = request.requestedPhase;
  if (clock.epoch != 0) {
    const uint32_t effectiveTick = getEffectivePlaybackTick(static_cast<uint32_t>(clock.tick));
    currentPhase = IntervalProjection::tickPhaseInProjectionCycle(
        effectiveTick, getProjectionCycleStartTick(), request.coverage.loopLengthTicks);
  }
  const PlaybackViewRequestResult admitted = requestPlaybackViewMaintenance(
      slotIndex, runtime->retainedGatherEvents.data(), runtime->retainedGatherEvents.size(), request.source,
      request.coverage, {}, {}, currentPhase, request.coverage.startPhase, clock);
  if (admitted != PlaybackViewRequestResult::Admitted) {
    // Task 3.7a (I3b): the **one** refusal on this path that is not an answer is the destination's own capacity
    // bound -- the gathered window is over `L`, which is exactly what the paged path exists for. The demand is
    // switched to pages for the same revision, and the admission is *not* argued with (its bound is its own rule).
    // A launch keeps its fallback, and a demand whose bodies cannot be paged keeps the refusal below.
    if (admitted == PlaybackViewRequestResult::WindowExceedsRetainedCapacity && !launch &&
        beginPagedPlaybackForSlot(slotIndex)) {
      runtime->discardRetainedGather();
      return stepPagedPlaybackForSlot(slotIndex, clock);
    }
    // Task 3.12 / 5.5 and task 3.7a / I7: the switch can also be refused by name -- when the interval's own content
    // exceeds the emission service's declared closure capacity, or when the revision pins Active Note note-edit rows the
    // page producer does not resolve, `beginPagedPlaybackForSlot` says so and no ring is established. The refusal is the
    // demand's own reason from here (terminal for this lane's retry, exactly as the over-`L` refusal below), so the
    // refusals stay distinguishable to a reader instead of collapsing into one name.
    const bool closuresRefused =
        runtime->pagedOutcome == LoopPlaybackRuntime::PagedPlaybackOutcome::ClosuresExceedDeclaredCapacity;
    const bool editRowsRefused =
        runtime->pagedOutcome == LoopPlaybackRuntime::PagedPlaybackOutcome::PinnedEditRowsUnresolved;
    // The gather's own refusal name is unchanged: what stopped the window lane is that the interval is over its
    // capacity, and the paged lane's own refusal is additive (it names *why* the pager declined to take over).
    runtime->retainedGatherRefusal = admitted == PlaybackViewRequestResult::WindowExceedsRetainedCapacity
        ? LoopPlaybackRuntime::RetainedGatherRefusal::WindowExceedsCapacity
        : LoopPlaybackRuntime::RetainedGatherRefusal::NoSource;
    // The demand is **coalesced back** into the record the lane reads (the oldest deadline of the same operation
    // survives, and an admission that already retained its own record is left untouched): a window that could not be
    // admitted is still owed an answer, and dropping it here is what would make a refusal indistinguishable from a
    // fulfilled promise.
    request.reason = closuresRefused ? PlaybackViewPendingReason::ClosuresExceedDeclaredCapacity
        : (editRowsRefused
               ? PlaybackViewPendingReason::PinnedEditRowsUnresolved
               : (admitted == PlaybackViewRequestResult::WindowExceedsRetainedCapacity
                      ? PlaybackViewPendingReason::WindowExceedsRetainedCapacity
                      : PlaybackViewPendingReason::SourceNotCurrent));
    if (!runtime->pendingViewRequest_.active) {
      runtime->pendingViewRequest_ = request;
    }
    runtime->frozenViewRequest = {};
    runtime->discardRetainedGather();
    return PlaybackViewTurnResult::GrainDenied;
  }
  runtime->frozenViewRequest = {};
  if (launch) {
    // The launch lane's own bookkeeping, exactly as `beginQualifiedCoverageBuild` performs it for the root-backed
    // path: the promotion to `Ready` at completion is the owner's existing branch.
    runtime->launchPreparation = LoopPlaybackRuntime::LaunchPreparation::Building;
  }
  runtime->discardRetainedGather();
  return PlaybackViewTurnResult::GrainServed;
}

/// Task 3.12 / 5.5: **the interval's own closure profile** — how many ledger identities its content holds open at once,
/// in the order the delivery presents it, and whether that content owes closures at all (`out` is appended by the same
/// budgeted window traversal the pages are produced from, so no second pass and no ordering assumption is introduced
/// here). One increment per event, bounded by the content the gather already traversed, allocation-free, and off-clock.
///
/// **What the two figures are, and what they are not.** A note-on opens one identity and a note-off closes one, so
/// `highWaterOut` is the ledger's occupancy trajectory for content whose every note-off closes one of its own note-ons
/// (which is what the stored representation holds: a pair is a pair). Three consequences are deliberate:
///
///   - it is a **prefix-safe** figure: a prefix's maximum can only be ≤ the whole traversal's, so a refusal decided on
///     a partially gathered interval (the slice-ceiling route) can never refuse content the whole would have carried;
///   - the criterion is `owesClosures` **and** over-capacity, not the peak alone: content that is nothing but
///     note-ons exceeds the capacity while losing **nothing**, because a table-full note-on is still emitted and there
///     is no closure it could orphan (the I3b/I5 fixtures' shape, and the reason the peak by itself would over-refuse);
///   - a note-off the ledger would refuse as an *orphan* is counted here as closing, so content that mis-pairs is
///     **not** detected by this preflight. The emission service's own refusal report is the backstop owed for that
///     shape, and it is named as an open case rather than invented here.
TRACK_COLD_MEM void closureProfileOfEvents(const MidiEvent* events, size_t count, uint32_t& highWaterOut,
                                           bool& owesClosuresOut) {
  highWaterOut = 0;
  owesClosuresOut = false;
  if (events == nullptr) {
    return;
  }
  uint32_t open = 0;
  for (size_t i = 0; i < count; ++i) {
    if (events[i].isNoteOn()) {
      ++open;
      if (open > highWaterOut) {
        highWaterOut = open;
      }
    } else if (events[i].isNoteOff()) {
      if (open > 0) {
        --open;
      }
      owesClosuresOut = true;
    }
  }
}
///
/// Task 3.7a (I3b) / DEC-108 §14(c): **prepare the interval that cannot fit one window as pages.** The source is the
/// same retained revision and, through `collectPinnedCaptureLists`, the same pinned bodies the gather reads -- one
/// enumeration, so the two readers cannot serve different content for one revision. The demand is not discharged here:
/// the frozen request stays the promise, and the ring becomes its destination (this is the switch, not the delivery).
///
/// **Idempotent for an interval whose pages are already held:** a re-demand must **not** re-prepare, because `reset` would
/// clear pages a consumer has not walked yet (`WindowPageCursor` carries the revision only, so a re-prepared interval
/// would hand an old position to new content -- the preparation therefore resets the cursor, and the slot keeps
/// **one walker**).
TRACK_COLD_MEM bool Track::beginPagedPlaybackForSlot(uint8_t slotIndex) {
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr || !runtime->frozenViewRequest.active) {
    return false;
  }
  const PlaybackViewPendingRequest& request = runtime->frozenViewRequest;
  if (request.sourceKind != PlaybackViewPendingRequest::SourceKind::CommittedSnapshot) {
    // A **launch** keeps its own answer for dense legacy content: the merged-material fallback is a working delivery,
    // and nothing emits from pages until I3c -- switching a launch here would take the fallback away and give back a
    // preparation with no consumer. Named rather than silent (`pagedOutcome` stays `None`).
    return false;
  }
  const uint32_t revision = request.source.playbackRevision;
  if (runtime->pagedActiveForInterval(revision, request.coverage)) {
    return true;  // already prepared for this revision and interval: the pages stay, exactly as they are
  }
  const Loop& loop = loopForSlot(slotIndex);
  const RetainedPlaybackSource* source = nullptr;
  for (const RetainedPlaybackSource& entry : runtime->retainedSources) {
    if (entry.isRetained() && entry.revision == revision) {
      source = &entry;
      break;
    }
  }
  if (source == nullptr) {
    return false;
  }
  std::vector<const CommittedChunkIdList*> lists;
  uint32_t missing = 0;
  collectPinnedCaptureLists(loop, *source, lists, missing);
  if (lists.empty()) {
    // No reachable body to page: the interval cannot be prepared from this revision at all, so it stays owed to a
    // source rather than being delivered as silence.
    runtime->pagedOutcome = LoopPlaybackRuntime::PagedPlaybackOutcome::NoBodies;
    return false;
  }
  // Task 3.7a / I7: **the pinned revision's Active Note note-edit rows are not the pager's to resolve.** Its pages are
  // the pinned bodies in traversal order, and the effective-content owner resolves those rows **per pass layer** (a
  // merged or flat application is a different answer -- a LIFO delete would pair a hidden On with another pass's Off),
  // so paging such a revision would emit content the edited revision does not have. The ring is therefore **not
  // established** and the refusal is named: the window lane's own per-layer resolution serves what fits a window, and
  // this is the reason the rest stays owed rather than being served as unresolved bodies.
  for (const EditPass& row : loop.passes.editPasses) {
    if (row.state != EditPassState::Active || row.passType != EditPassType::Note) {
      continue;
    }
    for (const EditPassId pinnedId : source->pins.editPassIds) {
      if (row.id == pinnedId) {
        runtime->pagedOutcome = LoopPlaybackRuntime::PagedPlaybackOutcome::PinnedEditRowsUnresolved;
        return false;
      }
    }
  }
  // Task 3.12 / 5.5 (the 3.12e falsification's repair): **the emission service's declared closure capacity, consulted
  // before the ring exists.** The 3.12e drive measured that this path served an over-`L` paired interval whose content
  // holds ~157 ledger identities open at once while the declared capacity is `ActiveNoteLedger::kMaxOpenNotes` (128):
  // the ledger stopped recording note-ons (its table-full policy emits the On and refuses the identity) and then
  // refused every later note-off for an unrecorded identity as an orphan, so the slot emitted note-ons it could never
  // close -- 370 wrong rows, stuck notes -- and the walk's own per-frame `consumed == due` shows neither the walk nor
  // the producer's page rate was ever the constraint. Serving a density the declared service cannot carry is worse
  // than not serving from pages (I3c-b's lesson), so the delivery is **refused by name** and the ring is never
  // established: nothing is produced, nothing is emitted, and the demand stays owed (`pendingViewRequest_`), never
  // dropped. The criterion is the interval's own content, read from the traversal the gather already performed for
  // this interval -- the completing route's reading is exact, and the slice-ceiling route's prefix can only
  // under-report -- and it is the peak **and** that the content owes closures at all: note-on-only content exceeds the
  // capacity while losing nothing (a table-full note-on is still emitted, and there is no closure for it to orphan),
  // so the peak alone would refuse content the service demonstrably carries.
  uint32_t closureHighWater = 0;
  bool owesClosures = false;
  closureProfileOfEvents(runtime->retainedGatherEvents.data(), runtime->retainedGatherEvents.size(),
                         closureHighWater, owesClosures);
  if (owesClosures && closureHighWater > ActiveNoteLedger::kMaxOpenNotes) {
    runtime->pagedOutcome = LoopPlaybackRuntime::PagedPlaybackOutcome::ClosuresExceedDeclaredCapacity;
    return false;
  }
  // The switch: the demand's own window lane states *why* it stopped being a window lane (`WindowExceedsCapacity` is
  // the named fact -- the interval is over `L`), and then the ring is established for the same revision and interval,
  // its pages preallocated, and the consumer's position started at the interval's own beginning.
  runtime->retainedGatherRefusal = LoopPlaybackRuntime::RetainedGatherRefusal::WindowExceedsCapacity;
  runtime->discardPagedWindow();
  runtime->pagedWindow.reset(revision);
  runtime->pagedCursor = WindowPageCursor{};
  runtime->pagedActive = true;
  runtime->pagedRevision = revision;
  runtime->pagedInterval = request.coverage;
  runtime->pagedPagesProduced = 0;
  runtime->pagedOutcome = LoopPlaybackRuntime::PagedPlaybackOutcome::None;
  // Task 3.7a (I3c-a): a **new delivery** starts from a fresh ring, so the emission state is bound to it -- a position
  // left over from a previous interval or revision would address ordinals that no longer exist (the ordinal space is
  // monotone only *within* one delivery; a new source is `reset`, which restarts the sequence numbering).
  runtime->pagedEmission.bindToDelivery();
  return true;
}

/// Task 3.7a (I3b): **one turn of that preparation.** The demand's own promise is validated exactly as the gather's is
/// (geometry, epoch, deadline, identity -- none of them re-granted), then **at most one page** is produced. `RingFull`
/// is the ring's own bound doing its job: the producer waits for its consumer instead of allocating or overwriting, and
/// the demand stays owed. `SourceComplete` is the **delivery**: the pages hold the whole interval for their revision,
/// which is all the window lane can promise for content that cannot fit a window -- so the promise is discharged there
/// and the interval's serving becomes the pager's own consumer.
TRACK_COLD_MEM Track::PlaybackViewTurnResult Track::stepPagedPlaybackForSlot(
    uint8_t slotIndex, PlaybackDeadlineClock clock) {
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr || !runtime->pagedActive) {
    if (runtime != nullptr) runtime->discardPagedWindow();
    return PlaybackViewTurnResult::GrainDenied;
  }
  if (!runtime->frozenViewRequest.active) {
    // The ring outlives its promise only as a standing delivery. Anything else is a leftover whose page buffers nobody
    // owes -- released here rather than left resident per slot.
    if (runtime->pagedOutcome != LoopPlaybackRuntime::PagedPlaybackOutcome::Prepared) {
      runtime->discardPagedWindow();
    }
    return PlaybackViewTurnResult::GrainDenied;
  }
  const Loop& loop = loopForSlot(slotIndex);
  PlaybackViewPendingRequest& request = runtime->frozenViewRequest;
  const bool geometryChanged = request.loopLengthTicks != loop.loopLengthTicks ||
      request.coverage.loopLengthTicks != loop.loopLengthTicks;
  const bool epochChanged = request.clockEpoch != 0 && clock.epoch != request.clockEpoch;
  const bool expired = request.deadlineTick != 0 && clock.tick >= request.deadlineTick;
  const bool sourceMoved = request.source.playbackRevision != loop.playbackRevision ||
                           !playbackWindowSourceIsCurrentForSlot(slotIndex);
  if (geometryChanged || epochChanged || expired || sourceMoved || request.positionInvalidated) {
    // Voided, and **named**: the promise is not silently re-granted, and the pages prepared under it are released
    // (a delivery for an interval whose identity moved is not a delivery).
    runtime->pagedOutcome = LoopPlaybackRuntime::PagedPlaybackOutcome::Cancelled;
    request.positionInvalidated = true;
    request.reason = geometryChanged ? PlaybackViewPendingReason::DeadlineInvalidatedGeometry
        : (epochChanged ? PlaybackViewPendingReason::DeadlineInvalidatedEpoch
                        : PlaybackViewPendingReason::DeadlineExpired);
    if (!runtime->pendingViewRequest_.active) {
      runtime->pendingViewRequest_ = request;
    }
    runtime->frozenViewRequest = {};
    runtime->discardPagedWindow();
    return PlaybackViewTurnResult::GrainDenied;
  }
  if (runtime->pagedWindow.sourceComplete()) {
    // The preparation completed on an earlier turn: this turn is the demand's own end (it was frozen for a *window*,
    // and a window is what the content cannot fit). Discharged as the named delivery, not dropped.
    runtime->pagedOutcome = LoopPlaybackRuntime::PagedPlaybackOutcome::Prepared;
    runtime->frozenViewRequest = {};
    return PlaybackViewTurnResult::GrainServed;
  }
  const RetainedPlaybackSource* source = nullptr;
  for (const RetainedPlaybackSource& entry : runtime->retainedSources) {
    if (entry.isRetained() && entry.revision == request.source.playbackRevision) {
      source = &entry;
      break;
    }
  }
  if (source == nullptr) {
    // The revision's bodies are no longer reachable: the preparation cannot be finished, and the demand goes back to
    // owing a *source* (its own name), with the ring released.
    runtime->pagedOutcome = LoopPlaybackRuntime::PagedPlaybackOutcome::NoBodies;
    request.reason = PlaybackViewPendingReason::SourceNotCurrent;
    if (!runtime->pendingViewRequest_.active) {
      runtime->pendingViewRequest_ = request;
    }
    runtime->frozenViewRequest = {};
    runtime->discardPagedWindow();
    return PlaybackViewTurnResult::GrainDenied;
  }
  if (runtime->pagedEmission.emitting &&
      runtime->pagedWindow.pagesPublished() + 1u >= kWindowPageCount) {
    // Task 3.7a (I3c-b): an **emitting** delivery keeps one page free. That is what lets the producer hand over the
    // next pass's head while this pass's tail is still queued -- the lead across the wrap the I3c-a measurement showed
    // a fully-occupied ring cannot provide (`passesUnstraddled`). A delivery nobody emits keeps I3b's full occupancy.
    runtime->pagedOutcome = LoopPlaybackRuntime::PagedPlaybackOutcome::ProducerWaiting;
    return PlaybackViewTurnResult::GrainDenied;
  }
  std::vector<const CommittedChunkIdList*> lists;
  uint32_t missing = 0;
  collectPinnedCaptureLists(loop, *source, lists, missing);
  // The gather's own clamp, so the pages describe the *same* interval the window would have described: a window at
  // least as long as the loop is the loop.
  uint32_t windowStart = request.coverage.startPhase;
  uint32_t windowLength = request.coverage.lengthTicks;
  if (windowLength >= loop.loopLengthTicks) {
    windowStart = 0;
    windowLength = loop.loopLengthTicks;
  } else {
    windowStart = request.coverage.startPhase % loop.loopLengthTicks;
  }
  const WindowPagedStream::ProduceOutcome produced = runtime->pagedWindow.produce(
      lists.data(), lists.size(), loop.loopLengthTicks, windowStart, windowLength,
      request.source.playbackRevision);
  if (produced == WindowPagedStream::ProduceOutcome::StaleRevision) {
    runtime->pagedOutcome = LoopPlaybackRuntime::PagedPlaybackOutcome::Cancelled;
    request.reason = PlaybackViewPendingReason::SourceNotCurrent;
    if (!runtime->pendingViewRequest_.active) {
      runtime->pendingViewRequest_ = request;
    }
    runtime->frozenViewRequest = {};
    runtime->discardPagedWindow();
    return PlaybackViewTurnResult::GrainDenied;
  }
  if (produced == WindowPagedStream::ProduceOutcome::RingFull) {
    // The ring is full with undrained pages: the producer waits for its consumer, and the demand stays owed (nothing
    // is dropped, nothing is overwritten, nothing is allocated).
    runtime->pagedOutcome = LoopPlaybackRuntime::PagedPlaybackOutcome::ProducerWaiting;
    return PlaybackViewTurnResult::GrainDenied;
  }
  ++runtime->pagedPagesProduced;
  if (runtime->pagedWindow.sourceComplete()) {
    // Task 3.7a (I3c-a): the pass is fully produced. A delivery the **emission** is serving keeps going: the ring
    // re-arms for the next pass -- restarting the traversal while **keeping** the pages the walk has not reached, and
    // keeping the ordinal space -- so the next pass's first page exists before the playhead reaches it. That is what
    // straddles a wrap. A delivery nobody is emitting keeps I3b's one-shot completion (`Prepared`, demand discharged),
    // which is what its own fixtures walk.
    if (runtime->pagedEmission.emitting) {
      runtime->pagedWindow.rearmForNextPass();
      // The pass's content ends here: the straddle at the next boundary is judged against this ordinal, and the pages
      // the walk has not reached stay published (that *is* the head-to-hand-over the wrap needs).
      runtime->pagedEmission.notePassContent(runtime->pagedWindow.producedOrdinal());
      runtime->pagedOutcome = LoopPlaybackRuntime::PagedPlaybackOutcome::None;
      return PlaybackViewTurnResult::GrainServed;
    }
    runtime->pagedOutcome = LoopPlaybackRuntime::PagedPlaybackOutcome::Prepared;
    runtime->frozenViewRequest = {};
  }
  return PlaybackViewTurnResult::GrainServed;
}

/// Task 3.7a (I3b) / task 3.12's owed production consumer: **walk the prepared interval exactly once.** The position is
/// the runtime's own cursor, so re-entering continues and cannot repeat; `NeedsPage` is the ring telling the caller to
/// produce, never a wait; and the revision these pages belong to is checked against the loop's **now** -- a revision the
/// loop has left is served by nobody, by name (`StaleRevision`).
TRACK_COLD_MEM Track::PagedWindowConsumeResult Track::consumePagedPlaybackWindowForSlot(
    uint8_t slotIndex, MidiEvent* out, size_t capacity, size_t& emittedOut) {
  emittedOut = 0;
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || !loopsAllocated()) {
    return PagedWindowConsumeResult::NoRuntime;
  }
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return PagedWindowConsumeResult::NoRuntime;
  }
  if (!runtime->pagedActive) {
    return PagedWindowConsumeResult::NoPages;
  }
  // Identity at the read: the pages were produced for one revision, and the loop's own revision is the only currency
  // a consumer may be served under. (A cursor cannot "resume" across revisions -- `WindowPageCursor` carries the
  // revision it was stamped with, and this check is the slot's own face of the same rule.)
  if (runtime->pagedRevision != loopForSlot(slotIndex).playbackRevision) {
    return PagedWindowConsumeResult::StaleRevision;
  }
  size_t emitted = 0;
  const WindowPagedStream::ConsumeOutcome outcome =
      runtime->pagedWindow.consume(runtime->pagedCursor, out, capacity, emitted);
  emittedOut = emitted;
  switch (outcome) {
    case WindowPagedStream::ConsumeOutcome::Emitted:
      if (runtime->pagedCursor.exhausted) {
        // Every page drained and the source complete: the delivery is finished, and the ring's storage is returned.
        runtime->pagedOutcome = LoopPlaybackRuntime::PagedPlaybackOutcome::Drained;
      }
      return PagedWindowConsumeResult::Emitted;
    case WindowPagedStream::ConsumeOutcome::NeedsPage:
      return PagedWindowConsumeResult::NeedsPage;
    case WindowPagedStream::ConsumeOutcome::Exhausted:
      // The walk is over, so the pages go back to the allocator and the **name** of what happened is written after the
      // release (the release clears the ring's own state, not the account).
      runtime->discardPagedWindow();
      runtime->pagedOutcome = LoopPlaybackRuntime::PagedPlaybackOutcome::Drained;
      return PagedWindowConsumeResult::Exhausted;
    case WindowPagedStream::ConsumeOutcome::StaleRevision:
      return PagedWindowConsumeResult::StaleRevision;
  }
  return PagedWindowConsumeResult::Exhausted;
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
    {
      const uint32_t dropBefore = probeCoverageBuildDrop(slotIndex);
      runtime->discardCoverageBuild();
      traceCoverageBuildDrop(slotIndex, CoverageBuildDropReason::CandidateReleased, dropBefore);
    }
    // Task 3.7a (I2): a **retained-source gather** is the other half of a candidate's preparation. The obligation it
    // was serving survives the release -- it is coalesced back into the record the lane reads, exactly as
    // `retireCoverageSource` coalesces a frozen root-backed request -- and only the gathered window is released.
    if (runtime->retainedGatherActive() && runtime->frozenViewRequest.active) {
      if (!runtime->pendingViewRequest_.active) runtime->pendingViewRequest_ = runtime->frozenViewRequest;
      runtime->frozenViewRequest = {};
      runtime->discardRetainedGather();
    }
    // Task 3.7a (I3b): the **paged preparation** is the third half of it, by the same rule -- the demand it serves is
    // coalesced back rather than dropped, and the ring returns its pages. A standing delivery whose consumer is
    // mid-walk is released with it: the transition is this slot's preparation boundary, and the read side names what
    // happened afterwards (`NoPages`) rather than resuming into pages that no longer exist.
    if (runtime->pagedActive) {
      if (runtime->frozenViewRequest.active && !runtime->pendingViewRequest_.active) {
        runtime->pendingViewRequest_ = runtime->frozenViewRequest;
      }
      runtime->frozenViewRequest = {};
      runtime->discardPagedWindow();
    }
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
    if (runtime->ownsLiveCaptureBuildBorrow()) return false;
    if (runtime->ownsCoverageSource() || runtime->ownsLaunchPreparation()) return false;
    // Task 3.7a (I2): a **retained-source gather** holds a window it gathered for a demand not yet admitted, so the
    // slot is owned by name until the gather is discharged or discarded.
    if (runtime->retainedGatherActive()) return false;
    // Task 3.7a (I3b): a **paged preparation** holds the ring that serves its interval (and a standing delivery holds
    // content its consumer may still walk), so the slot is owned by name until the pages are released -- by a
    // transition, by a reset, by a voided promise, or by the consumer walking the interval to its end.
    if (runtime->pagedActive) return false;
    if (runtime->ownsLiveFinalization()) return false;
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


/// **3.15f / C — the earliest future occurrence whose absolute lead admits the demand.** The next occurrence of the
/// handoff phase is not automatically eligible: on a short loop its lead is smaller than the declared cost. The same
/// occurrence is retried one loop later at a time until `canCompleteWithinLead` accepts the real absolute lead. `lead`
/// is the distance to the next occurrence; zero means "the phase is now", whose next future occurrence is a full loop
/// away. Returns false only when no occurrence within the bounded search fits (a genuinely oversized demand).
static bool earliestEligibleHandoffLead(const CoverageServiceContract& contract, uint32_t lead, uint32_t loopLength,
                                        uint16_t additionalGrains, uint64_t& outLead) {
  if (loopLength == 0) return false;
  const uint64_t first = lead == 0 ? loopLength : lead;
  for (uint32_t occurrence = 0; occurrence < 64; ++occurrence) {
    const uint64_t candidate = first + static_cast<uint64_t>(occurrence) * loopLength;
    if (candidate > UINT32_MAX) return false;
    if (contract.canCompleteWithinLead(candidate, additionalGrains)) {
      outLead = candidate;
      return true;
    }
  }
  return false;
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
#if PLAYBACK_WINDOW_ROLLING_ENABLED
  const bool cold = coldRollingRequest(*runtime, runtime->frozenViewRequest) &&
      runtime->frozenViewRequest.source == sourceIdentity &&
      runtime->frozenViewRequest.coverage.startPhase == coverage.startPhase &&
      runtime->frozenViewRequest.coverage.lengthTicks == coverage.lengthTicks;
#else
  const bool cold = false;
#endif
  const uint32_t lead = handoffLeadTicks(currentPhase, handoffPhase, coverage.loopLengthTicks);
  // **3.15f / C:** position at the earliest future occurrence whose absolute lead fits the declared cost. The
  // next occurrence is tried first (long loops are unchanged); a short loop advances whole loops until it fits.
  uint64_t positionedLead = 0;
  const bool positioned = !cold &&
      earliestEligibleHandoffLead(contract, lead, coverage.loopLengthTicks,
                                  sInterferenceBudgetEnforced ? playbackViewOutstandingGrains(slotIndex) : 0,
                                  positionedLead);
  if (positioned && clock.epoch != 0 && clock.tick <= UINT64_MAX - positionedLead) {
    admittedWork.deadlineTick = clock.tick + positionedLead;
    admittedWork.clockEpoch = clock.epoch;
  }
  admittedWork.positionedLater = positioned &&
      positionedLead > (lead == 0 ? coverage.loopLengthTicks : lead);
  // **The interference budget is computed and proved, but deliberately not enforced yet** -- the review's own ordering
  // puts that after the preventing paths are bounded and `Delta-max` is chosen. Charging a sibling's outstanding work
  // against a new window's lead only becomes usable once the per-turn advance inside that charge is the **measured**
  // one: with the declared one bar per turn, the charge for a second window on a track exceeds most loops, so enforcing
  // it now would refuse shapes the current constants admit -- the capacity reduction the review says the envelope work
  // must not start with. `playbackViewAdmissionKeepsAdmittedDeadlines` *is* the bound; step 4's formula
  // (`Delta-max x H x bounded per-track work`) is what turns it on.
  if (sInterferenceBudgetEnforced && !cold &&
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
  if (!cold && !positioned) {
    // The occurrence search (C) refuses only when no future occurrence within its bounded horizon fits, or the clock
    // is unqualified. Retention is unchanged: the demand is a deferred promise, not a lost one.
    retainRefusedPlaybackViewRequest(slotIndex, runtime, sourceCount, sourceIdentity, coverage, currentPhase,
                                     handoffPhase, clock, PlaybackViewPendingReason::CannotCompleteBeforeHandoff);
    return PlaybackViewRequestResult::CannotCompleteBeforeHandoff;
  }
  if (!runtime->beginCoverageBuild(contract, currentPhase, handoffPhase, source, sourceCount, sourceIdentity,
                                   coverage, cursorAnchor, ledgerBoundary,
                                   positioned ? static_cast<uint32_t>(positionedLead) : 0)) {
    return PlaybackViewRequestResult::AdmissionRefused;
  }
  // The admitted request's **bounded work**, recorded where the build begins -- and only once the build really began,
  // so a refused request records nothing (the record is read only while the build is active). A **new** promise
  // supersedes any retained completion (increment 28), so the retention is cleared here: only this promise's own
  // completion can set it, and a successor can never be judged as its predecessor.
  runtime->viewBuildWork = admittedWork;
  // **3.15f / C:** the admitted position is carried by the runtime until a publication either adopts the build as the
  // authoritative current window (cleared below) or leaves it as a positioned ready successor (the wait authority).
  runtime->readyPositionedLater = admittedWork.positionedLater;
  runtime->readyPositionedDeadlineTick = admittedWork.deadlineTick;
  runtime->readyPositionedClockEpoch = admittedWork.clockEpoch;
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
#if PLAYBACK_WINDOW_ROLLING_ENABLED
    } else if (runtime && runtime->frozenViewRequest.sourceKind == PlaybackViewPendingRequest::SourceKind::LiveCapture &&
               runtime->viewBuild.active()) {
      total += runtime->frozenViewRequest.work.requiredGrains();
#endif
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

TRACK_COLD_MEM bool Track::qualifiedFrozenRecordCaptureSourceForSlot(
    uint8_t slotIndex, uint64_t expectedCaptureInterval,
    RuntimePlaybackViewSource& sourceOut,
    PlaybackViewSourceIdentity& identityOut) const {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || !loopsAllocated()) return false;
  const CaptureStopPresentation* presentation = captureStopPresentationForSlot(slotIndex);
  if (presentation == nullptr || !hasRecordReaderOwnerForSlot(slotIndex) ||
      expectedCaptureInterval == 0 || presentation->slot != slotIndex ||
      presentation->interval != expectedCaptureInterval ||
      presentation->generation != presentationGenerationFor(presentation->interval) ||
      !presentation->ownsExtent || !presentation->rawRange.ready() ||
      presentation->frozenCaptureContentGeneration == 0) return false;
  const Loop& loop = loopForSlot(slotIndex);
  if (presentation->owner != &loop || presentation->loopId == kInvalidLoopId ||
      presentation->loopId != loop.loopId || presentation->loopLengthTicks == 0 ||
      presentation->loopLengthTicks != loop.loopLengthTicks) return false;
  const RuntimePlaybackViewSource source = RuntimePlaybackViewSource::fromFrozenRecordCapture(
      presentation->rawRange, presentation->frozenCaptureContentGeneration);
  if (source.context == nullptr || source.origin != PlaybackWindowOrigin::Capture ||
      source.captureContentToken != presentation->frozenCaptureContentGeneration) return false;
  sourceOut = source;
  identityOut = {loop.playbackRevision, playbackGeneration, nullptr,
                 PlaybackWindowOrigin::Capture, presentation->frozenCaptureContentGeneration};
  return true;
}

namespace {
/// DEC-136: the frozen reader a bound Record job's root successor must replace — a complete, non-raw
/// Capture-origin view built from that job's own frozen content token, for this loop's geometry. A
/// pre-publication copy keeps its own (older) revision, so revision currency is deliberately not required here;
/// the job's token is the identity that matters.
TRACK_COLD_MEM bool isFrozenRecordReturnWindow(const RuntimePlaybackView& view,
                                               uint64_t frozenCaptureContentGeneration,
                                               uint32_t loopLengthTicks) {
  return view.complete && !view.rawCoveragePrepared && view.source.snapshotIdentity == nullptr &&
      view.source.origin == PlaybackWindowOrigin::Capture &&
      view.source.captureContentGeneration != 0 &&
      view.source.captureContentGeneration == frozenCaptureContentGeneration &&
      view.coverage.loopLengthTicks == loopLengthTicks;
}
/// The reader being replaced may be the served window or the one waiting to be adopted. A plain helper rather
/// than a lambda: this TU's RAM1 bank is tight and a lambda body emitted into ITCM costs padding the build
/// guards (the same reason the rest of this TU's cold work carries the macro).
TRACK_COLD_MEM bool frozenRecordReturnWindowIsServedOrOwed(const RuntimePlaybackViewSet& set,
                                                           uint64_t frozenCaptureContentGeneration,
                                                           uint32_t loopLengthTicks) {
  const bool served = set.lifecycle.current < set.views.size() &&
      isFrozenRecordReturnWindow(set.views[set.lifecycle.current], frozenCaptureContentGeneration, loopLengthTicks);
  const bool owed = set.lifecycle.ready < set.views.size() &&
      isFrozenRecordReturnWindow(set.views[set.lifecycle.ready], frozenCaptureContentGeneration, loopLengthTicks);
  return served || owed;
}
}  // namespace

TRACK_COLD_MEM bool Track::rootReturnSuccessorIsOwedForSlot(
    uint8_t slotIndex, const PlaybackViewPendingRequest& request, PlaybackDeadlineClock clock) const {
#if !PLAYBACK_WINDOW_ROLLING_ENABLED
  (void)slotIndex;
  (void)request;
  (void)clock;
  return false;
#else
  // DEC-136 item 1: the whole classification, and nothing wider. Every term names an existing owner or state,
  // so an unrelated demand can never take the position-free route by accident.
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || !loopsAllocated() || clock.epoch == 0) return false;
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr || !request.active) return false;
  const RuntimePlaybackViewSet* set = runtime->viewSetIfPrewarmed();
  if (set == nullptr || !runtime->hasBoundRecordReturnWindowForPendingJob()) return false;
  const CaptureStopPresentation* presentation = captureStopPresentationForSlot(slotIndex);
  if (presentation == nullptr || !presentation->retirementActive) return false;
  RuntimePlaybackViewSource frozen{};
  PlaybackViewSourceIdentity receipt{};
  if (!qualifiedFrozenRecordCaptureSourceForSlot(slotIndex, presentation->interval, frozen, receipt)) return false;
  const Loop& loop = loopForSlot(slotIndex);
  if (loop.loopLengthTicks == 0 || !loop.publishedSnapshotIsCurrent()) return false;
  // The successor must be the **current published root's own content for this loop**: that is the only thing
  // that can discharge the job's source responsibility (S7.5b.5 reads exactly this identity).
  if (request.sourceKind != PlaybackViewPendingRequest::SourceKind::CommittedSnapshot ||
      request.source.origin != PlaybackWindowOrigin::Committed ||
      request.source.playbackRevision != loop.playbackRevision ||
      request.source.sourceGeneration != playbackGeneration ||
      request.source.snapshotIdentity != loop.publishedSnapshotIdentity()) {
    return false;
  }
  if (!request.coverage.isValid() || request.coverage.loopLengthTicks != loop.loopLengthTicks) return false;
  // The interval must be one the rolling reader's own window grid can adopt and re-serve: the same shape the
  // window owner prepares everywhere else. A demand raised at phase `p` is the return of the interval that
  // contained `p`, and its adoption boundary is that interval's next entry, so *timing* is deliberately not
  // judged here -- the existing boundary adoption (`coverage.startPhase == phase`, or containment on the idle
  // edge) still decides when the completed window is useful, and an unadoptable window is refused there.
  if (request.coverage.startPhase % Config::TICKS_PER_BAR != 0 ||
      request.coverage.lengthTicks != Config::TICKS_PER_BAR ||
      loop.loopLengthTicks % Config::TICKS_PER_BAR != 0) {
    return false;
  }
  return frozenRecordReturnWindowIsServedOrOwed(*set, presentation->frozenCaptureContentGeneration, loop.loopLengthTicks);
#endif
}

TRACK_COLD_MEM bool Track::ordinaryAdmissionCannotServeDemand(
    const CoverageServiceContract& contract, uint32_t loopLengthTicks) const {
  // DEC-136 item 3: the waiver is the envelope's own impossibility, so it is asked of the envelope. An
  // undeclared contract proves nothing and grants nothing; a loop long enough to serve the cost keeps the
  // ordinary path (and its named refusal) in charge.
  if (loopLengthTicks == 0 || !contract.isDeclared()) return false;
  return !contract.canCompleteWithinLead(loopLengthTicks);
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


TRACK_COLD_MEM Track::PlaybackViewRequestResult Track::requestActiveCommittedPlaybackWindow(
    uint32_t playheadPhase, PlaybackDeadlineClock clock) {
  return requestCommittedPlaybackWindowForSlot(getActiveLoopIndex(), playheadPhase, clock);
}

TRACK_COLD_MEM Track::PlaybackViewRequestResult Track::requestCommittedPlaybackWindowForSlot(
    uint8_t slotIndex, uint32_t playheadPhase, PlaybackDeadlineClock clock) {
#if PLAYBACK_WINDOW_ROLLING_ENABLED
  const uint8_t slot = slotIndex;
  if (slot >= Config::MAX_LOOPS_PER_TRACK) return PlaybackViewRequestResult::NoRuntime;
  Loop& loop = loopForSlot(slot);
  if (!loop.hasCommittedPasses() || loop.loopLengthTicks == 0) {
    return PlaybackViewRequestResult::NotServable;
  }
  // Whole-take (undemoted) slots keep their existing route; the windowed lane's production scope is the
  // demoted shape (the 1.4 scope split). The legacy fallbacks remain for every refusal below.
  const bool demoted = loop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks);
  if (!demoted) return PlaybackViewRequestResult::NotServable;
  if (playbackWindowReadinessForSlot(slot) == PlaybackWindowReadiness::Ready) {
    return PlaybackViewRequestResult::NotServable;
  }
  // **DEC-194 — the committed passes are enough for the first bounded window.** A rootless slot that is being asked for
  // playback retains its committed source here: pass/chunk pins only, no event gather and no root, so the window lane can
  // build the first window while whole-root publication remains optional background work. Retention is idempotent
  // (`AlreadyRetained` refreshes the role and never re-owns a body), so the per-turn demand costs a lookup, not a pin.
  if (!loop.publishedSnapshotIsCurrent() &&
      servingRetainedRevisionForSlot(slot) != loop.playbackRevision) {
    const PlaybackSourceRetainResult retained =
        retainPlaybackSourceForSlot(slot, loop.playbackRevision, playbackGeneration);
    if (retained != PlaybackSourceRetainResult::Admitted &&
        retained != PlaybackSourceRetainResult::AlreadyRetained) {
      return PlaybackViewRequestResult::SourceUnavailable;
    }
  }
  PlaybackCoverageInterval coverage{};
  if (!playbackWindowInterval(loop.loopLengthTicks, demoted, playheadPhase, coverage)) {
    return PlaybackViewRequestResult::NotServable;
  }
  const uint32_t phase = playheadPhase % loop.loopLengthTicks;
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slot);
  if (runtime != nullptr) {
    // Pass publication precedes root readiness. A committed request here would
    // take the holder ahead of frozen Record preparation, then wait on the very
    // root that the audible reader must survive without. Leave that interval
    // with its qualified frozen owner until a current root can replace it.
    if (!loop.publishedSnapshotIsCurrent() && runtime->hasBoundRecordReturnWindowForPendingJob()) {
      const auto* presentation = captureStopPresentationForSlot(slot);
      RuntimePlaybackViewSource frozen{};
      PlaybackViewSourceIdentity receipt{};
      if (presentation && qualifiedFrozenRecordCaptureSourceForSlot(
              slot, presentation->interval, frozen, receipt))
        return PlaybackViewRequestResult::NotServable;
    }
    // Main requests the playhead window before the rolling owner's identity refresh. A newer root
    // must not replace that owner's positioned debt with a different interval/earlier deadline.
    const auto& frame = runtime->rolling;
    const auto& rollingRequest = runtime->pendingViewRequest_;
    if (rollingRequest.active && rollingRequest.sourceKind == PlaybackViewPendingRequest::SourceKind::CommittedSnapshot &&
        frame.phase == LoopPlaybackRuntime::RollingPhase::Requested &&
        frame.generation == playbackGeneration && rollingRequest.operationGeneration == frame.generation &&
        frame.epoch == clock.epoch && frame.loopLength == loop.loopLengthTicks &&
        rollingRequest.deadlineTick == frame.deadlineTick && rollingRequest.clockEpoch == frame.epoch &&
        rollingRequest.coverage.startPhase == frame.startPhase &&
        rollingRequest.coverage.loopLengthTicks == frame.loopLength)
      return PlaybackViewRequestResult::DeferredSourceQualification;
    const auto holds = [&](const PlaybackViewPendingRequest& req) {
      if (!req.active || req.sourceKind != PlaybackViewPendingRequest::SourceKind::CommittedSnapshot) {
        return false;
      }
      if (req.source.playbackRevision != loop.playbackRevision) return false;
      return phase >= req.coverage.startPhase &&
             phase < req.coverage.startPhase + req.coverage.lengthTicks;
    };
    if (runtime->viewBuild.active()) return PlaybackViewRequestResult::DeferredSourceQualification;
    if (holds(runtime->pendingViewRequest_) || holds(runtime->frozenViewRequest)) {
      return PlaybackViewRequestResult::DeferredSourceQualification;
    }
  }
  return requestCommittedPlaybackWindow(slot, coverage, phase, clock);
#else
  (void)playheadPhase;
  (void)clock;
  return PlaybackViewRequestResult::NotServable;
#endif
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
  if (runtime->frozenViewRequest.sourceKind == PlaybackViewPendingRequest::SourceKind::CommittedLaunch) {
    runtime->frozenViewRequest = {};
    // Task 3.7a (I2): the gather was serving that frozen demand, so it goes with it rather than running on for a
    // demand nobody owns.
    runtime->discardRetainedGather();
  }
  // Completed worker may point at a now-published view. Only discard an active
  // launch worker; publication has already transferred a completed destination.
  if (runtime->launchPreparation == LoopPlaybackRuntime::LaunchPreparation::Requested ||
      runtime->launchPreparation == LoopPlaybackRuntime::LaunchPreparation::Building) {
    const uint32_t dropBefore = probeCoverageBuildDrop(slotIndex);
    runtime->discardCoverageBuild();
    traceCoverageBuildDrop(slotIndex, CoverageBuildDropReason::LaunchCancelled, dropBefore);
  }
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
  if (runtime->launchPreparation == LoopPlaybackRuntime::LaunchPreparation::Bound ||
      runtime->launchPreparation == LoopPlaybackRuntime::LaunchPreparation::FallbackBound) {
    // **A bound launch belongs to the playing slot, not to a pending switch.** `Bound` is only ever written by
    // `bindPlaybackLaunchForSlot`, i.e. by a commit that has already consumed this switch; the preparation's window is
    // now the playing playback's own source (the handoff's view and the seed's). The turn below is the provision
    // owner's "the preparation no longer matches the source, start over" path, and for a Bound preparation that
    // restart is destructive: `cancelPlaybackLaunchForSlot(slotIndex, true)` resets the adopted **current** view's
    // metadata, so the seed then refuses on identity (`sv=0`) and the lane falls back to the whole-loop demand this
    // slice exists to avoid. Device `captures/i5-s37f-device-32bar-01`: the bind at 246.699, this turn at 246.734,
    // `win_seed_refused rq=3` from 246.807 on, and the pre-3.7e storm for the rest of the switch; the same scenario in
    // `captures/i5-s37e-device-32bar-04` did not land in that interleaving and adopted. Nothing here admits, refuses or
    // reorders a switch: the pending switch keeps its state, and the commit's own window is left exactly as bound.
    reportPlaybackLaunchGateRow(slotIndex, "launch_bound_owned", sourceRequestNeeded, sourceRequested);
    return false;
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
  return loop.hasCommittedPasses() && playbackWindowSourceIsCurrentForSlot(slotIndex) && view.complete &&
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
  if (!runtime->readyPositionAllowsAdoption(clockManager.getPlaybackDeadlineClock()) ||
      !set->handoffReadyCoverage(PlaybackViewHandoffMode::Audible)) return false;
  runtime->launchBoundTick = tick;
  runtime->launchPreparation = LoopPlaybackRuntime::LaunchPreparation::Bound;
  runtime->syncRevision(loopForSlot(slotIndex).playbackRevision, playbackGeneration);
  // 2.2: the adopted window's admitted work is discharged by the serve. Leaving it recorded charges the launch
  // as an outstanding sibling against the next interval's admission (the 3.7e launch would cost the demand its
  // own margin). The frame serves the view; the work record is done.
  runtime->viewBuildWork = {};
  return true;
}

// windowed-playback-generation 3.7e -- **the committed switch target's own first window.**
//
// `captures/i5-s37d-device-32bar-10` measured the gap. The switch committed (`snap_gate,t=7,s=4,a=4` at 224.1 s) and
// the target's launch had already built and bound a phase-0 two-bar window -- the commit binds exactly that window and
// starts the playhead at its phase. The rolling frame still began `Idle`, so the lane opened a **second** demand for
// the same interval; the demand deferred to source qualification (`win_requested rq=12`), the qualification it started
// was a whole-loop build, and that build was charged against the rolling span's two-bar handoff
// (`handoff_unreachable` on five of thirteen grains, one DEC-147 re-position) -- twelve spans later
// `win_blocked_by_budget`, for the remaining 99 s. Nothing was wrong with the bound window; the frame simply did not
// know it existed.
//
// This seeds the frame onto that window. The predicate is the resolver's own serving contract (`resolveRollingFrame`,
// `phase == Serving` with the frame positioned at the span's start), read against the slot's live loop, so a frame seeded
// here is one the very next turn would have accepted anyway: same `boundView`, same storage generation, same start phase,
// same span, same revision and snapshot identity, and a coverage that contains the playhead. What it adds is the one
// thing the resolver cannot know -- that the **launch bound it for this slot**. A stale view (an edit after the bind, a
// new generation, a republished root, a moved playhead) fails the identity and the lane keeps its ordinary demand path.
// 3.7f: the bind's tick is also written as the frame's `deadlineTick`, so the seeded span has an **end** and hands the
// lane back its demand instead of stranding it on a coverage it can no longer validate.
TRACK_COLD_MEM Track::LaunchSeedRefusal Track::seedRollingFrameFromBoundLaunch(uint8_t slotIndex, uint32_t generation,
                                                                              const PlaybackDeadlineClock& clock,
                                                                              uint32_t phase) {
#if !PLAYBACK_WINDOW_ROLLING_ENABLED
  (void)slotIndex; (void)generation; (void)clock; (void)phase;
  return LaunchSeedRefusal::NoLaunch;
#else
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || !loopsAllocated() || clock.epoch == 0)
    return LaunchSeedRefusal::NoLaunch;
  auto* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) return LaunchSeedRefusal::NoLaunch;
  // **The witness is the commit's own bind, not the handoff view's preparation.** `bindPlaybackLaunchForSlot` sets
  // both (`launchBoundTick`, `launchPreparation = Bound`), but `boundLaunchView`'s expiry/invalidation is **sticky**: it
  // clears `launchPreparation` while leaving the bound view published and the bind's tick intact
  // (`src/Track/TrackPlaybackRuntime.cpp`, the `phase == elapsed` and `elapsed < lengthTicks` guards). That sticky term
  // answers "may the *handoff* still serve this view"; the question a rolling frame needs is "did this slot's playback
  // start inside the window the commit bound", and that one is the bind's tick. Gating on `Bound` therefore served only
  // the handoff's lifetime, and the lane fell back to the redundant demand this slice removes -- pinned by
  // `test_3_7e_a_launch_whose_handoff_view_expired_still_seeds_the_rolling_frame`, which drives exactly that state.
  if (runtime->launchBoundTick == 0) return LaunchSeedRefusal::NoLaunch;
  // A fallback target has no window to seed from, and a live preparation is another owner's work.
  if (runtime->launchPreparation != LoopPlaybackRuntime::LaunchPreparation::Bound &&
      runtime->launchPreparation != LoopPlaybackRuntime::LaunchPreparation::None)
    return LaunchSeedRefusal::NotBound;
  auto& frame = runtime->rolling;
  if (frame.phase != LoopPlaybackRuntime::RollingPhase::Idle) return LaunchSeedRefusal::NoLaunch;
  const auto* set = runtime->viewSetIfPrewarmed();
  if (set == nullptr || set->lifecycle.current >= set->views.size()) return LaunchSeedRefusal::Identity;
  const auto& view = set->views[set->lifecycle.current];
  const Loop& loop = loopForSlot(slotIndex);
  const uint32_t span = 2 * Config::TICKS_PER_BAR;
  if (!loop.hasCommittedPasses() || !playbackWindowSourceIsCurrentForSlot(slotIndex) ||
      runtime->ownsCoverageSource() || runtime->pendingViewRequest_.active || runtime->frozenViewRequest.active ||
      !view.complete || !view.published || view.storageGeneration == 0 ||
      view.source.origin != PlaybackWindowOrigin::Committed ||
      view.source.sourceGeneration != generation || view.source.playbackRevision != loop.playbackRevision ||
      (loop.publishedSnapshotIsCurrent() && view.source.snapshotIdentity != loop.publishedSnapshotIdentity()) ||
      view.coverage.loopLengthTicks != loop.loopLengthTicks || loop.loopLengthTicks % Config::TICKS_PER_BAR != 0 ||
      view.coverage.startPhase != 0 || view.coverage.lengthTicks != span)
    return LaunchSeedRefusal::Identity;
  // The bind named one traversal of the loop -- the window's own interval, starting at `launchBoundTick`. Past its end
  // the same phases belong to the next traversal, whose content this view does not carry, so the frame must not be
  // positioned there. (`boundLaunchView` states the same bound as `elapsed < coverage.lengthTicks`; this is the seed's
  // read of it, not a second rule.)
  const uint32_t elapsed = clock.tick < runtime->launchBoundTick
                               ? 0u
                               : static_cast<uint32_t>(clock.tick - runtime->launchBoundTick);
  if (elapsed >= view.coverage.lengthTicks) return LaunchSeedRefusal::Expired;
  if (!view.coverage.contains(phase)) return LaunchSeedRefusal::Uncovered;
  // Publish metadata last: until `phase` is written the frame is still the `Idle` one the caller found.
  frame.startPhase = view.coverage.startPhase;
  // **The frame is a positioned span, and the bind's tick is its start.** The resolver reads `deadlineTick == 0` as the
  // *whole-loop* (cold) class and sizes its serving span as `frame.loopLength`; a launch window is two bars, so a
  // zero-deadline frame here is rejected against its own coverage (`coverage.lengthTicks != span`) the moment the launch
  // view it was read through has no interval left. Device `captures/i5-s37e-device-32bar-04`: that rejection is
  // `win_blocked` 3.992 s after adoption, and the lane never asks again. With the span's start in `deadlineTick`, the
  // resolver serves the window for its own interval and ends it as `Expired` + `Idle`, so the lane's next turn opens the
  // successor span -- the same positioned-span life cycle every rolling frame already has.
  frame.deadlineTick = runtime->launchBoundTick;
  frame.refusedHandoffPhase = 0;
  frame.refusedSpanAttempts = 0;
  frame.refusedHandoffValid = false;
  frame.boundView = set->lifecycle.current;
  frame.boundStorageGeneration = view.storageGeneration;
  frame.epoch = clock.epoch;
  frame.generation = generation;
  frame.loopLength = loop.loopLengthTicks;
  frame.projectionStart = getProjectionCycleStartTick();
  frame.lastTick = clock.tick;
  frame.phase = LoopPlaybackRuntime::RollingPhase::Serving;
  return LaunchSeedRefusal::Adopted;
#endif
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
// Only an unpositioned, whole-loop committed request may use the cold path.
TRACK_COLD_MEM bool coldRollingRequest(const LoopPlaybackRuntime& runtime,
    const PlaybackViewPendingRequest& request) {
  const auto& frame = runtime.rolling;
  return request.active && request.sourceKind == PlaybackViewPendingRequest::SourceKind::CommittedSnapshot &&
      frame.phase == LoopPlaybackRuntime::RollingPhase::Requested && frame.epoch == 0 &&
      frame.deadlineTick == 0 && request.clockEpoch == 0 && request.deadlineTick == 0 &&
      request.operationGeneration == frame.generation && frame.startPhase == 0 && frame.loopLength != 0 &&
      request.coverage.startPhase == 0 && request.coverage.lengthTicks == frame.loopLength &&
      request.coverage.loopLengthTicks == frame.loopLength;
}
TRACK_COLD_MEM bool coldRollingShape(const LoopPlaybackRuntime& runtime, const Loop& loop,
    uint32_t generation) {
  const auto& frame = runtime.rolling;
  return (frame.phase == LoopPlaybackRuntime::RollingPhase::Requested ||
          frame.phase == LoopPlaybackRuntime::RollingPhase::Ready) && frame.epoch == 0 &&
      frame.deadlineTick == 0 && frame.generation == generation && frame.startPhase == 0 &&
      frame.loopLength == loop.loopLengthTicks && frame.loopLength == Config::TICKS_PER_BAR;
}

/// C3 (DEC-116) — one reader per sounding slot. **May this slot be served by the cold one-bar full-loop
/// route?** The active slot keeps the answer unconditionally: that is C1's route, already verified. A
/// **non-active** slot is admitted only as its own independent sounding holder -- allocated, `slotEnabled`, a
/// one-bar loop that still holds committed content. It adds no state and no second authority: `TrackManager`
/// already owns enablement, `LoopPlaybackRuntime` already owns the slot's roles and `Loop` already owns its
/// content, so this is a reading of existing facts. A disabled or exited slot, a long loop and a slot without
/// committed content answer false, which is what keeps the existing release policy (`cancelRollingPlaybackWindow`
/// on stop, slot exit, enabled-set replacement and long-loop/demoted targets) intact.
TRACK_COLD_MEM bool slotMayOwnColdFullLoopWindow(uint8_t slot, uint8_t activeLoopIndex, bool loopsAllocated,
                                                 bool slotEnabled, const Loop& loop) {
  if (slot == activeLoopIndex) return true;
  return loopsAllocated && slotEnabled && loop.loopLengthTicks == Config::TICKS_PER_BAR &&
      loop.hasCommittedPasses();
}

/// C3 (DEC-116): **the completed cold one-bar frame of a non-active slot, as the MIDI edge reads it.** This is
/// the shape `coldRollingShape` already names -- unpositioned (`epoch == 0`, `deadlineTick == 0`), a phase-zero
/// one-bar full-loop interval -- in the two phases a *reader* may use, before and after its adoption at the
/// first stamped boundary. (`Serving` keeps `deadlineTick == 0` and `startPhase == 0` through adoption, which
/// is why the view serving after that boundary is still this same frame and not a positioned successor.) It
/// deliberately does not re-check enablement: the only clock-path caller is already gated by the caller's own
/// `engineShouldRun(slotEnabled[...])`, and the frame is slot-owned, so the edge pays no extra lookup and holds
/// no extra authority. A positioned frame of a non-active slot answers false -- that route is not qualified
/// here and stays with the live compatibility emitter.
TRACK_COLD_MEM bool coldLayeredSlotFrameForEmission(const LoopPlaybackRuntime* runtime, const Loop& loop,
                                                    uint8_t slot, uint8_t activeLoopIndex) {
  if (runtime == nullptr || slot == activeLoopIndex) return false;
  if (loop.loopLengthTicks != Config::TICKS_PER_BAR) return false;
  const auto& frame = runtime->rolling;
  return frame.deadlineTick == 0 && frame.startPhase == 0 && frame.loopLength == loop.loopLengthTicks &&
      (frame.phase == LoopPlaybackRuntime::RollingPhase::Ready ||
       frame.phase == LoopPlaybackRuntime::RollingPhase::Serving);
}
TRACK_COLD_MEM bool staleColdRollingReady(const LoopPlaybackRuntime& runtime,
    const Loop& loop, uint32_t generation) {
  const auto& frame = runtime.rolling;
  if (frame.epoch != 0 || frame.deadlineTick != 0 ||
      (frame.phase != LoopPlaybackRuntime::RollingPhase::Ready &&
       frame.phase != LoopPlaybackRuntime::RollingPhase::Blocked) ||
      frame.loopLength != Config::TICKS_PER_BAR) return false;
  const auto* set = runtime.viewSetIfPrewarmed();
  const RuntimePlaybackView* ready = set && set->lifecycle.ready < set->views.size()
      ? &set->views[set->lifecycle.ready] : nullptr;
  return frame.generation != generation || frame.loopLength != loop.loopLengthTicks ||
      !ready || !ready->complete || ready->published ||
      ready->source.origin != PlaybackWindowOrigin::Committed ||
      ready->source.sourceGeneration != generation ||
      ready->source.playbackRevision != loop.playbackRevision ||
      ready->coverage.startPhase != 0 || ready->coverage.lengthTicks != frame.loopLength ||
      ready->coverage.loopLengthTicks != frame.loopLength ||
      (loop.publishedSnapshotIsCurrent() &&
       ready->source.snapshotIdentity != loop.publishedSnapshotIdentity());
}
TRACK_COLD_MEM __attribute__((noinline)) Track::RollingResolution rejectRollingFrame(
    LoopPlaybackRuntime& runtime, Loop& loop, PlaybackTickContext::RouteReject why) {
  auto& frame = runtime.rolling;
  // playback-tick-consume-only phase 2 (report-only): the predicate that blocks this frame, for the ticks after it.
  PlaybackTickContext::noteTurnReject(why);
  frame.blockedBy = static_cast<uint8_t>(why);
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
    const LoopPlaybackRuntime& runtime, bool sourceCurrent, const Loop& loop, uint32_t phase) {
  const auto& frame = runtime.rolling;
  const auto* set = runtime.viewSetIfPrewarmed();
  if (!set || runtime.ownsCoverageSource() || runtime.completedPromisePendingJudgement ||
      runtime.admittedPromiseInvalidated || runtime.countedMissedTraversals != 0 ||
      loop.lastTickInLoop != (phase == 0 ? loop.loopLengthTicks - 1 : phase - 1) ||
      set->lifecycle.ready >= set->views.size()) return false;
  const auto& work = runtime.viewBuildWork;
  const auto& ready = set->views[set->lifecycle.ready];
  return work.clockEpoch == frame.epoch && work.deadlineTick == frame.deadlineTick &&
      runtime.viewBuildOperationGeneration == frame.generation && sourceCurrent &&
      runtime.cachedLoopRevision == loop.playbackRevision && runtime.cachedTrackGeneration == frame.generation &&
      ready.complete && !ready.published && !ready.rawCoveragePrepared &&
      ready.source.origin == PlaybackWindowOrigin::Committed && ready.source.sourceGeneration == frame.generation &&
      ready.source.playbackRevision == loop.playbackRevision && ready.coverage.startPhase == frame.startPhase &&
      frame.startPhase < frame.loopLength && ready.coverage.lengthTicks ==
          std::min(uint32_t(2 * Config::TICKS_PER_BAR), frame.loopLength - frame.startPhase) &&
      ready.coverage.loopLengthTicks == frame.loopLength;
}
}
#endif

TRACK_COLD_MEM void Track::cancelRollingPlaybackWindow(uint8_t slot) {
#if PLAYBACK_WINDOW_ROLLING_ENABLED
  RollingMetadataGuard guard;
  auto* runtime = playbackRuntime.slotIfAllocated(slot);
  if (!runtime) return;
  const auto& live = runtime->pendingViewRequest_;
  if (live.sourceKind == PlaybackViewPendingRequest::SourceKind::LiveCapture &&
      live.liveLeaseGeneration == runtime->workingSetGeneration()) {
    runtime->cancelLiveCaptureBuildBorrows();
    auto* set = runtime->viewSetIfPrewarmed();
    if (set && set->lifecycle.ready < set->views.size()) {
      const auto& ready = set->views[set->lifecycle.ready];
      if (!ready.published && ready.source.origin == PlaybackWindowOrigin::Capture &&
          ready.source.sourceGeneration == live.operationGeneration)
        (void)set->lifecycle.retireSupersededReady(set->views);
    }
    runtime->pendingViewRequest_ = {};
    if (runtime->frozenViewRequest.sourceKind == PlaybackViewPendingRequest::SourceKind::LiveCapture)
      runtime->frozenViewRequest = {};
    runtime->rolling = {};
    return;
  }
  if (runtime->rolling.phase == LoopPlaybackRuntime::RollingPhase::Idle) return;
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
  if (frozenOwned || admittedOwned) {
    // Task 3.15h: a cancelled window releases an admitted builder -- name it and what it had done.
    const uint32_t dropBefore = probeCoverageBuildDrop(slot);
    runtime->discardCoverageBuild();
    traceCoverageBuildDrop(slot, CoverageBuildDropReason::WindowCancelled, dropBefore);
  }
  auto* set = runtime->viewSetIfPrewarmed();
  if (set && loopsAllocated() && set->lifecycle.current == frame.boundView && frame.boundView < set->views.size() &&
      set->views[frame.boundView].storageGeneration == frame.boundStorageGeneration)
    loopForSlot(slot).nextEventIndex = 0;
  // A cold ready candidate is not yet a reader. Return only its own complete
  // successor role; never touch another slot's view or the selected current.
  if (frame.epoch == 0 && frame.deadlineTick == 0 &&
      frame.phase == LoopPlaybackRuntime::RollingPhase::Ready && set &&
      set->lifecycle.ready < set->views.size()) {
    const auto& ready = set->views[set->lifecycle.ready];
    if (ready.complete && !ready.published &&
        ready.source.sourceGeneration == frame.generation &&
        ready.coverage.startPhase == 0 && ready.coverage.lengthTicks == frame.loopLength)
      (void)set->lifecycle.retireSupersededReady(set->views);
  }
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

/// **Task 3.7h -- the densest span-aligned window of this loop, priced before it is read.** The window cursor pins
/// at most the bars the interval intersects (`kMaximumWindowTicks` is two bars), so the three quantities the
/// admission will later *measure* can be bounded from the pinned pages alone: the event references in those bars,
/// the pages they span, and the retirement units the qualification will owe. No event is dereferenced and no page
/// content is read; only each page's `count` is visited, once per span.
struct RollingWindowWorkBound {
  size_t events = 0;
  size_t readUnits = 0;
  size_t releaseUnits = 0;
};

/// Two owner turns of headroom between the request and the admission's lead test: the qualification itself runs
/// before `beginQualifiedCoverageBuild` reads the clock, and a position that paid *exactly* the price would lose
/// those turns off the lead it was promised. Two turns is the declared per-turn advance twice, not a measured fudge.
constexpr uint64_t kRollingPositionSlackTicks = 2 * uint64_t(Track::kPlaybackViewPermittedAdvancePerTurn);

TRACK_COLD_MEM bool boundRollingWindowWork(const LoopSnapshot& snapshot, uint32_t loopLengthTicks,
                                          uint32_t windowTicks, RollingWindowWorkBound& out) {
  if (loopLengthTicks == 0 || windowTicks == 0 || windowTicks > loopLengthTicks ||
      windowTicks % Config::TICKS_PER_BAR != 0) {
    return false;
  }
  const uint32_t windowBars = windowTicks / Config::TICKS_PER_BAR;
  const uint32_t bars = loopLengthTicks / Config::TICKS_PER_BAR;
  if (windowBars == 0 || bars < windowBars) {
    return false;
  }
  RollingWindowWorkBound bound;
  for (uint32_t first = 0; first < bars; ++first) {
    // The terminal odd bar is a non-wrapping interval too. Price its actual pinned bars,
    // including bar-exit and retirement units, without inventing a read across phase zero.
    const uint32_t pinnedBars = std::min(windowBars, bars - first);
    size_t windowEvents = 0;
    size_t windowPages = 0;
    for (uint32_t bar = 0; bar < pinnedBars; ++bar) {
      auto page = snapshot.eventMembership(first + bar);
      while (page) {
        windowEvents += page->count;
        ++windowPages;
        page = page->next;
      }
    }
    // A window the admission cannot hold is not a candidate position: its own request would be refused by name.
    if (windowEvents > RuntimePlaybackViewConfig::kWindowAdmissionLimit) {
      return false;
    }
    // The cursor's read units are one per reference, one per page link and one per bar exit; retirement charges
    // every fixed slot of every pinned page, as `EventWindowCursor::qualifiedReleaseUnitBound` does after a read.
    const size_t readUnits = windowEvents + windowPages + pinnedBars;
    const size_t releaseUnits = windowPages * (kSnapshotMembershipPageCapacity + 1u) + pinnedBars;
    if (windowEvents > bound.events) bound.events = windowEvents;
    if (readUnits > bound.readUnits) bound.readUnits = readUnits;
    if (releaseUnits > bound.releaseUnits) bound.releaseUnits = releaseUnits;
  }
  if (bound.events == 0) {
    return false;
  }
  out = bound;
  return true;
}

TRACK_COLD_MEM Track::RollingOutcome Track::serviceRollingPlaybackWindow(PlaybackDeadlineClock clock) {
#if !PLAYBACK_WINDOW_ROLLING_ENABLED
  (void)clock;
  return RollingOutcome::Disabled;
#else
  RollingMetadataGuard guard;
  // Task 3.15g: refresh a retained Committed demand's identity from the live root on **every** lane turn, before any
  // selection. It must not sit behind the holder choice: a demand whose identity is stale is exactly one the selector
  // reports as unable to progress, so a refresh placed after selection would never run in the state that needs it.
  for (uint8_t refreshSlot = 0; refreshSlot < Config::MAX_LOOPS_PER_TRACK; ++refreshSlot)
    refreshRetainedDemandIdentityForSlot(refreshSlot);
  // C3 (DEC-116): **selection alone does not cancel an enabled sounding slot.** A non-active slot that still
  // holds its own cold full-loop candidate (`coldRollingShape`) of an enabled, committed one-bar loop keeps
  // it; everything else is released exactly as before -- the demoted two-bar launch of a switch target, a
  // long loop, a disabled or exited slot and a positioned successor all still cancel here, which is the
  // protection `test_rolling_stop_or_slot_exit_*` asserts.
  const uint8_t rollingTrackIndex = resolveTrackIndexForPersistence(*this);
  for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
    if (slot == activeLoopIndex) continue;
    const LoopPlaybackRuntime* sibling = playbackRuntime.slotIfAllocated(slot);
    const bool retainedCold = sibling != nullptr &&
        sibling->rolling.generation == playbackGeneration &&
        (coldRollingShape(*sibling, loopForSlot(slot), playbackGeneration) ||
         coldLayeredSlotFrameForEmission(sibling, loopForSlot(slot), slot, activeLoopIndex)) &&
        slotMayOwnColdFullLoopWindow(slot, activeLoopIndex, loopsAllocated(),
                                     trackManager.isSlotEnabled(rollingTrackIndex, slot),
                                     loopForSlot(slot));
    if (retainedCold) {
      continue;
    }
    cancelRollingPlaybackWindow(slot);
  }
  auto* runtime = playbackRuntime.slotIfAllocated(activeLoopIndex);
  if (!runtime || !loopsAllocated()) {
    reportPlaybackRollingGateRow("win_no_runtime", clock.epoch != 0);
    return RollingOutcome::Excluded;
  }
  auto& frame = runtime->rolling;
  auto& loop = getActiveLoop();
  if (isRecording() && liveRecordCaptureSource(loop, *runtime) &&
      !editManager.isNoteEditActive() && !runtime->previewSourceActive &&
      !hasQueuedPlaybackStart() && !hasPendingDeferredOverdubStop() &&
      !isJamPlaybackActive() && !isJamming()) {
    retainLiveRecordDemand(*runtime, loop, playbackGeneration, getProjectionCycleStartTick(), clock);
    const auto& request = runtime->pendingViewRequest_;
    if (request.positionInvalidated || (request.clockEpoch && request.clockEpoch != clock.epoch)) {
      runtime->cancelLiveCaptureBuildBorrows();
      auto* set = runtime->viewSetIfPrewarmed();
      if (set && set->lifecycle.ready < set->views.size()) {
        const auto& ready = set->views[set->lifecycle.ready];
        if (!ready.published && ready.source.origin == PlaybackWindowOrigin::Capture &&
            ready.source.sourceGeneration == request.operationGeneration)
          (void)set->lifecycle.retireSupersededReady(set->views);
      }
      runtime->pendingViewRequest_.positionInvalidated = true;
      runtime->pendingViewRequest_.active = false;
      return RollingOutcome::Rejected;
    }
    return request.active ? RollingOutcome::Waiting : RollingOutcome::Serving;
  }
  // An unpositioned cold initial view is prepared while stopped. The main-loop

  // selector must not cancel it on the next off-clock service opportunity.
  if (!isPlaying() && !isRecording() && !isOverdubbing() && clock.epoch == 0 &&
      !hasQueuedPlaybackStart() && !isJamPlaybackActive() && !isJamming() &&
      !editManager.isNoteEditActive() && !runtime->previewSourceActive &&
      (frame.phase == LoopPlaybackRuntime::RollingPhase::Requested ||
       frame.phase == LoopPlaybackRuntime::RollingPhase::Ready) &&
      coldRollingShape(*runtime, loop, playbackGeneration) &&
      playbackWindowSourceIsCurrentForSlot(activeLoopIndex))
    return frame.phase == LoopPlaybackRuntime::RollingPhase::Ready
        ? RollingOutcome::Ready : RollingOutcome::Waiting;
  // A frozen Record reader already bound to this slot survives the ordinary
  // committed-only requester exclusion, including while a successor overdub owns
  // ingress. Its receipt, copied current and physical view must still agree.
  if (frame.phase == LoopPlaybackRuntime::RollingPhase::Serving &&
      (isPlaying() || isOverdubbing()) && !editManager.isNoteEditActive() && !runtime->previewSourceActive &&
      !hasQueuedPlaybackStart() && !isJamPlaybackActive() && !isJamming() &&
      runtime->hasBoundRecordReturnWindowForPendingJob()) {
    const CaptureStopPresentation* presentation = captureStopPresentationForSlot(activeLoopIndex);
    RuntimePlaybackViewSource frozen{};
    PlaybackViewSourceIdentity receipt{};
    const auto* set = runtime->viewSetIfPrewarmed();
    if (presentation != nullptr && hasRecordReaderOwnerForSlot(activeLoopIndex) &&
        presentation->slot == activeLoopIndex &&
        qualifiedFrozenRecordCaptureSourceForSlot(activeLoopIndex, presentation->interval, frozen, receipt) &&
        set && frame.boundView == set->lifecycle.current && frame.boundView < set->views.size()) {
      const auto& current = set->views[frame.boundView];
      if (frame.epoch == clock.epoch && frame.generation == playbackGeneration &&
          frame.loopLength == loop.loopLengthTicks &&
          frame.loopLength != 0 &&
          (uint32_t(getProjectionCycleStartTick()) - uint32_t(frame.projectionStart)) % frame.loopLength == 0 &&
          current.published && current.complete &&
          current.source.sourceGeneration == receipt.sourceGeneration &&
          ((current.source.origin == PlaybackWindowOrigin::Capture &&
            current.source.captureContentGeneration == receipt.captureContentGeneration &&
            current.source.snapshotIdentity == nullptr) ||
           (current.source.origin == PlaybackWindowOrigin::Committed && loop.publishedSnapshotIsCurrent() &&
            current.source.playbackRevision == loop.playbackRevision &&
            current.source.snapshotIdentity == loop.publishedSnapshotIdentity())) &&
          current.storageGeneration == frame.boundStorageGeneration) {
        const uint32_t phase = IntervalProjection::tickPhaseInProjectionCycle(
            static_cast<uint32_t>(clock.tick), getProjectionCycleStartTick(), loop.loopLengthTicks);
        if (current.coverage.contains(phase)) return RollingOutcome::Serving;
      }
    }
    cancelRollingPlaybackWindow(activeLoopIndex);
  }
  // Task 3.15d (slice 2): **a NOTE_EDIT preview frame is the session lane's frame.** While the session owns the
  // source and the transport is rolling, this requester leaves it alone: the clock edge adopted it at a coverage
  // boundary and the session's cadence keeps its successor prepared. Cancelling here would hand the very next tick
  // to the merged reader and the tick after back to the preview -- cursor churn with a seam every turn. A stop, a
  // deferred overdub-stop, a jam or a queued start still falls through to the cancellation below.
  if (editManager.isNoteEditActive() && runtime->previewSourceActive && isPlaying() &&
      !hasPendingDeferredOverdubStop() && !hasQueuedPlaybackStart() && !isJamPlaybackActive() &&
      !isJamming()) {
    return frame.phase == LoopPlaybackRuntime::RollingPhase::Idle ? RollingOutcome::Waiting
                                                                  : RollingOutcome::Serving;
  }
  if (!isPlaying() || editManager.isNoteEditActive() || hasPendingDeferredOverdubStop() ||
      runtime->previewSourceActive) {
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
      loop.loopLengthTicks % Config::TICKS_PER_BAR != 0) {
    // The demand requires a stamped clock, demoted content and bar-aligned geometry.
    // Odd bar counts use a clamped terminal interval, never a wrapping cursor read.
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
      frame.refusedHandoffValid = false;
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
        request->reason == PlaybackViewPendingReason::WindowExceedsRetainedCapacity ||
        request->reason == PlaybackViewPendingReason::PinnedEditRowsUnresolved)) {
      frame.phase = LoopPlaybackRuntime::RollingPhase::Blocked;
      // Task 3.7d: **the term travels with the exit.** The retry lane can change the epoch, the geometry, the admitted
      // position or the deadline, or find the source stale, and records that only in the demand's own record -- so the
      // row used to leave five candidates and no reader. `rq` on this exit is the **`PlaybackViewPendingReason`
      // ordinal** that stopped the retained demand: the recorded term when the record names a terminal one, otherwise
      // the owner's existing recomputation (an invalidation flag alone carries no term of its own).
      const PlaybackViewPendingReason blockedBy =
          (request->reason == PlaybackViewPendingReason::None ||
           request->reason == PlaybackViewPendingReason::AwaitingSourceQualification)
              ? pendingViewRequestInvalidation(activeLoopIndex, clock)
              : request->reason;
      reportPlaybackRollingGateRow("win_blocked_by_request", clock.epoch != 0, /*reprojectedTick=*/false,
                                   static_cast<uint8_t>(blockedBy));
      return RollingOutcome::Rejected;
    }
    if (frame.phase != LoopPlaybackRuntime::RollingPhase::Ready) {
      reportPlaybackRollingGateRow("win_waiting", clock.epoch != 0);
      return RollingOutcome::Waiting;
    }
    frame.refusedHandoffValid = false;
    reportPlaybackRollingGateRow("win_ready", clock.epoch != 0);
    return RollingOutcome::Ready;
  }
  auto* set = runtime->viewSetIfPrewarmed();
  // Task 3.7c -- **the active slot's first window.** Until this slice the request demanded a window that was already
  // *served* (`sv`: the current view complete and published), and only a switch's launch could produce that -- so a
  // demoted slot that reached playback without a switch (a freshly recorded body, or a restored one) could never be
  // served from a window at all. On device (2026-10-07, `captures/s64-demand-01`) every such slot left here as
  // `win_not_servable` with `sv=0`, including where its root **was** current (`rt=1`) and its coverage role free
  // (`rl=1`), while the lane's own gap blocks showed it dispatched and idle. The spec's first requirement is the
  // opposite -- the demoted case is served by interval publication, and playback must not require whole-loop
  // publication -- so a served window is no longer a precondition of *requesting* one. What still refuses here is
  // having no view set to publish into, or no source to build from (`playbackWindowSourceIsCurrentForSlot`: a current
  // root, or the retained revision the loop carries). The interval requested is the same positioned one the roll
  // requests, judged by the same admission and prepared by the same off-clock owner; the completed build publishes as
  // the `ready` successor, the resolver adopts it at its boundary and the slot is served from then on (the merged
  // fallback covers the traversals in between).
  if (!set || set->lifecycle.current >= set->views.size() || !playbackWindowSourceIsCurrentForSlot(activeLoopIndex)) {
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
  // Task 3.7e -- **the window this slot already owns.** A committed switch target bound its launch's phase-0 window
  // before it became the active slot, and the playhead then started at that window's own phase; requesting the same
  // interval again is what cost run 10 a whole-loop qualification build charged to a two-bar span. The seed serves
  // that window as it stands (identity checked against the live loop by the helper) and opens no demand. It is placed
  // after the repositioning and busy gates because those own other promises, and before the boundary/lookahead work
  // because a frame that can serve now owes none of it.
  const LaunchSeedRefusal seed = seedRollingFrameFromBoundLaunch(activeLoopIndex, playbackGeneration, clock, phase);
  if (seed == LaunchSeedRefusal::Adopted) {
    reportPlaybackRollingGateRow("win_adopted_launch", clock.epoch != 0);
    return RollingOutcome::Serving;
  }
  // A refused launch is not the ordinary "no launch here": that one is silent by design. `launch_seed_not_bound`
  // carries the launch row's own `prep`/`own`/`dem` when the slot still has a *live* preparation this seed cannot read
  // as a window (`Requested`/`Building`/`Ready`/`Fallback`), which is the commit-side gap the row exists to expose.
  if (seed != LaunchSeedRefusal::NoLaunch) {
    reportPlaybackRollingGateRow("win_seed_refused", clock.epoch != 0, /*reprojectedTick=*/false,
                                 static_cast<uint8_t>(seed));
    if (seed == LaunchSeedRefusal::NotBound)
      reportPlaybackLaunchGateRow(activeLoopIndex, "launch_seed_not_bound");
  }
  const auto contract = playbackViewCoverageContract();
  uint64_t lookahead = uint64_t(contract.requiredGrains()) *
      (uint64_t(contract.maximumConsecutiveDeniedTurns) + 1) * contract.maximumPhaseAdvancePerTurn + contract.leadTicks;
  CoverageServiceContract positionCost = contract;
  // **Task 3.7h -- the position pays for the work it asks for.** The declared basis (`3/3/1`) prices the *lane's*
  // promise, but the admission prices the **window's own** derived work (`fills + two sorts + rank pass + the pinned
  // release`), and the two differ by an order of magnitude on a dense span: the device's take measured ~40 grains
  // against a declared 7, so a lead of `7 x 8 x 64 + 768` could never hold it and every attempt left as
  // `handoff_unreachable` -- twelve re-positions, then the traversal budget. The position is therefore taken from the
  // same price the admission will charge, bounded over **every** span of this loop (so whichever span the boundary
  // lands on cannot cost more than the position paid for). DEC-156 keeps this price when the first
  // traversal cannot hold it: the existing bounded occurrence search chooses a later absolute handoff.
  {
    RollingWindowWorkBound bound{};
    const auto snapshot = loop.publishedSnapshot();
    if (snapshot && boundRollingWindowWork(*snapshot, loop.loopLengthTicks, span, bound)) {
      CoverageServiceContract priced = contract;
      priced.deriveWindowGrainsFrom(bound.readUnits, bound.events, bound.releaseUnits,
                                    kPlaybackViewFillQuantum, kPlaybackViewOrderQuantum);
      positionCost = priced;
      positionCost.leadTicks += kRollingPositionSlackTicks;
      const uint64_t pricedLead = uint64_t(priced.requiredGrains()) *
              (uint64_t(priced.maximumConsecutiveDeniedTurns) + 1) * priced.maximumPhaseAdvancePerTurn +
          priced.leadTicks + kRollingPositionSlackTicks;
      if (pricedLead > lookahead && pricedLead + span <= loop.loopLengthTicks) lookahead = pricedLead;
    }
  }
  // Align within the loop, not on an infinite two-bar grid: after an odd terminal
  // bar the next legal boundary is phase zero, then the same loop-local grid again.
  const uint64_t target = uint64_t(phase) + lookahead;
  const uint64_t localBoundary = std::min(uint64_t(loop.loopLengthTicks),
      ((target % loop.loopLengthTicks + span - 1) / span) * span);
  uint64_t boundary = (target / loop.loopLengthTicks) * loop.loopLengthTicks + localBoundary;
  if (frame.refusedHandoffValid) {
    // DEC-147: start at the refused span's handoff, so the retry can never land on the position that was just measured
    // as unreachable. The distance is measured **forward** from the playhead: a numeric comparison is wrong across the
    // loop's wrap -- `boundary % len` can already be numerically past the handoff while still being a whole traversal
    // behind it -- and that is exactly how the retry fell back to the same marginal lead (measured natively: 128
    // re-positions whose chosen start advanced 100 ticks instead of one span each).
    const uint32_t handoffAhead = static_cast<uint32_t>(
        (uint64_t(frame.refusedHandoffPhase) + loop.loopLengthTicks -
         (phase % loop.loopLengthTicks)) % loop.loopLengthTicks);
    if (boundary - phase < handoffAhead)
      boundary = uint64_t(phase) + handoffAhead;
  }
  uint64_t positionLead = 0;
  const bool positionAvailable = boundary > phase && boundary - phase <= UINT32_MAX &&
      earliestEligibleHandoffLead(positionCost, static_cast<uint32_t>(boundary - phase),
          loop.loopLengthTicks, 0, positionLead);
  const bool boundaryOutOfRange = !lookahead || !positionAvailable || clock.tick > UINT64_MAX - positionLead;
  // DEC-147: a whole traversal of refused spans is the terminal answer. The budget is counted in **spans**, because
  // that is the quantity the contract is written in: a tick-based bound is re-derived from the moving playhead and, at
  // the device's measured ~68 ms refusal cadence, would allow ~900 attempts per traversal before it expired (measured
  // on device 2026-10-19: 231 re-positions in 15.7 s, none of them reaching a readiness row). The counter saturates at
  // its byte width so a very long loop cannot wrap the budget back to zero.
  const uint32_t spanBudget = (loop.loopLengthTicks - 1) / span + 1;
  if (frame.refusedHandoffValid) {
    // The guard's **input**, not its decision: the ordinal the retry carries into the next creation, reported once per
    // distinct value (the dedupe is keyed on it). Bounded by the traversal budget, and it is what makes "one attempt
    // per span" and "the budget was spent" separable on the wire.
    reportPlaybackRollingGateRow("win_retry_ordinal", clock.epoch != 0, /*reprojectedTick=*/false,
                                 frame.refusedSpanAttempts);
  }
  const bool traversalSpent = frame.refusedHandoffValid &&
      frame.refusedSpanAttempts >= (spanBudget > 255u ? 255u : spanBudget);
  if (boundaryOutOfRange || traversalSpent) {
    if (frame.refusedHandoffValid) {
      // A whole traversal of refused spans is a terminal answer, not permission to re-request forever. The token is
      // distinct from `win_blocked_by_request` on purpose: this exit is the **budget** answer (and `rq` is its spent
      // ordinal), while that one is the demand's own recorded **term**. One token, one meaning of `rq`.
      frame.phase = LoopPlaybackRuntime::RollingPhase::Blocked;
      reportPlaybackRollingGateRow("win_blocked_by_budget", clock.epoch != 0, /*reprojectedTick=*/false,
                                   frame.refusedSpanAttempts);
      return RollingOutcome::Rejected;
    }
    reportPlaybackRollingGateRow("win_boundary_rejected", clock.epoch != 0);
    return RollingOutcome::Rejected;
  }
  const uint32_t startPhase = static_cast<uint32_t>((phase + positionLead) % loop.loopLengthTicks);
  const PlaybackCoverageInterval coverage{startPhase, std::min(span, loop.loopLengthTicks - startPhase),
                                           loop.loopLengthTicks};
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
  // This requester owns a fresh record (the busy gate excluded prior promises). Transfer its chosen
  // absolute occurrence once; qualification and coalescing must never re-grant a later deadline.
  auto& positioned = runtime->pendingViewRequest_;
  positioned.deadlineTick = clock.tick + positionLead;
  positioned.clockEpoch = clock.epoch;
  positioned.work.deadlineTick = positioned.deadlineTick;
  positioned.work.clockEpoch = clock.epoch;
  positioned.work.positionedLater = positionLead > handoffLeadTicks(phase, coverage.startPhase, loop.loopLengthTicks);
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

FLASHMEM Track::RollingResolution Track::resolveRollingPlaybackWindow(
    uint8_t slot, uint32_t tick, PlaybackDeadlineClock clock) {
#if !PLAYBACK_WINDOW_ROLLING_ENABLED
  (void)slot; (void)tick; (void)clock;
  return {RollingOutcome::Disabled, nullptr};
#else
  RollingMetadataGuard guard;
  using PlaybackTickContext::RouteReject;
  auto* runtime = playbackRuntime.slotIfAllocated(slot);
  if (!runtime) {
    PlaybackTickContext::noteTurnReject(RouteReject::NoRuntime);
    return {RollingOutcome::Excluded, nullptr};
  }
  const bool launchedThisTick = runtime->rolling.phase == LoopPlaybackRuntime::RollingPhase::Serving &&
                                runtime->launchBoundTick == tick;
  if (runtime->rolling.phase == LoopPlaybackRuntime::RollingPhase::Idle) {
    PlaybackTickContext::noteTurnReject(RouteReject::FrameIdle);
    return {RollingOutcome::Excluded, nullptr};
  }
  auto& frame = runtime->rolling;
  auto& loop = loopForSlot(slot);
  auto* set = runtime->viewSetIfPrewarmed();
  // Report-only naming of a blocked frame (phase 2): the reason byte and the cause the blocking reject latched.
  const auto noteBlocked = [&]() {
    PlaybackTickContext::noteTurnReject(RouteReject::FrameBlocked);
    PlaybackTickContext::gState.turnCause = frame.blockedBy;
  };
  if (frame.phase == LoopPlaybackRuntime::RollingPhase::Blocked) {
    noteBlocked();
    return {RollingOutcome::Rejected, nullptr};
  }
  // C3 (DEC-116): the active slot's route is unchanged. A **non-active** slot resolves through this same
  // function only while it holds its own consumer-ready cold full-loop frame (a completed one-bar candidate
  // or its adopted serving view) and is an enabled sounding holder; every other non-active frame -- a
  // demoted two-bar launch, a long loop, a disabled or exited slot, a positioned successor -- keeps today's
  // refusal and stays with the legacy layered emitter.
  const bool layeredCold = slot != activeLoopIndex &&
      coldLayeredSlotFrameForEmission(runtime, loop, slot, activeLoopIndex) &&
      slotMayOwnColdFullLoopWindow(slot, activeLoopIndex, loopsAllocated(),
                                   trackManager.isSlotEnabled(resolveTrackIndexForPersistence(*this), slot),
                                   loop);
  if ((slot != activeLoopIndex && !layeredCold) ||
      (!isPlaying() && !(layeredCold && isAudiblePlaybackActive())) ||
      isJamPlaybackActive() || isJamming() || (hasQueuedPlaybackStart() && !launchedThisTick) ||
      editManager.isNoteEditActive() || hasPendingDeferredOverdubStop())
    // The condition above decided; the naming re-reads its terms in the same order (pure reads, reject path only).
    return rejectRollingFrame(*runtime, loop,
        (slot != activeLoopIndex && !layeredCold) ? RouteReject::GateNotActiveSlot
        : (!isPlaying() && !(layeredCold && isAudiblePlaybackActive())) ? RouteReject::GateNotPlaying
        : (isJamPlaybackActive() || isJamming()) ? RouteReject::GateJam
        : (hasQueuedPlaybackStart() && !launchedThisTick) ? RouteReject::GateQueuedStart
        : editManager.isNoteEditActive() ? RouteReject::GateNoteEdit
                                         : RouteReject::GateDeferredOverdubStop);
  const bool coldReady = frame.phase == LoopPlaybackRuntime::RollingPhase::Ready &&
      frame.epoch == 0 && frame.deadlineTick == 0 && frame.startPhase == 0 &&
      frame.loopLength == Config::TICKS_PER_BAR;
  const bool coldWaiting = frame.phase == LoopPlaybackRuntime::RollingPhase::Requested &&
      frame.epoch == 0 && frame.deadlineTick == 0 && frame.startPhase == 0 &&
      frame.loopLength == Config::TICKS_PER_BAR;
  // C3 (DEC-116): the **slot's own** projection origin. It is the Track anchor for the active slot
  // (unchanged) and the slot's `Loop::startLoopTick` for a non-active one -- the same per-slot origin
  // `Track::resetPlaybackStateForSlot`, `commitQueuedPlaybackStart` and the slot-owned display phase
  // already use, and congruent modulo the loop length by construction. A frame minted under either
  // convention therefore still validates against its own slot's origin, which is the point: the
  // switch's re-anchor of the shared Track anchor must not invalidate an outgoing sounding slot's frame.
  const int32_t slotProjectionOrigin =
      slot == activeLoopIndex ? getProjectionCycleStartTick() : static_cast<int32_t>(loop.startLoopTick);
  if (!clock.epoch || (!(coldReady || coldWaiting) && clock.epoch != frame.epoch) ||
      clock.tick < frame.lastTick || uint32_t(clock.tick) != tick ||
      playbackGeneration != frame.generation || loop.loopLengthTicks != frame.loopLength ||
      !frame.loopLength ||
      (uint32_t(slotProjectionOrigin) - uint32_t(frame.projectionStart)) % frame.loopLength != 0)
    return rejectRollingFrame(*runtime, loop,
        !clock.epoch ? RouteReject::ClockNoEpoch
        : (!(coldReady || coldWaiting) && clock.epoch != frame.epoch) ? RouteReject::ClockEpochChanged
        : (clock.tick < frame.lastTick || uint32_t(clock.tick) != tick) ? RouteReject::ClockTickSkew
        : playbackGeneration != frame.generation ? RouteReject::GenerationChanged
        : (loop.loopLengthTicks != frame.loopLength || !frame.loopLength) ? RouteReject::LoopLengthChanged
                                                                           : RouteReject::ProjectionMoved);
  frame.lastTick = clock.tick;
  if (frame.phase == LoopPlaybackRuntime::RollingPhase::Blocked) {
    noteBlocked();
    return {RollingOutcome::Rejected, nullptr};
  }
  // C3 (DEC-116): the phase is read through the slot's own origin below (`slotProjectionOrigin`); the active
  // slot's value is unchanged.
  const uint32_t phase = IntervalProjection::tickPhaseInProjectionCycle(tick,
      slotProjectionOrigin, loop.loopLengthTicks);
  if (!set) return rejectRollingFrame(*runtime, loop, RouteReject::NoViewSet);
  // Starting transport before cold preparation finishes is a named wait, not
  // a stale epoch. The previous route may contain this traversal; the owner
  // continues off-clock and only a complete successor can join at phase zero.
  if (coldWaiting) {
    PlaybackTickContext::noteTurnReject(RouteReject::ColdWaiting);
    return {RollingOutcome::Waiting, nullptr};
  }
  const uint32_t span = frame.deadlineTick == 0 ? frame.loopLength :
      (frame.startPhase < frame.loopLength
          ? std::min(uint32_t(2 * Config::TICKS_PER_BAR), frame.loopLength - frame.startPhase) : 0u);
  if (frame.phase == LoopPlaybackRuntime::RollingPhase::Serving) {
    if (set->lifecycle.current >= set->views.size() || set->lifecycle.current != frame.boundView)
      return rejectRollingFrame(*runtime, loop, RouteReject::ServingViewNotBound);
    const auto& current = set->views[set->lifecycle.current];
    if (!current.complete || !current.published || current.storageGeneration != frame.boundStorageGeneration ||
        current.source.sourceGeneration != frame.generation || current.source.origin != PlaybackWindowOrigin::Committed ||
        current.coverage.startPhase != frame.startPhase || current.coverage.lengthTicks != span ||
        current.coverage.loopLengthTicks != frame.loopLength ||
        (frame.deadlineTick == 0 && (current.source.playbackRevision != loop.playbackRevision ||
            !playbackWindowSourceIsCurrentForSlot(slot) ||
            (loop.publishedSnapshotIsCurrent() &&
             current.source.snapshotIdentity != loop.publishedSnapshotIdentity()))))
      return rejectRollingFrame(*runtime, loop,
          (!current.complete || !current.published || current.storageGeneration != frame.boundStorageGeneration)
              ? RouteReject::ServingViewNotPublished
          : (current.source.sourceGeneration != frame.generation ||
             current.source.origin != PlaybackWindowOrigin::Committed)
              ? RouteReject::ServingViewSourceStale
          : (current.coverage.startPhase != frame.startPhase || current.coverage.lengthTicks != span ||
             current.coverage.loopLengthTicks != frame.loopLength)
              ? RouteReject::ServingViewGeometry
              : RouteReject::ServingViewSourceStale);
    if (frame.deadlineTick != 0 && clock.tick >= frame.deadlineTick && clock.tick - frame.deadlineTick >= span) {
      frame.phase = LoopPlaybackRuntime::RollingPhase::Idle;
      loop.nextEventIndex = 0; // Fallback's crossing predicate excludes the already-served interval.
      PlaybackTickContext::noteTurnReject(RouteReject::ServingExpired);
      return {RollingOutcome::Expired, nullptr};
    }
    if (!current.coverage.contains(phase)) return rejectRollingFrame(*runtime, loop, RouteReject::ServingUncovered);
    // The copied interval is immutable even if a newer content revision arrives.
    return {RollingOutcome::Serving, &current};
  }
  if (coldReady) {
    // A cold build may finish after playback began. It missed the initial
    // phase-zero boundary; keep the complete successor private until the next
    // eligible wrap rather than blocking its frame on intermediate ticks.
    if (phase != 0) {
      PlaybackTickContext::noteTurnReject(RouteReject::ColdReadyOffBoundary);
      return {RollingOutcome::Ready, nullptr};
    }
    if (!set || set->lifecycle.ready >= set->views.size() ||
        runtime->viewBuildOperationGeneration != frame.generation ||
        runtime->completedPromisePendingJudgement || runtime->admittedPromiseInvalidated ||
        runtime->viewBuildWork.clockEpoch != 0 || runtime->viewBuildWork.deadlineTick != 0 ||
        !playbackWindowSourceIsCurrentForSlot(slot)) return rejectRollingFrame(*runtime, loop, RouteReject::ColdReadyRefused);
    const auto& ready = set->views[set->lifecycle.ready];
    if (!ready.complete || ready.published || ready.rawCoveragePrepared || !ready.storageGeneration ||
        ready.source.origin != PlaybackWindowOrigin::Committed ||
        ready.source.sourceGeneration != frame.generation ||
        ready.source.playbackRevision != loop.playbackRevision ||
        (loop.publishedSnapshotIsCurrent() && ready.source.snapshotIdentity != loop.publishedSnapshotIdentity()) ||
        ready.coverage.startPhase != 0 || ready.coverage.lengthTicks != frame.loopLength ||
        ready.coverage.loopLengthTicks != frame.loopLength ||
        !adoptPreparedPlaybackWindow(slot)) return rejectRollingFrame(*runtime, loop, RouteReject::ColdReadyRefused);
    loop.nextEventIndex = 0;
    frame.boundView = set->lifecycle.current;
    frame.boundStorageGeneration = set->views[frame.boundView].storageGeneration;
    frame.epoch = clock.epoch;
    frame.lastTick = clock.tick;
    frame.phase = LoopPlaybackRuntime::RollingPhase::Serving;
    return {RollingOutcome::Serving, &set->views[frame.boundView]};
  }
  if (clock.tick < frame.deadlineTick) {
    PlaybackTickContext::noteTurnReject(RouteReject::WaitingForHandoff);
    return {RollingOutcome::Waiting, nullptr};
  }
  if (clock.tick != frame.deadlineTick || frame.phase != LoopPlaybackRuntime::RollingPhase::Ready ||
      phase != frame.startPhase ||
      !rollingCandidateReadyAtBoundary(*runtime, playbackWindowSourceIsCurrentForSlot(slot), loop, phase))
    return rejectRollingFrame(*runtime, loop,
        clock.tick != frame.deadlineTick ? RouteReject::HandoffMissed
        : frame.phase != LoopPlaybackRuntime::RollingPhase::Ready ? RouteReject::CandidateNotReady
        : phase != frame.startPhase ? RouteReject::HandoffPhaseMismatch
                                    : RouteReject::CandidateRejected);
  if (!adoptPreparedPlaybackWindow(slot)) return rejectRollingFrame(*runtime, loop, RouteReject::AdoptRefused);
  loop.nextEventIndex = 0; // First event in the new half-open interval, no inline search/build.
  frame.boundView = set->lifecycle.current;
  frame.boundStorageGeneration = set->views[frame.boundView].storageGeneration;
  frame.phase = LoopPlaybackRuntime::RollingPhase::Serving;
  // Task 4.5's **eligible boundary, and its only recovery**: the replacement prepared for this traversal was adopted
  // here, so this traversal *may serve the current revision* -- the episode ends and a latched deadline-contract fault
  // clears, because the serving path really recovered rather than because a later edit forgave it.
  //
  // Two neighbours this site briefly had were withdrawn by independent review (2026-10-06), recorded in
  // `evidence/2026-10-06-s45e-lane-containment-recovery/`: judging the lag at the **expiry** attributes the miss to the
  // interval that was served in full (the promise route counts that miss with the demand's own identity), and applying
  // the **last resort** from this resolver's rejections would silence a slot the retained merged fallback is about to
  // serve in the same turn -- that needs an owner-level "no serving route at all" predicate first.
  (void)advancePlaybackLagTraversal(slot, frame.startPhase, span, loop.playbackRevision,
                                    /*replacementReady=*/true);
  return {RollingOutcome::Serving, &set->views[set->lifecycle.current]};
#endif
}

TRACK_COLD_MEM bool Track::rootQualificationWaitsForCaptureReady(
    uint8_t slotIndex, const LoopPlaybackRuntime& runtime, PlaybackDeadlineClock clock) const {
#if !PLAYBACK_WINDOW_ROLLING_ENABLED
  (void)slotIndex; (void)runtime; (void)clock;
  return false;
#else
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || !loopsAllocated() ||
      runtime.coveragePreparation != LoopPlaybackRuntime::CoveragePreparation::AwaitAdmission ||
      !runtime.ownsCoverageSource() || !runtime.viewBuild.active()) return false;
  const auto& request = runtime.frozenViewRequest;
  const Loop& loop = loopForSlot(slotIndex);
  if (!request.active || request.sourceKind != PlaybackViewPendingRequest::SourceKind::CommittedSnapshot ||
      request.positionInvalidated || request.operationGeneration != playbackGeneration ||
      clock.epoch == 0 || request.clockEpoch != clock.epoch ||
      clock.tick < request.work.admittedTick || clock.tick >= request.deadlineTick ||
      !loop.publishedSnapshotIsCurrent() || request.source.origin != PlaybackWindowOrigin::Committed ||
      request.source.playbackRevision != loop.playbackRevision ||
      request.source.sourceGeneration != playbackGeneration ||
      request.source.snapshotIdentity != loop.publishedSnapshotIdentity() ||
      request.coverage.loopLengthTicks != loop.loopLengthTicks) return false;
  const CaptureStopPresentation* presentation = captureStopPresentationForSlot(slotIndex);
  RuntimePlaybackViewSource frozen{};
  PlaybackViewSourceIdentity receipt{};
  if (presentation == nullptr || !presentation->retirementActive ||
      presentation->slot != slotIndex ||
      !qualifiedFrozenRecordCaptureSourceForSlot(slotIndex, presentation->interval, frozen, receipt)) return false;
  const auto* set = runtime.viewSetIfPrewarmed();
  if (!set || set->lifecycle.ready >= set->views.size() ||
      set->lifecycle.build >= set->views.size() ||
      set->lifecycle.ready == set->lifecycle.build) return false;
  const auto& ready = set->views[set->lifecycle.ready];
  const auto& build = set->views[set->lifecycle.build];
  return ready.complete && !ready.published &&
      ready.source.origin == PlaybackWindowOrigin::Capture &&
      ready.source.sourceGeneration == playbackGeneration &&
      ready.source.captureContentGeneration == receipt.captureContentGeneration &&
      ready.coverage.loopLengthTicks == loop.loopLengthTicks &&
      !build.complete && !build.published && build.source == request.source &&
      build.coverage.startPhase == request.coverage.startPhase &&
      build.coverage.lengthTicks == request.coverage.lengthTicks &&
      build.coverage.loopLengthTicks == request.coverage.loopLengthTicks;
#endif
}

/// Task 3.15g: **the retained demand's identity is refreshed from the live root.** The board's stall reads
/// `revEq = 0` (`captures/i5-rolling-base-2bar-13`): the demand the current-root handoff keeps is judged against a
/// `source.playbackRevision` captured before the root published, so the retry gate's final equality refuses a demand
/// that is, by construction, for the root that is now current. Nothing about the demand's *reason*, *position* or
/// *coverage* is touched here -- only the identity fields the gate compares, and only toward the live root.
TRACK_COLD_MEM void Track::refreshRetainedDemandIdentityForSlot(uint8_t slotIndex) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || !loopsAllocated()) return;
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) return;
  // Task 3.15g — **ownership trace, transition-cadenced**. The three records that can own the coverage work are the
  // retained demand (`pendingViewRequest_`), the frozen obligation (`frozenViewRequest`) and the build (`viewBuild`).
  // The stall is a race between the code that hands work between them, so this emits only when that ownership set
  // changes -- one row per transfer, never a per-turn sample. `mask` is the set (bit0 pending, bit1 frozen, bit2 build,
  // bit3 the Record job still pending) and `why` marks the **illegal** combination the owner named: the job is still
  // pending while none of the three records owns the work.
  {
    DMAMEM static uint8_t sLastOwnerSet[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK];
    DMAMEM static bool sOwnerSetInitialised = false;
    const uint8_t traceTrack = resolveTrackIndexForPersistence(*this);
    if (traceTrack < Config::NUM_TRACKS) {
      if (!sOwnerSetInitialised) {
        for (uint8_t t = 0; t < Config::NUM_TRACKS; ++t)
          for (uint8_t s2 = 0; s2 < Config::MAX_LOOPS_PER_TRACK; ++s2) sLastOwnerSet[t][s2] = 0xFFu;
        sOwnerSetInitialised = true;
      }
      const CaptureStopPresentation* presentation = captureStopPresentationForSlot(slotIndex);
      const bool jobPending = presentation != nullptr && presentation->retirementActive &&
          presentation->slot == slotIndex;
      uint8_t set = 0;
      if (runtime->pendingViewRequest_.active) set |= 1u;
      if (runtime->frozenViewRequest.active) set |= 2u;
      if (runtime->viewBuild.active()) set |= 4u;
      if (jobPending) set |= 8u;
      if (sLastOwnerSet[traceTrack][slotIndex] != set) {
        const uint8_t previousOwnerSet = sLastOwnerSet[traceTrack][slotIndex];
        sLastOwnerSet[traceTrack][slotIndex] = set;
        const bool ownerless = jobPending && (set & 0x7u) == 0;
        SC_FROZEN_WINDOW_READINESS(traceTrack, slotIndex, "owner_trace", "transition", loopForSlot(slotIndex).playbackRevision,
                                   0u, playbackGeneration, 0u, set, 0u,
                                   loopForSlot(slotIndex).loopLengthTicks, set, ownerless ? 0x80u : 0u, 0u);
        // Task 3.15h: the build bit can clear **without any release site running**. `RuntimePlaybackViewBuild::active()`
        // is false for `Phase::Complete`, so a builder that finished its last grain and was neither published nor
        // discarded leaves the owner set with the bit gone and the obligation still frozen -- the exact
        // `pending+frozen` state the board alternates into. Name that transition here, through the same probe the
        // release sites use, so the row says what the builder had done (qualified/ordered counts, phase) instead of
        // leaving it to be inferred from the absent release.
        if ((previousOwnerSet & 4u) != 0 && (set & 4u) == 0) {
          const uint32_t dropped = probeCoverageBuildDrop(slotIndex);
          const uint8_t phase = static_cast<uint8_t>((dropped >> 1) & 0x7u);
          traceCoverageBuildDrop(slotIndex,
              phase == static_cast<uint8_t>(RuntimePlaybackViewBuildGrain::Complete)
                  ? CoverageBuildDropReason::CompletedUnpublished
                  : CoverageBuildDropReason::ReleasedWithoutSite,
              dropped);
        }
      }
    }
  }
  const Loop& loop = loopForSlot(slotIndex);
  if (!loop.publishedSnapshotIsCurrent() || loop.loopLengthTicks == 0) return;
  auto& demand = runtime->pendingViewRequest_;
  if (!demand.active || demand.sourceKind != PlaybackViewPendingRequest::SourceKind::CommittedSnapshot ||
      demand.source.origin != PlaybackWindowOrigin::Committed) {
    return;
  }
  const auto snapshot = loop.publishedSnapshot();
  if (!snapshot) return;
  const bool stale = demand.source.playbackRevision != loop.playbackRevision ||
      demand.source.sourceGeneration != playbackGeneration ||
      demand.source.snapshotIdentity != loop.publishedSnapshotIdentity() ||
      demand.loopLengthTicks != loop.loopLengthTicks;
  if (!stale) return;
  demand.source.playbackRevision = loop.playbackRevision;
  demand.source.sourceGeneration = playbackGeneration;
  demand.source.snapshotIdentity = loop.publishedSnapshotIdentity();
  demand.loopLengthTicks = loop.loopLengthTicks;
  reportPlaybackViewMaintenanceRow(slotIndex, loopForSlot(slotIndex), "retained_identity_refreshed");
}

namespace {
/// The reason's own name, so a row states *which site* released the builder rather than only that it went away.
TRACK_COLD_MEM const char* coverageBuildDropReasonName(Track::CoverageBuildDropReason reason) {
  switch (reason) {
    case Track::CoverageBuildDropReason::CandidateReleased: return "candidate_released";
    case Track::CoverageBuildDropReason::LaunchCancelled: return "launch_cancelled";
    case Track::CoverageBuildDropReason::WindowCancelled: return "window_cancelled";
    case Track::CoverageBuildDropReason::DemandInvalidated: return "demand_invalidated";
    case Track::CoverageBuildDropReason::HandoffUnreachable: return "handoff_unreachable";
    case Track::CoverageBuildDropReason::SourceSuperseded: return "source_superseded";
    case Track::CoverageBuildDropReason::RecordReturnSourceSuperseded: return "record_return_superseded";
    case Track::CoverageBuildDropReason::GrainDeniedWhileOwned: return "grain_denied_while_owned";
    case Track::CoverageBuildDropReason::OverdubLifecycle: return "overdub_lifecycle";
    case Track::CoverageBuildDropReason::CompletionAdjudicated: return "completion_adjudicated";
    case Track::CoverageBuildDropReason::CompletedUnpublished: return "completed_unpublished";
    case Track::CoverageBuildDropReason::ReleasedWithoutSite: return "released_without_site";
    case Track::CoverageBuildDropReason::QualificationStarted: return "qualification_started";
    case Track::CoverageBuildDropReason::BuildAdmitted: return "build_admitted";
    case Track::CoverageBuildDropReason::PublishedAsReady: return "published_as_ready";
    case Track::CoverageBuildDropReason::PublishedAsCurrent: return "published_as_current";
    case Track::CoverageBuildDropReason::PublishReadyRefused: return "publish_ready_refused";
    case Track::CoverageBuildDropReason::PublishCurrentRefused: return "publish_current_refused";
    case Track::CoverageBuildDropReason::None: break;
  }
  return "none";
}
}  // namespace

TRACK_COLD_MEM uint32_t Track::probeCoverageBuildDrop(uint8_t slotIndex) const {
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) return 0;
  uint32_t packed = 0;
  if (runtime->viewBuild.active()) packed |= 1u;
  packed |= (uint32_t(static_cast<uint8_t>(runtime->viewBuild.nextGrain())) & 0x7u) << 1;
  if (runtime->viewBuild.coverageOwned()) packed |= 1u << 4;
  const RuntimePlaybackViewSet* set = runtime->viewSetIfPrewarmed();
  if (set != nullptr && set->lifecycle.build < set->views.size() &&
      runtime->viewBuild.completedDestinationIs(set->views[set->lifecycle.build])) packed |= 1u << 5;
  if (runtime->viewBuild.ownsLiveCaptureBorrow()) packed |= 1u << 6;
  packed |= (std::min<uint32_t>(runtime->viewBuild.qualifiedEventCount(), 255u) & 0xFFu) << 8;
  packed |= (std::min<uint32_t>(runtime->viewBuild.privateOrderedEventCount(), 255u) & 0xFFu) << 16;
  return packed;
}

/// Task 3.15h — **the release site's own account of itself.** The owner-set trace is turn-cadenced and therefore blind
/// to anything that opens and closes inside one turn, which is exactly the shape the board's stall takes: the builder is
/// admitted and released again before the next turn's row is written. This row is written *at* the release, carrying
/// four things the turn-cadenced row cannot: the site's reason, what the builder had done (`start`: phase, qualified and
/// ordered counts), who owns the work afterwards (`length`: the same owner bit set the transition row uses), and whether
/// the release published or prepared a successor (`adoptResult` bit 2/3).
///
/// **The falsifiable term is bit 7.** A release that had done work, published nothing, prepared no successor, and handed
/// the same demand to `pending`/`frozen` is the defect: the same unsatisfied work returned to its owner with no progress.
/// A release with bit 7 clear is either a real handoff (bits 2-5) or an empty builder (bit 0 clear), and neither is a
/// defect. The dedupe key includes the before-state and the owner set, so a repeating no-progress cycle reports its
/// first instance and every change of shape, not one line per release.
TRACK_COLD_MEM void Track::traceCoverageBuildDrop(uint8_t slotIndex, CoverageBuildDropReason reason, uint32_t before) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || !loopsAllocated()) return;
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  const uint8_t traceTrack = resolveTrackIndexForPersistence(*this);
  if (runtime == nullptr || traceTrack >= Config::NUM_TRACKS) return;
  const Loop& loop = loopForSlot(slotIndex);
  const CaptureStopPresentation* presentation = captureStopPresentationForSlot(slotIndex);
  const bool jobPending = presentation != nullptr && presentation->retirementActive &&
      presentation->slot == slotIndex;
  uint8_t owners = 0;
  if (runtime->pendingViewRequest_.active) owners |= 1u;
  if (runtime->frozenViewRequest.active) owners |= 2u;
  if (runtime->viewBuild.active()) owners |= 4u;
  if (jobPending) owners |= 8u;
  const RuntimePlaybackViewSet* set = runtime->viewSetIfPrewarmed();
  const bool windowServing = set != nullptr && set->lifecycle.current < set->views.size() &&
      set->views[set->lifecycle.current].complete && set->views[set->lifecycle.current].published;
  const bool successorPrepared = set != nullptr && set->lifecycle.ready < set->views.size() &&
      set->views[set->lifecycle.ready].complete && !set->views[set->lifecycle.ready].published;
  const bool beforeActive = (before & 1u) != 0;
  const bool progressed = (((before >> 8) & 0xFFu) + ((before >> 16) & 0xFFu)) != 0;
  uint8_t flags = 0;
  if (beforeActive) flags |= 1u;
  if (progressed) flags |= 1u << 1;
  if (windowServing) flags |= 1u << 2;
  if (successorPrepared) flags |= 1u << 3;
  if (runtime->frozenViewRequest.active) flags |= 1u << 4;
  if (runtime->pendingViewRequest_.active) flags |= 1u << 5;
  if (jobPending) flags |= 1u << 6;
  // The defect shape: work was in hand, nothing published, no successor prepared, and the demand went back to an owner.
  if (beforeActive && !windowServing && !successorPrepared && (owners & 0x3u) != 0) flags |= 1u << 7;
  // Demand stamp: which record owns the demand and under which reason/position the release happened.
  uint32_t stamp = 0;
  if (runtime->pendingViewRequest_.active) stamp |= 1u;
  if (runtime->frozenViewRequest.active) stamp |= 2u;
  stamp |= (uint32_t(static_cast<uint8_t>(runtime->pendingViewRequest_.reason)) & 0x3Fu) << 2;
  if (runtime->pendingViewRequest_.sourceKind == PlaybackViewPendingRequest::SourceKind::CommittedSnapshot) stamp |= 1u << 8;
  if (runtime->pendingViewRequest_.sourceKind == PlaybackViewPendingRequest::SourceKind::LiveCapture) stamp |= 1u << 9;
  if (loop.publishedSnapshotIsCurrent()) stamp |= 1u << 10;
  if (runtime->coverageSource.retiring()) stamp |= 1u << 11;
  if (runtime->retainedGatherActive()) stamp |= 1u << 12;
  if (runtime->pagedActive) stamp |= 1u << 13;
  if (runtime->viewBuild.coverageOwned()) stamp |= 1u << 14;
  if (runtime->pendingViewRequest_.clockEpoch == 0) stamp |= 1u << 15;
  if (runtime->pendingViewRequest_.deadlineTick == 0) stamp |= 1u << 16;
  {
    DMAMEM static uint32_t sLastKey[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK];
    DMAMEM static uint8_t sLastSeen[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK];
    // The key is the *shape* of the release: reason, before-state, who owns it afterwards, and the decided flags.
    const uint32_t key = (uint32_t(static_cast<uint8_t>(reason)) << 24) ^ (before & 0x00FFFFFFu) ^
        (uint32_t(owners) << 16) ^ (uint32_t(flags) << 8) ^ (uint32_t(stamp) & 0xFFu);
    if (sLastSeen[traceTrack][slotIndex] != 0 && sLastKey[traceTrack][slotIndex] == key) return;
    sLastKey[traceTrack][slotIndex] = key;
    sLastSeen[traceTrack][slotIndex] = 1;
  }
  SC_FROZEN_WINDOW_READINESS(traceTrack, slotIndex, "build_drop", coverageBuildDropReasonName(reason),
                             loop.playbackRevision, stamp, playbackGeneration,
                             static_cast<uint64_t>(reinterpret_cast<uintptr_t>(loop.publishedSnapshotIdentity())),
                             before, owners, loop.loopLengthTicks,
                             static_cast<uint8_t>(reason), flags, 0u);
}

TRACK_COLD_MEM bool Track::committedPlaybackRetryCanProgress(uint8_t slotIndex,
                                                             PlaybackDeadlineClock clock) const {
  // DEC-193: one implementation of the gate's order -- the yes/no answer and the reported refusal cannot disagree.
  const PlaybackBuildRefusal refusal = playbackBuildGateRefusal(slotIndex, clock);
  return refusal == PlaybackBuildRefusal::None || refusal == PlaybackBuildRefusal::SourceRetiring;
}

/// **DEC-193 — the gate's first failing term, named.** Same order as the predicate has always evaluated; the early
/// `None` answers are the gate being open (including the two "this turn has work" states), and every other value is the
/// term that stopped an owed build from advancing. Report-only: nothing reads it to decide.
TRACK_COLD_MEM PlaybackBuildRefusal Track::playbackBuildGateRefusal(uint8_t slotIndex,
                                                                   PlaybackDeadlineClock clock) const {
  const auto* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (!runtime) return PlaybackBuildRefusal::NoRuntime;
  if (runtime->coverageSource.retiring()) return PlaybackBuildRefusal::SourceRetiring;
  if (rootQualificationWaitsForCaptureReady(slotIndex, *runtime, clock)) return PlaybackBuildRefusal::CaptureReadyWait;
  // A frozen reservation always gets a turn, including cancellation/invalidity.
  if (runtime->ownsCoverageSource()) return PlaybackBuildRefusal::None;
  // Task 3.7a (I2): a retained-source gather in flight is this lane's own work; the frozen demand carries the promise.
  if (runtime->retainedGatherActive())
    return runtime->frozenViewRequest.active ? PlaybackBuildRefusal::None : PlaybackBuildRefusal::NoDemand;
  // Task 3.7a (I3b): a paged preparation in flight is this lane's own work too.
  if (runtime->pagedActive && runtime->frozenViewRequest.active) return PlaybackBuildRefusal::None;
  const auto* request = pendingViewRequest(slotIndex);
  if (request == nullptr) return PlaybackBuildRefusal::NoDemand;
  const bool launch = request->sourceKind == PlaybackViewPendingRequest::SourceKind::CommittedLaunch;
#if PLAYBACK_WINDOW_ROLLING_ENABLED
  const bool cold = coldRollingRequest(*runtime, *request);
#else
  const bool cold = false;
#endif
  // DEC-136: the bound frozen reader's privately prepared successor is owed **without a deadline**.
  const bool privateRootOwed = rootReturnSuccessorIsOwedForSlot(slotIndex, *request, clock);
  if (!launch && request->sourceKind != PlaybackViewPendingRequest::SourceKind::CommittedSnapshot)
    return PlaybackBuildRefusal::NoDemand;
  if (request->positionInvalidated) return PlaybackBuildRefusal::DemandInvalidated;
  if (!launch && !cold && !privateRootOwed) {
    if (request->clockEpoch == 0) return PlaybackBuildRefusal::Unqualified;
    if (clock.epoch != request->clockEpoch) return PlaybackBuildRefusal::EpochMismatch;
    if (clock.tick < request->work.admittedTick) return PlaybackBuildRefusal::AdmittedTickAhead;
    if (clock.tick >= request->deadlineTick) return PlaybackBuildRefusal::DeadlineExpired;
  }
  if (request->reason == PlaybackViewPendingReason::WindowExceedsRetainedCapacity)
    return PlaybackBuildRefusal::WindowExceedsRetainedCapacity;
  // Task 3.15g: these two are the paged lane's own answer, terminal only while that lane still says so.
  if (request->reason == PlaybackViewPendingReason::ClosuresExceedDeclaredCapacity &&
      runtime->pagedOutcome == LoopPlaybackRuntime::PagedPlaybackOutcome::ClosuresExceedDeclaredCapacity)
    return PlaybackBuildRefusal::ClosuresExceedDeclaredCapacity;
  if (request->reason == PlaybackViewPendingReason::PinnedEditRowsUnresolved &&
      runtime->pagedOutcome == LoopPlaybackRuntime::PagedPlaybackOutcome::PinnedEditRowsUnresolved)
    return PlaybackBuildRefusal::PinnedEditRowsUnresolved;
  if (!privateRootOwed && request->reason == PlaybackViewPendingReason::CannotCompleteBeforeHandoff)
    return PlaybackBuildRefusal::CannotCompleteBeforeHandoff;
  if (!runtime->coverageRoleAvailable(privateRootOwed)) return PlaybackBuildRefusal::NoCoverageRole;
  if (playbackViewOutstandingGrains(slotIndex) != 0) return PlaybackBuildRefusal::SiblingOutstanding;
  const auto& loop = loopForSlot(slotIndex);
  if (request->loopLengthTicks != loop.loopLengthTicks) return PlaybackBuildRefusal::GeometryChanged;
  if (!playbackWindowSourceIsCurrentForSlot(slotIndex)) return PlaybackBuildRefusal::SourceNotCurrent;
  // Task 3.15g: a Committed window is content of the published root; when that root is current the demand is for it.
  if (request->sourceKind == PlaybackViewPendingRequest::SourceKind::CommittedSnapshot &&
      loop.publishedSnapshotIsCurrent()) return PlaybackBuildRefusal::None;
  if (request->source.playbackRevision != loop.playbackRevision) return PlaybackBuildRefusal::RevisionMismatch;
  return PlaybackBuildRefusal::None;
}

/// DEC-193: the last refusal reported per (track, slot). The row is a **transition** record, so a slot parked on the
/// same term costs one row while every move is still named -- a row per turn would evict the evidence it explains.
/// RAM2 on the target (off the clock path), plain state on the host.
#if defined(__IMXRT1062__)
DMAMEM uint8_t gPlaybackBuildRefusalLast[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK];
DMAMEM uint8_t gPlaybackDispatchLast[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK];
#else
uint8_t gPlaybackBuildRefusalLast[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK];
uint8_t gPlaybackDispatchLast[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK];
#endif

TRACK_COLD_MEM Track::PlaybackViewTurnResult Track::stepCommittedPlaybackRetry(
    uint8_t slotIndex, PlaybackDeadlineClock clock) {
  auto* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  // DEC-193 (report-only): name the first failing term once per change, **and** prove reach when the gate is open.
  {
    const PlaybackBuildRefusal refusal = playbackBuildGateRefusal(slotIndex, clock);
    const bool blocked = !runtime || (refusal != PlaybackBuildRefusal::None &&
                                      refusal != PlaybackBuildRefusal::SourceRetiring);
    if (runtime != nullptr && slotIndex < Config::MAX_LOOPS_PER_TRACK) {
      const uint8_t trackIndex = playbackRebuildTrackIndex(*this);
      // `gate_open` is the reach proof: the retry was called and its gate let the turn through. Its absence (no row)
      // means the step returned before the retry -- the scheduler-side question this probe exists to separate.
      const uint8_t token = blocked ? static_cast<uint8_t>(refusal) : 0xFFu;
      if (trackIndex < Config::NUM_TRACKS &&
          gPlaybackBuildRefusalLast[trackIndex][slotIndex] != token) {
        gPlaybackBuildRefusalLast[trackIndex][slotIndex] = token;
        reportPlaybackViewMaintenanceRow(slotIndex, loopForSlot(slotIndex),
                                         blocked ? playbackBuildRefusalName(refusal) : "gate_open");
      }
    }
    if (blocked) return PlaybackViewTurnResult::Idle;
  }
  if (runtime->coverageSource.retiring()) {
    runtime->stepCoverageSourceRetirement(kPlaybackViewFillQuantum);
    return PlaybackViewTurnResult::GrainServed;
  }
  if (!runtime->ownsCoverageSource()) {
    // Task 3.7a (I2): the **retained-source gather**. Its own next slice first, then its beginning -- and only for a
    // slot whose published root cannot answer, so the root-backed path below is unchanged for every slot that has one.
    if (runtime->retainedGatherActive()) {
      return stepRetainedSourceGatherForSlot(slotIndex, clock);
    }
    // Task 3.7a (I3b): the **paged preparation** continues on its own turn (one page per turn) before any new demand is
    // begun for this slot: an in-flight preparation owns the ring, and its pages are what its consumer waits for.
    if (runtime->pagedActive && runtime->frozenViewRequest.active) {
      return stepPagedPlaybackForSlot(slotIndex, clock);
    }
    if (runtime->pendingViewRequest_.active && !loopForSlot(slotIndex).publishedSnapshotIsCurrent() &&
        playbackWindowSourceIsCurrentForSlot(slotIndex)) {
      // One turn is one bounded grain: the gather's beginning and its **first page** are served together, so the
      // slice accounting always describes work this turn actually did.
      if (!beginRetainedSourceGatherForSlot(slotIndex)) return PlaybackViewTurnResult::GrainDenied;
      // Task 3.7a (I3b): an interval whose pages are **already prepared** for this revision is served by that delivery.
      // Re-gathering it page by page would rediscover -- at the cost of the whole interval -- a fact the ring already
      // holds, and re-preparing would drop whatever its consumer has not walked yet. The gather is released first, so
      // the promise belongs to the ring's lane and to no other.
      if (runtime->frozenViewRequest.sourceKind == PlaybackViewPendingRequest::SourceKind::CommittedSnapshot &&
          runtime->pagedActiveForInterval(runtime->frozenViewRequest.source.playbackRevision,
                                          runtime->frozenViewRequest.coverage)) {
        runtime->discardRetainedGather();
        return stepPagedPlaybackForSlot(slotIndex, clock);
      }
      return stepRetainedSourceGatherForSlot(slotIndex, clock);
    }
    // A temporary root reference cannot retire the root here: the Loop still owns
    // this current root throughout this non-interleaved main-loop turn.
    const auto snapshot = loopForSlot(slotIndex).publishedSnapshot();
    runtime->pendingViewRequest_.source.snapshotIdentity = snapshot.get();
    const uint32_t makeBefore = probeCoverageBuildDrop(slotIndex);
    const bool qualified = snapshot && runtime->beginCoverageQualification(*snapshot);
    traceCoverageBuildDrop(slotIndex, CoverageBuildDropReason::QualificationStarted, makeBefore);
    return qualified ? PlaybackViewTurnResult::GrainServed : PlaybackViewTurnResult::GrainDenied;
  }
  auto& request = runtime->frozenViewRequest;
  const auto& loop = loopForSlot(slotIndex);
  const bool launch = request.sourceKind == PlaybackViewPendingRequest::SourceKind::CommittedLaunch;
#if PLAYBACK_WINDOW_ROLLING_ENABLED
  const bool cold = coldRollingRequest(*runtime, request);
#else
  const bool cold = false;
#endif
  // DEC-136 item 2: the demand is **owed without a deadline** — the frozen reader keeps serving until the
  // successor is complete and adopted — so the position terms cannot expire it. The structural classification
  // is re-evaluated every turn and is the only thing that keeps this true.
  const bool privateRootOwed = request.active &&
      rootReturnSuccessorIsOwedForSlot(slotIndex, request, clock);
  if (!request.active || request.operationGeneration != playbackGeneration || request.positionInvalidated ||
      (!launch && !cold && !privateRootOwed &&
       (clock.epoch == 0 || request.clockEpoch != clock.epoch ||
        clock.tick < request.work.admittedTick || clock.tick >= request.deadlineTick)) ||
      request.loopLengthTicks != loop.loopLengthTicks ||
      !playbackWindowSourceIsCurrentForSlot(slotIndex) || request.source.playbackRevision != loop.playbackRevision ||
      (cold && loop.publishedSnapshotIsCurrent() &&
       request.source.snapshotIdentity != loop.publishedSnapshotIdentity())) {
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
    const uint32_t dropBefore = probeCoverageBuildDrop(slotIndex);
    runtime->retireCoverageSource();
    traceCoverageBuildDrop(slotIndex, CoverageBuildDropReason::DemandInvalidated, dropBefore);
    return PlaybackViewTurnResult::GrainDenied;
  }
  if (runtime->coveragePreparation == LoopPlaybackRuntime::CoveragePreparation::Counting)
    return runtime->stepCoverageQualification(kPlaybackViewFillQuantum)
        ? PlaybackViewTurnResult::GrainServed : PlaybackViewTurnResult::GrainDenied;
  if (runtime->coveragePreparation != LoopPlaybackRuntime::CoveragePreparation::AwaitAdmission)
    return PlaybackViewTurnResult::Idle;
  auto contract = playbackViewCoverageContract();
  // Task 3.7h: **the same price the selector positions against** -- fill pages + the two bounded sorts and the rank
  // pass + the retirement of the topology the qualification pinned. The selector's position is priced from a bound
  // over every span of this loop (so the target's own cost can never exceed it); the admission prices the counted
  // window with the one function both read, so a position and a lead test can never disagree about what the work is.
  contract.deriveWindowGrainsFrom(runtime->coverageSourceUnits, runtime->coverageEventCount,
                                  runtime->coverageSource.qualifiedReleaseUnitBound(),
                                  kPlaybackViewFillQuantum, kPlaybackViewOrderQuantum);
  contract.requiredFillGrains = 0; // Already completed in private qualification; never reread.
  // Launch has no deadline to expire. A sibling appearing after qualification
  // temporarily owns service; preserve the counted source and retry admission.
  if ((launch || cold) && playbackViewOutstandingGrains(slotIndex) != 0)
    return PlaybackViewTurnResult::GrainDenied;
  // DEC-136 item 3: the **only** waived term, and only where the envelope itself proves no legal lead exists for
  // this demand's own derived cost on this loop geometry. The demand is then owed without a deadline: the frozen
  // reader keeps serving (declared by `ordinaryAdmissionCannotServeDemand`'s own arithmetic, never by a loop
  // length), a sibling's promise still refuses it below, and any other obstruction merely defers it.
  const bool privateRootWaived = privateRootOwed && ordinaryAdmissionCannotServeDemand(contract, loop.loopLengthTicks);
  if (privateRootWaived) {
    request.reason = PlaybackViewPendingReason::PrivateRootReturnPreparation;
    request.deadlineTick = 0;
    request.clockEpoch = 0;
    request.work.deadlineTick = 0;
    request.work.clockEpoch = 0;
    if ((launch || cold) && playbackViewOutstandingGrains(slotIndex) != 0)
      return PlaybackViewTurnResult::GrainDenied;
    if (!playbackViewAdmissionKeepsAdmittedDeadlines(contract, request.requestedPhase, request.loopLengthTicks,
        contract.requiredGrains(), clock)) {
      return PlaybackViewTurnResult::GrainDenied;
    }
    const uint32_t admitBefore = probeCoverageBuildDrop(slotIndex);
    const bool admitted = runtime->beginQualifiedCoverageBuild(contract, clock);
    traceCoverageBuildDrop(slotIndex, CoverageBuildDropReason::BuildAdmitted, admitBefore);
    return admitted ? PlaybackViewTurnResult::GrainServed : PlaybackViewTurnResult::GrainDenied;
  }
  constexpr uint32_t spanTicks = 2 * Config::TICKS_PER_BAR;
  const bool leadTooShort = !launch && !cold &&
      !contract.canCompleteWithinLead(request.deadlineTick - clock.tick,
                                      playbackViewOutstandingGrains(slotIndex));
  const bool siblingBroken = !launch && !cold &&
      !playbackViewAdmissionKeepsAdmittedDeadlines(contract, request.requestedPhase, request.loopLengthTicks,
                                                   contract.requiredGrains(), clock);
  if (leadTooShort || siblingBroken) {
    request.reason = PlaybackViewPendingReason::CannotCompleteBeforeHandoff;
    // DEC-147: only the **lead** term is a *position* refusal, and only the lane's own demand may be re-positioned.
    // Record the refused span, retire this attempt and let the creation path re-request at the next span -- a fresh
    // qualification for the new interval, never a reposition of the already-counted one. A broken sibling promise, and
    // every non-rolling demand, keeps the terminal path below, so admitted-deadline protection is unchanged.
    if (leadTooShort && !siblingBroken &&
        runtime->rolling.phase == LoopPlaybackRuntime::RollingPhase::Requested && loop.loopLengthTicks) {
      auto& frame = runtime->rolling;
      if (!frame.refusedHandoffValid)
        frame.refusedSpanAttempts = 0;
      if (frame.refusedSpanAttempts != 255)
        ++frame.refusedSpanAttempts;
      // Read out immediately: the row below must state what *this* refusal computed, not what the frame holds after the
      // retirement calls that follow.
      const uint8_t attemptOrdinal = frame.refusedSpanAttempts;
      frame.refusedHandoffPhase = static_cast<uint32_t>(
          (request.coverage.startPhase + spanTicks) % loop.loopLengthTicks);
      frame.refusedHandoffValid = true;
      frame.phase = LoopPlaybackRuntime::RollingPhase::Idle;
      const uint32_t dropBefore = probeCoverageBuildDrop(slotIndex);
      runtime->retireCoverageSource();
      // DEC-108 §13(a) keeps the **oldest** outstanding deadline when a refusal coalesces into the retained record;
      // that would re-impose the very position just measured as unreachable. The re-position replaces the obligation
      // rather than inheriting it, so the retained record is released here and re-created at the next span.
      runtime->pendingViewRequest_ = {};
      traceCoverageBuildDrop(slotIndex, CoverageBuildDropReason::HandoffUnreachable, dropBefore);
      // The row's `rq` field carries the **attempt ordinal** on this exit: it is the only place the traversal budget is
      // observable on the wire, and the ordinal at which the retry stops advancing is exactly what distinguishes "the
      // budget accumulated" from "the frame was re-created between attempts".
      reportPlaybackRollingGateRow("win_repositioned", clock.epoch != 0, /*reprojectedTick=*/false, attemptOrdinal);
      return PlaybackViewTurnResult::GrainDenied;
    }
    const uint32_t dropBefore = probeCoverageBuildDrop(slotIndex);
    runtime->retireCoverageSource();
    traceCoverageBuildDrop(slotIndex, CoverageBuildDropReason::HandoffUnreachable, dropBefore);
    return PlaybackViewTurnResult::GrainDenied;
  }
  const uint32_t admitBefore = probeCoverageBuildDrop(slotIndex);
  const bool admitted = runtime->beginQualifiedCoverageBuild(contract, clock);
  traceCoverageBuildDrop(slotIndex, CoverageBuildDropReason::BuildAdmitted, admitBefore);
  return admitted ? PlaybackViewTurnResult::GrainServed : PlaybackViewTurnResult::GrainDenied;
}

TRACK_COLD_MEM bool Track::hasPendingPlaybackViewMaintenance() const {
  for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
    const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slot);
    const bool runtimePresent = runtime != nullptr;
#if PLAYBACK_WINDOW_ROLLING_ENABLED
    if (runtimePresent &&
        slotMayOwnColdFullLoopWindow(slot, activeLoopIndex, loopsAllocated(),
                                     trackManager.isSlotEnabled(resolveTrackIndexForPersistence(*this), slot),
                                     loopForSlot(slot)) &&
        staleColdRollingReady(*runtime, loopForSlot(slot), playbackGeneration)) return true;
    if (runtimePresent &&
        slotMayOwnColdFullLoopWindow(slot, activeLoopIndex, loopsAllocated(),
                                     trackManager.isSlotEnabled(resolveTrackIndexForPersistence(*this), slot),
                                     loopForSlot(slot)) &&
        clockManager.getPlaybackDeadlineClock().epoch == 0 && !isPlaying() && !isRecording() &&
        !isOverdubbing() && !hasQueuedPlaybackStart() && !isJamPlaybackActive() && !isJamming() &&
        !editManager.isNoteEditActive() && loopForSlot(slot).hasCommittedPasses() &&
        loopForSlot(slot).loopLengthTicks == Config::TICKS_PER_BAR &&
        runtime->rolling.phase == LoopPlaybackRuntime::RollingPhase::Idle &&
        runtime->coverageRoleAvailable() && playbackViewOutstandingGrains(slot) == 0 &&
        runtime->viewSetIfPrewarmed()->lifecycle.current < RuntimePlaybackViewConfig::kViewCount &&
        !runtime->viewSetIfPrewarmed()->views[runtime->viewSetIfPrewarmed()->lifecycle.current].complete)
      return true;
#endif
    if (MidiServiceDrain::playbackViewMaintenanceNeeded(
            runtimePresent, runtimePresent && (runtime->coverageBuildNeedsService() || runtime->ownsCoverageSource() ||
                runtime->retainedGatherActive() ||
                // Task 3.7a (I3b): the **paged preparation** owes pages too -- and only while it still owes them: a
                // completed delivery is its consumer's business, not a deferred turn's, so a standing one does not
                // claim the track's service here.
                (runtime->pagedActive && runtime->frozenViewRequest.active) ||
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
#if PLAYBACK_WINDOW_ROLLING_ENABLED
    if (slot == activeLoopIndex && isRecording() &&
        !hasPendingDeferredOverdubStop() && !hasQueuedPlaybackStart() &&
        !isJamPlaybackActive() && !isJamming() && !editManager.isNoteEditActive() &&
        liveRecordCaptureSource(loopForSlot(slot), *runtime)) {
      const auto& request = runtime->pendingViewRequest_;
      auto& holder = holders[slot];
      const auto* set = runtime->viewSetIfPrewarmed();
      const bool ready = set->lifecycle.ready < set->views.size() &&
          set->views[set->lifecycle.ready].complete &&
          set->views[set->lifecycle.ready].source == request.source &&
          set->views[set->lifecycle.ready].coverage.startPhase == request.coverage.startPhase &&
          set->views[set->lifecycle.ready].coverage.loopLengthTicks == request.coverage.loopLengthTicks;
      const bool adoptedWorker = set->lifecycle.current < set->views.size() &&
          runtime->viewBuild.completedDestinationIs(set->views[set->lifecycle.current]);
      holder.eligible = (request.active && !ready) || adoptedWorker;
      holder.canProgress = holder.eligible && !request.positionInvalidated &&
          (!request.clockEpoch || request.clockEpoch == clock.epoch);
      holder.hasDeadline = request.active && request.clockEpoch != 0 && request.deadlineTick != 0;
      holder.handoffLeadTicks = request.deadlineTick > clock.tick
          ? uint32_t(std::min<uint64_t>(UINT32_MAX, request.deadlineTick - clock.tick)) : 0;
      continue;
    }
    if (slotMayOwnColdFullLoopWindow(slot, activeLoopIndex, loopsAllocated(),
                                     trackManager.isSlotEnabled(resolveTrackIndexForPersistence(*this), slot),
                                     loopForSlot(slot)) &&
        staleColdRollingReady(*runtime, loopForSlot(slot), playbackGeneration)) {
      holders[slot].eligible = true;
      holders[slot].canProgress = true;
      continue;
    }
    // C3 (DEC-116): the cold offer reaches every enabled sounding slot that owns the shape, not only the
    // active one (`slotMayOwnColdFullLoopWindow` returns true for the active slot, so C1 is unchanged).
    if (slotMayOwnColdFullLoopWindow(slot, activeLoopIndex, loopsAllocated(),
                                     trackManager.isSlotEnabled(resolveTrackIndexForPersistence(*this), slot),
                                     loopForSlot(slot)) &&
        clock.epoch == 0 && !isPlaying() &&
        !isRecording() && !isOverdubbing() && !hasQueuedPlaybackStart() &&
        !isJamPlaybackActive() && !isJamming() && !editManager.isNoteEditActive() &&
        loopForSlot(slot).hasCommittedPasses() &&
        loopForSlot(slot).loopLengthTicks == Config::TICKS_PER_BAR &&
        runtime->rolling.phase == LoopPlaybackRuntime::RollingPhase::Idle &&
        runtime->coverageRoleAvailable() && playbackViewOutstandingGrains(slot) == 0 &&
        runtime->viewSetIfPrewarmed()->lifecycle.current < RuntimePlaybackViewConfig::kViewCount &&
        !runtime->viewSetIfPrewarmed()->views[runtime->viewSetIfPrewarmed()->lifecycle.current].complete) {
      holders[slot].eligible = true;
      holders[slot].canProgress = true;
      continue;
    }
#endif
    // Task 3.7a (I2): a **retained-source gather** is a holder too -- it owes its next slice, and the frozen demand it
    // serves carries the deadline the urgency below is read from.
    if (!runtime->coverageBuildNeedsService() && (runtime->ownsCoverageSource() || runtime->retainedGatherActive() ||
        // Task 3.7a (I3b): a **paged preparation in flight** owes pages, so it is a holder too -- with the deadline of
        // the demand it serves (read below from the frozen request, which is this lane's promise).
        (runtime->pagedActive && runtime->frozenViewRequest.active) ||
        (pending && (pending->sourceKind == PlaybackViewPendingRequest::SourceKind::CommittedSnapshot ||
                     pending->sourceKind == PlaybackViewPendingRequest::SourceKind::CommittedLaunch)))) {
      auto& holder = holders[slot];
      holder.eligible = true;
      holder.canProgress = committedPlaybackRetryCanProgress(slot, clock);
#if PLAYBACK_WINDOW_ROLLING_ENABLED
      // A stale Committed request can be re-stamped without touching the
      // Capture ready that still owns this Record edge. The ordinary retry
      // cannot progress while ready is occupied, but this one scalar request
      // update is a valid Track-owner grain, not a build or a new scheduler.
      if (!holder.canProgress && pending &&
          pending->sourceKind == PlaybackViewPendingRequest::SourceKind::CommittedSnapshot &&
          !runtime->ownsCoverageSource() && !runtime->retainedGatherActive() &&
          !runtime->frozenViewRequest.active && clock.epoch != 0) {
        const CaptureStopPresentation* presentation = captureStopPresentationForSlot(slot);
        if (presentation != nullptr && presentation->retirementActive && presentation->slot == slot &&
            presentation->owner != nullptr && presentation->owner->publishedSnapshotIsCurrent() &&
            runtime->hasBoundRecordReturnWindowForPendingJob()) {
          const Loop& rootLoop = loopForSlot(slot);
          const auto* set = runtime->viewSetIfPrewarmed();
          RuntimePlaybackViewSource frozen{};
          PlaybackViewSourceIdentity receipt{};
          if (set && set->lifecycle.ready < set->views.size() &&
              qualifiedFrozenRecordCaptureSourceForSlot(slot, presentation->interval, frozen, receipt)) {
            const auto& ready = set->views[set->lifecycle.ready];
            if (ready.complete && !ready.published &&
                ready.source.origin == PlaybackWindowOrigin::Capture &&
                ready.source.sourceGeneration == playbackGeneration &&
                ready.source.captureContentGeneration == receipt.captureContentGeneration &&
                ready.coverage.loopLengthTicks == rootLoop.loopLengthTicks) {
              const uint32_t phase = IntervalProjection::tickPhaseInProjectionCycle(
                  currentPhase, projectionCycleStartTick, rootLoop.loopLengthTicks);
              const bool windowed = rootLoop.shouldAvoidFullVisualRebuild(rootLoop.loopLengthTicks);
              const uint32_t targetStart = windowed
                  ? (phase / Config::TICKS_PER_BAR) * Config::TICKS_PER_BAR : 0;
              const uint32_t targetLength = windowed
                  ? std::min<uint32_t>(Config::TICKS_PER_BAR, rootLoop.loopLengthTicks - targetStart)
                  : rootLoop.loopLengthTicks;
              const PlaybackViewSourceIdentity rootSource{
                  rootLoop.playbackRevision, playbackGeneration, rootLoop.publishedSnapshotIdentity()};
              const bool privateRootSourceCanStart =
                  !pending->positionInvalidated && pending->clockEpoch == clock.epoch &&
                  clock.tick >= pending->work.admittedTick && clock.tick < pending->deadlineTick &&
                  pending->source == rootSource && pending->coverage.startPhase == targetStart &&
                  pending->coverage.lengthTicks == targetLength &&
                  pending->coverage.loopLengthTicks == rootLoop.loopLengthTicks &&
                  runtime->coverageRoleAvailable(true);
              holder.canProgress = privateRootSourceCanStart || pending->positionInvalidated ||
                  pending->clockEpoch != clock.epoch || clock.tick < pending->work.admittedTick ||
                  clock.tick >= pending->deadlineTick || !(pending->source == rootSource) ||
                  pending->coverage.startPhase != targetStart ||
                  pending->coverage.lengthTicks != targetLength ||
                  pending->coverage.loopLengthTicks != rootLoop.loopLengthTicks;
            }
          }
        }
      }
#endif
      if (runtime->coverageSource.retiring() || (runtime->ownsCoverageSource() && !runtime->frozenViewRequest.active)) {
        holder.hasDeadline = true; holder.handoffLeadTicks = 0; continue;
      }
      const auto& request = runtime->frozenViewRequest.active
                                ? runtime->frozenViewRequest
                                : (pending != nullptr ? *pending : runtime->pendingViewRequest_);
      holder.hasDeadline = request.clockEpoch != 0 && request.clockEpoch == clock.epoch;
      const uint64_t lead = request.deadlineTick > clock.tick ? request.deadlineTick - clock.tick : 0;
      holder.handoffLeadTicks = lead > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(lead);
      continue;
    }
    // S7.5b.5: the RecordPublished owner remains a holder through source handoff.
    // While the root is stale, prepare the next frozen-range bar; once current,
    // prepare the root-backed replacement before Record retirement.
    if (!runtime->coverageBuildNeedsService()) {
      const CaptureStopPresentation* recordJob = captureStopPresentationForSlot(slot);
      const Loop& recordLoop = loopForSlot(slot);
      if (recordJob != nullptr && hasRecordReaderOwnerForSlot(slot) &&
          recordJob->slot == slot && recordJob->ownsExtent && recordJob->owner != nullptr &&
          runtime->hasBoundRecordReturnWindowForPendingJob() &&
          recordLoop.loopLengthTicks != 0) {
        const bool rootCurrent = recordJob->owner->publishedSnapshotIsCurrent();
        PlaybackViewSourceIdentity source{
            recordLoop.playbackRevision, playbackGeneration, recordLoop.publishedSnapshotIdentity()};
        if (!rootCurrent && recordJob->frozenCaptureContentGeneration != 0) {
          RuntimePlaybackViewSource frozen{};
          PlaybackViewSourceIdentity receipt{};
          if (!qualifiedFrozenRecordCaptureSourceForSlot(slot, recordJob->interval,
                                                         frozen, receipt)) continue;
#if PLAYBACK_WINDOW_ROLLING_ENABLED
          source = receipt;
#endif
        }
        const uint32_t phase = IntervalProjection::tickPhaseInProjectionCycle(
            currentPhase, projectionCycleStartTick, recordLoop.loopLengthTicks);
        const uint32_t barStart = (phase / Config::TICKS_PER_BAR) * Config::TICKS_PER_BAR;
        const bool windowed = recordLoop.shouldAvoidFullVisualRebuild(recordLoop.loopLengthTicks);
        uint32_t targetStart = rootCurrent ? (windowed ? barStart : 0)
            : (runtime->hasServableDeferredRecordView() ? barStart + Config::TICKS_PER_BAR : barStart);
        if (targetStart >= recordLoop.loopLengthTicks) targetStart = 0;
        const uint32_t targetLength = rootCurrent && !windowed
            ? recordLoop.loopLengthTicks
            : std::min<uint32_t>(Config::TICKS_PER_BAR, recordLoop.loopLengthTicks - targetStart);
        const RuntimePlaybackViewSet* set = runtime->viewSetIfPrewarmed();
        const auto matches = [&](uint8_t index, bool containsPhase) {
          if (set == nullptr || index >= set->views.size()) return false;
          const RuntimePlaybackView& view = set->views[index];
          return view.complete && view.source == source &&
              view.coverage.startPhase == targetStart && view.coverage.lengthTicks == targetLength &&
              view.coverage.loopLengthTicks == recordLoop.loopLengthTicks &&
              (!containsPhase || view.coverage.contains(phase));
        };
        const RuntimePlaybackView* current = set != nullptr && set->lifecycle.current < set->views.size()
            ? &set->views[set->lifecycle.current] : nullptr;
        const bool currentRebindable = rootCurrent && !windowed && current != nullptr &&
            current->complete && current->source.playbackRevision == source.playbackRevision &&
            current->source.sourceGeneration == source.sourceGeneration && current->coverage.contains(phase);
        const bool rootServed = rootCurrent &&
            (matches(set != nullptr ? set->lifecycle.current : RuntimePlaybackViewLifecycle::kNoView, true) ||
             currentRebindable);
        const bool boundFirstBarServesPlayhead = !rootCurrent && current != nullptr &&
            current->firstRecordReturnWindow && current->complete &&
            current->source == source && current->coverage.contains(phase);
        // A ready covering the playhead still belongs to the clock edge only
        // at its own boundary. Once that edge has passed without adoption,
        // it is late for this traversal and cannot satisfy the *next* bar's
        // preparation obligation merely because it still covers this phase.
        const bool readyAtOwnBoundary = set != nullptr &&
            set->lifecycle.ready < set->views.size() && set->views[set->lifecycle.ready].complete &&
            !set->views[set->lifecycle.ready].firstRecordReturnWindow &&
            set->views[set->lifecycle.ready].source == source &&
            set->views[set->lifecycle.ready].coverage.startPhase == phase &&
            !boundFirstBarServesPlayhead;
        const bool successorReady = readyAtOwnBoundary ||
            matches(set != nullptr ? set->lifecycle.ready : RuntimePlaybackViewLifecycle::kNoView,
                    !rootCurrent);
        const bool initialReturnWaiting = set != nullptr &&
            set->lifecycle.ready < set->views.size() && set->views[set->lifecycle.ready].complete &&
            set->views[set->lifecycle.ready].firstRecordReturnWindow;
        if (!rootServed && !successorReady && (rootCurrent || !initialReturnWaiting)) {
          const uint32_t boundary = rootCurrent
              ? ((targetStart + targetLength >= recordLoop.loopLengthTicks) ? 0 : targetStart + targetLength)
              : targetStart;
          const uint32_t lead = boundary > phase ? boundary - phase
              : recordLoop.loopLengthTicks - phase + boundary;
          MidiServiceDrain::PlaybackHolder& holder = holders[slot];
          holder.eligible = true;
          holder.canProgress = true;
          holder.hasDeadline = true;
          holder.handoffLeadTicks = lead;
          continue;
        }
      }
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
    holder.handoffLeadTicks = holder.hasDeadline
        ? handoffLeadTicks(currentPhase % loop.loopLengthTicks, interval.startPhase, loop.loopLengthTicks)
        : 0;
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

// The production nullptr entry needs a persistent tie cursor per Track. Keep this small
// off-clock owner state out of the eight DTCM Track objects and the RAM1 global bank.
#if defined(__IMXRT1062__)
DMAMEM static MidiServiceDrain::PlaybackHolderCursor sWithinTrackWindowCursors[Config::NUM_TRACKS]{};
#else
static MidiServiceDrain::PlaybackHolderCursor sWithinTrackWindowCursors[Config::NUM_TRACKS]{};
#endif

TRACK_COLD_MEM Track::PlaybackViewTurnResult Track::stepPlaybackViewMaintenance(
    uint32_t currentPhase, MidiServiceDrain::PlaybackHolderCursor* cursor, bool transportRunning,
    PlaybackDeadlineClock clock, PlaybackDeadlineClock (*completionClockReader)()) {
#if PLAYBACK_WINDOW_ROLLING_ENABLED
  if (isRecording()) (void)serviceRollingPlaybackWindow(clock);
#endif
  (void)transportRunning;  // Compatibility argument; sampling belongs to the lane dispatcher.
  // The same Track still owns every Loop. Do this before selection: an inactive
  // Capture is excluded as a holder but must still have a cancellation producer.
  playbackRuntime.cancelStaleLiveCaptureBuilds();
  // DEC-108 §13a / task 3.10 (blocker 3, half 2): **the deadlines the admission promised are serviced first**, for every
  // slot holding a live promise -- not only the holder this turn happens to serve, and not only when a build completes.
  // The judgement never depends on the revision currency; the consequence is `noteMissedTraversalForSlot`'s.
  serviceAdmittedDeadlineMisses(clock);
  // **DEC-202 -- a completed promise is judged or released on this turn, never held.** The earlier attempt wired
  // `revalidateCompletedPlaybackWindow` alone; its publish path had no production caller and crashed the native harness,
  // and it can only ever agree at one exact boundary. This turn asks the same predicate, judges only when it agrees, and
  // otherwise releases the completion so the slot's role is not locked. Every slot is swept here because the promise
  // belongs to the slot, not to the holder the rotation happens to serve.
  for (uint8_t promiseSlot = 0; promiseSlot < Config::MAX_LOOPS_PER_TRACK; ++promiseSlot) {
    (void)retireUnjudgeableCompletedPromise(promiseSlot, currentPhase, clock);
  }
  MidiServiceDrain::PlaybackHolder holders[Config::MAX_LOOPS_PER_TRACK] = {};
  fillPlaybackHolders(currentPhase, holders, clock);
  // The shared policy chooses which holder this turn serves (task 3.10): holders rotate, the earliest outstanding
  // handoff is served first, and the cursor advances even when the chosen holder cannot progress.
  const uint8_t trackIndex = resolveTrackIndexForPersistence(*this);
  MidiServiceDrain::PlaybackHolderCursor& rotation =
      cursor != nullptr ? *cursor : sWithinTrackWindowCursors[trackIndex < Config::NUM_TRACKS ? trackIndex : 0];
  // DEC-193 (report-only): one row per change, keyed by an id, so a turn that never reaches the service body is named.
  const auto reportDispatch = [&](uint8_t slotForRow, uint8_t id, const char* name) {
    if (slotForRow >= Config::MAX_LOOPS_PER_TRACK) return;
    const uint8_t dispatchTrack = playbackRebuildTrackIndex(*this);
    if (dispatchTrack < Config::NUM_TRACKS && gPlaybackDispatchLast[dispatchTrack][slotForRow] != id) {
      gPlaybackDispatchLast[dispatchTrack][slotForRow] = id;
      reportPlaybackViewMaintenanceRow(slotForRow, loopForSlot(slotForRow), name);
    }
  };
  // DEC-193: an eligible holder whose `canProgress` is false is **skipped before the service body**, so the retry's own
  // row can never explain it. `canProgress` is the same gate, so name it here, where it is asked.
  for (uint8_t offerSlot = 0; offerSlot < Config::MAX_LOOPS_PER_TRACK; ++offerSlot) {
    if (!holders[offerSlot].eligible || holders[offerSlot].canProgress) continue;
    const PlaybackBuildRefusal gate = playbackBuildGateRefusal(offerSlot, clock);
    if (gate != PlaybackBuildRefusal::None && gate != PlaybackBuildRefusal::SourceRetiring) {
      reportDispatch(offerSlot, static_cast<uint8_t>(gate), playbackBuildRefusalName(gate));
      // DEC-194: when the gate's term is the role, name the role's own first false term at the same instant.
      if (gate == PlaybackBuildRefusal::NoCoverageRole) {
        const LoopPlaybackRuntime* offerRuntime = playbackRuntime.slotIfAllocated(offerSlot);
        if (offerRuntime != nullptr) {
          const PlaybackCoverageRoleRefusal role = offerRuntime->coverageRoleRefusal(false);
          reportDispatch(offerSlot, static_cast<uint8_t>(0xE0u + static_cast<uint8_t>(role)),
                         playbackCoverageRoleRefusalName(role));
        }
      }
    }
  }
  uint8_t chosen = MidiServiceDrain::kNoPlaybackHolder;
  if (MidiServiceDrain::selectPlaybackHolder(rotation, holders, Config::MAX_LOOPS_PER_TRACK, chosen) !=
      MidiServiceDrain::PlaybackHolderSelection::Service) {
    reportDispatch(getActiveLoopIndex(), 0xF0u, "dispatch_no_holder");
    return PlaybackViewTurnResult::Idle;
  }
  const uint8_t slot = chosen;
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slot);
  if (runtime == nullptr) {
    reportDispatch(slot, 0xF1u, "dispatch_no_runtime");
    return PlaybackViewTurnResult::Idle;
  }
  RuntimePlaybackViewBuild::PreparationTurn primaryTurn(runtime->viewBuild);
  RuntimePlaybackViewBuild::PreparationTurn bridgeTurn(runtime->bridgeWindowBuild);
  if (!primaryTurn || !bridgeTurn) {
    reportDispatch(slot, 0xF2u, "dispatch_no_build_role");
    return PlaybackViewTurnResult::GrainDenied;
  }
  // DEC-193: where this turn stopped, when it did not reach the committed retry. Reported once per change below.
  PlaybackDispatchRefusal dispatch = PlaybackDispatchRefusal::SelectedNoBranch;
  // Every early return still reports cancellation of this selected owner's turn.
  const auto service = [&]() TRACK_COLD_MEM -> PlaybackViewTurnResult {
  dispatch = PlaybackDispatchRefusal::ServiceEntered;
#if PLAYBACK_WINDOW_ROLLING_ENABLED
  if (slot == activeLoopIndex && isRecording() &&
      !hasPendingDeferredOverdubStop() && !hasQueuedPlaybackStart() &&
      liveRecordCaptureSource(loopForSlot(slot), *runtime)) {
    dispatch = PlaybackDispatchRefusal::LiveCapture;
    auto& loop = loopForSlot(slot);
    auto* set = runtime->viewSetIfPrewarmed();
    auto& request = runtime->pendingViewRequest_;
    if (request.positionInvalidated ||
        !liveRecordRecipeMatches(request, loop, *runtime, playbackGeneration, getProjectionCycleStartTick()) ||
        (request.clockEpoch && request.clockEpoch != clock.epoch)) return PlaybackViewTurnResult::GrainDenied;
    // An adopted completed worker is detached before selecting another physical role.
    if (set->lifecycle.current < set->views.size() &&
        runtime->viewBuild.completedDestinationIs(set->views[set->lifecycle.current]))
      runtime->viewBuild.discard(&set->views[set->lifecycle.current]);
    if (!request.active) return PlaybackViewTurnResult::Idle;
    if (set->lifecycle.ready < set->views.size() && set->views[set->lifecycle.ready].complete) {
      auto& ready = set->views[set->lifecycle.ready];
      if (ready.source == request.source && ready.coverage.startPhase == request.coverage.startPhase &&
          ready.coverage.loopLengthTicks == request.coverage.loopLengthTicks) return PlaybackViewTurnResult::Idle;
      PlaybackSourceReadGuard roleGuard;
      // Reopen only this worker's unpublished private result. Reader/current stays immutable.
      if (!runtime->viewBuild.completedDestinationIs(ready) || ready.published)
        return PlaybackViewTurnResult::GrainDenied;
      std::swap(set->lifecycle.ready, set->lifecycle.build);
    }
    if (set->lifecycle.build >= set->views.size()) return PlaybackViewTurnResult::GrainDenied;
    auto& candidate = set->views[set->lifecycle.build];
    if (runtime->viewBuild.ownsLiveCaptureBorrow() &&
        (candidate.coverage.startPhase != request.coverage.startPhase ||
         candidate.coverage.lengthTicks != request.coverage.lengthTicks ||
         candidate.coverage.loopLengthTicks != request.coverage.loopLengthTicks))
      runtime->viewBuild.discard(); // Geometry is never append-certified.
    if (!runtime->viewBuild.ownsLiveCaptureBorrow()) {
      if (runtime->viewBuild.active() || candidate.published) return PlaybackViewTurnResult::GrainDenied;
      runtime->viewBuild.discard();
      const auto source = RuntimePlaybackViewSource::fromLiveCapture(loop, true);
      runtime->frozenViewRequest = request;
      if (!runtime->viewBuild.begin(candidate, source, request.source, request.coverage, {}, {},
                                    RuntimePlaybackViewFillScope::PlaybackCoverage))
        return PlaybackViewTurnResult::GrainDenied;
      return PlaybackViewTurnResult::GrainServed;
    }
    auto grain = runtime->viewBuild.nextGrain();
    if (!(candidate.source == request.source) && grain != RuntimePlaybackViewBuildGrain::Order) {
      const size_t frontier = runtime->viewBuild.liveCaptureSourceFrontier();
      const size_t orderedPrefix = runtime->viewBuild.privateOrderedEventCount();
      if (!runtime->viewBuild.extendLiveCapture(request.source)) {
        runtime->viewBuild.discard();
        return PlaybackViewTurnResult::GrainDenied;
      }
      runtime->frozenViewRequest = request;
      priceLiveRecordWork(runtime->frozenViewRequest.work, runtime->viewBuild.liveCaptureSourceExtent(),
                          frontier, orderedPrefix, candidate.storage.capacity);
      grain = runtime->viewBuild.nextGrain();
    }
    RuntimePlaybackViewBuildGrain result;
    {
      // DEC-239 profile scope (short: one declared grain, <= 64 fill / <= 48 order units, ExecutionBounds
      // row 27 -- a structural cap far below one cycle-counter period). Only the grain, never the sweep.
      PROFILE_SCOPE_SHORT(profileScope, RuntimeProfiler::ProfileId::PlaybackViewService_grain);
      result = grain == RuntimePlaybackViewBuildGrain::Fill
          ? runtime->viewBuild.stepFill(kPlaybackViewFillQuantum)
          : grain == RuntimePlaybackViewBuildGrain::Order
              ? runtime->viewBuild.stepOrder(kPlaybackViewOrderQuantum) : grain;
      profileScope.setResult(static_cast<uint8_t>(result));
    }
    if (result == RuntimePlaybackViewBuildGrain::Denied) {
      runtime->viewBuild.discard();
      return PlaybackViewTurnResult::GrainDenied;
    }
    if (result == RuntimePlaybackViewBuildGrain::Complete) {
      const auto finished = clock.epoch
          ? (completionClockReader ? completionClockReader() : clockManager.getPlaybackDeadlineClock()) : clock;
      if (finished.epoch != clock.epoch || finished.tick < clock.tick)
        return PlaybackViewTurnResult::GrainDenied;
      const bool initial = runtime->rolling.phase == LoopPlaybackRuntime::RollingPhase::Idle &&
          request.deadlineTick == 0;
      const uint32_t floor = IntervalProjection::tickPhaseInProjectionCycle(
          uint32_t(finished.tick), getProjectionCycleStartTick(), candidate.coverage.loopLengthTicks);
      size_t first = 0, last = initial ? candidate.storage.eventCount : 0;
      while (first < last) {
        const size_t middle = first + (last - first) / 2;
        if (candidate.storage.phases[candidate.storage.order[middle]] <= floor) first = middle + 1;
        else last = middle;
      }
      {
      PlaybackSourceReadGuard publicationGuard;
      if (!primaryTurn || !bridgeTurn || !runtime->viewBuild.completedDestinationIs(candidate) ||
          &set->views[set->lifecycle.build] != &candidate ||
          candidate.coverage.startPhase != request.coverage.startPhase ||
          candidate.coverage.lengthTicks != request.coverage.lengthTicks ||
          candidate.coverage.loopLengthTicks != request.coverage.loopLengthTicks ||
          !liveRecordRecipeMatches(request, loop, *runtime, playbackGeneration, getProjectionCycleStartTick()))
        return PlaybackViewTurnResult::GrainDenied;
      if (initial != (runtime->rolling.phase == LoopPlaybackRuntime::RollingPhase::Idle &&
                      request.deadlineTick == 0)) return PlaybackViewTurnResult::GrainServed;
      if (clock.epoch) {
        const auto now = clockManager.getPlaybackDeadlineClock();
        if (now.epoch != finished.epoch || (initial && now.tick != finished.tick))
          return PlaybackViewTurnResult::GrainServed;
      }
      if (candidate.source.captureContentGeneration != loop.captureContentGeneration())
        return PlaybackViewTurnResult::GrainServed; // Finish frozen order, then extend the bounded suffix.
      if (initial) {
        runtime->viewBuild.discard(&candidate);
        set->lifecycle.retained = RuntimePlaybackViewLifecycle::kNoView;
        if (set->lifecycle.ready >= set->views.size()) {
          set->lifecycle.ready = set->lifecycle.idle;
          if (set->lifecycle.ready < set->views.size())
            set->views[set->lifecycle.ready].resetMetadata(); // Idle copy is not a successor for this take.
          set->lifecycle.idle = RuntimePlaybackViewLifecycle::kNoView;
        }
        if (!set->lifecycle.publishCompletedBuildAsCurrentWindow(set->views))
          return PlaybackViewTurnResult::GrainDenied;
        request.active = false;
        runtime->frozenViewRequest = {};
        // Only the precomputed cursor floor is committed under the metadata mask.
        loop.nextEventIndex = static_cast<uint16_t>(first);
        loop.lastTickInLoop = floor;
      } else if (!set->lifecycle.publishCompletedBuildAsReadySuccessor(set->views))
        return PlaybackViewTurnResult::GrainDenied;
      if (!initial) runtime->frozenViewRequest.work.admittedTick = finished.tick;
      }
      const auto completed = finishPlaybackViewGrain(slot, result);
      reportPlaybackWindowReadinessReceipt(slot, currentPhase);
      return completed;
    }
    return PlaybackViewTurnResult::GrainServed;
  }
#endif
#if PLAYBACK_WINDOW_ROLLING_ENABLED
  // C3 (DEC-116): a **slot-qualified** stale cold candidate is retired on this turn too -- for the active
  // slot exactly as before, and for an enabled sounding slot whose own candidate went stale; a disabled or
  // exited slot is not offered here at all.
  if (slotMayOwnColdFullLoopWindow(slot, activeLoopIndex, loopsAllocated(),
                                   trackManager.isSlotEnabled(resolveTrackIndexForPersistence(*this), slot),
                                   loopForSlot(slot)) &&
      staleColdRollingReady(*runtime, loopForSlot(slot), playbackGeneration)) {
    const Loop& loop = loopForSlot(slot);
    cancelRollingPlaybackWindow(slot); // Deferred retirement, never from the MIDI edge.
    if (isPlaying() && loop.loopLengthTicks == Config::TICKS_PER_BAR) {
      const PlaybackCoverageInterval coverage{0, loop.loopLengthTicks, loop.loopLengthTicks};
      const PlaybackViewSourceIdentity source{loop.playbackRevision, playbackGeneration,
                                              loop.publishedSnapshotIdentity()};
      // C3 (DEC-116): read through **this slot's own** origin (`Loop::startLoopTick`) when it is not the
      // active one; the active slot's expression is unchanged.
      const uint32_t phase = IntervalProjection::tickPhaseInProjectionCycle(
          currentPhase,
          slot == activeLoopIndex ? projectionCycleStartTick
                                  : static_cast<int32_t>(loop.startLoopTick),
          loop.loopLengthTicks);
      retainRefusedPlaybackViewRequest(slot, runtime,
          RuntimePlaybackViewConfig::kWindowAdmissionLimit, source, coverage,
          phase, 0, clock, PlaybackViewPendingReason::CannotCompleteBeforeHandoff);
      runtime->pendingViewRequest_.sourceKind =
          PlaybackViewPendingRequest::SourceKind::CommittedSnapshot;
    }
    return PlaybackViewTurnResult::GrainDenied;
  }
  // C3 (DEC-116): the cold one-bar obligation belongs to the active slot **and** to every enabled sounding
  // slot that owns the same shape (`slotMayOwnColdFullLoopWindow`). Everything after this term is unchanged,
  // so nothing but the slot qualification of the route moves.
  if (slotMayOwnColdFullLoopWindow(slot, activeLoopIndex, loopsAllocated(),
                                   trackManager.isSlotEnabled(resolveTrackIndexForPersistence(*this), slot),
                                   loopForSlot(slot)) &&
      clock.epoch == 0 && !isPlaying() && !isRecording() &&
      !isOverdubbing() && !hasQueuedPlaybackStart() && !isJamPlaybackActive() &&
      !isJamming() && !editManager.isNoteEditActive() && loopsAllocated() &&
      loopForSlot(slot).hasCommittedPasses() &&
      loopForSlot(slot).loopLengthTicks == Config::TICKS_PER_BAR &&
      runtime->rolling.phase == LoopPlaybackRuntime::RollingPhase::Idle &&
      runtime->coverageRoleAvailable() && playbackViewOutstandingGrains(slot) == 0 &&
      runtime->viewSetIfPrewarmed()->lifecycle.current < RuntimePlaybackViewConfig::kViewCount &&
      !runtime->viewSetIfPrewarmed()->views[runtime->viewSetIfPrewarmed()->lifecycle.current].complete) {
    const auto& pending = runtime->pendingViewRequest_;
    const bool rebaseColdDebt = pending.active &&
        pending.sourceKind == PlaybackViewPendingRequest::SourceKind::CommittedSnapshot &&
        pending.reason == PlaybackViewPendingReason::CannotCompleteBeforeHandoff &&
        pending.operationGeneration == playbackGeneration &&
        pending.coverage.startPhase == 0 &&
        pending.coverage.lengthTicks == Config::TICKS_PER_BAR &&
        pending.coverage.loopLengthTicks == Config::TICKS_PER_BAR;
    if ((pending.active && !rebaseColdDebt) || runtime->frozenViewRequest.active ||
        !runtime->coverageRoleAvailable() || playbackViewOutstandingGrains(slot) != 0 ||
        captureStopJobHead() != nullptr || runtime->hasBoundRecordReturnWindowForPendingJob())
      return PlaybackViewTurnResult::GrainDenied;
    // The stopped cold source supersedes its prior missed-position debt, not a
    // different holder's request; the new request below owns this revision.
    if (rebaseColdDebt) runtime->clearViewPendingRequest();
    const PlaybackCoverageInterval coverage{0, Config::TICKS_PER_BAR, Config::TICKS_PER_BAR};
    if (requestCommittedPlaybackWindow(slot, coverage, 0, clock) !=
        PlaybackViewRequestResult::DeferredSourceQualification)
      return PlaybackViewTurnResult::GrainDenied;
    auto& frame = runtime->rolling;
    frame = LoopPlaybackRuntime::RollingFrame{};
    frame.generation = playbackGeneration;
    frame.loopLength = coverage.loopLengthTicks;
    // C3 (DEC-116): a **non-active** slot's cold frame is minted against **its own** origin
    // (`Loop::startLoopTick`, the convention `Track::resetPlaybackStateForSlot` and the slot-owned display
    // phase already use), never against the Track-level anchor that the selection's re-anchor moves. The
    // clock edge validates it against that same origin, so a switch cannot invalidate it.
    frame.projectionStart = slot == activeLoopIndex
        ? getProjectionCycleStartTick()
        : static_cast<int32_t>(loopForSlot(slot).startLoopTick);
    frame.phase = LoopPlaybackRuntime::RollingPhase::Requested;
    return PlaybackViewTurnResult::GrainServed;
  }
#endif
  const CaptureStopPresentation* recordJob = captureStopPresentationForSlot(slot);
  // Presentation ownership, not the interaction ring, carries the Record reader obligation.
  if (recordJob != nullptr && recordJob->retirementActive &&
      recordJob->slot == slot && recordJob->ownsExtent && recordJob->owner != nullptr &&
      recordJob->owner->publishedSnapshotIsCurrent() && runtime->hasServableDeferredRecordView() &&
      isAudiblePlaybackActive() && runtime->hasBoundRecordReturnWindowForPendingJob() &&
      !runtime->coverageBuildNeedsService()) {
    dispatch = PlaybackDispatchRefusal::RecordReturn;
    const Loop& recordLoop = loopForSlot(slot);
    const uint32_t phase = IntervalProjection::tickPhaseInProjectionCycle(
        currentPhase, projectionCycleStartTick, recordLoop.loopLengthTicks);
    // Rolling Record-return serves the same one-bar intervals the rolling reader
    // serves: a short take's whole-loop window can never be admitted within the
    // declared boundary lead, which stranded the held crossing Stop.
    const bool windowed = PLAYBACK_WINDOW_ROLLING_ENABLED ||
        recordLoop.shouldAvoidFullVisualRebuild(recordLoop.loopLengthTicks);
    const uint32_t targetStart = windowed
        ? (phase / Config::TICKS_PER_BAR) * Config::TICKS_PER_BAR : 0;
    const uint32_t targetLength = windowed
        ? std::min<uint32_t>(Config::TICKS_PER_BAR, recordLoop.loopLengthTicks - targetStart)
        : recordLoop.loopLengthTicks;
    const PlaybackCoverageInterval coverage{targetStart, targetLength, recordLoop.loopLengthTicks};
    const PlaybackViewSourceIdentity source{
        recordLoop.playbackRevision, playbackGeneration, recordLoop.publishedSnapshotIdentity()};
    RuntimePlaybackViewSet* set = runtime->viewSetIfPrewarmed();
    if (set == nullptr) return PlaybackViewTurnResult::GrainDenied;
    const RuntimePlaybackView* served = set->lifecycle.current < set->views.size()
        ? &set->views[set->lifecycle.current] : nullptr;
    if (!recordLoop.shouldAvoidFullVisualRebuild(recordLoop.loopLengthTicks) && served != nullptr &&
        served->complete && served->source.playbackRevision == source.playbackRevision &&
        served->source.sourceGeneration == source.sourceGeneration && served->coverage.contains(phase) &&
        (!PLAYBACK_WINDOW_ROLLING_ENABLED ||
         (served->source.origin == PlaybackWindowOrigin::Committed &&
          served->source.snapshotIdentity == source.snapshotIdentity))) {
      return PlaybackViewTurnResult::Idle; // A complete same-revision copy bridges the root identity swap.
    }
    if (!runtime->viewBuild.active() && set->lifecycle.build < set->views.size() &&
        set->lifecycle.build != set->lifecycle.current && set->lifecycle.build != set->lifecycle.ready &&
        set->lifecycle.build != set->lifecycle.retained &&
        // 3.15e/2b-ii-b: a retained overlay candidate is complete and unpublished by design; it is not a leftover
        // shell, and only its own retirement may clear it. Read it through the runtime owner (flash), never by
        // re-deriving the candidate predicate here.
        runtime->overdubOverlayCandidate() != &set->views[set->lifecycle.build]) {
      // The lifecycle's build role is private. Clear leftover completion metadata
      // before current-root qualification so it can reuse the existing buffer.
      set->views[set->lifecycle.build].resetMetadata();
    }
    if (runtime->viewBuild.active() && set->lifecycle.build < set->views.size() &&
        !(set->views[set->lifecycle.build].source == source)) {
      const uint32_t dropBefore = probeCoverageBuildDrop(slot);
      runtime->discardCoverageBuild();
      traceCoverageBuildDrop(slot, CoverageBuildDropReason::SourceSuperseded, dropBefore);
      return PlaybackViewTurnResult::GrainDenied;
    }
    if (set->lifecycle.ready < set->views.size() && set->views[set->lifecycle.ready].complete) {
      const RuntimePlaybackView& ready = set->views[set->lifecycle.ready];
#if PLAYBACK_WINDOW_ROLLING_ENABLED
      if (!ready.published && ready.source.origin == PlaybackWindowOrigin::Capture &&
          ready.source.sourceGeneration == playbackGeneration &&
          ready.source.captureContentGeneration != 0 &&
          ready.source.captureContentGeneration == recordJob->frozenCaptureContentGeneration &&
          ready.coverage.loopLengthTicks == recordLoop.loopLengthTicks) {
        RuntimePlaybackViewSource frozen{};
        PlaybackViewSourceIdentity receipt{};
        if (qualifiedFrozenRecordCaptureSourceForSlot(slot, recordJob->interval, frozen, receipt)) {
          // The Capture successor still owns its edge. Name the complete-root
          // obligation now, while the source is available, but do not consume
          // the occupied ready role or admit an unproven build on this turn.
          if (runtime->ownsCoverageSource()) {
            dispatch = PlaybackDispatchRefusal::Retry;
      return stepCommittedPlaybackRetry(slot, clock);
          }
          const PlaybackViewPendingRequest* pending = pendingViewRequest(slot);
          if (pending != nullptr) {
            if (pending->sourceKind != PlaybackViewPendingRequest::SourceKind::CommittedSnapshot)
              return PlaybackViewTurnResult::GrainDenied; // Another live demand owns the slot.
            if (!pending->positionInvalidated && clock.epoch != 0 &&
                pending->clockEpoch == clock.epoch && clock.tick >= pending->work.admittedTick &&
                clock.tick < pending->deadlineTick && pending->source == source &&
                pending->coverage.startPhase == coverage.startPhase &&
                pending->coverage.lengthTicks == coverage.lengthTicks &&
                pending->coverage.loopLengthTicks == coverage.loopLengthTicks) {
              if (!runtime->coverageRoleAvailable(true)) return PlaybackViewTurnResult::Idle;
              const auto snapshot = recordLoop.publishedSnapshot();
              if (snapshot && snapshot.get() == source.snapshotIdentity &&
                  runtime->beginCoverageQualification(*snapshot, true))
                return PlaybackViewTurnResult::GrainServed;
              return PlaybackViewTurnResult::GrainDenied;
            }
          }
          if (clock.epoch == 0) return PlaybackViewTurnResult::GrainDenied;
          const PlaybackViewRequestResult requested = requestCommittedPlaybackWindow(
              slot, coverage, phase, clock);
          return requested == PlaybackViewRequestResult::DeferredSourceQualification
              ? PlaybackViewTurnResult::GrainServed : PlaybackViewTurnResult::GrainDenied;
        }
      }
#endif
      if (!(ready.source == source) && !set->lifecycle.retireSupersededReady(set->views))
        return PlaybackViewTurnResult::GrainDenied;
    }
    // A completed but unpublished shell left in the build role is not a reader
    // and must return to the existing builder before current-root handoff work.
    if (!runtime->viewBuild.active() && set->lifecycle.build < set->views.size() &&
        !set->views[set->lifecycle.build].published &&
        set->views[set->lifecycle.build].complete &&
        !(set->views[set->lifecycle.build].source == source) &&
        // 3.15e/2b-ii-b: a retained overlay candidate is not a leftover shell; its own retirement clears it.
        runtime->overdubOverlayCandidate() != &set->views[set->lifecycle.build]) {
      set->views[set->lifecycle.build].resetMetadata();
    }
    const PlaybackViewPendingRequest* pending = pendingViewRequest(slot);
    if (pending != nullptr && pending->active &&
        (!(pending->source == source) || pending->coverage.startPhase != coverage.startPhase ||
         pending->coverage.lengthTicks != coverage.lengthTicks)) {
      runtime->clearViewPendingRequest();
      pending = nullptr;
    }
    if (runtime->coverageBuildNeedsService() || runtime->ownsCoverageSource() ||
        runtime->retainedGatherActive() ||
        (runtime->pagedActive && runtime->frozenViewRequest.active) || pending != nullptr) {
      dispatch = PlaybackDispatchRefusal::Retry;
      return stepCommittedPlaybackRetry(slot, clock);
    }
    const bool currentRootReady = set->lifecycle.ready < set->views.size() &&
        set->views[set->lifecycle.ready].complete &&
        set->views[set->lifecycle.ready].source == source &&
        set->views[set->lifecycle.ready].coverage.startPhase == coverage.startPhase &&
        set->views[set->lifecycle.ready].coverage.lengthTicks == coverage.lengthTicks;
    if (currentRootReady) return PlaybackViewTurnResult::Idle;
    const PlaybackViewRequestResult requested = requestCommittedPlaybackWindow(
        slot, coverage, phase, clock);
    return requested == PlaybackViewRequestResult::DeferredSourceQualification
        ? PlaybackViewTurnResult::GrainServed : PlaybackViewTurnResult::GrainDenied;
  }
  bool frozenRecordJobQualified = true;
  if (recordJob != nullptr && hasRecordReaderOwnerForSlot(slot) &&
      recordJob->slot == slot && recordJob->frozenCaptureContentGeneration != 0) {
    RuntimePlaybackViewSource frozen{};
    PlaybackViewSourceIdentity receipt{};
    frozenRecordJobQualified = qualifiedFrozenRecordCaptureSourceForSlot(
        slot, recordJob->interval, frozen, receipt);
  }
  // S7.5b.5: frozen Record-range successor preparation is served only after
  // the shared holder selector chooses this slot. The MIDI edge never builds it.
  if (!runtime->coverageBuildNeedsService() && frozenRecordJobQualified &&
      recordJob != nullptr && hasRecordReaderOwnerForSlot(slot) &&
      recordJob->slot == slot && recordJob->ownsExtent && recordJob->owner != nullptr &&
      (!recordJob->owner->publishedSnapshotIsCurrent() ||
       !runtime->hasServableDeferredRecordView()) &&
      runtime->hasBoundRecordReturnWindowForPendingJob() && !runtime->viewBuild.coverageOwned()) {
    const Loop& recordLoop = loopForSlot(slot);
    if (recordLoop.loopLengthTicks == 0) return PlaybackViewTurnResult::GrainDenied;
    // A STOPPED take resumes at phase zero: every Start from STOPPED re-anchors the projection
    // (`reanchorPlaybackProjection`), so the window owed while stopped is bar 1, not the stop phase's bar.
    // Preparing the stop-phase bar left bar 1 to be built after Start and adopted mid-window (121645).
    const uint32_t phase = isStopped() ? 0u : IntervalProjection::tickPhaseInProjectionCycle(
        currentPhase, projectionCycleStartTick, recordLoop.loopLengthTicks);
    const auto* returnSet = runtime->viewSetIfPrewarmed();
    const auto* returnCurrent = returnSet && returnSet->lifecycle.current < returnSet->views.size()
        ? &returnSet->views[returnSet->lifecycle.current] : nullptr;
    // A refreshed live interval can remain complete after a refused stop bind. Its old geometry or
    // token is not evidence that this take's first return interval is already served.
    const bool staleLiveReturn = returnCurrent && !returnCurrent->firstRecordReturnWindow &&
        returnCurrent->source.origin == PlaybackWindowOrigin::Capture &&
        (returnCurrent->coverage.loopLengthTicks != recordLoop.loopLengthTicks ||
         returnCurrent->source.captureContentGeneration != recordJob->frozenCaptureContentGeneration);
    // **DEC-136 window progression: the current window first, then forward.** A deferred view merely *existing* is not
    // coverage of the playhead: the bound first-return window can exist while the playhead's own interval has nothing
    // served -- measured on the qualification image as `adopt_wait,e2_boundary` with `start=768` while the playhead sat
    // in bar one of a 1536-tick loop, i.e. 1.7 s of silence before `RecordPublished` (`captures/i5-dec143-phase-seed-base-01`).
    // So the first window prepared is the one the playhead is **in**, and only an interval that is actually served
    // advances the demand to the next one. Never a whole-loop expansion.
    const uint32_t playheadWindowStart = (phase / Config::TICKS_PER_BAR) * Config::TICKS_PER_BAR;
    const bool playheadWindowServed = returnCurrent != nullptr && returnCurrent->complete &&
        returnCurrent->published && !returnCurrent->rawCoveragePrepared && !staleLiveReturn &&
        returnCurrent->coverage.loopLengthTicks == recordLoop.loopLengthTicks &&
        returnCurrent->coverage.contains(phase);
    // The pass's own last tick has no forward time left, so the owed successor there is the wrap's first window --
    // that is the one progression rule this change keeps from the previous form.
    const bool passEnding = phase + 1 >= recordLoop.loopLengthTicks;
    uint32_t nextBar = playheadWindowServed ? playheadWindowStart + Config::TICKS_PER_BAR
        : passEnding ? 0u
                     : playheadWindowStart;
    if (nextBar >= recordLoop.loopLengthTicks) nextBar = 0;
    PlaybackViewSourceIdentity source{
        recordLoop.playbackRevision, playbackGeneration, recordLoop.publishedSnapshotIdentity()};
    if (recordJob->owner->publishedSnapshotIsCurrent()) {
      const bool rootWindowed = PLAYBACK_WINDOW_ROLLING_ENABLED ||
          recordLoop.shouldAvoidFullVisualRebuild(recordLoop.loopLengthTicks);
      const PlaybackCoverageInterval rootCoverage{
          rootWindowed ? (phase / Config::TICKS_PER_BAR) * Config::TICKS_PER_BAR : 0u,
          rootWindowed ? std::min<uint32_t>(Config::TICKS_PER_BAR,
              recordLoop.loopLengthTicks - ((phase / Config::TICKS_PER_BAR) * Config::TICKS_PER_BAR))
                       : recordLoop.loopLengthTicks,
          recordLoop.loopLengthTicks};
      const auto* pendingRoot = pendingViewRequest(slot);
      const bool matchingRootDemand = pendingRoot && pendingRoot->active &&
          pendingRoot->sourceKind == PlaybackViewPendingRequest::SourceKind::CommittedSnapshot &&
          pendingRoot->source == source &&
          pendingRoot->coverage.startPhase == rootCoverage.startPhase &&
          pendingRoot->coverage.lengthTicks == rootCoverage.lengthTicks &&
          pendingRoot->coverage.loopLengthTicks == rootCoverage.loopLengthTicks;
      // A missing first return view still needs the ordinary root retry grains. Re-requesting the
      // same demand on every turn starves qualification even when the retained promise is admissible.
      if (runtime->ownsCoverageSource() || runtime->retainedGatherActive() || matchingRootDemand) {
        dispatch = PlaybackDispatchRefusal::Retry;
        return stepCommittedPlaybackRetry(slot, clock);
      }
      const auto requested = requestCommittedPlaybackWindow(slot, rootCoverage, phase, clock);
      return requested == PlaybackViewRequestResult::DeferredSourceQualification
          ? PlaybackViewTurnResult::GrainServed : PlaybackViewTurnResult::GrainDenied;
    }
#if PLAYBACK_WINDOW_ROLLING_ENABLED
    if (!recordJob->owner->publishedSnapshotIsCurrent() &&
        recordJob->frozenCaptureContentGeneration != 0) {
      RuntimePlaybackViewSource frozen{};
      if (!qualifiedFrozenRecordCaptureSourceForSlot(slot, recordJob->interval, frozen, source))
        return PlaybackViewTurnResult::GrainDenied;
    }
#endif
    RuntimePlaybackViewSet* set = runtime->viewSetIfPrewarmed();
    if (set == nullptr) return PlaybackViewTurnResult::GrainDenied;
    // Pass publication changes canonical currency, not this job's sealed range.
    // Keep private work/ready under its original source stamp until adoption.
    RuntimePlaybackViewSource frozen{};
    PlaybackViewSourceIdentity receipt{};
    if (qualifiedFrozenRecordCaptureSourceForSlot(slot, recordJob->interval, frozen, receipt)) {
      const auto fromFrozenJob = [&](uint8_t index) TRACK_COLD_MEM {
        if (index >= set->views.size()) return false;
        const auto& view = set->views[index];
        return !view.firstRecordReturnWindow && !view.rawCoveragePrepared &&
            view.source.snapshotIdentity == nullptr &&
#if PLAYBACK_WINDOW_ROLLING_ENABLED
            view.source.origin == PlaybackWindowOrigin::Capture &&
            view.source.captureContentGeneration == receipt.captureContentGeneration &&
#else
            view.source.origin == PlaybackWindowOrigin::Committed &&
#endif
            view.source.sourceGeneration == playbackGeneration &&
            view.coverage.loopLengthTicks == recordLoop.loopLengthTicks;
      };
      if (fromFrozenJob(set->lifecycle.ready))
        source = set->views[set->lifecycle.ready].source;
      else if (runtime->frozenWindowBuildActive() && fromFrozenJob(set->lifecycle.build))
        source = set->views[set->lifecycle.build].source;
    }
    const uint8_t buildIndex = set->lifecycle.build;
    if (runtime->frozenWindowBuildActive() && buildIndex < set->views.size() &&
        !(set->views[buildIndex].source == source)) {
      const uint32_t dropBefore = probeCoverageBuildDrop(slot);
      runtime->discardCoverageBuild();
      traceCoverageBuildDrop(slot, CoverageBuildDropReason::RecordReturnSourceSuperseded, dropBefore);
      return PlaybackViewTurnResult::GrainDenied;
    }
    if (set->lifecycle.ready < set->views.size() && set->views[set->lifecycle.ready].complete) {
      RuntimePlaybackView& ready = set->views[set->lifecycle.ready];
      // The bound bar-1 return window is served until the clock edge adopts its replacement.
      if (ready.firstRecordReturnWindow) return PlaybackViewTurnResult::Idle;
      // Preserve a ready for its own exact boundary; a view completed after
      // that edge is not the next bar's successor, even while it still covers
      // the playhead. The late view is retired below, off-clock, before the
      // same owner prepares the next owed interval.
      const RuntimePlaybackView* current = set->lifecycle.current < set->views.size()
          ? &set->views[set->lifecycle.current] : nullptr;
      const bool boundFirstBarServesPlayhead = current != nullptr &&
          current->firstRecordReturnWindow && current->complete &&
          current->source == source && current->coverage.contains(phase);
      if (ready.source == source && ready.coverage.startPhase == phase &&
          !boundFirstBarServesPlayhead) return PlaybackViewTurnResult::Idle;
      if (ready.source == source && ready.coverage.startPhase == nextBar) {
        return PlaybackViewTurnResult::Idle;
      }
      // A clock step can skip the ready interval entirely. Its start is then
      // behind this phase (or the last bar has wrapped); it missed its own edge
      // even when it no longer contains the playhead.
      const bool missedFrozenBoundary = ready.source == source &&
          ready.coverage.startPhase != phase &&
          (ready.coverage.startPhase < phase ||
           (ready.coverage.startPhase + ready.coverage.lengthTicks == recordLoop.loopLengthTicks &&
            phase < ready.coverage.startPhase));
      if (!set->lifecycle.retireSupersededReady(set->views))
        return PlaybackViewTurnResult::GrainDenied;
      if (missedFrozenBoundary)
        reportPlaybackViewMaintenanceRow(slot, loopForSlot(slot), "record_ready_late");
    }
    const bool wasReady = runtime->frozenRecordWindowReady();
    const bool progressed = runtime->serviceFrozenRecordWindow(
        recordJob->rawRange, recordJob->rawRange.generation(), source,
        recordLoop.loopLengthTicks, nextBar, Config::TICKS_PER_BAR);
    if (!progressed) {
      // D10 / task 3.9: the frozen owner's own denial is "a denied grain (whose holder now owes the work)" -- the
      // second event the receipt names -- and this branch reaches no other row. Only the **receipt** is offered here
      // (cadence-bounded inside the reporter), never a path row: a refusal can repeat on every owner turn, so a row
      // per refusal would be the flood that evicts the evidence it explains. No result or role changes.
      reportPlaybackWindowReadinessReceipt(slot, currentPhase);
      return PlaybackViewTurnResult::GrainDenied;
    }
    if (!wasReady && runtime->frozenRecordWindowReady()) {
      // D10 / task 3.9: the frozen Record-return window's completion is exactly the "a completion (whose window is
      // ready)" event the receipt names, and this branch returns before the coverage grain tail -- so it emits through
      // the same two existing emitters instead of hand-building the result. The ready role's state is unchanged.
      const PlaybackViewTurnResult completed =
          finishPlaybackViewGrain(slot, RuntimePlaybackViewBuildGrain::Complete);
      // **And the source-qualified receipt.** The row above names the slot's *committed* readiness, which for a first
      // Record take is `not_servable`: the generic reporter exits on `!loop.hasCommittedPasses()` before a frozen
      // Capture ready can be named. So on its own it cannot tell "the frozen successor completed" from "there is
      // nothing to serve". This row names the window that actually became ready -- its revision, source generation and
      // frozen content token, over the interval it covers. It is **event-shaped by construction**: the `!wasReady`
      // guard above is the completion transition, so it is reached once per completed frozen window -- the same bound
      // the `build_complete` path row already has -- and needs no shared cadence to stay bounded (which is also why it
      // can never be starved by the busy `win_readiness` phase).
      if (set != nullptr && set->lifecycle.ready < set->views.size()) {
        reportFrozenRecordReadinessReceipt(slot, currentPhase, set->views[set->lifecycle.ready]);
      }
      return completed;
    }
    return PlaybackViewTurnResult::GrainServed;
  }
  if (!runtime->coverageBuildNeedsService()) {
    dispatch = PlaybackDispatchRefusal::Retry;
      return stepCommittedPlaybackRetry(slot, clock);
  }
  if (runtime->launchPreparation == LoopPlaybackRuntime::LaunchPreparation::Building) {
    const auto* set = runtime->viewSetIfPrewarmed();
    const auto& loop = loopForSlot(slot);
    if (!set || set->lifecycle.build >= set->views.size() ||
        set->views[set->lifecycle.build].source.playbackRevision != loop.playbackRevision ||
        set->views[set->lifecycle.build].source.sourceGeneration != playbackGeneration ||
        set->views[set->lifecycle.build].coverage.loopLengthTicks != loop.loopLengthTicks ||
        !playbackWindowSourceIsCurrentForSlot(slot)) {
      cancelPlaybackLaunchForSlot(slot);
      return PlaybackViewTurnResult::GrainDenied;
    }
  }
  // One grain, and only one: the phase decides which, so a turn can never copy source entries *and* merge
  // order entries (the build contract's own rule), and the build's persistent cursor carries the progress
  // to the next turn.
  // A returned Complete is not authority after an interrupt reset/reused the role.
  RuntimePlaybackViewSet* completionSet = nullptr;
  uint8_t completionRole = RuntimePlaybackViewLifecycle::kNoView;
  uint64_t completionStorage = 0;
  uint32_t completionOperation = 0, completionEpoch = 0;
  {
    PlaybackSourceReadGuard receiptGuard;
    completionSet = runtime->viewSetIfPrewarmed();
    if (!completionSet || completionSet->lifecycle.build >= completionSet->views.size())
      return PlaybackViewTurnResult::GrainDenied;
    completionRole = completionSet->lifecycle.build;
    completionStorage = completionSet->views[completionRole].storageGeneration;
    completionOperation = runtime->viewBuildOperationGeneration;
    completionEpoch = runtime->viewBuildWork.clockEpoch;
  }
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
    // Clock sampling and deadline reporting remain outside the fixed metadata commit.
    const PlaybackDeadlineClock finished = completionEpoch != 0
        ? (completionClockReader != nullptr ? completionClockReader()
                                            : clockManager.getPlaybackDeadlineClock()) : clock;
    {
      PlaybackSourceReadGuard judgementGuard;
      if (!primaryTurn || !bridgeTurn || runtime->viewSetIfPrewarmed() != completionSet ||
          completionSet->lifecycle.build != completionRole ||
          completionSet->views[completionRole].storageGeneration != completionStorage ||
          runtime->viewBuildOperationGeneration != completionOperation ||
          !runtime->viewBuild.completedDestinationIs(completionSet->views[completionRole]))
        return PlaybackViewTurnResult::GrainDenied;
      // Completion must remain visible to deadline service after the builder becomes inactive.
      runtime->completedPromisePendingJudgement = completionEpoch != 0;
    }
    if (completionEpoch != 0) serviceAdmittedDeadlineMisses(finished);
    PlaybackSourceReadGuard publicationGuard;
    if (!primaryTurn || !bridgeTurn || runtime->viewSetIfPrewarmed() != completionSet ||
        completionSet->lifecycle.build != completionRole ||
        completionSet->views[completionRole].storageGeneration != completionStorage ||
        runtime->viewBuildOperationGeneration != completionOperation ||
        !runtime->viewBuild.completedDestinationIs(completionSet->views[completionRole]))
      return PlaybackViewTurnResult::GrainDenied;
    if (runtime->completedPromisePendingJudgement) {
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
      const auto& loop = loopForSlot(slot);
      const bool cold = coldRollingShape(*runtime, loop, playbackGeneration);
      const RuntimePlaybackView* candidate = set && set->lifecycle.build < set->views.size()
          ? &set->views[set->lifecycle.build] : nullptr;
      if (runtime->rolling.generation != playbackGeneration ||
          // C3 (DEC-116): the active slot's own term is unchanged. A **non-active** slot may publish only
          // its own cold full-loop candidate, and only while it is still an enabled sounding holder; a
          // positioned frame for a non-active slot is still cancelled here (that route is not qualified).
          (slot != activeLoopIndex &&
           !(cold && slotMayOwnColdFullLoopWindow(
                         slot, activeLoopIndex, loopsAllocated(),
                         trackManager.isSlotEnabled(resolveTrackIndexForPersistence(*this), slot), loop))) ||
          (!cold && !isPlaying()) ||
          (cold && (!candidate || !candidate->complete ||
              candidate->source.origin != PlaybackWindowOrigin::Committed ||
              candidate->source.playbackRevision != loop.playbackRevision ||
              candidate->source.sourceGeneration != playbackGeneration ||
              candidate->coverage.startPhase != 0 ||
              candidate->coverage.lengthTicks != loop.loopLengthTicks ||
              candidate->coverage.loopLengthTicks != loop.loopLengthTicks ||
              !playbackWindowSourceIsCurrentForSlot(slot) ||
              (loop.publishedSnapshotIsCurrent() &&
               candidate->source.snapshotIdentity != loop.publishedSnapshotIdentity())))) {
        cancelRollingPlaybackWindow(slot); // A stale private candidate never publishes.
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
    const uint32_t dropBefore = probeCoverageBuildDrop(slot);
    runtime->retireCoverageSource();
    traceCoverageBuildDrop(slot, CoverageBuildDropReason::GrainDeniedWhileOwned, dropBefore);
  }
  const PlaybackViewTurnResult turnResult = finishPlaybackViewGrain(slot, result);
  if (turnResult == PlaybackViewTurnResult::GrainDenied || turnResult == PlaybackViewTurnResult::BuildComplete) {
    // DEC-108 §13b / task 3.9: the **readiness receipt**, emitted where the outcome is reached -- a denied grain
    // (whose holder now owes the work) and a completion (whose window is ready) are the two events worth a row, not
    // every grain. Cadence-bounded inside the reporter, so it cannot evict what it explains.
    reportPlaybackWindowReadinessReceipt(slot, currentPhase);
  }
  return turnResult;
  };
  const auto outcome = service();
  // DEC-193 (report-only): one row per change names where the turn stopped relative to the committed retry.
  if (slot < Config::MAX_LOOPS_PER_TRACK) {
    const uint8_t dispatchTrack = playbackRebuildTrackIndex(*this);
    // +1: `Retry` is enum value 0, and the table is zero-initialised, so a raw value would make the *most important*
    // outcome indistinguishable from "never reported". Zero must mean "unseen" and nothing else.
    const uint8_t dispatchId = static_cast<uint8_t>(dispatch) + 1u;
    if (dispatchTrack < Config::NUM_TRACKS && gPlaybackDispatchLast[dispatchTrack][slot] != dispatchId) {
      gPlaybackDispatchLast[dispatchTrack][slot] = dispatchId;
      reportPlaybackViewMaintenanceRow(slot, loopForSlot(slot), playbackDispatchRefusalName(dispatch));
    }
  }
  if (isAudiblePlaybackActive()) (void)serviceCaptureStopPresentations();
  return primaryTurn && bridgeTurn ? outcome : PlaybackViewTurnResult::GrainDenied;
}

TRACK_COLD_MEM Track::OverdubOverlayDemand Track::overdubOverlayDemandForSlot(uint8_t slotIndex,
                                                                              uint32_t playheadPhase) const {
  OverdubOverlayDemand demand;
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) return demand;
  const Loop& loop = loopForSlot(slotIndex);
  demand.baseCommitted = loop.hasCommittedPasses() && loop.loopLengthTicks != 0;
  // Nothing is owed unless a live overdub is actually carrying rows, and there is a committed base to carry them over.
  if (!isOverdubbing() || loop.capture.phase != CapturePhase::Overdub || loop.capture.store.empty() ||
      !demand.baseCommitted) {
    return demand;
  }
  const uint32_t phase = playheadPhase % loop.loopLengthTicks;
  if (!playbackWindowInterval(loop.loopLengthTicks, loop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks), phase,
                              demand.coverage)) {
    return demand;
  }
  demand.owed = true;
  demand.captureGeneration = loop.captureContentGeneration();
  demand.playbackRevision = loop.playbackRevision;
  return demand;
}

TRACK_COLD_MEM OverdubOverlayLane::CandidateRetention Track::overdubOverlayRetentionForSlot(
    uint8_t slotIndex, uint32_t playheadPhase) const {
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) return {};
  const RuntimePlaybackView* candidate = runtime->overdubOverlayCandidate();
  if (candidate == nullptr) return {};
  const OverdubOverlayDemand demand = overdubOverlayDemandForSlot(slotIndex, playheadPhase);
  const PlaybackViewSourceIdentity base{demand.playbackRevision, playbackGeneration,
                                        loopForSlot(slotIndex).publishedSnapshotIdentity()};
  // One identity rule for the lane turn and the stopped oracle: the committed base identity and coverage must still be
  // the demand's, and a demand that ended leaves nothing for the candidate to answer.
  return OverdubOverlayLane::candidateRetention(candidate, demand.owed, base, demand.captureGeneration,
                                               demand.coverage);
}

/// **3.15e/2b-ii-b — the overlay lane's production owner turn.** Same per-track, off-clock visit as the NOTE_EDIT
/// preview service (`main.cpp`, the `PlaybackViewMaintenance` owner). Order is deliberate:
///
/// 1. **a boundary wins** — a pending stop, an accepted interaction or a wrap append retires the candidate and releases
///    the borrow, so the finalizer never waits for background preparation (this must be checked before the overdub state:
///    the stop edge already leaves `TRACK_OVERDUBBING`);
/// 2. **retirement before reuse** — a candidate whose committed base no longer answers the demand is retired before any
///    other owner can reinterpret the borrowed storage;
/// 3. only then observe the prerequisite (`BaseReady`), and on that answer drive **one grain** of the borrowed base
///    fill → continuation → append-stable tail. The lane never advances canonical preparation itself.
TRACK_COLD_MEM OverdubOverlayLane::Turn Track::serviceOverdubOverlayWindow() {
  using OverdubOverlayLane::Turn;
  if (!loopsAllocated()) return Turn::Idle;
  const uint8_t slot = getActiveLoopIndex();
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slot);
  if (runtime == nullptr) return Turn::Idle;
  Loop& loop = loopForSlot(slot);
  if (!loop.hasCommittedPasses() || loop.loopLengthTicks == 0) return Turn::Idle;
  // (0) **A boundary owns the turn** — a pending stop, an accepted interaction or a wrap append. This is checked
  // **before** the overdub state, because the stop edge already moves the track to `TRACK_OVERDUB_STOPPING` (or
  // PLAYING for a retargeted stop): gating on `isOverdubbing()` first would make this release unreachable for exactly
  // the finalizer it protects. Retire the candidate (its identity is about to move) and free the one companion
  // instance, so the canonical tail/oracle can never wait on this lane's background preparation.
  if (hasPendingDeferredOverdubStop() || hasAcceptedUnexecutedInteraction() ||
      loop.hasDeferredVisualCacheWrapAppend()) {
    (void)runtime->retireOverdubOverlayCandidate(); // no candidate is a no-op
    if (runtime->overdubOverlayBorrowHeld() || runtime->viewBuild.overlayContinuationActive()) {
      runtime->viewBuild.discard();
      runtime->frozenCompanionPasses.discard();
    }
    return Turn::Deferred;
  }
  // The lane exists only for a live overdub. Outside one, a leftover candidate cannot answer anything: retire it here
  // rather than leaving the stopped oracle to discover it. This also keeps the common per-track visit to one predicate.
  if (!isOverdubbing()) {
    if (runtime->overdubOverlayCandidate() != nullptr) {
      (void)runtime->retireOverdubOverlayCandidate();
      return Turn::RetiredStale;
    }
    // A session that ended without a stop job (or whose job already retired) can still leave a base half borrowed;
    // release it so the instance is never stranded outside the boundary branch.
    if (runtime->overdubOverlayBorrowHeld() || runtime->viewBuild.overlayContinuationActive()) {
      runtime->viewBuild.discard();
      runtime->frozenCompanionPasses.discard();
    }
    return Turn::Idle;
  }
  const PlaybackDeadlineClock clock = clockManager.getPlaybackDeadlineClock();
  const uint32_t tickNow = clock.epoch != 0 ? static_cast<uint32_t>(clock.tick) : clockManager.getCurrentTick();
  const uint32_t phase = IntervalProjection::tickPhaseInProjectionCycle(
      getEffectivePlaybackTick(tickNow), getProjectionCycleStartTick(), loop.loopLengthTicks);
  const OverdubOverlayDemand demand = overdubOverlayDemandForSlot(slot, phase);
  const PlaybackViewSourceIdentity baseSource{demand.playbackRevision, playbackGeneration,
                                             loop.publishedSnapshotIdentity()};

  // (1) The candidate's identity is the authority. Once stale, retire it before any other owner can reinterpret the
  // borrowed bytes as its own output.
  const OverdubOverlayLane::CandidateRetention retention = overdubOverlayRetentionForSlot(slot, phase);
  if (OverdubOverlayLane::mustRetire(retention)) {
    (void)runtime->retireOverdubOverlayCandidate();
    return Turn::RetiredStale;
  }
  if (!demand.owed || !demand.baseCommitted) {
    // The demand ended before the base half completed: release the borrow and the half-built window (a retained
    // candidate would already have been retired above).
    if (runtime->overdubOverlayBorrowHeld()) {
      runtime->viewBuild.discard();
      runtime->frozenCompanionPasses.discard();
    }
    return Turn::Idle;
  }

  OverdubOverlayLane::BuildRequest request;
  request.baseSource = baseSource;
  request.coverage = demand.coverage;
  request.captureGeneration = demand.captureGeneration;
  request.playheadPhase = phase;

  // A retained, current candidate: only the append-stable tail may move (one grain), and the build is already ours.
  if (retention.overlayCandidateRetained) {
    return runtime->stepOverdubOverlayBuild(request, loop, kPlaybackViewFillQuantum, kPlaybackViewOrderQuantum);
  }

  // (2) **The lane is already mid-build.** `observe()` is the *admission* decision; once this lane holds the borrow or
  // the append-stable continuation, the companion instance being busy is its own work, not a refusal. The marker is
  // exact: the instance is held and no other owner that uses it (canonical window/tail, capture bridge) is in flight.
  if (runtime->overdubOverlayBorrowHeld() || runtime->viewBuild.overlayContinuationActive()) {
    return runtime->stepOverdubOverlayBuild(request, loop, kPlaybackViewFillQuantum, kPlaybackViewOrderQuantum);
  }

  // (3) Observe the one prerequisite. Readiness is reported, never advanced: the committed canonical owner is the only
  // driver (`processDeferredCanonicalPrefix`), which the liveness measurement proved runs under overlay-only demand.
  OverdubOverlayLane::Observation observation;
  observation.demandOwed = demand.owed;
  observation.baseCommitted = demand.baseCommitted;
  observation.canonicalReady = runtime->canonicalPrefixPhase ==
          LoopPlaybackRuntime::CanonicalPrefixPhase::Ready &&
      runtime->canonicalPassInputs.ready() && (runtime->canonicalPrefixSource == baseSource);
  observation.committedRevision = demand.playbackRevision;
  observation.canonicalGeneration = runtime->canonicalPassInputs.generation();
  observation.captureGeneration = demand.captureGeneration;
  observation.coverage = demand.coverage;
  observation.companionAvailable = runtime->frozenCompanionPasses.idle() &&
      runtime->frozenCompanionApply.available();
  switch (OverdubOverlayLane::observe(observation)) {
    case OverdubOverlayLane::Step::NotRequired: return Turn::Idle;
    case OverdubOverlayLane::Step::WaitingCanonicalBase: return Turn::WaitingCanonicalBase;
    case OverdubOverlayLane::Step::BaseFillRefused: return Turn::BaseFillRefused;
    case OverdubOverlayLane::Step::RetireStaleCandidate:
      (void)runtime->retireOverdubOverlayCandidate();
      return Turn::RetiredStale;
    case OverdubOverlayLane::Step::BaseReady: break;
  }
  // One builder per slot: until role arbitration (2b-iii) the lane defers rather than competing for it.
  if (!runtime->overdubOverlayBuildExclusive()) return Turn::Deferred;
  RuntimePlaybackViewBuild::PreparationTurn turn(runtime->viewBuild);
  if (!turn) return Turn::Deferred;
  return runtime->stepOverdubOverlayBuild(request, loop, kPlaybackViewFillQuantum, kPlaybackViewOrderQuantum);
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

/// Task 3.15d: **the preview's interval source**. Two shapes, one answer:
///
/// * a **current published root** -- the session stores only its edit delta, so the interval's committed content is the
///   base and `applyNoteEditDeltaToCommittedWindow` applies the identity-qualified delta (the same rule the merged
///   NOTE_EDIT fallback uses, so the two readers cannot disagree while the switch is still ahead);
/// * a **rematerialized session** (no current root) -- the session's own store already holds the loop's effective
///   content, so the interval filter *is* the composition.
///
/// Returns false when the interval cannot be stated or a current root cannot be gathered; the caller then leaves the
/// lane's state untouched rather than publishing a composition it could not form. `out` is the caller's **resident**
/// scratch: the build copies it across grains, so it must outlive the request (the slot runtime owns it, and it is
/// written only when no build is active).
TRACK_COLD_MEM bool Track::composePreviewIntervalSourceForSlot(uint8_t slotIndex,
                                                              const PlaybackCoverageInterval& coverage,
                                                              SessionMidiEventVec& out) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || !coverage.isValid()) {
    return false;
  }
  Loop& loop = loopForSlot(slotIndex);
  out.clear();
  if (loop.publishedSnapshotIsCurrent()) {
    Loop::SnapshotWindowGatherPath path = Loop::SnapshotWindowGatherPath::None;
    if (!loop.tryGatherPublishedSnapshotEventsInWindow(out, coverage.startPhase, coverage.lengthTicks, &path) ||
        path != Loop::SnapshotWindowGatherPath::Snapshot) {
      out.clear();
      return false;
    }
    applyNoteEditDeltaToCommittedWindow(out, coverage, getMidiChannel());
    return true;
  }
  for (const MidiEvent& event : editManager.sessionMidiEvents()) {
    if (coverage.contains(event.tick)) {
      out.push_back(event);
    }
  }
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
  // the *current* published root; an `Overlay` carries unpublished session/pending passes and is the stale-root
  // allowance, so neither of those is served from anywhere but the root.
  //
  // A slot with **no published root at all** is the case task 3.7a opens: its source is the **retained revision**
  // (DEC-108 §10), so the interval is answered from that revision's pinned bodies, directed by the interval index --
  // and only when that authority actually holds it. Otherwise the named `SourceUnavailable` stands exactly as before,
  // and the merged-window fallback keeps serving the slot in the meantime.
  Loop::SnapshotWindowGatherPath path = Loop::SnapshotWindowGatherPath::None;
  uint32_t inspectedEntries = 0;
  scratch.clear();
  if (!loop.tryGatherPublishedSnapshotEventsInWindow(scratch, coverage.startPhase, coverage.lengthTicks, &path,
                                                     &inspectedEntries)) {
    CommittedEventRange::WindowTraversalCursor cursor{};
    CommittedEventRange::resetWindowTraversalCursor(cursor);
    // The per-layer scratch is this caller's own: the deferred lane gathers for the same slot through its own cursor
    // and its own scratch, so neither may share a buffer with the other (task 3.7a/I7).
    SessionMidiEventVec layerScratch;
    bool complete = false;
    for (uint32_t slice = 0; slice < kRetainedGatherMaxSlices && !complete; ++slice) {
      const RetainedWindowGather gathered = gatherRetainedSourceWindowForSlot(
          slotIndex, coverage.startPhase, coverage.lengthTicks, loop.playbackRevision, cursor, scratch, layerScratch);
      if (gathered.result != RetainedWindowGatherResult::Served) {
        scratch.clear();
        return PlaybackViewRequestResult::SourceUnavailable;
      }
      complete = gathered.complete;
    }
    if (!complete) {
      // The interval did not fit the gather's own slice budget: refused by name rather than handed to the admission as
      // a truncated window (the capacity bound is the admission's, not this loop's).
      scratch.clear();
      return PlaybackViewRequestResult::SourceUnavailable;
    }
  } else if (path != Loop::SnapshotWindowGatherPath::Snapshot) {
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
                                                                           uint32_t playheadPhase,
                                                                           uint32_t intervalPhase,
                                                                           PlaybackDeadlineClock clock) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return PlaybackViewRequestResult::NoRuntime;
  }
  ensureLoopsAllocated();
  if (sourceCount != 0 && source == nullptr) {
    return PlaybackViewRequestResult::NotServable;
  }
  Loop& loop = loopForSlot(slotIndex);
  if (loop.loopLengthTicks == 0) {
    return PlaybackViewRequestResult::NotServable;
  }
  // Task 3.15d: the preview reads the **same interval** the committed reader serves -- the loop's demoted shape at the
  // requested phase, resolved by the one policy (`playbackWindowInterval`), never a second geometry rule. A short loop's
  // interval is its whole loop, so the preview lane and the merged NOTE_EDIT preview agree there by construction.
  // The caller supplies the interval-qualified composition (`composePreviewIntervalSourceForSlot`); an empty
  // composition is a valid, complete preview (zero events over a real span), not a refusal.
  PlaybackCoverageInterval coverage{};
  if (!playbackWindowInterval(loop.loopLengthTicks, loop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks),
                              intervalPhase, coverage)) {
    return PlaybackViewRequestResult::NotServable;
  }
  // The **frozen source**: preview currency is the session's preview revision, and the origin marks the contract. The
  // snapshot identity is deliberately null -- this content has no published committed root, and the origin field is
  // what stops it from passing as committed currency.
  const PlaybackViewSourceIdentity identity{previewRevision, playbackGeneration, nullptr,
                                            PlaybackWindowOrigin::Preview};
  // The admission's lead is measured from the **playhead**, never from the interval's own centre: the interval can be
  // positioned ahead of the playhead (the cadence), and handing its centre in as "now" would declare almost a whole
  // loop of lead where only the distance to `coverage.startPhase` exists.
  const uint32_t phase = playheadPhase % loop.loopLengthTicks;
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

/// Task 3.7a (I3) / task 3.13: **the preview lane's production requester**, run on the focus lane's own deferred turn.
///
/// The seam is the *session's own revision*: the preview lane's currency is `sessionPlaybackPreviewRevision()`, and the
/// session bumps it when its content advances (an edit, or an in-session undo/redo) -- the `EditManager`'s deferred
/// display refresh is what flushes that advance. This caller therefore only compares, and acts on the two transitions
/// the contract names:
///
/// * the revision moved past the one this lane froze (`previewSourceActive && previewSourceRevision != revision`) --
///   the **mutation edge**: the superseded preparation is cancelled by name (`SourceMutated`, which is what makes a
///   stale completion publish nothing) and the session's new projection is requested;
/// * nothing is frozen and no preview window serves the revision -- the **first** request of the session's preview.
///
/// A session that has ended yields the source and **owes** the committed handoff; a track that is no longer the
/// selected one yields it without owing anything (a re-focus is not an exit). Both are the same two calls the exit and
/// re-focus hooks make, so a hook that did not run leaves no half-state: this is idempotent by construction.
///
/// Task 3.15d (slice 2) adds the **per-boundary re-cover cadence** and the adoption seam this requester feeds:
///
/// * the lane keeps **the playhead's own interval** serving (slice 1's geometry) and asks again **from the playhead**
///   as soon as the served window's coverage no longer contains it. A successor positioned one boundary ahead is not
///   requested: its true handoff lead is one bar, and the declared envelope (`canCompleteBeforeHandoff`) requires
///   `leadTicks` plus `grains x (denied + 1) x advance` -- it refuses that position by name, which is why the earlier
///   "two bars past the playhead" cadence was withdrawn rather than admitted on a lead it did not have;
/// * a prepared successor whose interval lies **behind** the playhead is retired without invalidating the operation
///   identity (`retireSupersededReady`), so the served frame and sibling slots keep their generation;
/// * the swap itself belongs to the clock edge (`Track::resolveNoteEditPreviewSourceForTurn`), never to this turn --
///   this caller prepares, the edge adopts;
/// * a source is cancelled (`SourceMutated`) only on an **actual** revision change, so a window that still serves its
///   own revision is never torn down by the cadence's next request, and nothing is asked while another build is in
///   flight.
TRACK_COLD_MEM void Track::servicePreviewWindowForNoteEditSession() {
  if (!loopsAllocated()) {
    return;
  }
  const uint8_t slot = getActiveLoopIndex();
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slot);
  if (runtime == nullptr) {
    return;
  }
  if (!trackManager.isSelectedTrack(*this)) {
    // Only the selected track is previewed. A leftover source is yielded by name, and a re-focus owes nothing.
    if (runtime->previewSourceActive) {
      cancelPreviewWindowForSlot(slot, PreviewWindowCancelReason::SelectedTrackChanged);
    }
    return;
  }
  if (!editManager.isNoteEditActive()) {
    // The session ended: the source yields and the committed successor becomes owed (the handoff). A source that is
    // already inactive means the exit hook performed it, and the handoff is not re-requested.
    if (runtime->previewSourceActive) {
      cancelPreviewWindowForSlot(slot, PreviewWindowCancelReason::SessionExited);
    }
    return;
  }
  const Loop& loop = loopForSlot(slot);
  if (!loop.hasCommittedPasses() || loop.loopLengthTicks == 0) {
    return;
  }
  const uint32_t previewRevision = editManager.sessionPlaybackPreviewRevision();
  if (previewRevision == 0) {
    return;
  }
  if (runtime->previewHandoffOwed) {
    // The session's exit has already yielded the source and the **committed** successor is owed. Requesting a preview
    // now would discharge that obligation (a preview request clears it), so the handoff wins: this lane has nothing to
    // prepare until the committed successor is adopted.
    return;
  }
  if (isJamPlaybackActive() || isJamming()) {
    // The clock edge does not adopt a preview under a jam (the jam owns the traversal), so a window prepared here
    // would have no reader. Spend nothing.
    return;
  }
  const PlaybackDeadlineClock clock = clockManager.getPlaybackDeadlineClock();
  const uint32_t tickNow = clock.epoch != 0 ? static_cast<uint32_t>(clock.tick) : clockManager.getCurrentTick();
  const uint32_t phase = IntervalProjection::tickPhaseInProjectionCycle(
      getEffectivePlaybackTick(tickNow), getProjectionCycleStartTick(), loop.loopLengthTicks);
  // Task 3.15d (slice 2): **one interval at a time, re-covered from the playhead.** The lane serves the interval the
  // committed route reads (slice 1's geometry), and that interval ends one bar behind the playhead, so the lane asks
  // again as soon as the served window's coverage no longer contains the playhead. The successor is therefore
  // **adopted mid-interval, at the playhead**, which is the only shape the declared service envelope admits: a
  // successor positioned one boundary ahead of the playhead would have a handoff lead of one bar, and
  // `CoverageServiceContract::canCompleteBeforeHandoff` requires `leadTicks` plus `grains x (denied + 1) x advance`
  // (2304 ticks for a three-grain window) -- it refuses that position by name (`CannotCompleteBeforeHandoff`). Asking
  // from the playhead keeps the request's declared lead honest: its interval starts behind the playhead, so the entry
  // that serves it is the next pass, `loopLength - halfWindow` away.
  //
  // Coverage, not identity, is what the terms below read: a window is "serving" only while its interval contains the
  // playhead (a readable-but-passed window is not serving).
  RuntimePlaybackViewSet* set = runtime->viewSetIfPrewarmed();
  if (set == nullptr || set->lifecycle.current >= RuntimePlaybackViewConfig::kViewCount) {
    return;
  }
  // **3.15f / C — the missed positioned handoff.** A positioned attempt stays valid across loop cycles; if its
  // absolute handoff passes while it is still the current demand, retire only that attempt and let the ordinary
  // request below position the next eligible occurrence (the search starts from the playhead, so the missed occurrence
  // cannot be chosen again). If the demand is obsolete, the mutation/exit edge owns it: retire it, never reposition it.
  const bool positionedAttempt =
      runtime->viewBuildWork.clockEpoch != 0 && runtime->viewBuildWork.positionedLater;
  if (positionedAttempt && clock.epoch == runtime->viewBuildWork.clockEpoch &&
      clock.tick > runtime->viewBuildWork.deadlineTick) {
    const bool demandStillCurrent = runtime->previewSourceActive &&
        runtime->previewSourceRevision == previewRevision;
    if (runtime->viewBuild.active()) runtime->viewBuild.discard();
    if (set->lifecycle.ready < RuntimePlaybackViewConfig::kViewCount &&
        set->views[set->lifecycle.ready].complete &&
        set->views[set->lifecycle.ready].source.origin == PlaybackWindowOrigin::Preview) {
      if (!set->lifecycle.retireSupersededReady(set->views)) {
        applyPlaybackViewTransition(slot, PlaybackViewTransition::ClearOrLoad);
      }
    }
    if (!demandStillCurrent) {
      return;  // obsolete before/at the missed boundary: retire only, never reposition the old demand
    }
  }
  const RuntimePlaybackView& current = set->views[set->lifecycle.current];
  const bool currentServesPlayhead =
      current.complete && current.source.origin == PlaybackWindowOrigin::Preview &&
      current.source.playbackRevision == previewRevision && current.coverage.isValid() &&
      current.coverage.loopLengthTicks == loop.loopLengthTicks && current.coverage.contains(phase);
  if (set->lifecycle.ready < RuntimePlaybackViewConfig::kViewCount &&
      set->views[set->lifecycle.ready].complete) {
    const RuntimePlaybackView& ready = set->views[set->lifecycle.ready];
    const bool readyIsSessionWindow =
        ready.source.origin == PlaybackWindowOrigin::Preview &&
        ready.source.playbackRevision == previewRevision && ready.coverage.isValid() &&
        ready.coverage.loopLengthTicks == loop.loopLengthTicks;
    if (!readyIsSessionWindow) {
      if (ready.source.origin == PlaybackWindowOrigin::Preview &&
          ready.source.playbackRevision != previewRevision) {
        // **3.15f / C — supersession.** This is this lane's own successor for a revision the session has left; it must
        // be retired by identity before the replacement demand is positioned, or it could hold the role and be adopted
        // after H. A foreign successor (committed/capture) is still not ours to retire.
        if (!set->lifecycle.retireSupersededReady(set->views)) {
          applyPlaybackViewTransition(slot, PlaybackViewTransition::ClearOrLoad);
        }
      } else {
        // **3.15f / C — lane arbitration.** A committed/capture successor is another owner's preparation, but while
        // NOTE_EDIT is open the session's preview is the content the transport should carry. Deferring to that
        // successor starves the preview on the shared runtime (measured on the device: committed reads throughout, no
        // preview `note_edit` build, `retained_identity_refreshed` from the committed lane). Release the role so the
        // preview can prepare; the committed lane re-prepares its successor at the session exit, where its handoff is
        // already owed.
        if (!set->lifecycle.retireSupersededReady(set->views)) {
          applyPlaybackViewTransition(slot, PlaybackViewTransition::ClearOrLoad);
        }
      }
    } else if (ready.coverage.contains(phase)) {
      return;  // the interval the clock edge adopts **at this boundary**: it must never be retired here
    }
    // Its interval is behind the playhead, so no boundary can adopt it. Retire it **without** invalidating the
    // operation identity: the served frame keeps its generation, and a sibling slot's frame is not torn by another
    // slot's edit session. The shared-view row is the fallback when the role accounting refuses the direct retire.
    if (!set->lifecycle.retireSupersededReady(set->views)) {
      applyPlaybackViewTransition(slot, PlaybackViewTransition::ClearOrLoad);
    }
  }
  if (currentServesPlayhead) {
    return;  // the lane is already serving the playhead's interval: nothing is owed until its coverage ends
  }
  if (runtime->previewSourceActive && runtime->previewSourceRevision != previewRevision) {
    // **The mutation edge.** The lane's frozen revision is gone, so the superseded preparation is cancelled by name
    // before a new one is requested -- a cancellation is what makes a stale completion publish nothing. This happens
    // whether or not the lane is busy: a build in flight for the old revision is exactly what must not land.
    cancelPreviewWindowForSlot(slot, PreviewWindowCancelReason::SourceMutated);
  }
  if (!isPlaying()) {
    // **3.15f / C — the request is a playing-transport feature.** The resolver that serves a preview frame requires
    // `isPlaying`, so a request made while stopped can only build a window nobody serves and retain a pending promise
    // that then strands the playing session (measured on the device: the stopped-open session never built a preview
    // after the transport started). The cancellations/retirement above still run; the playing turns request.
    return;
  }
  if (runtime->viewBuild.active() || runtime->ownsCoverageSource() ||
      runtime->completedPromisePendingJudgement) {
    return;  // the lane is still busy for another reason; the next seam re-checks rather than stacking a demand
  }
  if (runtime->pendingViewRequest_.active) {
    // **3.15f / C device session 3 — a foreign promise is not this lane's standing demand.** The shared pending slot
    // can hold the committed (or capture) lane's refused promise. While NOTE_EDIT is open the session's preview is the
    // content the transport should carry, and the committed handoff is re-requested at the exit; returning here would
    // strand the preview behind another source's promise (measured on the device: committed reads throughout, no
    // preview build). This lane's **own** terminal refusal still stands, but its own **"not now"** refusals
    // (`SuccessorAlreadyPrepared`, `BuildInFlight`, position invalidations, a cleared interference budget) are
    // promises the free role must re-attempt; the busy/ready guards above already return while the role is occupied.
    const PlaybackViewPendingReason reason = runtime->pendingViewRequest_.reason;
    // A retained **position** that the clock has voided (a different epoch, a changed geometry) is not a terminal
    // answer: the obligation survives and must be re-priced under the current clock. Consult the same predicate the
    // rolling lane reads, so the preview cannot outlive the clock it was priced under.
    const PlaybackViewPendingReason invalidatedPosition = pendingViewRequestInvalidation(slot, clock);
    const bool retryableOwnRefusal =
        reason == PlaybackViewPendingReason::SuccessorAlreadyPrepared ||
        reason == PlaybackViewPendingReason::BuildInFlight ||
        reason == PlaybackViewPendingReason::InterferenceBudget ||
        reason == PlaybackViewPendingReason::DeadlineInvalidatedEpoch ||
        reason == PlaybackViewPendingReason::DeadlineInvalidatedGeometry ||
        reason == PlaybackViewPendingReason::DeadlineExpired ||
        reason == PlaybackViewPendingReason::AwaitingSourceQualification ||
        invalidatedPosition != PlaybackViewPendingReason::None;
    if (runtime->pendingViewRequest_.source.origin == PlaybackWindowOrigin::Preview && !retryableOwnRefusal) {
      return;  // the lane's own terminal preview refusal stands
    }
  }
  // The requested interval is the **playhead's own** -- the one the committed route is reading -- so the edge can
  // adopt it as soon as it is complete, and the request's declared lead is the true one.
  // Task 3.15d: the lane reads the **committed route's interval**, composed with the session's delta. The composition
  // lives in the slot runtime's resident scratch because the build copies it across grains; it is written here only,
  // after the busy guard above, so a live build's source is never overwritten. An empty composition is a complete
  // preview (the interval really holds nothing after the session's edits), not a refusal.
  PlaybackCoverageInterval coverage{};
  if (!playbackWindowInterval(loop.loopLengthTicks, loop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks), phase,
                              coverage)) {
    return;
  }
  SessionMidiEventVec& composition = runtime->previewSourceEvents;
  if (!composePreviewIntervalSourceForSlot(slot, coverage, composition)) {
    return;
  }
  (void)requestPreviewWindow(slot, previewRevision, composition.data(), composition.size(), phase, phase, clock);
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
  // Task 3.15d (slice 3): **a refused preview request is still this lane's promise.** The admission refuses a request
  // it cannot complete before its handoff and *retains* it (`CannotCompleteBeforeHandoff`, `InterferenceBudget`); no
  // window was admitted, so `previewSourceActive` stays false and the early return below would leave the promise
  // behind. Its owner is the session projection, which is exactly what an exit, a mutation or a re-focus retires -- and
  // while it lives, the committed requester's own `pendingViewRequest_.active` guard answers `win_busy` behind a
  // promise nobody can keep (a short loop's whole-loop interval, or any demand whose lead is too short). Keyed on the
  // demand's **source identity**, so a committed, capture or launch promise is never touched.
  if (runtime->pendingViewRequest_.active &&
      runtime->pendingViewRequest_.source.origin == PlaybackWindowOrigin::Preview) {
    runtime->clearViewPendingRequest();
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
  // Task 3.15d (slice 2): **a frame the preview lane adopted cannot outlive its source.** The clock edge only binds a
  // `Preview` view while the session's revision is current, so from here on the seam declines the frame -- and the
  // committed reader *rejects* it, because its bound view is not a published `Committed` one; a rejected frame is
  // `Blocked`, and a blocked frame never re-requests. Ending it here, at the yield, is what lets the committed lane
  // request its successor (and discharge `previewHandoffOwed`) instead of going quiet for the rest of the play run.
  // Any other bound frame belongs to the committed or capture lane and is left exactly as it is.
  RuntimePlaybackViewSet* set = runtime->viewSetIfPrewarmed();
  if (set != nullptr && runtime->rolling.phase == LoopPlaybackRuntime::RollingPhase::Serving &&
      runtime->rolling.boundView < set->views.size() &&
      set->views[runtime->rolling.boundView].source.origin == PlaybackWindowOrigin::Preview) {
    runtime->rolling = LoopPlaybackRuntime::RollingFrame{};
    loopForSlot(slotIndex).nextEventIndex = 0;
  }
  if (reason == PreviewWindowCancelReason::SessionExited) {
    // The exit handoff: the preview source yields and the committed successor is owed. The preview window that is
    // serving right now keeps serving -- the handoff is a boundary, not a gap -- until a committed successor is
    // adopted. (Its **frame** has yielded above; the merged reader covers the traversals in between, exactly as it did
    // before this slice existed.)
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

TRACK_COLD_MEM bool Track::previewWindowSourceActiveForSlot(uint8_t slotIndex) const {
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  return runtime != nullptr && runtime->previewSourceActive;
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

/// Task 3.15g: the root-return adoption mask and answer, on the frozen-readiness receipt. Read-only observations of
/// the same terms the E2 adoption edge checks, so one device occurrence says *which* one refused. `DMAMEM` cadence state
/// (RAM2, off the clock path) holds the last answer per slot: a row costs a transition, not a turn.
DMAMEM static uint16_t sRootReturnAdoptionSignature[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK];
/// Task 3.15g: the hold's **lane** state, its own cadence (the paged/gather outcome that can repaint a demand's reason).
DMAMEM static uint8_t sRootReturnLaneSignature[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK];
DMAMEM static bool sRootReturnLaneInitialised = false;
DMAMEM static bool sRootReturnAdoptionInitialised = false;

TRACK_COLD_MEM void Track::reportRootReturnAdoptionForSlot(uint8_t slotIndex, const Loop& loop, uint32_t phase,
                                                          uint64_t frozenCaptureToken, bool attempted, bool adopted,
                                                          uint8_t why, bool fromRetirementHold) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || !loopsAllocated()) return;
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) return;
  RuntimePlaybackViewSet* set = runtime->viewSetIfPrewarmed();
  const auto& frame = runtime->rolling;
  // One lookup per role, then plain tests: the same eight terms, written so this cold helper stays small (its first
  // form cost 64 B of ITCM and pushed the qualification padding under the floor).
  const auto* cur = (set != nullptr && frame.boundView < set->views.size()) ? &set->views[frame.boundView] : nullptr;
  const auto* ready = (set != nullptr && set->lifecycle.ready < set->views.size())
      ? &set->views[set->lifecycle.ready] : nullptr;
  uint8_t mask = 0;
  if (cur != nullptr && cur->source.origin == PlaybackWindowOrigin::Capture &&
      cur->source.sourceGeneration == playbackGeneration &&
      cur->source.captureContentGeneration == frozenCaptureToken && cur->source.snapshotIdentity == nullptr &&
      cur->coverage.loopLengthTicks == loop.loopLengthTicks)
    mask |= 1u << 0;
  if (loop.publishedSnapshotIsCurrent()) mask |= 1u << 1;
  if (ready != nullptr) mask |= 1u << 2;
  if (ready != nullptr && ready->complete) mask |= 1u << 3;
  if (ready != nullptr && ready->complete && ready->source.origin == PlaybackWindowOrigin::Committed &&
      ready->source.playbackRevision == loop.playbackRevision &&
      ready->source.sourceGeneration == playbackGeneration &&
      ready->source.snapshotIdentity == loop.publishedSnapshotIdentity() &&
      ready->coverage.loopLengthTicks == loop.loopLengthTicks)
    mask |= 1u << 4;
  if (ready != nullptr && ready->complete && ready->coverage.startPhase == phase) mask |= 1u << 5;
  if (ready != nullptr && ready->coverage.loopLengthTicks == loop.loopLengthTicks) mask |= 1u << 6;
  if (cur != nullptr && frame.phase == LoopPlaybackRuntime::RollingPhase::Serving &&
      frame.generation == playbackGeneration && frame.loopLength == loop.loopLengthTicks && frame.loopLength != 0 &&
      frame.boundView == set->lifecycle.current &&
      (uint32_t(getProjectionCycleStartTick()) - uint32_t(frame.projectionStart)) % frame.loopLength == 0)
    mask |= 1u << 7;
  const uint8_t trackIndex = resolveTrackIndexForPersistence(*this);
  if (trackIndex >= Config::NUM_TRACKS) return;
  if (!sRootReturnAdoptionInitialised) {
    for (uint8_t t = 0; t < Config::NUM_TRACKS; ++t)
      for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) sRootReturnAdoptionSignature[t][s] = 0xFFFFu;
    sRootReturnAdoptionInitialised = true;
  }
  // The signature deliberately excludes bit5 (the ready view's start phase against the playhead): the two observing
  // edges sample at different instants, so including it made them alternate forever -- two rows a turn, the cadence trap
  // the authoring guide names. Every other term is shared, so a genuine change still costs exactly one row.
  const uint16_t signature = static_cast<uint16_t>((fromRetirementHold ? 0x1000u : 0u) |
                                                   (static_cast<uint16_t>(mask & 0xDFu) << 4) |
                                                   (adopted ? 2u : (attempted ? 1u : 0u)) |
                                                   (fromRetirementHold
                                                        ? static_cast<uint16_t>(
                                                              ((static_cast<unsigned>(runtime->pendingViewRequest_.reason) &
                                                                0x0Fu) << 9) |
                                                              (runtime->pendingViewRequest_.clockEpoch == 0 ? 0x200u : 0u))
                                                        : 0u));
  if (sRootReturnAdoptionSignature[trackIndex][slotIndex] == signature) return;
  sRootReturnAdoptionSignature[trackIndex][slotIndex] = signature;
  const char* answer = adopted ? "adopted" : (attempted ? "adopt_refused" : "adopt_wait");
  // Task 3.15g: a `retire_hold` row has no adoption to report, so its `why` field carries the **demand's own position
  // stamp** instead -- bit0 the record is active, bits 1-4 its reason (SourceNotCurrent is 7), bit5 clockEpoch == 0
  // (unpositioned in time), bit6 deadlineTick == 0 (no promised instant). That is the fact the board's stall turns on,
  // and it costs no call site and no literal.
  uint8_t reasonCode = why;
  if (fromRetirementHold) {
    const auto& demand = runtime->pendingViewRequest_;
    reasonCode = static_cast<uint8_t>((demand.active ? 1u : 0u) |
                                      ((static_cast<unsigned>(demand.reason) & 0x0Fu) << 1) |
                                      (demand.clockEpoch == 0 ? 0x20u : 0u) |
                                      (demand.deadlineTick == 0 ? 0x40u : 0u));
  }
  // Task 3.15g: while the hold is the observing edge, a **second** row carries the lane's own state -- the paged
  // outcome and the retained-gather refusal, four bits each -- because a sticky outcome there can repaint a demand's
  // reason without the demand's own content justifying it. Its own cadence, so it cannot ping-pong with the first.
  if (fromRetirementHold) {
    const uint8_t lane = static_cast<uint8_t>((static_cast<unsigned>(runtime->pagedOutcome) & 0x0Fu) |
                                              ((static_cast<unsigned>(runtime->retainedGatherRefusal) & 0x0Fu) << 4));
    // A zero-initialised array would read "lane 0" as an unchanged answer and never report it; 0xFF means "nothing
    // reported yet", so the first observation always costs one row.
    if (!sRootReturnLaneInitialised) {
      for (uint8_t t = 0; t < Config::NUM_TRACKS; ++t)
        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) sRootReturnLaneSignature[t][s] = 0xFFu;
      sRootReturnLaneInitialised = true;
    }
    if (sRootReturnLaneSignature[trackIndex][slotIndex] != lane) {
      sRootReturnLaneSignature[trackIndex][slotIndex] = lane;
      // The four remaining gate terms and the frame's own phase ride the two interval fields of this diagnostic-only
      // row (`start`/`length`): `start` is the packed byte, `length` is the rolling phase. A `hold_lane` row is not a
      // readiness receipt about an interval, so those two fields are free here — and this is the read that decides
      // whether the demand or the frame is the blocker.
      uint32_t gates = 0;
      if (runtime->coverageRoleAvailable()) gates |= 1u << 0;
      if (playbackViewOutstandingGrains(slotIndex) != 0) gates |= 1u << 1;
      if (playbackWindowSourceIsCurrentForSlot(slotIndex)) gates |= 1u << 2;
      if (runtime->pendingViewRequest_.source.playbackRevision == loop.playbackRevision) gates |= 1u << 3;
      gates |= (static_cast<uint32_t>(runtime->rolling.phase) & 0x7u) << 4;
      if (runtime->rolling.epoch == clockManager.getPlaybackDeadlineClock().epoch) gates |= 1u << 7;
      // Bits 8-15: the **current-root handoff** block's own entry terms (read-only), so one board occurrence says
      // whether that block is entered and, if not, which term stops it. Bit numbering is documented here and nowhere
      // else: 8 job owns its raw range, 9 a servable deferred record view exists, 10 the slot is audible, 11 a coverage
      // build needs service, 12 the served view is a complete same-revision copy containing the playhead, 13 that served
      // copy is already the current root's own window, 14 the root is current, 15 the return window is bound.
      const CaptureStopJob* heldJob = captureStopJobHead();
      const uint32_t phaseNow = loop.loopLengthTicks > 0
          ? IntervalProjection::tickPhaseInProjectionCycle(
                static_cast<uint32_t>(clockManager.getCurrentTick()), getProjectionCycleStartTick(),
                loop.loopLengthTicks)
          : 0;
      const RuntimePlaybackView* served = set != nullptr && set->lifecycle.current < set->views.size()
          ? &set->views[set->lifecycle.current] : nullptr;
      if (heldJob != nullptr && heldJob->ownsRawRange()) gates |= 1u << 8;
      if (runtime->hasServableDeferredRecordView()) gates |= 1u << 9;
      if (isAudiblePlaybackActive()) gates |= 1u << 10;
      if (runtime->coverageBuildNeedsService()) gates |= 1u << 11;
      if (served != nullptr && served->complete &&
          served->source.playbackRevision == loop.playbackRevision &&
          served->source.sourceGeneration == playbackGeneration && served->coverage.contains(phaseNow))
        gates |= 1u << 12;
      if (served != nullptr && served->source.origin == PlaybackWindowOrigin::Committed &&
          served->source.snapshotIdentity == loop.publishedSnapshotIdentity())
        gates |= 1u << 13;
      if (loop.publishedSnapshotIsCurrent()) gates |= 1u << 14;
      if (runtime->hasBoundRecordReturnWindowForPendingJob()) gates |= 1u << 15;
      // The **offer** the lane makes for this slot, packed into `length`: bits 0-2 rolling phase (as before), bit3
      // eligible, bit4 canProgress, bit5 hasDeadline, bits 6-11 the lead in 16-tick units (capped). This is the last
      // distinction the desk cannot make -- whether the lane declines to offer the slot or offers it and the owner turn
      // does nothing -- and it needs no new call site.
      const auto offer = playbackWindowServiceOffer(
          static_cast<uint32_t>(clockManager.getCurrentTick()), clockManager.getPlaybackDeadlineClock());
      const uint32_t leadUnits = (offer.handoffLeadTicks / 16u) > 63u ? 63u : (offer.handoffLeadTicks / 16u);
      const uint32_t offerPacked = (static_cast<uint32_t>(runtime->rolling.phase) & 0x7u) |
                                   (offer.eligible ? 1u << 3 : 0u) |
                                   (offer.canProgress ? 1u << 4 : 0u) |
                                   (offer.hasDeadline ? 1u << 5 : 0u) |
                                   (leadUnits << 6);
      // The **frozen** request's own state rides `result` (free on this label): bit0 active, bit1 CommittedSnapshot,
      // bit2 its identity names the live root, bit3 positionInvalidated, bits 4-7 its reason. After
      // `beginCoverageQualification` freezes the retained demand, this is the record that actually carries the work --
      // the pending stamp alone cannot say whether the chain moved or stopped.
      const auto& frozenReq = runtime->frozenViewRequest;
      uint32_t frozenPack = (frozenReq.active ? 1u : 0u) |
                            (frozenReq.sourceKind == PlaybackViewPendingRequest::SourceKind::CommittedSnapshot ? 2u : 0u) |
                            (frozenReq.source.playbackRevision == loop.playbackRevision ? 4u : 0u) |
                            (frozenReq.positionInvalidated ? 8u : 0u) |
                            ((static_cast<uint32_t>(frozenReq.reason) & 0x0Fu) << 4);
      SC_FROZEN_WINDOW_READINESS(trackIndex, slotIndex, "hold_lane", "retire_hold", loop.playbackRevision, 0u,
                                 playbackGeneration, frozenCaptureToken, gates, offerPacked, loop.loopLengthTicks, mask,
                                 frozenPack, lane);
    }
  }
  SC_FROZEN_WINDOW_READINESS(trackIndex, slotIndex, answer,
                             fromRetirementHold ? "retire_hold" : "e2_boundary", loop.playbackRevision,
                             0u, playbackGeneration, frozenCaptureToken,
                             ready != nullptr ? ready->coverage.startPhase : 0u,
                             ready != nullptr ? ready->coverage.lengthTicks : 0u, loop.loopLengthTicks, mask,
                             adopted ? 1u : (attempted ? 2u : 0u), reasonCode);
}

#if defined(SESSION_CAPTURE)
DMAMEM static uint32_t sFirstBarRouteSignature[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK];
DMAMEM static bool sFirstBarRouteInitialised = false;
#endif

TRACK_COLD_MEM void Track::reportFirstBarRouteForSlot(uint8_t slotIndex, uint32_t currentTick, uint8_t route,
                                                      const RuntimePlaybackView* view) {
#if defined(SESSION_CAPTURE)
  if (slotIndex != activeLoopIndex || slotIndex >= Config::MAX_LOOPS_PER_TRACK || !loopsAllocated() ||
      !trackManager.isSelectedTrack(*this)) return;
  const Loop& loop = loopForSlot(slotIndex);
  if (loop.loopLengthTicks == 0) return;
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  const RuntimePlaybackViewSet* set = runtime != nullptr ? runtime->viewSetIfPrewarmed() : nullptr;
  const uint32_t phase = IntervalProjection::tickPhaseInProjectionCycle(
      currentTick, projectionCycleStartTick, loop.loopLengthTicks);
  uint8_t viewIndex = 0x0Fu;
  if (set != nullptr && view != nullptr)
    for (uint8_t i = 0; i < set->views.size(); ++i)
      if (&set->views[i] == view) viewIndex = i;
  const RuntimePlaybackView* ready = set != nullptr && set->lifecycle.ready < set->views.size()
      ? &set->views[set->lifecycle.ready] : nullptr;
  const uint8_t rolling = runtime != nullptr ? static_cast<uint8_t>(runtime->rolling.phase) & 0x7u : 0x7u;
  const uint32_t signature = ((phase / Config::TICKS_PER_BAR) & 0xFFu) | (static_cast<uint32_t>(route & 0xFu) << 8) |
      (static_cast<uint32_t>(viewIndex) << 12) |
      ((view != nullptr && view->storage.eventCount == 0) ? (1u << 16) : 0u) |
      (static_cast<uint32_t>(set != nullptr ? set->lifecycle.ready & 0xFu : 0xFu) << 17) |
      ((ready != nullptr && ready->complete) ? (1u << 21) : 0u) | (static_cast<uint32_t>(rolling) << 22);
  const uint8_t trackIndex = resolveTrackIndexForPersistence(*this);
  if (trackIndex >= Config::NUM_TRACKS) return;
  if (!sFirstBarRouteInitialised) {
    for (uint8_t t = 0; t < Config::NUM_TRACKS; ++t)
      for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) sFirstBarRouteSignature[t][s] = 0xFFFFFFFFu;
    sFirstBarRouteInitialised = true;
  }
  if (sFirstBarRouteSignature[trackIndex][slotIndex] == signature) return;
  sFirstBarRouteSignature[trackIndex][slotIndex] = signature;
  uint8_t mask = 0;
  if (view != nullptr && view->firstRecordReturnWindow) mask |= 1u << 0;
  if (view != nullptr && view->source.origin == PlaybackWindowOrigin::Capture) mask |= 1u << 1;
  if (view != nullptr && view->published) mask |= 1u << 2;
  if (view != nullptr && view->rawCoveragePrepared) mask |= 1u << 3;
  if (ready != nullptr && ready->complete) mask |= 1u << 4;
  if (runtime != nullptr && runtime->hasBoundRecordReturnWindowForPendingJob()) mask |= 1u << 5;
  if (captureStopPresentationForSlot(slotIndex) != nullptr) mask |= 1u << 6;
  if (loop.hasCommittedPasses()) mask |= 1u << 7;
  // start = served view's start (or the ready's | 0x80000000 when no view), length = served view's event count.
  const uint32_t start = view != nullptr ? view->coverage.startPhase
      : ready != nullptr ? (0x80000000u | ready->coverage.startPhase) : 0xFFFFFFFFu;
  SC_FROZEN_WINDOW_READINESS(trackIndex, slotIndex, "route", "turn", loop.playbackRevision, phase,
                             playbackGeneration, view != nullptr ? view->source.captureContentGeneration : 0u, start,
                             view != nullptr ? static_cast<uint32_t>(view->storage.eventCount) : 0u,
                             loop.loopLengthTicks, mask, route, (viewIndex << 4) | rolling);
#else
  (void)slotIndex;
  (void)currentTick;
  (void)route;
  (void)view;
#endif
}

TRACK_COLD_MEM void Track::reportRootReturnHoldForSlot(uint8_t slotIndex) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || !loopsAllocated()) return;
  const CaptureStopJob* job = captureStopJobHead();
  if (job == nullptr || job->mode != CapturePhase::Record) return;
  reportRootReturnAdoptionForSlot(slotIndex, loopForSlot(slotIndex), 0u, job->frozenCaptureContentGeneration, false,
                                  false, 0, true);
}

TRACK_COLD_MEM bool Track::adoptPreparedPlaybackWindow(uint8_t slotIndex, uint8_t* refusalOut) {
  if (refusalOut != nullptr) *refusalOut = 0;
  PlaybackSourceReadGuard publicationGuard;
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    if (refusalOut != nullptr) *refusalOut = 1;
    return false;
  }
  RuntimePlaybackViewSet* set = runtime->viewSetIfPrewarmed();
  if (set == nullptr) {
    if (refusalOut != nullptr) *refusalOut = 2;
    return false;
  }
  // **3.15f / C — the ready's positioned handoff is a property of the handoff, not of one caller.** Every adoption
  // route (the preview resolver and the committed/rolling lane) must respect the tick the ready was positioned for;
  // otherwise a boundary that belongs to another lane can adopt it early. Unpositioned ready windows are unaffected.
  if (runtime->readyPositionedLater && runtime->readyPositionedClockEpoch != 0) {
    const PlaybackDeadlineClock now = clockManager.getPlaybackDeadlineClock();
    if (now.epoch != runtime->readyPositionedClockEpoch || now.tick < runtime->readyPositionedDeadlineTick) {
      if (refusalOut != nullptr) *refusalOut = 7;
      return false;
    }
  }
  // Task 3.15g: name the lifecycle's own refusal terms **read-only** before the atomic handoff, so the receipt can say
  // which one stands. The handoff below is unchanged; these reads add no state and no branch on its result.
  uint8_t readable = 0;
  if (set->lifecycle.current >= set->views.size()) readable = 3;
  else if (set->lifecycle.ready >= set->views.size()) readable = 4;
  else if (!set->views[set->lifecycle.ready].complete) readable = 5;
  // The successor lane's adoption: one atomic handoff (`RuntimePlaybackViewLifecycle::handoff`), prepared-mode
  // because a window carries no audible-launch semantics of its own. It refuses unless a **complete** successor
  // is waiting, so a caller can poll it safely.
  if (!runtime->readyPositionAllowsAdoption(clockManager.getPlaybackDeadlineClock()) ||
      !set->handoffReadyCoverage(PlaybackViewHandoffMode::Preparation)) {
    if (refusalOut != nullptr) *refusalOut = readable != 0 ? readable : 6;
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

TRACK_COLD_MEM PlaybackRevalidateRefusal Track::revalidateCompletedPlaybackWindowRefusal(
    uint8_t slotIndex, uint32_t currentPhase, PlaybackDeadlineClock clock) const {
  // One implementation of the preconditions: the verdict and the reported term cannot disagree.
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) return PlaybackRevalidateRefusal::BuildSlotOutOfRange;
  if (clock.epoch == 0) return PlaybackRevalidateRefusal::NoEpoch;
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) return PlaybackRevalidateRefusal::NoRuntime;
  if (!runtime->completedPromisePendingJudgement) return PlaybackRevalidateRefusal::NotPending;
  if (runtime->coverageBuildNeedsService()) return PlaybackRevalidateRefusal::NeedsService;
  const RuntimePlaybackViewSet* set = runtime->viewSetIfPrewarmed();
  if (set == nullptr) return PlaybackRevalidateRefusal::NoViewSet;
  if (set->lifecycle.build >= RuntimePlaybackViewConfig::kViewCount)
    return PlaybackRevalidateRefusal::BuildSlotOutOfRange;
  if (runtime->viewBuildOperationGeneration != playbackGeneration) return PlaybackRevalidateRefusal::GenerationMoved;
  const RuntimePlaybackView& candidate = set->views[set->lifecycle.build];
  const Loop& loop = loopForSlot(slotIndex);
  const auto& work = runtime->viewBuildWork;
  if (!candidate.complete) return PlaybackRevalidateRefusal::NotComplete;
  if (!candidate.coverage.isValid()) return PlaybackRevalidateRefusal::InvalidCoverage;
  if (loop.loopLengthTicks != candidate.coverage.loopLengthTicks)
    return PlaybackRevalidateRefusal::LengthMismatchCandidate;
  if (loop.loopLengthTicks != work.admittedLoopLength) return PlaybackRevalidateRefusal::LengthMismatchWork;
  if (currentPhase >= loop.loopLengthTicks) return PlaybackRevalidateRefusal::PhaseOutOfRange;
  if (currentPhase != candidate.coverage.startPhase) return PlaybackRevalidateRefusal::PhaseNotBoundary;
  if (candidate.source.origin == PlaybackWindowOrigin::Committed) {
    if (!playbackWindowSourceIsCurrentForSlot(slotIndex)) return PlaybackRevalidateRefusal::SourceNotCurrent;
    if (candidate.source.playbackRevision != loop.playbackRevision) return PlaybackRevalidateRefusal::SourceRevision;
    if (candidate.source.snapshotIdentity != loop.publishedSnapshotIdentity())
      return PlaybackRevalidateRefusal::SourceIdentity;
  } else {
    if (!runtime->previewSourceActive) return PlaybackRevalidateRefusal::PreviewInactive;
    if (candidate.source.playbackRevision != runtime->previewSourceRevision)
      return PlaybackRevalidateRefusal::PreviewRevision;
  }
  // In the original epoch a missed traversal remains fixed to its old revision: recovery may only use a following
  // entry, never the just-missed one. A new epoch has no positional relationship: the caller supplies the boundary phase.
  if (clock.epoch == work.clockEpoch) {
    if (runtime->admittedPromiseInvalidated) return PlaybackRevalidateRefusal::PromiseInvalidated;
    if (clock.tick < work.admittedTick) return PlaybackRevalidateRefusal::ClockBeforeAdmitted;
    if (work.deadlineTick > UINT64_MAX - work.admittedLoopLength)
      return PlaybackRevalidateRefusal::DeadlineOverflow;
    if (clock.tick < work.deadlineTick + work.admittedLoopLength) return PlaybackRevalidateRefusal::TooEarly;
    if ((clock.tick - work.deadlineTick) % work.admittedLoopLength != 0)
      return PlaybackRevalidateRefusal::NotAligned;
  }
  return PlaybackRevalidateRefusal::None;
}

TRACK_COLD_MEM bool Track::revalidateCompletedPlaybackWindow(uint8_t slotIndex, uint32_t currentPhase,
                                                             PlaybackDeadlineClock clock) {
  if (revalidateCompletedPlaybackWindowRefusal(slotIndex, currentPhase, clock) != PlaybackRevalidateRefusal::None)
    return false;
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  runtime->completedPromisePendingJudgement = false;
  if (!publishCompletedPlaybackWindow(slotIndex)) {
    runtime->completedPromisePendingJudgement = true;
    return false;
  }
  // Recovery does not erase fault history; the boundary consumer owns adoption and clearing a recovered episode. The
  // admitted work record remains unchanged.
  return true;
}

/// **DEC-202 -- the late-completion judge-or-release turn.** See the declaration for the contract; the order is:
///  1. nothing pending, or work still needing service -> nothing to do;
///  2. the revalidation is **legal now** -> judge it (publish). This is the only place the recovery path is reached, and
///     only when every precondition that names a boundary says so;
///  3. otherwise **release** the completion: discard its candidate, reset the build role to a shell, clear the flag and
///     the operation generation, and name the drop. The slot can build its next window on the next demand.
/// The failure this repairs is a deadlock, not a timing miss: a completion that arrives after its own window leaves
/// `completedPromisePendingJudgement` set, `coverageRoleRefusal` answers `role_promise_pending`, the gate refuses
/// `gate_no_role`, no holder is served, and nothing in the firmware ever clears it
/// (`test_dec193_a_restarted_rootless_slot_keeps_owing_a_build_that_is_never_served`).
TRACK_COLD_MEM bool Track::retireUnjudgeableCompletedPromise(uint8_t slotIndex, uint32_t currentPhase,
                                                            PlaybackDeadlineClock clock) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) return false;
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr || !runtime->completedPromisePendingJudgement) return false;
  if (runtime->coverageBuildNeedsService()) return false;
  if (revalidateCompletedPlaybackWindowRefusal(slotIndex, currentPhase, clock) == PlaybackRevalidateRefusal::None) {
    // Legal boundary: the existing recovery path owns it (and re-sets the flag if its publication refuses).
    return !revalidateCompletedPlaybackWindow(slotIndex, currentPhase, clock);
  }
  RuntimePlaybackViewSet* set = runtime->viewSetIfPrewarmed();
  const uint32_t dropBefore = probeCoverageBuildDrop(slotIndex);
  if (set != nullptr && set->lifecycle.build < set->views.size()) {
    RuntimePlaybackView& dropped = set->views[set->lifecycle.build];
    if (runtime->viewBuild.completedDestinationIs(dropped)) {
      // Keep the destination's generation (another owner may be reading it); only the *candidate* goes.
      runtime->viewBuild.discard(&dropped);
      dropped.resetMetadata();
    }
  }
  runtime->completedPromisePendingJudgement = false;
  runtime->viewBuildOperationGeneration = 0;
  traceCoverageBuildDrop(slotIndex, CoverageBuildDropReason::CompletionUnjudgeable, dropBefore);
  return true;
}

TRACK_COLD_MEM bool Track::publishCompletedPlaybackWindow(uint8_t slotIndex) {
  PlaybackSourceReadGuard publicationGuard;
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
  if (completedBuildRole >= set->views.size() ||
      !runtime->viewBuild.completedDestinationIs(set->views[completedBuildRole])) return false;
  if (completedBuildRole < RuntimePlaybackViewConfig::kViewCount &&
      runtime->viewBuildOperationGeneration != playbackGeneration) {
    {
      const uint32_t dropBefore = probeCoverageBuildDrop(slotIndex);
      runtime->viewBuild.discard(); // Only its generation-qualified private destination.
      traceCoverageBuildDrop(slotIndex, CoverageBuildDropReason::CompletionAdjudicated, dropBefore);
    }
    runtime->viewBuildOperationGeneration = 0;
    return false;
  }
  PlaybackWindowIdentity published{};
  if (publishedPlaybackWindowIdentityForSlot(slotIndex, published)) {
    // A window is being served: this completed build becomes its **ready successor**, and the served window is
    // left exactly as it was -- the replacement's swap is `adoptPreparedPlaybackWindow`'s single handoff, at an
    // eligible boundary (task 3.7). That is F-1's "a consumer read cannot straddle a replacement".
    const uint32_t publishBefore = probeCoverageBuildDrop(slotIndex);
    const bool publishedReady = set->lifecycle.publishCompletedBuildAsReadySuccessor(set->views);
    if (publishedReady) runtime->viewBuild.discard(&set->views[completedBuildRole]);
    traceCoverageBuildDrop(slotIndex, publishedReady ? CoverageBuildDropReason::PublishedAsReady
                                                    : CoverageBuildDropReason::PublishReadyRefused,
                           publishBefore);
    return publishedReady;
  }
  if (!runtime->previewSourceActive) {
    // Task 3.13: with **no** window being served, this committed publication becomes the served window itself, so the
    // preview exit handoff is discharged here. (When a preview window *is* served, the discharge belongs to the
    // adoption boundary instead: the preview window keeps serving until the committed successor replaces it.)
    runtime->previewHandoffOwed = false;
  }
  const uint32_t publishBefore = probeCoverageBuildDrop(slotIndex);
  const bool publishedCurrent = set->lifecycle.publishCompletedBuildAsCurrentWindow(set->views);
  if (publishedCurrent) runtime->viewBuild.discard(&set->views[completedBuildRole]);
  traceCoverageBuildDrop(slotIndex, publishedCurrent ? CoverageBuildDropReason::PublishedAsCurrent
                                                    : CoverageBuildDropReason::PublishCurrentRefused,
                         publishBefore);
  return publishedCurrent;
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
    if (!runtime->ledger.isActive(channel, note)) {
      // "Closures that became obsolete SHALL be suppressed": the obligation is already gone, because the owner that
      // planned it closed it first (task 4.5's moving closure is planned and discharged before this settlement is
      // applied). Emitting anyway would put a second, orphan NoteOff on the wire for the same sounding.
      continue;
    }
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

/// Task 4.5 (the moving closure): the phase this slot has already **served**, which is the term that decides whether an
/// edit's own NoteOff can still be reached before the pass restarts. The merged walk's cursor is the first source
/// (`playbackClosureServeFloor`, the handoff's own term); a **demoted** slot is served by interval windows rather than
/// by that walk, so its projection phase is the fallback. `UINT32_MAX` means nothing is served -- stopped, not the
/// playing slot, or no position -- and then no edit can make an end "behind" anything.
TRACK_COLD_MEM uint32_t Track::playbackClosureFloorForSlot(uint8_t slotIndex) const {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || !loopsAllocated()) {
    return UINT32_MAX;
  }
  const Loop& loop = loopForSlot(slotIndex);
  if (loop.loopLengthTicks == 0) {
    return UINT32_MAX;
  }
  const uint32_t served = playbackClosureServeFloor(*this, loop, clockManager.getCurrentTick());
  if (served != UINT32_MAX) {
    return served;
  }
  if (!isPlaying() || slotIndex != activeLoopIndex) {
    return UINT32_MAX;
  }
  return IntervalProjection::tickPhaseInProjectionCycle(
      clockManager.getCurrentTick(), getProjectionCycleStartTick(), loop.loopLengthTicks);
}

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
  //
  // Task 4.5 (the moving closure), **before** the presence settlement: an edit that keeps a sounding note but moves its
  // own NoteOff behind the phase this pass has already served ends the sounding instance now. That tick can never be
  // reached again before the pass restarts, and on a demoted slot the merged whole-loop reconcile that would otherwise
  // catch it never runs -- its own store is a two-bar slice. The predicate is the ledger's phase half over the batch's
  // own rows, so an identity the batch does not mention is untouched (an edit batch is not whole-loop authority).
  const uint32_t closureFloor = playbackClosureFloorForSlot(slotIndex);
  if (closureFloor != UINT32_MAX) {
    for (const EditPassId id : passIds) {
      for (const EditPass& pass : loop.passes.editPasses) {
        if (pass.id != id) {
          continue;
        }
        const ActiveNoteLedger::HandoffClosurePlan expired =
            runtime->ledger.planExpiredClosures(pass.addedEvents.data(), pass.addedEvents.size(), closureFloor,
                                                loop.loopLengthTicks);
        for (size_t i = 0; i < expired.count && settlement.closureCount < ClosureSettlement::kMaxKeys; ++i) {
          const uint16_t channel = static_cast<uint16_t>(expired.entries[i].channel) << 8;
          settlement.closures[settlement.closureCount++] =
              static_cast<uint16_t>(channel | expired.entries[i].note);
        }
        (void)closePlannedHandoffClosures(runtime->ledger, expired, clockManager.getCurrentTick());
        break;
      }
    }
  }
  const ClosureSettlement presence = settleLedgerAgainstPresence(runtime->ledger, affected, identitiesAreRemoved);
  // One settlement for the caller: the closures named above (already discharged on the wire) plus the presence walk's
  // own verdict over what is left. A closed entry is gone from the ledger, so the two halves cannot name the same key.
  for (size_t i = 0; i < presence.closureCount && settlement.closureCount < ClosureSettlement::kMaxKeys; ++i) {
    settlement.closures[settlement.closureCount++] = presence.closures[i];
  }
  for (size_t i = 0;
       i < presence.suppressedRetriggerCount && settlement.suppressedRetriggerCount < ClosureSettlement::kMaxKeys; ++i) {
    settlement.suppressedRetriggers[settlement.suppressedRetriggerCount++] = presence.suppressedRetriggers[i];
  }
  settlement.preservedCount = static_cast<uint16_t>(settlement.preservedCount + presence.preservedCount);
  settlement.eventEmptyWindow = presence.eventEmptyWindow;
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

TRACK_COLD_MEM void Track::observePlaybackSnapshotAvailability(uint8_t slotIndex, uint32_t tick) {
#if SC_PLAYBACK_READ_WITNESS_ENABLED
  if (!playbackEmitMidiOutput_ || !isAudiblePlaybackActive() || mergedMidiEventsBuildInProgress ||
      slotIndex >= Config::MAX_LOOPS_PER_TRACK) return;
  const auto* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  const Loop& loop = loopForSlot(slotIndex);
  if (runtime == nullptr || !loop.publishedSnapshotIsCurrent() ||
      loop.publishedSnapshotIdentity() == nullptr || runtime->cachedTrackGeneration != playbackGeneration) return;
  const auto& cache = runtime->mergedMidiEvents;
  if (cache.builtFromSnapshotIdentity != loop.publishedSnapshotIdentity() ||
      cache.builtFromRevision != loop.playbackRevision ||
      cache.builtFromRevision != loop.publishedSnapshotRevision()) return;
  const uint32_t phase = IntervalProjection::tickPhaseInProjectionCycle(tick,
      slotIndex == activeLoopIndex ? projectionCycleStartTick : static_cast<int32_t>(loop.startLoopTick),
      loop.loopLengthTicks);
  if (cache.windowLengthTicks == 0 || cache.windowStartTick >= loop.loopLengthTicks ||
      cache.windowLengthTicks > loop.loopLengthTicks - cache.windowStartTick ||
      phase < cache.windowStartTick || phase - cache.windowStartTick >= cache.windowLengthTicks) return;
  PlaybackReadWitness witness{};
  witness.atUs = micros(); witness.tick = tick;
  witness.track = playbackRebuildTrackIndex(*this); witness.slot = slotIndex;
  witness.generation = playbackGeneration;
  // One-to-one publication revision stamp (including revision0), not a new runtime counter.
  witness.storageGeneration = uint64_t(loop.publishedSnapshotRevision()) + 1u;
  witness.origin = static_cast<uint8_t>(PlaybackWindowOrigin::Committed);
  witness.start = cache.windowStartTick; witness.length = cache.windowLengthTicks;
  witness.period = loop.loopLengthTicks; witness.phase = phase; witness.availableOnly = true;
  SC_PLAYBACK_READ_ROW(witness);
#else
  (void)slotIndex; (void)tick;
#endif
}

TRACK_COLD_MEM void Track::observePlaybackReadForTurn(
    uint8_t slotIndex, const FixedPlaybackEventStorage* storage, const FixedPlaybackBridgeStorage* bridge) {
#if SC_PLAYBACK_READ_WITNESS_ENABLED
  if (!playbackEmitMidiOutput_ || !isAudiblePlaybackActive() ||
      slotIndex >= Config::MAX_LOOPS_PER_TRACK) return;
  const auto* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) return;
  const auto* set = runtime->viewSetIfPrewarmed();
  const PlaybackViewSourceIdentity* source = nullptr;
  const PlaybackCoverageInterval* coverage = nullptr;
  uint64_t storageGeneration = 0;
  if (bridge != nullptr) {
    if (set == nullptr || set->bridgeLifecycle.selected >= set->bridges.size() ||
        &set->bridges[set->bridgeLifecycle.selected] != bridge || !bridge->complete ||
        !bridge->selected || !bridge->streamPrepared) return;
    source = &bridge->source; coverage = &bridge->coverage;
    storageGeneration = bridge->storageGeneration;
  } else if (storage != nullptr) {
    if (set != nullptr && set->lifecycle.current < set->views.size()) {
      const auto& view = set->views[set->lifecycle.current];
      if (&view.storage != storage || !view.complete || !view.published) return;
      source = &view.source; coverage = &view.coverage; storageGeneration = view.storageGeneration;
    }
  }
  const uint32_t phase = loopForSlot(slotIndex).lastTickInLoop;
  if (source == nullptr || coverage == nullptr || source->sourceGeneration != playbackGeneration ||
      storageGeneration == 0 || !coverage->isValid() || !coverage->contains(phase)) return;
  PlaybackReadWitness witness{};
  witness.atUs = micros(); witness.tick = clockManager.getCurrentTick();
  witness.track = playbackRebuildTrackIndex(*this); witness.slot = slotIndex;
  witness.generation = source->sourceGeneration; witness.storageGeneration = storageGeneration;
  witness.origin = static_cast<uint8_t>(source->origin);
  witness.start = coverage->startPhase; witness.length = coverage->lengthTicks;
  witness.period = coverage->loopLengthTicks; witness.phase = phase;
  SC_PLAYBACK_READ_ROW(witness);
#else
  (void)slotIndex; (void)storage; (void)bridge;
#endif
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

/// Task 3.15g: the E2 root-return boundary row's **transition cadence**. The block that calls this runs on every clock
/// edge that reaches it, so a row per evaluation would evict the evidence it explains (the readiness receipt's own rule);
/// the state is one scalar per slot, in RAM2 off the clock path, exactly like `LaunchGateLastState`. The signature is the
/// decision itself — the flag mask plus the wrap marker plus the adoption answer — so a change of *reason* always costs
/// exactly one row, whatever the turn rate.

namespace {

/// The launch gate row's **last emitted state** for one (track, slot) target: the row is a *transition* record, so a
/// pending switch that sits on the same exit for hundreds of deferred turns costs one row while every move is still
/// named (a row per turn would evict the evidence it explains -- the reason `TelemetryCadence` exists for the readiness
/// receipt). `DMAMEM` on the target is RAM2: off-clock state, read by the deferred-idle turn alone, never DTCM -- and
/// `.bss.dma` is NOLOAD, so "never written" is proven by `GateRowLastState`'s written magic, never by an initialiser
/// (`Utils/GateRowLastState.h` carries the measured episode).
using LaunchGateLastState = GateRowLastState::LaunchState;

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
  // `DMAMEM` is NOLOAD: `exit` is stale until this entry is written, so the first row for a (track, slot) of the boot
  // resets it before the compare (`Utils/GateRowLastState.h`; a switch to an unwritten slot rebooted the device).
  (void)GateRowLastState::ensureInitialized(last, GateRowLastState::kLaunchMagic);
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
  /// The revision this row family last reported for the target. Part of the change key, **not** a cadence: the owner is
  /// read both **before** and **after** its step (the post-step confirmation the snapshot lane needs), and a publication
  /// can be the final owner turn -- so the row carrying the new `rv` must never be deduped away by a pre/post state that
  /// happens to look alike. Keying on it makes "a revision change is always named" a contract rather than a coincidence
  /// of the verdict bits.
  uint32_t publishedRevision = 0;
  /// Progress, **excluded from the row cadence**: eligible owner turns so far (a turn that will step the selected loop).
  uint32_t eligibleTurns = 0;
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
  if (eligible) ++last.eligibleTurns;
  // A progress heartbeat: while the owner is stepping, one row per 16 eligible turns, so the progress fields are sampled
  // mid-construction (state-change rows alone land on construction start/finish, where `cu` is 0). Bounded, and only
  // while work is eligible.
  const bool progressHeartbeat = eligible && (last.eligibleTurns % 16u) == 0u;
  // `publishedRevision` is part of the key: a **revision change is always named** (the post-step confirmation the
  // snapshot lane reads), even when the pre- and post-step verdict bits happen to coincide.
  if (!progressHeartbeat && last.seen && last.verdicts == verdicts && last.selectedSlot == selectedSlot &&
      last.activeSlot == activeSlot && last.constructionUnits == constructionUnits &&
      last.publishedRevision == selected.publishedSnapshotRevision()) {
    return;
  }
  last.verdicts = verdicts;
  last.selectedSlot = selectedSlot;
  last.activeSlot = activeSlot;
  last.constructionUnits = constructionUnits;
  last.publishedRevision = selected.publishedSnapshotRevision();
  last.seen = true;
  SC_SNAPSHOT_GATE(trackIndex, selectedSlot, activeSlot, verdicts, constructionUnits, last.eligibleTurns,
                   selected.inFlightSnapshotConstructionUnits(), selected.publishedSnapshotRevision());
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
using RollingGateLastState = GateRowLastState::RollingState;

constexpr size_t kRollingGateTrackCount = Config::NUM_TRACKS;
/// The bits whose change is a change of the **decision**. `coverageRoleAvailable` (10), `ownsCoverageSource` (11),
/// `viewBuild.active()` (13) and outstanding grains (14) are **observation**: they toggle between two turns without the
/// exit changing, and the device measured exactly that -- the `docs/Agents/hitl_authoring_traps.md` "dedupe that
/// alternates forever" case, found by the leg rather than by a fixture. Three device runs on 2026-10-05
/// (`captures/s64-win-gate-0{3,4,5}`, same 24-bar leg) measured the ladder: with every bit in the key **809** rows and
/// up to **20** identical lines per state (`bld`/`rl` alternating, 758 of 775 consecutive rows differing only there);
/// with only bits 11 and 13 cleared **817** rows (the wrong bit had been guessed -- `rl` is 10, not 11, and the
/// alternation survived); with bits 10/11/13/14 cleared **57** rows, the only remaining differences being genuine
/// (`len` 27, `al` 25 as the recorded loop grows). None of the four is a reason the row reports: every exit's own token
/// names its decision, and when one of them clears the guard the **exit changes**, so the transition is still named. The
/// kept bits are the ones that *are* reasons -- the entry trio (0-2), the not-playing terms (3-7), the not-servable pair
/// (8-9), the demand-active input (12) and re-projection (15).
constexpr uint16_t kRollingGateChangeKeyMask = 0x93FFu;
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
  // playback-tick-consume-only phase 2 (every capture image): the producer's last exit, named beside the tick's reason.
  {
    const uint8_t latchTrack = playbackRebuildTrackIndex(*this);
    if (latchTrack < 8) PlaybackTickContext::gState.producerExit[latchTrack] = exit;
  }
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
  // `seen`, the key and `exit` are stale in `DMAMEM` until the entry is written; a stale non-zero `seen` with a
  // matching key used to reach the same `strcmp` (`Utils/GateRowLastState.h`).
  (void)GateRowLastState::ensureInitialized(last, GateRowLastState::kRollingMagic);
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

// The frozen Record-return owner's **source-qualified** receipt. Unlike `reportPlaybackWindowReadinessReceipt` -- which
// is reached on the denial path that repeats on every owning turn and therefore needs `TelemetryCadence` to stay
// bounded -- this one is reached only on the completion **transition** (`!wasReady`, one per completed frozen window),
// so it is event-shaped by construction and needs no counter of its own. That is also why it cannot be starved by the
// busy shared `win_readiness` phase: there is no shared gate to lose a slot in. No new state, so no RAM1 or DTCM cost.
TRACK_COLD_MEM void Track::reportFrozenRecordReadinessReceipt(uint8_t slotIndex, uint32_t currentPhase,
                                                              const RuntimePlaybackView& ready) {
  const PlaybackCoverageInterval& coverage = ready.coverage;
  // The lead the receipt explains: the distance from the phase this turn is in to the interval it just completed.
  // Zero when the interval carries no usable geometry -- a receipt about "no interval" has no lead to report, and
  // saying zero is honest rather than inventing urgency.
  uint32_t leadTicks = 0;
  if (coverage.isValid() && coverage.loopLengthTicks > 0) {
    leadTicks = handoffLeadTicks(currentPhase % coverage.loopLengthTicks, coverage.startPhase,
                                 coverage.loopLengthTicks);
  }
  SC_FROZEN_WINDOW_READINESS(playbackRebuildTrackIndex(*this), slotIndex, "complete_ready", "clock_edge",
                             ready.source.playbackRevision, leadTicks, ready.source.sourceGeneration,
                             ready.source.captureContentGeneration, coverage.startPhase, coverage.lengthTicks,
                             coverage.loopLengthTicks);
}
