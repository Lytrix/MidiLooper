# Prior art — EditSessionAction geometry

**Date:** 2026-07-04  
**OpenSpec:** [`openspec/changes/edit-session-action-geometry/`](../../openspec/changes/edit-session-action-geometry/)  
**Status:** Decisions recorded (Q1–Q16)

---

## Decision log

| Q | Decision |
|---|----------|
| **Q1** | **Yes** — geometry parity classify fixtures in `test_edit_session_interaction` |
| **Q2** | **Yes** — prior art comparison table in `design.md` (plain language) |
| **Q3** | **Cross-pitch in scope** + **D21** pitch-lane re-eval on pitch change |
| **Q4** | **Add follows Move logic** |
| **Q5** | **NOTE_EDIT minimum note edit length hide** uses **`noteMinLengthTicks`** + **`noteMinLengthRemoveEnabled`** (same runtime globals as capture Q16) |
| **Q6** | Same tick: **note-on keeps tick**; **note-off → on−1** |
| **Q7** | **Locked A** — D10 for all **BoundaryTouch** |
| **Q8** | **No** batch cleanup at macro commit |
| **Q9** | Follow-on OpenSpec **`capture-pass-boundary-materialization`** |
| **Q10** | NOTE_EDIT pairing § in loop MIDI guide |
| **Q11** | Reuse Lytrix wrap test helpers |
| **Q12** | **Always apply** v1 |
| **Q13** | **MergeNote** future — see `design.md` |
| **Q14** | **Poly shorten deferred** |
| **Q15** | Keep **`OverlapNoteOn` / `OverlapNoteOff`** |
| **Q16** | **Locked** — **`noteMinLengthTicks`** (default **12**) + **`noteMinLengthRemoveEnabled`** (default **on**); hot stop after `LoopStopFinalize` |

---

## Q16 — Capture NoteMinLength (locked)

| Item | Value |
|------|--------|
| Runtime | **`noteMinLengthTicks`** + **`noteMinLengthRemoveEnabled`** |
| Defaults | **12 ticks**, removal **enabled** |
| Disable | **`noteMinLengthRemoveEnabled = false`** — skip short-pair removal on hot stop |
| When | **Hot stop** — record/overdub stop, after `LoopStopFinalize`, before publish |
| Action | Remove completed pairs with linear span **&lt; `noteMinLengthTicks`** |
| Not | NOTE_EDIT 32nd floor; not macro **`noteEditPass`**; not idle-only v1 |

**Dedup vs NoteMinLength:**

| | Live dedup | Hot-stop NoteMinLength |
|--|------------|------------------------|
| When | Each `appendCaptureEvent` | Once at stop |
| Rule | Drop duplicate **event** within 12t | Drop **pair** if span &lt; `noteMinLengthTicks` |
| Config | Fixed `DUPLICATE_TICK_TOLERANCE` | User threshold + **enable flag** |

Handoff: [`capture_pass_note_min_length_refinement.md`](capture_pass_note_min_length_refinement.md)

---

## Q8 — Macro commit vs micro cleanup

Micro **EditSessionAction** owns NOTE_EDIT each tick. Macro **`noteEditPass`** = baseline diff audit only. No Helio/LMMS second pass at commit.

Capture **NoteMinLength** is a **third tier** (hot stop) — separate from both.

---

## Capture tiers (summary)

| Tier | When | Role |
|------|------|------|
| Live dedup | Append | Duplicate events within 12t |
| Hot stop | Record/overdub stop | Pending offs, wrap finalize, **NoteMinLength (Q16)** |
| Cold idle | Deferred validate | Orphan on/off removal |

---

## Q5 — NOTE_EDIT vs capture minimum note length

| Context | Floor |
|---------|--------|
| NOTE_EDIT **minimum note edit length hide** (D16 step 5) | **`noteMinLengthTicks`** when **`noteMinLengthRemoveEnabled`**; skip when disabled |
| Capture **NoteMinLength** hot stop (Q16) | Same runtime globals on record/overdub stop |

**Supersedes** earlier “NOTE_EDIT 32nd (24t) fixed floor” wording — brownfield **`49`**-tick magic in **`NoteMovementUtils`** is **retired** with the pipeline wire.

---

## Patched artifacts

- [`design.md`](../../openspec/changes/edit-session-action-geometry/design.md)
- [`LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md)
- [`capture_pass_note_min_length_refinement.md`](capture_pass_note_min_length_refinement.md)
- `include/Globals.h` — `DEFAULT_NOTE_MIN_LENGTH_*`, `noteMinLengthTicks`, `noteMinLengthRemoveEnabled`

Implementation of `removePairsShorterThanNoteMinLength` → capture follow-on OpenSpec (Q9).

---

## Architecture review additions (2026-07-04 post-review)

Recorded in [`design.md`](../../openspec/changes/edit-session-action-geometry/design.md) — summary:

| # | Topic | Decision |
|---|-------|----------|
| 1 | Analyzer independence (D17) | Orchestrator owns changed causing notes + pair eligibility; analyze is pure geometry |
| 2 | D19 rename | **Edit driver boundary** (not “edit step boundary”) |
| 3 | Interaction grouping (D15) | **`groupEditSessionInteractionsByTarget`** → **`EditSessionInteractionsByTarget`**; row **`TargetNoteInteractionGroup`**; avoid **\*Set** (CurrentSet/SavedSet) |
| 4 | **Edit session action builder** | Observe constrained geometry + baseline + live store; compute minimal **`EditSessionActions`**; **omit unchanged live store actions** |
| 5 | Positive graph | Omitted pairs are meaningful; no **None** sentinel |
| 6 | Restore | Consequence of no incoming interactions + baseline-equivalent geometry + live store compare — not imperative |
| 7 | **`ConstrainedNoteGeometry`** | Sole desired-geometry unit; ideal test boundary |
| 8 | Invariant | **Constrained Geometry Authority** — builder must not read interactions |
| 9 | **`ResolutionPolicy`** | Stay removed (D22) |
| 10 | **\*Set** suffix | Avoid in pipeline types — **CurrentSet** / **SavedSet** collision |
| 11 | Omit unchanged live store actions | Edit session action builder |
| 12 | Constrained geometry target notes + precedence | **Interaction target notes** + **overlap restore candidate notes**; full **combine precedence** chain |
| 13 | Selected-to-selected overlap when geometry changed | **Deferred** — multi-select length future |
| 14 | No **Constraint** type | **`EditSessionInteraction`** + **`resolveConstrainedGeometry`** math only |
| 15 | Central algorithm | **Constrained geometry resolution** — combine interactions → **`ConstrainedNoteGeometry`** |
| 16 | Derived geometry philosophy | **`visible`** / actions derived per tick — no overlap scratch state machines |
| 17 | Rejected draft labels | Diff Builder, Executor, Constraint Set — use locked § Terminology |
| 18 | **`computeShortenedEndTick`** | Per **OverlapNoteOff**: linear **note-off tick** (not length); **`min`** in **restrictive shorten combine**; length floor in step 5 |
