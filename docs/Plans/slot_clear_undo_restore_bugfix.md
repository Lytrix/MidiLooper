# Slot clear undo restore bugfix

**Kind:** bugfix  
**Date:** 2026-07-13  
**Status:** Implemented

## Problem

Long-press clear on Record (36) pushed a `ClearSlot` undo checkpoint via `pushClearTrackSnapshot`, then `Track::clear()` called `clearUndoHistoryForSlot`, which erased **all** undo entries for that slot — including the checkpoint just pushed. Record double-press undo had nothing to restore.

## Fix

1. **`eraseUndoEntriesForSlot`** — prune pass/edit undo entries for the cleared slot but **preserve** `UndoEntryKind::ClearSlot` entries.
2. **`pushClearTrackSnapshot`** — capture `beforeSlotEnabled` / `beforeSlotMuted` from `TrackManager` before clear disables the slot.
3. **`handleClearTrack`** — push snapshot before slot-flag mutation.
4. **`undoForLoop` / `redoForLoop`** — post-restore side effects for `ClearSlot` (slot flags, playback re-anchor, LED refresh).
5. **`Track::clear()`** — no longer prunes pass undo entries; pass depth survives clear so **U:** restores after undo-clear. Fresh record on a cleared slot resets undo via `pushRecordPassAdded`.
6. **`undoDepthForLoop`** — sidebar **U:** counts pass/edit undo only when the slot has published MIDI; cleared slots show **U:--** while pass undo entries remain on the stack for restore-on-undo.

## Gesture (unchanged mapping)

| Record (36) | Action |
|-------------|--------|
| Long | Clear selected slot |
| Double | Undo (overdub, pass, **clear restore**) |
| Triple | Redo (including re-clear) |

Loop slot buttons (50–57) unchanged — remapped in future `slot-performance-interaction` OpenSpec.

## Pre-implementation review

### Ready

- Root cause confirmed in `Track::clear()` + `eraseUndoEntriesForSlot`
- Existing `applyUndoEntry` / `applyRedoEntry` `ClearSlot` branches sufficient for pass restore

### Resolved

| Topic | Decision |
|-------|----------|
| Control | Record (36) only |
| Gesture | Double = undo, triple = redo (existing map) |
| Loop slot buttons | Out of scope |

## Verification

- `pio test -e native` — `test_global_undo_slot_scope` ClearSlot preservation tests
- Manual: long-press Record → double-press → loop restored; triple-press re-clears
