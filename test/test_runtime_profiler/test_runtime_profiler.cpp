// Runtime profiler (DEC-239, openspec/changes/runtime-profiling-layer slice 1): the record path, both
// primitives, nesting, the ring and boot calibration, driven through the platform boundary with fake
// clocks. No production branch exists for tests; the fixture only defines `RuntimeProfiler::Platform`.
#define RUNTIME_PROFILE_SCOPES 1
#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "Utils/RuntimeProfiler.h"

namespace {
uint32_t gCycles = 0;
uint32_t gCycleStep = 0;  // added after each cycles() read (calibration arithmetic)
uint32_t gUs = 0;
uint32_t gIpsr = 0;
uint32_t gHz = 600000000u;
uint32_t gMicrosReads = 0;
std::vector<std::string> gRows;
}  // namespace

namespace RuntimeProfiler::Platform {
uint32_t cycles() {
  const uint32_t now = gCycles;
  gCycles += gCycleStep;
  return now;
}
uint32_t micros() {
  ++gMicrosReads;
  return gUs;
}
uint32_t ipsr() { return gIpsr; }
uint32_t cpuHz() { return gHz; }
void emitRow(const char* line) { gRows.emplace_back(line); }
}  // namespace RuntimeProfiler::Platform

#include "../../src/Utils/ProfileDiagRows.cpp"
#include "../../src/Utils/RuntimeProfiler.cpp"

using namespace RuntimeProfiler;

namespace {
constexpr uint32_t kCyclesPerUs = 600;
constexpr uint32_t kTrustUs600 = 7158278u - kCycleTrustGuardUs;  // floor(2^32 / 600) - guard

void boot(uint32_t hz = 600000000u) {
  gHz = hz;
  gCycleStep = 0;
  gIpsr = 0;
  gRows.clear();
  initAtBoot();
}
// Advance both clocks consistently (exact domain).
void advanceUs(uint32_t us) {
  gCycles += us * kCyclesPerUs;
  gUs += us;
}
const ProfileStat& self() { return stat(ProfileId::SelfTest); }
}  // namespace

void setUp() {}
void tearDown() {}

void test_boot_clears_a_prefilled_ram2_image() {
  // DMAMEM is NOLOAD: whatever the previous image left must not survive boot.
  std::memset(&detail::gState, 0xA5, sizeof(detail::gState));
  boot();
  for (size_t i = 0; i < kMaxIds; ++i) {
    const ProfileStat& st = statAt(i);
    TEST_ASSERT_EQUAL_UINT32(0, st.calls);
    TEST_ASSERT_EQUAL_UINT64(0, st.inclusiveCycles);
    TEST_ASSERT_EQUAL_UINT32(0, st.refused);
  }
  TEST_ASSERT_EQUAL_UINT16(0, counters().ringCount);
  TEST_ASSERT_EQUAL_UINT32(0, counters().dropped);
  TEST_ASSERT_EQUAL_UINT8(0, counters().depth);
  TEST_ASSERT_FALSE(counters().poisoned);
  TEST_ASSERT_EQUAL_UINT32(1, counters().nextSeq);
}

void test_boot_row_and_calibration_arithmetic() {
  gCycleStep = 7;  // each cycles() read advances 7: before, entry, exit, after -> cost 3 * 7
  gHz = 600000000u;
  gRows.clear();
  gMicrosReads = 0;
  initAtBoot();
  // Only the long variant reads the wall clock (entry + exit); the boot row reads it once more.
  TEST_ASSERT_EQUAL_UINT32(2u * kCalibrationSamples + 1u, gMicrosReads);
  const BootReport& b = bootReport();
  TEST_ASSERT_TRUE(b.sourceAvailable);
  TEST_ASSERT_EQUAL_UINT32(kCalibrationSamples, b.samples);
  TEST_ASSERT_EQUAL_UINT32(21, b.shortMinCycles);
  TEST_ASSERT_EQUAL_UINT32(21, b.shortAvgCycles);
  TEST_ASSERT_EQUAL_UINT32(21, b.longMinCycles);
  TEST_ASSERT_EQUAL_UINT32(21, b.longAvgCycles);
  TEST_ASSERT_EQUAL_UINT32(kTrustUs600, b.cycleTrustUs);
  // Calibration leaves no residue in any reported identity.
  TEST_ASSERT_EQUAL_UINT32(0, stat(ProfileId::Calibration).calls);
  TEST_ASSERT_EQUAL_UINT16(0, counters().ringCount);
  TEST_ASSERT_EQUAL_size_t(1, gRows.size());
  TEST_ASSERT_NOT_NULL(std::strstr(gRows[0].c_str(), "DIAG,profile_boot,source=1,cpu_hz=600000000,"));
  TEST_ASSERT_NOT_NULL(std::strstr(gRows[0].c_str(), ",samples=256,short_min_cycles=21,"));
  gCycleStep = 0;
}

void test_unavailable_source_refuses_short_and_long_and_still_emits_the_boot_row() {
  boot(0);
  TEST_ASSERT_FALSE(bootReport().sourceAvailable);
  TEST_ASSERT_EQUAL_UINT32(0, bootReport().samples);
  TEST_ASSERT_EQUAL_size_t(1, gRows.size());
  TEST_ASSERT_NOT_NULL(std::strstr(gRows[0].c_str(), "DIAG,profile_boot,source=0,"));
  { PROFILE_SCOPE_SHORT(s, ProfileId::SelfTest); advanceUs(10); }
  { PROFILE_SCOPE_LONG(s, ProfileId::SelfTest); advanceUs(10); }
  TEST_ASSERT_EQUAL_UINT32(0, self().calls);
  TEST_ASSERT_EQUAL_UINT32(2, self().refused);
  TEST_ASSERT_EQUAL_UINT32(0, self().lastUs);
  TEST_ASSERT_EQUAL_UINT8(0, counters().depth);
}

namespace {
bool earlyExitOperation(bool fail) {
  PROFILE_SCOPE_SHORT(scope, ProfileId::SelfTest);
  advanceUs(3);
  if (fail) return false;
  advanceUs(4);
  return true;
}
}  // namespace

void test_every_exit_path_is_timed_and_zero_duration_is_valid() {
  boot();
  TEST_ASSERT_FALSE(earlyExitOperation(true));
  TEST_ASSERT_EQUAL_UINT32(3, self().lastUs);
  TEST_ASSERT_TRUE(earlyExitOperation(false));
  TEST_ASSERT_EQUAL_UINT32(2, self().calls);
  TEST_ASSERT_EQUAL_UINT32(7, self().lastUs);
  TEST_ASSERT_EQUAL_UINT32(7, self().maxUs);
  TEST_ASSERT_EQUAL_UINT64(10ull * kCyclesPerUs, self().inclusiveCycles);
  { PROFILE_SCOPE_SHORT(s, ProfileId::SelfTest); }
  TEST_ASSERT_EQUAL_UINT32(3, self().calls);
  TEST_ASSERT_EQUAL_UINT32(0, self().lastCycles);
  TEST_ASSERT_EQUAL_UINT32(0, self().lastUs);
}

void test_parent_self_time_excludes_measured_children() {
  boot();
  {
    PROFILE_SCOPE_LONG(parent, ProfileId::RecordStart_edge);
    advanceUs(100);
    { PROFILE_SCOPE_LONG(child, ProfileId::PlaybackPrewarm_slot); advanceUs(1000); }
    advanceUs(10);
    { PROFILE_SCOPE_SHORT(child, ProfileId::SelfTest); advanceUs(500); }
    advanceUs(5);
  }
  const ProfileStat& p = stat(ProfileId::RecordStart_edge);
  TEST_ASSERT_EQUAL_UINT32(1, p.calls);
  TEST_ASSERT_EQUAL_UINT64(1615ull * kCyclesPerUs, p.inclusiveCycles);
  TEST_ASSERT_EQUAL_UINT64(115ull * kCyclesPerUs, p.selfCycles);
  TEST_ASSERT_EQUAL_UINT64(1000ull * kCyclesPerUs, stat(ProfileId::PlaybackPrewarm_slot).selfCycles);
  TEST_ASSERT_EQUAL_UINT8(0, counters().depth);
}

void test_result_and_context_are_optional_and_never_fabricated() {
  boot();
  { PROFILE_SCOPE_SHORT(s, ProfileId::SelfTest); advanceUs(1); }
  TEST_ASSERT_EQUAL_UINT8(kNoResult, self().lastResult);
  {
    PROFILE_SCOPE_SHORT(s, ProfileId::SelfTest);
    s.setResult(3);
    s.setContext(7, 2);
    advanceUs(3000);  // over the frame ceiling -> ring record carries the context
  }
  TEST_ASSERT_EQUAL_UINT8(3, self().lastResult);
  ProfileRingRecord r{};
  TEST_ASSERT_TRUE(ringAt(0, r));
  TEST_ASSERT_EQUAL_UINT8(7, r.track);
  TEST_ASSERT_EQUAL_UINT8(2, r.slot);
  TEST_ASSERT_EQUAL_UINT8(3, r.result);
  bool ok = false;
  {
    PROFILE_SCOPE_LONG_RESULT(s, ProfileId::SelfTest, ok);
    ok = true;  // read at exit, not at construction
  }
  TEST_ASSERT_EQUAL_UINT8(1, self().lastResult);
}

void test_only_over_ceiling_calls_enter_the_ring_with_their_parent() {
  boot();
  { PROFILE_SCOPE_SHORT(s, ProfileId::SelfTest); advanceUs(2000); }  // == frame ceiling: not over
  TEST_ASSERT_EQUAL_UINT16(0, counters().ringCount);
  TEST_ASSERT_EQUAL_UINT32(0, self().overCeiling);
  {
    PROFILE_SCOPE_LONG(parent, ProfileId::RecordStart_edge);
    { PROFILE_SCOPE_SHORT(s, ProfileId::SelfTest); advanceUs(2001); }
  }
  TEST_ASSERT_EQUAL_UINT32(1, self().overCeiling);
  TEST_ASSERT_EQUAL_UINT32(2, self().calls);
  TEST_ASSERT_EQUAL_UINT16(1, counters().ringCount);  // the parent stayed under the cold ceiling
  ProfileRingRecord r{};
  TEST_ASSERT_TRUE(ringAt(0, r));
  TEST_ASSERT_EQUAL_UINT32(1, r.seq);
  TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(ProfileId::SelfTest), r.id);
  TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(ProfileId::RecordStart_edge), r.parentId);
  TEST_ASSERT_EQUAL_UINT32(2001, r.micros);
  TEST_ASSERT_EQUAL_UINT32(2001u * kCyclesPerUs, r.cycles);
  TEST_ASSERT_EQUAL_UINT8(kNoContext, r.track);
  TEST_ASSERT_EQUAL_UINT8(kNoResult, r.result);
  TEST_ASSERT_EQUAL_UINT8(0, r.flags);
}

void test_ring_overwrite_counts_drops_and_keeps_a_seq_gap() {
  boot();
  for (int i = 0; i < 260; ++i) {
    PROFILE_SCOPE_SHORT(s, ProfileId::SelfTest);
    advanceUs(2500);
  }
  TEST_ASSERT_EQUAL_UINT16(kRingCapacity, counters().ringCount);
  TEST_ASSERT_EQUAL_UINT32(4, counters().dropped);
  ProfileRingRecord oldest{}, newest{};
  TEST_ASSERT_TRUE(ringAt(0, oldest));
  TEST_ASSERT_TRUE(ringAt(kRingCapacity - 1, newest));
  TEST_ASSERT_EQUAL_UINT32(5, oldest.seq);  // seq 1..4 overwritten: the gap is visible from seq alone
  TEST_ASSERT_EQUAL_UINT32(260, newest.seq);
  TEST_ASSERT_FALSE(ringAt(kRingCapacity, oldest));
}

void test_long_scope_source_selection_boundary_is_exact() {
  boot();
  // Just below the trust threshold: cycle-derived and exact.
  {
    PROFILE_SCOPE_LONG(s, ProfileId::SelfTest);
    gCycles += (kTrustUs600 - 1u) * kCyclesPerUs;
    gUs += kTrustUs600 - 1u;
  }
  TEST_ASSERT_EQUAL_UINT32((kTrustUs600 - 1u) * kCyclesPerUs, self().lastCycles);
  TEST_ASSERT_EQUAL_UINT8(0, self().flags & kStatFlagCoarse);
  // At and above the threshold: the wall clock reports, marked coarse, cycles saturated. Each delta is
  // checked on its own boot so the sticky coarse flag of one case cannot mask a miss in the other.
  for (uint32_t delta : {kTrustUs600, kTrustUs600 + 1u}) {
    boot();
    {
      PROFILE_SCOPE_LONG(s, ProfileId::SelfTest);
      gCycles += 12345u;  // a wrapped counter would read as this small modulo value
      gUs += delta;
    }
    TEST_ASSERT_EQUAL_UINT32(delta, self().lastUs);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, self().lastCycles);
    TEST_ASSERT_EQUAL_UINT8(kStatFlagCoarse, self().flags & kStatFlagCoarse);
  }
}

void test_short_scope_never_reads_the_wall_clock() {
  boot();
  {
    PROFILE_SCOPE_SHORT(s, ProfileId::SelfTest);
    gCycles += 600;
    gUs += 99999;  // ignored: the short primitive is DWT-only
  }
  TEST_ASSERT_EQUAL_UINT32(1, self().lastUs);
  TEST_ASSERT_EQUAL_UINT8(0, self().flags);
}

void test_coarse_multi_second_child_is_not_truncated_in_the_parent() {
  boot();
  {
    PROFILE_SCOPE_LONG(parent, ProfileId::RecordStart_edge);
    advanceUs(100);
    {
      PROFILE_SCOPE_LONG(child, ProfileId::PlaybackPrewarm_slot);
      gCycles += 777u;  // wrapped counter
      gUs += 10000000u;  // 10 s
    }
    advanceUs(50);
  }
  const ProfileStat& child = stat(ProfileId::PlaybackPrewarm_slot);
  TEST_ASSERT_EQUAL_UINT32(10000000u, child.maxUs);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, child.maxCycles);
  TEST_ASSERT_EQUAL_UINT64(6000000000ull, child.inclusiveCycles);  // > UINT32_MAX, kept in u64
  const ProfileStat& parent = stat(ProfileId::RecordStart_edge);
  // The parent's own window is 10.00015 s: coarse as well, and its self time excludes the child.
  TEST_ASSERT_EQUAL_UINT32(10000150u, parent.maxUs);
  TEST_ASSERT_EQUAL_UINT64(150ull * kCyclesPerUs, parent.selfCycles);
  ProfileRingRecord r{};
  TEST_ASSERT_TRUE(ringAt(0, r));
  TEST_ASSERT_EQUAL_UINT8(kRingFlagCoarse, r.flags & kRingFlagCoarse);
  TEST_ASSERT_EQUAL_UINT32(10000000u, r.micros);
}

void test_out_of_order_exit_poisons_until_reset_at_depth_zero() {
  boot();
  auto* outer = new Scope(ProfileId::RecordStart_edge, true);
  auto* inner = new Scope(ProfileId::SelfTest, false);
  advanceUs(10);
  delete outer;  // exits while `inner` is innermost
  TEST_ASSERT_TRUE(counters().poisoned);
  TEST_ASSERT_EQUAL_UINT16(1, stat(ProfileId::RecordStart_edge).orderFaults);
  TEST_ASSERT_EQUAL_UINT32(0, stat(ProfileId::RecordStart_edge).calls);
  delete inner;  // its frame was dropped with the outer one: also a fault, no duration
  TEST_ASSERT_EQUAL_UINT32(0, self().calls);
  TEST_ASSERT_EQUAL_UINT16(1, self().orderFaults);
  TEST_ASSERT_EQUAL_UINT8(0, counters().depth);
  // Poisoned: a well-nested scope still unwinds but records nothing.
  { PROFILE_SCOPE_SHORT(s, ProfileId::SelfTest); advanceUs(1); }
  TEST_ASSERT_EQUAL_UINT32(0, self().calls);
  // Reset is refused inside a live scope and permitted at depth zero.
  {
    PROFILE_SCOPE_SHORT(s, ProfileId::SelfTest);
    TEST_ASSERT_FALSE(reset());
    TEST_ASSERT_TRUE(counters().poisoned);
  }
  TEST_ASSERT_TRUE(reset());
  TEST_ASSERT_FALSE(counters().poisoned);
  TEST_ASSERT_EQUAL_UINT16(0, stat(ProfileId::RecordStart_edge).orderFaults);
  { PROFILE_SCOPE_SHORT(s, ProfileId::SelfTest); advanceUs(1); }
  TEST_ASSERT_EQUAL_UINT32(1, self().calls);
}

namespace {
void nest(int remaining) {
  PROFILE_SCOPE_SHORT(s, ProfileId::SelfTest);
  advanceUs(1);
  if (remaining > 1) nest(remaining - 1);
}
}  // namespace

void test_depth_overflow_is_counted_and_not_double_counted() {
  boot();
  {
    PROFILE_SCOPE_SHORT(root, ProfileId::RecordStart_edge);
    nest(static_cast<int>(kMaxDepth) + 2);  // 15 tracked below root, 3 overflowed
  }
  TEST_ASSERT_EQUAL_UINT32(3, counters().depthOverflow);
  TEST_ASSERT_EQUAL_UINT8(0, counters().depth);
  TEST_ASSERT_EQUAL_UINT32(kMaxDepth + 2, self().calls);
  TEST_ASSERT_FALSE(counters().poisoned);
  // Inclusive of the chain is 18 us at the top; self time sums to exactly the 18 us once.
  const ProfileStat& root = stat(ProfileId::RecordStart_edge);
  TEST_ASSERT_EQUAL_UINT64(18ull * kCyclesPerUs, root.inclusiveCycles);
  TEST_ASSERT_EQUAL_UINT64(0, root.selfCycles);
  // Tracked frames: self = 1 us each (15); the outermost overflowed scope reports its 3 us to the
  // deepest tracked frame, so that frame's self is 1 us, and the overflowed scopes record self =
  // inclusive (3 + 2 + 1). Total self = 15 + 6 = 21 us, inclusive is never added twice to a parent.
  TEST_ASSERT_EQUAL_UINT64(21ull * kCyclesPerUs, self().selfCycles);
}

namespace {
void nestSlow(int remaining) {
  PROFILE_SCOPE_SHORT(s, ProfileId::SelfTest);
  advanceUs(remaining == 1 ? 2500 : 1);
  if (remaining > 1) nestSlow(remaining - 1);
}
}  // namespace

void test_an_overflowed_breach_names_no_parent() {
  boot();
  nestSlow(static_cast<int>(kMaxDepth) + 1);  // the 17th scope overflows and breaches the frame ceiling
  ProfileRingRecord r{};
  TEST_ASSERT_TRUE(ringAt(0, r));
  TEST_ASSERT_EQUAL_UINT8(kRingFlagOverflowed, r.flags & kRingFlagOverflowed);
  TEST_ASSERT_EQUAL_UINT16(kNoParent, r.parentId);
  TEST_ASSERT_EQUAL_UINT32(2500, r.micros);
}

void test_a_tracked_exit_with_a_live_overflowed_child_is_an_order_fault() {
  boot();
  std::vector<Scope*> tracked;
  for (size_t i = 0; i < kMaxDepth; ++i) tracked.push_back(new Scope(ProfileId::SelfTest, false));
  auto* overflowed = new Scope(ProfileId::RecordStart_edge, false);
  advanceUs(5);
  delete tracked.back();  // frame 15 exits while its overflowed child is live
  tracked.pop_back();
  TEST_ASSERT_TRUE(counters().poisoned);
  TEST_ASSERT_EQUAL_UINT32(0, self().calls);
  delete overflowed;
  while (!tracked.empty()) {
    delete tracked.back();
    tracked.pop_back();
  }
  TEST_ASSERT_EQUAL_UINT8(0, counters().depth);
  TEST_ASSERT_EQUAL_UINT32(0, stat(ProfileId::RecordStart_edge).calls);
  TEST_ASSERT_TRUE(reset());
}

void test_a_scope_before_init_is_refused_on_cold_garbage() {
  std::memset(&detail::gState, 0x5A, sizeof(detail::gState));  // cold boot: magic is garbage
  { PROFILE_SCOPE_LONG(s, ProfileId::SelfTest); advanceUs(3000); }
  TEST_ASSERT_EQUAL_UINT8(0x5A, reinterpret_cast<const uint8_t*>(&detail::gState.ring)[0]);
  boot();
  TEST_ASSERT_EQUAL_UINT32(0, self().calls);
}

void test_a_scope_before_init_after_a_warm_reset_stays_in_bounds() {
  // Warm reset: the previous boot's magic survives with corrupt indices. Every index is bounded on
  // use, so the scope may only touch the profiler's own state, which `initAtBoot()` then clears.
  std::memset(&detail::gState, 0xFF, sizeof(detail::gState));
  detail::gState.initMagic = detail::kInitMagic;
  detail::gState.cpuHz = 600000000u;
  detail::gState.cycleTrustUs = kTrustUs600;
  // Nesting reads clean so the breach reaches the ring write; only the ring head and count are corrupt.
  detail::gState.counters.poisoned = false;
  detail::gState.counters.depth = 0;
  detail::gState.overflowDepth = 0;
  { PROFILE_SCOPE_LONG(s, ProfileId::SelfTest); advanceUs(3000); }  // breaches: writes the ring
  // A corrupt head of 0xFFFF lands on the last slot (masked), not 65,535 records past the ring.
  TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(ProfileId::SelfTest),
                           detail::gState.ring[kRingCapacity - 1].id);
  boot();
  TEST_ASSERT_EQUAL_UINT32(0, self().calls);
  TEST_ASSERT_EQUAL_UINT16(0, counters().ringCount);
}

void test_reset_keeps_the_interrupt_refusal_count() {
  boot();
  gIpsr = 11;
  { PROFILE_SCOPE_SHORT(s, ProfileId::SelfTest); }
  gIpsr = 0;
  TEST_ASSERT_TRUE(reset());
  TEST_ASSERT_EQUAL_UINT32(1, counters().isrRefused);
}

void test_exception_context_entry_is_refused_without_touching_nesting() {
  boot();
  {
    PROFILE_SCOPE_LONG(outer, ProfileId::RecordStart_edge);
    advanceUs(5);
    gIpsr = 15;  // SysTick
    { PROFILE_SCOPE_SHORT(isr, ProfileId::SelfTest); advanceUs(2); }
    gIpsr = 0;
    TEST_ASSERT_EQUAL_UINT8(1, counters().depth);
    advanceUs(3);
  }
  TEST_ASSERT_EQUAL_UINT32(1, counters().isrRefused);
  TEST_ASSERT_EQUAL_UINT32(0, self().calls);
  TEST_ASSERT_EQUAL_UINT32(0, self().refused);
  TEST_ASSERT_FALSE(counters().poisoned);
  const ProfileStat& outer = stat(ProfileId::RecordStart_edge);
  TEST_ASSERT_EQUAL_UINT32(1, outer.calls);
  TEST_ASSERT_EQUAL_UINT64(10ull * kCyclesPerUs, outer.selfCycles);  // interrupt time is included
}

void test_reset_keeps_seq_monotonic() {
  boot();
  { PROFILE_SCOPE_SHORT(s, ProfileId::SelfTest); advanceUs(2500); }
  TEST_ASSERT_TRUE(reset());
  TEST_ASSERT_EQUAL_UINT16(0, counters().ringCount);
  { PROFILE_SCOPE_SHORT(s, ProfileId::SelfTest); advanceUs(2500); }
  ProfileRingRecord r{};
  TEST_ASSERT_TRUE(ringAt(0, r));
  TEST_ASSERT_EQUAL_UINT32(2, r.seq);
}

void test_every_identity_has_a_unique_nonempty_name_and_a_declared_ceiling() {
  std::vector<std::string> names;
  for (size_t i = 0; i < static_cast<size_t>(ProfileId::Count); ++i) {
    const char* n = ProfileDiagRows::profileName(static_cast<ProfileId>(i));
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_TRUE(std::strlen(n) > 0);
    TEST_ASSERT_TRUE(std::strcmp(n, "Unknown") != 0);
    for (const auto& seen : names) TEST_ASSERT_TRUE(seen != n);
    names.emplace_back(n);
    if (static_cast<ProfileId>(i) != ProfileId::Calibration) {
      TEST_ASSERT_TRUE(ceilingUs(static_cast<ProfileId>(i)) < kCycleTrustGuardUs);
    }
  }
  TEST_ASSERT_EQUAL_UINT32(kOwnerTurnCeilingUs, ceilingUs(ProfileId::IdleOwner_unit));
  TEST_ASSERT_EQUAL_UINT32(kDeferredCeilingUs, ceilingUs(ProfileId::ViewPrewarm_unit));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_boot_clears_a_prefilled_ram2_image);
  RUN_TEST(test_boot_row_and_calibration_arithmetic);
  RUN_TEST(test_unavailable_source_refuses_short_and_long_and_still_emits_the_boot_row);
  RUN_TEST(test_every_exit_path_is_timed_and_zero_duration_is_valid);
  RUN_TEST(test_parent_self_time_excludes_measured_children);
  RUN_TEST(test_result_and_context_are_optional_and_never_fabricated);
  RUN_TEST(test_only_over_ceiling_calls_enter_the_ring_with_their_parent);
  RUN_TEST(test_ring_overwrite_counts_drops_and_keeps_a_seq_gap);
  RUN_TEST(test_long_scope_source_selection_boundary_is_exact);
  RUN_TEST(test_short_scope_never_reads_the_wall_clock);
  RUN_TEST(test_coarse_multi_second_child_is_not_truncated_in_the_parent);
  RUN_TEST(test_out_of_order_exit_poisons_until_reset_at_depth_zero);
  RUN_TEST(test_depth_overflow_is_counted_and_not_double_counted);
  RUN_TEST(test_an_overflowed_breach_names_no_parent);
  RUN_TEST(test_a_tracked_exit_with_a_live_overflowed_child_is_an_order_fault);
  RUN_TEST(test_a_scope_before_init_is_refused_on_cold_garbage);
  RUN_TEST(test_a_scope_before_init_after_a_warm_reset_stays_in_bounds);
  RUN_TEST(test_reset_keeps_the_interrupt_refusal_count);
  RUN_TEST(test_exception_context_entry_is_refused_without_touching_nesting);
  RUN_TEST(test_reset_keeps_seq_monotonic);
  RUN_TEST(test_every_identity_has_a_unique_nonempty_name_and_a_declared_ceiling);
  return UNITY_END();
}
