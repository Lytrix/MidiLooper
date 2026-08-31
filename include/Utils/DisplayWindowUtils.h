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

/// Post-stop handoff: keep only the committed compose prefix (drop capture suffix / tails).
inline size_t clampPreservedDisplayNoteCount(size_t liveNoteCount, size_t committedBaseCount) {
    return committedBaseCount < liveNoteCount ? committedBaseCount : liveNoteCount;
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

}  // namespace DisplayWindowUtils
