// Exercise the production ring, emitters and drain. Only USB serial is substituted.
#define SESSION_CAPTURE 1
#include <Arduino.h>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <unity.h>

struct CaptureSerial {
  std::string output;
  int room = 4096;
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
  void println(const char* line) { output += line; output += "\n"; }
} captureSerial;
#define Serial captureSerial
#include "../../src/Utils/DebugSessionCapture.cpp"
#undef Serial
#include "../../include/Utils/TelemetryCadence.h"

using namespace DebugSessionCapture;
static uint8_t backing[1024];
void setUp() {
  sCaptureRing = {};
  sCriticalJournal = CriticalJournal{};
  sCaptureRing.data = backing;
  sCaptureRing.capacity = sizeof(backing);
  sFlushInProgress = false;
  captureSerial.output.clear();
  captureSerial.room = 4096;
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

int main() {
  UNITY_BEGIN();
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
