## Why

The shipped M1/M2 stack (`Sets/_current/` + flat SavedSet folder copies) prevents data loss but does
not match the product model users need: **immutable history**, **continue editing after load**, and a
**single static save/load screen** without nested flows.

A **Current → Set → Revision** model preserves uninterrupted jamming while explicit Save creates
append-only history. Implementation **extends** existing deferred save FSM, CurrentSet, and pass/chunk
storage — it does **not** rewrite them.

## What Changes

- **Layout:** `MidiLooper/current/` (mutable) and `MidiLooper/sets/` (immutable) are **separate** —
  Current does not live inside Sets.
- **Current:** Epoch-based `MidiLooper/current/` (highest valid epoch at boot); dirty =
  `currentEpoch != lastCommittedEpoch`.
- **Save:** Revision commit via deferred FSM; Current unchanged after Save; `v####.bin` snapshots only.
- **Revision blob:** `REVPK01` streams canonical pass/chunk storage — no materialize on save path.
- **Recovery:** `MidiLooper/recovery/checkpoints/`; boot prefers Current epoch.
- **Runtime priority:** MIDI capture > playback > clock > display > persistence.

**Non-goals (v1):** compaction, deduplication, catalog search, partial restore, StorageManager/FSM
refactor.

**Do not refactor:** `StorageManager` core, `StorageLoopIo`, `LoopPasses`, deferred FSM core, chunk pool.

## Capabilities

- `current-workspace`, `set-revision-catalog`, `revision-packed-blob`, `revision-commit`,
  `revision-load`, `set-browser-overlay`, `slot-loop-import`, `loop-slot-buttons`, `recovery-boot`

## Impact

Extend `StorageManager` / `DisplayManager` / deferred FSM hooks — constrain existing architecture,
do not replace it.

**Brownfield:** `docs/Guides/DEFERRED_RUNTIME_PERSISTENCE.md`, CurrentSet §1 shipped.
