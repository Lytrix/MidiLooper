//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <vector>

#include "Utils/NoteUtils.h"
#include "Utils/ExternalMemoryFirstAllocator.h"

using VisualBarVec = std::vector<uint8_t>;
using DisplayNoteVec =
    std::vector<NoteUtils::DisplayNote, ExternalMemoryFirstAllocator<NoteUtils::DisplayNote>>;

inline uint32_t visualBarForTick(uint32_t tick, uint32_t ticksPerBar) {
  if (ticksPerBar == 0) {
    return 0;
  }
  return tick / ticksPerBar;
}

struct VisualCache {
  uint32_t revision = 0;
  DisplayNoteVec notes;
  VisualBarVec dirtyBars;

  void clear() {
    revision = 0;
    notes.clear();
    dirtyBars.clear();
  }

  template <typename Allocator>
  void setNotes(const std::vector<NoteUtils::DisplayNote, Allocator>& inNotes) {
    notes.assign(inNotes.begin(), inNotes.end());
  }

  void markBarDirty(uint32_t bar) {
    if (dirtyBars.size() <= bar) {
      dirtyBars.resize(bar + 1, 0);
    }
    dirtyBars[bar] = 1;
  }
};

struct CapturePreview {
  uint32_t revision = 0;
  DisplayNoteVec notes;
  VisualBarVec dirtyBars;

  void clear() {
    revision = 0;
    notes.clear();
    dirtyBars.clear();
  }

  void markBarDirty(uint32_t bar) {
    if (dirtyBars.size() <= bar) {
      dirtyBars.resize(bar + 1, 0);
    }
    dirtyBars[bar] = 1;
  }
};

struct VisualCacheDelta {
  bool replaceAll = false;
  DisplayNoteVec notes;
  VisualBarVec dirtyBars;

  void clear() {
    replaceAll = false;
    notes.clear();
    dirtyBars.clear();
  }

  void applyTo(VisualCache& cache) const {
    if (replaceAll) {
      cache.setNotes(notes);
    }
    for (size_t i = 0; i < dirtyBars.size(); ++i) {
      if (dirtyBars[i] == 0) {
        continue;
      }
      cache.markBarDirty(static_cast<uint32_t>(i));
    }
    ++cache.revision;
  }
};
