//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

#include "EditSessionAction.h"
#include "EditPass.h"
#include "MidiEvent.h"
#include "NoteEditFocus.h"

/// Edit session action apply — sole live-store writer for NOTE_EDIT geometry this tick.
/// Runs ordered actions then boundary split (D10). Does not call normalizeAll.
/// Caller must invoke track.invalidateCaches() after apply for playback audition refresh.
void applyEditSessionActions(const EditSessionActions& actions, MidiEventVec& liveStore,
                             NoteEditFocus& focus, uint8_t channel, uint32_t loopLength,
                             EditPassVec* applyOwnedRows = nullptr);

/// D10 boundary split sub-step — earlier off moves to later on tick − 1 when they share a tick.
void applyBoundarySplitForEditSession(MidiEventVec& liveStore, uint8_t channel);
