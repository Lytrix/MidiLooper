//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/PerformanceMonitor.h"
#include "Logger.h"

namespace PerformanceMonitor {

// Global performance monitor instance
#if defined(__IMXRT1062__)
DMAMEM PerformanceMonitor globalPerformanceMonitor;
#else
PerformanceMonitor globalPerformanceMonitor;
#endif

} // namespace PerformanceMonitor 