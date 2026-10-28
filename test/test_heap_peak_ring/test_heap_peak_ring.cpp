// Task 4.20 per-`_sbrk` accounting: the bounded event ring (`Utils/HeapPeakRing.h`) is pure logic, so its contract is
// pinned on the host. The linker wrappers that feed it are device-only and are proven by the first capture's rows.
#include <unity.h>

#include "Utils/HeapPeakRing.h"

using HeapPeakTrace::Event;
using HeapPeakTrace::Ring;

namespace {
Ring<8> makeGarbageRing() {
  // Uninitialised RAM2 at power-on, or a warm-reset leftover: neither is the magic.
  Ring<8> ring{};
  ring.magic = 0xDEADBEEFu;
  ring.nextSeq = 12345;
  ring.drainedSeq = 3;
  ring.lastUs = 77;
  return ring;
}
}  // namespace

void setUp() {}
void tearDown() {}

void test_events_drain_in_order_with_their_fields() {
  Ring<8> ring = makeGarbageRing();
  ring.record(100, 's', 45056, 40000, 0x6001A2B4u, 73728);
  ring.record(200, 'm', 0, 49152, 0x6001A2C8u, 73728);
  Event e;
  uint32_t lost = 99;
  TEST_ASSERT_TRUE(ring.drainOne(e, lost));
  TEST_ASSERT_EQUAL_UINT32(0, e.seq);
  TEST_ASSERT_EQUAL_UINT32(45056, e.incr);
  TEST_ASSERT_EQUAL_UINT32(40000, e.request);
  TEST_ASSERT_EQUAL_UINT32(0x6001A2B4u, e.caller);
  TEST_ASSERT_EQUAL_UINT32(73728, e.tailAfter);
  TEST_ASSERT_EQUAL_CHAR('s', e.kind);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, lost, "a first drain loses nothing");
  TEST_ASSERT_TRUE(ring.drainOne(e, lost));
  TEST_ASSERT_EQUAL_UINT32(1, e.seq);
  TEST_ASSERT_EQUAL_CHAR('m', e.kind);
  TEST_ASSERT_FALSE_MESSAGE(ring.drainOne(e, lost), "nothing pending after both drained");
  TEST_ASSERT_FALSE(ring.hasPending());
}

void test_overflow_keeps_the_newest_and_reports_the_gap() {
  Ring<8> ring = makeGarbageRing();
  for (uint32_t i = 0; i < 20; ++i) ring.record(10 + i, 's', 4096 * (i + 1), 0, 0, 1000 - i);
  Event e;
  uint32_t lost = 0;
  TEST_ASSERT_TRUE(ring.drainOne(e, lost));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(12, lost, "20 recorded into 8 slots: 12 overwritten before drain");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(12, e.seq, "the oldest survivor is the first of the newest 8");
  uint32_t last = e.seq;
  uint32_t drained = 1;
  while (ring.drainOne(e, lost)) {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, lost, "the gap is reported once, on the first drain after it");
    TEST_ASSERT_EQUAL_UINT32(last + 1, e.seq);
    last = e.seq;
    ++drained;
  }
  TEST_ASSERT_EQUAL_UINT32(8, drained);
  TEST_ASSERT_EQUAL_UINT32(19, last);
}

void test_a_rewound_clock_starts_a_new_run() {
  Ring<8> ring = makeGarbageRing();
  ring.record(5000, 's', 8192, 0, 0, 1);
  ring.record(6000, 's', 8192, 0, 0, 1);
  ring.record(10, 's', 4096, 0, 0, 1);  // micros() restarted: a reboot, not a continuation
  Event e;
  uint32_t lost = 0;
  TEST_ASSERT_TRUE(ring.drainOne(e, lost));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, e.seq, "stale events from the previous run are discarded");
  TEST_ASSERT_EQUAL_UINT32(4096, e.incr);
  TEST_ASSERT_FALSE(ring.drainOne(e, lost));
}

void test_an_empty_or_garbage_ring_has_nothing_to_drain() {
  Ring<8> ring = makeGarbageRing();
  Event e;
  uint32_t lost = 0;
  TEST_ASSERT_FALSE_MESSAGE(ring.drainOne(e, lost), "garbage must never be reported as events");
  TEST_ASSERT_FALSE(ring.hasPending());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_events_drain_in_order_with_their_fields);
  RUN_TEST(test_overflow_keeps_the_newest_and_reports_the_gap);
  RUN_TEST(test_a_rewound_clock_starts_a_new_run);
  RUN_TEST(test_an_empty_or_garbage_ring_has_nothing_to_drain);
  return UNITY_END();
}
