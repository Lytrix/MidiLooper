#include <unity.h>
#include <array>
#include "Utils/MidiServiceDrain.h"
#include "Utils/PlaybackPhysicalDeadline.h"

using namespace MidiServiceDrain;
namespace {
// Illustrative hardware/cost boundaries, not deployment budgets or a real producer.
PlaybackPhysicalDeadline reference() { return {100, 101, 3, 9, 1000, 200, 5, true}; }
PhysicalClockStamp at(uint64_t us) { return {us, 9}; }
CheckpointCostTable costs() {
  CheckpointCostTable result{};
  for (auto& cost : result) cost = {10, 5, true};
  return result;
}
constexpr PlaybackDeadlineClock clockStamp{100, 3};
void unchanged(const DeferredIdleOwnerCursor& a, const DeferredIdleOwnerCursor& b,
               const InputCheckpointCursor& c, const InputCheckpointCursor& d) {
  TEST_ASSERT_EQUAL_UINT8(unsigned(a.owner), unsigned(b.owner));
  TEST_ASSERT_EQUAL(a.extraSnapshotPending, b.extraSnapshotPending);
  TEST_ASSERT_EQUAL_UINT8(unsigned(c.owedNormal), unsigned(d.owedNormal));
  TEST_ASSERT_EQUAL(c.normalPending, d.normalPending);
}
}

void test_unqualified_reference_and_cost_consume_nothing() {
  DeferredIdleOwnerCursor normal{};
  InputCheckpointCursor checkpoint{DeferredIdleOwner::Persistence, true};
  const auto original = normal; const auto owed = checkpoint;
  auto table = costs();
  auto decision = selectInputCheckpointOwner(normal, checkpoint, true, true, true,
      {}, clockStamp, at(1000), table);
  TEST_ASSERT_FALSE(decision.dispatchOwner);
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::ReferenceUnavailable), unsigned(decision.admission));
  unchanged(original, normal, owed, checkpoint);
  table[unsigned(DeferredIdleOwner::Persistence)].established = false;
  decision = selectInputCheckpointOwner(normal, checkpoint, true, true, true,
      reference(), clockStamp, at(1000), table);
  TEST_ASSERT_FALSE(decision.dispatchOwner);
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::CostUnavailable), unsigned(decision.admission));
  unchanged(original, normal, owed, checkpoint);
}
void test_inserted_window_retains_mandatory_normal_and_its_cost() {
  DeferredIdleOwnerCursor normal{}; InputCheckpointCursor checkpoint{};
  auto table = costs();
  auto decision = selectInputCheckpointOwner(normal, checkpoint, true, false, false,
      reference(), clockStamp, at(1000), table);
  TEST_ASSERT_TRUE(decision.dispatchOwner && decision.insertedWindow);
  TEST_ASSERT_EQUAL_UINT8(unsigned(DeferredIdleOwner::PlaybackViewMaintenance), unsigned(decision.owner));
  TEST_ASSERT_TRUE(checkpoint.normalPending);
  TEST_ASSERT_EQUAL_UINT8(unsigned(DeferredIdleOwner::Snapshot), unsigned(checkpoint.owedNormal));
  const auto savedNormal = normal; const auto savedCheckpoint = checkpoint;
  table[unsigned(DeferredIdleOwner::Snapshot)] = {190, 10, true};
  decision = selectInputCheckpointOwner(normal, checkpoint, true, true, true,
      reference(), clockStamp, at(1000), table);
  TEST_ASSERT_FALSE(decision.dispatchOwner); // Window's cheap bound cannot admit expensive saved Snapshot.
  unchanged(savedNormal, normal, savedCheckpoint, checkpoint);
  table[unsigned(DeferredIdleOwner::Snapshot)] = {10, 5, true};
  decision = selectInputCheckpointOwner(normal, checkpoint, true, true, true,
      reference(), clockStamp, at(1000), table);
  TEST_ASSERT_TRUE(decision.dispatchOwner && !decision.insertedWindow);
  TEST_ASSERT_EQUAL_UINT8(unsigned(DeferredIdleOwner::Snapshot), unsigned(decision.owner));
  TEST_ASSERT_FALSE(checkpoint.normalPending);
  TEST_ASSERT_EQUAL_UINT8(unsigned(savedNormal.owner), unsigned(normal.owner));
}
void test_expired_time_retains_owed_owner_until_fresh_reference() {
  DeferredIdleOwnerCursor normal{DeferredIdleOwner::VisualCache, false};
  InputCheckpointCursor checkpoint{DeferredIdleOwner::Persistence, true};
  const auto savedNormal = normal; const auto savedCheckpoint = checkpoint;
  auto decision = selectInputCheckpointOwner(normal, checkpoint, true, false, false,
      reference(), clockStamp, at(1200), costs());
  TEST_ASSERT_FALSE(decision.dispatchOwner);
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::PhysicalDeadlinePassed), unsigned(decision.admission));
  unchanged(savedNormal, normal, savedCheckpoint, checkpoint);
  auto fresh = reference(); fresh.anchorUs = 1200;
  decision = selectInputCheckpointOwner(normal, checkpoint, true, false, false,
      fresh, clockStamp, at(1200), costs()); // Explicit producer-supplied reference, never automatic restamping.
  TEST_ASSERT_TRUE(decision.dispatchOwner);
  TEST_ASSERT_EQUAL_UINT8(unsigned(DeferredIdleOwner::Persistence), unsigned(decision.owner));
}
void test_normal_subsequence_matches_existing_selector_under_all_urgency_modes() {
  for (uint8_t owner = 0; owner < kDeferredIdleOwnerCount; ++owner)
    for (bool extra : {false, true}) for (bool backlog : {false, true})
      for (bool exitDrain : {false, true}) for (unsigned urgencyMode = 0; urgencyMode < 4; ++urgencyMode) {
        DeferredIdleOwnerCursor normal{static_cast<DeferredIdleOwner>(owner), extra};
        auto baseline = normal; InputCheckpointCursor checkpoint{};
        unsigned ordinary = 0, opportunities = 0;
        while (ordinary < 56 && opportunities < 112) {
          const bool urgent = urgencyMode == 1 || (urgencyMode == 2 && opportunities % 2) ||
                              (urgencyMode == 3 && opportunities % 3 == 0);
          const auto decision = selectInputCheckpointOwner(normal, checkpoint, urgent, backlog, exitDrain,
              reference(), clockStamp, at(1000), costs());
          ++opportunities;
          TEST_ASSERT_TRUE(decision.dispatchOwner);
          if (!decision.insertedWindow) {
            const auto expected = selectDeferredIdleOwner(baseline, backlog, exitDrain);
            TEST_ASSERT_EQUAL_UINT8(unsigned(expected), unsigned(decision.owner));
            ++ordinary;
          }
          TEST_ASSERT_TRUE(opportunities <= 2 * (ordinary + 1));
        }
        TEST_ASSERT_EQUAL_UINT32(56, ordinary);
        TEST_ASSERT_FALSE(checkpoint.normalPending);
        TEST_ASSERT_EQUAL_UINT8(unsigned(baseline.owner), unsigned(normal.owner));
        TEST_ASSERT_EQUAL(baseline.extraSnapshotPending, normal.extraSnapshotPending);
      }
}
void test_deadline_boundary_uncertainty_and_large_cost_sums() {
  const auto ref = reference(); const PhysicalCheckpointCost cost{10, 5, true};
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::Allowed), unsigned(ref.admit(clockStamp, at(1179), cost)));
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::InsufficientTime), unsigned(ref.admit(clockStamp, at(1180), cost)));
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::PhysicalDeadlinePassed), unsigned(ref.admit(clockStamp, at(1200), cost)));
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::InsufficientTime), unsigned(ref.admit(clockStamp, at(1000), {UINT32_MAX, UINT32_MAX, true})));
  auto uncertain = ref; uncertain.uncertaintyUs = UINT32_MAX;
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::InsufficientTime), unsigned(uncertain.admit(clockStamp, at(1000), cost)));
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::CostUnavailable), unsigned(ref.admit(clockStamp, at(1000), {0, 5, true})));
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::CostUnavailable), unsigned(ref.admit(clockStamp, at(1000), {10, 0, true})));
}
void test_logical_and_epoch_invalidation_cannot_mint_lead() {
  const auto ref = reference(); const PhysicalCheckpointCost cost{10, 5, true};
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::PositionInvalidated), unsigned(ref.admit({100, 4}, at(1000), cost)));
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::PositionInvalidated), unsigned(ref.admit({100, 0}, at(1000), cost)));
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::PositionInvalidated), unsigned(ref.admit({99, 3}, at(1000), cost)));
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::LogicalDeadlinePassed), unsigned(ref.admit({101, 3}, at(1000), cost)));
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::PhysicalDeadlinePassed), unsigned(ref.admit({100, 3}, at(1201), cost)));
}
void test_extended_microseconds_wrap_backwards_and_counter_end() {
  auto ref = reference(); ref.anchorUs = uint64_t(UINT32_MAX) - 10; ref.leadUs = 40; ref.uncertaintyUs = 0;
  const PhysicalCheckpointCost cost{10, 5, true};
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::Allowed), unsigned(ref.admit(clockStamp, at(uint64_t(UINT32_MAX) + 6), cost)));
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::PositionInvalidated), unsigned(ref.admit(clockStamp, at(ref.anchorUs - 1), cost)));
  ref = reference(); ref.leadUs = UINT64_MAX;
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::ReferenceUnavailable), unsigned(ref.admit(clockStamp, at(1000), cost)));
  ref = reference(); ref.leadUs = 0;
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::ReferenceUnavailable), unsigned(ref.admit(clockStamp, at(1000), cost)));
  ref = reference(); ref.anchorUs = UINT64_MAX - 40; ref.leadUs = 40; ref.uncertaintyUs = 0;
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::Allowed), unsigned(ref.admit(clockStamp, at(UINT64_MAX - 20), cost)));
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::PhysicalDeadlinePassed), unsigned(ref.admit(clockStamp, at(UINT64_MAX), cost)));
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::PositionInvalidated), unsigned(ref.admit(clockStamp, at(0), cost)));
}
void test_refused_preview_reports_no_insert_and_invalid_owner_is_safe() {
  DeferredIdleOwnerCursor normal{}; InputCheckpointCursor checkpoint{};
  const auto saved = normal; const auto debt = checkpoint;
  auto decision = selectInputCheckpointOwner(normal, checkpoint, true, false, false,
      {}, clockStamp, at(1000), costs());
  TEST_ASSERT_FALSE(decision.dispatchOwner || decision.insertedWindow);
  unchanged(saved, normal, debt, checkpoint);
  checkpoint = {static_cast<DeferredIdleOwner>(255), true};
  const auto invalid = checkpoint;
  decision = selectInputCheckpointOwner(normal, checkpoint, true, false, false,
      reference(), clockStamp, at(1000), costs());
  TEST_ASSERT_FALSE(decision.dispatchOwner || decision.insertedWindow);
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::CostUnavailable), unsigned(decision.admission));
  unchanged(saved, normal, invalid, checkpoint);
}
void test_reference_provenance_and_geometry_are_explicit() {
  const PhysicalCheckpointCost cost{10, 5, true};
  auto ref = reference(); ref.independent = false;
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::ReferenceUnavailable), unsigned(ref.admit(clockStamp, at(1000), cost)));
  ref = reference(); ref.clockEpoch = 0;
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::ReferenceUnavailable), unsigned(ref.admit(clockStamp, at(1000), cost)));
  ref = reference(); ref.adoptionTick = ref.anchorTick;
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::ReferenceUnavailable), unsigned(ref.admit(clockStamp, at(1000), cost)));
}
void test_full_microsecond_wrap_cannot_resurrect_expired_reference() {
  const auto ref = reference(); const PhysicalCheckpointCost cost{10, 5, true};
  const uint64_t physicallyLate = ref.anchorUs + (uint64_t{1} << 32);
  TEST_ASSERT_EQUAL_MESSAGE(unsigned(PhysicalCheckpointResult::PhysicalDeadlinePassed),
      unsigned(ref.admit(clockStamp, at(physicallyLate), cost)), "full physical wrap must not appear as fresh lead");
}
void test_unknown_or_recycled_physical_era_is_rejected() {
  const PhysicalCheckpointCost cost{10, 5, true};
  auto ref = reference();
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::PositionInvalidated), unsigned(ref.admit(clockStamp, {1000, 10}, cost)));
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::PositionInvalidated), unsigned(ref.admit(clockStamp, {1000, 0}, cost)));
  ref.physicalEra = 0;
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::ReferenceUnavailable), unsigned(ref.admit(clockStamp, at(1000), cost)));
}
void test_inserted_window_uses_its_own_cost_and_refuses_without_debt() {
  DeferredIdleOwnerCursor normal{}; InputCheckpointCursor checkpoint{};
  const auto saved = normal; const auto debt = checkpoint;
  auto table = costs();
  table[unsigned(DeferredIdleOwner::PlaybackViewMaintenance)] = {190, 10, true};
  auto decision = selectInputCheckpointOwner(normal, checkpoint, true, false, false,
      reference(), clockStamp, at(1000), table);
  TEST_ASSERT_FALSE(decision.dispatchOwner || decision.insertedWindow);
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::InsufficientTime), unsigned(decision.admission));
  unchanged(saved, normal, debt, checkpoint);
  table[unsigned(DeferredIdleOwner::PlaybackViewMaintenance)].established = false;
  decision = selectInputCheckpointOwner(normal, checkpoint, true, false, false,
      reference(), clockStamp, at(1000), table);
  TEST_ASSERT_FALSE(decision.dispatchOwner || decision.insertedWindow);
  TEST_ASSERT_EQUAL(unsigned(PhysicalCheckpointResult::CostUnavailable), unsigned(decision.admission));
  unchanged(saved, normal, debt, checkpoint);
  decision = selectInputCheckpointOwner(normal, checkpoint, false, false, false,
      reference(), clockStamp, at(1000), table);
  TEST_ASSERT_TRUE(decision.dispatchOwner && !decision.insertedWindow);
  TEST_ASSERT_EQUAL_UINT8(unsigned(DeferredIdleOwner::Snapshot), unsigned(decision.owner));
}
void test_changed_snapshot_flags_cannot_replace_saved_non_snapshot_owner() {
  DeferredIdleOwnerCursor normal{DeferredIdleOwner::LoadAndDisplay, false};
  InputCheckpointCursor checkpoint{};
  auto decision = selectInputCheckpointOwner(normal, checkpoint, true, false, false,
      reference(), clockStamp, at(1000), costs());
  TEST_ASSERT_TRUE(decision.dispatchOwner && decision.insertedWindow);
  TEST_ASSERT_EQUAL_UINT8(unsigned(DeferredIdleOwner::LoadAndDisplay), unsigned(checkpoint.owedNormal));
  const auto saved = normal;
  decision = selectInputCheckpointOwner(normal, checkpoint, true, true, true,
      reference(), clockStamp, at(1000), costs());
  TEST_ASSERT_TRUE(decision.dispatchOwner && !decision.insertedWindow);
  TEST_ASSERT_EQUAL_UINT8(unsigned(DeferredIdleOwner::LoadAndDisplay), unsigned(decision.owner));
  TEST_ASSERT_FALSE(checkpoint.normalPending);
  TEST_ASSERT_EQUAL_UINT8(unsigned(saved.owner), unsigned(normal.owner));
  TEST_ASSERT_EQUAL(saved.extraSnapshotPending, normal.extraSnapshotPending);
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_unqualified_reference_and_cost_consume_nothing);
  RUN_TEST(test_inserted_window_retains_mandatory_normal_and_its_cost);
  RUN_TEST(test_expired_time_retains_owed_owner_until_fresh_reference);
  RUN_TEST(test_normal_subsequence_matches_existing_selector_under_all_urgency_modes);
  RUN_TEST(test_deadline_boundary_uncertainty_and_large_cost_sums);
  RUN_TEST(test_logical_and_epoch_invalidation_cannot_mint_lead);
  RUN_TEST(test_extended_microseconds_wrap_backwards_and_counter_end);
  RUN_TEST(test_full_microsecond_wrap_cannot_resurrect_expired_reference);
  RUN_TEST(test_unknown_or_recycled_physical_era_is_rejected);
  RUN_TEST(test_refused_preview_reports_no_insert_and_invalid_owner_is_safe);
  RUN_TEST(test_reference_provenance_and_geometry_are_explicit);
  RUN_TEST(test_inserted_window_uses_its_own_cost_and_refuses_without_debt);
  RUN_TEST(test_changed_snapshot_flags_cannot_replace_saved_non_snapshot_owner);
  return UNITY_END();
}
