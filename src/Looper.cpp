//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0


#include <Arduino.h>
#include "Looper.h"
#include "LooperState.h"
#include "StorageManager.h"
#include <SD.h>

Looper looper;  // Global instance

Looper::Looper() {}

void Looper::setup() {
  // Initialize looper stuff if needed
  // ... any hardware or SD initialization ...
  SD.begin(BUILTIN_SDCARD); // or your SD chip select pin

#if defined(BOOT_QUARANTINE_WORKSPACE) && (BOOT_QUARANTINE_WORKSPACE)
  StorageManager::quarantineCurrentWorkspaceOnSd();
#elif defined(SESSION_CAPTURE)
  StorageManager::pollBootQuarantineWorkspaceBeforeLoad(3000);
#endif

  // Try to load previous state
  if (!StorageManager::loadState(looperState.getLooperState())) {
      // Optionally: print a message or handle first-time setup
      Serial.println("No previous looper state found or failed to load.");
  } else {
      Serial.println("Looper state loaded from SD card.");
  }
}

void Looper::update() {
}
