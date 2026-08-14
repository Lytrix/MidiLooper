//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// DEC-037 — query-time effective musical state. Native prototype; not on the MIDI path.

#pragma once

#include "EditPass.h"
#include "LoopPasses.h"
#include "MidiEvent.h"
#include "Utils/NoteUtils.h"

#include <cstdint>
#include <vector>

struct ResolutionCostCounters {
  uint32_t eventsInHistory = 0;
  uint32_t passesInHistory = 0;
  uint32_t eventsInQueryWindow = 0;
  uint32_t candidateEvents = 0;
  uint32_t resolutionOperations = 0;
  uint64_t elapsedMicros = 0;
};

struct SoundingNote {
  uint8_t channel = 0;
  uint8_t pitch = 0;
  NoteId noteId = kInvalidNoteId;
  uint32_t onTick = 0;
};

using SoundingNoteVec = std::vector<SoundingNote>;

// Stage 1 vocabulary pin (DEC-037): three roles, no fourth synonym.
// RawMidiEvent  — MIDI shape stored on capture passes (today: MidiEvent).
// EditAction    — the EditPass row (EditActionType + payload).
// ResolvedEvent — same MIDI shape after active-set + edit apply.
using RawMidiEvent = MidiEvent;
using ResolvedEvent = MidiEvent;
using EditAction = EditPass;

/// Owner of resolveState / resolveWindow. resolveNotes is a derived consumer.
struct LoopContentResolution {
  static void resolveWindow(const LoopPasses& passes, uint32_t loopLengthTicks,
                            uint32_t windowStart, uint32_t windowLength, SessionMidiEventVec& out,
                            ResolutionCostCounters* counters = nullptr);

  static void resolveState(const LoopPasses& passes, uint32_t loopLengthTicks, uint32_t tick,
                           SoundingNoteVec& out, ResolutionCostCounters* counters = nullptr);

  static void resolveNotes(const LoopPasses& passes, uint32_t loopLengthTicks, uint32_t windowStart,
                           uint32_t windowLength, NoteUtils::DisplayNoteVec& out,
                           ResolutionCostCounters* counters = nullptr);
};
