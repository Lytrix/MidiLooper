//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/RuntimeTimingTelemetry.h"

#include "Globals.h"
#include <new>
#include "Utils/DebugSessionCapture.h"
#include "Utils/InternalHeapFirstAllocator.h"
#include "Utils/MidiBatchComposition.h"

#if defined(SESSION_CAPTURE) || defined(PIO_UNIT_TEST_NATIVE)

#if defined(SESSION_CAPTURE) && defined(__IMXRT1062__)
#include <Arduino.h>
#endif

#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_SNAPSHOT_IDLE
#include "Utils/InvestigationCaptureLog.h"
#endif

#if defined(__IMXRT1062__)
#define RT_FLASHMEM_FN __attribute__((noinline, section(".flashmem")))
#else
#define RT_FLASHMEM_FN
#endif

namespace RuntimeTimingTelemetry {
namespace {

struct Accumulator {
  uint32_t maxUs = 0;
  uint32_t overCount = 0;
};

// S7.5a distribution instrument: a **companded** sample histogram (see `kEnvelopeDistBuckets`). 256 buckets of
// 256 µs cover 0..65,535 µs — the range the lateness family actually lives in — and 16 power-of-two buckets bound
// the long tail, so a distribution costs 544 B instead of a sample buffer. It lives in `State` (PSRAM on target).
constexpr uint16_t kDistFineBuckets = 256;
constexpr uint16_t kDistCoarseBuckets = 16;
constexpr uint32_t kDistFineBucketUs = 256;
static_assert(kDistFineBuckets + kDistCoarseBuckets == kEnvelopeDistBuckets,
              "bucket count must match the header's kEnvelopeDistBuckets");

struct Histogram {
  uint16_t counts[kEnvelopeDistBuckets] = {};
  uint32_t samples = 0;
};

// S7.5a per-route output-lateness split: a bounded, first-come table. A route that finds no free slot is counted in
// `lateRouteSpill` (carried on every route row), so a truncated split is visible rather than silent.
constexpr uint8_t kLateRouteSlots = 8;

struct LateRouteSlot {
  uint16_t key = 0;  // (type << 8) | channel; 0 means free (an emitted status byte is never 0)
  uint32_t maxUs = 0;
  uint32_t overCount = 0;
  uint32_t samples = 0;
};

constexpr uint8_t kStopTraceRecordCount = 128;

enum class StopTraceKind : uint8_t {
  Service,
  NoteEmission,
  StateTransition,
};

// Written by a playback-service context and drained only by the main loop.
// Keep this fixed and integer-only: stop tracing must never allocate or call
// the capture formatter from a timing-critical callback.
struct StopTraceRecord {
  StopTraceKind kind = StopTraceKind::Service;
  uint32_t sequence = 0;
  uint32_t actualUs = 0;
  uint32_t dueUs = 0;
  uint32_t deadlineUs = 0;
  uint32_t periodUs = 0;
  uint32_t tick = 0;
  uint32_t latenessUs = 0;
  bool isNoteOn = false;
  uint8_t track = 255, slot = 255, fromState = 255, toState = 255;
  uint8_t channel = 255, note = 255;
};

// State edges run in main; timer service and note emission may interrupt them.
// Preserve the caller's mask and protect only fixed-size telemetry updates.
struct StopTraceCriticalSection {
#if defined(__IMXRT1062__)
  uint32_t mask;
  StopTraceCriticalSection() {
    asm volatile("mrs %0, primask\n\tcpsid i" : "=r"(mask) :: "memory");
  }
  ~StopTraceCriticalSection() {
    asm volatile("msr primask, %0" :: "r"(mask) : "memory");
  }
#endif
};

void recordSample(Accumulator& acc, uint32_t durationUs) {
  if (durationUs > acc.maxUs) {
    acc.maxUs = durationUs;
  }
  if (durationUs > kObservationalSoftCeilingUs) {
    ++acc.overCount;
  }
}

// ISR-called (IntervalTimer updateInternalClock / sendClock / sendMidiEvent).
// Must stay in ITCM with noteClockPulse — FLASHMEM from that ISR starved USB
// after transport start (session_20260817_212654).
void recordLateness(Accumulator& acc, uint32_t latenessUs) {
  if (latenessUs > acc.maxUs) {
    acc.maxUs = latenessUs;
  }
  if (latenessUs > 0) {
    ++acc.overCount;
  }
}

uint32_t latenessAfterDeadline(uint32_t nowUs, uint32_t dueUs, uint32_t periodUs) {
  if (periodUs == 0) {
    return 0;
  }
  const uint32_t deadlineUs = dueUs + periodUs;
  if (static_cast<int32_t>(nowUs - deadlineUs) <= 0) {
    return 0;
  }
  return nowUs - deadlineUs;
}

#if defined(__IMXRT1062__)
DMAMEM Accumulator trackUpdates[Config::NUM_TRACKS];
#else
Accumulator trackUpdates[Config::NUM_TRACKS];
#endif

RT_FLASHMEM_FN void resetTrackUpdates() {
  for (Accumulator& trackUpdate : trackUpdates) {
    trackUpdate = Accumulator{};
  }
}

RT_FLASHMEM_FN Accumulator& trackUpdateAt(uint8_t trackIndex) {
#if defined(__IMXRT1062__)
  static bool initialized = false;
  if (!initialized) {
    resetTrackUpdates();
    initialized = true;
  }
#endif
  return trackUpdates[trackIndex];
}

struct State {
  StopPhaseTiming stopPhases[Config::NUM_TRACKS];
  CanonicalPrefixProgress canonicalPrefixProgress[Config::NUM_TRACKS];
  Accumulator midiGap;
  Accumulator midiInput;
  Accumulator clk;
  Accumulator tracks;
  Accumulator usbdev;
  Accumulator din;
  Accumulator hosttask;
  Accumulator hostdrain;
  Accumulator usbread;
  Accumulator usbdisp;
  Accumulator usbcap;
  Accumulator usbthru;
  Accumulator usbclk;
  Accumulator usbnote;
  Accumulator usbcc;
  Accumulator usbtrans;
  Accumulator noteappend;
  Accumulator notechg;
  Accumulator noterecon;
  Accumulator notepair;
  Accumulator idleMaint;
  Accumulator loadFrame;
  Accumulator persistSave;
  Accumulator lateOn;
  Accumulator lateOff;
  Accumulator lateClk;
  Accumulator extclkArr;
  Accumulator extclkDisp;
  uint32_t clkBatchMax = 0;
  uint32_t clkBatchCount = 0;
  uint32_t windowServicePeriodUs = 0;
  uint32_t usbBatchMax = 0;
  uint32_t usbBatchSaturatedCount = 0;
  uint32_t usbBatchClockMax = 0;
  uint32_t usbBatchConsecutiveClockMax = 0;
  uint32_t usbBatchSizeBuckets[MidiBatchComposition::kBucketCount] = {};
  uint32_t usbBatchClockBuckets[MidiBatchComposition::kBucketCount] = {};
  uint32_t usbBatchClockDispatchMaxUs[MidiBatchComposition::kBucketCount] = {};
  Accumulator trackRebuild;
  Accumulator trackOrder;
  Accumulator trackSend;
  Accumulator trackSlot;
  Accumulator trackResid;
  uint32_t trackPathRebuildUs = 0;
  uint32_t trackPathOrderUs = 0;
  uint32_t trackPathSendUs = 0;
  uint32_t trackPathSlotUs = 0;
  bool trackPathNestedActive = false;
  uint32_t extclkArrivalDueUs = 0;
  uint32_t extclkArrivalPeriodUs = 0;
  bool extclkArrivalCadenceActive = false;
  uint32_t extclkBatchArrivalUs = 0;
  uint32_t extclkBatchRemaining = 0;
  uint32_t usbNestedCaptureUs = 0;
  uint32_t usbNestedThruUs = 0;
  uint32_t usbNestedClockUs = 0;
  uint32_t usbNestedNoteUs = 0;
  uint32_t usbNestedCcUs = 0;
  uint32_t usbNestedTransportUs = 0;
  uint32_t usbNestedNoteAppendUs = 0;
  uint32_t usbNestedNoteChangeUs = 0;
  uint32_t usbNestedNoteReconUs = 0;
  uint32_t usbNestedNotePairUs = 0;
  bool usbNestedActive = false;
  uint32_t clockPulses = 0;
  // S7.5a envelope instruments — window-scoped distribution state: the four channel histograms behind the
  // `DIAG,env_dist` rows and the bounded per-route lateness table behind the `DIAG,late_route` rows.
  Histogram midiGapDist;
  Histogram midiInputDist;
  Histogram lateOnDist;
  Histogram lateOffDist;
  LateRouteSlot lateRoutes[kLateRouteSlots];
  uint32_t lateRouteSpill = 0;
  uint32_t lastInputExitUs = 0;
  uint32_t inputEnterUs = 0;
  bool inputActive = false;
  // S7.5a envelope instruments (declared in the header): the playback tick sampled at the entry to the MIDI input
  // service, that service's capture stamp error in ticks, and the replenishment owner's accounting.
  uint32_t inputServiceTick = 0;
  bool inputServiceTickValid = false;
  uint32_t captureStampMaxTicks = 0;
  uint32_t captureStampSamples = 0;
  Accumulator replenishWait;
  Accumulator replenishWork;
  uint32_t replenishRefused = 0;
  uint32_t replenishPairs = 0;
  uint32_t replenishRefusedSinceUs = 0;
  uint32_t windowStartUs = 0;
  uint32_t lastEmitUs = 0;
  uint32_t serviceDueUs = 0;
  uint32_t servicePeriodUs = 0;
  bool serviceCadenceActive = false;
  uint32_t serviceSequence = 0;
  StopBoundaryTiming stopBoundary;
  volatile bool stopTraceActive = false;
  StopTraceRecord stopTraceRecords[kStopTraceRecordCount];
  volatile uint8_t stopTraceHead = 0;
  volatile uint8_t stopTraceTail = 0;
  uint32_t stopTraceDropped = 0;
  uint32_t stopTraceDroppedReported = 0;
  uint32_t stopActiveTracks = 0;
  uint32_t clockDueUs = 0;
  uint32_t clockPeriodUs = 0;
  bool clockCadenceActive = false;
  bool lateOneShotArmed = true;
  bool pendingLateEvent = false;
  uint8_t pendingLateClass = 0;  // 0=on, 1=off, 2=clk
  uint32_t pendingLateUs = 0;
  uint32_t pendingLateTick = 0;
  uint32_t pendingLateSendUs = 0;
  uint32_t pendingLateDueUs = 0;
  uint32_t pendingLateDeadlineUs = 0;
  bool pendingPlaybackRebuild = false;
  uint32_t pendingRebuildDurationUs = 0;
  uint32_t pendingRebuildWindowStartTick = 0;
  uint32_t pendingRebuildWindowLengthTicks = 0;
  uint32_t pendingRebuildRevision = 0;
  uint32_t pendingRebuildCount = 0;
  uint32_t pendingRebuildAtUs = 0;
  uint32_t pendingRebuildPreviousPhase = UINT32_MAX;
  uint32_t pendingRebuildCurrentPhase = 0;
  uint32_t pendingRebuildEventCount = 0;
  uint8_t pendingRebuildTrack = 255;
  uint8_t pendingRebuildSlot = 255;
  PlaybackRebuildPath pendingRebuildPath = PlaybackRebuildPath::CompatibilityPasses;
};

RT_FLASHMEM_FN void initializeState(State& s) {
  // Construct in PSRAM directly; an aggregate temporary would consume most
  // of the remaining device stack as the bounded trace queue grows.
  ::new (static_cast<void*>(&s)) State();
}

State& state() {
#if defined(__IMXRT1062__) && EXTMEM_AVAILABLE
  // FLASHMEM emitters read this; PSRAM is legal, DMAMEM is not.
  static EXTMEM State s;
  static bool initialized = false;
  if (!initialized) {
    initializeState(s);
    initialized = true;
  }
  return s;
#else
  static State s;
  return s;
#endif
}

// Producer may run in the playback-service context. A byte index is atomic on
// the target; write the record before publishing its new head index.
void queueStopTraceRecord(State& s, const StopTraceRecord& record) {
  [[maybe_unused]] StopTraceCriticalSection critical;
  if (!s.stopTraceActive) {
    return;
  }
  const uint8_t head = s.stopTraceHead;
  const uint8_t next = static_cast<uint8_t>((head + 1u) % kStopTraceRecordCount);
  if (next == s.stopTraceTail) {
    ++s.stopTraceDropped;
    return;
  }
  s.stopTraceRecords[head] = record;
  s.stopTraceHead = next;
}

RT_FLASHMEM_FN void beginStopTrace(State& s, uint8_t track) {
  // Never reset an undrained queue when another stop starts.
  s.stopActiveTracks |= 1u << track;
  s.stopTraceActive = true;
  s.stopBoundary.traceActive = true;
}

void queueFirstLate(State& s, uint8_t lateClass, uint32_t latenessUs, uint32_t tick,
                    uint32_t sendUs, uint32_t dueUs, uint32_t deadlineUs) {
  if (latenessUs == 0 || !s.lateOneShotArmed) {
    return;
  }
  s.lateOneShotArmed = false;
  s.pendingLateEvent = true;
  s.pendingLateClass = lateClass;
  s.pendingLateUs = latenessUs;
  s.pendingLateTick = tick;
  s.pendingLateSendUs = sendUs;
  s.pendingLateDueUs = dueUs;
  s.pendingLateDeadlineUs = deadlineUs;
}

RT_FLASHMEM_FN const char* playbackRebuildPathLabel(PlaybackRebuildPath path) {
  // DEC-143's overlay label is answered **before** the dense switch on purpose: a seventh case grows the compiler's
  // jump table by four bytes, and four bytes of `.data` here crossed the linker's alignment edge and snapped the RAM1
  // `variables` figure by 4,096 B (measured on the qualification image: `variables` 96,192 -> 100,288 with `code`
  // unchanged). The label is one branch, not one table slot, so the qualification budget is untouched.
  if (path == PlaybackRebuildPath::PublishedRecordOverlay) {
#if defined(__IMXRT1062__)
    return PSTR("record_overlay");
#else
    return "record_overlay";
#endif
  }
#if defined(__IMXRT1062__)
  switch (path) {
    case PlaybackRebuildPath::PublishedSnapshot:
      return PSTR("snapshot");
    case PlaybackRebuildPath::GatherMissHoldEmpty:
      return PSTR("hold_empty");
    case PlaybackRebuildPath::CompatibilityPasses:
      return PSTR("compatibility");
    case PlaybackRebuildPath::NoteEditPreview:
      return PSTR("note_edit");
    case PlaybackRebuildPath::IncrementalOverdubSeal:
      return PSTR("incremental");
    case PlaybackRebuildPath::StaleRootOverlay:
      return PSTR("overlay");
    case PlaybackRebuildPath::PublishedRecordOverlay:
      break;  // Answered above; kept so the switch stays exhaustive without a table slot.
  }
  return PSTR("unknown");
#else
  switch (path) {
    case PlaybackRebuildPath::PublishedSnapshot:
      return "snapshot";
    case PlaybackRebuildPath::GatherMissHoldEmpty:
      return "hold_empty";
    case PlaybackRebuildPath::CompatibilityPasses:
      return "compatibility";
    case PlaybackRebuildPath::NoteEditPreview:
      return "note_edit";
    case PlaybackRebuildPath::IncrementalOverdubSeal:
      return "incremental";
    case PlaybackRebuildPath::StaleRootOverlay:
      return "overlay";
    case PlaybackRebuildPath::PublishedRecordOverlay:
      break;  // Answered above; kept so the switch stays exhaustive without a table slot.
  }
  return "unknown";
#endif
}

RT_FLASHMEM_FN void clearWindow(State& s) {
  s.midiGap = Accumulator{};
  s.midiInput = Accumulator{};
  s.clk = Accumulator{};
  s.tracks = Accumulator{};
  resetTrackUpdates();
  s.usbdev = Accumulator{};
  s.din = Accumulator{};
  s.hosttask = Accumulator{};
  s.hostdrain = Accumulator{};
  s.usbread = Accumulator{};
  s.usbdisp = Accumulator{};
  s.usbcap = Accumulator{};
  s.usbthru = Accumulator{};
  s.usbclk = Accumulator{};
  s.usbnote = Accumulator{};
  s.usbcc = Accumulator{};
  s.usbtrans = Accumulator{};
  s.noteappend = Accumulator{};
  s.notechg = Accumulator{};
  s.noterecon = Accumulator{};
  s.notepair = Accumulator{};
  s.idleMaint = Accumulator{};
  s.loadFrame = Accumulator{};
  s.persistSave = Accumulator{};
  s.lateOn = Accumulator{};
  s.lateOff = Accumulator{};
  s.lateClk = Accumulator{};
  s.extclkArr = Accumulator{};
  s.extclkDisp = Accumulator{};
  s.clkBatchMax = 0;
  s.clkBatchCount = 0;
  s.windowServicePeriodUs = 0;
  s.usbBatchMax = 0;
  s.usbBatchSaturatedCount = 0;
  s.usbBatchClockMax = 0;
  s.usbBatchConsecutiveClockMax = 0;
  for (uint8_t i = 0; i < MidiBatchComposition::kBucketCount; ++i) {
    s.usbBatchSizeBuckets[i] = 0;
    s.usbBatchClockBuckets[i] = 0;
    s.usbBatchClockDispatchMaxUs[i] = 0;
  }
  s.trackRebuild = Accumulator{};
  s.trackOrder = Accumulator{};
  s.trackSend = Accumulator{};
  s.trackSlot = Accumulator{};
  s.trackResid = Accumulator{};
  // S7.5a envelope instruments: window-scoped maxima/counts. `inputServiceTick` is deliberately NOT cleared here —
  // it belongs to one input service (`noteInputServiceTick`/`noteCaptureStampTick`), not to the telemetry window.
  s.captureStampMaxTicks = 0;
  s.captureStampSamples = 0;
  s.replenishWait = Accumulator{};
  s.replenishWork = Accumulator{};
  s.replenishRefused = 0;
  s.replenishPairs = 0;
  s.replenishRefusedSinceUs = 0;
  s.clockPulses = 0;
  // ...and the distribution instrument's window-scoped state: the four channel histograms and the per-route split.
  s.midiGapDist = Histogram{};
  s.midiInputDist = Histogram{};
  s.lateOnDist = Histogram{};
  s.lateOffDist = Histogram{};
  for (uint8_t index = 0; index < kLateRouteSlots; ++index) {
    s.lateRoutes[index] = LateRouteSlot{};
  }
  s.lateRouteSpill = 0;
}

RT_FLASHMEM_FN void emitWindow(const State& s, uint32_t nowUs, uint32_t windowElapsedUs) {
#if defined(SESSION_CAPTURE)
  DebugSessionCapture::runtimeTimingTelemetry("midi_gap", s.midiGap.maxUs, s.midiGap.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("midi_input", s.midiInput.maxUs, s.midiInput.overCount);
  // S7.5a envelope instruments, as **one aggregated row** (the tag and its fields live in the sink, whose format
  // is flash-resident): the captured-event stamp error in ticks and the replenishment owner's request-to-completion
  // accounting. Deliberately not one row per captured event, and deliberately not four new `DIAG` tag literals.
  DebugSessionCapture::envelopeInstrRow(s.captureStampMaxTicks, s.captureStampSamples, s.replenishWait.maxUs,
                                        s.replenishWork.maxUs, s.replenishRefused, s.replenishPairs);
  // S7.5a distribution rows: each channel's own p50/p99 **interval** for this window (a bucket bound, never a
  // fabricated exact microsecond — the exact maxima stay on the `late_*`/`midi_*` rows) and the per-route lateness
  // split, one row per route the window held. A channel with no sample emits no row at all, and every route row
  // carries the split's spill count so a truncation at the slot bound is visible rather than silent.
  for (uint8_t channel = 0; channel <= static_cast<uint8_t>(DistChannel::LateOff); ++channel) {
    DistQuantiles quantiles;
    if (distQuantiles(static_cast<DistChannel>(channel), quantiles)) {
      DebugSessionCapture::envelopeDistRow(channel, quantiles.p50LoUs, quantiles.p50HiUs, quantiles.p99LoUs,
                                           quantiles.p99HiUs, quantiles.samples);
    }
  }
  {
    const uint8_t routeCount = lateRouteSlotCount();
    const uint32_t routeSpill = lateRouteSpillCount();
    for (uint8_t index = 0; index < routeCount; ++index) {
      LateRouteEntry entry;
      if (lateRouteSlotAt(index, entry)) {
        DebugSessionCapture::envelopeRouteLatenessRow(entry.type, entry.ch, entry.maxUs, entry.overCount,
                                                      entry.samples, routeSpill);
      }
    }
  }
  DebugSessionCapture::runtimeTimingTelemetry("clk", s.clk.maxUs, s.clk.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("tracks", s.tracks.maxUs, s.tracks.overCount);
  for (uint8_t trackIndex = 0; trackIndex < Config::NUM_TRACKS; ++trackIndex) {
    const Accumulator& trackUpdate = trackUpdateAt(trackIndex);
    DebugSessionCapture::runtimeTimingTrackUpdate(
        trackIndex, trackUpdate.maxUs, trackUpdate.overCount);
  }
  DebugSessionCapture::runtimeTimingTelemetry("usbdev", s.usbdev.maxUs, s.usbdev.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("din", s.din.maxUs, s.din.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("hosttask", s.hosttask.maxUs, s.hosttask.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("hostdrain", s.hostdrain.maxUs, s.hostdrain.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("usbread", s.usbread.maxUs, s.usbread.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("usbdisp", s.usbdisp.maxUs, s.usbdisp.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("usbcap", s.usbcap.maxUs, s.usbcap.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("usbthru", s.usbthru.maxUs, s.usbthru.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("usbclk", s.usbclk.maxUs, s.usbclk.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("usbnote", s.usbnote.maxUs, s.usbnote.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("usbcc", s.usbcc.maxUs, s.usbcc.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("usbtrans", s.usbtrans.maxUs, s.usbtrans.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("noteappend", s.noteappend.maxUs, s.noteappend.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("notechg", s.notechg.maxUs, s.notechg.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("noterecon", s.noterecon.maxUs, s.noterecon.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("notepair", s.notepair.maxUs, s.notepair.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("idle_maint", s.idleMaint.maxUs, s.idleMaint.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("load_frame", s.loadFrame.maxUs, s.loadFrame.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("persist_save", s.persistSave.maxUs,
                                             s.persistSave.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("late_on", s.lateOn.maxUs, s.lateOn.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("late_off", s.lateOff.maxUs, s.lateOff.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("late_clk", s.lateClk.maxUs, s.lateClk.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("extclk_arrival", s.extclkArr.maxUs, s.extclkArr.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("extclk_dispatch", s.extclkDisp.maxUs, s.extclkDisp.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("clk_batch", s.clkBatchMax, s.clkBatchCount);
  DebugSessionCapture::runtimeTimingTelemetry("service_period", s.windowServicePeriodUs, 0);
  DebugSessionCapture::runtimeTimingUsbBatch(s.usbBatchMax, s.usbBatchSaturatedCount,
                                            s.usbBatchClockMax, s.usbBatchConsecutiveClockMax);
  DebugSessionCapture::runtimeTimingUsbBatchBuckets("usb_bkt_size", s.usbBatchSizeBuckets);
  DebugSessionCapture::runtimeTimingUsbBatchBuckets("usb_bkt_clk", s.usbBatchClockBuckets);
  DebugSessionCapture::runtimeTimingUsbBatchBuckets("usb_bkt_disp", s.usbBatchClockDispatchMaxUs);
  DebugSessionCapture::runtimeTimingTelemetry("track_rebuild", s.trackRebuild.maxUs,
                                             s.trackRebuild.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("track_order", s.trackOrder.maxUs,
                                             s.trackOrder.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("track_send", s.trackSend.maxUs,
                                             s.trackSend.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("track_slot", s.trackSlot.maxUs,
                                             s.trackSlot.overCount);
  DebugSessionCapture::runtimeTimingTelemetry("track_resid", s.trackResid.maxUs,
                                             s.trackResid.overCount);
  uint32_t pulsesPerSecond = 0;
  if (windowElapsedUs > 0) {
    pulsesPerSecond = static_cast<uint32_t>(
        (static_cast<uint64_t>(s.clockPulses) * 1000000ull) / windowElapsedUs);
  }
  DebugSessionCapture::runtimeTimingClockrate(pulsesPerSecond);
  (void)nowUs;
#else
  (void)s;
  (void)nowUs;
  (void)windowElapsedUs;
#endif
}


// S7.5a distribution instrument — internal helpers. The histograms and the route table live in `State`, which is
// internal to this translation unit, so only the bucket/quantile entry points and the route accessors are public.
RT_FLASHMEM_FN Histogram& histogramFor(State& s, DistChannel channel) {
  switch (channel) {
    case DistChannel::MidiInput:
      return s.midiInputDist;
    case DistChannel::LateOn:
      return s.lateOnDist;
    case DistChannel::LateOff:
      return s.lateOffDist;
    case DistChannel::MidiGap:
    default:
      return s.midiGapDist;
  }
}

// The bucket the `percentile`-th sample falls in, reported as that bucket's own µs interval. The rank is a ceiling,
// so the 99th percentile of a one-sample window is that sample rather than an empty answer.
RT_FLASHMEM_FN void quantileBucket(const Histogram& hist, uint32_t percentile, uint32_t& loUs, uint32_t& hiUs) {
  const uint32_t rank =
      static_cast<uint32_t>((static_cast<uint64_t>(hist.samples) * percentile + 99u) / 100u);
  uint32_t cumulative = 0;
  for (uint16_t bucket = 0; bucket < kEnvelopeDistBuckets; ++bucket) {
    cumulative += hist.counts[bucket];
    if (cumulative >= rank) {
      envelopeDistBucketBounds(bucket, loUs, hiUs);
      return;
    }
  }
  envelopeDistBucketBounds(kEnvelopeDistBuckets - 1, loUs, hiUs);
}

RT_FLASHMEM_FN void recordDistSample(Histogram& hist, uint32_t valueUs) {
  ++hist.counts[envelopeDistBucket(valueUs)];
  ++hist.samples;
}

// Window-scoped lateness accounting for one emitted output event's route. Slots are first-come: a route that finds
// no free slot is counted in the spill instead, which every route row carries.
RT_FLASHMEM_FN void recordRouteLateness(State& s, uint8_t type, uint8_t channel, uint32_t latenessUs) {
  const uint16_t key = static_cast<uint16_t>((static_cast<uint16_t>(type) << 8) | channel);
  if (key == 0) {
    return;
  }
  LateRouteSlot* freeSlot = nullptr;
  for (uint8_t index = 0; index < kLateRouteSlots; ++index) {
    LateRouteSlot& slot = s.lateRoutes[index];
    if (slot.key == key) {
      if (latenessUs > slot.maxUs) {
        slot.maxUs = latenessUs;
      }
      if (latenessUs > 0) {
        ++slot.overCount;
      }
      ++slot.samples;
      return;
    }
    if (freeSlot == nullptr && slot.key == 0) {
      freeSlot = &slot;
    }
  }
  if (freeSlot == nullptr) {
    ++s.lateRouteSpill;
    return;
  }
  freeSlot->key = key;
  freeSlot->maxUs = latenessUs;
  freeSlot->overCount = latenessUs > 0 ? 1u : 0u;
  freeSlot->samples = 1;
}
// One emitted event's lateness into both distribution surfaces: its family's histogram and the window's route split.
// A single entry point on purpose — this is reached from the note-emission path, which stays in ITCM, so what that
// path pays for the instrument is one call rather than two feeds (RAM1 `padding` is the guard on that).
RT_FLASHMEM_FN void recordLatenessSample(State& s, bool isNoteOn, uint8_t channel, uint32_t latenessUs) {
  recordDistSample(isNoteOn ? s.lateOnDist : s.lateOffDist, latenessUs);
  recordRouteLateness(s, static_cast<uint8_t>(isNoteOn ? 0x90u : 0x80u), channel, latenessUs);
}
}  // namespace

void resetForTest() {
  State& s = state();
  s = State{};
  resetTrackUpdates();
}

RT_FLASHMEM_FN void noteMidiInputEnter(uint32_t nowUs) {
  State& s = state();
  if (s.windowStartUs == 0) {
    s.windowStartUs = nowUs;
  }
  if (s.lastInputExitUs != 0) {
    const uint32_t gapUs = nowUs - s.lastInputExitUs;
    recordSample(s.midiGap, gapUs);
    recordDistSample(s.midiGapDist, gapUs);
  }
  s.inputEnterUs = nowUs;
  s.inputActive = true;
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_SNAPSHOT_IDLE
  InvestigationCaptureLog::appendSnapshotIdleReserveOnMidiEnter(nowUs);
#endif
}

RT_FLASHMEM_FN void noteMidiInputExit(uint32_t nowUs) {
  State& s = state();
  if (s.inputActive) {
    const uint32_t inputUs = nowUs - s.inputEnterUs;
    recordSample(s.midiInput, inputUs);
    recordDistSample(s.midiInputDist, inputUs);
    s.inputActive = false;
  }
  s.lastInputExitUs = nowUs;
}

void noteClockDispatch(uint32_t durationUs) {
  recordSample(state().clk, durationUs);
}

void noteTracksUpdate(uint32_t durationUs) {
  recordSample(state().tracks, durationUs);
}

RT_FLASHMEM_FN void noteInputServiceTick(uint32_t tick) {
  State& s = state();
  s.inputServiceTick = tick;
  s.inputServiceTickValid = true;
}

RT_FLASHMEM_FN void noteCaptureStampTick(uint32_t stampedTick) {
  State& s = state();
  if (!s.inputServiceTickValid) {
    return;  // an append outside the input service (overdub path): no arrival tick to compare against
  }
  ++s.captureStampSamples;
  if (stampedTick >= s.inputServiceTick) {
    const uint32_t error = stampedTick - s.inputServiceTick;
    if (error > s.captureStampMaxTicks) {
      s.captureStampMaxTicks = error;
    }
  }
}

RT_FLASHMEM_FN void noteReplenishWork(uint32_t durationUs) {
  recordSample(state().replenishWork, durationUs);
}

RT_FLASHMEM_FN void noteReplenishRefused(uint32_t nowUs) {
  State& s = state();
  ++s.replenishRefused;
  if (s.replenishRefusedSinceUs == 0) {
    s.replenishRefusedSinceUs = nowUs;
  }
}

RT_FLASHMEM_FN void noteReplenishCompletion(uint32_t nowUs) {
  State& s = state();
  if (s.replenishRefusedSinceUs == 0) {
    return;  // no refusal outstanding: this top-up resolved nothing
  }
  recordSample(s.replenishWait, nowUs - s.replenishRefusedSinceUs);
  ++s.replenishPairs;
  s.replenishRefusedSinceUs = 0;
}

// S7.5a distribution instrument — the companded bucket scheme. 256 buckets of 256 µs below 65,536 µs, then one
// power-of-two bucket per bit up to 2^32-1, so the quantile a window reports is an interval (the bucket's bounds)
// and never an invented exact microsecond.
RT_FLASHMEM_FN uint16_t envelopeDistBucket(uint32_t valueUs) {
  if (valueUs < (kDistFineBuckets * kDistFineBucketUs)) {
    return static_cast<uint16_t>(valueUs / kDistFineBucketUs);
  }
  uint32_t shifted = valueUs >> 16;
  uint16_t bit = 0;
  while (shifted > 1u && bit < (kDistCoarseBuckets - 1u)) {
    shifted >>= 1;
    ++bit;
  }
  return static_cast<uint16_t>(kDistFineBuckets + bit);
}

RT_FLASHMEM_FN void envelopeDistBucketBounds(uint16_t bucket, uint32_t& loUs, uint32_t& hiUs) {
  if (bucket < kDistFineBuckets) {
    loUs = static_cast<uint32_t>(bucket) * kDistFineBucketUs;
    hiUs = loUs + kDistFineBucketUs - 1u;
    return;
  }
  const uint16_t bit = static_cast<uint16_t>(bucket - kDistFineBuckets);
  loUs = 1u << (16u + bit);
  hiUs = (bit + 1u >= kDistCoarseBuckets) ? 0xFFFFFFFFu : ((1u << (17u + bit)) - 1u);
}

RT_FLASHMEM_FN bool distQuantiles(DistChannel channel, DistQuantiles& out) {
  const Histogram& hist = histogramFor(state(), channel);
  out = DistQuantiles{};
  out.samples = hist.samples;
  if (hist.samples == 0) {
    return false;  // no sample this window: the row is not emitted at all, never emitted as a zero
  }
  quantileBucket(hist, 50u, out.p50LoUs, out.p50HiUs);
  quantileBucket(hist, 99u, out.p99LoUs, out.p99HiUs);
  return true;
}

RT_FLASHMEM_FN uint8_t lateRouteSlotCount() {
  const State& s = state();
  uint8_t count = 0;
  for (uint8_t index = 0; index < kLateRouteSlots; ++index) {
    if (s.lateRoutes[index].key != 0) {
      ++count;
    }
  }
  return count;
}

RT_FLASHMEM_FN bool lateRouteSlotAt(uint8_t index, LateRouteEntry& out) {
  // A dense index over the non-empty slots, so callers loop 0..lateRouteSlotCount()-1.
  const State& s = state();
  uint8_t seen = 0;
  for (uint8_t slot = 0; slot < kLateRouteSlots; ++slot) {
    const LateRouteSlot& entry = s.lateRoutes[slot];
    if (entry.key == 0) {
      continue;
    }
    if (seen == index) {
      out.type = static_cast<uint8_t>(entry.key >> 8);
      out.ch = static_cast<uint8_t>(entry.key & 0xFFu);
      out.maxUs = entry.maxUs;
      out.overCount = entry.overCount;
      out.samples = entry.samples;
      return true;
    }
    ++seen;
  }
  return false;
}

RT_FLASHMEM_FN uint32_t lateRouteSpillCount() {
  return state().lateRouteSpill;
}

RT_FLASHMEM_FN void noteTrackUpdate(uint8_t trackIndex, uint32_t durationUs) {
  if (trackIndex < Config::NUM_TRACKS) {
    recordSample(trackUpdateAt(trackIndex), durationUs);
  }
}

namespace {

uint32_t saturatingSub(uint32_t lhs, uint32_t rhs) {
  return lhs > rhs ? lhs - rhs : 0;
}

}  // namespace

RT_FLASHMEM_FN void beginTrackPathNested() {
  State& s = state();
  s.trackPathRebuildUs = 0;
  s.trackPathOrderUs = 0;
  s.trackPathSendUs = 0;
  s.trackPathSlotUs = 0;
  s.trackPathNestedActive = true;
}

RT_FLASHMEM_FN void addTrackPathRebuild(uint32_t durationUs) {
  State& s = state();
  if (s.trackPathNestedActive) {
    s.trackPathRebuildUs += durationUs;
  }
}

RT_FLASHMEM_FN void addTrackPathOrder(uint32_t durationUs) {
  State& s = state();
  if (s.trackPathNestedActive) {
    s.trackPathOrderUs += durationUs;
  }
}

RT_FLASHMEM_FN void addTrackPathSend(uint32_t durationUs) {
  State& s = state();
  if (s.trackPathNestedActive) {
    s.trackPathSendUs += durationUs;
  }
}

RT_FLASHMEM_FN void addTrackPathSlot(uint32_t durationUs) {
  State& s = state();
  if (s.trackPathNestedActive) {
    s.trackPathSlotUs += durationUs;
  }
}

RT_FLASHMEM_FN uint32_t trackPathSendUs() {
  const State& s = state();
  return s.trackPathNestedActive ? s.trackPathSendUs : 0;
}

RT_FLASHMEM_FN uint32_t trackPathExclusiveUs(uint32_t wallUs, uint32_t sendBeforeUs) {
  const State& s = state();
  if (!s.trackPathNestedActive) {
    return 0;
  }
  return saturatingSub(wallUs, saturatingSub(s.trackPathSendUs, sendBeforeUs));
}

RT_FLASHMEM_FN void commitTrackPathNested(uint32_t trackDurationUs) {
  State& s = state();
  if (!s.trackPathNestedActive) {
    return;
  }
  recordSample(s.trackRebuild, s.trackPathRebuildUs);
  recordSample(s.trackOrder, s.trackPathOrderUs);
  recordSample(s.trackSend, s.trackPathSendUs);
  recordSample(s.trackSlot, s.trackPathSlotUs);
  const uint32_t attributed = s.trackPathRebuildUs + s.trackPathOrderUs + s.trackPathSendUs +
                              s.trackPathSlotUs;
  recordSample(s.trackResid, saturatingSub(trackDurationUs, attributed));
  s.trackPathRebuildUs = 0;
  s.trackPathOrderUs = 0;
  s.trackPathSendUs = 0;
  s.trackPathSlotUs = 0;
  s.trackPathNestedActive = false;
}

void noteUsbDeviceDrain(uint32_t durationUs) {
  recordSample(state().usbdev, durationUs);
}

void noteDinDrain(uint32_t durationUs) {
  recordSample(state().din, durationUs);
}

void noteUsbHostTask(uint32_t durationUs) {
  recordSample(state().hosttask, durationUs);
}

void noteUsbHostDrain(uint32_t durationUs) {
  recordSample(state().hostdrain, durationUs);
}

void noteUsbDeviceRead(uint32_t durationUs) {
  recordSample(state().usbread, durationUs);
}

void noteUsbDeviceDispatch(uint32_t durationUs) {
  recordSample(state().usbdisp, durationUs);
}

void beginUsbDeviceNested() {
  State& s = state();
  s.usbNestedCaptureUs = 0;
  s.usbNestedThruUs = 0;
  s.usbNestedClockUs = 0;
  s.usbNestedNoteUs = 0;
  s.usbNestedCcUs = 0;
  s.usbNestedTransportUs = 0;
  s.usbNestedNoteAppendUs = 0;
  s.usbNestedNoteChangeUs = 0;
  s.usbNestedNoteReconUs = 0;
  s.usbNestedNotePairUs = 0;
  s.usbNestedActive = true;
}

void addUsbDeviceCapture(uint32_t durationUs) {
  state().usbNestedCaptureUs += durationUs;
}

void addUsbDeviceThru(uint32_t durationUs) {
  state().usbNestedThruUs += durationUs;
}

void addUsbDeviceClock(uint32_t durationUs) {
  state().usbNestedClockUs += durationUs;
}

void addUsbDeviceNote(uint32_t durationUs) {
  state().usbNestedNoteUs += durationUs;
}

void addUsbDeviceCc(uint32_t durationUs) {
  state().usbNestedCcUs += durationUs;
}

void addUsbDeviceTransport(uint32_t durationUs) {
  state().usbNestedTransportUs += durationUs;
}

void addNoteAppend(uint32_t durationUs) {
  State& s = state();
  if (s.usbNestedActive) {
    s.usbNestedNoteAppendUs += durationUs;
  }
}

void addNoteChange(uint32_t durationUs) {
  State& s = state();
  if (s.usbNestedActive) {
    s.usbNestedNoteChangeUs += durationUs;
  }
}

void addNoteRecon(uint32_t durationUs) {
  State& s = state();
  if (s.usbNestedActive) {
    s.usbNestedNoteReconUs += durationUs;
  }
}

void addNotePair(uint32_t durationUs) {
  State& s = state();
  if (s.usbNestedActive) {
    s.usbNestedNotePairUs += durationUs;
  }
}

void commitUsbDeviceNested() {
  State& s = state();
  recordSample(s.usbcap, s.usbNestedCaptureUs);
  recordSample(s.usbthru, s.usbNestedThruUs);
  recordSample(s.usbclk, s.usbNestedClockUs);
  recordSample(s.usbnote, s.usbNestedNoteUs);
  recordSample(s.usbcc, s.usbNestedCcUs);
  recordSample(s.usbtrans, s.usbNestedTransportUs);
  recordSample(s.noteappend, s.usbNestedNoteAppendUs);
  recordSample(s.notechg, s.usbNestedNoteChangeUs);
  recordSample(s.noterecon, s.usbNestedNoteReconUs);
  recordSample(s.notepair, s.usbNestedNotePairUs);
  s.usbNestedCaptureUs = 0;
  s.usbNestedThruUs = 0;
  s.usbNestedClockUs = 0;
  s.usbNestedNoteUs = 0;
  s.usbNestedCcUs = 0;
  s.usbNestedTransportUs = 0;
  s.usbNestedNoteAppendUs = 0;
  s.usbNestedNoteChangeUs = 0;
  s.usbNestedNoteReconUs = 0;
  s.usbNestedNotePairUs = 0;
  s.usbNestedActive = false;
}

void noteClockPulse() {
  State& s = state();
  ++s.clockPulses;
}

void noteExternalClockBatch(uint32_t arrivalUs, uint32_t clockCount, uint32_t clockPeriodUs) {
  if (clockCount == 0) {
    return;
  }
  State& s = state();
  s.extclkBatchArrivalUs = arrivalUs;
  s.extclkBatchRemaining = clockCount;
  if (clockCount > s.clkBatchMax) {
    s.clkBatchMax = clockCount;
  }
  ++s.clkBatchCount;
  if (clockPeriodUs == 0) {
    return;
  }
  if (!s.extclkArrivalCadenceActive) {
    s.extclkArrivalDueUs = arrivalUs;
    s.extclkArrivalPeriodUs = clockPeriodUs;
    s.extclkArrivalCadenceActive = true;
    recordLateness(s.extclkArr, 0);
  } else {
    s.extclkArrivalPeriodUs = clockPeriodUs;
    s.extclkArrivalDueUs += clockPeriodUs;
    uint32_t latenessUs = 0;
    if (static_cast<int32_t>(arrivalUs - s.extclkArrivalDueUs) > 0) {
      latenessUs = arrivalUs - s.extclkArrivalDueUs;
    }
    recordLateness(s.extclkArr, latenessUs);
  }
  if (clockCount > 1) {
    s.extclkArrivalDueUs += clockPeriodUs * (clockCount - 1);
  }
}

void noteExternalClockDispatchEnter(uint32_t nowUs) {
  State& s = state();
  if (s.extclkBatchRemaining == 0) {
    return;
  }
  --s.extclkBatchRemaining;
  recordSample(s.extclkDisp, nowUs - s.extclkBatchArrivalUs);
}

void noteUsbDeviceBatchComposition(uint32_t size, uint32_t clockCount, uint32_t nonClockCount,
                                   uint32_t maxConsecutiveClocks, bool saturated,
                                   uint32_t dispatchUs) {
  if (size == 0) {
    return;
  }
  (void)nonClockCount;
  State& s = state();
  if (size > s.usbBatchMax) {
    s.usbBatchMax = size;
  }
  if (saturated) {
    ++s.usbBatchSaturatedCount;
  }
  ++s.usbBatchSizeBuckets[MidiBatchComposition::countBucket(size)];
  if (clockCount > s.usbBatchClockMax) {
    s.usbBatchClockMax = clockCount;
  }
  if (maxConsecutiveClocks > s.usbBatchConsecutiveClockMax) {
    s.usbBatchConsecutiveClockMax = maxConsecutiveClocks;
  }
  if (clockCount == 0) {
    return;
  }
  const uint8_t clockBucket = MidiBatchComposition::countBucket(clockCount);
  ++s.usbBatchClockBuckets[clockBucket];
  if (dispatchUs > s.usbBatchClockDispatchMaxUs[clockBucket]) {
    s.usbBatchClockDispatchMaxUs[clockBucket] = dispatchUs;
  }
}

void notePlaybackServiceEnter(uint32_t nowUs, uint32_t tickPeriodUs) {
  State& s = state();
  s.servicePeriodUs = tickPeriodUs;
  if (tickPeriodUs != 0) {
    s.windowServicePeriodUs = tickPeriodUs;
  }
  if (!s.serviceCadenceActive || tickPeriodUs == 0) {
    s.serviceDueUs = nowUs;
    s.serviceCadenceActive = tickPeriodUs != 0;
  } else {
    s.serviceDueUs += tickPeriodUs;
  }
  ++s.serviceSequence;
  s.stopBoundary.sequence = s.serviceSequence;
  s.stopBoundary.actualUs = nowUs;
  s.stopBoundary.dueUs = s.serviceDueUs;
  s.stopBoundary.deadlineUs = s.serviceDueUs + tickPeriodUs;
  s.stopBoundary.periodUs = tickPeriodUs;
  s.stopBoundary.droppedRecords = s.stopTraceDropped;
  s.stopBoundary.traceActive = s.stopTraceActive;
  queueStopTraceRecord(s, StopTraceRecord{StopTraceKind::Service, s.serviceSequence, nowUs,
      s.serviceDueUs, s.serviceDueUs + tickPeriodUs, tickPeriodUs});
}

RT_FLASHMEM_FN void resetPlaybackDeadlineCadence() {
  State& s = state();
  s.serviceDueUs = 0;
  s.servicePeriodUs = 0;
  s.serviceCadenceActive = false;
  s.clockDueUs = 0;
  s.clockPeriodUs = 0;
  s.clockCadenceActive = false;
  s.extclkArrivalDueUs = 0;
  s.extclkArrivalPeriodUs = 0;
  s.extclkArrivalCadenceActive = false;
  s.extclkBatchArrivalUs = 0;
  s.extclkBatchRemaining = 0;
}

uint32_t remainingUntilNextPlaybackServiceUs(uint32_t nowUs) {
  const State& s = state();
  if (!s.serviceCadenceActive || s.servicePeriodUs == 0) {
    return 0xFFFFFFFFu;
  }
  const uint32_t deadlineUs = s.serviceDueUs + s.servicePeriodUs;
  if (static_cast<int32_t>(nowUs - deadlineUs) >= 0) {
    return 0;
  }
  return deadlineUs - nowUs;
}

void recordNoteSendLateness(bool isNoteOn, uint32_t nowUs, uint32_t tick,
    uint8_t track, uint8_t slot, uint8_t channel, uint8_t note) {
  State& s = state();
  const uint32_t latenessUs =
      latenessAfterDeadline(nowUs, s.serviceDueUs, s.servicePeriodUs);
  recordLateness(isNoteOn ? s.lateOn : s.lateOff, latenessUs);
  // S7.5a distribution instrument: the same sample lands in its family's histogram and in the window's per-route
  // S7.5a distribution instrument: the same sample lands in its family's histogram and in the window's per-route
  // split, keyed by the emitted status byte and channel — the key the `MO` emission row carries.
  recordLatenessSample(s, isNoteOn, channel, latenessUs);
  queueStopTraceRecord(s, StopTraceRecord{StopTraceKind::NoteEmission, s.serviceSequence,
      nowUs, s.serviceDueUs, s.serviceDueUs + s.servicePeriodUs, s.servicePeriodUs, tick,
      latenessUs, isNoteOn, track, slot, 255, 255, channel, note});
  queueFirstLate(s, isNoteOn ? 0 : 1, latenessUs, tick, nowUs, s.serviceDueUs,
                 s.serviceDueUs + s.servicePeriodUs);
}

void recordOutgoingClockSend(uint32_t nowUs, uint32_t clockPeriodUs, uint32_t onTimeWindowUs,
                             uint32_t tick) {
  State& s = state();
  if (clockPeriodUs == 0) {
    return;
  }
  if (!s.clockCadenceActive) {
    s.clockDueUs = nowUs;
    s.clockPeriodUs = clockPeriodUs;
    s.clockCadenceActive = true;
    recordLateness(s.lateClk, 0);
    return;
  }
  s.clockPeriodUs = clockPeriodUs;
  s.clockDueUs += clockPeriodUs;
  const uint32_t latenessUs = latenessAfterDeadline(nowUs, s.clockDueUs, onTimeWindowUs);
  recordLateness(s.lateClk, latenessUs);
  queueFirstLate(s, 2, latenessUs, tick, nowUs, s.clockDueUs,
                 s.clockDueUs + onTimeWindowUs);
}

RT_FLASHMEM_FN void recordPlaybackRebuild(uint32_t durationUs, uint32_t windowStartTick,
                                          uint32_t windowLengthTicks, uint32_t revision,
                                          uint8_t trackIndex, uint8_t slotIndex,
                                          PlaybackRebuildPath path, uint32_t atUs,
                                          uint32_t previousPhase, uint32_t currentPhase,
                                          uint32_t eventCount) {
  State& s = state();
  ++s.pendingRebuildCount;
  if (!s.pendingPlaybackRebuild || durationUs > s.pendingRebuildDurationUs) {
    s.pendingRebuildDurationUs = durationUs;
    s.pendingRebuildWindowStartTick = windowStartTick;
    s.pendingRebuildWindowLengthTicks = windowLengthTicks;
    s.pendingRebuildRevision = revision;
    s.pendingRebuildTrack = trackIndex;
    s.pendingRebuildSlot = slotIndex;
    s.pendingRebuildPath = path;
    s.pendingRebuildAtUs = atUs;
    s.pendingRebuildPreviousPhase = previousPhase;
    s.pendingRebuildCurrentPhase = currentPhase;
    s.pendingRebuildEventCount = eventCount;
  }
  s.pendingPlaybackRebuild = true;
}

RT_FLASHMEM_FN void emitStopPhase(uint8_t track, const StopPhaseTiming& sample) {
#if defined(SESSION_CAPTURE)
  DebugSessionCapture::stopPhaseTiming(track, sample.slot, sample.phase,
      sample.generation, sample.epoch, sample.destination, sample.calls,
      sample.crossed, sample.unknown, sample.startUs, sample.endUs, sample.slackUs);
#else
  (void)track; (void)sample;
#endif
}

RT_FLASHMEM_FN void recordStopGrain(uint8_t track, uint8_t slot, uint8_t phase,
    uint32_t generation, uint32_t epoch, uint8_t destination,
    uint32_t startUs, uint32_t endUs, uint32_t slackUs, bool finishedPhase,
    const StopBoundaryTiming* admittedBoundary) {
  if (track >= Config::NUM_TRACKS) return;
  State& s = state();
  auto& sample = s.stopPhases[track];
  if (sample.calls && (sample.slot != slot || sample.phase != phase ||
      sample.generation != generation || sample.epoch != epoch || sample.destination != destination)) {
    emitStopPhase(track, sample);
    sample = StopPhaseTiming{};
  }
  sample.slot = slot; sample.phase = phase; sample.generation = generation;
  sample.epoch = epoch; sample.destination = destination;
  const uint32_t durationUs = endUs - startUs;
  if (!sample.calls || durationUs > sample.maxUs) {
    sample.startUs = startUs; sample.endUs = endUs;
    sample.slackUs = slackUs; sample.maxUs = durationUs;
  }
  ++sample.calls;
  if (slackUs == 0xFFFFFFFFu) ++sample.unknown;
  else if (durationUs > slackUs) ++sample.crossed;
  const StopBoundaryTiming boundary = admittedBoundary != nullptr
      ? *admittedBoundary : peekStopBoundary();
#if defined(SESSION_CAPTURE)
  // Per-grain rows are the capture ring's dominant source: the canonical two-overdub HITL
  // fixture logged 4,062 `stop_grain` rows for one run (~444 KB, 71% of all `#CAP` bytes) and
  // overflowed the ring, which evicted the `snapshot_unit` publication rows the run is read for.
  // The per-phase aggregate (`stop_phase`, emitted below) already carries calls/crossed/max and
  // the span, so only a bounded sample of grains is needed: the first grain of each phase (its
  // arrival), one in `kStopGrainEmitStride`, and the phase's terminal grain. `sample.calls` is
  // the per-phase grain counter, so a phase change resets the stride by construction.
  // A phase that never advances is bounded too: `kStopGrainMaxRowsPerPhase` rows are allowed, and the
  // first grain past that budget emits the per-phase aggregate once — naming the stalled phase with
  // its call count, max duration and crossed count — after which the stride drops to
  // `kStopGrainStalledStride`. A frozen `ReservedClosure` phase whose job keeps being offered
  // deferred turns (the deferral is read from `#STOP_STATUS`; the job itself is serviced every turn
  // exactly as before) therefore costs ~48 rows plus one row per 4096 grains, not ~8 rows/s
  // forever, and it can no longer flood the ring that carries the response it is meant to explain.
  // The `teensy41-capture-serial-no-grain` build sets `SESSION_CAPTURE_GRAIN=0` and drops the
  // family entirely for acceptance runs whose evidence is the snapshot/publication rows.
#if SESSION_CAPTURE_GRAIN
  constexpr uint32_t kStopGrainEmitStride = 16;
  constexpr uint32_t kStopGrainMaxRowsPerPhase = 48;
  constexpr uint32_t kStopGrainStalledStride = 4096;
  // Row 1 is the arrival and each completed stride adds one, so `1 + calls / kStopGrainEmitStride`
  // rows span `calls` and the first grain past the budget is `kStopGrainMaxRowsPerPhase * stride`.
  constexpr uint32_t kStopGrainFirstStalledCall =
      kStopGrainMaxRowsPerPhase * kStopGrainEmitStride;
  const bool firstGrainOfPhase = sample.calls == 1;
  const bool withinRowBudget = sample.calls < kStopGrainFirstStalledCall;
  const bool sampled = firstGrainOfPhase ||
      (withinRowBudget ? (sample.calls % kStopGrainEmitStride) == 0
                       : (sample.calls % kStopGrainStalledStride) == 0);
  if (sample.calls == kStopGrainFirstStalledCall && !finishedPhase) {
    // The one-shot aggregate for this phase: a phase that has not advanced is named here instead
    // of by a row per stride. A phase that finishes on this grain is summarized by the terminal
    // `emitStopPhase` below, not twice.
    emitStopPhase(track, sample);
  }
  if (sampled || finishedPhase) {
    DebugSessionCapture::stopGrainTiming(track, slot, phase, generation, epoch, destination,
        boundary.sequence, startUs, endUs, boundary.actualUs, boundary.dueUs,
        boundary.deadlineUs, boundary.periodUs, slackUs,
        finishedPhase ? 1u : 0u);
  }
#endif
#else
  (void)boundary;
#endif
  if (finishedPhase) {
    emitStopPhase(track, sample);
    sample = StopPhaseTiming{};
  }
}

RT_FLASHMEM_FN void recordStopStateTransition(uint8_t track, uint8_t slot, uint8_t fromState,
    uint8_t toState, uint32_t tick, uint32_t atUs) {
  if (track >= Config::NUM_TRACKS) return;
  State& s = state();
  [[maybe_unused]] StopTraceCriticalSection critical;
  const bool entry = fromState == 6 && toState == 7;
  const bool terminal = fromState == 7 && (toState == 1 || toState == 5);
  if (!entry && !terminal) return;
  if (entry) beginStopTrace(s, track);
  StopTraceRecord record{StopTraceKind::StateTransition, s.stopBoundary.sequence,
      atUs, s.stopBoundary.dueUs, s.stopBoundary.deadlineUs, s.stopBoundary.periodUs,
      tick, 0, false, track, slot, fromState, toState};
  queueStopTraceRecord(s, record);
  if (terminal) {
    s.stopActiveTracks &= ~(1u << track);
    s.stopTraceActive = s.stopActiveTracks != 0;
    s.stopBoundary.traceActive = s.stopTraceActive;
  }
}

RT_FLASHMEM_FN StopPhaseTiming peekStopPhase(uint8_t track) {
  return track < Config::NUM_TRACKS ? state().stopPhases[track] : StopPhaseTiming{};
}

RT_FLASHMEM_FN CanonicalPrefixProgress& canonicalPrefixProgress(uint8_t track) {
  // The caller always passes a live track index (0..NUM_TRACKS-1). Out-of-range is clamped to track 0
  // rather than answered from a static cell: a fallback static would cost a fresh 32-byte DTCM section
  // and break the pinned RAM1 budget. The clamp is unreachable in practice.
  return state().canonicalPrefixProgress[track < Config::NUM_TRACKS ? track : 0];
}

RT_FLASHMEM_FN StopBoundaryTiming peekStopBoundary() {
  [[maybe_unused]] StopTraceCriticalSection critical;
  StopBoundaryTiming boundary = state().stopBoundary;
  boundary.droppedRecords = state().stopTraceDropped;
  boundary.traceActive = state().stopTraceActive;
  return boundary;
}

RT_FLASHMEM_FN StopBoundaryTiming captureStopGrainBoundary(uint32_t& startUs) {
  [[maybe_unused]] StopTraceCriticalSection critical;
#if defined(__IMXRT1062__)
  startUs = micros();
#else
  (void)startUs;
#endif
  StopBoundaryTiming boundary = state().stopBoundary;
  if (!state().serviceCadenceActive) boundary.periodUs = 0;
  return boundary;
}

RT_FLASHMEM_FN void noteIdleMaint(uint32_t durationUs) {
  recordSample(state().idleMaint, durationUs);
}

RT_FLASHMEM_FN void noteLoadFrame(uint32_t durationUs) {
  recordSample(state().loadFrame, durationUs);
}

RT_FLASHMEM_FN void notePersistSave(uint32_t durationUs) {
  recordSample(state().persistSave, durationUs);
}

RT_FLASHMEM_FN void emitPendingDeadlineOneShots() {
  State& s = state();
  while (s.stopTraceTail != s.stopTraceHead) {
    const StopTraceRecord record = s.stopTraceRecords[s.stopTraceTail];
    s.stopTraceTail = static_cast<uint8_t>((s.stopTraceTail + 1u) % kStopTraceRecordCount);
#if defined(SESSION_CAPTURE)
    if (record.kind == StopTraceKind::Service) {
      DebugSessionCapture::stopServiceTiming(record.sequence, record.actualUs, record.dueUs,
          record.deadlineUs, record.periodUs);
    } else if (record.kind == StopTraceKind::StateTransition) {
      DebugSessionCapture::stopStateTiming(record.track, record.slot, record.fromState,
          record.toState, record.tick, record.actualUs, record.sequence, record.dueUs,
          record.deadlineUs, record.periodUs);
    } else {
      DebugSessionCapture::stopMidiEmissionTiming(record.sequence, record.isNoteOn ? 1u : 0u,
          record.tick, record.actualUs, record.dueUs, record.deadlineUs, record.latenessUs,
          record.track, record.slot, record.channel, record.note);
    }
#else
    (void)record;
#endif
  }
#if defined(SESSION_CAPTURE)
  if (s.stopTraceDropped != s.stopTraceDroppedReported) {
    DebugSessionCapture::stopTimelineDropped(s.stopTraceDropped - s.stopTraceDroppedReported);
    s.stopTraceDroppedReported = s.stopTraceDropped;
  }
#if defined(__IMXRT1062__)
  const char* midiClass = PSTR("on");
  if (s.pendingLateClass == 1) {
    midiClass = PSTR("off");
  } else if (s.pendingLateClass == 2) {
    midiClass = PSTR("clk");
  }
#else
  const char* midiClass = "on";
  if (s.pendingLateClass == 1) {
    midiClass = "off";
  } else if (s.pendingLateClass == 2) {
    midiClass = "clk";
  }
#endif
  if (s.pendingLateEvent) {
    DebugSessionCapture::midiDeadlineLateEvent(midiClass, s.pendingLateUs, s.pendingLateTick,
        s.pendingLateSendUs, s.pendingLateDueUs, s.pendingLateDeadlineUs);
  }
  // A first take has no committed source, so its expected hold-empty probe can
  // recur every loop.  It carries no timing-failure signal and must not flood
  // the capture ring while the display is sharing the live-capture budget.
  if (s.pendingPlaybackRebuild &&
      s.pendingRebuildPath != PlaybackRebuildPath::GatherMissHoldEmpty) {
    DebugSessionCapture::playbackRebuild(s.pendingRebuildDurationUs, s.pendingRebuildWindowStartTick,
                                         s.pendingRebuildWindowLengthTicks, s.pendingRebuildRevision,
                                         s.pendingRebuildTrack, s.pendingRebuildSlot,
                                         s.pendingRebuildCount,
                                         playbackRebuildPathLabel(s.pendingRebuildPath),
                                         s.pendingRebuildAtUs, s.pendingRebuildPreviousPhase,
                                         s.pendingRebuildCurrentPhase, s.pendingRebuildEventCount);
  }
#endif
  s.pendingLateEvent = false;
  s.pendingPlaybackRebuild = false;
  s.pendingRebuildCount = 0;
  s.pendingRebuildDurationUs = 0;
  s.pendingRebuildTrack = 255;
  s.pendingRebuildSlot = 255;
  s.pendingRebuildPath = PlaybackRebuildPath::CompatibilityPasses;
}

RT_FLASHMEM_FN void recordLoopRemainderIfMeasuring(bool measure, const char* span, uint32_t startUs) {
#if defined(SESSION_CAPTURE)
  if (!measure) {
    return;
  }
  DebugSessionCapture::recordLoopRemainderSpan(span, micros() - startUs);
#else
  (void)measure;
  (void)span;
  (void)startUs;
#endif
}

RT_FLASHMEM_FN void recordMidiLedHelperRem(bool measure, uint8_t helper, uint32_t startUs) {
#if defined(SESSION_CAPTURE) && defined(__IMXRT1062__)
  // PROGMEM (.progmem) stays in flash. Ordinary string literals become .rodata
  // and the Teensy 4 linker places all .rodata* in DTCM, which sits on the
  // _VectorsRam 1 KB align edge (+4 KB RAM1).
  const char* span = PSTR("midi_led_lookup");
  if (helper == 1) {
    span = PSTR("midi_led_analyze");
  } else if (helper == 2) {
    span = PSTR("midi_led_bars");
  } else if (helper == 3) {
    span = PSTR("midi_led_gather");
  }
  recordLoopRemainderIfMeasuring(measure, span, startUs);
#elif defined(SESSION_CAPTURE)
  const char* span = "midi_led_lookup";
  if (helper == 1) {
    span = "midi_led_analyze";
  } else if (helper == 2) {
    span = "midi_led_bars";
  } else if (helper == 3) {
    span = "midi_led_gather";
  }
  recordLoopRemainderIfMeasuring(measure, span, startUs);
#else
  (void)measure;
  (void)helper;
  (void)startUs;
#endif
}

RT_FLASHMEM_FN void recordLoadFrameChildRem(uint8_t child, uint32_t startUs) {
#if defined(SESSION_CAPTURE) && defined(__IMXRT1062__)
  const char* span = PSTR("display_frame");
  if (child == 1) {
    span = PSTR("load_job");
  } else if (child == 2) {
    span = PSTR("first_commit");
  } else if (child == 3) {
    span = PSTR("boot_commit");
  }
  recordLoopRemainderIfMeasuring(true, span, startUs);
#elif defined(SESSION_CAPTURE)
  const char* span = "display_frame";
  if (child == 1) {
    span = "load_job";
  } else if (child == 2) {
    span = "first_commit";
  } else if (child == 3) {
    span = "boot_commit";
  }
  recordLoopRemainderIfMeasuring(true, span, startUs);
#else
  (void)child;
  (void)startUs;
#endif
}

RT_FLASHMEM_FN void recordIdleMaintChildRem(uint8_t child, uint32_t startUs) {
#if defined(SESSION_CAPTURE) && defined(__IMXRT1062__)
  const char* span = PSTR("idle_gather");
  if (child == 1) {
    span = PSTR("idle_reconstruct");
  } else if (child == 2) {
    span = PSTR("idle_append");
  }
  recordLoopRemainderIfMeasuring(true, span, startUs);
#elif defined(SESSION_CAPTURE)
  const char* span = "idle_gather";
  if (child == 1) {
    span = "idle_reconstruct";
  } else if (child == 2) {
    span = "idle_append";
  }
  recordLoopRemainderIfMeasuring(true, span, startUs);
#else
  (void)child;
  (void)startUs;
#endif
}

RT_FLASHMEM_FN bool maybeEmit(uint32_t nowUs) {
  emitPendingDeadlineOneShots();
  State& s = state();
  if (s.windowStartUs == 0) {
    s.windowStartUs = nowUs;
    s.lastEmitUs = nowUs;
    return false;
  }
  if (s.lastEmitUs != 0 && (nowUs - s.lastEmitUs) < kEmitIntervalUs) {
    return false;
  }
  const uint32_t windowElapsedUs = nowUs - s.windowStartUs;
  emitWindow(s, nowUs, windowElapsedUs);
  clearWindow(s);
  s.lateOneShotArmed = true;
  s.windowStartUs = nowUs;
  s.lastEmitUs = nowUs;
  return true;
}

RT_FLASHMEM_FN Snapshot peek(uint32_t nowUs) {
  const State& s = state();
  Snapshot out;
  out.midiGapMaxUs = s.midiGap.maxUs;
  out.midiGapOverCount = s.midiGap.overCount;
  out.midiInputMaxUs = s.midiInput.maxUs;
  out.midiInputOverCount = s.midiInput.overCount;
  out.captureStampErrorMaxTicks = s.captureStampMaxTicks;
  out.captureStampSamples = s.captureStampSamples;
  out.replenishWaitMaxUs = s.replenishWait.maxUs;
  out.replenishWorkMaxUs = s.replenishWork.maxUs;
  out.replenishRefusedCount = s.replenishRefused;
  out.replenishCompletionSamples = s.replenishPairs;
  out.clkMaxUs = s.clk.maxUs;
  out.clkOverCount = s.clk.overCount;
  out.tracksMaxUs = s.tracks.maxUs;
  out.tracksOverCount = s.tracks.overCount;
  out.usbdevMaxUs = s.usbdev.maxUs;
  out.usbdevOverCount = s.usbdev.overCount;
  out.dinMaxUs = s.din.maxUs;
  out.dinOverCount = s.din.overCount;
  out.hosttaskMaxUs = s.hosttask.maxUs;
  out.hosttaskOverCount = s.hosttask.overCount;
  out.hostdrainMaxUs = s.hostdrain.maxUs;
  out.hostdrainOverCount = s.hostdrain.overCount;
  out.usbreadMaxUs = s.usbread.maxUs;
  out.usbreadOverCount = s.usbread.overCount;
  out.usbdispMaxUs = s.usbdisp.maxUs;
  out.usbdispOverCount = s.usbdisp.overCount;
  out.usbcapMaxUs = s.usbcap.maxUs;
  out.usbcapOverCount = s.usbcap.overCount;
  out.usbthruMaxUs = s.usbthru.maxUs;
  out.usbthruOverCount = s.usbthru.overCount;
  out.usbclkMaxUs = s.usbclk.maxUs;
  out.usbclkOverCount = s.usbclk.overCount;
  out.usbnoteMaxUs = s.usbnote.maxUs;
  out.usbnoteOverCount = s.usbnote.overCount;
  out.usbccMaxUs = s.usbcc.maxUs;
  out.usbccOverCount = s.usbcc.overCount;
  out.usbtransMaxUs = s.usbtrans.maxUs;
  out.usbtransOverCount = s.usbtrans.overCount;
  out.noteappendMaxUs = s.noteappend.maxUs;
  out.noteappendOverCount = s.noteappend.overCount;
  out.notechgMaxUs = s.notechg.maxUs;
  out.notechgOverCount = s.notechg.overCount;
  out.notereconMaxUs = s.noterecon.maxUs;
  out.notereconOverCount = s.noterecon.overCount;
  out.notepairMaxUs = s.notepair.maxUs;
  out.notepairOverCount = s.notepair.overCount;
  out.idleMaintMaxUs = s.idleMaint.maxUs;
  out.idleMaintOverCount = s.idleMaint.overCount;
  out.loadFrameMaxUs = s.loadFrame.maxUs;
  out.loadFrameOverCount = s.loadFrame.overCount;
  out.persistSaveMaxUs = s.persistSave.maxUs;
  out.persistSaveOverCount = s.persistSave.overCount;
  out.lateOnMaxUs = s.lateOn.maxUs;
  out.lateOnCount = s.lateOn.overCount;
  out.lateOffMaxUs = s.lateOff.maxUs;
  out.lateOffCount = s.lateOff.overCount;
  out.lateClkMaxUs = s.lateClk.maxUs;
  out.lateClkCount = s.lateClk.overCount;
  out.extclkArrivalMaxUs = s.extclkArr.maxUs;
  out.extclkArrivalCount = s.extclkArr.overCount;
  out.extclkDispatchMaxUs = s.extclkDisp.maxUs;
  out.extclkDispatchOverCount = s.extclkDisp.overCount;
  out.clkBatchMax = s.clkBatchMax;
  out.clkBatchCount = s.clkBatchCount;
  out.servicePeriodUs = s.windowServicePeriodUs;
  out.usbBatchMax = s.usbBatchMax;
  out.usbBatchSaturatedCount = s.usbBatchSaturatedCount;
  out.usbBatchClockMax = s.usbBatchClockMax;
  out.usbBatchConsecutiveClockMax = s.usbBatchConsecutiveClockMax;
  out.trackRebuildMaxUs = s.trackRebuild.maxUs;
  out.trackRebuildOverCount = s.trackRebuild.overCount;
  out.trackOrderMaxUs = s.trackOrder.maxUs;
  out.trackOrderOverCount = s.trackOrder.overCount;
  out.trackSendMaxUs = s.trackSend.maxUs;
  out.trackSendOverCount = s.trackSend.overCount;
  out.trackSlotMaxUs = s.trackSlot.maxUs;
  out.trackSlotOverCount = s.trackSlot.overCount;
  out.trackResidMaxUs = s.trackResid.maxUs;
  out.trackResidOverCount = s.trackResid.overCount;
  for (uint8_t i = 0; i < MidiBatchComposition::kBucketCount; ++i) {
    out.usbBatchSizeBuckets[i] = s.usbBatchSizeBuckets[i];
    out.usbBatchClockBuckets[i] = s.usbBatchClockBuckets[i];
    out.usbBatchClockDispatchMaxUs[i] = s.usbBatchClockDispatchMaxUs[i];
  }
  out.lateOneShotArmed = s.lateOneShotArmed;
  out.pendingLateEvent = s.pendingLateEvent;
  out.pendingLateSendUs = s.pendingLateSendUs;
  out.pendingLateDueUs = s.pendingLateDueUs;
  out.pendingLateDeadlineUs = s.pendingLateDeadlineUs;
  out.pendingPlaybackRebuild = s.pendingPlaybackRebuild;
  out.pendingRebuildDurationUs = s.pendingRebuildDurationUs;
  out.pendingRebuildCount = s.pendingRebuildCount;
  out.pendingRebuildTrack = s.pendingRebuildTrack;
  out.pendingRebuildSlot = s.pendingRebuildSlot;
  out.pendingRebuildPath = s.pendingRebuildPath;
  out.pendingRebuildAtUs = s.pendingRebuildAtUs;
  out.pendingRebuildPreviousPhase = s.pendingRebuildPreviousPhase;
  out.pendingRebuildCurrentPhase = s.pendingRebuildCurrentPhase;
  out.pendingRebuildEventCount = s.pendingRebuildEventCount;
  out.clockPulses = s.clockPulses;
  if (s.windowStartUs != 0 && nowUs != 0) {
    out.windowElapsedUs = nowUs - s.windowStartUs;
  }
  return out;
}

}  // namespace RuntimeTimingTelemetry

#endif  // SESSION_CAPTURE || PIO_UNIT_TEST_NATIVE
