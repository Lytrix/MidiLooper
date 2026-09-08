//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file InvestigationCaptureLog.h
 * @brief FLASHMEM capture emitters for investigation-diag and snapshot_idle builds.
 *
 * Hot paths call these noinline helpers with scalar arguments only. snprintf, format
 * strings (PSTR), and counter storage live in InvestigationCaptureLog.cpp so they do
 * not expand ITCM object files or DTCM .rodata/.bss on teensy41-capture-serial paths.
 */
#pragma once

#include <cstdint>

class Loop;
struct LoopPasses;

#ifndef SESSION_CAPTURE_INVESTIGATION_DIAG
#define SESSION_CAPTURE_INVESTIGATION_DIAG 0
#endif
#ifndef SESSION_CAPTURE_SNAPSHOT_IDLE
#define SESSION_CAPTURE_SNAPSHOT_IDLE 0
#endif

#if defined(SESSION_CAPTURE) && defined(ARDUINO) && \
    (SESSION_CAPTURE_INVESTIGATION_DIAG || SESSION_CAPTURE_SNAPSHOT_IDLE)

namespace InvestigationCaptureLog {

struct CommittedEventRangeDiagTiming {
  uint32_t rangeUs = 0;
  uint32_t collectUs = 0;
  uint32_t appendUs = 0;
  uint32_t sortUs = 0;
  uint32_t editUs = 0;
  uint32_t listCount = 0;
  uint32_t editRowCount = 0;
  uint32_t outCount = 0;
};

struct CaptureTailDiagTiming {
  uint32_t tailUs = 0;
  uint32_t committedUs = 0;
  uint32_t sortUs = 0;
  uint32_t copyUs = 0;
  uint32_t filterUs = 0;
  uint32_t mergeUs = 0;
  uint32_t captureFlatCount = 0;
  uint32_t captureWindowCount = 0;
  uint32_t outCount = 0;
};

struct CerEditSequenceDiagTiming {
  uint32_t sequenceUs = 0;
  uint32_t deleteUs = 0;
  uint32_t lengthUs = 0;
  uint32_t deletesExecuted = 0;
  uint32_t lengthsExecuted = 0;
  uint32_t eventCount = 0;
};

#if SESSION_CAPTURE_SNAPSHOT_IDLE
void appendSnapshotIdleLines(Loop& snapshotLoop, uint32_t snapshotIdleStartUs);

/** Pair a pending snapshot-idle start to this `handleMidiInput()` enter. FLASHMEM. */
void appendSnapshotIdleReserveOnMidiEnter(uint32_t enterUs);

/** Arm one constrained-reserve Snapshot turn. Capture-only one-shot. */
void armSnapshotIdleForcedYield();

/** True once after `armSnapshotIdleForcedYield`. Clears the one-shot. */
bool consumeSnapshotIdleForcedYield();

void appendSnapshotIdleYieldLine(uint32_t remainingUs, uint32_t marginUs, uint32_t admittedUs,
                                 uint32_t costUs, bool timingCritical, bool pending,
                                 uint32_t rootRevision);

void logOverdubStopHold(unsigned notes, uint32_t rootRevision, uint32_t playbackRevision);

void logSessionExitPostStopPublish(uint32_t oldRoot, uint32_t newRoot, uint32_t playbackRevision);
#endif

#if SESSION_CAPTURE_INVESTIGATION_DIAG
void logVisualCacheWindow(unsigned long nowUs, uint32_t windowUs, uint32_t reconstructUs,
                          unsigned flatEventCount, unsigned sliceNoteCount);

void logVisualCacheGatherProgress(unsigned long nowUs, unsigned eventsAppended,
                                  unsigned totalEvents, unsigned traversalComplete,
                                  unsigned resumed);

void logVisualCacheGatherSort(unsigned long nowUs, uint32_t sortUs, unsigned eventCount);

void logVisualCacheGatherEdit(unsigned long nowUs, uint32_t editUs, unsigned rowCount,
                              unsigned eventCount);

void maybeLogCommittedEventRangeDiag(const CommittedEventRangeDiagTiming& timing,
                                     const LoopPasses& passes, uint32_t reportThresholdUs);

void maybeLogCaptureTailDiag(const CaptureTailDiagTiming& timing, uint32_t reportThresholdUs);

void logSealCompanionRow(unsigned editPassId, unsigned targetNoteId, bool hideKind,
                         unsigned endTick);

void logSealCompanionBatch(unsigned companionRowsToSeal, unsigned sealedCount,
                           uint32_t durationUs);

void resetLengthPathCounters();
void recordLengthPathNoOp();
void recordLengthPathShorten();
void recordLengthPathLengthen();
void recordReconFilterUs(uint32_t durationUs);
void recordReconInternalHit();
void recordReconSpill(uint32_t byteEstimate);
void beginLengthPathLengthenProbe();
void endLengthPathLengthenProbe(uint32_t reconstructDurationUs);
void setCerEditWorkingSetExternal(bool external);
void maybeLogCerEditSequence(const CerEditSequenceDiagTiming& timing,
                             uint32_t reportThresholdUs);

void maybeLogCerCopySequence(uint32_t wrapUs, uint32_t copyInUs, uint32_t coreUs,
                             uint32_t copyOutUs, uint32_t eventCount, uint32_t reportThresholdUs);
#endif

}  // namespace InvestigationCaptureLog

#endif
