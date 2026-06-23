//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include "EditPass.h"
#include "MidiEvent.h"

namespace NoteUtils {
struct DisplayNote;
}

struct NoteBaseline {
  uint8_t pitch = 0;
  uint8_t velocity = 64;
  uint32_t startTick = 0;
  uint32_t endTick = 0;
};

inline bool noteRefEquals(const NoteRef& a, const NoteRef& b) {
  return a.channel == b.channel && a.note == b.note && a.startTick == b.startTick &&
         a.endTick == b.endTick;
}

struct NoteRefHash {
  size_t operator()(const NoteRef& ref) const noexcept {
    size_t h = std::hash<uint8_t>{}(ref.channel);
    h ^= std::hash<uint8_t>{}(ref.note) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<uint32_t>{}(ref.startTick) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<uint32_t>{}(ref.endTick) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};

struct NoteRefEqual {
  bool operator()(const NoteRef& a, const NoteRef& b) const noexcept {
    return noteRefEquals(a, b);
  }
};

using BaselineMap =
    std::unordered_map<NoteRef, NoteBaseline, NoteRefHash, NoteRefEqual>;

enum class OverlapNoteStoreState : uint8_t { Visible, Hidden, Shortened };

struct OverlapNote {
  NoteRef ref{};
  NoteBaseline baseline{};
  OverlapNoteStoreState state = OverlapNoteStoreState::Visible;
  uint32_t shortenedEndTick = 0;
  bool innerUnderMovingNote = false;
  /// Hidden overlap **DeleteNote** already saved in **Edits[]** — skip re-emit; keep for pitch lane restore.
  bool preCommitEmitted = false;
};

/// Scratch payload for re-inserting a hidden/shortened overlap note into session store events.
struct OverlapNoteRestore {
  uint8_t pitch = 0;
  uint8_t velocity = 64;
  uint32_t startTick = 0;
  uint32_t endTick = 0;
  uint32_t originalLength = 0;
  bool wasShortened = false;
  uint32_t shortenedToTick = 0;
};

using OverlapNoteMap =
    std::unordered_map<NoteRef, OverlapNote, NoteRefHash, NoteRefEqual>;

/// Tick range of the moving note on focus (start/end); used for inner overlap-note tests.
struct MovingNoteRange {
  uint32_t start = 0;
  uint32_t end = 0;
};

struct NoteEditFocus {
  bool active = false;
  NoteRef moving{};
  NoteBaseline commitBaseline{};
  MovingNoteRange movingNoteRange{};
  NoteBaseline last{};
  BaselineMap baselineMap;
  OverlapNoteMap overlapNotes;

  void clear() {
    active = false;
    moving = {};
    commitBaseline = {};
    movingNoteRange = {};
    last = {};
    baselineMap.clear();
    overlapNotes.clear();
  }
};

NoteRef noteRefFromDisplay(uint8_t channel, const NoteUtils::DisplayNote& dn);
NoteRef noteRefFromBaseline(uint8_t channel, const NoteBaseline& bl);

uint32_t movingNoteRangeDisplayEnd(const NoteEditFocus& focus, uint32_t loopLength);

bool isInnerOverlapNoteInMovingNoteRange(const NoteEditFocus& focus, uint8_t pitch,
                                         uint32_t noteStart, uint32_t noteEnd,
                                         uint32_t loopLength);

OverlapNote* findOverlapNoteEntry(NoteEditFocus& focus, const NoteRef& ref);
const OverlapNote* findOverlapNoteEntry(const NoteEditFocus& focus, const NoteRef& ref);

NoteRef findBaselineRefForNote(const NoteEditFocus& focus, uint8_t channel,
                               uint8_t pitch, uint32_t startTick, uint32_t endTick);

NoteBaseline baselineForDisplayNote(const NoteEditFocus& focus, uint8_t channel,
                                    const NoteUtils::DisplayNote& dn);

/// Read-only scan of loop MIDI events → full-loop baseline inventory.
/// **Internal / legacy encoder only** until Phase 2b — do not pass fader-1 **filtered** `selectedNoteIdx`
/// from NOTE_EDIT UI; use **rebuildNoteEditFocusForDisplayNote** instead (C14).
void rebuildNoteEditFocusFromStore(NoteEditFocus& focus, const MidiEventVec& loopMidiEvents,
                                   uint8_t channel, uint32_t loopLength,
                                   int selectedNoteIdx);

/// A1: length edit updates live end + moving note range only (not commitBaseline).
void noteEditFocusApplyLengthEnd(NoteEditFocus& focus, uint32_t newEndTick);

void noteEditFocusApplyMoveEnd(NoteEditFocus& focus, uint32_t newStart, uint32_t newEnd);

void noteEditFocusApplyPitch(NoteEditFocus& focus, uint8_t newPitch, uint32_t start,
                             uint32_t end, uint32_t loopLength);

bool noteEditFocusHasPendingLengthChange(const NoteEditFocus& focus);

uint32_t overlapNoteEffectiveEnd(const OverlapNote& entry);

/// B1: materialize Hidden/Shortened overlap notes in session store before commit (impacted refs only).
void resolveOverlapNotesForPreCommit(MidiEventVec& sessionStoreEvents, NoteEditFocus& focus,
                                     uint8_t channel, uint32_t loopLength);

/// B1: overlap-only edit pass rows (Hidden → Delete, Shortened → Length).
EditPassVec buildPreCommitOverlapEditPasses(const NoteEditFocus& focus);

/// B1: ordered edit pass rows per pre-commit emission (skip no-ops).
EditPassVec buildPreCommitEditPasses(const NoteEditFocus& focus, uint8_t channel);

/// NOTE_EDIT select/display inventory: session reconstruction minus Hidden and innerUnderMovingNote.
std::vector<NoteUtils::DisplayNote> filterSelectableDisplayNotes(
    const MidiEventVec& sessionEvents, const NoteEditFocus& focus, uint8_t channel,
    uint32_t loopLength);

NoteRef noteRefFromFilteredDisplayNote(uint8_t channel, const NoteEditFocus& focus,
                                       const std::vector<NoteUtils::DisplayNote>& filtered,
                                       int filteredIndex);

int filteredDisplayNoteIndexForNoteRef(uint8_t channel, const NoteEditFocus& focus,
                                       const std::vector<NoteUtils::DisplayNote>& filtered,
                                       const NoteRef& ref);
