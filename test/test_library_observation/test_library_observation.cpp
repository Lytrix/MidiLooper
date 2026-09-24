//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// `restored-library-stability` Packet A / task 1.2 — the pure classifier's contract.
//
// Oracle: the approved spec delta `lazy-slot-hydration` § *Observed current state is reported separately
// from a nullable historical cause* and `design.md` D3/D4, restated in
// `openspec/changes/restored-library-stability/ARCHITECTURE-REVIEW.md` § Packet for task 1.2 (L01–L08).
// The expected states/causes are written explicitly here; they are not read back from the implementation.

#include <unity.h>

#include <cstring>

#include "../../src/StorageManager/LibraryObservation.cpp"

using namespace LibraryObservation;

namespace {

/// A slot observed as having nothing at all: payload absent (the RAM mirror's own answer, no SD read),
/// no committed content, no request.
SlotInput emptySlotInput() {
    SlotInput input;
    input.track = 3;
    input.slot = 2;
    input.payload = Payload::Absent;
    input.offered = Offered::NotOffered;
    input.request = Request::None;
    input.contentObserved = true;
    input.hasCommittedContent = false;
    input.selectionObserved = true;
    input.requiredForInitialPlayback = false;
    return input;
}

bool isLossCause(Cause cause) {
    return cause == Cause::CatalogOmission || cause == Cause::RestoreRefusal ||
           cause == Cause::IntentionalEviction || cause == Cause::ContentLoss;
}

}  // namespace

/// L01 — the overlapping-input case. One case asserts **both** outputs, and that `empty` is not a cause.
void test_l01_empty_with_no_history_is_empty_state_and_unknown_cause() {
    const SlotResult result = classifySlot(emptySlotInput());

    TEST_ASSERT_TRUE_MESSAGE((result.currentState & kStateEmpty) != 0, "an observed absent slot is empty");
    TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(result.currentState & kStatePopulated));
    TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(result.currentState & kStatePending));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Cause::Unknown), static_cast<uint8_t>(result.cause));
    // `empty` is a state; it must not be recorded as a cause, and neither may any loss cause.
    TEST_ASSERT_FALSE(isLossCause(result.cause));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Cause::Unknown), static_cast<uint8_t>(result.cause));
    TEST_ASSERT_FALSE(result.evidence.payloadValidationFailed);
    TEST_ASSERT_FALSE(result.evidence.refusedTerminal);
    TEST_ASSERT_FALSE(result.evidence.priorInventoryDisappearance);
}

/// L02 — a prior inventory establishes the disappearance, not the cause.
void test_l02_prior_inventory_without_an_event_is_unknown_not_content_loss() {
    SlotInput input = emptySlotInput();
    input.priorInventoryKnown = true;
    input.priorPopulated = true;

    const SlotResult result = classifySlot(input);

    TEST_ASSERT_TRUE((result.currentState & kStateEmpty) != 0);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Cause::Unknown), static_cast<uint8_t>(result.cause));
    TEST_ASSERT_TRUE_MESSAGE(result.evidence.priorInventoryDisappearance,
                             "the prior inventory is the evidence for the disappearance");
    TEST_ASSERT_FALSE_MESSAGE(result.evidence.payloadValidationFailed, "no validation failure was observed");
    TEST_ASSERT_FALSE(result.evidence.refusedTerminal);
    TEST_ASSERT_NOT_EQUAL(static_cast<uint8_t>(Cause::ContentLoss), static_cast<uint8_t>(result.cause));
    TEST_ASSERT_NOT_EQUAL(static_cast<uint8_t>(Cause::IntentionalEviction),
                          static_cast<uint8_t>(result.cause));
}

/// L03 — only an explicit eviction/deletion event yields `intentional eviction`.
void test_l03_explicit_eviction_event_records_intentional_eviction() {
    SlotInput input = emptySlotInput();
    input.priorInventoryKnown = true;
    input.priorPopulated = true;
    input.explicitEvictionEvent = true;

    const SlotResult result = classifySlot(input);

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Cause::IntentionalEviction),
                            static_cast<uint8_t>(result.cause));
    TEST_ASSERT_TRUE(result.evidence.explicitEvictionEvent);
}

/// L04 — a corrupt payload that also refused is not forced into one cause.
void test_l04_corrupt_payload_and_refused_restore_record_both_facts_as_ambiguous() {
    SlotInput input = emptySlotInput();
    input.payload = Payload::InvalidToken;
    input.offered = Offered::Offered;
    input.request = Request::Terminal;
    input.terminal = Terminal::Refused;
    input.requestRecordExists = true;

    const SlotResult result = classifySlot(input);

    TEST_ASSERT_TRUE_MESSAGE(result.evidence.payloadValidationFailed, "validation failure is a recorded fact");
    TEST_ASSERT_TRUE_MESSAGE(result.evidence.refusedTerminal, "the refused terminal is a recorded fact");
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Cause::Ambiguous), static_cast<uint8_t>(result.cause));
    // The invalid payload is not a usable payload, so no populated state is claimed.
    TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(result.currentState & kStatePopulated));
}

/// L05 — an unfinished request is `pending`, and no terminal is asserted for its identity.
void test_l05_outstanding_request_is_pending_and_asserts_no_terminal() {
    const Request outstanding[] = {Request::Queued, Request::Running, Request::Waiting};
    for (Request request : outstanding) {
        SlotInput input = emptySlotInput();
        input.request = request;
        input.requestWait = 3;  // waiting_commit_gate
        input.requestRecordExists = true;
        // A stale terminal in the record must not be reported for a live request.
        input.terminal = Terminal::Committed;

        const SlotResult result = classifySlot(input);

        TEST_ASSERT_TRUE_MESSAGE((result.currentState & kStatePending) != 0, "an outstanding request is pending");
        TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(result.currentState & kStateEmpty));
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Terminal::None), static_cast<uint8_t>(result.terminal));
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Cause::NotApplicable),
                                static_cast<uint8_t>(result.cause));
        TEST_ASSERT_EQUAL_UINT8(3, result.requestWait);
    }
}

/// L06 — two equally consistent causes are both recorded, and the cause is `ambiguous`.
void test_l06_two_consistent_causes_are_recorded_not_resolved_by_preference() {
    SlotInput input = emptySlotInput();
    input.payload = Payload::InvalidToken;   // content loss candidate
    input.offered = Offered::NotOffered;     // catalog omission candidate
    input.priorInventoryKnown = true;
    input.priorPopulated = true;

    const SlotResult result = classifySlot(input);

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Cause::Ambiguous), static_cast<uint8_t>(result.cause));
    TEST_ASSERT_TRUE(result.evidence.payloadValidationFailed);
    TEST_ASSERT_TRUE(result.evidence.catalogOmissionCandidate);
}

/// L07 — a populated slot has a **nullable** cause: nothing is missing to attribute.
void test_l07_populated_slot_has_a_nullable_cause() {
    SlotInput withVerifiedPayload = emptySlotInput();
    withVerifiedPayload.payload = Payload::PresentVerified;
    withVerifiedPayload.offered = Offered::Offered;
    const SlotResult payloadResult = classifySlot(withVerifiedPayload);
    TEST_ASSERT_TRUE((payloadResult.currentState & kStatePopulated) != 0);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Cause::NotApplicable),
                            static_cast<uint8_t>(payloadResult.cause));

    SlotInput withCommittedContent = emptySlotInput();
    withCommittedContent.hasCommittedContent = true;
    withCommittedContent.committedPassCount = 2;
    withCommittedContent.committedEventCount = 48;
    const SlotResult contentResult = classifySlot(withCommittedContent);
    TEST_ASSERT_TRUE((contentResult.currentState & kStatePopulated) != 0);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Cause::NotApplicable),
                            static_cast<uint8_t>(contentResult.cause));
    TEST_ASSERT_FALSE((contentResult.currentState & kStateEmpty) != 0);
}

/// A slot with no observed field at all is **unknown**, not `empty` — the denied-SD degradation shape.
void test_unobserved_fields_are_unknown_not_empty() {
    SlotInput input;
    input.track = 1;
    input.slot = 1;

    const SlotResult result = classifySlot(input);

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, result.currentState,
                                    "no observed field means an empty state set (unknown), not empty");
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Cause::Unknown), static_cast<uint8_t>(result.cause));
}

/// A present-but-unverifiable payload (SD admission denied) is `unknown`, and the request is still named.
void test_denied_sd_admission_keeps_the_waiting_request_named() {
    SlotInput input = emptySlotInput();
    input.payload = Payload::Unknown;  // present in the mirror, token not verifiable
    input.contentObserved = false;
    input.offered = Offered::Offered;
    input.request = Request::Waiting;
    input.requestWait = 4;  // waiting_job_capacity

    const SlotResult result = classifySlot(input);

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Payload::Unknown), static_cast<uint8_t>(result.payload));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Request::Waiting),
                            static_cast<uint8_t>(result.request));
    TEST_ASSERT_EQUAL_UINT8(4, result.requestWait);
    TEST_ASSERT_TRUE((result.currentState & kStatePending) != 0);
    TEST_ASSERT_FALSE_MESSAGE((result.currentState & kStatePopulated) != 0,
                              "an unverified payload is not a populated slot");
}

/// Invalidation clears every observed field and drops the record from the counts.
void test_invalidation_clears_fields_and_is_excluded_from_counts() {
    SlotInput input = emptySlotInput();
    input.payload = Payload::PresentVerified;
    input.offered = Offered::Offered;
    input.priorInventoryKnown = true;
    input.priorPopulated = true;
    SlotResult result = classifySlot(input);
    TEST_ASSERT_TRUE((result.currentState & kStatePopulated) != 0);

    invalidateSlotResult(result);

    TEST_ASSERT_TRUE(result.invalidated);
    TEST_ASSERT_FALSE(result.observedThisSlice);
    TEST_ASSERT_EQUAL_UINT8(0, result.currentState);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Payload::Unknown), static_cast<uint8_t>(result.payload));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Cause::Unknown), static_cast<uint8_t>(result.cause));
    TEST_ASSERT_TRUE(result.evidence.priorInventoryDisappearance);

    const SlotResult slots[] = {result};
    Window window;
    const Report report = summarize(slots, 1, window);
    TEST_ASSERT_EQUAL_UINT16(0, report.slotsObserved);
    TEST_ASSERT_EQUAL_UINT16(0, report.counts.payloadInventory);
}

/// L08 — the four counts and the window framing are separate outputs.
void test_l08_four_counts_and_window_are_separate_outputs() {
    SlotInput verified = emptySlotInput();
    verified.track = 0;
    verified.slot = 0;
    verified.payload = Payload::PresentVerified;
    verified.offered = Offered::Offered;
    verified.requiredForInitialPlayback = true;
    verified.hasCommittedContent = true;
    verified.committedPassCount = 1;
    verified.committedEventCount = 12;
    verified.committedTerminalInWindow = true;

    SlotInput committedOnly = emptySlotInput();
    committedOnly.track = 1;
    committedOnly.slot = 0;
    committedOnly.requiredForInitialPlayback = true;
    committedOnly.hasCommittedContent = true;
    committedOnly.committedPassCount = 1;
    committedOnly.committedEventCount = 5;

    SlotInput absent = emptySlotInput();
    absent.track = 2;
    absent.slot = 0;

    const SlotResult slots[] = {classifySlot(verified), classifySlot(committedOnly), classifySlot(absent)};

    Window window;
    window.startUs = 1000;
    window.endUs = 2000;
    window.turns = 1;
    window.admissionComplete = false;
    window.everyInitialPlaybackSlotObserved = true;
    window.consistentView = true;
    Report report = summarize(slots, 3, window);

    TEST_ASSERT_EQUAL_UINT16(1, report.counts.payloadInventory);
    TEST_ASSERT_EQUAL_UINT16(2, report.counts.initialPlaybackRequired);
    TEST_ASSERT_EQUAL_UINT16(1, report.counts.observedRestoreCompletions);
    TEST_ASSERT_EQUAL_UINT16(2, report.counts.committedAtCheckpoint);
    TEST_ASSERT_EQUAL_UINT16(3, report.slotsObserved);
    TEST_ASSERT_FALSE_MESSAGE(report.complete, "an incomplete admission scan is not a settled window");
    TEST_ASSERT_EQUAL_UINT32(1000, report.window.startUs);
    TEST_ASSERT_EQUAL_UINT32(2000, report.window.endUs);

    // Completing the window flips only the framing, not the counts.
    window.admissionComplete = true;
    report = summarize(slots, 3, window);
    TEST_ASSERT_TRUE(report.complete);
    TEST_ASSERT_EQUAL_UINT16(1, report.counts.payloadInventory);

    // A cross-turn view is reported as such and does **not** silently pass as a single consistent
    // instant; window coverage (admission + every initial-playback slot) is what `complete` means.
    window.consistentView = false;
    report = summarize(slots, 3, window);
    TEST_ASSERT_FALSE(report.window.consistentView);
    TEST_ASSERT_TRUE_MESSAGE(report.complete,
                             "coverage is complete even though the view is explicitly cross-turn");

    // An initial-playback slot that could not be observed leaves the window incomplete.
    window.everyInitialPlaybackSlotObserved = false;
    report = summarize(slots, 3, window);
    TEST_ASSERT_FALSE(report.complete);
}

/// A completed restore whose content field could not be observed keeps a nullable cause (a positive
/// completion is not a loss), and its state stays `unknown` rather than claiming `empty`.
void test_completed_restore_with_unobserved_content_stays_nullable() {
    SlotInput input;
    input.track = 2;
    input.slot = 1;
    input.payload = Payload::Unknown;  // present in the mirror, token not verifiable
    input.contentObserved = false;     // SD admission denied at this slice
    input.offered = Offered::Offered;
    input.request = Request::Terminal;
    input.terminal = Terminal::Committed;
    input.requestRecordExists = true;

    const SlotResult result = classifySlot(input);

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, result.currentState,
                                    "absence is not established, so the state is unknown, not empty");
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Cause::NotApplicable),
                            static_cast<uint8_t>(result.cause));
}

void test_state_names_write_the_set() {
    char buffer[48];
    TEST_ASSERT_EQUAL_STRING("empty", writeStateNames(kStateEmpty, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_STRING("populated+pending",
                             writeStateNames(kStatePopulated | kStatePending, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_STRING("unknown", writeStateNames(0, buffer, sizeof(buffer)));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_l01_empty_with_no_history_is_empty_state_and_unknown_cause);
    RUN_TEST(test_l02_prior_inventory_without_an_event_is_unknown_not_content_loss);
    RUN_TEST(test_l03_explicit_eviction_event_records_intentional_eviction);
    RUN_TEST(test_l04_corrupt_payload_and_refused_restore_record_both_facts_as_ambiguous);
    RUN_TEST(test_l05_outstanding_request_is_pending_and_asserts_no_terminal);
    RUN_TEST(test_l06_two_consistent_causes_are_recorded_not_resolved_by_preference);
    RUN_TEST(test_l07_populated_slot_has_a_nullable_cause);
    RUN_TEST(test_unobserved_fields_are_unknown_not_empty);
    RUN_TEST(test_denied_sd_admission_keeps_the_waiting_request_named);
    RUN_TEST(test_invalidation_clears_fields_and_is_excluded_from_counts);
    RUN_TEST(test_l08_four_counts_and_window_are_separate_outputs);
    RUN_TEST(test_completed_restore_with_unobserved_content_stays_nullable);
    RUN_TEST(test_state_names_write_the_set);
    return UNITY_END();
}
