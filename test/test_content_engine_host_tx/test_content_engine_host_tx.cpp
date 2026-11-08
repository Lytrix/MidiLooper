//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The USB host port's transmit admission (`HostTx.h`): `hasRoom` is true exactly when USBHost_t36's
// `MIDIDeviceBase::write_packed` stores a packet on its first pass instead of spinning. The driver's condition is
// copied below and compared over every buffer state, including the timer's partial buffers (count = max while queued)
// and a zero `tx_size`.

#include <unity.h>

#include <cstdint>
#include <initializer_list>

#include "ContentEngine/HostTx.h"

using ContentEngine::hosttx::hasRoom;
using ContentEngine::hosttx::State;

namespace {

/// `write_packed`'s first loop iteration (USBHost_t36 midi.cpp): true when it returns without waiting.
bool driverStoresAtOnce(bool pipe, uint32_t tx1, uint32_t tx2, uint32_t txMax) {
  if (!pipe) return true;
  if (tx1 < txMax && (tx2 == 0 || tx2 >= txMax)) return true;
  return tx2 < txMax;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_matches_driver_over_every_state() {
  for (uint32_t txMax : {0u, 1u, 16u, 128u})
    for (uint32_t tx1 = 0; tx1 <= txMax; ++tx1)
      for (uint32_t tx2 = 0; tx2 <= txMax; ++tx2)
        for (bool pipe : {false, true})
          TEST_ASSERT_EQUAL(driverStoresAtOnce(pipe, tx1, tx2, txMax), hasRoom(State{pipe, tx1, tx2, txMax}));
}

void test_both_buffers_queued_refuses() {
  TEST_ASSERT_FALSE(hasRoom(State{true, 16, 16, 16}));
  TEST_ASSERT_TRUE(hasRoom(State{true, 16, 3, 16}));
  TEST_ASSERT_TRUE(hasRoom(State{true, 0, 16, 16}));
}

void test_no_pipe_admits() { TEST_ASSERT_TRUE(hasRoom(State{false, 16, 16, 16})); }

void test_zero_packet_size_refuses() { TEST_ASSERT_FALSE(hasRoom(State{true, 0, 0, 0})); }

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_matches_driver_over_every_state);
  RUN_TEST(test_both_buffers_queued_refuses);
  RUN_TEST(test_no_pipe_admits);
  RUN_TEST(test_zero_packet_size_refuses);
  return UNITY_END();
}
