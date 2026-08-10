//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "EditPass.h"
#include "MidiEvent.h"
#include "NoteEditSessionState.h"
#include "Utils/ExternalMemoryFirstAllocator.h"
#include "Utils/InternalHeapFirstAllocator.h"
#include "Utils/NoteEditMem.h"
#include "Utils/NoteUtils.h"

class NoteEditCurrentState;

template <typename T>
using ExternalMemoryUnorderedMapAllocator = ExternalMemoryFirstAllocator<std::pair<const NoteId, T>>;

struct NoteBaseline {
  uint8_t pitch = 0;
  uint8_t velocity = 64;
  uint32_t startTick = 0;
  uint32_t endTick = 0;
};

struct NoteIdHash {
  size_t operator()(NoteId id) const noexcept { return std::hash<NoteId>{}(id); }
};

using BaselineMap = std::unordered_map<NoteId, NoteBaseline, NoteIdHash, std::equal_to<NoteId>,
                                       ExternalMemoryUnorderedMapAllocator<NoteBaseline>>;

enum class OverlapNoteStoreState : uint8_t { Visible, Hidden, Shortened };

struct OverlapNote {
  NoteId noteId = kInvalidNoteId;
  NoteBaseline baseline{};
  OverlapNoteStoreState state = OverlapNoteStoreState::Visible;
  uint32_t shortenedEndTick = 0;
  bool innerUnderMovingNote = false;
  /// Hidden overlap **DeleteNote** already saved in **Edits[]** — skip re-emit; keep for pitch lane restore.
  bool preCommitEmitted = false;
};

/// Scratch payload for re-inserting a hidden/shortened overlap note into session store events.
struct OverlapNoteRestore {
  NoteId noteId = kInvalidNoteId;
  uint8_t pitch = 0;
  uint8_t velocity = 64;
  uint32_t startTick = 0;
  uint32_t endTick = 0;
  uint32_t originalLength = 0;
  bool wasShortened = false;
  uint32_t shortenedToTick = 0;
};

using OverlapNoteMap = std::unordered_map<NoteId, OverlapNote, NoteIdHash, std::equal_to<NoteId>,
                                          ExternalMemoryUnorderedMapAllocator<OverlapNote>>;

/// Sorted, unique NoteId list. Reuses NoteGeometryResolver's existing vector instantiation
/// instead of adding a std::unordered_set — RAM1/ITCM has under 1.4 KB of headroom before a
/// whole 32 KB block flips (docs/Plans/capture_serial_ram1_recovery_extmem_debug_enhancement.md),
/// and these lists hold a handful of ids, so linear search costs nothing measurable.
using NoteIdList = std::vector<NoteId, InternalHeapFirstAllocator<NoteId>>;

/// Tick range of the moving note on focus (start/end); used for inner overlap-note tests.
struct MovingNoteRange {
  uint32_t start = 0;
  uint32_t end = 0;
};

struct NoteEditFocus {
  bool active = false;
  NoteId movingNoteId = kInvalidNoteId;
  NoteBaseline commitBaseline{};
  MovingNoteRange movingNoteRange{};
  NoteBaseline last{};
  BaselineMap baselineMap;
  /// Scratch for evict/clear/session undo sizing — not commit or filter authority (baselineMap +
  /// live store). Retained until explicit retire task.
  OverlapNoteMap overlapNotes;

  void clear() {
    active = false;
    movingNoteId = kInvalidNoteId;
    commitBaseline = {};
    movingNoteRange = {};
    last = {};
    baselineMap.clear();
    overlapNotes.clear();
  }
};
