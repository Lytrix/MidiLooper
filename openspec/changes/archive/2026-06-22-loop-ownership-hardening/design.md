## Context

Post-M8 firmware stores canonical MIDI in **`passes[]`** per slot (**recordPass**, **overdubPasses**,
**editPasses**). Live capture uses **`Capture`**. Track FSM transitions are validated in
`TrackStateMachine`; SD v4 persists pass snapshots via `StorageLoopIo`.

The 2026-06-21 architecture review mapped seven findings to concrete code paths. This change
addresses all seven without altering the **passes** data model or **NoteEditSession** design.

Brownfield constraints ([`LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../../../docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md)):

- Hot stop path stays **wrap-window only** (`finalizeLoopAtStop`).
- Full validate remains **deferred** — but must complete under a bounded policy.
- Undo restore always **`cloneShared()`** — unchanged.

## Goals / Non-Goals

**Goals:**

1. No data loss on redundant overdub-start commands.
2. Slot `loopId` always resolves to the intended pool entry or a defined repair path.
3. SD save/load preserves loop phase origin (`startLoopTick`) for playback alignment.
4. **editPasses** unbounded growth mitigated by **`pool-budget`** (not in this change).
5. No heap allocation on first playback tick after transport start.
6. Deferred full validate eventually runs; document when.
7. Corrupt/truncated SD edit tails fail load — no silent empty **editPasses**.

**Non-Goals:**

- Changing **passes** schema version (v4 wire format unchanged except documented field restore).
- Pool chunk count / `LoopEventStore` global cap.
- JamRecorder, scenes, LFO.
- Rewriting `Looper` / `LooperStateManager`.

## Decisions

### D1 — Overdub start idempotency (guard, not FSM surgery)

**Choice:** `Track::startOverdubbing` returns immediately when `trackState == TRACK_OVERDUBBING`
and capture is already `CapturePhase::Overdub` — do **not** call `beginCapture` again.

**Alternatives:**

| Option | Rejected because |
|--------|------------------|
| Remove `OVERDUBBING → OVERDUBBING` from FSM | UI may rely on self-transition for LED refresh; guard is smaller blast radius |
| Queue second overdub as pending pass | Product undefined; risks duplicate undo entries |

**Call sites:** Also guard `TrackManager::startOverdubbingTrack` and `MidiButtonActions::OVERDUB_FOR_SLOT`.

### D2 — Slot resolution: fail explicit, repair on load

**Choice:**

- `LoopPool::findById` returns `nullptr` or `optional` — callers use `loopForSlot(poolIndex)` when
  `slots_[i].loopId` is `kInvalidLoopId` or unknown.
- **Remove** silent `at(0)` fallback.
- On SD load: if `slotLoopId` is not in `0 .. MAX_LOOPS_PER_TRACK-1`, rewrite to pool index `p`
  (1:1 stable ids) and log warning.

**Rationale:** 1:1 `LoopPool` mapping (`assignStableIds1To1`) means valid ids equal pool indices;
invalid persisted ids are migration corruption, not runtime aliases.

### D3 — Restore `startLoopTick` on load

**Choice:** `applySnapshotToLoop` sets `loop.startLoopTick = snapshot.startLoopTick` (was forced to 0).

**Note:** Playback phase still uses `tickPhaseInLoop(currentTick, startLoopTick, loopLengthTicks)`.
Zero remains valid for “origin at transport zero” loops; persisted non-zero values must round-trip.

### D4 — editPasses memory (deferred to `pool-budget`)

**Choice:** Do **not** add a fixed **editPass** row cap in this change. Unbounded **editPasses**
growth and fixed undo/capture caps are addressed in **`openspec/changes/pool-budget/`** via heap
admission, chunk pool admission, **`reclaimUnreferencedDisabledPasses`**, and memory-pressure undo
trim (**`PREFERRED_UNDO_DEPTH = 99`** when affordable).

**This change:** no task group 4; no `timeline-passes` delta for cardinality cap.

### D5 — Deferred validate completion

**Choice:** Two-tier idle maintenance in `main.cpp`:

1. **Existing:** skip all deferred work while any track is playing/recording/overdubbing.
2. **New:** per-track `deferredFullMidiValidate` sets a `deferredValidateQueuedAtMs` timestamp;
   when transport is active but track is **only PLAYING** (not capture) and flag age exceeds
   `Config::deferredValidateMaxDelayMs` (default 60s), run validate for that track once.

**Alternative rejected:** run full validate on every overdub stop — violates hot-path guide.

**TBD (product):** whether 60s is acceptable; spec uses configurable constant.

### D6 — Playback runtime prewarm

**Choice:** After `trackManager.allocateLoopsEarly()` in `setup()`, call
`TrackManager::prewarmPlaybackRuntime()`:

- For each track, for each slot with `hasPublishedEvents()` or slot enabled: touch
  `playbackRuntime.slot(i)` and `loop.getPlaybackOrder()` (forces unique_ptr allocation).
- Run once at boot and after `StorageManager::loadState` success.

**Hot path after prewarm:** `playMidiEvents` must not allocate; only rebuild vectors when
`playbackRevision` changes (existing).

### D7 — Storage tail fail-hard

**Choice:** `readPersistedEditsTail` returns `false` on any `ioRead` failure; remove branches that
return `true` with cleared **editPasses**.

Load failure surfaces as `readLoopPersisted` / `loadState` failure (existing error path).

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| Unbounded editPasses until pool-budget ships | Apply **pool-budget** before or soon after; do not ship fixed-25 cap here |
| Deferred validate during PLAYING causes hitch | Run only after delay; single track per idle slice; telemetry |
| Prewarm increases boot RAM | Only enabled slots + slots with data; native tests measure |
| Invalid slotLoopId repair masks SD corruption | Log at WARNING; HITL save/load round-trip test |
| Restoring `startLoopTick` changes loaded loop feel | Native round-trip test; matches save intent |

## Migration Plan

1. Ship firmware with guards + load repair (backward compatible with existing v4 files).
2. No SD format version bump.
3. Rollback: revert firmware; saved state remains v4.

## Open Questions

1. **deferredValidateMaxDelayMs:** 60s default — confirm with user after first HITL long-play session.
2. **Remove OVERDUBBING self-transition** from FSM once guards proven? (Parked; not in initial tasks.)
3. **editPass admission UX** — see **`pool-budget`** open questions.
