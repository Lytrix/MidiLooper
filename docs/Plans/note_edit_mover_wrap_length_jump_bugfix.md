# NOTE_EDIT mover wrap-length jump after overlap Hide

**Status:** Active — RC1 native shipped; device gate open  
**Date:** 2026-08-13  
**Kind:** bugfix  
**Parent:** [`note_edit_visual_cache_display_unification_refinement.md`](note_edit_visual_cache_display_unification_refinement.md) (Stages 8–9 shipped; do not reopen evaluate/select rematerialize)  
**Sibling:** [`note_edit_overlap_action_drop_and_wrap_stub_bugfix.md`](note_edit_overlap_action_drop_and_wrap_stub_bugfix.md) (RC2 14 `2256–2304` stays on that plan)  
**Evidence:** [`session_20260813_193838.log`](../../captures/session_20260813_193838.log) @156.013–156.239; same class [`session_20260813_192007.log`](../../captures/session_20260813_192007.log) note 108 `240–480` → `DNTE` length 2544  
**Overlap consume:** out of scope

After Hide of same-pitch neighbor **16**, mover **111** (length **47**) paints as length **2351** and the next resolve uses `focus.last` end **2975**.

---

## Debugging boundary

```
Loop::passes / rematerializeEditView     ← keep at open
        ↓
visualCache + current-state overlay      ← Stage 8/9; do not reopen rematerialize
        ↓
applyEditSessionActions (Hide 16 + Move 111 624–671)  ← 111 stays 624–671
        ↓
LoopTickNormalize::normalize Pass 1      ← first 2975 (wrap merge, no noteId)
        ↓
syncProjectingRowsFromSessionStore       ← currentSpan 2975
        ↓
projectNoteEditDisplayNotes / DNTE       ← 2351 paint (156.023)
        ↓
ensureNoteEditFocusForLiveEdit rebuild   ← last.end 2975 (156.230 bridge)
        ↓
moveNoteWithOverlapHandling              ← length 2351 → 672–3023
        ↓
note 14 ChangeLength 2256–2304           ← other plan (RC2)
overdubSourceViewNotes_ / overlapNoteIds ← do not read
```

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO. Same EditManager apply / project / rebuild / move path. |
| **State-transition change?** | NO. NOTE_EDIT open / commit unchanged. |
| **Reuse** | YES — extend `LoopTickNormalize::normalize` Pass 1. Keep `isInflatedDisplaySpan`. Do not add a new domain noun. |

---

## Device facts ([`193838`](../../captures/session_20260813_193838.log))

Loop **2304** ticks, cache **34**. Mover **111**, pitch **30**, length **47**.

| Time | What the log writes |
|------|---------------------|
| 156.013 | `MoveNote` 111 `624–671` (length 47). `ShortenNote` 96 `576–623`. `HideNote` 16 `663–854`. |
| 156.017 | `NOTE_EDIT micro normalize: closure geometry failed (check=16)` — `LoopEventCheck::NoteIdPairing` |
| 156.023 | `DNTE,30,624,624,2351` |
| 156.230 | `from=624 to=672`. Rebuild `noteId=111`. `Overlap move bridge: start=624, end=2975`. `Movement: … end actual 3023 … length=2351`. `MoveNote` 111 `672–3023`. `HideNote` 32 `2208–2975`. |

`2351 = 2975 − 624`. `2975 = 2304 + 671` (loop length + the correct linear end).

`NoteEditCurrentState::applyEditSessionAction` for `MoveNote` writes `currentSpan` from the action (`624–671`) and translates `committedSpan` by the same start delta, so after this move **committed == current** (`624–671`).

`ensureNoteEditFocusForLiveEdit` rebuilds only when the live driver is invalid. The rebuild at 156.230 ran. `noteEditFocusApplyDisplayNote` then set `focus.last` to a span whose end is **2975**.

Same class in [`192007`](../../captures/session_20260813_192007.log): note 108 `240–480` then `DNTE` length **2544** then `focus.last` end **2784**.

---

## RC0 — Native fixture names the first writer — shipped

**Owner:** `test_mover_wrap_length_jump_names_first_writer_193838` in [`test_note_edit_focus.cpp`](../../test/test_note_edit_focus/test_note_edit_focus.cpp). No firmware.

**Measured after Hide 16 `663–854` + Move 111 `624–671` (2304-tick loop):**

| Step | 111 end | projected length | last.end |
|------|---------|------------------|----------|
| After `applyEditSessionActions` | **671** | — | — |
| After `LoopTickNormalize::normalize` (`wrapPairsMerged=1`) + `syncProjectingRowsFromSessionStore` | **2975** | **2351** | **2975** |

**Decision table (filled):**

| currentSpan end | projected length | last.end after applyDisplayNote | First writer |
|-----------------|------------------|----------------------------------|--------------|
| 671 | 47 | 671 | Not reproduced — stop |
| 671 | 2351 | 671 | `projectNoteEditDisplayNotes` (paint only) |
| 671 | 2351 | 2975 | `noteEditFocusApplyDisplayNote` ignored Visible currentSpan |
| **2975** | **2351** | **2975** | **`LoopTickNormalize::normalize` Pass 1 wrap merge, then `syncProjectingRowsFromSessionStore`** |

Apply is clean. Projection and `noteEditFocusApplyDisplayNote` follow the inflated currentSpan. `ensureVisibleRowsForDisplayNotes` is not on this path.

Pass 1 matches pitch + channel only (no `noteId`). A wrap-window on (32 `@2208`) plus 111’s off `@671` (`671 < wrapWindow 768`) is treated as a head/tail pair. `linearOff = loopLength + 671 = 2975` is written onto 111’s off. Sync copies that span into current-state.

**Not this RC:** firmware, RC2 note 14, undo-warm.

---

## RC1 — Mover length stays 47 across that Hide — native shipped

**Owner:** `LoopTickNormalize::normalize` Pass 1 wrap merge in [`LoopTickNormalize.cpp`](../../src/Utils/LoopTickNormalize.cpp).

**Invariant:** after Hide of a same-pitch neighbor, the mover’s `currentSpan` / `focus.last` / selected `DNTE` length stay the pre-Hide length (**47** here). They must not become `loopLength + priorEnd − start` (**2351**).

**Change:** Pass 1 skips a wrap merge when both on and off are tagged and the `noteId`s disagree. Untagged (legacy) pairs and same-`noteId` wrap pairs still merge. Sync still copies store; it is not the fix.

**Test:** `test_mover_wrap_length_jump_names_first_writer_193838` — 111 end **671**, projected length **47**, `last.end` **671**. `test_normalize_wrap_merge_skips_other_tagged_note_id` / `test_normalize_wrap_pair_same_note_id_still_merges`.

**Not this RC:** note 14 `2256–2304`, paint 59 vs 60, undo-warm. Device gate below.

---

## Device gate (after RC1)

Same 34-note loop as 193838:

1. Move 111 through 16: after first `HideNote` 16, selected `DNTE` length stays **47** (not 2351).  
2. Next coarse step: `Overlap move bridge` end stays **671** (not 2975). No `MoveNote` 111 `672–3023`. No `HideNote` 32 `2208–2975`.  
3. 96 Shorten / Restore still runs.

Env: `teensy41-capture-serial`. Ask before upload.

**[`200154`](../../captures/session_20260813_200154.log) — RC1 gate PASS for the 111 jump.** No `2351` / `2975` / `3023`. Pitch-30 `DNTE` stays **47**. Mover **22** (pitch 29) keeps `Movement` / `DNTE` length **95** while overlap Shorten/Hide/Restore runs on 115, 87, 100, 7, 5, 6, 4, 8, 9, 23, 101, 25.

Remaining (not this RC):

- Note **5** leave-restore `720–2255`; later select `DNTE,71,720,720,1535`.  
- Note **14** `ChangeLength` `2256–2304` still on the wrap-stub plan.

---

## Pre-implementation review

### Ready

- Apply writes 111 as `624–671`. First 2351 is the following `DNTE`.  
- Next move rebuilds because the live driver is invalid, then the bridge reads `last.end=2975`.  
- `isInflatedDisplaySpan` already exists.

### Resolved

| Topic | Decision |
|-------|----------|
| Plan | Sibling bugfix; do not fold into RC2 or Stage 8/9 |
| RC order | RC0 fixture, then RC1 on the named owner |
| Inclusive overlap classify | Keep |

### Open before RC1 firmware

1. Device gate on the 34-note 193838 loop after flash.

### Proceed?

YES — RC1 native. Device after upload.
