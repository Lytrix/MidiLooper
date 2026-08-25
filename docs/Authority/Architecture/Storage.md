# Runtime architecture — capture storage

**Parent:** [RuntimeArchitecture.md](RuntimeArchitecture.md)

**Naming:** [NAMING.md](../NAMING.md) — Capture, passes, merge, materialize, Deferred vs Pending.

---

## Question

> What is the authoritative timeline?

Before snapshot migration, `Loop::passes` and live `Capture` provide the
runtime content boundary. The target authoritative content is the selected
complete immutable `LoopSnapshot`. Everything downstream remains derived;
consumers must not treat materialized or display lists as canonical.

---

## Owners

| Store | Owner | Role |
|-------|-------|------|
| `LoopSnapshot` | `Loop` publishes; `LoopPasses` stores roots | Target complete immutable content record and active snapshot identity |
| `Loop::passes` | `Loop` | Compatibility **recordPass**, **overdubPasses[]**, **editPasses[]** during migration |
| `Loop::capture` | `Loop` | Live record/overdub append buffer |
| Chunk payload | `LoopEventStore` pool | 256-event PSRAM chunks referenced by passes |
| `NoteEditSession.store` | `EditManager` | Live RAM overlay during NOTE_EDIT (not a pass row until commit) |

**Rule:** During migration, compatibility playback may use **merge** and
materialize. The target hot path selects the active `LoopSnapshot` and builds
only the consumer's bounded representation; it must not flatten the complete
loop on every stop. See
[LOOP_MIDI_STORAGE_AND_VALIDATION.md](../../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md).

---

## Storage revision

Snapshot publication or selection is the target source revision and invalidates
derived representations by snapshot identity. During migration, these
compatibility or live changes also advance the applicable revision:

- `sealCapture` / publish pass
- `saveNoteEditPass` / `closeNoteEditPass`
- NOTE_EDIT session store mutation
- snapshot publication, undo selection, or clear-slot restore
- `restoreFromSnapshot` — always deep clone while the compatibility DTO remains
- SD load into slot

Callers use the owning invalidation path and snapshot identity (with
NOTE_EDIT `sessionPreviewRevision_` where applicable), not ad-hoc consumer
rebuilds.

## Snapshot persistence boundary

`StorageManager` owns serialization, deferred save work, load, and compatibility
migration. A future storage version serializes immutable note and non-note
objects, stable logical references, snapshot roots, loop metadata, and the
serialized active branch tip. It does not serialize runtime addresses,
derived representations, playback cursor/order, active ledgers, or undo
history.

Loading rebuilds loop-local handles and selects the serialized complete
snapshot. The loaded snapshot is self-contained; a consumer does not traverse
older snapshots to obtain missing content. `Loop` remains the runtime
publication and selection owner.

## Large-loop save and load

A large snapshot may increase total serialization or migration work, but it
must not turn record/overdub stop, `handleMidiInput()`, clock dispatch, or a
track-tick path into a content-sized synchronous operation. Save and migration
use existing `StorageManager` deferred units. Any unit that executes between
`handleMidiInput()` entries must have a declared class, input/storage cap,
maximum indivisible step, progress state, and overrun/failure policy in
[ExecutionBounds.md](ExecutionBounds.md).

If a unit cannot fit, overruns, or fails allocation, the active snapshot and
previously valid persisted state remain intact. Pending work is retained or
explicitly rejected according to the phase policy; it is never silently lost.
Deferred persistence does not authorize a new persistence owner or a full-loop
catch-up at STOP.

---

## Non-goals

- Display-phase ticks, wrap, or viewport (→ [IntervalProjection.md](IntervalProjection.md))
- OLED layout (→ [Display.md](Display.md))
- Playback sort order (→ [Playback.md](Playback.md))
- Derived representation ownership, revision, invalidation, and build policy (→ [DerivedViews.md](DerivedViews.md))

**Normative storage guide:** [Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md](../../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md)
