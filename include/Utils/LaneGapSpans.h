//  Copyright (c) 2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
#pragma once

// C5.3b measurement (owner ruling 2026-11-03): a **near-silent** split of the main loop's time between window-lane
// turns. Flash-resident callees add their own wall time to a RAM2 bucket through a scope timer; `main.cpp` folds each
// loop iteration, keeps the worst iterations and gaps, and prints them only after the transport stops. Measurement
// image only (`PLAYBACK_ROLLING_SUCCESSOR_PROBE` with `SESSION_CAPTURE`); elsewhere every macro is empty.

#include <cstdint>

#if defined(PLAYBACK_ROLLING_SUCCESSOR_PROBE) && PLAYBACK_ROLLING_SUCCESSOR_PROBE && defined(SESSION_CAPTURE) && \
    defined(__IMXRT1062__)
#include <Arduino.h>
namespace LaneGapSpans {
enum Bucket : uint8_t { Midi = 0, CaptureFlush, CaptureImmediate, PlaybackFallback, LoopPrefix, EdgeEffects, Count };
/// Per-iteration accumulators (us), folded and cleared by `main.cpp` once per loop iteration.
extern uint32_t gIteration[Count];
inline bool inHandler() {
  uint32_t ipsr;
  __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
  return ipsr != 0;
}
struct Scope {
  uint8_t bucket;
  uint32_t startUs;
  bool active;
  explicit Scope(uint8_t b) : bucket(b), startUs(micros()), active(!inHandler()) {}
  ~Scope() {
    if (active) gIteration[bucket] += micros() - startUs;
  }
};
/// MIDI input split (owner ruling 2026-11-03), defined in MidiHandler.cpp: printed after the transport stops.
void dumpMidiSpans();
void resetMidiSpans();
/// Merged-fallback reuse failures (owner ruling 2026-11-03), defined in TrackPlaybackWindowBuild.cpp: per track, which
/// reuse term failed, and the first transitions with their old/new values. Printed after the transport stops.
void noteMergedReuseFailure(uint8_t track, uint8_t term, uint64_t tick, uint32_t oldRevision, uint32_t newRevision,
                            uintptr_t oldIdentity, uintptr_t newIdentity, uint8_t phase);
void dumpMergedReuse();
void resetMergedReuse();
}  // namespace LaneGapSpans
namespace LaneGapSpans {
/// Children of one dispatched MIDI message (owner ruling 2026-11-03): accumulated per message and folded by
/// `MidiHandler.cpp`'s `noteMidiMessage`, apart from the loop buckets above so nothing is counted twice.
enum MessageChild : uint8_t { StopOverdub = 0, StopRecordingTrack, RecordStopGeometry, OverlapHold, RecordEvents,
                              OverdubNoteChanges, OverdubWrapCommit, MessageChildCount };
extern uint32_t gMessage[MessageChildCount];
struct MessageScope {
  uint8_t child;
  uint32_t startUs;
  bool active;
  explicit MessageScope(uint8_t c) : child(c), startUs(micros()), active(!inHandler()) {}
  ~MessageScope() {
    if (active) gMessage[child] += micros() - startUs;
  }
};
/// Overdub NoteOff split (owner ruling 2026-11-03), defined in LoopPendingNoteChange.cpp and folded into the MIDI
/// dump: which path a completed note took (prepared span / retained obligation / overflow beyond a full set), the
/// deferred-set occupancy at entry, and the coverage-mark, retain and derive time. Everything else is the remainder.
enum NoteOffPath : uint8_t { PathPrepared = 0, PathRetained, PathOverflow, PathCount };
struct NoteOffProbe;
void noteOverdubNoteOff(const NoteOffProbe& probe, uint32_t totalUs, uint8_t occupancyAfter);
void dumpNoteOffSpans();
void resetNoteOffSpans();
struct NoteOffProbe {
  const uint8_t& occupancyNow;
  uint32_t startUs;
  uint32_t coverageUs = 0, retainUs = 0, deriveUs = 0;
  uint8_t occupancyAtEntry;
  uint8_t path = PathPrepared;
  bool active;
  explicit NoteOffProbe(const uint8_t& occupancy)
      : occupancyNow(occupancy), startUs(micros()), occupancyAtEntry(occupancy), active(!inHandler()) {}
  ~NoteOffProbe() {
    if (active) noteOverdubNoteOff(*this, micros() - startUs, occupancyNow);
  }
};
struct NoteOffPhase {
  uint32_t& field;
  uint32_t startUs;
  explicit NoteOffPhase(uint32_t& f) : field(f), startUs(micros()) {}
  ~NoteOffPhase() { field += micros() - startUs; }
};
/// Deferred-completion worker trace (owner ruling 2026-11-03): one counter per decision point that decides whether
/// the bounded obligation set can drain during live overdub -- the idle-turn owner reaching the worker gate, the
/// worker's outcome, each discharge's readiness, and generation moves that clear prepared coverage. Calls go to a
/// flash function, so an ITCM caller adds one call per site. Defined in LoopPendingNoteChange.cpp, dumped with it.
enum CoverageCounter : uint8_t { Entered = 0, GateReached, GatePassed, TurnNoSession, TurnPrivateYield, TurnNoOwed,
                                 TurnAdvanced, TurnPublished, DischargeCalls, Discharged, BlockedOccupancy,
                                 BlockedSpan, GenerationChanges, NewTilesSealed, CoverageCounterCount };
/// Every counter is split by whether the deferred set was **full** when it was last observed (NoteOn hold retain,
/// NoteOff, and each discharge call's entry and exit observe it), so turns can be placed against the full period.
void countCoverage(uint8_t counter, uint32_t by);
void countIdleOwner(uint8_t owner);
void observeDeferredOccupancy(uint8_t occupancy);
/// Who moves the committed generation during an overdub session: the caller's return address (symbolized offline
/// against the same ELF), the loop phase and the deferred-set occupancy. Also counts `GenerationChanges`.
void noteGenerationChange(uintptr_t caller, uint32_t phaseTick, uint8_t occupancy);
/// The capture-pass commit's own caller, latched at its entry, so a generation event names the Track owner too.
void noteCommitCaller(uintptr_t caller);
/// Overdub wrap breakdown (owner ruling 2026-11-03): one `commitOverdubWrap` split into its steps, with the C3.4
/// deadline split by source (retained set vs overflow rows), plus the counts that reach it. Active only inside a wrap,
/// so worker discharges elsewhere do not add to it. Defined in LoopPendingNoteChange.cpp and dumped with it.
enum WrapPhase : uint8_t { WrapHeldOns = 0, WrapCommitPass, WrapJournalRecount, WrapSeal, WrapPrivateHolds,
                           WrapDeadlineSet, WrapDeadlineOverflow, WrapSourceViewApply, WrapSessionBookkeeping,
                           WrapBeginCapture, WrapResumeCanonical, WrapCarryOpenNotes,
                           // Inside the snapshot journal (owner ruling 2026-11-03, one attribution pass). Rows includes
                           // the in-loop flushes; Flush is every fragment flush; Queue is the box hand-off.
                           WrapJournalRows, WrapJournalNonNote, WrapJournalFlush, WrapJournalQueue, WrapPhaseCount };
enum WrapCount : uint8_t { WrapCaptureEvents = 0, WrapHeldOnCount, WrapSetEntering, WrapOverflowEntering,
                           WrapDeadlineEvaluated, WrapDeadlineDerives, WrapSealedAdd, WrapSealedShorten,
                           WrapSealedHide, WrapJournalRowsSeen, WrapJournalNoteChanges,
                           WrapJournalEventChanges, WrapJournalFragments, WrapNonNotePassEvents, WrapNonNoteChecked,
                           WrapNonNoteMembershipRefs, WrapNonNoteAdded, WrapCountCount };
void beginWrapBreakdown();
void endWrapBreakdown();
bool wrapBreakdownActive();
void addWrapPhase(uint8_t phase, uint32_t us);
void addWrapCount(uint8_t count, uint32_t n);
struct WrapPhaseScope {
  uint8_t phase;
  uint32_t startUs;
  explicit WrapPhaseScope(uint8_t p) : phase(p), startUs(micros()) {}
  ~WrapPhaseScope() { addWrapPhase(phase, micros() - startUs); }
};
}  // namespace LaneGapSpans
#define LANE_WRAP_BEGIN() LaneGapSpans::beginWrapBreakdown()
#define LANE_WRAP_END() LaneGapSpans::endWrapBreakdown()
#define LANE_WRAP_PHASE(p) LaneGapSpans::WrapPhaseScope laneWrapPhase##p(LaneGapSpans::p)
#define LANE_WRAP_COUNT(c, n) LaneGapSpans::addWrapCount(LaneGapSpans::c, (n))
#define LANE_COV_COUNT(c) LaneGapSpans::countCoverage(LaneGapSpans::c, 1)
#define LANE_COV_ADD(c, n) LaneGapSpans::countCoverage(LaneGapSpans::c, (n))
#define LANE_COV_OWNER(o) LaneGapSpans::countIdleOwner(static_cast<uint8_t>(o))
#define LANE_COV_OBSERVE(occupancy) LaneGapSpans::observeDeferredOccupancy(occupancy)
#define LANE_COV_GENERATION(phase, occupancy)                                                         \
  LaneGapSpans::noteGenerationChange(reinterpret_cast<uintptr_t>(__builtin_return_address(0)), (phase), \
                                     (occupancy))
#define LANE_COV_COMMIT_CALLER() \
  LaneGapSpans::noteCommitCaller(reinterpret_cast<uintptr_t>(__builtin_return_address(0)))
#define LANE_GAP_SPAN(bucket) LaneGapSpans::Scope laneGapSpanScope(LaneGapSpans::bucket)
#define LANE_MSG_SPAN(child) LaneGapSpans::MessageScope laneMsgSpanScope(LaneGapSpans::child)
#define LANE_NOTEOFF_BEGIN(occupancy) LaneGapSpans::NoteOffProbe laneNoteOffProbe(occupancy)
#define LANE_NOTEOFF_PHASE(field) LaneGapSpans::NoteOffPhase laneNoteOffPhase(laneNoteOffProbe.field)
#define LANE_NOTEOFF_PATH(p) (laneNoteOffProbe.path = LaneGapSpans::p)
#define LANE_GAP_MIDI_SPLIT 1
#else
#define LANE_GAP_SPAN(bucket) ((void)0)
#define LANE_MSG_SPAN(child) ((void)0)
#define LANE_NOTEOFF_BEGIN(occupancy) ((void)0)
#define LANE_NOTEOFF_PHASE(field) ((void)0)
#define LANE_NOTEOFF_PATH(p) ((void)0)
#define LANE_COV_COUNT(c) ((void)0)
#define LANE_COV_ADD(c, n) ((void)0)
#define LANE_COV_OWNER(o) ((void)0)
#define LANE_COV_OBSERVE(occupancy) ((void)0)
#define LANE_COV_GENERATION(phase, occupancy) ((void)0)
#define LANE_COV_COMMIT_CALLER() ((void)0)
#define LANE_WRAP_BEGIN() ((void)0)
#define LANE_WRAP_END() ((void)0)
#define LANE_WRAP_PHASE(p) ((void)0)
#define LANE_WRAP_COUNT(c, n) ((void)0)
#endif
