//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file LoopMem.h
 * @brief Place Loop cold-path code in flash on Teensy 4.1 to preserve RAM1/ITCM.
 */
#pragma once

#if defined(__IMXRT1062__)
#include <Arduino.h>
#define LOOP_COLD_MEM FLASHMEM
#else
#define LOOP_COLD_MEM
#endif
