//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/DebugSessionCapture.h"

#ifdef SESSION_CAPTURE

#include <Arduino.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "Utils/CaptureLineTier.h"
#include "Utils/DiagnosticsTypes.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/RuntimeTimingTelemetry.h"
#include "LoopEventStore.h"
#include "StorageManager.h"

#if defined(__IMXRT1062__)
extern "C" void* extmem_malloc(size_t size);
extern "C" void extmem_free(void* ptr);
#define SC_FMT(literal) PSTR(literal)
#else
#define SC_FMT(literal) literal
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

// PLAYING/OVERDUBBING never flushes USB serial: capture must retain the full
// active-transport evidence burst until STOPPED.  The canonical two-overdub
// HITL fixture kept just over the former 96 KiB ring before its stop drain;
// reserve this fixed PSRAM budget rather than weakening Tier-A evidence.
// Raised 512 KiB -> 1 MiB (2026-09-17) after `captures/successor-wrap-46`/`-47` both aborted on
// `RING,overflow`: at 512 KiB the eviction shed the `snapshot_unit` publication rows a playback
// regression needs, so the run could not be explained from serial at all. The ring lives in
// PSRAM (see `ensureCaptureRingAllocated`), so the extra budget costs no RAM1/RAM2.
constexpr size_t kCaptureRingBytes = 1024 * 1024;
constexpr size_t kCaptureRingPressureBytes = (kCaptureRingBytes * 3) / 4;
constexpr size_t kMaxCaptureTextBytes = 192;

struct CaptureRing {
  uint8_t* data = nullptr;
  size_t capacity = 0;
  size_t head = 0;
  size_t tail = 0;
  size_t used = 0;
  // Two distinct losses used to share the `overflowPending` flag, and because the host treats
  // `RING,overflow` as a session-invalidating event, a long run aborted for a loss the ring is
  // designed to absorb. They are now separate:
  //
  //  * `tierADegradedPending` -- Tier-A *text* records were displaced by newer Tier-A records.
  //    The ring's documented policy is that Tier-A may only be displaced by Tier-A and that this
  //    keeps the newest windows, so the session stays usable; `RING,tier_a_evicted` reports it
  //    as a degradation with counts instead of invalidating the capture. `captures/rcemem5-lazy-pool`
  //    hit this at ~150 s of a 24-bar two-overdub run, where the 1 MiB bound is simply reachable.
  //  * `overflowPending` -- a *critical* record was lost (critical may only be displaced by
  //    critical, which cannot happen), so the capture really is incomplete and stays fatal.
  //
  // Timing-critical shedding is the third and least severe kind: playback deliberately discards
  // only non-Tier-A diagnostics instead of doing USB I/O, and that stays observable without
  // being reported as either kind of loss.
  bool overflowPending = false;
  bool tierADegradedPending = false;
  // `RING,overflow` / `RING,tier_a_evicted` are emitted only at a later safe USB flush. Preserve
  // the first actual eviction timestamp so a host report does not mistake that notification point
  // for the loss point.
  uint32_t overflowFirstEvictedUs = 0;
  uint32_t tierAFirstEvictedUs = 0;
  uint32_t tierAEvictedCount = 0;
  uint32_t timingShedPending = 0;

  // S1.3 save/restore-survival: bounded identity-path counters. They separate "the branch was never
  // reached" from "capture disabled", "enqueue rejected", "evicted" and "delivered but not parsed",
  // which linked call sites alone cannot. Fixed size, bumped once per event, never printed on a
  // timing-critical path (read back through `!CAP_IDENTITY_STATS` while stopped).
  uint32_t identityEmitEntered = 0;
  uint32_t identityEmitEnqueued = 0;
  uint32_t identityEmitRejected = 0;
  uint32_t identityTransmitted = 0;
  uint32_t identityEvicted = 0;
  // The last identity the emitter was handed (track, start slot, stop slot, commit slot, persist
  // track, persist slot, event char, take id) plus the enqueue outcome.
  uint8_t identityLastTrack = 0xFF;
  uint8_t identityLastStartSlot = 0xFF;
  uint8_t identityLastStopSlot = 0xFF;
  uint8_t identityLastCommitSlot = 0xFF;
  uint8_t identityLastPersistTrack = 0xFF;
  uint8_t identityLastPersistSlot = 0xFF;
  char identityLastEvent = '-';
  uint8_t identityLastEnqueued = 0;
  uint32_t identityLastTakeId = 0;
};

CaptureRing sCaptureRing;

bool readHeaderAt(size_t index, CaptureRecordHeader& headerOut);
void readBytesAt(size_t index, void* dest, size_t len);

SC_MEM_ATTR bool isTierATextLine(const char* line) {
  return CaptureLineTier::isTierALine(line);
}

SC_MEM_ATTR bool isCriticalTextLine(const char* line) {
  return CaptureLineTier::isCriticalLine(line);
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
  // Must hold "#CAP,<micros>,DIAG,clockrate," — the longest Tier-A prefix — for a
  // micros field that keeps growing over a long session.
  char line[48] = {};
  const size_t copyLen = static_cast<size_t>(header.payloadLen) < sizeof(line) - 1
                             ? static_cast<size_t>(header.payloadLen)
                             : sizeof(line) - 1;
  readBytesAt(sCaptureRing.head + sizeof(header), line, copyLen);
  line[sizeof(line) - 1] = '\0';
  return isTierATextLine(line);
}

SC_MEM_ATTR bool headRecordIsCriticalText() {
  if (sCaptureRing.used < sizeof(CaptureRecordHeader)) return false;
  CaptureRecordHeader header{};
  if (!readHeaderAt(sCaptureRing.head, header) ||
      header.type != static_cast<uint8_t>(CaptureRecordType::Text) || header.payloadLen == 0) {
    return false;
  }
  char line[48] = {};
  const size_t copyLen = static_cast<size_t>(header.payloadLen) < sizeof(line) - 1
                             ? static_cast<size_t>(header.payloadLen)
                             : sizeof(line) - 1;
  readBytesAt(sCaptureRing.head + sizeof(header), line, copyLen);
  line[sizeof(line) - 1] = '\0';
  return isCriticalTextLine(line);
}

/// Whether the ring head is a slot-identity (`RID,`) text record, so an eviction of one can be
/// counted. Same shape as the tier/critical head probes; header-only classification, no allocation.
SC_MEM_ATTR bool headRecordIsIdentityText() {
  if (sCaptureRing.used < sizeof(CaptureRecordHeader)) {
    return false;
  }
  CaptureRecordHeader header{};
  if (!readHeaderAt(sCaptureRing.head, header) ||
      header.type != static_cast<uint8_t>(CaptureRecordType::Text) || header.payloadLen == 0) {
    return false;
  }
  char line[48] = {};
  const size_t copyLen = static_cast<size_t>(header.payloadLen) < sizeof(line) - 1
                             ? static_cast<size_t>(header.payloadLen)
                             : sizeof(line) - 1;
  readBytesAt(sCaptureRing.head + sizeof(header), line, copyLen);
  line[sizeof(line) - 1] = '\0';
  return CaptureLineTier::tagStartsWith(line, "RID,");
}

SC_MEM_ATTR bool shouldSampleMidiOutCapture() {  if (!captureRingPressureHigh()) {
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
#if !SESSION_CAPTURE_LED
  if (CaptureLineTier::tagStartsWith(line, "LED,")) {
    return false;
  }
#endif
#if !SESSION_CAPTURE_MO
  if (CaptureLineTier::tagStartsWith(line, "MO,")) {
    return false;
  }
#endif
#if !SESSION_CAPTURE_DIAG
  if (CaptureLineTier::tagStartsWith(line, "DIAG,")) {
    return false;
  }
#endif
#if !SESSION_CAPTURE_SNAPSHOT_IDLE
  if (CaptureLineTier::tagStartsWith(line, "DIAG,snapshot_idle,")) {
    return false;
  }
#endif
#if !SESSION_CAPTURE_BPM
  if (CaptureLineTier::tagStartsWith(line, "BPM,")) {
    return false;
  }
#endif
#if !SESSION_CAPTURE_DRAW_SRC
  if (CaptureLineTier::tagStartsWith(line, "DIAG,draw_src,")) {
    return false;
  }
#endif
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

void noteTimingShed() {
  if (sCaptureRing.timingShedPending != UINT32_MAX) {
    ++sCaptureRing.timingShedPending;
  }
}

/// The kind comes from `CaptureLineTier::evictionKindFor`, which the host tests pin directly
/// (this translation unit needs Arduino, so the policy lives in the header).
void discardOldestRecord(CaptureLineTier::EvictionKind kind) {
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
  switch (kind) {
    case CaptureLineTier::EvictionKind::TimingShed:
      noteTimingShed();
      break;
    case CaptureLineTier::EvictionKind::TierAText:
      sCaptureRing.tierADegradedPending = true;
      ++sCaptureRing.tierAEvictedCount;
      if (sCaptureRing.tierAFirstEvictedUs == 0) {
        sCaptureRing.tierAFirstEvictedUs = micros();
      }
      break;
    case CaptureLineTier::EvictionKind::CriticalText:
      sCaptureRing.overflowPending = true;
      if (sCaptureRing.overflowFirstEvictedUs == 0) {
        sCaptureRing.overflowFirstEvictedUs = micros();
      }
      break;
  }
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
  // Tier-A may only be displaced by Tier-A. Under a timing-critical flush budget the
  // drop-only path in flushCaptureBuffer leaves Tier-A at the head untransmitted, so
  // eviction here was the only way it could leave the ring: 141815 lost every DIAG timing
  // window between 21.9s and 318.6s to Tier-B/C appends across a ~296s record/overdub pass.
  // Allowing Tier-A to evict Tier-A keeps the newest windows and cannot wedge the ring.
  const bool incomingTierA =
      type == CaptureRecordType::Text && isTierATextLine(static_cast<const char*>(payload));
  const bool incomingCritical =
      type == CaptureRecordType::Text && isCriticalTextLine(static_cast<const char*>(payload));
  const bool incomingIdentity =
      type == CaptureRecordType::Text &&
      CaptureLineTier::tagStartsWith(static_cast<const char*>(payload), "RID,");
  if (incomingIdentity) {
    ++sCaptureRing.identityEmitEnqueued;  // provisional; decremented below if the append is refused
  }
  while (sCaptureRing.used + total > sCaptureRing.capacity) {
    const bool headTierA = headRecordIsTierAText();
    const bool headCritical = headRecordIsCriticalText();
    if (!CaptureLineTier::incomingMayDisplace(incomingTierA, incomingCritical, headTierA,
                                              headCritical)) {
      // Preserve an actionable denial over ordinary timing telemetry.
      noteTimingShed();
      if (incomingIdentity) {
        --sCaptureRing.identityEmitEnqueued;
        ++sCaptureRing.identityEmitRejected;
      }
      return false;
    }
    // An identity record at the head is being displaced by this append: count it, so "the row was
    // written and then evicted" is distinguishable from "the row was never written".
    if (headRecordIsIdentityText()) {
      ++sCaptureRing.identityEvicted;
    }
    const size_t usedBefore = sCaptureRing.used;
    // A full ring may replace a non-Tier-A record without invalidating the
    // timing/state trace.  Tier-A text may only be replaced by Tier-A text (that is what keeps
    // the newest timing windows) and is reported as a degradation; only critical text loss
    // invalidates the capture.
    discardOldestRecord(CaptureLineTier::evictionKindFor(headTierA, headCritical));
    if (sCaptureRing.used == usedBefore) {
      if (incomingIdentity) {
        --sCaptureRing.identityEmitEnqueued;
        ++sCaptureRing.identityEmitRejected;
      }
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
  // clock-driven bar LEDs keep moving while 16th LEDs (updateMidiLedsDeferred) stall.
  const int room = Serial.availableForWrite();
  return room < 0 || static_cast<size_t>(room) >= bytesNeeded;
}

void emitOverflowNotice() {
  if (!sCaptureRing.overflowPending) {
    return;
  }
  if (!serialWriteRoom(64)) {
    return;  // keep pending; never block main on overflow notice
  }
  const uint32_t firstEvictedUs = sCaptureRing.overflowFirstEvictedUs;
  sCaptureRing.overflowPending = false;
  sCaptureRing.overflowFirstEvictedUs = 0;
  Serial.printf_P(SC_FMT("#CAP,%lu,RING,overflow,%lu\r\n"),
                  static_cast<unsigned long>(micros()),
                  static_cast<unsigned long>(firstEvictedUs));
}

/// Tier-A text records were displaced by newer Tier-A records. The ring's policy is that this
/// is the expected behaviour of a full ring -- it keeps the newest timing windows -- so the
/// capture stays usable and must not invalidate the session. Report it with counts so a host
/// verdict can be down-weighted rather than discarded, and so the thinning rate is measurable
/// (that rate is the input a Tier-A volume profile would be sized from).
void emitTierADegradedNotice() {
  if (!sCaptureRing.tierADegradedPending) {
    return;
  }
  if (!serialWriteRoom(96)) {
    return;  // keep pending; never block main on the degradation notice
  }
  const uint32_t firstEvictedUs = sCaptureRing.tierAFirstEvictedUs;
  const uint32_t evicted = sCaptureRing.tierAEvictedCount;
  sCaptureRing.tierADegradedPending = false;
  sCaptureRing.tierAFirstEvictedUs = 0;
  sCaptureRing.tierAEvictedCount = 0;
  Serial.printf_P(SC_FMT("#CAP,%lu,RING,tier_a_evicted,%lu,first=%lu\r\n"),
                  static_cast<unsigned long>(micros()),
                  static_cast<unsigned long>(evicted),
                  static_cast<unsigned long>(firstEvictedUs));
}

void emitTimingShedNotice() {
  if (sCaptureRing.timingShedPending == 0) {
    return;
  }
  if (!serialWriteRoom(64)) {
    return;  // keep pending; never block main on the diagnostic notice
  }
  const uint32_t shed = sCaptureRing.timingShedPending;
  sCaptureRing.timingShedPending = 0;
  Serial.printf("#CAP,%lu,RING,shed,%lu\r\n", (unsigned long)micros(),
                (unsigned long)shed);
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

SC_MEM_ATTR void restoreRequest(uint8_t track, uint8_t slot, uint32_t requestGeneration,
                                uint32_t setEpoch, const char* state, const char* wait,
                                const char* terminal) {
  emitCapPrintf("#CAP,%lu,RRQ,%u,%u,%lu,%lu,%s,%s,%s\r\n", (unsigned long)micros(), track, slot,
                (unsigned long)requestGeneration, (unsigned long)setEpoch, state, wait, terminal);
}

// `restored-library-stability` 1.3 — the on-demand library observation's `LIBOBS` record family. A
// distinct family from `DIAG,post_restore_census`: the observation is never emitted as, or by, the
// settled census checkpoint.
//
// **Compact on purpose.** The capture text cap is `kMaxCaptureTextBytes = 192`; the first version used
// full names and was silently truncated on device (`LIBOBS,slot,...` lost everything after
// `passes`), so the framing fields read as zero. Enum fields are wire **codes**
// (`LibraryObservation::causeCode` and friends) and keys are one/two characters; the host parser owns
// the reverse mapping.
SC_MEM_ATTR void libraryObservationBegin(uint32_t windowStartUs, uint32_t setEpoch, uint16_t slotCount) {
  emitCapPrintf(SC_FMT("#CAP,%lu,LIBOBS,B,a=%lu,e=%lu,s=%u\r\n"), (unsigned long)micros(),
                (unsigned long)windowStartUs, (unsigned long)setEpoch,
                static_cast<unsigned>(slotCount));
}

SC_MEM_ATTR void libraryObservationSlot(uint8_t track, uint8_t slot, uint8_t stateMask, char cause,
                                        uint8_t payload, uint8_t offered, uint8_t request, uint8_t wait,
                                        uint8_t terminal, uint32_t requestGeneration, uint32_t setEpoch,
                                        uint8_t hasCommittedContent, uint8_t hasPassContent,
                                        uint8_t hasRootContent, uint8_t rootIsCurrent,
                                        uint16_t committedPassCount,
                                        uint32_t highestCommittedPassId, uint32_t loopLengthTicks,
                                        uint32_t committedEventCount, uint8_t requiredForInitialPlayback,
                                        uint8_t committedTerminalInWindow, uint8_t invalidated) {
  emitCapPrintf(SC_FMT("#CAP,%lu,LIBOBS,S,t=%u,s=%u,st=%u,c=%c,p=%u,o=%u,q=%u,w=%u,m=%u,g=%lu,"
                       "e=%lu,k=%u,kp=%u,kr=%u,kc=%u,a=%u,h=%lu,l=%lu,v=%lu,n=%u,i=%u,x=%u\r\n"),
                (unsigned long)micros(), static_cast<unsigned>(track), static_cast<unsigned>(slot),
                static_cast<unsigned>(stateMask), cause, static_cast<unsigned>(payload),
                static_cast<unsigned>(offered), static_cast<unsigned>(request),
                static_cast<unsigned>(wait), static_cast<unsigned>(terminal),
                (unsigned long)requestGeneration, (unsigned long)setEpoch,
                static_cast<unsigned>(hasCommittedContent), static_cast<unsigned>(hasPassContent),
                static_cast<unsigned>(hasRootContent), static_cast<unsigned>(rootIsCurrent),
                static_cast<unsigned>(committedPassCount),
                (unsigned long)highestCommittedPassId, (unsigned long)loopLengthTicks,
                (unsigned long)committedEventCount,
                static_cast<unsigned>(requiredForInitialPlayback),
                static_cast<unsigned>(committedTerminalInWindow), static_cast<unsigned>(invalidated));
}

SC_MEM_ATTR void libraryObservationEnd(uint32_t windowStartUs, uint32_t windowEndUs, uint16_t turns,
                                       uint16_t restarts, uint16_t slotsObserved,
                                       uint16_t payloadInventory, uint16_t initialPlaybackRequired,
                                       uint16_t observedRestoreCompletions,
                                       uint16_t committedAtCheckpoint,
                                       uint16_t passBackedAtCheckpoint,
                                       uint16_t rootBackedAtCheckpoint, uint8_t admissionComplete,
                                       uint8_t everyInitialPlaybackSlotObserved,
                                       uint8_t consistentView, uint8_t complete) {
  emitCapPrintf(SC_FMT("#CAP,%lu,LIBOBS,E,a=%lu,b=%lu,t=%u,r=%u,s=%u,p=%u,i=%u,c=%u,m=%u,P=%u,R=%u,"
                       "n=%u,e=%u,v=%u,x=%u\r\n"),
                (unsigned long)micros(), (unsigned long)windowStartUs, (unsigned long)windowEndUs,
                static_cast<unsigned>(turns), static_cast<unsigned>(restarts),
                static_cast<unsigned>(slotsObserved), static_cast<unsigned>(payloadInventory),
                static_cast<unsigned>(initialPlaybackRequired),
                static_cast<unsigned>(observedRestoreCompletions),
                static_cast<unsigned>(committedAtCheckpoint),
                static_cast<unsigned>(passBackedAtCheckpoint),
                static_cast<unsigned>(rootBackedAtCheckpoint),
                static_cast<unsigned>(admissionComplete),
                static_cast<unsigned>(everyInitialPlaybackSlotObserved),
                static_cast<unsigned>(consistentView), static_cast<unsigned>(complete));
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

// S1.3 save/restore-survival: the record-stop slot-identity chain. `ev=start` fires once at record
// start and `ev=admit` once at the persistence admission; both carry the same `take` id, so a host
// can assert that the slot the take started on is the slot that was persisted. `commit` is the slot
// passed to `finalizeCommitSideEffects`, `pt/ps` the admitted persistence key, `chg` whether the
// slot changed between start and admission, `r` the commit reason and `w` the work-item kind.
SC_MEM_ATTR void recSlotIdentity(const char* event, uint32_t takeId, uint8_t trackIndex,
                                 uint8_t startSlot, uint8_t stopSlot, uint8_t commitSlot,
                                 uint8_t persistTrack, uint8_t persistSlot, char reason, char work,
                                 uint8_t slotChanged) {
  // S1.3 save/restore-survival: count entry and outcome here, where the branch is actually reached,
  // so "the emit never ran" is distinguishable from "the row was enqueued and then lost". Fixed-size
  // bumps only; no extra output on this path.
  ++sCaptureRing.identityEmitEntered;
  sCaptureRing.identityLastTrack = trackIndex;
  sCaptureRing.identityLastStartSlot = startSlot;
  sCaptureRing.identityLastStopSlot = stopSlot;
  sCaptureRing.identityLastCommitSlot = commitSlot;
  sCaptureRing.identityLastPersistTrack = persistTrack;
  sCaptureRing.identityLastPersistSlot = persistSlot;
  sCaptureRing.identityLastEvent = (event != nullptr && event[0] != '\0') ? event[0] : '-';
  sCaptureRing.identityLastTakeId = takeId;
  const uint32_t enqueuedBefore = sCaptureRing.identityEmitEnqueued;
  const uint32_t rejectedBefore = sCaptureRing.identityEmitRejected;
  emitCapPrintf(
      SC_FMT("#CAP,%lu,RID,%s,take=%lu,t=%u,ss=%u,sp=%u,cs=%u,pt=%u,ps=%u,r=%c,w=%c,chg=%u\r\n"),
      (unsigned long)micros(), event, (unsigned long)takeId, static_cast<unsigned>(trackIndex),
      static_cast<unsigned>(startSlot), static_cast<unsigned>(stopSlot),
      static_cast<unsigned>(commitSlot), static_cast<unsigned>(persistTrack),
      static_cast<unsigned>(persistSlot), reason, work, static_cast<unsigned>(slotChanged));
  sCaptureRing.identityLastEnqueued =
      (sCaptureRing.identityEmitEnqueued > enqueuedBefore &&
       sCaptureRing.identityEmitRejected == rejectedBefore)
          ? 1u
          : 0u;
}

// S1.3 save/restore-survival: the read-back for the bounded identity-path counters. One fixed-size
// record, emitted only from the stopped-only HITL command, so the record/persist path never prints.
SC_MEM_ATTR void emitIdentityPathStats() {
  // Emitted **directly**, not through the capture ring: the ring may reject or displace the row (that is
  // exactly the visibility problem these counters exist to diagnose), and this path is stopped-only,
  // host-requested and never on a timing-critical budget, so blocking USB output is safe here.
  Serial.printf(
      SC_FMT("#CAP,%lu,RID,stats,entered=%lu,enq=%lu,rej=%lu,tx=%lu,evict=%lu,lastEnq=%u,"
             "take=%lu,t=%u,ss=%u,sp=%u,cs=%u,pt=%u,ps=%u,ev=%c\r\n"),
      (unsigned long)micros(), (unsigned long)sCaptureRing.identityEmitEntered,
      (unsigned long)sCaptureRing.identityEmitEnqueued, (unsigned long)sCaptureRing.identityEmitRejected,
      (unsigned long)sCaptureRing.identityTransmitted, (unsigned long)sCaptureRing.identityEvicted,
      static_cast<unsigned>(sCaptureRing.identityLastEnqueued),
      (unsigned long)sCaptureRing.identityLastTakeId,
      static_cast<unsigned>(sCaptureRing.identityLastTrack),
      static_cast<unsigned>(sCaptureRing.identityLastStartSlot),
      static_cast<unsigned>(sCaptureRing.identityLastStopSlot),
      static_cast<unsigned>(sCaptureRing.identityLastCommitSlot),
      static_cast<unsigned>(sCaptureRing.identityLastPersistTrack),
      static_cast<unsigned>(sCaptureRing.identityLastPersistSlot),
      sCaptureRing.identityLastEvent);
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

SC_MEM_ATTR void captureAppendDeny(const char* reason, uint16_t freeChunks, uint16_t usedChunks,
                                   const char* pressure, uint8_t ch, uint8_t note, uint32_t tick,
                                   uint8_t pendingPass) {
  emitCapPrintf("#CAP,%lu,append,deny,%s,%u,%u,%s,%u,%u,%lu,%u\r\n", (unsigned long)micros(),
                reason, (unsigned)freeChunks, (unsigned)usedChunks, pressure, (unsigned)ch,
                (unsigned)note, (unsigned long)tick, (unsigned)pendingPass);
}

SC_MEM_ATTR void passReclaim(uint16_t chunksFreeBefore, uint16_t chunksFreeAfter,
                             uint16_t passesReclaimed, uint16_t chunksReleased,
                             uint32_t durationUs, const char* pressure, uint8_t transport,
                             uint16_t capturePins, uint16_t editPins, uint16_t slotsWithPins) {
  // S1.4's pinned-side fields ride this line rather than a second `printf`: a dedicated census line
  // cost 192 B of ITCM padding (1,032 -> 840, the floor) and `noinline`+FLASHMEM pushed it to 824,
  // below the floor, where the RAM1 budget check failed. Extending an existing emitter's argument
  // list is the cheap route the change recorded.
  emitCapPrintf("#CAP,%lu,DIAG,reclaim,%u,%u,%u,%u,%lu,%s,%u,%u,%u,%u\r\n", (unsigned long)micros(),
                (unsigned)chunksFreeBefore, (unsigned)chunksFreeAfter, (unsigned)passesReclaimed,
                (unsigned)chunksReleased, (unsigned long)durationUs, pressure,
                (unsigned)transport, (unsigned)capturePins, (unsigned)editPins,
                (unsigned)slotsWithPins);
}

SC_MEM_ATTR void architectureTiming(const char* name, uint32_t sumMicros, uint32_t sampleCount) {
  emitCapPrintf("#CAP,%lu,DIAG,timing,%s,%lu,%lu\r\n", (unsigned long)micros(), name,
                (unsigned long)sumMicros, (unsigned long)sampleCount);
}

SC_MEM_ATTR void architectureTimingMax(const char* name, uint32_t maxMicros) {
  emitCapPrintf("#CAP,%lu,DIAG,timing_max,%s,%lu\r\n", (unsigned long)micros(), name,
                (unsigned long)maxMicros);
}

SC_MEM_ATTR void runtimeTimingTelemetry(const char* tag, uint32_t maxUs, uint32_t overCount) {
  emitCapPrintf("#CAP,%lu,DIAG,%s,%lu,%lu\r\n", (unsigned long)micros(), tag,
                (unsigned long)maxUs, (unsigned long)overCount);
}

SC_MEM_ATTR void droidFeedbackQueue(uint16_t depth, uint32_t oldestAgeUs,
                                    uint32_t overflowCount) {
#if SESSION_CAPTURE_DROID_QUEUE
  emitCapPrintf("#CAP,%lu,DIAG,droid_queue,%u,%lu,%lu\r\n", (unsigned long)micros(),
                static_cast<unsigned>(depth), (unsigned long)oldestAgeUs,
                (unsigned long)overflowCount);
#else
  // Acceptance build (`teensy41-capture-serial-no-grain`): the family is dropped at compile time
  // so the capture ring keeps the snapshot-publication and transition rows. The queue counters and
  // the overflow `logger.log` warning are unaffected.
  (void)depth;
  (void)oldestAgeUs;
  (void)overflowCount;
#endif
}

SC_MEM_ATTR void runtimeTimingTrackUpdate(uint8_t trackIndex, uint32_t maxUs,
                                          uint32_t overCount) {
  emitCapPrintf(SC_FMT("#CAP,%lu,DIAG,track_update,%u,%lu,%lu\r\n"),
                (unsigned long)micros(), static_cast<unsigned>(trackIndex),
                (unsigned long)maxUs, (unsigned long)overCount);
}

SC_MEM_ATTR void runtimeTimingClockrate(uint32_t pulsesPerSecond) {
  emitCapPrintf("#CAP,%lu,DIAG,clockrate,%lu\r\n", (unsigned long)micros(),
                (unsigned long)pulsesPerSecond);
}

SC_MEM_ATTR void runtimeTimingUsbBatch(uint32_t maxSize, uint32_t saturatedCount, uint32_t maxClocks,
                                       uint32_t maxConsecutive) {
  emitCapPrintf("#CAP,%lu,DIAG,usb_batch,%lu,%lu,%lu,%lu\r\n", (unsigned long)micros(),
                (unsigned long)maxSize, (unsigned long)saturatedCount, (unsigned long)maxClocks,
                (unsigned long)maxConsecutive);
}

SC_MEM_ATTR void runtimeTimingUsbBatchBuckets(const char* kind, const uint32_t buckets[5]) {
  emitCapPrintf("#CAP,%lu,DIAG,%s,%lu,%lu,%lu,%lu,%lu\r\n", (unsigned long)micros(), kind,
                (unsigned long)buckets[0], (unsigned long)buckets[1], (unsigned long)buckets[2],
                (unsigned long)buckets[3], (unsigned long)buckets[4]);
}

SC_MEM_ATTR void stopPhaseTiming(uint8_t track, uint8_t slot, uint8_t phase,
    uint32_t generation, uint32_t epoch, uint8_t destination, uint32_t calls,
    uint32_t crossed, uint32_t unknown, uint32_t startUs, uint32_t endUs, uint32_t slackUs) {
  emitCapPrintf(SC_FMT("#CAP,%lu,DIAG,stop_phase,%u,%u,%u,%lu,%lu,%u,%lu,%lu,%lu,%lu,%lu,%lu\r\n"),
      (unsigned long)micros(), (unsigned)track, (unsigned)slot, (unsigned)phase,
      (unsigned long)generation, (unsigned long)epoch, (unsigned)destination,
      (unsigned long)calls, (unsigned long)crossed, (unsigned long)unknown,
      (unsigned long)startUs, (unsigned long)endUs, (unsigned long)slackUs);
}

SC_MEM_ATTR void stopGrainTiming(uint8_t track, uint8_t slot, uint8_t phase,
    uint32_t generation, uint32_t epoch, uint8_t destination, uint32_t serviceSequence,
    uint32_t grainStartUs, uint32_t grainEndUs, uint32_t serviceActualUs,
    uint32_t serviceDueUs, uint32_t serviceDeadlineUs, uint32_t servicePeriodUs,
    uint32_t slackUs, uint8_t finishedPhase) {
  emitCapPrintf(SC_FMT("#CAP,%lu,DIAG,stop_grain,%u,%u,%u,%lu,%lu,%u,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%u\r\n"),
      (unsigned long)micros(), (unsigned)track, (unsigned)slot, (unsigned)phase,
      (unsigned long)generation, (unsigned long)epoch, (unsigned)destination,
      (unsigned long)serviceSequence, (unsigned long)grainStartUs, (unsigned long)grainEndUs,
      (unsigned long)serviceActualUs, (unsigned long)serviceDueUs,
      (unsigned long)serviceDeadlineUs, (unsigned long)servicePeriodUs,
      (unsigned long)slackUs, (unsigned)finishedPhase);
}

SC_MEM_ATTR void stopStateTiming(uint8_t track, uint8_t slot, uint8_t fromState,
    uint8_t toState, uint32_t tick, uint32_t atUs, uint32_t serviceSequence,
    uint32_t serviceDueUs, uint32_t serviceDeadlineUs, uint32_t servicePeriodUs) {
  if (fromState == 6 && toState == 7) {
    // Preserve the existing host state protocol using the occurrence timestamp.
    emitCapPrintf(SC_FMT("#CAP,%lu,ST,Track,OVERDUBBING,OVERDUB_STOPPING\r\n"),
                  (unsigned long)atUs);
  } else if (fromState == 7 && toState == 5) {
    // The control boundary releases the prepared playback window immediately;
    // keep the same occurrence-timed protocol edge for host/HITL observers.
    emitCapPrintf(SC_FMT("#CAP,%lu,ST,Track,OVERDUB_STOPPING,PLAYING\r\n"),
                  (unsigned long)atUs);
  }
  emitCapPrintf(SC_FMT("#CAP,%lu,DIAG,stop_state,%u,%u,%u,%u,%lu,%lu,%lu,%lu,%lu,%lu\r\n"),
      (unsigned long)micros(), (unsigned)track, (unsigned)slot, (unsigned)fromState,
      (unsigned)toState, (unsigned long)tick, (unsigned long)atUs,
      (unsigned long)serviceSequence, (unsigned long)serviceDueUs,
      (unsigned long)serviceDeadlineUs, (unsigned long)servicePeriodUs);
}

SC_MEM_ATTR void stopServiceTiming(uint32_t serviceSequence, uint32_t actualUs,
    uint32_t dueUs, uint32_t deadlineUs, uint32_t periodUs) {
  emitCapPrintf(SC_FMT("#CAP,%lu,DIAG,stop_service,%lu,%lu,%lu,%lu,%lu\r\n"),
      (unsigned long)micros(), (unsigned long)serviceSequence, (unsigned long)actualUs,
      (unsigned long)dueUs, (unsigned long)deadlineUs, (unsigned long)periodUs);
}

SC_MEM_ATTR void stopMidiEmissionTiming(uint32_t serviceSequence, uint8_t isNoteOn,
    uint32_t tick, uint32_t sendUs, uint32_t dueUs, uint32_t deadlineUs,
    uint32_t latenessUs, uint8_t track, uint8_t slot, uint8_t channel, uint8_t note) {
#if defined(__IMXRT1062__)
  const char* kind = isNoteOn ? PSTR("on") : PSTR("off");
#else
  const char* kind = isNoteOn ? "on" : "off";
#endif
  emitCapPrintf(SC_FMT("#CAP,%lu,DIAG,stop_emit,%lu,%s,%lu,%lu,%lu,%lu,%lu,%u,%u,%u,%u\r\n"),
      (unsigned long)micros(), (unsigned long)serviceSequence, kind, (unsigned long)tick,
      (unsigned long)sendUs, (unsigned long)dueUs, (unsigned long)deadlineUs,
      (unsigned long)latenessUs, (unsigned)track, (unsigned)slot,
      (unsigned)channel, (unsigned)note);
}

SC_MEM_ATTR void stopTimelineDropped(uint32_t droppedRecords) {
  emitCapPrintf(SC_FMT("#CAP,%lu,DIAG,stop_trace_drop,%lu\r\n"),
      (unsigned long)micros(), (unsigned long)droppedRecords);
}

SC_MEM_ATTR void midiDeadlineLateEvent(const char* midiClass, uint32_t latenessUs, uint32_t tick,
                                          uint32_t sendUs, uint32_t dueUs, uint32_t deadlineUs) {
  emitCapPrintf(SC_FMT("#CAP,%lu,DIAG,late_event,%s,%lu,%lu,%lu,%lu,%lu\r\n"),
                (unsigned long)micros(), midiClass, (unsigned long)latenessUs,
                (unsigned long)tick, (unsigned long)sendUs, (unsigned long)dueUs,
                (unsigned long)deadlineUs);
}

SC_MEM_ATTR void playbackRebuild(uint32_t durationUs, uint32_t windowStartTick,
                                 uint32_t windowLengthTicks, uint32_t revision, uint8_t trackIndex,
                                 uint8_t slotIndex, uint32_t rebuildCount, const char* path,
                                 uint32_t atUs, uint32_t previousPhase,
                                 uint32_t currentPhase, uint32_t eventCount) {
  emitCapPrintf(SC_FMT("#CAP,%lu,DIAG,playback_build,%s,%lu,%lu,%lu,%lu,%u,%u,%lu,%lu,%lu,%lu,%lu\r\n"),
                (unsigned long)micros(), path, (unsigned long)durationUs,
                (unsigned long)windowStartTick, (unsigned long)windowLengthTicks,
                (unsigned long)revision, static_cast<unsigned>(trackIndex),
                static_cast<unsigned>(slotIndex), static_cast<unsigned long>(rebuildCount),
                (unsigned long)atUs, (unsigned long)previousPhase,
                (unsigned long)currentPhase, (unsigned long)eventCount);
}

SC_MEM_ATTR void playbackOrder(uint32_t durationUs, uint32_t eventCount) {
  emitCapPrintf("#CAP,%lu,DIAG,playback_order,%lu,%lu\r\n", (unsigned long)micros(),
                (unsigned long)durationUs, (unsigned long)eventCount);
}

SC_MEM_ATTR void successorStartWindowOpen(uint8_t slot, uint32_t stopTick, uint32_t loopLengthTicks) {
  emitCapPrintf("#CAP,%lu,DIAG,reentry_window_open,%lu,%lu,%lu\r\n", (unsigned long)micros(),
                (unsigned long)slot, (unsigned long)stopTick, (unsigned long)loopLengthTicks);
}

SC_MEM_ATTR void canonicalFinalizationCapacityReleased(uint8_t slot, uint32_t loopLengthTicks,
                                                      size_t availableChunks) {
  emitCapPrintf("#CAP,%lu,DIAG,canonical_release,%lu,%lu,%lu\r\n", (unsigned long)micros(),
                (unsigned long)slot, (unsigned long)loopLengthTicks,
                (unsigned long)availableChunks);
}

SC_MEM_ATTR void firstRecordReturnWindowAdopted(uint32_t loopLengthTicks, size_t retainedEvents) {
  emitCapPrintf("#CAP,%lu,DIAG,first_record_adopt,%lu,%lu\r\n", (unsigned long)micros(),
                (unsigned long)loopLengthTicks, (unsigned long)retainedEvents);
}

SC_MEM_ATTR void reentryDecision(const char* decision, uint32_t actionTick, uint64_t interval,
                                 uint8_t slot, size_t queueDepth) {
  emitCapPrintf(SC_FMT("#CAP,%lu,DIAG,reentry,%s,%lu,%lu,%lu,%lu\r\n"), (unsigned long)micros(),
                decision != nullptr ? decision : "?", (unsigned long)actionTick,
                (unsigned long)interval, static_cast<unsigned>(slot),
                (unsigned long)queueDepth);
}

SC_MEM_ATTR void loopRemainder(const char* span, uint32_t durationUs, uint8_t track, uint8_t slot,
                               uint8_t phase, uint8_t isFocus) {
  emitCapPrintf("#CAP,%lu,DIAG,loop_rem,%s,%lu,%u,%u,%u,%u\r\n", (unsigned long)micros(), span,
                (unsigned long)durationUs, static_cast<unsigned>(track),
                static_cast<unsigned>(slot), static_cast<unsigned>(phase),
                static_cast<unsigned>(isFocus));
}

SC_MEM_ATTR void recordLoopRemainderSpan(const char* span, uint32_t durationUs) {
  if (durationUs < RuntimeTimingTelemetry::kLoopRemainderOneShotUs) {
    return;
  }
  bool active = false;
  uint8_t track = 255;
  uint8_t slot = 255;
  uint8_t phase = 255;
  uint8_t isFocus = 0;
  StorageManager::probeActiveLoadLoopJob(active, track, slot, phase, isFocus);
  loopRemainder(span, durationUs, track, slot, phase, isFocus);
}

SC_MEM_ATTR void overdubStartStage(const char* stage, uint32_t durationUs, uint32_t heapBefore,
                                   uint32_t heapAfter, const char* outcome) {
  emitCapPrintf("#CAP,%lu,ODUB,stage,%s,%lu,%lu,%lu,%s\r\n", (unsigned long)micros(), stage,
                (unsigned long)durationUs, (unsigned long)heapBefore, (unsigned long)heapAfter,
                outcome);
}

SC_MEM_ATTR void capturePrewarm(uint8_t track, uint8_t slot, bool fullViews,
                               bool ready, uint16_t credits, uint16_t freeChunks,
                               uint32_t heapFree) {
  emitCapPrintf(SC_FMT("#CAP,%lu,DIAG,capture_prewarm,track=%u,slot=%u,full=%u,ready=%u,credits=%u,free_chunks=%u,heap_free=%lu\r\n"),
      (unsigned long)micros(), track, slot, fullViews, ready, credits, freeChunks,
      (unsigned long)heapFree);
}

SC_MEM_ATTR void capturePrewarmMemory(const char* phase) {
  emitCapPrintf(SC_FMT("#CAP,%lu,DIAG,capture_prewarm,phase=%s,psram_free=%lu,psram_pool=%lu,heap_free=%lu,free_chunks=%u\r\n"),
      (unsigned long)micros(), phase,
      (unsigned long)MemoryMonitor::getExternalMemoryPoolFreeBytes(),
      (unsigned long)MemoryMonitor::getExternalMemoryPoolTotalBytes(),
      (unsigned long)MemoryMonitor::getInternalHeapFreeBytes(),
      (unsigned)LoopEventStore::freeChunkCount());
}

SC_MEM_ATTR void postRestoreCensus(uint16_t freeChunks, uint32_t heapFree) {
  // Timestamped like every other DIAG line: `#CAP,<micros>,DIAG,...`. The tier parser takes the field
  // after micros as the tag, so an un-timestamped form would also lose its Tier-A class.
  emitCapPrintf(SC_FMT("#CAP,%lu,DIAG,post_restore_census,free_chunks=%u,admission_complete=1,"
                       "interactive_ready=1,undo_hydrated=1,transport_stopped=1,"
                       "deferred_prewarm_idle=1,heap_free=%lu\r\n"),
                (unsigned long)micros(), static_cast<unsigned>(freeChunks),
                (unsigned long)heapFree);
}

SC_MEM_ATTR void overdubWrapTick(uint32_t totalUs, uint32_t sealUs, uint32_t passApplyUs,
                                 uint32_t srcAppendUs, uint32_t commitUs, uint32_t sealPendUs,
                                 uint32_t beginUs) {
  emitCapPrintf(SC_FMT("#CAP,%lu,DIAG,wrap,total=%lu,seal=%lu,pass_apply=%lu,src_append=%lu,"
                       "commit=%lu,seal_pend=%lu,begin=%lu\r\n"),
                (unsigned long)micros(), (unsigned long)totalUs, (unsigned long)sealUs,
                (unsigned long)passApplyUs, (unsigned long)srcAppendUs, (unsigned long)commitUs,
                (unsigned long)sealPendUs, (unsigned long)beginUs);
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

SC_MEM_ATTR void persistenceBundle(const char* workType, uint32_t totalUs,
                                   uint32_t maxSliceUs, uint32_t sliceCount,
                                   uint32_t undoEntryCount, uint32_t snapshotCount) {
  emitCapPrintf("#CAP,%lu,PERS,bundle,%s,%lu,%lu,%lu,%lu,%lu\r\n",
                (unsigned long)micros(), workType == nullptr ? "?" : workType,
                (unsigned long)totalUs, (unsigned long)maxSliceUs,
                (unsigned long)sliceCount, (unsigned long)undoEntryCount,
                (unsigned long)snapshotCount);
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

SC_MEM_ATTR void persistenceBacklog(uint16_t workQueueDepth, uint16_t writingWorkItems,
                                  uint16_t chunkQueueDepth, uint32_t dirtyAgeMs,
                                  uint32_t estSliceSteps, uint32_t estSdBytes,
                                  uint8_t savePending, uint8_t urgentRequested,
                                  uint32_t transportBlockCount, uint32_t budgetBlockCount,
                                  uint32_t heapFloorBlockCount) {
  emitCapPrintf(
      "#CAP,%lu,PERS,backlog,%u,%u,%u,%lu,%lu,%lu,%u,%u,%lu,%lu,%lu\r\n",
      (unsigned long)micros(), workQueueDepth, writingWorkItems, chunkQueueDepth,
      (unsigned long)dirtyAgeMs, (unsigned long)estSliceSteps, (unsigned long)estSdBytes,
      savePending, urgentRequested, (unsigned long)transportBlockCount,
      (unsigned long)budgetBlockCount, (unsigned long)heapFloorBlockCount);
}

SC_MEM_ATTR void persistenceDrainFailed(const char* reason, uint32_t steps,
                                          uint32_t stuckIterations, uint16_t workQueueDepth,
                                          uint16_t chunkQueueDepth, uint32_t estSliceSteps,
                                          uint32_t estSdBytes) {
  emitCapPrintf(
      "#CAP,%lu,PERS,drain,%s,%lu,%lu,%u,%u,%lu,%lu\r\n", (unsigned long)micros(), reason,
      (unsigned long)steps, (unsigned long)stuckIterations, workQueueDepth, chunkQueueDepth,
      (unsigned long)estSliceSteps, (unsigned long)estSdBytes);
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

SC_MEM_ATTR void storedNotes(uint8_t track, uint8_t slot, uint32_t notes, uint32_t uniqueNoteIds,
                             uint32_t maxSamePitch) {
  emitCapPrintf("#CAP,%lu,DIAG,stored_notes,track,%u,slot,%u,notes,%lu,unique,%lu,max_same_pitch,%lu\r\n",
                (unsigned long)micros(), track, slot, (unsigned long)notes,
                (unsigned long)uniqueNoteIds, (unsigned long)maxSamePitch);
}

SC_MEM_ATTR void overlapHold(uint32_t noteOffs, uint32_t emptySets, uint32_t maxIds,
                             uint32_t overflows, uint32_t lookedUp, uint32_t maxExamined,
                             uint32_t sumExamined, uint32_t maxLookupUs, uint32_t sumLookupUs,
                             uint32_t add, uint32_t shorten, uint32_t hide) {
  // Keys stay short so add/shorten/hide fit in kMaxCaptureTextBytes (192, NUL
  // inclusive) at uint32 worst case. n=note_offs e=empty_sets i=max_ids
  // o=overflows k=looked_up x=max_examined s=sum_examined u=max_lookup_us
  // t=sum_lookup_us. Pre-2026-09-02 logs used DIAG,overlap_hold,note_offs,…
  // and clipped hide (session_20260902_150143).
  emitCapPrintf(
      "#CAP,%lu,DIAG,oh,n,%lu,e,%lu,i,%lu,o,%lu,k,%lu,x,%lu,s,%lu,u,%lu,t,%lu,"
      "add,%lu,shorten,%lu,hide,%lu",
      (unsigned long)micros(), (unsigned long)noteOffs, (unsigned long)emptySets,
      (unsigned long)maxIds, (unsigned long)overflows, (unsigned long)lookedUp,
      (unsigned long)maxExamined, (unsigned long)sumExamined, (unsigned long)maxLookupUs,
      (unsigned long)sumLookupUs, (unsigned long)add, (unsigned long)shorten,
      (unsigned long)hide);
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

SC_MEM_ATTR void visualCacheState(const char* phase, int32_t events, uint32_t notes,
                                  uint32_t firstBar, uint32_t lastBar, uint32_t totalBars,
                                  uint32_t dirtyBarsSize, uint32_t dirtyCount, uint8_t dirtyFlag) {
  emitCapPrintf("#CAP,%lu,VCACHE,%s,ev,%ld,notes,%lu,first,%ld,last,%ld,total,%lu,dsz,%lu,dcnt,%lu,dirty,%u\r\n",
                (unsigned long)micros(), phase == nullptr ? "?" : phase, (long)events,
                (unsigned long)notes, firstBar == UINT32_MAX ? -1L : (long)firstBar,
                firstBar == UINT32_MAX ? -1L : (long)lastBar, (unsigned long)totalBars,
                (unsigned long)dirtyBarsSize, (unsigned long)dirtyCount, dirtyFlag);
}

SC_MEM_ATTR void displaySnapshot(uint8_t slot, const char* trackState, uint32_t loopLen,
                                 size_t sourceEventCount, size_t visualNotes, size_t frameNotes,
                                 size_t bufferEvents, int hasCommittedPasses) {
  emitCapPrintf("#CAP,%lu,DISP,%u,%s,%lu,%zu,%zu,%zu,%zu,%d", (unsigned long)micros(), slot,
                trackState, (unsigned long)loopLen, sourceEventCount, visualNotes, frameNotes,
                bufferEvents, hasCommittedPasses);
}

SC_MEM_ATTR void displaySnapshotWindow(uint8_t slot, const char* trackState, uint32_t loopLen,
                                       size_t sourceEventCount, size_t visualNotes, size_t frameNotes,
                                       size_t bufferEvents, int hasCommittedPasses, uint32_t windowStartTick,
                                       uint8_t windowBars, size_t windowNoteCount) {
  emitCapPrintf("#CAP,%lu,DISP,%u,%s,%lu,%zu,%zu,%zu,%zu,%d,%lu,%u,%zu",
                (unsigned long)micros(), slot, trackState, (unsigned long)loopLen, sourceEventCount,
                visualNotes, frameNotes, bufferEvents, hasCommittedPasses, (unsigned long)windowStartTick,
                windowBars, windowNoteCount);
}

SC_MEM_ATTR void displayNoteInfo(uint8_t pitch, uint32_t storageStart, uint32_t displayStart,
                                 uint32_t length, int selectedIdx) {
  emitCapPrintf("#CAP,%lu,DNTE,%u,%lu,%lu,%lu,%d\r\n", (unsigned long)micros(), pitch,
                (unsigned long)storageStart, (unsigned long)displayStart, (unsigned long)length,
                selectedIdx);
}

SC_MEM_ATTR void displayFrame(uint32_t frameNotes, uint32_t elapsedUs, uint32_t frameIndex,
                              uint32_t resolveUs, uint32_t drawUs, uint32_t transferUs) {
  emitCapPrintf("#CAP,%lu,DFRAME,%lu,%lu,%lu,%lu,%lu,%lu", (unsigned long)micros(),
                (unsigned long)frameNotes, (unsigned long)elapsedUs, (unsigned long)frameIndex,
                (unsigned long)resolveUs, (unsigned long)drawUs, (unsigned long)transferUs);
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

SC_MEM_ATTR void pendingSamePitchNoteOnReject(uint8_t pitch, uint8_t channel,
                                              uint32_t pendingStartTick, uint32_t rejectedTick) {
  emitCapPrintf("#CAP,%lu,PENDON,reject,%u,%u,%lu,%lu\r\n", (unsigned long)micros(), pitch, channel,
                (unsigned long)pendingStartTick, (unsigned long)rejectedTick);
}

SC_MEM_ATTR void captureCleanup(const char* phase, const char* kind, uint32_t count) {
  emitCapPrintf("#CAP,%lu,CLN,%s,%s,%lu\r\n", (unsigned long)micros(), phase, kind,
                (unsigned long)count);
}

void flushCaptureBuffer(size_t maxRecords) {
  // Never emit capture notices via USB Serial under timing-critical budgets.
  if (maxRecords > 8) {
    emitOverflowNotice();
    emitTierADegradedNotice();
    emitTimingShedNotice();
  }
  if (sCaptureRing.data == nullptr || sCaptureRing.used == 0) {
    return;
  }
  // Do not inflate a timing-critical budget (e.g. PLAYING flush of 8) to 256
  // records of blocking Serial I/O. Overflow drains across later frames instead.
  // Tier-A degradation drains hard for the same reason: every record not yet sent is one the
  // ring may have to displace again.
  if ((sCaptureRing.overflowPending || sCaptureRing.tierADegradedPending) && maxRecords >= 64 &&
      maxRecords < 256) {
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
      // This path breaks out on a Tier-A head above, so it only ever sheds diagnostics.
      discardOldestRecord(CaptureLineTier::EvictionKind::TimingShed);
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
      // S1.3: count identity rows that actually leave the ring for the host, so "delivered but not
      // parsed" is distinguishable from "never delivered".
      if (CaptureLineTier::tagStartsWith(line, "RID,")) {
        ++sCaptureRing.identityTransmitted;
      }
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
