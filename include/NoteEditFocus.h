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
#include "Utils/ExternalMemoryFirstAllocator.h"
#include "Utils/NoteUtils.h"

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

NoteBaseline baselineFromDisplayNote(const NoteUtils::DisplayNote& dn);

uint32_t movingNoteRangeDisplayEnd(const NoteEditFocus& focus, uint32_t loopLength);

bool isInnerOverlapNoteInMovingNoteRange(const NoteEditFocus& focus, uint8_t pitch,
                                         uint32_t noteStart, uint32_t noteEnd,
                                         uint32_t loopLength);

OverlapNote* findOverlapNoteEntry(NoteEditFocus& focus, NoteId noteId);
const OverlapNote* findOverlapNoteEntry(const NoteEditFocus& focus, NoteId noteId);

NoteId findBaselineNoteIdForDisplay(const NoteEditFocus& focus,
                                    const NoteUtils::DisplayNote& dn);

NoteBaseline baselineForDisplayNote(const NoteEditFocus& focus,
                                    const NoteUtils::DisplayNote& dn);

/// Session-store linear span for overlap hide/restore; prefers live events, then baselineMap.
NoteBaseline linearBaselineForOverlapRestore(const NoteEditFocus& focus, const OverlapNote& entry,
                                             MidiEventVec* sessionEvents, uint8_t channel);

/// Resolve linear storage span for overlap hide/shorten/restore (baselineMap, then display, then session pair).
bool resolveLinearNoteSpanForOverlap(const NoteEditFocus& focus, MidiEventVec& events,
                                     uint8_t channel, const NoteUtils::DisplayNote& dn,
                                     NoteBaseline& out, uint32_t loopLength = 0);

/// Read-only scan of loop MIDI events → moving-note baseline for NOTE_EDIT focus.
template <typename Alloc>
void rebuildNoteEditFocusFromStore(NoteEditFocus& focus,
                                   const std::vector<MidiEvent, Alloc>& loopMidiEvents,
                                   uint8_t channel, uint32_t loopLength, int selectedNoteIdx);

/// A1: length edit updates live end + moving note range only (not commitBaseline).
void noteEditFocusApplyLengthEnd(NoteEditFocus& focus, uint32_t newEndTick);

void noteEditFocusApplyMoveEnd(NoteEditFocus& focus, uint32_t newStart, uint32_t newEnd);

void noteEditFocusApplyPitch(NoteEditFocus& focus, uint8_t newPitch, uint32_t start,
                             uint32_t end, uint32_t loopLength);

bool noteEditFocusHasPendingLengthChange(const NoteEditFocus& focus);

/// Reject LIFO mispairs (e.g. on@387 with off@loopLength+displayEnd).
bool isPlausibleStorageSpan(uint32_t startTick, uint32_t endTick, uint32_t loopLength);

/// Display segment whose length exceeds half the loop is usually wrap projection, not linear span.
bool isInflatedDisplaySpan(const NoteUtils::DisplayNote& dn, uint32_t loopLength);

/// Linear on/off span in canonical storage for noteId (not display projection).
template <typename Alloc>
bool findLinearNoteSpanForNoteId(std::vector<MidiEvent, Alloc>& events, NoteId noteId,
                                 uint8_t channel, NoteBaseline& outBaseline,
                                 uint32_t preferredStartTick = UINT32_MAX,
                                 uint32_t loopLength = 0);

/// Farthest plausible note-off with matching noteId (avoids LIFO steal from same-pitch neighbors).
template <typename Alloc>
MidiEvent* findLinearOffForNoteId(std::vector<MidiEvent, Alloc>& events, const MidiEvent& noteOn,
                                  NoteId noteId, uint32_t loopLength);

/// Refresh focus.last (and moving note range) from session store linear span.
template <typename Alloc>
bool syncNoteEditFocusLinearFromSessionStore(NoteEditFocus& focus,
                                             std::vector<MidiEvent, Alloc>& events,
                                             uint8_t channel, uint32_t loopLength = 0);

uint32_t overlapNoteEffectiveEnd(const OverlapNote& entry);

/// B1: materialize Hidden/Shortened overlap notes in session store before commit (impacted refs only).
template <typename Alloc>
void resolveOverlapNotesForPreCommit(std::vector<MidiEvent, Alloc>& sessionStoreEvents,
                                     NoteEditFocus& focus, uint8_t channel, uint32_t loopLength);

/// Drop overlap scratch rows that are already materialized in session store (display-wrap baselines).
void pruneOverlapNotesBeforePreCommit(NoteEditFocus& focus, MidiEventVec& events, uint8_t channel);

/// True when overlap scratch refers to the moving note (not a restorable overlap participant).
bool isMovingNoteOverlapScratchEntry(const NoteEditFocus& focus, NoteId noteId,
                                     const NoteBaseline& baseline);

/// B1: overlap-only edit pass rows (Hidden → Delete, Shortened → Length).
EditPassVec buildPreCommitOverlapEditPasses(const NoteEditFocus& focus);

/// B1: ordered edit pass rows per pre-commit emission (skip no-ops).
EditPassVec buildPreCommitEditPasses(const NoteEditFocus& focus, uint8_t channel);

/// NOTE_EDIT select/display inventory: session reconstruction minus Hidden and innerUnderMovingNote.
template <typename Alloc>
NoteUtils::DisplayNoteVec filterSelectableDisplayNotes(
    const std::vector<MidiEvent, Alloc>& sessionEvents, const NoteEditFocus& focus,
    uint8_t channel, uint32_t loopLength);

/// NoteIds for micro normalize scope: mover, overlap participants, same-pitch wrap interactors.
std::unordered_set<NoteId> buildEditClosureNoteIds(const NoteEditFocus& focus,
                                                 const MidiEventVec& sessionEvents,
                                                 uint8_t channel, uint32_t loopLength);

/// Committed-loop linear baselines for edit-closure note ids (moving note + wrap interactors).
template <typename Alloc>
void populateBaselineMapForEditClosure(NoteEditFocus& focus,
                                       const std::vector<MidiEvent, Alloc>& committedLoopEvents,
                                       const MidiEventVec& sessionEvents, uint8_t channel,
                                       uint32_t loopLength);

template <typename NotesVec>
inline NoteId noteIdFromFilteredDisplayNote(const NotesVec& filtered, int filteredIndex) {
  if (filteredIndex < 0 || filteredIndex >= static_cast<int>(filtered.size())) {
    return kInvalidNoteId;
  }
  return filtered[static_cast<size_t>(filteredIndex)].noteId;
}

template <typename NotesVec>
inline int filteredDisplayNoteIndexForNoteId(const NotesVec& filtered, NoteId noteId) {
  if (noteId == kInvalidNoteId) {
    return -1;
  }
  for (int i = 0; i < static_cast<int>(filtered.size()); ++i) {
    if (filtered[static_cast<size_t>(i)].noteId == noteId) {
      return i;
    }
  }
  return -1;
}

inline uint32_t displayStartTickFromStorageNote(uint32_t storageStart, uint32_t loopStartTick,
                                                uint32_t loopLength) {
  if (loopLength == 0) {
    return storageStart;
  }
  const uint32_t displayStart = (storageStart >= loopStartTick)
                                    ? (storageStart - loopStartTick)
                                    : (storageStart + loopLength - loopStartTick);
  return displayStart % loopLength;
}

template <typename NotesVec>
inline int filteredDisplayNoteIndexForNoteIdAndStart(const NotesVec& filtered, NoteId noteId,
                                                     uint32_t bracketDisplayTick,
                                                     uint32_t loopStartTick = 0,
                                                     uint32_t loopLength = 0) {
  if (noteId == kInvalidNoteId) {
    return -1;
  }
  for (int i = 0; i < static_cast<int>(filtered.size()); ++i) {
    const NoteUtils::DisplayNote& dn = filtered[static_cast<size_t>(i)];
    if (dn.noteId != noteId) {
      continue;
    }
    const uint32_t displayStart =
        displayStartTickFromStorageNote(dn.startTick, loopStartTick, loopLength);
    if (displayStart == bracketDisplayTick) {
      return i;
    }
  }
  return -1;
}

template <typename NotesVec>
inline int filteredDisplayNoteIndexForNoteIdAndEnd(const NotesVec& filtered, NoteId noteId,
                                                   uint32_t bracketDisplayTick,
                                                   uint32_t loopStartTick = 0,
                                                   uint32_t loopLength = 0) {
  if (noteId == kInvalidNoteId) {
    return -1;
  }
  for (int i = 0; i < static_cast<int>(filtered.size()); ++i) {
    const NoteUtils::DisplayNote& dn = filtered[static_cast<size_t>(i)];
    if (dn.noteId != noteId) {
      continue;
    }
    const uint32_t displayEnd =
        displayStartTickFromStorageNote(dn.endTick, loopStartTick, loopLength);
    if (displayEnd == bracketDisplayTick) {
      return i;
    }
  }
  return -1;
}

template <typename NotesVec>
inline int filteredDisplayNoteIndexForMovingNote(const NotesVec& filtered, NoteId noteId,
                                               uint32_t linearStartTick) {
  return filteredDisplayNoteIndexForNoteIdAndStart(filtered, noteId, linearStartTick);
}
