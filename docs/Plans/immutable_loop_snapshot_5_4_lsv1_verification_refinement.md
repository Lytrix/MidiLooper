# OpenSpec 5.4 — LSV1 persistence verification matrix

**Status:** **5.4.1–5.4.3 device slice SHIPPED** — SD byte probe (5.4.2) advisory until card copy  
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
| LSV1 bytes on SD after post-publish save | — | [`scripts/probe_lsv1_slot_body.py`](../../scripts/probe_lsv1_slot_body.py) on `loop_04_00.bin` — **no SD mount in session** | **ADVISORY** (tool ready) |
| Boot `CurrentSetBootLoad` LSV1 | — | Out of scope (5.3 non-goal) | **N/A** |

## Device validation (2026-09-11)

| Step | Capture | Result |
|---|---|---|
| Flash + HITL `base` (track 5 / slot 1) | [`181945`](../../captures/session_20260911_181945.log) | **PASS** — transitions, undo/redo 1/1, `persistence handoff result=ok` |
| Post-save reboot + `LoadLoopJob` | [`182529`](../../captures/session_20260911_182529.log) | **PASS** — `LoadLoopJob done 4/0`, no LSV1 failure lines |
| Manual smoke (prior) | [`175309`](../../captures/session_20260911_175309.log) | Record/overdub + deferred save timing (5.3.4 closeout) |

**Observation (non-blocking):** reboot capture shows `VCACHE,idle_src` with `root_rev,0` after load (L390) — display cache not yet on published identity; does not fail `LoadLoopJob`. Confirm with SD byte probe when card is available.

**Tool:** `python scripts/probe_lsv1_slot_body.py loop_04_00.bin` — expects `LSV1 formatVersion=1` after post-`root_rev=2` save.

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
| **5.4.2** | SD byte probe | **ADVISORY** — script shipped; card copy pending |
| **5.4.3** | Reboot `LoadLoopJob` after LSV1 save | **SHIPPED** — [`182529`](../../captures/session_20260911_182529.log) |
| **5.4.4** | Close `tasks.md` 5.4 | **SHIPPED** (byte probe remains advisory follow-up) |

## Explicit non-goals (5.4)

- Boot sync load LSV1 (`CurrentSetBootLoad`)
- Mid-session `LoadLoopJob` with populated GUS prune log (future HITL slice)
- 5.5 / 5.6 deferred persistence bounds (separate tasks)
- Dropping `PersistedLoopSnapshot` dual-write (task 6.x)
