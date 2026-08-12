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
using CapturePreviewChangeIndexVec =
    std::vector<uint32_t, ExternalMemoryFirstAllocator<uint32_t>>;
using CapturePreviewNoteIndexVec =
    std::vector<uint32_t, ExternalMemoryFirstAllocator<uint32_t>>;

struct CapturePreviewNoteState {
  uint8_t channel = 0;
  uint8_t pitch = 0;
  bool open = false;
  bool wrapHeld = false;
  bool hasPreferredHeadOff = false;
  uint32_t preferredHeadOffTick = 0;
};

using CapturePreviewNoteStateVec =
    std::vector<CapturePreviewNoteState,
                ExternalMemoryFirstAllocator<CapturePreviewNoteState>>;

inline uint32_t visualBarForTick(uint32_t tick, uint32_t ticksPerBar) {
  if (ticksPerBar == 0) {
    return 0;
  }
  return tick / ticksPerBar;
}

/// True when `visualCache` is fully built and may authorize window paint by filter.
/// Partial dirtyBars==0 neighborhoods must not count — that painted sparse slices as gaps
/// (session_20260811_032235 user report after RC4).
inline bool visualCacheCoversWindow(bool visualCacheDirty, const VisualBarVec& /*dirtyBars*/,
                                    uint32_t windowStart, uint32_t windowLength,
                                    uint32_t loopLength, uint32_t /*ticksPerBar*/) {
  if (loopLength == 0 || windowLength == 0 || windowStart >= loopLength) {
    return false;
  }
  return !visualCacheDirty;
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

/// Adopt a composed display window into visualCache without claiming the whole loop is built.
/// Bars touched by adopted notes are marked clean; every other bar stays dirty until idle slices
/// backfill. visualCacheDirty stays true while any bar remains dirty (RC-E / session_162230).
template <typename NoteVec>
inline void adoptPartialVisualCacheNotes(VisualCache& cache, bool& visualCacheDirty,
                                         const NoteVec& notes, uint32_t loopLengthTicks,
                                         uint32_t ticksPerBar) {
  cache.setNotes(notes);
  ++cache.revision;

  if (loopLengthTicks == 0 || ticksPerBar == 0) {
    cache.dirtyBars.clear();
    visualCacheDirty = !notes.empty();
    return;
  }

  const uint32_t totalBars = (loopLengthTicks + ticksPerBar - 1) / ticksPerBar;
  if (totalBars == 0) {
    cache.dirtyBars.clear();
    visualCacheDirty = false;
    return;
  }

  cache.dirtyBars.assign(totalBars, 1);
  for (const NoteUtils::DisplayNote& note : notes) {
    const uint32_t endTick = note.endTick >= note.startTick ? note.endTick : note.startTick;
    const uint32_t startBar = visualBarForTick(note.startTick, ticksPerBar);
    const uint32_t endBar = visualBarForTick(endTick, ticksPerBar);
    if (startBar >= totalBars) {
      continue;
    }
    const uint32_t cappedEnd = endBar < totalBars ? endBar : totalBars - 1;
    for (uint32_t bar = startBar; bar <= cappedEnd; ++bar) {
      cache.dirtyBars[bar] = 0;
    }
  }

  visualCacheDirty = false;
  for (uint8_t flag : cache.dirtyBars) {
    if (flag != 0) {
      visualCacheDirty = true;
      break;
    }
  }
}

struct CapturePreview {
  uint32_t revision = 0;
  uint32_t replacementRevision = 0;
  DisplayNoteVec notes;
  CapturePreviewNoteStateVec noteStates;
  CapturePreviewNoteIndexVec openNoteIndices;
  CapturePreviewChangeIndexVec changedNoteIndices;
  VisualBarVec dirtyBars;

  void clear() {
    ++revision;
    ++replacementRevision;
    notes.clear();
    noteStates.clear();
    openNoteIndices.clear();
    changedNoteIndices.clear();
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
