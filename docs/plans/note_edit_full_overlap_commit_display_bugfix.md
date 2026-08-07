# Note edit — full overlap commit still paints hidden short note

**Status:** **Step 1 shipped** (native) — HITL `203805` full-cover paint gate pending device verify.

**Parent:** [note_edit_resolver_authority_contracts_refinement.md](note_edit_resolver_authority_contracts_refinement.md) — Stage 6.5 display follow-up + **C4 display slice** (participant set from current state).

**Related shipped work (Stage 6.5 (1)):** `7c21e73` Visible shortened + inventory mask; `2aeb175` paint vs selectable cache split; `c4136fd` L→R Hidden→Visible shorten + leave-restore paint. HITL: `202147` (paint leak, fixed `2aeb175`), `202538` (`c4136fd`), **`203805`** shorten paint PASS. Full commit table: parent plan §8 “Shipped commits (Stage 6.5)”.

---

## Symptom (user report)

When a **short note** is **fully overlapped** by the mover and the edit is **committed** (select-bracket / macro commit path):

| Layer | Expected | Observed (`203805`) |
|-------|----------|---------------------|
| **Selectable inventory** (DNTE / fader-1) | Hidden overlap not selectable | **PASS** — count drops; note absent from inventory |
| **Grid paint** (OLED piano roll) | Fully covered note **removed** from display | **FAIL** — note **still drawn** at committed span |

Inventory masking and paint masking are **split** for shortened overlap (Stage 6.5 (1)); this bug is the **full hide / complete-cover** case after commit.

---

## Log anchors (`session_20260807_203805.log`)

### Short note fully covered (pitch 88, noteId=9, span 2592–2639, length 47)

| Time | Evidence |
|------|----------|
| ~`21.661s` | Mover (noteId=17) steps onto short note 9 at 2592 (`overlapNotes=0` — same-pitch stack). |
| **`34.404s`** | `EditSessionAction: type=2 noteId=9 start=2592 end=2639` — **HideNote** (complete cover). Mover `17` → 2544–2992. `interactions=1 constrained=1`. |
| `34.405s` | `Note selection changed: 9 -> 7` — inventory index drops (short note no longer selectable). |
| `34.406s` | `#CAP,34409689,DISP,2,STOPPED,5376,12,13,12,12,1` — **frameNotes=12**, **visualCache=13** (one note still in committed cache not removed from frame). |
| `34.406s` | `DNTE,88,2544,2544,448,7` — sidebar lists mover only (inventory OK). |

### Commit on overlapping short note (pitch 84, noteId=9 @ 2592 — prior overlap session)

| Time | Evidence |
|------|----------|
| `30.039s` | Focus rebuild: `changedOverlapNoteIds count=1`, `changedOverlapNoteId=9`. |
| `30.059s` | `NOTE_EDIT pre-commit row: targetNoteId=9 action=Update property=NoteRange start=2592 end=2639`. |
| `30.086s` | `commitEditAction loop_materialized: M84 start=2592 end=2639`. |
| `30.106s` | Focus rebuild: **`changedOverlapNoteIds count=0`** — overlap membership cleared after commit. |

---

## Debugging boundary

```
Geometry / apply (HideNote on complete cover)  ← inventory correct in log
  → NoteEditCurrentState (presence Hidden, not in session store)
  → commit / focus rebuild (changedOverlapNoteIds cleared)
  → projectNoteEditDisplayNotes + visualCache committed base  ← **current investigation**
```

Do **not** re-open shorten-vs-hidden semantics or overlap closure (Stage 6.5 (2)) unless a new log shows `ShortenNote` on this path.

---

## Hypothesis (code-backed)

`projectNoteEditDisplayNotes` builds from **`loop.visualCache.notes`** (committed base) and overlays **participants** from `changedOverlapNoteIds` + session store.

1. **HideNote** sets `NoteEditCurrentState` presence **Hidden** and removes the note from session store projection — inventory filter (`rowIncludedInSelectableInventory` / Hidden) works.
2. **Participant overlay** only applies to noteIds in the participant set (`hasChangedOverlapNote` / `changedOverlapNoteIds` when current state non-empty).
3. After **commit**, focus rebuild logs **`changedOverlapNoteIds count=0`** while the loop **visual cache** still holds the committed span for the hidden note (`visualCache=13` vs `frameNotes=12` in DISP).
4. Committed-base copy in `projectNoteEditDisplayNotes` **still includes** notes that are Hidden in `noteEditCurrentState` but **no longer listed as overlap participants** — so the grid paints the stale committed row.

**Root cause (contract framing):** Display projection still treats **`focus.changedOverlapNoteIds`** as the participant source for paint overlay (`collectProjectionParticipantNoteIds`), while inventory already trusts stage 2. After commit clears the Focus latch, paint falls back to stale **`visualCache`** — violating the **primary projection contract** (`visible == false` → no row) and display-side C4 membership routing.

**Authority matrix row:** [parent §10](note_edit_resolver_authority_contracts_refinement.md#10-authority-test-matrix) — Hidden + full geometry retained → paint **no**, inventory **no**.

**Invariant (step 1):** `visible == false` → projection emits **no row** — regardless of `currentSpan`, `committedSpan`, `visualCache`, or inventory. Not `if (presence == Hidden)` alone while another path paints `committedSpan`.

**Step 2 contract (C9):** Visible shortened → paint shortened stub; inventory may mask — [projection/inventory independence](note_edit_resolver_authority_contracts_refinement.md#6-target-contracts).

---

## Proposed fix — contract-shaped (C4 display slice)

Implement as **Stage 6.5 / C4 display migration**, not a one-off filter. Two coordinated changes in `projectNoteEditDisplayNotes` / `collectProjectionParticipantNoteIds`:

### 1. Participant discovery — current state, not Focus latch

When `noteEditCurrentState` is non-empty, build the paint participant set from **`collectOverlapParticipantNoteIdsFromCurrentState`** (already used on geometry/reselect paths) plus `movingNoteId`, instead of (or before) iterating **`focus.changedOverlapNoteIds`**.

| Current (legacy) | Target (C4) |
|------------------|-------------|
| `collectProjectionParticipantNoteIds` reads `focus.changedOverlapNoteIds` | Participant set = `currentStateRowIsOverlapParticipant` rows + mover |
| Hidden overlap cleared from latch after commit → not a participant | Hidden overlap **remains** a participant until current-state row removed or session ends |

This removes **one display dependency** on `changedOverlapNoteIds` without deleting the Focus field. **Cache removal** is post–Stage 8 cleanup (parent §11 step 5) — Stage 4 membership authority is already shipped.

### 2. Visibility gate — unconditional projection contract

When `noteEditCurrentState` non-empty, **non-visible** participants (`presence == Hidden` / `Deleted` today → `visible == false`) emit **no paint row** — independent of `committedBaseNotes`, `visualCache`, and participant latch. Implementation must not be a lone `if (presence == Hidden)` on one path while committed-base copy still paints.

Leave-restore paint (mover past committed span, RC10h) is a **separate** visibility/geometry case — step 3 HITL; must not regress `test_selectable_inventory_excludes_paint_only_hidden_row`.

### Rejected options

| Option | Verdict |
|--------|---------|
| **A-only** — committed-base strip without participant-source migration | Fixes symptom but **adds parallel rule**; keeps legacy latch path; does not complete C4 on display |
| **B** — fingerprint-only cache merge | Does not fix authority chain; stale cache can still win |
| **C** — patch `visualCache` on commit | **Away from contract** — couples display to pass bake; prefer projection owner |

### What this fix **does** move toward end state

| Contract | Progress |
|----------|----------|
| **C4** | Display participant set derived from `NoteEditCurrentState` |
| **Stage 4** | Display path aligned with `collectOverlapParticipantNoteIdsFromCurrentState` (geometry already routed) |
| **Stage 2 authority** | Paint respects **visibility projection contract** |
| Display C4 routing | One fewer consumer of `changedOverlapNoteIds` on paint path |

### What this fix **does not** complete

| Item | Still open |
|------|------------|
| **C5 / Stage 8** | Single span-resolution path; reduce `visualCache` as paint base |
| **C7 / Stage 7.4** | HITL full leave-restore → visible + paint |
| **§11 step 5** | Semantic cleanup — latch removal, live-store inference, helper deletion |
| **V5** | Sidebar `DNTE` span split (Stage 8) |
| **Stage 9** | Full macro-commit / `committedSpan` sealing contract |

---

## Remaining stages and steps to end state

**Approved sequence:** parent [§11 Remaining roadmap](note_edit_resolver_authority_contracts_refinement.md#11-remaining-roadmap-approved-sequence), [§10 Authority test matrix](note_edit_resolver_authority_contracts_refinement.md#10-authority-test-matrix), [§12 model direction](note_edit_resolver_authority_contracts_refinement.md#12-model-refinement--orthogonal-dimensions-design-direction) (conceptual only until post–Stage 8).

### This bugfix = roadmap step 1

| Prove | Mechanism |
|-------|-----------|
| `HideNote` → non-visible | Already shipped on apply path |
| `visible == false` → **no projection row** | Visibility gate — grid independent of `visualCache` |
| Selectable inventory **no row** | Already shipped (`rowIncludedInSelectableInventory`) |
| Resolver / action semantics | **No change** |

Native must prove: participant exists, spans differ or committed retained, `visualCache` contains note, inventory excludes, **`visible == false` → grid excludes**.

**Step 2 (same PR):** Contract test **C9** — visible shortened stub painted; inventory masked (`203805` shorten).

### After step 1–2

| Step | Scope |
|------|--------|
| **3** | Stage **7.4** HITL — Hidden → overlap cleared → Visible + paint restored (`163621`, `175858`) |
| **4** | Stage **8** — one **participant projection contract** (grid + sidebar + snapshot); separate rendering consumers |
| **5** | **Semantic cleanup** (refactor phase) — latch removal, live-store inference, helpers — parent §11 step 5 |

### Stage 8 tasks (parent §8)

| Task | Scope |
|------|--------|
| **8.1** | Sidebar `DNTE` via `resolveParticipantDisplaySpan` (**V5**) |
| **8.2** | Fader / snapshot — same path as grid |
| **8.3** | Reduce `visualCache` as paint base |
| **8.4** | HITL `151441` |

### End-state pipeline — shipped vs pending

| Stage | Owner | Shipped? | Pending |
|-------|--------|----------|---------|
| 1 | Identity / inventory drivers | **DONE** | — |
| 2 | `NoteEditCurrentState` authority | **DONE** (causing path) | Full writer gating via Stage 8 apply |
| 3 | `ParticipatingNoteSession` read model | **DONE** | Orthogonal `visible` model — post–Stage 8 (§11) |
| 4 | Participant discovery (**C4**) | **DONE** (geometry/reselect) | **Display path** — this bugfix |
| 5 | Driver / inventory sync (**C6**) | **DONE** (code) | HITL 5.4 |
| 6 | Action semantics (**C8**) | **DONE** (code) | HITL 6.5 — shorten done; full-overlap paint |
| 7 | Leave/restore (**C7**) | **DONE** (code) | HITL 7.4 (roadmap step 3) |
| 8 | Unified projection (**C5**) | **Pending** | Roadmap step 4 |
| 9 | Commit vs committed baseline | Partial (`syncCommittedSpan`) | Full macro-commit contract |

```text
Step 1 (this fix) + step 2 regression → 7.4 HITL → Stage 8 → helper deletion (step 5)
```

---

## Tests to add (before fix)

Native fixture from `203805` slice:

1. Hide full cover: non-visible row → paint and selectable exclude; prove with `visualCache` still holding the note.
2. Synthetic commit rebuild (`changedOverlapNoteIds` cleared, row still non-visible) — paint still excludes.
3. **C9 contract (step 2):** visible shortened — paint includes stub; inventory mask unchanged (`203805` shorten).
4. Leave-restore — mover past committed span — paint shows committed baseline (RC10h).

---

## Verification gate (after fix)

- `pio test -e native` — new fixtures above.
- HITL: repeat `203805` short-note full-cover + commit; DISP `frameNotes` should not include hidden id; grid visually empty at overlap tick.
- Update parent plan §8 task **6.5** and cross-link when HITL passes.

---

## Pre-implementation review

### Ready

- Log proves HideNote + inventory drop + DISP/cache mismatch.
- Owner for paint: `projectNoteEditDisplayNotes` / `collectProjectionParticipantNoteIds`.
- Contract target: **visibility projection contract** + C4 display participant routing (not Option C).

### Resolved (design)

| Topic | Decision |
|-------|----------|
| Fix shape | Visibility gate + C4 display slice — not `if (presence==Hidden)` on one path only |
| `visualCache` on commit | **No** — projection owner; Stage 8 drops cache-as-paint-base |
| `changedOverlapNoteIds` | Migrate display consumer; cache removal §11 step 5 only |
| Leave-restore paint | RC10h; step 3 HITL |

### Open before coding

1. Confirm `collectProjectionParticipantNoteIds` call sites — only display projection, or shared with inventory path (inventory already filtered separately).
2. Empty `noteEditCurrentState` fallback — keep `changedOverlapNoteIds` path when no current state (pre-session / legacy rebuild).

### Regression (`session_20260807_215126`)

First implementation replaced the full participant set with `collectOverlapParticipantNoteIdsFromCurrentState` and dropped the `rowProjectsToStore` supplement. After macro commit seals `currentSpan == committedSpan`, span-matched movers fell out of the participant set and painted from stale `visualCache` (DNTE length 448 ↔ 47 flicker). **Fix:** legacy latch + Hidden/Deleted current-state supplement + restore `rowProjectsToStore` loop; keep visibility gate only.

### Proceed?

- **YES** for implementation — no ownership or transition change; extends existing `collectOverlapParticipantNoteIdsFromCurrentState` + display projection owner.
