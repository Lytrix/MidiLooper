## ADDED Requirements

### Requirement: NOTE_EDIT transaction consistency

Within a single NOTE_EDIT geometry transaction, all sub-steps (overlap detection, reconstruction, deletion, shortening, and geometry mutation) SHALL operate on either:

1. A **stable snapshot** of transaction state at transaction start, or  
2. An **explicitly updated working copy** advanced by a **defined staged transformation pipeline**.

No sub-step SHALL observe partially applied mutation results from another sub-step except through such a staged pipeline. External readers (undo, committed fader outbound, next transaction, canonical playback merge) SHALL NOT read the store between pipeline stages.

#### Scenario: Staged moveNote pipeline

- **WHEN** `moveNote` runs its ordered stages (overlap restore → projection/overlap detect → shorten/delete → mover update)
- **THEN** each stage reads the working copy produced by the prior stage
- **AND** normalize does not run between stages
- **AND** no external reader observes the store until micro/macro commit boundaries

#### Scenario: Projection read within pipeline

- **WHEN** overlap detection calls `reconstructNotes` during stage 3 of `moveNote`
- **THEN** it projects from the current working copy after stage 2
- **AND** this is not treated as an external partial-state read

#### Scenario: External reader blocked mid-pipeline

- **WHEN** `moveNote` is between pipeline stages 2 and 5
- **THEN** undo snapshot, macro commit, and materialized playback merge do not run

### Requirement: Dual normalization boundaries for NOTE_EDIT

NOTE_EDIT geometry uses two normalization boundaries:

| Order | Event | API | Purpose |
|-------|-------|-----|---------|
| 1 | Staged pipeline completes | — | Linear off writes on working copy |
| 2 | `publishDependentFaderLatch` | `normalizeWindow(closureSet)` | Local geometric consistency; fader latch |
| 3 | `commitAllPendingNoteEditActions` | `normalizeAll` | Persistent canonical store; undo snapshots |

`normalizeWindow` scope is the **edit closure set** (seed `NoteId`s + paired events + overlap participants + wrap interactors). It MUST NOT use UI window or selection as scope.

#### Scenario: Micro normalize before fader latch

- **WHEN** fader geometry batch completes after the staged pipeline
- **THEN** `normalizeWindow` runs on the edit closure set before `publishDependentFaderLatch`
- **AND** fader outbound reads post-window-normalize geometry

#### Scenario: Macro normalize before undo

- **WHEN** `commitAllPendingNoteEditActions` runs
- **THEN** `normalizeAll` runs before undo snapshot push and edit pass commit readers
- **AND** invariants 1–7 hold on the full session store

### Requirement: Linear off writes (canonical mutation)

All NOTE_EDIT geometry mutations (`moveNote`, start/position edit, length edit, pitch edit) SHALL write `NoteOff.tick = NoteOn.tick + length` without `% loopLength` on mutation. Pitch edit SHALL NOT change ticks.

#### Scenario: Linear off on length edit

- **WHEN** note length is changed during NOTE_EDIT
- **THEN** `NoteOff.tick` is set to `NoteOn.tick + newLength` without `% loopLength`

#### Scenario: Start edit preserves length

- **WHEN** position/start edit moves `NoteOn` by Δ ticks
- **THEN** `NoteOff.tick` moves by the same Δ

#### Scenario: Pitch edit is tick no-op

- **WHEN** pitch edit changes note number only
- **THEN** `NoteOn.tick` and `NoteOff.tick` are unchanged

### Requirement: Overlap and focus use derived length at macro commit

Overlap scratch and focus moving-note range SHALL derive length from invariant 7 on canonical on/off ticks **after `normalizeAll`**. Within an active transaction, overlap stages MAY use projected DisplayNote coordinates from the working copy.

#### Scenario: Focus length after wrap move at macro commit

- **WHEN** a wrapped note move macro-commits with `normalizeAll`
- **THEN** focus moving-note length equals `NoteOff.tick - NoteOn.tick` on the canonical pair for that NoteId
