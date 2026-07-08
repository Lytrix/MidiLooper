//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file TrackMem.h
 * @brief Place track cold-path code in flash on Teensy 4.1 to preserve RAM1/ITCM.
 *
 * Slot clear, transport reconcile, and transport-stop record finalize are not
 * MIDI clock hot paths. See docs/plans/capture_serial_ram1_recovery_extmem_debug_enhancement.md.
 */
#pragma once

#if defined(__IMXRT1062__)
#include <Arduino.h>
#define TRACK_COLD_MEM FLASHMEM
#else
#define TRACK_COLD_MEM
#endif
