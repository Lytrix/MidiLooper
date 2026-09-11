//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/InvestigationCaptureLog.h"

#if defined(SESSION_CAPTURE) && defined(ARDUINO) && \
    (SESSION_CAPTURE_INVESTIGATION_DIAG || SESSION_CAPTURE_SNAPSHOT_IDLE)

#include "Loop.h"
#include "LoopPasses.h"
#include "PendingNoteChange.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/InvestigationAllocProbe.h"

#include <Arduino.h>
#include <cstdio>

#if defined(__IMXRT1062__)
#define IC_LOG_MEM __attribute__((noinline, section(".flashmem")))
#define IC_LOG_FMT(literal) PSTR(literal)
#else
#define IC_LOG_MEM
#define IC_LOG_FMT(literal) literal
#endif

namespace InvestigationCaptureLog {
namespace {

constexpr size_t kCaptureLineBytes = 192;
constexpr size_t kCaptureLineWideBytes = 256;

IC_LOG_MEM void queueCaptureLine(const char* line) {
  DebugSessionCapture::appendCaptureTextLine(line);
}

#if SESSION_CAPTURE_INVESTIGATION_DIAG
uint32_t g_lengthenPathCount = 0;
uint32_t g_shortenPathCount = 0;
uint32_t g_noOpPathCount = 0;
uint32_t g_reconstructUs = 0;
uint32_t g_reconInternalHits = 0;
uint32_t g_reconSpillHits = 0;
uint32_t g_reconSpillBytes = 0;
uint32_t g_reconClockPulses = 0;
uint32_t g_reconInternalClockDispatches = 0;
uint32_t g_workingSetExternal = 0;
uint32_t g_reconFilterUs = 0;
uint32_t g_reconSpanUs = 0;
uint32_t g_reconProjectUs = 0;
uint32_t g_reconIntervalTimerEntries = 0;
uint32_t g_reconIntervalTimerUs = 0;

uint32_t g_lengthProbeClockPulsesStart = 0;
uint32_t g_lengthProbeInternalClockStart = 0;
uint32_t g_lengthProbeSpanStartUs = 0;
uint32_t g_lengthProbeProjectStartUs = 0;
uint32_t g_lengthProbeIntervalTimerEntriesStart = 0;
uint32_t g_lengthProbeIntervalTimerUsStart = 0;
#endif

}  // namespace

#if SESSION_CAPTURE_SNAPSHOT_IDLE
namespace {
uint32_t g_pendingSnapshotIdleStartUs = 0;
bool g_snapshotIdleReservePending = false;
bool g_snapshotIdleForcedYieldArmed = false;

IC_LOG_MEM void emitSnapshotIdleReserveLine(uint32_t enterUs, uint32_t startUs,
                                             uint32_t reserveUs) {
  char reserveLine[kCaptureLineBytes];
  snprintf(reserveLine, sizeof(reserveLine),
           IC_LOG_FMT("#CAP,%lu,DIAG,snapshot_idle,reserve,start=%lu,enter=%lu,reserve_us=%lu"),
           static_cast<unsigned long>(enterUs), static_cast<unsigned long>(startUs),
           static_cast<unsigned long>(enterUs), static_cast<unsigned long>(reserveUs));
  queueCaptureLine(reserveLine);
}
}  // namespace

IC_LOG_MEM void appendSnapshotIdleReserveOnMidiEnter(uint32_t enterUs) {
  if (!g_snapshotIdleReservePending) {
    return;
  }
  const uint32_t startUs = g_pendingSnapshotIdleStartUs;
  g_snapshotIdleReservePending = false;
  emitSnapshotIdleReserveLine(enterUs, startUs, enterUs - startUs);
}

IC_LOG_MEM void appendSnapshotIdleLines(Loop& snapshotLoop, uint32_t snapshotIdleStartUs) {
  const unsigned long nowUs = static_cast<unsigned long>(micros());
#if defined(SESSION_CAPTURE)
  const uint32_t emitStartUs = micros();
#endif
  const ImmutableLoopSnapshotRef publishedSnapshot = snapshotLoop.publishedSnapshot();
  char snapshotLine[kCaptureLineBytes];
  snprintf(snapshotLine, sizeof(snapshotLine),
           IC_LOG_FMT("#CAP,%lu,DIAG,snapshot_idle,us=%lu,pending=%u,root=%p,root_rev=%lu,"
                      "playback_rev=%lu,queued=%lu,applied=%lu,construction=%lu,exit=%u,"
                      "inflight=%u,journals=%lu,fragment=%u/%u,parent_rev=%lu"),
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
  queueCaptureLine(snapshotLine);
#if defined(SESSION_CAPTURE)
  snapshotLoop.addSnapshotIdleEmitUs(micros() - emitStartUs);
#endif

  char childLine[kCaptureLineBytes];
  snprintf(childLine, sizeof(childLine),
           IC_LOG_FMT("#CAP,%lu,DIAG,snapshot_idle,child,start=%lu,pass_change=%lu,step=%lu,"
                      "finish=%lu,publish=%lu,probe=%lu,enqueue=%lu,discard=%lu,emit=%lu"),
           nowUs, static_cast<unsigned long>(snapshotLoop.lastSnapshotIdleStartUs()),
           static_cast<unsigned long>(snapshotLoop.lastSnapshotIdlePassChangeUs()),
           static_cast<unsigned long>(snapshotLoop.lastSnapshotIdleStepUs()),
           static_cast<unsigned long>(snapshotLoop.lastSnapshotIdleFinishUs()),
           static_cast<unsigned long>(snapshotLoop.lastSnapshotIdlePublishUs()),
           static_cast<unsigned long>(snapshotLoop.lastSnapshotIdleProbeUs()),
           static_cast<unsigned long>(snapshotLoop.lastSnapshotIdleEnqueueUs()),
           static_cast<unsigned long>(snapshotLoop.lastSnapshotIdleDiscardUs()),
           static_cast<unsigned long>(snapshotLoop.lastSnapshotIdleEmitUs()));
  queueCaptureLine(childLine);
  snprintf(childLine, sizeof(childLine),
           IC_LOG_FMT("#CAP,%lu,DIAG,snapshot_idle,inner,refill=%lu,event_scan=%lu,"
                      "note_lookup=%lu,change_build=%lu,construct=%lu,open=%u"),
           nowUs, static_cast<unsigned long>(snapshotLoop.lastSnapshotIdleRefillUs()),
           static_cast<unsigned long>(snapshotLoop.lastSnapshotIdleEventScanUs()),
           static_cast<unsigned long>(snapshotLoop.lastSnapshotIdleNoteLookupUs()),
           static_cast<unsigned long>(snapshotLoop.lastSnapshotIdleChangeBuildUs()),
           static_cast<unsigned long>(snapshotLoop.lastSnapshotIdleConstructUs()),
           snapshotLoop.liveRecordOpenNoteCount());
  queueCaptureLine(childLine);

  if (g_snapshotIdleReservePending) {
    emitSnapshotIdleReserveLine(nowUs, g_pendingSnapshotIdleStartUs, 0);
  }
  g_pendingSnapshotIdleStartUs = snapshotIdleStartUs;
  g_snapshotIdleReservePending = true;
}

IC_LOG_MEM void armSnapshotIdleForcedYield() {
  g_snapshotIdleForcedYieldArmed = true;
}

IC_LOG_MEM bool consumeSnapshotIdleForcedYield() {
  if (!g_snapshotIdleForcedYieldArmed) {
    return false;
  }
  g_snapshotIdleForcedYieldArmed = false;
  return true;
}

IC_LOG_MEM void logOverdubStopHold(unsigned notes, uint32_t rootRevision,
                                   uint32_t playbackRevision) {
  char line[kCaptureLineBytes];
  snprintf(line, sizeof(line),
           IC_LOG_FMT("#CAP,%lu,DIAG,lcr,src,why=stop,from=hold,notes=%u,root_rev=%lu,play_rev=%lu"),
           static_cast<unsigned long>(micros()), notes,
           static_cast<unsigned long>(rootRevision),
           static_cast<unsigned long>(playbackRevision));
  queueCaptureLine(line);
}

IC_LOG_MEM void logSessionExitPostStopPublish(uint32_t oldRoot, uint32_t newRoot,
                                              uint32_t playbackRevision) {
  char line[kCaptureLineBytes];
  snprintf(line, sizeof(line),
           IC_LOG_FMT("#CAP,%lu,DIAG,snapshot_idle,post_stop,old_root=%lu,new_root=%lu,play_rev=%lu"),
           static_cast<unsigned long>(micros()), static_cast<unsigned long>(oldRoot),
           static_cast<unsigned long>(newRoot), static_cast<unsigned long>(playbackRevision));
  queueCaptureLine(line);
}

IC_LOG_MEM void appendSnapshotIdleYieldLine(uint32_t remainingUs, uint32_t marginUs,
                                            uint32_t admittedUs, uint32_t costUs,
                                            bool timingCritical, bool pending,
                                            uint32_t rootRevision) {
  char yieldLine[kCaptureLineBytes];
  snprintf(yieldLine, sizeof(yieldLine),
           IC_LOG_FMT("#CAP,%lu,DIAG,snapshot_idle,yield,remaining=%lu,margin=%lu,admitted=%lu,"
                      "cost=%lu,crit=%u,pending=%u,root_rev=%lu"),
           static_cast<unsigned long>(micros()), static_cast<unsigned long>(remainingUs),
           static_cast<unsigned long>(marginUs), static_cast<unsigned long>(admittedUs),
           static_cast<unsigned long>(costUs), timingCritical ? 1u : 0u, pending ? 1u : 0u,
           static_cast<unsigned long>(rootRevision));
  queueCaptureLine(yieldLine);
}
#endif

#if SESSION_CAPTURE_INVESTIGATION_DIAG
IC_LOG_MEM void logVisualCacheWindow(unsigned long nowUs, uint32_t windowUs,
                                     uint32_t reconstructUs, unsigned flatEventCount,
                                     unsigned sliceNoteCount) {
  char line[kCaptureLineBytes];
  snprintf(line, sizeof(line),
           IC_LOG_FMT("#CAP,%lu,DIAG,lcr,vch,win=%lu,proj=%lu,tot=%lu,ev=%u,notes=%u"), nowUs,
           static_cast<unsigned long>(windowUs), static_cast<unsigned long>(reconstructUs),
           static_cast<unsigned long>(windowUs + reconstructUs), flatEventCount, sliceNoteCount);
  queueCaptureLine(line);
}

IC_LOG_MEM void logVisualCacheGatherProgress(unsigned long nowUs, unsigned eventsAppended,
                                             unsigned totalEvents, unsigned traversalComplete,
                                             unsigned resumed) {
  char line[kCaptureLineBytes];
  snprintf(line, sizeof(line),
           IC_LOG_FMT("#CAP,%lu,DIAG,lcr,vch,gather_progress,ev=%u,total=%u,done=%u,resume=%u"),
           nowUs, eventsAppended, totalEvents, traversalComplete, resumed);
  queueCaptureLine(line);
}

IC_LOG_MEM void logVisualCacheGatherSort(unsigned long nowUs, uint32_t sortUs,
                                         unsigned eventCount) {
  char line[kCaptureLineBytes];
  snprintf(line, sizeof(line),
           IC_LOG_FMT("#CAP,%lu,DIAG,lcr,vch,gather_sort,us=%lu,ev=%u"), nowUs,
           static_cast<unsigned long>(sortUs), eventCount);
  queueCaptureLine(line);
}

IC_LOG_MEM void logVisualCacheGatherEdit(unsigned long nowUs, uint32_t editUs, unsigned rowCount,
                                         unsigned eventCount) {
  char line[kCaptureLineBytes];
  snprintf(line, sizeof(line),
           IC_LOG_FMT("#CAP,%lu,DIAG,lcr,vch,gather_edit,us=%lu,rows=%u,ev=%u"), nowUs,
           static_cast<unsigned long>(editUs), rowCount, eventCount);
  queueCaptureLine(line);
}

IC_LOG_MEM void maybeLogCommittedEventRangeDiag(const CommittedEventRangeDiagTiming& timing,
                                                const LoopPasses& passes,
                                                uint32_t reportThresholdUs) {
  if (timing.rangeUs < reportThresholdUs) {
    return;
  }
  char line[kCaptureLineBytes];
  snprintf(line, sizeof(line),
           IC_LOG_FMT("#CAP,%lu,DIAG,cer_range,us=%lu,collect=%lu,append=%lu,sort=%lu,edit=%lu,"
                      "lists=%u,rows=%u,evout=%u"),
           static_cast<unsigned long>(micros()), static_cast<unsigned long>(timing.rangeUs),
           static_cast<unsigned long>(timing.collectUs), static_cast<unsigned long>(timing.appendUs),
           static_cast<unsigned long>(timing.sortUs), static_cast<unsigned long>(timing.editUs),
           timing.listCount, timing.editRowCount, timing.outCount);
  queueCaptureLine(line);

  uint32_t activeRowsTotal = 0;
  uint32_t disabledRows = 0;
  uint32_t companionRows = 0;
  uint32_t noteEditRows = 0;
  uint32_t deleteRows = 0;
  uint32_t createRows = 0;
  uint32_t updateLengthRows = 0;
  uint32_t updatePitchRows = 0;
  uint32_t updateVelocityRows = 0;
  uint32_t updateRangeRows = 0;
  for (const EditPass& editPass : passes.editPasses) {
    if (editPass.passType != EditPassType::Note) {
      continue;
    }
    if (editPass.state != EditPassState::Active) {
      ++disabledRows;
      continue;
    }
    ++activeRowsTotal;
    if (editPass.editPassIndex == kOverdubCompanionEditPassIndex) {
      ++companionRows;
    } else {
      ++noteEditRows;
    }
    switch (editPass.actionType) {
      case EditActionType::Delete:
        ++deleteRows;
        break;
      case EditActionType::Create:
        ++createRows;
        break;
      case EditActionType::Update:
        switch (editPass.propertyType) {
          case EditPropertyType::Length:
            ++updateLengthRows;
            break;
          case EditPropertyType::Pitch:
            ++updatePitchRows;
            break;
          case EditPropertyType::Velocity:
            ++updateVelocityRows;
            break;
          case EditPropertyType::NoteRange:
            ++updateRangeRows;
            break;
          default:
            break;
        }
        break;
    }
  }
  char rowsLine[kCaptureLineWideBytes];
  snprintf(rowsLine, sizeof(rowsLine),
           IC_LOG_FMT("#CAP,%lu,DIAG,cer_rows,active=%u,disabled=%u,comp=%u,ne=%u,del=%u,crt=%u,"
                      "ulen=%u,upit=%u,uvel=%u,urng=%u"),
           static_cast<unsigned long>(micros()), activeRowsTotal, disabledRows, companionRows,
           noteEditRows, deleteRows, createRows, updateLengthRows, updatePitchRows,
           updateVelocityRows, updateRangeRows);
  queueCaptureLine(rowsLine);
}

IC_LOG_MEM void maybeLogCaptureTailDiag(const CaptureTailDiagTiming& timing,
                                        uint32_t reportThresholdUs) {
  if (timing.tailUs < reportThresholdUs) {
    return;
  }
  char line[kCaptureLineWideBytes];
  snprintf(line, sizeof(line),
           IC_LOG_FMT("#CAP,%lu,DIAG,cer_cap,us=%lu,committed=%lu,sort=%lu,copy=%lu,filter=%lu,"
                      "merge=%lu,capflat=%u,capwin=%u,evout=%u"),
           static_cast<unsigned long>(micros()), static_cast<unsigned long>(timing.tailUs),
           static_cast<unsigned long>(timing.committedUs), static_cast<unsigned long>(timing.sortUs),
           static_cast<unsigned long>(timing.copyUs), static_cast<unsigned long>(timing.filterUs),
           static_cast<unsigned long>(timing.mergeUs), timing.captureFlatCount,
           timing.captureWindowCount, timing.outCount);
  queueCaptureLine(line);
}

IC_LOG_MEM void logSealCompanionRow(unsigned editPassId, unsigned targetNoteId, bool hideKind,
                                    unsigned endTick) {
  char line[kCaptureLineBytes];
  snprintf(line, sizeof(line),
           IC_LOG_FMT("#CAP,%lu,DIAG,seal_companion,id=%u,target=%u,kind=%s,end=%u"),
           static_cast<unsigned long>(micros()), editPassId, targetNoteId,
           hideKind ? "hide" : "shorten", endTick);
  queueCaptureLine(line);
}

IC_LOG_MEM void logSealCompanionBatch(unsigned companionRowsToSeal, unsigned sealedCount,
                                      uint32_t durationUs) {
  char line[kCaptureLineBytes];
  snprintf(line, sizeof(line),
           IC_LOG_FMT("#CAP,%lu,DIAG,seal_companion_batch,rows=%u,sealed=%u,us=%lu"),
           static_cast<unsigned long>(micros()), companionRowsToSeal, sealedCount,
           static_cast<unsigned long>(durationUs));
  queueCaptureLine(line);
}

IC_LOG_MEM void resetLengthPathCounters() {
  g_lengthenPathCount = 0;
  g_shortenPathCount = 0;
  g_noOpPathCount = 0;
  g_reconstructUs = 0;
  g_reconInternalHits = 0;
  g_reconSpillHits = 0;
  g_reconSpillBytes = 0;
  g_reconClockPulses = 0;
  g_reconInternalClockDispatches = 0;
  g_reconFilterUs = 0;
  g_reconSpanUs = 0;
  g_reconProjectUs = 0;
  g_reconIntervalTimerEntries = 0;
  g_reconIntervalTimerUs = 0;
}

IC_LOG_MEM void recordLengthPathNoOp() { ++g_noOpPathCount; }

IC_LOG_MEM void recordLengthPathShorten() { ++g_shortenPathCount; }

IC_LOG_MEM void recordLengthPathLengthen() { ++g_lengthenPathCount; }

IC_LOG_MEM void recordReconFilterUs(uint32_t durationUs) { g_reconFilterUs += durationUs; }

IC_LOG_MEM void recordReconInternalHit() { ++g_reconInternalHits; }

IC_LOG_MEM void recordReconSpill(uint32_t byteEstimate) {
  ++g_reconSpillHits;
  g_reconSpillBytes += byteEstimate;
}

IC_LOG_MEM void beginLengthPathLengthenProbe() {
  g_lengthProbeClockPulsesStart = InvestigationAllocProbe::clockPulseEntries();
  g_lengthProbeInternalClockStart = InvestigationAllocProbe::internalClockDispatches();
  g_lengthProbeSpanStartUs = InvestigationAllocProbe::spanBuildUs();
  g_lengthProbeProjectStartUs = InvestigationAllocProbe::projectUs();
  g_lengthProbeIntervalTimerEntriesStart = InvestigationAllocProbe::intervalTimerEntries();
  g_lengthProbeIntervalTimerUsStart = InvestigationAllocProbe::intervalTimerUs();
}

IC_LOG_MEM void endLengthPathLengthenProbe(uint32_t reconstructDurationUs) {
  g_reconstructUs += reconstructDurationUs;
  g_reconClockPulses +=
      InvestigationAllocProbe::clockPulseEntries() - g_lengthProbeClockPulsesStart;
  g_reconInternalClockDispatches +=
      InvestigationAllocProbe::internalClockDispatches() - g_lengthProbeInternalClockStart;
  g_reconSpanUs += InvestigationAllocProbe::spanBuildUs() - g_lengthProbeSpanStartUs;
  g_reconProjectUs += InvestigationAllocProbe::projectUs() - g_lengthProbeProjectStartUs;
  g_reconIntervalTimerEntries +=
      InvestigationAllocProbe::intervalTimerEntries() - g_lengthProbeIntervalTimerEntriesStart;
  g_reconIntervalTimerUs +=
      InvestigationAllocProbe::intervalTimerUs() - g_lengthProbeIntervalTimerUsStart;
}

IC_LOG_MEM void setCerEditWorkingSetExternal(bool external) {
  g_workingSetExternal = external ? 1u : 0u;
}

IC_LOG_MEM void maybeLogCerEditSequence(const CerEditSequenceDiagTiming& timing,
                                        uint32_t reportThresholdUs) {
  if (timing.sequenceUs < reportThresholdUs) {
    return;
  }
  char line[kCaptureLineBytes];
  snprintf(line, sizeof(line),
           IC_LOG_FMT("#CAP,%lu,DIAG,cer_edit,us=%lu,del=%lu,ulen=%lu,recon=%lu,ndel=%u,nlen=%u,"
                      "lgth=%u,shrt=%u,noop=%u,ev=%u"),
           static_cast<unsigned long>(micros()), static_cast<unsigned long>(timing.sequenceUs),
           static_cast<unsigned long>(timing.deleteUs), static_cast<unsigned long>(timing.lengthUs),
           static_cast<unsigned long>(g_reconstructUs), timing.deletesExecuted,
           timing.lengthsExecuted, g_lengthenPathCount, g_shortenPathCount, g_noOpPathCount,
           timing.eventCount);
  queueCaptureLine(line);
  snprintf(line, sizeof(line),
           IC_LOG_FMT("#CAP,%lu,DIAG,cer_heap,recon=%lu,ihit=%lu,spill=%lu,sbytes=%lu,clk=%lu,"
                      "iclk=%lu,ext=%u"),
           static_cast<unsigned long>(micros()), static_cast<unsigned long>(g_reconstructUs),
           static_cast<unsigned long>(g_reconInternalHits),
           static_cast<unsigned long>(g_reconSpillHits),
           static_cast<unsigned long>(g_reconSpillBytes),
           static_cast<unsigned long>(g_reconClockPulses),
           static_cast<unsigned long>(g_reconInternalClockDispatches), g_workingSetExternal);
  queueCaptureLine(line);
  snprintf(line, sizeof(line),
           IC_LOG_FMT("#CAP,%lu,DIAG,cer_recon,recon=%lu,filt=%lu,span=%lu,proj=%lu,itick=%lu,"
                      "itickus=%lu"),
           static_cast<unsigned long>(micros()), static_cast<unsigned long>(g_reconstructUs),
           static_cast<unsigned long>(g_reconFilterUs), static_cast<unsigned long>(g_reconSpanUs),
           static_cast<unsigned long>(g_reconProjectUs),
           static_cast<unsigned long>(g_reconIntervalTimerEntries),
           static_cast<unsigned long>(g_reconIntervalTimerUs));
  queueCaptureLine(line);
}
IC_LOG_MEM void maybeLogCerCopySequence(uint32_t wrapUs, uint32_t copyInUs, uint32_t coreUs,
                                        uint32_t copyOutUs, uint32_t eventCount,
                                        uint32_t reportThresholdUs) {
  if (wrapUs < reportThresholdUs) {
    return;
  }
  char line[kCaptureLineBytes];
  snprintf(line, sizeof(line),
           IC_LOG_FMT("#CAP,%lu,DIAG,cer_copy,us=%lu,copyin=%lu,core=%lu,copyout=%lu,ev=%u"),
           static_cast<unsigned long>(micros()), static_cast<unsigned long>(wrapUs),
           static_cast<unsigned long>(copyInUs), static_cast<unsigned long>(coreUs),
           static_cast<unsigned long>(copyOutUs), eventCount);
  queueCaptureLine(line);
}
#endif  // SESSION_CAPTURE_INVESTIGATION_DIAG

}  // namespace InvestigationCaptureLog

#endif
