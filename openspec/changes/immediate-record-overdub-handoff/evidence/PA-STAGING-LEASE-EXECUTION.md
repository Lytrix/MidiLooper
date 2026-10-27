# P-A step 2 — additive staging and interval finalization lease

## Execution identity and preflight

- Task2.2.14 — staging roles and finalization lease; [P-A packet](../packets/P-A-CUSTODY.md), [contracts](../CONTRACTS.md) C1/C2, [staging finding](PA-STEP2-STAGING.md).
- Branch `feature/unified-rolling-playback-path`; baseline HEAD `faa089b8365720972f8535decd747180a963723f` plus the pre-existing dirty tree. No commit/reset/rebase, no upload/device/SD/content action. Owner approval: explicit **A — add roles** ("yes A"), recorded in [DEC-161 — additive frozen staging roles](../../../../docs/DECISION_LOG.md#dec-161--additive-frozen-staging-roles-preserve-closure-accounting).
- Historical reuse: DEC-065 — capture-admitted staging, DEC-078 — per-interval finalization, DEC-142 — Record presentation lifetime split, DEC-159 — immediate first-overdub sealed custody, DEC-160 — predecessor-local finalization. No new Manager, no SD schema, no ownership transfer.

## What changed

| Owner | Change |
|---|---|
| `RuntimePlaybackViewSet` | `captureStagings` 2 → **6** roles (five frozen + one live). `exchangeCaptureStagingRoles()` leases a free role and no longer swaps the worker's frozen binding. New `releaseSurplusCaptureStagings()` returns roles 2–5 to the pool. |
| `LoopPlaybackRuntime` | `captureStagingsAdmitted()` / `releaseSurplusCaptureStagings()` forwarding; `prewarmViews` admits all six roles for the capturing slot only. |
| `Track::CaptureStopJobs` | `presentations[slot]` became a 16-byte `{interval, custodyIndex}` handle; `custodies[5]` own the extents; `CaptureStopJob::frozenStagingIndex` carries the interval's frozen role. |
| `Track` admission | Interval-qualified `hasPendingStopForInterval()` replaces the per-track "any job" gate at the stop/wrap/emission sites; `freeCaptureCustody()` is the per-interval destination; `custody_cap_denied` when the slab is full. |
| Canonical worker | `bindCaptureStopWorker()` re-binds the single `captureStopOwner` and the Loop's single sealed-range borrow to **that interval's** receipt and staging role; no second writer or output. |
| Closure ownership | The frozen closure writes only the stopped interval's own canonical/staging rows, never the successor's live producer. |
| Release | `releaseCustodyObligation()` clears one bit per consumer and moves the receipt to `Retiring`; `releaseSurplusCaptureStaging()` returns the overlap roles only when the Track owes no capture and no custody. |

## Measured cost (shipping ARM, preserved probe)

| Term | Target bytes | Note |
|---|---:|---|
| `CaptureAdmissionStaging` | 2,612 | per-role metadata |
| Role event rows | 59,392 | `(20 + 9) × 2,048` |
| **One admitted role** | **62,004** | 2,612 + 59,392 |
| Six roles during A/B/C overlap | 372,024 | capturing slot only |
| Two-role baseline (steady state) | 124,008 | after the last custody retires |
| Overlap increment over baseline | **248,016** | requested external bytes, released again |
| `RuntimePlaybackViewSet` | 33,248 → **43,696** | +10,448 = 4 × 2,612 inline role metadata per allocated runtime |
| `CaptureStopJobs` | 29,704 → **29,472** | −232: eight 120-byte slot extents replaced by 16-byte handles while five 120-byte interval receipts own the extents |

The approved four-role minimum (+124,008) covers A/B/C plus live ingress. Implementation uses **five frozen + one live** so the contracted next Stop (`K_trace = 4`, `K_capacity = 5`) is admissible without a serialized fallback; the honest increment is therefore 248,016 while the overlap is live, not 124,008. Roles are admitted at Capture admission and returned when the Track owes nothing, so the footprint does not scale with recorded slots. The +10,448 inline metadata remains resident per allocated runtime and is a device-gate item.

## Oracle results

`P-A-1` — `test_early_stop_ends_B_without_reusing_A_custody` (**now GREEN**):

```text
P1,A_sealed,state=5,interval=2,jobs=1,raw=64,rawgen=1,producer=0,committed=0,length=23808
P1,B_stop_requested,state=5,interval=4,jobs=2,raw=64,rawgen=1,producer=0,...
P1,input_after_B_stop,state=5,interval=4,jobs=2,raw=64,rawgen=1,producer=0,...
P1,ABC_serial_drain,state=5,interval=6,jobs=0,raw=64,rawgen=1,producer=0,committed=1,undo=3,...
PA2,turns=382,phase=-1
```

The input after B's requested Stop does not grow B (`producer` stays 0), A's 64-row extent and generation are unchanged throughout, and A/B/C drain through one worker (`jobs=0`, three undo entries).

`P-A-2` coexistence: `test_frozen_closure_is_interval_owned_while_a_later_interval_is_live` GREEN. Cap/stale/empty controls: `test_five_real_custodies_cap_and_clear_without_reinterpreting_stale_handles`, `test_empty_successor_stop_takes_no_custody_extent` GREEN. New `test_surplus_staging_roles_release_only_after_custody_drains` asserts a frozen role refuses the return, four roles release when nothing is owed, and the next admission re-admits all six.

## Verification receipts

- Focused `pio test -e native -f test_immediate_capture_handoff`: **8/8 PASS** (`pa-step2-parent-focused.log`).
- `IMMEDIATE_HANDOFF_CASE=stop`: **exit0**, 4 tests, 0 failures (`pa-step2-parent-stop.log`).
- Focused prewarm + handoff: **256/256 PASS** (`pa-step2-parent-release-focused.log`).
- Full native: **3,459 total / 7 skipped / 3,452 passed / 0 failed**, 300.9 s (`pa-step2-parent-full-native.log`).
- Shipping `teensy41-capture-serial`: **SUCCESS**, RAM1 identical to baseline — variables 96,192 / code 392,120 + padding 1,096 / locals 34,880 (`pa-step2-parent-build.log`). Build only; not programming/boot proof.
- Preserved probe re-run: `CaptureStopJobs` 29,472, `RuntimePlaybackViewSet` 43,696, `CaptureAdmissionStaging` 2,612 target bytes (`custody-pa/custody-sizes.md`).

## Not closed by this slice

- P-C `IMMEDIATE_HANDOFF_CASE=empty` remains the intended RED: empty A cleanup still resets successor geometry (DEC-160 item 1). Not weakened here.
- P-B private-source preparation/deadline and P-D bounded visual continuation remain separate packets.
- Device evidence NOT RUN: PSRAM head-room under the 248,016 overlap and the +10,448 per-runtime metadata, retirement-lane stack high-water, target allocator execution/fragmentation, input-service gap and output lateness. Native ordering and a successful link are not device qualification.
- No upload, reset, content stimulus, SD operation, commit or push in this slice; installed `74392e15` unchanged.

## Focused independent review

One read-only lifecycle review of this slice returned **APPROVE**. It traced both admission routes, all
four pop sites, the six staging writers/readers and the release path, and attacked role reuse, the release
race, cap/ABA reinterpretation, exactly-once closure and single-writer ownership. Negative results with
their carrying invariant:

- A frozen role can never be handed out (`freeCaptureStagingIndex` excludes the live index, frozen roles,
  rows and open reservations).
- The single per-slot `frozenStagingIndex` is correct only because it always names the current worker
  owner's role; that is re-established by `bindCaptureStopWorker` on every acquisition and never clobbered
  while an owner exists.
- The release refuses while any role holds rows, reservations or a freeze, and the Track gate adds
  `count == 0`, no live capture and all receipts `Free`; re-admission is reachable on both the record-arm
  and overdub-start routes and never resets an admitted interval.
- A stale `{interval, custodyIndex}` handle cannot be reinterpreted (interval + owner + loopId +
  interval-derived generation), and cap exhaustion denies before any mutation.
- Each obligation bit has one acquire and one release edge; the retirement grain refuses while
  `obligations != 0`, and every pop site clears or zeroes obligations first.

One low-severity finding was fixed and retested: the `raw_seal_unexpected` branch left `job.presentation`
pointing at a receipt that `freeCaptureCustody()` could re-issue, so the still-running job could later
release a successor's grains. The branch now clears the reference; retirement already early-returns when
the job owns no extent, so nothing else had to change.

Recorded coverage gaps, not defects and not closed here: no test drives the release with
`liveStagingIndex`/`frozenStagingIndex` ≥ 2 (the index-rewrite branch), and none forces the seal-refusal
alias. The two-interval binding and retirement overlap are covered indirectly by the focused handoff
suite. Both belong with P-B rather than reopening this slice.

## Final verification after the review fix

- Focused prewarm + handoff: **256/256 PASS**, 8.7 s (`pa-step2-final-focused.log`).
- Full native: **3,459 total / 7 skipped / 3,452 passed / 0 failed**, 216.3 s (`pa-step2-final-full-native.log`).
- Shipping build: **SUCCESS**, RAM1 still variables 96,192 / code 392,120 + padding 1,096 / locals 34,880
  (`pa-step2-final-build.log`).
