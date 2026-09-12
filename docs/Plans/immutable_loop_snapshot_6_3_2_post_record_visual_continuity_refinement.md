# OpenSpec 6.3.2 — Post-record visual continuity during deferred snapshot publication

**Status:** **SHIPPED** (2026-09-12)  
**OpenSpec:** `immutable-loop-snapshot-passes` (inserted before machinery-removal 6.3.3+)  
**Depends on:** [6.3.1 plan](immutable_loop_snapshot_6_3_compatibility_machinery_removal_refinement.md) (**SHIPPED**)  
**Evidence:** HITL `base` PASS [`013759`](../../captures/session_20260912_013759.log); user-visible ~1-bar flash after first record stop

## Decision (2026-09-12)

- **Keep 6.3.1** hold-empty / no C1-a compat restoration.
- **Do not** introduce synchronous session-exit publication (DEC-045 unchanged).
- Treat the post-record display flash as a **separate performance/UX slice**.
- **First fix direction:** hold-last-complete visual / composed handoff during the publication gap — **not** compatibility gather.

## North star

During `isPlaybackSessionExitPublicationGap()`, the display must not **contract** from a complete composed frame (e.g. 32 notes) to a partial gap artifact (e.g. 6 wrap-held notes) while idle snapshot construction runs (~450 ms in [`013759`](../../captures/session_20260912_013759.log)).

Playback may remain hold-empty (6.3.1). This slice is **display / visual-cache continuity only**.

## Architecture gate

| Question | Answer |
|---|---|
| **Owner module** | `Loop::rebuildVisualCacheIdleSlice`, `Loop::applyDeferredVisualCacheWrapAppend`, `DisplayManager::resolveDisplayNotesByDrawSource`, `DisplayWindowUtils` handoff predicates |
| **Primary invariant** | No partial gap rebuild becomes paint authority before published snapshot is current |
| **Ownership change?** | NO |
| **State transition change?** | NO |
| **Execution bound change?** | NO |
| **Behavior-preserving?** | NO for visible display during gap (intended UX fix); YES after snapshot publishes |
| **Reuse** | Extend existing handoff predicates (`shouldHoldOverdubStopHandoffUntilCurrentSnapshot`, `shouldKeepVisualCacheOverSmallerFullLoopSlice`, `shouldRetainRecordStopHandoffSeed`) |
| **Explicit non-goals** | `gatherCommittedEvents*`, `materializeToEventVector`, synchronous `tryPublishSessionExitSnapshot` on stop path |

### Architecture checkpoint

1. **Ownership?** NO  
2. **State transitions?** NO  
3. **Unbounded interval owner?** NO  

## RCA — why 32 → 6 (not merely empty)

Capture [`session_20260912_013759`](../../captures/session_20260912_013759.log), first record → PLAYING window:

| µs | Marker | Meaning |
|---|---|---|
| `261950797` | `STOPPED_RECORDING → PLAYING` | Session-exit publication gap begins (`exit=1`, `root_rev=0`) |
| `261986241` | `DISP PLAYING` **32** notes | Composed RECORDING handoff still paint authority (`visualCache` empty) |
| `261993572` | `VCACHE idle_src=None` | Idle slice: gap hold-empty gather (`sliceNotes.clear()`) |
| `262005981` | `VCACHE append_resume` **6** notes | Wrap-held append on empty gap slice |
| `262014075` | `VCACHE slice_clean` **0** notes | Full-loop slice cleared bars, empty `sliceNotes` |
| `262026657` | `VCACHE append_resume` **6** notes | Deferred wrap append again |
| `262051306` | `DISP PLAYING` **6** notes | Draw-source adopts poisoned clean cache |
| `262404633` | `snapshot_idle publish` `root≠0` | Gap ends (~454 ms after PLAYING) |

### Root cause chain

1. **Record stop** calls `markDisplayCachesStale()` (`TrackCaptureStopCommit.cpp`); `visualCache.notes` was already **empty** during RECORDING (paint used live compose / capture preview, not a full visual cache).

2. **6.3.1 gap path** in `rebuildVisualCacheIdleSlice` clears `sliceNotes` when `isPlaybackSessionExitPublicationGap()` — correct hold-empty for committed gather.

3. **Wrap append still runs** on wrap-edge slices (`appendOverdubPassDisplayNotes` at lines 451–461 in `LoopVisualCache.cpp`) even when the gap gather is empty. That injects **6 wrap-held notes** — not committed loop content.

4. **`slice_keep` does not protect** because `shouldKeepVisualCacheOverSmallerFullLoopSlice` requires `visualCacheNoteCount > 0`; cache was empty before the gap rebuild.

5. **Draw-source abandons handoff:** `resolveDisplayNotesByDrawSource` full rebuild uses `shouldRetainRecordStopHandoffSeed`, which requires `!visualCacheCleanNonempty`. Once the gap rebuild marks a **clean** cache with 6 notes, the 32-note RECORDING handoff is discarded and `gatherCommittedDrawSourceBars` reads the partial cache.

6. **`shouldHoldOverdubStopHandoffUntilCurrentSnapshot`** (in `resolveDisplayNotesCommitted`) would retain the handoff, but **piano-roll paint uses `resolveDisplayNotesByDrawSource`**, which lacks an equivalent session-exit gap gate.

### Conclusion

The flash is a **partial-source transition** (wrap append on empty gap gather + draw-source handoff drop), not proof that hold-empty is wrong for playback. Fix = **continuity policy** during gap, not C1-a restoration.

## Proposed implementation (two coordinated guards)

### A — Visual cache: do not publish partial gap material

In `rebuildVisualCacheIdleSlice` / `rebuildVisualCacheFromPasses` when `isPlaybackSessionExitPublicationGap()`:

- **Skip** `appendOverdubPassDisplayNotes` / deferred wrap append (gap is not a legal completeness source).
- **Early-return / no-op** when the slice would shrink paint authority: empty `sliceNotes` and no snapshot — do not run bar-removal merge that clears then partially refills.
- Optional: stamp `visualCacheDirty = true` during gap so `committedDisplayVisualCacheAuthoritative` stays false until snapshot publishes.

### B — Display draw-source: session-exit gap handoff

Extend `DisplayWindowUtils` + `resolveDisplayNotesByDrawSource` full-rebuild path:

- When `pendingSnapshotSessionExitQueued() && !publishedSnapshotIsCurrent() && preservedHandoffAuthority`, **retain** `liveDisplayNotes` (mirror `shouldHoldOverdubStopHandoffUntilCurrentSnapshot`).
- Do **not** treat a clean but partial gap `visualCache` as authority over the composed handoff.

Predicate name candidate: `shouldHoldSessionExitPublicationGapHandoff` (record + overdub stop).

## Tests

| Test | Owner |
|---|---|
| Native: gap idle slice does not shrink a seeded visual cache | `test_overdub_source_view` or new fixture in `test_loop_take_survival` |
| Native: draw-source retains handoff when session exit queued and cache partial | `test_display_window_utils` + display gather fixture |
| HITL `base` | No regression; optional capture check for `DISP` note count stable across first record → PLAYING gap |

## Verification gates

| Gate | Target |
|---|---|
| `pio test -e native` | PASS |
| RAM1 `teensy41-capture-serial` | ≥ 4000 locals |
| HITL `base` | PASS |
| Manual / capture | No `DISP` 32→6 contraction before `snapshot_idle publish` on record stop |

## Stage renumbering (parent 6.3 plan)

| Stage | Scope |
|---|---|
| **6.3.1** | C1-a hold-empty | **SHIPPED** |
| **6.3.2** | **This slice** — post-record visual continuity | **SHIPPED** |
| **6.3.3** | `passesMaterializedStore_` / `materializeEditViewFromPasses` removal | pending |
| **6.3.4+** | Remaining machinery removal (was 6.3.3–6.3.6) | pending |

## Pre-implementation review

### Ready

- RCA anchored to [`013759`](../../captures/session_20260912_013759.log)
- User decision: no C1-a restore, no sync publish
- Owners and predicates identified; no formal trigger

### Pinned decisions (2026-09-12, pre-coding)

1. **Gap guard scope:** `isPlaybackSessionExitPublicationGap()` only — record **and** overdub stop; no separate paths.
2. **Continuity mechanism:** display-only handoff retention (`shouldHoldSessionExitPublicationGapHandoff`); **do not** adopt composed handoff into `visualCache`.
3. **Visual-cache invariant:** during gap, no cache mutation may make an incomplete gap-derived representation clean/authoritative.
4. **Architecture gate:** existing `isPlaybackSessionExitPublicationGap()` is the sole trigger; no new state or transition.

### Implementation notes

- **A:** `rebuildVisualCacheIdleSlice` / `rebuildVisualCacheFromPasses` / `applyDeferredVisualCacheWrapAppend` no-op during `isPlaybackSessionExitPublicationGap()`; `idle_src=None` via existing `emitVisualCacheIdleGatherPath`. No new `SC_VCACHE` phase strings (each new literal costs ~4 KiB RAM1).
- **B:** `shouldHoldSessionExitPublicationGapHandoff` in draw-source full/slide rebuild paths.

### Verification

| Gate | Status |
|---|---|
| Native `pio test -e native` | **1830/1835** |
| RAM1 `teensy41-capture-serial` | **6880** locals |
| HITL `base` | **PASS** [`020309`](../../captures/session_20260912_020309.log) / report [`020354`](../../captures/host_midi_automation_baseline_20260912_020354.json) |
| Display continuity (record → PLAYING gap) | **PASS** — `DISP` 32→32 in [`020309`](../../captures/session_20260912_020309.log); no partial `append_resume` during gap |

## Deferred — record-start live preview timing (optional UX polish)

**Not in 6.3.2 scope.** Documented for a later optimization slice if the artifact
recurs outside HITL or on hardware.

### Symptom

During the **first bar of RECORDING**, the display can briefly show **~3 very short
note bodies** that vanish when recording stops and the committed frame appears. Stored
MIDI is correct (HITL verifiers pass); this is **live capture-preview paint only**.

### Evidence (HITL compare)

| Capture | First MIDI vs first `DISP RECORDING` | Early paint |
|---|---|---|
| [`020107`](../../captures/session_20260912_020107.log) (glitchy) | First note **16 ms before** first DISP | `take=8` at first paint; **3** window notes at `loopLen=173` (~46 ms after RECORDING) |
| [`020309`](../../captures/session_20260912_020309.log) (nominal) | First DISP **44 ms before** first note | Two empty frames, then 1→2→3 notes as loop grows |

Root cause: when USB MIDI arrives before the first RECORDING display frame, capture
preview paints notes into a **still-growing loop length** — they render compressed and
read as “short blips,” then disappear once the post-stop committed / composed frame
replaces the preview.

### Distinction from 6.3.2

| Issue | Phase | Trigger |
|---|---|---|
| 32→6 after record stop | **6.3.2 (shipped)** | `isPlaybackSessionExitPublicationGap()` — partial gap cache poisons draw-source |
| Short notes at record start | **Deferred** | `ARMED→RECORDING` — MIDI-before-first-paint race on live preview |

### Candidate fix (when prioritized)

Display-only; no compat gather, no snapshot publication change.

1. **Defer capture-preview paint** until after the first `RECORDING` display frame (or
   until `loopLengthTicks` exceeds a minimum bar-quantized threshold).
2. **Or** hold empty on the first N ms of RECORDING when `capture.store` is non-empty
   but loop length is still provisional — same spirit as gap handoff, different trigger.

**Owners (investigation):** `resolveDisplayNotesByDrawSource` capture-preview overlay,
`capturePreview` / live open-tail path, `emitDisplayCaptureSnapshot` during RECORDING.

**Trigger:** existing record-start FSM — no new state; gate on “first RECORDING paint
completed” or loop-length floor.

**Verification:** compare first-500 ms `DISP RECORDING` in HITL capture — no `frame=3`
at `loopLen < TICKS_PER_BAR`; manual record on device without HITL MIDI lead.
