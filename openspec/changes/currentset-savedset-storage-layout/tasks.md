## 1. CurrentSet incremental policy hardening

- [ ] 1.1 Remove default transport-stop full-slot dirty marking; keep full dirty only for explicit migration/repair paths.
- [ ] 1.2 Keep deferred save slot-skip behavior and confirm only dirty `loop_TT_SS.bin` files are rewritten in normal runtime flows.
- [ ] 1.3 Add/extend native tests that assert stop-path save does not force full 64-slot payload rewrite.

## 2. Slot metadata index

- [ ] 2.1 Define fixed-width slot-summary schema in CurrentSet `meta.bin` and wire read/write helpers.
- [ ] 2.2 Update slot-summary rows from all material mutation paths (record, overdub, edit commit, clear, import).
- [ ] 2.3 Add native tests for slot-summary correctness and deterministic parser behavior without dynamic allocation.

## 3. SavedSet packed storage

- [ ] 3.1 Implement SavedSet packed writer (`meta.bin` + `loops.bin`) with blob index table (`track`, `slot`, `offset`, `length`, `crc`).
- [ ] 3.2 Implement packed SavedSet loader that rebuilds CurrentSet from index+blob data.
- [ ] 3.3 Add atomic temp→verify→rename commit flow for both packed files and rollback-safe failure handling.

## 4. Compatibility and migration

- [ ] 4.1 Add format discriminator/version fields for SavedSet packed layout and loader dispatch.
- [ ] 4.2 Implement read compatibility strategy (read-both if legacy SavedSet layout exists; write packed for new snapshots).
- [ ] 4.3 Add migration and compatibility tests for existing CurrentSet v6 trees and mixed SavedSet formats.

## 5. Verification and performance

- [ ] 5.1 Run `pio test -e native` with new storage/index test suites.
- [ ] 5.2 Run HITL baseline to confirm no record/overdub stop timing regression from storage policy changes.
- [ ] 5.3 Capture SD write metrics (`PERS,result_stats`) comparing stop-path write counts and save duration before/after policy hardening.
