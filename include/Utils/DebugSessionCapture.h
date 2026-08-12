//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file DebugSessionCapture.h
 * @brief Session fixture recording for the instrumented capture build (Bucket 1 S1).
 *
 * Emits machine-parseable `#CAP,...` lines on USB serial so a hardware session can be
 * replayed offline (native harness) and turned into regression fixtures.
 *
 * Only active when built with -D SESSION_CAPTURE (env:teensy41-capture-serial).
 * In all other builds every SC_* macro compiles to nothing.
 *
 * Capture producers use FLASHMEM append paths into a PSRAM ring; flushCaptureBuffer runs from
 * RAM each main-loop iteration and prints to USB serial (teensy41-capture-serial RAM budget).
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "Utils/DiagnosticsTypes.h"

#ifdef SESSION_CAPTURE

#include "MidiEvent.h"

#if defined(__IMXRT1062__)
#include <Arduino.h>
#define SC_MEM_ATTR FLASHMEM
#else
#define SC_MEM_ATTR
#endif

namespace DebugSessionCapture {

struct PendingRevt {
  uint32_t tick;
  uint8_t ch;
  uint8_t note;
};

/** Allocate the external-memory capture ring (no-op when PSRAM unavailable). */
SC_MEM_ATTR void initCaptureBuffer();

/** True for ~5s after initCaptureBuffer (legacy; hot-path CAP lines always prefer the ring). */
bool captureBootGraceActive();

/** Reset the boot-grace timer (call after long setup, before first display paints). */
void restartCaptureBootGrace();

/** Queue a deferred serial line (#DBG, PERF, …) for flushCaptureBuffer. */
SC_MEM_ATTR void appendCaptureTextLine(const char* line);

/**
 * Drain up to @p maxRecords from the PSRAM ring to USB serial.
 * Runs from RAM (not FLASHMEM) — safe for Serial and ring access from main loop.
 */
void flushCaptureBuffer(size_t maxRecords = 64);

SC_MEM_ATTR void sessionHeader();
SC_MEM_ATTR void midiIn(char src, uint8_t type, uint8_t ch, uint8_t d1, uint8_t d2);
SC_MEM_ATTR void midiOut(uint8_t type, uint8_t ch, uint8_t d1, uint8_t d2);
SC_MEM_ATTR void midiOutEvent(const MidiEvent& e);
SC_MEM_ATTR void ledOut(bool on, uint8_t note, uint8_t vel);
SC_MEM_ATTR void gesture(uint8_t channel0, uint8_t note, int pressType);
SC_MEM_ATTR void stateTransition(const char* component, const char* from, const char* to);
SC_MEM_ATTR void bpm(float raw, float smoothed);
SC_MEM_ATTR void clockSource(const char* from, const char* to);
SC_MEM_ATTR void recStart(uint8_t slot, uint32_t tick);
SC_MEM_ATTR void recStop(const char* kind, uint8_t slot, uint32_t tick, uint32_t startTick,
                         uint32_t rawLength, uint32_t finalLength, bool align);
SC_MEM_ATTR void recStopStage(const char* stage, uint32_t elapsedUs, uint32_t durationUs,
                              uint32_t heapBefore, uint32_t heapAfter, size_t eventCount,
                              size_t chunkRefCount, const char* outcome);
SC_MEM_ATTR void overdubStartStage(const char* stage, uint32_t durationUs, uint32_t heapBefore,
                                   uint32_t heapAfter, const char* outcome);
SC_MEM_ATTR void overdubStopStage(const char* stage, uint32_t elapsedUs, uint32_t durationUs,
                                  uint32_t heapBefore, uint32_t heapAfter, size_t eventCount,
                                  size_t chunkRefCount, const char* outcome);
SC_MEM_ATTR void architectureCounter(const char* name, uint32_t value);
SC_MEM_ATTR void memoryPressureTransition(const char* transitionLabel, uint32_t heapFreeBytes,
                                          uint16_t chunksFree, uint16_t persistQueueDepth);
SC_MEM_ATTR void captureAppendDeny(const char* reason, uint16_t freeChunks, uint16_t usedChunks,
                                   const char* pressure, uint8_t ch, uint8_t note, uint32_t tick,
                                   uint8_t pendingPass);
SC_MEM_ATTR void passReclaim(uint16_t chunksFreeBefore, uint16_t chunksFreeAfter,
                             uint16_t passesReclaimed, uint16_t chunksReleased,
                             uint32_t durationUs, const char* pressure, uint8_t transport);
SC_MEM_ATTR void architectureTiming(const char* name, uint32_t sumMicros, uint32_t sampleCount);
SC_MEM_ATTR void architectureTimingMax(const char* name, uint32_t maxMicros);
/** S0/S0b timing envelope: DIAG,{msi|midisvc|clk|tracks|usbdev|din|hosttask|hostdrain},<maxUs>,<overCount> (Tier-A). */
SC_MEM_ATTR void runtimeTimingEnvelope(const char* tag, uint32_t maxUs, uint32_t overCount);
/** S0 timing envelope: DIAG,clockrate,<pulsesPerSecond> (Tier-A). */
SC_MEM_ATTR void runtimeTimingClockrate(uint32_t pulsesPerSecond);
SC_MEM_ATTR void persistence(const char* stage, uint32_t durationUs, uint32_t heapBefore,
                             uint32_t heapAfter, const char* outcome);
SC_MEM_ATTR void persistenceDiagnostic(uint16_t freeChunks, uint16_t usedChunks, uint16_t reserve,
                                       uint16_t queueDepth, uint16_t writingChunks,
                                       uint32_t transportBlockCount, uint32_t heapFloorBlockCount,
                                       uint32_t budgetBlockCount, uint32_t sliceDoneCount,
                                       uint32_t peakWriterLatencyUs, uint32_t oldestDirtyAgeMs,
                                       uint32_t maxDeferredBacklog, uint8_t savePending,
                                       uint8_t saveInProgress, uint8_t captureActive);
SC_MEM_ATTR void persistencePoolPressure(uint16_t freeChunks, uint16_t reserve,
                                         uint16_t usedChunks);
SC_MEM_ATTR void persistenceBacklog(uint16_t workQueueDepth, uint16_t writingWorkItems,
                                    uint16_t chunkQueueDepth, uint32_t dirtyAgeMs,
                                    uint32_t estSliceSteps, uint32_t estSdBytes,
                                    uint8_t savePending, uint8_t urgentRequested,
                                    uint32_t transportBlockCount, uint32_t budgetBlockCount,
                                    uint32_t heapFloorBlockCount);
SC_MEM_ATTR void persistenceDrainFailed(const char* reason, uint32_t steps,
                                          uint32_t stuckIterations, uint16_t workQueueDepth,
                                          uint16_t chunkQueueDepth, uint32_t estSliceSteps,
                                          uint32_t estSdBytes);
SC_MEM_ATTR void saveDisplayPhase(const char* phase, uint8_t rotateStep);
SC_MEM_ATTR void loadSaveMode(uint8_t active);
SC_MEM_ATTR void overlayListSelection(uint8_t mode, uint8_t row);
SC_MEM_ATTR void overlayRowConfirm(uint8_t mode, uint8_t row);
SC_MEM_ATTR void queueStoredNoteOn(uint32_t tick, uint8_t ch, uint8_t note);
SC_MEM_ATTR void recStoredNoteOn(uint32_t tick, uint8_t ch, uint8_t note);
SC_MEM_ATTR void storedNoteEvent(char kind, uint32_t tick, uint8_t ch, uint8_t note);
SC_MEM_ATTR void captureCoordinate(uint32_t absTick, uint32_t storageTick, uint32_t projPhase,
                                   uint32_t displayPhase, uint32_t startLoopTick,
                                   int32_t projectionCycleStartTick, uint32_t loopStartTick,
                                   uint8_t ch, uint8_t note);
SC_MEM_ATTR void displaySnapshot(uint8_t slot, const char* trackState, uint32_t loopLen,
                                 size_t sourceEventCount, size_t visualNotes, size_t frameNotes,
                                 size_t bufferEvents, int hasCommittedPasses);
SC_MEM_ATTR void displaySnapshotWindow(uint8_t slot, const char* trackState, uint32_t loopLen,
                                       size_t sourceEventCount, size_t visualNotes, size_t frameNotes,
                                       size_t bufferEvents, int hasCommittedPasses, uint32_t windowStartTick,
                                       uint8_t windowBars, size_t windowNoteCount);
/**
 * Visual cache coverage at a pass-lifecycle or rebuild boundary. Distinguishes committed
 * content loss from cache under-coverage: `events` is the gathered committed event count when
 * the phase performed a gather and -1 otherwise; `firstBar`/`lastBar` bound the bars actually
 * holding cached notes, against `totalBars` for the whole loop.
 */
SC_MEM_ATTR void visualCacheState(const char* phase, int32_t events, uint32_t notes,
                                  uint32_t firstBar, uint32_t lastBar, uint32_t totalBars,
                                  uint32_t dirtyBarsSize, uint32_t dirtyCount, uint8_t dirtyFlag);
SC_MEM_ATTR void displayNoteInfo(uint8_t pitch, uint32_t storageStart, uint32_t displayStart,
                                 uint32_t length, int selectedIdx);
SC_MEM_ATTR void displayFrame(uint32_t frameNotes, uint32_t elapsedUs, uint32_t frameIndex);
SC_MEM_ATTR void playbackFrame(uint8_t slot, uint32_t currentTick, uint32_t tickInLoop,
                               uint32_t prevTickInLoop, int32_t projectionCycleStartTick,
                               uint32_t loopStartTick, uint32_t loopLength, uint16_t indexBefore,
                               uint16_t indexAfter, uint16_t orderSize, uint16_t sent,
                               uint8_t atLoopStart, uint8_t wrapDetected, uint8_t wasStale,
                               uint32_t playbackRevision, uint32_t playbackGeneration);
SC_MEM_ATTR void storedWrapPair(uint32_t onTick, uint32_t offTick, uint8_t ch, uint8_t note);
SC_MEM_ATTR void captureCleanup(const char* phase, const char* kind, uint32_t count);
SC_MEM_ATTR size_t flushPendingRevts(size_t maxLines = 64);
SC_MEM_ATTR void flushAllPendingRevts();
SC_MEM_ATTR void update(uint32_t currentTick, uint32_t ticksPerBar);

/** Append a fixed-size Diagnostics::DiagTraceRecord payload to the PSRAM ring. */
SC_MEM_ATTR bool appendDiagTraceRecord(const void* record, uint16_t recordSize);

/** Emit one DIAGCHK line immediately (boot dump after fault). */
void emitDiagCheckpointLine(const Diagnostics::DiagTraceRecord& record);

}  // namespace DebugSessionCapture

#define SC_SESSION_HEADER()                DebugSessionCapture::sessionHeader()
#define SC_MIDI_IN(src, type, ch, d1, d2)  DebugSessionCapture::midiIn(src, type, ch, d1, d2)
#define SC_MIDI_OUT_EVENT(e)               DebugSessionCapture::midiOutEvent(e)
#define SC_LED_OUT(on, note, vel)          DebugSessionCapture::ledOut(on, note, vel)
#define SC_GESTURE(ch0, note, pressType)   DebugSessionCapture::gesture(ch0, note, pressType)
#define SC_STATE(component, from, to)      DebugSessionCapture::stateTransition(component, from, to)
#define SC_BPM(raw, smoothed)              DebugSessionCapture::bpm(raw, smoothed)
#define SC_CLOCK_SOURCE(from, to)          DebugSessionCapture::clockSource(from, to)
#define SC_REC_START(slot, tick)           DebugSessionCapture::recStart(slot, tick)
#define SC_REC_STOP(kind, slot, tick, start, raw, final, align) \
                                           DebugSessionCapture::recStop(kind, slot, tick, start, raw, final, align)
#define SC_REC_STOP_STAGE(stage, elapsedUs, durationUs, heapBefore, heapAfter, eventCount, chunkRefCount, outcome) \
                                           DebugSessionCapture::recStopStage(stage, elapsedUs, durationUs, heapBefore, heapAfter, eventCount, chunkRefCount, outcome)
#define SC_ODUB_STAGE(stage, durationUs, heapBefore, heapAfter, outcome) \
                                           DebugSessionCapture::overdubStartStage(stage, durationUs, heapBefore, heapAfter, outcome)
#define SC_ODUB_STOP_STAGE(stage, elapsedUs, durationUs, heapBefore, heapAfter, eventCount, chunkRefCount, outcome) \
                                           DebugSessionCapture::overdubStopStage(stage, elapsedUs, durationUs, heapBefore, heapAfter, eventCount, chunkRefCount, outcome)
#define SC_PERSIST(stage, durationUs, heapBefore, heapAfter, outcome) \
                                           DebugSessionCapture::persistence(stage, durationUs, heapBefore, heapAfter, outcome)
#define SC_PERSIST_DIAG(freeChunks, usedChunks, reserve, queueDepth, writingChunks, transportBlk, \
                        heapBlk, budgetBlk, sliceDone, peakLatUs, dirtyAgeMs, maxBacklog, pending, \
                        inProg, captureActive) \
  DebugSessionCapture::persistenceDiagnostic(freeChunks, usedChunks, reserve, queueDepth, \
                                             writingChunks, transportBlk, heapBlk, budgetBlk, \
                                             sliceDone, peakLatUs, dirtyAgeMs, maxBacklog, pending, \
                                             inProg, captureActive)
#define SC_PERSIST_PRESSURE(freeChunks, reserve, usedChunks) \
  DebugSessionCapture::persistencePoolPressure(freeChunks, reserve, usedChunks)
#define SC_PERSIST_BACKLOG(workQ, writingItems, chunkQ, dirtyAgeMs, estSteps, estBytes, pending, \
                           urgent, transportBlk, budgetBlk, heapBlk) \
  DebugSessionCapture::persistenceBacklog(workQ, writingItems, chunkQ, dirtyAgeMs, estSteps, estBytes, \
                                          pending, urgent, transportBlk, budgetBlk, heapBlk)
#define SC_PERSIST_DRAIN_FAIL(reason, steps, stuckIter, workQ, chunkQ, estSteps, estBytes) \
  DebugSessionCapture::persistenceDrainFailed(reason, steps, stuckIter, workQ, chunkQ, estSteps, \
                                              estBytes)
#define SC_SAVE(phase, rotateStep)         DebugSessionCapture::saveDisplayPhase(phase, rotateStep)
#define SC_LOADSAVE(active)                DebugSessionCapture::loadSaveMode(active)
#define SC_OVERLAY_SEL(mode, row)          DebugSessionCapture::overlayListSelection(mode, row)
#define SC_OVERLAY_CONFIRM(mode, row)      DebugSessionCapture::overlayRowConfirm(mode, row)
#define SC_REC_STORED_NOTE_ON(tick, ch, note) DebugSessionCapture::recStoredNoteOn(tick, ch, note)
#define SC_STORED_NOTE_EVENT(kind, tick, ch, note) DebugSessionCapture::storedNoteEvent(kind, tick, ch, note)
#define SC_CAPTURE_COORD(absTick, storageTick, projPhase, displayPhase, startLoopTick, \
                         projectionCycleStartTick, loopStartTick, ch, note) \
  DebugSessionCapture::captureCoordinate(absTick, storageTick, projPhase, displayPhase, \
                                         startLoopTick, projectionCycleStartTick, loopStartTick, \
                                         ch, note)
#define SC_DISP(slot, state, loopLen, take, visual, frame, buffer, hasCommittedPasses) \
  DebugSessionCapture::displaySnapshot(slot, state, loopLen, take, visual, frame, buffer, hasCommittedPasses)
#define SC_DISP_WINDOW(slot, state, loopLen, take, visual, frame, buffer, hasCommittedPasses, wStart, wBars, \
                      wNotes) \
  DebugSessionCapture::displaySnapshotWindow(slot, state, loopLen, take, visual, frame, buffer, \
                                             hasCommittedPasses, wStart, wBars, wNotes)
#define SC_VCACHE(phase, events, notes, firstBar, lastBar, totalBars, dirtyBarsSize, dirtyCount, \
                  dirtyFlag) \
  DebugSessionCapture::visualCacheState(phase, events, notes, firstBar, lastBar, totalBars, \
                                        dirtyBarsSize, dirtyCount, dirtyFlag)
#define SC_DNTE(pitch, storageStart, displayStart, length, selectedIdx) \
  DebugSessionCapture::displayNoteInfo(pitch, storageStart, displayStart, length, selectedIdx)
#define SC_DFRAME(frameNotes, elapsedUs, frameIndex) \
  DebugSessionCapture::displayFrame(frameNotes, elapsedUs, frameIndex)
#define SC_PLAYBACK_FRAME(slot, currentTick, tickInLoop, prevTickInLoop, projStart, loopStart, \
                          loopLen, idxBefore, idxAfter, orderSize, sent, atLoopStart, wrap, stale, \
                          rev, gen) \
  DebugSessionCapture::playbackFrame(slot, currentTick, tickInLoop, prevTickInLoop, projStart, \
                                     loopStart, loopLen, idxBefore, idxAfter, orderSize, sent, \
                                     atLoopStart, wrap, stale, rev, gen)
#define SC_STORED_WRAP_PAIR(onTick, offTick, ch, note) DebugSessionCapture::storedWrapPair(onTick, offTick, ch, note)
#define SC_CAPTURE_CLEANUP(phase, kind, count) DebugSessionCapture::captureCleanup(phase, kind, count)
#define SC_REC_QUEUE_STORED_NOTE_ON(tick, ch, note) DebugSessionCapture::queueStoredNoteOn(tick, ch, note)
#define SC_REC_FLUSH_PENDING_REVTS(maxLines) DebugSessionCapture::flushCaptureBuffer(maxLines)
#define SC_REC_FLUSH_ALL_PENDING_REVTS()     DebugSessionCapture::flushAllPendingRevts()
#define SC_CAPTURE_FLUSH(maxRecords)         DebugSessionCapture::flushCaptureBuffer(maxRecords)
#define SC_UPDATE(tick, ticksPerBar)         DebugSessionCapture::update(tick, ticksPerBar)
#define SC_MEMORY_PRESSURE(transition, heapFree, chunksFree, queueDepth) \
  DebugSessionCapture::memoryPressureTransition(transition, heapFree, chunksFree, queueDepth)
#define SC_CAPTURE_APPEND_DENY(reason, freeChunks, usedChunks, pressure, ch, note, tick, pendingPass) \
  DebugSessionCapture::captureAppendDeny(reason, freeChunks, usedChunks, pressure, ch, note, tick, \
                                         pendingPass)
#define SC_PASS_RECLAIM(chunksFreeBefore, chunksFreeAfter, passesReclaimed, chunksReleased, \
                        durationUs, pressure, transport) \
  DebugSessionCapture::passReclaim(chunksFreeBefore, chunksFreeAfter, passesReclaimed, \
                                 chunksReleased, durationUs, pressure, transport)

#else  // !SESSION_CAPTURE — all capture macros compile to nothing

namespace DebugSessionCapture {
inline bool appendDiagTraceRecord(const void*, uint16_t) { return false; }
inline void emitDiagCheckpointLine(const Diagnostics::DiagTraceRecord&) {}
inline bool captureBootGraceActive() { return false; }
inline void restartCaptureBootGrace() {}
}  // namespace DebugSessionCapture

#define SC_SESSION_HEADER()                ((void)0)
#define SC_MIDI_IN(src, type, ch, d1, d2)  ((void)0)
#define SC_MIDI_OUT_EVENT(e)               ((void)0)
#define SC_LED_OUT(on, note, vel)          ((void)0)
#define SC_GESTURE(ch0, note, pressType)   ((void)0)
#define SC_STATE(component, from, to)      ((void)0)
#define SC_BPM(raw, smoothed)              ((void)0)
#define SC_CLOCK_SOURCE(from, to)          ((void)0)
#define SC_REC_START(slot, tick)           ((void)0)
#define SC_REC_STOP(kind, slot, tick, start, raw, final, align) ((void)0)
#define SC_REC_STOP_STAGE(stage, elapsedUs, durationUs, heapBefore, heapAfter, eventCount, chunkRefCount, outcome) ((void)0)
#define SC_ODUB_STAGE(stage, durationUs, heapBefore, heapAfter, outcome) ((void)0)
#define SC_ODUB_STOP_STAGE(stage, elapsedUs, durationUs, heapBefore, heapAfter, eventCount, chunkRefCount, outcome) ((void)0)
#define SC_PERSIST(stage, durationUs, heapBefore, heapAfter, outcome) ((void)0)
#define SC_PERSIST_DIAG(freeChunks, usedChunks, reserve, queueDepth, writingChunks, transportBlk, \
                        heapBlk, budgetBlk, sliceDone, peakLatUs, dirtyAgeMs, maxBacklog, pending, \
                        inProg, captureActive) \
  ((void)0)
#define SC_PERSIST_PRESSURE(freeChunks, reserve, usedChunks) ((void)0)
#define SC_PERSIST_BACKLOG(workQ, writingItems, chunkQ, dirtyAgeMs, estSteps, estBytes, pending, \
                           urgent, transportBlk, budgetBlk, heapBlk) \
  ((void)0)
#define SC_PERSIST_DRAIN_FAIL(reason, steps, stuckIter, workQ, chunkQ, estSteps, estBytes) ((void)0)
#define SC_SAVE(phase, rotateStep)         ((void)0)
#define SC_LOADSAVE(active)                ((void)0)
#define SC_OVERLAY_SEL(mode, row)          ((void)0)
#define SC_OVERLAY_CONFIRM(mode, row)      ((void)0)
#define SC_REC_STORED_NOTE_ON(tick, ch, note) ((void)0)
#define SC_STORED_NOTE_EVENT(kind, tick, ch, note) ((void)0)
#define SC_CAPTURE_COORD(absTick, storageTick, projPhase, displayPhase, startLoopTick, \
                         projectionCycleStartTick, loopStartTick, ch, note) \
  ((void)0)
#define SC_DISP(slot, state, loopLen, take, visual, frame, buffer, hasCommittedPasses) ((void)0)
#define SC_DISP_WINDOW(slot, state, loopLen, take, visual, frame, buffer, hasCommittedPasses, wStart, wBars, \
                      wNotes) \
  ((void)0)
#define SC_VCACHE(phase, events, notes, firstBar, lastBar, totalBars, dirtyBarsSize, dirtyCount, \
                  dirtyFlag) \
  ((void)0)
#define SC_DNTE(pitch, storageStart, displayStart, length, selectedIdx) ((void)0)
#define SC_DFRAME(frameNotes, elapsedUs, frameIndex) ((void)0)
#define SC_PLAYBACK_FRAME(slot, currentTick, tickInLoop, prevTickInLoop, projStart, loopStart, \
                          loopLen, idxBefore, idxAfter, orderSize, sent, atLoopStart, wrap, stale, \
                          rev, gen) \
  ((void)0)
#define SC_STORED_WRAP_PAIR(onTick, offTick, ch, note) ((void)0)
#define SC_CAPTURE_CLEANUP(phase, kind, count) ((void)0)
#define SC_REC_QUEUE_STORED_NOTE_ON(tick, ch, note) ((void)0)
#define SC_REC_FLUSH_PENDING_REVTS(maxLines) ((void)0)
#define SC_REC_FLUSH_ALL_PENDING_REVTS()     ((void)0)
#define SC_CAPTURE_FLUSH(maxRecords)         ((void)0)
#define SC_UPDATE(tick, ticksPerBar)         ((void)0)
#define SC_MEMORY_PRESSURE(transition, heapFree, chunksFree, queueDepth) ((void)0)
#define SC_CAPTURE_APPEND_DENY(reason, freeChunks, usedChunks, pressure, ch, note, tick, pendingPass) \
  ((void)0)
#define SC_PASS_RECLAIM(chunksFreeBefore, chunksFreeAfter, passesReclaimed, chunksReleased, \
                        durationUs, pressure, transport) \
  ((void)0)

#endif  // SESSION_CAPTURE
