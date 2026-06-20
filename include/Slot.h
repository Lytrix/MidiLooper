//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include "LoopPasses.h"

/// Playable trigger address — holds a stable LoopId ref, not MIDI storage.
struct Slot {
  LoopId loopId = kInvalidLoopId;
};
