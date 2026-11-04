//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Fixed capacities of the content engine (openspec/changes/fixed-memory-engine, plan revision 3 § 2 and § 4).
// Every region the engine owns is sized here; nothing is sized at run time.

#pragma once

#include <cstddef>
#include <cstdint>

namespace ContentEngine {

using PageId = uint16_t;
using RunId = uint16_t;
using ManifestId = uint16_t;

constexpr PageId kNoPage = 0xFFFFu;
constexpr RunId kNoRun = 0xFFFFu;
constexpr ManifestId kNoManifest = 0xFFFFu;

namespace Config {
constexpr uint16_t kRowsPerPage = 64;  // 64 x 16 B = 1 KiB pages
constexpr uint16_t kPageCount = 4096;  // content quota 3,264 + compaction 256 + edit index 576
constexpr uint16_t kContentQuotaPages = 3264;
constexpr uint16_t kCompactionReservePages = 256;
constexpr uint16_t kIndexReservePages = 576;
static_assert(kContentQuotaPages + kCompactionReservePages + kIndexReservePages == kPageCount,
              "the page pool is exactly the three partitions");

constexpr uint16_t kRunCount = 544;
constexpr uint16_t kCommittedRunLimit = 512;
constexpr uint16_t kManifestCount = 288;
constexpr uint8_t kRunsPerManifest = 63;  // 60 committed + 3 writer runs; fits the 6-bit merge key
constexpr uint16_t kHistoryCapacity = 512;
constexpr uint8_t kSlotCount = 64;
constexpr uint8_t kWriterCount = 8;
constexpr uint8_t kInlinePagesPerRun = 16;
constexpr uint8_t kMaxOpenNotesPerWriter = 128;

// OUT-POLY (owner decision 2026-11-04, ratified end-state constant): at most 16 simultaneously sounding MIDI output
// note instances per track. It limits output only, never recording, storage, undo history or the piano roll.
constexpr uint8_t kMaxSoundingPerTrack = 16;
constexpr uint16_t kWindowEntries = 3072;           // span-window buffer, kept pending E3 qualification
constexpr uint16_t kWindowSourceAdmission = 2816;   // source START rows admitted per 2-bar window per slot
static_assert(kWindowSourceAdmission + kMaxSoundingPerTrack <= kWindowEntries,
              "CE-B1: entries(W) <= START rows + carried instances (2,832) fit the window buffer");
}  // namespace Config

}  // namespace ContentEngine
