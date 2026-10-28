<!-- Implementation record (2026-10-27): all tasks complete. Two honest deviations from the listed order,
     both recorded in REVIEW.md: (a) the obligation state + its read-only test access (2.1) were added before
     the fixtures because a fixture cannot observe an obligation that has no state -- the state is inert until
     3.x/4.x, and the mutation table proves each fixture still REDs without that wiring; (b) two fixtures were
     added beyond the list: 1.5b (source-identity change at the same committed generation) because 1.5's
     committed-generation mutation exercises coalescing, not the spec's separate "source identity changed"
     clause, and 1.2b (a second obligation after a completed build) which pins the no-idle-builder admission
     requirement the later slices discovered. -->

## 1. RED fixtures (before any production code)

- [x] 1.1 Record-return race fixture (mid-build): enter `CanonicalPrefix` while `causalBaseCurrent == true`, begin the inline build, then publish the Record return so authority changes mid-build; assert the inline candidate is rejected (`prefix_restart` / terminal `Denied`), the stop retires raw, and the slot becomes `Owed`. Verify: fixture fails against current firmware (raw escape without `Owed`).
- [x] 1.2 Deferred convergence fixture: after 1.1, settle the record-return authority; assert `Owed` → fresh source resolved → `stepCanonicalBase` builds → full companion published/adopted → obligation clears; content equals the healthy overdub-3 result. Verify: fails against current firmware (raw-only, no rebuild).
- [x] 1.3 Coalescing fixture: while owed/building for G, commit another raw overdub (G+1); assert no second obligation, target becomes G+1, stale candidate cannot publish, final result represents G+1 exactly once. Verify: fails against current firmware.
- [x] 1.4 FIFO liveness fixture: hold the record-return source unstable and drive >8 short stop interactions; assert every stop raw-commits and retires without filling the 8-entry FIFO, and the slot owns exactly one coalesced obligation. Verify: fails against current firmware.
- [x] 1.5 Mid-build committed-state mutation fixture: change the committed generation by a non-stop path (undo/redo or an edit) while `Building`; assert candidate discarded and re-resolved, no mixed-source publication. Verify: fails against current firmware.
- [x] 1.6 Convergence-failure non-blocking fixture: force convergence to return a terminal `Denied`; assert committed raw content stays authoritative/playable, FIFO stays drainable, obligation returns to `Owed` (not stuck in `Building`). Verify: fails against current firmware.
- [x] 1.7 Standing-demand wakeup fixture: `Owed` with source already available after a later owner turn (no caught transition); assert the slot-level owner observes the ready authority and begins convergence without another user interaction. Verify: fails against current firmware.
- [x] 1.8 Canonical-builder ownership fixture: create `Owed`, let deferred convergence enter `Building`, then admit a healthy inline stop; assert never two canonical-builder owners, the inline stop does not wait, the deferred candidate is discarded/yielded, the obligation returns to `Owed` or coalesces, and the inline canonical result completes. Verify: fails against current firmware.
- [x] 1.9 Inline-satisfaction fixture: after 1.8, let the healthy inline build adopt an authoritative source covering the target; assert the obligation becomes `Idle` and no duplicate later publication occurs. Verify: fails against current firmware.
- [x] 1.10 Slot-cancellation fixture: `Owed`/`Building` for loop A, then clear/load loop B; assert the obligation becomes `Idle` and candidate A cannot publish into B. Verify: fails against current firmware.

## 2. Obligation state and storage

- [x] 2.0 **Gate — self-invalidation audit:** enumerate every `++playbackRevision` site and prove the deferred convergence publication/adoption cannot advance its own target; record the finding (and re-scope the target identity if the companion-history commit is found to be part of the derived adoption). This MUST be green before 2.1 encodes the obligation field. Verify: the audit note is written and `committedPassRevision()` (or the re-scoped identity) is justified.
- [x] 2.1 Add the deferred-convergence obligation state to `LoopPlaybackRuntime` (state enum, `targetCommittedGeneration`, `buildSourceIdentity`, coverage) plus the test access to drive/observe it. Verify: `pio test -e native -f test_shared_capture_finalization` compiles with the new accessors.
- [x] 2.2 Measure the compiler `sizeof(LoopPlaybackRuntime)` on target and host, re-base the I5 pins (7768/9896), and record the measured delta in the change's review artifact. Verify: `pio run -e teensy41-capture-serial` links with the re-based `static_assert` and the RAM1 budget unchanged.

## 3. Inline gate and raw-retire → Owed handoff

- [x] 3.1 Add the `causalBaseCurrent` gate at the top of the `CanonicalPrefix` decision; on true, keep the existing inline path unchanged. Verify: healthy full-companion native fixtures (e.g. `test_destination_playing_base_builds_natively_under_live_successor`) still pass.
- [x] 3.2 Before bind: on `causalBaseCurrent == false`, never bind the stale source — raw commit via the existing `causal_base_superseded` route, mark the slot `Owed(committedPassRevision())`, retire the stop job in bounded turns. Verify: a before-bind race fixture passes.
- [x] 3.3 During build: make the DEC-171 terminal-denial escape (and any `stepCanonicalBase` terminal `Denied` caused by causal-authority change) also mark the slot `Owed(committedPassRevision())`, not merely commit raw. Verify: fixture 1.1 (mid-build race) passes.

## 4. Deferred owner turn

- [x] 4.1 Add a slot-level deferred-owner turn (routed in `TrackDeferredMaintenance`, never inside `processDeferredOverdubStopGrain`). Verify: fixture 1.4 passes (FIFO drains while owed).
- [x] 4.2 Build admission: resolve a fresh authoritative source when `publishedSnapshotIsCurrent() && committedPassRevision() >= targetCommittedGeneration`; bind `buildSourceIdentity` and enter `Building`. Verify: fixture 1.2 passes.
- [x] 4.3 Coalescing (universal) and identity re-resolution: any `committedPassRevision() > targetCommittedGeneration` while owed/building sets target to newest and discards the candidate; committed-generation/snapshot change mid-build discards and re-resolves. Verify: fixtures 1.3 and 1.5 pass.
- [x] 4.4 Completion and adoption: `Building` → `Ready` → atomic adoption/publication → `Idle`, strictly identity-qualified, scoped to the derived canonical window so adoption never advances `committedPassRevision()` (per audit 1.8). Verify: fixture 1.2's publication/adoption assertions pass.
- [x] 4.5 Standing-demand service: `Owed` itself makes the slot eligible; a later owner turn observes an already-ready authority and begins the build (publication may nudge but correctness must not depend on catching it). Verify: fixture 1.7 passes.
- [x] 4.6 Non-blocking failure: a terminal `Denied` returns the obligation to `Owed`, ordinary `Pending` keeps `Building`, and neither occupies the capture-stop FIFO; raw content stays authoritative. Verify: fixture 1.6 passes.
- [x] 4.7 Exclusive ownership arbitration: an inline stop preempts a deferred `Building` candidate (discard + `Owed`/coalesce), and a deferred owner turn does nothing while an inline build is active. Verify: fixture 1.8 passes.
- [x] 4.8 Inline satisfaction: a successfully adopted inline canonical result whose source covers `targetCommittedGeneration` clears the obligation (no duplicate publication). Verify: fixture 1.9 passes.
- [x] 4.9 Slot cancellation: fold the obligation discard into `LoopPlaybackRuntime::reset()` so clear/load/replacement/reset cancels the obligation and invalidates its candidate. Verify: fixture 1.10 passes.

## 5. Full verification

- [x] 5.1 Mutation red-proof each production change (disable the gate / the deferred owner and confirm the corresponding fixture fails). Verify: each mutation run shows the expected RED.
- [x] 5.2 Run `pio test -e native` (full suite) and confirm all tests pass. Verify: 0 failures.
- [x] 5.3 Build `teensy41-capture-serial`, confirm the RAM1 budget is unchanged and the re-based I5 pin holds. Verify: budget variables 96,192, padding >= 840.
- [x] 5.4 Update `docs/DECISION_LOG.md` (ownership-boundary decision) and `docs/Runtime/*` state at closeout. Verify: docs match the new behavior.

## 6. Device follow-up (OPEN — found by the 2026-10-27 capture)

- [x] 6.1a (done 2026-10-27) A **window-only** denial must not abandon the base: the window is the successor bridge's base, the prefix is ready, and the tail/companion path is still derivable. Split the terminal-denial escape by latch, keep the companion path, and commit pass + companions through `advanceAtomicTransferWithoutBridge`. Fixture 1.12; mutation M9 reds it.
- [x] 6.1c (done 2026-10-28) The owner-supersession commit route (the dominant display-loss cause on the
      correction-2 capture): when the companion horizon is `Ready` bound to this interval's receipt but a
      record-return commit moved `playbackRevision`, publish **pass + companions losslessly** through a
      store-unchanged proof instead of discarding (`supersessionReady()` tolerance + `commitPreparedCaptureAndCompanionsSupersession`,
      `PendingStopAdmission::supersededCompanionExchange`). Fixture 1.13; mutation M10 reds it; flashed
      2026-10-28 (boot `crash=0`/`BOOT,load,ok`); **owner-confirmed on device** — the overdubbed notes are visible.
- [ ] 6.1b Rebuild the **companion / note-edit representation** for a *prefix-denied* raw retire (the remaining case), not only the canonical bridge-base window. The display resolves overdubbed notes from the companion (`preparedCompanionHistory_` → `editPasses`), so a window rebuild is invisible to the owner. Verify: a device or native fixture shows a raw-retired overdub's overlapping note resolved into a companion edit pass after convergence.
- [ ] 6.2 (needed only for 6.1b) Re-scope the obligation's target identity per the task 2.0 fallback: the companion-history commit advances `committedPassRevision()` (`publishPreparedCompanionHistory`), so the target must be the **raw-pass commit revision** (or an equivalent pass-inventory identity) and the obligation's own companion publication must not re-arm it. Verify: a fixture converges once, and the published companion does not create a second obligation.
- [ ] 6.3 Re-capture on device after 6.1/6.2 with the same rapid-overdub gesture; verify every stop either derives its companion inline or converges afterwards, and that no later stop takes the "canonical base unavailable" shortcut.
      **Partial (2026-10-28):** `captures/session_20261028_112311.log` on `551cac47…` shows the early stops publishing
      their companions (`edit` 0 -> 7, display notes 246 -> 433) and the **owner confirmed the display shows the
      overdubbed notes**. Still uncovered in that capture: one `no canonical base` stop and three `retargeted` stops
      at a transport stop (both raw-only), so this task stays open for 6.1b/6.2.
- [x] 6.0 (done 2026-10-27) The three implementation defects the capture exposed: Track-level abandonment (now per job, fixture 1.11), the supersession gate's wrong premise (removed; obligation recorded after the raw commit), and `reset()` as a false slot-incarnation boundary (obligation survives a playback reset, cancelled only by a restarted timeline, fixture 1.10).
