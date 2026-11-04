//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// DEC-037 — query-time effective musical state. Native prototype; not on the MIDI path.

#pragma once

#include "CommittedEventRange.h"
#include "EditPass.h"
#include "LoopPasses.h"
#include "LoopSnapshot.h"
#include "MidiEvent.h"
#include "OverlapNoteIdSet.h"
#include "Utils/NoteUtils.h"

#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

#include "Utils/DisplayWindowUtils.h"

/// DEC-237 Slice 2 — identity-preserving display-note merge without a linear scan of the accumulated
/// destination. The identity rules are the existing ones, not a new global uniqueness assumption:
///   - `NoteId`: the per-pass resolver upsert (replace a valid NoteId, keep the first invalid pitch+start);
///   - `NoteIdAndStart`: cross-tile candidate assembly (a wrap head/tail pair shares a NoteId at different
///     starts, so the start tick is part of the key);
///   - `PitchStart`: the wrap-held append (replace the first non-wrap row at the same pitch+start, keep the
///     first wrap-held row).
class DisplayNoteUpsertIndex {
 public:
  enum class KeyMode : uint8_t { NoteId, NoteIdAndStart, PitchStart };

  void clear() {
    index_.clear();
    comparisons_ = 0;
  }

  /// Explicit comparisons performed by this index since `clear()`, for the deterministic work bound.
  size_t comparisons() const { return comparisons_; }

  /// Resolver upsert semantics. A valid `NoteId` row is replaced in place; an invalid row keeps the
  /// first row at its pitch+start. `PitchStart` also replaces a non-wrap row at that pitch+start.
  bool upsert(NoteUtils::DisplayNoteVec& dest, const NoteUtils::DisplayNote& note, KeyMode mode,
              uint32_t loopLength = 0) {
    const Key key = keyFor(note, mode);
    ++comparisons_;
    auto found = index_.find(key);
    if (found != index_.end()) {
      if (found->second >= dest.size()) {
        found->second = static_cast<uint32_t>(dest.size());
        dest.push_back(note);
        return true;
      }
      NoteUtils::DisplayNote& existing = dest[found->second];
      if (mode == KeyMode::PitchStart) {
        if (!DisplayWindowUtils::isWrapHeldDisplayNote(existing, loopLength)) {
          existing = note;
        }
        return false;
      }
      if (note.noteId != kInvalidNoteId) {
        existing = note;
      }
      return false;
    }
    index_.emplace(key, static_cast<uint32_t>(dest.size()));
    dest.push_back(note);
    return true;
  }

  /// Record an already-present row in the table without appending it. Used to prefill the wrap table
  /// from body rows that the candidate index already owns.
  void indexExisting(const NoteUtils::DisplayNoteVec& dest, uint32_t index, KeyMode mode) {
    if (index >= dest.size()) {
      return;
    }
    const Key key = keyFor(dest[index], mode);
    ++comparisons_;
    if (index_.find(key) == index_.end()) {
      index_.emplace(key, index);
    }
  }

  /// Keep the first row for the key. Used for cross-tile assembly and to prefill the wrap table with
  /// body rows, matching `displayNoteIdentityPresent` / the wrap append's replace-first rule.
  bool insertIfAbsent(NoteUtils::DisplayNoteVec& dest, const NoteUtils::DisplayNote& note,
                      KeyMode mode) {
    const Key key = keyFor(note, mode);
    ++comparisons_;
    auto found = index_.find(key);
    if (found != index_.end()) {
      if (found->second < dest.size()) {
        return false;
      }
      found->second = static_cast<uint32_t>(dest.size());
      dest.push_back(note);
      return true;
    }
    index_.emplace(key, static_cast<uint32_t>(dest.size()));
    dest.push_back(note);
    return true;
  }

 private:
  struct Key {
    uint32_t noteId = kInvalidNoteId;
    uint32_t startTick = 0;
    uint8_t pitch = 0;
    bool operator==(const Key& other) const {
      return noteId == other.noteId && startTick == other.startTick && pitch == other.pitch;
    }
  };
  struct KeyHash {
    size_t operator()(const Key& key) const {
      uint64_t h = static_cast<uint64_t>(key.noteId) * 0x9E3779B97F4A7C15ull;
      h ^= static_cast<uint64_t>(key.startTick) * 0xC2B2AE3D27D4EB4Full;
      h ^= static_cast<uint64_t>(key.pitch) * 0x165667B19E3779F9ull;
      h ^= h >> 29;
      return static_cast<size_t>(h);
    }
  };
  using Map =
      std::unordered_map<Key, uint32_t, KeyHash, std::equal_to<Key>,
                         ExternalMemoryFirstAllocator<std::pair<const Key, uint32_t>>>;

  static Key keyFor(const NoteUtils::DisplayNote& note, KeyMode mode) {
    Key key{};
    if (mode == KeyMode::PitchStart) {
      key.pitch = note.note;
      key.startTick = note.startTick;
    } else if (note.noteId == kInvalidNoteId) {
      key.pitch = note.note;
      key.startTick = note.startTick;
    } else {
      key.noteId = note.noteId;
      if (mode == KeyMode::NoteIdAndStart) {
        key.startTick = note.startTick;
      }
    }
    return key;
  }

  Map index_;
  size_t comparisons_ = 0;
};

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
    void appendCapturePassChunkEventRange(PassId id, uint16_t chunkId, uint32_t begin,
                                          uint32_t endExclusive);
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
    /// Same span rebuild using pass-local paired rows (not merged-stream LIFO).
    bool finishRebuildSpansFromEvents(const SessionMidiEventVec& resolved,
                                      const NoteUtils::DisplayNoteVec& pairedNotes,
                                      ResolutionCostCounters* counters);
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
    /// One checkpoint, a span sub-range. Does not walk the remaining spans.
    bool fillCheckpointSpanRange(uint32_t checkpointIndex, uint32_t spanBegin,
                                 uint32_t spanEndExclusive,
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

  /// DEC-181: display-note resolve bounded by a **window**, not by the pass. Same per-pass local
  /// pairing/NoteIds as `resolveDisplayNotes(passes, ...)`, but each pass layer is filled through the
  /// chunk-range window (`CommittedEventRange::inWindow`) so events outside the window are never read.
  /// First paint for a rootless slot walks only what the viewport asks for.
  /// **Execution grain for touched-coverage preparation** (`realtime-idle-owner-admission-bound` 4.6).
  ///
  /// The single-shot `resolveDisplayNotesInWindow` above is exact but indivisible: a dense tile can cost
  /// far more than one owner turn may execute, and a tile is a *correctness/publication* unit, not an
  /// execution bound. This steps the **same** derivation over a declared event budget, one pass at a
  /// time, and publishes nothing until every pass of the window is complete.
  ///
  /// Parity is part of the contract: for any budget, the accumulated notes equal the single-shot
  /// windowed result for the same window (same per-pass local pairing and `NoteId`s, same wrap pairing).
  struct TilePrepCursor {
    /// Ordered pass index: record first, then active overdubs by `mergeSequence`.
    uint32_t passCursor = 0;
    /// Per-pass resumable window traversal.
    CommittedEventRange::WindowTraversalCursor windowCursor{};
    /// The current pass's events for this window (complete when its traversal completes).
    SessionMidiEventVec layer;
    /// Notes accumulated from completed passes, in pass order.
    NoteUtils::DisplayNoteVec notes;
    /// The window's **raw** committed events, in the same pass order. Kept because pass-local pairing can
    /// omit a committed NoteOn identity (a duplicate On at one tick, or an On without its own Off in the
    /// window); the publication merges those identities back by `NoteId` so the source view carries the
    /// same identities playback and undo use. Bounded by the window's events, not by the pass.
    SessionMidiEventVec rawEvents;
    bool started = false;
    bool complete = false;
    /// Passes actually visited, for the caller's envelope check.
    uint16_t visitedPasses = 0;
  };
  /// Envelope: above this many passes the caller takes the single-shot path (named fallback).
  static constexpr uint8_t kTilePrepMaxPasses = 32;

  /// Advance one grain. Returns true when the window's derivation is complete; `cursor.notes` then holds
  /// the result. Consumes at most `eventBudget` events from the window traversal. `maxChunks` bounds the
  /// chunk spans inspected in this call (`0` keeps the historical unlimited behavior). `noteIndex`
  /// accelerates the per-pass upsert without changing its identity semantics; pass `nullptr` for the
  /// historical linear path. The out-params carry this call's deterministic work counts.
  /// C3.4 off the Clock (owner rulings 2026-11-04): an immutable source cut's row membership. `visible` answers for a
  /// capture pass or an edit pass id (one id space) given its current `active` state. Null keeps today's rule
  /// (current state Active), so every existing caller is unchanged.
  struct PassVisibility {
    bool (*visible)(const void* ctx, uint32_t rowId, bool active) = nullptr;
    const void* ctx = nullptr;
    /// Optional 128-bit pitch mask (4 words): only note events and edit rows on these pitches are kept. A cut that
    /// answers per-pitch overlap decisions needs nothing else, and the whole loop's other pitches would otherwise
    /// accumulate in the cursor (device: a 171,640 B growth refused at 7.74 MB PSRAM live). Null keeps every pitch.
    const uint32_t* pitchMask = nullptr;
  };
  static bool stepTilePreparation(const LoopPasses& passes, uint32_t loopLengthTicks,
                                  uint32_t windowStart, uint32_t windowLength, size_t eventBudget,
                                  TilePrepCursor& cursor,
                                  DisplayNoteUpsertIndex* noteIndex = nullptr,
                                  size_t maxChunks = 0, size_t* chunksInspectedOut = nullptr,
                                  size_t* eventsAppendedOut = nullptr,
                                  const PassVisibility* visibility = nullptr);

  static void resolveDisplayNotesInWindow(const LoopPasses& passes, uint32_t loopLengthTicks,
                                          uint32_t windowStart, uint32_t windowLength,
                                          NoteUtils::DisplayNoteVec& out,
                                          ResolutionCostCounters* counters = nullptr);

  /// Stateless effective-event query over one complete immutable snapshot.
  static void resolveWindow(const LoopSnapshot& snapshot, uint32_t windowStart,
                            uint32_t windowLength, SessionMidiEventVec& out,
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

  /// Stateless present-at query over one complete immutable snapshot.
  static void resolveState(const LoopSnapshot& snapshot, uint32_t tick, PresentNoteVec& out,
                           ResolutionCostCounters* counters = nullptr);

  static void resolveState(const StateCheckpoints& checkpoints, uint32_t tick, PresentNoteVec& out,
                           ResolutionCostCounters* counters = nullptr);

  static void resolveNotes(const LoopPasses& passes, uint32_t loopLengthTicks, uint32_t windowStart,
                           uint32_t windowLength, NoteUtils::DisplayNoteVec& out,
                           ResolutionCostCounters* counters = nullptr);

  /// Stateless display-note query over one complete immutable snapshot.
  static void resolveNotes(const LoopSnapshot& snapshot, uint32_t windowStart,
                           uint32_t windowLength, NoteUtils::DisplayNoteVec& out,
                           ResolutionCostCounters* counters = nullptr);

  /// Pair each active capture pass through `NoteUtils`, then merge rows by `NoteId`.
  /// Display/source geometry owner — does not change playback event merge.
  /// A later pass's Off cannot close an earlier pass's On.
  static void resolveDisplayNotes(const LoopPasses& passes, uint32_t loopLengthTicks,
                                  NoteUtils::DisplayNoteVec& out,
                                  ResolutionCostCounters* counters = nullptr);
  /// Full-content display notes from snapshot note-span membership
  /// (`appendSnapshotNotesForBarRange`). Not event-flat reconstruct.
  static void resolveDisplayNotes(const LoopSnapshot& snapshot,
                                  NoteUtils::DisplayNoteVec& out,
                                  ResolutionCostCounters* counters = nullptr);
  static void resolveDisplayNotes(const TickIndex& index, const EditPassVec& editPasses,
                                  uint32_t loopLengthTicks, NoteUtils::DisplayNoteVec& out,
                                  ResolutionCostCounters* counters = nullptr);
  /// Replace same valid `NoteId` with pass-local geometry; append new valid IDs.
  /// Invalid-ID rows keep pitch+start uniqueness.
  static void upsertResolvedDisplayNotes(NoteUtils::DisplayNoteVec& dest,
                                         const NoteUtils::DisplayNoteVec& passLocalNotes);

  struct DeviceGateSample {
    ResolutionCostCounters indexCommit;
    ResolutionCostCounters materialize;
    ResolutionCostCounters window;
    ResolutionCostCounters rebuild;
    ResolutionCostCounters state;
  };

  /// Last `deviceGateRunOneSlice` work unit. Slice 1A measurement only.
  struct DeviceGateSliceRecord {
    const char* step = "idle";
    uint32_t elapsedUs = 0;
    uint32_t workUnits = 0;
    uint32_t resumeCursor = 0;
    bool indivisible = false;
  };

  /// One-shot host/device sample. Leaves the session in Done; call `deviceGateComplete` to keep
  /// `TickIndex` for idle consume (6A).
  static void measureDeviceGate(const LoopPasses& passes, uint32_t loopLengthTicks,
                                DeviceGateSample& out);

  enum class DeviceGateSliceResult : uint8_t { Inactive, Continue, Complete };
  /// Active-gate dirty policy for STOPPED idle. Display dirty is not an identity break
  /// when the selected loop still matches the session.
  enum class DeviceGateDirtyPolicy : uint8_t { Run, Skip, Reset };

  /// Sliced device gate for idle maintenance — one heavy step per `deviceGateRunOneSlice`.
  static bool deviceGateFinished();
  static bool deviceGateActive();
  static void deviceGateBegin(uint32_t loopLengthTicks, uint8_t trackIndex = 0,
                              uint8_t loopIndex = 0);
  static bool deviceGateMatchesLoop(uint8_t trackIndex, uint8_t loopIndex);
  static uint8_t deviceGateTrackIndex();
  static uint8_t deviceGateLoopIndex();
  static DeviceGateSliceResult deviceGateRunOneSlice(const LoopPasses& passes,
                                                     uint32_t loopLengthTicks);
  static void deviceGateFormatCaptureLine(char* line, size_t cap);
  /// 5.18a pair-owner line. Empty string when the gate has not paired.
  static void deviceGateFormatPairLine(char* line, size_t cap);
  /// Rate-limited progress line. Returns false when the 1 s / phase-change gate skips.
  static bool deviceGateFormatPhaseLine(char* line, size_t cap);
  static DeviceGateSliceRecord deviceGateLastSlice();
  static bool deviceGateFormatPrepareSliceLine(char* line, size_t cap);
  /// Snapshot of an interrupted prepare. False when inactive or `reason` is null.
  static bool deviceGateFormatIncompleteLine(char* line, size_t cap, const char* reason);
  static void deviceGateReset();
  /// Prepared session loop length. 0 when the gate has not begun or was reset.
  static uint32_t deviceGateLoopLengthTicks();
  /// Skip the slice when dirty and live length matches the session. Reset when
  /// length disagrees. Not dirty → Run. Does not itself reset the session.
  static DeviceGateDirtyPolicy deviceGateDirtyPolicy(bool visualCacheDirty,
                                                     uint32_t liveLoopLengthTicks);
  /// Restore/hydrate defer LCR (those mutate passes). Deferred save is not a defer.
  static const char* deviceGateContentDeferReason(bool pendingRestore, bool pendingHydrate);
  /// Keep `TickIndex` plus `spans` / `spanBoundaries` / sparse `presentAt`.
  /// Drop rebuild working buffers. Does not construct or sort. Stamp is `playbackRevision`.
  static void deviceGateComplete(uint32_t playbackRevision);
  static bool preparedWindowReady(uint32_t playbackRevision);
  /// 6D.4 / 6E.4: append one committed overdub into the session `delta`, pair it, append
  /// its spans, and restamp. Projects this wrap's sealed companion Delete/Length rows onto
  /// existing `spans` / `spanBoundaries` / `presentAt`. Does not call `applyNoteEditPass`.
  /// No-op when no prepared index is kept, or when `loopLengthTicks` disagrees with the
  /// prepared session length. Does not write `tickEvents`.
  static void publishPreparedOverdubPass(const OverdubPass& pass, uint32_t playbackRevision,
                                         uint32_t loopLengthTicks,
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
  /// Prepared present-at-S NoteIds for pitch using RC8 `displayNotePresentAtHold` on
  /// `NoteSpan`s. Does not call `resolveState`. Returns false on prepared miss or
  /// when live `loopLengthTicks` disagrees with the prepared session length.
  static bool tryCollectPreparedPresentNoteIdsAtTick(uint32_t tick, uint8_t pitch,
                                                     uint32_t playbackRevision,
                                                     uint32_t loopLengthTicks,
                                                     OverlapNoteIdSet& out);
  /// Copy prepared `NoteSpan`s (Active passes + Disabled companions) to DisplayNotes.
  /// Same membership B walks. Omits Hide rows (`endTick == startTick`).
  /// When `windowEvents` is set, keep only spans whose `noteId` is a **unique
  /// window NoteOn ID** (030219: restamped leftover checkpoints must not replace
  /// the gathered MIDI). Membership is a rebuild-local sorted id list — not
  /// `OverlapNoteIdSet` (cap 128 is the USB occupy set). When `loopLengthTicks`
  /// is non-zero, miss if it disagrees with the checkpoint length.
  /// Returns false on prepared miss or when every span is skipped (empty is not
  /// authoritative). Does not reconstruct MIDI. After this returns true, the
  /// caller must not reconstruct.
  static bool tryCopyPreparedSpansToDisplayNotes(uint32_t playbackRevision,
                                                 NoteUtils::DisplayNoteVec& out,
                                                 const SessionMidiEventVec* windowEvents = nullptr,
                                                 uint32_t loopLengthTicks = 0,
                                                 bool logSpanExclusions = false);
  /// Prepared checkpoint membership (span or companion row). Diagnostic helper for wrap srcdrop.
  static bool preparedCheckpointHasNoteId(NoteId noteId);

  /// ---------------------------------------------------------------------------------------------
  /// C5.2a / P-D — the derived-view lane's bounded display continuation.
  ///
  /// `DeviceGateSession` is not live-lane-safe: eleven of its steps are content-sized and
  /// `indivisible` (two whole-index sorts plus whole-vector copies). This session is the visual
  /// continuation instead, and every step advances at most `kBoundedDisplayRowsPerSlice` source
  /// rows — no sort and no whole-vector copy anywhere. It reproduces
  /// `resolveDisplayNotes(passes, ...)` incrementally (pass order → per-pass edit rows → canonical
  /// span pairing → projection → first-occurrence de-duplication → pass-order upsert), publishes
  /// once when every active pass has been consumed, and is keyed by the exact request
  /// (loop instance, loopId, loop length, playbackRevision, pass count) so a supersession discards
  /// the partial candidate and the previous frame stays authoritative.
  static constexpr uint32_t kBoundedDisplayRowsPerSlice = 8;
  /// Declared content cap for the synchronous pass resolve. At or below it one slice may resolve
  /// the pass set directly — the work is then bounded by this constant rather than by the loop.
  /// Above it the slice must use this producer or defer; the whole-pass route is never selected.
  static constexpr uint32_t kBoundedDisplaySynchronousEventCap = 512;
  /// Request identity: the exact loop instance plus `(loopId, loop length, playbackRevision,
  /// pass count)`, and the loop's `canonicalStorageGeneration` as the storage-currency term the
  /// retained envelope compares. `owner` is the Loop's address — the qualification
  /// `deviceGateMatchesLoop` gives by index, without a second reader.
  struct BoundedDisplayRequest {
    const void* owner = nullptr;
    uint32_t loopId = 0;
    uint32_t loopLengthTicks = 0;
    uint32_t playbackRevision = 0;
    uint64_t canonicalStorageGeneration = 0;
  };
  struct BoundedDisplaySliceRecord {
    const char* step = "idle";
    uint32_t workUnits = 0;
    uint32_t cursor = 0;
  };
  /// Committed event rows for `passes`, counting no further than `limit + 1`.
  static uint32_t boundedDisplayCountCommittedEvents(const LoopPasses& passes, uint32_t limit);
  /// True when the pass set is above `kBoundedDisplaySynchronousEventCap`.
  static bool boundedDisplayNeedsProducer(const LoopPasses& passes);
  /// Admit, keep, or re-key the producer for this exact request. A different key is a qualified
  /// reset (the partial candidate is discarded), never a second producer.
  static bool boundedDisplayRequest(const BoundedDisplayRequest& request,
                                    const LoopPasses& passes);
  /// One bounded grain. False when no request matching `passes` is admitted.
  static bool boundedDisplayStep(const LoopPasses& passes);
  /// Complete and matching this exact request; `boundedDisplayNotes()` is then authoritative.
  static bool boundedDisplayReady(const BoundedDisplayRequest& request, const LoopPasses& passes);
  /// Valid only while `boundedDisplayReady(...)` is true for the live request. Read-only.
  static const NoteUtils::DisplayNoteVec& boundedDisplayNotes();
  static void boundedDisplayCancel();
  static BoundedDisplaySliceRecord boundedDisplayLastSlice();
  /// Named deferral reason for the last refused `boundedDisplayRequest`/`boundedDisplayStep`.
  static const char* boundedDisplayDeferReason();

  /// ---------------------------------------------------------------------------------------------
  /// Retained display source envelope (task 4.19, `docs/Plans/display_source_retention_handoff.md`).
  ///
  /// The promoted generation is an **exact live prefix** of `passes`: the retention identity is a
  /// bounded predicate over authoritative pass metadata read at reuse time — never a cached stamp
  /// or a digest, and it adds no `Loop` state. Walking only the retained prefix is what lets a wrap
  /// keep serving (its appended pass is deliberately not compared), while any state change *inside*
  /// the prefix is a named refusal and the caller keeps its honest hole. Retention never relabels
  /// old rows as current content.
  ///
  /// Reserved note slots the envelope keeps, priced `sizeof(DisplayNote) · capacity` — the capacity
  /// the promotion's `std::move` actually keeps, not the served row count.
  static constexpr uint32_t kRetainedDisplaySourceNoteCapacity = 4096;
  /// `retained_capacity` (over the declared cap), `retained_admission` (reserve refused) or
  /// `retained_stale` (the candidate's edit identity moved mid-build); nullptr when the last
  /// promotion was accepted. Every refusal keeps the previous envelope.
  static const char* boundedDisplayRetainedRefusal();
  /// True when the retained envelope is an exact live prefix of `passes` at this request's identity
  /// (`owner`, `loopId`, `loopLengthTicks`, `canonicalStorageGeneration`).
  static bool boundedDisplayRetainedReady(const BoundedDisplayRequest& request,
                                          const LoopPasses& passes);
  /// The retained rows. Valid while `boundedDisplayRetainedReady(...)`; read-only.
  static const NoteUtils::DisplayNoteVec& boundedDisplayRetainedNotes();
  /// Reserved note slots the envelope keeps, and the pass count of its retained prefix.
  static uint32_t boundedDisplayRetainedCapacity();
  static uint32_t boundedDisplayRetainedPassCount();
#if defined(PIO_UNIT_TEST_NATIVE)
  /// Witness: every `resolveDisplayNotesInWindow` derive, so a fixture can prove an edge did none.
  static uint32_t resolveDisplayNotesInWindowCountForTest();
#endif
};
