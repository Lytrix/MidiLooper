//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include "EditPass.h"
#include "MidiEvent.h"
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

struct NoteIdHash {
  size_t operator()(NoteId id) const noexcept { return std::hash<NoteId>{}(id); }
};

using BaselineMap = std::unordered_map<NoteId, NoteBaseline, NoteIdHash>;

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

using OverlapNoteMap = std::unordered_map<NoteId, OverlapNote, NoteIdHash>;

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

/// Read-only scan of loop MIDI events → full-loop baseline inventory.
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

NoteId noteIdFromFilteredDisplayNote(const std::vector<NoteUtils::DisplayNote>& filtered,
                                     int filteredIndex);

int filteredDisplayNoteIndexForNoteId(const std::vector<NoteUtils::DisplayNote>& filtered,
                                    NoteId noteId);
