//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/DebugSessionCapture.h"

#ifdef SESSION_CAPTURE

#include <Arduino.h>
#include <algorithm>
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
#if defined(__IMXRT1062__)
#define SC_JOURNAL_MEM_ATTR DMAMEM
#define SC_JOURNAL_COLD_ATTR FLASHMEM __attribute__((noinline))
#else
#define SC_JOURNAL_MEM_ATTR
#define SC_JOURNAL_COLD_ATTR
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
constexpr size_t kMaxCaptureTextBytes = 192;
constexpr size_t kCriticalJournalCapacity = 32;

struct CriticalJournal {
  uint32_t count = 0;
  uint32_t dumpIndex = 0;
  uint32_t dropped = 0;
  bool dumpStarted = false;
  char lines[kCriticalJournalCapacity][kMaxCaptureTextBytes] = {};
};
constexpr size_t kCaptureRingBytes = 1024 * 1024;
/// How far the head may scan forward for a record boundary after an unreadable head (see `headResyncDropBytes`).
constexpr size_t kHeadResyncScanBytes = 256;
constexpr size_t kCaptureRingPressureBytes = (kCaptureRingBytes * 3) / 4;
// Ring slice only Record rows may consume. A take's start/stop/stage/identity rows are at most a
// few kilobytes, so 32 KiB holds them many times over while costing ordinary traffic 3% of the ring.
constexpr size_t kRecordReserveBytes = 32 * 1024;
// Bytes of the unreadable head record kept for the wipe diagnostic (32 hex chars in the row).
constexpr size_t kSilentWipePrefixBytes = 16;

struct RecordFamilyStats {
  uint32_t entered = 0;
  uint32_t enqueued = 0;
  uint32_t rejected = 0;
  uint32_t transmitted = 0;
  uint32_t evicted = 0;
};

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
  //    critical, which the displacement policy permits), so the capture is incomplete and stays fatal.
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
  uint32_t criticalRejectedPending = 0;
  uint32_t criticalFirstRejectedUs = 0;
  // Append/flush accounting. Every *counted* loss path above is a policy outcome; these counters
  // exist for the one that was not: `flushCaptureBuffer` and `discardOldestRecord` reset the whole
  // ring when the head record cannot be read (`payloadLen == 0` or a length past `used`). That
  // discard was silent, so a capture that produced no rows for a whole phase could not be told from
  // a phase whose emitters never ran (measured: `captures/s4f22-step3-base-01` has 2 888 rows with
  // 34-44.5 s timestamps, 2 688 after 72 s, and zero rows at all between -- with every shed,
  // eviction and overflow counter at 0 or 1).
  uint32_t appendAttempts = 0;
  uint32_t appendsOk = 0;
  uint32_t flushCalls = 0;
  uint32_t flushTransmitted = 0;
  uint32_t silentWipes = 0;
  uint32_t silentWipeBytes = 0;
  uint32_t silentWipeFirstUs = 0;
  // Boot-lifetime totals. The `*Pending`/count fields above are cleared when their notice is
  // emitted, so they answer "is a notice owed", not "how much was lost". These never reset, and
  // they are what `RING,stats` reports: without them a finished run reads `shed=0 tierA_evict=0`
  // even when the ring evicted all session.
  uint32_t shedTotal = 0;
  uint32_t tierAEvictedTotal = 0;
  uint32_t overflowTotal = 0;
  uint32_t criticalRejectedTotal = 0;
  // First wipe's evidence: indices, the header field that failed the read, and the first bytes.
  uint32_t silentWipeHead = 0;
  uint32_t silentWipeTail = 0;
  uint32_t silentWipeUsed = 0;
  uint8_t silentWipeType = 0;
  uint16_t silentWipePayloadLen = 0;
  uint8_t silentWipePrefix[kSilentWipePrefixBytes] = {};
  // Boundary plausibility: the only two places that read a header and advance by it are
  // `discardOldestRecord` and the flush walk. If either advances by a total that does not match
  // the record it sat on, the head lands mid-record, the *next* read is text and the ring is
  // wiped. Checking right after each advance names the operation that did it, instead of only
  // reporting the wipe it caused (`captures/s4f22-step6-c2`: type=50, prefix "295,4294967295,1").
  uint32_t headBadCount = 0;
  uint32_t headBadFirstUs = 0;
  uint32_t headBadHead = 0;
  uint32_t headBadTail = 0;
  uint32_t headBadUsed = 0;
  uint8_t headBadOp = 0;
  uint8_t headBadType = 0;
  uint16_t headBadPayloadLen = 0;
  uint8_t headBadPrefix[kSilentWipePrefixBytes] = {};
  bool headBadPending = false;
  // Bounded head resynchronization: a head that is not a record boundary usually has a *valid* boundary a few
  // bytes ahead (measured 2026-10-01: 7 corrupt bytes at the head of a 9 524-byte live ring caused the whole ring
  // to be wiped and the HITL run to be aborted, `captures/s4b-gate-newimage-01`). Dropping only the unreadable
  // prefix keeps the readable records -- in this ring, all of them -- so the loss is bounded and named instead of
  // total and run-invalidating. Counted, never silent.
  uint32_t headResyncCount = 0;
  uint32_t headResyncBytes = 0;
  uint32_t headResyncFirstUs = 0;
  uint32_t headResyncHead = 0;
  bool headResyncPending = false;
  bool wipedPending = false;
  RecordFamilyStats recordStart;
  RecordFamilyStats recordStop;
  /// Task 2.2: the accepted-interaction latch's own family. Its edges are one-shot per episode, so its loss
  /// must be countable rather than silent -- the defect class `DIAG,reentry*` was made Tier-A to fix.
  RecordFamilyStats interactionLatch;

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

SC_JOURNAL_MEM_ATTR CriticalJournal sCriticalJournal{};
CaptureRing sCaptureRing;
// Ordinary .bss is cleared by Teensy startup; the DMAMEM journal is not.
bool sCriticalJournalInitialized = false;
struct PlaybackReadMailbox {
  PlaybackReadWitness witness{};
  uint32_t lastUs = 0;
  uint32_t lastTick = 0;
  bool seen = false;
  bool pending = false;
};
SC_JOURNAL_MEM_ATTR PlaybackReadMailbox sPlaybackReads[8][8];
uint8_t sPlaybackReadScan = 0;

/// True while `flushCaptureBuffer` is walking the ring. `Serial` output can re-enter this module
/// through the Teensy `yield()`, and a nested append that evicts would move the head under the
/// flush; a nested flush would do the same. Both are refused while set.
bool sFlushInProgress = false;

SC_MEM_ATTR void incrementSaturated(uint32_t& value) {
  if (value != UINT32_MAX) ++value;
}

SC_JOURNAL_COLD_ATTR void resetCriticalJournalMetadata() {
  sCriticalJournal.count = 0;
  sCriticalJournal.dumpIndex = 0;
  sCriticalJournal.dropped = 0;
  sCriticalJournal.dumpStarted = false;
  for (auto& track : sPlaybackReads) for (auto& slot : track) slot = PlaybackReadMailbox{};
  sPlaybackReadScan = 0;
}

SC_JOURNAL_COLD_ATTR void ensureCriticalJournalInitialized() {
  if (sCriticalJournalInitialized) return;
  resetCriticalJournalMetadata();
  sCriticalJournalInitialized = true;
}

SC_JOURNAL_COLD_ATTR bool validateCriticalJournalMetadata() {
  ensureCriticalJournalInitialized();
  if (sCriticalJournal.count <= kCriticalJournalCapacity &&
      sCriticalJournal.dumpIndex <= sCriticalJournal.count &&
      (sCriticalJournal.dumpIndex == 0 || sCriticalJournal.dumpStarted)) {
    return true;
  }
  // Unknown journal loss is one reported integrity incident, not a trusted row
  // count. Reset scalars only; never walk payload using a corrupted index.
  resetCriticalJournalMetadata();
  sCriticalJournal.dropped = 1;
  if (sCaptureRing.criticalRejectedPending == 0)
    sCaptureRing.criticalFirstRejectedUs = micros();
  incrementSaturated(sCaptureRing.criticalRejectedPending);
  incrementSaturated(sCaptureRing.criticalRejectedTotal);
  return false;
}

SC_MEM_ATTR RecordFamilyStats* recordFamilyStats(const char* line) {
  if (CaptureLineTier::tagStartsWith(line, "RECA,")) return &sCaptureRing.recordStart;
  if (CaptureLineTier::tagStartsWith(line, "RECS,")) return &sCaptureRing.recordStop;
  if (CaptureLineTier::tagStartsWith(line, "DIAG,latch,")) return &sCaptureRing.interactionLatch;
  return nullptr;
}

bool readHeaderAt(size_t index, CaptureRecordHeader& headerOut);
void readBytesAt(size_t index, void* dest, size_t len);
bool serialWriteRoom(size_t bytesNeeded);
SC_JOURNAL_COLD_ATTR void journalCriticalLine(const char* line) {
  if (line == nullptr) return;
  if (!validateCriticalJournalMetadata()) return;
  if (sCriticalJournal.count >= kCriticalJournalCapacity) {
    incrementSaturated(sCriticalJournal.dropped);
    return;
  }
  const size_t len = strnlen(line, kMaxCaptureTextBytes - 1);
  memcpy(sCriticalJournal.lines[sCriticalJournal.count], line, len);
  sCriticalJournal.lines[sCriticalJournal.count][len] = '\0';
  ++sCriticalJournal.count;
}

SC_JOURNAL_COLD_ATTR void journalCriticalHeadRecord() {
  if (!validateCriticalJournalMetadata()) return;
  if (sCriticalJournal.count >= kCriticalJournalCapacity) {
    incrementSaturated(sCriticalJournal.dropped);
    return;
  }
  CaptureRecordHeader header{};
  if (!readHeaderAt(sCaptureRing.head, header) ||
      header.type != static_cast<uint8_t>(CaptureRecordType::Text) || header.payloadLen == 0) {
    incrementSaturated(sCriticalJournal.dropped);
    return;
  }
  const size_t index = sCriticalJournal.count;
  const size_t copyLen = std::min<size_t>(header.payloadLen, kMaxCaptureTextBytes - 1);
  readBytesAt(sCaptureRing.head + sizeof(header), sCriticalJournal.lines[index], copyLen);
  sCriticalJournal.lines[index][copyLen] = '\0';
  ++sCriticalJournal.count;
}

SC_JOURNAL_COLD_ATTR void emitCriticalJournal() {
  if (!validateCriticalJournalMetadata()) return;
  if (sCriticalJournal.dumpIndex == 0 && sCriticalJournal.count == 0 &&
      sCriticalJournal.dropped == 0) {
    return;
  }
  if (!sCriticalJournal.dumpStarted) {
    if (!serialWriteRoom(112)) return;
    Serial.printf_P(SC_FMT("#CAP,%lu,EVIDENCE,critical_journal_begin,count=%u,dropped=%u\r\n"),
                    static_cast<unsigned long>(micros()),
                    static_cast<unsigned>(sCriticalJournal.count),
                    static_cast<unsigned>(sCriticalJournal.dropped));
    sCriticalJournal.dumpStarted = true;
  }
  while (sCriticalJournal.dumpIndex < sCriticalJournal.count) {
    if (!validateCriticalJournalMetadata()) return;
    const char* line = sCriticalJournal.lines[sCriticalJournal.dumpIndex];
    const size_t bytes = strnlen(line, kMaxCaptureTextBytes - 1) + 2;
    if (!serialWriteRoom(bytes)) return;
    Serial.println(line);
    ++sCriticalJournal.dumpIndex;
  }
  if (!serialWriteRoom(96)) return;
  Serial.printf_P(SC_FMT("#CAP,%lu,EVIDENCE,critical_journal_end\r\n"),
                  static_cast<unsigned long>(micros()));
  resetCriticalJournalMetadata();
}

static const char kPitMotionTierPrefix[] PROGMEM = "DIAG,pit_motion,";
#if MIDI_INGRESS_COST_QUALIFICATION
static const char kIngressCostTierPrefix[] PROGMEM = "DIAG,ingress_cost,";
#endif
#if defined(PIO_UNIT_TEST_NATIVE) || (PLAYBACK_WINDOW_ROLLING_ENABLED && PLAYBACK_WINDOW_ROLLING_QUALIFICATION)
static const char kRollingRecordTierPrefix[] PROGMEM = "DIAG,rolling_record,";
#endif
static const char kPlaybackReadTierPrefix[] PROGMEM = "DIAG,playback_read,";
static const char kPlaybackAvailableTierPrefix[] PROGMEM = "DIAG,playback_available,";
SC_MEM_ATTR bool isTierATextLine(const char* line) {
  // The window lane's **service-gap sample** (task 3.10) is Tier-A, and its tag is classified **here** rather than in
  // `CaptureLineTier.h`: that list is an inline header function, so every literal in it is instantiated in every
  // including TU, and one more literal of that size was measured to tip this image's DTCM across an ALIGN boundary
  // (RAM1 guard: `variables` +4,096 B). One literal in this TU costs nothing extra in that accounting.
  if (CaptureLineTier::isTierALine(line)) {
    return true;
  }
  // ...and so is the family's **named overrun row** (task 3.10 / DEC-108 §13b, blocker 3): the row that says the
  // permitted advance was *exceeded*. It does not start with `DIAG,win_gap,` -- the 13th byte is `_`, not `,` -- so it
  // needs its own prefix, and it needs it more than the sample does: an overrun can only be raised while the lane is
  // serving, i.e. under transport, which is exactly the budget-8 regime whose shed path drops non-Tier-A head records
  // silently. Without this the one row that reports an exceedance was the one dropped where exceedances happen. The
  // boundary is exact and pinned in `test_capture_ring`, the suite that asks **this** predicate (the header's
  // `CaptureLineTier::isTierALine` cannot see this family by construction -- see the note above).
  // ...and so are the **qualification-only gate rows**: a launch's decision is taken while the track plays, the
  // snapshot owner's refusal in the same regime, and the rolling demand path's own exit only exists *under* transport
  // (the lane is dispatched while a transport runs) -- the budget-8 shed path is exactly where all three are needed.
  // Measured: the first device runs of the launch row carried it in the firmware and delivered **none** of them.
  // (A duplicated `launch_gate` clause used to sit after the `return` here, unreachable; removed 2026-10-05.)
  return CaptureLineTier::tagStartsWith(line, "DIAG,win_gap,") ||
         CaptureLineTier::tagStartsWith(line, "DIAG,pit_obs,") ||
         CaptureLineTier::tagStartsWith(line, kPitMotionTierPrefix) ||
#if MIDI_INGRESS_COST_QUALIFICATION
         CaptureLineTier::tagStartsWith(line, kIngressCostTierPrefix) ||
#endif
         CaptureLineTier::tagStartsWith(line, "DIAG,win_gap_overrun,") ||
         CaptureLineTier::tagStartsWith(line, kPlaybackReadTierPrefix) ||
         CaptureLineTier::tagStartsWith(line, kPlaybackAvailableTierPrefix) ||
#if defined(PIO_UNIT_TEST_NATIVE) || (PLAYBACK_WINDOW_ROLLING_ENABLED && PLAYBACK_WINDOW_ROLLING_QUALIFICATION)
         CaptureLineTier::tagStartsWith(line, kRollingRecordTierPrefix) ||
#endif
         CaptureLineTier::tagStartsWith(line, "DIAG,paint_gate,") ||
         CaptureLineTier::tagStartsWith(line, "DIAG,launch_gate,") ||
         CaptureLineTier::tagStartsWith(line, "DIAG,snap_gate,") ||
         CaptureLineTier::tagStartsWith(line, "DIAG,win_gate,") ||
         // S7.5a envelope instruments (capture stamp error in ticks, capture-replenishment wait/work/refusals).
         // One prefix literal, classified here rather than in `CaptureLineTier.h` for the reason above; the row
         // itself is aggregated and its format is flash-resident (`SC_FMT`).
         CaptureLineTier::tagStartsWith(line, "DIAG,envelope_instr,") ||
         // S7.5a distribution instrument (the window's per-channel p50/p99 intervals and its per-route lateness
         // split). Classified here for the same reason as the row above -- it is emitted while a transport runs --
         // but the prefixes themselves come from the rows' own unit: two more literals in *this* unit's `.rodata`
         // crossed the DTCM align edge (`variables` 96,192 -> 100,288 with ITCM code unchanged), and that unit is
         // carved into FLASH (`linker/imxrt1062_t41_lcr.ld`). See `Utils/EnvelopeDistRows.cpp`.
         CaptureLineTier::tagStartsWith(line, envelopeDistTagPrefix()) ||
         CaptureLineTier::tagStartsWith(line, envelopeRouteTagPrefix()) ||
         // Task 4.20 allocation census: rate-limited (one row per diagnostics interval) and raised regardless of
         // transport state — including while a transport runs, which is exactly the budget-8 regime that sheds
         // non-Tier-A head records. Its own boundary is pinned in `test_capture_ring`.
         CaptureLineTier::tagStartsWith(line, "DIAG,alloc_census,") ||
         // Task 4.20 allocatability probe: host-requested, but it can only answer where the denial is felt — under
         // transport, the budget-8 regime that sheds non-Tier-A head records. Pinned in `test_capture_ring`. The
         // denial-edge capacity ladder rides this same prefix on purpose: one more literal in *this* unit's `.rodata`
         // was measured to cost exactly +32 B of DTCM, which the RAM1 budget gate refuses, and the ladder is the
         // probe family's second shape (`ladder,tail=…,need=…,largest=…,mask=…`) rather than a new family.
         CaptureLineTier::tagStartsWith(line, "DIAG,heap_probe,");
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

// Read only the bounded prefix needed by record-family classifiers.
SC_MEM_ATTR void headRecordTextPrefix(char (&line)[48]) {
  line[0] = '\0';
  CaptureRecordHeader header{};
  if (!readHeaderAt(sCaptureRing.head, header) ||
      header.type != static_cast<uint8_t>(CaptureRecordType::Text) || header.payloadLen == 0)
    return;
  const size_t copyLen = std::min<size_t>(header.payloadLen, sizeof(line) - 1);
  readBytesAt(sCaptureRing.head + sizeof(header), line, copyLen);
  line[copyLen] = '\0';
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
  incrementSaturated(sCaptureRing.shedTotal);
}

/// Account for a ring reset that discards content without an eviction/shed outcome. The bytes are
/// the occupancy the reset threw away, which is what makes the loss a number rather than a mystery.
/// The head/tail/used triple, the header field that failed the read and the first bytes of the
/// record are captured too: the wipe fires at a same-boot attach boundary
/// (`captures/s4f22-step5-b2`, first wipe at the same device timestamp as b3's), and those fields
/// are what distinguish a torn header from a stale tail or a wrapped-read bug.
SC_MEM_ATTR void noteSilentWipe() {
  if (sCaptureRing.silentWipes == 0) {
    sCaptureRing.silentWipeFirstUs = micros();
  }
  incrementSaturated(sCaptureRing.silentWipes);
  sCaptureRing.silentWipeBytes += static_cast<uint32_t>(sCaptureRing.used);
  if (sCaptureRing.silentWipes == 1) {
    sCaptureRing.silentWipeHead = static_cast<uint32_t>(sCaptureRing.head);
    sCaptureRing.silentWipeTail = static_cast<uint32_t>(sCaptureRing.tail);
    sCaptureRing.silentWipeUsed = static_cast<uint32_t>(sCaptureRing.used);
    CaptureRecordHeader header{};
    (void)readHeaderAt(sCaptureRing.head, header);
    sCaptureRing.silentWipeType = header.type;
    sCaptureRing.silentWipePayloadLen = header.payloadLen;
    for (size_t i = 0; i < kSilentWipePrefixBytes; ++i) {
      sCaptureRing.silentWipePrefix[i] = readByteAt(sCaptureRing.head + i);
    }
  }
  sCaptureRing.wipedPending = true;
}

/// Record the first head that is not a record boundary, naming the operation that produced it.
/// Op codes: 1 append entry, 2 flush entry, 3 after `discardOldestRecord` advance, 4 after the
/// flush walk's advance.
SC_MEM_ATTR void checkHeadPlausible(uint8_t op) {
  if (sCaptureRing.capacity == 0 || sCaptureRing.used < sizeof(CaptureRecordHeader)) {
    return;  // nothing live to check (an empty or <header>-sized tail is not a boundary question)
  }
  CaptureRecordHeader header{};
  (void)readHeaderAt(sCaptureRing.head, header);
  const size_t total = recordTotalBytes(header);
  if (header.payloadLen != 0 && total <= sCaptureRing.used) {
    return;
  }
  if (sCaptureRing.headBadCount == 0) {
    sCaptureRing.headBadFirstUs = micros();
    sCaptureRing.headBadHead = static_cast<uint32_t>(sCaptureRing.head);
    sCaptureRing.headBadTail = static_cast<uint32_t>(sCaptureRing.tail);
    sCaptureRing.headBadUsed = static_cast<uint32_t>(sCaptureRing.used);
    sCaptureRing.headBadOp = op;
    sCaptureRing.headBadType = header.type;
    sCaptureRing.headBadPayloadLen = header.payloadLen;
    for (size_t i = 0; i < kSilentWipePrefixBytes; ++i) {
      sCaptureRing.headBadPrefix[i] = readByteAt(sCaptureRing.head + i);
    }
  }
  incrementSaturated(sCaptureRing.headBadCount);
  sCaptureRing.headBadPending = true;
}

SC_MEM_ATTR void emitHeadResyncNotice() {
  if (!sCaptureRing.headResyncPending || !serialWriteRoom(96)) {
    return;
  }
  const uint32_t count = sCaptureRing.headResyncCount;
  const uint32_t bytes = sCaptureRing.headResyncBytes;
  const uint32_t firstUs = sCaptureRing.headResyncFirstUs;
  const uint32_t head = sCaptureRing.headResyncHead;
  sCaptureRing.headResyncPending = false;
  sCaptureRing.headResyncCount = 0;
  sCaptureRing.headResyncBytes = 0;
  sCaptureRing.headResyncFirstUs = 0;
  Serial.printf(SC_FMT("#CAP,%lu,RING,head_resync,%lu,%lu,%lu,head=%lu\r\n"),
                static_cast<unsigned long>(micros()), static_cast<unsigned long>(count),
                static_cast<unsigned long>(bytes), static_cast<unsigned long>(firstUs),
                static_cast<unsigned long>(head));
}

SC_MEM_ATTR void emitHeadBadNotice() {
  if (!sCaptureRing.headBadPending || !serialWriteRoom(160)) {
    return;
  }
  char prefix[kSilentWipePrefixBytes * 2 + 1] = {};
  static const char* kHex = "0123456789abcdef";
  for (size_t i = 0; i < kSilentWipePrefixBytes; ++i) {
    prefix[i * 2] = kHex[(sCaptureRing.headBadPrefix[i] >> 4) & 0x0F];
    prefix[i * 2 + 1] = kHex[sCaptureRing.headBadPrefix[i] & 0x0F];
  }
  sCaptureRing.headBadPending = false;
  Serial.printf(SC_FMT("#CAP,%lu,RING,head_bad,%lu,op=%u,head=%lu,tail=%lu,used=%lu,type=%u,"
                      "plen=%u,prefix=%s\r\n"),
                static_cast<unsigned long>(micros()),
                static_cast<unsigned long>(sCaptureRing.headBadCount),
                static_cast<unsigned>(sCaptureRing.headBadOp),
                static_cast<unsigned long>(sCaptureRing.headBadHead),
                static_cast<unsigned long>(sCaptureRing.headBadTail),
                static_cast<unsigned long>(sCaptureRing.headBadUsed),
                static_cast<unsigned>(sCaptureRing.headBadType),
                static_cast<unsigned>(sCaptureRing.headBadPayloadLen), prefix);
}

SC_MEM_ATTR void resetRingAfterUnreadableHead() {
  noteSilentWipe();
  sCaptureRing.head = sCaptureRing.tail;
  sCaptureRing.used = 0;
}

/// A record header is plausible when its type is one of the record types and the record it declares fits in what
/// the ring holds. This is the same condition `checkHeadPlausible` reports on, in the form the resync scan needs.
SC_MEM_ATTR bool headerIsPlausible(const CaptureRecordHeader& header, size_t available) {
  if (header.type < static_cast<uint8_t>(CaptureRecordType::Revt) ||
      header.type > static_cast<uint8_t>(CaptureRecordType::DiagTrace)) {
    return false;
  }
  return header.payloadLen != 0 && recordTotalBytes(header) <= available;
}

/// How far the head must move to sit on a plausible record boundary, or 0 when none is found within the bounded
/// scan window (the caller then resets, as before). A candidate is accepted only when the record *after* it is
/// plausible too (or the candidate ends the ring), so a single accidental byte pattern is not taken for a boundary.
SC_MEM_ATTR size_t headResyncDropBytes() {
  const size_t window = sCaptureRing.used < kHeadResyncScanBytes ? sCaptureRing.used : kHeadResyncScanBytes;
  for (size_t skip = 1; skip + sizeof(CaptureRecordHeader) <= window; ++skip) {
    CaptureRecordHeader candidate{};
    readBytesAt(sCaptureRing.head + skip, &candidate, sizeof(candidate));
    const size_t available = sCaptureRing.used - skip;
    if (!headerIsPlausible(candidate, available)) {
      continue;
    }
    const size_t afterCandidate = skip + recordTotalBytes(candidate);
    if (afterCandidate + sizeof(CaptureRecordHeader) > window) {
      return skip;  // the candidate ends the scanned window: nothing follows to contradict it
    }
    CaptureRecordHeader next{};
    readBytesAt(sCaptureRing.head + afterCandidate, &next, sizeof(next));
    if (headerIsPlausible(next, sCaptureRing.used - afterCandidate)) {
      return skip;
    }
  }
  return 0;
}

/// Recover from an unreadable head: drop only the unreadable prefix when a boundary is found nearby, and fall back
/// to the whole-ring reset when it is not. Op is the caller's site for the head_bad report.
SC_MEM_ATTR void recoverUnreadableHead() {
  const size_t drop = headResyncDropBytes();
  if (drop == 0) {
    resetRingAfterUnreadableHead();
    return;
  }
  if (sCaptureRing.headResyncCount == 0) {
    sCaptureRing.headResyncFirstUs = micros();
    sCaptureRing.headResyncHead = static_cast<uint32_t>(sCaptureRing.head);
  }
  incrementSaturated(sCaptureRing.headResyncCount);
  sCaptureRing.headResyncBytes += static_cast<uint32_t>(drop);
  sCaptureRing.headResyncPending = true;
  sCaptureRing.head = (sCaptureRing.head + drop) % sCaptureRing.capacity;
  sCaptureRing.used -= drop;
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
    resetRingAfterUnreadableHead();
    return;
  }
  const size_t total = recordTotalBytes(header);
  if (total > sCaptureRing.used) {
    recoverUnreadableHead();
    return;
  }
  advanceHead(total);
  checkHeadPlausible(3);
  switch (kind) {
    case CaptureLineTier::EvictionKind::TimingShed:
      noteTimingShed();
      break;
    case CaptureLineTier::EvictionKind::TierAText:
      sCaptureRing.tierADegradedPending = true;
      ++sCaptureRing.tierAEvictedCount;
      incrementSaturated(sCaptureRing.tierAEvictedTotal);
      if (sCaptureRing.tierAFirstEvictedUs == 0) {
        sCaptureRing.tierAFirstEvictedUs = micros();
      }
      break;
    case CaptureLineTier::EvictionKind::CriticalText:
      sCaptureRing.overflowPending = true;
      incrementSaturated(sCaptureRing.overflowTotal);
      if (sCaptureRing.overflowFirstEvictedUs == 0) {
        sCaptureRing.overflowFirstEvictedUs = micros();
      }
      break;
  }
}

SC_MEM_ATTR bool appendCaptureRecord(CaptureRecordType type, const void* payload, uint16_t payloadLen) {
  incrementSaturated(sCaptureRing.appendAttempts);
  checkHeadPlausible(1);
  const char* text = type == CaptureRecordType::Text ? static_cast<const char*>(payload) : nullptr;
  const bool incomingTierA = text != nullptr && isTierATextLine(text);
  const bool incomingCritical = text != nullptr && isCriticalTextLine(text);
  const bool incomingIdentity = CaptureLineTier::tagStartsWith(text, "RID,");
  RecordFamilyStats* family = recordFamilyStats(text);
  const auto reject = [&]() {
    if (family != nullptr) incrementSaturated(family->rejected);
    if (incomingIdentity) ++sCaptureRing.identityEmitRejected;
    if (incomingCritical) {
      journalCriticalLine(text);
      if (sCaptureRing.criticalRejectedPending == 0)
        sCaptureRing.criticalFirstRejectedUs = micros();
      incrementSaturated(sCaptureRing.criticalRejectedPending);
      incrementSaturated(sCaptureRing.criticalRejectedTotal);
    } else {
      noteTimingShed();
    }
    return false;
  };
  if (!ensureCaptureRingAllocated()) return reject();
  const CaptureRecordHeader header{static_cast<uint8_t>(type), payloadLen};
  const size_t total = recordTotalBytes(header);
  if (total > sCaptureRing.capacity) return reject();
  // A take's Record rows are its only evidence of geometry and persistence target, so ordinary
  // traffic may not consume the last `kRecordReserveBytes` of the ring. Keeping that slice free
  // means a start/stop/identity row is appended into reserved space instead of displacing an
  // earlier one -- the displacement that cost a whole RECS family when the ring filled during a
  // dense run. Ordinary rows still cycle: they displace an ordinary head at the ceiling, so the
  // reserved slice stays free and the ring cannot wedge. Only Record rows may use the reserve,
  // and a ring too small to hold one is unchanged.
  const size_t ceiling =
      (family != nullptr || sCaptureRing.capacity <= kRecordReserveBytes)
          ? sCaptureRing.capacity
          : sCaptureRing.capacity - kRecordReserveBytes;
  while (sCaptureRing.used + total > ceiling) {
    // Never move the head while a flush is walking it. `Serial.printf`/`println` can call the
    // Teensy `yield()`, which services the USB-host MIDI task; a message delivered there appends
    // to this ring. An eviction from that nested append advanced the head mid-record, so the
    // flush's next header read was garbage and the whole ring was reset -- 10 355 bytes thrown
    // away in `captures/s4f22-step3-base-04`, in the middle of a REVT burst, with every other
    // loss counter at zero. A nested append that cannot fit is refused and counted instead.
    if (sFlushInProgress) return reject();
    const bool headTierA = headRecordIsTierAText();
    const bool headCritical = headRecordIsCriticalText();
    if (!CaptureLineTier::incomingMayDisplace(incomingTierA, incomingCritical, headTierA,
                                              headCritical)) return reject();
    char head[48] = {};
    headRecordTextPrefix(head);
    if (CaptureLineTier::tagStartsWith(head, "RID,")) ++sCaptureRing.identityEvicted;
    if (auto* evictedFamily = recordFamilyStats(head)) incrementSaturated(evictedFamily->evicted);
    const size_t usedBefore = sCaptureRing.used;
    if (headCritical) journalCriticalHeadRecord();
    discardOldestRecord(CaptureLineTier::evictionKindFor(headTierA, headCritical));
    if (sCaptureRing.used == usedBefore) return reject();
  }
  writeBytesAt(sCaptureRing.tail, &header, sizeof(header));
  writeBytesAt(sCaptureRing.tail + sizeof(header), payload, payloadLen);
  sCaptureRing.tail = (sCaptureRing.tail + total) % sCaptureRing.capacity;
  sCaptureRing.used += total;
  if (family != nullptr) incrementSaturated(family->enqueued);
  if (incomingIdentity) ++sCaptureRing.identityEmitEnqueued;
  incrementSaturated(sCaptureRing.appendsOk);
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

SC_MEM_ATTR void emitCriticalRejectedNotice() {
  if (sCaptureRing.criticalRejectedPending == 0 || !serialWriteRoom(96)) return;
  Serial.printf_P(SC_FMT("#CAP,%lu,RING,critical_rejected,%lu,%lu\r\n"),
                  static_cast<unsigned long>(micros()),
                  static_cast<unsigned long>(sCaptureRing.criticalRejectedPending),
                  static_cast<unsigned long>(sCaptureRing.criticalFirstRejectedUs));
  sCaptureRing.criticalRejectedPending = 0;
  sCaptureRing.criticalFirstRejectedUs = 0;
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

/// A ring reset discarded content whose head record could not be read. That is a capture loss with
/// no eviction/shed outcome to report, so it gets its own notice and invalidates the capture.
void emitWipedNotice() {
  if (!sCaptureRing.wipedPending) {
    return;
  }
  if (!serialWriteRoom(96)) {
    return;  // keep pending; never block main on the diagnostic notice
  }
  const uint32_t wipes = sCaptureRing.silentWipes;
  const uint32_t bytes = sCaptureRing.silentWipeBytes;
  const uint32_t firstUs = sCaptureRing.silentWipeFirstUs;
  sCaptureRing.wipedPending = false;
  Serial.printf_P(SC_FMT("#CAP,%lu,RING,wiped,%lu,%lu,%lu\r\n"),
                  static_cast<unsigned long>(micros()), static_cast<unsigned long>(wipes),
                  static_cast<unsigned long>(bytes), static_cast<unsigned long>(firstUs));
  char prefix[kSilentWipePrefixBytes * 2 + 1] = {};
  for (size_t i = 0; i < kSilentWipePrefixBytes; ++i) {
    static const char* kHex = "0123456789abcdef";
    prefix[i * 2] = kHex[(sCaptureRing.silentWipePrefix[i] >> 4) & 0x0F];
    prefix[i * 2 + 1] = kHex[sCaptureRing.silentWipePrefix[i] & 0x0F];
  }
  Serial.printf(SC_FMT("#CAP,%lu,RING,wipe_detail,head=%lu,tail=%lu,used=%lu,type=%u,plen=%u,prefix=%s\r\n"),
                static_cast<unsigned long>(micros()),
                static_cast<unsigned long>(sCaptureRing.silentWipeHead),
                static_cast<unsigned long>(sCaptureRing.silentWipeTail),
                static_cast<unsigned long>(sCaptureRing.silentWipeUsed),
                static_cast<unsigned>(sCaptureRing.silentWipeType),
                static_cast<unsigned>(sCaptureRing.silentWipePayloadLen), prefix);
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
  ensureCriticalJournalInitialized();
  (void)ensureCaptureRingAllocated();
  markCaptureBootUs();
}

SC_MEM_ATTR bool captureRingRange(uintptr_t& start, size_t& bytes) {
  if (sCaptureRing.data == nullptr || sCaptureRing.capacity == 0) {
    return false;
  }
  start = reinterpret_cast<uintptr_t>(sCaptureRing.data);
  bytes = sCaptureRing.capacity;
  return true;
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

SC_MEM_ATTR void midiOut(uint8_t type, uint8_t ch, uint8_t d1, uint8_t d2, uint32_t tick) {
  if (shouldSampleMidiOutCapture()) {
    return;
  }
  emitCapPrintf("#CAP,%lu,MO,%u,%u,%u,%u,%lu", (unsigned long)micros(), type, ch, d1, d2,
                (unsigned long)tick);
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
  midiOut((uint8_t)e.type, e.channel, d1, d2, e.tick);
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
  incrementSaturated(sCaptureRing.recordStart.entered);
  emitCapPrintf("#CAP,%lu,RECA,%u,%lu\r\n", (unsigned long)micros(), slot, (unsigned long)tick);
}

SC_MEM_ATTR void recStop(const char* kind, uint8_t slot, uint32_t tick, uint32_t startTick,
                         uint32_t rawLength, uint32_t finalLength, bool align) {
  incrementSaturated(sCaptureRing.recordStop.entered);
  emitCapPrintf("#CAP,%lu,RECS,%s,%u,%lu,%lu,%lu,%lu,%d\r\n", (unsigned long)micros(), kind, slot,
                (unsigned long)tick, (unsigned long)startTick, (unsigned long)rawLength,
                (unsigned long)finalLength, align ? 1 : 0);
}

SC_MEM_ATTR void recStopStage(const char* stage, uint32_t elapsedUs, uint32_t durationUs,
                              uint32_t heapBefore, uint32_t heapAfter, size_t eventCount,
                              size_t chunkRefCount, const char* outcome) {
  incrementSaturated(sCaptureRing.recordStop.entered);
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

SC_MEM_ATTR bool emitTraceFence(const char* nonce) {
  // Fixed validated token, no ring fallback and no transport gate.
  if (nonce == nullptr) return false;
  for (size_t i = 0; i < 32; ++i) {
    const char ch = nonce[i];
    if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
  }
  if (nonce[32] != '\0' || !serialWriteRoom(96)) return false;
  Serial.printf_P(SC_FMT("#CAP,%lu,TRACE_FENCE,%s\r\n"),
                  static_cast<unsigned long>(micros()), nonce);
  return true;
}

SC_MEM_ATTR bool emitTrackStatus(uint8_t track, uint8_t slot, const char* state,
                                  bool transportRunning) {
  // Called by the read-only serial command owner, never on a clock edge.
  // Do not enqueue an old reply: the next request must observe fresh scalars.
  if (!serialWriteRoom(96)) return false;
  Serial.printf_P(SC_FMT("#TRACK_STATUS,us=%lu,track=%u,slot=%u,state=%s,transport=%u\r\n"),
                  static_cast<unsigned long>(micros()), static_cast<unsigned>(track),
                  static_cast<unsigned>(slot), state, transportRunning ? 1u : 0u);
  return true;
}

SC_MEM_ATTR bool emitOverlayStatus(bool open, uint8_t mode, uint8_t row,
                                    uint16_t setId, uint16_t revisionId, uint8_t phase) {
  // Request-only scalar observation. Never drain or queue a stale response.
  // Maximum-width output including CRLF is <128 bytes.
  if (!serialWriteRoom(128)) return false;
  Serial.printf_P(SC_FMT("#OVERLAY_STATUS,us=%lu,open=%u,mode=%u,row=%u,set=%u,rev=%u,phase=%u\r\n"),
                  static_cast<unsigned long>(micros()), open ? 1u : 0u,
                  static_cast<unsigned>(mode), static_cast<unsigned>(row),
                  static_cast<unsigned>(setId), static_cast<unsigned>(revisionId),
                  static_cast<unsigned>(phase));
  return true;
}

// S1.3 save/restore-survival: the read-back for the bounded identity-path counters. One fixed-size
// record, emitted only from the stopped-only HITL command, so the record/persist path never prints.
SC_MEM_ATTR void emitRecordFamilyStats(const char* family, const RecordFamilyStats& stats) {
  Serial.printf(SC_FMT("#CAP,%lu,RING,record_stats,%s,entered=%lu,enq=%lu,rej=%lu,tx=%lu,evict=%lu\r\n"),
                (unsigned long)micros(), family, (unsigned long)stats.entered,
                (unsigned long)stats.enqueued, (unsigned long)stats.rejected,
                (unsigned long)stats.transmitted, (unsigned long)stats.evicted);
}

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
  emitRecordFamilyStats("RECA", sCaptureRing.recordStart);
  emitRecordFamilyStats("RECS", sCaptureRing.recordStop);
  emitRecordFamilyStats("DIAG,latch", sCaptureRing.interactionLatch);
  // Ring-level accounting: attempts/appends separate "the emitters never ran" from "the appends
  // were refused", and wipes name the one content-discarding path that has no eviction outcome.
  Serial.printf(SC_FMT("#CAP,%lu,RING,stats,attempts=%lu,ok=%lu,flush=%lu,tx=%lu,shed=%lu,"
                       "tierA_evict=%lu,overflow=%lu,crit_rej=%lu,wipe=%lu,wipe_bytes=%lu\r\n"),
                static_cast<unsigned long>(micros()),
                static_cast<unsigned long>(sCaptureRing.appendAttempts),
                static_cast<unsigned long>(sCaptureRing.appendsOk),
                static_cast<unsigned long>(sCaptureRing.flushCalls),
                static_cast<unsigned long>(sCaptureRing.flushTransmitted),
                static_cast<unsigned long>(sCaptureRing.shedTotal),
                static_cast<unsigned long>(sCaptureRing.tierAEvictedTotal),
                static_cast<unsigned long>(sCaptureRing.overflowTotal),
                static_cast<unsigned long>(sCaptureRing.criticalRejectedTotal),
                static_cast<unsigned long>(sCaptureRing.silentWipes),
                static_cast<unsigned long>(sCaptureRing.silentWipeBytes));
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

// S7.5a envelope instruments for the runtime continuity envelope, as **one aggregated row per telemetry window**
// (never one row per captured event): the captured-event stamp error in **ticks** (max and how many appends were
// sampled) and the capture-replenishment owner's own request-to-completion accounting (max wait and work in µs,
// refused attempts, completed pairs). Emitted directly when USB has room with the ring as the fallback — the
// same two rules as `readinessReceiptImmediate`, because these are raised while a transport runs, exactly the
// regime whose shed path drops non-Tier-A head records.
SC_MEM_ATTR void envelopeInstrRow(uint32_t captureStampMaxTicks, uint32_t captureStampSamples,
                                 uint32_t replenishWaitUs, uint32_t replenishWorkUs,
                                 uint32_t replenishRefused, uint32_t replenishPairs) {
  if (serialWriteRoom(160)) {
    Serial.printf(SC_FMT("#CAP,%lu,DIAG,envelope_instr,capstamp_max=%lu,capstamp_n=%lu,repl_wait_us=%lu,"
                         "repl_work_us=%lu,repl_refused=%lu,repl_pairs=%lu\r\n"),
                  static_cast<unsigned long>(micros()),
                  static_cast<unsigned long>(captureStampMaxTicks),
                  static_cast<unsigned long>(captureStampSamples),
                  static_cast<unsigned long>(replenishWaitUs),
                  static_cast<unsigned long>(replenishWorkUs),
                  static_cast<unsigned long>(replenishRefused),
                  static_cast<unsigned long>(replenishPairs));
    return;
  }
  emitCapPrintf(SC_FMT("#CAP,%lu,DIAG,envelope_instr,capstamp_max=%lu,capstamp_n=%lu,repl_wait_us=%lu,"
                       "repl_work_us=%lu,repl_refused=%lu,repl_pairs=%lu\r\n"),
                static_cast<unsigned long>(micros()),
                static_cast<unsigned long>(captureStampMaxTicks),
                static_cast<unsigned long>(captureStampSamples),
                static_cast<unsigned long>(replenishWaitUs),
                static_cast<unsigned long>(replenishWorkUs),
                static_cast<unsigned long>(replenishRefused),
                static_cast<unsigned long>(replenishPairs));
}

// Task 4.20 allocation census — one aggregated row per caller-decided interval (the caller rate-limits; nothing here
// samples). Same two delivery rules as `envelopeInstrRow`/`readinessReceiptImmediate`: written directly when USB has
// room, otherwise enqueued on the ring, because the census is worth most while a transport runs and a merely enqueued
// Tier-A row arrives only after the stop. Field meanings, once, here rather than in the row:
//   heap/hmin — internal-heap free now, and the watermark floor since boot (`min_ever`);
//   chunks    — free pool chunks; cap/edit/undo — capture passes, edit passes, undo entries (cardinality);
//   ib        — internal-first resident bytes (edit payloads + undo nested ids + live session lists);
//   eb/ub     — the edit and undo shares of `ib`, so a growth can be attributed to one class;
//   xb        — external-first resident bytes (pass vectors + chunk directories), which are PSRAM on the device.
SC_MEM_ATTR void allocationCensusRow(uint32_t heapFreeBytes, uint32_t heapMinFreeBytes,
                                     uint16_t chunksFree, uint16_t capturePasses, uint16_t editPasses,
                                     uint16_t undoEntries, uint32_t internalBytes,
                                     uint32_t editPassBytes, uint32_t undoIdBytes,
                                     uint32_t externalBytes) {
  if (serialWriteRoom(160)) {
    Serial.printf(SC_FMT("#CAP,%lu,DIAG,alloc_census,heap=%lu,hmin=%lu,chunks=%u,cap=%u,edit=%u,"
                         "undo=%u,ib=%lu,eb=%lu,ub=%lu,xb=%lu\r\n"),
                  static_cast<unsigned long>(micros()),
                  static_cast<unsigned long>(heapFreeBytes),
                  static_cast<unsigned long>(heapMinFreeBytes), static_cast<unsigned>(chunksFree),
                  static_cast<unsigned>(capturePasses), static_cast<unsigned>(editPasses),
                  static_cast<unsigned>(undoEntries), static_cast<unsigned long>(internalBytes),
                  static_cast<unsigned long>(editPassBytes),
                  static_cast<unsigned long>(undoIdBytes),
                  static_cast<unsigned long>(externalBytes));
    return;
  }
  emitCapPrintf(SC_FMT("#CAP,%lu,DIAG,alloc_census,heap=%lu,hmin=%lu,chunks=%u,cap=%u,edit=%u,"
                       "undo=%u,ib=%lu,eb=%lu,ub=%lu,xb=%lu\r\n"),
                static_cast<unsigned long>(micros()), static_cast<unsigned long>(heapFreeBytes),
                static_cast<unsigned long>(heapMinFreeBytes), static_cast<unsigned>(chunksFree),
                static_cast<unsigned>(capturePasses), static_cast<unsigned>(editPasses),
                static_cast<unsigned>(undoEntries), static_cast<unsigned long>(internalBytes),
                static_cast<unsigned long>(editPassBytes),
                static_cast<unsigned long>(undoIdBytes),
                static_cast<unsigned long>(externalBytes));
}

// Task 4.20 allocatability probe — on demand only (`!HEAP_PROBE`), never per turn: each row costs a real allocation,
// which is the point (it is the one measurement the arena-tail metric cannot make). Two delivery rules again: the
// probe is most useful right where a denial is being felt, which can be under transport, so it writes directly when
// USB has room and falls back to the ring otherwise.
SC_MEM_ATTR void heapProbeRow(uint32_t tailFreeBytes, uint32_t requestedBytes, bool internalMallocOk,
                              bool externalPoolOk) {
  if (serialWriteRoom(120)) {
    Serial.printf(SC_FMT("#CAP,%lu,DIAG,heap_probe,tail=%lu,want=%lu,malloc=%u,pool=%u\r\n"),
                  static_cast<unsigned long>(micros()),
                  static_cast<unsigned long>(tailFreeBytes),
                  static_cast<unsigned long>(requestedBytes), internalMallocOk ? 1u : 0u,
                  externalPoolOk ? 1u : 0u);
    return;
  }
  emitCapPrintf(SC_FMT("#CAP,%lu,DIAG,heap_probe,tail=%lu,want=%lu,malloc=%u,pool=%u\r\n"),
                static_cast<unsigned long>(micros()),
                static_cast<unsigned long>(tailFreeBytes),
                static_cast<unsigned long>(requestedBytes), internalMallocOk ? 1u : 0u,
                externalPoolOk ? 1u : 0u);
}

// Task 4.20 denial-edge capacity ladder. Cold by construction (its caller has already decided the turn is being
// denied) and cooldown-bounded there; same two delivery rules as its siblings, because a denial is felt under transport.
SC_MEM_ATTR void heapLadderRow(uint32_t tailFreeBytes, uint32_t needBytes, uint32_t largestBytes,
                               uint8_t servedMask) {
  if (serialWriteRoom(120)) {
    Serial.printf(SC_FMT("#CAP,%lu,DIAG,heap_probe,ladder,tail=%lu,need=%lu,largest=%lu,mask=%u\r\n"),
                  static_cast<unsigned long>(micros()),
                  static_cast<unsigned long>(tailFreeBytes), static_cast<unsigned long>(needBytes),
                  static_cast<unsigned long>(largestBytes), static_cast<unsigned>(servedMask));
    return;
  }
  emitCapPrintf(SC_FMT("#CAP,%lu,DIAG,heap_probe,ladder,tail=%lu,need=%lu,largest=%lu,mask=%u\r\n"),
                static_cast<unsigned long>(micros()), static_cast<unsigned long>(tailFreeBytes),
                static_cast<unsigned long>(needBytes), static_cast<unsigned long>(largestBytes),
                static_cast<unsigned>(servedMask));
}

// S7.5a distribution instrument — the line hook the header describes: the rows are formatted in
// `Utils/EnvelopeDistRows.cpp` (DTCM-carved for its literals) and handed to this unit's ring here.
SC_MEM_ATTR void emitEnvelopeRowLine(const char* line) {
  emitCapLineOrSerial(line);
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

SC_MEM_ATTR void observePlaybackRead(const PlaybackReadWitness& witness) {
  if (witness.track >= 8 || witness.slot >= 8 ||
      witness.storageGeneration == 0 || witness.origin > 2 || (witness.availableOnly && witness.origin != 0) || witness.length == 0 ||
      witness.start >= witness.period || witness.length > witness.period - witness.start ||
      witness.phase < witness.start || witness.phase - witness.start >= witness.length) return;
  ensureCriticalJournalInitialized();
  auto& box = sPlaybackReads[witness.track][witness.slot];
  if (box.pending || (box.seen && (box.lastTick == witness.tick ||
      uint32_t(witness.atUs - box.lastUs) < 250000u))) return;
  box.witness = witness; box.lastUs = witness.atUs; box.lastTick = witness.tick;
  box.seen = true; box.pending = true;
}

SC_MEM_ATTR void flushPlaybackReadWitness(bool allowUsb) {
  if (!sCriticalJournalInitialized) return;
  // Fixed 64-descriptor scan, at most one bounded row per foreground flush.
  for (unsigned n = 0; n < 64; ++n) {
    const uint8_t index = sPlaybackReadScan;
    sPlaybackReadScan = (sPlaybackReadScan + 1u) % 64u;
    auto& box = sPlaybackReads[index / 8][index % 8];
    if (!box.pending) continue;
    const auto& w = box.witness;
    char row[160];
    const char* format = w.availableOnly
        ? SC_FMT("#CAP,%lu,DIAG,playback_available,1,%u,%u,%lu,%lu,%llu,%u,%lu,%lu,%lu,%lu")
        : SC_FMT("#CAP,%lu,DIAG,playback_read,1,%u,%u,%lu,%lu,%llu,%u,%lu,%lu,%lu,%lu");
    snprintf(row, sizeof(row), format,
        (unsigned long)w.atUs, w.track, w.slot, (unsigned long)w.tick,
        (unsigned long)w.generation, (unsigned long long)w.storageGeneration, w.origin,
        (unsigned long)w.start, (unsigned long)w.length, (unsigned long)w.period, (unsigned long)w.phase);
    if (allowUsb && serialWriteRoom(sizeof(row))) {
      box.pending = false; // before Serial can yield/reenter the producer
      sFlushInProgress = true;
      Serial.println(row);
      sFlushInProgress = false;
    } else if (queueCaptureTextLine(row)) {
      box.pending = false;
    }
    return;
  }
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

SC_MEM_ATTR void playbackBuildRow(uint8_t track, uint8_t slot, uint32_t loopLengthTicks, uint32_t revision,
                                  const char* path) {
  // Exactly the call the lane used to make inline, so the row is byte-for-byte what a capture build produced before the
  // seam existed; `windowStartTick` and `rebuildCount` stay 0 because the lane does not track them.
  playbackRebuild(0, loopLengthTicks, 0, revision, track, slot, 0, path);
}

#if SC_QUALIFICATION_ROWS_ENABLED
namespace {

/// The row's flag bits. 10 and 11 are the ones a **call site** supplies through
/// `Track::reportPlaybackLaunchGateRow`'s two booleans: this turn attempted `requestCommittedPassesRepublish`, and
/// whether that request was accepted -- `queueAllCommittedPassesInto` returning false is the silent failure that made
/// five device runs indistinguishable from "nothing was owed".
constexpr uint16_t kLaunchGateRequestAttempted = 1u << 10;
constexpr uint16_t kLaunchGateRequestAccepted = 1u << 11;

/// One `0|1` field for a flag bit, so the row reads without a legend.
unsigned launchGateBit(uint16_t flags, unsigned bit) {
  return (flags & static_cast<uint16_t>(1u << bit)) != 0 ? 1u : 0u;
}

/// `req` as the wire's three states: 0 nothing attempted, 1 accepted (the source is now owed), 2 refused.
unsigned launchGateRequestCode(uint16_t flags) {
  if ((flags & kLaunchGateRequestAttempted) == 0) return 0u;
  return (flags & kLaunchGateRequestAccepted) != 0 ? 1u : 2u;
}

}  // namespace

SC_MEM_ATTR void launchGateRow(uint8_t track, uint8_t slot, const char* exit, uint16_t flags,
                              uint32_t loopLengthTicks, uint8_t launchPreparation) {
  // The **row** and nothing else: which rows are owed -- the cadence -- is the caller's own state
  // (`Track::reportPlaybackLaunchGateRow`), so the bound is production logic a native suite can observe rather than a
  // property of this transport.
  // Flag bits: 0 root, 1 pend, 2 role, 3 own, 4 dem, 5 build-active, 6 build-coverage-owned, 7 completed-promise,
  // 8 avoid-full-rebuild, 9 has-committed-passes, 10 request-attempted, 11 request-accepted. `bld` is printed as its
  // three readable states rather than as two bits.
  const unsigned buildState = launchGateBit(flags, 6) != 0 ? 2u : launchGateBit(flags, 5);
  // **Two rules, the save-completion receipt's own** (`readinessReceiptImmediate`): emitted **directly when USB has
  // room**, and enqueued on the ring otherwise, so the row is **delayed rather than lost**. It needs the direct path
  // for the same measured reason the receipt did: the decision it reports is taken **under transport** (a queued switch
  // is prepared by the off-clock `SlotProvision` turn while the track plays), and the ring is **retained by design**
  // while a transport runs -- so a row merely enqueued during a leg arrives only after the transport stops, after the
  // launch it explains. This is not a precaution: two device episodes carried the row in the firmware and delivered
  // **none** of them (the Tier-A classification alone did not save them).
  if (serialWriteRoom(160)) {
    Serial.printf(SC_FMT("#CAP,%lu,DIAG,launch_gate,t=%u,s=%u,x=%s,len=%lu,prep=%u,root=%u,pend=%u,role=%u,own=%u,"
                         "dem=%u,bld=%u,cpm=%u,avoid=%u,cmt=%u,req=%u\r\n"),
                  static_cast<unsigned long>(micros()), static_cast<unsigned>(track),
                  static_cast<unsigned>(slot), exit == nullptr ? "?" : exit,
                  static_cast<unsigned long>(loopLengthTicks), static_cast<unsigned>(launchPreparation),
                  launchGateBit(flags, 0), launchGateBit(flags, 1), launchGateBit(flags, 2), launchGateBit(flags, 3),
                  launchGateBit(flags, 4), buildState, launchGateBit(flags, 7), launchGateBit(flags, 8),
                  launchGateBit(flags, 9), launchGateRequestCode(flags));
    return;
  }
  emitCapPrintf(SC_FMT("#CAP,%lu,DIAG,launch_gate,t=%u,s=%u,x=%s,len=%lu,prep=%u,root=%u,pend=%u,role=%u,own=%u,"
                       "dem=%u,bld=%u,cpm=%u,avoid=%u,cmt=%u,req=%u\r\n"),
                static_cast<unsigned long>(micros()), static_cast<unsigned>(track),
                static_cast<unsigned>(slot), exit == nullptr ? "?" : exit,
                static_cast<unsigned long>(loopLengthTicks), static_cast<unsigned>(launchPreparation),
                launchGateBit(flags, 0), launchGateBit(flags, 1), launchGateBit(flags, 2), launchGateBit(flags, 3),
                launchGateBit(flags, 4), buildState, launchGateBit(flags, 7), launchGateBit(flags, 8),
                launchGateBit(flags, 9), launchGateRequestCode(flags));
}

SC_MEM_ATTR void snapshotGateRow(uint8_t track, uint8_t selectedSlot, uint8_t activeSlot, uint16_t verdicts,
                                uint8_t constructionUnits, uint32_t eligibleTurns, uint16_t inFlightUnits,
                                uint32_t publishedRevision) {
  // The same **two delivery rules** as `launchGateRow` (and as the save-completion receipt), for the same measured
  // reason: this row is raised while the transport runs, and the ring is retained by design then. The row exists only
  // where its caller does -- which rows are owed, and on which state change, is production state
  // (`Track::reportSnapshotOwnerGateRow`). Bit order: see the declaration in the header.
  if (serialWriteRoom(200)) {
    Serial.printf(SC_FMT("#CAP,%lu,DIAG,snap_gate,t=%u,s=%u,a=%u,u=%u,p=%u,sep=%u,fc=%u,wj=%u,hp=%u,lr=%u,vc=%u,"
                         "rec=%u,play=%u,od=%u,step=%u,cl=%u,idle=%u,elig=%u,st=%lu,cu=%u,rv=%lu\r\n"),
                  static_cast<unsigned long>(micros()), static_cast<unsigned>(track),
                  static_cast<unsigned>(selectedSlot), static_cast<unsigned>(activeSlot),
                  static_cast<unsigned>(constructionUnits),
                  launchGateBit(verdicts, 0), launchGateBit(verdicts, 1), launchGateBit(verdicts, 2),
                  launchGateBit(verdicts, 3), launchGateBit(verdicts, 4), launchGateBit(verdicts, 5),
                  launchGateBit(verdicts, 6), launchGateBit(verdicts, 7), launchGateBit(verdicts, 8),
                  launchGateBit(verdicts, 9), launchGateBit(verdicts, 10), launchGateBit(verdicts, 11),
                  launchGateBit(verdicts, 12), launchGateBit(verdicts, 13), static_cast<unsigned long>(eligibleTurns),
                  static_cast<unsigned>(inFlightUnits), static_cast<unsigned long>(publishedRevision));
    return;
  }
  emitCapPrintf(SC_FMT("#CAP,%lu,DIAG,snap_gate,t=%u,s=%u,a=%u,u=%u,p=%u,sep=%u,fc=%u,wj=%u,hp=%u,lr=%u,vc=%u,"
                       "rec=%u,play=%u,od=%u,step=%u,cl=%u,idle=%u,elig=%u,st=%lu,cu=%u,rv=%lu\r\n"),
                static_cast<unsigned long>(micros()), static_cast<unsigned>(track),
                static_cast<unsigned>(selectedSlot), static_cast<unsigned>(activeSlot),
                static_cast<unsigned>(constructionUnits),
                launchGateBit(verdicts, 0), launchGateBit(verdicts, 1), launchGateBit(verdicts, 2),
                launchGateBit(verdicts, 3), launchGateBit(verdicts, 4), launchGateBit(verdicts, 5),
                launchGateBit(verdicts, 6), launchGateBit(verdicts, 7), launchGateBit(verdicts, 8),
                launchGateBit(verdicts, 9), launchGateBit(verdicts, 10), launchGateBit(verdicts, 11),
                launchGateBit(verdicts, 12), launchGateBit(verdicts, 13), static_cast<unsigned long>(eligibleTurns),
                static_cast<unsigned>(inFlightUnits), static_cast<unsigned long>(publishedRevision));
}

SC_MEM_ATTR void winGateRow(uint8_t track, uint8_t slot, const char* exit, uint16_t flags, uint32_t loopLengthTicks,
                            uint8_t requestResult) {
  // The **row** and nothing else: which rows are owed -- the cadence -- is the caller's own state
  // (`Track::reportPlaybackRollingGateRow`), so the bound is production logic a native suite can observe rather than
  // a property of this transport. The same **two delivery rules** as `launchGateRow`/`snapshotGateRow` (and as the
  // save-completion receipt), for the same measured reason: the decision this row reports is taken while the
  // transport runs -- that is when the lane is dispatched at all -- and the ring is *retained by design* then, so a
  // row merely enqueued during a leg arrives only after the transport stops, after the demand it explains.
  // Bit order: see the declaration in the header (bits 0..15, plus `rq` as the request's own ordinal).
  if (serialWriteRoom(160)) {
    Serial.printf(SC_FMT("#CAP,%lu,DIAG,win_gate,t=%u,s=%u,x=%s,len=%lu,ep=%u,av=%u,al=%u,pl=%u,ed=%u,od=%u,pv=%u,"
                         "rw=%u,sv=%u,rt=%u,rl=%u,ow=%u,dm=%u,bld=%u,gr=%u,rp=%u,rq=%u\r\n"),
                  static_cast<unsigned long>(micros()), static_cast<unsigned>(track), static_cast<unsigned>(slot),
                  exit == nullptr ? "?" : exit, static_cast<unsigned long>(loopLengthTicks),
                  launchGateBit(flags, 0), launchGateBit(flags, 1), launchGateBit(flags, 2), launchGateBit(flags, 3),
                  launchGateBit(flags, 4), launchGateBit(flags, 5), launchGateBit(flags, 6), launchGateBit(flags, 7),
                  launchGateBit(flags, 8), launchGateBit(flags, 9), launchGateBit(flags, 10), launchGateBit(flags, 11),
                  launchGateBit(flags, 12), launchGateBit(flags, 13), launchGateBit(flags, 14), launchGateBit(flags, 15),
                  static_cast<unsigned>(requestResult));
    return;
  }
  emitCapPrintf(SC_FMT("#CAP,%lu,DIAG,win_gate,t=%u,s=%u,x=%s,len=%lu,ep=%u,av=%u,al=%u,pl=%u,ed=%u,od=%u,pv=%u,"
                       "rw=%u,sv=%u,rt=%u,rl=%u,ow=%u,dm=%u,bld=%u,gr=%u,rp=%u,rq=%u\r\n"),
                static_cast<unsigned long>(micros()), static_cast<unsigned>(track), static_cast<unsigned>(slot),
                exit == nullptr ? "?" : exit, static_cast<unsigned long>(loopLengthTicks), launchGateBit(flags, 0),
                launchGateBit(flags, 1), launchGateBit(flags, 2), launchGateBit(flags, 3), launchGateBit(flags, 4),
                launchGateBit(flags, 5), launchGateBit(flags, 6), launchGateBit(flags, 7), launchGateBit(flags, 8),
                launchGateBit(flags, 9), launchGateBit(flags, 10), launchGateBit(flags, 11), launchGateBit(flags, 12),
                launchGateBit(flags, 13), launchGateBit(flags, 14), launchGateBit(flags, 15),
                static_cast<unsigned>(requestResult));
}
#endif  // SC_QUALIFICATION_ROWS_ENABLED

SC_MEM_ATTR void playbackOrder(uint32_t durationUs, uint32_t eventCount) {
  emitCapPrintf("#CAP,%lu,DIAG,playback_order,%lu,%lu\r\n", (unsigned long)micros(),
                (unsigned long)durationUs, (unsigned long)eventCount);
}

SC_MEM_ATTR void successorStartWindowOpen(uint8_t slot, uint32_t stopTick, uint32_t loopLengthTicks) {
  emitCapPrintf("#CAP,%lu,DIAG,reentry_window_open,%lu,%lu,%lu\r\n", (unsigned long)micros(),
                (unsigned long)slot, (unsigned long)stopTick, (unsigned long)loopLengthTicks);
}

SC_MEM_ATTR void canonicalFinalizationCapacityReleased(uint8_t slot, uint32_t generation,
                                                      uint32_t loopLengthTicks,
                                                      size_t availableChunks) {
  emitCapPrintf("#CAP,%lu,DIAG,canonical_release,%u,%lu,%lu,%lu\r\n", (unsigned long)micros(),
                static_cast<unsigned>(slot), (unsigned long)generation,
                (unsigned long)loopLengthTicks, (unsigned long)availableChunks);
}

SC_MEM_ATTR void canonicalFinalizationCapacityReleasedForInterval(
    uint8_t slot, uint64_t interval, uint32_t generation, uint32_t loopLengthTicks,
    size_t availableChunks) {
  emitCapPrintf("#CAP,%lu,DIAG,canon_rel_interval,%u,%llu,%lu,%lu,%lu\r\n",
                (unsigned long)micros(), static_cast<unsigned>(slot),
                static_cast<unsigned long long>(interval),
                (unsigned long)generation, (unsigned long)loopLengthTicks,
                (unsigned long)availableChunks);
}

SC_MEM_ATTR void workingSetLeaseReleased(uint8_t slot, uint16_t generation, const char* outcome) {
  emitCapPrintf(SC_FMT("#CAP,%lu,DIAG,ws_release,%u,%u,%s\r\n"), (unsigned long)micros(),
                static_cast<unsigned>(slot), static_cast<unsigned>(generation),
                outcome != nullptr ? outcome : "?");
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

SC_MEM_ATTR void paintGate(uint8_t owner, uint8_t allStopped, uint8_t titleDrain, uint8_t clockRunning,
                           uint8_t restorePending, uint8_t commitPending, uint16_t queue,
                           uint16_t focusNotes, uint8_t ownNotes) {
  emitCapPrintf(SC_FMT("#CAP,%lu,DIAG,paint_gate,%u,%u,%u,%u,%u,%u,%u,%u,%u\r\n"),
                (unsigned long)micros(), static_cast<unsigned>(owner),
                static_cast<unsigned>(allStopped), static_cast<unsigned>(titleDrain),
                static_cast<unsigned>(clockRunning), static_cast<unsigned>(restorePending),
                static_cast<unsigned>(commitPending), static_cast<unsigned>(queue),
                static_cast<unsigned>(focusNotes), static_cast<unsigned>(ownNotes));
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
                                  size_t chunkRefCount, const char* outcome, uint8_t track) {
  // `track` is appended last so a positional reader that stops at `outcome` keeps working. `0xFF` is
  // not supplied; see the declaration's note.
  emitCapPrintf("#CAP,%lu,ODUB,stop,%s,%lu,%lu,%lu,%lu,%lu,%lu,%s,track=%u\r\n", (unsigned long)micros(),
                stage, (unsigned long)elapsedUs, (unsigned long)durationUs,
                (unsigned long)heapBefore, (unsigned long)heapAfter, (unsigned long)eventCount,
                (unsigned long)chunkRefCount, outcome, static_cast<unsigned>(track));
}

SC_MEM_ATTR void interactionLatchTransition(const char* transition, uint8_t track, uint8_t slot,
                                            uint64_t interval, uint8_t destination,
                                            const char* outcome) {
  // Counted at the branch, exactly like `recStart`/`recStop`: "the branch was never reached" must stay
  // distinguishable from "the row was enqueued and then lost", which is the whole point of the family.
  incrementSaturated(sCaptureRing.interactionLatch.entered);
  // Low 32 bits, deliberately: `reentryDecision` prints the same quantity the same way, and the device is a
  // 32-bit target where `unsigned long` is 32 bits -- printing the full 64 would read differently there than
  // in a native fixture, which is exactly the divergence a fixture must not have.
  emitCapPrintf(SC_FMT("#CAP,%lu,DIAG,latch,%s,%u,%u,%lu,%u,%s\r\n"), (unsigned long)micros(),
                transition != nullptr ? transition : "?", static_cast<unsigned>(track),
                static_cast<unsigned>(slot), (unsigned long)static_cast<uint32_t>(interval),
                static_cast<unsigned>(destination), outcome != nullptr ? outcome : "?");
}

/// The save-completion receipt, emitted **directly when USB has room**.
///
/// This row is the successor-start receipt the acceptance surface fences on (`PERS,result`), and the ring under
/// transport is *retained by design* rather than drained (see the `kCaptureRingBytes` rationale): with a track
/// playing throughout, a receipt enqueued during the leg is delivered only after the transport stops -- which is
/// after its consumer has given up. Measured 2026-10-01, `captures/s4b9-cross-track-18/-19/-20`: the row exists in
/// the capture 3.1 s (device time) after the record stop while the reader's 25 s **and** 120 s waits both expired.
/// It is rare (a few per boot), so emitting it where it is produced costs no budget; when USB has no room it falls
/// back to the ring, so the row is delayed rather than lost.
SC_MEM_ATTR void persistenceResultImmediate(uint32_t durationUs, uint32_t heapBefore, uint32_t heapAfter,
                                           const char* outcome) {
  if (serialWriteRoom(96)) {
    Serial.printf(SC_FMT("#CAP,%lu,PERS,result,%lu,%lu,%lu,%s\r\n"), (static_cast<unsigned long>(micros())),
                  static_cast<unsigned long>(durationUs), static_cast<unsigned long>(heapBefore),
                  static_cast<unsigned long>(heapAfter), outcome == nullptr ? "?" : outcome);
    return;
  }
  emitCapPrintf("#CAP,%lu,PERS,result,%lu,%lu,%lu,%s\r\n", (static_cast<unsigned long>(micros())),
                static_cast<unsigned long>(durationUs), static_cast<unsigned long>(heapBefore),
                static_cast<unsigned long>(heapAfter), outcome == nullptr ? "?" : outcome);
}

/// The window lane's **readiness receipt** (DEC-108 §13b / task 3.9), delivered by the two rules the save-completion
/// receipt established: emitted **directly when USB has room**, and otherwise enqueued on the ring, so the row is
/// **delayed rather than lost**. The direct path is the point: a window is prepared under transport, and the ring is
/// retained by design while a transport runs, so a receipt enqueued during a leg would arrive only after the
/// transport stops -- after the readiness it explains. Event-shaped and cadence-bounded by its caller.
SC_MEM_ATTR void windowServiceGapImmediate(const PlaybackLaneMeasurement& sample, uint32_t phase,
                                         bool transportRunning, uint32_t epoch) {
  // Schema 3: schema 2 (eligible predecessor max, eligible turns, progress turns, denied turns,
  // worker max us, clock epoch, saturated gaps) plus the **permitted** advance this block compared
  // against, the number of eligible-predecessor gaps that exceeded it, and the worst of them.
  // The limit is part of the row so an observation is self-describing: a reader never has to know
  // the declaration to see whether a gap was an overrun.
  // Aggregate off-clock; no direct USB write inside the timed worker span.
  emitCapPrintf("#CAP,%lu,DIAG,win_gap,%lu,%lu,%lu,%u,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,3\r\n",
      static_cast<unsigned long>(micros()),
      static_cast<unsigned long>(sample.maxDeltaTicks), static_cast<unsigned long>(sample.opportunities),
      static_cast<unsigned long>(phase), static_cast<unsigned>(transportRunning),
      static_cast<unsigned long>(sample.discontinuities), static_cast<unsigned long>(sample.eligiblePredecessorMax),
      static_cast<unsigned long>(sample.eligibleTurns), static_cast<unsigned long>(sample.progressTurns),
      static_cast<unsigned long>(sample.deniedTurns), static_cast<unsigned long>(sample.maxWorkerUs),
      static_cast<unsigned long>(epoch), static_cast<unsigned long>(sample.saturatedGaps),
      static_cast<unsigned long>(sample.permittedAdvanceTicks), static_cast<unsigned long>(sample.overruns),
      static_cast<unsigned long>(sample.overrunMaxTicks));
}

SC_MEM_ATTR void windowGapOverrunImmediate(uint32_t permittedAdvanceTicks, uint32_t overruns, uint32_t overrunMaxTicks,
                                           bool transportRunning, uint32_t epoch) {
  // DEC-108 §13b / task 3.10 (blocker 3): the permitted service-gap limit was **exceeded**, named as its own event.
  // `permitted` travels with the report, so the row says what it was compared against rather than leaving a reader to
  // find the declaration. Off-clock; one row per reporting block at most (the block is the cadence).
  emitCapPrintf("#CAP,%lu,DIAG,win_gap_overrun,%lu,%lu,%lu,%u,%lu\r\n",
      static_cast<unsigned long>(micros()),
      static_cast<unsigned long>(permittedAdvanceTicks), static_cast<unsigned long>(overruns),
      static_cast<unsigned long>(overrunMaxTicks), static_cast<unsigned>(transportRunning),
      static_cast<unsigned long>(epoch));
}

static const char kPhysicalClockObservationFormat[] PROGMEM =
    "#CAP,%lu,DIAG,pit_obs,%lu,%u,%lu,%llu,%llu,%lu,%llu,%lu,%lu,%lu,%lu,%u,%u\r\n";
SC_MEM_ATTR void physicalClockObservationImmediate(uint32_t id, const ClockPhysicalObservation& o) {
  // The worst-case scalar row is <192 bytes, the capture text cap.
  if (serialWriteRoom(192)) {
    Serial.printf(kPhysicalClockObservationFormat, static_cast<unsigned long>(micros()),
        static_cast<unsigned long>(id), static_cast<unsigned>(o.status),
        static_cast<unsigned long>(o.era), static_cast<unsigned long long>(o.rawTicks),
        static_cast<unsigned long long>(o.beforeUs), static_cast<unsigned long>(o.widthUs),
        static_cast<unsigned long long>(o.musicalTick), static_cast<unsigned long>(o.epoch),
        static_cast<unsigned long>(o.readCycles), static_cast<unsigned long>(o.cpuHz),
        static_cast<unsigned long>(o.configBits), static_cast<unsigned>(o.transport), static_cast<unsigned>(o.source));
    return;
  }
  emitCapPrintf(kPhysicalClockObservationFormat, static_cast<unsigned long>(micros()),
      static_cast<unsigned long>(id), static_cast<unsigned>(o.status),
      static_cast<unsigned long>(o.era), static_cast<unsigned long long>(o.rawTicks),
      static_cast<unsigned long long>(o.beforeUs), static_cast<unsigned long>(o.widthUs),
      static_cast<unsigned long long>(o.musicalTick), static_cast<unsigned long>(o.epoch),
      static_cast<unsigned long>(o.readCycles), static_cast<unsigned long>(o.cpuHz),
      static_cast<unsigned long>(o.configBits), static_cast<unsigned>(o.transport), static_cast<unsigned>(o.source));
}

static const char kPhysicalClockMotionFormat[] PROGMEM =
    "#CAP,%lu,DIAG,pit_motion,%lu,%u,%lu,%llu,%llu,%lu,%llu,%lu,%lu,%lu,%lu,%u,%u\r\n";
SC_MEM_ATTR void physicalClockMotionImmediate(uint32_t id, const ClockPhysicalObservation& o, uint32_t guardBits) {
  if (serialWriteRoom(192)) {
    Serial.printf(kPhysicalClockMotionFormat, static_cast<unsigned long>(micros()),
        static_cast<unsigned long>(id), static_cast<unsigned>(o.status),
        static_cast<unsigned long>(o.era), static_cast<unsigned long long>(o.rawTicks),
        static_cast<unsigned long long>(o.beforeUs), static_cast<unsigned long>(o.widthUs),
        static_cast<unsigned long long>(o.musicalTick), static_cast<unsigned long>(o.epoch),
        static_cast<unsigned long>(o.periodUs), static_cast<unsigned long>(guardBits),
        static_cast<unsigned long>(o.configBits), static_cast<unsigned>(o.transport), static_cast<unsigned>(o.source));
    return;
  }
  emitCapPrintf(kPhysicalClockMotionFormat, static_cast<unsigned long>(micros()),
      static_cast<unsigned long>(id), static_cast<unsigned>(o.status),
      static_cast<unsigned long>(o.era), static_cast<unsigned long long>(o.rawTicks),
      static_cast<unsigned long long>(o.beforeUs), static_cast<unsigned long>(o.widthUs),
      static_cast<unsigned long long>(o.musicalTick), static_cast<unsigned long>(o.epoch),
      static_cast<unsigned long>(o.periodUs), static_cast<unsigned long>(guardBits),
      static_cast<unsigned long>(o.configBits), static_cast<unsigned>(o.transport), static_cast<unsigned>(o.source));
}

#if MIDI_INGRESS_COST_QUALIFICATION
static const char kIngressCostFormat[] PROGMEM =
    "#CAP,%lu,DIAG,ingress_cost,%lu,%u,%u,%lu,%lu,%lu,%lu,%lu,%lu,%llu,%u,%lu\r\n";
SC_MEM_ATTR void ingressCostImmediate(uint32_t id, uint8_t route, uint8_t lane,
    uint32_t cpuHz, uint32_t guardBits, const MidiIngress::IngressCostStats& s,
    uint32_t contractBits) {
  if (serialWriteRoom(192)) {
    Serial.printf(kIngressCostFormat, static_cast<unsigned long>(micros()),
        static_cast<unsigned long>(id), static_cast<unsigned>(route), static_cast<unsigned>(lane),
        static_cast<unsigned long>(cpuHz), static_cast<unsigned long>(guardBits),
        static_cast<unsigned long>(s.accepted), static_cast<unsigned long>(s.rejected),
        static_cast<unsigned long>(s.minimumCycles), static_cast<unsigned long>(s.maximumCycles),
        static_cast<unsigned long long>(s.sumCycles), static_cast<unsigned>(s.saturated),
        static_cast<unsigned long>(contractBits));
    return;
  }
  emitCapPrintf(kIngressCostFormat, static_cast<unsigned long>(micros()),
      static_cast<unsigned long>(id), static_cast<unsigned>(route), static_cast<unsigned>(lane),
      static_cast<unsigned long>(cpuHz), static_cast<unsigned long>(guardBits),
      static_cast<unsigned long>(s.accepted), static_cast<unsigned long>(s.rejected),
      static_cast<unsigned long>(s.minimumCycles), static_cast<unsigned long>(s.maximumCycles),
      static_cast<unsigned long long>(s.sumCycles), static_cast<unsigned>(s.saturated),
      static_cast<unsigned long>(contractBits));
}
#endif

SC_MEM_ATTR void readinessReceiptImmediate(uint8_t track, uint8_t slot, const char* readiness, const char* holder,
                                           uint32_t revision, uint32_t leadTicks) {
  if (serialWriteRoom(96)) {
    Serial.printf(SC_FMT("#CAP,%lu,DIAG,win_readiness,%u,%u,%s,%s,%lu,%lu\r\n"),
                  static_cast<unsigned long>(micros()), static_cast<unsigned>(track),
                  static_cast<unsigned>(slot), readiness == nullptr ? "?" : readiness,
                  holder == nullptr ? "?" : holder, static_cast<unsigned long>(revision),
                  static_cast<unsigned long>(leadTicks));
    return;
  }
  emitCapPrintf("#CAP,%lu,DIAG,win_readiness,%u,%u,%s,%s,%lu,%lu\r\n", static_cast<unsigned long>(micros()),
                static_cast<unsigned>(track), static_cast<unsigned>(slot), readiness == nullptr ? "?" : readiness,
                holder == nullptr ? "?" : holder, static_cast<unsigned long>(revision),
                static_cast<unsigned long>(leadTicks));
}

/// The frozen Record-return receipt's **ring-path** format, placed in memory-mapped FLASH (`PROGMEM` -> the
/// `.progmem` carve-out the linker script already routes to `FLASH`) rather than left as a `.rodata.str*` literal in
/// DTCM. `.rodata` is DTCM in this script, and this TU's newly added strings were ~150 B past the
/// `_VectorsRam`/periodictable alignment edge -- enough to snap the RAM1 `variables` figure by 4 KB and take it out of
/// the local allowance (`variables` 96,192 -> 100,288 with the ITCM `code` figure unchanged). The direct path's
/// literal already travels to FLASH because `SC_FMT` is `PSTR` on this target; this is the same treatment for the
/// ring path, so no linker-script carve-out and no budget re-base is needed.
/// DTCM. `.rodata` is DTCM in this script, and this TU's newly added strings were ~150 B past the
/// `_VectorsRam`/periodictable alignment edge -- enough to snap the RAM1 `variables` figure by 4 KB and take it out of
/// the local allowance (`variables` 96,192 -> 100,288 with the ITCM `code` figure unchanged). The direct path's
/// literal already travels to FLASH because `SC_FMT` is `PSTR` on this target; this is the same treatment for the
/// ring path, so no linker-script carve-out and no budget re-base is needed.
static const char kFrozenReadinessRowFormat[] PROGMEM =
    "#CAP,%lu,DIAG,win_frozen_readiness,%u,%u,%s,%s,%lu,%lu,%lu,%llu,%lu,%lu,%lu,%u,%u,%u\r\n";

/// The frozen Record-return's **source-qualified** readiness receipt. Same two delivery rules as
/// `readinessReceiptImmediate` -- direct when USB has room, ring otherwise -- so a receipt produced under transport is
/// delayed rather than lost; `win_frozen_readiness` is its own family so the windowed lane can require a
/// source-qualified receipt instead of accepting the generic `not_servable` row. The capture token is emitted in full
/// (`%llu`), so a reader compares it against the frozen job's generation without truncation arithmetic.
SC_MEM_ATTR void frozenReadinessReceiptImmediate(uint8_t track, uint8_t slot, const char* readiness, const char* holder,
                                                uint32_t revision, uint32_t leadTicks, uint32_t sourceGeneration,
                                                uint64_t captureToken, uint32_t start, uint32_t length,
                                                uint32_t loopLength, uint8_t adoptMask, uint8_t adoptResult,
                                                uint8_t adoptWhy) {
  if (serialWriteRoom(160)) {
    // `.progmem` is memory-mapped on this target, so the FLASH-resident format is read directly -- the same
    // treatment `SC_FMT` (`PSTR`) already gives the sibling receipt's direct path.
    Serial.printf(kFrozenReadinessRowFormat,
                  static_cast<unsigned long>(micros()), static_cast<unsigned>(track),
                  static_cast<unsigned>(slot), readiness == nullptr ? "?" : readiness,
                  holder == nullptr ? "?" : holder, static_cast<unsigned long>(revision),
                  static_cast<unsigned long>(leadTicks), static_cast<unsigned long>(sourceGeneration),
                  static_cast<unsigned long long>(captureToken), static_cast<unsigned long>(start),
                  static_cast<unsigned long>(length), static_cast<unsigned long>(loopLength),
                  static_cast<unsigned>(adoptMask), static_cast<unsigned>(adoptResult),
                  static_cast<unsigned>(adoptWhy));
    return;
  }
  emitCapPrintf(kFrozenReadinessRowFormat,
                static_cast<unsigned long>(micros()), static_cast<unsigned>(track),
                static_cast<unsigned>(slot), readiness == nullptr ? "?" : readiness,
                holder == nullptr ? "?" : holder, static_cast<unsigned long>(revision),
                static_cast<unsigned long>(leadTicks), static_cast<unsigned long>(sourceGeneration),
                static_cast<unsigned long long>(captureToken), static_cast<unsigned long>(start),
                static_cast<unsigned long>(length), static_cast<unsigned long>(loopLength),
                static_cast<unsigned>(adoptMask), static_cast<unsigned>(adoptResult),
                static_cast<unsigned>(adoptWhy));
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

/// The display window's **transition** receipt (`DIAG,disp_window`). Format kept in `.progmem` for the reason the frozen
/// receipt's format is: `.rodata` is DTCM in this script, and a few hundred bytes of new literals in this unit can snap
/// the RAM1 `variables` figure by 4 KB while ITCM `code` is unchanged.
static const char kDisplayWindowTransitionFormat[] PROGMEM =
    "#CAP,%lu,DIAG,disp_window,t=%u,s=%u,st=%s,ed=%u,es=%u,len=%lu,w=%lu,b=%u\r\n";

SC_MEM_ATTR void displayWindowTransition(uint8_t track, uint8_t slot, const char* trackState,
                                        uint8_t noteSessionOpen, uint8_t editSession,
                                        uint32_t loopLengthTicks, uint32_t windowStartTick,
                                        uint8_t windowBars) {
  // Two delivery rules, `readinessReceiptImmediate`'s: direct when USB has room, ring otherwise. The row is raised
  // while a transport runs, and the ring is retained by design then -- a merely enqueued copy would arrive only after
  // the stop, after the freeze / snap / hold bracket it explains.
  if (serialWriteRoom(140)) {
    Serial.printf(kDisplayWindowTransitionFormat, static_cast<unsigned long>(micros()),
                  static_cast<unsigned>(track), static_cast<unsigned>(slot),
                  trackState == nullptr ? "?" : trackState, static_cast<unsigned>(noteSessionOpen),
                  static_cast<unsigned>(editSession), static_cast<unsigned long>(loopLengthTicks),
                  static_cast<unsigned long>(windowStartTick), static_cast<unsigned>(windowBars));
    return;
  }
  emitCapPrintf(kDisplayWindowTransitionFormat, static_cast<unsigned long>(micros()),
                static_cast<unsigned>(track), static_cast<unsigned>(slot),
                trackState == nullptr ? "?" : trackState, static_cast<unsigned>(noteSessionOpen),
                static_cast<unsigned>(editSession), static_cast<unsigned long>(loopLengthTicks),
                static_cast<unsigned long>(windowStartTick), static_cast<unsigned>(windowBars));
}

/// The **drawn-vs-requested** receipt (`DIAG,disp_turn`). Same `.progmem` placement and the same two delivery rules
/// as the window transition receipt above: the row exists to answer one question a capture could not — was the
/// interval the display *drew* the interval the playhead *requested*, and if not, which named hold discarded the
/// request — and it is raised while a transport runs, where the ring is retained by design.
static const char kDisplayWindowDrawTurnFormat[] PROGMEM =
    "#CAP,%lu,DIAG,disp_turn,t=%u,s=%u,req=%lu,%lu,pub=%lu,%lu,n=%lu,st=%s,why=%s,key=%lu,%lu\r\n";

SC_MEM_ATTR void displayWindowDrawTurn(uint8_t track, uint8_t slot, uint32_t requestedStartTick,
                                       uint32_t requestedLengthTicks, uint32_t publishedStartTick,
                                       uint32_t publishedLengthTicks, uint32_t drawnNotes,
                                       const char* state, const char* reason,
                                       uint32_t retainedStartTick, uint32_t retainedLengthTicks) {
  const char* resolvedState = state == nullptr ? "?" : state;
  const char* resolvedReason = reason == nullptr ? "?" : reason;
  if (serialWriteRoom(190)) {
    Serial.printf(kDisplayWindowDrawTurnFormat, static_cast<unsigned long>(micros()),
                  static_cast<unsigned>(track), static_cast<unsigned>(slot),
                  static_cast<unsigned long>(requestedStartTick),
                  static_cast<unsigned long>(requestedLengthTicks),
                  static_cast<unsigned long>(publishedStartTick),
                  static_cast<unsigned long>(publishedLengthTicks),
                  static_cast<unsigned long>(drawnNotes), resolvedState, resolvedReason,
                  static_cast<unsigned long>(retainedStartTick),
                  static_cast<unsigned long>(retainedLengthTicks));
    return;
  }
  emitCapPrintf(kDisplayWindowDrawTurnFormat, static_cast<unsigned long>(micros()),
                static_cast<unsigned>(track), static_cast<unsigned>(slot),
                static_cast<unsigned long>(requestedStartTick),
                static_cast<unsigned long>(requestedLengthTicks),
                static_cast<unsigned long>(publishedStartTick),
                static_cast<unsigned long>(publishedLengthTicks),
                static_cast<unsigned long>(drawnNotes), resolvedState, resolvedReason,
                static_cast<unsigned long>(retainedStartTick),
                static_cast<unsigned long>(retainedLengthTicks));
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
  // A nested flush would fight the outer one for the head for the same reason a nested append
  // must not evict (see `appendCaptureRecord`): `Serial` writes can reach `yield()`.
  if (sFlushInProgress) {
    return;
  }
  // Enqueue before owning the ring walk, so a full ordinary ring can apply normal Tier-A eviction.
  // The direct USB path guards its own yield/reentry interval.
  flushPlaybackReadWitness(maxRecords > 8);
  sFlushInProgress = true;
  struct FlushGuard {
    ~FlushGuard() { sFlushInProgress = false; }
  } flushGuard;
  incrementSaturated(sCaptureRing.flushCalls);
  checkHeadPlausible(2);
  // Never emit capture notices via USB Serial under timing-critical budgets.
  if (maxRecords > 8) {
    emitCriticalJournal();
    emitCriticalRejectedNotice();
    emitOverflowNotice();
    emitTierADegradedNotice();
    emitHeadBadNotice();
    emitHeadResyncNotice();
    emitWipedNotice();
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
  size_t resyncs = 0;
  while (flushed < maxRecords && sCaptureRing.used >= sizeof(CaptureRecordHeader)) {
    CaptureRecordHeader header{};
    readBytesAt(sCaptureRing.head, &header, sizeof(header));
    const size_t total = recordTotalBytes(header);
    if (header.payloadLen == 0 || total > sCaptureRing.used) {
      recoverUnreadableHead();
      // The head now sits on a boundary (or the ring was reset): keep draining the records that are still
      // intact, within a bounded number of recoveries so corruption cannot make this walk loop.
      if (sCaptureRing.used < sizeof(CaptureRecordHeader) || ++resyncs > 8) {
        break;
      }
      continue;
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
      if (auto* family = recordFamilyStats(line)) incrementSaturated(family->transmitted);
      Serial.println(line);
    } else if (header.type == static_cast<uint8_t>(CaptureRecordType::DiagTrace)) {
      if (header.payloadLen == sizeof(Diagnostics::DiagTraceRecord)) {
        Diagnostics::DiagTraceRecord record{};
        readBytesAt(sCaptureRing.head + sizeof(header), &record, sizeof(record));
        flushOneDiagTraceRecord(record);
      }
    }

    advanceHead(total);
    checkHeadPlausible(4);
    ++flushed;
    incrementSaturated(sCaptureRing.flushTransmitted);
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

#if defined(__IMXRT1062__)
// The frozen receipt's **tag prefix**, FLASH-resident for the same reason as the format string above: `.rodata` is
// DTCM in this script, and this TU's literals already sit on the `_VectorsRam`/periodictable alignment edge, where the
// ~30 B of a new tag snaps the RAM1 `variables` figure by 4 KB. `CaptureLineTier.h` declares the accessor -- the header
// itself cannot carry the attribute (the compiler reports a section type conflict) -- and picks up this definition on
// the target; the host build returns the literal from the header instead. Defined at global scope, after the emitter
// namespace closes, because the classification namespace owns the name.
namespace CaptureLineTier {
namespace {
const char kFrozenReadinessTagPrefix[] PROGMEM = "DIAG,win_frozen_readiness,";
}  // namespace
const char* frozenReadinessTagPrefix() {
  return kFrozenReadinessTagPrefix;
}
}  // namespace CaptureLineTier
#endif  // __IMXRT1062__ -- the host build takes the header's inline literal instead
#endif  // SESSION_CAPTURE
