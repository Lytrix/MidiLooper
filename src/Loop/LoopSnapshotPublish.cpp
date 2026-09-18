//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"

#include "CommittedEventRange.h"
#include "LoopSnapshotBoxes.h"
#include "Utils/LoopMem.h"
#include "Utils/MemoryMonitor.h"

#include <utility>
#include <vector>

#if defined(SESSION_CAPTURE) && defined(ARDUINO)
#include "Utils/DebugSessionCapture.h"
#include "Utils/InvestigationCaptureLog.h"

#include <cstdio>
#endif

// Snapshot idle/unit instrumentation is deliberately gated on the broad `SESSION_CAPTURE` flag, not
// on `SESSION_CAPTURE_SNAPSHOT_IDLE`: the probes must be available in any capture build without
// changing the env of a running session, and their volume is bounded by the step-report interval and
// the phase/stall/signature conditions below. DEC-076 records the decision; the reader stays narrow.
#if defined(SESSION_CAPTURE)
#include <cstdint>

struct SnapshotIdleChildTimes {
  uint32_t startUs = 0;
  uint32_t passChangeUs = 0;
  uint32_t stepUs = 0;
  uint32_t finishUs = 0;
  uint32_t publishUs = 0;
  uint32_t probeUs = 0;
  uint32_t enqueueUs = 0;
  uint32_t discardUs = 0;
  uint32_t emitUs = 0;
  uint32_t refillUs = 0;
  uint32_t eventScanUs = 0;
  uint32_t noteLookupUs = 0;
  uint32_t changeBuildUs = 0;
  uint32_t constructUs = 0;
};

static SnapshotIdleChildTimes s_lastSnapshotIdleChildren;

struct SnapshotIdleChildAdd {
  uint32_t& field;
  uint32_t startUs;
  explicit SnapshotIdleChildAdd(uint32_t& target) : field(target), startUs(micros()) {}
  ~SnapshotIdleChildAdd() { field += micros() - startUs; }
};

static void resetSnapshotIdleChildTimes() {
  s_lastSnapshotIdleChildren = SnapshotIdleChildTimes{};
  const uint32_t probeA = micros();
  const uint32_t probeB = micros();
  s_lastSnapshotIdleChildren.probeUs = probeB - probeA;
}
#endif

#if defined(SESSION_CAPTURE) && defined(ARDUINO)
LOOP_COLD_MEM __attribute__((noinline)) void emitSnapshotUnitCapture(
    const Loop& loop, const char* kind, uint32_t heapBefore) {
  const uint32_t heapAfter = MemoryMonitor::getInternalHeapFreeBytes();
  char line[192];
  snprintf(line, sizeof(line),
           "#CAP,%lu,DIAG,snapshot_unit,kind=%s,ph=%u,ld=%u,p=%u,q=%lu,a=%lu,"
           "if=%u,hb=%lu,ha=%lu,psram=%lu,n=%lu,b=%lu,s=%lu",
           static_cast<unsigned long>(micros()), kind != nullptr ? kind : "none",
           static_cast<unsigned>(loop.loadedSnapshotHydrationPhase()),
           loop.pendingSnapshotFromLoadedPasses() ? 1u : 0u,
           loop.hasPendingSnapshotPublish() ? 1u : 0u,
           static_cast<unsigned long>(loop.pendingSnapshotQueuedPassCount()),
           static_cast<unsigned long>(loop.pendingSnapshotAppliedPassCount()),
           loop.hasInFlightSnapshotConstruction() ? 1u : 0u,
           static_cast<unsigned long>(heapBefore),
           static_cast<unsigned long>(heapAfter),
           static_cast<unsigned long>(DebugSessionCapture::kUnsampledHeapBytes),
           static_cast<unsigned long>(snapshotAllocNodeCount()),
           static_cast<unsigned long>(snapshotAllocByteCount()),
           static_cast<unsigned long>(snapshotAllocSlabCount()));
  DebugSessionCapture::appendCaptureTextLine(line);
}

// `step` dominates snapshot_unit volume (`033502`: 4,991 of 5,134 lines) while
// start / finish / publish are the rare decisive events. Report steps on an interval
// so a construction that stops advancing is still visible without one line per unit.
constexpr uint32_t kSnapshotUnitStepReportIntervalMs = 25u;

LOOP_COLD_MEM __attribute__((noinline)) void emitSnapshotUnitStepCapture(const Loop& loop,
                                                                        uint32_t heapBefore) {
  static uint32_t sLastStepReportMs = 0;
  const uint32_t nowMs = millis();
  if (sLastStepReportMs != 0 && (nowMs - sLastStepReportMs) < kSnapshotUnitStepReportIntervalMs) {
    return;
  }
  sLastStepReportMs = nowMs;
  emitSnapshotUnitCapture(loop, "step", heapBefore);
}
#endif

namespace {

bool isRecordSessionExit(CommitReason reason) {
  return reason == CommitReason::RecordStop ||
         reason == CommitReason::RecordStopToStopped;
}

bool isOverdubSessionExit(CommitReason reason) {
  return reason == CommitReason::OverdubStop ||
         reason == CommitReason::OverdubStopToStopped;
}

bool isSessionExit(CommitReason reason) {
  return isRecordSessionExit(reason) || isOverdubSessionExit(reason);
}

const CommittedChunkIdList* committedChunksForPass(const Loop& loop, PassId passId,
                                                   bool& isOverdubPass) {
  isOverdubPass = false;
  if (passId == kInvalidPassId) {
    return nullptr;
  }
  if (loop.passes.hasRecordPass() && loop.passes.recordPass.id == passId) {
    return &loop.passes.recordPass.committedChunkIds;
  }
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    if (pass.id == passId) {
      isOverdubPass = true;
      return &pass.committedChunkIds;
    }
  }
  return nullptr;
}

const EditPass* findActiveEditPass(const Loop& loop, EditPassId id) {
  for (const EditPass& row : loop.passes.editPasses) {
    if (row.id == id && row.state == EditPassState::Active) {
      return &row;
    }
  }
  return nullptr;
}

LOOP_COLD_MEM bool appendEditRowChange(const LoopSnapshot& parent, const EditPass& row,
                                       LoopId loopId, LoopSnapshotChangeSet& changes) {
  if (row.passType != EditPassType::Note) {
    return true;
  }
  if (row.actionType == EditActionType::Delete) {
    if (row.targetNoteId == kInvalidNoteId) {
      return false;
    }
    SnapshotNoteChange change{};
    change.kind = SnapshotChangeKind::Remove;
    change.next.loopId = loopId;
    change.next.noteId = row.targetNoteId;
    change.next.pitch = row.pitch;
    change.next.startTick = row.startTick;
    change.next.endTick = row.endTick;
    change.previous = parent.findNote(row.targetNoteId, row.pitch);
    changes.notes.push_back(change);
    return true;
  }

  LoopSnapshotNote next{};
  next.loopId = loopId;
  next.noteId = row.targetNoteId;
  next.pitch = row.pitch;
  next.velocity = row.velocity;
  next.startTick = row.startTick;
  next.endTick = row.endTick;

  const LoopSnapshotNoteRef previous = parent.findNote(row.targetNoteId, row.pitch);
  if (row.actionType == EditActionType::Create && !previous) {
    if (next.noteId == kInvalidNoteId) {
      return false;
    }
    SnapshotNoteChange change{};
    change.kind = SnapshotChangeKind::Add;
    change.next = next;
    changes.notes.push_back(change);
    return true;
  }
  if (row.targetNoteId == kInvalidNoteId) {
    return false;
  }
  const SnapshotNoteChange* earlierSameNote = nullptr;
  for (const SnapshotNoteChange& existing : changes.notes) {
    if (existing.next.noteId != row.targetNoteId ||
        row.targetNoteId == kInvalidNoteId) {
      continue;
    }
    if (existing.kind == SnapshotChangeKind::Replace ||
        existing.kind == SnapshotChangeKind::Add) {
      earlierSameNote = &existing;
    }
  }

  SnapshotNoteChange change{};
  change.kind = SnapshotChangeKind::Replace;
  if (earlierSameNote != nullptr) {
    // Prior row in this close set already replaced the note (Pitch then
    // NoteRange). Construction finds that candidate; do not restart from parent.
    change.next = earlierSameNote->next;
  } else {
    change.previous = previous;
    if (previous) {
      change.next = *previous;
    }
  }
  change.next.loopId = loopId;
  change.next.noteId = row.targetNoteId;
  // NoteRange/Length move ticks only (`applyMoveNoteById`). row.pitch is
  // unset (0) on those rows — do not publish MIDI pitch 0 (162207 close).
  if (row.propertyType == EditPropertyType::Pitch ||
      row.propertyType == EditPropertyType::None ||
      row.actionType == EditActionType::Create) {
    change.next.pitch = row.pitch;
  }
  if (row.propertyType == EditPropertyType::Length ||
      row.propertyType == EditPropertyType::NoteRange ||
      row.propertyType == EditPropertyType::Tick ||
      row.propertyType == EditPropertyType::None ||
      row.actionType == EditActionType::Create) {
    change.next.startTick = row.startTick;
    change.next.endTick = row.endTick;
  }
  if (row.velocity != 0 &&
      (row.propertyType == EditPropertyType::Velocity ||
       row.propertyType == EditPropertyType::None ||
       row.actionType == EditActionType::Create)) {
    change.next.velocity = row.velocity;
  }
  changes.notes.push_back(change);
  return true;
}

void appendCompanionEventChanges(const LoopSnapshot& parent, LoopId loopId,
                                 LoopSnapshotChangeSet& changes);

LOOP_COLD_MEM void appendCompanionChanges(const Loop& loop, const LoopSnapshot& parent,
                                          const EditPassIdList& companionIds,
                                          LoopSnapshotChangeSet& changes) {
  for (const EditPassId id : companionIds) {
    const EditPass* row = findActiveEditPass(loop, id);
    if (row == nullptr) {
      continue;
    }
    appendEditRowChange(parent, *row, loop.loopId, changes);
  }
  appendCompanionEventChanges(parent, loop.loopId, changes);
}

bool midiEventMatchesSnapshot(const MidiEvent& existing, const MidiEvent& incoming) {
  if (existing.tick != incoming.tick || existing.type != incoming.type ||
      existing.channel != incoming.channel) {
    return false;
  }
  // Snapshot membership must not collapse a new controller value (or another
  // controller) merely because an older pass has a CC at the same tick.
  if (incoming.type == midi::ControlChange) {
    return existing.data.ccData.cc == incoming.data.ccData.cc &&
           existing.data.ccData.value == incoming.data.ccData.value;
  }
  if ((incoming.isNoteOn() || incoming.isNoteOff()) &&
      existing.data.noteData.note != incoming.data.noteData.note) {
    return false;
  }
  if (incoming.noteId != kInvalidNoteId && existing.noteId != kInvalidNoteId &&
      incoming.noteId != existing.noteId) {
    return false;
  }
  return true;
}

bool eventAlreadyInSnapshot(const LoopSnapshot& snapshot, const MidiEvent& event) {
  if (snapshot.loopLengthTicks() == 0 || event.tick >= snapshot.loopLengthTicks()) {
    return false;
  }
  const uint32_t barIndex = event.tick / Config::TICKS_PER_BAR;
  const auto membership = snapshot.eventMembership(barIndex);
  if (!membership) {
    return false;
  }
  bool found = false;
  forEachSnapshotMembershipReference(
      membership.get(), [&found, &event](const LoopSnapshotEventRef& ref) {
        if (!found && ref && midiEventMatchesSnapshot(ref->value, event)) {
          found = true;
        }
      });
  if (found) {
    return true;
  }
  return false;
}

bool isCommittedOverdubPass(const Loop& loop, PassId passId) {
  bool isOverdubPass = false;
  return committedChunksForPass(loop, passId, isOverdubPass) != nullptr && isOverdubPass;
}

bool passAlreadyTracked(const PendingSnapshotPublishBox& box, PassId passId) {
  return box.constructionPassId == passId || containsId(box.queuedPassIds, passId) ||
         containsId(box.appliedPassIds, passId) || containsId(box.journaledPassIds, passId) ||
         (box.passChangeProgress.active && box.passChangeProgress.passId == passId);
}

uint32_t estimateNoteAffectedBars(const LoopSnapshotNote& note, uint32_t loopLengthTicks) {
  if (loopLengthTicks == 0) {
    return 0;
  }
  if (note.isWrap(loopLengthTicks)) {
    const uint32_t tailBars =
        note.startTick < loopLengthTicks
            ? ((loopLengthTicks - 1 - note.startTick) / Config::TICKS_PER_BAR) + 1
            : 0;
    const uint32_t headBars =
        note.endTick == 0 ? 0 : ((note.endTick - 1) / Config::TICKS_PER_BAR) + 1;
    return tailBars + headBars;
  }
  if (note.endTick <= note.startTick) {
    return 0;
  }
  return ((note.endTick - 1) / Config::TICKS_PER_BAR) -
         (note.startTick / Config::TICKS_PER_BAR) + 1;
}

size_t journalChangeCount(const LoopSnapshotChangeSet& changes) {
  return changes.notes.size() + changes.events.size();
}

size_t journalByteSize(const LoopSnapshotChangeSet& changes) {
  return changes.notes.size() * sizeof(SnapshotNoteChange) +
         changes.events.size() * sizeof(SnapshotEventChange);
}

bool journalWouldExceedCaps(const LoopSnapshotChangeSet& changes, uint32_t affectedBars,
                            uint32_t addedBars, size_t addedChanges) {
  return journalChangeCount(changes) + addedChanges > kSnapshotWrapJournalMaxChanges ||
         affectedBars + addedBars > kSnapshotWrapJournalMaxAffectedBars ||
         journalByteSize(changes) + addedChanges * sizeof(SnapshotNoteChange) >
             kSnapshotWrapJournalMaxBytes;
}

LoopSnapshotEventRef findPairedNoteEvent(const LoopSnapshot& snapshot, NoteId noteId,
                                         uint32_t tick, bool wantNoteOn) {
  if (noteId == kInvalidNoteId || snapshot.loopLengthTicks() == 0 ||
      tick >= snapshot.loopLengthTicks()) {
    return {};
  }
  const auto membership = snapshot.eventMembership(tick / Config::TICKS_PER_BAR);
  LoopSnapshotEventRef found;
  forEachSnapshotMembershipReference(
      membership.get(),
      [&found, noteId, wantNoteOn](const LoopSnapshotEventRef& reference) {
        if (found || !reference) {
          return;
        }
        if (reference->value.noteId != noteId) {
          return;
        }
        if (wantNoteOn ? reference->value.isNoteOn() : reference->value.isNoteOff()) {
          found = reference;
        }
      });
  return found;
}

void appendPairedNoteEventChanges(const LoopSnapshot& parent, const PendingNoteChange& change,
                                  LoopId loopId, uint32_t loopLengthTicks, EventId& nextEventId,
                                  uint32_t& nextCanonicalOrder, LoopSnapshotChangeSet& changes) {
  if (change.noteId == kInvalidNoteId || loopLengthTicks == 0) {
    return;
  }
  const bool isHide = change.kind == PendingNoteChangeKind::Hide;
  const bool isReplace = change.kind == PendingNoteChangeKind::Shorten;
  const LoopSnapshotNoteRef previousNote = parent.findNote(change.noteId, change.pitch);
  const uint32_t previousStart = previousNote ? previousNote->startTick : change.startTick;
  const uint32_t previousEnd = previousNote ? previousNote->endTick : change.endTick;

  auto makeNoteEvent = [loopId, &nextEventId, &nextCanonicalOrder](
                           uint32_t tick, uint8_t channel, uint8_t pitch, uint8_t velocity,
                           NoteId noteId, bool noteOn) {
    SnapshotEventChange eventChange{};
    eventChange.next.loopId = loopId;
    eventChange.next.eventId = nextEventId++;
    eventChange.next.canonicalOrder = nextCanonicalOrder++;
    eventChange.next.value =
        noteOn ? MidiEvent::NoteOn(tick, channel, pitch, velocity)
               : MidiEvent::NoteOff(tick, channel, pitch, 0);
    eventChange.next.value.noteId = noteId;
    return eventChange;
  };

  if (isHide || isReplace) {
    if (const auto previousOn =
            findPairedNoteEvent(parent, change.noteId, previousStart, true)) {
      SnapshotEventChange eventChange{};
      eventChange.kind = SnapshotChangeKind::Remove;
      eventChange.previous = previousOn;
      changes.events.push_back(eventChange);
    }
    const uint32_t previousOffTick =
        previousEnd < loopLengthTicks ? previousEnd : loopLengthTicks - 1;
    if (const auto previousOff =
            findPairedNoteEvent(parent, change.noteId, previousOffTick, false)) {
      SnapshotEventChange eventChange{};
      eventChange.kind = SnapshotChangeKind::Remove;
      eventChange.previous = previousOff;
      changes.events.push_back(eventChange);
    }
  }
  if (!isHide) {
    SnapshotEventChange onChange =
        makeNoteEvent(change.startTick, change.channel, change.pitch, change.velocity,
                      change.noteId, true);
    onChange.kind = SnapshotChangeKind::Add;
    changes.events.push_back(onChange);
    const uint32_t offTick =
        change.endTick < loopLengthTicks ? change.endTick : loopLengthTicks - 1;
    SnapshotEventChange offChange =
        makeNoteEvent(offTick, change.channel, change.pitch, 0, change.noteId, false);
    offChange.kind = SnapshotChangeKind::Add;
    changes.events.push_back(offChange);
  }
}

// Note membership and MIDI-event membership are independent snapshot roots.
// Publish the final companion result to both; emitting note rows alone leaves
// a deleted or shortened note audible once the stale-root overlay disappears.
LOOP_COLD_MEM void appendCompanionEventChanges(const LoopSnapshot& parent, LoopId loopId,
                                               LoopSnapshotChangeSet& changes) {
  EventId nextEventId = parent.nextEventId();
  uint32_t nextCanonicalOrder = parent.nextEventId();
  for (size_t i = 0; i < changes.notes.size(); ++i) {
    const auto& note = changes.notes[i];
    bool superseded = false;
    for (size_t j = i + 1; j < changes.notes.size(); ++j) {
      if (changes.notes[j].next.noteId == note.next.noteId) {
        superseded = true;
        break;
      }
    }
    if (superseded) continue;
    PendingNoteChange eventChange{};
    eventChange.kind = note.kind == SnapshotChangeKind::Remove
                           ? PendingNoteChangeKind::Hide : PendingNoteChangeKind::Shorten;
    eventChange.noteId = note.next.noteId;
    eventChange.pitch = note.next.pitch;
    eventChange.velocity = note.next.velocity;
    eventChange.startTick = note.next.startTick;
    eventChange.endTick = note.next.endTick;
    eventChange.channel = 1;
    const auto previous = parent.findNote(note.next.noteId, note.next.pitch);
    if (!previous && note.kind == SnapshotChangeKind::Replace) {
      bool createdInBatch = false;
      for (size_t j = 0; j < i; ++j) {
        if (changes.notes[j].next.noteId == note.next.noteId &&
            changes.notes[j].kind == SnapshotChangeKind::Add) createdInBatch = true;
      }
      // A stale Modify row cannot invent MIDI events when note construction
      // skips its missing target. A preceding Create in this batch can.
      if (!createdInBatch) continue;
    }
    if (previous) {
      const auto on = findPairedNoteEvent(parent, note.next.noteId, previous->startTick, true);
      if (on) eventChange.channel = on->value.channel;
    }
    appendPairedNoteEventChanges(parent, eventChange, loopId, parent.loopLengthTicks(),
                                nextEventId, nextCanonicalOrder, changes);
  }
}

void appendPendingNoteChange(const LoopSnapshot& parent, const PendingNoteChange& change,
                             LoopId loopId, uint32_t loopLengthTicks, EventId& nextEventId,
                             uint32_t& nextCanonicalOrder, LoopSnapshotChangeSet& changes) {
  if (change.noteId == kInvalidNoteId) {
    return;
  }
  SnapshotNoteChange noteChange{};
  noteChange.next.loopId = loopId;
  noteChange.next.noteId = change.noteId;
  noteChange.next.pitch = change.pitch;
  noteChange.next.velocity = change.velocity;
  noteChange.next.startTick = change.startTick;
  noteChange.next.endTick = change.endTick;
  if (change.kind == PendingNoteChangeKind::Add) {
    noteChange.kind = SnapshotChangeKind::Add;
  } else if (change.kind == PendingNoteChangeKind::Hide) {
    noteChange.kind = SnapshotChangeKind::Remove;
    noteChange.previous = parent.findNote(change.noteId, change.pitch);
  } else {
    noteChange.kind = SnapshotChangeKind::Replace;
    noteChange.previous = parent.findNote(change.noteId, change.pitch);
    if (noteChange.previous) {
      noteChange.next = *noteChange.previous;
      noteChange.next.loopId = loopId;
      noteChange.next.startTick = change.startTick;
      noteChange.next.endTick = change.endTick;
      noteChange.next.velocity =
          change.velocity == 0 ? noteChange.previous->velocity : change.velocity;
    }
  }
  changes.notes.push_back(noteChange);
  appendPairedNoteEventChanges(parent, change, loopId, loopLengthTicks, nextEventId,
                               nextCanonicalOrder, changes);
}

void appendWrapNonNoteEvents(const Loop& loop, const LoopSnapshot& parent, PassId passId,
                             EventId& nextEventId, uint32_t& nextCanonicalOrder,
                             LoopSnapshotChangeSet& changes) {
  bool isOverdubPass = false;
  const CommittedChunkIdList* chunks = committedChunksForPass(loop, passId, isOverdubPass);
  if (chunks == nullptr) {
    return;
  }
  MidiEventVec events;
  LoopEventStore::appendChunkRefEvents(*chunks, events);
  for (const MidiEvent& event : events) {
    if (event.isNoteOn() || event.isNoteOff() || eventAlreadyInSnapshot(parent, event)) {
      continue;
    }
    SnapshotEventChange change{};
    change.kind = SnapshotChangeKind::Add;
    change.next.loopId = loop.loopId;
    change.next.eventId = nextEventId++;
    change.next.canonicalOrder = nextCanonicalOrder++;
    change.next.value = event;
    changes.events.push_back(change);
  }
}

void flushWrapJournalFragment(std::vector<SnapshotWrapJournalFragment>& fragments,
                              ImmutableLoopSnapshotRef parent, PassId wrapPassId,
                              LoopSnapshotChangeSet& current) {
  if (current.notes.empty() && current.events.empty()) {
    return;
  }
  SnapshotWrapJournalFragment fragment;
  fragment.parent = std::move(parent);
  fragment.changes = std::move(current);
  fragment.wrapPassId = wrapPassId;
  fragments.push_back(std::move(fragment));
  current = {};
}

uint32_t liveRecordCompletedOrOneBar(uint32_t liveRecordCompletedTicks) {
  return liveRecordCompletedTicks == 0 ? Config::TICKS_PER_BAR : liveRecordCompletedTicks;
}

uint32_t liveRecordNewParentLength(uint32_t completedTicks, uint32_t loopLengthTicks) {
  // Small headroom beyond the completed frontier; extendLiveRecordCandidateCapacity
  // grows capacity O(1) when the frontier advances (Stage 6C-B).
  constexpr uint32_t kExtraBars = 2;
  const uint32_t withRoom = completedTicks + kExtraBars * Config::TICKS_PER_BAR;
  return loopLengthTicks > withRoom ? loopLengthTicks : withRoom;
}

bool pendingSnapshotFitsSealedLength(const PendingSnapshotPublishBox& box,
                                     uint32_t sealedLength) {
  if (sealedLength == 0) {
    return false;
  }
  if (box.appliedLiveRecordCompletedTicks > sealedLength) {
    return false;
  }
  if (box.appliedLiveRecordCompletedTicks > 0) {
    return true;
  }
  // Mid-window live-record work at session exit may not have persisted completed
  // ticks yet; handoff extends candidate/construction to the sealed length (RC1).
  return box.unpublishedCandidate || box.construction ||
         (box.passChangeProgress.active && box.passChangeProgress.fromCaptureStore);
}

bool handoffPendingSnapshotLength(PendingSnapshotPublishBox& box,
                                  uint32_t sealedLength) {
  if (box.unpublishedCandidate &&
      box.unpublishedCandidate->loopLengthTicks() != sealedLength) {
    ImmutableLoopSnapshotRef handedOff =
        box.unpublishedCandidate->withLoopLengthTicks(sealedLength);
    if (!handedOff) {
      return false;
    }
    box.unpublishedCandidate = std::move(handedOff);
  }
  if (box.construction &&
      box.construction->candidate().loopLengthTicks() != sealedLength) {
    box.construction->handoffLoopLengthTicks(sealedLength);
  }
  return true;
}

/// Stage 6C-A: extend idle parent and in-flight construction when the completed-bar
/// frontier exceeds candidate tick capacity. Reuses root-sharing `withLoopLengthTicks`.
bool extendLiveRecordCandidateCapacity(PendingSnapshotPublishBox& box,
                                       uint32_t completedTicks,
                                       uint32_t loopLengthTicks) {
  if (completedTicks == 0) {
    return false;
  }
  uint32_t currentLength = 0;
  if (box.unpublishedCandidate) {
    currentLength = box.unpublishedCandidate->loopLengthTicks();
  } else if (box.construction) {
    currentLength = box.construction->candidate().loopLengthTicks();
  } else {
    return true;
  }
  const bool constructionShort =
      box.construction &&
      box.construction->candidate().loopLengthTicks() < completedTicks;
  if (currentLength >= completedTicks && !constructionShort) {
    return true;
  }
  const uint32_t targetLength = liveRecordNewParentLength(completedTicks, loopLengthTicks);
  if (targetLength < completedTicks) {
    return false;
  }
  return handoffPendingSnapshotLength(box, targetLength);
}

ImmutableLoopSnapshotRef liveRecordFragmentParent(
    const ImmutableLoopSnapshotRef& resolvedParent,
    const PendingSnapshotPublishBox& box) {
  return box.unpublishedCandidate ? box.unpublishedCandidate : resolvedParent;
}

void discardPendingIfLoopLengthChanged(PendingSnapshotPublishBox& box, uint32_t loopLengthTicks) {
  if (!box.sessionExitQueued) {
    return;
  }
  const bool unpublishedStale = box.unpublishedCandidate &&
                                box.unpublishedCandidate->loopLengthTicks() != loopLengthTicks;
  const bool constructionStale =
      box.construction && box.construction->candidate().loopLengthTicks() != loopLengthTicks;
  if (!unpublishedStale && !constructionStale) {
    return;
  }
  if (pendingSnapshotFitsSealedLength(box, loopLengthTicks) &&
      handoffPendingSnapshotLength(box, loopLengthTicks)) {
    return;
  }
  box.unpublishedCandidate.reset();
  box.construction.reset();
  box.constructionPassId = kInvalidPassId;
  box.passChangeProgress.reset();
  box.appliedLiveRecordEventIndex = 0;
  box.appliedLiveRecordCompletedTicks = 0;
}

PendingPassChangeProgress::OpenNote* findOpenPassNote(PendingPassChangeProgress& progress,
                                                      NoteId noteId) {
  if (noteId == kInvalidNoteId) {
    return nullptr;
  }
  for (uint8_t i = 0; i < progress.openNoteCount; ++i) {
    if (progress.openNotes[i].noteId == noteId) {
      return &progress.openNotes[i];
    }
  }
  return nullptr;
}

PendingPassChangeProgress::OpenNote* addOpenPassNote(PendingPassChangeProgress& progress,
                                                     NoteId noteId) {
  if (noteId == kInvalidNoteId ||
      progress.openNoteCount >= kSnapshotWrapJournalMaxChanges) {
    return nullptr;
  }
  PendingPassChangeProgress::OpenNote& open = progress.openNotes[progress.openNoteCount++];
  open = PendingPassChangeProgress::OpenNote{};
  open.noteId = noteId;
  return &open;
}

void removeOpenPassNote(PendingPassChangeProgress& progress,
                        const PendingPassChangeProgress::OpenNote* open) {
  if (open == nullptr || progress.openNoteCount == 0) {
    return;
  }
  const size_t index = static_cast<size_t>(open - progress.openNotes.data());
  if (index >= progress.openNoteCount) {
    return;
  }
  for (size_t i = index + 1; i < progress.openNoteCount; ++i) {
    progress.openNotes[i - 1] = progress.openNotes[i];
  }
  --progress.openNoteCount;
  progress.openNotes[progress.openNoteCount] = {};
}

void updateOpenPassNoteFromEvent(PendingPassChangeProgress::OpenNote& open,
                                 const MidiEvent& event) {
  if (event.isNoteOn()) {
    open.hasStart = true;
    open.startTick = event.tick;
    open.pitch = event.data.noteData.note;
    open.velocity = event.data.noteData.velocity;
    return;
  }
  if (event.isNoteOff()) {
    open.hasEnd = true;
    open.endTick = event.tick;
    if (!open.hasStart) {
      open.pitch = event.data.noteData.note;
    }
  }
}

bool appendOpenNoteChange(const PendingPassChangeProgress::OpenNote& open, LoopId loopId,
                          LoopSnapshotChangeSet& changes) {
  if (!open.hasStart || !open.hasEnd || open.noteId == kInvalidNoteId) {
    return false;
  }
  SnapshotNoteChange change{};
  change.kind = SnapshotChangeKind::Add;
  change.next.loopId = loopId;
  change.next.noteId = open.noteId;
  change.next.pitch = open.pitch;
  change.next.velocity = open.velocity;
  change.next.startTick = open.startTick;
  change.next.endTick = open.endTick;
  changes.notes.push_back(change);
  return true;
}

LOOP_COLD_MEM void refillCaptureStoreBatch(const Loop& loop,
                                           PendingPassChangeProgress& progress) {
  if (progress.storeWindowComplete) {
    return;
  }
  const LoopEventStore& store = loop.capture.store;
  while (progress.batch.size() < kSnapshotWrapJournalMaxChanges &&
         progress.nextStoreIndex < store.size()) {
    const MidiEvent& event = store.at(progress.nextStoreIndex);
    if (event.tick >= progress.includeUntil) {
      progress.storeWindowComplete = true;
      return;
    }
    progress.batch.push_back(event);
    ++progress.nextStoreIndex;
  }
  if (progress.nextStoreIndex >= store.size()) {
    progress.storeWindowComplete = true;
  }
}

LOOP_COLD_MEM bool handoffLiveRecordProgressToCommittedPass(PendingSnapshotPublishBox& box,
                                                         const LoopSnapshot& parent) {
  PendingPassChangeProgress& progress = box.passChangeProgress;
  if (!box.sessionExitQueued || !progress.active || !progress.fromCaptureStore ||
      box.queuedPassIds.empty()) {
    return false;
  }
  const PassId expectedPassId = box.queuedPassIds.front();
  if (expectedPassId == kInvalidPassId) {
    return false;
  }

  box.persistLiveRecordCursor();
  box.queuedPassIds.erase(box.queuedPassIds.begin());
  progress.fromCaptureStore = false;
  progress.passId = expectedPassId;
  progress.batch.clear();
  progress.batchIndex = 0;
  CommittedEventRange::resetWindowTraversalCursor(progress.cursor);
  progress.storeWindowComplete = false;
  progress.nextStoreIndex = 0;
  progress.includeUntil = 0;
  progress.nextEventId = parent.nextEventId();
  progress.nextCanonicalOrder = parent.nextEventId();
  // Pass identity and the preserved open-note set are re-established by the
  // assignments above, so a post-hoc comparison of them cannot fail; the 128-byte
  // scratch array that fed it is gone.
  return true;
}

LOOP_COLD_MEM bool prepareLiveRecordWindow(Loop& loop, PendingSnapshotPublishBox& box,
                                           const LoopSnapshot& parent,
                                           uint32_t liveRecordCompletedTicks) {
  const uint32_t completedTicks = liveRecordCompletedOrOneBar(liveRecordCompletedTicks);
  if (!extendLiveRecordCandidateCapacity(box, completedTicks, loop.loopLengthTicks)) {
    return false;
  }
  const uint32_t parentLengthTicks =
      box.unpublishedCandidate ? box.unpublishedCandidate->loopLengthTicks()
                               : parent.loopLengthTicks();
  const uint32_t previousCompletedTicks = box.appliedLiveRecordCompletedTicks;
  uint32_t targetCompletedTicks = completedTicks;
  if (targetCompletedTicks > previousCompletedTicks + Config::TICKS_PER_BAR) {
    targetCompletedTicks = previousCompletedTicks + Config::TICKS_PER_BAR;
  }
  const uint32_t includeUntil = targetCompletedTicks < parentLengthTicks
                                    ? targetCompletedTicks
                                    : parentLengthTicks;
  if (includeUntil <= previousCompletedTicks) {
    return false;
  }
  PendingPassChangeProgress& progress = box.passChangeProgress;
  if (!progress.active) {
    progress.reset();
    progress.active = true;
    progress.fromCaptureStore = true;
    progress.passId = kInvalidPassId;
    progress.nextEventId = parent.nextEventId();
    progress.nextCanonicalOrder = parent.nextEventId();
    progress.nextStoreIndex = box.appliedLiveRecordEventIndex;
  } else if (!progress.fromCaptureStore || !progress.captureWindowConsumed()) {
    return false;
  } else {
    progress.storeWindowComplete = false;
    progress.batch.clear();
    progress.batchIndex = 0;
    progress.nextStoreIndex = box.appliedLiveRecordEventIndex;
  }
  progress.includeUntil = includeUntil;
  loop.assignMissingNoteIdsInStoreWindow(loop.capture.store, progress.nextStoreIndex,
                                         includeUntil);
  box.constructionPassId = kInvalidPassId;
  return true;
}

LOOP_COLD_MEM void refillPassChangeBatch(const Loop& loop, PendingPassChangeProgress& progress) {
  if (!progress.batchExhausted()) {
    return;
  }
  progress.batch.clear();
  progress.batchIndex = 0;
  if (progress.fromCaptureStore) {
    refillCaptureStoreBatch(loop, progress);
    return;
  }
  if (progress.cursor.complete) {
    return;
  }
  bool isOverdubPass = false;
  const CommittedChunkIdList* chunks =
      committedChunksForPass(loop, progress.passId, isOverdubPass);
  if (chunks == nullptr) {
    progress.cursor.complete = true;
    return;
  }
  const CommittedChunkIdList* lists[] = {chunks};
  CommittedEventRange::appendWindowBudgeted(lists, 1, loop.loopLengthTicks, 0,
                                            loop.loopLengthTicks,
                                            kSnapshotWrapJournalMaxChanges, progress.cursor,
                                            progress.batch);
}

LOOP_COLD_MEM __attribute__((noinline)) bool appendNextPassChangeFragment(
    const Loop& loop, const LoopSnapshot& parent, PendingPassChangeProgress& progress,
    LoopSnapshotChangeSet& changes, uint32_t& affectedBars, uint32_t& membershipEntries,
    bool skipParentNoteLookup) {
  {
#if defined(SESSION_CAPTURE)
    SnapshotIdleChildAdd timed(s_lastSnapshotIdleChildren.refillUs);
#endif
    refillPassChangeBatch(loop, progress);
  }
  while (!progress.batchExhausted()) {
    const MidiEvent& event = progress.batch[progress.batchIndex];
    bool addEvent = false;
    {
#if defined(SESSION_CAPTURE)
      SnapshotIdleChildAdd timed(s_lastSnapshotIdleChildren.eventScanUs);
#endif
      addEvent = skipParentNoteLookup ? true : !eventAlreadyInSnapshot(parent, event);
    }
    const bool isNoteEvent = event.isNoteOn() || event.isNoteOff();
    bool parentHasNote = false;
    if (isNoteEvent && event.noteId != kInvalidNoteId && !progress.fromCaptureStore &&
        !skipParentNoteLookup) {
#if defined(SESSION_CAPTURE)
      SnapshotIdleChildAdd timed(s_lastSnapshotIdleChildren.noteLookupUs);
#endif
      parentHasNote = static_cast<bool>(
          parent.findNoteAtPitch(event.noteId, event.data.noteData.note));
    }
#if defined(SESSION_CAPTURE)
    SnapshotIdleChildAdd timed(s_lastSnapshotIdleChildren.changeBuildUs);
#endif
    PendingPassChangeProgress::OpenNote prospective{};
    PendingPassChangeProgress::OpenNote* existing = nullptr;
    bool trackNote = false;
    if (isNoteEvent && event.noteId != kInvalidNoteId && !parentHasNote) {
      existing = findOpenPassNote(progress, event.noteId);
      if (existing != nullptr) {
        prospective = *existing;
      } else {
        prospective.noteId = event.noteId;
      }
      updateOpenPassNoteFromEvent(prospective, event);
      trackNote = true;
    }
    const bool emitNote = trackNote && prospective.hasStart && prospective.hasEnd;
    const size_t addedChanges = (addEvent ? 1 : 0) + (emitNote ? 1 : 0);
    uint32_t addedBars = 0;
    if (emitNote) {
      LoopSnapshotNote note{};
      note.startTick = prospective.startTick;
      note.endTick = prospective.endTick;
      addedBars = estimateNoteAffectedBars(note, loop.loopLengthTicks);
    }
    if (addedChanges == 0) {
      ++progress.batchIndex;
      continue;
    }
    if ((!changes.notes.empty() || !changes.events.empty()) &&
        (journalWouldExceedCaps(changes, affectedBars, addedBars, addedChanges) ||
         membershipEntries + addedBars > kSnapshotWrapJournalMaxMembershipEntries)) {
      break;
    }
    if (addEvent) {
      SnapshotEventChange eventChange{};
      eventChange.kind = SnapshotChangeKind::Add;
      eventChange.next.loopId = loop.loopId;
      eventChange.next.eventId = progress.nextEventId++;
      eventChange.next.canonicalOrder = progress.nextCanonicalOrder++;
      eventChange.next.value = event;
      changes.events.push_back(eventChange);
    }
    if (emitNote) {
      appendOpenNoteChange(prospective, loop.loopId, changes);
      if (existing != nullptr) {
        removeOpenPassNote(progress, existing);
      }
      affectedBars += addedBars;
      membershipEntries += addedBars;
    } else if (trackNote) {
      if (existing != nullptr) {
        *existing = prospective;
      } else {
        PendingPassChangeProgress::OpenNote* stored =
            addOpenPassNote(progress, prospective.noteId);
        if (stored == nullptr) {
          break;
        }
        *stored = prospective;
      }
    }
    ++progress.batchIndex;
  }
  return true;
}

LOOP_COLD_MEM __attribute__((noinline)) bool startNextPassChangeFragmentImpl(
    Loop& loop, PendingSnapshotPublishBox& box, const ImmutableLoopSnapshotRef& parent) {
  // Transaction (DEC-047 amendment 2026-09-16): build the fragment on a working
  // copy. Authoritative `passChangeProgress` advances only after the fragment is
  // successfully incorporated, so a failed fragment is rebuilt from the same
  // source state instead of being skipped.
  box.fragmentProgress = box.passChangeProgress;
  box.fragmentProgressValid = true;
  PendingPassChangeProgress& progress = box.fragmentProgress;
  LoopSnapshotChangeSet changes;
  uint32_t affectedBars = 0;
  uint32_t membershipEntries = 0;
  if (!appendNextPassChangeFragment(loop, *parent, progress, changes, affectedBars,
                                    membershipEntries, box.fromLoadedPasses)) {
    box.fragmentProgressValid = false;
    return false;
  }
  if (changes.notes.empty() && changes.events.empty()) {
    // Nothing to construct, so the fragment is trivially incorporated.
    box.passChangeProgress = progress;
    box.fragmentProgressValid = false;
    box.consecutiveFragmentFailures = 0;
    box.fragmentBlocked = false;
    box.persistLiveRecordCursor();
    if (box.passChangeProgress.complete()) {
      if (box.passChangeProgress.passId != kInvalidPassId) {
        box.appliedPassIds.push_back(box.passChangeProgress.passId);
      }
      box.constructionPassId = kInvalidPassId;
      box.passChangeProgress.reset();
    }
    return box.passChangeProgress.active || !box.queuedPassIds.empty();
  }
  {
#if defined(SESSION_CAPTURE)
    SnapshotIdleChildAdd timed(s_lastSnapshotIdleChildren.constructUs);
#endif
    box.construction = std::make_unique<LoopSnapshotConstruction>(parent, changes);
  }
#if defined(PIO_UNIT_TEST_NATIVE)
  box.construction->failAfterUnitsForTest(loop.snapshotFragmentFailureInjectionForTest(),
                                          loop.snapshotFragmentFailureReasonForTest());
  loop.clearSnapshotFragmentFailureInjectionForTest();
#endif
  box.constructionPassId = progress.passId;
  return true;
}

LOOP_COLD_MEM bool startNextPassChangeFragment(Loop& loop, PendingSnapshotPublishBox& box,
                                               const ImmutableLoopSnapshotRef& parent) {
#if defined(SESSION_CAPTURE)
  if (!box.passChangeProgress.fromCaptureStore) {
    SnapshotIdleChildAdd timed(s_lastSnapshotIdleChildren.passChangeUs);
    return startNextPassChangeFragmentImpl(loop, box, parent);
  }
#endif
  return startNextPassChangeFragmentImpl(loop, box, parent);
}

LOOP_COLD_MEM ImmutableLoopSnapshotRef resolvePendingParent(Loop& loop,
                                                            PendingSnapshotPublishBox& box,
                                                            uint32_t liveRecordCompletedTicks) {
  const uint32_t completedTicks = liveRecordCompletedOrOneBar(liveRecordCompletedTicks);
  if (box.unpublishedCandidate &&
      box.unpublishedCandidate->loopLengthTicks() == loop.loopLengthTicks &&
      (box.sessionExitQueued || box.unpublishedCandidate->loopLengthTicks() >= completedTicks)) {
    return box.unpublishedCandidate;
  }
  ImmutableLoopSnapshotRef parent = loop.publishedSnapshot();
  if (parent) {
    return parent;
  }
  if (box.sessionExitQueued && isRecordSessionExit(box.sessionExitReason)) {
    return LoopSnapshot::createEmpty(loop.loopId, loop.loopLengthTicks, loop.loopStartTick,
                                     loop.nextNoteId_, 1);
  }
  if (loop.passes.hasRecordPass()) {
    for (const PassId passId : box.queuedPassIds) {
      if (passId == loop.passes.recordPass.id) {
        return LoopSnapshot::createEmpty(loop.loopId, loop.loopLengthTicks, loop.loopStartTick,
                                         loop.nextNoteId_, 1);
      }
    }
    for (const PassId passId : box.appliedPassIds) {
      if (passId == loop.passes.recordPass.id) {
        return LoopSnapshot::createEmpty(loop.loopId, loop.loopLengthTicks, loop.loopStartTick,
                                         loop.nextNoteId_, 1);
      }
    }
    if (box.constructionPassId == loop.passes.recordPass.id) {
      return LoopSnapshot::createEmpty(loop.loopId, loop.loopLengthTicks, loop.loopStartTick,
                                       loop.nextNoteId_, 1);
    }
  }
  if (!box.sessionExitQueued && loop.capture.phase == CapturePhase::Record &&
      loop.loopLengthTicks > 0 && !loop.publishedSnapshot()) {
    if (box.unpublishedCandidate) {
      if (!extendLiveRecordCandidateCapacity(box, completedTicks, loop.loopLengthTicks)) {
        return {};
      }
      return box.unpublishedCandidate;
    }
    return LoopSnapshot::createEmpty(
        loop.loopId, liveRecordNewParentLength(completedTicks, loop.loopLengthTicks),
        loop.loopStartTick, loop.nextNoteId_, 1);
  }
  return {};
}

LOOP_COLD_MEM __attribute__((noinline)) bool startNextPendingConstruction(
    Loop& loop, PendingSnapshotPublishBox& box, uint32_t liveRecordCompletedTicks) {
  if (!box.queuedWrapJournals.empty()) {
    const SnapshotWrapJournalFragment& fragment = box.queuedWrapJournals.front();
    ImmutableLoopSnapshotRef parent = box.unpublishedCandidate
                                          ? box.unpublishedCandidate
                                          : fragment.parent;
    if (!parent) {
      parent = resolvePendingParent(loop, box, liveRecordCompletedTicks);
    }
    if (!parent) {
      return false;
    }
    box.construction = std::make_unique<LoopSnapshotConstruction>(parent, fragment.changes);
    box.constructionPassId = fragment.wrapPassId;
    box.wrapJournalInFlight = true;
    return true;
  }

  const ImmutableLoopSnapshotRef parent =
      resolvePendingParent(loop, box, liveRecordCompletedTicks);
  if (!parent) {
    return false;
  }

  ImmutableLoopSnapshotRef fragmentParent = parent;
  if (box.passChangeProgress.active) {
    if (box.sessionExitQueued && box.passChangeProgress.fromCaptureStore) {
      if (!handoffLiveRecordProgressToCommittedPass(box, *parent)) {
        return false;
      }
    } else if (box.passChangeProgress.fromCaptureStore &&
               box.passChangeProgress.captureWindowConsumed()) {
      if (!prepareLiveRecordWindow(loop, box, *parent, liveRecordCompletedTicks)) {
        return false;
      }
      fragmentParent = liveRecordFragmentParent(parent, box);
    }
    return startNextPassChangeFragment(loop, box, fragmentParent);
  }

  if (!box.queuedPassIds.empty()) {
    const PassId passId = box.queuedPassIds.front();
    box.queuedPassIds.erase(box.queuedPassIds.begin());
    bool isOverdubPass = false;
    const CommittedChunkIdList* chunks = committedChunksForPass(loop, passId, isOverdubPass);
    if (chunks == nullptr || chunks->empty()) {
      box.appliedPassIds.push_back(passId);
      return !box.queuedPassIds.empty() || box.sessionExitQueued;
    }
    box.passChangeProgress.reset();
    box.passChangeProgress.active = true;
    box.passChangeProgress.passId = passId;
    box.passChangeProgress.nextEventId = parent->nextEventId();
    box.passChangeProgress.nextCanonicalOrder = parent->nextEventId();
    CommittedEventRange::resetWindowTraversalCursor(box.passChangeProgress.cursor);
    box.constructionPassId = passId;
    return startNextPassChangeFragment(loop, box, parent);
  }

  if (!box.sessionExitQueued && loop.capture.phase == CapturePhase::Record &&
      loop.loopLengthTicks > 0 && !loop.publishedSnapshot()) {
    if (!prepareLiveRecordWindow(loop, box, *parent, liveRecordCompletedTicks)) {
      return false;
    }
    fragmentParent = liveRecordFragmentParent(parent, box);
    return startNextPassChangeFragment(loop, box, fragmentParent);
  }

  if (box.sessionExitQueued && !box.companionsApplied) {
    box.companionsApplied = true;
    LoopSnapshotChangeSet changes;
    appendCompanionChanges(loop, *parent, box.companionIds, changes);
    if (changes.notes.empty() && changes.events.empty()) {
      return false;
    }
    box.construction = std::make_unique<LoopSnapshotConstruction>(parent, changes);
    box.constructionPassId = kInvalidPassId;
    return true;
  }
  return false;
}

bool remainingSessionExitPublishWork(const PendingSnapshotPublishBox& box) {
  return !box.queuedPassIds.empty() || box.passChangeProgress.active ||
         !box.companionsApplied;
}

/// A settled wrap carries its companion transforms inside its own journal, so it has
/// no companion batch of its own to wait for. `companionsApplied` only ever becomes
/// true on the session-exit path, so reusing the session-exit predicate here left
/// every wrap candidate unpublished (`134803`: root_rev 1 against playback_rev 11).
bool remainingWrapPublishWork(const PendingSnapshotPublishBox& box) {
  return !box.queuedPassIds.empty() || box.passChangeProgress.active ||
         (box.sessionExitQueued && !box.companionsApplied);
}

/// A representability failure cannot be fixed by rebuilding the same fragment.
/// After this many consecutive failures publication parks with the previous root
/// selected instead of consuming an idle turn forever.
constexpr uint16_t kSnapshotFragmentMaxConsecutiveFailures = 2;

/// DEC-073 RC-PUB-1: internal-heap headroom one content-sized publication turn must leave beyond
/// the admission floor -- one slab plus the fragment working set. The slab itself comes from the
/// external pool, whose exhaustion the gate catches through the pool-refusal record; this margin
/// is what stops a low heap from being drained by a publication turn, which is the 2026-09-18
/// reset (`session_20260918_101615`): after the wrap commit the heap fell 108 -> 72 -> 36 -> 0 KB
/// in three turns while the pool still served slabs, and the exhausted allocator called `abort()`.
constexpr uint32_t kSnapshotPublicationReserveBytes = kSnapshotSlabBytes + (8u * 1024u);

/// Admit one content-sized publication turn -- a construction start, step, or freeze. These are
/// the only publication operations that allocate new snapshot nodes (32 KB pool slabs plus an
/// internal-heap working set), so they are admitted exactly like the record/overdub boundaries.
///
/// A denial is not a failure: nothing is written, the previous root stays selected, and the
/// publication stays pending. That is the state the persistence gate already understands
/// (`hasSettledPublishedSnapshotForSave` false -> passes-authoritative body), and it is strictly
/// better than reaching `ExternalMemoryFirstAllocator::abort()` on a full pool.
///
/// Admission is also the release condition for a transient allocation park (RC-PUB-2). The release
/// requires that the park was observed under a denial, so the same admission state that let the
/// fragment fail cannot immediately re-arm it.
LOOP_COLD_MEM bool admitSnapshotPublicationTurn(PendingSnapshotPublishBox& box) {
  const bool admitted = MemoryMonitor::admitOperationMemory(
      MemoryMonitor::OperationKind::SnapshotReserve, kSnapshotPublicationReserveBytes);
  if (!box.fragmentBlockTransient) {
    return admitted;
  }
  if (!admitted) {
    box.fragmentParkSawDenial = true;
    return false;
  }
  if (!box.fragmentParkSawDenial) {
    return false;
  }
  box.fragmentBlocked = false;
  box.fragmentBlockTransient = false;
  box.fragmentParkSawDenial = false;
  box.consecutiveFragmentFailures = 0;
  return true;
}

const char* snapshotFailureReasonName(LoopSnapshotFailureReason reason) {
  switch (reason) {
    case LoopSnapshotFailureReason::MembershipCapacity:
      return "membership_capacity";
    case LoopSnapshotFailureReason::Allocation:
      return "allocation";
    case LoopSnapshotFailureReason::Injected:
      return "injected";
    default:
      return "none";
  }
}

#if defined(SESSION_CAPTURE) && defined(ARDUINO)
LOOP_COLD_MEM void emitSnapshotFragmentFailure(const PendingSnapshotPublishBox& box,
                                               LoopSnapshotFailureReason reason) {
  char line[176];
  snprintf(line, sizeof(line),
           "#CAP,%lu,DIAG,snapshot_fragment,fail,reason=%s,n=%u,pass=%u,next=%u,blocked=%u",
           static_cast<unsigned long>(micros()), snapshotFailureReasonName(reason),
           static_cast<unsigned>(box.consecutiveFragmentFailures),
           static_cast<unsigned>(box.constructionPassId),
           static_cast<unsigned>(box.passChangeProgress.nextEventId),
           box.fragmentBlocked ? 1u : 0u);
  DebugSessionCapture::appendCaptureTextLine(line);
}
#endif

void noteFragmentIncorporationFailure(LoopSnapshotFailureReason reason,
                                      PendingSnapshotPublishBox& box) {
  if (box.consecutiveFragmentFailures < UINT16_MAX) {
    ++box.consecutiveFragmentFailures;
  }
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
  emitSnapshotFragmentFailure(box, reason);
#else
  (void)reason;
#endif
  // An injected (test) failure keeps retrying the exact fragment -- the DEC-047 transaction
  // fixture relies on the retry. A persistent representability failure parks publication for
  // good: the previous root stays selected and the same impossible fragment is not rebuilt every
  // turn. An allocation failure parks *transiently* (DEC-073 RC-PUB-2): it is memory pressure, so
  // the park is released once admission has seen the pressure and admits again. Before RC-PUB-2
  // only MembershipCapacity parked, so a full pool re-entered construction every turn -- each
  // attempt copying the fragment's change set onto the internal heap and retrying the slab --
  // which is what drove the heap to zero and reached `abort()` on 2026-09-18.
  if (box.consecutiveFragmentFailures >= kSnapshotFragmentMaxConsecutiveFailures) {
    if (reason == LoopSnapshotFailureReason::MembershipCapacity) {
      box.fragmentBlocked = true;
      box.fragmentBlockTransient = false;
    } else if (reason == LoopSnapshotFailureReason::Allocation) {
      box.fragmentBlocked = true;
      box.fragmentBlockTransient = true;
      box.fragmentParkSawDenial = false;
    }
  }
}

LOOP_COLD_MEM __attribute__((noinline)) bool finishPendingConstruction(
    PendingSnapshotPublishBox& box) {
  ImmutableLoopSnapshotRef frozen = box.construction->freeze();
  if (!frozen) {
    // Partial candidate is discarded; authoritative progress is untouched.
    box.fragmentProgressValid = false;
    return false;
  }
  box.unpublishedCandidate = std::move(frozen);
  if (box.fragmentProgressValid) {
    // The fragment was incorporated, so its working progress becomes
    // authoritative. This is the transaction commit point.
    box.passChangeProgress = box.fragmentProgress;
    box.fragmentProgressValid = false;
    box.persistLiveRecordCursor();
  }
  box.consecutiveFragmentFailures = 0;
  box.fragmentBlocked = false;
  if (box.wrapJournalInFlight && !box.queuedWrapJournals.empty()) {
    box.queuedWrapJournals.erase(box.queuedWrapJournals.begin());
  }
  const PassId finishedPassId = box.constructionPassId;
  box.construction.reset();
  box.wrapJournalInFlight = false;
  if (box.passChangeProgress.active) {
    box.persistLiveRecordCursor();
    if (box.passChangeProgress.complete()) {
      if (finishedPassId != kInvalidPassId) {
        box.appliedPassIds.push_back(finishedPassId);
      }
      box.passChangeProgress.reset();
      box.constructionPassId = kInvalidPassId;
    }
    return true;
  }
  if (finishedPassId != kInvalidPassId) {
    box.appliedPassIds.push_back(finishedPassId);
  }
  box.constructionPassId = kInvalidPassId;
  return true;
}

}  // namespace

LOOP_COLD_MEM bool Loop::queueOverdubWrapSnapshotJournal(PassId wrapPassId) {
  if (wrapPassId == kInvalidPassId || loopLengthTicks == 0) {
    return false;
  }
  ImmutableLoopSnapshotRef parent = publishedSnapshot();
  if (pendingSnapshotPublishBox_ && pendingSnapshotPublishBox_->unpublishedCandidate) {
    parent = pendingSnapshotPublishBox_->unpublishedCandidate;
  }
  if (!parent) {
    return false;
  }

  if (!pendingSnapshotPublishBox_) {
    pendingSnapshotPublishBox_ = std::make_unique<PendingSnapshotPublishBox>();
  }
  PendingSnapshotPublishBox& box = *pendingSnapshotPublishBox_;
  if (passAlreadyTracked(box, wrapPassId)) {
    return !box.queuedWrapJournals.empty();
  }

  LoopSnapshotChangeSet current;
  uint32_t affectedBars = 0;
  uint32_t membershipEntries = 0;
  EventId nextEventId = parent->nextEventId();
  uint32_t nextCanonicalOrder = parent->nextEventId();
  std::vector<SnapshotWrapJournalFragment> fragments;

  auto flushCurrent = [&]() {
    flushWrapJournalFragment(fragments, parent, wrapPassId, current);
    affectedBars = 0;
    membershipEntries = 0;
  };

  for (const PendingNoteChange& change : pendingNoteChanges_) {
    LoopSnapshotChangeSet incoming;
    appendPendingNoteChange(*parent, change, loopId, loopLengthTicks, nextEventId,
                            nextCanonicalOrder, incoming);
    if (incoming.notes.empty() && incoming.events.empty()) {
      continue;
    }
    uint32_t addedBars = 0;
    for (const SnapshotNoteChange& noteChange : incoming.notes) {
      if (noteChange.previous) {
        addedBars += estimateNoteAffectedBars(*noteChange.previous, parent->loopLengthTicks());
      }
      if (noteChange.kind != SnapshotChangeKind::Remove) {
        addedBars += estimateNoteAffectedBars(noteChange.next, parent->loopLengthTicks());
      }
    }
    const size_t addedChanges = journalChangeCount(incoming);
    const uint32_t addedEntries = addedBars;
    if (!current.notes.empty() || !current.events.empty()) {
      if (journalWouldExceedCaps(current, affectedBars, addedBars, addedChanges) ||
          membershipEntries + addedEntries > kSnapshotWrapJournalMaxMembershipEntries) {
        flushCurrent();
      }
    }
    current.notes.insert(current.notes.end(), incoming.notes.begin(), incoming.notes.end());
    current.events.insert(current.events.end(), incoming.events.begin(), incoming.events.end());
    affectedBars += addedBars;
    membershipEntries += addedEntries;
  }

  LoopSnapshotChangeSet nonNote;
  appendWrapNonNoteEvents(*this, *parent, wrapPassId, nextEventId, nextCanonicalOrder, nonNote);
  if (!nonNote.events.empty()) {
    if (!current.notes.empty() || !current.events.empty()) {
      if (journalWouldExceedCaps(current, affectedBars, 0, journalChangeCount(nonNote))) {
        flushCurrent();
      }
    }
    current.events.insert(current.events.end(), nonNote.events.begin(), nonNote.events.end());
  }
  flushCurrent();

  if (fragments.empty()) {
    return false;
  }
  for (size_t index = 0; index < fragments.size(); ++index) {
    fragments[index].fragmentIndex = static_cast<uint8_t>(index);
    fragments[index].fragmentCount = static_cast<uint8_t>(fragments.size());
    box.queuedWrapJournals.push_back(std::move(fragments[index]));
  }
  box.journaledPassIds.push_back(wrapPassId);
  return true;
}

LOOP_COLD_MEM void Loop::stampWrapSnapshotJournalRevision(PassId wrapPassId,
                                                          uint32_t targetRevision) {
  if (!pendingSnapshotPublishBox_ || wrapPassId == kInvalidPassId || targetRevision == 0) {
    return;
  }
  for (SnapshotWrapJournalFragment& fragment : pendingSnapshotPublishBox_->queuedWrapJournals) {
    if (fragment.wrapPassId == wrapPassId) {
      fragment.targetRevision = targetRevision;
    }
  }
}

LOOP_COLD_MEM void Loop::enqueueLiveOverdubPasses() {
  if (!pendingSnapshotPublishBox_ || !hasOverdubSession() ||
      pendingSnapshotPublishBox_->sessionExitQueued) {
    return;
  }
  PendingSnapshotPublishBox& box = *pendingSnapshotPublishBox_;
  const size_t queuedBefore = box.queuedPassIds.size();
  for (const PassId passId : overdubSessionPassIds_) {
    if (passId != kInvalidPassId && isCommittedOverdubPass(*this, passId) &&
        !passAlreadyTracked(box, passId)) {
      box.queuedPassIds.push_back(passId);
    }
  }
  if (lastCommittedPassId_ != kInvalidPassId &&
      isCommittedOverdubPass(*this, lastCommittedPassId_) &&
      !passAlreadyTracked(box, lastCommittedPassId_)) {
    box.queuedPassIds.push_back(lastCommittedPassId_);
  }
  // DEC-073: coverage advances only for passes actually enumerated into the box, which are applied
  // before any publish (`remainingSessionExitPublishWork` requires the queue to drain first).
  if (box.queuedPassIds.size() != queuedBefore) {
    box.coveredPassRevision = committedPassRevision();
  }
}

LOOP_COLD_MEM void Loop::discardUnpublishedSnapshotIfNotSessionExit() {
  if (hasPendingSessionExitReceipt() ||
      !pendingSnapshotPublishBox_ || pendingSnapshotPublishBox_->sessionExitQueued) {
    return;
  }
  // Skipped overdub stop (`094930` seal=skipped) never queues session-exit, then
  // closeOverdubSession used to drop in-flight wrap journals (`root_rev` stuck at 1).
  const PendingSnapshotPublishBox& box = *pendingSnapshotPublishBox_;
  if (!box.queuedWrapJournals.empty() || box.wrapJournalInFlight || box.construction) {
    return;
  }
  clearPendingSnapshotPublish();
}

LOOP_COLD_MEM bool Loop::hasPendingSnapshotPublish() const {
  if (hasPendingSessionExitReceipt()) return true;
  if (pendingSnapshotPublishBox_) {
    const PendingSnapshotPublishBox& box = *pendingSnapshotPublishBox_;
    if (box.construction || !box.queuedPassIds.empty() || box.sessionExitQueued ||
        !box.queuedWrapJournals.empty() || box.passChangeProgress.active) {
      return true;
    }
  }
  if (capture.phase == CapturePhase::Record && loopLengthTicks > 0 && !publishedSnapshot() &&
      !(pendingSnapshotPublishBox_ && pendingSnapshotPublishBox_->sessionExitQueued) &&
      !(pendingSnapshotPublishBox_ && pendingSnapshotPublishBox_->unpublishedCandidate)) {
    const size_t applied = pendingSnapshotPublishBox_
                               ? pendingSnapshotPublishBox_->appliedLiveRecordEventIndex
                               : 0;
    if (capture.store.size() > applied) {
      return true;
    }
  }
  if (!hasOverdubSession()) {
    return false;
  }
  const PendingSnapshotPublishBox* box = pendingSnapshotPublishBox_.get();
  for (const PassId passId : overdubSessionPassIds_) {
    if (passId != kInvalidPassId && isCommittedOverdubPass(*this, passId) &&
        (box == nullptr || !passAlreadyTracked(*box, passId))) {
      return true;
    }
  }
  return lastCommittedPassId_ != kInvalidPassId &&
         isCommittedOverdubPass(*this, lastCommittedPassId_) &&
         (box == nullptr || !passAlreadyTracked(*box, lastCommittedPassId_));
}

LOOP_COLD_MEM bool Loop::hasSettledPublishedSnapshotForSave() const {
  // DEC-073 RC2: a persisted body may embed the selected root only when the root demonstrably
  // represents the committed passes (`publishedSnapshotIsCurrent`) and the publisher has no newer
  // request in flight. Defined beside `hasPendingSnapshotPublish` because it calls it, and suites
  // that link only `LoopSnapshotLifetime.cpp` must not need the publisher TU.
  return publishedSnapshotIsCurrent() && !hasPendingSnapshotPublish();
}

LOOP_COLD_MEM bool Loop::hasInFlightSnapshotConstruction() const {
  return pendingSnapshotPublishBox_ && pendingSnapshotPublishBox_->construction;
}

LOOP_COLD_MEM bool Loop::hasPendingWrapJournal() const {
  return pendingSnapshotPublishBox_ &&
         !pendingSnapshotPublishBox_->queuedWrapJournals.empty();
}

#if defined(PIO_UNIT_TEST_NATIVE)
const LoopSnapshotChangeSet* Loop::pendingWrapJournalChangesForTest() const {
  if (!pendingSnapshotPublishBox_ || pendingSnapshotPublishBox_->queuedWrapJournals.empty()) {
    return nullptr;
  }
  return &pendingSnapshotPublishBox_->queuedWrapJournals.front().changes;
}

Loop::SnapshotFragmentProgressForTest Loop::snapshotFragmentProgressForTest() const {
  SnapshotFragmentProgressForTest out{};
  if (!pendingSnapshotPublishBox_) {
    return out;
  }
  const PendingPassChangeProgress& progress = pendingSnapshotPublishBox_->passChangeProgress;
  out.active = progress.active;
  out.batchSize = progress.batch.size();
  out.batchIndex = progress.batchIndex;
  out.nextStoreIndex = progress.nextStoreIndex;
  out.nextEventId = progress.nextEventId;
  out.nextCanonicalOrder = progress.nextCanonicalOrder;
  out.openNoteCount = progress.openNoteCount;
  out.cursorComplete = progress.cursor.complete;
  return out;
}

bool Loop::snapshotFragmentBlockedForTest() const {
  return pendingSnapshotPublishBox_ && pendingSnapshotPublishBox_->fragmentBlocked;
}

uint16_t Loop::snapshotFragmentConsecutiveFailuresForTest() const {
  return pendingSnapshotPublishBox_ ? pendingSnapshotPublishBox_->consecutiveFragmentFailures : 0;
}
#endif

LOOP_COLD_MEM size_t Loop::pendingSnapshotQueuedPassCount() const {
  return pendingSnapshotPublishBox_ ? pendingSnapshotPublishBox_->queuedPassIds.size() : 0;
}

LOOP_COLD_MEM size_t Loop::pendingSnapshotAppliedPassCount() const {
  return pendingSnapshotPublishBox_ ? pendingSnapshotPublishBox_->appliedPassIds.size() : 0;
}

LOOP_COLD_MEM PassId Loop::pendingSnapshotConstructionPassId() const {
  return pendingSnapshotPublishBox_ ? pendingSnapshotPublishBox_->constructionPassId
                                    : kInvalidPassId;
}

LOOP_COLD_MEM size_t Loop::pendingSnapshotWrapJournalCount() const {
  return pendingSnapshotPublishBox_ ? pendingSnapshotPublishBox_->queuedWrapJournals.size()
                                    : 0;
}

static LOOP_COLD_MEM const SnapshotWrapJournalFragment* pendingWrapJournalFront(
    const PendingSnapshotPublishBox* box) {
  if (box == nullptr || box->queuedWrapJournals.empty()) {
    return nullptr;
  }
  return &box->queuedWrapJournals.front();
}

LOOP_COLD_MEM uint8_t Loop::pendingSnapshotWrapFragmentIndex() const {
  const SnapshotWrapJournalFragment* fragment =
      pendingWrapJournalFront(pendingSnapshotPublishBox_.get());
  return fragment != nullptr ? fragment->fragmentIndex : 0;
}

LOOP_COLD_MEM uint8_t Loop::pendingSnapshotWrapFragmentCount() const {
  const SnapshotWrapJournalFragment* fragment =
      pendingWrapJournalFront(pendingSnapshotPublishBox_.get());
  return fragment != nullptr ? fragment->fragmentCount : 0;
}

LOOP_COLD_MEM uint32_t Loop::pendingSnapshotWrapParentRevision() const {
  const SnapshotWrapJournalFragment* fragment =
      pendingWrapJournalFront(pendingSnapshotPublishBox_.get());
  if (fragment == nullptr || !fragment->parent || !publishedSnapshotBox_ ||
      fragment->parent != publishedSnapshotBox_->snapshot) {
    return 0;
  }
  return publishedSnapshotBox_->revision;
}

LOOP_COLD_MEM bool Loop::pendingSnapshotSessionExitQueued() const {
  return hasPendingSessionExitReceipt() ||
         (pendingSnapshotPublishBox_ && pendingSnapshotPublishBox_->sessionExitQueued);
}

LOOP_COLD_MEM bool Loop::isPlaybackSessionExitPublicationGap() const {
  if (publishedSnapshot()) {
    return false;
  }
  return pendingSnapshotSessionExitQueued() && hasPendingSnapshotPublish();
}

LOOP_COLD_MEM bool Loop::pendingSnapshotFromLoadedPasses() const {
  return pendingSnapshotPublishBox_ && pendingSnapshotPublishBox_->fromLoadedPasses;
}

LOOP_COLD_MEM const EditPassIdList* Loop::pendingSnapshotCompanionIds() const {
  return pendingSnapshotPublishBox_ ? &pendingSnapshotPublishBox_->companionIds : nullptr;
}

LOOP_COLD_MEM void Loop::appendPendingSnapshotCompanionId(EditPassId id) {
  if (id == kInvalidEditPassId) {
    return;
  }
  ensurePendingSnapshotPublishBox();
  appendUniqueId(pendingSnapshotPublishBox_->companionIds, id, kInvalidEditPassId);
}

LOOP_COLD_MEM LoadedSnapshotHydrationPhase Loop::loadedSnapshotHydrationPhase() const {
  return pendingSnapshotPublishBox_ ? pendingSnapshotPublishBox_->loadedHydrationPhase
                                    : LoadedSnapshotHydrationPhase::None;
}

LOOP_COLD_MEM uint32_t Loop::pendingSnapshotCandidateLengthTicks() const {
  if (!pendingSnapshotPublishBox_) {
    return 0;
  }
  if (pendingSnapshotPublishBox_->unpublishedCandidate) {
    return pendingSnapshotPublishBox_->unpublishedCandidate->loopLengthTicks();
  }
  if (pendingSnapshotPublishBox_->construction) {
    return pendingSnapshotPublishBox_->construction->candidate().loopLengthTicks();
  }
  return 0;
}

LOOP_COLD_MEM uint32_t Loop::lastSnapshotIdleStartUs() const {
#if defined(SESSION_CAPTURE)
  return s_lastSnapshotIdleChildren.startUs;
#else
  return 0;
#endif
}

LOOP_COLD_MEM uint32_t Loop::lastSnapshotIdlePassChangeUs() const {
#if defined(SESSION_CAPTURE)
  return s_lastSnapshotIdleChildren.passChangeUs;
#else
  return 0;
#endif
}

LOOP_COLD_MEM uint32_t Loop::lastSnapshotIdleStepUs() const {
#if defined(SESSION_CAPTURE)
  return s_lastSnapshotIdleChildren.stepUs;
#else
  return 0;
#endif
}

LOOP_COLD_MEM uint32_t Loop::lastSnapshotIdleFinishUs() const {
#if defined(SESSION_CAPTURE)
  return s_lastSnapshotIdleChildren.finishUs;
#else
  return 0;
#endif
}

LOOP_COLD_MEM uint32_t Loop::lastSnapshotIdlePublishUs() const {
#if defined(SESSION_CAPTURE)
  return s_lastSnapshotIdleChildren.publishUs;
#else
  return 0;
#endif
}

LOOP_COLD_MEM uint32_t Loop::lastSnapshotIdleProbeUs() const {
#if defined(SESSION_CAPTURE)
  return s_lastSnapshotIdleChildren.probeUs;
#else
  return 0;
#endif
}

LOOP_COLD_MEM uint32_t Loop::lastSnapshotIdleEnqueueUs() const {
#if defined(SESSION_CAPTURE)
  return s_lastSnapshotIdleChildren.enqueueUs;
#else
  return 0;
#endif
}

LOOP_COLD_MEM uint32_t Loop::lastSnapshotIdleDiscardUs() const {
#if defined(SESSION_CAPTURE)
  return s_lastSnapshotIdleChildren.discardUs;
#else
  return 0;
#endif
}

LOOP_COLD_MEM uint32_t Loop::lastSnapshotIdleEmitUs() const {
#if defined(SESSION_CAPTURE)
  return s_lastSnapshotIdleChildren.emitUs;
#else
  return 0;
#endif
}

LOOP_COLD_MEM uint32_t Loop::lastSnapshotIdleRefillUs() const {
#if defined(SESSION_CAPTURE)
  return s_lastSnapshotIdleChildren.refillUs;
#else
  return 0;
#endif
}

LOOP_COLD_MEM uint32_t Loop::lastSnapshotIdleEventScanUs() const {
#if defined(SESSION_CAPTURE)
  return s_lastSnapshotIdleChildren.eventScanUs;
#else
  return 0;
#endif
}

LOOP_COLD_MEM uint32_t Loop::lastSnapshotIdleNoteLookupUs() const {
#if defined(SESSION_CAPTURE)
  return s_lastSnapshotIdleChildren.noteLookupUs;
#else
  return 0;
#endif
}

LOOP_COLD_MEM uint32_t Loop::lastSnapshotIdleChangeBuildUs() const {
#if defined(SESSION_CAPTURE)
  return s_lastSnapshotIdleChildren.changeBuildUs;
#else
  return 0;
#endif
}

LOOP_COLD_MEM uint32_t Loop::lastSnapshotIdleConstructUs() const {
#if defined(SESSION_CAPTURE)
  return s_lastSnapshotIdleChildren.constructUs;
#else
  return 0;
#endif
}

LOOP_COLD_MEM void Loop::addSnapshotIdleEmitUs(uint32_t durationUs) {
#if defined(SESSION_CAPTURE)
  s_lastSnapshotIdleChildren.emitUs += durationUs;
#else
  (void)durationUs;
#endif
}

LOOP_COLD_MEM uint32_t Loop::liveRecordAppliedCompletedTicks() const {
  return pendingSnapshotPublishBox_ ? pendingSnapshotPublishBox_->appliedLiveRecordCompletedTicks
                                    : 0;
}

LOOP_COLD_MEM size_t Loop::liveRecordAppliedEventIndex() const {
  if (!pendingSnapshotPublishBox_) {
    return 0;
  }
  const PendingPassChangeProgress& progress =
      pendingSnapshotPublishBox_->passChangeProgress;
  if (progress.fromCaptureStore && progress.active) {
    return progress.consumedStoreIndex();
  }
  return pendingSnapshotPublishBox_->appliedLiveRecordEventIndex;
}

LOOP_COLD_MEM uint8_t Loop::liveRecordOpenNoteCount() const {
  if (!pendingSnapshotPublishBox_) {
    return 0;
  }
  return pendingSnapshotPublishBox_->passChangeProgress.openNoteCount;
}

LOOP_COLD_MEM ImmutableLoopSnapshotRef Loop::unpublishedSnapshot() const {
  return pendingSnapshotPublishBox_ ? pendingSnapshotPublishBox_->unpublishedCandidate
                                    : ImmutableLoopSnapshotRef{};
}

LOOP_COLD_MEM const LoopSnapshotChangeSet* Loop::pendingSnapshotConstructionChanges() const {
  if (!pendingSnapshotPublishBox_ || !pendingSnapshotPublishBox_->construction) {
    return nullptr;
  }
  return &pendingSnapshotPublishBox_->construction->changes();
}

LOOP_COLD_MEM bool Loop::liveRecordCanApplyCompletedTicks(uint32_t completedTicks) const {
  if (completedTicks == 0) {
    return false;
  }
  // Stage 6C-A: capacity is extendable during live record; admission extends at
  // prepareLiveRecordWindow / resolvePendingParent rather than rejecting here, so
  // any non-zero completed length is admissible regardless of pending state.
  return true;
}

namespace {
/// Shared enumeration for a "publish everything committed" request. Returns false when the loop
/// has nothing to publish; the caller owns the published root and pending-box policy.
LOOP_COLD_MEM bool queueAllCommittedPassesInto(Loop& loop, PendingSnapshotPublishBox& box) {
  if (loop.passes.hasRecordPass() && loop.passes.recordPass.state == CapturePassState::Active &&
      !loop.passes.recordPass.committedChunkIds.empty() &&
      loop.passes.recordPass.id != kInvalidPassId) {
    box.queuedPassIds.push_back(loop.passes.recordPass.id);
  }
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    if (pass.state != CapturePassState::Active || pass.committedChunkIds.empty() ||
        pass.id == kInvalidPassId) {
      continue;
    }
    box.queuedPassIds.push_back(pass.id);
  }
  for (const EditPass& edit : loop.passes.editPasses) {
    if (edit.state == EditPassState::Active && edit.id != kInvalidEditPassId) {
      box.companionIds.push_back(edit.id);
    }
  }
  box.companionsApplied = box.companionIds.empty();
  return !box.queuedPassIds.empty() || !box.companionIds.empty();
}
}  // namespace

LOOP_COLD_MEM bool Loop::queueLoadedSnapshotPublish() {
  if (publishedSnapshotIsCurrent()) {
    return false;
  }
  if (loopLengthTicks == 0 || !hasCommittedPasses()) {
    clearPendingSnapshotPublish();
    return false;
  }
  if (publishedSnapshot()) {
    publishedSnapshotBox_.reset();
  }
  clearPendingSnapshotPublish();
  ensurePendingSnapshotPublishBox();
  PendingSnapshotPublishBox& box = *pendingSnapshotPublishBox_;
  box.sessionExitQueued = true;
  box.fromLoadedPasses = true;
  box.loadedHydrationPhase = LoadedSnapshotHydrationPhase::Queued;
  box.sessionExitReason = CommitReason::RecordStop;
  if (!queueAllCommittedPassesInto(*this, box)) {
    clearPendingSnapshotPublish();
    return false;
  }
  // DEC-073: the enumeration above covers the committed-pass inventory as of now.
  box.coveredPassRevision = committedPassRevision();
  return true;
}

LOOP_COLD_MEM bool Loop::requestCommittedPassesRepublish() {
  if (loopLengthTicks == 0 || !hasCommittedPasses()) {
    return false;
  }
  // Deliberately does NOT clear `publishedSnapshotBox_`: the previous root stays the audible source
  // until the replacement publishes, so an undo/redo never opens a playback gap. The idle Snapshot
  // owner services `fromLoadedPasses == false` work while transport is live (the boot hydration lane
  // is the one gated out by `allowLoadedSnapshotHydration`).
  clearPendingSnapshotPublish();
  ensurePendingSnapshotPublishBox();
  PendingSnapshotPublishBox& box = *pendingSnapshotPublishBox_;
  box.sessionExitQueued = true;
  box.fromLoadedPasses = false;
  box.loadedHydrationPhase = LoadedSnapshotHydrationPhase::None;
  box.sessionExitReason = CommitReason::RecordStop;
  if (!queueAllCommittedPassesInto(*this, box)) {
    clearPendingSnapshotPublish();
    return false;
  }
  // DEC-073: the republish request covers the pass inventory it just enumerated. A later inventory
  // change advances `committedPassRevision` without advancing this watermark, so the candidate
  // built from this request publishes as an intermediate root and never claims the newer coverage.
  box.coveredPassRevision = committedPassRevision();
  return true;
}

LOOP_COLD_MEM bool Loop::prewarmSessionExitSnapshotHandoff() {
  if (!sessionExitReceipts_) {
    auto* storage = ExternalMemoryFirstAllocator<SessionExitSnapshotHandoff>{}.allocate(
        kSessionExitReceiptCapacity);
    if (!storage) return false;
    for (size_t i = 0; i < kSessionExitReceiptCapacity; ++i) {
      new (&storage[i]) SessionExitSnapshotHandoff{};
    }
    sessionExitReceipts_.reset(storage);
    sessionExitReceiptHead_ = 0;
    sessionExitReceiptCount_ = 0;
  }
  return canHandoffSessionExitSnapshot();
}

LOOP_COLD_MEM bool Loop::canHandoffSessionExitSnapshot() const {
  // A *free slot* is the gate, not an unconsumed singleton: a successor session
  // must not wait behind the idle publisher. "One receipt per open session" is
  // enforced inside the handoff, which consumes the session identities.
  return sessionExitReceipts_ != nullptr &&
      sessionExitReceiptCount_ < kSessionExitReceiptCapacity;
}

LOOP_COLD_MEM const SessionExitSnapshotHandoff* Loop::sessionExitReceiptAt(size_t index) const {
  if (!sessionExitReceipts_ || index >= sessionExitReceiptCount_) return nullptr;
  return &sessionExitReceipts_[(sessionExitReceiptHead_ + index) % kSessionExitReceiptCapacity];
}

LOOP_COLD_MEM SessionExitSnapshotHandoff* Loop::sessionExitReceiptByRingIndex(size_t ringIndex) {
  if (sessionExitReceipts_ == nullptr || ringIndex >= kSessionExitReceiptCapacity) {
    return nullptr;
  }
  return &sessionExitReceipts_[ringIndex];
}

LOOP_COLD_MEM const SessionExitSnapshotHandoff* Loop::sessionExitReceiptByRingIndex(
    size_t ringIndex) const {
  return const_cast<Loop*>(this)->sessionExitReceiptByRingIndex(ringIndex);
}

LOOP_COLD_MEM size_t Loop::captureSessionExitTail(CommitReason reason) {
  if (!isSessionExit(reason) || !hasOverdubSession() || loopLengthTicks == 0) {
    return kInvalidSessionExitReceipt;
  }
  if (sessionExitReceiptCount_ >= kSessionExitReceiptCapacity) {
    return kInvalidSessionExitReceipt;
  }
  const size_t ringIndex =
      (sessionExitReceiptHead_ + sessionExitReceiptCount_) % kSessionExitReceiptCapacity;
  SessionExitSnapshotHandoff& receipt = sessionExitReceipts_[ringIndex];
  receipt.reason = reason;
  receipt.lastPassId = kInvalidPassId;
  receipt.companionRowCount = std::min(overdubSessionCursor_, overdubSessionCompanionIds_.size());
  // Swap, never copy: this admission boundary must not allocate, and this interval's identities
  // must leave the shared slot so a successor opens a session of its own (DEC-068).
  receipt.passIds.swap(overdubSessionPassIds_);
  receipt.companionIds.swap(overdubSessionCompanionIds_);
  overdubSessionCursor_ = 0;
  receipt.pending = true;
  receipt.ready = false;
  ++sessionExitReceiptCount_;
  return ringIndex;
}

LOOP_COLD_MEM bool Loop::handoffSessionExitSnapshot(CommitReason reason) {
  if (!canHandoffSessionExitSnapshot() || sessionExitHandedOff_ || !hasOverdubSession() ||
      !isOverdubSessionExit(reason) || loopLengthTicks == 0) return false;
  SessionExitSnapshotHandoff& receipt =
      sessionExitReceipts_[(sessionExitReceiptHead_ + sessionExitReceiptCount_) %
                           kSessionExitReceiptCapacity];
  receipt.reason = reason;
  receipt.lastPassId = lastCommittedPassId_;
  receipt.companionRowCount = std::min(overdubSessionCursor_, overdubSessionCompanionIds_.size());
  receipt.passIds.swap(overdubSessionPassIds_);
  receipt.companionIds.swap(overdubSessionCompanionIds_);
  receipt.pending = true;
  // The legacy one-shot handoff is complete at capture; the deferred path instead captures a
  // not-ready tail at its stop admission and fills the final pass id at its own commit
  // (DEC-068).
  receipt.ready = true;
  ++sessionExitReceiptCount_;
  sessionExitHandedOff_ = true;
  // Existing wrap construction must survive close until the idle owner merges
  // these identities into its queues. The receipt also supplies playback's
  // stale-root overlay before the first publisher turn.
  return true;
}

LOOP_COLD_MEM bool Loop::queueSessionExitSnapshot(CommitReason reason,
                                                  const EditPassIdList& companionIds) {
  if (!isSessionExit(reason) || loopLengthTicks == 0) {
    return false;
  }
  if (isOverdubSessionExit(reason) && !publishedSnapshot()) {
    const bool recordPublishInFlight =
        pendingSnapshotPublishBox_ &&
        (pendingSnapshotPublishBox_->construction ||
         !pendingSnapshotPublishBox_->queuedPassIds.empty() ||
         pendingSnapshotPublishBox_->unpublishedCandidate ||
         !pendingSnapshotPublishBox_->queuedWrapJournals.empty());
    if (!recordPublishInFlight) {
      return false;
    }
  }

  if (!pendingSnapshotPublishBox_) {
    pendingSnapshotPublishBox_ = std::make_unique<PendingSnapshotPublishBox>();
  }
  PendingSnapshotPublishBox& box = *pendingSnapshotPublishBox_;
  box.sessionExitQueued = true;
  box.fromLoadedPasses = false;
  box.loadedHydrationPhase = LoadedSnapshotHydrationPhase::None;
  box.sessionExitReason = reason;
  for (const EditPassId companionId : companionIds) {
    if (companionId == kInvalidEditPassId) {
      continue;
    }
    bool alreadyQueued = false;
    for (const EditPassId existing : box.companionIds) {
      if (existing == companionId) {
        alreadyQueued = true;
        break;
      }
    }
    if (!alreadyQueued) {
      box.companionIds.push_back(companionId);
    }
  }

  if (isOverdubSessionExit(reason)) {
    const auto* receipt = oldestPendingSessionExitReceipt();
    const auto& sessionPassIds = receipt ? receipt->passIds : overdubSessionPassIds_;
    const auto& sessionCompanionIds = receipt ? receipt->companionIds : overdubSessionCompanionIds_;
    const size_t sessionCursor = receipt ? receipt->companionRowCount : overdubSessionCursor_;
    for (const PassId passId : sessionPassIds) {
      if (passId != kInvalidPassId && !passAlreadyTracked(box, passId)) {
        box.queuedPassIds.push_back(passId);
      }
    }
    for (size_t i = 0; i < sessionCursor && i < sessionCompanionIds.size(); ++i) {
      for (const EditPassId companionId : sessionCompanionIds[i]) {
        if (companionId == kInvalidEditPassId) {
          continue;
        }
        bool alreadyQueued = false;
        for (const EditPassId existing : box.companionIds) {
          if (existing == companionId) {
            alreadyQueued = true;
            break;
          }
        }
        if (!alreadyQueued) {
          box.companionIds.push_back(companionId);
        }
      }
    }
  }
  const auto* pendingReceipt = oldestPendingSessionExitReceipt();
  const PassId finalPassId = pendingReceipt ? pendingReceipt->lastPassId : lastCommittedPassId_;
  if (finalPassId != kInvalidPassId && !passAlreadyTracked(box, finalPassId)) {
    box.queuedPassIds.push_back(finalPassId);
  }
  box.companionsApplied = box.companionIds.empty();
  // DEC-073: this enumeration reflects the committed-pass inventory as of now, so the box's
  // coverage watermark advances to it. Only an actual enumeration may advance coverage; a request
  // that arrives later without being enumerated here leaves the watermark behind.
  box.coveredPassRevision = committedPassRevision();

  if (box.construction || !box.queuedPassIds.empty() || box.unpublishedCandidate ||
      !box.companionIds.empty() || !box.queuedWrapJournals.empty()) {
    return true;
  }
  clearPendingSnapshotPublish();
  return false;
}

LOOP_COLD_MEM bool Loop::tryPublishSessionExitSnapshot(CommitReason reason,
                                                       const EditPassIdList& companionIds) {
  if (!queueSessionExitSnapshot(reason, companionIds)) {
    return false;
  }
  while (stepPendingSnapshotPublish(64)) {
  }
  return publishedSnapshotIsCurrent();
}

LOOP_COLD_MEM bool Loop::stepPendingSnapshotPublish(size_t maximumChanges,
                                                    bool allowStartConstruction,
                                                    uint32_t playheadPhaseTick,
                                                    uint32_t liveRecordCompletedTicks) {
#if defined(SESSION_CAPTURE)
  resetSnapshotIdleChildTimes();
#endif
  if (maximumChanges == 0) {
    return hasPendingSnapshotPublish();
  }
  if (hasPendingSessionExitReceipt()) {
    if (!allowStartConstruction) return true;
    // Consume the oldest receipt only: the ring may hold successors still being
    // finalised, and each publisher turn handles exactly one.
    SessionExitSnapshotHandoff& receipt =
        sessionExitReceipts_[sessionExitReceiptHead_ % kSessionExitReceiptCapacity];
    // DEC-068: an interval-level tail is only consumable once that interval's final pass has
    // committed and its id is filled. Skip it in place so FIFO order is preserved.
    if (!receipt.ready) return true;
    if (!queueSessionExitSnapshot(receipt.reason) && !publishedSnapshotIsCurrent()) return true;
    receipt.pending = false;
    receipt.passIds.clear();
    receipt.companionIds.clear();
    ++sessionExitReceiptHead_;
    if (sessionExitReceiptHead_ >= kSessionExitReceiptCapacity) sessionExitReceiptHead_ = 0;
    --sessionExitReceiptCount_;
    // Queue assembly and construction must not share a turn. Full idle
    // publisher allocation/timing bounds remain a separate integration gate.
    return true;
  }
  if (!pendingSnapshotPublishBox_) {
    if (!hasPendingSnapshotPublish()) {
      return false;
    }
    pendingSnapshotPublishBox_ = std::make_unique<PendingSnapshotPublishBox>();
  }
  PendingSnapshotPublishBox& box = *pendingSnapshotPublishBox_;
  {
#if defined(SESSION_CAPTURE)
    SnapshotIdleChildAdd timed(s_lastSnapshotIdleChildren.discardUs);
#endif
    discardPendingIfLoopLengthChanged(box, loopLengthTicks);
  }
  if (allowStartConstruction) {
#if defined(SESSION_CAPTURE)
    SnapshotIdleChildAdd timed(s_lastSnapshotIdleChildren.enqueueUs);
#endif
    enqueueLiveOverdubPasses();
  }

  size_t remaining = maximumChanges;
  bool steppedThisCall = false;
  bool readyToFinish = false;
  while (remaining > 0) {
    // DEC-073 RC-PUB-1: every turn that could start, step or freeze a candidate is admitted
    // first. A denial breaks out with nothing written, so the publication stays pending and the
    // previous root stays selected. Publishing an already-frozen candidate happens after this
    // loop and is deliberately not gated: it allocates no new content memory.
    if (!admitSnapshotPublicationTurn(box)) {
      break;
    }
    if (!box.construction) {
      // A persistent representability failure parked publication: the previous
      // root stays selected and the same impossible fragment is not rebuilt.
      if (box.fragmentBlocked) {
        break;
      }
      bool started = false;
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
      const uint32_t heapBefore = MemoryMonitor::getInternalHeapFreeBytes();
#endif
      {
#if defined(SESSION_CAPTURE)
        SnapshotIdleChildAdd timed(s_lastSnapshotIdleChildren.startUs);
#endif
        started = allowStartConstruction &&
                  startNextPendingConstruction(*this, box, liveRecordCompletedTicks);
      }
      if (!started) {
        break;
      }
      if (box.fromLoadedPasses) {
        box.loadedHydrationPhase = LoadedSnapshotHydrationPhase::Constructing;
      }
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
      emitSnapshotUnitCapture(*this, "start", heapBefore);
#endif
      // One start per step. Do not apply/freeze in the same 748-byte frame
      // (`010922` reset after freeze `nodes=1754`).
      break;
    }
    if (box.fromLoadedPasses && box.construction->complete() && !steppedThisCall) {
      readyToFinish = true;
      break;
    }
    const size_t before = box.construction->completedUnitCount();
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
    const uint32_t heapBefore = MemoryMonitor::getInternalHeapFreeBytes();
#endif
    {
#if defined(SESSION_CAPTURE)
      SnapshotIdleChildAdd timed(s_lastSnapshotIdleChildren.stepUs);
#endif
      box.construction->step(remaining, playheadPhaseTick);
    }
    steppedThisCall = true;
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
    emitSnapshotUnitStepCapture(*this, heapBefore);
#endif
    if (box.construction->failed()) {
      const LoopSnapshotFailureReason reason = box.construction->failureReason();
      // Discard the partially constructed candidate; authoritative progress is
      // untouched because the fragment was built on the working copy.
      noteFragmentIncorporationFailure(reason, box);
      box.construction.reset();
      box.fragmentProgressValid = false;
      if (!box.passChangeProgress.active) {
        box.constructionPassId = kInvalidPassId;
      }
      box.wrapJournalInFlight = false;
      return hasPendingSnapshotPublish();
    }
    const size_t after = box.construction->completedUnitCount();
    if (after <= before) {
      break;
    }
    remaining -= (after - before);
    if (!box.construction->complete()) {
      break;
    }
    if (box.fromLoadedPasses) {
      break;
    }
    readyToFinish = true;
    break;
  }

  if (readyToFinish && box.construction) {
    const bool wrapping = box.wrapJournalInFlight;
    const uint8_t fragmentIndex =
        wrapping && !box.queuedWrapJournals.empty()
            ? box.queuedWrapJournals.front().fragmentIndex
            : 0;
    const uint8_t fragmentCount =
        wrapping && !box.queuedWrapJournals.empty()
            ? box.queuedWrapJournals.front().fragmentCount
            : 1;
    const uint32_t wrapTargetRevision =
        wrapping && !box.queuedWrapJournals.empty()
            ? box.queuedWrapJournals.front().targetRevision
            : 0;
    bool finished = false;
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
    const uint32_t heapBefore = MemoryMonitor::getInternalHeapFreeBytes();
#endif
    {
#if defined(SESSION_CAPTURE)
      SnapshotIdleChildAdd timed(s_lastSnapshotIdleChildren.finishUs);
#endif
      finished = finishPendingConstruction(box);
    }
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
    emitSnapshotUnitCapture(*this, "finish", heapBefore);
#endif
    if (!finished) {
      // Freeze/allocation failure. The partial candidate is discarded and
      // authoritative progress is untouched (fragmentProgressValid cleared in
      // finishPendingConstruction), so the exact fragment is retried.
      // DEC-073 RC-PUB-2: count it, so a freeze that keeps failing reaches the park bound too
      // instead of being retried forever without ever incrementing a failure.
      noteFragmentIncorporationFailure(LoopSnapshotFailureReason::Allocation, box);
      box.construction.reset();
      box.fragmentProgressValid = false;
      if (!box.passChangeProgress.active) {
        box.constructionPassId = kInvalidPassId;
      }
      box.wrapJournalInFlight = false;
      return hasPendingSnapshotPublish();
    }
    if (wrapping && fragmentIndex + 1 == fragmentCount && wrapTargetRevision != 0 &&
        box.unpublishedCandidate && !remainingWrapPublishWork(box)) {
#if defined(SESSION_CAPTURE)
      SnapshotIdleChildAdd timed(s_lastSnapshotIdleChildren.publishUs);
#endif
      // DEC-073: a wrap candidate carries its own target revision as coverage, so it publishes as
      // an intermediate root when the live revision has already moved past the wrap seal.
      storePublishedSnapshot(box.unpublishedCandidate, wrapTargetRevision, wrapTargetRevision);
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
      emitSnapshotUnitCapture(*this, "publish", heapBefore);
#endif
    }
  }

  if (box.sessionExitQueued && !box.construction && box.queuedWrapJournals.empty() &&
      !remainingSessionExitPublishWork(box)) {
    if (!box.unpublishedCandidate) {
      clearPendingSnapshotPublish();
      return false;
    }
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
    const uint32_t heapBefore = MemoryMonitor::getInternalHeapFreeBytes();
#endif
    {
#if defined(SESSION_CAPTURE)
      SnapshotIdleChildAdd timed(s_lastSnapshotIdleChildren.publishUs);
#endif
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_SNAPSHOT_IDLE
      const uint32_t oldRoot = publishedSnapshotRevision();
#endif
      // DEC-073: publish the candidate only for the coverage it was actually enumerated from. If a
      // newer request was coalesced into the queue without this candidate incorporating it, the
      // watermark stays behind, this becomes an allowed intermediate root, and `publishedSnapshot-
      // IsCurrent()` is false so the newer request stays authoritative.
      storePublishedSnapshot(box.unpublishedCandidate, 0, box.coveredPassRevision);
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_SNAPSHOT_IDLE
      InvestigationCaptureLog::logSessionExitPostStopPublish(
          oldRoot, publishedSnapshotRevision(), playbackRevision);
#endif
    }
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
    emitSnapshotUnitCapture(*this, "publish", heapBefore);
#endif
    const uint32_t attachEntryId = box.sessionExitUndoAttachEntryId;
    const ImmutableLoopSnapshotRef published = publishedSnapshot();
    clearPendingSnapshotPublish();
    notifySessionExitPublishedUndoAttach(published, attachEntryId);
    return false;
  }
  return hasPendingSnapshotPublish();
}

LOOP_COLD_MEM bool Loop::tryPublishNoteEditSnapshot(const EditPassIdList& editPassIds) {
  const ImmutableLoopSnapshotRef parent = publishedSnapshot();
  if (!parent || editPassIds.empty() || loopLengthTicks == 0) {
    return false;
  }

  LoopSnapshotChangeSet changes;
  for (const EditPassId id : editPassIds) {
    const EditPass* row = findActiveEditPass(*this, id);
    if (row == nullptr) {
      return false;
    }
    if (!appendEditRowChange(*parent, *row, loopId, changes)) {
      return false;
    }
  }
  if (changes.notes.empty() && changes.events.empty()) {
    return false;
  }

  LoopSnapshotConstruction construction(parent, changes);
  while (!construction.complete() && !construction.failed()) {
    if (!construction.step(64)) {
      break;
    }
  }
  if (!construction.complete() || construction.failed()) {
    return false;
  }
  ImmutableLoopSnapshotRef frozen = construction.freeze();
  if (!frozen) {
    return false;
  }
  // DEC-073: a note-edit publication is constructed and published in one turn from the live state,
  // so its coverage is the current committed-pass revision.
  storePublishedSnapshot(std::move(frozen), 0, kSnapshotCoverageCurrent);
  return true;
}
