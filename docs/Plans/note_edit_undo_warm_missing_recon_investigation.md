# NOTE_EDIT UNDO_WARM + commit-recon investigation

**Status:** Active — B1 closed; **B2a device PASS**; **A intermediates PASS**; **C1–C3 device PASS** ([`172608`](../../captures/session_20260816_172608.log) / [`172909`](../../captures/session_20260816_172909.log) / [`173243`](../../captures/session_20260816_173243.log)). **C4** overlay pair-target restrict (device gate open). E: routing works; store-flat identity not logged. RAM1 bank recovered: `snapshotFocusForSessionUndo` → `NOTE_EDIT_MEM` (locals **8608** again).
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
        → buildSessionUndoEntry        ← Layer A (UNDO_WARM; A0 payload audit then minimize)
        ↓
pitch/move → GEOM_APPLY,resolve        ← Layer C (measured; not first)
        ↓
macro / exit commit → commitEditAction recon trace     ← Layer B1 (commit replay)
        → in-session deselect paint / DISP            ← Layer B2 (display vs commit split)
        → post-exit visualCache / DISP                ← Layer B2 / exit bake (partial PASS 161855)
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

The expensive work is **bulk PSRAM copy**, not undo computation (`baseline_probe` / `edit_rows` are microseconds). A therefore asks: **what does a select-boundary `SessionUndoEntry` need to own?** — not “under which probe flags can we skip copies?” See § A0.

### Layer B — commit replay (B1) + in-session display (B2)

Two sub-problems. **B1** is commit/replay authority. **B2** is NOTE_EDIT paint after macro commit on deselect (move snaps back to old span on grid while exit bake is correct).

#### Layer B1 — commit replay (largely closed)

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

**Native replay (landed):** `test_145518_note_range_pitch_replay_moves_when_store_id_matches` and `test_145518_note_range_pitch_replay_leaves_home_when_store_id_differs` in `test_edit_apply`. Same path as `commitEditAction` (`LoopPasses::materializeToEventVector`).

| Store `NoteId` at 888–1032 pitch 24 | Rows target | After replay |
|------------------------------------|-------------|--------------|
| 280 | 280 | Note **moves** to 312–504 pitch 45. Home gone. |
| not 280 | 280 | **M24 888–1032 stays.** No 45@312. |

Apply is not the owner when IDs match. Device 23.445 replay matching take-only is the mismatch case: capture flat has no NoteOn 280 at that home. After first deselect, `DNTE` still `24@888` (`24.892 s`). Live `EditSessionAction` still addressed 280 — session store has 280; capture replay does not.

Not bake (replay already wrong). Not the reload call itself (same `materializeToEventVector`).

**ID source (native landed):** `openNoteEditSession` rematerializes into `editSession.store`, then `assignMissingNoteIdsInStore(editSession.store)` / `assignMissingNoteIds` on event caches. Those IDs live on the session copy. `commitEditAction` rematerializes from capture chunks via `LoopPasses::materializeToEventVector` — chunks never received the assigned id.

`test_145518_open_assigned_note_id_missing_from_pass_rematerialize`: unidentified capture note at 888–1032 pitch 24; open assign yields 280 on the session store; pass rematerialize still has `kInvalidNoteId`; `findNoteOnById(280)` is -1; NoteRange+Pitch targeting 280 leaves **M24@888**.

`assignMissingNoteIdsInStore` does not log. The vector overloads log `assignMissingNoteIds: assigned …` — [`145518`](../../captures/session_20260816_145518.log) has none, which matches assign-on-store-only.

`noteEditFocusApplyDisplayNote` copies `liveSelected.noteId`. That id is 280 because current state was built from the assigned session store. Not a focus-rebuild bug.

**Firmware (this session):** architecture checkpoint both **NO**. `openNoteEditSession` now calls `Loop::assignMissingNoteIdsInCommittedCapturePasses` **before** `rebuildVisualCacheFromPasses` / `rematerializeEditView`. That fills `kInvalidNoteId` in-place on active record/overdub committed chunks via `LoopEventStore::assignMissingNoteIdsToNoteOnsInChunkIds` + `Loop::allocateNoteId`. Chunk ids unchanged (not COW). `LOOP_COLD_MEM` on the open-path walk. Session-store assign stays as a no-op safety net. `commitEditAction` still rematerializes from takes + edits. Do not patch `applyNoteEditPass` or apply-owned `ChangePitch` erase.

Native: `test_145518_assign_on_committed_passes_makes_rematerialize_find_id` — after assign-on-chunks, rematerialize finds 280 and NoteRange+Pitch **moves** the home. The session-only test stays as the hazard pin.

**Device gate (B1):** boot unblocked [`161855`](../../captures/session_20260816_161855.log). Macro commit `@ 128.4 s` → `replay_flat: M24 start=1656` (not 888). Exit `saved=1`, `NoteEditPassClosed`. Chunk assign path **PASS**.

Sibling index (open, different fixture): RC8 in [`note_edit_overlap_projection_followup.md`](note_edit_overlap_projection_followup.md) (`M65@369` / `M65@1050` in [`212810`](../../captures/session_20260806_212810.log)). Do not merge fixtures.

#### Layer B2 — in-session deselect paint (open; **B2a** chosen)

**Not a deselect bug.** Deselect exposes two competing notions of committed geometry in NOTE_EDIT paint. B1 fixed committed identity/replay; B2 fixes committed **display authority**.

[`161855`](../../captures/session_20260816_161855.log): after macro commit, **exit bake** keeps the move; **in-session deselect** still paints the note at the old position (888) on NOTE_EDIT display. LOOP_EDIT after exit shows the baked edit.

```text
commitEditAction
    ↓
takes + EditPasses
    ↓
materializeToEventVector
    ↓
session store                         ← correct committed state (B1)

BUT

NOTE_EDIT paint
    ↓
visualCache                            ← stale committed base
    +
session overlay
    ↓
wrong deselected display             ← B2 owner
```

| Signal | What it shows |
|--------|----------------|
| `DISP` | LOOP_EDIT 114 → NOTE_EDIT `110,114,110,110` → exit 115 |
| `DNTE` | Reverts to 888 on deselect; session store still has move (`DNTE,24,1656` after move) |
| `VCACHE,full` | Part of current latency story; B2b would couple correctness to this path — **not first** |

**B2 design rule (invariant):**

> Once an active committed `EditPass` exists for this session (`editSession.editPassIds` non-empty — see pin below), NOTE_EDIT **committed-base** projection must resolve from the same materialized pass state that `commitEditAction` and LOOP_EDIT use. Only **uncommitted** edits belong in the NOTE_EDIT overlay (session store / live fader geometry).

```text
                 committed base
                       │
          materialize active passes
                       │
                 DisplayNotes
                       │
          ┌────────────┴────────────┐
          │                         │
      LOOP_EDIT               NOTE_EDIT
                                    │
                            + uncommitted
                              session overlay
```

**Owners:** `EditManager::ensureNoteEditDisplayProjectionCachesBuilt` → `projectNoteEditDisplayNotes(committedBase, editAwareMidiEvents(), …)` in [`NoteEditDisplayProjection.cpp`](../../src/EditManager/NoteEditDisplayProjection.cpp) / [`NoteEditFocusDisplayProjection.cpp`](../../src/EditManager/NoteEditFocusDisplayProjection.cpp). Display routing: [`DisplayNoteResolve.cpp`](../../src/DisplayManager/DisplayNoteResolve.cpp).

**Device gate (B2):** after first deselect, NOTE_EDIT grid shows **committed** moved span (1656), not home (888). Exit bake already PASS; do not regress B1 replay trace. Measure after B2a: first deselect, repeated deselect, reselect, uncommitted move, macro commit, exit.

---

### Layer B2 — overdub comparison (shared commit, split display)

NOTE_EDIT and overdub already share the **commit/replay kernel**. The duplicate logic is the **display overlay**, not `saveNoteEditPass` or `LoopPasses::materialize`.

#### Shared primitives (reuse as-is)

| Primitive | Overdub | NOTE_EDIT |
|-----------|---------|-----------|
| Persist geometry | `sealPendingNoteChangesToEditPasses` → `saveNoteEditPass` (companion index) | `commitEditAction` → `saveNoteEditPass` (session index) |
| Replay committed state | `LoopPasses::materializeToEventVector` / `rematerializeEditView` | Same in `commitEditAction` session-store reload |
| Disable committed edits | `setEditPassState(Disabled)` on **U:** (`OverdubPassAdded` companions) | Same on **U:** `NoteEditPassClosed`; `materializeExcludingEditPassIds` on **E:** restore |
| Committed display materialize | `gatherCommittedEvents` → `rebuildEffectiveEventStore` (includes active `editPasses`) | Same path backs LOOP_EDIT / post-exit paint |

`commitEditAction` after macro commit already matches overdub’s “seal edit rows, then materialize” contract: session store reload from takes + active `editPasses` via `materializeToEventVector` ([`NoteEditSessionCommit.cpp`](../../src/EditManager/NoteEditSessionCommit.cpp)).

#### **E:** undo — same routing, different stacks (do not merge)

| | Overdub **E:** (while OVERDUBBING) | NOTE_EDIT **E:** |
|--|-----------------------------------|------------------|
| Stack owner | `Loop` overdub session (`overdubSessionPassIds_` + companion edit-pass ids) | `NoteEditSessionUndoStack` / `SessionUndoEntry` |
| Undo unit | Sealed wrap `PassId` + companion ids, or live capture snapshot | Pre-commit `editRows` or `currentState` clone + focus/selection |
| Restore | Toggle pass state (`undoOverdubSession`) — no flat replay | `applySessionEditRows` = `materializeExcludingEditPassIds` + `applyNoteEditPassSequence`, or `restoreSessionStoreFromCurrentState` |

Docs: [`loop_content_resolution_overdub_state_evaluation_refinement.md`](loop_content_resolution_overdub_state_evaluation_refinement.md) (DEC-038), [`LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) § Routing. Reuse **session-gated undo routing** only — not `NoteEditSessionUndoStack` payload on overdub.

**Bridge:** in-edit overdub stop (`foldLiveCaptureIntoNoteEditSession`) pushes NOTE_EDIT **E:** with `redoEditRows` — NOTE_EDIT consuming capture, not overdub E:.

#### Where NOTE_EDIT diverges from overdub (B2 owner)

**Overdub / LOOP_EDIT (single layer):** committed view = materialized passes; `appendOverdubPassDisplayNotes` applies active edit passes to overdub chunks before `reconstructDisplayNotes`; `notifyCommittedContentChanged` → idle/full visual cache rebuild includes edit passes.

**NOTE_EDIT in-session (two layers):**

1. **Base:** `track.getVisualNotesForSlot` → `visualCache` (may lag; keyed on `visualCache.revision`).
2. **Overlay:** `projectNoteEditDisplayNotes(committedBase, editAwareMidiEvents(), focus, currentState)`.

During NOTE_EDIT, `Track::invalidateCaches` bumps session preview only — it does **not** rebuild loop visual cache ([`TrackMidiEventRouting.cpp`](../../src/Track/TrackMidiEventRouting.cpp)). `saveNoteEditPass` still marks `visualCacheDirty` via `notifyCommittedContentChanged`, but paint treats `visualCache` as committed base and re-derives geometry through focus/participant rules. On deselect, `rebuildNoteEditFocusAtSelect(-1)` clears `focus.active` / `movingNoteId`, so projection falls back to participant/`currentState` logic instead of the materialized committed span overdub uses.

**Conclusion:** B1 commit path is overdub-aligned. B2 repairs the paint split — align **committed base** with materialized passes; do not add deselect branches in `projectNoteEditDisplayNotes` (B2c).

#### B2 implementation — **B2a** (chosen)

| Option | Verdict |
|--------|---------|
| **B2a** — materialized committed base when active committed edit passes exist | **Implement first** |
| **B2b** — eager `VCACHE,full` rebuild on every macro commit | Fallback only if B2a materialization cost is unacceptable on device |
| **B2c** — more projection/focus branches | **Avoid** |

**B2a logic (narrow):**

```text
if (session has active committed edit passes — editPassIds pin)
    committedBase = reconstructDisplayNotes(materialize active passes)
else
    committedBase = existing path (visualCache / getVisualNotesForSlot)
overlay = uncommitted session state only
```

**`editPassIds` pin (native fixture prerequisite):** Before firmware, verify `editSession.editPassIds` means **active committed edit passes that must appear in the committed base** (ids appended by `commitEditAction` / `saveNoteEditPass` for this session), not merely “this session has ever touched edit passes.” If the list can retain disabled or superseded ids, the B2a gate must use **active** committed passes (e.g. ids still `EditPassState::Active` in `loop.passes.editPasses`), not raw list non-emptiness alone.

**Why B2a over B2b:** B2b turns “NOTE_EDIT needs correct committed representation” into “every macro commit synchronously rebuilds global visual cache” — couples correctness to expensive cache machinery and risks Layer D / Slice 5. Recent architecture is **content authority → derived representation → consumer**, not force every consumer to rebuild. B2a follows that. `VCACHE,full` is already part of the latency story ([`145518`](../../captures/session_20260816_145518.log) on select).

**Materialization cost constraint:** B2a must not move the expensive work from `VCACHE,full` to every projection rebuild without reuse. Before firmware, answer: does `ensureNoteEditDisplayProjectionCachesBuilt` materialize the full loop on every cache miss, or can the committed-base result be **reused until committed revision changes** (`loop.playbackRevision`, `visualCache.revision`, and/or committed edit-pass identity)? Prefer extending existing projection cache fingerprint / revision machinery — **do not** add a second persistent display store for B2 alone.

**B2a scope — do not combine with B1 or A:**

- Do **not** extract `reloadNoteEditSessionStoreFromPasses` in the B2 slice (dedupe later if wanted).
- Do **not** touch unless B2 fixture proves involvement: `applyNoteEditPass`, apply-owned `ChangePitch` erase, `NoteEditSessionUndoStack`, `rebuildNoteEditFocusAtSelect`, global `ensureVisualCacheBuilt`, Layer D.

**Native fixture (B2 — before firmware):** `test_edit_apply` or `test_note_edit_current_state`. Critical assertion:

```text
committedBase == reconstructDisplayNotes(materialize(active edit passes))
```

—not only `1656 visible` (that could pass via another projection special case).

| # | Scenario | Expect |
|---|----------|--------|
| 1 | Open → no committed edit pass | Existing projection path (`visualCache` base) |
| 2 | Commit NoteRange → `editPassIds` active | Materialized committed base |
| 3 | Deselect | Moved span visible |
| 4 | Uncommitted move (no new macro commit) | Session overlay wins |
| 5 | Deselect again | Committed geometry still correct |
| 6 | Exit | Same geometry as in-session committed view |
| 7 | **U:** undo committed pass | Pass disabled → old geometry returns |

**Hard don'ts (unchanged):** do not patch `applyNoteEditPass` or apply-owned `ChangePitch` erase for B2; do not reuse overdub session pass-id stack for NOTE_EDIT E:; do not start grooming 4e / Slice 5 hydrate in this slice.

### Layer C — pitch/move `GEOM_APPLY,resolve` (C1–C3 device PASS)

A no longer hides this. Outer `GEOM_APPLY,resolve` wraps **all** of `applyNoteEditChange`, including `finalReconstructAndSelect` (`selectableDisplayNotesAtEditSelect` → projection). `GEOM_APPLY,focus` is microseconds; `GEOM_APPLY,undo` is microseconds except first-kind warm.

| Capture | resolve n | min / med / max | notes | empty-overlap pitch |
|---------|-----------|-----------------|-------|---------------------|
| [`192007`](../../captures/session_20260813_192007.log) | 161 | 17 / **31** / 52 ms | ~56 baseline | mixed |
| [`143144`](../../captures/session_20260816_143144.log) | 209 | 44 / **84** / 162 ms | 110 | `candidates=0` still applies |
| [`170942`](../../captures/session_20260816_170942.log) | 59 | 49 / **99** / 131 ms | 110 | `candidates=0` still applies |
| [`171228`](../../captures/session_20260816_171228.log) | 53 | 55 / **137** / 258 ms | 109 | move `candidates=13`; pitch `candidates=0` |
| [`171822`](../../captures/session_20260816_171822.log) | 12 | 49 / **57** / 60 ms | 110 | all `candidates=0` |

`VCACHE,full` count is far below resolve count (3 vs 59 on 170942). Empty-overlap pitch still pays tens of ms on the 110-note loop.

**C1 device PASS [`172608`](../../captures/session_20260816_172608.log):** 64 resolves, every phase present. `NoteEditKind`: Move=3, Pitch=4. `analyze` owns resolve (Move 76.7% med, Pitch 85.9% med). Do **not** skip setup / apply / reconstruct — they are not the cost. Do **not** skip `analyze` — empty-overlap pitch still pays it.

| Kind | n | setup med | analyze med | apply med | reconstruct med | resolve med |
|------|---|-----------|-------------|-----------|-----------------|-------------|
| Move (3) | 22 | 478 µs | **29.7 ms** | 778 µs | 3.9 ms | 38.8 ms |
| Pitch (4) | 42 | 647 µs | **49.3 ms** | 799 µs | 2.0 ms | 57.5 ms |

Pitch here is empty-overlap: `scope=0`, `candidates=0`, `interactions` med 0. First Pitch `GEOM_APPLY,undo` is 36 ms (kind-boundary warm); later undos are 2 µs. Crowded-lane Move (`scope=12`, `candidates=12`) analyze 73–86 ms. No `VCACHE,full` in this file.

**C2 device PASS [`172909`](../../captures/session_20260816_172909.log):** 57 resolves, every sub-phase present. `overlay` owns `analyze` (Move 99.6% med, Pitch 99.7% med). `pairs` / `interact` / `constrain` / `build` are microseconds. Do **not** skip those four. Do **not** skip `overlay` when pairs are non-empty.

| Kind | n | pairs | **overlay** | interact | constrain | build | analyze | resolve |
|------|---|-------|-------------|----------|-----------|-------|---------|---------|
| Move (3) | 22 | 8 µs | **65.2 ms** | 26 µs | 103 µs | 36 µs | 65.5 ms | 78.5 ms |
| Pitch (4) | 35 | 1 µs | **74.8 ms** | 13 µs | 105 µs | 9 µs | 75.0 ms | 84.0 ms |

Pitch `pairs=0` on 32/35 ticks: overlay still **74.8 ms**. Those ticks never `find()` the overlaid map (C2a). First Pitch `undo` 49 ms (kind-boundary warm).

**C2a consumer audit of `analysisBaseline`**

| Consumer | What it reads | Empty `eligiblePairs` |
|----------|---------------|------------------------|
| `analyzeEditSessionInteractions` | `findBaselineSpan(pair.targetNoteId)` only | No reads |
| `resolveAllConstrainedGeometry` | `projected.find(target)` only when that target has incoming interactions | Leave-restore uses **storage** baseline |
| `determineConstrainedGeometryTargetNoteIds` | Walks **storage** baseline | No projected reads |
| `appendOverlapTargetActions` | `projected.find(constrained.noteId)` for non-leave-restore rows | Empty constrained → no reads |
| `appendCausingNoteActions` | Does not take the map | — |

**Best skip:** `overlayAnalysisBaselineForSessionMovedOverlaps` when `eligiblePairs.empty()`; pass storage `baselineMap`. **Required:** overlay when pairs are non-empty (Move in this file always had 4–12 pairs). Do not change the overlay algorithm this slice.

**C3 device PASS [`173243`](../../captures/session_20260816_173243.log):** 60 resolves. Contract holds: `pairs=0` overlay extra1 `0` (38 ticks, overlay 0–2 µs); `pairs>0` extra1 `1` (22 ticks). Zero violations.

| Path | n | overlay | analyze | reconstruct | resolve |
|------|---|---------|---------|-------------|---------|
| Pitch skip (`pairs=0`) | 32 | **0–1 µs** | 138 µs | 2.8 ms | **10.1 ms** (was 84.0) |
| Pitch ran (`pairs>0`) | 5 | 41.3 ms | 41+ ms | — | 51.6 ms |
| Move skip (`pairs=0`) | 6 | **0–2 µs** | 141 µs | 6.1 ms | **13.0 ms** |
| Move ran (`pairs=12`) | 17 | **123.0 ms** | 123.0 ms | 8.2 ms | **142.4 ms** |

Empty-pair instrumented sum (setup+analyze+apply+reconstruct) med **4.5 ms**; outer `resolve` med **10.6 ms** — **5.5 ms** still inside `applyNoteEditChange` but outside those four timers. Two `VCACHE,full` (open + later), not per tick.

**C4 (this slice):** `overlayAnalysisBaselineForSessionMovedOverlaps` takes optional `pairTargetNoteIds`. Resolve passes unique `pair.targetNoteId`s. Null list keeps the full-map path for existing fixtures. Do not skip overlay when pairs are non-empty.

Device gate: overlap move `GEOM_APPLY,phase,overlay` extra1 `1`, extra0 ≈ pair-target count (12 on 173243 crowded lane), not 108. Empty-pair skip unchanged (extra1 `0`).

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
| B1 commit | `commitEditAction` rematerialize; `assignMissingNoteIdsInCommittedCapturePasses` at open | [`NoteEditSessionCommit.cpp`](../../src/EditManager/NoteEditSessionCommit.cpp), [`NoteEditSessionLifecycle.cpp`](../../src/EditManager/NoteEditSessionLifecycle.cpp) |
| B2 paint (in-session) | `ensureNoteEditDisplayProjectionCachesBuilt` → `projectNoteEditDisplayNotes` | [`NoteEditDisplayProjection.cpp`](../../src/EditManager/NoteEditDisplayProjection.cpp), [`NoteEditFocusDisplayProjection.cpp`](../../src/EditManager/NoteEditFocusDisplayProjection.cpp) |
| B2 display routing | `DisplayManager::resolveDisplayNotes` (NOTE_EDIT branch) | [`DisplayNoteResolve.cpp`](../../src/DisplayManager/DisplayNoteResolve.cpp) |
| B2 overdub reference | `gatherCommittedEvents` / `rebuildVisualCacheFromPasses` / `appendOverdubPassDisplayNotes` | [`LoopMaterialization.cpp`](../../src/Loop/LoopMaterialization.cpp), [`LoopVisualCache.cpp`](../../src/Loop/LoopVisualCache.cpp) |
| B paint after exit | `resolveDisplayNotesCommitted` + idle `rebuildVisualCacheIdleSlice` | grooming 4b / 4c |
| C | `NoteGeometryResolver::resolve` | [`NoteGeometryResolver.cpp`](../../src/EditManager/NoteGeometryResolver.cpp) |
| D | `Track::getVisualNotesForSlot` | [`Track.h`](../../include/Track.h) |

---

## Investigation order

One layer at a time. Native fixture before firmware. **Do not optimize Layer C while Layer A still dominates select (300–450 ms warm).**

```text
A0 — semantic payload audit (filled; split outcome)
  ↓
A  — drop unused build intermediates only (not currentState clone)
  ↓
B2 — native fixture (projection invariant + editPassIds pin)
  ↓
B2a — firmware (materialized committed base + revision reuse)
  ↓
B2 — device measure (B2b only if materialization cost fails gate)
  ↓
C  — geometry resolve (after A no longer pollutes measurement)
  ↓
D  — getVisualNotesForSlot / VCACHE (after B2; not in parallel)
```

### 1. Layer A — payload minimization (A0 filled; intermediates-only firmware next)

**Native pin (done):** `test_undo_warm_143144_focus_snap_copies_full_baseline_then_trims` — see table below.

**Remaining A cost ([`145518`](../../captures/session_20260816_145518.log)):**

| Component | Device |
|-----------|--------|
| `NoteEditCurrentState::clone` in `focus_snap` | 46–124 ms |
| Unlogged `sessionFlat` + full `focus` copies after `baseline_probe` flag 0 | 63–188 ms (med 150) |

**Do not treat the probe-flag skip as the architectural solution.** `if (!needsOverlapResolve && !needsBaselineMapDiff)` is an optimization derived from today’s implementation, not a statement of what `SessionUndoEntry` must represent. `buildSessionUndoEntry` currently snapshots broadly, then discovers most of it was unused — the same “materialize because the older API expected it” pattern this repo has been removing elsewhere.

Keep the experiment. Rename the intent:

> **A — derive the minimum undo payload for the boundary**, then make copies a consequence of that payload.

```text
buildSessionUndoEntry
        │
        ├── determine undo requirements   ← before expensive copies
        │
        ├── simple boundary
        │      └── compact/minimal payload
        │
        └── complex boundary
               └── richer payload
```

`baseline_probe` already sits near this split. It must run **before** `resolvedFlat = sessionFlat` and `focusCopy = focus`.

**Do not redesign the undo system.** New undo Session, lazy snapshot object, COW undo state, or another ownership layer: architecture checkpoint **YES — stop**. Do not move commit authority off `NoteEditCurrentState`. `SessionUndoEntry` stays; A only makes its stored fields match the semantic payload (`snapshotFocusForSessionUndo` 110 → 1 already started this; `buildSessionUndoEntry` then undoes the economy by cloning 110 current-state rows and making more broad copies).

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

**Firmware (landed `c644a5d`):** `snapshotFocusForSessionUndo` trims baseline map — not a Layer A close.

#### A0 — consumer contract (**filled** 2026-08-16)

Consumers: `EditManager::restoreSessionUndoEntry` / `sessionUndo` ([`NoteEditSessionUndo.cpp`](../../src/EditManager/NoteEditSessionUndo.cpp)); `applySessionUndoEntry` / `restoreSessionStoreFromCurrentState` ([`NoteEditSessionUndoStack.cpp`](../../src/EditManager/NoteEditSessionUndoStack.cpp)). `applySelectNav` after undo does **not** rebuild focus.

`openNoteEditSession` always builds `noteEditCurrentState` from the session store ([`NoteEditSessionLifecycle.cpp`](../../src/EditManager/NoteEditSessionLifecycle.cpp)). After a normal open, `hasUndoCurrentState` is true for every warm entry (`test_undo_warm_143144_focus_snap_copies_full_baseline_then_trims`).

| Field | Copied today? | Consumed on simple select undo? |
|-------|---------------|----------------------------------|
| `selection` | yes | **Yes** — `restoreSessionUndoEntry` assigns `sessionState.selection` |
| `focus` (trimmed snapshot) | yes (`snapshotFocusForSessionUndo`) | **Yes** — assigned to `editSession.focus`; `applyUndoRedoLanding` reads `active`, `movingNoteId`, `last.startTick`. Snapshot stays live (nav does not rebuild focus) |
| `focus.baselineMap` (full 110) | no after `c644a5d` (mover + overlap only) | **No** — restore never re-reads the discarded 109. Next geometry uses the trimmed map + later `overlayUneditedBaselineMapFromDisplayNotes` on rebuild |
| `focus.overlapNotes` | yes (in snapshot) | **Yes if non-empty** — assigned with focus. Simple select (`baseline_probe` flag 0, empty overlap): empty, cheap |
| `undoCurrentState` (110-row clone) | yes when current state non-empty | **Yes, in full** — `assignFrom` then `refreshNoteEditSessionProjection` → `projectToSessionStore` walks **every** row. Today's restore has no delta path |
| `editRows` | yes (`buildPreCommitEditPasses`) | **No** when `hasUndoCurrentState` — restore skips `applySessionUndoEntry`. Firmware `sessionUndo` only calls apply when current state is empty. Redo fills `redoEditRows` from a **new** `buildSessionUndoEntry` of live state, not the stored undo `editRows`. On simple select, `sessionStoreEvents` is nullptr so overlap-diff rows are not even built; mover rows are empty when `last == commitBaseline` |
| `editPassIdsAtPush` | yes | **Yes** — `sessionUndo` disables pass ids committed after the push |
| `sessionFlat` / `resolvedFlat` | working copy after probe | **No** on simple select. Probe **reads** `sessionFlat` in place (`noteEditFocusHasPendingBaselineMapDiff`). `resolvedFlat = sessionFlat` is unread when both flags are false. `buildPreCommitEditPasses` only uses the flat when `sessionStoreEvents != nullptr` (baseline-diff case) |
| full `focusCopy` after probe | working copy | **No** on simple select. `buildPreCommitEditPasses` takes `const NoteEditFocus&` and does not mutate it. Overlap resolve is the only writer of `focusCopy` |
| redo payload | not at warm build | n/a — filled at undo time from a fresh build |

**Split outcome (do not collapse to one verdict):**

| Piece | Outcome | Meaning |
|-------|---------|---------|
| Post-probe `resolvedFlat` + `focusCopy` | **Best** | Not payload. Drop because restore never sees them. The 63–188 ms gap is this pair |
| `editRows` on a current-state entry | **Best** | Not consumed on restore/redo of that entry. Building them is already cheap (`edit_rows` microseconds); skipping the call is optional hygiene |
| Trimmed `focus` + `selection` + `editPassIdsAtPush` | required | Keep |
| `undoCurrentState` 110-row clone | **Worst** given current restore | `assignFrom` + full `projectToSessionStore` **is** the undo of the session store. The 46–124 ms `focus_snap` clone is real. Do **not** skip it behind a probe flag. A smaller current-state payload would need a delta restore — new restore contract, not A firmware (checkpoint YES if that becomes a new representation) |

**A firmware authorized by A0:** in `buildSessionUndoEntry`, after `baseline_probe`, do not copy `sessionFlat` into `resolvedFlat` and do not copy full `focus` when overlap resolve and baseline-map diff are both false. Pass the original `focus` into `buildPreCommitEditPasses`. **Do not** skip `currentState->clone()`. Same select → warm → push contract. No new stack.

```text
                    buildSessionUndoEntry
                            │
                    baseline_probe
                            │
              ┌─────────────┴─────────────┐
              │                           │
       simple boundary              complex boundary
              │                           │
       no extra copies             copies required
              │
              └──────────────┐
                             ↓
                  undoCurrentState.clone()
                             ↓
                       SessionUndoEntry
```

- **`currentState` = semantic undo payload** — do not optimize `clone()` in this slice
- **`resolvedFlat` / `focusCopy` = incidental construction copies** — drop when unused

**Not authorized:** treating `!needsOverlapResolve && !needsBaselineMapDiff` as permission to omit `undoCurrentState`. That flag does not change restore. Delta restore is a different undo representation (design session).

**A native / device gate:**

| Check | Pass |
|-------|------|
| Simple select → warm → push | Functionally identical (`hasUndoCurrentState`, trimmed focus, same `editPassIdsAtPush`) |
| Undo | Restores identical session-store geometry (`assignFrom` + `projectToSessionStore`) |
| Redo | Restores identical session-store geometry from a **fresh** `buildSessionUndoEntry` of live state — must not depend on stored `editRows` |
| Simple path copies | No full `sessionFlat` copy and no full `NoteEditFocus` copy when both probe flags are false (`UNDO_WARM,phase,intermediates,…,0`) |
| Payload | `currentState` clone remains |

Expected measurement: post-probe 63–188 ms should collapse. Remaining floor is the legitimate `focus_snap` clone (46–124 ms), not more guesswork.

**Device [`165506`](../../captures/session_20260816_165506.log) — A intermediates PASS (clone floor remains):**

STOPPED 4-bar, open `visual_notes=115` `session_events=241`. 49 simple-select warms. `baseline_probe` flag **0** on all 49. `overlap_resolve` **0**. `flat_copy` **0**. `intermediates` **0–1 µs** (flag 0).

| Marker | [`145518`](../../captures/session_20260816_145518.log) (before) | [`165506`](../../captures/session_20260816_165506.log) (A intermediates) |
|--------|--------|--------|
| `focus_snap` | 46–124 ms (med 91) | **37–203 ms (med 78)** |
| post-probe copies | **63–188 ms (med 150)** | **gone** (`intermediates` 0 µs) |
| `warm,complete` | 110–312 ms (med 241) | **37–204 ms (med 78)** |
| `build,total` vs `focus_snap` | build ≫ snap | **build ≈ snap** |

Warm is now the `undoCurrentState` clone. That matches A0 Worst for current-state. Do **not** skip `clone()` from this capture. Undo/redo store geometry was **not** exercised here (`EditSession undo`/`redo` count 0) — still required before calling A closed.

B2 deselect paint is a different gate; this capture’s NOTE_EDIT `DISP` `110,115,110,110` at 21.5 s is the same in-session shape as [`161855`](../../captures/session_20260816_161855.log).

**Device [`165853`](../../captures/session_20260816_165853.log) — A undo/redo exercised; store-flat identity not logged:**

Open `@ 235.359` `visual_notes=117` `session_events=245`. 24 warms. Simple path still `intermediates` 0 µs. **One complex warm** `@ 248.920`: `overlap_resolve` **123.8 ms**, `flat_copy` **7 µs** — copies run only when the probe requires them.

`GEOM_APPLY,undo` here is `beginGeometryMutation` snapshot (kind 3 = Pitch), not E: restore.

| Time | What the log shows |
|------|--------------------|
| 240.70 | Live `DNTE,24,744` then `24,648` (move/pitch of M24) |
| 243.670 | Macro commit `editId=81`. `replay_flat` / `session_store` / `loop_materialized` all **M24 888–1032** (home still present) |
| 246.77 | `DNTE,24,888` — in-session paint back at home (B2) |
| 250.152 | Second commit `editId=82` M94 864–912 |
| 258.429 | **`MIDI: EditSession undo`**. `DISP` `110,117,110,110`. `DNTE,24,888` |
| 259.684 | **`MIDI: EditSession redo`**. `DISP` `109,117,109,109`. `DNTE,93,912` |
| 261.991 | Exit bake `saved=2`. LOOP_EDIT `DISP` 116 |

E: undo/redo **ran**. This log has no session-store flatten before/after those two lines, so it does **not** prove “identical session-store geometry.” Paint after undo is the pre-edit home; redo highlights a different `DNTE`. Do not close A on store identity from this file.

B2 still open: live 24@648 then commit rematerialize still 888.

### 2. Layer B1 — closed

Commit replay + chunk NoteId assign: [`161855`](../../captures/session_20260816_161855.log) replay PASS. Do not patch `applyNoteEditPass` or apply-owned `ChangePitch` erase.

### 3. Layer B2 — native fixture (landed)

`test_edit_apply`: `test_b2_edit_pass_ids_mean_active_committed_not_ever_created`, `test_b2_committed_base_matches_materialize_active_edit_passes` (161855 home 888 → 1656; seven scenarios), `test_b2_stale_visual_cache_overwrites_synced_current_state_spans`.

**`editPassIds` pin:** `commitEditAction` / `saveNoteEditPass` append Active ids. `replaceNoteEditPass` disables stale and returns new Active ids. `sessionUndo` assigns `editPassIds = editPassIdsAtPush` after disable — the session list does **not** retain Disabled ids. `loop.passes.editPasses` still holds Disabled rows. Gate must not mean “ever created.”

**Projection pin:** stale visualCache **with** mover NoteId still overlays `currentState` (1656). Stale visualCache **without** NoteId (`kInvalidNoteId`, 161117) keeps home 888 on deselect. Materialized base carries id + move.

**Reselect hazard (not B2a firmware):** `ensureVisibleRowsForDisplayNotes(stale home)` overwrites sealed equal spans back to 888. Owner stays `ensureCurrentStateVisibleRowsFromVisualCache` — do not fold into this slice.

### 4. Layer B2a — firmware (landed)

Extend `ensureNoteEditDisplayProjectionCachesBuilt` only. Gate is `loop.visualCacheDirty` (set by `saveNoteEditPass` / `disableEditPasses` / `enableEditPasses`), **not** raw `editPassIds` non-empty — after E: undo the list is empty and dirty is still set. While dirty: `materializeToEventVector` + `reconstructDisplayNotes` (do not call `getVisualNotesForSlot` — that is B2b `VCACHE,full`). While clean: existing visualCache path. Overlay unchanged. Reuse existing projection fingerprint + `playbackRevision`.

### 5. Layer B2 — device gate

145518 / 161855 gesture; compare B2a latency vs B2b only if materialization cost fails.

**Device [`170942`](../../captures/session_20260816_170942.log) — B1 move commit PASS; B2 mover deselect not proven:**

Open `@ 16.725` `visual_notes=111` `session_events=229`. NOTE_EDIT `DISP` `110,111,110,110`.

| Time | What the log shows |
|------|--------------------|
| 26.434 | Pitch-only commit `targetNoteId=256`. `replay_flat` **M24 888–1032** (home stays — no NoteRange) |
| 26.656 | `DNTE,24,888` after that pitch commit |
| 31.721–32.694 | Live move `24@840` → `744` → **`792`** |
| 34.7–39.3 | Pitch at 792; last live `DNTE,42,792` `@ 39.331` |
| 40.866 | Canonical **Delete 256 + NoteRange 280 792–936 + Pitch 42**. `replay_flat` / `session_store` / `loop_materialized` **M24@888 missing**. `take_only` still 888 |
| 41.118 | `VCACHE,full` notes **110** (was 111) |
| 41.532 / 42.091 | Select-away `DNTE` **12@480** / **86@480** — no mover `DNTE` after this commit |
| 44.081 | Exit bake `saved=3`. LOOP_EDIT `DISP` `110,110,110,110` |

No `DNTE,24,888` after the NoteRange commit. Also no post-commit `DNTE` of the mover at 792. Do **not** call B2 PASS from this file — the gate needs a deselect/reselect that paints the mover. Do not start B2b from the post-commit `VCACHE,full` (STOPPED 4-bar `getVisualNotesForSlot` on reselect / `ensureCurrentStateVisibleRowsFromVisualCache`).

**Device [`171228`](../../captures/session_20260816_171228.log) — B2 mover reselect PASS:**

Open `@ 161.805` `visual_notes=110` `session_events=231`. Live `60@832` → move/pitch to **`43@736`**.

| Time | What the log shows |
|------|--------------------|
| 175.022 | NoteRange **550** `736–912` + Pitch 43. `replay_flat` still **M60 832–1008** (home leftover / lookup — not the B2 paint question) |
| 176.859–177.865 | Select-away `82@672` / `71@704` / `81@720` |
| 178.279 / 179.090 / 180.329 | Reselect mover **`DNTE,43,736`** — committed span, not `60@832` |
| 183.851 | Uncommitted `43@256`; `@ 184.801` canonical=0 (no new pass) |
| 188.077 | Paint back **`43@736`** |
| 194.246 | Second NoteRange 550 `256–432`. `M43@736 missing` |
| 222.696 | Third NoteRange 526 `688–864`. `replay_flat` **M60@832 missing**; `take_only` still 832 |
| 231.402 | **E:** undo. `DNTE,60,832` |
| 232.969 | **E:** redo. `DNTE,86,480` |
| 235.807 | Exit bake `saved=2`. LOOP_EDIT `DISP` `114,114,114,114` |

B2 gate (reselect mover after first deselect): **PASS**. Do not reopen B1 from the first-commit `M60@832` leftover. Do not start B2b / Layer C/D from this file. E: undo/redo ran; no store-flat before/after — A identity still open.

**Device [`171822`](../../captures/session_20260816_171822.log) — E: routing works; no DNTE/store-flat:**

Open `@ 528.009` `visual_notes=114` `session_events=235`. Pitch 526 `688–864` `58→47`. Commit `@ 537.354` Pitch 47; `replay_flat` `M60@688 missing`.

| Time | What the log shows |
|------|--------------------|
| 542.587 | **`MIDI: EditSession undo`**. Selection `19 -> 26` |
| 544.371 | Second double-press: **`No session undo available (entries=0)`** |
| 545.998 | **`MIDI: EditSession redo`**. Selection `26 -> 19` |

This file has **no** `DNTE` lines and no session-store flatten around undo/redo. Routing and selection restore are in the log. Geometry identity is not. Do not close A on store-flat from this capture.

### 6. Layer C — C4 pair-target overlay (device gate next)

C3 [`173243`](../../captures/session_20260816_173243.log) skip contract holds. C4 restricts overlay to unique pair targets. Capture must show overlay extra0 ≈ pair count on crowded-lane Move, extra1 `1`. Empty-pair extra1 stays `0`. Do not start Layer D.

### 7. Layer D — after B2

`getVisualNotesForSlot` / `ensureVisualCacheBuilt`; do not globally delete. B2b overlaps D — use only as B2a fallback.

---

## Architecture checkpoint (before any firmware)

1. Ownership change? Extending `buildSessionUndoEntry` field population / existing restore: **NO**. New undo Session, lazy/COW snapshot object, or moving commit authority off `NoteEditCurrentState`: **YES** — stop.
2. State transition change? Same select → warm → push contract with a smaller stored payload: **NO**. Deferring NOTE_EDIT open, exit, or macro commit until warm/recon finishes: **YES** — design session.

### Layer A firmware checkpoint (after A0 only)

1. Ownership change? **NO** if `SessionUndoEntry` keeps the same fields and restore still assigns `undoCurrentState` / trimmed `focus` / `selection` / `editPassIdsAtPush`. **YES** if current-state becomes a delta or a new undo representation — stop.
2. State transition change? **NO** if undo of a simple select still restores the same store, focus, and selection. **YES** if simple-select undo omits `undoCurrentState`.

Reuse: **YES** — after `baseline_probe`, skip unused `resolvedFlat` / `focusCopy` because A0 proved they are not payload. **NO** — use that flag to omit `undoCurrentState`.

### Layer B firmware checkpoint (B1 — landed)

1. Ownership change? **NO** — `Loop` already owns `allocateNoteId` and `assignMissingNoteIds*`. Extending assign onto the committed capture chunks `commitEditAction` already rematerializes. Not applying commit onto the session store. Not a new NoteId owner.
2. State transition change? **NO** — still assign at NOTE_EDIT open; same open → edit → commit → rematerialize sequence. In-place identity fill; chunk ids unchanged.

Reuse: **YES** — extend `LoopEventStore::assignMissingNoteIdsToNoteOns` + `Loop::assignMissingNoteIdsInStore` pattern. Call from `openNoteEditSession` before rematerialize.

### Layer B2 firmware checkpoint (B2a — landed)

1. Ownership change? **NO** — `ensureNoteEditDisplayProjectionCachesBuilt` committed-base source only. No second display store. `NoteEditCurrentState` still owns uncommitted overlay.
2. State transition change? **NO** — same select / deselect / commit / exit. Paint reads `materialize(active)` when `visualCacheDirty`.

Reuse: **YES** — `LoopPasses::materializeToEventVector` + `NoteUtils::reconstructDisplayNotes`. **NO** — overdub session undo stack; **NO** — `reloadNoteEditSessionStoreFromPasses` extraction; **NO** — `getVisualNotesForSlot` while dirty.

### Layer C firmware checkpoint (C1 — instrumentation)

1. Ownership change? **NO** — same `NoteGeometryResolver::resolve` / `finalReconstructAndSelect`. CAP phases only.
2. State transition change? **NO** — same pitch/move/length apply contract.

Reuse: **YES** — existing `GEOM_APPLY` CAP helpers. **NO** — skip setup/apply/reconstruct (C1 pinned they are not the cost). **NO** — skip `analyze` (empty-overlap pitch still pays it).

### Layer C firmware checkpoint (C2 — analyze split)

1. Ownership change? **NO** — same `NoteGeometryResolver::resolve`. CAP sub-phases only.
2. State transition change? **NO** — same pitch/move/length apply contract.

Reuse: **YES** — `logGeomApplyPhase`. C2 pinned `overlay`. **NO** — skip `pairs` / `interact` / `constrain` / `build`. **NO** — skip overlay when pairs are non-empty.

### Layer C firmware checkpoint (C3 — skip unused overlay)

1. Ownership change? **NO** — same `NoteGeometryResolver::resolve`. Empty-pair path passes storage `baselineMap` instead of building an unread copy.
2. State transition change? **NO** — same pitch/move/length apply contract.

Reuse: **YES** — skip `overlayAnalysisBaselineForSessionMovedOverlaps` when C2a proved no consumer reads it. **NO** — skip overlay when pairs are non-empty.

### Layer C firmware checkpoint (C4 — pair-target overlay)

1. Ownership change? **NO** — same `overlayAnalysisBaselineForSessionMovedOverlaps` / `NoteGeometryResolver::resolve`.
2. State transition change? **NO** — same pitch/move/length apply contract.

Reuse: **YES** — optional `pairTargetNoteIds` on the existing overlay function. **NO** — new overlay owner. **NO** — skip overlay when pairs are non-empty.

## Pre-implementation review (B2a)

### Ready
- Owner: `EditManager::ensureNoteEditDisplayProjectionCachesBuilt`
- Oracle: `committedBase == reconstructDisplayNotes(materialize(active edit passes))` — native pin in `test_edit_apply`
- `editPassIds` are Active-only after session owners; Disabled rows remain on `loop.passes`

### Resolved (user / code)
| Topic | Decision |
|-------|----------|
| Gate | `visualCacheDirty`, not `editPassIds` non-empty (empty after E: undo of first commit) |
| Reuse | Existing projection fingerprint + `playbackRevision`; no second display store |
| B2b | Do not call `getVisualNotesForSlot` while dirty |
| `visualCacheNotesForSelectedSlot` | Unchanged — used by commit-row baseline and reselect; changing it would alter `buildCommitRowsFromCurrentState` |
| `rebuildNoteEditFocusAtSelect` | Unchanged |

### Open before coding
1. Device gate after flash: first deselect shows 1656, not 888. Reselect may still snap via `ensureVisibleRowsForDisplayNotes` — separate if it fails.

### Proceed?
- YES — checkpoint both NO; fixture landed first

**Layer boundaries:** B1 = committed state/replay. B2 = committed display projection. Do not merge because code shares materialization.

---

## Bottom line

| Layer | Role |
|-------|------|
| **B1** | Committed identity/replay — **closed** |
| **B2** | Committed display authority — **B2a** (materialized base when active committed edit passes exist; session overlay for uncommitted only) |
| **A** | Undo-warm cost — **A0 filled (split)**. Firmware: drop unused intermediates. Current-state clone stays until a delta-restore design session |
| **C** | Geometry resolve — after A stops polluting measurement |

Committed NOTE_EDIT paint must consume the same materialized committed state as replay/LOOP_EDIT. B2b is a fallback, not the default. B2c is out.

---

## Does not start (B2 slice)

- `reloadNoteEditSessionStoreFromPasses` helper extraction
- `applyNoteEditPass` / apply-owned `ChangePitch` erase
- `NoteEditSessionUndoStack` changes
- `rebuildNoteEditFocusAtSelect` (unless B2 fixture implicates)
- Global `ensureVisualCacheBuilt` deletion
- Grooming 4e last-resort gather
- Grooming Slice 5 / `openNoteEditSession` `rebuildVisualCacheFromPasses`
- RC1 driver-drift reopen
- RC8 pairing on the `212810` M65 fixture
- New undo Session / lazy snapshot / COW undo state (A0 does not authorize this)
- `UndoWarmJob` FSM / cooperative undo scheduler (scrapped in the 2026-08-05 plan)
- Interval reservation, PLAYING drain, `LoadLoopJob` paint work
- **B2b** as default (fallback only after B2a device cost gate)

---

## Related

- [`note_edit_kind_boundary_undo_warm_incremental_refinement.md`](note_edit_kind_boundary_undo_warm_incremental_refinement.md) — 68-event profile; Stream B first-move `GEOM_APPLY` still open
- [`note_edit_overlap_projection_followup.md`](note_edit_overlap_projection_followup.md) § RC8 — different missing-recon fixture
- [`note_edit_visual_cache_display_unification_refinement.md`](note_edit_visual_cache_display_unification_refinement.md) — undo-warm left open after Stages 8–9
- [`loop_content_resolution_overdub_state_evaluation_refinement.md`](loop_content_resolution_overdub_state_evaluation_refinement.md) — overdub E: routing vs NOTE_EDIT E:; shared materialize, not shared undo stack
- [`note_edit_select_commit_bracket_bugfix.md`](note_edit_select_commit_bracket_bugfix.md) — `M65@609 missing in recon` was wrong `mover_focus` (RC7b), not this layer
