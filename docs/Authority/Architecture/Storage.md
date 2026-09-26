# Runtime architecture — capture storage

**Parent:** [RuntimeArchitecture.md](RuntimeArchitecture.md)

**Naming:** [NAMING.md](../NAMING.md) — Capture, passes, merge, materialize, Deferred vs Pending.

---

## Question

> What is the authoritative timeline?

Before snapshot migration, `Loop::passes` and live `Capture` provide the
runtime content boundary. The target authoritative content is the selected
complete immutable `LoopSnapshot`, not a `LoopPass` compatibility record.
Everything downstream remains derived;
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
migration. **[DEC-057](../../DECISION_LOG.md#dec-057--loopsnapshot-body-format-version-1-lsv1-dual-write)**
approves the physical loop-slot body as **LoopSnapshot body format version 1**:

- Wire discriminator: 4-byte ASCII **`LSV1`**, encoded using the existing
  fixed-token wire convention (`EPT3` / `GEO1` / `OSI1`).
- Immediately after: `uint16_t formatVersion` (supported value **1** only) and
  `uint16_t flags`.
- Length-framed sections: `LoopMetadata`, `PersistedLoopSnapshot` (migration
  dual-write), `LoopSnapshot` (sole persisted complete/root snapshot).
- Absence of `LSV1` ⇒ **legacy** bare `PersistedLoopSnapshot` body (deferred
  migration in OpenSpec task 5.3).
- `LSV1` present with `formatVersion != 1` ⇒ **unsupported / reject** (not
  legacy).
- A valid `LSV1` + `formatVersion == 1` body with missing/unparseable required
  `LoopSnapshot` section must **not** fall back to `PersistedLoopSnapshot` as
  authority.

The format serializes immutable note and non-note objects, stable logical
`NoteRef` / `EventRef` membership (pitch → bar for complete notes; bar →
`EventRef` lists as an indexed view of existing event-stream order), loop
metadata, and the latest saved complete snapshot. It does not serialize runtime
addresses, derived representations, playback cursor/order, active ledgers, undo
history, or abandoned-branch lineage sufficient to restore post-reboot undo.
Outer CurrentSet / epoch / `SAVE` / `REVPK02` / `CONTAINER_VERSION` are unchanged
by DEC-057. Firmware reader/writer, save hook, and runtime `LoadLoopJob` load
authority shipped in OpenSpec task **5.3** (5.3.1–5.3.4). Boot sync load
(`CurrentSetBootLoad`) remains legacy-only.

The logical membership is the approved Phase 0 boundary; the physical
pitch/bar directory that provides structural sharing remains a Phase 1
measurement decision.

Loading rebuilds loop-local handles and selects the latest saved complete
snapshot. Under DEC-088 — load-time undo reconstruction, retained active pass
records and their grouping derive runtime undo units; redo starts empty. Undo
creates runtime Redo, and another slot loading preserves it. This changes neither
the persisted snapshot authority nor the content-only file shape. The loaded snapshot
is self-contained; a consumer does not traverse older snapshots to obtain
missing content. `Loop` remains the runtime publication and selection owner.

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

## Automatic runtime metadata repair at boot

DEC-089 — boot metadata repair runs in `StorageManager::loadState` before temp
cleanup. A valid runtime bundle takes the normal load path. Missing/corrupt metadata
is reconstructed only when `workspace.bin` and all 64 epoch-qualified slot files
validate through their production parsers. Missing/invalid slots, unsupported bodies,
wrong slot identities and epochs newer than the workspace cause refusal before any
write. This deliberately conservative recovery does not salvage partial libraries or
legacy slot bodies without the current epoch header.

Repair writes a separately named candidate in `current/temp`, verifies it, preserves
the corrupt original under a unique backup name, and promotes the checked candidate.
It leaves slot files, workspace metadata, unrelated folders and existing temp evidence
unchanged. Existing repair/backup names are never overwritten; exhaustion of the 64
names in either namespace blocks recovery. Normal current-workspace boot also skips
old temp cleanup so a later boot cannot silently discard repair evidence.

Replacement settings are stopped transport, 120 BPM, unmuted tracks, track 1 and
the first nonempty slot per track selected/enabled (slot 1 for an empty track).
Original performance settings are not recovered. Undo derives from saved passes
under DEC-088 — load-time undo reconstruction; redo starts empty. Repaired metadata
then enters the existing deferred slot loader.

Failed recovery latches `BootRecoveryJob::writesBlocked` for the boot. Main setup/loop,
save admission, deferred saves and synchronous save drain refuse normal operation;
an empty fallback is not saved over this workspace. A new boot re-evaluates sources.
The existing no-workspace legacy/fresh path remains available. Repair parses one slot
at a time during cold setup before normal MIDI/clock service, using existing parser
allocations; no realtime or arbitrary-size allocation guarantee is claimed.

Host SD fault injection verifies interrupted firmware operations, not the SD card's
physical power-loss durability or FAT atomicity. Device acceptance remains open;
the incident hold and standalone folder reader remain in place.

## Non-goals

- Display-phase ticks, wrap, or viewport (→ [IntervalProjection.md](IntervalProjection.md))
- OLED layout (→ [Display.md](Display.md))
- Playback sort order (→ [Playback.md](Playback.md))
- Derived representation ownership, revision, invalidation, and build policy (→ [DerivedViews.md](DerivedViews.md))

**Normative storage guide:** [Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md](../../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md)
