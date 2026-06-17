//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

/// Committed display cache for a loop (published epochs only). M1 placeholder — filled in M4.
struct VisualCache {
  uint32_t revision = 0;

  void clear() { revision = 0; }
};

/// Transient overlay for live capture strokes. Never saved or undo'd. M1 placeholder — filled in M4.
struct CapturePreview {
  uint32_t revision = 0;

  void clear() { revision = 0; }
};

/// Staged display delta produced during Seal; applied on Publish. M1 placeholder — filled in M4.
struct VisualCacheDelta {
  void clear() {}

  void applyTo(VisualCache& cache) const { ++cache.revision; }
};
