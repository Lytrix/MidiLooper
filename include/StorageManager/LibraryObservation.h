//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// `restored-library-stability` task 1.2 — the **pure** library-observation classifier.
//
// `design.md` D3/D4: the report carries, per `(track, slot)`, the observed fields, a **current state**
// that is a *set* over `empty` / `populated` / `pending`, and separately a **nullable historical cause**
// from `catalog omission` / `restore refusal` / `intentional eviction` / `content loss` / `ambiguous` /
// `unknown`. `empty` is a state and is never a cause. The classifier is a pure function over a snapshot:
// it owns no runtime state, reads no SD, and is deliberately not wired into a runtime decision in
// Packet A. Task 1.4's Python classifier is its offline mirror over `RRQ` capture lines.
//
// Observation is separate from attribution, so "not observed" is not "absent": a field that could not be
// observed is `Unknown`, and a slot with no observed field at all has an **empty state set** (unknown),
// not `empty`. `empty` is derived only from observed absence.
#pragma once

#include <cstddef>
#include <cstdint>

namespace LibraryObservation {

/// Payload observation. `Unknown` means "present in the RAM mirror, but the token could not be verified
/// (SD admission denied)"; `Absent` is the RAM mirror's own answer and needs no SD read.
enum class Payload : uint8_t { Unknown = 0, Absent, InvalidToken, PresentVerified };

/// Whether the slot was offered to the boot/background restore path (a request record exists for it).
enum class Offered : uint8_t { Unknown = 0, NotOffered, Offered };

/// The request lifecycle as observed. Mirrors `StorageManagerInternal::RestoreRequestState` plus `None`
/// (no record) and `Unknown` (not observed).
enum class Request : uint8_t { Unknown = 0, None, Queued, Running, Waiting, Terminal };

/// Mirrors `StorageManagerInternal::RestoreRequestTerminal`.
enum class Terminal : uint8_t { None = 0, Committed, Unnecessary, Refused, Superseded };

/// The current state is a **set**, not a scalar. `0` means no state could be observed.
enum StateBit : uint8_t {
    kStateEmpty = 1u << 0,
    kStatePopulated = 1u << 1,
    kStatePending = 1u << 2,
};

/// The historical cause. `NotApplicable` is the **null** value: nothing is missing to attribute (a
/// populated slot, or a request still in flight). `Unknown` is a *recorded* cause and is used only when
/// something is missing and no consistent cause could be established.
enum class Cause : uint8_t {
    NotApplicable = 0,
    CatalogOmission,
    RestoreRefusal,
    IntentionalEviction,
    ContentLoss,
    Ambiguous,
    Unknown,
};

/// Which recorded facts support the cause. Always reported beside the cause; a cause is never recorded
/// without its evidence.
struct CauseEvidence {
    bool explicitEvictionEvent = false;
    bool payloadValidationFailed = false;
    bool refusedTerminal = false;
    bool catalogOmissionCandidate = false;
    bool priorInventoryDisappearance = false;
};

/// The per-slot observation snapshot. Produced by task 1.3's collector from production state, or by the
/// offline capture analysis, and consumed by `classifySlot`.
struct SlotInput {
    uint8_t track = 0;
    uint8_t slot = 0;

    Payload payload = Payload::Unknown;
    Offered offered = Offered::Unknown;
    Request request = Request::Unknown;
    uint8_t requestWait = 0;
    Terminal terminal = Terminal::None;
    bool requestRecordExists = false;
    uint32_t requestGeneration = 0;
    uint32_t setEpoch = 0;

    /// `false` when the committed-content fields could not be observed (SD admission denied).
    bool contentObserved = false;
    /// The **union** content signal: the slot holds content in *some* representation.
    /// `hasCommittedContent` is the union of the two class bits below, not the pass-backed subset.
    bool hasCommittedContent = false;
    /// Pass-backed content (`Loop::passes`: a record pass or any committed overdub pass).
    bool hasPassContent = false;
    /// Published-root content: a root **is installed** on the slot. This is the LSV1 restore's
    /// representation and it leaves `passes` empty, so it is the only content signal for those slots.
    bool hasRootContent = false;
    /// Whether that installed root currently represents the live committed-pass state
    /// (`Loop::publishedSnapshotIsCurrent()`). An installed-but-not-current root is still content —
    /// playback reads it through the stale-root overlay path — so this is reported **beside**
    /// `hasRootContent`, never used to define it.
    bool rootIsCurrent = false;
    uint16_t committedPassCount = 0;
    uint32_t highestCommittedPassId = 0;
    uint32_t loopLengthTicks = 0;
    uint32_t committedEventCount = 0;

    /// `false` when the boot-playback selection could not be observed.
    bool selectionObserved = false;
    bool requiredForInitialPlayback = false;

    /// Distinct slots whose `committed` terminal was observed **within this observation's window**.
    bool committedTerminalInWindow = false;

    /// History. A prior inventory establishes a *disappearance*, never its cause.
    bool priorInventoryKnown = false;
    bool priorPopulated = false;
    bool explicitEvictionEvent = false;
};

/// The classifier's two separate outputs plus the echoed observed fields.
struct SlotResult {
    uint8_t track = 0;
    uint8_t slot = 0;
    uint8_t currentState = 0;
    Cause cause = Cause::NotApplicable;
    CauseEvidence evidence{};

    Payload payload = Payload::Unknown;
    Offered offered = Offered::Unknown;
    Request request = Request::Unknown;
    uint8_t requestWait = 0;
    Terminal terminal = Terminal::None;
    uint32_t requestGeneration = 0;
    uint32_t setEpoch = 0;
    bool contentObserved = false;
    bool hasCommittedContent = false;
    bool hasPassContent = false;
    bool hasRootContent = false;
    bool rootIsCurrent = false;
    uint16_t committedPassCount = 0;
    uint32_t highestCommittedPassId = 0;
    uint32_t loopLengthTicks = 0;
    uint32_t committedEventCount = 0;
    bool selectionObserved = false;
    bool requiredForInitialPlayback = false;
    bool committedTerminalInWindow = false;

    /// `false` once the record was invalidated (a generation/epoch/content-revision change between
    /// slices): the observed fields are back to `Unknown` and the record is not counted.
    bool observedThisSlice = true;
    bool invalidated = false;
};

/// Classify one slot. Never asserts a terminal for a request that has not terminated.
SlotResult classifySlot(const SlotInput& input);

/// Invalidate an already-classified record: every observed field returns to `Unknown`, the state set
/// empties, and the cause is `Unknown` with whatever evidence the record already carried. Used when a
/// slot's request generation, content revision or set epoch changed between slices.
void invalidateSlotResult(SlotResult& result);

/// The observation window and its completeness.
struct Window {
    uint32_t startUs = 0;
    uint32_t endUs = 0;
    bool admissionComplete = false;
    bool everyInitialPlaybackSlotObserved = false;
    bool consistentView = true;
    uint16_t turns = 0;
};

/// D4's four distinct quantities. None of them is "the library size".
struct Counts {
    uint16_t payloadInventory = 0;
    uint16_t initialPlaybackRequired = 0;
    uint16_t observedRestoreCompletions = 0;
    /// The loaded population: every slot with content in **any** representation (pass-backed,
    /// published-root-backed, or both). A slot with both is counted **once**.
    uint16_t committedAtCheckpoint = 0;
    /// The two content representations, reported separately so the union above is auditable and a
    /// pass-backed-only count can never be read as the loaded population.
    uint16_t passBackedAtCheckpoint = 0;
    uint16_t rootBackedAtCheckpoint = 0;
};

struct Report {
    Counts counts{};
    Window window{};
    uint16_t slotsObserved = 0;
    /// True only when every window completeness term holds; an incomplete window's counts are not a
    /// settled library state.
    bool complete = false;
};

/// Summarize a set of slot results. Invalidated records are not counted.
Report summarize(const SlotResult* slots, uint16_t count, const Window& window);

const char* causeName(Cause cause);
const char* payloadName(Payload payload);
const char* offeredName(Offered offered);
const char* requestName(Request request);
const char* terminalName(Terminal terminal);

// Compact wire codes for the capture records. The session-capture text cap is 192 bytes
// (`DebugSessionCapture::kMaxCaptureTextBytes`), and a full-name record exceeds it and is silently
// truncated — measured on device 2026-09-24, where the frame's trailing fields were lost. The names stay
// the human-facing API; the wire carries codes.
char causeCode(Cause cause);
uint8_t payloadCode(Payload payload);
uint8_t offeredCode(Offered offered);
uint8_t requestCode(Request request);
uint8_t terminalCode(Terminal terminal);

/// Write the state set as a `+`-joined list (`empty`, `populated+pending`, `unknown`) into `out`.
/// Returns `out`.
const char* writeStateNames(uint8_t state, char* out, size_t outSize);

}  // namespace LibraryObservation
