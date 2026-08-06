//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"

#include "Globals.h"
#include "LoopInternal.h"
#include "Utils/NoteUtils.h"

void Loop::shiftActiveCapturePassTicks(int64_t delta) {
  if (delta == 0 || !hasCommittedPasses()) {
    return;
  }
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Active &&
      !passes.recordPass.committedChunkIds.empty()) {
    MidiEventVec flat;
    LoopEventStore::appendChunkRefEvents(passes.recordPass.committedChunkIds, flat);
    if (!flat.empty()) {
      LoopEventStore staging;
      staging.loadFromEvents(flat);
      staging.shiftAllTicks(delta);
      LoopEventStore temp;
      temp.adoptAll(staging);
      LoopEventStore::releaseChunkRefs(passes.recordPass.committedChunkIds);
      passes.recordPass.committedChunkIds.clear();
      CaptureChunkIdList captureIds;
      temp.detachChunksTo(captureIds);
      LoopEventStore::transferCaptureChunkIdsToCommittedChunkIds(passes.recordPass.committedChunkIds,
                                                         captureIds);
    }
  }
  for (OverdubPass& pass : passes.overdubPasses) {
    if (pass.state != CapturePassState::Active || pass.committedChunkIds.empty()) {
      continue;
    }
    MidiEventVec flat;
    LoopEventStore::appendChunkRefEvents(pass.committedChunkIds, flat);
    if (flat.empty()) {
      continue;
    }
    LoopEventStore staging;
    staging.loadFromEvents(flat);
    staging.shiftAllTicks(delta);
    LoopEventStore temp;
    temp.adoptAll(staging);
    LoopEventStore::releaseChunkRefs(pass.committedChunkIds);
    pass.committedChunkIds.clear();
    CaptureChunkIdList captureIds;
    temp.detachChunksTo(captureIds);
    LoopEventStore::transferCaptureChunkIdsToCommittedChunkIds(pass.committedChunkIds, captureIds);
  }
  for (EditPass& editPass : passes.editPasses) {
    if (editPass.state != EditPassState::Active) {
      continue;
    }
    if (editPass.passType != EditPassType::Note) {
      continue;
    }
    editPass.startTick =
        static_cast<uint32_t>(static_cast<int64_t>(editPass.startTick) + delta);
    editPass.endTick =
        static_cast<uint32_t>(static_cast<int64_t>(editPass.endTick) + delta);
    for (MidiEvent& evt : editPass.addedEvents) {
      evt.tick = static_cast<uint32_t>(static_cast<int64_t>(evt.tick) + delta);
    }
  }
  ++playbackRevision;
  markPassDerivedStale();
}

void Loop::beginCapture(CapturePhase phase) {
  discardPendingCapturePass();
  capture.phase = phase;
  capture.store.clear();
  capturePreview.clear();
  captureNextEventIndex = 0;
  captureEventsSortDirty = false;
  captureDedupEventsDropped_ = 0;
  ++captureDisplayRevision;
}

void Loop::discardCapture() {
  capture.store.clear();
  capture.phase = CapturePhase::None;
  captureNextEventIndex = 0;
  captureEventsSortDirty = false;
  capturePreview.clear();
  captureDedupEventsDropped_ = 0;
}

bool Loop::appendCaptureEvent(const MidiEvent& evt) {
  if (capture.phase == CapturePhase::None) {
    return false;
  }
  if (hasPendingCapturePass_) {
    return false;
  }
  if (isDuplicateCaptureEvent(*this, evt)) {
    ++captureDedupEventsDropped_;
    return false;
  }
  if (!capture.store.append(evt)) {
    return false;
  }
  captureEventsSortDirty = true;
  applyCaptureEventToPreview(capturePreview, evt, Config::TICKS_PER_BAR);
  ++captureDisplayRevision;
  return true;
}

bool Loop::removeOpenCaptureNoteOn(uint8_t channel, uint8_t note) {
  if (!captureActive() || capture.store.empty() || loopLengthTicks == 0) {
    return false;
  }
  ensureCaptureEventsSorted();

  SessionMidiEventVec flat;
  capture.store.copyEventsTo(flat);
  if (flat.empty()) {
    return false;
  }

  const std::vector<NoteUtils::OpenNoteOn> opens =
      NoteUtils::findOpenNoteOns(flat, loopLengthTicks);
  uint32_t openTick = UINT32_MAX;
  for (const NoteUtils::OpenNoteOn& open : opens) {
    if (open.note != note) {
      continue;
    }
    for (const MidiEvent& evt : flat) {
      if (evt.isNoteOn() && evt.channel == channel && evt.data.noteData.note == note &&
          evt.tick == open.tick) {
        openTick = open.tick;
        break;
      }
    }
    if (openTick != UINT32_MAX) {
      break;
    }
  }
  if (openTick == UINT32_MAX) {
    return false;
  }

  SessionMidiEventVec kept;
  kept.reserve(flat.size() - 1);
  bool removed = false;
  for (const MidiEvent& evt : flat) {
    if (!removed && evt.isNoteOn() && evt.channel == channel &&
        evt.data.noteData.note == note && evt.tick == openTick &&
        evt.data.noteData.velocity > 0) {
      removed = true;
      continue;
    }
    kept.push_back(evt);
  }
  if (!removed) {
    return false;
  }

  capture.store.clear();
  if (!kept.empty()) {
    capture.store.loadFromEvents(kept);
  }
  captureEventsSortDirty = false;
  rebuildCapturePreviewFromStore(*this);
  ++captureDisplayRevision;
  return true;
}

bool Loop::captureHasNoteOffAfter(uint8_t channel, uint8_t note, uint32_t onTick) const {
  if (!captureActive() || capture.store.empty()) {
    return false;
  }
  SessionMidiEventVec flat;
  capture.store.copyEventsTo(flat);
  for (const MidiEvent& evt : flat) {
    if (evt.isNoteOff() && evt.channel == channel && evt.data.noteData.note == note &&
        evt.tick > onTick) {
      return true;
    }
  }
  return false;
}

size_t Loop::liveEventCount() const {
  MidiEventVec flat;
  passes.materializeToEventVector(flat, loopLengthTicks);
  size_t count = flat.size();
  if (captureActive()) {
    count += capture.store.size();
  }
  return count;
}

bool Loop::captureActive() const {
  return capture.phase != CapturePhase::None;
}

bool Loop::ensureCaptureEventsSorted() {
  if (!captureEventsSortDirty) {
    return false;
  }
  sortCaptureStoreByTick(capture.store);
  captureEventsSortDirty = false;
  return true;
}

void Loop::clearCaptureOnNewPass() {
  discardCapture();
}
