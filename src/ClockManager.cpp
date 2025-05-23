#include "ClockManager.h"
#include "Globals.h"
#include <IntervalTimer.h>
#include "TrackManager.h"
#include "Logger.h"

ClockManager clockManager;  // Global instance initiated
IntervalTimer clockTimer;

bool sequencerRunning = true;

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
  microsPerTick = 60000000UL / (bpm * MidiConfig::PPQN);
  clockTimer.begin([] { clockManager.updateInternalClock(); }, microsPerTick);
}

void ClockManager::setBpm(uint16_t newBpm) {
  bpm = newBpm;
  microsPerTick = 60000000UL / (bpm * MidiConfig::PPQN);
  clockTimer.update(microsPerTick);
}

void ClockManager::setTicksPerQuarterNote(uint16_t newTicks) {
  ticksPerQuarterNote = newTicks;
  microsPerTick = 60000000UL / (bpm * MidiConfig::PPQN);
  clockTimer.update(microsPerTick);
}


void ClockManager::updateInternalClock() {
  if (!sequencerRunning) return;
  currentTick++;
  trackManager.updateAllTracks(currentTick);  // Let TrackManager handle it
  lastInternalTickTime = micros();
}

void ClockManager::onMidiClockPulse() {
  if (!sequencerRunning) return;
  externalClockPresent = true;
  //Avoid snapping if currentTick is 0.
  if (currentTick != 0) {
    uint32_t expectedTick = ((currentTick / 8) + 1) * 8;
    if (currentTick != expectedTick) {
        currentTick = expectedTick; // Resync to MIDI clock
    }
  }
  
  // if (pendingStart) {
  //   // const uint32_t ticksPerBar = MidiConfig::PPQN * 4;
  //   // if (currentTick % ticksPerBar == 0) {
  //     currentTick = 0;
  //     pendingStart = false;
  //   // }
  // }

  trackManager.updateAllTracks(currentTick);
  lastMidiClockTime = setLastMidiClockTime(micros());
}

uint32_t ClockManager::setLastMidiClockTime(uint32_t lastMidiClockTime){
  return lastMidiClockTime;
}

void ClockManager::checkClockSource() {
  // Future enhancement: detect clock loss
}

void ClockManager::onMidiStart() {
  sequencerRunning = true;
  pendingStart = false;
  externalClockPresent = true;
  lastMidiClockTime = micros();
  currentTick = 0;
  trackManager.updateAllTracks(currentTick);
}

void ClockManager::onMidiStop() {
  sequencerRunning = false;
}

void ClockManager::handleMidiClock() {
  if (!externalClockPresent) {
    externalClockPresent = true;
    logger.info("External MIDI clock detected");
  }
  
  // Update internal clock based on MIDI clock
  currentTick += Config::TICKS_PER_CLOCK;
}

// Returns true if either the internal or external clock is running
bool ClockManager::isClockRunning() const {
    return sequencerRunning || externalClockPresent;
}