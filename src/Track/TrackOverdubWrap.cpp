//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackInternal.h"
#include "Utils/LaneGapSpans.h"

#include "ClockManager.h"
#include "Logger.h"
#include "Utils/TrackMem.h"

#if defined(SESSION_CAPTURE)
uint32_t g_overdubWrapCommitUs = 0;
uint32_t g_overdubWrapSealPendingUs = 0;
uint32_t g_overdubWrapBeginCaptureUs = 0;
#endif

namespace {
/// Cold: the wrap's obligation admission (the hot wrap check keeps one call).
TRACK_COLD_MEM __attribute__((noinline)) bool overdubWrapObligationAdmitted(const Loop& loop) {
  return loop.wrapObligationCapacityAvailable();
}
}  // namespace

bool Track::maybeCommitOverdubWrap(uint32_t prevPhase, uint32_t currentPhase) {
  Loop& loop = getActiveLoop();
  if (!loop.hasOverdubSession() || loop.loopLengthTicks == 0) {
    return false;
  }
  if (loop.consumeSuppressedOverdubWrapCrossing(prevPhase, currentPhase)) {
    return false;
  }
  loop.armOverdubWrapAfterLeavingStart(currentPhase);
  if (loop.shouldCommitOverdubWrap(prevPhase, currentPhase)) {
    // C3.4 off the Clock: both obligation slots owned -> the wrap defers through the same DEC-069 owed commit.
    if (!overdubWrapObligationAdmitted(loop)) {
      overdubWrapCommitPending_ = true;
      return false;
    }
    // Interval admission no longer blocks another slot's wrap. The slot's actual
    // canonical worker still excludes a committed-revision mutation underneath its
    // prefix: independent frozen staging does not duplicate canonical scratch.
    if (hasPendingStopForInterval(captureInterval_) || hasCaptureStopWorkerForSlot(activeLoopIndex)) {
      // No new human-log literal here on purpose: on this platform `.rodata` is RAM1-resident, and
      // one added string in this translation unit snaps 4,096 bytes of DTCM away with no symbol
      // size change (`.cursor/rules/Teensy-RAM1-Bank-Overflow.mdc`). The deferral is observable
      // from the existing markers: `Reentry released` for the predecessor now precedes this
      // interval's `Overdub wrap committed`, which the owed commit emits later.
      overdubWrapCommitPending_ = true;
      return false;
    }
    return commitOverdubWrap();
  }
  // The owed commit is retried on the normal playback turn, never from the stop edge: the caller
  // applies the committed pass to playback and to the interval's source view exactly as it does
  // for an immediate wrap.
  if (overdubWrapCommitPending_ && !hasPendingStopForInterval(captureInterval_) &&
      !hasCaptureStopWorkerForSlot(activeLoopIndex) && overdubWrapObligationAdmitted(loop)) {
    return commitOverdubWrap();
  }
  return false;
}

TRACK_COLD_MEM bool Track::commitOverdubWrap() {
  LANE_MSG_SPAN(OverdubWrapCommit);
  Loop& loop = getActiveLoop();
  if (!loop.hasOverdubSession() || loop.capture.phase != CapturePhase::Overdub) {
    return false;
  }
  // C3.4 off the Clock: the commit point admits the handover itself, so no caller can commit an interval it cannot
  // hand over. Both slots owned -> the DEC-069 owed commit, retried on a later playback turn; nothing is answered here.
  if (!overdubWrapObligationAdmitted(loop)) {
    overdubWrapCommitPending_ = true;
    return false;
  }
  // Consumed on every path: the interval's rows have left the producer whether the pass
  // committed, was empty, or moved to the interval's own stop.
  overdubWrapCommitPending_ = false;
  LANE_WRAP_BEGIN();
  struct WrapBreakdownEnd {
    ~WrapBreakdownEnd() { LANE_WRAP_END(); }
  } wrapBreakdownEnd;
  SessionMidiEventVec heldOns;
  {
    LANE_WRAP_PHASE(WrapHeldOns);
    loop.extractOpenCaptureNoteOns(heldOns);
  }
  LANE_WRAP_COUNT(WrapHeldOnCount, static_cast<uint32_t>(heldOns.size()));
  LANE_WRAP_COUNT(WrapCaptureEvents, static_cast<uint32_t>(loop.capture.store.size()));
  if (loop.capture.store.empty()) {
    for (const MidiEvent& evt : heldOns) {
      loop.appendCaptureEvent(evt);
    }
    loop.noteOverdubWrapCommitted();
    return false;
  }
  const uint32_t sealedAtTick = 0;
  const uint32_t priorPlaybackRevision = loop.playbackRevision;
#if defined(SESSION_CAPTURE)
  g_overdubWrapCommitUs = 0;
  g_overdubWrapSealPendingUs = 0;
  g_overdubWrapBeginCaptureUs = 0;
  const uint32_t wrapCommitStartUs = micros();
#endif
  CommitResult result;
  {
    LANE_WRAP_PHASE(WrapCommitPass);
    result = loop.commitCapturePass(CommitReason::OverdubWrap, sealedAtTick);
  }
#if defined(SESSION_CAPTURE)
  g_overdubWrapCommitUs = micros() - wrapCommitStartUs;
#endif
  const bool wrapped = result == CommitResult::Committed;
  if (wrapped) {
    const PassId passId = loop.lastCommittedPassId();
#if defined(SESSION_CAPTURE)
    const uint32_t wrapSealPendingStartUs = micros();
#endif
    {
      // C3.4 off the Clock (owner rulings 2026-11-04): only the private-predecessor holds are answered here. The
      // interval's pending set and its owed completions are handed over in constant work below; the discharge
      // lane answers them against the interval's own source cut and seals them into this record.
      LANE_WRAP_PHASE(WrapSeal);
      loop.materializeWrapPrivateHoldsAtDeadline();
    }
#if defined(SESSION_CAPTURE)
    g_overdubWrapSealPendingUs = micros() - wrapSealPendingStartUs;
#endif
    LANE_WRAP_PHASE(WrapSessionBookkeeping);
    loop.pushOverdubSessionPass(passId, EditPassIdList{});
    // Capacity was admitted before the commit (`maybeCommitOverdubWrap`), on this same thread.
    (void)loop.handOverWrapObligation(passId, loop.overdubSessionUndoDepth() - 1);
    loop.recordLastOverdubSeal(passId, EditPassIdList{}, priorPlaybackRevision);
    loop.stampWrapPublicationObligation();
    loop.markAffectedDisplayCacheRanges(passId, EditPassIdList{});
  }
#if defined(SESSION_CAPTURE)
  const uint32_t wrapBeginCaptureStartUs = micros();
#endif
  {
    LANE_WRAP_PHASE(WrapBeginCapture);
    loop.beginCapture(CapturePhase::Overdub, loop.playheadPhaseTick);
  }
#if defined(SESSION_CAPTURE)
  g_overdubWrapBeginCaptureUs = micros() - wrapBeginCaptureStartUs;
#endif
  // A held note crosses into a new capture pass at wrap. Re-admit its new
  // canonical NoteOn through fixed staging before mutating capture.store so
  // its later physical or stop-time NoteOff still owns a reservation. The
  // previous pass has already become canonical; this fresh staging generation
  // represents only the successor capture tail.
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(activeLoopIndex);
  // DEC-069: the slot's canonical pass inputs, its frozen order/rewrite builders and its frozen
  // source binding are one shared resource, and a pending predecessor stop job owns them for its
  // own interval. This wrap is the live interval's *third* transition (after the start and before
  // the stop) that would re-bind them, and it must respect that ownership exactly as the other
  // two do: re-binding here reset `canonicalPassInputs.live_`, so the predecessor's canonical tail
  // phase denied forever, its job never retired, and this interval's own stop could never be
  // admitted (`captures/session_20260917_172651.log`: OD2's job stayed pending from 590.687 s to
  // the end of the session and OD3's 598.245 s stop was refused as occupied). A skipped resume is
  // a supported outcome -- the source invalidation path owns the bounded restart -- and the
  // committed wrap pass still reaches playback through the caller's appended source view.
  const bool predecessorOwnsSlotCanonicalWork = hasCaptureStopWorkerForSlot(activeLoopIndex);
  if (wrapped && runtime && !predecessorOwnsSlotCanonicalWork) {
    const PlaybackViewSourceIdentity source{loop.playbackRevision, playbackGeneration,
                                            loop.publishedSnapshotIdentity()};
    LANE_WRAP_PHASE(WrapResumeCanonical);
    (void)runtime->resumeCanonicalAfterAppend(loop.passes, priorPlaybackRevision, source,
        {loop.playbackRevision, playbackGeneration, loop.loopLengthTicks, loop.canonicalStorageGeneration});
  }
  LANE_WRAP_PHASE(WrapCarryOpenNotes);
  RuntimePlaybackViewSet* viewSet =
      runtime != nullptr ? runtime->viewSetIfPrewarmed() : nullptr;
  if (runtime == nullptr || viewSet == nullptr || !viewSet->prewarmed) {
    // startOverdubbing() admitted this storage before capture began. Reaching
    // this branch is an invariant failure; retain the old append behavior so
    // the accepted held note is never silently omitted from canonical capture.
    for (const MidiEvent& evt : heldOns) {
      (void)loop.appendCaptureEvent(evt);
    }
  } else {
    // Discarding the frozen order/rewrite builders is the same ownership question: with no job
    // owning them the clear is wanted (a completed build must not keep describing a pre-wrap
    // tail), but doing it to a pending predecessor stranded it in its order phase.
    if (!predecessorOwnsSlotCanonicalWork) {
      runtime->discardFrozenCaptureOrder();
      runtime->discardFrozenCaptureCanonicalOrder();
    }
    CaptureAdmissionStaging& staging = viewSet->liveCaptureStaging();
    staging.reset();
    staging.captureGeneration = playbackGeneration;
    for (const MidiEvent& evt : heldOns) {
      if (!staging.appendContinuationNoteOn(evt)) {
        // Capacity was admitted at overdub start. Preserve canonical input if
        // that invariant has later been broken; recovery must retain rather
        // than drop an already accepted cross-wrap NoteOn.
        (void)loop.appendCaptureEvent(evt);
        continue;
      }
      if (!loop.appendCaptureEvent(evt)) {
        CaptureNoteReservation* reservation =
            staging.matchingOpenReservation(evt.noteId, evt.channel, evt.data.noteData.note);
        if (reservation != nullptr) {
          staging.close(*reservation);
        }
        continue;
      }
      ++staging.captureGeneration;
    }
  }
  // Re-opened holds crossed loop end — Stage 2 wrap split must not depend only on
  // playhead-vs-on inference after beginCapture cleared preview (`191636` pitch 18).
  for (const uint32_t previewNoteIndex : loop.capturePreview.openNoteIndices) {
    if (previewNoteIndex < loop.capturePreview.noteStates.size()) {
      loop.capturePreview.noteStates[previewNoteIndex].wrapHeld = true;
    }
  }
  loop.noteOverdubWrapCommitted();
  logger.logTrackEvent("Overdub wrap committed", clockManager.getCurrentTick());
  return wrapped;
}
