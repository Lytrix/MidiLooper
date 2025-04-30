#include "ClockManager.h"
#include "Globals.h"
#include <IntervalTimer.h>
#include "TrackManager.h"

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

void ClockManager::setLastMidiClockTime(uint32_t time) {
  lastMidiClockTime = time;
}

void ClockManager::onMidiClockTick() {
  currentTick += TICKS_PER_CLOCK;
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
  lastMidiClockTime = micros();
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
      track.playEvents(tick, true);
    }
  }
}
