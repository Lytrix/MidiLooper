//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <cstddef>

#include "Globals.h"

namespace LoadLoopBudget {

/// Focused slot load slice (µs per main-loop frame).
constexpr uint32_t FocusRestoreUs = 1200;
/// Background / demoted load slice.
constexpr uint32_t BackgroundRestoreUs = 250;
/// Boot title playback-set drain slice.
constexpr uint32_t BootTitleRestoreUs = 5000;

/// Internal SD read size per timed step (not the public scheduling unit).
constexpr size_t ReadChunkBytes = 2048;

/// **A3.5:** the post-load commit save holdoff, as **one** declaration. After
/// `cleanupLoadLoopJobPublish()` publishes a restored slot, the deferred save is held off for this long
/// so the post-commit prewarm frame is not competing with SD writes (device observation 000659).
///
/// It is a finite wait, not a gate state. The blocking drain **reports it by name rather than waiting it
/// out**: the drain runs on the interval path and the timing contract keeps every
/// `midiHandler.handleMidiInput()` call in `main.cpp`, so a drain that blocked here and serviced MIDI
/// would move an input drain into the interval. The drain therefore preserves the restore and the owed
/// save and returns `post_load_holdoff` immediately; the interval expires on its own deadline while
/// `main` keeps servicing MIDI, and the next attempt completes the save.
constexpr uint32_t PostLoadCommitSaveHoldoffMs = 100;

/// Resolve per-frame µs budget for LoadLoopJob work.
/// captureActive (RECORDING/OVERDUBBING): background budget is 0 unless focusWork.
uint32_t resolveLoadLoopSliceBudgetUs(bool bootTitle, bool focusWork, bool captureActive);

/// Same exhaustion rule as PersistenceBudget::persistenceSliceBudgetExhausted.
bool loadLoopSliceBudgetExhausted(uint32_t sliceBudgetUs, uint32_t elapsedUs);

/// Preemption exception during Reading: finish active job before switching focus
/// when apply is pending or one more read chunk completes the SD payload.
/// Parsing may be demoted/parked; Committing must finish (caller enforces).
bool shouldFinishActiveBeforePreempt(size_t bytesRead, size_t payloadSize,
                                     size_t chunkBytes = ReadChunkBytes);

}  // namespace LoadLoopBudget
