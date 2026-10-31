//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

#include "Globals.h"
#include "Loop.h"
#include "MidiEvent.h"
#include "NoteEditSessionState.h"
#include "Utils/NoteEditDisplaySnapshot.h"
#include "Utils/NoteUtils.h"
#include <vector>

class Track;
struct EditorSelection;

namespace DisplayManagerInternal {

/// Extra bars gathered beyond the paint window so auto-follow does not rebuild every tick.
constexpr uint8_t kWindowedGatherMarginBars = 2;

extern SessionMidiEventVec liveDisplayEventBuffer;

bool shouldAvoidFullVisualRebuild(const Loop& loop, uint32_t loopLength);
bool shouldDeferHeavyDisplayRebuild();

/// Slice 1 of DEC-237: report-only attribution for the committed display gather. Filled by
/// `rebuildDisplayNotesInWindow` only when the caller passes a trace (debug builds); the production
/// call sites pass `nullptr`, so no `micros()` runs.
struct DisplayBaseGatherTrace {
  const char* site = nullptr;
  const char* path = nullptr;
  uint32_t totalUs = 0;
  uint32_t rootUs = 0;
  uint32_t snapshotUs = 0;
  uint32_t passesUs = 0;
  uint32_t wrapUs = 0;
  uint32_t filterUs = 0;
  uint32_t events = 0;
  uint32_t notes = 0;
  uint32_t windowStart = 0;
  uint32_t windowLength = 0;
};

void rebuildDisplayNotesInWindow(Loop& mutLoop, const Loop& loop, uint32_t loopLength,
                                 uint32_t windowStart, uint32_t windowLength,
                                 SessionMidiEventVec& eventBuffer,
                                 NoteUtils::DisplayNoteVec& outNotes, bool includeActiveCapture,
                                 DisplayBaseGatherTrace* trace = nullptr);

/// Emit `DIAG,disp_base` for a slow gather (Tier-A through the `DIAG,disp_` family). No-op outside a
/// session-capture build or below the display frame budget.
void emitDisplayBaseGatherTrace(const DisplayBaseGatherTrace& trace, uint8_t displaySlot);

/// Filter `source` to the paint window plus follow margin. Writes gather bounds.
void filterNotesToFollowWindow(const NoteUtils::DisplayNoteVec& source, uint32_t windowStart,
                               uint32_t windowLength, uint32_t loopLength,
                               NoteUtils::DisplayNoteVec& outNotes, uint32_t& gatherStart,
                               uint32_t& gatherLength);

/// True when `visualCache` covers the paint window plus follow margin.
/// A nonempty dirty prefix must not authorize the 16-bar gather
/// (session_20260825_235954: 20 notes / first 2 bars).
bool visualCacheCoversFollowWindow(const Loop& loop, uint32_t windowStart, uint32_t windowLength,
                                   uint32_t loopLength);

uint8_t resolveTrackIndex(const Track& track);

/// `allowWrapContinuation`: policy from
/// `DisplayWindowUtils::shouldAllowCapturePlayheadWrapContinuation`.
/// OVERDUBBING + PLAYING → true; growing RECORD stays false.
void applyCapturePlayheadTails(const CapturePreview& preview, uint32_t loopLength,
                               uint32_t closeTick, size_t captureRegionStart,
                               NoteUtils::DisplayNoteVec& notes, bool allowWrapContinuation = true);
void applyLiveOpenTails(const std::vector<NoteUtils::OpenNoteOn>& openNotes,
                        const SessionMidiEventVec& midiEvents, uint32_t loopLength,
                        uint32_t closeTick, NoteUtils::DisplayNoteVec& notes,
                        bool extendHeldNotesToPlayhead);

/// Single pending-Shorten source recovery owner. Live capture may invoke this;
/// it must not define a second recovery policy.
bool recoverMissingPendingShortenSources(const Loop& loop, NoteUtils::DisplayNoteVec& compose,
                                         size_t& committedEnd);

/// After wrap seal, pending is empty but sealed Length companions still need
/// compose rows when snapshot gather omits overdub NoteIds (`234535`).
bool recoverMissingSealedShortenSources(const Loop& loop, NoteUtils::DisplayNoteVec& compose,
                                        size_t& committedEnd);

void emitPaintedPendingShortenDiagnostics(const Loop& loop,
                                          const NoteUtils::DisplayNoteVec& compose,
                                          const NoteUtils::DisplayNoteVec& paint);

uint32_t resolveBracketDisplayTick(uint32_t loopStartTick, uint32_t loopLength);
int resolveDrawHighlightIndex(const NoteUtils::DisplayNoteVec& notes,
                              const EditorSelection& selection, uint32_t loopStartTick,
                              uint32_t loopLength, bool windowRelativeTicks,
                              uint32_t windowStartTick, uint32_t bracketDisplayTick);

/// Native-diag: how many committed draw-source gathers have taken the **passes** branch (a stale or
/// missing published root) since the last reset. A current root must never walk the passes.
uint32_t drawSourceCommittedPassGatherCountForTest();
void resetDrawSourceCommittedPassGatherCountForTest();

}  // namespace DisplayManagerInternal
