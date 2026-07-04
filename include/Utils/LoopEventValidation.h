//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <unordered_set>

#include "MidiEvent.h"

namespace LoopEventValidation {

enum class LoopEventCheck : uint32_t {
  NoteOnInLoopRange = 1u << 0,
  LinearNoteOff = 1u << 1,
  NoWrappedPairStorage = 1u << 2,
  DerivedLength = 1u << 3,
  NoteIdPairing = 1u << 4,
  PersistedTickCap = 1u << 5,
  OrphanNoteOff = 1u << 6,
  OrphanNoteOn = 1u << 7,
};

constexpr uint32_t kCanonicalInvariantMask =
    static_cast<uint32_t>(LoopEventCheck::NoteOnInLoopRange) |
    static_cast<uint32_t>(LoopEventCheck::LinearNoteOff) |
    static_cast<uint32_t>(LoopEventCheck::NoWrappedPairStorage) |
    static_cast<uint32_t>(LoopEventCheck::DerivedLength) |
    static_cast<uint32_t>(LoopEventCheck::NoteIdPairing) |
    static_cast<uint32_t>(LoopEventCheck::PersistedTickCap);

constexpr uint32_t kIdleCleanupMask =
    kCanonicalInvariantMask | static_cast<uint32_t>(LoopEventCheck::OrphanNoteOff) |
    static_cast<uint32_t>(LoopEventCheck::OrphanNoteOn);

struct LoopEventValidationResult {
  bool passed = true;
  LoopEventCheck firstFailure = LoopEventCheck::NoteOnInLoopRange;
};

/// Geometry checks expected on the edit closure set after micro normalize (subset of canonical 1–7).
constexpr uint32_t kClosureLinearGeometryMask =
    static_cast<uint32_t>(LoopEventCheck::LinearNoteOff) |
    static_cast<uint32_t>(LoopEventCheck::NoWrappedPairStorage) |
    static_cast<uint32_t>(LoopEventCheck::DerivedLength) |
    static_cast<uint32_t>(LoopEventCheck::NoteIdPairing);

/// Pure predicates — does not mutate events or call normalize.
LoopEventValidationResult validateLoopEvents(const MidiEventVec& events, uint32_t loopLength,
                                             uint32_t checkMask,
                                             uint32_t wrapWindowTicks = 768);

/// Events whose NoteId is in closure, plus channel/pitch-matched on/off pairs for those notes.
MidiEventVec extractEventsForNoteIds(const MidiEventVec& events,
                                     const std::unordered_set<NoteId>& noteIds);

bool checkNoteOnInLoopRange(const MidiEventVec& events, uint32_t loopLength);
bool checkLinearNoteOff(const MidiEventVec& events, uint32_t loopLength);
bool checkNoWrappedPairStorage(const MidiEventVec& events, uint32_t loopLength,
                               uint32_t wrapWindowTicks);
bool checkDerivedLength(const MidiEventVec& events, uint32_t loopLength);
bool checkNoteIdPairing(const MidiEventVec& events);
bool checkPersistedTickCap(const MidiEventVec& events, uint32_t loopLength);
bool checkOrphanNoteOff(const MidiEventVec& events, uint32_t loopLength,
                        uint32_t wrapWindowTicks);
bool checkOrphanNoteOn(const MidiEventVec& events, uint32_t loopLength);

struct OrphanRepairResult {
  size_t orphanedRemoved = 0;
};

/// Sorts events, removes orphan note-ons/offs (LIFO duplicate ons). Does not insert synthetic offs.
OrphanRepairResult repairOrphanNoteEvents(MidiEventVec& events, uint32_t loopLengthTicks,
                                          uint32_t wrapWindowTicks = 768);

}  // namespace LoopEventValidation
