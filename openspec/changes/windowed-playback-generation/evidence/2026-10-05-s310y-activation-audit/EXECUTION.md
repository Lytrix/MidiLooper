# Task 3.10 — activation audit: which production demand carries a qualified stamp

Execution packet, written **before** the fixtures. Authority:
[IMPLEMENTATION_READINESS.md](../../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice),
[`docs/Templates/EXECUTION_PACKET.md`](../../../../docs/Templates/EXECUTION_PACKET.md). Verification-only slice: **no
production behaviour change**, no gate flip.

## Identity and scope

- **Slice:** windowed-playback-generation task 3.10, the remainder its own records name first -- *"activation therefore
  needs the clock-owner wiring, not a better constant"* (increment 25) and *"the calling owners (the MIDI edge, the switch
  preparation and the preview lane) do not pass stamps yet"* (increment 25's measurement). Contract: **DEC-108** §13c
  (unqualified demand fails closed) and §13b (the enforced derived admission).
- **Question the slice answers with evidence:** *which production demand carries a qualified clock stamp today, what
  happens to one that does not, and which production paths are deliberately exempt?* The answer decides whether
  "activation" is a wiring task still owed or a gate that now waits only on the owner's enablement decision.
- **Owner and production entry → deferred worker (unchanged by this slice):** `src/main.cpp`'s
  `runDeferredIdleMaintenance(PlaybackViewMaintenance)` takes one `PlaybackDeadlineClock` from
  `clockManager.getPlaybackDeadlineClock()`, passes it to every track's `serviceRollingPlaybackWindow` and
  `serviceAdmittedDeadlineMisses`, and into the chosen track's `stepPlaybackViewMaintenance`. The launch path
  (`TrackManager::servicePendingSlotProvision` → `Track::preparePlaybackLaunchForSlot`) and the capture edge
  (`Track::captureIntervalAdmissionForSlot`) are the two other production paths that reach window state.
- **Primary files:** `test/test_runtime_working_set_ownership/test_runtime_working_set_ownership.cpp` (two fixtures and
  their `RUN_TEST` registrations). No production line is touched.
- **Recorded code inventory (grep, not a fixture):** the only production *window request* is
  `Track::serviceRollingPlaybackWindow`'s call to `requestCommittedPlaybackWindow(slot, coverage, clock)`
  (`src/Track/TrackPlaybackWindowBuild.cpp`), reached from `main.cpp`'s stamped lane turn;
  `Track::requestPlaybackWindow` (the caller-owned-scratch entry) has **no production caller** -- fixtures only;
  `Track::requestPreviewWindow` likewise (its production callers belong to 3.13). A fixture cannot assert an inventory, so
  it is recorded here with its command and its result in `EVIDENCE.md`, and the *behaviour* it implies is what the two
  fixtures below pin.
- **Explicit non-goals:** flipping `sInterferenceBudgetEnforced` (the owner's enablement decision, and the device leg
  belongs to 6.5); wiring the preview/undo requesters (3.13/3.14 own them); changing any constant, capacity or gate.
- **Commit boundary:** test-only; independently buildable.

## Oracle fixed before behavior edits

| Case | Initial state | Ordered actions | Expected observation |
|---|---|---|---|
| **AA-1** the demand that exists is positioned | a playing, demoted (24-bar) target whose launch is bound and served — the rolling slice's own fixture setup | (a) `serviceRollingPlaybackWindow({origin, 0})`; (b) `serviceRollingPlaybackWindow({origin, 1})` | (a) `Excluded`, the rolling frame still `Idle` and **no** pending demand (an unqualified clock promises nothing); (b) `Requested`, the retained demand carries `clockEpoch == 1` and a `deadlineTick > origin`, and the frame's epoch/deadline equal the demand's |
| **AA-2** the exempt demand is unpositioned and never judged | a demoted target whose launch is *requested* but not yet served | (a) read the launch demand; (b) `serviceAdmittedDeadlineMisses({1e9 + n, 1})` eight times; (c) turn the interference budget on and drain the launch to readiness and bind | (a) `clockEpoch == 0`, `deadlineTick == 0` — unpositioned **by design**; (b) **no** `one_pass_late`, `lag_breach`, `promise_epoch_invalidated` or `pending_position_invalidated` row: an unpositioned demand is not a late one; (c) it still reaches readiness and binds, so enforcement exempts a launch rather than refusing it |

- **Counterexamples that must fail these fixtures:** removing the `!clock.epoch` guard in
  `serviceRollingPlaybackWindow` (AA-1 would see `Requested` and a zero-epoch demand); dropping the unpositioned guard in
  `serviceAdmittedDeadlineMisses` (AA-2 would name `promise_epoch_invalidated`); giving the launch demand a position in
  `preparePlaybackLaunchForSlot` (AA-2's unpositioned assertions).
- **Preserved invariants:** nothing is published or re-positioned by the audit; the enforcement flag is restored by the
  fixture; the deadline/epoch of an admitted promise is never rewritten.

## Controls and coverage

- **Reused seams:** the suite's `assert_s4b_7_a_demoted_target_through_the_turn`, `setupImmutableLaunch` /
  `drainImmutableLaunch`, `TrackCaptureStopTestAccess::runtimeIfAllocated`, `PlaybackPathRows::reset`/`countOf`, and the
  `sInterferenceBudgetEnforced` runtime flag (restored after use, as the existing fixtures do).
- **Production paths exercised:** the lane turn's demand (`serviceRollingPlaybackWindow`), the promise-deadline service
  (`serviceAdmittedDeadlineMisses`), the launch retry admission (`stepCommittedPlaybackRetry` via
  `stepPlaybackViewMaintenance`), and the switch provisioning turn.
- **Untestable-before-link:** `src/main.cpp` is not native-linkable, so the *caller site's* stamp (that the lane reads
  `ClockManager`) stays build-verified; the fixtures pin the receiving end's requirements instead.
- **Bounded drain:** the launch drain is the existing bounded helper (5,000 turns); the deadline service is called a fixed
  eight times.
- **Numeric limits:** none invented -- the clock values are the fixtures' own (origin, `origin + 1` epoch, and a tick far
  beyond any deadline), and no unit or constant is declared by this slice.

## Verification recipe

- Focused: `pio test -e native -f test_runtime_working_set_ownership`.
- Broad: `pio test -e native`; build: `pio run -e teensy41-capture-serial` (RAM1 line must be unchanged).
- Also `git diff --check` and `openspec validate windowed-playback-generation --strict`.
- Device: **NOT RUN** (nothing is enabled; the device leg remains 6.5's).
- Evidence path: `evidence/2026-10-05-s310y-activation-audit/`.

## Completion record

- [ ] Fixtures implemented; oracle above honoured (recorded in `EVIDENCE.md`)
- [ ] Mutations red-prove each fixture, restored byte-identically
- [ ] Native verification (focused + full)
- [ ] Build/RAM1 verification
- [x] Fixtures implemented; oracle above honoured (recorded in `EVIDENCE.md`)
- [x] Mutations red-prove each fixture (MA1, MA2, MA3), restored byte-identically
- [x] Native verification: focused **137/137**; full **2791 / 2784 succeeded / 7 skipped / 0 failed**
- [x] Build/RAM1 verification: `teensy41-capture-serial` **SUCCESS**, `variables 96,192` / `padding 1,640` (no re-base)
- [x] Device verification: NOT RUN (not claimed)
- [x] Docs: runtime state + this change's task records
- [x] Task checkoff: none on its own (the audit resolves the *wiring* question; 3.10's qualification and 6.5's device leg
      stay open)
- **Recorded anomaly** (not a pass, not a finding): three earlier focused runs of this slice failed at the pre-existing
  S4b.7 helper's commit assertion and the same byte-identical source then passed 20+ runs, a clean rebuild and the full
  suite; the cause stays open and unproven. See `EVIDENCE.md` §"One observed anomaly".
