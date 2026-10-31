# Execution packet — slice 4 (remaining bodies, coverage, overhead A/B)

From [`docs/Templates/EXECUTION_PACKET.md`](../../../../docs/Templates/EXECUTION_PACKET.md). Tasks 5.1–5.6.

## Identity and scope

**Status:** code, native and build verified. Device coverage is partial. **The overhead A/B (5.5) is
BLOCKED** by the open stop-edge wedge, which reproduces on the pre-change default image (see § Device).

### Sites as landed

| Identity | Placement | Variant / ceiling | Memory |
|---|---|---|---|
| `PlaybackWindow_build` | free `ensurePlaybackMergedMidiEventsBuilt`, **after the reuse return** | long / FRAME | FLASH |
| `PlaybackViewService_grain` | block around the `stepFill` / `stepOrder` grain in `Track::stepPlaybackViewMaintenance` | **short** (row 27: ≤ 64 fill / ≤ 48 order units) / DEFERRED | FLASH |
| `SnapshotPublish_step` | around the `stepPendingSnapshotPublish` call (`TrackDeferredMaintenance.cpp`) | long / DEFERRED | FLASH |
| `SaveWorkItem_step` | whole `StorageManagerInternal::stepPersistenceWorkItem` (reached only when the writer is admitted) | long / DEFERRED | FLASH |
| `DisplayBase_gather` | whole `DisplayManager::buildCommittedDisplayBase` | long / FRAME | FLASH |
| `Display_frame_phase` | one scope per running phase case in `DisplayManager::update` (result 0 resolve / 1 draw / 2 transfer); never on Idle | long / **DEFERRED** (was FRAME) | **ITCM** |
| `LoadStep` | `stepLoadLoopJob`, after the no-job gate | long / DEFERRED | FLASH |
| `OverdubStart_body` | whole `Track::startOverdubbing`; refusals are timed exits | long / COLD | **ITCM** |
| ~~`IdleOwner_unit`~~ | **removed by decision** (below) | — | — |

The placements differ from the D12 table in three ways, each decided on code or device evidence:

1. **`IdleOwner_unit` removed.**
   - The first slice-4 device run measured **6,162,100 calls at about 6 µs** in one chain. The
     `runDeferredIdleMaintenance` admission is not a no-work gate, so the turn is the poll.
   - That is about 3.7 s of profiler cost per run.
   - D11 says a body that cannot be split from its poll is not instrumented; its children
     (`ViewPrewarm_unit`, `SnapshotPublish_step`, `PlaybackWindow_build`) keep their own scopes.
   - The identity number stays reserved (append-only). A native case pins the removal.
2. **`PlaybackWindow_build` moved after the reuse return.** On device the cheap reuse path was about
   32,000 calls per chain at around 17 µs. A native case pins that a reuse call is not measured.
3. **`Display_frame_phase` ceiling raised from FRAME (2 ms) to DEFERRED (5 ms).**
   - Draw and resolve phases routinely take about 3 ms. In one run, 1,780 of 5,001 phases breached
     2 ms and flooded the 256-record ring (`dropped=1,793`).
   - In the ring sample, 247 of 250 display records exceeded 3 ms and 35 exceeded 5 ms.
   - This is the DEC-218 lesson: ordinary traffic must not own the ring.
   - Report-only, so no bound changes.
4. **`SaveWorkItem_step` is in the flash callee**, not the ITCM `processDeferredSaveState`.

### Memory

| Env | RAM1 line | Note |
|---|---|---|
| profile | variables 96,192 / code **392,008** + padding **1,208** | ITCM +80 B, from the two ITCM owners (three phase cases plus `startOverdubbing`). Padding is 368 B above the 840 floor. `scripts/ram1_budget.json` now carries its own entry with a note, instead of the alias. |
| default | code 391,928 + padding 1,288 | symbols identical to pristine (addresses and names, `CSWTCH.N` numbering normalized; `__rtc_localtime` excluded); section headers identical |
| no-grain | identical to the default | |

## Native (task 5.3)

`test_runtime_profiler_sites`, cases added:

| Case | Asserts |
|---|---|
| `PlaybackWindow_build` | a build is measured and a reuse call is not |
| `IdleOwner_unit` | not instrumented (0 calls after an owner turn) and nesting stays clean |
| `OverdubStart_body` | counts every call, including a refusal |
| `SaveWorkItem_step` | counted |
| `LoadStep` | **not** counted without a job (the gate is the poll) |

Mutation: moving the `LoadStep` scope before its gate fails (`Expected 0 Was 1`).

Not reachable natively: the display owners (`DisplayManager*` is not linked by the harness) and the
rolling grain (no rolling demand in the fixture). Both are device-only, below.

Results:
- focused suites `test_runtime_profiler` + `test_runtime_profiler_sites`: 38/38;
- full native: see § Native full run.

## Device

**Images:**
- `cb4694f5…` (first slice-4 placement);
- `975e0577…` (corrected placement);
- default `909c930dea06465ebafa61b9d45aa6ae7ad5be1f24a423c2a90bf28e57e4b975`.

Boot was proven on every run (`boot_reset,crash=0`, `BOOT,load,ok`, and on the profile images one
`profile_boot` row delivered through the retained ring).

| Run | Image | Capture | Result |
|---|---|---|---|
| chain-63 | `cb4694f5…` profile | `session_20261101_003116.log` | **wedged**: `order_deny,reason=5,…,persist=3,used=256,expected=256,count=632,chunks=3`, 11,854 `preparation_denied`, 0 overdubs |
| chain-64 | `975e0577…` profile (corrected) | `session_20261101_003547.log` | **wedged**, same term, 12,029 `preparation_denied` |
| chain-65 | **`909c930d…` default, no profiler** | `session_20261101_003852.log` | **wedged**, same term, 11,817 `preparation_denied` |

**Conclusion:** the wedge is the open stop-edge defect named in `CURRENT_WORK.md` (the clear→Record
`reason=5` persistence-state term, first seen on `1bd8a99e…`), not the profiler.
- The default image, which gave two valid chains on 2026-10-31 (P8), now wedges on the same stimulus.
- So the trigger is device or card state reached after the first wedge, not the profile image.
- Further chains were stopped here: each clears the scratch slot again and reproduces the wedge.

### Coverage (task 5.4) — partial

From the chain-63 read-back (`session_20261101_003320.log`, complete stats dump, first placement):

| Domain | Identity | calls | max µs | over |
|---|---|---|---|---|
| Record edge | `RecordStart_edge` | 1 | 2,876 | 0 |
| prewarm | `PlaybackPrewarm_slot` | 18 | 125,113 (boot, USB transmit timeout; slice-3 finding) | 1 |
| off-edge preparation | `ViewPrewarm_unit` | 42 | 19,724 | 33 |
| window build | `PlaybackWindow_build` | 32,159 (pre-fix placement) | 8,740 | 59 |
| rolling grain | `PlaybackViewService_grain` | 450 | 171 | 0 |
| save step | `SaveWorkItem_step` | 981 | 38,028 | 12 |
| frame phase | `Display_frame_phase` | 5,001 | 8,780 | 1,780 (at 2 ms) |
| slot load | `LoadStep` | 525 | **300,935** | 46 |
| snapshot publication | `SnapshotPublish_step` | **0** — not exercised: no overdub committed before the wedge | — | — |
| display base gather | `DisplayBase_gather` | **0** — not exercised in this run | — | — |
| overdub start | `OverdubStart_body` | **0** — not exercised: the wedge prevented every overdub | — | — |
| monolithic views | `PlaybackPrewarm_views` | 0 — expected on the prepared path | — | — |

Seven of the ten domains have exercised calls. The three unexercised ones are named, not absent,
and need a valid chain. The chain-64 read-back on the corrected image was partial: the stats request got
no terminator while the wedged device flooded serial with stall rows, and the ring returned one batch.
It shows the boot-time `LoadStep` breaches again, including **300,518 µs**.

**Findings for the timing work (not investigated here):**
- `LoadStep` single steps reach about **300 ms**, and 36–37 ms is common, during the boot restore sweep (t ≈ 9–10 s).
- `SaveWorkItem_step` reaches about 38–42 ms.
- `PlaybackPrewarm_slot` takes 125 ms at boot (the USB transmit timeout, slice 3).

### Overhead A/B (task 5.5) — BLOCKED

No valid chain is possible on either image until the stop-edge wedge is repaired (owner priority 1 in
`CURRENT_WORK.md`). The A side already exists, P8's two valid default-image runs; the B side needs two
valid runs on `975e0577…`. Measured per-scope cost, for when it runs: short 286 / 360 cycles min/avg,
long 377–392 / 393–414 (0.5–0.7 µs).

### Ring capacity (task 5.6) — partial

- The 256-record ring was flooded within one chain by display phases at the 2 ms ceiling, so the ceiling
  was raised. A 5 ms ceiling would still have admitted roughly 250 display records per chain at the
  measured rate.
- TBD-3 stays open until a valid chain on the corrected image shows the ring's real fill. The dump
  batch sizes (16 / 32) behaved as designed in every read-back.

## Native full run

`pio test -e native` after the placement fixes → **3,821 cases: 8 skipped, 3,813 succeeded**, exit 0, no `[ERRORED]` suite.
`teensy41-capture-serial-no-grain`: variables 96,192 / code 391,928 + padding 1,288 (unchanged).

## Completion record

- **Eligible for checkoff:** 5.1, 5.2, 5.3.
- **5.4:** partial (7 of 10 domains).
- **5.5:** BLOCKED.
- **5.6:** partial.
- **Board left on the default image `909c930d…`.** The scratch slot (track 8 / slot 4) holds whatever the
  last wedged run left; the next valid run clears it first.
