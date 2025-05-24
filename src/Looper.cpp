#include "Looper.h"
#include "StorageManager.h"
#include <SD.h>
#include <Arduino.h>

Looper looper;  // Global instance

Looper::Looper()
  : state(LOOPER_IDLE) {}

void Looper::setup() {
  // Initialize looper stuff if needed
  // ... any hardware or SD initialization ...
  SD.begin(BUILTIN_SDCARD); // or your SD chip select pin

  // Try to load previous state
  if (!StorageManager::loadState(looperState)) {
      // Optionally: print a message or handle first-time setup
      Serial.println("No previous looper state found or failed to load.");
  } else {
      Serial.println("Looper state loaded from SD card.");
  }
}

void Looper::update() {
  handleState();
}

void Looper::startRecording() {
  requestStateTransition(LOOPER_RECORDING, true);
}

void Looper::stopRecording() {
  requestStateTransition(LOOPER_PLAYING, true);
}

void Looper::startPlayback() {
  requestStateTransition(LOOPER_PLAYING, false);
}

void Looper::stopPlayback() {
  requestStateTransition(LOOPER_IDLE, false);
}

void Looper::startOverdub() {
  requestStateTransition(LOOPER_OVERDUBBING, false);
}

void Looper::stopOverdub() {
  requestStateTransition(LOOPER_PLAYING, false);
}

LooperState Looper::getState() const {
  return state;
}

// Private ---------------------------

void Looper::handleState() {
  // Example: check for scheduled transitions, or metronome sync
}

void Looper::requestStateTransition(LooperState targetState, bool quantize) {
  // Example: if quantize == true, schedule it at next bar boundary
  // Otherwise, switch immediately:
  state = targetState;
}
