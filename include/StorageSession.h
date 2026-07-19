//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <SD.h>
#include <array>
#include <cstdint>
#include <vector>

#include "CurrentSetStorage.h"
#include "Globals.h"
#include "LooperState.h"
#include "MidiEvent.h"
#include "RevisionLoadPolicy.h"
#include "RevisionPackedBlob.h"
#include "SetBrowserOverlayPolicy.h"
#include "SetRevisionCatalog.h"
#include "StorageManagerInternal/PersistenceWorkQueue.h"
#include "TrackState.h"
#include "Utils/ExternalMemoryFirstAllocator.h"

/// Persistence job stage enums (DEC-012). Shared by StorageManager job FSMs.
enum class DeferredSaveStage : uint8_t {
  Idle = 0,
  CurrentSetMeta,
  TrackHeaderAndSlots,
  CurrentSetLoopSlot,
  Footer,
  UndoStacks,
  CurrentSetCompletion,
};

enum class DeferredGlobalHeaderStage : uint8_t {
  Version = 0,
  Bpm,
  LooperState,
  MasterLoopLength,
  TrackCount,
};

enum class DeferredTrackWriteStage : uint8_t {
  TrackState = 0,
  Muted,
};

enum class DeferredSlotWriteStage : uint8_t {
  SlotEnabled = 0,
  SlotMuted,
  SlotLoopId,
};

enum class DeferredFooterWriteStage : uint8_t {
  SelectedTrack = 0,
  ActiveLoopIndex,
  SelectedSlotExtensionToken,
  SelectedSlotIndex,
  GlobalUndoStackToken,
};

enum class DeferredLoopWriteStage : uint8_t {
  Header = 0,
  CapturePassHeader,
  CapturePassChunk,
  EditTail,
};

enum class LoopPersistPayloadCrc : uint8_t {
  None = 0,
  RevisionCommit,
};

enum class DeferredUndoWriteStage : uint8_t {
  Header = 0,
  EntryHeader,
  BeforeSnapshotPresence,
  BeforeSnapshotLoop,
  AfterSnapshotPresence,
  AfterSnapshotLoop,
  EntryTail,
};

enum class RevisionCommitStage : uint8_t {
  Idle = 0,
  Snapshot,
  Write,
  Validate,
  CatalogUpdate,
  Complete,
};

enum class RevisionWriteStage : uint8_t {
  PrepareLayout = 0,
  OpenTempFile,
  WriteHeader,
  WriteTransportChunk,
  WriteLoopSlotChunks,
  WriteSlotIndexChunk,
  WriteFooter,
};

enum class RevisionLoadStage : uint8_t {
  Idle = 0,
  Validate,
  Write,
  ReloadRam,
  Complete,
};

enum class RevisionLoadWriteStage : uint8_t {
  PrepareEpoch = 0,
  OpenMetaTemp,
  CopyTransportBody,
  FinalizeMetaTemp,
  WriteLoopSlots,
};

enum class RevisionLoadReloadRamStage : uint8_t {
  WriteWorkspaceMeta = 0,
  ReadMetaHeaders,
  LoadLoopSlot,
  ReadFooter,
};

constexpr uint16_t kMaxRevisionLoopIndexEntries =
    static_cast<uint16_t>(Config::NUM_TRACKS * Config::MAX_LOOPS_PER_TRACK);

/// RAM aggregate for active persistence jobs on StorageManager (DEC-012).
struct CurrentWorkspaceSaveJob {
  bool pending = false;
  bool inProgress = false;
  bool sdIoActive = false;
  bool urgentRequested = false;
  DeferredSaveStage stage = DeferredSaveStage::Idle;
  DeferredGlobalHeaderStage globalHeaderStage = DeferredGlobalHeaderStage::Version;
  DeferredTrackWriteStage trackWriteStage = DeferredTrackWriteStage::TrackState;
  DeferredSlotWriteStage slotWriteStage = DeferredSlotWriteStage::SlotEnabled;
  DeferredFooterWriteStage footerWriteStage = DeferredFooterWriteStage::SelectedTrack;
  DeferredLoopWriteStage loopWriteStage = DeferredLoopWriteStage::Header;
  DeferredUndoWriteStage undoWriteStage = DeferredUndoWriteStage::Header;
  LooperState stateSnapshot = LOOPER_IDLE;
  File file;
  File loopFile;
  bool loopFileOpen = false;
  uint8_t numTracks = 0;
  uint8_t trackCursor = 0;
  uint8_t slotCursor = 0;
  uint8_t poolCursor = 0;
  uint8_t undoTrackCursor = 0;
  uint16_t capturePassCursor = 0;
  uint16_t chunkCursor = 0;
  uint32_t undoEntryCursor = 0;
  bool trackHeaderWritten = false;
  uint8_t footerTrackCursor = 0;
  uint32_t startedAtUs = 0;
  uint32_t heapBefore = 0;
  uint32_t admissionHeap = 0;
  bool heapFloorDeferred = false;
  bool lastCompletedOk = false;
  uint32_t completedAtMs = 0;
  uint32_t failedAtMs = 0;
  /// While millis() < this value, skip starting a new full workspace save during transport.
  uint32_t deferDispatchUntilMs = 0;
  std::vector<MidiEvent, ExternalMemoryFirstAllocator<MidiEvent>> midiBatch;
  uint16_t loopSlotsWritten = 0;
  uint16_t loopSlotsSkipped = 0;
  uint32_t displayBlockUs = 0;
  uint32_t workspaceEpoch = 0;
};

struct RevisionCommitJob {
  bool overlayBackgroundCommit = false;
  bool pending = false;
  bool inProgress = false;
  bool sdIoActive = false;
  RevisionCommitStage stage = RevisionCommitStage::Idle;
  RevisionWriteStage writeStage = RevisionWriteStage::PrepareLayout;
  uint32_t sourceEpoch = 0;
  uint32_t workspaceEpochBeforeSnapshot = 0;
  uint16_t setId = 0;
  uint16_t pendingRevisionId = 0;
  char tempPath[80] = {};
  char finalPath[80] = {};
  File file;
  SetRevisionCatalog::SetCatalogIndex catalogIndex{};
  SetRevisionCatalog::SetMetaRecord setMeta{};
  RevisionPackedBlob::RevisionHeader header{};
  RevisionPackedBlob::RevisionLoopSlotDirectoryEntry slotEntries[kMaxRevisionLoopIndexEntries]{};
  uint16_t slotIndexCount = 0;
  uint16_t slotIndexWriteCursor = 0;
  uint32_t payloadWriteOffset = 0;
  uint16_t chunkCount = 0;
  uint8_t copyTrackCursor = 0;
  uint8_t copySlotCursor = 0;
  uint32_t runtimeBundleSize = 0;
  uint32_t runtimeBundleReadPos = 0;
  uint32_t slotReadPos = 0;
  uint32_t slotBodyRemaining = 0;
  bool loopSlotBodyActive = false;
  File sourceFile;
  bool sourceFileOpen = false;
  std::array<uint8_t, 512> copyBuffer{};
  uint32_t payloadCrc = 0;
  bool payloadCrcSeeded = false;
  bool allocatedNewSet = false;
  bool slotIndexChunkWritten = false;
  uint32_t lastBlockedLogAtMs = 0;
};

struct RevisionLoadJob {
  bool requested = false;
  uint16_t requestedSetId = 0;
  uint16_t requestedRevisionId = 0;
  bool heldForWorkspaceDirty = false;
  RevisionLoadPolicy::DirtyPromptChoice confirmChoice =
      RevisionLoadPolicy::DirtyPromptChoice::None;
  bool loadAfterRevisionCommit = false;
  bool pending = false;
  bool inProgress = false;
  bool sdIoActive = false;
  RevisionLoadStage stage = RevisionLoadStage::Idle;
  RevisionLoadWriteStage writeStage = RevisionLoadWriteStage::PrepareEpoch;
  uint16_t setId = 0;
  uint16_t revisionId = 0;
  char sourcePath[80] = {};
  RevisionPackedBlob::RevisionHeader header{};
  uint16_t slotIndexCount = 0;
  RevisionPackedBlob::RevisionLoopSlotDirectoryEntry slotDirectoryEntries[kMaxRevisionLoopIndexEntries]{};
  uint32_t workspaceEpoch = 0;
  uint32_t transportFileOffset = 0;
  uint32_t transportBodySize = 0;
  uint32_t transportReadPos = 0;
  uint8_t copyTrackCursor = 0;
  uint8_t copySlotCursor = 0;
  uint32_t slotBodyRemaining = 0;
  uint32_t slotReadPos = 0;
  File sourceFile;
  bool sourceFileOpen = false;
  File destFile;
  bool destFileOpen = false;
  bool writingEmptySlot = false;
  DeferredLoopWriteStage loopWriteStage = DeferredLoopWriteStage::Header;
  uint32_t lastBlockedLogAtMs = 0;
  bool usedDefaultTransport = false;
  bool displayRefreshPending = false;
  uint32_t completedAtMs = 0;
  uint32_t failedAtMs = 0;
  uint16_t lastDisplaySetId = 0;
  uint16_t lastDisplayRevisionId = 0;
  RevisionLoadReloadRamStage reloadRamStage = RevisionLoadReloadRamStage::WriteWorkspaceMeta;
  File reloadMetaFile;
  bool reloadMetaFileOpen = false;
  uint8_t reloadTrackCursor = 0;
  uint8_t reloadSlotCursor = 0;
  uint8_t reloadNumTracks = 0;
  bool reloadAnySlotHasEvents[Config::NUM_TRACKS] = {};
  TrackState reloadLoadedTrackState[Config::NUM_TRACKS] = {};
  bool reloadMuted[Config::NUM_TRACKS] = {};
  std::vector<uint8_t> reloadActiveLoopIndex;
  std::vector<uint8_t> reloadSelectedSlotIndex;
  uint8_t reloadSelectedTrackIdx = 0;
  LooperState reloadLooperState = LOOPER_IDLE;
  uint32_t reloadMasterLoopLength = 0;
};

struct BootRecoveryJob {
  bool pending = false;
  uint16_t setId = 0;
  uint16_t revisionId = 0;
};

struct MidPassChunkPersistJob {
  bool sdIoActive = false;
  uint8_t trackIndex = 0xFF;
  uint8_t slotIndex = 0xFF;
  File journalFile;
  bool journalOpen = false;
  uint16_t chunksPersisted = 0;
};

struct PersistenceWorkItemJob {
  bool sdIoActive = false;
  bool itemActive = false;
  bool bundleWriteActive = false;
  bool skipLoopSlotStage = false;
  PersistWorkItem item{};
  LooperState stateSnapshot = LOOPER_IDLE;
  uint8_t trackIndex = 0xFF;
  uint8_t slotIndex = 0xFF;
  uint32_t flushStartedAtUs = 0;
  uint32_t flushHeapBefore = 0;
};

struct StorageSession {
  CurrentWorkspaceSaveJob currentWorkspaceSave;
  MidPassChunkPersistJob midPassChunkPersist;
  PersistenceWorkItemJob persistenceWorkItem;
  RevisionCommitJob revisionCommit;
  RevisionLoadJob revisionLoad;
  BootRecoveryJob bootRecovery;
  SetBrowserOverlayPolicy::NavigationState setBrowserNavigation;

  /// Explicit ctor so DMAMEM placement still runs member construction
  /// (.bss.dma is NOLOAD and is not zero-filled at startup).
  StorageSession() = default;
};
