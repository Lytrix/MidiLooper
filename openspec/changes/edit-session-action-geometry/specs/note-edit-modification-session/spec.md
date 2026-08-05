## MODIFIED Requirements

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

### Requirement: Macro commit produces one noteEditPass batch

At **`commitAllPendingNoteEditActions`**, the system SHALL emit **one `noteEditPass` batch** with **`EditPass` rows for every changed `NoteId`** (mover, overlap hide/shorten/restore, add, delete) from **transaction baseline compared to final live store**. **`overlapNotes`** MUST NOT be the row source.

#### Scenario: Overlap rows from baseline diff

- **WHEN** macro commit runs after a move that hid an overlap note
- **THEN** **`EditPass`** rows for the mover and hidden target are derived from baseline diff
- **AND** **`buildPreCommitOverlapEditPasses`** from **`overlapNotes`** is not used

#### Scenario: Apply-owned rows do not own macro commit

- **GIVEN** apply-owned diagnostic rows exist for the open NOTE_EDIT session
- **WHEN** macro commit runs
- **THEN** the persisted **`noteEditPass`** batch is serialized from transaction baseline compared to final live store
- **AND** apply-owned diagnostic rows are not used as persistence authority

### Requirement: Overlap and focus use derived length at macro commit

After **`normalizeAll`** at macro commit, focus moving-note length SHALL equal linear pair length for **`EditorSelection.primaryNote`**.

**`movingNoteRange`** is **retired**.

#### Scenario: Macro commit refreshes mover length from linear pair

- **WHEN** macro commit normalizes a moved NOTE_EDIT session
- **THEN** the moving-note length is read from the canonical linear note-on/off pair
- **AND** **`movingNoteRange`** is not used as length authority
