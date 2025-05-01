#include "ClockManager.h"
#include "Globals.h"
#include <IntervalTimer.h>
#include "TrackManager.h"
#include "Logger.h"

ClockManager clockManager;  // Global instance
IntervalTimer clockTimer;

ClockManager::ClockManager()
  : pendingStart(false),
    microsPerTick(0),
    currentTick(0),
    lastMidiClockTime(0),
    lastInternalTickTime(0),
    externalClockPresent(false)
    
{}

uint32_t ClockManager::getCurrentTick() const {
  noInterrupts();
  uint32_t tick = currentTick;
  interrupts();
  return tick;
}

bool ClockManager::isExternalClockPresent() const {
  return externalClockPresent;
}

void ClockManager::setExternalClockPresent(bool present) {
  externalClockPresent = present;
}

void ClockManager::setup() {
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

void ClockManager::updateInternalClock() {
  if (!externalClockPresent) {
    currentTick++;
    updateAllTracks(currentTick);
    lastInternalTickTime = micros();
  }
}

void ClockManager::onMidiClockPulse() {
  externalClockPresent = true;
  currentTick++;

  if (pendingStart) {
    const uint32_t ticksPerBar = ticksPerQuarterNote * 4;
    if (currentTick % ticksPerBar == 0) {
      currentTick = 0;
      pendingStart = false;
    }
  }

  updateAllTracks(currentTick);
  lastMidiClockTime = setLastMidiClockTime(micros());
}

uint32_t ClockManager::setLastMidiClockTime(uint32_t lastMidiClockTime){
  return lastMidiClockTime;
}

void ClockManager::checkClockSource() {
  // Future enhancement: detect clock loss
}

void ClockManager::onMidiStart() {
  pendingStart = true;
  externalClockPresent = true;
  lastMidiClockTime = micros();
}

void ClockManager::onMidiStop() {
  // externalClockPresent = false;
}

void ClockManager::updateAllTracks(uint32_t tick) {
  for (int i = 0; i < trackManager.getTrackCount(); ++i) {
    Track& track = trackManager.getTrack(i);
    if (track.isPlaying() || track.isOverdubbing()) {
      track.playMidiEvents(tick, true);
    }
  }
}

void ClockManager::handleMidiClock() {
  if (!externalClockPresent) {
    externalClockPresent = true;
    logger.info("External MIDI clock detected");
  }
  
  // Update internal clock based on MIDI clock
  currentTick += Config::TICKS_PER_CLOCK;
}
