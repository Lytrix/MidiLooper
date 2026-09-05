//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"

#include "CommittedEventRange.h"
#include "EditApply.h"
#include "Globals.h"
#include "LoopInternal.h"
#include "Utils/Diagnostics.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/LoopMem.h"

#include <algorithm>
#include <vector>

#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
#include "Utils/DebugSessionCapture.h"
#include <Arduino.h>
#endif

// Keep diagnostic format strings out of DTCM .rodata — see RT_TAG in RuntimeTimingTelemetry.
#if defined(SESSION_CAPTURE) && defined(__IMXRT1062__)
#define LOOP_DIAG_FMT(literal) PSTR(literal)
#else
#define LOOP_DIAG_FMT(literal) literal
#endif

namespace {

struct CommittedPitchQueryWork {
  uint32_t fullMaterializeCount = 0;
};

}  // namespace

static CommittedPitchQueryWork g_committedPitchQueryWork;

namespace {

template <typename MidiEventVector>
void mergeSortedLoopCaptureLayers(MidiEventVector& base, MidiEventVector&& addition) {
  if (addition.empty()) {
    return;
  }
  if (base.empty()) {
    base = std::move(addition);
    return;
  }
  MidiEventVector merged;
  merged.reserve(base.size() + addition.size());
  std::merge(base.begin(), base.end(), addition.begin(), addition.end(),
             std::back_inserter(merged),
             [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
  base = std::move(merged);
}

template <typename MidiEventVector>
void mergeActiveCapturePassesInto(const LoopPasses& passes, MidiEventVector& out) {
  out.clear();
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Active &&
      !passes.recordPass.committedChunkIds.empty()) {
    LoopEventStore::appendChunkRefEvents(passes.recordPass.committedChunkIds, out);
  }
  std::vector<const OverdubPass*> activeOverdubs;
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.state == CapturePassState::Active && !pass.committedChunkIds.empty()) {
      activeOverdubs.push_back(&pass);
    }
  }
  std::sort(activeOverdubs.begin(), activeOverdubs.end(),
            [](const OverdubPass* a, const OverdubPass* b) {
              return a->mergeSequence < b->mergeSequence;
            });
  for (const OverdubPass* pass : activeOverdubs) {
    MidiEventVector layer;
    LoopEventStore::appendChunkRefEvents(pass->committedChunkIds, layer);
    mergeSortedLoopCaptureLayers(out, std::move(layer));
  }
}

void mergeCaptureStoreIntoWindowedEvents(const Loop& loop, SessionMidiEventVec& out,
                                         uint32_t windowStart, uint32_t windowLength) {
  if (!loop.captureActive() || loop.capture.store.empty()) {
    return;
  }
  const_cast<Loop&>(loop).ensureCaptureEventsSorted();
  SessionMidiEventVec captureFlat;
  loop.capture.store.copyEventsTo(captureFlat);
  SessionMidiEventVec captureWindow;
  DisplayWindowUtils::filterMidiEventsToWindow(captureFlat, captureWindow, windowStart, windowLength,
                                               loop.loopLengthTicks);
  if (captureWindow.empty()) {
    return;
  }
  mergeSortedLoopCaptureLayers(out, std::move(captureWindow));
}

template <typename MidiEventVector>
void mergeCaptureStoreIntoMaterializedEvents(const Loop& loop, MidiEventVector& out) {
  if (!loop.captureActive() || loop.capture.store.empty()) {
    return;
  }
  const_cast<Loop&>(loop).ensureCaptureEventsSorted();

  MidiEventVector captureFlat;
  loop.capture.store.copyEventsTo(captureFlat);
  if (out.empty()) {
    out = std::move(captureFlat);
    return;
  }
  if (captureFlat.empty()) {
    return;
  }

  MidiEventVector merged;
  merged.reserve(out.size() + captureFlat.size());
  std::merge(out.begin(), out.end(), captureFlat.begin(), captureFlat.end(),
             std::back_inserter(merged),
             [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
  out = std::move(merged);
}

void collectActiveCommittedChunkLists(const LoopPasses& passes,
                                      std::vector<const CommittedChunkIdList*>& lists) {
  lists.clear();
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Active &&
      !passes.recordPass.committedChunkIds.empty()) {
    lists.push_back(&passes.recordPass.committedChunkIds);
  }
  std::vector<const OverdubPass*> activeOverdubs;
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.state == CapturePassState::Active && !pass.committedChunkIds.empty()) {
      activeOverdubs.push_back(&pass);
    }
  }
  std::sort(activeOverdubs.begin(), activeOverdubs.end(),
            [](const OverdubPass* a, const OverdubPass* b) {
              return a->mergeSequence < b->mergeSequence;
            });
  for (const OverdubPass* pass : activeOverdubs) {
    lists.push_back(&pass->committedChunkIds);
  }
}

}  // namespace

void Loop::mergeActiveCapturePasses(MidiEventVec& out) const {
  mergeActiveCapturePassesInto(passes, out);
}

void Loop::mergeActiveCapturePasses(SessionMidiEventVec& out) const {
  mergeActiveCapturePassesInto(passes, out);
}

void Loop::materializeEditViewFromPasses() const {
  ensureEffectiveEventStoreCurrent();
}

void Loop::rebuildEffectiveEventStore() const {
  Loop* self = const_cast<Loop*>(this);
  SessionMidiEventVec flat;
  passes.materializeToEventVector(flat, self->loopLengthTicks);
  self->passesMaterializedStore_.mutStore().clear();
  self->passesMaterializedStore_.discardEventsCache();
  SessionMidiEventVec& events = self->passesMaterializedStore_.mutEvents();
  events = std::move(flat);
  self->passesMaterializedStoreStale_ = false;
  ++self->effectiveEventStoreRevision_;
}

void Loop::ensureEffectiveEventStoreCurrent() const {
  if (!passesMaterializedStoreStale_) {
    if (!hasCommittedPasses()) {
      return;
    }
    if (!passesMaterializedStore_.readEvents().empty()) {
      return;
    }
  }
  rebuildEffectiveEventStore();
}

void Loop::copyEffectiveCommittedEvents(SessionMidiEventVec& out) const {
  ensureEffectiveEventStoreCurrent();
  const SessionMidiEventVec& events = passesMaterializedStore_.readEvents();
  out.assign(events.begin(), events.end());
}

LOOP_COLD_MEM void Loop::copyEffectiveCommittedEventsInRange(SessionMidiEventVec& out,
                                                             uint32_t windowStart,
                                                             uint32_t windowLength,
                                                             bool includeActiveEditRows) const {
  if (loopLengthTicks == 0 || windowLength == 0) {
    out.clear();
    return;
  }
  if (windowLength >= loopLengthTicks) {
    windowStart = 0;
    windowLength = loopLengthTicks;
  }

#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
  // Attribute the siblings of appendWindowBudgeted. DIAG,cer_win never crossed 10 ms during a
  // 34.6 s gather (session_20260821_103826), so the cost is outside the traversal.
  constexpr uint32_t kRangeReportUs = 10000u;
  const uint32_t rangeStartUs = micros();
  uint32_t rangePhaseUs = rangeStartUs;
  uint32_t collectUs = 0;
  uint32_t appendUs = 0;
  uint32_t sortUs = 0;
  uint32_t editUs = 0;
  uint32_t editRowCount = 0;
#endif
  std::vector<const CommittedChunkIdList*> lists;
  collectActiveCommittedChunkLists(passes, lists);
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
  collectUs = micros() - rangePhaseUs;
  rangePhaseUs = micros();
#endif
  out.clear();
  if (lists.empty()) {
    return;
  }

  EditPassVec activeRows;
  if (includeActiveEditRows) {
    for (const EditPass& editPass : passes.editPasses) {
      if (editPass.state == EditPassState::Active && editPass.passType == EditPassType::Note) {
        activeRows.push_back(editPass);
      }
    }
  }
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
  editRowCount = static_cast<uint32_t>(activeRows.size());
#endif
  const auto tickLess = [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; };

  if (activeRows.empty()) {
    CommittedEventRange::inWindow(lists.data(), lists.size(), loopLengthTicks, windowStart,
                                  windowLength)
        .appendTo(out);
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
    appendUs = micros() - rangePhaseUs;
    rangePhaseUs = micros();
#endif
    std::sort(out.begin(), out.end(), tickLess);
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
    sortUs = micros() - rangePhaseUs;
    rangePhaseUs = micros();
#endif
  } else {
    // Active Note edits are pass-layer scoped. Apply each committed list, then
    // merge. Merged-then-apply lets LIFO Delete pair a hidden On with another
    // pass's Off (144743 48@616 / Off@1016).
    for (const CommittedChunkIdList* list : lists) {
      if (list == nullptr || list->empty()) {
        continue;
      }
      SessionMidiEventVec layer;
      CommittedEventRange::inWindow(&list, 1, loopLengthTicks, windowStart, windowLength)
          .appendTo(layer);
      std::sort(layer.begin(), layer.end(), tickLess);
      applyNoteEditPassSequence(layer, activeRows, loopLengthTicks);
      mergeSortedLoopCaptureLayers(out, std::move(layer));
    }
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
    appendUs = micros() - rangePhaseUs;
    rangePhaseUs = micros();
    editUs = micros() - rangePhaseUs;
#endif
  }
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
  {
    if (activeRows.empty()) {
      editUs = micros() - rangePhaseUs;
    }
    const uint32_t rangeUs = micros() - rangeStartUs;
    if (rangeUs >= kRangeReportUs) {
      char line[224];
      snprintf(line, sizeof(line),
               LOOP_DIAG_FMT("#CAP,%lu,DIAG,cer_range,us=%lu,collect=%lu,append=%lu,sort=%lu,"
                             "edit=%lu,lists=%u,rows=%u,evout=%u"),
               static_cast<unsigned long>(micros()), static_cast<unsigned long>(rangeUs),
               static_cast<unsigned long>(collectUs), static_cast<unsigned long>(appendUs),
               static_cast<unsigned long>(sortUs), static_cast<unsigned long>(editUs),
               static_cast<unsigned>(lists.size()), static_cast<unsigned>(editRowCount),
               static_cast<unsigned>(out.size()));
      DebugSessionCapture::appendCaptureTextLine(line);

      // Composition of the replayed population. planContiguousCollapsedRows only merges
      // contiguous Pitch/Velocity updates, so this separates what it can fold from what it
      // cannot (overdub Hide/Shorten companions at editPassIndex 255).
      uint32_t activeRowsTotal = 0;
      uint32_t disabledRows = 0;
      uint32_t companionRows = 0;
      uint32_t noteEditRows = 0;
      uint32_t deleteRows = 0;
      uint32_t createRows = 0;
      uint32_t updateLengthRows = 0;
      uint32_t updatePitchRows = 0;
      uint32_t updateVelocityRows = 0;
      uint32_t updateRangeRows = 0;
      for (const EditPass& editPass : passes.editPasses) {
        if (editPass.passType != EditPassType::Note) {
          continue;
        }
        if (editPass.state != EditPassState::Active) {
          ++disabledRows;
          continue;
        }
        ++activeRowsTotal;
        if (editPass.editPassIndex == kOverdubCompanionEditPassIndex) {
          ++companionRows;
        } else {
          ++noteEditRows;
        }
        switch (editPass.actionType) {
          case EditActionType::Delete:
            ++deleteRows;
            break;
          case EditActionType::Create:
            ++createRows;
            break;
          case EditActionType::Update:
            switch (editPass.propertyType) {
              case EditPropertyType::Length:
                ++updateLengthRows;
                break;
              case EditPropertyType::Pitch:
                ++updatePitchRows;
                break;
              case EditPropertyType::Velocity:
                ++updateVelocityRows;
                break;
              case EditPropertyType::NoteRange:
                ++updateRangeRows;
                break;
              default:
                break;
            }
            break;
        }
      }
      char rowsLine[256];
      snprintf(rowsLine, sizeof(rowsLine),
               LOOP_DIAG_FMT("#CAP,%lu,DIAG,cer_rows,active=%u,disabled=%u,comp=%u,ne=%u,del=%u,"
                             "crt=%u,ulen=%u,upit=%u,uvel=%u,urng=%u"),
               static_cast<unsigned long>(micros()), static_cast<unsigned>(activeRowsTotal),
               static_cast<unsigned>(disabledRows), static_cast<unsigned>(companionRows),
               static_cast<unsigned>(noteEditRows), static_cast<unsigned>(deleteRows),
               static_cast<unsigned>(createRows), static_cast<unsigned>(updateLengthRows),
               static_cast<unsigned>(updatePitchRows), static_cast<unsigned>(updateVelocityRows),
               static_cast<unsigned>(updateRangeRows));
      DebugSessionCapture::appendCaptureTextLine(rowsLine);
    }
  }
#endif
}

void Loop::rematerializeEditView(LoopEventStore& store) const {
  LoopEventStore::enterEphemeralSeal();
  passes.materialize(store, loopLengthTicks);
  LoopEventStore::leaveEphemeralSeal();
}

SessionMidiEventVec& Loop::midiEvents() {
  materializeEditViewFromPasses();
  return passesMaterializedStore_.mutEvents();
}

const SessionMidiEventVec& Loop::midiEvents() const {
  materializeEditViewFromPasses();
  return passesMaterializedStore_.readEvents();
}

void Loop::discardPassesMaterializedCache() {
  passesMaterializedStore_.mutStore().clear();
  passesMaterializedStore_.discardEventsCache();
  passesMaterializedStoreStale_ = true;
}

LOOP_COLD_MEM bool Loop::tryDiscardPassesMaterializedCache() {
  if (!passesMaterializedStoreStale_) {
    return false;
  }
  if (captureActive()) {
    return false;
  }
  const bool hadStore = !passesMaterializedStore_.empty();
  discardPassesMaterializedCache();
  return hadStore;
}

void Loop::mergeMaterializedPassesWithCapture(MidiEventVec& out) const {
  gatherCommittedEventsWithCapture(out);
}

void Loop::mergeMaterializedPassesWithCapture(SessionMidiEventVec& out) const {
  gatherCommittedEventsWithCapture(out);
}

LOOP_COLD_MEM void Loop::gatherCommittedEvents(SessionMidiEventVec& out) const {
  copyEffectiveCommittedEvents(out);
}

LOOP_COLD_MEM void Loop::resetCommittedPitchQueryWork() {
  g_committedPitchQueryWork = CommittedPitchQueryWork{};
}

LOOP_COLD_MEM uint32_t Loop::committedEventsFullMaterializeCount() {
  return g_committedPitchQueryWork.fullMaterializeCount;
}

LOOP_COLD_MEM void Loop::gatherCommittedEvents(MidiEventVec& out) const {
  SessionMidiEventVec extmemFlat;
  gatherCommittedEvents(extmemFlat);
  out.assign(extmemFlat.begin(), extmemFlat.end());
}

LOOP_COLD_MEM void Loop::gatherCommittedEventsInWindow(SessionMidiEventVec& out, uint32_t windowStart,
                                                       uint32_t windowLength,
                                                       bool includeActiveEditRows) const {
  copyEffectiveCommittedEventsInRange(out, windowStart, windowLength, includeActiveEditRows);
}

LOOP_COLD_MEM void Loop::gatherCommittedEventsInWindow(MidiEventVec& out, uint32_t windowStart,
                                                       uint32_t windowLength,
                                                       bool includeActiveEditRows) const {
  SessionMidiEventVec extmemFlat;
  gatherCommittedEventsInWindow(extmemFlat, windowStart, windowLength, includeActiveEditRows);
  out.assign(extmemFlat.begin(), extmemFlat.end());
}

LOOP_COLD_MEM void Loop::gatherCommittedEventsInWindowWithCapture(SessionMidiEventVec& out,
                                                                  uint32_t windowStart,
                                                                  uint32_t windowLength) const {
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
  // The capture tail is unwindowed: copyEventsTo copies the whole capture store before the
  // window filter. Split it so `gather` is fully attributed.
  constexpr uint32_t kCaptureTailReportUs = 10000u;
  const uint32_t tailStartUs = micros();
  uint32_t tailPhaseUs = tailStartUs;
  uint32_t committedUs = 0;
  uint32_t sortUs = 0;
  uint32_t copyUs = 0;
  uint32_t filterUs = 0;
  uint32_t mergeUs = 0;
  uint32_t captureFlatCount = 0;
  uint32_t captureWindowCount = 0;
  const auto emitCaptureTailDiag = [&]() {
    const uint32_t tailUs = micros() - tailStartUs;
    if (tailUs < kCaptureTailReportUs) {
      return;
    }
    char line[256];
    snprintf(line, sizeof(line),
             LOOP_DIAG_FMT("#CAP,%lu,DIAG,cer_cap,us=%lu,committed=%lu,sort=%lu,copy=%lu,"
                           "filter=%lu,merge=%lu,capflat=%u,capwin=%u,evout=%u"),
             static_cast<unsigned long>(micros()), static_cast<unsigned long>(tailUs),
             static_cast<unsigned long>(committedUs), static_cast<unsigned long>(sortUs),
             static_cast<unsigned long>(copyUs), static_cast<unsigned long>(filterUs),
             static_cast<unsigned long>(mergeUs), static_cast<unsigned>(captureFlatCount),
             static_cast<unsigned>(captureWindowCount), static_cast<unsigned>(out.size()));
    DebugSessionCapture::appendCaptureTextLine(line);
  };
#define LOOP_EMIT_CAPTURE_TAIL() emitCaptureTailDiag()
#else
#define LOOP_EMIT_CAPTURE_TAIL() \
  do {                           \
  } while (0)
#endif
  gatherCommittedEventsInWindow(out, windowStart, windowLength);
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
  committedUs = micros() - tailPhaseUs;
  tailPhaseUs = micros();
#endif
  if (!captureActive() || capture.store.empty()) {
    LOOP_EMIT_CAPTURE_TAIL();
    return;
  }
  SessionMidiEventVec captureFlat;
  const_cast<Loop*>(this)->ensureCaptureEventsSorted();
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
  sortUs = micros() - tailPhaseUs;
  tailPhaseUs = micros();
#endif
  capture.store.copyEventsTo(captureFlat);
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
  copyUs = micros() - tailPhaseUs;
  captureFlatCount = static_cast<uint32_t>(captureFlat.size());
  tailPhaseUs = micros();
#endif
  SessionMidiEventVec captureWindow;
  DisplayWindowUtils::filterMidiEventsToWindow(captureFlat, captureWindow, windowStart, windowLength,
                                               loopLengthTicks);
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
  filterUs = micros() - tailPhaseUs;
  captureWindowCount = static_cast<uint32_t>(captureWindow.size());
  tailPhaseUs = micros();
#endif
  if (captureWindow.empty()) {
    LOOP_EMIT_CAPTURE_TAIL();
    return;
  }
  if (out.empty()) {
    out = std::move(captureWindow);
    LOOP_EMIT_CAPTURE_TAIL();
    return;
  }
  SessionMidiEventVec merged;
  merged.reserve(out.size() + captureWindow.size());
  std::merge(out.begin(), out.end(), captureWindow.begin(), captureWindow.end(),
             std::back_inserter(merged),
             [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
  out = std::move(merged);
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
  mergeUs = micros() - tailPhaseUs;
#endif
  LOOP_EMIT_CAPTURE_TAIL();
}

#undef LOOP_EMIT_CAPTURE_TAIL

LOOP_COLD_MEM bool Loop::tryGatherPublishedSnapshotEventsInWindowWithCapture(
    SessionMidiEventVec& out, uint32_t windowStart, uint32_t windowLength,
    SnapshotWindowGatherPath* pathOut) const {
  if (!tryGatherPublishedSnapshotEventsInWindow(out, windowStart, windowLength, pathOut)) {
    return false;
  }
  mergeCaptureStoreIntoWindowedEvents(*this, out, windowStart, windowLength);
  return true;
}

LOOP_COLD_MEM bool Loop::shouldAvoidFullVisualRebuild(uint32_t loopLength) const {
  const uint32_t boundedThreshold =
      DisplayWindowUtils::kMaxDetailedWindowBars * Config::TICKS_PER_BAR;
  return loopLength > boundedThreshold;
}

LOOP_COLD_MEM void Loop::gatherCommittedEventsForDerivedView(SessionMidiEventVec& flat) const {
  gatherCommittedEvents(flat);
}

LOOP_COLD_MEM void Loop::gatherCommittedEventsForDerivedView(MidiEventVec& flat) const {
  gatherCommittedEvents(flat);
}

LOOP_COLD_MEM void Loop::gatherCommittedEventsWithCapture(SessionMidiEventVec& flat) const {
  gatherCommittedEvents(flat);
  mergeCaptureStoreIntoMaterializedEvents(*this, flat);
}

LOOP_COLD_MEM void Loop::gatherCommittedEventsWithCapture(MidiEventVec& flat) const {
  gatherCommittedEvents(flat);
  mergeCaptureStoreIntoMaterializedEvents(*this, flat);
}
