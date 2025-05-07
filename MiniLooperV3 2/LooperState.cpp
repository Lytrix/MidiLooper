#include "Globals.h"      // Shared timing, bar length, etc.
#include "LooperState.h"
#include "TrackManager.h"
#include "MidiHandler.h"

// --- Current and Pending State ---
LooperState looperState = LOOPER_IDLE;

static LooperState pendingState = LOOPER_IDLE;
static bool pendingQuantized = false;
static bool transitionArmed = false;

// --- Internal Transition Logic ---
static void actuallyTransition() {
  // --- Exit old state ---
  switch (looperState) {
    case LOOPER_RECORDING:
      // Finalize recording, maybe trim
      break;
    case LOOPER_OVERDUBBING:
      // Optionally finalize overdub layer
      break;
    case LOOPER_PLAYING:
    case LOOPER_IDLE:
      break;
  }

  looperState = pendingState;

  // --- Enter new state ---
  switch (looperState) {
    case LOOPER_RECORDING:
      // Start recording
      break;
    case LOOPER_OVERDUBBING:
      // Begin overdub
      break;
    case LOOPER_PLAYING:
      // Resume playback
      break;
    case LOOPER_IDLE:
      // Silence?
      break;
  }

  transitionArmed = false;
}

static void handleTransition() {
  if (!transitionArmed) return;

  if (pendingQuantized) {
    if (isBarBoundary()) {  // <- Implement this check!
      //actuallyTransition();
    }
  } else {
    //actuallyTransition();
  }
}

// --- Main State Handler (called in loop) ---
void handleLooperState() {
  handleTransition();

  switch (looperState) {
    case LOOPER_IDLE:
      // Nothing to do
      break;

    case LOOPER_RECORDING:
      // Could flash LED or show recording status
      break;

    case LOOPER_PLAYING:
      // Could blink LED in tempo, etc.
      break;

    case LOOPER_OVERDUBBING:
      // Visual feedback for overdubbing
      break;
  }
}

// --- Public API ---
void requestStateTransition(LooperState newState, bool quantize) {
  pendingState = newState;
  pendingQuantized = quantize;
  transitionArmed = true;
}
