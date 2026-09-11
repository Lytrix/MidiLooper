//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "StorageLoopIo.h"

#if defined(ARDUINO)
#include <Arduino.h>
#define LSV1_IO_MEM FLASHMEM
#else
#define LSV1_IO_MEM
#endif

#include "Globals.h"
#include "LoopContentResolution.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr char kLoopSnapshotBodyFormatIdentifier[4] = {'L', 'S', 'V', '1'};
constexpr uint16_t kLoopSnapshotBodyFormatVersion = 1;
constexpr uint16_t kLsv1HeaderFlagHasPersistedLoopSnapshot = 0x0001u;

constexpr size_t kLsv1BodyHeaderBytes = 8;
constexpr size_t kLsv1LoopMetadataPayloadBytes = 32;
constexpr size_t kLsv1LoopSnapshotRootHeaderBytes = 20;
constexpr size_t kLsv1LoopSnapshotNoteRecordBytes = 20;
constexpr size_t kLsv1LoopSnapshotEventRecordPrefixBytes = 8;
constexpr size_t kLsv1LoopSnapshotEventRecordBytes =
    kLsv1LoopSnapshotEventRecordPrefixBytes + kLsv1LoopSnapshotMidiEventBytes;

constexpr uint8_t kLsv1MaxSectionNameLength = 64;
constexpr uint32_t kLsv1MaxSectionPayloadBytes = 64u * 1024u * 1024u;
constexpr uint32_t kLsv1MaxSnapshotObjects = 1'048'576u;
constexpr uint32_t kLsv1MaxMembershipRefsPerBar = 4096u;

constexpr char kLsv1SectionLoopMetadata[] = "LoopMetadata";
constexpr char kLsv1SectionPersistedLoopSnapshot[] = "PersistedLoopSnapshot";
constexpr char kLsv1SectionLoopSnapshot[] = "LoopSnapshot";

#if defined(PIO_UNIT_TEST_NATIVE)
uint32_t g_testRebuildSlotFromLoopContentCallCount = 0;
#endif

bool lsv1IoWrite(const StorageIo& io, const void* data, size_t size) {
  return io.write && io.write(data, size);
}

bool checkedSizeMul(size_t count, size_t elementBytes, size_t& out) {
  if (count != 0 && elementBytes > (SIZE_MAX / count)) {
    return false;
  }
  out = count * elementBytes;
  return true;
}

bool checkedSizeAdd(size_t left, size_t right, size_t& out) {
  if (left > SIZE_MAX - right) {
    return false;
  }
  out = left + right;
  return true;
}

uint32_t maxBarIndexForLoopLength(uint32_t loopLengthTicks) {
  if (loopLengthTicks == 0 || Config::TICKS_PER_BAR == 0) {
    return 0;
  }
  return (loopLengthTicks - 1u) / Config::TICKS_PER_BAR;
}

bool isPersistableMidiEvent(const MidiEvent& event) {
  if (event.type == midi::SystemExclusive) {
    return event.data.sysexData.data == nullptr;
  }
  return true;
}

bool midiEventBytesValidOnRead(const MidiEvent& event) {
  if (event.type == midi::SystemExclusive) {
    const uintptr_t pointerValue =
        reinterpret_cast<uintptr_t>(event.data.sysexData.data);
    return pointerValue == 0;
  }
  return true;
}

struct Lsv1GatheredNote {
  NoteId noteId = kInvalidNoteId;
  uint8_t pitch = 0;
  uint8_t velocity = 0;
  uint32_t startTick = 0;
  uint32_t endTick = 0;
};

struct Lsv1NoteMembershipBar {
  uint32_t barIndex = 0;
  std::vector<NoteId> noteIds;
};

struct Lsv1PitchMembership {
  uint8_t pitch = 0;
  std::vector<Lsv1NoteMembershipBar> bars;
};

struct Lsv1GatheredEvent {
  EventId eventId = kInvalidEventId;
  uint32_t canonicalOrder = 0;
  MidiEvent value{};
};

struct Lsv1EventMembershipBar {
  uint32_t barIndex = 0;
  std::vector<EventId> eventIds;
};

struct Lsv1GatheredLoopSnapshotPayload {
  LoopId loopId = kInvalidLoopId;
  uint32_t loopLengthTicks = 0;
  uint32_t loopStartTick = 0;
  uint32_t nextNoteId = 1;
  uint32_t nextEventId = 1;
  std::vector<Lsv1GatheredNote> notes;
  std::vector<Lsv1GatheredEvent> events;
  std::vector<Lsv1PitchMembership> pitchMembership;
  std::vector<Lsv1EventMembershipBar> eventMembership;
};

bool gatherLoopSnapshotPayload(const LoopSnapshot& snapshot,
                               Lsv1GatheredLoopSnapshotPayload& out) {
  out = {};
  out.loopId = snapshot.loopId();
  out.loopLengthTicks = snapshot.loopLengthTicks();
  out.loopStartTick = snapshot.loopStartTick();
  out.nextNoteId = snapshot.nextNoteId();
  out.nextEventId = snapshot.nextEventId();

  const uint32_t loopLengthTicks = out.loopLengthTicks;
  if (loopLengthTicks == 0 || Config::TICKS_PER_BAR == 0) {
    return true;
  }

  const uint32_t barCount =
      (loopLengthTicks + Config::TICKS_PER_BAR - 1u) / Config::TICKS_PER_BAR;
  std::map<NoteId, Lsv1GatheredNote> notesById;
  std::map<EventId, Lsv1GatheredEvent> eventsById;

  for (uint32_t pitch = 0; pitch < kLoopSnapshotPitchCount; ++pitch) {
    Lsv1PitchMembership pitchMembership{};
    pitchMembership.pitch = static_cast<uint8_t>(pitch);
    for (uint32_t barIndex = 0; barIndex < barCount; ++barIndex) {
      const SnapshotMembershipRef<LoopSnapshotNoteRef> membership =
          snapshot.noteMembership(static_cast<uint8_t>(pitch), barIndex);
      if (!membership) {
        continue;
      }
      Lsv1NoteMembershipBar barEntry{};
      barEntry.barIndex = barIndex;
      std::set<NoteId> seenInBar;
      forEachSnapshotMembershipReference(
          membership.get(), [&](const LoopSnapshotNoteRef& reference) {
            if (!reference || reference->noteId == kInvalidNoteId) {
              return;
            }
            if (!seenInBar.insert(reference->noteId).second) {
              return;
            }
            barEntry.noteIds.push_back(reference->noteId);
            if (notesById.find(reference->noteId) == notesById.end()) {
              Lsv1GatheredNote gathered{};
              gathered.noteId = reference->noteId;
              gathered.pitch = reference->pitch;
              gathered.velocity = reference->velocity;
              gathered.startTick = reference->startTick;
              gathered.endTick = reference->endTick;
              notesById.emplace(reference->noteId, gathered);
            }
          });
      if (!barEntry.noteIds.empty()) {
        pitchMembership.bars.push_back(std::move(barEntry));
      }
    }
    if (!pitchMembership.bars.empty()) {
      out.pitchMembership.push_back(std::move(pitchMembership));
    }
  }

  for (uint32_t barIndex = 0; barIndex < barCount; ++barIndex) {
    const SnapshotMembershipRef<LoopSnapshotEventRef> membership =
        snapshot.eventMembership(barIndex);
    if (!membership) {
      continue;
    }
    Lsv1EventMembershipBar barEntry{};
    barEntry.barIndex = barIndex;
    std::set<EventId> seenInBar;
    forEachSnapshotMembershipReference(
        membership.get(), [&](const LoopSnapshotEventRef& reference) {
          if (!reference || reference->eventId == kInvalidEventId) {
            return;
          }
          if (!seenInBar.insert(reference->eventId).second) {
            return;
          }
          barEntry.eventIds.push_back(reference->eventId);
          if (eventsById.find(reference->eventId) == eventsById.end()) {
            Lsv1GatheredEvent gathered{};
            gathered.eventId = reference->eventId;
            gathered.canonicalOrder = reference->canonicalOrder;
            gathered.value = reference->value;
            eventsById.emplace(reference->eventId, gathered);
          }
        });
    if (!barEntry.eventIds.empty()) {
      out.eventMembership.push_back(std::move(barEntry));
    }
  }

  out.notes.reserve(notesById.size());
  for (const auto& entry : notesById) {
    out.notes.push_back(entry.second);
  }
  std::sort(out.notes.begin(), out.notes.end(),
            [](const Lsv1GatheredNote& left, const Lsv1GatheredNote& right) {
              return left.noteId < right.noteId;
            });

  out.events.reserve(eventsById.size());
  for (const auto& entry : eventsById) {
    out.events.push_back(entry.second);
  }
  std::sort(out.events.begin(), out.events.end(),
            [](const Lsv1GatheredEvent& left, const Lsv1GatheredEvent& right) {
              if (left.canonicalOrder != right.canonicalOrder) {
                return left.canonicalOrder < right.canonicalOrder;
              }
              return left.eventId < right.eventId;
            });

  for (const Lsv1GatheredEvent& event : out.events) {
    if (!isPersistableMidiEvent(event.value)) {
      return false;
    }
  }
  return true;
}

bool writeLoopMetadataPayload(const StorageIo& io, const PersistedLoopSnapshot& metadata) {
  if (!lsv1IoWrite(io, &metadata.loopId, sizeof(metadata.loopId))) return false;
  if (!lsv1IoWrite(io, &metadata.startLoopTick, sizeof(metadata.startLoopTick))) return false;
  if (!lsv1IoWrite(io, &metadata.loopLengthTicks, sizeof(metadata.loopLengthTicks))) return false;
  if (!lsv1IoWrite(io, &metadata.loopStartTick, sizeof(metadata.loopStartTick))) return false;
  if (!lsv1IoWrite(io, &metadata.nextPassId, sizeof(metadata.nextPassId))) return false;
  if (!lsv1IoWrite(io, &metadata.nextNoteId, sizeof(metadata.nextNoteId))) return false;
  if (!lsv1IoWrite(io, &metadata.nextMergeSequence, sizeof(metadata.nextMergeSequence))) return false;
  if (!lsv1IoWrite(io, &metadata.lastCommittedPassId, sizeof(metadata.lastCommittedPassId))) {
    return false;
  }
  return true;
}

bool writeLoopSnapshotPayload(const StorageIo& io,
                              const Lsv1GatheredLoopSnapshotPayload& payload) {
  if (!lsv1IoWrite(io, &payload.loopId, sizeof(payload.loopId))) return false;
  if (!lsv1IoWrite(io, &payload.loopLengthTicks, sizeof(payload.loopLengthTicks))) return false;
  if (!lsv1IoWrite(io, &payload.loopStartTick, sizeof(payload.loopStartTick))) return false;
  if (!lsv1IoWrite(io, &payload.nextNoteId, sizeof(payload.nextNoteId))) return false;
  if (!lsv1IoWrite(io, &payload.nextEventId, sizeof(payload.nextEventId))) return false;

  const uint32_t noteCount = static_cast<uint32_t>(payload.notes.size());
  if (!lsv1IoWrite(io, &noteCount, sizeof(noteCount))) return false;
  for (const Lsv1GatheredNote& note : payload.notes) {
    if (!lsv1IoWrite(io, &note.noteId, sizeof(note.noteId))) return false;
    if (!lsv1IoWrite(io, &note.pitch, sizeof(note.pitch))) return false;
    if (!lsv1IoWrite(io, &note.velocity, sizeof(note.velocity))) return false;
    const uint16_t padding = 0;
    if (!lsv1IoWrite(io, &padding, sizeof(padding))) return false;
    if (!lsv1IoWrite(io, &note.startTick, sizeof(note.startTick))) return false;
    if (!lsv1IoWrite(io, &note.endTick, sizeof(note.endTick))) return false;
  }

  const uint32_t eventCount = static_cast<uint32_t>(payload.events.size());
  if (!lsv1IoWrite(io, &eventCount, sizeof(eventCount))) return false;
  for (const Lsv1GatheredEvent& event : payload.events) {
    if (!lsv1IoWrite(io, &event.eventId, sizeof(event.eventId))) return false;
    if (!lsv1IoWrite(io, &event.canonicalOrder, sizeof(event.canonicalOrder))) return false;
    if (!lsv1IoWrite(io, &event.value, kLsv1LoopSnapshotMidiEventBytes)) return false;
  }

  const uint32_t pitchMembershipSectionCount =
      static_cast<uint32_t>(payload.pitchMembership.size());
  if (!lsv1IoWrite(io, &pitchMembershipSectionCount, sizeof(pitchMembershipSectionCount))) {
    return false;
  }
  for (const Lsv1PitchMembership& pitchMembership : payload.pitchMembership) {
    if (!lsv1IoWrite(io, &pitchMembership.pitch, sizeof(pitchMembership.pitch))) return false;
    const uint32_t barEntryCount = static_cast<uint32_t>(pitchMembership.bars.size());
    if (!lsv1IoWrite(io, &barEntryCount, sizeof(barEntryCount))) return false;
    for (const Lsv1NoteMembershipBar& bar : pitchMembership.bars) {
      if (!lsv1IoWrite(io, &bar.barIndex, sizeof(bar.barIndex))) return false;
      const uint32_t refCount = static_cast<uint32_t>(bar.noteIds.size());
      if (!lsv1IoWrite(io, &refCount, sizeof(refCount))) return false;
      if (refCount > 0 &&
          !lsv1IoWrite(io, bar.noteIds.data(), refCount * sizeof(NoteId))) {
        return false;
      }
    }
  }

  const uint32_t eventBarEntryCount = static_cast<uint32_t>(payload.eventMembership.size());
  if (!lsv1IoWrite(io, &eventBarEntryCount, sizeof(eventBarEntryCount))) return false;
  for (const Lsv1EventMembershipBar& bar : payload.eventMembership) {
    if (!lsv1IoWrite(io, &bar.barIndex, sizeof(bar.barIndex))) return false;
    const uint32_t refCount = static_cast<uint32_t>(bar.eventIds.size());
    if (!lsv1IoWrite(io, &refCount, sizeof(refCount))) return false;
    if (refCount > 0 &&
        !lsv1IoWrite(io, bar.eventIds.data(), refCount * sizeof(EventId))) {
      return false;
    }
  }
  return true;
}

bool writeSectionPayload(const StorageIo& io, const char* sectionName,
                         const std::vector<uint8_t>& payload) {
  const size_t nameLength = std::strlen(sectionName);
  if (nameLength == 0 || nameLength > kLsv1MaxSectionNameLength) {
    return false;
  }
  if (payload.size() > kLsv1MaxSectionPayloadBytes) {
    return false;
  }
  const uint8_t nameLengthByte = static_cast<uint8_t>(nameLength);
  if (!lsv1IoWrite(io, &nameLengthByte, 1)) return false;
  if (!lsv1IoWrite(io, sectionName, nameLength)) return false;
  const uint32_t payloadLength = static_cast<uint32_t>(payload.size());
  if (!lsv1IoWrite(io, &payloadLength, sizeof(payloadLength))) return false;
  if (payload.empty()) {
    return true;
  }
  return lsv1IoWrite(io, payload.data(), payload.size());
}

bool writeSectionToBuffer(const std::function<bool(const StorageIo&)>& writePayload,
                          std::vector<uint8_t>& payload) {
  payload.clear();
  StorageIo io{
      [&payload](const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        payload.insert(payload.end(), bytes, bytes + size);
        return true;
      },
      nullptr};
  return writePayload(io);
}

struct BufferReader {
  const uint8_t* data = nullptr;
  size_t size = 0;
  size_t pos = 0;

  bool read(void* out, size_t nbytes) {
    if (pos + nbytes > size) {
      return false;
    }
    std::memcpy(out, data + pos, nbytes);
    pos += nbytes;
    return true;
  }

  size_t remaining() const { return size - pos; }

  bool skip(size_t nbytes) {
    if (pos + nbytes > size) {
      return false;
    }
    pos += nbytes;
    return true;
  }
};

bool readLoopMetadataPayload(BufferReader& reader, PersistedLoopSnapshot& metadata) {
  if (!reader.read(&metadata.loopId, sizeof(metadata.loopId))) return false;
  if (!reader.read(&metadata.startLoopTick, sizeof(metadata.startLoopTick))) return false;
  if (!reader.read(&metadata.loopLengthTicks, sizeof(metadata.loopLengthTicks))) return false;
  if (!reader.read(&metadata.loopStartTick, sizeof(metadata.loopStartTick))) return false;
  if (!reader.read(&metadata.nextPassId, sizeof(metadata.nextPassId))) return false;
  if (!reader.read(&metadata.nextNoteId, sizeof(metadata.nextNoteId))) return false;
  if (!reader.read(&metadata.nextMergeSequence, sizeof(metadata.nextMergeSequence))) return false;
  if (!reader.read(&metadata.lastCommittedPassId, sizeof(metadata.lastCommittedPassId))) {
    return false;
  }
  return true;
}

bool noteTickSane(const Lsv1GatheredNote& note, uint32_t loopLengthTicks) {
  if (loopLengthTicks == 0) {
    return true;
  }
  if (note.endTick >= note.startTick) {
    return note.endTick <= loopLengthTicks + Config::TICKS_PER_BAR;
  }
  return note.endTick < loopLengthTicks;
}

ImmutableLoopSnapshotRef buildLoopSnapshotFromValidatedPayload(
    const Lsv1GatheredLoopSnapshotPayload& payload) {
  const ImmutableLoopSnapshotRef parent = LoopSnapshot::createEmpty(
      payload.loopId, payload.loopLengthTicks, payload.loopStartTick, payload.nextNoteId,
      payload.nextEventId);
  if (!parent) {
    return nullptr;
  }

  LoopSnapshotChangeSet changes;
  for (const Lsv1GatheredNote& note : payload.notes) {
    SnapshotNoteChange change{};
    change.kind = SnapshotChangeKind::Add;
    change.next.loopId = payload.loopId;
    change.next.noteId = note.noteId;
    change.next.pitch = note.pitch;
    change.next.velocity = note.velocity;
    change.next.startTick = note.startTick;
    change.next.endTick = note.endTick;
    changes.notes.push_back(change);
  }
  for (const Lsv1GatheredEvent& event : payload.events) {
    SnapshotEventChange change{};
    change.kind = SnapshotChangeKind::Add;
    change.next.loopId = payload.loopId;
    change.next.eventId = event.eventId;
    change.next.canonicalOrder = event.canonicalOrder;
    change.next.value = event.value;
    changes.events.push_back(change);
  }

  LoopSnapshotConstruction construction(parent, changes);
  while (!construction.complete()) {
    if (construction.failed() || !construction.step(1024)) {
      return nullptr;
    }
  }
  return construction.freeze();
}

bool readLoopSnapshotPayload(BufferReader& reader, Lsv1GatheredLoopSnapshotPayload& payload) {
  payload = {};
  if (!reader.read(&payload.loopId, sizeof(payload.loopId))) return false;
  if (!reader.read(&payload.loopLengthTicks, sizeof(payload.loopLengthTicks))) return false;
  if (!reader.read(&payload.loopStartTick, sizeof(payload.loopStartTick))) return false;
  if (!reader.read(&payload.nextNoteId, sizeof(payload.nextNoteId))) return false;
  if (!reader.read(&payload.nextEventId, sizeof(payload.nextEventId))) return false;

  const uint32_t maxBarIndex = maxBarIndexForLoopLength(payload.loopLengthTicks);

  uint32_t noteCount = 0;
  if (!reader.read(&noteCount, sizeof(noteCount))) return false;
  if (noteCount > kLsv1MaxSnapshotObjects) return false;
  size_t noteTableBytes = 0;
  if (!checkedSizeMul(noteCount, kLsv1LoopSnapshotNoteRecordBytes, noteTableBytes)) return false;
  if (noteTableBytes > reader.remaining()) return false;

  std::set<NoteId> noteIds;
  NoteId maxNoteId = 0;
  payload.notes.reserve(noteCount);
  for (uint32_t index = 0; index < noteCount; ++index) {
    Lsv1GatheredNote note{};
    if (!reader.read(&note.noteId, sizeof(note.noteId))) return false;
    if (!reader.read(&note.pitch, sizeof(note.pitch))) return false;
    if (!reader.read(&note.velocity, sizeof(note.velocity))) return false;
    uint16_t padding = 0;
    if (!reader.read(&padding, sizeof(padding))) return false;
    if (!reader.read(&note.startTick, sizeof(note.startTick))) return false;
    if (!reader.read(&note.endTick, sizeof(note.endTick))) return false;
    if (note.noteId == kInvalidNoteId || note.pitch > 127 || !noteIds.insert(note.noteId).second ||
        !noteTickSane(note, payload.loopLengthTicks)) {
      return false;
    }
    maxNoteId = std::max(maxNoteId, note.noteId);
    payload.notes.push_back(note);
  }

  uint32_t eventCount = 0;
  if (!reader.read(&eventCount, sizeof(eventCount))) return false;
  if (eventCount > kLsv1MaxSnapshotObjects) return false;
  size_t eventTableBytes = 0;
  if (!checkedSizeMul(eventCount, kLsv1LoopSnapshotEventRecordBytes, eventTableBytes)) return false;
  if (eventTableBytes > reader.remaining()) return false;

  std::set<EventId> eventIds;
  EventId maxEventId = 0;
  payload.events.reserve(eventCount);
  for (uint32_t index = 0; index < eventCount; ++index) {
    Lsv1GatheredEvent event{};
    if (!reader.read(&event.eventId, sizeof(event.eventId))) return false;
    if (!reader.read(&event.canonicalOrder, sizeof(event.canonicalOrder))) return false;
    if (!reader.read(&event.value, kLsv1LoopSnapshotMidiEventBytes)) return false;
    if (event.eventId == kInvalidEventId || !eventIds.insert(event.eventId).second ||
        !midiEventBytesValidOnRead(event.value)) {
      return false;
    }
    maxEventId = std::max(maxEventId, event.eventId);
    payload.events.push_back(event);
  }

  if (!payload.notes.empty() && payload.nextNoteId <= maxNoteId) {
    return false;
  }
  if (!payload.events.empty() && payload.nextEventId <= maxEventId) {
    return false;
  }

  uint32_t pitchMembershipSectionCount = 0;
  if (!reader.read(&pitchMembershipSectionCount, sizeof(pitchMembershipSectionCount))) return false;
  if (pitchMembershipSectionCount > kLoopSnapshotPitchCount) return false;
  payload.pitchMembership.reserve(pitchMembershipSectionCount);

  for (uint32_t pitchSection = 0; pitchSection < pitchMembershipSectionCount; ++pitchSection) {
    Lsv1PitchMembership pitchMembership{};
    if (!reader.read(&pitchMembership.pitch, sizeof(pitchMembership.pitch))) return false;
    if (pitchMembership.pitch > 127) return false;
    uint32_t barEntryCount = 0;
    if (!reader.read(&barEntryCount, sizeof(barEntryCount))) return false;
    if (barEntryCount > kLsv1MaxSnapshotObjects) return false;
    pitchMembership.bars.reserve(barEntryCount);
    for (uint32_t barEntryIndex = 0; barEntryIndex < barEntryCount; ++barEntryIndex) {
      Lsv1NoteMembershipBar bar{};
      if (!reader.read(&bar.barIndex, sizeof(bar.barIndex))) return false;
      if (bar.barIndex > maxBarIndex) return false;
      uint32_t refCount = 0;
      if (!reader.read(&refCount, sizeof(refCount))) return false;
      if (refCount > kLsv1MaxMembershipRefsPerBar) return false;
      size_t refBytes = 0;
      if (!checkedSizeMul(refCount, sizeof(NoteId), refBytes)) return false;
      if (refBytes > reader.remaining()) return false;
      bar.noteIds.resize(refCount);
      if (refCount > 0 && !reader.read(bar.noteIds.data(), refBytes)) return false;
      std::set<NoteId> refsInBar;
      for (NoteId noteId : bar.noteIds) {
        if (noteIds.find(noteId) == noteIds.end() || !refsInBar.insert(noteId).second) {
          return false;
        }
      }
      pitchMembership.bars.push_back(std::move(bar));
    }
    payload.pitchMembership.push_back(std::move(pitchMembership));
  }

  uint32_t eventBarEntryCount = 0;
  if (!reader.read(&eventBarEntryCount, sizeof(eventBarEntryCount))) return false;
  if (eventBarEntryCount > kLsv1MaxSnapshotObjects) return false;
  payload.eventMembership.reserve(eventBarEntryCount);
  for (uint32_t barEntryIndex = 0; barEntryIndex < eventBarEntryCount; ++barEntryIndex) {
    Lsv1EventMembershipBar bar{};
    if (!reader.read(&bar.barIndex, sizeof(bar.barIndex))) return false;
    if (bar.barIndex > maxBarIndex) return false;
    uint32_t refCount = 0;
    if (!reader.read(&refCount, sizeof(refCount))) return false;
    if (refCount > kLsv1MaxMembershipRefsPerBar) return false;
    size_t refBytes = 0;
    if (!checkedSizeMul(refCount, sizeof(EventId), refBytes)) return false;
    if (refBytes > reader.remaining()) return false;
    bar.eventIds.resize(refCount);
    if (refCount > 0 && !reader.read(bar.eventIds.data(), refBytes)) return false;
    std::set<EventId> refsInBar;
    for (EventId eventId : bar.eventIds) {
      if (eventIds.find(eventId) == eventIds.end() || !refsInBar.insert(eventId).second) {
        return false;
      }
    }
    payload.eventMembership.push_back(std::move(bar));
  }

  return reader.remaining() == 0;
}

}  // namespace

LoopSnapshotBodyFormatProbeResult LSV1_IO_MEM probeLoopSnapshotBodyFormat(const uint8_t* data,
                                                                          size_t size) {
  if (size < kLsv1BodyHeaderBytes) {
    return LoopSnapshotBodyFormatProbeResult::Truncated;
  }
  if (std::memcmp(data, kLoopSnapshotBodyFormatIdentifier, 4) != 0) {
    return LoopSnapshotBodyFormatProbeResult::Legacy;
  }
  uint16_t formatVersion = 0;
  std::memcpy(&formatVersion, data + 4, sizeof(formatVersion));
  if (formatVersion != kLoopSnapshotBodyFormatVersion) {
    return LoopSnapshotBodyFormatProbeResult::UnsupportedVersion;
  }
  return LoopSnapshotBodyFormatProbeResult::SupportedV1;
}

bool LSV1_IO_MEM writeLoopSnapshotBodyFormatV1SavePreamble(
    const StorageIo& io, const PersistedLoopSnapshot& metadata,
    uint32_t persistedOracleSectionBytes) {
  if (persistedOracleSectionBytes > kLsv1MaxSectionPayloadBytes) {
    return false;
  }

  std::vector<uint8_t> metadataSection;
  if (!writeSectionToBuffer(
          [&](const StorageIo& sectionIo) { return writeLoopMetadataPayload(sectionIo, metadata); },
          metadataSection) ||
      metadataSection.size() != kLsv1LoopMetadataPayloadBytes) {
    return false;
  }

  if (!lsv1IoWrite(io, kLoopSnapshotBodyFormatIdentifier, 4)) return false;
  const uint16_t formatVersion = kLoopSnapshotBodyFormatVersion;
  if (!lsv1IoWrite(io, &formatVersion, sizeof(formatVersion))) return false;
  const uint16_t flags = kLsv1HeaderFlagHasPersistedLoopSnapshot;
  if (!lsv1IoWrite(io, &flags, sizeof(flags))) return false;
  if (!writeSectionPayload(io, kLsv1SectionLoopMetadata, metadataSection)) {
    return false;
  }

  const size_t nameLength = std::strlen(kLsv1SectionPersistedLoopSnapshot);
  const uint8_t nameLengthByte = static_cast<uint8_t>(nameLength);
  if (!lsv1IoWrite(io, &nameLengthByte, 1)) return false;
  if (!lsv1IoWrite(io, kLsv1SectionPersistedLoopSnapshot, nameLength)) return false;
  if (!lsv1IoWrite(io, &persistedOracleSectionBytes, sizeof(persistedOracleSectionBytes))) {
    return false;
  }
  return true;
}

bool LSV1_IO_MEM writeLoopSnapshotBodyFormatV1SaveTail(const StorageIo& io,
                                                        const LoopSnapshot& loopSnapshot) {
  Lsv1GatheredLoopSnapshotPayload gatheredPayload{};
  if (!gatherLoopSnapshotPayload(loopSnapshot, gatheredPayload)) {
    return false;
  }
  std::vector<uint8_t> loopSnapshotSection;
  if (!writeSectionToBuffer(
          [&](const StorageIo& sectionIo) {
            return writeLoopSnapshotPayload(sectionIo, gatheredPayload);
          },
          loopSnapshotSection) ||
      loopSnapshotSection.empty()) {
    return false;
  }
  return writeSectionPayload(io, kLsv1SectionLoopSnapshot, loopSnapshotSection);
}

size_t LSV1_IO_MEM measureLoopSnapshotBodyFormatV1Bytes(
    const PersistedLoopSnapshot& metadata, const PersistedLoopSnapshot& persistedOracle,
    const LoopSnapshot& loopSnapshot) {
  size_t nbytes = 0;
  StorageIo io{
      [&nbytes](const void*, size_t size) {
        nbytes += size;
        return true;
      },
      nullptr};
  if (!writeLoopSnapshotBodyFormatV1(io, metadata, persistedOracle, loopSnapshot)) {
    return 0;
  }
  return nbytes;
}

bool LSV1_IO_MEM writeLoopSnapshotBodyFormatV1(const StorageIo& io,
                                                const PersistedLoopSnapshot& metadata,
                                                const PersistedLoopSnapshot& persistedOracle,
                                                const LoopSnapshot& loopSnapshot) {
  Lsv1GatheredLoopSnapshotPayload gatheredPayload{};
  if (!gatherLoopSnapshotPayload(loopSnapshot, gatheredPayload)) {
    return false;
  }

  std::vector<uint8_t> metadataSection;
  if (!writeSectionToBuffer(
          [&](const StorageIo& sectionIo) { return writeLoopMetadataPayload(sectionIo, metadata); },
          metadataSection) ||
      metadataSection.size() != kLsv1LoopMetadataPayloadBytes) {
    return false;
  }

  std::vector<uint8_t> persistedSection;
  if (!writeSectionToBuffer(
          [&](const StorageIo& sectionIo) {
            return writePersistedLoopSnapshot(sectionIo, persistedOracle);
          },
          persistedSection)) {
    return false;
  }

  std::vector<uint8_t> loopSnapshotSection;
  if (!writeSectionToBuffer(
          [&](const StorageIo& sectionIo) {
            return writeLoopSnapshotPayload(sectionIo, gatheredPayload);
          },
          loopSnapshotSection) ||
      loopSnapshotSection.empty()) {
    return false;
  }

  if (!lsv1IoWrite(io, kLoopSnapshotBodyFormatIdentifier, 4)) return false;
  const uint16_t formatVersion = kLoopSnapshotBodyFormatVersion;
  if (!lsv1IoWrite(io, &formatVersion, sizeof(formatVersion))) return false;
  const uint16_t flags = kLsv1HeaderFlagHasPersistedLoopSnapshot;
  if (!lsv1IoWrite(io, &flags, sizeof(flags))) return false;

  if (!writeSectionPayload(io, kLsv1SectionLoopMetadata, metadataSection)) {
    return false;
  }
  if (!writeSectionPayload(io, kLsv1SectionPersistedLoopSnapshot, persistedSection)) {
    return false;
  }
  if (!writeSectionPayload(io, kLsv1SectionLoopSnapshot, loopSnapshotSection)) {
    return false;
  }
  return true;
}

bool LSV1_IO_MEM readLoopSnapshotBodyFormatV1(const uint8_t* data, size_t size,
                                              LoopSnapshotBodyFormatV1ReadResult& out) {
  out = {};
  const LoopSnapshotBodyFormatProbeResult probe = probeLoopSnapshotBodyFormat(data, size);
  if (probe == LoopSnapshotBodyFormatProbeResult::Legacy) {
    out.legacy = true;
    return false;
  }
  if (probe == LoopSnapshotBodyFormatProbeResult::Truncated) {
    return false;
  }
  if (probe == LoopSnapshotBodyFormatProbeResult::UnsupportedVersion) {
    out.unsupportedVersion = true;
    return false;
  }

  uint16_t flags = 0;
  std::memcpy(&flags, data + 6, sizeof(flags));
  if ((flags & ~kLsv1HeaderFlagHasPersistedLoopSnapshot) != 0) {
    return false;
  }
  BufferReader reader{data, size, kLsv1BodyHeaderBytes};
  const bool expectPersistedOracle = (flags & kLsv1HeaderFlagHasPersistedLoopSnapshot) != 0;

  bool sawLoopMetadata = false;
  bool sawPersistedLoopSnapshot = false;
  bool sawLoopSnapshot = false;
  Lsv1GatheredLoopSnapshotPayload loopSnapshotPayload{};

  while (reader.remaining() > 0) {
    uint8_t nameLength = 0;
    if (!reader.read(&nameLength, sizeof(nameLength))) {
      return false;
    }
    if (nameLength == 0 || nameLength > kLsv1MaxSectionNameLength) {
      return false;
    }
    if (nameLength > reader.remaining()) {
      return false;
    }
    std::string sectionName(reinterpret_cast<const char*>(data + reader.pos), nameLength);
    if (!reader.skip(nameLength)) {
      return false;
    }
    uint32_t payloadByteLength = 0;
    if (!reader.read(&payloadByteLength, sizeof(payloadByteLength))) {
      return false;
    }
    if (payloadByteLength > reader.remaining() || payloadByteLength > kLsv1MaxSectionPayloadBytes) {
      return false;
    }

    const size_t payloadStart = reader.pos;
    const size_t payloadEnd = payloadStart + payloadByteLength;

    if (sectionName == kLsv1SectionLoopMetadata) {
      if (sawLoopMetadata || payloadByteLength != kLsv1LoopMetadataPayloadBytes) {
        return false;
      }
      sawLoopMetadata = true;
      if (!readLoopMetadataPayload(reader, out.metadata)) {
        return false;
      }
    } else if (sectionName == kLsv1SectionPersistedLoopSnapshot) {
      if (sawPersistedLoopSnapshot) {
        return false;
      }
      sawPersistedLoopSnapshot = true;
      PersistedLoopParseState parseState{};
      PersistedLoopParseStepResult parseResult = PersistedLoopParseStepResult::MoreWork;
      while (parseResult == PersistedLoopParseStepResult::MoreWork) {
        parseResult = stepPersistedLoopSnapshotParse(data + payloadStart, payloadByteLength,
                                                     out.persistedOracle, parseState);
      }
      if (parseResult != PersistedLoopParseStepResult::Completed ||
          parseState.pos != payloadByteLength) {
        return false;
      }
      reader.pos = payloadEnd;
      out.hasPersistedOracle = true;
    } else if (sectionName == kLsv1SectionLoopSnapshot) {
      if (sawLoopSnapshot) {
        return false;
      }
      sawLoopSnapshot = true;
      BufferReader payloadReader{data, size, payloadStart};
      if (!readLoopSnapshotPayload(payloadReader, loopSnapshotPayload)) {
        return false;
      }
      reader.pos = payloadEnd;
    } else {
      if (!reader.skip(payloadByteLength)) {
        return false;
      }
    }
  }

  if (!sawLoopMetadata || !sawLoopSnapshot) {
    return false;
  }
  if (expectPersistedOracle && !sawPersistedLoopSnapshot) {
    return false;
  }

  out.loopSnapshot = buildLoopSnapshotFromValidatedPayload(loopSnapshotPayload);
  if (!out.loopSnapshot) {
    return false;
  }
  out.ok = true;
  return true;
}

#if defined(PIO_UNIT_TEST_NATIVE)
uint32_t testRebuildSlotFromLoopContentCallCount() {
  return g_testRebuildSlotFromLoopContentCallCount;
}

void testResetRebuildSlotFromLoopContentCallCount() {
  g_testRebuildSlotFromLoopContentCallCount = 0;
}
#endif
