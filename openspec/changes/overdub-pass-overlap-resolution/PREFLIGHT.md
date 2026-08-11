# PREFLIGHT — Overdub overlap encode (C → A)

**Change:** `overdub-pass-overlap-resolution`  
**Date:** 2026-08-12  
**Mode:** Full (formal trigger: undo semantics for one logical overdub unit spanning OverdubPass + EditPass rows)  
**User decision:** Option 1 — C → A (pending buffer → seal into existing representations)

---

## Problem

Overdub must resolve each inserted overlapping note against a stable `overdubSourceView` and commit a **complete delta** (Add + Shorten/Hide). Today `OverdubPass` holds only capture chunks; Shorten/Hide already exist as `EditPass` rows. Encode must reuse those representations, keep one logical undo per stopped overdub session, and not contradict stop-time overlap restore.

## Domain

Timeline / capture / undo (overdub); note-edit geometry reuse (not NOTE_EDIT session ownership).

## Similar historical decisions

### Search locations

- [x] `docs/DECISION_LOG.md` — DEC-020 (mid-pass raw capture), DEC-004 (stop validation), DEC-024 (loop-owned undo direction), DEC-028/029/030 (geometry / current state)
- [x] `openspec/changes/overdub-pass-overlap-resolution/`
- [x] `openspec/specs/edit-session-action-geometry/spec.md`, `timeline-passes`
- [x] `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`

### Relevant findings

| ID / path | Relevance |
|-----------|-----------|
| DEC-020 | Mid-pass sealed capture stays raw bytes — pending ops are runtime-only until stop |
| DEC-004 | Stop-path validation policy — do not invent full validate on stop |
| DEC-024 | Loop-owned undo direction — extend TrackUndo/Loop, no new Manager |
| DEC-028 | Geometry pipeline — reuse free `resolveConstrainedGeometry`, not session-gated `NoteGeometryResolver::resolve` |
| OpenSpec overdub-pass-overlap-resolution | One session → one overdubPass/undo; source immutable; persistence does not resolve |

### Existing owner

- Canonical source: `Loop` (`overdubSourceView`)
- Capture lifecycle: `Track` → `Loop::beginCapture` / commit stop
- Shorten/Hide durable rows: `Loop::saveNoteEditPass`
- Undo push/apply: `TrackUndo::pushOverdubPassAdded` / `applyUndoEntry`
- Geometry decisions: free `resolveConstrainedGeometry` (+ map to EditPass-shaped ops)

### Existing extension point

- `UndoEntry.editPassIds` already on every undo entry (used today by `NoteEditPassClosed`)
- `UndoEntry.passId` already identifies the sealed `OverdubPass`
- `Loop::saveNoteEditPass` already materializes Shorten/Hide
- GUS STK1 scoped-edit extension already serializes `editPassIds` for edit-closed kinds

### Reuse possible

**YES** — extend `OverdubPassAdded` apply/push/reclaim/persistence to honor `editPassIds`; do not invent a new undo kind or OverdubPass op list.

### Architecture review required

**YES** — documented here + `ARCHITECTURE-REVIEW.md` Phase 2. No new top-level Manager. No per-wrap undo.

---

## Loaded docs

- [x] `docs/Runtime/PROJECT_STATE.md`
- [x] `docs/Runtime/CURRENT_WORK.md`
- [x] `docs/DECISION_LOG.md`
- [x] `docs/Authority/ARCHITECTURE_RULES.md`
- [x] `docs/Authority/DELIVERY_RULES.md`
- [x] `openspec/changes/overdub-pass-overlap-resolution/tasks.md`
- [x] Domain: `LOOP_MIDI_STORAGE_AND_VALIDATION.md`, `EditPass.h`, `GlobalUndoStack.h`, `TrackUndo.cpp`, `TrackCaptureInput.cpp` (`finalizePendingNotes`), `PassReclaim.cpp`

---

## Ownership change

| Field | Answer |
|-------|--------|
| Current owner | `Loop` (passes / editPasses / source view); `TrackUndo` (global undo entry) |
| Target owner | Same |
| Transfer needed | **NO** |
| Compat removal | N/A |

Pending overdub-operation buffer is **session state on `Loop`**, not a timeline-pass concept and not a new Manager.

---

## Encode decision (user-pinned): C → A

```text
OVERDUBBING
  ├── raw capture (Add material)
  ├── immutable overdubSourceView
  └── pending overlap operations   ← session state only
        │
     STOP / COMMIT
        ├── OverdubPass chunks     ← additions
        └── EditPass rows          ← Shorten / Hide
        └── one OverdubPassAdded undo grouping both
```

- Do **not** extend `OverdubPass` with Shorten/Hide fields.
- Do **not** write `EditPass` rows mid-session.
- Do **not** treat the pending buffer as a pass.

---

## Q1 — Existing undo representation (pin)

### Today

| Kind | Identifies | Undo action | Redo action |
|------|------------|-------------|-------------|
| `OverdubPassAdded` | `entry.passId` | `disableCapturePass(passId)` only | `enableCapturePass(passId)` |
| `NoteEditPassClosed` | `entry.editPassIds` | `setEditPassState(..., Disabled)` | enable same ids |

Evidence: `TrackUndo::pushOverdubPassAdded` sets only `passId`; `applyUndoEntry` OverdubPassAdded branch does not read `editPassIds`.  
`UndoEntry.editPassIds` already exists on the struct (`include/GlobalUndoStack.h`).  
`collectReferencedPasses` pins only `passId` for `OverdubPassAdded` (`PassReclaim.cpp`).  
GUS STK1 scoped-edit wire serializes `editPassIds` **only** for `NoteEditPassClosed` / `ControlChangeEditPassClosed` (`test_global_undo_stack_persistence` mirror of production pattern).

### Smallest extension (pinned)

**Reuse `UndoEntryKind::OverdubPassAdded` with populated `editPassIds`.**

1. `pushOverdubPassAdded` (or sealed overload) sets `passId` + companion `editPassIds`.
2. Undo: disable capture pass **and** disable listed edit passes (same helpers as edit-closed).
3. Redo: enable both.
4. `PassReclaim::collectReferencedPasses`: also `pinEditPass` for those ids when kind is `OverdubPassAdded`.
5. GUS persistence: extend STK1 (or kind-specific) serialization so `OverdubPassAdded` rows round-trip `editPassIds`.

**Rejected as larger:**

| Alternative | Why not |
|-------------|---------|
| Two stack entries (Overdub + NoteEditPassClosed) | Two U: steps; not one logical undo |
| New `UndoEntryKind` | New kind + persistence + apply switch — unnecessary when `editPassIds` exists |
| Put ops on `OverdubPass` | User forbid unless PREFLIGHT proves EditPass insufficient — EditPass already expresses Shorten/Hide |

**Invariant:** one stopped overdub session → one undo cursor step that reverses additions **and** source transforms.

---

## Q2 — Pass identity / grouping (pin)

- Grouping identity for the logical overdub unit: **`OverdubPass.id`** (`UndoEntry.passId`).
- Companion Shorten/Hide rows: normal `EditPass.id` values listed in **`UndoEntry.editPassIds`**.
- **No new overdub-operation identifier.**
- `EditPass` does **not** require a new `sourceOverdubPassId` field for v1 — undo entry is the grouping authority; loop materialize applies all Active editPasses after capture merges.
- Companion rows use `Loop::saveNoteEditPass` with `EditPassType::Note`. Do **not** open `NoteEditSession`. Do **not** push `NoteEditPassClosed` for these rows.
- `editPassIndex` on companion rows: use a dedicated reserved index for overdub-sealed companions (implementation constant in seal slice) so NOTE_EDIT session indices are not conflated; undo/reclaim key off `editPassIds`, not index.

---

## Q3 — Commit ordering (pin)

Materialize order is capture passes then `editPasses`. Therefore incomplete publish must not be visible as “shortened source without Adds”.

**Required order at overdub stop (after pending notes finalized):**

1. Seal addition chunks from capture (`sealCapture` → pending capture pass) — existing.
2. Publish `OverdubPass` (`commitPendingCapturePass`) — Adds become Active.
3. Seal pending overlap ops → `Loop::saveNoteEditPass` Shorten/Hide rows (Active).
4. Push **one** `OverdubPassAdded` with `passId` + `editPassIds`.
5. Clear pending-op buffer + `overdubSourceView` (session end).

**Cancel / discard overdub:** clear pending-op buffer with capture discard; write **no** EditPass rows; push **no** undo.

**Atomicity rule:** never push `OverdubPassAdded` until companion EditPass ids (if any) are allocated; never leave Active companion EditPasses without the matching OverdubPass published in the same stop path.

---

## Q4 — `shouldRestoreCommittedOverlapOnOverdubStop` (pin)

### Current behavior (proof)

In `Track::finalizePendingNotes` (`TrackCaptureInput.cpp`), while overdubbing:

- If `shouldRestoreCommittedOverlapOnOverdubStop(loop, note, pendingOnPhaseTick, phaseTick)` is true  
  (`TrackStopTelemetryColdHelpers.cpp`: same pitch sounding in **committed** materialize at on-tick or close-tick),
- then `Loop::removeOpenCaptureNoteOn` runs and the overdub NoteOn is dropped (`overlapCaptureRestored`).

This runs **before** `commitCaptureForStop`. It is a stop-time “drop Add if committed already sounds” heuristic — not canonical geometry Shorten/Hide.

### Authority after Phase 2 path

| Mechanism | Role after pin |
|-----------|----------------|
| Per-note overlap resolution → pending ops | **Authoritative** for Add / Shorten / Hide during overdub |
| `shouldRestoreCommittedOverlapOnOverdubStop` → `removeOpenCaptureNoteOn` | **Must not contradict** authoritative Add |

**Pinned coexistence:** When an overdub session has an established `overdubSourceView` (Phase 1+ sessions), **do not** take the restore/remove branch in `finalizePendingNotes`. Keep NoteOff synthesis for open pending notes. Retire restore-as-authority for those sessions.

Implementation may gate on `loop.hasOverdubSourceView()` (or equivalent session flag) so pre-path behavior is unchanged only if a session somehow lacks a view (should not happen after Phase 1).

Hardware validation of wrap/`183525` must not run until this gate is implemented in the seal/stop slice.

---

## Files affected (estimate)

| Slice | Paths |
|-------|-------|
| Pending buffer + native bridge | `include/Loop.h`, `src/Loop/LoopCapture.cpp` (or new Loop TU), native `test_overdub_*` |
| Stop seal + undo | `TrackCaptureStopCommit.cpp`, `TrackUndo.cpp` / `.h`, `PassReclaim.cpp`, GUS persistence writers |
| Restore gate | `TrackCaptureInput.cpp` (`finalizePendingNotes`) |
| Docs | this PREFLIGHT, design, ARCHITECTURE-REVIEW, DEC-031 |

---

## New abstractions

| Name | Kind | Justification |
|------|------|---------------|
| Pending overdub-operation buffer | Loop session state (struct + vec) | Required session accumulation; **not** a timeline pass |
| (optional) map constrain→EditPass-shaped op | function on Loop / existing geometry helpers | Bridge only — no Manager |

---

## Persistence impact

- **Loop slot SD:** no schema bump expected — OverdubPass chunks + EditPass EPT3 tail unchanged.
- **GUS undo footer:** extend serialization so `OverdubPassAdded` round-trips `editPassIds` (STK1 or kind-specific). Firmware + host undo persistence tests.
- Pending buffer: **not persisted**; DEC-020 mid-pass remains raw capture.

## Undo impact

Extend `OverdubPassAdded` apply/redo/reclaim/persist to include companion `editPassIds`. No new `UndoEntryKind`.

## Migration required

Firmware-only for loop content; GUS wire extension for undo history that includes overdub+edit companions. Old stacks without companion ids remain valid (empty `editPassIds`).

## Architecture review required (summary)

**YES** — Phase 2 gate in `ARCHITECTURE-REVIEW.md`; pins above. No ownership transfer. No wrap→undo.

## Authority conflict check

| Source | Conflicts with architecture or intent? |
|--------|----------------------------------------|
| OpenSpec / tasks | **NO** — C→A realizes complete delta without new pass kind |
| Pending buffer as session state | **NO** — not a Manager; not a timeline pass |
| Extending OverdubPassAdded.editPassIds | **NO** — field already exists; behavior extension documented |

---

## Context summary

1. Encode: C → A (pending ops → OverdubPass chunks + EditPass rows at stop).
2. Pending buffer = session state on Loop; survives wraps; cleared on discard/commit.
3. Undo: one `OverdubPassAdded` with `passId` + `editPassIds`; extend apply/reclaim/GUS.
4. Grouping identity: `OverdubPass.id`; no new op id; no mid-session EditPass writes.
5. Commit order: publish OverdubPass → save EditPass rows → push undo.
6. Restore path: gated off when `overdubSourceView` established — overlap resolution is authoritative.
7. Geometry: free `resolveConstrainedGeometry`; not fake NOTE_EDIT session.
8. Phase 2 first firmware slice: pending buffer + native constrain→pending ops tests; seal/undo/restore gate next.
9. Tests: native matrix Add/Shorten/Hide/multi-wrap/immutability; then GUS + stop integration.
10. DEC-031 records this encode/undo/restore pin.
