//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

#include "Globals.h"
#include "Loop.h"
#include "MidiEvent.h"
#include "NoteEditSessionState.h"
#include "Utils/NoteEditDisplaySnapshot.h"
#include "Utils/NoteUtils.h"
#include <vector>

class Track;
struct EditorSelection;

namespace DisplayManagerInternal {

/// Extra bars gathered beyond the paint window so auto-follow does not rebuild every tick.
constexpr uint8_t kWindowedGatherMarginBars = 2;

extern SessionMidiEventVec liveDisplayEventBuffer;

bool shouldAvoidFullVisualRebuild(const Loop& loop, uint32_t loopLength);
bool shouldDeferHeavyDisplayRebuild();

void rebuildDisplayNotesInWindow(Loop& mutLoop, const Loop& loop, uint32_t loopLength,
                                 uint32_t windowStart, uint32_t windowLength,
                                 SessionMidiEventVec& eventBuffer,
                                 NoteUtils::DisplayNoteVec& outNotes, bool includeActiveCapture);

uint8_t resolveTrackIndex(const Track& track);

/// `allowWrapContinuation`: false during growing live record (no loop wrap yet).
void applyCapturePlayheadTails(const CapturePreview& preview, uint32_t loopLength,
                               uint32_t closeTick, size_t captureRegionStart,
                               NoteUtils::DisplayNoteVec& notes, bool allowWrapContinuation = true);
void applyLiveOpenTails(const std::vector<NoteUtils::OpenNoteOn>& openNotes,
                        const SessionMidiEventVec& midiEvents, uint32_t loopLength,
                        uint32_t closeTick, NoteUtils::DisplayNoteVec& notes,
                        bool extendHeldNotesToPlayhead);

uint32_t resolveBracketDisplayTick(uint32_t loopStartTick, uint32_t loopLength);
int resolveDrawHighlightIndex(const NoteUtils::DisplayNoteVec& notes,
                              const EditorSelection& selection, uint32_t loopStartTick,
                              uint32_t loopLength, bool windowRelativeTicks,
                              uint32_t windowStartTick, uint32_t bracketDisplayTick);

}  // namespace DisplayManagerInternal
