//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0


#include <Arduino.h>
#include "Looper.h"
#include "LooperState.h"
#include "StorageManager.h"
#include <SD.h>

Looper looper;  // Global instance

namespace {

constexpr uint8_t kStorageCardInitMaxAttempts = 3;
constexpr uint16_t kStorageCardInitRetryDelayMs = 50;

bool beginStorageCardForBoot() {
  for (uint8_t attempt = 1; attempt <= kStorageCardInitMaxAttempts; ++attempt) {
    if (SD.begin(BUILTIN_SDCARD)) {
      return true;
    }
    Serial.print("[StorageManager] SD init failed (attempt ");
    Serial.print(attempt);
    Serial.print("/");
    Serial.print(kStorageCardInitMaxAttempts);
    Serial.println(")");
    delay(kStorageCardInitRetryDelayMs);
  }
  return false;
}

}  // namespace

Looper::Looper() {}

void Looper::setup() {
  // Initialize looper stuff if needed
  // ... any hardware or SD initialization ...
  const bool storageCardReady = beginStorageCardForBoot();
  if (!storageCardReady) {
    Serial.println("[StorageManager] Boot fallback: SD unavailable; starting with empty runtime state.");
    return;
  }

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
