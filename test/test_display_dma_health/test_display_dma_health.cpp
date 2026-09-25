//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Host test for the recoverable display DMA transport policy
// (openspec/changes/recoverable-async-display-dma, task 2.3).
//
// SSD1322_API delegates every health decision to SSD1322_DMA_HEALTH, so these cases
// drive the *production* transitions with no bus attached: rejected submission,
// timeout recovery, successful retry, and finite degraded mode.

#include <unity.h>

#include "SSD1322_DMA_HEALTH.h"

namespace {

SSD1322_DMA_HEALTH initializedHealth() {
	SSD1322_DMA_HEALTH health;
	health.markInitialized(true);
	return health;
}

}  // namespace

void test_uninitialized_transport_never_submits_dma() {
	SSD1322_DMA_HEALTH health;
	TEST_ASSERT_FALSE(health.isInitialized());
	TEST_ASSERT_FALSE(health.canSubmitDma(100000));
	health.markInitialized(true);
	TEST_ASSERT_TRUE(health.canSubmitDma(100000));
}

void test_rejected_submission_arms_the_retry_cooldown() {
	SSD1322_DMA_HEALTH health = initializedHealth();
	TEST_ASSERT_TRUE(health.canSubmitDma(1000));

	health.onSubmissionRejected(1000);

	TEST_ASSERT_EQUAL_UINT32(1, health.rejections());
	TEST_ASSERT_EQUAL_UINT8(1, health.failureRun());
	TEST_ASSERT_EQUAL_UINT32(1100, health.retryAtMs());
	TEST_ASSERT_FALSE(health.isDegraded());
	TEST_ASSERT_FALSE(health.canSubmitDma(1099));
	TEST_ASSERT_TRUE(health.canSubmitDma(1100));
}

void test_timeout_recovery_arms_the_retry_cooldown() {
	SSD1322_DMA_HEALTH health = initializedHealth();
	TEST_ASSERT_FALSE(health.hasTimedOut(1019, 1000));
	TEST_ASSERT_TRUE(health.hasTimedOut(1020, 1000));

	health.onTimeout(1020);

	TEST_ASSERT_EQUAL_UINT32(1, health.timeouts());
	TEST_ASSERT_EQUAL_UINT8(1, health.failureRun());
	TEST_ASSERT_FALSE(health.isDegraded());
	TEST_ASSERT_FALSE(health.canSubmitDma(1119));
	TEST_ASSERT_TRUE(health.canSubmitDma(1120));
}

void test_transient_failure_returns_to_dma_on_a_successful_retry() {
	SSD1322_DMA_HEALTH health = initializedHealth();
	health.onSubmissionRejected(0);
	TEST_ASSERT_FALSE(health.canSubmitDma(99));

	// The retry is admitted once the cooldown elapses and the wire drains.
	TEST_ASSERT_TRUE(health.canSubmitDma(100));
	health.onTransferComplete();

	TEST_ASSERT_EQUAL_UINT8(0, health.failureRun());
	TEST_ASSERT_EQUAL_UINT32(1, health.recoveries());
	TEST_ASSERT_EQUAL_UINT32(1, health.completedFrames());
	TEST_ASSERT_FALSE(health.isDegraded());
	TEST_ASSERT_TRUE(health.canSubmitDma(100));
}

void test_consecutive_failures_enter_finite_degraded_mode() {
	SSD1322_DMA_HEALTH health = initializedHealth();
	health.onSubmissionRejected(0);
	health.onTimeout(200);
	TEST_ASSERT_FALSE(health.isDegraded());

	health.onSubmissionRejected(400);

	TEST_ASSERT_TRUE(health.isDegraded());
	TEST_ASSERT_EQUAL_UINT8(3, health.failureRun());
	TEST_ASSERT_EQUAL_UINT32(2, health.rejections());
	TEST_ASSERT_EQUAL_UINT32(1, health.timeouts());
	// Degraded is boot-local and finite: no later time re-admits DMA.
	TEST_ASSERT_FALSE(health.canSubmitDma(100000));
}

void test_success_resets_a_partial_failure_run() {
	SSD1322_DMA_HEALTH health = initializedHealth();
	health.onTimeout(0);
	health.onTimeout(200);
	TEST_ASSERT_EQUAL_UINT8(2, health.failureRun());

	health.onTransferComplete();
	TEST_ASSERT_EQUAL_UINT8(0, health.failureRun());

	// Two more failures are a new run, not a fourth consecutive one.
	health.onTimeout(400);
	health.onTimeout(600);
	TEST_ASSERT_FALSE(health.isDegraded());
	TEST_ASSERT_EQUAL_UINT32(1, health.recoveries());
}

void test_blocking_frames_are_censused_apart_from_dma_frames() {
	SSD1322_DMA_HEALTH health = initializedHealth();
	health.onTransferComplete();
	health.onTransferComplete();
	health.onBlockingFrame();
	health.onBlockingFrame();
	health.onBlockingFrame();

	TEST_ASSERT_EQUAL_UINT32(2, health.completedFrames());
	TEST_ASSERT_EQUAL_UINT32(3, health.blockingFrames());
}

void test_default_limits_match_the_recorded_derivation() {
	SSD1322_DMA_HEALTH health;
	TEST_ASSERT_EQUAL_UINT32(20, health.limits().timeoutMs);
	TEST_ASSERT_EQUAL_UINT32(100, health.limits().retryDelayMs);
	TEST_ASSERT_EQUAL_UINT8(3, health.limits().failureLimit);
}

void test_millis_wraparound_is_handled() {
	SSD1322_DMA_HEALTH health = initializedHealth();
	health.onSubmissionRejected(0xFFFFFFF0u);
	// retryAtMs_ wraps to 0x54; the cooldown still reads correctly across the rollover.
	TEST_ASSERT_FALSE(health.canSubmitDma(0xFFFFFFF0u));
	TEST_ASSERT_TRUE(health.canSubmitDma(0x54u));
	TEST_ASSERT_FALSE(health.hasTimedOut(0xFFFFFFF0u, 0xFFFFFFF0u));
	TEST_ASSERT_TRUE(health.hasTimedOut(0x00000004u, 0xFFFFFFF0u));
}

int main(int /*argc*/, char** /*argv*/) {
	UNITY_BEGIN();
	RUN_TEST(test_uninitialized_transport_never_submits_dma);
	RUN_TEST(test_rejected_submission_arms_the_retry_cooldown);
	RUN_TEST(test_timeout_recovery_arms_the_retry_cooldown);
	RUN_TEST(test_transient_failure_returns_to_dma_on_a_successful_retry);
	RUN_TEST(test_consecutive_failures_enter_finite_degraded_mode);
	RUN_TEST(test_success_resets_a_partial_failure_run);
	RUN_TEST(test_blocking_frames_are_censused_apart_from_dma_frames);
	RUN_TEST(test_default_limits_match_the_recorded_derivation);
	RUN_TEST(test_millis_wraparound_is_handled);
	return UNITY_END();
}
