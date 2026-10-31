//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "DisplayManager.h"
#include "DisplayManagerInternal.h"

#include "Globals.h"
#include "ClockManager.h"
#include "EditManager.h"
#include "Loop.h"
#include "LoopContentResolution.h"
#include "LoopSnapshot.h"
#include "PendingNoteChange.h"
#include "TrackManager.h"
#include "TrackStateMachine.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/Diagnostics.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/IntervalProjection.h"
#include "Utils/SnapshotNoteConversion.h"
#include "Utils/SlotFocusDisplay.h"
#include "VisualCache.h"
#include <Arduino.h>
#include <algorithm>
#include <cstdio>
#include <vector>

#if defined(__IMXRT1062__)
#define DISP_COLD_MEM FLASHMEM
#define DISP_CAPTURE_MEM FLASHMEM
/// Mutable capture-sampler state (task 5.3 / invariant 36): external RAM, never DTCM. At 250 Hz the slower access is
/// irrelevant, and the approved RAM1 line stays exactly as measured for the lane.
#define DISP_RECEIPT_MEM DMAMEM
#else
#define DISP_COLD_MEM
#define DISP_CAPTURE_MEM
#define DISP_RECEIPT_MEM
#endif

using namespace DisplayManagerInternal;

namespace {
// Native diag: counts committed draw-source gathers that took the **passes** branch (stale/missing root).
// DMAMEM keeps this test-only counter out of DTCM so the parked display-source fix does not shift the
// approved RAM1 placement (96,192 variables).
DMAMEM uint32_t gDrawSourceCommittedPassGathers = 0;
}  // namespace

uint32_t DisplayManagerInternal::drawSourceCommittedPassGatherCountForTest() {
    return gDrawSourceCommittedPassGathers;
}
void DisplayManagerInternal::resetDrawSourceCommittedPassGatherCountForTest() {
    gDrawSourceCommittedPassGathers = 0;
}

namespace {
DISP_COLD_MEM bool displaySourcePublicationPending(const Track& track, const Loop& loop,
                                                  uint8_t slot) {
    // A stopped Record obligation owns the source before its worker queues
    // Snapshot publication. Do not erase the complete frame in that interval.
    return loop.isPlaybackSessionExitPublicationGap() ||
        (!track.isRecording() && !loop.publishedSnapshot() && track.hasStopInFlightForSlot(slot));
}
/// The `DIAG,disp_turn` name of the source that served a composed interval (C5.6). An if/else chain on
/// purpose: a `switch` over this enum emits a jump table into `.data`, which is what the RAM1 rule forbids.
DISP_COLD_MEM const char* qualifiedDisplaySourceToken(Loop::QualifiedDisplaySource source) {
    if (source == Loop::QualifiedDisplaySource::PreparedWindow) return "prepared";
    if (source == Loop::QualifiedDisplaySource::BoundedContinuation) return "bound";
    if (source == Loop::QualifiedDisplaySource::RetainedEnvelope) return "retained";
    if (source == Loop::QualifiedDisplaySource::VisualCache) return "cache";
    return "-";
}
}  // namespace

DISP_COLD_MEM void DisplayManagerInternal::filterNotesToFollowWindow(
    const NoteUtils::DisplayNoteVec& source, uint32_t windowStart, uint32_t windowLength,
    uint32_t loopLength, NoteUtils::DisplayNoteVec& outNotes, uint32_t& gatherStart,
    uint32_t& gatherLength) {
    const uint32_t padBars =
        kWindowedGatherMarginBars > DisplayWindowUtils::kFollowReadyLookaheadBars
            ? kWindowedGatherMarginBars
            : static_cast<uint8_t>(DisplayWindowUtils::kFollowReadyLookaheadBars);
    const uint32_t marginTicks = static_cast<uint32_t>(padBars) * Config::TICKS_PER_BAR;
    DisplayWindowUtils::expandWindowWithMargin(windowStart, windowLength, loopLength, marginTicks,
                                               gatherStart, gatherLength);
    outNotes = DisplayWindowUtils::filterDisplayNotesByWindowInclusion(source, gatherStart,
                                                                       gatherLength, loopLength);
}

DISP_COLD_MEM bool DisplayManager::syncDetailedPaintWindow(const Track& track, uint8_t displaySlot,
                                             uint32_t currentTick, uint32_t loopLength,
                                             uint32_t& outWindowStart, uint32_t& outWindowLength,
                                             uint8_t& outWindowBars, bool* outHeldRetainedContent) {
    if (outHeldRetainedContent != nullptr) *outHeldRetainedContent = false;
    if (track.isJamming() || displaySlot >= kDisplaySlotCount) {
        outWindowStart = 0;
        outWindowLength = loopLength;
        outWindowBars = DisplayWindowUtils::kMaxDetailedWindowBars;
        return false;
    }
    const Loop& loop = track.getLoop(displaySlot);
    const bool sameHeldScope = liveDrawSourceKey_.valid != 0 &&
        liveDrawSourceKey_.slot == displaySlot &&
        liveDrawSourceKey_.track == resolveTrackIndex(track) &&
        livePlaybackDisplaySlot_ == displaySlot &&
        livePlaybackDisplayTrack_ == liveDrawSourceKey_.track && !liveDisplayNotes.empty();
    // The interval the playhead asks for this turn, resolved before any retained-frame hold substitutes the held
    // one: `DIAG,disp_turn` pairs it with the interval the published frame carries, which is what makes the
    // drawn-vs-requested question decidable in a capture instead of inferred from it.
    const uint8_t requestedBars = std::min<uint8_t>(detailedWindowBars_[displaySlot],
                                                    DisplayWindowUtils::kMaxDetailedWindowBars);
    const uint32_t horizonLength =
        static_cast<uint32_t>(requestedBars) * Config::TICKS_PER_BAR;
    const bool autoFollow = !DisplayWindowUtils::windowCoversLoop(horizonLength, loopLength) &&
                            shouldAutoFollowDetailedWindow(track, loopLength);
    const uint32_t playhead = autoFollow ? resolvePlayheadInLoop(track, displaySlot, currentTick)
                                         : 0;
    uint32_t requestedStart = 0;
    uint32_t requestedLength = 0;
    uint8_t requestedWindowBars = 0;
    const bool haveRequest = DisplayWindowUtils::resolveDetailedPaintWindow(
        loopLength, requestedBars, detailedWindowStartTick_[displaySlot], playhead, autoFollow,
        requestedStart, requestedLength, requestedWindowBars);
    detailedWindowRequestedStartTick_ = haveRequest ? requestedStart : 0;
    detailedWindowRequestedLengthTicks_ = haveRequest ? requestedLength : loopLength;
    detailedWindowRequestedSlot_ = displaySlot;
    // The split (`DIAG,disp_turn`): the interval the frame is **drawn** in follows this request on every turn,
    // even while the hold below substitutes the retained interval for the **content**. A cursor mapped in the
    // retained coordinates is what left it behind the moving window on hardware (capture
    // `session_20261026_221420`: 154 retained frames whose request had already diverged from the published
    // interval). Both facts stay separately visible: `req` is this pair, `pub` is the viewport pair, and
    // `st=ret`/`why=hold_gap` name the content hold that keeps `liveDrawSourceKey_` where it is.
    detailedWindowViewportStartTick_ = detailedWindowRequestedStartTick_;
    detailedWindowViewportLengthTicks_ = detailedWindowRequestedLengthTicks_;
    detailedWindowViewportSlot_ = detailedWindowRequestedSlot_;
    if (sameHeldScope && displaySourcePublicationPending(track, loop, displaySlot) &&
        !track.hasCaptureStopPreviewForSlot(displaySlot) &&
        liveDrawSourceKey_.windowStart < loopLength && liveDrawSourceKey_.windowLength > 0 &&
        liveDrawSourceKey_.windowLength <= loopLength) {
        // The **content** stays the retained frame's qualified interval: it is not re-gathered, not advanced and
        // never presented as the requested interval's content. Only the viewport above follows the request.
        if (outHeldRetainedContent != nullptr) *outHeldRetainedContent = true;
        outWindowStart = liveDrawSourceKey_.windowStart;
        outWindowLength = liveDrawSourceKey_.windowLength;
        outWindowBars = static_cast<uint8_t>((outWindowLength + Config::TICKS_PER_BAR - 1) /
                                             Config::TICKS_PER_BAR);
        detailedWindowStartTick_[displaySlot] = outWindowStart;
        return true;
    }
    if (!haveRequest) {
        outWindowStart = 0;
        outWindowLength = loopLength;
        outWindowBars = requestedBars;
        return false;
    }
    outWindowStart = requestedStart;
    outWindowLength = requestedLength;
    outWindowBars = requestedWindowBars;
    detailedWindowStartTick_[displaySlot] = outWindowStart;
    return true;
}

#if defined(SESSION_CAPTURE)
namespace {

/// What the display-window receipt is keyed on: the values that decide **what the display holds and shows**, never the
/// content. A change of any field is one transition row; no change is no row.
///
/// `windowBand` is the held window start **in whole bars**, and it is what decides "the window moved". The follow window
/// recentres continuously (measured on the 2026-10-20 rerun: ~24 ticks per 63 ms sample), so keying on the raw start
/// emits a row per sample — a stream, not a transition. A band change is the display's own granularity for "it moved";
/// the emitted row still carries the raw start, so a bracket compares exact positions.
struct DetailedWindowReceiptKey {
    uint8_t track = 0xFF;
    uint8_t slot = 0xFF;
    uint8_t state = 0xFF;
    uint8_t noteSessionOpen = 0xFF;
    uint8_t editSession = 0xFF;
    uint32_t loopLength = 0xFFFFFFFFu;
    uint32_t windowBand = 0xFFFFFFFFu;
    uint32_t windowStart = 0xFFFFFFFFu;
    uint8_t windowBars = 0xFF;
    /// Not part of the comparison. `.bss.dma` is **ALLOC-only** (verified on the ELF: no `CONTENTS`, no initial image),
    /// so nothing here is initialised at boot -- this is DEC-121's poisoned-init hazard, and a `= 0` initialiser would be
    /// a lie. Arming is therefore a **value the sampler writes** on every emit: `armedMagic == kReceiptArmedMagic` means
    /// "a key has been recorded". Fresh NOLOAD garbage practically never equals the magic, and a warm reset that does
    /// match leaves the previous boot's last key, which is exactly a legitimate "last emitted".
    uint32_t armedMagic = 0;
};

/// Task 5.3 / invariant 36: the key is *mutable* state, so it must not land in DTCM — the RAM1 line stays as measured
/// for the lane, and the sampler's 32 B (28 B key + cadence stamp) live in external RAM beyond the RAM1 line. The pin
/// makes the size
/// deliberate: one key per boot, one cache line, re-base at the assert if the key ever grows.
static_assert(sizeof(DetailedWindowReceiptKey) <= 32,
              "DEC-149 display-window receipt key grew past one cache line; re-base this pin deliberately");

/// 250 Hz sampling: fine enough that a 2 s freeze / snap / hold bracket cannot lose a transition, and it runs from
/// `update()` (not from the paint path) precisely so a window that stops repainting keeps being observed.
constexpr uint32_t kDetailedWindowReceiptSampleMs = 4;

/// Observed only while the track plays (task 6.4 measures the playing window), plus **one closing row** when it leaves
/// that state. Recording is out of scope on purpose: the loop length grows on nearly every sample, so a recording-time
/// sampler is a stream by construction (the first rerun emitted 2 757 rows and evicted a Tier-A row).
constexpr uint8_t kObservedTrackState = static_cast<uint8_t>(TRACK_PLAYING);

/// External RAM on the target (invariant 36), plain statics on the host.
DISP_RECEIPT_MEM DetailedWindowReceiptKey lastDetailedWindowReceipt;
/// Only a cadence stamp: it is read only once a key is armed, and a write precedes that, so NOLOAD boot bytes can at
/// worst admit one early sample.
DISP_RECEIPT_MEM uint32_t lastDetailedWindowReceiptMs;

/// The value that arms the sampler; written by `observeDetailedWindowTransition`, never initialised (see above).
constexpr uint32_t kReceiptArmedMagic = 0x44575031u;  // 'DWP1'

inline bool detailedWindowReceiptArmed() {
    return lastDetailedWindowReceipt.armedMagic == kReceiptArmedMagic;
}


/// What the **drawn-vs-requested** receipt compares (`DIAG,disp_turn`): the interval the published frame carries,
/// whether the frame was fresh or retained, and the slot it belongs to. The *requested* interval is deliberately
/// not part of the comparison -- it moves every turn, so keying on it would emit one row per frame and turn the
/// receipt into the stream it is meant not to be. A diverged request is reported on the cadence instead.
struct DisplayTurnReceiptKey {
    uint8_t track = 0xFF;
    uint8_t slot = 0xFF;
    uint8_t retained = 0xFF;
    uint32_t publishedStart = 0xFFFFFFFFu;
    uint32_t publishedLength = 0xFFFFFFFFu;
    /// Same poisoned-init rule as `DetailedWindowReceiptKey`: `.bss.dma` is ALLOC-only, so arming is a value the
    /// emitter writes, never an initialiser.
    uint32_t armedMagic = 0;
};

DISP_RECEIPT_MEM DisplayTurnReceiptKey lastDisplayTurnReceipt;
DISP_RECEIPT_MEM uint32_t lastDisplayTurnReceiptMs;

/// The value that arms the receipt; written by `emitDisplayTurnReceipt`, never initialised (see above).
constexpr uint32_t kDisplayTurnArmedMagic = 0x44545731u;  // 'DTW1'

/// One row per visual turn, bounded twice: a change of the published interval or of retained/fresh always emits,
/// and a request that keeps diverging from the published interval emits at most once per this interval. A frame
/// that tracks its request emits nothing, so the normal case costs no rows at all.
constexpr uint32_t kDisplayTurnReceiptSampleMs = 100;

/// Slice 3 source-identity row floor (2026-10-30 plan): one row per 200 ms even when the frame key changes
/// faster. RAM2 so the display TU's DTCM is unchanged.
constexpr uint32_t kDisplaySourceIdentitySampleMs = 200;
DMAMEM uint32_t lastDisplaySourceIdentityMs = 0;

inline bool displayTurnReceiptArmed() {
    return lastDisplayTurnReceipt.armedMagic == kDisplayTurnArmedMagic;
}
}  // namespace
#endif

DISP_COLD_MEM void DisplayManager::observeDetailedWindowTransition() {
#if defined(SESSION_CAPTURE)
    const uint32_t nowMs = millis();
    if (detailedWindowReceiptArmed() &&
        static_cast<uint32_t>(nowMs - lastDetailedWindowReceiptMs) < kDetailedWindowReceiptSampleMs) {
        return;
    }
    const uint8_t trackIndex = trackManager.getSelectedTrackIndex();
    const uint8_t displaySlot = trackManager.getSelectedSlotIndex(trackIndex);
    if (displaySlot >= kDisplaySlotCount) {
        return;
    }
    const Track& track = trackManager.getTrack(trackIndex);
    const uint8_t state = static_cast<uint8_t>(track.getState());
    // Outside the observed state only a *transition out of it* is worth a row: the verifier reads the last playing row
    // and the closing row as the interval's ends, and nothing else is sampled.
    if (state != kObservedTrackState &&
        !(detailedWindowReceiptArmed() && lastDetailedWindowReceipt.state == kObservedTrackState)) {
        return;
    }
    DetailedWindowReceiptKey key{};
    key.track = trackIndex;
    key.slot = displaySlot;
    key.state = state;
    key.noteSessionOpen = editManager.isNoteEditActive() ? 1 : 0;
    key.editSession = static_cast<uint8_t>(editManager.getEditSessionType());
    key.loopLength = resolveDisplayLoopLength(track, displaySlot, clockManager.getCurrentTick());
    // The window the display holds for this slot: the values its next paint resolves from. During NOTE_EDIT the follow
    // decision is off (`shouldAutoFollowDetailedWindow`), so these stay put -- which is exactly the freeze the leg measures.
    key.windowStart = detailedWindowStartTick_[displaySlot];
    key.windowBand = key.windowStart / static_cast<uint32_t>(Config::TICKS_PER_BAR);
    key.windowBars = detailedWindowBars_[displaySlot];
    lastDetailedWindowReceiptMs = nowMs;
    if (detailedWindowReceiptArmed() && key.track == lastDetailedWindowReceipt.track &&
        key.slot == lastDetailedWindowReceipt.slot && key.state == lastDetailedWindowReceipt.state &&
        key.noteSessionOpen == lastDetailedWindowReceipt.noteSessionOpen &&
        key.editSession == lastDetailedWindowReceipt.editSession &&
        key.loopLength == lastDetailedWindowReceipt.loopLength &&
        key.windowBand == lastDetailedWindowReceipt.windowBand &&
        key.windowBars == lastDetailedWindowReceipt.windowBars) {
        return;
    }
    key.armedMagic = kReceiptArmedMagic;
    lastDetailedWindowReceipt = key;
    SC_DISP_WINDOW_TRANSITION(key.track, key.slot, TrackStateMachine::toString(track.getState()),
                              key.noteSessionOpen, key.editSession, key.loopLength, key.windowStart,
                              key.windowBars);
#endif
}

/// Slice 3 source identity (2026-10-30 plan) plus a 1 s read-only mode heartbeat (K3): the committed
/// generation, the live/sealed/stop-preview overlays, the pending stop job, and the edit session. The
/// heartbeat is emitted even when the `disp_turn` key is unchanged, so a HITL rig can assert LOOP_EDIT
/// before the clear without sending a mode-changing gesture.
DISP_COLD_MEM void emitDisplaySourceRow(const Track& track, uint8_t displaySlot, bool retained,
                                        uint32_t minIntervalMs) {
#if defined(SESSION_CAPTURE)
    const uint32_t nowMs = millis();
    if (nowMs - lastDisplaySourceIdentityMs < minIntervalMs) {
        return;
    }
    lastDisplaySourceIdentityMs = nowMs;
    const uint8_t trackIndex = resolveTrackIndex(track);
    const Loop& loop = track.getLoop(displaySlot);
    const auto pending = track.pendingDeferredStopIdentity();
    const auto& preview = loop.capturePreviewView();
    const size_t stopPreviews = track.captureStopPreviewCount();
    size_t stopPreviewNotes = 0;
    for (size_t i = 0; i < stopPreviews; ++i) {
        if (const CapturePreview* stopPreview = track.captureStopPreviewForSlot(displaySlot, i)) {
            stopPreviewNotes += stopPreview->notes.size();
        }
    }
    DebugSessionCapture::displaySourceIdentity(
        trackIndex, displaySlot, loop.playbackRevision, loop.committedPassRevision(),
        preview.revision, static_cast<uint32_t>(preview.notes.size()),
        static_cast<uint32_t>(loop.sealedCapturePreviewNotes().size()),
        static_cast<uint32_t>(stopPreviews), static_cast<uint32_t>(stopPreviewNotes), pending.active,
        pending.phase, pending.generation, editManager.isLoopEditSession(), retained);
#else
    (void)track;
    (void)displaySlot;
    (void)retained;
    (void)minIntervalMs;
#endif
}

/// Bounded `DIAG,disp_turn` emitter: one row per visual turn that changes what the display publishes, and at most
/// one row per `kDisplayTurnReceiptSampleMs` while the requested interval stays diverged from the published one.
/// `requested`/`published` are tick intervals; `retained` says which of the two the drawn frame carries,
/// `reason` names the rule that discarded the request (`-` when nothing was discarded) or the provider that
/// served it (`prepared`/`bound`/`cache`), and `key*` is the covered interval of the frame that carries the
/// content (`DIAG,disp_turn`'s `key=` pair) — the retained interval while a hold keeps the frame, the served
/// interval once a provider published for the request.
DISP_COLD_MEM void emitDisplayTurnReceipt(const Track& track, uint8_t displaySlot,
                                          uint32_t requestedStart, uint32_t requestedLength,
                                          uint32_t publishedStart, uint32_t publishedLength,
                                          uint32_t drawnNotes, bool retained,
                                          const char* discardReason, uint32_t keyStartTick,
                                          uint32_t keyLengthTicks) {
#if defined(SESSION_CAPTURE)
    const uint8_t trackIndex = resolveTrackIndex(track);
    const uint8_t retainedFlag = retained ? 1u : 0u;
    // K3: a 1 s heartbeat even when the key is unchanged; the transition path below refreshes at 200 ms.
    emitDisplaySourceRow(track, displaySlot, retained, 1000u);
    const bool diverged = requestedStart != publishedStart || requestedLength != publishedLength;
    const bool transition = !displayTurnReceiptArmed() ||
        lastDisplayTurnReceipt.track != trackIndex || lastDisplayTurnReceipt.slot != displaySlot ||
        lastDisplayTurnReceipt.retained != retainedFlag ||
        lastDisplayTurnReceipt.publishedStart != publishedStart ||
        lastDisplayTurnReceipt.publishedLength != publishedLength;
    if (!transition) {
        if (!diverged) {
            return;
        }
        if (static_cast<uint32_t>(millis() - lastDisplayTurnReceiptMs) <
            kDisplayTurnReceiptSampleMs) {
            return;
        }
    }
    lastDisplayTurnReceiptMs = millis();
    lastDisplayTurnReceipt.track = trackIndex;
    lastDisplayTurnReceipt.slot = displaySlot;
    lastDisplayTurnReceipt.retained = retainedFlag;
    lastDisplayTurnReceipt.publishedStart = publishedStart;
    lastDisplayTurnReceipt.publishedLength = publishedLength;
    lastDisplayTurnReceipt.armedMagic = kDisplayTurnArmedMagic;
    // `key` is the covered interval of the frame that carries the content: the retained interval while the hold
    // keeps the frame, and the requested interval when a qualified provider served it (`why=<provider>`).
    SC_DISP_WINDOW_TURN(trackIndex, displaySlot, requestedStart, requestedLength, publishedStart,
                        publishedLength, drawnNotes, retained ? "ret" : "fresh",
                        discardReason != nullptr ? discardReason : "-", keyStartTick,
                        keyLengthTicks);
    emitDisplaySourceRow(track, displaySlot, retained, kDisplaySourceIdentitySampleMs);
#else
    (void)track;
    (void)displaySlot;
    (void)requestedStart;
    (void)requestedLength;
    (void)publishedStart;
    (void)publishedLength;
    (void)drawnNotes;
    (void)retained;
    (void)discardReason;
    (void)keyStartTick;
    (void)keyLengthTicks;
#endif
}

DISP_COLD_MEM const DisplayNoteVec& DisplayManager::resolveWindowedDisplayNotes(
    const Track& track, Loop& mutLoop, const Loop& loop, uint8_t displaySlot, uint32_t loopLength,
    uint32_t windowStart, uint32_t windowLength) {
    const uint8_t trackIndex = resolveTrackIndex(track);
    const bool sealedHandoffActive =
        DisplayWindowUtils::shouldBypassWindowedVisualCacheForSealedCapturePreview(
            loop.hasSealedCapturePreview(), loop.sealedCapturePreviewIsCovered());

    // Prefer filtering visualCache over gather+reconstruct when capture is idle and the
    // cache covers the paint neighborhood. A nonempty dirty prefix must not win — after
    // record stop the first idle slice left 20 notes and later bars never painted
    // (session_20260825_235954). The fast filter has no sealed-preview overlay,
    // so it must also yield during the post-stop handoff (`221700`).
    if (!loop.captureActive() && !sealedHandoffActive &&
        visualCacheCoversFollowWindow(loop, windowStart, windowLength, loopLength)) {
        const bool visualCacheHit =
            liveWindowGatherValid_ && displaySlot == livePlaybackDisplaySlot_ &&
            trackIndex == livePlaybackDisplayTrack_ &&
            liveMergePlaybackRevision_ == loop.playbackRevision &&
            liveMergeCaptureRevision_ == loop.captureDisplayRevision &&
            liveWindowVisualCacheRevision_ == loop.visualCache.revision &&
            liveWindowGatherLoopLength_ == loopLength &&
            DisplayWindowUtils::paintWindowInsideGather(windowStart, windowLength,
                                                        liveWindowGatherStart_,
                                                        liveWindowGatherLength_);
        if (visualCacheHit) {
            DIAG_COUNTER_INC(DisplayIncrementalUpdate);
            return liveDisplayNotes;
        }
        const uint32_t displayBuildStartUs = micros();
        DIAG_COUNTER_INC(DisplayIncrementalUpdate);
        uint32_t gatherStart = 0;
        uint32_t gatherLength = 0;
        filterNotesToFollowWindow(loop.visualCache.notes, windowStart, windowLength, loopLength,
                                  liveDisplayNotes, gatherStart, gatherLength);
        liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
        liveMergePlaybackRevision_ = loop.playbackRevision;
        liveMergeCaptureRevision_ = loop.captureDisplayRevision;
        livePlaybackDisplaySlot_ = displaySlot;
        livePlaybackDisplayTrack_ = trackIndex;
        liveWindowGatherStart_ = gatherStart;
        liveWindowGatherLength_ = gatherLength;
        liveWindowGatherLoopLength_ = loopLength;
        liveWindowGatherValid_ = true;
        liveWindowVisualCacheRevision_ = loop.visualCache.revision;
        DIAG_TIMING_RECORD(DisplayBuild, micros() - displayBuildStartUs);
        return liveDisplayNotes;
    }

    const bool cacheHit = !sealedHandoffActive &&
        liveWindowGatherValid_ && displaySlot == livePlaybackDisplaySlot_ &&
        trackIndex == livePlaybackDisplayTrack_ &&
        liveMergePlaybackRevision_ == loop.playbackRevision &&
        liveMergeCaptureRevision_ == loop.captureDisplayRevision &&
        liveWindowVisualCacheRevision_ == UINT32_MAX &&
        liveWindowGatherLoopLength_ == loopLength &&
        DisplayWindowUtils::paintWindowInsideGather(windowStart, windowLength,
                                                    liveWindowGatherStart_,
                                                    liveWindowGatherLength_);
    if (cacheHit) {
        DIAG_COUNTER_INC(DisplayIncrementalUpdate);
        return liveDisplayNotes;
    }

    const uint32_t displayBuildStartUs = micros();
    DIAG_COUNTER_INC(DisplayFullRebuild);
    const uint32_t marginTicks =
        static_cast<uint32_t>(kWindowedGatherMarginBars) * Config::TICKS_PER_BAR;
    uint32_t gatherStart = 0;
    uint32_t gatherLength = 0;
    DisplayWindowUtils::expandWindowWithMargin(windowStart, windowLength, loopLength, marginTicks,
                                               gatherStart, gatherLength);
    rebuildDisplayNotesInWindow(mutLoop, loop, loopLength, gatherStart, gatherLength,
                                liveDisplayEventBuffer, liveDisplayNotes, true);
    liveDisplayCacheCommittedNoteCount_ = liveDisplayNotes.size();
    if (sealedHandoffActive) {
        // `rebuildDisplayNotesInWindow` materializes committed content only once
        // capture has closed. Retain the sealed rows as an overlay until cache
        // membership proves them covered; otherwise the first PLAYING frame can
        // transfer an empty/stale detailed window after record stop.
        for (const NoteUtils::DisplayNote& sealedRow : loop.sealedCapturePreviewNotes()) {
            if (DisplayWindowUtils::noteIntersectsWindow(
                    sealedRow.startTick, sealedRow.endTick, gatherStart, gatherLength,
                    loopLength)) {
                DisplayWindowUtils::upsertDisplayNoteAtPitchStart(liveDisplayNotes, sealedRow);
            }
        }
    }
    liveMergePlaybackRevision_ = loop.playbackRevision;
    liveMergeCaptureRevision_ = loop.captureDisplayRevision;
    livePlaybackDisplaySlot_ = displaySlot;
    livePlaybackDisplayTrack_ = trackIndex;
    liveWindowGatherStart_ = gatherStart;
    liveWindowGatherLength_ = gatherLength;
    liveWindowGatherLoopLength_ = loopLength;
    liveWindowGatherValid_ = true;
    liveWindowVisualCacheRevision_ = UINT32_MAX;
    DIAG_TIMING_RECORD(DisplayBuild, micros() - displayBuildStartUs);
    return liveDisplayNotes;
}

namespace {

const EditPass* findActiveSealedCompanionRow(const Loop& loop, EditPassId id) {
    for (const EditPass& row : loop.passes.editPasses) {
        if (row.id == id && row.state == EditPassState::Active) {
            return &row;
        }
    }
    return nullptr;
}

bool previewNoteIntersectsBar(const NoteUtils::DisplayNote& note, uint32_t bar,
                              uint32_t loopLength) {
    const uint32_t barStart = bar * Config::TICKS_PER_BAR;
    return DisplayWindowUtils::noteIntersectsWindow(note.startTick, note.endTick, barStart,
                                                    Config::TICKS_PER_BAR, loopLength);
}

void appendSnapshotNotesForBar(const LoopSnapshot& snapshot, uint32_t bar, uint8_t pitchLow,
                               uint8_t pitchHigh, NoteUtils::DisplayNoteVec& out) {
    const uint8_t lastPitch =
        pitchHigh < kLoopSnapshotPitchCount ? pitchHigh
                                            : static_cast<uint8_t>(kLoopSnapshotPitchCount - 1);
    for (uint16_t pitch = pitchLow; pitch <= lastPitch; ++pitch) {
        const auto membership = snapshot.noteMembership(static_cast<uint8_t>(pitch), bar);
        forEachSnapshotMembershipReference(
            membership.get(), [&out](const LoopSnapshotNoteRef& reference) {
                if (!reference) {
                    return;
                }
                appendDisplayNoteUniqueById(out, displayNoteFromSnapshotNote(*reference));
            });
    }
}

void appendNotesIntersectingBar(const NoteUtils::DisplayNoteVec& source, uint32_t bar,
                                uint32_t loopLength, NoteUtils::DisplayNoteVec& out) {
    for (const NoteUtils::DisplayNote& note : source) {
        if (previewNoteIntersectsBar(note, bar, loopLength)) {
            appendDisplayNoteUniqueById(out, note);
        }
    }
}

DISP_CAPTURE_MEM void reuseCommittedDrawSourcePrefix(const NoteUtils::DisplayNoteVec& prefix,
                                                     NoteUtils::DisplayNoteVec& out) {
    out.clear();
    if (prefix.empty()) {
        return;
    }
    out.insert(out.end(), prefix.begin(), prefix.end());
}

DISP_CAPTURE_MEM void storeCommittedDrawSourcePrefix(const NoteUtils::DisplayNoteVec& gathered,
                                                     size_t committedEnd,
                                                     NoteUtils::DisplayNoteVec& prefixOut) {
    prefixOut.clear();
    if (committedEnd > gathered.size()) {
        committedEnd = gathered.size();
    }
    if (committedEnd == 0) {
        return;
    }
    prefixOut.insert(prefixOut.end(), gathered.begin(),
                     gathered.begin() + static_cast<std::ptrdiff_t>(committedEnd));
}

/// Session-exit gap handoff during capture keeps the prior composed frame visible,
/// but the committed prefix must stay overlay-free. `033502` folded preview rows
/// into the prefix and Reuse re-appended them every frame (2890 rows / 359 unique).
DISP_CAPTURE_MEM void retainDrawSourceHandoffCommittedPrefix(
    const NoteUtils::DisplayNoteVec& handoffFrame, size_t gatheredCommittedEnd,
    size_t priorCommittedEnd, NoteUtils::DisplayNoteVec& prefixOut, size_t& committedEndOut) {
    const size_t committedOnlyEnd =
        gatheredCommittedEnd > priorCommittedEnd ? gatheredCommittedEnd : priorCommittedEnd;
    storeCommittedDrawSourcePrefix(handoffFrame, committedOnlyEnd, prefixOut);
    committedEndOut = committedOnlyEnd;
}


// Record previews use growing linear coordinates. Clip the existing representation to final
// geometry; never fold a trimmed partial-bar row into bar one.
void appendRecordStopPreviewForBar(const CapturePreview& preview, uint32_t bar,
                                    uint32_t loopLength, uint32_t closureTick,
                                    NoteUtils::DisplayNoteVec& out) {
    for (const auto& source : preview.notes) {
        if (source.startTick >= loopLength) continue;
        NoteUtils::DisplayNote note = source;
        if (DisplayWindowUtils::capturePreviewNoteIsOpenHold(note.startTick, note.endTick))
            note.endTick = closureTick;
        else if (note.endTick >= loopLength)
            note.endTick = loopLength - 1;
        if (previewNoteIntersectsBar(note, bar, loopLength)) appendDisplayNoteUniqueById(out, note);
    }
}

namespace {
/// The committed base already owns this preview row's identity (same NoteId and same start): a stop or
/// live preview must not append a second copy of a note whose own pass is committed. Different starts are
/// the wrap-head split of one NoteId and must still append.
bool committedBaseOwnsPreviewRow(const NoteUtils::DisplayNoteVec& out, size_t committedEnd,
                                 const NoteUtils::DisplayNote& row) {
    if (row.noteId == kInvalidNoteId) return false;
    const size_t end = committedEnd < out.size() ? committedEnd : out.size();
    for (size_t i = 0; i < end; ++i) {
        if (out[i].noteId == row.noteId && out[i].startTick == row.startTick) return true;
    }
    return false;
}
}  // namespace

DISP_CAPTURE_MEM void gatherCommittedDrawSourceBars(const Track& track, uint8_t slot, const Loop& loop,
                                                    uint32_t firstBar, uint32_t lastBar, uint8_t pitchLow,
                                                    uint8_t pitchHigh, NoteUtils::DisplayNoteVec& out,
                                                    size_t& committedEnd) {
    const uint32_t loopLength = loop.loopLengthTicks;
    if (loopLength == 0 || firstBar > lastBar) {
        committedEnd = out.size();
        return;
    }
    const ImmutableLoopSnapshotRef snapshot = loop.publishedSnapshot();
    const bool rootStale = static_cast<bool>(snapshot) && !loop.publishedSnapshotIsCurrent();
    const bool requestedSnapshot = DisplayWindowUtils::shouldGatherPublishedSnapshotForDrawSource(
        static_cast<bool>(snapshot), loop.captureActive(), loop.hasOverdubSession(),
        !loop.visualCache.notes.empty(), loop.visualCacheDirty,
        loop.publishedSnapshotIsCurrent());
    // A **stale** published root may never be the committed draw source: the committed passes are
    // authority and roots are acceleration (chain-48: passes advanced while publication lagged, and the
    // stale root blanked every committed overdub row outside a live preview). Every other selection is
    // unchanged. A cache **built from passes** stays a qualified source (the partial-cache contract);
    // a cache built from a root mirrors that root and is not.
    const bool cacheIsPassDerived =
        !loop.visualCache.notes.empty() && loop.visualCache.builtFromSnapshotIdentity == nullptr;
    const bool usePublishedSnapshot = requestedSnapshot && !rootStale;
    const uint8_t resolvedPitchHigh = pitchHigh < pitchLow ? pitchLow : pitchHigh;
    uint32_t closureTick = loopLength - 1;
    const CapturePreview* recordPreview = track.recordStopPreviewForSlot(slot, &closureTick);
    const uint32_t loopBars = Config::TICKS_PER_BAR > 0
                                  ? (loopLength + Config::TICKS_PER_BAR - 1u) / Config::TICKS_PER_BAR
                                  : 0;
    if (rootStale && requestedSnapshot && recordPreview == nullptr && !cacheIsPassDerived) {
        // Window-local, wrap-aware gather over the authoritative committed passes.
        const uint32_t windowStart = firstBar * Config::TICKS_PER_BAR;
        const uint32_t windowLength = (lastBar - firstBar + 1u) * Config::TICKS_PER_BAR;
        NoteUtils::DisplayNoteVec passNotes;
        LoopContentResolution::resolveDisplayNotesInWindow(loop.passes, loopLength, windowStart,
                                                           windowLength, passNotes);
        ++gDrawSourceCommittedPassGathers;
        // This gather may append to a retained prefix (the one-bar slide gather). A committed row the
        // prefix already carries must not be appended twice.
        for (const NoteUtils::DisplayNote& note : passNotes) {
            bool present = false;
            for (const NoteUtils::DisplayNote& existing : out) {
                if (existing.noteId == note.noteId && existing.startTick == note.startTick) {
                    present = true;
                    break;
                }
            }
            if (!present) out.push_back(note);
        }
        committedEnd = out.size();
        return;
    }
    for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
        if (usePublishedSnapshot) {
            // The window may cross the loop end (one generation, two physical spans), so a logical
            // bar beyond the loop resolves to its physical bar instead of an empty membership.
            const uint32_t physicalBar = loopBars == 0 ? 0u : bar % loopBars;
            appendSnapshotNotesForBar(*snapshot, physicalBar, pitchLow, resolvedPitchHigh, out);
        } else if (recordPreview) {
            appendRecordStopPreviewForBar(*recordPreview, bar, loopLength, closureTick, out);
        } else {
            appendNotesIntersectingBar(loop.visualCache.notes, bar, loopLength, out);
        }
    }
    // Membership RC-A (`handoffLiveRecordProgressToCommittedPass`) keeps
    // bar-crossing NoteRefs in the published snapshot. Do not re-heal draw
    // source from visual/events here (consumer Stage 6 unwind).
    committedEnd = out.size();
}

DISP_CAPTURE_MEM void applyDrawSourceOverlays(const Track& track, uint8_t slot, const Loop& loop,
                                              uint32_t firstBar, uint32_t lastBar,
                                              NoteUtils::DisplayNoteVec& out, size_t committedEnd) {
    const uint32_t loopLength = loop.loopLengthTicks;
    if (loopLength == 0 || firstBar > lastBar) {
        return;
    }
    const bool captureActive = loop.captureActive();
    if (DisplayWindowUtils::shouldUnionSealedCapturePreviewForDrawSource(
            loop.hasSealedCapturePreview(), loop.sealedCapturePreviewIsCovered(), captureActive,
            loop.hasOverdubSession())) {
        for (const NoteUtils::DisplayNote& sealedRow : loop.sealedCapturePreviewNotes()) {
            if (committedBaseOwnsPreviewRow(out, committedEnd, sealedRow)) continue;
            for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
                if (!previewNoteIntersectsBar(sealedRow, bar, loopLength)) {
                    continue;
                }
                DisplayWindowUtils::upsertDisplayNoteAtPitchStartFrom(out, committedEnd, sealedRow,
                                                                     captureActive);
                break;
            }
        }
    }
    if (loop.retiredSealedDisplayNeededForFrame()) {
        for (const NoteUtils::DisplayNote& retiredRow : loop.retiredSealedDisplayNotes()) {
            if (committedBaseOwnsPreviewRow(out, committedEnd, retiredRow)) continue;
            for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
                if (!previewNoteIntersectsBar(retiredRow, bar, loopLength)) {
                    continue;
                }
                DisplayWindowUtils::upsertDisplayNoteAtPitchStartFrom(out, committedEnd, retiredRow,
                                                                     captureActive);
                break;
            }
        }
    }
    // Between overdub passes, capture has been closed while the session still
    // owns its preview rows. Keep composing that tier until session close;
    // otherwise a current snapshot alone can erase the just-recorded row.
    if (!captureActive && !loop.hasOverdubSession() && track.captureStopPreviewCount() == 0 &&
        track.inputClosedOverdubPreviewForSlot(slot, nullptr) == nullptr) {
      return;
    }
    const auto appendPreview = [&](const CapturePreview& preview, bool frozen, uint32_t closureTick) {
        for (size_t index = 0; index < preview.notes.size(); ++index) {
            NoteUtils::DisplayNote previewNote = preview.notes[index];
            if (frozen && index < preview.noteStates.size() && preview.noteStates[index].open)
                previewNote.endTick = closureTick;
            bool intersectsWindow = false;
            for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
                if (previewNoteIntersectsBar(previewNote, bar, loopLength)) {
                    intersectsWindow = true;
                    break;
                }
            }
            if (!intersectsWindow) continue;
            if (DisplayWindowUtils::capturePreviewNoteIsOpenHold(previewNote.startTick,
                                                                 previewNote.endTick)) {
                appendDisplayNoteUniqueById(out, previewNote);
            } else {
                if (committedBaseOwnsPreviewRow(out, committedEnd, previewNote)) continue;
                DisplayWindowUtils::upsertDisplayNoteAtPitchStartFrom(out, committedEnd, previewNote,
                                                                     true);
            }
        }
    };
    // All admitted intervals retain their own previews. The single Loop borrow cannot represent
    // both sealed B and sealed C while A still owns canonical finalization.
    const size_t count = track.captureStopPreviewCount();
    for (size_t ordinal = 0; ordinal < count; ++ordinal) {
        uint32_t closureTick = 0;
        if (const CapturePreview* preview = track.captureStopPreviewForSlot(slot, ordinal, &closureTick)) {
            if (preview != track.recordStopPreviewForSlot(slot)) appendPreview(*preview, true, closureTick);
        }
    }
    // DEC-233 decision 5: an input-closed Stop that has not bound yet keeps its producer preview as a
    // **closed** source -- original geometry, its own closure phase, pending transforms intact -- never a
    // live playhead tail. Once the job owns the preview the tier above serves it, so the handoff cannot
    // change what the panel shows.
    uint32_t inputClosedClosureTick = 0;
    if (const CapturePreview* closedPreview =
            track.inputClosedOverdubPreviewForSlot(slot, &inputClosedClosureTick)) {
        appendPreview(*closedPreview, true, inputClosedClosureTick);
    } else {
        appendPreview(loop.capturePreview, false, 0);
        if (count == 0 && &loop.capturePreviewView() != &loop.capturePreview)
            appendPreview(loop.capturePreviewView(), false, 0);
    }
}

uint32_t fingerprintPendingNoteChanges(const PendingNoteChangeVec& changes) {
    uint32_t generation = static_cast<uint32_t>(changes.size());
    for (const PendingNoteChange& change : changes) {
        generation ^= static_cast<uint32_t>(change.noteId) + 0x9e3779b9u + (generation << 6) +
                      (generation >> 2);
        generation ^= static_cast<uint32_t>(change.kind);
        generation ^= change.startTick;
        generation ^= change.endTick;
    }
    return generation;
}

void applyPendingDrawSourceTransforms(const Loop& loop, NoteUtils::DisplayNoteVec& notes,
                                      size_t committedDisplayEnd) {
    if (loop.hasPendingNoteChanges() || loop.hasSealedCompanionOverlay()) {
        loop.applyPendingNoteChangesToDisplayNotes(notes, committedDisplayEnd);
    }
}

}  // namespace

namespace DisplayManagerInternal {

#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_DRAW_SRC
DISP_CAPTURE_MEM void emitPendingShortenSourceKind(
    NoteId noteId, DisplayWindowUtils::PendingShortenSourceKind kind,
    const NoteUtils::DisplayNote* visualHint, const NoteUtils::DisplayNote* authoritative) {
    const char* kindName = "not_needed";
    if (kind == DisplayWindowUtils::PendingShortenSourceKind::RecoverAuthoritative) {
        kindName = "stale_visual_source";
    } else if (kind == DisplayWindowUtils::PendingShortenSourceKind::AuthoritativeAbsence) {
        kindName = "authoritative_absence";
    }
    char line[192];
    snprintf(line, sizeof(line),
             "#CAP,%lu,DIAG,draw_src,shorten_src,id,%u,kind,%s,old,%u,%u,%u,new,%u,%u,%u",
             static_cast<unsigned long>(micros()), static_cast<unsigned>(noteId), kindName,
             visualHint != nullptr ? static_cast<unsigned>(visualHint->note) : 0u,
             visualHint != nullptr ? static_cast<unsigned>(visualHint->startTick) : 0u,
             visualHint != nullptr ? static_cast<unsigned>(visualHint->endTick) : 0u,
             authoritative != nullptr ? static_cast<unsigned>(authoritative->note) : 0u,
             authoritative != nullptr ? static_cast<unsigned>(authoritative->startTick) : 0u,
             authoritative != nullptr ? static_cast<unsigned>(authoritative->endTick) : 0u);
    DebugSessionCapture::appendCaptureTextLine(line);
}
#endif

DISP_CAPTURE_MEM bool lookupAuthoritativeCommittedShortenRow(
    const Loop& loop, NoteId noteId, uint8_t pitch, const NoteUtils::DisplayNote* visualHint,
    NoteUtils::DisplayNote& out) {
    if (noteId == kInvalidNoteId) {
        return false;
    }
    if (const ImmutableLoopSnapshotRef snapshot = loop.publishedSnapshot()) {
        if (const LoopSnapshotNoteRef reference = snapshot->findNote(noteId, pitch)) {
            out = displayNoteFromSnapshotNote(*reference);
            return true;
        }
    }
    // The overlap domain (`overdubSourceViewNotes_`) is not a display source: it is touched coverage and
    // may legitimately omit a committed note. The snapshot is authoritative when current; otherwise the
    // bounded two-bar committed reconstruct below answers without reading the overlap view.
    if (visualHint == nullptr || loop.loopLengthTicks == 0 || Config::TICKS_PER_BAR == 0) {
        return false;
    }
    const uint32_t barTicks = Config::TICKS_PER_BAR;
    const uint32_t barStart = (visualHint->startTick / barTicks) * barTicks;
    SessionMidiEventVec flat;
    loop.copyEffectiveCommittedEventsInRange(flat, barStart, barTicks * 2u, true);
    const NoteUtils::DisplayNoteVec reconstructed =
        NoteUtils::reconstructDisplayNotes(flat, loop.loopLengthTicks, false, false);
    if (const NoteUtils::DisplayNote* found =
            DisplayWindowUtils::findDisplayNoteByNoteId(reconstructed, noteId)) {
        out = *found;
        return true;
    }
    return false;
}

DISP_CAPTURE_MEM bool recoverMissingPendingShortenSources(const Loop& loop,
                                                          NoteUtils::DisplayNoteVec& compose,
                                                          size_t& committedEnd) {
    if (!loop.hasPendingNoteChanges()) {
        return false;
    }
    bool recoveredAny = false;
    for (const PendingNoteChange& change : loop.pendingNoteChanges()) {
        if (change.kind != PendingNoteChangeKind::Shorten || change.noteId == kInvalidNoteId) {
            continue;
        }
        const bool composeHasTarget =
            DisplayWindowUtils::displayNotesContainNoteId(compose, change.noteId);
        const NoteUtils::DisplayNote* visualHint =
            DisplayWindowUtils::findDisplayNoteByNoteId(loop.visualCache.notes, change.noteId);
        NoteUtils::DisplayNote authoritative{};
        const bool authoritativeHasTarget =
            lookupAuthoritativeCommittedShortenRow(loop, change.noteId, change.pitch, visualHint,
                                                   authoritative);
        const DisplayWindowUtils::PendingShortenSourceKind kind =
            DisplayWindowUtils::classifyPendingShortenSource(composeHasTarget, visualHint != nullptr,
                                                             authoritativeHasTarget);
        if (kind == DisplayWindowUtils::PendingShortenSourceKind::RecoverAuthoritative) {
            if (!DisplayWindowUtils::displayNotesContainNoteId(compose, change.noteId)) {
                DisplayWindowUtils::admitRecoveredCommittedShortenRow(compose, committedEnd,
                                                                      authoritative);
            }
            recoveredAny = true;
        }
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_DRAW_SRC
        if (kind != DisplayWindowUtils::PendingShortenSourceKind::NotNeeded) {
            emitPendingShortenSourceKind(change.noteId, kind, visualHint,
                                         authoritativeHasTarget ? &authoritative : nullptr);
        }
#endif
    }
    return recoveredAny;
}

DISP_CAPTURE_MEM bool recoverMissingSealedShortenSources(const Loop& loop,
                                                         NoteUtils::DisplayNoteVec& compose,
                                                         size_t& committedEnd) {
    if (!loop.hasSealedCompanionOverlay()) {
        return false;
    }
    std::vector<NoteId> shortenIds;
    NoteUtils::DisplayNoteVec authoritative;
    shortenIds.reserve(loop.sealedCompanionOverlayIds().size());
    authoritative.reserve(loop.sealedCompanionOverlayIds().size());
    for (const EditPassId id : loop.sealedCompanionOverlayIds()) {
        const EditPass* row = findActiveSealedCompanionRow(loop, id);
        if (row == nullptr || row->targetNoteId == kInvalidNoteId) {
            continue;
        }
        if (row->actionType != EditActionType::Update ||
            row->propertyType != EditPropertyType::Length) {
            continue;
        }
        const NoteUtils::DisplayNote* visualHint =
            DisplayWindowUtils::findDisplayNoteByNoteId(loop.visualCache.notes,
                                                        row->targetNoteId);
        NoteUtils::DisplayNote authoritativeRow{};
        if (!lookupAuthoritativeCommittedShortenRow(loop, row->targetNoteId, row->pitch,
                                                    visualHint, authoritativeRow)) {
            // `235546` id 76: snapshot/source-view/visual can miss later wrap-1
            // NoteIds while the first sealed Length is still findable. The sealed
            // Length row is the committed shorten geometry — same payload stop
            // effective already keeps.
            authoritativeRow.noteId = row->targetNoteId;
            authoritativeRow.note = row->pitch;
            authoritativeRow.velocity = row->velocity;
            authoritativeRow.startTick = row->startTick;
            authoritativeRow.endTick = row->endTick;
        }
        shortenIds.push_back(row->targetNoteId);
        authoritative.push_back(authoritativeRow);
    }
    if (shortenIds.empty()) {
        return false;
    }
    const DisplayWindowUtils::PendingShortenRecoveryCounts counts =
        DisplayWindowUtils::recoverMissingPendingShortenRows(compose, loop.visualCache.notes,
                                                             shortenIds.data(), shortenIds.size(),
                                                             authoritative, &committedEnd);
    if (counts.recovered == 0) {
        return false;
    }
    return true;
}

DISP_CAPTURE_MEM void emitPaintedPendingShortenDiagnostics(const Loop& loop,
                                                          const NoteUtils::DisplayNoteVec& compose,
                                                          const NoteUtils::DisplayNoteVec& paint) {
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_DRAW_SRC
    if (!loop.hasPendingNoteChanges()) {
        return;
    }
    const uint32_t generation = fingerprintPendingNoteChanges(loop.pendingNoteChanges());
    static uint32_t sLastPaintedShortenGeneration = 0;
    if (generation == sLastPaintedShortenGeneration) {
        return;
    }
    bool emitted = false;
    for (const PendingNoteChange& change : loop.pendingNoteChanges()) {
        if (change.kind != PendingNoteChangeKind::Shorten || change.noteId == kInvalidNoteId) {
            continue;
        }
        const DisplayWindowUtils::PaintedPendingShorten described =
            DisplayWindowUtils::describePaintedPendingShorten(compose, paint, change.noteId,
                                                             change.endTick);
        if (!described.paintHasShortenedEnd) {
            continue;
        }
        char line[192];
        snprintf(line, sizeof(line),
                 "#CAP,%lu,DIAG,draw_src,painted_pending_shorten,id,%u,old,%u,%u,%u,new,%u,%u,%u",
                 static_cast<unsigned long>(micros()), static_cast<unsigned>(change.noteId),
                 described.composeHasTarget ? static_cast<unsigned>(change.pitch) : 0u,
                 static_cast<unsigned>(described.composeStartTick),
                 static_cast<unsigned>(described.composeEndTick),
                 static_cast<unsigned>(change.pitch),
                 static_cast<unsigned>(described.paintStartTick),
                 static_cast<unsigned>(described.paintEndTick));
        DebugSessionCapture::appendCaptureTextLine(line);
        emitted = true;
    }
    if (emitted) {
        sLastPaintedShortenGeneration = generation;
    }
#else
    (void)loop;
    (void)compose;
    (void)paint;
#endif
}

}  // namespace DisplayManagerInternal

namespace {

#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
DISP_CAPTURE_MEM void emitPaintedOccupy193815Row(const char* stage,
                                                 const Loop& loop,
                                                 const NoteUtils::DisplayNoteVec& notes) {
    if (!loop.hasPendingNoteChanges() || stage == nullptr) {
        return;
    }
    constexpr uint8_t kPitch = 63;
    constexpr uint32_t kStartTick = 1536;
    uint32_t present = 0;
    int32_t noteId = -1;
    uint32_t endTick = 0;
    for (const NoteUtils::DisplayNote& note : notes) {
        if (note.note == kPitch && note.startTick == kStartTick) {
            present = 1;
            noteId = static_cast<int32_t>(note.noteId);
            endTick = note.endTick;
            break;
        }
    }
    char line[176];
    snprintf(line, sizeof(line),
             "#CAP,%lu,DIAG,draw_src,bug2,%s,p,63,s,1536,present,%u,id,%ld,end,%u",
             static_cast<unsigned long>(micros()), stage, static_cast<unsigned>(present),
             static_cast<long>(noteId), static_cast<unsigned>(endTick));
    DebugSessionCapture::appendCaptureTextLine(line);
}
#endif

#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
/// Painted open-tail attribution (2026-10-30): names the branch that produced an open preview row's
/// painted end, so the wrap-split (`tailEnd = loopLength - 1`) and the window-slide shapes are read
/// from the device instead of inferred. Rate-limited; at most four open rows per emission.
void emitPaintedOpenTailRows(const Loop& loop, uint32_t closeTick, uint32_t windowStart,
                             const NoteUtils::DisplayNoteVec& painted) {
    if (loop.capturePreview.openNoteIndices.empty() || loop.loopLengthTicks == 0) {
        return;
    }
    static uint32_t lastOpenTailRowMs = 0;
    const uint32_t nowMs = millis();
    if (lastOpenTailRowMs != 0 && nowMs - lastOpenTailRowMs < 100u) {
        return;
    }
    lastOpenTailRowMs = nowMs;
    uint32_t emitted = 0;
    for (const uint32_t index : loop.capturePreview.openNoteIndices) {
        if (index >= loop.capturePreview.notes.size() || emitted >= 4u) {
            break;
        }
        const NoteUtils::DisplayNote& open = loop.capturePreview.notes[index];
        uint32_t paintedEnd = open.endTick;
        for (const NoteUtils::DisplayNote& row : painted) {
            if (row.noteId == open.noteId) {
                paintedEnd = row.endTick;
                break;
            }
        }
        const bool split = NoteUtils::isLiveWrapHeadContinuationDisplay(
            open.startTick, closeTick, loop.loopLengthTicks, true);
        char line[192];
        snprintf(line, sizeof(line),
                 "#CAP,%lu,DIAG,draw_src,tail,id,%u,s,%u,e,%u,close,%lu,win,%lu,split,%u,len,%lu",
                 (unsigned long)micros(), static_cast<unsigned>(open.noteId),
                 static_cast<unsigned>(open.startTick), static_cast<unsigned>(paintedEnd),
                 (unsigned long)closeTick, (unsigned long)windowStart, split ? 1u : 0u,
                 (unsigned long)loop.loopLengthTicks);
        DebugSessionCapture::appendCaptureTextLine(line);
        ++emitted;
    }
}
#endif

DisplayWindowUtils::DrawSourceGatherKey makeDrawSourceGatherKey(
    const Track& track, const Loop& loop, uint8_t displaySlot, uint32_t windowStart,
    uint32_t windowLength, uint32_t playheadBar, uint8_t pitchLow, uint8_t pitchHigh) {
    DisplayWindowUtils::DrawSourceGatherKey key;
    key.slot = displaySlot;
    key.track = 255;
    key.pitchLow = pitchLow;
    key.pitchHigh = pitchHigh;
    key.windowStart = windowStart;
    key.windowLength = windowLength;
    key.playbackRevision = loop.playbackRevision;
    key.captureDisplayRevision = loop.captureDisplayRevision;
    key.capturePreviewRevision = loop.capturePreviewView().revision;
    key.visualCacheRevision = loop.visualCache.revision;
    key.publishedSnapshotRevision = loop.publishedSnapshotRevision();
    key.publishedSnapshotIdentity = loop.publishedSnapshotIdentity();
    if (DisplayWindowUtils::shouldUnionSealedCapturePreviewForDrawSource(
            loop.hasSealedCapturePreview(), loop.sealedCapturePreviewIsCovered(),
            loop.captureActive(), loop.hasOverdubSession())) {
        key.sealedPreviewRevision = loop.playbackRevision;
        const size_t sealedCount = loop.sealedCapturePreviewNotes().size();
        key.sealedPreviewCount =
            static_cast<uint16_t>(sealedCount > 0xFFFFu ? 0xFFFFu : sealedCount);
    } else {
        key.sealedPreviewRevision = 0;
        key.sealedPreviewCount = 0;
    }
    key.pendingChangeGeneration = fingerprintPendingNoteChanges(loop.pendingNoteChanges());
    key.playheadBar = playheadBar;
    key.snapshotCurrent = loop.publishedSnapshotIsCurrent() ? 1 : 0;
    key.sealedCompanionOverlay = loop.hasSealedCompanionOverlay() ? 1 : 0;
    key.valid = 1;
    (void)track;
    return key;
}

}  // namespace

DISP_CAPTURE_MEM const DisplayNoteVec& DisplayManager::resolveDisplayNotesByDrawSource(
    const Track& track, uint8_t displaySlot, uint32_t currentTick, uint8_t pitchLow,
    uint8_t pitchHigh) {
    const Loop& loop = track.getLoop(displaySlot);
    const uint32_t loopLength = resolveDisplayLoopLength(track, displaySlot, currentTick);
    if (loopLength == 0) {
        liveDisplayNotes.clear();
        liveDisplayCommittedPrefix_.clear();
        liveDrawSourceKey_ = {};
        livePlaybackDisplaySlot_ = 255;
        livePlaybackDisplayTrack_ = 255;
        return liveDisplayNotes;
    }

    uint32_t windowStart = 0;
    uint32_t windowLength = loopLength;
    uint8_t windowBars = 0;
    // The hold may substitute the retained interval for the requested one; C5.6 needs that fact explicitly.
    bool heldRetainedContent = false;
    syncDetailedPaintWindow(track, displaySlot, currentTick, loopLength, windowStart, windowLength,
                            windowBars, &heldRetainedContent);
    if (windowLength == 0) {
        windowStart = 0;
        windowLength = loopLength;
    }

    const uint32_t playheadTick = resolvePlayheadInLoop(track, displaySlot, currentTick);
    const uint32_t playheadBar =
        Config::TICKS_PER_BAR > 0 ? playheadTick / Config::TICKS_PER_BAR : 0;
    uint32_t firstBar =
        Config::TICKS_PER_BAR > 0 ? windowStart / Config::TICKS_PER_BAR : 0;
    const uint32_t lastTick = windowStart + windowLength > 0 ? windowStart + windowLength - 1 : 0;
    uint32_t lastBar = Config::TICKS_PER_BAR > 0 ? lastTick / Config::TICKS_PER_BAR : 0;
    const uint8_t resolvedPitchLow = pitchLow;
    const uint8_t resolvedPitchHigh = pitchHigh < pitchLow ? pitchLow : pitchHigh;
    const uint8_t trackIndex = resolveTrackIndex(track);

    DisplayWindowUtils::DrawSourceGatherKey currentKey = makeDrawSourceGatherKey(
        track, loop, displaySlot, windowStart, windowLength, playheadBar, resolvedPitchLow,
        resolvedPitchHigh);
    currentKey.track = trackIndex;

    DisplayWindowUtils::DrawSourceReuseKind reuseKind =
        DisplayWindowUtils::selectDrawSourceReuseKind(liveDrawSourceKey_, currentKey,
                                                      Config::TICKS_PER_BAR, loopLength);
    const bool canReuseCommittedPrefix = !liveDisplayCommittedPrefix_.empty();
    const bool canSlideCommittedPrefix =
        reuseKind == DisplayWindowUtils::DrawSourceReuseKind::SlideCommittedDrawSource &&
        canReuseCommittedPrefix;
    const bool preservedHandoffAuthority =
        !liveDisplayNotes.empty() && displaySlot == livePlaybackDisplaySlot_ &&
        trackIndex == livePlaybackDisplayTrack_;
    const CapturePreview* recordPreview = track.recordStopPreviewForSlot(displaySlot);
    const bool sameHandoffWindow = liveDrawSourceKey_.valid != 0 &&
        liveDrawSourceKey_.track == trackIndex && liveDrawSourceKey_.slot == displaySlot &&
        liveDrawSourceKey_.windowStart == windowStart && liveDrawSourceKey_.windowLength == windowLength;
    const bool holdSessionExitGapHandoff =
        DisplayWindowUtils::shouldHoldSessionExitPublicationGapHandoff(
            preservedHandoffAuthority, loop.publishedSnapshotIsCurrent(),
            displaySourcePublicationPending(track, loop, displaySlot)) &&
        (sameHandoffWindow || recordPreview == nullptr);

    // C5.6 composition contract. A retained frame is authoritative **only** over its recorded coverage and
    // its own generation, so when the requested interval leaves that coverage the retained rows may not be
    // presented for it -- and no source may be stretched to cover the difference. The requested interval is
    // instead served by a provider that proves its own qualification for *this* interval at *this* generation
    // (`Loop::collectQualifiedCommittedDisplayNotes`), composed with the recognised live overlay once. The
    // retained frame remains the whole-frame fallback only where no such provider exists (the honest hole);
    // the two are never stitched into one frame, so no cross-generation frame can be built.
    const uint32_t requestedStartTick = detailedWindowRequestedSlot_ == displaySlot
                                            ? detailedWindowRequestedStartTick_
                                            : windowStart;
    const uint32_t requestedLengthTicks = detailedWindowRequestedSlot_ == displaySlot
                                              ? detailedWindowRequestedLengthTicks_
                                              : windowLength;
    DisplayWindowUtils::DrawSourceComposeScratch<DisplayNoteVec> composeScratch(
        liveDisplayPendingPaintNotes_);
    DisplayNoteVec& candidate = composeScratch.notes;
    // The hold substituted an interval the request has left: only a provider qualified for the *requested*
    // interval may serve it, and only for the interval it was served for. Anything else keeps the retained
    // frame's honest hole rather than inventing rows for an interval no source covers.
    bool composedForRequest = false;
    const char* qualifiedSourceToken = "-";
    // A pass-state refresh may already cover this very window while its replacement root is pending.
    // Prefer that qualified cache (including an empty interval) to the pre-undo held frame. Capture
    // handoffs keep their existing policy: they may still owe accepted preview content beyond the cache.
    const bool currentCacheReplacement = holdSessionExitGapHandoff &&
        !loop.captureActive() && !loop.hasOverdubSession() &&
        loop.tryCopyCurrentVisualCacheDisplayNotesInWindow(
            requestedStartTick, requestedLengthTicks, candidate);
    if (currentCacheReplacement ||
        (heldRetainedContent && detailedWindowRequestedSlot_ == displaySlot &&
         requestedLengthTicks > 0 &&
         (requestedStartTick != windowStart || requestedLengthTicks != windowLength) &&
         loop.collectQualifiedCommittedDisplayNotes(requestedStartTick, requestedLengthTicks, candidate))) {
        // One generation, one base. The retained rows contribute nothing here: the frame is the provider's
        // committed rows for the requested interval, and its content key is that interval.
        windowStart = requestedStartTick;
        windowLength = requestedLengthTicks;
        if (Config::TICKS_PER_BAR > 0) {
            firstBar = windowStart / Config::TICKS_PER_BAR;
            lastBar = (windowStart + windowLength - 1) / Config::TICKS_PER_BAR;
        }
        currentKey.windowStart = windowStart;
        currentKey.windowLength = windowLength;
        // The stored manual window start follows the published interval, as the no-hold path's does.
        detailedWindowStartTick_[displaySlot] = windowStart;
        qualifiedSourceToken = qualifiedDisplaySourceToken(loop.lastQualifiedDisplaySource());
        composedForRequest = true;
    } else {
        candidate.clear();
    }
    // What this turn publishes, for the `DIAG,disp_turn` receipt only: `holdRetainedFrame` keeps the frame the
    // interval was qualified for; the seed retention marks the same state without a publication gap. Everything
    // else publishes a frame for the requested interval.
    bool holdRetainedFrame = holdSessionExitGapHandoff && !composedForRequest;
    bool retainedFrame = holdRetainedFrame;
    size_t committedDisplayEnd = liveDisplayCommittedPrefix_.size();
    const uint32_t gatherStartUs = micros();
    if (composedForRequest) {
        // The recognised live overlay is composed once, on top of the provider's committed base.
        DIAG_COUNTER_INC(DisplayFullRebuild);
        committedDisplayEnd = candidate.size();
        storeCommittedDrawSourcePrefix(candidate, committedDisplayEnd, liveDisplayCommittedPrefix_);
        applyDrawSourceOverlays(track, displaySlot, loop, firstBar, lastBar, candidate,
                                committedDisplayEnd);
        liveDisplayNotes.swap(candidate);
    } else if (holdSessionExitGapHandoff && (loop.captureActive() || loop.hasOverdubSession()) &&
        liveDrawSourceKey_.valid != 0 && liveDrawSourceKey_.track == trackIndex &&
        liveDrawSourceKey_.slot == displaySlot && liveDrawSourceKey_.windowStart == windowStart &&
        liveDrawSourceKey_.windowLength == windowLength) {
        // Hold the complete committed base, not the live Capture overlay. No
        // unpublished root/candidate enters the prefix; accepted preview rows
        // still refresh through the existing atomic compose/paint handoff.
        storeCommittedDrawSourcePrefix(liveDisplayNotes, liveDisplayCacheCommittedNoteCount_,
                                       liveDisplayCommittedPrefix_);
        reuseCommittedDrawSourcePrefix(liveDisplayCommittedPrefix_, candidate);
        committedDisplayEnd = candidate.size();
        applyDrawSourceOverlays(track, displaySlot, loop, firstBar, lastBar, candidate, committedDisplayEnd);
        liveDisplayNotes.swap(candidate);
        DIAG_COUNTER_INC(DisplayIncrementalUpdate);
    } else if (reuseKind == DisplayWindowUtils::DrawSourceReuseKind::ReuseCommittedDrawSource &&
               canReuseCommittedPrefix) {
        // Reuse the untransformed committed prefix. Overlays stay on compose.
        // Pending/overlay transforms paint on a copy so the next frame cannot
        // reuse a compose source that predates the pending change.
        DIAG_COUNTER_INC(DisplayIncrementalUpdate);
        reuseCommittedDrawSourcePrefix(liveDisplayCommittedPrefix_, candidate);
        committedDisplayEnd = candidate.size();
        applyDrawSourceOverlays(track, displaySlot, loop, firstBar, lastBar, candidate, committedDisplayEnd);
        liveDisplayNotes.swap(candidate);
    } else if (canSlideCommittedPrefix) {
        DIAG_COUNTER_INC(DisplayIncrementalUpdate);
        reuseCommittedDrawSourcePrefix(liveDisplayCommittedPrefix_, candidate);
        DisplayWindowUtils::filterDisplayNotesByWindowInclusionInPlace(
            candidate, windowStart, windowLength, loopLength);
        const uint32_t enteringBar =
            windowStart > liveDrawSourceKey_.windowStart ? lastBar : firstBar;
        gatherCommittedDrawSourceBars(track, displaySlot, loop, enteringBar, enteringBar,
                                      resolvedPitchLow, resolvedPitchHigh, candidate, committedDisplayEnd);
        const size_t gatheredCommittedEnd = committedDisplayEnd;
        storeCommittedDrawSourcePrefix(candidate, gatheredCommittedEnd, liveDisplayCommittedPrefix_);
        applyDrawSourceOverlays(track, displaySlot, loop, firstBar, lastBar, candidate, committedDisplayEnd);
        if (holdRetainedFrame) {
            retainDrawSourceHandoffCommittedPrefix(
                liveDisplayNotes, gatheredCommittedEnd, liveDisplayCacheCommittedNoteCount_,
                liveDisplayCommittedPrefix_, committedDisplayEnd);
        } else {
            liveDisplayNotes.swap(candidate);
        }
    } else {
        DIAG_COUNTER_INC(DisplayFullRebuild);
        gatherCommittedDrawSourceBars(track, displaySlot, loop, firstBar, lastBar, resolvedPitchLow,
                                      resolvedPitchHigh, candidate, committedDisplayEnd);
        const bool captureOrOverdubActive =
            loop.captureActive() || loop.hasOverdubSession();
        if (DisplayWindowUtils::shouldRetainUnpublishedVisualCacheOverStaleGather(
                loop.publishedSnapshotIsCurrent(), captureOrOverdubActive,
                loop.visualCache.notes.size(), committedDisplayEnd,
                loop.hasSealedCapturePreview() && !loop.sealedCapturePreviewIsCovered())) {
            candidate.assign(loop.visualCache.notes.begin(), loop.visualCache.notes.end());
            committedDisplayEnd = candidate.size();
        }
        const size_t gatheredCommittedEnd = committedDisplayEnd;
        const size_t priorCommittedEnd = liveDisplayCacheCommittedNoteCount_;
        const bool snapshotSelected =
            DisplayWindowUtils::shouldGatherPublishedSnapshotForDrawSource(
                static_cast<bool>(loop.publishedSnapshot()), loop.captureActive(),
                loop.hasOverdubSession(), !loop.visualCache.notes.empty(),
                loop.visualCacheDirty, loop.publishedSnapshotIsCurrent());
        const bool visualCleanNonempty =
            !loop.visualCacheDirty && !loop.visualCache.notes.empty();
        const bool retainHandoffSeed =
            recordPreview == nullptr && !loop.captureActive() && !loop.hasOverdubSession() &&
            DisplayWindowUtils::shouldRetainRecordStopHandoffSeed(snapshotSelected,
                                                                 visualCleanNonempty) &&
            !liveDisplayNotes.empty();
        const bool willRetainHandoffFrame =
            holdRetainedFrame || retainHandoffSeed;
        retainedFrame = willRetainHandoffFrame;
        if (!willRetainHandoffFrame || !captureOrOverdubActive || gatheredCommittedEnd > 0) {
            storeCommittedDrawSourcePrefix(candidate, gatheredCommittedEnd,
                                           liveDisplayCommittedPrefix_);
        }
        applyDrawSourceOverlays(track, displaySlot, loop, firstBar, lastBar, candidate, committedDisplayEnd);
        if (willRetainHandoffFrame) {
            if (captureOrOverdubActive) {
                retainDrawSourceHandoffCommittedPrefix(
                    liveDisplayNotes, gatheredCommittedEnd, priorCommittedEnd,
                    liveDisplayCommittedPrefix_, committedDisplayEnd);
            } else {
                committedDisplayEnd = liveDisplayNotes.size();
                storeCommittedDrawSourcePrefix(liveDisplayNotes, committedDisplayEnd,
                                               liveDisplayCommittedPrefix_);
            }
        } else {
            liveDisplayNotes.swap(candidate);
        }
    }
    // The retained handoff is already a complete frame. Unpublished Snapshot candidates
    // are not draw sources: adopting uncovered bars would mix frame identities and
    // advancing the key would falsely mark the old frame as the new window.
    DIAG_TIMING_RECORD(DisplayBuild, micros() - gatherStartUs);

    if (!holdRetainedFrame &&
        (recoverMissingPendingShortenSources(loop, liveDisplayNotes, committedDisplayEnd) ||
         recoverMissingSealedShortenSources(loop, liveDisplayNotes, committedDisplayEnd))) {
        storeCommittedDrawSourcePrefix(liveDisplayNotes, committedDisplayEnd,
                                       liveDisplayCommittedPrefix_);
    }

    // Stage 3B: STOPPED idle draw-source — snapshot spans (or reused snapshot
    // prefix) can omit wrap-held On→loopEnd. Complete with the same pass
    // append used by VC rebuild / Stage 3A. Snapshot remains base.
    if (DisplayWindowUtils::shouldCompleteStoppedDrawSourceWrapHeld(
            track.isStopped(), loop.captureActive(),
            loop.publishedSnapshotIsCurrent())) {
        Loop& mutLoop = const_cast<Loop&>(loop);
        const size_t beforeComplete = liveDisplayNotes.size();
        mutLoop.appendOverdubPassDisplayNotes(liveDisplayNotes);
        if (liveDisplayNotes.size() != beforeComplete) {
            committedDisplayEnd = liveDisplayNotes.size();
            storeCommittedDrawSourcePrefix(liveDisplayNotes, committedDisplayEnd,
                                           liveDisplayCommittedPrefix_);
        }
    }

    const bool applyLiveCaptureTails = DisplayWindowUtils::shouldApplyLiveCapturePlayheadTails(
        track.isRecording(), track.isOverdubbing(), false, false,
        !loop.capturePreview.openNoteIndices.empty());
    const bool havePendingPaint = DisplayWindowUtils::shouldPaintPendingDrawSourceRepresentation(
        track.isOverdubbing(), loop.hasPendingNoteChanges(), loop.hasSealedCompanionOverlay());
    composeScratch.restorePaintStorage();
    const DisplayNoteVec* paintedNotes = &liveDisplayNotes;
    if (applyLiveCaptureTails || havePendingPaint) {
        DisplayWindowUtils::replacePaintFromCompose(liveDisplayPendingPaintNotes_, liveDisplayNotes);
        if (havePendingPaint) {
            // Match live resolve ordering: apply pending first, then grow tails.
            // This keeps incoming open holds from being swallowed by a same-start
            // committed row that gets transformed away on the same frame (`004710`).
            applyPendingDrawSourceTransforms(loop, liveDisplayPendingPaintNotes_,
                                             committedDisplayEnd);
            emitPaintedPendingShortenDiagnostics(loop, liveDisplayNotes,
                                                 liveDisplayPendingPaintNotes_);
        }
        if (applyLiveCaptureTails) {
            // Withdrawn 2026-10-30: freezing this at the accepted stop's closure tick made open
            // notes in the wrap window hit the wrap-split branch (`tailEnd = loopLength - 1`), which
            // the owner saw as notes lengthened to the end of the piano roll. The tail follows the
            // playhead until the wrap-split policy itself is reworked.
            const uint32_t playheadCloseTick =
                resolvePlayheadInLoop(track, displaySlot, currentTick);
            const bool allowWrapContinuation =
                DisplayWindowUtils::shouldAllowCapturePlayheadWrapContinuation(
                    track.isRecording(), track.isAudiblePlaybackActive(),
                    track.isOverdubbing());
            applyCapturePlayheadTails(loop.capturePreview, loopLength, playheadCloseTick,
                                      committedDisplayEnd, liveDisplayPendingPaintNotes_,
                                      allowWrapContinuation);
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
            emitPaintedOpenTailRows(loop, playheadCloseTick, windowStart,
                                    liveDisplayPendingPaintNotes_);
#endif
        }
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
        emitPaintedOccupy193815Row("after_pending", loop, liveDisplayPendingPaintNotes_);
#endif
        paintedNotes = &liveDisplayPendingPaintNotes_;
    }
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
    emitPaintedOccupy193815Row("after_tails", loop, *paintedNotes);
#endif

    liveDisplayCacheCommittedNoteCount_ = committedDisplayEnd;
    liveDisplayCacheBaseNoteCount_ = liveDisplayNotes.size();
    livePlaybackDisplaySlot_ = displaySlot;
    livePlaybackDisplayTrack_ = trackIndex;
    if (!holdRetainedFrame) {
        liveMergePlaybackRevision_ = loop.playbackRevision;
        liveMergeCaptureRevision_ = loop.captureDisplayRevision;
        liveDrawSourceKey_ = currentKey;
    }
    // `DIAG,disp_turn` (capture-gated): the interval this turn requested against the interval the frame is
    // **drawn** in. `pub` is the viewport fact (`detailedWindowViewport*_`, the coordinates the piano roll maps
    // its grid, bars and cursor in), not the content key: under a content hold the two are deliberately
    // different, and the row exists to prove the viewport still tracked the request (`req == pub`, `st=ret`,
    // `why=hold_gap`). The content interval itself is not re-published here -- that is the hold's whole point.
    emitDisplayTurnReceipt(
        track, displaySlot,
        detailedWindowRequestedSlot_ == displaySlot ? detailedWindowRequestedStartTick_
                                                    : currentKey.windowStart,
        detailedWindowRequestedSlot_ == displaySlot ? detailedWindowRequestedLengthTicks_
                                                    : currentKey.windowLength,
        detailedWindowViewportSlot_ == displaySlot ? detailedWindowViewportStartTick_ : 0,
        detailedWindowViewportSlot_ == displaySlot ? detailedWindowViewportLengthTicks_ : 0,
        static_cast<uint32_t>(paintedNotes->size()), retainedFrame,
        retainedFrame ? (holdRetainedFrame ? "hold_gap" : "handoff_seed") : qualifiedSourceToken,
        // `key=`: the covered interval of the frame that carries the content this turn.
        currentKey.windowStart, currentKey.windowLength);
    return *paintedNotes;
}
