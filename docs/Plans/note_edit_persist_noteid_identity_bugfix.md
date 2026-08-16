# NOTE_EDIT persist NoteId identity

**Status:** Stage 1 native PASS — device gate open; Stage 2 parked  
**Date:** 2026-08-16  
**Kind:** bugfix (identity / persist bind)  
**Parent investigation:** [`note_edit_undo_warm_missing_recon_investigation.md`](note_edit_undo_warm_missing_recon_investigation.md)  
**Evidence:** [`173806`](../../captures/session_20260816_173806.log) exit; [`170942`](../../captures/session_20260816_170942.log) same home 256 vs 280  
**Authority:** DEC-039 (persist identity at commit boundary); DEC-029 (`NoteEditCurrentState` owns editable geometry); `applyNoteEditPass` locates by `targetNoteId`

C5 overlay `appendNotesForIds` stays parked. Stage 2 stays parked until Stage 1 is device-proven and a fresh select still mismatches.

---

## Problem

Exit from NOTE_EDIT saves the live move, then LOOP_EDIT paints the rematerialized loop. The rematerialize still has the note at the old home.

[`173806`](../../captures/session_20260816_173806.log):

| Step | What holds |
|------|------------|
| Live | `EditSessionAction` + `DNTE` on **256** at **60@696** |
| Exit rows | NoteRange **256** `696–888` + Pitch **60** |
| After save | `replay_flat` / `take_only` / `session_store` / `loop_materialized` all **M24@888** |
| LOOP_EDIT | paints that rematerialize |

`DisplayNote.noteId` is crossing the persist boundary. `applyNoteEditPass` looks up `findNoteOnById(targetNoteId)`. Home still present means rematerialize has no NoteOn **256** at that capture note.

B1 assign-at-open does not cover this file (assigned **0**). Same home in [`170942`](../../captures/session_20260816_170942.log): rows targeting **256** left M24@888; rows targeting **280** removed it.

Do **not** patch `applyNoteEditPass` to look up by pitch+start. Do **not** rewrite existing valid chunk ids at open.

---

## Authority hierarchy (DEC-039)

```text
Capture / rematerialized NoteOn
        ↓
authoritative persisted NoteId
        ↓
NOTE_EDIT focus / session identity   ← may temporarily differ
        ↓
commit boundary reconcile
        ↓
EditPass.targetNoteId
        ↓
applyNoteEditPass()                  ← stays strictly NoteId-keyed
```

`DisplayNote.noteId` is a **derived identity from the display representation**, not persist authority.

| Note | Identity |
|------|----------|
| Existing capture note | Rematerialized capture `NoteId` |
| Newly added session note | Session-allocated `NoteId` until Create persists it |

An existing-note mover must **never** fall back to a session/display id merely because lookup failed.

---

## Architecture checkpoint (Stage 1)

1. **Ownership change?** **NO.** Persist `NoteId` already belongs to `Loop` rematerialize. `EditManager::commitEditAction` already owns the display/session → `EditPass` boundary. Stage 1 extends that boundary. Not a transfer of `saveNoteEditPass` or `applyNoteEditPass`. `NoteEditCurrentState` does not resolve persist identity.
2. **State transition change?** **NO.** Same open → select → edit → macro/exit `commitEditAction` → rematerialize.

Reuse: **YES** — `commitEditAction` before `saveNoteEditPass`, identity source = `LoopPasses::materializeToEventVector` **before** the new rows are saved (the flat apply will start from).

---

## Invariant

Identity reconciliation succeeds **only** with exactly one authoritative rematerialized NoteOn at `focus.commitBaseline` pitch + start.

```text
findNoteOnById(rematerialize, sessionId) >= 0
    → already present; do not retarget

else count NoteOns at commitBaseline.pitch + commitBaseline.startTick
    0 → Unresolved; do not retarget; do not guess
    1 → Unique; retarget mover Update rows
    >1 → Ambiguous; do not retarget; log
```

`findNoteOnById == -1` does **not** by itself imply pitch+start identity.

Resolve against the same rematerialize source `commitEditAction` uses to apply the saved rows (`materializeToEventVector` before save) — not a convenient `take_only` alias.

---

## Stage 1 — commit-boundary reconcile (approved)

```text
DisplayNote ID
    ↓
session / focus ID
    ↓
commit boundary
    ↓
resolvePersistIdentityForExistingNote
    ↓
EditPass.targetNoteId
    ↓
applyNoteEditPass()
```

Helper name encodes **why** (persist identity for an existing note), not generic `noteIdAtPitchAndStart`.

Retarget **mover Update** rows only (NoteRange / Pitch / Length / Velocity) whose `targetNoteId` equals the session mover id. Do **not** retarget overlap Delete/Update or Create.

Native oracles:

| Case | Expect |
|------|--------|
| Existing note, session 999, unique rematerialize 280 @ 24,888 | retarget 999→280; replay 280 @ 60,696; no 24@888 |
| Added note (no rematerialize NoteOn) | no retarget |
| Zero match at commitBaseline | no retarget; unresolved |
| Two NoteOns at 24,888 | no retarget; ambiguous |

Device gate: 173806 gesture. `replay_flat` home **missing**. LOOP_EDIT shows the moved span.

**Device [`181235`](../../captures/session_20260816_181235.log):** not the unique-mismatch fixture. Select at tick 888 is **2/2 notes**. Live actions address **256** and **280** as two notes at `24@888`. Commits target **280** (`AlreadyPresent` — no `persist identity: retarget` line). After NoteRange+Pitch on 280, `replay_flat` still has **M24@888 noteId=256**; `take_only` still has **noteId=280** (capture). Last commit `M60@552 missing` — 280 left that span. Exit `saved=6`. The remaining home is the sibling **256**, not a failed apply of 280. Stage 1 unique-retarget is not exercised. Stage 2 stays parked.

**Device [`181651`](../../captures/session_20260816_181651.log):** inverse. Select `1/2` at tick 888 → **256**. NoteRange **256** `600–792`. No `persist identity:` line (`AlreadyPresent`). After save, all four traces **M24@888 noteId=280**. Second commit NoteRange+Pitch **256** `360–552` / 60: `M24@600 missing` — **256** left its prior span. Exit `saved=3`. Leftover at 888 is sibling **280**.

**Device [`181941`](../../captures/session_20260816_181941.log):** not sibling leftover. Targeted **280**; after NoteRange+Pitch **280** `744–888` / 45, all four traces still **M24 start=888 end=1032 noteId=280**. Close bake `stale=2 rows=2` used `buildSessionStoreEditPasses`.

**Pass handling (DEC-037 / LCR Stages 3–5):** raw capture + `EditAction`. `resolveNotes` is a projection.

**Device [`190822`](../../captures/session_20260816_190822.log):** live **256** `60@744–936`. Commit Delete **526** + NoteRange+Pitch **256**. `replay_flat` home leftover is sibling **280** — apply of 256 landed (`flatEvents` 219→217, VCACHE **108**). Exit `NoteEditPass replaced stale=3 rows=4` then VCACHE **109** / DISP 109. Close bake disabled the working EditActions and re-encoded a DisplayNote diff. Close now keeps committed `editPassIds` (live-capture bake only when ids are empty). Log: `NoteEditPass close keep committed`.

**Device [`191411`](../../captures/session_20260816_191411.log):** persist PASS — `NoteEditPass close keep committed` twice; LOOP_EDIT after first exit is 109. Open paint is a sibling: LOOP_EDIT 109 / visual 108 / NOTE_EDIT DISP **106**. Wrap `splitHeadTail` cache (tail to loop end + head from 0, same `noteId`) was last-write-wins to the head. Owner: `ensureVisibleRowsForDisplayNotes` — not persist identity, not `applyNoteEditPass`.

**Device [`192259`](../../captures/session_20260816_192259.log):** open wrap paint PASS (`DNTE` 12@2592 length **576**). First move `2592–96` → length **96** / `2640–2736`. Owner: `moveNoteWithOverlapHandling` — use `calculateNoteLength` on wrap `focus.last`.

**Device [`192906`](../../captures/session_20260816_192906.log):** open regression. First `DNTE` **12@0 length 96** (wrap head); no 12@2592/576. Head-only currentSpan skipped the committed tail. `ensureVisibleRowsForDisplayNotes` now groups split fragments to one wrap and does not last-write-wins a head over a wrap.

**Device [`193525`](../../captures/session_20260816_193525.log):** enter NOTE_EDIT selected display head `DNTE` 12@0 length **96**; paint 105 vs visual 107. LCR extra / EditAction already `tid=273,s=0,e=96`. Session row as the head plus splitHeadTail last-write-wins dropped the loop-end tail. Display must not lead. `ensureVisibleRowsForDisplayNotes` merges tail+head to wrap `2592–96` even when the existing row is `0–96`. `projectNoteEditDisplayNotes` keeps cache tail when currentSpan is the head.

**Device [`195050`](../../captures/session_20260816_195050.log):** wrap select PASS (`DNTE` 12@2592 length **576**). Live moves keep 576. Deselect committed `NoteRange start=2736 end=3071` (`mover_focus`). Reselect `DNTE` length **432**, then **240**. Linear pairing / `normalizeAll` `closeOpenTails` wrote the display tail; wrap mover rows were rejected (`end < start`). Commit from current-state wrap; `isPlausibleStorageSpan` allows wrap. Do not wire `resolveNotes` onto paint. Stage 2 stays parked.

**Device [`195941`](../../captures/session_20260816_195941.log):** wrap select still PASS. Live `Movement: start 2736->2784, end actual 3360 (display 288), length=576`. Wrap-modulo helper then wrote `EditSessionAction` / `NoteRange 2784–288`. `replay_flat: M12@2592 missing`; `take_only` still `M12 start=2592`. Reselect `DNTE` 12@2784 length **384** (`2784` wrap-to-`96`). Second deselect `2928–240` → length **240**. Root cause: `findNoteOffForOnIndex` LIFO skips tick-sorted wrap Off@96 (empty stack), so `applyMoveNoteById` moves the On and leaves the wrap Off. Session/edit workflow stays linear (`linearStorageOffTickForSpanEnd`); wrap pairing is the apply resolve only. Do not add more wrap-skip helpers on deselect/sync/commit. Stage 2 stays parked.

**Device [`200952`](../../captures/session_20260816_200952.log):** live storage is linear (`EditSessionAction` 2688–3264, `DNTE` length **576**). Piano roll wrap paint is `end < start` only (`clampNonWrapDisplayNoteBarTicks` clamps 3264 to loop end — tail, no head). `projectNoteEditDisplayNotes` now writes display phase (`2688–192`) so wrap paint runs. Session `currentSpan` stays linear. After deselect, reselect `DNTE` 12@2784 length **384** remains a persist-apply gate. Stage 2 stays parked.

**Device [`201446`](../../captures/session_20260816_201446.log):** wrap display during move PASS (`DNTE` length **576**, `EditSessionAction` 2832–3408). Deselect `pre-commit` NoteRange `2832–3408`; `replay_flat: M12@2592 missing`; `take_only` still `M12 start=2592 end=2688`. After: `DNTE` 12@2832 length **336** (`3072-2832+96`). Second deselect `2880–3216` → length **288**. Rematerialize has wrap Off@96 and false linear Off@2688. LIFO pairs 2688 and never reaches wrap fallback; leftover Off@96 reconstructs with the moved On. `applyMoveNoteById` moves the wrap-head off (`isPreferredWrapTailForHeadOff`) and erases the LIFO off when they differ. Session storage stays linear. Do not add deselect wrap-skip helpers. Stage 2 stays parked.

**195941 / 201446 files (this RC, not persist-identity):**

| File | Change |
|------|--------|
| [`EditApply.cpp`](../../src/EditManager/EditApply.cpp) | wrap-head off via `isPreferredWrapTailForHeadOff`; move that off and drop the false LIFO off |
| [`NoteEditGeometryApplyMutate.cpp`](../../src/EditManager/NoteEditGeometryApplyMutate.cpp) | mover `editedSpan` end is `start+len`; display phase only for bracket |
| [`NoteEditWrapHeadAdjust.cpp`](../../src/EditManager/NoteEditWrapHeadAdjust.cpp) | removed wrap-modulo `displayFocusEndTickForMove` and unused wrap helpers |
| [`test_edit_apply.cpp`](../../test/test_edit_apply/test_edit_apply.cpp) | wrap home Off@96 / On@2592 → linear 2784–3360; dual-off 2592+96+2688 → 2832–3408 with no leftover offs |
| [`NoteEditFocusDisplayProjection.cpp`](../../src/EditManager/NoteEditFocusDisplayProjection.cpp) | linear-beyond-loop `currentSpan` paints wrap display phase (200952) |

---

## Stage 2 — parked

Do **not** implement select-time bind or change `ensureVisibleRowsForDisplayNotes` persist awareness.

After Stage 1 device PASS, open/select again and check whether `DisplayNote.noteId` equals the persisted id. If yes, Stage 2 is unnecessary. If no, pin a fixture and design Stage 2 as identity reconciliation against rematerialize — not generic NoteId assignment — and keep `NoteEditCurrentState` dumb about persist authority.

---

## Observability (Stage 1)

- `commitEditAction persist identity: retarget from=… to=… pitch=… start=…`
- `… unresolved …` / `… ambiguous count=…`
- `logChangeLengthCommitTrace` prints home `noteId` when the home geometry is found

---

## Files (Stage 1)

| File | Change |
|------|--------|
| [`EditApply.h`](../../include/EditApply.h) / [`EditApply.cpp`](../../src/EditManager/EditApply.cpp) | `resolvePersistIdentityForExistingNote`, `reconcileMoverPersistIdentity` — **not** called from `applyNoteEditPass` |
| [`NoteEditSessionCommit.cpp`](../../src/EditManager/NoteEditSessionCommit.cpp) | call reconcile before `saveNoteEditPass` |
| [`NoteEditCommitColdHelpers.cpp`](../../src/EditManager/NoteEditCommitColdHelpers.cpp) | home `noteId` on commit trace |
| [`test_edit_apply.cpp`](../../test/test_edit_apply/test_edit_apply.cpp) | Stage 1 native pins |

No `EditApply.cpp` apply-path change. No `NoteEditCurrentState` persist rule. No focus-rebuild bind.

---

## Hard don'ts

- Do not patch `applyNoteEditPass` or apply-owned `ChangePitch` erase  
- Do not skip `currentState->clone()`  
- Do not start Stage 2 / C5 / B2b / Layer D / grooming 4e  
- Do not reuse `overdubSourceViewNotes_` / `PendingNote.overlapNoteIds`  
- Do not rewrite existing valid chunk ids at open  
- Do not use `take_only` as the identity-resolution source  

---

## Pre-implementation review (Stage 1)

### Ready
- Owner: `EditManager::commitEditAction`
- Identity source: `loop.passes.materializeToEventVector` before save
- Helper: `resolvePersistIdentityForExistingNote` (exactly-one match)

### Resolved
| Topic | Decision |
|-------|----------|
| Stage 2 | Parked until Stage 1 device + fresh-select evidence |
| take_only | Not the identity source |
| 0 / 1 / >1 | Only 1 retargets |
| CurrentState | No persist-identity rule |

### Open before coding
None for Stage 1.

### Proceed?
- YES — Stage 1 only
