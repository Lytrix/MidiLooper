## Why

NOTE_EDIT geometry (move, length, pitch, add, delete) is implemented as an imperative chain in [`NoteMovementUtils`](../../../src/Utils/NoteMovementUtils.cpp): restore overlap notes, detect overlaps, shorten/hide, move, restore again — often mutating the session store in the same call stack. **`overlapNotes` scratch** carries frame-to-frame state, so ordering bugs and restoration chains accumulate with every bugfix.

The editor should answer one question per geometry update:

> Given the **immutable transaction baseline** and **current edited geometry** (**`EditorSelection`** + linear causing spans for selected notes), what should every affected note look like **right now**?

Not: undo the previous frame and patch toward the next.

This is a **design-session change** (ownership + state transitions). Central algorithm: **`resolveConstrainedGeometry`** combines interactions into derived **`ConstrainedNoteGeometry`** — no persistent overlap scratch. Complements **`linear-loop-tick-storage`** (canonical ticks + normalize/validate).

Brownfield: [`NoteEditFocus`](../../../include/NoteEditFocus.h), [`EditorSelection`](../../../include/NoteEditSessionState.h), [`NoteMovementUtils`](../../../src/Utils/NoteMovementUtils.cpp), DEC-013/014.

## What Changes

- **`EditSessionAction` pipeline** — `normalizeWrapToLinear` → `analyzeEditSessionInteractions` → `groupEditSessionInteractionsByTarget` → `resolveConstrainedGeometry` → `buildEditSessionActions` → `applyEditSessionActions`
- **Recompute, not unwind** — interactions regrouped by target each tick; restore when constrained geometry matches baseline
- **Transaction baseline** — **`baselineMap`** at **edit driver boundary** (primary driver **`NoteId`**); v1 full loop; **`overlapNotes` removed**
- **Macro commit** — one **`noteEditPass` batch** with **`EditPass` row per changed `NoteId`** (baseline diff)
- **Add/Delete** — selected new note triggers overlap; removing causing note restores hidden targets via rebuild
- **Cross-session shape** — same pipeline for future Loop/CC; NOTE_EDIT ships first
- **Native tests** — interaction, resolver, builder, apply without `Track` / Arduino

## Capabilities

### New

- **`edit-session-action-geometry`**: Interaction analysis (pure), interaction grouping by target, constrained geometry resolver, **edit session action builder**, **edit session action apply**

### Modified

- **`note-edit-modification-session`**: Replace staged restore/mutate pipeline; retire **`overlapNotes`**, **`movingNoteRange`**
- **`note-edit-session-undo`**: Session undo unchanged contract; new apply path
- **`note-edit-fader-feedback`**: Fader latch reads post-apply + micro-normalize geometry (brownfield: empty-step F2–F4 motor sync off + NOTELEN grace fix shipped pre-pipeline — see `design.md` § Brownfield interim)
- **`linear-loop-tick-storage`**: **Edit session action apply** writes linear ticks; invariant gates at normalize boundaries

## Impact

- **New modules:** `include/EditSessionAction.h`, `src/EditSessionInteraction.cpp` (includes **`groupEditSessionInteractionsByTarget`**), `src/ResolveConstrainedGeometry.cpp`, `src/EditSessionActionBuilder.cpp`, `src/ApplyEditSessionActions.cpp`
- **Retire:** restore-first paths in `NoteMovementUtils.cpp`; **`overlapNotes`**; **`movingNoteRange`**
- **Wire:** `NoteEditManager` / `EditManager` geometry entry points call pipeline
- **Tests:** `test_edit_session_interaction`, `test_resolve_constrained_geometry`, `test_edit_session_action_builder`, `test_apply_edit_session_actions`
- **Docs:** [`docs/plans/note_edit_session_action_geometry_enhancement.md`](../../../docs/plans/note_edit_session_action_geometry_enhancement.md)

## Non-Goals

- Loop/jam/CC action variants in first implementation
- Chunk-scoped baseline v1 (full loop)
- Capture/overdub boundary pass materialization (noted as follow-on in D10)
- SD format changes

## Delivery Sequence

| Phase | Work |
|-------|------|
| **0** | Design + OpenSpec — user approval |
| **1** | Analyze + group by target + resolver + native tests |
| **2** | `buildEditSessionActions` + native tests |
| **3** | `applyEditSessionActions` + invariant gates + native tests |
| **4** | Wire move/length/pitch/add/delete; retire scratch |
| **5** | HITL regression matrix + archive |
