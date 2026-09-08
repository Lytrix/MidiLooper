//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"

#include "CommittedEventRange.h"
#include "LoopSnapshotBoxes.h"
#include "Utils/LoopMem.h"
#include "Utils/NoteUtils.h"

#include <utility>
#include <vector>

#if defined(SESSION_CAPTURE) && defined(ARDUINO)
#include "Utils/DebugSessionCapture.h"
#include "Utils/InvestigationCaptureLog.h"
#include "Utils/MemoryMonitor.h"

#include <cstdio>
#endif

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
}

bool midiEventMatchesSnapshot(const MidiEvent& existing, const MidiEvent& incoming) {
  if (existing.tick != incoming.tick || existing.type != incoming.type ||
      existing.channel != incoming.channel) {
    return false;
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

bool passIdInList(const std::vector<PassId>& passIds, PassId passId) {
  for (const PassId id : passIds) {
    if (id == passId) {
      return true;
    }
  }
  return false;
}

bool isCommittedOverdubPass(const Loop& loop, PassId passId) {
  bool isOverdubPass = false;
  return committedChunksForPass(loop, passId, isOverdubPass) != nullptr && isOverdubPass;
}

bool passAlreadyTracked(const PendingSnapshotPublishBox& box, PassId passId) {
  return box.constructionPassId == passId || passIdInList(box.queuedPassIds, passId) ||
         passIdInList(box.appliedPassIds, passId) || passIdInList(box.journaledPassIds, passId) ||
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
  // One detailed-window of extra bars so later completed bars append instead of
  // rebuilding the whole prefix on each length step (session_20260826_121754).
  constexpr uint32_t kExtraBars = 16;
  const uint32_t withRoom = completedTicks + kExtraBars * Config::TICKS_PER_BAR;
  return loopLengthTicks > withRoom ? loopLengthTicks : withRoom;
}

bool pendingSnapshotFitsSealedLength(const PendingSnapshotPublishBox& box,
                                     uint32_t sealedLength) {
  if (sealedLength == 0 || box.appliedLiveRecordCompletedTicks == 0 ||
      box.appliedLiveRecordCompletedTicks > sealedLength) {
    return false;
  }
  if (box.unpublishedCandidate &&
      box.unpublishedCandidate->loopLengthTicks() < sealedLength) {
    return false;
  }
  if (box.construction &&
      box.construction->candidate().loopLengthTicks() < sealedLength) {
    return false;
  }
  return true;
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
  box.applyingCompanions = false;
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

LOOP_COLD_MEM bool prepareLiveRecordWindow(Loop& loop, PendingSnapshotPublishBox& box,
                                           const LoopSnapshot& parent,
                                           uint32_t liveRecordCompletedTicks) {
  const uint32_t completedTicks = liveRecordCompletedOrOneBar(liveRecordCompletedTicks);
  if (box.unpublishedCandidate &&
      box.unpublishedCandidate->loopLengthTicks() < completedTicks) {
    return false;
  }
  const uint32_t previousCompletedTicks = box.appliedLiveRecordCompletedTicks;
  uint32_t targetCompletedTicks = completedTicks;
  if (targetCompletedTicks > previousCompletedTicks + Config::TICKS_PER_BAR) {
    targetCompletedTicks = previousCompletedTicks + Config::TICKS_PER_BAR;
  }
  const uint32_t includeUntil = targetCompletedTicks < parent.loopLengthTicks()
                                    ? targetCompletedTicks
                                    : parent.loopLengthTicks();
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
  box.applyingCompanions = false;
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
  PendingPassChangeProgress& progress = box.passChangeProgress;
  LoopSnapshotChangeSet changes;
  uint32_t affectedBars = 0;
  uint32_t membershipEntries = 0;
  if (!appendNextPassChangeFragment(loop, *parent, progress, changes, affectedBars,
                                    membershipEntries, box.fromLoadedPasses)) {
    box.persistLiveRecordCursor();
    return false;
  }
  box.persistLiveRecordCursor();
  if (changes.notes.empty() && changes.events.empty()) {
    if (progress.complete()) {
      if (progress.passId != kInvalidPassId) {
        box.appliedPassIds.push_back(progress.passId);
      }
      box.constructionPassId = kInvalidPassId;
      progress.reset();
    }
    return progress.active || !box.queuedPassIds.empty();
  }
  {
#if defined(SESSION_CAPTURE)
    SnapshotIdleChildAdd timed(s_lastSnapshotIdleChildren.constructUs);
#endif
    box.construction = std::make_unique<LoopSnapshotConstruction>(parent, changes);
  }
  box.constructionPassId = progress.passId;
  box.applyingCompanions = false;
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

LOOP_COLD_MEM ImmutableLoopSnapshotRef resolvePendingParent(const Loop& loop,
                                                            const PendingSnapshotPublishBox& box,
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
      if (box.unpublishedCandidate->loopLengthTicks() >= completedTicks) {
        return box.unpublishedCandidate;
      }
      return {};
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
    box.applyingCompanions = false;
    return true;
  }

  const ImmutableLoopSnapshotRef parent =
      resolvePendingParent(loop, box, liveRecordCompletedTicks);
  if (!parent) {
    return false;
  }

  if (box.passChangeProgress.active) {
    if (box.passChangeProgress.fromCaptureStore &&
        box.passChangeProgress.captureWindowConsumed()) {
      if (!prepareLiveRecordWindow(loop, box, *parent, liveRecordCompletedTicks)) {
        return false;
      }
    }
    return startNextPassChangeFragment(loop, box, parent);
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
    box.applyingCompanions = false;
    return startNextPassChangeFragment(loop, box, parent);
  }

  if (!box.sessionExitQueued && loop.capture.phase == CapturePhase::Record &&
      loop.loopLengthTicks > 0 && !loop.publishedSnapshot()) {
    if (!prepareLiveRecordWindow(loop, box, *parent, liveRecordCompletedTicks)) {
      return false;
    }
    return startNextPassChangeFragment(loop, box, parent);
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
    box.applyingCompanions = true;
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

LOOP_COLD_MEM __attribute__((noinline)) bool finishPendingConstruction(
    PendingSnapshotPublishBox& box) {
  ImmutableLoopSnapshotRef frozen = box.construction->freeze();
  if (!frozen) {
    return false;
  }
  box.unpublishedCandidate = std::move(frozen);
  if (box.wrapJournalInFlight && !box.queuedWrapJournals.empty()) {
    box.queuedWrapJournals.erase(box.queuedWrapJournals.begin());
  }
  const PassId finishedPassId = box.constructionPassId;
  box.construction.reset();
  box.wrapJournalInFlight = false;
  box.applyingCompanions = false;
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
}

LOOP_COLD_MEM void Loop::discardUnpublishedSnapshotIfNotSessionExit() {
  if (!pendingSnapshotPublishBox_ || pendingSnapshotPublishBox_->sessionExitQueued) {
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
  return pendingSnapshotPublishBox_ && pendingSnapshotPublishBox_->sessionExitQueued;
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
  if (!pendingSnapshotPublishBox_) {
    pendingSnapshotPublishBox_ = std::make_unique<PendingSnapshotPublishBox>();
  }
  EditPassIdList& ids = pendingSnapshotPublishBox_->companionIds;
  for (const EditPassId existing : ids) {
    if (existing == id) {
      return;
    }
  }
  ids.push_back(id);
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
  if (!pendingSnapshotPublishBox_ || !pendingSnapshotPublishBox_->unpublishedCandidate) {
    return true;
  }
  if (pendingSnapshotPublishBox_->sessionExitQueued) {
    return true;
  }
  return pendingSnapshotPublishBox_->unpublishedCandidate->loopLengthTicks() >= completedTicks;
}

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
  pendingSnapshotPublishBox_ = std::make_unique<PendingSnapshotPublishBox>();
  PendingSnapshotPublishBox& box = *pendingSnapshotPublishBox_;
  box.sessionExitQueued = true;
  box.fromLoadedPasses = true;
  box.loadedHydrationPhase = LoadedSnapshotHydrationPhase::Queued;
  box.sessionExitReason = CommitReason::RecordStop;
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Active &&
      !passes.recordPass.committedChunkIds.empty() &&
      passes.recordPass.id != kInvalidPassId) {
    box.queuedPassIds.push_back(passes.recordPass.id);
  }
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.state != CapturePassState::Active || pass.committedChunkIds.empty() ||
        pass.id == kInvalidPassId) {
      continue;
    }
    box.queuedPassIds.push_back(pass.id);
  }
  for (const EditPass& edit : passes.editPasses) {
    if (edit.state == EditPassState::Active && edit.id != kInvalidEditPassId) {
      box.companionIds.push_back(edit.id);
    }
  }
  box.companionsApplied = box.companionIds.empty();
  if (box.queuedPassIds.empty() && box.companionIds.empty()) {
    clearPendingSnapshotPublish();
    return false;
  }
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
  if (box.passChangeProgress.fromCaptureStore) {
    box.persistLiveRecordCursor();
    box.passChangeProgress.reset();
  }
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
    for (const PassId passId : overdubSessionPassIds_) {
      if (passId != kInvalidPassId && !passAlreadyTracked(box, passId)) {
        box.queuedPassIds.push_back(passId);
      }
    }
    for (size_t i = 0; i < overdubSessionCursor_ && i < overdubSessionCompanionIds_.size();
         ++i) {
      for (const EditPassId companionId : overdubSessionCompanionIds_[i]) {
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
  if (lastCommittedPassId_ != kInvalidPassId && !passAlreadyTracked(box, lastCommittedPassId_)) {
    box.queuedPassIds.push_back(lastCommittedPassId_);
  }
  box.companionsApplied = box.companionIds.empty();

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
    if (!box.construction) {
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
    emitSnapshotUnitCapture(*this, "step", heapBefore);
#endif
    if (box.construction->failed()) {
      box.construction.reset();
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
      box.construction.reset();
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
      storePublishedSnapshot(box.unpublishedCandidate, wrapTargetRevision);
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
      storePublishedSnapshot(box.unpublishedCandidate);
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_SNAPSHOT_IDLE
      InvestigationCaptureLog::logSessionExitPostStopPublish(
          oldRoot, publishedSnapshotRevision(), playbackRevision);
#endif
    }
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
    emitSnapshotUnitCapture(*this, "publish", heapBefore);
#endif
    clearPendingSnapshotPublish();
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
  storePublishedSnapshot(std::move(frozen));
  return true;
}
