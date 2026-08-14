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
  uint32_t checkpointIntervalTicks = 0;
  uint32_t checkpointCount = 0;
  uint32_t replayStartTick = 0;
  uint32_t eventsReplayed = 0;
};

struct SoundingNote {
  uint8_t channel = 0;
  uint8_t pitch = 0;
  NoteId noteId = kInvalidNoteId;
  uint32_t onTick = 0;
};

using SoundingNoteVec = std::vector<SoundingNote>;

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

    std::vector<CapturePassEntry> capturePasses;
    std::unordered_map<PassId, size_t> passById;
    std::multimap<uint32_t, std::pair<PassId, uint32_t>> byTick;
    std::unordered_map<NoteId, NoteLocation> byNoteId;
  };

  /// Stage 7 in-RAM sounding snapshots. Not persisted (D3 is out of scope).
  struct StateCheckpoints {
    struct NoteSpan {
      SoundingNote note;
      uint32_t startTick = 0;
      uint32_t endTick = 0;
    };

    uint32_t intervalTicks = 0;
    uint32_t loopLengthTicks = 0;
    std::vector<SoundingNoteVec> soundingAt;
    std::vector<NoteSpan> spans;
    /// Start and exclusive-end ticks for tail replay (both keyed here).
    std::multimap<uint32_t, size_t> startsByTick;

    void rebuild(const TickIndex& index, const EditPassVec& editPasses, uint32_t loopLength,
                 uint32_t checkpointIntervalTicks, ResolutionCostCounters* counters = nullptr);
    void resolveState(uint32_t tick, SoundingNoteVec& out,
                      ResolutionCostCounters* counters = nullptr) const;
  };

  static void resolveWindow(const LoopPasses& passes, uint32_t loopLengthTicks,
                            uint32_t windowStart, uint32_t windowLength, SessionMidiEventVec& out,
                            ResolutionCostCounters* counters = nullptr);

  static void resolveWindow(const TickIndex& index, const EditPassVec& editPasses,
                            uint32_t loopLengthTicks, uint32_t windowStart, uint32_t windowLength,
                            SessionMidiEventVec& out, ResolutionCostCounters* counters = nullptr);

  static void resolveState(const LoopPasses& passes, uint32_t loopLengthTicks, uint32_t tick,
                           SoundingNoteVec& out, ResolutionCostCounters* counters = nullptr);

  static void resolveState(const StateCheckpoints& checkpoints, uint32_t tick, SoundingNoteVec& out,
                           ResolutionCostCounters* counters = nullptr);

  static void resolveNotes(const LoopPasses& passes, uint32_t loopLengthTicks, uint32_t windowStart,
                           uint32_t windowLength, NoteUtils::DisplayNoteVec& out,
                           ResolutionCostCounters* counters = nullptr);
};
