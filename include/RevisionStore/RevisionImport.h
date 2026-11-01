//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
#pragma once

// Stage C1 (DEC-246): import a Loop's committed passes into a disposable RevisionStore manifest.
//
// Production keeps every write (capture, commit, undo, persistence); this only *reads* committed state after it is
// authoritative. Layout of the imported manifest:
//   - one run per active capture pass in commit order (Record, then overdubs by `mergeSequence`), holding that pass's
//     committed chunk rows (DEC-245 layer 1); NoteOff rows are stamped with their note's id using production's own
//     pairing (`NoteUtils::reconstructDisplayNotes`), so a hide removes both ends of a note;
//   - ONE Edit run for all active Note edit passes: per target a hide plus its final geometry after production's own
//     `applyNoteEditPass` of that target's rows in edit-pass id order. Edit semantics are never re-implemented here.
//     This collapsed Edit run is a Stage-C shadow-import convenience, valid for parity only: it discards per-edit
//     provenance and order, so it is NOT a candidate long-term representation of edits (stage D/E keeps one immutable
//     edit run per edit, DEC-245).
// A pass the importer cannot express, or a store that cannot hold the loop, is a named refusal and leaves the store
// empty: there is no partial mirror, and the production reader stays authoritative.
//
// Off-line / stopped use: this walks every committed row and allocates production vectors; it is not a real-time path.

#include <algorithm>
#include <vector>

#include "CommittedEventRange.h"
#include "EditApply.h"
#include "EditPass.h"
#include "Loop.h"
#include "LoopPasses.h"
#include "MidiEvent.h"
#include "RevisionStore/RevisionStore.h"
#include "Utils/NoteUtils.h"

namespace RevisionStore {

enum class ImportResult : uint8_t {
  Ok,
  NoRecordPass,         // nothing committed and active to import
  DeniedCapacity,       // the store refused a pass, a row or a manifest: the slot does not fit the Stage-C pool
  UnsupportedEditPass,  // an active edit pass the importer does not express (ControlChange / Audio)
};

struct ImportStats {
  uint32_t captureRuns = 0;
  uint32_t editRuns = 0;
  uint32_t rows = 0;
  uint32_t hides = 0;
  uint32_t unpairedNoteOffs = 0;  // NoteOff rows production pairing did not attribute to a note (kept, id 0)
};

namespace detail {

inline uint8_t statusOf(const MidiEvent& e) {
  return static_cast<uint8_t>(static_cast<uint8_t>(e.type) | ((e.channel - 1) & 0x0F));
}

// The committed rows of one capture pass, with each NoteOff given its note's id by production pairing.
inline void passRowsWithPairedIds(const CommittedChunkIdList& chunks, uint32_t loopLengthTicks, MidiEventVec& rows,
                                  ImportStats& stats) {
  rows.clear();
  const CommittedChunkIdList* lists[] = {&chunks};
  CommittedEventRange::full(lists, 1, loopLengthTicks).appendTo(rows);
  const NoteUtils::DisplayNoteVec notes =
      NoteUtils::reconstructDisplayNotes(rows, loopLengthTicks, /*verboseLog=*/false, /*finishOpenNotes=*/false);
  for (MidiEvent& e : rows) {
    if (!e.isNoteOff() || e.noteId != kInvalidNoteId) continue;
    for (const NoteUtils::DisplayNote& n : notes) {
      if (n.note == e.data.noteData.note && n.endTick == e.tick) {
        e.noteId = n.noteId;
        break;
      }
    }
    if (e.noteId == kInvalidNoteId) ++stats.unpairedNoteOffs;
  }
}

template <typename Cap>
inline bool appendRows(StoreCore<Cap>& store, const MidiEventVec& rows) {
  for (const MidiEvent& e : rows) {
    if (store.appendMidi(e.tick, statusOf(e), e.data.noteData.note, e.data.noteData.velocity, e.noteId) !=
        AppendResult::Ok) {
      return false;
    }
  }
  return true;
}

}  // namespace detail

/// Replace `store`'s content with an import of `loop`'s committed, active passes. On any refusal the store is left
/// empty and the result names why.
template <typename Cap>
ImportResult importLoop(const Loop& loop, StoreCore<Cap>& store, ImportStats* statsOut = nullptr) {
  ImportStats stats;
  store.releaseAll();
  const LoopPasses& passes = loop.passes;
  const uint32_t length = loop.loopLengthTicks;
  if (!passes.hasRecordPass() || passes.recordPass.state != CapturePassState::Active) {
    return ImportResult::NoRecordPass;
  }
  // Unsupported edit kinds refuse before anything is written.
  for (const EditPass& edit : passes.editPasses) {
    if (edit.state == EditPassState::Active && edit.passType != EditPassType::Note) {
      return ImportResult::UnsupportedEditPass;
    }
  }
  auto refuse = [&]() {
    store.abandonPass();
    store.releaseAll();
    if (statsOut != nullptr) *statsOut = stats;
    return ImportResult::DeniedCapacity;
  };

  // Every source note's events, for the edit runs' starting geometry.
  MidiEventVec allSourceRows;
  MidiEventVec rows;
  std::vector<const OverdubPass*> overdubs;
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.state == CapturePassState::Active) overdubs.push_back(&pass);
  }
  std::sort(overdubs.begin(), overdubs.end(),
            [](const OverdubPass* a, const OverdubPass* b) { return a->mergeSequence < b->mergeSequence; });

  for (size_t p = 0; p <= overdubs.size(); ++p) {
    const CommittedChunkIdList& chunks = p == 0 ? passes.recordPass.committedChunkIds : overdubs[p - 1]->committedChunkIds;
    detail::passRowsWithPairedIds(chunks, length, rows, stats);
    if (store.beginPass(p == 0 ? PassKind::Record : PassKind::Overdub, length) != AdmitResult::Ok) return refuse();
    if (!detail::appendRows(store, rows)) return refuse();
    if (store.commitPass() == kNoManifest) return refuse();
    ++stats.captureRuns;
    stats.rows += static_cast<uint32_t>(rows.size());
    allSourceRows.insert(allSourceRows.end(), rows.begin(), rows.end());
  }

  // Note edit passes, applied in id order per target by production's own `applyNoteEditPass`. The import collapses
  // them into ONE Edit run (a hide per edited target plus its final geometry): a disposable read view does not spend
  // one manifest run per edit. Per-edit provenance arrives when the store owns edits (stage D/E).
  std::vector<const EditPass*> edits;
  for (const EditPass& edit : passes.editPasses) {
    if (edit.state == EditPassState::Active) edits.push_back(&edit);
  }
  if (!edits.empty()) {
    std::sort(edits.begin(), edits.end(), [](const EditPass* a, const EditPass* b) { return a->id < b->id; });
    struct Current {
      NoteId id;
      MidiEventVec events;
    };
    std::vector<Current> current;  // each edited target's geometry, in first-edit order
    for (const EditPass* edit : edits) {
      const NoteId target = edit->targetNoteId;
      Current* entry = nullptr;
      for (Current& c : current) {
        if (c.id == target) entry = &c;
      }
      if (entry == nullptr) {
        current.push_back({target, {}});
        entry = &current.back();
        for (const MidiEvent& e : allSourceRows) {
          if (target != kInvalidNoteId && e.noteId == target) entry->events.push_back(e);
        }
      }
      applyNoteEditPass(entry->events, *edit, length);
      for (MidiEvent& e : entry->events) {
        if ((e.isNoteOn() || e.isNoteOff()) && e.noteId == kInvalidNoteId) e.noteId = target;
      }
    }
    MidiEventVec replacements;
    for (const Current& c : current) replacements.insert(replacements.end(), c.events.begin(), c.events.end());
    std::stable_sort(replacements.begin(), replacements.end(), [](const MidiEvent& a, const MidiEvent& b) {
      if (a.tick != b.tick) return a.tick < b.tick;
      const int ca = a.isNoteOff() ? 0 : (a.isNoteOn() ? 1 : 2);
      const int cb = b.isNoteOff() ? 0 : (b.isNoteOn() ? 1 : 2);
      return ca < cb;
    });
    if (store.beginPass(PassKind::Edit) != AdmitResult::Ok) return refuse();
    for (const Current& c : current) {
      if (c.id == kInvalidNoteId) continue;
      if (store.appendHide(c.id) != AppendResult::Ok) return refuse();
      ++stats.hides;
    }
    if (!detail::appendRows(store, replacements)) return refuse();
    if (store.commitPass() == kNoManifest) return refuse();
    ++stats.editRuns;
    stats.rows += static_cast<uint32_t>(replacements.size());
  }
  if (statsOut != nullptr) *statsOut = stats;
  return ImportResult::Ok;
}

}  // namespace RevisionStore
