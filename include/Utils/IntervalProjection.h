//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <vector>

#include "MidiEvent.h"
#include "NoteEditSessionState.h"
#include "Utils/ExternalMemoryFirstAllocator.h"
#include "Utils/NoteUtils.h"

/// Shared span primitive — interval is primary; length is derived.
struct TickInterval {
    int32_t start = 0;
    int32_t end = 0;

    int32_t length() const { return end - start; }

    bool intersects(const TickInterval& other) const {
        return start < other.end && end > other.start;
    }
};

enum class ProjectionType : uint8_t {
    Playback,
    Display,
    Edit,
    Timeline,  // reserved — long-loop viewport / future clip window
};

/// Coordinate space for interval projection (input frame + consumer extensions).
struct ProjectionContext {
    uint32_t loopLength = 0;  // period (duration) — NOT a TickInterval
    TickInterval window;      // inclusive working coordinate window [start, end)
    ProjectionType type = ProjectionType::Playback;
    // Playback extensions
    int32_t originTick = 0;
    int32_t projectionCycleStartTick = 0;
    bool useQueuedStart = false;
    int32_t queuedStartTick = 0;
    // Loop geometry (playback + edit + display)
    int32_t loopStartTick = 0;
    // Edit extensions
    int32_t selectedTick = 0;
};

struct CanonicalNoteSpan {
    NoteId noteId = kInvalidNoteId;
    TickInterval interval;
    uint8_t pitch = 0;
    uint8_t velocity = 0;
    bool isOpen = false;
    bool splitHeadTail = false;
};

struct ProjectedNoteInterval {
    NoteId noteId = kInvalidNoteId;
    TickInterval interval;
    uint8_t pitch = 0;
};

using CanonicalNoteSpanVec =
    std::vector<CanonicalNoteSpan, ExternalMemoryFirstAllocator<CanonicalNoteSpan>>;
using ProjectedIntervalVec =
    std::vector<ProjectedNoteInterval, ExternalMemoryFirstAllocator<ProjectedNoteInterval>>;

namespace IntervalProjection {

/// Position in loop [0, loopLength). Signed-delta safe.
uint32_t tickPhaseInLoop(uint32_t currentTick, uint32_t startLoopTick, uint32_t loopLengthTicks);

/// Projected-interval coordinate: storage absolute → relative [0, loopLength).
uint32_t noteRelativeTick(uint32_t absolutePos, uint32_t loopStartTick, uint32_t loopLength);

/// Projected-interval coordinate: relative → storage absolute.
uint32_t noteStorageTick(uint32_t relativeTick, uint32_t loopStartTick, uint32_t loopLength);

/// Phase in rolling projection cycle (D13): (currentTick - projectionCycleStartTick) % loopLength.
uint32_t tickPhaseInProjectionCycle(uint32_t currentTick, int32_t projectionCycleStartTick,
                                    uint32_t loopLength);

/// True when display playhead crossed the loop head backward (musical wrap, not storage phase wrap).
bool didDisplayPlayheadWrapBackward(uint32_t newStoragePhase, uint32_t prevStoragePhase,
                                    uint32_t loopStartTick, uint32_t loopLength);

/// Advance cycle origin on wrap; uses loopLength at wrap time.
int32_t advanceProjectionCycleStartTickOnWrap(int32_t projectionCycleStartTick,
                                            uint32_t loopLengthAtWrap);

// Stage 1 — pure math; bounded k shifts by loopLength; deterministic ascending k order.
ProjectedIntervalVec generateEquivalentIntervals(const CanonicalNoteSpan& span, uint32_t loopLength,
                                                 const ProjectionContext& context);

// Stage 2 — consumer-specific selection (Playback / Edit / Timeline).
ProjectedNoteInterval selectProjectedInterval(const ProjectedIntervalVec& candidates,
                                              const ProjectionContext& context);

// Stage 2 — Display: every candidate intersecting window.
ProjectedIntervalVec selectProjectedIntervalsForDisplay(const ProjectedIntervalVec& candidates,
                                                        const ProjectionContext& context);

// Batch helper — generate → select per ProjectionType.
ProjectedIntervalVec projectNoteIntervals(const CanonicalNoteSpanVec& spans,
                                          const ProjectionContext& context);

/// v1 NOTE_EDIT overlap analysis window — full loop `[0, loopLength)`.
TickInterval makeFullLoopEditAnalysisWindow(uint32_t loopLength);

/// Edit projection context — `originTick` is primary edited note linear start (storage ticks).
ProjectionContext buildEditProjectionContext(const EditorSelection& selection, uint32_t loopLength,
                                             TickInterval analysisWindow,
                                             int32_t primaryLinearStartTick,
                                             int32_t loopStartTick = 0);

/// Single-span Edit projection — generate → select closest to `context.originTick`.
ProjectedNoteInterval projectEditLinearSpan(const CanonicalNoteSpan& span,
                                            const ProjectionContext& context);

/// Batch Edit projection for overlap analyze — forces `ProjectionType::Edit`.
ProjectedIntervalVec projectEditIntervalsForAnalysis(const CanonicalNoteSpanVec& spans,
                                                     const ProjectionContext& context);

/// v1 display analysis frame — full loop `[0, loopLength)`.
TickInterval makeFullLoopDisplayWindow(uint32_t loopLength);

/// Display projection context — `window` is the drawable coordinate frame.
ProjectionContext buildDisplayProjectionContext(uint32_t loopLength, TickInterval window,
                                                int32_t loopStartTick = 0);

/// Display projection + rendering — Stage 2 selection, head/tail split, live open-tail.
NoteUtils::DisplayNoteVec projectDisplayNotes(const CanonicalNoteSpanVec& spans,
                                              const ProjectionContext& context,
                                              uint32_t playheadTick = UINT32_MAX);

/// v1 playback analysis frame — full loop `[0, loopLength)`.
TickInterval makeFullLoopPlaybackWindow(uint32_t loopLength);

/// Playback projection context — `originTick` is global transport / effective playback tick.
ProjectionContext buildPlaybackProjectionContext(uint32_t loopLength, TickInterval window,
                                                 int32_t originTick,
                                                 int32_t projectionCycleStartTick,
                                                 int32_t loopStartTick, bool useQueuedStart,
                                                 int32_t queuedStartTick);

/// Map one storage event tick to in-loop playback phase `[0, loopLength)` — no heap alloc (hot path).
uint32_t playbackEventPhase(uint32_t storageTick, uint32_t loopLength);

/// Map one storage event tick to in-loop playback phase `[0, loopLength)`.
uint32_t projectPlaybackEventPhase(uint32_t storageTick, const ProjectionContext& context);

/// Display segment whose length exceeds half the loop is usually wrap projection, not linear span.
bool isInflatedDisplaySpan(const NoteUtils::DisplayNote& displayNote, uint32_t loopLength);

}  // namespace IntervalProjection
