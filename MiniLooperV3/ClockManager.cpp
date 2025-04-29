// ClockManager.cpp
#include "ClockManager.h"
#include "Globals.h" // only if you need bpm, ticksPerQuarterNote, etc.
#include <IntervalTimer.h>
#include "TrackManager.h"

ClockManager clockManager;  // Correct global instance

IntervalTimer clockTimer;

ClockManager::ClockManager()
  : microsPerTick(0),
    lastMidiClockTime(0),
    lastInternalTickTime(0),
    currentTick(0),
    externalClockPresent(true)
{
}


// Te most important TICK of all :)
uint32_t ClockManager::getCurrentTick() {
  return currentTick;
}

void ClockManager::setupClock() {
  microsPerTick = 60000000UL / (bpm * ticksPerQuarterNote);
  clockTimer.begin([] { clockManager.updateInternalClock(); }, microsPerTick);
}

void ClockManager::setBpm(uint16_t newBpm) {
    bpm = newBpm;
    microsPerTick = 60000000UL / (bpm * ticksPerQuarterNote);
    clockTimer.update(microsPerTick); // <- update timer immediately
}

void ClockManager::setTicksPerQuarterNote(uint16_t newTicks) {
    ticksPerQuarterNote = newTicks;
    microsPerTick = 60000000UL / (bpm * ticksPerQuarterNote);
    clockTimer.update(microsPerTick);
}


void ClockManager::updateInternalClock() {
  if (!externalClockPresent) {
    currentTick++;
    // Update all tracks playback
    for (int i = 0; i < trackManager.getTrackCount(); ++i) {
        Track& track = trackManager.getTrack(i);

        if (track.isPlaying() || track.isOverdubbing()) {   // <-- check BOTH playing and overdubbing
          track.playEvents(currentTick, true);
        }
    }
    lastInternalTickTime = micros();
  }
}

void ClockManager::onMidiClockPulse() {
  externalClockPresent = true;
  currentTick++;

  if (pendingStart) {
    // Quantize start to bar boundary
    const uint32_t ticksPerBar = ticksPerQuarterNote * 4; // Assuming 4/4 time
    if (currentTick % ticksPerBar == 0) {
      currentTick = 0;
      pendingStart = false;
      // Optionally: trigger playback start here if needed
    }
  }

  // Update all tracks playback
  for (int i = 0; i < trackManager.getTrackCount(); ++i) {
    Track& track = trackManager.getTrack(i);

    if (track.isPlaying() || track.isOverdubbing()) {   // <-- check BOTH playing and overdubbing
      track.playEvents(currentTick, true);
    }
  }
  lastMidiClockTime = micros();
}

void ClockManager::checkClockSource() {
  uint32_t now = micros();
  if (externalClockPresent && (now - lastMidiClockTime > midiClockTimeout)) {
    externalClockPresent = false; // fall back to internal clock
  }
}

void ClockManager::onMidiStart() {
  pendingStart = true;
  externalClockPresent = true;
  lastMidiClockTime = micros();
}

void ClockManager::onMidiStop() {
  externalClockPresent = false;
}
