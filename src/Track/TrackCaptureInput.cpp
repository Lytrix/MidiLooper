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
#include "Utils/DebugSessionCapture.h"
#include "Utils/IntervalProjection.h"
#include "OverlapNoteIdObservation.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/MemoryPressurePolicy.h"
#include "Utils/RecordStopLength.h"
#include "Utils/RuntimeTimingTelemetry.h"

#if defined(SESSION_CAPTURE)
static void logCaptureAppendDeny(const Loop& loop, const CaptureAppendResult& result,
                                 uint8_t channel, uint8_t note, uint32_t tick) {
  SC_CAPTURE_APPEND_DENY(captureAppendDenyReasonLabel(result.reason),
                           LoopEventStore::freeChunkCount(), LoopEventStore::usedChunkCount(),
                           memoryPressureLevelName(MemoryMonitor::getAdvisoryPressureLevel()),
                           channel, note, tick, loop.hasPendingCapturePass() ? 1 : 0);
}
#endif

const uint32_t Track::TICKS_PER_BAR = Config::TICKS_PER_BAR;

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
  const uint32_t occupyPhase = capturePhaseTick(pending.startNoteTick);
  if (loop.lastTickInLoop != UINT32_MAX && loop.lastTickInLoop < occupyPhase) {
    playMidiEvents(pending.startNoteTick, playbackEmitMidiOutput_);
  }
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(activeLoopIndex);
  if (runtime == nullptr) {
    pending.overlapNoteIds.clear();
    return;
  }
  loop.collectOverdubNoteOnParticipantIds(pending.note, midiChannel, runtime->ledger,
                                          pending.overlapNoteIds);
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
  const uint32_t holdStart = IntervalProjection::tickPhaseInLoop(pending.startNoteTick, 0, loopLength);
  OverlapNoteIdSet sourceViewIds;
  loop.collectOverdubSourceHoldParticipantIds(holdStart, pending.note, sourceViewIds);
  OverlapNoteIdSet preparedIds;
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
  char line[256];
  snprintf(line, sizeof(line),
           "#CAP,%lu,DIAG,lcr,part,why=on,from=ledger,pitch=%u,n=%u,a=%u,b=%u,eq=%u,ao=%u,bo=%u,"
           "as=%lu,ae=%lu,us=%lu",
           static_cast<unsigned long>(micros()), static_cast<unsigned>(pending.note),
           static_cast<unsigned>(pending.overlapNoteIds.size()),
           static_cast<unsigned>(sourceViewIds.size()),
           static_cast<unsigned>(prepared ? preparedIds.size() : 0u),
           (prepared && onlyA == 0 && onlyB == 0) ? 1u : 0u, onlyA, onlyB,
           static_cast<unsigned long>(sourceViewStart),
           static_cast<unsigned long>(sourceViewEnd),
           static_cast<unsigned long>(observeUs));
  DebugSessionCapture::appendCaptureTextLine(line);
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
  }
}

void Track::startRecording(uint32_t currentTick) {
  Loop& loop = getActiveLoop();
  recordCaptureBaselineGeometry_ = {loop.loopLengthTicks, loop.startLoopTick, loop.loopStartTick};
  hasRecordCaptureBaselineGeometry_ = true;
  if (!loop.hasCommittedPasses()) {
    loop.loopLengthTicks = 0;
    loop.loopStartTick = 0;
  }
  auto preRoll = std::move(armedPreRollNotes);
  if (!setState(TRACK_RECORDING)) {
    armedPreRollNotes = std::move(preRoll);
    return;
  }
  recordAddedNoteOnCount = 0;
  loop.resetPassTimeline();
  loop.beginCapture(CapturePhase::Record);
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

  // Stamp the new start tick quantized to a beat.
  loop.startLoopTick = currentTick;
  projectionCycleStartTick = static_cast<int32_t>(currentTick);

  invalidateCaches();
  // Prewarm playback runtime while heap headroom is higher than at record-stop tail;
  // stopRecording must not be the first trySlot on this slot (null-deref on alloc failure).
  prewarmPlaybackForSlot(activeLoopIndex);
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

uint32_t Track::capturePhaseTick(uint32_t absTick) const {
  const Loop& loop = getActiveLoop();
  if (loop.loopLengthTicks == 0 || loop.startLoopTick == UINT32_MAX) {
    if (absTick >= loop.startLoopTick) {
      return absTick - loop.startLoopTick;
    }
    return 0;
  }
  if (isOverdubbing() || (isRecording() && isPlaying())) {
    return IntervalProjection::tickPhaseInProjectionCycle(
        absTick, projectionCycleStartTick, loop.loopLengthTicks);
  }
  if (isRecording() && !isPlaying()) {
    return absTick >= loop.startLoopTick ? absTick - loop.startLoopTick : 0;
  }
  return tickPhaseInLoop(absTick, loop.startLoopTick, loop.loopLengthTicks);
}

bool Track::appendCaptureNoteOffAtPhase(uint8_t channel, uint8_t note, uint32_t phaseTick) {
  Loop& loop = getActiveLoop();
  if (!loop.captureActive()) {
    return false;
  }
  uint32_t tickRelative = phaseTick;
  if (loop.loopLengthTicks > 0 && tickRelative >= loop.loopLengthTicks) {
    tickRelative = loop.loopLengthTicks - 1;
  }
  const MidiEvent newEvt = MidiEvent::NoteOff(tickRelative, channel, note, 0);
  const CaptureAppendResult appendResult = loop.appendCaptureEventWithResult(newEvt);
  if (!appendResult.accepted) {
    return false;
  }
  return true;
}

void Track::finalizePendingNotes(uint32_t offAbsTick) {
  const uint32_t phaseTick = capturePhaseTick(offAbsTick);
  Loop& loop = getActiveLoop();

  std::vector<std::pair<uint8_t, uint8_t>> toClose;
  toClose.reserve(pendingNotes.size());
  for (const auto& kv : pendingNotes) {
    toClose.push_back(kv.first);
  }

  size_t pendingFinalized = 0;
  size_t captureNoteOffsAppended = 0;
  size_t overlapCaptureRestored = 0;
  for (const auto& key : toClose) {
    const uint8_t note = key.first;
    const uint8_t channel = key.second;
    ++pendingFinalized;

    if (isOverdubbing()) {
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
      // G2: when overdubSourceView owns overlap resolution, do not drop the Add.
      // shouldRestoreCommittedOverlapOnOverdubStop never materializes; it stays
      // false so a missing-view session synthesizes NoteOff instead.
      if (!loop.hasOverdubSourceView() &&
          shouldRestoreCommittedOverlapOnOverdubStop(loop, note, pendingOnPhaseTick, phaseTick)) {
        if (loop.removeOpenCaptureNoteOn(channel, note)) {
          ++overlapCaptureRestored;
        }
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
        static const OverlapNoteIdSet kEmptyOverlapNoteIds{};
        const OverlapNoteIdSet& overlapNoteIds = (overlapIt != pendingNotes.end())
                                                     ? overlapIt->second.overlapNoteIds
                                                     : kEmptyOverlapNoteIds;
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
              channel, note, velocity, prior.tick, phaseTick, prior.noteId, overlapNoteIds);
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
  logger.info("Stop finalize pending: finalized=%u capture_offs=%u overlap_restore=%u phase=%u",
              static_cast<unsigned>(pendingFinalized),
              static_cast<unsigned>(captureNoteOffsAppended),
              static_cast<unsigned>(overlapCaptureRestored),
              phaseTick);
#endif

  invalidateCaches();
}

void Track::recordMidiEvents(midi::MidiType type, byte channel, byte data1, byte data2,
                             uint32_t currentTick) {
  if ((isRecording() && !isPlaying()) || isOverdubbing()) {
    Loop& loop = getActiveLoop();
    uint32_t tickRelative;

    if (isRecording() && !isPlaying()) {
      tickRelative = currentTick - loop.startLoopTick;

    } else if (isOverdubbing()) {
      if (loop.loopLengthTicks == 0) return;
      tickRelative = capturePhaseTick(currentTick);

    } else {
      return;
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
        return;
    }

    if (!eventAdded) return;

    if (type == midi::NoteOff && isOverdubbing() && loop.loopLengthTicks > 0) {
      const LoopEventStore& capture = loop.capture.store;
      for (size_t i = capture.size(); i > 0; --i) {
        const MidiEvent& prior = capture.at(i - 1);
        if (!prior.isNoteOn() || prior.channel != channel || prior.data.noteData.note != data1) {
          continue;
        }
        const bool wrappedHeadOff =
            tickRelative < prior.tick &&
            pendingNotes.find({data1, channel}) != pendingNotes.end();
        if (tickRelative <= prior.tick && !wrappedHeadOff) {
          tickRelative = prior.tick + 1;
          newEvt.tick = tickRelative;
        }
        break;
      }
    }

    if (loop.loopLengthTicks > 0 && tickRelative >= loop.loopLengthTicks) {
      tickRelative = loop.loopLengthTicks - 1;
      newEvt.tick = tickRelative;
    }

    const uint32_t appendStartUs = micros();
    const CaptureAppendResult appendResult = loop.appendCaptureEventWithResult(newEvt);
    RuntimeTimingTelemetry::addNoteAppend(micros() - appendStartUs);
    if (!appendResult.accepted) {
      logger.log(CAT_TRACK, LOG_WARNING,
                 "Capture append failed (%s) ch=%u note=%u",
                 captureAppendDenyReasonLabel(appendResult.reason),
                 static_cast<unsigned>(channel), static_cast<unsigned>(data1));
      MemoryMonitor::notifyCaptureAppendFailed(millis());
#if defined(SESSION_CAPTURE)
      logCaptureAppendDeny(loop, appendResult, channel, data1, newEvt.tick);
#endif
      return;
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
        const uint32_t noteChangeStartUs = micros();
        const auto pendingIt = pendingNotes.find({data1, channel});
        static const OverlapNoteIdSet kEmptyOverlapNoteIds{};
        const OverlapNoteIdSet& overlapNoteIds = (pendingIt != pendingNotes.end())
                                                     ? pendingIt->second.overlapNoteIds
                                                     : kEmptyOverlapNoteIds;
        (void)loop.accumulatePendingNoteChangesForIncomingNote(
            channel, data1, prior.data.noteData.velocity, prior.tick, newEvt.tick, prior.noteId,
            overlapNoteIds);
        RuntimeTimingTelemetry::addNoteChange(micros() - noteChangeStartUs);
        break;
      }
    }
  }
}

void Track::noteOn(uint8_t channel, uint8_t note, uint8_t velocity, uint32_t tick) {
  if (ignorePlaybackMidiInput) return;  // Ignore playback-triggered MIDI events

  if (trackState == TRACK_ARMED) {
    armedPreRollNotes[{note, channel}] = PendingNote{note, channel, tick, velocity};
    return;
  }

  if (trackState == TRACK_RECORDING || trackState == TRACK_OVERDUBBING) {
    // Store pending note for later duration fix
    PendingNote pending{note, channel, tick, velocity};
    snapshotOverlapHoldCandidates(pending);
    pendingNotes[{note, channel}] = pending;

    recordMidiEvents(midi::NoteOn, channel, note, velocity, tick);
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
      recordMidiEvents(midi::NoteOff, channel, note, 0, tick);
      pendingNotes.erase(it);
    } else {
      logger.log(CAT_MIDI, LOG_WARNING, "NoteOff for note %d on ch %d with no matching NoteOn",
                 note, channel);
    }
    return;
  }

  // After stop/finalize: pending already closed; ignore late physical release.
}
