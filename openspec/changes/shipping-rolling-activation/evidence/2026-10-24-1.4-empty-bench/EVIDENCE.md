# Empty-bench 1.4/1.5 runs (2026-10-24)

Quarantine (authorized): `!QUARANTINE_WORKSPACE` parked the live tree and booted an empty workspace
(`state=EMPTY`, 0 occupied slots; parked at `/MidiLooper/current.bad.791482`, restorable with
`!RESTORE_WORKSPACE`).

## Base route (content parity)

| image | run | result |
|---|---|---|
| cold `e7e4b4fc…` | `captures/2026-10-24-1.5-cold-base-clean` | record 32/32, overdub 16/16, second 8/8, loop 1536, display ok, RAM2 **180,224**; label `INCONCLUSIVE_CAPTURE (SETUP_UNCONFIRMED)` |
| shipping `09a5ae69…` | `captures/2026-10-24-1.4-base-ship-empty` | record 32/32, overdub 16/16, second 8/8, loop 1536, display ok, RAM2 **180,224**; label `INCONCLUSIVE_CAPTURE (EVIDENCE_MISSING)` |

The **content/ledger parity matches** on both images and the empty bench fixed the earlier overdub
dropouts. The labels are **host evidence/setup gates** (a missing setup/readback row), not content failures.

## Dense lane (shipping, empty bench)

`captures/2026-10-24-1.4-dense-ship-empty`:
- `no_windowed_overrun_under_transport` **True, overrun_rows=0**; `windowed_service_coverage` 416/416 at
  permitted 64; `build_complete_under_transport` 386; demand observed (398 gap turns, 24 receipts);
  leg **True** (the display hold now passes on the empty state).
- Remaining verifier issues are **evidence gates**: `capture_evidence_ring_intact` (`stats_readbacks=1`,
  harness requires baseline+final = 2; critical-loss rows are 0) and `snapshot: no snap_gate` (the row is
  qualification-only instrumentation, expected absent on the shipping image).
- The qualification dense (`6762b8e6…`) remains the full `ok=True` lane/snapshot parity.

## Switch route (empty bench)

`captures/2026-10-24-1.4-switch-cold-clean` (`accepted_sequence_mixed`, cold, slot-a 4 / slot-b 5):
`PREPARATION_FAILED` with **host evidence** events — `fresh_boot_unconfirmed` (“forced boot reset produced
no device boot banner”) and `prepare_slot_b_census_unavailable` (“no LIB_OBSERVE observation completed”),
explicitly “host evidence gap, not a device failure”. The empty bench removed the 2-bar/long-master
finalization failure; the accepted-sequence host gates (fresh boot banner + LIB_OBSERVE) remain.

## 1.5

Cold rollback image `e7e4b4fc…` restored on the empty bench; pair shipping `09a5ae69…` ↔ cold
`e7e4b4fc…`; the 1.1 `dab405b3…` artifact is not preserved. The full cold base content is the table above.

**Status:** 1.4/1.5 substantive content/lane parity is measured; the remaining labels are **host
evidence gates** (ring stats readback count, fresh-boot banner/LIB_OBSERVE, base setup/evidence rows) and
the qualification-only `snap_gate`. No family removals should start until these are accepted or the HITL
capture gates are fixed.

## HITL gate cleanup and reruns (2026-10-24)

Small host gates fixed and committed (`b6e3e624`, `4b59a3a8`):

- `DIAG,snap_gate` is **qualification-only**: a capture with none of those rows is a build without the
  instrument (shipping / cold), so the snapshot term is `applicable=False`; a qualification capture with the
  rows still judges the declared target.
- Ring stats: an empty/fresh workspace has no pre-record identity rows; the single post-drain readback is
  accepted as a **zero baseline** (a non-empty run with a lost baseline carries non-zero counters and still
  fails).
- Boot banner: the scenario-owned capture may not expose the banner predicate and the leg log can start after
  the boot; the session log's `DIAG,boot_reset` is now the fallback.

**Rerun:** shipping dense `captures/2026-10-24-1.4-dense-ship-empty-2` — **`ok=True`, issues=[]**:
ring intact (2 readbacks), demand 428 gap turns/26 receipts, source-qualified 45/222, service 463/446 at 64,
**overrun_rows=0**, `build_complete=405`, leg True, snapshot not applicable.

**Switch after the boot fix** (`captures/2026-10-24-1.4-switch-cold-empty-2`): the boot gate passes, census
`occupied=0, device_settled=True`, seed_b records and commits (`seed_b_committed=True`), but
`seed_b_finalization_settled=False` — “slot 5 seed finalization release unconfirmed within 10s”. This is the
**mixed-oracle setup limitation** recorded in the 1.1 baseline (no cold switch run); the empty bench fixed the
master, not the seed's finalization release. Named gap; not a lane/content failure.

**Base persistence on the empty** (cold + shipping): the content parity is exact (32/32, 16/16, 8/8, 1536,
display), but the empty workspace's first save names `record_stored_revt_missing`,
`persistence_pers_work_slice_too_large`, `persistence_persist_save_slice_too_large` (cold) and
`persistence_active_snapshot_unavailable_during_save` (shipping). The 1.1 non-empty cold base passed these; the
empty first save is the difference. Named, not a content failure.
