## 1. Metadata and catalog foundation

- [x] 1.1 Structs per `current-workspace` (epochs) + `set-revision-catalog`; `schemaVersion` only.
- [x] 1.2 `SetRevisionCatalog` — `index.bin`, `set.bin`; revision id on COMPLETE only.
- [x] 1.3 Set id monotonic from `index.bin`; failed revision reuses id.
- [x] 1.4 `REVPK02` layout — typed chunk stream, SlotIndex directory, `sourceEpoch`, 12 B footer.
- [x] 1.5 Native: catalog + packed blob parser (no heap, no materialize on save path).

## 2. Current workspace

- [x] 2.1 Epoch headers on `MidiLooper/current/` files; `workspace.bin` epoch fields.
- [x] 2.2 Retarget to `MidiLooper/current/` + `MidiLooper/sets/` (separate roots); extend CurrentSet deferred FSM.
- [x] 2.3 `SlotSummary[8]` for overlay preview.
- [x] 2.4 Native: epoch valid/invalid; dirty = `currentEpoch != lastCommittedEpoch`.

## 3. Revision commit and load

- [x] 3.1 Commit FSM stages on deferred save infrastructure; `maxPersistenceMicros` slices.
- [x] 3.2 Deferred load → new Current epoch; provenance after 100%.
  - [x] 3.2a Load **VALIDATE** streams footer CRC + chunk walk from SD (no full-file RAM cap).
  - [x] 3.2b Commit **WRITE** emits Transport chunk header before runtime bundle body.
  - [x] 3.2c Commit includes **occupied** LoopSlots only (published passes/events); SlotIndex carries `loopLengthTicks`, `noteCount`, `bars`.
  - [x] 3.2d Load tolerates missing/empty Transport chunk — default transport from SlotIndex; LoopSlots still restore.
  - [x] 3.2e Display refresh pending consumed after load (`consumeRevisionLoadDisplayRefreshPending`).
  - [x] 3.2f HITL: `revision_load`, `revision_load_record` (base + post-record commit/load), `!REV_LOAD` / `!REV_CLEANUP`.
- [x] 3.3 SNAPSHOT freezes completed epoch; post-snapshot capture → next epoch.
- [x] 3.4 `lastCommittedEpoch` sync at COMPLETE only.
- [x] 3.5 Native: commit during PLAYING uses budget; no materialize on WRITE path.
- [x] 3.6 Remove SavedSet shims; stream via `StorageLoopIo` / pass shapes.
- [x] 3.7 DIRTY_PROMPT Yes/No/Cancel; minimal until pipeline done.
- [ ] 3.8 Boot: Current epoch → derived rev → latest → recovery checkpoints → empty.
- [ ] 3.9 8h failsafe when epochs diverge > 8h.

## 4. Set browser overlay

- [ ] 4.1 Modes: ROOT, DIRTY_PROMPT, REVISION_HISTORY, LOOP_PICK, MINIMAL_LOADING.
- [ ] 4.2 Save → commit REQUEST + exit; playback/persistence continue.
- [ ] 4.3–4.10 (unchanged UX tasks — overlay input-modal, encoder GPIO, loop picker).

## 5–7. Loop copy, recovery docs, verification

(See prior tasks 5.x–7.x; HITL 4.6 retained.)

**Do not refactor:** `StorageManager`, `StorageLoopIo`, `LoopPasses`, deferred FSM core, chunk pool.

**Apply order:** 1.x → 2.x → 3.x → 4.x → 5.x → 6.x.
