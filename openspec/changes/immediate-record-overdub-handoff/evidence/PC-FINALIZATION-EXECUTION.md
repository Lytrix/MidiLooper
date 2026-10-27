# P-C — successor-safe finalization, undo and empty cleanup

## Execution identity and preflight

- Task2.2 (C4 implementation half); [P-C packet](../packets/P-C-FINALIZATION.md), [contracts](../CONTRACTS.md) C4.1–C4.7, **DEC-160 — a predecessor's finalization is local to its own interval** (ratified), DEC-159 custody, DEC-142 presentation lifetime split.
- Branch `feature/unified-rolling-playback-path`; baseline HEAD `faa089b8365720972f8535decd747180a963723f` plus the pre-existing dirty tree. No commit/reset/rebase, no upload/device/SD/content operation.
- Historical reuse: no new undo kind, no changed routing, no early private root authority, no per-interval persistence receipt, no custody change (that is P-A), no source or visual change.

## What changed

| Owner | Change |
|---|---|
| `UndoEntry` | gains `uint64_t interval` — the capture interval that produced the checkpoint. `0` means unqualified (restored/derived history) and stays prunable by any bound, preserving shipped behaviour for it. Measured **132 → 144 target bytes**. |
| `TrackUndo::pushRecordPassAdded` | job-owned overload takes the interval, stamps it on the entry, and prunes with that bound. |
| `eraseUndoEntriesForSlot` | removes only entries with `interval <= maxInterval` (default `UINT64_MAX` keeps every existing caller's behaviour). |
| `Track::hasLiveOrSealedSuccessorForSlot` | new read-only, allocation-free predicate: a live Capture on this slot with a later interval, a custody receipt with a later interval for the same slot/LoopId that still owns or retires an extent, or a queued successor stop that has not sealed yet. |
| `Track::stepDeferredRecordEmptyResult` | `resetGeometry = activeCapturePassCount() == 0 && !successor`. With a successor the grain no longer calls `resetPassTimeline`, does not write `loopLengthTicks`, `loopStartTick`, `startLoopTick`, `nextEventIndex` or `lastTickInLoop`, and does not resolve the transport state; it releases only its own custody obligation through the existing retirement path. |
| `CaptureStopJobs` size pin | 29,472 → **29,536** (+64 = the larger `TrackUndoPublication` the job carries). |

`presentations[]`/custody ownership is unchanged by this packet — P-C only decides *who may clear shared state* and *which undo checkpoints a predecessor may prune*.

## Oracle results

`P-C-1` — `test_filtered_empty_record_does_not_clear_live_B` (**GREEN**):

```text
P1,empty_A_B_live,state=6,interval=3,jobs=1,raw=2,rawgen=1,producer=2,committed=0,changes=1,undo=0,length=768
P1,A_worker_drain,state=6,interval=3,jobs=0,raw=0,rawgen=0,producer=2,committed=0,changes=1,undo=0,length=768
P1,empty_A_late_cleanup,state=6,interval=3,jobs=0,raw=0,rawgen=0,producer=2,committed=0,changes=1,undo=0,length=768
```

B keeps its interval (3), its two raw rows and its producer; loop geometry stays **768** and its pending-change count stays **1**; A is released (`raw=0`, `rawgen=0`); no committed Record pass. Previously geometry fell to 0 and pending changes to 0.

`P-C-2`/`P-C-3` — `test_successor_undo_checkpoint_survives_a_late_predecessor_publication` (**GREEN**): a successor checkpoint at `intervalA + 1` is already on the stack when the predecessor publishes at `intervalA`; both survive with causal order, and the shipped supersede rule still holds — a Record take at `intervalA + 2` prunes both.

`P-C-4` (exactly-once) and `P-C-5` (stale identity) are carried by the existing controls that remain green in the full native run: the late-publication control, the clear/stale-range-receipt control, and the empty-successor-stop control.

## Verification receipts

- Focused `test_immediate_capture_handoff`: **9/9 PASS** (`pc-focused2.log`).
- `IMMEDIATE_HANDOFF_CASE=empty`: **exit 0**, 2/2 tests (`pc-empty2.log`).
- Full native: **3,460 total / 7 skipped / 3,453 passed / 0 failed**, 301.6 s (`pc-full-native.log`).
- Shipping `teensy41-capture-serial`: **SUCCESS** — RAM1 variables 96,192 (unchanged), code 392,248 (+128), padding 968 (−128), locals 34,880 (unchanged): the code delta is absorbed by padding, net RAM1 unchanged; RAM2 and EXTRAM unchanged (`pc-build3.log`).

## Focused independent review and its repairs

One read-only lifecycle review returned **REQUEST CHANGES** with one medium finding and two latent
traps. All three were fixed and retested.

1. **Medium — the empty-take geometry policy was dropped, not transferred.** With a successor the grain
   skipped `resetPassTimeline`, and nothing re-applied the policy later. If B was an *overdub* that also
   accepted nothing, the slot kept geometry 768 with no passes and no rows, stayed PLAYING, and was never
   persisted — the pre-slice inline result was geometry 0 / EMPTY. Repair: the slot records the debt in
   `CaptureStopJobs::emptyGeometryOwed[slot]` (64 target bytes, pinned with the ring) and
   `Track::settleEmptyRecordGeometry()` applies it on the first deferred turn where the Track owes no
   capture and no custody, clearing the debt untouched when the successor published content. New case
   `test_both_empty_intervals_settle_to_the_empty_geometry` asserts geometry 0 and `TRACK_EMPTY`.
2. **Latent — the 3-arg inline `pushRecordPassAdded` stamped `UINT64_MAX`.** It now stamps `0`
   (unqualified), which is prunable by any bound and therefore keeps the shipped behaviour if that route
   is ever reached.
3. **Latent — only the Record route stamps the interval.** Recorded in the `UndoEntry::interval` comment:
   every other live push leaves 0, which stays safe only while a successor's entry can be pushed just
   when it is the stop-ring head; moving that guard requires stamping the producing interval everywhere.

Review results that needed no change: the successor predicate has no reachable false negative (its live
clause holds through the stop window; a zero-row successor is covered by the queued-job clause) and no
permanent false positive (receipts always reach `Free`, and `Track::clear()` invalidates live receipts and
queued jobs); `interval <= maxInterval` is correct for all four erasure callers and `interval` is RAM-only,
so no STK token or format changes; exactly-once publication is unchanged; and `markDisplayCachesStale` is
the only shared-state write left on the successor path. The review also noted the peer mirror
`test/test_global_undo_slot_scope` still mirrors the pre-DEC-160 predicate; that is recorded here as
follow-up rather than changed in this slice.

## Final verification after the review repairs

- `IMMEDIATE_HANDOFF_CASE=empty`: **exit 0**, 3/3 — predecessor-local cleanup, scoped undo, and the
  both-empty settle (`pc-empty4.log`).
- Default `test_immediate_capture_handoff`: **9/9 PASS** (`pc-focused4.log`).
- Full native: **3,460 total / 7 skipped / 3,453 passed / 0 failed**, 301.6 s (`pc-full-native2.log`).
- Shipping build: **SUCCESS**, RAM1 variables 96,192 / code 392,248 + padding 968 / locals 34,880
  (`pc-build4.log`); ring pin **29,600** (+64 undo publication interval, +64 empty-geometry debt).
- Preserved probe: `UndoEntry` **144** target bytes (the packet predicted 144), `CaptureStopJobs`
  **29,600** (`custody-pc4/custody-sizes.md`). The exact ring pin was relaxed to a range only long
  enough to read each measured value, then restored with the number and its reason.

## Not closed by this slice

- P-B private-source preparation/deadline and P-D bounded visual continuation remain separate packets; task2.2.12 (bound the prepared-index continuation) is still open.
- Device evidence NOT RUN: the empty-take path's PSRAM/stack behaviour, retirement-lane stack high-water, target allocator execution, and input/output timing. A successful link is not programming or boot proof.
- No upload, reset, content stimulus, SD operation, commit or push; installed `74392e15` unchanged.
