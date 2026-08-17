//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// DEC-037 — query-time effective musical state. Native prototype; not on the MIDI path.

#pragma once

#include "EditPass.h"
#include "LoopPasses.h"
#include "MidiEvent.h"
#include "Utils/NoteUtils.h"

#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

struct ResolutionCostCounters {
  uint32_t eventsInHistory = 0;
  uint32_t passesInHistory = 0;
  uint32_t eventsInQueryWindow = 0;
  uint32_t candidateEvents = 0;
  uint32_t resolutionOperations = 0;
  uint64_t elapsedMicros = 0;
  /// Incremented only when a capture pass chunk list is read (commit of that pass).
  uint32_t passChunkListsWalked = 0;
  /// Tick-index nodes visited during find. Must not require walking pass lists.
  uint32_t indexEntriesVisited = 0;
  /// 6D.2 two-source find: entries visited in the frozen historical `tickEvents` vector.
  uint32_t indexHistoryEntriesVisited = 0;
  /// 6D.2 two-source find: entries visited in the delta `TickEventEntryVec`.
  uint32_t indexDeltaEntriesVisited = 0;
  uint32_t checkpointIntervalTicks = 0;
  uint32_t checkpointCount = 0;
  uint32_t replayStartTick = 0;
  uint32_t eventsReplayed = 0;
  /// 5.15c span-boundary index: sequential append vs one `stable_sort` by tick.
  uint64_t spanBoundaryAppendMicros = 0;
  uint64_t spanBoundarySortMicros = 0;
  /// 5.17d TickIndex flat A: sequential append vs one `stable_sort` by tick.
  uint64_t tickEventAppendMicros = 0;
  uint64_t tickEventSortMicros = 0;
  /// 5.7c channel lookup: sequential NOTE_ON append vs one `stable_sort` + unique.
  uint64_t channelByNoteIdAppendMicros = 0;
  uint64_t channelByNoteIdSortMicros = 0;
  /// 5.18a/b pair walk: `byNoteId` vs `openOnByPitch` vs `find` vs remainder; `nsort` is unique keep-last.
  uint64_t pairTotalMicros = 0;
  uint64_t pairByNoteIdMicros = 0;
  uint64_t pairOpenOnByPitchMicros = 0;
  uint64_t pairLookupMicros = 0;
  uint64_t pairOtherMicros = 0;
  uint32_t pairByNoteIdEntries = 0;
  uint32_t pairByNoteIdInserts = 0;
  uint32_t pairByNoteIdOverwrites = 0;
  uint32_t pairByNoteIdLookups = 0;
  uint64_t pairByNoteIdSortMicros = 0;
  uint32_t pairOpenOnPushes = 0;
  uint32_t pairOpenOnPops = 0;
  uint32_t pairOpenOnPeakDepth = 0;
  uint32_t pairOpenOnPitchKeys = 0;
  uint32_t pairOpenOnAllocations = 0;
  uint64_t pairOpenOnHeapBytes = 0;
};

struct PresentNote {
  uint8_t channel = 0;
  uint8_t pitch = 0;
  NoteId noteId = kInvalidNoteId;
  uint32_t onTick = 0;
};

using PresentNoteVec = std::vector<PresentNote, ExternalMemoryFirstAllocator<PresentNote>>;

// Stage 1 vocabulary pin (DEC-037): three roles, no fourth synonym.
// RawMidiEvent  — MIDI shape stored on capture passes (today: MidiEvent).
// EditAction    — the EditPass row (EditActionType + payload).
// ResolvedEvent — same MIDI shape after active-set + edit apply.
using RawMidiEvent = MidiEvent;
using ResolvedEvent = MidiEvent;
using EditAction = EditPass;

/// Owner of resolveState / resolveWindow. resolveNotes is a derived consumer.
struct LoopContentResolution {
  /// Stage 6 find structure. Commit walks one pass chunk list; find does not walk pass lists.
  struct TickIndex {
    void commitCapturePass(PassId id, const CommittedChunkIdList& chunks, CapturePassState state,
                           uint32_t mergeSequence, ResolutionCostCounters* counters = nullptr);
    /// Sliced form of `commitCapturePass` — begin, append one chunk, index an event range, pair.
    void beginCapturePass(PassId id, CapturePassState state, uint32_t mergeSequence);
    void appendCapturePassChunk(PassId id, uint16_t chunkId);
    void indexCapturePassEventRange(PassId id, uint32_t beginEvent, uint32_t endEventExclusive,
                                    ResolutionCostCounters* counters = nullptr);
    void pairCapturePassNotes(PassId id);
    /// Sliced form of `pairCapturePassNotes`. `openOnByPitch` must persist across ranges of one pass.
    void pairCapturePassEventRange(PassId id, uint32_t beginEvent, uint32_t endEventExclusive,
                                   std::map<uint8_t, std::vector<uint32_t>>& openOnByPitch,
                                   ResolutionCostCounters* counters = nullptr);
    void commitLoopPasses(const LoopPasses& passes, ResolutionCostCounters* counters = nullptr);
    void setCapturePassState(PassId id, CapturePassState state);

    void findRawWindow(uint32_t loopLengthTicks, uint32_t windowStart, uint32_t windowLength,
                       SessionMidiEventVec& out, ResolutionCostCounters* counters = nullptr) const;
    void appendNoteEvents(NoteId noteId, SessionMidiEventVec& out) const;
    void materializeActive(SessionMidiEventVec& out) const;

    uint32_t indexedEventCount() const;
    uint32_t indexedPassCount() const;

    struct CapturePassEntry {
      PassId id = kInvalidPassId;
      uint32_t mergeSequence = 0;
      CapturePassState state = CapturePassState::Active;
      SessionMidiEventVec events;
    };
    struct NoteLocation {
      PassId passId = kInvalidPassId;
      uint32_t onIndex = 0;
      int32_t offIndex = -1;
    };
    /// Private index entry — last-wins `NoteId` lookup after sort+unique (5.18b).
    struct ByNoteIdEntry {
      NoteId noteId = kInvalidNoteId;
      NoteLocation loc;
    };

    using CapturePassEntryVec =
        std::vector<CapturePassEntry, ExternalMemoryFirstAllocator<CapturePassEntry>>;
    using PassByIdMap =
        std::unordered_map<PassId, size_t, std::hash<PassId>, std::equal_to<PassId>,
                           ExternalMemoryFirstAllocator<std::pair<const PassId, size_t>>>;
    using ByNoteIdEntryVec =
        std::vector<ByNoteIdEntry, ExternalMemoryFirstAllocator<ByNoteIdEntry>>;

    /// Private event-index entry — not a span boundary. One row per MIDI event.
    struct TickEventEntry {
      uint32_t tick = 0;
      PassId passId = kInvalidPassId;
      uint32_t eventIndex = 0;
    };
    using TickEventEntryVec =
        std::vector<TickEventEntry, ExternalMemoryFirstAllocator<TickEventEntry>>;

    /// C-order append (pass event-index order), then `stable_sort` by tick. 5.17 A.
    /// A slice at `begin` reserves remaining events in this pass, not this slice only (5.7a).
    static void appendTickEventEntries(const CapturePassEntry& pass, uint32_t begin,
                                       uint32_t endExclusive, TickEventEntryVec& out);
    static void sortTickEventEntriesByTick(TickEventEntryVec& entries);
    void findRawWindowFromTickEvents(const TickEventEntryVec& entries, uint32_t loopLengthTicks,
                                     uint32_t windowStart, uint32_t windowLength,
                                     SessionMidiEventVec& out,
                                     ResolutionCostCounters* counters = nullptr) const;
    /// 6D.2: visit two ordered sources without compacting them into one vector.
    /// `history` is the frozen 5.17 `tickEvents`. `delta` is a separate `TickEventEntryVec`.
    /// Not a `TickIndex` member and not a new domain type.
    void findRawWindowFromTickEvents(const TickEventEntryVec& history,
                                     const TickEventEntryVec& delta, uint32_t loopLengthTicks,
                                     uint32_t windowStart, uint32_t windowLength,
                                     SessionMidiEventVec& out,
                                     ResolutionCostCounters* counters = nullptr) const;

    /// Record first (mergeSequence 0), then overdubs by mergeSequence. Same order as
    /// `materializeActive`.
    void collectActiveMaterializePasses(std::vector<const CapturePassEntry*>& ordered) const;
    /// Sliced copy of one pass into `out`. Used for the record layer and empty-base overdubs.
    void appendMaterializePassEvents(const CapturePassEntry& pass, uint32_t begin,
                                     uint32_t endExclusive, SessionMidiEventVec& out) const;
    /// Same tick compare as `mergeSortedMidiVectors` / `std::merge`. Appends at most `maxEvents`.
    static uint32_t mergeSortedMidiEventRange(const SessionMidiEventVec& base, uint32_t& baseCursor,
                                              const SessionMidiEventVec& addition,
                                              uint32_t& addCursor, SessionMidiEventVec& merged,
                                              uint32_t maxEvents);

    CapturePassEntryVec capturePasses;
    PassByIdMap passById;
    TickEventEntryVec tickEvents;
    ByNoteIdEntryVec byNoteId;
    bool byNoteIdSorted = false;

    static void appendByNoteIdEntry(ByNoteIdEntryVec& entries, NoteId noteId, PassId passId,
                                    uint32_t onIndex);
    static void sortAndUniqueByNoteIdEntries(ByNoteIdEntryVec& entries);
    static const ByNoteIdEntry* findByNoteIdEntry(const ByNoteIdEntryVec& entries, NoteId noteId,
                                                  bool sorted);
    static ByNoteIdEntry* findByNoteIdEntryMutable(ByNoteIdEntryVec& entries, NoteId noteId);
    void sortAndUniqueByNoteId(ResolutionCostCounters* counters = nullptr);
    const ByNoteIdEntry* findByNoteId(NoteId noteId) const {
      return findByNoteIdEntry(byNoteId, noteId, byNoteIdSorted);
    }
  };

  /// Stage 7 in-RAM present-at-S snapshots. Not persisted (D3 is out of scope).
  struct StateCheckpoints {
    struct NoteSpan {
      PresentNote note;
      uint32_t startTick = 0;
      uint32_t endTick = 0;
    };
    /// Private index entry — not a musical noun. Start and exclusive-end both appear.
    struct SpanBoundaryEntry {
      uint32_t tick = 0;
      size_t spanIndex = 0;
    };
    /// Private index entry — not a musical noun. Channel is the value, not part of the key.
    struct ChannelByNoteIdEntry {
      NoteId noteId = kInvalidNoteId;
      uint8_t channel = 0;
    };
    using NoteSpanVec = std::vector<NoteSpan, ExternalMemoryFirstAllocator<NoteSpan>>;
    using SpanBoundaryEntryVec =
        std::vector<SpanBoundaryEntry, ExternalMemoryFirstAllocator<SpanBoundaryEntry>>;
    using ChannelByNoteIdEntryVec =
        std::vector<ChannelByNoteIdEntry, ExternalMemoryFirstAllocator<ChannelByNoteIdEntry>>;

    uint32_t intervalTicks = 0;
    uint32_t loopLengthTicks = 0;
    std::vector<PresentNoteVec, ExternalMemoryFirstAllocator<PresentNoteVec>> presentAt;
    NoteSpanVec spans;
    /// Tick-ordered start and exclusive-end entries for tail replay (flat A).
    SpanBoundaryEntryVec spanBoundaries;
    /// First NOTE_ON channel per noteId from the current `resolved` list (5.7c flat A).
    ChannelByNoteIdEntryVec channelByNoteId;

    void rebuild(const TickIndex& index, const EditPassVec& editPasses, uint32_t loopLength,
                 uint32_t checkpointIntervalTicks, ResolutionCostCounters* counters = nullptr);
    void prepareRebuildSpans(const TickIndex& index, const EditPassVec& editPasses,
                             uint32_t loopLength, uint32_t checkpointIntervalTicks,
                             ResolutionCostCounters* counters = nullptr);
    /// Clears checkpoint working state. Device `RebuildPrepare` then slices materialize.
    bool beginRebuildResolvedEvents(uint32_t loopLength, uint32_t checkpointIntervalTicks,
                                    SessionMidiEventVec& resolved);
    bool prepareRebuildResolvedEvents(const TickIndex& index, const EditPassVec& editPasses,
                                      uint32_t loopLength, uint32_t checkpointIntervalTicks,
                                      SessionMidiEventVec& resolved,
                                      ResolutionCostCounters* counters = nullptr);
    /// Idle-slice 2: reconstruct display notes into spans.
    bool finishRebuildSpansFromEvents(const SessionMidiEventVec& resolved,
                                      ResolutionCostCounters* counters = nullptr);
    /// Sliced span + boundary append after reconstruct. `[begin, endExclusive)` notes.
    /// Reserves `spans` to `notes.size()` and `spanBoundaries` to `2 * notes.size()` (5.7a).
    /// Channel comes from `channelByNoteId` (5.7c). Fill that index before this call.
    /// Does not sort; call `sortSpanBoundaries` after the last range.
    bool appendSpansFromNotes(const SessionMidiEventVec& resolved,
                              const NoteUtils::DisplayNoteVec& notes, uint32_t begin,
                              uint32_t endExclusive, ResolutionCostCounters* counters = nullptr);
    void sortSpanBoundaries(ResolutionCostCounters* counters = nullptr);
    /// C-order append of NOTE_ONs. A slice at `begin` reserves remaining events in `resolved`.
    bool appendChannelByNoteIdRange(const SessionMidiEventVec& resolved, uint32_t begin,
                                    uint32_t endExclusive, ResolutionCostCounters* counters = nullptr);
    void sortChannelByNoteId(ResolutionCostCounters* counters = nullptr);
    bool fillCheckpointRange(uint32_t beginIndex, uint32_t endIndexExclusive,
                             ResolutionCostCounters* counters = nullptr);
    void resolveState(uint32_t tick, PresentNoteVec& out,
                      ResolutionCostCounters* counters = nullptr) const;
    /// C-order append (start then end per span), then `stable_sort` by tick.
    static void appendSpanBoundaryEntries(const NoteSpanVec& spans, uint32_t begin,
                                          uint32_t endExclusive, SpanBoundaryEntryVec& out);
    static void sortSpanBoundaryEntriesByTick(SpanBoundaryEntryVec& entries);
    static void appendChannelByNoteIdEntries(const SessionMidiEventVec& resolved, uint32_t begin,
                                             uint32_t endExclusive, ChannelByNoteIdEntryVec& out);
    static void sortAndUniqueChannelByNoteIdEntries(ChannelByNoteIdEntryVec& entries);
    static uint8_t findChannelByNoteId(const ChannelByNoteIdEntryVec& entries, NoteId noteId);
    void resolveStateFromSpanBoundaries(const SpanBoundaryEntryVec& entries, uint32_t tick,
                                        PresentNoteVec& out,
                                        ResolutionCostCounters* counters = nullptr) const;
  };

  /// Checkpoint density is a performance parameter (DEC-037). Same active history ⇒ same
  /// `resolveState` answers. Native Stage 7/8 stay at 1 bar. Device idle gate uses 8 bars.
  static constexpr uint32_t kNativeCheckpointBarStride = 1;
  static constexpr uint32_t kDeviceCheckpointBarStride = 8;
  /// 5.7 `idle_maint` bar. Same value as `RuntimeTimingTelemetry::kLoopRemainderOneShotUs`.
  static constexpr uint32_t kDeviceGateSliceBudgetUs = 50000;
  /// Index / span units per idle slice. Device IndexCommit appends this many tick-event
  /// rows into `tickEvents` (5.17e). Each slice reserves remaining events in the pass (5.7a).
  static constexpr uint32_t kDeviceGateEventsPerSlice = 8;
  /// Phase progress line: on step change, and at most once per this interval on device.
  static constexpr uint32_t kDeviceGatePhaseLogIntervalUs = 1000000;

  static void resolveWindow(const LoopPasses& passes, uint32_t loopLengthTicks,
                            uint32_t windowStart, uint32_t windowLength, SessionMidiEventVec& out,
                            ResolutionCostCounters* counters = nullptr);

  /// Apply note edits on each capture pass, then merge (same order as
  /// `LoopPasses::materializeToEventVector`). Hide of a shorter same-start Add
  /// must not LIFO-pair a later pass's Off (003204).
  static void resolveWindow(const TickIndex& index, const EditPassVec& editPasses,
                            uint32_t loopLengthTicks, uint32_t windowStart, uint32_t windowLength,
                            SessionMidiEventVec& out, ResolutionCostCounters* counters = nullptr);
  static void resolveWindow(const TickIndex& index, const TickIndex::TickEventEntryVec& tickEvents,
                            const EditPassVec& editPasses, uint32_t loopLengthTicks,
                            uint32_t windowStart, uint32_t windowLength, SessionMidiEventVec& out,
                            ResolutionCostCounters* counters = nullptr);
  /// 6D.2: `resolveWindow` over frozen historical `tickEvents` plus a delta vector.
  static void resolveWindow(const TickIndex& index, const TickIndex::TickEventEntryVec& history,
                            const TickIndex::TickEventEntryVec& delta, const EditPassVec& editPasses,
                            uint32_t loopLengthTicks, uint32_t windowStart, uint32_t windowLength,
                            SessionMidiEventVec& out, ResolutionCostCounters* counters = nullptr);

  static void resolveState(const LoopPasses& passes, uint32_t loopLengthTicks, uint32_t tick,
                           PresentNoteVec& out, ResolutionCostCounters* counters = nullptr);

  static void resolveState(const StateCheckpoints& checkpoints, uint32_t tick, PresentNoteVec& out,
                           ResolutionCostCounters* counters = nullptr);

  static void resolveNotes(const LoopPasses& passes, uint32_t loopLengthTicks, uint32_t windowStart,
                           uint32_t windowLength, NoteUtils::DisplayNoteVec& out,
                           ResolutionCostCounters* counters = nullptr);

  struct DeviceGateSample {
    ResolutionCostCounters indexCommit;
    ResolutionCostCounters materialize;
    ResolutionCostCounters window;
    ResolutionCostCounters rebuild;
    ResolutionCostCounters state;
  };

  /// One-shot host/device sample. Leaves the session in Done; call `deviceGateComplete` to keep
  /// `TickIndex` for idle consume (6A).
  static void measureDeviceGate(const LoopPasses& passes, uint32_t loopLengthTicks,
                                DeviceGateSample& out);

  enum class DeviceGateSliceResult : uint8_t { Inactive, Continue, Complete };

  /// Sliced device gate for idle maintenance — one heavy step per `deviceGateRunOneSlice`.
  static bool deviceGateFinished();
  static bool deviceGateActive();
  static void deviceGateBegin(uint32_t loopLengthTicks);
  static DeviceGateSliceResult deviceGateRunOneSlice(const LoopPasses& passes,
                                                     uint32_t loopLengthTicks);
  static void deviceGateFormatCaptureLine(char* line, size_t cap);
  /// 5.18a pair-owner line. Empty string when the gate has not paired.
  static void deviceGateFormatPairLine(char* line, size_t cap);
  /// Rate-limited progress line. Returns false when the 1 s / phase-change gate skips.
  static bool deviceGateFormatPhaseLine(char* line, size_t cap);
  static void deviceGateReset();
  /// Keep `TickIndex` plus `spans` / `spanBoundaries` / sparse `presentAt`.
  /// Drop rebuild working buffers. Does not construct or sort. Stamp is `playbackRevision`.
  static void deviceGateComplete(uint32_t playbackRevision);
  static bool preparedWindowReady(uint32_t playbackRevision);
  /// 6D.4 / 6E.4: append one committed overdub into the session `delta`, pair it, append
  /// its spans, and restamp. Projects this wrap's sealed companion Delete/Length rows onto
  /// existing `spans` / `spanBoundaries` / `presentAt`. Does not call `applyNoteEditPass`.
  /// No-op when no prepared index is kept. Does not write `tickEvents`.
  static void publishPreparedOverdubPass(const OverdubPass& pass, uint32_t playbackRevision,
                                         const EditPassVec& editPasses = EditPassVec(),
                                         const EditPassIdList& companionIds = EditPassIdList());
  /// Session-disable / re-enable a prepared capture pass. Does not restamp.
  static void setPreparedCapturePassState(PassId id, CapturePassState state);
  /// Session-disable / re-enable a published companion. Does not restamp.
  static void setPreparedEditPassState(EditPassId id, EditPassState state);
  /// Keep prepared ready after `Loop` bumps `playbackRevision` (undo/redo). No-op on miss.
  static void restampPreparedPlaybackRevision(uint32_t playbackRevision);
  /// Consume the kept index. Returns false on miss or stamp mismatch — never rebuilds.
  static bool tryResolvePreparedWindow(const EditPassVec& editPasses, uint32_t loopLengthTicks,
                                       uint32_t windowStart, uint32_t windowLength,
                                       uint32_t playbackRevision, SessionMidiEventVec& out,
                                       ResolutionCostCounters* counters = nullptr);
  /// Consume kept `spans` + `spanBoundaries`. Returns false on miss or stamp mismatch.
  static bool tryResolvePreparedState(uint32_t tick, uint32_t playbackRevision, PresentNoteVec& out,
                                      ResolutionCostCounters* counters = nullptr);
};
