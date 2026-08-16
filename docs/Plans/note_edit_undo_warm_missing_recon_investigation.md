# NOTE_EDIT UNDO_WARM + commit-recon investigation

**Status:** Active — Layer B pinned (deselect drops move, keeps pitch); remaining A open  
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

### Layer B — deselect keeps pitch, drops the move

Pinned from [`145518`](../../captures/session_20260816_145518.log). Same shape in [`143144`](../../captures/session_20260816_143144.log) `@ 48.603 s`.

Note 280: pitch at `888–1080`, move to `312–504`, empty-step deselect, reselect, move to `216–408`, pitch `50→43`, select-away, exit.

| Time | What the log shows |
|------|--------------------|
| 23.402 | Canonical **NoteRange 312–504 + Pitch 45**. `apply_owned=1 first=1` (NoteRange only — last kind was move). |
| 23.445–23.550 | `replay_flat` / `take_only` / `session_store` / `loop_materialized` all still **M24 888–1032**. Take-only is capture-only. Replay matching take-only means those two saved rows did not move or re-pitch that home. |
| 23.636 | Empty-step deselect. `VCACHE,full` 114. |
| 41.275 | In-session `DNTE` **43@216** (live session store still has the second move). |
| 42.905 | Select-away. Canonical **NoteRange 216–408 + Pitch 43**. `apply_owned=1 first=0` (Pitch only). |
| 53.328 | Exit bake **`rows=1 saved=1`**. `NoteEditPassClosed edits=1`. |

`commitEditAction` then discards the live session flat and reloads from capture + saved `EditPass` replay. Display and the exit undo step therefore see that replay, not the in-session `DNTE` at 216.

`recordApplyOwnedEditPassRow(ChangePitch)` erases the mover’s existing `NoteRange` row. That is why the second deselect’s apply-owned set is pitch-only. Apply-owned is **not** persistence authority (`note_edit_singular_commit_pipeline_refinement.md`). Canonical still emitted both rows both times. Do not treat the erase as the Layer B owner.

**Do not treat `M50@888 missing in recon` as proof the move landed.** That lookup is pre-edit `commitBaseline`. The first deselect is the pin: home **still present** on replay after NoteRange+Pitch were saved.

**Next (native, not started):** replay the two canonical rows from 23.402 onto a capture-shaped flat that contains the mover’s store `NoteId`. State whether `applyNoteEditPassSequence` moves that note. Only then decide ID mismatch vs bake vs reload. Do not start RC8 pairing on `212810`.

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
2. **Layer B pin** — done from [`145518`](../../captures/session_20260816_145518.log) deselect. Next: native replay of the 23.402 `NoteRange`+`Pitch` rows onto a capture-shaped flat. Do not patch `ChangePitch` apply-owned erase as the persistence fix.
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
