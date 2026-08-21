//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file RuntimeTimingTelemetry.h
 * @brief S0 observation-only runtime timing telemetry.
 *
 * Measures the MIDI Input Gap between handleMidiInput() entries, the duration
 * of handleMidiInput(), Clock/track durations, USB/DIN drain segments, nested
 * dispatch sums, and (Stage 1) MIDI deadline lateness. Hot-path hooks only
 * accumulate integers. `#CAP` for lateness is a 5 s window plus rare one-shots
 * drained from the main loop — never a line per MIDI event. Does not admit work,
 * add handleMidiInput() call sites, or drive any scheduling decision.
 *
 * See docs/Plans/runtime_scheduling_admission_model_architecture.md.
 */
#pragma once

#include <cstdint>

#if defined(SESSION_CAPTURE) || defined(PIO_UNIT_TEST_NATIVE)
#define RUNTIME_TIMING_ENABLED 1
#else
#define RUNTIME_TIMING_ENABLED 0
#endif

namespace RuntimeTimingTelemetry {

/** Rate-limit DIAG emission to once per this many microseconds. */
constexpr uint32_t kEmitIntervalUs = 5000000u;

/**
 * Soft ceiling used only to count `overCount` samples in S0 telemetry.
 * Not a runtime-admission contract and not a MIDI Input Gap ceiling.
 */
constexpr uint32_t kObservationalSoftCeilingUs = 5000u;

struct Snapshot {
  uint32_t midiGapMaxUs = 0;
  uint32_t midiGapOverCount = 0;
  uint32_t midiInputMaxUs = 0;
  uint32_t midiInputOverCount = 0;
  uint32_t clkMaxUs = 0;
  uint32_t clkOverCount = 0;
  uint32_t tracksMaxUs = 0;
  uint32_t tracksOverCount = 0;
  uint32_t usbdevMaxUs = 0;
  uint32_t usbdevOverCount = 0;
  uint32_t dinMaxUs = 0;
  uint32_t dinOverCount = 0;
  uint32_t hosttaskMaxUs = 0;
  uint32_t hosttaskOverCount = 0;
  uint32_t hostdrainMaxUs = 0;
  uint32_t hostdrainOverCount = 0;
  uint32_t usbreadMaxUs = 0;
  uint32_t usbreadOverCount = 0;
  uint32_t usbdispMaxUs = 0;
  uint32_t usbdispOverCount = 0;
  uint32_t usbcapMaxUs = 0;
  uint32_t usbcapOverCount = 0;
  uint32_t usbthruMaxUs = 0;
  uint32_t usbthruOverCount = 0;
  uint32_t usbclkMaxUs = 0;
  uint32_t usbclkOverCount = 0;
  uint32_t usbnoteMaxUs = 0;
  uint32_t usbnoteOverCount = 0;
  uint32_t usbccMaxUs = 0;
  uint32_t usbccOverCount = 0;
  uint32_t usbtransMaxUs = 0;
  uint32_t usbtransOverCount = 0;
  uint32_t noteappendMaxUs = 0;
  uint32_t noteappendOverCount = 0;
  uint32_t notechgMaxUs = 0;
  uint32_t notechgOverCount = 0;
  uint32_t notereconMaxUs = 0;
  uint32_t notereconOverCount = 0;
  uint32_t notepairMaxUs = 0;
  uint32_t notepairOverCount = 0;
  uint32_t idleMaintMaxUs = 0;
  uint32_t idleMaintOverCount = 0;
  uint32_t loadFrameMaxUs = 0;
  uint32_t loadFrameOverCount = 0;
  uint32_t persistSaveMaxUs = 0;
  uint32_t persistSaveOverCount = 0;
  uint32_t clockPulses = 0;
  uint32_t windowElapsedUs = 0;
  uint32_t lateOnMaxUs = 0;
  uint32_t lateOnCount = 0;
  uint32_t lateOffMaxUs = 0;
  uint32_t lateOffCount = 0;
  uint32_t lateClkMaxUs = 0;
  uint32_t lateClkCount = 0;
  bool lateOneShotArmed = true;
  bool pendingLateEvent = false;
  bool pendingPlaybackRebuild = false;
};

void resetForTest();

/** Call on entry to MidiHandler::handleMidiInput — records MIDI Input Gap from prior exit. */
void noteMidiInputEnter(uint32_t nowUs);

/** Call on exit from MidiHandler::handleMidiInput — records handleMidiInput() duration. */
void noteMidiInputExit(uint32_t nowUs);

/** Record duration of ClockManager::onMidiClockPulse (includes nested updateAllTracks). */
void noteClockDispatch(uint32_t durationUs);

/** Record duration of TrackManager::updateAllTracks (accumulate only; ISR-safe). */
void noteTracksUpdate(uint32_t durationUs);

/** S0b: USB-device read loop + dispatchMidiBatch. */
void noteUsbDeviceDrain(uint32_t durationUs);

/** S0b: DIN (Serial8) read loop + dispatchMidiBatch. */
void noteDinDrain(uint32_t durationUs);

/** S0b: usbHost.Task() stack service. */
void noteUsbHostTask(uint32_t durationUs);

/** S0b: usbHostMIDI.read() drain (callbacks into handleMidiMessage). */
void noteUsbHostDrain(uint32_t durationUs);

/** S0c: usbMIDI.read() loop only (USB-device drain, before dispatch). */
void noteUsbDeviceRead(uint32_t durationUs);

/** S0c: dispatchMidiBatch for the USB-device batch. */
void noteUsbDeviceDispatch(uint32_t durationUs);

/** S0c: start summing nested USB-device dispatch work. */
void beginUsbDeviceNested();

/** S0c: add one SOURCE_USB SC_MIDI_IN duration into the current dispatch sum. */
void addUsbDeviceCapture(uint32_t durationUs);

/** S0c: add one SOURCE_USB sendMidiThru duration into the current dispatch sum. */
void addUsbDeviceThru(uint32_t durationUs);

/** S0d: add one SOURCE_USB Clock / onMidiClockPulse duration into the current dispatch sum. */
void addUsbDeviceClock(uint32_t durationUs);

/** S0d: add one SOURCE_USB NoteOn/NoteOff handler duration into the current dispatch sum. */
void addUsbDeviceNote(uint32_t durationUs);

/** S0d: add one SOURCE_USB CC/pitch/AT/PC handler duration into the current dispatch sum. */
void addUsbDeviceCc(uint32_t durationUs);

/** S0d: add one SOURCE_USB Start/Stop/Continue handler duration into the current dispatch sum. */
void addUsbDeviceTransport(uint32_t durationUs);

/** S0e: add one appendCaptureEventWithResult duration into the current USB dispatch sum. */
void addNoteAppend(uint32_t durationUs);

/** S0e: add one accumulatePendingNoteChangesForIncomingNote duration into the current USB dispatch sum. */
void addNoteChange(uint32_t durationUs);

/** S0e: add one reconstructDisplayNotes duration inside accumulatePendingNoteChangesForIncomingNote. */
void addNoteRecon(uint32_t durationUs);

/** S0e: add one candidate-pair scan duration inside accumulatePendingNoteChangesForIncomingNote. */
void addNotePair(uint32_t durationUs);

/** S0c/S0d/S0e: record the nested sums as one sample each and reset them. */
void commitUsbDeviceNested();

/** Count one external MIDI Clock pulse for clockrate. */
void noteClockPulse();

/**
 * Mark the start of one playback service interval (internal tick ISR or external
 * clock pulse). ISR-safe ITCM integer stores — not FLASHMEM. `tickPeriodUs` is one
 * internal tick or one MIDI-clock period (`TICKS_PER_CLOCK` ticks).
 */
void notePlaybackServiceEnter(uint32_t nowUs, uint32_t tickPeriodUs);

/** Clear due-time cadence on transport stop/start so the next pulse is not scored late. */
void resetPlaybackDeadlineCadence();

/**
 * Note-on/off send lateness. ISR-safe ITCM stores. On time iff sent before the next
 * playback tick is due (`now <= serviceDue + period`). Equality and early are 0.
 */
void recordNoteSendLateness(bool isNoteOn, uint32_t nowUs, uint32_t tick);

/**
 * Outgoing MIDI clock send lateness (internal master `sendClock`). ISR-safe ITCM
 * stores. Cadence is `clockPeriodUs`. On time iff sent before `clockDue + onTimeWindowUs`
 * (one internal tick). Equality and early are 0.
 */
void recordOutgoingClockSend(uint32_t nowUs, uint32_t clockPeriodUs, uint32_t onTimeWindowUs,
                             uint32_t tick);

/**
 * Queue a playback-gather rebuild one-shot. ISR-safe integer stores; emit from
 * `emitPendingDeadlineOneShots` on the main loop.
 */
void recordPlaybackRebuild(uint32_t durationUs, uint32_t windowStartTick,
                           uint32_t windowLengthTicks, uint32_t revision);

/**
 * Main-loop only: emit pending first-late / rebuild `#CAP` lines (SESSION_CAPTURE).
 * Does not emit per-event traffic. `maybeEmit` calls this first so the main loop
 * does not need a second hot-path call.
 */
void emitPendingDeadlineOneShots();

/** Post-BAR remainder: Track::processDeferredIdleMaintenance across all tracks. */
void noteIdleMaint(uint32_t durationUs);

/** Post-BAR remainder: runDeferredLoadAndDisplayFrame (LoadLoopJob + OLED). */
void noteLoadFrame(uint32_t durationUs);

/** Post-BAR remainder: StorageManager::processDeferredSaveState. */
void notePersistSave(uint32_t durationUs);

/** Emit a one-shot remainder line when a span exceeds this duration. */
constexpr uint32_t kLoopRemainderOneShotUs = 50000u;

/** SESSION_CAPTURE: emit loop_rem when measure is set and duration >= kLoopRemainderOneShotUs. */
void recordLoopRemainderIfMeasuring(bool measure, const char* span, uint32_t startUs);

/**
 * SESSION_CAPTURE: time one updateLeds helper.
 * helper 0=lookup, 1=analyze, 2=bars, 3=gather.
 * Implementation is FLASHMEM + noinline so span strings stay out of ITCM/DTCM.
 */
void recordMidiLedHelperRem(bool measure, uint8_t helper, uint32_t startUs);

/**
 * SESSION_CAPTURE: time one load_frame child.
 * child 0=display_frame, 1=load_job, 2=first_commit, 3=boot_commit.
 * FLASHMEM + PSTR so span strings stay out of DTCM.
 */
void recordLoadFrameChildRem(uint8_t child, uint32_t startUs);

/**
 * SESSION_CAPTURE: time one idle visual-cache slice child.
 * child 0=idle_gather, 1=idle_reconstruct, 2=idle_append.
 * FLASHMEM + PSTR so span strings stay out of DTCM.
 */
void recordIdleMaintChildRem(uint8_t child, uint32_t startUs);

/**
 * Emit Tier-A DIAG lines if the emit interval has elapsed.
 * Returns true when a window was emitted and counters reset.
 * Safe to call from main loop only (not from ISR). Drains pending
 * deadline one-shots before the 5 s window check.
 */
bool maybeEmit(uint32_t nowUs);

Snapshot peek(uint32_t nowUs = 0);

}  // namespace RuntimeTimingTelemetry

#if RUNTIME_TIMING_ENABLED
#define RUNTIME_TIMING_NOTE_MIDI_INPUT_ENTER(nowUs) RuntimeTimingTelemetry::noteMidiInputEnter(nowUs)
#define RUNTIME_TIMING_NOTE_MIDI_INPUT_EXIT(nowUs) RuntimeTimingTelemetry::noteMidiInputExit(nowUs)
#define RUNTIME_TIMING_NOTE_CLOCK_DISPATCH(durationUs) RuntimeTimingTelemetry::noteClockDispatch(durationUs)
#define RUNTIME_TIMING_NOTE_TRACKS_UPDATE(durationUs) RuntimeTimingTelemetry::noteTracksUpdate(durationUs)
#define RUNTIME_TIMING_NOTE_USB_DEVICE_DRAIN(durationUs) RuntimeTimingTelemetry::noteUsbDeviceDrain(durationUs)
#define RUNTIME_TIMING_NOTE_DIN_DRAIN(durationUs) RuntimeTimingTelemetry::noteDinDrain(durationUs)
#define RUNTIME_TIMING_NOTE_USB_HOST_TASK(durationUs) RuntimeTimingTelemetry::noteUsbHostTask(durationUs)
#define RUNTIME_TIMING_NOTE_USB_HOST_DRAIN(durationUs) RuntimeTimingTelemetry::noteUsbHostDrain(durationUs)
#define RUNTIME_TIMING_NOTE_USB_DEVICE_READ(durationUs) RuntimeTimingTelemetry::noteUsbDeviceRead(durationUs)
#define RUNTIME_TIMING_NOTE_USB_DEVICE_DISPATCH(durationUs) RuntimeTimingTelemetry::noteUsbDeviceDispatch(durationUs)
#define RUNTIME_TIMING_BEGIN_USB_DEVICE_NESTED() RuntimeTimingTelemetry::beginUsbDeviceNested()
#define RUNTIME_TIMING_ADD_USB_DEVICE_CAPTURE(durationUs) RuntimeTimingTelemetry::addUsbDeviceCapture(durationUs)
#define RUNTIME_TIMING_ADD_USB_DEVICE_THRU(durationUs) RuntimeTimingTelemetry::addUsbDeviceThru(durationUs)
#define RUNTIME_TIMING_ADD_USB_DEVICE_CLOCK(durationUs) RuntimeTimingTelemetry::addUsbDeviceClock(durationUs)
#define RUNTIME_TIMING_ADD_USB_DEVICE_NOTE(durationUs) RuntimeTimingTelemetry::addUsbDeviceNote(durationUs)
#define RUNTIME_TIMING_ADD_USB_DEVICE_CC(durationUs) RuntimeTimingTelemetry::addUsbDeviceCc(durationUs)
#define RUNTIME_TIMING_ADD_USB_DEVICE_TRANSPORT(durationUs) RuntimeTimingTelemetry::addUsbDeviceTransport(durationUs)
#define RUNTIME_TIMING_ADD_NOTE_APPEND(durationUs) RuntimeTimingTelemetry::addNoteAppend(durationUs)
#define RUNTIME_TIMING_ADD_NOTE_CHANGE(durationUs) RuntimeTimingTelemetry::addNoteChange(durationUs)
#define RUNTIME_TIMING_ADD_NOTE_RECON(durationUs) RuntimeTimingTelemetry::addNoteRecon(durationUs)
#define RUNTIME_TIMING_ADD_NOTE_PAIR(durationUs) RuntimeTimingTelemetry::addNotePair(durationUs)
#define RUNTIME_TIMING_COMMIT_USB_DEVICE_NESTED() RuntimeTimingTelemetry::commitUsbDeviceNested()
#define RUNTIME_TIMING_NOTE_CLOCK_PULSE() RuntimeTimingTelemetry::noteClockPulse()
#define RUNTIME_TIMING_NOTE_PLAYBACK_SERVICE_ENTER(nowUs, tickPeriodUs) \
  RuntimeTimingTelemetry::notePlaybackServiceEnter(nowUs, tickPeriodUs)
#define RUNTIME_TIMING_RESET_PLAYBACK_DEADLINE_CADENCE() \
  RuntimeTimingTelemetry::resetPlaybackDeadlineCadence()
#define RUNTIME_TIMING_RECORD_NOTE_SEND_LATENESS(isNoteOn, nowUs, tick) \
  RuntimeTimingTelemetry::recordNoteSendLateness(isNoteOn, nowUs, tick)
#define RUNTIME_TIMING_RECORD_OUTGOING_CLOCK_SEND(nowUs, clockPeriodUs, onTimeWindowUs, tick) \
  RuntimeTimingTelemetry::recordOutgoingClockSend(nowUs, clockPeriodUs, onTimeWindowUs, tick)
#define RUNTIME_TIMING_RECORD_PLAYBACK_REBUILD(durationUs, windowStartTick, windowLengthTicks, revision) \
  RuntimeTimingTelemetry::recordPlaybackRebuild(durationUs, windowStartTick, windowLengthTicks, revision)
#define RUNTIME_TIMING_RECORD_LOOP_REMAINDER_IF_MEASURING(measure, span, startUs) \
  RuntimeTimingTelemetry::recordLoopRemainderIfMeasuring(measure, span, startUs)
#define RUNTIME_TIMING_RECORD_MIDI_LED_HELPER_REM(measure, helper, startUs) \
  RuntimeTimingTelemetry::recordMidiLedHelperRem(measure, helper, startUs)
#define RUNTIME_TIMING_RECORD_LOAD_FRAME_CHILD_REM(child, startUs) \
  RuntimeTimingTelemetry::recordLoadFrameChildRem(child, startUs)
#define RUNTIME_TIMING_RECORD_IDLE_MAINT_CHILD_REM(child, startUs) \
  RuntimeTimingTelemetry::recordIdleMaintChildRem(child, startUs)
#define RUNTIME_TIMING_NOTE_IDLE_MAINT(durationUs) RuntimeTimingTelemetry::noteIdleMaint(durationUs)
#define RUNTIME_TIMING_NOTE_LOAD_FRAME(durationUs) RuntimeTimingTelemetry::noteLoadFrame(durationUs)
#define RUNTIME_TIMING_NOTE_PERSIST_SAVE(durationUs) RuntimeTimingTelemetry::notePersistSave(durationUs)
#define RUNTIME_TIMING_MAYBE_EMIT(nowUs) RuntimeTimingTelemetry::maybeEmit(nowUs)
#else
#define RUNTIME_TIMING_NOTE_MIDI_INPUT_ENTER(nowUs) ((void)0)
#define RUNTIME_TIMING_NOTE_MIDI_INPUT_EXIT(nowUs) ((void)0)
#define RUNTIME_TIMING_NOTE_CLOCK_DISPATCH(durationUs) ((void)0)
#define RUNTIME_TIMING_NOTE_TRACKS_UPDATE(durationUs) ((void)0)
#define RUNTIME_TIMING_NOTE_USB_DEVICE_DRAIN(durationUs) ((void)0)
#define RUNTIME_TIMING_NOTE_DIN_DRAIN(durationUs) ((void)0)
#define RUNTIME_TIMING_NOTE_USB_HOST_TASK(durationUs) ((void)0)
#define RUNTIME_TIMING_NOTE_USB_HOST_DRAIN(durationUs) ((void)0)
#define RUNTIME_TIMING_NOTE_USB_DEVICE_READ(durationUs) ((void)0)
#define RUNTIME_TIMING_NOTE_USB_DEVICE_DISPATCH(durationUs) ((void)0)
#define RUNTIME_TIMING_BEGIN_USB_DEVICE_NESTED() ((void)0)
#define RUNTIME_TIMING_ADD_USB_DEVICE_CAPTURE(durationUs) ((void)0)
#define RUNTIME_TIMING_ADD_USB_DEVICE_THRU(durationUs) ((void)0)
#define RUNTIME_TIMING_ADD_USB_DEVICE_CLOCK(durationUs) ((void)0)
#define RUNTIME_TIMING_ADD_USB_DEVICE_NOTE(durationUs) ((void)0)
#define RUNTIME_TIMING_ADD_USB_DEVICE_CC(durationUs) ((void)0)
#define RUNTIME_TIMING_ADD_USB_DEVICE_TRANSPORT(durationUs) ((void)0)
#define RUNTIME_TIMING_ADD_NOTE_APPEND(durationUs) ((void)0)
#define RUNTIME_TIMING_ADD_NOTE_CHANGE(durationUs) ((void)0)
#define RUNTIME_TIMING_ADD_NOTE_RECON(durationUs) ((void)0)
#define RUNTIME_TIMING_ADD_NOTE_PAIR(durationUs) ((void)0)
#define RUNTIME_TIMING_COMMIT_USB_DEVICE_NESTED() ((void)0)
#define RUNTIME_TIMING_NOTE_CLOCK_PULSE() ((void)0)
#define RUNTIME_TIMING_NOTE_PLAYBACK_SERVICE_ENTER(nowUs, tickPeriodUs) ((void)0)
#define RUNTIME_TIMING_RESET_PLAYBACK_DEADLINE_CADENCE() ((void)0)
#define RUNTIME_TIMING_RECORD_NOTE_SEND_LATENESS(isNoteOn, nowUs, tick) ((void)0)
#define RUNTIME_TIMING_RECORD_OUTGOING_CLOCK_SEND(nowUs, clockPeriodUs, onTimeWindowUs, tick) ((void)0)
#define RUNTIME_TIMING_RECORD_PLAYBACK_REBUILD(durationUs, windowStartTick, windowLengthTicks, revision) ((void)0)
#define RUNTIME_TIMING_RECORD_LOOP_REMAINDER_IF_MEASURING(measure, span, startUs) ((void)0)
#define RUNTIME_TIMING_RECORD_MIDI_LED_HELPER_REM(measure, helper, startUs) ((void)0)
#define RUNTIME_TIMING_RECORD_LOAD_FRAME_CHILD_REM(child, startUs) ((void)0)
#define RUNTIME_TIMING_RECORD_IDLE_MAINT_CHILD_REM(child, startUs) ((void)0)
#define RUNTIME_TIMING_NOTE_IDLE_MAINT(durationUs) ((void)0)
#define RUNTIME_TIMING_NOTE_LOAD_FRAME(durationUs) ((void)0)
#define RUNTIME_TIMING_NOTE_PERSIST_SAVE(durationUs) ((void)0)
#define RUNTIME_TIMING_MAYBE_EMIT(nowUs) ((void)0)
#endif
