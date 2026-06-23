//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once
#include <cstdint>
#include "LooperState.h"

/**
 * @class StorageManager
 * @brief Manages persistent saving and loading of the looper state to non-volatile storage.
 *
 * Provides static methods to serialize the current LooperState to external memory (e.g., SD card
 * or flash) and to reload it on startup. Runtime callers should request deferred saves via
 * requestDeferredSaveState(); saveState() drains the deferred writer synchronously (maintenance /
 * explicit flush only — not for hot paths).
 */
class StorageManager {
public:
    static bool saveState(const LooperState& state);
    static bool loadState(LooperState& state);
    static void requestDeferredSaveState(const LooperState& state, uint32_t admissionHeap = UINT32_MAX);
    static void processDeferredSaveState(const LooperState& state);
    static bool isDeferredSaveActive();
    static void requestUrgentEditSave();
    static void processEditAutosave(const LooperState& state);
}; 