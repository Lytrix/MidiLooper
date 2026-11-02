// Exercise the production ring, emitters and drain. Only USB serial is substituted.
#define SESSION_CAPTURE 1
#define MIDI_INGRESS_RECEIPTS_ENABLED 1
#define MIDI_INGRESS_COST_QUALIFICATION 1
#include <Arduino.h>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <unity.h>

struct CaptureSerial {
  std::string output;
  int room = 4096;
  int remainingLines = -1; // native I/O pressure after a chosen transmitted row
  int availableForWrite() const { return room; }
  void printf(const char* fmt, ...) {
    char line[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    output += line;
  }
  template<class... A> void printf_P(const char* fmt, A... args) { printf(fmt, args...); }
  void println(const char* line) {
    output += line; output += "\n";
    if (remainingLines > 0 && --remainingLines == 0) room = 0;
  }
} captureSerial;
#define Serial captureSerial
#include "../../src/Utils/DebugSessionCapture.cpp"
// S7.5a distribution instrument: the two sink rows (and the Tier-A prefixes the classifier above asks them for) live
// in their own DTCM-carved translation unit, so the suite that includes the sink includes them too.
#include "../../src/Utils/EnvelopeDistRows.cpp"
#include "../../src/Utils/StopDiagRows.cpp"
#undef Serial
#include "../../include/Utils/TelemetryCadence.h"
#include "../../src/StorageManager/HitlLiveObservationCommands.h"

using namespace DebugSessionCapture;
static uint8_t backing[1024];
void setUp() {
  sCaptureRing = {};
  sCriticalJournal = CriticalJournal{};
  sCriticalJournalInitialized = false; // model a new boot; poisoned tests overwrite RAM2 below
  sCaptureRing.data = backing;
  sCaptureRing.capacity = sizeof(backing);
  sFlushInProgress = false;
  captureSerial.output.clear();
  captureSerial.room = 4096;
  captureSerial.remainingLines = -1;
}
void tearDown() {}

void test_real_emitters_are_accounted_and_drained() {
  recStart(2, 100);
  recStop("stop", 2, 800, 100, 700, 768, false);
  recStopStage("publish", 10, 1, 100, 100, 2, 1, "ok");
  recSlotIdentity("start", 1, 0, 2, 2, 2, 0, 2, 'r', 'r', 0);
  TEST_ASSERT_EQUAL_UINT32(1, sCaptureRing.recordStart.entered);
  TEST_ASSERT_EQUAL_UINT32(2, sCaptureRing.recordStop.entered);
  TEST_ASSERT_EQUAL_UINT32(2, sCaptureRing.recordStop.enqueued);
  flushCaptureBuffer(8);
  TEST_ASSERT_TRUE(captureSerial.output.empty());
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.recordStop.transmitted);
  flushCaptureBuffer(64);
  TEST_ASSERT_EQUAL_UINT32(1, sCaptureRing.recordStart.transmitted);
  TEST_ASSERT_EQUAL_UINT32(2, sCaptureRing.recordStop.transmitted);
  TEST_ASSERT_EQUAL_UINT32(1, sCaptureRing.identityTransmitted);
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.used);
  emitIdentityPathStats();
  TEST_ASSERT_TRUE(captureSerial.output.find("RID,stats,entered=1,enq=1,rej=0,tx=1,evict=0") != std::string::npos);
  TEST_ASSERT_TRUE(captureSerial.output.find("RING,record_stats,RECA,entered=1,enq=1,rej=0,tx=1,evict=0") != std::string::npos);
  TEST_ASSERT_TRUE(captureSerial.output.find("RING,record_stats,RECS,entered=2,enq=2,rej=0,tx=2,evict=0") != std::string::npos);
}

void test_tier_a_flood_cannot_evict_record_evidence() {
  recStart(0, 0);
  for (int i = 0; i < 100; ++i)
    queueCaptureTextLine("#CAP,1,DIAG,reentry,deny,slot=0");
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.recordStart.evicted);
  TEST_ASSERT_GREATER_THAN_UINT32(0, sCaptureRing.timingShedPending);
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.criticalRejectedPending);
  flushCaptureBuffer(256);
  TEST_ASSERT_EQUAL_UINT32(1, sCaptureRing.recordStart.transmitted);
  TEST_ASSERT_TRUE(captureSerial.output.find("RECA,0,0") != std::string::npos);
}

static uint8_t reservedBacking[kRecordReserveBytes + 8192];

void test_record_rows_have_a_reserved_ring_slice() {
  sCaptureRing = {};
  sCaptureRing.data = reservedBacking;
  sCaptureRing.capacity = sizeof(reservedBacking);
  captureSerial.output.clear();
  // Ordinary Tier-A rows fill up to the non-reserved ceiling and then cycle there.
  const std::string filler = "#CAP,1,DIAG,idle_maint," + std::string(90, 'x');
  for (int i = 0; i < 200; ++i) queueCaptureTextLine(filler.c_str());
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(kRecordReserveBytes,
                                      sCaptureRing.capacity - sCaptureRing.used);
  // A take's rows get in, and none displaces another.
  recStart(2, 100);
  recStop("stop", 2, 800, 100, 700, 768, false);
  TEST_ASSERT_EQUAL_UINT32(1, sCaptureRing.recordStart.enqueued);
  TEST_ASSERT_EQUAL_UINT32(1, sCaptureRing.recordStop.enqueued);
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.recordStart.evicted);
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.recordStop.evicted);
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.criticalRejectedPending);
  // The ordinary region still cycles: once a Record row reaches the head its own traffic is
  // refused and reported as timing shed -- never as displacement of the Record row.
  for (int i = 0; i < 2000; ++i) queueCaptureTextLine(filler.c_str());
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.recordStart.evicted);
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.recordStop.evicted);
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.criticalRejectedPending);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(sCaptureRing.capacity, sCaptureRing.used);
  TEST_ASSERT_GREATER_THAN_UINT32(0, sCaptureRing.timingShedPending);
  // A later take's identity row still lands in the reserve without displacing anything.
  recSlotIdentity("start", 2, 0, 2, 2, 2, 0, 2, 'r', 'r', 0);
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.identityEmitRejected);
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.identityEvicted);
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.criticalRejectedPending);
}

void test_critical_flood_counts_actual_family_eviction() {
  recStart(0, 0);
  recSlotIdentity("start", 1, 0, 0, 0, 0, 0, 0, 'r', 'r', 0);
  for (int i = 0; i < 100; ++i)
    recStop("stop", 0, 800, 0, 800, 768, false);
  TEST_ASSERT_EQUAL_UINT32(1, sCaptureRing.recordStart.evicted);
  TEST_ASSERT_EQUAL_UINT32(1, sCaptureRing.identityEvicted);
  TEST_ASSERT_TRUE(sCaptureRing.overflowPending);
  flushCaptureBuffer(256);
  TEST_ASSERT_EQUAL_UINT32(100, sCaptureRing.recordStop.enqueued);
  TEST_ASSERT_EQUAL_UINT32(100, sCaptureRing.recordStop.evicted + sCaptureRing.recordStop.transmitted);
}

void test_allocation_failure_is_reported_after_safe_usb_retry() {
  // Native allocation boundary refuses: no extmem allocator. The real append path runs.
  sCaptureRing.data = nullptr;
  sCaptureRing.capacity = 0;
  recStart(0, 123);
  recSlotIdentity("start", 1, 0, 0, 0, 0, 0, 0, 'r', 'r', 0);
  TEST_ASSERT_EQUAL_UINT32(1, sCaptureRing.recordStart.entered);
  TEST_ASSERT_EQUAL_UINT32(1, sCaptureRing.recordStart.rejected);
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.recordStart.enqueued);
  TEST_ASSERT_EQUAL_UINT32(1, sCaptureRing.identityEmitRejected);
  TEST_ASSERT_EQUAL_UINT32(2, sCaptureRing.criticalRejectedPending);
  TEST_ASSERT_TRUE(captureSerial.output.empty());
  flushCaptureBuffer(8);
  TEST_ASSERT_TRUE(captureSerial.output.empty());
  captureSerial.room = 0;
  flushCaptureBuffer(64);
  TEST_ASSERT_EQUAL_UINT32(2, sCaptureRing.criticalRejectedPending);
  TEST_ASSERT_TRUE(captureSerial.output.empty());
  captureSerial.room = 4096;
  flushCaptureBuffer(64);
  TEST_ASSERT_TRUE(captureSerial.output.find("RING,critical_rejected,2,") != std::string::npos);
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.criticalRejectedPending);
  const auto output = captureSerial.output;
  flushCaptureBuffer(64);
  TEST_ASSERT_EQUAL_STRING(output.c_str(), captureSerial.output.c_str());
}

void test_unreadable_head_is_reported_as_a_wipe() {
  // A torn/garbage head record used to reset the ring silently: content was discarded with no
  // eviction or shed outcome, so a phase that produced no rows could not be told from a phase whose
  // emitters never ran. It must now count, latch a notice and invalidate the capture.
  queueCaptureTextLine("#CAP,1,DIAG,idle_maint,12345");
  TEST_ASSERT_GREATER_THAN_UINT32(0, sCaptureRing.used);
  const size_t discarded = sCaptureRing.used;
  // Zero-length payload at the head makes the record unreadable.
  CaptureRecordHeader torn{static_cast<uint8_t>(CaptureRecordType::Text), 0};
  writeBytesAt(sCaptureRing.head, &torn, sizeof(torn));
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.silentWipes);
  flushCaptureBuffer(64);
  TEST_ASSERT_EQUAL_UINT32(1, sCaptureRing.silentWipes);
  TEST_ASSERT_EQUAL_UINT32(discarded, sCaptureRing.silentWipeBytes);
  TEST_ASSERT_TRUE(sCaptureRing.wipedPending);
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.used);
  flushCaptureBuffer(64);
  TEST_ASSERT_TRUE(captureSerial.output.find("RING,wiped,1,") != std::string::npos);
  TEST_ASSERT_FALSE(sCaptureRing.wipedPending);
}

void test_nested_append_during_a_flush_cannot_evict_the_head() {
  // `Serial` output can reach the Teensy `yield()`, which can deliver a USB-host MIDI message and
  // append to this ring while a flush is walking it. An eviction there moved the head mid-record
  // and reset the whole ring (captures/s4f22-step3-base-04 lost 10 355 bytes that way).
  const std::string filler = "#CAP,1,DIAG,idle_maint," + std::string(90, 'x');
  for (int i = 0; i < 200; ++i) queueCaptureTextLine(filler.c_str());
  TEST_ASSERT_TRUE(sCaptureRing.used + 112 > sCaptureRing.capacity - kRecordReserveBytes ||
                   sCaptureRing.capacity <= kRecordReserveBytes);
  const size_t usedBefore = sCaptureRing.used;
  const size_t headBefore = sCaptureRing.head;
  sFlushInProgress = true;
  TEST_ASSERT_FALSE(queueCaptureTextLine(filler.c_str()));
  TEST_ASSERT_EQUAL_UINT32(usedBefore, sCaptureRing.used);
  TEST_ASSERT_EQUAL_UINT32(headBefore, sCaptureRing.head);
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.silentWipes);
  TEST_ASSERT_GREATER_THAN_UINT32(0, sCaptureRing.timingShedPending);
  // A nested flush is refused too: it would fight the outer one for the same head.
  const uint32_t flushCallsBefore = sCaptureRing.flushCalls;
  flushCaptureBuffer(64);
  TEST_ASSERT_EQUAL_UINT32(flushCallsBefore, sCaptureRing.flushCalls);
  sFlushInProgress = false;
}

void test_a_non_boundary_head_is_reported_with_its_operation() {
  // The wipe's own detail showed the head sitting mid-text (`type=50`, prefix
  // "295,4294967295,1"): something advanced by a total that did not match its record. The check
  // right after each advance must name that operation instead of only the wipe it causes.
  queueCaptureTextLine("#CAP,1,DIAG,idle_maint,12345");
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.headBadCount);
  sCaptureRing.head = (sCaptureRing.head + 2) % sCaptureRing.capacity;
  sCaptureRing.used -= 2;
  checkHeadPlausible(4);
  TEST_ASSERT_EQUAL_UINT32(1, sCaptureRing.headBadCount);
  TEST_ASSERT_EQUAL_UINT8(4, sCaptureRing.headBadOp);
  TEST_ASSERT_TRUE(sCaptureRing.headBadPending);
  flushCaptureBuffer(64);
  TEST_ASSERT_TRUE(captureSerial.output.find("RING,head_bad,") != std::string::npos);
  TEST_ASSERT_TRUE(captureSerial.output.find(",op=4,") != std::string::npos);
}

/// Task 2.2: the latch family is Tier-A *and* counted, so a lost edge is attributable rather than silent.
///
/// The emitter here is the real one (`src/Utils/DebugSessionCapture.cpp` is linked in this suite): one call
/// must show up as `entered`, reach the ring (`enq`) with its identity in the row text, and be reported by the
/// stopped-only read-back.
void test_task22_the_latch_family_is_accounted_and_read_back() {
  interactionLatchTransition("accept", 3, 4, 0x1234u, 0, "playing");
  interactionLatchTransition("consume", 3, 4, 0x1234u, 0, "admitted");
  TEST_ASSERT_EQUAL_UINT32(2, sCaptureRing.interactionLatch.entered);
  TEST_ASSERT_EQUAL_UINT32(2, sCaptureRing.interactionLatch.enqueued);
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.interactionLatch.rejected);
  flushCaptureBuffer(64);
  TEST_ASSERT_EQUAL_UINT32(2, sCaptureRing.interactionLatch.transmitted);
  TEST_ASSERT_TRUE(captureSerial.output.find("DIAG,latch,accept,3,4,4660,0,playing") != std::string::npos);
  TEST_ASSERT_TRUE(captureSerial.output.find("DIAG,latch,consume,3,4,4660,0,admitted") != std::string::npos);
  emitIdentityPathStats();
  TEST_ASSERT_TRUE(captureSerial.output.find(
      "RING,record_stats,DIAG,latch,entered=2,enq=2,rej=0,tx=2,evict=0") != std::string::npos);
}

/// A refused latch append is counted, not silent: the family's `rejected` is the evidence that separates "the
/// branch never ran" from "the row was refused".
void test_task22_a_refused_latch_append_is_counted() {
  sCaptureRing.capacity = 1;  // no room for any record
  interactionLatchTransition("accept", 0, 1, 7u, 1, "stopped");
  TEST_ASSERT_EQUAL_UINT32(1, sCaptureRing.interactionLatch.entered);
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.interactionLatch.enqueued);
  TEST_ASSERT_EQUAL_UINT32(1, sCaptureRing.interactionLatch.rejected);
  TEST_ASSERT_TRUE_MESSAGE(CaptureLineTier::isTierALine("#CAP,1,DIAG,latch,accept,0,1,7,1,stopped"),
                           "a latch edge is one-shot per episode: it must not be shed as ordinary timing");
}
/// The per-slice persistence rows are the volume that made the ring shed acceptance evidence (measured
/// `captures/s4b9-cross-track-10/-11`: 5 951 `PERS,work` rows in one boot, 241 per slot body). The cadence keeps
/// the phases a verifier requires and keeps the carried duration a faithful upper bound.
void test_persistence_slice_telemetry_is_rate_limited_and_carries_the_max() {
  TelemetryCadence::SliceLogState state;
  uint32_t carried = 0;
  // A phase's first slice always logs: `deferred_persist_large_loop_bounds` requires `payload_slice` and
  // `finalize_slice` to be present in every completed window.
  TEST_ASSERT_TRUE(
      TelemetryCadence::shouldLogSlice(state, 100, TelemetryCadence::kSliceLogInterval, carried));
  TEST_ASSERT_EQUAL_UINT32(100, carried);
  // The slices between two logged ones are not emitted ...
  for (uint32_t i = 0; i < TelemetryCadence::kSliceLogInterval - 2; ++i) {
    TEST_ASSERT_FALSE(
        TelemetryCadence::shouldLogSlice(state, 120u + i, TelemetryCadence::kSliceLogInterval, carried));
  }
  // ... and a spike among them still reaches the next logged row, because the carried value is the **running
  // max**, not the last slice's duration (the verifier takes max over the rows it can see).
  TEST_ASSERT_EQUAL_UINT32(120 + TelemetryCadence::kSliceLogInterval - 3, carried);
  TEST_ASSERT_TRUE(
      TelemetryCadence::shouldLogSlice(state, 50, TelemetryCadence::kSliceLogInterval, carried));
  TEST_ASSERT_EQUAL_UINT32(120 + TelemetryCadence::kSliceLogInterval - 3, carried);
  // A new work item resets the cadence, so its first slice logs and its max starts fresh.
  TelemetryCadence::reset(state);
  TEST_ASSERT_TRUE(
      TelemetryCadence::shouldLogSlice(state, 7, TelemetryCadence::kSliceLogInterval, carried));
  TEST_ASSERT_EQUAL_UINT32(7, carried);
  // Interval 0 is the escape hatch: every slice logs.
  TelemetryCadence::reset(state);
  for (int i = 0; i < 5; ++i) {
    TEST_ASSERT_TRUE(TelemetryCadence::shouldLogSlice(state, 10, 0, carried));
  }
  // The volume this buys, on the measured per-slot slice count: 241 slices emit 16 rows, not 241.
  TelemetryCadence::reset(state);
  uint32_t rows = 0;
  for (uint32_t i = 0; i < 241; ++i) {
    uint32_t carry = 0;
    if (TelemetryCadence::shouldLogSlice(state, 1000, TelemetryCadence::kSliceLogInterval, carry)) {
      ++rows;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(16, rows);
}

/// A few corrupt bytes at the head must cost those bytes, not the ring.
///
/// Measured on device (`captures/s4b-gate-newimage-01`): 7 unreadable bytes at the head of a 9 524-byte live ring
/// wiped the whole ring, and the host's `RING,wiped` rule then invalidated the capture and aborted the run. The
/// resync drops only the unreadable prefix and keeps the records that are still intact.
void test_a_corrupt_head_prefix_drops_only_the_prefix_and_keeps_the_records() {
  queueCaptureTextLine("#CAP,1,DIAG,t_a,1");
  queueCaptureTextLine("#CAP,2,DIAG,t_b,2");
  queueCaptureTextLine("#CAP,3,DIAG,t_c,3");
  TEST_ASSERT_EQUAL_UINT32(3, sCaptureRing.appendsOk);
  // Corrupt the head: move it 3 bytes into the first record, as an interleaved write would leave it.
  sCaptureRing.head = (sCaptureRing.head + 3) % sCaptureRing.capacity;
  sCaptureRing.used -= 3;
  flushCaptureBuffer(64);  // the walk resyncs; the notice is owed, not yet emitted
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.silentWipes);
  TEST_ASSERT_EQUAL_UINT32(1, sCaptureRing.headResyncCount);
  // The whole corrupt record goes, not just the 3 bytes: its start is what made it unreadable, so dropping a
  // suffix of it would leave a row with no known origin. What matters is the order of magnitude -- bytes, not
  // the whole ring.
  TEST_ASSERT_TRUE(sCaptureRing.headResyncBytes > 3);
  TEST_ASSERT_TRUE(sCaptureRing.headResyncBytes < 64);
  TEST_ASSERT_TRUE(sCaptureRing.headResyncPending);
  // The records after the corrupt prefix are still delivered; only the corrupted one is lost.
  TEST_ASSERT_TRUE(captureSerial.output.find("t_b,2") != std::string::npos);
  TEST_ASSERT_TRUE(captureSerial.output.find("t_c,3") != std::string::npos);
  flushCaptureBuffer(64);  // the next flush entry emits the resync's own row
  TEST_ASSERT_TRUE(captureSerial.output.find("RING,head_resync,1,") != std::string::npos);
  TEST_ASSERT_TRUE(captureSerial.output.find("RING,wiped") == std::string::npos);
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.used);
}

/// The fallback is unchanged: with no plausible boundary anywhere in the scan window, the ring resets and the wipe
/// is reported -- the resync bounds a *small* corruption, it does not pretend a ring of garbage is readable.
void test_a_head_with_no_boundary_anywhere_still_wipes() {
  queueCaptureTextLine("#CAP,1,DIAG,t_a,1");
  const size_t corruptFrom = sCaptureRing.head;
  const size_t corruptBytes = sCaptureRing.used;
  for (size_t i = 0; i < corruptBytes; ++i) {
    sCaptureRing.data[(corruptFrom + i) % sCaptureRing.capacity] = 0x55;
  }
  flushCaptureBuffer(64);
  // The notices are emitted at the *next* flush entry (the reset happens inside this walk), so it takes a second
  // call for the wipe to be reported -- the same shape a run sees.
  flushCaptureBuffer(64);
  TEST_ASSERT_EQUAL_UINT32(1, sCaptureRing.silentWipes);
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.headResyncCount);
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.used);
  TEST_ASSERT_TRUE(captureSerial.output.find("RING,wiped,1,") != std::string::npos);
}

/// The save-completion receipt is emitted **where it is produced**, not enqueued.
///
/// The ring under transport is retained by design rather than drained, so an enqueued receipt is delivered only
/// after the stop -- after its consumer has given up (measured `captures/s4b9-cross-track-18`: the row is in the
/// capture 3.1 s after the record stop while reader waits of 25 s and then 120 s both expired).
void test_the_save_completion_receipt_is_emitted_directly() {
  persistenceResultImmediate(1234, 100, 90, "ok");
  TEST_ASSERT_TRUE(captureSerial.output.find("PERS,result,1234,100,90,ok") != std::string::npos);
  // Nothing was enqueued: a reader that never flushes still sees the receipt.
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.appendsOk);
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.used);
}

/// DEC-108 §13b / task 3.9 — **the readiness receipt's two delivery rules**, the half of that pair that had no
/// fixture at all, and **the tier the fallback depends on**. The delivery *shape* is the save-completion receipt's:
/// emitted directly when USB has room, and carried by the ring when there is none. The second half is the one the
/// precedent's own fixture never covered.
///
/// **"Delayed rather than lost" is only true because the row's family is protected.** The receipt is produced
/// *under transport*, and that is exactly the regime whose flush budget is **8** (`src/main.cpp`,
/// `timingCriticalTrackActive ? 8 : 64`) and whose shed path drops non-Tier-A head records **silently, by design**.
/// Half (C) drives that regime, which is why `DIAG,win_readiness` is Tier-A; without the classification the queued
/// receipt was shed by the timing-critical flush and no later drain could deliver it.
void test_the_window_readiness_receipt_is_emitted_directly_and_delayed_rather_than_lost() {
  // (A) With room: written straight to the stream, and **nothing enqueued** -- a reader that never flushes still
  // sees the receipt, which is the whole point for a row produced under transport (the ring is retained by design
  // while a transport runs).
  captureSerial.room = 4096;
  readinessReceiptImmediate(3, 5, "owes_build", "window_build_owner", 42u, 768u);
  TEST_ASSERT_TRUE(captureSerial.output.find("DIAG,win_readiness,3,5,owes_build,window_build_owner,42,768") !=
                   std::string::npos);
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.appendsOk);
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.used);

  // (B) No room: not written directly, **not lost** either -- it goes on the ring, and the drain delivers it once
  // there is room. This is the half of the two rules that no fixture covered before this one.
  captureSerial.output.clear();
  captureSerial.room = 0;
  readinessReceiptImmediate(3, 5, "owes_source", "published_source", 43u, 0u);
  TEST_ASSERT_TRUE_MESSAGE(captureSerial.output.find("win_readiness") == std::string::npos,
                           "with no room the receipt is not written directly");
  TEST_ASSERT_GREATER_THAN_UINT32(0, sCaptureRing.used);
  captureSerial.room = 4096;
  flushCaptureBuffer(64);
  TEST_ASSERT_TRUE_MESSAGE(
      captureSerial.output.find("DIAG,win_readiness,3,5,owes_source,published_source,43,0") != std::string::npos,
      "delayed rather than lost: the ring delivered the receipt");

  // (C) **The transport regime**, which is the regime the receipt is actually produced in: a flush budget of 8
  // leaves Tier-A records for the idle drain and sheds everything else silently. So the ring fallback is only safe
  // if this family is Tier-A -- and this half is what proves it end to end (the drained row text, classified by the
  // production predicate). Before the promotion this half failed: the timing-critical flush dropped the queued
  // receipt and the idle drain had nothing to deliver.
  captureSerial.output.clear();
  captureSerial.room = 0;
  readinessReceiptImmediate(3, 5, "ready", "ready", 44u, 0u);
  TEST_ASSERT_GREATER_THAN_UINT32(0, sCaptureRing.used);
  // The shed path's own question, asked directly: the *stored record* must classify as Tier-A, since that is
  // what `flushCaptureBuffer`'s timing-critical budget consults before it retains or sheds the head.
  {
    char head[48] = {};
    headRecordTextPrefix(head);
    TEST_ASSERT_TRUE_MESSAGE(CaptureLineTier::isTierALine(head), head);
  }
  TEST_ASSERT_GREATER_THAN_UINT32(0, sCaptureRing.used);
  flushCaptureBuffer(8);
  // Unity's order is (threshold, actual, message): the receipt must still be in the ring, because the
  // timing-critical budget retains a Tier-A head instead of shedding it.
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(
      0u, sCaptureRing.used,
      "the timing-critical flush leaves a Tier-A receipt for the idle drain instead of shedding it");
  captureSerial.room = 4096;
  flushCaptureBuffer(64);
  const std::string drained = captureSerial.output;
  const size_t rowPos = drained.rfind("#CAP,", drained.find("win_readiness"));
  TEST_ASSERT_TRUE(rowPos != std::string::npos);
  const std::string row = drained.substr(rowPos, drained.find('\r', rowPos) - rowPos);
  TEST_ASSERT_TRUE_MESSAGE(row.find("DIAG,win_readiness,3,5,ready,ready,44,0") != std::string::npos,
                           "the receipt survived the timing-critical flush and the idle drain delivered it");
  TEST_ASSERT_TRUE_MESSAGE(CaptureLineTier::isTierALine(row.c_str()),
                           "and the row the production emitter produced is Tier-A by the production predicate");

  // DEC-108 §13b / task 3.10: the window lane's **service-gap sample** is Tier-A too, classified through the production
  // predicate (which is where the tag lives -- see the comment there). The boundary is exact.
  TEST_ASSERT_TRUE_MESSAGE(isTierATextLine("#CAP,1,DIAG,win_gap,5000,32,123456"),
                           "the service-gap sample survives the timing-critical flush");
  TEST_ASSERT_FALSE_MESSAGE(isTierATextLine("#CAP,1,DIAG,win_gapx,1"), "and a longer subtag is not swept in");
  // DEC-246 amendment 25: the per-track arbitration row beside it.
  TEST_ASSERT_TRUE_MESSAGE(isTierATextLine("#CAP,1234567890,DIAG,lane_fair,1286,2,7,90,0,0,0,0,0,0,0,0,0,0,0,0,0,0"),
                           "the lane fairness row is Tier-A");
  TEST_ASSERT_FALSE_MESSAGE(isTierATextLine("#CAP,1,DIAG,lane_fairx,1"), "and a longer subtag is not swept in");
  TEST_ASSERT_TRUE_MESSAGE(isTierATextLine("#CAP,1234567890,DIAG,succ,3,120,118,2,0,0,904,312,8,52"),
                           "the successor-probe row is Tier-A");
  TEST_ASSERT_FALSE_MESSAGE(isTierATextLine("#CAP,1,DIAG,success,1"), "and a longer word is not swept in");
  // ...and the family's **named overrun row** with it: the row that reports an *exceedance* of the permitted advance can
  // only be raised while the lane serves (i.e. under transport, the budget-8 regime), so it is the one that must survive
  // most -- and it does not share its sibling's prefix, which is why it needs its own entry and its own boundary.
  TEST_ASSERT_TRUE_MESSAGE(isTierATextLine("#CAP,112456330,DIAG,win_gap_overrun,64,3,9,1,2"),
                           "the named overrun row survives the timing-critical flush");
  TEST_ASSERT_FALSE_MESSAGE(isTierATextLine("#CAP,1,DIAG,win_gap_overrunx,1"),
                            "and a longer subtag is not swept in");
  // Task 6.4: the **display window's transition receipt** is raised under transport like the rows above (a frozen window
  // reports itself while the track plays), so the ring's shed path must not take the only copy that says what the display
  // held. It is classified by the header's `DIAG,disp_` family, which this pins through the production predicate.
  TEST_ASSERT_TRUE_MESSAGE(
      isTierATextLine("#CAP,112456330,DIAG,disp_window,t=7,s=3,st=PLAYING,ed=1,es=1,len=18432,w=1536,b=16"),
      "the display window's transition receipt survives the timing-critical flush");
  // ...and task 3.15g: the frozen Record-return receipt now carries the root-return adoption diagnostics, so it must
  // survive the timing-critical flush like the family it appends to.
  TEST_ASSERT_TRUE_MESSAGE(
      isTierATextLine("#CAP,1,DIAG,win_frozen_readiness,7,3,adopt_wait,e2_boundary,4,0,2,900000001,0,0,1536,255,0,0"),
      "the frozen Record-return adoption receipt survives the timing-critical flush");
  TEST_ASSERT_FALSE_MESSAGE(isTierATextLine("#CAP,1,DIAG,win_frozen_readiness_partial,1"),
                            "and a longer subtag is not swept in");
  // ...and the launch path's **gate row** with them, for the same reason and by its own boundary: the launch's decision
  // is taken under transport (a queued switch is prepared by the off-clock `SlotProvision` turn while the track plays),
  // so a non-Tier-A gate row is shed exactly where it is needed. Measured on device before this entry: the build carried
  // the row and the capture delivered none.
  TEST_ASSERT_TRUE_MESSAGE(isTierATextLine("#CAP,112456330,DIAG,launch_gate,t=7,s=4,x=launch_role_unavailable,len=18432"),
                           "the launch gate row survives the timing-critical flush");
  // S7.5a envelope instruments: the capture stamp error (ticks) and the replenishment owner's request-to-completion
  // row are raised while a transport runs, which is exactly the regime whose shed path drops non-Tier-A head
  // records — so the row needs its own prefix, pinned here by its boundary (a longer subtag is not swept in).
  TEST_ASSERT_TRUE_MESSAGE(
      isTierATextLine("#CAP,112456330,DIAG,envelope_instr,capstamp_max=4,capstamp_n=17,repl_wait_us=1200,"
                      "repl_work_us=340,repl_refused=3,repl_pairs=2"),
      "the envelope-instrument row survives the timing-critical flush");
  TEST_ASSERT_FALSE_MESSAGE(isTierATextLine("#CAP,1,DIAG,envelope_instrx,1"),
                            "and a longer subtag is not swept in");
  // ...and the S7.5a **distribution** rows with them, each pinned by its own boundary: the per-channel p50/p99
  // intervals and the per-route lateness split are emitted from the same window emitter, while a transport runs.
  TEST_ASSERT_TRUE_MESSAGE(
      isTierATextLine("#CAP,112456330,DIAG,env_dist,late_on,0,255,256,511,17"),
      "the envelope distribution row survives the timing-critical flush");
  TEST_ASSERT_FALSE_MESSAGE(isTierATextLine("#CAP,1,DIAG,env_distx,1"),
                            "and a longer subtag is not swept in");
  TEST_ASSERT_TRUE_MESSAGE(isTierATextLine("#CAP,112456330,DIAG,late_route,144,8,6620,12,14,0"),
                           "the per-route lateness split row survives the timing-critical flush");
  TEST_ASSERT_FALSE_MESSAGE(isTierATextLine("#CAP,1,DIAG,late_routex,1"),
                            "and a longer subtag is not swept in");
  // Task 4.20 allocation census: the row is rate-limited rather than event-shaped, and it is raised while a transport
  // may be running (that is when the census matters), so it needs the same Tier-A boundary. Pinned by its own
  // boundary: a longer subtag is not swept in.
  TEST_ASSERT_TRUE_MESSAGE(
      isTierATextLine("#CAP,112456330,DIAG,alloc_census,heap=48213,hmin=28016,chunks=477,cap=129,"
                      "edit=104,undo=8,ib=131072,eb=32768,ub=4096,xb=65536"),
      "the allocation census survives the timing-critical flush");
  TEST_ASSERT_FALSE_MESSAGE(isTierATextLine("#CAP,1,DIAG,alloc_censusx,1"),
                            "and a longer subtag is not swept in");
  // Deferred-stop **tail liveness**: one row per job whose canonical tail could not begin (or latched
  // `Denied`) under transport. That is exactly the budget-8 regime, and the row is the only evidence that
  // the 51 s `CanonicalTail` park (`session_20261030_143745.log`) was a refusal rather than work. It is
  // classified in `isTierATextLine`, not the header, for the DTCM reason above; pinned by its boundary.
  TEST_ASSERT_TRUE_MESSAGE(isTierATextLine("#CAP,112456330,DIAG,stop_tail,7,3,5,no_start,32"),
                           "the canonical-tail liveness row survives the timing-critical flush");
  TEST_ASSERT_FALSE_MESSAGE(isTierATextLine("#CAP,1,DIAG,stop_tailx,1"),
                            "and a longer subtag is not swept in");
  // DEC-216's aggregate owner-turn row moved out of the header classifier into this predicate for the same
  // DTCM reason (`StopDiagRows.cpp`). Its tier is load-bearing, so it is pinned here, the suite that asks the
  // production predicate; `test_capture_line_tier` pins that it stays out of the header list.
  TEST_ASSERT_TRUE_MESSAGE(
      isTierATextLine("#CAP,105246493,DIAG,idle_turn,track=7,us=186383,turn=283622,units=2"),
      "the aggregate owner-turn row survives the timing-critical flush");
  TEST_ASSERT_FALSE_MESSAGE(isTierATextLine("#CAP,1,DIAG,idle_turn_x,track=7,us=9000"),
                            "and a longer subtag is not swept in");
  // Task 4.20 allocatability probe: on demand from the host (`!HEAP_PROBE`) but it has to survive the same
  // timing-critical flush, because the denial it explains is felt while a transport runs.
  TEST_ASSERT_TRUE_MESSAGE(
      isTierATextLine("#CAP,112456330,DIAG,heap_probe,tail=45056,want=16384,malloc=1,pool=0"),
      "the allocatability probe survives the timing-critical flush");
  TEST_ASSERT_FALSE_MESSAGE(isTierATextLine("#CAP,1,DIAG,heap_probex,1"),
                            "and a longer subtag is not swept in");
  // Task 4.20 denial-edge capacity ladder: same family, second shape (`ladder,...`), raised at the denial itself and
  // therefore under transport. Pinned as a shape so a later reader cannot "helpfully" retag it and drop the tier.
  TEST_ASSERT_TRUE_MESSAGE(
      isTierATextLine("#CAP,112456330,DIAG,heap_probe,ladder,tail=36864,need=49152,largest=32768,mask=7"),
      "the denial-edge capacity ladder survives the timing-critical flush");
  TEST_ASSERT_FALSE_MESSAGE(isTierATextLine("#CAP,1,DIAG,launch_gatex,1"),
                            "and a longer subtag is not swept in");
  // ...and the publication owner's gate row beside it: the refusal it names is taken in the same regime (the Snapshot
  // owner's turn runs while the track plays), so it needs the same survival, with its own exact boundary.
  TEST_ASSERT_TRUE_MESSAGE(
      isTierATextLine("#CAP,112456330,DIAG,snap_gate,t=7,s=5,a=5,u=2,p=1,sep=0,fc=0,wj=0,hp=1,lr=0,vc=0,elig=0"),
      "the snapshot gate row survives the timing-critical flush");
  TEST_ASSERT_FALSE_MESSAGE(isTierATextLine("#CAP,1,DIAG,snap_gatex,1"),
                            "and a longer subtag is not swept in");
  // ...and the rolling demand path's gate row as the third of the family: its exit exists only *under* transport (the
  // lane is dispatched while a transport runs and nowhere else), so its absence in a budget-8 drain would be read as
  // "nothing was owed" -- exactly the confusion F-16 records. Own boundary, exact.
  TEST_ASSERT_TRUE_MESSAGE(
      isTierATextLine("#CAP,112456330,DIAG,win_gate,t=7,s=5,x=win_window_ineligible,len=18432,ep=0,av=1,al=1,pl=1,"
                      "ed=0,od=0,pv=0,rw=0,sv=1,rt=1,rl=1,ow=0,dm=0,bld=0,gr=0,rp=0,rq=0"),
      "the rolling demand path's gate row survives the timing-critical flush");
  TEST_ASSERT_FALSE_MESSAGE(isTierATextLine("#CAP,1,DIAG,win_gatex,1"),
                            "and a longer subtag is not swept in");
  // The report-only private-order rejection names a terminal Record-stop denial; it must survive a busy ring.
  TEST_ASSERT_TRUE_MESSAGE(isTierATextLine("#CAP,112456330,DIAG,order_deny,reason=4,phase=1,canon=1,chunk=17,id=203,"
                                           "ref=2,persist=0,inuse=1,used=256,expected=256,count=4500,chunks=18"),
                           "the order-denial detail row survives the timing-critical flush");
  TEST_ASSERT_FALSE_MESSAGE(isTierATextLine("#CAP,1,DIAG,order_denyx,1"), "and a longer subtag is not swept in");
  // The mid-pass persistence outcome row names why a take's chunks were not written; it must survive a busy ring.
  TEST_ASSERT_TRUE_MESSAGE(isTierATextLine("#CAP,112456330,DIAG,midpass_skip,reason=8,queue=3,writing=0,detail=0,"
                                           "prev_repeats=0,admit_refused=0,admit_last=0"),
                           "the mid-pass outcome row survives the timing-critical flush");
  TEST_ASSERT_FALSE_MESSAGE(isTierATextLine("#CAP,1,DIAG,midpass_skipx,1"), "and a longer subtag is not swept in");
  // The display rebuild-mix rows (5 s runtime-timing family) are Tier-A through the `DIAG,disp_` family prefix.
  TEST_ASSERT_TRUE_MESSAGE(isTierATextLine("#CAP,112456330,DIAG,disp_rebuild,42,7"),
                           "the display rebuild-mix row survives the timing-critical flush");
  TEST_ASSERT_TRUE_MESSAGE(isTierATextLine("#CAP,112456330,DIAG,disp_passwalk,12,0"),
                           "the pass-walk row survives the timing-critical flush");
  // Slice 1 of DEC-237: the committed-gather attribution row is in the same Tier-A family.
  TEST_ASSERT_TRUE_MESSAGE(
      isTierATextLine("#CAP,112456330,DIAG,disp_base,s=0,site=live,path=passes,us=65000,root=0,"
                      "snap=0,passes=61000,wrap=3000,filter=1000,ev=0,notes=420,win=0,6144"),
      "the committed-gather attribution row survives the timing-critical flush");
  // And the slow-frame phase split, which must survive the ring shedding that drops `DFRAME`.
  TEST_ASSERT_TRUE_MESSAGE(
      isTierATextLine("#CAP,112456330,DIAG,disp_frame_slow,n=493,us=101667,resolve=1415,"
                      "draw=586,transfer=48,idx=900"),
      "the slow display-frame phase row survives the timing-critical flush");
  // C6.4 T: the VisualCache owner-turn cadence is in the same Tier-A family.
  TEST_ASSERT_TRUE_MESSAGE(
      isTierATextLine("#CAP,112456330,DIAG,disp_owner_cadence,vcache_turns=42,max_us=180000,"
                      "last_us=90000,win_ms=5000"),
      "the owner-cadence row survives the timing-critical flush");
}
void test_rejected_critical_rows_are_replayed_at_stopped_flush() {
  sCaptureRing.capacity = 1;
  recStop("stop", 3, 800, 0, 800, 768, false);
  TEST_ASSERT_EQUAL_UINT32(1, sCriticalJournal.count);
  TEST_ASSERT_TRUE(captureSerial.output.empty());
  flushCaptureBuffer(8);
  TEST_ASSERT_TRUE(captureSerial.output.empty());
  flushCaptureBuffer(64);
  TEST_ASSERT_TRUE(captureSerial.output.find("EVIDENCE,critical_journal_begin") != std::string::npos);
  TEST_ASSERT_TRUE(captureSerial.output.find("RECS,stop,3") != std::string::npos);
  TEST_ASSERT_TRUE(captureSerial.output.find("EVIDENCE,critical_journal_end") != std::string::npos);
  TEST_ASSERT_EQUAL_UINT32(0, sCriticalJournal.count);
}

void test_live_track_status_is_direct_and_does_not_flush_retained_evidence() {
  recStart(3, 0);
  const size_t retained = sCaptureRing.used;
  flushCaptureBuffer(8);
  TEST_ASSERT_TRUE(captureSerial.output.empty());
  TEST_ASSERT_TRUE(emitTrackStatus(7, 3, "PLAYING", true));
  TEST_ASSERT_TRUE(captureSerial.output.find("track=7,slot=3,state=PLAYING,transport=1") != std::string::npos);
  TEST_ASSERT_EQUAL_UINT32(retained, sCaptureRing.used);
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.recordStart.transmitted);
}

void test_live_track_status_refuses_backpressure_without_queuing_a_stale_reply() {
  captureSerial.room = 95;
  TEST_ASSERT_FALSE(emitTrackStatus(7, 3, "PLAYING", true));
  TEST_ASSERT_TRUE(captureSerial.output.empty());
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.used);
  captureSerial.room = 96;
  TEST_ASSERT_TRUE(emitTrackStatus(7, 3, "STOPPED", false));
  TEST_ASSERT_TRUE(captureSerial.output.find("state=STOPPED,transport=0") != std::string::npos);
  TEST_ASSERT_TRUE(captureSerial.output.find("state=PLAYING") == std::string::npos);
}

void test_production_live_dispatch_frames_stopped_and_playing_status_without_ring_flush() {
  using StorageManagerInternal::processLiveObservationCommand;
  recStart(3, 0);
  const size_t retained = sCaptureRing.used;
  static const char* state;
  static bool running;
  const auto reply = [] { (void)emitTrackStatus(7, 3, state, running); };
  for (const char* sample : {"STOPPED", "PLAYING"}) {
    state = sample;
    running = std::strcmp(sample, "PLAYING") == 0;
    captureSerial.output.clear();
    TEST_ASSERT_TRUE(processLiveObservationCommand("!TRACE_FENCE aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", reply));
    TEST_ASSERT_TRUE(processLiveObservationCommand("!TRACK_STATUS", reply));
    TEST_ASSERT_TRUE(processLiveObservationCommand("!TRACE_FENCE bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", reply));
    const size_t start = captureSerial.output.find("TRACE_FENCE,aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    const size_t status = captureSerial.output.find("#TRACK_STATUS,");
    const size_t end = captureSerial.output.find("TRACE_FENCE,bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    TEST_ASSERT_TRUE(start != std::string::npos && status != std::string::npos && end != std::string::npos);
    TEST_ASSERT_TRUE(start < status && status < end);
    TEST_ASSERT_EQUAL_UINT32(retained, sCaptureRing.used);
  }
}

void test_live_fence_dispatch_refuses_backpressure_and_invalid_tokens() {
  using StorageManagerInternal::processLiveObservationCommand;
  const auto reply = [] { (void)emitTrackStatus(7, 3, "PLAYING", true); };
  captureSerial.room = 95;
  TEST_ASSERT_TRUE(processLiveObservationCommand("!TRACE_FENCE aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", reply));
  TEST_ASSERT_TRUE(processLiveObservationCommand("!TRACK_STATUS", reply));
  TEST_ASSERT_TRUE(processLiveObservationCommand("!TRACE_FENCE bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", reply));
  TEST_ASSERT_TRUE(captureSerial.output.empty());
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.used);
  captureSerial.room = 96;
  TEST_ASSERT_FALSE(emitTraceFence("short"));
  TEST_ASSERT_FALSE(emitTraceFence("zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz"));
  TEST_ASSERT_FALSE(emitTraceFence("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
  TEST_ASSERT_TRUE(captureSerial.output.empty());
}

void test_live_overlay_command_is_recognized_without_track_status_side_effect() {
  using StorageManagerInternal::processLiveObservationCommand;
  static bool trackCalled;
  trackCalled = false;
  const auto trackReply = [] { trackCalled = true; };
  TEST_ASSERT_TRUE_MESSAGE(processLiveObservationCommand("!OVERLAY_STATUS", trackReply),
                           "live overlay scalar command must be recognized");
  TEST_ASSERT_FALSE(trackCalled);
  TEST_ASSERT_FALSE(processLiveObservationCommand("!OVERLAY_STATUS 1", trackReply));
}

void test_live_overlay_snapshot_is_direct_framed_and_preserves_retained_ring() {
  using StorageManagerInternal::processLiveObservationCommand;
  recStart(3, 0);
  const size_t retained = sCaptureRing.used;
  const auto trackReply = [] {};
  const auto overlayReply = [] { (void)emitOverlayStatus(true, 2, 0, 1, 0, 0); };
  TEST_ASSERT_TRUE(processLiveObservationCommand("!TRACE_FENCE aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", trackReply));
  TEST_ASSERT_TRUE(processLiveObservationCommand("!OVERLAY_STATUS", trackReply, overlayReply));
  TEST_ASSERT_TRUE(processLiveObservationCommand("!TRACE_FENCE bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", trackReply));
  const size_t start = captureSerial.output.find("TRACE_FENCE,aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  const size_t status = captureSerial.output.find("#OVERLAY_STATUS,");
  const size_t end = captureSerial.output.find("TRACE_FENCE,bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
  TEST_ASSERT_TRUE(start < status && status < end && end != std::string::npos);
  TEST_ASSERT_TRUE(captureSerial.output.find("open=1,mode=2,row=0,set=1,rev=0,phase=0") != std::string::npos);
  TEST_ASSERT_EQUAL_UINT32(retained, sCaptureRing.used);
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.recordStart.transmitted);
  flushCaptureBuffer(8);
  TEST_ASSERT_EQUAL_UINT32(retained, sCaptureRing.used);
}

void test_live_overlay_snapshot_refuses_pressure_then_reports_current_scalars() {
  recStart(3, 0);
  const size_t retained = sCaptureRing.used;
  captureSerial.room = 127;
  TEST_ASSERT_FALSE(emitOverlayStatus(true, 2, 0, 1, 0, 0));
  TEST_ASSERT_TRUE(captureSerial.output.empty());
  captureSerial.room = 128;
  TEST_ASSERT_TRUE(emitOverlayStatus(true, 1, 0, 1, 2, 0));
  TEST_ASSERT_TRUE(captureSerial.output.find("open=1,mode=1,row=0,set=1,rev=2,phase=0") != std::string::npos);
  TEST_ASSERT_TRUE(captureSerial.output.find("mode=2") == std::string::npos);
  TEST_ASSERT_EQUAL_UINT32(retained, sCaptureRing.used);
}

void test_live_overlay_snapshot_maximum_fields_fit_declared_tx_cap() {
  captureSerial.room = 128;
  TEST_ASSERT_TRUE(emitOverlayStatus(true, 255, 255, 65535, 65535, 255));
  TEST_ASSERT_LESS_THAN_UINT32(128, captureSerial.output.size() + 10); // micros() may use all 10 digits
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.used);
}

void test_boot_init_clears_poisoned_journal_metadata_without_serial_output() {
  sCriticalJournal.count = 28374339;
  sCriticalJournal.dumpIndex = 28374338;
  sCriticalJournal.dropped = 123;
  sCriticalJournal.dumpStarted = true;
  initCaptureBuffer();
  TEST_ASSERT_EQUAL_UINT32(0, sCriticalJournal.count);
  TEST_ASSERT_EQUAL_UINT32(0, sCriticalJournal.dumpIndex);
  TEST_ASSERT_EQUAL_UINT32(0, sCriticalJournal.dropped);
  TEST_ASSERT_FALSE(sCriticalJournal.dumpStarted);
  TEST_ASSERT_TRUE(captureSerial.output.empty());
}

void test_repeated_init_keeps_early_rejected_record_journal() {
  sCriticalJournal.count = 28374339;
  sCriticalJournal.dumpIndex = 28374338;
  sCriticalJournal.dumpStarted = true;
  sCaptureRing.capacity = 1;
  recStop("stop", 3, 800, 0, 800, 768, false);
  TEST_ASSERT_EQUAL_UINT32(1, sCriticalJournal.count);
  initCaptureBuffer();
  sessionHeader();
  TEST_ASSERT_EQUAL_UINT32(1, sCriticalJournal.count);
  TEST_ASSERT_NOT_NULL(std::strstr(sCriticalJournal.lines[0], ",RECS,stop,3,800,0,800,768,0"));
  flushCaptureBuffer(64);
  TEST_ASSERT_TRUE(captureSerial.output.find("RECS,stop,3") != std::string::npos);
}

void test_bad_journal_metadata_is_refused_and_reported_without_ring_mutation() {
  recStart(3, 0);
  const size_t retained = sCaptureRing.used;
  initCaptureBuffer();
  sCriticalJournal.count = 33;
  sCriticalJournal.dumpIndex = 0;
  captureSerial.room = 0; // pre-fix path cannot dereference an out-of-range row
  flushCaptureBuffer(64);
  TEST_ASSERT_EQUAL_UINT32(0, sCriticalJournal.count);
  TEST_ASSERT_EQUAL_UINT32(0, sCriticalJournal.dumpIndex);
  TEST_ASSERT_EQUAL_UINT32(1, sCriticalJournal.dropped);
  TEST_ASSERT_EQUAL_UINT32(1, sCaptureRing.criticalRejectedPending);
  TEST_ASSERT_EQUAL_UINT32(1, sCaptureRing.criticalRejectedTotal);
  TEST_ASSERT_EQUAL_UINT32(retained, sCaptureRing.used);
  TEST_ASSERT_TRUE(captureSerial.output.empty());
  flushCaptureBuffer(64);
  TEST_ASSERT_EQUAL_UINT32(1, sCaptureRing.criticalRejectedTotal);
  captureSerial.room = 4096;
  flushCaptureBuffer(64);
  TEST_ASSERT_TRUE(captureSerial.output.find("RING,critical_rejected,1,") != std::string::npos);
  TEST_ASSERT_TRUE(captureSerial.output.find("critical_journal_begin,count=0,dropped=1") != std::string::npos);
}

void test_first_stopped_flush_initializes_poisoned_ram2_journal() {
  sCriticalJournal.count = 28374339;
  sCriticalJournal.dumpIndex = 28374338;
  sCriticalJournal.dumpStarted = true;
  sCriticalJournal.dropped = UINT32_MAX;
  flushCaptureBuffer(8);
  TEST_ASSERT_TRUE(captureSerial.output.empty()); // playing path stays no-USB
  flushCaptureBuffer(64);
  TEST_ASSERT_EQUAL_UINT32(0, sCriticalJournal.count);
  TEST_ASSERT_EQUAL_UINT32(0, sCriticalJournal.dumpIndex);
  TEST_ASSERT_EQUAL_UINT32(0, sCriticalJournal.dropped);
  TEST_ASSERT_TRUE(sCriticalJournalInitialized);
  TEST_ASSERT_TRUE(captureSerial.output.empty());
}

void test_bad_dump_indices_are_refused_before_reading_a_row() {
  for (unsigned sample = 0; sample < 3; ++sample) {
    setUp();
    initCaptureBuffer();
    sCriticalJournal.count = sample == 2 ? 28374339 : 1;
    sCriticalJournal.dumpIndex = sample == 2 ? 28374338 : sample == 0 ? 2 : 1;
    sCriticalJournal.dumpStarted = sample != 1;
    captureSerial.room = 0;
    flushCaptureBuffer(8);
    TEST_ASSERT_TRUE(captureSerial.output.empty());
    flushCaptureBuffer(64);
    TEST_ASSERT_EQUAL_UINT32(0, sCriticalJournal.count);
    TEST_ASSERT_EQUAL_UINT32(0, sCriticalJournal.dumpIndex);
    TEST_ASSERT_FALSE(sCriticalJournal.dumpStarted);
    TEST_ASSERT_EQUAL_UINT32(1, sCaptureRing.criticalRejectedPending);
    TEST_ASSERT_EQUAL_UINT32(1, sCaptureRing.criticalRejectedTotal);
  }
}

void test_corrupt_journal_append_and_eviction_refuse_without_indexing() {
  initCaptureBuffer();
  sCriticalJournal.count = 1;
  sCriticalJournal.dumpIndex = 2;
  journalCriticalLine(reinterpret_cast<const char*>(1)); // refused before even reading input
  TEST_ASSERT_EQUAL_UINT32(0, sCriticalJournal.count);
  TEST_ASSERT_EQUAL_UINT32(1, sCaptureRing.criticalRejectedTotal);
  recStart(3, 0);
  const size_t retained = sCaptureRing.used;
  sCriticalJournal.count = 33;
  journalCriticalHeadRecord();
  TEST_ASSERT_EQUAL_UINT32(0, sCriticalJournal.count);
  TEST_ASSERT_EQUAL_UINT32(retained, sCaptureRing.used);
  TEST_ASSERT_EQUAL_UINT32(2, sCaptureRing.criticalRejectedTotal);
  journalCriticalHeadRecord();
  TEST_ASSERT_EQUAL_UINT32(1, sCriticalJournal.count);
  TEST_ASSERT_NOT_NULL(std::strstr(sCriticalJournal.lines[0], ",RECA,3,0"));
}

static size_t occurrences(const std::string& text, const char* needle) {
  size_t count = 0, pos = 0;
  while ((pos = text.find(needle, pos)) != std::string::npos) { ++count; ++pos; }
  return count;
}

void test_partial_journal_replay_and_reinitialization_preserve_exactly_once_rows() {
  journalCriticalLine("#CAP,1,RECA,3,0");
  journalCriticalLine("#CAP,2,RECS,stop,3,768,0,768,768,0");
  captureSerial.remainingLines = 1;
  flushCaptureBuffer(64);
  TEST_ASSERT_EQUAL_UINT32(1, sCriticalJournal.dumpIndex);
  TEST_ASSERT_EQUAL_UINT32(2, sCriticalJournal.count);
  TEST_ASSERT_TRUE(sCriticalJournal.dumpStarted);
  initCaptureBuffer();
  initCaptureBuffer();
  TEST_ASSERT_EQUAL_UINT32(1, sCriticalJournal.dumpIndex);
  TEST_ASSERT_EQUAL_UINT32(2, sCriticalJournal.count);
  captureSerial.room = 4096;
  captureSerial.remainingLines = -1;
  flushCaptureBuffer(64);
  TEST_ASSERT_EQUAL_UINT32(1, occurrences(captureSerial.output, "#CAP,1,RECA,3,0"));
  TEST_ASSERT_EQUAL_UINT32(1, occurrences(captureSerial.output, "#CAP,2,RECS,stop,3"));
  TEST_ASSERT_EQUAL_UINT32(1, occurrences(captureSerial.output, "critical_journal_begin"));
  TEST_ASSERT_EQUAL_UINT32(1, occurrences(captureSerial.output, "critical_journal_end"));
  TEST_ASSERT_EQUAL_UINT32(0, sCriticalJournal.count);
  TEST_ASSERT_EQUAL_UINT32(0, sCriticalJournal.dumpIndex);
}

void test_full_journal_capacity_and_drop_survive_reinitialization() {
  for (size_t i = 0; i < kCriticalJournalCapacity; ++i) {
    journalCriticalLine("#CAP,1,RECA,3,0");
  }
  journalCriticalLine("#CAP,2,RECA,3,0");
  TEST_ASSERT_EQUAL_UINT32(32, sCriticalJournal.count);
  TEST_ASSERT_EQUAL_UINT32(1, sCriticalJournal.dropped);
  initCaptureBuffer();
  TEST_ASSERT_EQUAL_UINT32(32, sCriticalJournal.count);
  TEST_ASSERT_EQUAL_UINT32(1, sCriticalJournal.dropped);
  flushCaptureBuffer(64);
  TEST_ASSERT_EQUAL_UINT32(32, occurrences(captureSerial.output, "#CAP,1,RECA,3,0"));
  TEST_ASSERT_EQUAL_UINT32(0, occurrences(captureSerial.output, "#CAP,2,RECA,3,0"));
  TEST_ASSERT_TRUE(captureSerial.output.find("critical_journal_begin,count=32,dropped=1") != std::string::npos);
  flushCaptureBuffer(64);
  TEST_ASSERT_EQUAL_UINT32(32, occurrences(captureSerial.output, "#CAP,1,RECA,3,0"));
}

void test_pit_observation_delivery_tier_and_command_boundaries() {
  ClockPhysicalObservation observation{};
  observation.status = 2; observation.era = 2; observation.rawTicks = 4294967300ULL;
  observation.beforeUs = 178956970; observation.widthUs = 1; observation.cpuHz = 600000000;
  observation.configBits = 767; observation.epoch = 3; observation.readCycles = 100;
  physicalClockObservationImmediate(42, observation);
  TEST_ASSERT_TRUE(captureSerial.output.find("DIAG,pit_obs,42,2,2,4294967300,178956970,1,0,3,100,600000000,767,0,0") != std::string::npos);
  TEST_ASSERT_TRUE(isTierATextLine(captureSerial.output.c_str()));
  TEST_ASSERT_TRUE(isTierATextLine("#CAP,4294967295,DIAG,pit_obs,42"));
  TEST_ASSERT_FALSE(isTierATextLine("#CAP,1,DIAG,pit_obsx,42"));
  TEST_ASSERT_FALSE(CaptureLineTier::isTierALine("#CAP,1,DIAG,pit_obs,42")); // source-central extension
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.used);
  captureSerial.output.clear(); captureSerial.room = 0;
  physicalClockObservationImmediate(43, observation);
  TEST_ASSERT_TRUE(captureSerial.output.empty());
  TEST_ASSERT_TRUE(sCaptureRing.used > 0);
  captureSerial.room = 4096; flushCaptureBuffer(64);
  TEST_ASSERT_TRUE(captureSerial.output.find("DIAG,pit_obs,43,") != std::string::npos);
  captureSerial.output.clear();
  observation.rawTicks = observation.beforeUs = observation.musicalTick = UINT64_MAX;
  observation.era = observation.widthUs = observation.epoch = observation.readCycles = observation.cpuHz = UINT32_MAX;
  observation.configBits = 8191; observation.status = 9; observation.transport = true; observation.source = 1;
  physicalClockObservationImmediate(UINT32_MAX, observation);
  TEST_ASSERT_TRUE(captureSerial.output.size() + 9 < 192); // micros=1 fixture vs10digit target maximum
  TEST_ASSERT_TRUE(captureSerial.output.find(",8191,1,1\r\n") != std::string::npos);

  static uint32_t seen, calls;
  seen = calls = 0;
  const auto reply = [](uint32_t id) { seen = id; ++calls; };
  const auto noTrack = [] { TEST_FAIL_MESSAGE("PIT command must not query Track"); };
  using StorageManagerInternal::processLiveObservationCommand;
  TEST_ASSERT_TRUE(processLiveObservationCommand("!PIT_OBSERVE 4294967295", noTrack, nullptr, reply));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, seen); TEST_ASSERT_EQUAL_UINT32(1, calls);
  for (const char* bad : {"!PIT_OBSERVE ", "!PIT_OBSERVE 4294967296", "!PIT_OBSERVE -1", "!PIT_OBSERVE 1x", "!PIT_OBSERVE 1 "}) {
    TEST_ASSERT_TRUE(processLiveObservationCommand(bad, noTrack, nullptr, reply));
    TEST_ASSERT_EQUAL_UINT32(1, calls);
  }
}

void test_pit_ring_fallback_can_follow_end_fence_without_losing_request_identity() {
  using StorageManagerInternal::processLiveObservationCommand;
  const auto noTrack = [] { TEST_FAIL_MESSAGE("PIT request must not observe Track"); };
  const auto reply = [](uint32_t id) {
    ClockPhysicalObservation o{};
    o.status = 2; o.era = 2;
    physicalClockObservationImmediate(id, o);
  };
  captureSerial.room = 128; // Both96B fences fit;192B PIT row is ring-delayed.
  TEST_ASSERT_TRUE(processLiveObservationCommand("!TRACE_FENCE aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", noTrack));
  TEST_ASSERT_TRUE(processLiveObservationCommand("!PIT_OBSERVE 17", noTrack, nullptr, reply));
  TEST_ASSERT_TRUE(processLiveObservationCommand("!TRACE_FENCE bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", noTrack));
  TEST_ASSERT_TRUE(captureSerial.output.find("DIAG,pit_obs,") == std::string::npos);
  captureSerial.room = 4096;
  flushCaptureBuffer(64);
  const auto end = captureSerial.output.find("TRACE_FENCE,bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
  const auto row = captureSerial.output.find("DIAG,pit_obs,17,2,2,");
  TEST_ASSERT_TRUE(end != std::string::npos && row != std::string::npos && end < row);
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.used);
}

void test_pit_motion_delivery_tier_parser_and_fit() {
  ClockPhysicalObservation o{};
  o.status = 2; o.era = 2; o.periodUs = 2604; o.transport = true;
  physicalClockMotionImmediate(42, o, 9);
  TEST_ASSERT_TRUE(captureSerial.output.find("DIAG,pit_motion,42,2,2,0,0,0,0,0,2604,9,0,1,0") != std::string::npos);
  TEST_ASSERT_TRUE(isTierATextLine(captureSerial.output.c_str()));
  TEST_ASSERT_TRUE(isTierATextLine("#CAP,4294967295,DIAG,pit_motion,42"));
  TEST_ASSERT_FALSE(isTierATextLine("#CAP,1,DIAG,pit_motionx,42"));
  captureSerial.output.clear(); captureSerial.room = 128;
  physicalClockMotionImmediate(43, o, 15);
  TEST_ASSERT_TRUE(captureSerial.output.empty());
  captureSerial.room = 4096; flushCaptureBuffer(64);
  TEST_ASSERT_TRUE(captureSerial.output.find("DIAG,pit_motion,43,") != std::string::npos);
  captureSerial.output.clear();
  o.rawTicks = o.beforeUs = o.musicalTick = UINT64_MAX;
  o.era = o.widthUs = o.epoch = o.periodUs = UINT32_MAX;
  o.configBits = 8191; o.status = 9; o.source = 1;
  physicalClockMotionImmediate(UINT32_MAX, o, 15);
  TEST_ASSERT_TRUE(captureSerial.output.size() + 9 < 192);
  static uint32_t seen, calls;
  seen = calls = 0;
  const auto reply = [](uint32_t id) { seen = id; ++calls; };
  const auto forbidden = [] { TEST_FAIL_MESSAGE("motion must not query selected track"); };
  using StorageManagerInternal::processLiveObservationCommand;
  TEST_ASSERT_TRUE(processLiveObservationCommand("!PIT_MOTION 4294967295", forbidden, nullptr, nullptr, reply));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, seen); TEST_ASSERT_EQUAL_UINT32(1, calls);
  for (const char* bad : {"!PIT_MOTION ", "!PIT_MOTION 4294967296", "!PIT_MOTION -1", "!PIT_MOTION 1x", "!PIT_MOTION 1 "}) {
    TEST_ASSERT_TRUE(processLiveObservationCommand(bad, forbidden, nullptr, nullptr, reply));
    TEST_ASSERT_EQUAL_UINT32(1, calls);
  }
}

void test_tick_route_row_is_tier_a_and_fits_the_head_read() {
  // playback-tick-consume-only phase 1: the consumed-route / playback-miss window row.
  captureSerial.output.clear(); captureSerial.room = 4096;
  tickRouteRow(1, 2, 3, 35856, 5, 6, 0);
  TEST_ASSERT_TRUE(captureSerial.output.find(
                       "DIAG,tick_route,view=1,bridge=2,paged=3,merged=35856,none=5,miss_int=6,miss_ext=0\r\n") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(isTierATextLine("#CAP,4294967295,DIAG,tick_route,view=0"));
  TEST_ASSERT_FALSE(CaptureLineTier::isTierALine("#CAP,1,DIAG,tick_route,view=0"));  // source list only
  TEST_ASSERT_FALSE(isTierATextLine("#CAP,1,DIAG,tick_routex,view=0"));
  captureSerial.output.clear(); captureSerial.room = 8;  // no room: the row falls back to the ring
  tickRouteRow(UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX);
  TEST_ASSERT_TRUE(captureSerial.output.empty());
  captureSerial.room = 4096; flushCaptureBuffer(64);
  TEST_ASSERT_TRUE(captureSerial.output.find("DIAG,tick_route,view=4294967295,") != std::string::npos);
  TEST_ASSERT_TRUE(captureSerial.output.size() < 192);
}

void test_merged_build_witness_row_is_tier_a_and_fits_the_head_read() {
  // Overdub-audibility witness (2026-11-01): one row per merged-fallback build, at field maxima.
  captureSerial.output.clear(); captureSerial.room = 4096;
  mergedBuildWitnessRow(7, 7, 4294967295u, 4294967295u, 4294967295u, 6, true, 4294967295u, true, true, 4294967295u,
                        4294967295u, 4294967295u, 5, 14);
  TEST_ASSERT_TRUE(captureSerial.output.find("DIAG,merged_build,t=7,s=7,win=4294967295+4294967295,req=4294967295,"
                                             "path=6,root=1,rootcov=4294967295,ovl=1,rec=1,od=4294967295,"
                                             "ev=4294967295,exp=4294967295,ovt=5,pst=14\r\n") != std::string::npos);
  TEST_ASSERT_TRUE(captureSerial.output.size() < 230);
  TEST_ASSERT_TRUE(isTierATextLine("#CAP,4294967295,DIAG,merged_build,t=0"));
  captureSerial.output.clear(); captureSerial.room = 8;  // no room: the row falls back to the ring
  mergedBuildWitnessRow(7, 3, 0, 1536, 14, 6, false, 1, true, false, 12, 63, 140, 1, 13);
  TEST_ASSERT_TRUE(captureSerial.output.empty());
  captureSerial.room = 4096; flushCaptureBuffer(64);
  TEST_ASSERT_TRUE(captureSerial.output.find("DIAG,merged_build,t=7,s=3,win=0+1536,req=14,path=6,") != std::string::npos);
}

void test_route_reject_row_is_tier_a_and_fits_the_head_read() {
  // playback-tick-consume-only phase 2: the merged-fallback reason row.
  captureSerial.output.clear(); captureSerial.room = 4096;
  static const char kExit[] = "win_blocked_by_budget";
  routeRejectRow(7, 7, 37, 37, 9, 4294967295u, 4294967295u, 4294967295u, 4294967295u, kExit);
  TEST_ASSERT_TRUE(captureSerial.output.find("DIAG,route_reject,t=7,s=7,why=37,cause=37,prev=9,prev_n=4294967295,"
                                             "since=4294967295,chg=4294967295,len=4294967295,"
                                             "prod=win_blocked_by_budget\r\n") != std::string::npos);
  TEST_ASSERT_TRUE(captureSerial.output.size() < 192);
  TEST_ASSERT_TRUE(isTierATextLine("#CAP,4294967295,DIAG,route_reject,t=0"));
  TEST_ASSERT_FALSE(isTierATextLine("#CAP,1,DIAG,route_rejectx,t=0"));
  captureSerial.output.clear(); captureSerial.room = 8;  // no room: the row falls back to the ring
  routeRejectRow(0, 1, 8, 0, 0, 0, 0, 1, 7680, nullptr);
  TEST_ASSERT_TRUE(captureSerial.output.empty());
  captureSerial.room = 4096; flushCaptureBuffer(64);
  TEST_ASSERT_TRUE(captureSerial.output.find("DIAG,route_reject,t=0,s=1,why=8,cause=0,") != std::string::npos);
  TEST_ASSERT_TRUE(captureSerial.output.find("prod=-\r\n") != std::string::npos);
}

void test_runtime_profiler_rows_are_tier_a_in_the_source_list_only() {
  // DEC-239: every profiler row family, classified by the `.cpp` list at the longest timestamp, with the
  // boundary pinned; the header list must not learn the family (its literals cost DTCM in every TU).
  for (const char* row : {"#CAP,4294967295,DIAG,profile_boot,source=1,cpu_hz=600000000",
                          "#CAP,4294967295,DIAG,profile_stat,2,RecordStart_edge,calls=1",
                          "#CAP,4294967295,DIAG,profile_stat,end,count=14,resume=0",
                          "#CAP,4294967295,DIAG,profile_ring,1,t_us=5,id=5",
                          "#CAP,4294967295,DIAG,profile_reset,15",
                          "#CAP,4294967295,DIAG,profile_cmd,refused,depth=1"}) {
    TEST_ASSERT_TRUE(isTierATextLine(row));
    TEST_ASSERT_FALSE(CaptureLineTier::isTierALine(row));
  }
  TEST_ASSERT_FALSE(isTierATextLine("#CAP,1,DIAG,profilex,1"));
  TEST_ASSERT_FALSE(isTierATextLine("#CAP,1,DIAG,profil_stat,1"));
}

void test_ingress_cost_delivery_tier_parser_and_fit() {
  MidiIngress::IngressCostStats stats{};
  stats.accepted = 3; stats.minimumCycles = 10; stats.maximumCycles = 20; stats.sumCycles = 45;
  ingressCostImmediate(42, 0, 1, 600000000, 15, stats, 7);
  TEST_ASSERT_TRUE(captureSerial.output.find("DIAG,ingress_cost,42,0,1,600000000,15,3,0,10,20,45,0,7\r\n") != std::string::npos);
  TEST_ASSERT_TRUE(isTierATextLine(captureSerial.output.c_str()));
  TEST_ASSERT_TRUE(isTierATextLine("#CAP,4294967295,DIAG,ingress_cost,42"));
  TEST_ASSERT_FALSE(isTierATextLine("#CAP,1,DIAG,ingress_costx,42"));
  TEST_ASSERT_FALSE(CaptureLineTier::isTierALine("#CAP,1,DIAG,ingress_cost,42")); // source-central extension
  captureSerial.output.clear(); captureSerial.room = 128;
  ingressCostImmediate(43, 2, 3, 600000000, 15, stats, 7);
  TEST_ASSERT_TRUE(captureSerial.output.empty());
  captureSerial.room = 4096; flushCaptureBuffer(64);
  TEST_ASSERT_TRUE(captureSerial.output.find("DIAG,ingress_cost,43,2,3,") != std::string::npos);
  captureSerial.output.clear();
  stats.accepted = stats.rejected = stats.minimumCycles = stats.maximumCycles = UINT32_MAX;
  stats.sumCycles = UINT64_MAX; stats.saturated = true;
  ingressCostImmediate(UINT32_MAX, 2, 3, UINT32_MAX, UINT32_MAX, stats, UINT32_MAX);
  TEST_ASSERT_TRUE(captureSerial.output.size() + 9 < 192);
  static uint32_t seen, calls;
  seen = calls = 0;
  const auto reply = [](uint32_t id) { seen = id; ++calls; };
  const auto forbidden = [] { TEST_FAIL_MESSAGE("cost command must not query selected Track"); };
  using StorageManagerInternal::processLiveObservationCommand;
  TEST_ASSERT_TRUE(processLiveObservationCommand("!INGRESS_COST 4294967295", forbidden, nullptr, nullptr, nullptr, reply));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, seen); TEST_ASSERT_EQUAL_UINT32(1, calls);
  for (const char* bad : {"!INGRESS_COST ", "!INGRESS_COST 4294967296", "!INGRESS_COST -1", "!INGRESS_COST 1x", "!INGRESS_COST 1 "}) {
    TEST_ASSERT_TRUE(processLiveObservationCommand(bad, forbidden, nullptr, nullptr, nullptr, reply));
    TEST_ASSERT_EQUAL_UINT32(1, calls);
  }
  TEST_ASSERT_FALSE(processLiveObservationCommand("!INGRESS_COSTX 1", forbidden, nullptr, nullptr, nullptr, reply));
}

void test_rolling_record_tier_exact_prefix_and_max_timestamp_fit() {
  const char* row = "#CAP,4294967295,DIAG,rolling_record,4294967295,0,0,1";
  TEST_ASSERT_TRUE(isTierATextLine(row));
  TEST_ASSERT_FALSE(CaptureLineTier::isTierALine(row)); // .cpp extension, no header literal.
  TEST_ASSERT_FALSE(isTierATextLine("#CAP,1,DIAG,rolling_recordx,1"));
  TEST_ASSERT_FALSE(isTierATextLine("#CAP,1,DIAG,rolling_record"));
  TEST_ASSERT_FALSE(isTierATextLine("DIAG,rolling_record,1"));
  char head[48]{};
  std::memcpy(head, row, 47);
  TEST_ASSERT_TRUE(isTierATextLine(head)); // Actual ring classification head bound.
}

void test_playback_read_mailbox_preserves_time_cadence_and_tier_without_usb_on_edge() {
  PlaybackReadWitness row{};
  row.atUs = 1000000; row.tick = 100; row.generation = 5; row.storageGeneration = 29;
  row.start = 768; row.length = 768; row.period = 1536; row.phase = 1024;
  row.track = 7; row.slot = 3; row.origin = 2;
  observePlaybackRead(row);
  TEST_ASSERT_TRUE(captureSerial.output.empty());
  TEST_ASSERT_EQUAL_UINT32(0, sCaptureRing.used); // scalar mailbox, not formatting or allocation
  flushCaptureBuffer(8);
  TEST_ASSERT_TRUE(captureSerial.output.empty()); // timing-critical foreground budget never writes USB
  TEST_ASSERT_GREATER_THAN_UINT32(0, sCaptureRing.used);
  flushCaptureBuffer(64);
  const auto first = captureSerial.output;
  TEST_ASSERT_TRUE(first.find("#CAP,1000000,DIAG,playback_read,1,7,3,100,5,29,2,768,768,1536,1024") != std::string::npos);
  row.atUs += 100000; ++row.tick;
  observePlaybackRead(row); flushCaptureBuffer(64);
  TEST_ASSERT_EQUAL_STRING(first.c_str(), captureSerial.output.c_str());
  row.atUs = 1250000; ++row.tick;
  observePlaybackRead(row); flushCaptureBuffer(64);
  TEST_ASSERT_TRUE(captureSerial.output.size() > first.size());
  const auto second = captureSerial.output;
  row.atUs += 250000; // a repeated tick is not reader progress
  observePlaybackRead(row); flushCaptureBuffer(64);
  TEST_ASSERT_EQUAL_STRING(second.c_str(), captureSerial.output.c_str());
  ++row.tick; row.storageGeneration = 0;
  observePlaybackRead(row); flushCaptureBuffer(64);
  TEST_ASSERT_EQUAL_STRING(second.c_str(), captureSerial.output.c_str());
  row.storageGeneration = 29; row.generation = 0; row.origin = 3;
  observePlaybackRead(row); flushCaptureBuffer(64);
  TEST_ASSERT_EQUAL_STRING(second.c_str(), captureSerial.output.c_str());
  row.origin = 2;
  for (unsigned i = 0; i < 100; ++i)
    queueCaptureTextLine("#CAP,1,DIAG,win_gap,full,full,full,full,full,full,full,full");
  observePlaybackRead(row);
  TEST_ASSERT_TRUE(sPlaybackReads[7][3].pending);
  flushCaptureBuffer(8); // pre-walk enqueue must progress even with a retained Tier-A head
  TEST_ASSERT_FALSE(sPlaybackReads[7][3].pending);
  TEST_ASSERT_FALSE(sFlushInProgress);
  flushCaptureBuffer(64);
  TEST_ASSERT_TRUE(captureSerial.output.find("#CAP,1500000,DIAG,playback_read,") != std::string::npos);
  TEST_ASSERT_TRUE(captureSerial.output.find(",0,29,2,768,768,1536,1024") != std::string::npos);
  const char* prefix = "#CAP,4294967295,DIAG,playback_read,1,7,3,1";
  TEST_ASSERT_TRUE(isTierATextLine(prefix));
  TEST_ASSERT_FALSE(isTierATextLine("#CAP,1,DIAG,playback_reader,1"));
  char head[48]{}; std::memcpy(head, prefix, std::min<size_t>(47, std::strlen(prefix)));
  TEST_ASSERT_TRUE(isTierATextLine(head));
  row.atUs += 250000; ++row.tick; row.availableOnly = true;
  observePlaybackRead(row); flushCaptureBuffer(64); // Capture origin cannot claim current-root availability
  TEST_ASSERT_TRUE(captureSerial.output.find("DIAG,playback_available,") == std::string::npos);
  row.origin = 0; row.storageGeneration = 2;
  observePlaybackRead(row); flushCaptureBuffer(64);
  TEST_ASSERT_TRUE(captureSerial.output.find("#CAP,1750000,DIAG,playback_available,1,") != std::string::npos);
  const char* availablePrefix = "#CAP,4294967295,DIAG,playback_available,1,7,3";
  TEST_ASSERT_TRUE(isTierATextLine(availablePrefix));
  TEST_ASSERT_FALSE(isTierATextLine("#CAP,1,DIAG,playback_availables,1"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_playback_read_mailbox_preserves_time_cadence_and_tier_without_usb_on_edge);
  RUN_TEST(test_rolling_record_tier_exact_prefix_and_max_timestamp_fit);
  RUN_TEST(test_tick_route_row_is_tier_a_and_fits_the_head_read);
  RUN_TEST(test_route_reject_row_is_tier_a_and_fits_the_head_read);
  RUN_TEST(test_merged_build_witness_row_is_tier_a_and_fits_the_head_read);
  RUN_TEST(test_runtime_profiler_rows_are_tier_a_in_the_source_list_only);
  RUN_TEST(test_ingress_cost_delivery_tier_parser_and_fit);
  RUN_TEST(test_pit_motion_delivery_tier_parser_and_fit);
  RUN_TEST(test_pit_ring_fallback_can_follow_end_fence_without_losing_request_identity);
  RUN_TEST(test_pit_observation_delivery_tier_and_command_boundaries);
  RUN_TEST(test_first_stopped_flush_initializes_poisoned_ram2_journal);
  RUN_TEST(test_bad_dump_indices_are_refused_before_reading_a_row);
  RUN_TEST(test_corrupt_journal_append_and_eviction_refuse_without_indexing);
  RUN_TEST(test_partial_journal_replay_and_reinitialization_preserve_exactly_once_rows);
  RUN_TEST(test_full_journal_capacity_and_drop_survive_reinitialization);
  RUN_TEST(test_boot_init_clears_poisoned_journal_metadata_without_serial_output);
  RUN_TEST(test_repeated_init_keeps_early_rejected_record_journal);
  RUN_TEST(test_bad_journal_metadata_is_refused_and_reported_without_ring_mutation);
  RUN_TEST(test_live_overlay_snapshot_is_direct_framed_and_preserves_retained_ring);
  RUN_TEST(test_live_overlay_snapshot_refuses_pressure_then_reports_current_scalars);
  RUN_TEST(test_live_overlay_snapshot_maximum_fields_fit_declared_tx_cap);
  RUN_TEST(test_live_overlay_command_is_recognized_without_track_status_side_effect);
  RUN_TEST(test_production_live_dispatch_frames_stopped_and_playing_status_without_ring_flush);
  RUN_TEST(test_live_fence_dispatch_refuses_backpressure_and_invalid_tokens);
  RUN_TEST(test_live_track_status_is_direct_and_does_not_flush_retained_evidence);
  RUN_TEST(test_live_track_status_refuses_backpressure_without_queuing_a_stale_reply);
  RUN_TEST(test_real_emitters_are_accounted_and_drained);
  RUN_TEST(test_tier_a_flood_cannot_evict_record_evidence);
  RUN_TEST(test_task22_the_latch_family_is_accounted_and_read_back);
  RUN_TEST(test_task22_a_refused_latch_append_is_counted);
  RUN_TEST(test_record_rows_have_a_reserved_ring_slice);
  RUN_TEST(test_critical_flood_counts_actual_family_eviction);
  RUN_TEST(test_allocation_failure_is_reported_after_safe_usb_retry);
  RUN_TEST(test_unreadable_head_is_reported_as_a_wipe);
  RUN_TEST(test_nested_append_during_a_flush_cannot_evict_the_head);
  RUN_TEST(test_a_non_boundary_head_is_reported_with_its_operation);
  RUN_TEST(test_persistence_slice_telemetry_is_rate_limited_and_carries_the_max);
  RUN_TEST(test_a_corrupt_head_prefix_drops_only_the_prefix_and_keeps_the_records);
  RUN_TEST(test_a_head_with_no_boundary_anywhere_still_wipes);
  RUN_TEST(test_the_save_completion_receipt_is_emitted_directly);
  RUN_TEST(test_the_window_readiness_receipt_is_emitted_directly_and_delayed_rather_than_lost);
  RUN_TEST(test_rejected_critical_rows_are_replayed_at_stopped_flush);
  return UNITY_END();
}
