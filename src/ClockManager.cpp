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
  trackManager.updateAllTracks(currentTick);  // Let TrackManager handle it
  lastInternalTickTime = micros();
}

void ClockManager::onMidiClockPulse() {
  if (!sequencerRunning) return;

  requestTransitionTo(CLOCK_EXTERNAL);

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
        if (bpmSmoothed > 0.0f && fabsf(computedBpm - bpmSmoothed) < 3.0f) {
          computedBpm = 0.02f * computedBpm + 0.98f * bpmSmoothed;
        }
        bpmSmoothed = computedBpm;
        setBpmFloat(computedBpm);
      }
    }
  }
  pulseHead = (pulseHead + 1) % PULSE_BUF_SIZE;

  // Midish pattern: advance at the START of each pulse, but skip on the
  // first pulse after Start (currentTick is already 0 from onMidiStart).
  // This keeps currentTick at the correct musical position between pulses,
  // so notes arriving after a clock read the right value from getCurrentTick().
  if (firstPulseAfterStart) {
    firstPulseAfterStart = false;
  } else {
    currentTick += Config::TICKS_PER_CLOCK;
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
  pulseFillCount = 0;
  pulseHead = 0;
  lastMidiClockTime = micros();
  currentTick = 0;
  firstPulseAfterStart = true;

  // Start all stopped tracks (same as toggleTransport when starting)
  for (uint8_t i = 0; i < Config::NUM_TRACKS; i++) {
    Track& t = trackManager.getTrack(i);
    if (t.isStopped()) {
      t.startPlaying(currentTick);
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

void ClockManager::resetToLoopStart() {
  noInterrupts();
  currentTick = 0;
  interrupts();
  trackManager.updateAllTracks(0);
  logger.info("Reset to loop start (tick 0)");
}

void ClockManager::toggleTransport() {
  if (sequencerRunning) {
    sequencerRunning = false;
    // Stop all active tracks (stopPlaying already sends All Notes Off per track)
    for (uint8_t i = 0; i < Config::NUM_TRACKS; i++) {
      Track& t = trackManager.getTrack(i);
      if (t.isPlaying() || t.isOverdubbing()) {
        t.stopPlaying();
      }
    }
    StorageManager::saveState(looperState.getLooperState());
    logger.info("Transport stopped");
  } else {
    sequencerRunning = true;
    // Resume playback for all tracks that have data
    for (uint8_t i = 0; i < Config::NUM_TRACKS; i++) {
      Track& t = trackManager.getTrack(i);
      if (t.isStopped()) {
        t.startPlaying(currentTick);
      }
    }
    logger.info("Transport started");
  }
}