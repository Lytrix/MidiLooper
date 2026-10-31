//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file ProfileDiagRows.h
 * @brief Names and row formats of the runtime profiler (DEC-239). The literals live in
 * `src/Utils/ProfileDiagRows.cpp`, whose `.rodata.str*` is carved into FLASH by
 * `linker/imxrt1062_t41_lcr.ld`, so they cost no DTCM.
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "Utils/RuntimeProfiler.h"

#if RUNTIME_PROFILE_SCOPES

namespace ProfileDiagRows {

constexpr size_t kBootRowCapacity = 224;

/// Stable display name of an identity; never empty, unique per identity.
const char* profileName(RuntimeProfiler::ProfileId id);

/// `#CAP,<us>,DIAG,profile_boot,...` without CRLF. Returns false if the buffer was too small.
bool formatBootRow(char* out, size_t capacity, const RuntimeProfiler::BootReport& report, uint32_t nowUs);

}  // namespace ProfileDiagRows

#endif  // RUNTIME_PROFILE_SCOPES
