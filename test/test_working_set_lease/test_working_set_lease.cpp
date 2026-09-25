//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Host test for the working-set lease vocabulary (runtime-working-set-ownership S3.1).
//
// The lease identity is slot + generation + role, roles are composable, and the pool holds
// declared caps without allocating. These cases fix the contract the S4 release path and the
// S4.2 stale-owner rejection depend on, with no device or runtimeBySlot_ dependency.

#include <unity.h>

#include "Utils/WorkingSetLease.h"

namespace {

WorkingSetLease leaseFor(uint8_t slot, uint16_t generation, WorkingSetRole role) {
	WorkingSetLease lease;
	lease.slot = slot;
	lease.generation = generation;
	lease.role = role;
	return lease;
}

}  // namespace

void test_roles_are_composable_on_one_slot() {
	const WorkingSetRole both = WorkingSetRole::PlaybackWindow | WorkingSetRole::LiveCapture;
	TEST_ASSERT_TRUE(hasRole(both, WorkingSetRole::PlaybackWindow));
	TEST_ASSERT_TRUE(hasRole(both, WorkingSetRole::LiveCapture));
	TEST_ASSERT_FALSE(hasRole(both, WorkingSetRole::Interactive));

	WorkingSetLeasePool pool;
	TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WorkingSetOutcome::Acquired),
	                        static_cast<uint8_t>(pool.acquire(leaseFor(3, 1, both))));
	TEST_ASSERT_TRUE(pool.isHeld(leaseFor(3, 1, WorkingSetRole::PlaybackWindow)));
	TEST_ASSERT_TRUE(pool.isHeld(leaseFor(3, 1, WorkingSetRole::LiveCapture)));
	TEST_ASSERT_FALSE(pool.isHeld(leaseFor(3, 1, WorkingSetRole::Interactive)));
}

void test_identity_is_slot_and_generation_not_role() {
	TEST_ASSERT_TRUE(sameWorkingSetIdentity(leaseFor(2, 7, WorkingSetRole::PlaybackWindow),
	                                        leaseFor(2, 7, WorkingSetRole::Interactive)));
	TEST_ASSERT_FALSE(sameWorkingSetIdentity(leaseFor(2, 7, WorkingSetRole::PlaybackWindow),
	                                         leaseFor(2, 8, WorkingSetRole::PlaybackWindow)));
	TEST_ASSERT_FALSE(sameWorkingSetIdentity(leaseFor(2, 7, WorkingSetRole::PlaybackWindow),
	                                         leaseFor(3, 7, WorkingSetRole::PlaybackWindow)));
}

void test_a_lease_without_a_role_is_invalid() {
	WorkingSetLeasePool pool;
	TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WorkingSetOutcome::InvalidRole),
	                        static_cast<uint8_t>(pool.acquire(leaseFor(1, 1, WorkingSetRole::None))));
	TEST_ASSERT_EQUAL_UINT8(0, pool.totalHolders());
}

void test_generation_mismatched_release_is_a_reported_no_op() {
	WorkingSetLeasePool pool;
	TEST_ASSERT_EQUAL_UINT8(
	    static_cast<uint8_t>(WorkingSetOutcome::Acquired),
	    static_cast<uint8_t>(pool.acquire(leaseFor(5, 17, WorkingSetRole::PlaybackWindow))));

	// A late cleanup from generation 17 reaches a slot now owned by generation 18.
	TEST_ASSERT_EQUAL_UINT8(
	    static_cast<uint8_t>(WorkingSetOutcome::StaleGeneration),
	    static_cast<uint8_t>(pool.release(leaseFor(5, 18, WorkingSetRole::PlaybackWindow))));
	TEST_ASSERT_TRUE(pool.isHeld(leaseFor(5, 17, WorkingSetRole::PlaybackWindow)));

	TEST_ASSERT_EQUAL_UINT8(
	    static_cast<uint8_t>(WorkingSetOutcome::Released),
	    static_cast<uint8_t>(pool.release(leaseFor(5, 17, WorkingSetRole::PlaybackWindow))));
	TEST_ASSERT_FALSE(pool.isHeld(leaseFor(5, 17, WorkingSetRole::PlaybackWindow)));
}

void test_release_of_an_unheld_identity_is_reported() {
	WorkingSetLeasePool pool;
	TEST_ASSERT_EQUAL_UINT8(
	    static_cast<uint8_t>(WorkingSetOutcome::NotHeld),
	    static_cast<uint8_t>(pool.release(leaseFor(0, 1, WorkingSetRole::PlaybackWindow))));

	pool.acquire(leaseFor(0, 1, WorkingSetRole::PlaybackWindow));
	TEST_ASSERT_EQUAL_UINT8(
	    static_cast<uint8_t>(WorkingSetOutcome::NotHeld),
	    static_cast<uint8_t>(pool.release(leaseFor(0, 1, WorkingSetRole::Interactive))));
	TEST_ASSERT_TRUE(pool.isHeld(leaseFor(0, 1, WorkingSetRole::PlaybackWindow)));
}

void test_total_release_retires_every_role_at_the_identity() {
	WorkingSetLeasePool pool;
	pool.acquire(leaseFor(4, 2, WorkingSetRole::PlaybackWindow | WorkingSetRole::LiveCapture));
	pool.acquire(leaseFor(4, 2, WorkingSetRole::Interactive));
	TEST_ASSERT_EQUAL_UINT8(2, pool.totalHolders());
	TEST_ASSERT_EQUAL_UINT8(1, pool.holderCount(WorkingSetRole::Interactive));

	TEST_ASSERT_EQUAL_UINT8(2, pool.releaseAllRoles(leaseFor(4, 2, WorkingSetRole::None)));
	TEST_ASSERT_EQUAL_UINT8(0, pool.totalHolders());
	TEST_ASSERT_EQUAL_UINT8(0, pool.holderCount(WorkingSetRole::PlaybackWindow));
	TEST_ASSERT_EQUAL_UINT8(0, pool.holderCount(WorkingSetRole::LiveCapture));
	TEST_ASSERT_EQUAL_UINT8(0, pool.holderCount(WorkingSetRole::Interactive));
}

void test_total_release_leaves_another_generation_alone() {
	WorkingSetLeasePool pool;
	pool.acquire(leaseFor(4, 2, WorkingSetRole::PlaybackWindow));
	pool.acquire(leaseFor(4, 3, WorkingSetRole::PlaybackWindow));
	TEST_ASSERT_EQUAL_UINT8(1, pool.releaseAllRoles(leaseFor(4, 2, WorkingSetRole::None)));
	TEST_ASSERT_TRUE(pool.isHeld(leaseFor(4, 3, WorkingSetRole::PlaybackWindow)));
	TEST_ASSERT_FALSE(pool.isHeld(leaseFor(4, 2, WorkingSetRole::PlaybackWindow)));
}

void test_cap_breach_is_reported_without_growing() {
	WorkingSetLeaseCaps caps;
	caps.playbackWindow = 2;
	WorkingSetLeasePool pool(caps);

	TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WorkingSetOutcome::Acquired),
	                        static_cast<uint8_t>(pool.acquire(leaseFor(0, 1, WorkingSetRole::PlaybackWindow))));
	TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WorkingSetOutcome::Acquired),
	                        static_cast<uint8_t>(pool.acquire(leaseFor(1, 1, WorkingSetRole::PlaybackWindow))));
	TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WorkingSetOutcome::CapReached),
	                        static_cast<uint8_t>(pool.acquire(leaseFor(2, 1, WorkingSetRole::PlaybackWindow))));
	TEST_ASSERT_EQUAL_UINT8(2, pool.totalHolders());
	TEST_ASSERT_FALSE(pool.isHeld(leaseFor(2, 1, WorkingSetRole::PlaybackWindow)));
}

void test_a_composite_lease_is_refused_when_any_named_class_is_full() {
	WorkingSetLeaseCaps caps;
	caps.playbackWindow = 8;
	caps.liveCapturePerTrack = 1;
	WorkingSetLeasePool pool(caps);

	pool.acquire(leaseFor(0, 1, WorkingSetRole::LiveCapture));
	// Slot 1 has playback room but the track's single capture lease is taken.
	TEST_ASSERT_EQUAL_UINT8(
	    static_cast<uint8_t>(WorkingSetOutcome::CapReached),
	    static_cast<uint8_t>(pool.acquire(leaseFor(1, 1, WorkingSetRole::PlaybackWindow |
	                                                        WorkingSetRole::LiveCapture))));
	TEST_ASSERT_FALSE(pool.isHeld(leaseFor(1, 1, WorkingSetRole::PlaybackWindow)));
}

void test_per_class_census_tracks_holders() {
	WorkingSetLeasePool pool;
	pool.acquire(leaseFor(0, 1, WorkingSetRole::PlaybackWindow));
	pool.acquire(leaseFor(1, 1, WorkingSetRole::PlaybackWindow));
	pool.acquire(leaseFor(2, 1, WorkingSetRole::LiveCapture));
	pool.acquire(leaseFor(3, 1, WorkingSetRole::DeferredFinalization));

	TEST_ASSERT_EQUAL_UINT8(2, pool.holderCount(WorkingSetRole::PlaybackWindow));
	TEST_ASSERT_EQUAL_UINT8(1, pool.holderCount(WorkingSetRole::LiveCapture));
	TEST_ASSERT_EQUAL_UINT8(1, pool.holderCount(WorkingSetRole::DeferredFinalization));
	TEST_ASSERT_EQUAL_UINT8(0, pool.holderCount(WorkingSetRole::Interactive));
	TEST_ASSERT_EQUAL_UINT8(4, pool.totalHolders());
	TEST_ASSERT_EQUAL_UINT8(
	    static_cast<uint8_t>(WorkingSetRole::PlaybackWindow | WorkingSetRole::LiveCapture),
	    static_cast<uint8_t>(pool.rolesHeldAtSlot(0) | pool.rolesHeldAtSlot(2)));
}

void test_default_caps_match_the_measured_envelope() {
	WorkingSetLeasePool pool;
	TEST_ASSERT_EQUAL_UINT8(8, pool.caps().playbackWindow);       // 8 tracks x 1 slot today
	TEST_ASSERT_EQUAL_UINT8(1, pool.caps().liveCapturePerTrack);  // handoff §2
	TEST_ASSERT_EQUAL_UINT8(1, pool.caps().deferredFinalization); // DEC-078
	TEST_ASSERT_EQUAL_UINT8(1, pool.caps().interactive);
}

int main(int /*argc*/, char** /*argv*/) {
	UNITY_BEGIN();
	RUN_TEST(test_roles_are_composable_on_one_slot);
	RUN_TEST(test_identity_is_slot_and_generation_not_role);
	RUN_TEST(test_a_lease_without_a_role_is_invalid);
	RUN_TEST(test_generation_mismatched_release_is_a_reported_no_op);
	RUN_TEST(test_release_of_an_unheld_identity_is_reported);
	RUN_TEST(test_total_release_retires_every_role_at_the_identity);
	RUN_TEST(test_total_release_leaves_another_generation_alone);
	RUN_TEST(test_cap_breach_is_reported_without_growing);
	RUN_TEST(test_a_composite_lease_is_refused_when_any_named_class_is_full);
	RUN_TEST(test_per_class_census_tracks_holders);
	RUN_TEST(test_default_caps_match_the_measured_envelope);
	return UNITY_END();
}
