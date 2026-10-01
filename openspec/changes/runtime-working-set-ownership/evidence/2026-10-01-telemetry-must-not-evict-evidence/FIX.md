# FIX — the persistence telemetry no longer floods the capture ring (the competitor a device leg ran into)

**Date:** 2026-10-01
**Image:** `014af85dc913b1c5f9e63684f4df689ccfabe4887874823023cea14bdab544da` (new; the previous session image was
`cdab16f4…`). RAM1 unchanged: `variables 96,192 / ITCM 393,216 (code 391,416 + padding 1,800) / locals 34,880`,
padding floor 840 → **no `ram1_budget.json` re-base**.
**Runs:** `captures/s4b9-cross-track-12 … -16`, seeds `captures/s4b9-seed2-t5s3`, `-seed3-t5s3`.

## What the leg was actually blocked by

The user's requirement, verbatim in effect: the leg must work **while** the device does its boot/large-library work —
that state must not be waited out. The measured competitor was not the restore *work* but its **telemetry**:

* a workspace save of one track's metadata (TrackMeta + one SlotMeta per slot) emitted a `PERS,work` row **per
  slice** — 241 rows per slot body, **5 951 rows in one boot** (`attempt 11`, and 6 967 in `attempt 14`);
* the loop-snapshot tail write emitted a row **per step** — 2 705 in one boot;
* that volume made the capture ring **shed** rows (`RING,shed`) and once **wipe** (`RING,wiped`), and the sheds
  dropped the record/phase rows the acceptance surface fences on. `attempt 10` shows the full chain: shed rows →
  `SERIAL_INCOMPLETE` → run abort → the successor-start gate refusing with *"no predecessor stop-completion
  receipt"* **while the receipt row was in the capture** (61.683 s, 2.9 s after the stop). The gate's own silent
  abort branch hid that, so the investigation went after the device instead of after the abort.

So the fix has two halves: stop the diagnostic from evicting the evidence, and stop the gate from misreporting why
it refused.

## Firmware — the diagnostic yields to the evidence

* New `include/Utils/TelemetryCadence.h`: `kSliceLogInterval = 16`, `kTailStepLogInterval = 512`,
  `kTailWriteStepLogInterval = 128`, and a pure `shouldLogSlice` / `shouldLogOccurrence`. A phase's **first** slice
  always logs (the acceptance surface asserts phase presence) and the emitted duration is the **running maximum**,
  so a bound check over the rows it can see stays a faithful upper bound rather than a sample of one.
* `StorageManager/PersistenceWorkItemPersist.cpp`: the LoopPersist `payload_slice` / `finalize_slice` rows go
  through the cadence, with the state reset whenever the item's key changes.
* `StorageLoopIoBodyFormatV1.cpp`: the tail-step emitter no longer treats `sigChanged` as an emission trigger — the
  progress signature includes the write offset, so it changed on nearly every step and made the interval
  irrelevant (3 275 rows in `attempt 14`). Stall detection (`unchangedSigStreak`), phase changes, the interval and
  completion still log.
* `StorageManager/WorkspaceSave.cpp`: the hand-rolled per-step `loop_snapshot_tail,step` row is gated by the
  cadence and the counter resets when a tail write begins.
* The new counters live in `DMAMEM` (OCRAM), following the RAM1 budget's own recorded rule that mutable telemetry
  counters must not grow DTCM; RAM1 variables are unchanged at 96,192 B.

**Measured, same class of work, before → after:**

| family | attempt 11 / 14 (before) | run 15 / 16 (after) |
|---|---|---|
| `PERS,work` rows in a boot | 5 951 / 6 967 | 3 059 / **382** |
| `payload_slice` per slot body | 241 | ~18 |
| `loop_snapshot_tail,step` (tail write) | 2 705 | **29** |
| `RING,wiped` | 1 (attempt 11) | **0** |
| `RING,tier_a_evicted` | 0 | 0 |
| `RING,shed` | 3 | 2 (residual) |

## Host tooling — the gate names the real cause

`scripts/hitl/legacy_record_baseline.py::_await_successor_start_receipt`: when the run has already aborted, the
gate now prints the **abort reason** and says the receipt was not the decider, instead of returning silently into a
caller that reports "no predecessor stop-completion receipt". Pinned by
`test_capture_transitions.py::test_an_aborted_run_is_named_instead_of_claiming_a_missing_receipt` (red-proved: with
the silent branch restored the fixture fails; the file was restored byte-identically).

## What the legs show on the fixed image

| run | receipt gate | leg | evidence integrity | notes |
|---|---|---|---|---|
| 12 (before the tail fix) | refused | — | `wiped` 0, `shed` 2; **the previously missing record rows are present again** | the prelude scenario's take was empty (`note_on_count: 0`, 267 ticks, transport stopped 0.33 s in) — a **prelude** failure, not the leg's |
| 13 | **admitted** (`Record pass undo reset for slot`) | `switch sent: True`, `Selected track 5` mid-finalization | `wiped` 0 | first cross-track leg of the session to pass its preconditions |
| 15 | **admitted** | `switch sent: True`, `Selected track 5` 48.487 s, destination press acted (`Loop 2: Toggle Play/Stop`) | `wiped` 0, `tier_a` 0 | |
| 16 (tail fix + DMAMEM) | **admitted** | `switch sent: True`, `Selected track 5` 47.131 s, `Loop 2: Toggle Play/Stop` | `wiped` 0, `tier_a` 0; **issues reduced to the base recipe's two known items** — no `*_missing`, no `missing_phase_boundaries` | `overflow_free` / `call_site_gate_ok` true |

**Verification:** `pio test -e native` **2,663 cases / 2,656 succeeded / 7 skipped / 0 failed** (the new cadence
fixture is the +1); `scripts/test_capture_transitions.py` 62/62; two cadence red-proofs (carry the last slice
instead of the max; drop the cadence) with the header restored byte-identically (`f2f76514…`); one gate red-proof;
`teensy41-capture-serial` SUCCESS with the RAM1 line unchanged.

## What this does *not* claim

* **The leg's own clause is still open** (see [`../2026-10-01-s4b9-cross-track/EVIDENCE.md`](../2026-10-01-s4b9-cross-track/EVIDENCE.md)):
  with the destination track *not playing*, a slot press there is a selection (`Toggle Play/Stop`), so the
  `Queued playback switch` shape still needs the destination track's loop to be **playing** — run 15/16's
  `Loop 2: Toggle Play/Stop` is that semantics, not a missing fixture.
* The prelude failure in run 12 is recorded, not diagnosed; it did not recur in 13/15/16.
* The restore-retry churn (the other device finding) is untouched by this change.
