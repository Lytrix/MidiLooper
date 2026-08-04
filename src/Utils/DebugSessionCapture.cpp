//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/DebugSessionCapture.h"

#ifdef SESSION_CAPTURE

#include <Arduino.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "Utils/DiagnosticsTypes.h"

#if defined(__IMXRT1062__)
extern "C" void* extmem_malloc(size_t size);
extern "C" void extmem_free(void* ptr);
#endif

namespace DebugSessionCapture {

constexpr uint32_t kBootSerialGraceUs = 5000000u;
uint32_t sCaptureBootUs = 0;

void markCaptureBootUs() {
  if (sCaptureBootUs == 0) {
    sCaptureBootUs = micros();
  }
}

namespace {

enum class CaptureRecordType : uint8_t {
  Revt = 1,
  Text = 2,
  DiagTrace = 3,
};

struct CaptureRecordHeader {
  uint8_t type = 0;
  uint16_t payloadLen = 0;
};

constexpr size_t kCaptureRingBytes = 96 * 1024;
constexpr size_t kCaptureRingPressureBytes = (kCaptureRingBytes * 3) / 4;
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

bool readHeaderAt(size_t index, CaptureRecordHeader& headerOut);
void readBytesAt(size_t index, void* dest, size_t len);

SC_MEM_ATTR bool isTierATextLine(const char* line) {
  if (line == nullptr || strncmp(line, "#CAP,", 5) != 0) {
    return false;
  }
  const char* tagStart = strchr(line + 5, ',');
  if (tagStart == nullptr) {
    return false;
  }
  tagStart = strchr(tagStart + 1, ',');
  if (tagStart == nullptr) {
    return false;
  }
  tagStart++;
  return strncmp(tagStart, "ST,", 3) == 0 || strncmp(tagStart, "PERS,", 5) == 0 ||
         strncmp(tagStart, "RECS,", 5) == 0 || strncmp(tagStart, "HDR,", 4) == 0;
}

SC_MEM_ATTR bool isTierCTextLine(const char* line) {
  if (line == nullptr || strncmp(line, "#CAP,", 5) != 0) {
    return false;
  }
  const char* tagStart = strchr(line + 5, ',');
  if (tagStart == nullptr) {
    return false;
  }
  tagStart = strchr(tagStart + 1, ',');
  if (tagStart == nullptr) {
    return false;
  }
  tagStart++;
  return strncmp(tagStart, "MO,", 3) == 0 || strncmp(tagStart, "MI,", 3) == 0;
}

SC_MEM_ATTR bool captureRingPressureHigh() {
  return sCaptureRing.overflowPending || sCaptureRing.used >= kCaptureRingPressureBytes;
}

SC_MEM_ATTR bool headRecordIsTierAText() {
  if (sCaptureRing.used < sizeof(CaptureRecordHeader)) {
    return false;
  }
  CaptureRecordHeader header{};
  if (!readHeaderAt(sCaptureRing.head, header)) {
    return false;
  }
  if (header.type != static_cast<uint8_t>(CaptureRecordType::Text) || header.payloadLen == 0) {
    return false;
  }
  char line[32] = {};
  const size_t copyLen = static_cast<size_t>(header.payloadLen) < sizeof(line) - 1
                             ? static_cast<size_t>(header.payloadLen)
                             : sizeof(line) - 1;
  readBytesAt(sCaptureRing.head + sizeof(header), line, copyLen);
  line[sizeof(line) - 1] = '\0';
  return isTierATextLine(line);
}

SC_MEM_ATTR bool shouldSampleMidiOutCapture() {
  if (!captureRingPressureHigh()) {
    return false;
  }
  static uint8_t sampleCounter = 0;
  return (++sampleCounter & 7u) != 0;
}

SC_MEM_ATTR bool appendCaptureRecord(CaptureRecordType type, const void* payload, uint16_t payloadLen);

SC_MEM_ATTR bool queueCaptureTextLine(const char* line) {
  if (line == nullptr) {
    return false;
  }
  const size_t len = strnlen(line, kMaxCaptureTextBytes - 1);
  if (len == 0) {
    return false;
  }
  return appendCaptureRecord(CaptureRecordType::Text, line, static_cast<uint16_t>(len + 1));
}

SC_MEM_ATTR void emitCapLineOrSerial(const char* line) {
  if (line == nullptr) {
    return;
  }
  // Never fall back to blocking Serial.println when the ring is full —
  // that path soft-locks the main loop under PLAYING + CAP flood
  // (session_20260719_013003 final hang after f_tel_done).
  (void)queueCaptureTextLine(line);
}

SC_MEM_ATTR void emitCapPrintf(const char* fmt, ...) {
  if (fmt == nullptr) {
    return;
  }
  char line[kMaxCaptureTextBytes];
  va_list args;
  va_start(args, fmt);
  const int len = vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  if (len <= 0) {
    return;
  }
  emitCapLineOrSerial(line);
}

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
  const bool incomingTierC =
      type == CaptureRecordType::Text && isTierCTextLine(static_cast<const char*>(payload));
  while (sCaptureRing.used + total > sCaptureRing.capacity) {
    if (incomingTierC && headRecordIsTierAText()) {
      return false;
    }
    const size_t usedBefore = sCaptureRing.used;
    discardOldestRecord();
    if (sCaptureRing.used == usedBefore) {
      return false;
    }
  }
  writeBytesAt(sCaptureRing.tail, &header, sizeof(header));
  writeBytesAt(sCaptureRing.tail + sizeof(header), payload, payloadLen);
  sCaptureRing.tail = (sCaptureRing.tail + total) % sCaptureRing.capacity;
  sCaptureRing.used += total;
  return true;
}

bool serialWriteRoom(size_t bytesNeeded) {
  // USB CDC Serial.println/printf can block indefinitely when the host TX
  // buffer is full. Under PLAYING + heavy LoadLoopJob CAP traffic that soft-
  // locks the main loop (session_20260719_012717: f_tel_done, no f_flush_done);
  // clock-driven bar LEDs keep moving while 16th LEDs (updateLedsDeferred) stall.
  const int room = Serial.availableForWrite();
  return room < 0 || static_cast<size_t>(room) >= bytesNeeded;
}

void emitOverflowNotice() {
  if (!sCaptureRing.overflowPending) {
    return;
  }
  if (!serialWriteRoom(48)) {
    return;  // keep pending; never block main on overflow notice
  }
  sCaptureRing.overflowPending = false;
  Serial.printf("#CAP,%lu,RING,overflow\r\n", (unsigned long)micros());
}

void flushOneRevtRecord(const PendingRevt& revt) {
  Serial.printf("#CAP,%lu,REVT,%lu,%u,%u\r\n",
                (unsigned long)micros(), (unsigned long)revt.tick, revt.ch, revt.note);
}

void flushOneDiagTraceRecord(const Diagnostics::DiagTraceRecord& record) {
  Serial.printf(
      "#CAP,%lu,DIAG,%u,%u,%u,%u,%u,%u,%u,%lu,%lu,%lu,%lu\r\n",
      (unsigned long)record.micros, static_cast<unsigned>(Diagnostics::kTraceFormatVersion),
      static_cast<unsigned>(record.eventId),
      static_cast<unsigned>(record.context.trackState),
      static_cast<unsigned>(record.context.editSession),
      static_cast<unsigned>(record.context.looperState),
      static_cast<unsigned>(record.context.flags),
      static_cast<unsigned>(record.recordFlags),
      (unsigned long)record.heapFree, (unsigned long)record.heapUsed,
      (unsigned long)record.extmemFree, (unsigned long)record.payload);
}

void flushOneDiagCheckpointRecord(const Diagnostics::DiagTraceRecord& record) {
  Serial.printf(
      "#CAP,%lu,DIAGCHK,%u,%u,%u,%u,%u,%u,%u,%lu,%lu,%lu,%lu\r\n",
      (unsigned long)micros(), static_cast<unsigned>(Diagnostics::kTraceFormatVersion),
      static_cast<unsigned>(record.eventId),
      static_cast<unsigned>(record.context.trackState),
      static_cast<unsigned>(record.context.editSession),
      static_cast<unsigned>(record.context.looperState),
      static_cast<unsigned>(record.context.flags),
      static_cast<unsigned>(record.recordFlags),
      (unsigned long)record.heapFree, (unsigned long)record.heapUsed,
      (unsigned long)record.extmemFree, (unsigned long)record.payload);
}

}  // namespace

bool captureBootGraceActive() {
  if (sCaptureBootUs == 0) {
    return false;
  }
  return (micros() - sCaptureBootUs) < kBootSerialGraceUs;
}

void restartCaptureBootGrace() {
  sCaptureBootUs = micros();
}

SC_MEM_ATTR void initCaptureBuffer() {
  (void)ensureCaptureRingAllocated();
  markCaptureBootUs();
}

SC_MEM_ATTR void appendCaptureTextLine(const char* line) {
  (void)queueCaptureTextLine(line);
}

SC_MEM_ATTR void sessionHeader() {
  initCaptureBuffer();
  emitCapPrintf("#CAP,%lu,HDR,v1", (unsigned long)micros());
}

SC_MEM_ATTR void midiIn(char src, uint8_t type, uint8_t ch, uint8_t d1, uint8_t d2) {
  emitCapPrintf("#CAP,%lu,MI,%c,%u,%u,%u,%u\r\n", (unsigned long)micros(), src, type, ch, d1, d2);
}

SC_MEM_ATTR void midiOut(uint8_t type, uint8_t ch, uint8_t d1, uint8_t d2) {
  if (shouldSampleMidiOutCapture()) {
    return;
  }
  emitCapPrintf("#CAP,%lu,MO,%u,%u,%u,%u", (unsigned long)micros(), type, ch, d1, d2);
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
  emitCapPrintf("#CAP,%lu,LED,%d,%u,%u", (unsigned long)micros(), on ? 1 : 0, note, vel);
}

SC_MEM_ATTR void gesture(uint8_t channel0, uint8_t note, int pressType) {
  emitCapPrintf("#CAP,%lu,GS,%u,%u,%d\r\n", (unsigned long)micros(), channel0, note, pressType);
}

SC_MEM_ATTR void stateTransition(const char* component, const char* from, const char* to) {
  emitCapPrintf("#CAP,%lu,ST,%s,%s,%s\r\n", (unsigned long)micros(), component, from, to);
}

SC_MEM_ATTR void bpm(float raw, float smoothed) {
  emitCapPrintf("#CAP,%lu,BPM,%.3f,%.3f\r\n", (unsigned long)micros(), (double)raw, (double)smoothed);
}

SC_MEM_ATTR void clockSource(const char* from, const char* to) {
  emitCapPrintf("#CAP,%lu,CS,%s,%s\r\n", (unsigned long)micros(), from, to);
}

SC_MEM_ATTR void recStart(uint8_t slot, uint32_t tick) {
  emitCapPrintf("#CAP,%lu,RECA,%u,%lu\r\n", (unsigned long)micros(), slot, (unsigned long)tick);
}

SC_MEM_ATTR void recStop(const char* kind, uint8_t slot, uint32_t tick, uint32_t startTick,
                         uint32_t rawLength, uint32_t finalLength, bool align) {
  emitCapPrintf("#CAP,%lu,RECS,%s,%u,%lu,%lu,%lu,%lu,%d\r\n", (unsigned long)micros(), kind, slot,
                (unsigned long)tick, (unsigned long)startTick, (unsigned long)rawLength,
                (unsigned long)finalLength, align ? 1 : 0);
}

SC_MEM_ATTR void recStopStage(const char* stage, uint32_t elapsedUs, uint32_t durationUs,
                              uint32_t heapBefore, uint32_t heapAfter, size_t eventCount,
                              size_t chunkRefCount, const char* outcome) {
  emitCapPrintf("#CAP,%lu,RECS,stage,%s,%lu,%lu,%lu,%lu,%lu,%lu,%s\r\n", (unsigned long)micros(),
                stage, (unsigned long)elapsedUs, (unsigned long)durationUs,
                (unsigned long)heapBefore, (unsigned long)heapAfter, (unsigned long)eventCount,
                (unsigned long)chunkRefCount, outcome);
}

SC_MEM_ATTR void architectureCounter(const char* name, uint32_t value) {
  emitCapPrintf("#CAP,%lu,DIAG,counter,%s,%lu\r\n", (unsigned long)micros(), name,
                (unsigned long)value);
}

SC_MEM_ATTR void memoryPressureTransition(const char* transitionLabel, uint32_t heapFreeBytes,
                                          uint16_t chunksFree, uint16_t persistQueueDepth) {
  emitCapPrintf("#CAP,%lu,DIAG,pressure,%s,%lu,%u,%u\r\n", (unsigned long)micros(),
                transitionLabel, (unsigned long)heapFreeBytes, (unsigned)chunksFree,
                (unsigned)persistQueueDepth);
}

SC_MEM_ATTR void architectureTiming(const char* name, uint32_t sumMicros, uint32_t sampleCount) {
  emitCapPrintf("#CAP,%lu,DIAG,timing,%s,%lu,%lu\r\n", (unsigned long)micros(), name,
                (unsigned long)sumMicros, (unsigned long)sampleCount);
}

SC_MEM_ATTR void overdubStartStage(const char* stage, uint32_t durationUs, uint32_t heapBefore,
                                   uint32_t heapAfter, const char* outcome) {
  emitCapPrintf("#CAP,%lu,ODUB,stage,%s,%lu,%lu,%lu,%s\r\n", (unsigned long)micros(), stage,
                (unsigned long)durationUs, (unsigned long)heapBefore, (unsigned long)heapAfter,
                outcome);
}

SC_MEM_ATTR void overdubStopStage(const char* stage, uint32_t elapsedUs, uint32_t durationUs,
                                  uint32_t heapBefore, uint32_t heapAfter, size_t eventCount,
                                  size_t chunkRefCount, const char* outcome) {
  emitCapPrintf("#CAP,%lu,ODUB,stop,%s,%lu,%lu,%lu,%lu,%lu,%lu,%s\r\n", (unsigned long)micros(),
                stage, (unsigned long)elapsedUs, (unsigned long)durationUs,
                (unsigned long)heapBefore, (unsigned long)heapAfter, (unsigned long)eventCount,
                (unsigned long)chunkRefCount, outcome);
}

SC_MEM_ATTR void persistence(const char* stage, uint32_t durationUs, uint32_t heapBefore,
                             uint32_t heapAfter, const char* outcome) {
  emitCapPrintf("#CAP,%lu,PERS,%s,%lu,%lu,%lu,%s\r\n", (unsigned long)micros(), stage,
                (unsigned long)durationUs, (unsigned long)heapBefore, (unsigned long)heapAfter,
                outcome);
}

SC_MEM_ATTR void persistenceDiagnostic(uint16_t freeChunks, uint16_t usedChunks, uint16_t reserve,
                                       uint16_t queueDepth, uint16_t writingChunks,
                                       uint32_t transportBlockCount, uint32_t heapFloorBlockCount,
                                       uint32_t budgetBlockCount, uint32_t sliceDoneCount,
                                       uint32_t peakWriterLatencyUs, uint32_t oldestDirtyAgeMs,
                                       uint32_t maxDeferredBacklog, uint8_t savePending,
                                       uint8_t saveInProgress, uint8_t captureActive) {
  emitCapPrintf(
      "#CAP,%lu,PERS,diag,%u,%u,%u,%u,%u,%lu,%lu,%lu,%lu,%lu,%lu,%u,%u,%u\r\n",
      (unsigned long)micros(), freeChunks, usedChunks, reserve, queueDepth, writingChunks,
      (unsigned long)transportBlockCount, (unsigned long)heapFloorBlockCount,
      (unsigned long)budgetBlockCount, (unsigned long)sliceDoneCount,
      (unsigned long)peakWriterLatencyUs, (unsigned long)oldestDirtyAgeMs,
      (unsigned long)maxDeferredBacklog, savePending, saveInProgress, captureActive);
}

SC_MEM_ATTR void persistencePoolPressure(uint16_t freeChunks, uint16_t reserve,
                                         uint16_t usedChunks) {
  emitCapPrintf("#CAP,%lu,PERS,pressure,%u,%u,%u\r\n", (unsigned long)micros(), freeChunks,
                reserve, usedChunks);
}

SC_MEM_ATTR void saveDisplayPhase(const char* phase, uint8_t rotateStep) {
  emitCapPrintf("#CAP,%lu,SAVE,%s,%u\r\n", (unsigned long)micros(), phase, rotateStep);
}

SC_MEM_ATTR void loadSaveMode(uint8_t active) {
  emitCapPrintf("#CAP,%lu,LDSV,%u\r\n", (unsigned long)micros(), active);
}

SC_MEM_ATTR void overlayListSelection(uint8_t mode, uint8_t row) {
  emitCapPrintf("#CAP,%lu,OVLY,sel,%u,%u\r\n", (unsigned long)micros(), mode, row);
}

SC_MEM_ATTR void overlayRowConfirm(uint8_t mode, uint8_t row) {
  emitCapPrintf("#CAP,%lu,OVLY,confirm,%u,%u\r\n", (unsigned long)micros(), mode, row);
}

SC_MEM_ATTR void queueStoredNoteOn(uint32_t tick, uint8_t ch, uint8_t note) {
  const PendingRevt revt{tick, ch, note};
  (void)appendCaptureRecord(CaptureRecordType::Revt, &revt, sizeof(revt));
}

SC_MEM_ATTR void recStoredNoteOn(uint32_t tick, uint8_t ch, uint8_t note) {
  queueStoredNoteOn(tick, ch, note);
}

SC_MEM_ATTR void storedNoteEvent(char kind, uint32_t tick, uint8_t ch, uint8_t note) {
  emitCapPrintf("#CAP,%lu,SEVT,%c,%lu,%u,%u\r\n", (unsigned long)micros(), kind,
                (unsigned long)tick, ch, note);
}

SC_MEM_ATTR void captureCoordinate(uint32_t absTick, uint32_t storageTick, uint32_t projPhase,
                                   uint32_t displayPhase, uint32_t startLoopTick,
                                   int32_t projectionCycleStartTick, uint32_t loopStartTick,
                                   uint8_t ch, uint8_t note) {
  emitCapPrintf(
      "#CAP,%lu,COORD,abs,%u,storage,%u,proj,%u,display,%u,startLoop,%u,projStart,%d,loopStart,%u,ch,%u,note,%u\r\n",
      (unsigned long)micros(), absTick, storageTick, projPhase, displayPhase, startLoopTick,
      (int)projectionCycleStartTick, loopStartTick, ch, note);
}

SC_MEM_ATTR void displaySnapshot(uint8_t slot, const char* trackState, uint32_t loopLen,
                                 size_t sourceEventCount, size_t visualNotes, size_t frameNotes,
                                 size_t bufferEvents, int published) {
  emitCapPrintf("#CAP,%lu,DISP,%u,%s,%lu,%zu,%zu,%zu,%zu,%d", (unsigned long)micros(), slot,
                trackState, (unsigned long)loopLen, sourceEventCount, visualNotes, frameNotes,
                bufferEvents, published);
}

SC_MEM_ATTR void displaySnapshotWindow(uint8_t slot, const char* trackState, uint32_t loopLen,
                                       size_t sourceEventCount, size_t visualNotes, size_t frameNotes,
                                       size_t bufferEvents, int published, uint32_t windowStartTick,
                                       uint8_t windowBars, size_t windowNoteCount) {
  emitCapPrintf("#CAP,%lu,DISP,%u,%s,%lu,%zu,%zu,%zu,%zu,%d,%lu,%u,%zu",
                (unsigned long)micros(), slot, trackState, (unsigned long)loopLen, sourceEventCount,
                visualNotes, frameNotes, bufferEvents, published, (unsigned long)windowStartTick,
                windowBars, windowNoteCount);
}

SC_MEM_ATTR void displayNoteInfo(uint8_t pitch, uint32_t storageStart, uint32_t displayStart,
                                 uint32_t length, int selectedIdx) {
  emitCapPrintf("#CAP,%lu,DNTE,%u,%lu,%lu,%lu,%d\r\n", (unsigned long)micros(), pitch,
                (unsigned long)storageStart, (unsigned long)displayStart, (unsigned long)length,
                selectedIdx);
}

SC_MEM_ATTR void displayFrame(uint32_t frameNotes, uint32_t elapsedUs, uint32_t frameIndex) {
  emitCapPrintf("#CAP,%lu,DFRAME,%lu,%lu,%lu", (unsigned long)micros(), (unsigned long)frameNotes,
                (unsigned long)elapsedUs, (unsigned long)frameIndex);
}

SC_MEM_ATTR void playbackFrame(uint8_t slot, uint32_t currentTick, uint32_t tickInLoop,
                               uint32_t prevTickInLoop, int32_t projectionCycleStartTick,
                               uint32_t loopStartTick, uint32_t loopLength, uint16_t indexBefore,
                               uint16_t indexAfter, uint16_t orderSize, uint16_t sent,
                               uint8_t atLoopStart, uint8_t wrapDetected, uint8_t wasStale,
                               uint32_t playbackRevision, uint32_t playbackGeneration) {
  emitCapPrintf("#CAP,%lu,PBF,%u,%lu,%lu,%lu,%ld,%lu,%lu,%u,%u,%u,%u,%u,%u,%u,%lu,%lu\r\n",
                (unsigned long)micros(), slot, (unsigned long)currentTick,
                (unsigned long)tickInLoop, (unsigned long)prevTickInLoop,
                (long)projectionCycleStartTick, (unsigned long)loopStartTick,
                (unsigned long)loopLength, indexBefore, indexAfter, orderSize, sent, atLoopStart,
                wrapDetected, wasStale, (unsigned long)playbackRevision,
                (unsigned long)playbackGeneration);
}

SC_MEM_ATTR void storedWrapPair(uint32_t onTick, uint32_t offTick, uint8_t ch, uint8_t note) {
  emitCapPrintf("#CAP,%lu,WRAP,%lu,%lu,%u,%u\r\n", (unsigned long)micros(), (unsigned long)onTick,
                (unsigned long)offTick, ch, note);
}

SC_MEM_ATTR void captureCleanup(const char* phase, const char* kind, uint32_t count) {
  emitCapPrintf("#CAP,%lu,CLN,%s,%s,%lu\r\n", (unsigned long)micros(), phase, kind,
                (unsigned long)count);
}

void flushCaptureBuffer(size_t maxRecords) {
  // Never emit overflow via USB Serial under timing-critical budgets — 013530 logged
  // 813× RING,overflow during PLAYING (emitOverflowNotice before tc_skip return).
  if (maxRecords > 8) {
    emitOverflowNotice();
  }
  if (sCaptureRing.data == nullptr || sCaptureRing.used == 0) {
    return;
  }
  // Do not inflate a timing-critical budget (e.g. PLAYING flush of 8) to 256
  // records of blocking Serial I/O. Overflow drains across later frames instead.
  if (sCaptureRing.overflowPending && maxRecords >= 64 && maxRecords < 256) {
    maxRecords = 256;
  }

  // Timing-critical budget (PLAYING flush of 8): never call USB Serial.
  // availableForWrite does not prevent usb_serial_write from blocking in yield()
  // (013003). Pressure-gated skip never fired in 013315 (flush_tc_skip=0) so the
  // next frame still Serial.println'd and soft-locked after f_flush_leave.
  // Drop non-Tier-A head records only; leave Tier-A for idle (maxRecords>=64) drain.
  if (maxRecords <= 8) {
    size_t dropped = 0;
    while (dropped < maxRecords && sCaptureRing.used >= sizeof(CaptureRecordHeader)) {
      if (headRecordIsTierAText()) {
        break;
      }
      const size_t usedBefore = sCaptureRing.used;
      discardOldestRecord();
      if (sCaptureRing.used >= usedBefore) {
        break;
      }
      ++dropped;
    }
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

    // Estimate TX bytes for this record; abort flush if USB has no room.
    const size_t writeBytes =
        (header.type == static_cast<uint8_t>(CaptureRecordType::Text))
            ? static_cast<size_t>(header.payloadLen) + 2
            : 96;
    if (!serialWriteRoom(writeBytes)) {
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
      const size_t copyLen = static_cast<size_t>(header.payloadLen) < sizeof(line)
                                 ? static_cast<size_t>(header.payloadLen)
                                 : sizeof(line);
      readBytesAt(sCaptureRing.head + sizeof(header), line, copyLen);
      line[sizeof(line) - 1] = '\0';
      Serial.println(line);
    } else if (header.type == static_cast<uint8_t>(CaptureRecordType::DiagTrace)) {
      if (header.payloadLen == sizeof(Diagnostics::DiagTraceRecord)) {
        Diagnostics::DiagTraceRecord record{};
        readBytesAt(sCaptureRing.head + sizeof(header), &record, sizeof(record));
        flushOneDiagTraceRecord(record);
      }
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
    const size_t usedBefore = sCaptureRing.used;
    flushCaptureBuffer(sCaptureRing.overflowPending ? 256 : 64);
    if (sCaptureRing.used >= usedBefore) {
      break;  // USB backpressure or empty progress — do not spin forever
    }
  }
}

SC_MEM_ATTR void update(uint32_t currentTick, uint32_t ticksPerBar) {
  static uint32_t lastBar = 0xFFFFFFFF;
  const uint32_t bar = currentTick / ticksPerBar;
  if (bar != lastBar) {
    lastBar = bar;
    emitCapPrintf("#CAP,%lu,BAR,%lu,%lu", (unsigned long)micros(), (unsigned long)currentTick,
                  (unsigned long)bar);
  }
}

SC_MEM_ATTR bool appendDiagTraceRecord(const void* record, uint16_t recordSize) {
  if (record == nullptr || recordSize == 0) {
    return false;
  }
  return appendCaptureRecord(CaptureRecordType::DiagTrace, record, recordSize);
}

void emitDiagCheckpointLine(const Diagnostics::DiagTraceRecord& record) {
  flushOneDiagCheckpointRecord(record);
}

}  // namespace DebugSessionCapture

#endif  // SESSION_CAPTURE
