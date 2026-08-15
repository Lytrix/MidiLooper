//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/NoteUtils.h"
#include "MidiEvent.h"
#include "Utils/IntervalProjection.h"
#include "Utils/MidiEventVecFnvHash.h"
#include "Logger.h"
#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include "Utils/NoteEditMem.h"

namespace {

constexpr size_t kReconstructVerboseMaxEvents = 32;

struct RankedNote {
    uint8_t note = 0;
    uint32_t startTick = 0;
    uint32_t endTick = 0;
    uint32_t index = 0;
};

NOTE_EDIT_MEM int compareRankedNoteKeyThenIndex(const void* lhs, const void* rhs) {
    const RankedNote& a = *static_cast<const RankedNote*>(lhs);
    const RankedNote& b = *static_cast<const RankedNote*>(rhs);
    if (a.note != b.note) {
        return a.note < b.note ? -1 : 1;
    }
    if (a.startTick != b.startTick) {
        return a.startTick < b.startTick ? -1 : 1;
    }
    if (a.endTick != b.endTick) {
        return a.endTick < b.endTick ? -1 : 1;
    }
    if (a.index != b.index) {
        return a.index < b.index ? -1 : 1;
    }
    return 0;
}

NOTE_EDIT_MEM int compareRankedNoteIndex(const void* lhs, const void* rhs) {
    const RankedNote& a = *static_cast<const RankedNote*>(lhs);
    const RankedNote& b = *static_cast<const RankedNote*>(rhs);
    if (a.index == b.index) {
        return 0;
    }
    return a.index < b.index ? -1 : 1;
}

template <typename Alloc>
NOTE_EDIT_MEM NoteId noteIdAtOnTick(const std::vector<MidiEvent, Alloc>& events, uint8_t channel,
                      uint8_t pitch, uint32_t onTick) {
  for (const MidiEvent& evt : events) {
    if (evt.isNoteOn() && evt.channel == channel && evt.data.noteData.note == pitch &&
        evt.tick == onTick) {
      return evt.noteId;
    }
  }
  return kInvalidNoteId;
}

template <typename Alloc>
NOTE_EDIT_MEM bool allLaterOnsInTailOrNone(const std::vector<MidiEvent, Alloc>& midiEvents,
                             uint32_t headOffTick, uint8_t pitch, uint8_t channel,
                             uint32_t loopLength) {
    const uint32_t tailStart = NoteUtils::wrapTailStartTick(loopLength);
    for (const MidiEvent& evt : midiEvents) {
        if (!evt.isNoteOn() || evt.channel != channel || evt.data.noteData.note != pitch) {
            continue;
        }
        if (evt.tick >= loopLength) {
            continue;
        }
        if (evt.tick > headOffTick && evt.tick < tailStart) {
            return false;
        }
    }
    return true;
}

template <typename Alloc>
NOTE_EDIT_MEM bool isPreferredWrapTailForHeadOffInSpanBuild(uint32_t tailOnTick, uint32_t headOffTick,
                          const std::vector<MidiEvent, Alloc>& midiEvents, uint8_t pitch,
                          uint8_t channel, uint32_t loopLength) {
    if (!NoteUtils::isHeadTailWrappedPair(tailOnTick, headOffTick, loopLength)) {
        return false;
    }
    for (const auto& evt : midiEvents) {
        if (!evt.isNoteOn() || evt.channel != channel || evt.data.noteData.note != pitch) {
            continue;
        }
        if (evt.tick >= loopLength) {
            continue;
        }
        if (evt.tick > tailOnTick &&
            NoteUtils::isHeadTailWrappedPair(evt.tick, headOffTick, loopLength)) {
            return false;
        }
    }
    return true;
}

template <typename Alloc>
NOTE_EDIT_MEM bool tryPairWrappedTailOn(const std::vector<MidiEvent, Alloc>& midiEvents,
                          uint32_t noteOffTick, uint8_t pitch, uint8_t channel, uint32_t loopLength,
                          uint32_t& outOnTick, uint8_t& outVelocity) {
    // Tick-0 Off is the loop-wrap release of a tail On. A later same-pitch On in
    // the body is the next performance (012925 note 30 @ 672 after Off @ 0), not
    // an intervening owner of that Off.
    if (noteOffTick != 0 &&
        !allLaterOnsInTailOrNone(midiEvents, noteOffTick, pitch, channel, loopLength)) {
        return false;
    }
    uint32_t bestOnTick = 0;
    uint8_t bestVelocity = 0;
    for (const auto& later : midiEvents) {
        const bool laterOn = (later.type == midi::NoteOn && later.data.noteData.velocity > 0);
        if (!laterOn || later.data.noteData.note != pitch || later.channel != channel) {
            continue;
        }
        const uint32_t onTick = later.tick;
        if (onTick >= loopLength) {
            continue;
        }
        if (!isPreferredWrapTailForHeadOffInSpanBuild(onTick, noteOffTick, midiEvents, pitch,
                                                      channel, loopLength)) {
            continue;
        }
        if (onTick > bestOnTick) {
            bestOnTick = onTick;
            bestVelocity = later.data.noteData.velocity;
        }
    }
    if (bestOnTick == 0) {
        return false;
    }
    outOnTick = bestOnTick;
    outVelocity = bestVelocity;
    return true;
}

NOTE_EDIT_MEM bool shouldLogReconstructDetails(bool verboseLog, size_t eventCount) {
    return verboseLog && eventCount <= kReconstructVerboseMaxEvents;
}

template <typename Alloc>
NOTE_EDIT_MEM bool hasDeferredLoopEndHeadOff(const std::vector<MidiEvent, Alloc>& midiEvents,
                               size_t fromIndex, uint8_t pitch, uint8_t channel, uint32_t tailOnTick,
                               uint32_t loopLength) {
    for (size_t j = fromIndex + 1; j < midiEvents.size(); ++j) {
        const MidiEvent& later = midiEvents[j];
        if (!later.isNoteOff() || later.channel != channel ||
            later.data.noteData.note != pitch) {
            continue;
        }
        uint32_t headOffTick = later.tick;
        if (headOffTick >= loopLength) {
            headOffTick %= loopLength;
        }
        if (isPreferredWrapTailForHeadOffInSpanBuild(tailOnTick, headOffTick, midiEvents, pitch,
                                                     channel, loopLength)) {
            return true;
        }
    }
    return false;
}

template <typename Alloc>
NOTE_EDIT_MEM bool isLatestTailNoteOn(const std::vector<MidiEvent, Alloc>& midiEvents,
                        uint32_t tailOnTick, uint8_t pitch, uint8_t channel, uint32_t loopLength) {
    const uint32_t tailStart = NoteUtils::wrapTailStartTick(loopLength);
    for (const MidiEvent& evt : midiEvents) {
        if (!evt.isNoteOn() || evt.channel != channel || evt.data.noteData.note != pitch) {
            continue;
        }
        if (evt.tick >= loopLength) {
            continue;
        }
        if (evt.tick >= tailStart && evt.tick > tailOnTick) {
            return false;
        }
    }
    return true;
}

template <typename Alloc>
NOTE_EDIT_MEM bool shouldDeferLoopEndOff(const std::vector<MidiEvent, Alloc>& midiEvents,
                           size_t fromIndex, uint8_t pitch, uint8_t channel, uint32_t tailOnTick,
                           uint32_t loopLength) {
    if (hasDeferredLoopEndHeadOff(midiEvents, fromIndex, pitch, channel, tailOnTick, loopLength)) {
        return true;
    }
    if (!isLatestTailNoteOn(midiEvents, tailOnTick, pitch, channel, loopLength)) {
        return false;
    }
    for (size_t j = fromIndex; j < midiEvents.size(); ++j) {
        const MidiEvent& evt = midiEvents[j];
        if (evt.isNoteOff() && evt.channel == channel && evt.data.noteData.note == pitch &&
            evt.tick == loopLength - 1) {
            return true;
        }
    }
    return false;
}

template <typename Alloc>
NOTE_EDIT_MEM bool isWrapHeldOpenNoteImpl(const std::vector<MidiEvent, Alloc>& midiEvents,
                            const NoteUtils::OpenNoteOn& open, uint32_t loopLength) {
    if (loopLength == 0) {
        return false;
    }
    const uint32_t tailStart = NoteUtils::wrapTailStartTick(loopLength);
    if (open.tick < tailStart) {
        return false;
    }

    uint8_t channel = 0;
    bool channelKnown = false;
    for (const MidiEvent& evt : midiEvents) {
        if (evt.isNoteOn() && evt.data.noteData.note == open.note && evt.tick == open.tick) {
            channel = evt.channel;
            channelKnown = true;
            break;
        }
    }

    // Prefer explicit head-off pairing: a tail note-on with a head note-off (including tick 0)
    // should be treated as wrap-held even when no loop-end synthetic off exists yet.
    if (channelKnown) {
        for (const MidiEvent& evt : midiEvents) {
            if (!evt.isNoteOff() || evt.channel != channel ||
                evt.data.noteData.note != open.note) {
                continue;
            }
            uint32_t headOffTick = evt.tick;
            if (headOffTick >= loopLength) {
                headOffTick %= loopLength;
            }
            if (headOffTick >= loopLength - 1) {
                continue;
            }
            if (NoteUtils::isPreferredWrapTailForHeadOff(open.tick, headOffTick, midiEvents,
                                                         open.note, channel, loopLength)) {
                return true;
            }
        }
    }

    bool hasLoopEndOff = false;
    for (const MidiEvent& evt : midiEvents) {
        if (!evt.isNoteOff() || evt.data.noteData.note != open.note) {
            continue;
        }
        if (!channelKnown || evt.channel == channel) {
            if (evt.tick == loopLength - 1) {
                hasLoopEndOff = true;
                break;
            }
        }
    }
    if (!hasLoopEndOff) {
        return false;
    }

    if (!channelKnown) {
        return isLatestTailNoteOn(midiEvents, open.tick, open.note, 0, loopLength);
    }

    return isLatestTailNoteOn(midiEvents, open.tick, open.note, channel, loopLength);
}

template <typename Alloc>
NOTE_EDIT_MEM bool isPreferredWrapTailForHeadOffImpl(uint32_t tailOnTick, uint32_t headOffTick,
                                                     const std::vector<MidiEvent, Alloc>& midiEvents,
                                                     uint8_t pitch, uint8_t channel,
                                                     uint32_t loopLength) {
    if (!NoteUtils::isHeadTailWrappedPair(tailOnTick, headOffTick, loopLength)) {
        return false;
    }
    for (const auto& evt : midiEvents) {
        if (!evt.isNoteOn() || evt.channel != channel || evt.data.noteData.note != pitch) {
            continue;
        }
        if (evt.tick >= loopLength) {
            continue;
        }
        if (evt.tick > tailOnTick &&
            NoteUtils::isHeadTailWrappedPair(evt.tick, headOffTick, loopLength)) {
            return false;
        }
    }
    return true;
}

}  // namespace

NOTE_EDIT_MEM bool NoteUtils::isWrapHeldOpenNote(const MidiEventVec& midiEvents, const OpenNoteOn& open,
                                   uint32_t loopLength) {
    return isWrapHeldOpenNoteImpl(midiEvents, open, loopLength);
}

NOTE_EDIT_MEM bool NoteUtils::isWrapHeldOpenNote(const SessionMidiEventVec& midiEvents,
                                   const OpenNoteOn& open, uint32_t loopLength) {
    return isWrapHeldOpenNoteImpl(midiEvents, open, loopLength);
}

NOTE_EDIT_MEM bool NoteUtils::isPreferredWrapTailForHeadOff(uint32_t tailOnTick, uint32_t headOffTick,
                                              const MidiEventVec& midiEvents, uint8_t pitch,
                                              uint8_t channel, uint32_t loopLength) {
    return isPreferredWrapTailForHeadOffImpl(tailOnTick, headOffTick, midiEvents, pitch, channel,
                                            loopLength);
}

NOTE_EDIT_MEM bool NoteUtils::isPreferredWrapTailForHeadOff(uint32_t tailOnTick, uint32_t headOffTick,
                                              const SessionMidiEventVec& midiEvents, uint8_t pitch,
                                              uint8_t channel, uint32_t loopLength) {
    return isPreferredWrapTailForHeadOffImpl(tailOnTick, headOffTick, midiEvents, pitch, channel,
                                            loopLength);
}

NOTE_EDIT_MEM bool NoteUtils::wrapPairIsUnblocked(const MidiEventVec& midiEvents, uint32_t offTick, uint32_t onTick,
                                    uint8_t pitch, uint8_t channel) {
    for (const auto& evt : midiEvents) {
        if (!evt.isNoteOn() || evt.channel != channel || evt.data.noteData.note != pitch) {
            continue;
        }
        if (evt.tick > offTick && evt.tick < onTick) {
            return false;
        }
    }
    return true;
}

NOTE_EDIT_MEM NoteUtils::WrapHeadSegment NoteUtils::resolveWrapHeadSegment(uint32_t loopLength,
                                                                 uint32_t headEndInclusive,
                                                                 NoteUtils::WrapHeadSegmentContext context,
                                                                 uint32_t wrapWindowTicks,
                                                                 uint32_t tailOnTick) {
    NoteUtils::WrapHeadSegment segment;
    if (loopLength == 0 || loopLength < 2) {
        return segment;
    }
    if (headEndInclusive >= loopLength - 1) {
        return segment;
    }
    const uint32_t wrapWindow =
        wrapWindowTicks > loopLength ? loopLength : wrapWindowTicks;
    if (headEndInclusive >= wrapWindow) {
        return segment;
    }
    if (context == NoteUtils::WrapHeadSegmentContext::LivePlayhead && tailOnTick != UINT32_MAX &&
        headEndInclusive >= tailOnTick) {
        return segment;
    }
    if (context == NoteUtils::WrapHeadSegmentContext::CommittedHeadOff && headEndInclusive == 0) {
        return segment;
    }
    segment.visible = true;
    segment.startTick = 0;
    segment.endTickInclusive = headEndInclusive;
    return segment;
}

NOTE_EDIT_MEM uint32_t NoteUtils::wrapHeadExclusiveEndForDraw(uint32_t endTickInclusive,
                                                            uint32_t loopLength) {
    if (loopLength == 0 || endTickInclusive >= loopLength - 1) {
        return 0;
    }
    return endTickInclusive + 1;
}

// CachedNoteList implementation
NOTE_EDIT_MEM uint32_t NoteUtils::CachedNoteList::computeMidiHash(const MidiEventVec& midiEvents) {
    return midiEventVecFnv1aHash(midiEvents);
}

NOTE_EDIT_MEM uint32_t NoteUtils::CachedNoteList::computeMidiHash(const SessionMidiEventVec& midiEvents) {
    return midiEventVecFnv1aHash(midiEvents);
}

const NoteUtils::DisplayNoteVec&
NoteUtils::CachedNoteList::getNotes(const MidiEventVec& midiEvents, uint32_t loopLength) {
    uint32_t currentHash = computeMidiHash(midiEvents);
    
    if (isValid && currentHash == lastMidiHash && loopLength == lastLoopLength) {
        return cachedNotes; // Return cached result
    }
    
    // Cache miss - rebuild notes
    cachedNotes = reconstructDisplayNotes(midiEvents, loopLength, false);
    lastMidiHash = currentHash;
    lastLoopLength = loopLength;
    isValid = true;
    
    return cachedNotes;
}

const NoteUtils::DisplayNoteVec&
NoteUtils::CachedNoteList::getNotes(const SessionMidiEventVec& midiEvents, uint32_t loopLength) {
    uint32_t currentHash = computeMidiHash(midiEvents);

    if (isValid && currentHash == lastMidiHash && loopLength == lastLoopLength) {
        return cachedNotes;
    }

    cachedNotes = reconstructDisplayNotes(midiEvents, loopLength, false);
    lastMidiHash = currentHash;
    lastLoopLength = loopLength;
    isValid = true;

    return cachedNotes;
}

struct ActiveCanonicalNote {
    NoteId noteId = kInvalidNoteId;
    uint8_t pitch = 0;
    uint8_t velocity = 0;
    uint32_t startTick = 0;
};

NOTE_EDIT_MEM int32_t exclusiveEndForWrappedHeadOff(uint32_t headOffTick, uint32_t loopLength) {
    return static_cast<int32_t>(headOffTick + loopLength + 1);
}

NOTE_EDIT_MEM int32_t exclusiveEndForLinearOff(uint32_t linearOffTick) {
    return static_cast<int32_t>(linearOffTick + 1);
}

NOTE_EDIT_MEM int32_t exclusiveEndForLoopOff(uint32_t loopOffTick) {
    return static_cast<int32_t>(loopOffTick + 1);
}

NOTE_EDIT_MEM void pushCanonicalSpan(CanonicalNoteSpanVec& spans, NoteId noteId, uint8_t pitch,
                                     uint8_t velocity, int32_t startTick, int32_t exclusiveEndTick,
                                     bool isOpen = false, bool splitHeadTail = false) {
    CanonicalNoteSpan span;
    span.noteId = noteId;
    span.pitch = pitch;
    span.velocity = velocity;
    span.interval.start = startTick;
    span.interval.end = exclusiveEndTick;
    span.isOpen = isOpen;
    span.splitHeadTail = splitHeadTail;
    spans.push_back(span);
}

using ActiveCanonicalNoteVec =
    std::vector<ActiveCanonicalNote, ExternalMemoryFirstAllocator<ActiveCanonicalNote>>;
using ActiveNoteStackMap = std::map<uint8_t, ActiveCanonicalNoteVec, std::less<uint8_t>,
                                   ExternalMemoryFirstAllocator<std::pair<const uint8_t, ActiveCanonicalNoteVec>>>;
using WrappedTailOnTickSet =
    std::set<std::pair<uint8_t, uint32_t>, std::less<std::pair<uint8_t, uint32_t>>,
             ExternalMemoryFirstAllocator<std::pair<uint8_t, uint32_t>>>;

struct NoteUtils::CanonicalSpanBuild::Impl {
    CanonicalNoteSpanVec spans;
    ActiveNoteStackMap activeNoteStacks;
    WrappedTailOnTickSet wrappedTailOnTicks;
};

namespace {

template <typename EventAlloc>
NOTE_EDIT_MEM void appendCanonicalSpansFromMidiRange(
    const std::vector<MidiEvent, EventAlloc>& midiEvents, uint32_t loopLength, bool verboseLog,
    uint32_t beginEvent, uint32_t endEventExclusive, CanonicalNoteSpanVec& spans,
    ActiveNoteStackMap& activeNoteStacks, WrappedTailOnTickSet& wrappedTailOnTicks) {
    if (loopLength == 0) {
        return;
    }
    const uint32_t limit = static_cast<uint32_t>(midiEvents.size());
    if (beginEvent >= limit) {
        return;
    }
    if (endEventExclusive > limit) {
        endEventExclusive = limit;
    }

    const bool logDetails = shouldLogReconstructDetails(verboseLog, midiEvents.size());
    const uint32_t tailStart = NoteUtils::wrapTailStartTick(loopLength);

    for (uint32_t eventIndex = beginEvent; eventIndex < endEventExclusive; ++eventIndex) {
        const MidiEvent& evt = midiEvents[eventIndex];
        const bool isNoteOn = (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0);
        const bool isNoteOff =
            (evt.type == midi::NoteOff || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0));
        const uint8_t pitch = evt.data.noteData.note;

        if (isNoteOn) {
            uint32_t noteOnTick = evt.tick;
            if (wrappedTailOnTicks.count({pitch, noteOnTick}) != 0) {
                continue;
            }
            if (noteOnTick >= loopLength) {
                if (logDetails) {
                    logger.log(CAT_TRACK, LOG_DEBUG,
                               "Discarding note-on beyond loop boundary: pitch=%d, tick=%lu, loop=%lu",
                               pitch, noteOnTick, loopLength);
                }
                continue;
            }

            ActiveCanonicalNote note;
            note.noteId = evt.noteId;
            note.pitch = pitch;
            note.velocity = evt.data.noteData.velocity;
            note.startTick = noteOnTick;
            activeNoteStacks[pitch].push_back(note);
        } else if (isNoteOff) {
            uint32_t noteOffTick = evt.tick;
            const bool offWasBeyondLoop = noteOffTick >= loopLength;
            if (offWasBeyondLoop) {
                noteOffTick = IntervalProjection::tickPhaseInLoop(noteOffTick, 0, loopLength);
            }

            if (activeNoteStacks[pitch].empty()) {
                uint32_t wrappedOnTick = 0;
                uint8_t wrappedVelocity = 0;
                if (tryPairWrappedTailOn(midiEvents, noteOffTick, pitch, evt.channel, loopLength,
                                         wrappedOnTick, wrappedVelocity)) {
                    const NoteId wrapId =
                        noteIdAtOnTick(midiEvents, evt.channel, pitch, wrappedOnTick);
                    pushCanonicalSpan(spans, wrapId, pitch, wrappedVelocity,
                                      static_cast<int32_t>(wrappedOnTick),
                                      exclusiveEndForWrappedHeadOff(noteOffTick, loopLength), false,
                                      true);
                    wrappedTailOnTicks.insert({pitch, wrappedOnTick});
                    continue;
                }
                continue;
            }

            const uint32_t pairingOffTick = offWasBeyondLoop ? evt.tick : noteOffTick;
            ActiveCanonicalNoteVec& stack = activeNoteStacks[pitch];
            int pairIndex = -1;
            if (!stack.empty() && stack.back().startTick < pairingOffTick) {
                pairIndex = static_cast<int>(stack.size()) - 1;
            } else {
                for (int stackIndex = static_cast<int>(stack.size()) - 1; stackIndex >= 0;
                     --stackIndex) {
                    if (stack[static_cast<size_t>(stackIndex)].startTick < pairingOffTick) {
                        pairIndex = stackIndex;
                        break;
                    }
                }
                if (pairIndex < 0 && !stack.empty()) {
                    pairIndex = static_cast<int>(stack.size()) - 1;
                }
            }
            if (pairIndex < 0) {
                continue;
            }

            ActiveCanonicalNote& note = stack[static_cast<size_t>(pairIndex)];
            if (!offWasBeyondLoop && noteOffTick == loopLength - 1 &&
                note.startTick >= tailStart &&
                shouldDeferLoopEndOff(midiEvents, eventIndex, pitch, evt.channel, note.startTick,
                                      loopLength)) {
                continue;
            }

            const bool allowWrapSplitForBeyondLoopOff =
                offWasBeyondLoop && note.startTick >= tailStart && tailStart > 0;
            if (noteOffTick < note.startTick &&
                isPreferredWrapTailForHeadOffInSpanBuild(note.startTick, noteOffTick, midiEvents,
                                                         pitch, evt.channel, loopLength) &&
                note.startTick >= tailStart &&
                (!offWasBeyondLoop || allowWrapSplitForBeyondLoopOff)) {
                // Head-off exactly at loop wrap (tick 0) represents an end-at-boundary note.
                // Treat as tail-only (no [0..0] head segment).
                if (noteOffTick == 0) {
                    pushCanonicalSpan(spans, note.noteId, pitch, note.velocity,
                                      static_cast<int32_t>(note.startTick),
                                      exclusiveEndForLoopOff(loopLength - 1));
                    stack.erase(stack.begin() + static_cast<std::ptrdiff_t>(pairIndex));
                    continue;
                }
                pushCanonicalSpan(spans, note.noteId, pitch, note.velocity,
                                  static_cast<int32_t>(note.startTick),
                                  exclusiveEndForWrappedHeadOff(noteOffTick, loopLength), false,
                                  true);
                stack.erase(stack.begin() + static_cast<std::ptrdiff_t>(pairIndex));
                continue;
            }

            const int32_t exclusiveEnd = offWasBeyondLoop
                                             ? exclusiveEndForLinearOff(evt.tick)
                                             : exclusiveEndForLoopOff(noteOffTick);
            pushCanonicalSpan(spans, note.noteId, pitch, note.velocity,
                              static_cast<int32_t>(note.startTick), exclusiveEnd);
            stack.erase(stack.begin() + static_cast<std::ptrdiff_t>(pairIndex));
        }
    }
}

NOTE_EDIT_MEM void finishCanonicalSpansOpenNotes(uint32_t loopLength, CanonicalNoteSpanVec& spans,
                                                 ActiveNoteStackMap& activeNoteStacks) {
    if (loopLength == 0) {
        return;
    }
    for (auto& [pitch, noteStack] : activeNoteStacks) {
        for (const ActiveCanonicalNote& note : noteStack) {
            pushCanonicalSpan(spans, note.noteId, pitch, note.velocity,
                              static_cast<int32_t>(note.startTick),
                              static_cast<int32_t>(loopLength), true);
        }
        (void)pitch;
    }
    activeNoteStacks.clear();
}

template <typename EventAlloc>
NOTE_EDIT_MEM CanonicalNoteSpanVec buildCanonicalSpansFromMidi(
    const std::vector<MidiEvent, EventAlloc>& midiEvents, uint32_t loopLength, bool verboseLog,
    bool finishOpenNotes) {
    CanonicalNoteSpanVec spans;
    ActiveNoteStackMap activeNoteStacks;
    WrappedTailOnTickSet wrappedTailOnTicks;
    if (loopLength == 0) {
        return spans;
    }
    const bool logDetails = shouldLogReconstructDetails(verboseLog, midiEvents.size());
    if (logDetails) {
        logger.log(CAT_TRACK, LOG_DEBUG, "Building canonical spans with loop length: %lu ticks",
                   loopLength);
    }
    appendCanonicalSpansFromMidiRange(midiEvents, loopLength, verboseLog, 0,
                                      static_cast<uint32_t>(midiEvents.size()), spans,
                                      activeNoteStacks, wrappedTailOnTicks);
    if (finishOpenNotes) {
        finishCanonicalSpansOpenNotes(loopLength, spans, activeNoteStacks);
    }
    return spans;
}

template <typename NoteVector>
NOTE_EDIT_MEM NoteVector dedupeProjectedDisplayNoteVec(const NoteUtils::DisplayNoteVec& projected,
                                                       size_t spanCountForLog, bool verboseLog) {
    using DisplayNote = NoteUtils::DisplayNote;
    NoteVector finalNotes;
    const size_t originalCount = projected.size();
    using RankedNoteVec = std::vector<RankedNote, ExternalMemoryFirstAllocator<RankedNote>>;
    RankedNoteVec ranked;
    ranked.reserve(projected.size());
    for (uint32_t i = 0; i < static_cast<uint32_t>(projected.size()); ++i) {
        const DisplayNote& note = projected[i];
        ranked.push_back(RankedNote{note.note, note.startTick, note.endTick, i});
    }
    if (!ranked.empty()) {
        qsort(ranked.data(), ranked.size(), sizeof(RankedNote), compareRankedNoteKeyThenIndex);
    }
    size_t uniqueCount = 0;
    for (size_t i = 0; i < ranked.size(); ++i) {
        if (uniqueCount == 0 || ranked[i].note != ranked[uniqueCount - 1].note ||
            ranked[i].startTick != ranked[uniqueCount - 1].startTick ||
            ranked[i].endTick != ranked[uniqueCount - 1].endTick) {
            ranked[uniqueCount++] = ranked[i];
        }
    }
    ranked.resize(uniqueCount);
    if (!ranked.empty()) {
        qsort(ranked.data(), ranked.size(), sizeof(RankedNote), compareRankedNoteIndex);
    }
    finalNotes.reserve(ranked.size());
    for (const RankedNote& row : ranked) {
        finalNotes.push_back(projected[row.index]);
    }
    if (shouldLogReconstructDetails(verboseLog, spanCountForLog)) {
        logger.log(CAT_TRACK, LOG_DEBUG,
                   "Reconstruction complete: %zu notes total (%zu duplicates removed)",
                   finalNotes.size(), originalCount - finalNotes.size());
    }
    return finalNotes;
}

template <typename NoteVector>
NOTE_EDIT_MEM NoteVector displayNotesFromCanonicalSpanVec(const CanonicalNoteSpanVec& spans,
                                                          uint32_t loopLength, bool verboseLog) {
    NoteVector finalNotes;
    if (loopLength == 0) {
        return finalNotes;
    }
    const TickInterval window = IntervalProjection::makeFullLoopDisplayWindow(loopLength);
    const ProjectionContext context =
        IntervalProjection::buildDisplayProjectionContext(loopLength, window);
    NoteUtils::DisplayNoteVec projected;
    IntervalProjection::projectDisplayNotes(spans, 0, static_cast<uint32_t>(spans.size()), context,
                                            projected);
    return dedupeProjectedDisplayNoteVec<NoteVector>(projected, spans.size(), verboseLog);
}

template <typename NoteVector, typename EventAlloc>
NoteVector reconstructNotesImpl(const std::vector<MidiEvent, EventAlloc>& midiEvents,
                                uint32_t loopLength, bool verboseLog, bool finishOpenNotes = true) {
    if (loopLength == 0) {
        return NoteVector{};
    }
    const CanonicalNoteSpanVec spans =
        buildCanonicalSpansFromMidi(midiEvents, loopLength, verboseLog, finishOpenNotes);
    return displayNotesFromCanonicalSpanVec<NoteVector>(spans, loopLength, verboseLog);
}

}  // namespace

NoteUtils::CanonicalSpanBuild::CanonicalSpanBuild() : impl(new Impl()) {}

NoteUtils::CanonicalSpanBuild::~CanonicalSpanBuild() = default;

NoteUtils::CanonicalSpanBuild::CanonicalSpanBuild(CanonicalSpanBuild&&) noexcept = default;

NoteUtils::CanonicalSpanBuild& NoteUtils::CanonicalSpanBuild::operator=(
    CanonicalSpanBuild&&) noexcept = default;

void NoteUtils::CanonicalSpanBuild::clear() { impl.reset(new Impl()); }

uint32_t NoteUtils::CanonicalSpanBuild::spanCount() const {
    return impl != nullptr ? static_cast<uint32_t>(impl->spans.size()) : 0;
}

NOTE_EDIT_MEM void NoteUtils::appendCanonicalSpansFromMidi(const SessionMidiEventVec& midiEvents,
                                                           uint32_t loopLength,
                                                           uint32_t beginEvent,
                                                           uint32_t endEventExclusive,
                                                           CanonicalSpanBuild& build) {
    if (build.impl == nullptr) {
        build.impl.reset(new CanonicalSpanBuild::Impl());
    }
    appendCanonicalSpansFromMidiRange(midiEvents, loopLength, false, beginEvent, endEventExclusive,
                                      build.impl->spans, build.impl->activeNoteStacks,
                                      build.impl->wrappedTailOnTicks);
}

NOTE_EDIT_MEM void NoteUtils::finishCanonicalSpansFromMidi(uint32_t loopLength,
                                                           CanonicalSpanBuild& build) {
    if (build.impl == nullptr) {
        return;
    }
    finishCanonicalSpansOpenNotes(loopLength, build.impl->spans, build.impl->activeNoteStacks);
}

NOTE_EDIT_MEM void NoteUtils::appendProjectedDisplayNotes(const CanonicalSpanBuild& build,
                                                          uint32_t loopLength, uint32_t beginSpan,
                                                          uint32_t endSpanExclusive,
                                                          DisplayNoteVec& out) {
    if (build.impl == nullptr || loopLength == 0) {
        return;
    }
    const TickInterval window = IntervalProjection::makeFullLoopDisplayWindow(loopLength);
    const ProjectionContext context =
        IntervalProjection::buildDisplayProjectionContext(loopLength, window);
    IntervalProjection::projectDisplayNotes(build.impl->spans, beginSpan, endSpanExclusive, context,
                                            out);
}

NOTE_EDIT_MEM NoteUtils::DisplayNoteVec NoteUtils::dedupeProjectedDisplayNotes(
    const DisplayNoteVec& projected) {
    return dedupeProjectedDisplayNoteVec<DisplayNoteVec>(projected, projected.size(), false);
}

NOTE_EDIT_MEM NoteUtils::DisplayNoteVec NoteUtils::displayNotesFromCanonicalSpans(
    const CanonicalSpanBuild& build, uint32_t loopLength) {
    if (build.impl == nullptr) {
        return DisplayNoteVec{};
    }
    return displayNotesFromCanonicalSpanVec<DisplayNoteVec>(build.impl->spans, loopLength, false);
}

NOTE_EDIT_MEM std::vector<NoteUtils::DisplayNote> NoteUtils::reconstructNotes(
    const MidiEventVec& midiEvents, uint32_t loopLength, bool verboseLog) {
    return reconstructNotesImpl<std::vector<DisplayNote>>(midiEvents, loopLength, verboseLog);
}

NOTE_EDIT_MEM std::vector<NoteUtils::DisplayNote> NoteUtils::reconstructNotes(
    const SessionMidiEventVec& midiEvents, uint32_t loopLength, bool verboseLog) {
    return reconstructNotesImpl<std::vector<DisplayNote>>(midiEvents, loopLength, verboseLog);
}

NOTE_EDIT_MEM NoteUtils::DisplayNoteVec NoteUtils::reconstructDisplayNotes(
    const MidiEventVec& midiEvents, uint32_t loopLength, bool verboseLog, bool finishOpenNotes) {
    return reconstructNotesImpl<DisplayNoteVec>(midiEvents, loopLength, verboseLog, finishOpenNotes);
}

template <typename Alloc>
NOTE_EDIT_MEM NoteUtils::DisplayNoteVec NoteUtils::reconstructDisplayNotes(
    const std::vector<MidiEvent, Alloc>& midiEvents, uint32_t loopLength, bool verboseLog,
    bool finishOpenNotes) {
    return reconstructNotesImpl<DisplayNoteVec>(midiEvents, loopLength, verboseLog, finishOpenNotes);
}

template NoteUtils::DisplayNoteVec NoteUtils::reconstructDisplayNotes<InternalHeapFirstAllocator<MidiEvent>>(
    const MidiEventVec& midiEvents, uint32_t loopLength, bool verboseLog, bool finishOpenNotes);
template NoteUtils::DisplayNoteVec NoteUtils::reconstructDisplayNotes<ExternalMemoryFirstAllocator<MidiEvent>>(
    const SessionMidiEventVec& midiEvents, uint32_t loopLength, bool verboseLog,
    bool finishOpenNotes);

NOTE_EDIT_MEM std::vector<NoteUtils::OpenNoteOn> NoteUtils::findOpenNoteOns(const MidiEventVec& midiEvents,
                                                               uint32_t loopLength) {
    std::vector<OpenNoteOn> openNotes;
    if (loopLength == 0) {
        return openNotes;
    }

    std::map<uint8_t, std::vector<OpenNoteOn>> activeStacks;
    std::set<std::pair<uint8_t, uint32_t>> wrappedTailOnTicks;
    for (size_t eventIndex = 0; eventIndex < midiEvents.size(); ++eventIndex) {
        const MidiEvent& evt = midiEvents[eventIndex];
        if (evt.tick >= loopLength) {
            continue;
        }

        const bool isNoteOn = (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0);
        const bool isNoteOff =
            (evt.type == midi::NoteOff || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0));
        if (!isNoteOn && !isNoteOff) {
            continue;
        }

        const uint8_t pitch = evt.data.noteData.note;
        if (isNoteOn) {
            if (wrappedTailOnTicks.count({pitch, evt.tick}) != 0) {
                continue;
            }
            activeStacks[pitch].push_back({pitch, evt.data.noteData.velocity, evt.tick, eventIndex});
        } else if (!activeStacks[pitch].empty()) {
            const OpenNoteOn& open = activeStacks[pitch].back();
            const uint32_t tailStart = NoteUtils::wrapTailStartTick(loopLength);
            if (evt.tick == loopLength - 1 && open.tick >= tailStart &&
                shouldDeferLoopEndOff(midiEvents, eventIndex, pitch, evt.channel, open.tick,
                                      loopLength)) {
                continue;
            }
            activeStacks[pitch].pop_back();
        } else {
            uint32_t wrappedOnTick = 0;
            uint8_t wrappedVelocity = 0;
            if (tryPairWrappedTailOn(midiEvents, evt.tick, pitch, evt.channel, loopLength,
                                     wrappedOnTick, wrappedVelocity)) {
                wrappedTailOnTicks.insert({pitch, wrappedOnTick});
            }
        }
    }

    for (auto& [pitch, stack] : activeStacks) {
        (void)pitch;
        for (const auto& open : stack) {
            openNotes.push_back(open);
        }
    }

    return openNotes;
}

template <typename Alloc>
std::vector<NoteUtils::OpenNoteOn> NoteUtils::findOpenNoteOns(
    const std::vector<MidiEvent, Alloc>& midiEvents, uint32_t loopLength) {
    return findOpenNoteOns(MidiEventVec(midiEvents.begin(), midiEvents.end()), loopLength);
}

template std::vector<NoteUtils::OpenNoteOn> NoteUtils::findOpenNoteOns<ExternalMemoryFirstAllocator<MidiEvent>>(
    const SessionMidiEventVec&, uint32_t);

// Build a fast lookup index for NoteOn/NoteOff events
namespace {

template <typename Alloc>
NoteUtils::EventIndex buildEventIndexImpl(const std::vector<MidiEvent, Alloc>& midiEvents) {
    using Key = NoteUtils::Key;
    NoteUtils::EventIndexMap onIndex;
    NoteUtils::EventIndexMap offIndex;
    onIndex.reserve(midiEvents.size());
    offIndex.reserve(midiEvents.size());
    for (size_t i = 0; i < midiEvents.size(); ++i) {
        const auto& evt = midiEvents[i];
        const bool isOn = (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0);
        const bool isOff =
            (evt.type == midi::NoteOff || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0));
        if (isOn || isOff) {
            const Key key = (static_cast<Key>(evt.data.noteData.note) << 32) | evt.tick;
            if (isOn) {
                onIndex[key] = i;
            } else {
                offIndex[key] = i;
            }
        }
    }
    return {std::move(onIndex), std::move(offIndex)};
}

}  // namespace

NoteUtils::EventIndex NoteUtils::buildEventIndex(const MidiEventVec& midiEvents) {
    return buildEventIndexImpl(midiEvents);
}

NoteUtils::EventIndex NoteUtils::buildEventIndex(const SessionMidiEventVec& midiEvents) {
    return buildEventIndexImpl(midiEvents);
}

namespace {

NOTE_EDIT_MEM uint32_t pairedNoteOnTickForOffAtIndex(const MidiEventVec& midiEvents, uint8_t channel,
                                       uint8_t pitch, size_t offIndex) {
    std::vector<uint32_t> onTicks;
    for (size_t i = 0; i < midiEvents.size(); ++i) {
        const MidiEvent& evt = midiEvents[i];
        const bool isOn =
            evt.isNoteOn() && evt.data.noteData.velocity > 0 && evt.channel == channel &&
            evt.data.noteData.note == pitch;
        const bool isOff =
            evt.isNoteOff() && evt.channel == channel && evt.data.noteData.note == pitch;
        if (isOn) {
            onTicks.push_back(evt.tick);
        } else if (isOff) {
            if (i == offIndex) {
                return onTicks.empty() ? 0u : onTicks.back();
            }
            if (!onTicks.empty()) {
                onTicks.pop_back();
            }
        }
    }
    return 0u;
}

}  // namespace

NOTE_EDIT_MEM bool NoteUtils::notesOverlap(uint32_t start1, uint32_t end1, uint32_t start2, uint32_t end2,
                             uint32_t loopLength) {
    uint32_t unwrappedEnd1 = end1;
    uint32_t unwrappedEnd2 = end2;
    const bool wrapped1 = (end1 < start1);
    const bool wrapped2 = (end2 < start2);
    if (wrapped1) {
        unwrappedEnd1 = end1 + loopLength;
    }
    if (wrapped2) {
        unwrappedEnd2 = end2 + loopLength;
    }
    if (!wrapped1 && !wrapped2) {
        return (start1 < end2) && (start2 < end1);
    }
    if (wrapped1 && !wrapped2) {
        return (start1 < end2) || (start2 < unwrappedEnd1);
    }
    if (!wrapped1 && wrapped2) {
        return (start1 < unwrappedEnd2) || (start2 < end1);
    }
    return (start1 < unwrappedEnd2) || (start2 < unwrappedEnd1);
}

NOTE_EDIT_MEM void NoteUtils::sortMidiEventsChronologically(MidiEventVec& midiEvents) {
    std::sort(midiEvents.begin(), midiEvents.end(), [](const MidiEvent& a, const MidiEvent& b) {
        if (a.tick != b.tick) {
            return a.tick < b.tick;
        }
        const int aOrder = a.isNoteOff() ? 0 : (a.isNoteOn() ? 1 : 2);
        const int bOrder = b.isNoteOff() ? 0 : (b.isNoteOn() ? 1 : 2);
        return aOrder < bOrder;
    });
}

NOTE_EDIT_MEM void NoteUtils::orderSamePitchNoteOffsForLifo(MidiEventVec& midiEvents, uint8_t channel,
                                              uint8_t pitch) {
    std::map<uint32_t, std::vector<size_t>> offsByTick;
    for (size_t i = 0; i < midiEvents.size(); ++i) {
        const MidiEvent& evt = midiEvents[i];
        if (evt.isNoteOff() && evt.channel == channel && evt.data.noteData.note == pitch) {
            offsByTick[evt.tick].push_back(i);
        }
    }

    for (auto& [tick, indices] : offsByTick) {
        (void)tick;
        if (indices.size() < 2) {
            continue;
        }
        std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
            const uint32_t onA = pairedNoteOnTickForOffAtIndex(midiEvents, channel, pitch, a);
            const uint32_t onB = pairedNoteOnTickForOffAtIndex(midiEvents, channel, pitch, b);
            return onA > onB;
        });
        std::vector<MidiEvent> sortedOffs;
        sortedOffs.reserve(indices.size());
        for (size_t idx : indices) {
            sortedOffs.push_back(midiEvents[idx]);
        }
        for (size_t j = 0; j < indices.size(); ++j) {
            midiEvents[indices[j]] = sortedOffs[j];
        }
    }
}

NOTE_EDIT_MEM void NoteUtils::ensureNoteOffsBeforeNoteOnsAtTick(MidiEventVec& midiEvents, uint8_t pitch,
                                                    uint32_t tick) {
    std::vector<size_t> indices;
    indices.reserve(4);
    for (size_t i = 0; i < midiEvents.size(); ++i) {
        const MidiEvent& evt = midiEvents[i];
        if (evt.tick != tick || evt.data.noteData.note != pitch) {
            continue;
        }
        if (evt.isNoteOn() || evt.isNoteOff()) {
            indices.push_back(i);
        }
    }
    if (indices.size() < 2) {
        return;
    }

    size_t firstOn = midiEvents.size();
    size_t firstOff = midiEvents.size();
    for (size_t idx : indices) {
        if (midiEvents[idx].isNoteOn() && idx < firstOn) {
            firstOn = idx;
        }
        if (midiEvents[idx].isNoteOff() && idx < firstOff) {
            firstOff = idx;
        }
    }
    if (firstOn >= firstOff) {
        return;
    }

    std::vector<MidiEvent> offs;
    std::vector<MidiEvent> ons;
    offs.reserve(indices.size());
    ons.reserve(indices.size());
    for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
        const MidiEvent evt = midiEvents[*it];
        if (evt.isNoteOff()) {
            offs.push_back(evt);
        } else if (evt.isNoteOn()) {
            ons.push_back(evt);
        }
        midiEvents.erase(midiEvents.begin() + static_cast<std::ptrdiff_t>(*it));
    }
    std::reverse(offs.begin(), offs.end());

    const size_t insertPos = *std::min_element(indices.begin(), indices.end());
    size_t pos = insertPos;
    for (const auto& evt : offs) {
        midiEvents.insert(midiEvents.begin() + static_cast<std::ptrdiff_t>(pos), evt);
        ++pos;
    }
    for (const auto& evt : ons) {
        midiEvents.insert(midiEvents.begin() + static_cast<std::ptrdiff_t>(pos), evt);
        ++pos;
    }
}

NOTE_EDIT_MEM void NoteUtils::removeDuplicateNotePairsAtSpan(MidiEventVec& midiEvents, uint8_t pitch,
                                               uint32_t startTick, uint32_t endTick) {
    std::vector<size_t> onIndices;
    std::vector<size_t> offIndices;
    onIndices.reserve(4);
    offIndices.reserve(4);

    for (size_t i = 0; i < midiEvents.size(); ++i) {
        const auto& evt = midiEvents[i];
        if (evt.data.noteData.note != pitch) {
            continue;
        }
        const bool isOn = (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0);
        const bool isOff =
            (evt.type == midi::NoteOff ||
             (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0));
        if (isOn && evt.tick == startTick) {
            onIndices.push_back(i);
        } else if (isOff && evt.tick == endTick) {
            offIndices.push_back(i);
        }
    }

    while (onIndices.size() > 1 && offIndices.size() > 1) {
        const size_t onIdx = onIndices.back();
        onIndices.pop_back();
        const size_t offIdx = offIndices.back();
        offIndices.pop_back();
        const size_t hi = std::max(onIdx, offIdx);
        const size_t lo = std::min(onIdx, offIdx);
        midiEvents.erase(midiEvents.begin() + static_cast<std::ptrdiff_t>(hi));
        midiEvents.erase(midiEvents.begin() + static_cast<std::ptrdiff_t>(lo));
        logger.log(CAT_MIDI, LOG_DEBUG,
                   "Removed duplicate note pair: pitch=%d start=%lu end=%lu", pitch, startTick,
                   endTick);
    }
}
