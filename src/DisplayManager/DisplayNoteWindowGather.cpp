//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "DisplayManager.h"
#include "DisplayManagerInternal.h"

#include "Globals.h"
#include "Loop.h"
#include "LoopSnapshot.h"
#include "PendingNoteChange.h"
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
#else
#define DISP_COLD_MEM
#define DISP_CAPTURE_MEM
#endif

using namespace DisplayManagerInternal;

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

bool DisplayManager::syncDetailedPaintWindow(const Track& track, uint8_t displaySlot,
                                             uint32_t currentTick, uint32_t loopLength,
                                             uint32_t& outWindowStart, uint32_t& outWindowLength,
                                             uint8_t& outWindowBars) {
    if (track.isJamming() || displaySlot >= kDisplaySlotCount) {
        outWindowStart = 0;
        outWindowLength = loopLength;
        outWindowBars = DisplayWindowUtils::kMaxDetailedWindowBars;
        return false;
    }
    const uint8_t requestedBars = std::min<uint8_t>(detailedWindowBars_[displaySlot],
                                                    DisplayWindowUtils::kMaxDetailedWindowBars);
    const uint32_t horizonLength =
        static_cast<uint32_t>(requestedBars) * Config::TICKS_PER_BAR;
    const bool autoFollow = !DisplayWindowUtils::windowCoversLoop(horizonLength, loopLength) &&
                            shouldAutoFollowDetailedWindow(track, loopLength);
    const uint32_t playhead = autoFollow ? resolvePlayheadInLoop(track, displaySlot, currentTick)
                                         : 0;
    if (!DisplayWindowUtils::resolveDetailedPaintWindow(
            loopLength, requestedBars, detailedWindowStartTick_[displaySlot], playhead, autoFollow,
            outWindowStart, outWindowLength, outWindowBars)) {
        outWindowStart = 0;
        outWindowLength = loopLength;
        outWindowBars = requestedBars;
        return false;
    }
    detailedWindowStartTick_[displaySlot] = outWindowStart;
    return true;
}

DISP_COLD_MEM const DisplayNoteVec& DisplayManager::resolveWindowedDisplayNotes(
    const Track& track, Loop& mutLoop, const Loop& loop, uint8_t displaySlot, uint32_t loopLength,
    uint32_t windowStart, uint32_t windowLength) {
    const uint8_t trackIndex = resolveTrackIndex(track);

    // Prefer filtering visualCache over gather+reconstruct when capture is idle and the
    // cache covers the paint neighborhood. A nonempty dirty prefix must not win — after
    // record stop the first idle slice left 20 notes and later bars never painted
    // (session_20260825_235954).
    if (!loop.captureActive() &&
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

    const bool cacheHit =
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

DISP_CAPTURE_MEM void gatherCommittedDrawSourceBars(const Loop& loop, uint32_t firstBar,
                                                    uint32_t lastBar, uint8_t pitchLow,
                                                    uint8_t pitchHigh, NoteUtils::DisplayNoteVec& out,
                                                    size_t& committedEnd) {
    const uint32_t loopLength = loop.loopLengthTicks;
    if (loopLength == 0 || firstBar > lastBar) {
        committedEnd = out.size();
        return;
    }
    const ImmutableLoopSnapshotRef snapshot = loop.publishedSnapshot();
    const bool usePublishedSnapshot = DisplayWindowUtils::shouldGatherPublishedSnapshotForDrawSource(
        static_cast<bool>(snapshot), loop.captureActive(), loop.hasOverdubSession(),
        !loop.visualCache.notes.empty(), loop.visualCacheDirty,
        loop.publishedSnapshotIsCurrent());
    const uint8_t resolvedPitchHigh = pitchHigh < pitchLow ? pitchLow : pitchHigh;
    for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
        if (usePublishedSnapshot) {
            appendSnapshotNotesForBar(*snapshot, bar, pitchLow, resolvedPitchHigh, out);
        } else {
            appendNotesIntersectingBar(loop.visualCache.notes, bar, loopLength, out);
        }
    }
    committedEnd = out.size();
}

DISP_CAPTURE_MEM void applyDrawSourceOverlays(const Loop& loop, uint32_t firstBar, uint32_t lastBar,
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
    if (!captureActive) {
        return;
    }
    for (const NoteUtils::DisplayNote& previewNote : loop.capturePreview.notes) {
        bool intersectsWindow = false;
        for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
            if (previewNoteIntersectsBar(previewNote, bar, loopLength)) {
                intersectsWindow = true;
                break;
            }
        }
        if (!intersectsWindow) {
            continue;
        }
        if (DisplayWindowUtils::capturePreviewNoteIsOpenHold(previewNote.startTick,
                                                             previewNote.endTick)) {
            appendDisplayNoteUniqueById(out, previewNote);
        } else {
            DisplayWindowUtils::upsertDisplayNoteAtPitchStartFrom(out, committedEnd, previewNote,
                                                                 true);
        }
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
    for (const NoteUtils::DisplayNote& note : loop.overdubSourceViewNotes()) {
        if (note.noteId == noteId) {
            out = note;
            return true;
        }
    }
    if (visualHint == nullptr || loop.loopLengthTicks == 0 || Config::TICKS_PER_BAR == 0) {
        return false;
    }
    const uint32_t barTicks = Config::TICKS_PER_BAR;
    const uint32_t barStart = (visualHint->startTick / barTicks) * barTicks;
    SessionMidiEventVec flat;
    loop.gatherCommittedEventsInWindow(flat, barStart, barTicks * 2u, true);
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
    key.capturePreviewRevision = loop.capturePreview.revision;
    key.visualCacheRevision = loop.visualCache.revision;
    key.publishedSnapshotRevision = loop.publishedSnapshotRevision();
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
    syncDetailedPaintWindow(track, displaySlot, currentTick, loopLength, windowStart, windowLength,
                            windowBars);
    if (windowLength == 0) {
        windowStart = 0;
        windowLength = loopLength;
    }

    const uint32_t playheadTick = resolvePlayheadInLoop(track, displaySlot, currentTick);
    const uint32_t playheadBar =
        Config::TICKS_PER_BAR > 0 ? playheadTick / Config::TICKS_PER_BAR : 0;
    const uint32_t firstBar =
        Config::TICKS_PER_BAR > 0 ? windowStart / Config::TICKS_PER_BAR : 0;
    const uint32_t lastTick = windowStart + windowLength > 0 ? windowStart + windowLength - 1 : 0;
    const uint32_t lastBar = Config::TICKS_PER_BAR > 0 ? lastTick / Config::TICKS_PER_BAR : 0;
    const uint8_t resolvedPitchLow = pitchLow;
    const uint8_t resolvedPitchHigh = pitchHigh < pitchLow ? pitchLow : pitchHigh;
    const uint8_t trackIndex = resolveTrackIndex(track);

    DisplayWindowUtils::DrawSourceGatherKey currentKey = makeDrawSourceGatherKey(
        track, loop, displaySlot, windowStart, windowLength, playheadBar, resolvedPitchLow,
        resolvedPitchHigh);
    currentKey.track = trackIndex;

    DisplayWindowUtils::DrawSourceReuseKind reuseKind =
        DisplayWindowUtils::selectDrawSourceReuseKind(liveDrawSourceKey_, currentKey,
                                                      Config::TICKS_PER_BAR);
    const bool canReuseCommittedPrefix = !liveDisplayCommittedPrefix_.empty();
    const bool canSlideCommittedPrefix =
        reuseKind == DisplayWindowUtils::DrawSourceReuseKind::SlideCommittedDrawSource &&
        canReuseCommittedPrefix;

    size_t committedDisplayEnd = liveDisplayCommittedPrefix_.size();
    DisplayNoteVec candidate;
    const uint32_t gatherStartUs = micros();
    if (reuseKind == DisplayWindowUtils::DrawSourceReuseKind::ReuseCommittedDrawSource &&
        canReuseCommittedPrefix) {
        // Reuse the untransformed committed prefix. Overlays stay on compose.
        // Pending/overlay transforms paint on a copy so the next frame cannot
        // reuse a compose source that predates the pending change.
        DIAG_COUNTER_INC(DisplayIncrementalUpdate);
        reuseCommittedDrawSourcePrefix(liveDisplayCommittedPrefix_, candidate);
        committedDisplayEnd = candidate.size();
        applyDrawSourceOverlays(loop, firstBar, lastBar, candidate, committedDisplayEnd);
        liveDisplayNotes.swap(candidate);
    } else if (canSlideCommittedPrefix) {
        DIAG_COUNTER_INC(DisplayIncrementalUpdate);
        reuseCommittedDrawSourcePrefix(liveDisplayCommittedPrefix_, candidate);
        candidate = DisplayWindowUtils::filterDisplayNotesByWindowInclusion(
            candidate, windowStart, windowLength, loopLength);
        const uint32_t enteringBar =
            windowStart > liveDrawSourceKey_.windowStart ? lastBar : firstBar;
        gatherCommittedDrawSourceBars(loop, enteringBar, enteringBar, resolvedPitchLow,
                                      resolvedPitchHigh, candidate, committedDisplayEnd);
        storeCommittedDrawSourcePrefix(candidate, committedDisplayEnd, liveDisplayCommittedPrefix_);
        applyDrawSourceOverlays(loop, firstBar, lastBar, candidate, committedDisplayEnd);
        liveDisplayNotes.swap(candidate);
    } else {
        DIAG_COUNTER_INC(DisplayFullRebuild);
        gatherCommittedDrawSourceBars(loop, firstBar, lastBar, resolvedPitchLow, resolvedPitchHigh,
                                      candidate, committedDisplayEnd);
        storeCommittedDrawSourcePrefix(candidate, committedDisplayEnd, liveDisplayCommittedPrefix_);
        applyDrawSourceOverlays(loop, firstBar, lastBar, candidate, committedDisplayEnd);
        const bool snapshotSelected =
            DisplayWindowUtils::shouldGatherPublishedSnapshotForDrawSource(
                static_cast<bool>(loop.publishedSnapshot()), loop.captureActive(),
                loop.hasOverdubSession(), !loop.visualCache.notes.empty(),
                loop.visualCacheDirty, loop.publishedSnapshotIsCurrent());
        const bool visualCleanNonempty =
            !loop.visualCacheDirty && !loop.visualCache.notes.empty();
        const bool retainHandoffSeed =
            !loop.captureActive() && !loop.hasOverdubSession() &&
            DisplayWindowUtils::shouldRetainRecordStopHandoffSeed(snapshotSelected,
                                                                 visualCleanNonempty) &&
            !liveDisplayNotes.empty();
        if (retainHandoffSeed) {
            committedDisplayEnd = liveDisplayNotes.size();
            storeCommittedDrawSourcePrefix(liveDisplayNotes, committedDisplayEnd,
                                           liveDisplayCommittedPrefix_);
        } else {
            liveDisplayNotes.swap(candidate);
        }
    }
    DIAG_TIMING_RECORD(DisplayBuild, micros() - gatherStartUs);

    if (recoverMissingPendingShortenSources(loop, liveDisplayNotes, committedDisplayEnd) ||
        recoverMissingSealedShortenSources(loop, liveDisplayNotes, committedDisplayEnd)) {
        storeCommittedDrawSourcePrefix(liveDisplayNotes, committedDisplayEnd,
                                       liveDisplayCommittedPrefix_);
    }

    const bool applyLiveCaptureTails = DisplayWindowUtils::shouldApplyLiveCapturePlayheadTails(
        track.isRecording(), track.isOverdubbing(), false, false,
        !loop.capturePreview.openNoteIndices.empty());
    const bool havePendingPaint = DisplayWindowUtils::shouldPaintPendingDrawSourceRepresentation(
        track.isOverdubbing(), loop.hasPendingNoteChanges(), loop.hasSealedCompanionOverlay());
    const DisplayNoteVec* paintedNotes = &liveDisplayNotes;
    if (applyLiveCaptureTails || havePendingPaint) {
        liveDisplayPendingPaintNotes_ = liveDisplayNotes;
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
            const uint32_t playheadCloseTick =
                resolvePlayheadInLoop(track, displaySlot, currentTick);
            const bool allowWrapContinuation =
                !track.isOverdubbing() && !(track.isRecording() && !track.isPlaying());
            applyCapturePlayheadTails(loop.capturePreview, loopLength, playheadCloseTick,
                                      committedDisplayEnd, liveDisplayPendingPaintNotes_,
                                      allowWrapContinuation);
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
    liveMergePlaybackRevision_ = loop.playbackRevision;
    liveMergeCaptureRevision_ = loop.captureDisplayRevision;
    liveDrawSourceKey_ = currentKey;
    return *paintedNotes;
}
