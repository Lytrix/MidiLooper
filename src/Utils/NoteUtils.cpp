//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/NoteUtils.h"
#include "Utils/MidiEventVecFnvHash.h"
#include "Logger.h"
#include <algorithm>
#include <set>
#include <tuple>

namespace {

constexpr size_t kReconstructVerboseMaxEvents = 32;

bool shouldLogReconstructDetails(bool verboseLog, size_t eventCount) {
    return verboseLog && eventCount <= kReconstructVerboseMaxEvents;
}

bool allLaterOnsInTailOrNone(const MidiEventVec& midiEvents, uint32_t headOffTick, uint8_t pitch,
                             uint8_t channel, uint32_t loopLength) {
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

bool tryPairWrappedTailOn(const MidiEventVec& midiEvents, uint32_t noteOffTick, uint8_t pitch,
                          uint8_t channel, uint32_t loopLength, uint32_t& outOnTick,
                          uint8_t& outVelocity) {
    if (!allLaterOnsInTailOrNone(midiEvents, noteOffTick, pitch, channel, loopLength)) {
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
        if (!NoteUtils::isPreferredWrapTailForHeadOff(onTick, noteOffTick, midiEvents, pitch,
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

bool hasDeferredLoopEndHeadOff(const MidiEventVec& midiEvents, size_t fromIndex, uint8_t pitch,
                               uint8_t channel, uint32_t tailOnTick, uint32_t loopLength) {
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
        if (NoteUtils::isPreferredWrapTailForHeadOff(tailOnTick, headOffTick, midiEvents, pitch,
                                                     channel, loopLength)) {
            return true;
        }
    }
    return false;
}

bool isLatestTailNoteOn(const MidiEventVec& midiEvents, uint32_t tailOnTick, uint8_t pitch,
                        uint8_t channel, uint32_t loopLength) {
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

bool shouldDeferLoopEndOff(const MidiEventVec& midiEvents, size_t fromIndex, uint8_t pitch,
                           uint8_t channel, uint32_t tailOnTick, uint32_t loopLength) {
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

bool isWrapHeldOpenNoteImpl(const MidiEventVec& midiEvents, const NoteUtils::OpenNoteOn& open,
                            uint32_t loopLength) {
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

    for (const MidiEvent& evt : midiEvents) {
        if (!evt.isNoteOff() || evt.channel != channel || evt.data.noteData.note != open.note) {
            continue;
        }
        uint32_t headOffTick = evt.tick;
        if (headOffTick >= loopLength) {
            headOffTick %= loopLength;
        }
        if (headOffTick >= loopLength - 1) {
            continue;
        }
        if (NoteUtils::isPreferredWrapTailForHeadOff(open.tick, headOffTick, midiEvents, open.note,
                                                     channel, loopLength)) {
            return true;
        }
    }

    return isLatestTailNoteOn(midiEvents, open.tick, open.note, channel, loopLength);
}

}  // namespace

bool NoteUtils::isWrapHeldOpenNote(const MidiEventVec& midiEvents, const OpenNoteOn& open,
                                   uint32_t loopLength) {
    return isWrapHeldOpenNoteImpl(midiEvents, open, loopLength);
}

bool NoteUtils::isPreferredWrapTailForHeadOff(uint32_t tailOnTick, uint32_t headOffTick,
                                              const MidiEventVec& midiEvents, uint8_t pitch,
                                              uint8_t channel, uint32_t loopLength) {
    if (!isHeadTailWrappedPair(tailOnTick, headOffTick, loopLength)) {
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
            isHeadTailWrappedPair(evt.tick, headOffTick, loopLength)) {
            return false;
        }
    }
    return true;
}

bool NoteUtils::wrapPairIsUnblocked(const MidiEventVec& midiEvents, uint32_t offTick, uint32_t onTick,
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

// CachedNoteList implementation
uint32_t NoteUtils::CachedNoteList::computeMidiHash(const MidiEventVec& midiEvents) {
    return midiEventVecFnv1aHash(midiEvents);
}

const std::vector<NoteUtils::DisplayNote>& NoteUtils::CachedNoteList::getNotes(const MidiEventVec& midiEvents, uint32_t loopLength) {
    uint32_t currentHash = computeMidiHash(midiEvents);
    
    if (isValid && currentHash == lastMidiHash && loopLength == lastLoopLength) {
        return cachedNotes; // Return cached result
    }
    
    // Cache miss - rebuild notes
    cachedNotes = reconstructNotes(midiEvents, loopLength);
    lastMidiHash = currentHash;
    lastLoopLength = loopLength;
    isValid = true;
    
    return cachedNotes;
}

std::vector<NoteUtils::DisplayNote> NoteUtils::reconstructNotes(const MidiEventVec& midiEvents, uint32_t loopLength,
                                                                bool verboseLog) {
    using DisplayNote = NoteUtils::DisplayNote;
    std::vector<DisplayNote> notes;
    std::map<uint8_t, std::vector<DisplayNote>> activeNoteStacks;

    if (loopLength == 0) {
        return notes;
    }

    const bool logDetails = shouldLogReconstructDetails(verboseLog, midiEvents.size());

    if (logDetails) {
        logger.log(CAT_TRACK, LOG_DEBUG, "Reconstructing notes with loop length: %lu ticks", loopLength);
    }

    std::set<std::pair<uint8_t, uint32_t>> wrappedTailOnTicks;

    const uint32_t tailStart = NoteUtils::wrapTailStartTick(loopLength);

    // Process ALL MIDI events to handle notes that extend beyond current loop
    for (size_t eventIndex = 0; eventIndex < midiEvents.size(); ++eventIndex) {
        const MidiEvent& evt = midiEvents[eventIndex];
        bool isNoteOn = (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0);
        bool isNoteOff = (evt.type == midi::NoteOff || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0));
        uint8_t pitch = evt.data.noteData.note;
        
        if (isNoteOn) {
            uint32_t noteOnTick = evt.tick;

            if (wrappedTailOnTicks.count({pitch, noteOnTick}) != 0) {
                continue;
            }
            
            // If note-on is beyond current loop boundary, only wrap it if it's from original loop extension
            // For loop shortening: discard notes that start beyond the new boundary
            if (noteOnTick >= loopLength) {
                if (logDetails) {
                    logger.log(CAT_TRACK, LOG_DEBUG,
                               "Discarding note-on beyond loop boundary: pitch=%d, tick=%lu, loop=%lu",
                               pitch, noteOnTick, loopLength);
                }
                continue;
            }
            
            if (logDetails) {
                logger.log(CAT_TRACK, LOG_DEBUG, "Note-on: pitch=%d, tick=%lu", pitch, noteOnTick);
            }
            
            DisplayNote note;
            note.note = pitch;
            note.startTick = noteOnTick;
            note.endTick = noteOnTick; // Will be updated when note-off is found
            note.velocity = evt.data.noteData.velocity;
            
            activeNoteStacks[pitch].push_back(note);
            
        } else if (isNoteOff) {
            uint32_t noteOffTick = evt.tick;
            const bool offWasBeyondLoop = noteOffTick >= loopLength;
            
            // Handle note-off that might be beyond current loop boundary
            if (offWasBeyondLoop) {
                // Wrap the note-off position for notes that extend beyond loop
                noteOffTick = noteOffTick % loopLength;
                if (logDetails) {
                    logger.log(CAT_TRACK, LOG_DEBUG,
                               "Wrapped note-off: pitch=%d, original_tick=%lu -> wrapped_tick=%lu",
                               pitch, evt.tick, noteOffTick);
                }
            }
            
            if (logDetails) {
                logger.log(CAT_TRACK, LOG_DEBUG, "Note-off: pitch=%d, tick=%lu", pitch, noteOffTick);
            }
            
            if (activeNoteStacks[pitch].empty()) {
                uint32_t wrappedOnTick = 0;
                uint8_t wrappedVelocity = 0;
                if (tryPairWrappedTailOn(midiEvents, noteOffTick, pitch, evt.channel, loopLength,
                                         wrappedOnTick, wrappedVelocity)) {
                    DisplayNote tailSeg;
                    tailSeg.note = pitch;
                    tailSeg.startTick = wrappedOnTick;
                    tailSeg.endTick = loopLength - 1;
                    tailSeg.velocity = wrappedVelocity;
                    notes.push_back(tailSeg);

                    if (noteOffTick > 0) {
                        DisplayNote headSeg;
                        headSeg.note = pitch;
                        headSeg.startTick = 0;
                        headSeg.endTick = noteOffTick;
                        headSeg.velocity = wrappedVelocity;
                        notes.push_back(headSeg);
                    }

                    wrappedTailOnTicks.insert({pitch, wrappedOnTick});
                    if (logDetails) {
                        logger.log(CAT_TRACK, LOG_DEBUG,
                                   "Wrapped pair split: pitch=%d, tail=%lu-%lu, head=0-%lu",
                                   pitch, wrappedOnTick, loopLength - 1, noteOffTick);
                    }
                    continue;
                }
                if (logDetails) {
                    logger.log(CAT_TRACK, LOG_DEBUG, "Note-off without matching note-on: pitch=%d", pitch);
                }
                continue;
            }
            
            // Complete the most recent note-on for this pitch
            DisplayNote& note = activeNoteStacks[pitch].back();
            if (!offWasBeyondLoop && noteOffTick == loopLength - 1 &&
                note.startTick >= tailStart &&
                shouldDeferLoopEndOff(midiEvents, eventIndex, pitch, evt.channel, note.startTick,
                                      loopLength)) {
                if (logDetails) {
                    logger.log(CAT_TRACK, LOG_DEBUG,
                               "Defer loop-end off for wrap hold: pitch=%d, tail=%lu",
                               pitch, note.startTick);
                }
                continue;
            }

            if (!offWasBeyondLoop && noteOffTick < note.startTick &&
                NoteUtils::isPreferredWrapTailForHeadOff(note.startTick, noteOffTick, midiEvents, pitch,
                                                         evt.channel, loopLength) &&
                note.startTick >= tailStart) {
                DisplayNote tailSeg = note;
                tailSeg.endTick = loopLength - 1;
                notes.push_back(tailSeg);

                if (noteOffTick > 0) {
                    DisplayNote headSeg = note;
                    headSeg.startTick = 0;
                    headSeg.endTick = noteOffTick;
                    notes.push_back(headSeg);
                }

                if (logDetails) {
                    logger.log(CAT_TRACK, LOG_DEBUG,
                               "Wrapped stack split: pitch=%d, tail=%lu-%lu, head=0-%lu",
                               pitch, note.startTick, loopLength - 1, noteOffTick);
                }

                activeNoteStacks[pitch].pop_back();
                continue;
            }

            note.endTick = noteOffTick;
            
            if (logDetails) {
                logger.log(CAT_TRACK, LOG_DEBUG, "Final note: pitch=%d, start=%lu, end=%lu",
                           note.note, note.startTick, note.endTick);
            }
            
            notes.push_back(note);
            activeNoteStacks[pitch].pop_back();
        }
    }

    // Handle any remaining active notes (notes without explicit note-offs)
    for (auto& [pitch, noteStack] : activeNoteStacks) {
        for (const auto& note : noteStack) {
            DisplayNote completedNote = note;
            completedNote.endTick = loopLength - 1; // End at loop boundary
            
            if (logDetails) {
                logger.log(CAT_TRACK, LOG_DEBUG, "Active note at loop end: pitch=%d, start=%lu, end=%lu",
                           completedNote.note, completedNote.startTick, completedNote.endTick);
            }
            
            notes.push_back(completedNote);
        }
        (void)pitch;
    }
    
    // Deduplicate notes with same pitch, start, and end
    std::set<std::tuple<uint8_t, uint32_t, uint32_t>> seenNotes;
    std::vector<DisplayNote> finalNotes;
    
    size_t originalCount = notes.size();
    for (const auto& note : notes) {
        auto key = std::make_tuple(note.note, note.startTick, note.endTick);
        if (seenNotes.find(key) == seenNotes.end()) {
            seenNotes.insert(key);
            finalNotes.push_back(note);
        } else if (logDetails) {
            logger.log(CAT_TRACK, LOG_DEBUG, "Deduplicated note: pitch=%d, start=%lu, end=%lu",
                       note.note, note.startTick, note.endTick);
        }
    }

    if (logDetails) {
        logger.log(CAT_TRACK, LOG_DEBUG, "Reconstruction complete: %zu notes total (%zu duplicates removed)",
                   finalNotes.size(), originalCount - finalNotes.size());
    }
    
    return finalNotes;
}

std::vector<NoteUtils::OpenNoteOn> NoteUtils::findOpenNoteOns(const MidiEventVec& midiEvents,
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
            activeStacks[pitch].push_back({pitch, evt.data.noteData.velocity, evt.tick});
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

// Build a fast lookup index for NoteOn/NoteOff events
NoteUtils::EventIndex NoteUtils::buildEventIndex(const MidiEventVec& midiEvents) {
    using Key = NoteUtils::Key;
    EventIndexMap onIndex;
    EventIndexMap offIndex;
    onIndex.reserve(midiEvents.size());
    offIndex.reserve(midiEvents.size());
    for (size_t i = 0; i < midiEvents.size(); ++i) {
        const auto& evt = midiEvents[i];
        bool isOn = (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0);
        bool isOff = (evt.type == midi::NoteOff || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0));
        if (isOn || isOff) {
            Key key = ((Key)evt.data.noteData.note << 32) | evt.tick;
            if (isOn) onIndex[key] = i;
            else offIndex[key] = i;
        }
    }
    return {std::move(onIndex), std::move(offIndex)};
}

void NoteUtils::ensureNoteOffsBeforeNoteOnsAtTick(MidiEventVec& midiEvents, uint8_t pitch,
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

void NoteUtils::removeDuplicateNotePairsAtSpan(MidiEventVec& midiEvents, uint8_t pitch,
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
