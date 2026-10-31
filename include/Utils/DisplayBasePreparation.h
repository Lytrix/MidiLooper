//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// DEC-237 Slice 2 — pure, unwired producer for the committed display base.
//
// The candidate is one generation of 4-bar coverage tiles. A tile is a construction/publication unit,
// not an execution bound; `stepTilePreparation` is the bounded execution primitive. No partially
// prepared tile set is exposed: `ready` becomes true only after every required tile completes and the
// candidate is assembled. The frame/idle-owner wiring is Slice 3 and is deliberately absent here.

#pragma once

#include <cstdint>
#include <vector>

#include "Globals.h"
#include "Loop.h"
#include "LoopContentResolution.h"
#include "Utils/CoveragePrerequisite.h"
#include "Utils/DisplayWindowUtils.h"

namespace DisplayBasePreparation {

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
  NoteUtils::DisplayNoteVec tileNotes;
  std::vector<NoteUtils::DisplayNoteVec> tiles;
  NoteUtils::DisplayNoteVec stagedNotes;
  uint32_t steps = 0;

  bool readyFor(const Key& other) const {
    return ready && key.trackIndex == other.trackIndex && key.displaySlot == other.displaySlot &&
           key.loopId == other.loopId && key.playbackRevision == other.playbackRevision &&
           key.loopLengthTicks == other.loopLengthTicks &&
           key.snapshotCurrent == other.snapshotCurrent &&
           key.publishedIdentity == other.publishedIdentity &&
           key.rangeStart == other.rangeStart && key.rangeLength == other.rangeLength;
  }
};

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
  prep.tileNotes.clear();
  prep.tiles.clear();
  prep.stagedNotes.clear();
  prep.steps = 0;
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
  return true;
}

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
  prep.active = true;
  prep.ready = false;
  if (!beginTile(prep)) {
    prep.active = false;
    return false;
  }
  return true;
}

/// Identity-preserving append for candidate assembly: a valid NoteId is unique per start tick (a
/// wrap-head/tail pair shares the id at different starts); invalid-ID rows use pitch+start. The frame
/// never sees this vector until every tile has completed.
inline void appendTileNotesByIdentity(NoteUtils::DisplayNoteVec& out,
                                      const NoteUtils::DisplayNoteVec& tile) {
  for (const NoteUtils::DisplayNote& note : tile) {
    bool present = false;
    for (const NoteUtils::DisplayNote& existing : out) {
      if (note.noteId != kInvalidNoteId) {
        if (existing.noteId == note.noteId && existing.startTick == note.startTick) {
          present = true;
          break;
        }
      } else if (existing.noteId == kInvalidNoteId && existing.note == note.note &&
                 existing.startTick == note.startTick) {
        present = true;
        break;
      }
    }
    if (!present) {
      out.push_back(note);
    }
  }
}

/// One bounded execution turn. Returns true only when the complete tile set is ready. An incomplete
/// call publishes nothing; the candidate stays private.
inline bool step(Prep& prep, const Loop& loop, size_t eventBudget) {
  if (!prep.active || prep.ready) {
    return prep.ready;
  }
  if (prep.tileIndex >= prep.tileCount) {
    return false;
  }
  const bool complete = LoopContentResolution::stepTilePreparation(
      loop.passes, prep.key.loopLengthTicks, prep.lookupStart, prep.lookupLength, eventBudget,
      prep.cursor);
  ++prep.steps;
  if (!complete) {
    return false;
  }
  // The tile's own derive is complete. Keep the display's wrap-held rows, then clip the complete note
  // geometry to this tile (a closure row may start before it).
  prep.tileNotes.assign(prep.cursor.notes.begin(), prep.cursor.notes.end());
  loop.appendOverdubPassDisplayNotes(prep.tileNotes);
  DisplayWindowUtils::filterDisplayNotesByWindowInclusionInPlace(
      prep.tileNotes, prep.tileStart, prep.tileLength, prep.key.loopLengthTicks);
  prep.tiles[prep.tileIndex] = prep.tileNotes;

  if (static_cast<uint16_t>(prep.tileIndex + 1) < prep.tileCount) {
    ++prep.tileIndex;
    if (!beginTile(prep)) {
      prep.active = false;
      return false;
    }
    return false;
  }

  // Every required tile is complete: assemble once, expose atomically.
  prep.stagedNotes.clear();
  for (const NoteUtils::DisplayNoteVec& tile : prep.tiles) {
    appendTileNotesByIdentity(prep.stagedNotes, tile);
  }
  prep.active = false;
  prep.ready = true;
  return true;
}

/// Drop the candidate without exposing or rebuilding anything. The caller owns any storage reuse.
inline void cancel(Prep& prep) { resetPrep(prep); }

}  // namespace DisplayBasePreparation
