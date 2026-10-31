//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// DEC-237 Slice 2 — pure, unwired producer for the committed display base.
//
// The candidate is one generation of 4-bar coverage tiles. A tile is a construction/publication unit,
// not an execution bound; `stepTilePreparation` is the bounded execution primitive. No partially
// prepared candidate is exposed: `ready` becomes true only after every body tile completes, every
// active pass's wrap-held rows are staged, and the candidate is assembled. The frame/idle-owner wiring
// is Slice 3 and is deliberately absent here.
//
// One bounded unit per turn. The three units are:
//   * one tile's windowed traversal (event and inspected-chunk budgets, indexed per-pass merge);
//   * one active pass's wrap-held staging (one pass layer, merged into the candidate by pitch+start);
//   * the last unit that completes the candidate (no content-sized assembly step).
//
// Body tiles are applied before wrap rows, matching the existing `resolveDisplayNotesInWindow` +
// `appendOverdubPassDisplayNotes` composition: a wrap-held row replaces a non-wrap body row at the same
// pitch+start, never the other way around.

#pragma once

#include <cstdint>
#include <vector>

#include "Globals.h"
#include "Loop.h"
#include "LoopContentResolution.h"
#include "Utils/CoveragePrerequisite.h"
#include "Utils/DisplayWindowUtils.h"

namespace DisplayBasePreparation {

/// Deterministic work bound for one `step()` call. These are native-fixture claims, not host wall-clock
/// measurements; the device microsecond bound is Slice 4's acceptance gate.
constexpr uint32_t kMaxChunksInspectedPerStep = 64;
constexpr uint32_t kMaxEventsAppendedPerStep = 64;
constexpr uint32_t kMaxPassLayersProjectedPerStep = 1;
constexpr uint32_t kMaxWrapRowsStagedPerStep = 256;
/// Two indexed merges per body note (candidate + wrap-prefill) plus the wrap upserts.
constexpr uint32_t kMaxMergeComparisonsPerStep = 512;

#if defined(__IMXRT1062__)
#define DISPLAY_BASE_PREP_FLASH __attribute__((noinline, section(".flashmem")))
#else
#define DISPLAY_BASE_PREP_FLASH
#endif

struct Key {
  uint8_t trackIndex = 255;
  uint8_t displaySlot = 255;
  LoopId loopId = kInvalidLoopId;
  uint32_t playbackRevision = 0;
  uint32_t loopLengthTicks = 0;
  bool snapshotCurrent = false;
  const void* publishedIdentity = nullptr;
  uint32_t rangeStart = 0;
  uint32_t rangeLength = 0;
};

inline bool keyMatches(const Key& a, const Key& b) {
  return a.trackIndex == b.trackIndex && a.displaySlot == b.displaySlot && a.loopId == b.loopId &&
         a.playbackRevision == b.playbackRevision && a.loopLengthTicks == b.loopLengthTicks &&
         a.snapshotCurrent == b.snapshotCurrent && a.publishedIdentity == b.publishedIdentity &&
         a.rangeStart == b.rangeStart && a.rangeLength == b.rangeLength;
}

struct StepWork {
  uint32_t chunksInspected = 0;
  uint32_t eventsAppended = 0;
  uint32_t passLayersProjected = 0;
  uint32_t wrapRowsStaged = 0;
  uint32_t mergeComparisons = 0;
};

enum class Phase : uint8_t { Tiles, WrapStaging, Ready };

struct Prep {
  bool active = false;
  bool ready = false;
  Key key;
  uint32_t closureTicks = 0;
  uint16_t tileCount = 0;
  uint16_t tileIndex = 0;
  uint32_t tileStart = 0;
  uint32_t tileLength = 0;
  uint32_t lookupStart = 0;
  uint32_t lookupLength = 0;
  LoopContentResolution::TilePrepCursor cursor;
  /// Per-tile pass-local upsert (bare NoteId, else pitch+start) — the resolver's identity.
  DisplayNoteUpsertIndex tileNoteIndex;
  /// Cross-tile body assembly (NoteId+start, else pitch+start).
  DisplayNoteUpsertIndex candidateIndex;
  /// Pitch+start table: body rows prefilled, wrap rows replace the first non-wrap row.
  DisplayNoteUpsertIndex wrapIndex;
  NoteUtils::DisplayNoteVec tileNotes;
  NoteUtils::DisplayNoteVec wrapScratch;
  std::vector<NoteUtils::DisplayNoteVec> tiles;
  NoteUtils::DisplayNoteVec stagedNotes;
  Phase phase = Phase::Tiles;
  uint16_t wrapPassIndex = 0;
  uint16_t wrapPassCount = 0;
  /// Active pass indices that can produce a wrap-held row (or every active pass when an active Note
  /// edit may move a note across the boundary). Built once at `start`; the wrap phase iterates this.
  std::vector<uint16_t> wrapPasses;
  uint32_t steps = 0;
  StepWork lastWork;
  StepWork maxWork;

  bool readyFor(const Key& other) const { return ready && keyMatches(key, other); }
};

inline void recordStep(Prep& prep, const StepWork& work) {
  ++prep.steps;
  prep.lastWork = work;
  auto maxOf = [](uint32_t& slot, uint32_t value) {
    if (value > slot) {
      slot = value;
    }
  };
  maxOf(prep.maxWork.chunksInspected, work.chunksInspected);
  maxOf(prep.maxWork.eventsAppended, work.eventsAppended);
  maxOf(prep.maxWork.passLayersProjected, work.passLayersProjected);
  maxOf(prep.maxWork.wrapRowsStaged, work.wrapRowsStaged);
  maxOf(prep.maxWork.mergeComparisons, work.mergeComparisons);
}

inline void resetPrep(Prep& prep) {
  prep.active = false;
  prep.ready = false;
  prep.tileCount = 0;
  prep.tileIndex = 0;
  prep.tileStart = 0;
  prep.tileLength = 0;
  prep.lookupStart = 0;
  prep.lookupLength = 0;
  prep.cursor = LoopContentResolution::TilePrepCursor{};
  prep.tileNoteIndex.clear();
  prep.candidateIndex.clear();
  prep.wrapIndex.clear();
  prep.tileNotes.clear();
  prep.wrapScratch.clear();
  prep.tiles.clear();
  prep.stagedNotes.clear();
  prep.phase = Phase::Tiles;
  prep.wrapPassIndex = 0;
  prep.wrapPassCount = 0;
  prep.wrapPasses.clear();
  prep.steps = 0;
  prep.lastWork = StepWork{};
  prep.maxWork = StepWork{};
}

inline bool beginTile(Prep& prep) {
  const uint32_t tileUnit = CoveragePrerequisite::tileLengthTicks();
  if (tileUnit == 0 || prep.tileIndex >= prep.tileCount) {
    return false;
  }
  prep.tileStart = prep.key.rangeStart + static_cast<uint32_t>(prep.tileIndex) * tileUnit;
  if (prep.tileStart >= prep.key.loopLengthTicks) {
    return false;
  }
  prep.tileLength = prep.key.loopLengthTicks - prep.tileStart;
  if (prep.tileLength > tileUnit) {
    prep.tileLength = tileUnit;
  }
  CoveragePrerequisite::closureWindowAround(prep.tileStart, prep.tileLength, prep.closureTicks,
                                            prep.key.loopLengthTicks, prep.lookupStart,
                                            prep.lookupLength);
  prep.cursor = LoopContentResolution::TilePrepCursor{};
  prep.tileNotes.clear();
  prep.tileNoteIndex.clear();
  return true;
}

/// Flash-resident wrappers used by the firmware service. The producer's inline tile/index code must not
/// be instantiated in an ITCM TU; these definitions live in `LoopContentResolution.cpp` (flash).
DISPLAY_BASE_PREP_FLASH Prep& constructFlash(void* storage);
DISPLAY_BASE_PREP_FLASH bool startFlash(Prep& prep, const Loop& loop, const Key& key,
                                        uint32_t closureTicks);
DISPLAY_BASE_PREP_FLASH bool stepFlash(Prep& prep, const Loop& loop, size_t eventBudget);
DISPLAY_BASE_PREP_FLASH void cancelFlash(Prep& prep);

/// Start a candidate for `key` over `key.rangeStart..key.rangeLength` in 4-bar tiles. `closureTicks`
/// is the committed maximum note span used to widen each tile's lookup; the caller owns that value.
/// Returns false for an empty loop, an empty range, or a range beyond the 64-tile envelope.
inline bool start(Prep& prep, const Loop& loop, const Key& key, uint32_t closureTicks) {
  resetPrep(prep);
  if (loop.loopLengthTicks == 0 || key.loopLengthTicks == 0 || key.rangeLength == 0) {
    return false;
  }
  const uint32_t tileUnit = CoveragePrerequisite::tileLengthTicks();
  if (tileUnit == 0) {
    return false;
  }
  const uint32_t count = (key.rangeLength + tileUnit - 1u) / tileUnit;
  if (count == 0 || count > CoveragePrerequisite::kMaxTiles) {
    return false;
  }
  prep.key = key;
  prep.closureTicks = closureTicks;
  prep.tileCount = static_cast<uint16_t>(count);
  prep.tiles.assign(prep.tileCount, NoteUtils::DisplayNoteVec{});
  // Skip wrap staging for passes that cannot produce a wrap-held row. An active Note edit may move a
  // note across the boundary, so an edited slot stages every active pass (the conservative fallback).
  bool editMayMoveBoundary = false;
  for (const EditPass& editPass : loop.passes.editPasses) {
    if (editPass.state == EditPassState::Active && editPass.passType == EditPassType::Note) {
      editMayMoveBoundary = true;
      break;
    }
  }
  prep.wrapPasses.clear();
  const uint16_t activePasses = loop.activeCommittedPassCount();
  for (uint16_t passIndex = 0; passIndex < activePasses; ++passIndex) {
    if (editMayMoveBoundary || loop.activeCommittedPassHasWrapContent(passIndex, closureTicks)) {
      prep.wrapPasses.push_back(passIndex);
    }
  }
  prep.wrapPassCount = static_cast<uint16_t>(prep.wrapPasses.size());
  prep.phase = Phase::Tiles;
  prep.active = true;
  prep.ready = false;
  if (!beginTile(prep)) {
    prep.active = false;
    return false;
  }
  return true;
}

/// One bounded execution turn. Returns true only when the complete candidate is ready. An incomplete
/// call publishes nothing; the candidate stays private.
inline bool step(Prep& prep, const Loop& loop, size_t eventBudget) {
  if (!prep.active || prep.ready) {
    return prep.ready;
  }
  StepWork work{};
  switch (prep.phase) {
    case Phase::Tiles: {
      const size_t indexBefore = prep.tileNoteIndex.comparisons();
      size_t chunks = 0;
      size_t events = 0;
      const bool complete = LoopContentResolution::stepTilePreparation(
          loop.passes, prep.key.loopLengthTicks, prep.lookupStart, prep.lookupLength, eventBudget,
          prep.cursor, &prep.tileNoteIndex, kMaxChunksInspectedPerStep, &chunks, &events);
      work.chunksInspected = static_cast<uint32_t>(chunks);
      work.eventsAppended = static_cast<uint32_t>(events);
      work.mergeComparisons = static_cast<uint32_t>(prep.tileNoteIndex.comparisons() - indexBefore);
      if (!complete) {
        recordStep(prep, work);
        return false;
      }

      // The tile's own derive is complete. Clip the complete note geometry to this tile (a closure row
      // may start before it), then assemble it into the candidate under the cross-tile identity.
      prep.tileNotes.assign(prep.cursor.notes.begin(), prep.cursor.notes.end());
      DisplayWindowUtils::filterDisplayNotesByWindowInclusionInPlace(
          prep.tileNotes, prep.tileStart, prep.tileLength, prep.key.loopLengthTicks);
      const size_t candidateBefore = prep.candidateIndex.comparisons();
      const size_t wrapBefore = prep.wrapIndex.comparisons();
      for (const NoteUtils::DisplayNote& note : prep.tileNotes) {
        if (prep.candidateIndex.insertIfAbsent(
                prep.stagedNotes, note, DisplayNoteUpsertIndex::KeyMode::NoteIdAndStart)) {
          prep.wrapIndex.indexExisting(prep.stagedNotes,
                                       static_cast<uint32_t>(prep.stagedNotes.size() - 1),
                                       DisplayNoteUpsertIndex::KeyMode::PitchStart);
        }
      }
      work.mergeComparisons +=
          static_cast<uint32_t>((prep.candidateIndex.comparisons() - candidateBefore) +
                                (prep.wrapIndex.comparisons() - wrapBefore));
      prep.tiles[prep.tileIndex] = prep.tileNotes;
      ++prep.tileIndex;
      if (prep.tileIndex < prep.tileCount) {
        if (!beginTile(prep)) {
          prep.active = false;
          return false;
        }
        recordStep(prep, work);
        return false;
      }
      if (prep.wrapPassCount == 0) {
        prep.phase = Phase::Ready;
        prep.active = false;
        prep.ready = true;
        recordStep(prep, work);
        return true;
      }
      prep.phase = Phase::WrapStaging;
      prep.wrapPassIndex = 0;
      recordStep(prep, work);
      return false;
    }
    case Phase::WrapStaging: {
      prep.wrapScratch.clear();
      const size_t wrapBefore = prep.wrapIndex.comparisons();
      const uint16_t passIndex = prep.wrapPasses[prep.wrapPassIndex];
      if (!loop.appendOverdubWrapNotesForPass(passIndex, prep.wrapScratch)) {
        // The active pass inventory changed under the candidate. Slice 3 owns cancellation/admission;
        // here the candidate simply stops being publishable.
        prep.active = false;
        prep.ready = false;
        return false;
      }
      work.passLayersProjected = 1;
      work.wrapRowsStaged = static_cast<uint32_t>(prep.wrapScratch.size());
      for (const NoteUtils::DisplayNote& note : prep.wrapScratch) {
        if (DisplayWindowUtils::noteIntersectsWindow(note.startTick, note.endTick,
                                                     prep.key.rangeStart, prep.key.rangeLength,
                                                     prep.key.loopLengthTicks)) {
          prep.wrapIndex.upsert(prep.stagedNotes, note,
                                DisplayNoteUpsertIndex::KeyMode::PitchStart,
                                prep.key.loopLengthTicks);
        }
      }
      work.mergeComparisons = static_cast<uint32_t>(prep.wrapIndex.comparisons() - wrapBefore);
      ++prep.wrapPassIndex;
      if (prep.wrapPassIndex >= prep.wrapPassCount) {
        prep.phase = Phase::Ready;
        prep.active = false;
        prep.ready = true;
        recordStep(prep, work);
        return true;
      }
      recordStep(prep, work);
      return false;
    }
    case Phase::Ready:
      return true;
  }
  return false;
}

/// Drop the candidate without exposing or rebuilding anything. The caller owns any storage reuse.
inline void cancel(Prep& prep) { resetPrep(prep); }

}  // namespace DisplayBasePreparation
