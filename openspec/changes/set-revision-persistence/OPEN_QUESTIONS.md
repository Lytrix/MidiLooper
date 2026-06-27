# Open questions — resolution log

**Updated:** 2026-06-27 — **runtime architecture refinement** integrated.

## Refinement summary (2026-06-27)

Aligned with existing firmware architecture:

| Topic | Decision |
|-------|----------|
| Runtime priority | MIDI > playback > clock > display > persistence |
| Current | **Epoch** persistence (not file-atomic sync save) |
| Dirty | Derived: `lastCommittedEpoch != currentEpoch` |
| Commit | Reuse **deferred save FSM** stages; no queue workers |
| Save snapshot | Last **completed** epoch only |
| Revision blob | **Pass/chunk** canonical — no materialize on save |
| Boot | Current epoch first; + recovery checkpoints step 4 |
| Revision ids | Allocate on COMPLETE; failed commit reuses id |
| Budget | `maxPersistenceMicros` per slice |
| Overlay | Input-modal only; playback + persistence continue |
| Schema | `schemaVersion` only (no formatVersion/footerVersion) |

**Do not refactor:** StorageManager, StorageLoopIo, LoopPasses, deferred FSM, chunk pool.

Prior gap items A1–E2 remain closed; see spec files listed below.

## Spec index

- `set-revision-catalog`, `current-workspace`, `revision-packed-blob`
- `revision-commit`, `revision-load`, `recovery-boot`
- `set-browser-overlay`, `slot-loop-import`, `loop-slot-buttons`

## Parked tasks (2026-06-27)

| Task | Topic | Rationale |
|------|-------|-----------|
| **3.9** | Eight-hour failsafe → silent **revision commit** when `currentEpoch != lastCommittedEpoch` | Spec'd in `recovery-boot/spec.md`; implementation **parked** until field testing validates trigger conditions and whether to retire legacy `processSavedSetFailsafe` (`saveNewSet` anchor). |

**Next apply:** section **4** overlay or parked **3.9**.

## Deferred v2

Load all slots from track, MIDI encoder, revision compaction, catalog search, partial revision restore.
