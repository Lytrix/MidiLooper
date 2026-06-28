//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "DeferredSaveDisplayStatus.h"

void test_idle_when_no_save_activity() {
    DeferredSaveDisplayInputs inputs{};
    const DeferredSaveDisplayStatus status = resolveDeferredSaveDisplayStatus(1000, inputs);
    TEST_ASSERT_EQUAL(DeferredSaveDisplayPhase::Idle, status.phase);
    TEST_ASSERT_EQUAL(0, status.rotateStep);
}

void test_pending_when_queued_not_dispatching() {
    DeferredSaveDisplayInputs inputs{};
    inputs.savePending = true;
    const DeferredSaveDisplayStatus status = resolveDeferredSaveDisplayStatus(1000, inputs);
    TEST_ASSERT_EQUAL(DeferredSaveDisplayPhase::Pending, status.phase);
}

void test_in_progress_overrides_pending() {
    DeferredSaveDisplayInputs inputs{};
    inputs.savePending = true;
    inputs.saveInProgress = true;
    const DeferredSaveDisplayStatus status = resolveDeferredSaveDisplayStatus(450, inputs);
    TEST_ASSERT_EQUAL(DeferredSaveDisplayPhase::InProgress, status.phase);
    TEST_ASSERT_EQUAL(2, status.rotateStep);
}

void test_rotate_step_cycles_every_200_ms() {
    DeferredSaveDisplayInputs inputs{};
    inputs.saveInProgress = true;
    TEST_ASSERT_EQUAL(0, resolveDeferredSaveDisplayStatus(0, inputs).rotateStep);
    TEST_ASSERT_EQUAL(1, resolveDeferredSaveDisplayStatus(200, inputs).rotateStep);
    TEST_ASSERT_EQUAL(2, resolveDeferredSaveDisplayStatus(400, inputs).rotateStep);
    TEST_ASSERT_EQUAL(3, resolveDeferredSaveDisplayStatus(600, inputs).rotateStep);
    TEST_ASSERT_EQUAL(0, resolveDeferredSaveDisplayStatus(800, inputs).rotateStep);
}

void test_completed_flash_within_800_ms() {
    DeferredSaveDisplayInputs inputs{};
    inputs.completedAtMs = 5000;
    TEST_ASSERT_EQUAL(DeferredSaveDisplayPhase::Completed,
                      resolveDeferredSaveDisplayStatus(5500, inputs).phase);
    TEST_ASSERT_EQUAL(DeferredSaveDisplayPhase::Idle,
                      resolveDeferredSaveDisplayStatus(5800, inputs).phase);
}

void test_failed_flash_within_800_ms() {
    DeferredSaveDisplayInputs inputs{};
    inputs.failedAtMs = 10000;
    TEST_ASSERT_EQUAL(DeferredSaveDisplayPhase::Failed,
                      resolveDeferredSaveDisplayStatus(10500, inputs).phase);
    TEST_ASSERT_EQUAL(DeferredSaveDisplayPhase::Idle,
                      resolveDeferredSaveDisplayStatus(10800, inputs).phase);
}

void test_in_progress_overrides_completed_flash() {
    DeferredSaveDisplayInputs inputs{};
    inputs.saveInProgress = true;
    inputs.completedAtMs = 1000;
    const DeferredSaveDisplayStatus status = resolveDeferredSaveDisplayStatus(1200, inputs);
    TEST_ASSERT_EQUAL(DeferredSaveDisplayPhase::InProgress, status.phase);
}

void test_pending_overrides_completed_flash() {
    DeferredSaveDisplayInputs inputs{};
    inputs.savePending = true;
    inputs.completedAtMs = 1000;
    const DeferredSaveDisplayStatus status = resolveDeferredSaveDisplayStatus(1200, inputs);
    TEST_ASSERT_EQUAL(DeferredSaveDisplayPhase::Pending, status.phase);
}

void test_revision_commit_pending_shows_pending() {
    DeferredSaveDisplayInputs inputs{};
    inputs.revisionCommitPending = true;
    const DeferredSaveDisplayStatus status = resolveDeferredSaveDisplayStatus(1000, inputs);
    TEST_ASSERT_EQUAL(DeferredSaveDisplayPhase::Pending, status.phase);
}

void test_revision_commit_in_progress_overrides_pending() {
    DeferredSaveDisplayInputs inputs{};
    inputs.revisionCommitPending = true;
    inputs.revisionCommitInProgress = true;
    const DeferredSaveDisplayStatus status = resolveDeferredSaveDisplayStatus(400, inputs);
    TEST_ASSERT_EQUAL(DeferredSaveDisplayPhase::InProgress, status.phase);
    TEST_ASSERT_EQUAL(2, status.rotateStep);
}

void test_load_idle_when_no_load_activity() {
    DeferredLoadDisplayInputs inputs{};
    const DeferredSaveDisplayStatus status = resolveDeferredLoadDisplayStatus(1000, inputs);
    TEST_ASSERT_EQUAL(DeferredSaveDisplayPhase::Idle, status.phase);
}

void test_load_pending_when_queued_not_dispatching() {
    DeferredLoadDisplayInputs inputs{};
    inputs.loadPending = true;
    const DeferredSaveDisplayStatus status = resolveDeferredLoadDisplayStatus(1000, inputs);
    TEST_ASSERT_EQUAL(DeferredSaveDisplayPhase::Pending, status.phase);
}

void test_load_in_progress_overrides_pending() {
    DeferredLoadDisplayInputs inputs{};
    inputs.loadPending = true;
    inputs.loadInProgress = true;
    const DeferredSaveDisplayStatus status = resolveDeferredLoadDisplayStatus(450, inputs);
    TEST_ASSERT_EQUAL(DeferredSaveDisplayPhase::InProgress, status.phase);
    TEST_ASSERT_EQUAL(2, status.rotateStep);
}

void test_save_then_load_commit_counts_as_load_in_progress() {
    DeferredLoadDisplayInputs inputs{};
    inputs.loadPending = true;
    inputs.saveThenLoadCommitInProgress = true;
    const DeferredSaveDisplayStatus status = resolveDeferredLoadDisplayStatus(400, inputs);
    TEST_ASSERT_EQUAL(DeferredSaveDisplayPhase::InProgress, status.phase);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_idle_when_no_save_activity);
    RUN_TEST(test_pending_when_queued_not_dispatching);
    RUN_TEST(test_in_progress_overrides_pending);
    RUN_TEST(test_rotate_step_cycles_every_200_ms);
    RUN_TEST(test_completed_flash_within_800_ms);
    RUN_TEST(test_failed_flash_within_800_ms);
    RUN_TEST(test_in_progress_overrides_completed_flash);
    RUN_TEST(test_pending_overrides_completed_flash);
    RUN_TEST(test_revision_commit_pending_shows_pending);
    RUN_TEST(test_revision_commit_in_progress_overrides_pending);
    RUN_TEST(test_load_idle_when_no_load_activity);
    RUN_TEST(test_load_pending_when_queued_not_dispatching);
    RUN_TEST(test_load_in_progress_overrides_pending);
    RUN_TEST(test_save_then_load_commit_counts_as_load_in_progress);
    return UNITY_END();
}
