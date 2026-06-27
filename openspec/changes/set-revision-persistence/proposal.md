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
- **Revision blob:** `REVPK02` chunk stream — canonical pass/chunk storage via `StorageLoopIo`; no materialize on save path.

### Format choice: REVPK02 (LMDB-inspired chunk stream)

**Mental model** — not an LMDB port (no mmap, B+tree, MVCC):

- `current/` + catalog (`workspace.bin`, `index.bin`) = environment root (mutable meta)
- `v####.bin` = immutable snapshot (append-only typed chunk stream)
- Single deferred writer FSM

**Pros**

- Aligns revision file shape with slice-by-slice SD writes during play
- Natural home for canonical pass/chunk bodies via `StorageLoopIo` (`recordPass`, `overdubPasses[]`, `editPasses[]` inside `LoopSlot` chunks)
- Load path scans typed chunks; `SlotIndex` directory written last with final byte offsets
- Same immutability contract: validated `.tmp` → rename; Current unchanged after Save

**Cons / trade-offs**

- More on-disk structure than REVPK01 (8 B chunk header per section, `SlotIndex` chunk)
- Footer CRC re-reads full payload before seal — must stay chunk-bounded within slice budget as revisions grow
- Commit WRITE still copies opaque `current/` epoch bodies until task **3.6** (true `StorageLoopIo` streaming)

**Save-while-playing (MIDI timing)** — format choice does **not** provide the guarantee. That comes from:

- `maxPersistenceMicrosActive` (300µs) during `PLAYING` / `RECORDING` / `OVERDUBBING`
- Deferred FSM with chunk-bounded I/O (512 B copies)
- No `LoopPasses::materialize` on save path
- Revision commit blocked while deferred `current/` save is active

Continuous epoch save to `current/` is the primary save-during-play path; revision commit is heavier (assemble + validate + catalog) but uses the same slice budget.

**Verdict:** REVPK02 is better for architecture and future streaming; neutral for MIDI timing today vs REVPK01, provided all commit steps (CRC, validate) respect the active slice budget.

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
