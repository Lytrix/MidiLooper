//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <vector>
#include <cstdint>
#include "NoteEditGeometryApplyWrap.h"
#include "MidiEvent.h"
#include "Utils/NoteUtils.h"
#include "EditManager.h"
#if defined(PIO_UNIT_TEST_NATIVE)
class Track;
#else
#include "Track.h"
#endif

namespace NoteEditGeometryApply {

enum class NoteEditChangeKind : uint8_t { Move, Length, Pitch };

bool applyNoteEditChange(Track& track, EditManager& manager, NoteEditChangeKind kind,
                         const NoteUtils::DisplayNote& currentNote, uint32_t targetTick,
                         int delta, uint32_t targetEndTick, uint8_t currentPitch,
                         uint8_t newPitch, uint32_t& inOutStart, uint32_t& inOutEnd,
                         bool refreshPlaybackPreview = true);

bool moveNoteWithOverlapHandling(Track& track, EditManager& manager,
                                 const NoteUtils::DisplayNote& currentNote, uint32_t targetTick,
                                 int delta, bool refreshPlaybackPreview = true);

void changeLengthWithOverlapHandling(Track& track, EditManager& manager,
                                     const NoteUtils::DisplayNote& currentNote,
                                     uint32_t targetEndTick,
                                     bool refreshPlaybackPreview = true);

bool applyPitchChange(Track& track, EditManager& manager,
                      uint8_t currentNoteValue, uint8_t newNoteValue,
                      uint32_t& noteStart, uint32_t& noteEnd,
                      bool refreshPlaybackPreview = true);

bool linearStorageSpansOverlap(uint32_t start1, uint32_t end1, uint32_t start2, uint32_t end2);

template <typename Alloc>
MidiEvent* findCorrespondingNoteOff(std::vector<MidiEvent, Alloc>& midiEvents, MidiEvent* noteOnEvent,
                                    uint8_t pitch, std::uint32_t startTick, std::uint32_t endTick);

MidiEvent* findNoteOffPairedAt(MidiEventVec& midiEvents, uint8_t pitch, uint32_t startTick,
                               uint32_t endTick);

MidiEvent* findNoteOffForNoteOnAtStart(MidiEventVec& midiEvents, uint8_t channel,
                                       uint8_t pitch, uint32_t startTick, NoteId noteId = kInvalidNoteId,
                                       uint32_t loopLength = 0);

MidiEvent* resolveNoteOffForEditSpan(MidiEventVec& midiEvents, MidiEvent* noteOnEvent,
                                     uint8_t channel, uint8_t pitch, uint32_t startTick,
                                     uint32_t displayEndTick, uint32_t loopLength);

bool isOpenTailNoteAtLoopEnd(const MidiEventVec& midiEvents, uint8_t channel, uint8_t pitch,
                             uint32_t startTick, uint32_t displayEndTick, uint32_t loopLength);

}  // namespace NoteEditGeometryApply
