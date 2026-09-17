//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackInternal.h"

#include <algorithm>
#include <cstdio>
#include <utility>
#include <vector>

#include "Globals.h"
#include "Logger.h"
#include "CaptureAppendResult.h"
#include "LoopEventStore.h"
#include "TickPhase.h"
#include "Utils/CommittedPlaybackLedgerCatchUp.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/IntervalProjection.h"
#include "OverlapNoteIdObservation.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/MemoryPressurePolicy.h"
#include "Utils/RecordStopLength.h"
#include "Utils/ExternalPoolAccounting.h"
#include "Utils/RuntimeTimingTelemetry.h"
#include "OverlapNoteIdSet.h"

#if defined(__IMXRT1062__)
#define TRACK_CAPTURE_ADMISSION_MEM FLASHMEM
#else
#define TRACK_CAPTURE_ADMISSION_MEM
#endif

namespace {
const OverlapNoteIdSet kEmptyOverlapNoteIds{};
}

#if defined(SESSION_CAPTURE)
static void logCaptureAppendDeny(const Loop& loop, const CaptureAppendResult& result,
                                 uint8_t channel, uint8_t note, uint32_t tick) {
  SC_CAPTURE_APPEND_DENY(captureAppendDenyReasonLabel(result.reason),
                           LoopEventStore::freeChunkCount(), LoopEventStore::usedChunkCount(),
                           memoryPressureLevelName(MemoryMonitor::getAdvisoryPressureLevel()),
                           channel, note, tick, loop.hasPendingCapturePass() ? 1 : 0);
}
#endif

#if defined(SESSION_CAPTURE) && defined(ARDUINO)
namespace {

constexpr size_t kOccupyMismatchSpanLogLimit = 6;
constexpr size_t kOccupyMismatchEventLogLimit = 8;

// Foreground-only observation scratch. The two 128-ID sets and formatting
// buffer must not stack on top of noteOn's PendingNote and libc formatting.
// Reset on every use; no musical/session state is retained here.
struct OccupyObservationScratch {
  OverlapNoteIdSet sourceViewIds;
  OverlapNoteIdSet preparedIds;
  char line[256];
};

/// Circular tick distance on the loop, so a hold near the wrap still sees its lane events.
uint32_t occupyMismatchTickDistance(uint32_t phase, uint32_t holdStart, uint32_t loopLength) {
  const uint32_t linear = phase >= holdStart ? phase - holdStart : holdStart - phase;
  if (loopLength == 0 || linear <= loopLength / 2) {
    return linear;
  }
  return loopLength - linear;
}

/// Occupy mismatch evidence: which lane the ledger holds, every source-view span on that
/// lane, and the committed events that could have written it. Emitted only when the ledger
/// and the source view disagree, so passing runs stay quiet and the ring is not flooded.
TRACK_COLD_MEM __attribute__((noinline)) void logOccupyLedgerMismatch(
    const Loop& loop, const ActiveNoteLedger& ledger, const PlaybackMergedMidiEvents& merged,
    uint8_t channel, uint8_t pitch, uint32_t holdStart, uint32_t loopLength,
    unsigned ledgerParticipants, unsigned sourceViewParticipants) {
  const uint32_t diagnosticStartUs = micros();
  unsigned ledgerActive = 0;
  NoteId ledgerNoteId = kInvalidNoteId;
  uint32_t ledgerStartTick = 0;
  ledger.forEachActive(
      [&](uint8_t entryChannel, uint8_t entryNote, const ActiveNoteLedger::Entry& entry) {
        if (entryChannel != channel || entryNote != pitch) {
          return;
        }
        ++ledgerActive;
        ledgerNoteId = entry.noteId;
        ledgerStartTick = entry.startTick;
      });
  const unsigned catchUpWouldApply =
      CommittedPlaybackLedgerCatchUp::shouldApply(loop, holdStart) ? 1u : 0u;

  char line[256];
  snprintf(line, sizeof(line),
           "#CAP,%lu,DIAG,lcr,mismatch,pitch=%u,ch=%u,hs=%lu,n=%u,a=%u,led=%u,lid=%lu,lst=%lu,"
           "ltick=%lu,cu=%u,win=%lu,wlen=%lu,rev=%lu,dus=%lu",
           static_cast<unsigned long>(micros()), static_cast<unsigned>(pitch),
           static_cast<unsigned>(channel), static_cast<unsigned long>(holdStart),
           ledgerParticipants, sourceViewParticipants, ledgerActive,
           static_cast<unsigned long>(ledgerNoteId), static_cast<unsigned long>(ledgerStartTick),
           static_cast<unsigned long>(loop.lastTickInLoop), catchUpWouldApply,
           static_cast<unsigned long>(merged.windowStartTick),
           static_cast<unsigned long>(merged.windowLengthTicks),
           static_cast<unsigned long>(merged.builtFromRevision),
           static_cast<unsigned long>(micros() - diagnosticStartUs));
  DebugSessionCapture::appendCaptureTextLine(line);

  // Every span on the lane, not only the first covering one — overlap candidates are the point.
  // Spans covering the hold go first: they are the participants being counted, and a lane can
  // carry more non-covering spans than the cap allows.
  size_t spansLogged = 0;
  for (unsigned pass = 0; pass < 2 && spansLogged < kOccupyMismatchSpanLogLimit; ++pass) {
    for (const NoteUtils::DisplayNote& note : loop.overdubSourceViewNotes()) {
      if (spansLogged >= kOccupyMismatchSpanLogLimit) {
        break;
      }
      if (note.note != pitch) {
        continue;
      }
      const unsigned presentAtHold = OverlapNoteIdObservation::displayNotePresentAtHold(
                                         note.startTick, note.endTick, holdStart, loopLength)
                                         ? 1u
                                         : 0u;
      if (presentAtHold != (pass == 0 ? 1u : 0u)) {
        continue;
      }
      unsigned noteOnInMerged = 0;
      for (const MidiEvent& evt : merged.mergedEvents) {
        if (evt.isNoteOn() && evt.noteId == note.noteId) {
          noteOnInMerged = 1;
          break;
        }
      }
      snprintf(line, sizeof(line),
               "#CAP,%lu,DIAG,lcr,mmspan,pitch=%u,i=%u,s=%lu,e=%lu,id=%lu,p=%u,on=%u",
               static_cast<unsigned long>(micros()), static_cast<unsigned>(pitch),
               static_cast<unsigned>(spansLogged), static_cast<unsigned long>(note.startTick),
               static_cast<unsigned long>(note.endTick), static_cast<unsigned long>(note.noteId),
               presentAtHold, noteOnInMerged);
      DebugSessionCapture::appendCaptureTextLine(line);
      ++spansLogged;
    }
  }

  // Raw stored tick and phase both: overdub-pass events are linear and may exceed loopLength.
  size_t eventsLogged = 0;
  for (const MidiEvent& evt : merged.mergedEvents) {
    if (eventsLogged >= kOccupyMismatchEventLogLimit) {
      break;
    }
    if (!evt.isNoteOn() && !evt.isNoteOff()) {
      continue;
    }
    // Pitch only: the ledger collapses every channel message onto the track channel
    // (remappedPlaybackChannel), while stored capture events keep their input channel.
    // Filtering merged events on the track channel matches nothing. Log evt.channel instead.
    if (evt.data.noteData.note != pitch) {
      continue;
    }
    const uint32_t phase = IntervalProjection::playbackEventPhase(evt.tick, loopLength);
    if (occupyMismatchTickDistance(phase, holdStart, loopLength) > Config::TICKS_PER_BAR) {
      continue;
    }
    snprintf(line, sizeof(line),
             "#CAP,%lu,DIAG,lcr,mmevt,pitch=%u,i=%u,t=%lu,ph=%lu,k=%s,id=%lu,ech=%u",
             static_cast<unsigned long>(micros()), static_cast<unsigned>(pitch),
             static_cast<unsigned>(eventsLogged), static_cast<unsigned long>(evt.tick),
             static_cast<unsigned long>(phase), evt.isNoteOff() ? "off" : "on",
             static_cast<unsigned long>(evt.noteId), static_cast<unsigned>(evt.channel));
    DebugSessionCapture::appendCaptureTextLine(line);
    ++eventsLogged;
  }
}

}  // namespace
#endif

const uint32_t Track::TICKS_PER_BAR = Config::TICKS_PER_BAR;

TRACK_COLD_MEM __attribute__((noinline)) void Track::catchUpCommittedPlaybackLedgerToPhase(
    uint32_t occupyPhase) {
  // Ledger catch-up only. Clock owns lastTickInLoop, nextEventIndex, send, wrap.
  // FLASHMEM: same RAM1 rule as wrap-tick catch-up — occupy must not consume ITCM.
  Loop& loop = getActiveLoop();
  if (!CommittedPlaybackLedgerCatchUp::shouldApply(loop, occupyPhase)) {
    return;
  }
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(activeLoopIndex);
  if (runtime == nullptr) {
    return;
  }
  CommittedPlaybackLedgerCatchUp::applyOpenClosedInterval(
      runtime->ledger, midiChannel, runtime->mergedMidiEvents.mergedEvents, loop.lastTickInLoop,
      occupyPhase, loop.loopLengthTicks);
}

TRACK_COLD_MEM __attribute__((noinline)) void Track::snapshotOverlapHoldCandidates(
    PendingNote& pending) {
  if (!isOverdubbing()) {
    return;
  }
  Loop& loop = getActiveLoop();
  if (!loop.hasOverdubSourceView()) {
    return;
  }
  const uint32_t loopLength = loop.overdubSourceViewLoopLengthTicks();
  if (loopLength == 0) {
    return;
  }
  // Occupy is ledger lookup at currentTick. No 16-bar hold fill on note-on.
  // Ahead notes still merge at note-off (`ensureOverdubSourceNotesForHold`).
  // Do not call playMidiEvents here: lastTick < occupyPhase can cross session
  // start and commit a wrap on the USB occupy path (214856). Ledger catch-up
  // only: (lastTickInLoop, occupyPhase] via catchUpCommittedPlaybackLedgerToPhase.
  const uint32_t holdStart =
      IntervalProjection::tickPhaseInLoop(pending.startNoteTick, 0, loopLength);
  catchUpCommittedPlaybackLedgerToPhase(holdStart);
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(activeLoopIndex);
  if (runtime == nullptr) {
    pending.overlapNoteIds.clear();
    return;
  }
  loop.collectOverdubNoteOnParticipantIds(pending.note, midiChannel, runtime->ledger,
                                          pending.overlapNoteIds);
  if (pending.overlapNoteIds.size() == 0) {
    loop.collectOverdubSourceHoldParticipantIds(holdStart, pending.note, pending.overlapNoteIds);
  }
  pending.occupancyResolvedNoteIds.clear();
  loop.resolveOccupancyAtNoteOn(pending.channel, pending.note, pending.velocity,
                                pending.overlapNoteIds, holdStart,
                                &pending.occupancyResolvedNoteIds);
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
#if defined(__IMXRT1062__)
  static DMAMEM OccupyObservationScratch sOccupyObservationScratch;
#else
  static OccupyObservationScratch sOccupyObservationScratch;
#endif
  auto& sourceViewIds = sOccupyObservationScratch.sourceViewIds;
  sourceViewIds.clear();
  loop.collectOverdubSourceHoldParticipantIds(holdStart, pending.note, sourceViewIds);
  auto& preparedIds = sOccupyObservationScratch.preparedIds;
  preparedIds.clear();
  const uint32_t observeStartUs = micros();
  const bool prepared =
      loop.tryCollectPreparedPresentNoteIdsAtTick(holdStart, pending.note, preparedIds);
  const uint32_t observeUs = micros() - observeStartUs;
  unsigned onlyA = 0;
  unsigned onlyB = 0;
  uint32_t sourceViewStart = 0;
  uint32_t sourceViewEnd = 0;
  for (const NoteUtils::DisplayNote& note : loop.overdubSourceViewNotes()) {
    if (note.note != pending.note || note.noteId == kInvalidNoteId) {
      continue;
    }
    if (!OverlapNoteIdObservation::displayNotePresentAtHold(note.startTick, note.endTick,
                                                            holdStart, loopLength)) {
      continue;
    }
    sourceViewStart = note.startTick;
    sourceViewEnd = note.endTick;
    break;
  }
  if (prepared) {
    for (size_t i = 0; i < sourceViewIds.size(); ++i) {
      if (!preparedIds.contains(sourceViewIds.at(i))) {
        ++onlyA;
      }
    }
    for (size_t i = 0; i < preparedIds.size(); ++i) {
      if (!sourceViewIds.contains(preparedIds.at(i))) {
        ++onlyB;
      }
    }
  }
  auto& line = sOccupyObservationScratch.line;
  snprintf(line, sizeof(line),
           "#CAP,%lu,DIAG,lcr,part,why=on,from=ledger,pitch=%u,n=%u,a=%u,b=%u,eq=%u,ao=%u,bo=%u,"
           "as=%lu,ae=%lu,hs=%lu,us=%lu",
           static_cast<unsigned long>(micros()), static_cast<unsigned>(pending.note),
           static_cast<unsigned>(pending.overlapNoteIds.size()),
           static_cast<unsigned>(sourceViewIds.size()),
           static_cast<unsigned>(prepared ? preparedIds.size() : 0u),
           (prepared && onlyA == 0 && onlyB == 0) ? 1u : 0u, onlyA, onlyB,
           static_cast<unsigned long>(sourceViewStart),
           static_cast<unsigned long>(sourceViewEnd),
           static_cast<unsigned long>(holdStart),
           static_cast<unsigned long>(observeUs));
  DebugSessionCapture::appendCaptureTextLine(line);
  // Fail loud when nothing is present at the hold: every `141717` hold at phase 0 reported
  // n=0/a=0/b=0 while 768/1536/2304 resolved, and the capture could not say whether the
  // prior pass's row was absent, shaped so present-at-hold misses it, or filtered out.
  if (sourceViewIds.size() == 0) {
    unsigned pitchRows = 0;
    unsigned rowsLogged = 0;
    constexpr unsigned kMaxZeroParticipantRows = 6;
    for (const NoteUtils::DisplayNote& note : loop.overdubSourceViewNotes()) {
      if (note.note != pending.note) {
        continue;
      }
      ++pitchRows;
      if (rowsLogged >= kMaxZeroParticipantRows) {
        continue;
      }
      snprintf(line, sizeof(line), "#CAP,%lu,DIAG,lcr,nopart,row,id=%lu,s=%lu,e=%lu",
               static_cast<unsigned long>(micros()), static_cast<unsigned long>(note.noteId),
               static_cast<unsigned long>(note.startTick),
               static_cast<unsigned long>(note.endTick));
      DebugSessionCapture::appendCaptureTextLine(line);
      ++rowsLogged;
    }
    unsigned mergedOns = 0;
    unsigned mergedLogged = 0;
    for (const MidiEvent& evt : runtime->mergedMidiEvents.mergedEvents) {
      if (!evt.isNoteOn() || evt.data.noteData.note != pending.note) {
        continue;
      }
      ++mergedOns;
      if (mergedLogged >= kMaxZeroParticipantRows) {
        continue;
      }
      snprintf(line, sizeof(line), "#CAP,%lu,DIAG,lcr,nopart,merged,id=%lu,t=%lu",
               static_cast<unsigned long>(micros()), static_cast<unsigned long>(evt.noteId),
               static_cast<unsigned long>(evt.tick));
      DebugSessionCapture::appendCaptureTextLine(line);
      ++mergedLogged;
    }
    snprintf(line, sizeof(line),
             "#CAP,%lu,DIAG,lcr,nopart,pitch=%u,hs=%lu,rows=%u,mons=%u,est=%u,len=%lu,win=%lu,"
             "mrev=%lu,prev=%lu",
             static_cast<unsigned long>(micros()), static_cast<unsigned>(pending.note),
             static_cast<unsigned long>(holdStart), pitchRows, mergedOns,
             loop.hasOverdubSourceView() ? 1u : 0u, static_cast<unsigned long>(loopLength),
             static_cast<unsigned long>(runtime->mergedMidiEvents.windowLengthTicks),
             static_cast<unsigned long>(runtime->mergedMidiEvents.builtFromRevision),
             static_cast<unsigned long>(loop.playbackRevision));
    DebugSessionCapture::appendCaptureTextLine(line);
  }
  // Mismatch only: ledger vs source view disagree on this lane. Passing occupies stay quiet.
  if (pending.overlapNoteIds.size() != sourceViewIds.size()) {
    logOccupyLedgerMismatch(loop, runtime->ledger, runtime->mergedMidiEvents, midiChannel,
                            pending.note, holdStart, loopLength,
                            static_cast<unsigned>(pending.overlapNoteIds.size()),
                            static_cast<unsigned>(sourceViewIds.size()));
  }
#endif
}

TRACK_COLD_MEM __attribute__((noinline)) void Track::collectOverlapHoldPlaybackNoteOn(
    NoteId noteId, uint8_t pitch) {
  if (noteId == kInvalidNoteId || pendingNotes.empty()) {
    return;
  }
  for (auto& entry : pendingNotes) {
    PendingNote& pending = entry.second;
    if (pitch != pending.note) {
      continue;
    }
    (void)pending.overlapNoteIds.insert(noteId);
    OverlapNoteIdSet crossed;
    (void)crossed.insert(noteId);
    Loop& loop = getActiveLoop();
    const uint32_t holdStart = loop.overdubSourceViewLoopLengthTicks() == 0
                                   ? pending.startNoteTick
                                   : IntervalProjection::tickPhaseInLoop(
                                         pending.startNoteTick, 0,
                                         loop.overdubSourceViewLoopLengthTicks());
    loop.resolveOccupancyAtNoteOn(pending.channel, pending.note, pending.velocity, crossed,
                                  holdStart, &pending.occupancyResolvedNoteIds);
  }
}

void Track::startRecording(uint32_t currentTick) {
  // Capture admission, and it sits before every mutation on purpose - including the
  // take reset below, which clears the slot's passes. The required reserves do not
  // depend on the new take's timeline (only their generation stamp does, which the
  // post-reset pass re-applies), so establishing them first means a denied take leaves
  // the slot's pass timeline, the armed pre-roll, the pending-note set, the capture
  // baseline and the track state exactly as they were. Admission succeeds only if every
  // allocation-free-stop prerequisite for this take has been established.
  if (!prewarmPlaybackForSlot(activeLoopIndex, true)) {
    MemoryMonitor::reportPrewarmAdmissionDenied(MemoryMonitor::OperationKind::RecordStart);
    return;
  }
  Loop& loop = getActiveLoop();
  recordCaptureBaselineGeometry_ = {loop.loopLengthTicks, loop.startLoopTick, loop.loopStartTick};
  hasRecordCaptureBaselineGeometry_ = true;
  if (!loop.hasCommittedPasses()) {
    loop.loopLengthTicks = 0;
    loop.loopStartTick = 0;
  }
  // The transfer admission owns external rows needed by the later
  // OVERDUB_STOPPING handoff. It may allocate only while every track is
  // inactive, so it must run before this record command enters RECORDING.
  // Calling this below setState(TRACK_RECORDING) made every fresh take reach
  // PLAYING without the capacity required to begin its first overdub.
  // First-record entry owns the same playback-window preparation used by
  // ordinary playback. This makes bar 1 available before capture starts and
  // leaves the retained view ready for the record-stop handoff.
  // Reset invalidates canonical generations and snapshot ownership. Reserve
  // the new take's resources afterwards, so its later stop can use them.
  ExternalPoolAccounting::beginTag(ExternalPoolAccounting::TagResetPassTimeline);
  loop.resetPassTimeline();
  ExternalPoolAccounting::endTag();
  ExternalPoolAccounting::beginTag(ExternalPoolAccounting::TagViewPrewarm);
  // Re-run against the new take so generation-stamped receipts describe this take.
  // Everything is already reserved by the admission pass above, so this allocates
  // nothing; it exists to re-stamp against the post-reset canonical generation and the
  // now-empty pass set.
  (void)prewarmPlaybackForSlot(activeLoopIndex, true);
  ExternalPoolAccounting::endTag();
  auto preRoll = std::move(armedPreRollNotes);
  if (LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(activeLoopIndex)) {
    // Establish the empty bar-1 playback view before the first accepted input.
    // Later capture updates are still admitted by the canonical capture store;
    // this preparation step must never become a new input rejection policy.
    ExternalPoolAccounting::beginTag(ExternalPoolAccounting::TagPrepareFirstBar);
    const bool firstBarReady = runtime->prepareFirstRecordingBar(Config::TICKS_PER_BAR);
    ExternalPoolAccounting::endTag();
    if (!firstBarReady) {
      logger.log(CAT_TRACK, LOG_WARNING,
                 "First-record bar-1 playback preparation unavailable; capture admission retained");
    }
  }
  if (!setState(TRACK_RECORDING)) {
    armedPreRollNotes = std::move(preRoll);
    return;
  }
  transportStopOutputBarrier_ = false;
  recordAddedNoteOnCount = 0;
  loop.beginCapture(CapturePhase::Record);
  // Establish raw recording coordinates before pre-roll enters the same input
  // path. Using the previous slot origin here misplaces its first notes.
  loop.startLoopTick = currentTick;
  loop.loopStartTick = 0;
  projectionCycleStartTick = static_cast<int32_t>(currentTick);
  pendingNotes.clear();
  for (const auto& entry : preRoll) {
    const PendingNote& pn = entry.second;
    pendingNotes[entry.first] =
        PendingNote{pn.note, pn.channel, currentTick, pn.velocity};
    recordMidiEvents(midi::NoteOn, pn.channel, pn.note, pn.velocity, currentTick);
  }
  loop.nextEventIndex = 0;    // so playback will start from the top
  loop.lastTickInLoop = 0;
  // Fresh take: a stale loop-start window (prior loop-start edit or SD restore) must not
  // shift the new notes on the piano roll — playback and grid use raw storage ticks.
  loop.loopStartTick = 0;


  invalidateCaches();
  SC_REC_START(activeLoopIndex, currentTick);
  logger.logTrackEvent("Recording started", currentTick, "startLoopTick=%lu loopStart=0",
                       static_cast<unsigned long>(loop.startLoopTick));
}

uint32_t Track::quantizeStart(uint32_t original) const {
  return (original / TICKS_PER_BAR) * TICKS_PER_BAR;
}

void Track::shiftMidiEvents(int32_t offset) {
  Loop& loop = getActiveLoop();
  loop.shiftActiveCapturePassTicks(offset);
  invalidateCaches();
}

uint32_t Track::findLastEventTick() const {
  const Loop& loop = getActiveLoop();
  MidiEventVec flat;
  loop.mergeActiveCapturePasses(flat);
  uint32_t last = 0;
  for (const auto& evt : flat) {
    last = std::max(last, evt.tick);
  }
  return last;
}

uint32_t Track::computeLoopLengthTicks(uint32_t lastTick) const {
  return RecordStopLength::computeLoopLengthTicks(lastTick);
}

TRACK_CAPTURE_ADMISSION_MEM uint32_t Track::capturePhaseTick(uint32_t absTick) const {
  const Loop& loop = getActiveLoop();
  if (loop.loopLengthTicks == 0 || loop.startLoopTick == UINT32_MAX) {
    if (absTick >= loop.startLoopTick) {
      return absTick - loop.startLoopTick;
    }
    return 0;
  }
  if (isOverdubbing() || hasPendingDeferredOverdubStop() || (isRecording() && isPlaying())) {
    return IntervalProjection::tickPhaseInProjectionCycle(
        absTick, projectionCycleStartTick, loop.loopLengthTicks);
  }
  if (isRecording() && !isPlaying()) {
    return absTick >= loop.startLoopTick ? absTick - loop.startLoopTick : 0;
  }
  return tickPhaseInLoop(absTick, loop.startLoopTick, loop.loopLengthTicks);
}

TRACK_CAPTURE_ADMISSION_MEM bool Track::appendCaptureNoteOffAtPhase(
    uint8_t channel, uint8_t note, uint32_t phaseTick) {
  Loop& loop = getActiveLoop();
  if (!loop.captureActive()) {
    return false;
  }
  uint32_t tickRelative = phaseTick;
  if (loop.loopLengthTicks > 0 && tickRelative >= loop.loopLengthTicks) {
    tickRelative = loop.loopLengthTicks - 1;
  }
  MidiEvent newEvt = MidiEvent::NoteOff(tickRelative, channel, note, 0);
  // Resolve the closure identity against the interval that owns the reservation:
  // the admitted job's sealed extent when one is bound, otherwise the producer.
  // A capture that has been sealed no longer has producer rows to scan.
  NoteId closedNoteId = kInvalidNoteId;
  if (loop.findLastCaptureNoteOnIdentity(channel, note, closedNoteId)) {
    newEvt.noteId = closedNoteId;
  }
  CaptureAdmissionStaging* staging = nullptr;
  CaptureNoteReservation* reservation = nullptr;
  if ((isOverdubbing() || hasPendingDeferredOverdubStop()) && newEvt.noteId != kInvalidNoteId) {
    LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(activeLoopIndex);
    // A physical closure of an already-open note belongs to whichever role owns
    // that reservation: the pending stop job's frozen staging while it is
    // finishing, otherwise the live ingress staging of the open interval.
    CaptureAdmissionStaging* viewSet = runtime != nullptr
        ? (hasPendingDeferredOverdubStop() ? runtime->captureStagingIfPrewarmed()
                                           : runtime->liveCaptureStagingIfPrewarmed())
        : nullptr;
    if (viewSet != nullptr) {
      staging = viewSet;
      reservation = staging->matchingOpenReservation(newEvt.noteId, channel, note);
    }
  }
  if (reservation != nullptr) {
    return appendReservedCaptureNoteOffAtPhase(*reservation, phaseTick);
  }
  const CaptureAppendResult appendResult = loop.appendCaptureEventWithResult(newEvt);
  if (!appendResult.accepted) {
    return false;
  }
  return true;
}

TRACK_CAPTURE_ADMISSION_MEM bool Track::appendReservedCaptureNoteOffAtPhase(
    CaptureNoteReservation& reservation, uint32_t phaseTick) {
  Loop& loop = getActiveLoop();
  if (!reservation.open || !loop.captureActive()) {
    return false;
  }
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(activeLoopIndex);
  CaptureAdmissionStaging* viewSet = runtime != nullptr
      ? (hasPendingDeferredOverdubStop() ? runtime->captureStagingIfPrewarmed()
                                         : runtime->liveCaptureStagingIfPrewarmed())
      : nullptr;
  CaptureAdmissionStaging* staging =
      viewSet != nullptr ? viewSet : nullptr;
  return staging && appendReservedCaptureNoteOffForOwner(loop, *staging, reservation, phaseTick);
}

TRACK_CAPTURE_ADMISSION_MEM bool Track::appendReservedCaptureNoteOffForOwner(
    Loop& loop, CaptureAdmissionStaging& ownerStaging,
    CaptureNoteReservation& reservation, uint32_t phaseTick) {
  auto* staging = &ownerStaging;
  if (!reservation.open || !loop.captureActive() ||
      staging->matchingOpenReservation(reservation.noteId, reservation.channel,
                                       reservation.note) != &reservation) return false;
  uint32_t tickRelative = phaseTick;
  if (loop.loopLengthTicks > 0 && tickRelative >= loop.loopLengthTicks) {
    tickRelative = loop.loopLengthTicks - 1;
  }
  MidiEvent terminal =
      MidiEvent::NoteOff(tickRelative, reservation.channel, reservation.note, 0);
  terminal.noteId = reservation.noteId;
  const CaptureAppendResult appendResult = loop.appendCaptureEventWithResult(terminal);
  if (!appendResult.accepted) {
    return false;
  }
  bool retainPair = true;
  if (noteMinLengthRemoveEnabled && noteMinLengthTicks != 0 &&
      reservation.noteOnStagingIndex < staging->storage.eventCount) {
    const MidiEvent& noteOn = staging->storage.events[reservation.noteOnStagingIndex];
    retainPair = terminal.tick < noteOn.tick ||
                 terminal.tick - noteOn.tick >= noteMinLengthTicks;
  }
  if (!staging->appendReservedNoteOff(terminal, retainPair)) {
    return false;
  }
  if (!staging->frozen) {
    ++staging->captureGeneration;
  }
  return true;
}

TRACK_CAPTURE_ADMISSION_MEM void Track::finalizePendingNotes(uint32_t offAbsTick) {
  const uint32_t phaseTick = capturePhaseTick(offAbsTick);
  Loop& loop = getActiveLoop();

  std::vector<std::pair<uint8_t, uint8_t>> toClose;
  toClose.reserve(pendingNotes.size());
  for (const auto& kv : pendingNotes) {
    toClose.push_back(kv.first);
  }

  size_t pendingFinalized = 0;
  size_t captureNoteOffsAppended = 0;
  for (const auto& key : toClose) {
    const uint8_t note = key.first;
    const uint8_t channel = key.second;
    ++pendingFinalized;

    if (isOverdubbing() || hasPendingDeferredOverdubStop()) {
      const auto pendingIt = pendingNotes.find(key);
      const uint32_t pendingOnAbsTick =
          pendingIt != pendingNotes.end() ? pendingIt->second.startNoteTick : offAbsTick;
      const uint32_t pendingOnPhaseTick = capturePhaseTick(pendingOnAbsTick);
      // If a performer NoteOff already landed in capture but pending did not clear (dedup / warning),
      // do not synthesize a stop-time off that truncates the earlier note.
      if (loop.captureHasNoteOffAfter(channel, note, pendingOnPhaseTick)) {
        pendingNotes.erase(key);
        continue;
      }
    }

    if (appendCaptureNoteOffAtPhase(channel, note, phaseTick)) {
#if defined(SESSION_CAPTURE)
      uint32_t storageTick = phaseTick;
      if (loop.loopLengthTicks > 0 && storageTick >= loop.loopLengthTicks) {
        storageTick = loop.loopLengthTicks - 1;
      }
      logOverdubCaptureCoordinate(*this, offAbsTick, storageTick, channel, note);
#endif
      if (isOverdubbing() && loop.hasOverdubSourceView()) {
        const auto overlapIt = pendingNotes.find(key);
        const OverlapNoteIdSet& overlapNoteIds = (overlapIt != pendingNotes.end())
                                                     ? overlapIt->second.overlapNoteIds
                                                     : kEmptyOverlapNoteIds;
        const OverlapNoteIdSet* occupancyResolvedNoteIds =
            (overlapIt != pendingNotes.end()) ? &overlapIt->second.occupancyResolvedNoteIds
                                              : nullptr;
        const uint8_t velocity =
            (overlapIt != pendingNotes.end()) ? overlapIt->second.velocity : 0;
        const LoopEventStore& capture = loop.capture.store;
        for (size_t i = capture.size(); i > 0; --i) {
          const MidiEvent& prior = capture.at(i - 1);
          if (!prior.isNoteOn() || prior.channel != channel ||
              prior.data.noteData.note != note) {
            continue;
          }
          (void)loop.accumulatePendingNoteChangesForIncomingNote(
              channel, note, velocity, prior.tick, phaseTick, prior.noteId, overlapNoteIds,
              occupancyResolvedNoteIds);
          break;
        }
      }
      pendingNotes.erase(key);
      ++captureNoteOffsAppended;
    } else {
      pendingNotes.erase(key);
    }
  }

#if defined(SESSION_CAPTURE)
  logger.info("Stop finalize pending: finalized=%u capture_offs=%u phase=%u",
              static_cast<unsigned>(pendingFinalized),
              static_cast<unsigned>(captureNoteOffsAppended),
              phaseTick);
#endif

  invalidateCaches();
}

TRACK_CAPTURE_ADMISSION_MEM __attribute__((noinline)) bool Track::recordMidiEvents(
    midi::MidiType type, byte channel, byte data1, byte data2, uint32_t currentTick) {
  if ((isRecording() && !isPlaying()) || isOverdubbing()) {
    Loop& loop = getActiveLoop();
    uint32_t tickRelative;

    if (isRecording() && !isPlaying()) {
      tickRelative = currentTick - loop.startLoopTick;

    } else if (isOverdubbing()) {
      if (loop.loopLengthTicks == 0) return false;
      tickRelative = capturePhaseTick(currentTick);

    } else {
      return false;
    }

    // Build the new event first (needed for duplicate check)
    MidiEvent newEvt;
    bool eventAdded = false;
    switch (type) {
      case midi::NoteOn:
        newEvt = MidiEvent::NoteOn(tickRelative, channel, data1, data2);
        newEvt.noteId = loop.allocateNoteId();
        eventAdded = true;
        break;
      case midi::NoteOff:
        newEvt = MidiEvent::NoteOff(tickRelative, channel, data1, data2);
        eventAdded = true;
        break;
      case midi::ControlChange:
        newEvt = MidiEvent::ControlChange(tickRelative, channel, data1, data2);
        eventAdded = true;
        break;
      case midi::ProgramChange:
        newEvt = MidiEvent::ProgramChange(tickRelative, channel, data1);
        eventAdded = true;
        break;
      case midi::AfterTouchChannel:
        newEvt = MidiEvent::ChannelAftertouch(tickRelative, channel, data1);
        eventAdded = true;
        break;
      case midi::PitchBend:
        newEvt = MidiEvent::PitchBend(tickRelative, channel, (int16_t)((data2 << 7) | data1));
        eventAdded = true;
        break;
      default:
        return false;
    }

    if (!eventAdded) return false;

    if (type == midi::NoteOff) {
      const LoopEventStore& capture = loop.capture.store;
      for (size_t i = capture.size(); i > 0; --i) {
        const MidiEvent& prior = capture.at(i - 1);
        if (!prior.isNoteOn() || prior.channel != channel || prior.data.noteData.note != data1) {
          continue;
        }
        newEvt.noteId = prior.noteId;
        if (isOverdubbing() && loop.loopLengthTicks > 0) {
          const bool wrappedHeadOff =
              tickRelative < prior.tick &&
              pendingNotes.find({data1, channel}) != pendingNotes.end();
          if (tickRelative <= prior.tick && !wrappedHeadOff) {
            tickRelative = prior.tick + 1;
            newEvt.tick = tickRelative;
          }
        }
        break;
      }
    }

    if (loop.loopLengthTicks > 0 && tickRelative >= loop.loopLengthTicks &&
        !(isRecording() && !isPlaying())) {
      tickRelative = loop.loopLengthTicks - 1;
      newEvt.tick = tickRelative;
    }

    // Overdub admission must reserve the complete NoteOn lifetime before the
    // mutable capture store changes.  Two staging/bridge rows cover On+Off;
    // the stop grain receives one reserved closure credit.  Runtime views are
    // prewarmed at setup/load and record start, never from this MIDI path.
    CaptureAdmissionStaging* staging = nullptr;
    CaptureNoteReservation* reservation = nullptr;
    bool independentEventReservation = false;
    if (isOverdubbing()) {
      LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(activeLoopIndex);
      CaptureAdmissionStaging* viewSet =
          runtime != nullptr ? runtime->liveCaptureStagingIfPrewarmed() : nullptr;
      if (viewSet == nullptr) {
        logger.log(CAT_TRACK, LOG_WARNING,
                   "Capture admission rejected: runtime staging unavailable ch=%u note=%u",
                   static_cast<unsigned>(channel), static_cast<unsigned>(data1));
        return false;
      }
      staging = viewSet;
      if (type == midi::NoteOn) {
        // noteOn() may have reserved this exact identity before it created a
        // PendingNote. Direct callers retain the same admission rule here.
        reservation = staging->matchingOpenReservation(newEvt.noteId, channel, data1);
        if (reservation == nullptr) {
          reservation = staging->reserve(newEvt.noteId, channel, data1, 2, 2, 1);
        }
        if (reservation == nullptr) {
          logger.log(CAT_TRACK, LOG_WARNING,
                     "Capture admission rejected: NoteOn lifetime unreservable ch=%u note=%u",
                     static_cast<unsigned>(channel), static_cast<unsigned>(data1));
          return false;
        }
      } else if (type == midi::NoteOff) {
        reservation = staging->matchingOpenReservation(newEvt.noteId, channel, data1);
        if (reservation == nullptr) {
          logger.log(CAT_TRACK, LOG_WARNING,
                     "Capture admission rejected: unmatched NoteOff ch=%u note=%u",
                     static_cast<unsigned>(channel), static_cast<unsigned>(data1));
          return false;
        }
      } else {
        // Every accepted controller/program/pressure event must enter the
        // fixed candidate too. Unlike a NoteOn it has no terminal closure, so
        // reserve its one staging row and bridge row immediately.
        independentEventReservation = staging->reserveIndependentEvent();
        if (!independentEventReservation) {
          logger.log(CAT_TRACK, LOG_WARNING,
                     "Capture admission rejected: event staging unavailable type=%u ch=%u data=%u",
                     static_cast<unsigned>(type), static_cast<unsigned>(channel),
                     static_cast<unsigned>(data1));
          return false;
        }
      }
    }

#if RUNTIME_TIMING_ENABLED
    const uint32_t appendStartUs = micros();
#endif
    const CaptureAppendResult appendResult = loop.appendCaptureEventWithResult(newEvt);
#if RUNTIME_TIMING_ENABLED
    RUNTIME_TIMING_ADD_NOTE_APPEND(micros() - appendStartUs);
#endif
    if (!appendResult.accepted) {
      if (type == midi::NoteOn && staging != nullptr && reservation != nullptr) {
        staging->close(*reservation);
      }
      if (independentEventReservation && staging != nullptr) {
        staging->releaseIndependentEvent();
      }
      logger.log(CAT_TRACK, LOG_WARNING,
                 "Capture append failed (%s) ch=%u note=%u",
                 captureAppendDenyReasonLabel(appendResult.reason),
                 static_cast<unsigned>(channel), static_cast<unsigned>(data1));
      MemoryMonitor::notifyCaptureAppendFailed(millis());
#if defined(SESSION_CAPTURE)
      logCaptureAppendDeny(loop, appendResult, channel, data1, newEvt.tick);
#endif
      return false;
    }

    bool retainStagedPair = true;
    if (type == midi::NoteOff && reservation != nullptr && noteMinLengthRemoveEnabled &&
        noteMinLengthTicks != 0 && reservation->noteOnStagingIndex < staging->storage.eventCount) {
      const MidiEvent& noteOn = staging->storage.events[reservation->noteOnStagingIndex];
      retainStagedPair = newEvt.tick < noteOn.tick ||
                         newEvt.tick - noteOn.tick >= noteMinLengthTicks;
    }
    const bool stagingAccepted =
        staging == nullptr ||
        (type == midi::NoteOff && reservation != nullptr
             ? staging->appendReservedNoteOff(newEvt, retainStagedPair)
             : staging->storage.append(newEvt));
    if (stagingAccepted && staging != nullptr && type == midi::NoteOn && reservation != nullptr) {
      reservation->noteOnStagingIndex = staging->storage.eventCount - 1;
    }
    if (!stagingAccepted) {
      // This is an invariant breach: NoteOn reservation proved capacity before
      // the canonical append.  Keep the canonical capture and reservation for
      // STOPPED recovery; do not silently release its required closing event.
      logger.log(CAT_TRACK, LOG_ERROR,
                 "Capture staging invariant breach after canonical append ch=%u note=%u",
                 static_cast<unsigned>(channel), static_cast<unsigned>(data1));
      return false;
    }
    if (staging != nullptr) {
      // The candidate identity advances only after canonical and fixed staging
      // storage both contain the accepted event.  A failed staging append is
      // an invariant breach and must not advertise a partially frozen prefix.
      ++staging->captureGeneration;
    }

#if defined(SESSION_CAPTURE)
    if (isOverdubbing() && (type == midi::NoteOn || type == midi::NoteOff)) {
      logOverdubCaptureCoordinate(*this, currentTick, newEvt.tick, channel, data1);
    }
#endif

    if (type == midi::NoteOn) {
      ++recordAddedNoteOnCount;
    }

    // G2: evaluate completed overdub notes against overdubSourceView → pending delta.
    if (type == midi::NoteOff && isOverdubbing() && loop.hasOverdubSourceView()) {
      const LoopEventStore& capture = loop.capture.store;
      for (size_t i = capture.size(); i > 0; --i) {
        const MidiEvent& prior = capture.at(i - 1);
        if (!prior.isNoteOn() || prior.channel != channel ||
            prior.data.noteData.note != data1) {
          continue;
        }
#if RUNTIME_TIMING_ENABLED
        const uint32_t noteChangeStartUs = micros();
#endif
        const auto pendingIt = pendingNotes.find({data1, channel});
        const OverlapNoteIdSet& overlapNoteIds = (pendingIt != pendingNotes.end())
                                                     ? pendingIt->second.overlapNoteIds
                                                     : kEmptyOverlapNoteIds;
        const OverlapNoteIdSet* occupancyResolvedNoteIds =
            (pendingIt != pendingNotes.end()) ? &pendingIt->second.occupancyResolvedNoteIds
                                              : nullptr;
        (void)loop.accumulatePendingNoteChangesForIncomingNote(
            channel, data1, prior.data.noteData.velocity, prior.tick, newEvt.tick, prior.noteId,
            overlapNoteIds, occupancyResolvedNoteIds);
#if RUNTIME_TIMING_ENABLED
        RUNTIME_TIMING_ADD_NOTE_CHANGE(micros() - noteChangeStartUs);
#endif
        break;
      }
    }
    return true;
  }
  return false;
}

void Track::noteOn(uint8_t channel, uint8_t note, uint8_t velocity, uint32_t tick) {
  if (ignorePlaybackMidiInput) return;  // Ignore playback-triggered MIDI events

  if (trackState == TRACK_ARMED) {
    armedPreRollNotes[{note, channel}] = PendingNote{note, channel, tick, velocity};
    return;
  }

  if (trackState == TRACK_RECORDING || trackState == TRACK_OVERDUBBING) {
    // Single slot per (note, channel). A second live NoteOn is capture-admission
    // reject only — MIDI thru already sent it. Do not replace the pending start
    // or append a second On (`202226` On@435, On@520, Off@529).
    const auto key = std::make_pair(note, channel);
    const auto pendingIt = pendingNotes.find(key);
    if (pendingIt != pendingNotes.end()) {
      SC_PENDING_SAME_PITCH_NOTE_ON_REJECT(note, channel, pendingIt->second.startNoteTick, tick);
      return;
    }

    // An overdub NoteOn is admissible only if its terminal NoteOff, bridge,
    // staging, and deferred-stop credits are reserved before this function
    // creates any pending-note or preview state. `recordMidiEvents()` consumes
    // this exact reservation when it assigns the canonical identity.
    if (trackState == TRACK_OVERDUBBING) {
      Loop& loop = getActiveLoop();
      LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(activeLoopIndex);
      CaptureAdmissionStaging* viewSet =
          runtime != nullptr ? runtime->liveCaptureStagingIfPrewarmed() : nullptr;
      CaptureAdmissionStaging* staging =
          viewSet != nullptr ? viewSet : nullptr;
      if (staging == nullptr ||
          staging->reserve(loop.peekNextNoteId(), channel, note, 2, 2, 1) == nullptr) {
        return;
      }
    }
    PendingNote pending{note, channel, tick, velocity};
    snapshotOverlapHoldCandidates(pending);
    pendingNotes[key] = pending;

    if (!recordMidiEvents(midi::NoteOn, channel, note, velocity, tick)) {
      // Rejected NoteOns must not leave pending/preview/ledger capture state.
      pendingNotes.erase(key);
    }
  }
}

void Track::noteOff(uint8_t channel, uint8_t note, uint8_t velocity, uint32_t tick) {
  if (ignorePlaybackMidiInput) return;

  if (trackState == TRACK_ARMED) {
    armedPreRollNotes.erase({note, channel});
    return;
  }

  if (trackState == TRACK_RECORDING || trackState == TRACK_OVERDUBBING) {
    auto key = std::make_pair(note, channel);
    auto it = pendingNotes.find(key);
    if (it != pendingNotes.end()) {
      if (recordMidiEvents(midi::NoteOff, channel, note, 0, tick)) {
        pendingNotes.erase(it);
      }
    } else {
      logger.log(CAT_MIDI, LOG_WARNING, "NoteOff for note %d on ch %d with no matching NoteOn",
                 note, channel);
    }
    return;
  }

  // After stop/finalize: pending already closed; ignore late physical release.
}
