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
  for (const MidiEvent& evt : heldOns) {
    loop.appendCaptureEvent(evt);
  }
  loop.noteOverdubWrapCommitted();
  logger.logTrackEvent("Overdub wrap committed", clockManager.getCurrentTick());
  return wrapped;
}
