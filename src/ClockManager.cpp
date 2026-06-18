//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "ClockManager.h"
#include "ClockSourceStateMachine.h"
#include "Globals.h"
#include "LooperState.h"
#include "StorageManager.h"
#include <IntervalTimer.h>
#include "TrackManager.h"
#include "Logger.h"
#include "MidiHandler.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/ClockTransportUtils.h"

ClockManager clockManager;  // Global instance initiated
IntervalTimer clockTimer;

bool sequencerRunning = false;

ClockManager::ClockManager()
  : pendingStart(false),
    microsPerTick(0),
    currentTick(0),
    lastMidiClockTime(0),
    lastInternalTickTime(0),
    firstPulseAfterStart(false),
    clockSource(CLOCK_INTERNAL),
    pendingClockSource(CLOCK_INTERNAL),
    transitionPending(false),
    pulseHead(0),
    pulseFillCount(0),
    bpmSmoothed(0.0f)
{
  memset(pulseTimestamps, 0, sizeof(pulseTimestamps));
}


uint32_t ClockManager::getCurrentTick() const {
  noInterrupts();
  uint32_t tick = currentTick;
  interrupts();
  return tick;
}

bool ClockManager::isExternalClockPresent() const {
  return clockSource == CLOCK_EXTERNAL;
}

ClockSource ClockManager::getClockSource() const {
  return clockSource;
}

void ClockManager::setup() {
  microsPerTick = 60000000UL / (bpm * Config::INTERNAL_PPQN);
  clockTimer.begin([] { clockManager.updateInternalClock(); }, microsPerTick);
}

void ClockManager::setBpm(uint16_t newBpm) {
  bpm = (float)newBpm;
  microsPerTick = 60000000UL / (bpm * Config::INTERNAL_PPQN);
  clockTimer.update(microsPerTick);
}

void ClockManager::setBpmFloat(float newBpm) {
  if (newBpm < 20.0f) newBpm = 20.0f;
  if (newBpm > 300.0f) newBpm = 300.0f;
  bpm = newBpm;
  microsPerTick = 60000000UL / (bpm * Config::INTERNAL_PPQN);
  clockTimer.update(microsPerTick);
}

void ClockManager::setTicksPerQuarterNote(uint16_t newTicks) {
  ticksPerQuarterNote = newTicks;
  microsPerTick = 60000000UL / (bpm * Config::INTERNAL_PPQN);
  clockTimer.update(microsPerTick);
}

void ClockManager::requestTransitionTo(ClockSource target) {
  if (target != clockSource) {
    pendingClockSource = target;
    transitionPending = true;
  }
}

void ClockManager::actuallyTransition(ClockSource from, ClockSource to) {
  SC_CLOCK_SOURCE(from == CLOCK_EXTERNAL ? "EXT" : "INT",
                  to == CLOCK_EXTERNAL ? "EXT" : "INT");
  clockSource = to;
  pendingClockSource = to;
  transitionPending = false;

  if (from == CLOCK_EXTERNAL && to == CLOCK_INTERNAL) {
    pulseFillCount = 0;
    pulseHead = 0;
    bpmSmoothed = 0.0f;
    StorageManager::saveState(looperState.getLooperState());
    logger.info("MIDI clock lost, switching to internal at %.1f BPM", (double)bpm);
  } else if (from == CLOCK_INTERNAL && to == CLOCK_EXTERNAL) {
    logger.info("External MIDI clock detected");
  }
}


void ClockManager::updateInternalClock() {
  if (!sequencerRunning) return;
  // When slaved to external MIDI clock, tick is driven only by onMidiClockPulse
  if (clockSource == CLOCK_EXTERNAL) return;
  currentTick++;
  // Output MIDI clock when we are master (every 8 internal ticks = 24 PPQN)
  if (currentTick % Config::TICKS_PER_CLOCK == 0) {
    midiHandler.sendClock();
  }
  trackManager.advanceJamTicks(1);
  trackManager.updateAllTracks(currentTick);
  lastInternalTickTime = micros();
}

void ClockManager::onMidiClockPulse() {
  if (!sequencerRunning) return;

  requestTransitionTo(CLOCK_EXTERNAL);
  if (transitionPending &&
      ClockSourceStateMachine::isValidTransition(clockSource, CLOCK_EXTERNAL)) {
    actuallyTransition(clockSource, CLOCK_EXTERNAL);
  }

  // Sliding window BPM: ring buffer of 25 timestamps = 24 intervals (one quarter note)
  // Formula: 60e6 / elapsed_us = BPM (elapsed = micros for 24 MIDI clock pulses)
  pulseTimestamps[pulseHead] = micros();
  if (pulseFillCount < PULSE_BUF_SIZE) pulseFillCount++;

  if (pulseFillCount >= PULSE_BUF_SIZE) {
    uint8_t tailIdx = (pulseHead + 1) % PULSE_BUF_SIZE;
    uint32_t elapsed = pulseTimestamps[pulseHead] - pulseTimestamps[tailIdx];
    if (elapsed > 0) {
      float computedBpm = 60000000.0f / (float)elapsed;
      if (computedBpm >= 20.0f && computedBpm <= 300.0f) {
        [[maybe_unused]] const float windowBpm = computedBpm;
        if (bpmSmoothed > 0.0f && fabsf(computedBpm - bpmSmoothed) < 3.0f) {
          computedBpm = 0.02f * computedBpm + 0.98f * bpmSmoothed;
        }
        bpmSmoothed = computedBpm;
        setBpmFloat(computedBpm);
        SC_BPM(windowBpm, bpmSmoothed);
      }
    }
  }
  pulseHead = (pulseHead + 1) % PULSE_BUF_SIZE;

  // Advance before updateAllTracks. Skip the first advance after MIDI Start so
  // channel messages that follow the downbeat Clock still read tick 0 (not +8).
  if (firstPulseAfterStart) {
    firstPulseAfterStart = false;
  } else {
    currentTick += Config::TICKS_PER_CLOCK;
    trackManager.advanceJamTicks(Config::TICKS_PER_CLOCK);
  }
  trackManager.updateAllTracks(currentTick);
  lastMidiClockTime = micros();
}

uint32_t ClockManager::setLastMidiClockTime(uint32_t lastMidiClockTime){
  return lastMidiClockTime;
}

void ClockManager::checkClockSource() {
  if (clockSource == CLOCK_EXTERNAL && (micros() - lastMidiClockTime) > midiClockTimeout) {
    requestTransitionTo(CLOCK_INTERNAL);
  }

  if (transitionPending) {
    if (ClockSourceStateMachine::isValidTransition(clockSource, pendingClockSource)) {
      actuallyTransition(clockSource, pendingClockSource);
    } else {
      transitionPending = false;
    }
  }
}

void ClockManager::onMidiStart() {
  sequencerRunning = true;
  pendingStart = false;
  requestTransitionTo(CLOCK_EXTERNAL);
  // Slave immediately: while clockSource stays INTERNAL, updateInternalClock()
  // keeps advancing currentTick at 192 PPQN until checkClockSource() runs.
  // That drifted ~96 ticks (half a beat) ahead of the external sequencer
  // before the first Clock pulse, clipping the downbeat and shifting notes.
  if (transitionPending &&
      ClockSourceStateMachine::isValidTransition(clockSource, CLOCK_EXTERNAL)) {
    actuallyTransition(clockSource, CLOCK_EXTERNAL);
  }
  pulseFillCount = 0;
  pulseHead = 0;
  lastMidiClockTime = micros();
  currentTick = 0;
  firstPulseAfterStart = true;

  // Do not auto-play other loops while a capture is active or armed — playback
  // bleeds back through MIDI thru and gets recorded as a spurious downbeat note.
  if (!trackManager.hasActiveOrPendingCapture()) {
    for (uint8_t i = 0; i < Config::NUM_TRACKS; i++) {
      Track& t = trackManager.getTrack(i);
      if (t.isStopped()) {
        t.startPlaying(currentTick);
      }
    }
  }

  trackManager.updateAllTracks(currentTick);
}

void ClockManager::onMidiStop() {
  sequencerRunning = false;
  firstPulseAfterStart = false;
}

void ClockManager::handleMidiClock() {
  requestTransitionTo(CLOCK_EXTERNAL);
  currentTick += Config::TICKS_PER_CLOCK;
}

bool ClockManager::isClockRunning() const {
  return sequencerRunning;
}

bool ClockManager::isTransportRunning() const {
  return sequencerRunning;
}

bool ClockManager::shouldQuantizeRecordStart() const {
  return isClockRunning();
}

void ClockManager::resetToLoopStart() {
  noInterrupts();
  currentTick = 0;
  interrupts();
  trackManager.updateAllTracks(0);
  logger.info("Reset to loop start (tick 0)");
}

void ClockManager::setCurrentTick(uint32_t tick) {
  noInterrupts();
  currentTick = tick;
  interrupts();
  trackManager.updateAllTracks(tick);
  logger.log(CAT_CLOCK, LOG_DEBUG, "Seek: currentTick set to %lu", tick);
}

void ClockManager::toggleTransport() {
  const uint32_t now = micros();
  const bool emitTransport =
      ClockTransportUtils::shouldEmitMidiTransportAsMaster(
          static_cast<int>(clockSource), lastMidiClockTime, now, midiClockTimeout);

  if (sequencerRunning) {
    sequencerRunning = false;
    if (emitTransport) {
      if (clockSource == CLOCK_EXTERNAL) {
        actuallyTransition(CLOCK_EXTERNAL, CLOCK_INTERNAL);
      }
      midiHandler.sendStop();
    }
    trackManager.handleTransportStop();
    logger.info("Transport stopped");
  } else {
    sequencerRunning = true;
    if (emitTransport) {
      if (clockSource == CLOCK_EXTERNAL) {
        actuallyTransition(CLOCK_EXTERNAL, CLOCK_INTERNAL);
      }
      currentTick = 0;  // Align with Start — downbeat at tick 0
      midiHandler.sendStart();
      midiHandler.sendClock();  // First clock after Start is the downbeat (per MIDI spec)
      trackManager.updateAllTracks(0);  // Armed record + pending actions at downbeat
    }
    if (!trackManager.hasActiveOrPendingCapture()) {
      for (uint8_t i = 0; i < Config::NUM_TRACKS; i++) {
        Track& t = trackManager.getTrack(i);
        if (t.isStopped()) {
          t.startPlaying(currentTick);
        }
      }
    }
    logger.info("Transport started");
  }
}