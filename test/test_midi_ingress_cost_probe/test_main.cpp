#define MIDI_INGRESS_RECEIPTS_ENABLED 1
#define MIDI_INGRESS_COST_QUALIFICATION 1
#define MIDI_INGRESS_MASK_QUALIFICATION 1
#include <unity.h>
#include <Arduino.h>
#include "../test_support/MidiIngressMaskFixture.h"
#include "Utils/MidiIngressReceipt.h"
namespace {
uint64_t timeUs = 1000;
uint32_t cycleCount = 0, sampleCalls = 0, era = 9, cpu = 600000000, configuration = 1;
char dinOwner, hostOwner, foreignOwner;
bool nestOnce = false;
PhysicalClockStamp sample();
}
namespace MidiIngress::CostPlatform {
uint32_t cycles() { cycleCount += 100; return cycleCount; }
uint32_t cpuHz() { return cpu; }
uint32_t configuration() { return ::configuration; }
}
#include "../../src/Utils/MidiIngressReceipt.cpp"
namespace {
PhysicalClockStamp sample() {
  ++sampleCalls; timeUs += 10;
  if (nestOnce) {
    nestOnce = false;
    MIDI_INGRESS_PACKET(2, &hostOwner, 400);
  }
  return {timeUs, era};
}
const void* owner(unsigned route) { return route == 0 ? nullptr : route == 1 ? &dinOwner : &hostOwner; }
uint32_t capacity(unsigned route) { return route == 0 ? 6 : route == 1 ? 64 : 400; }
void cold() {
  MidiIngress::setup(&dinOwner, &hostOwner);
  MidiIngress::setClockSampler(sample);
  // Production setup intentionally does not recycle sequence/generation identity.
  // Establish this fixture's explicit starting identity through the native seam.
  for (uint8_t route = 0; route < 3; ++route) MidiIngress::setCountersForTests(route, 1, 0);
  timeUs = 1000; cycleCount = sampleCalls = 0; era = 9; cpu = 600000000; configuration = 1;
  IngressMaskFixture::reset(); nestOnce = false;
}
void stats(unsigned route, uint32_t count, uint64_t hookCycles = 500) {
  MidiIngress::IngressCostAggregate all[3];
  TEST_ASSERT_TRUE(MidiIngress::snapshotCosts(all));
  const uint32_t expectedCounts[] = {count, count, count * 3, count};
  const uint64_t expectedSums[] = {hookCycles * count, 100ULL * count, 300ULL * count, 100ULL * count};
  for (unsigned lane = 0; lane < 4; ++lane) {
    const auto& s = all[route].lanes[lane];
    TEST_ASSERT_EQUAL_UINT32(expectedCounts[lane], s.accepted);
    TEST_ASSERT_EQUAL_UINT64(expectedSums[lane], s.sumCycles);
    TEST_ASSERT_EQUAL_UINT32(0, s.rejected);
    TEST_ASSERT_FALSE(s.saturated);
  }
}
}
void setUp() { cold(); }
void tearDown() { IngressMaskFixture::reset(); }
void test_all_routes_incoming_masks_and_exact_receipt_frequency() {
  for (unsigned route = 0; route < 3; ++route) for (uint32_t mask = 0; mask < 2; ++mask) {
    cold(); IngressMaskFixture::reset(mask);
    MIDI_INGRESS_PACKET(route, owner(route), capacity(route));
    TEST_ASSERT_EQUAL_UINT32(mask, IngressMaskFixture::primask);
    TEST_ASSERT_EQUAL_UINT32(25, sampleCalls);
    stats(route, 1);
    midi_ingress_publish(route, owner(route), 0, 0, 1);
    midi_ingress_consume(route, owner(route), 0, 0);
    const auto receipt = MidiIngress::lastReceipt(route);
    TEST_ASSERT_TRUE(receipt.independentObservation());
    TEST_ASSERT_EQUAL_UINT64(1130, receipt.observedUs); // Actual observe, not a probe endpoint.
    TEST_ASSERT_EQUAL_UINT64(1, receipt.receiveSequence);
    TEST_ASSERT_EQUAL_UINT32(25, sampleCalls);
    TEST_ASSERT_EQUAL_UINT32(mask, IngressMaskFixture::primask);
    stats(route, 1);
  }
}
void test_direct_late_path_is_measured_without_extra_receipt() {
  for (uint32_t mask = 0; mask < 2; ++mask) {
    cold(); IngressMaskFixture::reset(mask);
    MIDI_INGRESS_DIRECT(1, &dinOwner);
    const auto receipt = MidiIngress::lastReceipt(1);
    TEST_ASSERT_TRUE(receipt.independentObservation());
    TEST_ASSERT_TRUE(receipt.flags & MidiIngressReceipt::LateHardwareRead);
    TEST_ASSERT_EQUAL_UINT64(1, receipt.receiveSequence);
    TEST_ASSERT_EQUAL_UINT64(1130, receipt.observedUs);
    TEST_ASSERT_EQUAL_UINT32(25, sampleCalls);
    stats(1, 1);
    TEST_ASSERT_EQUAL_UINT32(mask, IngressMaskFixture::primask);
  }
}
void test_refused_foreign_capacity_and_exhaustion_still_measure_only_refusal() {
  for (unsigned fault = 0; fault < 3; ++fault) {
    cold();
    if (fault == 2) MidiIngress::setCountersForTests(0, 7, UINT64_MAX);
    MIDI_INGRESS_PACKET(0, fault == 0 ? &foreignOwner : nullptr, fault == 1 ? 7 : 6);
    TEST_ASSERT_EQUAL_UINT32(24, sampleCalls); // No actual observe; probes alone.
    stats(0, 1);
    midi_ingress_publish(0, nullptr, 0, 0, 1); midi_ingress_consume(0, nullptr, 0, 0);
    TEST_ASSERT_FALSE(MidiIngress::lastReceipt(0).associated());
  }
}
void test_clock_faults_refuse_cost_without_suppressing_accepted_payload_receipt() {
  for (unsigned fault = 0; fault < 4; ++fault) {
    cold();
    if (fault == 0) era = 0;
    if (fault == 1) cpu = 0;
    if (fault == 2) configuration = 0;
    if (fault == 3) MidiIngress::setClockSampler(nullptr);
    MIDI_INGRESS_PACKET(0, nullptr, 6);
    MidiIngress::IngressCostAggregate all[3]; TEST_ASSERT_TRUE(MidiIngress::snapshotCosts(all));
    const uint32_t rejected[] = {1, 1, 3, 1};
    for (unsigned lane = 0; lane < 4; ++lane) {
      TEST_ASSERT_EQUAL_UINT32(0, all[0].lanes[lane].accepted);
      TEST_ASSERT_EQUAL_UINT32(rejected[lane], all[0].lanes[lane].rejected);
    }
    midi_ingress_publish(0, nullptr, 0, 0, 1); midi_ingress_consume(0, nullptr, 0, 0);
    TEST_ASSERT_TRUE(MidiIngress::lastReceipt(0).associated());
    TEST_ASSERT_EQUAL_UINT64(1, MidiIngress::lastReceipt(0).receiveSequence);
    TEST_ASSERT_EQUAL_UINT32(0, IngressMaskFixture::primask);
  }
}
void test_nested_callback_tokens_do_not_replace_outer_observation() {
  nestOnce = true;
  MIDI_INGRESS_PACKET(0, nullptr, 6);
  MidiIngress::IngressCostAggregate all[3]; TEST_ASSERT_TRUE(MidiIngress::snapshotCosts(all));
  for (unsigned route : {0u, 2u}) for (unsigned lane = 0; lane < 4; ++lane)
    TEST_ASSERT_EQUAL_UINT32(lane == 2 ? 3 : 1, all[route].lanes[lane].accepted);
  TEST_ASSERT_EQUAL_UINT64(1, ingressState->usb.sequence);
  TEST_ASSERT_EQUAL_UINT64(1, ingressState->host.sequence);
  TEST_ASSERT_EQUAL_UINT32(50, sampleCalls);
  TEST_ASSERT_EQUAL_UINT32(0, IngressMaskFixture::primask);
}
void test_snapshot_is_read_only_and_original_hook_is_not_a_measured_wrapper() {
  midi_ingress_packet(0, nullptr, 6, 0);
  MidiIngress::IngressCostAggregate first[3], last[3];
  TEST_ASSERT_TRUE(MidiIngress::snapshotCosts(first));
  TEST_ASSERT_TRUE(MidiIngress::snapshotCosts(last));
  TEST_ASSERT_EQUAL_UINT32(0, first[0].lanes[0].accepted);
  TEST_ASSERT_EQUAL_UINT32(1, first[0].lanes[1].accepted);
  TEST_ASSERT_EQUAL_UINT32(1, first[0].lanes[2].accepted);
  TEST_ASSERT_EQUAL_UINT32(0, first[0].lanes[3].accepted);
  TEST_ASSERT_EQUAL_MEMORY(first, last, sizeof(first));
}
void test_invalid_route_and_terminal_saturation_never_wrap() {
  MIDI_INGRESS_PACKET(3, nullptr, 6);
  MidiIngress::IngressCostAggregate all[3]; TEST_ASSERT_TRUE(MidiIngress::snapshotCosts(all));
  for (const auto& route : all) for (const auto& lane : route.lanes) TEST_ASSERT_EQUAL_UINT32(0, lane.accepted);
  ingressState->costs[0].lanes[0].accepted = UINT32_MAX;
  MIDI_INGRESS_PACKET(0, nullptr, 6);
  TEST_ASSERT_TRUE(MidiIngress::snapshotCosts(all));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, all[0].lanes[0].accepted);
  TEST_ASSERT_TRUE(all[0].lanes[0].saturated);
  TEST_ASSERT_EQUAL_UINT32(1, all[0].lanes[0].rejected);
  TEST_ASSERT_EQUAL_UINT64(1, ingressState->usb.sequence);
}
void test_missing_state_bypasses_costs_and_keeps_original_no_metadata_behavior() {
  auto* saved = ingressState; ingressState = nullptr;
  MIDI_INGRESS_PACKET(0, nullptr, 6);
  MidiIngress::IngressCostAggregate all[3];
  const bool present = MidiIngress::snapshotCosts(all);
  ingressState = saved;
  TEST_ASSERT_FALSE(present);
  TEST_ASSERT_EQUAL_UINT32(0, sampleCalls);
  TEST_ASSERT_EQUAL_UINT32(0, IngressMaskFixture::primask);
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_all_routes_incoming_masks_and_exact_receipt_frequency);
  RUN_TEST(test_direct_late_path_is_measured_without_extra_receipt);
  RUN_TEST(test_refused_foreign_capacity_and_exhaustion_still_measure_only_refusal);
  RUN_TEST(test_clock_faults_refuse_cost_without_suppressing_accepted_payload_receipt);
  RUN_TEST(test_nested_callback_tokens_do_not_replace_outer_observation);
  RUN_TEST(test_snapshot_is_read_only_and_original_hook_is_not_a_measured_wrapper);
  RUN_TEST(test_invalid_route_and_terminal_saturation_never_wrap);
  RUN_TEST(test_missing_state_bypasses_costs_and_keeps_original_no_metadata_behavior);
  return UNITY_END();
}
