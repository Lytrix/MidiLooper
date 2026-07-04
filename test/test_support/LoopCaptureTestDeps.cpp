//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Include once in native tests that link Loop.cpp (capture incremental sanity symbols).

#pragma once

#include "Globals.h"

uint32_t noteMinLengthTicks = Config::DEFAULT_NOTE_MIN_LENGTH_TICKS;
bool noteMinLengthRemoveEnabled = Config::DEFAULT_NOTE_MIN_LENGTH_REMOVE_ENABLED;

#include "../../src/Utils/CaptureIncrementalSanity.cpp"
