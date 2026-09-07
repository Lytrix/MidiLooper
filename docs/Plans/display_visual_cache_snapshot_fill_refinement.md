# Display visual-cache snapshot fill

**Status:** Stage 2.1 host shipped (membership copy). Stage 2.0 load publish not started.  
**Host:** `teensy41-capture-serial` RAM1 locals **6848**. Native **1727/1728** (1 skipped). Device recapture open (`idle_src,SnapshotNotes` when a current root exists).  
**Kind:** refinement  
**GitHub:** [#41](https://github.com/Lytrix/MidiLooper/issues/41) Stage 2  
**Parent:** [`display_visual_cache_snapshot_reuse_refinement.md`](display_visual_cache_snapshot_reuse_refinement.md)  
**Does not start:** OpenSpec `3.5`/`3.6` checkoff, SD snapshot persistence, snapshot-identity undo, LCR conversion (4.3), all-track idle fan-out during RECORDING, cooperative `reconstructDisplayNotes`.

---

## Destination

`visualCache.notes` stays the piano-roll list. A current published `LoopSnapshot` is the committed fill source. Idle copies `LoopSnapshotNote` membership for dirty bars. `reconstructDisplayNotes` / `from=passes` run only when no current root covers those bars.

Paint already selects per bar (`DEC-050`). This slice makes the idle cache a thin copy of that same membership, not a second pairing pipeline.

---

## Why two slices

Boot `adoptPersistedSnapshot` still `markDisplayCachesStale()` and does **not** publish a `LoopSnapshot` (`root_rev=0` on T6/S0 in [`235930`](../../captures/session_20260907_235930.log)). Stage 2.1 copy cannot run there until a root exists.

| Slice | Invariant | Transition change? |
|---|---|---|
| **2.1 membership copy** | When `publishedSnapshotIsCurrent()`, `rebuildVisualCacheIdleSlice` copies snapshot notes for the dirty bar range. Reconstruct is not taken. | NO |
| **2.0 load enqueue** | After adopt/restore of a committed loop with no published root, queue the existing `Loop` publication owner so STOPPED Snapshot idle can build one. | YES — load starts publication. Confirm before firmware. |

2.0 is the “save snapshots” step. It reuses `queueSessionExitSnapshot` / `stepPendingSnapshotPublish`. It does not persist snapshots to SD (OpenSpec later).

---

## Stage 2.1 change

In `Loop::rebuildVisualCacheIdleSlice`, if `publishedSnapshotIsCurrent()`:

1. Copy `LoopSnapshotNote` membership for `[startBar, endBar]` through `appendSnapshotNotesForBarRange`.
2. Set `lastVisualCacheIdleGatherSource_ = Snapshot` and `lastVisualCacheIdleCopiedSnapshotNotes_ = true`.
3. Skip event gather, `reconstructDisplayNotes`, and wrap-edge `appendOverdubPassDisplayNotes` (current root already has sealed membership).
4. Keep existing splice / dirty-bar clear / `slice_clean`.

If the root is missing or not current, keep prepared → budgeted Fallback (Stage 1e).

Native pin: `test_visual_cache_idle_slice_copies_current_snapshot_notes`.

---

## Pre-implementation review

### Ready
- Fill owner: `Loop::rebuildVisualCacheIdleSlice`
- Conversion: `displayNoteFromSnapshotNote` / `forEachSnapshotMembershipReference`
- Paint already copies the same membership per bar in `appendSnapshotNotesForBar`
- Idle already prefers current-root Snapshot events, then reconstructs — 2.1 replaces that reconstruct

### Resolved
| Topic | Decision |
|---|---|
| List owner | `visualCache.notes` stays |
| Copy vs events | Copy `LoopSnapshotNote` when root is current |
| Stale root | Do not copy; Stage 1e prepared/Fallback |
| Wrap append | Skip when membership copy ran |
| Load publish | Not this slice |

### Open before coding
1. Stage 2.0 load enqueue — user confirm (state transition).

### Proceed?
YES for 2.1. NO for 2.0 until confirmed.

---

## Architecture checkpoint (2.1)

1. Ownership change? **NO** — `Loop` still fills `visualCache`.
2. State transition change? **NO** — idle fill source only.
3. Unbounded MIDI-interval owner? **NO** — idle, existing `maxBarsPerSlice`.
