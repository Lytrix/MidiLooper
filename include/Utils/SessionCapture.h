//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file SessionCapture.h
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
 *   BAR  ,<tick>,<bar>                         bar boundary marker (tick<->micros alignment)
 */
#pragma once

#ifdef SESSION_CAPTURE

#include <Arduino.h>
#include <vector>
#include <algorithm>
#include "MidiEvent.h"

namespace SessionCapture {

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

} // namespace SessionCapture

#define SC_SESSION_HEADER()                SessionCapture::sessionHeader()
#define SC_MIDI_IN(src, type, ch, d1, d2)  SessionCapture::midiIn(src, type, ch, d1, d2)
#define SC_MIDI_OUT_EVENT(e)               SessionCapture::midiOutEvent(e)
#define SC_LED_OUT(on, note, vel)          SessionCapture::ledOut(on, note, vel)
#define SC_GESTURE(ch0, note, pressType)   SessionCapture::gesture(ch0, note, pressType)
#define SC_STATE(component, from, to)      SessionCapture::stateTransition(component, from, to)
#define SC_BPM(raw, smoothed)              SessionCapture::bpm(raw, smoothed)
#define SC_CLOCK_SOURCE(from, to)          SessionCapture::clockSource(from, to)
#define SC_REC_START(slot, tick)           SessionCapture::recStart(slot, tick)
#define SC_REC_STOP(kind, slot, tick, start, raw, final, align) \
                                           SessionCapture::recStop(kind, slot, tick, start, raw, final, align)
#define SC_REC_STORED_NOTE_ON(tick, ch, note) SessionCapture::recStoredNoteOn(tick, ch, note)
#define SC_REC_QUEUE_STORED_NOTE_ON(tick, ch, note) SessionCapture::queueStoredNoteOn(tick, ch, note)
#define SC_REC_FLUSH_PENDING_REVTS(maxLines) SessionCapture::flushPendingRevts(maxLines)
#define SC_REC_FLUSH_ALL_PENDING_REVTS()   SessionCapture::flushAllPendingRevts()
#define SC_UPDATE(tick, ticksPerBar)       SessionCapture::update(tick, ticksPerBar)

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
#define SC_REC_STORED_NOTE_ON(tick, ch, note) ((void)0)
#define SC_REC_QUEUE_STORED_NOTE_ON(tick, ch, note) ((void)0)
#define SC_REC_FLUSH_PENDING_REVTS(maxLines) ((void)0)
#define SC_REC_FLUSH_ALL_PENDING_REVTS()   ((void)0)
#define SC_UPDATE(tick, ticksPerBar)       ((void)0)

#endif // SESSION_CAPTURE
