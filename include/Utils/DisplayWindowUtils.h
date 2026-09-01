//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "MidiEvent.h"
#include "Utils/IntervalProjection.h"
#include "Utils/NoteUtils.h"

namespace DisplayWindowUtils {

constexpr uint32_t kMaxDetailedWindowBars = 16;

/// Extra bar past the paint window that must already be in the committed filter
/// so auto-follow can advance `currentTick` without painting an empty next bar.
constexpr uint32_t kFollowReadyLookaheadBars = 1;

uint32_t chooseBarsPerSegment(uint32_t loopBars, uint32_t segmentCount);

bool noteIntersectsWindow(const TickInterval& noteSpan, const TickInterval& viewport,
                          uint32_t loopLength);

bool noteIntersectsWindow(uint32_t startTick, uint32_t endTick, uint32_t windowStart,
                          uint32_t windowLength, uint32_t loopLength);

NoteUtils::DisplayNoteVec filterDisplayNotesToWindow(const NoteUtils::DisplayNoteVec& notes,
                                                     const TickInterval& viewport,
                                                     uint32_t loopLength);

NoteUtils::DisplayNoteVec filterDisplayNotesToWindow(const NoteUtils::DisplayNoteVec& notes,
                                                     uint32_t windowStart, uint32_t windowLength,
                                                     uint32_t loopLength);

/// Window inclusion filter for edit/motor/nav inventory — keeps storage ticks unchanged.
NoteUtils::DisplayNoteVec filterDisplayNotesByWindowInclusion(
    const NoteUtils::DisplayNoteVec& notes, const TickInterval& viewport, uint32_t loopLength);

NoteUtils::DisplayNoteVec filterDisplayNotesByWindowInclusion(
    const NoteUtils::DisplayNoteVec& notes, uint32_t windowStart, uint32_t windowLength,
    uint32_t loopLength);

bool segmentHasNotes(const NoteUtils::DisplayNoteVec& notes, uint32_t loopLength,
                     const TickInterval& segment);

bool segmentHasNotes(const NoteUtils::DisplayNoteVec& notes, uint32_t loopLength,
                     uint32_t segStartTick, uint32_t segEndTick);

/// Place detailed window so playheadTick sits near the horizontal center (clamped to loop).
uint32_t resolveCenteredWindowStart(uint32_t playheadTick, uint32_t windowLength,
                                    uint32_t loopLength);

/// True when the detailed window covers the whole loop (same pixels as a full-loop roll).
inline bool windowCoversLoop(uint32_t windowLength, uint32_t loopLength) {
    return loopLength > 0 && windowLength >= loopLength;
}

/// Compute a detailed paint window. Returns false only when `TICKS_PER_BAR` is 0.
/// The window may cover the entire loop. Does not classify loop size.
/// `requestedBars` of 0 uses `kMaxDetailedWindowBars`.
bool resolveDetailedPaintWindow(uint32_t loopLength, uint8_t requestedBars,
                                uint32_t storedWindowStart, uint32_t playheadTick,
                                bool autoFollow, uint32_t& outWindowStart,
                                uint32_t& outWindowLength, uint8_t& outWindowBars);

/// Post-stop handoff: keep only the committed compose prefix (drop capture suffix / tails).
inline size_t clampPreservedDisplayNoteCount(size_t liveNoteCount, size_t committedBaseCount) {
    return committedBaseCount < liveNoteCount ? committedBaseCount : liveNoteCount;
}

/// Never grow a DisplayNote vector by resize. Default-constructed rows are MIDI 0 /
/// tick 0 placeholders (`111343` `DNTE,0,0,0,0`). Rebuild from capturePreview instead.
inline size_t clampDisplayNoteResizeCount(size_t currentSize, size_t requestedCount) {
    return requestedCount < currentSize ? requestedCount : currentSize;
}

/// After hide/dedupe shrinks the composed frame, clamp layer counts to the vector.
/// Capture cannot extend past `noteCount`; base is the vector size.
inline void syncLiveDisplayComposeCounts(size_t noteCount, size_t& committedCount,
                                         size_t& captureCount, size_t& baseCount) {
    if (committedCount > noteCount) {
        committedCount = noteCount;
    }
    if (committedCount + captureCount > noteCount) {
        captureCount = noteCount - committedCount;
    }
    baseCount = noteCount;
}

/// Overdub-stop handoff (RC5a): drop temporary playhead-tail rows only — never strip the
/// capture-preview suffix. When compose base bookkeeping is cold (0), keep the full frame.
inline size_t preservedOverdubStopDisplayNoteCount(size_t liveNoteCount, size_t composeBaseCount) {
    if (composeBaseCount == 0) {
        return liveNoteCount;
    }
    return composeBaseCount < liveNoteCount ? composeBaseCount : liveNoteCount;
}

/// RC5a: while visualCache is dirty after overdub stop, the composed frame
/// (committed + capture suffix) stays paint authority. A nonempty dirty cache
/// does not yet include those just-committed notes (`000553`: 655 → 583 at same vis=2166).
/// Revision need not match — wrap and stop both increment `playbackRevision`
/// (`011829` wrap-then-stop gathered 155 ms when the hold required a match).
inline bool preferPreservedOverdubStopHandoff(bool incrementalCommittedDisplay,
                                             bool preservedHandoffAuthority,
                                             bool visualCacheDirty) {
    return incrementalCommittedDisplay && preservedHandoffAuthority && visualCacheDirty;
}

/// `225243`: playhead tails (`endTick = currentTick`) are paint-only. RC5a
/// STOPPED handoff reads `liveDisplayNotes`. In-place tails freeze those ends
/// (D#2 ON@624 → stop phase 704). Compose base stays at capture-preview lengths.
inline bool capturePlayheadTailsArePaintOnly() {
    return true;
}

/// Overdub open notes grow to currentTick on every frame, including over-budget
/// reuse (`112806` second-overdub). RECORD keeps the budget gate.
inline bool shouldApplyLiveCapturePlayheadTails(bool recording, bool overdubbing,
                                               bool reuseLastValidFrame, bool budgetExceeded,
                                               bool hasOpenCaptureNotes) {
    if (!hasOpenCaptureNotes) {
        return false;
    }
    if (overdubbing) {
        return true;
    }
    return recording && !reuseLastValidFrame && !budgetExceeded;
}

/// RC-W1 wrap resets live compose bookkeeping so the next OVERDUBBING frame
/// rebuilds the committed prefix. It must not drop the composed DisplayNote
/// vector — a stop before that frame has nothing to hold
/// (`011829`: last DISP 121.203 s, wrap invalidate 121.318 s, no DISP until
/// `ODUB,stop,display` 155 ms).
inline bool preserveComposedNotesOnOverdubWrapInvalidate() {
    return true;
}

/// Wrap first paint (`231953` 109→65): revision bump rebuild must keep the last
/// composed vector (committed + just-sealed capture). 1i `resize(committedCount)`
/// is the pre-wrap record prefix only. Idle grow still assigns when cache passes
/// that hold. Policy: `preserveComposedNotesOnOverdubWrapInvalidate`.
inline bool shouldHoldComposedNotesOnOverdubWrapRevision(bool wrapRevisionChanged,
                                                        bool visualCacheDirty,
                                                        bool composedNotesNonempty,
                                                        bool visualCacheGrewPastHold) {
    return preserveComposedNotesOnOverdubWrapInvalidate() && wrapRevisionChanged &&
           visualCacheDirty && composedNotesNonempty && !visualCacheGrewPastHold;
}

/// Clean visualCache may authorize committed display; dirty/stale cache must not (RC5b).
inline bool committedDisplayVisualCacheAuthoritative(bool visualCacheDirty,
                                                     bool visualCacheNonempty) {
    return !visualCacheDirty && visualCacheNonempty;
}

/// RC5f: window filter / revision-matched preserve apply while PLAYING defers rebuild
/// or after transport stop (TRACK_STOPPED). Dirty-cache gather must not run on the stop stack.
inline bool preferIncrementalCommittedDisplay(bool deferVisualRebuild, bool trackStopped) {
    return deferVisualRebuild || trackStopped;
}

/// Growing-capture frontier: MIDI note ticks can lead display loopLength by a frame.
/// `endTick > lengthLoop` with `endTick >= startTick` is overflow to clamp — not wrap-head
/// geometry (wrap pairs use endTick < startTick). Prevents 1px flicker at tick 0 on NoteOn.
inline bool clampNonWrapDisplayNoteBarTicks(uint32_t& startTick, uint32_t& endTick,
                                            uint32_t lengthLoop) {
    if (lengthLoop == 0) {
        return false;
    }
    bool clamped = false;
    // Inclusive bar ticks are in [0, lengthLoop-1]. start/end == lengthLoop is frontier
    // overflow (same as > lengthLoop) — must clamp before paint/modulo.
    if (endTick >= lengthLoop && endTick >= startTick) {
        endTick = lengthLoop - 1;
        clamped = true;
    }
    if (startTick >= lengthLoop) {
        startTick = lengthLoop - 1;
        if (endTick < startTick) {
            endTick = startTick;
        }
        clamped = true;
    }
    return clamped;
}

/// Map storage display-note ticks into loop-paint space.
/// Always clamp frontier overflow before any jam-origin modulo — `startTick == loopLength`
/// would otherwise become 0 and paint a 1px blip on the left grid (session_20260811_182528).
inline void mapDisplayNoteBarTicksForLoopPaint(uint32_t startTick, uint32_t endTick,
                                               uint32_t jamStartTick, uint32_t loopLength,
                                               uint32_t& outStartTick, uint32_t& outEndTick) {
    outStartTick = startTick;
    outEndTick = endTick;
    if (loopLength == 0) {
        return;
    }
    clampNonWrapDisplayNoteBarTicks(outStartTick, outEndTick, loopLength);
    if (jamStartTick == 0) {
        return;
    }
    outStartTick = (outStartTick - jamStartTick + loopLength) % loopLength;
    outEndTick = (outEndTick - jamStartTick + loopLength) % loopLength;
}

/// Overdub committed-window reuse: never treat committedCount==0 as a hit (would resize empty).
inline bool overdubCommittedWindowCacheReusable(size_t committedNoteCount, size_t liveNoteCount) {
  return committedNoteCount > 0 && committedNoteCount <= liveNoteCount;
}

/// Hold the PLAYING/overdub committed handoff only while the paint window still
/// sits inside the last gather. A follow exit must snapshot-gather once
/// (`001907`: 21-bar loop kept the first 16-bar slice for the whole overdub).
inline bool shouldHoldOverdubCommittedHandoff(bool canHoldCommittedLayer, bool havePaintWindow,
                                             bool liveWindowGatherValid,
                                             bool paintWindowInsideGather) {
  if (!canHoldCommittedLayer) {
    return false;
  }
  if (!havePaintWindow) {
    return true;
  }
  return liveWindowGatherValid && paintWindowInsideGather;
}

/// Wrap first paint (`222349` 102→67): dirty cache + held layer must not
/// snapshot-gather when the paint window left the last gather. Idle slice fills.
inline bool shouldHoldDirtyOverdubLayerBeforeWindowGather(bool visualCacheDirty,
                                                         bool canHoldCommittedLayer,
                                                         bool visualCacheGrewPastHold) {
  return visualCacheDirty && canHoldCommittedLayer && !visualCacheGrewPastHold;
}

/// STOPPED first paint (`222349` 159/31): after slot mismatch clears the
/// composed handoff, a dirty nonempty cache is hold-until-idle — not last-resort
/// gather. Dirty cache is not clean authority (RC5b).
inline bool shouldHoldDirtyVisualCacheUntilIdle(bool visualCacheDirty, bool visualCacheNonempty,
                                               bool incrementalCommittedDisplay,
                                               bool preservedHandoffAuthority) {
  return visualCacheDirty && visualCacheNonempty && incrementalCommittedDisplay &&
         !preservedHandoffAuthority;
}

/// After wrap, idle or wrap-append can grow `visualCache` past the held PLAYING
/// layer (`201556`: visual=206, frame stayed 134). Do not hold that smaller layer.
/// Held size is the composed frame, not the pre-wrap committed prefix
/// (`232910` wrap 1: cache 89 > prefix 63 assigned cache and dropped compose 104).
inline bool shouldAssignGrownVisualCacheOverOverdubHold(size_t visualCacheNoteCount,
                                                       size_t heldCommittedNoteCount,
                                                       size_t composedNoteCount) {
  const size_t hold = composedNoteCount > heldCommittedNoteCount ? composedNoteCount
                                                                 : heldCommittedNoteCount;
  return visualCacheNoteCount > hold;
}

inline bool shouldAssignGrownVisualCacheOverOverdubHold(size_t visualCacheNoteCount,
                                                       size_t heldCommittedNoteCount) {
  return shouldAssignGrownVisualCacheOverOverdubHold(visualCacheNoteCount, heldCommittedNoteCount,
                                                     heldCommittedNoteCount);
}

/// During capture, keep the last published snapshot even when stale (`004351` wrap 2).
/// After stop, a nonempty `visualCache` (`adopt_partial`) is paint authority so a
/// successor snapshot cannot replace linear stop closes with wrap-shaped ends (`010217`).
inline bool shouldGatherPublishedSnapshotForDrawSource(bool hasPublishedSnapshot,
                                                       bool captureActive,
                                                       bool visualCacheNonempty) {
    return hasPublishedSnapshot && (captureActive || !visualCacheNonempty);
}

/// `000013`: global stop hops tracks and clears `liveDisplayNotes` (141→31) before
/// RC5a refresh. A short-loop composed frame that is larger than `visualCache`
/// must be adopted onto that loop. Do not replace a long-loop cache with a
/// 16-bar window (`185931` 2403→496).
inline bool shouldAdoptComposedNotesOverVisualCache(size_t composedNoteCount,
                                                    size_t visualCacheNoteCount,
                                                    uint32_t loopBars) {
  if (composedNoteCount == 0) {
    return false;
  }
  if (visualCacheNoteCount == 0) {
    return true;
  }
  return loopBars > 0 && loopBars <= kMaxDetailedWindowBars &&
         composedNoteCount > visualCacheNoteCount;
}

/// Full-loop idle must not replace a larger cache with a smaller slice
/// (`000013` Prepared 95→80; `234233` compose 325 then slice 176).
inline bool shouldKeepVisualCacheOverSmallerFullLoopSlice(size_t visualCacheNoteCount,
                                                          size_t sliceNoteCount) {
  return visualCacheNoteCount > 0 && sliceNoteCount < visualCacheNoteCount;
}

/// Coverage contract (`153517` RC1): `builtFromRevision = R` only when the completed
/// gather consumed committed content at R. Overlay-rejected fallback does not establish
/// that coverage, even if a substitute gather ran while `playbackRevision` was R.
inline bool visualCacheGatherCoversPlaybackRevision(bool overlayRejected, bool usedCurrentSnapshot,
                                                    bool usedPrepared, bool fallbackComplete) {
  if (overlayRejected) {
    return false;
  }
  return usedCurrentSnapshot || usedPrepared || fallbackComplete;
}

/// Display-only: one row per pitch+start, shortest exclusive end. Reconstruct
/// keeps every noteId so occupy can Hide a longer same-start sibling (`002145`
/// norow 75/76). Wrap-held (`endTick < startTick`) rows are left untouched.
inline void keepShortestEndAtPitchStart(NoteUtils::DisplayNoteVec& notes) {
  if (notes.size() < 2) {
    return;
  }
  NoteUtils::DisplayNoteVec kept;
  kept.reserve(notes.size());
  for (size_t i = 0; i < notes.size(); ++i) {
    const NoteUtils::DisplayNote& note = notes[i];
    if (note.endTick < note.startTick) {
      kept.push_back(note);
      continue;
    }
    bool dominated = false;
    for (size_t j = 0; j < notes.size(); ++j) {
      const NoteUtils::DisplayNote& other = notes[j];
      if (other.note != note.note || other.startTick != note.startTick ||
          other.endTick < other.startTick) {
        continue;
      }
      if (other.endTick < note.endTick || (other.endTick == note.endTick && j < i)) {
        dominated = true;
        break;
      }
    }
    if (!dominated) {
      kept.push_back(note);
    }
  }
  notes.swap(kept);
}

/// Seal handoff only: a retained sealed-capture row replaces the committed row at the same
/// pitch+start, otherwise it is appended. Deliberately narrow — not a general "newer wins"
/// rule, so it cannot override a companion-transformed row at a different start. Wrap-held
/// rows (`endTick < startTick`) never replace and are only appended. Row order is preserved.
inline void upsertDisplayNoteAtPitchStart(NoteUtils::DisplayNoteVec& notes,
                                          const NoteUtils::DisplayNote& sealedRow) {
  if (sealedRow.endTick >= sealedRow.startTick) {
    for (NoteUtils::DisplayNote& existing : notes) {
      if (existing.note == sealedRow.note && existing.startTick == sealedRow.startTick) {
        existing = sealedRow;
        return;
      }
    }
  }
  notes.push_back(sealedRow);
}

/// Same-start dedupe for the committed prefix only. Live capture suffix rows
/// (open overdub holds) must not lose to a shorter committed sibling (`110022`).
inline void keepShortestEndAtPitchStartOnCommittedPrefix(NoteUtils::DisplayNoteVec& notes,
                                                        size_t committedEnd) {
  if (notes.size() < 2) {
    return;
  }
  if (committedEnd == 0 || committedEnd >= notes.size()) {
    keepShortestEndAtPitchStart(notes);
    return;
  }
  NoteUtils::DisplayNoteVec committed(notes.begin(), notes.begin() + committedEnd);
  keepShortestEndAtPitchStart(committed);
  NoteUtils::DisplayNoteVec rebuilt;
  rebuilt.reserve(notes.size());
  rebuilt.insert(rebuilt.end(), committed.begin(), committed.end());
  rebuilt.insert(rebuilt.end(), notes.begin() + static_cast<std::ptrdiff_t>(committedEnd),
                 notes.end());
  notes.swap(rebuilt);
}

/// Overdub committed layer must switch from window gather to full visualCache when idle
/// slices finish — otherwise wrap/auto-follow keeps painting a stale ~18-bar gather until stop.
inline bool shouldPromoteOverdubCommittedToFullVisualCache(bool overdubbing, bool visualCacheDirty,
                                                           bool visualCacheNonempty,
                                                           bool committedFromWindowGather) {
  return overdubbing && !visualCacheDirty && visualCacheNonempty && committedFromWindowGather;
}

/// True when the paint window lies entirely inside a previously gathered tick range.
inline bool paintWindowInsideGather(uint32_t windowStart, uint32_t windowLength,
                                    uint32_t gatherStart, uint32_t gatherLength) {
    if (windowLength == 0 || gatherLength == 0 || windowStart < gatherStart) {
        return false;
    }
    return (windowStart - gatherStart) + windowLength <= gatherLength;
}

/// Widen a paint window by `marginTicks` on each side, clamped to `[0, loopLength)`.
/// A trailing remainder of at most two margins is included so a 16-bar window on
/// a 20- or 21-bar loop does not omit the last bars (`002634`).
void expandWindowWithMargin(uint32_t windowStart, uint32_t windowLength, uint32_t loopLength,
                            uint32_t marginTicks, uint32_t& outStart, uint32_t& outLength);

TickInterval makeViewportInterval(uint32_t windowStart, uint32_t windowLength);

/// Copy MIDI events whose tick lies in the half-open loop window [start, start + length).
void filterMidiEventsToWindow(const MidiEventVec& events, MidiEventVec& out, uint32_t windowStart,
                              uint32_t windowLength, uint32_t loopLength);

void filterMidiEventsToWindow(const SessionMidiEventVec& events, SessionMidiEventVec& out,
                              uint32_t windowStart, uint32_t windowLength, uint32_t loopLength);

/// Info-strip LEN field: `" --"` when empty; otherwise 3-digit bar count capped at 999.
void formatLoopLengthBars(char* out, size_t outSize, uint32_t loopLengthTicks,
                          uint32_t ticksPerBar);

/// Overview strip rows, and therefore the number of pitch bands the density mask carries.
constexpr uint8_t kOverviewBandCount = 8;

/// Pitch band for the overview density mask: 8 bands of 16 semitones over the MIDI range.
inline uint8_t overviewBandForNote(uint8_t note) {
    const uint8_t band = static_cast<uint8_t>(note / 16u);
    return band < kOverviewBandCount ? band : static_cast<uint8_t>(kOverviewBandCount - 1);
}

/// Set the band bit of every bar the note spans. Bars outside `mask` are ignored, so a mask
/// sized to the loop-so-far stays valid while a record pass grows (RC-G).
template <typename Note>
inline void accumulateOverviewBandMask(std::vector<uint8_t>& mask, const Note& note,
                                       uint32_t ticksPerBar) {
    if (mask.empty() || ticksPerBar == 0) {
        return;
    }
    const uint32_t endTick = note.endTick >= note.startTick ? note.endTick : note.startTick;
    const uint32_t startBar = note.startTick / ticksPerBar;
    if (startBar >= mask.size()) {
        return;
    }
    uint32_t endBar = endTick / ticksPerBar;
    if (endBar >= mask.size()) {
        endBar = static_cast<uint32_t>(mask.size()) - 1;
    }
    const uint8_t bit = static_cast<uint8_t>(1u << overviewBandForNote(note.note));
    for (uint32_t bar = startBar; bar <= endBar; ++bar) {
        mask[bar] |= bit;
    }
}

/// Per-bar draw source. While capture is active every window bar reads committed
/// Snapshot (when a published snapshot exists) or VisualCache notes, then unions
/// `capturePreview`. After stop, a nonempty VisualCache is preferred over the
/// successor snapshot. Closed preview rows supersede at pitch+start in the suffix
/// after `committedDisplayEnd`; open holds (`endTick == startTick`) append.
enum class DrawSourcePath : uint8_t {
    Capture = 0,
    Snapshot = 1,
    VisualCache = 2,
};

inline DrawSourcePath selectDrawSourceForBar(bool captureActive, bool barIsLiveCapture,
                                            bool publishedSnapshotIsCurrent) {
    (void)barIsLiveCapture;
    if (captureActive) {
        return DrawSourcePath::Capture;
    }
    if (publishedSnapshotIsCurrent) {
        return DrawSourcePath::Snapshot;
    }
    return DrawSourcePath::VisualCache;
}

/// Open holds stay a suffix (`110022`). Closed and wrap-held preview rows use
/// the sealed-handoff supersede.
inline bool capturePreviewNoteIsOpenHold(uint32_t startTick, uint32_t endTick) {
    return endTick == startTick;
}

/// Last successfully resolved draw-source gather. Not a painted framebuffer.
enum class DrawSourceReuseKind : uint8_t {
    Hit = 0,
    SlideOneBar = 1,
    Rebuild = 2,
};

struct DrawSourceGatherKey {
    uint8_t slot = 255;
    uint8_t track = 255;
    uint8_t pitchLow = 0;
    uint8_t pitchHigh = 127;
    uint32_t windowStart = 0;
    uint32_t windowLength = 0;
    uint32_t playbackRevision = 0xFFFFFFFFu;
    uint32_t capturePreviewRevision = 0xFFFFFFFFu;
    uint32_t visualCacheRevision = 0xFFFFFFFFu;
    uint32_t publishedSnapshotRevision = 0xFFFFFFFFu;
    uint32_t sealedPreviewRevision = 0;
    uint32_t pendingChangeGeneration = 0;
    uint32_t playheadBar = 0xFFFFFFFFu;
    uint16_t captureDisplayRevision = 0;
    uint16_t sealedPreviewCount = 0;
    uint8_t snapshotCurrent = 0;
    uint8_t sealedCompanionOverlay = 0;
    uint8_t valid = 0;
};

inline bool drawSourceGatherContentKeysMatch(const DrawSourceGatherKey& last,
                                             const DrawSourceGatherKey& current) {
    return last.slot == current.slot && last.track == current.track &&
           last.pitchLow == current.pitchLow && last.pitchHigh == current.pitchHigh &&
           last.windowLength == current.windowLength &&
           last.playbackRevision == current.playbackRevision &&
           last.captureDisplayRevision == current.captureDisplayRevision &&
           last.capturePreviewRevision == current.capturePreviewRevision &&
           last.visualCacheRevision == current.visualCacheRevision &&
           last.publishedSnapshotRevision == current.publishedSnapshotRevision &&
           last.sealedPreviewRevision == current.sealedPreviewRevision &&
           last.sealedPreviewCount == current.sealedPreviewCount &&
           last.pendingChangeGeneration == current.pendingChangeGeneration &&
           last.snapshotCurrent == current.snapshotCurrent &&
           last.sealedCompanionOverlay == current.sealedCompanionOverlay;
}

/// Hit when every mutation key and the playhead bar are unchanged. One-bar slide
/// only when content keys match and `windowStart` moves by exactly one bar.
inline DrawSourceReuseKind selectDrawSourceReuseKind(const DrawSourceGatherKey& last,
                                                     const DrawSourceGatherKey& current,
                                                     uint32_t ticksPerBar) {
    if (last.valid == 0 || current.valid == 0) {
        return DrawSourceReuseKind::Rebuild;
    }
    if (!drawSourceGatherContentKeysMatch(last, current)) {
        return DrawSourceReuseKind::Rebuild;
    }
    if (last.windowStart == current.windowStart && last.playheadBar == current.playheadBar) {
        return DrawSourceReuseKind::Hit;
    }
    if (last.windowStart == current.windowStart) {
        return DrawSourceReuseKind::Rebuild;
    }
    if (ticksPerBar == 0) {
        return DrawSourceReuseKind::Rebuild;
    }
    const uint32_t delta = last.windowStart > current.windowStart
                               ? last.windowStart - current.windowStart
                               : current.windowStart - last.windowStart;
    if (delta == ticksPerBar) {
        return DrawSourceReuseKind::SlideOneBar;
    }
    return DrawSourceReuseKind::Rebuild;
}

/// One DisplayManager paint phase. Idle means no frame is in progress.
enum class DisplayFramePhase : uint8_t {
    Idle = 0,
    Resolve = 1,
    Draw = 2,
    Transfer = 3,
};

inline bool displayFramePhaseIsPending(DisplayFramePhase pending) {
    return pending != DisplayFramePhase::Idle;
}

/// Interval and NOTE_EDIT request start a frame. A pending phase continues.
inline bool admitDisplayFrameTurn(bool pendingPhase, bool intervalDue, bool noteEditRequest) {
    return pendingPhase || intervalDue || noteEditRequest;
}

inline bool admitDisplayFrameTurn(DisplayFramePhase pending, bool intervalDue,
                                  bool noteEditRequest) {
    return admitDisplayFrameTurn(displayFramePhaseIsPending(pending), intervalDue,
                                 noteEditRequest);
}

inline DisplayFramePhase displayFramePhaseToRun(DisplayFramePhase pending) {
    return pending == DisplayFramePhase::Idle ? DisplayFramePhase::Resolve : pending;
}

struct DisplayFramePhaseAdvance {
    DisplayFramePhase pending = DisplayFramePhase::Idle;
    bool frameComplete = false;
};

inline DisplayFramePhaseAdvance advanceDisplayFramePhase(DisplayFramePhase ran) {
    DisplayFramePhaseAdvance out;
    switch (ran) {
        case DisplayFramePhase::Resolve:
            out.pending = DisplayFramePhase::Draw;
            break;
        case DisplayFramePhase::Draw:
            out.pending = DisplayFramePhase::Transfer;
            break;
        case DisplayFramePhase::Transfer:
            out.pending = DisplayFramePhase::Idle;
            out.frameComplete = true;
            break;
        case DisplayFramePhase::Idle:
            break;
    }
    return out;
}

/// Resolved notes stay live until the in-progress frame finishes transfer.
inline bool shouldClearResolvedPaintNotesNow(DisplayFramePhase pending) {
    return pending == DisplayFramePhase::Idle;
}

inline void noteDisplayFrameInvalidate(DisplayFramePhase pending, bool& coalesceFlag) {
    if (pending != DisplayFramePhase::Idle) {
        coalesceFlag = true;
    }
}

}  // namespace DisplayWindowUtils
