//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file DebugSessionCapture.h
 * @brief Session fixture recording for the instrumented capture build (Bucket 1 S1).
 *
 * Emits machine-parseable `#CAP,...` lines on USB serial so a hardware session can be
 * replayed offline (native harness) and turned into regression fixtures.
 *
 * Only active when built with -D SESSION_CAPTURE (env:teensy41-capture-serial).
 * In all other builds every SC_* macro compiles to nothing.
 *
 * Capture producers use FLASHMEM append paths into a PSRAM ring; flushCaptureBuffer runs from
 * RAM each main-loop iteration and prints to USB serial (teensy41-capture-serial RAM budget).
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "Utils/DiagnosticsTypes.h"

#ifdef SESSION_CAPTURE

// Compile-time tag gates. Default on so teensy41-capture-serial keeps the HITL #CAP
// contract. Set a tag to 0 to drop that family at queue time (investigation builds).
#ifndef SESSION_CAPTURE_LED
#define SESSION_CAPTURE_LED 1
#endif
#ifndef SESSION_CAPTURE_MO
#define SESSION_CAPTURE_MO 1
#endif
#ifndef SESSION_CAPTURE_DIAG
#define SESSION_CAPTURE_DIAG 1
#endif
// Per-grain stop diagnostics (`DIAG,stop_grain`). Default on and sampled one-in-sixteen. A build
// that sets this to 0 drops the family at compile time and keeps the per-phase aggregate
// (`DIAG,stop_phase`), so an acceptance run's ring budget goes to the snapshot-publication rows
// instead of the stop-job flood. See env `teensy41-capture-serial-no-grain`.
#ifndef SESSION_CAPTURE_GRAIN
#define SESSION_CAPTURE_GRAIN 1
#endif
// Deferred DROID USB-host feedback queue telemetry (`DIAG,droid_queue`). Off in the default capture
// env (`teensy41-capture-serial` sets it to 0), on in `teensy41-capture-serial-investigation-diag`
// as the explicit debug gate. The emitter fires on every enqueue/coalesce, so a HITL run that drives
// the control-surface LEDs logs thousands of rows: the 2026-09-18 eight-bar record+overdub
// acceptance fixture logged 5,636 rows (~310 KB) and, with the per-slice persistence trace,
// overflowed the 1 MiB capture ring, evicting the `snapshot_unit`/`ST` rows the base-run oracle and
// the content-verification dump are read for. The macro default stays 1 so a build that names no env
// (native suites, or a bare `-D SESSION_CAPTURE=1`) keeps the family; a build that sets it to 0
// drops it at compile time while keeping the counters and the overflow warning (`logger.log` on the
// prompt channel). See envs `teensy41-capture-serial-no-grain` (acceptance) and
// `teensy41-capture-serial-investigation-diag`.
#ifndef SESSION_CAPTURE_DROID_QUEUE
#define SESSION_CAPTURE_DROID_QUEUE 1
#endif
// LoopContentResolution / capture-ledger diagnostics (`DIAG,lcr,*`). Default on. The family is
// emitted per prepared source span, per resolution slice and per capture-ledger mismatch, so a
// large loaded loop floods the ring: the 2026-09-18 acceptance fixture logged 12,733
// `DIAG,lcr,srcspan` rows of 13,173 `#CAP` lines and overflowed the 1 MiB capture ring twice
// (`captures/rc2-settle-device-b`, `RING,overflow` 44.9 MB, `overflow_free=False`), evicting the
// note/ST rows the base-run oracle and the content-verification dump are read for. A build that
// sets this to 0 gates the family at its emitter blocks (all lcr-only), so no other diagnostic is
// affected and the suppressed build carries no extra code. See env
// `teensy41-capture-serial-no-grain` (the acceptance target).
#ifndef SESSION_CAPTURE_LCR
#define SESSION_CAPTURE_LCR 1
#endif
// Investigation-only high-volume DIAG families (cer_* / lcr,vch,gather_* / seal_companion).
// Keep off by default for responsive capture sessions; enable in dedicated targets when needed.
#ifndef SESSION_CAPTURE_INVESTIGATION_DIAG
#define SESSION_CAPTURE_INVESTIGATION_DIAG 0
#endif
// Immediate Serial breadcrumbs in rebuildVisualCacheIdleSlice. Off by default.
#ifndef SESSION_CAPTURE_VCACHE_SLICE
#define SESSION_CAPTURE_VCACHE_SLICE 0
#endif
// High-volume families that stretch USB serial and the clock-path BPM sample
// (session_20260901_213738: 46k snapshot_idle, 8.5k BPM, 2k draw_src). Off by
// default; re-enable independently or via teensy41-capture-serial-investigation-diag.
#ifndef SESSION_CAPTURE_SNAPSHOT_IDLE
#define SESSION_CAPTURE_SNAPSHOT_IDLE 0
#endif
#ifndef SESSION_CAPTURE_BPM
#define SESSION_CAPTURE_BPM 0
#endif
#ifndef SESSION_CAPTURE_DRAW_SRC
#define SESSION_CAPTURE_DRAW_SRC 0
#endif

#include "MidiEvent.h"

#if defined(__IMXRT1062__)
#include <Arduino.h>
#define SC_MEM_ATTR FLASHMEM
#else
#define SC_MEM_ATTR
#endif

namespace DebugSessionCapture {

/** Heap field when capture telemetry skips live sampling (direct query hangs Teensy). */
constexpr uint32_t kUnsampledHeapBytes = UINT32_MAX;

struct PendingRevt {
  uint32_t tick;
  uint8_t ch;
  uint8_t note;
};

/** Allocate the external-memory capture ring (no-op when PSRAM unavailable). */
SC_MEM_ATTR void initCaptureBuffer();

/** True for ~5s after initCaptureBuffer (legacy; hot-path CAP lines always prefer the ring). */
bool captureBootGraceActive();

/** Reset the boot-grace timer (call after long setup, before first display paints). */
void restartCaptureBootGrace();

/** Queue a deferred serial line (#DBG, PERF, …) for flushCaptureBuffer. */
SC_MEM_ATTR void appendCaptureTextLine(const char* line);

/**
 * The capture ring's own PSRAM range, when it is allocated.
 *
 * The ring and the loop-event pool blocks share the Teensy `extmem` smalloc pool, and frees are
 * classified by address window alone, so a mis-freed block would let a pool block be handed the
 * ring's memory and zero it (`captures/s4f22-step8-e3`: `head_bad type=0 plen=0`, all-zero prefix,
 * self-consistent head/tail/used -- 384 571 bytes of content lost). `poolAlloc` calls this to
 * report an overlap instead of leaving it to be discovered as a wipe.
 */
SC_MEM_ATTR bool captureRingRange(uintptr_t& start, size_t& bytes);

/**
 * Drain up to @p maxRecords from the PSRAM ring to USB serial.
 * Runs from RAM (not FLASHMEM) — safe for Serial and ring access from main loop.
 */
void flushCaptureBuffer(size_t maxRecords = 64);

SC_MEM_ATTR void sessionHeader();
SC_MEM_ATTR void midiIn(char src, uint8_t type, uint8_t ch, uint8_t d1, uint8_t d2);
SC_MEM_ATTR void midiOut(uint8_t type, uint8_t ch, uint8_t d1, uint8_t d2);
SC_MEM_ATTR void midiOutEvent(const MidiEvent& e);
SC_MEM_ATTR void ledOut(bool on, uint8_t note, uint8_t vel);
SC_MEM_ATTR void gesture(uint8_t channel0, uint8_t note, int pressType);
SC_MEM_ATTR void stateTransition(const char* component, const char* from, const char* to);
SC_MEM_ATTR void bpm(float raw, float smoothed);
SC_MEM_ATTR void clockSource(const char* from, const char* to);
SC_MEM_ATTR void recStart(uint8_t slot, uint32_t tick);
SC_MEM_ATTR void recStop(const char* kind, uint8_t slot, uint32_t tick, uint32_t startTick,
                         uint32_t rawLength, uint32_t finalLength, bool align);
/// Record-stop **slot-identity** diagnostic (S1.3 save/restore-survival investigation). One line per
/// record start and one per persistence admission, so a recording's slot can be followed from capture
/// through the commit side effects to the admitted persistence key. Emitted under session capture only;
/// it exists because the observed failure was a *targeting* mismatch (recorded slot vs persisted slot),
/// which no existing record correlates.
SC_MEM_ATTR void recSlotIdentity(const char* event, uint32_t takeId, uint8_t trackIndex,
                                 uint8_t startSlot, uint8_t stopSlot, uint8_t commitSlot,
                                 uint8_t persistTrack, uint8_t persistSlot, char reason, char work,
                                 uint8_t slotChanged);
SC_MEM_ATTR void recStopStage(const char* stage, uint32_t elapsedUs, uint32_t durationUs,
                              uint32_t heapBefore, uint32_t heapAfter, size_t eventCount,
                              size_t chunkRefCount, const char* outcome);
SC_MEM_ATTR void overdubStartStage(const char* stage, uint32_t durationUs, uint32_t heapBefore,
                                   uint32_t heapAfter, const char* outcome);
SC_MEM_ATTR void capturePrewarm(uint8_t track, uint8_t slot, bool fullViews,
                               bool ready, uint16_t credits, uint16_t freeChunks,
                               uint32_t heapFree);
/** Boot-only external-pool admission sample: DIAG,capture_prewarm,phase=<before|after>,
 *  psram_free, psram_pool, heap_free, free_chunks. Walks the smalloc pool, so it is
 *  called only from the pre-transport boot prewarm. */
SC_MEM_ATTR void capturePrewarmMemory(const char* phase);
/** S1.3/S2b.3 one-shot post-restore census checkpoint (Tier-A):
 *  DIAG,post_restore_census,free_chunks,admission_complete,interactive_ready,undo_hydrated,
 *  transport_stopped,deferred_prewarm_idle,heap_free. Emitted at most once per boot, only when every
 *  readiness condition holds; see `StorageManager::emitPostRestoreCensusIfReady`. */
SC_MEM_ATTR void postRestoreCensus(uint16_t freeChunks, uint32_t heapFree);
SC_MEM_ATTR void overdubStopStage(const char* stage, uint32_t elapsedUs, uint32_t durationUs,
                                  uint32_t heapBefore, uint32_t heapAfter, size_t eventCount,
                                  size_t chunkRefCount, const char* outcome, uint8_t track = 0xFF);
/** Why `track` exists (appended to the wire row as a trailing `,track=%u`, so a positional reader that stops at
 *  `outcome` is unaffected): the family previously carried **neither slot nor track**, so on a multi-track run a
 *  denial could not be attributed to the track that met it -- the same gap `DIAG,latch` closed for the
 *  admitted/waiting path, left open on the denial path. `0xFF` means "not supplied" (the house sentinel, as in
 *  `#STOP_PREFIX`'s `ready=255`), which is what the sites that cannot name their track emit. */

/** The accepted-interaction (DEC-078) latch's own sequenced evidence, one shot per latch transition from the
 *  latch's owner. Wire form: `DIAG,latch,<transition>,<track>,<slot>,<interval>,<destination>,<outcome>`.
 *
 *  Why it exists (OpenSpec `response-driven-hitl-acceptance` task 2.2): the latch can be **accepted and
 *  consumed inside one poll gap** -- the deferred lane runs many turns per second -- so a poller that sees the
 *  latch empty before and after cannot tell that it was ever held. The wait rows that do exist
 *  (`ODUB,stop,admission,…,accepted_stop_finalizer_wait`) name no slot, so on a multi-track run they cannot be
 *  attributed to the interval that owes them. This row carries the identity the latch holds, on every edge.
 *
 *  `transition` is a stable literal: `accept`, `repeat` (the documented no-op for a repeat of the same stop),
 *  `wait`, `park_stall`, `consume`. `interval` is the latch's `captureInterval_` printed as its low 32 bits, so
 *  the device row and a native fixture read the same number. `destination` is 0/1 (Playing/Stopped) and
 *  `outcome` names the state or the consumption reason (`admitted`, `superseded`, `sync_stop`, …). Tier-A: the
 *  edges are one-shot per episode and their loss must be reported, not silent. */
SC_MEM_ATTR void interactionLatchTransition(const char* transition, uint8_t track, uint8_t slot,
                                            uint64_t interval, uint8_t destination,
                                            const char* outcome);
/** Wrap-tick children only: DIAG,wrap,total=<us>,seal=<us>,pass_apply=<us>,src_append=<us>,
 *  commit=<us>,seal_pend=<us>,begin=<us>. */
SC_MEM_ATTR void overdubWrapTick(uint32_t totalUs, uint32_t sealUs, uint32_t passApplyUs,
                                 uint32_t srcAppendUs, uint32_t commitUs, uint32_t sealPendUs,
                                 uint32_t beginUs);
SC_MEM_ATTR void architectureCounter(const char* name, uint32_t value);
SC_MEM_ATTR void memoryPressureTransition(const char* transitionLabel, uint32_t heapFreeBytes,
                                          uint16_t chunksFree, uint16_t persistQueueDepth);
SC_MEM_ATTR void captureAppendDeny(const char* reason, uint16_t freeChunks, uint16_t usedChunks,
                                   const char* pressure, uint8_t ch, uint8_t note, uint32_t tick,
                                   uint8_t pendingPass);
SC_MEM_ATTR void passReclaim(uint16_t chunksFreeBefore, uint16_t chunksFreeAfter,
                             uint16_t passesReclaimed, uint16_t chunksReleased,
                             uint32_t durationUs, const char* pressure, uint8_t transport,
                             uint16_t capturePins, uint16_t editPins, uint16_t slotsWithPins);
SC_MEM_ATTR void architectureTiming(const char* name, uint32_t sumMicros, uint32_t sampleCount);
SC_MEM_ATTR void architectureTimingMax(const char* name, uint32_t maxMicros);
/** S0 timing telemetry: DIAG,{midi_gap|midi_input|clk|tracks|usbdev|din|hosttask|hostdrain|usbread|usbdisp|usbcap|usbthru|usbclk|usbnote|usbcc|usbtrans|noteappend|notechg|noterecon|notepair|idle_maint|load_frame|persist_save|late_on|late_off|late_clk|extclk_arrival|extclk_dispatch|clk_batch|service_period|track_rebuild|track_order|track_send|track_slot|track_resid},<maxUs>,<overCount> (Tier-A). Historical captures used msi/midisvc for midi_gap/midi_input. late_* overCount is late_event_count. extclk_arrival is USB-device clock-batch arrival vs expected instant (not late_clk). clk_batch overCount is clock-bearing batch count. service_period overCount is 0. Stage 4a track_* spans are one `updateAllTracks` iteration; residual is track_update minus attributed spans. */
SC_MEM_ATTR void runtimeTimingTelemetry(const char* tag, uint32_t maxUs, uint32_t overCount);
/** Deferred DROID host feedback queue: depth, oldest age, and overflow count (Tier-A). */
SC_MEM_ATTR void droidFeedbackQueue(uint16_t depth, uint32_t oldestAgeUs, uint32_t overflowCount);
/** Per-track S0 timing telemetry: DIAG,track_update,<track>,<maxUs>,<overCount> (Tier-A). */
SC_MEM_ATTR void runtimeTimingTrackUpdate(uint8_t trackIndex, uint32_t maxUs,
                                          uint32_t overCount);
/** S0 timing telemetry: DIAG,clockrate,<pulsesPerSecond> (Tier-A). */
SC_MEM_ATTR void runtimeTimingClockrate(uint32_t pulsesPerSecond);
/** USB-device batch composition maxima: DIAG,usb_batch,<maxSize>,<saturated>,<maxClocks>,<maxConsecutive> (Tier-A). */
SC_MEM_ATTR void runtimeTimingUsbBatch(uint32_t maxSize, uint32_t saturatedCount, uint32_t maxClocks,
                                       uint32_t maxConsecutive);
/** USB-device batch buckets: DIAG,{usb_bkt_size|usb_bkt_clk|usb_bkt_disp},<b1_8>,<b9_16>,<b17_32>,<b33_64>,<b65_128> (Tier-A). */
SC_MEM_ATTR void runtimeTimingUsbBatchBuckets(const char* kind, const uint32_t buckets[5]);
/** Stop-phase summary, device timestamps; maximum-duration call plus all-call counts. */
SC_MEM_ATTR void stopPhaseTiming(uint8_t track, uint8_t slot, uint8_t phase,
    uint32_t generation, uint32_t epoch, uint8_t destination, uint32_t calls,
    uint32_t crossed, uint32_t unknown, uint32_t startUs, uint32_t endUs, uint32_t slackUs);

/** Stop-finalization device-clock trace records (Tier-A). */
SC_MEM_ATTR void stopGrainTiming(uint8_t track, uint8_t slot, uint8_t phase,
    uint32_t generation, uint32_t epoch, uint8_t destination, uint32_t serviceSequence,
    uint32_t grainStartUs, uint32_t grainEndUs, uint32_t serviceActualUs,
    uint32_t serviceDueUs, uint32_t serviceDeadlineUs, uint32_t servicePeriodUs,
    uint32_t slackUs, uint8_t finishedPhase);
SC_MEM_ATTR void stopStateTiming(uint8_t track, uint8_t slot, uint8_t fromState,
    uint8_t toState, uint32_t tick, uint32_t atUs, uint32_t serviceSequence,
    uint32_t serviceDueUs, uint32_t serviceDeadlineUs, uint32_t servicePeriodUs);
SC_MEM_ATTR void stopServiceTiming(uint32_t serviceSequence, uint32_t actualUs,
    uint32_t dueUs, uint32_t deadlineUs, uint32_t periodUs);
SC_MEM_ATTR void stopMidiEmissionTiming(uint32_t serviceSequence, uint8_t isNoteOn,
    uint32_t tick, uint32_t sendUs, uint32_t dueUs, uint32_t deadlineUs,
    uint32_t latenessUs, uint8_t track, uint8_t slot, uint8_t channel, uint8_t note);
SC_MEM_ATTR void stopTimelineDropped(uint32_t droppedRecords);

/** First late MIDI send in a 5 s window: DIAG,late_event,<on|off|clk>,<latenessUs>,<tick>,<sendUs>,<dueUs>,<deadlineUs> (Tier-A). Due is cadence-derived; the CAP timestamp is emission time, not send time. */
SC_MEM_ATTR void midiDeadlineLateEvent(const char* midiClass, uint32_t latenessUs, uint32_t tick,
                                      uint32_t sendUs, uint32_t dueUs, uint32_t deadlineUs);
/** Playback gather rebuild: DIAG,playback_build,<path>,... Tier-A path labels include
 * snapshot, overlay, session_exit_compat, hold_empty, compatibility, incremental, note_edit.
 * Trailing fields: rebuildStartUs, previousPhase, currentPhase, eventCount.
 * A zero rebuildStartUs means legacy/unspecified occurrence context. */
SC_MEM_ATTR void playbackRebuild(uint32_t durationUs, uint32_t windowStartTick,
                                 uint32_t windowLengthTicks, uint32_t revision, uint8_t trackIndex,
                                 uint8_t slotIndex, uint32_t rebuildCount, const char* path,
                                 uint32_t atUs = 0, uint32_t previousPhase = UINT32_MAX,
                                 uint32_t currentPhase = 0, uint32_t eventCount = 0);
/** Playback-order rebuild on the same tick as gather: DIAG,playback_order,<durationUs>,<eventCount> (Tier-A). */
SC_MEM_ATTR void playbackOrder(uint32_t durationUs, uint32_t eventCount);
/** The deferred predecessor stop has frozen its interval and handed the live capture role
 *  back, so a successor interval may start (DEC-067, the `successor_start_window_open`
 *  edge). One shot per deferred stop.
 *  Wire form: DIAG,reentry_window_open,<slot>,<stopTick>,<loopLengthTicks>.
 *  The tag stays short on purpose: a longer capture format string tipped a `.data`/`.bss`
 *  alignment snap that cost 4 KB of the RAM1 local allowance (see Teensy-RAM1-Bank-Overflow). */
SC_MEM_ATTR void successorStartWindowOpen(uint8_t slot, uint32_t stopTick, uint32_t loopLengthTicks);
/** The deferred stop's finalization resources are free again -- canonical copy target empty,
 *  transfer chunks admitted, no stop owner -- so a successor's stop may be admitted
 *  (DEC-067, the `canonical_finalization_capacity_released` edge). One shot per retired stop.
 *  Wire form: DIAG,canonical_release,<slot>,<generation>,<loopLengthTicks>,<availableChunks>.
 *  `<slot>` and `<generation>` name the **retiring owner**, not the currently active slot: the marker is
 *  armed on the retiring job's runtime, and the active slot has usually moved by the time this fires.
 *  The timestamp is the first maintenance turn at which the resources were observed free, so it is bounded
 *  by the maintenance period rather than being the exact instant. */
SC_MEM_ATTR void canonicalFinalizationCapacityReleased(uint8_t slot, uint32_t generation,
                                                      uint32_t loopLengthTicks,
                                                      size_t availableChunks);
/** First-record return window selected as the audible source:
 *  DIAG,first_record_adopt,<loopLengthTicks>,<retainedEvents> (Tier-A). One line
 *  per adoption; the retained count shows how much of bar 1 the window held. */
/** The slot's **working-set lease** was released, by identity: the `(slot, generation)` release entry
 *  accepted that identity and the lease generation advanced (S4.1/S4.2). One shot per released slot.
 *  Wire form: DIAG,ws_release,<slot>,<generation>,<outcome>.
 *  `canonical_release` reports the retiring **occupancy** -- a different resource -- so without this row
 *  the working-set lease release could only be *inferred* from whichever sweep happened to run it. The
 *  `outcome` literal keeps the refusal visible: the pre-S4.1 sweep discarded it. */
SC_MEM_ATTR void workingSetLeaseReleased(uint8_t slot, uint16_t generation, const char* outcome);
SC_MEM_ATTR void firstRecordReturnWindowAdopted(uint32_t loopLengthTicks, size_t retainedEvents);
/** Compact reentry admission/denial record (OpenSpec successor-capture-reentry-unguarding 5.6).
 *  One shot per reentry decision, from the decision owner itself -- never a per-event line.
 *  Wire form: DIAG,reentry,<decision>,<tick>,<interval>,<slot>,<depth>.
 *  `decision` is a stable literal (`admit`, `deny`, `deny_resource`); `interval` is the track's
 *  capture interval identity and `depth` the stop-descriptor queue depth at the decision. */
SC_MEM_ATTR void reentryDecision(const char* decision, uint32_t actionTick, uint64_t interval,
                                 uint8_t slot, size_t queueDepth);
/** Post-BAR remainder one-shot: DIAG,loop_rem,<span>,<us>,<track>,<slot>,<phase>,<focus> (Tier-A). */
SC_MEM_ATTR void loopRemainder(const char* span, uint32_t durationUs, uint8_t track, uint8_t slot,
                               uint8_t phase, uint8_t isFocus);
/** Emit loopRemainder when duration is at least kLoopRemainderOneShotUs. */
SC_MEM_ATTR void recordLoopRemainderSpan(const char* span, uint32_t durationUs);
SC_MEM_ATTR void persistence(const char* stage, uint32_t durationUs, uint32_t heapBefore,
                             uint32_t heapAfter, const char* outcome);
SC_MEM_ATTR void persistenceResultImmediate(uint32_t durationUs, uint32_t heapBefore,
                                           uint32_t heapAfter, const char* outcome);
/// A1.8/A2.1 (design D6): one event-shaped line per restore **request state transition**, carrying the
/// full identity and the named reason/outcome. Event-shaped, not per-turn: the capture-ring precedent
/// showed a per-turn family evicts the rows a run is read for. `state`, `wait` and `terminal` are the
/// static name strings from `restoreRequestStateName`/`WaitName`/`TerminalName`.
SC_MEM_ATTR void restoreRequest(uint8_t track, uint8_t slot, uint32_t requestGeneration,
                                uint32_t setEpoch, const char* state, const char* wait,
                                const char* terminal);
/// `restored-library-stability` 1.3: the on-demand library observation's record family (`LIBOBS`), a
/// separate family from the settled `post_restore_census` checkpoint. Three record shapes: `begin`
/// (window open), one slot record per observed `(track, slot)`, and `end` (the four D4 counts and the
/// window framing). Event-shaped and bounded by the caller's per-invocation emission bound.
///
/// The wire form is **compact on purpose**: the session-capture text cap is
/// `kMaxCaptureTextBytes = 192`, and a full-name record exceeds it and is silently truncated (measured
/// on device 2026-09-24, where the frame's trailing fields were lost). Enum fields are emitted as codes
/// (`LibraryObservation::causeCode` / `payloadCode` / `offeredCode` / `requestCode` / `terminalCode`) and
/// keys are short; the host parser owns the reverse mapping.
SC_MEM_ATTR void libraryObservationBegin(uint32_t windowStartUs, uint32_t setEpoch, uint16_t slotCount);
SC_MEM_ATTR void libraryObservationSlot(uint8_t track, uint8_t slot, uint8_t stateMask, char cause,
                                        uint8_t payload, uint8_t offered, uint8_t request, uint8_t wait,
                                        uint8_t terminal, uint32_t requestGeneration, uint32_t setEpoch,
                                        uint8_t hasCommittedContent, uint8_t hasPassContent,
                                        uint8_t hasRootContent, uint8_t rootIsCurrent,
                                        uint16_t committedPassCount,
                                        uint32_t highestCommittedPassId, uint32_t loopLengthTicks,
                                        uint32_t committedEventCount, uint8_t requiredForInitialPlayback,
                                        uint8_t committedTerminalInWindow, uint8_t invalidated);
SC_MEM_ATTR void libraryObservationEnd(uint32_t windowStartUs, uint32_t windowEndUs, uint16_t turns,
                                       uint16_t restarts, uint16_t slotsObserved,
                                       uint16_t payloadInventory, uint16_t initialPlaybackRequired,
                                       uint16_t observedRestoreCompletions,
                                       uint16_t committedAtCheckpoint,
                                       uint16_t passBackedAtCheckpoint,
                                       uint16_t rootBackedAtCheckpoint, uint8_t admissionComplete,
                                       uint8_t everyInitialPlaybackSlotObserved,
                                       uint8_t consistentView, uint8_t complete);
/// S1.3 save/restore-survival: read back the bounded slot-identity path counters. Read-only and called
/// from the stopped-only HITL command (`!CAP_IDENTITY_STATS`), never from the record/persist path:
/// emitter entries, enqueue successes, enqueue rejections, transmissions and evictions, plus the last
/// identity the emitter was handed. Separates "branch never reached" from "capture disabled",
/// "enqueue rejected", "evicted" and "delivered but not parsed". Also emits independent
/// RING,record_stats rows for RECA and RECS. All counters cover the current boot.
SC_MEM_ATTR void emitIdentityPathStats();
SC_MEM_ATTR void persistenceBundle(const char* workType, uint32_t totalUs,
                                   uint32_t maxSliceUs, uint32_t sliceCount,
                                   uint32_t undoEntryCount, uint32_t snapshotCount);
SC_MEM_ATTR void persistenceDiagnostic(uint16_t freeChunks, uint16_t usedChunks, uint16_t reserve,
                                       uint16_t queueDepth, uint16_t writingChunks,
                                       uint32_t transportBlockCount, uint32_t heapFloorBlockCount,
                                       uint32_t budgetBlockCount, uint32_t sliceDoneCount,
                                       uint32_t peakWriterLatencyUs, uint32_t oldestDirtyAgeMs,
                                       uint32_t maxDeferredBacklog, uint8_t savePending,
                                       uint8_t saveInProgress, uint8_t captureActive);
SC_MEM_ATTR void persistencePoolPressure(uint16_t freeChunks, uint16_t reserve,
                                         uint16_t usedChunks);
SC_MEM_ATTR void persistenceBacklog(uint16_t workQueueDepth, uint16_t writingWorkItems,
                                    uint16_t chunkQueueDepth, uint32_t dirtyAgeMs,
                                    uint32_t estSliceSteps, uint32_t estSdBytes,
                                    uint8_t savePending, uint8_t urgentRequested,
                                    uint32_t transportBlockCount, uint32_t budgetBlockCount,
                                    uint32_t heapFloorBlockCount);
SC_MEM_ATTR void persistenceDrainFailed(const char* reason, uint32_t steps,
                                          uint32_t stuckIterations, uint16_t workQueueDepth,
                                          uint16_t chunkQueueDepth, uint32_t estSliceSteps,
                                          uint32_t estSdBytes);
SC_MEM_ATTR void saveDisplayPhase(const char* phase, uint8_t rotateStep);
SC_MEM_ATTR void loadSaveMode(uint8_t active);
SC_MEM_ATTR void overlayListSelection(uint8_t mode, uint8_t row);
SC_MEM_ATTR void overlayRowConfirm(uint8_t mode, uint8_t row);
SC_MEM_ATTR void queueStoredNoteOn(uint32_t tick, uint8_t ch, uint8_t note);
SC_MEM_ATTR void recStoredNoteOn(uint32_t tick, uint8_t ch, uint8_t note);
SC_MEM_ATTR void storedNoteEvent(char kind, uint32_t tick, uint8_t ch, uint8_t note);
/** One-shot idle inventory from a clean visual cache. Tier-A. Not a SEVT dump. */
SC_MEM_ATTR void storedNotes(uint8_t track, uint8_t slot, uint32_t notes, uint32_t uniqueNoteIds,
                             uint32_t maxSamePitch);
/** One-shot overdub-stop overlap totals. Tier-A. Not a SEVT dump. */
SC_MEM_ATTR void overlapHold(uint32_t noteOffs, uint32_t emptySets, uint32_t maxIds,
                             uint32_t overflows, uint32_t lookedUp, uint32_t maxExamined,
                             uint32_t sumExamined, uint32_t maxLookupUs, uint32_t sumLookupUs,
                             uint32_t add, uint32_t shorten, uint32_t hide);
SC_MEM_ATTR void captureCoordinate(uint32_t absTick, uint32_t storageTick, uint32_t projPhase,
                                   uint32_t displayPhase, uint32_t startLoopTick,
                                   int32_t projectionCycleStartTick, uint32_t loopStartTick,
                                   uint8_t ch, uint8_t note);
SC_MEM_ATTR void displaySnapshot(uint8_t slot, const char* trackState, uint32_t loopLen,
                                 size_t sourceEventCount, size_t visualNotes, size_t frameNotes,
                                 size_t bufferEvents, int hasCommittedPasses);
SC_MEM_ATTR void displaySnapshotWindow(uint8_t slot, const char* trackState, uint32_t loopLen,
                                       size_t sourceEventCount, size_t visualNotes, size_t frameNotes,
                                       size_t bufferEvents, int hasCommittedPasses, uint32_t windowStartTick,
                                       uint8_t windowBars, size_t windowNoteCount);
/**
 * Visual cache coverage at a pass-lifecycle or rebuild boundary. Distinguishes committed
 * content loss from cache under-coverage: `events` is the gathered committed event count when
 * the phase performed a gather and -1 otherwise; `firstBar`/`lastBar` bound the bars actually
 * holding cached notes, against `totalBars` for the whole loop.
 */
SC_MEM_ATTR void visualCacheState(const char* phase, int32_t events, uint32_t notes,
                                  uint32_t firstBar, uint32_t lastBar, uint32_t totalBars,
                                  uint32_t dirtyBarsSize, uint32_t dirtyCount, uint8_t dirtyFlag);
SC_MEM_ATTR void displayNoteInfo(uint8_t pitch, uint32_t storageStart, uint32_t displayStart,
                                 uint32_t length, int selectedIdx);
SC_MEM_ATTR void displayFrame(uint32_t frameNotes, uint32_t elapsedUs, uint32_t frameIndex,
                              uint32_t resolveUs = 0, uint32_t drawUs = 0, uint32_t transferUs = 0);
SC_MEM_ATTR void playbackFrame(uint8_t slot, uint32_t currentTick, uint32_t tickInLoop,
                               uint32_t prevTickInLoop, int32_t projectionCycleStartTick,
                               uint32_t loopStartTick, uint32_t loopLength, uint16_t indexBefore,
                               uint16_t indexAfter, uint16_t orderSize, uint16_t sent,
                               uint8_t atLoopStart, uint8_t wrapDetected, uint8_t wasStale,
                               uint32_t playbackRevision, uint32_t playbackGeneration);
SC_MEM_ATTR void storedWrapPair(uint32_t onTick, uint32_t offTick, uint8_t ch, uint8_t note);
SC_MEM_ATTR void pendingSamePitchNoteOnReject(uint8_t pitch, uint8_t channel,
                                              uint32_t pendingStartTick, uint32_t rejectedTick);
SC_MEM_ATTR void captureCleanup(const char* phase, const char* kind, uint32_t count);
SC_MEM_ATTR size_t flushPendingRevts(size_t maxLines = 64);
SC_MEM_ATTR void flushAllPendingRevts();
SC_MEM_ATTR void update(uint32_t currentTick, uint32_t ticksPerBar);

/** Append a fixed-size Diagnostics::DiagTraceRecord payload to the PSRAM ring. */
SC_MEM_ATTR bool appendDiagTraceRecord(const void* record, uint16_t recordSize);

/** Emit one DIAGCHK line immediately (boot dump after fault). */
void emitDiagCheckpointLine(const Diagnostics::DiagTraceRecord& record);

}  // namespace DebugSessionCapture

#define SC_SESSION_HEADER()                DebugSessionCapture::sessionHeader()
#define SC_MIDI_IN(src, type, ch, d1, d2)  DebugSessionCapture::midiIn(src, type, ch, d1, d2)
#define SC_MIDI_OUT_EVENT(e)               DebugSessionCapture::midiOutEvent(e)
#define SC_LED_OUT(on, note, vel)          DebugSessionCapture::ledOut(on, note, vel)
#define SC_GESTURE(ch0, note, pressType)   DebugSessionCapture::gesture(ch0, note, pressType)
#define SC_STATE(component, from, to)      DebugSessionCapture::stateTransition(component, from, to)
#if SESSION_CAPTURE_BPM
#define SC_BPM(raw, smoothed)              DebugSessionCapture::bpm(raw, smoothed)
#else
#define SC_BPM(raw, smoothed)              ((void)0)
#endif
#define SC_CLOCK_SOURCE(from, to)          DebugSessionCapture::clockSource(from, to)
#define SC_REC_START(slot, tick)           DebugSessionCapture::recStart(slot, tick)
#define SC_REC_STOP(kind, slot, tick, start, raw, final, align) \
                                           DebugSessionCapture::recStop(kind, slot, tick, start, raw, final, align)
#define SC_REC_SLOT_IDENTITY(event, takeId, trackIndex, startSlot, stopSlot, commitSlot, persistTrack, \
                             persistSlot, reason, work, slotChanged)                                \
  DebugSessionCapture::recSlotIdentity(event, takeId, trackIndex, startSlot, stopSlot, commitSlot,   \
                                       persistTrack, persistSlot, reason, work, slotChanged)
#define SC_REC_STOP_STAGE(stage, elapsedUs, durationUs, heapBefore, heapAfter, eventCount, chunkRefCount, outcome) \
                                           DebugSessionCapture::recStopStage(stage, elapsedUs, durationUs, heapBefore, heapAfter, eventCount, chunkRefCount, outcome)
#define SC_ODUB_STAGE(stage, durationUs, heapBefore, heapAfter, outcome) \
                                           DebugSessionCapture::overdubStartStage(stage, durationUs, heapBefore, heapAfter, outcome)
#define SC_ODUB_WRAP_TICK(totalUs, sealUs, passApplyUs, srcAppendUs, commitUs, sealPendUs, \
                          beginUs) \
  DebugSessionCapture::overdubWrapTick(totalUs, sealUs, passApplyUs, srcAppendUs, commitUs, \
                                       sealPendUs, beginUs)
#define SC_PLAYBACK_ORDER(durationUs, eventCount) \
  DebugSessionCapture::playbackOrder(durationUs, eventCount)
#define SC_DROID_FEEDBACK_QUEUE(depth, oldestAgeUs, overflowCount) \
  DebugSessionCapture::droidFeedbackQueue(depth, oldestAgeUs, overflowCount)
// Variadic on purpose: the 34 existing sites pass the eight positional fields, and a site that knows its track
// appends it as a ninth argument. A fixed-arity macro would have forced all 34 to change for a field most of them
// cannot supply.
#define SC_ODUB_STOP_STAGE(...) DebugSessionCapture::overdubStopStage(__VA_ARGS__)
// `#ifndef`-guarded so a native suite can substitute its own recorder for the latch rows without a capture
// build (the pattern `SC_LIBRARY_OBSERVATION_*` established).
#ifndef SC_ACCEPTED_LATCH
#define SC_ACCEPTED_LATCH(transition, track, slot, interval, destination, outcome) \
  DebugSessionCapture::interactionLatchTransition(transition, track, slot, interval, destination, outcome)
#endif
#define SC_PERSIST(stage, durationUs, heapBefore, heapAfter, outcome) \
                                           DebugSessionCapture::persistence(stage, durationUs, heapBefore, heapAfter, outcome)
/// `#ifndef`-guarded so a native suite can substitute its own recorder (the `SC_ACCEPTED_LATCH` pattern).
#ifndef SC_PERSIST_RESULT
/// The save-completion receipt, emitted directly when USB has room (see `persistenceResultImmediate`).
#define SC_PERSIST_RESULT(durationUs, heapBefore, heapAfter, outcome) \
  DebugSessionCapture::persistenceResultImmediate(durationUs, heapBefore, heapAfter, outcome)
#endif
#define SC_RESTORE_REQUEST(track, slot, requestGeneration, setEpoch, state, wait, terminal) \
  DebugSessionCapture::restoreRequest(track, slot, requestGeneration, setEpoch, state, wait, terminal)
// `restored-library-stability` 1.3. `#ifndef`-guarded so a native suite can substitute its own recorder
// for the observation's emitted records without a capture build.
#ifndef SC_LIBRARY_OBSERVATION_BEGIN
#define SC_LIBRARY_OBSERVATION_BEGIN(windowStartUs, setEpoch, slotCount) \
  DebugSessionCapture::libraryObservationBegin(windowStartUs, setEpoch, slotCount)
#endif
#ifndef SC_LIBRARY_OBSERVATION_SLOT
#define SC_LIBRARY_OBSERVATION_SLOT(track, slot, state, cause, payload, offered, request, wait, \
                                    terminal, requestGeneration, setEpoch, hasCommittedContent, \
                                    hasPassContent, hasRootContent, rootIsCurrent,                 \
                                    committedPassCount, highestCommittedPassId, loopLengthTicks,  \
                                    committedEventCount, requiredForInitialPlayback,              \
                                    committedTerminalInWindow, invalidated)                       \
  DebugSessionCapture::libraryObservationSlot(                                                     \
      track, slot, state, cause, payload, offered, request, wait, terminal, requestGeneration,      \
      setEpoch, hasCommittedContent, hasPassContent, hasRootContent, rootIsCurrent,                 \
      committedPassCount, highestCommittedPassId, loopLengthTicks, committedEventCount,             \
      requiredForInitialPlayback, committedTerminalInWindow, invalidated)
#endif
#ifndef SC_LIBRARY_OBSERVATION_END
#define SC_LIBRARY_OBSERVATION_END(windowStartUs, windowEndUs, turns, restarts, slotsObserved, \
                                   payloadInventory, initialPlaybackRequired,                  \
                                   observedRestoreCompletions, committedAtCheckpoint,          \
                                   passBackedAtCheckpoint, rootBackedAtCheckpoint,             \
                                   admissionComplete, everyInitialPlaybackSlotObserved,        \
                                   consistentView, complete)                                   \
  DebugSessionCapture::libraryObservationEnd(                                                  \
      windowStartUs, windowEndUs, turns, restarts, slotsObserved, payloadInventory,            \
      initialPlaybackRequired, observedRestoreCompletions, committedAtCheckpoint,              \
      passBackedAtCheckpoint, rootBackedAtCheckpoint, admissionComplete,                       \
      everyInitialPlaybackSlotObserved, consistentView, complete)
#endif
#define SC_PERSIST_BUNDLE(workType, totalUs, maxSliceUs, sliceCount, undoEntryCount, snapshotCount) \
  DebugSessionCapture::persistenceBundle(workType, totalUs, maxSliceUs, sliceCount, undoEntryCount, snapshotCount)
#define SC_PERSIST_DIAG(freeChunks, usedChunks, reserve, queueDepth, writingChunks, transportBlk, \
                        heapBlk, budgetBlk, sliceDone, peakLatUs, dirtyAgeMs, maxBacklog, pending, \
                        inProg, captureActive) \
  DebugSessionCapture::persistenceDiagnostic(freeChunks, usedChunks, reserve, queueDepth, \
                                             writingChunks, transportBlk, heapBlk, budgetBlk, \
                                             sliceDone, peakLatUs, dirtyAgeMs, maxBacklog, pending, \
                                             inProg, captureActive)
#define SC_PERSIST_PRESSURE(freeChunks, reserve, usedChunks) \
  DebugSessionCapture::persistencePoolPressure(freeChunks, reserve, usedChunks)
#define SC_PERSIST_BACKLOG(workQ, writingItems, chunkQ, dirtyAgeMs, estSteps, estBytes, pending, \
                           urgent, transportBlk, budgetBlk, heapBlk) \
  DebugSessionCapture::persistenceBacklog(workQ, writingItems, chunkQ, dirtyAgeMs, estSteps, estBytes, \
                                          pending, urgent, transportBlk, budgetBlk, heapBlk)
#define SC_PERSIST_DRAIN_FAIL(reason, steps, stuckIter, workQ, chunkQ, estSteps, estBytes) \
  DebugSessionCapture::persistenceDrainFailed(reason, steps, stuckIter, workQ, chunkQ, estSteps, \
                                              estBytes)
#define SC_SAVE(phase, rotateStep)         DebugSessionCapture::saveDisplayPhase(phase, rotateStep)
#define SC_LOADSAVE(active)                DebugSessionCapture::loadSaveMode(active)
#define SC_OVERLAY_SEL(mode, row)          DebugSessionCapture::overlayListSelection(mode, row)
#define SC_OVERLAY_CONFIRM(mode, row)      DebugSessionCapture::overlayRowConfirm(mode, row)
#define SC_REC_STORED_NOTE_ON(tick, ch, note) DebugSessionCapture::recStoredNoteOn(tick, ch, note)
#define SC_STORED_NOTE_EVENT(kind, tick, ch, note) DebugSessionCapture::storedNoteEvent(kind, tick, ch, note)
#define SC_STORED_NOTES(track, slot, notes, uniqueNoteIds, maxSamePitch) \
  DebugSessionCapture::storedNotes(track, slot, notes, uniqueNoteIds, maxSamePitch)
#define SC_OVERLAP_HOLD(noteOffs, emptySets, maxIds, overflows, lookedUp, maxExamined, \
                        sumExamined, maxLookupUs, sumLookupUs, add, shorten, hide) \
  DebugSessionCapture::overlapHold(noteOffs, emptySets, maxIds, overflows, lookedUp, \
                                   maxExamined, sumExamined, maxLookupUs, sumLookupUs, add, \
                                   shorten, hide)
#define SC_CAPTURE_COORD(absTick, storageTick, projPhase, displayPhase, startLoopTick, \
                         projectionCycleStartTick, loopStartTick, ch, note) \
  DebugSessionCapture::captureCoordinate(absTick, storageTick, projPhase, displayPhase, \
                                         startLoopTick, projectionCycleStartTick, loopStartTick, \
                                         ch, note)
#define SC_DISP(slot, state, loopLen, take, visual, frame, buffer, hasCommittedPasses) \
  DebugSessionCapture::displaySnapshot(slot, state, loopLen, take, visual, frame, buffer, hasCommittedPasses)
#define SC_DISP_WINDOW(slot, state, loopLen, take, visual, frame, buffer, hasCommittedPasses, wStart, wBars, \
                      wNotes) \
  DebugSessionCapture::displaySnapshotWindow(slot, state, loopLen, take, visual, frame, buffer, \
                                             hasCommittedPasses, wStart, wBars, wNotes)
#define SC_VCACHE(phase, events, notes, firstBar, lastBar, totalBars, dirtyBarsSize, dirtyCount, \
                  dirtyFlag) \
  DebugSessionCapture::visualCacheState(phase, events, notes, firstBar, lastBar, totalBars, \
                                        dirtyBarsSize, dirtyCount, dirtyFlag)
#define SC_DNTE(pitch, storageStart, displayStart, length, selectedIdx) \
  DebugSessionCapture::displayNoteInfo(pitch, storageStart, displayStart, length, selectedIdx)
#define SC_DFRAME(frameNotes, elapsedUs, frameIndex, resolveUs, drawUs, transferUs) \
  DebugSessionCapture::displayFrame(frameNotes, elapsedUs, frameIndex, resolveUs, drawUs, \
                                    transferUs)
#define SC_PLAYBACK_FRAME(slot, currentTick, tickInLoop, prevTickInLoop, projStart, loopStart, \
                          loopLen, idxBefore, idxAfter, orderSize, sent, atLoopStart, wrap, stale, \
                          rev, gen) \
  DebugSessionCapture::playbackFrame(slot, currentTick, tickInLoop, prevTickInLoop, projStart, \
                                     loopStart, loopLen, idxBefore, idxAfter, orderSize, sent, \
                                     atLoopStart, wrap, stale, rev, gen)
#define SC_STORED_WRAP_PAIR(onTick, offTick, ch, note) DebugSessionCapture::storedWrapPair(onTick, offTick, ch, note)
#define SC_PENDING_SAME_PITCH_NOTE_ON_REJECT(pitch, ch, pendingStartTick, rejectedTick) \
  DebugSessionCapture::pendingSamePitchNoteOnReject(pitch, ch, pendingStartTick, rejectedTick)
#define SC_CAPTURE_CLEANUP(phase, kind, count) DebugSessionCapture::captureCleanup(phase, kind, count)
#define SC_REC_QUEUE_STORED_NOTE_ON(tick, ch, note) DebugSessionCapture::queueStoredNoteOn(tick, ch, note)
#define SC_REC_FLUSH_PENDING_REVTS(maxLines) DebugSessionCapture::flushCaptureBuffer(maxLines)
#define SC_REC_FLUSH_ALL_PENDING_REVTS()     DebugSessionCapture::flushAllPendingRevts()
#define SC_CAPTURE_FLUSH(maxRecords)         DebugSessionCapture::flushCaptureBuffer(maxRecords)
#define SC_UPDATE(tick, ticksPerBar)         DebugSessionCapture::update(tick, ticksPerBar)
#define SC_MEMORY_PRESSURE(transition, heapFree, chunksFree, queueDepth) \
  DebugSessionCapture::memoryPressureTransition(transition, heapFree, chunksFree, queueDepth)
#define SC_CAPTURE_APPEND_DENY(reason, freeChunks, usedChunks, pressure, ch, note, tick, pendingPass) \
  DebugSessionCapture::captureAppendDeny(reason, freeChunks, usedChunks, pressure, ch, note, tick, \
                                         pendingPass)
#define SC_PASS_RECLAIM(chunksFreeBefore, chunksFreeAfter, passesReclaimed, chunksReleased, \
                        durationUs, pressure, transport, capturePins, editPins, slotsWithPins) \
  DebugSessionCapture::passReclaim(chunksFreeBefore, chunksFreeAfter, passesReclaimed, \
                                 chunksReleased, durationUs, pressure, transport, capturePins, \
                                 editPins, slotsWithPins)

#else  // !SESSION_CAPTURE — all capture macros compile to nothing

namespace DebugSessionCapture {
constexpr uint32_t kUnsampledHeapBytes = UINT32_MAX;
inline bool appendDiagTraceRecord(const void*, uint16_t) { return false; }
inline void emitDiagCheckpointLine(const Diagnostics::DiagTraceRecord&) {}
inline bool captureBootGraceActive() { return false; }
inline void restartCaptureBootGrace() {}
inline bool captureRingRange(uintptr_t&, size_t&) { return false; }
}  // namespace DebugSessionCapture

#define SC_SESSION_HEADER()                ((void)0)
#define SC_MIDI_IN(src, type, ch, d1, d2)  ((void)0)
#define SC_MIDI_OUT_EVENT(e)               ((void)0)
#define SC_LED_OUT(on, note, vel)          ((void)0)
#define SC_GESTURE(ch0, note, pressType)   ((void)0)
#define SC_STATE(component, from, to)      ((void)0)
#define SC_BPM(raw, smoothed)              ((void)0)
#define SC_CLOCK_SOURCE(from, to)          ((void)0)
#define SC_REC_START(slot, tick)           ((void)0)
#define SC_REC_STOP(kind, slot, tick, start, raw, final, align) ((void)0)
#define SC_REC_SLOT_IDENTITY(event, takeId, trackIndex, startSlot, stopSlot, commitSlot, persistTrack, \
                             persistSlot, reason, work, slotChanged) ((void)0)
#define SC_REC_STOP_STAGE(stage, elapsedUs, durationUs, heapBefore, heapAfter, eventCount, chunkRefCount, outcome) ((void)0)
#define SC_ODUB_STAGE(stage, durationUs, heapBefore, heapAfter, outcome) ((void)0)
#define SC_ODUB_WRAP_TICK(totalUs, sealUs, passApplyUs, srcAppendUs, commitUs, sealPendUs, \
                          beginUs) \
  ((void)0)
#define SC_PLAYBACK_ORDER(durationUs, eventCount) ((void)0)
#define SC_DROID_FEEDBACK_QUEUE(depth, oldestAgeUs, overflowCount) ((void)0)
#define SC_ODUB_STOP_STAGE(...) ((void)0)
#ifndef SC_ACCEPTED_LATCH
#define SC_ACCEPTED_LATCH(transition, track, slot, interval, destination, outcome) ((void)0)
#endif
#define SC_PERSIST(stage, durationUs, heapBefore, heapAfter, outcome) ((void)0)
#define SC_PERSIST_RESULT(durationUs, heapBefore, heapAfter, outcome) ((void)0)
#define SC_RESTORE_REQUEST(track, slot, requestGeneration, setEpoch, state, wait, terminal) ((void)0)
// `restored-library-stability` 1.3: no records without a capture build (the request trigger is
// session-capture-only too). `#ifndef`-guarded for a native suite that substitutes its own recorder.
#ifndef SC_LIBRARY_OBSERVATION_BEGIN
#define SC_LIBRARY_OBSERVATION_BEGIN(windowStartUs, setEpoch, slotCount) ((void)0)
#endif
#ifndef SC_LIBRARY_OBSERVATION_SLOT
#define SC_LIBRARY_OBSERVATION_SLOT(track, slot, state, cause, payload, offered, request, wait, \
                                    terminal, requestGeneration, setEpoch, hasCommittedContent, \
                                    committedPassCount, highestCommittedPassId, loopLengthTicks, \
                                    committedEventCount, requiredForInitialPlayback, \
                                    committedTerminalInWindow, invalidated) \
  ((void)0)
#endif
#ifndef SC_LIBRARY_OBSERVATION_END
#define SC_LIBRARY_OBSERVATION_END(windowStartUs, windowEndUs, turns, restarts, slotsObserved, \
                                   payloadInventory, initialPlaybackRequired,                  \
                                   observedRestoreCompletions, committedAtCheckpoint,          \
                                   admissionComplete, everyInitialPlaybackSlotObserved,        \
                                   consistentView, complete)                                   \
  ((void)0)
#endif
#define SC_PERSIST_BUNDLE(workType, totalUs, maxSliceUs, sliceCount, undoEntryCount, snapshotCount) ((void)0)
#define SC_PERSIST_DIAG(freeChunks, usedChunks, reserve, queueDepth, writingChunks, transportBlk, \
                        heapBlk, budgetBlk, sliceDone, peakLatUs, dirtyAgeMs, maxBacklog, pending, \
                        inProg, captureActive) \
  ((void)0)
#define SC_PERSIST_PRESSURE(freeChunks, reserve, usedChunks) ((void)0)
#define SC_PERSIST_BACKLOG(workQ, writingItems, chunkQ, dirtyAgeMs, estSteps, estBytes, pending, \
                           urgent, transportBlk, budgetBlk, heapBlk) \
  ((void)0)
#define SC_PERSIST_DRAIN_FAIL(reason, steps, stuckIter, workQ, chunkQ, estSteps, estBytes) ((void)0)
#define SC_SAVE(phase, rotateStep)         ((void)0)
#define SC_LOADSAVE(active)                ((void)0)
#define SC_OVERLAY_SEL(mode, row)          ((void)0)
#define SC_OVERLAY_CONFIRM(mode, row)      ((void)0)
#define SC_REC_STORED_NOTE_ON(tick, ch, note) ((void)0)
#define SC_STORED_NOTE_EVENT(kind, tick, ch, note) ((void)0)
#define SC_STORED_NOTES(track, slot, notes, uniqueNoteIds, maxSamePitch) ((void)0)
#define SC_OVERLAP_HOLD(noteOffs, emptySets, maxIds, overflows, lookedUp, maxExamined, \
                        sumExamined, maxLookupUs, sumLookupUs, add, shorten, hide) \
  ((void)0)
#define SC_CAPTURE_COORD(absTick, storageTick, projPhase, displayPhase, startLoopTick, \
                         projectionCycleStartTick, loopStartTick, ch, note) \
  ((void)0)
#define SC_DISP(slot, state, loopLen, take, visual, frame, buffer, hasCommittedPasses) ((void)0)
#define SC_DISP_WINDOW(slot, state, loopLen, take, visual, frame, buffer, hasCommittedPasses, wStart, wBars, \
                      wNotes) \
  ((void)0)
#define SC_VCACHE(phase, events, notes, firstBar, lastBar, totalBars, dirtyBarsSize, dirtyCount, \
                  dirtyFlag) \
  ((void)0)
#define SC_DNTE(pitch, storageStart, displayStart, length, selectedIdx) ((void)0)
#define SC_DFRAME(frameNotes, elapsedUs, frameIndex, resolveUs, drawUs, transferUs) ((void)0)
#define SC_PLAYBACK_FRAME(slot, currentTick, tickInLoop, prevTickInLoop, projStart, loopStart, \
                          loopLen, idxBefore, idxAfter, orderSize, sent, atLoopStart, wrap, stale, \
                          rev, gen) \
  ((void)0)
#define SC_STORED_WRAP_PAIR(onTick, offTick, ch, note) ((void)0)
#define SC_PENDING_SAME_PITCH_NOTE_ON_REJECT(pitch, ch, pendingStartTick, rejectedTick) ((void)0)
#define SC_CAPTURE_CLEANUP(phase, kind, count) ((void)0)
#define SC_REC_QUEUE_STORED_NOTE_ON(tick, ch, note) ((void)0)
#define SC_REC_FLUSH_PENDING_REVTS(maxLines) ((void)0)
#define SC_REC_FLUSH_ALL_PENDING_REVTS()     ((void)0)
#define SC_CAPTURE_FLUSH(maxRecords)         ((void)0)
#define SC_UPDATE(tick, ticksPerBar)         ((void)0)
#define SC_MEMORY_PRESSURE(transition, heapFree, chunksFree, queueDepth) ((void)0)
#define SC_CAPTURE_APPEND_DENY(reason, freeChunks, usedChunks, pressure, ch, note, tick, pendingPass) \
  ((void)0)
#define SC_PASS_RECLAIM(chunksFreeBefore, chunksFreeAfter, passesReclaimed, chunksReleased, \
                        durationUs, pressure, transport, capturePins, editPins, slotsWithPins) \
  ((void)0)

#endif  // SESSION_CAPTURE
