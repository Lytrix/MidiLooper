//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file DebugSessionCapture.h
 * @brief Session fixture recording for the instrumented capture build (Bucket 1 S1).
 *
 * Emits machine-parseable `#CAP,...` lines on USB serial so a hardware session can be
 * replayed offline (native harness) and turned into regression fixtures.
 *
 * Only active when built with -D SESSION_CAPTURE (env:teensy41-capture in platformio.ini).
 * In all other builds every SC_* macro compiles to nothing.
 *
 * Record types (all lines start with `#CAP,<micros>`):
 *   HDR  ,v1                                  session header (end of setup)
 *   MI   ,<src>,<type>,<ch>,<d1>,<d2>          MIDI in  (src: U=usb device, S=serial, H=usb host; includes Clock=248)
 *   MO   ,<type>,<ch>,<d1>,<d2>                MIDI out via sendMidiEvent (Clock excluded)
 *   LED  ,<1|0>,<note>,<vel>                   LED feedback out (ch15 fast path)
 *   GS   ,<ch0>,<note>,<pressType>             resolved button gesture (PressType enum value)
 *   ST   ,<component>,<from>,<to>              state machine transition
 *   BPM  ,<raw>,<smoothed>                     external-clock BPM: window value vs smoothed (B2)
 *   CS   ,<from>,<to>                          clock source transition (INT/EXT)
 *   RECA ,<slot>,<tick>                        recording armed/started: startLoopTick stamp (B1)
 *   RECS ,<kind>,<slot>,<tick>,<start>,<raw>,<final>,<align>  recording stopped: length finalization (B1)
 *   REVT ,<tick>,<ch>,<note>                   stored note-on tick after record commit (B1)
 *   SEVT ,<N|F>,<tick>,<ch>,<note>             stored note-on/off after overdub commit (verify)
 *   DISP ,<slot>,<state>,<loopLen>,<take>,<visual>,<frame>,<buffer>,<published>
 *                                               OLED piano-roll snapshot (display vs storage)
 *   DNTE ,<pitch>,<storageStart>,<displayStart>,<length>,<selectedIdx>
 *                                               OLED note-info row (edit selection; display ticks)
 *   SAVE ,<phase>,<rotateStep>                 sidebar deferred-save status phase transition
 *   WRAP ,<onTick>,<offTick>,<ch>,<note>       wrapped tail-on / head-off pair in committed store
 *   BAR  ,<tick>,<bar>                         bar boundary marker (tick<->micros alignment)
 */
#pragma once

#ifdef SESSION_CAPTURE

#include <Arduino.h>
#include <vector>
#include <algorithm>
#include "MidiEvent.h"

namespace DebugSessionCapture {

inline void sessionHeader() {
  Serial.printf("#CAP,%lu,HDR,v1\r\n", (unsigned long)micros());
}

inline void midiIn(char src, uint8_t type, uint8_t ch, uint8_t d1, uint8_t d2) {
  Serial.printf("#CAP,%lu,MI,%c,%u,%u,%u,%u\r\n",
                (unsigned long)micros(), src, type, ch, d1, d2);
}

inline void midiOut(uint8_t type, uint8_t ch, uint8_t d1, uint8_t d2) {
  Serial.printf("#CAP,%lu,MO,%u,%u,%u,%u\r\n",
                (unsigned long)micros(), type, ch, d1, d2);
}

inline void midiOutEvent(const MidiEvent& e) {
  uint8_t d1 = 0, d2 = 0;
  switch (e.type) {
    case midi::NoteOn:
    case midi::NoteOff:
      d1 = e.data.noteData.note; d2 = e.data.noteData.velocity; break;
    case midi::ControlChange:
      d1 = e.data.ccData.cc; d2 = e.data.ccData.value; break;
    case midi::PitchBend: {
      uint16_t raw = (uint16_t)((int32_t)e.data.pitchBend + 8192);
      d1 = raw & 0x7F; d2 = (raw >> 7) & 0x7F; break;
    }
    case midi::AfterTouchChannel:
      d1 = e.data.channelPressure; break;
    case midi::ProgramChange:
      d1 = e.data.program; break;
    default:
      break;
  }
  midiOut((uint8_t)e.type, e.channel, d1, d2);
}

inline void ledOut(bool on, uint8_t note, uint8_t vel) {
  Serial.printf("#CAP,%lu,LED,%d,%u,%u\r\n",
                (unsigned long)micros(), on ? 1 : 0, note, vel);
}

inline void gesture(uint8_t channel0, uint8_t note, int pressType) {
  Serial.printf("#CAP,%lu,GS,%u,%u,%d\r\n",
                (unsigned long)micros(), channel0, note, pressType);
}

inline void stateTransition(const char* component, const char* from, const char* to) {
  Serial.printf("#CAP,%lu,ST,%s,%s,%s\r\n",
                (unsigned long)micros(), component, from, to);
}

inline void bpm(float raw, float smoothed) {
  Serial.printf("#CAP,%lu,BPM,%.3f,%.3f\r\n",
                (unsigned long)micros(), (double)raw, (double)smoothed);
}

inline void clockSource(const char* from, const char* to) {
  Serial.printf("#CAP,%lu,CS,%s,%s\r\n", (unsigned long)micros(), from, to);
}

inline void recStart(uint8_t slot, uint32_t tick) {
  Serial.printf("#CAP,%lu,RECA,%u,%lu\r\n",
                (unsigned long)micros(), slot, (unsigned long)tick);
}

inline void recStop(const char* kind, uint8_t slot, uint32_t tick, uint32_t startTick,
                    uint32_t rawLength, uint32_t finalLength, bool align) {
  Serial.printf("#CAP,%lu,RECS,%s,%u,%lu,%lu,%lu,%lu,%d\r\n",
                (unsigned long)micros(), kind, slot, (unsigned long)tick,
                (unsigned long)startTick, (unsigned long)rawLength,
                (unsigned long)finalLength, align ? 1 : 0);
}

inline void recStopStage(const char* stage, uint32_t elapsedUs, uint32_t durationUs,
                         uint32_t heapBefore, uint32_t heapAfter,
                         size_t eventCount, size_t chunkRefCount,
                         const char* outcome) {
  Serial.printf("#CAP,%lu,RECS,stage,%s,%lu,%lu,%lu,%lu,%lu,%lu,%s\r\n",
                (unsigned long)micros(), stage,
                (unsigned long)elapsedUs, (unsigned long)durationUs,
                (unsigned long)heapBefore, (unsigned long)heapAfter,
                (unsigned long)eventCount, (unsigned long)chunkRefCount, outcome);
}

inline void persistence(const char* stage, uint32_t durationUs,
                        uint32_t heapBefore, uint32_t heapAfter,
                        const char* outcome) {
  Serial.printf("#CAP,%lu,PERS,%s,%lu,%lu,%lu,%s\r\n",
                (unsigned long)micros(), stage, (unsigned long)durationUs,
                (unsigned long)heapBefore, (unsigned long)heapAfter, outcome);
}

inline void saveDisplayPhase(const char* phase, uint8_t rotateStep) {
  Serial.printf("#CAP,%lu,SAVE,%s,%u\r\n",
                (unsigned long)micros(), phase, rotateStep);
}

struct PendingRevt {
  uint32_t tick;
  uint8_t ch;
  uint8_t note;
};

inline std::vector<PendingRevt>& pendingRevts() {
  static std::vector<PendingRevt> queue;
  return queue;
}

inline void queueStoredNoteOn(uint32_t tick, uint8_t ch, uint8_t note) {
  pendingRevts().push_back({tick, ch, note});
}

inline void recStoredNoteOn(uint32_t tick, uint8_t ch, uint8_t note) {
  Serial.printf("#CAP,%lu,REVT,%lu,%u,%u\r\n",
                (unsigned long)micros(), (unsigned long)tick, ch, note);
}

inline void storedNoteEvent(char kind, uint32_t tick, uint8_t ch, uint8_t note) {
  Serial.printf("#CAP,%lu,SEVT,%c,%lu,%u,%u\r\n",
                (unsigned long)micros(), kind, (unsigned long)tick, ch, note);
}

inline void displaySnapshot(uint8_t slot, const char* trackState, uint32_t loopLen,
                            size_t takeEvents, size_t visualNotes, size_t frameNotes,
                            size_t bufferEvents, int published) {
  Serial.printf("#CAP,%lu,DISP,%u,%s,%lu,%zu,%zu,%zu,%zu,%d\r\n",
                (unsigned long)micros(), slot, trackState, (unsigned long)loopLen,
                takeEvents, visualNotes, frameNotes, bufferEvents, published);
}

inline void displaySnapshotWindow(uint8_t slot, const char* trackState, uint32_t loopLen,
                                  size_t takeEvents, size_t visualNotes, size_t frameNotes,
                                  size_t bufferEvents, int published, uint32_t windowStartTick,
                                  uint8_t windowBars, size_t windowNoteCount) {
  Serial.printf("#CAP,%lu,DISP,%u,%s,%lu,%zu,%zu,%zu,%zu,%d,%lu,%u,%zu\r\n",
                (unsigned long)micros(), slot, trackState, (unsigned long)loopLen,
                takeEvents, visualNotes, frameNotes, bufferEvents, published,
                (unsigned long)windowStartTick, windowBars, windowNoteCount);
}

inline void displayNoteInfo(uint8_t pitch, uint32_t storageStart, uint32_t displayStart,
                            uint32_t length, int selectedIdx) {
  Serial.printf("#CAP,%lu,DNTE,%u,%lu,%lu,%lu,%d\r\n",
                (unsigned long)micros(), pitch, (unsigned long)storageStart,
                (unsigned long)displayStart, (unsigned long)length, selectedIdx);
}

inline void storedWrapPair(uint32_t onTick, uint32_t offTick, uint8_t ch, uint8_t note) {
  Serial.printf("#CAP,%lu,WRAP,%lu,%lu,%u,%u\r\n",
                (unsigned long)micros(), (unsigned long)onTick, (unsigned long)offTick, ch, note);
}

/// Emit queued REVT lines in batches (idle path; keeps stop hot path short).
inline size_t flushPendingRevts(size_t maxLines = 64) {
  auto& queue = pendingRevts();
  const size_t n = std::min(queue.size(), maxLines);
  for (size_t i = 0; i < n; ++i) {
    const PendingRevt& r = queue[i];
    recStoredNoteOn(r.tick, r.ch, r.note);
  }
  if (n > 0) {
    queue.erase(queue.begin(), queue.begin() + static_cast<std::ptrdiff_t>(n));
  }
  return n;
}

inline void flushAllPendingRevts() {
  while (!pendingRevts().empty()) {
    flushPendingRevts(64);
  }
}

/// Call from the main loop; emits a BAR line whenever the bar number changes.
inline void update(uint32_t currentTick, uint32_t ticksPerBar) {
  static uint32_t lastBar = 0xFFFFFFFF;
  uint32_t bar = currentTick / ticksPerBar;
  if (bar != lastBar) {
    lastBar = bar;
    Serial.printf("#CAP,%lu,BAR,%lu,%lu\r\n",
                  (unsigned long)micros(), (unsigned long)currentTick, (unsigned long)bar);
  }
}

} // namespace DebugSessionCapture

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

#else // !SESSION_CAPTURE — all capture macros compile to nothing

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

#endif // SESSION_CAPTURE
