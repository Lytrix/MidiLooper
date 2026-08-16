//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once
#include <vector>
#include <map>
#include <cstdint>
#include <memory>
#include "MidiEvent.h"
#include "MidiEvent.h"
#include "Utils/ExternalMemoryFirstAllocator.h"
#include <unordered_map>
#include <utility> // for std::pair

namespace NoteUtils {

/// Tail note-on paired with a head note-off after loop wrap (off tick < on tick).
inline bool isWrappedLoopNotePair(uint32_t noteOnTick, uint32_t noteOffTick, uint32_t loopLength) {
  if (loopLength == 0) {
    return false;
  }
  return noteOnTick < loopLength && noteOffTick < loopLength && noteOffTick < noteOnTick &&
         (noteOnTick - noteOffTick) > (loopLength / 2);
}

/// Head/tail window check aligned with LoopStopFinalize wrap window.
inline bool isHeadTailWrappedPair(uint32_t noteOnTick, uint32_t noteOffTick, uint32_t loopLength,
                                  uint32_t wrapWindowTicks = 768) {
  if (!isWrappedLoopNotePair(noteOnTick, noteOffTick, loopLength)) {
    return false;
  }
  const uint32_t window = wrapWindowTicks > loopLength ? loopLength : wrapWindowTicks;
  const uint32_t tailStart = loopLength > window ? loopLength - window : 0;
  return noteOnTick >= tailStart && noteOffTick < window;
}

inline uint32_t wrapTailStartTick(uint32_t loopLength, uint32_t wrapWindowTicks = 768) {
  const uint32_t window = wrapWindowTicks > loopLength ? loopLength : wrapWindowTicks;
  return loopLength > window ? loopLength - window : 0;
}

enum class WrapHeadSegmentContext {
  LivePlayhead,
  CommittedHeadOff,
};

struct WrapHeadSegment {
  bool visible = false;
  uint32_t startTick = 0;
  uint32_t endTickInclusive = 0;
};

/// True when live capture should split tail + head because playhead wrapped before tail-on.
/// Growing live-record lengths must not use this path: closeTick < tailOnTick there means the
/// playhead is still catching up to a frontier note-on, not a loop wrap (false head from tick 0).
inline bool isLiveWrapHeadContinuationDisplay(uint32_t tailOnTick, uint32_t closeTick,
                                              uint32_t loopLength, bool extendHeldNotesToPlayhead) {
  if (!extendHeldNotesToPlayhead || loopLength == 0 || closeTick >= tailOnTick) {
    return false;
  }
  const uint32_t tailStart = wrapTailStartTick(loopLength);
  // No distinct wrap-tail region (loop shorter than wrap window) — never infer wrap continuation.
  if (tailStart == 0) {
    return false;
  }
  return tailOnTick >= tailStart;
}

/// Single authority for wrap-held head segment visibility (display only).
/// For LivePlayhead, pass tailOnTick so head stays hidden while playhead is still in the tail.
WrapHeadSegment resolveWrapHeadSegment(uint32_t loopLength, uint32_t headEndInclusive,
                                       WrapHeadSegmentContext context,
                                       uint32_t wrapWindowTicks = 768,
                                       uint32_t tailOnTick = UINT32_MAX);

/// Inclusive wrap-head end → exclusive end for piano-roll pixel mapping (0 = do not draw head).
uint32_t wrapHeadExclusiveEndForDraw(uint32_t endTickInclusive, uint32_t loopLength);

/// True when no same-pitch note-on exists strictly between offTick and onTick.
bool wrapPairIsUnblocked(const MidiEventVec& midiEvents, uint32_t offTick, uint32_t onTick,
                         uint8_t pitch, uint8_t channel);

/// Head-off pairs with tailOn; no later same-pitch tail is a better match for that head-off.
bool isPreferredWrapTailForHeadOff(uint32_t tailOnTick, uint32_t headOffTick,
                                   const MidiEventVec& midiEvents, uint8_t pitch, uint8_t channel,
                                   uint32_t loopLength);
bool isPreferredWrapTailForHeadOff(uint32_t tailOnTick, uint32_t headOffTick,
                                   const SessionMidiEventVec& midiEvents, uint8_t pitch,
                                   uint8_t channel, uint32_t loopLength);

/****
 * @struct DisplayNote
 * @brief Simplified note representation for UI and overlap logic.
 *
 * Contains pitch, velocity, and start/end ticks for a MIDI note event.
 ****/
 struct DisplayNote {
     NoteId noteId = kInvalidNoteId;
     uint8_t note;
     uint8_t velocity;
     uint32_t startTick;
     uint32_t endTick;
 };

using DisplayNoteVec = std::vector<DisplayNote, ExternalMemoryFirstAllocator<DisplayNote>>;

/**
 * @class CachedNoteList
 * @brief Caches reconstructed notes to avoid expensive recalculation
 */
class CachedNoteList {
private:
    DisplayNoteVec cachedNotes;
    uint32_t lastMidiHash;
    uint32_t lastLoopLength;
    bool isValid;

public:
    CachedNoteList() : lastMidiHash(0), lastLoopLength(0), isValid(false) {}
    
    const DisplayNoteVec& getNotes(const MidiEventVec& midiEvents, uint32_t loopLength);
    const DisplayNoteVec& getNotes(const SessionMidiEventVec& midiEvents, uint32_t loopLength);
    void invalidate() { isValid = false; }
    
private:
    uint32_t computeMidiHash(const MidiEventVec& midiEvents);
    uint32_t computeMidiHash(const SessionMidiEventVec& midiEvents);
};

/**
 * @brief Reconstructs a list of DisplayNote from raw MIDI events using LIFO pairing.
 *
 * Matches NoteOn/NoteOff (or NoteOn with zero velocity) events per pitch in LIFO order,
 * wrapping any notes still active at loop end. Loop-wrap pairs split into tail (on→loop end)
 * and head (tick 0→off) display segments; playback still uses raw MIDI events only.
 *
 * @param midiEvents  The full list of MIDI events from a Track.
 * @param loopLength  The loop length in ticks.
 * @param verboseLog  When false, or when midiEvents is large, per-event DEBUG lines are omitted.
 * @return Vector of paired DisplayNote entries.
 */
std::vector<DisplayNote> reconstructNotes(const MidiEventVec& midiEvents, uint32_t loopLength,
                                          bool verboseLog = true);
std::vector<DisplayNote> reconstructNotes(const SessionMidiEventVec& midiEvents, uint32_t loopLength,
                                          bool verboseLog = true);

/// `finishOpenNotes` (default true) runs `finishCanonicalSpansOpenNotes` so unpaired
/// NoteOns become loop-end tails. Committed wrap/display passes false (6E.5).
/// `overdubPassWrapPairing` (default false) keeps a wrap-held pair when later
/// same-pitch body notes are already completed. Use only on one overdub pass's
/// events — never on record or merged record+overdub (015618).
DisplayNoteVec reconstructDisplayNotes(const MidiEventVec& midiEvents, uint32_t loopLength,
                                       bool verboseLog = true, bool finishOpenNotes = true,
                                       bool overdubPassWrapPairing = false);

template <typename Alloc>
DisplayNoteVec reconstructDisplayNotes(const std::vector<MidiEvent, Alloc>& midiEvents,
                                       uint32_t loopLength, bool verboseLog = true,
                                       bool finishOpenNotes = true,
                                       bool overdubPassWrapPairing = false);

/// Persistent stacks + spans for `buildCanonicalSpansFromMidi`. Not a new musical type.
struct CanonicalSpanBuild {
  CanonicalSpanBuild();
  ~CanonicalSpanBuild();
  CanonicalSpanBuild(CanonicalSpanBuild&&) noexcept;
  CanonicalSpanBuild& operator=(CanonicalSpanBuild&&) noexcept;
  CanonicalSpanBuild(const CanonicalSpanBuild&) = delete;
  CanonicalSpanBuild& operator=(const CanonicalSpanBuild&) = delete;
  void clear();
  uint32_t spanCount() const;

  struct Impl;
  std::unique_ptr<Impl> impl;
};

void appendCanonicalSpansFromMidi(const SessionMidiEventVec& midiEvents, uint32_t loopLength,
                                  uint32_t beginEvent, uint32_t endEventExclusive,
                                  CanonicalSpanBuild& build);
void finishCanonicalSpansFromMidi(uint32_t loopLength, CanonicalSpanBuild& build);
void appendProjectedDisplayNotes(const CanonicalSpanBuild& build, uint32_t loopLength,
                                 uint32_t beginSpan, uint32_t endSpanExclusive,
                                 DisplayNoteVec& out);
DisplayNoteVec dedupeProjectedDisplayNotes(const DisplayNoteVec& projected);
DisplayNoteVec displayNotesFromCanonicalSpans(const CanonicalSpanBuild& build, uint32_t loopLength);

struct OpenNoteOn {
    uint8_t note;
    uint8_t velocity;
    uint32_t tick;
    /// Index in the event vector passed to findOpenNoteOns. Distinguishes two
    /// NoteOns that share pitch and storage tick (same grid on a later wrap).
    size_t eventIndex = 0;
};

/**
 * @brief Returns note-ons within loopLength that have no matching note-off yet (LIFO per pitch).
 */
std::vector<OpenNoteOn> findOpenNoteOns(const MidiEventVec& midiEvents, uint32_t loopLength);

template <typename Alloc>
std::vector<OpenNoteOn> findOpenNoteOns(const std::vector<MidiEvent, Alloc>& midiEvents,
                                          uint32_t loopLength);

bool isWrapHeldOpenNote(const MidiEventVec& midiEvents, const OpenNoteOn& open, uint32_t loopLength);
bool isWrapHeldOpenNote(const SessionMidiEventVec& midiEvents, const OpenNoteOn& open,
                        uint32_t loopLength);

/**
 * @brief Fast lookup index for NoteOn/NoteOff events by (pitch<<32)|tick.
 * @param midiEvents The full list of MIDI events.
 * @returns A pair of maps: first is NoteOn index, second is NoteOff index.
 */
using Key = uint64_t;
using EventIndexMap = std::unordered_map<Key, size_t>;
using EventIndex = std::pair<EventIndexMap, EventIndexMap>;
EventIndex buildEventIndex(const MidiEventVec& midiEvents);
EventIndex buildEventIndex(const SessionMidiEventVec& midiEvents);

/** Remove extra note-on/note-off pairs with identical pitch, start, and end (after pitch merge). */
void removeDuplicateNotePairsAtSpan(MidiEventVec& midiEvents, uint8_t pitch, uint32_t startTick,
                                    uint32_t endTick);

/**
 * @brief At one tick, ensure note-offs for pitch are ordered before note-ons (LIFO pairing).
 *
 * Adjacent same-pitch notes that touch at a boundary (inner off == mover on) corrupt
 * reconstruction when the note-on appears before the note-off in the event vector.
 */
void ensureNoteOffsBeforeNoteOnsAtTick(MidiEventVec& midiEvents, uint8_t pitch, uint32_t tick);

/** True when two note spans overlap within loopLength (non-wrapped comparison). */
bool notesOverlap(uint32_t start1, uint32_t end1, uint32_t start2, uint32_t end2,
                  uint32_t loopLength);

/**
 * Same-tick same-pitch note-offs: order so LIFO pairing matches note-on order
 * (later note-on's off appears before earlier note-on's off at that tick).
 */
void orderSamePitchNoteOffsForLifo(MidiEventVec& midiEvents, uint8_t channel, uint8_t pitch);

/**
 * Sort by tick ascending; at equal ticks order note-offs before note-ons so reconstruction
 * and LIFO pairing stay stable after geometry edits.
 */
void sortMidiEventsChronologically(MidiEventVec& midiEvents);

} // namespace NoteUtils 