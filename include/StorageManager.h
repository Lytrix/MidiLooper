//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once
#include "LooperState.h"

/**
 * @class StorageManager
 * @brief Manages persistent saving and loading of the looper state to non-volatile storage.
 *
 * Provides static methods to serialize the current LooperState to external memory (e.g., SD card
 * or flash) and to reload it on startup. saveState() writes the state and returns true on success;
 * loadState() restores a saved state and returns true on success.
 */
class StorageManager {
public:
    static bool saveState(const LooperState& state);
    static bool loadState(LooperState& state);
}; 