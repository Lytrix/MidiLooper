//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

// DEC-115 (pool containment): the **native default for the projection gate's host symbol**.
//
// Every native suite that compiles a projection translation unit (`Utils/NoteUtils.cpp`,
// `Utils/DisplayWindowUtils.cpp`, `Loop/LoopVisualCache.cpp`) now odr-uses
// `MemoryMonitor::admitOperationMemory` through the inline projection admission helper in
// `include/Utils/MemoryMonitor.h`. Suites that include `MemoryMonitorNativeDeps.cpp` link the **real**
// gate (point-of-use probe, `setNativeForcedReserveDenied`); the rest take this **weak** stub, which
// admits -- exactly what the host gate does on a platform with no pool.
//
// Weak on purpose: symbol resolution decides, so no suite needs per-file wiring and no include order
// matters. On the device this file is not compiled at all (it is added to native test builds only, by
// `scripts/native_test_link.py`); the firmware's gate is the real one in `src/Utils/MemoryMonitor.cpp`.
#include "Utils/MemoryMonitor.h"

namespace MemoryMonitor {

__attribute__((weak)) bool admitOperationMemory(OperationKind, uint32_t) { return true; }

}  // namespace MemoryMonitor
