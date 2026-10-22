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
#include "Utils/PlaybackDemandClaims.h"

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

// (9) **Per-class identity rules.** Currency is compared within one class only, and an absent currency is never current:
// "no root yet", "no session", "no capture" are ineligible rather than accidentally matching a zero bound value.
void test_s2b_each_class_decides_its_own_identity_and_absent_is_never_current() {
  using namespace PlaybackDemandClaims;
  // Committed: revision *and* generation must match the bound pair, and the live pair must be present.
  TEST_ASSERT_TRUE(committedIdentityCurrent(/*live*/5, 9, /*bound*/5, 9));
  TEST_ASSERT_FALSE(committedIdentityCurrent(6, 9, 5, 9));
  TEST_ASSERT_FALSE(committedIdentityCurrent(5, 10, 5, 9));
  TEST_ASSERT_FALSE_MESSAGE(committedIdentityCurrent(0, 9, 0, 9), "no published root is not a current identity");
  // Preview: the session revision must exist and match.
  TEST_ASSERT_TRUE(previewIdentityCurrent(4, 4));
  TEST_ASSERT_FALSE(previewIdentityCurrent(5, 4));
  TEST_ASSERT_FALSE_MESSAGE(previewIdentityCurrent(0, 0), "no session is not a current preview");
  // Overlay: the live overdub capture generation must exist and match (a pass commit advances it).
  TEST_ASSERT_TRUE(overlayIdentityCurrent(12, 12));
  TEST_ASSERT_FALSE(overlayIdentityCurrent(13, 12));
  TEST_ASSERT_FALSE_MESSAGE(overlayIdentityCurrent(0, 0), "no live capture is not a current overlay");
}

// (10) The providers feed the contract unchanged: a wrap's pass commit advances the capture generation, so the overlay's
// own claim goes stale and the committed successor wins — the identity rule, not the class order, decides it.
void test_s2b_a_wrap_pass_commit_makes_the_overlay_claim_lose_to_the_committed_successor() {
  using namespace PlaybackDemandClaims;
  PlaybackDemandArbitration::Claim claims[PlaybackDemandArbitration::kClassCount];
  claims[0] = preview(false, 0, 0, false);
  claims[1] = overlay(/*owed=*/true, /*liveCaptureGeneration=*/13, /*boundCaptureGeneration=*/12, /*ready=*/true);
  claims[2] = committed(/*owed=*/true, 5, 9, 5, 9, /*ready=*/false);
  const auto resolution = PlaybackDemandArbitration::resolve(claims);
  TEST_ASSERT_EQUAL(PlaybackDemandArbitration::Class::Committed, resolution.owner);
  TEST_ASSERT_EQUAL(PlaybackDemandArbitration::Outcome::RefusedStaleIdentity,
                    resolution.outcomeFor(PlaybackDemandArbitration::Class::Overlay));
  TEST_ASSERT_TRUE(resolution.retireStaleReady);
}

// (11) The converse: a committed root that advanced past its bound claim is stale and cannot hold the boundary, even
// though the overlay's demand is newer and lower priority.
void test_s2b_an_advanced_committed_identity_is_stale_against_a_valid_overlay() {
  using namespace PlaybackDemandClaims;
  PlaybackDemandArbitration::Claim claims[PlaybackDemandArbitration::kClassCount];
  claims[0] = preview(false, 0, 0, false);
  claims[1] = overlay(true, 12, 12, false);
  claims[2] = committed(/*owed=*/true, /*live*/6, 9, /*bound*/5, 9, /*ready=*/true);
  const auto resolution = PlaybackDemandArbitration::resolve(claims);
  TEST_ASSERT_EQUAL(PlaybackDemandArbitration::Class::Overlay, resolution.owner);
  TEST_ASSERT_EQUAL(PlaybackDemandArbitration::Outcome::RefusedStaleIdentity,
                    resolution.outcomeFor(PlaybackDemandArbitration::Class::Committed));
  TEST_ASSERT_TRUE(resolution.retireStaleReady);
}

// (12) **Three-view pressure.** With one `ready` role and three valid demands, exactly one owner exists, the role's fate
// is decided by name, and the two losers hold no window of their own — the resolution is a decision, not storage.
void test_s2b_three_valid_demands_hold_one_ready_role_and_no_second_window() {
  using namespace PlaybackDemandClaims;
  PlaybackDemandArbitration::Claim claims[PlaybackDemandArbitration::kClassCount];
  claims[0] = preview(true, 4, 4, false);
  claims[1] = overlay(true, 12, 12, false);
  claims[2] = committed(true, 5, 9, 5, 9, /*ready=*/true);
  const auto resolution = PlaybackDemandArbitration::resolve(claims);
  TEST_ASSERT_TRUE(resolution.hasOwner);
  TEST_ASSERT_EQUAL(PlaybackDemandArbitration::Class::Preview, resolution.owner);
  TEST_ASSERT_EQUAL(PlaybackDemandArbitration::Outcome::DeferredToHigherPriority,
                    resolution.outcomeFor(PlaybackDemandArbitration::Class::Overlay));
  TEST_ASSERT_EQUAL(PlaybackDemandArbitration::Outcome::DeferredToHigherPriority,
                    resolution.outcomeFor(PlaybackDemandArbitration::Class::Committed));
  TEST_ASSERT_TRUE_MESSAGE(resolution.supersedeReady, "the committed candidate's role is retired, not duplicated");
  TEST_ASSERT_FALSE(resolution.retireStaleReady);
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(owners(resolution)));
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
  RUN_TEST(test_s2b_each_class_decides_its_own_identity_and_absent_is_never_current);
  RUN_TEST(test_s2b_a_wrap_pass_commit_makes_the_overlay_claim_lose_to_the_committed_successor);
  RUN_TEST(test_s2b_an_advanced_committed_identity_is_stale_against_a_valid_overlay);
  RUN_TEST(test_s2b_three_valid_demands_hold_one_ready_role_and_no_second_window);
  return UNITY_END();
}
