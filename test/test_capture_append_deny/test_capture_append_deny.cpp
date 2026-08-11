#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/EditManager/EditApply.cpp"
#include "../../src/Loop/LoopPasses.cpp"
#include "../test_support/MemoryMonitorNativeDeps.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/Loop.cpp"
#include "../test_support/LoopCaptureTestDeps.cpp"
#include "../../src/CaptureAppendResult.cpp"

#include "CaptureAppendResult.h"
#include "Globals.h"
#include "Loop.h"
#include "LoopEventStore.h"

namespace {

void beginOverdubCapture(Loop& loop) {
  loop.loopLengthTicks = Config::TICKS_PER_BAR * 32;
  loop.beginCapture(CapturePhase::Overdub);
}

void consumeAllPoolChunks() {
  LoopEventStore sink;
  while (sink.append(MidiEvent::NoteOn(0, 1, 60, 100))) {
  }
}

}  // namespace

void test_append_deny_phase_none() {
  Loop loop;
  const CaptureAppendResult result =
      loop.appendCaptureEventWithResult(MidiEvent::NoteOn(0, 1, 60, 100));
  TEST_ASSERT_FALSE(result.accepted);
  TEST_ASSERT_EQUAL(static_cast<int>(CaptureAppendDenyReason::PhaseNone),
                    static_cast<int>(result.reason));
}

void test_append_deny_pending_pass() {
  Loop loop;
  beginOverdubCapture(loop);
  loop.hasPendingCapturePass_ = true;
  const CaptureAppendResult result =
      loop.appendCaptureEventWithResult(MidiEvent::NoteOn(0, 1, 60, 100));
  TEST_ASSERT_FALSE(result.accepted);
  TEST_ASSERT_EQUAL(static_cast<int>(CaptureAppendDenyReason::PendingPass),
                    static_cast<int>(result.reason));
}

void test_append_deny_duplicate() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  beginOverdubCapture(loop);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(10, 1, 60, 100)));
  const CaptureAppendResult result =
      loop.appendCaptureEventWithResult(MidiEvent::NoteOn(10, 1, 60, 100));
  TEST_ASSERT_FALSE(result.accepted);
  TEST_ASSERT_EQUAL(static_cast<int>(CaptureAppendDenyReason::Duplicate),
                    static_cast<int>(result.reason));
}

void test_append_deny_pool_alloc() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  consumeAllPoolChunks();

  Loop loop;
  beginOverdubCapture(loop);
  const CaptureAppendResult result =
      loop.appendCaptureEventWithResult(MidiEvent::NoteOn(0, 1, 60, 100));
  TEST_ASSERT_FALSE(result.accepted);
  TEST_ASSERT_EQUAL(static_cast<int>(CaptureAppendDenyReason::PoolAlloc),
                    static_cast<int>(result.reason));
}

void test_append_accepted() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  beginOverdubCapture(loop);
  const CaptureAppendResult result =
      loop.appendCaptureEventWithResult(MidiEvent::NoteOn(0, 1, 60, 100));
  TEST_ASSERT_TRUE(result.accepted);
  TEST_ASSERT_EQUAL(static_cast<int>(CaptureAppendDenyReason::Accepted),
                    static_cast<int>(result.reason));
}

void test_deny_reason_labels() {
  TEST_ASSERT_EQUAL_STRING("phase_none",
                           captureAppendDenyReasonLabel(CaptureAppendDenyReason::PhaseNone));
  TEST_ASSERT_EQUAL_STRING("pool_alloc",
                           captureAppendDenyReasonLabel(CaptureAppendDenyReason::PoolAlloc));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_append_deny_phase_none);
  RUN_TEST(test_append_deny_pending_pass);
  RUN_TEST(test_append_deny_duplicate);
  RUN_TEST(test_append_deny_pool_alloc);
  RUN_TEST(test_append_accepted);
  RUN_TEST(test_deny_reason_labels);
  return UNITY_END();
}
