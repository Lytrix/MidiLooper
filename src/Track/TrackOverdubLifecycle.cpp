//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackInternal.h"

#include <Arduino.h>
#include <cstdio>

#include "ClockManager.h"
#include "DisplayManager.h"
#include "EditManager.h"
#include "Globals.h"
#include "Logger.h"
#include "StorageManager.h"
#include "TickPhase.h"
#include "TrackUndo.h"
#include "TrackUndoPublication.h"
#include "TrackPreparedStopCommit.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/ExternalPoolAccounting.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/HotPathTelemetry.h"
#include "Utils/RuntimeTimingTelemetry.h"

namespace {

constexpr uint16_t kDeferredStopOrderQuantum = 32;
constexpr StopAdmissionContract kDeferredStopContract{
    PendingStopAdmission::kMaximumClosureGrains,
    3072,  // 2x fixed orders + rewrite/trim upper bound for 2,048 staging rows at 32 entries.
    1,
    kDeferredStopOrderQuantum,
};

constexpr uint16_t kDeferredStopPreparationQuantum = 32;

/// OpenSpec 5.6: one compact resource-denial record from the admission owner. A macro keeps the
/// decision label a literal at its branch (so each denial row names its own resource) and keeps
/// the whole call compiled out of a non-capture build.
#if defined(SESSION_CAPTURE)
#define DENY_REENTRY_RESOURCE()                                                        \
  DebugSessionCapture::reentryDecision("deny_resource", clockManager.getCurrentTick(),  \
                                       captureInterval_, activeLoopIndex,               \
                                       captureStopQueueDepth())
#else
#define DENY_REENTRY_RESOURCE() ((void)0)
#endif
/// Bounded cold reservation for the protected undo publication. Deliberately far
/// below Config::ABSOLUTE_MAX_UNDO_ENTRIES: the eager per-track 512-entry reserve
/// broke device boot (see prewarmProtectedUndo). Shared with `TrackUndo`'s stack rebuilds,
/// which must preserve it (`kUndoStackProtectedCapacity`).
constexpr uint16_t kProtectedUndoPrewarmEntries = kUndoStackProtectedCapacity;
/// Bounded cold reservation for one job's undo-collection output lists.
/// `PassIdList` / `EditPassIdList` are InternalHeapFirstAllocator (EditPass.h), so
/// reserving RuntimePlaybackViewConfig::kRecordsPerBridge (2048) for all four ring
/// entries across eight tracks demands about 512 KB of the ~340 KB internal heap
/// and aborted boot (captures/task23-recover-verify, 2026-09-16). A session longer
/// than this bound simply publishes no prepared undo entry and takes the plain
/// commit.
constexpr size_t kUndoCollectionReserveRows = 64;

}  // namespace

TRACK_COLD_MEM Track::CaptureStopJobs::CaptureStopJobs() {
  // OpenSpec 5.12 / D8: the ring carries `PendingCapturePass` receipts, so this constructor is
  // non-trivial. Keep it in FLASH -- as an implicit aggregate constructor it was emitted into
  // ITCM and pushed RAM1 across a 32 KB FlexRAM bank (locals 6,400 -> -26,368).
  // Every member has an initializer or a non-trivial default constructor, so this body leaves the
  // same state the previous value-initialized aggregate had.
}

TRACK_COLD_MEM void Track::CaptureStopJobsDeleter::operator()(CaptureStopJobs* jobs) const {
  if (!jobs) return;
  jobs->~CaptureStopJobs();
  ExternalMemoryFirstAllocator<CaptureStopJobs>{}.deallocate(jobs, 1);
}

/// S4f.1/S3.0: the latch is a value member of the externally allocated ring object, so
/// "provisioning" is the ring's own cold admission and never an allocation at the stop edge.
TRACK_COLD_MEM bool Track::prewarmInteractionLatch() { return captureStopJobs_ != nullptr; }

TRACK_COLD_MEM bool Track::hasAcceptedUnexecutedInteraction() const {
  const PendingInteractionLatch* latch = interactionLatch();
  return latch != nullptr && latch->isSet();
}

TRACK_COLD_MEM bool Track::hasLiveCaptureInterval() const {
  return isRecording() || isOverdubbing();
}

TRACK_COLD_MEM bool Track::hasStopInFlight() const {
  return hasPendingDeferredOverdubStop() || hasAcceptedUnexecutedInteraction();
}

TRACK_COLD_MEM bool Track::hasStopInFlightForSlot(uint8_t slot) const {
  if (const Track::CaptureStopJob* job = captureStopJobHead()) {
    if (job->slot == slot) {
      return true;
    }
  }
  const PendingInteractionLatch* latch = interactionLatch();
  if (latch != nullptr && latch->isSet() && latch->slot == slot) {
    return true;
  }
  return false;
}

TRACK_COLD_MEM bool Track::captureWorkSlot(uint8_t& slotOut) const {
  if (hasLiveCaptureInterval()) {
    // Only the active slot may RECORD/OVERDUB, so here the active index *is* the interval's identity
    // rather than a stand-in for it.
    slotOut = getActiveLoopIndex();
    return true;
  }
  if (!hasStopInFlight()) {
    return false;
  }
  // A stop in flight is pinned to its interval's slot; the active index is only the fallback for a
  // stop-in-flight state that carries no slot at all.
  if (pinnedStopSlot(slotOut)) {
    return true;
  }
  slotOut = getActiveLoopIndex();
  return true;
}

TRACK_COLD_MEM bool Track::pinnedStopSlot(uint8_t& slotOut) const {
  // S4.1: the interval's own slot, not the active index. The deferred job carries `job.slot` and the
  // latch carries `slot`; both survive an active-slot change, which is exactly why a consumer that
  // means "the interval being finalized" cannot read `getActiveLoopIndex()`. The job is consulted
  // first because it is the admitted obligation; a latch without a job is a stop accepted but not yet
  // admitted (S4f.5), and it still names the target the user stopped.
  if (const Track::CaptureStopJob* job = captureStopJobHead()) {
    slotOut = job->slot;
    return true;
  }
  const PendingInteractionLatch* latch = interactionLatch();
  if (latch != nullptr && latch->isSet()) {
    slotOut = latch->slot;
    return true;
  }
  return false;
}

TRACK_COLD_MEM uint32_t Track::acceptedStopWaitCount() const {
  const PendingInteractionLatch* latch = interactionLatch();
  return latch != nullptr ? latch->waitCount : 0;
}

/// S4f.1: retain an accepted stop against the interval's own slot and generation. The identity
/// is written once and never overwritten by a later request (handoff §5): a repeat of the same
/// stop is a no-op, not a second obligation.
TRACK_COLD_MEM void Track::latchAcceptedStop(PendingStopDestination destination, uint8_t slot,
                                             uint64_t interval) {
  PendingInteractionLatch* latch = interactionLatch();
  if (latch == nullptr || !loopsAllocated() || slot >= Config::MAX_LOOPS_PER_TRACK) return;
  const Loop& owner = getLoop(slot);
  if (latch->matches(PendingInteractionLatch::Kind::Stop, slot, 0, interval) &&
      latch->loopId == owner.loopId && latch->mode == owner.capture.phase) return;
  latch->clear();
  latch->kind = PendingInteractionLatch::Kind::Stop;
  latch->destination = destination;
  latch->slot = slot;
  latch->loopId = owner.loopId;
  latch->mode = owner.capture.phase;
  latch->interval = interval;
  latch->phaseTick = capturePhaseTick(clockManager.getCurrentTick());
}

/// The stop obligation is consumed exactly once: by the admission that binds it to the job, by
/// the transport-stop path that retargets it to STOPPED, or by a synchronous stop that completes
/// the interval. It never clears an unrelated pending switch or start carrier (handoff §5).
TRACK_COLD_MEM void Track::consumeAcceptedStopObligation() {
  PendingInteractionLatch* latch = interactionLatch();
  if (latch == nullptr || latch->kind != PendingInteractionLatch::Kind::Stop) return;
  latch->clear();
}

/// S2b.6: the stop-side park. Admission found the interval slot's canonical-copy reservation
/// unadmitted. Legal here because a deferred turn is not the action edge. It re-runs the slot's
/// reservation, then reports `canonical_output_busy` as a **wait** and lets the next deferred
/// turn re-evaluate the full guard chain. It never freezes, never exchanges staging roles, and
/// never cancels the accepted request: the user never presses twice (invariant 31).
TRACK_COLD_MEM void Track::parkAcceptedStopForCanonicalOutput(uint8_t slot) {
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slot);
  PendingInteractionLatch* latch = interactionLatch();
  if (runtime == nullptr || latch == nullptr) return;
  const bool admitted = runtime->canonicalCaptureOutputAdmitted();
  if (!admitted && runtime->canonicalCaptureOutput.store.empty() &&
      LoopEventStore::freeChunkCount() != 0) {
    (void)runtime->replenishCanonicalCaptureChunk();
  }
  if (!admitted) (void)runtime->prewarmCanonicalCaptureOutput();
  if (!latch->waitReported) {
    latch->waitReported = true;
    SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX, 1, 0,
                       "accepted_stop_finalizer_wait");
  }
  ++latch->waitCount;
  // S2b.6's diagnostic terminal (invariant 31; spec.md "A park terminal never cancels an accepted
  // interaction"). `accepted_stop_finalizer_wait` above marks the *first* wait; this marks the point
  // where waiting has become **stalled**: the canonical target is held by a live owner -- a populated
  // store, which the park must not clear because that is exactly what S2b.4 forbids mid-copy -- and
  // the reservation cannot be acquired, so no deferred turn can admit the stop until that owner frees.
  //
  // Reported, never acted on: it does not clear, drain or cancel, so the accepted stop stays
  // authoritative and still executes exactly once when the owner frees. Keyed off the existing
  // per-latch `waitCount` rather than a new latch field because `PendingInteractionLatch` is
  // size-pinned by S3.0's placement asserts; `==` makes it once per episode, since the count passes
  // through the threshold only once however long the wedge lasts.
  constexpr uint32_t kAcceptedStopParkTerminalAfterWaits = 8;
  if (!runtime->canonicalCaptureOutputAdmitted() &&
      !runtime->canonicalCaptureOutput.store.empty() &&
      latch->waitCount == kAcceptedStopParkTerminalAfterWaits) {
    SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX, 1, 0,
                       "accepted_stop_park_terminal");
  }
}

/// S4f.2: the dispatch decision. Called at the top of `processDeferredIdleMaintenance`, after the
/// admitted stop's grain and before the owner-specific branches, so the latch is reachable on
/// every deferred owner lane. The admitted job always owns the turn first, so an accepted request
/// can never stack a second stop job.
TRACK_COLD_MEM bool Track::serviceAcceptedInteractionTurn() {
  if (!hasAcceptedUnexecutedInteraction() || hasPendingDeferredOverdubStop()) return false;
  (void)driveAcceptedInteraction();
  return true;
}

/// S4f.2: one deferred turn of the accepted interaction.
TRACK_COLD_MEM bool Track::driveAcceptedInteraction() {
  const PendingInteractionLatch* latch = interactionLatch();
  if (latch == nullptr) return false;
  const PendingInteractionLatch request = *latch;
  if (request.kind != PendingInteractionLatch::Kind::Stop) return false;
  // S4f.4 transition 3 (load/replacement): a load or a replacement interval can leave the slot *number*
  // unchanged while the interval the request identified is gone. Such a request must not bind to the
  // replacement content, and it must not park either -- parking would wait forever for an interval that can
  // never return. It is **superseded**: reported and retired, exactly once.
  if (!loopsAllocated() || request.slot >= Config::MAX_LOOPS_PER_TRACK ||
      request.interval != captureInterval_ ||
      getLoop(request.slot).loopId != request.loopId ||
      getLoop(request.slot).capture.phase != request.mode) {
    logger.log(CAT_TRACK, LOG_INFO,
               "Accepted stop superseded: its interval is no longer live (slot=%u)",
               static_cast<unsigned>(request.slot));
    consumeAcceptedStopObligation();
    return true;
  }
  // DEC-078 item 3: re-derive the destination from live transport state rather than trusting a
  // stored absolute tick or a destination that transport stop has since retargeted.
  const PendingStopDestination destination =
      clockManager.isTransportRunning() ? request.destination : PendingStopDestination::Stopped;
  if (beginDeferredOverdubStopForSlot(destination, clockManager.getCurrentTick(), request.slot,
                                      request.interval)) {
    consumeAcceptedStopObligation();
    return true;
  }
  parkAcceptedStopForCanonicalOutput(request.slot);
  return false;
}

/// S2b.2 (invariant 34): the reserve set a slot needs at its own overdub start once boot/load no
/// longer prewarms a committed-but-inactive slot. It is the record arm's set, minus the canonical
/// copy (which stays lazy and is acquired by the accepted stop's park). FLASH-resident: it is an
/// allocation-bearing cold path, and inlining it into `Track::startOverdubbing` (ITCM) cost ~300 B
/// of the RAM1 padding line.
TRACK_COLD_MEM bool Track::admitOverdubStartWorkingSet() {
  // No arguments on purpose: the caller is ITCM and this is the only cold call it makes for the
  // admission, so every instruction at the call site is RAM1 padding spent.
  auto* runtimePtr = playbackRuntime.slotIfAllocated(activeLoopIndex);
  if (runtimePtr == nullptr) return false;
  LoopPlaybackRuntime& runtime = *runtimePtr;
  Loop& loop = getActiveLoop();
  // The denial report lives here rather than at the call site: this function is FLASH-resident and
  // the caller is ITCM, so every call saved there is RAM1 padding kept.
  struct DenialReport {
    bool ok = true;
    ~DenialReport() {
      if (!ok) MemoryMonitor::reportPrewarmAdmissionDenied(MemoryMonitor::OperationKind::OverdubStart);
    }
  } denial;
  // (1) The view/bridge/companion scratch. The stop's declared preparation estimates are
  // `UINT64_MAX` without it (`LoopPlaybackRuntime::requiredStopRankedGrains` returns UINT64_MAX when
  // `viewSetIfPrewarmed()` is null), so admission would deny as `preparation_service_unavailable`
  // forever. Guarded, so a record-armed or successor slot reserves nothing.
  if (runtime.viewSetIfPrewarmed() == nullptr) {
    ExternalPoolAccounting::beginTag(ExternalPoolAccounting::TagViewPrewarm);
    const bool viewsAdmitted = runtime.prewarmViews(loop.companionRowHorizonBound(),
                                                   loop.fullTakePlaybackViewAdmissible());
    ExternalPoolAccounting::endTag();
    if (!viewsAdmitted) { denial.ok = false; return false; }
  }
  // (2) The transfer horizon, which the DEC-067 guard chain reads as
  // `hasActiveOverdubTransferAdmission()`. Guarded by that same predicate, which is what makes the
  // placement safe: `prewarmActiveOverdubTransferHorizon` grows `passes.overdubPasses`, so it must
  // not run while an admitted job depends on that vector's addressing. When the predicate is already
  // true nothing is reserved, and when it is false no stop job can hold the slot (admission
  // requires it).
  if (!loop.hasActiveOverdubTransferAdmission()) {
    ExternalPoolAccounting::beginTag(ExternalPoolAccounting::TagTransferHorizon);
    const bool horizonAdmitted = loop.prewarmActiveOverdubTransferHorizon();
    ExternalPoolAccounting::endTag();
    if (!horizonAdmitted) { denial.ok = false; return false; }
  }
  // (3) The Loop-level prepared-companion-history horizon, which the deferred stop's
  // `PrepareTransfer`/`CanonicalTail` phases deny without (`stepPreparedCompanionHistory` is
  // refused), so a loaded slot would admit its stop and then never retire it. Same derived grant the
  // record arm uses.
  const size_t usedRows =
      std::min(loop.passes.editPasses.size(), RuntimePlaybackViewConfig::kRecordsPerBridge);
  const size_t companionCapacity =
      std::min(RuntimePlaybackViewConfig::kRecordsPerBridge - usedRows,
               loop.companionRowHorizonBound());
  // A live stop job already proved this reserve (its admission requires it) and it may be
  // *mid-preparation* on it: `prewarmPreparedCompanionHistory` resets the Loop's prepared history,
  // which is exactly the DEC-072/5.12 successor hazard, so never touch it while a job is in flight.
  if (!hasPendingDeferredOverdubStop() &&
      !loop.preparedCompanionHistoryPrewarmed(companionCapacity)) {
    ExternalPoolAccounting::beginTag(ExternalPoolAccounting::TagCompanionHistory);
    const bool prepared = loop.prewarmPreparedCompanionHistory(companionCapacity);
    ExternalPoolAccounting::endTag();
    if (!prepared) { denial.ok = false; return false; }
  }
  // S3.1: the loaded slot now holds the same working set the record arm establishes, so record the
  // same ownership. This path is why a boot-cold slot can stop without the boot reservation
  // (S2b.1/S2b.2), and it must not diverge from the record arm about who owns the slot.
  runtime.acquireWorkingSetLease(WorkingSetRole::PlaybackWindow | WorkingSetRole::LiveCapture);
  return true;
}

TRACK_COLD_MEM bool Track::prewarmCaptureStopJobs() {
  if (!captureStopJobs_) {
    auto* storage = ExternalMemoryFirstAllocator<CaptureStopJobs>{}.allocate(1);
    if (!storage) return false;
    captureStopJobs_.reset(new (storage) CaptureStopJobs{});
  }
  // Cold admission of every descriptor's sealed-range destination directory.
  // A descriptor that already owns a sealed extent keeps it; a released one is
  // re-admitted here so the action edge never reserves storage.
  for (size_t index = 0; index < CaptureStopJobs::kCapacity; ++index) {
    CaptureStopJob& entry = captureStopJobs_->entries[index];
    if (!entry.rawRange.ready() && !entry.rawRange.reserveTail()) return false;
    // The bounded undo collection requires caller-supplied, pre-reserved private
    // outputs; it never grows them itself (a growth would be a denied collection).
    const size_t rows = std::min<size_t>(RuntimePlaybackViewConfig::kRecordsPerBridge,
                                         kUndoCollectionReserveRows);
    if (entry.undoPassIds.capacity() < rows) entry.undoPassIds.reserve(rows);
    if (entry.undoCompanionIds.capacity() < rows) entry.undoCompanionIds.reserve(rows);
  }
  // S4f.1: the accepted-interaction latch is cold admission too; the stop edge must never
  // allocate one, and a missing latch would silently degrade acceptance back to a denial.
  return prewarmInteractionLatch() && prewarmProtectedUndo();
}

TRACK_COLD_MEM bool Track::retireJobCaptureSourceGrain(CaptureStopJob& job, Loop& loop,
                                                       bool closeCapture) {  if (!job.ownsRawRange) {
    // Legacy producer path: this job never took ownership of a sealed extent.
    return loop.retireCopiedCaptureSourceGrain(closeCapture);
  }
  const bool complete = job.rawRange.releaseGrain(1);
  if (!complete) return false;
  // The owned extent is fully released. Drop the Loop's read borrow only while it
  // still identifies this exact job, so a stale job can never unbind a successor.
  (void)loop.releaseSealedCaptureRangeBorrow(&job.admission);
  job.ownsRawRange = false;
  // Re-arm this descriptor's reserved destination directory for a future
  // admission. Bounded cold work in a deferred turn (one pool chunk plus the
  // retained directory); if it fails, the next admission denies explicitly
  // instead of reserving at the action edge.
  (void)job.rawRange.reserveTail();
  if (closeCapture) loop.closeProducerCapturePhase();
  return true;
}

TRACK_COLD_MEM Track::CaptureStopJob* Track::captureStopJobHead() {
  if (!captureStopJobs_ || captureStopJobs_->count == 0) return nullptr;
  return &captureStopJobs_->entries[captureStopJobs_->head];
}

TRACK_COLD_MEM const Track::CaptureStopJob* Track::captureStopJobHead() const {
  if (!captureStopJobs_ || captureStopJobs_->count == 0) return nullptr;
  return &captureStopJobs_->entries[captureStopJobs_->head];
}

TRACK_COLD_MEM PendingCapturePass* Track::ownedPendingCapturePass() {
  CaptureStopJob* head = captureStopJobHead();
  return (head != nullptr && head->ownsPendingPass) ? &head->pendingPass : nullptr;
}

TRACK_COLD_MEM const PendingCapturePass* Track::ownedPendingCapturePass() const {
  const CaptureStopJob* head = captureStopJobHead();
  return (head != nullptr && head->ownsPendingPass) ? &head->pendingPass : nullptr;
}

TRACK_COLD_MEM Track::CaptureStopJob* Track::captureStopJobTail() {
  if (!captureStopJobs_) return nullptr;
  if (captureStopJobs_->count >= CaptureStopJobs::kCapacity) return nullptr;
  const size_t tail =
      (captureStopJobs_->head + captureStopJobs_->count) % CaptureStopJobs::kCapacity;
  return &captureStopJobs_->entries[tail];
}

TRACK_COLD_MEM Track::CaptureStopJob& Track::captureStopJob() {
  return captureStopJobs_->entries[captureStopJobs_->head];
}

TRACK_COLD_MEM const Track::CaptureStopJob& Track::captureStopJob() const {
  return captureStopJobs_->entries[captureStopJobs_->head];
}

#if defined(SESSION_CAPTURE)
TRACK_COLD_MEM void Track::printStoppedCaptureContentForHitl() {
  if (clockManager.isTransportRunning() || hasPendingDeferredOverdubStop() ||
      !loopsAllocated() || isRecording() || isOverdubbing()) {
    Serial.println("CAPTURE_CONTENT,denied");
    return;
  }
  const auto& loop = getActiveLoop();
  Serial.printf("CAPTURE_CONTENT,begin,track=%u,slot=%u,length=%lu\r\n",
      midiChannel, activeLoopIndex, static_cast<unsigned long>(loop.loopLengthTicks));
  SessionMidiEventVec rows;
  auto printRows = [&rows](const char* kind, unsigned pass) {
    Serial.printf("CAPTURE_CONTENT,rows,%s,%u,%u\r\n", kind, pass,
                  static_cast<unsigned>(rows.size()));
    for (const auto& e : rows) {
      Serial.printf("CAPTURE_CONTENT,event,%s,%u,%lu,%u,%u,%u,%u,%lu\r\n",
          kind, pass, static_cast<unsigned long>(e.tick), static_cast<unsigned>(e.type),
          e.channel, e.data.noteData.note, e.data.noteData.velocity,
          static_cast<unsigned long>(e.noteId));
    }
  };
  LoopEventStore::appendChunkRefEvents(loop.passes.recordPass.committedChunkIds, rows);
  printRows("record", 0);
  for (size_t i = 0; i < loop.passes.overdubPasses.size(); ++i) {
    rows.clear();
    LoopEventStore::appendChunkRefEvents(loop.passes.overdubPasses[i].committedChunkIds, rows);
    printRows("overdub", static_cast<unsigned>(i));
  }
  loop.passes.materializeToEventVector(rows, loop.loopLengthTicks);
  printRows("effective", 0);
  Serial.println("CAPTURE_CONTENT,end");
}

TRACK_COLD_MEM void Track::printStoppedServiceStatusForHitl() {
  if (clockManager.isTransportRunning() || !captureStopJobs_) return;
  const auto& stop = captureStopJob().admission;
  const auto* runtime = playbackRuntime.slotIfAllocated(activeLoopIndex);
  const uint8_t trackIndex = resolveTrackIndexForPersistence(*this);
  Serial.printf("#STOP_OUTPUT,track=%u,slot=%u,muted=%u,audible=%u,enabled=%u,slot_muted=%u,barrier=%u,tick=%lu,last=%lu\r\n",
      trackIndex, activeLoopIndex, isMuted(), trackManager.isTrackAudible(trackIndex),
      trackManager.isSlotEnabled(trackIndex, activeLoopIndex),
      trackManager.isSlotMuted(trackIndex, activeLoopIndex), transportStopOutputBarrier_,
      static_cast<unsigned long>(clockManager.getCurrentTick()),
      static_cast<unsigned long>(loopsAllocated() ? getActiveLoop().lastTickInLoop : UINT32_MAX));
  Serial.printf("#STOP_STATUS,us=%lu,state=%s,pending=%u,phase=%u,dest=%u,order=%llu,ranked=%llu,canonical=%llu,prefix=%u,tail=%u,bridge=%u,wrap=%u,queue=%u/%u\r\n",
      static_cast<unsigned long>(micros()), getStateName(getState()), stop.pending,
      static_cast<unsigned>(stop.phase), static_cast<unsigned>(stop.destination),
      static_cast<unsigned long long>(stop.remainingOrderGrains),
      static_cast<unsigned long long>(stop.remainingRankedPreparationGrains),
      static_cast<unsigned long long>(stop.remainingCanonicalPreparationGrains),
      runtime ? static_cast<unsigned>(runtime->canonicalPrefixPhase) : 255u,
      runtime ? static_cast<unsigned>(runtime->canonicalTailPhase) : 255u,
      runtime ? static_cast<unsigned>(runtime->captureBridgePhase) : 255u,
      loopsAllocated() && getActiveLoop().hasDeferredVisualCacheWrapAppend(),
      static_cast<unsigned>(captureStopQueueDepth()),
      static_cast<unsigned>(captureStopQueueCapacity()));
  if (loopsAllocated() && runtime) {
    const auto* set = runtime->viewSetIfPrewarmed();
    Serial.printf("#STOP_PREFIX,phase=%u,window=%u,attached=%u,inputs=%u,apply=%u,passes=%u,build_active=%u,index=%u,current=%u,ready=%u,build=%u,retained=%u\r\n",
        static_cast<unsigned>(runtime->canonicalPrefixPhase), static_cast<unsigned>(runtime->canonicalWindowPhase),
        runtime->canonicalPassInputs.captureAttached(), runtime->canonicalPassInputs.ready(),
        runtime->frozenCompanionApply.available(), runtime->frozenCompanionPasses.idle(), runtime->viewBuild.active(),
        runtime->canonicalWindowIndex, set ? set->lifecycle.current : 255u,
        set ? set->lifecycle.ready : 255u, set ? set->lifecycle.build : 255u,
        set ? set->lifecycle.retained : 255u);
    const auto& loop = getActiveLoop();
    const auto& work = loop.preparedCompanionHistory_;
    const PendingCapturePass* jobPass = ownedPendingCapturePass();
    const bool hasStagedPass = jobPass != nullptr || loop.hasPendingCapturePass();
    const size_t stagedChunks = jobPass != nullptr ? jobPass->committedChunkIds.size()
                                                   : loop.pendingCapturePass_.committedChunkIds.size();
    const PassId stagedPassId = jobPass != nullptr ? jobPass->id : loop.pendingCapturePass_.id;
    Serial.printf("#STOP_HISTORY,phase=%u,storage=%llu,current=%llu,history=%u,current_history=%u,rows=%u,prewarmed=%u,pass=%u,pass_id=%u,chunks=%u\r\n",
        static_cast<unsigned>(work.phase),
        static_cast<unsigned long long>(work.storageGeneration),
        static_cast<unsigned long long>(loop.canonicalStorageGeneration),
        static_cast<unsigned>(work.historyCount), static_cast<unsigned>(loop.passes.editPasses.size()),
        static_cast<unsigned>(runtime->frozenCaptureCompanions.size()),
        loop.preparedCompanionHistoryPrewarmed(runtime->frozenCaptureCompanions.size()),
        hasStagedPass, static_cast<unsigned>(stagedPassId), static_cast<unsigned>(stagedChunks));
  }
}
#endif

TRACK_COLD_MEM bool Track::commitPreparedOverdubStopBridge(
    Loop& loop, LoopPlaybackRuntime& runtime, PendingStopAdmission& stop,
    const uint32_t& generation, const EmissionCursor& cursor,
    PreparedCapturePassTransfer& transferred, PendingCapturePass* pendingPass) {
  return TrackPreparedStopCommit::commit(loop, runtime, stop, generation, cursor, transferred,
                                        pendingPass);
}

TRACK_COLD_MEM bool Track::beginDeferredOverdubStop(PendingStopDestination destination,
                                                    uint32_t currentTick) {
  // S4f.0: the live edge admits against the interval that is actually recording. The deferred
  // drive calls the slot-parameterised form with the latch's own (slot, interval).
  return beginDeferredOverdubStopForSlot(destination, currentTick, activeLoopIndex,
                                         captureInterval_);
}

TRACK_COLD_MEM bool Track::beginDeferredOverdubStopForSlot(PendingStopDestination destination,
                                                           uint32_t currentTick, uint8_t slot,
                                                           uint64_t interval) {
  if (!captureStopJobs_ || !isOverdubbing() || hasPendingDeferredOverdubStop()) {
    return false;
  }
  // DEC-078 item 3 / S4f.0: the request names the interval it belongs to. A drive whose
  // interval is no longer live is a superseded request, not a refusal of a user interaction:
  // it must not bind this job to a later interval's content, staging or generation.
  if (slot >= Config::MAX_LOOPS_PER_TRACK || interval != captureInterval_) {
    SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX, 1, 0, "stale_interval");
    return false;
  }
  // The ring is the FIFO admission authority. A new stop interval always takes
  // the tail descriptor; a full ring is an explicit exhaustion denial, never
  // ordinary finalization latency.
  CaptureStopJob* admission = captureStopJobTail();
  if (admission == nullptr || !admission->pendingNotes.empty()) {
    SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX,
                       admission != nullptr, captureStopQueueDepth(), "queue_full");
    return false;
  }
  CaptureStopJob& job = *admission;
  // S4f.0: every Loop/runtime-derived guard and binding below resolves the interval's own
  // slot, so a later active-slot movement cannot make the drive test the wrong slot.
  Loop& loop = getLoop(slot);
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slot);
  // A selected bridge is intentionally retained across PLAYING so it can
  // stream the just-published pass. Its completed build workers are no longer
  // an active stop transaction, however. Retire those workers before admitting
  // a later record/overdub-button stop; the selected bridge survives discard.
  if (runtime != nullptr &&
      runtime->captureBridgePhase == LoopPlaybackRuntime::CaptureBridgePhase::Complete) {
    runtime->discardFrozenCaptureBridge();
  }
  CaptureAdmissionStaging* views =
      runtime != nullptr ? runtime->liveCaptureStagingIfPrewarmed() : nullptr;
  if (runtime == nullptr || views == nullptr) {
    SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX,
                       views != nullptr, loop.hasActiveOverdubTransferAdmission(), "storage_denied");
    return false;
  }
  // DEC-067: this is the admission boundary that owns the finalization resources.
  // They are checked here, and not at the interval's START, because none of them is
  // used before this edge. Each denial is explicit and non-destructive: no stop
  // descriptor is taken, and the interval stays the live recording interval.
  if (runtime->captureStopOwner != nullptr) {
    SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX, 1,
                       loop.hasActiveOverdubTransferAdmission(), "finalizer_busy");
    DENY_REENTRY_RESOURCE();
    return false;
  }
  if (!loop.hasActiveOverdubTransferAdmission()) {
    SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX, 1, 0, "transfer_denied");
    DENY_REENTRY_RESOURCE();
    return false;
  }
  if (!runtime->canonicalCaptureOutputAdmitted()) {
    SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX, 1, 0, "canonical_output_busy");
    DENY_REENTRY_RESOURCE();
    return false;
  }
  if (!loop.canHandoffSessionExitSnapshot()) {
    SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX, 1, 0, "session_exit_handoff_full");
    DENY_REENTRY_RESOURCE();
    return false;
  }
  // DEC-068: this interval's own session-exit tail is taken below, once admission is committed.
  // It must NOT be taken here: the causal-base, credit, raw-range and freeze checks below can
  // still deny admission, and swapping the session identities into a receipt for an admission
  // that never happened leaves the synchronous fallback (`stopOverdubbing` ->
  // `commitCaptureForStop` -> `pushOverdubSessionOnStop`) reading an empty live session -- it then
  // records only the final pass, and one undo removes a partial overdub.
  // This admission just proved that no job owns the slot's finalization resources, so the
  // frozen order/rewrite builders still carry the *retired* interval's binding. Clear it
  // here: this interval's CanonicalOrder phase binds them for its own frozen rows, and
  // `beginFrozenCaptureOrder` denies a build whose binding does not match the role
  // (a successor job stalled in CanonicalOrder until this moved out of `startOverdubbing`).
  runtime->discardFrozenCaptureOrder();
  runtime->discardFrozenCaptureCanonicalOrder();
  CaptureAdmissionStaging& staging = *views;
  const size_t openReservations = staging.openReservationCount();
  const size_t finalFrozenCount = staging.storage.eventCount + openReservations;
  const uint32_t generation = staging.captureGeneration;
  const size_t pendingChangeCount = std::min(
      loop.pendingNoteChanges().size(), RuntimePlaybackViewConfig::kRecordsPerBridge);
  const size_t historyCount = std::min(
      loop.passes.editPasses.size(), RuntimePlaybackViewConfig::kRecordsPerBridge);
  const size_t snapshotCount = loop.pendingSnapshotCompanionIds() == nullptr
      ? 0 : loop.pendingSnapshotCompanionIds()->size();
  const size_t captureChunks = finalFrozenCount == 0 ? 1 :
      1 + (finalFrozenCount - 1) / LoopEventStoreConfig::CHUNK_CAPACITY;
  const bool stoppedDestination = destination == PendingStopDestination::Stopped;
  const PlaybackViewSourceIdentity baseSource{
      loop.playbackRevision, playbackGeneration, loop.publishedSnapshotIdentity()};
  // Causal predecessor identity. Resolve and pin it here, before the raw-range
  // ownership transfer, so a later grain resolves this interval's successor
  // semantics against the source it actually succeeded. A published identity that
  // cannot be pinned must deny admission rather than be recorded as a dangling
  // pointer: nothing after the transfer may fail.
  ImmutableLoopSnapshotRef causalBaseSnapshot;
  if (baseSource.snapshotIdentity) {
    causalBaseSnapshot = loop.publishedSnapshot();
    if (!causalBaseSnapshot ||
        static_cast<const void*>(causalBaseSnapshot.get()) != baseSource.snapshotIdentity) {
      SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX,
                         finalFrozenCount, openReservations, "causal_base_unavailable");
      return false;
    }
  }
  // Rolling emission windows do not describe the canonical candidate horizon.
  // Use the prewarmed owner, even when the last capture contains only one event.
  uint64_t rankedCredits = stoppedDestination || finalFrozenCount == 0 ? 0 :
      runtime->requiredStopRankedGrains(baseSource,
          std::min(loop.passes.overdubPasses.size() + 2,
                   RuntimePlaybackViewConfig::kRecordsPerBridge),
          historyCount, pendingChangeCount, kDeferredStopPreparationQuantum);
  if (rankedCredits != UINT64_MAX && rankedCredits &&
      runtime->selectedFullLoopBridge(baseSource, loop.loopLengthTicks)) {
    rankedCredits += RuntimePlaybackBridgeBuild::requiredRetainedClosureGrains(
        ActiveNoteLedger::kMaxOpenNotes, kDeferredStopPreparationQuantum);
  }
  const uint64_t canonicalRequired = stoppedDestination || finalFrozenCount == 0 ? 0 :
      Loop::requiredPreparedCompanionHistoryGrains(
          historyCount, 0, pendingChangeCount, kDeferredStopPreparationQuantum,
          snapshotCount, finalFrozenCount, captureChunks);
  const uint64_t canonicalCredits = canonicalRequired == UINT64_MAX ? UINT64_MAX :
      canonicalRequired + 1;
  const uint64_t recycleRequired = stoppedDestination || finalFrozenCount == 0 ? 0 :
      Loop::requiredPreparedCompanionRecycleGrains(
          historyCount + pendingChangeCount, 0, pendingChangeCount, snapshotCount,
          kDeferredStopPreparationQuantum);
  const uint64_t recycleCredits = recycleRequired == UINT64_MAX ? UINT64_MAX :
      recycleRequired + 1;
  // Bounded undo-identity collection for the prepared PLAYING route. The STOPPED
  // and empty routes skip companion preparation, so they carry no such estimate.
  const uint64_t undoCollectRequired = stoppedDestination || finalFrozenCount == 0 ? 0 :
      Loop::requiredSessionStopUndoCollectionGrains(
          std::min(loop.passes.overdubPasses.size() + 2,
                   RuntimePlaybackViewConfig::kRecordsPerBridge),
          pendingChangeCount, snapshotCount, kDeferredStopPreparationQuantum);
  const uint64_t undoCollectionCredits =
      undoCollectRequired == UINT64_MAX ? UINT64_MAX : undoCollectRequired;
  if (rankedCredits == UINT64_MAX || canonicalCredits == UINT64_MAX ||
      recycleCredits == UINT64_MAX || undoCollectionCredits == UINT64_MAX) {
    SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX,
                       rankedCredits, canonicalCredits, "preparation_service_unavailable");
    return false;
  }
  // Prove the raw ownership transfer before any admission mutation. A denied
  // admission must perform no seal and no transfer. An empty capture owns no
  // extent and stays on the legacy producer path. The destination is a
  // *reserved* range (not a sealed one), so this checks its receivability.
  const bool hasRawExtent = !loop.capture.store.empty();
  if (hasRawExtent && !loop.capture.store.canSealCaptureRange(job.rawRange)) {
    SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX,
                       job.ownsRawRange, static_cast<uint32_t>(loop.capture.store.size()),
                       "raw_range_unavailable");
    return false;
  }
  // Pure predicate: resolve it before the seal so no fallible step follows the
  // ownership transfer.
  if (!isValidStateTransition(TRACK_OVERDUB_STOPPING)) {
    return false;
  }
  if (!job.admission.begin(generation, currentTick, 0, openReservations,
                                   finalFrozenCount, kDeferredStopContract,
                                   rankedCredits, canonicalCredits, recycleCredits,
                                   undoCollectionCredits) ||
      !staging.freeze(generation)) {
    SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX,
                       finalFrozenCount, openReservations, "freeze_or_contract_denied");
    job.admission.reset();
    return false;
  }
  // The live role now holds the job's immutable frozen rows; the previous
  // frozen role becomes clean ingress ownership for the next interval. This is
  // a constant-work index exchange: neither staging is copied or shared with
  // the other, and the job's reserved ranges stay intact.
  runtime->exchangeCaptureStagingRoles();
  // There is no closure grain when every captured NoteOn already has its
  // matching NoteOff. Advance directly to canonical ordering; otherwise the
  // deferred owner would remain in ReservedClosure forever waiting for a
  // reservation that cannot exist (the clean normal-stop hang).
  if (openReservations == 0) {
    job.admission.setPhase(PendingStopPhase::CanonicalPrefix);
  }
  job.admission.destination = destination;
  job.owner = &loop;
  runtime->setCaptureStopOwner(job.admission, job.admission.frozenGeneration);
#if defined(SESSION_CAPTURE)
  // S4f.3 L5: the accepted stop is **bound** -- the interval the request named is now the job's. The
  // denial record (`DIAG,reentry,deny,<tick>,<interval>,<slot>,<depth>`) carries the request's interval,
  // but nothing else on the stop tier does: those records carry `frozenGeneration`, which is
  // `staging.captureGeneration` and a different identity space. Reusing `reentryDecision` with the
  // `bound` literal makes the binding edge name the *same* interval identity, so a denied request and its
  // later binding can be correlated instead of inferred. `interval` here is the parameter the caller
  // passed (`captureInterval_` via `beginDeferredOverdubStop`, or the latch's stored interval via
  // `driveAcceptedInteraction`), which is exactly what the denial reports.
  DebugSessionCapture::reentryDecision("bound", currentTick, interval, slot, captureStopQueueDepth());
#endif
  // DEC-068: admission is now committed and no fallible step remains, so take this interval's own
  // session-exit tail. The shared session state is still this interval's (the successor's window
  // opens below), and this receipt is the only record of our identities once it swaps them out.
  const CommitReason sessionExitReason =
      destination == PendingStopDestination::Playing ? CommitReason::OverdubStop
                                                     : CommitReason::OverdubStopToStopped;
  const size_t sessionExitReceiptRing = loop.captureSessionExitTail(sessionExitReason);
  if (sessionExitReceiptRing == Loop::kInvalidSessionExitReceipt) {
    // `canHandoffSessionExitSnapshot` admitted the ring above, so this is unexpected; the
    // deferred commit falls back to the legacy one-shot handoff when the ring is invalid.
    SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX, 1, 0, "session_exit_tail_denied");
  }
  // DEC-067: the predecessor now owns the frozen role and the live role is clean, so a
  // successor interval may start even though this job is still pending. The human log line
  // is the *timely* edge for the HITL probe (`#CAP` DIAG rows queue behind the deferred
  // stop's flood and arrive seconds late); the DIAG row is the evidence-tier record.
  logger.log(CAT_TRACK, LOG_INFO, "Reentry window open slot=%u",
             static_cast<unsigned>(slot));
#if defined(SESSION_CAPTURE)
  DebugSessionCapture::successorStartWindowOpen(slot, currentTick,
                                                loop.loopLengthTicks);
#endif
  deferredStopStallNoted_ = false;
  job.loopId = loop.loopId;
  job.slot = slot;
  job.mode = loop.capture.phase;
  job.phaseTick = capturePhaseTick(currentTick);
  job.loopLengthTicks = loop.loopLengthTicks;
  job.interval = interval;
  job.admission.sessionExitReceiptRing = sessionExitReceiptRing;
  job.causalBase = baseSource;
  job.causalBaseSnapshot = std::move(causalBaseSnapshot);
  if (!job.causalBase.snapshotIdentity) job.causalBaseSnapshot = {};
  job.causalBaseRecorded = true;
  job.pendingNotes.swap(pendingNotes);
  ++captureStopJobs_->count;
  // Ownership transfer. The precheck proved this cut and nothing between it and
  // here mutates the producer or the destination directory, so admission has no
  // fallible step left. If the cut still fails, the job stays on the legacy
  // producer path instead of publishing an interval without its owned input.
  if (hasRawExtent && loop.capture.store.sealCaptureRange(job.rawRange)) {
    job.ownsRawRange = true;
    (void)loop.borrowSealedCaptureRange(job.rawRange, &job.admission);
  } else {
    job.ownsRawRange = false;
    if (hasRawExtent) {
      SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX,
                         finalFrozenCount, openReservations, "raw_seal_unexpected");
    }
  }
  // Job-owned pending note changes (Add/Shorten/Hide intents). Constant-work
  // swap: this job resolves against its own set while the live session keeps an
  // independent, prewarmed set for a successor interval.
  loop.swapPendingNoteChanges(job.pendingChanges);
  (void)loop.borrowPendingNoteChanges(job.pendingChanges, &job.admission);
  // Job-owned capture preview: the deferred cleanup seals this interval's preview
  // even if a successor interval has already replaced the live one.
  loop.swapCapturePreview(job.preview);
  (void)loop.borrowCapturePreview(job.preview, &job.admission);
  // Job-owned overdub-session undo baseline. The successor session's
  // beginOverdubSession sets its own; without this move it would overwrite the
  // baseline this interval's OverdubPassAdded entry must record.
  if (hasOverdubSessionBeforePublished_) {
    job.beforePublished = std::move(overdubSessionBeforePublished_);
    job.beforePublishedCoverage = overdubSessionBeforePublishedCoverage_;
    job.hasBeforePublished = true;
    overdubSessionBeforePublished_ = {};
    overdubSessionBeforePublishedCoverage_ = kSnapshotCoverageUnset;
    hasOverdubSessionBeforePublished_ = false;
  }
  // DEC-069: this interval's owed wrap commit is consumed by its own stop. The rows that had not
  // been sealed at a crossing travel with the sealed extent into the pass this job commits, so no
  // deferred wrap survives its interval.
  overdubWrapCommitPending_ = false;
  // Button A closes input at this boundary. The sealed frozen capture owns all
  // remaining canonical/companion work, while the existing prepared playback
  // window remains the active source. Publish PLAYING immediately so the
  // control surface never waits behind a resumable preparation pass.
  // `job.admission` remains the capture-admission and transport-stop
  // barrier until the joint commit and cleanup retire it.
  trackState = TRACK_OVERDUB_STOPPING;
#if RUNTIME_TIMING_ENABLED
  RuntimeTimingTelemetry::recordStopStateTransition(
      resolveTrackIndexForPersistence(*this), slot,
      TRACK_OVERDUBBING, TRACK_OVERDUB_STOPPING, job.admission.stopTick, micros());
#endif
  if (destination == PendingStopDestination::Playing) {
    trackState = TRACK_PLAYING;
#if RUNTIME_TIMING_ENABLED
    RuntimeTimingTelemetry::recordStopStateTransition(
        resolveTrackIndexForPersistence(*this), slot,
        TRACK_OVERDUB_STOPPING, TRACK_PLAYING, job.admission.stopTick, micros());
#endif
    armPlayingMidiDrainAfterOverdubStop();
  }
  return true;
}

TRACK_COLD_MEM bool Track::prewarmProtectedUndo() {
  if (!protectedUndo_) {
    auto* storage = ExternalMemoryFirstAllocator<TrackUndoPublication>{}.allocate(1);
    if (!storage) return false;
    protectedUndo_.reset(new (storage) TrackUndoPublication{});
  }
  if (!protectedUndo_->prewarm(kProtectedUndoPrewarmEntries)) return false;
  // DEC-063: the stack's own entry capacity is part of the same cold admission.
  // The atomic publish must never grow it, and `begin` rejects a transaction whose
  // cursor has reached capacity, so this is a publication precondition.
  //
  // Bounded, NOT the 512-entry policy maximum: reserving
  // Config::ABSOLUTE_MAX_UNDO_ENTRIES per track at boot (8 x ~512 UndoEntry rows,
  // ~1 MB) made the device stop streaming after `boot_reset` and report
  // `crash=1` on one boot (captures/task23-undo-crash, 2026-09-16). The policy
  // maximum needs a pressure-aware or lazily admitted strategy before it can be
  // reserved eagerly. Past this many retained entries a stop simply publishes no
  // protected entry (plain commit), which keeps the stop correct.
  GlobalUndoStack& stack = getGlobalUndoStack();
  if (stack.entries.capacity() < kProtectedUndoPrewarmEntries) {
    stack.entries.reserve(kProtectedUndoPrewarmEntries);
  }
  return true;
}

TRACK_COLD_MEM TrackUndoPublication* Track::protectedUndo() { return protectedUndo_.get(); }

TRACK_COLD_MEM void Track::ProtectedUndoDeleter::operator()(TrackUndoPublication* undo) const {
  if (!undo) return;
  undo->~TrackUndoPublication();
  ExternalMemoryFirstAllocator<TrackUndoPublication>{}.deallocate(undo, 1);
}

TRACK_COLD_MEM bool Track::stepProtectedUndoDrain() {
  TrackUndoPublication* undo = protectedUndo();
  if (undo == nullptr || !undo->protectedState()) return true;
  // Bounded: one release/reclaim/trim quantum per owner turn. The disabled-pass
  // reclaim continuation remains a separate DEC-063 item; trim pressure is
  // therefore measured without it, which can only trim at least as much as the
  // fully wired policy would.
  switch (undo->phase()) {
    case TrackUndoPublication::Phase::DetachRedo:
    case TrackUndoPublication::Phase::Ready:
      // Prepared but never published (publishability was not proven): restore the
      // detached redo so the stack is whole again, then finish.
      if (!undo->beginRestore()) return true;
      return false;
    case TrackUndoPublication::Phase::Restore:
      if (!undo->stepRestore(kDeferredStopPreparationQuantum)) return false;
      // Hand the prepared entry back before finishing: finish() refuses while a
      // private unpublished entry with a valid pass id is still held. The
      // recovered entry is discarded here because the stop already committed
      // without a protected publication.
      {
        UndoEntry recovered;
        (void)undo->recoverPreparedEntry(recovered);
      }
      (void)undo->finish();
      return true;
    case TrackUndoPublication::Phase::Published:
    case TrackUndoPublication::Phase::ReleaseRedo:
      if (undo->stepReleaseRedo(kDeferredStopPreparationQuantum)) {
        return undo->phase() == TrackUndoPublication::Phase::Complete
                   ? (undo->finish(), true)
                   : false;
      }
      return false;
    case TrackUndoPublication::Phase::AwaitReclaim:
      if (!undo->beginTrimAfterReclaim()) return true;
      return false;
    case TrackUndoPublication::Phase::Trim:
      if (undo->stepTrim(kDeferredStopPreparationQuantum)) return (undo->finish(), true);
      return false;
    case TrackUndoPublication::Phase::Complete:
      if (undo->finish()) {
#if defined(SESSION_CAPTURE)
        {
          GlobalUndoStack& stack = getGlobalUndoStack();
          char line[160];
          snprintf(line, sizeof(line), "#CAP,%lu,DIAG,gus,drain,idle=1,cursor=%zu,size=%zu",
                   static_cast<unsigned long>(micros()), stack.cursor, stack.entries.size());
          DebugSessionCapture::appendCaptureTextLine(line);
        }
#endif
        return true;
      }
      return true;
    default:
      return true;
  }
}

TRACK_COLD_MEM bool Track::stepDeferredUndoCollection(CaptureStopJob& job, Loop& loop,
                                                      PendingStopAdmission& stop) {
  if (!job.undoCollectionStarted) {
    job.undoCollectionStarted = true;
    // OpenSpec 5.12 / D8: this interval's final pass identity comes from the job-owned receipt
    // while it lives in the job; the Loop producer slot may already belong to a successor.
    const PassId finalPassId = job.ownsPendingPass ? job.pendingPass.id
                                                   : loop.pendingCapturePass().id;
    job.undoCollectable = loop.beginCollectOverdubStopUndoPasses(
        job.undoCollection, job.undoPassIds, job.undoCompanionIds,
        finalPassId, true, job.admission.intervalToken(), job.admission.sessionExitReceiptRing);
    (void)stop.consumeUndoCollectionGrain();
    if (!job.undoCollectable) {
      // No prepared session identities to record for this interval.
      job.undoCollected = true;
      return true;
    }
    // Collection setup is its own admitted owner turn.
    return false;
  }
  if (job.undoCollected) return true;
  const Loop::SessionUndoCollection::Phase phase =
      loop.stepCollectOverdubSessionUndoPasses(job.undoCollection,
                                               kDeferredStopPreparationQuantum);
  (void)stop.consumeUndoCollectionGrain();
  if (phase == Loop::SessionUndoCollection::Phase::Denied) {
    job.undoCollected = true;
    return true;
  }
  if (phase != Loop::SessionUndoCollection::Phase::Complete) return false;
  // Build the entry from the job's own slot and baseline, never the live session's.
  job.undoCollected = TrackUndo::prepareOverdubSessionEntry(
      loop, job.undoCollection, job.slot, job.beforePublished, job.undoEntry,
      job.beforePublishedCoverage);
#if defined(SESSION_CAPTURE)
  // The deferred route used to record only the final pass (the session identities had already
  // moved into this interval's session-exit receipt). Emit the same shape the synchronous
  // `pushOverdubSessionOnStop` emits, so the GUS entry's pass count is device-visible.
  if (job.undoCollected) {
    char line[192];
    snprintf(line, sizeof(line),
             "#CAP,%lu,DIAG,gus,slot,kind=%u,passId=%lu,passIds_n=%u,src=deferred",
             static_cast<unsigned long>(micros()), static_cast<unsigned>(job.undoEntry.kind),
             static_cast<unsigned long>(job.undoEntry.passId),
             static_cast<unsigned>(job.undoEntry.passIds.size()));
    DebugSessionCapture::appendCaptureTextLine(line);
  }
#endif
  return job.undoCollected;
}

TRACK_COLD_MEM bool Track::stepDeferredUndoPublication(CaptureStopJob& job,
                                                       PendingStopAdmission& stop) {
  TrackUndoPublication* undo = protectedUndo();
  if (undo == nullptr) {
    job.undoPublicationReady = false;
    return true;
  }
  if (!job.undoPublicationStarted) {
    job.undoPublicationStarted = true;
    // The transaction is exclusive: a predecessor must have finished draining.
    job.undoPublicationReady = undo->begin(getGlobalUndoStack(), job.undoEntry);
    (void)stop.consumeUndoCollectionGrain();
#if defined(SESSION_CAPTURE)
    {
      GlobalUndoStack& stack = getGlobalUndoStack();
      char line[224];
      snprintf(line, sizeof(line),
               "#CAP,%lu,DIAG,gus,publish_begin,ok=%u,phase=%u,cursor=%zu,size=%zu,cap=%zu,"
               "passId=%lu,passIds_n=%u",
               static_cast<unsigned long>(micros()), job.undoPublicationReady ? 1u : 0u,
               static_cast<unsigned>(undo->phase()), stack.cursor, stack.entries.size(),
               stack.entries.capacity(), static_cast<unsigned long>(job.undoEntry.passId),
               static_cast<unsigned>(job.undoEntry.passIds.size()));
      DebugSessionCapture::appendCaptureTextLine(line);
    }
#endif
    // A rejected begin leaves the entry intact and means "nothing to publish".
    // A successful begin still owes its prepare turns before it can publish.
    if (!job.undoPublicationReady) return true;
    return false;
  }
  if (!undo->protectedState()) {
    job.undoPublicationReady = false;
    return true;
  }
  if (undo->phase() == TrackUndoPublication::Phase::Ready) {
    job.undoPublicationReady = true;
    return true;
  }
  if (!undo->stepPrepare(kDeferredStopPreparationQuantum)) return false;
  job.undoPublicationReady = undo->phase() == TrackUndoPublication::Phase::Ready;
  return job.undoPublicationReady;
}

TRACK_COLD_MEM void Track::requestDeferredOverdubStopToStopped() {
  if (!hasPendingDeferredOverdubStop()) return;
  CaptureStopJob& job = captureStopJob();
  const uint8_t phaseAtRetarget = static_cast<uint8_t>(job.admission.phase);
  job.admission.requestTransportStop();
  // DEC-070: a job admitted for PLAYING and retargeted to STOPPED keeps the companion (note-edit)
  // preparation its interval contracted. That representation is what the display resolves the
  // overdubbed notes from, and its `UndoCollection` phase is what publishes the protected undo
  // entry, so retiring those estimates or dropping the horizon loses both (the reverted attempt at
  // `baafd68a` did exactly that: display-only gains, no undo, stalled save).
  //
  // It cannot simply continue either. The slot's *retained* canonical tail cache -- kept across
  // punch-out by the predecessor's cleanup -- leaves `canonicalTailPhase` complete and owns the
  // companion workers, so this job cannot begin its own preparation (probe `successor-wrap-43`:
  // `probe_before`/`probe_after` show `frozenCaptureCompanions.idle()`,
  // `frozenCompanionPasses.idle()` and `canonicalTailPhase == Idle` all clear, while the command
  // epoch retarget itself works). Release that cache -- it is recomputable, so this costs work and
  // no content -- rebind the canonical command epoch, and let the job walk
  // `CanonicalTail -> CompanionHistory -> UndoCollection -> CompanionBridge -> AtomicTransfer`,
  // where the verified companions and the undo entry publish through `commitStoppedWithUndo`.
  // The grain's bounded per-phase fallback remains the safety net if a phase truly cannot advance.
  LoopPlaybackRuntime* retargetRuntime = playbackRuntime.slotIfAllocated(job.slot);
  retargetStallGrains_ = 0;
  retargetStallPhase_ = phaseAtRetarget;
  if (retargetRuntime != nullptr) {
    // Two caches hold the predecessor's companion work, one on each side of the boundary:
    // the runtime's frozen canonical tail and the Loop's prepared companion-history horizon (the
    // retained worker stays owned by the interval that prepared it, so a retargeted successor's
    // `beginPreparedCompanionHistory` cannot claim it -- `captures/successor-wrap-44` still stalled
    // in `CompanionHistory` after only the runtime side was released). Return both to their
    // prewarmed state; both are recomputable, so this costs work and no content.
    retargetRuntime->discardFrozenCanonicalTail();
    if (!job.owner->restoreSupersededPreparedCompanionHistory(job.admission.intervalToken())) {
      job.owner->discardPreparedCompanionHistory();
    }
    (void)TrackPreparedStopCommit::retargetStoppedRecovery(*retargetRuntime, job.admission);
  }
  logger.log(CAT_TRACK, LOG_INFO, "Overdub stop retarget: phase=%u dest=%u companions=kept",
             static_cast<unsigned>(phaseAtRetarget),
             static_cast<unsigned>(job.admission.destination));
}

namespace {
/// Grain budget for one phase of a retargeted job's companion preparation before the raw-only
/// fallback takes over (DEC-070). Generous: the device completes healthy phases in a few grains.
constexpr uint16_t kRetargetStallGrainLimit = 512;
}  // namespace

TRACK_COLD_MEM bool Track::processDeferredOverdubStopGrain() {
  // Bind the FIFO head once for the whole grain. The head only advances at the
  // end of this function, so a single stable reference is the contract: no
  // intermediate step may re-resolve the ring and pick up a different job.
  CaptureStopJob* head = captureStopJobHead();
  if (head == nullptr) return false;
  CaptureStopJob& job = *head;
  auto& stop = job.admission;
  if (!stop.pending) {
    return false;
  }
  // DEC-070 diagnostic: every silent early-out below holds the job's finalization resources, so a
  // predicate that can never become true wedges the slot (no stop is ever admitted again). Name it
  // once instead of returning silently forever -- an aborted HITL run left exactly this state.
  const auto noteStallOnce = [&](const char* reason) {
    if (deferredStopStallNoted_) return;
    deferredStopStallNoted_ = true;
    logger.log(CAT_TRACK, LOG_WARNING, "Deferred stop stalled: %s", reason);
  };
  // Never reinterpret a saved job through the currently selected slot.
  if (!job.owner || !loopsAllocated() ||
      loopIdForSlot(job.slot) != job.loopId ||
      &getLoop(job.slot) != job.owner ||
      job.owner->loopLengthTicks != job.loopLengthTicks) {
    noteStallOnce("job owner or loop geometry no longer matches");
    return false;
  }
  Loop& loop = *job.owner;
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(job.slot);
  CaptureAdmissionStaging* views = runtime != nullptr ? runtime->captureStagingIfPrewarmed() : nullptr;
  if (runtime == nullptr || views == nullptr ||
      views->frozenGeneration != stop.frozenGeneration) {
    noteStallOnce("frozen capture staging generation lost");
    return false;
  }
  // DEC-070 bounded fallback. A retargeted job keeps its PLAYING-route companion preparation so the
  // STOPPED publication can carry the note-edit representation the display resolves the overdubbed
  // notes from. If that preparation cannot complete -- its bindings no longer describe the interval
  // after the transport stopped -- the job must not park: retire the companion estimates, drop the
  // horizon and commit the staged pass alone, which is the raw-only route the slot used to take.
  // Progress resets the count, so only a phase that truly makes no progress reaches the bound.
  if (stop.retargetedFromPlaying &&
      stop.phase >= PendingStopPhase::CanonicalTail &&
      stop.phase <= PendingStopPhase::CompanionBridge) {
    if (retargetStallPhase_ != static_cast<uint8_t>(stop.phase)) {
      retargetStallPhase_ = static_cast<uint8_t>(stop.phase);
      retargetStallGrains_ = 0;
    } else if (retargetStallGrains_ < kRetargetStallGrainLimit) {
      ++retargetStallGrains_;
    }
    if (retargetStallGrains_ >= kRetargetStallGrainLimit) {
      stop.setPhase(PendingStopPhase::PrepareTransfer);
      (void)stop.retireSkippedCompanionPreparation(false);
      loop.discardPreparedCompanionHistory(stop.intervalToken());
      logger.log(CAT_TRACK, LOG_WARNING,
                 "Overdub stop retarget: companion path stalled; raw pass only");
      stop.setPhase(PendingStopPhase::AtomicTransfer);
    }
  }
  CaptureAdmissionStaging& staging = *views;
  const uint32_t phaseTick = job.phaseTick;
  // Approved superseded rule. The recorded predecessor is the *supersession
  // reference*, not the resolver: the canonical pipeline binds its owner identity
  // by address to the live Loop scalars and the ordering phases need one coherent
  // live epoch, so preparation always resolves live. Only the published revision
  // and root decide whether a successor window may still be published;
  // `sourceGeneration` is a live cache epoch that advances during finalization.
  const bool causalBaseCurrent = !job.causalBaseRecorded ||
      (job.causalBase.playbackRevision == loop.playbackRevision &&
       job.causalBase.snapshotIdentity == loop.publishedSnapshotIdentity());

  // A clean stop commonly follows a completed wrap with no successor rows.
  // There is no canonical or audible ordering work in that candidate; advance
  // directly to the admitted transfer tail instead of invoking zero-row
  // builders on the active transport path.
  if (staging.storage.eventCount == 0 &&
      (stop.phase == PendingStopPhase::CanonicalPrefix ||
       stop.phase == PendingStopPhase::CanonicalOrder)) {
    stop.setPhase(PendingStopPhase::PrepareTransfer);
    return false;
  }

  if (stop.phase == PendingStopPhase::ReservedClosure) {
    CaptureNoteReservation* reservation = staging.firstOpenReservation();
    if (reservation == nullptr) {
      if (staging.openReservationCount() == 0) {
        stop.setPhase(PendingStopPhase::CanonicalOrder);
      }
      return false;
    }
    if (!appendReservedCaptureNoteOffForOwner(loop, staging, *reservation, phaseTick)) {
      return false;
    }
    (void)stop.consumeClosureGrain();
    if (stop.remainingClosureGrains == 0) {
      stop.setPhase(
          stop.destination == PendingStopDestination::Stopped
              ? PendingStopPhase::CanonicalOrder : PendingStopPhase::CanonicalPrefix);
    }
    return false;
  }
  const PlaybackViewSourceIdentity baseSource{
      loop.playbackRevision, playbackGeneration, loop.publishedSnapshotIdentity()};
  const uint32_t targetRevisionDelta = runtime->frozenCaptureCompanions.ready(staging)
      ? (runtime->frozenCaptureCompanions.size() == 0 ? 1u : 2u)
      : (loop.hasPendingNoteChanges() ? 2u : 1u);
  const PlaybackViewSourceIdentity targetSource{
      loop.playbackRevision + targetRevisionDelta, playbackGeneration, nullptr};
  const RuntimeCanonicalPassInputs::CurrentSource currentSource{
      loop.playbackRevision, playbackGeneration, loop.loopLengthTicks,
      loop.canonicalStorageGeneration};
  const RuntimeCanonicalPassInputs::FrozenSource frozenSource{
      loop.playbackRevision, playbackGeneration,
      stop.transportCommandEpoch, loop.loopLengthTicks, staging};

  if (stop.phase == PendingStopPhase::CanonicalPrefix) {
    if (stop.destination == PendingStopDestination::Stopped) {
      stop.setPhase(PendingStopPhase::CanonicalOrder);
      return false;
    }
    const auto anchor = runtime->currentCycleCursor(phaseTick, baseSource.sourceGeneration);
    if (runtime->stepCanonicalBase(loop.passes, baseSource, currentSource, anchor,
                                   kDeferredStopPreparationQuantum) ==
        RuntimePlaybackOrderBuildResult::Complete)
      stop.setPhase(PendingStopPhase::CanonicalOrder);
    return false;
  }
  if (stop.phase == PendingStopPhase::CanonicalOrder) {
    if (!runtime->beginFrozenCaptureCanonicalOrder(stop.frozenGeneration)) {
      return false;
    }
    const RuntimePlaybackOrderBuildResult result =
        runtime->stepFrozenCaptureCanonicalOrder(kDeferredStopOrderQuantum);
    if (result == RuntimePlaybackOrderBuildResult::Denied) {
      return false;
    }
    (void)stop.consumeOrderGrain();
    if (result == RuntimePlaybackOrderBuildResult::Complete) {
      stop.setPhase(PendingStopPhase::CanonicalRewrite);
    }
    return false;
  }
  if (stop.phase == PendingStopPhase::CanonicalRewrite) {
    const LoopEventStoreRewriteResult result = runtime->stepFrozenCaptureCanonicalCopy(
        stop.frozenGeneration, kDeferredStopOrderQuantum);
    if (result == LoopEventStoreRewriteResult::Denied) {
      return false;
    }
    (void)stop.consumeOrderGrain();
    if (result == LoopEventStoreRewriteResult::Complete) {
      const size_t count = runtime->frozenCaptureCanonicalOrder.orderedEventCount();
      // The prepared output owns different physical chunks. Retire only one
      // copied raw chunk per turn; never rewrite sealed persistence input.
      if (!retireJobCaptureSourceGrain(job, loop, false)) return false;
      // An empty successor capture has no audible rows to project.  Skip the
      // zero-work audible-order builder and continue with the already-admitted
      // transfer tail; this keeps a clean stop from parking on an unnecessary
      // owner turn after canonical rewrite.
      stop.setPhase(count == 0 ? PendingStopPhase::PrepareTransfer
                                                : PendingStopPhase::AudibleOrder);
    }
    return false;
  }
  if (stop.phase == PendingStopPhase::AudibleOrder) {
    if (!runtime->beginFrozenCaptureOrder(loop.loopLengthTicks,
                                          stop.frozenGeneration)) {
      return false;
    }
    const RuntimePlaybackOrderBuildResult result =
        runtime->stepFrozenCaptureOrder(kDeferredStopOrderQuantum);
    if (result == RuntimePlaybackOrderBuildResult::Denied) {
      return false;
    }
    (void)stop.consumeOrderGrain();
    if (result == RuntimePlaybackOrderBuildResult::Complete) {
      stop.setPhase(PendingStopPhase::PrepareTransfer);
    }
    return false;
  }
  if (stop.phase == PendingStopPhase::PrepareTransfer) {
    // Ordering/rewrite have finished. Unused estimates are not service work.
    if (!stop.retireCompletedPreparation()) return false;
    const SealOutcome staged = runtime->canonicalCaptureOutput.store.empty()
                                   ? SealOutcome::SkippedEmpty
                                   : loop.stagePreparedCapturePassFromActiveAdmission(
                                         runtime->canonicalCaptureOutput, stop.stopTick,
                                         stop.destination == PendingStopDestination::Playing
                                             ? CommitReason::OverdubStop
                                             : CommitReason::OverdubStopToStopped);
    if (staged != SealOutcome::Ok && staged != SealOutcome::SkippedEmpty) {
      return false;
    }
    // OpenSpec 5.12 / D8: take ownership of the pass in the same non-yielding grain that staged
    // it. The Loop pending slot is only this job's transient construction point; from here the
    // receipt lives in the job, so a successor interval's `beginCapture` cannot discard it and
    // this job's terminal transfer never has to reacquire the (possibly successor-owned) slot.
    if (staged == SealOutcome::Ok) {
      if (!loop.takePendingCapturePass(job.pendingPass)) {
        return false;  // Cannot happen: the staging call just proved a pass exists.
      }
      job.ownsPendingPass = true;
    }
    // A zero-event stop has no canonical tail or companion bridge. Keep the
    // existing bounded transfer path for that case; populated captures use
    // the prepared joint handoff below.
    // DEC-070: a job admitted for PLAYING and retargeted to STOPPED keeps the companion work its
    // interval contracted. Skipping it published the capture rows alone, so the overdubbed notes
    // stayed audible but vanished from the display (which resolves them from the note-edit
    // representation). Only a job *admitted* for STOPPED, or a genuinely empty capture, skips it.
    if (staged == SealOutcome::SkippedEmpty ||
        (stop.destination == PendingStopDestination::Stopped &&
         !stop.retargetedFromPlaying)) {
      if (!stop.retireSkippedCompanionPreparation(
              staged == SealOutcome::SkippedEmpty)) return false;
      stop.setPhase(PendingStopPhase::AtomicTransfer);
    } else {
      // The control boundary already released the track to the current
      // prepared PLAYING window. Continue only the frozen companion work here.
      stop.setPhase(PendingStopPhase::CanonicalTail);
    }
    return false;
  }
  if (stop.phase == PendingStopPhase::CanonicalTail) {
    if (!runtime->frozenCanonicalTailReady(targetSource)) {
      if (runtime->beginFrozenCanonicalTail(frozenSource, job.pendingChanges, targetSource)) {
        return false;
      }
      const auto result = runtime->stepReservedFrozenCanonicalTail(
          stop, kDeferredStopPreparationQuantum);
      if (result == RuntimePlaybackOrderBuildResult::Denied) return false;
      if (result != RuntimePlaybackOrderBuildResult::Complete) return false;
    }
    stop.setPhase(PendingStopPhase::CompanionHistory);
    return false;
  }
  if (stop.phase == PendingStopPhase::CompanionHistory) {
    if (!loop.preparedCompanionHistoryReady(job.admission.intervalToken())) {
      const bool canStep = loop.preparedCompanionHistoryCanStep(runtime->frozenCaptureCompanions,
                                                                staging, runtime->canonicalPassInputs,
                                                                job.admission.intervalToken());
      if (!canStep) {
        // OpenSpec 5.12 / D8: name this interval's own staged receipt unconditionally. The Loop
        // producer slot may already belong to a successor, and passing `&job.pendingPass` even when
        // the job owns nothing lets `beginPreparedCompanionHistory` deny explicitly instead of
        // silently binding a successor's receipt.
        if (!loop.beginPreparedCompanionHistory(runtime->frozenCaptureCompanions,
                                                 staging, &runtime->canonicalPassInputs,
                                                 job.admission.intervalToken(),
                                                 &job.pendingPass)) {
          return false;
        }
        return false; // Setup is its own admitted owner turn.
      }
      const auto result = TrackPreparedStopCommit::stepPreparation(
          loop, *runtime, stop, kDeferredStopPreparationQuantum);
      if (result == RuntimePlaybackOrderBuildResult::Denied) return false;
      return false;
    }
    if (!stop.retireCompletedPreparation()) return false;
#if defined(SESSION_CAPTURE)
    const auto* windowBase = runtime->preparedCanonicalWindow(baseSource);
    const auto* publishedBase = windowBase ? windowBase : runtime->publishedFullLoopView(baseSource, loop.loopLengthTicks);
    SC_ODUB_STOP_STAGE("bridge_base", 0, 0, UINT32_MAX, UINT32_MAX,
        publishedBase != nullptr, publishedBase && publishedBase->passOriginsReady,
        windowBase ? "prepared_window" : publishedBase ? "present" : "missing_canonical_base");
#endif
    stop.setPhase(PendingStopPhase::UndoCollection);
    return false;
  }
  if (stop.phase == PendingStopPhase::UndoCollection) {
    // Bounded, resumable collection of the session's undo identities, then the
    // prepared OverdubPassAdded entry built from this job's slot and baseline,
    // then the protected transaction's prepare (begin + detach redo).
    if (!stepDeferredUndoCollection(job, loop, stop)) return false;
    if (!stepDeferredUndoPublication(job, stop)) return false;
    stop.setPhase(PendingStopPhase::CompanionBridge);
    return false;
  }
  if (stop.phase == PendingStopPhase::CompanionBridge) {
    if (stop.destination == PendingStopDestination::Stopped) {
      // History is complete; a stopped destination has no audible bridge.
      if (!stop.retireCompletedPreparation()) return false;
      stop.setPhase(PendingStopPhase::AtomicTransfer);
      return false;
    }
    if (!causalBaseCurrent) {
      // Retire the ranked (bridge) estimate: no bridge will be built. The atomic
      // transfer then commits the accepted capture without a successor window.
      if (!stop.retireSupersededPreparation()) return false;
      stop.setPhase(PendingStopPhase::AtomicTransfer);
      return false;
    }
    const auto anchor = runtime->currentCycleCursor(
        loop.lastTickInLoop == UINT32_MAX ? phaseTick : loop.lastTickInLoop, targetSource.sourceGeneration);
    if (!runtime->canSelectFrozenCaptureBridge(
            stop.frozenGeneration,
            stop.transportCommandEpoch, loop.loopLengthTicks, anchor)) {
      if (runtime->refreshFrozenCompanionBridge(anchor)) return false;
      if (runtime->beginFrozenCompanionBridge(
              baseSource, targetSource, stop.frozenGeneration,
              stop.transportCommandEpoch, loop.loopLengthTicks, anchor)) {
        return false;
      }
      const auto result = runtime->stepReservedFrozenCaptureBridge(
          stop, kDeferredStopPreparationQuantum);
      if (result == RuntimePlaybackOrderBuildResult::Denied) return false;
      return false;
    }
    if (!stop.retireCompletedPreparation()) return false;
    stop.setPhase(PendingStopPhase::AtomicTransfer);
    return false;
  }
  if (stop.phase == PendingStopPhase::AtomicTransfer) {
    if (!causalBaseCurrent) {
      if (!stop.retireSupersededPreparation()) return false;
      SC_ODUB_STOP_STAGE("atomic_transfer", 0, 0, UINT32_MAX, UINT32_MAX,
                         stop.remainingHistoryRecycleGrains, job.causalBase.playbackRevision,
                         "causal_base_superseded");
      PreparedCapturePassTransfer supersededTransfer{};
      TrackUndoPublication* supersededUndo = protectedUndo();
      // OpenSpec 5.12 / D8: the undo identity is this interval's own job-owned receipt.
      const PassId jobFinalPassId = job.ownsPendingPass ? job.pendingPass.id
                                                        : loop.pendingCapturePass().id;
      const bool supersededPublishable = job.undoPublicationReady && supersededUndo != nullptr &&
          supersededUndo->canPublish(getGlobalUndoStack(), loop.loopId, jobFinalPassId);
      if (!TrackPreparedStopCommit::commitSupersededCausalBase(
              loop, stop, supersededTransfer,
              supersededPublishable ? supersededUndo : nullptr,
              job.ownsPendingPass ? &job.pendingPass : nullptr))
        return false;
      // The unpublished companion preparation returns to its prewarmed horizon
      // here, so the next interval never needs a STOPPED-only re-prewarm.
      if (!loop.restoreSupersededPreparedCompanionHistory(job.admission.intervalToken())) {
        loop.discardPreparedCompanionHistory(job.admission.intervalToken());
      }
      return false;
    }
    const auto anchor = runtime->currentCycleCursor(
        loop.lastTickInLoop == UINT32_MAX ? phaseTick : loop.lastTickInLoop, targetSource.sourceGeneration);
    PendingCapturePass* jobPass = job.ownsPendingPass ? &job.pendingPass : nullptr;
    if (TrackPreparedStopCommit::requiresBridgeRefresh(
            loop, *runtime, stop, anchor, jobPass)) {
      stop.setPhase(PendingStopPhase::CompanionBridge);
      return false;
    }
    PreparedCapturePassTransfer transferred{};
    TrackUndoPublication* undo = protectedUndo();
    // The caller must prove publishability before entering the undo path; a
    // Ready-but-unpublishable transaction is restored by the drain instead.
    const PassId jobFinalPassId = jobPass != nullptr ? jobPass->id
                                                     : loop.pendingCapturePass().id;
    const bool publishable = job.undoPublicationReady && undo != nullptr &&
        undo->canPublish(getGlobalUndoStack(), loop.loopId, jobFinalPassId);
    if (publishable) {
      (void)TrackPreparedStopCommit::advanceAtomicTransferWithUndo(
          loop, *runtime, stop, playbackGeneration, anchor, transferred,
          getGlobalUndoStack(), *undo, jobPass);
    } else {
      if (job.undoPublicationReady) {
        SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX,
                           job.undoPublicationReady, undo != nullptr, "undo_not_publishable");
      }
      (void)TrackPreparedStopCommit::advanceAtomicTransfer(
          loop, *runtime, stop, playbackGeneration, anchor, transferred, jobPass);
    }
    return false;
  }

  // Drain the protected undo transaction before final cleanup retires the job.
  // Bounded: one release/reclaim/trim quantum per owner turn, so protectedState()
  // is Idle again before the next interval can admit.
  if (!stepProtectedUndoDrain()) return false;

  if (stop.historyRecycleOwner == &loop) {
    if (!stop.historyRecycleStarted) {
      if (!TrackPreparedStopCommit::beginHistoryRecycle(loop, stop)) {
        // DEC-070 diagnostic: `beginHistoryRecycle` needs the contracted recycle quantum and the
        // *current* prepared-companion-history generation. A job admitted for the PLAYING route and
        // then retargeted to STOPPED by `stopOverdubbingToStopped` can satisfy neither, and it used
        // to return here silently forever -- holding `captureStopOwner`, the frozen role and the
        // session-exit receipt, so every later stop was refused and the slot needed a reboot.
        noteStallOnce("history recycle cannot start for this job contract");
        return false;
      }
      return false;
    }
    const auto result = TrackPreparedStopCommit::stepHistoryRecycle(
        loop, stop, kDeferredStopPreparationQuantum);
    if (result == RuntimePlaybackOrderBuildResult::Denied) {
      noteStallOnce("history recycle denied for this job contract");
      return false;
    }
    if (result != RuntimePlaybackOrderBuildResult::Complete) return false;
    stop.historyRecycleOwner = nullptr;
  }

  // One retired pending-note node per turn; never clear the live ingress map.
  if (!job.pendingNotes.empty()) {
    job.pendingNotes.erase(job.pendingNotes.begin());
    return false;
  }
  // Restore one consumed physical output credit per owner turn. If physical
  // storage is exhausted, finish this accepted job; the next admission must
  // explicitly deny until a later refill succeeds.
  if (runtime->replenishCanonicalCaptureChunk()) return false;
  const bool ownsTrackState = job.interval == captureInterval_ &&
      job.slot == activeLoopIndex;
  const PendingStopDestination destination = stop.destination;
  if (!TrackPreparedStopCommit::completeDeferredCleanup(loop, *runtime, stop, job.preview,
                                                         ownsTrackState)) {
    if (stop.phase == PendingStopPhase::DeferredCleanup &&
        (stop.finalizationComplete() || stop.historyRecycleOwner)) {
      noteStallOnce("deferred cleanup prerequisites unmet");
    }
    return false;
  }
  // Retire this job's immutable frozen admission rows now that no consumer
  // borrows them. The role then returns to clean ingress ownership on the next
  // exchange; no live ingress state is touched here.
  views->reset();
  // The deferred completion is the normal route for an overdub stop (both destinations).
  // The synchronous fallback `commitCaptureForStop(OverdubStopToStopped, ...)` requests the
  // stored-MIDI verification from `finalizeCommitSideEffects`; mirror it here so both commit
  // routes request the dump from the one place that owns the overdub-stop commit. Without
  // this the transport-stop-during-overdub path never queues the dump at all (capture
  // `sevt-diag-stop` showed zero `stored_verify` markers despite a completed stop grain).
  queueDeferredStoredMidiVerification();
  (void)loop.releasePendingNoteChangesBorrow(&job.admission);
  (void)loop.releaseCapturePreviewBorrow(&job.admission);
  // A baseline left unconsumed by the commit must not leak into a later interval.
  job.hasBeforePublished = false;
  job.beforePublished = {};
  job.causalBase = {};
  job.causalBaseSnapshot = {};
  job.causalBaseRecorded = false;
  // OpenSpec 5.12 / D8: end this interval's staged-pass ownership. The terminal transfer already
  // consumed the receipt on every committing route; release anything a skipped route left so a
  // job can never retire holding chunk references the pool cannot reclaim.
  if (job.ownsPendingPass) {
    LoopEventStore::releaseChunkRefs(job.pendingPass.committedChunkIds);
  }
  job.pendingPass = PendingCapturePass{};
  job.ownsPendingPass = false;
  // S2b.6 retire-path invariant (invariant 34; precondition of S2b.1). A retiring job leaves the
  // slot's canonical-copy target empty. Every committing route consumes the store at
  // `PrepareTransfer`, but a skipped or failed route can leave it populated with no ring entry --
  // and once boot stops reserving the canonical output per slot, a populated store makes
  // `prewarmCanonicalCaptureOutput` deny forever, so the accepted stop would wait on a resource
  // nothing can ever free. Drain it here so the next admission's reservation has an empty target.
  if (!runtime->canonicalCaptureOutput.store.empty()) {
    runtime->canonicalCaptureOutput.store.clear();
    runtime->canonicalCaptureOutput.phase = CapturePhase::None;
    // The store's copy cursor is authoritative only while a copy is in flight; at retirement the
    // job is past `DeferredCleanup`, so releasing the frozen canonical builders here keeps the
    // joint state consistent instead of leaving a cursor aimed at an emptied store.
    runtime->discardFrozenCaptureCanonicalOrder();
    SC_ODUB_STOP_STAGE("deferred_cleanup", 0, 0, UINT32_MAX, UINT32_MAX, 0, 0,
                       "canonical_output_drained_at_retire");
  }
  job.owner = nullptr;
  job.loopId = kInvalidLoopId;
  job.mode = CapturePhase::None;
  captureStopJobs_->head = (captureStopJobs_->head + 1) % CaptureStopJobs::kCapacity;
  --captureStopJobs_->count;
  if (!ownsTrackState) return true;
  (void)retireJobCaptureSourceGrain(job, loop, true);
  if (destination == PendingStopDestination::Stopped) {
    silenceTrackMidiOutput();
    setState(TRACK_STOPPED);
    resetPlaybackState(clockManager.getCurrentTick());
  } else {
    // Playback continued during finalization. Keep its cursor and held-note
    // ledger so cleanup cannot truncate a note or replay earlier events.
    // Normal Button A release has already published PLAYING.
    if (!isPlaying()) {
      setState(TRACK_PLAYING);
    }
    armPlayingMidiDrainAfterOverdubStop();
  }
  return true;
}

TRACK_COLD_MEM void Track::clearStoppingIntervalLedger() {
  // S4.1: an interval's stop clears **its own** slot's ledger. The track-global
  // `TrackPlaybackRuntime::clearAllLedgers()` is reserved for the deliberate whole-track sweep
  // (`Track::sendAllNotesOff`); using it here erased a sibling slot's ledger while that sibling was
  // still finalizing, which is the S4.1 rerun blocker.
  if (LoopPlaybackRuntime* stopRuntime = playbackRuntime.slotIfAllocated(activeLoopIndex)) {
    stopRuntime->clearLedger();
  }
}

bool Track::handleNoteEditFold(bool endInPlaying, uint32_t currentTick, uint32_t closeTick,
                               uint32_t stopStartUs) {
  (void)closeTick;
  if (!editManager.isNoteEditActive()) {
    return false;
  }
  Loop& loop = getActiveLoop();
#if defined(SESSION_CAPTURE)
  const bool probeVisualDirtyBeforeStop = loop.visualCacheDirty;
  const uint16_t probeDirtyBarsBeforeStop = countVisualCacheDirtyBars(loop);
  const bool probeSavePendingBeforeStop = StorageManager::hasDeferredSaveWork();
#endif
  finalizePendingNotes(currentTick);
  if (!loop.capture.store.empty() && loop.loopLengthTicks > 0) {
    loop.ensureCaptureEventsSorted();
    loop.assignMissingNoteIdsInStore(loop.capture.store);
    loop.finalizeCaptureWrapWindowAtStop(currentTick);
  }
  editManager.foldLiveCaptureIntoNoteEditSession(*this);
  pendingNotes.clear();
  if (endInPlaying) {
    const uint32_t stateStartUs = micros();
    silenceTrackMidiOutput();
      clearStoppingIntervalLedger();
    pendingNotes.clear();
    resetPlaybackState(currentTick);
    setState(TRACK_PLAYING);
    logOverdubStopStage(loop, stopStartUs, "set_state", micros() - stateStartUs,
                        DebugSessionCapture::kUnsampledHeapBytes,
                        DebugSessionCapture::kUnsampledHeapBytes, "in_edit");
    const uint32_t flushStartUs = micros();
    SC_REC_FLUSH_PENDING_REVTS(128);
    logOverdubStopStage(loop, stopStartUs, "flush", micros() - flushStartUs,
                        DebugSessionCapture::kUnsampledHeapBytes,
                        DebugSessionCapture::kUnsampledHeapBytes, "ok");
    logMemoryAfterOverdubStop(recordAddedNoteOnCount, loop);
    logger.logTrackEvent("Overdubbing stopped", currentTick);
    logger.info("Overdub stopped (in-edit fold): events=%d, undo_entries=%d",
                static_cast<int>(loop.displayEventCountHint()), TrackUndo::getUndoCount(*this));
    const uint32_t storagePhaseTickAtStop =
        (loop.loopLengthTicks > 0)
            ? tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks)
            : 0;
    displayManager.refreshViewportAfterOverdubStop(*this, activeLoopIndex, storagePhaseTickAtStop);
    emitOverdubStopDisplaySnapshot(*this, activeLoopIndex, currentTick);
    logOverdubStopStage(loop, stopStartUs, "display", 0,
                        DebugSessionCapture::kUnsampledHeapBytes,
                        DebugSessionCapture::kUnsampledHeapBytes, "hold");
#if defined(SESSION_CAPTURE)
    armOverdubStopDebtProbe(probeVisualDirtyBeforeStop, probeDirtyBarsBeforeStop,
                            probeSavePendingBeforeStop, loop);
#endif
    HotPathTelemetry::requestDeferredSummary("overdub_stop");
    armPlayingMidiDrainAfterOverdubStop();
  } else {
    logMemoryAfterOverdubStop(recordAddedNoteOnCount, loop);
    setState(TRACK_STOPPED);
    resetPlaybackState(currentTick);
    const uint32_t storagePhaseTickAtStop =
        (loop.loopLengthTicks > 0)
            ? tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks)
            : 0;
    // RC5e: same promote/preserve handoff as overdub→PLAYING (session_20260811_174742).
    displayManager.refreshViewportAfterOverdubStop(*this, activeLoopIndex, storagePhaseTickAtStop);
    displayManager.emitDisplayCaptureSnapshot(*this, activeLoopIndex, currentTick);
    logger.logTrackEvent("Overdubbing stopped (to STOPPED)", currentTick);
    HotPathTelemetry::requestDeferredSummary("overdub_stop_to_stopped");
  }
  return true;
}
void Track::startOverdubbing(uint32_t currentTick) {
  // A pending predecessor is not by itself a reason to refuse live ingress: its
  // frozen stop candidate owns the *frozen* admission role, while this interval
  // only ever resets the *live* role below. The predecessor-pending refusal that
  // used to sit here predated the dual-role split. What still bounds reentry is
  // capacity: this start needs the live role and the frozen role is what a *stop*
  // needs, so a successor may record while the predecessor finalizes but its own
  // stop is denied until the predecessor retires (`beginDeferredOverdubStop`).
  Loop& loopRef = getActiveLoop();
  // S4f.3: this is the switch handoff's start path. It must not silently do nothing: after a handoff the
  // track can still report `TRACK_OVERDUBBING` from the predecessor while the *active* loop is a
  // different, empty successor. An overdub needs an existing loop to overdub onto, so an empty active
  // loop is not a completed overdub -- it is a successor that has not established its length, and the
  // caller must route it through record admission instead. Returning silently here is what made
  // "B never starts" read as a carrier gap for three passes.
  if (trackState == TRACK_OVERDUBBING && loopRef.capture.phase == CapturePhase::Overdub) {
    if (loopRef.loopLengthTicks == 0) {
      logger.log(CAT_TRACK, LOG_WARNING,
                 "Overdub start on an empty active loop (slot=%u): the interval belongs to a "
                 "predecessor, so this successor needs record admission, not overdub",
                 static_cast<unsigned>(activeLoopIndex));
    }
    return;
  }
  // Capture staging is provisioned by capture admission, not by boot (DEC-065). This
  // is the overdub operation's own cold admission turn: reserve both roles *before*
  // any mutation and deny explicitly if the pool and the heap cannot supply them.
  // Reserving both keeps punch-out allocation-free (the live role is frozen while the
  // successor uses the other, already-reserved role), and the checks below are the
  // require step that runs after the reserve.
  LoopPlaybackRuntime* stagingAdmission = playbackRuntime.trySlot(activeLoopIndex);
  bool stagingAdmitted = false;
  if (stagingAdmission != nullptr) {
    ExternalPoolAccounting::beginTag(ExternalPoolAccounting::TagCaptureStaging);
    stagingAdmitted = stagingAdmission->prewarmCaptureStaging();
    ExternalPoolAccounting::endTag();
  }
  if (!stagingAdmitted) {
    MemoryMonitor::reportPrewarmAdmissionDenied(MemoryMonitor::OperationKind::OverdubStart);
    return;
  }
  // The companion-row reserve is derived from the slot's canonical content, which can grow
  // when a pass commits; re-grant it here so this interval's transform set cannot outgrow a
  // stale grant. Cold point only - this admission turn - never a deferred grain.
  // `ensureCapacity` only grows, so a sparse slot keeps the smaller reserve.
  bool companionRowsAdmitted = false;
  if (stagingAdmission != nullptr) {
    ExternalPoolAccounting::beginTag(ExternalPoolAccounting::TagCompanionHistory);
    companionRowsAdmitted = stagingAdmission->frozenCaptureCompanions.ensureCapacity(
        loopRef.companionRowHorizonBound());
    ExternalPoolAccounting::endTag();
  }
  if (!companionRowsAdmitted) {
    MemoryMonitor::reportPrewarmAdmissionDenied(MemoryMonitor::OperationKind::OverdubStart);
    return;
  }
  // S2b.2 (invariant 34): boot/load no longer prewarms a committed-but-inactive slot's working set,
  // so this interval's own cold admission must establish what the later deferred stop declares. It
  // is an allocation-bearing cold turn, so it lives in FLASH with the other cold admission helpers:
  // inlining it here cost ~300 B of ITCM and took the RAM1 padding line from 872 to 568. The
  // canonical copy stays lazy -- START is deliberately not gated on it; the stop's park acquires it.
  if (!admitOverdubStartWorkingSet()) return;
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(activeLoopIndex);
  CaptureAdmissionStaging* viewSet =
      runtime != nullptr ? runtime->liveCaptureStagingIfPrewarmed() : nullptr;
  if (viewSet == nullptr || !captureStopJobs_) {
    logger.log(CAT_TRACK, LOG_WARNING,
               "Overdub start rejected: capture staging or stop descriptors are not prewarmed");
    return;
  }
  // DEC-067: START owns the live interval only. The canonical copy target, the
  // active transfer admission and the session-exit receipt slot are *finalization*
  // resources -- none of them is used before this interval's own stop edge -- so
  // `beginDeferredOverdubStop` answers them, with its own denial outcome. A pending
  // predecessor may hold all three while this interval records.
  // Memory admission, and it sits before every mutation below on purpose: on
  // 2026-09-17 an admitted overdub reached `ExternalMemoryFirstAllocator::abort()`
  // about two seconds in and took the transport down with it. Exhaustion is an
  // operation-level denial, so refuse here with the loop still playing and no
  // ownership, reservation or state change to unwind. Emits `#MEM_DENIED` with the
  // operation, the reason, the requested margin and the measured availability.
  if (!MemoryMonitor::admitOperationMemory(MemoryMonitor::OperationKind::OverdubStart, 0)) {
    return;
  }
  // The frozen order/rewrite builders are the STOP's scratch, not the start's: a pending
  // predecessor is mid order/rewrite on them, and this admission must not touch them
  // (DEC-067). `beginDeferredOverdubStop` resets them once no job owns them.
  viewSet->reset();
  viewSet->captureGeneration = playbackGeneration;
  playingMidiDrainAfterOverdubStop_ = false;
  playingMidiDrainAfterOverdubStopIdleNoted_ = false;
  overdubDrainFallbackRetryAtMs_ = 0;
  loopPrefixMeasureAfterUndo_ = false;
  loopPrefixMeasureAfterUndoNoted_ = false;
  overdubStopDebtProbeArmed_ = false;
  // DEC-069: a wrap commit owed by a previous interval never crosses an interval boundary.
  overdubWrapCommitPending_ = false;
  const uint32_t telemetryStartUs = micros();
  SC_ODUB_STAGE("enter", 0, DebugSessionCapture::kUnsampledHeapBytes,
                DebugSessionCapture::kUnsampledHeapBytes, "ok");
  const Loop& active = loopRef;
  if (trackState == TRACK_EMPTY && active.loopLengthTicks > 0) {
    forceSetState(TRACK_STOPPED);
  }
  const uint32_t stateAdvanceStartUs = micros();
  if (!beginCaptureInterval(TRACK_OVERDUBBING)) {
    SC_ODUB_STAGE("set_state", micros() - stateAdvanceStartUs,
                  DebugSessionCapture::kUnsampledHeapBytes,
                  DebugSessionCapture::kUnsampledHeapBytes, "failed");
    return;
  }

#if defined(SESSION_CAPTURE)
  // OpenSpec 5.6: one compact admission record per reentry decision, emitted by the decision
  // owner (`reentryDecision`), so a capture shows the admitted interval identity and the queue
  // depth without per-event logging.
  DebugSessionCapture::reentryDecision("admit", currentTick, captureInterval_, activeLoopIndex,
                                       captureStopQueueDepth());
#endif
  transportStopOutputBarrier_ = false;
  SC_ODUB_STAGE("set_state", micros() - stateAdvanceStartUs,
                DebugSessionCapture::kUnsampledHeapBytes,
                DebugSessionCapture::kUnsampledHeapBytes, "ok");
  recordAddedNoteOnCount = 0;
  Loop& loop = getActiveLoop();
  uint32_t playheadPhase = 0;
  if (loop.loopLengthTicks > 0) {
    playheadPhase =
        tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks);
  }
  const uint32_t captureStartUs = micros();
  // DEC-068: a pending predecessor's session tail is captured in its own receipt, so this
  // interval must not join the draining session state.
  loop.openOverdubSession(playheadPhase, hasPendingDeferredOverdubStop());
  loop.beginCapture(CapturePhase::Overdub, playheadPhase);
  SC_ODUB_STAGE("begin_capture", micros() - captureStartUs,
                DebugSessionCapture::kUnsampledHeapBytes,
                DebugSessionCapture::kUnsampledHeapBytes, "ok");
  if (loop.loopLengthTicks > 0) {
    const uint32_t phase =
        tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks);
    projectionCycleStartTick =
        static_cast<int32_t>(currentTick) - static_cast<int32_t>(phase);
  }
  const uint32_t undoStartUs = micros();
  TrackUndo::beginOverdubSession(*this);
  SC_ODUB_STAGE("undo_session", micros() - undoStartUs, DebugSessionCapture::kUnsampledHeapBytes,
                DebugSessionCapture::kUnsampledHeapBytes, "ok");
  logger.info("Overdub session opened: events=%d, undo_entries=%d",
              static_cast<int>(loop.displayEventCountHint()),
              static_cast<int>(TrackUndo::getUndoCount(*this)));
  HotPathTelemetry::recordOverdubStart(micros() - telemetryStartUs,
                                       static_cast<uint32_t>(loop.displayEventCountHint()),
                                       static_cast<uint32_t>(TrackUndo::getUndoCount(*this)));
  SC_ODUB_STAGE("complete", micros() - telemetryStartUs, DebugSessionCapture::kUnsampledHeapBytes,
                DebugSessionCapture::kUnsampledHeapBytes, "ok");
  logger.logTrackEvent("Overdubbing started", currentTick);
}


TRACK_COLD_MEM void Track::stopOverdubbing() {
  const uint32_t currentTick = clockManager.getCurrentTick();
  Loop& loop = getActiveLoop();
#if defined(SESSION_CAPTURE)
  const bool probeVisualDirtyBeforeStop = loop.visualCacheDirty;
  const uint16_t probeDirtyBarsBeforeStop = countVisualCacheDirtyBars(loop);
  const bool probeSavePendingBeforeStop = StorageManager::hasDeferredSaveWork();
#endif
  const uint32_t stopStartUs = micros();
  uint32_t closeTick = UINT32_MAX;
  if (loop.loopLengthTicks > 0) {
    closeTick = capturePhaseTick(currentTick);
  }
  if (handleNoteEditFold(true, currentTick, closeTick, stopStartUs)) {
    // The in-edit fold completed this interval's stop, so an accepted stop's obligation is
    // consumed here rather than being driven against an interval that no longer records.
    consumeAcceptedStopObligation();
    loop.closeOverdubSession();
    return;
  }
  // Normal capture uses the frozen, generation-matched tail. The legacy path
  // remains a fault fallback until the stop owner has its explicit recovery
  // state; ordinary admitted input never reaches it.
  if (beginDeferredOverdubStop(PendingStopDestination::Playing, currentTick)) {
    consumeAcceptedStopObligation();
    return;
  }
  // Capacity denial: a predecessor still owns the sole frozen/deferred admission
  // role, so this live interval cannot freeze into a second deferred job. The
  // denial is explicit and non-destructive — the interval keeps recording with its
  // rows, reservations, pending notes and preview intact, and no stop descriptor is
  // created for it. A later stop request is admitted once the predecessor retires.
  // This is the precise capacity question the pending-deferred predicate answers.
  // A denial is only a *fault* when nothing can say why. When the finalization
  // resources this stop would need are still owned or not yet replenished, the
  // denial is a capacity answer and the interval keeps recording (DEC-067); the
  // legacy fallback below would otherwise freeze a pass against spent credits.
  auto* stopRuntime = playbackRuntime.slotIfAllocated(activeLoopIndex);
  const bool finalizationResourcesBusy =
      stopRuntime != nullptr &&
      (stopRuntime->captureStopOwner != nullptr ||
       !stopRuntime->canonicalCaptureOutputAdmitted() ||
       !loop.hasActiveOverdubTransferAdmission() ||
       !loop.canHandoffSessionExitSnapshot());
  if (hasPendingDeferredOverdubStop() || finalizationResourcesBusy) {
    SC_ODUB_STOP_STAGE("admission", 0, 0, UINT32_MAX, UINT32_MAX,
                       captureStopQueueDepth(), 0, "successor_stop_denied_capacity");
#if defined(SESSION_CAPTURE)
    DebugSessionCapture::reentryDecision("deny", currentTick, captureInterval_, activeLoopIndex,
                                         captureStopQueueDepth());
#endif
    // DEC-078 / invariant 31: this is not a refusal. The stop is accepted and retained against
    // the interval's own slot and generation; `Track::driveAcceptedInteraction` re-runs this
    // guard chain on a deferred turn and executes it exactly once when the owner frees. The
    // interval keeps recording, with its rows, reservations, pending notes and preview intact,
    // and the user never presses twice. The `deny` row above is retained as the DEC-067
    // counterfactual the HITL oracle already reads; `accepted_stop_finalizer_wait` is the
    // DEC-078 scheduling evidence.
    latchAcceptedStop(PendingStopDestination::Playing, activeLoopIndex, captureInterval_);
    // DEC-070 diagnostic: name the resource that is still owned. The generic message hid which of
    // five different leaks wedged the slot (`ring_count_without_job` in particular means the ring
    // bookkeeping, not a live job, is holding the refusal).
    const CaptureStopJob* headJob = captureStopJobHead();
    const char* busyReason =
        hasPendingDeferredOverdubStop()
            ? ((headJob != nullptr && headJob->admission.pending) ? "stop_job_pending"
                                                                 : "ring_count_without_job")
            : (stopRuntime == nullptr
                   ? "runtime_unavailable"
                   : stopRuntime->captureStopOwner != nullptr
                         ? "stop_owner"
                         : !stopRuntime->canonicalCaptureOutputAdmitted()
                               ? "canonical_output"
                               : !loop.hasActiveOverdubTransferAdmission() ? "transfer_admission"
                                                                          : "session_exit_handoff");
    if (headJob != nullptr && headJob->admission.pending) {
      // Name the slot and the interval here too, for the same reason the `else` branch does: this row IS the
      // timely edge, and `stop_job_pending` is the reason a successor's stop actually meets on the reentry
      // path -- so leaving it without identity is precisely where attribution is needed. Without it the
      // retention oracle's L2 can only report that "a denial happened", not which slot/interval was denied
      // (measured on device 2026-09-27: this branch produced the run's only denial, with no slot/interval, so
      // L2 fell back to UNKNOWN and the run could not be a verdict).
      logger.log(CAT_TRACK, LOG_INFO,
                 "Overdub stop refused: %s slot=%u interval=%lu phase=%u dest=%u closure=%u recycle_owner=%u "
                 "recycle_started=%u undo_ready=%u",
                 busyReason, static_cast<unsigned>(activeLoopIndex),
                 static_cast<unsigned long>(captureInterval_),
                 static_cast<unsigned>(headJob->admission.phase),
                 static_cast<unsigned>(headJob->admission.destination),
                 static_cast<unsigned>(headJob->admission.remainingClosureGrains),
                 headJob->admission.historyRecycleOwner != nullptr ? 1u : 0u,
                 headJob->admission.historyRecycleStarted ? 1u : 0u,
                 headJob->undoPublicationReady ? 1u : 0u);
    } else {
      // Name the slot and the interval. The human row is the timely edge, and without the identity it cannot
      // attribute the denial: "which slot was refused" was previously readable only from the deferred `#CAP`
      // row, which the timing-critical shed could remove.
      logger.log(CAT_TRACK, LOG_INFO, "Overdub stop refused: %s slot=%u interval=%lu", busyReason,
                 static_cast<unsigned>(activeLoopIndex),
                 static_cast<unsigned long>(captureInterval_));
    }
    return;
  }
  // The admitted path above must leave the MIDI button handler without
  // formatting capture traffic. Its state edge and all stop work are emitted
  // later from the main-loop grain owner. Retain the legacy diagnostics only
  // for the fault fallback that still performs a synchronous stop here.
  logOverdubStopStage(loop, stopStartUs, "enter", 0, DebugSessionCapture::kUnsampledHeapBytes,
                      DebugSessionCapture::kUnsampledHeapBytes, "fallback");
  // The fallback still runs from the MIDI button boundary, so it must not
  // perform a blocking USB drain before it can publish PLAYING.  The old
  // 128-record flush could wait indefinitely on CDC backpressure and made a
  // clean normal stop look hung.  The timing-critical budget only sheds
  // nonessential records; the idle owner drains the retained trace later.
  SC_REC_FLUSH_PENDING_REVTS(8);
  finalizePendingNotes(currentTick);
  const uint32_t sealStartUs = micros();
  const CommitResult sideEffectResult =
      commitCaptureForStop(CommitReason::OverdubStop, currentTick, closeTick, activeLoopIndex);
  // Stage order seal → finalize preserved; duration covers seal+finalize together on seal.
  logOverdubStopStage(loop, stopStartUs, "seal", micros() - sealStartUs,
                      DebugSessionCapture::kUnsampledHeapBytes,
                      DebugSessionCapture::kUnsampledHeapBytes,
                      commitResultLabel(sideEffectResult));
  logOverdubStopStage(loop, stopStartUs, "finalize", 0,
                      DebugSessionCapture::kUnsampledHeapBytes,
                      DebugSessionCapture::kUnsampledHeapBytes,
                      commitResultLabel(sideEffectResult));
  const uint32_t stateStartUs = micros();
  // Silence this track only, then resume loop playback. Do not CC123 every channel —
  // that mutes other playing tracks. Transport stop still uses sendAllNotesOff().
  silenceTrackMidiOutput();
    clearStoppingIntervalLedger();
  pendingNotes.clear();
  reanchorPlaybackCycle(currentTick);
  setState(TRACK_PLAYING);
  logOverdubStopStage(loop, stopStartUs, "set_state", micros() - stateStartUs,
                      DebugSessionCapture::kUnsampledHeapBytes,
                      DebugSessionCapture::kUnsampledHeapBytes, "ok");
  const uint32_t flushStartUs = micros();
  // Keep the MIDI command bounded after the state edge as well.  Retained
  // capture evidence is drained by the idle owner once PLAYING is published.
  SC_REC_FLUSH_PENDING_REVTS(8);
  logOverdubStopStage(loop, stopStartUs, "flush", micros() - flushStartUs,
                      DebugSessionCapture::kUnsampledHeapBytes,
                      DebugSessionCapture::kUnsampledHeapBytes, "ok");
  logMemoryAfterOverdubStop(recordAddedNoteOnCount, loop);
  logger.logTrackEvent("Overdubbing stopped", currentTick);
  logger.info("Overdub stopped: events=%d, undo_entries=%d", static_cast<int>(loop.displayEventCountHint()),
              TrackUndo::getUndoCount(*this));

  const uint32_t storagePhaseTickAtStop =
      (loop.loopLengthTicks > 0)
          ? tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks)
          : 0;
  displayManager.refreshViewportAfterOverdubStop(*this, activeLoopIndex, storagePhaseTickAtStop);
  emitOverdubStopDisplaySnapshot(*this, activeLoopIndex, currentTick);
  logOverdubStopStage(loop, stopStartUs, "display", 0,
                      DebugSessionCapture::kUnsampledHeapBytes,
                      DebugSessionCapture::kUnsampledHeapBytes, "hold");
#if defined(SESSION_CAPTURE)
  armOverdubStopDebtProbe(probeVisualDirtyBeforeStop, probeDirtyBarsBeforeStop,
                          probeSavePendingBeforeStop, loop);
#endif
  HotPathTelemetry::requestDeferredSummary("overdub_stop");
  loop.closeOverdubSession();
  armPlayingMidiDrainAfterOverdubStop();
}

void Track::armPlayingMidiDrainAfterOverdubStop() {
  playingMidiDrainAfterOverdubStop_ = true;
  playingMidiDrainAfterOverdubStopIdleNoted_ = false;
}

bool Track::playingMidiDrainAfterOverdubStopActive() const {
  if (!playingMidiDrainAfterOverdubStop_ || !isPlaying() || !loopsAllocated()) {
    return false;
  }
  return getActiveLoop().visualCacheDirty || !playingMidiDrainAfterOverdubStopIdleNoted_;
}

void Track::notePlayingMidiDrainAfterOverdubStopIdle() {
  if (!playingMidiDrainAfterOverdubStop_) {
    return;
  }
  playingMidiDrainAfterOverdubStopIdleNoted_ = true;
  if (!isPlaying() || !loopsAllocated() || !getActiveLoop().visualCacheDirty) {
    playingMidiDrainAfterOverdubStop_ = false;
    overdubDrainFallbackRetryAtMs_ = 0;
  }
}

TRACK_COLD_MEM void Track::armLoopPrefixMeasureAfterUndo() {
  loopPrefixMeasureAfterUndo_ = true;
  loopPrefixMeasureAfterUndoNoted_ = false;
}

TRACK_COLD_MEM bool Track::loopPrefixMeasureAfterUndoActive() const {
  if (!loopPrefixMeasureAfterUndo_ || !isPlaying() || !loopsAllocated()) {
    return false;
  }
  return getActiveLoop().visualCacheDirty || !loopPrefixMeasureAfterUndoNoted_;
}

TRACK_COLD_MEM void Track::noteLoopPrefixMeasureAfterUndo() {
  if (!loopPrefixMeasureAfterUndo_) {
    return;
  }
  loopPrefixMeasureAfterUndoNoted_ = true;
  if (!isPlaying() || !loopsAllocated() || !getActiveLoop().visualCacheDirty) {
    loopPrefixMeasureAfterUndo_ = false;
  }
}

TRACK_COLD_MEM void Track::stopOverdubbingToStopped() {
  if (isEmpty()) return;
  // S4f.4, transport stop: transport stop wins over a pending return to PLAYING, and this path
  // admits (or synchronously completes) the stop itself. The accepted stop's obligation is
  // therefore consumed here, so the deferred drive can never execute a stale PLAYING stop
  // against an interval this path already stopped.
  consumeAcceptedStopObligation();
  suppressMidiOutputForTransportStop();
  if (hasPendingDeferredOverdubStop()) {
    // Transport Stop wins over a pending return to PLAYING. The frozen source
    // remains valid, but its continuation may resolve only to STOPPED.
    requestDeferredOverdubStopToStopped();
    return;
  }
  overdubStopDebtProbeArmed_ = false;
  // DEC-069: this interval's own rows are sealed below, so no deferred wrap commit survives it.
  overdubWrapCommitPending_ = false;
  const uint32_t currentTick = clockManager.getCurrentTick();
  Loop& loop = getActiveLoop();
  const uint32_t stopStartUs = micros();
  logOverdubStopStage(loop, stopStartUs, "enter", 0, DebugSessionCapture::kUnsampledHeapBytes,
                      DebugSessionCapture::kUnsampledHeapBytes, "entered");
  uint32_t closeTick = UINT32_MAX;
  if (loop.loopLengthTicks > 0) {
    closeTick = capturePhaseTick(currentTick);
  }
  if (handleNoteEditFold(false, currentTick, closeTick, stopStartUs)) {
    loop.closeOverdubSession();
    return;
  }
  if (beginDeferredOverdubStop(PendingStopDestination::Stopped, currentTick)) {
    consumeAcceptedStopObligation();
    return;
  }
  finalizePendingNotes(currentTick);
  const uint32_t sealStartUs = micros();
  const CommitResult commitResult =
      commitCaptureForStop(CommitReason::OverdubStopToStopped, currentTick, closeTick, activeLoopIndex);
  logOverdubStopStage(loop, stopStartUs, "seal", micros() - sealStartUs,
                      DebugSessionCapture::kUnsampledHeapBytes,
                      DebugSessionCapture::kUnsampledHeapBytes,
                      commitResultLabel(commitResult));
  silenceTrackMidiOutput();
  logMemoryAfterOverdubStop(recordAddedNoteOnCount, loop);
  setState(TRACK_STOPPED);
  resetPlaybackState(currentTick);
  const uint32_t storagePhaseTickAtStop =
      (loop.loopLengthTicks > 0)
          ? tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks)
          : 0;
  // RC5e: adopt composed capture display before STOPPED snapshot (174742 DFRAME gap).
  displayManager.refreshViewportAfterOverdubStop(*this, activeLoopIndex, storagePhaseTickAtStop);
  displayManager.emitDisplayCaptureSnapshot(*this, activeLoopIndex, currentTick);
  logger.logTrackEvent("Overdubbing stopped (to STOPPED)", currentTick);
  HotPathTelemetry::requestDeferredSummary("overdub_stop_to_stopped");
  loop.closeOverdubSession();
}
