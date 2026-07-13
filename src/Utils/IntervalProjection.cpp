//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/IntervalProjection.h"

#include "Utils/NoteUtils.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <vector>

namespace {

int64_t floorDiv(int64_t numerator, int64_t denominator) {
    if (denominator == 0) {
        return 0;
    }
    int64_t quotient = numerator / denominator;
    const int64_t remainder = numerator % denominator;
    if (remainder != 0 && ((numerator < 0) != (denominator < 0))) {
        quotient--;
    }
    return quotient;
}

bool computeKBounds(int32_t spanStart, int32_t spanEnd, uint32_t loopLength, const TickInterval& window,
                    int32_t& kMin, int32_t& kMax) {
    if (loopLength == 0) {
        return false;
    }
    const int64_t loop = static_cast<int64_t>(loopLength);
    kMin = static_cast<int32_t>(floorDiv(static_cast<int64_t>(window.start) - spanEnd, loop) + 1);
    kMax = static_cast<int32_t>(floorDiv(static_cast<int64_t>(window.end) - 1 - spanStart, loop));
    return kMin <= kMax;
}

int64_t distanceToInterval(int32_t tick, const TickInterval& interval) {
    if (tick >= interval.start && tick < interval.end) {
        return 0;
    }
    const int64_t distanceToStart = std::llabs(static_cast<int64_t>(tick) - interval.start);
    const int64_t distanceToEnd = std::llabs(static_cast<int64_t>(tick) - interval.end);
    return std::min(distanceToStart, distanceToEnd);
}

ProjectedNoteInterval makeEmptyProjectedInterval() {
    return ProjectedNoteInterval{kInvalidNoteId, TickInterval{}, 0};
}

uint32_t displayInclusiveEndTick(int32_t exclusiveEnd, int32_t start, uint32_t loopLength) {
    if (loopLength == 0) {
        return 0;
    }
    if (exclusiveEnd <= static_cast<int32_t>(start)) {
        const int32_t wrapped = exclusiveEnd % static_cast<int32_t>(loopLength);
        return wrapped <= 0 ? 0u : static_cast<uint32_t>(wrapped - 1);
    }
    if (exclusiveEnd > static_cast<int32_t>(loopLength)) {
        const uint32_t wrapped = static_cast<uint32_t>(exclusiveEnd % static_cast<int32_t>(loopLength));
        return wrapped == 0 ? loopLength - 1 : wrapped - 1;
    }
    return static_cast<uint32_t>(exclusiveEnd - 1);
}

void appendDisplayNote(NoteUtils::DisplayNoteVec& out, NoteId noteId, uint8_t pitch, uint8_t velocity,
                       uint32_t startTick, uint32_t endTickInclusive) {
    NoteUtils::DisplayNote note;
    note.noteId = noteId;
    note.note = pitch;
    note.velocity = velocity;
    note.startTick = startTick;
    note.endTick = endTickInclusive;
    out.push_back(note);
}

void renderProjectedIntervalToDisplayNotes(const ProjectedNoteInterval& projected, uint8_t velocity,
                                           bool isOpen, bool splitHeadTail, uint32_t loopLength,
                                           uint32_t playheadTick, NoteUtils::DisplayNoteVec& out) {
    if (loopLength == 0) {
        return;
    }

    const int32_t start = projected.interval.start;
    const int32_t exclusiveEnd = projected.interval.end;
    const uint32_t startTick = start < 0 ? 0u : static_cast<uint32_t>(start);

    if (isOpen) {
        uint32_t endTick = loopLength - 1;
        if (playheadTick != UINT32_MAX) {
            endTick = std::min(playheadTick % loopLength, loopLength - 1);
            if (endTick < startTick) {
                endTick = loopLength - 1;
            }
        }
        appendDisplayNote(out, projected.noteId, projected.pitch, velocity, startTick, endTick);
        return;
    }

    if (splitHeadTail) {
        const uint32_t headOffInclusive = displayInclusiveEndTick(exclusiveEnd, start, loopLength);
        appendDisplayNote(out, projected.noteId, projected.pitch, velocity, startTick,
                          loopLength - 1);
        const NoteUtils::WrapHeadSegment head = NoteUtils::resolveWrapHeadSegment(
            loopLength, headOffInclusive, NoteUtils::WrapHeadSegmentContext::CommittedHeadOff);
        if (head.visible) {
            appendDisplayNote(out, projected.noteId, projected.pitch, velocity, head.startTick,
                              head.endTickInclusive);
        }
        return;
    }

    const uint32_t endTickInclusive = displayInclusiveEndTick(exclusiveEnd, start, loopLength);
    appendDisplayNote(out, projected.noteId, projected.pitch, velocity, startTick,
                      endTickInclusive);
}

ProjectedNoteInterval selectPlaybackEventProjectedInterval(const ProjectedIntervalVec& candidates,
                                                           uint32_t loopLength, int32_t originTick) {
    if (loopLength == 0) {
        return makeEmptyProjectedInterval();
    }
    const int32_t loop = static_cast<int32_t>(loopLength);
    for (const ProjectedNoteInterval& candidate : candidates) {
        if (candidate.interval.start >= 0 && candidate.interval.start < loop) {
            return candidate;
        }
    }
    ProjectionContext context{};
    context.originTick = originTick;
    return IntervalProjection::selectProjectedInterval(candidates, context);
}

ProjectedNoteInterval selectSingleDisplayProjectedInterval(const ProjectedIntervalVec& candidates,
                                                           const CanonicalNoteSpan& span,
                                                           const ProjectionContext& context) {
    const ProjectedNoteInterval* storageAligned = nullptr;
    const ProjectedNoteInterval* firstIntersecting = nullptr;
    for (const ProjectedNoteInterval& candidate : candidates) {
        if (!candidate.interval.intersects(context.window)) {
            continue;
        }
        if (firstIntersecting == nullptr) {
            firstIntersecting = &candidate;
        }
        if (candidate.interval.start == span.interval.start) {
            storageAligned = &candidate;
            break;
        }
    }
    if (storageAligned != nullptr) {
        return *storageAligned;
    }
    return firstIntersecting != nullptr ? *firstIntersecting : makeEmptyProjectedInterval();
}

}  // namespace

namespace IntervalProjection {

uint32_t tickPhaseInLoop(uint32_t currentTick, uint32_t startLoopTick, uint32_t loopLengthTicks) {
    if (loopLengthTicks == 0) {
        return 0;
    }
    const int64_t delta = static_cast<int64_t>(currentTick) - static_cast<int64_t>(startLoopTick);
    const int64_t loop = static_cast<int64_t>(loopLengthTicks);
    int64_t phase = delta % loop;
    if (phase < 0) {
        phase += loop;
    }
    return static_cast<uint32_t>(phase);
}

uint32_t noteRelativeTick(uint32_t absolutePos, uint32_t loopStartTick, uint32_t loopLength) {
    if (loopLength == 0) {
        return 0;
    }
    const uint32_t relativePos = (absolutePos >= loopStartTick)
                                     ? (absolutePos - loopStartTick)
                                     : (absolutePos + loopLength - loopStartTick);
    return relativePos % loopLength;
}

uint32_t noteStorageTick(uint32_t relativeTick, uint32_t loopStartTick, uint32_t loopLength) {
    if (loopLength == 0) {
        return 0;
    }
    return (relativeTick + loopStartTick) % loopLength;
}

uint32_t tickPhaseInProjectionCycle(uint32_t currentTick, int32_t projectionCycleStartTick,
                                    uint32_t loopLength) {
    if (loopLength == 0) {
        return 0;
    }
    const int64_t delta =
        static_cast<int64_t>(currentTick) - static_cast<int64_t>(projectionCycleStartTick);
    const int64_t loop = static_cast<int64_t>(loopLength);
    int64_t phase = delta % loop;
    if (phase < 0) {
        phase += loop;
    }
    return static_cast<uint32_t>(phase);
}

bool didDisplayPlayheadWrapBackward(uint32_t newStoragePhase, uint32_t prevStoragePhase,
                                    uint32_t loopStartTick, uint32_t loopLength) {
    if (loopLength == 0 || prevStoragePhase == UINT32_MAX) {
        return false;
    }
    const uint32_t newDisplay = noteRelativeTick(newStoragePhase, loopStartTick, loopLength);
    const uint32_t prevDisplay = noteRelativeTick(prevStoragePhase, loopStartTick, loopLength);
    return newDisplay < prevDisplay;
}

bool isPlaybackAtLoopStart(uint32_t prevTickInLoop, uint32_t tickInLoop,
                           bool allowFreshOriginCatchUp) {
    if (prevTickInLoop == UINT32_MAX) {
        return allowFreshOriginCatchUp;
    }
    return tickInLoop < prevTickInLoop;
}

int32_t advanceProjectionCycleStartTickOnWrap(int32_t projectionCycleStartTick,
                                              uint32_t loopLengthAtWrap) {
    return projectionCycleStartTick + static_cast<int32_t>(loopLengthAtWrap);
}

ProjectedIntervalVec generateEquivalentIntervals(const CanonicalNoteSpan& span, uint32_t loopLength,
                                                 const ProjectionContext& context) {
    ProjectedIntervalVec candidates;
    if (loopLength == 0) {
        return candidates;
    }

    int32_t kMin = 0;
    int32_t kMax = 0;
    if (!computeKBounds(span.interval.start, span.interval.end, loopLength, context.window, kMin,
                      kMax)) {
        return candidates;
    }

    const int64_t loop = static_cast<int64_t>(loopLength);
    candidates.reserve(static_cast<size_t>(kMax - kMin + 1));
    for (int32_t k = kMin; k <= kMax; ++k) {
        const int64_t shiftedStart = static_cast<int64_t>(span.interval.start) + k * loop;
        const int64_t shiftedEnd = static_cast<int64_t>(span.interval.end) + k * loop;
        if (shiftedStart > std::numeric_limits<int32_t>::max() ||
            shiftedStart < std::numeric_limits<int32_t>::min() ||
            shiftedEnd > std::numeric_limits<int32_t>::max() ||
            shiftedEnd < std::numeric_limits<int32_t>::min()) {
            continue;
        }
        candidates.push_back(ProjectedNoteInterval{
            span.noteId,
            TickInterval{static_cast<int32_t>(shiftedStart), static_cast<int32_t>(shiftedEnd)},
            span.pitch,
        });
    }
    return candidates;
}

ProjectedNoteInterval selectProjectedInterval(const ProjectedIntervalVec& candidates,
                                                const ProjectionContext& context) {
    if (candidates.empty()) {
        return makeEmptyProjectedInterval();
    }

    const ProjectedNoteInterval* best = nullptr;
    int64_t bestDistance = std::numeric_limits<int64_t>::max();

    for (const ProjectedNoteInterval& candidate : candidates) {
        const int64_t distance = distanceToInterval(context.originTick, candidate.interval);
        if (distance == 0) {
            return candidate;
        }
        if (distance < bestDistance) {
            bestDistance = distance;
            best = &candidate;
        }
    }

    return best != nullptr ? *best : makeEmptyProjectedInterval();
}

ProjectedIntervalVec selectProjectedIntervalsForDisplay(const ProjectedIntervalVec& candidates,
                                                        const ProjectionContext& context) {
    ProjectedIntervalVec selected;
    selected.reserve(candidates.size());
    for (const ProjectedNoteInterval& candidate : candidates) {
        if (candidate.interval.intersects(context.window)) {
            selected.push_back(candidate);
        }
    }
    return selected;
}

ProjectedIntervalVec projectNoteIntervals(const CanonicalNoteSpanVec& spans,
                                                        const ProjectionContext& context) {
    ProjectedIntervalVec projected;
    for (const CanonicalNoteSpan& span : spans) {
        const ProjectedIntervalVec candidates =
            generateEquivalentIntervals(span, context.loopLength, context);
        if (context.type == ProjectionType::Display) {
            const ProjectedIntervalVec selected =
                selectProjectedIntervalsForDisplay(candidates, context);
            projected.insert(projected.end(), selected.begin(), selected.end());
        } else {
            const ProjectedNoteInterval selected = selectProjectedInterval(candidates, context);
            if (selected.noteId != kInvalidNoteId) {
                projected.push_back(selected);
            }
        }
    }
    return projected;
}

TickInterval makeFullLoopEditAnalysisWindow(uint32_t loopLength) {
    return TickInterval{0, static_cast<int32_t>(loopLength)};
}

ProjectionContext buildEditProjectionContext(const EditorSelection& selection, uint32_t loopLength,
                                             TickInterval analysisWindow,
                                             int32_t primaryLinearStartTick, int32_t loopStartTick) {
    ProjectionContext context{};
    context.type = ProjectionType::Edit;
    context.loopLength = loopLength;
    context.window = analysisWindow;
    context.selectedTick = static_cast<int32_t>(selection.selectedTick);
    context.originTick = primaryLinearStartTick;
    context.loopStartTick = loopStartTick;
    (void)selection.primaryNote;
    return context;
}

ProjectedNoteInterval projectEditLinearSpan(const CanonicalNoteSpan& span,
                                              const ProjectionContext& context) {
    const ProjectedIntervalVec candidates =
        generateEquivalentIntervals(span, context.loopLength, context);
    return selectProjectedInterval(candidates, context);
}

ProjectedIntervalVec projectEditIntervalsForAnalysis(const CanonicalNoteSpanVec& spans,
                                                     const ProjectionContext& context) {
    ProjectionContext editContext = context;
    editContext.type = ProjectionType::Edit;
    return projectNoteIntervals(spans, editContext);
}

TickInterval makeFullLoopDisplayWindow(uint32_t loopLength) {
    return TickInterval{0, static_cast<int32_t>(loopLength)};
}

ProjectionContext buildDisplayProjectionContext(uint32_t loopLength, TickInterval window,
                                                int32_t loopStartTick) {
    ProjectionContext context{};
    context.type = ProjectionType::Display;
    context.loopLength = loopLength;
    context.window = window;
    context.loopStartTick = loopStartTick;
    return context;
}

TickInterval makeFullLoopPlaybackWindow(uint32_t loopLength) {
    return TickInterval{0, static_cast<int32_t>(loopLength)};
}

uint32_t playbackEventPhase(uint32_t storageTick, uint32_t loopLength) {
    if (loopLength == 0) {
        return 0;
    }
    const int32_t spanStart = static_cast<int32_t>(storageTick);
    const int32_t spanEnd = spanStart + 1;
    const TickInterval window{0, static_cast<int32_t>(loopLength)};
    int32_t kMin = 0;
    int32_t kMax = 0;
    if (!computeKBounds(spanStart, spanEnd, loopLength, window, kMin, kMax)) {
        return storageTick % loopLength;
    }
    const int32_t loop = static_cast<int32_t>(loopLength);
    for (int32_t k = kMin; k <= kMax; ++k) {
        const int32_t start = spanStart + k * loop;
        if (start >= 0 && start < static_cast<int32_t>(loopLength)) {
            return static_cast<uint32_t>(start);
        }
    }
    return storageTick % loopLength;
}

ProjectionContext buildPlaybackProjectionContext(uint32_t loopLength, TickInterval window,
                                                 int32_t originTick,
                                                 int32_t projectionCycleStartTick,
                                                 int32_t loopStartTick, bool useQueuedStart,
                                                 int32_t queuedStartTick) {
    ProjectionContext context{};
    context.type = ProjectionType::Playback;
    context.loopLength = loopLength;
    context.window = window;
    context.originTick = originTick;
    context.projectionCycleStartTick = projectionCycleStartTick;
    context.loopStartTick = loopStartTick;
    context.useQueuedStart = useQueuedStart;
    context.queuedStartTick = queuedStartTick;
    return context;
}

uint32_t projectPlaybackEventPhase(uint32_t storageTick, const ProjectionContext& context) {
    (void)context.originTick;
    (void)context.projectionCycleStartTick;
    (void)context.useQueuedStart;
    (void)context.queuedStartTick;
    (void)context.loopStartTick;
    return playbackEventPhase(storageTick, context.loopLength);
}

NoteUtils::DisplayNoteVec projectDisplayNotes(const CanonicalNoteSpanVec& spans,
                                              const ProjectionContext& context,
                                              uint32_t playheadTick) {
    NoteUtils::DisplayNoteVec notes;
    if (context.loopLength == 0) {
        return notes;
    }

    ProjectionContext displayContext = context;
    displayContext.type = ProjectionType::Display;

    for (const CanonicalNoteSpan& span : spans) {
        const ProjectedIntervalVec candidates =
            generateEquivalentIntervals(span, displayContext.loopLength, displayContext);
        const ProjectedNoteInterval selected =
            selectSingleDisplayProjectedInterval(candidates, span, displayContext);
        if (selected.interval.start < selected.interval.end) {
            renderProjectedIntervalToDisplayNotes(selected, span.velocity, span.isOpen,
                                                  span.splitHeadTail, displayContext.loopLength,
                                                  playheadTick, notes);
        }
    }
    return notes;
}

bool isInflatedDisplaySpan(const NoteUtils::DisplayNote& displayNote, uint32_t loopLength) {
    if (loopLength == 0 || displayNote.endTick < displayNote.startTick) {
        return displayNote.endTick < displayNote.startTick;
    }
    return (displayNote.endTick - displayNote.startTick) > loopLength / 2;
}

}  // namespace IntervalProjection
