//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "LoopInternal.h"

#include "Globals.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/NoteUtils.h"

#include <algorithm>
#include <chrono>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

LOOP_INTERNAL_MEM bool eventsEquivalent(const MidiEvent& a, const MidiEvent& b) {
  if (a.type != b.type || a.channel != b.channel || a.tick != b.tick) {
    return false;
  }
  if (a.type == midi::NoteOn || a.type == midi::NoteOff) {
    return a.data.noteData.note == b.data.noteData.note &&
           a.data.noteData.velocity == b.data.noteData.velocity;
  }
  if (a.type == midi::ControlChange) {
    return a.data.ccData.cc == b.data.ccData.cc && a.data.ccData.value == b.data.ccData.value;
  }
  return true;
}

LOOP_INTERNAL_MEM bool isDuplicateCaptureEvent(const Loop& loop, const MidiEvent& candidate) {
  const uint32_t lo = (candidate.tick > Config::DUPLICATE_TICK_TOLERANCE)
                          ? (candidate.tick - Config::DUPLICATE_TICK_TOLERANCE)
                          : 0;
  const uint32_t hi = candidate.tick + Config::DUPLICATE_TICK_TOLERANCE;

  const size_t captureCount = loop.capture.store.size();
  for (size_t i = captureCount; i > 0; --i) {
    const MidiEvent& evt = loop.capture.store.at(i - 1);
    if (evt.tick < lo) {
      break;
    }
    if (evt.tick > hi) {
      continue;
    }
    if (eventsEquivalent(evt, candidate)) {
      return true;
    }
  }
  return false;
}

LOOP_INTERNAL_MEM const char* capturePhaseLabel(CapturePhase phase) {
  switch (phase) {
    case CapturePhase::Record:
      return "record";
    case CapturePhase::Overdub:
      return "overdub";
    default:
      return "none";
  }
}

LOOP_INTERNAL_MEM void sortCaptureStoreByTick(LoopEventStore& store) {
  SessionMidiEventVec sorted;
  store.copyEventsTo(sorted);
  std::stable_sort(sorted.begin(), sorted.end(),
                   [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
  store.clear();
  store.loadFromEvents(sorted);
}

LOOP_INTERNAL_MEM void markPreviewSpan(CapturePreview& preview, uint32_t startTick, uint32_t endTick,
                                        uint32_t ticksPerBar) {
  if (ticksPerBar == 0) {
    return;
  }
  const uint32_t startBar = startTick / ticksPerBar;
  const uint32_t endBar = endTick / ticksPerBar;
  for (uint32_t bar = startBar; bar <= endBar; ++bar) {
    preview.markBarDirty(bar);
  }
}

namespace {

void removeOpenPreviewNote(CapturePreview& preview, uint32_t noteIndex) {
  const auto found =
      std::find(preview.openNoteIndices.begin(), preview.openNoteIndices.end(), noteIndex);
  if (found != preview.openNoteIndices.end()) {
    preview.openNoteIndices.erase(found);
  }
}

int findPreferredWrapPreviewNote(const CapturePreview& preview, const MidiEvent& evt,
                                 uint32_t loopLength) {
  int preferredIndex = -1;
  uint32_t preferredStartTick = 0;
  const size_t noteCount = std::min(preview.notes.size(), preview.noteStates.size());
  for (size_t noteIndex = 0; noteIndex < noteCount; ++noteIndex) {
    const CapturePreviewNoteState& state = preview.noteStates[noteIndex];
    const NoteUtils::DisplayNote& note = preview.notes[noteIndex];
    const bool isOpenOrLoopEndClosed =
        state.open || (!state.wrapHeld && loopLength > 0 &&
                       note.endTick == loopLength - 1);
    if (!isOpenOrLoopEndClosed || state.channel != evt.channel ||
        state.pitch != evt.data.noteData.note ||
        !NoteUtils::isHeadTailWrappedPair(note.startTick, evt.tick, loopLength)) {
      continue;
    }
    if (preferredIndex < 0 || note.startTick > preferredStartTick) {
      preferredIndex = static_cast<int>(noteIndex);
      preferredStartTick = note.startTick;
    }
  }
  return preferredIndex;
}

bool findPreferredPreviewHeadOffTick(const MidiEventVec& events,
                                     const NoteUtils::DisplayNote& note,
                                     const CapturePreviewNoteState& state,
                                     uint32_t loopLength, uint32_t& headOffTickOut) {
  if (loopLength == 0) {
    return false;
  }
  for (const MidiEvent& evt : events) {
    if (!evt.isNoteOff() || evt.channel != state.channel ||
        evt.data.noteData.note != state.pitch) {
      continue;
    }
    uint32_t headOffTick = evt.tick;
    if (headOffTick >= loopLength) {
      headOffTick %= loopLength;
    }
    if (headOffTick >= loopLength - 1) {
      continue;
    }
    if (NoteUtils::isPreferredWrapTailForHeadOff(
            note.startTick, headOffTick, events, state.pitch, state.channel,
            loopLength)) {
      headOffTickOut = headOffTick;
      return true;
    }
  }
  return false;
}

}  // namespace

LOOP_INTERNAL_MEM void applyCaptureEventToPreview(CapturePreview& preview, const MidiEvent& evt,
                                                  uint32_t ticksPerBar,
                                                  uint32_t loopLength) {
  if (evt.isNoteOn()) {
    NoteUtils::DisplayNote note{};
    note.note = evt.data.noteData.note;
    note.velocity = evt.data.noteData.velocity;
    note.startTick = evt.tick;
    note.endTick = evt.tick;
    preview.notes.push_back(note);
    CapturePreviewNoteState state;
    state.channel = evt.channel;
    state.pitch = evt.data.noteData.note;
    state.open = true;
    preview.noteStates.push_back(state);
    preview.openNoteIndices.push_back(
        static_cast<uint32_t>(preview.notes.size() - 1));
    markPreviewSpan(preview, evt.tick, evt.tick, ticksPerBar);
    ++preview.revision;
    return;
  }

  if (!evt.isNoteOff()) {
    return;
  }

  const int wrapNoteIndex = findPreferredWrapPreviewNote(preview, evt, loopLength);
  if (wrapNoteIndex >= 0) {
    const size_t noteIndex = static_cast<size_t>(wrapNoteIndex);
    NoteUtils::DisplayNote& note = preview.notes[noteIndex];
    CapturePreviewNoteState& state = preview.noteStates[noteIndex];
    state.open = false;
    state.wrapHeld = true;
    state.hasPreferredHeadOff = true;
    state.preferredHeadOffTick = evt.tick;
    removeOpenPreviewNote(preview, static_cast<uint32_t>(noteIndex));

    const uint32_t tailStartTick = note.startTick;
    note.endTick = loopLength - 1;
    preview.changedNoteIndices.push_back(static_cast<uint32_t>(noteIndex));
    const NoteUtils::WrapHeadSegment head = NoteUtils::resolveWrapHeadSegment(
        loopLength, evt.tick, NoteUtils::WrapHeadSegmentContext::CommittedHeadOff);
    if (head.visible) {
      NoteUtils::DisplayNote headNote{};
      headNote.note = note.note;
      headNote.velocity = note.velocity;
      headNote.startTick = head.startTick;
      headNote.endTick = head.endTickInclusive;
      preview.notes.push_back(headNote);
      CapturePreviewNoteState headState;
      headState.channel = evt.channel;
      headState.pitch = evt.data.noteData.note;
      preview.noteStates.push_back(headState);
    }
    markPreviewSpan(preview, tailStartTick, loopLength - 1, ticksPerBar);
    markPreviewSpan(preview, 0, evt.tick, ticksPerBar);
    ++preview.revision;
    return;
  }

  for (auto it = preview.notes.rbegin(); it != preview.notes.rend(); ++it) {
    if (it->note != evt.data.noteData.note) {
      continue;
    }
    const size_t noteIndex =
        static_cast<size_t>(std::distance(preview.notes.begin(), it.base()) - 1);
    if (noteIndex >= preview.noteStates.size() ||
        !preview.noteStates[noteIndex].open) {
      continue;
    }
    if (evt.tick < it->startTick && noteIndex < preview.noteStates.size() &&
        preview.noteStates[noteIndex].channel != evt.channel) {
      continue;
    }
    const uint32_t start = it->startTick;
    it->endTick = evt.tick;
    if (noteIndex < preview.noteStates.size()) {
      preview.noteStates[noteIndex].open = false;
    }
    removeOpenPreviewNote(preview, static_cast<uint32_t>(noteIndex));
    preview.changedNoteIndices.push_back(static_cast<uint32_t>(noteIndex));
    markPreviewSpan(preview, start, evt.tick, ticksPerBar);
    ++preview.revision;
    return;
  }
}

LOOP_INTERNAL_MEM void rebuildCapturePreviewFromStore(Loop& loop) {
  MidiEventVec flat;
  loop.capture.store.copyEventsTo(flat);
  const NoteUtils::DisplayNoteVec rebuiltNotes =
      NoteUtils::reconstructDisplayNotes(flat, loop.loopLengthTicks, false);
  loop.capturePreview.notes.assign(rebuiltNotes.begin(), rebuiltNotes.end());
  loop.capturePreview.noteStates.clear();
  loop.capturePreview.noteStates.resize(loop.capturePreview.notes.size());
  loop.capturePreview.openNoteIndices.clear();
  const std::vector<NoteUtils::OpenNoteOn> openNotes =
      NoteUtils::findOpenNoteOns(flat, loop.loopLengthTicks);
  for (size_t noteIndex = 0; noteIndex < loop.capturePreview.notes.size(); ++noteIndex) {
    const NoteUtils::DisplayNote& note = loop.capturePreview.notes[noteIndex];
    CapturePreviewNoteState& state = loop.capturePreview.noteStates[noteIndex];
    state.pitch = note.note;
    for (const MidiEvent& evt : flat) {
      if (evt.isNoteOn() && evt.data.noteData.note == note.note &&
          evt.tick == note.startTick) {
        state.channel = evt.channel;
        break;
      }
    }
    const bool isOpen = std::any_of(
        openNotes.begin(), openNotes.end(), [&](const NoteUtils::OpenNoteOn& open) {
          return open.note == note.note && open.tick == note.startTick;
        });
    if (isOpen) {
      state.open = true;
      loop.capturePreview.openNoteIndices.push_back(
          static_cast<uint32_t>(noteIndex));
    }
    uint32_t preferredHeadOffTick = 0;
    if (findPreferredPreviewHeadOffTick(flat, note, state, loop.loopLengthTicks,
                                        preferredHeadOffTick)) {
      state.wrapHeld = true;
      state.hasPreferredHeadOff = true;
      state.preferredHeadOffTick = preferredHeadOffTick;
    } else if (state.open) {
      const NoteUtils::OpenNoteOn open{note.note, note.velocity, note.startTick};
      state.wrapHeld =
          NoteUtils::isWrapHeldOpenNote(flat, open, loop.loopLengthTicks);
    }
  }
  loop.capturePreview.changedNoteIndices.clear();
  loop.capturePreview.dirtyBars.clear();
  if (loop.loopLengthTicks > 0) {
    for (const auto& note : loop.capturePreview.notes) {
      const uint32_t endTick = note.endTick >= note.startTick ? note.endTick : note.startTick;
      markPreviewSpan(loop.capturePreview, note.startTick, endTick, Config::TICKS_PER_BAR);
    }
  }
  ++loop.capturePreview.replacementRevision;
  ++loop.capturePreview.revision;
}

LOOP_INTERNAL_MEM RecordPass deepCloneRecordPass(const RecordPass& pass) {
  RecordPass cloned = pass;
  CommittedChunkIdList clonedIds;
  if (!LoopEventStore::deepCloneCommittedChunkIds(clonedIds, pass.committedChunkIds)) {
    cloned.committedChunkIds.clear();
    return cloned;
  }
  cloned.committedChunkIds = std::move(clonedIds);
  return cloned;
}

LOOP_INTERNAL_MEM OverdubPass deepCloneOverdubPass(const OverdubPass& pass) {
  OverdubPass cloned = pass;
  CommittedChunkIdList clonedIds;
  if (!LoopEventStore::deepCloneCommittedChunkIds(clonedIds, pass.committedChunkIds)) {
    cloned.committedChunkIds.clear();
    return cloned;
  }
  cloned.committedChunkIds = std::move(clonedIds);
  return cloned;
}

LOOP_INTERNAL_MEM LoopPasses deepClonePasses(const LoopPasses& passes) {
  LoopPasses cloned;
  if (passes.hasRecordPass()) {
    cloned.recordPass = deepCloneRecordPass(passes.recordPass);
  }
  cloned.overdubPasses.reserve(passes.overdubPasses.size());
  for (const OverdubPass& pass : passes.overdubPasses) {
    cloned.overdubPasses.push_back(deepCloneOverdubPass(pass));
  }
  cloned.editPasses = passes.editPasses;
  cloned.loopGeometries = passes.loopGeometries;
  return cloned;
}

LOOP_INTERNAL_MEM size_t estimatedEditPassBytes(const EditPass& row) {
  size_t bytes = sizeof(EditPass);
  bytes += row.addedEvents.size() * sizeof(MidiEvent);
  return bytes;
}

LOOP_INTERNAL_MEM bool canHeapAdmitEditPass(const EditPass& row) {
  const size_t needed = Config::HEAP_RESERVE_BYTES + estimatedEditPassBytes(row);
  return MemoryMonitor::getInternalHeapFreeBytes() >= needed;
}

LOOP_INTERNAL_MEM const char* sealOutcomeLabel(SealOutcome outcome) {
  switch (outcome) {
    case SealOutcome::Ok:
      return "ok";
    case SealOutcome::SkippedEmpty:
      return "skipped_empty";
    case SealOutcome::FailedValidation:
      return "failed_validation";
    case SealOutcome::AlreadyPending:
      return "already_pending";
    case SealOutcome::PoolExhausted:
      return "pool_exhausted";
  }
  return "unknown";
}

LOOP_INTERNAL_MEM size_t stopPathChunkRefCount(const Loop& loop) {
  size_t refs = 0;
  if (loop.passes.hasRecordPass() && loop.passes.recordPass.state == CapturePassState::Active) {
    refs += loop.passes.recordPass.committedChunkIds.size();
  }
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    if (pass.state == CapturePassState::Active) {
      refs += pass.committedChunkIds.size();
    }
  }
  if (loop.hasPendingCapturePass()) {
    refs += loop.pendingCapturePass().committedChunkIds.size();
  }
  return refs;
}

LOOP_INTERNAL_MEM size_t stopPathEventCount(const Loop& loop) {
  size_t events = 0;
  if (loop.passes.hasRecordPass() && loop.passes.recordPass.state == CapturePassState::Active) {
    events += LoopEventStore::countEventsInChunkIds(loop.passes.recordPass.committedChunkIds);
  }
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    if (pass.state == CapturePassState::Active) {
      events += LoopEventStore::countEventsInChunkIds(pass.committedChunkIds);
    }
  }
  if (loop.hasPendingCapturePass()) {
    events += LoopEventStore::countEventsInChunkIds(loop.pendingCapturePass().committedChunkIds);
  }
  if (loop.captureActive()) {
    events += loop.capture.store.size();
  }
  return events;
}

LOOP_INTERNAL_MEM uint32_t traceMicros() {
#if defined(ARDUINO)
  return micros();
#else
  using namespace std::chrono;
  return static_cast<uint32_t>(
      duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
#endif
}
