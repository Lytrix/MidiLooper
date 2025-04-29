// ClockManager.cpp
#include "ClockManager.h"
#include "Globals.h"
#include <IntervalTimer.h>
#include "TrackManager.h"

ClockManager clockManager;  // Global instance

IntervalTimer clockTimer;

ClockManager::ClockManager()
  : microsPerTick(0),
    currentTick(0),
    lastMidiClockTime(0),
    lastInternalTickTime(0),
    pendingStart(false),
    externalClockPresent(false)
{}


uint32_t ClockManager::getCurrentTick() {
  noInterrupts();
  uint32_t tick = currentTick;
  interrupts();
  return tick;
}


void ClockManager::setupClock() {
  microsPerTick = 60000000UL / (bpm * ticksPerQuarterNote);
  clockTimer.begin([] { clockManager.updateInternalClock(); }, microsPerTick);
}

void ClockManager::setBpm(uint16_t newBpm) {
  bpm = newBpm;
  microsPerTick = 60000000UL / (bpm * ticksPerQuarterNote);
  clockTimer.update(microsPerTick);
}

void ClockManager::setTicksPerQuarterNote(uint16_t newTicks) {
  ticksPerQuarterNote = newTicks;
  microsPerTick = 60000000UL / (bpm * ticksPerQuarterNote);
  clockTimer.update(microsPerTick);
}

// Called by hardware timer if externalClock is NOT present
void ClockManager::updateInternalClock() {
  if (!externalClockPresent) {
    currentTick++;
    updateAllTracks(currentTick);
    lastInternalTickTime = micros();
  }
}

// Called by MIDI clock tick (F8)
void ClockManager::onMidiClockPulse() {
  externalClockPresent = true;
  currentTick++;

  if (pendingStart) {
    // Quantize to bar boundary
    const uint32_t ticksPerBar = ticksPerQuarterNote * 4;
    if (currentTick % ticksPerBar == 0) {
      currentTick = 0;
      pendingStart = false;
    }
  }

  updateAllTracks(currentTick);
  lastMidiClockTime = micros();
}

// Check regularly (e.g., from loop()) to detect loss of MIDI clock
void ClockManager::checkClockSource() {
  // uint32_t now = micros();
  // if (externalClockPresent && (now - lastMidiClockTime > midiClockTimeout)) {
  //   externalClockPresent = false;
  //   Serial.println("MIDI clock lost, falling back to internal clock.");
  // }
}

void ClockManager::onMidiStart() {
  pendingStart = true;
  externalClockPresent = true;
  lastMidiClockTime = micros();
}

void ClockManager::onMidiStop() {
  // externalClockPresent = false;
}

// Common playback update for all tracks
void ClockManager::updateAllTracks(uint32_t tick) {
  for (int i = 0; i < trackManager.getTrackCount(); ++i) {
    Track& track = trackManager.getTrack(i);
    if (track.isPlaying() || track.isOverdubbing()) {
      track.playEvents(tick, true);
    }
  }
}
