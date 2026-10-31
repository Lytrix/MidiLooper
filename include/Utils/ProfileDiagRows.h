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

/// Per-request row maxima (design D8).
constexpr size_t kStatsBatchMax = 16;
constexpr size_t kRingBatchMax = 32;
constexpr size_t kRowCapacity = 224;

/// Handles `!PROFILE_STATS`, `!PROFILE_RING` and `!PROFILE_RESET`. Returns false for any other line.
/// Every profile command is refused (one `profile_cmd,refused` row, no state change) unless no scope
/// is live. Rows go through `RuntimeProfiler::Platform::emitRow`.
bool handleCommand(const char* line);

/// One batch of each dump, with an explicit maximum so fixtures can drain a table in several batches.
/// Return the number of data rows emitted (the terminator is not counted).
size_t dumpStatsBatch(size_t maxRows);
size_t dumpRingBatch(size_t maxRows);

}  // namespace ProfileDiagRows

#endif  // RUNTIME_PROFILE_SCOPES
