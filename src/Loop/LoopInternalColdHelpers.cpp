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

LOOP_INTERNAL_MEM void applyCaptureEventToPreview(CapturePreview& preview, const MidiEvent& evt,
                                                  uint32_t ticksPerBar) {
  if (evt.isNoteOn()) {
    NoteUtils::DisplayNote note{};
    note.note = evt.data.noteData.note;
    note.velocity = evt.data.noteData.velocity;
    note.startTick = evt.tick;
    note.endTick = evt.tick;
    preview.notes.push_back(note);
    markPreviewSpan(preview, evt.tick, evt.tick, ticksPerBar);
    ++preview.revision;
    return;
  }

  if (!evt.isNoteOff()) {
    return;
  }

  for (auto it = preview.notes.rbegin(); it != preview.notes.rend(); ++it) {
    if (it->note != evt.data.noteData.note) {
      continue;
    }
    if (it->endTick != it->startTick) {
      continue;
    }
    const uint32_t start = it->startTick;
    it->endTick = evt.tick;
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
  loop.capturePreview.dirtyBars.clear();
  if (loop.loopLengthTicks > 0) {
    for (const auto& note : loop.capturePreview.notes) {
      const uint32_t endTick = note.endTick >= note.startTick ? note.endTick : note.startTick;
      markPreviewSpan(loop.capturePreview, note.startTick, endTick, Config::TICKS_PER_BAR);
    }
  }
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
