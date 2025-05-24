#pragma once
#include "LooperState.h"

class StorageManager {
public:
    static bool saveState(const LooperState& state);
    static bool loadState(LooperState& state);
}; 