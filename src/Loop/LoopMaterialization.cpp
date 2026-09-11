//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"

#include "CommittedEventRange.h"
#include "EditApply.h"
#include "Globals.h"
#include "LoopContentResolution.h"
#include "LoopInternal.h"
#include "Utils/Diagnostics.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/LoopMem.h"
#include "Utils/NoteUtils.h"
#include "Utils/SnapshotNoteConversion.h"

#include <algorithm>
#include <vector>

#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_INVESTIGATION_DIAG
#include "Utils/InvestigationCaptureLog.h"
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

LOOP_COLD_MEM void Loop::copyCommittedPassReplayEvents(SessionMidiEventVec& out) const {
  if (loopLengthTicks == 0) {
    out.clear();
    return;
  }
  passes.materializeToEventVector(out, loopLengthTicks);
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
    InvestigationCaptureLog::CommittedEventRangeDiagTiming rangeTiming{};
    rangeTiming.rangeUs = micros() - rangeStartUs;
    rangeTiming.collectUs = collectUs;
    rangeTiming.appendUs = appendUs;
    rangeTiming.sortUs = sortUs;
    rangeTiming.editUs = editUs;
    rangeTiming.listCount = static_cast<uint32_t>(lists.size());
    rangeTiming.editRowCount = editRowCount;
    rangeTiming.outCount = static_cast<uint32_t>(out.size());
    InvestigationCaptureLog::maybeLogCommittedEventRangeDiag(rangeTiming, passes, kRangeReportUs);
  }
#endif
}

namespace {

LOOP_COLD_MEM bool snapshotSpanHasNoteId(const NoteUtils::DisplayNoteVec& spanNotes,
                                         NoteId noteId) {
  if (noteId == kInvalidNoteId) {
    return false;
  }
  for (const NoteUtils::DisplayNote& note : spanNotes) {
    if (note.noteId == noteId) {
      return true;
    }
  }
  return false;
}

LOOP_COLD_MEM void applySessionEditPassesToEvents(const Loop& loop,
                                                 const EditPassIdList& sessionEditPassIds,
                                                 SessionMidiEventVec& events) {
  EditPassVec sessionRows;
  for (const EditPassId id : sessionEditPassIds) {
    for (const EditPass& row : loop.passes.editPasses) {
      if (row.id == id && row.state == EditPassState::Active) {
        sessionRows.push_back(row);
        break;
      }
    }
  }
  if (!sessionRows.empty()) {
    applyNoteEditPassSequence(events, sessionRows, loop.loopLengthTicks);
  }
}

LOOP_COLD_MEM bool copyEditViewEventsFromSnapshot(const LoopSnapshot& snapshot,
                                                  SessionMidiEventVec& out) {
  out.clear();
  NoteUtils::DisplayNoteVec spanNotes;
  LoopContentResolution::resolveDisplayNotes(snapshot, spanNotes);
  SessionMidiEventVec events;
  LoopContentResolution::resolveWindow(snapshot, 0, snapshot.loopLengthTicks(), events);
  for (const MidiEvent& evt : events) {
    if (!evt.isNoteOn() && !evt.isNoteOff()) {
      out.push_back(evt);
      continue;
    }
    if (snapshotSpanHasNoteId(spanNotes, evt.noteId)) {
      out.push_back(evt);
    }
  }
  return true;
}

LOOP_COLD_MEM bool copyPublishedSnapshotEditViewEvents(const Loop& loop, SessionMidiEventVec& out) {
  const ImmutableLoopSnapshotRef snapshot = loop.publishedSnapshot();
  if (!snapshot) {
    out.clear();
    return false;
  }
  return copyEditViewEventsFromSnapshot(*snapshot, out);
}

LOOP_COLD_MEM bool shouldUsePublishedSnapshotForNoteEditCommit(
    const Loop& loop, bool openedOnCurrentSnapshot, uint32_t openedSnapshotRevision) {
  if (!loop.publishedSnapshot()) {
    return false;
  }
  if (openedOnCurrentSnapshot &&
      loop.publishedSnapshotRevision() == openedSnapshotRevision) {
    return true;
  }
  return loop.publishedSnapshotIsCurrent();
}

}  // namespace

LOOP_COLD_MEM bool Loop::tryCopyCurrentSnapshotEditViewEvents(SessionMidiEventVec& out) const {
  const void* unusedIdentity = nullptr;
  return tryCopyCurrentSnapshotEditViewEvents(out, unusedIdentity);
}

LOOP_COLD_MEM bool Loop::tryCopyCurrentSnapshotEditViewEvents(
    SessionMidiEventVec& out, const void*& observedIdentity) const {
  observedIdentity = nullptr;
  out.clear();
  if (!publishedSnapshotIsCurrent()) {
    return false;
  }
  const ImmutableLoopSnapshotRef observed = publishedSnapshot();
  if (!observed) {
    return false;
  }
  if (!copyEditViewEventsFromSnapshot(*observed, out)) {
    return false;
  }
  observedIdentity = observed.get();
  return true;
}

LOOP_COLD_MEM bool Loop::tryCopyCurrentSnapshotDisplayNotesInWindow(
    NoteUtils::DisplayNoteVec& out, uint32_t windowStart, uint32_t windowLength) const {
  out.clear();
  if (!publishedSnapshotIsCurrent() || loopLengthTicks == 0 || windowLength == 0) {
    return false;
  }
  const ImmutableLoopSnapshotRef snapshot = publishedSnapshot();
  if (!snapshot) {
    return false;
  }
  if (windowLength >= loopLengthTicks) {
    windowStart = 0;
    windowLength = loopLengthTicks;
  }
  const uint32_t barTicks = Config::TICKS_PER_BAR;
  const uint32_t barCount =
      (loopLengthTicks + barTicks - 1u) / barTicks;
  if (barCount == 0) {
    return true;
  }
  if (windowLength >= loopLengthTicks) {
    appendSnapshotNotesForBarRange(*snapshot, 0, barCount - 1u, out);
  } else if (windowStart + windowLength > loopLengthTicks) {
    const uint32_t firstBar = windowStart / barTicks;
    appendSnapshotNotesForBarRange(*snapshot, firstBar, barCount - 1u, out);
    const uint32_t wrappedEnd = windowStart + windowLength - loopLengthTicks;
    if (wrappedEnd > 0) {
      const uint32_t lastHeadBar = (wrappedEnd - 1u) / barTicks;
      appendSnapshotNotesForBarRange(*snapshot, 0, lastHeadBar, out);
    }
  } else {
    const uint32_t firstBar = windowStart / barTicks;
    const uint32_t lastBar = (windowStart + windowLength - 1u) / barTicks;
    appendSnapshotNotesForBarRange(*snapshot, firstBar, lastBar, out);
  }
  const uint32_t windowEnd = windowStart + windowLength;
  out.erase(std::remove_if(out.begin(), out.end(),
                           [windowStart, windowEnd](const NoteUtils::DisplayNote& note) {
                             return note.endTick < windowStart || note.startTick >= windowEnd;
                           }),
            out.end());
  return true;
}

LOOP_COLD_MEM void Loop::copyIdleValidateEvents(MidiEventVec& out) const {
  SessionMidiEventVec sessionEvents;
  if (tryCopyCurrentSnapshotEditViewEvents(sessionEvents)) {
    out.assign(sessionEvents.begin(), sessionEvents.end());
    return;
  }
  mergeActiveCapturePasses(out);
}

LOOP_COLD_MEM bool Loop::tryCopyNoteEditOpenedSnapshotEvents(bool openedOnCurrentSnapshot,
                                                            uint32_t openedSnapshotRevision,
                                                            SessionMidiEventVec& out) const {
  if (!shouldUsePublishedSnapshotForNoteEditCommit(*this, openedOnCurrentSnapshot,
                                                   openedSnapshotRevision)) {
    out.clear();
    return false;
  }
  return copyPublishedSnapshotEditViewEvents(*this, out);
}

LOOP_COLD_MEM bool Loop::tryCopyNoteEditOpenedSnapshotEvents(bool openedOnCurrentSnapshot,
                                                            uint32_t openedSnapshotRevision,
                                                            MidiEventVec& out) const {
  SessionMidiEventVec sessionEvents;
  if (!tryCopyNoteEditOpenedSnapshotEvents(openedOnCurrentSnapshot, openedSnapshotRevision,
                                           sessionEvents)) {
    out.clear();
    return false;
  }
  out.assign(sessionEvents.begin(), sessionEvents.end());
  return true;
}

LOOP_COLD_MEM void Loop::copyNoteEditCommitBase(const EditPassIdList& sessionEditPassIds,
                                               SessionMidiEventVec& out,
                                               bool openedOnCurrentSnapshot,
                                               uint32_t openedSnapshotRevision) const {
  if (shouldUsePublishedSnapshotForNoteEditCommit(*this, openedOnCurrentSnapshot,
                                                  openedSnapshotRevision) &&
      copyPublishedSnapshotEditViewEvents(*this, out)) {
    applySessionEditPassesToEvents(*this, sessionEditPassIds, out);
    return;
  }
  passes.materializeToEventVector(out, loopLengthTicks);
}

LOOP_COLD_MEM void Loop::copyNoteEditCommitBase(const EditPassIdList& sessionEditPassIds,
                                               MidiEventVec& out, bool openedOnCurrentSnapshot,
                                               uint32_t openedSnapshotRevision) const {
  SessionMidiEventVec sessionEvents;
  if (shouldUsePublishedSnapshotForNoteEditCommit(*this, openedOnCurrentSnapshot,
                                                  openedSnapshotRevision) &&
      copyPublishedSnapshotEditViewEvents(*this, sessionEvents)) {
    applySessionEditPassesToEvents(*this, sessionEditPassIds, sessionEvents);
    out.assign(sessionEvents.begin(), sessionEvents.end());
    return;
  }
  passes.materializeToEventVector(out, loopLengthTicks);
}

LOOP_COLD_MEM void Loop::rematerializeEditView(LoopEventStore& store) const {
  LoopEventStore::enterEphemeralSeal();
  SessionMidiEventVec snapshotEvents;
  if (tryCopyCurrentSnapshotEditViewEvents(snapshotEvents)) {
    store.clear();
    if (!snapshotEvents.empty()) {
      store.loadFromEvents(snapshotEvents);
    }
  } else {
    passes.materialize(store, loopLengthTicks);
  }
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
#define LOOP_EMIT_CAPTURE_TAIL()                                                                  \
  do {                                                                                            \
    InvestigationCaptureLog::CaptureTailDiagTiming tailTiming{};                                  \
    tailTiming.tailUs = micros() - tailStartUs;                                                   \
    tailTiming.committedUs = committedUs;                                                           \
    tailTiming.sortUs = sortUs;                                                                   \
    tailTiming.copyUs = copyUs;                                                                   \
    tailTiming.filterUs = filterUs;                                                               \
    tailTiming.mergeUs = mergeUs;                                                                 \
    tailTiming.captureFlatCount = captureFlatCount;                                               \
    tailTiming.captureWindowCount = captureWindowCount;                                             \
    tailTiming.outCount = static_cast<uint32_t>(out.size());                                      \
    InvestigationCaptureLog::maybeLogCaptureTailDiag(tailTiming, kCaptureTailReportUs);           \
  } while (0)
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
