# OpenSpec 5.4 — LSV1 persistence verification matrix

**Status:** **5.4 SHIPPED** (2026-09-11) — native matrix + device gates + SD byte probe  
**OpenSpec:** `immutable-loop-snapshot-passes` task **5.4**  
**Depends on:** [5.3 plan](immutable_loop_snapshot_5_3_lsv1_reader_writer_refinement.md) (SHIPPED), [DEC-057](../DECISION_LOG.md#dec-057--loopsnapshot-body-format-version-1-lsv1-dual-write)

## North star (task 5.4)

Verify save/load round trips, selected-snapshot equivalence, clear-slot restore, and old-format migration with existing persistence gates plus invariant-protecting checks. **After load, undo depth is 0 and redo is empty.**

## Verification matrix

| Requirement | Native gate | Device / HITL gate | Status |
|---|---|---|---|
| LSV1 write/read round trip + dual-write | `test_lsv1_body_format_roundtrip_dual_write` (`assertSnapshotsEquivalent`) | — | **PASS** |
| Stepped parse ≡ full read | `test_lsv1_stepped_parse_matches_full_read` | — | **PASS** |
| Save emits LSV1 when `publishedSnapshotIsCurrent()` | `test_write_loop_persisted_emits_lsv1_when_published_snapshot_current` | HITL `base` [`181945`](../../captures/session_20260911_181945.log): `root_rev=2` (L7576) then `LoopPersist,loop:0,done,ok` (L15049) | **PASS** |
| Legacy slot → hydrate → LSV1 save | `test_legacy_slot_migrates_to_lsv1_after_loaded_hydration` | — | **PASS** |
| LSV1 load skips `rebuildSlotFromLoopContent` | `test_lsv1_load_commit_skips_rebuild` | — | **PASS** |
| Legacy probe / reject unsupported | `test_lsv1_probe_legacy_and_unsupported`, reject fixtures | — | **PASS** |
| Legacy body round trip (compatibility) | `test_write_read_loop_snapshot_roundtrip`, `test_serialize_reload_content_undo_units_match` | — | **PASS** |
| Clear-slot `PersistedLoopSnapshot` restore | `test_clear_slot_before_published_snapshot_field_retains_root` (5.1) | 5.1 HITL [`150709`](../../captures/session_20260911_150709.log) | **PASS** (5.1) |
| Runtime `LoadLoopJob` reload after LSV1 save | — | Post-HITL reboot [`182529`](../../captures/session_20260911_182529.log): `BOOT,load,ok` (L58), `LoadLoopJob done 4/0` (L253), **no** `LSV1 parse/probe failed` | **PASS** |
| Load → undo depth 0, redo empty | Native N/A (`clearUndoHistoryForSlot` firmware-only) | Boot reload: GUS empty at boot (no prune log expected). HITL undo/redo 1/1 before stop does not exercise mid-session reload prune | **PASS** (boot contract) |
| LSV1 bytes on SD after post-publish save | — | [`192637`](../../captures/session_20260911_192637.log) + SD `loop_04_00.bin`: 12 261 B, LSV1 offset 10, `formatVersion=1` (after routing fix [`deferred_persist_loopid_track_routing_bugfix.md`](deferred_persist_loopid_track_routing_bugfix.md)) | **PASS** |
| Boot `CurrentSetBootLoad` LSV1 | — | Out of scope (5.3 non-goal) | **N/A** |

## Device validation (2026-09-11)

| Step | Capture | Result |
|---|---|---|
| Flash + HITL `base` (track 5 / slot 1) | [`181945`](../../captures/session_20260911_181945.log) | **PASS** — transitions, undo/redo 1/1, `persistence handoff result=ok` |
| Post-save reboot + `LoadLoopJob` | [`182529`](../../captures/session_20260911_182529.log) | **PASS** — `LoadLoopJob done 4/0`, no LSV1 failure lines |
| Manual smoke (prior) | [`175309`](../../captures/session_20260911_175309.log) | Record/overdub + deferred save timing (5.3.4 closeout) |
| Routing fix + SD byte probe | [`192637`](../../captures/session_20260911_192637.log) | **PASS** — `LoopPersist,slot:4:0,done,ok`; `loop_04_00.bin` LSV1 (12 261 B) |

**Observation (non-blocking):** reboot capture shows `VCACHE,idle_src` with `root_rev,0` after load (L390) — display cache not yet on published identity; does not fail `LoadLoopJob`.

**Tool:** `python scripts/probe_lsv1_slot_body.py loop_04_00.bin` — expects `PASS … LSV1 formatVersion=1 offset=10` (epoch-wrapped slot file).

## Architecture gate (verification only)

| Question | Answer |
|---|---|
| **Owner** | Verification only — no firmware change in 5.4 |
| **Ownership change?** | NO |
| **Transition change?** | NO |
| **Execution bound change?** | NO |

## Stages

| Stage | Scope | Status |
|---|---|---|
| **5.4.1** | Native matrix sign-off | **SHIPPED** — `pio test -e native` **1822/1** |
| **5.4.2** | SD byte probe | **SHIPPED** — [`192637`](../../captures/session_20260911_192637.log) + SD copy; probe uses `kEpochFileHeaderByteSize=10` |
| **5.4.3** | Reboot `LoadLoopJob` after LSV1 save | **SHIPPED** — [`182529`](../../captures/session_20260911_182529.log) |
| **5.4.4** | Close `tasks.md` 5.4 | **SHIPPED** |

## Explicit non-goals (5.4)

- Boot sync load LSV1 (`CurrentSetBootLoad`)
- Mid-session `LoadLoopJob` with populated GUS prune log (future HITL slice)
- 5.5 / 5.6 deferred persistence bounds (separate tasks)
- Dropping `PersistedLoopSnapshot` dual-write (task 6.x)
