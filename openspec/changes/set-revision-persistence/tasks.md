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
  **I4 — revision-only admission repair, locally verified 2026-10-12:** production main admits commit/load pending and in-progress through existing Persistence opportunity; no workspace-query/gate/budget change. Save→real commit→exact created revision load passes with independent streamed-footer CRC and preserved Current payloads. Header patch non-truncating; destructive reopen regression repaired.134focused/native3070pass7skip/buildpadding904>=840/reviewAPPROVE. Candidate8ed5a9dc NOT uploaded; full device/manual gates remainopen. [I4 — repair packet](../runtime-working-set-ownership/evidence/2026-10-12-i4-ui-storage/ADMISSION-FIX.md).
- [x] 3.1a **I4 — bounded save serialization optimization (local 2026-10-12):** ≤16membershiplookups/≤1populatedpage per existinggrain, forward≤512bytes using existing scalar cursors, actual revision lengths/privatebackpatch instead offullmeasure. Sparse24bar3102→199turns/exactbytes; densepaged/mixed/retry/zero-sizing/SlotIndex/CRC/commit-load tests.148focusedpass/fullnative3074pass7skip0fail; defaultcaptureSUCCESS/RAM1padding1160>=840, one staticreviewno findings; DEC-118 — bounded forward save serialization. CandidateHEXcdfe4b51 NOT uploaded, no device/Git action. [I4 — execution evidence](../runtime-working-set-ownership/evidence/2026-10-12-i4-ui-storage/SERIALIZER-OPTIMIZATION.md).
- [x] 3.1b **I4 — optimized checkpoint hardware duration/receipt/cohort qualification (2026-10-12):** approveddefaultcdfe4b51upload/hashunchanged/padding1160>=840; canonicalregistered03 createsS0001_v0002/live+drainedoraclePASS/two-drainscomplete/no latch. All41orderedtail tuples match02/8ed5a9dc (152bars/256129payloadbytes/notes/events); Save44.656534→14.862218s (3.0047×/66.7188%less), tailcalls20566→4329. Owner: “Completed normally; responsive; wait is acceptable.” Qualified only this stoppedworkload/observable shape; not fullhardwareSD/MIDIbyteparity, generaldeadline, recordPLAYINGspinner or wholeI4PASS. No record/clear/slotchange/load/cleanup/Git action. [I4 — optimized hardware evidence](../runtime-working-set-ownership/evidence/2026-10-12-i4-ui-storage/OPTIMIZED-HARDWARE.md).
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
- [x] 3.8 Boot: Current epoch → derived rev → latest → recovery checkpoints → empty.
- [ ] 3.9 **Parked** — 8h failsafe when epochs diverge > 8h (spec in `recovery-boot`; needs field testing before implementation — legacy `processSavedSetFailsafe` unchanged).

## 4. Set browser overlay

- [x] 4.1 Modes: ROOT, DIRTY_PROMPT, REVISION_HISTORY, LOOP_PICK, MINIMAL_LOADING.
- [x] 4.2 Save → commit REQUEST + exit; playback/persistence continue.
- [x] 4.3 Overlay input-modal: suspend record/overdub arm, loop slot gestures, note-edit; keep transport + deferred FSM.
- [x] 4.4 GPIO encoder rotation → overlay list focus (ROOT + DIRTY_PROMPT via `adjustLoadSaveListSelection`).
- [x] 4.5 GPIO encoder button → overlay row actions (short confirm/load, double favorite, long history/exit).
- [x] 4.6 Workspace Set browser lists revision catalog (`sets/S####/`), sorted by `updatedUnix`.
- [x] 4.7 Revision history UI — list, scroll, load, back navigation, detail preview.
- [ ] 4.8–4.10 Loop picker UI + HITL `set_revision_overlay`.

## 5–7. Loop copy, recovery docs, verification

(See prior tasks 5.x–7.x; HITL 4.6 retained.)

**Do not refactor:** `StorageManager`, `StorageLoopIo`, `LoopPasses`, deferred FSM core, chunk pool.

**Apply order:** 1.x → 2.x → 3.x → 4.x → 5.x → 6.x.
