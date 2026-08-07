//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include "MidiEvent.h"
#include "NoteEditFocus.h"
#include "Utils/ExternalMemoryFirstAllocator.h"

struct EditSessionAction;

enum class NoteEditPresenceType : uint8_t {
  Visible,
  Hidden,
  Deleted,
  Added,
};

struct NoteEditCurrentNoteState {
  NoteId noteId = kInvalidNoteId;
  NoteBaseline committedSpan{};
  NoteBaseline currentSpan{};
  NoteEditPresenceType presence = NoteEditPresenceType::Visible;
};

template <typename T>
using NoteEditCurrentStateMapAllocator =
    ExternalMemoryFirstAllocator<std::pair<const NoteId, T>>;

using NoteEditCurrentStateMap =
    std::unordered_map<NoteId, NoteEditCurrentNoteState, NoteIdHash, std::equal_to<NoteId>,
                       NoteEditCurrentStateMapAllocator<NoteEditCurrentNoteState>>;

struct NoteEditCurrentStateVerifyResult {
  bool passed = true;
  bool duplicateNoteIds = false;
  bool selectedNoteMissing = false;
  bool visibleProjectionMismatch = false;
  bool hiddenOrDeletedProjected = false;
};

/// Marker for direct projected-store mutation during migration.
/// Removal: openspec/changes/note-edit-current-state/tasks.md §5–7.
#define NOTE_EDIT_PROJECTED_STORE_COMPAT 1

/// Authoritative NOTE_EDIT editable note state keyed by NoteId.
class NoteEditCurrentState {
 public:
  void clear() { rows_.clear(); }
  bool empty() const { return rows_.empty(); }
  size_t size() const { return rows_.size(); }

  const NoteEditCurrentNoteState* find(NoteId noteId) const;
  NoteEditCurrentNoteState* find(NoteId noteId);

  NoteEditCurrentNoteState& upsertRow(NoteId noteId, const NoteBaseline& committedSpan,
                                      const NoteBaseline& currentSpan,
                                      NoteEditPresenceType presence);

  const NoteEditCurrentStateMap& rows() const { return rows_; }

  /// Build visible rows from a stamped NOTE_EDIT session store at open/reopen boundaries.
  static NoteEditCurrentState buildFromSessionStore(const MidiEventVec& store, uint8_t channel);

  /// Canonical lossy projection: Visible/Added rows only.
  void projectToSessionStore(MidiEventVec& store, uint8_t channel) const;

  NoteEditCurrentStateVerifyResult verifyInvariants(NoteId selectedPrimaryNote =
                                                        kInvalidNoteId) const;
  NoteEditCurrentStateVerifyResult verifyProjection(const MidiEventVec& store,
                                                    uint8_t channel) const;

  bool hasRow(NoteId noteId) const;
  bool rowProjectsToStore(NoteId noteId) const;
  /// Visible/Added rows whose overlap tail is inventory-masked are excluded from selectable inventory
  /// but remain semantically shortened (not Hidden).
  bool rowIncludedInSelectableInventory(NoteId noteId) const;
  bool isRowHiddenOrDeleted(NoteId noteId) const;
  bool readCurrentSpan(NoteId noteId, NoteBaseline& out) const;

  void applyEditSessionAction(const EditSessionAction& action);
  void markRowDeleted(NoteId noteId);
  void removeRow(NoteId noteId);
  void syncProjectingRowsFromSessionStore(const MidiEventVec& store, uint8_t channel);
  /// After macro commit seals geometry to passes, align session committed baseline so
  /// leave-restore does not revert to a stale committed span (session_20260807_200656).
  void syncCommittedSpan(NoteId noteId, const NoteBaseline& committedSpan);

  /// Full row-map snapshot for session undo/redo (edit-scope sized, not loop-length sized).
  NoteEditCurrentState clone() const;
  void assignFrom(const NoteEditCurrentState& other);

  /// Fold overdub capture into current state as Added rows before projection refresh.
  void mergeCaptureNotesAsAdded(const MidiEventVec& captureFlat, uint8_t channel,
                                uint32_t loopLength);

 private:
  NoteEditCurrentStateMap rows_;
};
