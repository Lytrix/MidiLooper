//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>
#include <cstdio>
#include <random>
#include <vector>

// Production harness objects: see test/test_support/track_harness.inc
#include "../test_support/track_harness.inc"



namespace {

/// P1 control (DEC-080): deny canonical-copy admission through **real pool-credit
/// exhaustion** rather than by mocking `beginDeferredOverdubStop`, publication,
/// liveness or the load guards. Leave `keepFree` credits so the denial is exact: a
/// `ChunkReservation` cannot grow past the free count, and a failed request rolls
/// back to its prior credits.
struct PoolCreditControl {
  LoopEventStore::ChunkReservation soak;
  size_t held = 0;

  bool denyAdmission(size_t keepFree = 0) {
    const size_t free = LoopEventStore::freeChunkCount();
    if (free <= keepFree) return false;
    const size_t take = free - keepFree;
    if (!soak.reserveChunks(take)) return false;
    held += take;
    return true;
  }

  void restore() {
    soak.releaseGrain(LoopEventStoreConfig::POOL_CHUNK_COUNT);
    held = 0;
  }
};

}  // namespace

// Production lifecycle/input/undo implementations are linked above. The clock
// and physical outputs are native boundaries; off-path domain stubs fail closed.
void setUp() {
  nativeTransportRunning = true;
  g_nativeSilenceTrackCalls = 0;
  trackManager.~TrackManager();
  LoopEventStore::resetPoolForTests();
  new (&trackManager) TrackManager();
}
void tearDown() {}

Track& preparePlayingTrack() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  Loop& loop = track.getActiveLoop();
  loop.loopLengthTicks = 768;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(0, 1, 48, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(48, 1, 48, 0)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(768, CommitReason::RecordStop));
  loop.commitPendingCapturePass();
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
  TEST_ASSERT_TRUE(loop.prewarmActiveOverdubTransferHorizon());
  TEST_ASSERT_TRUE(loop.prewarmSessionExitSnapshotHandoff());
  track.prewarmPlaybackForSlot(0, true);
  track.forceSetState(TRACK_PLAYING);
  return track;
}

/// S2b.1/S2b.2: a committed slot admitted through **boot/load only** -- the runtime index, the
/// stop-descriptor ring and the canonical copy directory. No record-arm prewarm, so the slot holds
/// no canonical reservation and no transfer horizon; that is the state a loaded slot is in after
/// the boot reservation is removed, and it is what the overdub start and the stop park must supply.
Track& prepareLoadedPlayingTrack() {
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  Loop& loop = track.getActiveLoop();
  loop.loopLengthTicks = 768;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(0, 1, 48, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(48, 1, 48, 0)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(768, CommitReason::RecordStop));
  loop.commitPendingCapturePass();
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
  trackManager.prewarmPlaybackRuntime();
  track.forceSetState(TRACK_PLAYING);
  return track;
}

struct TrackCaptureStopTestAccess {
  static auto& job(Track& track) { return track.captureStopJob(); }
  static size_t queueSize(const Track& track) { return track.captureStopJobs_->count; }
  static size_t queueCapacity() { return Track::CaptureStopJobs::kCapacity; }
  static void discardColdQueue(Track& track) { track.captureStopJobs_.reset(); }
  static auto& liveNotes(Track& track) { return track.pendingNotes; }
  static auto& runtime(Track& track) { return *track.playbackRuntime.slotIfAllocated(0); }
  static bool runtimeAllocated(const Track& track, uint8_t slot) {
    return track.playbackRuntime.slotIfAllocated(slot) != nullptr;
  }
  /// Residence signal for the working-set law (DEC-076 point 1): true when this slot
  /// still holds an admitted/prewarmed *working set* -- playback views, canonical
  /// capture output or live-capture staging. An inactive slot may keep descriptors
  /// and bounded metadata; it must not keep one of these.
  static bool hasResidentWorkingSet(const Track& track, uint8_t slot) {
    const LoopPlaybackRuntime* rt = track.playbackRuntime.slotIfAllocated(slot);
    if (rt == nullptr) return false;
    return rt->viewSetIfPrewarmed() != nullptr ||
           rt->liveCaptureStagingIfPrewarmed() != nullptr ||
           rt->canonicalCaptureOutputAdmitted();
  }
  // ---- P1 read-only ownership census (DEC-080) ---------------------------------
  // Every accessor here reads existing production state; none adds a production
  // owner, a counter or an allocation on any path.
  static LoopPlaybackRuntime* runtimeIfAllocated(Track& track, uint8_t slot) {
    return track.playbackRuntime.slotIfAllocated(slot);
  }
  /// Credits this slot's canonical-copy reservation currently holds.
  static size_t canonicalReservedChunks(const Track& track, uint8_t slot) {
    const LoopPlaybackRuntime* rt = track.playbackRuntime.slotIfAllocated(slot);
    return rt != nullptr ? rt->canonicalCaptureChunks.availableChunks() : 0;
  }
  static bool canonicalOutputEmpty(const Track& track, uint8_t slot) {
    const LoopPlaybackRuntime* rt = track.playbackRuntime.slotIfAllocated(slot);
    return rt == nullptr || rt->canonicalCaptureOutput.store.empty();
  }
  static bool canonicalOutputAdmitted(const Track& track, uint8_t slot) {
    const LoopPlaybackRuntime* rt = track.playbackRuntime.slotIfAllocated(slot);
    return rt != nullptr && rt->canonicalCaptureOutputAdmitted();
  }
  /// Slot named by the live ring window's `index`-th entry (0 == head), or -1 when
  /// the entry is outside the live window.
  static int liveRingEntrySlot(const Track& track, size_t index) {
    if (track.captureStopJobs_ == nullptr || index >= track.captureStopJobs_->count) return -1;
    const size_t at = (track.captureStopJobs_->head + index) % Track::CaptureStopJobs::kCapacity;
    return static_cast<int>(track.captureStopJobs_->entries[at].slot);
  }
  static size_t liveRingEntryCount(const Track& track) {
    return track.captureStopJobs_ != nullptr ? track.captureStopJobs_->count : 0;
  }
  static uint8_t slotOfActiveLoop(const Track& track) { return track.activeLoopIndex; }
  /// B01: whether the slot's loop admitted an active-overdub transfer horizon.
  static bool transferHorizonAdmitted(const Track& track, uint8_t slot) {
    return track.loopForSlot(slot).hasActiveOverdubTransferAdmission();
  }
  static bool step(Track& track) { return track.processDeferredOverdubStopGrain(); }
  /// Latch an accepted interaction without admitting it, for eligibility assertions.
  static void latchAcceptedStop(Track& track, uint8_t slot) {
    track.latchAcceptedStop(PendingStopDestination::Playing, slot, track.captureInterval_);
  }
  static uint64_t acceptedLatchPhaseTick(const Track& track) {
    const auto* latch = track.interactionLatch();
    return latch == nullptr ? 0 : latch->phaseTick;
  }
  static uint64_t jobPhaseTick(Track& track) { return job(track).phaseTick; }
  /// S4f.1 latch identity, read through primitive accessors so the fixture never names the
  /// private nested type (the friend struct reads it; the test function cannot).
  static int acceptedLatchKind(const Track& track) {
    const auto* latch = track.interactionLatch();
    return latch == nullptr ? 0 : static_cast<int>(latch->kind);
  }
  static int acceptedLatchSlot(const Track& track) {
    const auto* latch = track.interactionLatch();
    return latch == nullptr ? -1 : static_cast<int>(latch->slot);
  }
  static uint64_t acceptedLatchInterval(const Track& track) {
    const auto* latch = track.interactionLatch();
    return latch == nullptr ? 0 : latch->interval;
  }
  /// S2b admission diagnostics: the individual preconditions `beginDeferredOverdubStopForSlot`
  /// checks, so a denied drive can be localised without the SESSION_CAPTURE diagnostic tier.
  static bool admitForSlot(Track& track, PendingStopDestination destination, uint32_t tick,
                           uint8_t slot, uint64_t interval) {
    return track.beginDeferredOverdubStopForSlot(destination, tick, slot, interval);
  }
  static bool tailPendingNotesEmpty(Track& track) {
    Track::CaptureStopJob* tail = track.captureStopJobTail();
    return tail != nullptr && tail->pendingNotes.empty();
  }
  static bool rawExtentSealable(Track& track, uint8_t slot) {
    Track::CaptureStopJob* tail = track.captureStopJobTail();
    return tail != nullptr && track.loopForSlot(slot).capture.store.canSealCaptureRange(tail->rawRange);
  }
  static size_t liveStagingEvents(Track& track, uint8_t slot) {
    auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    auto* st = rt != nullptr ? rt->liveCaptureStagingIfPrewarmed() : nullptr;
    return st != nullptr ? st->storage.eventCount : 0;
  }
  static bool stopStoppingTransitionValid(Track& track) {
    return track.isValidStateTransition(TRACK_OVERDUB_STOPPING);
  }
  /// S4.0/S2b.4 access: the per-slot liveness predicate, the reservation release and re-reserve, and a
  /// raw ring entry's slot by storage index (including entries outside the live window).
  static bool noLiveOwner(Track& track, uint8_t slot) { return track.noLiveOwnerForSlot(slot); }
  /// The production release entry point, which enforces the liveness predicate itself.
  static bool releaseCanonicalReservation(Track& track, uint8_t slot) {
    return track.tryReleaseCanonicalCaptureReservation(slot);
  }
  static bool prewarmCanonicalReservation(Track& track, uint8_t slot) {
    auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    return rt != nullptr && rt->prewarmCanonicalCaptureOutput();
  }
  /// S2b.4's mid-copy state: a canonical store that already holds an appended event, which is
  /// what `stepFrozenCaptureCanonicalCopy` leaves behind between quanta -- that grain appends
  /// into this store through this very reservation (`TrackPlaybackRuntime.cpp:1949`). Both
  /// calls are the store's own production API; no member is poked, and the event's payload is
  /// irrelevant because the release predicate asks only whether the store is empty.
  static bool appendCanonicalEvent(Track& track, uint8_t slot) {
    auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    if (rt == nullptr) return false;
    LoopEventStore& store = rt->canonicalCaptureOutput.store;
    if (!store.reservedAppendReady() && !store.reserveCaptureTail()) return false;
    MidiEvent event{};
    return store.appendReserved(event, rt->canonicalCaptureChunks);
  }
  static void clearCanonicalOutput(Track& track, uint8_t slot) {
    auto* rt = track.playbackRuntime.slotIfAllocated(slot);
    if (rt != nullptr) rt->canonicalCaptureOutput.store.clear();
  }
  static int rawRingEntrySlot(const Track& track, size_t index) {
    if (track.captureStopJobs_ == nullptr || index >= Track::CaptureStopJobs::kCapacity) return -1;
    return static_cast<int>(track.captureStopJobs_->entries[index].slot);
  }
  /// The real S4f.2 dispatch decision (the same entry `processDeferredIdleMaintenance` calls).
  static bool serviceAcceptedInteraction(Track& track) {
    return track.serviceAcceptedInteractionTurn();
  }
  static void selectSlot(Track& track, uint8_t slot) { track.activeLoopIndex = slot; }
  static void advanceInterval(Track& track) { ++track.captureInterval_; }
  static uint64_t interval(const Track& track) { return track.captureInterval_; }
  static uint64_t jobInterval(Track& track) { return job(track).interval; }
  static size_t jobReceiptRing(Track& track) { return job(track).admission.sessionExitReceiptRing; }
  static const SessionExitSnapshotHandoff* receiptAt(Track& track, size_t offset) {
    return track.getActiveLoop().sessionExitReceiptAt(offset);
  }
  static size_t receiptCount(Track& track) {
    return track.getActiveLoop().sessionExitReceiptCount();
  }
  static uint8_t jobPhase(Track& track) {
    return static_cast<uint8_t>(job(track).admission.phase);
  }
  /// Identity of the interval's staged capture pass (`kInvalidPassId` when none is staged).
  /// OpenSpec 5.12 / D8 asserts on this instead of on row counts, so accidental shared-container
  /// reuse is caught even when the rows happen to come out right. The receipt is the *job-owned*
  /// pass while a deferred job holds one (D8 moved it out of the shared Loop slot); otherwise the
  /// live producer slot.
  static PassId stagedPassId(Track& track) {
    const auto* job = track.captureStopJobHead();
    if (job != nullptr && job->ownsPendingPass) return job->pendingPass.id;
    const Loop& loop = track.getActiveLoop();
    return loop.hasPendingCapturePass() ? loop.pendingCapturePass().id : kInvalidPassId;
  }
  static bool hasStagedPass(Track& track) {
    const auto* job = track.captureStopJobHead();
    if (job != nullptr && job->ownsPendingPass) return true;
    return track.getActiveLoop().hasPendingCapturePass();
  }
  /// The head job's owned staged pass, or nullptr when it owns none (OpenSpec 5.12 / D8).
  static auto* jobPass(Track& track) {
    auto* job = track.captureStopJobHead();
    return (job != nullptr && job->ownsPendingPass) ? &job->pendingPass : nullptr;
  }
  static auto* loopPass(Track& track) {
    Loop& loop = track.getActiveLoop();
    return loop.hasPendingCapturePass() ? &loop.pendingCapturePass_ : nullptr;
  }
  static auto generation(const Track& track) { return track.playbackGeneration; }
  static size_t liveReservations(Track& track) {
    auto* staging = runtime(track).liveCaptureStagingIfPrewarmed();
    return staging != nullptr ? staging->openReservationCount() : 0;
  }
  static void invalidate(Track& track) { track.invalidatePlaybackMergedMidiEvents(true); }
  static const void* headPtr(Track& track) { return track.captureStopJobHead(); }
  static const void* tailPtr(Track& track) { return track.captureStopJobTail(); }
  static const void* entryPtr(Track& track, size_t index) {
    return &track.captureStopJobs_->entries[index];
  }
  static size_t headIndex(const Track& track) { return track.captureStopJobs_->head; }
  static void setQueueCount(Track& track, size_t count) { track.captureStopJobs_->count = count; }
  static bool admit(Track& track, PendingStopDestination destination, uint32_t tick) {
    return track.beginDeferredOverdubStop(destination, tick);
  }
  static auto& rawRange(Track& track) { return track.captureStopJob().rawRange; }
  /// Published playback root identity, for the 5.3 stale-window assertion.
  static const void* publishedRootIdentity(Track& track) {
    return track.getActiveLoop().publishedSnapshotIdentity();
  }
  static uint32_t publishedRootRevision(const Track& track) {
    return track.getActiveLoop().publishedSnapshotRevision();
  }
  static uint32_t ownRevision(const Track& track) { return track.getActiveLoop().playbackRevision; }
  static bool ownsRawRange(Track& track) { return track.captureStopJob().ownsRawRange; }
  static TrackUndoPublication* undoPublication(Track& track) { return track.protectedUndo(); }
  static GlobalUndoStack& undoStack(Track& track) { return track.getGlobalUndoStack(); }
  static bool appendNoteOffAtPhase(Track& track, uint8_t channel, uint8_t note, uint32_t phase) {
    return track.appendCaptureNoteOffAtPhase(channel, note, phase);
  }
  static bool jobHasBeforePublished(Track& track) {
    return track.captureStopJob().hasBeforePublished;
  }
  static bool liveHasBeforePublished(const Track& track) {
    return track.hasOverdubSessionBeforePublished_;
  }
  static void setLiveBeforePublished(Track& track, bool value) {
    track.hasOverdubSessionBeforePublished_ = value;
  }
  static const PlaybackViewSourceIdentity& causalBase(Track& track) {
    return track.captureStopJob().causalBase;
  }
  static bool causalBaseRecorded(Track& track) {
    return track.captureStopJob().causalBaseRecorded;
  }
  static bool entryCausalBaseRecorded(Track& track, size_t index) {
    return track.captureStopJobs_->entries[index].causalBaseRecorded;
  }
  static const void* causalBaseSnapshotIdentity(Track& track) {
    return track.captureStopJob().causalBase.snapshotIdentity;
  }
  static void* pinnedCausalSnapshot(Track& track) {
    return const_cast<void*>(static_cast<const void*>(track.captureStopJob().causalBaseSnapshot.get()));
  }
  static uint8_t bridgeSelected(Track& track) {
    return runtime(track).viewSetIfPrewarmed()->bridgeLifecycle.selected;
  }
  static uint32_t playbackRevision(const Track& track) {
    return track.getActiveLoop().playbackRevision;
  }
  static void setPlaybackRevision(Track& track, uint32_t revision) {
    track.getActiveLoop().playbackRevision = revision;
  }
  static bool canonicalOrderComplete(Track& track) {
    return runtime(track).frozenCaptureCanonicalOrder.complete();
  }
  static bool frozenOrderComplete(Track& track) { return runtime(track).frozenCaptureOrder.complete(); }
  static bool wrapCommitPending(Track& track) { return track.overdubWrapCommitPending_; }
  static uint8_t jobDestination(Track& track) {
    return static_cast<uint8_t>(job(track).admission.destination);
  }
  static void requestStoppedStop(Track& track) { track.requestDeferredOverdubStopToStopped(); }
  static bool captureStopOwnerClear(Track& track) { return runtime(track).captureStopOwner == nullptr; }
  static bool canonicalHasFrozenBinding(Track& track) {
    return runtime(track).canonicalPassInputs.hasFrozenBinding();
  }
  static uint64_t frozenOrderStorageGeneration(Track& track) {
    return runtime(track).frozenCaptureOrderStorageGeneration;
  }
  static bool canonicalOutputAdmitted(Track& track) {
    return runtime(track).canonicalCaptureOutputAdmitted();
  }
  static size_t canonicalChunks(Track& track) {
    return runtime(track).canonicalCaptureChunks.availableChunks();
  }
  /// Emulate the predecessor's transfer spending the slot's canonical copy credits.
  static void drainCanonicalChunks(Track& track) {
    auto& reservation = runtime(track).canonicalCaptureChunks;
    (void)reservation.releaseGrain(reservation.availableChunks());
  }
  static bool restoreCanonicalChunks(Track& track, size_t chunks) {
    return runtime(track).canonicalCaptureChunks.reserveChunks(chunks);
  }
};

/// Read a job-owned sealed extent's rows in stored order (bounded by its exact
/// captured end, not a live producer extent).
SessionMidiEventVec sealedRangeRows(const LoopEventStore::CaptureRange& range) {
  SessionMidiEventVec rows;
  const uint64_t generation = range.generation();
  for (size_t chunkIndex = 0; chunkIndex < range.chunkCount(); ++chunkIndex) {
    for (uint16_t eventIndex = 0;; ++eventIndex) {
      MidiEvent event;
      bool atEnd = false;
      if (!range.read(generation, chunkIndex, eventIndex, event, atEnd)) break;
      if (atEnd) break;
      rows.push_back(event);
    }
  }
  return rows;
}

void inputNote(uint8_t pitch, uint32_t onTick, uint32_t offTick) {
  clockManager.assignCurrentTickSilently(onTick);
  midiHandler.handleMidiMessage(midi::NoteOn, 1, pitch, 100, SOURCE_USB);
  if (offTick != UINT32_MAX) {
    clockManager.assignCurrentTickSilently(offTick);
    midiHandler.handleMidiMessage(midi::NoteOff, 1, pitch, 0, SOURCE_USB);
  }
}

void test_first_interval_uses_production_routes_and_freezes_at_punch_out() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.isOverdubbing());
  inputNote(60, 97, 120);
  const auto& capture = track.getActiveLoop().capture.store;
  TEST_ASSERT_EQUAL(2, capture.size());
  TEST_ASSERT_EQUAL(97, capture.at(0).tick);
  TEST_ASSERT_EQUAL(120, capture.at(1).tick);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_TRUE(track.isPlaying());
}

/// Button A successor reentry under the directional capacity. Starting a successor
/// while the predecessor finalizes is admitted (live role free). Stopping that
/// successor is denied while the predecessor owns the sole frozen/deferred role, and
/// the denial must be non-destructive: the successor stays the live recording
/// interval with no stop descriptor, and its rows, notes, reservations and preview
/// are untouched. Once the predecessor retires, the same interval's subsequent stop
/// is admitted and only then may a further interval start.
///
/// The N-job deferred queueing scenario these guards previously shaded (two frozen
/// jobs plus a live third interval) is a documented follow-up capacity case; see
/// openspec/changes/successor-capture-reentry-unguarding/design.md (D2).

/// A successor-stop admission failure because the deferred capacity is occupied
/// leaves the live interval completely intact and still recording. Once capacity
/// becomes available, a subsequent stop request transfers that same interval
/// exactly once, and the finalized output equals the accepted input exactly.
void test_successor_stop_denied_by_occupied_deferred_capacity_is_non_destructive() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  // A deferred (owns the frozen role).
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  // B live (Track-level admission) with exact input.
  clockManager.assignCurrentTickSilently(192);
  track.startOverdubbing(192);
  TEST_ASSERT_TRUE(track.isOverdubbing());
  inputNote(62, 193, 216);
  const size_t rowsBefore = loop.capture.store.size();
  const size_t notesBefore = TrackCaptureStopTestAccess::liveNotes(track).size();
  const size_t queueBefore = TrackCaptureStopTestAccess::queueSize(track);
  TEST_ASSERT_EQUAL(2, rowsBefore);
  // Denied stop: no descriptor, no consumed rows/notes, still recording.
  clockManager.assignCurrentTickSilently(240);
  track.stopOverdubbing();
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(),
      "an occupied deferred role must deny the stop without ending the live interval");
  TEST_ASSERT_EQUAL(queueBefore, TrackCaptureStopTestAccess::queueSize(track));
  TEST_ASSERT_EQUAL(rowsBefore, loop.capture.store.size());
  TEST_ASSERT_EQUAL(notesBefore, TrackCaptureStopTestAccess::liveNotes(track).size());
  TEST_ASSERT_EQUAL(193, loop.capture.store.at(0).tick);
  TEST_ASSERT_EQUAL(216, loop.capture.store.at(1).tick);
  // A retires. Its cleanup is job-local: B stays live and intact.
  for (unsigned i = 0; i < 10000 && TrackCaptureStopTestAccess::queueSize(track) > 0; ++i)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL(0, TrackCaptureStopTestAccess::queueSize(track));
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(),
      "the predecessor's completion must not end the live successor");
  TEST_ASSERT_EQUAL(rowsBefore, loop.capture.store.size());
  TEST_ASSERT_EQUAL(notesBefore, TrackCaptureStopTestAccess::liveNotes(track).size());
  // The same interval's subsequent stop is admitted and transfers exactly once.
  inputNote(63, 241, 264);
  clockManager.assignCurrentTickSilently(288);
  track.stopOverdubbing();
  TEST_ASSERT_FALSE(track.isOverdubbing());
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_EQUAL(1, TrackCaptureStopTestAccess::queueSize(track));
  const SessionMidiEventVec finalized = sealedRangeRows(TrackCaptureStopTestAccess::rawRange(track));
  TEST_ASSERT_EQUAL(4, finalized.size());
  TEST_ASSERT_EQUAL(193, finalized[0].tick);
  TEST_ASSERT_EQUAL(216, finalized[1].tick);
  TEST_ASSERT_EQUAL(241, finalized[2].tick);
  TEST_ASSERT_EQUAL(264, finalized[3].tick);
}

void test_button_a_successor_stop_denied_until_deferred_role_available() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  // A: record at 96, stop at 144 -> A owns the frozen/deferred role.
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.isOverdubbing());
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_EQUAL(1, TrackCaptureStopTestAccess::queueSize(track));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::ownsRawRange(track));
  const SessionMidiEventVec aRows = sealedRangeRows(TrackCaptureStopTestAccess::rawRange(track));
  TEST_ASSERT_EQUAL(2, aRows.size());
  // B: start at 192 while A pends -> admitted into the live role, exact input.
  clockManager.assignCurrentTickSilently(192);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(),
      "B must start while A's deferred finalization is pending");
  inputNote(62, 193, 216);
  const size_t bRowsBeforeStop = loop.capture.store.size();
  const size_t bNotesBeforeStop = TrackCaptureStopTestAccess::liveNotes(track).size();
  TEST_ASSERT_EQUAL(2, bRowsBeforeStop);
  TEST_ASSERT_EQUAL(193, loop.capture.store.at(0).tick);
  TEST_ASSERT_EQUAL(216, loop.capture.store.at(1).tick);
  // B stop at 240 while A owns the frozen role: explicit, non-destructive denial.
  clockManager.assignCurrentTickSilently(240);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(),
      "a denied successor stop must leave the live interval recording");
  TEST_ASSERT_EQUAL(1, TrackCaptureStopTestAccess::queueSize(track));
  TEST_ASSERT_EQUAL(bRowsBeforeStop, loop.capture.store.size());
  TEST_ASSERT_EQUAL(bNotesBeforeStop, TrackCaptureStopTestAccess::liveNotes(track).size());
  TEST_ASSERT_EQUAL(193, loop.capture.store.at(0).tick);
  // A retires -> the frozen role frees. A completes while B is live, so its cleanup
  // must be job-local: B's state, rows and notes stay its own.
  for (unsigned i = 0; i < 10000 && TrackCaptureStopTestAccess::queueSize(track) > 0; ++i)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL(0, TrackCaptureStopTestAccess::queueSize(track));
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(),
      "A's completion must not end B's live interval");
  TEST_ASSERT_EQUAL(bRowsBeforeStop, loop.capture.store.size());
  TEST_ASSERT_EQUAL(bNotesBeforeStop, TrackCaptureStopTestAccess::liveNotes(track).size());
  // B's subsequent stop request is admitted: the same interval transfers once.
  inputNote(63, 241, 264);
  clockManager.assignCurrentTickSilently(288);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_FALSE(track.isOverdubbing());
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_EQUAL(1, TrackCaptureStopTestAccess::queueSize(track));
  // The admitted transfer moved B's accepted interval into the job's sealed range, so the
  // live store is empty and the finalized output is read from the job - asserting the
  // finalized B output equals the accepted B input, exactly once.
  TEST_ASSERT_EQUAL(0, loop.capture.store.size());
  const SessionMidiEventVec bSealed = sealedRangeRows(TrackCaptureStopTestAccess::rawRange(track));
  TEST_ASSERT_EQUAL(4, bSealed.size());
  TEST_ASSERT_EQUAL(193, bSealed.at(0).tick);
  TEST_ASSERT_EQUAL(216, bSealed.at(1).tick);
  TEST_ASSERT_EQUAL(241, bSealed.at(2).tick);
  TEST_ASSERT_EQUAL(264, bSealed.at(3).tick);
  // C start is legal now that the live role is free.
  clockManager.assignCurrentTickSilently(336);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(),
      "C must start once B has transferred into the deferred role");
  inputNote(64, 337, 360);
  TEST_ASSERT_EQUAL(2, loop.capture.store.size());
  TEST_ASSERT_EQUAL(337, loop.capture.store.at(0).tick);
}


// Record admission is atomic: a failed *required* prewarm refuses the take instead of
// being ignored and letting RECORDING start with an unestablished stop prerequisite.
// The fixture prewarms slot 0, so this uses a fresh slot where the companion-history
// horizon genuinely has to be reserved.
void test_record_admission_denied_when_required_prewarm_fails() {
  Track& track = preparePlayingTrack();
  TrackCaptureStopTestAccess::selectSlot(track, 1);
  Loop& loop = track.getActiveLoop();
  const size_t stopQueueBefore = TrackCaptureStopTestAccess::queueSize(track);

  MemoryMonitor::setNativeForcedReserveDenied(true);
  track.forceSetState(TRACK_ARMED);
  track.startRecording(0);
  MemoryMonitor::setNativeForcedReserveDenied(false);

  // Denied: no RECORDING transition, no capture interval admitted, no pending stop
  // ownership, no pre-roll admitted, and the armed state is what it was.
  TEST_ASSERT_EQUAL(static_cast<int>(TRACK_ARMED), static_cast<int>(track.getState()));
  TEST_ASSERT_FALSE(loop.captureActive());
  TEST_ASSERT_EQUAL(stopQueueBefore, TrackCaptureStopTestAccess::queueSize(track));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::liveNotes(track).empty());
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(loop.passes.overdubPasses.size()));

  // Control: with the reserve available the same call is admitted, so the assertions
  // above are about admission and not about the state machine refusing everything.
  track.forceSetState(TRACK_ARMED);
  track.startRecording(0);
  TEST_ASSERT_EQUAL(static_cast<int>(TRACK_RECORDING), static_cast<int>(track.getState()));
  TEST_ASSERT_TRUE(loop.captureActive());
}

/// Task 4.2: the three admission routes (direct `Track::startOverdubbing`, the manager's
/// `TrackManager::startOverdubbingTrack`, and Button A's `MidiButtonActions::handleToggleRecord`)
/// must agree. Each route is driven against the same fixture - A deferred and owning the
/// frozen/deferred role, B started into the live role - and must produce the same observable
/// outcome for the admit and for the capacity denial. `Track::stopOverdubbing` is the stop edge
/// for all three routes, so the stop half asserts the same denial through each route's entry.
enum class ReentryRoute : uint8_t { TrackDirect, Manager, ButtonA };

const char* reentryRouteName(ReentryRoute route) {
  switch (route) {
    case ReentryRoute::TrackDirect: return "Track::startOverdubbing";
    case ReentryRoute::Manager: return "TrackManager::startOverdubbingTrack";
    case ReentryRoute::ButtonA: return "MidiButtonActions::handleToggleRecord";
  }
  return "unknown";
}

void startSuccessorThrough(ReentryRoute route, Track& track, uint32_t tick) {
  clockManager.assignCurrentTickSilently(tick);
  switch (route) {
    case ReentryRoute::TrackDirect: track.startOverdubbing(tick); return;
    case ReentryRoute::Manager: trackManager.startOverdubbingTrack(trackManager.getSelectedTrackIndex()); return;
    case ReentryRoute::ButtonA: midiButtonActions.handleToggleRecord(); return;
  }
}

void stopSuccessorThrough(ReentryRoute route, Track& track, uint32_t tick) {
  clockManager.assignCurrentTickSilently(tick);
  switch (route) {
    case ReentryRoute::TrackDirect: track.stopOverdubbing(); return;
    case ReentryRoute::Manager:
    case ReentryRoute::ButtonA: midiButtonActions.handleToggleRecord(); return;
  }
}

void assertRouteAgreesOnSuccessorAdmitAndDenial(ReentryRoute route) {
  const char* const routeName = reentryRouteName(route);
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  // A: a normal route start/stop that defers, so A owns the frozen/deferred role.
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_EQUAL(1, TrackCaptureStopTestAccess::queueSize(track));
  // Admit: every route must open B's live interval while A pends.
  startSuccessorThrough(route, track, 192);
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(), routeName);
  inputNote(62, 193, 216);
  const size_t rowsAfterAdmit = loop.capture.store.size();
  const size_t notesAfterAdmit = TrackCaptureStopTestAccess::liveNotes(track).size();
  const size_t queueAfterAdmit = TrackCaptureStopTestAccess::queueSize(track);
  TEST_ASSERT_EQUAL_MESSAGE(2, rowsAfterAdmit, routeName);
  TEST_ASSERT_EQUAL_MESSAGE(193, loop.capture.store.at(0).tick, routeName);
  TEST_ASSERT_EQUAL_MESSAGE(216, loop.capture.store.at(1).tick, routeName);
  // Deny: a stop that exceeds the declared capacity is explicit and non-destructive.
  stopSuccessorThrough(route, track, 240);
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(),
      "an occupied deferred role must deny the stop through every route");
  TEST_ASSERT_EQUAL_MESSAGE(queueAfterAdmit, TrackCaptureStopTestAccess::queueSize(track), routeName);
  TEST_ASSERT_EQUAL_MESSAGE(rowsAfterAdmit, loop.capture.store.size(), routeName);
  TEST_ASSERT_EQUAL_MESSAGE(notesAfterAdmit, TrackCaptureStopTestAccess::liveNotes(track).size(), routeName);
  TEST_ASSERT_EQUAL_MESSAGE(193, loop.capture.store.at(0).tick, routeName);
  TEST_ASSERT_EQUAL_MESSAGE(216, loop.capture.store.at(1).tick, routeName);
  // All three routes leave the identical live interval, so they agree bit for bit: the
  // same two accepted rows, no lingering pending note, no second descriptor.
  TEST_ASSERT_EQUAL_MESSAGE(2, rowsAfterAdmit, routeName);
  TEST_ASSERT_EQUAL_MESSAGE(0, notesAfterAdmit, routeName);
  TEST_ASSERT_EQUAL_MESSAGE(1, queueAfterAdmit, routeName);
  // Leave the fixture clean for the next route: the denial above deliberately keeps this
  // job parked, so retire it (and the interval) before the next route reuses the track.
  stopSuccessorThrough(route, track, 288);
  for (unsigned turn = 0; turn < 20000 && TrackCaptureStopTestAccess::queueSize(track) > 0; ++turn)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, static_cast<uint32_t>(TrackCaptureStopTestAccess::queueSize(track)),
                                   routeName);
  TEST_ASSERT_FALSE_MESSAGE(track.hasPendingDeferredOverdubStop(), routeName);
}

void test_reentry_routes_agree_on_admission_and_capacity_denial() {
  assertRouteAgreesOnSuccessorAdmitAndDenial(ReentryRoute::Manager);
  assertRouteAgreesOnSuccessorAdmitAndDenial(ReentryRoute::ButtonA);
  assertRouteAgreesOnSuccessorAdmitAndDenial(ReentryRoute::TrackDirect);
}

void test_manager_reentry_with_a_held_same_pitch_note() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, UINT32_MAX);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  clockManager.assignCurrentTickSilently(192);
  trackManager.startOverdubbingTrack(trackManager.getSelectedTrackIndex());
  inputNote(60, 193, 216);
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(),
      "manager must admit B without consuming A's same-pitch closure");
  const auto& capture = track.getActiveLoop().capture.store;
  TEST_ASSERT_EQUAL(2, capture.size());
  TEST_ASSERT_EQUAL(193, capture.at(0).tick);
  TEST_ASSERT_EQUAL(216, capture.at(1).tick);
}

void test_track_reentry_does_not_depend_on_manager_guard() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  clockManager.assignCurrentTickSilently(192);
  track.startOverdubbing(192);
  inputNote(62, 193, 216);
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(),
      "Track must own a distinct live interval before removing outer guards");
  // Completion-during-capture (task 5.2): let A's deferred job retire while B is live and
  // assert B's interval is untouched - D6 makes A's cleanup job-local because A's interval
  // identity no longer matches the live one.
  const uint8_t stateBeforeCompletion = track.getState();
  const size_t rowsBeforeCompletion = track.getActiveLoop().capture.store.size();
  for (unsigned turn = 0; turn < 10000 && TrackCaptureStopTestAccess::queueSize(track) > 0; ++turn)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(TrackCaptureStopTestAccess::queueSize(track)));
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(),
      "A's completion must not transition B's live interval");
  TEST_ASSERT_EQUAL(stateBeforeCompletion, track.getState());
  TEST_ASSERT_EQUAL(rowsBeforeCompletion, track.getActiveLoop().capture.store.size());
}

/// DEC-068 (1)(5): a session-exit receipt belongs to the interval that captured it. A's
/// receipt is taken at A's stop admission (not ready), carries A's committed pass id once A
/// retires, and B's receipt is a distinct one for a distinct interval.
void test_session_exit_receipts_are_per_interval() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  // A's tail is captured at admission, before any successor exists, and is not consumable yet.
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(TrackCaptureStopTestAccess::receiptCount(track)));
  const size_t aRing = TrackCaptureStopTestAccess::jobReceiptRing(track);
  TEST_ASSERT_NOT_EQUAL(Loop::kInvalidSessionExitReceipt, aRing);
  const SessionExitSnapshotHandoff* aReceipt = TrackCaptureStopTestAccess::receiptAt(track, 0);
  TEST_ASSERT_NOT_NULL(aReceipt);
  TEST_ASSERT_TRUE(aReceipt->pending);
  TEST_ASSERT_FALSE(aReceipt->ready);
  TEST_ASSERT_EQUAL(kInvalidPassId, aReceipt->lastPassId);
  // B starts while A pends and gets its own session.
  clockManager.assignCurrentTickSilently(192);
  track.startOverdubbing(192);
  TEST_ASSERT_TRUE(track.isOverdubbing());
  TEST_ASSERT_TRUE(loop.hasOverdubSession());
  inputNote(62, 193, 216);
  // A retires: its receipt becomes ready with A's pass id, and B's session survives.
  for (unsigned turn = 0; turn < 10000 && TrackCaptureStopTestAccess::queueSize(track) > 0; ++turn)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  const PassId aPassId = loop.passes.overdubPasses.back().id;
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(TrackCaptureStopTestAccess::receiptCount(track)));
  aReceipt = TrackCaptureStopTestAccess::receiptAt(track, 0);
  TEST_ASSERT_NOT_NULL(aReceipt);
  TEST_ASSERT_TRUE(aReceipt->ready);
  TEST_ASSERT_EQUAL(aPassId, aReceipt->lastPassId);
  // The publisher consumes it once; B's own receipt is a different slot and a different pass.
  for (unsigned grain = 0; grain < 10000 && loop.hasPendingSnapshotPublish(); ++grain)
    loop.stepPendingSnapshotPublish(1);
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(TrackCaptureStopTestAccess::receiptCount(track)));
  const size_t passesBeforeB = loop.passes.overdubPasses.size();
  clockManager.assignCurrentTickSilently(288);
  track.stopOverdubbing();
  TEST_ASSERT_FALSE(track.isOverdubbing());
  const size_t bRing = TrackCaptureStopTestAccess::jobReceiptRing(track);
  TEST_ASSERT_NOT_EQUAL(Loop::kInvalidSessionExitReceipt, bRing);
  TEST_ASSERT_NOT_EQUAL(aRing, bRing);
  for (unsigned turn = 0; turn < 20000 && TrackCaptureStopTestAccess::queueSize(track) > 0; ++turn)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(TrackCaptureStopTestAccess::queueSize(track)));
  TEST_ASSERT_EQUAL_UINT32(passesBeforeB + 1,
                           static_cast<uint32_t>(loop.passes.overdubPasses.size()));
  const PassId bPassId = loop.passes.overdubPasses.back().id;
  TEST_ASSERT_TRUE(bPassId != aPassId);
  const SessionExitSnapshotHandoff* bReceipt = TrackCaptureStopTestAccess::receiptAt(track, 0);
  TEST_ASSERT_NOT_NULL(bReceipt);
  TEST_ASSERT_TRUE(bReceipt->ready);
  TEST_ASSERT_EQUAL(bPassId, bReceipt->lastPassId);
}

/// DEC-068 (2)(3): A's cleanup retires A's descriptor while B stays live, and it must not close
/// the successor's session. B's own session is still the live one after A is gone.
void test_predecessor_cleanup_keeps_the_successor_session() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  clockManager.assignCurrentTickSilently(192);
  track.startOverdubbing(192);
  inputNote(62, 193, 240);
  const size_t rowsBefore = loop.capture.store.size();
  const size_t notesBefore = TrackCaptureStopTestAccess::liveNotes(track).size();
  for (unsigned turn = 0; turn < 10000 && TrackCaptureStopTestAccess::queueSize(track) > 0; ++turn)
    (void)TrackCaptureStopTestAccess::step(track);
  // A retired exactly once...
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(TrackCaptureStopTestAccess::queueSize(track)));
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  // ...without ending B's session or touching B's accepted state.
  TEST_ASSERT_TRUE_MESSAGE(loop.hasOverdubSession(),
                           "A's cleanup closed the session of the live successor");
  TEST_ASSERT_TRUE(track.isOverdubbing());
  TEST_ASSERT_EQUAL(rowsBefore, loop.capture.store.size());
  TEST_ASSERT_EQUAL(notesBefore, TrackCaptureStopTestAccess::liveNotes(track).size());
}

/// DEC-068 (4): after B stops, B captures its own receipt, publishes it, and its descriptor
/// retires — the successor's session ends with the successor.
void test_successor_session_ends_with_its_own_retirement() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  clockManager.assignCurrentTickSilently(192);
  track.startOverdubbing(192);
  inputNote(62, 193, 216);
  for (unsigned turn = 0; turn < 10000 && TrackCaptureStopTestAccess::queueSize(track) > 0; ++turn)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_TRUE(loop.hasOverdubSession());
  // The publisher drains A's receipt on idle turns (the harness drives them explicitly).
  for (unsigned grain = 0; grain < 10000 && loop.hasPendingSnapshotPublish(); ++grain)
    loop.stepPendingSnapshotPublish(1);
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(TrackCaptureStopTestAccess::receiptCount(track)));
  clockManager.assignCurrentTickSilently(288);
  track.stopOverdubbing();
  TEST_ASSERT_FALSE(track.isOverdubbing());
  for (unsigned turn = 0; turn < 20000 && TrackCaptureStopTestAccess::queueSize(track) > 0; ++turn)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(TrackCaptureStopTestAccess::queueSize(track)));
  // B owns the last interval, so its retirement ends the session, and its receipt is queued for
  // the publisher rather than dropped.
  TEST_ASSERT_FALSE(loop.hasOverdubSession());
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(TrackCaptureStopTestAccess::receiptCount(track)));
  const SessionExitSnapshotHandoff* receipt = TrackCaptureStopTestAccess::receiptAt(track, 0);
  TEST_ASSERT_NOT_NULL(receipt);
  TEST_ASSERT_TRUE(receipt->ready);
  TEST_ASSERT_EQUAL(loop.passes.overdubPasses.back().id, receipt->lastPassId);
}

/// DEC-068 (6): the same rule on the superseded route — A retiring through
/// `commitSupersededCausalBase` releases only its own receipt and cannot close B's session.
void test_superseded_predecessor_releases_only_its_own_receipt() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  loop.storePublishedSnapshotForTest(
      LoopSnapshot::createEmpty(loop.loopId, loop.loopLengthTicks, loop.loopStartTick,
                                loop.nextNoteId_, 1),
      loop.playbackRevision);
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, UINT32_MAX);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  // The recorded causal base stops being the live root, so A retires superseded.
  TrackCaptureStopTestAccess::setPlaybackRevision(track, TrackCaptureStopTestAccess::playbackRevision(track) + 7);
  clockManager.assignCurrentTickSilently(192);
  track.startOverdubbing(192);
  inputNote(62, 193, 216);
  const size_t passesBefore = loop.passes.overdubPasses.size();
  for (unsigned turn = 0; turn < 20000 && TrackCaptureStopTestAccess::queueSize(track) > 0; ++turn)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(TrackCaptureStopTestAccess::queueSize(track)));
  TEST_ASSERT_EQUAL_UINT8(RuntimePlaybackBridgeLifecycle::kNoBridge,
                          TrackCaptureStopTestAccess::bridgeSelected(track));
  TEST_ASSERT_EQUAL_UINT32(passesBefore + 1,
                           static_cast<uint32_t>(loop.passes.overdubPasses.size()));
  // A released its own receipt (consumed or still pending, but always with A's pass id)...
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(TrackCaptureStopTestAccess::receiptCount(track)));
  const SessionExitSnapshotHandoff* receipt = TrackCaptureStopTestAccess::receiptAt(track, 0);
  TEST_ASSERT_NOT_NULL(receipt);
  TEST_ASSERT_TRUE(receipt->ready);
  TEST_ASSERT_EQUAL(loop.passes.overdubPasses.back().id, receipt->lastPassId);
  // ...and B's session is untouched by A's superseded retirement.
  TEST_ASSERT_TRUE(loop.hasOverdubSession());
  TEST_ASSERT_TRUE(track.isOverdubbing());
}

/// Task 5.1(d): after A retires, B's own stop is admitted and that same interval transfers
/// exactly once — one job descriptor for B's interval identity, B's accepted rows moved out of
/// the live store in order, and exactly one committed pass whose content is those rows.
///
/// Scope note (open finding, CURRENT_WORK): the assertions stop at the committed pass, because
/// a retiring predecessor's cleanup closes the *live successor's* overdub session
/// (`Loop::closeOverdubSession`), so B's own `DeferredCleanup` can never hand off its session
/// exit and B's descriptor leaks. That is a session-ownership defect under reentry, documented
/// separately; the transfer and the committed content below are independent of it.
void test_successor_same_interval_transfers_exactly_once() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  // A: deferred stop owning the finalization resources.
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  // B: live interval with distinctive input while A pends.
  clockManager.assignCurrentTickSilently(192);
  track.startOverdubbing(192);
  TEST_ASSERT_TRUE(track.isOverdubbing());
  inputNote(62, 193, 216);
  inputNote(64, 217, 240);
  const uint64_t bInterval = TrackCaptureStopTestAccess::interval(track);
  TEST_ASSERT_EQUAL_UINT64(2u, bInterval);
  MidiEventVec bAccepted;
  for (size_t row = 0; row < loop.capture.store.size(); ++row) {
    bAccepted.push_back(loop.capture.store.at(row));
  }
  TEST_ASSERT_EQUAL_UINT32(4, static_cast<uint32_t>(bAccepted.size()));
  // B's stop while A owns those resources: refused, nothing consumed, same interval.
  clockManager.assignCurrentTickSilently(240);
  track.stopOverdubbing();
  TEST_ASSERT_TRUE(track.isOverdubbing());
  TEST_ASSERT_EQUAL_UINT64(bInterval, TrackCaptureStopTestAccess::interval(track));
  TEST_ASSERT_EQUAL_UINT32(4, static_cast<uint32_t>(loop.capture.store.size()));
  // A retires.
  for (unsigned turn = 0; turn < 10000 && TrackCaptureStopTestAccess::queueSize(track) > 0; ++turn)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  const size_t passesBeforeB = loop.passes.overdubPasses.size();
  // The same interval's stop is admitted, once, and it belongs to B's interval identity.
  clockManager.assignCurrentTickSilently(288);
  track.stopOverdubbing();
  TEST_ASSERT_FALSE(track.isOverdubbing());
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(TrackCaptureStopTestAccess::queueSize(track)));
  TEST_ASSERT_EQUAL_UINT64(bInterval, TrackCaptureStopTestAccess::jobInterval(track));
  // The transfer moved B's accepted rows out of the live store, in order.
  const SessionMidiEventVec sealed = sealedRangeRows(TrackCaptureStopTestAccess::rawRange(track));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(bAccepted.size()),
                           static_cast<uint32_t>(loop.capture.store.size() + sealed.size()));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(bAccepted.size()),
                           static_cast<uint32_t>(sealed.size()));
  for (size_t row = 0; row < bAccepted.size() && row < sealed.size(); ++row) {
    TEST_ASSERT_EQUAL(bAccepted[row].tick, sealed[row].tick);
    TEST_ASSERT_EQUAL(bAccepted[row].data.noteData.note, sealed[row].data.noteData.note);
  }
  // Exactly one committed pass, whose content is the accepted B input, once.
  for (unsigned turn = 0;
       turn < 20000 && loop.passes.overdubPasses.size() == passesBeforeB; ++turn) {
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_EQUAL_UINT32(passesBeforeB + 1,
                           static_cast<uint32_t>(loop.passes.overdubPasses.size()));
  SessionMidiEventVec committed;
  LoopEventStore::appendChunkRefEvents(loop.passes.overdubPasses.back().committedChunkIds, committed);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(bAccepted.size()),
                           static_cast<uint32_t>(committed.size()));
  for (size_t row = 0; row < bAccepted.size(); ++row) {
    TEST_ASSERT_EQUAL(bAccepted[row].tick, committed[row].tick);
    TEST_ASSERT_EQUAL(bAccepted[row].data.noteData.note, committed[row].data.noteData.note);
  }
  // ...and that interval's job retires, releasing its own receipt (DEC-068).
  for (unsigned turn = 0; turn < 20000 && TrackCaptureStopTestAccess::queueSize(track) > 0; ++turn)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(TrackCaptureStopTestAccess::queueSize(track)));
  TEST_ASSERT_FALSE(loop.hasOverdubSession());
  // A's receipt may still be queued ahead of it (FIFO); this interval's own is the newest.
  const size_t receiptCount = TrackCaptureStopTestAccess::receiptCount(track);
  TEST_ASSERT_TRUE(receiptCount >= 1);
  const SessionExitSnapshotHandoff* receipt =
      TrackCaptureStopTestAccess::receiptAt(track, receiptCount - 1);
  TEST_ASSERT_NOT_NULL(receipt);
  TEST_ASSERT_TRUE(receipt->ready);
  TEST_ASSERT_EQUAL(loop.passes.overdubPasses.back().id, receipt->lastPassId);
}

/// Task 5.1(e): a start request while B is still live is not admitted as a second live
/// interval — same interval identity, no staging reset, accepted state untouched — and a start
/// becomes admissible again after B's interval transfers (the live role is free from B's
/// admission; the transfer releases nothing the live role needs).
void test_start_request_while_successor_is_live_is_not_a_second_interval() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  clockManager.assignCurrentTickSilently(192);
  track.startOverdubbing(192);
  inputNote(62, 193, 216);
  const uint64_t bInterval = TrackCaptureStopTestAccess::interval(track);
  const size_t rowsBefore = loop.capture.store.size();
  const size_t notesBefore = TrackCaptureStopTestAccess::liveNotes(track).size();
  const size_t reservationsBefore = TrackCaptureStopTestAccess::liveReservations(track);
  const uint32_t stagingGenerationBefore =
      TrackCaptureStopTestAccess::runtime(track).liveCaptureStagingIfPrewarmed()->captureGeneration;
  // The request is refused as a *new* interval: same identity, no reset, nothing consumed.
  clockManager.assignCurrentTickSilently(220);
  track.startOverdubbing(220);
  TEST_ASSERT_TRUE(track.isOverdubbing());
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(bInterval, TrackCaptureStopTestAccess::interval(track),
                                   "a start while live must not create a second interval");
  TEST_ASSERT_EQUAL_UINT32(stagingGenerationBefore,
                           TrackCaptureStopTestAccess::runtime(track)
                               .liveCaptureStagingIfPrewarmed()->captureGeneration);
  TEST_ASSERT_EQUAL(rowsBefore, loop.capture.store.size());
  TEST_ASSERT_EQUAL(notesBefore, TrackCaptureStopTestAccess::liveNotes(track).size());
  TEST_ASSERT_EQUAL(reservationsBefore, TrackCaptureStopTestAccess::liveReservations(track));
  // Input after the request continues the same interval.
  inputNote(65, 221, 240);
  TEST_ASSERT_EQUAL(rowsBefore + 2, loop.capture.store.size());
  // A retires, then B's own stop is admitted once (its interval identity is preserved).
  for (unsigned turn = 0; turn < 10000 && TrackCaptureStopTestAccess::queueSize(track) > 0; ++turn)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  clockManager.assignCurrentTickSilently(288);
  track.stopOverdubbing();
  TEST_ASSERT_FALSE(track.isOverdubbing());
  TEST_ASSERT_EQUAL_UINT64(bInterval, TrackCaptureStopTestAccess::jobInterval(track));
  // A start is admissible again — the live role was freed when B froze into its job — as a
  // new interval with a clean live store. Its own stop is denied while B's job holds the
  // finalization resources, which is the same capacity rule one level down.
  clockManager.assignCurrentTickSilently(336);
  track.startOverdubbing(336);
  TEST_ASSERT_TRUE(track.isOverdubbing());
  TEST_ASSERT_EQUAL_UINT64(bInterval + 1, TrackCaptureStopTestAccess::interval(track));
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(loop.capture.store.size()));
  clockManager.assignCurrentTickSilently(400);
  track.stopOverdubbing();
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(),
                           "a successor stop must wait for the previous job's resources");
}

/// DEC-067: START is admitted on the live role alone. The device refused successor starts
/// with "canonical copy chunks are not reserved" while the predecessor finalized
/// (`successor-reentry-19` / `-20`); those resources are first used at the successor's own
/// stop edge, so they must not gate its start.
void test_successor_start_is_admitted_while_finalization_resources_are_busy() {
  Track& track = preparePlayingTrack();
  const size_t chunksBefore = TrackCaptureStopTestAccess::canonicalChunks(track);
  // A: deferred stop admitted while the resources were still available.
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  // A now owns them: its copy target is in use and its transfer credits are spent.
  TrackCaptureStopTestAccess::drainCanonicalChunks(track);
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::canonicalOutputAdmitted(track));
  // B: START must not wait for them.
  clockManager.assignCurrentTickSilently(192);
  track.startOverdubbing(192);
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(),
      "a successor start must not require the predecessor's finalization resources");
  inputNote(62, 193, 216);
  TEST_ASSERT_TRUE(track.getActiveLoop().capture.store.size() > 0);
  // Let A retire with its credits restored, so the next case starts from the prewarmed state.
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::restoreCanonicalChunks(track, chunksBefore));
  for (unsigned grain = 0; grain < 20000 && track.hasPendingDeferredOverdubStop(); ++grain)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
}

/// DEC-067: the frozen order/rewrite builders are per slot, not per interval. The start path
/// used to discard them unconditionally, which would clobber a pending predecessor mid-order;
/// it now only clears them when no job owns them.
void test_successor_start_keeps_the_predecessors_frozen_builders() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, UINT32_MAX);  // held: the interval carries an open reservation and closure work
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  for (unsigned grain = 0; grain < 64 && !TrackCaptureStopTestAccess::frozenOrderComplete(track); ++grain)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::frozenOrderComplete(track),
      "the predecessor must reach its frozen order before the successor starts");
  const uint64_t generationBefore = TrackCaptureStopTestAccess::frozenOrderStorageGeneration(track);
  clockManager.assignCurrentTickSilently(192);
  track.startOverdubbing(192);
  TEST_ASSERT_TRUE(track.isOverdubbing());
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::frozenOrderComplete(track),
      "the successor start must not discard the pending predecessor's frozen order");
  TEST_ASSERT_EQUAL_UINT64(generationBefore,
                           TrackCaptureStopTestAccess::frozenOrderStorageGeneration(track));
  const size_t passesBefore = track.getActiveLoop().passes.overdubPasses.size();
  for (unsigned grain = 0; grain < 20000 && track.hasPendingDeferredOverdubStop(); ++grain)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_EQUAL_UINT32(passesBefore + 1, track.getActiveLoop().passes.overdubPasses.size());
}

/// DEC-067: the stop admission is the single owner of the finalization-resource questions.
/// With no stop owner yet, an unavailable canonical copy target must refuse the stop
/// atomically: still recording, no descriptor, accepted rows intact.
void test_successor_stop_is_denied_when_the_canonical_copy_target_is_busy() {
  Track& track = preparePlayingTrack();
  const size_t chunksBefore = TrackCaptureStopTestAccess::canonicalChunks(track);
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  TEST_ASSERT_TRUE(track.isOverdubbing());
  TrackCaptureStopTestAccess::drainCanonicalChunks(track);
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::canonicalOutputAdmitted(track));
  const uint8_t stateBefore = track.getState();
  const size_t rowsBefore = track.getActiveLoop().capture.store.size();
  const size_t descriptorsBefore = TrackCaptureStopTestAccess::queueSize(track);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(),
      "a stop without the canonical copy target must stay in the live interval");
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(descriptorsBefore),
                           static_cast<uint32_t>(TrackCaptureStopTestAccess::queueSize(track)));
  TEST_ASSERT_EQUAL_UINT8(stateBefore, track.getState());
  TEST_ASSERT_EQUAL(rowsBefore, track.getActiveLoop().capture.store.size());
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::restoreCanonicalChunks(track, chunksBefore));
}

/// Task 5.2 isolation: A's deferred completion during B's capture leaves B's rows, pending
/// notes, reservations, preview and track state untouched, and forces no transition on B's
/// behalf. This is the assertion breadth task 2.3 was waiting on.
void test_deferred_predecessor_completion_leaves_successor_interval_untouched() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  // A: deferred stop, owning the frozen role.
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  // B: successor interval with a distinctive held note, so a reservation and a pending
  // note exist to be trampled if A's cleanup were not job-local.
  clockManager.assignCurrentTickSilently(192);
  track.startOverdubbing(192);
  inputNote(64, 193, UINT32_MAX);
  TEST_ASSERT_TRUE(track.isOverdubbing());
  const uint8_t stateBefore = track.getState();
  const size_t rowsBefore = loop.capture.store.size();
  const size_t notesBefore = TrackCaptureStopTestAccess::liveNotes(track).size();
  const size_t previewBefore = loop.capturePreview.notes.size();
  const size_t reservationsBefore = TrackCaptureStopTestAccess::liveReservations(track);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(rowsBefore));
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(notesBefore));
  TEST_ASSERT_TRUE(reservationsBefore > 0);
  // A retires while B is live.
  for (unsigned turn = 0; turn < 10000 && TrackCaptureStopTestAccess::queueSize(track) > 0; ++turn)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(TrackCaptureStopTestAccess::queueSize(track)));
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(),
      "A's deferred completion must not force a transition on B");
  TEST_ASSERT_EQUAL(stateBefore, track.getState());
  TEST_ASSERT_EQUAL(rowsBefore, loop.capture.store.size());
  TEST_ASSERT_EQUAL(193, loop.capture.store.at(0).tick);
  TEST_ASSERT_EQUAL(notesBefore, TrackCaptureStopTestAccess::liveNotes(track).size());
  TEST_ASSERT_EQUAL(previewBefore, loop.capturePreview.notes.size());
  TEST_ASSERT_EQUAL(reservationsBefore, TrackCaptureStopTestAccess::liveReservations(track));
}

/// The deferred stop records the published source it succeeded, and pins that
/// source's root, at admission. A later grain must never re-resolve it against a
/// source a successor published in the meantime.
/// The deferred stop records the published source it succeeded and pins that
/// source's root at admission. A later grain must never re-resolve it against a
/// source published in the meantime: once the recorded predecessor is no longer
/// the live root, the interval retires without a successor window, keeping its
/// accepted capture pass and publishing no audible bridge.
void test_deferred_stop_records_and_pins_its_causal_predecessor_source() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  loop.storePublishedSnapshotForTest(
      LoopSnapshot::createEmpty(loop.loopId, loop.loopLengthTicks, loop.loopStartTick,
                                loop.nextNoteId_, 1),
      loop.playbackRevision);
  const uint32_t admittedRevision = loop.playbackRevision;
  const void* admittedSnapshot = loop.publishedSnapshotIdentity();
  TEST_ASSERT_NOT_NULL(admittedSnapshot);
  const size_t admittedHead = TrackCaptureStopTestAccess::headIndex(track);
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, UINT32_MAX);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::causalBaseRecorded(track));
  const PlaybackViewSourceIdentity recorded = TrackCaptureStopTestAccess::causalBase(track);
  TEST_ASSERT_EQUAL_UINT32(admittedRevision, recorded.playbackRevision);
  TEST_ASSERT_EQUAL_UINT32(TrackCaptureStopTestAccess::generation(track), recorded.sourceGeneration);
  TEST_ASSERT_EQUAL_PTR(admittedSnapshot, recorded.snapshotIdentity);
  TEST_ASSERT_EQUAL_PTR(recorded.snapshotIdentity, TrackCaptureStopTestAccess::pinnedCausalSnapshot(track));
  // A later publication must not rewrite where this interval came from.
  TrackCaptureStopTestAccess::setPlaybackRevision(track, admittedRevision + 7);
  TEST_ASSERT_EQUAL_UINT32(admittedRevision, TrackCaptureStopTestAccess::causalBase(track).playbackRevision);
  TEST_ASSERT_EQUAL_PTR(admittedSnapshot, TrackCaptureStopTestAccess::pinnedCausalSnapshot(track));
  TEST_ASSERT_EQUAL_UINT32(admittedRevision + 7, TrackCaptureStopTestAccess::playbackRevision(track));
  // Approved superseded rule: retire the interval, keep its accepted capture, and
  // never select an audible bridge against a source it did not succeed.
  const size_t passesBefore = loop.passes.overdubPasses.size();
  for (unsigned grain = 0; grain < 4096 && track.hasPendingDeferredOverdubStop(); ++grain)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_EQUAL_UINT32(passesBefore + 1, loop.passes.overdubPasses.size());
  TEST_ASSERT_EQUAL_UINT8(RuntimePlaybackBridgeLifecycle::kNoBridge,
                          TrackCaptureStopTestAccess::bridgeSelected(track));
  // The unpublished companion preparation is back on its prewarmed horizon, so
  // the next interval needs no STOPPED-only re-prewarm, and the retired
  // descriptor carries no causal receipt.
  TEST_ASSERT_FALSE(loop.preparedCompanionHistoryInFlight());
  TEST_ASSERT_TRUE(loop.preparedCompanionHistoryPrewarmed());
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::entryCausalBaseRecorded(track, admittedHead));
}

/// Openspec 5.4: supersession under reentry. A deferred predecessor's recorded causal base is no
/// longer the published root by the time it resolves, and a successor interval is live at the same
/// time. The predecessor must commit its accepted capture exactly once, select no bridge
/// (`kNoBridge`), leave the newer root selected, and never touch the successor's live state --
/// while the successor keeps its rows, pending notes, preview and reservations.
void test_superseded_predecessor_retires_without_touching_a_live_successor() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  loop.storePublishedSnapshotForTest(
      LoopSnapshot::createEmpty(loop.loopId, loop.loopLengthTicks, loop.loopStartTick,
                                loop.nextNoteId_, 1),
      loop.playbackRevision);
  const uint32_t admittedRevision = loop.playbackRevision;
  const void* const admittedSnapshot = loop.publishedSnapshotIdentity();
  TEST_ASSERT_NOT_NULL(admittedSnapshot);
  // A deferred against the admitted root.
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::causalBaseRecorded(track));
  TEST_ASSERT_EQUAL_PTR(admittedSnapshot,
                        TrackCaptureStopTestAccess::causalBase(track).snapshotIdentity);
  // B is live while A pends, with its own accepted input.
  clockManager.assignCurrentTickSilently(192);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.isOverdubbing());
  inputNote(62, 193, 216);
  const size_t bRows = loop.capture.store.size();
  const size_t bNotes = TrackCaptureStopTestAccess::liveNotes(track).size();
  const size_t bPreview = loop.capturePreview.notes.size();
  const size_t bReservations = TrackCaptureStopTestAccess::liveReservations(track);
  // Publication advances past A's recorded causal predecessor.
  TrackCaptureStopTestAccess::setPlaybackRevision(track, admittedRevision + 7);
  const uint32_t supersededRevision = TrackCaptureStopTestAccess::playbackRevision(track);
  const size_t passesBefore = loop.passes.overdubPasses.size();
  for (unsigned grain = 0; grain < 20000 && track.hasPendingDeferredOverdubStop(); ++grain)
    (void)TrackCaptureStopTestAccess::step(track);
  // A commits exactly one pass, publishes no bridge, and restores its unpublished horizon.
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_EQUAL_UINT32(passesBefore + 1, static_cast<uint32_t>(loop.passes.overdubPasses.size()));
  TEST_ASSERT_EQUAL_UINT8(RuntimePlaybackBridgeLifecycle::kNoBridge,
                          TrackCaptureStopTestAccess::bridgeSelected(track));
  TEST_ASSERT_TRUE(loop.preparedCompanionHistoryPrewarmed());
  // The successor's live state is untouched by the retiring predecessor.
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(),
      "a superseded predecessor must not transition the live successor");
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(bRows), static_cast<uint32_t>(loop.capture.store.size()));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(bNotes),
                           static_cast<uint32_t>(TrackCaptureStopTestAccess::liveNotes(track).size()));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(bPreview),
                           static_cast<uint32_t>(loop.capturePreview.notes.size()));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(bReservations),
                           static_cast<uint32_t>(TrackCaptureStopTestAccess::liveReservations(track)));
  TEST_ASSERT_EQUAL(193, loop.capture.store.at(0).tick);
  TEST_ASSERT_EQUAL(216, loop.capture.store.at(1).tick);
  // The live revision is still the newer one; A never regressed it.
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(supersededRevision,
                                      TrackCaptureStopTestAccess::playbackRevision(track));
  TEST_ASSERT_GREATER_THAN_UINT32(admittedRevision, TrackCaptureStopTestAccess::playbackRevision(track));
}

/// Openspec 5.5: reentry must not depend on where the deferred predecessor happens to be.
/// The predecessor is paused at meaningful worker checkpoints, a successor starts at each one and
/// accepts a distinctive row, and the predecessor is then resumed to completion. Both intervals
/// are verified at every checkpoint: the successor keeps its accepted input and stays the live
/// interval, and the predecessor retires exactly once with its own job-local cleanup.
/// OpenSpec 5.12 / D8 resolved the `CompanionHistory` checkpoint that used to be red here: the
/// predecessor's staged pass is now interval-owned, so the successor's capture start cannot
/// discard it and the companion preparation re-admits against the job's own receipt. All
/// checkpoints now run in the default gate; 5.5's parameterized coverage is complete.
///
/// `pauseAtStagedPassOwnership` is the D8 boundary itself: the phase enum is *not* execution order
/// (`PrepareTransfer` dispatches before `CanonicalTail`), and the grain that enters
/// `PrepareTransfer` also stages the pass and leaves the phase, so that checkpoint cannot be
/// expressed as a phase threshold. It is instead "the turn on which the job first owns its staged
/// pass", which is exactly the state a successor's `beginCapture` used to destroy.
void runAdversarialMidGrainReentry(uint8_t checkpointPhase, const char* checkpointName,
                                   bool pauseAtStagedPassOwnership = false) {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(track.hasPendingDeferredOverdubStop(), checkpointName);
  // Advance the predecessor's worker to the requested checkpoint and stop turning it there.
  if (pauseAtStagedPassOwnership) {
    for (unsigned grain = 0; grain < 20000; ++grain) {
      if (TrackCaptureStopTestAccess::jobPass(track) != nullptr) break;
      (void)TrackCaptureStopTestAccess::step(track);
    }
    TEST_ASSERT_NOT_NULL_MESSAGE(TrackCaptureStopTestAccess::jobPass(track),
        "the ownership checkpoint requires the predecessor to own its staged pass");
    // Staging, taking the receipt and advancing the phase are one non-yielding grain, so the pause
    // lands on the phase that follows `PrepareTransfer` (`CanonicalTail`: the capture is populated).
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<uint8_t>(PendingStopPhase::CanonicalTail),
        TrackCaptureStopTestAccess::jobPhase(track),
        "the ownership checkpoint must pause on the turn the staged pass moved into the job");
  } else {
    for (unsigned grain = 0; grain < 20000; ++grain) {
      if (TrackCaptureStopTestAccess::jobPhase(track) >= checkpointPhase) break;
      (void)TrackCaptureStopTestAccess::step(track);
    }
    // Equality, not a threshold: a checkpoint that silently ran past its phase would not be the
    // state the fixture claims to exercise.
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(checkpointPhase, TrackCaptureStopTestAccess::jobPhase(track),
        checkpointName);
  }
  TEST_ASSERT_TRUE_MESSAGE(track.hasPendingDeferredOverdubStop(), checkpointName);
  // OpenSpec 5.12 / D8: once the predecessor has staged its pass (`PrepareTransfer` and later),
  // that pass must survive the successor's capture start with its own identity, because the
  // predecessor's companion preparation admits only while it exists. Asserting identity -- not row
  // counts -- catches a shared container that happens to hold the right number of rows. Earlier
  // checkpoints never staged a pass (nothing to protect yet), so the assertion is scoped there.
  // The staged pass exists from `PrepareTransfer` through the companion work; at
  // `AtomicTransfer` the job has already consumed it, so there is nothing to protect.
  const bool passStaged = pauseAtStagedPassOwnership ||
      checkpointPhase == static_cast<uint8_t>(PendingStopPhase::CanonicalTail) ||
      checkpointPhase == static_cast<uint8_t>(PendingStopPhase::CompanionHistory);
  const PassId aStagedBeforeB = TrackCaptureStopTestAccess::stagedPassId(track);
  // The successor starts at the checkpoint, on the live role, and accepts its own row.
  clockManager.assignCurrentTickSilently(192);
  midiButtonActions.handleToggleRecord();
  if (passStaged) {
    TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::hasStagedPass(track),
        "the predecessor's staged pass must survive the successor's capture start");
    TEST_ASSERT_EQUAL_MESSAGE(aStagedBeforeB, TrackCaptureStopTestAccess::stagedPassId(track),
        "the surviving pass must still be the predecessor's own pass");
  }
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(), checkpointName);
  inputNote(62, 193, 216);
  const size_t bRows = loop.capture.store.size();
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, static_cast<uint32_t>(bRows), checkpointName);
  TEST_ASSERT_EQUAL_MESSAGE(193, loop.capture.store.at(0).tick, checkpointName);
  TEST_ASSERT_EQUAL_MESSAGE(216, loop.capture.store.at(1).tick, checkpointName);
  // Resume the predecessor: it must retire without disturbing the live successor.
  for (unsigned grain = 0; grain < 20000 && track.hasPendingDeferredOverdubStop(); ++grain)
    (void)TrackCaptureStopTestAccess::step(track);
  // OpenSpec 5.12 / D8 retired this checkpoint's open finding. The predecessor's staged pass now
  // lives in its own job from `PrepareTransfer` onward, so the successor's `beginCapture` no longer
  // discards it, `beginPreparedCompanionHistory` binds the job-owned receipt, and the job retires.
  TEST_ASSERT_FALSE_MESSAGE(track.hasPendingDeferredOverdubStop(),
      "a predecessor paused at CompanionHistory must still retire after a successor starts");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::captureStopOwnerClear(track), checkpointName);
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(), checkpointName);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(bRows),
                                   static_cast<uint32_t>(loop.capture.store.size()), checkpointName);
  TEST_ASSERT_EQUAL_MESSAGE(193, loop.capture.store.at(0).tick, checkpointName);
  TEST_ASSERT_EQUAL_MESSAGE(216, loop.capture.store.at(1).tick, checkpointName);
}

/// OpenSpec 5.12 / D8: a staged capture pass is interval-owned, not loop-shared. This is the
/// strongest D8 regression because it proves the shared-slot dependency is *gone* rather than
/// merely deferred in time.
///
/// A reaches `CompanionHistory`, so A's pass has been staged and moved into job A while the Loop
/// pending slot is empty again. B (the live successor) then stages its own pass into that slot --
/// exactly the staging a successor's `PrepareTransfer` performs. The old shared scalar answered
/// `AlreadyPending` here, and the "protect the shared pass" attempt could only buy A's survival by
/// denying B. With D8 both receipts exist concurrently with distinct identities and chunk storage.
///
/// Route note: a wrap commit cannot produce this state today because DEC-069 defers the live
/// interval's wrap commit while the ring is non-empty, so the case is constructed at the same Loop
/// boundary that owns the collision (`stagePreparedCapturePassFromActiveAdmission`).
void test_simultaneous_interval_owned_staged_passes() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  // Advance A to the companion checkpoint: its pass exists and is owned by the job.
  for (unsigned grain = 0; grain < 20000; ++grain) {
    if (TrackCaptureStopTestAccess::jobPhase(track) >=
        static_cast<uint8_t>(PendingStopPhase::CompanionHistory)) break;
    (void)TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_GREATER_OR_EQUAL_UINT8(static_cast<uint8_t>(PendingStopPhase::CompanionHistory),
                                     TrackCaptureStopTestAccess::jobPhase(track));
  const auto* aPass = TrackCaptureStopTestAccess::jobPass(track);
  TEST_ASSERT_NOT_NULL_MESSAGE(aPass, "job A must own its staged pass from PrepareTransfer onward");
  const PassId aId = aPass->id;
  const uint16_t* const aChunkData = aPass->committedChunkIds.data();
  const size_t aChunkCount = aPass->committedChunkIds.size();
  const uint8_t aSession = aPass->overdubSessionIndex;
  TEST_ASSERT_GREATER_THAN_UINT32(0u, static_cast<uint32_t>(aChunkCount));
  TEST_ASSERT_NULL_MESSAGE(TrackCaptureStopTestAccess::loopPass(track),
      "the Loop pending slot is empty once job A owns its pass");
  const size_t aPassCount = loop.passes.overdubPasses.size();
  const size_t aPassCapacity = loop.passes.overdubPasses.capacity();
  const void* const aPassRowData = loop.passes.overdubPasses.data();
  // B starts on the live role and accepts its own distinctive row.
  clockManager.assignCurrentTickSilently(192);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.isOverdubbing());
  inputNote(62, 193, 216);
  // B's own `PrepareTransfer` staging: the Loop slot it uses is free *because* A's receipt moved
  // into the job, which is what makes concurrent ownership possible at all.
  Capture bCapture;
  bCapture.phase = CapturePhase::Overdub;
  TEST_ASSERT_TRUE(bCapture.store.append(MidiEvent::NoteOn(193, 1, 62, 100)));
  TEST_ASSERT_TRUE(bCapture.store.append(MidiEvent::NoteOff(216, 1, 62, 0)));
  TEST_ASSERT_EQUAL_MESSAGE(SealOutcome::Ok,
      loop.stagePreparedCapturePassFromActiveAdmission(bCapture, 240, CommitReason::OverdubStop),
      "a successor must be able to stage its own pass while a predecessor job owns one");
  const auto* bPass = TrackCaptureStopTestAccess::loopPass(track);
  TEST_ASSERT_NOT_NULL(bPass);
  // Simultaneous ownership with distinct identities, storage and session.
  TEST_ASSERT_NOT_EQUAL_MESSAGE(aId, bPass->id, "the two intervals must own distinct passes");
  TEST_ASSERT_TRUE_MESSAGE(bPass->committedChunkIds.data() != aChunkData,
      "the two intervals must not share one chunk container");
  if (aSession != kUngroupedOverdubSessionIndex) {
    TEST_ASSERT_NOT_EQUAL_MESSAGE(aSession, bPass->overdubSessionIndex,
        "a successor opens its own overdub session");
  }
  TEST_ASSERT_EQUAL_MESSAGE(aId, TrackCaptureStopTestAccess::jobPass(track)->id,
      "B's staging must not rebind A's receipt");
  TEST_ASSERT_EQUAL_PTR_MESSAGE(aChunkData,
      TrackCaptureStopTestAccess::jobPass(track)->committedChunkIds.data(),
      "B's staging must not relocate A's chunk storage");
  // Resume A. Its terminal transfer consumes A's pass directly -- never the Loop slot -- and does
  // no allocation: the canonical row vector neither grows nor relocates, and A's chunk storage is
  // moved by pointer into A's canonical pass.
  for (unsigned grain = 0; grain < 20000 && track.hasPendingDeferredOverdubStop(); ++grain)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_NULL_MESSAGE(TrackCaptureStopTestAccess::jobPass(track),
      "A's ownership is retired with its job");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(aPassCount + 1),
      static_cast<uint32_t>(loop.passes.overdubPasses.size()),
      "A's transfer must append exactly one canonical pass");
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(aPassCapacity),
                           static_cast<uint32_t>(loop.passes.overdubPasses.capacity()));
  TEST_ASSERT_EQUAL_PTR_MESSAGE(aPassRowData, loop.passes.overdubPasses.data(),
      "A's transfer must not relocate canonical pass storage");
  TEST_ASSERT_EQUAL(aId, loop.passes.overdubPasses.back().id);
  TEST_ASSERT_EQUAL_PTR_MESSAGE(aChunkData, loop.passes.overdubPasses.back().committedChunkIds.data(),
      "A's transfer must move its chunk storage, not copy it");
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(aChunkCount),
      static_cast<uint32_t>(loop.passes.overdubPasses.back().committedChunkIds.size()));
  // B's receipt is exactly where and what it was: A's retirement cannot consume or clear it.
  const auto* bPassAfter = TrackCaptureStopTestAccess::loopPass(track);
  TEST_ASSERT_NOT_NULL(bPassAfter);
  TEST_ASSERT_NOT_EQUAL(aId, bPassAfter->id);
  TEST_ASSERT_TRUE(loop.hasPendingCapturePass());
  const PassId bId = bPassAfter->id;
  const uint16_t* const bChunkData = bPassAfter->committedChunkIds.data();
  const size_t bChunkCount = bPassAfter->committedChunkIds.size();
  // B then consumes B exactly once, and its own receipt is the one that moves.
  PreparedCapturePassTransfer bTransfer{};
  TEST_ASSERT_TRUE(loop.commitPreparedCapturePassAtomic(bTransfer));
  TEST_ASSERT_EQUAL(bId, bTransfer.id);
  TEST_ASSERT_FALSE(loop.hasPendingCapturePass());
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(aPassCount + 2),
                           static_cast<uint32_t>(loop.passes.overdubPasses.size()));
  TEST_ASSERT_EQUAL(bId, loop.passes.overdubPasses.back().id);
  TEST_ASSERT_EQUAL_PTR_MESSAGE(bChunkData,
      loop.passes.overdubPasses.back().committedChunkIds.data(),
      "B's transfer must move B's chunk storage, not copy A's");
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(bChunkCount),
      static_cast<uint32_t>(loop.passes.overdubPasses.back().committedChunkIds.size()));
  TEST_ASSERT_FALSE_MESSAGE(loop.commitPreparedCapturePassAtomic(bTransfer),
      "a consumed pass cannot be consumed twice");
}

// ---------------------------------------------------------------- 2.5a differential harness
// OpenSpec 2.5a (parent `resumable-midi-boundary-playback-maintenance`): compare the emitted
// playback stream of the current *synchronous* merged/order/reanchor path (ORACLE) against a
// pair built off the clock path and consumed (CANDIDATE). The comparison is ordered rows --
// kind, channel, note, value, tick, and therefore same-tick order -- not aggregate counts,
// because the coherency bugs live in ordering and transition boundaries.
namespace {

struct DifferentialRow {
  uint32_t tick = 0;
  uint8_t type = 0;
  uint8_t channel = 0;
  uint8_t note = 0;
  uint8_t value = 0;
};

struct DifferentialTrace {
  std::vector<DifferentialRow> rows;
};

void differentialRecordSend(void* ctx, const MidiEvent& evt, uint8_t) {
  auto* trace = static_cast<DifferentialTrace*>(ctx);
  DifferentialRow row{};
  row.tick = evt.tick;
  row.type = static_cast<uint8_t>(evt.type);
  row.channel = evt.channel;
  row.note = evt.data.noteData.note;
  row.value = evt.data.noteData.velocity;
  trace->rows.push_back(row);
}

struct DifferentialPairStream {
  const SessionMidiEventVec* events = nullptr;
  const PlaybackOrderVec* order = nullptr;
};

size_t differentialStreamSize(const void* ctx) {
  return static_cast<const DifferentialPairStream*>(ctx)->order->size();
}

const MidiEvent& differentialStreamEventAt(const void* ctx, uint16_t cursor) {
  const auto* stream = static_cast<const DifferentialPairStream*>(ctx);
  return (*stream->events)[(*stream->order)[cursor]];
}

uint32_t differentialStreamPhase(const MidiEvent& evt, const ProjectionContext&) {
  return evt.tick;
}

/// Seed the prepared pair directly. 2.5a's first testable unit is the *pair consumption*
/// contract: the merged window and the order it indexes must be consumed coherently, without
/// the consumer rebuilding either. (Gathering a real window needs Loop's published snapshot
/// root, i.e. the publication pipeline; that fixture is the next 2.5 increment.)
void seedPreparedPair(Loop& loop, LoopPlaybackRuntime& runtime,
                      const SessionMidiEventVec& events) {
  runtime.mergedMidiEvents.mergedEvents = events;
  runtime.mergedMidiEvents.windowStartTick = 0;
  runtime.mergedMidiEvents.windowLengthTicks = loop.loopLengthTicks;
  runtime.mergedMidiEvents.builtFromRevision = loop.playbackRevision;
  runtime.mergedMidiEvents.builtFromSnapshotIdentity = loop.publishedSnapshotIdentity();
  ProjectionContext ctx{};
  ctx.loopLength = loop.loopLengthTicks;
  // The off-clock owner's pair completion: order rebuild + reanchor, then the pair is coherent.
  ::rebuildPlaybackOrder(loop, runtime.mergedMidiEvents.mergedEvents, ctx);
  reanchorPlaybackIndex(loop, runtime.mergedMidiEvents.mergedEvents, loop.getPlaybackOrder(), ctx);
}

/// Consume the currently prepared pair over (fromTick, toTick] and record the emitted rows.
void consumePairOverRange(Loop& loop, LoopPlaybackRuntime& runtime, uint32_t fromTick,
                          uint32_t toTick, DifferentialTrace& out) {
  DifferentialPairStream pairStream{&runtime.mergedMidiEvents.mergedEvents,
                                    &loop.getPlaybackOrder()};
  const PlaybackEventStream stream{&pairStream, differentialStreamSize, nullptr,
                                   differentialStreamEventAt, differentialStreamPhase};
  for (uint32_t tick = fromTick + 1; tick <= toTick; ++tick) {
    ProjectionContext ctx{};
    ctx.loopLength = loop.loopLengthTicks;
    const PlaybackTickFrame frame{&ctx, tick, tick - 1, false};
    PlaybackCursorAdvanceState advance{&loop.nextEventIndex, &loop.playbackOrderDirty};
    (void)advancePlaybackCursor(advance, frame, PlaybackEmitPolicy::ActiveCommitted, stream,
                                differentialRecordSend, &out, 0, nullptr, nullptr, 1);
  }
}

/// OpenSpec 2.4 slice 2: consume the *published* immutable pair over (fromTick, toTick]
/// through the same production stream adapter the clock path uses. The pair supplies both
/// members and its own cursor seed, so nothing is rebuilt and nothing is reanchored.
void consumePublishedPairOverRange(Loop& loop, LoopPlaybackRuntime& runtime, uint32_t fromTick,
                                   uint32_t toTick, DifferentialTrace& out) {
  TEST_ASSERT_TRUE(runtime.preparedPair.ready);
  TEST_ASSERT_TRUE(runtime.preparedPair.storage != nullptr);
  FixedPlaybackStreamCtx pairCtx{runtime.preparedPair.storage};
  const PlaybackEventStream stream = makeFixedPlaybackStream(pairCtx);
  loop.nextEventIndex = runtime.preparedPair.cursorSeed;
  for (uint32_t tick = fromTick + 1; tick <= toTick; ++tick) {
    ProjectionContext ctx{};
    ctx.loopLength = loop.loopLengthTicks;
    const PlaybackTickFrame frame{&ctx, tick, tick - 1, false};
    // Immutable source: no playbackOrderDirty invalidation pointer.
    PlaybackCursorAdvanceState advance{&loop.nextEventIndex, nullptr};
    (void)advancePlaybackCursor(advance, frame, PlaybackEmitPolicy::ActiveCommitted, stream,
                                differentialRecordSend, &out, 0, nullptr, nullptr, 1);
  }
}

bool differentialTracesEqual(const DifferentialTrace& a, const DifferentialTrace& b) {
  if (a.rows.size() != b.rows.size()) return false;
  for (size_t i = 0; i < a.rows.size(); ++i) {
    const DifferentialRow& x = a.rows[i];
    const DifferentialRow& y = b.rows[i];
    if (x.tick != y.tick || x.type != y.type || x.channel != y.channel || x.note != y.note ||
        x.value != y.value) {
      return false;
    }
  }
  return true;
}

// ------------------------------------------- 2.5a gathering fixtures (real snapshot root)
//
// The 2.5a harness could compare a pair built from a *synthetic* (events, order) window. These
// helpers drive the production gather instead: a published snapshot root supplies the window,
// `ensurePlaybackMergedEventsForSlot` / `servicePendingPreparedPlaybackPair` run the synchronous
// builder and publish the pair, and the two consumers must emit byte-identical ordered rows.

struct FixtureSnapshotNote {
  uint8_t pitch = 60;
  uint32_t startTick = 0;
  /// Exclusive end. `endTick < startTick` is the snapshot's wrap representation; an end at or
  /// beyond the loop length is the loop-edge case whose Off the window gather drops.
  uint32_t endTick = 0;
};

/// Build a snapshot root whose content becomes the gather source for playback, and publish it at
/// the loop's current revision so `publishedSnapshotIsCurrent()` holds and the gather takes the
/// plain `Snapshot` path.
///
/// Note membership and MIDI-event membership are independent snapshot roots (see
/// `appendCompanionEventChanges`), and the playback gather reads the *event* root, so the helper
/// adds both. The event rows mirror the publisher: an Off at or beyond the loop length is clamped to
/// `loopLength - 1`, and a wrap note (`endTick < startTick`) becomes a head plus a tail with the same
/// NoteId.
ImmutableLoopSnapshotRef buildFixtureRoot(Loop& loop,
                                          const std::vector<FixtureSnapshotNote>& notes) {
  const uint32_t loopLength = loop.loopLengthTicks;
  NoteId noteId = loop.nextNoteId_;
  EventId eventId = 1;
  uint32_t canonicalOrder = 1;
  ImmutableLoopSnapshotRef parent = LoopSnapshot::createEmpty(
      loop.loopId, loop.loopLengthTicks, loop.loopStartTick, loop.nextNoteId_, 1);
  LoopSnapshotChangeSet changes;
  auto addEvent = [&](uint32_t tick, uint8_t pitch, NoteId id, bool noteOn) {
    SnapshotEventChange change{};
    change.kind = SnapshotChangeKind::Add;
    change.next.loopId = loop.loopId;
    change.next.eventId = eventId++;
    change.next.canonicalOrder = canonicalOrder++;
    change.next.value = noteOn ? MidiEvent::NoteOn(tick, 1, pitch, 100)
                              : MidiEvent::NoteOff(tick, 1, pitch, 0);
    change.next.value.noteId = id;
    changes.events.push_back(change);
  };
  for (const FixtureSnapshotNote& note : notes) {
    const NoteId id = noteId++;
    SnapshotNoteChange add{};
    add.kind = SnapshotChangeKind::Add;
    add.next.loopId = loop.loopId;
    add.next.noteId = id;
    add.next.pitch = note.pitch;
    add.next.velocity = 100;
    add.next.startTick = note.startTick;
    add.next.endTick = note.endTick;
    changes.notes.push_back(add);
    if (note.endTick < note.startTick) {
      addEvent(note.startTick, note.pitch, id, true);
      addEvent(loopLength - 1, note.pitch, id, false);
      addEvent(0, note.pitch, id, true);
      addEvent(note.endTick, note.pitch, id, false);
    } else {
      addEvent(note.startTick, note.pitch, id, true);
      addEvent(note.endTick < loopLength ? note.endTick : loopLength - 1, note.pitch, id, false);
    }
  }
  LoopSnapshotConstruction construction(std::move(parent), changes);
  while (!construction.complete()) {
    if (!construction.step(128)) break;
  }
  TEST_ASSERT_FALSE_MESSAGE(construction.failed(), "fixture snapshot construction must succeed");
  ImmutableLoopSnapshotRef root = construction.freeze();
  TEST_ASSERT_NOT_NULL(root.get());
  loop.storePublishedSnapshotForTest(root, loop.playbackRevision);
  TEST_ASSERT_TRUE_MESSAGE(loop.publishedSnapshotIsCurrent(),
                           "the fixture root must be current for the plain gather path");
  return root;
}

/// Compare the published pair against the synchronous merged/order path from the pair's own
/// anchor. The pair is stored in emission order with an identity `order`, so its `cursorSeed`
/// indexes the same emission position in the synchronous order; anchoring both consumers is what
/// makes the comparison about the pair, not about where each cursor happened to start.
void assertPairMatchesSynchronousPathFromAnchor(Loop& loop, LoopPlaybackRuntime& runtime,
                                                uint32_t toTick, DifferentialTrace& oracleOut,
                                                DifferentialTrace& candidateOut) {
  TEST_ASSERT_TRUE(runtime.preparedPair.ready);
  loop.nextEventIndex = runtime.preparedPair.cursorSeed;
  consumePairOverRange(loop, runtime, 0, toTick, oracleOut);
  consumePublishedPairOverRange(loop, runtime, 0, toTick, candidateOut);
  TEST_ASSERT_TRUE_MESSAGE(differentialTracesEqual(oracleOut, candidateOut),
      "the published pair must emit the synchronous path's rows in the same order");
}

/// Independent reference order for the long-loop window fixture: phase, then tick, then the
/// canonical equal-phase NoteOff-before-NoteOn key. Built here in the test so the reference is
/// not the production builder it is checking.
struct DifferentialRefStream {
  const SessionMidiEventVec* events = nullptr;
  const std::vector<uint16_t>* order = nullptr;
};

size_t differentialRefStreamSize(const void* ctx) {
  return static_cast<const DifferentialRefStream*>(ctx)->order->size();
}

const MidiEvent& differentialRefStreamEventAt(const void* ctx, uint16_t cursor) {
  const auto* stream = static_cast<const DifferentialRefStream*>(ctx);
  return (*stream->events)[(*stream->order)[cursor]];
}

void buildReferenceEmissionOrder(const SessionMidiEventVec& events, uint32_t loopLength,
                                 std::vector<uint16_t>& orderOut) {
  orderOut.resize(events.size());
  for (size_t i = 0; i < events.size(); ++i) orderOut[i] = static_cast<uint16_t>(i);
  std::stable_sort(orderOut.begin(), orderOut.end(), [&](uint16_t a, uint16_t b) {
    const uint32_t phaseA = IntervalProjection::playbackEventPhase(events[a].tick, loopLength);
    const uint32_t phaseB = IntervalProjection::playbackEventPhase(events[b].tick, loopLength);
    if (phaseA != phaseB) return phaseA < phaseB;
    if (events[a].tick != events[b].tick) return events[a].tick < events[b].tick;
    const int keyA = events[a].isNoteOff() ? 0 : (events[a].isNoteOn() ? 1 : 2);
    const int keyB = events[b].isNoteOff() ? 0 : (events[b].isNoteOn() ? 1 : 2);
    return keyA < keyB;
  });
}

void consumeReferenceOverRange(Loop& loop, const SessionMidiEventVec& events,
                               const std::vector<uint16_t>& order, uint32_t fromTick, uint32_t toTick,
                               DifferentialTrace& out) {
  DifferentialRefStream refStream{&events, &order};
  const PlaybackEventStream stream{&refStream, differentialRefStreamSize, nullptr,
                                   differentialRefStreamEventAt, differentialStreamPhase};
  loop.nextEventIndex = 0;
  for (uint32_t tick = fromTick + 1; tick <= toTick; ++tick) {
    ProjectionContext ctx{};
    ctx.loopLength = loop.loopLengthTicks;
    const PlaybackTickFrame frame{&ctx, tick, tick - 1, false};
    PlaybackCursorAdvanceState advance{&loop.nextEventIndex, nullptr};
    (void)advancePlaybackCursor(advance, frame, PlaybackEmitPolicy::ActiveCommitted, stream,
                                differentialRecordSend, &out, 0, nullptr, nullptr, 1);
  }
}

}  // namespace

/// Fixture 1 -- plain immutable loop playback. The prepared pair and the independently
/// re-prepared pair must emit the identical ordered stream, so the harness itself is proven
/// non-vacuous before any transition fixture can use it.
void test_prepared_pair_stream_matches_synchronous_oracle_plain_playback() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  LoopPlaybackRuntime& runtime = TrackCaptureStopTestAccess::runtime(track);
  SessionMidiEventVec committed;
  committed.push_back(MidiEvent::NoteOn(0, 1, 48, 100));
  committed.push_back(MidiEvent::NoteOff(48, 1, 48, 0));
  committed.push_back(MidiEvent::NoteOn(384, 1, 55, 90));
  committed.push_back(MidiEvent::NoteOff(432, 1, 55, 0));
  // ORACLE: prepare the pair, consume it, then prepare + consume it again independently.
  seedPreparedPair(loop, runtime, committed);
  DifferentialTrace oracle;
  consumePairOverRange(loop, runtime, 0, 768, oracle);
  // CANDIDATE: a second consumer over a freshly prepared pair.
  seedPreparedPair(loop, runtime, committed);
  DifferentialTrace candidate;
  consumePairOverRange(loop, runtime, 0, 768, candidate);
  TEST_ASSERT_GREATER_THAN_UINT32(0u, static_cast<uint32_t>(oracle.rows.size()));
  TEST_ASSERT_TRUE_MESSAGE(differentialTracesEqual(oracle, candidate),
      "the prepared pair must emit the same ordered stream as the synchronous path");
}

/// Negative control -- the comparison must actually detect a different pair, otherwise every
/// green differential above is meaningless.
void test_differential_harness_detects_a_perturbed_pair() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  LoopPlaybackRuntime& runtime = TrackCaptureStopTestAccess::runtime(track);
  SessionMidiEventVec committed;
  committed.push_back(MidiEvent::NoteOn(0, 1, 48, 100));
  committed.push_back(MidiEvent::NoteOff(48, 1, 48, 0));
  committed.push_back(MidiEvent::NoteOn(384, 1, 55, 90));
  committed.push_back(MidiEvent::NoteOff(432, 1, 55, 0));
  seedPreparedPair(loop, runtime, committed);
  DifferentialTrace oracle;
  consumePairOverRange(loop, runtime, 0, 768, oracle);
  // Perturb the candidate pair: drop the last order row, so the tail event cannot be emitted.
  PlaybackOrderVec& order = loop.getPlaybackOrder();
  TEST_ASSERT_GREATER_THAN_UINT32(1u, static_cast<uint32_t>(order.size()));
  order.pop_back();
  DifferentialTrace perturbed;
  consumePairOverRange(loop, runtime, 0, 768, perturbed);
  TEST_ASSERT_FALSE_MESSAGE(differentialTracesEqual(oracle, perturbed),
      "the harness must detect a perturbed pair");
}

/// Fixture 2 (owner priority #1) -- an overdub seal admitted into the merged window. This is the
/// transition that invalidated literal request+hold: `tryAdmitLastOverdubSealIntoMergedEvents`
/// appends the sealed pass's events and sets `playbackOrderDirty`, so the pair's members are from
/// different generations until the order is rebuilt. The oracle (full replacement) publishes the
/// sealed rows; continuing to consume the stale pair loses them. Slice 2's atomic pair publication
/// is what makes the losing case unreachable; this pins the boundary.
void test_seal_admitted_into_pair_requires_atomic_replacement() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  LoopPlaybackRuntime& runtime = TrackCaptureStopTestAccess::runtime(track);
  SessionMidiEventVec committed;
  committed.push_back(MidiEvent::NoteOn(0, 1, 48, 100));
  committed.push_back(MidiEvent::NoteOff(48, 1, 48, 0));
  committed.push_back(MidiEvent::NoteOn(384, 1, 55, 90));
  committed.push_back(MidiEvent::NoteOff(432, 1, 55, 0));
  seedPreparedPair(loop, runtime, committed);
  DifferentialTrace before;
  consumePairOverRange(loop, runtime, 0, 192, before);

  // The seal admission: the window gains the sealed pass and the order is now stale.
  runtime.mergedMidiEvents.mergedEvents.push_back(MidiEvent::NoteOn(240, 1, 60, 80));
  runtime.mergedMidiEvents.mergedEvents.push_back(MidiEvent::NoteOff(288, 1, 60, 0));
  loop.playbackOrderDirty = true;
  TEST_ASSERT_TRUE(loop.playbackOrderDirty);

  // Stale pair: consumed without replacement, so the sealed rows cannot reach the output.
  DifferentialTrace stale;
  consumePairOverRange(loop, runtime, 192, 384, stale);
  bool stale_has_sealed = false;
  for (const DifferentialRow& row : stale.rows) {
    if (row.note == 60) stale_has_sealed = true;
  }
  TEST_ASSERT_FALSE_MESSAGE(stale_has_sealed,
      "a stale pair cannot emit the newly admitted seal -- this is why replacement is required");

  // Full replacement (the oracle): rebuild the order + reanchor, then consume the same range.
  const SessionMidiEventVec replacedEvents = runtime.mergedMidiEvents.mergedEvents;
  seedPreparedPair(loop, runtime, replacedEvents);
  DifferentialTrace replaced;
  consumePairOverRange(loop, runtime, 192, 384, replaced);
  bool replaced_has_sealed = false;
  for (const DifferentialRow& row : replaced.rows) {
    if (row.note == 60) replaced_has_sealed = true;
  }
  TEST_ASSERT_TRUE_MESSAGE(replaced_has_sealed,
      "the replacement pair must publish the admitted seal (no accepted audio lost)");
  TEST_ASSERT_FALSE_MESSAGE(differentialTracesEqual(stale, replaced),
      "the transition must be observable as a stream difference before slice 2 makes it atomic");
}

/// Fixture 3 (owner priority #2) -- same-tick NoteOff/NoteOn ordering. The pair's order rebuild
/// carries the equal-phase Off-before-On key; a prepared pair must preserve it exactly.
void test_prepared_pair_preserves_same_tick_off_before_on_order() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  LoopPlaybackRuntime& runtime = TrackCaptureStopTestAccess::runtime(track);
  SessionMidiEventVec committed;
  // Closing Off and a new On share tick 192.
  committed.push_back(MidiEvent::NoteOn(0, 1, 48, 100));
  committed.push_back(MidiEvent::NoteOff(192, 1, 48, 0));
  committed.push_back(MidiEvent::NoteOn(192, 1, 55, 90));
  committed.push_back(MidiEvent::NoteOff(336, 1, 55, 0));
  seedPreparedPair(loop, runtime, committed);
  DifferentialTrace trace;
  consumePairOverRange(loop, runtime, 0, 384, trace);
  size_t off_index = trace.rows.size();
  size_t on_index = trace.rows.size();
  for (size_t i = 0; i < trace.rows.size(); ++i) {
    const DifferentialRow& row = trace.rows[i];
    if (row.tick != 192) continue;
    const bool is_off = row.type == static_cast<uint8_t>(midi::NoteOff);
    const bool is_on = row.type == static_cast<uint8_t>(midi::NoteOn);
    if (is_off && row.note == 48 && off_index == trace.rows.size()) off_index = i;
    if (is_on && row.note == 55 && on_index == trace.rows.size()) on_index = i;
  }
  TEST_ASSERT_NOT_EQUAL(trace.rows.size(), off_index);
  TEST_ASSERT_NOT_EQUAL(trace.rows.size(), on_index);
  TEST_ASSERT_TRUE_MESSAGE(off_index < on_index,
      "a same-tick NoteOff must precede its NoteOn in the prepared pair");
}

/// Fixture 4 (owner priority #3) -- the reanchor case. The anchor/cursor seed is part of the
/// pair's identity: a pair anchored at a mid-loop playhead must not re-emit what is already behind
/// it, and two pairs anchored differently must not produce the same stream.
void test_prepared_pair_anchor_is_part_of_its_identity() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  LoopPlaybackRuntime& runtime = TrackCaptureStopTestAccess::runtime(track);
  SessionMidiEventVec committed;
  committed.push_back(MidiEvent::NoteOn(0, 1, 48, 100));
  committed.push_back(MidiEvent::NoteOff(48, 1, 48, 0));
  committed.push_back(MidiEvent::NoteOn(384, 1, 55, 90));
  committed.push_back(MidiEvent::NoteOff(432, 1, 55, 0));
  // P0 playing: the cursor is ahead of tick 0, so nothing before it may be emitted again.
  seedPreparedPair(loop, runtime, committed);
  loop.lastTickInLoop = 192;
  ProjectionContext ctx{};
  ctx.loopLength = loop.loopLengthTicks;
  reanchorPlaybackIndex(loop, runtime.mergedMidiEvents.mergedEvents, loop.getPlaybackOrder(), ctx);
  DifferentialTrace anchored;
  consumePairOverRange(loop, runtime, 192, 448, anchored);
  TEST_ASSERT_GREATER_THAN_UINT32(0u, static_cast<uint32_t>(anchored.rows.size()));
  for (const DifferentialRow& row : anchored.rows) {
    TEST_ASSERT_TRUE_MESSAGE(row.tick > 192,
        "a pair anchored at 192 must not re-emit the events behind its anchor");
  }
  // A pair anchored at the loop start is a different pair: consumed from tick 0 it also emits
  // the rows the anchored pair had already passed, so the anchor seed is part of the identity.
  loop.nextEventIndex = 0;
  DifferentialTrace fromStart;
  consumePairOverRange(loop, runtime, 0, 448, fromStart);
  TEST_ASSERT_TRUE_MESSAGE(fromStart.rows.size() > anchored.rows.size(),
      "an unanchored pair emits from the loop start; the anchor seed is part of the pair");
}

// ---------------------------------------------------- 2.4 slice 2: prepared pair
// The pinned invariant: playback never observes `mergedEvents` and `playbackOrder` from
// different generations; a pair becomes consumable only when both members and their
// cursor/reanchor state are complete for the same source generation.

/// The off-clock owner publishes one coherent pair from the working window, and the clock
/// path's consume decision reads it without building anything. Coherence is structural:
/// the descriptor's order indexes its own event buffer, which is not the working window.
void test_prepared_pair_publishes_off_clock_and_the_consume_decision_never_builds() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  LoopPlaybackRuntime& runtime = TrackCaptureStopTestAccess::runtime(track);
  SessionMidiEventVec committed;
  committed.push_back(MidiEvent::NoteOn(0, 1, 48, 100));
  committed.push_back(MidiEvent::NoteOff(48, 1, 48, 0));
  committed.push_back(MidiEvent::NoteOn(384, 1, 55, 90));
  committed.push_back(MidiEvent::NoteOff(432, 1, 55, 0));
  seedPreparedPair(loop, runtime, committed);
  TEST_ASSERT_FALSE(runtime.preparedPair.ready);

  Track::resetPlaybackOrderHotRebuildCount();
  track.ensurePlaybackMergedEventsForSlot(0);
  const Track::PreparedPairMetrics published = track.preparedPairMetrics(0);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, published.publishCount,
      "the off-clock owner publishes exactly one pair");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(4u, static_cast<uint32_t>(published.publishedEventCount),
      "the published pair carries the whole coherent window");
  TEST_ASSERT_TRUE(runtime.preparedPair.ready);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, Track::playbackOrderHotRebuildCount(),
      "off-clock publication is not a clock-path content-sized build");
  // Structural coherence: the pair owns its event buffer, and its order indexes only that
  // buffer, so no consumer can pair these events with another generation's order.
  const FixedPlaybackEventStorage& pairStorage = *runtime.preparedPair.storage;
  TEST_ASSERT_TRUE(pairStorage.events != runtime.mergedMidiEvents.mergedEvents.data());
  TEST_ASSERT_TRUE(pairStorage.order != nullptr);
  for (size_t index = 0; index < runtime.preparedPair.eventCount; ++index) {
    TEST_ASSERT_TRUE(pairStorage.order[index] < runtime.preparedPair.eventCount);
  }

  const PreparedPairConsumeDecision decision = resolvePreparedPairConsume(
      runtime, loop, loop.playbackRevision, TrackCaptureStopTestAccess::generation(track),
      loop.publishedSnapshotIdentity(), false, 96);
  TEST_ASSERT_TRUE(decision.pair == &runtime.preparedPair);
  TEST_ASSERT_FALSE(decision.requested);
  TEST_ASSERT_FALSE(decision.held);
  const Track::PreparedPairMetrics afterConsume = track.preparedPairMetrics(0);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, afterConsume.requestCount,
      "a current pair is consumed; no request is raised");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, Track::playbackOrderHotRebuildCount(),
      "consuming a current pair is a read, never a build");
}

/// A source change requests a replacement and the clock path keeps consuming the previous
/// coherent pair (P0) until the off-clock owner publishes P1. Nothing builds on the clock
/// path, and the measured hold/latency is compared against the audibility edge.
void test_source_change_requests_a_replacement_and_holds_the_last_coherent_pair() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  LoopPlaybackRuntime& runtime = TrackCaptureStopTestAccess::runtime(track);
  SessionMidiEventVec committed;
  committed.push_back(MidiEvent::NoteOn(0, 1, 48, 100));
  committed.push_back(MidiEvent::NoteOff(48, 1, 48, 0));
  committed.push_back(MidiEvent::NoteOn(384, 1, 55, 90));
  committed.push_back(MidiEvent::NoteOff(432, 1, 55, 0));
  seedPreparedPair(loop, runtime, committed);
  loop.lastTickInLoop = 0;
  track.ensurePlaybackMergedEventsForSlot(0);
  TEST_ASSERT_EQUAL_UINT32(1u, track.preparedPairMetrics(0).publishCount);
  const uint32_t p0Generation = runtime.preparedPair.generation;
  const size_t p0EventCount = runtime.preparedPair.eventCount;

  // Source change: a new canonical revision plus the sealed row the replacement must carry.
  // The working window is what the off-clock gather would leave behind; its order is stale
  // until the satisfier rebuilds it.
  TrackCaptureStopTestAccess::setPlaybackRevision(
      track, TrackCaptureStopTestAccess::playbackRevision(track) + 1);
  runtime.mergedMidiEvents.mergedEvents.push_back(MidiEvent::NoteOn(240, 1, 60, 80));
  runtime.mergedMidiEvents.mergedEvents.push_back(MidiEvent::NoteOff(288, 1, 60, 0));
  runtime.mergedMidiEvents.builtFromRevision = loop.playbackRevision;
  runtime.mergedMidiEvents.builtFromSnapshotIdentity = loop.publishedSnapshotIdentity();
  loop.playbackOrderDirty = true;

  const uint32_t gen = TrackCaptureStopTestAccess::generation(track);
  const uint32_t requestTick = 96;
  Track::resetPlaybackOrderHotRebuildCount();
  for (uint32_t tick = requestTick + 1; tick <= requestTick + 8; ++tick) {
    const PreparedPairConsumeDecision decision = resolvePreparedPairConsume(
        runtime, loop, loop.playbackRevision, gen, loop.publishedSnapshotIdentity(), false, tick);
    TEST_ASSERT_TRUE_MESSAGE(decision.requested, "the source change raises a replacement request");
    TEST_ASSERT_TRUE_MESSAGE(decision.held, "the tick keeps consuming the previous coherent pair");
    TEST_ASSERT_TRUE(decision.pair == &runtime.preparedPair);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(p0Generation, decision.pair->generation,
        "the held pair is still P0 until the replacement is published");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(p0EventCount),
        static_cast<uint32_t>(decision.pair->eventCount),
        "holding P0 never exposes the partially prepared replacement window");
    runtime.notePreparedPairHoldTick();
  }
  const Track::PreparedPairMetrics held = track.preparedPairMetrics(0);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, held.requestCount,
      "one request per source generation, not one per tick");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(8u, held.holdTicks, "every waiting tick is a held tick");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, Track::playbackOrderHotRebuildCount(),
      "request-and-hold must not build on the clock path");

  // The off-clock owner satisfies the request, rebuilding the order and publishing P1.
  const uint32_t publishTick = requestTick + 8;
  clockManager.assignCurrentTickSilently(publishTick);
  TEST_ASSERT_TRUE(track.servicePendingPreparedPlaybackPair());
  const Track::PreparedPairMetrics satisfied = track.preparedPairMetrics(0);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u, satisfied.publishCount, "the replacement is published");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(publishTick - (requestTick + 1), satisfied.maxLatencyTicks,
      "measured request tick -> publish tick latency, in clock ticks");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(p0EventCount + 2),
      satisfied.rebuildUnits - static_cast<uint32_t>(p0EventCount),
      "rebuild units are the replacement window's rows, reported for the slice-3 decision");
  TEST_ASSERT_TRUE(runtime.preparedPair.generation > p0Generation);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(p0EventCount + 2),
      static_cast<uint32_t>(runtime.preparedPair.eventCount),
      "the replacement pair carries the admitted row");
  // The detector counts *operations*, not owners: the tick contributed zero (asserted
  // above), and the one counted build is the off-clock owner's order rebuild. That single
  // per-source-change build, not a per-tick one, is the quantity slice 3 (b) would make
  // incremental -- and `prepared_pair_rebuild_units` records the rows it costs.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, Track::playbackOrderHotRebuildCount(),
      "exactly one content-sized build: the off-clock owner's replacement order");

  // Audibility edge: the hold is a fraction of the one-bar maintenance budget, so the
  // replacement lands long before the next quantized edge that must make the sealed row
  // audible. This is the measurement that decides against slice 3 (b).
  TEST_ASSERT_TRUE_MESSAGE(satisfied.holdTicks < static_cast<uint32_t>(Config::TICKS_PER_BAR / 8),
      "the hold must be a small fraction of one bar, not the one-bar maintenance budget");
  TEST_ASSERT_TRUE_MESSAGE(satisfied.maxLatencyTicks < static_cast<uint32_t>(Config::TICKS_PER_BAR),
      "the replacement must publish strictly inside the one-bar maintenance budget");

  // Consuming P1 is again a pure read, and its cursor seed is part of the pair.
  const uint32_t buildsBeforeP1Consume = Track::playbackOrderHotRebuildCount();
  const PreparedPairConsumeDecision after = resolvePreparedPairConsume(
      runtime, loop, loop.playbackRevision, gen, loop.publishedSnapshotIdentity(), false,
      requestTick + 9);
  TEST_ASSERT_TRUE(after.pair == &runtime.preparedPair);
  TEST_ASSERT_FALSE(after.requested);
  TEST_ASSERT_FALSE(after.held);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, after.pair->cursorSeed,
      "the seed is the first row after the publication phase, resolved inside publication");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(buildsBeforeP1Consume, Track::playbackOrderHotRebuildCount(),
      "consuming the replacement built nothing");
}

/// The published pair emits byte-identical rows to the synchronous oracle over the same
/// admitted window, so atomic publication does not lose, duplicate or reorder audio.
void test_published_pair_emits_the_synchronous_oracle_trace_after_a_source_change() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  LoopPlaybackRuntime& runtime = TrackCaptureStopTestAccess::runtime(track);
  SessionMidiEventVec committed;
  committed.push_back(MidiEvent::NoteOn(0, 1, 48, 100));
  committed.push_back(MidiEvent::NoteOff(192, 1, 48, 0));
  committed.push_back(MidiEvent::NoteOn(192, 1, 55, 90));
  committed.push_back(MidiEvent::NoteOff(336, 1, 55, 0));
  seedPreparedPair(loop, runtime, committed);
  loop.lastTickInLoop = 0;
  track.ensurePlaybackMergedEventsForSlot(0);

  // Source change that admits one more row (the seal-admission shape), then satisfy it
  // off-clock exactly as the production owner does.
  TrackCaptureStopTestAccess::setPlaybackRevision(
      track, TrackCaptureStopTestAccess::playbackRevision(track) + 1);
  runtime.mergedMidiEvents.mergedEvents.push_back(MidiEvent::NoteOn(96, 1, 60, 77));
  runtime.mergedMidiEvents.mergedEvents.push_back(MidiEvent::NoteOff(144, 1, 60, 0));
  runtime.mergedMidiEvents.builtFromRevision = loop.playbackRevision;
  runtime.mergedMidiEvents.builtFromSnapshotIdentity = loop.publishedSnapshotIdentity();
  loop.playbackOrderDirty = true;
  clockManager.assignCurrentTickSilently(480);
  // The clock path raises the request; the off-clock owner is what satisfies it.
  const PreparedPairConsumeDecision requested = resolvePreparedPairConsume(
      runtime, loop, loop.playbackRevision, TrackCaptureStopTestAccess::generation(track),
      loop.publishedSnapshotIdentity(), false, 480);
  TEST_ASSERT_TRUE(requested.requested);
  TEST_ASSERT_TRUE(track.servicePendingPreparedPlaybackPair());

  // ORACLE: the synchronous merged window + order, consumed from the loop start.
  Track::resetPlaybackOrderHotRebuildCount();
  loop.nextEventIndex = 0;
  DifferentialTrace oracle;
  consumePairOverRange(loop, runtime, 0, 768, oracle);
  // CANDIDATE: the published immutable pair through its own production stream adapter.
  DifferentialTrace candidate;
  consumePublishedPairOverRange(loop, runtime, 0, 768, candidate);
  TEST_ASSERT_GREATER_THAN_UINT32(0u, static_cast<uint32_t>(oracle.rows.size()));
  TEST_ASSERT_TRUE_MESSAGE(differentialTracesEqual(oracle, candidate),
      "the published pair must emit the same ordered stream as the synchronous oracle");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, Track::playbackOrderHotRebuildCount(),
      "consuming the published pair never rebuilds an order");
  bool candidate_has_admitted = false;
  for (const DifferentialRow& row : candidate.rows) {
    if (row.note == 60) candidate_has_admitted = true;
  }
  TEST_ASSERT_TRUE_MESSAGE(candidate_has_admitted,
      "the replacement pair must make the admitted row audible (no accepted audio lost)");
}

/// The one-shot overdub-seal admit receipt is itself a source-change signal: it forces the
/// request/hold decision even before the gather makes the revision visible to the cache.
void test_seal_admission_signal_requests_the_pair_replacement() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  LoopPlaybackRuntime& runtime = TrackCaptureStopTestAccess::runtime(track);
  SessionMidiEventVec committed;
  committed.push_back(MidiEvent::NoteOn(0, 1, 48, 100));
  committed.push_back(MidiEvent::NoteOff(48, 1, 48, 0));
  seedPreparedPair(loop, runtime, committed);
  track.ensurePlaybackMergedEventsForSlot(0);
  TEST_ASSERT_FALSE(loop.hasPendingOverdubSealAdmission());

  loop.recordLastOverdubSeal(loop.lastCommittedPassId(), EditPassIdList{},
                             loop.playbackRevision);
  TEST_ASSERT_TRUE(loop.hasPendingOverdubSealAdmission());
  Track::resetPlaybackOrderHotRebuildCount();
  const PreparedPairConsumeDecision decision = resolvePreparedPairConsume(
      runtime, loop, loop.playbackRevision, TrackCaptureStopTestAccess::generation(track),
      loop.publishedSnapshotIdentity(), loop.hasPendingOverdubSealAdmission(), 100);
  TEST_ASSERT_TRUE_MESSAGE(decision.requested,
      "a pending seal admit receipt is a source change even at the same revision");
  TEST_ASSERT_TRUE(decision.held);
  TEST_ASSERT_TRUE(decision.pair == &runtime.preparedPair);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, Track::playbackOrderHotRebuildCount(),
      "the seal signal must request, never rebuild in place");
}

/// 2.5a gathering fixture (owner priority #4) -- wrap boundary and a note at the loop edge, gathered
/// from a real published snapshot root. The pipeline clamps the edge note's Off to `loopLength - 1`
/// (so it stays inside the window), and a wrap note becomes a head plus a tail with the same NoteId;
/// the pair must emit exactly what the synchronous path emits for both.
void test_gathered_pair_matches_the_synchronous_path_across_a_wrap_and_a_loop_edge_note() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  LoopPlaybackRuntime& runtime = TrackCaptureStopTestAccess::runtime(track);
  const uint32_t loopLength = loop.loopLengthTicks;
  const std::vector<FixtureSnapshotNote> notes{
      {48, 24, 72},           // plain note (after phase 0: the harness starts at tick 1)
      {55, 700, loopLength},  // loop-edge note: its Off is clamped to the last tick
      {60, 700, 20},          // wrap note: snapshot `endTick < startTick` form
  };
  buildFixtureRoot(loop, notes);
  loop.lastTickInLoop = 0;
  track.ensurePlaybackMergedEventsForSlot(0);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, track.preparedPairMetrics(0).publishCount,
      "the real published-snapshot gather must publish a pair");
  TEST_ASSERT_TRUE(runtime.preparedPair.ready);
  TEST_ASSERT_TRUE_MESSAGE(runtime.preparedPair.storage != nullptr &&
                               runtime.preparedPair.eventCount > 0,
      "the gathered window must carry the root's rows");

  Track::resetPlaybackOrderHotRebuildCount();
  DifferentialTrace oracle;
  DifferentialTrace candidate;
  assertPairMatchesSynchronousPathFromAnchor(loop, runtime, loopLength, oracle, candidate);
  TEST_ASSERT_GREATER_THAN_UINT32(0u, static_cast<uint32_t>(oracle.rows.size()));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, Track::playbackOrderHotRebuildCount(),
      "consuming the gathered pair never rebuilds an order");
  bool plainOn = false;
  bool edgeOn = false;
  bool edgeOffAtLastTick = false;
  bool wrapHeadOn = false;
  for (const DifferentialRow& row : candidate.rows) {
    if (row.type == static_cast<uint8_t>(midi::NoteOn) && row.note == 48) plainOn = true;
    if (row.type == static_cast<uint8_t>(midi::NoteOn) && row.note == 55) edgeOn = true;
    if (row.type == static_cast<uint8_t>(midi::NoteOff) && row.note == 55 &&
        row.tick == loopLength - 1) {
      edgeOffAtLastTick = true;
    }
    if (row.type == static_cast<uint8_t>(midi::NoteOn) && row.note == 60) wrapHeadOn = true;
  }
  TEST_ASSERT_TRUE_MESSAGE(plainOn, "the plain note must be audible through the pair");
  TEST_ASSERT_TRUE_MESSAGE(edgeOn, "the loop-edge note's On must be audible");
  TEST_ASSERT_TRUE_MESSAGE(edgeOffAtLastTick,
      "the loop-edge note's Off must be clamped to the last tick and stay audible");
  TEST_ASSERT_TRUE_MESSAGE(wrapHeadOn, "the wrap note's head must be audible through the pair");
}

/// 2.5a gathering fixture (owner priority #5) -- a source change mid-window. A new root published at
/// the next revision raises a request; the tick keeps consuming the previous coherent pair, the
/// off-clock owner gathers the new root, and the replacement pair's rows equal the synchronous path's.
void test_gathered_pair_holds_then_replaces_atomically_when_the_root_changes_mid_window() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  LoopPlaybackRuntime& runtime = TrackCaptureStopTestAccess::runtime(track);
  buildFixtureRoot(loop, {{48, 0, 200}, {55, 300, 500}});
  loop.lastTickInLoop = 0;
  track.ensurePlaybackMergedEventsForSlot(0);
  TEST_ASSERT_EQUAL_UINT32(1u, track.preparedPairMetrics(0).publishCount);
  const uint64_t p0Generation = runtime.preparedPair.generation;
  const size_t p0EventCount = runtime.preparedPair.eventCount;
  TEST_ASSERT_GREATER_THAN_UINT32(0u, static_cast<uint32_t>(p0EventCount));

  // The publication pipeline lands a new root at the next revision (a canonical commit).
  TrackCaptureStopTestAccess::setPlaybackRevision(
      track, TrackCaptureStopTestAccess::playbackRevision(track) + 1);
  buildFixtureRoot(loop, {{48, 0, 200}, {55, 300, 500}, {60, 100, 150}});
  const uint32_t gen = TrackCaptureStopTestAccess::generation(track);

  Track::resetPlaybackOrderHotRebuildCount();
  const PreparedPairConsumeDecision held = resolvePreparedPairConsume(
      runtime, loop, loop.playbackRevision, gen, loop.publishedSnapshotIdentity(), false, 96);
  TEST_ASSERT_TRUE_MESSAGE(held.requested, "the new root raises a replacement request");
  TEST_ASSERT_TRUE_MESSAGE(held.held, "the tick keeps consuming the previous coherent pair");
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(p0Generation, held.pair->generation,
      "the held pair is still the pre-change pair");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(p0EventCount),
      static_cast<uint32_t>(held.pair->eventCount),
      "holding never exposes the partially prepared replacement");
  runtime.notePreparedPairHoldTick();
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, Track::playbackOrderHotRebuildCount(),
      "the hold builds nothing on the clock path");

  clockManager.assignCurrentTickSilently(104);
  TEST_ASSERT_TRUE(track.servicePendingPreparedPlaybackPair());
  const Track::PreparedPairMetrics satisfied = track.preparedPairMetrics(0);
  TEST_ASSERT_EQUAL_UINT32(2u, satisfied.publishCount);
  TEST_ASSERT_TRUE(runtime.preparedPair.generation > p0Generation);

  DifferentialTrace oracle;
  DifferentialTrace candidate;
  assertPairMatchesSynchronousPathFromAnchor(loop, runtime, loop.loopLengthTicks, oracle, candidate);
  bool addedRowAudible = false;
  for (const DifferentialRow& row : candidate.rows) {
    if (row.note == 60) addedRowAudible = true;
  }
  TEST_ASSERT_TRUE_MESSAGE(addedRowAudible,
      "the replacement root's added row must become audible (no accepted audio lost)");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, satisfied.holdTicks,
      "one waiting tick consumed the previous pair");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(8u, satisfied.maxLatencyTicks,
      "the replacement published one owner turn (8 ticks) after the request");
}

/// 2.5a gathering fixture (owner priority #6) -- the long-loop window boundary. A windowed long-loop
/// gather is deliberately not a full-loop pair (recorded as its own slice), so no pair is published;
/// the windowed production stream must still match an independent phase-sorted reference built from
/// the public full-loop gather restricted to the same window.
void test_long_loop_window_is_not_a_pair_and_matches_an_independent_window_reference() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  LoopPlaybackRuntime& runtime = TrackCaptureStopTestAccess::runtime(track);
  const uint32_t longLength = 17u * Config::TICKS_PER_BAR;
  loop.loopLengthTicks = longLength;
  TEST_ASSERT_TRUE_MESSAGE(loop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks),
      "the fixture loop must take the windowed gather path");
  const uint32_t fifthBar = 5u * Config::TICKS_PER_BAR;
  buildFixtureRoot(loop, {{48, 0, 48},
                          {55, fifthBar, fifthBar + 48},
                          {60, longLength - 96, longLength - 48}});
  const uint32_t playhead = fifthBar + 24;
  loop.lastTickInLoop = playhead;
  clockManager.assignCurrentTickSilently(playhead);
  track.ensurePlaybackMergedEventsForSlot(0);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, track.preparedPairMetrics(0).publishCount,
      "a windowed long-loop gather is not a full-loop pair (documented boundary)");
  TEST_ASSERT_FALSE(runtime.preparedPair.ready);

  const uint32_t winStart = runtime.mergedMidiEvents.windowStartTick;
  const uint32_t winLength = runtime.mergedMidiEvents.windowLengthTicks;
  TEST_ASSERT_TRUE(winLength > 0);
  TEST_ASSERT_TRUE_MESSAGE(winStart <= playhead && playhead < winStart + winLength,
      "the windowed gather must cover the playhead");
  TEST_ASSERT_GREATER_THAN_UINT32(0u,
      static_cast<uint32_t>(runtime.mergedMidiEvents.mergedEvents.size()));

  // Independent reference: the public full-loop gather, restricted by the consumption range and
  // ordered by the test's own phase/tick/Off-before-On key.
  SessionMidiEventVec fullGather;
  Loop::SnapshotWindowGatherPath gatherPath = Loop::SnapshotWindowGatherPath::None;
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(fullGather, 0, longLength,
                                                                 &gatherPath));
  TEST_ASSERT_EQUAL(Loop::SnapshotWindowGatherPath::Snapshot, gatherPath);
  std::vector<uint16_t> referenceOrder;
  buildReferenceEmissionOrder(fullGather, longLength, referenceOrder);

  DifferentialTrace windowed;
  loop.nextEventIndex = 0;
  consumePairOverRange(loop, runtime, winStart, winStart + winLength, windowed);
  DifferentialTrace reference;
  consumeReferenceOverRange(loop, fullGather, referenceOrder, winStart, winStart + winLength,
                            reference);
  TEST_ASSERT_GREATER_THAN_UINT32(0u, static_cast<uint32_t>(windowed.rows.size()));
  TEST_ASSERT_TRUE_MESSAGE(differentialTracesEqual(windowed, reference),
      "the windowed stream must equal the independently ordered full-gather window");
  bool windowNoteAudible = false;
  for (const DifferentialRow& row : windowed.rows) {
    if (row.note == 55) windowNoteAudible = true;
  }
  TEST_ASSERT_TRUE_MESSAGE(windowNoteAudible, "the playhead's bar must be inside the window");
}

/// 2.5a gathering fixture (owner priority #7) -- the record-stop publication gap. With no published
/// root the gather misses and no pair may be published (a pair would be an empty authority); once the
/// session-exit root lands, the owner turn publishes the pair and it matches the synchronous path.
void test_gathered_pair_waits_for_the_record_stop_root_then_publishes() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  LoopPlaybackRuntime& runtime = TrackCaptureStopTestAccess::runtime(track);
  TEST_ASSERT_NULL_MESSAGE(loop.publishedSnapshotIdentity(),
      "the fixture must start inside the record-stop publication gap");
  loop.lastTickInLoop = 0;
  clockManager.assignCurrentTickSilently(0);
  track.ensurePlaybackMergedEventsForSlot(0);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, track.preparedPairMetrics(0).publishCount,
      "an empty gather must not publish a pair");
  TEST_ASSERT_FALSE(runtime.preparedPair.ready);
  TEST_ASSERT_TRUE(runtime.mergedMidiEvents.mergedEvents.empty());
  TEST_ASSERT_FALSE(track.hasPendingPreparedPlaybackPairRequest());

  // The record-stop handoff publishes the root; the clock path requests and the owner turn publishes.
  buildFixtureRoot(loop, {{48, 0, 200}, {55, 300, 500}});
  const uint32_t gen = TrackCaptureStopTestAccess::generation(track);
  const PreparedPairConsumeDecision requested = resolvePreparedPairConsume(
      runtime, loop, loop.playbackRevision, gen, loop.publishedSnapshotIdentity(), false, 96);
  TEST_ASSERT_TRUE(requested.requested);
  TEST_ASSERT_NULL_MESSAGE(requested.pair,
      "with no published pair yet the tick takes the cold merged fallback");
  TEST_ASSERT_TRUE(track.hasPendingPreparedPlaybackPairRequest());

  clockManager.assignCurrentTickSilently(104);
  TEST_ASSERT_TRUE(track.servicePendingPreparedPlaybackPair());
  TEST_ASSERT_EQUAL_UINT32(1u, track.preparedPairMetrics(0).publishCount);
  TEST_ASSERT_TRUE(runtime.preparedPair.ready);

  DifferentialTrace oracle;
  DifferentialTrace candidate;
  assertPairMatchesSynchronousPathFromAnchor(loop, runtime, loop.loopLengthTicks, oracle, candidate);
  TEST_ASSERT_GREATER_THAN_UINT32(0u, static_cast<uint32_t>(candidate.rows.size()));
}

/// OpenSpec 2.4 (parent `resumable-midi-boundary-playback-maintenance`): the content-sized
/// playback-order build is instrumented, so a synchronous fallback on the active clock path
/// cannot land silently. This native suite has no MIDI-output harness, so the detector is
/// proven directly: a build that runs while a track is timing-critical is counted, and the
/// STOPPED-oracle build is not. When the tick's request/consume conversion lands, the active
/// route must leave this counter at zero.
void test_active_transport_playback_order_rebuild_is_instrumented() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  track.forceSetState(TRACK_PLAYING);
  TEST_ASSERT_TRUE(track.isAudiblePlaybackActive());
  SessionMidiEventVec merged;
  merged.push_back(MidiEvent::NoteOn(0, 1, 48, 100));
  merged.push_back(MidiEvent::NoteOff(48, 1, 48, 0));
  const ProjectionContext ctx = makePlaybackContext(track, loop, 96);
  Track::resetPlaybackOrderHotRebuildCount();
  rebuildPlaybackOrder(loop, merged, ctx);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, Track::playbackOrderHotRebuildCount(),
      "a content-sized order build during active transport must be counted");
  reanchorPlaybackIndex(loop, merged, loop.getPlaybackOrder(), ctx);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u, Track::playbackOrderHotRebuildCount(),
      "the paired full reanchor must be counted too");
}

void test_stopped_oracle_playback_order_build_is_not_a_hot_rebuild() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  track.forceSetState(TRACK_STOPPED);
  TEST_ASSERT_FALSE(track.isAudiblePlaybackActive());
  SessionMidiEventVec merged;
  merged.push_back(MidiEvent::NoteOn(0, 1, 48, 100));
  const ProjectionContext ctx = makePlaybackContext(track, loop, 0);
  Track::resetPlaybackOrderHotRebuildCount();
  // The complete in-place builder is retained as the STOPPED-state oracle (task 2.4).
  rebuildPlaybackOrder(loop, merged, ctx);
  reanchorPlaybackIndex(loop, merged, loop.getPlaybackOrder(), ctx);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, Track::playbackOrderHotRebuildCount(),
      "the STOPPED oracle build is not an active-transport rebuild");
}

void test_adversarial_reentry_at_a_canonical_checkpoint() {
  runAdversarialMidGrainReentry(static_cast<uint8_t>(PendingStopPhase::CanonicalOrder),
                                "paused in canonical ordering");
}

/// 5.5's missing "after the raw-range stage" checkpoint: the predecessor's raw range is sealed and
/// its canonical order is built, and it is rewriting that order into the canonical copy when the
/// successor starts. No staged pass exists yet, so this checkpoint proves the successor's capture
/// start does not disturb the frozen canonical claim either.
void test_adversarial_reentry_at_a_canonical_rewrite_checkpoint() {
  runAdversarialMidGrainReentry(static_cast<uint8_t>(PendingStopPhase::CanonicalRewrite),
                                "paused in canonical rewrite");
}

void test_adversarial_reentry_at_a_companion_checkpoint() {
  runAdversarialMidGrainReentry(static_cast<uint8_t>(PendingStopPhase::CompanionHistory),
                                "paused in companion history");
}

/// The D8 ownership boundary as an adversarial pause: the successor starts on the exact turn the
/// predecessor first owns its staged pass. This is the state a successor's `beginCapture` used to
/// destroy, and it cannot be named by a phase threshold (see the helper's comment).
void test_adversarial_reentry_at_the_staged_pass_ownership_checkpoint() {
  // The nominal phase is the boundary this checkpoint represents; the flag selects the
  // ownership-based pause, because `PrepareTransfer` is not observable as a resting phase.
  runAdversarialMidGrainReentry(static_cast<uint8_t>(PendingStopPhase::PrepareTransfer),
                                "paused on first staged-pass ownership",
                                /*pauseAtStagedPassOwnership=*/true);
}

void test_adversarial_reentry_at_the_publication_checkpoint() {
  runAdversarialMidGrainReentry(static_cast<uint8_t>(PendingStopPhase::AtomicTransfer),
                                "paused at the atomic transfer");
}

void test_stop_job_owns_held_notes_and_closes_original_slot() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, UINT32_MAX);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  auto& job = TrackCaptureStopTestAccess::job(track);
  TEST_ASSERT_EQUAL(1, job.pendingNotes.size());
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::liveNotes(track).empty());
  TEST_ASSERT_EQUAL(144, job.phaseTick);
  Loop& original = track.getLoop(0);
  Loop& other = track.getLoop(1);
  other.loopLengthTicks = 384;
  other.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(other.appendCaptureEvent(MidiEvent::NoteOn(11, 1, 72, 90)));
  // Inject a newer ingress map and selection independently of the guarded
  // public reentry path. The worker must use its receipt, never this selection.
  TrackCaptureStopTestAccess::liveNotes(track)[{60, 1}] = PendingNote{60, 1, 193, 100};
  TrackCaptureStopTestAccess::selectSlot(track, 1);
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::step(track));
  TEST_ASSERT_EQUAL(0, job.admission.remainingClosureGrains);
  // The captured NoteOn belongs to the job-owned sealed extent. The reserved
  // terminal Off is appended afterwards to the successor producer extent, which
  // is why the readable capture interval exposes both sources.
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::ownsRawRange(track));
  const SessionMidiEventVec sealedRows =
      sealedRangeRows(TrackCaptureStopTestAccess::rawRange(track));
  TEST_ASSERT_EQUAL(1, sealedRows.size());
  TEST_ASSERT_TRUE(sealedRows[0].isNoteOn());
  TEST_ASSERT_EQUAL(97, sealedRows[0].tick);
  TEST_ASSERT_EQUAL(1, original.capture.store.size());
  TEST_ASSERT_TRUE(original.capture.store.at(0).isNoteOff());
  TEST_ASSERT_EQUAL(144, original.capture.store.at(0).tick);
  TEST_ASSERT_EQUAL(1, other.capture.store.size());
  TEST_ASSERT_EQUAL(11, other.capture.store.at(0).tick);
  TEST_ASSERT_EQUAL(193, TrackCaptureStopTestAccess::liveNotes(track).at({60, 1}).startNoteTick);
}

void test_stop_job_denies_changed_geometry_before_closure_mutation() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, UINT32_MAX);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  auto& job = TrackCaptureStopTestAccess::job(track);
  Loop& original = track.getLoop(0);
  original.loopLengthTicks = 384;
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::step(track));
  // Denial mutates neither the job-owned sealed extent nor the producer.
  TEST_ASSERT_EQUAL(1, TrackCaptureStopTestAccess::rawRange(track).size());
  TEST_ASSERT_EQUAL(0, original.capture.store.size());
  TEST_ASSERT_EQUAL(1, job.admission.remainingClosureGrains);
  TEST_ASSERT_EQUAL(1, job.pendingNotes.size());
  original.loopLengthTicks = 768;
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::step(track));
  TEST_ASSERT_EQUAL(1, original.capture.store.size());
  TEST_ASSERT_EQUAL(0, job.admission.remainingClosureGrains);
}

void test_empty_stop_runs_existing_worker_to_completion() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  for (unsigned i = 0; i < 32 && track.hasPendingDeferredOverdubStop(); ++i)
    TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_TRUE(track.isPlaying());
  TEST_ASSERT_NULL(TrackCaptureStopTestAccess::job(track).owner);
}

void test_completed_job_cannot_transition_a_newer_track_interval() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  // State/receipt isolation only: actual reentry is still guarded until Loop
  // session, raw storage and staging are migrated. Do not claim a live B here.
  TrackCaptureStopTestAccess::advanceInterval(track);
  TrackCaptureStopTestAccess::liveNotes(track)[{60, 1}] = PendingNote{60, 1, 193, 100};
  track.forceSetState(TRACK_OVERDUBBING);
  for (unsigned i = 0; i < 32 && track.hasPendingDeferredOverdubStop(); ++i)
    TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_TRUE(track.isOverdubbing());
  TEST_ASSERT_EQUAL(193, TrackCaptureStopTestAccess::liveNotes(track).at({60, 1}).startNoteTick);
}

void test_populated_stop_runs_existing_worker_to_completion() {
  for (bool held : {false, true}) {
    if (held) setUp();
    Track& track = preparePlayingTrack();
    clockManager.assignCurrentTickSilently(96);
    midiButtonActions.handleToggleRecord();
    inputNote(60, 97, held ? UINT32_MAX : 120);
    clockManager.assignCurrentTickSilently(144);
    midiButtonActions.handleToggleRecord();
    for (unsigned i = 0; i < 10000 && track.hasPendingDeferredOverdubStop(); ++i)
      TrackCaptureStopTestAccess::step(track);
    TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
    TEST_ASSERT_TRUE(track.isPlaying());
    TEST_ASSERT_NULL(TrackCaptureStopTestAccess::job(track).owner);
    TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::job(track).pendingNotes.empty());
    TEST_ASSERT_EQUAL(1, track.getActiveLoop().passes.overdubPasses.size());
  }
}

void test_job_note_retirement_does_not_clear_live_notes() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, UINT32_MAX);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  auto& job = TrackCaptureStopTestAccess::job(track);
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::step(track));
  // Isolate the post-commit node-retirement grain. Do not pretend this phase
  // injection proves canonical publication; the empty-stop test runs that path.
  job.admission.phase = PendingStopPhase::DeferredCleanup;
  TrackCaptureStopTestAccess::liveNotes(track)[{60, 1}] = PendingNote{60, 1, 193, 100};
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::step(track));
  TEST_ASSERT_TRUE(job.pendingNotes.empty());
  TEST_ASSERT_EQUAL(193, TrackCaptureStopTestAccess::liveNotes(track).at({60, 1}).startNoteTick);
  TEST_ASSERT_TRUE(job.admission.pending);
}

void test_stop_copies_canonical_rows_without_mutating_sealed_raw_input() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  for (unsigned i = 0; i < 300; ++i)
    TEST_ASSERT_TRUE(track.recordMidiEvents(midi::ControlChange, 1, 7, i % 128, 300 - i));
  auto& loop = track.getActiveLoop();
  const auto rawChunk = loop.capture.store.chunkIds().front();
  TEST_ASSERT_EQUAL(ChunkLifecycleState::Sealed, LoopEventStore::chunkLifecycleState(rawChunk));
  clockManager.assignCurrentTickSilently(600);
  midiButtonActions.handleToggleRecord();
  auto& job = TrackCaptureStopTestAccess::job(track);
  auto& runtime = TrackCaptureStopTestAccess::runtime(track);
  for (unsigned i = 0; i < 10000 && job.admission.phase != PendingStopPhase::CanonicalRewrite; ++i)
    TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL(PendingStopPhase::CanonicalRewrite, job.admission.phase);
  TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL(32, runtime.canonicalCaptureOutput.store.size());
  TEST_ASSERT_EQUAL(1, runtime.canonicalCaptureOutput.store.at(0).tick);
  // Ownership transferred at punch-out: the job owns the sealed 300-row extent
  // and the producer holds only the reserved successor directory.
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::ownsRawRange(track));
  TEST_ASSERT_EQUAL(300, TrackCaptureStopTestAccess::rawRange(track).size());
  const SessionMidiEventVec sealedRows =
      sealedRangeRows(TrackCaptureStopTestAccess::rawRange(track));
  TEST_ASSERT_EQUAL(300, sealedRows.size());
  // The sealed extent preserves raw append order (descending ticks here); the
  // canonical copy is what imposes ascending order.
  TEST_ASSERT_EQUAL(300, sealedRows[0].tick);
  TEST_ASSERT_EQUAL(1, sealedRows[299].tick);
  TEST_ASSERT_TRUE(loop.capture.store.empty());
  TEST_ASSERT_NOT_EQUAL(rawChunk, runtime.canonicalCaptureOutput.store.chunkIds().front());
  // The sealed pool chunk is still byte-identical: the canonical copy wrote to
  // different physical chunks and never rewrote the job-owned extent.
  MidiEvent raw; bool end = true;
  TEST_ASSERT_TRUE(LoopEventStore::readSealedChunkEvent(rawChunk, 0, raw, end));
  TEST_ASSERT_EQUAL(300, raw.tick);
  for (unsigned i = 0; i < 10000 && track.hasPendingDeferredOverdubStop(); ++i)
    TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_TRUE(runtime.canonicalCaptureOutputAdmitted());
  TEST_ASSERT_EQUAL(1, loop.passes.overdubPasses.size());
  SessionMidiEventVec canonical;
  LoopEventStore::appendChunkRefEvents(loop.passes.overdubPasses.back().committedChunkIds, canonical);
  TEST_ASSERT_EQUAL(300, canonical.size());
  for (unsigned i = 0; i < 300; ++i) TEST_ASSERT_EQUAL(i + 1, canonical[i].tick);
}

void test_transport_stop_during_canonical_copy_preserves_accepted_rows() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  for (unsigned i = 0; i < 300; ++i)
    TEST_ASSERT_TRUE(track.recordMidiEvents(midi::ControlChange, 1, 7, i % 128, 300 - i));
  clockManager.assignCurrentTickSilently(600);
  midiButtonActions.handleToggleRecord();
  auto& job = TrackCaptureStopTestAccess::job(track);
  auto& runtime = TrackCaptureStopTestAccess::runtime(track);
  for (unsigned i = 0; i < 10000 && job.admission.phase != PendingStopPhase::CanonicalRewrite; ++i)
    TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL(PendingStopPhase::CanonicalRewrite, job.admission.phase);
  TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL(32, runtime.canonicalCaptureOutput.store.size());
  const auto epoch = job.admission.transportCommandEpoch;
  track.stopOverdubbingToStopped();
  TEST_ASSERT_EQUAL(PendingStopDestination::Stopped, job.admission.destination);
  TEST_ASSERT_NOT_EQUAL(epoch, job.admission.transportCommandEpoch);
  TEST_ASSERT_EQUAL(32, runtime.canonicalCaptureOutput.store.size());
  for (unsigned i = 0; i < 10000 && job.admission.phase != PendingStopPhase::DeferredCleanup; ++i)
    TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL(PendingStopPhase::DeferredCleanup, job.admission.phase);
  TEST_ASSERT_EQUAL(PendingStopDestination::Stopped, job.admission.destination);
  TEST_ASSERT_TRUE(runtime.canonicalCaptureOutput.store.empty());
  auto& loop = track.getActiveLoop();
  TEST_ASSERT_EQUAL(1, loop.passes.overdubPasses.size());
  SessionMidiEventVec rows;
  LoopEventStore::appendChunkRefEvents(loop.passes.overdubPasses.back().committedChunkIds, rows);
  TEST_ASSERT_EQUAL(300, rows.size());
  for (unsigned i = 0; i < 300; ++i) TEST_ASSERT_EQUAL(i + 1, rows[i].tick);
}

void test_playback_reset_preserves_partial_copy_until_stopped_recovery() {
  for (unsigned route = 0; route < 2; ++route) {
    if (route) setUp();
    Track& track = preparePlayingTrack();
    clockManager.assignCurrentTickSilently(96);
    midiButtonActions.handleToggleRecord();
    for (unsigned i = 0; i < 300; ++i)
      TEST_ASSERT_TRUE(track.recordMidiEvents(midi::ControlChange, 1, 7, i % 128, 300 - i));
    clockManager.assignCurrentTickSilently(600);
    midiButtonActions.handleToggleRecord();
    auto& job = TrackCaptureStopTestAccess::job(track);
    auto& runtime = TrackCaptureStopTestAccess::runtime(track);
    TEST_ASSERT_EQUAL_PTR(&job.admission, runtime.captureStopOwner);
    for (unsigned i = 0; i < 10000 && job.admission.phase != PendingStopPhase::CanonicalRewrite; ++i)
      TrackCaptureStopTestAccess::step(track);
    TEST_ASSERT_EQUAL(PendingStopPhase::CanonicalRewrite, job.admission.phase);
    TrackCaptureStopTestAccess::step(track);
    TEST_ASSERT_EQUAL(32, runtime.canonicalCaptureOutput.store.size());
    const auto generation = TrackCaptureStopTestAccess::generation(track);
    if (route == 0) TEST_ASSERT_FALSE(runtime.reset(false));
    else TrackCaptureStopTestAccess::invalidate(track);
    TEST_ASSERT_EQUAL(generation, TrackCaptureStopTestAccess::generation(track));
    TEST_ASSERT_EQUAL(PendingStopDestination::Stopped, job.admission.destination);
    TEST_ASSERT_FALSE(job.admission.permitsPlaybackEmission());
    TEST_ASSERT_EQUAL(32, runtime.canonicalCaptureOutput.store.size());
    TEST_ASSERT_EQUAL(300, runtime.captureStagingIfPrewarmed()->storage.eventCount);
    TEST_ASSERT_TRUE(runtime.captureStagingIfPrewarmed()->frozen);
    const auto epoch = job.admission.transportCommandEpoch;
    TEST_ASSERT_FALSE(runtime.reset(true));
    TEST_ASSERT_EQUAL(epoch, job.admission.transportCommandEpoch);
    // Isolate job cleanup from the physical silence boundary in this fixture.
    // This does not claim that a newer live interval can already be admitted.
    TrackCaptureStopTestAccess::advanceInterval(track);
    for (unsigned i = 0; i < 10000 && track.hasPendingDeferredOverdubStop(); ++i)
      TrackCaptureStopTestAccess::step(track);
    TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
    TEST_ASSERT_NULL(runtime.captureStopOwner);
    auto& loop = track.getActiveLoop();
    TEST_ASSERT_EQUAL(1, loop.passes.overdubPasses.size());
    SessionMidiEventVec rows;
    LoopEventStore::appendChunkRefEvents(loop.passes.overdubPasses.back().committedChunkIds, rows);
    TEST_ASSERT_EQUAL(300, rows.size());
    for (unsigned i = 0; i < 300; ++i) TEST_ASSERT_EQUAL(i + 1, rows[i].tick);
    TEST_ASSERT_TRUE(runtime.reset(false));
  }
}

void test_cache_reset_at_each_stop_phase_keeps_accepted_capture() {
  const PendingStopPhase phases[] = {
      PendingStopPhase::ReservedClosure, PendingStopPhase::CanonicalPrefix,
      PendingStopPhase::CanonicalOrder, PendingStopPhase::CanonicalRewrite,
      PendingStopPhase::AudibleOrder, PendingStopPhase::PrepareTransfer,
      PendingStopPhase::CanonicalTail, PendingStopPhase::CompanionHistory,
      PendingStopPhase::CompanionBridge, PendingStopPhase::AtomicTransfer,
      PendingStopPhase::DeferredCleanup};
  for (size_t phaseIndex = 0; phaseIndex < sizeof(phases) / sizeof(phases[0]); ++phaseIndex) {
    if (phaseIndex) setUp();
    Track& track = preparePlayingTrack();
    clockManager.assignCurrentTickSilently(96);
    midiButtonActions.handleToggleRecord();
    for (unsigned i = 0; i < 300; ++i)
      TEST_ASSERT_TRUE(track.recordMidiEvents(midi::ControlChange, 1, 7, i % 128, 300 - i));
    inputNote(65, 97, UINT32_MAX);
    clockManager.assignCurrentTickSilently(600);
    midiButtonActions.handleToggleRecord();
    auto& job = TrackCaptureStopTestAccess::job(track);
    auto& runtime = TrackCaptureStopTestAccess::runtime(track);
    for (unsigned i = 0; i < 20000 && job.admission.phase != phases[phaseIndex]; ++i)
      TrackCaptureStopTestAccess::step(track);
    TEST_ASSERT_EQUAL(phases[phaseIndex], job.admission.phase);
    TEST_ASSERT_FALSE(runtime.reset(false));
    TEST_ASSERT_EQUAL(PendingStopDestination::Stopped, job.admission.destination);
    TEST_ASSERT_FALSE(job.admission.permitsPlaybackEmission());
    // Keep physical output outside this native fixture, as in the partial-copy test.
    TrackCaptureStopTestAccess::advanceInterval(track);
    for (unsigned i = 0; i < 20000 && track.hasPendingDeferredOverdubStop(); ++i)
      TrackCaptureStopTestAccess::step(track);
    TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
    TEST_ASSERT_NULL(runtime.captureStopOwner);
    auto& loop = track.getActiveLoop();
    TEST_ASSERT_EQUAL(1, loop.passes.overdubPasses.size());
    SessionMidiEventVec rows;
    LoopEventStore::appendChunkRefEvents(loop.passes.overdubPasses.back().committedChunkIds, rows);
    TEST_ASSERT_EQUAL(302, rows.size());
    unsigned ons = 0, offs = 0, controls = 0;
    for (const auto& event : rows) {
      if (event.isNoteOn()) { ++ons; TEST_ASSERT_EQUAL(97, event.tick); }
      else if (event.isNoteOff()) { ++offs; TEST_ASSERT_EQUAL(600, event.tick); }
      else ++controls;
    }
    TEST_ASSERT_EQUAL(1, ons);
    TEST_ASSERT_EQUAL(1, offs);
    TEST_ASSERT_EQUAL(300, controls);
    TEST_ASSERT_TRUE(runtime.reset(false));
  }
}

void test_overdub_shortens_record_pass_note_and_completes_stop() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(0);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.isOverdubbing());
  inputNote(48, 24, 72);
  auto& loop = track.getActiveLoop();
  TEST_ASSERT_TRUE(loop.hasPendingNoteChanges());
  clockManager.assignCurrentTickSilently(100);
  midiButtonActions.handleToggleRecord();
  for (unsigned i = 0; i < 20000 && track.hasPendingDeferredOverdubStop(); ++i)
    TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_TRUE(track.isPlaying());
  TEST_ASSERT_EQUAL(1, loop.passes.overdubPasses.size());
  SessionMidiEventVec rows;
  loop.passes.materializeToEventVector(rows, loop.loopLengthTicks);
  unsigned onCount = 0, offCount = 0;
  bool shortened = false;
  for (const auto& event : rows) {
    if (event.isNoteOn()) ++onCount;
    if (event.isNoteOff()) { ++offCount; shortened |= event.tick == 23; }
  }
  TEST_ASSERT_EQUAL(2, onCount);
  TEST_ASSERT_EQUAL(2, offCount);
  TEST_ASSERT_TRUE(shortened);
}

void test_stop_descriptor_rotation_preserves_worker_borrow_and_reuses_retired_slots() {
  Track& track = preparePlayingTrack();
  const void* first = &TrackCaptureStopTestAccess::job(track);
  for (size_t i = 0; i <= TrackCaptureStopTestAccess::queueCapacity(); ++i) {
    clockManager.assignCurrentTickSilently(96);
    midiButtonActions.handleToggleRecord();
    TEST_ASSERT_TRUE(track.isOverdubbing());
    auto* receipt = &TrackCaptureStopTestAccess::job(track).admission;
    if (i == TrackCaptureStopTestAccess::queueCapacity())
      TEST_ASSERT_EQUAL_PTR(first, &TrackCaptureStopTestAccess::job(track));
    clockManager.assignCurrentTickSilently(144);
    midiButtonActions.handleToggleRecord();
    TEST_ASSERT_EQUAL(1, TrackCaptureStopTestAccess::queueSize(track));
    TEST_ASSERT_EQUAL_PTR(receipt, TrackCaptureStopTestAccess::runtime(track).captureStopOwner);
    for (unsigned grain = 0; grain < 10000 && track.hasPendingDeferredOverdubStop(); ++grain)
      TrackCaptureStopTestAccess::step(track);
    TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
    TEST_ASSERT_EQUAL(0, TrackCaptureStopTestAccess::queueSize(track));
    TEST_ASSERT_NULL(TrackCaptureStopTestAccess::runtime(track).captureStopOwner);
    TEST_ASSERT_TRUE(track.isPlaying());
    // This test covers sequential descriptor reuse, not concurrent reentry.
    // The existing session handoff must finish before the next ordinary start.
    auto& loop = track.getActiveLoop();
    for (unsigned grain = 0; grain < 10000 && loop.hasPendingSnapshotPublish(); ++grain)
      loop.stepPendingSnapshotPublish(1);
    TEST_ASSERT_TRUE(loop.canHandoffSessionExitSnapshot());
  }
}

void test_missing_stop_descriptors_rejects_before_opening_capture() {
  Track& track = preparePlayingTrack();
  TrackCaptureStopTestAccess::discardColdQueue(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.isPlaying());
  TEST_ASSERT_FALSE(track.getActiveLoop().captureActive());
  inputNote(60, 97, 120);
  TEST_ASSERT_TRUE(track.getActiveLoop().capture.store.empty());
}

/// Task 2.3 FIFO binding: the ring is the admission authority. A new interval
/// always takes the tail descriptor, the worker resolves one stable head for the
/// whole grain, and a full ring is an explicit denial that consumes no accepted
/// input or live state.
void test_stop_ring_admits_at_tail_and_binds_worker_to_head() {
  Track& track = preparePlayingTrack();
  // Empty ring: no head, and the next admission target is the entry at head.
  TEST_ASSERT_NULL(TrackCaptureStopTestAccess::headPtr(track));
  TEST_ASSERT_EQUAL_PTR(TrackCaptureStopTestAccess::entryPtr(track, 0),
                        TrackCaptureStopTestAccess::tailPtr(track));

  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_EQUAL(1, TrackCaptureStopTestAccess::queueSize(track));
  // With one queued job the admitted tail descriptor is the head the worker
  // binds, and it stays pending across grains.
  TEST_ASSERT_EQUAL_PTR(TrackCaptureStopTestAccess::entryPtr(track, 0),
                        TrackCaptureStopTestAccess::headPtr(track));
  TEST_ASSERT_EQUAL_PTR(TrackCaptureStopTestAccess::headPtr(track),
                        &TrackCaptureStopTestAccess::job(track));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::job(track).admission.pending);
}

/// Seal transfers the exact admitted extent, and a successor interval on the
/// same loop may reuse the producer without altering the job's owned extent.
void test_seal_transfers_extent_and_successor_capture_preserves_it() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  inputNote(62, 100, 130);
  Loop& loop = track.getActiveLoop();
  TEST_ASSERT_EQUAL(4, loop.capture.store.size());
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::ownsRawRange(track));
  TEST_ASSERT_TRUE(loop.capture.store.empty());
  const auto& range = TrackCaptureStopTestAccess::rawRange(track);
  const SessionMidiEventVec before = sealedRangeRows(range);
  TEST_ASSERT_EQUAL(4, before.size());
  const uint64_t generation = range.generation();

  // Successor interval begins on the same loop (the guarded public reentry
  // route is deliberately bypassed). The predecessor extent must survive.
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(200, 1, 64, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(210, 1, 64, 0)));
  TEST_ASSERT_EQUAL(2, loop.capture.store.size());
  TEST_ASSERT_EQUAL_UINT64(generation, range.generation());
  const SessionMidiEventVec after = sealedRangeRows(range);
  TEST_ASSERT_EQUAL(before.size(), after.size());
  for (size_t i = 0; i < before.size(); ++i) {
    TEST_ASSERT_EQUAL(before[i].tick, after[i].tick);
    TEST_ASSERT_EQUAL(static_cast<int>(before[i].type), static_cast<int>(after[i].type));
    TEST_ASSERT_EQUAL(before[i].data.noteData.note, after[i].data.noteData.note);
  }
}

/// The readable capture interval yields the same effective rows before and after
/// sealing, and ordering the merge is scratch-only: a borrowed sealed extent is
/// never sorted or rewritten in place.
void test_read_source_yields_same_rows_before_and_after_sealing() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  // Deliberately unsorted append order: the merge must impose scratch order.
  inputNote(62, 130, 150);
  inputNote(64, 100, 120);
  Loop& loop = track.getActiveLoop();
  SessionMidiEventVec before;
  loop.copyEffectiveCommittedEventsWithCapture(before);
  TEST_ASSERT_TRUE(before.size() >= 4);

  clockManager.assignCurrentTickSilently(180);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::ownsRawRange(track));
  const SessionMidiEventVec rangeBefore =
      sealedRangeRows(TrackCaptureStopTestAccess::rawRange(track));

  SessionMidiEventVec after;
  loop.copyEffectiveCommittedEventsWithCapture(after);
  TEST_ASSERT_EQUAL(before.size(), after.size());
  for (size_t i = 0; i < before.size(); ++i) {
    TEST_ASSERT_EQUAL(before[i].tick, after[i].tick);
    TEST_ASSERT_EQUAL(static_cast<int>(before[i].type), static_cast<int>(after[i].type));
    TEST_ASSERT_EQUAL(before[i].data.noteData.note, after[i].data.noteData.note);
  }
  // The sealed extent is byte-identical after the merge: ordering used scratch.
  const SessionMidiEventVec rangeAfter =
      sealedRangeRows(TrackCaptureStopTestAccess::rawRange(track));
  TEST_ASSERT_EQUAL(rangeBefore.size(), rangeAfter.size());
  for (size_t i = 0; i < rangeBefore.size(); ++i) {
    TEST_ASSERT_EQUAL(rangeBefore[i].tick, rangeAfter[i].tick);
    TEST_ASSERT_EQUAL(static_cast<int>(rangeBefore[i].type), static_cast<int>(rangeAfter[i].type));
    TEST_ASSERT_EQUAL(rangeBefore[i].data.noteData.note, rangeAfter[i].data.noteData.note);
  }
}

/// A closure whose source NoteOn now lives only in the job-owned sealed extent
/// still resolves the original NoteId.
void test_sealed_extent_resolves_closure_note_id() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(65, 97, UINT32_MAX);
  Loop& loop = track.getActiveLoop();
  TEST_ASSERT_EQUAL(1, loop.capture.store.size());
  const NoteId capturedId = loop.capture.store.at(0).noteId;
  TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, capturedId);

  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::ownsRawRange(track));
  TEST_ASSERT_TRUE(loop.capture.store.empty());
  TEST_ASSERT_EQUAL(1, TrackCaptureStopTestAccess::rawRange(track).size());

  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::appendNoteOffAtPhase(track, 1, 65, 150));
  TEST_ASSERT_EQUAL(1, loop.capture.store.size());
  TEST_ASSERT_TRUE(loop.capture.store.at(0).isNoteOff());
  TEST_ASSERT_EQUAL(capturedId, loop.capture.store.at(0).noteId);
  // The sealed extent stays immutable and still holds exactly the captured On.
  TEST_ASSERT_EQUAL(1, TrackCaptureStopTestAccess::rawRange(track).size());
}

/// A stale job may not drop a successor job's read borrow.
void test_sealed_range_borrow_release_is_owner_identified() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  LoopEventStore producer;
  TEST_ASSERT_TRUE(producer.reserveCaptureTail());
  LoopEventStore::CaptureRange rangeA;
  LoopEventStore::CaptureRange rangeB;
  TEST_ASSERT_TRUE(rangeA.reserveTail());
  TEST_ASSERT_TRUE(rangeB.reserveTail());
  TEST_ASSERT_TRUE(producer.append(MidiEvent::NoteOn(10, 1, 60, 100)));
  TEST_ASSERT_TRUE(producer.sealCaptureRange(rangeA));
  TEST_ASSERT_TRUE(producer.sealCaptureRange(rangeB));

  const int ownerA = 1;
  const int ownerB = 2;
  TEST_ASSERT_TRUE(loop.borrowSealedCaptureRange(rangeA, &ownerA));
  TEST_ASSERT_TRUE(loop.borrowSealedCaptureRange(rangeB, &ownerB));
  TEST_ASSERT_FALSE(loop.releaseSealedCaptureRangeBorrow(&ownerA));
  TEST_ASSERT_TRUE(loop.hasSealedCaptureBorrow());
  TEST_ASSERT_EQUAL_PTR(&rangeB, loop.captureReadSource().finalizing);
  TEST_ASSERT_TRUE(loop.releaseSealedCaptureRangeBorrow(&ownerB));
  TEST_ASSERT_FALSE(loop.hasSealedCaptureBorrow());
}

/// Pending note changes (Add/Shorten/Hide intents) are moved into job ownership
/// at punch-out. The worker must resolve against that owned set, and read
/// consumers must keep seeing it through the job-identified borrow.
void test_stop_job_owns_pending_note_changes() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(0);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.isOverdubbing());
  inputNote(48, 24, 72);
  Loop& loop = track.getActiveLoop();
  TEST_ASSERT_TRUE(loop.hasPendingNoteChanges());
  const size_t changeCount = loop.pendingNoteChanges().size();
  TEST_ASSERT_TRUE(changeCount > 0);

  clockManager.assignCurrentTickSilently(100);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  auto& job = TrackCaptureStopTestAccess::job(track);
  // Ownership moved to the job; the readable set is the borrowed job copy.
  TEST_ASSERT_EQUAL(changeCount, job.pendingChanges.size());
  TEST_ASSERT_EQUAL_PTR(&job.pendingChanges, &loop.pendingNoteChanges());

  for (unsigned i = 0; i < 20000 && track.hasPendingDeferredOverdubStop(); ++i)
    TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  // The borrow is retired with the job, and the live session's own set — which
  // a successor interval would use — is independent of the job's.
  TEST_ASSERT_TRUE(&loop.pendingNoteChanges() != &job.pendingChanges);
  TEST_ASSERT_TRUE(loop.pendingNoteChanges().empty());
  TEST_ASSERT_EQUAL(changeCount, job.pendingChanges.size());
}

/// A stale job may not drop a successor job's pending-change borrow.
void test_pending_change_borrow_release_is_owner_identified() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  PendingNoteChangeVec changesA;
  PendingNoteChangeVec changesB;
  const int ownerA = 1;
  const int ownerB = 2;
  TEST_ASSERT_TRUE(loop.borrowPendingNoteChanges(changesA, &ownerA));
  TEST_ASSERT_TRUE(loop.borrowPendingNoteChanges(changesB, &ownerB));
  TEST_ASSERT_FALSE(loop.releasePendingNoteChangesBorrow(&ownerA));
  TEST_ASSERT_EQUAL_PTR(&changesB, &loop.pendingNoteChanges());
  TEST_ASSERT_TRUE(loop.releasePendingNoteChangesBorrow(&ownerB));
  const PendingNoteChangeVec* live = &loop.pendingNoteChanges();
  TEST_ASSERT_TRUE(live != &changesA);
  TEST_ASSERT_TRUE(live != &changesB);
}

/// The capture preview is moved into job ownership at punch-out, so the deferred
/// cleanup seals this interval's preview even if a successor replaces the live
/// one. Display readers keep seeing it through the job-identified borrow.
void test_stop_job_owns_capture_preview() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  Loop& loop = track.getActiveLoop();
  TEST_ASSERT_TRUE(loop.capturePreview.notes.size() > 0);
  const size_t previewRows = loop.capturePreview.notes.size();

  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  auto& job = TrackCaptureStopTestAccess::job(track);
  // Ownership moved to the job; the display view is the borrowed job preview.
  TEST_ASSERT_EQUAL(previewRows, job.preview.notes.size());
  TEST_ASSERT_EQUAL_PTR(&job.preview, &loop.capturePreviewView());
  TEST_ASSERT_EQUAL(previewRows, loop.capturePreviewView().notes.size());

  for (unsigned i = 0; i < 20000 && track.hasPendingDeferredOverdubStop(); ++i)
    TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  // The borrow is retired with the job; the live preview a successor would use is
  // a different buffer and the job's copy was consumed by the deferred seal.
  TEST_ASSERT_TRUE(&loop.capturePreviewView() != &job.preview);
  TEST_ASSERT_TRUE(job.preview.notes.empty());
}

/// A stale job may not drop a successor job's preview borrow.
void test_capture_preview_borrow_release_is_owner_identified() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  CapturePreview previewA;
  CapturePreview previewB;
  const int ownerA = 1;
  const int ownerB = 2;
  TEST_ASSERT_TRUE(loop.borrowCapturePreview(previewA, &ownerA));
  TEST_ASSERT_TRUE(loop.borrowCapturePreview(previewB, &ownerB));
  TEST_ASSERT_FALSE(loop.releaseCapturePreviewBorrow(&ownerA));
  TEST_ASSERT_EQUAL_PTR(&previewB, &loop.capturePreviewView());
  TEST_ASSERT_TRUE(loop.releaseCapturePreviewBorrow(&ownerB));
  const CapturePreview* live = &loop.capturePreviewView();
  TEST_ASSERT_TRUE(live != &previewA);
  TEST_ASSERT_TRUE(live != &previewB);
}

/// The overdub-session undo baseline (published root at session open) moves into
/// job ownership at punch-out, so a successor session's own baseline cannot
/// rewrite the before-snapshot this interval's OverdubPassAdded entry records.
void test_stop_job_owns_overdub_session_undo_baseline() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.isOverdubbing());
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::liveHasBeforePublished(track));

  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::jobHasBeforePublished(track));
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::liveHasBeforePublished(track));

  // A successor session records its own baseline; the job's stays intact.
  TrackCaptureStopTestAccess::setLiveBeforePublished(track, true);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::liveHasBeforePublished(track));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::jobHasBeforePublished(track));

  for (unsigned i = 0; i < 20000 && track.hasPendingDeferredOverdubStop(); ++i)
    TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  // Consumed by the undo entry or retired with the job; never carried forward.
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::jobHasBeforePublished(track));
}

/// The deferred stop collects the session's undo identities in bounded grains and
/// builds the OverdubPassAdded entry from the job's own slot and baseline, on the
/// prepared PLAYING route.
void test_deferred_stop_prepares_job_owned_undo_entry() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  auto& job = TrackCaptureStopTestAccess::job(track);
  TEST_ASSERT_FALSE(job.undoCollected);

  for (unsigned i = 0; i < 20000 && !job.undoCollected; ++i)
    TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_TRUE(job.undoCollectionStarted);
  TEST_ASSERT_TRUE_MESSAGE(job.undoCollected, "the deferred stop must build a prepared undo entry");
  TEST_ASSERT_TRUE(job.undoCollectable);
  TEST_ASSERT_EQUAL(static_cast<int>(UndoEntryKind::OverdubPassAdded),
                    static_cast<int>(job.undoEntry.kind));
  TEST_ASSERT_EQUAL_UINT8(job.slot, job.undoEntry.slotIndex);
  TEST_ASSERT_EQUAL_UINT32(job.loopId, job.undoEntry.loopId);
  TEST_ASSERT_NOT_EQUAL(kInvalidPassId, job.undoEntry.passId);
  // The entry's baseline is the job's own captured one (null is legitimate when
  // the session opened before any snapshot was published).
  TEST_ASSERT_EQUAL_PTR(job.beforePublished.get(), job.undoEntry.beforePublishedSnapshot.get());

  // Run the deferred stop to completion: the prepared entry must actually be
  // published as one OverdubPassAdded on the atomic commit, and the protected
  // transaction must return to Idle so a later interval can publish again.
  for (unsigned i = 0; i < 20000 && track.hasPendingDeferredOverdubStop(); ++i)
    TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  auto* publication = TrackCaptureStopTestAccess::undoPublication(track);
  TEST_ASSERT_NOT_NULL(publication);
  TEST_ASSERT_FALSE(publication->protectedState());
  const GlobalUndoStack& stack = TrackCaptureStopTestAccess::undoStack(track);
  size_t overdubEntries = 0;
  for (const UndoEntry& entry : stack.entries) {
    if (entry.kind == UndoEntryKind::OverdubPassAdded && entry.slotIndex == job.slot) ++overdubEntries;
  }
  TEST_ASSERT_EQUAL(1, overdubEntries);
}

/// The GUS records ONE `OverdubPassAdded` per overdub session, and it must carry every pass that
/// session committed -- a wrap pass plus the final stop pass. A single undo then removes the whole
/// overdub, not just its last pass.
void test_deferred_stop_undo_entry_carries_every_session_pass() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.isOverdubbing());
  inputNote(60, 97, 120);
  TEST_ASSERT_TRUE_MESSAGE(track.commitOverdubWrap(), "the first lap must commit a wrap pass");
  const PassId wrapPassId = track.getActiveLoop().lastCommittedPassId();
  TEST_ASSERT_NOT_EQUAL(kInvalidPassId, wrapPassId);
  inputNote(62, 121, 140);

  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  auto& job = TrackCaptureStopTestAccess::job(track);
  for (unsigned i = 0; i < 20000 && track.hasPendingDeferredOverdubStop(); ++i)
    TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());

  // One GUS entry for the session, carrying the wrap pass and the final stop pass.
  const GlobalUndoStack& stack = TrackCaptureStopTestAccess::undoStack(track);
  const UndoEntry* overdubEntry = nullptr;
  for (const UndoEntry& entry : stack.entries) {
    if (entry.kind == UndoEntryKind::OverdubPassAdded && entry.slotIndex == job.slot) {
      overdubEntry = &entry;
    }
  }
  TEST_ASSERT_NOT_NULL(overdubEntry);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u, overdubEntry->passIds.size(),
                                   "one entry must carry every pass of the session");
  TEST_ASSERT_EQUAL(wrapPassId, overdubEntry->passIds[0]);
  TEST_ASSERT_EQUAL(overdubEntry->passId, overdubEntry->passIds[1]);
  TEST_ASSERT_NOT_EQUAL(wrapPassId, overdubEntry->passId);
  // A single undo must disable both passes, so the whole overdub leaves the committed set.
  PassIdList undoIds;
  appendOverdubCapturePassIds(*overdubEntry, undoIds);
  TEST_ASSERT_EQUAL_UINT32(2u, undoIds.size());
  // `applyUndoEntry` disables every id `appendOverdubCapturePassIds` yields, and refuses the whole
  // entry if any id is missing. Both must therefore still be Active committed passes.
  for (const PassId id : undoIds) {
    bool found = false;
    for (const OverdubPass& pass : track.getActiveLoop().passes.overdubPasses) {
      if (pass.id == id) {
        found = true;
        TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(CapturePassState::Active),
                                  static_cast<int>(pass.state),
                                  "every pass in the entry must still be disableable");
      }
    }
    TEST_ASSERT_TRUE_MESSAGE(found, "every pass in the entry must still exist");
  }
}

/// The deferred terminal commit must name the committed pass's display bars. `notifyCommittedContentChanged`
/// alone leaves `dirtyBars` as the previous rebuild left it, and after a `slice_clean` that set is empty --
/// `rebuildVisualCacheIdleSlice` then takes its `slice_nodirty` early return and clears the stale flag without
/// rebuilding, so the OLED keeps serving pre-commit content until the next wrap marks bars (the reported
/// "second overdub disappeared after overdub stop and came back at the wrap").
void test_deferred_stop_commit_marks_the_display_ranges_of_the_new_pass() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  // The bug needs the precondition a settled display leaves behind: a non-empty cache that is NOT
  // dirty and has an empty dirty set (`slice_clean`). Seed that state directly so the test does not
  // depend on a snapshot drain.
  loop.visualCache.notes.clear();
  loop.visualCache.notes.push_back(NoteUtils::DisplayNote{});
  loop.visualCache.dirtyBars.clear();
  loop.visualCacheDirty = false;

  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.isOverdubbing());
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  for (unsigned i = 0; i < 20000 && track.hasPendingDeferredOverdubStop(); ++i)
    TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());

  TEST_ASSERT_TRUE_MESSAGE(loop.visualCacheDirty,
                           "a committed pass must leave the visual cache stale");
  TEST_ASSERT_FALSE_MESSAGE(loop.visualCache.dirtyBars.empty(),
                            "the committed pass must name the bars the rebuild may revisit");
}

/// Two overdub sessions must leave two `OverdubPassAdded` entries (plus the record's), and the top
/// entry is the second session's. A dropped second publication leaves the depth at 2 and makes the
/// next undo fall through to the first overdub.
void test_two_deferred_overdub_sessions_keep_both_undo_entries() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  GlobalUndoStack& stack = TrackCaptureStopTestAccess::undoStack(track);
  // The record entry `pushRecordPassAdded` would leave; the fixture seeds the loop directly.
  UndoEntry record;
  record.kind = UndoEntryKind::RecordPassAdded;
  record.slotIndex = 0;
  record.loopId = loop.loopId;
  record.passId = loop.passes.recordPass.id;
  record.id = stack.nextEntryId++;
  stack.entries.push_back(record);
  stack.cursor = stack.entries.size();
  TEST_ASSERT_TRUE_MESSAGE(stack.entries.capacity() >= 32,
                           "the protected undo prewarm must reserve the publication capacity");

  auto runSession = [&](uint32_t onTick) {
    clockManager.assignCurrentTickSilently(onTick);
    midiButtonActions.handleToggleRecord();
    TEST_ASSERT_TRUE(track.isOverdubbing());
    inputNote(60, onTick + 1, onTick + 24);
    clockManager.assignCurrentTickSilently(onTick + 48);
    midiButtonActions.handleToggleRecord();
    TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
    for (unsigned i = 0; i < 20000 && track.hasPendingDeferredOverdubStop(); ++i)
      TrackCaptureStopTestAccess::step(track);
    TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  };

  runSession(96);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u, stack.cursor, "one session must add one entry");
  const PassId firstOverdubPassId = loop.lastCommittedPassId();
  runSession(200);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      3u, stack.cursor, "the second overdub session must add its own GUS entry");
  TEST_ASSERT_EQUAL(static_cast<int>(UndoEntryKind::OverdubPassAdded),
                    static_cast<int>(stack.entries.back().kind));
  TEST_ASSERT_NOT_EQUAL(firstOverdubPassId, stack.entries.back().passId);
}

/// A full ring is explicit exhaustion, not finalization latency: admission is
/// denied, performs no seal/ownership transfer, and consumes no accepted input.
void test_full_stop_ring_denies_admission_and_preserves_accepted_capture() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.isOverdubbing());
  inputNote(60, 97, 120);
  TEST_ASSERT_EQUAL(2, track.getActiveLoop().capture.store.size());
  const size_t liveNotesBefore = TrackCaptureStopTestAccess::liveNotes(track).size();

  // Saturate the ring outside the guarded public reentry route. The ring, not a
  // staging-buffer side effect, is the admission authority.
  TrackCaptureStopTestAccess::setQueueCount(track, TrackCaptureStopTestAccess::queueCapacity());
  TEST_ASSERT_EQUAL(TrackCaptureStopTestAccess::queueCapacity(),
                    TrackCaptureStopTestAccess::queueSize(track));
  TEST_ASSERT_NULL(TrackCaptureStopTestAccess::tailPtr(track));
  TEST_ASSERT_NOT_NULL(TrackCaptureStopTestAccess::headPtr(track));

  TEST_ASSERT_FALSE(
      TrackCaptureStopTestAccess::admit(track, PendingStopDestination::Playing, 144));
  // A denied admission must not consume accepted input or live state, and it
  // performs no seal and no ownership transfer.
  TEST_ASSERT_TRUE(track.isOverdubbing());
  TEST_ASSERT_EQUAL(2, track.getActiveLoop().capture.store.size());
  TEST_ASSERT_EQUAL(liveNotesBefore, TrackCaptureStopTestAccess::liveNotes(track).size());
  TEST_ASSERT_EQUAL(TrackCaptureStopTestAccess::queueCapacity(),
                    TrackCaptureStopTestAccess::queueSize(track));
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::ownsRawRange(track));
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::rawRange(track).ready());
}

/// Task 2.3 staging ownership: an admitted stop job borrows the frozen admission
/// staging while live ingress owns a physically separate buffer. Punch-out
/// exchanges the two roles in constant work, so old-job completion can never
/// clear, reset or alias a successor interval's ingress rows.
void test_stop_job_and_live_ingress_own_distinct_staging() {
  Track& track = preparePlayingTrack();
  auto& runtime = TrackCaptureStopTestAccess::runtime(track);
  auto* liveRoleBefore = runtime.liveCaptureStagingIfPrewarmed();
  auto* frozenRoleBefore = runtime.captureStagingIfPrewarmed();
  TEST_ASSERT_NOT_NULL(liveRoleBefore);
  TEST_ASSERT_NOT_NULL(frozenRoleBefore);
  TEST_ASSERT_TRUE(liveRoleBefore->storage.events != frozenRoleBefore->storage.events);

  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());

  // The punch-out exchanged the roles: the job owns the rows it captured, and
  // the previous clean buffer is now live ingress ownership.
  auto* frozen = runtime.captureStagingIfPrewarmed();
  auto* live = runtime.liveCaptureStagingIfPrewarmed();
  TEST_ASSERT_NOT_NULL(frozen);
  TEST_ASSERT_NOT_NULL(live);
  TEST_ASSERT_EQUAL_PTR(liveRoleBefore, frozen);
  TEST_ASSERT_EQUAL_PTR(frozenRoleBefore, live);
  TEST_ASSERT_TRUE(frozen->frozen);
  TEST_ASSERT_EQUAL(2, frozen->storage.eventCount);
  TEST_ASSERT_FALSE(live->frozen);
  TEST_ASSERT_EQUAL(0, live->storage.eventCount);

  // A successor interval's accepted ingress rows must survive this job's
  // canonical work, commit and cleanup unchanged.
  TEST_ASSERT_TRUE(live->storage.append(MidiEvent::NoteOn(193, 1, 62, 100)));
  for (unsigned i = 0; i < 10000 && track.hasPendingDeferredOverdubStop(); ++i)
    TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_TRUE(track.isPlaying());
  TEST_ASSERT_EQUAL(1, live->storage.eventCount);
  TEST_ASSERT_EQUAL(193, live->storage.events[0].tick);
}

#include "off_path_deps.h"

/// Regression for the reset-before-guard defect: a redundant or ineligible
/// queueDeferredStoredMidiVerification() call must not cancel an already-queued dump.
/// Production reaches this with two overdub-stop finalize attempts (two
/// ODUB,stop,prefix_reuse lines); here the two queue attempts are direct.
void test_redundant_stored_verification_queue_keeps_pending_dump() {
  Track& track = preparePlayingTrack();
  track.setDeferredStoredVerificationArmedForTest(true);
  track.queueDeferredStoredMidiVerification();
  TEST_ASSERT_TRUE(track.deferredStoredVerificationPendingForTest());
  TEST_ASSERT_FALSE(track.deferredStoredVerificationArmedForTest());
  TEST_ASSERT_EQUAL_UINT32(1, track.deferredStoredVerificationOpportunityCountForTest());

  // Second call with the arm already consumed is ineligible and must be a no-op.
  track.queueDeferredStoredMidiVerification();
  TEST_ASSERT_TRUE_MESSAGE(track.deferredStoredVerificationPendingForTest(),
                           "ineligible queue must not clear a pending dump");
  TEST_ASSERT_EQUAL_UINT32(1, track.deferredStoredVerificationOpportunityCountForTest());
}

/// Ineligible because the loop is not representable (no committed pass): also a no-op.
void test_ineligible_stored_verification_queue_keeps_pending_dump() {
  Track& track = preparePlayingTrack();
  track.setDeferredStoredVerificationArmedForTest(true);
  track.queueDeferredStoredMidiVerification();
  TEST_ASSERT_TRUE(track.deferredStoredVerificationPendingForTest());

  track.setDeferredStoredVerificationArmedForTest(true);
  track.getActiveLoop().loopLengthTicks = 0;
  track.queueDeferredStoredMidiVerification();
  TEST_ASSERT_TRUE_MESSAGE(track.deferredStoredVerificationPendingForTest(),
                           "ineligible (unrepresentable) queue must not clear a pending dump");
  TEST_ASSERT_EQUAL_UINT32(1, track.deferredStoredVerificationOpportunityCountForTest());
}

/// DEC-069 defensive guard: `Track::commitOverdubWrap` may only be entered when no deferred job
/// owns the slot (the deferral gate in `maybeCommitOverdubWrap` is the primary owner of that
/// question), but the wrap must still not clobber a pending predecessor's canonical work if it is
/// ever reached with a job pending. It used to call `resumeCanonicalAfterAppend` -- whose
/// `resumeCurrentAfterAppend` resets the frozen capture binding -- and to discard the frozen
/// order/rewrite builders unconditionally. This pins the guard directly.
void test_successor_wrap_keeps_the_predecessors_frozen_builders() {
  Track& track = preparePlayingTrack();
  // A: deferred stop whose job owns the frozen role and binds the slot's canonical source.
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, UINT32_MAX);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  for (unsigned grain = 0;
       grain < 20000 &&
       TrackCaptureStopTestAccess::jobPhase(track) <
           static_cast<uint8_t>(PendingStopPhase::CanonicalTail);
       ++grain)
    (void)TrackCaptureStopTestAccess::step(track);
  for (unsigned grain = 0;
       grain < 64 && !TrackCaptureStopTestAccess::canonicalHasFrozenBinding(track); ++grain)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::canonicalHasFrozenBinding(track),
      "the predecessor must bind its frozen canonical source first");
  const uint64_t generationBefore = TrackCaptureStopTestAccess::frozenOrderStorageGeneration(track);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::canonicalOrderComplete(track));
  // B: successor interval with rows, then a direct commit attempt while A is still pending.
  clockManager.assignCurrentTickSilently(192);
  track.startOverdubbing(192);
  inputNote(64, 193, UINT32_MAX);
  inputNote(62, 200, 240);
  (void)track.commitOverdubWrap();
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      generationBefore, TrackCaptureStopTestAccess::frozenOrderStorageGeneration(track),
      "the wrap must not discard the pending predecessor's frozen builders");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::canonicalOrderComplete(track),
      "the predecessor's completed canonical order must survive the wrap");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::canonicalHasFrozenBinding(track),
      "the wrap must not reset the pending predecessor's frozen canonical source");
}

/// DEC-069 Option A: while a deferred stop job owns the slot's canonical revision, the live
/// interval's wrap records across loop end but seals no pass; the commit is owed to the first turn
/// after the job retires. Before this, the seal advanced `Loop::playbackRevision`, which
/// invalidated the job's frozen canonical source, so it never retired and this interval's own stop
/// was refused as occupied for the rest of the session.
void test_successor_wrap_defers_its_pass_commit_while_a_stop_job_is_pending() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  // A: deferred stop whose job owns the slot's canonical work (held note gives it closure work).
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, UINT32_MAX);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  const uint32_t revisionBefore = loop.playbackRevision;
  const size_t passesBefore = loop.passes.overdubPasses.size();
  // B: successor interval that crosses loop end while A is still pending.
  clockManager.assignCurrentTickSilently(192);
  track.startOverdubbing(192);
  inputNote(64, 193, UINT32_MAX);
  inputNote(62, 200, 240);
  TEST_ASSERT_FALSE_MESSAGE(track.maybeCommitOverdubWrap(700, 40),
      "the crossing must not seal a pass while a stop job owns the slot's revision");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::wrapCommitPending(track),
      "the deferred wrap commit must be owed, not dropped");
  TEST_ASSERT_EQUAL_UINT32(revisionBefore, loop.playbackRevision);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(passesBefore),
                           static_cast<uint32_t>(loop.passes.overdubPasses.size()));
  // A retires: nothing advanced the revision it pinned, so its own commit succeeds.
  for (unsigned grain = 0; grain < 20000 && track.hasPendingDeferredOverdubStop(); ++grain)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE_MESSAGE(track.hasPendingDeferredOverdubStop(),
      "the predecessor must retire when the successor wrap defers its pass commit");
  const size_t passesAfterPredecessor = loop.passes.overdubPasses.size();
  const uint32_t revisionAfterPredecessor = loop.playbackRevision;
  // The owed commit runs on the next ordinary playback turn and seals exactly one pass.
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::wrapCommitPending(track));
  TEST_ASSERT_TRUE(track.maybeCommitOverdubWrap(40, 41));
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::wrapCommitPending(track));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(passesAfterPredecessor + 1),
                           static_cast<uint32_t>(loop.passes.overdubPasses.size()));
  TEST_ASSERT_GREATER_THAN_UINT32(revisionAfterPredecessor, loop.playbackRevision);
}

/// DEC-070: a transport stop retargets a job that was admitted for PLAYING. Its PLAYING-only
/// preparation (canonical tail, companion history, undo collection, companion bridge) is bound to
/// the pre-stop transport command epoch, so continuing it parked the job forever -- holding
/// `captureStopOwner`, the frozen role and the session-exit receipt -- and every later stop was
/// refused until a reboot (`Overdub stop refused: stop_job_pending phase=7 dest=1`). The retarget
/// must re-route onto the STOPPED route's continuation and retire the job.
void test_transport_stop_retarget_retires_a_midflight_playing_job() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PendingStopDestination::Playing),
                          TrackCaptureStopTestAccess::jobDestination(track));
  // Drive the job past the transfer decision point into the PLAYING-only branch
  // (`CanonicalTail`..`CompanionBridge`); without a retarget it parks there.
  for (unsigned grain = 0; grain < 20000; ++grain) {
    const uint8_t phase = TrackCaptureStopTestAccess::jobPhase(track);
    if (phase >= static_cast<uint8_t>(PendingStopPhase::CanonicalTail) &&
        phase != static_cast<uint8_t>(PendingStopPhase::PrepareTransfer))
      break;
    (void)TrackCaptureStopTestAccess::step(track);
  }
  const uint8_t phaseBeforeStop = TrackCaptureStopTestAccess::jobPhase(track);
  TEST_ASSERT_TRUE_MESSAGE(
      phaseBeforeStop >= static_cast<uint8_t>(PendingStopPhase::CanonicalTail) &&
          phaseBeforeStop != static_cast<uint8_t>(PendingStopPhase::PrepareTransfer),
      "the job must reach the PLAYING-only branch before the transport stop");
  // Transport Stop while the job is mid-flight.
  TrackCaptureStopTestAccess::requestStoppedStop(track);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PendingStopDestination::Stopped),
                          TrackCaptureStopTestAccess::jobDestination(track));
  // A newer interval owns the live state (the device case: the transport stops while a successor is
  // live), so the retiring job must be job-local and must not drive the track's state transition.
  TrackCaptureStopTestAccess::advanceInterval(track);
  // The retarget keeps the interval's companion (note-edit) preparation: the display resolves the
  // overdubbed notes from it, so it must be *prepared and published*, not skipped. The job therefore
  // continues into the PLAYING-route companion phases instead of jumping straight to the transfer.
  (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<uint8_t>(PendingStopPhase::CanonicalTail),
      TrackCaptureStopTestAccess::jobPhase(track),
      "a retargeted job must keep preparing its companion representation");
  // Those phases cannot complete in this fixture (their playback-side inputs never run), so the
  // bounded fallback must take over and retire the job rather than park it.
  for (unsigned grain = 0; grain < 20000 && track.hasPendingDeferredOverdubStop(); ++grain)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE_MESSAGE(track.hasPendingDeferredOverdubStop(),
      "a transport-stop retarget must retire the mid-flight job, not park it");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::captureStopOwnerClear(track),
      "the retired job must release the stop owner");
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::canonicalOutputAdmitted(track));
  // And a later stop of a live interval is admitted again.
  clockManager.assignCurrentTickSilently(240);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.isOverdubbing());
  clockManager.assignCurrentTickSilently(288);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE_MESSAGE(track.hasPendingDeferredOverdubStop(),
      "the slot must accept a later stop once the retargeted job retired");
}

/// Openspec 5.3: the transport-stop ordering case with the explicit state under test. A deferred
/// job is admitted for PLAYING against root R0; the successor interval starts and accepts input
/// while the transport is still running; then Transport Stop arrives while A is still pending.
/// Three independent outcomes are asserted: (a) A retires exactly once on its own causal rules,
/// (b) B keeps its accepted input and reaches its own stop admission, and (c) no stale A-derived
/// window replaces the newer valid published root.
void test_transport_stop_while_successor_is_live_keeps_its_rows_and_root() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  // A deferred against R0 (the committed record pass's published root).
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  const void* const rootBefore = TrackCaptureStopTestAccess::publishedRootIdentity(track);
  // B's live interval with distinctive accepted input.
  clockManager.assignCurrentTickSilently(192);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.isOverdubbing());
  inputNote(62, 193, 216);
  const size_t bRows = loop.capture.store.size();
  TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(bRows));
  TEST_ASSERT_EQUAL(193, loop.capture.store.at(0).tick);
  TEST_ASSERT_EQUAL(216, loop.capture.store.at(1).tick);
  // Transport Stop while A is still pending: the mode change must not consume B's interval.
  TrackCaptureStopTestAccess::requestStoppedStop(track);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PendingStopDestination::Stopped),
                          TrackCaptureStopTestAccess::jobDestination(track));
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(),
      "the transport-stop retarget must not end the live successor interval");
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(bRows), static_cast<uint32_t>(loop.capture.store.size()));
  TEST_ASSERT_EQUAL(193, loop.capture.store.at(0).tick);
  TEST_ASSERT_EQUAL(216, loop.capture.store.at(1).tick);
  // (a) A resolves on its recorded rules and retires exactly once.
  for (unsigned grain = 0; grain < 20000 && track.hasPendingDeferredOverdubStop(); ++grain)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE_MESSAGE(track.hasPendingDeferredOverdubStop(),
      "the retargeted predecessor must retire exactly once, not park");
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::captureStopOwnerClear(track));
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(TrackCaptureStopTestAccess::queueSize(track)));
  // (b) B still owns its accepted rows and its own stop is admitted afterwards.
  TEST_ASSERT_TRUE(track.isOverdubbing());
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(bRows), static_cast<uint32_t>(loop.capture.store.size()));
  clockManager.assignCurrentTickSilently(288);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_FALSE(track.isOverdubbing());
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(TrackCaptureStopTestAccess::queueSize(track)));
  const SessionMidiEventVec bSealed = sealedRangeRows(TrackCaptureStopTestAccess::rawRange(track));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(bRows), static_cast<uint32_t>(bSealed.size()));
  TEST_ASSERT_EQUAL(193, bSealed.at(0).tick);
  TEST_ASSERT_EQUAL(216, bSealed.at(1).tick);
  // (c) No stale A-derived window replaces the newer valid root: the published root identity is
  // still the one B's interval resolved against (or a newer current root), never a regressed one.
  const void* const rootAfter = TrackCaptureStopTestAccess::publishedRootIdentity(track);
  if (rootAfter != nullptr && rootBefore != nullptr) {
    TEST_ASSERT_EQUAL_PTR_MESSAGE(rootBefore, rootAfter,
        "a retiring predecessor must not publish over the newer valid root");
  }
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(
      static_cast<uint32_t>(TrackCaptureStopTestAccess::publishedRootRevision(track)),
      static_cast<uint32_t>(TrackCaptureStopTestAccess::ownRevision(track)) > 0 ? 1u : 0u);
}

/// With the wrap commit deferred (DEC-069 Option A) a note held across loop end never loses its
/// staging reservation at all: the rows stay in the same unsealed pass, so the physical NoteOff
/// that arrives after the crossing closes the recorded note instead of being rejected as
/// `unmatched NoteOff`. Rejection left the note in the live pending set (the next NoteOn for that
/// pitch is dropped as a duplicate and every later off is rejected too) and the committed content
/// carried a NoteOn with no terminal off -- the reported "third overdub lengthens the note offs".
void test_successor_wrap_keeps_a_held_note_reservation_for_its_physical_note_off() {
  Track& track = preparePlayingTrack();
  Loop& loop = track.getActiveLoop();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  clockManager.assignCurrentTickSilently(192);
  track.startOverdubbing(192);
  inputNote(64, 193, UINT32_MAX);
  inputNote(62, 200, 240);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(TrackCaptureStopTestAccess::liveNotes(track).size()));
  TEST_ASSERT_GREATER_THAN_UINT32(0u, static_cast<uint32_t>(TrackCaptureStopTestAccess::liveReservations(track)));
  TEST_ASSERT_FALSE(track.maybeCommitOverdubWrap(700, 40));
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(
      0u, static_cast<uint32_t>(TrackCaptureStopTestAccess::liveReservations(track)),
      "a deferred wrap must not reset the held note's reservation");
  // The physical NoteOff arrives after the crossing and must be admitted.
  clockManager.assignCurrentTickSilently(768 + 250);
  midiHandler.handleMidiMessage(midi::NoteOff, 1, 64, 0, SOURCE_USB);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      0u, static_cast<uint32_t>(TrackCaptureStopTestAccess::liveNotes(track).size()),
      "the post-crossing NoteOff must be admitted, not rejected as unmatched");
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(TrackCaptureStopTestAccess::liveReservations(track)));
  // The pass still in the producer now carries the complete pair: a NoteOn and its terminal off,
  // with no dangling NoteOn for the resolver to lengthen into the previous overdub's content.
  const auto& capture = loop.capture.store;
  bool hasOn = false;
  bool hasOff = false;
  for (size_t i = 0; i < capture.size(); ++i) {
    const MidiEvent& evt = capture.at(i);
    if (evt.channel != 1 || evt.data.noteData.note != 64) continue;
    if (evt.isNoteOn()) hasOn = true;
    if (evt.isNoteOff()) hasOff = true;
  }
  TEST_ASSERT_TRUE_MESSAGE(hasOn && hasOff,
      "the deferred pass must carry the held note's on and its terminal off");
  // Once the predecessor retires, the owed commit seals that pass exactly once.
  for (unsigned grain = 0; grain < 20000 && track.hasPendingDeferredOverdubStop(); ++grain)
    (void)TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  const size_t passesAfterPredecessor = loop.passes.overdubPasses.size();
  TEST_ASSERT_TRUE(track.maybeCommitOverdubWrap(40, 41));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(passesAfterPredecessor + 1),
                           static_cast<uint32_t>(loop.passes.overdubPasses.size()));
}

/// The deferred overdub-stop completion is the normal commit route for a stop during
/// OVERDUBBING (both destinations share this completion). It must request the stored-MIDI
/// verification dump exactly like the synchronous `commitCaptureForStop(OverdubStop*, ...)`
/// fallback, otherwise the SEVT rows are never emitted (`sevt-diag-stop` capture had zero
/// `stored_verify` markers despite a completed stop grain). The STOPPED destination cannot be
/// driven to completion natively (it calls `silenceTrackMidiOutput`), so the shared completion
/// is asserted on the PLAYING destination.
void test_deferred_overdub_stop_completion_queues_stored_verification() {
  Track& track = preparePlayingTrack();
  track.setDeferredStoredVerificationArmedForTest(true);
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_FALSE(track.deferredStoredVerificationPendingForTest());

  for (unsigned i = 0; i < 10000 && track.hasPendingDeferredOverdubStop(); ++i)
    TrackCaptureStopTestAccess::step(track);
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_TRUE_MESSAGE(
      track.deferredStoredVerificationPendingForTest(),
      "a completed deferred overdub stop must queue the stored-MIDI verification dump");
  TEST_ASSERT_EQUAL_UINT32(1, track.deferredStoredVerificationOpportunityCountForTest());
}

// ---------------------------------------------------------------------------
// S1.2 -- working-set residence scaling characterization
// (openspec/changes/runtime-working-set-ownership, invariant 30 / DEC-076 point 1)
//
// The law: resident working-set cost is owned by the *active runtime role*, so with a
// constant active set it must not grow with the number of recorded slots. Today both
// terms grow: boot/load admission (`TrackManager::prewarmPlaybackRuntime`) reserves a
// canonical capture output and a transfer horizon per committed slot, and the per-slot
// runtime bundle keeps its working set resident with no release path.
//
// The law cases failed from 2026-09-23 (when they were written) until S2b.1 removed the
// per-slot boot reservation, behind a temporary `WORKING_SET_RED` gate so a repository-wide
// native run stayed green. **The gate is gone**: S2b.1 turned them green the same day and all
// three now run in the default gate (RECIPE R, IMPLEMENTATION_READINESS.md).
// ---------------------------------------------------------------------------
namespace {

struct WorkingSetResidence {
  size_t recordedSlots = 0;
  /// Chunks the committed content itself holds, before any admission runs.
  uint16_t canonicalPayloadChunks = 0;
  /// Chunks held once boot/load admission has reserved for every committed slot.
  uint16_t canonicalAdmittedChunks = 0;
  /// Slots whose runtime still holds a resident working set after admission.
  size_t residentWorkingSetSlots = 0;
  /// Slots that have a runtime bundle at all (allocated, working set or not).
  size_t allocatedRuntimeSlots = 0;
  /// Per committed slot: canonical-copy credits held, and whether an active-overdub
  /// transfer horizon was admitted (B01).
  std::vector<size_t> canonicalReservedBySlot;
  std::vector<bool> transferHorizonBySlot;

  /// The part of the measured residency that belongs to *inactive* slots: what
  /// admission added on top of the committed content, plus any working set held by a
  /// slot that is not the one active role.
  int32_t reservedChunks() const {
    return static_cast<int32_t>(canonicalAdmittedChunks) -
           static_cast<int32_t>(canonicalPayloadChunks);
  }
};

void commitRecordPassIntoTrackSlot(Track& track, uint8_t slot) {
  TrackCaptureStopTestAccess::selectSlot(track, slot);
  Loop& loop = track.getActiveLoop();
  loop.loopLengthTicks = 768;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(0, 1, 48, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(48, 1, 48, 0)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(768, CommitReason::RecordStop));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass());
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
}

/// Boot the production admission path with `recordedSlots` committed slots on the
/// selected track, then admit the one active slot through the record-arm path. The
/// active set is one slot in every variant, so any growth in the result is growth
/// caused by the recorded-slot count.
WorkingSetResidence measureWorkingSetResidence(size_t recordedSlots) {
  trackManager.~TrackManager();
  LoopEventStore::resetPoolForTests();
  new (&trackManager) TrackManager();
  LoopEventStore::initPool();

  const uint16_t poolChunks = LoopEventStoreConfig::POOL_CHUNK_COUNT;
  WorkingSetResidence result;
  result.recordedSlots = recordedSlots;

  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  for (uint8_t slot = 0; slot < static_cast<uint8_t>(recordedSlots); ++slot) {
    commitRecordPassIntoTrackSlot(track, slot);
  }
  result.canonicalPayloadChunks =
      static_cast<uint16_t>(poolChunks - LoopEventStore::freeChunkCount());

  // Production boot/load admission: every committed-but-inactive slot today.
  trackManager.prewarmPlaybackRuntime();
  // Production admission of the one active role (the record-arm route).
  TEST_ASSERT_TRUE(track.prewarmPlaybackForSlot(0, true));

  result.canonicalAdmittedChunks =
      static_cast<uint16_t>(poolChunks - LoopEventStore::freeChunkCount());

  for (uint8_t slot = 0; slot < static_cast<uint8_t>(recordedSlots); ++slot) {
    result.canonicalReservedBySlot.push_back(
        TrackCaptureStopTestAccess::canonicalReservedChunks(track, slot));
    result.transferHorizonBySlot.push_back(
        TrackCaptureStopTestAccess::transferHorizonAdmitted(track, slot));
  }

  for (uint8_t t = 0; t < Config::NUM_TRACKS; ++t) {
    Track& candidate = trackManager.getTrack(t);
    for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
      if (TrackCaptureStopTestAccess::runtimeAllocated(candidate, s)) {
        ++result.allocatedRuntimeSlots;
      }
      if (TrackCaptureStopTestAccess::hasResidentWorkingSet(candidate, s)) {
        ++result.residentWorkingSetSlots;
      }
    }
  }

  // The constant active set: exactly this slot is selected, nothing is sounding.
  TrackCaptureStopTestAccess::selectSlot(track, 0);
  return result;
}

}  // namespace

/// Default-suite case: the measurement itself is well formed, so the gated law case
/// cannot rot silently. It deliberately does not assert the law -- see the gated case.
void test_working_set_residence_measurement_is_well_formed() {
  const WorkingSetResidence one = measureWorkingSetResidence(1);
  const WorkingSetResidence four = measureWorkingSetResidence(4);

  TEST_ASSERT_EQUAL_UINT32(1, one.recordedSlots);
  TEST_ASSERT_EQUAL_UINT32(4, four.recordedSlots);
  // Identical per-slot content, so the committed payload is identical for one slot and
  // is what the four-slot variant holds plus three more slots' worth.
  TEST_ASSERT_GREATER_THAN(0, one.canonicalPayloadChunks);
  TEST_ASSERT_GREATER_THAN(0, four.canonicalPayloadChunks);
  TEST_ASSERT_TRUE(four.canonicalPayloadChunks >= one.canonicalPayloadChunks);
  // Admission must have observed the committed content, not started from empty.
  TEST_ASSERT_TRUE(one.canonicalAdmittedChunks >= one.canonicalPayloadChunks);
  TEST_ASSERT_TRUE(four.canonicalAdmittedChunks >= four.canonicalPayloadChunks);
  // The one active role is admitted in both variants.
  TEST_ASSERT_GREATER_THAN(0, one.residentWorkingSetSlots);
  TEST_ASSERT_GREATER_THAN(0, four.residentWorkingSetSlots);
}

/// S1.2 law case (a). PlatformIO captures native stdout, so the numbers are carried in the
/// failure message rather than printed.
void test_working_set_residence_does_not_grow_with_recorded_slots() {
  const WorkingSetResidence one = measureWorkingSetResidence(1);
  const WorkingSetResidence four = measureWorkingSetResidence(4);

  char message[192];
  snprintf(message, sizeof(message),
           "boot/load admission reserves per recorded slot: reserved(1 slot)=%d, "
           "reserved(4 slots)=%d (payload %u/%u, admitted %u/%u)",
           one.reservedChunks(), four.reservedChunks(),
           static_cast<unsigned>(one.canonicalPayloadChunks),
           static_cast<unsigned>(four.canonicalPayloadChunks),
           static_cast<unsigned>(one.canonicalAdmittedChunks),
           static_cast<unsigned>(four.canonicalAdmittedChunks));
  TEST_ASSERT_EQUAL_INT32_MESSAGE(one.reservedChunks(), four.reservedChunks(), message);
}

/// S1.2 law case (b).
void test_inactive_committed_slot_retains_no_resident_working_set() {
  const WorkingSetResidence one = measureWorkingSetResidence(1);
  const WorkingSetResidence four = measureWorkingSetResidence(4);

  char message[192];
  snprintf(message, sizeof(message),
           "an inactive committed slot keeps a resident working set: "
           "resident(1 slot)=%u, resident(4 slots)=%u (allocated bundles %u/%u)",
           static_cast<unsigned>(one.residentWorkingSetSlots),
           static_cast<unsigned>(four.residentWorkingSetSlots),
           static_cast<unsigned>(one.allocatedRuntimeSlots),
           static_cast<unsigned>(four.allocatedRuntimeSlots));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(one.residentWorkingSetSlots,
                                   four.residentWorkingSetSlots, message);
}

/// P1/B01 boot cold reservation. Green since S2b.1; before it, the unconditional per-slot
/// canonical reservation was the discriminating failure.
void test_b01_inactive_committed_slots_reserve_no_canonical_copy_or_horizon() {
  const WorkingSetResidence four = measureWorkingSetResidence(4);
  TEST_ASSERT_EQUAL_UINT32(4, four.recordedSlots);
  TEST_ASSERT_EQUAL_UINT32(4, static_cast<uint32_t>(four.canonicalReservedBySlot.size()));
  TEST_ASSERT_EQUAL_UINT32(4, static_cast<uint32_t>(four.transferHorizonBySlot.size()));

  // Slot 0 is the one active role; slots 1..3 are committed but inactive, so the
  // canonical-copy reservation and the transfer horizon must not be established for
  // them (S2b.1, invariant 34).
  size_t reservedOnInactive = 0;
  size_t horizonsOnInactive = 0;
  for (size_t slot = 1; slot < four.canonicalReservedBySlot.size(); ++slot) {
    reservedOnInactive += four.canonicalReservedBySlot[slot];
    if (four.transferHorizonBySlot[slot]) ++horizonsOnInactive;
  }

  char message[224];
  snprintf(message, sizeof(message),
           "an inactive committed slot reserved canonical-copy credits: "
           "total(inactive)=%u over %u slots, per-slot %u/%u/%u (active slot 0 = %u)",
           static_cast<unsigned>(reservedOnInactive),
           static_cast<unsigned>(four.canonicalReservedBySlot.size() - 1),
           static_cast<unsigned>(four.canonicalReservedBySlot[1]),
           static_cast<unsigned>(four.canonicalReservedBySlot[2]),
           static_cast<unsigned>(four.canonicalReservedBySlot[3]),
           static_cast<unsigned>(four.canonicalReservedBySlot[0]));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, static_cast<uint32_t>(reservedOnInactive), message);

  snprintf(message, sizeof(message),
           "an inactive committed slot admitted a transfer horizon: %u of %u inactive slots",
           static_cast<unsigned>(horizonsOnInactive),
           static_cast<unsigned>(four.transferHorizonBySlot.size() - 1));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, static_cast<uint32_t>(horizonsOnInactive), message);
}

/// P1/B02 (DEC-078, invariant 31): an accepted stop waits for the canonical-copy target and
/// then executes exactly once, from one press. This is the case the change exists for: the
/// pre-DEC-078 firmware dropped the request and required a second press.
void test_b02_accepted_stop_waits_for_canonical_credit_then_executes_once() {
  Track& track = preparePlayingTrack();
  const size_t chunksBefore = TrackCaptureStopTestAccess::canonicalChunks(track);
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  TEST_ASSERT_TRUE(track.isOverdubbing());
  const uint64_t interval = TrackCaptureStopTestAccess::interval(track);
  const size_t rowsBefore = track.getActiveLoop().capture.store.size();

  // Deny the canonical-copy target, then issue the stop exactly once.
  TrackCaptureStopTestAccess::drainCanonicalChunks(track);
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::canonicalOutputAdmitted(track));
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();

  // Accepted, not refused: same live interval, no job, no descriptor, identical content.
  TEST_ASSERT_TRUE_MESSAGE(track.isOverdubbing(),
                           "an accepted stop stays in the live interval while it waits");
  TEST_ASSERT_TRUE(track.hasAcceptedUnexecutedInteraction());
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(TrackCaptureStopTestAccess::queueSize(track)));
  TEST_ASSERT_EQUAL_UINT64(interval, TrackCaptureStopTestAccess::interval(track));
  // S4f.1: the latch carries the *interval's* own slot and generation, not whatever the active
  // slot happens to be at drive time.
  TEST_ASSERT_EQUAL_INT(1 /* Kind::Stop */,
                        TrackCaptureStopTestAccess::acceptedLatchKind(track));
  TEST_ASSERT_EQUAL_INT(0, TrackCaptureStopTestAccess::acceptedLatchSlot(track));
  TEST_ASSERT_EQUAL_UINT64(interval,
                           TrackCaptureStopTestAccess::acceptedLatchInterval(track));
  TEST_ASSERT_EQUAL(rowsBefore, track.getActiveLoop().capture.store.size());

  // Dispatch reachability: the deferred owner lane drives the latch. The park re-runs the
  // interval slot's canonical reservation, so the stop completes without a second press.
  for (size_t turn = 0; turn < 8 && !track.hasPendingDeferredOverdubStop(); ++turn) {
    (void)TrackCaptureStopTestAccess::serviceAcceptedInteraction(track);
  }
  TEST_ASSERT_TRUE_MESSAGE(track.hasPendingDeferredOverdubStop(),
                           "the accepted stop must be admitted once capacity returns");
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(TrackCaptureStopTestAccess::queueSize(track)));
  TEST_ASSERT_EQUAL_INT(
      0, TrackCaptureStopTestAccess::liveRingEntrySlot(track, 0));
  // The admission consumed the accepted obligation: it is not a second pending request.
  TEST_ASSERT_FALSE(track.hasAcceptedUnexecutedInteraction());

  // Drain the admitted job to retirement: the stop completes exactly once.
  for (unsigned i = 0; i < 20000 && track.hasPendingDeferredOverdubStop(); ++i) {
    TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_FALSE(track.hasAcceptedUnexecutedInteraction());
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(TrackCaptureStopTestAccess::queueSize(track)));
  TEST_ASSERT_TRUE(track.getActiveLoop().hasCommittedPasses());
  // The captured pair is in the committed content, and the interval's own rows survived the
  // wait unchanged (no partial seal happened while it was parked).
  TEST_ASSERT_TRUE(rowsBefore > 0);
  (void)chunksBefore;
}

/// S2b.1 + S2b.2 + S2b.6 (invariant 34): a loaded slot that was never record-armed still admits
/// its own overdub and completes one stop. Boot reserves no canonical-copy capacity for it, so the
/// overdub start must establish the transfer horizon and the park must acquire the canonical target.
void test_s2b2_loaded_slot_stop_acquires_its_own_horizon_and_canonical_target() {
  Track& track = prepareLoadedPlayingTrack();
  Loop& loop = track.getActiveLoop();
  // Boot/load admission only: neither resource exists for this committed-but-inactive slot.
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::canonicalOutputAdmitted(track),
                            "boot must not reserve the canonical copy for a committed slot");
  TEST_ASSERT_FALSE_MESSAGE(loop.hasActiveOverdubTransferAdmission(),
                            "boot must not admit a transfer horizon for a committed slot");

  // The overdub start is this slot's own cold admission, and it is explicitly **not** gated on the
  // canonical output -- that would re-create the DEC-067 START denial. It establishes the record
  // arm's reserve set instead: the view/bridge/companion scratch (without which the stop's declared
  // estimates are UINT64_MAX), the transfer horizon and the prepared-companion-history horizon.
  clockManager.assignCurrentTickSilently(96);
  track.startOverdubbing(96);
  TEST_ASSERT_TRUE(track.isOverdubbing());
  TEST_ASSERT_TRUE(loop.hasActiveOverdubTransferAdmission());
  TEST_ASSERT_TRUE(loop.preparedCompanionHistoryPrewarmed(0));
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::canonicalOutputAdmitted(track),
                            "START must not be gated on the canonical copy");

  // One Stop press while the canonical target is absent: accepted and retained, no job.
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.isOverdubbing());
  TEST_ASSERT_TRUE(track.hasAcceptedUnexecutedInteraction());
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(TrackCaptureStopTestAccess::queueSize(track)));

  // Required admission preconditions are independently satisfied, so the only thing denying the
  // drive is the canonical-copy target the park has to acquire.
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::tailPendingNotesEmpty(track));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::rawExtentSealable(track, 0));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::liveStagingEvents(track, 0) > 0);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::stopStoppingTransitionValid(track));
  TEST_ASSERT_TRUE(loop.canHandoffSessionExitSnapshot());

  // The park acquires the canonical target in a deferred turn and the drive then binds one job.
  for (size_t turn = 0; turn < 8 && !track.hasPendingDeferredOverdubStop(); ++turn) {
    (void)TrackCaptureStopTestAccess::serviceAcceptedInteraction(track);
  }
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::canonicalOutputAdmitted(track),
                           "the accepted stop's park must acquire the canonical target");
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(TrackCaptureStopTestAccess::queueSize(track)));
  TEST_ASSERT_EQUAL_INT(0, TrackCaptureStopTestAccess::liveRingEntrySlot(track, 0));
  // Admission consumed the accepted obligation: it is not a second, still-pending request.
  TEST_ASSERT_FALSE(track.hasAcceptedUnexecutedInteraction());

  for (unsigned i = 0; i < 20000 && track.hasPendingDeferredOverdubStop(); ++i) {
    TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE_MESSAGE(track.hasPendingDeferredOverdubStop(),
                            "a loaded slot's accepted stop must retire without a second press");
  TEST_ASSERT_FALSE(track.hasAcceptedUnexecutedInteraction());
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
  // S2b.6's retire-path invariant: the retiring job leaves the canonical target empty, so the next
  // admission's reservation has something it can actually acquire.
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::canonicalOutputEmpty(track, 0),
                           "a retiring job must not leave the canonical copy store populated");
}

/// S4.0 + S2b.4 (invariant 33/34): the per-slot liveness predicate governs the reservation release,
/// the ring scan is over the live window only, and release is idempotent while re-reserving re-retains.
void test_s4_0_release_respects_the_live_ring_window_and_is_idempotent() {
  Track& track = preparePlayingTrack();
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(PassConfig::ACTIVE_OVERDUB_TRANSFER_CHUNKS),
                           static_cast<uint32_t>(TrackCaptureStopTestAccess::canonicalChunks(track)));
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::noLiveOwner(track, 0),
                           "an idle slot with no job, latch or owner has no live owner");

  // A live stop job on slot 0 is an owner: the release must be refused, not merely skipped.
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_EQUAL_INT(0, TrackCaptureStopTestAccess::liveRingEntrySlot(track, 0));
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::noLiveOwner(track, 0),
                            "an admitted job for the slot is a live owner");
  // S2b.5 depends on this scope: a finalizing predecessor must block a load into ITS slot and must
  // not block any other slot on the same track. A track-level "capture active" flag would fail here.
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::noLiveOwner(track, 1),
                           "a live job on slot 0 must not mark another slot of the same track owned");
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::releaseCanonicalReservation(track, 0));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(PassConfig::ACTIVE_OVERDUB_TRANSFER_CHUNKS),
                           static_cast<uint32_t>(TrackCaptureStopTestAccess::canonicalChunks(track)));

  for (unsigned i = 0; i < 20000 && track.hasPendingDeferredOverdubStop(); ++i) {
    TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  // The retired descriptor is out of the live window but still carries `slot == 0`; a whole-array
  // scan would report it as an owner forever (the B6 stale positive).
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(TrackCaptureStopTestAccess::liveRingEntryCount(track)));
  TEST_ASSERT_EQUAL_INT(0, TrackCaptureStopTestAccess::rawRingEntrySlot(track, 0));
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::noLiveOwner(track, 0),
                           "a retired entry outside the live window is not a live owner");

  // Release, idempotently, then re-reserve and confirm the ids are retained again.
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::releaseCanonicalReservation(track, 0));
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(TrackCaptureStopTestAccess::canonicalChunks(track)));
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::releaseCanonicalReservation(track, 0),
                           "a second release must be a no-op success");
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(TrackCaptureStopTestAccess::canonicalChunks(track)));
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::prewarmCanonicalReservation(track, 0));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(PassConfig::ACTIVE_OVERDUB_TRANSFER_CHUNKS),
                           static_cast<uint32_t>(TrackCaptureStopTestAccess::canonicalChunks(track)));
}

/// S2b.4 (invariants 33/34) mid-copy: the release must be **refused** while the canonical store
/// still holds a taken event. Releasing there would hand the credits back to the pool while the
/// in-flight copy still expects them -- the `Denied` grain S2b.4 names -- so the refusal, not the
/// release, is the safety property. The slot is deliberately given no live owner, which makes the
/// populated store the *only* admissible refusal reason: the same call succeeds once the store is
/// empty, so the fixture cannot pass on an unrelated guard.
void test_s2b4_midcopy_populated_store_refuses_release() {
  Track& track = preparePlayingTrack();
  // Refcount census (S2b.4's second clause). `usedChunkCount()` counts every **allocated** chunk --
  // both the credits a reservation holds and the chunks backing committed content -- and
  // `freeChunkCount()` is its complement, so the two must move together by exactly the credit count
  // across a release. `preparePlayingTrack()` has already admitted this slot's reservation, so the
  // 8 credits are inside `usedBefore`; the release at the end of the fixture is what must give them
  // back. (Established by measurement, not by reading: two earlier cuts of this census assumed
  // "held" was invisible to `used` and the pool answered 13 and 13 where they predicted 21 and 14.)
  const uint16_t usedBefore = LoopEventStore::usedChunkCount();
  const uint16_t freeBefore = LoopEventStore::freeChunkCount();
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::prewarmCanonicalReservation(track, 0));
  const size_t reserved = TrackCaptureStopTestAccess::canonicalChunks(track);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(PassConfig::ACTIVE_OVERDUB_TRANSFER_CHUNKS),
                                   static_cast<uint32_t>(reserved),
                                   "the slot's canonical reservation is held before the fixture runs");
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::noLiveOwner(track, 0),
                           "the slot must be idle so the populated store is the sole refusal reason");
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::canonicalOutputEmpty(track, 0));

  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::appendCanonicalEvent(track, 0),
                           "the reserved tail must accept one event (the mid-copy state)");
  TEST_ASSERT_FALSE_MESSAGE(TrackCaptureStopTestAccess::canonicalOutputEmpty(track, 0),
                            "the store must be populated for this fixture to mean anything");
  // Appending the copied event consumes the reservation credit that backs it. That *is* the
  // mid-copy signature, and it is precisely why releasing here would strand the copy: the
  // credit is already committed to an event the pool would otherwise hand to someone else.
  const size_t afterAppend = TrackCaptureStopTestAccess::canonicalChunks(track);
  TEST_ASSERT_TRUE_MESSAGE(afterAppend < reserved,
                           "storing a copied event must spend the credit that backs it");
  // The event lands in a chunk the reservation already holds and already counts as allocated, so
  // neither counter moves: writing into a held chunk allocates nothing new.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      static_cast<uint32_t>(usedBefore), static_cast<uint32_t>(LoopEventStore::usedChunkCount()),
      "writing into an already-held chunk must not allocate another one");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      static_cast<uint32_t>(freeBefore), static_cast<uint32_t>(LoopEventStore::freeChunkCount()),
      "writing into a held chunk takes nothing further from the free list");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(reserved - 1),
                                   static_cast<uint32_t>(afterAppend),
                                   "the append must spend exactly one held credit");

  TEST_ASSERT_FALSE_MESSAGE(
      TrackCaptureStopTestAccess::releaseCanonicalReservation(track, 0),
      "a populated store must refuse release: releasing mid-copy strands the copy");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      static_cast<uint32_t>(afterAppend),
      static_cast<uint32_t>(TrackCaptureStopTestAccess::canonicalChunks(track)),
      "a refused release must not give any further credit back");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(usedBefore),
                                   static_cast<uint32_t>(LoopEventStore::usedChunkCount()),
                                   "a refused release must return no chunk to the pool");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(static_cast<uint32_t>(freeBefore),
                                   static_cast<uint32_t>(LoopEventStore::freeChunkCount()),
                                   "a refused release must not free a held credit");

  // Draining the copy makes the same call succeed, so the refusal above was the store and not
  // the liveness predicate or a missing runtime.
  TrackCaptureStopTestAccess::clearCanonicalOutput(track, 0);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::canonicalOutputEmpty(track, 0));
  TEST_ASSERT_TRUE_MESSAGE(TrackCaptureStopTestAccess::releaseCanonicalReservation(track, 0),
                           "an empty store with no live owner must release");
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(TrackCaptureStopTestAccess::canonicalChunks(track)));
  // Releasing must hand the held credits back: `used` falls and `free` rises by exactly the credit
  // count, so no residue is left. A residue here would be credits the slot never gives back -- the
  // working-set leak this change exists to remove.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      static_cast<uint32_t>(usedBefore) - static_cast<uint32_t>(PassConfig::ACTIVE_OVERDUB_TRANSFER_CHUNKS),
      static_cast<uint32_t>(LoopEventStore::usedChunkCount()),
      "release must deallocate exactly the reservation's own credits");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      static_cast<uint32_t>(freeBefore) + static_cast<uint32_t>(PassConfig::ACTIVE_OVERDUB_TRANSFER_CHUNKS),
      static_cast<uint32_t>(LoopEventStore::freeChunkCount()),
      "release must return every held credit to the free list");
}

/// S2b.3 boot budget evidence (invariant 34): boot consumes only the committed content, never a
/// per-slot reservation, and leaves the pool's own reserve intact. The device-scale peak figure
/// (a 24-bar overdub's reservation) is S1.5's device leg, not this fixture.
void test_s2b3_boot_consumes_only_committed_content_and_keeps_the_pool_reserve() {
  const WorkingSetResidence one = measureWorkingSetResidence(1);
  const WorkingSetResidence four = measureWorkingSetResidence(4);

  // Boot's own cost is the descriptor directories for the *default enabled set* (slot 0 of each of
  // the 8 tracks), which is a constant here and independent of the committed-slot count -- not a
  // per-recorded-slot reservation. The law is about the slope, so assert the slope is zero and
  // record the constant.
  char message[256];
  snprintf(message, sizeof(message),
           "boot's per-committed-slot pool cost must be zero: reserved(1 slot)=%d reserved(4 slots)=%d "
           "(payload %u/%u, admitted %u/%u)",
           one.reservedChunks(), four.reservedChunks(),
           static_cast<unsigned>(one.canonicalPayloadChunks),
           static_cast<unsigned>(four.canonicalPayloadChunks),
           static_cast<unsigned>(one.canonicalAdmittedChunks),
           static_cast<unsigned>(four.canonicalAdmittedChunks));
  TEST_ASSERT_EQUAL_INT32_MESSAGE(one.reservedChunks(), four.reservedChunks(), message);

  // The pool's own reserve survives boot: `PassConfig::CHUNK_RESERVE` (16) is the floor admission
  // refuses below, so boot must not consume it.
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(static_cast<uint32_t>(PassConfig::CHUNK_RESERVE),
                                      static_cast<uint32_t>(LoopEventStore::freeChunkCount()));
}

/// S4f.5 (intermediate slice): an accepted, not-yet-executed stop is in-flight work, so neither a
/// record start nor a slot switch may move underneath it. Before this fix the latch was invisible to
/// the manager's start guard (which checked only `hasPendingDeferredOverdubStop()`) and to
/// `finalizeCaptureAndSelectSlot`'s switch path.
void test_s4f5_accepted_stop_blocks_start_and_switch_until_it_executes() {
  Track& track = preparePlayingTrack();
  const uint8_t trackIndex = 0;
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  // Deny the canonical target so the stop is accepted and retained rather than admitted.
  TrackCaptureStopTestAccess::drainCanonicalChunks(track);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();

  TEST_ASSERT_TRUE(track.isOverdubbing());
  TEST_ASSERT_TRUE(track.hasAcceptedUnexecutedInteraction());
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_TRUE_MESSAGE(track.hasStopInFlight(),
                           "an accepted stop is in-flight work before any job exists");
  TEST_ASSERT_TRUE(track.hasLiveCaptureInterval());

  // A record start on this track is refused while the accepted stop is unexecuted.
  clockManager.assignCurrentTickSilently(168);
  trackManager.startRecordingTrack(trackIndex, 168);
  TEST_ASSERT_FALSE_MESSAGE(track.isRecording(),
                            "a record start must not move under an accepted stop");
  TEST_ASSERT_TRUE(track.isOverdubbing());

  // The slot switch must not move the active slot either: the accepted stop's interval owns slot 0.
  trackManager.setActiveLoopIndex(trackIndex, 1);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, track.getActiveLoopIndex(),
                                  "the active slot must not move under an unadmitted stop");
  TEST_ASSERT_TRUE(track.hasAcceptedUnexecutedInteraction());

  // The park acquires the canonical target and the drive binds one job to the interval's slot.
  for (size_t turn = 0; turn < 8 && !track.hasPendingDeferredOverdubStop(); ++turn) {
    (void)TrackCaptureStopTestAccess::serviceAcceptedInteraction(track);
  }
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_FALSE(track.hasAcceptedUnexecutedInteraction());
  TEST_ASSERT_TRUE(track.hasStopInFlight());
  // The admitted job still owns its active slot until it retires (the pre-existing DEC-067 rule this
  // slice does not change), so the switch stays refused.
  trackManager.setActiveLoopIndex(trackIndex, 1);
  TEST_ASSERT_EQUAL_UINT8(0, track.getActiveLoopIndex());

  for (unsigned i = 0; i < 20000 && track.hasPendingDeferredOverdubStop(); ++i) {
    TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_FALSE(track.hasStopInFlight());
  // The positive half -- that a switch *does* move once nothing is in flight -- cannot be asserted in
  // this suite: the move calls `StorageManager::prioritizeLoopSlotRestoreForFocus`, which is a
  // fail-closed off-path stub here. The guard reads `hasStopInFlight()`, which is now false, so the
  // refusal above is what this fixture owns.
  TEST_ASSERT_EQUAL_UINT8(0, track.getActiveLoopIndex());
}

/// P1/B03 (S4f.4, DEC-070): transport stop retargets a pending stop to STOPPED through the
/// **production** route, and the accepted request is not lost. A fake running-clock toggle would not
/// cover this: the retarget is `Track::stopOverdubbingToStopped` -> `requestDeferredOverdubStopToStopped`.
void test_b03_transport_stop_retargets_an_admitted_playing_stop_without_losing_it() {
  Track& track = preparePlayingTrack();
  clockManager.assignCurrentTickSilently(96);
  midiButtonActions.handleToggleRecord();
  inputNote(60, 97, 120);
  clockManager.assignCurrentTickSilently(144);
  midiButtonActions.handleToggleRecord();
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PendingStopDestination::Playing),
                          TrackCaptureStopTestAccess::jobDestination(track));

  // The production transport-stop entry.
  track.stopOverdubbingToStopped();
  TEST_ASSERT_TRUE_MESSAGE(track.hasPendingDeferredOverdubStop(),
                           "an admitted stop must survive the transport stop, not be lost");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<uint8_t>(PendingStopDestination::Stopped),
                                  TrackCaptureStopTestAccess::jobDestination(track),
                                  "transport stop retargets the pending stop to STOPPED");
  // Consuming the accepted obligation at the transport-stop entry is load-bearing: a latch left
  // behind would keep `hasStopInFlight()` true forever and block every later start and switch.
  TEST_ASSERT_FALSE(track.hasAcceptedUnexecutedInteraction());

  // S4f.4 transition 4 -- repeated retries. Driving the accepted interaction again while the retargeted
  // job is still pending must not stack a second descriptor or a second obligation.
  for (size_t turn = 0; turn < 4; ++turn) {
    (void)TrackCaptureStopTestAccess::serviceAcceptedInteraction(track);
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, static_cast<uint32_t>(TrackCaptureStopTestAccess::queueSize(track)),
                                   "repeated retries must not stack a second stop job");
  // The job keeps its identity across the retarget: still one descriptor, bound to the interval's
  // slot, with its admission still pending. Nothing was silently dropped or re-created.
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(TrackCaptureStopTestAccess::queueSize(track)));
  TEST_ASSERT_EQUAL_INT(0, TrackCaptureStopTestAccess::liveRingEntrySlot(track, 0));
  TEST_ASSERT_EQUAL_UINT64(TrackCaptureStopTestAccess::interval(track),
                           TrackCaptureStopTestAccess::jobInterval(track));
  // Drain the retargeted job. This half is only reachable now that `silenceTrackMidiOutput` is a
  // counting output boundary instead of a fail-closed stub; the `Stopped`-destination retire block is
  // the only production path that silenced a track and had no native coverage at all.
  for (unsigned i = 0; i < 20000 && track.hasPendingDeferredOverdubStop(); ++i) {
    TrackCaptureStopTestAccess::step(track);
  }
  TEST_ASSERT_FALSE(track.hasPendingDeferredOverdubStop());
  TEST_ASSERT_TRUE_MESSAGE(track.isStopped(),
                           "a retargeted stop must resolve to STOPPED, never PLAYING");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, g_nativeSilenceTrackCalls,
                                   "the STOPPED retire path silences the track exactly once");
  TEST_ASSERT_FALSE(track.hasAcceptedUnexecutedInteraction());
  TEST_ASSERT_TRUE(track.getActiveLoop().hasCommittedPasses());

  // S4f.4 transition 1 -- Transport Stop -> Start. Live state alone cannot distinguish "running
  // continuously" from "stopped and restarted", so the earlier stop must not be able to regain a
  // PLAYING destination or produce a second job when the transport runs again. History is covered by
  // consumption plus identity validation, not by a stored epoch.
  nativeTransportRunning = true;
  clockManager.assignCurrentTickSilently(192);
  for (size_t turn = 0; turn < 8; ++turn) {
    (void)TrackCaptureStopTestAccess::serviceAcceptedInteraction(track);
  }
  TEST_ASSERT_FALSE_MESSAGE(track.hasPendingDeferredOverdubStop(),
                            "a consumed stop must not be re-created when the transport restarts");
  TEST_ASSERT_FALSE(track.hasAcceptedUnexecutedInteraction());
  TEST_ASSERT_TRUE_MESSAGE(track.isStopped(), "the retargeted stop stays STOPPED after a restart");
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(TrackCaptureStopTestAccess::queueSize(track)));
}

/// S4f.2/B06: a track holding an **accepted, not-yet-executed interaction** must be eligible for a deferred
/// turn even when it is otherwise idle. This is the stopped-track reachability gap at the eligibility
/// predicate -- the scheduler's own use of it lives in `src/main.cpp`, which is not native-linkable.
void test_s4f2_deferred_eligibility_includes_an_accepted_interaction_on_an_idle_track() {
  Track& track = preparePlayingTrack();
  track.forceSetState(TRACK_STOPPED);
  TEST_ASSERT_FALSE_MESSAGE(track.hasRealtimeDeferredIdleWork(),
                            "an idle track with no obligation has no realtime deferred work");
  TEST_ASSERT_FALSE(track.hasAcceptedUnexecutedInteraction());

  TrackCaptureStopTestAccess::latchAcceptedStop(track, 0);
  TEST_ASSERT_TRUE(track.hasAcceptedUnexecutedInteraction());
  TEST_ASSERT_TRUE_MESSAGE(
      track.hasRealtimeDeferredIdleWork(),
      "an accepted interaction must make its track eligible even when the track is idle");
}

/// S4f.4 transition 3 (load/replacement): a request whose interval is gone must not bind to whatever now
/// occupies the same slot number. It is superseded -- reported and retired once -- rather than parked, since
/// parking would wait forever for an interval that can never return.
void test_s4f4_a_request_whose_interval_is_gone_is_superseded_not_bound() {
  Track& track = preparePlayingTrack();
  const size_t rowsBefore = track.getActiveLoop().capture.store.size();
  TrackCaptureStopTestAccess::latchAcceptedStop(track, 0);
  const uint64_t staleInterval = TrackCaptureStopTestAccess::acceptedLatchInterval(track);
  TEST_ASSERT_TRUE(track.hasAcceptedUnexecutedInteraction());

  // A replacement interval takes the same slot number; the old request's identity is now stale.
  TrackCaptureStopTestAccess::advanceInterval(track);
  TEST_ASSERT_NOT_EQUAL(staleInterval, TrackCaptureStopTestAccess::interval(track));

  (void)TrackCaptureStopTestAccess::serviceAcceptedInteraction(track);
  TEST_ASSERT_FALSE_MESSAGE(track.hasAcceptedUnexecutedInteraction(),
                            "a stale request is superseded and retired, not retained");
  TEST_ASSERT_FALSE_MESSAGE(track.hasPendingDeferredOverdubStop(),
                            "a stale request must not bind to the replacement content");
  TEST_ASSERT_EQUAL(rowsBefore, track.getActiveLoop().capture.store.size());
  // A second drive has nothing left to do: exactly one supersession.
  TEST_ASSERT_FALSE(TrackCaptureStopTestAccess::serviceAcceptedInteraction(track));
}

/// S4f.4 transition 2 (seek/reset): the boundary is recomputed in the tick domain present at execution, not
/// taken from the tick stored when the request was accepted. `ClockManager`'s own seek/reset/MIDI-Start
/// entries are harness stubs, so the tick domain is moved with the harness's controlled clock -- which is
/// enough to prove the *decision*: the drive passes the live tick, not `request.phaseTick`.
void test_s4f4_the_boundary_is_recomputed_in_the_tick_domain_at_execution() {
  Track& track = preparePlayingTrack();
  // The interval is live (OVERDUBBING) so the drive can actually admit; the point of this case is *which*
  // tick the admitted boundary derives from, not whether admission is reachable.
  track.forceSetState(TRACK_OVERDUBBING);
  clockManager.assignCurrentTickSilently(96);
  TrackCaptureStopTestAccess::latchAcceptedStop(track, 0);
  const uint64_t latchPhaseTick = TrackCaptureStopTestAccess::acceptedLatchPhaseTick(track);
  TEST_ASSERT_TRUE(track.hasAcceptedUnexecutedInteraction());

  // A seek moves the tick domain before the request executes.
  clockManager.assignCurrentTickSilently(600);
  TEST_ASSERT_TRUE(TrackCaptureStopTestAccess::serviceAcceptedInteraction(track));
  TEST_ASSERT_TRUE(track.hasPendingDeferredOverdubStop());

  const uint64_t executedPhaseTick = TrackCaptureStopTestAccess::jobPhaseTick(track);
  TEST_ASSERT_NOT_EQUAL_MESSAGE(latchPhaseTick, executedPhaseTick,
                               "the executed boundary must not be the tick stored at acceptance");
  TEST_ASSERT_TRUE_MESSAGE(executedPhaseTick > latchPhaseTick,
                           "the boundary must be recomputed in the new tick domain");
}

/// The record-arm admission is **repeatable**: three successive grants on an otherwise idle slot all succeed.
/// This was written to test whether the S1.5 capacity sweep's early refusal (N=2, with ~480 of 512 chunk ids
/// still free) was a non-idempotent arm rather than capacity -- it is not, so the arm is exonerated and the
/// guard is kept. It also rules out re-granting as a source of the per-slot slope the change removes.
void test_record_arm_admission_is_repeatable() {
  trackManager.~TrackManager();
  LoopEventStore::resetPoolForTests();
  new (&trackManager) TrackManager();
  LoopEventStore::initPool();
  Track& track = trackManager.getSelectedTrack();
  track.ensureLoopsAllocated();
  TEST_ASSERT_TRUE_MESSAGE(track.prewarmPlaybackForSlot(0, true), "first arm admission");
  TEST_ASSERT_TRUE_MESSAGE(track.prewarmPlaybackForSlot(0, true), "second arm admission must also succeed");
  TEST_ASSERT_TRUE_MESSAGE(track.prewarmPlaybackForSlot(0, true), "third arm admission must also succeed");
}

int main() {
  UNITY_BEGIN();
  // `ONLY_COMPANION=1` is the focused single-case runner for the former D8 reproducer (now part of
  // the default gate): it keeps an investigation trace from being diluted by every other fixture.
  if (getenv("ONLY_COMPANION") != nullptr) {
    RUN_TEST(test_adversarial_reentry_at_a_companion_checkpoint);
    return UNITY_END();
  }
  RUN_TEST(test_first_interval_uses_production_routes_and_freezes_at_punch_out);
  RUN_TEST(test_stop_descriptor_rotation_preserves_worker_borrow_and_reuses_retired_slots);
  RUN_TEST(test_missing_stop_descriptors_rejects_before_opening_capture);
  RUN_TEST(test_stop_ring_admits_at_tail_and_binds_worker_to_head);
  RUN_TEST(test_full_stop_ring_denies_admission_and_preserves_accepted_capture);
  RUN_TEST(test_seal_transfers_extent_and_successor_capture_preserves_it);
  RUN_TEST(test_read_source_yields_same_rows_before_and_after_sealing);
  RUN_TEST(test_sealed_extent_resolves_closure_note_id);
  RUN_TEST(test_sealed_range_borrow_release_is_owner_identified);
  RUN_TEST(test_stop_job_owns_pending_note_changes);
  RUN_TEST(test_pending_change_borrow_release_is_owner_identified);
  RUN_TEST(test_stop_job_owns_capture_preview);
  RUN_TEST(test_capture_preview_borrow_release_is_owner_identified);
  RUN_TEST(test_stop_job_owns_overdub_session_undo_baseline);
  RUN_TEST(test_deferred_stop_prepares_job_owned_undo_entry);
  RUN_TEST(test_deferred_stop_commit_marks_the_display_ranges_of_the_new_pass);
  RUN_TEST(test_deferred_stop_undo_entry_carries_every_session_pass);
  RUN_TEST(test_two_deferred_overdub_sessions_keep_both_undo_entries);
  RUN_TEST(test_stop_job_and_live_ingress_own_distinct_staging);
  RUN_TEST(test_overdub_shortens_record_pass_note_and_completes_stop);
  RUN_TEST(test_stop_copies_canonical_rows_without_mutating_sealed_raw_input);
  RUN_TEST(test_transport_stop_during_canonical_copy_preserves_accepted_rows);
  RUN_TEST(test_playback_reset_preserves_partial_copy_until_stopped_recovery);
  RUN_TEST(test_cache_reset_at_each_stop_phase_keeps_accepted_capture);
  RUN_TEST(test_empty_stop_runs_existing_worker_to_completion);
  RUN_TEST(test_populated_stop_runs_existing_worker_to_completion);
  RUN_TEST(test_completed_job_cannot_transition_a_newer_track_interval);
  RUN_TEST(test_job_note_retirement_does_not_clear_live_notes);
  RUN_TEST(test_deferred_stop_records_and_pins_its_causal_predecessor_source);
  RUN_TEST(test_superseded_predecessor_retires_without_touching_a_live_successor);
  RUN_TEST(test_prepared_pair_stream_matches_synchronous_oracle_plain_playback);
  RUN_TEST(test_differential_harness_detects_a_perturbed_pair);
  RUN_TEST(test_seal_admitted_into_pair_requires_atomic_replacement);
  RUN_TEST(test_prepared_pair_preserves_same_tick_off_before_on_order);
  RUN_TEST(test_prepared_pair_anchor_is_part_of_its_identity);
  RUN_TEST(test_prepared_pair_publishes_off_clock_and_the_consume_decision_never_builds);
  RUN_TEST(test_source_change_requests_a_replacement_and_holds_the_last_coherent_pair);
  RUN_TEST(test_published_pair_emits_the_synchronous_oracle_trace_after_a_source_change);
  RUN_TEST(test_seal_admission_signal_requests_the_pair_replacement);
  RUN_TEST(test_gathered_pair_matches_the_synchronous_path_across_a_wrap_and_a_loop_edge_note);
  RUN_TEST(test_gathered_pair_holds_then_replaces_atomically_when_the_root_changes_mid_window);
  RUN_TEST(test_long_loop_window_is_not_a_pair_and_matches_an_independent_window_reference);
  RUN_TEST(test_gathered_pair_waits_for_the_record_stop_root_then_publishes);
  RUN_TEST(test_active_transport_playback_order_rebuild_is_instrumented);
  RUN_TEST(test_stopped_oracle_playback_order_build_is_not_a_hot_rebuild);
  RUN_TEST(test_adversarial_reentry_at_a_canonical_checkpoint);
  RUN_TEST(test_adversarial_reentry_at_a_canonical_rewrite_checkpoint);
  // OpenSpec 5.5 promoted the D8-fixed `CompanionHistory` checkpoint into the default gate.
  RUN_TEST(test_adversarial_reentry_at_a_companion_checkpoint);
  RUN_TEST(test_adversarial_reentry_at_the_staged_pass_ownership_checkpoint);
  RUN_TEST(test_adversarial_reentry_at_the_publication_checkpoint);
  RUN_TEST(test_simultaneous_interval_owned_staged_passes);
  RUN_TEST(test_stop_job_owns_held_notes_and_closes_original_slot);
  RUN_TEST(test_stop_job_denies_changed_geometry_before_closure_mutation);
  RUN_TEST(test_button_a_successor_stop_denied_until_deferred_role_available);
  RUN_TEST(test_successor_stop_denied_by_occupied_deferred_capacity_is_non_destructive);
  RUN_TEST(test_record_admission_denied_when_required_prewarm_fails);
  RUN_TEST(test_reentry_routes_agree_on_admission_and_capacity_denial);
  RUN_TEST(test_manager_reentry_with_a_held_same_pitch_note);
  RUN_TEST(test_track_reentry_does_not_depend_on_manager_guard);
  RUN_TEST(test_session_exit_receipts_are_per_interval);
  RUN_TEST(test_predecessor_cleanup_keeps_the_successor_session);
  RUN_TEST(test_successor_session_ends_with_its_own_retirement);
  RUN_TEST(test_superseded_predecessor_releases_only_its_own_receipt);
  RUN_TEST(test_successor_same_interval_transfers_exactly_once);
  RUN_TEST(test_start_request_while_successor_is_live_is_not_a_second_interval);
  RUN_TEST(test_successor_start_is_admitted_while_finalization_resources_are_busy);
  RUN_TEST(test_successor_start_keeps_the_predecessors_frozen_builders);
  RUN_TEST(test_successor_stop_is_denied_when_the_canonical_copy_target_is_busy);
  RUN_TEST(test_deferred_predecessor_completion_leaves_successor_interval_untouched);
  RUN_TEST(test_transport_stop_retarget_retires_a_midflight_playing_job);
  RUN_TEST(test_transport_stop_while_successor_is_live_keeps_its_rows_and_root);
  RUN_TEST(test_successor_wrap_defers_its_pass_commit_while_a_stop_job_is_pending);
  RUN_TEST(test_successor_wrap_keeps_the_predecessors_frozen_builders);
  RUN_TEST(test_successor_wrap_keeps_a_held_note_reservation_for_its_physical_note_off);
  RUN_TEST(test_redundant_stored_verification_queue_keeps_pending_dump);
  RUN_TEST(test_ineligible_stored_verification_queue_keeps_pending_dump);
  RUN_TEST(test_deferred_overdub_stop_completion_queues_stored_verification);
  // S1.2 working-set residence characterization: the measurement runs in the default
  // gate; the law case is the known-red characterization and stays behind its gate
  // until the implementing slice (S2b + S3/S4 release) lands.
  RUN_TEST(test_working_set_residence_measurement_is_well_formed);
  // P1/B02 accepted-stop acceptance: green once the latch + park are in place, so it runs
  // in the default gate (unlike the residence characterizations).
  RUN_TEST(test_b02_accepted_stop_waits_for_canonical_credit_then_executes_once);
  RUN_TEST(test_s2b2_loaded_slot_stop_acquires_its_own_horizon_and_canonical_target);
  RUN_TEST(test_s4_0_release_respects_the_live_ring_window_and_is_idempotent);
  RUN_TEST(test_s2b4_midcopy_populated_store_refuses_release);
  RUN_TEST(test_s2b3_boot_consumes_only_committed_content_and_keeps_the_pool_reserve);
  RUN_TEST(test_s4f5_accepted_stop_blocks_start_and_switch_until_it_executes);
  RUN_TEST(test_s4f2_deferred_eligibility_includes_an_accepted_interaction_on_an_idle_track);
  RUN_TEST(test_s4f4_a_request_whose_interval_is_gone_is_superseded_not_bound);
  RUN_TEST(test_s4f4_the_boundary_is_recomputed_in_the_tick_domain_at_execution);
  RUN_TEST(test_record_arm_admission_is_repeatable);
  RUN_TEST(test_b03_transport_stop_retargets_an_admitted_playing_stop_without_losing_it);
  // S2b.1 landed on 2026-09-23, so the residence law and B01 are green and the former
  // `WORKING_SET_RED` gate is gone: they are part of the default gate now (RECIPE R, "remove a known
  // baseline exception once the implementing slice lands").
  RUN_TEST(test_working_set_residence_does_not_grow_with_recorded_slots);
  RUN_TEST(test_inactive_committed_slot_retains_no_resident_working_set);
  RUN_TEST(test_b01_inactive_committed_slots_reserve_no_canonical_copy_or_horizon);
  return UNITY_END();
}
