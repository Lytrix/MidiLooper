//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Fixed capacities of the content engine.
// Every region the engine owns is sized here; nothing is sized at run time.

#pragma once

#include <cstddef>
#include <cstdint>

/// Code run off the hot path (the groove builder, run on a setting change): placed in flash on the Teensy, so it
/// takes no RAM1 (ITCM) — RAM1 is assigned in 32 KiB banks and a few KiB of new hot code can cost a bank of stack.
#if defined(__arm__)
#define CE_COLD __attribute__((section(".flashmem.engine")))  // its own section: inline members (see OledScreen)
#else
#define CE_COLD
#endif

namespace ContentEngine {

using PageId = uint16_t;
using RunId = uint16_t;
using ManifestId = uint16_t;

constexpr PageId kNoPage = 0xFFFFu;
constexpr RunId kNoRun = 0xFFFFu;
constexpr ManifestId kNoManifest = 0xFFFFu;

namespace Config {
// Writers: one track at a time records or overdubs, so one writer's holdings are reserved.
constexpr uint8_t kWriterCount = 1;
constexpr uint8_t kMaxOpenNotesPerWriter = 128;
constexpr uint32_t kWriterReserveRows = kMaxOpenNotesPerWriter + 32;  // owed NoteOffs + controller closures
// NOTE_EDIT: one session of <= 512 selected notes; its edit run is <= 3 rows per note (hide + NoteOn + NoteOff),
// and a rebuild writes a fresh run while the published one still plays, so two edit runs are budgeted.
constexpr uint16_t kEditSelection = 512;
constexpr uint8_t kEditSteps = 99;            // E: steps (limit 99), oldest forgotten
constexpr uint16_t kEditLogEntries = 1024;    // changed notes recorded across the E: steps
constexpr uint32_t kEditRunRows = 3u * kEditSelection;
constexpr uint32_t kEditReserveRows = 2u * kEditRunRows;
constexpr uint32_t kCommittedRowLimit = 131072;                        // today's admission ceiling
constexpr uint32_t kContentRows = kCommittedRowLimit + kWriterCount * kWriterReserveRows + kEditReserveRows;

// Descriptors: committed and history runs <= 512, per writer <= 3 (active + closing + next), two edit runs (published +
// rebuilding), one compaction output. Manifests: history 99 (slot currents and overdub / NOTE_EDIT session steps are
// history states), writer commits 3 each, one NOTE_EDIT rebuild being published, pinned after replacement 24 windows +
// 18 cursors + 6 tiles + 3 workers.
constexpr uint16_t kCommittedRunLimit = 512;
constexpr uint16_t kRunCount = 520;
static_assert(kCommittedRunLimit + kWriterCount * 3 + 2 + 1 <= kRunCount, "run descriptors cover W");
constexpr uint16_t kManifestCount = 256;
static_assert(99 + kWriterCount * 3 + 1 + 24 + 18 + 6 + 3 + 1 <= kManifestCount, "manifest slots cover W and a preview");

constexpr uint16_t kRowsPerPage = 64;  // 64 x 16 B = 1 KiB pages
// Content quota: full pages for every row, a partial page per live run plus one hide partial per edit run
// (capped at 500), and the directory pages of the full pages (16 inline pages per run, 512 ids per directory page).
constexpr uint32_t kContentFullPages = (kContentRows + kRowsPerPage - 1) / kRowsPerPage;
constexpr uint16_t kContentQuotaPages = static_cast<uint16_t>(kContentFullPages + kRunCount + 500 + kContentFullPages / 17 +
                                                              (kContentFullPages + 511) / 512);
constexpr uint16_t kCompactionReservePages = 256;
constexpr uint16_t kIndexReservePages = 576;
constexpr uint8_t kRunsPerManifest = 63;  // 60 committed + 3 writer runs; fits the 6-bit merge key
// Get-a-loop preview (8,192 rows): its own run descriptors (one state's
// worth) and pages — the full pages of its rows, a partial page per run, the directory pages — never content's.
constexpr uint32_t kPreviewRows = 8192;
constexpr uint16_t kPreviewRuns = kRunsPerManifest;
constexpr uint16_t kPreviewPages = static_cast<uint16_t>(kPreviewRows / kRowsPerPage + kPreviewRuns +
                                                         kPreviewRows / kRowsPerPage / 17 + 1);
constexpr uint16_t kPageCount = kContentQuotaPages + kCompactionReservePages + kIndexReservePages + kPreviewPages;
static_assert(kContentQuotaPages == 3247 && kPreviewPages == 199 && kPageCount == 4278,
              "the pool's partitions (one writer, two edit runs, the preview partition)");
constexpr uint8_t kHistoryDepth = 99;  // hot undo states across all slots (the oldest goes cold on the card, DEC-254)
constexpr uint8_t kSlotCount = 64;
constexpr uint8_t kInlinePagesPerRun = 16;

// OUT-POLY: at most 16 simultaneously sounding MIDI output
// note instances per track. It limits output only, never recording, storage, undo history or the piano roll.
constexpr uint8_t kMaxSoundingPerTrack = 16;
// Continuous controllers (MidiClass.h): at most 64 values per track per tick, recorded
// (the writer) and played (the composer leaves later ones out of the window). Device: a controller message on 3 ports
// costs ~0.5 us, so 64 per track take ~31 us per tick and ~250 us for 8 tracks (~24 % of a 1.04 ms tick at 300 BPM).
constexpr uint8_t kMaxContinuousPerTick = 64;
constexpr uint16_t kWindowEntries = 3072;           // span-window buffer
constexpr uint16_t kWindowSourceAdmission = 2816;   // source START rows admitted per 2-bar window per slot
static_assert(kWindowSourceAdmission + kMaxSoundingPerTrack <= kWindowEntries,
              "CE-B1: entries(W) <= START rows + carried instances (2,832) fit the window buffer");
}  // namespace Config

}  // namespace ContentEngine
