//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Globals.h"      // Shared timing, bar length, etc.
#include "LooperState.h"
#include "TrackManager.h"
#include "MidiHandler.h"
#include "Utils/DebugSessionCapture.h"

LooperStateManager looperState;

// --- Internal Transition Logic ---
void LooperStateManager::actuallyTransition() {
  // --- Exit old state ---
  switch (this->looperState) {
    case LOOPER_RECORDING:
    case LOOPER_OVERDUBBING:
      // TODO: Add exit logic if needed
      break;
    default:
      break;
  }
  // --- Enter new state ---
  this->looperState = this->pendingState;
  switch (this->looperState) {
    case LOOPER_RECORDING:
    case LOOPER_OVERDUBBING:
      // TODO: Add enter logic if needed
      break;
    default:
      break;
  }
  this->transitionArmed = false;
}

void LooperStateManager::update() {
  // Handle overlays
  if (this->editOverlayActive) {
    // Edit overlay logic
    return;
  }
  if (this->settingsOverlayActive) {
    // Settings overlay logic
    return;
  }
  if (this->loadSaveOverlayActive) {
    // Load/save set browser overlay logic
    return;
  }
  // Main looper state logic
  if (this->transitionArmed) {
    this->actuallyTransition();
  }
  // TODO: Add main looper state handling here
}

void LooperStateManager::requestStateTransition(LooperState newState, bool quantize) {
  this->pendingState = newState;
  this->pendingQuantized = quantize;
  this->transitionArmed = true;
}

void LooperStateManager::enterEditMode(EditContext ctx) {
  this->editOverlayActive = true;
  this->editContext = ctx;
}

void LooperStateManager::exitEditMode() {
  this->editOverlayActive = false;
  this->editContext = EDIT_NONE;
}

void LooperStateManager::enterSettingsMode() {
  this->settingsOverlayActive = true;
}

void LooperStateManager::exitSettingsMode() {
  this->settingsOverlayActive = false;
}

void LooperStateManager::enterLoadSaveMode() {
  if (this->loadSaveOverlayActive) {
    return;
  }
  this->loadSaveOverlayActive = true;
  SC_LOADSAVE(1);
}

void LooperStateManager::exitLoadSaveMode() {
  if (!this->loadSaveOverlayActive) {
    return;
  }
  this->loadSaveOverlayActive = false;
  SC_LOADSAVE(0);
}

void LooperStateManager::setEditContext(EditContext ctx) {
  this->editContext = ctx;
}

EditContext LooperStateManager::getEditContext() {
  return this->editContext;
}

