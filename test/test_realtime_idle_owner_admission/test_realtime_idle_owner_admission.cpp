// `realtime-idle-owner-admission-bound` — owner-admission contract (packet RIAO-1, cases A-G).
//
// These cases pin the *contract*, not a particular call site: `runDeferredIdleMaintenance` lives in
// main.cpp and is not linkable natively, so the dispatch decision was extracted into
// `Utils/MidiServiceDrain.h` and is exercised here through the same function production calls.
//
// Numeric limits are the registry's declared maxima. The native assertion is in **work units /
// declared microseconds**, never in host wall-clock, so the suite is deterministic.

#include <unity.h>

#include "Utils/MidiServiceDrain.h"

using namespace MidiServiceDrain;

namespace {

IdleOwnerChildDecision decisionFor(IdleOwnerChild child, bool owed, bool timingCritical,
                                   uint32_t remaining) {
  return admitIdleOwnerChild(child, owed, timingCritical, remaining).decision;
}

}  // namespace

// RIAO-1.A — an unbounded child is never dispatched while timing-critical.
void test_riao1_a_unbounded_child_defers_while_timing_critical() {
  const IdleOwnerChild unbounded[] = {
      IdleOwnerChild::OverdubSourceSpanPrewarm,
      IdleOwnerChild::WholeLoopDisplayResolve,
  };
  for (IdleOwnerChild child : unbounded) {
    const IdleOwnerAdmission admission = admitIdleOwnerChild(child, true, true, kOwnerWorkBudgetUs);
    TEST_ASSERT_EQUAL(IdleOwnerChildDecision::Defer, admission.decision);
    TEST_ASSERT_EQUAL_UINT32(0, admission.grantedUs);
    TEST_ASSERT_NOT_NULL(admission.name);
    TEST_ASSERT_TRUE(admission.name[0] != '\0');
  }
}

// RIAO-1.A control — an unbounded child still runs when nothing is timing-critical. This is the
// half of the rule that keeps STOPPED behaviour byte-identical for every unbounded child.
void test_riao1_a_control_unbounded_child_runs_when_not_timing_critical() {
  TEST_ASSERT_EQUAL(IdleOwnerChildDecision::Run,
                    decisionFor(IdleOwnerChild::OverdubSourceSpanPrewarm, true, false, 0));
  TEST_ASSERT_EQUAL(IdleOwnerChildDecision::Run,
                    decisionFor(IdleOwnerChild::WholeLoopDisplayResolve, true, false, 0));
}

// Task 7.1/7.2 — the stored-MIDI verification/SEVT dump is now a bounded child. The production gate is
// header-only (no registry-array ODR use from the hot Track TU); the constants are pinned to the
// registry here.
void test_riao7_1_bounded_verification_admits_the_declared_grain() {
  const IdleOwnerChildBound& verification =
      idleOwnerChildBound(IdleOwnerChild::StoredMidiVerification);
  const IdleOwnerChildBound& sevt = idleOwnerChildBound(IdleOwnerChild::SevtDump);
  TEST_ASSERT_FALSE(verification.unbounded);
  TEST_ASSERT_FALSE(sevt.unbounded);
  TEST_ASSERT_EQUAL_UINT32(kStoredVerificationMaxUs, verification.maxUs);
  TEST_ASSERT_EQUAL_UINT32(kStoredVerificationFineGrainUs, verification.fineGrainUs);
  TEST_ASSERT_EQUAL_UINT32(kStoredVerificationMaxUs, sevt.maxUs);
  TEST_ASSERT_EQUAL_UINT32(kStoredVerificationFineGrainUs, sevt.fineGrainUs);

  // Under transport: the declared max fits the owner budget, so the child is admitted.
  TEST_ASSERT_TRUE(boundedIdleOwnerChildMayRun(true, true, kStoredVerificationMaxUs,
                                               kOwnerWorkBudgetUs,
                                               kStoredVerificationFineGrainUs));
  // Only the fine grain fits: still admitted for that grain (the derive is one bar per turn).
  TEST_ASSERT_TRUE(boundedIdleOwnerChildMayRun(true, true, kStoredVerificationMaxUs,
                                               kStoredVerificationFineGrainUs,
                                               kStoredVerificationFineGrainUs));
  // Below the fine grain: denied (the 2.3 fallback).
  TEST_ASSERT_FALSE(boundedIdleOwnerChildMayRun(true, true, kStoredVerificationMaxUs,
                                                kStoredVerificationFineGrainUs - 1,
                                                kStoredVerificationFineGrainUs));
  // Nothing owed, or a stopped lane: the child costs nothing / runs without the budget.
  TEST_ASSERT_FALSE(boundedIdleOwnerChildMayRun(false, true, kStoredVerificationMaxUs,
                                                kOwnerWorkBudgetUs,
                                                kStoredVerificationFineGrainUs));
  TEST_ASSERT_TRUE(boundedIdleOwnerChildMayRun(true, false, kStoredVerificationMaxUs, 0, 0));
}

// DEC-237 Slice 3 — the staged display base is a declared, bounded idle owner child. It is a 4 ms child
// with a 1 ms fine grain (one tile or one boundary pass per turn).
// DEC-246 C2b: the shadow store's edit-index grain is a declared, bounded child; the header gate constants mirror it.
void test_c2b_revision_edit_index_grain_is_declared() {
  const IdleOwnerChildBound& bound = idleOwnerChildBound(IdleOwnerChild::RevisionEditIndexGrain);
  TEST_ASSERT_EQUAL_STRING("revision_edit_index", bound.name);
  TEST_ASSERT_EQUAL_UINT16(2000, bound.maxUs);
  TEST_ASSERT_EQUAL_UINT16(500, bound.fineGrainUs);
  TEST_ASSERT_FALSE(bound.unbounded);
  TEST_ASSERT_EQUAL_UINT32(bound.maxUs, kRevisionEditIndexMaxUs);
  TEST_ASSERT_EQUAL_UINT32(bound.fineGrainUs, kRevisionEditIndexFineGrainUs);
  TEST_ASSERT_TRUE(boundedIdleOwnerChildMayRun(true, true, kRevisionEditIndexMaxUs, kOwnerWorkBudgetUs,
                                               kRevisionEditIndexFineGrainUs));
  TEST_ASSERT_FALSE(boundedIdleOwnerChildMayRun(true, true, kRevisionEditIndexMaxUs,
                                                kRevisionEditIndexFineGrainUs - 1u, kRevisionEditIndexFineGrainUs));
}

// DEC-246 C3b: the shadow import is its own declared, bounded child.
void test_c3b_revision_shadow_import_grain_is_declared() {
  const IdleOwnerChildBound& bound = idleOwnerChildBound(IdleOwnerChild::RevisionShadowImportGrain);
  TEST_ASSERT_EQUAL_STRING("revision_shadow_import", bound.name);
  TEST_ASSERT_EQUAL_UINT16(2000, bound.maxUs);
  TEST_ASSERT_EQUAL_UINT16(500, bound.fineGrainUs);
  TEST_ASSERT_FALSE(bound.unbounded);
  TEST_ASSERT_EQUAL_UINT32(bound.maxUs, kRevisionShadowImportMaxUs);
  TEST_ASSERT_EQUAL_UINT32(bound.fineGrainUs, kRevisionShadowImportFineGrainUs);
}

// DEC-246 C3d: the shadow window producer is its own declared, bounded child.
void test_c3d_revision_shadow_window_grain_is_declared() {
  const IdleOwnerChildBound& bound = idleOwnerChildBound(IdleOwnerChild::RevisionShadowWindowGrain);
  TEST_ASSERT_EQUAL_STRING("revision_shadow_window", bound.name);
  TEST_ASSERT_EQUAL_UINT16(2000, bound.maxUs);
  TEST_ASSERT_EQUAL_UINT16(500, bound.fineGrainUs);
  TEST_ASSERT_EQUAL_UINT32(bound.maxUs, kRevisionShadowWindowMaxUs);
  TEST_ASSERT_EQUAL_UINT32(bound.fineGrainUs, kRevisionShadowWindowFineGrainUs);
}

void test_dec237_display_base_grain_is_declared() {
  const IdleOwnerChildBound& bound = idleOwnerChildBound(IdleOwnerChild::DisplayBaseGrain);
  TEST_ASSERT_EQUAL_STRING("display_base_grain", bound.name);
  TEST_ASSERT_EQUAL_UINT16(4000, bound.maxUs);
  TEST_ASSERT_EQUAL_UINT16(1000, bound.fineGrainUs);
  TEST_ASSERT_FALSE(bound.unbounded);
  TEST_ASSERT_TRUE(boundedIdleOwnerChildMayRun(true, true, bound.maxUs, kOwnerWorkBudgetUs,
                                               bound.fineGrainUs));
  TEST_ASSERT_TRUE(boundedIdleOwnerChildMayRun(true, true, bound.maxUs, bound.fineGrainUs,
                                               bound.fineGrainUs));
  TEST_ASSERT_FALSE(boundedIdleOwnerChildMayRun(true, true, bound.maxUs,
                                                static_cast<uint32_t>(bound.fineGrainUs) - 1u,
                                                bound.fineGrainUs));
  TEST_ASSERT_FALSE(boundedIdleOwnerChildMayRun(false, true, bound.maxUs, kOwnerWorkBudgetUs,
                                                bound.fineGrainUs));
}

// Not owed costs nothing in either regime: the turn must not spend budget on a child with no work.
void test_riao1_not_owed_costs_nothing() {
  for (uint8_t i = 0; i < kIdleOwnerChildCount; ++i) {
    const IdleOwnerChild child = static_cast<IdleOwnerChild>(i);
    TEST_ASSERT_EQUAL(IdleOwnerChildDecision::NotOwed, decisionFor(child, false, true, kOwnerWorkBudgetUs));
    TEST_ASSERT_EQUAL(IdleOwnerChildDecision::NotOwed, decisionFor(child, false, false, 0));
  }
}

// RIAO-1.B — reservation rejects a child whose declared maximum cannot fit and which is not finer
// resumable. This is the case that used to overshoot silently.
void test_riao1_b_child_that_cannot_fit_is_not_dispatched() {
  const uint32_t justUnder = static_cast<uint32_t>(idleOwnerChildBound(IdleOwnerChild::VisualCacheSlice).maxUs) - 1u;
  TEST_ASSERT_EQUAL(IdleOwnerChildDecision::Defer,
                    decisionFor(IdleOwnerChild::VisualCacheSlice, true, true, justUnder));
  // A zero remaining budget admits nothing bounded, however small its grain.
  TEST_ASSERT_EQUAL(IdleOwnerChildDecision::Defer,
                    decisionFor(IdleOwnerChild::PlaybackWindowGrain, true, true, 0));
}

// RIAO-1.C — a finer-grained child may take the remaining deadline, and never more.
void test_riao1_c_finer_grain_takes_the_remaining_deadline() {
  const IdleOwnerChildBound& bound = idleOwnerChildBound(IdleOwnerChild::OverdubSourceView);
  TEST_ASSERT_FALSE(bound.unbounded);
  TEST_ASSERT_TRUE(bound.fineGrainUs != 0);
  TEST_ASSERT_TRUE(bound.fineGrainUs < bound.maxUs);

  // Between the fine grain and the declared maximum: admitted for exactly the remainder.
  const uint32_t remaining = static_cast<uint32_t>(bound.fineGrainUs) + 500u;
  const IdleOwnerAdmission admission =
      admitIdleOwnerChild(IdleOwnerChild::OverdubSourceView, true, true, remaining);
  TEST_ASSERT_EQUAL(IdleOwnerChildDecision::Run, admission.decision);
  TEST_ASSERT_EQUAL_UINT32(remaining, admission.grantedUs);

  // Below the fine grain: deferred, not admitted into an overshoot.
  TEST_ASSERT_EQUAL(IdleOwnerChildDecision::Defer,
                    decisionFor(IdleOwnerChild::OverdubSourceView, true, true,
                                static_cast<uint32_t>(bound.fineGrainUs) - 1u));
}

// RIAO-1.D — the aggregate turn is bounded, and the bound holds for every arrival order.
void test_riao1_d_aggregate_turn_is_bounded() {
  // Registry maxima against the 4 ms budget: the overdub source view (4 ms) is an exact fit; a 3 ms
  // canonical-prefix grain after it does not fit (4 + 3 > 4). One grain per turn is the discipline.
  IdleOwnerTurnBudget turn;
  TEST_ASSERT_EQUAL(IdleOwnerChildDecision::Run,
                    turn.admit(IdleOwnerChild::OverdubSourceView, true, true).decision);
  TEST_ASSERT_EQUAL_UINT32(0, turn.remainingUs);  // exact fit is admitted, not rounded away
  const IdleOwnerAdmission second = turn.admit(IdleOwnerChild::CanonicalPrefixGrain, true, true);
  TEST_ASSERT_EQUAL(IdleOwnerChildDecision::Defer, second.decision);
  TEST_ASSERT_EQUAL_UINT8(1, turn.admitted);

  // A finer-grain child can still take the remainder rather than being refused outright.
  IdleOwnerTurnBudget fineTurn;
  TEST_ASSERT_EQUAL(IdleOwnerChildDecision::Run,
                    fineTurn.admit(IdleOwnerChild::PlaybackWindowGrain, true, true).decision);  // 2 ms
  const uint32_t remainingBefore = fineTurn.remainingUs;
  const IdleOwnerAdmission partial = fineTurn.admit(IdleOwnerChild::OverdubSourceView, true, true);
  TEST_ASSERT_EQUAL(IdleOwnerChildDecision::Run, partial.decision);
  TEST_ASSERT_EQUAL_UINT32(remainingBefore, partial.grantedUs);  // granted exactly the remainder
  TEST_ASSERT_EQUAL_UINT32(0, fineTurn.remainingUs);

  // Every permutation stays within the plan: admitted declarations never sum past the owner budget,
  // whichever order they arrive in. (The admitted count legitimately varies with order.)
  const IdleOwnerChild children[] = {IdleOwnerChild::OverdubSourceView, IdleOwnerChild::PlaybackWindowGrain,
                                     IdleOwnerChild::CanonicalPrefixGrain};
  for (uint8_t a = 0; a < 3; ++a) {
    for (uint8_t b = 0; b < 3; ++b) {
      for (uint8_t c = 0; c < 3; ++c) {
        if (a == b || b == c || a == c) continue;
        IdleOwnerTurnBudget perTurn;
        uint32_t charged = 0;
        for (IdleOwnerChild child : {children[a], children[b], children[c]}) {
          const IdleOwnerAdmission admission = perTurn.admit(child, true, true);
          if (admission.decision == IdleOwnerChildDecision::Run) {
            const uint32_t maximum = idleOwnerChildBound(child).maxUs;
            charged += admission.grantedUs < maximum ? admission.grantedUs : maximum;
          }
        }
        TEST_ASSERT_TRUE(charged <= kOwnerWorkBudgetUs);
      }
    }
  }
}

// The budget contract is against the **fastest supported** MITI clock interval, not a measured 120 BPM
// sample: `budget + worst-case residual <= minimum supported clock interval`, where the enforced tempo
// range caps at 300 BPM and MIDI clock is 24 PPQN (60e6 / (300 * 24) = 8333 us). Both earlier drafts are
// rejected here: 25 ms exceeded even the 120 BPM interval, and 8 ms would leave ~333 us of residual at
// 300 BPM, which no measurement would defend.
void test_riao1_budget_fits_inside_the_fastest_supported_clock_interval() {
  TEST_ASSERT_TRUE(kOwnerWorkBudgetUs < kIdleMaintBreachThresholdUs);
  TEST_ASSERT_EQUAL_UINT32(50000, kIdleMaintBreachThresholdUs);
  TEST_ASSERT_EQUAL_UINT32(8333, kMinimumSupportedClockIntervalUs);
  TEST_ASSERT_TRUE_MESSAGE(kOwnerWorkBudgetUs + kOwnerWorkResidualUs <= kMinimumSupportedClockIntervalUs,
                           "budget + worst-case residual must fit one clock edge at 300 BPM");
  TEST_ASSERT_TRUE_MESSAGE(kOwnerWorkBudgetUs * 2u <= kMinimumSupportedClockIntervalUs,
                           "the child budget must leave the turn real headroom at the fastest tempo");
}

// The registry is total: every child has a name and a coherent declaration, so a new enum value without
// a row is a compile/test failure rather than an undeclared child in the field.
void test_riao1_e_registry_is_total_and_coherent() {
  for (uint8_t i = 0; i < kIdleOwnerChildCount; ++i) {
    const IdleOwnerChildBound& bound = idleOwnerChildBound(static_cast<IdleOwnerChild>(i));
    TEST_ASSERT_NOT_NULL(bound.name);
    TEST_ASSERT_TRUE(bound.name[0] != '\0');
    if (bound.unbounded) {
      TEST_ASSERT_EQUAL_UINT16(0, bound.maxUs);
      TEST_ASSERT_EQUAL_UINT16(0, bound.fineGrainUs);
    } else {
      TEST_ASSERT_TRUE(bound.maxUs > 0);
      TEST_ASSERT_TRUE(bound.fineGrainUs <= bound.maxUs);
      TEST_ASSERT_TRUE(static_cast<uint32_t>(bound.maxUs) <= kOwnerWorkBudgetUs);
    }
  }
}

// RIAO-1.E — deferral is lossless: a deferred child leaves the plan untouched and is admitted again on a
// non-timing-critical turn with the same declaration.
void test_riao1_e_deferral_is_lossless_and_resumable() {
  IdleOwnerTurnBudget turn;
  // Spend the plan down so the source-view child cannot be admitted whole.
  (void)turn.admit(IdleOwnerChild::VisualCacheSlice, true, true);
  (void)turn.admit(IdleOwnerChild::SlotProvision, true, true);
  (void)turn.admit(IdleOwnerChild::CanonicalPrefixGrain, true, true);
  const uint32_t remainingBefore = turn.remainingUs;
  const uint8_t admittedBefore = turn.admitted;
  const IdleOwnerAdmission deferred = turn.admit(IdleOwnerChild::WrapAppend, true, true);
  TEST_ASSERT_EQUAL(IdleOwnerChildDecision::Defer, deferred.decision);
  // A refused admission does not spend budget and does not count as admitted.
  TEST_ASSERT_EQUAL_UINT32(remainingBefore, turn.remainingUs);
  TEST_ASSERT_EQUAL_UINT8(admittedBefore, turn.admitted);

  // The same child is owed again later and is admitted unchanged.
  const IdleOwnerAdmission resumed =
      admitIdleOwnerChild(IdleOwnerChild::WrapAppend, true, false, 0);
  TEST_ASSERT_EQUAL(IdleOwnerChildDecision::Run, resumed.decision);
}

// RIAO-1.F — coverage currency: a deferral of the source-view refresh is only legitimate while the
// established view still covers the interval the overlap path will evaluate.
void test_riao1_f_source_view_deferral_requires_coverage() {
  constexpr uint32_t kLoop = 23808;   // the capture's 31-bar loop
  constexpr uint32_t kWindow = 12288; // 16 bars
  // Whole-loop authority covers everything.
  TEST_ASSERT_TRUE(idleOwnerSourceViewCovers(0, kLoop, 9000, kWindow, kLoop));
  // Inside: the deferral is safe (covered [7680, 19968)).
  TEST_ASSERT_TRUE(idleOwnerSourceViewCovers(7680, kWindow, 8000, 4096, kLoop));
  // The request outgrows the established coverage: the deferral is NOT safe.
  TEST_ASSERT_FALSE(idleOwnerSourceViewCovers(7680, kWindow, 9000, kWindow, kLoop));
  TEST_ASSERT_FALSE(idleOwnerSourceViewCovers(7680, kWindow, 15000, kWindow, kLoop));
  TEST_ASSERT_FALSE(idleOwnerSourceViewCovers(0, 768, 4800, kWindow, kLoop));
  // Wrap: a view straddling the loop end (covered [20000, 23808) + [0, 3872)) covers a request that fits
  // inside it, and not one that does not.
  TEST_ASSERT_TRUE(idleOwnerSourceViewCovers(20000, 7680, 3000, 800, kLoop));
  TEST_ASSERT_FALSE(idleOwnerSourceViewCovers(20000, 7680, 3000, 1536, kLoop));
  TEST_ASSERT_FALSE(idleOwnerSourceViewCovers(20000, 7680, 8000, 1536, kLoop));
  // Degenerate cases are answered, not undefined.
  TEST_ASSERT_FALSE(idleOwnerSourceViewCovers(0, kWindow, 0, kWindow, 0));
  TEST_ASSERT_TRUE(idleOwnerSourceViewCovers(0, 0, 0, 0, kLoop));
  TEST_ASSERT_FALSE(idleOwnerSourceViewCovers(0, 0, 0, kWindow, kLoop));
}

// DEC-216 — the **aggregate** realtime walk. `kOwnerWorkBudgetUs` bounds one child, never the sum of the
// tracks one deferred turn visits: on device one turn spent 283,622 us across that walk
// (`session_20261030_114009.log`) while playback notes went 294-326 ms late. The walk asks this predicate
// before starting another track unit, so a turn yields once its budget is spent.
void test_dec216_realtime_walk_starts_no_unit_after_the_turn_budget() {
  // Fresh turn: another unit may start.
  TEST_ASSERT_TRUE(idleOwnerTurnMayStartAnotherUnit(0));
  TEST_ASSERT_TRUE(idleOwnerTurnMayStartAnotherUnit(kOwnerWorkBudgetUs - 1));
  // Budget spent: the walk stops starting units (the current unit is never interrupted).
  TEST_ASSERT_FALSE(idleOwnerTurnMayStartAnotherUnit(kOwnerWorkBudgetUs));
  TEST_ASSERT_FALSE(idleOwnerTurnMayStartAnotherUnit(kOwnerWorkBudgetUs + 1));
  // The measured device turn is far past the bound, and stays a stop.
  TEST_ASSERT_FALSE(idleOwnerTurnMayStartAnotherUnit(283622));
}

// DEC-216 control — the aggregate ceiling is the *declared* budget and is strictly tighter than both the
// 50 ms alarm and the shortest supported clock interval, so a complying turn always leaves a MIDI edge.
void test_dec216_turn_budget_is_tighter_than_the_alarm_and_one_clock_interval() {
  TEST_ASSERT_TRUE(kOwnerWorkBudgetUs < kIdleMaintBreachThresholdUs);
  TEST_ASSERT_TRUE(kOwnerWorkBudgetUs + kOwnerWorkResidualUs <= kMinimumSupportedClockIntervalUs);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_riao1_a_unbounded_child_defers_while_timing_critical);
  RUN_TEST(test_riao1_a_control_unbounded_child_runs_when_not_timing_critical);
  RUN_TEST(test_riao7_1_bounded_verification_admits_the_declared_grain);
  RUN_TEST(test_dec237_display_base_grain_is_declared);
  RUN_TEST(test_c2b_revision_edit_index_grain_is_declared);
  RUN_TEST(test_c3b_revision_shadow_import_grain_is_declared);
  RUN_TEST(test_c3d_revision_shadow_window_grain_is_declared);
  RUN_TEST(test_riao1_not_owed_costs_nothing);
  RUN_TEST(test_riao1_b_child_that_cannot_fit_is_not_dispatched);
  RUN_TEST(test_riao1_c_finer_grain_takes_the_remaining_deadline);
  RUN_TEST(test_riao1_d_aggregate_turn_is_bounded);
  RUN_TEST(test_riao1_budget_fits_inside_the_fastest_supported_clock_interval);
  RUN_TEST(test_riao1_e_registry_is_total_and_coherent);
  RUN_TEST(test_riao1_e_deferral_is_lossless_and_resumable);
  RUN_TEST(test_riao1_f_source_view_deferral_requires_coverage);
  RUN_TEST(test_dec216_realtime_walk_starts_no_unit_after_the_turn_budget);
  RUN_TEST(test_dec216_turn_budget_is_tighter_than_the_alarm_and_one_clock_interval);
  return UNITY_END();
}
