//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file LoopValidationMem.h
 * @brief Place loop event validation code in flash on Teensy 4.1 to preserve RAM1/ITCM.
 *
 * Canonical invariant checks run at macro commit, deferred idle maintenance, and SD load —
 * never on the MIDI clock hot path (full validate is explicitly deferred off the stop path, see
 * docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md). Scoped to LoopEventValidation because that
 * module serves Track, Loop and Storage alike, so the note-edit and track macros do not fit.
 * See docs/plans/capture_serial_ram1_recovery_extmem_debug_enhancement.md.
 */
#pragma once

#if defined(__IMXRT1062__)
#include <Arduino.h>
#define LOOP_VALIDATION_MEM FLASHMEM
#else
#define LOOP_VALIDATION_MEM
#endif
