# C3c — boot-restored slots import from the persisted pass records they restored

Date: 2026-11-01. Owner: "Start C3c" (DEC-246 amendment 17: C3c before C5).

## Finding: production already hydrates the persisted passes; no second source is needed

The C3b claim that restored slots are `NoCommittedSource` was a design assumption, never measured. The code and the
device show otherwise:

- **Both persisted formats carry the full pass records.** The slot file holds each capture pass's header (id,
  `mergeSequence`, state, type, `sealedAtTick`) with its events, then the edit-pass tail.
  - The legacy format is passes-only.
  - Body V1 embeds the passes plus a root that was proven equivalent to them (DEC-179).
- **Boot restore installs them as the live passes.**
  - `readLoopPersisted` → `readPersistedLoopSnapshot` reads every capture pass (its events staged into fresh committed
    chunks) and every edit pass.
  - `Loop::adoptPersistedSnapshot` then does `passes = std::move(snapshot.passes)`.
- So the importer's existing source (`loop.passes`, resolved by id each step) **is** the persisted-pass source once a
  restore has applied. A separate `PersistedPassesSource` would duplicate a hydration production already performs,
  and it would need its own SD reader on the real-time device.
- **Before the restore applies** (boot queues "playback loop slot restore … pending"), the slot has no committed passes.
  It gives `NoCommittedSource`: a named refusal with nothing written. The importer begins again when the restore
  changes the source key. A snapshot is never imported.

## Host evidence

`test_revision_store_differential` now links the persisted slot format (`StorageLoopIo.cpp`, `StorageLoopIoBodyFormatV1.cpp`
and their dependencies). Two new tests:

- `test_restored_slot_imports_from_its_persisted_passes`:
  1. Setup: Record, one overdub with a Hide companion, one overdub with a Shorten companion.
  2. Before the save: `revision()` (`IndexOnly`, index published) equals the passes view.
  3. Round trip: `writeLoopPersisted` → `readLoopPersisted` into a fresh `Loop`. The record pass, the overdub count and
     the edit-pass count all survive.
  4. Result: the restored import equals the pre-save import row for row, and equals the restored loop's passes view.
     The hidden note stays hidden, and the shortened note keeps its new end (the original NoteOff at 1500 is absent).
- `test_slot_before_restore_is_a_named_refusal`: an empty slot gives `NoCommittedSource` and 0 owned pages.

Suite: 29 / 29.

## Device evidence — reboot, no stimulus

- Image: probe/shadow `ed82d0b6e0bf1cba…`.
- Session: forced fresh boot `captures/session_20261101_211044.log`; `boot_reset,crash=0`, `BOOT,load,ok`.
- Boot rows: `Slot restore applied saved passes; root not current: track=7 slot=3` (plus tracks 2 and 3).
- No chain was run: the slot is the armed owner-chain take saved by the previous session.
- Raw rows: [status](2026-11-01-c3c-boot-status-raw.txt), [windows](2026-11-01-c3c-boot-windows-raw.txt).

| Reading | Value |
|---|---|
| Import of the restored slot (t8 / s4) | `last_result=0` (Ok), 13 capture runs, 1,210 rows, 0 unpaired NoteOffs |
| Shadow publishes / refusals since boot | 1 / 1 (the refusal is consistent with the slot before its restore applied; not separately proven) |
| Per-run grid phase | identical to the pre-reboot import (Record 319/319 on the grid, first at tick 0) |
| Whole loop, shadow `revision()` vs passes | **1,210 / 1,210**, 0 / 0 differences, identity 1 |
| Windows compared / matched | **10 / 10**, `c4_clean=1` |
| Import worst phases (begin, captures, pair walk) | 100, 172, 177 µs |

## Not covered

- **A restored slot with edit passes on device.** Production skipped every overdub companion on the owner chain (DEC-246
  amendment 16), so the saved slot has none. Edits across the round trip are covered host-side.
- **Body V1 specifically.** The host round trip writes whichever format `writeLoopPersisted` selects. The device slot
  was restored passes-authoritative ("root not current"). Both formats read the same pass records.
- Slots that are never restored in a boot are not imported; the shadow follows the selected slot only.

## Consequence for the plan

C3c needs no new source type. DEC-246 amendment 13's `LivePassesSource` / `PersistedPassesSource` split collapses into
the one live source, recorded as amendment 18 (finding, for the owner to confirm). The "no snapshot as source" rule
stands unchanged. C5 is not blocked by restored slots.
