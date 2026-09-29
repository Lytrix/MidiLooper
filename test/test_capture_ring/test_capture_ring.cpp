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

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_real_emitters_are_accounted_and_drained);
  RUN_TEST(test_tier_a_flood_cannot_evict_record_evidence);
  RUN_TEST(test_critical_flood_counts_actual_family_eviction);
  RUN_TEST(test_allocation_failure_is_reported_after_safe_usb_retry);
  return UNITY_END();
}
