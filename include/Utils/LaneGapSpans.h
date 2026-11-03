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
}  // namespace LaneGapSpans
#define LANE_GAP_SPAN(bucket) LaneGapSpans::Scope laneGapSpanScope(LaneGapSpans::bucket)
#define LANE_MSG_SPAN(child) LaneGapSpans::MessageScope laneMsgSpanScope(LaneGapSpans::child)
#define LANE_GAP_MIDI_SPLIT 1
#else
#define LANE_GAP_SPAN(bucket) ((void)0)
#define LANE_MSG_SPAN(child) ((void)0)
#endif
