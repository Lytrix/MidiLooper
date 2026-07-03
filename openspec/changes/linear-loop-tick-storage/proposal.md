## Why

Loop MIDI events are stored with modulo ticks and synthetic note-offs at `loopLength - 1`, while display and playback already project wrap via `% loopLength`. That mismatch causes note-edit move across the loop boundary to **cut off** notes (`1536 % 1536 → 0`, session_20260703_152335) and causes loop **extension** to **inflate** note length for synth-off tails. Canonical storage must be **linear** (`NoteOff.tick >= NoteOn.tick`, may exceed `loopLength`); wrapping belongs only in a **projection layer** that never writes back.

This is a **design-session change** (representation ownership + transaction boundaries). It supersedes the partial side fix `note-edit-tick-coordinates-and-audition` (Tier 1–2 shipped; HITL pending). Brownfield: [`docs/Guides/NOTE_WRAPPING_LOGIC.md`](../../docs/Guides/NOTE_WRAPPING_LOGIC.md), [`docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../../docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md), [`docs/DELIVERABLE_TRACKING.md`](../../docs/DELIVERABLE_TRACKING.md).

## What Changes

- **Canonical storage invariants (1–7)** — linear note spans; length always derived as `NoteOff.tick - NoteOn.tick`
- **Dual normalization boundaries** — `normalizeWindow` at micro-transaction (`publishDependentFaderLatch`) for local geometric consistency; `normalizeAll` at macro-transaction (`commitAllPendingNoteEditActions`) for persistent canonical state and undo
- **`LoopTickNormalize`** — `normalizeWindow` (edit **closure set**) / `normalizeAll` (full store)
- **`LoopEventValidation`** — unified check registry (enum/struct per invariant); **asserts** on load; idle cleanup uses same checks with optional repair for non-geometry corruption
- **Projection layer** — `materializeWrapSegments` (alias `reconstructNotes`, playback order, window clip); temporary derived objects only; **no write-back**
- **NOTE_EDIT transaction consistency** — staged pipeline or start snapshot; no ad-hoc partial reads across sub-steps
- **Edit audition (verification)** — shipped Tier 2 (`sessionMidiEvents()` in playback) already satisfies audition; §2.4 is HITL verification only, not new merge logic
- **Set window drives fader range** — F1/F2 coarse range and display view derive from bar-quantized **set window**; when window equals loop length, notes wrap at fader extremes (partial-window slide deferred)
- **Dev SD reset** — `!DEV_RESET_SD` wipes `current/` + `sets/` + RAM slot/loop index; **no legacy migration**
- **Load policy** — `validateLoopEvents` on SD load; fail with clear log if non-canonical
- **Park / supersede** — `note-edit-tick-coordinates-and-audition` (remaining HITL absorbed here)

## Capabilities

### New Capabilities

- `linear-loop-tick-storage`: Canonical invariants, normalize contract, validate assert-only contract, visibility rule, dev reset + load reject
- `loop-wrap-projection`: Projection responsibilities (wrap reconstruct, playback order, piano-roll clip, editor window); no write-back guarantee

### Modified Capabilities

- `storage-loop-io`: Persist/load linear tick invariants; reject non-canonical slot files on load
- `timeline-passes`: Capture stop and pass commit call normalize before readers; open notes during capture; linear offs after stop
- `note-edit-modification-session`: Dual normalize boundaries; edit closure set; linear off writes; staged pipeline
- `loop-wrap-projection`: Loop shorten/lengthen projection; NOTE_EDIT playback uses session store (verification)
- `loop-temporal-persistence`: Unified validation checks; dev `resetDevelopmentPersistence`; refactor `validateAndCleanupMidiEvents` onto check registry
- `change-length-commit-rematerialize`: ChangeLength commits linear `NoteOff.tick`; normalize at pass boundary
- `note-edit-fader-feedback`: Set-window-driven F1/F2 range; micro vs macro normalize ordering
- `note-edit-session-undo`: Undo snapshots after `normalizeAll` at macro boundary; restored spans stay canonical

## Impact

- **New modules:** `include/Utils/LoopTickNormalize.h`, `include/Utils/LoopEventValidation.h` (unified check registry)
- **Edit / move:** `NoteMovementUtils.cpp`, `NoteEditManager.cpp`, `EditApply.cpp`
- **Capture / stop:** `LoopStopFinalize.h`, `Track.cpp`, `Loop.cpp`
- **Projection:** `NoteUtils.cpp`, `Track.cpp` (playback order), `DisplayManager.cpp`, `DisplayWindowUtils.cpp`
- **Loop length:** `Track.cpp` (`setLoopLengthWithWrapping`), `LoopEditManager.cpp`
- **SD / dev reset:** `StorageLoopIo.cpp`, `StorageManager.cpp`, `RevisionLoad.cpp`
- **Tests:** new `test_loop_tick_normalize`; extend `test_noteutils_reconstruct`, `test_loop_stop_finalize`, `test_storage_loop_io`
- **Storage:** Dev wipe required — **no SD migration**; pre-linear sets fail load or are removed by `!DEV_RESET_SD`
- **Docs:** [`docs/plans/linear_loop_tick_storage_enhancement.md`](../../docs/plans/linear_loop_tick_storage_enhancement.md); update NOTE_WRAPPING_LOGIC + LOOP_MIDI guides on archive

## Non-Goals

- Production user migration from pre-linear SD sets
- Jam tick semantics / multiloop slot model changes
- NOTE_EDIT long-loop **partial window slide** (D13) — future; Phase 2 ships full-loop wrap only
- Full rename of `reconstructNotes` in first PR (boundary alias acceptable)
- JamRecorder, M10 Scenes, D13 capture

## Relationship to Active Work

Orthogonal to `set-revision-persistence` overlay track ([`CURRENT_WORK.md`](../../docs/runtime/CURRENT_WORK.md)). Register in PROJECT_STATE when implementation starts; does not block revision overlay merge but shares `StorageManager` / `StorageLoopIo` touch points — coordinate dev reset with revision HITL cleanup patterns (`rev_nuke_sets`).

## Delivery Sequence

| Phase | Work |
|-------|------|
| **1** | Normalize + unified validation checks + projection boundary + native tests |
| **1.5** | Dev SD reset (`!DEV_RESET_SD`) — before any SD save/load HITL |
| **2** | Note edit geometry (152335) + dual normalize boundaries + edit closure set + set-window fader range |
| **3** | Capture / stop / overdub linear offs |
| **4** | Loop length change + stretch/shorten projection preservation |
| **5** | Load reject + passes materialize + idle cleanup on check registry |
| **6** | Regression matrix HITL + archive |
