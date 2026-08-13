# NOTE_EDIT / LOOP_EDIT shared display representation

**Status:** Active — Stages 1–2 and 5–9 shipped; Stages 3–4 not started  
**Date:** 2026-08-13  
**Kind:** refinement  
**Parent:** [`Display.md`](../Authority/Architecture/Display.md), [`DerivedViews.md`](../Authority/Architecture/DerivedViews.md), DEC-029  
**Trigger:** [`session_20260813_171219.log`](../../captures/session_20260813_171219.log) — LOOP_EDIT `DFRAME` / `visualCache` **68**; NOTE_EDIT `DISP` frame **23**; cache stays 68; notes return on LOOP_EDIT  
**Overlap consume (separate owner):** [`overdub_playback_observation_overlap_refinement.md`](overdub_playback_observation_overlap_refinement.md)  
**Sibling (192755 commit):** [`note_edit_overlap_action_drop_and_wrap_stub_bugfix.md`](note_edit_overlap_action_drop_and_wrap_stub_bugfix.md) — RC1 withdrawn; RC2 wrap-stub Length.  
**Sibling (193838 mover jump):** [`note_edit_mover_wrap_length_jump_bugfix.md`](note_edit_mover_wrap_length_jump_bugfix.md) — 47→2351 after Hide. Do not reopen Stages 8–9.

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

**Device [`181114`](../../captures/session_20260813_181114.log)** (do not reopen Stage 7 firmware):

- LOOP_EDIT `DISP` **64/64**; first NOTE_EDIT frame **61/64** (Stage 7 first-frame gate not met). Paint 61 vs 64 stays open — not Stage 8/9.
- noteId **76** first `DNTE,24,168,168,671`. Time-axis moves keep length 671 and `interactions=0`.
- Pitch onto 46: `HideNote` noteId **45** as `1440–2160` (720). Leave-restore (`RestoreNote`) writes **45** back to `1440–2160`.
- After 76 is at `840` (`DNTE,46,840,840,671`), select “note 4 at tick 168” rebuilds the same noteId **76**; next `DNTE` is `46,168,168,671`.
- Later move of noteId **87** on pitch 12 uses length **240** and does Shorten/Hide — overlap runs when the mover span is the cache length.

### Sluggishness (measured, rematerialize call sites)

[`181114`](../../captures/session_20260813_181114.log) on this 64-note loop:

| Marker | Range | When |
|--------|--------|------|
| `GEOM_APPLY,resolve` | **24–51 ms** (24942–50993 µs) | every time/pitch fader step |
| `UNDO_WARM,build,total` | **39–108 ms** (39393–108522 µs) | select and first geometry of a gesture |

Rematerialize on the NOTE_EDIT fader path:

- `rebuildNoteEditFocusAtSelect` calls `loop.passes.materializeToEventVector` on every select (bypasses cached `materializedLoopEventsForNoteEditFocus`), then `NoteUtils::reconstructNotes(sessionMidiEvents())` to set `focus.last`.
- `rebuildNoteEditFocusForDisplayNote` uses the cached materialize helper, then `findLinearNoteSpanForNoteId` + `populateBaselineMapForEditClosure` on that rematerialize store.
- `NoteGeometryResolver::resolve` stamps offs and pairs from `editAwareMidiEvents()` (`readLiveLinearSpan`) every step.
- `openNoteEditSession` still calls `loop.rematerializeEditView` once at open (keep; session-store apply, DEC-029).

`UNDO_WARM` is a separate measured cost. Do not fold it into Stage 8/9.

### Stage 8 — Overlap targets use display spans ✅ shipped

**Owner:** `ensureBaselineMapEntriesForEvaluationScope` / `overlayAnalysisBaselineForSessionMovedOverlaps` in [`EditSessionInteraction.cpp`](../../src/EditManager/EditSessionInteraction.cpp); [`NoteGeometryResolver::resolve`](../../src/EditManager/NoteGeometryResolver.cpp).

**Invariant:** a NOTE_EDIT overlap target’s analysis span (and leave-restore `baselineMap` span) is the painted DisplayNote span (visual cache, or this-session current-state if Hidden / Shortened / moved / Added). Rematerialize `readLiveLinearSpan` / `findLinearNoteSpanForNoteId` must not supply a longer end for an unedited Existing row.

**Change:**

- At resolve start, `ensureCurrentStateVisibleRowsFromVisualCache` (Stage 7 align).
- Overlay unedited `focus.baselineMap` entries from `visualCache.notes` so `RestoreNote` does not write a rematerialize end.
- `ensureBaselineMapEntriesForEvaluationScope` / `overlayAnalysisBaselineForSessionMovedOverlaps` take the committed display list: current-state this-session geometry wins; else the display span; else live store.
- Keep `linearSpansOverlapForAnalysis` inclusive classification. Do not call overdub consume. Do not include `OverlapNoteIdObservation.h`.

**Test:** `test_overlay_unedited_row_uses_display_span_not_rematerialize_181114` — 45 rematerialize `1440–2160`, cache `1344–1536`; analysis and Hide/Restore use end **1536**. Hidden / C9 / 87-at-240 fixtures stay PASS.

**Not this stage:** select rematerialize, undo-warm, paint 61 vs 64, overdub consume, zero-length noteId 94.

### Stage 9 — Select / rebuild without rematerialize ✅ shipped

**Owner:** `rebuildNoteEditFocusAtSelect` / `rebuildNoteEditFocusForDisplayNote` in [`NoteEditFocusRebuild.cpp`](../../src/EditManager/NoteEditFocusRebuild.cpp); `noteEditFocusApplyDisplayNote`.

**Invariant:** select and focus rebuild set `focus.last` / `movingNoteId` from the projected DisplayNote (paint / select inventory), not from `materializeToEventVector` + `reconstructNotes(sessionMidiEvents())`. After a this-session move of 76 to 840, selecting the painted 76 rebuilds at 840, not cache tick 168.

**Change:**

- `rebuildNoteEditFocusAtSelect` does not call `loop.passes.materializeToEventVector`. Index select uses `selectableDisplayNotesAtEditSelect` then `rebuildNoteEditFocusForDisplayNote`. Deselect overlays `visualCache.notes` or preserves overlap without rematerialize.
- `noteEditFocusApplyDisplayNote` sets `movingNoteId` from the painted DisplayNote id (no rematerialize / baselineMap span remap). Visible current-state `currentSpan` wins for `focus.last`.
- `rebuildNoteEditFocusForDisplayNote` does not call `findLinearNoteSpanForNoteId` or `populateBaselineMapForEditClosure`.
- `materializedLoopEventsForNoteEditFocus` has no remaining rebuild caller; the helper is kept, no new flatten.

**Test:** `test_focus_apply_display_note_uses_current_state_span_not_cache_tick_181114` — painted 76 at 840 rebuilds `focus.last.startTick == 840`; cache-tick ghost of 76 still uses 840; a different painted note at 168 does not become noteId 76.

**Not this stage:** undo-warm, open-time `rematerializeEditView`, paint 61 vs 64.

### Device gate (after Stage 8+9)

**[`192007`](../../captures/session_20260813_192007.log)** — 3-bar loop (`2304` ticks), visual cache **60** notes. Not the 181114 64-note loop. Mover in the overlap gestures is noteId **108**, not 76.

| Gate | Result |
|------|--------|
| 1. First NOTE_EDIT `DISP` | LOOP_EDIT `60/60`; first NOTE_EDIT `59/60`. Paint gap open (same class as 61 vs 64). |
| 2. Hide/Restore of 45 must not use 720 / 2160 | **45 PASS.** Pitch of 108 onto 46: `HideNote` 45 as `1440–1511`; `RestoreNote` 45 as `1440–1511`. Never `1440–2160`. **76 as target still uses `840–2160`** on Hide and Restore (29.806 / 29.836 and again 63.866 Shorten `840–959` then Restore `840–2160`). |
| 3. Reselect 76 at 840 | **Not the 181114 gesture.** No `MoveNote` of 76. Select “note 21 at tick 840” rebuilds noteId **76** at 38.795 and again at 67.229. No reset to 168 in this capture. |
| 4. `GEOM_APPLY,resolve` | **161** samples, **16.8–52.3 ms** (16813–52256 µs). 181114 was 24–51 ms. `UNDO_WARM,build,total` **35–185 ms** (still open, not this gate). |

At 27.194 noteId 108 is `240–480`; next `DNTE` is `24,240,240,2544`; next pitch step already reads `focus.last` end **2784**. Mover wrap-length jump is recorded; not Stage 8/9 target-span.

Env: `teensy41-capture-serial`.

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
| [`EditSessionInteraction.cpp`](../../src/EditManager/EditSessionInteraction.cpp) / [`NoteGeometryResolver.cpp`](../../src/EditManager/NoteGeometryResolver.cpp) | 8 |
| [`NoteEditFocusRebuild.cpp`](../../src/EditManager/NoteEditFocusRebuild.cpp) / [`NoteEditFocusState.cpp`](../../src/EditManager/NoteEditFocusState.cpp) | 9 |
| [`DisplayNoteResolve.cpp`](../../src/DisplayManager/DisplayNoteResolve.cpp) | 3 (only if the branch still reconstructs) |
| This plan + CURRENT_WORK | each commit |
