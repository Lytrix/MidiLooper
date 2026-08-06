//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

#include "Globals.h"
#include "Loop.h"
#include "MidiEvent.h"
#include "Utils/NoteUtils.h"
#include <vector>

class Track;

namespace DisplayManagerInternal {

/// Extra bars gathered beyond the paint window so auto-follow does not rebuild every tick.
constexpr uint8_t kWindowedGatherMarginBars = 2;

extern SessionMidiEventVec liveDisplayEventBuffer;

bool shouldAvoidFullVisualRebuild(const Loop& loop, uint32_t loopLength);
bool shouldDeferHeavyDisplayRebuild();

void rebuildDisplayNotesInWindow(Loop& mutLoop, const Loop& loop, uint32_t loopLength,
                                 uint32_t windowStart, uint32_t windowLength,
                                 SessionMidiEventVec& eventBuffer,
                                 NoteUtils::DisplayNoteVec& outNotes);

uint8_t resolveTrackIndex(const Track& track);

std::vector<NoteUtils::OpenNoteOn> findCaptureOpenNoteOnsFromPreview(const Loop& loop);
void copySortedCaptureEvents(const Loop& loop, SessionMidiEventVec& out);
void applyCapturePlayheadTails(const std::vector<NoteUtils::OpenNoteOn>& captureOpens,
                               const SessionMidiEventVec& captureEvents, uint32_t loopLength,
                               uint32_t closeTick, size_t captureRegionStart,
                               NoteUtils::DisplayNoteVec& notes);
void applyRecordingPreviewOpenTails(NoteUtils::DisplayNoteVec& notes, uint32_t loopLength,
                                    uint32_t closeTick);
void applyLiveOpenTails(const std::vector<NoteUtils::OpenNoteOn>& openNotes,
                        const SessionMidiEventVec& midiEvents, uint32_t loopLength,
                        uint32_t closeTick, NoteUtils::DisplayNoteVec& notes,
                        bool extendHeldNotesToPlayhead);

}  // namespace DisplayManagerInternal
