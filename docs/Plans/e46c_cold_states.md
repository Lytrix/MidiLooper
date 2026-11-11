# E4.6c — per-slot depth, hot and cold states (execution packet)

Authority: DEC-254 (approved 2026-11-11), OpenSpec `fixed-memory-engine` E4.6c. Branch `feature/engine-per-slot-history`,
after E4.6b (`e46b_run_once_store.md`). Not authority beyond this slice.

## Scope

- **Per-slot depth 99.** Each slot keeps at most 99 states (`kSlotDepth`), hot and cold together; a commit past it
  forgets that slot's own oldest step — never another slot's.
- **Hot table unchanged.** `History`'s 99 shared entries (manifests in PSRAM) are the hot tier. When they are full, the
  oldest undo step anywhere is **demoted** (cold) instead of forgotten — when every run it lists is on the card
  (`Persistence::cardRun_`); otherwise it is forgotten as today (counted). Redo steps, slot currents and a session's
  protected start are never demoted.
- **Cold states** (`ColdTier.h`, PSRAM): per slot a list, oldest first, of descriptors — revision, loop length, and per
  run its record (sector, rows, kind) through a shared, reference-counted card-run directory. Cold states lie below a
  slot's oldest hot state; the UNDO level counts them.
- **On the card:** a slot's state record gets a **cold section** after the hot stream (its own magic and CRC); the boot
  loads it back into the cold tier and marks its runs' extents held (reclaim stays correct).
- **Undo into a cold state is E4.6d** (the fetch). Until then undo stops at the oldest hot state, counted by name
  (`undoAtCold`).

Non-goals: the fetch (E4.6d); boot loading hot states only, and rows pressure (E4.6e); milestones (DEC-258) keep their
hot chain only (cold steps are not in a saved stage).

## Transition oracle (native)

1. A full hot table demotes the oldest saved undo step (no `evictedUndo`, `demoted` counted); its slot's UNDO level is
   unchanged; an unsaved one is forgotten as before.
2. Per-slot depth: a slot's 100th state forgets its own oldest (cold first), and no other slot loses a step.
3. Save / boot: cold states round-trip (count, revisions, run records) and their extents stay held at boot.
4. Undo at the hot floor with cold states below stops, counted; the session rules (protected starts, folds) unchanged.
5. The existing History, persistence and surface suites pass.

## Device gate

Many steps on one slot past the hot table (the owner's busy slots), a reboot: the slot's UNDO level keeps its depth,
no other slot lost steps; `!ENGINE history` reports demoted / cold counts; timing as E4.6b.

## Evidence (2026-11-11)

- Native: `test_a_full_table_demotes_the_oldest_step_cold`, `test_a_slot_keeps_99_and_forgets_only_its_own`,
  `test_undo_stops_above_a_cold_state`, `test_cold_states_survive_a_reboot`; mutations (no demotion, cold not read back,
  no cold floor) each caught. Full native 4,555.
- Device `9faa6e4a…`: boot clean (cluster map 2,067 = runs + the reserved cold region); a take and an overdub with the
  table full → `demoted=1`, `evicted_undo=0`, persistence's worst grain 83 µs; after a reboot every slot's UNDO / REDO
  level unchanged (the demoted state came back hot: its slot was not saved since, and the table had room).
- Note: one busy slot alone never demotes — its 99 and the table's 99 are reached together, so it forgets its own
  oldest (per-slot depth); demotion is for several slots sharing the table.
