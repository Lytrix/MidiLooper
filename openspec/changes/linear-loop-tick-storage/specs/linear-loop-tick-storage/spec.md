## ADDED Requirements

### Requirement: Canonical storage invariants

Loop MIDI event storage SHALL satisfy these invariants after **`normalizeAll`** completes at a **macro commit boundary**:

1. Every **closed note span** owns exactly one NoteOn event (identified by **NoteId** on the NoteOn).
2. Every **closed note span** owns exactly one NoteOff event paired to that NoteOn / NoteId.
3. `0 <= NoteOn.tick < loopLength`.
4. `NoteOff.tick >= NoteOn.tick`.
5. Storage never wraps; linear spans MAY extend beyond `loopLength`.
6. Storage SHALL NOT contain wrapped geometry (no canonical head NoteOff paired with tail NoteOn).
7. Note length SHALL be derived as `NoteOff.tick - NoteOn.tick`; length SHALL NOT be stored as a separate authoritative field in canonical storage.

NoteOff.tick SHALL NOT exceed `loopLength + BAR_TICKS` (persisted cap).

#### Scenario: Linear span crosses loop boundary

- **WHEN** a note has `NoteOn.tick = 1344`, `NoteOff.tick = 1536`, and `loopLength = 1536`
- **THEN** canonical storage retains `NoteOff.tick = 1536` (not `0`)
- **AND** derived length equals `192`

#### Scenario: Invariant 7 after move at commit boundary

- **WHEN** a note is moved by Δ ticks on both on and off events and the transaction commits with normalize
- **THEN** `NoteOff.tick - NoteOn.tick` equals the pre-move length

### Requirement: Dual normalization boundary rule

The system SHALL provide `normalizeWindow` and `normalizeAll` with **distinct scopes**:

| API | Boundary | Scope | Persistent canonical? |
|-----|----------|-------|------------------------|
| `normalizeWindow` | Micro: `publishDependentFaderLatch` (after staged pipeline) | Edit closure set (see below) | **No** — local geometric consistency for interaction and projection |
| `normalizeAll` | Macro: `commitAllPendingNoteEditActions`; loop length change | Full store | **Yes** — authoritative canonical state, undo snapshots, pass readers |

`normalizeAll` at macro boundaries MUST NOT be skipped. `normalizeWindow` MUST NOT be the sole authority for persistent canonical state.

Neither API SHALL run during mid-pipeline stages. Neither SHALL generate playback or display artifacts directly.

#### Scenario: Micro normalize at fader latch

- **WHEN** a fader geometry batch completes and `publishDependentFaderLatch` runs
- **THEN** `normalizeWindow` runs on the edit closure set before fader outbound
- **AND** the closure set satisfies linear geometry for live interaction
- **AND** full-store canonical invariants MAY still fail until macro commit

#### Scenario: Macro normalize at edit commit

- **WHEN** `commitAllPendingNoteEditActions` runs
- **THEN** `normalizeAll` runs before undo snapshot push and pass-commit readers
- **AND** the session store satisfies invariants 1–7

#### Scenario: No normalize mid-pipeline

- **WHEN** a staged geometry pipeline (e.g. `moveNote` stages 2–5) is in progress
- **THEN** neither `normalizeWindow` nor `normalizeAll` runs between pipeline stages
- **AND** each stage uses the working copy from the prior stage or transaction-start snapshot

### Requirement: Edit closure set (normalizeWindow scope)

`normalizeWindow` SHALL operate on the **edit closure set**, computed deterministically from the geometry transaction:

**Seed:** all `NoteId` values modified in the transaction.

**Closure expansion:**

1. Directly edited notes (moved, resized, deleted)
2. Paired `NoteOn` / `NoteOff` for each seed note
3. Overlap participants from overlap resolution triggered by the edit
4. Wrap-boundary interactors whose temporal relationships change due to loop-boundary interaction
5. Adjacent dependency range — structural adjacency only when required for stable reconstruction (not UI margin)

The closure set MUST NOT depend on UI selection, focus index, piano-roll window position, or arbitrary tick ranges.

#### Scenario: Closure includes overlap participant

- **WHEN** moving a note triggers shorten on an overlap note
- **THEN** the overlap note's `NoteId` is in the closure set
- **AND** `normalizeWindow` may rewrite that note's linear ticks

#### Scenario: Closure excludes unrelated loop region

- **WHEN** a note at tick `100` is edited and no other notes in ticks `5000+` participate in overlap or wrap interaction
- **THEN** notes at `5000+` are not in the closure set
- **AND** `normalizeWindow` does not modify them

### Requirement: Normalization conversion rules

`LoopTickNormalize` SHALL be the **only** subsystem that mutates event ticks for normalization. Conversion rules are **pure transforms** inside `normalizeWindow` / `normalizeAll`. They SHALL NOT run during active pipeline stages or on SD load.

`normalizeAll` on loop **shorten** SHALL NOT delete notes with `NoteOn.tick >= newLoopLength`; projection-only hide applies per loop-wrap-projection.

Running the same normalize API twice on unchanged input SHALL be **idempotent**.

#### Scenario: Wrap-pair storage merges to linear span

- **WHEN** storage has `NoteOn.tick = 1400`, `NoteOff.tick = 50`, `loopLength = 1536` (head/tail wrap pair)
- **AND** `normalizeWindow` runs at a commit boundary
- **THEN** canonical storage has one `NoteOn@1400` and one `NoteOff@1586` (`1400 + 186`)
- **AND** the head `NoteOff@50` event is removed

#### Scenario: Synth loop-end off promoted to linear off

- **WHEN** storage has `NoteOn.tick = 1487`, `NoteOff.tick = 1535` (synth at `loopLength - 1`) and no separate head off
- **AND** `normalizeWindow` runs at commit
- **THEN** canonical storage has `NoteOff.tick = 1536` (linear, `on + length`)
- **AND** no authoritative length remains at `loopLength - 1` only

#### Scenario: Open capture tail closed at stop

- **WHEN** storage has `NoteOn` only (no `NoteOff`) for a closed span at capture stop
- **AND** `normalizeWindow` runs with `openTailCloseTick` set to the stop playhead tick
- **THEN** normalize assigns a linear `NoteOff.tick = NoteOn.tick + derivedLength` using stop-derived length (not synth `loopLength - 1` as sole authority)

#### Scenario: Already-linear span is idempotent

- **WHEN** storage has `NoteOn.tick = 1344`, `NoteOff.tick = 1536`, `loopLength = 1536`
- **AND** `normalizeAll` runs twice
- **THEN** ticks are unchanged after the first pass
- **AND** the second pass makes no modifications

#### Scenario: normalizeAll on loop length change

- **WHEN** `loopLength` shortens from `3072` to `1536` and a note has `NoteOn.tick = 2000`
- **AND** `normalizeAll` runs at the length-change commit boundary
- **THEN** the `NoteOn` and paired `NoteOff` events remain in storage unchanged
- **AND** projection omits the note until loop length includes `2000` again

### Requirement: Canonical mutation vs projection separation

All canonical storage write paths (note-edit geometry, capture stop, edit pass commit) SHALL mutate ticks in **linear tick-space** only. They MUST NOT apply `% loopLength` or write display-derived end ticks into `MidiEvent.tick`.

Display, editor wrapping, fader display anchors, and `% loopLength` transforms SHALL reside in the **projection layer** and MUST NOT mutate canonical storage or drive normalization scope.

#### Scenario: Move writes linear off

- **WHEN** `moveNote` updates storage at the end of the staged pipeline
- **THEN** `NoteOff.tick = NoteOn.tick + length` with no modulo
- **AND** wrapped display segments are produced only by `reconstructNotes`

#### Scenario: Projection does not normalize

- **WHEN** `reconstructNotes` or fader snapshot builders run
- **THEN** canonical `MidiEvent` ticks are unchanged

### Requirement: Unified validation check registry

The system SHALL provide `LoopEventValidation` with a **check registry** (`LoopEventCheck` enum + `validateLoopEvents(store, loopLength, checkMask)`). Each canonical invariant and integrity condition SHALL be a distinct check.

**Validate is assert-only:** checks are pure boolean predicates. `validateLoopEvents` SHALL NOT mutate storage, SHALL NOT call `LoopTickNormalize`, and SHALL NOT repair geometry.

SD load SHALL use the canonical invariant check mask and **reject** on failure. `validateAndCleanupMidiEvents` SHALL use the same registry; it MAY remove orphan events but MUST NOT rewrite note geometry or reintroduce wrap-pair storage.

Canonical check mask SHALL include: `NoteOnInLoopRange`, `LinearNoteOff`, `NoWrappedPairStorage`, `DerivedLength`, `NoteIdPairing`, `PersistedTickCap`.

#### Scenario: NoteOnInLoopRange fails

- **WHEN** a closed span has `NoteOn.tick >= loopLength`
- **THEN** `NoteOnInLoopRange` fails
- **AND** validate returns `firstFailure = NoteOnInLoopRange`

#### Scenario: LinearNoteOff fails on wrapped off

- **WHEN** a paired `NoteOff.tick < NoteOn.tick` with both ticks in `[0, loopLength)` (legacy wrap-pair off)
- **THEN** `LinearNoteOff` fails

#### Scenario: NoWrappedPairStorage fails

- **WHEN** storage contains tail `NoteOn` in the wrap window and head `NoteOff` in the head window satisfying `isHeadTailWrappedPair`
- **THEN** `NoWrappedPairStorage` fails

#### Scenario: NoteIdPairing fails

- **WHEN** a `NoteOn` has `noteId != 0` but no `NoteOff` shares that `noteId`, or two `NoteOn` events share the same `noteId`
- **THEN** `NoteIdPairing` fails

#### Scenario: PersistedTickCap fails

- **WHEN** `NoteOff.tick > loopLength + BAR_TICKS`
- **THEN** `PersistedTickCap` fails

#### Scenario: OrphanNoteOff repair allowed on idle only

- **WHEN** deferred idle finds a `NoteOff` with no matching `NoteOn` and no valid wrap-pair tail ahead
- **THEN** `validateAndCleanupMidiEvents` MAY remove that event
- **AND** canonical geometry checks are not used to rewrite paired ticks

#### Scenario: Load rejects non-canonical storage

- **WHEN** a loop slot file fails a canonical invariant check (e.g. wrapped NoteOff, synth loop-end off without linear end)
- **THEN** SD load fails with a logged reject reason
- **AND** no normalize runs on load

#### Scenario: Validate after normalize passes

- **WHEN** storage was just normalized at a **macro** commit boundary (`normalizeAll`)
- **THEN** canonical invariant checks succeed

#### Scenario: Idle cleanup uses same checks

- **WHEN** deferred idle runs `validateAndCleanupMidiEvents`
- **THEN** it uses the shared check registry
- **AND** orphan removal does not change NoteOn/NoteOff ticks of paired notes

### Requirement: Open notes during capture

During live **Capture**, events MAY lack a NoteOff until stop. After capture stop and **normalize at commit boundary**, committed pass data for **closed** notes SHALL satisfy invariants 1–7. Open tails SHALL receive linear NoteOff ticks (not synth `loopLength - 1` as sole length authority).

#### Scenario: Stop normalizes open capture tail

- **WHEN** overdub stops with a note still open at the loop tail
- **THEN** normalize at stop assigns a linear NoteOff before pass commit
- **AND** committed pass satisfies invariants for that closed span

### Requirement: Dev SD reset for clean development state

The system SHALL provide `resetDevelopmentPersistence` (serial `!DEV_RESET_SD` under SESSION_CAPTURE) that wipes dev SD state and resets RAM indexing. Run once when adopting linear-tick storage; not a recurring migration.

#### Scenario: Dev reset wipes sets and current workspace

- **WHEN** operator sends `!DEV_RESET_SD` with no active revision commit/load job
- **THEN** `/MidiLooper/sets/` and `/MidiLooper/current/` are cleared
- **AND** RAM loop pools, SlotSummary, workspace metadata, and undo stacks are reset
- **AND** serial emits `#CAP,PERS,dev_reset_sd,…,ok`

#### Scenario: Dev reset blocked during revision job

- **WHEN** a revision commit or load job is active
- **THEN** `!DEV_RESET_SD` fails without partial wipe

### Requirement: Regression matrix acceptance

The system SHALL satisfy these acceptance scenarios (native and/or HITL):

#### Scenario: Move across loop end preserves length

- **WHEN** a note spanning the loop boundary is moved past `loopLength - 1` and the transaction commits
- **THEN** canonical storage preserves `NoteOff.tick - NoteOn.tick`
- **AND** projection shows tail and head segments

#### Scenario: Resize across loop end preserves length

- **WHEN** note length is changed across the loop boundary and the transaction commits
- **THEN** invariant 7 holds on the canonical pair

#### Scenario: Extend loop length preserves stored length

- **WHEN** loop length increases after a note was stored with linear off beyond the old boundary
- **THEN** derived length in canonical storage is unchanged
- **AND** display does not inflate solely due to a synth loop-end off

#### Scenario: Shorten loop omits projection but retains storage

- **WHEN** loop length shortens below a note's `NoteOn.tick`
- **THEN** projection omits that note from playback/display
- **AND** canonical storage retains the note events unchanged

#### Scenario: Lengthen loop restores projected notes

- **WHEN** loop length later increases to include a previously hidden note's `NoteOn.tick`
- **THEN** projection shows the note again without re-recording

#### Scenario: Undo after wrapped edit restores linear span

- **WHEN** undo restores a pre-edit snapshot after a wrapped geometry edit
- **THEN** the restored NoteOn/NoteOff ticks match the snapshot linear span

#### Scenario: Save and load round-trip post-reset

- **WHEN** a canonical loop is saved and loaded after dev reset
- **THEN** NoteOn and NoteOff ticks match exactly

#### Scenario: Overdub wrapped note single linear span

- **WHEN** overdub records a note crossing the loop boundary and stop commits
- **THEN** canonical storage has one linear span per NoteId (not wrap-pair storage)

#### Scenario: Same-pitch overlap preserves pairing

- **WHEN** moving note overlap affects same-pitch neighbors and the transaction commits
- **THEN** each affected NoteId retains one on/off pair satisfying invariants

#### Scenario: Playback reconstruction unchanged

- **WHEN** canonical linear storage is projected for playback
- **THEN** audible note-on/off timing matches baseline for equivalent geometry

#### Scenario: Display reconstruction wrap correct

- **WHEN** canonical linear storage is projected for display
- **THEN** wrapped tail/head segments match expected DisplayNote ranges
