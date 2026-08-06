//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Internal helpers for NoteEditFocus domain modules under src/EditManager/.
// Implementations live in src/EditManager/NoteEditFocus*.cpp domain modules.

#pragma once

#include <cstdint>
#include <vector>

#include "EditPass.h"
#include "MidiEvent.h"
#include "NoteEditFocus.h"
#include "Utils/NoteEditMem.h"
#include "Utils/NoteUtils.h"

#if defined(__IMXRT1062__)
#define NOTE_EDIT_FOCUS_INTERNAL_MEM NOTE_EDIT_MEM
#else
#define NOTE_EDIT_FOCUS_INTERNAL_MEM
#endif

// --- NoteEditFocusBaseline (Phase 3) ---

NOTE_EDIT_FOCUS_INTERNAL_MEM bool isDisplayWrappedBaseline(const NoteBaseline& baseline);

NOTE_EDIT_FOCUS_INTERNAL_MEM bool projectCanonicalBaselineForEdit(NoteId noteId,
                                                                  const NoteBaseline& canonical,
                                                                  int32_t originTick,
                                                                  uint32_t loopLength,
                                                                  NoteBaseline& out);

template <typename Alloc>
NOTE_EDIT_FOCUS_INTERNAL_MEM bool readLiveBaselineForOverlapDiff(
    const std::vector<MidiEvent, Alloc>& sessionEvents, NoteId noteId,
    const NoteBaseline& baseline, uint8_t channel, uint32_t loopLength, NoteId movingNoteId,
    NoteBaseline& out);

// --- NoteEditFocusOverlap (Phase 4) ---

NOTE_EDIT_FOCUS_INTERNAL_MEM bool baselineMapPitchLaneNeedsRestore(const NoteEditFocus& focus,
                                                                   const MidiEventVec& liveStore,
                                                                   uint8_t channel,
                                                                   uint8_t clearedPitch);

NOTE_EDIT_FOCUS_INTERNAL_MEM bool linearStorageSpansOverlapLocal(uint32_t start1, uint32_t end1,
                                                                 uint32_t start2, uint32_t end2);

template <typename Alloc>
NOTE_EDIT_FOCUS_INTERNAL_MEM void materializeShortenedOverlap(
    std::vector<MidiEvent, Alloc>& flat, uint8_t channel, const OverlapNote& entry,
    uint32_t loopLength);

NOTE_EDIT_FOCUS_INTERNAL_MEM bool eventMatchesNoteEndpoint(const MidiEvent& e, uint8_t channel,
                                                           uint8_t pitch, uint32_t tick,
                                                           bool wantOn);

template <typename Alloc>
NOTE_EDIT_FOCUS_INTERNAL_MEM void eraseNoteEndpoint(std::vector<MidiEvent, Alloc>& flat,
                                                    uint8_t channel, uint8_t pitch, uint32_t tick,
                                                    bool wantOn);

template <typename Alloc>
NOTE_EDIT_FOCUS_INTERNAL_MEM void eraseNotePairAtBaseline(std::vector<MidiEvent, Alloc>& flat,
                                                          uint8_t channel, const NoteBaseline& bl);

template <typename Alloc>
NOTE_EDIT_FOCUS_INTERNAL_MEM MidiEvent* findNoteOnAt(std::vector<MidiEvent, Alloc>& flat,
                                                     uint8_t channel, uint8_t pitch,
                                                     uint32_t startTick);

template <typename Alloc>
NOTE_EDIT_FOCUS_INTERNAL_MEM MidiEvent* findNoteOffForOn(std::vector<MidiEvent, Alloc>& flat,
                                                         uint8_t channel, uint8_t pitch,
                                                         uint32_t startTick, uint32_t endTick);

template <typename Alloc>
NOTE_EDIT_FOCUS_INTERNAL_MEM void insertNotePair(std::vector<MidiEvent, Alloc>& flat,
                                                 uint8_t channel, const NoteBaseline& bl,
                                                 uint32_t endTick, NoteId noteId);

// --- NoteEditFocusLinearSpan (Phase 2) ---

template <typename Alloc>
NOTE_EDIT_FOCUS_INTERNAL_MEM MidiEvent* findLinearOffForNoteOnLifo(
    std::vector<MidiEvent, Alloc>& events, MidiEvent* noteOnEvent, uint8_t pitch);

template <typename Alloc>
NOTE_EDIT_FOCUS_INTERNAL_MEM MidiEvent* findPlausibleOffForNoteOn(
    std::vector<MidiEvent, Alloc>& events, const MidiEvent& noteOn, uint32_t loopLength);

template <typename Alloc>
NOTE_EDIT_FOCUS_INTERNAL_MEM MidiEvent* findNoteOnAtChannelPitchTick(
    std::vector<MidiEvent, Alloc>& events, uint8_t channel, uint8_t pitch, uint32_t tick);

template <typename Alloc>
NOTE_EDIT_FOCUS_INTERNAL_MEM MidiEvent* findNoteOnForNoteIdAtTick(
    std::vector<MidiEvent, Alloc>& events, NoteId noteId, uint8_t channel, uint8_t pitch,
    uint32_t tick);

template <typename Alloc>
NOTE_EDIT_FOCUS_INTERNAL_MEM MidiEvent* findNoteOnForNoteIdAnyChannel(
    std::vector<MidiEvent, Alloc>& events, NoteId noteId, uint8_t pitch);

// --- NoteEditFocusPreCommit (Phase 6) ---

NOTE_EDIT_FOCUS_INTERNAL_MEM EditPass makeNoteEditRow(EditActionType actionType,
                                                      EditPropertyType propertyType);

// --- NoteEditFocusDisplayProjection (Phase 7) ---

NOTE_EDIT_FOCUS_INTERNAL_MEM void sortNoteIdList(NoteIdList& ids);

NOTE_EDIT_FOCUS_INTERNAL_MEM bool displayNoteOrderBefore(const NoteUtils::DisplayNote& left,
                                                         const NoteUtils::DisplayNote& right);
