//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackInternal.h"

#include <Arduino.h>

#include "ClockManager.h"
#include "EditManager.h"
#include "Globals.h"
#include "TickPhase.h"
#include "Logger.h"
#include "LoopContentResolution.h"
#include "LoopEventStore.h"
#include "SlotLoadSession.h"
#include "StorageManager.h"
#include "Utils/BootRestoreGate.h"
#include "TrackManager.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/InvestigationCaptureLog.h"
#include "Utils/MidiServiceDrain.h"
#include "Utils/RuntimeTimingTelemetry.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/IntervalProjection.h"
#include "Utils/LoopEventValidation.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/NoteUtils.h"
#include "Utils/TrackMem.h"
#include "VisualCache.h"
#include "DisplayManager.h"

#include <cstdio>

extern TrackManager trackManager;

namespace {
#if defined(__IMXRT1062__)
DMAMEM uint32_t gIdleOwnerDeferNextLogMs[3];
#else
uint32_t gIdleOwnerDeferNextLogMs[3];
#endif

/// **Named deferral row** (tasks 2.3/2.4). One row per child per 5 s interval, emitted as Tier-A through
/// the registered `DIAG,idle_owner,` literal so the reduced timing-critical capture flush cannot shed it.
/// The child keeps its request/cursor: the deferral is state, not a retry. `slot` is a small local index
/// (not the registry) so this hot-cold TU does not ODR-use the registry array in `.data`.
void emitIdleOwnerDeferral(uint8_t slot, const char* childName, uint32_t nowMs) {
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
  // The capture emitter exists only on a capture build. Native fixtures exercise the admission decision and
  // the tier literal (`test_capture_line_tier`); the row itself is device evidence.
  if (slot >= 3) {
    return;
  }
  if (gIdleOwnerDeferNextLogMs[slot] != 0 && nowMs < gIdleOwnerDeferNextLogMs[slot]) {
    return;
  }
  gIdleOwnerDeferNextLogMs[slot] = nowMs + 5000u;
  char line[160];
  snprintf(line, sizeof(line),
           "#CAP,%lu,DIAG,idle_owner,defer,child=%s,reason=timing_critical_unbounded",
           static_cast<unsigned long>(micros()), childName);
  DebugSessionCapture::appendCaptureTextLine(line);
#else
  (void)slot;
  (void)childName;
  (void)nowMs;
#endif
}
}  // namespace

#if defined(SESSION_CAPTURE)
namespace {

#if defined(ARDUINO)
bool logContentResolutionDeviceGateOnce(const char* kind, const char* reason) {
  struct Seen {
    const char* kind;
    const char* reason;
  };
  static Seen seen[8]{};
  static uint8_t seenCount = 0;
  for (uint8_t i = 0; i < seenCount; ++i) {
    if (seen[i].kind == kind && seen[i].reason == reason) {
      return false;
    }
  }
  if (seenCount >= 8) {
    return false;
  }
  seen[seenCount].kind = kind;
  seen[seenCount].reason = reason;
  ++seenCount;
  char line[96];
  snprintf(line, sizeof(line), "#CAP,%lu,DIAG,lcr,%s,%s",
           static_cast<unsigned long>(micros()), kind, reason);
  DebugSessionCapture::appendCaptureTextLine(line);
  return true;
}

void logContentResolutionDeviceGateIncomplete(const char* reason) {
  char line[192];
  if (LoopContentResolution::deviceGateFormatIncompleteLine(line, sizeof(line), reason)) {
    DebugSessionCapture::appendCaptureTextLine(line);
  }
}

#include "Utils/InvestigationCaptureLog.h"
TRACK_COLD_MEM bool displayNoteIdentityMatch(const NoteUtils::DisplayNote& a,
                                             const NoteUtils::DisplayNote& b) {
  return a.noteId == b.noteId && a.note == b.note && a.startTick == b.startTick &&
         a.endTick == b.endTick;
}

TRACK_COLD_MEM bool displayNotesMatch(const NoteUtils::DisplayNoteVec& expected,
                                      const NoteUtils::DisplayNoteVec& actual) {
  if (expected.size() != actual.size()) {
    return false;
  }
  for (const NoteUtils::DisplayNote& note : expected) {
    bool found = false;
    for (const NoteUtils::DisplayNote& other : actual) {
      if (displayNoteIdentityMatch(note, other)) {
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
  }
  return true;
}

TRACK_COLD_MEM uint32_t countDisplayNoteMatches(const NoteUtils::DisplayNoteVec& expected,
                                                const NoteUtils::DisplayNoteVec& actual,
                                                const NoteUtils::DisplayNote** firstMissing) {
  if (firstMissing != nullptr) {
    *firstMissing = nullptr;
  }
  uint32_t found = 0;
  for (const NoteUtils::DisplayNote& note : expected) {
    bool matched = false;
    for (const NoteUtils::DisplayNote& other : actual) {
      if (displayNoteIdentityMatch(note, other)) {
        matched = true;
        break;
      }
    }
    if (matched) {
      ++found;
    } else if (firstMissing != nullptr && *firstMissing == nullptr) {
      *firstMissing = &note;
    }
  }
  return found;
}

TRACK_COLD_MEM const NoteUtils::DisplayNote* findDisplayNoteGeometry(
    const NoteUtils::DisplayNoteVec& notes, uint8_t pitch, uint32_t startTick, uint32_t endTick) {
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.note == pitch && note.startTick == startTick && note.endTick == endTick) {
      return &note;
    }
  }
  return nullptr;
}

TRACK_COLD_MEM void logDisplayNoteDiffs(const char* tag, const NoteUtils::DisplayNoteVec& expected,
                                        const NoteUtils::DisplayNoteVec& actual) {
  uint32_t logged = 0;
  for (const NoteUtils::DisplayNote& note : expected) {
    bool matched = false;
    const NoteUtils::DisplayNote* sameId = nullptr;
    for (const NoteUtils::DisplayNote& other : actual) {
      if (other.noteId == note.noteId) {
        sameId = &other;
      }
      if (displayNoteIdentityMatch(note, other)) {
        matched = true;
        break;
      }
    }
    if (matched) {
      continue;
    }
    const NoteUtils::DisplayNote* geom = findDisplayNoteGeometry(actual, note.note, note.startTick,
                                                                 note.endTick);
    char line[224];
    if (sameId != nullptr) {
      snprintf(line, sizeof(line),
               "#CAP,%lu,DIAG,lcr,vch,%s,id=%lu,n=%u,s=%lu,e=%lu,sameid_n=%u,sameid_s=%lu,sameid_e=%lu",
               static_cast<unsigned long>(micros()), tag,
               static_cast<unsigned long>(note.noteId), static_cast<unsigned>(note.note),
               static_cast<unsigned long>(note.startTick),
               static_cast<unsigned long>(note.endTick), static_cast<unsigned>(sameId->note),
               static_cast<unsigned long>(sameId->startTick),
               static_cast<unsigned long>(sameId->endTick));
    } else if (geom != nullptr) {
      snprintf(line, sizeof(line),
               "#CAP,%lu,DIAG,lcr,vch,%s,id=%lu,n=%u,s=%lu,e=%lu,alt=%lu",
               static_cast<unsigned long>(micros()), tag,
               static_cast<unsigned long>(note.noteId), static_cast<unsigned>(note.note),
               static_cast<unsigned long>(note.startTick),
               static_cast<unsigned long>(note.endTick),
               static_cast<unsigned long>(geom->noteId));
    } else {
      snprintf(line, sizeof(line), "#CAP,%lu,DIAG,lcr,vch,%s,id=%lu,n=%u,s=%lu,e=%lu",
               static_cast<unsigned long>(micros()), tag,
               static_cast<unsigned long>(note.noteId), static_cast<unsigned>(note.note),
               static_cast<unsigned long>(note.startTick),
               static_cast<unsigned long>(note.endTick));
    }
    DebugSessionCapture::appendCaptureTextLine(line);
    ++logged;
    if (logged >= 8) {
      break;
    }
  }
}

TRACK_COLD_MEM void logPreparedDisplayRangeSample(Loop& loop) {
  const uint32_t padTicks = Config::TICKS_PER_BAR;
  const uint32_t windowLength =
      loop.loopLengthTicks > padTicks * 2u ? padTicks * 2u : loop.loopLengthTicks;
  if (windowLength == 0) {
    return;
  }
  SessionMidiEventVec prepared;
  ResolutionCostCounters windowCounters;
  const uint32_t windowStart = 0;
  if (!LoopContentResolution::tryResolvePreparedWindow(
          loop.passes.editPasses, loop.loopLengthTicks, windowStart, windowLength,
          loop.playbackRevision, prepared, &windowCounters)) {
    return;
  }
  const uint32_t reconstructStartUs = micros();
  const NoteUtils::DisplayNoteVec preparedNotes =
      NoteUtils::reconstructDisplayNotes(prepared, loop.loopLengthTicks, false, false);
  const uint32_t reconstructUs = micros() - reconstructStartUs;
  const uint32_t oracleStartUs = micros();
  SessionMidiEventVec oracle;
  loop.copyEffectiveCommittedEventsInRange(oracle, windowStart, windowLength);
  const NoteUtils::DisplayNoteVec oracleNotes =
      NoteUtils::reconstructDisplayNotes(oracle, loop.loopLengthTicks, false, false);
  const uint32_t oracleUs = micros() - oracleStartUs;
  const uint32_t nativeStartUs = micros();
  SessionMidiEventVec nativeFull;
  loop.copyEffectiveCommittedEvents(nativeFull);
  SessionMidiEventVec nativeWindow;
  DisplayWindowUtils::filterMidiEventsToWindow(nativeFull, nativeWindow, windowStart, windowLength,
                                               loop.loopLengthTicks);
  const NoteUtils::DisplayNoteVec nativeNotes =
      NoteUtils::reconstructDisplayNotes(nativeWindow, loop.loopLengthTicks, false, false);
  const uint32_t nativeUs = micros() - nativeStartUs;
  const uint32_t windowUs = static_cast<uint32_t>(windowCounters.elapsedMicros);
  const uint32_t foundInPrepared = countDisplayNoteMatches(nativeNotes, preparedNotes, nullptr);
  const uint32_t foundInNative = countDisplayNoteMatches(preparedNotes, nativeNotes, nullptr);
  const uint32_t miss = static_cast<uint32_t>(nativeNotes.size()) - foundInPrepared;
  const uint32_t extra = static_cast<uint32_t>(preparedNotes.size()) - foundInNative;
  const uint32_t nativeFoundPrepared = foundInPrepared;
  const uint32_t nativeFoundGather = countDisplayNoteMatches(nativeNotes, oracleNotes, nullptr);
  uint32_t editRows = 0;
  char line[300];
  for (const EditPass& editPass : loop.passes.editPasses) {
    if (editPass.state != EditPassState::Active || editPass.passType != EditPassType::Note) {
      continue;
    }
    ++editRows;
    if (editRows > 4) {
      continue;
    }
    snprintf(line, sizeof(line),
             "#CAP,%lu,DIAG,lcr,vch,ed,id=%lu,act=%u,prop=%u,tid=%lu,s=%lu,e=%lu,p=%u",
             static_cast<unsigned long>(micros()), static_cast<unsigned long>(editPass.id),
             static_cast<unsigned>(editPass.actionType),
             static_cast<unsigned>(editPass.propertyType),
             static_cast<unsigned long>(editPass.targetNoteId),
             static_cast<unsigned long>(editPass.startTick),
             static_cast<unsigned long>(editPass.endTick),
             static_cast<unsigned>(editPass.pitch));
    DebugSessionCapture::appendCaptureTextLine(line);
  }
  snprintf(line, sizeof(line),
           "#CAP,%lu,DIAG,lcr,vch,win=%lu,proj=%lu,oracle=%lu,tot=%lu,ev=%u,notes=%u,"
           "oev=%u,onotes=%u,found=%u,miss=%u,extra=%u,ed=%u,match=%u",
           static_cast<unsigned long>(micros()), static_cast<unsigned long>(windowUs),
           static_cast<unsigned long>(reconstructUs), static_cast<unsigned long>(oracleUs),
           static_cast<unsigned long>(windowUs + reconstructUs),
           static_cast<unsigned>(prepared.size()), static_cast<unsigned>(preparedNotes.size()),
           static_cast<unsigned>(nativeWindow.size()), static_cast<unsigned>(nativeNotes.size()),
           static_cast<unsigned>(foundInPrepared), static_cast<unsigned>(miss),
           static_cast<unsigned>(extra), static_cast<unsigned>(editRows),
           displayNotesMatch(nativeNotes, preparedNotes) ? 1u : 0u);
  DebugSessionCapture::appendCaptureTextLine(line);
  snprintf(line, sizeof(line),
           "#CAP,%lu,DIAG,lcr,vch,nat,us=%lu,ev=%u,notes=%u,pfound=%u,gfound=%u,pmatch=%u,gmatch=%u",
           static_cast<unsigned long>(micros()), static_cast<unsigned long>(nativeUs),
           static_cast<unsigned>(nativeWindow.size()), static_cast<unsigned>(nativeNotes.size()),
           static_cast<unsigned>(nativeFoundPrepared), static_cast<unsigned>(nativeFoundGather),
           displayNotesMatch(nativeNotes, preparedNotes) ? 1u : 0u,
           displayNotesMatch(nativeNotes, oracleNotes) ? 1u : 0u);
  DebugSessionCapture::appendCaptureTextLine(line);
  logDisplayNoteDiffs("miss", nativeNotes, preparedNotes);
  logDisplayNoteDiffs("extra", preparedNotes, nativeNotes);
}
#endif

const char* contentResolutionDeviceGateDeferReason() {
  return LoopContentResolution::deviceGateContentDeferReason(
      StorageManager::hasPendingLoopSlotRestore(),
      StorageManager::hasPendingUndoSnapshotHydrate());
}

}  // namespace
#endif

void Track::validateAndCleanupMidiEvents(uint32_t openTailCloseTick) {
    (void)openTailCloseTick;
    Loop& loop = getActiveLoop();
    MidiEventVec materializedEvents;
    loop.copyIdleValidateEvents(materializedEvents);
    if (materializedEvents.empty()) return;

    const LoopEventValidation::LoopEventValidationResult invariantResult =
        LoopEventValidation::validateLoopEvents(materializedEvents, loop.loopLengthTicks,
                                                LoopEventValidation::kCanonicalInvariantMask);
    if (!invariantResult.passed) {
        logger.log(CAT_MIDI, LOG_WARNING,
                   "MIDI idle validate: non-canonical storage (check=%u); orphan repair log-only",
                   static_cast<unsigned>(invariantResult.firstFailure));
    }

    MidiEventVec repairProbe = materializedEvents;
    const LoopEventValidation::OrphanRepairResult repair =
        LoopEventValidation::repairOrphanNoteEvents(repairProbe, loop.loopLengthTicks,
                                                    Config::TICKS_PER_BAR);

    if (repair.orphanedRemoved > 0) {
        logger.log(CAT_MIDI, LOG_INFO,
                  "MIDI idle validate: would remove %d orphaned events (log-only v1), %d events remaining",
                  static_cast<int>(repair.orphanedRemoved),
                  static_cast<int>(repairProbe.size()));
    } else {
        logger.log(CAT_MIDI, LOG_INFO,
                  "MIDI validation complete: no orphaned events found, %d events total",
                  static_cast<int>(materializedEvents.size()));
    }
}

namespace {

// Wrap-pair verification walks one NoteOn per idle event and scans Offs for that pitch.
// Do not skip the dump when the flatten is larger than this — `230400` had 552 SEVT and
// emitted WRAP=0. Bound work per slice instead.

// One stored-MIDI verification dump after each queue. Overdub stop consumes the boot arm so
// later stops stay cheap. Selected-track `stopPlaying` re-arms so occupy HITL can score that
// loop's first-STOPPED effective / frame. Leftover tracks in the transport-stop hop do not re-arm.
//
// The arm/pending bookkeeping is not SESSION_CAPTURE-only: native tests exercise the queue
// lifecycle (arm -> eligible queue -> redundant queue) without the emission path.
bool sStoredMidiVerificationArmed = true;

#if defined(SESSION_CAPTURE)
void logStoredMidiVerificationArmState(bool armed) {
  Serial.printf("#CAP,DIAG,stored_verify,armed,%u\n", armed ? 1u : 0u);
}

// TEMPORARY DEVICE DIAGNOSTIC (remove with temporary C once SEVT is restored to a
// required gate). The 175335-style "no SEVT" families are only distinguishable when
// the queue decision and the drain reset reason are visible in the capture.
void logStoredMidiVerificationDecision(const char* decision) {
  Serial.printf("#CAP,DIAG,stored_verify,%s\n", decision);
}
#else
void logStoredMidiVerificationDecision(const char*) {}
#endif

enum StoredVerificationPhase : uint8_t {
  kStoredVerificationNotes = 0,
  kStoredVerificationWrapPairs = 1,
  kStoredVerificationDisplayNotes = 2,
  kStoredVerificationEffectiveNotes = 3,
  kStoredVerificationEffectiveRows = 4,
  kStoredVerificationFrameNotes = 5,
};

}  // namespace

// Idle total-notes / max_same_pitch inventory from a clean visual cache.
TRACK_COLD_MEM __attribute__((noinline)) void Track::maybeLogStoredNoteCount() {
#if !defined(SESSION_CAPTURE)
  return;
#else
  Loop& loop = getActiveLoop();
  if (!loop.hasCommittedPasses() || loop.visualCacheDirty || loop.visualCache.notes.empty()) {
    return;
  }
  const uint8_t slot = getActiveLoopIndex();
  if (slot >= Config::MAX_LOOPS_PER_TRACK) {
    return;
  }
  const uint8_t slotBit = static_cast<uint8_t>(1u << slot);
  if ((storedNoteCountLoggedMask_ & slotBit) != 0) {
    return;
  }
  // Count in this FLASHMEM function. Do not call the header template — that
  // instantiation lands in ITCM and crosses a 32 KB RAM1 block.
  uint32_t notes = 0;
  uint32_t uniqueNoteIds = 0;
  uint32_t maxSamePitch = 0;
  uint16_t pitchCount[128] = {};
  for (const NoteUtils::DisplayNote& note : loop.visualCache.notes) {
    if (note.noteId == kInvalidNoteId || note.startTick == note.endTick) {
      continue;
    }
    ++notes;
    ++uniqueNoteIds;
    if (note.note < 128) {
      ++pitchCount[note.note];
      if (pitchCount[note.note] > maxSamePitch) {
        maxSamePitch = pitchCount[note.note];
      }
    }
  }
  SC_STORED_NOTES(resolveTrackIndexForPersistence(*this), slot, notes, uniqueNoteIds, maxSamePitch);
  storedNoteCountLoggedMask_ |= slotBit;
#endif
}

TRACK_COLD_MEM void Track::maybeQueueContentResolutionDeviceGate() {
#if !defined(SESSION_CAPTURE)
  return;
#else
  if (LoopContentResolution::deviceGateActive()) {
    return;
  }
  if (this != &trackManager.getSelectedTrack()) {
    return;
  }
  Loop& loop = getActiveLoop();
  if (!loop.hasCommittedPasses() || loop.visualCacheDirty || loop.loopLengthTicks == 0) {
    return;
  }
  if (const char* deferReason = contentResolutionDeviceGateDeferReason()) {
#if defined(ARDUINO)
    logContentResolutionDeviceGateOnce("skip", deferReason);
#endif
    return;
  }
#if defined(ARDUINO)
  if (editManager.isNoteEditActive()) {
    logContentResolutionDeviceGateOnce("skip", "edit");
    return;
  }
#endif
  if (LoopContentResolution::deviceGateFinished()) {
    if (loop.loopLengthTicks == LoopContentResolution::deviceGateLoopLengthTicks()) {
      return;
    }
#if defined(ARDUINO)
    logContentResolutionDeviceGateOnce("reset", "length");
#endif
    LoopContentResolution::deviceGateReset();
  }
  LoopContentResolution::deviceGateBegin(loop.loopLengthTicks,
                                         resolveTrackIndexForPersistence(*this),
                                         getActiveLoopIndex());
#endif
}

TRACK_COLD_MEM void Track::processDeferredContentResolutionDeviceGate() {
#if !defined(SESSION_CAPTURE)
  return;
#else
  if (this != &trackManager.getSelectedTrack()) {
    return;
  }
  if (LoopContentResolution::deviceGateFinished() || !LoopContentResolution::deviceGateActive()) {
    return;
  }
  const uint8_t trackIndex = resolveTrackIndexForPersistence(*this);
  const uint8_t loopIndex = getActiveLoopIndex();
  if (!LoopContentResolution::deviceGateMatchesLoop(trackIndex, loopIndex)) {
    return;
  }
  Loop& loop = getActiveLoop();
#if defined(ARDUINO)
  if (editManager.isNoteEditActive()) {
    logContentResolutionDeviceGateIncomplete("edit");
    logContentResolutionDeviceGateOnce("reset", "edit");
    LoopContentResolution::deviceGateReset();
    return;
  }
#endif
  if (!loop.hasCommittedPasses() || loop.loopLengthTicks == 0) {
#if defined(ARDUINO)
    logContentResolutionDeviceGateIncomplete("dirty");
    logContentResolutionDeviceGateOnce("reset", "dirty");
#endif
    LoopContentResolution::deviceGateReset();
    return;
  }
  if (loop.visualCacheDirty) {
    const LoopContentResolution::DeviceGateDirtyPolicy policy =
        LoopContentResolution::deviceGateDirtyPolicy(true, loop.loopLengthTicks);
    if (policy == LoopContentResolution::DeviceGateDirtyPolicy::Skip) {
#if defined(ARDUINO)
      if (logContentResolutionDeviceGateOnce("skip", "dirty")) {
        logContentResolutionDeviceGateIncomplete("dirty");
      }
#endif
      return;
    }
#if defined(ARDUINO)
    logContentResolutionDeviceGateIncomplete("dirty");
    logContentResolutionDeviceGateOnce("reset", "dirty");
#endif
    LoopContentResolution::deviceGateReset();
    return;
  }
  if (const char* deferReason = contentResolutionDeviceGateDeferReason()) {
#if defined(ARDUINO)
    if (logContentResolutionDeviceGateOnce("skip", deferReason)) {
      logContentResolutionDeviceGateIncomplete(deferReason);
    }
#endif
    return;
  }
  const LoopContentResolution::DeviceGateSliceResult result =
      LoopContentResolution::deviceGateRunOneSlice(loop.passes, loop.loopLengthTicks);
  if (result == LoopContentResolution::DeviceGateSliceResult::Inactive) {
#if defined(ARDUINO)
    logContentResolutionDeviceGateIncomplete("abort");
    logContentResolutionDeviceGateOnce("reset", "abort");
#endif
    return;
  }
  char phaseLine[192];
  if (LoopContentResolution::deviceGateFormatPhaseLine(phaseLine, sizeof(phaseLine))) {
    DebugSessionCapture::appendCaptureTextLine(phaseLine);
  }
  char prepareSliceLine[192];
  if (LoopContentResolution::deviceGateFormatPrepareSliceLine(prepareSliceLine,
                                                             sizeof(prepareSliceLine))) {
    DebugSessionCapture::appendCaptureTextLine(prepareSliceLine);
  }
  if (result != LoopContentResolution::DeviceGateSliceResult::Complete) {
    return;
  }
  char line[224];
  LoopContentResolution::deviceGateFormatCaptureLine(line, sizeof(line));
  DebugSessionCapture::appendCaptureTextLine(line);
  char pairLine[320];
  LoopContentResolution::deviceGateFormatPairLine(pairLine, sizeof(pairLine));
  DebugSessionCapture::appendCaptureTextLine(pairLine);
  LoopContentResolution::deviceGateComplete(loop.playbackRevision);
#if defined(ARDUINO)
  logPreparedDisplayRangeSample(loop);
#endif
#endif
}

TRACK_COLD_MEM void Track::resetDeferredStoredMidiVerification() {
  deferredStoredVerificationPending = false;
  deferredStoredVerificationPhase = kStoredVerificationNotes;
  deferredStoredVerificationCursor = 0;
  deferredStoredVerificationEvents.clear();
#if defined(SESSION_CAPTURE)
  deferredStoredVerificationNotes.clear();
#endif
}

TRACK_COLD_MEM void Track::queueDeferredStoredMidiVerification() {
  // Eligibility is evaluated BEFORE any state mutation: an ineligible call (already
  // consumed arm, empty/uncommitted loop) must not destroy an already-queued dump.
  const Loop& loop = getActiveLoop();
  if (loop.loopLengthTicks == 0 || !loop.hasCommittedPasses()) {
    logStoredMidiVerificationDecision("queue_skip_loop");
    return;
  }
  if (!sStoredMidiVerificationArmed) {
    logStoredMidiVerificationDecision("queue_skip_unarmed");
    return;
  }
  // Accepted as a new queue operation: replace any prior deferred verification state.
  resetDeferredStoredMidiVerification();
  deferredStoredVerificationPending = true;
  sStoredMidiVerificationArmed = false;
  logStoredMidiVerificationDecision("queue_accepted");
#if defined(PIO_UNIT_TEST_NATIVE)
  ++deferredStoredVerificationOpportunityCount;
#endif
#if defined(SESSION_CAPTURE)
  logStoredMidiVerificationArmState(false);
#endif
}

TRACK_COLD_MEM void Track::rearmDeferredStoredMidiVerification() {
  sStoredMidiVerificationArmed = true;
#if defined(SESSION_CAPTURE)
  logStoredMidiVerificationArmState(true);
#endif
  queueDeferredStoredMidiVerification();
}

#if defined(PIO_UNIT_TEST_NATIVE)
bool Track::deferredStoredVerificationPendingForTest() const {
  return deferredStoredVerificationPending;
}

void Track::setDeferredStoredVerificationArmedForTest(bool armed) {
  sStoredMidiVerificationArmed = armed;
}

bool Track::deferredStoredVerificationArmedForTest() const {
  return sStoredMidiVerificationArmed;
}

uint32_t Track::deferredStoredVerificationOpportunityCountForTest() const {
  return deferredStoredVerificationOpportunityCount;
}
#endif

TRACK_COLD_MEM void Track::processDeferredStoredMidiVerification(size_t maxEventsPerSlice) {
  RuntimeTimingTelemetry::IdleMaintChildScope idleChild(1);
#if !defined(SESSION_CAPTURE)
  (void)maxEventsPerSlice;
  return;
#else
  if (!deferredStoredVerificationPending) {
    return;
  }

  const Loop& loop = getActiveLoop();
  if (loop.loopLengthTicks == 0 || !loop.hasCommittedPasses()) {
    logStoredMidiVerificationDecision("drain_reset_loop");
    resetDeferredStoredMidiVerification();
    return;
  }

  const uint32_t heapBeforeMerge = MemoryMonitor::getInternalHeapFreeBytes();
  if (!LoopEventStore::hasInternalHeapHeadroomForNonCriticalWork(heapBeforeMerge)) {
    logStoredMidiVerificationDecision("drain_reset_heap");
    resetDeferredStoredMidiVerification();
    return;
  }

  // Flatten once, on the first idle slice — never on the stop path.
  if (deferredStoredVerificationEvents.empty() && deferredStoredVerificationCursor == 0 &&
      deferredStoredVerificationPhase == kStoredVerificationNotes) {
    loop.mergeActiveCapturePasses(deferredStoredVerificationEvents);
    if (deferredStoredVerificationEvents.empty()) {
      logStoredMidiVerificationDecision("drain_reset_no_events");
      resetDeferredStoredMidiVerification();
      return;
    }
    logStoredMidiVerificationDecision("drain_emit_start");
  }

  const SessionMidiEventVec& flat = deferredStoredVerificationEvents;

  if (deferredStoredVerificationPhase == kStoredVerificationNotes) {
    size_t emitted = 0;
    while (deferredStoredVerificationCursor < flat.size() && emitted < maxEventsPerSlice) {
      const MidiEvent& evt = flat[deferredStoredVerificationCursor++];
      if (evt.isNoteOn()) {
        SC_STORED_NOTE_EVENT('N', evt.tick, evt.channel, evt.data.noteData.note);
      } else if (evt.isNoteOff()) {
        SC_STORED_NOTE_EVENT('F', evt.tick, evt.channel, evt.data.noteData.note);
      } else {
        continue;
      }
      ++emitted;
    }
    if (deferredStoredVerificationCursor < flat.size()) {
      return;
    }
    deferredStoredVerificationCursor = 0;
    deferredStoredVerificationPhase = kStoredVerificationWrapPairs;
    return;
  }

  if (deferredStoredVerificationPhase == kStoredVerificationWrapPairs) {
    size_t emitted = 0;
    while (deferredStoredVerificationCursor < flat.size() && emitted < maxEventsPerSlice) {
      const MidiEvent& on = flat[deferredStoredVerificationCursor++];
      if (!on.isNoteOn()) {
        continue;
      }
      ++emitted;
      for (const MidiEvent& off : flat) {
        if (!off.isNoteOff() || off.channel != on.channel ||
            off.data.noteData.note != on.data.noteData.note) {
          continue;
        }
        if (NoteUtils::isWrappedLoopNotePair(on.tick, off.tick, loop.loopLengthTicks)) {
          SC_STORED_WRAP_PAIR(on.tick, off.tick, on.channel, on.data.noteData.note);
          break;
        }
      }
    }
    if (deferredStoredVerificationCursor < flat.size()) {
      return;
    }
    deferredStoredVerificationCursor = 0;
    deferredStoredVerificationPhase = kStoredVerificationDisplayNotes;
    return;
  }

  if (deferredStoredVerificationPhase == kStoredVerificationDisplayNotes) {
    if (deferredStoredVerificationNotes.empty() && deferredStoredVerificationCursor == 0) {
      deferredStoredVerificationNotes =
          NoteUtils::reconstructDisplayNotes(flat, loop.loopLengthTicks, false);
      char header[128];
      snprintf(header, sizeof(header),
               "#CAP,%lu,DIAG,dnte,src,reconstruct,count,%u",
               static_cast<unsigned long>(micros()),
               static_cast<unsigned>(deferredStoredVerificationNotes.size()));
      DebugSessionCapture::appendCaptureTextLine(header);
    }
    size_t emitted = 0;
    while (deferredStoredVerificationCursor < deferredStoredVerificationNotes.size() &&
           emitted < maxEventsPerSlice) {
      const NoteUtils::DisplayNote& note =
          deferredStoredVerificationNotes[deferredStoredVerificationCursor];
      const uint32_t displayStart = IntervalProjection::noteRelativeTick(
          note.startTick, loop.loopStartTick, loop.loopLengthTicks);
      // DisplayNote.endTick is a display boundary tick; when a note ends at loop wrap its tail
      // segment ends at loopLength-1, but its exclusive end is loopLength.
      uint32_t length = 0;
      if (loop.loopLengthTicks > 0 && note.endTick >= note.startTick) {
        const uint32_t endExclusive =
            (note.endTick == loop.loopLengthTicks - 1) ? loop.loopLengthTicks : note.endTick;
        length = endExclusive > note.startTick ? (endExclusive - note.startTick) : 0;
      }
      SC_DNTE(note.note, note.startTick, displayStart, length,
              static_cast<int>(deferredStoredVerificationCursor));
      ++deferredStoredVerificationCursor;
      ++emitted;
    }
    if (deferredStoredVerificationCursor < deferredStoredVerificationNotes.size()) {
      return;
    }
    deferredStoredVerificationNotes.clear();
    deferredStoredVerificationCursor = 0;
    deferredStoredVerificationPhase = kStoredVerificationEffectiveNotes;
    return;
  }

  if (deferredStoredVerificationPhase == kStoredVerificationEffectiveNotes) {
    if (deferredStoredVerificationCursor == 0 && deferredStoredVerificationNotes.empty()) {
      // Raw reconstruct used mergeActiveCapturePasses (no EditPass replay).
      // Effective dump pairs each capture pass, then applies companions, so
      // occupancy convergence does not LIFO-steal another pass's Off (`211440`).
      //
      // Task 7.1: the former whole-pass `resolveDisplayNotes` ran here, and the
      // `copyEffectiveCommittedEvents` result was not consumed by either the resolve or the SEVT
      // rows (the vector is cleared after this phase). The copy is removed; the resolve is now one
      // bar per owner turn, merged by `upsertResolvedDisplayNotes` with the standard one-bar pad.
      // The cursor doubles as the window start until every window is merged.
      deferredStoredVerificationEvents.clear();
    }
    if (deferredStoredVerificationCursor < loop.loopLengthTicks) {
      const uint32_t windowStart = static_cast<uint32_t>(deferredStoredVerificationCursor);
      const uint32_t remaining = loop.loopLengthTicks - windowStart;
      const uint32_t windowLength =
          remaining < Config::TICKS_PER_BAR ? remaining : Config::TICKS_PER_BAR;
      NoteUtils::DisplayNoteVec windowNotes;
      loop.resolveEffectiveDisplayNotesInWindow(windowStart, windowLength, windowNotes);
      LoopContentResolution::upsertResolvedDisplayNotes(deferredStoredVerificationNotes,
                                                        windowNotes);
      deferredStoredVerificationCursor = windowStart + windowLength;
      return;
    }
    // Match STOPPED paint: LCR passes + wrap-held append (`180916` parity). One whole-loop append
    // after the bounded window derives; it only adds wrap-paired overdub rows.
    loop.appendOverdubPassDisplayNotes(deferredStoredVerificationNotes);
    char header[128];
    snprintf(header, sizeof(header),
             "#CAP,%lu,DIAG,dnte,src,effective,count,%u",
             static_cast<unsigned long>(micros()),
             static_cast<unsigned>(deferredStoredVerificationNotes.size()));
    DebugSessionCapture::appendCaptureTextLine(header);
    deferredStoredVerificationCursor = 0;
    deferredStoredVerificationPhase = kStoredVerificationEffectiveRows;
    return;
  }

  if (deferredStoredVerificationPhase == kStoredVerificationEffectiveRows) {
    size_t emitted = 0;
    while (deferredStoredVerificationCursor < deferredStoredVerificationNotes.size() &&
           emitted < maxEventsPerSlice) {
      const NoteUtils::DisplayNote& note =
          deferredStoredVerificationNotes[deferredStoredVerificationCursor];
      const uint32_t displayStart = IntervalProjection::noteRelativeTick(
          note.startTick, loop.loopStartTick, loop.loopLengthTicks);
      uint32_t length = 0;
      if (loop.loopLengthTicks > 0 && note.endTick >= note.startTick) {
        const uint32_t endExclusive =
            (note.endTick == loop.loopLengthTicks - 1) ? loop.loopLengthTicks : note.endTick;
        length = endExclusive > note.startTick ? (endExclusive - note.startTick) : 0;
      }
      SC_DNTE(note.note, note.startTick, displayStart, length,
              static_cast<int>(deferredStoredVerificationCursor));
      ++deferredStoredVerificationCursor;
      ++emitted;
    }
    if (deferredStoredVerificationCursor < deferredStoredVerificationNotes.size()) {
      return;
    }
    deferredStoredVerificationNotes.clear();
    deferredStoredVerificationEvents.clear();
    deferredStoredVerificationCursor = 0;
    deferredStoredVerificationPhase = kStoredVerificationFrameNotes;
    return;
  }

  if (deferredStoredVerificationPhase == kStoredVerificationFrameNotes) {
    if (displayManager.emitStoppedFrameNoteDump(maxEventsPerSlice) > 0) {
      return;
    }
    logStoredMidiVerificationDecision("drain_complete");
    resetDeferredStoredMidiVerification();
  }
#endif  // SESSION_CAPTURE
}

namespace {

TRACK_COLD_MEM const char* classifyStopDebtState(bool preState, bool postState, uint16_t preDirtyBars,
                                                 uint16_t postDirtyBars) {
  if (!preState && postState) {
    return "stop_created_debt";
  }
  if (preState && postState && postDirtyBars > preDirtyBars) {
    return "stop_amplified_debt";
  }
  if (preState && postState) {
    return "pre_existing_debt";
  }
  return "newly_scheduled_after_stop";
}

}  // namespace

TRACK_COLD_MEM uint16_t Track::countVisualCacheDirtyBars(const Loop& loop) const {
  if (!loop.visualCacheDirty || loop.visualCache.dirtyBars.empty()) {
    return 0;
  }
  uint16_t dirtyBars = 0;
  for (uint8_t dirty : loop.visualCache.dirtyBars) {
    if (dirty != 0) {
      ++dirtyBars;
    }
  }
  return dirtyBars;
}

TRACK_COLD_MEM void Track::armOverdubStopDebtProbe(bool visualDirtyBeforeStop,
                                                   uint16_t dirtyBarsBeforeStop,
                                                   bool savePendingBeforeStop,
                                                   const Loop& loop) {
#if !defined(SESSION_CAPTURE)
  (void)visualDirtyBeforeStop;
  (void)dirtyBarsBeforeStop;
  (void)savePendingBeforeStop;
  (void)loop;
  return;
#else
  overdubStopDebtProbeArmed_ = true;
  overdubStopDebtProbeArmedUs_ = micros();
  overdubStopVisualDirtyBefore_ = visualDirtyBeforeStop;
  overdubStopVisualDirtyAfter_ = loop.visualCacheDirty;
  overdubStopDirtyBarsBefore_ = dirtyBarsBeforeStop;
  overdubStopDirtyBarsAfter_ = countVisualCacheDirtyBars(loop);
  overdubStopSavePendingBefore_ = savePendingBeforeStop;
  overdubStopSavePendingAfter_ = StorageManager::hasDeferredSaveWork();
#endif
}

TRACK_COLD_MEM void Track::emitOverdubStopDebtProbeIfArmed(const Loop& loop,
                                                           bool idleVisualSliceRan,
                                                           uint8_t idleBarsPerSlice,
                                                           uint16_t idleDirtyBefore,
                                                           uint16_t idleDirtyAfter) {
#if !defined(SESSION_CAPTURE)
  (void)loop;
  (void)idleVisualSliceRan;
  (void)idleBarsPerSlice;
  (void)idleDirtyBefore;
  (void)idleDirtyAfter;
  return;
#else
  if (!overdubStopDebtProbeArmed_) {
    return;
  }
  if (!isPlaying()) {
    overdubStopDebtProbeArmed_ = false;
    return;
  }
  const char* visualClass =
      classifyStopDebtState(overdubStopVisualDirtyBefore_, overdubStopVisualDirtyAfter_,
                            overdubStopDirtyBarsBefore_, overdubStopDirtyBarsAfter_);
  const char* saveClass = classifyStopDebtState(overdubStopSavePendingBefore_,
                                                overdubStopSavePendingAfter_, 0, 0);
  logOverdubStopStage(loop, overdubStopDebtProbeArmedUs_, "debt_handoff", 0,
                      DebugSessionCapture::kUnsampledHeapBytes,
                      DebugSessionCapture::kUnsampledHeapBytes, visualClass, nullptr,
                      ownedPendingCapturePass());
#if defined(ARDUINO)
  char line[288];
  snprintf(line, sizeof(line),
           "#CAP,%lu,DIAG,odub_debt,track=%u,visual=%s,save=%s,pre_v=%u,post_v=%u,pre_b=%u,"
           "post_b=%u,idle_slice=%u,idle_bars=%u,idle_pre_b=%u,idle_post_b=%u,state=%s",
           static_cast<unsigned long>(micros()),
           static_cast<unsigned>(resolveTrackIndexForPersistence(*this)), visualClass, saveClass,
           overdubStopVisualDirtyBefore_ ? 1u : 0u, overdubStopVisualDirtyAfter_ ? 1u : 0u,
           static_cast<unsigned>(overdubStopDirtyBarsBefore_),
           static_cast<unsigned>(overdubStopDirtyBarsAfter_), idleVisualSliceRan ? 1u : 0u,
           static_cast<unsigned>(idleBarsPerSlice), static_cast<unsigned>(idleDirtyBefore),
           static_cast<unsigned>(idleDirtyAfter), getStateName(getState()));
  DebugSessionCapture::appendCaptureTextLine(line);
#endif
  overdubStopDebtProbeArmed_ = false;
#endif
}

TRACK_COLD_MEM bool Track::processDeferredCanonicalPrefix() {
  // S4f.2: the accepted interaction's drive owns its turn, so the canonical-prefix child yields while a stop
  // obligation is pending -- admitted (`hasPendingDeferredOverdubStop`) or merely accepted
  // (`hasAcceptedUnexecutedInteraction`). Without the second term the two compete for the same deferred turn
  // and the drive can be starved by background preparation.
  if ((!isPlaying() && !isOverdubbing()) || hasPendingDeferredOverdubStop() ||
      hasAcceptedUnexecutedInteraction() || !loopsAllocated()) {
    return false;
  }
  Loop& loop = getActiveLoop();
  if (MidiServiceDrain::canonicalPreparationHasPriorityWork(
      isOverdubbing(), loop.visualCacheDirty, loop.hasDeferredVisualCacheWrapAppend(),
      loop.overdubSourceSpanCacheReady())) return false;
  auto* runtime = playbackRuntime.slotIfAllocated(activeLoopIndex);
  if (!runtime) return false;
  const PlaybackViewSourceIdentity source{loop.playbackRevision, playbackGeneration,
                                         loop.publishedSnapshotIdentity()};
  if (!runtime->canonicalPrefixNeedsService(loop.passes, source, loop.loopLengthTicks,
                                            loop.canonicalStorageGeneration) &&
      (runtime->preparedCanonicalWindow(source) ||
       runtime->canonicalWindowPhase == LoopPlaybackRuntime::CanonicalWindowPhase::Denied ||
       runtime->canonicalPrefixPhase != LoopPlaybackRuntime::CanonicalPrefixPhase::Ready)) return false;
  RuntimeCanonicalPassInputs::CurrentSource live{loop.playbackRevision, playbackGeneration,
                                                loop.loopLengthTicks, loop.canonicalStorageGeneration};
  const auto anchor = runtime->currentCycleCursor(
      loop.lastTickInLoop == UINT32_MAX ? 0 : loop.lastTickInLoop, source.sourceGeneration);
  (void)runtime->stepCanonicalBase(loop.passes, source, live, anchor,
                                  MidiServiceDrain::kCanonicalPrefixInputQuantum);
  return true; // Setup, cleanup, denied, or Q input work consumed this turn.
}

/// CONTRACTS.md C3.1: the session-open qualification. Same receipt resolution as the grain, but it
/// binds only -- the cursor's units are still spent one grain at a time from the maintenance lane.
TRACK_COLD_MEM bool Track::bindPrivatePredecessorSourceForOverdub(Loop& loop) {
  const CaptureStopPresentation* presentation = captureStopPresentationForSlot(activeLoopIndex);
  if (presentation == nullptr || !presentation->ownsExtent || presentation->retirementActive ||
      presentation->slot != activeLoopIndex) {
    return false;
  }
  Loop::PrivatePredecessorBinding binding{};
  binding.range = &presentation->rawRange;
  binding.receipt = presentation;
  binding.loopId = presentation->loopId;
  binding.interval = presentation->interval;
  binding.captureContentGeneration = presentation->frozenCaptureContentGeneration;
  return loop.bindPrivatePredecessorSourceOnly(binding);
}
/// CONTRACTS.md C3.2/C3.4: one bounded private-predecessor source grain for the active slot.
///
/// The predecessor is the *slot's currently owed custody receipt* -- the interval this Track
/// sealed and has not published. Publication clears the obligation (`retirementActive`), which is
/// exactly the supersession edge C3.5 names: the cursor is dropped and the committed origin
/// answers instead.
TRACK_COLD_MEM Loop::PrivateSourceStep Track::stepPrivatePredecessorSourceGrain(Loop& loop) {
  const CaptureStopPresentation* presentation = captureStopPresentationForSlot(activeLoopIndex);
  Loop::PrivatePredecessorBinding binding{};
  if (presentation != nullptr && presentation->ownsExtent && !presentation->retirementActive &&
      presentation->slot == activeLoopIndex) {
    binding.range = &presentation->rawRange;
    binding.receipt = presentation;
    binding.loopId = presentation->loopId;
    binding.interval = presentation->interval;
    binding.captureContentGeneration = presentation->frozenCaptureContentGeneration;
  }
  // An empty binding means no private predecessor is owed any more (published, retired, or another
  // interval). The Loop supersedes the scan itself and still materializes whatever intents it
  // retained, so publishing A can delay B's overlap decision but can never erase it -- including the
  // case where the seal deadline (C3.4) has already reported the denial by name.
  const Loop::PrivateSourceStep step = loop.stepPrivatePredecessorSource(binding);
  if (step == Loop::PrivateSourceStep::Denied) {
    // Named, not silent: the token says which half refused, and the intent stays retained so the
    // dependent transform is denied rather than applied from stale or empty content.
    logger.log(CAT_TRACK, LOG_INFO, "Private source deny=%s slot=%u interval=%llu",
               Loop::privateSourceDenyName(loop.privateSourceDeny()),
               static_cast<unsigned>(activeLoopIndex),
               static_cast<unsigned long long>(binding.interval));
  }
  return step;
}
TRACK_COLD_MEM bool Track::hasDeferredConvergenceObligation() const {
  if (!loopsAllocated()) return false;
  for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
    const auto* runtime = playbackRuntime.slotIfAllocated(slot);
    if (runtime != nullptr && runtime->deferredConvergenceActive()) return true;
  }
  return false;
}

TRACK_COLD_MEM bool Track::serviceDeferredCanonicalConvergence() {
  if (!loopsAllocated()) return false;
  for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
    auto* runtime = playbackRuntime.slotIfAllocated(slot);
    if (runtime == nullptr || !runtime->deferredConvergenceActive()) continue;
    // Exclusive canonical-builder ownership: the inline capture stop wins. The stop grain is this
    // machinery's only inline owner and it holds the slot's capture-stop owner for its whole life, so
    // a non-null owner is an exact "an inline build owns the builders" test -- the deferred turn does
    // nothing and the obligation stays `Owed` (spec: active inline build defers service).
    if (runtime->captureStopOwner != nullptr) return false;
    Loop& loop = getLoop(slot);
    // Slot incarnation check: clear/load/replacement restarts the committed timeline at zero, so a
    // target above the live revision can only mean the content this obligation owed is gone.
    if (loop.committedPassRevision() < runtime->deferredConvergenceTarget) {
      runtime->cancelDeferredConvergence();
      continue;
    }
    // Standing demand, universal coalescing: one obligation, targeting the newest committed
    // generation. Any newer generation drops an unpublished candidate and re-arms `Owed`.
    (void)runtime->coalesceDeferredConvergence(loop.committedPassRevision());
    switch (runtime->deferredConvergencePhase) {
      case LoopPlaybackRuntime::DeferredConvergencePhase::Idle:
        continue;
      case LoopPlaybackRuntime::DeferredConvergencePhase::Owed: {
        // Build only against a fresh authoritative source covering the target generation. No
        // authority -> stay owed; the committed raw content remains authoritative and playable.
        if (!loop.publishedSnapshotIsCurrent()) return true;
        if (loop.committedPassRevision() < runtime->deferredConvergenceTarget) return true;
        // Never steal a prewarmed view set this slot does not have. The builder's own admission
        // decides the rest: a completed previous build is *reused* when it already represents this
        // source and restarted (identity-mismatched) when it does not, so requiring an idle builder
        // here would deadlock every obligation after the first completed one.
        if (runtime->viewSetIfPrewarmed() == nullptr) return true;
        runtime->deferredConvergenceSource = PlaybackViewSourceIdentity{
            loop.playbackRevision, playbackGeneration, loop.publishedSnapshotIdentity()};
        runtime->deferredConvergenceLength = loop.loopLengthTicks;
        runtime->deferredConvergenceDenial = LoopPlaybackRuntime::DeferredConvergenceDenial::None;
        runtime->deferredConvergencePhase = LoopPlaybackRuntime::DeferredConvergencePhase::Building;
        return true;  // binding is its own owner turn
      }
      case LoopPlaybackRuntime::DeferredConvergencePhase::Building: {
        const auto& bound = runtime->deferredConvergenceSource;
        if (loop.playbackRevision != bound.playbackRevision ||
            loop.publishedSnapshotIdentity() != bound.snapshotIdentity ||
            loop.loopLengthTicks != runtime->deferredConvergenceLength) {
          // The authoritative source changed under the build: discard, never publish a mix of the
          // superseded prefix with a newer suffix, and re-resolve from the new authority.
          runtime->discardCanonicalBuildCandidate();
          runtime->deferredConvergenceDenial =
              LoopPlaybackRuntime::DeferredConvergenceDenial::SourceChanged;
          runtime->deferredConvergencePhase = LoopPlaybackRuntime::DeferredConvergencePhase::Owed;
          return true;
        }
        const RuntimeCanonicalPassInputs::CurrentSource live{
            loop.playbackRevision, playbackGeneration, loop.loopLengthTicks,
            loop.canonicalStorageGeneration};
        const uint32_t playhead =
            loop.lastTickInLoop == UINT32_MAX || loop.loopLengthTicks == 0
                ? 0u : loop.lastTickInLoop % loop.loopLengthTicks;
        const auto anchor = runtime->currentCycleCursor(playhead, bound.sourceGeneration);
        const auto result = runtime->stepCanonicalBase(
            loop.passes, bound, live, anchor, Loop::kDeferredStopPreparationQuantum);
        if (result == RuntimePlaybackOrderBuildResult::Complete) {
          // The derived canonical window is built and published (`view.published`); adoption is
          // identity-qualified on the next turn. `Building -> Ready` never bumps the committed
          // revision, so this adoption cannot invalidate its own target (task 2.0 audit).
          runtime->deferredConvergencePhase = LoopPlaybackRuntime::DeferredConvergencePhase::Ready;
        } else if (result == RuntimePlaybackOrderBuildResult::Denied) {
          // A terminal denial discards the candidate and returns to `Owed`; it never blocks
          // interaction and never leaves the obligation stuck in `Building`.
          runtime->deferredConvergenceDenial =
              LoopPlaybackRuntime::DeferredConvergenceDenial::BuildDenied;
          runtime->discardCanonicalBuildCandidate();
          runtime->deferredConvergencePhase = LoopPlaybackRuntime::DeferredConvergencePhase::Owed;
        }
        return true;
      }
      case LoopPlaybackRuntime::DeferredConvergencePhase::Ready: {
        const auto& bound = runtime->deferredConvergenceSource;
        if (loop.committedPassRevision() != runtime->deferredConvergenceTarget ||
            loop.playbackRevision != bound.playbackRevision ||
            loop.publishedSnapshotIdentity() != bound.snapshotIdentity) {
          runtime->discardCanonicalBuildCandidate();
          runtime->deferredConvergenceDenial =
              LoopPlaybackRuntime::DeferredConvergenceDenial::SourceChanged;
          runtime->deferredConvergencePhase = LoopPlaybackRuntime::DeferredConvergencePhase::Owed;
          return true;
        }
        // Adopted through the target generation: one obligation, now discharged.
        runtime->cancelDeferredConvergence();
        return true;
      }
    }
  }
  return false;
}

#if defined(__IMXRT1062__)
DMAMEM uint8_t gStopGrainBurstLastTurnGrains;
#else
uint8_t gStopGrainBurstLastTurnGrains;  // host harnesses include this TU without Arduino's DMAMEM
#endif

uint8_t stopGrainBurstLastTurnGrains() { return gStopGrainBurstLastTurnGrains; }

TRACK_COLD_MEM void Track::processDeferredIdleMaintenance(
    uint32_t nowMs, bool postOverdubDrainWindowActive,
    MidiServiceDrain::DeferredIdleOwner deferredOwner, uint8_t snapshotConstructionUnits,
    bool timingCritical) {
  // The producer is single/global. Preserve the selected display's gap source
  // before either maintenance branch can ask for unrelated cache backfill.
  const Track& displayTrack = trackManager.getSelectedTrack();
  const bool displayGapSourceOwed = displayTrack.loopsAllocated() &&
      displayTrack.isAudiblePlaybackActive() &&
      displayTrack.getActiveLoopIndex() == trackManager.getSelectedSlotIndex(trackManager.getSelectedTrackIndex()) &&
      displayTrack.getActiveLoop().isPlaybackSessionExitPublicationGap() &&
      !displayTrack.getActiveLoop().privateSourceOwedForSeal();
  const bool deferBackgroundVisualSource = displayGapSourceOwed && &displayTrack != this;
  // DEC-231: one off-edge view-prewarm unit per idle turn. Never while another track is timing-
  // critical; the Record edge stays armed/pending until the state reaches Prepared.
  // DEC-233: an accepted **Stop** obligation is capacity-blocking work and must not be starved by a
  // preparation request that returns every turn while it is pending. A retained Start latch keeps the
  // preparation-first order: preparation is exactly what readies that edge.
  const PendingInteractionLatch* prewarmOrderLatch = interactionLatch();
  const bool stopObligationAheadOfPrewarm =
      prewarmOrderLatch != nullptr && prewarmOrderLatch->kind == PendingInteractionLatch::Kind::Stop;
  if (!timingCritical && !stopObligationAheadOfPrewarm && stepViewPrewarmForIdle()) {
    return;
  }
  // Touched-coverage worker (task 4.6/4.7): one declared grain of the next owed tile, published atomically
  // when that tile's derive completes. It runs **before** every early return below, because the demand was
  // created by an accepted note rather than by this turn, and a turn that returns early would otherwise
  // never serve it. Marks on the capture path derive nothing; this is the only place that does.
  if (isOverdubbing() && deferredOwner == MidiServiceDrain::DeferredIdleOwner::VisualCache) {
    (void)getActiveLoop().serviceOverdubCoverageTurn(CoveragePrerequisite::kGrainEventsPerTurn);
  }
  // DEC-194: the passes-backed stream is built on demand by the playback path (`rebuildPlaybackOrder`), which is
  // where a missing stream is noticed. An unconditional re-attempt from this lane was tried and **removed**: it ran a
  // multi-bar gather every idle turn for every playing track without a stream and starved the boot restore sweep
  // (device 2026-10-29: 4-5 slots committed in 34 s, versus 26 in 17 s on the 02:13 image).
  // DEC-191: the orphan settlement is deliberately NOT run from this lane. It ran here unconditionally and the
  // device regressed (`session_20261029_011304.log`): a deferred Record stop parked at `CanonicalPrefix` with
  // `pub=1`, `playback_read` zero and the panel holding `why=hold_gap` -- i.e. the settlement released a
  // reservation the stop's publication still needed, and nothing was logged. Recovery stays where the defect was
  // observed (the Start claim path), where a Start is actually asking for the slot.
  // STOPPING owns the active capture and consumes exactly one declared
  if (hasPendingDeferredOverdubStop()) {
    CaptureStopJob* pendingStop = captureStopJobHead();
    if (deferredOwner == MidiServiceDrain::DeferredIdleOwner::Snapshot && pendingStop != nullptr &&
        pendingStop->mode == CapturePhase::Record &&
        pendingStop->admission.phase == PendingStopPhase::RecordPublished &&
        pendingStop->owner != nullptr && pendingStop->owner->pendingSnapshotSessionExitQueued() &&
        !pendingStop->owner->publishedSnapshotIsCurrent()) {
      // Record retirement waits for this queued root to become current. Lend the existing
      // Snapshot owner its normal bounded turn; do not stack snapshot work with a stop grain.
      processSnapshotOwnerTurn(loopWithPendingSnapshotPublish(), snapshotConstructionUnits);
      return;
    }
    // DEC-178 (rate): burst servicing. Measured on `session_20261028_155453.log`, a deferred stop grain costs
    // 60-380 us of CPU (22 PrepareTransfer grains in 20 jobs, 76 us each; 20 DeferredCleanup grains, 379 us), while
    // a job took ~1.4 s of wall clock -- 99% of that is *scheduler cadence*, one grain per owner turn. With a backlog
    // (more than one job queued) and a live interval already recording, spend a bounded burst instead: at most
    // `kStopGrainBurstMaxGrains` grains and at most `kStopGrainBurstBudgetUs` of this turn. Both bounds are enforced
    // per grain, so a turn never becomes unbounded, and the normal single-job cadence is untouched.
#if defined(STOP_GRAIN_BURST_DISABLED)
    constexpr uint8_t kStopGrainBurstMaxGrains = 1;   // bisect/rollback image: exact pre-DEC-178 cadence
    constexpr uint32_t kStopGrainBurstBudgetUs = 0;
#else
    // Device `session_20261028_162431.log` had the lane keeping up at 4 grains/600 us; the first cut (8 grains /
    // 1500 us) let a turn carry a `DeferredCleanup` recycle grain (2.6 ms) and overshoot the playback service
    // deadline -- the reported MIDI-timing deterioration and display stalls. The burst is now capped by the
    // scheduler's own remaining time as well as by these two bounds.
    constexpr uint8_t kStopGrainBurstMaxGrains = 4;
    constexpr uint32_t kStopGrainBurstBudgetUs = 600;
#endif
    constexpr uint32_t kStopGrainBurstMinRemainingUs = 2500;  // stop before the next playback service is near
    // Burst only the **overdub** lane: it is the one measured as cadence-bound (60-380 us grains), and the Record
    // lane's publish/reader handoff keeps its one-grain-per-turn separation (`test_record_note_stream_survives_four_stops_
    // before_predecessor_publication` loses events when a burst steps it -- measured, hence this scope).
    const CaptureStopJob* burstHead = captureStopJobHead();
    const bool backlog = captureStopQueueDepth() > 1 && burstHead != nullptr &&
                         burstHead->mode == CapturePhase::Overdub;
    const uint8_t grainBudget = backlog ? kStopGrainBurstMaxGrains : 1u;
#if defined(SESSION_CAPTURE)
    uint32_t burstStartUs = 0;
    if (backlog) burstStartUs = micros();
#endif
    uint8_t grainsThisTurn = 0;
    for (uint8_t grainIndex = 0; grainIndex < grainBudget; ++grainIndex) {
      // Deadline gate: a burst grain may only run while the next playback service still has room. This is the same
      // measure the Snapshot idle lane uses; without it a backlog turn could cross the audio deadline.
      if (grainIndex != 0 && backlog) {
        const uint32_t remainingUs =
            RUNTIME_TIMING_REMAINING_UNTIL_NEXT_PLAYBACK_SERVICE_US(micros());
        if (remainingUs < kStopGrainBurstMinRemainingUs) break;
      }
      const CaptureStopJob* headBefore = captureStopJobHead();
      const uint64_t intervalBefore = headBefore != nullptr ? headBefore->interval : 0;
      const auto phaseBefore = headBefore != nullptr ? headBefore->admission.phase
                                                     : PendingStopPhase::DeferredCleanup;
#if defined(SESSION_CAPTURE)
      const auto phase = captureStopJob().admission.phase;
      const auto generation = captureStopJob().admission.frozenGeneration;
      const auto epoch = captureStopJob().admission.transportCommandEpoch;
      const auto destination = captureStopJob().admission.destination;
      const uint8_t slot = captureStopJob().slot;
      const uint8_t track = resolveTrackIndexForPersistence(*this);
      uint32_t startUs = 0;
      const auto boundary = RuntimeTimingTelemetry::captureStopGrainBoundary(startUs);
      const uint32_t slackUs = boundary.periodUs == 0 ? 0xFFFFFFFFu
          : (static_cast<int32_t>(boundary.deadlineUs - startUs) > 0
              ? boundary.deadlineUs - startUs : 0u);
#endif
      (void)processDeferredOverdubStopGrain();
#if defined(SESSION_CAPTURE)
      const uint32_t endUs = micros();
      RuntimeTimingTelemetry::recordStopGrain(track, slot, static_cast<uint8_t>(phase),
          generation, epoch, static_cast<uint8_t>(destination), startUs, endUs, slackUs,
          !captureStopJob().admission.pending || captureStopJob().admission.phase != phase, &boundary);
#endif
      ++grainsThisTurn;
      if (!hasPendingDeferredOverdubStop()) break;
      const CaptureStopJob* nextHead = captureStopJobHead();
      if (nextHead == nullptr || nextHead->mode != CapturePhase::Overdub) break;  // Record keeps the old cadence
      // A phase legitimately needs several grains (CanonicalPrefix measured ~9 per job across 189 grains for 20
      // jobs), so an unchanged phase is not "no progress" -- the per-turn grain cap and the time budget are what keep
      // this turn bounded. A refusing grain is cheap (~100 us), and the cap bounds even a fully refused burst.
      (void)intervalBefore; (void)phaseBefore;
#if defined(SESSION_CAPTURE)
      if (backlog && micros() - burstStartUs >= kStopGrainBurstBudgetUs) break;
#endif
    }
    gStopGrainBurstLastTurnGrains = grainsThisTurn;
    // DEC-233 decision 3: a successor Start retained behind this interval's Stop must not wait for
    // publication or cleanup. It is driven **after** the job's own grain, so the successor opens while
    // the predecessor is still queued and the predecessor keeps its progress. A Stop request is never
    // driven here (it would stack a second stop job). Scope is the **overdub** lane: a Record job's
    // geometry/filtering is still provisional, and starting over it was measured to open an overdub on
    // a take that later publishes empty.
    const CaptureStopJob* successorOwner = captureStopJobHead();
    if (hasAcceptedUnexecutedInteraction() && successorOwner != nullptr &&
        successorOwner->mode == CapturePhase::Overdub) {
      const PendingInteractionLatch* successor = interactionLatch();
      if (successor != nullptr && successor->kind == PendingInteractionLatch::Kind::Start) {
        (void)driveAcceptedInteraction();
      }
    }
    // The pending-stop branch also owns a VisualCache source opportunity. It
    // must not exclude the committed display source for the entire Stop FIFO.
    // Use one source grain: private work first, otherwise the gap display source.
    if (deferredOwner == MidiServiceDrain::DeferredIdleOwner::VisualCache && loopsAllocated() &&
        isAudiblePlaybackActive()) {
      Loop& loop = getActiveLoop();
      if (!deferBackgroundVisualSource && loop.isPlaybackSessionExitPublicationGap() &&
          !loop.privateSourceOwedForSeal())
        (void)loop.stepVisualCacheSourceContinuation();
      else if (isOverdubbing())
        (void)stepPrivatePredecessorSourceGrain(loop);
    }
    return;
  }
  // Deferred canonical convergence rides the Snapshot lane, never the VisualCache slice: it drives
  // the same canonical builders the inline capture stop owns, and it must never starve the display's
  // bounded source work. It is placed **before** the publication gates for one structural reason:
  // convergence correctness must not depend on the publication lane being able to advance (a
  // publication held by a live overdub would otherwise starve the obligation forever). One grain per
  // Snapshot turn, and the rest of this turn still runs, so the lane keeps its own work.
  // `Owed` itself is the eligibility -- a later turn observes an already-ready authority without any
  // caught publication transition.
  if (deferredOwner == MidiServiceDrain::DeferredIdleOwner::Snapshot) {
    (void)serviceDeferredCanonicalConvergence();
  }
  // A presentation-held Record interval still depends on the normal Snapshot owner for
  // its queued root. Preserve that owner priority; presentation retirement may run only
  // after the root opportunity has been serviced.
  if (deferredOwner == MidiServiceDrain::DeferredIdleOwner::Snapshot &&
      hasSnapshotPublicationBacklogOnAnySlot()) {
    processSnapshotOwnerTurn(loopWithPendingSnapshotPublish(), snapshotConstructionUnits);
    return;
  }
  // A retained display frame covers only its own interval. During root publication
  // the cache slice is intentionally disabled, but its bounded source must still
  // progress so a moving viewport can obtain independently qualified content.
  // Keep stop/Snapshot/private-source priority and spend only this producer grain.
  if (!deferBackgroundVisualSource &&
      deferredOwner == MidiServiceDrain::DeferredIdleOwner::VisualCache && loopsAllocated() &&
      isAudiblePlaybackActive()) {
    Loop& loop = getActiveLoop();
    if (loop.isPlaybackSessionExitPublicationGap() && !loop.privateSourceOwedForSeal() &&
        loop.stepVisualCacheSourceContinuation()) return;
  }
  // Record finalization is complete once the interaction job is promoted. Its frozen reader
  // obligation is a presentation-owned descriptor and must be serviced independently of the
  // FIFO interaction ring, so a successor stop can take the next ring head.
  //
  // DEC-216 (mask bit 7, `obligation`): this block is the one class of per-track work that is neither a
  // visual-cache nor a snapshot child and can run a long indivisible step when it fires -- a stop
  // presentation retirement, a staging lease release, or a transfer-horizon top-up (which may grow the
  // pass vector). It was invisible in `idle_maint,breakdown`'s `residual`, which is how a 283,622 us live
  // turn (session_20261030_114009) could be reported with only 2 us of admitted child work. One scope, one
  // bit, no behavioural change.
  {
    RuntimeTimingTelemetry::IdleMaintChildScope obligationChild(7);
    if (serviceCaptureStopPresentations()) return;
    // Active-role lease return: after the last custody of this Track retires, the surplus
    // frozen roles go back to the pool. Bounded and refused while any interval still owes
    // rows, so this never shortens an admitted overlap.
    releaseSurplusCaptureStaging();
    // DEC-160 item 1: an empty Record take whose geometry policy a successor blocked applies it
    // here once this Track owes no capture and no custody. Same admission gate as the role return
    // above, so it can never rewrite a live or sealed successor's geometry.
    settleEmptyRecordGeometry();
    // Task 6.4: before the accepted-interaction re-drive, because a stop retained on a lapsed transfer
    // horizon can only be served once this turn has restored the row it waits for.
    if (replenishLiveOverdubTransferHorizon()) return;
  }
  // sits immediately after the admitted stop's grain -- which owns the turn whenever a job
  // exists -- and before the owner-specific branches, so the latch is reachable on every
  // deferred owner lane (the VisualCache branch below is focus-track-only).
  if (serviceAcceptedInteractionTurn()) return;
  // DEC-067 acceptance edge: once a retired deferred stop's finalization resources
  // are admitted again (canonical copy target empty and the transfer chunks back),
  // the successor's own stop may be admitted. Report it once per job so the HITL
  // probe can drive the STOP retry from an edge instead of a delay.
  if (deferredOwner == MidiServiceDrain::DeferredIdleOwner::VisualCache) {
    // DEC-067: the marker belongs to the runtime of the job that **retired**
    // (`TrackPreparedStopCommit` arms it on `runtime` for `stop.slot`). This read `activeLoopIndex`, which is
    // the slot active *now* -- and by the time this maintenance turn runs the active slot has usually moved,
    // so the edge was reported against the wrong slot, the retiring slot's marker stayed armed forever, and
    // this turn's record was whatever *other* slot happened to be armed. Measured on device: the only release
    // row was `canonical_release,5,...` (B's slot) while A's slot 4 never appeared, even though A's retirement
    // is what freed the capacity B's own stop then used. Scanning names the slot that actually retired, and
    // it also fixes the *latency*: previously the record could not fire until the armed slot happened to be
    // the active one.
    for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
      auto* runtime = playbackRuntime.slotIfAllocated(slot);
      if (runtime == nullptr || !runtime->canonicalReleaseMarkerArmed) continue;
      if (runtime->captureStopOwner != nullptr || !runtime->canonicalCaptureOutputAdmitted()) continue;
      const uint32_t generation = runtime->canonicalReleaseMarkerGeneration;
      const uint64_t interval = runtime->canonicalReleaseMarkerInterval;
      runtime->canonicalReleaseMarkerArmed = false;
      runtime->canonicalReleaseMarkerGeneration = 0;
      runtime->canonicalReleaseMarkerInterval = 0;
      // Human log line first: the HITL probe paces an admitted stop from this edge; the `#CAP` tiers queue
      // behind the deferred stop's flood and arrive seconds late. Slot, identity and interval are named so
      // the edge is attributable even when the firmware slot is reused by a seeded predecessor.
      logger.info("Reentry released slot=%u generation=%lu interval=%llu", static_cast<unsigned>(slot),
                  static_cast<unsigned long>(generation), static_cast<unsigned long long>(interval));
#if defined(SESSION_CAPTURE)
      DebugSessionCapture::canonicalFinalizationCapacityReleased(
          slot, generation, getLoop(slot).loopLengthTicks,
          runtime->canonicalCaptureChunks.availableChunks());
      DebugSessionCapture::canonicalFinalizationCapacityReleasedForInterval(
          slot, interval, generation, getLoop(slot).loopLengthTicks,
          runtime->canonicalCaptureChunks.availableChunks());
#endif
    }
  }
  // S4.5: a descriptor the retirement path left un-rearmed is finished here (one bounded grain).
  // It sits above the lower-priority idle owners so a lost re-arm cannot starve behind them, and
  // below the stop and accepted-interaction obligations so a live stop always keeps the turn.
  // One physical-credit refill is one VisualCache owner turn. A full pool
  // leaves other maintenance eligible; it must not create a busy retry loop.
  if (deferredOwner == MidiServiceDrain::DeferredIdleOwner::VisualCache && loopsAllocated() &&
      LoopEventStore::freeChunkCount() != 0) {
    auto* runtime = playbackRuntime.slotIfAllocated(activeLoopIndex);
    if (runtime && runtime->canonicalCaptureOutput.store.empty() &&
        runtime->canonicalCaptureOutput.store.reservedAppendReady() &&
        runtime->canonicalCaptureChunks.availableChunks() < PassConfig::ACTIVE_OVERDUB_TRANSFER_CHUNKS) {
      (void)runtime->replenishCanonicalCaptureChunk();
      return;
    }
  }
  if (isPlaying()) {
    auto* runtime = playbackRuntime.slotIfAllocated(activeLoopIndex);
    const auto phase = getActiveLoop().lastTickInLoop;
    if (runtime && phase != UINT32_MAX && runtime->serviceBridgeWindow(
        phase, Config::TICKS_PER_BAR, MidiServiceDrain::kCanonicalPrefixInputQuantum)) return;
  }
  updateGrowingRecordLoopLength(clockManager.getCurrentTick());
  Loop& activeLoop = getActiveLoop();
  // Keep the prewarmed first-record bar current on the existing playback
  // preparation owner. One fill/order grain is allowed per idle turn, so this
  // work cannot stack with snapshot or visual-cache maintenance.
  if (deferredOwner == MidiServiceDrain::DeferredIdleOwner::VisualCache && isRecording()) {
    auto* runtime = playbackRuntime.slotIfAllocated(activeLoopIndex);
    if (runtime != nullptr) {
      const PlaybackViewSourceIdentity source{activeLoop.playbackRevision, playbackGeneration,
                                              activeLoop.publishedSnapshotIdentity()};
      if (runtime->serviceFirstRecordingBar(activeLoop, source, Config::TICKS_PER_BAR,
                                             MidiServiceDrain::kCanonicalPrefixInputQuantum)) {
        return;
      }
    }
  }
  // Free the seal-handoff rows once a committed tier carries them. Paint reads the coverage
  // predicate itself, so this only reclaims the vector and never gates correctness.
  activeLoop.releaseSealedCapturePreviewIfCovered();
  activeLoop.releaseRetiredSealedDisplayIfCovered();
  if (deferredOwner == MidiServiceDrain::DeferredIdleOwner::VisualCacheWrapAppend) {
    if (activeLoop.hasDeferredVisualCacheWrapAppend()) {
      activeLoop.applyDeferredVisualCacheWrapAppend();
    }
    return;
  }

  if (deferredOwner == MidiServiceDrain::DeferredIdleOwner::Snapshot) {
    processSnapshotOwnerTurn(loopWithPendingSnapshotPublish(), snapshotConstructionUnits);
  }

  // Title drain must not pass-gather a loaded loop (`015331`). After
  // boot_commit, Stage 2.1 fallback may fill from passes until a current
  // root exists (`022447` STOPPED `DISP` stayed at 0 notes). This gate
  // does not depend on auto loaded hydration (`023222`).
  if (deferredOwner == MidiServiceDrain::DeferredIdleOwner::VisualCache &&
      StorageManager::isBootTitleLoadDrain() &&
      activeLoop.hasCommittedPasses() && !activeLoop.publishedSnapshotIsCurrent()) {
    return;
  }

  // REVT and stored-MIDI verification. The stored-content verification still needs a fully idle
  // transport: its first slice flattens the active passes and blocks MIDI for seconds on grown
  // loops (session_20260813_020631). The Record scan (REVT) does not: its queue time already copies
  // the committed chunk ids, so the slice is a bounded chunk walk. It must be allowed to run while a
  // loop plays, because the accepted-sequence window is continuously playing by contract and the
  // take's stored note-on grid is otherwise never emitted (record_stored_revt_missing).
  const bool transportFullyIdle =
      !isAudiblePlaybackActive() && !isRecording() && !isStoppedRecording();
  const bool playbackIdleOwner =
      isAudiblePlaybackActive() && !isRecording() && !isStoppedRecording();
  if (transportFullyIdle) {
    size_t revtSlice = 64;
    size_t verificationSlice = timingCritical ? 4 : 64;
    if (StorageManager::hasDeferredSaveWork()) {
      revtSlice = 8;
      verificationSlice = timingCritical ? 4 : 16;
    }
    processDeferredRecordRevts(revtSlice, /*allowMerge=*/true);
    // Task 7.1/7.2: the effective notes derive one bar per turn and the SEVT rows emit four notes per
    // timing-critical turn (the measured 54-row burst cost 44 ms). The bounded child may still be
    // denied by the owner gate if its grain cannot fit; its queue survives for a later turn.
    if (MidiServiceDrain::boundedIdleOwnerChildMayRun(
            true, timingCritical, MidiServiceDrain::kStoredVerificationMaxUs,
            MidiServiceDrain::kOwnerWorkBudgetUs,
            MidiServiceDrain::kStoredVerificationFineGrainUs)) {
      processDeferredStoredMidiVerification(verificationSlice);
    } else {
      emitIdleOwnerDeferral(1, "stored_midi_verification", nowMs);
    }
    processDeferredContentResolutionDeviceGate();
  } else if (playbackIdleOwner) {
    processDeferredRecordRevts(32, /*allowMerge=*/false);
  }

  const bool deferredDerivedViewMaintenance =
      (isAudiblePlaybackActive() || isStoppedRecording()) && !isRecording() &&
      deferredOwner == MidiServiceDrain::DeferredIdleOwner::VisualCache;
  bool idleVisualSliceRan = false;
  uint8_t idleVisualBarsPerSlice = 0;
  uint16_t idleVisualDirtyBefore = 0;
  uint16_t idleVisualDirtyAfter = 0;
  if (deferredDerivedViewMaintenance) {
    Loop& loop = activeLoop;
    const bool postOverdubDrainWindow =
        postOverdubDrainWindowActive && (isPlaying() || isStoppedRecording());
    if (!postOverdubDrainWindow) {
      overdubDrainFallbackRetryAtMs_ = 0;
    }
    const bool deferredSavePending = StorageManager::hasDeferredSaveWork();
    const bool constrainedFallbackRequested = postOverdubDrainWindow;
    if (!constrainedFallbackRequested) {
      overdubDrainFallbackRetryAtMs_ = 0;
    }
    const bool constrainedFallbackCooldownActive =
        constrainedFallbackRequested && overdubDrainFallbackRetryAtMs_ != 0 &&
        nowMs < overdubDrainFallbackRetryAtMs_;
    const bool constrainFallbackGather =
        postOverdubDrainWindow && !constrainedFallbackCooldownActive;
    const bool overdubDrainOwnerTrack = playingMidiDrainAfterOverdubStopActive();
    const bool allowVisualMaintenanceSlice =
        (!postOverdubDrainWindow || overdubDrainOwnerTrack) &&
        !constrainedFallbackCooldownActive;
    if (allowVisualMaintenanceSlice && isOverdubbing() && loop.privatePredecessorSourcePending()) {
      // CONTRACTS.md C3.3: the one contracted wakeup, one grain per turn. Tasks 4.7/4.8 retired the
      // moving-window producer that used to live here: the coverage worker publishes tile content and its
      // prepared bit together, and the view is recut there, so this lane adds no second source writer.
      (void)stepPrivatePredecessorSourceGrain(loop);
      const uint32_t refreshTick =
          loop.lastTickInLoop != UINT32_MAX ? loop.lastTickInLoop : loop.playheadPhaseTick;
      loop.rebuildOverdubSourceView(refreshTick, "idle");
    } else if (allowVisualMaintenanceSlice && !deferBackgroundVisualSource &&
               !(postOverdubDrainWindow && loop.pendingSnapshotSessionExitQueued() &&
                 !loop.publishedSnapshotIsCurrent()) &&
               MidiServiceDrain::allowVisualCacheReconstructSlice(isOverdubbing()) &&
               loop.hasCommittedPasses() && loop.visualCacheDirty) {
      uint8_t barsPerSlice = 4;
      if (deferredSavePending) {
        barsPerSlice = 2;
      }
      if (postOverdubDrainWindow && barsPerSlice > 1) {
        barsPerSlice = 1;
      }
      idleVisualBarsPerSlice = barsPerSlice;
      idleVisualDirtyBefore = countVisualCacheDirtyBars(loop);
      uint32_t playheadBar = 0;
      if (loop.lastTickInLoop != UINT32_MAX) {
        playheadBar = visualBarForTick(loop.lastTickInLoop, Config::TICKS_PER_BAR);
      }
      const uint32_t totalBars =
          (loop.loopLengthTicks + Config::TICKS_PER_BAR - 1) / Config::TICKS_PER_BAR;
      // One slice per maintenance: alternate playhead vs loop-tail priority so tail fills
      // over time without doubling gather cost in one main-loop iteration (122003 MIDI lag).
      static bool sPrioritizeLoopTailVisualCache = false;
      uint32_t priorityBar = playheadBar;
      if (totalBars > 0 && sPrioritizeLoopTailVisualCache) {
        priorityBar = totalBars - 1;
      }
      sPrioritizeLoopTailVisualCache = !sPrioritizeLoopTailVisualCache;
      // PLAYING/OVERDUB: backfill near the playhead/paint window so idle reconstruct does not
      // race the OLED path across a full long loop (session_20260811_030614).
      constexpr uint32_t kPlayingVisualCacheNeighborhoodBars =
          DisplayWindowUtils::kMaxDetailedWindowBars + 4u;
      uint32_t maxBarDistanceFromPriority = kPlayingVisualCacheNeighborhoodBars;
      // Partial adopt (RC-E): some bars clean and some dirty — the neighborhood cap leaves a
      // dead zone on long loops (bars 36–62 on an 84-bar loop). Allow any dirty bar while mixed.
      if (loop.visualCache.dirtyBars.size() == totalBars && totalBars > 0) {
        bool sawCleanBar = false;
        bool sawDirtyBar = false;
        for (uint8_t flag : loop.visualCache.dirtyBars) {
          if (flag != 0) {
            sawDirtyBar = true;
          } else {
            sawCleanBar = true;
          }
          if (sawCleanBar && sawDirtyBar) {
            // Post-overdub drain window: keep neighborhood-bounded slices to avoid
            // one-turn long-loop dirty-bar walks that starve MIDI input.
            if (!constrainedFallbackRequested) {
              maxBarDistanceFromPriority = UINT32_MAX;
            }
            break;
          }
        }
      }
      // Runtime-priority policy: defer wrap-pair append while realtime playback/capture
      // states are active; resume in STOPPED idle maintenance.
      const bool allowOverdubWrapAppend = false;
      constexpr size_t kRealtimeFallbackGatherEventBudget = 192;
      constexpr size_t kPostOverdubDrainFallbackGatherEventBudget = 96;
      const size_t fallbackGatherEventBudget =
          postOverdubDrainWindow ? kPostOverdubDrainFallbackGatherEventBudget
                                 : kRealtimeFallbackGatherEventBudget;
      // C5.2a/P-D: the bounded content source runs before the slice so the slice can observe a
      // published frame. One grain per turn, on this lane, for this loop's exact request.
      (void)loop.stepVisualCacheSourceContinuation();
      loop.rebuildVisualCacheIdleSlice(barsPerSlice, priorityBar,
                                       maxBarDistanceFromPriority, allowOverdubWrapAppend,
                                       constrainFallbackGather, fallbackGatherEventBudget);
      idleVisualSliceRan = true;
      idleVisualDirtyAfter = countVisualCacheDirtyBars(loop);
      if (constrainFallbackGather) {
        constexpr uint32_t kOverdubDrainFallbackRetryDelayMs = 250;
        if (idleVisualDirtyAfter >= idleVisualDirtyBefore) {
          overdubDrainFallbackRetryAtMs_ =
              nowMs + kOverdubDrainFallbackRetryDelayMs;
        } else {
          overdubDrainFallbackRetryAtMs_ = 0;
        }
      }
    }
  }

  // Touched-coverage worker (task 4.6/4.7): one declared grain of the next owed tile, published atomically
  // when that tile's derive completes. Marked demand is served here and nowhere else; the mark sites on
  // the capture path derive nothing. Runs on the same admitted VisualCache turn as the derived-view work.
  emitOverdubStopDebtProbeIfArmed(activeLoop, idleVisualSliceRan, idleVisualBarsPerSlice,
                                  idleVisualDirtyBefore, idleVisualDirtyAfter);

  if (deferredOwner == MidiServiceDrain::DeferredIdleOwner::VisualCache &&
      !deferBackgroundVisualSource && !isAudiblePlaybackActive() && !isRecording() && !isStoppedRecording()) {
    Loop& loop = getActiveLoop();
    if (loop.hasCommittedPasses()) {
      // Phase 3: queued background restores must not block idle visual work.
      const bool bootHydrateActive =
          SlotLoadSession::isActive() || StorageManager::hasPendingUndoSnapshotHydrate();
      const bool deferHeavyDerivedView =
          bootHydrateActive || StorageManager::hasDeferredSaveWork();
      const bool avoidFullVisual =
          loop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks) || deferHeavyDerivedView;
      const bool visualCacheCleanAtTurnStart = !loop.visualCacheDirty;
        if (loop.visualCacheDirty) {
        // One idle slice per call. Do not ensureVisualCacheBuilt here — short
        // loops use the same resumable owner as long loops (Slice 4c).
        const uint8_t barsPerSlice = StorageManager::hasDeferredSaveWork() ? 2 : 4;
        // 161804 / 165107: budget=0 plus wrap-append on the bar-0 edge paid
        // idle_append 682–906 ms in one STOPPED turn. PLAYING already budgets
        // gather and defers wrap-append; STOPPED uses the same pair. Prewarm
        // stays on a later clean-cache turn (160519 / Stage B).
        // C5.2a/P-D: bounded content source before the slice on the STOPPED lane too.
        (void)loop.stepVisualCacheSourceContinuation();
        constexpr size_t kStoppedFallbackGatherEventBudget = 192;
        loop.rebuildVisualCacheIdleSlice(barsPerSlice, 0, UINT32_MAX, false, false,
                                         kStoppedFallbackGatherEventBudget);
      }
      bool ranDeferredWrapAppend = false;
      if (!loop.visualCacheDirty && loop.hasDeferredVisualCacheWrapAppend()) {
        // The append itself is bounded; its revision-moved fallback is the unbounded whole-pass resolve.
        // Admit that fallback separately so a timing-critical turn keeps the append pending (lossless).
        const bool allowWholeLoopResolve =
            MidiServiceDrain::unboundedIdleOwnerChildMayRun(true, timingCritical);
        if (!allowWholeLoopResolve) {
          emitIdleOwnerDeferral(2, "whole_loop_resolve", nowMs);
        }
        loop.applyDeferredVisualCacheWrapAppend(allowWholeLoopResolve);
        ranDeferredWrapAppend = true;
      }
      // After boot_commit and on a later clean-cache turn only. Same-turn
      // prewarm after slice_clean hung 160519; 162928 opened notes=0 without
      // any caller.
      if (visualCacheCleanAtTurnStart && !loop.visualCacheDirty && !ranDeferredWrapAppend &&
          !StorageManager::isBootTitleLoadDrain()) {
        // Task 2.3: the whole-loop span prewarm is unbounded and optional; it is never dispatched on a
        // timing-critical turn. No consumer may depend on it (D3: per-region readiness is the contract).
        if (MidiServiceDrain::unboundedIdleOwnerChildMayRun(true, timingCritical)) {
          loop.prewarmOverdubSourceSpanCache();
        } else {
          emitIdleOwnerDeferral(0, "overdub_span_prewarm", nowMs);
        }
      }
      maybeLogStoredNoteCount();
      maybeQueueContentResolutionDeviceGate();
    }
  }

  if (!DeferredValidatePolicy::shouldRunDeferredFullValidate(
          deferredFullMidiValidate, isAudiblePlaybackActive(), isRecording(),
          isOverdubbing() || hasPendingDeferredOverdubStop(),
          deferredValidateQueuedAtMs, nowMs)) {
    if (deferredFullMidiValidate && deferredValidateQueuedAtMs == 0) {
      deferredValidateQueuedAtMs = nowMs;
    }
    return;
  }
  deferredFullMidiValidate = false;
  deferredValidateQueuedAtMs = 0;
  validateAndCleanupMidiEvents();
}

void Track::resetDeferredRecordRevts() {
  // Releases the reference `queueDeferredRecordRevts` took when it borrowed the producer's chunk
  // ids. It is a no-op for the merge path, which never copied a directory.
  LoopEventStore::releaseChunkRefs(deferredRecordRevtChunkRefs);
  deferredRecordRevtsSlot = Config::INVALID_LOOP_SLOT;
  deferredRecordRevtChunkScan = false;
  deferredRecordRevtCursor = 0;
  deferredRecordRevtEvents.clear();
  deferredRecordRevtChunkRefs.clear();
  deferredRecordRevtChunkCursor = 0;
  deferredRecordRevtChunkEvents.clear();
  deferredRecordRevtChunkEventCursor = 0;
}

void Track::queueDeferredRecordRevts(uint8_t slot) {
  if (!loopsAllocated() || slot >= Config::MAX_LOOPS_PER_TRACK) {
    resetDeferredRecordRevts();
    return;
  }
  const Loop& loop = getLoop(slot);
  if (!loop.hasCommittedPasses()) {
    resetDeferredRecordRevts();
    return;
  }

  resetDeferredRecordRevts();
  deferredRecordRevtsSlot = slot;

  const bool hasActiveRecordPass =
      loop.passes.hasRecordPass() &&
      loop.passes.recordPass.state == CapturePassState::Active &&
      !loop.passes.recordPass.committedChunkIds.empty();
  bool hasActiveOverdubPass = false;
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    if (pass.state == CapturePassState::Active && !pass.committedChunkIds.empty()) {
      hasActiveOverdubPass = true;
      break;
    }
  }

  // Fast path: record-stop baseline has one active record pass and no active overdub passes.
  if (hasActiveRecordPass && !hasActiveOverdubPass) {
    deferredRecordRevtChunkScan = true;
    if (!LoopEventStore::tryCopyCommittedChunkIds(deferredRecordRevtChunkRefs,
                                                  loop.passes.recordPass.committedChunkIds)) {
      resetDeferredRecordRevts();
    } else {
      // The copy borrows the producer's chunk references. Pin them for the deferral: the scan runs
      // in a later turn, and the publication/retirement that runs first releases its own directory,
      // which frees the chunks. `appendChunkRefEvent` then returns nothing for a freed id, so the
      // take reported zero stored notes with no error (`captures/s4f22-step3-base-01`:
      // `Queued REVT note-ons (deferred): 0`, zero `REVT` rows, and a failed stored-grid check on a
      // take that had recorded, published and played). `resetDeferredRecordRevts` releases the pin.
      LoopEventStore::retainChunkRefs(deferredRecordRevtChunkRefs);
    }
  }
}

void Track::processDeferredRecordRevts(size_t maxEventsPerSlice, bool allowMerge) {
  RuntimeTimingTelemetry::IdleMaintChildScope idleChild(6);
  // The interval that queued this scan names itself; the active slot may have moved (and may hold
  // a successor take) by the time the idle owner runs.
  const uint8_t slot = deferredRecordRevtsSlot;
  if (slot == Config::INVALID_LOOP_SLOT) {
    return;
  }

  if (!loopsAllocated() || slot >= Config::MAX_LOOPS_PER_TRACK ||
      !getLoop(slot).hasCommittedPasses()) {
    resetDeferredRecordRevts();
    return;
  }
  const Loop& loop = getLoop(slot);

  if (deferredRecordRevtChunkScan) {
    size_t queued = 0;
    size_t chunksRead = 0;
    while (queued < maxEventsPerSlice) {
      if (deferredRecordRevtChunkEventCursor >= deferredRecordRevtChunkEvents.size()) {
        deferredRecordRevtChunkEvents.clear();
        deferredRecordRevtChunkEventCursor = 0;
        if (deferredRecordRevtChunkCursor >= deferredRecordRevtChunkRefs.size()) {
          break;
        }
        const uint16_t chunkId = deferredRecordRevtChunkRefs[deferredRecordRevtChunkCursor++];
        LoopEventStore::appendChunkRefEvent(chunkId, deferredRecordRevtChunkEvents);
        ++chunksRead;
        continue;
      }

      const MidiEvent& evt =
          deferredRecordRevtChunkEvents[deferredRecordRevtChunkEventCursor++];
      if (!evt.isNoteOn()) {
        continue;
      }
      SC_REC_QUEUE_STORED_NOTE_ON(evt.tick, evt.channel, evt.data.noteData.note);
      ++queued;
    }

    const bool done =
        deferredRecordRevtChunkCursor >= deferredRecordRevtChunkRefs.size() &&
        deferredRecordRevtChunkEventCursor >= deferredRecordRevtChunkEvents.size();
    if (done) {
      // `refs` and `events` make a zero readable: refs=0 is an empty directory, while
      // refs>0 with events=0 means the chunk could not be read (a freed/never-owned id).
      logger.log(CAT_TRACK, LOG_DEBUG,
                 "Queued REVT note-ons (deferred): %d (mode=chunk refs=%u events=%u chunksRead=%u)",
                 static_cast<int>(queued), static_cast<unsigned>(deferredRecordRevtChunkRefs.size()),
                 static_cast<unsigned>(deferredRecordRevtChunkEvents.size()),
                 static_cast<unsigned>(chunksRead));
      resetDeferredRecordRevts();
    }
    return;
  }

  if (!allowMerge) {
    // Playback slice: the chunk walk above is bounded per turn, but flattening the active
    // passes is not. Leave the merge for a fully idle transport rather than blocking MIDI.
    return;
  }
  if (deferredRecordRevtEvents.empty() && deferredRecordRevtCursor == 0) {
    loop.mergeActiveCapturePasses(deferredRecordRevtEvents);
  }

  size_t queued = 0;
  while (deferredRecordRevtCursor < deferredRecordRevtEvents.size() &&
         queued < maxEventsPerSlice) {
    const MidiEvent& evt = deferredRecordRevtEvents[deferredRecordRevtCursor++];
    if (evt.isNoteOn()) {
      SC_REC_QUEUE_STORED_NOTE_ON(evt.tick, evt.channel, evt.data.noteData.note);
      ++queued;
    }
  }

  if (deferredRecordRevtCursor >= deferredRecordRevtEvents.size()) {
    logger.log(CAT_TRACK, LOG_DEBUG, "Queued REVT note-ons (deferred): %d",
               static_cast<int>(queued));
    resetDeferredRecordRevts();
  }
}

namespace {
/// The native harness has no restore sweep; the gate's decision itself is pinned by its own test there.
bool bootRestoreSweepPending() {
#if defined(PIO_UNIT_TEST_NATIVE)
  return false;
#else
  return StorageManager::hasPendingLoopSlotRestore();
#endif
}
}  // namespace

#if defined(__IMXRT1062__)
DMAMEM PublicationIdentityRefusal sLastPublicationIdentityRefusal = PublicationIdentityRefusal::None;
#else
PublicationIdentityRefusal sLastPublicationIdentityRefusal = PublicationIdentityRefusal::None;
#endif

void Track::processSnapshotOwnerTurn(Loop& snapshotLoop, uint8_t snapshotConstructionUnits) {
  RuntimeTimingTelemetry::IdleMaintChildScope idleChild(0);
  // Rootless committed playback has neither the sealed Record reader nor a root
  // for the merged fallback. Reuse launch's on-demand publisher on this deferred
  // owner, once, without resetting any publication/capture/receipt owner.
  // **DEC-194 — during the boot restore sweep the shared session belongs to the loads.** Not only recovery: *any*
  // snapshot construction (hydrated-root publication included) competes for the same deferred turn and the same SD
  // session, and the measured waits (3-25 s per slot, 0.19 s of actual work) come from that competition. A root is an
  // accelerator; nothing in the boot set needs one before its content is back. The sweep is short, so the root work
  // starts immediately afterwards.
  if (bootRestoreSweepPending()) return;
  if (playbackSourceRecoveryNeededForSlot(playbackSlotIndexForLoop(snapshotLoop))) {
    (void)snapshotLoop.requestCommittedPassesRepublish();
    // DEC-192 (report-only): name why a repeat request was not idempotent, once per change.
    const PublicationIdentityRefusal identity = loopLastPublicationIdentityRefusal(snapshotLoop);
    if (identity != PublicationIdentityRefusal::None &&
        identity != sLastPublicationIdentityRefusal) {
      sLastPublicationIdentityRefusal = identity;
      reportPlaybackViewMaintenanceRow(playbackSlotIndexForLoop(snapshotLoop), snapshotLoop,
                                       publicationIdentityRefusalName(identity));
    } else if (identity == PublicationIdentityRefusal::None) {
      sLastPublicationIdentityRefusal = identity;
    }
    return; // Request/enumeration is its own turn; construction keeps its normal quantum.
  }
  const bool havePublishedSnapshot = static_cast<bool>(snapshotLoop.publishedSnapshot());
  const bool visualCacheNeedsFirstPaint =
      snapshotLoop.hasCommittedPasses() && snapshotLoop.visualCacheDirty &&
      snapshotLoop.visualCache.notes.empty();
  uint32_t recordElapsedTicks = 0;
  if (snapshotLoop.startLoopTick != UINT32_MAX) {
    const uint32_t currentTick = clockManager.getCurrentTick();
    if (currentTick >= snapshotLoop.startLoopTick) {
      recordElapsedTicks = currentTick - snapshotLoop.startLoopTick;
    }
  }
  const bool liveRecordPastFirstBar = MidiServiceDrain::allowLiveRecordSnapshotStart(
      isRecording(), recordElapsedTicks, Config::TICKS_PER_BAR);
  const bool hasWrapJournal = snapshotLoop.hasPendingWrapJournal();
  const uint32_t liveRecordCompletedTicks = MidiServiceDrain::liveRecordCompletedTicks(
      recordElapsedTicks, Config::TICKS_PER_BAR);
  const bool snapshotPending =
      snapshotLoop.hasPendingSnapshotPublish() ||
      (isRecording() && !havePublishedSnapshot &&
       liveRecordCompletedTicks > snapshotLoop.liveRecordAppliedCompletedTicks() &&
       snapshotLoop.liveRecordCanApplyCompletedTicks(liveRecordCompletedTicks));
  const bool snapshotIdleAllowed =
      MidiServiceDrain::allowSnapshotIdleStep(
          isRecording(), isPlaying(), isOverdubbing(), havePublishedSnapshot,
          snapshotLoop.hasInFlightSnapshotConstruction(), visualCacheNeedsFirstPaint,
          hasWrapJournal, snapshotPending) &&
      (liveRecordPastFirstBar || snapshotLoop.hasInFlightSnapshotConstruction() ||
       hasWrapJournal || snapshotLoop.pendingSnapshotSessionExitQueued());
  const bool snapshotEligible =
      snapshotPending && snapshotIdleAllowed &&
      !(snapshotLoop.pendingSnapshotFromLoadedPasses() &&
        !MidiServiceDrain::allowLoadedSnapshotHydration(
            false, StorageManager::isBootTitleLoadDrain(),
            StorageManager::bootInteractiveReady()));
#if SC_QUALIFICATION_ROWS_ENABLED
  reportSnapshotOwnerGateRow(
      playbackSlotIndexForLoop(snapshotLoop), activeLoopIndex, snapshotConstructionUnits,
      liveRecordPastFirstBar, visualCacheNeedsFirstPaint,
      MidiServiceDrain::allowSnapshotIdleStep(
          isRecording(), isPlaying(), isOverdubbing(), havePublishedSnapshot,
          snapshotLoop.hasInFlightSnapshotConstruction(), visualCacheNeedsFirstPaint,
          hasWrapJournal, snapshotPending),
      liveRecordPastFirstBar || snapshotLoop.hasInFlightSnapshotConstruction() || hasWrapJournal ||
          snapshotLoop.pendingSnapshotSessionExitQueued(),
      snapshotIdleAllowed, snapshotEligible);
#endif
  if (!snapshotEligible) return;

  const bool timingCritical =
      isRecording() || isAudiblePlaybackActive() || isStoppedRecording() || isArmed();
  uint32_t remainingUs = RUNTIME_TIMING_REMAINING_UNTIL_NEXT_PLAYBACK_SERVICE_US(
#if defined(ARDUINO)
      micros()
#else
      0u
#endif
  );
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_SNAPSHOT_IDLE
  if (InvestigationCaptureLog::consumeSnapshotIdleForcedYield() &&
      MidiServiceDrain::kSnapshotIdleReserveYieldEnabled) {
    remainingUs = 0;
  }
#endif
  if (MidiServiceDrain::kSnapshotIdleReserveYieldEnabled &&
      !MidiServiceDrain::allowSnapshotIdleReserve(
          remainingUs, timingCritical, snapshotConstructionUnits)) {
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_SNAPSHOT_IDLE
    InvestigationCaptureLog::appendSnapshotIdleYieldLine(
        remainingUs, MidiServiceDrain::kSnapshotIdleSafetyMarginUs,
        MidiServiceDrain::admittedSnapshotIdleUs(remainingUs),
        MidiServiceDrain::snapshotIdleConstructionCostUs(snapshotConstructionUnits),
        timingCritical, snapshotLoop.hasPendingSnapshotPublish(),
        snapshotLoop.publishedSnapshotRevision());
#endif
    return;
  }
#if defined(SESSION_CAPTURE) && SESSION_CAPTURE_SNAPSHOT_IDLE
  const uint32_t snapshotIdleStartUs = micros();
#endif
  const uint32_t playheadPhaseTick = snapshotLoop.loopLengthTicks == 0
      ? 0
      : tickPhaseInLoop(getEffectivePlaybackTick(clockManager.getCurrentTick()),
                        snapshotLoop.startLoopTick, snapshotLoop.loopLengthTicks);
#if SC_QUALIFICATION_ROWS_ENABLED
  const uint32_t publishedRevisionBeforeStep = snapshotLoop.publishedSnapshotRevision();
#endif
  snapshotLoop.stepPendingSnapshotPublish(
      snapshotConstructionUnits,
      MidiServiceDrain::allowSnapshotConstructionStart(
          isOverdubbing() ||
              (captureStopJobHead() != nullptr && captureStopJobHead()->admission.pending &&
               captureStopJobHead()->mode == CapturePhase::Overdub),
          liveRecordPastFirstBar, hasWrapJournal, havePublishedSnapshot,
          snapshotLoop.pendingSnapshotSessionExitQueued()),
      playheadPhaseTick, liveRecordCompletedTicks);
#if SC_QUALIFICATION_ROWS_ENABLED
  if (snapshotLoop.publishedSnapshotRevision() != publishedRevisionBeforeStep) {
    const bool postPublished = static_cast<bool>(snapshotLoop.publishedSnapshot());
    const bool postFirstPaint = snapshotLoop.hasCommittedPasses() &&
        snapshotLoop.visualCacheDirty && snapshotLoop.visualCache.notes.empty();
    const bool postPending = snapshotLoop.hasPendingSnapshotPublish();
    const bool postWrapJournal = snapshotLoop.hasPendingWrapJournal();
    const bool postInFlight = snapshotLoop.hasInFlightSnapshotConstruction();
    const bool postStepAdmits = MidiServiceDrain::allowSnapshotIdleStep(
        isRecording(), isPlaying(), isOverdubbing(), postPublished, postInFlight,
        postFirstPaint, postWrapJournal, postPending);
    const bool postClauseAdmits = liveRecordPastFirstBar || postInFlight ||
        postWrapJournal || snapshotLoop.pendingSnapshotSessionExitQueued();
    const bool postIdleAllowed = postStepAdmits && postClauseAdmits;
    const bool postEligible = postPending && postIdleAllowed &&
        !(snapshotLoop.pendingSnapshotFromLoadedPasses() &&
          !MidiServiceDrain::allowLoadedSnapshotHydration(
              false, StorageManager::isBootTitleLoadDrain(),
              StorageManager::bootInteractiveReady()));
    reportSnapshotOwnerGateRow(
        playbackSlotIndexForLoop(snapshotLoop), activeLoopIndex, snapshotConstructionUnits,
        liveRecordPastFirstBar, postFirstPaint, postStepAdmits, postClauseAdmits,
        postIdleAllowed, postEligible);
  }
#endif
#if defined(SESSION_CAPTURE) && SESSION_CAPTURE_SNAPSHOT_IDLE
  InvestigationCaptureLog::appendSnapshotIdleLines(snapshotLoop, snapshotIdleStartUs);
#endif
}

// DEC-189: the two lanes that can strand a publication, read without touching an owner. Called from the
// 1 s diagnostics turn; emits nothing itself (the row is `#STOP_TRACE`, direct serial, so it cannot be shed).
TRACK_COLD_MEM Track::StallCensus Track::stallCensus() const {
  StallCensus census{};
  if (captureStopJobs_ != nullptr && captureStopJobs_->count != 0) {
    const CaptureStopJob& head = captureStopJob();
    census.stopPending = head.admission.pending;
    census.stopMode = static_cast<uint8_t>(head.mode);
    census.stopPhase = static_cast<uint8_t>(head.admission.phase);
    census.stopGeneration = head.admission.frozenGeneration;
    census.orderGrains = head.admission.remainingOrderGrains;
    census.canonicalGrains = head.admission.remainingCanonicalPreparationGrains;
    census.rankedGrains = head.admission.remainingRankedPreparationGrains;
    census.undoGrains = head.admission.remainingUndoCollectionGrains;
  }
  const uint8_t slot = activeLoopIndex;
  if (slot < Config::MAX_LOOPS_PER_TRACK && playbackRuntime.slotIfAllocated(slot) != nullptr) {
    const Loop& loop = loopForSlot(slot);
    census.publishPending = loop.hasPendingSnapshotPublish();
    census.publishQueued = static_cast<uint32_t>(loop.pendingSnapshotQueuedPassCount());
    census.publishApplied = static_cast<uint32_t>(loop.pendingSnapshotAppliedPassCount());
    census.publishInFlight = loop.hasInFlightSnapshotConstruction();
    census.hasCommittedPasses = loop.hasCommittedPasses();
    census.loopLengthTicks = loop.loopLengthTicks;
  }
  return census;
}
