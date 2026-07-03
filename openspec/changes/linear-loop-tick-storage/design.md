## Context

Today loop MIDI storage uses ticks in `[0, loopLength)` with wrap pairs (tail NoteOn + head NoteOff) and synthetic NoteOff at `loopLength - 1` for open tails ([`LoopStopFinalize`](../../../include/Utils/LoopStopFinalize.h), [`NoteUtils::reconstructNotes`](../../../src/Utils/NoteUtils.cpp)). Edit geometry ([`NoteMovementUtils::storageOffTickForSpanEnd`](../../../src/Utils/NoteMovementUtils.cpp)) applies `% loopLength` on mutation, breaking spans that cross the boundary. Display and playback already treat `noteOff >= loopLength` as wrap via projection; storage is the outlier.

**Design authority:** Cursor plan `linear_loop_tick_storage_4c7cb38f.plan.md` (2026-07-03).

**Constraints (unchanged):**

- No full-loop validate on record/overdub stop ([`LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../../../docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md))
- Hot paths: `normalizeWindow` scoped; no timer-based canonicalization
- SD tick cap: `maxPersistedEventTick` = `loopLength + BAR_TICKS` ([`StorageLoopIo.cpp`](../../../src/StorageLoopIo.cpp))
- Playback order index already sorts by `tick % loopLength` ([`Track.cpp`](../../../src/Track.cpp))

## Goals / Non-Goals

**Goals:**

- Single mutable representation: **canonical linear storage**
- Three narrow subsystems: **Normalize** (produce), **Validate** (assert), **Projection** (derive, no write-back)
- Normalize runs **only at transaction commit boundaries**; intermediate state within an active transaction MAY remain non-canonical
- Dev-era clean break: `!DEV_RESET_SD` + load reject (no migration)
- Fix 152335 move cutoff and loop-stretch synth-off inflation

**Non-Goals:**

- SD upgrade path for existing dev sets
- Renaming every projection call site in phase 1
- Changing jam / multiloop semantics

## Decisions

### D1 — Canonical storage invariants (1–7)

| # | Rule |
|---|------|
| 1 | One NoteOn per closed note span (identified by **NoteId** on the NoteOn event) |
| 2 | One NoteOff per closed note span (paired to that NoteId / NoteOn) |
| 3 | `0 <= NoteOn.tick < loopLength` |
| 4 | `NoteOff.tick >= NoteOn.tick` |
| 5 | Storage never wraps; linear span may exceed `loopLength` |
| 6 | No wrapped geometry in storage (no canonical head-off + tail-on pair) |
| 7 | `length = NoteOff.tick - NoteOn.tick` (derived only) |

**Rationale:** Invariant 7 eliminates ambiguous “display end” vs storage end and fixes stretch bugs tied to synth loop-end offs.

### D2 — Subsystem split

```
Geometry → [active transaction: non-canonical OK] → micro boundary (normalizeWindow on closure set)
         → macro boundary (normalizeAll) → persistent Canonical Storage
                                      → LoopEventValidation (check registry)
                                      → materializeWrapSegments() (project)
```

- **Normalize** — two scopes (see **D4**): window for live interaction; all for persistent canonical state
- **Validate** runs registered invariant checks; does not canonicalize
- **Projection** derives temporary objects; **never writes back**
- **NOTE_EDIT playback** — Tier 2 replaces playback source with `sessionMidiEvents()`; no canonical/transaction merge required (see **D9**)

**Alternative rejected:** Single `finalizeWrapWindow`-style module doing migration + pairing + display — caused ownership drift and write-back bugs.

### D3 — Dual normalization boundary rule

The system defines **two distinct normalization scopes**:

| Scope | API | Boundary | Purpose |
|-------|-----|----------|---------|
| **Micro** | `normalizeWindow` | `publishDependentFaderLatch` (after staged pipeline) | Local geometric consistency for live interaction, projection, and fader latch |
| **Macro** | `normalizeAll` | `commitAllPendingNoteEditActions` | Persistent canonical storage, undo snapshots, pass commit readers |

**Rules:**

- `normalizeWindow` at micro boundaries MUST NOT be treated as the sole authority for persistent canonical state; it prepares the **edit closure set** for interaction.
- `normalizeAll` at macro boundaries MUST NOT be skipped; it is the authoritative canonical commit for the session store.
- Neither normalize runs during mid-pipeline stages (D3a).

Other macro boundaries (unchanged): `setLoopLengthWithWrapping` → `normalizeAll`; capture stop may use `normalizeWindow` on capture closure before `commitCapturePass`, then macro canonicalization at pass visibility per timeline-passes.

**Within a transaction:** sub-steps use stable snapshot or staged pipeline. Storage writes MUST use linear `NoteOff.tick = NoteOn.tick + length` (no `% loopLength` on mutation). See **D15**.

**After macro commit:** undo snapshots, SD staging, materialized cache, and next-transaction readers see canonical storage only.

### D3a — NOTE_EDIT transaction consistency

Within a single NOTE_EDIT geometry transaction, all sub-steps (overlap detection, reconstruction, deletion, shortening, geometry mutation) SHALL operate on either:

1. A **stable snapshot** of transaction state captured at transaction start, or  
2. An **explicitly updated working copy** advanced by a **defined staged transformation pipeline**.

No sub-step MAY observe partially applied mutation results from another sub-step **except** through such a staged pipeline. External readers (undo, faders, display inventory for committed UI, playback canonical path, next transaction) MUST NOT read the store mid-pipeline.

**Canonical staged pipeline — `moveNote`:**

| Stage | Action | Working copy |
|-------|--------|----------------|
| 0 | Snapshot intent (currentStart, noteLen, target) | Read store |
| 2 | Restore overlap notes no longer covered | Mutate working copy |
| 3 | `reconstructNotes` → overlap detection inputs | **Projection read** of working copy |
| 4 | Apply shorten/delete impacts | Mutate working copy |
| 5 | Move mover on/off ticks | Mutate working copy |
| Commit | `normalizeWindow` on **edit closure set** | Micro — local consistency |

Stages 2→5 are **ordered and intentional**; stage 3 projection reads the post-stage-2 working copy, not arbitrary partial state from unrelated code paths.

**Forbidden:** calling undo snapshot, fader outbound (committed), or materialized playback merge between stages 2–5.

### D4 — `normalizeWindow` vs `normalizeAll`

| API | When | Scope | Guarantee |
|-----|------|-------|-----------|
| `normalizeWindow(store, loopLength, closureSet)` | Micro: `publishDependentFaderLatch` after pipeline; capture stop (capture closure) | **Edit closure set** only (D4a) | Events in closure satisfy linear geometry for interaction/projection |
| `normalizeAll(store, loopLength)` | Macro: `commitAllPendingNoteEditActions`; loop length change; full-pass touch | Entire store | Persistent canonical invariants 1–7 |

`normalizeWindow` MUST NOT use UI selection focus, arbitrary tick ranges, or display-window margins as scope input. Scope is the **edit closure set** (D4a).

### D4a — Edit closure set (normalizeWindow scope)

**Seed:** all `NoteId` values modified in the current geometry transaction.

**Expand deterministically:**

1. **Directly edited notes** — moved, resized, deleted movers
2. **Paired events** — each seed note's `NoteOn` and `NoteOff`
3. **Overlap participants** — notes affected by overlap resolution triggered by the edit
4. **Wrap boundary interactors** — notes whose temporal relationship changes due to loop-boundary interaction from the edit
5. **Adjacent dependency range** (optional) — structural adjacency only when required for stable reconstruction (not UI margin)

The closure set is computed from transaction inputs and overlap scratch; it MUST NOT depend on UI state, selection index, or piano-roll window position.

### D5 — Visibility rule

| Reader | When allowed |
|--------|----------------|
| Staged pipeline sub-steps | Mid-transaction working copy only |
| Projection (`reconstructNotes`, display, fader snapshot pre-latch) | Transaction-state store; may be non-canonical until micro normalize |
| Fader latch outbound | After micro `normalizeWindow` on closure set |
| Undo snapshot, pass commit, materialized cache, SD staging | After macro `normalizeAll` only |

**Implementation hook (edit):** staged pipeline completes → `normalizeWindow(closureSet)` → `publishDependentFaderLatch`; later `commitAllPendingNoteEditActions` → `normalizeAll` → undo / pass readers.

### D6 — Unified validation check registry

Replace separate `LoopEventValidate` + ad-hoc `validateAndCleanupMidiEvents` logic with one mechanism:

```cpp
enum class LoopEventCheck : uint32_t {
  NoteOnInLoopRange,      // invariant 3
  LinearNoteOff,          // invariants 4–5
  NoWrappedPairStorage,   // invariant 6
  DerivedLength,          // invariant 7
  NoteIdPairing,          // invariants 1–2
  OrphanNoteOff,
  OrphanNoteOn,
  // ...
};

struct LoopEventValidationResult {
  bool passed;
  LoopEventCheck firstFailure;
};

LoopEventValidationResult validateLoopEvents(store, loopLength, checkMask);
```

| Call site | checkMask | Repair |
|-----------|-----------|--------|
| SD load | canonical invariants (1–7) | **Reject load** — no normalize |
| After normalize (debug) | canonical invariants | Log only |
| Deferred idle (`validateAndCleanupMidiEvents`) | canonical + orphan checks | Orphan **removal** only — MUST NOT rewrite note geometry or reintroduce wrap pairs |

`validateAndCleanupMidiEvents` becomes a thin caller: run check registry, apply allowed repairs for orphan checks, log if canonical checks fail (dev).

### D7 — Dev persistence (no migration)

- `StorageManager::resetDevelopmentPersistence()` — superset of [`nukeHitlSetsCatalog`](../../../src/StorageManager.cpp)
- Wipe: `/MidiLooper/sets/`, `/MidiLooper/current/`, RAM loop pools, SlotSummary, undo stacks
- Serial: `!DEV_RESET_SD` → `#CAP,PERS,dev_reset_sd,…,ok|failed`
- Load: `validateLoopEvents` → reject non-canonical (`LOAD_REJECT non-canonical tick storage`)

**Alternative rejected:** One-time normalize-on-load migration — user confirmed dev-only wipe is sufficient.

### D8 — Supersede `note-edit-tick-coordinates-and-audition`

Remaining wrap/move HITL and Tier 2 audition overlay refinement move here. [`PARKED.md`](../note-edit-tick-coordinates-and-audition/PARKED.md) marks the old change id read-only.

### D9 — NOTE_EDIT playback (verification only)

Shipped Tier 2 (`sessionPreviewRevision_`, `ensurePlaybackWindowBuilt` → `sessionMidiEvents()`) **replaces** the playback source during NOTE_EDIT. There is no materialized underlay to merge or suppress.

Phase 2 §2.4 is **HITL verification** that session-store playback reflects transaction geometry during edit — not new overlay implementation.

### D10 — Loop shorten preserves storage

When loop length **shortens**, notes with `NoteOn.tick >= newLoopLength` are **omitted from projection** (playback/display) but **remain in canonical storage**. When loop length **lengthens** again, those notes **reappear** in projection without re-recording.

### D11 — Projection boundary (phase 1 alias)

Phase 1 documents `materializeWrapSegments` as the boundary; implementation aliases:

- `NoteUtils::reconstructNotes` → wrapped display segments
- `Track::rebuildPlaybackOrder` → playback ordering
- `DisplayWindowUtils::noteIntersectsWindow` → editor window probe

Audit and remove write-back paths: `% loopLength` on mutation, `isWrapHeadOffForTailOn` storage branches, display end → `MidiEvent.tick`.

### D12 — Canonical mutation vs projection separation

**Canonical write path (Phase 2 geometry):** all storage mutations (`NoteMovementUtils`, `EditApply` geometry commit, capture stop) operate in **linear tick-space** only. No modulo loop wrapping or display-end → `MidiEvent.tick` write-back.

**Projection path:** all `% loopLength`, wrapped display segments, piano-roll window clip, and fader **display anchors** live in projection / UI layers (`NoteUtils`, `DisplayWindowUtils`, `NoteEditManager` snapshot builders). Projection MUST NOT mutate canonical storage or influence `normalizeAll`.

### D13 — Set window drives fader range and display

The **set window** (bar-quantized edit viewport) is the primary driver for:

- F1 select and F2 coarse **tick range** (notes selectable within window)
- Filtered `NoteId` set for select navigation
- Display / piano-roll view extent

**Current behavior (Phase 2):** when set window span equals full loop length, fader extremes wrap note position within the loop (existing wrap semantics).

**Future (out of Phase 2 scope):** on long loops, when set window is a proper subset (e.g. 2 bars of a 64-bar loop), moving F2 past the window maximum shifts the window one bar right (and one bar left at minimum when space allows), refreshing underlying selectable data. Document in roadmap; do not block linear-tick geometry work.

## Impacted Functions and Files

| Subsystem | Primary files |
|-----------|---------------|
| Normalize | **new** `LoopTickNormalize.h/.cpp` |
| Validate | **new** `LoopEventValidation.h/.cpp` (check registry; replaces separate validate + ad-hoc cleanup checks) |
| Projection | `NoteUtils.cpp`, `Track.cpp`, `DisplayManager.cpp`, `DisplayWindowUtils.cpp` |
| Edit geometry | `NoteMovementUtils.cpp`, `NoteEditManager.cpp`, `EditApply.cpp`, `NoteEditFocus.cpp` |
| Capture / stop | `LoopStopFinalize.h`, `Track.cpp`, `Loop.cpp`, `EditManager.cpp` |
| Loop length | `Track.cpp`, `LoopEditManager.cpp` |
| SD / reset | `StorageLoopIo.cpp`, `StorageManager.cpp`, `RevisionLoad.cpp` |
| Tests | **new** `test_loop_tick_normalize`; extend reconstruct, stop_finalize, storage_loop_io |

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| `normalizeWindow` cost on every fader latch | Scope limited to **edit closure set** (D4a), not full loop |
| Overlap mid-transaction | Overlap uses **projection** on non-canonical intermediate state; normalize only at commit |
| Same-pitch LIFO with linear offs | Native collision tests; playback uses `% L` order index |
| Audition vs canonical at commit | Tier 2 session store — verification only (D9) |
| StorageManager overlap with revision track | Dev reset (Phase 1.5) before SD HITL; reuses HITL nuke guards |
| Missed write-back path | Grep audit in phase 1; regression matrix |

## Migration Plan

1. Flash firmware with this change
2. Run `!DEV_RESET_SD` once; confirm `dev_reset_sd ok`
3. Re-record test loops; new saves are canonical
4. Load rejects any stray pre-linear slot file

**Rollback:** Re-flash prior firmware + dev reset (no cross-version snapshot compatibility promised).

## Open Questions

- Optional metadata flag on new saves for fast load reject — implement if full validate scan is too slow on device
- Whether `LINEAR_TICK_DEV_RESET_ON_BOOT` compile flag is needed or manual serial is enough for team
- Set window slide on partial long-loop viewport (D13 future) — separate enhancement after window ⊂ loopLength UI ships
