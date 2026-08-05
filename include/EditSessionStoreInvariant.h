//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

#include "MidiEvent.h"
#include "NoteEditFocus.h"
#include "Utils/LoopEventValidation.h"

struct EditSessionStoreInvariantResult {
  bool passed = true;
  LoopEventValidation::LoopEventCheck firstFailure =
      LoopEventValidation::LoopEventCheck::NoteOnInLoopRange;
};

/// Pure verifier for edit-session stores (native tests and post-apply checks).
EditSessionStoreInvariantResult verifyEditSessionStoreInvariant(const MidiEventVec& events,
                                                                uint32_t loopLength,
                                                                uint8_t channel);

/// Apply-owned repair: close tagged orphan note-ons, then remove LIFO duplicate ons/offs.
void enforceEditSessionStoreInvariant(MidiEventVec& liveStore, const NoteEditFocus& focus,
                                      uint8_t channel, uint32_t loopLength);
