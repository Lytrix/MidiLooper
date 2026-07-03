//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file NoteEditMem.h
 * @brief Place note-edit cold-path code in flash on Teensy 4.1 to preserve RAM1/ITCM.
 *
 * Note-edit overlap, focus rebuild, and reconstructNotes run on fader/encoder paths —
 * not MIDI clock hot paths. See docs/plans/capture_serial_ram1_recovery_extmem_debug_enhancement.md.
 */
#pragma once

#if defined(__IMXRT1062__)
#include <Arduino.h>
#define NOTE_EDIT_MEM FLASHMEM
#else
#define NOTE_EDIT_MEM
#endif
