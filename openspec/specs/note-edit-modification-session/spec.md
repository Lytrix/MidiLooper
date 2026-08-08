## Purpose

During **NoteEditSession**, move / length / pitch / add / delete geometry, macro commit, and overlap
policy are owned by the **EditSessionAction** pipeline (`runEditSessionGeometryPipelineForCausingNote`)
with **`NoteEditFocus`** transaction baseline and **`EditorSelection`** as inputs. Shipped in
**note-edit-modification-session** (2026-06) and **edit-session-action-geometry** (archived 2026-08-05).
Supersedes restore-first **`applyNoteEditChange`** overlap chains.

**Related specs:** [`edit-session-action-geometry`](edit-session-action-geometry/spec.md),
[`note-edit-session-undo`](note-edit-session-undo/spec.md). **Display-only** loop wrap:
[`docs/Guides/NOTE_WRAPPING_LOGIC.md`](../../docs/Guides/NOTE_WRAPPING_LOGIC.md).

**HITL:** Full D14 per-interaction regression matrix is **parked** —
[`docs/Plans/m8_edit_note_edit_hitl_automation_refinement.md`](../../docs/Plans/m8_edit_note_edit_hitl_automation_refinement.md).
Interim smoke: `edit_minimal` preset.

## Requirements

### Requirement: NOTE_EDIT transaction consistency

Within a single NOTE_EDIT geometry update, the system SHALL:

1. Read **transaction baseline** (**`baselineMap`**, **`commitBaseline`**) as immutable input for the current **edit driver** (D19)
2. Read **edited geometry** — **`EditorSelection`** + linear causing spans (`focus.last` for **`primaryNote`**; see **`edit-session-action-geometry`** spec § Edited geometry)
3. **Orchestrator** — determine changed causing notes and eligible pairs (D17)
4. Run **Edit projection** (D20) — **`buildEditProjectionContext`** + **`projectEditIntervalsForAnalysis`** — before analysis
5. Run **`analyzeEditSessionInteractions`** (pure; supplied causing inputs only)
6. Run **`groupEditSessionInteractionsByTarget`** (ephemeral; grouped per target)
7. Run **`resolveConstrainedGeometry`** (pure)
8. Run **`buildEditSessionActions`** (edit session action builder; inputs: constrained geometry, edited geometry, transaction baseline, live store; does not mutate live store)
9. Run **`applyEditSessionActions`** (**edit session action apply** — sole live store writer; includes boundary split)
10. Run **`normalizeWindow`** on edit closure at micro boundary only

The system MUST NOT interleave storage mutation with analysis. The restore-first staged pipeline is **retired** in favor of rebuild + **RestoreNote** actions.

#### Scenario: No restore prelude

- **WHEN** move/length/pitch/add geometry runs
- **THEN** `restoreOverlapNotesNoLongerOverlapping` is not called as a first stage
- **AND** any restore is an explicit **RestoreNote** in **`EditSessionActions`**

#### Scenario: External reader blocked mid-pipeline

- **WHEN** analysis, interaction grouping, constrained geometry resolution, and edit session action builder run
- **THEN** **live store** is unchanged until **`applyEditSessionActions`**

### Requirement: Baseline at note select

When the user selects a note with fader 1 during note edit, the system SHALL preserve the active edit driver's read-only transaction baseline map and SHALL set the focus **commitBaseline** from the selected note's committed baseline entry.

Current editable geometry for the selected `NoteId` SHALL be read from note edit current state, not reconstructed from `NoteEditSession.store` or display ordering.

#### Scenario: Overlap notes from committed passes included

- **WHEN** the selected moving note has other notes present in committed materialized state from a **recordPass**
- **THEN** those notes SHALL appear in the transaction baseline map without a separate pass read
- **AND** the selected note's current editable span SHALL come from note edit current state

### Requirement: Live source of truth

During note edit, note edit current state SHALL be the sole mutable source of current editable note geometry and presence. **NoteEditSession.store** SHALL be the canonical MIDI event projection for `editAwareMidiEvents()`, playback preview, serialization, and compatibility during migration.

Hiding an overlap note SHALL mark the row `Hidden` in current state while omitting the projected event pair from **NoteEditSession.store**. **`overlapNotes`** scratch is not commit, filter, or current-geometry authority.

#### Scenario: Incremental current-state mutation

- **WHEN** overlap hides one overlap note via **`applyEditSessionActions`**
- **THEN** only the affected current-state rows change for that action batch
- **AND** **NoteEditSession.store** is refreshed from the canonical projection of those rows

### Requirement: Macro commit produces one noteEditPass batch

At **`commitAllPendingNoteEditActions`**, the system SHALL emit **one `noteEditPass` batch** with **`EditPass` rows for every changed `NoteId`** (mover, overlap hide/shorten/restore, add, delete) from committed baseline compared to note edit current state. **`overlapNotes`** and projected-store pair absence MUST NOT be the row source.

Legacy store-diff builders MAY remain during migration only as parity checks. They MUST NOT change the committed **`EditPass`** output after current-state commit authority is active.

#### Scenario: Overlap rows from current-state diff

- **WHEN** macro commit runs after a move that hid an overlap note
- **THEN** **`EditPass`** rows for the mover and hidden target are derived from current-state rows compared to committed baseline
- **AND** **`buildPreCommitOverlapEditPasses`** from **`overlapNotes`** is not used

#### Scenario: Legacy store diff does not own macro commit

- **GIVEN** legacy store-diff diagnostic output exists for the open NOTE_EDIT session
- **WHEN** macro commit runs
- **THEN** the persisted **`noteEditPass`** batch is serialized from current state compared to committed baseline
- **AND** legacy store-diff diagnostic output is not used as persistence authority

### Requirement: Overlap and focus use derived length at macro commit

After **`normalizeAll`** at macro commit, focus moving-note length SHALL equal the current-state span length for **`EditorSelection.primaryNote`**.

**`movingNoteRange`** is **retired** as length authority.

#### Scenario: Macro commit refreshes mover length from current state

- **WHEN** macro commit normalizes a moved NOTE_EDIT session
- **THEN** the moving-note length is read from the current-state row for **`EditorSelection.primaryNote`**
- **AND** **`movingNoteRange`** is not used as length authority

### Requirement: Focus and baseline maps keyed by NoteId

**NoteEditFocus** SHALL track **movingNoteId**, **`overlapNotes`** (scratch only), and **baselineMap** keyed by **`NoteId`**. **commitBaseline** and overlap restore SHALL preserve **`NoteId`** on restored note-ons.

Current editable geometry and presence SHALL be read from note edit current state. **NoteEditFocus** SHALL NOT become a second current-geometry owner.

#### Scenario: Moving note id stable across pitch edit

- **WHEN** pitch edit changes the moving note's pitch field
- **THEN** **focus.movingNoteId** is unchanged
- **AND** the current-state row for that **`NoteId`** owns the updated pitch

#### Scenario: Focus reads current state after display reorder

- **WHEN** display ordering changes after same-pitch note movement
- **THEN** focus refresh resolves **`EditorSelection.primaryNote`** by **`NoteId`**
- **AND** reads that row's current span from note edit current state

### Requirement: CC and velocity out of overlap scratch

Velocity and control-change edits SHALL NOT use **overlapNotes** for commit authority. They SHALL apply only to the focus moving note (or future **ControlChangeEditSession** scope).

#### Scenario: Velocity change during note edit

- **WHEN** the user changes velocity on the selected note
- **THEN** no overlap note SHALL be added to **overlapNotes** as persistence authority
