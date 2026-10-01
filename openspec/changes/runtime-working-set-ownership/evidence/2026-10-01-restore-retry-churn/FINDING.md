# Finding — the restore-request retry loop is unbounded, so one slot can churn the whole session

**Date:** 2026-10-01
**Image:** hex sha256 `cdab16f4e5f65988a7383b9fd4633d965c76a77c226c73ce462abfa6e7afde3c` (flashed twice in the
session, both builds byte-identical to the recorded image)
**Came from:** the S4b gate's dedicated repeated-switch device run. The legs' *precondition* waits are where an
operator would notice it; the churn itself is what this file records.
**Runs:** `captures/s4b8-repeated-switches-02`, `-03`, `-04`, plus passive serial reads after a re-flash.

## Shape 1 — one coordinate churns with no progress between attempts

```
#CAP,37390300,RRQ,4,7,0,0,running,none,none
#CAP,37420123,RRQ,4,7,0,0,waiting,waiting_retry,none
[StorageManager] WARN: LoadLoopJob LSV1 parse failed
#CAP,37452681,RRQ,4,7,0,0,running,none,none
…
```

`RRQ` carries `(track, slot)` **0-based** — the boot fill passes `s` from `0 .. MAX_LOOPS_PER_TRACK-1`
(`src/StorageManager/CurrentSetBootLoad.cpp`, the "Boot playback set" loop) — so `4,7` is **track 5, Loop 8**.

| observation | window | `RRQ,4,7` transitions | `LSV1 parse failed` | how it ended |
|---|---|---|---|---|
| run 04 (`-04`) | 37.4 s → 68.3 s | 269 in ~31 s | 87 | 68.3 s: churn stops; `running` 105.424 s → `terminal,none,committed` 105.461 s |
| run 03 (`-03`) | 24.1 s → 289.5 s | 4 948 over ~265 s | 25 in the same window ([see below](#why-the-two-counts-differ)) | device **rebooted** (a fresh boot banner follows at 289 s; no crash report) |
| passive read after a re-flash | 12 s | 690 | 151 | still churning when the read ended |

At its worst the device emitted **5 118 serial lines in 12 s (~427/s)** from this one request.

The two outcomes are the same mechanism seen at different lengths: the request **commits as soon as the device is
idle** (run 04: 37 ms after its next `running`, which is the first service turn after the harness's final transport
stop at 105.07 s), and it **never finishes while the device is busy** (run 03: 4 948 attempts, then a reboot).

## Why it cannot stop (confirmed in code, not a hypothesis)

* `kMaxLoopSlotRestoreAttempts = 3` (`src/StorageManager/LoadLoopJob.cpp:71`) is **read** once, in
  `scheduleLoopSlotRestoreRetry` (`:83`), and **nothing in the tree ever increments `loopSlotRestoreAttempts_`**
  (`:72`; it is only zero-initialised, and the similarly named `markLoopSlotRestoreAttempted` records a
  *terminal* rather than counting — `src/StorageManager/LoopSlotRestoreQueue.cpp`). So
  `loopSlotRestoreAttempts_[t][s] >= 3` is never true and the "**bounded**: the request reaches a named,
  non-retryable outcome instead of retrying forever" branch that the function's own comment promises is
  unreachable.
* The retry re-queues through `appendBootLoopSlotRestore` (`:92`), and every retryable failure resets the slot and
  discards the attempt — `resetLoopSlotToEmpty` then `scheduleLoopSlotRestoreRetry` — including from the
  `LSV1 parse failed` path (`:681`).
* The request can therefore only end by (a) a service window it can actually complete in, or (b) being superseded
  or terminalled by another path. Nothing time- or attempt-bounds it.

This is the `deferred-slot-restore-liveness` area (whose archive claims the bound as done, task 385), not this
change's, and **no fix is proposed here**.

<a id="why-the-two-counts-differ"></a>
### Why the two counts differ

Run 03 shows many more transitions than parse failures; run 04 shows the reverse ratio. The retry path has several
**silent** entry points that re-queue without a warning — a bad save-file token, a failed `SD.open`, a short
payload, a failed seek (`LoadLoopJob.cpp:388 / :399 / :410 / :420`), plus a parse failure that *does* warn. Which
of those ran per attempt is not distinguished here: only the warn-bearing one is visible in the log, which is why
the same mechanism presents two different ratios.

## What it costs a HITL leg (measured, this session)

* **The same leg failed its precondition wait at the harness's default and passed at a longer one.**
  `captures/s4b8-repeated-switches-02` (state-sync wait at the harness default, 8 s) aborted with
  `overdub start refused: no predecessor stop-completion receipt`; the same runner with the same targets and a
  25 s wait (`-04`) proceeded and drove the full leg. The device's own rows in run 02 do contain a receipt
  (`PERS,result` at 61.683 s device time, 2.9 s after the stop at 58.771 s), so this is recorded as an
  **observation**: on this device state the default wait was not satisfied, and with 25 s it was. The churn is the
  standing difference from the leg that passed on a clean state (`captures/s4b8-leg-03`), but the *cause* of the
  miss is **not proven** here (row delivery versus harness observation timing is not separated by the evidence).
* One attach failed outright while the churn was running: run 03's `!STOP_STATUS` probe got no reply and the
  harness refused to run the scenario; the log's last rows are a **fresh boot** sequence with no crash report.
  Whether the reboot was caused by the churn or by the port being opened is **not** distinguished.

## Not claimed

* No failing *step* is named beyond "the parse returned `Failed`", no bytes were inspected, and the "one retry
  discards partial read progress" reading is what the code does, not a measured per-attempt accounting.
* The reboot's cause is not attributed.
* No owner, severity or fix: this is a device observation with its code path, recorded because it blocks the
  change's device legs and because it is adjacent to this change's subject (what the device does under memory and
  CPU pressure). The census-accounting question in
  [`../2026-10-01-pool-free-but-append-denied/FINDING.md`](../2026-10-01-pool-free-but-append-denied/FINDING.md) is
  its sibling: both are cases where a device-surface number and a device-surface admission disagree.
