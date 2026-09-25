//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Host test for the working-set lease vocabulary (runtime-working-set-ownership S3.1).
//
// The lease identity is slot + generation + role and roles are composable. `WorkingSetLeaseSlot`
// is what a `LoopPlaybackRuntime` carries (one lease per slot); `WorkingSetLeaseTable` is the
// multi-slot census the S4 release path needs. Both hold counts only and allocate nothing.

#include <unity.h>

#include "Utils/WorkingSetLease.h"

namespace {

constexpr WorkingSetRole kBoth =
    WorkingSetRole::PlaybackWindow | WorkingSetRole::LiveCapture;

}  // namespace

void test_roles_are_composable() {
	TEST_ASSERT_TRUE(hasRole(kBoth, WorkingSetRole::PlaybackWindow));
	TEST_ASSERT_TRUE(hasRole(kBoth, WorkingSetRole::LiveCapture));
	TEST_ASSERT_FALSE(hasRole(kBoth, WorkingSetRole::Interactive));
	TEST_ASSERT_TRUE(isDeclaredRoleSet(kBoth));
	TEST_ASSERT_FALSE(isDeclaredRoleSet(WorkingSetRole::None));
	TEST_ASSERT_FALSE(isDeclaredRoleSet(static_cast<WorkingSetRole>(0x80)));
}

void test_slot_acquire_records_every_named_role() {
	WorkingSetLeaseSlot lease;
	TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WorkingSetOutcome::Acquired),
	                        static_cast<uint8_t>(lease.acquire(kBoth)));
	TEST_ASSERT_TRUE(lease.isHeld(lease.generation(), WorkingSetRole::PlaybackWindow));
	TEST_ASSERT_TRUE(lease.isHeld(lease.generation(), WorkingSetRole::LiveCapture));
	TEST_ASSERT_FALSE(lease.isHeld(lease.generation(), WorkingSetRole::Interactive));
}

void test_slot_refuses_an_undeclared_role() {
	WorkingSetLeaseSlot lease;
	TEST_ASSERT_EQUAL_UINT8(
	    static_cast<uint8_t>(WorkingSetOutcome::InvalidRole),
	    static_cast<uint8_t>(lease.acquire(static_cast<WorkingSetRole>(0x40))));
	TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WorkingSetRole::None),
	                        static_cast<uint8_t>(lease.roles()));
}

void test_slot_generation_mismatched_release_is_a_reported_no_op() {
	WorkingSetLeaseSlot lease;
	lease.acquire(kBoth);
	// A late cleanup from the previous generation reaches a reused slot runtime.
	TEST_ASSERT_EQUAL_UINT8(
	    static_cast<uint8_t>(WorkingSetOutcome::StaleGeneration),
	    static_cast<uint8_t>(lease.release(lease.generation() + 1, WorkingSetRole::PlaybackWindow)));
	TEST_ASSERT_TRUE(lease.isHeld(lease.generation(), WorkingSetRole::PlaybackWindow));

	TEST_ASSERT_EQUAL_UINT8(
	    static_cast<uint8_t>(WorkingSetOutcome::Released),
	    static_cast<uint8_t>(lease.release(lease.generation(), WorkingSetRole::PlaybackWindow)));
	TEST_ASSERT_FALSE(lease.isHeld(lease.generation(), WorkingSetRole::PlaybackWindow));
	TEST_ASSERT_TRUE(lease.isHeld(lease.generation(), WorkingSetRole::LiveCapture));
}

void test_slot_release_of_an_unheld_role_is_reported() {
	WorkingSetLeaseSlot lease;
	TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WorkingSetOutcome::NotHeld),
	                        static_cast<uint8_t>(lease.release(0, WorkingSetRole::PlaybackWindow)));
	lease.acquire(WorkingSetRole::PlaybackWindow);
	TEST_ASSERT_EQUAL_UINT8(
	    static_cast<uint8_t>(WorkingSetOutcome::NotHeld),
	    static_cast<uint8_t>(lease.release(lease.generation(), WorkingSetRole::Interactive)));
}

void test_slot_total_release_bumps_the_generation() {
	WorkingSetLeaseSlot lease;
	lease.acquire(kBoth);
	const uint16_t before = lease.generation();
	TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WorkingSetOutcome::Released),
	                        static_cast<uint8_t>(lease.releaseAll(before)));
	TEST_ASSERT_EQUAL_UINT16(before + 1, lease.generation());
	TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WorkingSetRole::None),
	                        static_cast<uint8_t>(lease.roles()));
	// The slot runtime is now reusable; the previous holder's release is stale, not accepted.
	TEST_ASSERT_EQUAL_UINT8(
	    static_cast<uint8_t>(WorkingSetOutcome::StaleGeneration),
	    static_cast<uint8_t>(lease.release(before, WorkingSetRole::PlaybackWindow)));
}

void test_slot_total_release_without_a_hold_does_not_bump() {
	WorkingSetLeaseSlot lease;
	TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WorkingSetOutcome::NotHeld),
	                        static_cast<uint8_t>(lease.releaseAll(lease.generation())));
	TEST_ASSERT_EQUAL_UINT16(0, lease.generation());
}

void test_table_tracks_a_slot_indexed_census() {
	WorkingSetLeaseTable<8> table;
	TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WorkingSetOutcome::Acquired),
	                        static_cast<uint8_t>(table.acquire(0, WorkingSetRole::PlaybackWindow)));
	TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WorkingSetOutcome::Acquired),
	                        static_cast<uint8_t>(table.acquire(1, kBoth)));
	TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WorkingSetOutcome::Acquired),
	                        static_cast<uint8_t>(table.acquire(2, WorkingSetRole::DeferredFinalization)));

	TEST_ASSERT_EQUAL_UINT8(2, table.holderCount(WorkingSetRole::PlaybackWindow));
	TEST_ASSERT_EQUAL_UINT8(1, table.holderCount(WorkingSetRole::LiveCapture));
	TEST_ASSERT_EQUAL_UINT8(1, table.holderCount(WorkingSetRole::DeferredFinalization));
	TEST_ASSERT_EQUAL_UINT8(0, table.holderCount(WorkingSetRole::Interactive));
	TEST_ASSERT_EQUAL_UINT8(3, table.totalHolders());
	TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(kBoth),
	                        static_cast<uint8_t>(table.rolesHeldAtSlot(1)));
	// A composite lease is one holder of each class it names, not two playback holders.
	TEST_ASSERT_TRUE(table.isHeld(1, table.generationAtSlot(1), WorkingSetRole::LiveCapture));
}

void test_table_refuses_an_out_of_range_slot_and_a_bad_role() {
	WorkingSetLeaseTable<8> table;
	TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WorkingSetOutcome::InvalidRole),
	                        static_cast<uint8_t>(table.acquire(8, WorkingSetRole::PlaybackWindow)));
	TEST_ASSERT_EQUAL_UINT8(
	    static_cast<uint8_t>(WorkingSetOutcome::InvalidRole),
	    static_cast<uint8_t>(table.acquire(0, static_cast<WorkingSetRole>(0x80))));
	TEST_ASSERT_EQUAL_UINT8(0, table.totalHolders());
}

void test_table_enforces_the_per_track_capture_cap() {
	WorkingSetLeaseTable<8> table;
	TEST_ASSERT_EQUAL_UINT8(
	    static_cast<uint8_t>(WorkingSetOutcome::Acquired),
	    static_cast<uint8_t>(table.acquire(0, WorkingSetRole::LiveCapture)));
	// LiveCapture is one per track (handoff §2): a second slot cannot hold it.
	TEST_ASSERT_EQUAL_UINT8(
	    static_cast<uint8_t>(WorkingSetOutcome::CapReached),
	    static_cast<uint8_t>(table.acquire(1, WorkingSetRole::LiveCapture)));
	TEST_ASSERT_FALSE(table.isHeld(1, table.generationAtSlot(1), WorkingSetRole::LiveCapture));
	// But a composite request whose other class still has room is still refused.
	TEST_ASSERT_EQUAL_UINT8(
	    static_cast<uint8_t>(WorkingSetOutcome::CapReached),
	    static_cast<uint8_t>(table.acquire(1, kBoth)));
}

void test_table_release_all_roles_bumps_that_slot_generation_only() {
	WorkingSetLeaseTable<8> table;
	table.acquire(3, kBoth);
	table.acquire(4, WorkingSetRole::PlaybackWindow);
	TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WorkingSetOutcome::Released),
	                        static_cast<uint8_t>(table.releaseAllRoles(3, table.generationAtSlot(3))));
	TEST_ASSERT_EQUAL_UINT16(1, table.generationAtSlot(3));
	TEST_ASSERT_EQUAL_UINT16(0, table.generationAtSlot(4));
	TEST_ASSERT_FALSE(table.isHeld(3, 1, WorkingSetRole::PlaybackWindow));
	TEST_ASSERT_TRUE(table.isHeld(4, 0, WorkingSetRole::PlaybackWindow));
}

int main(int /*argc*/, char** /*argv*/) {
	UNITY_BEGIN();
	RUN_TEST(test_roles_are_composable);
	RUN_TEST(test_slot_acquire_records_every_named_role);
	RUN_TEST(test_slot_refuses_an_undeclared_role);
	RUN_TEST(test_slot_generation_mismatched_release_is_a_reported_no_op);
	RUN_TEST(test_slot_release_of_an_unheld_role_is_reported);
	RUN_TEST(test_slot_total_release_bumps_the_generation);
	RUN_TEST(test_slot_total_release_without_a_hold_does_not_bump);
	RUN_TEST(test_table_tracks_a_slot_indexed_census);
	RUN_TEST(test_table_refuses_an_out_of_range_slot_and_a_bad_role);
	RUN_TEST(test_table_enforces_the_per_track_capture_cap);
	RUN_TEST(test_table_release_all_roles_bumps_that_slot_generation_only);
	return UNITY_END();
}
