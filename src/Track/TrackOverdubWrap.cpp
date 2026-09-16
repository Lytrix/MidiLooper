//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackInternal.h"

#include "ClockManager.h"
#include "Logger.h"
#include "Utils/TrackMem.h"

#if defined(SESSION_CAPTURE)
uint32_t g_overdubWrapCommitUs = 0;
uint32_t g_overdubWrapSealPendingUs = 0;
uint32_t g_overdubWrapBeginCaptureUs = 0;
#endif

bool Track::maybeCommitOverdubWrap(uint32_t prevPhase, uint32_t currentPhase) {
  Loop& loop = getActiveLoop();
  if (!loop.hasOverdubSession() || loop.loopLengthTicks == 0) {
    return false;
  }
  if (loop.consumeSuppressedOverdubWrapCrossing(prevPhase, currentPhase)) {
    return false;
  }
  loop.armOverdubWrapAfterLeavingStart(currentPhase);
  if (!loop.shouldCommitOverdubWrap(prevPhase, currentPhase)) {
    return false;
  }
  return commitOverdubWrap();
}

TRACK_COLD_MEM bool Track::commitOverdubWrap() {
  Loop& loop = getActiveLoop();
  if (!loop.hasOverdubSession() || loop.capture.phase != CapturePhase::Overdub) {
    return false;
  }
  SessionMidiEventVec heldOns;
  loop.extractOpenCaptureNoteOns(heldOns);
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
  const CommitResult result = loop.commitCapturePass(CommitReason::OverdubWrap, sealedAtTick);
#if defined(SESSION_CAPTURE)
  g_overdubWrapCommitUs = micros() - wrapCommitStartUs;
#endif
  const bool wrapped = result == CommitResult::Committed;
  if (wrapped) {
    const PassId passId = loop.lastCommittedPassId();
    loop.queueOverdubWrapSnapshotJournal(passId);
    loop.recountOverlapHoldTotalsFromPending();
    loop.emitOverlapHoldTotals();
#if defined(SESSION_CAPTURE)
    const uint32_t wrapSealPendingStartUs = micros();
#endif
    EditPassIdList companionIds = loop.sealPendingNoteChangesToEditPasses();
#if defined(SESSION_CAPTURE)
    g_overdubWrapSealPendingUs = micros() - wrapSealPendingStartUs;
#endif
    loop.pushOverdubSessionPass(passId, companionIds);
    loop.recordLastOverdubSeal(passId, companionIds, priorPlaybackRevision);
    loop.markAffectedDisplayCacheRanges(passId, companionIds);
  }
#if defined(SESSION_CAPTURE)
  const uint32_t wrapBeginCaptureStartUs = micros();
#endif
  loop.beginCapture(CapturePhase::Overdub, loop.playheadPhaseTick);
#if defined(SESSION_CAPTURE)
  g_overdubWrapBeginCaptureUs = micros() - wrapBeginCaptureStartUs;
#endif
  // A held note crosses into a new capture pass at wrap. Re-admit its new
  // canonical NoteOn through fixed staging before mutating capture.store so
  // its later physical or stop-time NoteOff still owns a reservation. The
  // previous pass has already become canonical; this fresh staging generation
  // represents only the successor capture tail.
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(activeLoopIndex);
  if (wrapped && runtime) {
    const PlaybackViewSourceIdentity source{loop.playbackRevision, playbackGeneration,
                                            loop.publishedSnapshotIdentity()};
    (void)runtime->resumeCanonicalAfterAppend(loop.passes, priorPlaybackRevision, source,
        {loop.playbackRevision, playbackGeneration, loop.loopLengthTicks, loop.canonicalStorageGeneration});
  }
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
    runtime->discardFrozenCaptureOrder();
    runtime->discardFrozenCaptureCanonicalOrder();
    CaptureAdmissionStaging& staging = viewSet->captureStaging;
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
