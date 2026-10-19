#define MIDI_INGRESS_RECEIPTS_ENABLED 1
#include <unity.h>
#include <Arduino.h>
#include "Utils/MidiIngressReceipt.h"
#include "../../src/Utils/MidiIngressReceipt.cpp"
namespace {
char serialOwner, hostOwner, foreignOwner;
uint64_t wideTime = 0;
uint32_t era = 9, samples = 0;
PhysicalClockStamp sample() { ++samples; return {wideTime, era}; }
void cold() {
  MidiIngress::setup(&serialOwner, &hostOwner);
  MidiIngress::setClockSampler(sample);
  samples = 0; wideTime = 1000; ingressRawMicros = 1000; era = 9;
}
void expect(const MidiIngressReceipt& r, uint64_t time, uint16_t offset) {
  TEST_ASSERT_TRUE(r.associated()); TEST_ASSERT_TRUE(r.current());
  TEST_ASSERT_TRUE(r.independentObservation());
  TEST_ASSERT_EQUAL_UINT64(time, r.observedUs); TEST_ASSERT_EQUAL_UINT16(offset, r.eventOffset);
}
}
void test_cold_refusal_has_no_receipt_and_recovers_without_touching_payload() {
  MidiIngress::denyPrewarmForTests(true); MidiIngress::setup(&serialOwner, &hostOwner);
  TEST_ASSERT_FALSE(MidiIngress::ready());
  midi_ingress_packet(0, nullptr, 6, 0); midi_ingress_publish(0, nullptr, 0, 0, 1);
  midi_ingress_consume(0, nullptr, 0, 0); TEST_ASSERT_FALSE(MidiIngress::lastReceipt(0).associated());
  MidiIngress::denyPrewarmForTests(false); cold(); TEST_ASSERT_TRUE(MidiIngress::ready());
}
void test_usb_packet_offsets_preserve_receive_time_across_partial_dequeue() {
  cold(); midi_ingress_packet(0, nullptr, 6, 0); midi_ingress_publish(0, nullptr, 2, 0, 4);
  TEST_ASSERT_EQUAL_UINT32(1, samples); wideTime = 9000; ingressRawMicros = 9000;
  for (uint16_t i = 0; i < 4; ++i) {
    midi_ingress_clear_last(0, nullptr); midi_ingress_consume(0, nullptr, 2, i);
    const auto r = MidiIngress::lastReceipt(0); expect(r, 1000, i);
    TEST_ASSERT_EQUAL_UINT32(1, samples); MidiIngress::noteDispatch(0, r);
    expect(MidiIngress::lastDispatchedReceipt(0), 1000, i);
  }
  midi_ingress_consume(0, nullptr, 2, 0); TEST_ASSERT_FALSE(MidiIngress::lastReceipt(0).associated());
}
void test_host_fifo_slots_roll_without_restamping_or_old_receipt_reuse() {
  cold();
  for (uint32_t i = 0; i < 800; ++i) {
    wideTime = 1000 + i;
    midi_ingress_packet(2, &hostOwner, 400, 0); midi_ingress_publish(2, &hostOwner, i % 400, i % 128, 1);
    wideTime += 10000; midi_ingress_consume(2, &hostOwner, i % 400, 0);
    expect(MidiIngress::lastReceipt(2), 1000 + i, i % 128);
    midi_ingress_consume(2, &hostOwner, i % 400, 0);
    TEST_ASSERT_FALSE(MidiIngress::lastReceipt(2).associated());
  }
}
void test_disconnect_and_overflow_keep_original_observation_but_refuse_currency() {
  cold(); midi_ingress_packet(2, &hostOwner, 400, 0); midi_ingress_publish(2, &hostOwner, 3, 0, 1);
  midi_ingress_invalidate(2, &hostOwner); midi_ingress_consume(2, &hostOwner, 3, 0);
  auto r = MidiIngress::lastReceipt(2); TEST_ASSERT_TRUE(r.associated());
  TEST_ASSERT_FALSE(r.current() || r.independentObservation()); TEST_ASSERT_EQUAL_UINT64(1000, r.observedUs);
  midi_ingress_packet(1, &serialOwner, 64, 0); midi_ingress_publish(1, &serialOwner, 7, 0, 1);
  midi_ingress_consume(1, &serialOwner, 7, 0); r = MidiIngress::lastReceipt(1);
  midi_ingress_invalidate(1, &serialOwner); TEST_ASSERT_FALSE(MidiIngress::currentReceipt(1, r));
  MidiIngress::noteDispatch(1, r); TEST_ASSERT_FALSE(MidiIngress::lastDispatchedReceipt(1).current());
}
void test_foreign_owner_missing_hook_and_unsupported_capacity_do_not_bless_metadata() {
  cold(); midi_ingress_packet(1, &foreignOwner, 64, 0); midi_ingress_publish(1, &serialOwner, 0, 0, 1);
  midi_ingress_consume(1, &serialOwner, 0, 0); TEST_ASSERT_FALSE(MidiIngress::lastReceipt(1).associated());
  TEST_ASSERT_EQUAL_UINT32(0, samples);
  midi_ingress_reset(1, &serialOwner, 128); midi_ingress_packet(1, &serialOwner, 128, 0);
  midi_ingress_publish(1, &serialOwner, 1, 0, 1); midi_ingress_consume(1, &serialOwner, 1, 0);
  TEST_ASSERT_FALSE(MidiIngress::lastReceipt(1).associated());
  midi_ingress_reset(1, &serialOwner, 64); midi_ingress_packet(1, &serialOwner, 64, 0);
  midi_ingress_publish(1, &serialOwner, 1, 0, 1); midi_ingress_consume(1, &serialOwner, 1, 0);
  expect(MidiIngress::lastReceipt(1), 1000, 0);
}
void test_raw_micros32_and_late_fifo_observation_never_claim_arrival_bounds() {
  cold(); MidiIngress::setClockSampler(nullptr); ingressRawMicros = UINT32_MAX;
  midi_ingress_packet(0, nullptr, 6, 0); midi_ingress_publish(0, nullptr, 0, 0, 1);
  ingressRawMicros = 1; midi_ingress_consume(0, nullptr, 0, 0);
  auto r = MidiIngress::lastReceipt(0); TEST_ASSERT_TRUE(r.associated());
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, r.rawObservedUs); TEST_ASSERT_EQUAL_UINT64(0, r.observedUs);
  TEST_ASSERT_FALSE(r.independentObservation());
  MidiIngress::setClockSampler(sample); midi_ingress_direct(1, &serialOwner);
  r = MidiIngress::lastReceipt(1); expect(r, 1000, 0);
  TEST_ASSERT_TRUE(r.flags & MidiIngressReceipt::LateHardwareRead);
  // An independent counter observation is still not a FIFO/wire-arrival proof.
}
void test_reset_sequence_exhaustion_and_generation_exhaustion_fail_closed() {
  cold(); MidiIngress::setCountersForTests(0, 42, UINT64_MAX);
  midi_ingress_packet(0, nullptr, 6, 0); midi_ingress_publish(0, nullptr, 0, 0, 1);
  midi_ingress_consume(0, nullptr, 0, 0); TEST_ASSERT_FALSE(MidiIngress::lastReceipt(0).associated());
  midi_ingress_reset(0, nullptr, 6); midi_ingress_packet(0, nullptr, 6, 0);
  TEST_ASSERT_EQUAL_UINT32(0, samples); // Reset cannot recycle exhausted producer identity.
  MidiIngress::setCountersForTests(0, UINT32_MAX, 1); midi_ingress_reset(0, nullptr, 6);
  midi_ingress_packet(0, nullptr, 6, 0); TEST_ASSERT_EQUAL_UINT32(0, samples);
  MidiIngress::setCountersForTests(0, 1, 0); // Test-only fault restoration, not runtime recovery.
}
void test_invalid_slot_offset_and_empty_read_refuse_without_sampling_again() {
  cold(); midi_ingress_packet(0, nullptr, 6, 0); midi_ingress_publish(0, nullptr, 1, 0, 2);
  midi_ingress_consume(0, nullptr, 1, 2); TEST_ASSERT_FALSE(MidiIngress::lastReceipt(0).associated());
  midi_ingress_consume(0, nullptr, 1, 0); expect(MidiIngress::lastReceipt(0), 1000, 0);
  midi_ingress_clear_last(0, nullptr); TEST_ASSERT_FALSE(MidiIngress::lastReceipt(0).associated());
  midi_ingress_publish(0, nullptr, 6, 0, 1); midi_ingress_consume(0, nullptr, 1, 1);
  TEST_ASSERT_FALSE(MidiIngress::lastReceipt(0).current()); TEST_ASSERT_EQUAL_UINT32(1, samples);
}
void test_sampler_replacement_and_unknown_era_preserve_observation_not_currency() {
  cold(); midi_ingress_packet(0, nullptr, 6, 0); midi_ingress_publish(0, nullptr, 1, 0, 1);
  MidiIngress::setClockSampler(nullptr); midi_ingress_consume(0, nullptr, 1, 0);
  auto r = MidiIngress::lastReceipt(0);
  TEST_ASSERT_TRUE(r.associated()); TEST_ASSERT_FALSE(r.current());
  TEST_ASSERT_EQUAL_UINT64(1000, r.observedUs); TEST_ASSERT_EQUAL_UINT32(9, r.clockEra);
  MidiIngress::setClockSampler(sample); era = 0;
  midi_ingress_packet(0, nullptr, 6, 0); midi_ingress_publish(0, nullptr, 1, 0, 1);
  MidiIngress::setClockSampler(sample); // Same callback does not manufacture a new source identity.
  midi_ingress_consume(0, nullptr, 1, 0); r = MidiIngress::lastReceipt(0);
  TEST_ASSERT_TRUE(r.associated() && r.current()); TEST_ASSERT_FALSE(r.independentObservation());
  TEST_ASSERT_EQUAL_UINT64(0, r.observedUs); TEST_ASSERT_EQUAL_UINT32(0, r.clockEra);
  TEST_ASSERT_EQUAL_UINT8(unsigned(MidiIngressReceipt::Clock::RawMicros32), unsigned(r.clock));
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_cold_refusal_has_no_receipt_and_recovers_without_touching_payload);
  RUN_TEST(test_usb_packet_offsets_preserve_receive_time_across_partial_dequeue);
  RUN_TEST(test_host_fifo_slots_roll_without_restamping_or_old_receipt_reuse);
  RUN_TEST(test_disconnect_and_overflow_keep_original_observation_but_refuse_currency);
  RUN_TEST(test_foreign_owner_missing_hook_and_unsupported_capacity_do_not_bless_metadata);
  RUN_TEST(test_raw_micros32_and_late_fifo_observation_never_claim_arrival_bounds);
  RUN_TEST(test_reset_sequence_exhaustion_and_generation_exhaustion_fail_closed);
  RUN_TEST(test_invalid_slot_offset_and_empty_read_refuse_without_sampling_again);
  RUN_TEST(test_sampler_replacement_and_unknown_era_preserve_observation_not_currency);
  return UNITY_END();
}
