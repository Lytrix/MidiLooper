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
 * Implementations live in DebugSessionCapture.cpp with FLASHMEM so capture I/O stays out of
 * ITCM/RAM1 (teensy41-capture-serial RAM budget).
 */
#pragma once

#ifdef SESSION_CAPTURE

#include <cstddef>
#include <cstdint>

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
SC_MEM_ATTR void persistence(const char* stage, uint32_t durationUs, uint32_t heapBefore,
                             uint32_t heapAfter, const char* outcome);
SC_MEM_ATTR void saveDisplayPhase(const char* phase, uint8_t rotateStep);
SC_MEM_ATTR void loadSaveMode(uint8_t active);
SC_MEM_ATTR void overlayListSelection(uint8_t mode, uint8_t row);
SC_MEM_ATTR void overlayRowConfirm(uint8_t mode, uint8_t row);
SC_MEM_ATTR void queueStoredNoteOn(uint32_t tick, uint8_t ch, uint8_t note);
SC_MEM_ATTR void recStoredNoteOn(uint32_t tick, uint8_t ch, uint8_t note);
SC_MEM_ATTR void storedNoteEvent(char kind, uint32_t tick, uint8_t ch, uint8_t note);
SC_MEM_ATTR void displaySnapshot(uint8_t slot, const char* trackState, uint32_t loopLen,
                                 size_t takeEvents, size_t visualNotes, size_t frameNotes,
                                 size_t bufferEvents, int published);
SC_MEM_ATTR void displaySnapshotWindow(uint8_t slot, const char* trackState, uint32_t loopLen,
                                       size_t takeEvents, size_t visualNotes, size_t frameNotes,
                                       size_t bufferEvents, int published, uint32_t windowStartTick,
                                       uint8_t windowBars, size_t windowNoteCount);
SC_MEM_ATTR void displayNoteInfo(uint8_t pitch, uint32_t storageStart, uint32_t displayStart,
                                 uint32_t length, int selectedIdx);
SC_MEM_ATTR void storedWrapPair(uint32_t onTick, uint32_t offTick, uint8_t ch, uint8_t note);
SC_MEM_ATTR size_t flushPendingRevts(size_t maxLines = 64);
SC_MEM_ATTR void flushAllPendingRevts();
SC_MEM_ATTR void update(uint32_t currentTick, uint32_t ticksPerBar);

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
#define SC_PERSIST(stage, durationUs, heapBefore, heapAfter, outcome) \
                                           DebugSessionCapture::persistence(stage, durationUs, heapBefore, heapAfter, outcome)
#define SC_SAVE(phase, rotateStep)         DebugSessionCapture::saveDisplayPhase(phase, rotateStep)
#define SC_LOADSAVE(active)                DebugSessionCapture::loadSaveMode(active)
#define SC_OVERLAY_SEL(mode, row)          DebugSessionCapture::overlayListSelection(mode, row)
#define SC_OVERLAY_CONFIRM(mode, row)      DebugSessionCapture::overlayRowConfirm(mode, row)
#define SC_REC_STORED_NOTE_ON(tick, ch, note) DebugSessionCapture::recStoredNoteOn(tick, ch, note)
#define SC_STORED_NOTE_EVENT(kind, tick, ch, note) DebugSessionCapture::storedNoteEvent(kind, tick, ch, note)
#define SC_DISP(slot, state, loopLen, take, visual, frame, buffer, published) \
  DebugSessionCapture::displaySnapshot(slot, state, loopLen, take, visual, frame, buffer, published)
#define SC_DISP_WINDOW(slot, state, loopLen, take, visual, frame, buffer, published, wStart, wBars, \
                       wNotes) \
  DebugSessionCapture::displaySnapshotWindow(slot, state, loopLen, take, visual, frame, buffer, \
                                             published, wStart, wBars, wNotes)
#define SC_DNTE(pitch, storageStart, displayStart, length, selectedIdx) \
  DebugSessionCapture::displayNoteInfo(pitch, storageStart, displayStart, length, selectedIdx)
#define SC_STORED_WRAP_PAIR(onTick, offTick, ch, note) DebugSessionCapture::storedWrapPair(onTick, offTick, ch, note)
#define SC_REC_QUEUE_STORED_NOTE_ON(tick, ch, note) DebugSessionCapture::queueStoredNoteOn(tick, ch, note)
#define SC_REC_FLUSH_PENDING_REVTS(maxLines) DebugSessionCapture::flushPendingRevts(maxLines)
#define SC_REC_FLUSH_ALL_PENDING_REVTS()   DebugSessionCapture::flushAllPendingRevts()
#define SC_UPDATE(tick, ticksPerBar)       DebugSessionCapture::update(tick, ticksPerBar)

#else  // !SESSION_CAPTURE — all capture macros compile to nothing

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
#define SC_PERSIST(stage, durationUs, heapBefore, heapAfter, outcome) ((void)0)
#define SC_SAVE(phase, rotateStep)         ((void)0)
#define SC_LOADSAVE(active)                ((void)0)
#define SC_OVERLAY_SEL(mode, row)          ((void)0)
#define SC_OVERLAY_CONFIRM(mode, row)      ((void)0)
#define SC_REC_STORED_NOTE_ON(tick, ch, note) ((void)0)
#define SC_STORED_NOTE_EVENT(kind, tick, ch, note) ((void)0)
#define SC_DISP(slot, state, loopLen, take, visual, frame, buffer, published) ((void)0)
#define SC_DISP_WINDOW(slot, state, loopLen, take, visual, frame, buffer, published, wStart, wBars, \
                       wNotes) \
  ((void)0)
#define SC_DNTE(pitch, storageStart, displayStart, length, selectedIdx) ((void)0)
#define SC_STORED_WRAP_PAIR(onTick, offTick, ch, note) ((void)0)
#define SC_REC_QUEUE_STORED_NOTE_ON(tick, ch, note) ((void)0)
#define SC_REC_FLUSH_PENDING_REVTS(maxLines) ((void)0)
#define SC_REC_FLUSH_ALL_PENDING_REVTS()   ((void)0)
#define SC_UPDATE(tick, ticksPerBar)       ((void)0)

#endif  // SESSION_CAPTURE
