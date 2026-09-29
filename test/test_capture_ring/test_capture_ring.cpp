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

using namespace DebugSessionCapture;
static uint8_t backing[1024];
void setUp() {
  sCaptureRing = {};
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

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_real_emitters_are_accounted_and_drained);
  RUN_TEST(test_tier_a_flood_cannot_evict_record_evidence);
  RUN_TEST(test_record_rows_have_a_reserved_ring_slice);
  RUN_TEST(test_critical_flood_counts_actual_family_eviction);
  RUN_TEST(test_allocation_failure_is_reported_after_safe_usb_retry);
  RUN_TEST(test_unreadable_head_is_reported_as_a_wipe);
  RUN_TEST(test_nested_append_during_a_flush_cannot_evict_the_head);
  return UNITY_END();
}
