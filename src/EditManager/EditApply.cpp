//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditApply.h"

#include "Utils/NoteEditMem.h"
#include "Utils/NoteUtils.h"

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
#include "Utils/DebugSessionCapture.h"
#include "Utils/InvestigationAllocProbe.h"
#include <Arduino.h>
#endif

// Keep diagnostic format strings out of DTCM .rodata — see RT_TAG in RuntimeTimingTelemetry.
#if defined(SESSION_CAPTURE) && defined(__IMXRT1062__)
#define EDIT_DIAG_FMT(literal) PSTR(literal)
#else
#define EDIT_DIAG_FMT(literal) literal
#endif

namespace {

#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
// Which branch of applyChangeLengthById runs. Only the lengthen branch calls
// NoteUtils::reconstructNotes over the whole event vector; the shorten branch returns early.
uint32_t g_lengthenPathCount = 0;
uint32_t g_shortenPathCount = 0;
uint32_t g_noOpPathCount = 0;
uint32_t g_reconstructUs = 0;
uint32_t g_reconInternalHits = 0;
uint32_t g_reconSpillHits = 0;
uint32_t g_reconSpillBytes = 0;
uint32_t g_reconClockPulses = 0;
uint32_t g_reconInternalClockDispatches = 0;
uint32_t g_workingSetExternal = 0;

void resetLengthPathCounters() {
  g_lengthenPathCount = 0;
  g_shortenPathCount = 0;
  g_noOpPathCount = 0;
  g_reconstructUs = 0;
  g_reconInternalHits = 0;
  g_reconSpillHits = 0;
  g_reconSpillBytes = 0;
  g_reconClockPulses = 0;
  g_reconInternalClockDispatches = 0;
}
#endif

struct NoteOnIndexLookupCache {
  std::vector<std::pair<NoteId, int>> entries;
};

void clearNoteOnIndexLookupCache(NoteOnIndexLookupCache* cache) {
  if (cache != nullptr) {
    cache->entries.clear();
  }
}

bool lookupNoteOnIndexFromCache(const MidiEventVec& events, NoteId noteId,
                                NoteOnIndexLookupCache* cache, int& outIndex) {
  if (cache == nullptr || noteId == kInvalidNoteId || cache->entries.empty()) {
    return false;
  }
  auto it = std::lower_bound(
      cache->entries.begin(), cache->entries.end(), noteId,
      [](const std::pair<NoteId, int>& entry, NoteId id) { return entry.first < id; });
  if (it == cache->entries.end() || it->first != noteId) {
    return false;
  }
  const int cachedIndex = it->second;
  if (cachedIndex >= 0 && static_cast<size_t>(cachedIndex) < events.size() &&
      events[static_cast<size_t>(cachedIndex)].isNoteOn() &&
      events[static_cast<size_t>(cachedIndex)].noteId == noteId) {
    outIndex = cachedIndex;
    return true;
  }
  cache->entries.erase(it);
  return false;
}

void storeNoteOnIndexInCache(NoteId noteId, int index, NoteOnIndexLookupCache* cache) {
  if (cache == nullptr || noteId == kInvalidNoteId || index < 0) {
    return;
  }
  auto it = std::lower_bound(
      cache->entries.begin(), cache->entries.end(), noteId,
      [](const std::pair<NoteId, int>& entry, NoteId id) { return entry.first < id; });
  if (it != cache->entries.end() && it->first == noteId) {
    it->second = index;
    return;
  }
  cache->entries.insert(it, std::make_pair(noteId, index));
}

int findNoteOnByIdLinear(const MidiEventVec& events, NoteId noteId) {
  if (noteId == kInvalidNoteId) {
    return -1;
  }
  for (size_t i = 0; i < events.size(); ++i) {
    if (events[i].isNoteOn() && events[i].noteId == noteId) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int findNoteOnByIdCached(const MidiEventVec& events, NoteId noteId,
                         NoteOnIndexLookupCache* cache) {
  int cachedIndex = -1;
  if (lookupNoteOnIndexFromCache(events, noteId, cache, cachedIndex)) {
    return cachedIndex;
  }
  const int resolvedIndex = findNoteOnByIdLinear(events, noteId);
  if (resolvedIndex >= 0) {
    storeNoteOnIndexInCache(noteId, resolvedIndex, cache);
  }
  return resolvedIndex;
}

uint32_t inferLoopLength(const MidiEventVec& events, uint32_t hint) {
  if (hint > 0) {
    return hint;
  }
  uint32_t maxTick = 0;
  for (const MidiEvent& evt : events) {
    maxTick = std::max(maxTick, evt.tick);
  }
  return std::max<uint32_t>(maxTick + 1u, 768u);
}

NOTE_EDIT_MEM int findNoteOffForOnIndex(const MidiEventVec& events, int onIndex) {
  if (onIndex < 0 || static_cast<size_t>(onIndex) >= events.size()) {
    return -1;
  }
  const MidiEvent& onEvt = events[static_cast<size_t>(onIndex)];
  const uint8_t channel = onEvt.channel;
  const uint8_t note = onEvt.data.noteData.note;

  // Same-pitch pairing is LIFO everywhere else (findLinearOffForNoteOnLifo,
  // findCorrespondingNoteOff, pairedNoteOnTickForOffAtIndex). Walk vector order,
  // except at an equal tick process Off before On (same keys as
  // sortMidiEventsChronologically / reconstruct / playback clock). Do not sort
  // by tick across the whole stream: capture stores sequential pairs
  // (On, Off, On, Off); a full tick sort would nest them (On, On, Off, Off) and
  // steal the wrong Off (pre-commit Length of the later note).
  // Tick-only gather is On then Off at a shared tick; vector-order LIFO then
  // gives the Off to the later On (121141 Off@168 → 6073).
  // Do not mutate the vector: applyChangeLengthById lengthen ends with tick-only
  // stable_sort and would restore On-before-Off for a later row.
  // Tick-sorted wrap capture (195941: Off@96 then On@2592) hits the off while the stack is
  // empty, so LIFO never pairs it. After the linear walk, pair a still-open on to its wrap
  // off (tick < on.tick) — tagged NoteId first, else the unique unpaired wrap off.
  // What costs here is the number of full passes over the event vector, not allocation size:
  // MidiEventVec is InternalHeapFirstAllocator and spills to external memory once the internal
  // heap is gone, so at boot sizes each pass is expensive. One pass collects the walked
  // channel + pitch; the wrap-off scan below then reuses that list instead of walking every event
  // a second time. Do not add a counting pass to size this exactly — an exact reserve costs more
  // than the geometric growth it saves (session_20260821_114927: +15 % per delete).
  std::vector<size_t> pitchIndices;
  for (size_t i = 0; i < events.size(); ++i) {
    const MidiEvent& evt = events[i];
    if (evt.channel == channel && evt.data.noteData.note == note &&
        (evt.isNoteOn() || evt.isNoteOff())) {
      pitchIndices.push_back(i);
    }
  }

  std::vector<size_t> walkOrder(pitchIndices);
  std::stable_sort(walkOrder.begin(), walkOrder.end(), [&](size_t a, size_t b) {
    const MidiEvent& ea = events[a];
    const MidiEvent& eb = events[b];
    if (ea.tick == eb.tick) {
      return NoteUtils::midiEventChronologicalLess(ea, eb);
    }
    return a < b;
  });

  std::vector<size_t> openNoteOnIndices;
  // Indexed by position in pitchIndices, which is ascending, so lower_bound maps an event index.
  std::vector<uint8_t> offPaired(pitchIndices.size(), 0);
  for (const size_t i : walkOrder) {
    const MidiEvent& evt = events[i];
    if (evt.channel != channel || evt.data.noteData.note != note) {
      continue;
    }
    if (evt.isNoteOn()) {
      openNoteOnIndices.push_back(i);
      continue;
    }
    if (!evt.isNoteOff() || openNoteOnIndices.empty()) {
      continue;
    }
    const size_t pairedOnIndex = openNoteOnIndices.back();
    openNoteOnIndices.pop_back();
    const auto pairedIt = std::lower_bound(pitchIndices.begin(), pitchIndices.end(), i);
    if (pairedIt != pitchIndices.end() && *pairedIt == i) {
      offPaired[static_cast<size_t>(pairedIt - pitchIndices.begin())] = 1;
    }
    if (pairedOnIndex == static_cast<size_t>(onIndex)) {
      return static_cast<int>(i);
    }
  }

  bool targetStillOpen = false;
  for (const size_t openIndex : openNoteOnIndices) {
    if (openIndex == static_cast<size_t>(onIndex)) {
      targetStillOpen = true;
      break;
    }
  }
  if (!targetStillOpen) {
    return -1;
  }

  int taggedWrapOff = -1;
  int untaggedWrapOff = -1;
  uint32_t untaggedWrapOffCount = 0;
  // pitchIndices is ascending and holds exactly this channel + pitch, so walking it is the same
  // traversal as scanning every event and skipping the non-matching ones.
  for (size_t p = 0; p < pitchIndices.size(); ++p) {
    if (offPaired[p] != 0) {
      continue;
    }
    const size_t i = pitchIndices[p];
    const MidiEvent& evt = events[i];
    if (!evt.isNoteOff()) {
      continue;
    }
    if (evt.tick >= onEvt.tick) {
      continue;
    }
    if (onEvt.noteId != kInvalidNoteId && evt.noteId == onEvt.noteId) {
      taggedWrapOff = static_cast<int>(i);
      break;
    }
    if (evt.noteId == kInvalidNoteId) {
      untaggedWrapOff = static_cast<int>(i);
      ++untaggedWrapOffCount;
    }
  }
  if (taggedWrapOff >= 0) {
    return taggedWrapOff;
  }
  if (untaggedWrapOffCount == 1) {
    return untaggedWrapOff;
  }
  return -1;
}

int findWrapHeadOffForOnIndex(const MidiEventVec& events, int onIndex, uint32_t loopLength) {
  if (onIndex < 0 || static_cast<size_t>(onIndex) >= events.size() || loopLength == 0) {
    return -1;
  }
  const MidiEvent& onEvt = events[static_cast<size_t>(onIndex)];
  const uint8_t channel = onEvt.channel;
  const uint8_t note = onEvt.data.noteData.note;
  int tagged = -1;
  int preferred = -1;
  uint32_t preferredCount = 0;
  for (size_t i = 0; i < events.size(); ++i) {
    const MidiEvent& evt = events[i];
    if (!evt.isNoteOff() || evt.channel != channel || evt.data.noteData.note != note) {
      continue;
    }
    uint32_t headOffTick = evt.tick;
    if (headOffTick >= loopLength) {
      headOffTick %= loopLength;
    }
    if (!NoteUtils::isPreferredWrapTailForHeadOff(onEvt.tick, headOffTick, events, note, channel,
                                                  loopLength)) {
      continue;
    }
    if (onEvt.noteId != kInvalidNoteId && evt.noteId == onEvt.noteId) {
      tagged = static_cast<int>(i);
      break;
    }
    preferred = static_cast<int>(i);
    ++preferredCount;
  }
  if (tagged >= 0) {
    return tagged;
  }
  if (preferredCount == 1) {
    return preferred;
  }
  return -1;
}

void eraseEventIndices(MidiEventVec& events, std::vector<int> indices) {
  std::sort(indices.begin(), indices.end(), std::greater<int>());
  int last = -1;
  for (int idx : indices) {
    if (idx < 0 || idx == last || static_cast<size_t>(idx) >= events.size()) {
      continue;
    }
    events.erase(events.begin() + idx);
    last = idx;
  }
}

void applyDeleteNoteById(MidiEventVec& events, NoteId noteId, uint32_t loopLength,
                         NoteOnIndexLookupCache* cache) {
  const int onIndex = findNoteOnByIdCached(events, noteId, cache);
  if (onIndex < 0) {
    return;
  }
  const int lifoOff = findNoteOffForOnIndex(events, onIndex);
  const int wrapOff = findWrapHeadOffForOnIndex(events, onIndex, inferLoopLength(events, loopLength));
  std::vector<int> remove;
  remove.push_back(onIndex);
  if (lifoOff >= 0) {
    remove.push_back(lifoOff);
  }
  if (wrapOff >= 0) {
    remove.push_back(wrapOff);
  }
  eraseEventIndices(events, remove);
  clearNoteOnIndexLookupCache(cache);
}

void applyMoveNoteById(MidiEventVec& events, NoteId noteId, uint32_t newStart, uint32_t newEnd,
                       uint32_t loopLength, NoteOnIndexLookupCache* cache) {
  const int onIndex = findNoteOnByIdCached(events, noteId, cache);
  if (onIndex < 0) {
    return;
  }
  loopLength = inferLoopLength(events, loopLength);
  const int lifoOff = findNoteOffForOnIndex(events, onIndex);
  const int wrapOff = findWrapHeadOffForOnIndex(events, onIndex, loopLength);
  events[static_cast<size_t>(onIndex)].tick = newStart;
  const int offToMove = wrapOff >= 0 ? wrapOff : lifoOff;
  if (offToMove >= 0) {
    events[static_cast<size_t>(offToMove)].tick = newEnd;
  } else {
    const MidiEvent& onEvt = events[static_cast<size_t>(onIndex)];
    events.push_back(MidiEvent::NoteOff(newEnd, onEvt.channel, onEvt.data.noteData.note, 0));
  }
  if (wrapOff >= 0 && lifoOff >= 0 && lifoOff != wrapOff) {
    eraseEventIndices(events, {lifoOff});
  }
  clearNoteOnIndexLookupCache(cache);
}

void applyChangePitchById(MidiEventVec& events, NoteId noteId, uint8_t newPitch,
                          uint32_t loopLength, NoteOnIndexLookupCache* cache) {
  const int onIndex = findNoteOnByIdCached(events, noteId, cache);
  if (onIndex < 0) {
    return;
  }
  const int lifoOff = findNoteOffForOnIndex(events, onIndex);
  const int wrapOff = findWrapHeadOffForOnIndex(events, onIndex, inferLoopLength(events, loopLength));
  events[static_cast<size_t>(onIndex)].data.noteData.note = newPitch;
  if (lifoOff >= 0) {
    events[static_cast<size_t>(lifoOff)].data.noteData.note = newPitch;
  }
  if (wrapOff >= 0) {
    events[static_cast<size_t>(wrapOff)].data.noteData.note = newPitch;
  }
}

void applyChangeVelocityById(MidiEventVec& events, NoteId noteId, uint8_t newVelocity,
                             NoteOnIndexLookupCache* cache) {
  const int onIndex = findNoteOnByIdCached(events, noteId, cache);
  if (onIndex >= 0) {
    events[static_cast<size_t>(onIndex)].data.noteData.velocity = newVelocity;
  }
}

void shortenNoteEndById(MidiEventVec& events, NoteId noteId, uint32_t newEndTick,
                        NoteOnIndexLookupCache* cache) {
  const int onIndex = findNoteOnByIdCached(events, noteId, cache);
  const int offIndex = findNoteOffForOnIndex(events, onIndex);
  if (offIndex >= 0) {
    events[static_cast<size_t>(offIndex)].tick = newEndTick;
  }
}

// The lengthen branch only inspects reconstructed notes that share the edited note's pitch, and
// reconstruction is pitch-local end to end: pairing keys on activeNoteStacks[pitch], projection is
// span-local, and dedupe keys on (note, startTick, endTick). Reconstructing every pitch is
// therefore discarded work, and it is superlinear — an unpaired note-off sends
// tryPairWrappedTailOn back over the whole event vector — so a full reconstruct cost ~576 ms per
// call at 2513 events (session_20260821_112151: 70 calls, 40.3 s of a 51.1 s replay).
NOTE_EDIT_MEM std::vector<NoteUtils::DisplayNote> reconstructNotesForPitch(
    const MidiEventVec& events, uint8_t pitch, uint32_t loopLength) {
  MidiEventVec pitchEvents;
  pitchEvents.reserve(events.size());
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
  if (isInExternalMemoryPool(pitchEvents.data())) {
    ++g_reconSpillHits;
    g_reconSpillBytes += static_cast<uint32_t>(events.size() * sizeof(MidiEvent));
  } else {
    ++g_reconInternalHits;
  }
#endif
  for (const MidiEvent& evt : events) {
    if ((evt.isNoteOn() || evt.isNoteOff()) && evt.data.noteData.note == pitch) {
      pitchEvents.push_back(evt);
    }
  }
  return NoteUtils::reconstructNotes(pitchEvents, loopLength, false);
}

void applyChangeLengthById(MidiEventVec& events, NoteId noteId, uint32_t newEnd,
                           uint32_t loopLength, NoteOnIndexLookupCache* cache) {
  const int onIndex = findNoteOnByIdCached(events, noteId, cache);
  if (onIndex < 0) {
    return;
  }
  const MidiEvent& onEvt = events[static_cast<size_t>(onIndex)];
  const uint32_t refStart = onEvt.tick;
  const int offIndex = findNoteOffForOnIndex(events, onIndex);
  loopLength = inferLoopLength(events, loopLength);
  const uint32_t refEnd =
      offIndex >= 0 ? events[static_cast<size_t>(offIndex)].tick : refStart;
  if (offIndex < 0 && newEnd == loopLength) {
    return;
  }

  if (newEnd == refEnd) {
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
    ++g_noOpPathCount;
#endif
    return;
  }

  if (newEnd < refEnd) {
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
    ++g_shortenPathCount;
#endif
    shortenNoteEndById(events, noteId, newEnd, cache);
    return;
  }

#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
  ++g_lengthenPathCount;
  const uint32_t reconstructStartUs = micros();
  const uint32_t clockPulsesStart = InvestigationAllocProbe::clockPulseEntries();
  const uint32_t internalClockStart = InvestigationAllocProbe::internalClockDispatches();
#endif
  const uint32_t newStart = refStart;
  const uint32_t displayNewEnd = (newEnd > loopLength) ? (newEnd % loopLength) : newEnd;

  const std::vector<NoteUtils::DisplayNote> allNotes =
      reconstructNotesForPitch(events, onEvt.data.noteData.note, loopLength);
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
  g_reconstructUs += micros() - reconstructStartUs;
  g_reconClockPulses += InvestigationAllocProbe::clockPulseEntries() - clockPulsesStart;
  g_reconInternalClockDispatches +=
      InvestigationAllocProbe::internalClockDispatches() - internalClockStart;
#endif
  std::vector<NoteUtils::DisplayNote> notesToDelete;
  std::vector<std::pair<NoteUtils::DisplayNote, uint32_t>> notesToShorten;

  for (const NoteUtils::DisplayNote& note : allNotes) {
    if (note.noteId == noteId) {
      continue;
    }
    if (note.note != onEvt.data.noteData.note) {
      continue;
    }
    if (!NoteUtils::notesOverlap(newStart, displayNewEnd, note.startTick, note.endTick,
                                 loopLength)) {
      continue;
    }

    bool noteCompletelyContained = false;
    if (displayNewEnd >= newStart) {
      noteCompletelyContained =
          (note.startTick >= newStart && note.endTick <= displayNewEnd);
    } else {
      noteCompletelyContained =
          (note.startTick >= newStart || note.endTick <= displayNewEnd);
    }

    if (noteCompletelyContained) {
      if (note.startTick > newStart && note.endTick == displayNewEnd) {
        continue;
      }
      notesToDelete.push_back(note);
      continue;
    }

    if (note.startTick >= newStart && note.endTick > displayNewEnd) {
      continue;
    }

    if (note.startTick == displayNewEnd && note.endTick > displayNewEnd) {
      notesToDelete.push_back(note);
      continue;
    }

    if (note.startTick < newStart) {
      const uint32_t shortenedEnd =
          (newStart == 0) ? (loopLength - 1) : (newStart - 1);
      notesToShorten.push_back({note, shortenedEnd});
    } else {
      notesToDelete.push_back(note);
    }
  }

  for (const auto& [note, shortenedEnd] : notesToShorten) {
    shortenNoteEndById(events, note.noteId, shortenedEnd, cache);
  }
  for (const NoteUtils::DisplayNote& note : notesToDelete) {
    applyDeleteNoteById(events, note.noteId, loopLength, cache);
  }

  const int refreshedOnIndex = findNoteOnByIdCached(events, noteId, cache);
  const int refreshedOffIndex = findNoteOffForOnIndex(events, refreshedOnIndex);
  if (refreshedOffIndex >= 0) {
    events[static_cast<size_t>(refreshedOffIndex)].tick = newEnd;
  } else if (refreshedOnIndex >= 0) {
    const MidiEvent& refreshedOn = events[static_cast<size_t>(refreshedOnIndex)];
    events.push_back(
        MidiEvent::NoteOff(newEnd, refreshedOn.channel, refreshedOn.data.noteData.note, 0));
  }

  NoteUtils::orderSamePitchNoteOffsForLifo(events, onEvt.channel, onEvt.data.noteData.note);
  std::stable_sort(events.begin(), events.end(),
                   [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
  clearNoteOnIndexLookupCache(cache);
}

void applyAddNote(MidiEventVec& events, const MidiEventVec& addedEvents,
                  NoteOnIndexLookupCache* cache) {
  for (const MidiEvent& evt : addedEvents) {
    events.push_back(evt);
  }
  std::sort(events.begin(), events.end(),
            [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
  clearNoteOnIndexLookupCache(cache);
}

void collectPresentNoteIds(const MidiEventVec& events, std::vector<NoteId>& noteIds) {
  noteIds.clear();
  noteIds.reserve(events.size());
  for (const MidiEvent& evt : events) {
    if (!evt.isNoteOn() || evt.noteId == kInvalidNoteId) {
      continue;
    }
    noteIds.push_back(evt.noteId);
  }
  std::sort(noteIds.begin(), noteIds.end());
  noteIds.erase(std::unique(noteIds.begin(), noteIds.end()), noteIds.end());
}

bool containsPresentNoteId(const std::vector<NoteId>& noteIds, NoteId noteId) {
  if (noteId == kInvalidNoteId || noteIds.empty()) {
    return false;
  }
  return std::binary_search(noteIds.begin(), noteIds.end(), noteId);
}

void insertPresentNoteId(std::vector<NoteId>& noteIds, NoteId noteId) {
  if (noteId == kInvalidNoteId) {
    return;
  }
  const auto it = std::lower_bound(noteIds.begin(), noteIds.end(), noteId);
  if (it == noteIds.end() || *it != noteId) {
    noteIds.insert(it, noteId);
  }
}

void erasePresentNoteId(std::vector<NoteId>& noteIds, NoteId noteId) {
  if (noteId == kInvalidNoteId || noteIds.empty()) {
    return;
  }
  const auto it = std::lower_bound(noteIds.begin(), noteIds.end(), noteId);
  if (it != noteIds.end() && *it == noteId) {
    noteIds.erase(it);
  }
}

bool isContiguousCollapsibleUpdate(const EditPass& row) {
  if (row.actionType != EditActionType::Update || row.targetNoteId == kInvalidNoteId) {
    return false;
  }
  return row.propertyType == EditPropertyType::Velocity ||
         row.propertyType == EditPropertyType::Pitch;
}

NOTE_EDIT_MEM void planContiguousCollapsedRows(const EditPassVec& rows,
                                               std::vector<const EditPass*>& plannedRows,
                                               uint32_t& rowsCollapsedOut) {
  plannedRows.clear();
  plannedRows.reserve(rows.size());
  rowsCollapsedOut = 0;
  size_t i = 0;
  while (i < rows.size()) {
    const EditPass& row = rows[i];
    if (!isContiguousCollapsibleUpdate(row)) {
      plannedRows.push_back(&row);
      ++i;
      continue;
    }

    size_t last = i;
    size_t j = i + 1;
    while (j < rows.size()) {
      const EditPass& candidate = rows[j];
      if (!isContiguousCollapsibleUpdate(candidate)) {
        break;
      }
      if (candidate.targetNoteId != row.targetNoteId ||
          candidate.propertyType != row.propertyType) {
        break;
      }
      last = j;
      ++j;
    }

    plannedRows.push_back(&rows[last]);
    rowsCollapsedOut += static_cast<uint32_t>(last - i);
    i = j;
  }
}

}  // namespace

int findNoteOnById(const MidiEventVec& events, NoteId noteId) {
  return findNoteOnByIdLinear(events, noteId);
}

PersistIdentityResolve resolvePersistIdentityForExistingNote(
    const MidiEventVec& rematerializeEvents, uint8_t commitBaselinePitch,
    uint32_t commitBaselineStartTick) {
  PersistIdentityResolve result;
  NoteId uniqueId = kInvalidNoteId;
  for (const MidiEvent& evt : rematerializeEvents) {
    if (!evt.isNoteOn() || evt.data.noteData.note != commitBaselinePitch ||
        evt.tick != commitBaselineStartTick) {
      continue;
    }
    ++result.matchCount;
    uniqueId = evt.noteId;
  }
  if (result.matchCount == 1 && uniqueId != kInvalidNoteId) {
    result.status = PersistIdentityResolveStatus::Unique;
    result.noteId = uniqueId;
    return result;
  }
  if (result.matchCount > 1) {
    result.status = PersistIdentityResolveStatus::Ambiguous;
    return result;
  }
  result.status = PersistIdentityResolveStatus::Unresolved;
  return result;
}

static void retargetMoverEditPassRows(EditPassVec& rows, NoteId sessionNoteId, NoteId persistNoteId) {
  if (sessionNoteId == kInvalidNoteId || persistNoteId == kInvalidNoteId ||
      sessionNoteId == persistNoteId) {
    return;
  }
  for (EditPass& row : rows) {
    if (row.actionType != EditActionType::Update || row.targetNoteId != sessionNoteId) {
      continue;
    }
    switch (row.propertyType) {
      case EditPropertyType::NoteRange:
      case EditPropertyType::Pitch:
      case EditPropertyType::Length:
      case EditPropertyType::Velocity:
        row.targetNoteId = persistNoteId;
        break;
      default:
        break;
    }
  }
}

PersistIdentityReconcileResult reconcileMoverPersistIdentity(
    const MidiEventVec& rematerializeEvents, NoteId sessionNoteId, uint8_t commitBaselinePitch,
    uint32_t commitBaselineStartTick, EditPassVec& rows) {
  PersistIdentityReconcileResult result;
  if (sessionNoteId != kInvalidNoteId && findNoteOnById(rematerializeEvents, sessionNoteId) >= 0) {
    result.status = PersistIdentityReconcileStatus::AlreadyPresent;
    result.persistNoteId = sessionNoteId;
    return result;
  }
  const PersistIdentityResolve resolved = resolvePersistIdentityForExistingNote(
      rematerializeEvents, commitBaselinePitch, commitBaselineStartTick);
  result.matchCount = resolved.matchCount;
  if (resolved.status == PersistIdentityResolveStatus::Unique) {
    result.status = PersistIdentityReconcileStatus::Unique;
    result.persistNoteId = resolved.noteId;
    retargetMoverEditPassRows(rows, sessionNoteId, resolved.noteId);
    return result;
  }
  if (resolved.status == PersistIdentityResolveStatus::Ambiguous) {
    result.status = PersistIdentityReconcileStatus::Ambiguous;
    return result;
  }
  result.status = PersistIdentityReconcileStatus::Unresolved;
  return result;
}

void deleteNoteById(MidiEventVec& events, NoteId noteId) {
  applyDeleteNoteById(events, noteId, 0, nullptr);
}

void applyNoteEditPassWithLookupCache(MidiEventVec& events, const EditPass& editPass,
                                      uint32_t loopLengthTicks,
                                      NoteOnIndexLookupCache* lookupCache) {
  switch (editPass.actionType) {
    case EditActionType::Delete:
      applyDeleteNoteById(events, editPass.targetNoteId, loopLengthTicks, lookupCache);
      // Positional cache entries become unsafe after structural mutations.
      clearNoteOnIndexLookupCache(lookupCache);
      break;
    case EditActionType::Create:
      applyAddNote(events, editPass.addedEvents, lookupCache);
      clearNoteOnIndexLookupCache(lookupCache);
      break;
    case EditActionType::Update:
      switch (editPass.propertyType) {
        case EditPropertyType::NoteRange:
          applyMoveNoteById(events, editPass.targetNoteId, editPass.startTick, editPass.endTick,
                            loopLengthTicks, lookupCache);
          clearNoteOnIndexLookupCache(lookupCache);
          break;
        case EditPropertyType::Length:
          applyChangeLengthById(events, editPass.targetNoteId, editPass.endTick, loopLengthTicks,
                                lookupCache);
          clearNoteOnIndexLookupCache(lookupCache);
          break;
        case EditPropertyType::Pitch:
          applyChangePitchById(events, editPass.targetNoteId, editPass.pitch, loopLengthTicks,
                               lookupCache);
          break;
        case EditPropertyType::Velocity:
          applyChangeVelocityById(events, editPass.targetNoteId, editPass.velocity, lookupCache);
          break;
        default:
          break;
      }
      break;
  }
}

void applyNoteEditPass(MidiEventVec& events, const EditPass& editPass, uint32_t loopLengthTicks) {
  applyNoteEditPassWithLookupCache(events, editPass, loopLengthTicks, nullptr);
}

NOTE_EDIT_MEM void applyNoteEditPassSequence(MidiEventVec& events, const EditPassVec& rows,
                                             uint32_t loopLengthTicks) {
  // Rows locate their note by targetNoteId, so startTick / endTick are payload only. Before
  // stable NoteId they doubled as the span lookup key and a later row had to be re-pointed at
  // an earlier row's span; carrying that rewrite forward overwrote the payload of a second row
  // for the same note.
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
  // Which executed applies actually cost the time. Duplicate Hide rows are already skipped by
  // the presentNoteIds guard, so the population count alone does not predict cost
  // (session_20260821_110842: 1694 rows, 45.4 s).
  constexpr uint32_t kSequenceReportUs = 10000u;
  const uint32_t sequenceStartUs = micros();
  uint32_t phaseStartUs = sequenceStartUs;
  uint32_t planUs = 0;
  uint32_t collectUs = 0;
  uint32_t guardUs = 0;
  uint32_t deleteUs = 0;
  uint32_t lengthUs = 0;
  uint32_t otherUs = 0;
  uint32_t rowsSkipped = 0;
  uint32_t deletesExecuted = 0;
  uint32_t lengthsExecuted = 0;
  resetLengthPathCounters();
  g_workingSetExternal = isInExternalMemoryPool(events.data()) ? 1u : 0u;
#endif
  std::vector<const EditPass*> plannedRows;
  uint32_t rowsCollapsed = 0;
  planContiguousCollapsedRows(rows, plannedRows, rowsCollapsed);
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
  planUs = micros() - phaseStartUs;
  phaseStartUs = micros();
#endif

  std::vector<NoteId> presentNoteIds;
  collectPresentNoteIds(events, presentNoteIds);
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
  collectUs = micros() - phaseStartUs;
#endif
  NoteOnIndexLookupCache noteOnLookupCache;
  noteOnLookupCache.entries.reserve(plannedRows.size());
  uint32_t rowsExecuted = 0;
  for (const EditPass* rowPtr : plannedRows) {
    if (rowPtr == nullptr) {
      continue;
    }
    const EditPass& row = *rowPtr;
    if (row.actionType == EditActionType::Delete ||
        row.actionType == EditActionType::Update) {
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
      const uint32_t guardStartUs = micros();
      const bool targetPresent = containsPresentNoteId(presentNoteIds, row.targetNoteId);
      guardUs += micros() - guardStartUs;
      if (!targetPresent) {
        ++rowsSkipped;
        clearNoteOnIndexLookupCache(&noteOnLookupCache);
        continue;
      }
#else
      if (!containsPresentNoteId(presentNoteIds, row.targetNoteId)) {
        clearNoteOnIndexLookupCache(&noteOnLookupCache);
        continue;
      }
#endif
    }
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
    const uint32_t applyStartUs = micros();
#endif
    applyNoteEditPassWithLookupCache(events, row, loopLengthTicks, &noteOnLookupCache);
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
    {
      const uint32_t applyUs = micros() - applyStartUs;
      if (row.actionType == EditActionType::Delete) {
        deleteUs += applyUs;
        ++deletesExecuted;
      } else if (row.actionType == EditActionType::Update &&
                 row.propertyType == EditPropertyType::Length) {
        lengthUs += applyUs;
        ++lengthsExecuted;
      } else {
        otherUs += applyUs;
      }
    }
#endif
    ++rowsExecuted;
    switch (row.actionType) {
      case EditActionType::Delete:
        erasePresentNoteId(presentNoteIds, row.targetNoteId);
        break;
      case EditActionType::Create:
        for (const MidiEvent& evt : row.addedEvents) {
          if (evt.isNoteOn()) {
            insertPresentNoteId(presentNoteIds, evt.noteId);
          }
        }
        break;
      case EditActionType::Update:
        break;
    }
  }
  (void)rowsCollapsed;
  (void)rowsExecuted;
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
  (void)planUs;
  (void)collectUs;
  (void)guardUs;
  (void)otherUs;
  (void)rowsSkipped;
  {
    const uint32_t sequenceUs = micros() - sequenceStartUs;
    if (sequenceUs >= kSequenceReportUs) {
      // Must fit kMaxCaptureTextBytes (192) including the #CAP prefix — a longer line is
      // silently clipped by queueCaptureTextLine. plan/collect/guard/other and the
      // planned/exec/skip/present counts are already measured and negligible.
      char line[192];
      snprintf(line, sizeof(line),
               EDIT_DIAG_FMT("#CAP,%lu,DIAG,cer_edit,us=%lu,del=%lu,ulen=%lu,recon=%lu,ndel=%u,"
                             "nlen=%u,lgth=%u,shrt=%u,noop=%u,ev=%u"),
               static_cast<unsigned long>(micros()), static_cast<unsigned long>(sequenceUs),
               static_cast<unsigned long>(deleteUs), static_cast<unsigned long>(lengthUs),
               static_cast<unsigned long>(g_reconstructUs),
               static_cast<unsigned>(deletesExecuted), static_cast<unsigned>(lengthsExecuted),
               static_cast<unsigned>(g_lengthenPathCount),
               static_cast<unsigned>(g_shortenPathCount),
               static_cast<unsigned>(g_noOpPathCount), static_cast<unsigned>(events.size()));
      DebugSessionCapture::appendCaptureTextLine(line);
      snprintf(line, sizeof(line),
               EDIT_DIAG_FMT("#CAP,%lu,DIAG,cer_heap,recon=%lu,ihit=%lu,spill=%lu,sbytes=%lu,"
                             "clk=%lu,iclk=%lu,ext=%u"),
               static_cast<unsigned long>(micros()), static_cast<unsigned long>(g_reconstructUs),
               static_cast<unsigned long>(g_reconInternalHits),
               static_cast<unsigned long>(g_reconSpillHits),
               static_cast<unsigned long>(g_reconSpillBytes),
               static_cast<unsigned long>(g_reconClockPulses),
               static_cast<unsigned long>(g_reconInternalClockDispatches),
               static_cast<unsigned>(g_workingSetExternal));
      DebugSessionCapture::appendCaptureTextLine(line);
    }
  }
#endif
}

NOTE_EDIT_MEM void applyNoteEditPassSequence(SessionMidiEventVec& events, const EditPassVec& rows,
                                             uint32_t loopLengthTicks) {
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
  const uint32_t wrapStartUs = micros();
#endif
  MidiEventVec scratch(events.begin(), events.end());
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
  const uint32_t copyInUs = micros() - wrapStartUs;
  const uint32_t coreStartUs = micros();
#endif
  applyNoteEditPassSequence(scratch, rows, loopLengthTicks);
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
  const uint32_t coreUs = micros() - coreStartUs;
  const uint32_t copyOutStartUs = micros();
#endif
  events.assign(scratch.begin(), scratch.end());
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
  {
    const uint32_t copyOutUs = micros() - copyOutStartUs;
    const uint32_t wrapUs = micros() - wrapStartUs;
    if (wrapUs >= 10000u) {
      char line[224];
      snprintf(line, sizeof(line),
               EDIT_DIAG_FMT("#CAP,%lu,DIAG,cer_copy,us=%lu,copyin=%lu,core=%lu,copyout=%lu,ev=%u"),
               static_cast<unsigned long>(micros()), static_cast<unsigned long>(wrapUs),
               static_cast<unsigned long>(copyInUs), static_cast<unsigned long>(coreUs),
               static_cast<unsigned long>(copyOutUs), static_cast<unsigned>(events.size()));
      DebugSessionCapture::appendCaptureTextLine(line);
    }
  }
#endif
}
