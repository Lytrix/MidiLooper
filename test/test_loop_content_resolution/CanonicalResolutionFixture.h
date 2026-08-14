//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// DEC-037 Stage 0 — canonical stress fixture for LoopContentResolution.

#pragma once

#include <cstdint>

#include "EditPass.h"
#include "Globals.h"
#include "LoopContentResolution.h"
#include "LoopPasses.h"
#include "MidiEvent.h"

struct CanonicalResolutionFixture {
  LoopPasses passes;
  uint32_t loopLengthTicks = 0;
  NoteId wrapNoteId = kInvalidNoteId;
  NoteId overlapHostNoteId = kInvalidNoteId;
  NoteId overlapIncomingNoteId = kInvalidNoteId;
  NoteId deletedNoteId = kInvalidNoteId;
  NoteId shortenedNoteId = kInvalidNoteId;
  NoteId movedNoteId = kInvalidNoteId;
};

constexpr uint32_t kCanonicalBars = 64;
constexpr uint32_t kCanonicalOverdubPasses = 44;
constexpr uint32_t kCanonicalQueryWindowBars = 16;

CanonicalResolutionFixture buildCanonicalResolutionFixture();
void countWindowEvents(const SessionMidiEventVec& events, uint32_t loopLengthTicks,
                       uint32_t windowStart, uint32_t windowLength, ResolutionCostCounters& counters);
