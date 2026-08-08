//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "NoteEditFocus.h"
#include "NoteEditFocusInternal.h"

#include <unordered_set>
#include <vector>

#include "EditSessionLiveStoreSpan.h"
#include "Utils/IntervalProjection.h"
#include "Utils/NoteEditMem.h"
#include "Utils/NoteUtils.h"

NOTE_EDIT_MEM NoteBaseline baselineFromDisplayNote(const NoteUtils::DisplayNote& dn) {
  return NoteBaseline{dn.note, dn.velocity, dn.startTick, dn.endTick};
}

NOTE_EDIT_MEM NoteId findBaselineNoteIdForDisplay(const NoteEditFocus& focus,
                                    const NoteUtils::DisplayNote& dn) {
  if (dn.noteId != kInvalidNoteId && focus.baselineMap.find(dn.noteId) != focus.baselineMap.end()) {
    return dn.noteId;
  }
  for (const auto& [noteId, baseline] : focus.baselineMap) {
    if (baseline.pitch == dn.note && baseline.startTick == dn.startTick &&
        baseline.endTick == dn.endTick) {
      return noteId;
    }
  }
  return dn.noteId;
}

NOTE_EDIT_MEM NoteBaseline baselineForDisplayNote(const NoteEditFocus& focus,
                                    const NoteUtils::DisplayNote& dn) {
  const NoteId noteId = findBaselineNoteIdForDisplay(focus, dn);
  const auto it = focus.baselineMap.find(noteId);
  if (it != focus.baselineMap.end()) {
    return it->second;
  }
  return baselineFromDisplayNote(dn);
}

NOTE_EDIT_FOCUS_INTERNAL_MEM bool isDisplayWrappedBaseline(const NoteBaseline& baseline) {
  return baseline.endTick < baseline.startTick;
}

NOTE_EDIT_MEM bool isPlausibleStorageSpan(uint32_t startTick, uint32_t endTick, uint32_t loopLength) {
  if (loopLength == 0) {
    return endTick > startTick;
  }
  if (endTick <= startTick) {
    return endTick + loopLength > startTick;
  }
  const uint32_t length = endTick - startTick;
  if (length > loopLength) {
    return false;
  }
  if (endTick <= loopLength) {
    return true;
  }
  return startTick > loopLength / 2 && endTick <= startTick + loopLength;
}

NOTE_EDIT_MEM bool isInflatedDisplaySpan(const NoteUtils::DisplayNote& dn, uint32_t loopLength) {
  return IntervalProjection::isInflatedDisplaySpan(dn, loopLength);
}

NOTE_EDIT_FOCUS_INTERNAL_MEM bool projectCanonicalBaselineForEdit(const NoteId noteId,
                                                   const NoteBaseline& canonical,
                                                   int32_t originTick, uint32_t loopLength,
                                                   NoteBaseline& out) {
  if (noteId == kInvalidNoteId || loopLength == 0) {
    return false;
  }
  if (canonical.endTick < canonical.startTick) {
    return false;
  }
  ProjectionContext context{};
  context.type = ProjectionType::Edit;
  context.loopLength = loopLength;
  context.window = IntervalProjection::makeFullLoopEditAnalysisWindow(loopLength);
  context.originTick = originTick;
  const CanonicalNoteSpan span{
      noteId,
      TickInterval{static_cast<int32_t>(canonical.startTick),
                   static_cast<int32_t>(canonical.endTick)},
      canonical.pitch,
      canonical.velocity,
  };
  const ProjectedNoteInterval projected =
      IntervalProjection::projectEditLinearSpan(span, context);
  if (projected.noteId == kInvalidNoteId) {
    return false;
  }
  out = canonical;
  out.startTick = static_cast<uint32_t>(projected.interval.start);
  out.endTick = static_cast<uint32_t>(projected.interval.end);
  return true;
}

NOTE_EDIT_MEM NoteBaseline linearBaselineForOverlapRestore(const NoteEditFocus& focus,
                                             const OverlapNote& entry,
                                             MidiEventVec* sessionEvents, uint8_t channel) {
  (void)sessionEvents;
  (void)channel;
  NoteBaseline baseline = entry.baseline;
  if (entry.noteId == kInvalidNoteId) {
    return baseline;
  }
  const auto mapIt = focus.baselineMap.find(entry.noteId);
  if (mapIt == focus.baselineMap.end() ||
      mapIt->second.endTick < mapIt->second.startTick) {
    return baseline;
  }
  if (mapIt->second.startTick != baseline.startTick) {
    return baseline;
  }
  if (entry.state == OverlapNoteStoreState::Hidden) {
    if (isDisplayWrappedBaseline(baseline)) {
      baseline = mapIt->second;
    }
    return baseline;
  }
  if (entry.state == OverlapNoteStoreState::Shortened) {
    baseline = mapIt->second;
    if (mapIt->second.endTick > entry.shortenedEndTick) {
      baseline.endTick = mapIt->second.endTick;
    }
    return baseline;
  }
  baseline = mapIt->second;
  return baseline;
}

NOTE_EDIT_MEM bool resolveLinearNoteSpanForOverlap(const NoteEditFocus& focus, MidiEventVec& events,
                                     uint8_t channel, const NoteUtils::DisplayNote& dn,
                                     NoteBaseline& out, uint32_t loopLength) {
  const NoteId noteId = dn.noteId != kInvalidNoteId ? dn.noteId
                                                    : findBaselineNoteIdForDisplay(focus, dn);
  NoteBaseline canonical{};
  bool hasCanonical = false;
  if (noteId != kInvalidNoteId) {
    const auto mapIt = focus.baselineMap.find(noteId);
    if (mapIt != focus.baselineMap.end() &&
        (loopLength == 0 ||
         isPlausibleStorageSpan(mapIt->second.startTick, mapIt->second.endTick, loopLength))) {
      canonical = mapIt->second;
      hasCanonical = true;
    }
  }
  if (!hasCanonical && dn.endTick >= dn.startTick &&
      (loopLength == 0 || !isInflatedDisplaySpan(dn, loopLength)) &&
      (loopLength == 0 ||
       isPlausibleStorageSpan(dn.startTick, dn.endTick, loopLength))) {
    canonical = baselineFromDisplayNote(dn);
    hasCanonical = true;
  }
  if (!hasCanonical && noteId != kInvalidNoteId &&
      findLinearNoteSpanForNoteId(events, noteId, channel, canonical, dn.startTick, loopLength)) {
    hasCanonical = true;
  }
  if (!hasCanonical && noteId != kInvalidNoteId &&
      findLinearNoteSpanForNoteId(events, noteId, channel, canonical, UINT32_MAX, loopLength)) {
    hasCanonical = true;
  }
  if (!hasCanonical) {
    return false;
  }
  const int32_t originTick = static_cast<int32_t>(dn.startTick);
  return projectCanonicalBaselineForEdit(noteId, canonical, originTick, loopLength, out);
}

NOTE_EDIT_FOCUS_INTERNAL_MEM bool isPlausibleOverlapDiffLiveBaseline(const NoteBaseline& live,
                                                                     uint32_t loopLength) {
  if (loopLength == 0) {
    return live.endTick > live.startTick;
  }
  return isPlausibleStorageSpan(live.startTick, live.endTick, loopLength);
}

template <typename Alloc>
NOTE_EDIT_FOCUS_INTERNAL_MEM bool readLiveBaselineForOverlapDiff(
    const std::vector<MidiEvent, Alloc>& sessionEvents, NoteId noteId, const NoteBaseline& baseline,
    uint8_t channel, uint32_t loopLength, NoteId movingNoteId, NoteBaseline& out) {
  std::vector<MidiEvent, Alloc>& mutableEvents =
      const_cast<std::vector<MidiEvent, Alloc>&>(sessionEvents);
  if (noteId != kInvalidNoteId &&
      findLinearNoteSpanForNoteId(mutableEvents, noteId, channel, out, UINT32_MAX, loopLength) &&
      isPlausibleOverlapDiffLiveBaseline(out, loopLength)) {
    return true;
  }
  NoteId resolvedId = kInvalidNoteId;
  for (const MidiEvent& evt : sessionEvents) {
    if (evt.channel != channel || !evt.isNoteOn() || evt.data.noteData.velocity == 0) {
      continue;
    }
    if (evt.data.noteData.note == baseline.pitch && evt.tick == baseline.startTick &&
        evt.noteId != kInvalidNoteId) {
      resolvedId = evt.noteId;
      break;
    }
  }
  if (resolvedId == kInvalidNoteId) {
    for (const MidiEvent& evt : sessionEvents) {
      if (!evt.isNoteOn() || evt.data.noteData.velocity == 0) {
        continue;
      }
      if (evt.data.noteData.note == baseline.pitch && evt.tick == baseline.startTick &&
          evt.noteId != kInvalidNoteId) {
        resolvedId = evt.noteId;
        break;
      }
    }
  }
  if (resolvedId == kInvalidNoteId) {
    resolvedId = noteId;
  }
  if (movingNoteId != kInvalidNoteId && resolvedId == movingNoteId) {
    return false;
  }
  if (findLinearNoteSpanForNoteId(mutableEvents, resolvedId, channel, out, UINT32_MAX, loopLength) &&
      isPlausibleOverlapDiffLiveBaseline(out, loopLength)) {
    return true;
  }
  if (findLinearNoteSpanForNoteId(mutableEvents, resolvedId, channel, out, baseline.startTick,
                                  loopLength) &&
      isPlausibleOverlapDiffLiveBaseline(out, loopLength)) {
    return true;
  }
  return false;
}

template bool readLiveBaselineForOverlapDiff<InternalHeapFirstAllocator<MidiEvent>>(
    const MidiEventVec&, NoteId, const NoteBaseline&, uint8_t, uint32_t, NoteId, NoteBaseline&);
template bool readLiveBaselineForOverlapDiff<ExternalMemoryFirstAllocator<MidiEvent>>(
    const SessionMidiEventVec&, NoteId, const NoteBaseline&, uint8_t, uint32_t, NoteId,
    NoteBaseline&);

NOTE_EDIT_FOCUS_INTERNAL_MEM bool linearStorageSpansOverlapLocal(uint32_t start1, uint32_t end1,
                                                  uint32_t start2, uint32_t end2) {
  return start1 < end2 && start2 < end1;
}

template <typename AllocA, typename AllocB>
NOTE_EDIT_MEM void populateBaselineMapForEditClosure(
    NoteEditFocus& focus, const std::vector<MidiEvent, AllocA>& committedLoopEvents,
    const std::vector<MidiEvent, AllocB>& sessionEvents, uint8_t channel, uint32_t loopLength) {
  if (!focus.active || loopLength == 0) {
    return;
  }
  // Transaction baseline is frozen at edit-driver boundary (D19). Insert-if-missing only —
  // never prune from live-store presence (hidden notes must keep their baseline entry).
  const std::unordered_set<NoteId> closure =
      buildEditClosureNoteIds(focus, sessionEvents, channel, loopLength);
  std::vector<MidiEvent, AllocA> mutableCommitted = committedLoopEvents;
  // Pass materialize noteIds can differ from session-store ids — resolve committed span by
  // live pitch+start when noteId lookup fails.
  std::vector<MidiEvent, AllocB> mutableSession = sessionEvents;
  for (NoteId noteId : closure) {
    if (noteId == kInvalidNoteId || focus.baselineMap.find(noteId) != focus.baselineMap.end()) {
      continue;
    }
    NoteBaseline baseline{};
    if (findLinearNoteSpanForNoteId(mutableCommitted, noteId, channel, baseline, UINT32_MAX,
                                    loopLength)) {
      focus.baselineMap[noteId] = baseline;
      continue;
    }
    NoteBaseline liveSpan{};
    if (!findLinearNoteSpanForNoteId(mutableSession, noteId, channel, liveSpan, UINT32_MAX,
                                     loopLength)) {
      continue;
    }
    // AllocA for committed is always InternalHeapFirstAllocator (MidiEventVec) at call sites.
    if (findCommittedLinearSpanForPitchStart(mutableCommitted, channel, liveSpan.pitch,
                                             liveSpan.startTick, loopLength, baseline)) {
      focus.baselineMap[noteId] = baseline;
    } else {
      focus.baselineMap[noteId] = liveSpan;
    }
  }
}

template void populateBaselineMapForEditClosure<InternalHeapFirstAllocator<MidiEvent>,
                                                InternalHeapFirstAllocator<MidiEvent>>(
    NoteEditFocus&, const MidiEventVec&, const MidiEventVec&, uint8_t, uint32_t);
template void populateBaselineMapForEditClosure<InternalHeapFirstAllocator<MidiEvent>,
                                                ExternalMemoryFirstAllocator<MidiEvent>>(
    NoteEditFocus&, const MidiEventVec&, const SessionMidiEventVec&, uint8_t, uint32_t);

template <typename Alloc>
std::unordered_set<NoteId> buildEditClosureNoteIds(const NoteEditFocus& focus,
                                                 const std::vector<MidiEvent, Alloc>& sessionEvents,
                                                 uint8_t channel, uint32_t loopLength) {
  std::unordered_set<NoteId> ids;
  if (!focus.active || loopLength == 0) {
    return ids;
  }
  if (focus.movingNoteId != kInvalidNoteId) {
    ids.insert(focus.movingNoteId);
  }
  for (const auto& [noteId, entry] : focus.overlapNotes) {
    (void)entry;
    if (noteId != kInvalidNoteId) {
      ids.insert(noteId);
    }
  }
  // Full-loop transaction baseline (D19 / D21): every live noteId participates so restore
  // candidates survive pitch changes. Analyze still pitch-gates Hide/Shorten (Q14).
  for (const MidiEvent& onEvt : sessionEvents) {
    if (!onEvt.isNoteOn() || onEvt.data.noteData.velocity == 0 ||
        onEvt.noteId == kInvalidNoteId) {
      continue;
    }
    ids.insert(onEvt.noteId);
  }
  // BaselineMap keys that are already frozen (including notes hidden from live store).
  for (const auto& [noteId, baseline] : focus.baselineMap) {
    (void)baseline;
    if (noteId != kInvalidNoteId) {
      ids.insert(noteId);
    }
  }
  return ids;
}

template std::unordered_set<NoteId> buildEditClosureNoteIds<InternalHeapFirstAllocator<MidiEvent>>(
    const NoteEditFocus&, const MidiEventVec&, uint8_t, uint32_t);
template std::unordered_set<NoteId> buildEditClosureNoteIds<ExternalMemoryFirstAllocator<MidiEvent>>(
    const NoteEditFocus&, const SessionMidiEventVec&, uint8_t, uint32_t);
