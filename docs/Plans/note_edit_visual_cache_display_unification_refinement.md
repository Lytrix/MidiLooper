# NOTE_EDIT / LOOP_EDIT shared display representation

**Status:** Active — Stages 1–2 and 5–7 shipped; Stages 3–4 not started  
**Date:** 2026-08-13  
**Kind:** refinement  
**Parent:** [`Display.md`](../Authority/Architecture/Display.md), [`DerivedViews.md`](../Authority/Architecture/DerivedViews.md), DEC-029  
**Trigger:** [`session_20260813_171219.log`](../../captures/session_20260813_171219.log) — LOOP_EDIT `DFRAME` / `visualCache` **68**; NOTE_EDIT `DISP` frame **23**; cache stays 68; notes return on LOOP_EDIT  
**Overlap consume (separate owner):** [`overdub_playback_observation_overlap_refinement.md`](overdub_playback_observation_overlap_refinement.md)

Align NOTE_EDIT paint with the Loop display representation already used by LOOP_EDIT. No new store. No new domain noun.

---

## Goal

One committed `DisplayNote` list for both edit modes: `Loop::visualCache.notes`.

NOTE_EDIT may overlay **this-session** `NoteEditCurrentState` rows (Hide / Shorten / move / add). It must not reconstruct a second committed list from `passes.materializeToEventVector` + `reconstructDisplayNotes`.

---

## Invariant

While NOTE_EDIT is open and `NoteEditCurrentState` has no Hidden / Deleted / geometry-diff rows, `projectedNoteEditDisplayNotes` equals `visualCache.notes` (same `NoteId` set and spans), then the existing window filter may apply.

A committed display note with **no** current-state row stays painted. Only an explicit Hidden or Deleted row removes it.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO. `Loop` keeps the display representation (`visualCache.notes`). `EditManager` keeps `NoteEditCurrentState` overlay (DEC-029). `DisplayManager` keeps draw. |
| **State-transition change?** | NO. NOTE_EDIT still opens via `openNoteEditSession` → `rematerializeEditView` → `buildFromSessionStore`. Only which `DisplayNote` list is painted changes. |
| **Reuse** | YES — extend `ensureNoteEditDisplayProjectionCachesBuilt` and `projectNoteEditDisplayNotes`. Do not add a parallel cache or Manager. |

`visualCache` is **not** the overdub-overlap authority. Consume stays on `overdubSourceViewNotes_` ([overlap plan](overdub_playback_observation_overlap_refinement.md)).

---

## Debugging boundary

```
Loop::passes / companion EditPass seal     ← overlap consume; do not revisit here
        ↓
Loop::visualCache.notes                    ← shared committed display list
        ↓
projectNoteEditDisplayNotes                ← this plan (overlay only)
        ↓
DisplayManager::resolveDisplayNotes        ← NOTE_EDIT must read the same list
```

Do not treat `Track::getCachedNotes()` as the shared list. That path reconstructs from `editAwareMidiEvents()` (session store while NOTE_EDIT is active). The LOOP_EDIT piano roll uses `visualCache.notes` / `getVisualNotesForSlot`.

---

## Current vs target

| Consumer | Today | Target |
|----------|--------|--------|
| LOOP_EDIT paint | `resolveDisplayNotesCommitted` → `visualCache.notes` | unchanged |
| NOTE_EDIT committed base | `reconstructDisplayNotes(materializeToEventVector)` in `ensureNoteEditDisplayProjectionCachesBuilt` | `visualCache.notes` (build via `ensureVisualCacheBuilt` when short-loop policy allows, same as `getVisualNotesForSlot`) |
| NOTE_EDIT overlay | `projectNoteEditDisplayNotes` + `noteEditCurrentState` | same function; missing row ≠ Hidden |
| Session store | `rematerializeEditView` / `editAwareMidiEvents` | unchanged — edit apply and playback preview only (DEC-029) |

171219 `DISP,0,STOPPED,2304,23,68,23,23,1` is `frameNotes=23`, `visualCache.notes=68`. Target first NOTE_EDIT frame: `frameNotes == visualCache.notes` (68 on that loop).

---

## Why NOTE_EDIT drops notes today

`NoteEditCurrentState::buildFromSessionStore` only inserts rows that pass `readLiveLinearSpan`. `readLiveLinearSpan` / `findLifoOffTickForNoteOn` require `off.tick > on.tick`.

`projectNoteEditDisplayNotes` then calls `nonVisibleParticipantSuppressedFromProjection`, which uses `rowIsVisible`. A missing row returns false, so the committed display note is dropped.

`NoteEditPresenceType::Visible` is 0. 171219 `presence=0 rowProjectsToStore=1` means the **selected** notes are in current state. It does not describe the 45 notes that never got a row.

Hidden-row fixtures stay in force: `test_display_projection_inactive_focus_masks_hidden_overlaps` must still omit explicit Hidden ids.

---

## Out of scope

- Overlap consume, wrap-head tail-only, `overlapNoteIds`, companion seal
- Making `visualCache` the overdub source view
- Unifying LOOP_EDIT onto rematerialize (that would paint 23 in both modes)
- New `*View` / materialized-note store
- Changing NOTE_EDIT open / rematerialize / commit transitions
- Long-loop overview strip
- Select-fader 23→17 in 171219 — re-measure after Stages 1–2; open a new RC only if it remains

---

## Stages

Commit each verified stage before the next.

### Stage 1 — Missing current-state row is not Hidden ✅ shipped

**Owner:** `nonVisibleParticipantSuppressedFromProjection` in [`NoteEditFocusDisplayProjection.cpp`](../../src/EditManager/NoteEditFocusDisplayProjection.cpp)

**Invariant:** `projectNoteEditDisplayNotes` keeps a committed-base note whose `NoteId` has no current-state row. Explicit Hidden / Deleted rows still omit.

**Change:** suppress only via `isRowHiddenOrDeleted`. A missing row is not Hidden.

**Test:** `test_display_projection_keeps_committed_note_without_current_state_row` — wrap note A (`end < start`) in committed base, current state has only linear note B; paint contains A and B. Hidden fixtures stay PASS (`test_display_projection_inactive_focus_masks_hidden_overlaps`).

**Not this stage:** visual-cache wiring. Device still shows 23 until Stage 2 uses `visualCache.notes` as committed base.

### Stage 2 — Committed base is `visualCache.notes` ✅ shipped

**Owner:** `EditManager::ensureNoteEditDisplayProjectionCachesBuilt` in [`NoteEditDisplayProjection.cpp`](../../src/EditManager/NoteEditDisplayProjection.cpp)

**Invariant:** NOTE_EDIT committed base is the selected loop’s `visualCache.notes`, not a rematerialize reconstruct.

**Change:**

- Committed base is `track.getVisualNotesForSlot(slot)` (same STOPPED / short-loop `ensureVisualCacheBuilt` gate as LOOP_EDIT).
- Long loop: no full flatten on NOTE_EDIT open; existing cache + window filter.
- Overlay unchanged: `projectNoteEditDisplayNotes(committedBase, editAwareMidiEvents(), …, &noteEditCurrentState)`.
- Cache key includes `visualCache.revision`.
- `materializedLoopEventsForNoteEditFocus` kept for `rebuildNoteEditFocusForDisplayNote`.

**Test:** `test_display_projection_paint_matches_committed_base_when_current_state_is_subset` — 8-note committed stand-in, 3 linear current-state rows, paint count 8. Device gate remains Stage 4 (171219).

### Stage 3 — `resolveDisplayNotes` NOTE_EDIT path

**Owner:** `DisplayManager::resolveDisplayNotes` in [`DisplayNoteResolve.cpp`](../../src/DisplayManager/DisplayNoteResolve.cpp)

**Invariant:** NOTE_EDIT paint is `projectedNoteEditDisplayNotes`, and that projection’s committed base is the visual cache (Stage 2). No fallback that rebuilds from session store for the piano roll.

**Change:** keep the `EditSessionType::Note` branch calling `projectedNoteEditDisplayNotes`. Do not assign `getCachedNotes()` or rematerialize reconstruct. Idle NOTE_EDIT (no Hidden / Deleted / geometry-diff rows) must not take a path that yields a different `NoteId` set than LOOP_EDIT.

**Test:** existing `test_note_edit_current_state` / `test_note_edit_focus` projection suite + Stage 1 fixture. Device: Stage 4.

### Stage 4 — Device gate (171219)

**Setup:** same loop that LOOP_EDIT paints at 68 (or whatever `visualCache.notes.size()` is before the short press).

**Pass:**

- First NOTE_EDIT `DISP` after `EditSession opened`: `frameNotes == visualCache.notes` (both 68 on that capture’s loop).
- Select empty steps: paint count stays that shared count.
- Cycle to LOOP_EDIT: `DFRAME` / `DISP` matches the same count.
- After a this-session Hide, that `NoteId` is absent in NOTE_EDIT and, after commit + cache rebuild, absent in LOOP_EDIT.

**Fail:** `frameNotes` drops on NOTE_EDIT open while `visualCache.notes` stays at the LOOP_EDIT count.

Env: `teensy41-capture-serial`. Ask before upload.

[`174139`](../../captures/session_20260813_174139.log): Stage 2 paint is on device — LOOP_EDIT `DISP` 68/68; NOTE_EDIT open frame **75** (cache 68). Notes show. Select is Stage 5.

### Stage 5 — Selectable inventory includes painted notes without a current-state row ✅ shipped

**Owner:** `filterSelectableDisplayNotes` in [`NoteEditFocusDisplayProjection.cpp`](../../src/EditManager/NoteEditFocusDisplayProjection.cpp)

**Evidence:** [`174139`](../../captures/session_20260813_174139.log) — paint 75; select only `note_idx=0` (tick 0) and `note_idx=1` (tick 192). Ticks 48, 96, 144, 240, 288, 336 are `empty_step`.

**Invariant:** a painted committed note with no current-state row stays in selectable inventory. Explicit Hidden / Deleted and the existing shortened-tail mask still exclude.

**Change:** drop from inventory only when `hasRow(noteId)` and `!rowIncludedInSelectableInventory(...)`. Do not treat `find == nullptr` as excluded. Do not change `rowIncludedInSelectableInventory` itself (that API answers “does this row qualify”; a missing row is not a qualifying row).

**Not this stage:** paint 75→69 on focus (174139 / 171219 23→17). Stage 3 audit. `visualCache` as overlap source.

**Test:** `test_selectable_inventory_keeps_painted_note_without_current_state_row` — wrap A + linear B + Hidden C; selectable has A and B, not C. Hidden / C9 fixtures stay PASS.

### Stage 6 — Current-state Visible rows for painted notes without a store pair ✅ shipped

**Owner:** `NoteEditCurrentState::ensureVisibleRowsForDisplayNotes` in [`NoteEditCurrentState.cpp`](../../src/EditManager/NoteEditCurrentState.cpp); called from `openNoteEditSession` and `rebuildNoteEditFocusForDisplayNote` via `EditManager::ensureCurrentStateVisibleRowsFromVisualCache`.

**Evidence:** [`174635`](../../captures/session_20260813_174635.log) — Stage 5 select works. noteId **108** (presence=-1) moved and ran overlap. noteId **78** (pitch 23 @ 384, presence=-1): coarse fader computed 384→336, then `NoteGeometryResolver did not apply move` / `GEOM_APPLY,resolve,…,0`. `appendCausingNoteActions` skips the mover when `readEditableCurrentSpan` and `rowProjectsToStore` both fail. noteId **101** later only emitted Shorten/Hide on neighbor 23 — the mover itself never left 1248.

**Invariant:** a painted committed display note with a non-zero span and no current-state row gets a Visible row from `visualCache.notes` at NOTE_EDIT open (and on focus rebuild). Explicit Hidden / Deleted / existing rows are not overwritten. Zero-length display notes (`endTick == startTick`, 174635 noteId=14) are skipped.

**Change:** after `buildFromSessionStore`, upsert missing Visible rows from `track.getVisualNotesForSlot`. Do not make `visualCache` the overlap-consume source. Do not change `readLiveLinearSpan`.

**Test:** `test_current_state_upserts_visible_row_from_display_note_without_store_pair` — cache-only 78 and wrap 40 become Visible + driver-valid; Hidden stays Hidden; zero-length 14 is not inserted.

**Not this stage:** paint 75 vs cache 68. Stage 3 audit. Zero-length reconstruct (noteId=14).

### Stage 7 — Visual-cache span is the open-time length (not rematerialize pairing) ✅ shipped

**Owner:** `NoteEditCurrentState::ensureVisibleRowsForDisplayNotes` / `projectNoteEditDisplayNotes`

**Evidence:** [`175621`](../../captures/session_20260813_175621.log)

- Stage 6 gate met: `presence=0 rowProjectsToStore=1`; `GEOM_APPLY,resolve,…,1` on former `-1` notes.
- Open `DISP` **74** vs cache **68**. Empty-step `DISP` **73** vs **69**.
- noteId **25** and **63** both receive Shorten at pitch 12 start **1344** (two current-state rows, one musical place).
- Select noteId **45** at 1344: `DNTE,12,1344,1344,720,33` — painted length **720** before the next move. Move then uses `focus.last` end **2064**, Hides six same-pitch neighbors, `DNTE` length stays 720.
- LOOP_EDIT exit: `DISP` **64/64**. The committed visual-cache reconstruct does not keep the 720-span.

**Invariant:** at NOTE_EDIT open, a Visible row whose `NoteId` is in `visualCache.notes` uses that display span. Paint overlay does not add rematerialize-only Visible rows that are not in the committed base (except this-session Added). A Visible row that matches a committed display note by span still paints when the cache row has no `NoteId` (161117). Hidden / Deleted unchanged.

**Change:** `ensureVisibleRowsForDisplayNotes` writes the display span onto unedited Existing Visible rows (`committedSpan == currentSpan`). `projectNoteEditDisplayNotes` adds rematerialize-only Visible rows to paint only when lifecycle is Added or the row matches a committed display note by span.

**Test:** `test_ensure_aligns_unedited_visible_row_to_display_span` (45: 2064 → 1584; edited 87 and Hidden stay). `test_display_projection_omits_rematerialize_only_visible_row` (63 omitted; Added paints). `test_display_projection_binds_visible_row_to_invalid_id_committed_note` (161117). Hidden / C9 fixtures stay PASS.

**Not this stage:** zero-length noteId 94 (`DNTE` length 0). Overlap consume. Making `visualCache` the overdub source.

---

## Pre-implementation review

### Ready

- Display owner is already `Loop::visualCache` ([Display.md](../Authority/Architecture/Display.md)).
- Overlay owner is already `NoteEditCurrentState` (DEC-029).
- 171219 proves the two lists diverge; `presence=0` is Visible, not missing.
- Hidden-row tests already pin RC6 / RC10b omit behavior.

### Resolved

| Topic | Decision |
|-------|----------|
| Shared list | `visualCache.notes`, not `getCachedNotes()`, not rematerialize reconstruct |
| Missing current-state row | Keep committed display note |
| Explicit Hidden / Deleted | Still omit (RC6) |
| Overlap consume | Out of scope |
| Long-loop open | No new full flatten; existing cache + window filter |

### Open before coding

1. After Stage 6 device verify, re-measure paint 75 vs cache 68 (174139 / 174635). New RC if it remains — do not fold into Stage 6.
2. `rg materializedLoopEventsForNoteEditFocus` before shrinking that helper; focus rebuild still reads it.

### Proceed?

YES for Stage 1 after this plan is accepted. Stages 2–3 follow only when Stage 1 native is green.

---

## Docs closeout (when a stage ships)

- This plan: mark the stage shipped.
- [`CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md): pointer + status.
- [`Display.md`](../Authority/Architecture/Display.md): NOTE_EDIT reads `visualCache.notes` then current-state overlay — if the prose still implies a separate reconstruct.
- Do not duplicate DEC-029. No DEC unless a later stage moves ownership (it must not).

---

## Files (expected)

| File | Stage |
|------|--------|
| [`NoteEditFocusDisplayProjection.cpp`](../../src/EditManager/NoteEditFocusDisplayProjection.cpp) | 1 |
| [`test_note_edit_current_state.cpp`](../../test/test_note_edit_current_state/test_note_edit_current_state.cpp) | 1 |
| [`NoteEditDisplayProjection.cpp`](../../src/EditManager/NoteEditDisplayProjection.cpp) | 2 |
| [`NoteEditFocusDisplayProjection.cpp`](../../src/EditManager/NoteEditFocusDisplayProjection.cpp) `filterSelectableDisplayNotes` | 5 |
| [`NoteEditCurrentState.cpp`](../../src/EditManager/NoteEditCurrentState.cpp) `ensureVisibleRowsForDisplayNotes` | 6 |
| [`NoteEditSessionLifecycle.cpp`](../../src/EditManager/NoteEditSessionLifecycle.cpp) / [`NoteEditFocusRebuild.cpp`](../../src/EditManager/NoteEditFocusRebuild.cpp) | 6 |
| [`NoteEditCurrentState.cpp`](../../src/EditManager/NoteEditCurrentState.cpp) `ensureVisibleRowsForDisplayNotes` align | 7 |
| [`NoteEditFocusDisplayProjection.cpp`](../../src/EditManager/NoteEditFocusDisplayProjection.cpp) `projectNoteEditDisplayNotes` | 7 |
| [`DisplayNoteResolve.cpp`](../../src/DisplayManager/DisplayNoteResolve.cpp) | 3 (only if the branch still reconstructs) |
| This plan + CURRENT_WORK | each commit |
