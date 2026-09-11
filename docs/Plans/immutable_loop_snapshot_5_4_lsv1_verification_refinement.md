# OpenSpec 5.4 — LSV1 persistence verification matrix

**Status:** IN PROGRESS  
**OpenSpec:** `immutable-loop-snapshot-passes` task **5.4**  
**Depends on:** [5.3 plan](immutable_loop_snapshot_5_3_lsv1_reader_writer_refinement.md) (SHIPPED), [DEC-057](../DECISION_LOG.md#dec-057--loopsnapshot-body-format-version-1-lsv1-dual-write)

## North star (task 5.4)

Verify save/load round trips, selected-snapshot equivalence, clear-slot restore, and old-format migration with existing persistence gates plus invariant-protecting checks. **After load, undo depth is 0 and redo is empty.**

## Verification matrix

| Requirement | Native gate | Device / HITL gate | Status |
|---|---|---|---|
| LSV1 write/read round trip + dual-write | `test_lsv1_body_format_roundtrip_dual_write` (`assertSnapshotsEquivalent`) | — | **PASS** |
| Stepped parse ≡ full read | `test_lsv1_stepped_parse_matches_full_read` | — | **PASS** |
| Save emits LSV1 when `publishedSnapshotIsCurrent()` | `test_write_loop_persisted_emits_lsv1_when_published_snapshot_current` | Post-`root_rev=2` saves in [`175309`](../../captures/session_20260911_175309.log) (timing only) | **PASS** native; device timing **INFERRED** |
| Legacy slot → hydrate → LSV1 save | `test_legacy_slot_migrates_to_lsv1_after_loaded_hydration` | — | **PASS** |
| LSV1 load skips `rebuildSlotFromLoopContent` | `test_lsv1_load_commit_skips_rebuild` | — | **PASS** |
| Legacy probe / reject unsupported | `test_lsv1_probe_legacy_and_unsupported`, reject fixtures | — | **PASS** |
| Legacy body round trip (compatibility) | `test_write_read_loop_snapshot_roundtrip`, `test_serialize_reload_content_undo_units_match` | — | **PASS** |
| Clear-slot `PersistedLoopSnapshot` restore | `test_clear_slot_before_published_snapshot_field_retains_root` (5.1) | 5.1 HITL [`150709`](../../captures/session_20260911_150709.log) | **PASS** (5.1) |
| Load → undo depth 0, redo empty | *Not on native* — `clearUndoHistoryForSlot` gated `#if !PIO_UNIT_TEST_NATIVE` in `applyLoadedLoopSlotBodyCommit` | Reboot + `LoadLoopJob` with LSV1 slot file; or dedicated HITL reload slice | **OPEN** |
| LSV1 bytes on SD after post-publish save | — | `hexdump` / probe `loop_*_*.bin` for `LSV1` magic | **OPEN** |
| Boot `CurrentSetBootLoad` LSV1 | — | Out of scope until boot path migrated (5.3 non-goal) | **N/A** |

## Architecture gate (verification only)

| Question | Answer |
|---|---|
| **Owner** | Verification only — no firmware change in 5.4 unless a gate fails |
| **Ownership change?** | NO |
| **Transition change?** | NO |
| **Execution bound change?** | NO |

## Stages

| Stage | Scope | Status |
|---|---|---|
| **5.4.1** | Native matrix sign-off (table above) | **DONE** — `pio test -e native` **1822/1** at 5.3.4 close |
| **5.4.2** | SD byte probe on device slot after LSV1 save | OPEN |
| **5.4.3** | Reboot + runtime `LoadLoopJob` LSV1 reload; assert GUS depth 0 / redo empty | OPEN |
| **5.4.4** | Check off `tasks.md` 5.4 + `ARCHITECTURE-REVIEW.md` when 5.4.2–5.4.3 pass | OPEN |

## Next actions

1. After flash with 5.3 firmware: save loop with `publishedSnapshotIsCurrent()`, pull `loop_TT_SS.bin`, confirm `LSV1` at body offset.
2. Reboot Teensy; confirm `LoadLoopJob` parses LSV1, `publishedSnapshotIsCurrent()`, no `rebuildSlotFromLoopContent` log, undo stack empty for slot.
3. Optional: extend HITL verify module with LSV1 probe helper (no new canonical stimulus required for 5.4.2).

## Explicit non-goals (5.4)

- Boot sync load LSV1 (`CurrentSetBootLoad`)
- 5.5 / 5.6 deferred persistence bounds (separate tasks)
- Dropping `PersistedLoopSnapshot` dual-write (task 6.x)
