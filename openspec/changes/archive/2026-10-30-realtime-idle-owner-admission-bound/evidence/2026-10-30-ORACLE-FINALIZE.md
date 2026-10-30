# LoopPersist finalize sub-stage probe — 2026-10-30

The instrumentation image `17693d9a…` (capture-only
`DIAG,persist_save,finalize,stage=…,us=…,done=…`) answers the 6.3 question: the residual step is
**`VerifyAndRename`**, 33-45 ms, on every LoopPersist save.

## Probe

- Image: `teensy41-capture-serial-no-grain`, HEX
  `sha256 17693d9ad55711fd973ca4f0a8147a1c8c2eca6c5628ef2abedae16aac64e902`, retained in
  `captures/17693d9a/`. Flash: loader 2.3, `Found HalfKay` -> `Programming` -> `Booting`, exit 0.
- Run: base preset, 2-bar record + overdubs, capture
  `captures/riao-finalize-probe/session_20261030_101144.log`.
- Each LoopPersist finalize reports four sub-steps:

| Stage | Measured |
|---|---|
| `WriteToken` | 4.0-5.7 ms |
| `EpochCrcBody` | 2.3-5.4 ms |
| `EpochCrcHeader` | 4.6-7.9 ms |
| **`VerifyAndRename`** | **33.6-44.9 ms** |

The 2-bar files are small, so the cost is not payload size: `verifySaveFileTokenAtPath` (open + seek +
read 4 B + close) plus `atomicRenameTempFile` (`exists` temp, `exists` final, maybe `remove`, `rename`)
is four-to-five SD metadata operations. The combined stage is 33-45 ms on every save, and the
`loop_rem,persist_save` aggregate reached 109,345 us when two finalizes landed in one turn.

## Conclusion

`VerifyAndRename` cannot be split into CPU slices: it is an atomic SD commit (rename) plus its safety
verify. The active-transport budget can only be enforced by **not starting that stage while the
transport runs** (defer it to stop; the mid-pass capture-chunk flush remains the safety net), or by
accepting the step as an indivisible commit and recording row 14 permanently coarse. The report-only
named-step row correctly shows the work item below the 50 ms bar (max 35.7 ms); the aggregate
`loop_rem,persist_save` still breaches when the finalize lands in a turn.

6.3 remains open. No policy change has been applied.

## Split probe (image `dda52d30…`)

The second instrumented image separates the stage, captured at
`captures/riao-split-probe/session_20261030_101615.log` (11 saves, every one consistent):

| Sub-step | Measured |
|---|---|
| `VerifyToken` | 2.1-5.2 ms |
| **`AtomicRename`** | **26.4-35.8 ms** |
| `SealJournal` | 4.8-6.4 ms |
| outer `VerifyAndRename` | 33.6-45.7 ms |

The atomic SD rename is the barrier; the token verify is cheap. Deferring only the verify would
recover ~2-5 ms and leave the rename on the timing-critical lane. The only way to keep the stage off
that lane is to defer the **rename** (and its follow-up seal-journal removal) while the transport runs,
or to accept the atomic commit as an indivisible step.

## Rename optimization probe (image `dca070a7…`)

`atomicRenameTempFile` previously did `exists(temp)` + `exists(final)` + `remove(final)` + `rename`.
The first exists is redundant after the token verify; the second exists + remove became one
unconditional remove. Same probe (`captures/riao-rename-probe/session_20261030_102155.log`, 14 saves):

| Sub-step | Before (median) | After (median) |
|---|---|---|
| `VerifyToken` | ~2.8 ms | 2.8 ms |
| **`AtomicRename`** | ~28.4 ms | **22.0 ms** (min 21.3, max **161.4**) |
| `SealJournal` | ~4.9 ms | 4.9 ms |
| `VerifyAndRename` | ~36.2 ms | 29.6 ms (max **169.1**) |

The two removed lookups save **~6-7 ms** per save. The remaining 22 ms is the FAT rename itself, and
the 161 ms max is a card-level stall: no code split can bound it. A defer-under-transport policy would
keep both the median and the stall off the live lane; accepting it would leave the live lane exposed.

## Second consistency probe and owner acceptance (DEC-215)

Second probe on the optimized image (`dca070a7…`, `captures/riao-rename-probe2/`, 12 saves):

- `AtomicRename`: min 21.3 ms, median **22.96 ms**, max **28.34 ms**, **zero over 50 ms**. The 161 ms
  single-rename sample from the first probe did not recur.
- The recurring 161,587 us `persist_save` one-shot at 80.46 s is **post-stop** (transport stopped at
  74.07 s) and is an aggregate save turn, not one rename.
- The live window contains three transport-running `win_gap_overrun` rows (overruns 7/1/8, max
  78/90/81 ticks) coinciding with the 22-28 ms blocking renames.

The owner accepted the blocking atomic commit: the rename stays, the redundant-lookup optimization
stays, deferral under transport was declined, and row 14 remains coarse/empirical. See DEC-215.
