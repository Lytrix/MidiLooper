// S4f.16: actual stop/worker/undo/storage owners; no copied stop implementation.
//
// S4f.17/S4f.21: this suite is the **enforced** finalization gate, not an opt-in one. The
// R→R / R→OD / OD→R / OD→OD rotation oracle asserts the deferred production shape unconditionally
// -- `S4F16_REQUIRE_CONTRACT` is no longer read here, so ordinary suite success IS the strict
// result and there is no weaker mode to select. Do not reintroduce a conditional: a mode in which
// the Record stop may still publish inline is exactly the regression this suite exists to catch.
#if defined(UNIFIED_RECORD_RETURN_READER_ORACLE)
#define PLAYBACK_WINDOW_ROLLING_ENABLED 1 // qualification-native only; default emitter stays live
#endif
#define HARNESS_REAL_PLAYBACK_HOT_PATH
#define HARNESS_REAL_CAPTURE_STOP

// windowed-playback-generation (the switch leg's second diagnostic): the `Snapshot` deferred owner's **gate row**,
// substituted through the `#ifndef`-guarded `SC_SNAPSHOT_GATE` seam exactly as the capture suites substitute their
// others. This suite is the one that already drives the real `processDeferredIdleMaintenance` (with the storage owners
// linked), so it is where the row's contract is checked rather than described: **which loop the publication owner
// selected** and **which gate refused** it. The cadence that bounds the row is production state, so a substituted
// recorder counts exactly the rows the device would emit.
#include <cstddef>
#include "Utils/PlaybackDemandClaims.h"
#include "Utils/OverdubOverlayLane.h"
#include <cstdint>
namespace SnapshotGateRows {
struct Row {
  uint8_t track = 0;
  uint8_t selectedSlot = 0;
  uint8_t activeSlot = 0;
  uint16_t verdicts = 0;
  uint8_t constructionUnits = 0;
  uint32_t eligibleTurns = 0;
  uint16_t inFlightUnits = 0;
  uint32_t publishedRevision = 0;
};
inline Row rows[64];
inline size_t count = 0;
inline void reset() { count = 0; }
inline void record(uint8_t track, uint8_t selectedSlot, uint8_t activeSlot, uint16_t verdicts,
                   uint8_t constructionUnits, uint32_t eligibleTurns, uint16_t inFlightUnits,
                   uint32_t publishedRevision) {
  if (count < sizeof(rows) / sizeof(rows[0])) {
    rows[count] = Row{track, selectedSlot, activeSlot, verdicts, constructionUnits, eligibleTurns, inFlightUnits, publishedRevision};
  }
  ++count;
}
inline const Row* last() { return count == 0 ? nullptr : &rows[count - 1]; }
inline size_t countSelecting(uint8_t slot) {
  size_t seen = 0;
  for (size_t i = 0; i < count && i < sizeof(rows) / sizeof(rows[0]); ++i) {
    if (rows[i].selectedSlot == slot) ++seen;
  }
  return seen;
}
}  // namespace SnapshotGateRows
#define SC_SNAPSHOT_GATE(track, selectedSlot, activeSlot, verdicts, constructionUnits, eligibleTurns, inFlightUnits, \
                         publishedRevision) \
  SnapshotGateRows::record(track, selectedSlot, activeSlot, verdicts, constructionUnits, eligibleTurns, inFlightUnits, \
                           publishedRevision)
// DEC-108 §13b / task 3.9: the window lane's **path rows** (`DIAG,playback_build,<path>`) and its **readiness
// receipt**, substituted through the `#ifndef`-guarded `SC_PLAYBACK_BUILD_ROW` / `SC_WINDOW_READINESS` seams -- the
// same pattern the sibling `test_runtime_working_set_ownership` suite established. This suite is the one that drives a
// **real Record stop** to a frozen-return window completion, so the rows that completion owes are checked here as
// **the rows** rather than described. Unconditional: the recorders are inert in a case that asserts nothing about them.
#include <cstring>
namespace PlaybackPathRows {
inline const char* paths[96] = {};
inline size_t count = 0;
inline void reset() { count = 0; }
inline void record(uint8_t, uint8_t, uint32_t, uint32_t, const char* path) {
  if (count < sizeof(paths) / sizeof(paths[0])) {
    paths[count] = path;
  }
  ++count;
}
inline size_t countOf(const char* path) {
  size_t seen = 0;
  for (size_t i = 0; i < count && i < sizeof(paths) / sizeof(paths[0]); ++i) {
    if (paths[i] != nullptr && std::strcmp(paths[i], path) == 0) {
      ++seen;
    }
  }
  return seen;
}
}  // namespace PlaybackPathRows
#define SC_PLAYBACK_BUILD_ROW(track, slot, loopLengthTicks, revision, path) \
  PlaybackPathRows::record(track, slot, loopLengthTicks, revision, path)
namespace WindowReadinessRows {
struct Row {
  uint8_t track = 0;
  uint8_t slot = 0;
  const char* readiness = nullptr;
  const char* holder = nullptr;
  uint32_t revision = 0;
  uint32_t leadTicks = 0;
};
inline Row rows[96] = {};
inline size_t count = 0;
inline void reset() { count = 0; }
inline void record(uint8_t track, uint8_t slot, const char* readiness, const char* holder, uint32_t revision,
                   uint32_t leadTicks) {
  if (count < sizeof(rows) / sizeof(rows[0])) {
    rows[count] = Row{track, slot, readiness, holder, revision, leadTicks};
  }
  ++count;
}
/// Rows scoped to one slot: the **branch** that produced a receipt is identified by the slot it names, which is how
/// the frozen Record-return owner's own rows are told apart from a sibling holder's coverage-tail rows.
inline size_t countForSlot(uint8_t slot) {
  size_t seen = 0;
  for (size_t i = 0; i < count && i < sizeof(rows) / sizeof(rows[0]); ++i) {
    if (rows[i].slot == slot) {
      ++seen;
    }
  }
  return seen;
}
}  // namespace WindowReadinessRows
#define SC_WINDOW_READINESS(track, slot, readiness, holder, revision, leadTicks) \
  WindowReadinessRows::record(track, slot, readiness, holder, revision, leadTicks)
// DEC-108 §13b / task 3.9: the frozen Record-return **completion**'s own receipt. `SC_WINDOW_READINESS` above names
// the slot's *committed* readiness, which for a first Record take is `not_servable` because
// `playbackWindowReadinessForSlot` exits on `!loop.hasCommittedPasses()` before a frozen Capture ready can be named.
// This is the **source-qualified** companion: the same completion transition, carrying the identity and interval of
// the window that actually became ready. Substituted through the `#ifndef`-guarded seam like every sibling here.
namespace FrozenReadinessRows {
struct Row {
  uint8_t track = 0;
  uint8_t slot = 0;
  const char* readiness = nullptr;
  const char* holder = nullptr;
  uint32_t revision = 0;
  uint32_t leadTicks = 0;
  uint32_t sourceGeneration = 0;
  uint64_t captureToken = 0;
  uint32_t start = 0;
  uint32_t length = 0;
  uint32_t loopLength = 0;
};
inline Row rows[64] = {};
inline size_t count = 0;
inline void reset() { count = 0; }
inline void record(uint8_t track, uint8_t slot, const char* readiness, const char* holder, uint32_t revision,
                   uint32_t leadTicks, uint32_t sourceGeneration, uint64_t captureToken, uint32_t start,
                   uint32_t length, uint32_t loopLength) {
  if (count < sizeof(rows) / sizeof(rows[0])) {
    rows[count] = Row{track, slot, readiness, holder, revision, leadTicks, sourceGeneration, captureToken, start,
                      length, loopLength};
  }
  ++count;
}
}  // namespace FrozenReadinessRows
// Task 3.15g appends three adoption-diagnostic fields to this family; the fixture's recorder keeps the original
// eleven and ignores the appended tail, exactly as a reader that stops at field 14 does.
#define SC_FROZEN_WINDOW_READINESS(track, slot, readiness, holder, revision, leadTicks, sourceGeneration, \
                                   captureToken, start, length, loopLength, ...) \
  FrozenReadinessRows::record(track, slot, readiness, holder, revision, leadTicks, sourceGeneration, captureToken, \
                              start, length, loopLength)
#include "../test_support/capture_storage_harness.inc"
#include <cstdlib>
#if BYPASS_STOP_UNDO_SAVE
#error This fixture requires production undo and persistence admission
#endif

struct TrackCaptureStopTestAccess {
  static bool outputBarrier(const Track& t) { return t.transportStopOutputBarrier_; }
  // Task 3.15g: the lane's own admission decision, asked directly instead of inferred from a state dump.
  static bool retryCanProgress(const Track& t, uint8_t slot, PlaybackDeadlineClock clock) {
    return t.committedPlaybackRetryCanProgress(slot, clock);
  }
  static bool rootReturnOwed(const Track& t, uint8_t slot, const PlaybackViewPendingRequest& request,
                             PlaybackDeadlineClock clock) {
    return t.rootReturnSuccessorIsOwedForSlot(slot, request, clock);
  }
  static auto& pendingRequest(Track& t, uint8_t slot) { return t.playbackRuntime.slotIfAllocated(slot)->pendingViewRequest_; }
  static auto& bindRecordClosure(Track& t, uint8_t slot, uint32_t phaseTick, uint32_t length,
                                 bool presentationRetiring = true) {
    TEST_ASSERT_TRUE(t.prewarmCaptureStopJobs());
    auto* job = t.captureStopJobTail();
    TEST_ASSERT_NOT_NULL(job);
    job->presentation = &t.captureStopJobs_->presentations[slot];
    job->presentation->owner = &t.getLoop(slot);
    job->presentation->loopId = t.getLoop(slot).loopId;
    job->presentation->slot = static_cast<uint8_t>(slot);
    job->presentation->interval = t.captureInterval_;
    job->presentation->loopLengthTicks = length;
    job->presentation->armed = true;
    job->owner = &t.getLoop(slot);
    job->slot = slot; job->loopId = job->owner->loopId;
    job->mode = CapturePhase::Record; job->interval = t.captureInterval_;
    job->phaseTick = phaseTick; job->loopLengthTicks = length;
    TEST_ASSERT_TRUE(job->owner->capture.store.sealCaptureRange(job->presentation->rawRange));
    job->presentation->ownsExtent = true;
    TEST_ASSERT_TRUE(
        job->owner->borrowSealedCaptureRange(job->presentation->rawRange, job->presentation));
    job->pendingNotes.swap(t.pendingNotes);
    // Controlled binding only: production Record admission is a separate open gate.
    job->admission.pending = true;
    job->admission.frozenGeneration = 1;
    job->admission.stopTick = 1000;
    job->admission.phase = PendingStopPhase::ReservedClosure;
    job->admission.remainingClosureGrains = job->pendingNotes.size();
    auto* runtime = t.playbackRuntime.slotIfAllocated(slot);
    TEST_ASSERT_NOT_NULL(runtime);
    TEST_ASSERT_TRUE(runtime->prewarmCanonicalCaptureOutput());
    runtime->setCaptureStopOwner(job->admission, job->admission.frozenGeneration);
    // The shared rolling fixtures model the **post-publication** owner split: the frozen-reader
    // obligation belongs to the slot-owned presentation record, exactly as
    // `publishRecordPresentation` leaves it, and every reader predicate is keyed on that owner.
    // Which role retires the interval is therefore the fixture's own choice:
    //   * `presentationRetiring` (default) -- the published shape; retirement runs through the
    //     presentation driver and the interaction job is already conceptually popped.
    //   * `false` -- the empty-take shape. `stepDeferredRecordEmptyResult` deliberately promotes
    //     no presentation, so its job still owes its own retirement end through the ring.
    job->presentation->retirementActive = presentationRetiring;
    // Interval-derived, mirroring the production admissions: the qualification guard re-derives it.
    job->presentation->generation = static_cast<uint32_t>(job->presentation->interval);
    job->presentation->frozenCaptureContentGeneration = job->frozenCaptureContentGeneration;
    runtime->setPresentationOwner(job->presentation, job->presentation->generation);
    // Production bind captures the interval's job-owned Record geometry receipt.
    t.captureRecordGeometryReceipt(*job);
    ++t.captureStopJobs_->count;
    return *job;
  }
  static auto closureGrain(Track& t) {
    return t.stepDeferredRecordClosure(*t.captureStopJobHead());
  }
  static auto stagingGrain(Track& t) {
    return t.stepDeferredRecordSourceStaging(*t.captureStopJobHead());
  }
  static auto normalizationGrain(Track& t) {
    return t.stepDeferredRecordNormalization(*t.captureStopJobHead());
  }
  static auto passTransferGrain(Track& t) {
    return t.stepDeferredRecordPassTransfer(*t.captureStopJobHead());
  }
  static auto publicationGrain(Track& t) {
    return t.stepDeferredRecordPublication(*t.captureStopJobHead());
  }
  static auto emptyGrain(Track& t) {
    return t.stepDeferredRecordEmptyResult(*t.captureStopJobHead());
  }
  static void clearGeometryReceipt(Track& t) {
    t.captureStopJobHead()->hasRecordGeometry = false;
  }
  static void restoreGeometryReceipt(Track& t, LoopId loopId) {
    auto& job = *t.captureStopJobHead();
    job.recordGeometryLoopId = loopId;
    job.hasRecordGeometry = true;
  }
  static bool ownsPendingPass(const Track& t) {
    const auto* job = t.captureStopJobHead();
    return job != nullptr && job->ownsPendingPass;
  }
  static bool hasRecordPass(const Track& t, uint8_t slot) {
    return t.getLoop(slot).passes.hasRecordPass();
  }
  static bool audibleSource(const Track& t, uint8_t slot) {
    return t.hasAudiblePlaybackSource(slot);
  }
  static bool admitRecordStop(Track& t, uint32_t tick, uint64_t interval) {
    return t.beginDeferredRecordStopForSlot(PendingStopDestination::Playing, tick, 0, interval);
  }
  static size_t trackHeldNotes(const Track& t) { return t.pendingNotes.size(); }
  static bool deferredRevtsPending(const Track& t) {
    return t.deferredRecordRevtsSlot != Config::INVALID_LOOP_SLOT;
  }
  /// S4.5: whether a stop descriptor still owns a sealed extent it has not returned to the pool. The
  /// record retirement used to drop its job with this true, which both leaked the extent and left the
  /// descriptor un-sealable for every later interval in that slot.
  static bool descriptorNeedsSourceRelease(Track& t, size_t index) {
    return t.captureStopJobs_->presentations[index].rawRange.needsSourceRelease();
  }
  static bool serviceDescriptorReservations(Track& t) {
    return t.serviceCaptureStopDescriptorReservations();
  }
  static size_t descriptorCount() { return Track::captureStopQueueCapacity(); }
  static uint8_t deferredRevtsSlot(const Track& t) { return t.deferredRecordRevtsSlot; }
  static void serviceDeferredRevts(Track& t) { t.processDeferredRecordRevts(64); }
  /// The slice the idle owner runs while a loop is audible: bounded chunk walk only.
  static void serviceDeferredRevtsForPlayback(Track& t) {
    t.processDeferredRecordRevts(32, /*allowMerge=*/false);
  }
  static bool deferredRevtsChunkScan(const Track& t) { return t.deferredRecordRevtChunkScan; }
  static void setDeferredRevtsScanMode(Track& t, bool chunkScan) {
    t.deferredRecordRevtChunkScan = chunkScan;
  }
  static std::vector<uint16_t> deferredRevtChunkIds(const Track& t) {
    return std::vector<uint16_t>(t.deferredRecordRevtChunkRefs.begin(),
                                 t.deferredRecordRevtChunkRefs.end());
  }
  /// The producer's own directory release, as the publication/retirement path performs it.
  static void releaseRecordPassDirectory(Track& t, uint8_t slot) {
    LoopEventStore::releaseChunkRefs(t.getLoop(slot).passes.recordPass.committedChunkIds);
    t.getLoop(slot).passes.recordPass.committedChunkIds.clear();
  }
  /// The transport-stop retarget, as `Track::stopOverdubbingToStopped` delivers it to a pending job.
  static void retargetPendingStopToStopped(Track& t) {
    t.requestDeferredOverdubStopToStopped();
  }
  /// The Track-level committed-MIDI scratch has no public reader, so the fixture stamps it fresh
  /// and then observes whether a publication invalidated it -- which is exactly the consumer
  /// contract `CommittedMidiScratch::isFresh` encodes.
  static void markCommittedScratchFresh(Track& t) {
    t.committedMidiScratchRevision_ = 0;
    t.committedMidiScratchPublishedIdentity_ = reinterpret_cast<const void*>(1);
  }
  static bool committedScratchInvalidated(const Track& t) {
    return t.committedMidiScratchRevision_ == UINT32_MAX &&
           t.committedMidiScratchPublishedIdentity_ == nullptr;
  }
  static Track::CaptureStopJob& head(Track& t) {
    if (auto* job = t.captureStopJobHead()) return *job;
    static Track::CaptureStopJob presentationShadow{};
    for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
      const auto& presentation = t.captureStopJobs_->presentations[slot];
      if (!presentation.retirementActive) continue;
      presentationShadow.owner = presentation.owner;
      presentationShadow.loopId = presentation.loopId;
      presentationShadow.slot = presentation.slot;
      presentationShadow.mode = CapturePhase::Record;
      presentationShadow.loopLengthTicks = presentation.loopLengthTicks;
      presentationShadow.interval = presentation.interval;
      presentationShadow.presentation = const_cast<Track::CaptureStopPresentation*>(&presentation);
      presentationShadow.frozenCaptureContentGeneration = presentation.frozenCaptureContentGeneration;
      presentationShadow.admission.pending = true;
      presentationShadow.admission.phase = PendingStopPhase::RecordPublished;
      presentationShadow.admission.frozenGeneration = presentation.generation;
      return presentationShadow;
    }
    return t.captureStopJob();
  }
  static bool recordWorkPending(const Track& t) {
    if (t.captureStopJobs_ != nullptr && t.captureStopJobs_->count != 0) return true;
    if (t.captureStopJobs_ != nullptr) {
      for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot)
        if (t.captureStopJobs_->presentations[slot].retirementActive) return true;
    }
    return false;
  }
  static LoopPlaybackRuntime& runtimeFor(Track& t, uint8_t slot) {
    return *t.playbackRuntime.slotIfAllocated(slot);
  }
  /// Task 3.10: the deferred stop's own FSM phase, read so a fixture can tell "this owner turn **advanced** the
  /// finalization" from "this owner turn was offered and did nothing". `-1` means no job is left to advance.
  static int headStopPhase(const Track& t) {
    const auto* job = t.captureStopJobHead();
    return job != nullptr ? static_cast<int>(job->admission.phase) : -1;
  }
  static bool presentationActive(const Track& t, uint8_t slot) {
    return t.captureStopJobs_ != nullptr && slot < Config::MAX_LOOPS_PER_TRACK &&
        t.captureStopJobs_->presentations[slot].retirementActive;
  }
  static uint32_t playbackGenerationOf(const Track& t) { return t.playbackGeneration; }
  static void resetProjectionCycle(Track& t) { t.projectionCycleStartTick = 0; }
  static void unmute(Track& t) { t.muted = false; }
  static void armEmit(Track& t) { t.playbackEmitMidiOutput_ = true; }
  static void selectSlot(Track& t, uint8_t slot) { t.activeLoopIndex = slot; }
  static void directSend(Track& t, const MidiEvent& event, uint8_t slot) {
    t.sendMidiEvent(event, slot);
  }
  /// Ends a fixture-modelled presentation obligation the way the presentation driver does on
  /// completion: the runtime's reader owner is dropped and the slot's record stops retiring.
  /// Production only ever holds that ownership on a job it already popped, so a fixture that ends
  /// an interval by hand has to release it here or the reader predicate would outlive its job.
  static void releasePresentationOwnership(Track& t, uint8_t slot) {
    auto* runtime = t.playbackRuntime.slotIfAllocated(slot);
    if (runtime == nullptr || t.captureStopJobs_ == nullptr) return;
    auto& presentation = t.captureStopJobs_->presentations[slot];
    (void)runtime->clearPresentationOwner(&presentation, presentation.generation);
    presentation.retirementActive = false;
  }
  /// Task 3.15g: the sealed extent and the frozen-reader identity live on the **slot-owned**
  /// presentation record, whose address is fixed for the borrow's whole lifetime. Publication
  /// nulls the interaction job's back-pointer when it pops it, so a fixture that keeps driving
  /// the extent after `RecordPublished` must resolve it through this accessor.
  static Track::CaptureStopPresentation* presentation(Track& t, uint8_t slot) {
    return t.captureStopPresentationForSlot(slot);
  }
  /// Task 3.15g: the sealed extent belongs to the **slot-owned** record, and publication nulls the
  /// interaction job's back-pointer when it pops it. "Is this interval's source still retained?" is
  /// therefore a question for the record, not for the retired job.
  static bool extentOwned(const Track& t, uint8_t slot) {
    const auto* presentation = t.captureStopPresentationForSlot(slot);
    return presentation != nullptr && presentation->ownsExtent;
  }
  /// Task 3.15g: `RecordPublished` pops the interaction job and moves the frozen-reader
  /// obligation to the slot-owned presentation record, so a fixture grain must be able to finish
  /// finish that retirement after the ring is empty. While a ring job exists it owns its own retirement
  /// end; only when it is gone does the presentation driver owe the remaining work.
  static LoopEventStoreRewriteResult retirementGrain(Track& t) {
    if (auto* job = t.captureStopJobHead()) return t.stepDeferredRecordRetirement(*job);
    (void)t.serviceCaptureStopPresentations();
    for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot)
      if (presentationActive(t, slot)) return LoopEventStoreRewriteResult::Pending;
    return LoopEventStoreRewriteResult::Complete;
  }
  static auto& closureRuntime(Track& t) {
    return *t.playbackRuntime.slotIfAllocated(t.captureStopJobHead()->slot);
  }
  static size_t heldCount(const Track& t) { return t.pendingNotes.size(); }
  static size_t stopQueueDepth(const Track& t) { return t.captureStopQueueDepth(); }
  static UndoLoopGeometry jobBeforeGeometry(Track& t) {
    return t.captureStopJobHead()->recordBeforeGeometry;
  }
  static void seedSuccessorRecordBaseline(Track& t, uint32_t length, uint32_t startLoop,
                                          uint32_t loopStart) {
    t.recordCaptureBaselineGeometry_ = {length, startLoop, loopStart};
    t.hasRecordCaptureBaselineGeometry_ = true;
  }
  static NoteId recordIdentity(const Track& t, uint8_t channel, uint8_t note) {
    const auto found = t.pendingNotes.find({note, channel});
    return found == t.pendingNotes.end() ? kInvalidNoteId : found->second.recordNoteId;
  }

  static void assertDeniedUndoRetainsObligation(Track& t) {
    auto& job = *t.captureStopJobHead();
    const auto phase = job.undoCollection.phase;
    const bool started = job.undoCollectionStarted, collected = job.undoCollected;
    const auto credits = job.admission.remainingUndoCollectionGrains;
    TEST_ASSERT_GREATER_THAN(0, credits);
    job.undoCollectionStarted = job.undoCollected = true;
    job.undoCollection.phase = Loop::SessionUndoCollection::Phase::Denied;
    step(t);
    const auto actualPhase = job.admission.phase;
    const auto actualCredits = job.admission.remainingUndoCollectionGrains;
    job.undoCollection.phase = phase;
    job.undoCollectionStarted = started; job.undoCollected = collected;
    TEST_ASSERT_EQUAL(PendingStopPhase::UndoCollection, actualPhase);
    TEST_ASSERT_EQUAL(credits, actualCredits);
    TEST_ASSERT_FALSE(job.undoPublicationStarted);
  }
  static void assertDeniedUndoPublicationRetries(Track& t) {
    auto& job = *t.captureStopJobHead();
    auto& stack = t.getGlobalUndoStack();
    const auto nextId = stack.nextEntryId;
    stack.nextEntryId = 0; // Refuse begin without moving the prepared entry.
    for (unsigned i = 0; i < 64 && !job.undoCollected; ++i) step(t);
    step(t); // A second refusal must remain in the same phase.
    const auto phase = job.admission.phase;
    const bool ready = job.undoPublicationReady, started = job.undoPublicationStarted;
    const auto retainedPass = job.undoEntry.passId;
    stack.nextEntryId = nextId; // Restore the admission before assertions/retry.
    TEST_ASSERT_EQUAL(PendingStopPhase::UndoCollection, phase);
    TEST_ASSERT_FALSE(ready); TEST_ASSERT_FALSE(started);
    TEST_ASSERT_EQUAL(job.pendingPass.id, retainedPass);
  }
  static PendingCapturePass& pass(Track& t) { return t.captureStopJobHead()->pendingPass; }
  static Capture& output(Track& t) {
    return t.playbackRuntime.slotIfAllocated(t.captureStopJobHead()->slot)->canonicalCaptureOutput;
  }
  static PendingStopPhase phase(const Track& t) {
    if (const auto* j = t.captureStopJobHead()) return j->admission.phase;
    if (t.captureStopJobs_ != nullptr) {
      for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot)
        if (t.captureStopJobs_->presentations[slot].retirementActive)
          return PendingStopPhase::RecordPublished;
    }
    TEST_ASSERT_NOT_NULL(t.captureStopJobHead());
    return PendingStopPhase::RecordPublished;
  }
  static bool hasReadablePass(Track& t) { return t.ownedPendingCapturePass() != nullptr; }
  static uint64_t interval(const Track& t) { return t.captureInterval_; }
  static size_t jobs(const Track& t) { return t.captureStopQueueDepth(); }
  static void step(Track& t) {
    const auto* before = t.captureStopJobHead();
    if (before) {
      const auto& a = before->admission;
      TEST_ASSERT_LESS_OR_EQUAL_UINT(32, a.rankedPreparationQuantum);
      TEST_ASSERT_LESS_OR_EQUAL_UINT(32, a.canonicalPreparationQuantum);
      TEST_ASSERT_LESS_OR_EQUAL_UINT(32, a.historyRecycleQuantum);
      TEST_ASSERT_LESS_OR_EQUAL_UINT(32, a.undoCollectionQuantum);
    }
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  }
  static void assertRetiredCursors(const Track& t) {
    for (const auto& job : t.captureStopJobs_->entries) {
      TEST_ASSERT_FALSE(job.passTransfer.active());
      TEST_ASSERT_FALSE(job.passTransfer.complete());
    }
  }
  static uint64_t headInterval(const Track& t) {
    const auto* j = t.captureStopJobHead();
    return j ? j->interval : 0;
  }
  static uint64_t budget(const Track& t) {
    const auto* j = t.captureStopJobHead();
    if (!j) return 0;
    const auto& a = j->admission;
    return a.remainingClosureGrains + a.remainingOrderGrains +
      a.remainingRankedPreparationGrains + a.remainingCanonicalPreparationGrains +
      a.remainingHistoryRecycleGrains + a.remainingUndoCollectionGrains + 128;
  }
  static void assertHead(const Track& t, uint8_t slot, uint64_t interval, CapturePhase mode) {
    const auto* j = t.captureStopJobHead();
    TEST_ASSERT_NOT_NULL(j);
    TEST_ASSERT_EQUAL_UINT(slot, j->slot);
    TEST_ASSERT_EQUAL(mode, j->mode);
    TEST_ASSERT_EQUAL_UINT64(interval, j->interval);
    TEST_ASSERT_EQUAL(t.getLoop(slot).loopId, j->loopId);
    TEST_ASSERT_EQUAL_PTR(&t.getLoop(slot), j->owner);
    TEST_ASSERT_TRUE(j->ownsRawRange());
  }
  static bool occupyCanonicalOutput(Track& t) {
    auto* rt = t.playbackRuntime.slotIfAllocated(0);
    return rt && rt->prewarmCanonicalCaptureOutput() &&
      rt->canonicalCaptureOutput.store.appendReserved(MidiEvent::ControlChange(0, 1, 1, 1),
                                                       rt->canonicalCaptureChunks);
  }
  static bool drive(Track& t) { return t.driveAcceptedInteraction(); }
  static void diagnostic(const Track& t) {
    const auto* j = t.captureStopJobHead();
    printf("jobs=%zu interval=%llu phase=%u job_bytes=%zu ring_bytes=%zu\n", jobs(t),
      static_cast<unsigned long long>(j ? j->interval : interval(t)),
      j ? static_cast<unsigned>(j->admission.phase) : 0,
      sizeof(Track::CaptureStopJob), sizeof(Track::CaptureStopJobs));
    if (j) {
      printf("credits closure=%u order=%llu ranked=%llu canonical=%llu undo=%llu history=%llu\n",
        j->admission.remainingClosureGrains,
        (unsigned long long)j->admission.remainingOrderGrains,
        (unsigned long long)j->admission.remainingRankedPreparationGrains,
        (unsigned long long)j->admission.remainingCanonicalPreparationGrains,
        (unsigned long long)j->admission.remainingUndoCollectionGrains,
        (unsigned long long)j->admission.remainingHistoryRecycleGrains);
      auto* rt = t.playbackRuntime.slotIfAllocated(j->slot);
      auto& loop = *j->owner;
      printf("commit=%u joint=%u verified=%u matches=%u gen=%u prefixGen=%u pending=%u\n",
        j->admission.canCommit(j->admission.transportCommandEpoch, j->admission.destination),
        loop.canCommitPreparedCaptureAndCompanions(j->admission.intervalToken(), &j->pendingPass),
        loop.preparedCompanionCaptureVerified(rt->canonicalPassInputs,j->admission.intervalToken()),
        rt->canonicalPassInputs.matchesCanonicalOwner(loop.passes,
          {loop.playbackRevision, t.playbackGeneration, loop.loopLengthTicks, loop.canonicalStorageGeneration}),
        t.playbackGeneration,rt->canonicalPrefixSource.sourceGeneration,j->pendingPass.preparationPending);
      printf("slot=%u loop=%lu raw=%u pass=%u credits=%llu free_chunks=%u\n", j->slot,
        static_cast<unsigned long>(j->loopId), j->ownsRawRange(), j->ownsPendingPass,
        static_cast<unsigned long long>(budget(t)), LoopEventStore::freeChunkCount());
    }
  }
};
void TrackManager::beginSlotSelectionHold(uint8_t, uint8_t) { A34A_OFF_PATH(); }
void TrackManager::endSlotSelectionHold(uint8_t, uint8_t, uint32_t) { A34A_OFF_PATH(); }
void setUp() {
  noteMinLengthTicks = Config::DEFAULT_NOTE_MIN_LENGTH_TICKS;
  noteMinLengthRemoveEnabled = Config::DEFAULT_NOTE_MIN_LENGTH_REMOVE_ENABLED;
  PersistenceWorkQueue::resetForTests();
  StorageManagerInternal::storageSession.currentWorkspaceSave.pending = false;
  StorageManagerInternal::storageSession.currentWorkspaceSave.urgentRequested = false;
  trackManager.~TrackManager();
  LoopEventStore::resetPoolForTests();
  new (&trackManager) TrackManager();
  nativeTransportRunning = true;
  nativePlaybackDeadlineClock = {};
  clockManager.assignCurrentTickSilently(0);
  nativeMillisSet(0);
  A34aBoundary::resetSlotFlags();
  for (auto& row : StorageManagerInternal::currentSetLoopSlotDirty) row.fill(false);
}
void tearDown() {}

// Select only the capture owner's input slot. Manager/carrier switching is explicitly
// outside this fixture; no state owner on the asserted stop/worker path is substituted.
void prepareSlot(Track& t, uint8_t slot, bool overdub) {
  Loop& loop = t.getLoop(slot);
  if (overdub) {
    loop.loopLengthTicks = 768;
    loop.beginCapture(CapturePhase::Record);
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(0, 1, 48, 100)));
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(48, 1, 48, 0)));
    TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(768, CommitReason::RecordStop));
    loop.commitPendingCapturePass();
    TEST_ASSERT_TRUE(loop.prewarmActiveOverdubTransferHorizon());
    TEST_ASSERT_TRUE(loop.prewarmSessionExitSnapshotHandoff());
  }
  TEST_ASSERT_TRUE(t.prewarmPlaybackForSlot(slot, true));
}
void start(Track& t, uint8_t slot, bool overdub, uint32_t tick, uint8_t note) {
  t.setActiveLoopIndex(slot);
  if (overdub) t.startOverdubbing(tick); else t.startRecording(tick);
  TEST_ASSERT_TRUE(overdub ? t.isOverdubbing() : t.isRecording());
  t.noteOn(1, note, 100, tick + 12);
  t.noteOff(1, note, 0, tick + 96);
}
void stop(Track& t, bool overdub, uint32_t tick) {
  clockManager.assignCurrentTickSilently(tick);
  if (overdub) t.stopOverdubbing(); else t.stopRecordingToStopped(tick);
}
/// S4f.17: the production stop edge admits a deferred Record job and returns; the pass,
/// its undo entry and its persistence admission come from the worker's grains. Every
/// Record fixture must therefore consume the chain before it asserts published content.
void driveStopToRetirement(Track& t) {
  const uint64_t declared = TrackCaptureStopTestAccess::budget(t);
  uint64_t turns = 0;
  const uint64_t cap = declared + 20000;
  while ((t.hasStopInFlight() || TrackCaptureStopTestAccess::recordWorkPending(t)) &&
         turns < cap && turns < 30000) {
    ++turns;
    if (t.hasSnapshotPublicationBacklogOnAnySlot())
      t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
    else
      TrackCaptureStopTestAccess::step(t);
  }
  if (t.hasStopInFlight() || TrackCaptureStopTestAccess::recordWorkPending(t)) {
    const size_t jobs = TrackCaptureStopTestAccess::jobs(t);
    char msg[224];
    snprintf(msg, sizeof(msg),
             "deferred Record stop did not retire: turns=%llu declared=%llu jobs=%zu pending_latch=%d "
             "phase=%u length=%u capture_store=%s",
             static_cast<unsigned long long>(turns), static_cast<unsigned long long>(declared), jobs,
             static_cast<int>(t.hasAcceptedUnexecutedInteraction()),
             jobs ? static_cast<unsigned>(TrackCaptureStopTestAccess::phase(t)) : 99u,
             static_cast<unsigned>(t.getLoop(0).loopLengthTicks),
             t.getLoop(0).capture.store.empty() ? "empty" : "populated");
    TEST_ASSERT_TRUE_MESSAGE(false, msg);
  }
}
void assertContent(Track& t, uint8_t slot, bool overdub, uint8_t note) {
  const Loop& loop = t.getLoop(slot);
  TEST_ASSERT_EQUAL_UINT32(768, loop.loopLengthTicks);
  TEST_ASSERT_EQUAL_UINT32(overdub ? 1 : 0, loop.passes.overdubPasses.size());
  SessionMidiEventVec rows;
  const auto& chunks = overdub ? loop.passes.overdubPasses.back().committedChunkIds : loop.passes.recordPass.committedChunkIds;
  LoopEventStore::appendChunkRefEvents(chunks, rows);
  TEST_ASSERT_EQUAL_UINT32(2, rows.size());
  TEST_ASSERT_EQUAL_UINT8(note, rows[0].data.noteData.note);
  TEST_ASSERT_EQUAL_UINT8(note, rows[1].data.noteData.note);
  TEST_ASSERT_EQUAL_UINT32(12, rows[0].tick);
  TEST_ASSERT_EQUAL_UINT32(96, rows[1].tick);
  TEST_ASSERT_TRUE_MESSAGE(StorageManagerInternal::currentSetLoopSlotDirty[0][slot],
    "completed interval must request persistence for its own slot");
}
void rotation(bool aOverdub, bool bOverdub) {
  LoopEventStore::initPool();
  Track& t = trackManager.getSelectedTrack();
  t.ensureLoopsAllocated();
  prepareSlot(t, 0, aOverdub);
  prepareSlot(t, 1, bOverdub);
  t.forceSetState(TRACK_PLAYING);
  start(t, 0, aOverdub, 0, 60);
  const uint64_t a = TrackCaptureStopTestAccess::interval(t);
  stop(t, aOverdub, 768);
  // S4f.17: both stop entry points admit a deferred job, so the deferred shape is the
  // production shape on every mode rotation -- it is no longer a contract-gated expectation.
  TEST_ASSERT_EQUAL_UINT_MESSAGE(1, TrackCaptureStopTestAccess::jobs(t),
    "A must own a deferred job before any worker grain");
  start(t, 1, bOverdub, 768, 62);
  const uint64_t b = TrackCaptureStopTestAccess::interval(t);
  TEST_ASSERT_TRUE_MESSAGE(b > a, "each actual start needs a fresh interval");
  TrackCaptureStopTestAccess::assertHead(
      t, 0, a, aOverdub ? CapturePhase::Overdub : CapturePhase::Record);
  stop(t, bOverdub, 1536);
  // A's job still owns the ring, so B's stop is retained rather than admitted. That is the
  // DEC-067/DEC-078 obligation, identical on the Record and overdub routes.
  TEST_ASSERT_TRUE(t.hasAcceptedUnexecutedInteraction());
  TEST_ASSERT_EQUAL_UINT(1, TrackCaptureStopTestAccess::jobs(t));
  const auto freeBefore = LoopEventStore::freeChunkCount();
  unsigned turns = 0;
  uint64_t allowance = 2; // At most two accepted-interaction admission turns.
  uint64_t lastJob = 0;
  while (t.hasStopInFlight() || TrackCaptureStopTestAccess::recordWorkPending(t)) {
    if (!t.hasStopInFlight() && t.isPlaying()) t.stopPlaying();
    const auto id = TrackCaptureStopTestAccess::headInterval(t);
    if (id != 0 && id != lastJob) {
      allowance += TrackCaptureStopTestAccess::budget(t);
      lastJob = id;
    }
    const bool snapshotTurn = t.hasSnapshotPublicationBacklogOnAnySlot();
    if (snapshotTurn) {
      t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
      continue;
    }
    if (++turns > allowance || turns > 20000) {
      TrackCaptureStopTestAccess::diagnostic(t);
      TEST_FAIL_MESSAGE("phase-credit budget or 20,000-turn fixture watchdog exhausted");
    }
    TrackCaptureStopTestAccess::step(t);
  }
  TrackCaptureStopTestAccess::diagnostic(t);
  printf("rotation=%s/%s turns=%u persist_queue=%u undo=%zu\n", aOverdub ? "OD" : "R",
    bOverdub ? "OD" : "R", turns, PersistenceWorkQueue::queueDepth(), t.getGlobalUndoStack().undoCount());
  printf("allowance=%llu free_chunks_before=%u after=%u\n",
    static_cast<unsigned long long>(allowance), freeBefore, LoopEventStore::freeChunkCount());
  TEST_ASSERT_FALSE_MESSAGE(t.hasStopInFlight(), "worker/latch failed to retire within diagnostic ceiling");
  assertContent(t, 0, aOverdub, 60);
  assertContent(t, 1, bOverdub, 62);
  TEST_ASSERT_EQUAL_UINT(2, t.getGlobalUndoStack().undoCount());
  const auto& undo = t.getGlobalUndoStack().entries;
  TEST_ASSERT_TRUE(undo[0].loopId != undo[1].loopId);
  for (uint8_t slot = 0; slot < 2; ++slot) {
    bool found = false;
    for (const auto& entry : undo) {
      if (entry.loopId != t.getLoop(slot).loopId) continue;
      found = true;
      TEST_ASSERT_EQUAL_UINT(slot, entry.slotIndex);
      const bool overdub = slot == 0 ? aOverdub : bOverdub;
      TEST_ASSERT_EQUAL(overdub ? UndoEntryKind::OverdubPassAdded : UndoEntryKind::RecordPassAdded,
                        entry.kind);
      TEST_ASSERT_EQUAL(PersistWorkState::Queued, PersistenceWorkQueue::workState(
        PersistWorkType::LoopPersist, persistKeyForSlot(0, slot)));
    }
    TEST_ASSERT_TRUE_MESSAGE(found, "undo must retain each interval's LoopId");
  }
  const size_t queue = PersistenceWorkQueue::queueDepth();
  for (unsigned i = 0; i < 10; ++i) {
    TrackCaptureStopTestAccess::step(t);
    TrackCaptureStopTestAccess::drive(t);
  }
  TEST_ASSERT_EQUAL_UINT(2, t.getGlobalUndoStack().undoCount());
  TEST_ASSERT_EQUAL_UINT(queue, PersistenceWorkQueue::queueDepth());
  assertContent(t, 0, aOverdub, 60);
  assertContent(t, 1, bOverdub, 62);
}
void test_record_to_record() { rotation(false, false); }
void test_record_to_overdub() { rotation(false, true); }
void test_overdub_to_record() { rotation(true, false); }
void test_overdub_to_overdub() { rotation(true, true); }
void test_actual_start_stamps_once_and_duplicate_keeps_capture() {
  LoopEventStore::initPool();
  Track& t = trackManager.getSelectedTrack();
  t.ensureLoopsAllocated();
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60);
  const auto id = TrackCaptureStopTestAccess::interval(t);
  TEST_ASSERT_EQUAL_UINT64(1, id);
  TEST_ASSERT_EQUAL_UINT(2, t.getLoop(0).capture.store.size());
  t.startRecording(400);
  TEST_ASSERT_EQUAL_UINT64(id, TrackCaptureStopTestAccess::interval(t));
  TEST_ASSERT_EQUAL_UINT(2, t.getLoop(0).capture.store.size());
  TEST_ASSERT_EQUAL_UINT32(0, t.getLoop(0).startLoopTick);
}
void test_invalid_record_start_does_not_reset_overdub_or_stamp() {
  LoopEventStore::initPool();
  Track& t = trackManager.getSelectedTrack();
  t.ensureLoopsAllocated();
  prepareSlot(t, 0, true);
  t.forceSetState(TRACK_PLAYING);
  start(t, 0, true, 0, 60);
  const auto id = TrackCaptureStopTestAccess::interval(t);
  const auto passId = t.getLoop(0).passes.recordPass.id;
  t.startRecording(400);
  TEST_ASSERT_TRUE(t.isOverdubbing());
  TEST_ASSERT_EQUAL_UINT64(id, TrackCaptureStopTestAccess::interval(t));
  TEST_ASSERT_EQUAL(passId, t.getLoop(0).passes.recordPass.id);
  TEST_ASSERT_EQUAL_UINT(2, t.getLoop(0).capture.store.size());
}
void test_record_length_summary_handles_out_of_order_input_and_removal() {
  LoopEventStore::initPool();
  Track& t = trackManager.getSelectedTrack();
  t.ensureLoopsAllocated();
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  t.startRecording(0);
  Loop& loop = t.getLoop(0);
  loop.loopLengthTicks = 3072; // The removal API requires established geometry.
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(12, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(96, 1, 60, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(1800, 1, 62, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::ControlChange(100, 1, 1, 64)));
  TEST_ASSERT_TRUE(loop.removeOpenCaptureNoteOn(1, 62));
  t.stopRecordingToStopped(3072);
  TEST_ASSERT_EQUAL_UINT32(768, loop.loopLengthTicks);
}
void test_record_length_uses_maximum_not_last_appended_tick() {
  LoopEventStore::initPool();
  Track& t = trackManager.getSelectedTrack();
  t.ensureLoopsAllocated();
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  t.startRecording(0);
  Loop& loop = t.getLoop(0);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::ControlChange(1800, 1, 1, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::ControlChange(100, 1, 2, 64)));
  t.stopRecordingToStopped(3072);
  TEST_ASSERT_EQUAL_UINT32(2304, loop.loopLengthTicks);
}
void staleAcceptedStop(bool replaceLoop) {
  LoopEventStore::initPool();
  Track& t = trackManager.getSelectedTrack();
  t.ensureLoopsAllocated();
  prepareSlot(t, 0, true);
  t.forceSetState(TRACK_PLAYING);
  start(t, 0, true, 0, 60);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::occupyCanonicalOutput(t));
  stop(t, true, 768);
  TEST_ASSERT_TRUE(t.hasAcceptedUnexecutedInteraction());
  TEST_ASSERT_EQUAL_UINT(0, TrackCaptureStopTestAccess::jobs(t));
  Loop& loop = t.getLoop(0);
  const size_t rows = loop.capture.store.size();
  // Fault injection: a replacement reuses the slot without advancing Track's counter.
  // The old request must not park or consume the replacement's resources.
  if (replaceLoop) ++loop.loopId;
  else loop.capture.phase = CapturePhase::Record;
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::drive(t));
  TEST_ASSERT_FALSE(t.hasAcceptedUnexecutedInteraction());
  TEST_ASSERT_EQUAL_UINT(0, TrackCaptureStopTestAccess::jobs(t));
  TEST_ASSERT_EQUAL_UINT(rows, loop.capture.store.size());
}
void test_replaced_loop_supersedes_accepted_stop() { staleAcceptedStop(true); }
void test_changed_capture_mode_supersedes_accepted_stop() { staleAcceptedStop(false); }
void test_denied_record_preflight_preserves_content_and_interval_then_retries() {
  LoopEventStore::initPool();
  Track& t = trackManager.getSelectedTrack();
  t.ensureLoopsAllocated();
  Loop& loop = t.getLoop(0);
  loop.loopLengthTicks = 768;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(12, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(96, 1, 60, 0)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(768, CommitReason::RecordStop));
  loop.commitPendingCapturePass();
  const PassId oldPass = loop.passes.recordPass.id;
  t.forceSetState(TRACK_PLAYING);
  LoopEventStore::ChunkReservation occupied;
  TEST_ASSERT_TRUE(occupied.reserveChunks(LoopEventStore::freeChunkCount()));
  t.startRecording(1000);
  TEST_ASSERT_TRUE(t.isPlaying());
  TEST_ASSERT_EQUAL_UINT64(0, TrackCaptureStopTestAccess::interval(t));
  TEST_ASSERT_EQUAL(oldPass, loop.passes.recordPass.id);
  TEST_ASSERT_EQUAL_UINT32(768, loop.loopLengthTicks);
  TEST_ASSERT_TRUE(occupied.releaseGrain(LoopEventStoreConfig::POOL_CHUNK_COUNT));
  t.startRecording(1200);
  TEST_ASSERT_TRUE(t.isRecording());
  TEST_ASSERT_EQUAL_UINT64(1, TrackCaptureStopTestAccess::interval(t));
  TEST_ASSERT_FALSE(loop.hasCommittedPasses());
  TEST_ASSERT_EQUAL_UINT32(1200, loop.startLoopTick);
}
// Publication is observed by phase, not an arbitrary count of maintenance turns.
void advanceToPhase(Track& t, PendingStopPhase phase) {
  const uint64_t budget = TrackCaptureStopTestAccess::budget(t);
  for (uint64_t grain = 0; grain < budget && grain < 20000; ++grain) {
    if (TrackCaptureStopTestAccess::phase(t) == phase) return;
    TrackCaptureStopTestAccess::step(t);
  }
  TrackCaptureStopTestAccess::diagnostic(t);
  TEST_FAIL_MESSAGE("publication phase not reached within admitted work");
}
void assertNoPersistenceRequest() {
  TEST_ASSERT_FALSE(StorageManagerInternal::storageSession.currentWorkspaceSave.pending);
  TEST_ASSERT_EQUAL_UINT(0, PersistenceWorkQueue::queueDepth());
  for (const auto& row : StorageManagerInternal::currentSetLoopSlotDirty)
    for (bool dirty : row) TEST_ASSERT_FALSE(dirty);
}
void deferredPersistence(bool stopped, bool superseded, bool stale) {
  LoopEventStore::initPool();
  // Use a non-selected track: persistence must resolve the owner, not UI focus.
  Track& t = trackManager.getTrack(2);
  t.ensureLoopsAllocated();
  prepareSlot(t, 0, true);
  prepareSlot(t, 1, true);
  t.forceSetState(TRACK_PLAYING);
  start(t, 0, true, 0, 60);
  clockManager.assignCurrentTickSilently(768);
  if (stopped) t.stopOverdubbingToStopped(); else t.stopOverdubbing();
  TEST_ASSERT_EQUAL_UINT(1, TrackCaptureStopTestAccess::jobs(t));
  if (stopped) t.forceSetState(TRACK_PLAYING);
  start(t, 1, true, 768, 62);
  assertNoPersistenceRequest();
  advanceToPhase(t, PendingStopPhase::AtomicTransfer);
  assertNoPersistenceRequest();
  Loop& owner = t.getLoop(0);
  TEST_ASSERT_EQUAL_UINT(0, owner.passes.overdubPasses.size());
  if (stale) {
    // Fault-inject the adoption: a replacement interval reuses the slot *number* with a new LoopId.
    // S4f.4 transition 3 makes such a job **superseded** -- reported and retired -- rather than
    // parked: parking would wait forever for an interval that can never return, holding the job's
    // raw range, its capture-stop owner and its ring entry so no later stop on this track could be
    // admitted. It must publish nothing and admit no persistence for the interval that is gone.
    const LoopId original = owner.loopId;
    ++owner.loopId;
    for (unsigned i = 0; i < 4; ++i) TrackCaptureStopTestAccess::step(t);
    owner.loopId = original; // restore even when the assertion below aborts this case
    assertNoPersistenceRequest();
    TEST_ASSERT_FALSE_MESSAGE(t.hasStopInFlight(),
                              "a job whose interval identity is gone must be superseded, not parked");
    TEST_ASSERT_EQUAL_UINT(0, owner.passes.overdubPasses.size());
    TEST_ASSERT_TRUE(t.isOverdubbing());
    TEST_ASSERT_EQUAL_UINT(2, t.getLoop(1).capture.store.size());
    return;
  }
  if (superseded) ++owner.playbackRevision; // fault-inject a replaced causal base
  advanceToPhase(t, PendingStopPhase::DeferredCleanup);
  TEST_ASSERT_EQUAL_UINT(1, owner.passes.overdubPasses.size());
  TEST_ASSERT_TRUE(StorageManagerInternal::currentSetLoopSlotDirty[2][0]);
  TEST_ASSERT_FALSE(StorageManagerInternal::currentSetLoopSlotDirty[2][1]);
  TEST_ASSERT_FALSE(StorageManagerInternal::currentSetLoopSlotDirty[0][0]);
  TEST_ASSERT_TRUE(StorageManagerInternal::storageSession.currentWorkspaceSave.pending);
  TEST_ASSERT_TRUE(StorageManagerInternal::storageSession.currentWorkspaceSave.urgentRequested);
  TEST_ASSERT_EQUAL_UINT(2, PersistenceWorkQueue::queueDepth());
  for (auto type : {PersistWorkType::SlotMeta, PersistWorkType::LoopPersist})
    TEST_ASSERT_EQUAL(PersistWorkState::Queued,
      PersistenceWorkQueue::workState(type, persistKeyForSlot(2, 0)));
  // Acknowledge work at the I/O boundary so coalescing cannot hide repeat requests.
  PersistWorkItem item;
  while (PersistenceWorkQueue::beginWriteQueuedItem(item)) PersistenceWorkQueue::markItemPersisted(item);
  StorageManagerInternal::storageSession.currentWorkspaceSave.pending = false;
  StorageManagerInternal::currentSetLoopSlotDirty[2][0] = false;
  const uint64_t budget = TrackCaptureStopTestAccess::budget(t);
  for (uint64_t grain = 0; t.hasStopInFlight() && grain < budget && grain < 20000; ++grain)
    TrackCaptureStopTestAccess::step(t);
  TEST_ASSERT_FALSE(t.hasStopInFlight());
  for (unsigned i = 0; i < 10; ++i) TrackCaptureStopTestAccess::step(t);
  assertNoPersistenceRequest();
  TEST_ASSERT_TRUE(t.isOverdubbing());
  TEST_ASSERT_EQUAL_UINT(2, t.getLoop(1).capture.store.size());
  TEST_ASSERT_EQUAL_UINT(1, owner.passes.overdubPasses.size());
  TEST_ASSERT_EQUAL(PersistWorkState::Persisted,
    PersistenceWorkQueue::workState(PersistWorkType::LoopPersist, persistKeyForSlot(2, 0)));
}
void test_deferred_publication_requests_own_slot_once() { deferredPersistence(false, false, false); }
void test_stopped_publication_requests_own_slot_once() { deferredPersistence(true, false, false); }
void test_superseded_publication_requests_own_slot_once() { deferredPersistence(false, true, false); }
void test_stale_publication_is_superseded_without_persistence() { deferredPersistence(false, false, true); }
void test_record_closures_keep_identity_after_raw_source_freezes() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.startRecording(100);
  t.noteOn(1, 60, 100, 112);
  t.noteOn(2, 60, 90, 120);
  Loop& loop = t.getLoop(0);
  const NoteId physicalId = loop.capture.store.at(0).noteId;
  const NoteId stopId = loop.capture.store.at(1).noteId;
  TEST_ASSERT_NOT_EQUAL(physicalId, stopId);
  t.noteOn(1, 60, 80, 130); // rejected duplicate must not replace the receipt
  TEST_ASSERT_EQUAL(2, loop.capture.store.size());
  LoopEventStore::CaptureRange frozen;
  TEST_ASSERT_TRUE(frozen.reserveTail());
  TEST_ASSERT_TRUE(loop.capture.store.sealCaptureRange(frozen));
  // Another producer row cannot supply the frozen interval's closure identity.
  MidiEvent other = MidiEvent::NoteOn(30, 1, 60, 70);
  other.noteId = loop.allocateNoteId();
  TEST_ASSERT_TRUE(loop.capture.store.append(other));
  t.noteOff(1, 60, 0, 180);
  TEST_ASSERT_EQUAL(physicalId, loop.capture.store.at(1).noteId);
  loop.loopLengthTicks = 192;
  t.finalizePendingNotes(400);
  TEST_ASSERT_EQUAL(3, loop.capture.store.size());
  TEST_ASSERT_EQUAL(stopId, loop.capture.store.at(2).noteId);
  TEST_ASSERT_EQUAL(191, loop.capture.store.at(2).tick);
  TEST_ASSERT_EQUAL(2, loop.capture.store.at(2).channel);
  TEST_ASSERT_EQUAL(2, frozen.size());
  MidiEvent original; bool end = false;
  TEST_ASSERT_TRUE(frozen.read(frozen.generation(), 0, 0, original, end));
  TEST_ASSERT_EQUAL(physicalId, original.noteId);
}

void test_record_stop_commits_exact_held_note_closures() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.startRecording(100);
  t.noteOn(1, 60, 100, 112);
  t.noteOn(2, 61, 90, 300);
  Loop& loop = t.getLoop(0);
  const NoteId first = loop.capture.store.at(0).noteId;
  const NoteId second = loop.capture.store.at(1).noteId;
  t.stopRecordingToStopped(868);
  driveStopToRetirement(t);
  TEST_ASSERT_EQUAL(768, loop.loopLengthTicks);
  TEST_ASSERT_EQUAL(0, TrackCaptureStopTestAccess::heldCount(t));
  SessionMidiEventVec rows;
  LoopEventStore::appendChunkRefEvents(loop.passes.recordPass.committedChunkIds, rows);
  // The existing stopped-record transition projects this boundary to phase zero;
  // full stop normalization adds invalid-ID terminal Offs at length-1. Preserve
  // that current six-row content here; this slice changes identity lookup only.
  TEST_ASSERT_EQUAL(6, rows.size());
  unsigned ons[2] = {}, offs[2] = {};
  for (const auto& row : rows) {
    TEST_ASSERT_TRUE(row.channel == 1 || row.channel == 2);
    const unsigned channel = row.channel - 1;
    TEST_ASSERT_EQUAL(channel == 0 ? 60 : 61, row.data.noteData.note);
    if (row.isNoteOn()) {
      ++ons[channel];
      TEST_ASSERT_EQUAL(channel == 0 ? 12 : 200, row.tick);
      TEST_ASSERT_EQUAL(channel == 0 ? first : second, row.noteId);
    } else {
      TEST_ASSERT_TRUE(row.isNoteOff());
      ++offs[channel];
      TEST_ASSERT_TRUE(row.tick == 0 || row.tick == 767);
      TEST_ASSERT_EQUAL(row.tick == 767 ? kInvalidNoteId : (channel == 0 ? first : second), row.noteId);
    }
  }
  TEST_ASSERT_EQUAL(1, ons[0]); TEST_ASSERT_EQUAL(1, ons[1]);
  TEST_ASSERT_EQUAL(2, offs[0]); TEST_ASSERT_EQUAL(2, offs[1]);
}

void test_record_identity_requires_accepted_input_and_handles_preroll() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  TEST_ASSERT_TRUE(t.setState(TRACK_ARMED));
  t.noteOn(1, 60, 100, 12);
  t.startRecording(100);
  Loop& loop = t.getLoop(0);
  TEST_ASSERT_EQUAL(1, loop.capture.store.size());
  TEST_ASSERT_EQUAL(0, loop.capture.store.at(0).tick);
  TEST_ASSERT_EQUAL(loop.capture.store.at(0).noteId,
      TrackCaptureStopTestAccess::recordIdentity(t, 1, 60));
  const NoteId first = loop.capture.store.at(0).noteId;
  // Existing phase-denial seam: rejected input must not leave a closure receipt.
  loop.capture.phase = CapturePhase::None;
  t.noteOn(1, 62, 100, 120);
  TEST_ASSERT_EQUAL(1, TrackCaptureStopTestAccess::heldCount(t));
  TEST_ASSERT_EQUAL(kInvalidNoteId, TrackCaptureStopTestAccess::recordIdentity(t, 1, 62));
  loop.capture.phase = CapturePhase::Record;
  t.noteOn(1, 62, 100, 130);
  const NoteId retried = loop.capture.store.at(1).noteId;
  TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, retried);
  TEST_ASSERT_NOT_EQUAL(first, retried);
  TEST_ASSERT_EQUAL(retried, TrackCaptureStopTestAccess::recordIdentity(t, 1, 62));
  t.noteOff(1, 62, 0, 160);
  TEST_ASSERT_EQUAL(retried, loop.capture.store.at(2).noteId);
  TEST_ASSERT_EQUAL(kInvalidNoteId, TrackCaptureStopTestAccess::recordIdentity(t, 1, 62));
  printf("PendingNote bytes=%zu recordId offset=%zu\n", sizeof(PendingNote), offsetof(PendingNote, recordNoteId));
}

void test_record_stop_closure_does_not_search_reused_producer() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.startRecording(100);
  t.noteOn(1, 60, 100, 112);
  Loop& loop = t.getLoop(0);
  const NoteId identity = loop.capture.store.at(0).noteId;
  LoopEventStore::CaptureRange frozen;
  TEST_ASSERT_TRUE(frozen.reserveTail());
  TEST_ASSERT_TRUE(loop.capture.store.sealCaptureRange(frozen));
  MidiEvent unrelated = MidiEvent::NoteOn(40, 1, 60, 90);
  unrelated.noteId = loop.allocateNoteId();
  TEST_ASSERT_TRUE(loop.capture.store.append(unrelated));
  loop.loopLengthTicks = 192;
  t.finalizePendingNotes(400);
  TEST_ASSERT_EQUAL(2, loop.capture.store.size());
  TEST_ASSERT_EQUAL(identity, loop.capture.store.at(1).noteId);
  TEST_ASSERT_EQUAL(191, loop.capture.store.at(1).tick);
  TEST_ASSERT_EQUAL(0, TrackCaptureStopTestAccess::heldCount(t));
  TEST_ASSERT_EQUAL(1, frozen.size());
}

void test_record_job_closures_preserve_a_live_successor() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false); prepareSlot(t, 1, false);
  t.startRecording(100);
  t.noteOn(1, 60, 100, 112); t.noteOn(2, 61, 90, 120);
  const NoteId first = t.getLoop(0).capture.store.at(0).noteId;
  const NoteId second = t.getLoop(0).capture.store.at(1).noteId;
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, 1000, 768);
  auto& runtime = TrackCaptureStopTestAccess::closureRuntime(t);
  t.forceSetState(TRACK_PLAYING); // binding transition controlled by fixture
  t.setActiveLoopIndex(1); t.startRecording(1100);
  TEST_ASSERT_TRUE(t.isRecording());
  t.noteOn(3, 76, 80, 1112);
  const auto chunks = LoopEventStore::usedChunkCount();
  for (unsigned i = 0; i < 2; ++i) {
    job.admission.destination = i == 0 ? PendingStopDestination::Playing : PendingStopDestination::Stopped;
    TEST_ASSERT_EQUAL(i == 0 ? LoopEventStoreRewriteResult::Pending : LoopEventStoreRewriteResult::Complete,
        TrackCaptureStopTestAccess::closureGrain(t));
    TEST_ASSERT_EQUAL(i + 1, runtime.canonicalCaptureOutput.store.size());
    TEST_ASSERT_EQUAL(1 - i, job.pendingNotes.size());
    TEST_ASSERT_EQUAL(1 - i, job.admission.remainingClosureGrains);
    TEST_ASSERT_EQUAL(1, TrackCaptureStopTestAccess::heldCount(t));
    TEST_ASSERT_EQUAL(1, t.getLoop(1).capture.store.size());
    TEST_ASSERT_EQUAL(chunks, LoopEventStore::usedChunkCount());
    TEST_ASSERT_EQUAL(2, TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.size());
    for (size_t raw = 0; raw < 2; ++raw) {
      MidiEvent event; bool end = false;
      TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.read(TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.generation(), 0, raw, event, end));
      TEST_ASSERT_FALSE(end); TEST_ASSERT_TRUE(event.isNoteOn());
      TEST_ASSERT_EQUAL(raw == 0 ? first : second, event.noteId);
      TEST_ASSERT_EQUAL(raw == 0 ? 12 : 20, event.tick);
    }
    if (i == 0) {
      // Rebinding the runtime at the same address must not adopt old output.
      ++job.admission.frozenGeneration; ++runtime.captureStopOwnerGeneration;
      TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied, TrackCaptureStopTestAccess::closureGrain(t));
      TEST_ASSERT_EQUAL(1, runtime.canonicalCaptureOutput.store.size());
      TEST_ASSERT_EQUAL(1, job.pendingNotes.size());
      --job.admission.frozenGeneration; --runtime.captureStopOwnerGeneration;
      PendingStopAdmission anotherDescriptor;
      anotherDescriptor.frozenGeneration = job.admission.frozenGeneration;
      runtime.frozenCaptureRewrite.sourceOwner = &anotherDescriptor;
      TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied, TrackCaptureStopTestAccess::closureGrain(t));
      TEST_ASSERT_EQUAL(1, runtime.canonicalCaptureOutput.store.size());
      TEST_ASSERT_EQUAL(1, job.pendingNotes.size());
      runtime.frozenCaptureRewrite.sourceOwner = &job.admission;
    }
  }
  TEST_ASSERT_EQUAL(CapturePhase::Record, runtime.canonicalCaptureOutput.phase);
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordSourceStaging, job.admission.phase);
  for (size_t i = 0; i < 2; ++i) {
    const auto& row = runtime.canonicalCaptureOutput.store.at(i);
    TEST_ASSERT_TRUE(row.isNoteOff()); TEST_ASSERT_EQUAL(767, row.tick);
    TEST_ASSERT_EQUAL(row.channel == 1 ? first : second, row.noteId);
    TEST_ASSERT_EQUAL(row.channel == 1 ? 60 : 61, row.data.noteData.note);
  }
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, TrackCaptureStopTestAccess::closureGrain(t));
  TEST_ASSERT_EQUAL(2, runtime.canonicalCaptureOutput.store.size());
}

void test_record_source_staging_merges_the_frozen_take_after_closure() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.startRecording(100);
  t.noteOn(1, 60, 100, 112);
  t.noteOn(2, 61, 90, 130);
  const NoteId first = t.getLoop(0).capture.store.at(0).noteId;
  const NoteId second = t.getLoop(0).capture.store.at(1).noteId;
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, 1000, 768);
  auto& runtime = TrackCaptureStopTestAccess::closureRuntime(t);
  LoopEventStoreRewriteResult result = LoopEventStoreRewriteResult::Pending;
  while (result == LoopEventStoreRewriteResult::Pending) {
    result = TrackCaptureStopTestAccess::closureGrain(t);
  }
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, result);
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordSourceStaging, job.admission.phase);
  TEST_ASSERT_EQUAL(2, runtime.canonicalCaptureOutput.store.size());
  TEST_ASSERT_EQUAL(2, TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.size());

  // A runtime owner generation that no longer matches the job must not stage.
  ++runtime.captureStopOwnerGeneration;
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied, TrackCaptureStopTestAccess::stagingGrain(t));
  TEST_ASSERT_EQUAL(2, runtime.canonicalCaptureOutput.store.size());
  --runtime.captureStopOwnerGeneration;

  result = LoopEventStoreRewriteResult::Pending;
  size_t turns = 0;
  while (result == LoopEventStoreRewriteResult::Pending && turns++ < 4096) {
    result = TrackCaptureStopTestAccess::stagingGrain(t);
  }
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, result);
  TEST_ASSERT_EQUAL(PendingStopPhase::CanonicalOrder, job.admission.phase);

  // The closure Offs stay at the head and the frozen take follows with its identities.
  const auto& store = runtime.canonicalCaptureOutput.store;
  TEST_ASSERT_EQUAL(4, store.size());
  TEST_ASSERT_TRUE(store.at(0).isNoteOff());
  TEST_ASSERT_TRUE(store.at(1).isNoteOff());
  TEST_ASSERT_TRUE(store.at(2).isNoteOn());
  TEST_ASSERT_TRUE(store.at(3).isNoteOn());
  bool sawFirst = false, sawSecond = false;
  for (size_t i = 2; i < 4; ++i) {
    sawFirst = sawFirst || store.at(i).noteId == first;
    sawSecond = sawSecond || store.at(i).noteId == second;
  }
  TEST_ASSERT_TRUE(sawFirst);
  TEST_ASSERT_TRUE(sawSecond);
}

void test_record_normalization_orders_and_identity_completes_the_merged_store() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.startRecording(100);
  t.noteOn(1, 60, 100, 112);
  t.noteOn(2, 61, 90, 120);
  const NoteId first = t.getLoop(0).capture.store.at(0).noteId;
  const NoteId second = t.getLoop(0).capture.store.at(1).noteId;
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, 1000, 768);
  auto& runtime = TrackCaptureStopTestAccess::closureRuntime(t);
  LoopEventStoreRewriteResult result = LoopEventStoreRewriteResult::Pending;
  while (result == LoopEventStoreRewriteResult::Pending) {
    result = TrackCaptureStopTestAccess::closureGrain(t);
  }
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, result);
  result = LoopEventStoreRewriteResult::Pending;
  size_t turns = 0;
  while (result == LoopEventStoreRewriteResult::Pending && turns++ < 4096) {
    result = TrackCaptureStopTestAccess::stagingGrain(t);
  }
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, result);
  TEST_ASSERT_EQUAL(PendingStopPhase::CanonicalOrder, job.admission.phase);
  TEST_ASSERT_EQUAL(4, runtime.canonicalCaptureOutput.store.size());

  // A runtime binding that no longer matches the job must not normalize.
  ++runtime.captureStopOwnerGeneration;
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied,
                    TrackCaptureStopTestAccess::normalizationGrain(t));
  TEST_ASSERT_EQUAL(4, runtime.canonicalCaptureOutput.store.size());
  --runtime.captureStopOwnerGeneration;

  result = LoopEventStoreRewriteResult::Pending;
  turns = 0;
  while (result == LoopEventStoreRewriteResult::Pending && turns++ < 65536) {
    result = TrackCaptureStopTestAccess::normalizationGrain(t);
  }
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, result);
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPrepared, job.admission.phase);

  // Canonical order: the two staged NoteOns by tick, then the terminal Offs.
  const auto& store = runtime.canonicalCaptureOutput.store;
  TEST_ASSERT_EQUAL(4, store.size());
  TEST_ASSERT_TRUE(store.at(0).isNoteOn());
  TEST_ASSERT_EQUAL(12, store.at(0).tick);
  TEST_ASSERT_TRUE(store.at(1).isNoteOn());
  TEST_ASSERT_EQUAL(20, store.at(1).tick);
  TEST_ASSERT_TRUE(store.at(2).isNoteOff());
  TEST_ASSERT_TRUE(store.at(3).isNoteOff());
  TEST_ASSERT_EQUAL(767, store.at(2).tick);
  TEST_ASSERT_EQUAL(767, store.at(3).tick);
  bool sawFirst = false, sawSecond = false;
  for (size_t i = 0; i < 2; ++i) {
    sawFirst = sawFirst || store.at(i).noteId == first;
    sawSecond = sawSecond || store.at(i).noteId == second;
  }
  TEST_ASSERT_TRUE(sawFirst);
  TEST_ASSERT_TRUE(sawSecond);

  // The terminal phase must not re-run preparation: it waits for publication.
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied,
                    TrackCaptureStopTestAccess::normalizationGrain(t));
  TEST_ASSERT_EQUAL(4, runtime.canonicalCaptureOutput.store.size());

  // Bounded pass transfer into the job-owned receipt; nothing is published yet.
  TEST_ASSERT_TRUE(t.getLoop(0).prewarmActiveOverdubTransferHorizon());
  TEST_ASSERT_TRUE(t.getLoop(0).hasActiveOverdubTransferAdmission());
  result = LoopEventStoreRewriteResult::Pending;
  turns = 0;
  while (result == LoopEventStoreRewriteResult::Pending && turns++ < 65536) {
    result = TrackCaptureStopTestAccess::passTransferGrain(t);
  }
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, result);
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPassReady, job.admission.phase);
  TEST_ASSERT_TRUE(job.ownsPendingPass);
  TEST_ASSERT_FALSE(job.pendingPass.committedChunkIds.empty());
  TEST_ASSERT_EQUAL(CapturePassPhase::Record, job.pendingPass.phase);
  TEST_ASSERT_EQUAL(0, runtime.canonicalCaptureOutput.store.size());

  // The terminal wait refuses to transfer twice.
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied,
                    TrackCaptureStopTestAccess::passTransferGrain(t));

  // Atomic publication: the staged pass becomes canonical and its Record undo is pushed.
  // A successor record start rewrites the Track's inline baseline first; the publication
  // must still use this interval's job-owned receipt.
  const UndoLoopGeometry jobBefore = TrackCaptureStopTestAccess::jobBeforeGeometry(t);
  TrackCaptureStopTestAccess::seedSuccessorRecordBaseline(t, 4242, 4243, 4244);
  TEST_ASSERT_EQUAL(0, TrackUndo::getUndoCount(t));
  result = LoopEventStoreRewriteResult::Pending;
  turns = 0;
  while (result == LoopEventStoreRewriteResult::Pending && turns++ < 4096) {
    result = TrackCaptureStopTestAccess::publicationGrain(t);
  }
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, result);
  // Task 3.15g: publication pops the interaction job, so the terminal phase is read through the
  // presentation-aware accessor rather than from the retired ring entry.
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPublished, TrackCaptureStopTestAccess::phase(t));
  TEST_ASSERT_TRUE(t.getLoop(0).passes.hasRecordPass());
  TEST_ASSERT_EQUAL(1, TrackUndo::getUndoCount(t));
  const auto& tip = t.getGlobalUndoStack().entries.back();
  TEST_ASSERT_EQUAL(UndoEntryKind::RecordPassAdded, tip.kind);
  TEST_ASSERT_EQUAL(jobBefore.loopLengthTicks, tip.beforeGeometry.loopLengthTicks);
  TEST_ASSERT_EQUAL(jobBefore.startLoopTick, tip.beforeGeometry.startLoopTick);
  TEST_ASSERT_EQUAL(jobBefore.loopStartTick, tip.beforeGeometry.loopStartTick);
  TEST_ASSERT_NOT_EQUAL(4242u, tip.beforeGeometry.loopLengthTicks);
  TEST_ASSERT_EQUAL(t.getLoop(0).loopLengthTicks, tip.afterGeometry.loopLengthTicks);

  // Bounded retirement releases the frozen source and returns the ring entry.
  const size_t depthBefore = TrackCaptureStopTestAccess::stopQueueDepth(t);
  result = LoopEventStoreRewriteResult::Pending;
  turns = 0;
  while (result == LoopEventStoreRewriteResult::Pending && turns++ < 65536) {
    result = TrackCaptureStopTestAccess::retirementGrain(t);
  }
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, result);
  // Task 3.15g: publication pops the interaction job synchronously, so the ring was already empty
  // before this retirement ran; the obligation it finishes belongs to the presentation record.
  TEST_ASSERT_EQUAL(0, depthBefore);
  TEST_ASSERT_EQUAL(0, TrackCaptureStopTestAccess::stopQueueDepth(t));
}

void test_record_job_closure_denial_retains_obligation_for_retry() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false); t.startRecording(100); t.noteOn(1, 60, 100, 112);
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, 96, 768);
  auto& runtime = TrackCaptureStopTestAccess::closureRuntime(t);
  auto denied = [&]() {
    TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied, TrackCaptureStopTestAccess::closureGrain(t));
    TEST_ASSERT_EQUAL(1, job.pendingNotes.size());
    TEST_ASSERT_EQUAL(1, job.admission.remainingClosureGrains);
    TEST_ASSERT_EQUAL(0, runtime.canonicalCaptureOutput.store.size());
  };
  job.mode = CapturePhase::Overdub; denied(); job.mode = CapturePhase::Record;
  ++runtime.captureStopOwnerGeneration; denied(); --runtime.captureStopOwnerGeneration;
  ++job.loopId; denied(); --job.loopId;
  const auto id = job.pendingNotes.begin()->second.recordNoteId;
  job.pendingNotes.begin()->second.recordNoteId = kInvalidNoteId; denied();
  job.pendingNotes.begin()->second.recordNoteId = id;
  TEST_ASSERT_TRUE(runtime.canonicalCaptureChunks.releaseGrain(LoopEventStoreConfig::POOL_CHUNK_COUNT));
  denied();
  TEST_ASSERT_TRUE(runtime.canonicalCaptureChunks.reserveChunks(1));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, TrackCaptureStopTestAccess::closureGrain(t));
  TEST_ASSERT_EQUAL(id, runtime.canonicalCaptureOutput.store.at(0).noteId);
  TEST_ASSERT_EQUAL(96, runtime.canonicalCaptureOutput.store.at(0).tick);
}

void test_record_job_closure_rejects_reused_raw_and_replaced_output() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false); t.startRecording(100);
  t.noteOn(1, 60, 100, 112); t.noteOn(1, 61, 100, 120);
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, 96, 768);
  auto& runtime = TrackCaptureStopTestAccess::closureRuntime(t);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Pending, TrackCaptureStopTestAccess::closureGrain(t));
  runtime.canonicalCaptureOutput.store.clear();
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied, TrackCaptureStopTestAccess::closureGrain(t));
  TEST_ASSERT_EQUAL(1, job.pendingNotes.size());
  TEST_ASSERT_EQUAL(1, job.admission.remainingClosureGrains);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.releaseGrain(1));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.reserveTail());
  TEST_ASSERT_TRUE(job.owner->capture.store.append(MidiEvent::NoteOn(12, 1, 60, 90)));
  TEST_ASSERT_TRUE(job.owner->capture.store.sealCaptureRange(TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange));
  // Remove the output fault so the saved raw generation is the remaining denial.
  runtime.discardFrozenCaptureCanonicalOrder();
  TEST_ASSERT_TRUE(runtime.canonicalCaptureOutput.store.prewarmCaptureDirectory());
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied, TrackCaptureStopTestAccess::closureGrain(t));
  TEST_ASSERT_EQUAL(1, job.pendingNotes.size());
  TEST_ASSERT_EQUAL(1, job.admission.remainingClosureGrains);
}

void test_record_job_closures_exceed_overdub_hold_limit_without_staging() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false); t.startRecording(100);
  constexpr unsigned count = LoopEventStoreConfig::CHUNK_CAPACITY + 1;
  static_assert(count > RuntimePlaybackViewConfig::kMaxCaptureNoteReservations, "exercise Record envelope");
  std::array<NoteId, count> identities{};
  for (unsigned i = 0; i < count; ++i) {
    t.noteOn(1 + i / 128, i % 128, 100, 112 + i);
    identities[i] = t.getLoop(0).capture.store.at(i).noteId;
  }
  TEST_ASSERT_EQUAL(count, TrackCaptureStopTestAccess::heldCount(t));
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, 700, 768);
  auto& runtime = TrackCaptureStopTestAccess::closureRuntime(t);
  const auto chunks = LoopEventStore::usedChunkCount();
  for (unsigned i = 0; i < count; ++i) {
    TEST_ASSERT_EQUAL(i + 1 == count ? LoopEventStoreRewriteResult::Complete : LoopEventStoreRewriteResult::Pending,
        TrackCaptureStopTestAccess::closureGrain(t));
    TEST_ASSERT_EQUAL(i + 1, runtime.canonicalCaptureOutput.store.size());
    TEST_ASSERT_EQUAL(count - i - 1, job.pendingNotes.size());
    TEST_ASSERT_EQUAL(chunks, LoopEventStore::usedChunkCount());
  }
  TEST_ASSERT_EQUAL(count, TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.size());
  std::array<bool, count> seen{};
  for (unsigned i = 0; i < count; ++i) {
    const auto& row = runtime.canonicalCaptureOutput.store.at(i);
    const size_t input = (row.channel - 1) * 128 + row.data.noteData.note;
    TEST_ASSERT_LESS_THAN(count, input);
    TEST_ASSERT_FALSE(seen[input]); seen[input] = true;
    TEST_ASSERT_TRUE(row.isNoteOff()); TEST_ASSERT_EQUAL(700, row.tick);
    TEST_ASSERT_EQUAL(identities[input], row.noteId);
  }
}

void test_record_job_without_held_notes_emits_no_closures() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false); t.startRecording(100);
  t.noteOn(1, 60, 100, 112); t.noteOff(1, 60, 0, 180);
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, 96, 768);
  auto& runtime = TrackCaptureStopTestAccess::closureRuntime(t);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, TrackCaptureStopTestAccess::closureGrain(t));
  TEST_ASSERT_EQUAL(0, runtime.canonicalCaptureOutput.store.size());
  TEST_ASSERT_EQUAL(0, job.admission.remainingClosureGrains);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, TrackCaptureStopTestAccess::closureGrain(t));
}

// ---------------------------------------------------------------------------
// S4f-CLOSURE-PLAN Step 1: preparation/publication production-worker boundaries.
// ---------------------------------------------------------------------------

using GrainFn = LoopEventStoreRewriteResult (*)(Track&);

static LoopEventStoreRewriteResult driveGrains(Track& t, GrainFn grain, size_t limit = 200000) {
  LoopEventStoreRewriteResult result = LoopEventStoreRewriteResult::Pending;
  size_t turns = 0;
  while (result == LoopEventStoreRewriteResult::Pending && turns++ < limit) {
    result = grain(t);
    if (result == LoopEventStoreRewriteResult::Pending &&
        grain == TrackCaptureStopTestAccess::retirementGrain) {
      auto& job = TrackCaptureStopTestAccess::head(t);
      if (job.mode == CapturePhase::Record &&
          job.admission.phase == PendingStopPhase::RecordPublished && job.owner != nullptr &&
          job.owner->pendingSnapshotSessionExitQueued() && !job.owner->publishedSnapshotIsCurrent()) {
        t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
      }
    }
  }
  return result;
}

/// Drive closure -> staging -> normalization to the prepared terminal.
static void driveToPrepared(Track& t) {
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::closureGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::stagingGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::normalizationGrain));
}

void test_record_publication_preflight_denies_before_the_irreversible_commit() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.startRecording(100);
  t.noteOn(1, 60, 100, 112);
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, 768, 768);
  driveToPrepared(t);
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPrepared, job.admission.phase);
  TEST_ASSERT_TRUE(t.getLoop(0).prewarmActiveOverdubTransferHorizon());
  TEST_ASSERT_TRUE(t.getLoop(0).hasActiveOverdubTransferAdmission());
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::passTransferGrain));
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPassReady, job.admission.phase);
  TEST_ASSERT_TRUE(job.ownsPendingPass);
  TEST_ASSERT_FALSE(job.pendingPass.committedChunkIds.empty());

  // A missing job-owned geometry receipt must deny BEFORE the irreversible commit:
  // committing the pass first would publish content with no `RecordPassAdded` undo
  // entry, no persistence, and no retryable receipt.
  TrackCaptureStopTestAccess::clearGeometryReceipt(t);
  const size_t queueBefore = PersistenceWorkQueue::queueDepth();
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied,
                    TrackCaptureStopTestAccess::publicationGrain(t));
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPassReady, job.admission.phase);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::ownsPendingPass(t),
                           "the job-owned pass must survive a preflight denial");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::hasRecordPass(t, 0),
                            "no content may be published before the preflight passes");
  TEST_ASSERT_EQUAL(0, TrackUndo::getUndoCount(t));
  TEST_ASSERT_EQUAL(queueBefore, PersistenceWorkQueue::queueDepth());
  TEST_ASSERT_FALSE(StorageManagerInternal::currentSetLoopSlotDirty[0][0]);

  // The same binding retries once the receipt is back: exactly one publish.
  TrackCaptureStopTestAccess::restoreGeometryReceipt(t, job.loopId);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::publicationGrain));
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPublished, TrackCaptureStopTestAccess::phase(t));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::hasRecordPass(t, 0));
  TEST_ASSERT_EQUAL(1, TrackUndo::getUndoCount(t));
  TEST_ASSERT_TRUE(StorageManagerInternal::currentSetLoopSlotDirty[0][0]);
}

void test_record_pass_transfer_continues_after_the_store_empties() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.startRecording(100);
  t.noteOn(1, 60, 100, 112);
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, 768, 768);
  auto& runtime = TrackCaptureStopTestAccess::closureRuntime(t);
  auto& output = runtime.canonicalCaptureOutput.store;
  // Fill the prepared private output with enough chunks that one bounded transfer
  // grain moves every chunk and still has destination-order reversals left: the
  // store empties while the transfer is provably Pending. 24 chunks at the
  // production 32-unit preparation quantum (24 moves + 8 of 12 swaps) is exactly
  // that shape; if the quantum moves, this fixture fails loudly rather than
  // silently dropping the case.
  constexpr size_t kChunks = 24;
  std::vector<uint16_t> sourceOrder;
  LoopEventStore::enterEphemeralSeal();
  for (size_t i = 0; i < kChunks * LoopEventStoreConfig::CHUNK_CAPACITY; ++i) {
    TEST_ASSERT_TRUE(output.append(MidiEvent::NoteOn(
        static_cast<uint32_t>(i), 1, 60, static_cast<uint32_t>(i + 1))));
  }
  LoopEventStore::leaveEphemeralSeal();
  TEST_ASSERT_EQUAL(kChunks, output.chunkIds().size());
  sourceOrder.assign(output.chunkIds().begin(), output.chunkIds().end());

  // The transfer destination is the prewarmed ownership row; the production
  // transfer sizes it from the prepared output, so this fixture must observe an
  // accepted take far beyond the fixed overdub transfer row.
  TEST_ASSERT_TRUE(t.getLoop(0).prewarmActiveOverdubTransferHorizon());
  TEST_ASSERT_TRUE(t.getLoop(0).hasActiveOverdubTransferAdmission());
  job.admission.phase = PendingStopPhase::RecordPrepared;

  bool sawEmptyPending = false;
  LoopEventStoreRewriteResult result = LoopEventStoreRewriteResult::Pending;
  for (size_t turn = 0; turn < 200 && result == LoopEventStoreRewriteResult::Pending; ++turn) {
    result = TrackCaptureStopTestAccess::passTransferGrain(t);
    if (result == LoopEventStoreRewriteResult::Pending && output.empty()) {
      sawEmptyPending = true;
      break;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(sawEmptyPending,
                           "the fixture must observe an empty store mid-transfer");
  TEST_ASSERT_TRUE(job.ownsPendingPass);
  TEST_ASSERT_TRUE(job.pendingPass.preparationPending);
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPrepared, job.admission.phase);
  TEST_ASSERT_EQUAL_UINT32(kChunks, job.pendingPass.committedChunkIds.size());

  // The owned continuation must finish rather than deny on the emptied store.
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::passTransferGrain));
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPassReady, job.admission.phase);
  TEST_ASSERT_FALSE(job.pendingPass.preparationPending);
  TEST_ASSERT_EQUAL_UINT32(kChunks, job.pendingPass.committedChunkIds.size());
  // Exact directory order: the bounded reversal restores the source order.
  size_t index = 0;
  for (uint16_t id : job.pendingPass.committedChunkIds) {
    TEST_ASSERT_EQUAL_UINT32(sourceOrder[index++], id);
  }
  TEST_ASSERT_EQUAL_UINT32(kChunks, index);
  TEST_ASSERT_FALSE(LoopEventStore::isEphemeralSeal());
}

void test_record_pass_transfer_capacity_refusal_fails_closed() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.startRecording(100);
  t.noteOn(1, 60, 100, 112);
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, 768, 768);
  auto& runtime = TrackCaptureStopTestAccess::closureRuntime(t);
  auto& output = runtime.canonicalCaptureOutput.store;
  // A take beyond the fixed overdub transfer row forces the ownership-row capacity
  // admission before the bounded transfer can start.
  constexpr size_t kChunks = PassConfig::ACTIVE_OVERDUB_TRANSFER_CHUNKS + 2;
  LoopEventStore::enterEphemeralSeal();
  for (size_t i = 0; i < kChunks * LoopEventStoreConfig::CHUNK_CAPACITY; ++i) {
    TEST_ASSERT_TRUE(output.append(MidiEvent::NoteOn(
        static_cast<uint32_t>(i), 1, 60, static_cast<uint32_t>(i + 1))));
  }
  LoopEventStore::leaveEphemeralSeal();
  TEST_ASSERT_TRUE(t.getLoop(0).prewarmActiveOverdubTransferHorizon());
  TEST_ASSERT_TRUE(t.getLoop(0).hasActiveOverdubTransferAdmission());
  job.admission.phase = PendingStopPhase::RecordPrepared;

  // Forced allocation refusal: the admission must report failure. `std::vector`
  // growth cannot report a null allocate, so an unguarded push_back would write
  // through the refused block (reproduced as an ASan null-write).
  LoopEventStore::setCommittedChunkIdProbeRefusedForTests(true);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied,
                    TrackCaptureStopTestAccess::passTransferGrain(t));
  LoopEventStore::setCommittedChunkIdProbeRefusedForTests(false);
  TEST_ASSERT_FALSE_MESSAGE(job.ownsPendingPass,
                            "a refused allocation must not begin the pass transfer");
  TEST_ASSERT_FALSE(job.pendingPass.preparationPending);
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPrepared, job.admission.phase);
  TEST_ASSERT_EQUAL(kChunks * LoopEventStoreConfig::CHUNK_CAPACITY, output.size());

  // The same binding retries once the allocation is available again.
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::passTransferGrain));
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPassReady, job.admission.phase);
  TEST_ASSERT_TRUE(job.ownsPendingPass);
  TEST_ASSERT_EQUAL(kChunks, job.pendingPass.committedChunkIds.size());
}

void test_empty_record_take_all_dropped_retires_without_content() {
    // All rows dropped by the stop projection: a non-note row beyond the final
    // length leaves the closure and the staging output empty.
    Track& t = trackManager.getTrack(0);
    prepareSlot(t, 0, false);
    t.startRecording(100);
    TEST_ASSERT_TRUE(t.getLoop(0).capture.store.append(MidiEvent::ControlChange(900, 1, 7, 1)));
    // An empty take promotes no presentation, so this job owes its own retirement end.
    auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, 1000, 768, /*presentationRetiring=*/false);
    auto& runtime = TrackCaptureStopTestAccess::closureRuntime(t);
    driveToPrepared(t);
    TEST_ASSERT_EQUAL(PendingStopPhase::RecordEmpty, job.admission.phase);
    TEST_ASSERT_EQUAL(0, runtime.canonicalCaptureOutput.store.size());
    TEST_ASSERT_FALSE(job.ownsPendingPass);

    // A live successor on another slot must keep its producer untouched.
    Track& t1 = trackManager.getTrack(1);
    prepareSlot(t1, 0, false);
    t1.setActiveLoopIndex(0);
    t1.startRecording(200);
    TEST_ASSERT_TRUE(t1.getLoop(0).capture.store.append(MidiEvent::NoteOn(212, 1, 70, 100)));
    const size_t successorRows = t1.getLoop(0).capture.store.size();

    const size_t queueBefore = PersistenceWorkQueue::queueDepth();
    TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                      driveGrains(t, TrackCaptureStopTestAccess::emptyGrain));
    TEST_ASSERT_EQUAL(PendingStopPhase::RecordPublished, job.admission.phase);
    TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::hasRecordPass(t, 0));
    TEST_ASSERT_EQUAL(0, TrackUndo::getUndoCount(t));
    TEST_ASSERT_EQUAL(queueBefore, PersistenceWorkQueue::queueDepth());
    TEST_ASSERT_FALSE(StorageManagerInternal::currentSetLoopSlotDirty[0][0]);
    TEST_ASSERT_EQUAL_UINT32(0, t.getLoop(0).loopLengthTicks);
    TEST_ASSERT_EQUAL_UINT32(0, t.getLoop(0).startLoopTick);
    TEST_ASSERT_EQUAL(successorRows, t1.getLoop(0).capture.store.size());
    TEST_ASSERT_EQUAL(CapturePhase::Record, t1.getLoop(0).capture.phase);

    const size_t depthBefore = TrackCaptureStopTestAccess::stopQueueDepth(t);
    TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                      driveGrains(t, TrackCaptureStopTestAccess::retirementGrain));
    TEST_ASSERT_EQUAL(depthBefore - 1, TrackCaptureStopTestAccess::stopQueueDepth(t));
    TEST_ASSERT_EQUAL(successorRows, t1.getLoop(0).capture.store.size());
    TEST_ASSERT_EQUAL(CapturePhase::Record, t1.getLoop(0).capture.phase);
}

void test_empty_record_take_all_filtered_retires_without_content() {
    // All rows filtered away: a pair shorter than the minimum-length policy leaves
    // a non-empty prepared store that the filter compacts to nothing.
    Track& t = trackManager.getTrack(0);
    prepareSlot(t, 0, false);
    t.startRecording(100);
    t.noteOn(1, 60, 100, 112);
    t.noteOff(1, 60, 0, 124);  // 12 ticks: survives the capture-time minimum
    // An empty take promotes no presentation, so this job owes its own retirement end.
    auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, 768, 768, /*presentationRetiring=*/false);
    // The preparation policy now removes the pair, so a non-empty staged take is
    // compacted to nothing rather than being denied.
    noteMinLengthTicks = 48;
    auto& runtime = TrackCaptureStopTestAccess::closureRuntime(t);
    TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                      driveGrains(t, TrackCaptureStopTestAccess::closureGrain));
    TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                      driveGrains(t, TrackCaptureStopTestAccess::stagingGrain));
    TEST_ASSERT_EQUAL_UINT32(2, runtime.canonicalCaptureOutput.store.size());
    TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                      driveGrains(t, TrackCaptureStopTestAccess::normalizationGrain));
    TEST_ASSERT_EQUAL(PendingStopPhase::RecordEmpty, job.admission.phase);
    TEST_ASSERT_EQUAL(0, runtime.canonicalCaptureOutput.store.size());
    TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                      driveGrains(t, TrackCaptureStopTestAccess::emptyGrain));
    TEST_ASSERT_EQUAL(PendingStopPhase::RecordPublished, job.admission.phase);
    TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::hasRecordPass(t, 0));
    TEST_ASSERT_EQUAL(0, TrackUndo::getUndoCount(t));
    TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                      driveGrains(t, TrackCaptureStopTestAccess::retirementGrain));
    TEST_ASSERT_EQUAL(0, TrackCaptureStopTestAccess::stopQueueDepth(t));
}

void test_deferred_record_interval_is_audible_only_with_a_bound_window() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  Loop& loop = t.getLoop(0);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  if (runtime.viewSetIfPrewarmed() == nullptr) {
    TEST_ASSERT_TRUE(runtime.prewarmViews(loop.companionRowHorizonBound(),
                                          loop.fullTakePlaybackViewAdmissible()));
  }
  // The production record start prepares and installs the retained bar-1 window.
  t.startRecording(100);
  t.noteOn(1, 60, 100, 112);
  t.noteOff(1, 60, 0, 180);
  // The stop edge stamps the interval's final geometry; the deferred worker then binds
  // the retained window to it.
  loop.loopLengthTicks = 768;
  {
    auto* set = runtime.viewSetIfPrewarmed();
    TEST_ASSERT_NOT_NULL(set);
    TEST_ASSERT_NOT_EQUAL(RuntimePlaybackViewLifecycle::kNoView, set->lifecycle.retained);
  }
  const PlaybackViewSourceIdentity recordingSource{
      loop.playbackRevision, TrackCaptureStopTestAccess::playbackGenerationOf(t),
      loop.publishedSnapshotIdentity()};
  for (size_t i = 0; i < 32; ++i) {
    runtime.serviceFirstRecordingBar(loop, recordingSource, 768, 1);
  }

  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, 768, 768);
  {
    auto* set = runtime.viewSetIfPrewarmed();
    TEST_ASSERT_NOT_NULL(set);
    TEST_ASSERT_NOT_EQUAL(RuntimePlaybackViewLifecycle::kNoView, set->lifecycle.retained);
    TEST_ASSERT_TRUE(set->views[set->lifecycle.retained].complete);
    TEST_ASSERT_TRUE(set->views[set->lifecycle.retained].rawCoveragePrepared);
  }
  // A pending job whose window is still raw has no audible source: the interval must
  // stay silent rather than fabricate one from nothing.
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::audibleSource(t, 0));
  MidiSendCapture::reset();
  t.playMidiEvents(10, true);
  TEST_ASSERT_EQUAL_UINT32(0, MidiSendCapture::count());
  // The closure grain binds the window to this interval's geometry and frozen source.
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    TrackCaptureStopTestAccess::closureGrain(t));
  {
    auto* set = runtime.viewSetIfPrewarmed();
    TEST_ASSERT_NOT_EQUAL(RuntimePlaybackViewLifecycle::kNoView, set->firstRecordReturnWindowIndex());
  }
  TEST_ASSERT_TRUE(runtime.hasBoundRecordReturnWindowForPendingJob());
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::audibleSource(t, 0));

  // Another slot has no source of its own.
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::audibleSource(t, 1));
  // The layered entry point is for another track's committed bridge. It still never reaches into the fixed view it
  // does not own (`isActive` is target-based on `PlaybackMidiTarget::ActiveSlot`); since **DEC-143** it serves the same
  // merged representation it serves for committed content, and this interval is published read material (the fixture
  // binds the post-publication owner split above). So the expectation is the take's own pair, once each -- an
  // unreadable interval would still emit nothing here, exactly as before the decision.
  MidiSendCapture::reset();
  t.forceSetState(TRACK_PLAYING);
  TrackCaptureStopTestAccess::unmute(t);
  TrackCaptureStopTestAccess::selectSlot(t, 0);
  TrackCaptureStopTestAccess::resetProjectionCycle(t);
  loop.lastTickInLoop = 0;
  loop.nextEventIndex = 0;
  t.playMidiEventsForSlot(0, 90, true);
  {
    size_t layeredOns = 0;
    size_t layeredOffs = 0;
    for (size_t i = 0; i < MidiSendCapture::count(); ++i) {
      const MidiEvent& event = MidiSendCapture::at(i);
      TEST_ASSERT_TRUE(event.isNoteOn() || event.isNoteOff());
      TEST_ASSERT_EQUAL_UINT8(60, event.data.noteData.note);
      if (event.isNoteOn()) ++layeredOns;
      if (event.isNoteOff()) ++layeredOffs;
    }
    TEST_ASSERT_EQUAL_UINT32(1, layeredOns);
    TEST_ASSERT_EQUAL_UINT32(1, layeredOffs);
  }

  // Emission through the production active-slot entry point: the unpublished interval
  // must actually reach the wire from its bound window, once per event.
  MidiSendCapture::reset();
  t.forceSetState(TRACK_PLAYING);
  TrackCaptureStopTestAccess::unmute(t);
  TrackCaptureStopTestAccess::selectSlot(t, 0);
  TrackCaptureStopTestAccess::resetProjectionCycle(t);
  loop.lastTickInLoop = 0;
  loop.nextEventIndex = 0;
  t.playMidiEvents(90, true);
  size_t ons = 0;
  size_t offs = 0;
  char trace[512];
  size_t written = 0;
  for (size_t i = 0; i < MidiSendCapture::count() && written + 24 < sizeof(trace); ++i) {
    const MidiEvent& event = MidiSendCapture::at(i);
    written += static_cast<size_t>(snprintf(trace + written, sizeof(trace) - written,
                                            "[%u %s %u]", event.tick,
                                            event.isNoteOn() ? "on" : "off",
                                            event.data.noteData.note));
    if (event.isNoteOn()) ++ons;
    if (event.isNoteOff()) ++offs;
  }
  trace[written] = '\0';
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, ons, trace);
  TEST_ASSERT_EQUAL_UINT32(1, offs);

  // S4f.19 successor start: a successor record on another slot must not disturb the
  // predecessor's ownership. The contract's requirement here is that B's start freezes the
  // job-owned source rather than invalidating it: the frozen range stays sealed and owned,
  // the servable view survives, and retirement still waits. Audibility is deliberately not
  // asserted: playback emission is suppressed while the track is RECORDING by design
  // (`isAudiblePlaybackActive` excludes it), so only ownership must survive the start.
  prepareSlot(t, 1, false);
  TrackCaptureStopTestAccess::selectSlot(t, 1);
  t.startRecording(400);
  t.noteOn(1, 70, 100, 412);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.ready());
  TEST_ASSERT_EQUAL(static_cast<int>(CapturePhase::Record), static_cast<int>(job.mode));
  TEST_ASSERT_TRUE(runtime.hasServableDeferredRecordView());
  TEST_ASSERT_FALSE(runtime.frozenWindowBuildActive());
  const size_t depthUnderSuccessor = TrackCaptureStopTestAccess::stopQueueDepth(t);
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(depthUnderSuccessor));

  // A stale/unready interval emits nothing. Task 3.15g re-keyed the reader obligation onto the
  // slot-owned presentation record, so ending the interval means releasing that ownership -- the
  // interaction job's own `pending` flag no longer speaks for the frozen reader.
  TrackCaptureStopTestAccess::releasePresentationOwnership(t, 0);
  job.admission.pending = false; // This test also ends its deliberately pre-publication finalizer.
  MidiSendCapture::reset();
  t.playMidiEvents(110, true);
  t.playMidiEventsForSlot(0, 110, true);
  t.playMidiEventsForSlot(1, 110, true);
  TEST_ASSERT_EQUAL_UINT32(0, MidiSendCapture::count());
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::audibleSource(t, 0));
}

// The native send boundary must record a send before any playback assertion relies on it.
void test_native_midi_send_capture_records_a_direct_send() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_PLAYING);
  TrackCaptureStopTestAccess::unmute(t);
  TrackCaptureStopTestAccess::armEmit(t);
  MidiSendCapture::reset();
  TrackCaptureStopTestAccess::directSend(t, MidiEvent::NoteOn(0, 1, 60, 100), 0);
  TEST_ASSERT_EQUAL_UINT32(1, MidiSendCapture::count());
  TEST_ASSERT_TRUE(MidiSendCapture::at(0).isNoteOn());
  MidiSendCapture::reset();
  TEST_ASSERT_EQUAL_UINT32(0, MidiSendCapture::count());
}

// S4f.19 replacement handoff: once the interval's own pass is published, emitting must
// neither repeat the already-served window events nor omit the committed boundary.
static void assertExactlyOnePlaybackNotePair(uint8_t note, uint32_t onTick, uint32_t offTick) {
  TEST_ASSERT_EQUAL_UINT32(2, MidiSendCapture::count());
  const MidiEvent& on = MidiSendCapture::at(0);
  const MidiEvent& off = MidiSendCapture::at(1);
  TEST_ASSERT_TRUE(on.isNoteOn());
  TEST_ASSERT_EQUAL_UINT8(note, on.data.noteData.note);
  TEST_ASSERT_EQUAL_UINT32(onTick, on.tick);
  TEST_ASSERT_TRUE(off.isNoteOff());
  TEST_ASSERT_EQUAL_UINT8(note, off.data.noteData.note);
  TEST_ASSERT_EQUAL_UINT32(offTick, off.tick);
}

void test_published_record_interval_does_not_replay_the_return_window() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  Loop& loop = t.getLoop(0);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  if (runtime.viewSetIfPrewarmed() == nullptr) {
    TEST_ASSERT_TRUE(runtime.prewarmViews(loop.companionRowHorizonBound(),
                                          loop.fullTakePlaybackViewAdmissible()));
  }
  t.startRecording(100);
  t.noteOn(1, 60, 100, 112);
  t.noteOff(1, 60, 0, 180);
  loop.loopLengthTicks = 768;
  const PlaybackViewSourceIdentity recordingSource{
      loop.playbackRevision, TrackCaptureStopTestAccess::playbackGenerationOf(t),
      loop.publishedSnapshotIdentity()};
  for (size_t i = 0; i < 32; ++i) {
    runtime.serviceFirstRecordingBar(loop, recordingSource, 768, 1);
  }
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, 768, 768);
  // Task 3.15g: publication pops the interaction job and nulls its `presentation` back-pointer,
  // so the sealed extent this fixture keeps driving is reached through the slot-owned record.
  auto* presentation = job.presentation;
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    TrackCaptureStopTestAccess::closureGrain(t));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::stagingGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::normalizationGrain));
  TEST_ASSERT_TRUE(loop.prewarmActiveOverdubTransferHorizon());
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::passTransferGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::publicationGrain));
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPublished, TrackCaptureStopTestAccess::phase(t));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::hasRecordPass(t, 0));
  {
    auto* set = runtime.viewSetIfPrewarmed();
    TEST_ASSERT_NOT_NULL(set);
    TEST_ASSERT_NOT_EQUAL(RuntimePlaybackViewLifecycle::kNoView, set->firstRecordReturnWindowIndex());
    TEST_ASSERT_TRUE(runtime.hasBoundRecordReturnWindowForPendingJob());
  }

  // The bound window survives publication and is still adoptable: no matching
  // replacement *view* has been handed off yet, so the contract keeps serving it.
  // Stamping its source here makes the drive below deterministic about which source is
  // served (the production adopt runs inside the entry point and re-seeds the cursor to
  // skip the pre-stop phase, which is the "already heard live" rule).
  {
    const PlaybackViewSourceIdentity expected{
        loop.playbackRevision, TrackCaptureStopTestAccess::playbackGenerationOf(t),
        loop.publishedSnapshotIdentity()};
    TEST_ASSERT_NOT_NULL(
        runtime.adoptFirstRecordingBarReturnWindow(loop, expected, 768, 90));
  }

  // Emit after publication: the committed pass is the boundary now.
  MidiSendCapture::reset();
  t.forceSetState(TRACK_PLAYING);
  TrackCaptureStopTestAccess::unmute(t);
  TrackCaptureStopTestAccess::selectSlot(t, 0);
  TrackCaptureStopTestAccess::resetProjectionCycle(t);
  loop.lastTickInLoop = 0;
  loop.nextEventIndex = 0;
  t.playMidiEvents(90, true);
  assertExactlyOnePlaybackNotePair(60, 12, 80);

  // S4f.19 continuity gate: retirement waits for an in-flight frozen build to drain,
  // because that build still borrows the raw range, and then drops the range-derived
  // window before the first release grain.
  const PlaybackViewSourceIdentity expected{
      loop.playbackRevision, TrackCaptureStopTestAccess::playbackGenerationOf(t),
      loop.publishedSnapshotIdentity()};
  TEST_ASSERT_TRUE(runtime.serviceFrozenRecordWindow(presentation->rawRange, presentation->rawRange.generation(),
                                                     expected, 768, 400, 768, 1));
  TEST_ASSERT_TRUE(runtime.frozenWindowBuildActive());
  const size_t depthBeforeGate = TrackCaptureStopTestAccess::stopQueueDepth(t);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Pending,
                    TrackCaptureStopTestAccess::retirementGrain(t));
  TEST_ASSERT_EQUAL(depthBeforeGate, TrackCaptureStopTestAccess::stopQueueDepth(t));
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPublished, TrackCaptureStopTestAccess::phase(t));
  // Drain the build; retirement then completes and the job leaves the ring.
  for (size_t turn = 0; turn < 4096 && runtime.frozenWindowBuildActive(); ++turn) {
    (void)runtime.serviceFrozenRecordWindow(presentation->rawRange, presentation->rawRange.generation(), expected,
                                            768, 400, 768, 1);
  }
  TEST_ASSERT_FALSE(runtime.frozenWindowBuildActive());
  while (loop.hasPendingSnapshotPublish()) {
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
  }
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  t.playMidiEvents(90, false); // hand the complete serving return window to the current root identity
  PlaybackViewSourceIdentity rootIdentity{};
  PlaybackCoverageInterval rootCoverage{};
  TEST_ASSERT_TRUE(t.publishedPlaybackWindowForSlot(0, rootIdentity, rootCoverage));
  TEST_ASSERT_EQUAL_PTR(loop.publishedSnapshotIdentity(), rootIdentity.snapshotIdentity);
  const LoopEventStoreRewriteResult retired =
      driveGrains(t, TrackCaptureStopTestAccess::retirementGrain);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, retired);
  TEST_ASSERT_EQUAL(0, TrackCaptureStopTestAccess::stopQueueDepth(t));
}
// S7.5b.4: a committed Record pass is not an audible-source handoff while its session-exit
// LoopSnapshot is still stale. Retirement must preserve the job-owned range so the existing
// onward-window path can keep serving later bars until the current root can take over.
void test_record_retirement_keeps_frozen_source_until_snapshot_is_current() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  Loop& loop = t.getLoop(0);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  if (runtime.viewSetIfPrewarmed() == nullptr) {
    TEST_ASSERT_TRUE(runtime.prewarmViews(loop.companionRowHorizonBound(),
                                          loop.fullTakePlaybackViewAdmissible()));
  }

  t.startRecording(100);
  t.noteOn(1, 60, 100, 112);   // bar 1: relative tick 12
  t.noteOff(1, 60, 0, 180);    // bar 1: relative tick 80
  t.noteOn(1, 62, 100, 900);   // bar 2: relative tick 800
  t.noteOff(1, 62, 0, 980);    // bar 2: relative tick 880
  loop.loopLengthTicks = Config::TICKS_PER_BAR * 2;
  const PlaybackViewSourceIdentity recordingSource{
      loop.playbackRevision, TrackCaptureStopTestAccess::playbackGenerationOf(t),
      loop.publishedSnapshotIdentity()};
  for (size_t grain = 0; grain < 64; ++grain) {
    runtime.serviceFirstRecordingBar(loop, recordingSource, Config::TICKS_PER_BAR, 1);
  }

  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(
      t, 0, loop.loopLengthTicks, loop.loopLengthTicks);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    TrackCaptureStopTestAccess::closureGrain(t));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::stagingGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::normalizationGrain));
  TEST_ASSERT_TRUE(loop.prewarmActiveOverdubTransferHorizon());
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::passTransferGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::publicationGrain));
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPublished, TrackCaptureStopTestAccess::phase(t));
  TEST_ASSERT_TRUE(loop.passes.hasRecordPass());
  TEST_ASSERT_TRUE(loop.pendingSnapshotSessionExitQueued());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot));
  TEST_ASSERT_TRUE(runtime.hasBoundRecordReturnWindowForPendingJob());
  TEST_ASSERT_FALSE(runtime.frozenWindowBuildActive());

  const PlaybackViewSourceIdentity expected{
      loop.playbackRevision, TrackCaptureStopTestAccess::playbackGenerationOf(t),
      loop.publishedSnapshotIdentity()};
  TEST_ASSERT_NOT_NULL(runtime.adoptFirstRecordingBarReturnWindow(
      loop, expected, loop.loopLengthTicks, 90));

  // The old retirement predicate completes here: no build happens to be active, although
  // the published root is still stale and the first-bar window is the only valid source.
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Pending,
                    TrackCaptureStopTestAccess::retirementGrain(t));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::presentationActive(t, 0));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot));
  TEST_ASSERT_TRUE(runtime.hasBoundRecordReturnWindowForPendingJob());

  t.forceSetState(TRACK_PLAYING);
  TrackCaptureStopTestAccess::unmute(t);
  TrackCaptureStopTestAccess::selectSlot(t, 0);
  TrackCaptureStopTestAccess::resetProjectionCycle(t);
  loop.lastTickInLoop = 0;
  loop.nextEventIndex = 0;
  MidiSendCapture::reset();
  t.playMidiEvents(90, true);
  assertExactlyOnePlaybackNotePair(60, 12, 80);

  // Keep snapshot publication deliberately pending while the production Track entry
  // advances the onward window from the frozen CaptureRange and emits bar 2 exactly once.
  for (size_t turn = 0; turn < 512 && !runtime.frozenRecordWindowReady(); ++turn) {
    (void)t.stepPlaybackViewMaintenance(Config::TICKS_PER_BAR - 1, nullptr, true, {});
  }
  TEST_ASSERT_TRUE_MESSAGE(runtime.frozenRecordWindowReady(),
                           "maintenance must prepare the bar-2 successor before the clock edge");
  MidiSendCapture::reset();
  TrackCaptureStopTestAccess::resetProjectionCycle(t);
  loop.lastTickInLoop = Config::TICKS_PER_BAR;
  loop.nextEventIndex = 0;
  t.playMidiEvents(940, true);
  assertExactlyOnePlaybackNotePair(62, 800, 880);
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::presentationActive(t, 0));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Pending,
                    TrackCaptureStopTestAccess::retirementGrain(t));

  // The real session-exit publisher completes while transport remains in PLAYING.
  size_t snapshotGrains = 0;
  while (loop.hasPendingSnapshotPublish() && snapshotGrains < 20000) {
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
    ++snapshotGrains;
  }
  TEST_ASSERT_FALSE_MESSAGE(loop.hasPendingSnapshotPublish(),
                            "the committed source must finish publishing");
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  const PlaybackViewSourceIdentity currentRootIdentity{
      loop.playbackRevision, TrackCaptureStopTestAccess::playbackGenerationOf(t),
      loop.publishedSnapshotIdentity()};
  const auto rootReady = [&]() {
    const RuntimePlaybackViewSet* set = runtime.viewSetIfPrewarmed();
    if (set == nullptr) return false;
    if (loop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks)) {
      if (set->lifecycle.ready >= set->views.size()) return false;
      const RuntimePlaybackView& view = set->views[set->lifecycle.ready];
      return view.complete && view.source == currentRootIdentity &&
          view.coverage.startPhase == Config::TICKS_PER_BAR &&
          view.coverage.lengthTicks == Config::TICKS_PER_BAR;
    }
    if (set->lifecycle.current >= set->views.size()) return false;
    const RuntimePlaybackView& served = set->views[set->lifecycle.current];
    return served.complete &&
        served.source.playbackRevision == currentRootIdentity.playbackRevision &&
        served.source.sourceGeneration == currentRootIdentity.sourceGeneration &&
        served.coverage.contains(940);
  };
  const auto completionClock = []() { return PlaybackDeadlineClock{940, 1}; };
  size_t rootBuildTurns = 0;
  Track::PlaybackViewTurnResult lastRootTurn = Track::PlaybackViewTurnResult::Idle;
  for (; rootBuildTurns < 4096 && !rootReady(); ++rootBuildTurns) {
    lastRootTurn = t.stepPlaybackViewMaintenance(940, nullptr, true, {940, 1}, completionClock);
  }
  char rootBuildDetail[320];
  const RuntimePlaybackViewSet* rootBuildSet = runtime.viewSetIfPrewarmed();
  const PlaybackViewPendingRequest* rootPending = t.pendingViewRequest(0);
  const PlaybackViewPendingRequest* rawPending = runtime.pendingViewRequest();
  snprintf(rootBuildDetail, sizeof(rootBuildDetail),
      "root successor: turns=%zu result=%u cur=%u ready=%u build=%u active=%d cov=%d pending=%d raw=%d op=%u/gen=%u rev=%u/%u reason=%u",
      rootBuildTurns, static_cast<unsigned>(lastRootTurn), rootBuildSet ? rootBuildSet->lifecycle.current : 255,
      rootBuildSet ? rootBuildSet->lifecycle.ready : 255, rootBuildSet ? rootBuildSet->lifecycle.build : 255,
      runtime.viewBuild.active() ? 1 : 0, runtime.coverageBuildNeedsService() ? 1 : 0,
      rootPending && rootPending->active ? 1 : 0, rawPending ? 1 : 0,
      rawPending ? static_cast<unsigned>(rawPending->operationGeneration) : 0,
      static_cast<unsigned>(TrackCaptureStopTestAccess::playbackGenerationOf(t)),
      rawPending ? static_cast<unsigned>(rawPending->source.playbackRevision) : 0,
      static_cast<unsigned>(loop.playbackRevision),
      rawPending ? static_cast<unsigned>(rawPending->reason) : 0u);
  TEST_ASSERT_TRUE_MESSAGE(rootReady(), rootBuildDetail);
  TrackCaptureStopTestAccess::resetProjectionCycle(t);
  t.playMidiEvents(940, false); // atomically adopt the current-root successor
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::retirementGrain));
  TEST_ASSERT_EQUAL_UINT32(0, TrackCaptureStopTestAccess::stopQueueDepth(t));
  TEST_ASSERT_FALSE(runtime.hasBoundRecordReturnWindowForPendingJob());
  PlaybackViewSourceIdentity afterRetirement{};
  PlaybackCoverageInterval afterRetirementCoverage{};
  TEST_ASSERT_TRUE(t.publishedPlaybackWindowForSlot(0, afterRetirement,
                                                    afterRetirementCoverage));
  TEST_ASSERT_EQUAL_PTR(loop.publishedSnapshotIdentity(), afterRetirement.snapshotIdentity);
}


// S4f.19: the retirement wait is bounded (DEC-070's shape for a predicate that can no longer
// become true). The frozen-window build has two writers and both sit on other owners: the playback
// hot path (`adoptOrServiceOnwardRecordWindow` -> `serviceFrozenRecordWindow`) and the maintenance
// owner's `serviceFirstRecordingBar`, which is gated on `TRACK_RECORDING`. A job whose interval has
// left that state therefore holds a build no writer can advance. The device parked exactly that
// way: `#STOP_STATUS,state=OVERDUBBING,pending=1,phase=16` with `build_active=1` on every poll for
// 30 s, and every later stop on the track refused (`Overdub stop refused: stop_job_pending`).
//
// The gate itself is not weakened -- `test_published_record_interval_does_not_replay_the_return_window`
// above drives the same state to completion and asserts that retirement yields while a build is
// being fed. This fixture is the case where nothing feeds it.
void test_stalled_frozen_window_build_does_not_park_the_record_job() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  Loop& loop = t.getLoop(0);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  if (runtime.viewSetIfPrewarmed() == nullptr) {
    TEST_ASSERT_TRUE(runtime.prewarmViews(loop.companionRowHorizonBound(),
                                          loop.fullTakePlaybackViewAdmissible()));
  }
  t.startRecording(100);
  t.noteOn(1, 60, 100, 112);
  t.noteOff(1, 60, 0, 180);
  loop.loopLengthTicks = 768;
  const PlaybackViewSourceIdentity recordingSource{
      loop.playbackRevision, TrackCaptureStopTestAccess::playbackGenerationOf(t),
      loop.publishedSnapshotIdentity()};
  for (size_t i = 0; i < 32; ++i) {
    runtime.serviceFirstRecordingBar(loop, recordingSource, 768, 1);
  }
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, 768, 768);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    TrackCaptureStopTestAccess::closureGrain(t));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::stagingGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::normalizationGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::passTransferGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::publicationGrain));
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPublished, TrackCaptureStopTestAccess::phase(t));

  // The device's state when it wedged: the interval is audible, but this is not the state the
  // record-window writer is gated on (`isRecording()` is `TRACK_RECORDING` exactly).
  t.forceSetState(TRACK_OVERDUBBING);

  // An in-flight build that nothing will advance.
  const PlaybackViewSourceIdentity expected{
      loop.playbackRevision, TrackCaptureStopTestAccess::playbackGenerationOf(t),
      loop.publishedSnapshotIdentity()};
  TEST_ASSERT_TRUE(runtime.serviceFrozenRecordWindow(TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange, TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.generation(),
                                                    expected, 768, 400, 768, 1));
  TEST_ASSERT_TRUE(runtime.frozenWindowBuildActive());

  // The gate is not weakened: retirement still yields to a live build ...
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Pending,
                    TrackCaptureStopTestAccess::retirementGrain(t));
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPublished, TrackCaptureStopTestAccess::phase(t));

  // ... but a build no writer can advance must not park the job, its raw range and its
  // capture-stop owner forever. The bound is deliberately asserted as *boundedness* (generously
  // above the source's grain budget), not as the exact constant.
  const size_t depthBefore = TrackCaptureStopTestAccess::stopQueueDepth(t);
  // Task 3.15g: publication already popped the interaction job; the obligation this bound governs
  // is the slot-owned presentation record's.
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::presentationActive(t, job.slot));
  t.forceSetState(TRACK_STOPPED); // No boundary can come: the bounded stopped-retirement contract.
  size_t turns = 0;
  LoopEventStoreRewriteResult retired = LoopEventStoreRewriteResult::Pending;
  for (; turns < 1024; ++turns) {
    if (loop.pendingSnapshotSessionExitQueued() && !loop.publishedSnapshotIsCurrent()) {
      t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
      continue;
    }
    retired = TrackCaptureStopTestAccess::retirementGrain(t);
    if (retired == LoopEventStoreRewriteResult::Complete) break;
  }
  TEST_ASSERT_EQUAL_MESSAGE(LoopEventStoreRewriteResult::Complete, retired,
                            "a frozen window build nothing advances parked the record job");
  TEST_ASSERT_LESS_THAN_UINT32(1024, turns);
  // The stale build went before the raw range: the gate's own protection, kept.
  TEST_ASSERT_FALSE(runtime.frozenWindowBuildActive());
  TEST_ASSERT_EQUAL(0, TrackCaptureStopTestAccess::stopQueueDepth(t));
}

// S4f.19 traversal: past the bound bar-1 window the unpublished interval is served from
// a window built over the job's own frozen source, so bar two stays audible while the
// pass is still finalizing.
void test_deferred_record_interval_serves_beyond_bar_one_from_the_frozen_source() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  Loop& loop = t.getLoop(0);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  if (runtime.viewSetIfPrewarmed() == nullptr) {
    TEST_ASSERT_TRUE(runtime.prewarmViews(loop.companionRowHorizonBound(),
                                          loop.fullTakePlaybackViewAdmissible()));
  }
  t.startRecording(100);
  t.noteOn(1, 60, 100, 112);   // relative tick 12  (bar 1)
  t.noteOff(1, 60, 0, 180);    // relative tick 80
  t.noteOn(1, 62, 100, 900);   // relative tick 800 (bar 2)
  t.noteOff(1, 62, 0, 1000);   // relative tick 900
  loop.loopLengthTicks = 1536;
  const PlaybackViewSourceIdentity recordingSource{
      loop.playbackRevision, TrackCaptureStopTestAccess::playbackGenerationOf(t),
      loop.publishedSnapshotIdentity()};
  for (size_t i = 0; i < 64; ++i) {
    runtime.serviceFirstRecordingBar(loop, recordingSource, 768, 1);
  }
  runtime.reset(true); // production resetPlaybackState preserves the raw bar and its currency
  // Production stop-edge ordering: bind the complete first bar before the
  // deferred closure grain runs. This moves retained -> current and clears raw.
  TEST_ASSERT_TRUE(runtime.bindFirstRecordingBar(loop.loopLengthTicks, &loop));
  auto* returnSet = runtime.viewSetIfPrewarmed();
  TEST_ASSERT_NOT_NULL(returnSet);
  TEST_ASSERT_EQUAL(RuntimePlaybackViewLifecycle::kNoView, returnSet->lifecycle.retained);
  const uint8_t returnIndex = returnSet->firstRecordReturnWindowIndex();
  TEST_ASSERT_EQUAL_UINT8(returnSet->lifecycle.current, returnIndex);
  const auto returnGeneration = returnSet->views[returnIndex].storageGeneration;
  const size_t returnEvents = returnSet->views[returnIndex].storage.eventCount;
  TEST_ASSERT_EQUAL_UINT32(2, returnEvents);
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, 1536, 1536);
  TEST_ASSERT_FALSE(runtime.bindDeferredRecordReturnWindow(
      TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange, TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.generation() + 1, loop.loopLengthTicks));
  TEST_ASSERT_FALSE(runtime.bindDeferredRecordReturnWindow(
      TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange, TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.generation(), loop.loopLengthTicks + 1));
  TEST_ASSERT_EQUAL_UINT32(returnGeneration, returnSet->views[returnIndex].storageGeneration);
  TEST_ASSERT_EQUAL_UINT32(returnEvents, returnSet->views[returnIndex].storage.eventCount);
  // The deferred owner must acknowledge the already-bound immutable bar, not
  // look for the raw view in a retained role the stop edge just released.
  TEST_ASSERT_TRUE_MESSAGE(
      runtime.bindDeferredRecordReturnWindow(TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange, TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.generation(),
                                             loop.loopLengthTicks),
      "deferred bind must recognize the stop-edge current return view");
  TEST_ASSERT_EQUAL_UINT32(returnGeneration, returnSet->views[returnIndex].storageGeneration);
  TEST_ASSERT_EQUAL_UINT32(returnEvents, returnSet->views[returnIndex].storage.eventCount);
  TEST_ASSERT_FALSE(returnSet->views[returnIndex].rawCoveragePrepared);
  uint32_t frozenBarTwoOn = UINT32_MAX;
  uint32_t frozenBarTwoOff = UINT32_MAX;
  for (size_t chunk = 0; chunk < TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.chunkCount(); ++chunk) {
    for (uint16_t index = 0;; ++index) {
      MidiEvent event;
      bool atEnd = false;
      TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.read(TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.generation(), chunk, index, event, atEnd));
      if (atEnd) break;
      if (event.data.noteData.note != 62) continue;
      if (event.isNoteOn()) frozenBarTwoOn = event.tick;
      if (event.isNoteOff()) frozenBarTwoOff = event.tick;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(800, frozenBarTwoOn);
  TEST_ASSERT_EQUAL_UINT32(900, frozenBarTwoOff);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    TrackCaptureStopTestAccess::closureGrain(t));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::audibleSource(t, 0));

  // The bar-1 window still serves the first bar.
  MidiSendCapture::reset();
  t.forceSetState(TRACK_PLAYING);
  TrackCaptureStopTestAccess::unmute(t);
  TrackCaptureStopTestAccess::selectSlot(t, 0);
  TrackCaptureStopTestAccess::resetProjectionCycle(t);
  loop.lastTickInLoop = 0;
  loop.nextEventIndex = 0;
  t.playMidiEvents(90, true);
  size_t barOneOns = 0;
  for (size_t i = 0; i < MidiSendCapture::count(); ++i) {
    const MidiEvent& event = MidiSendCapture::at(i);
    if (event.isNoteOn() && event.data.noteData.note == 60) ++barOneOns;
  }
  TEST_ASSERT_EQUAL_UINT32(1, barOneOns);

  // Past bar one the bar-1 window no longer covers the phase; the onward window is built
  // from the job's frozen source and serves the bar-2 note exactly once.
  // S7.5b.5 red-first oracle: while the first immutable bar is served, the real
  // holder-selection/maintenance entry must prepare bar 2 before playback reaches it.
  // No direct frozen-source builder call; the Snapshot owner remains pending until the ready window is checked.
  // D10 / task 3.9: this branch returns **before** the coverage grain tail that emits the lane's rows, so its own
  // completion owes them here -- and the recorders are cleared so the rows below are this loop's, not a sibling's.
  PlaybackPathRows::reset();
  WindowReadinessRows::reset();
  for (size_t turn = 0; turn < 512 && !runtime.frozenRecordWindowReady(); ++turn) {
    const auto ownerResult = t.stepPlaybackViewMaintenance(
        Config::TICKS_PER_BAR - 1, nullptr, true, {});
    if (ownerResult == Track::PlaybackViewTurnResult::Idle) continue;
  }
  TEST_ASSERT_TRUE_MESSAGE(runtime.frozenRecordWindowReady(),
                           "the selected maintenance owner must ready bar 2 before its boundary");
  TEST_ASSERT_TRUE_MESSAGE(PlaybackPathRows::countOf("build_complete") >= 1,
      "a frozen Record-return completion must be named by the lane's own build_complete row");
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot));
  TEST_ASSERT_EQUAL_UINT8(Config::TICKS_PER_BAR, runtime.viewSetIfPrewarmed()->views[
      runtime.viewSetIfPrewarmed()->lifecycle.ready].coverage.startPhase);
  const RuntimePlaybackView& preparedBarTwo = runtime.viewSetIfPrewarmed()->views[
      runtime.viewSetIfPrewarmed()->lifecycle.ready];
  uint32_t preparedOffTick = UINT32_MAX;
  for (size_t index = 0; index < preparedBarTwo.storage.eventCount; ++index) {
    const MidiEvent& event = preparedBarTwo.storage.events[index];
    if (event.isNoteOff() && event.data.noteData.note == 62) preparedOffTick = event.tick;
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(frozenBarTwoOff, preparedOffTick,
                                   "the immutable successor must match its frozen CaptureRange rows");

  // The prepared successor is consumed at the actual boundary; the clock edge
  // performs adoption/emission only and sees the exact bar-2 note pair.
  MidiSendCapture::reset();
  TrackCaptureStopTestAccess::resetProjectionCycle(t);
  loop.lastTickInLoop = 0;
  loop.nextEventIndex = 0;
  t.playMidiEvents(940, true);
  assertExactlyOnePlaybackNotePair(62, frozenBarTwoOn, frozenBarTwoOff);
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::hasRecordPass(t, 0));

  // Wrap while finalizing: the playhead returns to bar one, which the bound bar-1 window
  // still covers, so the interval stays audible across the wrap.
  for (size_t turn = 0; turn < 512 && !runtime.frozenRecordWindowReady(); ++turn) {
    (void)t.stepPlaybackViewMaintenance(loop.loopLengthTicks - 1, nullptr, true, {});
  }
  TEST_ASSERT_TRUE_MESSAGE(runtime.frozenRecordWindowReady(),
                           "the frozen-source owner prepares bar 1 before the wrap boundary");
  MidiSendCapture::reset();
  TEST_ASSERT_TRUE_MESSAGE(PlaybackPathRows::countOf("build_complete") >= 2,
      "the wrapped frozen Record-return successor must name its own completion too");
  for (size_t turn = 0; turn < 128 && MidiSendCapture::count() == 0; ++turn) {
    TrackCaptureStopTestAccess::resetProjectionCycle(t);
    loop.lastTickInLoop = 1500;
    loop.nextEventIndex = 0;
    t.playMidiEvents(1600, true);
  }
  size_t wrapOns = 0;
  char wrapTrace[256];
  size_t wrapWritten = 0;
  for (size_t i = 0; i < MidiSendCapture::count() && wrapWritten + 24 < sizeof(wrapTrace); ++i) {
    const MidiEvent& event = MidiSendCapture::at(i);
    wrapWritten += static_cast<size_t>(snprintf(wrapTrace + wrapWritten,
                                                sizeof(wrapTrace) - wrapWritten, "[%u %s %u]",
                                                event.tick, event.isNoteOn() ? "on" : "off",
                                                event.data.noteData.note));
    if (event.isNoteOn() && event.data.noteData.note == 60) ++wrapOns;
  }
  wrapTrace[wrapWritten] = '\0';
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, wrapOns, wrapTrace);
}

// D10 / task 3.9: the frozen Record-return owner's **denial** terminal must report the slot's readiness receipt. That
// row is the one the device needs when the return window cannot be built at all: it names the slot, its readiness and
// the holder that owes the work, where the lane previously said nothing (the branch returns before the coverage grain
// tail that emits the family). The forced state is the runtime's own `ownsLaunchPreparation()` guard -- a launch
// preparation owning the runtime, which the production guard refuses the frozen service under -- so the case adds no
// state, copies no state machine and changes no result: every owner turn reaches the denial terminal and nothing is
// published. The receipt's row is a **cadence-bounded sample** (one per declared interval), so the case drives more
// than one interval's worth of refusals; that is what makes the row assertion phase-independent rather than a coin
// flip on the process-wide cadence counter.
void test_frozen_record_return_terminals_report_the_slots_readiness_receipt() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  Loop& loop = t.getLoop(0);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  if (runtime.viewSetIfPrewarmed() == nullptr) {
    TEST_ASSERT_TRUE(runtime.prewarmViews(loop.companionRowHorizonBound(),
                                          loop.fullTakePlaybackViewAdmissible()));
  }
  t.startRecording(100);
  t.noteOn(1, 60, 100, 112);
  t.noteOff(1, 60, 0, 180);
  loop.loopLengthTicks = 768;
  const PlaybackViewSourceIdentity recordingSource{
      loop.playbackRevision, TrackCaptureStopTestAccess::playbackGenerationOf(t),
      loop.publishedSnapshotIdentity()};
  for (size_t i = 0; i < 64; ++i) {
    runtime.serviceFirstRecordingBar(loop, recordingSource, 768, 1);
  }
  runtime.reset(true);
  TEST_ASSERT_TRUE(runtime.bindFirstRecordingBar(loop.loopLengthTicks, &loop));
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, 768, 768);
  TEST_ASSERT_TRUE_MESSAGE(
      runtime.bindDeferredRecordReturnWindow(TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange, TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.generation(), loop.loopLengthTicks),
      "the deferred bind must recognize the stop-edge current return view");
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    TrackCaptureStopTestAccess::closureGrain(t));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::audibleSource(t, 0));
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  t.forceSetState(TRACK_PLAYING);
  TrackCaptureStopTestAccess::unmute(t);
  TrackCaptureStopTestAccess::selectSlot(t, 0);
  TrackCaptureStopTestAccess::resetProjectionCycle(t);

  // The runtime's launch preparation owns the slot: the frozen service refuses on every turn from here.
  runtime.launchPreparation = LoopPlaybackRuntime::LaunchPreparation::Building;
  TEST_ASSERT_TRUE(runtime.ownsLaunchPreparation());
  TEST_ASSERT_FALSE(runtime.frozenRecordWindowReady());
  MidiSendCapture::reset();
  WindowReadinessRows::reset();
  PlaybackPathRows::reset();
  unsigned deniedTurns = 0;
  for (unsigned turn = 0; turn < 24; ++turn) {
    const auto ownerResult =
        t.stepPlaybackViewMaintenance(Config::TICKS_PER_BAR - 1, nullptr, true, {});
    if (ownerResult == Track::PlaybackViewTurnResult::GrainDenied) ++deniedTurns;
  }
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(
      16u, deniedTurns, "the frozen owner must keep naming its refusal while the launch preparation owns the slot");
  TEST_ASSERT_TRUE_MESSAGE(WindowReadinessRows::countForSlot(0) >= 1,
      "the frozen Record-return owner's denial must report slot 0's readiness receipt");
  TEST_ASSERT_FALSE_MESSAGE(runtime.frozenRecordWindowReady(),
                            "a refused frozen service must not publish a window");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, MidiSendCapture::count(), "a refusal emits no MIDI");
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot) && TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.ready());
}

void test_late_first_bar_append_refuses_stale_stop_view_and_recovers_from_frozen_range() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  Loop& loop = t.getLoop(0);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  if (runtime.viewSetIfPrewarmed() == nullptr) {
    TEST_ASSERT_TRUE(runtime.prewarmViews(loop.companionRowHorizonBound(),
                                          loop.fullTakePlaybackViewAdmissible()));
  }
  t.startRecording(100);
  t.noteOn(1, 60, 100, 112);
  t.noteOff(1, 60, 0, 180);
  loop.loopLengthTicks = 2 * Config::TICKS_PER_BAR;
  const PlaybackViewSourceIdentity source{
      loop.playbackRevision, TrackCaptureStopTestAccess::playbackGenerationOf(t),
      loop.publishedSnapshotIdentity()};
  for (unsigned i = 0; i < 64; ++i)
    runtime.serviceFirstRecordingBar(loop, source, Config::TICKS_PER_BAR, 1);
  auto* set = runtime.viewSetIfPrewarmed();
  TEST_ASSERT_TRUE(set->lifecycle.retained < set->views.size());
  TEST_ASSERT_EQUAL_UINT32(2, set->views[set->lifecycle.retained].storage.eventCount);
  // This accepted note arrives after the last idle build. A complete view of
  // the earlier generation is NOT a complete view of this Record pass.
  t.noteOn(1, 61, 100, 196);
  t.noteOff(1, 61, 0, 245);
  runtime.reset(true); // the production stop's resetPlaybackState preserves raw bar 1
  TEST_ASSERT_FALSE_MESSAGE(runtime.bindFirstRecordingBar(loop.loopLengthTicks, &loop),
                            "the stop edge must not bind a stale first bar");
  TEST_ASSERT_FALSE(set->views[set->lifecycle.retained].complete);
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, loop.loopLengthTicks,
                                                             loop.loopLengthTicks);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    TrackCaptureStopTestAccess::closureGrain(t));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot));
  t.forceSetState(TRACK_PLAYING);
  TrackCaptureStopTestAccess::selectSlot(t, 0);
  TrackCaptureStopTestAccess::resetProjectionCycle(t);
  for (unsigned i = 0; i < 256 && !runtime.frozenRecordWindowReady(); ++i)
    (void)t.stepPlaybackViewMaintenance(0, nullptr, true, {});
  TEST_ASSERT_TRUE_MESSAGE(runtime.frozenRecordWindowReady(),
                           "the selected owner must recover the complete first bar from the frozen range");
  MidiSendCapture::reset();
  loop.lastTickInLoop = 0;
  loop.nextEventIndex = 0;
  TrackCaptureStopTestAccess::unmute(t);
  t.playMidiEvents(350, true);
  TEST_ASSERT_EQUAL_UINT32(4, MidiSendCapture::count());
  TEST_ASSERT_TRUE(MidiSendCapture::at(2).isNoteOn());
  TEST_ASSERT_EQUAL_UINT8(61, MidiSendCapture::at(2).data.noteData.note);
  TEST_ASSERT_TRUE(MidiSendCapture::at(3).isNoteOff());
  TEST_ASSERT_EQUAL_UINT8(61, MidiSendCapture::at(3).data.noteData.note);
}

void test_record_return_recycles_redundant_ready_bar_before_next_bar_build() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  Loop& loop = t.getLoop(0);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  if (runtime.viewSetIfPrewarmed() == nullptr) {
    TEST_ASSERT_TRUE(runtime.prewarmViews(loop.companionRowHorizonBound(),
                                          loop.fullTakePlaybackViewAdmissible()));
  }
  t.startRecording(100);
  t.noteOn(1, 60, 100, 112);
  t.noteOff(1, 60, 0, 180);
  t.noteOn(1, 62, 100, 900);
  t.noteOff(1, 62, 0, 1000);
  loop.loopLengthTicks = 2 * Config::TICKS_PER_BAR;
  const PlaybackViewSourceIdentity source{loop.playbackRevision,
      TrackCaptureStopTestAccess::playbackGenerationOf(t), loop.publishedSnapshotIdentity()};
  for (unsigned i = 0; i < 64; ++i)
    runtime.serviceFirstRecordingBar(loop, source, Config::TICKS_PER_BAR, 1);
  runtime.reset(true);
  TEST_ASSERT_TRUE(runtime.bindFirstRecordingBar(loop.loopLengthTicks, &loop));
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, loop.loopLengthTicks,
                                                             loop.loopLengthTicks);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    TrackCaptureStopTestAccess::closureGrain(t));
  t.forceSetState(TRACK_PLAYING);
  TrackCaptureStopTestAccess::selectSlot(t, 0);
  TrackCaptureStopTestAccess::resetProjectionCycle(t);
  TrackCaptureStopTestAccess::unmute(t);
  auto* set = runtime.viewSetIfPrewarmed();
  // Just before wrap the owner may prepare bar one again even though its
  // already-bound current view will serve that same bar at the clock edge.
  for (unsigned i = 0; i < 256 && !runtime.frozenRecordWindowReady(); ++i)
    (void)t.stepPlaybackViewMaintenance(loop.loopLengthTicks - 1, nullptr, true, {});
  TEST_ASSERT_TRUE_MESSAGE(runtime.frozenRecordWindowReady(), "redundant bar-one successor owed");
  TEST_ASSERT_EQUAL_UINT32(0, set->views[set->lifecycle.ready].coverage.startPhase);
  loop.lastTickInLoop = loop.loopLengthTicks - 1;
  t.playMidiEvents(loop.loopLengthTicks, true); // clock crosses the wrap; current bound bar remains served
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot));
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  // The holder must release the redundant complete ready role, not just clear
  // its metadata. Otherwise every new bar-two build finishes at a denied swap.
  TEST_ASSERT_TRUE_MESSAGE(t.playbackWindowServiceOffer(0, {}).eligible,
                           "a redundant ready copy must not suppress the holder after wrap");
  bool barTwoReady = false;
  for (unsigned i = 0; i < 256; ++i) {
    (void)t.stepPlaybackViewMaintenance(0, nullptr, true, {});
    if (set->lifecycle.ready < set->views.size() &&
        set->views[set->lifecycle.ready].complete &&
        set->views[set->lifecycle.ready].coverage.startPhase == Config::TICKS_PER_BAR) {
      barTwoReady = true;
      break;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(barTwoReady,
                           "selected owner cannot publish bar two while stale ready role remains occupied");
  const auto& ready = set->views[set->lifecycle.ready];
  TEST_ASSERT_EQUAL_UINT32(2, ready.storage.eventCount);
  TEST_ASSERT_TRUE(ready.storage.events[0].isNoteOn());
  TEST_ASSERT_EQUAL_UINT8(62, ready.storage.events[0].data.noteData.note);
  TEST_ASSERT_TRUE(ready.storage.events[1].isNoteOff());
  TEST_ASSERT_EQUAL_UINT8(62, ready.storage.events[1].data.noteData.note);
  MidiSendCapture::reset();
  TrackCaptureStopTestAccess::resetProjectionCycle(t);
  loop.lastTickInLoop = 0;
  loop.nextEventIndex = 0;
  t.playMidiEvents(940, true); // adopt only; never fill/order on this edge
  assertExactlyOnePlaybackNotePair(62, 800, 900);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot));
}

void test_missing_record_return_view_keeps_source_until_current_bar_is_adopted() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  Loop& loop = t.getLoop(0);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  if (runtime.viewSetIfPrewarmed() == nullptr) {
    TEST_ASSERT_TRUE(runtime.prewarmViews(loop.companionRowHorizonBound(),
                                          loop.fullTakePlaybackViewAdmissible()));
  }
  t.startRecording(100);
  t.noteOn(1, 60, 100, 112);
  t.noteOff(1, 60, 0, 180);
  t.noteOn(1, 62, 100, 900);
  t.noteOff(1, 62, 0, 1000);
  loop.loopLengthTicks = 2 * Config::TICKS_PER_BAR;
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, loop.loopLengthTicks,
                                                             loop.loopLengthTicks);
  auto* set = runtime.viewSetIfPrewarmed();
  TEST_ASSERT_NOT_NULL(set);
  TEST_ASSERT_TRUE(set->lifecycle.retained < set->views.size());
  // Controlled bind refusal: a prepared but incomplete first return view.
  // The frozen range and the remaining build role are otherwise real.
  set->views[set->lifecycle.retained].resetMetadata();
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    TrackCaptureStopTestAccess::closureGrain(t));
  TEST_ASSERT_EQUAL(RuntimePlaybackViewLifecycle::kNoView,
                    set->firstRecordReturnWindowIndex());
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::stagingGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::normalizationGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::passTransferGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::publicationGrain));
  TEST_ASSERT_TRUE(loop.pendingSnapshotSessionExitQueued());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  t.forceSetState(TRACK_PLAYING);
  TrackCaptureStopTestAccess::selectSlot(t, 0);
  TrackCaptureStopTestAccess::resetProjectionCycle(t);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Pending,
                    TrackCaptureStopTestAccess::retirementGrain(t));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot));
  // Task 3.15g: `RecordPublished` popped the interaction job, so the reader obligation is the
  // slot-owned presentation record's -- the ring depth can no longer speak for it.
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::presentationActive(t, job.slot));
  TEST_ASSERT_TRUE(t.playbackWindowServiceOffer(0, {}).eligible);
  for (unsigned turn = 0; turn < 256 && !runtime.frozenRecordWindowReady(); ++turn) {
    (void)t.stepPlaybackViewMaintenance(0, nullptr, true, {});
  }
  TEST_ASSERT_TRUE_MESSAGE(runtime.frozenRecordWindowReady(),
                           "the selected holder must prepare the current bar from the retained range");
  TEST_ASSERT_EQUAL_UINT32(0, set->views[set->lifecycle.ready].coverage.startPhase);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Pending,
                    TrackCaptureStopTestAccess::retirementGrain(t));
  MidiSendCapture::reset();
  loop.lastTickInLoop = 0;
  loop.nextEventIndex = 0;
  TrackCaptureStopTestAccess::unmute(t);
  t.playMidiEvents(90, true);
  assertExactlyOnePlaybackNotePair(60, 12, 80);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot));
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_TRUE(t.playbackWindowServiceOffer(Config::TICKS_PER_BAR - 1, {}).eligible);
}

// Real Record admission with no manually prebuilt raw first bar. Snapshot turns
// are withheld: only the frozen job can supply the first and second return bars.
// Task 3.15g private-root preparation regression. A published presentation retains a qualified
// frozen Capture source while its session-exit root is private. The same off-clock owner must prepare
// its return coverage without waiting for root publication. The rolling profile carries Capture
// origin plus the frozen content token; the default compatibility profile intentionally stamps copied
// coverage as Committed. Keep both controls: a default-profile origin is not a rolling precedence RED.
void test_published_record_presentation_prepares_frozen_return_before_root_is_current() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  Loop& loop = t.getLoop(0);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  t.startRecording(0);
  t.noteOn(1, 60, 100, 12);
  t.noteOff(1, 60, 0, 80);
  t.noteOn(1, 61, 100, 400);
  t.noteOff(1, 61, 0, 480);
  clockManager.assignCurrentTickSilently(1536);
  t.stopRecording(1536);
  // Shape 1 needs a live reader obligation during the deferred publication turns. Establish the
  // audible/playing condition before any owner can service presentation retirement; doing it after
  // the ready assertion would model the old ring-owned lifecycle and let the presentation retire first.
  TrackCaptureStopTestAccess::unmute(t);
  TEST_ASSERT_TRUE(t.isPlaying());
  TEST_ASSERT_TRUE(t.hasPendingDeferredOverdubStop());
  TEST_ASSERT_GREATER_THAN_UINT32(0, loop.loopLengthTicks);
  // Cross the real publication boundary: the job is popped and the presentation owns the reader.
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    TrackCaptureStopTestAccess::closureGrain(t));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::stagingGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::normalizationGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::passTransferGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::publicationGrain));
  auto* presentation = TrackCaptureStopTestAccess::presentation(t, 0);
  TEST_ASSERT_NOT_NULL(presentation);
  TEST_ASSERT_TRUE(presentation->retirementActive);
  TEST_ASSERT_TRUE(presentation->ownsExtent);
  // Premise: the qualified frozen source is bound while the root is still private.
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  RuntimePlaybackViewSource frozen{};
  PlaybackViewSourceIdentity receipt{};
  TEST_ASSERT_TRUE(t.qualifiedFrozenRecordCaptureSourceForSlot(0, presentation->interval, frozen, receipt));
  TEST_ASSERT_EQUAL_UINT64(presentation->frozenCaptureContentGeneration, receipt.captureContentGeneration);
  // Audible, playing, cursor at the top: the state the frozen reader serves.
  t.forceSetState(TRACK_PLAYING);
  TrackCaptureStopTestAccess::selectSlot(t, 0);
  TrackCaptureStopTestAccess::resetProjectionCycle(t);
  TrackCaptureStopTestAccess::unmute(t);
  loop.lastTickInLoop = 0;
  loop.nextEventIndex = 0;
  for (unsigned turn = 0; turn < 256 && !runtime.frozenRecordWindowReady(); ++turn)
    (void)t.stepPlaybackViewMaintenance(loop.loopLengthTicks, nullptr, true,
                                        PlaybackDeadlineClock{loop.loopLengthTicks, 1u});
  const auto* set = runtime.viewSetIfPrewarmed();
  TEST_ASSERT_NOT_NULL(set);
  char detail[288];
  const bool ready = set->lifecycle.ready < set->views.size();
  snprintf(detail, sizeof(detail),
           "ready=%u complete=%u origin=%u token=%llu frozenToken=%llu rootCurrent=%u curOrigin=%u",
           set->lifecycle.ready, ready ? static_cast<unsigned>(set->views[set->lifecycle.ready].complete) : 9u,
           ready ? static_cast<unsigned>(set->views[set->lifecycle.ready].source.origin) : 9u,
           ready ? static_cast<unsigned long long>(
                       set->views[set->lifecycle.ready].source.captureContentGeneration) : 0ull,
           static_cast<unsigned long long>(presentation->frozenCaptureContentGeneration),
           loop.publishedSnapshotIsCurrent() ? 1u : 0u,
           set->lifecycle.current < set->views.size()
               ? static_cast<unsigned>(set->views[set->lifecycle.current].source.origin) : 9u);
  TEST_ASSERT_TRUE_MESSAGE(runtime.frozenRecordWindowReady(), detail);
#if PLAYBACK_WINDOW_ROLLING_ENABLED
  TEST_ASSERT_EQUAL_MESSAGE(PlaybackWindowOrigin::Capture,
                            set->views[set->lifecycle.ready].source.origin, detail);
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(presentation->frozenCaptureContentGeneration,
                                   set->views[set->lifecycle.ready].source.captureContentGeneration, detail);
#else
  // The default compatibility reader uses committed currency for copied frozen coverage.
  TEST_ASSERT_EQUAL_MESSAGE(PlaybackWindowOrigin::Committed,
                            set->views[set->lifecycle.ready].source.origin, detail);
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(0,
                                   set->views[set->lifecycle.ready].source.captureContentGeneration, detail);
#endif
}
static void assertRecordRecoveryServesSecondBar(bool enterOverdub, bool checkPresentationIdentity = false,
                                               bool refreshRaw = false, bool holdReadyBeforeBoundary = false) {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  Loop& loop = t.getLoop(0);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  t.startRecording(0);
  t.noteOn(1, 60, 100, 12);
  t.noteOff(1, 60, 0, 80);
  if (refreshRaw) {
    clockManager.assignCurrentTickSilently(96);
#if PLAYBACK_WINDOW_ROLLING_ENABLED
    const auto refreshed = [&]() {
      const auto* views = runtime.viewSetIfPrewarmed();
      return views != nullptr && views->lifecycle.current < views->views.size() &&
          views->views[views->lifecycle.current].complete &&
          views->views[views->lifecycle.current].published &&
          views->views[views->lifecycle.current].source.origin == PlaybackWindowOrigin::Capture &&
          views->views[views->lifecycle.current].source.captureContentGeneration == loop.captureContentGeneration();
    };
    for (unsigned turn = 0; turn < 256 && !refreshed(); ++turn)
      (void)t.stepPlaybackViewMaintenance(96, nullptr, false, {});
    TEST_ASSERT_TRUE(refreshed());
#else
    for (unsigned turn = 0; turn < 256 &&
         (!runtime.recordingWindowGenerationValid ||
          runtime.recordingWindowGeneration != loop.captureDisplayRevision); ++turn)
      t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
    TEST_ASSERT_TRUE(runtime.recordingWindowGenerationValid);
    TEST_ASSERT_EQUAL_UINT64(loop.captureDisplayRevision, runtime.recordingWindowGeneration);
#endif
  }
  t.noteOn(1, 61, 100, 400); t.noteOff(1, 61, 0, 480);
  t.noteOn(1, 62, 100, 800); t.noteOff(1, 62, 0, 880);
  t.noteOn(1, 65, 100, 1480); t.noteOff(1, 65, 0, 1500);
  clockManager.assignCurrentTickSilently(1536);
  t.stopRecording(1536);
  TEST_ASSERT_TRUE(t.isPlaying());
  TEST_ASSERT_EQUAL_UINT32(1536, loop.loopLengthTicks);
  auto* set = runtime.viewSetIfPrewarmed();
  TEST_ASSERT_NOT_NULL(set);
  TEST_ASSERT_EQUAL(RuntimePlaybackViewLifecycle::kNoView, set->firstRecordReturnWindowIndex());
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
      driveGrains(t, TrackCaptureStopTestAccess::closureGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
      driveGrains(t, TrackCaptureStopTestAccess::stagingGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
      driveGrains(t, TrackCaptureStopTestAccess::normalizationGrain));
  TEST_ASSERT_TRUE(loop.prewarmActiveOverdubTransferHorizon());
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
      driveGrains(t, TrackCaptureStopTestAccess::passTransferGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
      driveGrains(t, TrackCaptureStopTestAccess::publicationGrain));
  TEST_ASSERT_EQUAL_UINT32(0, TrackCaptureStopTestAccess::stopQueueDepth(t));
  auto* presentation = TrackCaptureStopTestAccess::presentation(t, 0);
  TEST_ASSERT_TRUE(presentation->retirementActive && presentation->ownsExtent);
  RuntimePlaybackViewSource frozen{};
  PlaybackViewSourceIdentity receipt{};
  TEST_ASSERT_TRUE_MESSAGE(t.qualifiedFrozenRecordCaptureSourceForSlot(
      0, presentation->interval, frozen, receipt), "admission-produced presentation identity must qualify");
  TEST_ASSERT_EQUAL_UINT64(presentation->frozenCaptureContentGeneration, frozen.captureContentToken);
  TEST_ASSERT_TRUE(loop.pendingSnapshotSessionExitQueued());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TrackCaptureStopTestAccess::selectSlot(t, 0);
  TrackCaptureStopTestAccess::resetProjectionCycle(t);
  TrackCaptureStopTestAccess::unmute(t);
  loop.lastTickInLoop = 0;
  loop.nextEventIndex = 0;
  const auto position = [&](uint32_t tick) {
    clockManager.assignCurrentTickSilently(tick);
#if PLAYBACK_WINDOW_ROLLING_ENABLED
    nativePlaybackDeadlineClock = {tick, 1};
#else
    nativePlaybackDeadlineClock = {};
#endif
  };
  const auto play = [&](uint32_t tick) { position(tick); t.playMidiEvents(tick, true); };
  const auto prepare = [&](uint32_t tick, uint32_t expectedStart) {
    position(tick);
    for (unsigned turn = 0; turn < 256 && !runtime.frozenRecordWindowReady(); ++turn)
      (void)t.stepPlaybackViewMaintenance(tick, nullptr, true, nativePlaybackDeadlineClock);
    TEST_ASSERT_TRUE_MESSAGE(runtime.frozenRecordWindowReady(), "off-clock owner must prepare the frozen interval");
    const auto& ready = set->views[set->lifecycle.ready];
    TEST_ASSERT_EQUAL_UINT32(expectedStart, ready.coverage.startPhase);
#if PLAYBACK_WINDOW_ROLLING_ENABLED
    TEST_ASSERT_EQUAL(PlaybackWindowOrigin::Capture, ready.source.origin);
    TEST_ASSERT_EQUAL_UINT64(presentation->frozenCaptureContentGeneration, ready.source.captureContentGeneration);
#else
    TEST_ASSERT_EQUAL(PlaybackWindowOrigin::Committed, ready.source.origin);
#endif
  };
  prepare(1536, 0);
  const uint8_t firstRole = set->lifecycle.ready;
  MidiSendCapture::reset();
  play(1536);  // Real eligible clock edge: prepare -> ready -> E2/current -> serve.
  play(1548); play(1616);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, MidiSendCapture::count(), "first recovery bar emits its scheduled pair");
  assertExactlyOnePlaybackNotePair(60, 12, 80);
#if PLAYBACK_WINDOW_ROLLING_ENABLED
  TEST_ASSERT_EQUAL_UINT8(firstRole, runtime.rolling.boundView);
  TEST_ASSERT_EQUAL_UINT8(firstRole, set->lifecycle.current);
#else
  (void)firstRole;
#endif
  TEST_ASSERT_FALSE(runtime.frozenRecordWindowReady());
  play(1664);
  prepare(1664, 768);
  const uint8_t nextRole = set->lifecycle.ready;
  const uint64_t nextStorage = set->views[nextRole].storageGeneration;
  if (checkPresentationIdentity) {
    const LoopId correctLoopId = presentation->loopId;
    presentation->loopId = kInvalidLoopId;
    TEST_ASSERT_FALSE(t.qualifiedFrozenRecordCaptureSourceForSlot(0, presentation->interval, frozen, receipt));
    presentation->loopId = correctLoopId;
    TEST_ASSERT_TRUE(t.qualifiedFrozenRecordCaptureSourceForSlot(0, presentation->interval, frozen, receipt));
  }
  MidiSendCapture::reset();
  play(1936); play(2016);
  TEST_ASSERT_EQUAL_UINT32(2, MidiSendCapture::count());
  assertExactlyOnePlaybackNotePair(61, 400, 480);
  TEST_ASSERT_EQUAL_UINT8(nextRole, set->lifecycle.ready); // No mid-interval adoption.
  if (holdReadyBeforeBoundary) {
    position(2256);
    (void)t.stepPlaybackViewMaintenance(2256, nullptr, true, nativePlaybackDeadlineClock);
    TEST_ASSERT_EQUAL_UINT8(nextRole, set->lifecycle.ready);
    TEST_ASSERT_EQUAL_UINT64(nextStorage, set->views[nextRole].storageGeneration);
  }
  if (enterOverdub) {
    position(2240);
    t.startOverdubbing(2240);
    TEST_ASSERT_TRUE(t.isOverdubbing());
    t.noteOn(1, 70, 100, 2256); t.noteOff(1, 70, 0, 2272);
    MidiSendCapture::reset();
    play(2256); play(2272);
    assertExactlyOnePlaybackNotePair(70, 720, 736);
  }
  MidiSendCapture::reset();
  play(2304); play(2336); play(2416);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, MidiSendCapture::count(), "second recovery bar emits its scheduled pair");
  assertExactlyOnePlaybackNotePair(62, 800, 880);
  const size_t emitted = MidiSendCapture::count();
  play(2416);
  TEST_ASSERT_EQUAL_UINT32(emitted, MidiSendCapture::count());
  TEST_ASSERT_TRUE(presentation->ownsExtent);
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  unsigned sounding = 0;
  runtime.ledger.forEachActive([&](uint8_t, uint8_t, const ActiveNoteLedger::Entry&) { ++sounding; });
  TEST_ASSERT_EQUAL_UINT32(0, sounding);
  if (enterOverdub) TEST_ASSERT_EQUAL_UINT32(2, loop.capture.store.size());
  else {
    prepare(2496, 0);
    MidiSendCapture::reset();
    play(3016); play(3036);
    assertExactlyOnePlaybackNotePair(65, 1480, 1500);
    MidiSendCapture::reset();
    play(3072); play(3084); play(3152);
    assertExactlyOnePlaybackNotePair(60, 12, 80);
    TEST_ASSERT_TRUE(presentation->ownsExtent);
    TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  }
}

void test_record_bind_refusal_recovers_and_prepares_second_bar() {
  assertRecordRecoveryServesSecondBar(false);
}

void test_record_bind_refusal_keeps_second_bar_audible_during_overdub() {
  assertRecordRecoveryServesSecondBar(true);
}

void test_record_bind_refusal_keeps_base_during_record_publication() {
  assertRecordRecoveryServesSecondBar(true, true);
}

void test_record_ready_before_publication_survives_owner_and_adoption() {
  assertRecordRecoveryServesSecondBar(true, true, false, true);
}

void test_refreshed_raw_record_view_keeps_spare_role_after_bind_refusal() {
  assertRecordRecoveryServesSecondBar(false, false, true);
}

void test_bound_record_revision_handoff_keeps_current_tick_off_before_on() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  Loop& loop = t.getLoop(0);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  t.startRecording(0);
  t.noteOn(1, 60, 100, 672);
  t.noteOff(1, 60, 0, 720);
  t.noteOn(1, 61, 100, 720);
  t.noteOff(1, 61, 0, 760);
  t.noteOn(1, 62, 100, 1480);
  t.noteOff(1, 62, 0, 1500);
  clockManager.assignCurrentTickSilently(1504);
  for (unsigned turn = 0; turn < 256 &&
       (!runtime.recordingWindowGenerationValid ||
        runtime.recordingWindowGeneration != loop.captureDisplayRevision); ++turn) {
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  }
  TEST_ASSERT_TRUE(runtime.recordingWindowGenerationValid);
  TEST_ASSERT_EQUAL_UINT64(loop.captureDisplayRevision, runtime.recordingWindowGeneration);
  clockManager.assignCurrentTickSilently(1536);
  t.stopRecording(1536);
  auto* set = runtime.viewSetIfPrewarmed();
  TEST_ASSERT_TRUE(set->firstRecordReturnWindowIndex() < set->views.size());
  TrackCaptureStopTestAccess::unmute(t);
  MidiSendCapture::reset();
  t.playMidiEvents(1536, true);
  t.playMidiEvents(2208, true);
  TEST_ASSERT_EQUAL_UINT32(1, MidiSendCapture::count());
  TEST_ASSERT_TRUE(MidiSendCapture::at(0).isNoteOn());
  TEST_ASSERT_EQUAL_UINT32(672, MidiSendCapture::at(0).tick);
  for (unsigned turn = 0; turn < 4096 &&
       TrackCaptureStopTestAccess::phase(t) != PendingStopPhase::RecordPublished; ++turn) {
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  }
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPublished, TrackCaptureStopTestAccess::phase(t));
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  t.playMidiEvents(2256, true);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, MidiSendCapture::count(), "revision handoff must not skip due tick720 Off/On");
  const MidiEvent& off = MidiSendCapture::at(1);
  const MidiEvent& on = MidiSendCapture::at(2);
  TEST_ASSERT_TRUE(off.isNoteOff());
  TEST_ASSERT_EQUAL_UINT8(60, off.data.noteData.note);
  TEST_ASSERT_EQUAL_UINT32(720, off.tick);
  TEST_ASSERT_TRUE(on.isNoteOn());
  TEST_ASSERT_EQUAL_UINT8(61, on.data.noteData.note);
  TEST_ASSERT_EQUAL_UINT32(720, on.tick);
  t.playMidiEvents(2256, true);
  TEST_ASSERT_EQUAL_UINT32(3, MidiSendCapture::count());
  t.playMidiEvents(2296, true);
  TEST_ASSERT_EQUAL_UINT32(4, MidiSendCapture::count());
  TEST_ASSERT_TRUE(MidiSendCapture::at(3).isNoteOff());
  TEST_ASSERT_EQUAL_UINT8(61, MidiSendCapture::at(3).data.noteData.note);
  TEST_ASSERT_EQUAL_UINT32(760, MidiSendCapture::at(3).tick);
  size_t openNotes = 0;
  runtime.ledger.forEachActive([&](uint8_t, uint8_t, const ActiveNoteLedger::Entry&) { ++openNotes; });
  TEST_ASSERT_EQUAL_UINT32(0, openNotes);
}

void test_missing_record_return_view_survives_snapshot_publication_until_handoff() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  Loop& loop = t.getLoop(0);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  if (runtime.viewSetIfPrewarmed() == nullptr) {
    TEST_ASSERT_TRUE(runtime.prewarmViews(loop.companionRowHorizonBound(),
                                          loop.fullTakePlaybackViewAdmissible()));
  }
  t.startRecording(100);
  t.noteOn(1, 60, 100, 112);
  t.noteOff(1, 60, 0, 180);
  loop.loopLengthTicks = 24 * Config::TICKS_PER_BAR; // Admissible next-wrap lead with no served reader.
  // A missing view has no retention lease. Clearing metadata alone would leave retained==current
  // and correctly prevent the lifecycle from recycling that role at initial publication.
  TEST_ASSERT_TRUE(runtime.releaseFirstRecordingBarReturnWindow());
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, loop.loopLengthTicks,
                                                             loop.loopLengthTicks);
  auto* set = runtime.viewSetIfPrewarmed();
  TEST_ASSERT_EQUAL(RuntimePlaybackViewLifecycle::kNoView, set->lifecycle.retained);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    TrackCaptureStopTestAccess::closureGrain(t));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::stagingGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::normalizationGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::passTransferGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::publicationGrain));
  t.forceSetState(TRACK_PLAYING);
  TrackCaptureStopTestAccess::selectSlot(t, 0);
  TrackCaptureStopTestAccess::resetProjectionCycle(t);
  loop.lastTickInLoop = 0;
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Pending,
                    TrackCaptureStopTestAccess::retirementGrain(t));
  for (unsigned turn = 0; turn < 20000 && !loop.publishedSnapshotIsCurrent(); ++turn)
    loop.stepPendingSnapshotPublish(4, true, 0);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Pending,
                    TrackCaptureStopTestAccess::retirementGrain(t));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot));
  // The root is current, but the playback owner still must publish a complete
  // replacement before retirement; the clock edge never builds that replacement.
  // With no initial reader, use an admissible next-wrap promise rather than the one-bar shape whose
  // complete service cost exceeds an entire traversal. The public request API retains/adjudicates it.
  runtime.clearViewPendingRequest();
  nativePlaybackDeadlineClock = {1, 1};
  clockManager.assignCurrentTickSilently(1);
  TEST_ASSERT_EQUAL(Track::PlaybackViewRequestResult::DeferredSourceQualification,
      t.requestCommittedPlaybackWindow(0, {0, Config::TICKS_PER_BAR, loop.loopLengthTicks},
                                       1, nativePlaybackDeadlineClock));
  const auto rootServed = [&]() {
    PlaybackViewSourceIdentity served{};
    PlaybackCoverageInterval coverage{};
    return t.publishedPlaybackWindowForSlot(0, served, coverage) &&
        served.origin == PlaybackWindowOrigin::Committed &&
        served.snapshotIdentity == loop.publishedSnapshotIdentity();
  };
  for (unsigned turn = 0; turn < 512 && !rootServed(); ++turn)
    (void)t.stepPlaybackViewMaintenance(1, nullptr, true, nativePlaybackDeadlineClock);
  TEST_ASSERT_TRUE_MESSAGE(rootServed(), "the owner must publish the first complete current-root reader");
  TrackCaptureStopTestAccess::unmute(t);
  loop.nextEventIndex = 0;
  MidiSendCapture::reset();
  nativePlaybackDeadlineClock = {90, 1};
  clockManager.assignCurrentTickSilently(90);
  t.playMidiEvents(90, true);
  assertExactlyOnePlaybackNotePair(60, 12, 80);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::retirementGrain));
  TEST_ASSERT_EQUAL_UINT32(0, TrackCaptureStopTestAccess::stopQueueDepth(t));
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::presentationActive(t, 0));
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::extentOwned(t, 0));
  TEST_ASSERT_FALSE(runtime.hasBoundRecordReturnWindowForPendingJob());
}

// ---------------------------------------------------------------------------
// S7.5b.5 (packet oracles 4-6): the Record-return lookahead crossed with the real owner selection.
//
// The packet's own words: the retained Record source "MUST be eligible through the existing
// playback-holder selection/admission path; it MUST NOT be serviced by a side call that bypasses
// owner selection, fairness, or the one-grain-per-turn contract". The device's selection is the main
// loop's `PlaybackViewMaintenance` dispatch -- every track offers its most urgent owing holder, the
// shared policy picks one **track** round-robin, and the chosen track's `stepPlaybackViewMaintenance`
// serves one holder turn (src/main.cpp). This helper reproduces that dispatch, so the fixture drives
// the same selection the clock path does, and reports which track was served.
/// serves one holder turn (src/main.cpp). This helper reproduces that dispatch, so the fixture drives
/// the same selection the clock path does, and reports which track was served. The per-track holder
/// cursors are the calling convention the policy documents for a caller that serves more than one
/// holder ("a caller that serves more than one holder passes its own cursor so the rotation persists
/// across turns"), which is what makes the rotation's fairness bound observable here.
static uint8_t stepWindowOwnerCadence(MidiServiceDrain::PlaybackHolderCursor& cursor,
                                     MidiServiceDrain::PlaybackHolderCursor* holderCursorPerTrack,
                                     uint32_t phase, PlaybackDeadlineClock clock) {
  const uint8_t trackCount = trackManager.getTrackCount();
  const uint8_t offerCount =
      trackCount < static_cast<uint8_t>(Config::NUM_TRACKS) ? trackCount : static_cast<uint8_t>(Config::NUM_TRACKS);
  MidiServiceDrain::PlaybackHolder offers[Config::NUM_TRACKS] = {};
  for (uint8_t i = 0; i < offerCount; ++i) {
    trackManager.getTrack(i).serviceAdmittedDeadlineMisses(clock);
    offers[i] = trackManager.getTrack(i).playbackWindowServiceOffer(phase, clock);
  }
  uint8_t chosen = MidiServiceDrain::kNoPlaybackHolder;
  if (MidiServiceDrain::selectPlaybackHolderRoundRobin(cursor, offers, offerCount, chosen) !=
      MidiServiceDrain::PlaybackHolderSelection::Service) {
    return MidiServiceDrain::kNoPlaybackHolder;
  }
  (void)trackManager.getTrack(chosen).stepPlaybackViewMaintenance(
      phase, holderCursorPerTrack != nullptr ? &holderCursorPerTrack[chosen] : nullptr, true, clock);
  return chosen;
}

// The multi-bar Record-return lookahead. A four-bar Record pass is stopped inside the publication
// gap: the committed source (the session-exit `LoopSnapshot`) is still stale, the job owns its sealed
// `CaptureRange`, and the bound bar-1 return window is the only served view. Through the main
// owner-selection cadence -- with a competing eligible holder on the same track and the Snapshot and
// persistence owners interleaved -- the fixture then walks the whole interval at 64-tick frames and
// requires, per boundary: the next bar's complete one-bar successor prepared through the selected
// holder (never on the clock edge), the served view unchanged until adoption, and the exact ordered
// MIDI of the committed take emitted once, with no gap, duplicate or early event.
void test_record_return_lookahead_readies_every_boundary_under_owner_contention() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  Loop& loop = t.getLoop(0);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  constexpr uint32_t kBar = Config::TICKS_PER_BAR;
  constexpr uint32_t kBars = 4;
  constexpr uint32_t kFrame = 64;
  constexpr uint32_t kDensePairs = 40;  // bar 2 is dense, so its successor needs several fill grains
  const uint32_t loopLength = kBars * kBar;
  if (runtime.viewSetIfPrewarmed() == nullptr) {
    TEST_ASSERT_TRUE(runtime.prewarmViews(loop.companionRowHorizonBound(),
                                          loop.fullTakePlaybackViewAdmissible()));
  }

  // The competing eligible holder: an independently owed committed window on the **same track's**
  // other slot, requested through the production admission. It is owed for the whole lookahead
  // window, so its own eligibility is its own work and not an artifact of the job under test.
  const auto prepareCompetingHolder = [&]() {
    Loop& sibling = t.getLoop(1);
    sibling.loopLengthTicks = 2 * kBar;
    sibling.beginCapture(CapturePhase::Record);
    for (uint32_t bar = 0; bar < 2; ++bar) {
      TEST_ASSERT_TRUE(sibling.appendCaptureEvent(MidiEvent::NoteOn(bar * kBar, 1, 40, 100)));
      TEST_ASSERT_TRUE(sibling.appendCaptureEvent(MidiEvent::NoteOff(bar * kBar + 48, 1, 40, 0)));
    }
    TEST_ASSERT_EQUAL(SealOutcome::Ok,
                      sibling.sealCapture(sibling.loopLengthTicks, CommitReason::RecordStop));
    TEST_ASSERT_TRUE(sibling.commitPendingCapturePass());
    for (unsigned i = 0; i < 20000 && sibling.hasPendingSnapshotPublish(); ++i) {
      sibling.stepPendingSnapshotPublish(4, true, 0);
    }
    TEST_ASSERT_TRUE_MESSAGE(sibling.publishedSnapshotIsCurrent(),
                             "the competing holder needs its own current root to owe its window");
    // Its deadline is deliberately further out than the Record-return holder's own lead, so the
    // policy's answer is deterministic: the imminent Record boundary is served first while the
    // sibling stays **eligible** (which is the contention) and is served as soon as the Record
    // holder's work is done.
    const PlaybackCoverageInterval siblingCoverage{kBar, kBar, sibling.loopLengthTicks};
    const Track::PlaybackViewRequestResult siblingRequest = t.requestCommittedPlaybackWindow(
        1, siblingCoverage, 0u, PlaybackDeadlineClock{0, 1});
    TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::PlaybackViewRequestResult::DeferredSourceQualification),
                              static_cast<int>(siblingRequest),
                              "the competing holder must be admitted through the production request");
  };
  prepareSlot(t, 1, false);

  // The Record pass: four bars, one pair in bars 1/3/4 and a dense pair block in bar 2.
  t.startRecording(100);
  for (uint32_t bar = 0; bar < kBars; ++bar) {
    const uint32_t rows = bar == 2 ? kDensePairs : 1;
    for (uint32_t i = 0; i < rows; ++i) {
      // 16-tick spacing and a 12-tick pair: safely above the capture's 8-tick minimum note length.
      const uint32_t onset = 100 + bar * kBar + 12 + (i * 16);
      t.noteOn(1, static_cast<uint8_t>(60 + bar), 100, onset);
      t.noteOff(1, static_cast<uint8_t>(60 + bar), 0, onset + 12);
    }
  }
  loop.loopLengthTicks = loopLength;
  const PlaybackViewSourceIdentity recordingSource{
      loop.playbackRevision, TrackCaptureStopTestAccess::playbackGenerationOf(t),
      loop.publishedSnapshotIdentity()};
  for (size_t grain = 0; grain < 128; ++grain) {
    runtime.serviceFirstRecordingBar(loop, recordingSource, kBar, 1);
  }
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, loopLength, loopLength);
  {
    const auto closure = TrackCaptureStopTestAccess::closureGrain(t);
    const auto staging = closure == LoopEventStoreRewriteResult::Complete
        ? driveGrains(t, TrackCaptureStopTestAccess::stagingGrain) : closure;
    const auto normalization = staging == LoopEventStoreRewriteResult::Complete
        ? driveGrains(t, TrackCaptureStopTestAccess::normalizationGrain) : staging;
    const bool horizon = normalization == LoopEventStoreRewriteResult::Complete
        ? loop.prewarmActiveOverdubTransferHorizon() : false;
    const auto transfer = horizon
        ? driveGrains(t, TrackCaptureStopTestAccess::passTransferGrain) : normalization;
    char stepTrace[320];
    snprintf(stepTrace, sizeof(stepTrace),
             "closure=%u staging=%u normalization=%u horizon=%d transfer=%u outEvents=%zu "
             "outChunks=%zu ownsPendingPass=%d ownsRaw=%d passes=%zu",
             static_cast<unsigned>(closure), static_cast<unsigned>(staging),
             static_cast<unsigned>(normalization), horizon ? 1 : 0,
             static_cast<unsigned>(transfer), runtime.canonicalCaptureOutput.store.size(),
             runtime.canonicalCaptureOutput.store.chunkIds().size(),
             job.ownsPendingPass ? 1 : 0, TrackCaptureStopTestAccess::extentOwned(t, job.slot) ? 1 : 0,
             loop.passes.overdubPasses.size());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        static_cast<uint32_t>(LoopEventStoreRewriteResult::Complete),
        static_cast<uint32_t>(transfer), stepTrace);
    TEST_ASSERT_EQUAL_MESSAGE(
        LoopEventStoreRewriteResult::Complete,
        driveGrains(t, TrackCaptureStopTestAccess::publicationGrain), "publication");
  }
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPublished, TrackCaptureStopTestAccess::phase(t));
  TEST_ASSERT_TRUE(loop.passes.hasRecordPass());
  TEST_ASSERT_TRUE(loop.pendingSnapshotSessionExitQueued());
  TEST_ASSERT_FALSE_MESSAGE(loop.publishedSnapshotIsCurrent(),
                            "precondition: the session-exit root is still stale");
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot));
  TEST_ASSERT_TRUE(runtime.hasBoundRecordReturnWindowForPendingJob());
  TEST_ASSERT_FALSE(runtime.frozenWindowBuildActive());
  // The competing holder is admitted for the publication gap itself (the window this fixture drives).
  prepareCompetingHolder();
  TEST_ASSERT_NOT_NULL_MESSAGE(t.pendingViewRequest(1),
                               "the competing holder must be owed work during the gap");

  // Independent expectation: while the root is stale the job's own frozen `CaptureRange` **is** the
  // committed content, so every row it holds is expected exactly once, in its own bar.
  struct ExpectedRow {
    uint32_t tick;
    bool on;
    uint8_t note;
  };
  std::vector<ExpectedRow> expected[kBars];
  for (size_t chunk = 0; chunk < TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.chunkCount(); ++chunk) {
    for (uint16_t index = 0;; ++index) {
      MidiEvent event;
      bool atEnd = false;
      TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.read(TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.generation(), chunk, index, event, atEnd));
      if (atEnd) break;
      if (!event.isNoteOn() && !event.isNoteOff()) continue;
      const uint32_t phase = IntervalProjection::playbackEventPhase(event.tick, loopLength);
      expected[phase / kBar].push_back(
          ExpectedRow{phase, event.isNoteOn(), event.data.noteData.note});
    }
  }
  for (uint32_t bar = 0; bar < kBars; ++bar) {
    const uint32_t rows = bar == 2 ? kDensePairs * 2 : 2;
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(rows, static_cast<uint32_t>(expected[bar].size()),
                                     "the frozen range must hold each bar's pair exactly once");
  }

  t.forceSetState(TRACK_PLAYING);
  TrackCaptureStopTestAccess::unmute(t);
  TrackCaptureStopTestAccess::selectSlot(t, 0);
  TrackCaptureStopTestAccess::resetProjectionCycle(t);
  loop.lastTickInLoop = 0;
  loop.nextEventIndex = 0;
  MidiSendCapture::reset();

  const PlaybackViewSourceIdentity expectedSource{
      loop.playbackRevision, TrackCaptureStopTestAccess::playbackGenerationOf(t),
      loop.publishedSnapshotIdentity()};
  TEST_ASSERT_NOT_NULL_MESSAGE(
      runtime.adoptFirstRecordingBarReturnWindow(loop, expectedSource, loopLength, kBar / 2),
      "the bound bar-1 return window is the only source while the root is stale");

  MidiServiceDrain::PlaybackHolderCursor trackCursor{};
  MidiServiceDrain::PlaybackHolderCursor holderCursors[Config::NUM_TRACKS] = {};
  size_t emittedBase = 0;
  std::vector<MidiEvent> emitted;
  const auto drainEmitted = [&]() {
    while (emittedBase < MidiSendCapture::count()) {
      emitted.push_back(MidiSendCapture::at(emittedBase++));
    }
  };
  const RuntimePlaybackViewSet* set = runtime.viewSetIfPrewarmed();
  const auto readyView = [&]() -> const RuntimePlaybackView* {
    const RuntimePlaybackViewSet* live = runtime.viewSetIfPrewarmed();
    if (live == nullptr || !runtime.frozenRecordWindowReady()) return nullptr;
    return &live->views[live->lifecycle.ready];
  };
  const auto readyTargetStart = [&]() -> uint32_t {
    const RuntimePlaybackView* ready = readyView();
    return ready == nullptr ? UINT32_MAX : ready->coverage.startPhase;
  };
  const auto fillProgress = [&]() -> size_t {
    const RuntimePlaybackViewSet* live = runtime.viewSetIfPrewarmed();
    if (live == nullptr) return 0;
    if (runtime.frozenWindowBuildActive() && live->lifecycle.build < live->views.size()) {
      return live->views[live->lifecycle.build].storage.eventCount;
    }
    if (live->lifecycle.ready < live->views.size() &&
        live->views[live->lifecycle.ready].complete &&
        !live->views[live->lifecycle.ready].firstRecordReturnWindow) {
      return live->views[live->lifecycle.ready].storage.eventCount;
    }
    return 0;
  };
  // The competing holder's own progress, so a turn's service can be attributed to the holder the
  // policy actually chose (the two holders share this track, so the track alone does not say which).
  auto& siblingRuntime = TrackCaptureStopTestAccess::runtimeFor(t, 1);
  const auto siblingProgress = [&]() -> uint32_t {
    return static_cast<uint32_t>(siblingRuntime.coverageSource.readUnits()) +
        (siblingRuntime.viewBuild.active() ? 1u : 0u) +
        (siblingRuntime.ownsCoverageSource() ? 1u : 0u);
  };

  uint32_t perBarTurns[kBars] = {};
  uint32_t perBarMaxFillDelta[kBars] = {};
  size_t competitorTurns = 0;
  size_t recordTurns = 0;
  size_t snapshotTurns = 0;
  size_t persistenceTurns = 0;
  // One preparation turn per frame would prepare the successor a whole bar early; the fixture instead
  // prepares it as soon as the playhead is inside the preceding bar, which is the contract's "before
  // each boundary".
  size_t servedViewAdoptions = 0;
  std::string history;
  const size_t stepCount = (loopLength + kBar) / kFrame;
  uint8_t lastCurrent = RuntimePlaybackViewLifecycle::kNoView;
  uint32_t lastCurrentStart = UINT32_MAX;
  // D10 / task 3.9: the frozen Record-return lane's own rows over this four-bar publication gap. Measured (2026-10-09):
  // this window produces **5** completed successors, so the `build_complete` count is asserted here -- the completion is
  // an event, not a cadence sample, so the assertion is exact. The **receipt**'s row is a sample of a declared interval
  // and this window's few calls cannot carry it; it is pinned by
  // `test_frozen_record_return_terminals_report_the_slots_readiness_receipt`, which drives more than one interval's
  // worth of the frozen owner's own refusals.
  PlaybackPathRows::reset();
  WindowReadinessRows::reset();
  PlaybackPathRows::reset();
  WindowReadinessRows::reset();
  for (size_t step = 1; step <= stepCount; ++step) {
    const uint32_t tick = static_cast<uint32_t>(step) * kFrame;
    const uint32_t phase = IntervalProjection::tickPhaseInProjectionCycle(tick, 0, loopLength);
    const uint32_t upcoming =
        ((phase / kBar) + 1) * kBar >= loopLength ? 0u : (((phase / kBar) + 1) * kBar);
    const uint32_t upcomingBar = upcoming / kBar;
    const bool atBoundary = (phase % kBar) == 0;  // includes the wrap (phase 0)
    const bool boundarySuccessorWaiting = [&]() {
      const RuntimePlaybackView* ready = readyView();
      return ready != nullptr && ready->coverage.contains(phase);
    }();
    // (b) The Snapshot publication and the persistence owner stay eligible beside the lookahead.
    if (loop.hasPendingSnapshotPublish()) {
      t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
      ++snapshotTurns;
      TEST_ASSERT_FALSE_MESSAGE(loop.publishedSnapshotIsCurrent(),
                                "no partial candidate may be marked current mid-construction");
    }
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Persistence, 1);
    ++persistenceTurns;
    // (a) The next bar's successor, one bar ahead, through the real selection while a competing
    // eligible holder is owed on the same track. A **boundary tick** already holds the successor for
    // the bar the playhead has just entered: preparation for the following bar must wait for the
    // clock edge to adopt it, so this turn is skipped.
    if (!(atBoundary && boundarySuccessorWaiting) && readyTargetStart() != upcoming) {
      size_t turns = 0;
      uint32_t maxDelta = 0;
      for (; turns < 512 && readyTargetStart() != upcoming; ++turns) {
        const size_t filledBefore = fillProgress();
        const uint32_t siblingBefore = siblingProgress();
        const uint8_t served = stepWindowOwnerCadence(trackCursor, holderCursors, phase,
                                                      PlaybackDeadlineClock{phase, 1});
        if (served == MidiServiceDrain::kNoPlaybackHolder) continue;
        const size_t filledAfter = fillProgress();
        const uint32_t siblingAfter = siblingProgress();
        if (filledAfter > filledBefore) ++recordTurns;
        if (siblingAfter > siblingBefore) ++competitorTurns;
        if (filledAfter > filledBefore) {
          const uint32_t delta = static_cast<uint32_t>(filledAfter - filledBefore);
          if (delta > maxDelta) maxDelta = delta;
        }
      }
      perBarTurns[upcomingBar] += static_cast<uint32_t>(turns);
      if (maxDelta > perBarMaxFillDelta[upcomingBar]) perBarMaxFillDelta[upcomingBar] = maxDelta;
    }
    // (b) The owner turn may run **before** the clock edge of this same tick -- the device order is
    // the clock's business, not this owner's. Drive one extra selected turn at a boundary tick and
    // require the successor the boundary needs to survive it: a maintenance turn that discarded the
    // window the playhead just entered would hand the clock edge an empty bar (the gap this slice's
    // packet records). The turn slot is then used by the clock edge, which is served below.
    if (atBoundary && boundarySuccessorWaiting) {
      (void)stepWindowOwnerCadence(trackCursor, holderCursors, phase, PlaybackDeadlineClock{phase, 1});
      const RuntimePlaybackView* survivor = readyView();
      TEST_ASSERT_NOT_NULL_MESSAGE(
          survivor,
          "an owner turn at the boundary must not discard the successor the boundary needs");
      TEST_ASSERT_TRUE_MESSAGE(survivor->coverage.contains(phase),
                               "the boundary's successor must still cover the playhead");
    }
    // (c) The clock edge consumes the prepared successor and emits this tick's committed rows. It
    // must find no fill/order work in flight, and the served view may not be swapped mid-bar.
    TEST_ASSERT_FALSE_MESSAGE(runtime.frozenWindowBuildActive(),
                              "the clock edge must not find fill/order work in flight");
    TEST_ASSERT_FALSE_MESSAGE(loop.publishedSnapshotIsCurrent(),
                              "the root stays stale for the whole lookahead window");
    TrackCaptureStopTestAccess::resetProjectionCycle(t);
    loop.nextEventIndex = 0;
    t.playMidiEvents(tick, true);
    drainEmitted();
    const RuntimePlaybackViewSet* servedSet = runtime.viewSetIfPrewarmed();
    if (servedSet != nullptr) {
      const RuntimePlaybackView& servedView = servedSet->views[servedSet->lifecycle.current];
      if (!atBoundary && lastCurrent != RuntimePlaybackViewLifecycle::kNoView) {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(lastCurrent, servedSet->lifecycle.current,
                                        "the served view must not change mid-bar");
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(lastCurrentStart, servedView.coverage.startPhase,
                                         "the served coverage must not change mid-bar");
      }
      lastCurrent = servedSet->lifecycle.current;
      lastCurrentStart = servedView.coverage.startPhase;
    }
    {
      const RuntimePlaybackViewSet* live = runtime.viewSetIfPrewarmed();
      MidiServiceDrain::PlaybackHolder holders[Config::MAX_LOOPS_PER_TRACK] = {};
      t.fillPlaybackHolders(phase, holders, PlaybackDeadlineClock{phase, 1});
      const auto* pending0 = t.pendingViewRequest(0);
      const auto* pending1 = t.pendingViewRequest(1);
      const auto viewState = [&](uint8_t index) {
        if (live == nullptr || index >= live->views.size()) return std::string("--");
        const RuntimePlaybackView& v = live->views[index];
        char one[96];
        snprintf(one, sizeof(one), "%u/%d/%u/%u/%d/%d", static_cast<unsigned>(index),
                 v.complete ? 1 : 0, v.coverage.startPhase, v.coverage.lengthTicks,
                 v.published ? 1 : 0, v.firstRecordReturnWindow ? 1 : 0);
        return std::string(one);
      };
      char trace[720];
      snprintf(trace, sizeof(trace),
               "step=%zu phase=%u upcoming=%u emitted=%zu readyStart=%u cur=[%s] ready=[%s] "
               "build=[%s] buildActive=%d grain=%u h0(e%d,p%d,d%d,lead%u) h1(e%d,p%d,d%d,lead%u) "
               "covSvc=%d ownSrc=%d pend0=%d pend1=%d rootCurrent=%d",
               step, phase, upcoming, emitted.size(), readyTargetStart(),
               viewState(live ? live->lifecycle.current : 255).c_str(),
               viewState(live ? live->lifecycle.ready : 255).c_str(),
               viewState(live ? live->lifecycle.build : 255).c_str(),
               runtime.frozenWindowBuildActive() ? 1 : 0,
               static_cast<unsigned>(runtime.frozenWindowBuildGrain()),
               holders[0].eligible ? 1 : 0, holders[0].canProgress ? 1 : 0,
               holders[0].hasDeadline ? 1 : 0, holders[0].handoffLeadTicks,
               holders[1].eligible ? 1 : 0, holders[1].canProgress ? 1 : 0,
               holders[1].hasDeadline ? 1 : 0, holders[1].handoffLeadTicks,
               runtime.coverageBuildNeedsService() ? 1 : 0, runtime.ownsCoverageSource() ? 1 : 0,
               pending0 != nullptr && pending0->active ? 1 : 0,
               pending1 != nullptr && pending1->active ? 1 : 0,
               loop.publishedSnapshotIsCurrent() ? 1 : 0);
      history += trace;
      history += " || ";
      if (atBoundary) {
        // The boundary is served by the successor prepared for it, adopted exactly here: the bar
        // entered is the one whose window was complete before the playhead reached it.
        const RuntimePlaybackViewSet* liveSet = runtime.viewSetIfPrewarmed();
        TEST_ASSERT_NOT_NULL(liveSet);
        const RuntimePlaybackView& adopted = liveSet->views[liveSet->lifecycle.current];
        TEST_ASSERT_TRUE_MESSAGE(adopted.complete && adopted.coverage.contains(phase),
                                 (std::string(trace) + " HISTORY " + history).c_str());
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(phase, adopted.coverage.startPhase,
                                         (std::string(trace) + " HISTORY " + history).c_str());
      } else {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(upcoming, readyTargetStart(),
                                         (std::string(trace) + " HISTORY " + history).c_str());
      }
    }
  }
  (void)set;
  (void)servedViewAdoptions;

  TEST_ASSERT_TRUE_MESSAGE(PlaybackPathRows::countOf("build_complete") >= 2,
      "the frozen Record-return lane must name every completed successor, not just one");
  // The receipt's **row** is a cadence-bounded sample (one in every declared interval), so this window's few completion
  // calls cannot assert it deterministically; its emission at the frozen owner's terminals is pinned by
  // `test_frozen_record_return_terminals_report_the_slots_readiness_receipt` instead.

  // The accumulated stream is the whole publication gap: every committed row exactly once, in order,
  // bar by bar, with the wrap re-articulating bar 1's own rows.
  std::vector<ExpectedRow> want;
  for (uint32_t bar = 0; bar < kBars; ++bar) {
    want.insert(want.end(), expected[bar].begin(), expected[bar].end());
  }
  want.insert(want.end(), expected[0].begin(), expected[0].end());
  char trace[1024];
  size_t written = 0;
  for (size_t i = 0; i < emitted.size() && written + 32 < sizeof(trace); ++i) {
    written += static_cast<size_t>(snprintf(trace + written, sizeof(trace) - written, "[%u %s %u]",
                                            emitted[i].tick, emitted[i].isNoteOn() ? "on" : "off",
                                            emitted[i].data.noteData.note));
  }
  trace[written] = '\0';
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(want.size()),
                                   static_cast<uint32_t>(emitted.size()), trace);
  for (size_t i = 0; i < want.size(); ++i) {
    TEST_ASSERT_EQUAL_UINT32(want[i].tick, emitted[i].tick);
    TEST_ASSERT_EQUAL(want[i].on, emitted[i].isNoteOn());
    TEST_ASSERT_EQUAL_UINT8(want[i].note, emitted[i].data.noteData.note);
  }

  // The measured per-bar cost, recorded rather than inferred: the turns the **selected** owner needed
  // for that bar's successor, the rows that bar actually holds, and the largest single-turn fill
  // advance. The numbers are asserted as *bounds*, never as a timing guarantee: the supported
  // service-gap envelope stays S7.5a's (the packet's own limit).
  for (uint32_t bar = 0; bar < kBars; ++bar) {
    printf("S7.5b.5 boundary=%u rows=%zu turns=%u maxFillDelta=%u rowsPerTurnBound=%zu "
           "recordTurns=%zu competitorTurns=%zu snapshotTurns=%zu persistenceTurns=%zu emitted=%zu\n",
           bar, expected[bar].size(), perBarTurns[bar], perBarMaxFillDelta[bar],
           (expected[bar].size() + MidiServiceDrain::kCanonicalPrefixInputQuantum - 1) /
               MidiServiceDrain::kCanonicalPrefixInputQuantum,
           recordTurns, competitorTurns, snapshotTurns, persistenceTurns, emitted.size());
    char barDetail[256];
    const size_t ownFillGrains =
        (expected[bar].size() + MidiServiceDrain::kCanonicalPrefixInputQuantum - 1) /
        MidiServiceDrain::kCanonicalPrefixInputQuantum;
    snprintf(barDetail, sizeof(barDetail),
             "bar %u: rows=%zu turns=%u maxFillDelta=%u ownFillGrains=%zu",
             bar, expected[bar].size(), perBarTurns[bar], perBarMaxFillDelta[bar], ownFillGrains);
    // One selected turn advances at most one fill quantum (and its own bar can never need fewer
    // turns than its own rows require).
    TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(
        static_cast<uint32_t>(MidiServiceDrain::kCanonicalPrefixInputQuantum),
        perBarMaxFillDelta[bar], barDetail);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(ownFillGrains),
                                                perBarTurns[bar], barDetail);
  }
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, static_cast<uint32_t>(recordTurns),
      "the Record-return demand must be served by the selected owner");
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, static_cast<uint32_t>(competitorTurns),
      "the competing eligible holder must still be served (no starvation, no bypassed policy)");
}

// S7.5b.5 packet oracle 7: the Record-to-overdub interleaving with a **source-generation transition**.
// An accepted overdub starts while a frozen-range successor is mid-build, and the interval's content
// revision is then superseded (what the overdub's own committed wrap / a real undo does to it). The
// old-identity candidate must not publish or be adopted, the last complete valid view must keep
// serving, the accepted overdub must keep its content and its own continuations, and preparation must
// resume against the valid post-transition source with the committed take still emitted exactly once
// per row across every boundary.
void test_record_return_lookahead_rejects_a_superseded_candidate_mid_overdub() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  Loop& loop = t.getLoop(0);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  constexpr uint32_t kBar = Config::TICKS_PER_BAR;
  const uint32_t loopLength = 2 * kBar;
  if (runtime.viewSetIfPrewarmed() == nullptr) {
    TEST_ASSERT_TRUE(runtime.prewarmViews(loop.companionRowHorizonBound(),
                                          loop.fullTakePlaybackViewAdmissible()));
  }
  t.startRecording(100);
  t.noteOn(1, 60, 100, 100 + 12);   // bar 1
  t.noteOff(1, 60, 0, 100 + 80);
  t.noteOn(1, 62, 100, 100 + kBar + 12);  // bar 2
  t.noteOff(1, 62, 0, 100 + kBar + 80);
  loop.loopLengthTicks = loopLength;
  const PlaybackViewSourceIdentity recordingSource{
      loop.playbackRevision, TrackCaptureStopTestAccess::playbackGenerationOf(t),
      loop.publishedSnapshotIdentity()};
  for (size_t grain = 0; grain < 64; ++grain) {
    runtime.serviceFirstRecordingBar(loop, recordingSource, kBar, 1);
  }
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, loopLength, loopLength);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    TrackCaptureStopTestAccess::closureGrain(t));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::stagingGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::normalizationGrain));
  TEST_ASSERT_TRUE(loop.prewarmActiveOverdubTransferHorizon());
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::passTransferGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::publicationGrain));
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPublished, TrackCaptureStopTestAccess::phase(t));
  TEST_ASSERT_TRUE(loop.pendingSnapshotSessionExitQueued());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot));
  TEST_ASSERT_TRUE(runtime.hasBoundRecordReturnWindowForPendingJob());

  t.forceSetState(TRACK_PLAYING);
  TrackCaptureStopTestAccess::unmute(t);
  TrackCaptureStopTestAccess::selectSlot(t, 0);
  TrackCaptureStopTestAccess::resetProjectionCycle(t);
  loop.lastTickInLoop = 0;
  loop.nextEventIndex = 0;
  MidiSendCapture::reset();
  const PlaybackViewSourceIdentity launchSource{
      loop.playbackRevision, TrackCaptureStopTestAccess::playbackGenerationOf(t),
      loop.publishedSnapshotIdentity()};
  TEST_ASSERT_NOT_NULL(runtime.adoptFirstRecordingBarReturnWindow(loop, launchSource, loopLength, kBar / 2));

  // The interval's first bar is served before the interleaving: the adoption rule is "already heard
  // live", so bar 1's rows belong to the pre-transition cycle and the transition itself must not
  // re-emit, duplicate or lose anything.
  MidiSendCapture::reset();
  t.playMidiEvents(kBar / 2, true);
  assertExactlyOnePlaybackNotePair(60, 12, 80);
  const size_t servedBeforeTransition = MidiSendCapture::count();

  MidiServiceDrain::PlaybackHolderCursor trackCursor{};
  MidiServiceDrain::PlaybackHolderCursor holderCursors[Config::NUM_TRACKS] = {};
  const auto ownerTurn = [&](uint32_t phase) {
    return stepWindowOwnerCadence(trackCursor, holderCursors, phase,
                                  PlaybackDeadlineClock{phase, 1});
  };
  const auto readyView = [&]() -> const RuntimePlaybackView* {
    const RuntimePlaybackViewSet* set = runtime.viewSetIfPrewarmed();
    if (set == nullptr || !runtime.frozenRecordWindowReady()) return nullptr;
    return &set->views[set->lifecycle.ready];
  };

  // (1) One selected owner turn: the bar-2 successor is mid-build, not complete.
  const uint32_t oldRevision = loop.playbackRevision;
  TEST_ASSERT_EQUAL_UINT8(0, ownerTurn(kBar - 1));
  TEST_ASSERT_TRUE_MESSAGE(runtime.frozenWindowBuildActive(),
                           "the successor build must be in flight before the overdub");
  TEST_ASSERT_FALSE_MESSAGE(runtime.frozenRecordWindowReady(),
                            "an in-flight successor is not a ready successor");

  // (2) The accepted overdub starts while that build is in flight: accepted, never gated by lookahead.
  t.startOverdubbing(700);
  TEST_ASSERT_TRUE_MESSAGE(t.isOverdubbing(), "lookahead preparation must not block overdub START");
  const auto readyTargetStart = [&]() -> uint32_t {
    const RuntimePlaybackView* ready = readyView();
    return ready == nullptr ? UINT32_MAX : ready->coverage.startPhase;
  };
  t.noteOn(1, 64, 100, 712);
  t.noteOff(1, 64, 0, 744);
  const size_t overdubRows = loop.capture.store.size();
  TEST_ASSERT_GREATER_THAN_UINT32(0, static_cast<uint32_t>(overdubRows));

  // (3) The content revision is superseded under the in-flight candidate.
  ++loop.playbackRevision;
  TEST_ASSERT_NOT_EQUAL_UINT32(oldRevision, loop.playbackRevision);
  (void)ownerTurn(kBar - 1);
  TEST_ASSERT_FALSE_MESSAGE(runtime.frozenRecordWindowReady(),
                            "a candidate bound to a superseded revision must not publish");
  TEST_ASSERT_FALSE_MESSAGE(runtime.frozenWindowBuildActive(),
                            "the superseded in-flight candidate must be discarded, not continued");
  uint8_t servingSuperseded = RuntimePlaybackViewLifecycle::kNoView;
  for (uint8_t i = 0; i < RuntimePlaybackViewConfig::kViewCount; ++i) {
    const RuntimePlaybackViewSet* set = runtime.viewSetIfPrewarmed();
    TEST_ASSERT_NOT_NULL(set);
    const RuntimePlaybackView& view = set->views[i];
    if (!view.complete || view.source.playbackRevision != oldRevision) continue;
    // The bound bar-1 return window is the **last complete valid view**, and the contract keeps it
    // serving until a matching replacement is adopted. Any *other* view carrying the superseded
    // revision would be a candidate the transition should have refused.
    TEST_ASSERT_TRUE_MESSAGE(view.firstRecordReturnWindow,
                             "a superseded-revision candidate survived the transition");
    TEST_ASSERT_TRUE(view.published);
    servingSuperseded = i;
  }
  // The last complete valid view keeps serving, and the accepted overdub keeps its content.
  const RuntimePlaybackViewSet* served = runtime.viewSetIfPrewarmed();
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(servingSuperseded, served->lifecycle.current,
                                  "the retained bar-1 return window must still be the served view");
  TEST_ASSERT_TRUE(served->views[served->lifecycle.current].complete);
  TEST_ASSERT_TRUE(served->views[served->lifecycle.current].coverage.contains(kBar - 1));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(overdubRows),
                           static_cast<uint32_t>(loop.capture.store.size()));
  TEST_ASSERT_TRUE(t.isOverdubbing());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());

  // (4) Preparation resumes against the valid post-transition source.
  size_t resumedTurns = 0;
  while (resumedTurns < 256 &&
         !(readyView() != nullptr && readyView()->source.playbackRevision == loop.playbackRevision)) {
    (void)ownerTurn(kBar - 1);
    ++resumedTurns;
  }
  TEST_ASSERT_NOT_NULL_MESSAGE(readyView(), "preparation must resume after the transition");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(loop.playbackRevision, readyView()->source.playbackRevision,
                                   "the resumed successor must carry the valid source identity");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(kBar, readyView()->coverage.startPhase,
                                   "the resumed successor serves the upcoming bar");

  // (5) Exact MIDI through the resumed boundary. The transport is put back in the state this lane is
  // defined for (`PlaybackMidiTarget::ActiveSlot` serves it only while `isPlaying()`; a live
  // OVERDUBBING session keeps its own capture/bridge monitoring lane), so the claim here is the one
  // the packet makes -- after the transition the committed take is emitted exactly once per row
  // across every boundary -- and a live overdub that *spans* the gap is recorded as a named limit
  // rather than silently claimed through this lane.
  t.forceSetState(TRACK_PLAYING);
  TrackCaptureStopTestAccess::selectSlot(t, 0);
  std::vector<MidiEvent> stream;
  size_t emittedBase = 0;
  std::string frameHistory;
  for (size_t i = 0; i < servedBeforeTransition; ++i) {
    stream.push_back(MidiSendCapture::at(emittedBase++));
  }
  for (uint32_t tick = kBar - 64; tick <= loopLength + kBar / 2; tick += 64) {
    const uint32_t phase = IntervalProjection::tickPhaseInProjectionCycle(tick, 0, loopLength);
    const uint32_t upcoming = ((phase / kBar) + 1) * kBar;
    const uint32_t target = upcoming >= loopLength ? 0u : upcoming;
    for (size_t turn = 0; turn < 256; ++turn) {
      const RuntimePlaybackView* ready = readyView();
      if (ready != nullptr && ready->coverage.startPhase == target) break;
      (void)ownerTurn(phase == 0 ? loopLength - 1 : phase);
    }
    TEST_ASSERT_TRUE_MESSAGE(runtime.frozenRecordWindowReady(),
                             "a complete successor must be ready before every boundary");
    TEST_ASSERT_FALSE(runtime.frozenWindowBuildActive());
    TrackCaptureStopTestAccess::resetProjectionCycle(t);
    loop.nextEventIndex = 0;
    {
      const RuntimePlaybackViewSet* live = runtime.viewSetIfPrewarmed();
      const auto viewOne = [&](uint8_t index) {
        if (live == nullptr || index >= live->views.size()) return std::string("--");
        const RuntimePlaybackView& v = live->views[index];
        char one[96];
        snprintf(one, sizeof(one), "%u/%d/%u/%u/%d/%d", static_cast<unsigned>(index),
                 v.complete ? 1 : 0, v.coverage.startPhase, v.coverage.lengthTicks,
                 v.published ? 1 : 0, v.firstRecordReturnWindow ? 1 : 0);
        return std::string(one);
      };
      char one[320];
      snprintf(one, sizeof(one),
               "f tick=%u ph=%u readyStart=%u cur=[%s] last=%u idx=%u next=%u emitted=%zu | ",
               tick, phase, readyTargetStart(),
               viewOne(live ? live->lifecycle.current : 255).c_str(), loop.lastTickInLoop,
               static_cast<unsigned>(loop.nextEventIndex),
               static_cast<unsigned>(loop.nextEventIndex), stream.size());
      frameHistory += one;
      char diag[320];
      snprintf(diag, sizeof(diag),
               "ev=%u rev=%u viewRev=%u gen=%u viewGen=%u audible=%d muted=%d barrier=%d src=%d "
               "obstacle=%d | ",
               live != nullptr && live->lifecycle.current < live->views.size()
                   ? static_cast<unsigned>(live->views[live->lifecycle.current].storage.eventCount) : 0u,
               static_cast<unsigned>(loop.playbackRevision),
               live != nullptr ? static_cast<unsigned>(live->views[live->lifecycle.current].source.playbackRevision) : 0u,
               static_cast<unsigned>(TrackCaptureStopTestAccess::playbackGenerationOf(t)),
               live != nullptr ? static_cast<unsigned>(live->views[live->lifecycle.current].source.sourceGeneration) : 0u,
               t.isAudiblePlaybackActive() ? 1 : 0, t.isMuted() ? 1 : 0,
               TrackCaptureStopTestAccess::outputBarrier(t) ? 1 : 0,
               t.hasAudiblePlaybackSource(0) ? 1 : 0,
               t.hasPendingDeferredOverdubStop() ? 1 : 0);
      frameHistory += diag;
    }
    t.playMidiEvents(tick, true);
    while (emittedBase < MidiSendCapture::count()) stream.push_back(MidiSendCapture::at(emittedBase++));
  }
  struct CommittedRow {
    uint32_t tick;
    bool on;
    uint8_t note;
  };
  std::vector<CommittedRow> committed;
  uint32_t overdubOns = 0;
  uint32_t overdubOffs = 0;
  for (const MidiEvent& event : stream) {
    if (event.data.noteData.note == 64) {
      if (event.isNoteOn()) ++overdubOns; else ++overdubOffs;
      continue;
    }
    if (event.isNoteOn() || event.isNoteOff()) {
      committed.push_back(CommittedRow{event.tick, event.isNoteOn(), event.data.noteData.note});
    }
  }
  // Committed expectation: bar 1's pair, bar 2's pair, then the wrap re-articulating bar 1's pair.
  const CommittedRow want[] = {
      {12, true, 60}, {80, false, 60},
      {kBar + 12, true, 62}, {kBar + 80, false, 62},
      {12, true, 60}, {80, false, 60},
  };
  char streamTrace[512];
  size_t written = 0;
  for (size_t i = 0; i < committed.size() && written + 24 < sizeof(streamTrace); ++i) {
    written += static_cast<size_t>(snprintf(streamTrace + written, sizeof(streamTrace) - written,
                                            "[%u %s %u]", committed[i].tick,
                                            committed[i].on ? "on" : "off", committed[i].note));
  }
  streamTrace[written] = '\0';
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(sizeof(want) / sizeof(want[0])),
                                   static_cast<uint32_t>(committed.size()),
                                   (std::string(streamTrace) + " HISTORY " + frameHistory).c_str());
  for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); ++i) {
    TEST_ASSERT_EQUAL_UINT32(want[i].tick, committed[i].tick);
    TEST_ASSERT_EQUAL(want[i].on, committed[i].on);
    TEST_ASSERT_EQUAL_UINT8(want[i].note, committed[i].note);
  }
  // The committed lane serves **committed** content: the live capture's own rows are the capture
  // lane's business (a live overdub's monitoring is not this lane's source), and the lookahead may
  // never substitute them for the take. The accepted overdub's rows are still the interval's.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, overdubOns + overdubOffs,
      "the lookahead must serve committed content, not the in-flight capture's rows");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(overdubRows),
                                   static_cast<uint32_t>(loop.capture.store.size()),
                                   "the accepted overdub keeps its content through the transition");
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
}

// S7.5b.5 interleaving: an accepted overdub begins while the Record-range successor
// builder is in flight. The bounded playback owner remains serviceable and the live
// overdub's own MIDI rows are not lost behind the immutable base preparation.
void test_record_successor_preparation_does_not_block_live_overdub() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  Loop& loop = t.getLoop(0);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  if (runtime.viewSetIfPrewarmed() == nullptr) {
    TEST_ASSERT_TRUE(runtime.prewarmViews(loop.companionRowHorizonBound(),
                                          loop.fullTakePlaybackViewAdmissible()));
  }
  t.startRecording(100);
  t.noteOn(1, 60, 100, 112);
  t.noteOff(1, 60, 0, 180);
  t.noteOn(1, 62, 100, 900);
  t.noteOff(1, 62, 0, 1000);
  loop.loopLengthTicks = Config::TICKS_PER_BAR * 2;
  const PlaybackViewSourceIdentity recordingSource{
      loop.playbackRevision, TrackCaptureStopTestAccess::playbackGenerationOf(t),
      loop.publishedSnapshotIdentity()};
  for (size_t grain = 0; grain < 64; ++grain) {
    runtime.serviceFirstRecordingBar(loop, recordingSource, Config::TICKS_PER_BAR, 1);
  }
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(
      t, 0, loop.loopLengthTicks, loop.loopLengthTicks);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    TrackCaptureStopTestAccess::closureGrain(t));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::stagingGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::normalizationGrain));
  TEST_ASSERT_TRUE(loop.prewarmActiveOverdubTransferHorizon());
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::passTransferGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::publicationGrain));
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPublished, TrackCaptureStopTestAccess::phase(t));

  t.forceSetState(TRACK_PLAYING);
  TrackCaptureStopTestAccess::unmute(t);
  TrackCaptureStopTestAccess::resetProjectionCycle(t);
  loop.lastTickInLoop = 0;
  t.playMidiEvents(90, true);  // Adopt the complete first-bar return window.
  (void)t.stepPlaybackViewMaintenance(Config::TICKS_PER_BAR - 1, nullptr, true, {});
  TEST_ASSERT_TRUE(runtime.frozenWindowBuildActive());

  // DEC-078: the Record stop owner is busy, but overdub START is still accepted.
  t.startOverdubbing(700);
  TEST_ASSERT_TRUE_MESSAGE(t.isOverdubbing(), "lookahead preparation must not block overdub START");
  t.noteOn(1, 64, 100, 710);
  t.noteOff(1, 64, 0, 740);
  TEST_ASSERT_GREATER_THAN_UINT32(0, loop.capture.store.size());

  size_t turns = 0;
  for (; turns < 512 && !runtime.frozenRecordWindowReady(); ++turns) {
    (void)t.stepPlaybackViewMaintenance(Config::TICKS_PER_BAR - 1, nullptr, true, {});
  }
  TEST_ASSERT_TRUE_MESSAGE(runtime.frozenRecordWindowReady(),
                           "the selected owner must resume/finish successor work during live overdub");
  const RuntimePlaybackViewSet* set = runtime.viewSetIfPrewarmed();
  TEST_ASSERT_NOT_NULL(set);
  const RuntimePlaybackView& successor = set->views[set->lifecycle.ready];
  TEST_ASSERT_EQUAL_UINT32(loop.playbackRevision, successor.source.playbackRevision);
  TEST_ASSERT_EQUAL_UINT32(TrackCaptureStopTestAccess::playbackGenerationOf(t),
                           successor.source.sourceGeneration);

  // Owner service must leave the newly accepted live overdub content intact.
  TEST_ASSERT_EQUAL_UINT32(2, loop.capture.store.size());
  TEST_ASSERT_TRUE(t.isOverdubbing());
}

// S4f.19 multiple sounding tracks: two tracks each own an unpublished deferred interval and
// each serve their own bound window independently. This is S4f.19's multi-track property (the
// committed-bridge multi-track case belongs to task 2.4).
void test_two_tracks_each_serve_their_own_deferred_window() {
  Track* tracks[2] = {&trackManager.getTrack(0), &trackManager.getTrack(1)};
  uint8_t notes[2] = {60, 62};
  LoopPlaybackRuntime* runtimes[2] = {nullptr, nullptr};
  for (size_t i = 0; i < 2; ++i) {
    Track& t = *tracks[i];
    prepareSlot(t, 0, false);
    Loop& loop = t.getLoop(0);
    auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
    runtimes[i] = &runtime;
    if (runtime.viewSetIfPrewarmed() == nullptr) {
      TEST_ASSERT_TRUE(runtime.prewarmViews(loop.companionRowHorizonBound(),
                                            loop.fullTakePlaybackViewAdmissible()));
    }
    t.startRecording(100);
    t.noteOn(1, notes[i], 100, 112);
    t.noteOff(1, notes[i], 0, 180);
    loop.loopLengthTicks = 768;
    const PlaybackViewSourceIdentity recordingSource{
        loop.playbackRevision, TrackCaptureStopTestAccess::playbackGenerationOf(t),
        loop.publishedSnapshotIdentity()};
    for (size_t grain = 0; grain < 32; ++grain) {
      runtime.serviceFirstRecordingBar(loop, recordingSource, 768, 1);
    }
    auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t, 0, 768, 768);
    TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                      TrackCaptureStopTestAccess::closureGrain(t));
    TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::audibleSource(t, 0));
    TEST_ASSERT_TRUE(runtime.hasServableDeferredRecordView());
    TEST_ASSERT_TRUE(job.admission.pending);
  }

  MidiSendCapture::reset();
  for (size_t i = 0; i < 2; ++i) {
    Track& t = *tracks[i];
    t.forceSetState(TRACK_PLAYING);
    TrackCaptureStopTestAccess::unmute(t);
    TrackCaptureStopTestAccess::selectSlot(t, 0);
    TrackCaptureStopTestAccess::resetProjectionCycle(t);
    t.getLoop(0).lastTickInLoop = 0;
    t.getLoop(0).nextEventIndex = 0;
    t.playMidiEvents(90, true);
  }
  size_t note60 = 0;
  size_t note62 = 0;
  for (size_t i = 0; i < MidiSendCapture::count(); ++i) {
    const MidiEvent& event = MidiSendCapture::at(i);
    if (!event.isNoteOn()) continue;
    if (event.data.noteData.note == 60) ++note60;
    if (event.data.noteData.note == 62) ++note62;
  }
  TEST_ASSERT_EQUAL_UINT32(1, note60);
  TEST_ASSERT_EQUAL_UINT32(1, note62);
}

// S4f.17 S17.1: a Record stop admitted through the **production** admission binds a job with
// the record receipts (raw range, held notes, job geometry) and the existing worker drives it to
// exactly-once publication and retirement. This is the gate that lets S17.2 route the stop
// entry points through the drive.
void test_record_stop_admits_through_the_common_drive() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  Loop& loop = t.getLoop(0);
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  if (runtime.viewSetIfPrewarmed() == nullptr) {
    TEST_ASSERT_TRUE(runtime.prewarmViews(loop.companionRowHorizonBound(),
                                          loop.fullTakePlaybackViewAdmissible()));
  }
  t.startRecording(100);
  // Left held: the held-note receipt and the terminal closure Off are part of the bind.
  t.noteOn(1, 60, 100, 112);
  const uint64_t interval = TrackCaptureStopTestAccess::interval(t);
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(TrackCaptureStopTestAccess::trackHeldNotes(t)));

  // Production admission: no controlled bind, no manual receipt construction.
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::admitRecordStop(t, 768, interval));
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(TrackCaptureStopTestAccess::jobs(t)));
  auto& job = TrackCaptureStopTestAccess::head(t);
  TEST_ASSERT_EQUAL(CapturePhase::Record, job.mode);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.ready());
  TEST_ASSERT_TRUE(job.admission.pending);
  TEST_ASSERT_EQUAL_UINT64(interval, job.interval);
  TEST_ASSERT_EQUAL(PendingStopPhase::ReservedClosure, job.admission.phase);
  TEST_ASSERT_EQUAL_UINT32(job.pendingNotes.size(),
                           static_cast<uint32_t>(job.admission.remainingClosureGrains));
  // The held-note receipt moved to the job, so a successor cannot rewrite it.
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(TrackCaptureStopTestAccess::trackHeldNotes(t)));
  TEST_ASSERT_GREATER_THAN_UINT32(0, job.loopLengthTicks);
  // The geometry receipt is the job's own.
  TEST_ASSERT_TRUE(job.hasRecordGeometry);
  TEST_ASSERT_EQUAL(job.loopId, job.recordGeometryLoopId);
  const uint32_t finalLength = job.loopLengthTicks;
  // The execution boundary stamps the interval's final geometry once (S17.2 does this in the
  // stop entry point; here the fixture supplies it so the publication geometry is well defined).
  loop.loopLengthTicks = finalLength;

  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::closureGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::stagingGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::normalizationGrain));
  TEST_ASSERT_TRUE(loop.prewarmActiveOverdubTransferHorizon());
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::passTransferGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t, TrackCaptureStopTestAccess::publicationGrain));
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPublished, TrackCaptureStopTestAccess::phase(t));
  // Exactly-once publication: one pass, one RecordPassAdded entry, one persistence request.
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::hasRecordPass(t, 0));
  TEST_ASSERT_EQUAL(1, TrackUndo::getUndoCount(t));
  TEST_ASSERT_EQUAL(UndoEntryKind::RecordPassAdded, t.getGlobalUndoStack().entries.back().kind);
  TEST_ASSERT_TRUE(StorageManagerInternal::currentSetLoopSlotDirty[0][0]);
  t.forceSetState(TRACK_STOPPED); // Controlled admission above bypasses the normal transport edge.
  driveStopToRetirement(t);
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::presentationActive(t, 0));
  TEST_ASSERT_EQUAL(0, TrackCaptureStopTestAccess::stopQueueDepth(t));
}

// S4f.20 parity -- the session-exit queue the inline stop owned. `Loop::commitPendingCapturePass`
// ends by queuing the interval's session-exit snapshot publication, and that queue is the only
// enumeration of the **committed Record pass** into the pending box. The pass does not exist at the
// stop edge (the job owns it until this grain), so the deferred publication must own the queue.
// Without it the box publishes the retained live-record candidate alone: that candidate's content
// stops at the completed-bar frontier while its declared length is handed off to the sealed length,
// and the successor's exit stamps the root current -- the device shape behind
// `captures/session_20260929_225529.log` (Record pass's last bar missing from the display until an
// overdub undo made the root non-current).
void test_deferred_record_publication_queues_the_record_session_exit_snapshot() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_PLAYING);
  Loop& loop = t.getLoop(0);
  // Two bars recorded, stopped inside bar 2: the live-record candidate can only have covered the
  // *completed* bar (768), one bar short of the sealed length (1536).
  t.setActiveLoopIndex(0);
  t.startRecording(0);
  t.noteOn(1, 60, 100, 12);
  t.noteOff(1, 60, 0, 96);
  t.noteOn(1, 62, 100, 800);
  t.noteOff(1, 62, 0, 900);
  loop.loopLengthTicks = Config::TICKS_PER_BAR * 2;
  // Exercise the selected track's existing Snapshot idle owner while RECORDING;
  // never call Loop::stepPendingSnapshotPublish directly in this integration oracle.
  clockManager.assignCurrentTickSilently(Config::TICKS_PER_BAR);
  size_t preStopSnapshotTurns = 0;
  uint32_t preStopAppliedTicks = 0;
  uint32_t preStopMaxTurnAdvance = 0;
  for (; preStopSnapshotTurns < 64 &&
         loop.liveRecordAppliedCompletedTicks() < Config::TICKS_PER_BAR;
       ++preStopSnapshotTurns) {
    const uint32_t appliedBefore = loop.liveRecordAppliedCompletedTicks();
    t.processDeferredIdleMaintenance(0, false,
        MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
    const uint32_t appliedAfter = loop.liveRecordAppliedCompletedTicks();
    if (appliedAfter > appliedBefore) {
      const uint32_t delta = appliedAfter - appliedBefore;
      if (delta > preStopMaxTurnAdvance) preStopMaxTurnAdvance = delta;
    }
  }
  preStopAppliedTicks = loop.liveRecordAppliedCompletedTicks();
  TEST_ASSERT_GREATER_THAN_UINT32(0, preStopSnapshotTurns);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(Config::TICKS_PER_BAR, preStopAppliedTicks,
                                   "the bounded Snapshot owner should apply the completed live-record bar");
  // Each owner turn advances at most the existing bounded construction unit -- here one completed
  // bar of live-record content -- so the progress is grained rather than a whole-loop build.
  TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(Config::TICKS_PER_BAR, preStopMaxTurnAdvance,
                                           "a Snapshot owner turn must stay within its bounded unit");
  // The selected root is not marked current for incomplete content.
  TEST_ASSERT_NULL(loop.publishedSnapshot().get());
  TEST_ASSERT_FALSE_MESSAGE(loop.pendingSnapshotSessionExitQueued(),
                            "live-record progress is not a session-exit publication");
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());

  stop(t, false, Config::TICKS_PER_BAR * 2);
  TEST_ASSERT_EQUAL_UINT32(Config::TICKS_PER_BAR * 2, loop.loopLengthTicks);
  advanceToPhase(t, PendingStopPhase::RecordPublished);
  TEST_ASSERT_TRUE_MESSAGE(loop.passes.hasRecordPass(),
                           "precondition: the deferred publication commits a Record pass");

  // The contract this fixture exists for.
  TEST_ASSERT_TRUE_MESSAGE(loop.pendingSnapshotSessionExitQueued(),
                           "the deferred Record publication must queue the record session exit");
  TEST_ASSERT_TRUE_MESSAGE(loop.pendingSnapshotQueuedPassCount() > 0,
                           "the committed Record pass must be enumerated, not left to the "
                           "completed-bar-prefix candidate");
  // The remaining construction work at the stop edge, in the owner's own terms: the enumeration the
  // session exit owes. Pre-stop progress has already discharged it for this pass.
  const size_t preStopQueuedPasses = loop.pendingSnapshotQueuedPassCount();
  const size_t preStopAppliedPasses = loop.pendingSnapshotAppliedPassCount();

  // Only a target-qualified row emitted by the actual final owner turn confirms publication.
  const uint32_t beforePublicationRevision = loop.publishedSnapshotRevision();
  SnapshotGateRows::reset();
  size_t drain = 0;
  while (loop.hasPendingSnapshotPublish() && drain < 10000) {
    t.processDeferredIdleMaintenance(0, false,
        MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
    ++drain;
  }
  TEST_ASSERT_FALSE_MESSAGE(loop.hasPendingSnapshotPublish(),
                            "the queued session exit must drain to a published root");
  TEST_ASSERT_TRUE_MESSAGE(loop.publishedSnapshotIsCurrent(),
                           "the root published from the retained candidate must cover the Record pass");
  TEST_ASSERT_EQUAL_UINT32(Config::TICKS_PER_BAR * 2, loop.publishedSnapshot()->loopLengthTicks());
  // Content, not just currency: the note beyond the completed-bar frontier must be in the root.
  SessionMidiEventVec events;
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(events, 0, loop.loopLengthTicks));
  uint32_t barOneNote = 0;
  uint32_t lastBarNote = 0;
  for (const MidiEvent& row : events) {
    if (row.isNoteOn() && row.data.noteData.note == 60) ++barOneNote;
    if (row.isNoteOn() && row.data.noteData.note == 62) ++lastBarNote;
  }
  TEST_ASSERT_EQUAL_UINT32(1, barOneNote);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, lastBarNote,
                                   "the published root lost the take's last-bar note");
  size_t confirmations = 0;
  for (size_t i = 0; i < SnapshotGateRows::count && i < 64; ++i) {
    const auto& row = SnapshotGateRows::rows[i];
    if (row.track == 0 && row.selectedSlot == 0 &&
        row.publishedRevision != beforePublicationRevision &&
        (row.verdicts & ((1u << 0) | (1u << 1) | (1u << 2))) == 0 &&
        (row.verdicts & (1u << 4)) != 0) ++confirmations;
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, confirmations,
      "the final production Snapshot owner turn must report the target's complete published root");
  const size_t rowsAfterPublish = SnapshotGateRows::count;
  t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(rowsAfterPublish, SnapshotGateRows::count,
      "a duplicate owner turn must not repeat the publication confirmation");
  TEST_ASSERT_GREATER_THAN_UINT32(0, drain);

  // Identical no-prework control: live-record Snapshot work remains eligible, but
  // does not run until after stop. Compare residual owner-turn work and root content.
  Track& controlTrack = trackManager.getTrack(1);
  prepareSlot(controlTrack, 0, false);
  controlTrack.forceSetState(TRACK_PLAYING);
  Loop& controlLoop = controlTrack.getLoop(0);
  controlTrack.setActiveLoopIndex(0);
  controlTrack.startRecording(0);
  controlTrack.noteOn(1, 60, 100, 12);
  controlTrack.noteOff(1, 60, 0, 96);
  controlTrack.noteOn(1, 62, 100, 800);
  controlTrack.noteOff(1, 62, 0, 900);
  controlLoop.loopLengthTicks = Config::TICKS_PER_BAR * 2;
  // The control is the identical pass with **no** eligible pre-stop Snapshot turn: its live-record
  // progress has not advanced at the stop edge.
  const uint32_t controlAppliedAtStop = controlLoop.liveRecordAppliedCompletedTicks();
  stop(controlTrack, false, Config::TICKS_PER_BAR * 2);
  advanceToPhase(controlTrack, PendingStopPhase::RecordPublished);
  const size_t controlQueuedPasses = controlLoop.pendingSnapshotQueuedPassCount();
  const size_t controlAppliedPasses = controlLoop.pendingSnapshotAppliedPassCount();
  size_t controlDrain = 0;
  while (controlLoop.hasPendingSnapshotPublish() && controlDrain < 10000) {
    controlTrack.processDeferredIdleMaintenance(0, false,
        MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
    ++controlDrain;
  }
  TEST_ASSERT_FALSE(controlLoop.hasPendingSnapshotPublish());
  TEST_ASSERT_TRUE(controlLoop.publishedSnapshotIsCurrent());
  SessionMidiEventVec controlEvents;
  // A separate Track/Loop publishes its own root; it cannot supply the target receipt.
  TEST_ASSERT_NOT_NULL(SnapshotGateRows::last());
  TEST_ASSERT_EQUAL_UINT8(1, SnapshotGateRows::last()->track);
  TEST_ASSERT_EQUAL_UINT8(0, SnapshotGateRows::last()->selectedSlot);
  TEST_ASSERT_EQUAL_UINT32(controlLoop.publishedSnapshotRevision(),
                           SnapshotGateRows::last()->publishedRevision);
  TEST_ASSERT_TRUE(controlLoop.tryGatherPublishedSnapshotEventsInWindow(
      controlEvents, 0, controlLoop.loopLengthTicks));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(events.size(), controlEvents.size(),
                                   "the control's published root must hold the identical content");
  for (size_t i = 0; i < events.size(); ++i) {
    TEST_ASSERT_EQUAL_UINT32(events[i].tick, controlEvents[i].tick);
    TEST_ASSERT_EQUAL_UINT8(events[i].type, controlEvents[i].type);
    TEST_ASSERT_EQUAL_UINT8(events[i].data.noteData.note, controlEvents[i].data.noteData.note);
  }
  TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(controlDrain, drain,
      "pre-stop progress must leave fewer residual post-stop construction turns");
  // The stop edge clears the live-record progress, so the pre-stop figure is the one captured before it.
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(controlAppliedAtStop, preStopAppliedTicks,
      "the pre-stop path is the one with eligible live-record progress");
  // The measured form the packet asks for: pre-stop progress achieved, the remaining post-stop
  // construction work at the stop edge, the residual owner turns for this path and for the control.
  printf("S7.5b.5 pre-stop appliedTicks=%u turns=%zu maxTurnAdvance=%u queuedPasses=%zu appliedPasses=%zu "
         "drainTurns=%zu | control appliedTicks=%u queuedPasses=%zu appliedPasses=%zu drainTurns=%zu\n",
         preStopAppliedTicks, preStopSnapshotTurns, preStopMaxTurnAdvance, preStopQueuedPasses,
         preStopAppliedPasses, drain, controlAppliedAtStop, controlQueuedPasses, controlAppliedPasses,
         controlDrain);
  // Both paths owe the **identical** session-exit enumeration at the stop edge; what pre-stop
  // progress changes is the residual owner-turn work (`drain` above), not the work's definition.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(controlQueuedPasses),
                                   static_cast<uint32_t>(preStopQueuedPasses),
                                   "the comparison must be over the same remaining work");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(controlAppliedPasses),
                                   static_cast<uint32_t>(preStopAppliedPasses),
                                   "neither path has applied the session-exit enumeration at the stop edge");
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(controlTrack, TrackCaptureStopTestAccess::retirementGrain));
}

// The two short overdub-stop routes -- a clean stop whose frozen staging holds no rows, and the
// STOPPED destination -- skip the CompanionHistory/UndoCollection phases, and with them this
// interval's `OverdubPassAdded` entry: a stopped overdub added no sidebar `U:` level at runtime, and
// the entry appeared only after a reboot's content reconstruction (DEC-088). The deferred publication
// now builds it from the interval's own session-exit receipt (DEC-068) and commits it with the pass
// across one boundary (DEC-063), exactly once per job.
void test_stopped_overdub_stop_publishes_one_undo_entry() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, true);
  t.forceSetState(TRACK_PLAYING);
  t.setActiveLoopIndex(0);
  Loop& loop = t.getLoop(0);
  const size_t undoBefore = TrackUndo::getUndoCount(t);
  const size_t depthBefore = TrackUndo::undoDepthForLoop(t, loop);

  start(t, 0, true, 0, 60);
  clockManager.assignCurrentTickSilently(768);
  t.stopOverdubbingToStopped();  // STOPPED destination -> the route that skipped the entry
  driveStopToRetirement(t);

  TEST_ASSERT_TRUE_MESSAGE(loop.passes.hasRecordPass(),
                           "precondition: the record pass survives the overdub stop");
  TEST_ASSERT_EQUAL_UINT(1, loop.passes.overdubPasses.size());
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      undoBefore + 1, TrackUndo::getUndoCount(t),
      "a stopped overdub must add exactly one global undo entry");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      depthBefore + 1, TrackUndo::undoDepthForLoop(t, loop),
      "the sidebar U: level for this slot must gain the stopped overdub");
  const auto& entries = t.getGlobalUndoStack().entries;
  TEST_ASSERT_EQUAL(UndoEntryKind::OverdubPassAdded, entries.back().kind);
  TEST_ASSERT_EQUAL_UINT8(0, entries.back().slotIndex);
  TEST_ASSERT_EQUAL(loop.passes.overdubPasses.back().id, entries.back().passId);
  // Canonical `OverdubPassAdded` shape (`TrackUndo::pushOverdubPassAdded`): when `passIds` carries a
  // set, its tail is the entry's `passId`, so an undo removes exactly the passes this interval added.
  if (!entries.back().passIds.empty()) {
    TEST_ASSERT_EQUAL(entries.back().passId, entries.back().passIds.back());
    for (const PassId id : entries.back().passIds) {
      TEST_ASSERT_NOT_EQUAL(kInvalidPassId, id);
    }
  }

  // A second stopped overdub on the same slot registers its **own** level. That is the device rule the
  // sidebar must show: `U:02` after one stop and `U:03` after two, not one level that a later stop
  // replaces -- and exactly what a reboot's content reconstruction (DEC-088) already reported.
  t.forceSetState(TRACK_PLAYING);
  start(t, 0, true, 768, 64);
  clockManager.assignCurrentTickSilently(1536);
  t.stopOverdubbingToStopped();
  driveStopToRetirement(t);
  TEST_ASSERT_EQUAL_UINT(2, loop.passes.overdubPasses.size());
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      undoBefore + 2, TrackUndo::getUndoCount(t),
      "the second stopped overdub must add one more level, not replace the first");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      depthBefore + 2, TrackUndo::undoDepthForLoop(t, loop),
      "the sidebar U: level must count both stopped overdubs");
  TEST_ASSERT_EQUAL(loop.passes.overdubPasses.back().id,
                    t.getGlobalUndoStack().entries.back().passId);

  // A superseded causal base must publish its interval's level too. The superseded transfer variant
  // commits the accepted pass with no bridge, and it runs *before* the audible-bridge handoff used to
  // sit -- so the entry has to be prepared (and driven to `Ready`) at the top of the transfer phase.
  // Same fault injection as `deferredPersistence(false, true, false)`: a replaced causal base.
  t.forceSetState(TRACK_PLAYING);
  start(t, 0, true, 1536, 66);
  clockManager.assignCurrentTickSilently(2304);
  t.stopOverdubbingToStopped();
  advanceToPhase(t, PendingStopPhase::AtomicTransfer);
  ++loop.playbackRevision;
  driveStopToRetirement(t);
  TEST_ASSERT_FALSE_MESSAGE(t.hasStopInFlight(),
                            "a superseded job must retire, not park on its undone entry");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      undoBefore + 3, TrackUndo::getUndoCount(t),
      "a superseded stopped overdub must still publish its level");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      depthBefore + 3, TrackUndo::undoDepthForLoop(t, loop),
      "the sidebar U: level must count the superseded stop as well");
  TEST_ASSERT_EQUAL(loop.passes.overdubPasses.back().id,
                    t.getGlobalUndoStack().entries.back().passId);

  // The device's dominant stopped-overdub route: a **clean** stop whose wraps already committed the
  // take (`stop_phase` trail `1,6,10,11`, no `CompanionHistory`/`UndoCollection`; the 3-bar device runs
  // hit it). This stop stages no pass of its own -- no rows -- so the interval's identity comes from
  // its own session-exit receipt: the pass its wrap recorded with `pushOverdubSessionPass`. Publishing
  // that entry is this transfer's whole publication, and it is the level the sidebar used to lose.
  t.forceSetState(TRACK_PLAYING);
  const PassId wrappedPassId = loop.passes.overdubPasses.back().id;
  const size_t passesBefore = loop.passes.overdubPasses.size();
  t.setActiveLoopIndex(0);
  t.startOverdubbing(2304);
  TEST_ASSERT_TRUE(t.isOverdubbing());
  // The wrap commits the take *during* the live session; that is what puts the identity in the
  // interval's own receipt at its stop admission.
  loop.pushOverdubSessionPass(wrappedPassId, EditPassIdList{});
  clockManager.assignCurrentTickSilently(3072);
  t.stopOverdubbingToStopped();
  driveStopToRetirement(t);
  // The stop committed nothing new (its take was already committed by that wrap) ...
  TEST_ASSERT_EQUAL_UINT_MESSAGE(passesBefore, loop.passes.overdubPasses.size(),
                                 "a clean stop stages no pass of its own");
  // ... yet its interval still owns a level, named by the receipt's identity.
  // ... yet its interval still owns a level, named by the receipt's identity.
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      undoBefore + 4, TrackUndo::getUndoCount(t),
      "a clean stop whose wraps committed the take must publish the entry for those identities");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      depthBefore + 4, TrackUndo::undoDepthForLoop(t, loop),
      "the sidebar U: level must gain the clean stop as well");
  TEST_ASSERT_EQUAL(wrappedPassId, t.getGlobalUndoStack().entries.back().passId);
}

// S4f.20 deferred-REVT parity: the publication is the commit, so it queues the same stored
// note-on scan the inline stop queued from `finalizeCommitSideEffects` -- for **its own slot**.
// The queue is slot-qualified because the active index may already hold a successor take, which
// an active-slot read would scan instead (or would find nothing to scan at all).
void test_deferred_record_publication_queues_revts_for_its_own_slot() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  prepareSlot(t, 1, false);
  t.forceSetState(TRACK_PLAYING);
  start(t, 0, false, 0, 60);
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::deferredRevtsPending(t),
                            "nothing may be queued before the pass commits");
  stop(t, false, 768);
  advanceToPhase(t, PendingStopPhase::RecordPublished);
  TEST_ASSERT_TRUE_MESSAGE(t.getLoop(0).passes.hasRecordPass(),
                           "precondition: the deferred publication must commit a Record pass");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, TrackCaptureStopTestAccess::deferredRevtsSlot(t),
                                  "the publication must queue the REVT scan for its own slot");

  // A successor take now owns the active slot. The queued scan must still name slot 0.
  start(t, 1, false, 768, 62);
  TEST_ASSERT_EQUAL_UINT8(1, t.getActiveLoopIndex());
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, TrackCaptureStopTestAccess::deferredRevtsSlot(t),
                                  "a successor take must not redirect the queued scan");
  TrackCaptureStopTestAccess::serviceDeferredRevts(t);
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::deferredRevtsPending(t),
                            "the slot-qualified scan must run to completion");
}

// The accepted-sequence acceptance window is continuously playing by contract, so a take's stored
// note-on grid has to be able to drain while the loop is audible: gating it on a fully idle
// transport left every overlap run reporting `record_stored_revt_missing` against a correct take.
// The queue time already copied the committed chunk ids, so the playing slice is a bounded chunk
// walk. A scan that still needs the full merge must NOT flatten the active passes on a playing
// transport -- that cost is why the gate existed -- so it waits for the idle owner.
void test_playing_record_scan_drains_the_bounded_walk_and_defers_the_merge() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_PLAYING);
  start(t, 0, false, 0, 60);
  stop(t, false, 768);
  advanceToPhase(t, PendingStopPhase::RecordPublished);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::deferredRevtsPending(t));
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::deferredRevtsChunkScan(t),
                           "precondition: the stop's own take queues the bounded chunk walk");
  // The subject here is the *slice*, not the stop flow's own state (pinned by the fixtures above),
  // so place the track in the audible state the accepted-sequence window holds.
  t.forceSetState(TRACK_PLAYING);
  TEST_ASSERT_TRUE_MESSAGE(t.isAudiblePlaybackActive(),
                           "precondition: the slice under test runs while the loop is audible");

  // A merge-requiring scan survives a playing slice untouched.
  TrackCaptureStopTestAccess::setDeferredRevtsScanMode(t, false);
  TrackCaptureStopTestAccess::serviceDeferredRevtsForPlayback(t);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::deferredRevtsPending(t),
                           "the full merge belongs to the idle owner, not to a playing turn");

  // The bounded walk drains on that same playing transport.
  TrackCaptureStopTestAccess::setDeferredRevtsScanMode(t, true);
  TrackCaptureStopTestAccess::serviceDeferredRevtsForPlayback(t);
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::deferredRevtsPending(t),
                            "the bounded chunk walk must drain during playback");
}

// The queued scan borrows the producer's chunk ids, and the publication/retirement that runs before
// the deferred turn releases that directory -- which frees the chunks and makes `appendChunkRefEvent`
// read nothing. On device that surfaced as `Queued REVT note-ons (deferred): 0` with zero `REVT`
// rows on a take that had recorded, published and played. The pin must hold the chunks across the
// deferral and be released when the scan finishes.
void test_queued_revt_scan_pins_its_chunks_across_the_deferral() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_PLAYING);
  start(t, 0, false, 0, 60);
  stop(t, false, 768);
  advanceToPhase(t, PendingStopPhase::RecordPublished);
  const auto ids = TrackCaptureStopTestAccess::deferredRevtChunkIds(t);
  TEST_ASSERT_FALSE_MESSAGE(ids.empty(),
                            "precondition: the fast path copied the take's chunk directory");
  std::vector<uint16_t> pinnedCounts;
  for (uint16_t id : ids) {
    pinnedCounts.push_back(LoopEventStore::chunkReferenceCount(id));
  }
  // The producer releases its own directory first, exactly as publication/retirement does.
  TrackCaptureStopTestAccess::releaseRecordPassDirectory(t, 0);
  for (size_t i = 0; i < ids.size(); ++i) {
    TEST_ASSERT_GREATER_THAN_UINT16(0, LoopEventStore::chunkReferenceCount(ids[i]));
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        pinnedCounts[i] - 1, LoopEventStore::chunkReferenceCount(ids[i]),
        "the borrowed directory must keep the chunk alive after the producer releases it");
  }
  TrackCaptureStopTestAccess::serviceDeferredRevts(t);
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::deferredRevtsPending(t));
  for (size_t i = 0; i < ids.size(); ++i) {
    // Two releases happened by now: the producer's and the pin's.
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        pinnedCounts[i] - 2, LoopEventStore::chunkReferenceCount(ids[i]),
        "the pin must be released when the scan completes");
  }
}

// S4f.20 `invalidateCaches()` parity, proven against the actual consumer contract rather than
// assumed from `Loop::notifyCommittedContentChanged`. The atomic handoff covers the Loop's own
// derived caches, but the inline stop's `Track::invalidateCaches()` also drops the Track-level
// committed-MIDI scratch (`CommittedMidiScratch::isFresh` compares revision + published identity).
// The deferred publication must do the same, for the job's slot.
void test_deferred_record_publication_invalidates_the_track_committed_scratch() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_PLAYING);
  start(t, 0, false, 0, 60);
  TrackCaptureStopTestAccess::markCommittedScratchFresh(t);
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::committedScratchInvalidated(t));
  stop(t, false, 768);
  advanceToPhase(t, PendingStopPhase::RecordPublished);
  TEST_ASSERT_TRUE_MESSAGE(
      TrackCaptureStopTestAccess::committedScratchInvalidated(t),
      "the deferred publication must invalidate the Track-level committed-MIDI scratch");
}

// S4f.21: a transport stop delivered while a **Record** job is finalizing. The stop edge already
// launched PLAYING, so the retarget has to (a) be mode-aware -- the Record chain owns none of the
// companion, canonical-tail or undo-collection roles the overdub recovery manipulates -- and
// (b) resolve the final transport state at retirement. Without (b) the track stayed TRACK_PLAYING
// with a STOPPED destination, because `handleTransportStop`'s `isPlaying()` branch is unreachable
// once the pending-job branch has taken the turn.
void test_record_stop_playback_reset_preserves_playing_destination() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60);
  clockManager.assignCurrentTickSilently(768);
  t.stopRecording(768);
  TEST_ASSERT_TRUE(t.isPlaying());
  TEST_ASSERT_TRUE(t.hasPendingDeferredOverdubStop());
  TEST_ASSERT_EQUAL_MESSAGE(PendingStopDestination::Playing,
      TrackCaptureStopTestAccess::head(t).admission.destination,
      "cache invalidation at the Record playback edge must not retarget the job to STOPPED");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::outputBarrier(t),
      "Record playback must not inherit the transport Stop MIDI barrier");
  // A same-slot live successor must enter while its predecessor still finalizes.
  t.startOverdubbing(800);
  TEST_ASSERT_TRUE(t.isOverdubbing());
  TEST_ASSERT_TRUE(t.hasPendingDeferredOverdubStop());
  TEST_ASSERT_EQUAL(PendingStopDestination::Playing,
      TrackCaptureStopTestAccess::head(t).admission.destination);
}

// 3.10 / E3: the real Record stop already seals a job-owned frozen range. A
// complete bar-2 window needs the Track owner, not stop-publication or an
// unfinished whole-root Snapshot, when that sealed range covers the interval.
static void exerciseRecordBarTwoWindow(bool requireUnifiedRecordReader,
                                       bool publishRootBeforeReader = false,
                                       bool requireNextWrap = false,
                                       bool rebaseRootRequestBeforeReader = false,
                                       bool qualifyRootBeforeReader = false,
                                       bool carryRootThroughCaptureEdge = false) {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60); // accepted bar-1 On@12 / Off@96
  t.noteOn(1, 62, 100, bar + 12);
  t.noteOff(1, 62, 0, bar + 240); // beyond content-length grace; two bars survive the real stop
  Loop& loop = t.getLoop(0);
  loop.loopLengthTicks = 2 * bar;
  // Production VisualCache turns prepare the first bar while Record is live.
  clockManager.assignCurrentTickSilently(bar);
  for (unsigned turn = 0; turn < 64; ++turn)
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  const uint64_t liveCaptureReceipt = loop.captureContentGeneration();
  TEST_ASSERT_TRUE(liveCaptureReceipt != 0);
  clockManager.assignCurrentTickSilently(2 * bar);
  t.stopRecording(2 * bar); // playing-destination stop, no test-written job or view
  TEST_ASSERT_TRUE(t.isPlaying());
  TEST_ASSERT_TRUE(t.hasPendingDeferredOverdubStop());
  TEST_ASSERT_FALSE(loop.passes.hasRecordPass());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  auto& job = TrackCaptureStopTestAccess::head(t);
  TEST_ASSERT_EQUAL(PendingStopPhase::ReservedClosure, job.admission.phase);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.ready());
  TEST_ASSERT_EQUAL_UINT64(liveCaptureReceipt, job.frozenCaptureContentGeneration);
  TEST_ASSERT_NOT_EQUAL_UINT64(liveCaptureReceipt, loop.captureContentGeneration());
  TEST_ASSERT_TRUE(loop.capture.store.empty());
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  t.playMidiEvents(2 * bar, true);
  t.playMidiEvents(2 * bar + 90, true);
  t.playMidiEvents(2 * bar + 96, true); // discharge the accepted bar-1 Off
  const auto* set = runtime.viewSetIfPrewarmed();
  TEST_ASSERT_NOT_NULL(set);
  TEST_ASSERT_TRUE(set->lifecycle.current < set->views.size());
  const auto& first = set->views[set->lifecycle.current];
  TEST_ASSERT_TRUE(first.complete && first.firstRecordReturnWindow);
  const uint8_t firstRole = set->lifecycle.current;
  const uint64_t firstStorage = first.storageGeneration;
  const uint32_t firstRevision = first.source.playbackRevision;
  TEST_ASSERT_FALSE(runtime.frozenRecordWindowReady());
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
      driveGrains(t, TrackCaptureStopTestAccess::closureGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
      driveGrains(t, TrackCaptureStopTestAccess::stagingGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
      driveGrains(t, TrackCaptureStopTestAccess::normalizationGrain));
  TEST_ASSERT_TRUE(loop.prewarmActiveOverdubTransferHorizon());
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
      driveGrains(t, TrackCaptureStopTestAccess::passTransferGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
      driveGrains(t, TrackCaptureStopTestAccess::publicationGrain));
  MidiSendCapture::reset();
  auto* frozenPresentation = TrackCaptureStopTestAccess::presentation(t, 0);
  const LoopId frozenLoopId = frozenPresentation->loopId;
  frozenPresentation->loopId = kInvalidLoopId;
  for (unsigned denied = 0; denied < 64; ++denied)
    (void)t.stepPlaybackViewMaintenance(3 * bar - 1, nullptr, true, {});
  TEST_ASSERT_FALSE_MESSAGE(runtime.frozenRecordWindowReady(),
      "a stale job LoopId must not publish frozen bar-two coverage");
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot) && TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.ready());
  TEST_ASSERT_EQUAL_UINT8(firstRole, set->lifecycle.current);
  TEST_ASSERT_EQUAL_UINT64(firstStorage, set->views[firstRole].storageGeneration);
  TEST_ASSERT_EQUAL_UINT32(0u, MidiSendCapture::count());
  frozenPresentation->loopId = frozenLoopId;
  if (requireUnifiedRecordReader) {
    const uint64_t frozenToken = job.frozenCaptureContentGeneration;
    job.frozenCaptureContentGeneration = 0;
    for (unsigned denied = 0; denied < 64; ++denied)
      (void)t.stepPlaybackViewMaintenance(3 * bar - 1, nullptr, true, {});
    if (runtime.frozenRecordWindowReady()) {
      const auto* borrowedSet = runtime.viewSetIfPrewarmed();
      TEST_ASSERT_NOT_EQUAL(PlaybackWindowOrigin::Capture,
          borrowedSet->views[borrowedSet->lifecycle.ready].source.origin);
    }
    job.frozenCaptureContentGeneration = frozenToken;
  }
  unsigned windowTurns = 0;
  for (; windowTurns < 64; ++windowTurns) {
    const auto* windowSet = runtime.viewSetIfPrewarmed();
    if (runtime.frozenRecordWindowReady() &&
        (!requireUnifiedRecordReader ||
         windowSet->views[windowSet->lifecycle.ready].source.origin == PlaybackWindowOrigin::Capture)) break;
    (void)t.stepPlaybackViewMaintenance(3 * bar - 1, nullptr, true, {});
  }
  TEST_ASSERT_TRUE_MESSAGE(runtime.frozenRecordWindowReady(),
      "the Track owner prepares bar 2 from the already sealed range without stop/Snapshot turns");
  TEST_ASSERT_EQUAL_UINT32(0u, MidiSendCapture::count());
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordPublished, TrackCaptureStopTestAccess::phase(t));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.ready());
  TEST_ASSERT_TRUE(loop.passes.hasRecordPass());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_EQUAL_UINT8(firstRole, set->lifecycle.current);
  TEST_ASSERT_EQUAL_UINT64(firstStorage, set->views[set->lifecycle.current].storageGeneration);
  TEST_ASSERT_EQUAL_UINT32(firstRevision, set->views[set->lifecycle.current].source.playbackRevision);
  const auto& ready = set->views[set->lifecycle.ready];
  TEST_ASSERT_TRUE(ready.complete && !ready.published && !ready.firstRecordReturnWindow);
  TEST_ASSERT_EQUAL_UINT32(bar, ready.coverage.startPhase);
  TEST_ASSERT_EQUAL_UINT32(bar, ready.coverage.lengthTicks);
  TEST_ASSERT_EQUAL_UINT32(2 * bar, ready.coverage.loopLengthTicks);
  TEST_ASSERT_EQUAL_UINT32(loop.playbackRevision, ready.source.playbackRevision);
  TEST_ASSERT_EQUAL_UINT32(TrackCaptureStopTestAccess::playbackGenerationOf(t),
      ready.source.sourceGeneration);
  TEST_ASSERT_NULL(ready.source.snapshotIdentity);
  if (requireUnifiedRecordReader) {
    TEST_ASSERT_EQUAL(PlaybackWindowOrigin::Capture, ready.source.origin);
    TEST_ASSERT_EQUAL_UINT64(job.frozenCaptureContentGeneration,
                             ready.source.captureContentGeneration);
    RuntimePlaybackViewSource frozen{};
    PlaybackViewSourceIdentity receipt{};
    TEST_ASSERT_TRUE(t.qualifiedFrozenRecordCaptureSourceForSlot(0, job.interval, frozen, receipt));
    TEST_ASSERT_TRUE(ready.source == receipt);
    TEST_ASSERT_EQUAL_PTR(&TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange, frozen.context);
  }
  TEST_ASSERT_EQUAL_UINT32(2u, ready.storage.eventCount);
  TEST_ASSERT_TRUE(ready.storage.events[0].isNoteOn());
  TEST_ASSERT_EQUAL_UINT32(bar + 12, ready.storage.events[0].tick);
  TEST_ASSERT_EQUAL_UINT8(62, ready.storage.events[0].data.noteData.note);
  TEST_ASSERT_TRUE(ready.storage.events[1].isNoteOff());
  TEST_ASSERT_EQUAL_UINT32(bar + 240, ready.storage.events[1].tick);
  TEST_ASSERT_EQUAL_UINT8(62, ready.storage.events[1].data.noteData.note);
  if (publishRootBeforeReader) {
    unsigned stopTurns = 0;
    for (; stopTurns < 8192 && job.admission.phase != PendingStopPhase::RecordPublished; ++stopTurns)
      t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
    TEST_ASSERT_EQUAL(PendingStopPhase::RecordPublished, TrackCaptureStopTestAccess::phase(t));
    unsigned snapshotTurns = 0;
    for (; snapshotTurns < 4096 && !loop.publishedSnapshotIsCurrent(); ++snapshotTurns)
      t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
    TEST_ASSERT_TRUE_MESSAGE(loop.publishedSnapshotIsCurrent(),
        "whole-root Snapshot must publish while the frozen ready still owns bar two");
    TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot) && TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.ready());
    TEST_ASSERT_TRUE(runtime.frozenRecordWindowReady());
    TEST_ASSERT_EQUAL_UINT8(firstRole, set->lifecycle.current);
    TEST_ASSERT_EQUAL_UINT64(firstStorage, set->views[firstRole].storageGeneration);
    const uint8_t frozenReadyRole = set->lifecycle.ready;
    const uint64_t frozenReadyStorage = set->views[frozenReadyRole].storageGeneration;
    for (unsigned turn = 0; turn < 8; ++turn) {
      t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
      TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::extentOwned(t, job.slot) && TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.ready(),
          "root publication must not release an unpublished frozen Capture ready");
      TEST_ASSERT_EQUAL_UINT8(frozenReadyRole, set->lifecycle.ready);
      TEST_ASSERT_EQUAL_UINT64(frozenReadyStorage, set->views[frozenReadyRole].storageGeneration);
    }
    const PlaybackViewSourceIdentity frozenReadySource = set->views[frozenReadyRole].source;
    nativePlaybackDeadlineClock = {2 * bar + 1, 1};
    clockManager.assignCurrentTickSilently(2 * bar + 1);
    const auto rootOffer = t.playbackWindowServiceOffer(2 * bar + 1, nativePlaybackDeadlineClock);
    TEST_ASSERT_TRUE_MESSAGE(rootOffer.eligible && rootOffer.canProgress,
        "raw first-bar current cannot satisfy full-loop committed-root coverage");
    (void)t.stepPlaybackViewMaintenance(2 * bar + 1, nullptr, true, nativePlaybackDeadlineClock);
    const auto* owedRoot = t.pendingViewRequest(0);
    TEST_ASSERT_NOT_NULL_MESSAGE(owedRoot,
        "Track must retain root-source demand while frozen Capture ready owns the edge");
    TEST_ASSERT_EQUAL(PlaybackViewPendingRequest::SourceKind::CommittedSnapshot, owedRoot->sourceKind);
    TEST_ASSERT_EQUAL(PlaybackViewPendingReason::AwaitingSourceQualification, owedRoot->reason);
    TEST_ASSERT_EQUAL_UINT64(4 * bar, owedRoot->deadlineTick);
    TEST_ASSERT_EQUAL_UINT32(1u, owedRoot->clockEpoch);
    TEST_ASSERT_EQUAL_PTR(loop.publishedSnapshotIdentity(), owedRoot->source.snapshotIdentity);
    TEST_ASSERT_EQUAL_UINT32(2 * bar, owedRoot->coverage.lengthTicks);
    TEST_ASSERT_EQUAL_UINT8(frozenReadyRole, set->lifecycle.ready);
    TEST_ASSERT_EQUAL_UINT64(frozenReadyStorage, set->views[frozenReadyRole].storageGeneration);
    TEST_ASSERT_TRUE(set->views[frozenReadyRole].source == frozenReadySource);
    TEST_ASSERT_EQUAL_UINT8(firstRole, set->lifecycle.current);
    TEST_ASSERT_EQUAL_UINT64(firstStorage, set->views[firstRole].storageGeneration);
    TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot) && TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.ready());
    TEST_ASSERT_EQUAL_UINT32(0u, MidiSendCapture::count());
    if (qualifyRootBeforeReader) {
      const uint8_t rootBuildRole = set->lifecycle.build;
      TEST_ASSERT_TRUE(rootBuildRole < set->views.size() &&
                       rootBuildRole != firstRole && rootBuildRole != frozenReadyRole);
      const auto begin = t.stepPlaybackViewMaintenance(2 * bar + 1, nullptr, true,
                                                       nativePlaybackDeadlineClock);
      TEST_ASSERT_EQUAL_MESSAGE(Track::PlaybackViewTurnResult::GrainServed, begin,
          "Track owner must begin private root qualification in the third view");
      TEST_ASSERT_TRUE(runtime.ownsCoverageSource());
      TEST_ASSERT_EQUAL(LoopPlaybackRuntime::CoveragePreparation::Counting,
                        runtime.coveragePreparation);
      TEST_ASSERT_EQUAL_UINT8(rootBuildRole, set->lifecycle.build);
      const auto& rootCandidate = set->views[rootBuildRole];
      TEST_ASSERT_EQUAL(PlaybackWindowOrigin::Committed, rootCandidate.source.origin);
      TEST_ASSERT_EQUAL_PTR(loop.publishedSnapshotIdentity(), rootCandidate.source.snapshotIdentity);
      TEST_ASSERT_EQUAL_UINT32(loop.playbackRevision, rootCandidate.source.playbackRevision);
      TEST_ASSERT_EQUAL_UINT32(0u, rootCandidate.coverage.startPhase);
      TEST_ASSERT_EQUAL_UINT32(2 * bar, rootCandidate.coverage.lengthTicks);
      TEST_ASSERT_FALSE(rootCandidate.complete || rootCandidate.published);
      (void)t.stepPlaybackViewMaintenance(2 * bar + 1, nullptr, true,
                                          nativePlaybackDeadlineClock);
      TEST_ASSERT_TRUE_MESSAGE(runtime.coverageSourceUnits > 0,
          "Track owner must advance bounded private root source inspection");
      TEST_ASSERT_EQUAL_UINT8(frozenReadyRole, set->lifecycle.ready);
      TEST_ASSERT_EQUAL_UINT64(frozenReadyStorage, set->views[frozenReadyRole].storageGeneration);
      TEST_ASSERT_TRUE(set->views[frozenReadyRole].source == frozenReadySource);
      TEST_ASSERT_EQUAL_UINT8(firstRole, set->lifecycle.current);
      TEST_ASSERT_EQUAL_UINT64(firstStorage, set->views[firstRole].storageGeneration);
      TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot) && TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.ready());
      TEST_ASSERT_EQUAL_UINT32(0u, MidiSendCapture::count());
      unsigned sourceTurns = 0;
      for (; sourceTurns < 512 &&
             runtime.coveragePreparation == LoopPlaybackRuntime::CoveragePreparation::Counting; ++sourceTurns)
        (void)t.stepPlaybackViewMaintenance(2 * bar + 1, nullptr, true,
                                            nativePlaybackDeadlineClock);
      TEST_ASSERT_EQUAL_MESSAGE(LoopPlaybackRuntime::CoveragePreparation::AwaitAdmission,
          runtime.coveragePreparation, "bounded root source qualification must complete privately");
      TEST_ASSERT_TRUE(runtime.frozenViewRequest.active && runtime.coverageSource.complete());
      TEST_ASSERT_EQUAL_UINT64(4 * bar, runtime.frozenViewRequest.deadlineTick);
      for (unsigned waitTurn = 0; waitTurn < 8; ++waitTurn) {
        (void)t.stepPlaybackViewMaintenance(2 * bar + 1, nullptr, true,
                                            nativePlaybackDeadlineClock);
        TEST_ASSERT_EQUAL_MESSAGE(LoopPlaybackRuntime::CoveragePreparation::AwaitAdmission,
            runtime.coveragePreparation, "root source may not admit while Capture ready is occupied");
        TEST_ASSERT_TRUE(runtime.frozenViewRequest.active && runtime.ownsCoverageSource());
        TEST_ASSERT_FALSE(set->views[rootBuildRole].complete ||
                          set->views[rootBuildRole].published);
        TEST_ASSERT_EQUAL_UINT8(frozenReadyRole, set->lifecycle.ready);
        TEST_ASSERT_EQUAL_UINT64(frozenReadyStorage, set->views[frozenReadyRole].storageGeneration);
        TEST_ASSERT_EQUAL_UINT8(firstRole, set->lifecycle.current);
        TEST_ASSERT_EQUAL_UINT64(firstStorage, set->views[firstRole].storageGeneration);
        TEST_ASSERT_FALSE(t.playbackWindowServiceOffer(2 * bar + 1, nativePlaybackDeadlineClock).canProgress);
        TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot) && TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.ready());
        TEST_ASSERT_EQUAL_UINT32(0u, MidiSendCapture::count());
      }
      if (carryRootThroughCaptureEdge) {
        nativePlaybackDeadlineClock.tick = 3 * bar;
        clockManager.assignCurrentTickSilently(3 * bar);
        t.playMidiEvents(3 * bar, true);
        TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::RollingPhase::Serving),
            static_cast<int>(runtime.rolling.phase));
        TEST_ASSERT_EQUAL_UINT8(frozenReadyRole, set->lifecycle.current);
        TEST_ASSERT_EQUAL_UINT64(frozenReadyStorage,
            set->views[frozenReadyRole].storageGeneration);
        TEST_ASSERT_TRUE(set->views[frozenReadyRole].source == frozenReadySource);
        TEST_ASSERT_EQUAL_UINT8(RuntimePlaybackViewLifecycle::kNoView, set->lifecycle.ready);
        TEST_ASSERT_EQUAL_UINT8(rootBuildRole, set->lifecycle.build);
        TEST_ASSERT_EQUAL(LoopPlaybackRuntime::CoveragePreparation::AwaitAdmission,
                          runtime.coveragePreparation);
        TEST_ASSERT_TRUE(runtime.frozenViewRequest.active && runtime.ownsCoverageSource());
        TEST_ASSERT_FALSE(set->views[rootBuildRole].complete ||
                          set->views[rootBuildRole].published);
        TEST_ASSERT_EQUAL_UINT32(0u, MidiSendCapture::count());
        (void)t.stepPlaybackViewMaintenance(3 * bar, nullptr, true, nativePlaybackDeadlineClock);
        const auto* lateRoot = t.pendingViewRequest(0);
        TEST_ASSERT_NOT_NULL_MESSAGE(lateRoot,
            "root source must retain a named obligation if remaining work cannot meet the wrap");
        TEST_ASSERT_EQUAL(PlaybackViewPendingReason::CannotCompleteBeforeHandoff,
                          lateRoot->reason);
        TEST_ASSERT_EQUAL_UINT8(frozenReadyRole, set->lifecycle.current);
        TEST_ASSERT_EQUAL_UINT64(frozenReadyStorage,
            set->views[frozenReadyRole].storageGeneration);
        TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot) && TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.ready());
        TEST_ASSERT_EQUAL_UINT32(0u, MidiSendCapture::count());
        return;
      }
      nativePlaybackDeadlineClock = {2 * bar + 2, 2};
      clockManager.assignCurrentTickSilently(2 * bar + 2);
      TEST_ASSERT_EQUAL(Track::PlaybackViewTurnResult::GrainDenied,
          t.stepPlaybackViewMaintenance(2 * bar + 2, nullptr, true, nativePlaybackDeadlineClock));
      const auto* invalidatedRoot = t.pendingViewRequest(0);
      TEST_ASSERT_NOT_NULL(invalidatedRoot);
      TEST_ASSERT_EQUAL(PlaybackViewPendingReason::DeadlineInvalidatedEpoch,
                        invalidatedRoot->reason);
      TEST_ASSERT_TRUE(invalidatedRoot->positionInvalidated);
      TEST_ASSERT_FALSE(runtime.viewBuild.active());
      TEST_ASSERT_EQUAL_UINT8(frozenReadyRole, set->lifecycle.ready);
      TEST_ASSERT_EQUAL_UINT64(frozenReadyStorage, set->views[frozenReadyRole].storageGeneration);
      TEST_ASSERT_TRUE(set->views[frozenReadyRole].source == frozenReadySource);
      TEST_ASSERT_EQUAL_UINT8(firstRole, set->lifecycle.current);
      TEST_ASSERT_EQUAL_UINT64(firstStorage, set->views[firstRole].storageGeneration);
      TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot) && TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.ready());
      TEST_ASSERT_EQUAL_UINT32(0u, MidiSendCapture::count());
      return; // Private source grains, not a root ready or an admission verdict.
    }
    if (rebaseRootRequestBeforeReader) {
      nativePlaybackDeadlineClock = {2 * bar + 2, 2};
      clockManager.assignCurrentTickSilently(2 * bar + 2);
      const auto rebaseOffer = t.playbackWindowServiceOffer(2 * bar + 2, nativePlaybackDeadlineClock);
      TEST_ASSERT_TRUE_MESSAGE(rebaseOffer.eligible && rebaseOffer.canProgress,
          "stale root request needs a Track-owner restamp turn while Capture ready waits");
      (void)t.stepPlaybackViewMaintenance(2 * bar + 2, nullptr, true, nativePlaybackDeadlineClock);
      const auto* rebased = t.pendingViewRequest(0);
      TEST_ASSERT_NOT_NULL(rebased);
      TEST_ASSERT_EQUAL(PlaybackViewPendingRequest::SourceKind::CommittedSnapshot, rebased->sourceKind);
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u, rebased->clockEpoch,
          "stale root epoch must not suppress the new positioned demand");
      TEST_ASSERT_EQUAL_UINT64(4 * bar, rebased->deadlineTick);
      TEST_ASSERT_EQUAL_UINT64(2 * bar + 2, rebased->work.admittedTick);
      TEST_ASSERT_EQUAL_PTR(loop.publishedSnapshotIdentity(), rebased->source.snapshotIdentity);
      TEST_ASSERT_EQUAL_UINT32(2 * bar, rebased->coverage.lengthTicks);
      TEST_ASSERT_EQUAL_UINT8(frozenReadyRole, set->lifecycle.ready);
      TEST_ASSERT_EQUAL_UINT64(frozenReadyStorage, set->views[frozenReadyRole].storageGeneration);
      TEST_ASSERT_TRUE(set->views[frozenReadyRole].source == frozenReadySource);
      TEST_ASSERT_EQUAL_UINT8(firstRole, set->lifecycle.current);
      TEST_ASSERT_EQUAL_UINT64(firstStorage, set->views[firstRole].storageGeneration);
      TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot) && TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.ready());
      TEST_ASSERT_EQUAL_UINT32(0u, MidiSendCapture::count());
      return; // This case isolates request currency; no edge under the old epoch.
    }
    nativePlaybackDeadlineClock = {3 * bar - 1, 1};
    clockManager.assignCurrentTickSilently(3 * bar - 1);
    (void)t.stepPlaybackViewMaintenance(3 * bar - 1, nullptr, true, nativePlaybackDeadlineClock);
    const auto* pendingRoot = t.pendingViewRequest(0);
    const auto& retainedRoot = pendingRoot != nullptr ? *pendingRoot : runtime.frozenViewRequest;
    TEST_ASSERT_TRUE(retainedRoot.active);
    TEST_ASSERT_EQUAL(PlaybackViewPendingRequest::SourceKind::CommittedSnapshot, retainedRoot.sourceKind);
    TEST_ASSERT_EQUAL_UINT64(4 * bar, retainedRoot.deadlineTick);
    TEST_ASSERT_EQUAL_UINT32(1u, retainedRoot.clockEpoch);
    TEST_ASSERT_EQUAL_PTR(loop.publishedSnapshotIdentity(), retainedRoot.source.snapshotIdentity);
    if (pendingRoot == nullptr) {
      TEST_ASSERT_TRUE(runtime.ownsCoverageSource());
      TEST_ASSERT_TRUE(set->lifecycle.build < set->views.size());
      TEST_ASSERT_FALSE(set->views[set->lifecycle.build].complete ||
                        set->views[set->lifecycle.build].published);
    }
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(frozenReadyRole, set->lifecycle.ready,
        "root owner may not retire a frozen ready owed at this edge");
    TEST_ASSERT_EQUAL_UINT64(frozenReadyStorage, set->views[frozenReadyRole].storageGeneration);
    TEST_ASSERT_TRUE(set->views[frozenReadyRole].complete &&
                     set->views[frozenReadyRole].source == frozenReadySource);
    TEST_ASSERT_EQUAL_UINT8(firstRole, set->lifecycle.current);
    TEST_ASSERT_EQUAL_UINT64(firstStorage, set->views[firstRole].storageGeneration);
    TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot) && TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.ready());
    TEST_ASSERT_EQUAL_UINT32(0u, MidiSendCapture::count());
  }
  if (!requireUnifiedRecordReader) return;
  // The legacy first-bar view above is only a setup control. The bar-2 edge
  // must select the *same* Track-owned ready through the unified rolling reader.
  const uint8_t preparedRole = set->lifecycle.ready;
  const uint64_t preparedStorage = ready.storageGeneration;
  // A ready view is not sufficient if the owning job changes before the edge.
  // The legacy route may still run, but it cannot claim Capture adoption.
  const LoopId readyLoopId = job.loopId;
  job.loopId = kInvalidLoopId;
  nativePlaybackDeadlineClock = {3 * bar - 1, 1};
  clockManager.assignCurrentTickSilently(3 * bar - 1);
  t.playMidiEvents(3 * bar - 1, true);
  TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::RollingPhase::Idle),
      static_cast<int>(runtime.rolling.phase));
  TEST_ASSERT_EQUAL_UINT8(preparedRole, set->lifecycle.ready);
  TEST_ASSERT_EQUAL_UINT64(preparedStorage, set->views[preparedRole].storageGeneration);
  TEST_ASSERT_EQUAL_UINT32(0u, MidiSendCapture::count());
  job.loopId = readyLoopId;
  nativePlaybackDeadlineClock = {3 * bar, 1};
  clockManager.assignCurrentTickSilently(3 * bar);
  t.playMidiEvents(3 * bar, true);
  TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::RollingPhase::Serving),
      static_cast<int>(runtime.rolling.phase));
  TEST_ASSERT_EQUAL_UINT8(preparedRole, runtime.rolling.boundView);
  TEST_ASSERT_EQUAL_UINT64(preparedStorage, runtime.rolling.boundStorageGeneration);
  TEST_ASSERT_EQUAL_UINT64(3 * bar, runtime.rolling.lastTick);
  TEST_ASSERT_EQUAL_UINT8(preparedRole, set->lifecycle.current);
  TEST_ASSERT_EQUAL_UINT64(preparedStorage, set->views[set->lifecycle.current].storageGeneration);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot));
  TEST_ASSERT_EQUAL(publishRootBeforeReader, loop.publishedSnapshotIsCurrent());
  if (publishRootBeforeReader) {
    TEST_ASSERT_EQUAL(static_cast<int>(Track::RollingOutcome::Serving),
        static_cast<int>(t.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock)));
    TEST_ASSERT_EQUAL_UINT8(preparedRole, set->lifecycle.current);
    TEST_ASSERT_EQUAL_UINT64(preparedStorage, set->views[preparedRole].storageGeneration);
    TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot) && TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.ready());
    for (unsigned turn = 0; turn < 8; ++turn) {
      t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
      TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::extentOwned(t, job.slot) && TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.ready(),
          "root publication alone must not release the serving frozen Capture range");
      TEST_ASSERT_TRUE(t.hasPendingDeferredOverdubStop());
      TEST_ASSERT_EQUAL_UINT8(preparedRole, set->lifecycle.current);
      TEST_ASSERT_EQUAL_UINT64(preparedStorage, set->views[preparedRole].storageGeneration);
    }
  }
  TEST_ASSERT_EQUAL_UINT32(0u, MidiSendCapture::count());
  const uint32_t ticks[] = {3 * bar + 12, 3 * bar + 240};
  for (size_t index = 0; index < 2; ++index) {
    nativePlaybackDeadlineClock.tick = ticks[index];
    clockManager.assignCurrentTickSilently(ticks[index]);
    t.playMidiEvents(ticks[index], true);
    TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::RollingPhase::Serving),
        static_cast<int>(runtime.rolling.phase));
    TEST_ASSERT_EQUAL_UINT8(preparedRole, runtime.rolling.boundView);
    TEST_ASSERT_EQUAL_UINT64(preparedStorage, runtime.rolling.boundStorageGeneration);
    TEST_ASSERT_EQUAL_UINT64(ticks[index], runtime.rolling.lastTick);
    TEST_ASSERT_EQUAL_UINT32(index + 1u, MidiSendCapture::count());
    const MidiEvent& event = MidiSendCapture::at(index);
    TEST_ASSERT_EQUAL_UINT32(bar + (index == 0 ? 12u : 240u), event.tick);
    TEST_ASSERT_EQUAL_UINT8(1, event.channel);
    TEST_ASSERT_EQUAL_UINT8(62, event.data.noteData.note);
    TEST_ASSERT_EQUAL_UINT8(index == 0 ? 100 : 0, event.data.noteData.velocity);
    TEST_ASSERT_EQUAL(index == 0, event.isNoteOn());
    TEST_ASSERT_EQUAL(index == 1, event.isNoteOff());
    unsigned sounding = 0;
    runtime.ledger.forEachActive([&](uint8_t channel, uint8_t note,
                                     const ActiveNoteLedger::Entry&) {
      ++sounding;
      TEST_ASSERT_EQUAL_UINT8(1, channel);
      TEST_ASSERT_EQUAL_UINT8(62, note);
    });
    TEST_ASSERT_EQUAL_UINT32(index == 0 ? 1u : 0u, sounding);
    t.playMidiEvents(ticks[index], true);
    TEST_ASSERT_EQUAL_UINT32(index + 1u, MidiSendCapture::count());
    TEST_ASSERT_EQUAL_UINT8(preparedRole, set->lifecycle.current);
    TEST_ASSERT_EQUAL_UINT64(preparedStorage, set->views[set->lifecycle.current].storageGeneration);
  }
  if (!requireNextWrap) return;
  const auto wrappedReady = [&]() -> const RuntimePlaybackView* {
    if (!runtime.frozenRecordWindowReady() || set->lifecycle.ready >= set->views.size()) return nullptr;
    const auto& view = set->views[set->lifecycle.ready];
    const bool sourceMatches = publishRootBeforeReader
        ? view.source.origin == PlaybackWindowOrigin::Committed &&
          view.source.snapshotIdentity == loop.publishedSnapshotIdentity() &&
          view.source.playbackRevision == loop.playbackRevision
        : view.source.origin == PlaybackWindowOrigin::Capture &&
          view.source.captureContentGeneration == job.frozenCaptureContentGeneration;
    return view.complete && !view.published && sourceMatches &&
        view.source.sourceGeneration == TrackCaptureStopTestAccess::playbackGenerationOf(t) &&
        view.coverage.startPhase == 0 &&
        view.coverage.lengthTicks == (publishRootBeforeReader ? 2 * bar : bar) &&
        view.coverage.loopLengthTicks == 2 * bar ? &view : nullptr;
  };
  if (publishRootBeforeReader) {
    const auto offer = t.playbackWindowServiceOffer(ticks[1], nativePlaybackDeadlineClock);
    TEST_ASSERT_TRUE_MESSAGE(offer.eligible && offer.canProgress,
        "Capture current must not count as a served committed-root window");
  }
  unsigned successorTurns = 0;
  auto lastTurn = Track::PlaybackViewTurnResult::Idle;
  for (; successorTurns < 128 && wrappedReady() == nullptr; ++successorTurns)
    lastTurn = t.stepPlaybackViewMaintenance(
        publishRootBeforeReader ? ticks[1] : 4 * bar - 1, nullptr, true,
        publishRootBeforeReader ? nativePlaybackDeadlineClock : PlaybackDeadlineClock{});
  const auto* pendingRoot = t.pendingViewRequest(0);
  const uint8_t readyIndex = set->lifecycle.ready;
  const RuntimePlaybackView* lastReady = readyIndex < set->views.size() ? &set->views[readyIndex] : nullptr;
  char wrapDetail[230];
  snprintf(wrapDetail, sizeof(wrapDetail),
      "wrap root=%u turns=%u last=%u pending=%u reason=%u ready=%u origin=%u len=%u build=%u",
      loop.publishedSnapshotIsCurrent(), successorTurns, static_cast<unsigned>(lastTurn),
      pendingRoot != nullptr, pendingRoot ? static_cast<unsigned>(pendingRoot->reason) : 255u,
      readyIndex, lastReady ? static_cast<unsigned>(lastReady->source.origin) : 255u,
      lastReady ? lastReady->coverage.lengthTicks : 0u, set->lifecycle.build);
  if (publishRootBeforeReader && wrappedReady() == nullptr) {
    TEST_ASSERT_NOT_NULL_MESSAGE(pendingRoot, wrapDetail);
    TEST_ASSERT_EQUAL(PlaybackViewPendingReason::CannotCompleteBeforeHandoff, pendingRoot->reason);
    TEST_ASSERT_EQUAL_UINT8(preparedRole, set->lifecycle.current);
    TEST_ASSERT_EQUAL_UINT64(preparedStorage, set->views[preparedRole].storageGeneration);
    TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot) && TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.ready());
  }
  TEST_ASSERT_NOT_NULL_MESSAGE(wrappedReady(), wrapDetail);
  TEST_ASSERT_EQUAL_UINT8(preparedRole, set->lifecycle.current);
  TEST_ASSERT_EQUAL_UINT64(preparedStorage, set->views[preparedRole].storageGeneration);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot) && TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.ready());
  TEST_ASSERT_EQUAL_UINT32(2u, MidiSendCapture::count());
  const uint8_t wrappedRole = set->lifecycle.ready;
  const uint64_t wrappedStorage = set->views[wrappedRole].storageGeneration;
  TEST_ASSERT_EQUAL_UINT32(publishRootBeforeReader ? 4u : 2u,
                           set->views[wrappedRole].storage.eventCount);
  TEST_ASSERT_TRUE(set->views[wrappedRole].storage.events[0].isNoteOn());
  TEST_ASSERT_EQUAL_UINT32(12u, set->views[wrappedRole].storage.events[0].tick);
  TEST_ASSERT_TRUE(set->views[wrappedRole].storage.events[1].isNoteOff());
  TEST_ASSERT_EQUAL_UINT32(96u, set->views[wrappedRole].storage.events[1].tick);
  nativePlaybackDeadlineClock.tick = 4 * bar;
  clockManager.assignCurrentTickSilently(4 * bar);
  t.playMidiEvents(4 * bar, true);
  TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::RollingPhase::Serving),
      static_cast<int>(runtime.rolling.phase));
  TEST_ASSERT_EQUAL_UINT8(wrappedRole, runtime.rolling.boundView);
  TEST_ASSERT_EQUAL_UINT64(wrappedStorage, runtime.rolling.boundStorageGeneration);
  TEST_ASSERT_EQUAL_UINT8(wrappedRole, set->lifecycle.current);
  TEST_ASSERT_EQUAL_UINT32(2u, MidiSendCapture::count());
  TEST_ASSERT_EQUAL(static_cast<int>(Track::RollingOutcome::Serving),
      static_cast<int>(t.serviceRollingPlaybackWindow(nativePlaybackDeadlineClock)));
  TEST_ASSERT_EQUAL_UINT8(wrappedRole, set->lifecycle.current);
  TEST_ASSERT_EQUAL_UINT64(wrappedStorage, set->views[wrappedRole].storageGeneration);
  const uint32_t wrapTicks[] = {4 * bar + 12, 4 * bar + 96};
  for (size_t index = 0; index < 2; ++index) {
    nativePlaybackDeadlineClock.tick = wrapTicks[index];
    clockManager.assignCurrentTickSilently(wrapTicks[index]);
    t.playMidiEvents(wrapTicks[index], true);
    TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::RollingPhase::Serving),
        static_cast<int>(runtime.rolling.phase));
    TEST_ASSERT_EQUAL_UINT8(wrappedRole, runtime.rolling.boundView);
    TEST_ASSERT_EQUAL_UINT64(wrappedStorage, runtime.rolling.boundStorageGeneration);
    TEST_ASSERT_EQUAL_UINT64(wrapTicks[index], runtime.rolling.lastTick);
    TEST_ASSERT_EQUAL_UINT32(3u + index, MidiSendCapture::count());
    const MidiEvent& event = MidiSendCapture::at(2 + index);
    TEST_ASSERT_EQUAL_UINT32(index == 0 ? 12u : 96u, event.tick);
    TEST_ASSERT_EQUAL_UINT8(1, event.channel);
    TEST_ASSERT_EQUAL_UINT8(60, event.data.noteData.note);
    TEST_ASSERT_EQUAL_UINT8(index == 0 ? 100 : 0, event.data.noteData.velocity);
    TEST_ASSERT_EQUAL(index == 0, event.isNoteOn());
    TEST_ASSERT_EQUAL(index == 1, event.isNoteOff());
    unsigned sounding = 0;
    runtime.ledger.forEachActive([&](uint8_t channel, uint8_t note,
                                     const ActiveNoteLedger::Entry&) {
      ++sounding;
      TEST_ASSERT_EQUAL_UINT8(1, channel);
      TEST_ASSERT_EQUAL_UINT8(60, note);
    });
    TEST_ASSERT_EQUAL_UINT32(index == 0 ? 1u : 0u, sounding);
    t.playMidiEvents(wrapTicks[index], true);
    TEST_ASSERT_EQUAL_UINT32(3u + index, MidiSendCapture::count());
  }
}

// A sealed producer is empty; the Record job's declared preparation work must
// count the pre-cut rows and chunks, not that emptied producer directory.
void test_record_stop_preparation_receipt_counts_multichunk_frozen_capture() {
  LoopEventStore::initPool();
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60);
  constexpr size_t kPairs = LoopEventStoreConfig::CHUNK_CAPACITY / 2 + 1;
  for (size_t index = 0; index < kPairs; ++index) {
    const uint32_t on = 100 + 2 * static_cast<uint32_t>(index);
    t.noteOn(1, 62, 100, on);
    t.noteOff(1, 62, 0, on + 1);
  }
  Loop& loop = t.getLoop(0);
  const size_t rows = loop.capture.store.size();
  const size_t chunks = loop.capture.store.chunkIds().size();
  TEST_ASSERT_TRUE(rows > LoopEventStoreConfig::CHUNK_CAPACITY);
  TEST_ASSERT_TRUE(chunks > 1);
  const uint64_t receipt = loop.captureContentGeneration();
  clockManager.assignCurrentTickSilently(2 * Config::TICKS_PER_BAR);
  t.stopRecording(2 * Config::TICKS_PER_BAR);
  TEST_ASSERT_TRUE(t.hasPendingDeferredOverdubStop());
  const auto& job = TrackCaptureStopTestAccess::head(t);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot));
  TEST_ASSERT_EQUAL_UINT64(receipt, job.frozenCaptureContentGeneration);
  TEST_ASSERT_EQUAL_UINT32(rows, TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.size());
  const uint64_t quantum = job.admission.canonicalPreparationQuantum;
  TEST_ASSERT_TRUE(quantum > 0);
  const auto grains = [quantum](size_t count) {
    return (static_cast<uint64_t>(count) + quantum - 1) / quantum;
  };
  const uint64_t expected =
      2 * grains(LoopStopFinalize::TailRepairWorkspace::kLanes) +
      8 * grains(rows) + 3 * grains(chunks) + 2 + 4;
  TEST_ASSERT_EQUAL_UINT64(expected, job.admission.remainingCanonicalPreparationGrains);
}

void test_frozen_record_capture_adapter_keeps_job_content_and_range_identities_distinct() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  LoopEventStore::initPool();
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60); // On@12/Off@96
  t.noteOn(1, 62, 100, bar + 12);
  t.noteOff(1, 62, 0, bar + 240);
  Loop& loop = t.getLoop(0);
  const uint64_t beforeCut = loop.captureContentGeneration();
  clockManager.assignCurrentTickSilently(2 * bar);
  t.stopRecording(2 * bar);
  TEST_ASSERT_TRUE(t.hasPendingDeferredOverdubStop());
  auto& job = TrackCaptureStopTestAccess::head(t);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot) && TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.ready());
  TEST_ASSERT_EQUAL_UINT32(2 * bar, job.loopLengthTicks);
  TEST_ASSERT_EQUAL_UINT64(beforeCut, job.frozenCaptureContentGeneration);
  TEST_ASSERT_NOT_EQUAL_UINT64(beforeCut, loop.captureContentGeneration());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  RuntimePlaybackViewSource source{};
  PlaybackViewSourceIdentity identity{};
  auto* presentation = TrackCaptureStopTestAccess::presentation(t, 0);
  TEST_ASSERT_TRUE(t.qualifiedFrozenRecordCaptureSourceForSlot(0, presentation->interval, source, identity));
  source = {}; identity = {}; // rejected lookups below must not populate their output
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
      driveGrains(t, TrackCaptureStopTestAccess::closureGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
      driveGrains(t, TrackCaptureStopTestAccess::stagingGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
      driveGrains(t, TrackCaptureStopTestAccess::normalizationGrain));
  TEST_ASSERT_TRUE(loop.prewarmActiveOverdubTransferHorizon());
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
      driveGrains(t, TrackCaptureStopTestAccess::passTransferGrain));
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
      driveGrains(t, TrackCaptureStopTestAccess::publicationGrain));
  TEST_ASSERT_EQUAL_UINT32(0, TrackCaptureStopTestAccess::stopQueueDepth(t));
  TEST_ASSERT_TRUE(presentation->retirementActive);
  TEST_ASSERT_FALSE(t.qualifiedFrozenRecordCaptureSourceForSlot(1, presentation->interval, source, identity));
  TEST_ASSERT_NULL(source.context);
  const auto qualifies = [&]() {
    return t.qualifiedFrozenRecordCaptureSourceForSlot(0, presentation->interval, source, identity);
  };
  TEST_ASSERT_TRUE(qualifies());
  TEST_ASSERT_EQUAL(PlaybackWindowOrigin::Capture, source.origin);
  TEST_ASSERT_FALSE(t.qualifiedFrozenRecordCaptureSourceForSlot(0, presentation->interval + 1, source, identity));
  const LoopId loopId = presentation->loopId;
  presentation->loopId = kInvalidLoopId;
  TEST_ASSERT_FALSE(qualifies());
  presentation->loopId = loopId;
  Loop* owner = presentation->owner;
  presentation->owner = &t.getLoop(1);
  TEST_ASSERT_FALSE(qualifies());
  presentation->owner = owner;
  const uint32_t length = presentation->loopLengthTicks;
  presentation->loopLengthTicks = bar;
  TEST_ASSERT_FALSE(qualifies());
  presentation->loopLengthTicks = length;
  const uint32_t generation = presentation->generation;
  ++presentation->generation;
  TEST_ASSERT_FALSE(qualifies());
  presentation->generation = generation;
  const uint64_t frozenReceipt = presentation->frozenCaptureContentGeneration;
  presentation->frozenCaptureContentGeneration = 0;
  TEST_ASSERT_FALSE(qualifies());
  presentation->frozenCaptureContentGeneration = frozenReceipt;
  TEST_ASSERT_TRUE(qualifies());
  TEST_ASSERT_EQUAL_PTR(&presentation->rawRange, source.context);
  TEST_ASSERT_EQUAL_UINT64(frozenReceipt, identity.captureContentGeneration);
  TEST_ASSERT_EQUAL_UINT64(presentation->rawRange.generation(), source.generationToken);
  TEST_ASSERT_EQUAL_UINT64(beforeCut, source.captureContentToken);
  TEST_ASSERT_NOT_EQUAL_UINT64(source.generationToken, source.captureContentToken);
  TEST_ASSERT_FALSE(source.mutableCapture);
  TEST_ASSERT_EQUAL_UINT32(loop.playbackRevision, identity.playbackRevision);
  TEST_ASSERT_EQUAL_UINT32(TrackCaptureStopTestAccess::playbackGenerationOf(t), identity.sourceGeneration);
  TEST_ASSERT_NULL(identity.snapshotIdentity);
  const PlaybackCoverageInterval coverage{bar, bar, 2 * bar};
  RuntimePlaybackView view;
  TEST_ASSERT_TRUE(view.prewarm(4));
  RuntimePlaybackViewBuild build;
  TEST_ASSERT_FALSE(build.begin(view, source,
      {identity.playbackRevision, identity.sourceGeneration, nullptr,
       PlaybackWindowOrigin::Capture, identity.captureContentGeneration + 1},
      coverage, {}, {}, RuntimePlaybackViewFillScope::PlaybackCoverage));
  TEST_ASSERT_FALSE(view.complete || view.published);
  MidiSendCapture::reset();
  TEST_ASSERT_TRUE(build.begin(view, source, identity, coverage, {}, {},
      RuntimePlaybackViewFillScope::PlaybackCoverage));
  for (unsigned i = 0; i < 16 && build.nextGrain() == RuntimePlaybackViewBuildGrain::Fill; ++i)
    (void)build.stepFill(2);
  for (unsigned i = 0; i < 16 && build.nextGrain() == RuntimePlaybackViewBuildGrain::Order; ++i)
    (void)build.stepOrder(4);
  TEST_ASSERT_EQUAL(RuntimePlaybackViewBuildGrain::Complete, build.nextGrain());
  TEST_ASSERT_TRUE(view.complete && !view.published && !view.rawCoveragePrepared);
  TEST_ASSERT_TRUE(view.source == identity);
  TEST_ASSERT_EQUAL_UINT32(2, view.storage.eventCount);
  const MidiEvent& on = view.storage.events[view.storage.order[0]];
  const MidiEvent& off = view.storage.events[view.storage.order[1]];
  TEST_ASSERT_TRUE(on.isNoteOn());
  TEST_ASSERT_EQUAL_UINT32(bar + 12, on.tick);
  TEST_ASSERT_EQUAL_UINT8(1, on.channel);
  TEST_ASSERT_EQUAL_UINT8(62, on.data.noteData.note);
  TEST_ASSERT_EQUAL_UINT8(100, on.data.noteData.velocity);
  TEST_ASSERT_TRUE(off.isNoteOff());
  TEST_ASSERT_EQUAL_UINT32(bar + 240, off.tick);
  TEST_ASSERT_EQUAL_UINT8(1, off.channel);
  TEST_ASSERT_EQUAL_UINT8(62, off.data.noteData.note);
  TEST_ASSERT_EQUAL_UINT32(0u, MidiSendCapture::count());
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.ready());
}

void test_record_bar_two_window_uses_sealed_range_before_stop_publication() {
  exerciseRecordBarTwoWindow(false);
}

#if defined(UNIFIED_RECORD_RETURN_READER_ORACLE)
void test_record_return_bar_two_uses_unified_window_reader() {
  exerciseRecordBarTwoWindow(true);
}
void test_record_return_keeps_frozen_reader_after_root_publishes() {
  exerciseRecordBarTwoWindow(true, true);
}
void test_record_return_reader_rotates_frozen_range_across_wrap() {
  exerciseRecordBarTwoWindow(true, false, true);
}
void test_record_return_reader_hands_frozen_current_to_root_at_wrap() {
  exerciseRecordBarTwoWindow(true, true, true);
}
void test_record_root_request_rebases_while_capture_ready_waits() {
  exerciseRecordBarTwoWindow(true, true, false, true);
}
void test_record_root_source_qualifies_privately_behind_capture_ready() {
  exerciseRecordBarTwoWindow(true, true, false, false, true);
}
void test_record_root_source_waits_then_rechecks_after_capture_adoption() {
  exerciseRecordBarTwoWindow(true, true, false, false, true, true);
}
// A long Record return must not mistake a later clean playback cycle for
// complete coverage immediately after the stop. Keep the whole root private;
// each bar below has an independently accepted On/Off in the frozen range.

// Task 3.9 / D10: the frozen Record-return owner's **completion** terminal must report a receipt that names the window
// it just made ready -- not only the slot's *committed* readiness. On the device the only receipt row was
// `win_readiness,<track>,<slot>,not_servable,...` (`captures/e2-lane-rows-202609…-run2`), which cannot be told from
// "there is nothing to serve". The fixture drives the **production** path -- a real two-bar first Record through
// `Track::noteOn`/`noteOff`, the prewarm/return-window preparation turns, the real `Track::stopRecording`, then the
// real `Track::stepPlaybackViewMaintenance` owner until the frozen successor completes -- so the assertion is about
// the row the device would emit rather than about a state beside it. RED before the source-qualified row exists
// (`FrozenReadinessRows::count == 0`), and RED again if the emitter is disabled at its call site.
void test_frozen_record_completion_emits_source_qualified_receipt() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60);
  t.noteOn(1, 62, 100, bar + 12);
  t.noteOff(1, 62, 0, bar + 240);
  Loop& loop = t.getLoop(0);
  loop.loopLengthTicks = 2 * bar;
  clockManager.assignCurrentTickSilently(bar);
  for (unsigned turn = 0; turn < 64; ++turn)
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  clockManager.assignCurrentTickSilently(2 * bar);
  t.stopRecording(2 * bar);
  TEST_ASSERT_TRUE(t.isPlaying());
  TEST_ASSERT_EQUAL_UINT32(2 * bar, loop.loopLengthTicks);
  TEST_ASSERT_FALSE(loop.passes.hasRecordPass());
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  auto* set = runtime.viewSetIfPrewarmed();
  TEST_ASSERT_NOT_NULL(set);
  TEST_ASSERT_FALSE_MESSAGE(runtime.frozenRecordWindowReady(),
      "precondition: no frozen window is ready before the owner turns");
  FrozenReadinessRows::reset();
  PlaybackPathRows::reset();
  nativePlaybackDeadlineClock = {3 * bar - 1, 1};
  clockManager.assignCurrentTickSilently(3 * bar - 1);
  unsigned drivenTurns = 0;
  for (unsigned turn = 0; turn < 256; ++turn) {
    if (set->lifecycle.ready < set->views.size() &&
        set->views[set->lifecycle.ready].complete &&
        set->views[set->lifecycle.ready].source.origin == PlaybackWindowOrigin::Capture) break;
    (void)t.stepPlaybackViewMaintenance(3 * bar - 1, nullptr, true, nativePlaybackDeadlineClock);
    ++drivenTurns;
  }
  TEST_ASSERT_TRUE_MESSAGE(set->lifecycle.ready < set->views.size() && drivenTurns > 0,
      "the job-owned frozen source must reach the real Track window owner");
  const auto& ready = set->views[set->lifecycle.ready];
  TEST_ASSERT_TRUE_MESSAGE(ready.complete && !ready.published,
      "the frozen completion must be a complete, as-yet-unpublished Capture window");
  TEST_ASSERT_EQUAL(PlaybackWindowOrigin::Capture, ready.source.origin);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, FrozenReadinessRows::count,
      "the first frozen completion must report once, on its own transition");
  const auto& row = FrozenReadinessRows::rows[0];
  TEST_ASSERT_EQUAL_UINT8(0, row.track);
  TEST_ASSERT_EQUAL_UINT8(0, row.slot);
  TEST_ASSERT_EQUAL_STRING("complete_ready", row.readiness);
  TEST_ASSERT_EQUAL_STRING("clock_edge", row.holder);
  // The row must name **this** window: the completed view's own identity and interval, not a generic slot verdict.
  TEST_ASSERT_EQUAL_UINT32(ready.source.playbackRevision, row.revision);
  TEST_ASSERT_EQUAL_UINT32(ready.source.sourceGeneration, row.sourceGeneration);
  TEST_ASSERT_EQUAL_UINT64(ready.source.captureContentGeneration, row.captureToken);
  TEST_ASSERT_NOT_EQUAL_UINT64(0, row.captureToken);
  TEST_ASSERT_EQUAL_UINT32(ready.coverage.startPhase, row.start);
  TEST_ASSERT_EQUAL_UINT32(ready.coverage.lengthTicks, row.length);
  TEST_ASSERT_EQUAL_UINT32(ready.coverage.loopLengthTicks, row.loopLength);
  TEST_ASSERT_EQUAL_UINT32(handoffLeadTicks((3 * bar - 1) % ready.coverage.loopLengthTicks,
                                            ready.coverage.startPhase, ready.coverage.loopLengthTicks),
                           row.leadTicks);
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
}
void test_long_record_return_serves_early_frozen_bars_before_root_publication() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  constexpr uint32_t length = 24 * bar;
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60);
  for (uint32_t index = 0; index < 24; ++index) {
    for (uint32_t note = 0; note < 16; ++note) {
      if (index == 0 && note == 0) continue; // start() already accepted this bar's first pair.
      const uint32_t onTick = index * bar + 12 + note * 48;
      const uint8_t pitch = static_cast<uint8_t>(36 + note);
      t.noteOn(1, pitch, 100, onTick);
      t.noteOff(1, pitch, 0, onTick + 20);
    }
  }
  Loop& loop = t.getLoop(0);
  loop.loopLengthTicks = length;
  clockManager.assignCurrentTickSilently(bar);
  for (unsigned turn = 0; turn < 64; ++turn)
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  clockManager.assignCurrentTickSilently(length + 473);
  t.stopRecording(length + 473);
  TEST_ASSERT_TRUE(t.isPlaying());
  TEST_ASSERT_EQUAL_UINT32(length, loop.loopLengthTicks);
  auto& job = TrackCaptureStopTestAccess::head(t);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot) && TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.ready());
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  MidiSendCapture::reset();
  // The first return bar remains the legacy setup control, not proof of the
  // migrated reader. Each subsequent bar must adopt a complete Capture view.
  nativePlaybackDeadlineClock = {length + 473, 1};
  clockManager.assignCurrentTickSilently(length + 473);
  t.playMidiEvents(length + 473, true);
  MidiSendCapture::reset();
  for (uint32_t index = 0; index < 5; ++index) {
    const uint32_t firstOffset = index == 0 ? 474 : 0;
    const uint32_t expectedBefore = MidiSendCapture::count();
    for (uint32_t offset = firstOffset; offset < bar; ++offset) {
      const uint32_t tick = length + index * bar + offset;
      nativePlaybackDeadlineClock.tick = tick;
      clockManager.assignCurrentTickSilently(tick);
      if (offset % 64 == 0) {
        (void)t.stepPlaybackViewMaintenance(tick, nullptr, true, nativePlaybackDeadlineClock);
      }
      t.playMidiEvents(tick, true);
    }
    if (index == 0) continue; // The first partial bar is a legacy setup control.
    TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::extentOwned(t, job.slot) && TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.ready(),
        "the frozen source must remain owned until an actual reader handoff");
    TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
    const auto* set = runtime.viewSetIfPrewarmed();
    const auto offer = t.playbackWindowServiceOffer(length + (index + 1) * bar - 1,
                                                    nativePlaybackDeadlineClock);
    const uint8_t readyRole = set ? set->lifecycle.ready : RuntimePlaybackViewLifecycle::kNoView;
    const RuntimePlaybackView* ready = set && readyRole < set->views.size()
        ? &set->views[readyRole] : nullptr;
    char earlyDetail[220];
    snprintf(earlyDetail, sizeof(earlyDetail),
        "bar=%u phase=%u ready=%u readyStart=%u readyCount=%u current=%u build=%u offer=%u/%u pending=%u jobPhase=%u",
        static_cast<unsigned>(index), static_cast<unsigned>(runtime.rolling.phase), readyRole,
        ready ? ready->coverage.startPhase : 0u,
        ready ? static_cast<unsigned>(ready->storage.eventCount) : 0u,
        set ? set->lifecycle.current : 255u, set ? set->lifecycle.build : 255u,
        offer.eligible, offer.canProgress, t.pendingViewRequest(0) != nullptr,
        static_cast<unsigned>(job.admission.phase));
    TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(LoopPlaybackRuntime::RollingPhase::Serving),
        static_cast<int>(runtime.rolling.phase), earlyDetail);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(expectedBefore + 32, MidiSendCapture::count(),
        "all 16 accepted early-bar On/Off pairs must sound on their first traversal");
    for (uint32_t note = 0; note < 16; ++note) {
      const MidiEvent& on = MidiSendCapture::at(expectedBefore + 2 * note);
      const MidiEvent& off = MidiSendCapture::at(expectedBefore + 2 * note + 1);
      TEST_ASSERT_TRUE(on.isNoteOn() && off.isNoteOff());
      TEST_ASSERT_EQUAL_UINT8(1, on.channel);
      TEST_ASSERT_EQUAL_UINT8(36 + note, on.data.noteData.note);
      TEST_ASSERT_EQUAL_UINT8(36 + note, off.data.noteData.note);
      TEST_ASSERT_EQUAL_UINT32(index * bar + 12 + note * 48, on.tick);
      TEST_ASSERT_EQUAL_UINT32(index * bar + 32 + note * 48, off.tick);
    }
    unsigned sounding = 0;
    runtime.ledger.forEachActive([&](uint8_t, uint8_t, const ActiveNoteLedger::Entry&) {
      ++sounding;
    });
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, sounding,
        "all accepted early-bar NoteOffs must close their reader-owned notes");
    const uint32_t stableCount = MidiSendCapture::count();
    t.playMidiEvents(length + (index + 1) * bar - 1, true);
    TEST_ASSERT_EQUAL_UINT32(stableCount, MidiSendCapture::count());
  }
}
// Device-shaped, non-aligned stop: only production Record, Track owner and clock
// reader may construct/adopt views. The root stays private throughout the return.
static void exerciseNonAlignedRecordReturn(bool readyBeforeWrap, bool skipBarZero = false) {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  constexpr uint32_t length = 25 * bar;
  constexpr uint32_t stopTick = 18922;
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 36);
  for (uint32_t b = 0; b < 24; ++b) {
    for (uint32_t n = 0; n < 16; ++n) {
      if (b == 0 && n == 0) continue;
      const uint32_t on = b * bar + 12 + n * 48;
      const uint8_t pitch = static_cast<uint8_t>(36 + n);
      t.noteOn(1, pitch, 100, on);
      t.noteOff(1, pitch, 0, on + 20);
    }
  }
  t.noteOn(1, 70, 100, 24 * bar + 300);
  t.noteOff(1, 70, 0, 24 * bar + 320);
  Loop& loop = t.getLoop(0);
  loop.loopLengthTicks = length;
  clockManager.assignCurrentTickSilently(bar);
  for (unsigned i = 0; i < 64; ++i)
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::VisualCache, 1);
  clockManager.assignCurrentTickSilently(stopTick);
  t.stopRecording(stopTick);
  TEST_ASSERT_TRUE(t.isPlaying());
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(length, loop.loopLengthTicks,
      "real Record stop must preserve 25-bar geometry, not the input raw tick");
  auto& job = TrackCaptureStopTestAccess::head(t);
  TEST_ASSERT_EQUAL_UINT32(length, job.loopLengthTicks);
  TEST_ASSERT_EQUAL_UINT32(stopTick, job.phaseTick);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot) && TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.ready());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  auto* set = runtime.viewSetIfPrewarmed();
  TEST_ASSERT_NOT_NULL(set);
  RuntimePlaybackViewSource frozen{};
  PlaybackViewSourceIdentity receipt{};
  TEST_ASSERT_TRUE(t.qualifiedFrozenRecordCaptureSourceForSlot(0, job.interval, frozen, receipt));
  TEST_ASSERT_EQUAL_PTR(&TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange, frozen.context);
  const auto readyBarZero = [&]() {
    if (set->lifecycle.ready >= set->views.size()) return false;
    const auto& view = set->views[set->lifecycle.ready];
    return view.complete && !view.published && view.source == receipt &&
        view.coverage.startPhase == 0 && view.coverage.lengthTicks == bar &&
        view.coverage.loopLengthTicks == length && view.storage.eventCount == 32;
  };
  nativePlaybackDeadlineClock = {stopTick, 1};
  clockManager.assignCurrentTickSilently(stopTick);
  MidiSendCapture::reset();
  t.playMidiEvents(stopTick, true);
  MidiSendCapture::reset();
  const uint32_t preTick = length - 1;
  nativePlaybackDeadlineClock.tick = preTick;
  clockManager.assignCurrentTickSilently(preTick);
  for (unsigned i = 0; i < (readyBeforeWrap ? 128u : 1u); ++i)
    (void)t.stepPlaybackViewMaintenance(preTick, nullptr, true, nativePlaybackDeadlineClock);
  const uint8_t beforeCurrent = set->lifecycle.current;
  const uint64_t beforeStorage = beforeCurrent < set->views.size()
      ? set->views[beforeCurrent].storageGeneration : 0;
  if (readyBeforeWrap) {
    char detail[240];
    const auto offer = t.playbackWindowServiceOffer(preTick, nativePlaybackDeadlineClock);
    snprintf(detail, sizeof(detail), "before wrap: ready=%u current=%u build=%u offer=%u/%u rolling=%u pending=%u job=%u root=%u",
        set->lifecycle.ready, beforeCurrent, set->lifecycle.build, offer.eligible, offer.canProgress,
        static_cast<unsigned>(runtime.rolling.phase), t.pendingViewRequest(0) != nullptr,
        static_cast<unsigned>(job.admission.phase), loop.publishedSnapshotIsCurrent());
    TEST_ASSERT_TRUE_MESSAGE(readyBarZero(), detail);
  }
  nativePlaybackDeadlineClock.tick = length;
  clockManager.assignCurrentTickSilently(length);
  t.playMidiEvents(length, true);
  if (!readyBeforeWrap) {
    for (unsigned i = 0; i < 128 && !readyBarZero(); ++i) {
      nativePlaybackDeadlineClock.tick = length + 1;
      clockManager.assignCurrentTickSilently(length + 1);
      (void)t.stepPlaybackViewMaintenance(length + 1, nullptr, true, nativePlaybackDeadlineClock);
    }
  }
  const auto offer = t.playbackWindowServiceOffer(length + 1, nativePlaybackDeadlineClock);
  char detail[256];
  snprintf(detail, sizeof(detail), "after wrap: ready=%u current=%u build=%u offer=%u/%u rolling=%u pending=%u job=%u root=%u",
      set->lifecycle.ready, set->lifecycle.current, set->lifecycle.build, offer.eligible, offer.canProgress,
      static_cast<unsigned>(runtime.rolling.phase), t.pendingViewRequest(0) != nullptr,
      static_cast<unsigned>(job.admission.phase), loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_TRUE_MESSAGE(readyBarZero() ||
      (readyBeforeWrap && runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Serving), detail);
  if (!readyBeforeWrap) {
    TEST_ASSERT_TRUE_MESSAGE(readyBarZero(), detail);
    TEST_ASSERT_EQUAL(PlaybackWindowOrigin::Capture,
                      set->views[set->lifecycle.ready].source.origin);
    TEST_ASSERT_EQUAL_UINT64(job.frozenCaptureContentGeneration,
                             set->views[set->lifecycle.ready].source.captureContentGeneration);
    TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot) && TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.ready());
    TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  }
  if (skipBarZero) {
    // A seek/clock step can pass the entire bar between owner turns. The old
    // ready is still job-qualified but no longer covers the playhead.
    nativePlaybackDeadlineClock.tick = length + bar + 2;
    clockManager.assignCurrentTickSilently(length + bar + 2);
    t.playMidiEvents(length + bar + 2, true);
    PlaybackPathRows::reset();
    (void)t.stepPlaybackViewMaintenance(length + bar + 2, nullptr, true,
                                        nativePlaybackDeadlineClock);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, PlaybackPathRows::countOf("record_ready_late"),
        "a skipped frozen bar must name its missed boundary once");
    TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot) && TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.ready());
    TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
    return;
  }
  if (!readyBeforeWrap) {
    TEST_ASSERT_EQUAL_UINT8(beforeCurrent, set->lifecycle.current);
    TEST_ASSERT_EQUAL_UINT64(beforeStorage, set->views[beforeCurrent].storageGeneration);
    nativePlaybackDeadlineClock.tick = length + 2;
    clockManager.assignCurrentTickSilently(length + 2);
    t.playMidiEvents(length + 2, true);
    // A completion after the bar-zero boundary is late. Never adopt it at
    // phase 2 or replay notes from phase 12; bar one is the next obligation.
    TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::RollingPhase::Idle),
                      static_cast<int>(runtime.rolling.phase));
    TEST_ASSERT_EQUAL_UINT32(0u, MidiSendCapture::count());
    const auto nextOffer = t.playbackWindowServiceOffer(length + 2, nativePlaybackDeadlineClock);
    TEST_ASSERT_TRUE_MESSAGE(nextOffer.eligible && nextOffer.canProgress,
        "a late bar-zero ready must not mask frozen bar one's holder");
    PlaybackPathRows::reset();
    // Grant no more than one owner quantum per 64 advanced ticks. Repeated
    // turns at a frozen clock stamp would hide a hardware service debt.
    for (uint32_t tick = length + 2; tick < length + bar; tick += 64) {
      nativePlaybackDeadlineClock.tick = tick;
      clockManager.assignCurrentTickSilently(tick);
      (void)t.stepPlaybackViewMaintenance(tick, nullptr, true, nativePlaybackDeadlineClock);
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, PlaybackPathRows::countOf("record_ready_late"),
        "the missed frozen bar must be named once on off-clock retirement");
    TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot) && TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.ready());
    TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
    TEST_ASSERT_TRUE_MESSAGE(set->lifecycle.ready < set->views.size(),
        "bar one needs a complete frozen successor before its boundary");
    const auto& next = set->views[set->lifecycle.ready];
    TEST_ASSERT_TRUE(next.complete && !next.published && next.source == receipt);
    TEST_ASSERT_EQUAL_UINT32(bar, next.coverage.startPhase);
    TEST_ASSERT_EQUAL_UINT32(bar, next.coverage.lengthTicks);
    const uint8_t nextRole = set->lifecycle.ready;
    const uint64_t nextStorage = next.storageGeneration;
    nativePlaybackDeadlineClock.tick = length + bar;
    clockManager.assignCurrentTickSilently(length + bar);
    t.playMidiEvents(length + bar, true);
    TEST_ASSERT_EQUAL(static_cast<int>(LoopPlaybackRuntime::RollingPhase::Serving),
                      static_cast<int>(runtime.rolling.phase));
    TEST_ASSERT_EQUAL_UINT8(nextRole, set->lifecycle.current);
    TEST_ASSERT_EQUAL_UINT64(nextStorage, set->views[nextRole].storageGeneration);
    TEST_ASSERT_EQUAL_UINT32(0u, MidiSendCapture::count());
    for (uint32_t offset : {12u, 32u}) {
      nativePlaybackDeadlineClock.tick = length + bar + offset;
      clockManager.assignCurrentTickSilently(length + bar + offset);
      t.playMidiEvents(length + bar + offset, true);
      TEST_ASSERT_EQUAL_UINT32(offset == 12u ? 1u : 2u, MidiSendCapture::count());
      const MidiEvent& event = MidiSendCapture::at(offset == 12u ? 0u : 1u);
      TEST_ASSERT_EQUAL_UINT32(bar + offset, event.tick);
      TEST_ASSERT_EQUAL_UINT8(1u, event.channel);
      TEST_ASSERT_EQUAL_UINT8(36u, event.data.noteData.note);
      TEST_ASSERT_EQUAL(offset == 12u, event.isNoteOn());
      TEST_ASSERT_EQUAL(offset == 32u, event.isNoteOff());
      t.playMidiEvents(length + bar + offset, true);
      TEST_ASSERT_EQUAL_UINT32(offset == 12u ? 1u : 2u, MidiSendCapture::count());
    }
    unsigned sounding = 0;
    runtime.ledger.forEachActive([&](uint8_t, uint8_t, const ActiveNoteLedger::Entry&) {
      ++sounding;
    });
    TEST_ASSERT_EQUAL_UINT32(0u, sounding);
    return;
  }
  char readerDetail[320];
  snprintf(readerDetail, sizeof(readerDetail),
      "reader: before=%u/%llu now=%u ready=%u build=%u phase=%u bound=%u count=%zu job=%u root=%u offer=%u/%u",
      beforeCurrent, static_cast<unsigned long long>(beforeStorage), set->lifecycle.current,
      set->lifecycle.ready, set->lifecycle.build, static_cast<unsigned>(runtime.rolling.phase),
      runtime.rolling.boundView, MidiSendCapture::count(), static_cast<unsigned>(job.admission.phase),
      loop.publishedSnapshotIsCurrent(), offer.eligible, offer.canProgress);
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(LoopPlaybackRuntime::RollingPhase::Serving),
                            static_cast<int>(runtime.rolling.phase), readerDetail);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, MidiSendCapture::count(), readerDetail);
  const uint32_t edgeTicks[] = {length + 12, length + 60, length + 80, length + 96};
  for (unsigned i = 0; i < 4; ++i) {
    nativePlaybackDeadlineClock.tick = edgeTicks[i];
    clockManager.assignCurrentTickSilently(edgeTicks[i]);
    t.playMidiEvents(edgeTicks[i], true);
    char eventDetail[300];
    snprintf(eventDetail, sizeof(eventDetail),
        "edge=%u count=%zu phase=%u current=%u ready=%u bound=%u first=%u/%u/%u second=%u/%u/%u root=%u",
        edgeTicks[i], MidiSendCapture::count(), static_cast<unsigned>(runtime.rolling.phase),
        set->lifecycle.current, set->lifecycle.ready, runtime.rolling.boundView,
        MidiSendCapture::count() ? MidiSendCapture::at(0).tick : 0u,
        MidiSendCapture::count() ? MidiSendCapture::at(0).data.noteData.note : 0u,
        MidiSendCapture::count() ? MidiSendCapture::at(0).isNoteOn() : 0,
        MidiSendCapture::count() > 1 ? MidiSendCapture::at(1).tick : 0u,
        MidiSendCapture::count() > 1 ? MidiSendCapture::at(1).data.noteData.note : 0u,
        MidiSendCapture::count() > 1 ? MidiSendCapture::at(1).isNoteOn() : 0,
        loop.publishedSnapshotIsCurrent());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(i + 1, MidiSendCapture::count(), eventDetail);
    const auto& event = MidiSendCapture::at(i);
    TEST_ASSERT_EQUAL_UINT32(edgeTicks[i] - length, event.tick);
    TEST_ASSERT_EQUAL_UINT8(1, event.channel);
    TEST_ASSERT_EQUAL_UINT8(i == 1 || i == 2 ? 37 : 36, event.data.noteData.note);
    TEST_ASSERT_EQUAL(i < 2, event.isNoteOn());
    TEST_ASSERT_EQUAL(i >= 2, event.isNoteOff());
    unsigned sounding = 0;
    runtime.ledger.forEachActive([&](uint8_t channel, uint8_t note, const ActiveNoteLedger::Entry&) {
      ++sounding;
      TEST_ASSERT_EQUAL_UINT8(1, channel);
      TEST_ASSERT_TRUE(note == 36 || (i == 1 && note == 37));
    });
    TEST_ASSERT_EQUAL_UINT32(i == 0 ? 1u : i == 1 ? 2u : i == 2 ? 1u : 0u, sounding);
    t.playMidiEvents(edgeTicks[i], true);
    TEST_ASSERT_EQUAL_UINT32(i + 1, MidiSendCapture::count());
  }
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot) && TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.ready());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  // The next bar has its own holder obligation at the following edge.
  nativePlaybackDeadlineClock.tick = length + bar - 1;
  clockManager.assignCurrentTickSilently(length + bar - 1);
  const uint8_t servedRole = set->lifecycle.current;
  const uint64_t servedStorage = set->views[servedRole].storageGeneration;
  const auto nextOffer = t.playbackWindowServiceOffer(length + bar - 1, nativePlaybackDeadlineClock);
  TEST_ASSERT_TRUE_MESSAGE(nextOffer.eligible && nextOffer.canProgress,
      "frozen bar one must be offered to the Track holder before its next edge");
  for (unsigned i = 0; i < 128; ++i)
    (void)t.stepPlaybackViewMaintenance(length + bar - 1, nullptr, true, nativePlaybackDeadlineClock);
  TEST_ASSERT_TRUE_MESSAGE(set->lifecycle.ready < set->views.size() &&
      set->views[set->lifecycle.ready].complete &&
      set->views[set->lifecycle.ready].source == receipt &&
      set->views[set->lifecycle.ready].coverage.startPhase == bar,
      "Track owner must offer and ready the next frozen Capture bar before its edge");
  const uint8_t nextRole = set->lifecycle.ready;
  const uint64_t nextStorage = set->views[nextRole].storageGeneration;
  TEST_ASSERT_EQUAL_UINT8(servedRole, set->lifecycle.current);
  TEST_ASSERT_EQUAL_UINT64(servedStorage, set->views[servedRole].storageGeneration);
  nativePlaybackDeadlineClock.tick = length + bar;
  clockManager.assignCurrentTickSilently(length + bar);
  t.playMidiEvents(length + bar, true);
  TEST_ASSERT_EQUAL_UINT8(nextRole, set->lifecycle.current);
  TEST_ASSERT_EQUAL_UINT64(nextStorage, set->views[nextRole].storageGeneration);
  TEST_ASSERT_TRUE(set->views[nextRole].source == receipt);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::extentOwned(t, job.slot) && TrackCaptureStopTestAccess::presentation(t, job.slot)->rawRange.ready());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
}
void test_non_aligned_record_return_has_bar_zero_ready_before_wrap() {
  exerciseNonAlignedRecordReturn(true);
}
void test_non_aligned_record_return_accepts_bar_zero_ready_after_wrap() {
  exerciseNonAlignedRecordReturn(false);
}
void test_non_aligned_record_return_reports_ready_skipped_by_clock_step() {
  exerciseNonAlignedRecordReturn(false, true);
}
#endif

// 3.10 — a rootless committed demand with no retained source genuinely needs
// the separate Snapshot owner before Track can prepare its interval window.
void test_rootless_committed_window_waits_for_needed_snapshot_then_track_owner() {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  constexpr uint32_t length = 24 * bar;
  LoopEventStore::initPool();
  Track& t = trackManager.getTrack(0);
  t.ensureLoopsAllocated();
  t.setActiveLoopIndex(0);
  Loop& loop = t.getLoop(0);
  loop.loopLengthTicks = length;
  loop.beginCapture(CapturePhase::Record);
  for (uint32_t index = 0; index < 2; ++index) {
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(index * bar, 1, 48, 100)));
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(index * bar + 48, 1, 48, 0)));
  }
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(length, CommitReason::RecordStop));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass());
  TEST_ASSERT_TRUE(t.prewarmPlaybackForSlot(0, true));
  if (!loop.hasPendingSnapshotPublish())
    TEST_ASSERT_TRUE(loop.requestCommittedPassesRepublish());
  TEST_ASSERT_TRUE(loop.hasPendingSnapshotPublish());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_FALSE(t.playbackWindowSourceIsCurrentForSlot(0));
  t.forceSetState(TRACK_PLAYING);
  const PlaybackCoverageInterval coverage{0, 2 * bar, length};
  const PlaybackDeadlineClock stamp{1000, 1};
  clockManager.assignCurrentTickSilently(static_cast<uint32_t>(stamp.tick));
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::DeferredSourceQualification),
      static_cast<int>(t.requestCommittedPlaybackWindow(0, coverage, 13 * bar, stamp)));
  const auto* pending = t.pendingViewRequest(0);
  TEST_ASSERT_NOT_NULL(pending);
  const uint32_t revision = loop.playbackRevision;
  const uint32_t generation = TrackCaptureStopTestAccess::playbackGenerationOf(t);
  TEST_ASSERT_EQUAL_UINT32(revision, pending->source.playbackRevision);
  TEST_ASSERT_EQUAL_UINT32(length, pending->coverage.loopLengthTicks);
  for (unsigned turn = 0; turn < 16; ++turn)
    (void)t.stepPlaybackViewMaintenance(0, nullptr, true, stamp);
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_FALSE(t.playbackWindowSourceIsCurrentForSlot(0));
  TEST_ASSERT_NOT_NULL_MESSAGE(t.pendingViewRequest(0),
      "window turns cannot discharge a source-less pending interval");
  TEST_ASSERT_EQUAL(static_cast<int>(PlaybackViewPendingReason::AwaitingSourceQualification),
      static_cast<int>(t.pendingViewRequest(0)->reason));
  PlaybackWindowIdentity published{};
  TEST_ASSERT_FALSE(t.publishedPlaybackWindowIdentityForSlot(0, published));

  unsigned snapshotTurns = 0;
  for (; snapshotTurns < 4096 && !loop.publishedSnapshotIsCurrent(); ++snapshotTurns)
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
  TEST_ASSERT_TRUE_MESSAGE(loop.publishedSnapshotIsCurrent(),
      "only the bounded Track Snapshot owner publishes the needed complete source");
  TEST_ASSERT_TRUE(snapshotTurns > 0);
  TEST_ASSERT_TRUE(t.playbackWindowSourceIsCurrentForSlot(0));
  TEST_ASSERT_NOT_NULL_MESSAGE(t.pendingViewRequest(0),
      "source publication alone does not discharge the separate window demand");
  TEST_ASSERT_EQUAL(static_cast<int>(PlaybackViewPendingReason::AwaitingSourceQualification),
      static_cast<int>(t.pendingViewRequest(0)->reason));
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  const auto* beforeWindow = runtime.viewSetIfPrewarmed();
  TEST_ASSERT_NOT_NULL(beforeWindow);
  TEST_ASSERT_FALSE(beforeWindow->views[beforeWindow->lifecycle.current].complete);
  TEST_ASSERT_FALSE(t.publishedPlaybackWindowIdentityForSlot(0, published));
  unsigned windowTurns = 0;
  for (; windowTurns < 512 && !t.publishedPlaybackWindowIdentityForSlot(0, published); ++windowTurns)
    (void)t.stepPlaybackViewMaintenance(0, nullptr, true, stamp,
        []() { return PlaybackDeadlineClock{1000, 1}; });
  const auto* set = runtime.viewSetIfPrewarmed();
  TEST_ASSERT_NOT_NULL(set);
  const auto* owing = t.pendingViewRequest(0);
  char detail[230];
  snprintf(detail, sizeof(detail), "source=%u snap=%u turns=%u pending=%u reason=%u build=%u current=%u ready=%u",
      t.playbackWindowSourceIsCurrentForSlot(0), loop.publishedSnapshotIsCurrent(), windowTurns,
      owing != nullptr, owing ? static_cast<unsigned>(owing->reason) : 0u,
      runtime.viewBuild.active(), set->lifecycle.current, set->lifecycle.ready);
  TEST_ASSERT_TRUE_MESSAGE(t.publishedPlaybackWindowIdentityForSlot(0, published), detail);
  TEST_ASSERT_TRUE_MESSAGE(windowTurns > 0, "a needed window must consume its own owner opportunities");
  TEST_ASSERT_EQUAL_UINT32(revision, published.playbackRevision);
  TEST_ASSERT_NULL(t.pendingViewRequest(0));
  const auto& current = set->views[set->lifecycle.current];
  TEST_ASSERT_TRUE(current.complete && current.published);
  TEST_ASSERT_EQUAL_PTR(loop.publishedSnapshotIdentity(), current.source.snapshotIdentity);
  TEST_ASSERT_EQUAL_UINT32(generation, current.source.sourceGeneration);
  TEST_ASSERT_EQUAL_UINT32(coverage.startPhase, current.coverage.startPhase);
  TEST_ASSERT_EQUAL_UINT32(coverage.lengthTicks, current.coverage.lengthTicks);
  TEST_ASSERT_EQUAL_UINT32(4u, current.storage.eventCount);
  const uint32_t expected[] = {0u, 48u, bar, bar + 48u};
  for (size_t i = 0; i < 4; ++i) {
    const MidiEvent& event = current.storage.events[current.storage.order[i]];
    TEST_ASSERT_EQUAL_UINT32(expected[i], event.tick);
    TEST_ASSERT_EQUAL_UINT8(48, event.data.noteData.note);
    TEST_ASSERT_EQUAL(i % 2 == 0, event.isNoteOn());
    TEST_ASSERT_EQUAL(i % 2 == 1, event.isNoteOff());
  }
}

// Host clock observation for the *actual completion turn* of a production window grain.
static PlaybackDeadlineClock sComposedOwnerClock{};

// 3.10: compose a genuinely needed Snapshot source and the Track window owner
// through the existing seven-kind selector. The second variant also dispatches
// another Track's actual deferred Record stop on its eligible owner turns.
static void runRootlessSourceAndWindowUnderOwnerRotation(bool withFinalization) {
  constexpr uint32_t bar = Config::TICKS_PER_BAR;
  constexpr uint32_t length = 24 * bar;
  LoopEventStore::initPool();
  Track& t = trackManager.getTrack(0);
  t.ensureLoopsAllocated();
  t.setActiveLoopIndex(0);
  Loop& loop = t.getLoop(0);
  loop.loopLengthTicks = length;
  loop.beginCapture(CapturePhase::Record);
  for (uint32_t index = 0; index < 2; ++index) {
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(index * bar, 1, 48, 100)));
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(index * bar + 48, 1, 48, 0)));
  }
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(length, CommitReason::RecordStop));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass());
  if (!loop.hasPendingSnapshotPublish())
    TEST_ASSERT_TRUE(loop.requestCommittedPassesRepublish());
  TEST_ASSERT_TRUE(t.prewarmPlaybackForSlot(0, true));
  TEST_ASSERT_FALSE(t.playbackWindowSourceIsCurrentForSlot(0));
  t.forceSetState(TRACK_PLAYING);
  Track* stopping = nullptr;
  if (withFinalization) {
    Track& other = trackManager.getTrack(1);
    prepareSlot(other, 0, false);
    other.forceSetState(TRACK_STOPPED);
    TEST_ASSERT_FALSE_MESSAGE(other.hasRealtimeDeferredIdleWork(),
        "a stopped Track without an accepted job has no realtime deferred obligation");
    start(other, 0, false, 0, 60);
    TEST_ASSERT_FALSE_MESSAGE(trackManager.backgroundSnapshotWorkAllowed(),
        "a live Record Capture keeps background Snapshot publication gated");
    stop(other, false, bar); // production playing/stop owner, independent of Track 0's interval
    TEST_ASSERT_TRUE(other.hasPendingDeferredOverdubStop());
    TEST_ASSERT_EQUAL(PendingStopPhase::ReservedClosure, TrackCaptureStopTestAccess::phase(other));
    TEST_ASSERT_FALSE_MESSAGE(trackManager.backgroundSnapshotWorkAllowed(),
        "a pending stop before RecordPublished still excludes background Snapshot work");
    TEST_ASSERT_TRUE(other.isStopped());
    TEST_ASSERT_FALSE(other.isStoppedRecording());
    TEST_ASSERT_TRUE_MESSAGE(other.hasRealtimeDeferredIdleWork(),
        "a pending stopped Record job must remain reachable while another Track plays");
    stopping = &other;
  }
  const PlaybackCoverageInterval coverage{0, 2 * bar, length};
  PlaybackDeadlineClock clock{4 * bar, 1};
  clockManager.assignCurrentTickSilently(static_cast<uint32_t>(clock.tick));
  TEST_ASSERT_EQUAL(static_cast<int>(Track::PlaybackViewRequestResult::DeferredSourceQualification),
      static_cast<int>(t.requestCommittedPlaybackWindow(0, coverage, 4 * bar, clock)));
  const auto* request = t.pendingViewRequest(0);
  TEST_ASSERT_NOT_NULL(request);
  const uint64_t deadline = request->deadlineTick;
  TEST_ASSERT_TRUE(deadline > clock.tick);
  const uint32_t revision = loop.playbackRevision;
  const uint32_t generation = TrackCaptureStopTestAccess::playbackGenerationOf(t);
  MidiServiceDrain::DeferredIdleOwnerCursor ownerCursor{};
  MidiServiceDrain::PlaybackHolderCursor trackCursor{};
  uint32_t opportunities[MidiServiceDrain::kDeferredIdleOwnerCount] = {};
  uint32_t snapshotProgress = 0;
  uint32_t windowProgress = 0;
  uint32_t finalizationGrants = 0;
  uint64_t sourceReadyTick = UINT64_MAX;
  uint64_t windowReadyTick = UINT64_MAX;
  PlaybackWindowIdentity published{};
  for (; clock.tick < deadline && windowReadyTick == UINT64_MAX;
       clock.tick += Track::kPlaybackViewPermittedAdvancePerTurn) {
    clockManager.assignCurrentTickSilently(static_cast<uint32_t>(clock.tick));
    const auto owner = MidiServiceDrain::selectDeferredIdleOwner(
        ownerCursor, t.hasPendingSnapshotPublishOnAnySlot(), false);
    ++opportunities[static_cast<uint8_t>(owner)];
    if (owner == MidiServiceDrain::DeferredIdleOwner::Snapshot) {
      t.processDeferredIdleMaintenance(0, false, owner, 1);
      if (loop.publishedSnapshotIsCurrent() && sourceReadyTick == UINT64_MAX)
        sourceReadyTick = clock.tick;
      if (sourceReadyTick == UINT64_MAX) ++snapshotProgress;
    } else if (owner == MidiServiceDrain::DeferredIdleOwner::PlaybackViewMaintenance) {
      MidiServiceDrain::PlaybackHolder offers[Config::NUM_TRACKS] = {};
      const uint8_t trackCount = trackManager.getTrackCount();
      const uint32_t phase = static_cast<uint32_t>(clock.tick % length);
      for (uint8_t i = 0; i < trackCount; ++i) {
        Track& candidate = trackManager.getTrack(i);
        candidate.serviceAdmittedDeadlineMisses(clock);
        offers[i] = candidate.playbackWindowServiceOffer(phase, clock);
      }
      uint8_t chosen = MidiServiceDrain::kNoPlaybackHolder;
      if (MidiServiceDrain::selectPlaybackHolderRoundRobin(trackCursor, offers, trackCount, chosen) ==
          MidiServiceDrain::PlaybackHolderSelection::Service) {
        sComposedOwnerClock = clock;
        const auto result = trackManager.getTrack(chosen).stepPlaybackViewMaintenance(
            phase, nullptr, true, clock, []() { return sComposedOwnerClock; });
        if (result == Track::PlaybackViewTurnResult::GrainServed ||
            result == Track::PlaybackViewTurnResult::BuildComplete) ++windowProgress;
      }
      if (sourceReadyTick == UINT64_MAX) {
        TEST_ASSERT_NOT_NULL(t.pendingViewRequest(0));
        TEST_ASSERT_FALSE(t.publishedPlaybackWindowIdentityForSlot(0, published));
      } else if (t.publishedPlaybackWindowIdentityForSlot(0, published)) {
        windowReadyTick = clock.tick;
      }
    }
    if (withFinalization && (owner == MidiServiceDrain::DeferredIdleOwner::VisualCache ||
                             owner == MidiServiceDrain::DeferredIdleOwner::VisualCacheWrapAppend)) {
      // `runDeferredIdleMaintenance` visits realtime-admitted tracks on these
      // ordinary owner turns; an accepted stop consumes one real grain there.
      for (uint8_t i = 0; i < trackManager.getTrackCount(); ++i) {
        Track& candidate = trackManager.getTrack(i);
        if (!candidate.hasRealtimeDeferredIdleWork()) continue;
        if (stopping == &candidate && candidate.hasPendingDeferredOverdubStop()) ++finalizationGrants;
        candidate.processDeferredIdleMaintenance(0, false, owner, 1);
      }
    }
  }
  char detail[230];
  snprintf(detail, sizeof(detail),
      "clock=%llu deadline=%llu source=%llu window=%llu snapTurns=%u winTurns=%u selectedSnap=%u selectedWin=%u persist=%u load=%u pending=%u",
      static_cast<unsigned long long>(clock.tick), static_cast<unsigned long long>(deadline),
      static_cast<unsigned long long>(sourceReadyTick), static_cast<unsigned long long>(windowReadyTick),
      snapshotProgress, windowProgress,
      opportunities[static_cast<uint8_t>(MidiServiceDrain::DeferredIdleOwner::Snapshot)],
      opportunities[static_cast<uint8_t>(MidiServiceDrain::DeferredIdleOwner::PlaybackViewMaintenance)],
      opportunities[static_cast<uint8_t>(MidiServiceDrain::DeferredIdleOwner::Persistence)],
      opportunities[static_cast<uint8_t>(MidiServiceDrain::DeferredIdleOwner::LoadAndDisplay)],
      t.pendingViewRequest(0) != nullptr);
  TEST_ASSERT_TRUE_MESSAGE(sourceReadyTick != UINT64_MAX && windowReadyTick != UINT64_MAX &&
      sourceReadyTick < windowReadyTick && windowReadyTick < deadline, detail);
  TEST_ASSERT_TRUE(snapshotProgress > 0 && windowProgress > 0);
  for (uint8_t i = 0; i < MidiServiceDrain::kDeferredIdleOwnerCount; ++i)
    TEST_ASSERT_TRUE_MESSAGE(opportunities[i] > 0, "every deferred owner kind keeps its selection opportunity");
  if (withFinalization) {
    TEST_ASSERT_NOT_NULL(stopping);
    TEST_ASSERT_TRUE_MESSAGE(finalizationGrants > 0,
        "the independent admitted Record stop receives real deferred owner grains");
    const bool advanced = stopping->getLoop(0).passes.hasRecordPass() ||
        (stopping->hasPendingDeferredOverdubStop() &&
         TrackCaptureStopTestAccess::phase(*stopping) != PendingStopPhase::ReservedClosure);
    TEST_ASSERT_TRUE_MESSAGE(advanced,
        "the competing stop advances its real phase without stealing Snapshot/window turns");
  }
  TEST_ASSERT_NULL(t.pendingViewRequest(0));
  TEST_ASSERT_EQUAL_UINT32(revision, published.playbackRevision);
  TEST_ASSERT_EQUAL_UINT8(0, published.slot);
  TEST_ASSERT_EQUAL_UINT32(generation, published.playbackGeneration);
  TEST_ASSERT_EQUAL_UINT32(coverage.startPhase, published.interval.startPhase);
  TEST_ASSERT_EQUAL_UINT32(coverage.lengthTicks, published.interval.lengthTicks);
  TEST_ASSERT_EQUAL_UINT32(coverage.loopLengthTicks, published.interval.loopLengthTicks);
  TEST_ASSERT_EQUAL(static_cast<int>(PlaybackWindowOrigin::Committed), static_cast<int>(published.origin));
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  const auto* set = runtime.viewSetIfPrewarmed();
  TEST_ASSERT_NOT_NULL(set);
  const auto& current = set->views[set->lifecycle.current];
  TEST_ASSERT_TRUE(current.complete && current.published);
  TEST_ASSERT_EQUAL_UINT32(generation, current.source.sourceGeneration);
  TEST_ASSERT_EQUAL_PTR(loop.publishedSnapshotIdentity(), current.source.snapshotIdentity);
  TEST_ASSERT_EQUAL_UINT32(revision, current.source.playbackRevision);
  TEST_ASSERT_EQUAL_UINT32(coverage.startPhase, current.coverage.startPhase);
  TEST_ASSERT_EQUAL_UINT32(coverage.lengthTicks, current.coverage.lengthTicks);
  TEST_ASSERT_EQUAL_UINT32(4u, current.storage.eventCount);
  const uint32_t expected[] = {0u, 48u, bar, bar + 48u};
  for (size_t i = 0; i < 4; ++i) {
    const MidiEvent& event = current.storage.events[current.storage.order[i]];
    TEST_ASSERT_EQUAL_UINT32(expected[i], event.tick);
    TEST_ASSERT_EQUAL_UINT8(48, event.data.noteData.note);
    TEST_ASSERT_EQUAL_UINT8(1, event.channel);
    TEST_ASSERT_EQUAL_UINT8(i % 2 == 0 ? 100 : 0, event.data.noteData.velocity);
    TEST_ASSERT_EQUAL(i % 2 == 0, event.isNoteOn());
    TEST_ASSERT_EQUAL(i % 2 == 1, event.isNoteOff());
  }
  const uint8_t servedRole = set->lifecycle.current;
  if (withFinalization) {
    TEST_ASSERT_NOT_NULL(stopping);
    TEST_ASSERT_TRUE(stopping->hasPendingDeferredOverdubStop());
    TEST_ASSERT_FALSE_MESSAGE(stopping->canServiceBackgroundSnapshotOwnerTurn(),
        "an earlier stop phase cannot borrow Snapshot publication ahead of its owner");
    const uint64_t stopInterval = TrackCaptureStopTestAccess::head(*stopping).interval;
    const uint64_t diagnosticCap = TrackCaptureStopTestAccess::budget(*stopping) *
        MidiServiceDrain::kDeferredIdleOwnerCount * 8u;
    uint64_t drainTurns = 0;
    uint32_t backgroundSnapshotGrants = 0;
    uint32_t continuedStopGrants = 0;
    for (; drainTurns < diagnosticCap && TrackCaptureStopTestAccess::recordWorkPending(*stopping);
         ++drainTurns, clock.tick += Track::kPlaybackViewPermittedAdvancePerTurn) {
      clockManager.assignCurrentTickSilently(static_cast<uint32_t>(clock.tick));
      const bool backlog = t.hasSnapshotPublicationBacklogOnAnySlot() ||
                           stopping->hasSnapshotPublicationBacklogOnAnySlot();
      const auto owner = MidiServiceDrain::selectDeferredIdleOwner(ownerCursor, backlog, false);
      if (owner == MidiServiceDrain::DeferredIdleOwner::Snapshot) {
        // Main serves the selected realtime Track first, then one bounded
        // background Track. A stopped RecordPublished job needs that latter turn.
        t.processDeferredIdleMaintenance(0, false, owner, 1);
        const bool needsRoot = TrackCaptureStopTestAccess::phase(*stopping) ==
                PendingStopPhase::RecordPublished &&
            stopping->getLoop(0).pendingSnapshotSessionExitQueued() &&
            !stopping->getLoop(0).publishedSnapshotIsCurrent();
        if (needsRoot) {
          TEST_ASSERT_TRUE_MESSAGE(trackManager.backgroundSnapshotWorkAllowed(),
              "the stopped RecordPublished root must pass the global background Snapshot gate");
          TEST_ASSERT_TRUE_MESSAGE(stopping->canServiceBackgroundSnapshotOwnerTurn(),
              "the stopped RecordPublished job must be eligible for its queued root's Snapshot owner");
        }
        if (trackManager.backgroundSnapshotWorkAllowed() &&
            stopping->canServiceBackgroundSnapshotOwnerTurn()) {
          stopping->processDeferredIdleMaintenance(0, false, owner, 1);
          ++backgroundSnapshotGrants;
        }
      } else if (owner == MidiServiceDrain::DeferredIdleOwner::VisualCache ||
                 owner == MidiServiceDrain::DeferredIdleOwner::VisualCacheWrapAppend) {
        if (stopping->hasRealtimeDeferredIdleWork()) {
          stopping->processDeferredIdleMaintenance(0, false, owner, 1);
          ++continuedStopGrants;
        }
      }
    }
    char stopDetail[210];
    snprintf(stopDetail, sizeof(stopDetail),
        "stopTurns=%llu cap=%llu phase=%u queued=%u root=%u rootGrants=%u stopGrants=%u",
        static_cast<unsigned long long>(drainTurns), static_cast<unsigned long long>(diagnosticCap),
        stopping->hasPendingDeferredOverdubStop()
            ? static_cast<unsigned>(TrackCaptureStopTestAccess::phase(*stopping)) : 255u,
        stopping->getLoop(0).pendingSnapshotSessionExitQueued(),
        stopping->getLoop(0).publishedSnapshotIsCurrent(),
        backgroundSnapshotGrants, continuedStopGrants);
    TEST_ASSERT_FALSE_MESSAGE(stopping->hasPendingDeferredOverdubStop(), stopDetail);
    TEST_ASSERT_TRUE(backgroundSnapshotGrants > 0 && continuedStopGrants > 0);
    TEST_ASSERT_TRUE(stopping->getLoop(0).publishedSnapshotIsCurrent());
    TEST_ASSERT_TRUE(stopping->getLoop(0).passes.hasRecordPass());
    TEST_ASSERT_EQUAL_UINT64(stopInterval, TrackCaptureStopTestAccess::interval(*stopping));
    for (size_t index = 0; index < TrackCaptureStopTestAccess::descriptorCount(); ++index)
      TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::descriptorNeedsSourceRelease(*stopping, index),
          "retired Record job must release its sealed raw-range descriptor");
    SessionMidiEventVec committed;
    LoopEventStore::appendChunkRefEvents(
        stopping->getLoop(0).passes.recordPass.committedChunkIds, committed);
    TEST_ASSERT_EQUAL_UINT32(2u, committed.size());
    TEST_ASSERT_TRUE(committed[0].isNoteOn());
    TEST_ASSERT_EQUAL_UINT32(12u, committed[0].tick);
    TEST_ASSERT_EQUAL_UINT8(60, committed[0].data.noteData.note);
    TEST_ASSERT_TRUE(committed[1].isNoteOff());
    TEST_ASSERT_EQUAL_UINT32(96u, committed[1].tick);
    TEST_ASSERT_EQUAL_UINT8(60, committed[1].data.noteData.note);
    TEST_ASSERT_TRUE(t.isPlaying());
    const auto* served = runtime.viewSetIfPrewarmed();
    TEST_ASSERT_NOT_NULL(served);
    TEST_ASSERT_EQUAL_UINT8(servedRole, served->lifecycle.current);
    TEST_ASSERT_EQUAL_PTR(loop.publishedSnapshotIdentity(),
        served->views[served->lifecycle.current].source.snapshotIdentity);
    TEST_ASSERT_TRUE(served->views[served->lifecycle.current].complete);
    TEST_ASSERT_EQUAL_UINT32(current.storageGeneration,
        served->views[served->lifecycle.current].storageGeneration);
    TEST_ASSERT_EQUAL_UINT32(revision,
        served->views[served->lifecycle.current].source.playbackRevision);
  }
}

void test_rootless_source_and_window_complete_inside_own_deadline_under_owner_rotation() {
  runRootlessSourceAndWindowUnderOwnerRotation(false);
}

void test_rootless_window_and_competing_record_stop_both_progress() {
  runRootlessSourceAndWindowUnderOwnerRotation(true);
}

void test_transport_retarget_of_a_record_job_resolves_stopped() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60);

  clockManager.assignCurrentTickSilently(768);
  t.stopRecording(768);  // Playing destination: the edge launches playback while the job finalizes.
  TEST_ASSERT_TRUE_MESSAGE(t.isPlaying(), "precondition: the Playing edge must launch playback");
  TEST_ASSERT_TRUE(t.hasPendingDeferredOverdubStop());

  TrackCaptureStopTestAccess::retargetPendingStopToStopped(t);
  driveStopToRetirement(t);

  TEST_ASSERT_FALSE(t.hasPendingDeferredOverdubStop());
  TEST_ASSERT_EQUAL_MESSAGE(TRACK_STOPPED, t.getState(),
                            "a retargeted Record stop must end the transport at STOPPED");
  TEST_ASSERT_TRUE_MESSAGE(t.getLoop(0).passes.hasRecordPass(),
                           "the retargeted job must still commit its take");
}

// S4f.21: a **replacement** interval (slot load / re-record) adopted over the slot while a Record
// stop is in flight. The S4f.4 transition-3 rule is that such a request is *superseded* -- reported
// and retired -- not parked: the prologue's LoopId guard must not be able to hold the job, its raw
// range, its capture-stop owner and its ring entry forever.
void test_replaced_loop_during_a_record_finalization_retires_the_job() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60);
  clockManager.assignCurrentTickSilently(768);
  t.stopRecordingToStopped(768);
  TEST_ASSERT_TRUE_MESSAGE(t.hasPendingDeferredOverdubStop(),
                           "precondition: the Record stop must be admitted");
  // Fault-inject the adoption: a replacement interval reuses the slot number with a new LoopId.
  ++t.getLoop(0).loopId;
  driveStopToRetirement(t);
  TEST_ASSERT_FALSE_MESSAGE(t.hasPendingDeferredOverdubStop(),
                            "a superseded Record job must retire, not hold the ring forever");
}

// S4f.21: a slot **clear** issued while a Record stop is in flight. The clear destroys the
// interval's ledger, so the job must be superseded -- otherwise the worker would publish the cleared
// take back into the slot the user just emptied. The one-time ownership release runs on the clear
// (the user's action), and the bounded extent release plus the ring advance finish on the deferred
// owner's turn.
void test_slot_clear_during_a_record_finalization_does_not_republish() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60);
  clockManager.assignCurrentTickSilently(768);
  t.stopRecordingToStopped(768);
  TEST_ASSERT_TRUE_MESSAGE(t.hasPendingDeferredOverdubStop(),
                           "precondition: the Record stop must be admitted");

  t.clear();

  driveStopToRetirement(t);
  TEST_ASSERT_FALSE_MESSAGE(t.hasPendingDeferredOverdubStop(),
                            "a cleared slot's Record job must retire, not hold the ring");
  TEST_ASSERT_FALSE_MESSAGE(t.getLoop(0).passes.hasRecordPass(),
                            "a cleared slot must not receive the in-flight take");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, t.getLoop(0).loopLengthTicks,
                                   "the cleared geometry must survive the retired job");
}

// Same-boot HITL shape: a slot completed a Record, was cleared, then seeded again.
// Both intervals must retire through the real closure/preparation/publication worker.
void test_record_clear_record_reuses_finalization_scratch() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);
  start(t, 0, false, 0, 60);
  stop(t, false, 768);
  driveStopToRetirement(t);
  assertContent(t, 0, false, 60);

  t.clear();
  prepareSlot(t, 0, false);
  start(t, 0, false, 1536, 62);
  stop(t, false, 2304);
  const auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
  const auto& nextStop = TrackCaptureStopTestAccess::head(t).admission;
  TEST_ASSERT_TRUE_MESSAGE(runtime.frozenCaptureRewriteOwner == nullptr ||
      (runtime.frozenCaptureRewrite.sourceOwner == &nextStop &&
       runtime.frozenCaptureRewrite.sourceGeneration == nextStop.frozenGeneration),
      "a new record stop must not inherit the retired interval's rewrite binding");
  driveStopToRetirement(t);
  assertContent(t, 0, false, 62);
}

// S4.5 root cause, native: the Record retirement used to drop its job while the descriptor still owned
// the sealed source extent, so the extent never returned to the pool and that descriptor could never be
// sealed again. On hardware the second preparation of a boot then denied `raw_range_unsealable`
// (`captures/s45-deny-20261002-174528-od2`) while the take kept recording. This pins the leak directly
// and the two-cycle behaviour that depends on it.
void test_a_record_retirement_returns_its_extent_and_rearms_the_descriptor() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);

  start(t, 0, false, 0, 60);
  stop(t, false, 768);
  driveStopToRetirement(t);
  // The leak, pinned *before* any recovery runs: a retired record job must not leave its descriptor
  for (size_t index = 0; index < TrackCaptureStopTestAccess::descriptorCount(); ++index) {
    TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::descriptorNeedsSourceRelease(t, index),
                              "a retired record interval must return its sealed extent to the pool");
  }
  // And the recovery half: whatever a route leaves behind converges in bounded idle turns.
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::serviceDescriptorReservations(t),
                           "no descriptor may be left un-rearmed once the service has run");

  // Cycle 2 in the same slot: the second preparation of a boot, admitted rather than latched.
  t.clear();
  prepareSlot(t, 0, false);
  start(t, 0, false, 1536, 62);
  stop(t, false, 2304);
  TEST_ASSERT_FALSE_MESSAGE(t.hasAcceptedUnexecutedInteraction(),
                            "the second record stop must be admitted, not latched as accepted_wait");
  driveStopToRetirement(t);
  assertContent(t, 0, false, 62);
}

// S4.5 HITL shape: the **second** preparation of one slot on one boot. The device run
// `captures/s45-campaign3-20261002-173113-od2` showed the second cycle's Record stop denied admission
// (`RECS,stage,finalize,…,accepted_wait`) with the interval left recording, while the first cycle on that same boot
// committed and retired normally. The denial path consults nine predicates and logs none of them, so this fixture
// pins **every precondition** `beginDeferredRecordStopForSlot` evaluates: a denial here then names its own guard,
// and if all of them hold, the device's denial has to come from state this harness cannot express -- a pending
// persistence bundle holding one of those resources -- which is what the board-side named denial is for.
void test_second_record_cycle_is_admitted_with_every_guard_precondition_pinned() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  t.forceSetState(TRACK_STOPPED);

  // Cycle 1: the preparation that works on hardware.
  start(t, 0, false, 0, 60);
  stop(t, false, 768);
  driveStopToRetirement(t);
  assertContent(t, 0, false, 60);

  {
    auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
    TEST_ASSERT_FALSE_MESSAGE(runtime.ownsLiveFinalization(),
                              "cycle 1 must release the slot's finalizer ownership");
    TEST_ASSERT_TRUE_MESSAGE(runtime.canonicalCaptureOutputAdmitted(),
                             "cycle 1 must leave the canonical capture output admitted");
  }
  TEST_ASSERT_FALSE_MESSAGE(t.hasPendingDeferredOverdubStop(),
                            "cycle 1 must leave no overdub stop obligation pending");
  TEST_ASSERT_TRUE_MESSAGE(t.loopsAllocated(), "the loop pool must still be allocated");

  // Cycle 2: clear, then record the same slot again -- the device's second preparation.
  t.clear();
  prepareSlot(t, 0, false);
  start(t, 0, false, 1536, 62);
  {
    auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);
    TEST_ASSERT_EQUAL(CapturePhase::Record, t.getLoop(0).capture.phase);
    TEST_ASSERT_TRUE_MESSAGE(runtime.canonicalCaptureOutputAdmitted(),
                             "cycle 2's Record start must admit the canonical output again");
    TEST_ASSERT_FALSE_MESSAGE(runtime.ownsLiveFinalization(),
                              "cycle 2 must start with no finalizer owning the slot");
  }

  // The production stop edge, exactly as the device's commit press reaches it.
  stop(t, false, 2304);
  TEST_ASSERT_FALSE_MESSAGE(t.hasAcceptedUnexecutedInteraction(),
                            "the second cycle's Record stop must be admitted, not latched as accepted_wait");
  driveStopToRetirement(t);
  assertContent(t, 0, false, 62);
}

// S4f.21 / DEC-078 item 3: the accepted-stop drive re-derives the destination from **live**
// transport state rather than the destination the retained request carried. That is the route the
// clock source reaches this path through (`isTransportRunning()` is false both for a stopped
// internal transport and for an external clock that has gone quiet), so a Record stop retained
// while the transport ran must be admitted for STOPPED once it is not running -- and it must still
// publish its take there, from its own job-qualified geometry.
void test_retained_record_stop_admits_stopped_once_the_transport_is_not_running() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  prepareSlot(t, 1, false);
  t.forceSetState(TRACK_PLAYING);

  // (1) A's Record stop binds first and owns the ring.
  start(t, 0, false, 0, 60);
  clockManager.assignCurrentTickSilently(768);
  t.stopRecordingToStopped(768);
  TEST_ASSERT_TRUE(t.hasPendingDeferredOverdubStop());

  // (2) B's **PLAYING**-destination Record stop is retained: A still owns the ring.
  start(t, 1, false, 768, 62);
  const uint64_t bInterval = TrackCaptureStopTestAccess::interval(t);
  clockManager.assignCurrentTickSilently(1536);
  t.stopRecording(1536);
  TEST_ASSERT_TRUE_MESSAGE(t.hasAcceptedInteractionForSlot(1),
                           "precondition: B's stop must be retained against slot 1");
  TEST_ASSERT_FALSE(t.hasLiveCaptureStopJobForSlot(1));

  // (3) A retires, and the transport stops before the drive runs.
  for (int turn = 0; turn < 8192 && t.hasPendingDeferredOverdubStop(); ++turn) {
    auto& predecessor = TrackCaptureStopTestAccess::head(t);
    if (predecessor.mode == CapturePhase::Record &&
        predecessor.admission.phase == PendingStopPhase::RecordPublished && predecessor.owner != nullptr &&
        predecessor.owner->pendingSnapshotSessionExitQueued() &&
        !predecessor.owner->publishedSnapshotIsCurrent()) {
      t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
    } else {
      (void)TrackCaptureStopTestAccess::step(t);
    }
  }
  TEST_ASSERT_FALSE(t.hasPendingDeferredOverdubStop());
  nativeTransportRunning = false;
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::drive(t),
                           "the retained stop must admit once the ring frees");

  // (4) The destination was re-derived from live transport state, not taken from the request.
  // The transport-state assert below is the discriminator: the PLAYING edge's own runtime reset
  // also retargets a pending job's destination (that is the S4f.19 path this change pins
  // separately), so the destination field alone can be Stopped even when the drive trusted the
  // request. Only resolving the *transport state* from that retarget distinguishes the two.
  auto& job = TrackCaptureStopTestAccess::head(t);
  TEST_ASSERT_EQUAL_UINT8(1, job.slot);
  TEST_ASSERT_EQUAL(CapturePhase::Record, job.mode);
  TEST_ASSERT_EQUAL_UINT64(bInterval, job.interval);
  TEST_ASSERT_EQUAL_MESSAGE(PendingStopDestination::Stopped, job.admission.destination,
                            "a stop driven while the transport is not running must admit STOPPED");

  driveStopToRetirement(t);
  TEST_ASSERT_TRUE_MESSAGE(t.getLoop(1).passes.hasRecordPass(),
                           "the STOPPED-admitted Record take must still publish");
  TEST_ASSERT_EQUAL_MESSAGE(TRACK_STOPPED, t.getState(),
                            "a stop driven while the transport is not running must resolve STOPPED");
  nativeTransportRunning = true;  // leave the shim as the next fixture's setUp expects it
}

void test_private_grains_never_hold_the_global_ephemeral_seal() {
  Track& t0 = trackManager.getTrack(0);
  prepareSlot(t0, 0, false);
  t0.startRecording(100);
  // Enough closed pairs that staging needs many bounded grains and its private
  // output seals more than one chunk.
  for (uint32_t pair = 0; pair < 160; ++pair) {
    const uint32_t tick = 4 + pair * 4;
    t0.noteOn(1 + (pair % 8), static_cast<uint8_t>(pair % 128), 100, tick);
    t0.noteOff(1 + (pair % 8), static_cast<uint8_t>(pair % 128), 0, tick + 2);
  }
  auto& job = TrackCaptureStopTestAccess::bindRecordClosure(t0, 0, 768, 768);
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete,
                    driveGrains(t0, TrackCaptureStopTestAccess::closureGrain));
  TEST_ASSERT_EQUAL(PendingStopPhase::RecordSourceStaging, job.admission.phase);

  // A second track's normal capture, appended between two private grains, must
  // still reach normal persistence admission: a long-held global seal would make
  // `sealChunk` skip it.
  Track& t1 = trackManager.getTrack(1);
  prepareSlot(t1, 0, false);
  t1.setActiveLoopIndex(0);
  t1.startRecording(200);
  const size_t queueBefore = PersistenceQueue::queueDepth();
  t1.noteOn(1, 60, 100, 212);
  while (t1.getLoop(0).capture.store.size() < LoopEventStoreConfig::CHUNK_CAPACITY) {
    TEST_ASSERT_TRUE(t1.getLoop(0).capture.store.append(MidiEvent::ControlChange(
        static_cast<uint32_t>(212 + t1.getLoop(0).capture.store.size()), 1, 7, 1)));
  }
  TEST_ASSERT_EQUAL(queueBefore + 1, PersistenceQueue::queueDepth());
  TEST_ASSERT_EQUAL(static_cast<int>(ChunkPersistenceState::Queued),
                    static_cast<int>(PersistenceQueue::chunkState(
                        t1.getLoop(0).capture.store.chunkIds().front())));

  // Every private grain must return with the global seal absent, and the private
  // output chunks (which seal past the staging ceiling) must never be admitted.
  const size_t queueAfterNormal = PersistenceQueue::queueDepth();
  LoopEventStoreRewriteResult result = LoopEventStoreRewriteResult::Pending;
  size_t turns = 0;
  while (result == LoopEventStoreRewriteResult::Pending && turns++ < 200000) {
    result = TrackCaptureStopTestAccess::stagingGrain(t0);
    TEST_ASSERT_FALSE(LoopEventStore::isEphemeralSeal());
    TEST_ASSERT_EQUAL(queueAfterNormal, PersistenceQueue::queueDepth());
  }
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Complete, result);
  TEST_ASSERT_GREATER_THAN(1, TrackCaptureStopTestAccess::closureRuntime(t0)
                                  .canonicalCaptureOutput.store.chunkIds().size());
  for (uint16_t id : TrackCaptureStopTestAccess::closureRuntime(t0)
                         .canonicalCaptureOutput.store.chunkIds()) {
    TEST_ASSERT_EQUAL(static_cast<int>(ChunkPersistenceState::NotScheduled),
                      static_cast<int>(PersistenceQueue::chunkState(id)));
  }
  TEST_ASSERT_EQUAL(queueAfterNormal, PersistenceQueue::queueDepth());
}

// Independent inventories for the deferred Record normalizer migration. These
// call the real stop entry points; they do not copy normalization into a fixture.
void assertRecordRows(const Loop& loop, const SessionMidiEventVec& expected) {
  SessionMidiEventVec rows;
  LoopEventStore::appendChunkRefEvents(loop.passes.recordPass.committedChunkIds, rows);
  TEST_ASSERT_EQUAL(expected.size(), rows.size());
  for (const auto& want : expected) {
    size_t matches = 0;
    for (const auto& actual : rows) {
      const bool payload = want.type == midi::ControlChange
          ? actual.data.ccData.cc == want.data.ccData.cc && actual.data.ccData.value == want.data.ccData.value
          : actual.data.noteData.note == want.data.noteData.note && actual.data.noteData.velocity == want.data.noteData.velocity;
      if (actual.type == want.type && actual.tick == want.tick &&
          actual.channel == want.channel && actual.noteId == want.noteId && payload) ++matches;
    }
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, matches, "record normalization inventory mismatch");
  }
}

void recordNormalizationInventory(bool playing, bool removeShort) {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false);
  noteMinLengthTicks = 48; noteMinLengthRemoveEnabled = removeShort;
  constexpr uint32_t origin = 768;
  t.startRecording(origin);
  Loop& loop = t.getLoop(0);
  SessionMidiEventVec expected;
  auto pair = [&](uint8_t channel, uint8_t note, uint32_t on, uint32_t off,
                  uint32_t expectedOn, uint32_t expectedOff, bool synthetic, bool keep) {
    t.noteOn(channel, note, 100, origin + on);
    const NoteId id = TrackCaptureStopTestAccess::recordIdentity(t, channel, note);
    TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, id);
    t.noteOff(channel, note, 0, origin + off);
    if (!keep) return;
    auto a = MidiEvent::NoteOn(expectedOn, channel, note, 100); a.noteId = id;
    auto b = MidiEvent::NoteOff(expectedOff, channel, note, 0); b.noteId = synthetic ? kInvalidNoteId : id;
    expected.push_back(a); expected.push_back(b);
  };
  pair(1, 60, 780, 820, 12, 132, true, true);
  pair(2, 61, 700, 800, 700, 767, true, true);
  pair(3, 62, 50, 70, 50, 70, false, !removeShort);
  pair(4, 63, 100, 148, 100, 148, false, true);
  const auto cc = MidiEvent::ControlChange(30, 1, 7, 91);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(cc)); expected.push_back(cc);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::ControlChange(780, 1, 7, 19)));
  TEST_ASSERT_EQUAL(10, loop.capture.store.size());
  TEST_ASSERT_EQUAL(0, TrackCaptureStopTestAccess::heldCount(t));
  clockManager.assignCurrentTickSilently(origin + 900);
  if (playing) t.stopRecording(origin + 900); else t.stopRecordingToStopped(origin + 900);
  TEST_ASSERT_EQUAL(playing ? TRACK_PLAYING : TRACK_STOPPED, t.getState());
  if (playing) t.stopPlaying(); // Inventory is a lifecycle test; stop before draining reader retirement.
  driveStopToRetirement(t);
  TEST_ASSERT_EQUAL(768, loop.loopLengthTicks);
  TEST_ASSERT_TRUE(loop.passes.hasRecordPass());
  TEST_ASSERT_EQUAL(0, loop.passes.overdubPasses.size());
  TEST_ASSERT_EQUAL(TRACK_STOPPED, t.getState());
  assertRecordRows(loop, expected);
}
void test_record_normalization_stopped_inventory() { recordNormalizationInventory(false, true); }
void test_record_normalization_playing_inventory() { recordNormalizationInventory(true, true); }
void test_record_normalization_disabled_minimum_inventory() { recordNormalizationInventory(false, false); }

void test_record_normalization_preserves_take_larger_than_overdub_staging() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false); t.startRecording(768);
  noteMinLengthRemoveEnabled = false;
  constexpr size_t pairs = RuntimePlaybackViewConfig::kCaptureStagingEvents / 2 + 4;
  static_assert(pairs * 2 > RuntimePlaybackViewConfig::kCaptureStagingEvents, "cross staging envelope");
  SessionMidiEventVec expected;
  for (size_t i = 0; i < pairs; ++i) {
    const uint32_t on = static_cast<uint32_t>(i * 48 + 12);
    const auto channel = static_cast<uint8_t>(1 + (i / 128) % 16);
    const auto note = static_cast<uint8_t>(i % 128);
    t.noteOn(channel, note, 100, 768 + on);
    const NoteId id = TrackCaptureStopTestAccess::recordIdentity(t, channel, note);
    TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, id);
    t.noteOff(channel, note, 0, 768 + on + 24);
    auto a = MidiEvent::NoteOn(on, channel, note, 100); a.noteId = id;
    auto b = MidiEvent::NoteOff(on + 24, channel, note, 0); b.noteId = id;
    expected.push_back(a); expected.push_back(b);
  }
  TEST_ASSERT_EQUAL(pairs * 2, t.getLoop(0).capture.store.size());
  const uint32_t length = static_cast<uint32_t>(((pairs * 48 + 767) / 768) * 768);
  clockManager.assignCurrentTickSilently(768 + length);
  t.stopRecordingToStopped(768 + length);
  driveStopToRetirement(t);
  TEST_ASSERT_EQUAL(length, t.getLoop(0).loopLengthTicks);
  assertRecordRows(t.getLoop(0), expected);
}

void test_record_normalization_filtered_empty_resets_geometry() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, false); t.startRecording(768);
  noteMinLengthTicks = 48; noteMinLengthRemoveEnabled = true;
  t.noteOn(1, 60, 100, 780); t.noteOff(1, 60, 0, 800);
  TEST_ASSERT_EQUAL(2, t.getLoop(0).capture.store.size());
  clockManager.assignCurrentTickSilently(1536);
  t.stopRecordingToStopped(1536);
  driveStopToRetirement(t);
  TEST_ASSERT_FALSE(t.getLoop(0).passes.hasRecordPass());
  TEST_ASSERT_EQUAL(0, t.getLoop(0).loopLengthTicks);
  TEST_ASSERT_EQUAL(0, t.getLoop(0).capture.store.size());
  TEST_ASSERT_EQUAL(TRACK_EMPTY, t.getState());
}

void test_job_pass_staging_yields_and_preserves_successor() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, true); prepareSlot(t, 1, false);
  t.forceSetState(TRACK_PLAYING);
  start(t, 0, true, 0, 60);
  for (unsigned i = 0; i < 140; ++i) {
    const uint8_t channel = 2 + i / 128, note = i % 128;
    t.noteOn(channel, note, 100, 12); t.noteOff(channel, note, 0, 96);
  }
  stop(t, true, 768);
  advanceToPhase(t, PendingStopPhase::PrepareTransfer);
  auto& source = TrackCaptureStopTestAccess::output(t);
  const size_t chunks = source.store.chunkIds().size();
  TEST_ASSERT_GREATER_THAN(1, chunks);
  const size_t rows = source.store.size();
  TEST_ASSERT_EQUAL(282, rows);
  TrackCaptureStopTestAccess::step(t); // scalar admission
  auto& pass = TrackCaptureStopTestAccess::pass(t);
  const PassId id = pass.id;
  TEST_ASSERT_TRUE(pass.preparationPending);
  TEST_ASSERT_TRUE(pass.committedChunkIds.empty());
  TEST_ASSERT_EQUAL(rows, source.store.size());
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::hasReadablePass(t));
  TEST_ASSERT_FALSE(t.getLoop(0).hasPendingCapturePass());
  start(t, 1, false, 768, 72);
  const auto successor = TrackCaptureStopTestAccess::interval(t);
  for (size_t grain = 0; grain < chunks + chunks / 2; ++grain) {
    const auto before = source.store.chunkIds().size();
    const auto moved = pass.committedChunkIds.size();
    TEST_ASSERT_TRUE(pass.preparationPending);
    PreparedCapturePassTransfer transfer;
    TEST_ASSERT_FALSE(t.getLoop(0).transferCapturePass(pass, transfer));
    assertNoPersistenceRequest();
    TrackCaptureStopTestAccess::step(t);
    TEST_ASSERT_EQUAL(id, pass.id);
    TEST_ASSERT_EQUAL(before ? before - 1 : 0, source.store.chunkIds().size());
    TEST_ASSERT_EQUAL(moved + (before ? 1 : 0), pass.committedChunkIds.size());
    TEST_ASSERT_EQUAL(successor, TrackCaptureStopTestAccess::interval(t));
    TEST_ASSERT_TRUE(t.isRecording());
    TEST_ASSERT_EQUAL(2, t.getLoop(1).capture.store.size());
  }
  TEST_ASSERT_FALSE(pass.preparationPending);
  TEST_ASSERT_EQUAL(CapturePhase::None, source.phase);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::hasReadablePass(t));
  TEST_ASSERT_EQUAL(PendingStopPhase::CanonicalTail, TrackCaptureStopTestAccess::phase(t));
  advanceToPhase(t, PendingStopPhase::UndoCollection);
  TrackCaptureStopTestAccess::assertDeniedUndoRetainsObligation(t);
  TrackCaptureStopTestAccess::assertDeniedUndoPublicationRetries(t);
  assertNoPersistenceRequest();
  advanceToPhase(t, PendingStopPhase::DeferredCleanup);
  TEST_ASSERT_EQUAL(1, t.getLoop(0).passes.overdubPasses.size());
  SessionMidiEventVec committed;
  LoopEventStore::appendChunkRefEvents(t.getLoop(0).passes.overdubPasses[0].committedChunkIds, committed);
  TEST_ASSERT_EQUAL(rows, committed.size());
  for (unsigned i = 0; i < 140; ++i) {
    unsigned ons = 0, offs = 0;
    for (const auto& event : committed) {
      if (event.channel != 2 + i / 128 || event.data.noteData.note != i % 128) continue;
      if (event.type == midi::NoteOn) { ++ons; TEST_ASSERT_EQUAL(12, event.tick); }
      if (event.type == midi::NoteOff) { ++offs; TEST_ASSERT_EQUAL(96, event.tick); }
    }
    TEST_ASSERT_EQUAL(1, ons); TEST_ASSERT_EQUAL(1, offs);
  }
  TEST_ASSERT_TRUE(StorageManagerInternal::currentSetLoopSlotDirty[0][0]);
  TEST_ASSERT_FALSE(StorageManagerInternal::currentSetLoopSlotDirty[0][1]);
  const auto budget = TrackCaptureStopTestAccess::budget(t);
  for (uint64_t i = 0; i < budget && TrackCaptureStopTestAccess::jobs(t); ++i)
    TrackCaptureStopTestAccess::step(t);
  TEST_ASSERT_EQUAL(0, TrackCaptureStopTestAccess::jobs(t));
  TrackCaptureStopTestAccess::assertRetiredCursors(t);
  const auto persisted = PersistenceWorkQueue::queueDepth();
  const auto undoCount = t.getGlobalUndoStack().undoCount();
  TEST_ASSERT_EQUAL(1, undoCount);
  for (unsigned i = 0; i < 10; ++i) TrackCaptureStopTestAccess::step(t);
  TEST_ASSERT_EQUAL(persisted, PersistenceWorkQueue::queueDepth());
  TEST_ASSERT_EQUAL(undoCount, t.getGlobalUndoStack().undoCount());
  TEST_ASSERT_EQUAL(1, t.getLoop(0).passes.overdubPasses.size());
  TEST_ASSERT_TRUE(t.isRecording());
  TEST_ASSERT_EQUAL(2, t.getLoop(1).capture.store.size());
}

void test_job_pass_staging_denial_and_receipt_binding() {
  Track& t = trackManager.getTrack(0);
  prepareSlot(t, 0, true);
  auto& loop = t.getLoop(0);
  Capture source; source.phase = CapturePhase::Overdub;
  TEST_ASSERT_TRUE(source.store.append(MidiEvent::ControlChange(1, 1, 1, 64)));
  PendingCapturePass pass, other;
  LoopEventStoreCommitCursor cursor;
  uint64_t generation = 4;
  TEST_ASSERT_EQUAL(SealOutcome::AlreadyPending,
    loop.beginPreparedCapturePassFromActiveAdmission(loop.capture, pass, cursor, generation, 768));
  TEST_ASSERT_EQUAL(SealOutcome::FailedValidation,
    loop.beginPreparedCapturePassFromActiveAdmission(source, pass, cursor, generation, 768));
  TEST_ASSERT_TRUE(loop.hasActiveOverdubTransferAdmission());
  TEST_ASSERT_EQUAL(kInvalidPassId, pass.id);
  TEST_ASSERT_EQUAL(1, source.store.size());
  TEST_ASSERT_TRUE(source.store.prewarmCaptureDirectory());
  TEST_ASSERT_EQUAL(SealOutcome::Ok,
    loop.beginPreparedCapturePassFromActiveAdmission(source, pass, cursor, generation, 768));
  const auto id = pass.id;
  TEST_ASSERT_EQUAL(SealOutcome::AlreadyPending,
    loop.beginPreparedCapturePassFromActiveAdmission(source, pass, cursor, generation, 768));
  other.id = id;
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied,
    loop.stepPreparedCapturePass(source, other, cursor, 1));
  TEST_ASSERT_TRUE(pass.preparationPending);
  ++generation;
  TEST_ASSERT_EQUAL(LoopEventStoreRewriteResult::Denied,
    loop.stepPreparedCapturePass(source, pass, cursor, 1));
  TEST_ASSERT_EQUAL(1, source.store.size());
  TEST_ASSERT_TRUE(pass.committedChunkIds.empty());
  TEST_ASSERT_FALSE(loop.canTransferCapturePassWithoutAllocation(pass));
  source.store.clear();

}

/// **The publication owner's own gate row** (windowed-playback-generation, the switch leg's second diagnostic). The
/// launch gate proved the demoted target's republish request was **accepted** and then never serviced; this pins the
/// mechanism one owner upstream. `Track::loopWithPendingSnapshotPublish` returns the **active** loop whenever it has any
/// pending publication, and the Snapshot owner's turn is bound to that one loop -- so a demoted, non-active target's
/// accepted request waits behind it. The fixture drives the **production turn** (`processDeferredIdleMaintenance`,
/// Snapshot owner, one unit): the earlier regression stepped the publication primitive by hand on the target, which is
/// precisely the seam that hides the loop choice on hardware.
void test_snapshot_owner_prefers_the_active_loop_on_a_tie() {
  Track& t = trackManager.getTrack(0);
  t.ensureLoopsAllocated();
  // The demoted, non-active target (index 2): committed content, no published root -- the shape the switch leg reads.
  Loop& target = t.getLoop(2);
  t.setActiveLoopIndex(2);  // selected only long enough to be written
  target.loopLengthTicks = 24 * 768;
  target.beginCapture(CapturePhase::Record);
  for (uint32_t bar = 0; bar < 24; ++bar) {
    TEST_ASSERT_TRUE(target.appendCaptureEvent(MidiEvent::NoteOn(bar * 768, 1, 48, 100)));
    TEST_ASSERT_TRUE(target.appendCaptureEvent(MidiEvent::NoteOff(bar * 768 + 48, 1, 48, 0)));
  }
  TEST_ASSERT_EQUAL(SealOutcome::Ok, target.sealCapture(24 * 768, CommitReason::RecordStop));
  TEST_ASSERT_TRUE(target.commitPendingCapturePass());
  for (unsigned i = 0; i < 20000 && target.hasPendingSnapshotPublish(); ++i)
    target.stepPendingSnapshotPublish(4, true, 0);
  target.storePublishedSnapshotForTest({}, 0);
  TEST_ASSERT_FALSE(target.publishedSnapshotIsCurrent());
  // The operator's own (active) slot -- **index 3, deliberately after the target's 2 in the selector's walk**, so the
  // fixture discriminates the active-loop preference from the walk's own order. A 24-bar commit is many construction
  // units, so the selector has work to prefer for the whole phase.
  Loop& active = t.getLoop(3);
  t.setActiveLoopIndex(3);
  active.loopLengthTicks = 24 * 768;
  active.beginCapture(CapturePhase::Record);
  for (uint32_t bar = 0; bar < 24; ++bar) {
    TEST_ASSERT_TRUE(active.appendCaptureEvent(MidiEvent::NoteOn(bar * 768, 1, 50, 100)));
    TEST_ASSERT_TRUE(active.appendCaptureEvent(MidiEvent::NoteOff(bar * 768 + 48, 1, 50, 0)));
  }
  TEST_ASSERT_EQUAL(SealOutcome::Ok, active.sealCapture(24 * 768, CommitReason::RecordStop));
  TEST_ASSERT_TRUE(active.commitPendingCapturePass());
  TEST_ASSERT_TRUE(active.requestCommittedPassesRepublish());
  TEST_ASSERT_TRUE(target.requestCommittedPassesRepublish());
  SnapshotGateRows::reset();
  for (unsigned turn = 0; turn < 8; ++turn) {
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
    TEST_ASSERT_NOT_NULL(SnapshotGateRows::last());
    // Every turn names the **active** loop -- index 3, *after* the pending target at index 2 -- with the active slot
    // printed beside it and the pending-publication bit that keeps the selector on it: that pair is what the device row
    // is read for.
    TEST_ASSERT_EQUAL_UINT8(0, SnapshotGateRows::last()->track);
    TEST_ASSERT_EQUAL_UINT8(3, SnapshotGateRows::last()->selectedSlot);
    TEST_ASSERT_EQUAL_UINT8(3, SnapshotGateRows::last()->activeSlot);
    TEST_ASSERT_EQUAL_UINT8(1, SnapshotGateRows::last()->constructionUnits);
    TEST_ASSERT_TRUE((SnapshotGateRows::last()->verdicts & (1u << 0)) != 0);
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, SnapshotGateRows::countSelecting(2),
                                   "the non-active target must not be selected while the active loop owes work");
  // The row is a **transition record**, not a heartbeat: eight turns of one state do not produce eight rows. The
  // cadence lives in production code, which is why a substituted recorder can count it at all.
  TEST_ASSERT_TRUE_MESSAGE(SnapshotGateRows::count < 8, "the gate row must be emitted on state changes, not per turn");
  // The consequence, asserted as state: the target's accepted request is still unserved and its root is still absent.
  TEST_ASSERT_TRUE(target.hasPendingSnapshotPublish());
  TEST_ASSERT_FALSE(target.publishedSnapshotIsCurrent());
  // **The contrast, and the discriminator the episode is read for:** a track whose active loop owes nothing selects the
  // pending slot instead (`s != a`), so "the target never got the turn" is a property of the selector, not of the row.
  Track& other = trackManager.getTrack(1);
  other.ensureLoopsAllocated();
  Loop& otherTarget = other.getLoop(3);
  other.setActiveLoopIndex(3);
  otherTarget.loopLengthTicks = 768;
  otherTarget.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(otherTarget.appendCaptureEvent(MidiEvent::NoteOn(0, 1, 52, 100)));
  TEST_ASSERT_TRUE(otherTarget.appendCaptureEvent(MidiEvent::NoteOff(48, 1, 52, 0)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, otherTarget.sealCapture(768, CommitReason::RecordStop));
  TEST_ASSERT_TRUE(otherTarget.commitPendingCapturePass());
  for (unsigned i = 0; i < 20000 && otherTarget.hasPendingSnapshotPublish(); ++i)
    otherTarget.stepPendingSnapshotPublish(4, true, 0);
  other.setActiveLoopIndex(1);  // an empty slot: nothing of its own is owed
  TEST_ASSERT_FALSE(other.getLoop(1).hasPendingSnapshotPublish());
  TEST_ASSERT_TRUE(otherTarget.requestCommittedPassesRepublish());
  SnapshotGateRows::reset();
  other.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
  TEST_ASSERT_NOT_NULL(SnapshotGateRows::last());
  TEST_ASSERT_EQUAL_UINT8(1, SnapshotGateRows::last()->track);
  TEST_ASSERT_EQUAL_UINT8(3, SnapshotGateRows::last()->selectedSlot);
  TEST_ASSERT_EQUAL_UINT8(1, SnapshotGateRows::last()->activeSlot);
  TEST_ASSERT_TRUE((SnapshotGateRows::last()->verdicts & (1u << 0)) != 0);
}

/// **The fix's acceptance oracle.** A demoted slot has a publication construction already in flight (it was selected
/// while the active loop owed nothing); the operator's active loop then queues its own republish. The selector must let
/// the started construction finish -- rank 0 beats the active loop's merely startable work -- instead of rebinding every
/// turn to the active loop and leaving the demoted slot's accepted request behind it.
void test_snapshot_owner_resumes_an_in_flight_slot_behind_the_active_loop() {
  Track& t = trackManager.getTrack(2);
  t.ensureLoopsAllocated();
  const auto commitTake = [&t](uint8_t slot, uint8_t note) -> Loop& {
    Loop& l = t.getLoop(slot);
    t.setActiveLoopIndex(slot);
    l.loopLengthTicks = 24 * 768;
    l.beginCapture(CapturePhase::Record);
    for (uint32_t bar = 0; bar < 24; ++bar) {
      TEST_ASSERT_TRUE(l.appendCaptureEvent(MidiEvent::NoteOn(bar * 768, 1, note, 100)));
      TEST_ASSERT_TRUE(l.appendCaptureEvent(MidiEvent::NoteOff(bar * 768 + 48, 1, note, 0)));
    }
    TEST_ASSERT_EQUAL(SealOutcome::Ok, l.sealCapture(24 * 768, CommitReason::RecordStop));
    TEST_ASSERT_TRUE(l.commitPendingCapturePass());
    return l;
  };
  Loop& target = commitTake(2, 48);
  for (unsigned i = 0; i < 20000 && target.hasPendingSnapshotPublish(); ++i)
    target.stepPendingSnapshotPublish(4, true, 0);
  target.storePublishedSnapshotForTest({}, 0);
  Loop& active = commitTake(3, 50);
  t.setActiveLoopIndex(3);
  TEST_ASSERT_TRUE(target.requestCommittedPassesRepublish());
  for (unsigned i = 0; i < 400 && !target.hasInFlightSnapshotConstruction(); ++i)
    target.stepPendingSnapshotPublish(1, true, 0);
  // Precondition (the shape under test): the target is mid-construction; the active loop owes startable work.
  TEST_ASSERT_TRUE(target.hasInFlightSnapshotConstruction());
  TEST_ASSERT_TRUE(target.hasPendingSnapshotPublish());
  TEST_ASSERT_TRUE(active.requestCommittedPassesRepublish());
  TEST_ASSERT_FALSE(active.hasInFlightSnapshotConstruction());
  TEST_ASSERT_TRUE(&t.loopWithPendingSnapshotPublish() == &target);
  // The production turn serves it, and once construction is in flight it keeps the turn.
  SnapshotGateRows::reset();
  t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
  TEST_ASSERT_NOT_NULL(SnapshotGateRows::last());
  TEST_ASSERT_EQUAL_UINT8(2, SnapshotGateRows::last()->selectedSlot);
  TEST_ASSERT_EQUAL_UINT8(3, SnapshotGateRows::last()->activeSlot);
  for (unsigned turn = 0; turn < 20000 && target.hasPendingSnapshotPublish(); ++turn) {
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
  }
  TEST_ASSERT_FALSE_MESSAGE(target.hasPendingSnapshotPublish(), "the demoted slot's request must be served");
  TEST_ASSERT_TRUE(target.publishedSnapshotIsCurrent());
}

/// The shape the selector's rank 2 exists for: the active loop owes a publication the owner's gate cannot start (queued
/// pass id only -- no session exit, wrap journal or construction), so it must not hold the turn against a demoted slot
/// whose accepted republish request *is* startable. Exits the starvation the device row (`step=0` on the active loop)
/// could produce.
void test_snapshot_owner_serves_a_startable_slot_behind_an_unstartable_active_loop() {
  Track& t = trackManager.getTrack(3);
  t.ensureLoopsAllocated();
  const auto commitTake = [&t](uint8_t slot, uint8_t note) -> Loop& {
    Loop& l = t.getLoop(slot);
    t.setActiveLoopIndex(slot);
    l.loopLengthTicks = 24 * 768;
    l.beginCapture(CapturePhase::Record);
    for (uint32_t bar = 0; bar < 24; ++bar) {
      TEST_ASSERT_TRUE(l.appendCaptureEvent(MidiEvent::NoteOn(bar * 768, 1, note, 100)));
      TEST_ASSERT_TRUE(l.appendCaptureEvent(MidiEvent::NoteOff(bar * 768 + 48, 1, note, 0)));
    }
    TEST_ASSERT_EQUAL(SealOutcome::Ok, l.sealCapture(24 * 768, CommitReason::RecordStop));
    TEST_ASSERT_TRUE(l.commitPendingCapturePass());
    for (unsigned i = 0; i < 20000 && l.hasPendingSnapshotPublish(); ++i) l.stepPendingSnapshotPublish(4, true, 0);
    return l;
  };
  Loop& target = commitTake(2, 48);
  target.storePublishedSnapshotForTest({}, 0);
  Loop& active = commitTake(3, 50);
  t.setActiveLoopIndex(3);
  TEST_ASSERT_TRUE(target.requestCommittedPassesRepublish());
  TEST_ASSERT_TRUE(active.queueLastCommittedPassForPublishForTest());
  TEST_ASSERT_TRUE(active.hasPendingSnapshotPublish());
  TEST_ASSERT_FALSE(active.pendingSnapshotSessionExitQueued());
  TEST_ASSERT_FALSE(active.hasInFlightSnapshotConstruction());
  TEST_ASSERT_FALSE(active.hasPendingWrapJournal());
  TEST_ASSERT_TRUE(target.pendingSnapshotSessionExitQueued());
  TEST_ASSERT_TRUE(&t.loopWithPendingSnapshotPublish() == &target);
  SnapshotGateRows::reset();
  t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
  TEST_ASSERT_NOT_NULL(SnapshotGateRows::last());
  TEST_ASSERT_EQUAL_UINT8(2, SnapshotGateRows::last()->selectedSlot);
  TEST_ASSERT_EQUAL_UINT8(3, SnapshotGateRows::last()->activeSlot);
  for (unsigned turn = 0; turn < 20000 && target.hasPendingSnapshotPublish(); ++turn) {
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
  }
  TEST_ASSERT_FALSE_MESSAGE(target.hasPendingSnapshotPublish(), "the startable demoted slot must be served");
  TEST_ASSERT_TRUE(target.publishedSnapshotIsCurrent());
}

/// The device episode that followed the selector fix (`switch-trace`): the owner selected the rootless demoted target
/// on 135/136 rows with `elig=1` while the track PLAYS, yet no publication unit was serviced for 22 s. Drives the same
/// production turn **with the track playing** and the active loop already rooted.
void test_snapshot_owner_serves_a_rootless_pending_slot_while_the_track_plays() {
  Track& t = trackManager.getTrack(4);
  t.ensureLoopsAllocated();
  const auto commitTake = [&t](uint8_t slot, uint8_t note) -> Loop& {
    Loop& l = t.getLoop(slot);
    t.setActiveLoopIndex(slot);
    l.loopLengthTicks = 24 * 768;
    l.beginCapture(CapturePhase::Record);
    for (uint32_t bar = 0; bar < 24; ++bar) {
      TEST_ASSERT_TRUE(l.appendCaptureEvent(MidiEvent::NoteOn(bar * 768, 1, note, 100)));
      TEST_ASSERT_TRUE(l.appendCaptureEvent(MidiEvent::NoteOff(bar * 768 + 48, 1, note, 0)));
    }
    TEST_ASSERT_EQUAL(SealOutcome::Ok, l.sealCapture(24 * 768, CommitReason::RecordStop));
    TEST_ASSERT_TRUE(l.commitPendingCapturePass());
    for (unsigned i = 0; i < 20000 && l.hasPendingSnapshotPublish(); ++i) l.stepPendingSnapshotPublish(4, true, 0);
    return l;
  };
  Loop& target = commitTake(5, 48);
  target.storePublishedSnapshotForTest({}, 0);
  Loop& active = commitTake(4, 50);
  t.setActiveLoopIndex(4);
  TEST_ASSERT_TRUE(static_cast<bool>(active.publishedSnapshot()));
  TEST_ASSERT_TRUE(target.requestCommittedPassesRepublish());
  t.forceSetState(TRACK_PLAYING);
  TEST_ASSERT_TRUE(t.isPlaying());
  TEST_ASSERT_TRUE(&t.loopWithPendingSnapshotPublish() == &target);
  SnapshotGateRows::reset();
  unsigned turns = 0;
  for (; turns < 20000 && target.hasPendingSnapshotPublish(); ++turns) {
    t.processDeferredIdleMaintenance(0, false, MidiServiceDrain::DeferredIdleOwner::Snapshot, 1);
  }
  // The row's progress fields: the eligible-turn counter advances with the turns the owner stepped (cadence unchanged:
  // far fewer rows than turns), and an in-flight construction reports its completed units.
  TEST_ASSERT_TRUE(SnapshotGateRows::count >= 2 && SnapshotGateRows::count < turns);
  TEST_ASSERT_TRUE(SnapshotGateRows::last()->eligibleTurns > SnapshotGateRows::rows[0].eligibleTurns);
  bool sawUnits = false;
  for (size_t i = 0; i < SnapshotGateRows::count && i < 64; ++i) sawUnits |= SnapshotGateRows::rows[i].inFlightUnits > 0;
  TEST_ASSERT_TRUE(sawUnits);
  TEST_ASSERT_FALSE_MESSAGE(target.hasPendingSnapshotPublish(), "a playing track must still serve the pending slot");
  TEST_ASSERT_TRUE(target.publishedSnapshotIsCurrent());
}

// Task 3.10 — **the driven composition: a blocked holder, later holders owing preparation, and a real capture
// finalization in one window, all carried by the production deferred-owner rotation.**
//
// The task's own fixture text asks for an early holder that is **blocked** while later holders need preparation and
// capture/persistence work is pending, with a progress bound for **every admitted holder** (including inbound switch
// preparation and overlapping finalization). The pure fixtures state the fairness rules where they are exact; this one
// drives the composition `src/main.cpp` runs, with the other owners **executed** rather than counted:
//
//   owner = selectDeferredIdleOwner(...)            -- one owner per idle turn, the production rotation
//   PlaybackViewMaintenance -> every track's offer -> selectPlaybackHolderRoundRobin -> one grain on the chosen track
//   Snapshot / VisualCache / VisualCacheWrapAppend -> the selected realtime track's processDeferredIdleMaintenance
//   Persistence / LoadAndDisplay -> the storage owner's own boundary, offered on every turn with the rotation's owner
//
// The holding set: track 1 owes a **blocked** demand (retained while the clock was in an earlier epoch: the obligation
// survives, only its position is void -- `committedPlaybackRetryCanProgress`'s own false); tracks 2 and 3 owe real,
// admitted window builds; track 0 is the **selected, playing** track and carries a **real accepted overdub stop**, so
// its finalization is in flight and its **sealed capture chunks** are a real persistence obligation in the same window.
// (A workspace save cannot be requested in this harness -- `StorageManager::requestDeferredSaveState` returns early on
// `bootRecoveryBlocked()` -- so the class measured here is the sealed-chunk one; the save/revision class's completion
// through the same boundary is the storage suite's `test_i4_revision_only_save_dispatches_and_completes_...`.) The
// clock advances by the declaration's own `maximumPhaseAdvancePerTurn` per turn, so every gap measured here is in the
// currency the envelope is stated in.
//
// The bound asserted for each kind is the rotation's own: **no advance waits longer than one rotation of the seven
// deferred owners**, and no owner loses or doubles its turn while the lane owes windows.
namespace {
SessionMidiEventVec& compositionWindowScratch(uint8_t track, uint8_t slot) {
  static SessionMidiEventVec buffers[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK];
  return buffers[track % Config::NUM_TRACKS][slot % Config::MAX_LOOPS_PER_TRACK];
}

/// Publish `count` note pairs inside `[fromBar, toBar)` as the slot's current root, so the committed lane has an
/// interval to gather. The same construction the runtime-working-set suite's window fixtures use.
void publishCompositionRootNotes(Loop& loop, uint32_t fromBar, uint32_t toBar, uint32_t count) {
  auto empty = std::make_shared<const LoopSnapshot>(
      LoopSnapshot(loop.loopId, loop.loopLengthTicks, 0, 8, 8));
  LoopSnapshotChangeSet changes;
  const uint32_t span = (toBar - fromBar) * Config::TICKS_PER_BAR;
  EventId nextEventId = 9000;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t on = fromBar * Config::TICKS_PER_BAR + (span * i) / (count > 0 ? count : 1);
    const uint8_t pitch = static_cast<uint8_t>(36 + (i % 40));
    SnapshotNoteChange add{};
    add.kind = SnapshotChangeKind::Add;
    add.next.loopId = loop.loopId;
    add.next.noteId = static_cast<NoteId>(4000 + i);
    add.next.pitch = pitch;
    add.next.velocity = 100;
    add.next.startTick = on;
    add.next.endTick = on + 48;
    changes.notes.push_back(add);
    SnapshotEventChange on_change{};
    on_change.kind = SnapshotChangeKind::Add;
    on_change.next.loopId = loop.loopId;
    on_change.next.eventId = nextEventId++;
    on_change.next.canonicalOrder = nextEventId * 2;
    on_change.next.value = MidiEvent::NoteOn(on, 1, pitch, 100);
    changes.events.push_back(on_change);
    SnapshotEventChange off_change{};
    off_change.kind = SnapshotChangeKind::Add;
    off_change.next.loopId = loop.loopId;
    off_change.next.eventId = nextEventId++;
    off_change.next.canonicalOrder = nextEventId * 2 + 1;
    off_change.next.value = MidiEvent::NoteOff(on + 48, 1, pitch, 0);
    changes.events.push_back(off_change);
  }
  LoopSnapshotConstruction construction(empty, changes);
  while (!construction.complete()) {
    TEST_ASSERT_TRUE(construction.step(128));
  }
  const ImmutableLoopSnapshotRef snapshot = construction.freeze();
  TEST_ASSERT_NOT_NULL(snapshot.get());
  loop.storePublishedSnapshotForTest(snapshot, loop.playbackRevision);
}

/// One slot with committed content, a published root and a prewarmed runtime: the shape a window request needs.
void stageCompositionSlot(Track& track, uint8_t slot, uint32_t loopTicks, uint32_t fromBar, uint32_t toBar,
                          uint32_t notes) {
  track.setActiveLoopIndex(slot);
  Loop& loop = track.getLoop(slot);
  loop.loopLengthTicks = loopTicks;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(0, 1, 48, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(48, 1, 48, 0)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(loopTicks, CommitReason::RecordStop));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass());
  publishCompositionRootNotes(loop, fromBar, toBar, notes);
  TEST_ASSERT_TRUE_MESSAGE(track.prewarmPlaybackForSlot(slot),
                           "the slot must prewarm a runtime to owe a window grain");
}

/// A window holder that **cannot progress**: a committed demand retained while the clock was in an earlier epoch.
void stageBlockedCompositionHolder(Track& track, uint8_t slot, uint32_t loopTicks, uint32_t fromBar, uint32_t toBar,
                                   uint32_t notes, uint64_t tick) {
  stageCompositionSlot(track, slot, loopTicks, fromBar, toBar, notes);
  const PlaybackCoverageInterval coverage{fromBar * Config::TICKS_PER_BAR, 2 * Config::TICKS_PER_BAR, loopTicks};
  TEST_ASSERT_EQUAL_MESSAGE(
      static_cast<int>(Track::PlaybackViewRequestResult::DeferredSourceQualification),
      static_cast<int>(track.requestCommittedPlaybackWindow(slot, coverage, fromBar * Config::TICKS_PER_BAR,
                                                            {tick, 1u})),
      "the blocked holder's demand is retained as a promise its owner still owes");
}

/// A window holder that owes **real work**: an admitted build, gathered from the slot's published root.
void stageOwingCompositionHolder(Track& track, uint8_t slot, uint32_t loopTicks, uint32_t fromBar, uint32_t toBar,
                                 uint32_t notes, uint32_t requestPhase, uint8_t trackIndex, uint64_t tick) {
  stageCompositionSlot(track, slot, loopTicks, fromBar, toBar, notes);
  SessionMidiEventVec& scratch = compositionWindowScratch(trackIndex, slot);
  scratch.clear();
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Track::PlaybackViewRequestResult::Admitted),
                            static_cast<int>(track.requestPlaybackWindow(slot, requestPhase, scratch, {tick, 1u})),
                            "the fixture's owing window must sit inside the declared envelope");
}
}  // namespace

void test_3_10_the_owner_rotation_carries_a_blocked_holder_finalization_and_persistence() {
  constexpr uint32_t kBar = Config::TICKS_PER_BAR;
  constexpr uint32_t kLoopTicks = 32u * kBar;
  constexpr uint32_t kNotes = 20u;
  constexpr uint32_t kNoteBar = 12u;
  constexpr uint32_t kRequestPhase = kNoteBar * kBar + 384u;
  constexpr uint8_t kFinalizingTrack = 0;
  constexpr uint8_t kHolderCount = 3;  // one blocked holder ahead of two owing ones
  const uint8_t kOwnerCount = MidiServiceDrain::kDeferredIdleOwnerCount;
  LoopEventStore::initPool();
  for (uint8_t i = 0; i < trackManager.getTrackCount(); ++i) {
    trackManager.getTrack(i).ensureLoopsAllocated();
  }

  // (1) Holder 1 is **blocked**; holders 2 and 3 owe real builds (two bars of notes at the request bar, so the
  // gathered interval is the two-bar window the demoted policy selects). The blocked holder still sits **ahead** of
  // every holder that can progress, which is the ordering the fairness rule must survive.
  constexpr uint8_t kBlockedTrack = 1;
  constexpr uint8_t kOwingTrackA = 2;
  constexpr uint8_t kOwingTrackB = 3;
  stageBlockedCompositionHolder(trackManager.getTrack(kBlockedTrack), 0, kLoopTicks, kNoteBar, kNoteBar + 1, kNotes,
                                1000000ull);
  stageOwingCompositionHolder(trackManager.getTrack(kOwingTrackA), 0, kLoopTicks, kNoteBar, kNoteBar + 1, kNotes,
                              kRequestPhase, kOwingTrackA, 1000064ull);
  stageOwingCompositionHolder(trackManager.getTrack(kOwingTrackB), 0, kLoopTicks, kNoteBar, kNoteBar + 1, kNotes,
                              kRequestPhase, kOwingTrackB, 1000128ull);
  TEST_ASSERT_TRUE(trackManager.getTrack(kOwingTrackA).hasPendingPlaybackViewMaintenance());
  TEST_ASSERT_TRUE(trackManager.getTrack(kOwingTrackB).hasPendingPlaybackViewMaintenance());

  // (2) A **real accepted overdub stop** on the selected track: the finalization is in flight for the whole fixture,
  // and the same stop leaves a real persistence obligation behind. The selected track is where `main.cpp`'s Snapshot
  // owner turn and its realtime track loop both reach the job, so the finalization is reachable on every owner turn
  // whose owner owns it.
  Track& finalizing = trackManager.getTrack(kFinalizingTrack);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(kFinalizingTrack, trackManager.getSelectedTrackIndex(),
                                  "the finalizing track is the selected one");
  prepareSlot(finalizing, 0, true);
  start(finalizing, 0, true, 96, 60);
  stop(finalizing, true, 144);
  TEST_ASSERT_TRUE_MESSAGE(finalizing.hasPendingDeferredOverdubStop() || finalizing.hasStopInFlight(),
                           "fixture precondition: the stop is accepted and its finalization is in flight");
  TEST_ASSERT_TRUE_MESSAGE(finalizing.hasRealtimeDeferredIdleWork(),
                           "and the production gate that reaches this track on a non-lane owner turn holds");
  // The stop's own persistence obligation: its sealed chunks are queued for the incremental flush, which is the class
  // that is admitted on any owner turn. (The workspace-save request is issued here too, through the production entry the
  // stop paths use, but this harness answers `bootRecoveryBlocked()` and drops it -- measured, not assumed; the assertion
  // below therefore requires *an* obligation and the drive asserts the class it actually got.)
  StorageManager::requestDeferredSaveState(looperState.getLooperState());
  const bool persistencePending = StorageManager::hasDeferredSaveWork() ||
                                  StorageManager::hasQueuedCaptureChunksToPersist() ||
                                  StorageManager::hasRevisionCommitWork();
  TEST_ASSERT_TRUE_MESSAGE(persistencePending,
                           "the same window must carry a real persistence obligation, not only a finalization");
  TEST_ASSERT_TRUE_MESSAGE(StorageManager::hasQueuedCaptureChunksToPersist(),
                           "this window's persistence work is the same stop's sealed chunks");

  MidiServiceDrain::DeferredIdleOwnerCursor ownerCursor{};
  MidiServiceDrain::PlaybackHolderCursor laneCursor{};
  uint32_t ownerTurns[MidiServiceDrain::kDeferredIdleOwnerCount] = {};
  uint32_t eligibleTurns[Config::NUM_TRACKS] = {};
  uint32_t servedTurns[Config::NUM_TRACKS] = {};
  uint32_t lastServedLaneTurn[Config::NUM_TRACKS] = {};
  uint32_t maxLaneGap = 0;
  uint32_t laneTurns = 0;
  uint32_t laneOpportunityTurns = 0;
  uint32_t finalizationAdvances = 0;
  uint32_t maxFinalizationGap = 0;
  uint32_t lastFinalizationAdvanceTurn = 0;
  uint32_t persistenceTurns = 0;
  uint32_t chunkOwedButUnserved = 0;
  uint32_t ownerOwedButUnserved = 0;
  uint32_t turnsUsed = 0;
  const auto finalizationDone = [&]() {
    return !finalizing.hasPendingDeferredOverdubStop() && !finalizing.hasStopInFlight() &&
           !TrackCaptureStopTestAccess::recordWorkPending(finalizing);
  };
  const auto laneDone = [&]() {
    return !trackManager.getTrack(kOwingTrackA).hasPendingPlaybackViewMaintenance() &&
           !trackManager.getTrack(kOwingTrackB).hasPendingPlaybackViewMaintenance();
  };
  const auto persistenceDone = [&]() {
    return !StorageManager::hasDeferredSaveWork() && !StorageManager::hasQueuedCaptureChunksToPersist() &&
           !StorageManager::hasRevisionCommitWork();
  };

  PlaybackDeadlineClock clock{1001000ull, 2u};
  const uint32_t kTurnCap = 4096u;
  for (uint32_t turn = 1; turn <= kTurnCap; ++turn) {
    if (finalizationDone() && laneDone() && persistenceDone()) break;
    turnsUsed = turn;
    clock.tick += Track::kPlaybackViewPermittedAdvancePerTurn;
    const MidiServiceDrain::DeferredIdleOwner owner =
        MidiServiceDrain::selectDeferredIdleOwner(ownerCursor, false);
    ++ownerTurns[static_cast<uint8_t>(owner)];
    if (owner == MidiServiceDrain::DeferredIdleOwner::PlaybackViewMaintenance) {
      ++laneOpportunityTurns;
      const uint8_t trackCount = trackManager.getTrackCount();
      MidiServiceDrain::PlaybackHolder offers[Config::NUM_TRACKS] = {};
      for (uint8_t i = 0; i < trackCount; ++i) {
        Track& track = trackManager.getTrack(i);
        track.serviceAdmittedDeadlineMisses(clock);
        offers[i] = track.playbackWindowServiceOffer(static_cast<uint32_t>(clock.tick), clock);
        if (offers[i].eligible) ++eligibleTurns[i];
      }
      uint8_t chosen = MidiServiceDrain::kNoPlaybackHolder;
      if (MidiServiceDrain::selectPlaybackHolderRoundRobin(laneCursor, offers, trackCount, chosen) ==
          MidiServiceDrain::PlaybackHolderSelection::Service) {
        ++servedTurns[chosen];
        ++laneTurns;
        if (lastServedLaneTurn[chosen] != 0u) {
          const uint32_t gap = laneTurns - lastServedLaneTurn[chosen];
          if (gap > maxLaneGap) maxLaneGap = gap;
        }
        lastServedLaneTurn[chosen] = laneTurns;
        (void)trackManager.getTrack(chosen).stepPlaybackViewMaintenance(
            static_cast<uint32_t>(clock.tick), nullptr, false, clock);
      }
      continue;
    }
    // `src/main.cpp` offers the storage boundary on **every** turn, with the rotation's own owner; the boundary decides
    // whether this owner's turn is its opportunity. That is the composition being measured, so it is driven here too.
    // **The persistence bound, stated as the boundary's own two admission classes** (never as an invented cadence):
    // sealed capture chunks are admitted on *any* owner turn, while the save/revision classes are admitted only on the
    // persistence owner's own turn. Both halves can fail: a collision or an owner-gate mistake leaves owed work
    // unserved, and the rotation is what supplies the owner turn in the second class.
    const bool chunkClassOwed = StorageManager::hasQueuedCaptureChunksToPersist();
    const bool ownerClassOwed =
        StorageManager::hasDeferredSaveWork() || StorageManager::hasRevisionCommitWork();
    const bool persistenceRan = MidiServiceDrain::serviceDeferredPersistenceTurn<StorageManager>(
        looperState.getLooperState(), owner, true, false, 0, false, false);
    if (persistenceRan) ++persistenceTurns;
    if (chunkClassOwed && !persistenceRan) ++chunkOwedButUnserved;
    if (ownerClassOwed && !persistenceRan && owner == MidiServiceDrain::DeferredIdleOwner::Persistence) {
      ++ownerOwedButUnserved;
    }
    if (owner == MidiServiceDrain::DeferredIdleOwner::Persistence ||
        owner == MidiServiceDrain::DeferredIdleOwner::LoadAndDisplay ||
        owner == MidiServiceDrain::DeferredIdleOwner::SlotProvision) {
      // `main.cpp` hands these three to their own owners (storage, display, and the pending-switch provisioning) and
      // never reaches a track's deferred maintenance through them. The persistence boundary above already took its own
      // opportunity on these turns; none of the three may consume the finalization's.
      continue;
    }
    if (finalizationDone()) continue;
    // The production gate that decides whether a non-lane owner turn reaches this track at all
    // (`hasRealtimeDeferredIdleWork`, the term the 2026-10-08 cross-track finalization repair added). A track that is
    // merely stopped and owes no job is not visited; one holding an accepted stop is.
    if (!finalizing.hasRealtimeDeferredIdleWork()) continue;
    const int phaseBefore = TrackCaptureStopTestAccess::headStopPhase(finalizing);
    const uint64_t budgetBefore = TrackCaptureStopTestAccess::budget(finalizing);
    finalizing.processDeferredIdleMaintenance(0, false, owner);
    // **Progress is work, not a phase name.** A bounded grain can repeat inside one phase (the closure tail does), so
    // the observable is the job's own remaining-work figure moving, or the phase/ job moving at all.
    const bool advanced = finalizationDone() ||
                          TrackCaptureStopTestAccess::headStopPhase(finalizing) != phaseBefore ||
                          TrackCaptureStopTestAccess::budget(finalizing) < budgetBefore;
    if (!advanced) continue;
    ++finalizationAdvances;
    if (lastFinalizationAdvanceTurn != 0u) {
      const uint32_t gap = turn - lastFinalizationAdvanceTurn;
      if (gap > maxFinalizationGap) maxFinalizationGap = gap;
    }
    lastFinalizationAdvanceTurn = turn;
  }

  // (A) **All three owed kinds complete in the same window**: the real finalization, both real window builds, and the
  // real persistence obligation.
  TEST_ASSERT_TRUE_MESSAGE(finalizationDone(), "the real finalization completes inside the rotation");
  TEST_ASSERT_TRUE_MESSAGE(laneDone(), "both owing windows complete inside the same window");
  TEST_ASSERT_TRUE_MESSAGE(persistenceDone(), "and the persistence obligation completes too");

  // (B) **The progress bound, in the rotation's own unit, for each kind** -- no advance waits longer than one rotation
  // of the seven deferred owners. This is the task's "progress bound for every admitted holder ... including overlapping
  // finalization", stated where it can fail: a kind reachable on too few owners, or stalled behind the lane, crosses it.
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0u, finalizationAdvances, "the finalization advanced to get there");
  // **The bound is the rotation itself, and the measurement is tight against it** (observed worst gap: one full
  // rotation, 7 turns, in this window). A stop's grain is offered on the three track-maintenance owners -- `Snapshot`,
  // `VisualCache`, `VisualCacheWrapAppend` -- so one rotation is the declared maximum; anything that removes an owner
  // *or* stalls the job behind the lane crosses it or fails the completion assertion above.
  TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(
      kOwnerCount, maxFinalizationGap,
      "every finalization advance is at most one deferred-owner rotation after the last");
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0u, persistenceTurns, "the persistence obligation did real work");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, chunkOwedButUnserved,
                                   "the any-owner class (sealed chunks) is never refused while it is owed");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, ownerOwedButUnserved,
                                   "and the owned class (save/revision) is never refused on its own owner's turn");

  // (C) **The lane's bound survives the composition**: neither progressable holder waited more than one rotation of the
  // live holder set (three holders, one of them blocked).
  TEST_ASSERT_GREATER_THAN_UINT32(0, laneTurns);
  const uint8_t owingTracks[2] = {kOwingTrackA, kOwingTrackB};
  for (uint8_t i = 0; i < 2; ++i) {
    TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0u, servedTurns[owingTracks[i]], "each owing holder was served");
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(eligibleTurns[owingTracks[i]] / kHolderCount,
                                                servedTurns[owingTracks[i]],
                                                "one service per rotation of the still-owing set");
  }
  TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(3u, maxLaneGap,
                                           "no progressable holder waits beyond one rotation of three holders");

  // (D) **The blocked holder is owed and offered on every lane turn, and never served** -- it neither takes the
  // rotation's position nor drops out of it.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, servedTurns[kBlockedTrack],
                                   "a blocked holder never consumes the lane's turn");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(laneOpportunityTurns, eligibleTurns[kBlockedTrack],
                                   "and it is offered on every lane opportunity while its obligation survives");

  // (E) **No track that owes no window is served**, so the lane's turn is not handed to an unrelated track -- the
  // finalizing track included.
  for (uint8_t i = 0; i < trackManager.getTrackCount(); ++i) {
    if (i == kBlockedTrack || i == kOwingTrackA || i == kOwingTrackB) continue;
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, servedTurns[i], "a track with no owed window never takes the lane's turn");
  }

  // (F) **Every owner kept exactly its own turn per rotation**: the lane's work does not absorb the persistence,
  // slot-provision or snapshot opportunities, and no owner takes a second turn inside a rotation.
  const uint32_t rotations = turnsUsed / static_cast<uint32_t>(kOwnerCount);
  TEST_ASSERT_GREATER_THAN_UINT32(0, rotations);
  for (uint8_t owner = 0; owner < kOwnerCount; ++owner) {
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(rotations, ownerTurns[owner],
                                                "no owner loses a turn while the window carries all three kinds");
    TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(rotations + 1u, ownerTurns[owner],
                                             "and no owner takes a second turn in a rotation");
  }
}

// Task 3.15a — **the live overdub/wrap route at the production edge, plus the control that makes it an oracle.**
//
// The audit's route table leaves "live overdub / wrap" UNKNOWN: the suites run the edge and commit overdub passes, but no
// fixture asserts the **overdub overlay's ordered stream through the reader**. This is that oracle, and it is the *cold
// reference* 3.15f compares the migrated route against: it drives the production clock entry (`Track::playMidiEvents`),
// not a lane helper, and it states an **independent** ordered expectation -- explicit ticks, explicit notes, explicit
// On/Off order -- rather than reading the answer back out of the thing under test.
//
// Both worlds carry the **same** published root (48 in bar one, 62 in bar two) over the same two-bar pass, and both are
// driven identically: two whole traversals in 64-tick steps, so the wrap is crossed by the production wrap logic. Only
// world A also commits an **overdub of 65 in bar two**, through the production session and the real deferred
// finalization. Because the overdub is recorded *after* the root published, that root is stale for the region the
// overlay supplies -- the exact "live overlay over a stale root" shape the route is about -- and the 65s can only reach
// the wire if the overlay is served. World B serves four events per traversal, world A six: the difference **is** the
// overlay, which is what makes the oracle falsifiable without arguing about internals.
struct RootPair {
  uint8_t note;
  uint32_t on;
  uint32_t off;
};

void publishRootPairs(Loop& loop, const RootPair* pairs, size_t count) {
  auto empty = std::make_shared<const LoopSnapshot>(LoopSnapshot(loop.loopId, loop.loopLengthTicks, 0, 8, 8));
  LoopSnapshotChangeSet changes;
  EventId nextEventId = 7000;
  for (size_t i = 0; i < count; ++i) {
    const RootPair& pair = pairs[i];
    SnapshotNoteChange add{};
    add.kind = SnapshotChangeKind::Add;
    add.next.loopId = loop.loopId;
    add.next.noteId = static_cast<NoteId>(3000 + i);
    add.next.pitch = pair.note;
    add.next.velocity = 100;
    add.next.startTick = pair.on;
    add.next.endTick = pair.off;
    changes.notes.push_back(add);
    SnapshotEventChange on_change{};
    on_change.kind = SnapshotChangeKind::Add;
    on_change.next.loopId = loop.loopId;
    on_change.next.eventId = nextEventId++;
    on_change.next.canonicalOrder = nextEventId * 2;
    on_change.next.value = MidiEvent::NoteOn(pair.on, 1, pair.note, 100);
    changes.events.push_back(on_change);
    SnapshotEventChange off_change{};
    off_change.kind = SnapshotChangeKind::Add;
    off_change.next.loopId = loop.loopId;
    off_change.next.eventId = nextEventId++;
    off_change.next.canonicalOrder = nextEventId * 2 + 1;
    off_change.next.value = MidiEvent::NoteOff(pair.off, 1, pair.note, 0);
    changes.events.push_back(off_change);
  }
  LoopSnapshotConstruction construction(empty, changes);
  while (!construction.complete()) {
    TEST_ASSERT_TRUE(construction.step(128));
  }
  const ImmutableLoopSnapshotRef snapshot = construction.freeze();
  TEST_ASSERT_NOT_NULL(snapshot.get());
  loop.storePublishedSnapshotForTest(snapshot, loop.playbackRevision);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
}

/// Stage one world: the two-bar pass, the published root, and a prewarmed runtime. The root and the pass carry the same
/// two soundings, so the overlay's contribution is the only difference between the worlds.
void stageTwoBarWorld(Track& t, uint8_t slot, Loop*& loopOut) {
  constexpr uint32_t kBar = Config::TICKS_PER_BAR;
  constexpr uint32_t kLoopTicks = 2u * kBar;
  Loop& loop = t.getLoop(slot);
  loop.loopLengthTicks = kLoopTicks;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(12, 1, 48, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(96, 1, 48, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(kBar + 32, 1, 62, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(kBar + 112, 1, 62, 0)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(kLoopTicks, CommitReason::RecordStop));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass());
  const RootPair pairs[] = {{48, 12, 96}, {62, kBar + 32, kBar + 112}};
  publishRootPairs(loop, pairs, 2);
  TEST_ASSERT_TRUE_MESSAGE(t.prewarmPlaybackForSlot(slot, true),
                           "the slot must hold a playback runtime before the edge is driven");
  loopOut = &loop;
}

void serveTwoBarTraversalsAndCheckOrder(Track& t, Loop& loop, uint8_t slot, bool expectOverdub) {
  constexpr uint32_t kBar = Config::TICKS_PER_BAR;
  constexpr uint32_t kLoopTicks = 2u * kBar;
  constexpr uint32_t kTraversals = 2;
  struct ExpectedWire {
    uint32_t tick;
    uint8_t note;
    bool on;
  };
  const ExpectedWire kRecordOnly[] = {
      {12, 48, true}, {96, 48, false}, {kBar + 32, 62, true}, {kBar + 112, 62, false}};
  const ExpectedWire kWithOverdub[] = {
      {12, 48, true},          {96, 48, false},
      {kBar + 32, 62, true},   {kBar + 112, 62, false},
      {1112, 65, true},        {1196, 65, false}};
  const ExpectedWire* expected = expectOverdub ? kWithOverdub : kRecordOnly;
  const size_t perTraversal = expectOverdub ? 6u : 4u;

  MidiSendCapture::reset();
  t.forceSetState(TRACK_PLAYING);
  TrackCaptureStopTestAccess::unmute(t);
  TrackCaptureStopTestAccess::selectSlot(t, slot);
  TrackCaptureStopTestAccess::resetProjectionCycle(t);
  loop.lastTickInLoop = 0;
  loop.nextEventIndex = 0;
  for (uint32_t tick = 64; tick <= kTraversals * kLoopTicks; tick += 64) {
    t.playMidiEvents(tick, true);
  }

  // (A) **The exact ordered stream, twice.** Each traversal serves the same events at the same loop-local ticks, in the
  // same order -- the whole "no replay, no omission, no tear at the wrap" claim in one comparison.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(perTraversal * kTraversals),
                                   static_cast<uint32_t>(MidiSendCapture::count()),
                                   expectOverdub
                                       ? "each sounding (record and overlay) is served once per traversal"
                                       : "the record pass alone is served once per traversal");
  for (uint32_t traversal = 0; traversal < kTraversals; ++traversal) {
    for (size_t i = 0; i < perTraversal; ++i) {
      const MidiEvent& event = MidiSendCapture::at(traversal * perTraversal + i);
      const ExpectedWire& want = expected[i];
      char message[160];
      snprintf(message, sizeof(message), "%s traversal %u event %u: tick %u note %u %s",
               expectOverdub ? "overdub" : "record-only", static_cast<unsigned>(traversal),
               static_cast<unsigned>(i), want.tick, static_cast<unsigned>(want.note),
               want.on ? "on" : "off");
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(want.tick, event.tick, message);
      TEST_ASSERT_EQUAL_UINT8_MESSAGE(want.note, event.data.noteData.note, message);
      TEST_ASSERT_EQUAL_MESSAGE(want.on, event.isNoteOn(), message);
    }
  }

  // (B) **The ledger closes after every traversal**, so the second traversal's ons are independent events and not a
  // re-trigger over a still-open note.
  unsigned sounding = 0;
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, slot);
  runtime.ledger.forEachActive([&](uint8_t, uint8_t, const ActiveNoteLedger::Entry&) { ++sounding; });
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, sounding, "every served note is closed by its own off");

  // (C) **One On per note per traversal**, counted independently of the order comparison above.
  const uint8_t notes[] = {48, 62, 65};
  for (uint8_t note : notes) {
    uint32_t ons = 0;
    for (size_t i = 0; i < MidiSendCapture::count(); ++i) {
      const MidiEvent& event = MidiSendCapture::at(i);
      if (event.isNoteOn() && event.data.noteData.note == note) ++ons;
    }
    if (!expectOverdub && note == 65) {
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, ons, "the control world has no overlay note at all");
      continue;
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(kTraversals, ons, "each sounding is articulated once per traversal");
  }
}

void test_3_15a_a_committed_overdub_is_served_exactly_once_per_traversal_across_the_wrap() {
  LoopEventStore::initPool();
  Track& t = trackManager.getTrack(0);
  t.ensureLoopsAllocated();

  // World A -- root + one overdub note in bar two, recorded after that root published, retired through the real
  // deferred finalization.
  Loop* loopA = nullptr;
  stageTwoBarWorld(t, 0, loopA);
  TEST_ASSERT_NOT_NULL(loopA);
  t.forceSetState(TRACK_PLAYING);
  start(t, 0, true, 1100, 65);  // input lands at 1112..1196
  stop(t, true, 1200);
  TEST_ASSERT_TRUE_MESSAGE(t.hasPendingDeferredOverdubStop() || t.hasStopInFlight(),
                           "fixture precondition: the overdub stop is a deferred job");
  driveStopToRetirement(t);
  TEST_ASSERT_FALSE_MESSAGE(t.hasStopInFlight(), "the overdub finalization must retire before the oracle runs");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, static_cast<uint32_t>(loopA->passes.overdubPasses.size()),
                                   "the overdub committed exactly one pass");
  {
    SessionMidiEventVec rows;
    LoopEventStore::appendChunkRefEvents(loopA->passes.overdubPasses.back().committedChunkIds, rows);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u, static_cast<uint32_t>(rows.size()),
                                     "the overdub pass holds the note's own pair");
    TEST_ASSERT_EQUAL_UINT8(65, rows[0].data.noteData.note);
    TEST_ASSERT_EQUAL_UINT32(1112u, rows[0].tick);
    TEST_ASSERT_EQUAL_UINT32(1196u, rows[1].tick);
  }
  TEST_ASSERT_FALSE_MESSAGE(loopA->publishedSnapshotIsCurrent(),
                            "the root published before the overdub is stale for the overlay's region");
  serveTwoBarTraversalsAndCheckOrder(t, *loopA, 0, true);

  // World B -- the control: the same root, pass and drive, with no overdub at all.
  Loop* loopB = nullptr;
  stageTwoBarWorld(t, 1, loopB);
  TEST_ASSERT_NOT_NULL(loopB);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(loopB->passes.overdubPasses.size()),
                                   "the control world carries no overlay");
  serveTwoBarTraversalsAndCheckOrder(t, *loopB, 1, false);
}

// Task 3.15e slice 2b-ii — **the per-overdub overlay demand, derived from live state only.**
//
// The overlay lane may not keep a demand record (that would be the hidden fourth state the arbitration contract forbids), so
// the demand *is* a reading of live state: an overdub that is actually carrying rows on top of committed content owes a
// window whose coverage is the committed route's own policy at the playhead, whose identity is the live capture generation,
// and whose pass/stop boundaries make it disappear rather than go stale. The claim the arbiter consumes is built from the
// same reading, which is what keeps "identity beats class" honest across the wrap.
void test_3_15e_slice2b_the_overlay_demand_is_the_live_generation_and_the_committed_interval() {
  static constexpr uint32_t kLoopTicks = 2u * Config::TICKS_PER_BAR;
  LoopEventStore::initPool();
  Track& t = trackManager.getTrack(0);
  t.ensureLoopsAllocated();
  Loop* loopPtr = nullptr;
  stageTwoBarWorld(t, 0, loopPtr);
  TEST_ASSERT_NOT_NULL(loopPtr);
  Loop& loop = *loopPtr;
  t.forceSetState(TRACK_PLAYING);
  TrackCaptureStopTestAccess::unmute(t);
  TrackCaptureStopTestAccess::selectSlot(t, 0);
  TrackCaptureStopTestAccess::resetProjectionCycle(t);
  loop.lastTickInLoop = 0;
  loop.nextEventIndex = 0;
  const auto position = [&](uint32_t tick) {
    clockManager.assignCurrentTickSilently(tick);
    nativePlaybackDeadlineClock = {tick, 1};
  };
  const auto play = [&](uint32_t tick) { position(tick); t.playMidiEvents(tick, true); };

  // (1) Not overdubbing: nothing owed, but the base the overlay would carry is present. A slot with no committed content
  // has no base and therefore no demand either.
  const Track::OverdubOverlayDemand idle = t.overdubOverlayDemandForSlot(0, 100);
  TEST_ASSERT_FALSE(idle.owed);
  TEST_ASSERT_TRUE(idle.baseCommitted);
  TEST_ASSERT_EQUAL_UINT64(0, idle.captureGeneration);
  const Track::OverdubOverlayDemand empty = t.overdubOverlayDemandForSlot(1, 100);
  TEST_ASSERT_FALSE(empty.owed);
  TEST_ASSERT_FALSE_MESSAGE(empty.baseCommitted, "a slot with no committed content owes no overlay");

  // (2) A live overdub with no rows yet carries nothing, so there is nothing to prepare.
  position(kLoopTicks + 1024);
  t.startOverdubbing(kLoopTicks + 1024);
  TEST_ASSERT_TRUE(t.isOverdubbing());
  const Track::OverdubOverlayDemand opened = t.overdubOverlayDemandForSlot(0, 1024);
  TEST_ASSERT_FALSE(opened.owed);
  TEST_ASSERT_TRUE(opened.baseCommitted);

  // (3) With rows, the demand names the live capture generation, the current playback revision, and the **committed
  // route's own** coverage policy at the playhead (so the overlay replaces exactly that interval).
  t.noteOn(1, 70, 100, kLoopTicks + 1470);
  t.noteOff(1, 70, 0, kLoopTicks + 1500);
  // (2b-0) **The waiting state is a pure observation.** Before any canonical work exists, the lane's step is
  // `WaitingCanonicalBase` and observing it changes nothing: the identity quartet is reported, and the capture, revision,
  // canonical generation and demand are all unchanged afterwards.
  {
    using namespace OverdubOverlayLane;
    auto& rt = TrackCaptureStopTestAccess::runtimeFor(t, 0);
    const Track::OverdubOverlayDemand demand = t.overdubOverlayDemandForSlot(0, 1470);
    Observation observation;
    observation.demandOwed = demand.owed;
    observation.baseCommitted = demand.baseCommitted;
    observation.canonicalReady = rt.canonicalPassInputs.ready();
    observation.committedRevision = demand.playbackRevision;
    observation.canonicalGeneration = rt.canonicalPassInputs.generation();
    observation.captureGeneration = demand.captureGeneration;
    observation.coverage = demand.coverage;
    TEST_ASSERT_FALSE_MESSAGE(observation.canonicalReady, "fixture precondition: no canonical base yet");
    TEST_ASSERT_EQUAL(Step::WaitingCanonicalBase, observe(observation));
    TEST_ASSERT_TRUE(waiting(observe(observation)));
    // Purity: the same observation reports the same identity and nothing moved.
    TEST_ASSERT_EQUAL_UINT32(loop.playbackRevision, observation.committedRevision);
    TEST_ASSERT_EQUAL_UINT64(loop.captureContentGeneration(), observation.captureGeneration);
    TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(rt.canonicalPassInputs.passCount()));
    TEST_ASSERT_FALSE(t.overdubOverlayDemandForSlot(0, 1470).owed == false);
    // A candidate whose identity moved retires before anything is built, and a slot with no base is not "waiting".
    Observation stale = observation;
    stale.candidateIdentityMismatch = true;
    TEST_ASSERT_EQUAL(Step::RetireStaleCandidate, observe(stale));
    Observation noBase = observation;
    noBase.baseCommitted = false;
    TEST_ASSERT_EQUAL(Step::NotRequired, observe(noBase));
    Observation notOwed = observation;
    notOwed.demandOwed = false;
    TEST_ASSERT_EQUAL(Step::NotRequired, observe(notOwed));
  }

  // (2b) **Liveness of the prerequisite, measured through the real rotation shape.** The overlay lane may observe canonical
  // readiness but never advance it, so a live overlay demand must make the **committed canonical owner** runnable. This is
  // the same `MidiServiceDrain::serviceCanonicalPrefixTurn` + `Track::processDeferredCanonicalPrefix` pair `main.cpp` uses,
  // with the same priority predicate.
  {
    auto& rt = TrackCaptureStopTestAccess::runtimeFor(t, 0);
    uint8_t nextTrack = 0;
    const auto serviceCanonicalTurn = [&]() {
      return MidiServiceDrain::serviceCanonicalPrefixTurn(
          MidiServiceDrain::DeferredIdleOwner::VisualCache, Config::NUM_TRACKS, nextTrack,
          [&](uint8_t index) {
            if (index != 0) return false;
            const Loop& active = t.getActiveLoop();
            return MidiServiceDrain::canonicalPreparationHasPriorityWork(
                t.isOverdubbing(), active.visualCacheDirty, active.hasDeferredVisualCacheWrapAppend(),
                active.overdubSourceSpanCacheReady());
          },
          [&](uint8_t index) { return index == 0 && trackManager.getTrack(0).processDeferredCanonicalPrefix(); });
    };
    TEST_ASSERT_FALSE(rt.canonicalPassInputs.ready());
    // The real rotation establishes the display/source-span prerequisite (paint); with that ready the deprioritization term
    // no longer holds and the canonical owner is runnable. Recorded honestly: the **negative companion** -- that the
    // prerequisite must not steal a turn while `canonicalPreparationHasPriorityWork` is true -- is not demonstrable in this
    // fixture, because the span cache is already established by this point; it needs a dirty-span overdub state of its own.
    if (!loop.overdubSourceSpanCacheReady()) loop.rebuildOverdubSourceSpanCacheForTest(0, loop.loopLengthTicks);
    TEST_ASSERT_TRUE(loop.overdubSourceSpanCacheReady());
    unsigned stepped = 0;
    for (unsigned turn = 0; turn < 400 && !rt.canonicalPassInputs.ready(); ++turn) {
      if (serviceCanonicalTurn()) ++stepped;
    }
    // (i) the prerequisite made measurable progress, (ii) through the committed canonical owner's own grains...
    TEST_ASSERT_TRUE_MESSAGE(rt.canonicalPassInputs.ready(),
                             "a live overlay demand must make its canonical prerequisite runnable");
    TEST_ASSERT_GREATER_THAN_UINT32(0, stepped);
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(rt.canonicalPassInputs.passCount()));
    // ...and (iii) without the overlay lane issuing any canonical work: its only entry is this reading, and it created no
    // committed demand of its own (so nothing here is a second canonical driver).
    TEST_ASSERT_FALSE_MESSAGE(rt.pendingViewRequest_.active,
                              "the overlay demand must not become a committed demand of its own");
    TEST_ASSERT_TRUE(t.overdubOverlayDemandForSlot(0, 1470).owed);
  }
  const Track::OverdubOverlayDemand live = t.overdubOverlayDemandForSlot(0, 1470);
  TEST_ASSERT_TRUE(live.owed);
  TEST_ASSERT_EQUAL_UINT64(loop.captureContentGeneration(), live.captureGeneration);
  TEST_ASSERT_EQUAL_UINT32(loop.playbackRevision, live.playbackRevision);
  PlaybackCoverageInterval want{};
  TEST_ASSERT_TRUE(Track::playbackWindowInterval(loop.loopLengthTicks,
                                                 loop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks), 1470, want));
  TEST_ASSERT_EQUAL_UINT32(want.startPhase, live.coverage.startPhase);
  TEST_ASSERT_EQUAL_UINT32(want.lengthTicks, live.coverage.lengthTicks);
  TEST_ASSERT_EQUAL_UINT32(want.loopLengthTicks, live.coverage.loopLengthTicks);

  // The arbitration claim is current against that same live generation.
  const auto liveClaim = PlaybackDemandClaims::overlay(true, loop.captureContentGeneration(), live.captureGeneration,
                                                       /*readyComplete=*/false);
  TEST_ASSERT_TRUE(liveClaim.identityCurrent);
  TEST_ASSERT_EQUAL(PlaybackDemandArbitration::Class::Overlay, liveClaim.demandClass);

  // (4) The wrap's pass commit advances the capture generation: the demand taken a moment ago is **stale** by identity (not
  // by class), the store restarts, and nothing is owed until new rows exist.
  for (uint32_t tick = kLoopTicks + 1056; tick <= kLoopTicks + 1504; tick += 32) play(tick);
  play(kLoopTicks + 1536);
  TEST_ASSERT_TRUE_MESSAGE(loop.captureContentGeneration() != live.captureGeneration,
                           "the wrap commit must advance the capture generation");
  const auto staleClaim = PlaybackDemandClaims::overlay(true, loop.captureContentGeneration(), live.captureGeneration,
                                                        /*readyComplete=*/true);
  TEST_ASSERT_FALSE_MESSAGE(staleClaim.identityCurrent, "the recorded demand is stale after the pass commit");
  const Track::OverdubOverlayDemand afterWrap = t.overdubOverlayDemandForSlot(0, 0);
  TEST_ASSERT_FALSE_MESSAGE(afterWrap.owed, "a re-based store carries nothing, so nothing is owed");

  // (5) A pending stop ends the demand as well: the overlay lane never owns a boundary once the session is stopping.
  t.noteOn(1, 71, 100, kLoopTicks + 1536 + 200);
  t.noteOff(1, 71, 0, kLoopTicks + 1536 + 260);
  TEST_ASSERT_TRUE(t.overdubOverlayDemandForSlot(0, 200).owed);
  position(kLoopTicks + 1536 + 320);
  t.stopOverdubbing();
  TEST_ASSERT_FALSE(t.isOverdubbing());
  TEST_ASSERT_FALSE_MESSAGE(t.overdubOverlayDemandForSlot(0, 320).owed,
                           "a stopping session owes no overlay boundary");
}

// Task 3.15e — **the live overdub overlay is served exactly once through the wrap, the pass handoff and the stop.**
//
// The existing 3.15a fixture proves a *committed* overdub is served once per traversal; `assertRecordRecoveryServesSecondBar`
// proves a **live** overdub note is emitted once, but never across a wrap. This fixture closes that gap for 3.15e's
// overdub-overlay clause, on the production clock entry (`Track::playMidiEvents`) and the real deferred overdub stop.
//
// Shape: a committed two-bar Record pass with a published root, then a live overdub whose note **A** (70) lies entirely
// **before** the wrap and whose note **B** (71) lies entirely **after** it. Driving the edge over the wrap fires
// `maybeCommitOverdubWrap`: A's pass is sealed and the live store is re-based, so A becomes committed content *inside the
// same traversal*, while B stays the live tail. The oracle pins each hand-off:
//  * A is echoed by the live-capture walk exactly once before the wrap;
//  * the wrap turn emits nothing (the committed interval is owned by the wrap catch-up, so A is not served twice);
//  * A is then served from the committed reader exactly once, at its own phase, while B is still live (B's rows are not
//    replayed by that reader);
//  * the merged store never carries the uncommitted overlay: with A live and uncommitted it holds the base only, and A
//    appears in it only after the wrap commit;
//  * the stop commits B's pass and, after retirement, one traversal emits the base, A and B exactly once each, in tick
//    order, with the ledger closed;
//  * the growing overlay forces **no** content-sized rebuild on the MIDI turn (the counter stays 0 inside the live pass).
//
// Named limit (recorded in the 2026-10-22 packet, not asserted here as satisfied): the live overlay is still served by its
// own `ActiveCaptureOverdub` cursor walk after the committed walk, with `ensureCaptureEventsSorted()` on the turn -- the
// "same reader" half of 3.15e stays open. The ledger split (echo is emit-only, invariant `221334`/`224719`) is exercised
// here: the echo pairs never appear in the ledger and the committed stream still closes.
void test_3_15e_a_live_overdub_is_served_exactly_once_through_the_wrap_and_the_pass_handoff() {
  static constexpr uint32_t kLoopTicks = 2u * Config::TICKS_PER_BAR;
  LoopEventStore::initPool();
  Track& t = trackManager.getTrack(0);
  t.ensureLoopsAllocated();
  Loop* loopPtr = nullptr;
  stageTwoBarWorld(t, 0, loopPtr);
  TEST_ASSERT_NOT_NULL(loopPtr);
  Loop& loop = *loopPtr;
  auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, 0);

  t.forceSetState(TRACK_PLAYING);
  TrackCaptureStopTestAccess::unmute(t);
  TrackCaptureStopTestAccess::selectSlot(t, 0);
  TrackCaptureStopTestAccess::resetProjectionCycle(t);
  loop.lastTickInLoop = 0;
  loop.nextEventIndex = 0;

  const auto position = [&](uint32_t tick) {
    clockManager.assignCurrentTickSilently(tick);
    nativePlaybackDeadlineClock = {tick, 1};
  };
  const auto play = [&](uint32_t tick) { position(tick); t.playMidiEvents(tick, true); };
  // The production deferred owner visit; it publishes/ahead-prepares off the MIDI turn, exactly as `main.cpp` does.
  const auto owner = [&](uint32_t tick) {
    position(tick);
    (void)t.stepPlaybackViewMaintenance(tick, nullptr, true, nativePlaybackDeadlineClock);
  };
  const auto passCount = [&]() { return static_cast<uint32_t>(loop.passes.overdubPasses.size()); };
  const auto ledgerOpen = [&]() { return static_cast<unsigned>(runtime.ledger.openCount()); };

  struct ExpectedWire {
    uint32_t tick;
    uint8_t note;
    bool on;
  };
  // An independent, hand-written expectation: the wire, element-wise, never read back from the route that produced it.
  const auto checkWire = [&](const ExpectedWire* want, size_t count, const char* clause) -> const char* {
    static char message[192];
    if (MidiSendCapture::count() != count) {
      snprintf(message, sizeof(message), "%s: expected %u rows, got %u", clause, static_cast<unsigned>(count),
               static_cast<unsigned>(MidiSendCapture::count()));
      return message;
    }
    for (size_t i = 0; i < count; ++i) {
      const MidiEvent& event = MidiSendCapture::at(i);
      if (event.tick != want[i].tick || event.data.noteData.note != want[i].note ||
          event.isNoteOn() != want[i].on) {
        snprintf(message, sizeof(message), "%s: row %u expected tick %u note %u %s, got tick %u note %u %s", clause,
                 static_cast<unsigned>(i), want[i].tick, static_cast<unsigned>(want[i].note),
                 want[i].on ? "on" : "off", static_cast<unsigned>(event.tick),
                 static_cast<unsigned>(event.data.noteData.note), event.isNoteOn() ? "on" : "off");
        return message;
      }
    }
    return nullptr;
  };

  // (0) Settle the reader: one clean committed traversal of the base pass.
  MidiSendCapture::reset();
  for (uint32_t tick = 64; tick <= kLoopTicks; tick += 32) {
    play(tick);
    owner(tick + 8);
  }
  {
    const ExpectedWire base[] = {{12, 48, true}, {96, 48, false}, {800, 62, true}, {880, 62, false}};
    const char* problem = checkWire(base, 4, "settled traversal");
    TEST_ASSERT_NULL_MESSAGE(problem, problem);
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, ledgerOpen(), "the settled traversal closes its own ledger");

  // (1) Live overdub. A is entirely before the wrap; the overlay is *not* materialised into the merged store.
  const uint32_t kSessionStart = kLoopTicks + 1024;
  Track::resetPlaybackOrderHotRebuildCount();
  position(kSessionStart);
  t.startOverdubbing(kSessionStart);
  TEST_ASSERT_TRUE(t.isOverdubbing());
  t.noteOn(1, 70, 100, kLoopTicks + 1470);
  t.noteOff(1, 70, 0, kLoopTicks + 1500);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u, static_cast<uint32_t>(loop.capture.store.size()),
                                   "the live overlay holds A's own pair");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(4u, static_cast<uint32_t>(runtime.mergedMidiEvents.mergedEvents.size()),
                                   "the merged store carries the committed base only, never the live overlay");

  MidiSendCapture::reset();
  for (uint32_t tick = kLoopTicks + 1056; tick <= kLoopTicks + 1504; tick += 32) {
    const uint32_t before = passCount();
    play(tick);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before, passCount(), "no pass boundary is crossed inside the first bar");
    owner(tick + 8);
  }
  {
    const ExpectedWire baseAndA[] = {{12, 48, true}, {96, 48, false}, {800, 62, true}, {880, 62, false},
                                     {1470, 70, true}, {1500, 70, false}};
    const char* problem = checkWire(baseAndA, 6, "live A before the wrap");
    TEST_ASSERT_NULL_MESSAGE(problem, problem);
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(Track::playbackOrderHotRebuildCount()),
                                   "the growing overlay forces no content-sized build on the MIDI turn");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, ledgerOpen(),
                                   "the echo is emit-only: it writes no ledger entry (221334)");

  // (2) The wrap turn inside the live session: A's pass commits and the live store is re-based. It emits nothing, because
  // the committed interval is owned by the wrap catch-up -- this is the exactly-once seam.
  MidiSendCapture::reset();
  play(2u * kLoopTicks);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(MidiSendCapture::count()),
                                   "the wrap turn must not re-serve the interval it just committed");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, passCount(), "A's pass is committed at the wrap");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, static_cast<uint32_t>(loop.capture.store.size()),
                                   "the pass boundary re-bases the live store");
  owner(2u * kLoopTicks + 8);

  // B lands entirely after the wrap, in the new pass; capturing it must not disturb the committed reader.
  t.noteOn(1, 71, 100, 2u * kLoopTicks + 700);
  t.noteOff(1, 71, 0, 2u * kLoopTicks + 760);
  MidiSendCapture::reset();
  for (uint32_t tick = 2u * kLoopTicks + 32; tick <= 2u * kLoopTicks + 896; tick += 32) {
    play(tick);
    owner(tick + 8);
  }
  {
    const ExpectedWire baseAndB[] = {{12, 48, true}, {96, 48, false}, {700, 71, true},
                                     {760, 71, false}, {800, 62, true}, {880, 62, false}};
    const char* problem = checkWire(baseAndB, 6, "live B after the wrap");
    TEST_ASSERT_NULL_MESSAGE(problem, problem);
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u, static_cast<uint32_t>(loop.capture.store.size()),
                                   "the live tail is B's own pair");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(6u, static_cast<uint32_t>(runtime.mergedMidiEvents.mergedEvents.size()),
                                   "A entered the merged store only by being committed");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, ledgerOpen(), "the committed stream closes B's bar and A's pass");

  // (3) The pass handoff, measured inside the same traversal: A is now committed content and is served from the committed
  // reader exactly once at its own phase, while B is still live and is *not* replayed.
  MidiSendCapture::reset();
  for (uint32_t tick = 2u * kLoopTicks + 928; tick <= 2u * kLoopTicks + 1504; tick += 32) {
    play(tick);
    owner(tick + 8);
  }
  {
    const ExpectedWire committedA[] = {{1470, 70, true}, {1500, 70, false}};
    const char* problem = checkWire(committedA, 2, "A served from the committed reader");
    TEST_ASSERT_NULL_MESSAGE(problem, problem);
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, ledgerOpen(), "the committed reader's own pair closes");

  // (4) The stop commits B's pass through the real deferred owner; the handoff retires the job.
  position(2u * kLoopTicks + 1528);
  t.stopOverdubbing();
  TEST_ASSERT_TRUE_MESSAGE(t.hasPendingDeferredOverdubStop(),
                           "fixture precondition: the overdub stop is a deferred job");
  driveStopToRetirement(t);
  TEST_ASSERT_FALSE_MESSAGE(t.hasStopInFlight(), "the overdub finalization retires");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u, passCount(), "the stop committed B's pass");

  // (5) One full traversal after the handoff: the base, A and B, each exactly once, in tick order, ledger closed.
  TrackCaptureStopTestAccess::resetProjectionCycle(t);
  loop.lastTickInLoop = 0;
  loop.nextEventIndex = 0;
  MidiSendCapture::reset();
  for (uint32_t tick = 32; tick <= kLoopTicks; tick += 32) {
    play(tick);
    owner(tick + 8);
  }
  {
    const ExpectedWire handedOff[] = {{12, 48, true},   {96, 48, false},  {700, 71, true}, {760, 71, false},
                                      {800, 62, true},  {880, 62, false}, {1470, 70, true}, {1500, 70, false}};
    const char* problem = checkWire(handedOff, 8, "post-handoff traversal");
    TEST_ASSERT_NULL_MESSAGE(problem, problem);
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, ledgerOpen(),
                                   "every articulation after the handoff is closed by its own off");
}

// Task 3.15a — **the jam region is a service filter at the production edge, and the control proves the filter is live.**
//
// The audit leaves "layered/jam" UNKNOWN. The **layered** half is covered by RWO's
// `test_layered_two_bar_off_keeps_its_phase_after_reachable_action` (two enabled sounding slots, ordered exactly-once
// through `updateAllTracks`); this fixture closes the **jam** half, which no test in the tree exercised at all.
//
// Jam playback narrows the served storage region: `isStorageTickInJamRegion` drops every event outside
// `[jamStartTick, jamStartTick + jamLength)`. The oracle drives the production clock entry over two whole traversals of
// a two-bar loop whose bar one and bar two each carry one sounding, with the jam region set to **bar two only**: only the
// bar-two pair may reach the wire, once per traversal, and the ledger must close. The control clears the jam and serves
// both soundings -- so the fixture cannot pass by emitting nothing, and the suppression is attributable to the jam
// region rather than to a missing route.
void test_3_15a_jam_playback_serves_only_its_region_and_the_control_serves_both_bars() {
  constexpr uint32_t kBar = Config::TICKS_PER_BAR;
  constexpr uint32_t kLoopTicks = 2u * kBar;
  LoopEventStore::initPool();
  Track& t = trackManager.getTrack(0);
  t.ensureLoopsAllocated();
  Loop* loop = nullptr;
  stageTwoBarWorld(t, 0, loop);
  TEST_ASSERT_NOT_NULL(loop);

  const auto driveTwoTraversals = [&](uint8_t slot, Loop& driven) {
    MidiSendCapture::reset();
    t.forceSetState(TRACK_PLAYING);
    TrackCaptureStopTestAccess::unmute(t);
    TrackCaptureStopTestAccess::selectSlot(t, slot);
    TrackCaptureStopTestAccess::resetProjectionCycle(t);
    driven.lastTickInLoop = 0;
    driven.nextEventIndex = 0;
    for (uint32_t tick = 64; tick <= 2u * kLoopTicks; tick += 64) {
      t.playMidiEvents(tick, true);
    }
  };
  const auto countNotes = [&](uint8_t note, bool onsOnly) {
    uint32_t seen = 0;
    for (size_t i = 0; i < MidiSendCapture::count(); ++i) {
      const MidiEvent& event = MidiSendCapture::at(i);
      if (event.data.noteData.note != note) continue;
      if (!onsOnly || event.isNoteOn()) ++seen;
    }
    return seen;
  };
  const auto ledgerOpen = [&](uint8_t slot) {
    unsigned sounding = 0;
    auto& runtime = TrackCaptureStopTestAccess::runtimeFor(t, slot);
    runtime.ledger.forEachActive([&](uint8_t, uint8_t, const ActiveNoteLedger::Entry&) { ++sounding; });
    return sounding;
  };

  // (1) Jam over **bar two only**: bar two's sounding is served once per traversal; bar one's is filtered out.
  t.setJam(kBar, kBar);
  t.setJamPlayback(true);
  TEST_ASSERT_TRUE(t.isJamPlaybackActive());
  driveTwoTraversals(0, *loop);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u, countNotes(62, true),
                                   "the jam region's own sounding is served once per traversal");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, countNotes(48, true),
                                   "a sounding outside the jam region is filtered, not played");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, ledgerOpen(0), "the served note is closed by its own off");

  // (2) The control: the same loop, the same drive, the jam cleared. Both soundings are served -- twice each over the two
  // traversals -- so the suppression above belongs to the jam region and the fixture is not passing on silence.
  t.clearJam();
  TEST_ASSERT_FALSE(t.isJamPlaybackActive());
  driveTwoTraversals(0, *loop);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u, countNotes(62, true), "with no jam region, bar two's sounding is still served");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u, countNotes(48, true), "and bar one's is served again, once per traversal");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, ledgerOpen(0), "and the ledger still closes");
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_3_15e_slice2b_the_overlay_demand_is_the_live_generation_and_the_committed_interval);
  RUN_TEST(test_3_15e_a_live_overdub_is_served_exactly_once_through_the_wrap_and_the_pass_handoff);
  RUN_TEST(test_3_15a_jam_playback_serves_only_its_region_and_the_control_serves_both_bars);
  RUN_TEST(test_3_15a_a_committed_overdub_is_served_exactly_once_per_traversal_across_the_wrap);
  RUN_TEST(test_3_10_the_owner_rotation_carries_a_blocked_holder_finalization_and_persistence);
  RUN_TEST(test_record_normalization_orders_and_identity_completes_the_merged_store);
RUN_TEST(test_record_source_staging_merges_the_frozen_take_after_closure);
RUN_TEST(test_job_pass_staging_yields_and_preserves_successor);
  RUN_TEST(test_job_pass_staging_denial_and_receipt_binding);
  RUN_TEST(test_record_normalization_stopped_inventory);
  RUN_TEST(test_record_normalization_playing_inventory);
  RUN_TEST(test_record_normalization_disabled_minimum_inventory);
  RUN_TEST(test_record_normalization_preserves_take_larger_than_overdub_staging);
  RUN_TEST(test_record_normalization_filtered_empty_resets_geometry);
  RUN_TEST(test_record_job_closure_rejects_reused_raw_and_replaced_output);
  RUN_TEST(test_record_job_closures_exceed_overdub_hold_limit_without_staging);
  RUN_TEST(test_record_job_without_held_notes_emits_no_closures);
  RUN_TEST(test_record_publication_preflight_denies_before_the_irreversible_commit);
  RUN_TEST(test_record_pass_transfer_continues_after_the_store_empties);
  RUN_TEST(test_record_pass_transfer_capacity_refusal_fails_closed);
  RUN_TEST(test_empty_record_take_all_dropped_retires_without_content);
  RUN_TEST(test_empty_record_take_all_filtered_retires_without_content);
  RUN_TEST(test_deferred_record_interval_is_audible_only_with_a_bound_window);
  RUN_TEST(test_native_midi_send_capture_records_a_direct_send);
  RUN_TEST(test_record_stop_admits_through_the_common_drive);
  RUN_TEST(test_deferred_record_publication_queues_the_record_session_exit_snapshot);
  RUN_TEST(test_stopped_overdub_stop_publishes_one_undo_entry);
  RUN_TEST(test_frozen_record_capture_adapter_keeps_job_content_and_range_identities_distinct);
  RUN_TEST(test_record_stop_preparation_receipt_counts_multichunk_frozen_capture);
  RUN_TEST(test_deferred_record_publication_queues_revts_for_its_own_slot);
  RUN_TEST(test_playing_record_scan_drains_the_bounded_walk_and_defers_the_merge);
  RUN_TEST(test_queued_revt_scan_pins_its_chunks_across_the_deferral);
  RUN_TEST(test_deferred_record_publication_invalidates_the_track_committed_scratch);
  RUN_TEST(test_record_stop_playback_reset_preserves_playing_destination);
  RUN_TEST(test_record_bar_two_window_uses_sealed_range_before_stop_publication);
#if defined(UNIFIED_RECORD_RETURN_READER_ORACLE)
  RUN_TEST(test_record_return_bar_two_uses_unified_window_reader);
  RUN_TEST(test_record_return_keeps_frozen_reader_after_root_publishes);
  RUN_TEST(test_record_return_reader_rotates_frozen_range_across_wrap);
  RUN_TEST(test_record_return_reader_hands_frozen_current_to_root_at_wrap);
  RUN_TEST(test_record_root_request_rebases_while_capture_ready_waits);
  RUN_TEST(test_record_root_source_qualifies_privately_behind_capture_ready);
  RUN_TEST(test_record_root_source_waits_then_rechecks_after_capture_adoption);
  RUN_TEST(test_long_record_return_serves_early_frozen_bars_before_root_publication);
  RUN_TEST(test_frozen_record_completion_emits_source_qualified_receipt);
  RUN_TEST(test_non_aligned_record_return_has_bar_zero_ready_before_wrap);
  RUN_TEST(test_non_aligned_record_return_accepts_bar_zero_ready_after_wrap);
  RUN_TEST(test_non_aligned_record_return_reports_ready_skipped_by_clock_step);
#endif
  RUN_TEST(test_rootless_committed_window_waits_for_needed_snapshot_then_track_owner);
  RUN_TEST(test_rootless_source_and_window_complete_inside_own_deadline_under_owner_rotation);
  RUN_TEST(test_rootless_window_and_competing_record_stop_both_progress);
  RUN_TEST(test_transport_retarget_of_a_record_job_resolves_stopped);
  RUN_TEST(test_replaced_loop_during_a_record_finalization_retires_the_job);
  RUN_TEST(test_record_clear_record_reuses_finalization_scratch);
  RUN_TEST(test_second_record_cycle_is_admitted_with_every_guard_precondition_pinned);
  RUN_TEST(test_a_record_retirement_returns_its_extent_and_rearms_the_descriptor);
  RUN_TEST(test_slot_clear_during_a_record_finalization_does_not_republish);
  RUN_TEST(test_retained_record_stop_admits_stopped_once_the_transport_is_not_running);
  RUN_TEST(test_two_tracks_each_serve_their_own_deferred_window);
  RUN_TEST(test_snapshot_owner_prefers_the_active_loop_on_a_tie);
  RUN_TEST(test_snapshot_owner_resumes_an_in_flight_slot_behind_the_active_loop);
  RUN_TEST(test_snapshot_owner_serves_a_startable_slot_behind_an_unstartable_active_loop);
  RUN_TEST(test_snapshot_owner_serves_a_rootless_pending_slot_while_the_track_plays);
  RUN_TEST(test_deferred_record_interval_serves_beyond_bar_one_from_the_frozen_source);
  RUN_TEST(test_frozen_record_return_terminals_report_the_slots_readiness_receipt);
  RUN_TEST(test_record_return_recycles_redundant_ready_bar_before_next_bar_build);
  RUN_TEST(test_late_first_bar_append_refuses_stale_stop_view_and_recovers_from_frozen_range);
  RUN_TEST(test_missing_record_return_view_keeps_source_until_current_bar_is_adopted);
  RUN_TEST(test_published_record_presentation_prepares_frozen_return_before_root_is_current);
  RUN_TEST(test_record_bind_refusal_recovers_and_prepares_second_bar);
  RUN_TEST(test_record_bind_refusal_keeps_second_bar_audible_during_overdub);
  RUN_TEST(test_record_bind_refusal_keeps_base_during_record_publication);
  RUN_TEST(test_record_ready_before_publication_survives_owner_and_adoption);
  RUN_TEST(test_refreshed_raw_record_view_keeps_spare_role_after_bind_refusal);
  RUN_TEST(test_bound_record_revision_handoff_keeps_current_tick_off_before_on);
  RUN_TEST(test_missing_record_return_view_survives_snapshot_publication_until_handoff);
  RUN_TEST(test_record_return_lookahead_readies_every_boundary_under_owner_contention);
  RUN_TEST(test_record_return_lookahead_rejects_a_superseded_candidate_mid_overdub);
  RUN_TEST(test_record_successor_preparation_does_not_block_live_overdub);
  RUN_TEST(test_published_record_interval_does_not_replay_the_return_window);
  RUN_TEST(test_record_retirement_keeps_frozen_source_until_snapshot_is_current);
  RUN_TEST(test_stalled_frozen_window_build_does_not_park_the_record_job);
  RUN_TEST(test_private_grains_never_hold_the_global_ephemeral_seal);
  RUN_TEST(test_record_job_closures_preserve_a_live_successor);
  RUN_TEST(test_record_job_closure_denial_retains_obligation_for_retry);
  RUN_TEST(test_record_stop_closure_does_not_search_reused_producer);
  RUN_TEST(test_record_stop_commits_exact_held_note_closures);
  RUN_TEST(test_record_identity_requires_accepted_input_and_handles_preroll);
  RUN_TEST(test_record_closures_keep_identity_after_raw_source_freezes);
  RUN_TEST(test_deferred_publication_requests_own_slot_once);
  RUN_TEST(test_stopped_publication_requests_own_slot_once);
  RUN_TEST(test_superseded_publication_requests_own_slot_once);
  RUN_TEST(test_stale_publication_is_superseded_without_persistence);
  RUN_TEST(test_denied_record_preflight_preserves_content_and_interval_then_retries);
  RUN_TEST(test_replaced_loop_supersedes_accepted_stop);
  RUN_TEST(test_changed_capture_mode_supersedes_accepted_stop);
  RUN_TEST(test_record_length_uses_maximum_not_last_appended_tick);
  RUN_TEST(test_record_length_summary_handles_out_of_order_input_and_removal);
  RUN_TEST(test_actual_start_stamps_once_and_duplicate_keeps_capture);
  RUN_TEST(test_invalid_record_start_does_not_reset_overdub_or_stamp);
  RUN_TEST(test_record_to_record);
  RUN_TEST(test_record_to_overdub);
  RUN_TEST(test_overdub_to_record);
  RUN_TEST(test_overdub_to_overdub);
  return UNITY_END();
}
