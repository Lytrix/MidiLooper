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
// Writers (owner decision 2026-11-05): one track at a time records or overdubs, so one writer's holdings are reserved.
constexpr uint8_t kWriterCount = 1;
constexpr uint8_t kMaxOpenNotesPerWriter = 128;
constexpr uint32_t kWriterReserveRows = kMaxOpenNotesPerWriter + 32;  // owed NoteOffs + controller closures
constexpr uint32_t kEditReserveRows = 1536;                           // one NOTE_EDIT session
constexpr uint32_t kCommittedRowLimit = 131072;                        // today's admission ceiling
constexpr uint32_t kContentRows = kCommittedRowLimit + kWriterCount * kWriterReserveRows + kEditReserveRows;

// Descriptors (plan § 5.2): committed and history runs <= 512, per writer <= 3 (active + closing + next), one edit
// run, one compaction output. Manifests: history 99 (slot currents and overdub / NOTE_EDIT session steps are history
// states), writer commits 3 each, pinned after replacement 24 windows + 18 cursors + 6 tiles + 3 workers.
constexpr uint16_t kCommittedRunLimit = 512;
constexpr uint16_t kRunCount = 520;
static_assert(kCommittedRunLimit + kWriterCount * 3 + 1 + 1 <= kRunCount, "run descriptors cover W");
constexpr uint16_t kManifestCount = 256;
static_assert(99 + kWriterCount * 3 + 24 + 18 + 6 + 3 <= kManifestCount, "manifest slots cover W");

constexpr uint16_t kRowsPerPage = 64;  // 64 x 16 B = 1 KiB pages
// Content quota (plan § 5.1): full pages for every row, a partial page per live run plus one hide partial per edit run
// (capped at 500), and the directory pages of the full pages (16 inline pages per run, 512 ids per directory page).
constexpr uint32_t kContentFullPages = (kContentRows + kRowsPerPage - 1) / kRowsPerPage;
constexpr uint16_t kContentQuotaPages = static_cast<uint16_t>(kContentFullPages + kRunCount + 500 + kContentFullPages / 17 +
                                                              (kContentFullPages + 511) / 512);
constexpr uint16_t kCompactionReservePages = 256;
constexpr uint16_t kIndexReservePages = 576;
constexpr uint16_t kPageCount = kContentQuotaPages + kCompactionReservePages + kIndexReservePages;
static_assert(kContentQuotaPages == 3222 && kPageCount == 4054, "plan § 5.1 figures (one writer)");

constexpr uint8_t kRunsPerManifest = 63;  // 60 committed + 3 writer runs; fits the 6-bit merge key
constexpr uint8_t kHistoryDepth = 99;  // undo states held across all slots (owner 2026-11-05: shared, oldest evicted)
constexpr uint8_t kSlotCount = 64;
constexpr uint8_t kInlinePagesPerRun = 16;

// OUT-POLY (owner decision 2026-11-04, ratified end-state constant): at most 16 simultaneously sounding MIDI output
// note instances per track. It limits output only, never recording, storage, undo history or the piano roll.
constexpr uint8_t kMaxSoundingPerTrack = 16;
constexpr uint16_t kWindowEntries = 3072;           // span-window buffer, kept pending E3 qualification
constexpr uint16_t kWindowSourceAdmission = 2816;   // source START rows admitted per 2-bar window per slot
static_assert(kWindowSourceAdmission + kMaxSoundingPerTrack <= kWindowEntries,
              "CE-B1: entries(W) <= START rows + carried instances (2,832) fit the window buffer");
}  // namespace Config

}  // namespace ContentEngine
