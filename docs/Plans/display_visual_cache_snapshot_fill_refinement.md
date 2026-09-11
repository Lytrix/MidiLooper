# Display visual-cache snapshot fill

**Status:** Stage 2.1 host shipped. Stage 2.0 load publish uses snapshot-authoritative boot hydration.
**Host:** Stage 2.1 `teensy41-capture-serial` RAM1 locals **6848**. Native **1727/1728** (1 skipped).
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

Boot `adoptPersistedSnapshot` used to `markDisplayCachesStale()` and **not** publish a `LoopSnapshot` (`root_rev=0` on T6/S0 in [`235930`](../../captures/session_20260907_235930.log)). Stage 2.1 copy cannot run there until a root exists.

| Slice | Invariant | Transition change? |
|---|---|---|
| **2.1 membership copy** | When `publishedSnapshotIsCurrent()`, `rebuildVisualCacheIdleSlice` copies snapshot notes for the dirty bar range. Reconstruct is not taken. | NO — shipped |
| **2.0 load enqueue** | After adopt/restore of a committed loop with no published root, queue the existing `Loop` publication owner so STOPPED Snapshot idle can build one. | YES — load starts publication. Confirmed. |

2.0 is the “save snapshots” step. It queues on the existing owner. It does not persist snapshots to SD (OpenSpec later). It does not construct on the load path.

---

## Stage 2.0 change

`adoptPersistedSnapshot` and `restorePassesSnapshot` no longer call `queueLoadedSnapshotPublish` after the revision bump and stale mark (`023222`). The function stays for an explicit later selected-slot trigger. See [`loaded_hydration_stability_bugfix.md`](loaded_hydration_stability_bugfix.md).

1. No-op when `publishedSnapshotIsCurrent()`.
2. Empty / no committed passes: `clearPendingSnapshotPublish()` and do not queue.
3. Discard a stale published root so `resolvePendingParent` does not apply loaded passes onto the previous loop.
4. Queue the record pass first, then each active overdub pass, then active edit-pass companions.
5. Set `sessionExitQueued` with `CommitReason::RecordStop` so the first record pass can start from `LoopSnapshot::createEmpty`.
6. Idle `stepPendingSnapshotPublish` constructs. Load/restore only enqueue.
7. STOPPED tracks do not step that work while another track is RECORDING /
   PLAYING / OVERDUBBING. [`003555`](../../captures/session_20260908_003555.log)
   rebooted (`CAPTURE_RECONNECT`) during T0 `RECORDING` while T6 loaded
   construction was still climbing (`snapshot_alloc` `nodes=2083`).
8. STOPPED VisualCache reconstruction is deferred for the active loop while
   its snapshot publication is pending. [`005044`](../../captures/session_20260908_005044.log)
   showed the 66-bar loop continuing snapshot allocation while pass-based
   VisualCache maintenance was still taking 100–187 ms per turn.
9. Loaded-pass fragments set `fromLoadedPasses` and skip
   `findNoteAtPitch` and `eventAlreadyInSnapshot` (same skip as live-record
   `fromCaptureStore` for lookup). Pairing stays on `openNotes`. Live
   session-exit clears the flag so record-stop still looks up notes already
   on the unpublished parent.
   [`005731`](../../captures/session_20260908_005731.log) `CrashReport`:
   `CFSR: 82`, accessed `0x20016544` (stack), PC first instruction of
   `LoopSnapshot::findNoteAtPitch` during title-boot construction
   (`nodes=1754`).
   [`010922`](../../captures/session_20260908_010922.log) uploaded that skip
   and still reset at the same freeze (`nodes=1754`, three boots). Snapshot
   idle is withheld while `isBootTitleLoadDrain()`. One `stepPendingSnapshotPublish`
   call starts or finishes one construction, not both. [`012242`](../../captures/session_20260908_012242.log)
   then showed the title readiness deadlock: VisualCache was also withheld
   while the pending publication remained.    Boot title no longer admits pass-based VisualCache fallback. Title exit
   waits for restore plus a current focus `LoopSnapshot` (or empty content).
   Loaded hydration is the Snapshot owner after restore is empty.
   See [`display_snapshot_boot_hydration_refinement.md`](display_snapshot_boot_hydration_refinement.md).

Native pins: `test_adopt_does_not_queue_loaded_snapshot_publish`,
`test_restore_empty_does_not_queue_loaded_snapshot_publish`,
`test_stopped_track_snapshot_pauses_during_realtime`,
`test_session_exit_pass_change_skips_note_already_on_parent`.

Device gate: after boot STOPPED idle, T6/S0 `root_rev!=0` and VisualCache `idle_src,SnapshotNotes` before first record when idle had time. No `CrashReport` / `CAPTURE_RECONNECT` on title boot while the 66-bar loop publishes.

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
- Fill owner: `Loop::rebuildVisualCacheIdleSlice` (2.1 shipped)
- Publication owner: `queueSessionExitSnapshot` / `stepPendingSnapshotPublish`
- Load owners: `adoptPersistedSnapshot` / `restorePassesSnapshot` (`applySnapshotToLoop` calls adopt)
- 2.1 copy runs only when a current root exists

### Resolved
| Topic | Decision |
|---|---|
| List owner | `visualCache.notes` stays |
| Copy vs events | Copy `LoopSnapshotNote` when root is current |
| Stale root | Do not copy; Stage 1e prepared/Fallback |
| Wrap append | Skip when membership copy ran |
| Load publish | Queue only; `queueLoadedSnapshotPublish` lists every active committed pass |
| Empty restore | No queue; pending cleared |
| Construct on load | No — idle Snapshot owner only |

### Open before coding
None for 2.0. User confirmed the load transition.

### Proceed?
YES for 2.0.

---

## Architecture checkpoint (2.0)

1. Ownership change? **NO** — same `Loop` publication owner.
2. State transition change? **YES** — adopt/restore start publication. User confirmed.
3. Unbounded MIDI-interval owner? **NO** — queue only; existing idle Snapshot unit caps.
