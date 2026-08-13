//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include "NoteEditFocusTypes.h"

#include <unordered_set>

#include "NoteEditSessionState.h"
#include "Utils/NoteEditMem.h"
#include "Utils/NoteUtils.h"

class NoteEditCurrentState;

NoteBaseline baselineFromDisplayNote(const NoteUtils::DisplayNote& dn);

/// Fingerprint for note-edit display caches — geometry overlap state, mover live span, current-state rows.
uint32_t noteEditDisplayCacheFingerprint(const NoteEditFocus& focus,
                                       const NoteEditCurrentState* currentState = nullptr);

uint32_t movingNoteRangeDisplayEnd(const NoteEditFocus& focus, uint32_t loopLength);

bool isInnerOverlapNoteInMovingNoteRange(const NoteEditFocus& focus, uint8_t pitch,
                                         uint32_t noteStart, uint32_t noteEnd,
                                         uint32_t loopLength);

OverlapNote* findOverlapNoteEntry(NoteEditFocus& focus, NoteId noteId);
const OverlapNote* findOverlapNoteEntry(const NoteEditFocus& focus, NoteId noteId);

/// Sticky end-of-participation: mark Visible shortened rows Ended.
/// Does not rewrite currentSpan (session_20260807_232118).
void clearChangedOverlapParticipationWhenInteractionCleared(
    NoteEditFocus& focus, NoteEditCurrentState& currentState, const NoteBaseline& causingSpan,
    NoteId movingNoteId);
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

/// Select/rebuild: movingNoteId is the painted DisplayNote id (no rematerialize remap).
/// Visible current-state currentSpan wins for last; otherwise the painted span (181114 76@840).
void noteEditFocusApplyDisplayNote(NoteEditFocus& focus, const NoteUtils::DisplayNote& liveSelected,
                                   const NoteEditCurrentState* currentState);

bool noteEditFocusHasPendingLengthChange(const NoteEditFocus& focus);

/// True when pre-commit would emit moving-note and/or overlap edit pass rows.
bool noteEditFocusHasPendingCommit(const NoteEditFocus& focus);

/// Overlap hide/shorten pending via baselineMap + live store, gated by current-state participation.
template <typename Alloc>
bool noteEditFocusHasPendingBaselineMapDiff(const NoteEditFocus& focus,
                                            const std::vector<MidiEvent, Alloc>& sessionEvents,
                                            uint8_t channel, uint32_t loopLength,
                                            const NoteEditCurrentState* currentState = nullptr);

/// True when pitch edit can update the mover pair only (no overlap lane work).
bool canApplySimplePitchChange(MidiEventVec& sessionEvents, const NoteEditFocus& focus,
                               uint8_t channel, uint8_t currentPitch, uint8_t targetPitch,
                               uint32_t moverStart, uint32_t moverEnd, uint32_t loopLength);

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

NOTE_EDIT_MEM bool isLiveEditDriverValidFromCurrentState(const EditorSelection& selection,
                                                         const NoteEditFocus& focus,
                                                         const NoteEditCurrentState& currentState);

NOTE_EDIT_MEM void syncNoteEditFocusLastFromCurrentState(NoteEditFocus& focus, NoteId primaryNote,
                                                         const NoteEditCurrentState& currentState);

/// True when fader-1 select may macro-commit pending mover geometry for **selectNoteId** at
/// **selectBracketTick**. Re-selecting the same **NoteId** at a bracket that disagrees with
/// **focus.last** must not commit (stale mover_focus row — RC7b). Selecting a **different**
/// **NoteId** (mover handoff) always allows commit (Stage 6.5 (3)). Deselect to an empty step
/// seals pending geometry (Stage 7.5 slice C) unless **focus.last** disagrees with the
/// authoritative mover span in @p currentState (session_20260807_014541).
bool isMacroCommitAlignedWithSelectTarget(NoteId selectNoteId,
                                                        uint32_t selectBracketTick,
                                                        const NoteEditFocus& focus,
                                                        uint32_t loopStartTick,
                                                        uint32_t loopLength, bool lengthBracket,
                                                        const NoteEditCurrentState* currentState =
                                                            nullptr);

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
                                     uint32_t loopLength = 0,
                                     const NoteEditCurrentState* currentState = nullptr,
                                     const NoteUtils::DisplayNoteVec* committedDisplayNotes = nullptr);

/// Macro commit rows from current state compared to committed baseline (`baselineMap`).
EditPassVec buildCommitRowsFromCurrentState(const NoteEditFocus& focus,
                                            const NoteEditCurrentState& currentState,
                                            uint8_t channel, uint32_t loopLength,
                                            const NoteUtils::DisplayNoteVec* committedDisplayNotes = nullptr);

/// NoteIds whose geometry is read from the live session store during NOTE_EDIT display projection.
/// When \p currentState is non-empty, overlap participants come from current state (C4 display).
NoteIdList collectProjectionParticipantNoteIds(const NoteEditFocus& focus,
                                               const NoteEditCurrentState* currentState = nullptr);

/// NOTE_EDIT display projection: overlay participant geometry onto a committed/windowed base list.
template <typename Alloc>
NoteUtils::DisplayNoteVec projectNoteEditDisplayNotes(
    const NoteUtils::DisplayNoteVec& committedBaseNotes,
    const std::vector<MidiEvent, Alloc>& sessionEvents, const NoteEditFocus& focus,
    uint8_t channel, uint32_t loopLength, const NoteEditCurrentState* currentState = nullptr);

/// Test/back-compat path — uses session-store reconstruction as the committed base.
template <typename Alloc>
NoteUtils::DisplayNoteVec projectNoteEditDisplayNotes(
    const std::vector<MidiEvent, Alloc>& sessionEvents, const NoteEditFocus& focus,
    uint8_t channel, uint32_t loopLength) {
  const NoteUtils::DisplayNoteVec committedBase =
      NoteUtils::reconstructDisplayNotes(sessionEvents, loopLength, false);
  return projectNoteEditDisplayNotes(committedBase, sessionEvents, focus, channel, loopLength);
}

/// Exclude display-only rows (Hidden/Deleted leave-restore paint) from selectable inventory.
NOTE_EDIT_MEM NoteUtils::DisplayNoteVec filterSelectableDisplayNotes(
    const NoteUtils::DisplayNoteVec& projected, const NoteEditCurrentState* currentState,
    const NoteEditFocus& focus, int selectedNoteIdx);

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

#include "NoteEditFocusSelect.h"
