# EVIDENCE — S2b gates' slope-to-failure sweep (2026-10-01, current image)

**Run:** `captures/s13-sweep-06` (`scripts/hitl_library_growth_sweep.py`) and
`captures/s13-sweep-06-post-census` (the same library read by `scripts/hitl_boot_census.py`).
**Scratch:** track 6, slots 1..8 (the authorized scratch range), a 4-bar take per step, a verified reboot per step.
**Corroboration:** `captures/s13-sweep-05-full` (2026-09-24, older image): 8 steps, banner on each, 16 walk rows
each, zero non-zero `credits` — the same flat shape.

## The eight boots, one row each (`sweep-result.json`)

| step | slot | reset ok | banner | reconcile warnings | free before | free after | N committed |
|---|---|---|---|---|---|---|---|
| 0 | 1 | yes | yes | 0 | 512 | 480 | 35 |
| 1 | 2 | yes | yes | 0 | 512 | 480 | 41 |
| 2 | 3 | yes | yes | 0 | 512 | 480 | 44 |
| 3 | 4 | yes | yes | 0 | 512 | 480 | 44 |
| 4 | 5 | yes | yes | 0 | 512 | 480 | 35 |
| 5 | 6 | yes | yes | 0 | 512 | 480 | 43 |
| 6 | 7 | yes | yes | 0 | 512 | 480 | 35 |
| 7 | 8 | yes | yes | 0 | 512 | 480 | 44 |

`N` is the **whole library's** committed-slot count, not the step index: the sweep grew inside an already-large
library, which is why N sits at 35-44 while eight new takes were added. Every boot reads the same figures, and the
library walk is 16 rows with `credits=0` on every one.

**Post-restore, on the same grown library** (`s13-sweep-06-post-census`):

```
[census] POST-RESTORE free_chunks=397 heap_free=159744 (admission=1 interactive=1 undo=1 stopped=1 deferred_prewarm_idle=1)
   checkpoint 0 (enabled-set walk): 512 -> 480 (delta -32)
   checkpoint 1 (library walk):     480 -> 480 (delta +0)
[census] S2b.3: free=397 vs required 16 -> met=True
```

## What this establishes

* **The reservation is flat in the committed-slot count, over eight verified reboots.** The only boot cost is the
  **enabled set** (`512 → 480` = 32 chunks, constant); the library walk adds **nothing** (`480 → 480`) and **every
  per-slot reservation reads `credits=0`**. The pre-change baseline spent 8 chunks (41,056 B) per committed slot, so a
  slope would have shown `free_before` falling with N; N moving between 35 and 44 changes nothing.
* **No failure point was reached, and that is this gate's expectation, not an omission:** all eight steps completed
  with `reconcile_warnings=0` (the catalog-reconcile failure that stopped the 2026-09-24 attempt did not recur),
  nothing exhausted residency or canonical capacity, and the gate's own text keeps "the genuine 60-long-slot and
  total-residency gates" in **S6/S7, not this prerequisite slice**.
* **The per-committed-slot pool reservation before any activation is zero on device** — the device half of S1.5's
  first clause, matching the native leg's corrected "exactly zero" finding.

## Not claimed

* The 2026-09-24 sweep's failure was a **USB** fault (the board dropped off), not capacity; this run records that the
  shape did not recur and does not re-explain it.
* **S1.5's refusal clause is untouched:** "the smallest N at which a 24-bar overdub is refused" needs 24-bar takes
  (48 s each at 120 BPM) added at increasing N until a refusal, and the earlier record for that clause was already
  withdrawn (a fixture seal failure, `SealOutcome::FailedValidation`, not a device refusal). This sweep is 4-bar
  takes.
* No S6/S7-scale residency claim, and no per-slot byte breakdown beyond the walk's `credits`.
