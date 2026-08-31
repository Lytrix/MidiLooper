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
#include "TrackManager.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/MidiServiceDrain.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/IntervalProjection.h"
#include "Utils/LoopEventValidation.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/NoteUtils.h"
#include "Utils/TrackMem.h"
#include "VisualCache.h"

#include <cstdio>

extern TrackManager trackManager;

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

TRACK_COLD_MEM __attribute__((noinline)) void appendSnapshotIdleCaptureLine(
    Loop& snapshotLoop, uint32_t snapshotIdleStartUs) {
  const unsigned long nowUs = static_cast<unsigned long>(micros());
#if defined(SESSION_CAPTURE)
  const uint32_t emitStartUs = micros();
#endif
  const ImmutableLoopSnapshotRef publishedSnapshot = snapshotLoop.publishedSnapshot();
  char snapshotLine[192];
  snprintf(snapshotLine, sizeof(snapshotLine),
           "#CAP,%lu,DIAG,snapshot_idle,us=%lu,pending=%u,root=%p,root_rev=%lu,"
           "playback_rev=%lu,queued=%lu,applied=%lu,construction=%lu,exit=%u,inflight=%u,"
           "journals=%lu,fragment=%u/%u,parent_rev=%lu",
           nowUs, nowUs - static_cast<unsigned long>(snapshotIdleStartUs),
           snapshotLoop.hasPendingSnapshotPublish() ? 1u : 0u, publishedSnapshot.get(),
           static_cast<unsigned long>(snapshotLoop.publishedSnapshotRevision()),
           static_cast<unsigned long>(snapshotLoop.playbackRevision),
           static_cast<unsigned long>(snapshotLoop.pendingSnapshotQueuedPassCount()),
           static_cast<unsigned long>(snapshotLoop.pendingSnapshotAppliedPassCount()),
           static_cast<unsigned long>(snapshotLoop.pendingSnapshotConstructionPassId()),
           snapshotLoop.pendingSnapshotSessionExitQueued() ? 1u : 0u,
           snapshotLoop.hasInFlightSnapshotConstruction() ? 1u : 0u,
           static_cast<unsigned long>(snapshotLoop.pendingSnapshotWrapJournalCount()),
           snapshotLoop.pendingSnapshotWrapFragmentIndex(),
           snapshotLoop.pendingSnapshotWrapFragmentCount(),
           static_cast<unsigned long>(snapshotLoop.pendingSnapshotWrapParentRevision()));
  DebugSessionCapture::appendCaptureTextLine(snapshotLine);
#if defined(SESSION_CAPTURE)
  snapshotLoop.addSnapshotIdleEmitUs(micros() - emitStartUs);
#endif

  char childLine[192];
  snprintf(childLine, sizeof(childLine),
           "#CAP,%lu,DIAG,snapshot_idle,child,start=%lu,pass_change=%lu,step=%lu,"
           "finish=%lu,publish=%lu,probe=%lu,enqueue=%lu,discard=%lu,emit=%lu",
           nowUs, static_cast<unsigned long>(snapshotLoop.lastSnapshotIdleStartUs()),
           static_cast<unsigned long>(snapshotLoop.lastSnapshotIdlePassChangeUs()),
           static_cast<unsigned long>(snapshotLoop.lastSnapshotIdleStepUs()),
           static_cast<unsigned long>(snapshotLoop.lastSnapshotIdleFinishUs()),
           static_cast<unsigned long>(snapshotLoop.lastSnapshotIdlePublishUs()),
           static_cast<unsigned long>(snapshotLoop.lastSnapshotIdleProbeUs()),
           static_cast<unsigned long>(snapshotLoop.lastSnapshotIdleEnqueueUs()),
           static_cast<unsigned long>(snapshotLoop.lastSnapshotIdleDiscardUs()),
           static_cast<unsigned long>(snapshotLoop.lastSnapshotIdleEmitUs()));
  DebugSessionCapture::appendCaptureTextLine(childLine);
  snprintf(childLine, sizeof(childLine),
           "#CAP,%lu,DIAG,snapshot_idle,inner,refill=%lu,event_scan=%lu,"
           "note_lookup=%lu,change_build=%lu,construct=%lu",
           nowUs, static_cast<unsigned long>(snapshotLoop.lastSnapshotIdleRefillUs()),
           static_cast<unsigned long>(snapshotLoop.lastSnapshotIdleEventScanUs()),
           static_cast<unsigned long>(snapshotLoop.lastSnapshotIdleNoteLookupUs()),
           static_cast<unsigned long>(snapshotLoop.lastSnapshotIdleChangeBuildUs()),
           static_cast<unsigned long>(snapshotLoop.lastSnapshotIdleConstructUs()));
  DebugSessionCapture::appendCaptureTextLine(childLine);
}

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
    for (const NoteUtils::DisplayNote& other : actual) {
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
    char line[192];
    if (geom != nullptr) {
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
  loop.gatherCommittedEventsInWindow(oracle, windowStart, windowLength);
  const NoteUtils::DisplayNoteVec oracleNotes =
      NoteUtils::reconstructDisplayNotes(oracle, loop.loopLengthTicks, false, false);
  const uint32_t oracleUs = micros() - oracleStartUs;
  const uint32_t nativeStartUs = micros();
  SessionMidiEventVec nativeFull;
  loop.gatherCommittedEvents(nativeFull);
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
    loop.mergeActiveCapturePasses(materializedEvents);
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

// Wrap-pair verification is quadratic, so it only runs on loops small enough to afford it.
constexpr size_t kMaxWrapPairVerifyEvents = 512;
constexpr size_t kMaxDisplayNoteVerifyLines = 32;

#if defined(SESSION_CAPTURE)
// One stored-MIDI verification dump per boot — enough for a single HITL/evidence capture, then
// off so manual overdub retests are not blocked by merge+SEVT cost on every stop. Reboot to re-arm.
bool sStoredMidiVerificationArmed = true;

void logStoredMidiVerificationArmState(bool armed) {
  Serial.printf("#CAP,DIAG,stored_verify,armed,%u\n", armed ? 1u : 0u);
}
#endif

enum StoredVerificationPhase : uint8_t {
  kStoredVerificationNotes = 0,
  kStoredVerificationWrapPairs = 1,
  kStoredVerificationDisplayNotes = 2,
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
}

TRACK_COLD_MEM void Track::queueDeferredStoredMidiVerification() {
#if !defined(SESSION_CAPTURE)
  // Every emission in the drain compiles out without SESSION_CAPTURE; queueing would only buy a
  // flatten, a wrap-pair scan and a reconstruct that produce nothing.
  return;
#else
  resetDeferredStoredMidiVerification();
  const Loop& loop = getActiveLoop();
  if (loop.loopLengthTicks == 0 || !loop.hasCommittedPasses()) {
    return;
  }
  if (!sStoredMidiVerificationArmed) {
    return;
  }
  deferredStoredVerificationPending = true;
  sStoredMidiVerificationArmed = false;
  logStoredMidiVerificationArmState(false);
#endif
}

TRACK_COLD_MEM void Track::processDeferredStoredMidiVerification(size_t maxEventsPerSlice) {
#if !defined(SESSION_CAPTURE)
  (void)maxEventsPerSlice;
  return;
#else
  if (!deferredStoredVerificationPending) {
    return;
  }

  const Loop& loop = getActiveLoop();
  if (loop.loopLengthTicks == 0 || !loop.hasCommittedPasses()) {
    resetDeferredStoredMidiVerification();
    return;
  }

  const uint32_t heapBeforeMerge = MemoryMonitor::getInternalHeapFreeBytes();
  if (!LoopEventStore::hasInternalHeapHeadroomForNonCriticalWork(heapBeforeMerge)) {
    resetDeferredStoredMidiVerification();
    return;
  }

  // Flatten once, on the first idle slice — never on the stop path.
  if (deferredStoredVerificationEvents.empty() && deferredStoredVerificationCursor == 0 &&
      deferredStoredVerificationPhase == kStoredVerificationNotes) {
    loop.mergeActiveCapturePasses(deferredStoredVerificationEvents);
    if (deferredStoredVerificationEvents.empty()) {
      resetDeferredStoredMidiVerification();
      return;
    }
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
    deferredStoredVerificationPhase = kStoredVerificationWrapPairs;
    return;
  }

  if (deferredStoredVerificationPhase == kStoredVerificationWrapPairs) {
    if (flat.size() <= kMaxWrapPairVerifyEvents) {
      for (size_t i = 0; i < flat.size(); ++i) {
        const MidiEvent& on = flat[i];
        if (!on.isNoteOn()) {
          continue;
        }
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
    }
    deferredStoredVerificationPhase = kStoredVerificationDisplayNotes;
    return;
  }

  const auto reconstructed = NoteUtils::reconstructNotes(flat, loop.loopLengthTicks, false);
  size_t reconLogged = 0;
  for (const NoteUtils::DisplayNote& note : reconstructed) {
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
    SC_DNTE(note.note, note.startTick, displayStart, length, static_cast<int>(reconLogged));
    ++reconLogged;
    if (reconLogged >= kMaxDisplayNoteVerifyLines) {
      break;
    }
  }
  resetDeferredStoredMidiVerification();
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
                      DebugSessionCapture::kUnsampledHeapBytes, visualClass);
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

TRACK_COLD_MEM void Track::processDeferredIdleMaintenance(
    uint32_t nowMs, bool postOverdubDrainWindowActive,
    MidiServiceDrain::DeferredIdleOwner deferredOwner) {
  updateGrowingRecordLoopLength(clockManager.getCurrentTick());
  Loop& activeLoop = getActiveLoop();
  if (deferredOwner == MidiServiceDrain::DeferredIdleOwner::VisualCacheWrapAppend) {
    if (activeLoop.hasDeferredVisualCacheWrapAppend()) {
      activeLoop.applyDeferredVisualCacheWrapAppend();
    }
    return;
  }

  Loop& snapshotLoop = loopWithPendingSnapshotPublish();
  const bool havePublishedSnapshot =
      static_cast<bool>(snapshotLoop.publishedSnapshot());
  const bool visualCacheNeedsFirstPaint =
      snapshotLoop.hasCommittedPasses() && snapshotLoop.visualCacheDirty &&
      snapshotLoop.visualCache.notes.empty();
  // After the first root exists, playback overlays unpublished passes on that
  // window. Successor construction copies the span root per note-bar unit and
  // did not finish after OD1 in session_20260825_232329 (3631 pending=1
  // steps; OD2 BPM 57; OD4 BPM 46). Do not step that work on PLAYING /
  // OVERDUBBING once a root exists. First-record uses the overdub setup:
  // start during RECORDING after the playhead has left bar 1, and start on
  // PLAYING. OVERDUBBING only advances an in-flight candidate.
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
       hasWrapJournal);
  const bool snapshotEligible = snapshotPending && snapshotIdleAllowed;
  if (deferredOwner == MidiServiceDrain::DeferredIdleOwner::Snapshot &&
      snapshotEligible) {
#if defined(SESSION_CAPTURE)
    const uint32_t snapshotIdleStartUs = micros();
#endif
    const uint32_t playheadPhaseTick =
        snapshotLoop.loopLengthTicks == 0
            ? 0
            : tickPhaseInLoop(getEffectivePlaybackTick(clockManager.getCurrentTick()),
                              snapshotLoop.startLoopTick, snapshotLoop.loopLengthTicks);
    snapshotLoop.stepPendingSnapshotPublish(
        1, (!isOverdubbing() && liveRecordPastFirstBar) || hasWrapJournal, playheadPhaseTick,
        liveRecordCompletedTicks);
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
    appendSnapshotIdleCaptureLine(snapshotLoop, snapshotIdleStartUs);
#endif
  }
  // REVT and stored-MIDI verification: emit only when transport is fully idle (58d6c08 reference).
  // Verification is one-shot per boot; it must not drain during PLAYING — the first slice still
  // does a full merge and blocks MIDI for seconds on grown loops (session_20260813_020631).
  if (!isPlaying() && !isRecording() && !isOverdubbing() && !isStoppedRecording()) {
    size_t revtSlice = 64;
    size_t verificationSlice = 64;
    if (StorageManager::hasDeferredSaveWork()) {
      revtSlice = 8;
      verificationSlice = 16;
    }
    processDeferredRecordRevts(revtSlice);
    processDeferredStoredMidiVerification(verificationSlice);
    processDeferredContentResolutionDeviceGate();
  }

  const bool deferredDerivedViewMaintenance =
      (isPlaying() || isOverdubbing() || isStoppedRecording()) && !isRecording() &&
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
    if (allowVisualMaintenanceSlice && isOverdubbing() && !loop.overdubSourceSpanCacheReady()) {
      PassId lastWrapPassId = kInvalidPassId;
      EditPassIdList lastWrapCompanionIds;
      if (loop.collectLastOverdubSessionPass(lastWrapPassId, lastWrapCompanionIds)) {
        for (const OverdubPass& pass : loop.passes.overdubPasses) {
          if (pass.id == lastWrapPassId) {
            LoopContentResolution::publishPreparedOverdubPass(
                pass, loop.playbackRevision, loop.loopLengthTicks, loop.passes.editPasses,
                lastWrapCompanionIds);
            break;
          }
        }
      }
      const uint32_t refreshTick =
          loop.lastTickInLoop != UINT32_MAX ? loop.lastTickInLoop : loop.playheadPhaseTick;
      loop.rebuildOverdubSourceView(refreshTick, "idle");
    } else if (allowVisualMaintenanceSlice && loop.hasCommittedPasses() && loop.visualCacheDirty) {
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

  emitOverdubStopDebtProbeIfArmed(activeLoop, idleVisualSliceRan, idleVisualBarsPerSlice,
                                  idleVisualDirtyBefore, idleVisualDirtyAfter);

  if (deferredOwner == MidiServiceDrain::DeferredIdleOwner::VisualCache &&
      !isPlaying() && !isRecording() && !isOverdubbing() && !isStoppedRecording()) {
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
      if (!loop.isPassesMaterializedStoreFresh() && !deferHeavyDerivedView && !avoidFullVisual) {
        loop.materializeEditViewFromPasses();
      }
        if (loop.visualCacheDirty) {
        // One idle slice per call. Do not ensureVisualCacheBuilt here — short
        // loops use the same resumable owner as long loops (Slice 4c).
        const uint8_t barsPerSlice = StorageManager::hasDeferredSaveWork() ? 2 : 4;
        // 161804 / 165107: budget=0 plus wrap-append on the bar-0 edge paid
        // idle_append 682–906 ms in one STOPPED turn. PLAYING already budgets
        // gather and defers wrap-append; STOPPED uses the same pair. Prewarm
        // stays on a later clean-cache turn (160519 / Stage B).
        constexpr size_t kStoppedFallbackGatherEventBudget = 192;
        loop.rebuildVisualCacheIdleSlice(barsPerSlice, 0, UINT32_MAX, false, false,
                                         kStoppedFallbackGatherEventBudget);
      }
      bool ranDeferredWrapAppend = false;
      if (!loop.visualCacheDirty && loop.hasDeferredVisualCacheWrapAppend()) {
        loop.applyDeferredVisualCacheWrapAppend();
        ranDeferredWrapAppend = true;
      }
      // After boot_commit and on a later clean-cache turn only. Same-turn
      // prewarm after slice_clean hung 160519; 162928 opened notes=0 without
      // any caller.
      if (visualCacheCleanAtTurnStart && !loop.visualCacheDirty && !ranDeferredWrapAppend &&
          !StorageManager::isBootTitleLoadDrain()) {
        loop.prewarmOverdubSourceSpanCache();
      }
      maybeLogStoredNoteCount();
      maybeQueueContentResolutionDeviceGate();
    }
  }

  if (!DeferredValidatePolicy::shouldRunDeferredFullValidate(
          deferredFullMidiValidate, isPlaying(), isRecording(), isOverdubbing(),
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
  deferredRecordRevtsPending = false;
  deferredRecordRevtChunkScan = false;
  deferredRecordRevtCursor = 0;
  deferredRecordRevtEvents.clear();
  deferredRecordRevtChunkRefs.clear();
  deferredRecordRevtChunkCursor = 0;
  deferredRecordRevtChunkEvents.clear();
  deferredRecordRevtChunkEventCursor = 0;
}

void Track::queueDeferredRecordRevts() {
  const Loop& loop = getActiveLoop();
  if (!loop.hasCommittedPasses()) {
    resetDeferredRecordRevts();
    return;
  }

  resetDeferredRecordRevts();
  deferredRecordRevtsPending = true;

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
    }
  }
}

void Track::processDeferredRecordRevts(size_t maxEventsPerSlice) {
  if (!deferredRecordRevtsPending) {
    return;
  }

  const Loop& loop = getActiveLoop();
  if (!loop.hasCommittedPasses()) {
    resetDeferredRecordRevts();
    return;
  }

  if (deferredRecordRevtChunkScan) {
    size_t queued = 0;
    while (queued < maxEventsPerSlice) {
      if (deferredRecordRevtChunkEventCursor >= deferredRecordRevtChunkEvents.size()) {
        deferredRecordRevtChunkEvents.clear();
        deferredRecordRevtChunkEventCursor = 0;
        if (deferredRecordRevtChunkCursor >= deferredRecordRevtChunkRefs.size()) {
          break;
        }
        const uint16_t chunkId = deferredRecordRevtChunkRefs[deferredRecordRevtChunkCursor++];
        LoopEventStore::appendChunkRefEvent(chunkId, deferredRecordRevtChunkEvents);
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
      logger.log(CAT_TRACK, LOG_DEBUG, "Queued REVT note-ons (deferred): %d",
                 static_cast<int>(queued));
      resetDeferredRecordRevts();
    }
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
