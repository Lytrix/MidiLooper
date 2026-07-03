//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <unordered_set>

#include "MidiEvent.h"

namespace LoopTickNormalize {

constexpr uint32_t kDefaultWrapWindowTicks = 768;

/// Pass-2 synth promotion only for short open tails near loop end (matches min note gate).
constexpr uint32_t kSynthOpenTailMaxSpanTicks = 49;

struct NormalizeResult {
  size_t wrapPairsMerged = 0;
  size_t synthOffsPromoted = 0;
  size_t openTailsClosed = 0;
};

struct NormalizeOptions {
  uint32_t wrapWindowTicks = kDefaultWrapWindowTicks;
  /// Micro edit latch: false. Macro commit / capture stop: true.
  bool closeOpenTails = false;
  uint32_t openTailCloseTick = UINT32_MAX;
};

/// What events participate in conversion passes (one engine; scope differs per call site).
struct NormalizeScope {
  enum class Kind { EntireStore, TickRange, NoteIds };

  Kind kind = Kind::EntireStore;
  uint32_t rangeStart = 0;
  uint32_t rangeEnd = 0;
  std::unordered_set<NoteId> closureNoteIds;

  static NormalizeScope entireStore();
  static NormalizeScope tickRange(uint32_t rangeStart, uint32_t rangeEnd,
                                uint32_t wrapWindowTicks = kDefaultWrapWindowTicks);
  static NormalizeScope noteIds(std::unordered_set<NoteId> ids);
};

/// Single conversion entry point — micro/macro/capture differ only in scope and options.
NormalizeResult normalize(MidiEventVec& events, uint32_t loopLength, NormalizeScope scope,
                          NormalizeOptions options = {});

/// Legacy tick-range scope (capture wrap window, unit tests). Same as normalize(tickRange(...)).
NormalizeResult normalizeWindow(MidiEventVec& events, uint32_t loopLength, uint32_t rangeStart,
                                uint32_t rangeEnd, uint32_t wrapWindowTicks = kDefaultWrapWindowTicks,
                                uint32_t openTailCloseTick = UINT32_MAX);

/// Full-store scope with open-tail closing (macro commit, loop length change).
NormalizeResult normalizeAll(MidiEventVec& events, uint32_t loopLength,
                             uint32_t wrapWindowTicks = kDefaultWrapWindowTicks,
                             uint32_t openTailCloseTick = UINT32_MAX);

}  // namespace LoopTickNormalize
