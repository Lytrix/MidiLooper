//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "PersistenceLayout.h"
#include "CurrentWorkspaceStorage.h"
#include "StorageLoopIo.h"
#include "Globals.h"

namespace CurrentSetStorage {

constexpr uint32_t CONTAINER_VERSION = 6;
constexpr uint32_t kSaveFileToken = 0x45564153UL;  // "SAVE"

/// Legacy flat SavedSet folders (brownfield) under sets/archive until 3.6.
inline constexpr const char* kSetsRoot = PersistenceLayout::kSetsRoot;
inline constexpr const char* kSetsArchiveDir = PersistenceLayout::kSetsArchiveDir;
inline constexpr const char* kSetIndexPath = "/MidiLooper/sets/index.bin";
inline constexpr const char* kSetIndexTempPath = "/MidiLooper/sets/index.bin.tmp";

/// Mutable workspace root — NOT inside sets/.
inline constexpr const char* kCurrentSetDir = PersistenceLayout::kCurrentRoot;
inline constexpr const char* kCurrentSlotsDir = "/MidiLooper/current/slots";

/// Interim deferred-FSM runtime bundle (transport+tracks+undo) until transport.bin/global.bin split.
inline constexpr const char* kCurrentRuntimeBundlePath =
    "/MidiLooper/current/temp/runtime.bundle.bin";
inline constexpr const char* kCurrentRuntimeBundleTempPath =
    "/MidiLooper/current/temp/runtime.bundle.bin.tmp";

/// Legacy alias — SD path is `runtime.bundle.bin` under `current/temp/` (not `set.bin`).
inline constexpr const char* kCurrentMetaPath = kCurrentRuntimeBundlePath;
inline constexpr const char* kCurrentMetaTempPath = kCurrentRuntimeBundleTempPath;

inline constexpr const char* kCheckpointsDir = PersistenceLayout::kCheckpointsDir;
inline constexpr const char* kLegacyMonolithPath = "/midilooper_state.raw";

/// Per-Set metadata filename (flat SavedSet folders and `sets/S####/`).
inline constexpr const char* kSetBinFileName = "set.bin";
inline constexpr const char* kSetBinTempFileName = "set.bin.tmp";

constexpr size_t kMetaPayloadOffset = CurrentWorkspaceStorage::kEpochFileHeaderByteSize;
constexpr size_t kLastActiveUnixOffset = kMetaPayloadOffset + sizeof(uint32_t);
constexpr size_t kAnchorFieldsOffset = kMetaPayloadOffset + sizeof(uint32_t) + sizeof(uint32_t);

struct AnchorFields {
  uint32_t loadedFromSequence = 0;
  uint32_t lastAnchoredSequence = 0;
  uint32_t lastMaterialChangeUnix = 0;
  uint8_t hasMaterialChangesSinceAnchor = 0;
  uint8_t reserved[3] = {0, 0, 0};
};

struct MetaHeader {
  uint32_t containerVersion = CONTAINER_VERSION;
  uint32_t lastActiveUnix = 0;
  AnchorFields anchor{};
};

bool formatLoopSlotPath(char* out, size_t outSize, uint8_t trackIndex, uint8_t slotIndex);
bool formatLoopSlotTempPath(char* out, size_t outSize, uint8_t trackIndex, uint8_t slotIndex);
bool formatLoopSlotSealJournalPath(char* out, size_t outSize, uint8_t trackIndex, uint8_t slotIndex);

bool writeMetaHeader(const StorageIo& io, const MetaHeader& header);
bool readMetaHeader(const StorageIo& io, MetaHeader& header);

bool writeSaveFileToken(const StorageIo& io);
bool verifySaveFileTokenAtEnd(const uint8_t* fileBytes, size_t fileSize);

bool shouldWriteLoopPayloadForSlot(bool forceCurrentSetFullLoopWrite, bool slotDirty);
/// DEC-110: a slot with no content must not keep a persisted payload (an empty body reads as populated to the
/// library observation, which then refuses the slot as a record target). Pure policy over the two facts the
/// caller owns: the slot has no content, and a verified payload exists on the card.
bool shouldRemoveLoopSlotPayloadForSlot(bool slotHoldsNoContent, bool payloadPresentOnSd);

struct LoopSlotPayloadWriteCounts {
  uint16_t writes = 0;
  uint16_t skips = 0;
};

LoopSlotPayloadWriteCounts countLoopSlotPayloadWrites(
    bool forceFullRewrite,
    const bool slotDirty[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK]);

bool shouldAutoSaveBeforeLoadIntoCurrent(const AnchorFields& anchor);

void applyLoadedSetAnchorFields(uint32_t sourceSequence, AnchorFields& anchor);

#if defined(ARDUINO)
/// Open `path` for a **from-scratch** write: any existing file is removed first.
///
/// `SD.open(path, FILE_WRITE)` is `O_RDWR | O_CREAT | O_AT_END` on this platform -- it *appends* --
/// and `FILE_WRITE_BEGIN` is `O_RDWR | O_CREAT`, which only starts at offset 0 because `O_TRUNC` is
/// never set. **Neither mode truncates**, so a leftover file corrupts the next write: the new body
/// lands after the stale prefix, or a stale tail survives past it. For a temp file that is then
/// renamed over a real one, that turns one failed write into a permanently malformed file -- which
/// is how `sets/index.bin` stopped reconciling (the trailing SAVE token is read from the end).
///
/// Use this for every file that is written from its beginning: temps, and copy destinations.
/// Do **not** use it to patch a file in place or to append (a seal journal, a crash log).
File openForRewrite(const char* path);
bool ensureDirectory(const char* path);
bool atomicRenameTempFile(const char* tempPath, const char* finalPath);
bool verifySaveFileTokenAtPath(const char* path);
bool removeLoopSlotSealJournal(uint8_t trackIndex, uint8_t slotIndex);
bool patchLastActiveUnix(const char* metaPath, uint32_t lastActiveUnix);
bool patchAnchorFields(const char* metaPath, const AnchorFields& anchor);
#endif

}  // namespace CurrentSetStorage
