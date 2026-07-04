//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/DebugSessionCapture.h"

#ifdef SESSION_CAPTURE

#include <Arduino.h>
#include <algorithm>
#include <cstring>

#if defined(__IMXRT1062__)
extern "C" void* extmem_malloc(size_t size);
extern "C" void extmem_free(void* ptr);
#endif

namespace DebugSessionCapture {

namespace {

enum class CaptureRecordType : uint8_t {
  Revt = 1,
  Text = 2,
};

struct CaptureRecordHeader {
  uint8_t type = 0;
  uint16_t payloadLen = 0;
};

constexpr size_t kCaptureRingBytes = 96 * 1024;
constexpr size_t kMaxCaptureTextBytes = 192;

struct CaptureRing {
  uint8_t* data = nullptr;
  size_t capacity = 0;
  size_t head = 0;
  size_t tail = 0;
  size_t used = 0;
  bool overflowPending = false;
};

CaptureRing sCaptureRing;

bool ensureCaptureRingAllocated() {
  if (sCaptureRing.data != nullptr) {
    return true;
  }
#if defined(__IMXRT1062__)
  sCaptureRing.data = static_cast<uint8_t*>(extmem_malloc(kCaptureRingBytes));
  if (sCaptureRing.data == nullptr) {
    return false;
  }
  sCaptureRing.capacity = kCaptureRingBytes;
  sCaptureRing.head = 0;
  sCaptureRing.tail = 0;
  sCaptureRing.used = 0;
  return true;
#else
  return false;
#endif
}

void writeByteAt(size_t index, uint8_t value) {
  sCaptureRing.data[index % sCaptureRing.capacity] = value;
}

uint8_t readByteAt(size_t index) {
  return sCaptureRing.data[index % sCaptureRing.capacity];
}

void writeBytesAt(size_t index, const void* src, size_t len) {
  const uint8_t* bytes = static_cast<const uint8_t*>(src);
  for (size_t i = 0; i < len; ++i) {
    writeByteAt(index + i, bytes[i]);
  }
}

void readBytesAt(size_t index, void* dest, size_t len) {
  uint8_t* bytes = static_cast<uint8_t*>(dest);
  for (size_t i = 0; i < len; ++i) {
    bytes[i] = readByteAt(index + i);
  }
}

bool readHeaderAt(size_t index, CaptureRecordHeader& headerOut) {
  if (sCaptureRing.used < sizeof(CaptureRecordHeader)) {
    return false;
  }
  readBytesAt(index, &headerOut, sizeof(CaptureRecordHeader));
  return true;
}

size_t recordTotalBytes(const CaptureRecordHeader& header) {
  return sizeof(CaptureRecordHeader) + header.payloadLen;
}

void advanceHead(size_t bytes) {
  sCaptureRing.head = (sCaptureRing.head + bytes) % sCaptureRing.capacity;
  sCaptureRing.used -= bytes;
}

void discardOldestRecord() {
  if (sCaptureRing.used < sizeof(CaptureRecordHeader)) {
    sCaptureRing.head = sCaptureRing.tail;
    sCaptureRing.used = 0;
    return;
  }
  CaptureRecordHeader header{};
  if (!readHeaderAt(sCaptureRing.head, header)) {
    sCaptureRing.head = sCaptureRing.tail;
    sCaptureRing.used = 0;
    return;
  }
  const size_t total = recordTotalBytes(header);
  if (total > sCaptureRing.used) {
    sCaptureRing.head = sCaptureRing.tail;
    sCaptureRing.used = 0;
    return;
  }
  advanceHead(total);
  sCaptureRing.overflowPending = true;
}

SC_MEM_ATTR bool appendCaptureRecord(CaptureRecordType type, const void* payload, uint16_t payloadLen) {
  if (!ensureCaptureRingAllocated()) {
    return false;
  }
  const CaptureRecordHeader header{
      static_cast<uint8_t>(type),
      payloadLen,
  };
  const size_t total = recordTotalBytes(header);
  while (sCaptureRing.used + total > sCaptureRing.capacity) {
    discardOldestRecord();
  }
  writeBytesAt(sCaptureRing.tail, &header, sizeof(header));
  writeBytesAt(sCaptureRing.tail + sizeof(header), payload, payloadLen);
  sCaptureRing.tail = (sCaptureRing.tail + total) % sCaptureRing.capacity;
  sCaptureRing.used += total;
  return true;
}

void emitOverflowNotice() {
  if (!sCaptureRing.overflowPending) {
    return;
  }
  sCaptureRing.overflowPending = false;
  Serial.printf("#CAP,%lu,RING,overflow\r\n", (unsigned long)micros());
}

void flushOneRevtRecord(const PendingRevt& revt) {
  Serial.printf("#CAP,%lu,REVT,%lu,%u,%u\r\n",
                (unsigned long)micros(), (unsigned long)revt.tick, revt.ch, revt.note);
}

}  // namespace

SC_MEM_ATTR void initCaptureBuffer() {
  (void)ensureCaptureRingAllocated();
}

SC_MEM_ATTR void appendCaptureTextLine(const char* line) {
  if (line == nullptr) {
    return;
  }
  const size_t len = strnlen(line, kMaxCaptureTextBytes - 1);
  if (len == 0) {
    return;
  }
  (void)appendCaptureRecord(CaptureRecordType::Text, line, static_cast<uint16_t>(len + 1));
}

SC_MEM_ATTR void sessionHeader() {
  initCaptureBuffer();
  Serial.printf("#CAP,%lu,HDR,v1\r\n", (unsigned long)micros());
}

SC_MEM_ATTR void midiIn(char src, uint8_t type, uint8_t ch, uint8_t d1, uint8_t d2) {
  Serial.printf("#CAP,%lu,MI,%c,%u,%u,%u,%u\r\n",
                (unsigned long)micros(), src, type, ch, d1, d2);
}

SC_MEM_ATTR void midiOut(uint8_t type, uint8_t ch, uint8_t d1, uint8_t d2) {
  Serial.printf("#CAP,%lu,MO,%u,%u,%u,%u\r\n",
                (unsigned long)micros(), type, ch, d1, d2);
}

SC_MEM_ATTR void midiOutEvent(const MidiEvent& e) {
  uint8_t d1 = 0, d2 = 0;
  switch (e.type) {
    case midi::NoteOn:
    case midi::NoteOff:
      d1 = e.data.noteData.note;
      d2 = e.data.noteData.velocity;
      break;
    case midi::ControlChange:
      d1 = e.data.ccData.cc;
      d2 = e.data.ccData.value;
      break;
    case midi::PitchBend: {
      uint16_t raw = (uint16_t)((int32_t)e.data.pitchBend + 8192);
      d1 = raw & 0x7F;
      d2 = (raw >> 7) & 0x7F;
      break;
    }
    case midi::AfterTouchChannel:
      d1 = e.data.channelPressure;
      break;
    case midi::ProgramChange:
      d1 = e.data.program;
      break;
    default:
      break;
  }
  midiOut((uint8_t)e.type, e.channel, d1, d2);
}

SC_MEM_ATTR void ledOut(bool on, uint8_t note, uint8_t vel) {
  Serial.printf("#CAP,%lu,LED,%d,%u,%u\r\n",
                (unsigned long)micros(), on ? 1 : 0, note, vel);
}

SC_MEM_ATTR void gesture(uint8_t channel0, uint8_t note, int pressType) {
  Serial.printf("#CAP,%lu,GS,%u,%u,%d\r\n",
                (unsigned long)micros(), channel0, note, pressType);
}

SC_MEM_ATTR void stateTransition(const char* component, const char* from, const char* to) {
  Serial.printf("#CAP,%lu,ST,%s,%s,%s\r\n",
                (unsigned long)micros(), component, from, to);
}

SC_MEM_ATTR void bpm(float raw, float smoothed) {
  Serial.printf("#CAP,%lu,BPM,%.3f,%.3f\r\n",
                (unsigned long)micros(), (double)raw, (double)smoothed);
}

SC_MEM_ATTR void clockSource(const char* from, const char* to) {
  Serial.printf("#CAP,%lu,CS,%s,%s\r\n", (unsigned long)micros(), from, to);
}

SC_MEM_ATTR void recStart(uint8_t slot, uint32_t tick) {
  Serial.printf("#CAP,%lu,RECA,%u,%lu\r\n",
                (unsigned long)micros(), slot, (unsigned long)tick);
}

SC_MEM_ATTR void recStop(const char* kind, uint8_t slot, uint32_t tick, uint32_t startTick,
                         uint32_t rawLength, uint32_t finalLength, bool align) {
  Serial.printf("#CAP,%lu,RECS,%s,%u,%lu,%lu,%lu,%lu,%d\r\n",
                (unsigned long)micros(), kind, slot, (unsigned long)tick,
                (unsigned long)startTick, (unsigned long)rawLength,
                (unsigned long)finalLength, align ? 1 : 0);
}

SC_MEM_ATTR void recStopStage(const char* stage, uint32_t elapsedUs, uint32_t durationUs,
                              uint32_t heapBefore, uint32_t heapAfter, size_t eventCount,
                              size_t chunkRefCount, const char* outcome) {
  Serial.printf("#CAP,%lu,RECS,stage,%s,%lu,%lu,%lu,%lu,%lu,%lu,%s\r\n",
                (unsigned long)micros(), stage,
                (unsigned long)elapsedUs, (unsigned long)durationUs,
                (unsigned long)heapBefore, (unsigned long)heapAfter,
                (unsigned long)eventCount, (unsigned long)chunkRefCount, outcome);
}

SC_MEM_ATTR void persistence(const char* stage, uint32_t durationUs, uint32_t heapBefore,
                             uint32_t heapAfter, const char* outcome) {
  Serial.printf("#CAP,%lu,PERS,%s,%lu,%lu,%lu,%s\r\n",
                (unsigned long)micros(), stage, (unsigned long)durationUs,
                (unsigned long)heapBefore, (unsigned long)heapAfter, outcome);
}

SC_MEM_ATTR void saveDisplayPhase(const char* phase, uint8_t rotateStep) {
  Serial.printf("#CAP,%lu,SAVE,%s,%u\r\n",
                (unsigned long)micros(), phase, rotateStep);
}

SC_MEM_ATTR void loadSaveMode(uint8_t active) {
  Serial.printf("#CAP,%lu,LDSV,%u\r\n", (unsigned long)micros(), active);
}

SC_MEM_ATTR void overlayListSelection(uint8_t mode, uint8_t row) {
  Serial.printf("#CAP,%lu,OVLY,sel,%u,%u\r\n", (unsigned long)micros(), mode, row);
}

SC_MEM_ATTR void overlayRowConfirm(uint8_t mode, uint8_t row) {
  Serial.printf("#CAP,%lu,OVLY,confirm,%u,%u\r\n", (unsigned long)micros(), mode, row);
}

SC_MEM_ATTR void queueStoredNoteOn(uint32_t tick, uint8_t ch, uint8_t note) {
  const PendingRevt revt{tick, ch, note};
  (void)appendCaptureRecord(CaptureRecordType::Revt, &revt, sizeof(revt));
}

SC_MEM_ATTR void recStoredNoteOn(uint32_t tick, uint8_t ch, uint8_t note) {
  Serial.printf("#CAP,%lu,REVT,%lu,%u,%u\r\n",
                (unsigned long)micros(), (unsigned long)tick, ch, note);
}

SC_MEM_ATTR void storedNoteEvent(char kind, uint32_t tick, uint8_t ch, uint8_t note) {
  Serial.printf("#CAP,%lu,SEVT,%c,%lu,%u,%u\r\n",
                (unsigned long)micros(), kind, (unsigned long)tick, ch, note);
}

SC_MEM_ATTR void displaySnapshot(uint8_t slot, const char* trackState, uint32_t loopLen,
                                 size_t takeEvents, size_t visualNotes, size_t frameNotes,
                                 size_t bufferEvents, int published) {
  Serial.printf("#CAP,%lu,DISP,%u,%s,%lu,%zu,%zu,%zu,%zu,%d\r\n",
                (unsigned long)micros(), slot, trackState, (unsigned long)loopLen,
                takeEvents, visualNotes, frameNotes, bufferEvents, published);
}

SC_MEM_ATTR void displaySnapshotWindow(uint8_t slot, const char* trackState, uint32_t loopLen,
                                       size_t takeEvents, size_t visualNotes, size_t frameNotes,
                                       size_t bufferEvents, int published, uint32_t windowStartTick,
                                       uint8_t windowBars, size_t windowNoteCount) {
  Serial.printf("#CAP,%lu,DISP,%u,%s,%lu,%zu,%zu,%zu,%zu,%d,%lu,%u,%zu\r\n",
                (unsigned long)micros(), slot, trackState, (unsigned long)loopLen,
                takeEvents, visualNotes, frameNotes, bufferEvents, published,
                (unsigned long)windowStartTick, windowBars, windowNoteCount);
}

SC_MEM_ATTR void displayNoteInfo(uint8_t pitch, uint32_t storageStart, uint32_t displayStart,
                                 uint32_t length, int selectedIdx) {
  Serial.printf("#CAP,%lu,DNTE,%u,%lu,%lu,%lu,%d\r\n",
                (unsigned long)micros(), pitch, (unsigned long)storageStart,
                (unsigned long)displayStart, (unsigned long)length, selectedIdx);
}

SC_MEM_ATTR void storedWrapPair(uint32_t onTick, uint32_t offTick, uint8_t ch, uint8_t note) {
  Serial.printf("#CAP,%lu,WRAP,%lu,%lu,%u,%u\r\n",
                (unsigned long)micros(), (unsigned long)onTick, (unsigned long)offTick, ch, note);
}

SC_MEM_ATTR void captureCleanup(const char* phase, const char* kind, uint32_t count) {
  Serial.printf("#CAP,%lu,CLN,%s,%s,%lu\r\n",
                (unsigned long)micros(), phase, kind, (unsigned long)count);
}

void flushCaptureBuffer(size_t maxRecords) {
  emitOverflowNotice();
  if (sCaptureRing.data == nullptr || sCaptureRing.used == 0) {
    return;
  }

  size_t flushed = 0;
  while (flushed < maxRecords && sCaptureRing.used >= sizeof(CaptureRecordHeader)) {
    CaptureRecordHeader header{};
    readBytesAt(sCaptureRing.head, &header, sizeof(header));
    const size_t total = recordTotalBytes(header);
    if (header.payloadLen == 0 || total > sCaptureRing.used) {
      sCaptureRing.head = sCaptureRing.tail;
      sCaptureRing.used = 0;
      break;
    }

    if (header.type == static_cast<uint8_t>(CaptureRecordType::Revt)) {
      if (header.payloadLen == sizeof(PendingRevt)) {
        PendingRevt revt{};
        readBytesAt(sCaptureRing.head + sizeof(header), &revt, sizeof(revt));
        flushOneRevtRecord(revt);
      }
    } else if (header.type == static_cast<uint8_t>(CaptureRecordType::Text)) {
      char line[kMaxCaptureTextBytes] = {};
      const size_t copyLen = std::min(static_cast<size_t>(header.payloadLen), sizeof(line));
      readBytesAt(sCaptureRing.head + sizeof(header), line, copyLen);
      line[sizeof(line) - 1] = '\0';
      Serial.println(line);
    }

    advanceHead(total);
    ++flushed;
  }
}

SC_MEM_ATTR size_t flushPendingRevts(size_t maxLines) {
  flushCaptureBuffer(maxLines);
  return maxLines;
}

SC_MEM_ATTR void flushAllPendingRevts() {
  while (sCaptureRing.used > 0) {
    flushCaptureBuffer(64);
  }
}

SC_MEM_ATTR void update(uint32_t currentTick, uint32_t ticksPerBar) {
  static uint32_t lastBar = 0xFFFFFFFF;
  const uint32_t bar = currentTick / ticksPerBar;
  if (bar != lastBar) {
    lastBar = bar;
    Serial.printf("#CAP,%lu,BAR,%lu,%lu\r\n",
                  (unsigned long)micros(), (unsigned long)currentTick, (unsigned long)bar);
  }
}

}  // namespace DebugSessionCapture

#endif  // SESSION_CAPTURE
