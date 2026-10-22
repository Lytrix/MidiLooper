// Task 3.15e slice 2 — **owner-level arbitration for the one `ready` role.**
//
// Three owners can owe a window on one slot: the NOTE_EDIT preview, the live overdub overlay, and the committed successor.
// `RuntimePlaybackViewConfig::kViewCount == 3` gives exactly one `ready` role, so this resolver decides *who owns the next
// adoption opportunity* and names every other outcome. It is pure (no window, no queue, no runtime field), which is what
// makes "no hidden fourth state" testable: a losing owner keeps its own existing obligation and re-asks on its own turn.
//
// The audit's four claims, one case each:
//  * priority is deterministic and contractual (Preview > Overlay > Committed), never "who finished first";
//  * a losing complete candidate has a named outcome (superseded by the winner, retired as stale, or deferred to its owner);
//  * identity beats role labels (a stale overlay/preview loses however high its class);
//  * only one class ever owns the boundary, and an owner that cannot get a role is refused by name rather than silently
//    handing the slot to the compatibility echo.
#include <unity.h>

#include "Utils/PlaybackDemandArbitration.h"

using namespace PlaybackDemandArbitration;

namespace {

Claim preview(bool owed, bool identityCurrent = true, bool ready = false, uint64_t token = 7) {
  return Claim{Class::Preview, owed, identityCurrent, ready, token};
}
Claim overlay(bool owed, bool identityCurrent = true, bool ready = false, uint64_t captureGeneration = 9) {
  return Claim{Class::Overlay, owed, identityCurrent, ready, captureGeneration};
}
Claim committed(bool owed, bool identityCurrent = true, bool ready = false, uint64_t revision = 3) {
  return Claim{Class::Committed, owed, identityCurrent, ready, revision};
}
Claim arrayOf(const Claim& previewClaim, const Claim& overlayClaim, const Claim& committedClaim, Claim (&out)[kClassCount]) {
  out[0] = previewClaim;
  out[1] = overlayClaim;
  out[2] = committedClaim;
  return out[0];
}
size_t owners(const Resolution& resolution) {
  size_t count = 0;
  for (size_t i = 0; i < kClassCount; ++i) {
    if (resolution.outcomes[i] == Outcome::OwnsNextOpportunity) ++count;
  }
  return count;
}

}  // namespace

// (1) A committed complete `ready` candidate, then the same slot's live overlay becomes owed: the overlay owns the next
// boundary, the committed candidate is superseded **by name**, and the committed demand is deferred rather than dropped.
void test_s2_committed_ready_then_live_overlay_owes_a_deterministic_winner() {
  Claim claims[kClassCount];
  arrayOf(preview(false), overlay(true), committed(true, true, true), claims);
  const Resolution r = resolve(claims);
  TEST_ASSERT_TRUE(r.hasOwner);
  TEST_ASSERT_EQUAL(Class::Overlay, r.owner);
  TEST_ASSERT_EQUAL(Outcome::OwnsNextOpportunity, r.outcomeFor(Class::Overlay));
  TEST_ASSERT_EQUAL(Outcome::DeferredToHigherPriority, r.outcomeFor(Class::Committed));
  TEST_ASSERT_EQUAL(Outcome::NotClaimed, r.outcomeFor(Class::Preview));
  TEST_ASSERT_TRUE_MESSAGE(r.supersedeReady, "the committed candidate's role is retired by the arbitration, not by luck");
  TEST_ASSERT_FALSE(r.retireStaleReady);
  TEST_ASSERT_FALSE(r.compatibilityAuthoritative);
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(owners(r)));
}

// (2) A live overlay holds `ready`, then NOTE_EDIT starts: the preview owns the boundary and the overlay's candidate is
// superseded. The overlay's demand stays owed, so it resumes when the session ends (case 3).
void test_s2_overlay_ready_then_note_edit_starts_preview_wins() {
  Claim claims[kClassCount];
  arrayOf(preview(true), overlay(true, true, true), committed(false), claims);
  const Resolution r = resolve(claims);
  TEST_ASSERT_EQUAL(Class::Preview, r.owner);
  TEST_ASSERT_EQUAL(Outcome::DeferredToHigherPriority, r.outcomeFor(Class::Overlay));
  TEST_ASSERT_TRUE(r.supersedeReady);
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(owners(r)));
}

// (3) The session exits (no preview demand) while the overdub is still live: arbitration returns cleanly to the overlay,
// with no supersede (the preview lane retired its own ready on exit) and no leftover ownership.
void test_s2_note_edit_exit_returns_the_boundary_to_the_live_overlay() {
  Claim claims[kClassCount];
  arrayOf(preview(false), overlay(true), committed(true), claims);
  const Resolution r = resolve(claims);
  TEST_ASSERT_EQUAL(Class::Overlay, r.owner);
  TEST_ASSERT_FALSE(r.supersedeReady);
  TEST_ASSERT_FALSE(r.retireStaleReady);
  TEST_ASSERT_EQUAL(Outcome::DeferredToHigherPriority, r.outcomeFor(Class::Committed));
  TEST_ASSERT_EQUAL(Outcome::NotClaimed, r.outcomeFor(Class::Preview));
}

// (4) Identity beats role labels: an overlay whose capture generation no longer matches the active overdub loses **even
// though its class outranks the committed successor**, and its complete `ready` candidate is retired rather than adopted.
void test_s2_a_stale_live_overlay_generation_loses_regardless_of_class() {
  Claim claims[kClassCount];
  arrayOf(preview(false), overlay(true, /*identityCurrent=*/false, /*ready=*/true), committed(true), claims);
  const Resolution r = resolve(claims);
  TEST_ASSERT_EQUAL(Class::Committed, r.owner);
  TEST_ASSERT_EQUAL(Outcome::RefusedStaleIdentity, r.outcomeFor(Class::Overlay));
  TEST_ASSERT_TRUE_MESSAGE(r.retireStaleReady, "a stale candidate must be retired, never adopted");
  TEST_ASSERT_FALSE(r.supersedeReady);
  TEST_ASSERT_FALSE(r.compatibilityAuthoritative);
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(owners(r)));
}

// (5) The same rule for the preview: a stale session revision cannot own a boundary, and the valid overlay takes it.
void test_s2_a_stale_preview_revision_loses_immediately() {
  Claim claims[kClassCount];
  arrayOf(preview(true, /*identityCurrent=*/false), overlay(true), committed(false), claims);
  const Resolution r = resolve(claims);
  TEST_ASSERT_EQUAL(Class::Overlay, r.owner);
  TEST_ASSERT_EQUAL(Outcome::RefusedStaleIdentity, r.outcomeFor(Class::Preview));
  TEST_ASSERT_FALSE(r.retireStaleReady);
}

// (6) Two complete candidates can never both claim the boundary: the resolution reports one owner and a named fate for
// the other, and repeating it on the same claims is identical (the rule is contractual, not timing-dependent).
void test_s2_two_complete_candidates_cannot_both_own_the_boundary() {
  Claim claims[kClassCount];
  arrayOf(preview(true, true, /*ready=*/true), overlay(false), committed(true, true, /*ready=*/true), claims);
  const Resolution first = resolve(claims);
  TEST_ASSERT_TRUE(first.hasOwner);
  TEST_ASSERT_EQUAL(Class::Preview, first.owner);
  TEST_ASSERT_EQUAL(Outcome::RefusedRoleConflict, first.outcomeFor(Class::Committed));
  TEST_ASSERT_TRUE_MESSAGE(first.roleConflict, "two owners cannot hold one ready role; the conflict is named");
  TEST_ASSERT_TRUE(first.supersedeReady);
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(owners(first)));
  const Resolution again = resolve(claims);
  TEST_ASSERT_EQUAL(first.owner, again.owner);
  TEST_ASSERT_EQUAL(first.supersedeReady, again.supersedeReady);
  TEST_ASSERT_EQUAL(first.roleConflict, again.roleConflict);
  for (size_t i = 0; i < kClassCount; ++i) TEST_ASSERT_EQUAL(first.outcomes[i], again.outcomes[i]);
}

// (7) Role exhaustion is a **named** outcome: with no free role the owner is refused by name and the caller is told the
// compatibility route carries the slot — never silence, never a silent echo pass.
void test_s2_role_exhaustion_is_refused_by_name_not_a_silent_echo_fallback() {
  Claim claims[kClassCount];
  arrayOf(preview(false), overlay(true), committed(true, true, /*ready=*/true), claims);
  const Resolution r = refuseForRoleCapacity(claims);
  TEST_ASSERT_FALSE(r.hasOwner);
  TEST_ASSERT_EQUAL(Outcome::RefusedNoRoleCapacity, r.outcomeFor(Class::Overlay));
  TEST_ASSERT_EQUAL(Outcome::DeferredToHigherPriority, r.outcomeFor(Class::Committed));
  TEST_ASSERT_TRUE_MESSAGE(r.compatibilityAuthoritative,
                           "the caller must report that the compatibility route is authoritative");
  TEST_ASSERT_FALSE(r.supersedeReady);
  TEST_ASSERT_FALSE(r.retireStaleReady);
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(owners(r)));
}

// (8) All three owed: one contractual winner with two named deferrals. Claim order does not matter (the rule reads the
// class, not the array position), and nothing is owed by a class that reports `NotClaimed`.
void test_s2_all_three_owed_names_every_outcome_and_ignores_claim_order() {
  Claim claims[kClassCount];
  arrayOf(preview(true), overlay(true), committed(true), claims);
  const Resolution r = resolve(claims);
  TEST_ASSERT_EQUAL(Class::Preview, r.owner);
  TEST_ASSERT_EQUAL(Outcome::OwnsNextOpportunity, r.outcomeFor(Class::Preview));
  TEST_ASSERT_EQUAL(Outcome::DeferredToHigherPriority, r.outcomeFor(Class::Overlay));
  TEST_ASSERT_EQUAL(Outcome::DeferredToHigherPriority, r.outcomeFor(Class::Committed));
  TEST_ASSERT_FALSE(r.supersedeReady);
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(owners(r)));

  // The same facts in a different array position resolve identically.
  Claim reordered[kClassCount];
  reordered[0] = committed(true);
  reordered[1] = preview(true);
  reordered[2] = overlay(true);
  const Resolution permuted = resolve(reordered);
  TEST_ASSERT_EQUAL(Class::Preview, permuted.owner);
  TEST_ASSERT_EQUAL(Outcome::DeferredToHigherPriority, permuted.outcomeFor(Class::Overlay));
  TEST_ASSERT_EQUAL(Outcome::DeferredToHigherPriority, permuted.outcomeFor(Class::Committed));

  // Nothing owed at all is not "an owner with nothing to do".
  Claim idle[kClassCount];
  arrayOf(preview(false), overlay(false), committed(false), idle);
  const Resolution none = resolve(idle);
  TEST_ASSERT_FALSE(none.hasOwner);
  TEST_ASSERT_FALSE(none.compatibilityAuthoritative);
  for (size_t i = 0; i < kClassCount; ++i) TEST_ASSERT_EQUAL(Outcome::NotClaimed, none.outcomes[i]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_s2_committed_ready_then_live_overlay_owes_a_deterministic_winner);
  RUN_TEST(test_s2_overlay_ready_then_note_edit_starts_preview_wins);
  RUN_TEST(test_s2_note_edit_exit_returns_the_boundary_to_the_live_overlay);
  RUN_TEST(test_s2_a_stale_live_overlay_generation_loses_regardless_of_class);
  RUN_TEST(test_s2_a_stale_preview_revision_loses_immediately);
  RUN_TEST(test_s2_two_complete_candidates_cannot_both_own_the_boundary);
  RUN_TEST(test_s2_role_exhaustion_is_refused_by_name_not_a_silent_echo_fallback);
  RUN_TEST(test_s2_all_three_owed_names_every_outcome_and_ignores_claim_order);
  return UNITY_END();
}
