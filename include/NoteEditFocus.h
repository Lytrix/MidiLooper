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

/// Sorted, unique NoteId list. Reuses the geometry pipeline's existing vector instantiation
/// instead of adding a std::unordered_set — RAM1/ITCM has under 1.4 KB of headroom before a
/// whole 32 KB block flips (docs/plans/capture_serial_ram1_recovery_extmem_debug_enhancement.md),
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
  /// live store + changedOverlapNoteIds). Retained until explicit retire task.
  OverlapNoteMap overlapNotes;
  /// Overlap notes the geometry pipeline hid or shortened under the current edit driver.
  /// Transient session state: only geometry actions write it, it is cleared at the edit driver
  /// boundary and session end, and it is never persisted to storage. It travels with the focus in
  /// session undo snapshots (like `baselineMap` / `overlapNotes`) so a note changed several steps
  /// back keeps its pre-commit authority across undo. Membership authorises overlap Update rows;
  /// membership plus absence from the live store authorises a Delete row — see
  /// `buildPreCommitBaselineLiveDiffOverlapPasses`.
  NoteIdList changedOverlapNoteIds;

  void clear() {
    active = false;
    movingNoteId = kInvalidNoteId;
    commitBaseline = {};
    movingNoteRange = {};
    last = {};
    baselineMap.clear();
    overlapNotes.clear();
    changedOverlapNoteIds.clear();
  }
};

NoteBaseline baselineFromDisplayNote(const NoteUtils::DisplayNote& dn);

/// Fingerprint for note-edit display caches — geometry overlap state and mover live span.
uint32_t noteEditDisplayCacheFingerprint(const NoteEditFocus& focus);

uint32_t movingNoteRangeDisplayEnd(const NoteEditFocus& focus, uint32_t loopLength);

bool isInnerOverlapNoteInMovingNoteRange(const NoteEditFocus& focus, uint8_t pitch,
                                         uint32_t noteStart, uint32_t noteEnd,
                                         uint32_t loopLength);

OverlapNote* findOverlapNoteEntry(NoteEditFocus& focus, NoteId noteId);
const OverlapNote* findOverlapNoteEntry(const NoteEditFocus& focus, NoteId noteId);

/// Delete authority for `changedOverlapNoteIds` — see the member comment on `NoteEditFocus`.
bool hasChangedOverlapNote(const NoteEditFocus& focus, NoteId noteId);
void recordChangedOverlapNote(NoteEditFocus& focus, NoteId noteId);
void forgetChangedOverlapNote(NoteEditFocus& focus, NoteId noteId);
void applyCommittedOverlapUpdateToFocus(NoteEditFocus& focus, NoteId noteId,
                                        const NoteBaseline& baseline);
void clearCommittedOverlapDeleteIdsFromFocus(NoteEditFocus& focus, const NoteIdList& noteIds);

/// When an overlap target becomes the selected causing note, drop its scratch row (driver boundary).
bool evictOverlapScratchForSelectedNote(NoteEditFocus& focus, NoteId selectedNoteId);

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

/// True when pre-commit would emit moving-note and/or overlap edit pass rows.
bool noteEditFocusHasPendingCommit(const NoteEditFocus& focus);

/// Phase 4 geometry: overlap hide/shorten via baselineMap + live store (overlapNotes scratch empty).
template <typename Alloc>
bool noteEditFocusHasPendingBaselineMapDiff(const NoteEditFocus& focus,
                                            const std::vector<MidiEvent, Alloc>& sessionEvents,
                                            uint8_t channel, uint32_t loopLength);

/// True when pitch edit can update the mover pair only (no overlap lane work).
bool canApplySimplePitchChange(MidiEventVec& sessionEvents, const NoteEditFocus& focus,
                               uint8_t channel, uint8_t currentPitch, uint8_t targetPitch,
                               uint32_t moverStart, uint32_t moverEnd, uint32_t loopLength);

/// Sticky overlap candidates on a pitch lane so the geometry pipeline can RestoreNote when leaving
/// that lane (replaces legacy overlapNotes scratch restore before pitch change).
void recordBaselinePitchLaneRestoreOverlapCandidates(NoteEditFocus& focus,
                                                     const MidiEventVec& liveStore,
                                                     uint8_t channel, uint8_t pitch);

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

/// Live geometry edit: resolve mover note-on by NoteId first, else channel+pitch+start.
template <typename Alloc>
MidiEvent* findNoteOnForMovingNoteEdit(std::vector<MidiEvent, Alloc>& events,
                                       const NoteEditFocus& focus, uint8_t channel,
                                       uint8_t pitch, uint32_t startTick, uint32_t loopLength);

/// Refresh focus.last (and moving note range) from session store linear span.
template <typename Alloc>
bool syncNoteEditFocusLinearFromSessionStore(NoteEditFocus& focus,
                                             std::vector<MidiEvent, Alloc>& events,
                                             uint8_t channel, uint32_t loopLength = 0);

/// True when cached focus is a valid live edit driver (D19a: **NoteId** + **LinearSpan**).
template <typename Alloc>
bool isLiveEditDriverValid(const EditorSelection& selection, const NoteEditFocus& focus,
                           const std::vector<MidiEvent, Alloc>& sessionStore, uint8_t channel,
                           uint32_t loopLength);

/// True when fader-1 select may macro-commit pending mover geometry for **selectNoteId** at
/// **selectBracketTick**. Re-selecting the same **NoteId** at a bracket that disagrees with
/// **focus.last** must not commit (stale mover_focus row — RC7b).
bool isMacroCommitAlignedWithSelectTarget(NoteId selectNoteId,
                                                        uint32_t selectBracketTick,
                                                        const NoteEditFocus& focus,
                                                        uint32_t loopStartTick,
                                                        uint32_t loopLength, bool lengthBracket);

/// Session-open pairing aid: stamp each note-on's noteId onto its LIFO-paired note-off when the
/// off still has kInvalidNoteId. Safe only on non-overlapping same-pitch stores (canonical MIDI).
/// Pairs within each event's own channel — the track's output channel is not an identity key.
template <typename Alloc>
void stampNoteIdsOntoPairedNoteOffs(std::vector<MidiEvent, Alloc>& events);

uint32_t overlapNoteEffectiveEnd(const OverlapNote& entry);

/// B1: materialize Hidden/Shortened overlap notes in session store before commit (impacted refs only).
template <typename Alloc>
void resolveOverlapNotesForPreCommit(std::vector<MidiEvent, Alloc>& sessionStoreEvents,
                                     NoteEditFocus& focus, uint8_t channel, uint32_t loopLength);

/// Drop overlap scratch rows that are already materialized in session store (display-wrap baselines).
template <typename Alloc>
void pruneOverlapNotesBeforePreCommit(NoteEditFocus& focus, std::vector<MidiEvent, Alloc>& events,
                                      uint8_t channel);

/// True when overlap scratch refers to the moving note (not a restorable overlap participant).
bool isMovingNoteOverlapScratchEntry(const NoteEditFocus& focus, NoteId noteId,
                                     const NoteBaseline& baseline);

/// B1: ordered edit pass rows per pre-commit emission (skip no-ops).
/// When @p sessionStoreEvents is set, overlap target rows derive from baseline vs live store.
/// When unset, overlap rows are omitted (moving-note rows still emitted when focus is active).
EditPassVec buildPreCommitEditPasses(const NoteEditFocus& focus, uint8_t channel,
                                     const MidiEventVec* sessionStoreEvents = nullptr,
                                     uint32_t loopLength = 0);

/// NoteIds whose geometry is read from the live session store during NOTE_EDIT display projection.
NoteIdList collectProjectionParticipantNoteIds(const NoteEditFocus& focus);

/// NOTE_EDIT display projection: overlay participant geometry onto a committed/windowed base list.
template <typename Alloc>
NoteUtils::DisplayNoteVec projectNoteEditDisplayNotes(
    const NoteUtils::DisplayNoteVec& committedBaseNotes,
    const std::vector<MidiEvent, Alloc>& sessionEvents, const NoteEditFocus& focus,
    uint8_t channel, uint32_t loopLength);

/// Test/back-compat path — uses session-store reconstruction as the committed base.
template <typename Alloc>
NoteUtils::DisplayNoteVec projectNoteEditDisplayNotes(
    const std::vector<MidiEvent, Alloc>& sessionEvents, const NoteEditFocus& focus,
    uint8_t channel, uint32_t loopLength) {
  const NoteUtils::DisplayNoteVec committedBase =
      NoteUtils::reconstructDisplayNotes(sessionEvents, loopLength, false);
  return projectNoteEditDisplayNotes(committedBase, sessionEvents, focus, channel, loopLength);
}

/// NoteIds for micro normalize + full-loop transaction baseline (mover, overlap, all live notes).
template <typename Alloc>
std::unordered_set<NoteId> buildEditClosureNoteIds(const NoteEditFocus& focus,
                                                   const std::vector<MidiEvent, Alloc>& sessionEvents,
                                                   uint8_t channel, uint32_t loopLength);

/// Snapshot missing baselineMap entries from committed materialize at edit-driver boundary (D19).
/// Insert-if-missing only — never prune hidden notes. Keys use live-store noteIds.
template <typename AllocA, typename AllocB>
void populateBaselineMapForEditClosure(NoteEditFocus& focus,
                                       const std::vector<MidiEvent, AllocA>& committedLoopEvents,
                                       const std::vector<MidiEvent, AllocB>& sessionEvents,
                                       uint8_t channel, uint32_t loopLength);

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
                                                 uint32_t linearStartTick,
                                                 uint32_t loopStartTick = 0,
                                                 uint32_t loopLength = 0) {
  const uint32_t displayBracket =
      loopLength > 0 ? displayStartTickFromStorageNote(linearStartTick, loopStartTick, loopLength)
                     : linearStartTick;
  return filteredDisplayNoteIndexForNoteIdAndStart(filtered, noteId, displayBracket,
                                                   loopStartTick, loopLength);
}
