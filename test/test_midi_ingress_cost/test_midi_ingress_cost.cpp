#include <unity.h>
#include <type_traits>
#include "Utils/MidiIngressCost.h"

using namespace MidiIngress;
namespace {
// Illustrative 1 MHz bound, not a Teensy clock/budget/accuracy assertion.
constexpr IngressCounterContract continuous{1000000, true};
IngressCycleEndpoint point(uint64_t us, uint32_t cycles) {
  return {us, us, cycles, 1000000, 7, 3};
}
void result(IngressCostResult expected, IngressCostResult actual) {
  TEST_ASSERT_EQUAL_UINT8(unsigned(expected), unsigned(actual));
}
void record(IngressCostAggregate& aggregate, IngressCostLane lane, uint32_t cycles) {
  IngressCostToken token;
  TEST_ASSERT_TRUE(token.begin(lane, point(100, 0)));
  result(IngressCostResult::Accepted, token.finish(aggregate, point(200, cycles), continuous));
  TEST_ASSERT_FALSE(token.pending());
}
void empty(const IngressCostStats& stats) {
  TEST_ASSERT_EQUAL_UINT32(0, stats.accepted);
  TEST_ASSERT_EQUAL_UINT64(0, stats.sumCycles);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, stats.minimumCycles);
  TEST_ASSERT_EQUAL_UINT32(0, stats.maximumCycles);
}
}
void setUp() {}
void tearDown() {}

void test_empty_and_exact_lane_statistics() {
  IngressCostAggregate a;
  for (const auto& s : a.lanes) { empty(s); TEST_ASSERT_EQUAL_UINT32(0, s.rejected); }
  record(a, IngressCostLane::HookCall, 11);
  record(a, IngressCostLane::HookCall, 23);
  record(a, IngressCostLane::HookCall, 0);
  const auto& s = a.lanes[0];
  TEST_ASSERT_EQUAL_UINT32(3, s.accepted);
  TEST_ASSERT_EQUAL_UINT64(34, s.sumCycles);
  TEST_ASSERT_EQUAL_UINT32(0, s.minimumCycles);
  TEST_ASSERT_EQUAL_UINT32(23, s.maximumCycles);
  for (unsigned i = 1; i < 4; ++i) empty(a.lanes[i]);
}
void test_nested_tokens_busy_begin_and_exact_once_finish() {
  static_assert(!std::is_copy_constructible<IngressCostToken>::value, "local tokens cannot duplicate finishes");
  static_assert(!std::is_move_constructible<IngressCostToken>::value, "local token address/lifetime is fixed");
  IngressCostAggregate a;
  IngressCostToken outer, inner;
  TEST_ASSERT_TRUE(outer.begin(IngressCostLane::HookCall, point(100, 50)));
  TEST_ASSERT_TRUE(inner.begin(IngressCostLane::Publication, point(110, 60)));
  TEST_ASSERT_FALSE(outer.begin(IngressCostLane::MaskBracket, point(120, 70)));
  result(IngressCostResult::Accepted, inner.finish(a, point(130, 85), continuous));
  result(IngressCostResult::Accepted, outer.finish(a, point(150, 100), continuous));
  TEST_ASSERT_EQUAL_UINT64(50, a.lanes[0].sumCycles);
  TEST_ASSERT_EQUAL_UINT64(25, a.lanes[2].sumCycles);
  empty(a.lanes[1]); empty(a.lanes[3]);
  result(IngressCostResult::NotPending, outer.finish(a, point(160, 110), continuous));
  TEST_ASSERT_EQUAL_UINT32(1, a.lanes[0].accepted);
  TEST_ASSERT_EQUAL_UINT32(0, a.lanes[0].rejected);
}
void test_missing_and_changed_contracts_refuse_then_recover() {
  for (unsigned fault = 0; fault != 16; ++fault) {
    auto first = point(100, 10), last = point(120, 30);
    auto contract = continuous;
    switch (fault) {
      case 0: contract.continuous = false; break;
      case 1: contract.maximumCounterHz = 0; break;
      case 2: first.physicalEra = 0; break;
      case 3: last.physicalEra = 0; break;
      case 4: last.physicalEra++; break;
      case 5: first.cpuHz = 0; break;
      case 6: last.cpuHz = 0; break;
      case 7: last.cpuHz--; break;
      case 8: contract.maximumCounterHz--; break;
      case 9: first.configuration = 0; break;
      case 10: last.configuration = 0; break;
      case 11: last.configuration++; break;
      case 12: first.afterUs = 99; break;
      case 13: last.afterUs = 119; break;
      case 14: last.beforeUs = last.afterUs = 99; break;
      case 15: first.afterUs = 121; break;
    }
    IngressCostAggregate a;
    IngressCostToken token;
    TEST_ASSERT_TRUE(token.begin(IngressCostLane::MaskBracket, first));
    result(IngressCostResult::InvalidSample, token.finish(a, last, contract));
    TEST_ASSERT_FALSE(token.pending());
    empty(a.lanes[1]);
    TEST_ASSERT_EQUAL_UINT32(1, a.lanes[1].rejected);
    result(IngressCostResult::NotPending, token.finish(a, last, continuous));
    TEST_ASSERT_EQUAL_UINT32(1, a.lanes[1].rejected);
    record(a, IngressCostLane::MaskBracket, 9);
    TEST_ASSERT_EQUAL_UINT32(1, a.lanes[1].accepted);
    TEST_ASSERT_EQUAL_UINT64(9, a.lanes[1].sumCycles);
  }
}
void test_low_word_carry_and_full_wrap_resurrection_refusal() {
  IngressCostAggregate a;
  IngressCostToken token;
  TEST_ASSERT_TRUE(token.begin(IngressCostLane::HookCall, point(0, UINT32_MAX - 4)));
  result(IngressCostResult::Accepted, token.finish(a, point(10, 5), continuous));
  TEST_ASSERT_EQUAL_UINT64(10, a.lanes[0].sumCycles);
  TEST_ASSERT_TRUE(token.begin(IngressCostLane::HookCall, point(0, UINT32_MAX - 4)));
  result(IngressCostResult::InvalidSample,
         token.finish(a, point(uint64_t(UINT32_MAX) + 1, 5), continuous));
  TEST_ASSERT_EQUAL_UINT32(1, a.lanes[0].accepted);
  TEST_ASSERT_EQUAL_UINT32(1, a.lanes[0].rejected);
  TEST_ASSERT_EQUAL_UINT64(10, a.lanes[0].sumCycles);
}
void test_exact_wrap_boundary_and_huge_span_are_not_multiplied() {
  const uint64_t spans[] = {UINT32_MAX, uint64_t(UINT32_MAX) + 1, UINT64_MAX};
  for (unsigned i = 0; i < 3; ++i) {
    IngressCostAggregate a;
    IngressCostToken token;
    TEST_ASSERT_TRUE(token.begin(IngressCostLane::EmptyProbe, point(0, 0)));
    result(i == 0 ? IngressCostResult::Accepted : IngressCostResult::InvalidSample,
           token.finish(a, point(spans[i], 1), continuous));
    TEST_ASSERT_EQUAL_UINT32(i == 0 ? 1 : 0, a.lanes[3].accepted);
  }
}
void test_enclosing_bracket_not_inner_difference_controls_wrap_gate() {
  // Independent non-integral wrap-time oracle: 2^32 / 3 cycles per microsecond.
  // Reject a hardcoded 1 MHz cutoff and off-by-one division rounding.
  for (unsigned i = 0; i < 2; ++i) {
    IngressCostAggregate a;
    IngressCostToken token;
    auto first = point(0, 0), last = point(1431655765ULL + i, 1);
    first.cpuHz = last.cpuHz = 3000000;
    TEST_ASSERT_TRUE(token.begin(IngressCostLane::EmptyProbe, first));
    result(i == 0 ? IngressCostResult::Accepted : IngressCostResult::InvalidSample,
           token.finish(a, last, {3000000, true}));
    TEST_ASSERT_EQUAL_UINT32(i == 0 ? 1 : 0, a.lanes[3].accepted);
  }
  IngressCostAggregate a;
  IngressCostToken token;
  auto first = point(0, 0), last = point(UINT32_MAX, 10);
  first.afterUs = 10;
  last.afterUs = uint64_t(UINT32_MAX) + 1;
  TEST_ASSERT_TRUE(token.begin(IngressCostLane::MaskBracket, first));
  result(IngressCostResult::InvalidSample, token.finish(a, last, continuous));
  empty(a.lanes[1]);
}
void test_equal_boundary_and_zero_sample_are_valid() {
  IngressCostAggregate a;
  IngressCostToken token;
  TEST_ASSERT_TRUE(token.begin(IngressCostLane::EmptyProbe, point(42, 8)));
  result(IngressCostResult::Accepted, token.finish(a, point(42, 8), continuous));
  TEST_ASSERT_EQUAL_UINT32(1, a.lanes[3].accepted);
  TEST_ASSERT_EQUAL_UINT32(0, a.lanes[3].minimumCycles);
}
void test_count_saturation_freezes_accepted_statistics_terminally() {
  IngressCostAggregate a;
  record(a, IngressCostLane::HookCall, 11);
  auto& s = a.lanes[0]; s.accepted = UINT32_MAX;
  IngressCostToken token;
  TEST_ASSERT_TRUE(token.begin(IngressCostLane::HookCall, point(0, 0)));
  result(IngressCostResult::Saturated, token.finish(a, point(10, 9), continuous));
  TEST_ASSERT_TRUE(s.saturated);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, s.accepted);
  TEST_ASSERT_EQUAL_UINT64(11, s.sumCycles);
  TEST_ASSERT_EQUAL_UINT32(11, s.minimumCycles);
  TEST_ASSERT_EQUAL_UINT32(11, s.maximumCycles);
  TEST_ASSERT_EQUAL_UINT32(1, s.rejected);
  TEST_ASSERT_TRUE(token.begin(IngressCostLane::HookCall, point(0, 0)));
  result(IngressCostResult::Saturated, token.finish(a, point(10, 0), continuous));
  TEST_ASSERT_EQUAL_UINT32(2, s.rejected);
  TEST_ASSERT_EQUAL_UINT64(11, s.sumCycles);
}
void test_sum_overflow_and_rejected_saturation_do_not_wrap() {
  IngressCostAggregate a;
  record(a, IngressCostLane::Publication, 7);
  auto& s = a.lanes[2]; s.sumCycles = UINT64_MAX - 3; s.rejected = UINT32_MAX;
  IngressCostToken token;
  TEST_ASSERT_TRUE(token.begin(IngressCostLane::Publication, point(0, 0)));
  result(IngressCostResult::Saturated, token.finish(a, point(10, 4), continuous));
  TEST_ASSERT_TRUE(s.saturated);
  TEST_ASSERT_EQUAL_UINT32(1, s.accepted);
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX - 3, s.sumCycles);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, s.rejected);
  TEST_ASSERT_EQUAL_UINT32(7, s.minimumCycles);
  TEST_ASSERT_EQUAL_UINT32(7, s.maximumCycles);
  record(a, IngressCostLane::HookCall, 2); // Saturation is lane-local.
}
void test_sum_exact_limit_is_accepted_then_next_increment_refuses() {
  IngressCostAggregate a;
  auto& s = a.lanes[1]; s.sumCycles = UINT64_MAX - 3;
  record(a, IngressCostLane::MaskBracket, 3);
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, s.sumCycles);
  TEST_ASSERT_FALSE(s.saturated);
  IngressCostToken token;
  TEST_ASSERT_TRUE(token.begin(IngressCostLane::MaskBracket, point(0, 0)));
  result(IngressCostResult::Saturated, token.finish(a, point(1, 1), continuous));
  TEST_ASSERT_EQUAL_UINT32(1, s.accepted);
}
void test_invalid_lane_cannot_create_pending_token() {
  IngressCostAggregate a;
  IngressCostToken token;
  TEST_ASSERT_FALSE(token.begin(static_cast<IngressCostLane>(4), point(0, 0)));
  TEST_ASSERT_FALSE(token.pending());
  result(IngressCostResult::NotPending, token.finish(a, point(1, 1), continuous));
  for (const auto& s : a.lanes) { empty(s); TEST_ASSERT_EQUAL_UINT32(0, s.rejected); }
}
void test_deterministic_short_carry_matrix_all_lanes() {
  // Expected costs constructed before modulo endpoint conversion, not from implementation subtraction.
  for (unsigned lane = 0; lane < 4; ++lane) {
    IngressCostAggregate a;
    for (uint32_t n = 0; n < 257; ++n) {
      const uint32_t cost = n * 13;
      const uint32_t start = UINT32_MAX - n * 7;
      const uint32_t end = uint32_t(uint64_t(start) + cost);
      IngressCostToken token;
      TEST_ASSERT_TRUE(token.begin(static_cast<IngressCostLane>(lane), point(100, start)));
      result(IngressCostResult::Accepted, token.finish(a, point(5000, end), continuous));
    }
    const auto& s = a.lanes[lane];
    TEST_ASSERT_EQUAL_UINT32(257, s.accepted);
    TEST_ASSERT_EQUAL_UINT64(427648, s.sumCycles); // 13 * (256*257/2)
    TEST_ASSERT_EQUAL_UINT32(0, s.minimumCycles);
    TEST_ASSERT_EQUAL_UINT32(3328, s.maximumCycles);
    for (unsigned other = 0; other < 4; ++other) if (other != lane) empty(a.lanes[other]);
  }
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_empty_and_exact_lane_statistics);
  RUN_TEST(test_nested_tokens_busy_begin_and_exact_once_finish);
  RUN_TEST(test_missing_and_changed_contracts_refuse_then_recover);
  RUN_TEST(test_low_word_carry_and_full_wrap_resurrection_refusal);
  RUN_TEST(test_exact_wrap_boundary_and_huge_span_are_not_multiplied);
  RUN_TEST(test_enclosing_bracket_not_inner_difference_controls_wrap_gate);
  RUN_TEST(test_equal_boundary_and_zero_sample_are_valid);
  RUN_TEST(test_count_saturation_freezes_accepted_statistics_terminally);
  RUN_TEST(test_sum_overflow_and_rejected_saturation_do_not_wrap);
  RUN_TEST(test_sum_exact_limit_is_accepted_then_next_increment_refuses);
  RUN_TEST(test_invalid_lane_cannot_create_pending_token);
  RUN_TEST(test_deterministic_short_carry_matrix_all_lanes);
  return UNITY_END();
}
