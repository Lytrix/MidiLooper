//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// `restored-library-stability` task 1.2 — pure classifier implementation. No Arduino, no SD, no globals.
// Placed in flash on device (`TRACK_COLD_MEM`): this tree sits on the RAM1 FlexRAM bank edge.

#include "StorageManager/LibraryObservation.h"

#include <cstdio>
#include <cstring>

#include "Utils/TrackMem.h"

namespace LibraryObservation {

namespace {

bool requestIsOutstanding(Request request) {
    return request == Request::Queued || request == Request::Running || request == Request::Waiting;
}

bool stateIsPopulated(const SlotInput& input) {
    return input.payload == Payload::PresentVerified ||
           (input.contentObserved && input.hasCommittedContent);
}

}  // namespace

TRACK_COLD_MEM SlotResult classifySlot(const SlotInput& input) {
    SlotResult result;
    result.track = input.track;
    result.slot = input.slot;

    // Observed fields, echoed. A request that has not terminated never asserts a terminal.
    result.payload = input.payload;
    result.offered = input.offered;
    result.request = input.request;
    result.requestWait = requestIsOutstanding(input.request) ? input.requestWait : 0;
    result.terminal = requestIsOutstanding(input.request) ? Terminal::None : input.terminal;
    result.requestGeneration = input.requestGeneration;
    result.setEpoch = input.setEpoch;
    result.contentObserved = input.contentObserved;
    result.hasCommittedContent = input.contentObserved && input.hasCommittedContent;
    result.committedPassCount = input.contentObserved ? input.committedPassCount : 0;
    result.highestCommittedPassId = input.contentObserved ? input.highestCommittedPassId : 0;
    result.loopLengthTicks = input.contentObserved ? input.loopLengthTicks : 0;
    result.committedEventCount = input.contentObserved ? input.committedEventCount : 0;
    result.selectionObserved = input.selectionObserved;
    result.requiredForInitialPlayback = input.selectionObserved && input.requiredForInitialPlayback;
    result.committedTerminalInWindow = input.committedTerminalInWindow;

    const bool populated = stateIsPopulated(input);
    const bool pending = requestIsOutstanding(input.request);
    // A **completed** restore whose content field could not be observed (SD admission denied) is a
    // positive completion, not a loss: it keeps a nullable cause rather than being attributed `unknown`.
    const bool committedButContentUnobserved =
        !input.contentObserved && result.terminal == Terminal::Committed;
    // `empty` requires the absence to be **observed**: no payload, no committed content, no outstanding
    // request. If the payload or content field is `Unknown`, absence is not established and the state set
    // stays empty (unknown).
    const bool absenceEstablished = input.payload == Payload::Absent && input.contentObserved &&
                                    !input.hasCommittedContent;
    uint8_t state = 0;
    if (populated) {
        state |= kStatePopulated;
    }
    if (pending) {
        state |= kStatePending;
    }
    if (!populated && !pending && absenceEstablished) {
        state |= kStateEmpty;
    }
    result.currentState = state;

    // Evidence, recorded whatever the cause turns out to be. Only an *observed* validation failure
    // counts; `Unknown` (SD admission denied) is not evidence of loss.
    CauseEvidence evidence;
    evidence.explicitEvictionEvent = input.explicitEvictionEvent;
    evidence.payloadValidationFailed = input.payload == Payload::InvalidToken;
    evidence.refusedTerminal = result.terminal == Terminal::Refused;
    evidence.priorInventoryDisappearance = input.priorInventoryKnown && input.priorPopulated;
    // `catalog omission` means a payload exists that the boot enumeration did **not** offer. It is not
    // inferred from a prior inventory alone: a slot with no payload now would not be offered regardless,
    // and the spec forbids promoting a bare prior-inventory disappearance to a cause.
    evidence.catalogOmissionCandidate =
        input.offered == Offered::NotOffered &&
        (input.payload == Payload::PresentVerified || input.payload == Payload::InvalidToken);
    result.evidence = evidence;

    // Precedence (design D3, spec delta § observed-vs-cause):
    //   1. an explicit eviction/deletion decision is authoritative;
    //   2. content present -> nothing is missing to attribute;
    //   3. an outstanding request -> the outcome is not decided, so no cause is attributed;
    //   3b. a completed restore whose content field was not observable -> likewise no cause;
    //   4. otherwise gather consistent causes; two or more -> `ambiguous`, exactly one -> that cause;
    //   5. none -> `unknown` (a prior inventory establishes the disappearance, not the cause).
    if (evidence.explicitEvictionEvent) {
        result.cause = Cause::IntentionalEviction;
        return result;
    }
    if (populated || pending || committedButContentUnobserved) {
        result.cause = Cause::NotApplicable;
        return result;
    }
    const int consistent = (evidence.payloadValidationFailed ? 1 : 0) +
                           (evidence.refusedTerminal ? 1 : 0) +
                           (evidence.catalogOmissionCandidate ? 1 : 0);
    if (consistent >= 2) {
        result.cause = Cause::Ambiguous;
    } else if (evidence.payloadValidationFailed) {
        result.cause = Cause::ContentLoss;
    } else if (evidence.refusedTerminal) {
        result.cause = Cause::RestoreRefusal;
    } else if (evidence.catalogOmissionCandidate) {
        result.cause = Cause::CatalogOmission;
    } else {
        result.cause = Cause::Unknown;
    }
    return result;
}

TRACK_COLD_MEM void invalidateSlotResult(SlotResult& result) {
    const bool priorDisappearance = result.evidence.priorInventoryDisappearance;
    result.observedThisSlice = false;
    result.invalidated = true;
    result.payload = Payload::Unknown;
    result.offered = Offered::Unknown;
    result.request = Request::Unknown;
    result.requestWait = 0;
    result.terminal = Terminal::None;
    result.contentObserved = false;
    result.hasCommittedContent = false;
    result.committedPassCount = 0;
    result.highestCommittedPassId = 0;
    result.loopLengthTicks = 0;
    result.committedEventCount = 0;
    result.selectionObserved = false;
    result.requiredForInitialPlayback = false;
    result.committedTerminalInWindow = false;
    // No observed field -> an empty state set, which is *unknown*, not `empty`.
    result.currentState = 0;
    result.cause = Cause::Unknown;
    CauseEvidence evidence;
    evidence.priorInventoryDisappearance = priorDisappearance;
    result.evidence = evidence;
}

TRACK_COLD_MEM Report summarize(const SlotResult* slots, uint16_t count, const Window& window) {
    Report report;
    report.window = window;
    if (slots != nullptr) {
        for (uint16_t i = 0; i < count; ++i) {
            const SlotResult& slot = slots[i];
            if (slot.invalidated) {
                continue;
            }
            ++report.slotsObserved;
            if (slot.payload == Payload::PresentVerified) {
                ++report.counts.payloadInventory;
            }
            if (slot.selectionObserved && slot.requiredForInitialPlayback) {
                ++report.counts.initialPlaybackRequired;
            }
            if (slot.committedTerminalInWindow) {
                ++report.counts.observedRestoreCompletions;
            }
            if (slot.contentObserved && slot.hasCommittedContent) {
                ++report.counts.committedAtCheckpoint;
            }
        }
    }
    // `complete` is about window coverage — every initial-playback slot observed and the admission scan
    // finished. `consistentView` is reported separately: an observation assembled across turns is
    // explicitly *not* a single consistent instant (spec delta § consistency and framing).
    report.complete = window.admissionComplete && window.everyInitialPlaybackSlotObserved;
    return report;
}

TRACK_COLD_MEM const char* causeName(Cause cause) {
    switch (cause) {
        case Cause::NotApplicable:
            return "not_applicable";
        case Cause::CatalogOmission:
            return "catalog_omission";
        case Cause::RestoreRefusal:
            return "restore_refusal";
        case Cause::IntentionalEviction:
            return "intentional_eviction";
        case Cause::ContentLoss:
            return "content_loss";
        case Cause::Ambiguous:
            return "ambiguous";
        case Cause::Unknown:
            return "unknown";
    }
    return "unknown";
}

TRACK_COLD_MEM const char* payloadName(Payload payload) {
    switch (payload) {
        case Payload::Unknown:
            return "unknown";
        case Payload::Absent:
            return "absent";
        case Payload::InvalidToken:
            return "invalid_token";
        case Payload::PresentVerified:
            return "present_verified";
    }
    return "unknown";
}

TRACK_COLD_MEM const char* offeredName(Offered offered) {
    switch (offered) {
        case Offered::Unknown:
            return "unknown";
        case Offered::NotOffered:
            return "not_offered";
        case Offered::Offered:
            return "offered";
    }
    return "unknown";
}

TRACK_COLD_MEM const char* requestName(Request request) {
    switch (request) {
        case Request::Unknown:
            return "unknown";
        case Request::None:
            return "none";
        case Request::Queued:
            return "queued";
        case Request::Running:
            return "running";
        case Request::Waiting:
            return "waiting";
        case Request::Terminal:
            return "terminal";
    }
    return "unknown";
}

TRACK_COLD_MEM const char* terminalName(Terminal terminal) {
    switch (terminal) {
        case Terminal::None:
            return "none";
        case Terminal::Committed:
            return "committed";
        case Terminal::Unnecessary:
            return "unnecessary";
        case Terminal::Refused:
            return "refused";
        case Terminal::Superseded:
            return "superseded";
    }
    return "unknown";
}

TRACK_COLD_MEM char causeCode(Cause cause) {
    switch (cause) {
        case Cause::NotApplicable:
            return 'n';
        case Cause::CatalogOmission:
            return 'c';
        case Cause::RestoreRefusal:
            return 'r';
        case Cause::IntentionalEviction:
            return 'i';
        case Cause::ContentLoss:
            return 'l';
        case Cause::Ambiguous:
            return 'a';
        case Cause::Unknown:
            return 'u';
    }
    return 'u';
}

TRACK_COLD_MEM uint8_t payloadCode(Payload payload) {
    return static_cast<uint8_t>(payload);
}

TRACK_COLD_MEM uint8_t offeredCode(Offered offered) {
    return static_cast<uint8_t>(offered);
}

TRACK_COLD_MEM uint8_t requestCode(Request request) {
    return static_cast<uint8_t>(request);
}

TRACK_COLD_MEM uint8_t terminalCode(Terminal terminal) {
    return static_cast<uint8_t>(terminal);
}

TRACK_COLD_MEM const char* writeStateNames(uint8_t state, char* out, size_t outSize) {
    if (out == nullptr || outSize == 0) {
        return out;
    }
    out[0] = '\0';
    if (state == 0) {
        std::snprintf(out, outSize, "unknown");
        return out;
    }
    const char* separator = "";
    if ((state & kStateEmpty) != 0) {
        std::snprintf(out, outSize, "%sempty", separator);
        separator = "+";
    }
    if ((state & kStatePopulated) != 0) {
        const size_t used = std::strlen(out);
        std::snprintf(out + used, outSize > used ? outSize - used : 0, "%spopulated", separator);
        separator = "+";
    }
    if ((state & kStatePending) != 0) {
        const size_t used = std::strlen(out);
        std::snprintf(out + used, outSize > used ? outSize - used : 0, "%spending", separator);
    }
    return out;
}

}  // namespace LibraryObservation
