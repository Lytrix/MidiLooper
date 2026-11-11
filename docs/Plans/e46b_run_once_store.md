# E4.6b — the run-once store (execution packet)

Authority: DEC-254 (owner approval 2026-11-11: "I choose DEC-254's run-once store"; deferral lifted), OpenSpec
`fixed-memory-engine` E4.6b. Branch `feature/engine-per-slot-history`. Not authority beyond this slice.

## Scope

A new store file `/MidiLooper/engine/store2.bin` in which each run is written **once**, as its own record, and each slot
keeps a small **A / B state record** (the states and which run records they list). Saving writes only the runs not on
the card yet. History depth, hot / cold and fetch are later slices (E4.6c–e): this slice still loads a slot's whole
history at boot, as today.

Non-goals: per-slot depth, cold states, fetch, pressure, library (milestone) format changes.

## Design

- **Reuse the slot stream.** `SaveJob` / `LoadJob` (`SetFile.h`) are unchanged: a slot is the same byte stream (header,
  run directory, rows per run, states, CRC). A new **sink** splits it — header, directory (with each run's record
  address) and states go to the slot's state record; a run's rows go to its run record, or nowhere when it is on the
  card already. A new **source** rebuilds the stream from the state record and the run records, so admission, the CRC
  check and full release on failure stay `LoadJob`'s. The stream's CRC (covering the rows) is the end-to-end check.
- **Layout** (512 B sectors): 0–1 the surface state (as today); then per slot two state-record copies of
  `kCopySectors` (a header sector "MLT1": slot, sequence, body bytes, check; then the body); then the **run area**
  (1 GiB): run records in extents of 8-sector clusters — a header sector "MLR1" (slot, kind, rows, rows CRC, check)
  then the rows, 32 a sector.
- **Write order** (power loss): a run's rows, then its header; all runs of a save before the slot's state record body,
  the record's header last (A / B with a sequence, as DEC-253). An interrupted save leaves the other copy and the runs
  it lists whole.
- **Run identity.** `RunId` → (record address, run serial): a run is on the card when its entry matches the run's
  serial (RunIds are reused). Set when a run record is complete and when a slot is loaded.
- **Space.** A cluster bitmap (PSRAM): built at boot from every valid state record (both copies of every slot), so a
  run no valid copy lists — freed, or from an interrupted save — is reclaimed at the next boot. During a session space
  is only taken (a session writes each new run once; 1 GiB holds 500 full content pools). Full: the save fails by name.
- **One sector a grain.** The sink queues sectors (at most four); a save grain writes one, only when the card is
  `ready` for it (no wait, E9.8h) — the timing rule of DEC-254.
- **Migration.** No `store2.bin` but a `store.bin`: the boot loads from the old store (today's code, read only), then
  opens the new store and saves every slot into it. `store.bin` is left on the card (not deleted).

## Transition oracle (native)

1. Save / boot round trip through the new store: every slot's history, current state, empty-below, note ids.
2. **Run-once:** after an overdub, the second save writes only the new run's record (counted sector writes) and the
   state record.
3. A damaged run record or state-record body: the other copy loads (or the slot fails by name).
4. An interrupted save: the previous copy loads.
5. Reclaim: after a clear and a save, the next boot's bitmap frees the old take's extent (allocation reuses it).
6. Migration: an old-format store loads, and its slots are written into the new store.
7. The existing persistence suite (state sectors, approval, A / B alternation) passes on the new layout.

## Device gate

Boot (`crash=0`, `BOOT,load,ok`), migration of the owner's set, a save after an overdub writing only the new run
(`!ENGINE` store stats), and timing: the worst persistence step no worse than today's worst pass (~0.9 ms) during a
dense overdub with saves.

## Evidence (2026-11-11)

- Native: `test_each_run_is_written_once`, `test_a_damaged_run_record_fails_by_name`,
  `test_a_run_no_copy_lists_is_reclaimed_at_boot`, `test_the_dec253_store_is_migrated`, and the 16 earlier persistence
  tests on the new layout; mutations (every run written, no rows CRC, no boot marking) each caught. Full native 4,551.
- Device `953d7297…`: migration — 29 slots loaded from `store.bin`, 27 saved into `store2.bin` (235 runs, 948 sectors,
  `save_failed=0`); reboot → loaded from `store2.bin` (273 clusters, history unchanged).
- Device `dea5111b…` (per-class grain timing in `!ENGINE take`): a take saved during playback — persistence's worst
  grain **94 µs**; the pass's other grains (transition 603, background 370, display 330 µs) unchanged in kind.
- Finding for E4.6d: the boot load's grains (32 sectors) reach 31 ms; a fetch while playing needs a few sectors a grain.
