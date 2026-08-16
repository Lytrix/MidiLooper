# NOTE_EDIT UNDO_WARM + commit-recon investigation

**Status:** Active — Layer A snapshot recorded [`145518`](../../captures/session_20260816_145518.log); remaining A open  
**Date:** 2026-08-16  
**Kind:** investigation  
**Trigger:** [`session_20260816_143144.log`](../../captures/session_20260816_143144.log) — STOPPED 4-bar NOTE_EDIT: select/pitch sluggish; exit does not keep edits on display  
**Parent capture context:** grooming Slice 4d is **not** the owner — [`runtime_scheduler_lcr_consumer_grooming_refinement.md`](runtime_scheduler_lcr_consumer_grooming_refinement.md)  
**Authority:** DEC-029 (`NoteEditCurrentState`); [`note_edit_visual_cache_display_unification_refinement.md`](note_edit_visual_cache_display_unification_refinement.md) Stages 8–9 shipped (undo-warm left open)

This file is **investigation only**. Do not patch until a layer is pinned and the architecture checkpoint for that layer is both **NO**.

---

## Debugging boundary

```text
grooming Slice 1–4c     ← trust (device PASS)
grooming Slice 4d       ← idle NOTE_EDIT paint only; not this investigation
        ↓
select → scheduleKindBoundaryUndoWarm → processKindBoundaryUndoWarm
        → buildSessionUndoEntry        ← Layer A (UNDO_WARM latency)
        ↓
pitch/move → GEOM_APPLY,resolve        ← Layer C (measured; not first)
        ↓
macro / exit commit → commitEditAction recon trace
        → post-exit visualCache / DISP ← Layer B (edits not kept)
        ↓
getVisualNotesForSlot ensure           ← Layer D (adjacent; after A/B)
```

Do **not** reopen frozen RC1 driver drift. Do **not** fold this into grooming 4e or Slice 5 hydrate. Do **not** start a GitHub Bug from this file until a layer has a pinned fixture.

---

## What [`143144`](../../captures/session_20260816_143144.log) proves

4-bar loop (`3072` ticks), STOPPED. Open `@ 7.814 s`: `visual_notes=111`, `session_events=229`. `VCACHE,full` on open is `openNoteEditSession` → `rebuildVisualCacheFromPasses` (grooming Slice 5 / hydrate — out of scope).

### Layer A — select `UNDO_WARM`

`EditManager::applySelectNav` calls `scheduleKindBoundaryUndoWarm`. `ControlSurfaceManager` then calls `processKindBoundaryUndoWarm` → `buildSessionUndoEntry`.

| Marker | [`143144`](../../captures/session_20260816_143144.log) | [`145518`](../../captures/session_20260816_145518.log) (after snapshot firmware) | [`234116`](../../captures/session_20260805_234116.log) (2026-08-05) |
|--------|--------|--------|--------|
| Session events | 220–235 | 220–237 | 68 |
| Baseline map | 110 | 110 | 33 |
| `focus_snap` | **68–272 ms** (med 112) | **46–124 ms** (med 91) | 3.1–5.4 ms |
| `warm,complete` | **128–451 ms** (med 203) | **110–312 ms** (med 241) | ~6.0–6.5 ms |
| `baseline_probe` / `edit_rows` | microseconds | microseconds; probe flag **0** on all 28 | <200 µs |
| Unlogged post-probe copies | (inside same function; not split) | **63–188 ms** (med 150); no `overlap_resolve` / `flat_copy` | — |

`focus_snap` in `buildSessionUndoEntry` is `snapshotFocusForSessionUndo` plus `NoteEditCurrentState::clone` when current state is non-empty. `snapshotFocusForSessionUndo` copies the whole `NoteEditFocus` (including the 110-entry `baselineMap`) then trims.

The 2026-08-05 conclusion in [`note_edit_kind_boundary_undo_warm_incremental_refinement.md`](note_edit_kind_boundary_undo_warm_incremental_refinement.md) (“undo warm is not the first-move bottleneck”) used a **68-event** session. It does not apply to this 229-event / 110-note loop. Do not scrap that plan’s Stream B first-move `GEOM_APPLY` work; do not treat its 6–8 ms bound as current.

Mid-session `VCACHE,full` `@ 48.744 s` and `@ 83.710 s` follow select `VCACHE,stale`. That is `getVisualNotesForSlot` → `ensureVisualCacheBuilt` after `invalidateCaches` (Layer D). Not Layer A’s owner, but it adds a hitch on the same select.

### Layer B — exit does not keep edits

First exit `@ 88.786 s`: `NoteEditPass replaced … rows=3 saved=3`. Then `VCACHE,stale` notes=**111**, idle `slice_clean` **112**, `DISP` **112**.

In-session commits already logged:

```
48.603  NoteRange 274 1057–1249 + Pitch 43
48.628  replay_flat: M24@120 missing in recon
48.643  take_only: M24 start=120 end=312
48.660  session_store: M24@120 missing in recon
48.683  loop_materialized: M24@120 missing in recon
```

`logChangeLengthCommitTrace` looks up `focus.commitBaseline.pitch` + `startTick` (here **M24@120**, the pre-edit home). `take_only` is capture passes without the new edit pass. Replay / session_store / loop_materialized are after `saveNoteEditPass`.

**Do not treat `missing in recon` as proof the edit was dropped.** The same split is what a successful pitch/range overlay looks like: take-only still has the old home; replay no longer does.

What is **not** in [`143144`](../../captures/session_20260816_143144.log): a post-exit `DNTE` / visual-cache dump of the **new** span (pitch 43 at 1057, or the last painted mover). Layer B’s first job is that fixture — not a pairing patch.

[`145518`](../../captures/session_20260816_145518.log) exit `@ 53.328 s`: `rows=1 saved=1` after in-session `NoteRange 216–408` + `Pitch 43` (note 280; recon looks up `M50@888`). In-session `DNTE` `43,216,216,192,8`. Post-exit `slice_clean` / `DISP` **114**. Still no post-exit `DNTE` of `43@216`. Same Layer B gap.

Sibling index (open, different fixture): RC8 in [`note_edit_overlap_projection_followup.md`](note_edit_overlap_projection_followup.md) (`M65@369` / `M65@1050` in [`212810`](../../captures/session_20260806_212810.log)). Do not merge fixtures. Do not start RC8 pairing until 143144 shows the committed **new** span absent from post-exit `visualCache.notes`.

### Layer C — pitch/move `GEOM_APPLY,resolve` (not first)

[`143144`](../../captures/session_20260816_143144.log): **209** samples, min 44 ms, median **84 ms**, max 162 ms, 199 over 50 ms.

Unification device [`192007`](../../captures/session_20260813_192007.log) was 16.8–52.3 ms on a 60-note loop. Owner is `NoteGeometryResolver::resolve` / `applyNoteEditChange`. Do not start Layer C while Layer A still runs 128–451 ms on the same fader session.

### Layer D — `getVisualNotesForSlot` ensure (adjacent)

STOPPED short-loop `Track::getVisualNotesForSlot` still calls `ensureVisualCacheBuilt`. Every NOTE_EDIT projection caller uses it. Grooming left this for Slice 5. After Layer A/B are pinned, decide whether select-time `VCACHE,full` is a third commit or stays with hydrate.

---

## Owners (code)

| Layer | Owner | File |
|-------|--------|------|
| A schedule | `EditManager::applySelectNav` → `scheduleKindBoundaryUndoWarm` | [`NoteEditSelection.cpp`](../../src/EditManager/NoteEditSelection.cpp) |
| A run | `EditManager::processKindBoundaryUndoWarm` | [`NoteEditSessionUndo.cpp`](../../src/EditManager/NoteEditSessionUndo.cpp) |
| A cost | `buildSessionUndoEntry` / `snapshotFocusForSessionUndo` | [`NoteEditSessionUndoStack.cpp`](../../src/EditManager/NoteEditSessionUndoStack.cpp) |
| A pump | `ControlSurfaceManager` (every surface tick) | [`ControlSurfaceManager.cpp`](../../src/ControlSurfaceManager.cpp) |
| B commit | `EditManager::commitEditAction` + `logChangeLengthCommitTrace` | [`NoteEditSessionCommit.cpp`](../../src/EditManager/NoteEditSessionCommit.cpp), [`NoteEditCommitColdHelpers.cpp`](../../src/EditManager/NoteEditCommitColdHelpers.cpp) |
| B paint after exit | `resolveDisplayNotesCommitted` + idle `rebuildVisualCacheIdleSlice` | grooming 4b / 4c |
| C | `NoteGeometryResolver::resolve` | [`NoteGeometryResolver.cpp`](../../src/EditManager/NoteGeometryResolver.cpp) |
| D | `Track::getVisualNotesForSlot` | [`Track.h`](../../include/Track.h) |

---

## Investigation order

One layer at a time. Native fixture before firmware.

1. **Layer A pin** — native: `buildSessionUndoEntry` on a 110-note / 229-event snapshot; report `focus_snap` vs clone vs trim. Device: one select must not exceed a bound we set after the pin (do not invent a bound here).

### Layer A pin (native)

`test_undo_warm_143144_focus_snap_copies_full_baseline_then_trims` — 110 notes, 229 events (220 note + 9 CC), empty overlap.

| Step | Result |
|------|--------|
| `NoteEditFocus` copy | source `baselineMap` stays 110 |
| `snapshotFocusForSessionUndo` | **110 → 1** (mover only) |
| `NoteEditCurrentState::clone` | 110 rows |
| `buildSessionUndoEntry` | `hasUndoCurrentState`; entry focus map size 1; current-state size 110 |

Host microseconds (not a device bound): copy 14, snap 19, clone 24, build 75. Device `focus_snap` 68–272 ms is the same copy-then-trim on `ExternalMemoryFirstAllocator` (PSRAM). Host cannot reproduce that latency.

**Pinned owner:** `snapshotFocusForSessionUndo` copies the whole `NoteEditFocus` (110-entry `baselineMap`) then throws 109 entries away; `buildSessionUndoEntry` then clones 110 current-state rows.

**Firmware (landed `c644a5d`):** `snapshotFocusForSessionUndo` copies scalars + `overlapNotes`, then inserts mover and overlap baselines only. It does not copy the full `baselineMap`. Same select → warm → push contract.

**Device [`145518`](../../captures/session_20260816_145518.log):** STOPPED 4-bar, open `visual_notes=113` `session_events=237`. 28 select warms. `focus_snap` **46–124 ms** (med 91) vs [`143144`](../../captures/session_20260816_143144.log) 68–272 (med 112). `warm,complete` **110–312 ms** (med 241) vs 128–451 (med 203). Snapshot slice is **not** a Layer A close — A still dominates select.

Remaining Layer A, same owner `buildSessionUndoEntry`:

1. `focus_snap` still includes `NoteEditCurrentState::clone` of 110 rows (46–124 ms).
2. After `baseline_probe` (flag 0 on every sample), the function always does `resolvedFlat = sessionFlat` and `NoteEditFocus focusCopy = focus`. Those copies are unlogged. [`145518`](../../captures/session_20260816_145518.log) gap after named phases: **63–188 ms** (med 150). No `overlap_resolve` or `flat_copy` line in this capture.

**Next firmware (not started):** skip the session-flat and full-focus copies when `!needsOverlapResolve && !needsBaselineMapDiff`. Do not clone current state in this slice. Same select → warm → push contract.
2. **Layer B pin** — from [`143144`](../../captures/session_20260816_143144.log) saved rows (`NoteRange 1057–1249`, `Pitch 43`, later `Length 1057–1127` / `NoteRange 1128–1320`), state whether post-exit `slice_clean` / `DISP` can show that geometry. If the log cannot, add one commit-trace field for the **new** span (behavior-preserving) or a native replay of the three saved rows. Only then decide RC8 pairing vs stale-cache paint vs idle-slice omit.
3. **Layer C** — only after A no longer dominates select/pitch.
4. **Layer D** — only after A/B; do not globally delete `ensureVisualCacheBuilt`.

---

## Architecture checkpoint (before any firmware)

1. Ownership change? Extending `buildSessionUndoEntry` / `commitEditAction` / existing trace: **NO**. New undo Session or moving commit authority off `NoteEditCurrentState`: **YES** — stop.
2. State transition change? Faster warm with the same select → warm → push contract: **NO**. Deferring NOTE_EDIT open, exit, or macro commit until warm/recon finishes: **YES** — design session.

---

## Does not start

- Grooming 4e last-resort gather
- Grooming Slice 5 / `openNoteEditSession` `rebuildVisualCacheFromPasses`
- Global deletion of `ensureVisualCacheBuilt`
- RC1 driver-drift reopen
- RC8 pairing on the `212810` M65 fixture
- `UndoWarmJob` FSM / cooperative undo scheduler (scrapped in the 2026-08-05 plan)
- Interval reservation, PLAYING drain, `LoadLoopJob` paint work

---

## Related

- [`note_edit_kind_boundary_undo_warm_incremental_refinement.md`](note_edit_kind_boundary_undo_warm_incremental_refinement.md) — 68-event profile; Stream B first-move `GEOM_APPLY` still open
- [`note_edit_overlap_projection_followup.md`](note_edit_overlap_projection_followup.md) § RC8 — different missing-recon fixture
- [`note_edit_visual_cache_display_unification_refinement.md`](note_edit_visual_cache_display_unification_refinement.md) — undo-warm left open after Stages 8–9
- [`note_edit_select_commit_bracket_bugfix.md`](note_edit_select_commit_bracket_bugfix.md) — `M65@609 missing in recon` was wrong `mover_focus` (RC7b), not this layer
