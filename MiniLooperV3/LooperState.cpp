// LooperState.cpp

#include "Globals.h"  // where you store tempo, bar length etc.
#include "LooperState.h"
#include "Track.h"
#include "TrackManager.h"
#include "MidiHandler.h"

LooperState looperState = LOOPER_IDLE;

static LooperState pendingState = LOOPER_IDLE;
static bool pendingQuantized = false;   // <- NEW
static bool transitionArmed = false;    // <- NEW

void actuallyTransition() {
  // Exit old state
  switch (looperState) {
    case LOOPER_RECORDING:
      // Finish recording
      break;
    case LOOPER_PLAYING:
      break;
    case LOOPER_OVERDUBBING:
      break;
    case LOOPER_IDLE:
      break;
  }

  // Move to new state
  looperState = pendingState;

  // Enter new state
  switch (looperState) {
    case LOOPER_RECORDING:
      track.clear();
      break;
    case LOOPER_PLAYING:
      break;
    case LOOPER_OVERDUBBING:
      break;
    case LOOPER_IDLE:
      break;
  }

  // Clear pending
  transitionArmed = false;
}

void handleTransition() {
  if (!transitionArmed) return;  // no pending transition

  if (pendingQuantized) {
    // Check if we are at the end of the bar
    if (isBarBoundary()) {  // <- implement this function!
      actuallyTransition();
    }
  } else {
    // Immediate transition
    actuallyTransition();
  }
}

void handleLooperState() {
  handleTransition();

  uint32_t now = millis();

  switch (looperState) {
    case LOOPER_IDLE:
      break;
    case LOOPER_RECORDING:
      break;
    case LOOPER_PLAYING:
      break;
    case LOOPER_OVERDUBBING:
      break;
  }
}

void requestStateTransition(LooperState newState, bool quantize) {
  pendingState = newState;
  pendingQuantized = quantize;
  transitionArmed = true;
}

