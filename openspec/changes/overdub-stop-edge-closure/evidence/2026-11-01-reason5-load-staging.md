# 2026-11-01 — the `order_deny reason=5` Record-stop wedge: SD-load staging leaked into capture seals (DEC-240)

## Symptom

Owner-rig chain (`scripts/hitl_midifile_chain.py`, 20-bar Record + 12 overdubs) wedges at the Record stop:
`DIAG,order_deny,reason=5,phase=1,canon=1,chunk=0,id=84,ref=1,persist=3,inuse=1,used=256,expected=256,count=632,chunks=3`,
then ~12,000 `preparation_denied`, 0 overdubs. Reproduced **3/3** on 2026-11-01:

| Capture | Image | Record started | Result |
|---|---|---|---|
| `session_20261101_003116.log` | `cb4694f5…` (profile) | — | wedged |
| `session_20261101_003547.log` | `975e0577…` (profile) | 15.1 s | wedged |
| `session_20261101_003852.log` | `909c930d…` **default, no profiler** | 14.1 s | wedged |

The same default image ran two valid chains on 2026-10-31 (`session_20261031_232715.log`, Record at 35.1 s;
`233358.log`, 23.9 s), so the trigger is device/card state and timing, not the image.

## Mechanism

- Persistence state 3 = `Persisted`. Only two paths set it: a finished mid-pass write (`markChunkPersisted`, from
  `Writing`) and `markChunkPersistedFromSdLoad`, which `LoopEventStore::sealChunk` takes whenever
  `isSdLoadStaging()` is true.
- In every wedged take the mid-pass writer logged exactly one row at Record start —
  `midpass_skip,reason=1,queue=0,admit_refused=0` — and never another: no chunk of the take was admitted *or*
  refused, yet its chunk read `Persisted`. So the take's chunks were sealed through the SD-load branch.
- `sdLoadStagingDepth_` was held by `SlotLoadSession` for its whole lifetime. A load job keeps its session alive
  across main-loop turns and pauses while capture runs ("Recording still defers the load"), so a Record started
  while the boot restore sweep still had a load in flight sealed every capture chunk as "already on SD". Those
  chunks are never written, and the Record stop's private-order preflight (requires `NotScheduled`) denies them
  forever.
- The earlier note in `docs/Plans/display_base_staged_preparation_bugfix.md` ("zero `PERS,mid_pass … ok:t7:s3`
  rows in every denied session; Record arms before the clear's save has finished") is the same signature: the
  writer had nothing to write because nothing was ever queued.

## Repair (DEC-240)

`LoopEventStore::SdLoadStagingScope` (RAII) around the load's own appends and tail detach in `StorageLoopIo`
only; `SlotLoadSession` no longer enters staging. No other behaviour changed.

## Verification

- Native RED→GREEN: `test_persistence_queue` /
  `test_capture_seal_while_a_slot_load_session_is_alive_is_queued_not_persisted` — before:
  `Expected 1 Was 3` (Queued vs Persisted, the device's `persist=3`); after: pass. `test_storage_manager_load`
  real-load smoke now also asserts an empty chunk queue and no open staging scope after the job retires.
- Full native: **3,822 cases / 8 skipped / 3,814 succeeded**. RAM1 unchanged in all three envs
  (default/no-grain 96,192 / code 391,928 + 1,288; profile 392,008 + 1,208).
- Device, default image with the fix `cbf3b47d02fe977fc06ac3cea675b4f2788d8a4a12b31eead0667570afb3588a`, same card
  state that wedged 3/3, boot proven each run (`boot_reset,crash=0`, `BOOT,load,ok`):

  | Run | Capture | Record at | overdubs | `order_deny` | `preparation_denied` | `win_gap_overrun` |
  |---|---|---|---|---|---|---|
  | chain-66 | `session_20261101_010150.log` | **12.9 s** | 12 | 0 | 0 | 3 |
  | chain-67 | `session_20261101_010518.log` | **13.0 s** | 12 | 0 | 0 | 4 |

  Both Records started *earlier* than the wedged ones, inside the fatal window. Direct confirmation in chain-66:
  at 12.92 s the writer reports `midpass_skip,reason=3` (**slot-load SD active**) with `queue=15`, growing to 19 —
  the take's chunks are now queued while a load is in flight, where before they were silently marked persisted.

## Timing of these runs — not comparable, not claimed

`parse_overdub_chain_timing.py`: chain-66 midi_gap med/max 88/158 ms, late_on max 118, frames>50 ms 0; chain-67
89/227 ms, late_on max 105, frames>50 ms 0. The P8 reference (Record at 24–35 s, after the restore sweep):
72–75 / 193–196 ms, late_on 155–170, frames>50 ms 29–45. The fixed runs record **during** the restore sweep —
a condition no earlier valid run had — so these are not an A/B of the fix. The `win_gap_overrun` rows (3 and 4)
also occur in older sessions of this tree (`151347`: 28, `183306`: 1, `212428`: 1). A like-for-like timing A/B
needs Record started after the sweep (or the sweep's own cost addressed); recorded as open.
