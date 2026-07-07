//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "StorageManagerInternal.h"

#include "CurrentSetStorage.h"
#include "LoopEventStore.h"
#include "MidPassSealJournal.h"
#include "PersistenceFailurePolicy.h"
#include "PersistenceQueue.h"
#include "TrackManager.h"
#include "Utils/DebugSessionCapture.h"
#include <Arduino.h>
#include <cstdio>
#include <vector>

namespace StorageManagerInternal {
namespace {

std::vector<MidiEvent, ExternalMemoryFirstAllocator<MidiEvent>> midPassMidiBatch;

STORAGE_PERSIST_MEM void closeMidPassJournal(MidPassChunkPersistJob& job) {
    if (job.journalOpen) {
        job.journalFile.close();
        job.journalOpen = false;
    }
}

STORAGE_PERSIST_MEM bool resolveActiveCaptureTrackSlot(uint8_t& trackIndexOut, uint8_t& slotIndexOut) {
    for (uint8_t trackIndex = 0; trackIndex < trackManager.getTrackCount(); ++trackIndex) {
        const Track& track = trackManager.getTrack(trackIndex);
        if (track.isRecording() || track.isOverdubbing()) {
            trackIndexOut = trackIndex;
            slotIndexOut = track.getActiveLoopIndex();
            return true;
        }
    }
    return false;
}

STORAGE_PERSIST_MEM bool resolveCaptureTrackSlotForMidPass(uint8_t& trackIndexOut, uint8_t& slotIndexOut) {
    if (resolveActiveCaptureTrackSlot(trackIndexOut, slotIndexOut)) {
        return true;
    }
    const MidPassChunkPersistJob& job = storageSession.midPassChunkPersist;
    if (job.trackIndex != 0xFF && job.slotIndex != 0xFF) {
        trackIndexOut = job.trackIndex;
        slotIndexOut = job.slotIndex;
        return true;
    }
    const uint8_t selectedTrack = trackManager.getSelectedTrackIndex();
    if (selectedTrack < trackManager.getTrackCount()) {
        trackIndexOut = selectedTrack;
        slotIndexOut = trackManager.getTrack(selectedTrack).getActiveLoopIndex();
        return true;
    }
    return false;
}

STORAGE_PERSIST_MEM bool ensureMidPassJournalOpen(MidPassChunkPersistJob& job, uint8_t trackIndex,
                                uint8_t slotIndex) {
    if (job.journalOpen && job.trackIndex == trackIndex && job.slotIndex == slotIndex) {
        return true;
    }

    closeMidPassJournal(job);
    job.trackIndex = trackIndex;
    job.slotIndex = slotIndex;

    if (!CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCurrentSlotsDir)) {
        return false;
    }

    char journalPath[48];
    if (!CurrentSetStorage::formatLoopSlotSealJournalPath(journalPath, sizeof(journalPath), trackIndex,
                                                          slotIndex)) {
        return false;
    }

    const bool exists = SD.exists(journalPath);
    job.journalFile = SD.open(journalPath, exists ? FILE_WRITE : FILE_WRITE | O_CREAT);
    if (!job.journalFile) {
        return false;
    }
    if (!exists) {
        MidPassSealJournal::FileHeader header{};
        header.trackIndex = trackIndex;
        header.slotIndex = slotIndex;
        if (job.journalFile.write(reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header)) {
            closeMidPassJournal(job);
            return false;
        }
    } else if (!job.journalFile.seek(job.journalFile.size())) {
        closeMidPassJournal(job);
        return false;
    }
    job.journalOpen = true;
    return true;
}

STORAGE_PERSIST_MEM bool appendMidPassJournalRecord(File& file, uint16_t chunkId, uint32_t sealSequence) {
    midPassMidiBatch.clear();
    LoopEventStore::appendChunkRefEvent(chunkId, midPassMidiBatch);
    MidPassSealJournal::RecordHeader record{};
    record.chunkId = chunkId;
    record.sealSequence = sealSequence;
    record.eventCount = static_cast<uint16_t>(midPassMidiBatch.size());
    if (file.write(reinterpret_cast<uint8_t*>(&record), sizeof(record)) != sizeof(record)) {
        return false;
    }
    if (record.eventCount == 0) {
        return true;
    }
    return file.write(reinterpret_cast<uint8_t*>(midPassMidiBatch.data()),
                    record.eventCount * sizeof(MidiEvent)) ==
           record.eventCount * sizeof(MidiEvent);
}

}  // namespace

STORAGE_PERSIST_MEM void resetMidPassChunkPersistState() {
    MidPassChunkPersistJob& job = storageSession.midPassChunkPersist;
    closeMidPassJournal(job);
    job.sdIoActive = false;
    job.trackIndex = 0xFF;
    job.slotIndex = 0xFF;
    job.chunksPersisted = 0;
}

STORAGE_PERSIST_MEM bool stepMidPassChunkPersist() {
#if BYPASS_STOP_UNDO_SAVE
    return true;
#else
    if (PersistenceQueue::queueDepth() == 0 && PersistenceQueue::writingChunkCount() == 0) {
        return true;
    }

    uint16_t chunkId = 0;
    if (!PersistenceQueue::beginWriteQueuedChunk(chunkId)) {
        return true;
    }

    uint8_t trackIndex = 0;
    uint8_t slotIndex = 0;
    if (!resolveCaptureTrackSlotForMidPass(trackIndex, slotIndex)) {
        PersistenceQueue::requeueWritingChunk(chunkId);
        return true;
    }

    MidPassChunkPersistJob& job = storageSession.midPassChunkPersist;
    if (!ensureMidPassJournalOpen(job, trackIndex, slotIndex)) {
        PersistenceQueue::requeueWritingChunk(chunkId);
        return false;
    }

    const uint32_t sealSequence = PersistenceQueue::sealSequenceForChunk(chunkId);
    if (!appendMidPassJournalRecord(job.journalFile, chunkId, sealSequence)) {
        PersistenceQueue::requeueWritingChunk(chunkId);
        return false;
    }

    PersistenceQueue::markChunkPersisted(chunkId);
    ++job.chunksPersisted;

#if defined(SESSION_CAPTURE)
    char outcome[48];
    std::snprintf(outcome, sizeof(outcome), "ok:t%u:s%u:c%u:n%u", static_cast<unsigned>(trackIndex),
                  static_cast<unsigned>(slotIndex), static_cast<unsigned>(chunkId),
                  static_cast<unsigned>(midPassMidiBatch.size()));
    SC_PERSIST("mid_pass", 0, sealSequence, chunkId, outcome);
#endif
    return true;
#endif
}

}  // namespace StorageManagerInternal
