# Tasks — runtime profiling layer (Level 1)

Commit boundary for every group: the working branch (`feature/unified-rolling-playback-path`,
baseline HEAD `5ddba83e`, tree clean at rebase on 2026-10-31), never `dev`, one reviewable commit per
group, staged by hunk. Re-check `git status` before each commit and leave any unrelated modification
out. No PR unless requested.

## 0. Prerequisites — create these first (no firmware behavior change)

Each item is a hard gate for slice 1 (proposal § Prerequisites, P1–P8). Items 0.1–0.4 are
documentation/evidence only; 0.5–0.6 are build plumbing whose only effect is a new env.

- [x] 0.1 **P1 — scope authorization.** Add a `## Now implementing` entry to
      `docs/Runtime/CURRENT_WORK.md` naming this change, its thirteen sites, its non-goals and that
      `HotPathTelemetry` is untouched; verify it sits at the top of that section and no other active
      declaration is overwritten. Owner-visible gate: firmware work waits for it.
- [x] 0.2 **P2 — decision review and DEC.** Fill `docs/Templates/DECISION_REVIEW.md` into
      `evidence/DECISION_REVIEW.md` after searching `docs/DECISION_LOG.md`, `openspec/specs/` and
      `docs/Plans/` for: profiler, DWT, cycle counter, RAII, telemetry, instrumentation, span timing,
      `view_prewarm_step`, `capture_prewarm_split`. Record the reused owners
      (`ClockPhysicalReferencePlatform` boot contract, `IngressCost` validity shape, `StopDiagRows.cpp`
      carve-out) and why `HotPathTelemetry` is not migrated. Then append one `DEC-###` recording: the
      short/long validity contract and source-selection rule, the report-only ceiling classes, the
      depth-zero command gate and batch/horizon rules, and the dedicated env; verify it cites DEC-043
      point 4, DEC-214 and DEC-231 and states that no `ExecutionBounds.md` row is added.
- [x] 0.3 **P3 — rebased preflight and baseline.** Fill `docs/Templates/PREFLIGHT.md` into
      `evidence/PREFLIGHT.md`: branch, HEAD `5ddba83e`, tree state, required gates, exact build/test
      commands for slices 1–4. **Before any edit**, build `teensy41-capture-serial` and
      `teensy41-capture-serial-no-grain` and record their `check_ram1_budget.py` lines (variables,
      locals free, ITCM code, padding), the default image's HEX SHA-256, and the full
      `pio test -e native` case/skip counts. These figures are the "default image unchanged" oracle
      and the slice-4 A/B image.
- [x] 0.4 **P6 — boot hook.** Read `setup()` in `src/main.cpp` and record in `evidence/PREFLIGHT.md`
      the exact position where `RuntimeProfiler::initAtBoot()` will be called: after `Serial`/capture
      init (the boot row needs them), before the first call that can reach an instrumented owner
      (boot load, boot prewarm, the first display update). Verify by listing those calls in order.
- [x] 0.5 **P4 — profile env and RAM1 baseline.** Add `[env:teensy41-capture-serial-profile]` to
      `platformio.ini` extending `teensy41-capture-serial` with `-D RUNTIME_PROFILE_SCOPES=1`, and add
      `"teensy41-capture-serial-profile": "teensy41-capture-serial"` to `scripts/ram1_budget.json`
      (the checker fails an env without a baseline). Verify `pio run -e teensy41-capture-serial-profile`
      builds (the flag has no consumer yet) and the default env figures equal 0.3.
- [x] 0.6 **P5 — linker carve-out.** Add `*ProfileDiagRows.cpp.o(.rodata.str* .rodata.*.str*)` beside
      the `StopDiagRows`/`EnvelopeDistRows` lines in `linker/imxrt1062_t41_lcr.ld`, with a comment
      naming this change; verify both envs still link at the 0.3 figures (a rule matching no object
      is inert).
- [x] 0.7 **P7 — HITL stimulus and recipe.** Read `docs/Agents/hitl_authoring_traps.md`, run
      `.venv/bin/python scripts/host_midi_hitl.py stimulus list`, and fill `HITL_TEST_RECIPE.md` into
      `evidence/HITL_TEST_RECIPE.md` for the slice-2/3/4 device legs. The chain rig
      (`scripts/hitl_midifile_chain.py`, used for DEC-230/231) is not a registered stimulus: register a
      temporary variant `STIM-PROFILE-CHAIN-TEMP-01` (record + idle preparation + playback, then
      `!PROFILE_STATS`/`!PROFILE_RING` drains), or record the owner's acceptance of the rig as a reuse.
      Verify the recipe names tier registration, RAM1 headroom and reused `scripts/hitl/actions/`.
- [x] 0.8 **P8 — same-day reference capture.** On the 0.3 default image, run the 0.7 stimulus once
      and record in `evidence/<date>-baseline-device.md`: the Record edge (`capture_prewarm_split`
      `views`/`total`), every `view_prewarm_step` unit (`unit`, `us`), `midi_gap`/`late_on` maxima and
      `win_gap_overrun` count, plus a second run for run-to-run spread. Verify boot proof
      (`boot_reset,crash=0`, `BOOT,load,ok`) and the HEX hash. These numbers replace the DEC-230
      image `79aa5b7f…` figures as the slice-3 oracle. *(Done 2026-10-31:
      `evidence/2026-10-31-baseline-device.md`, image `909c930d…`, two valid runs.)*

## 1. Contract review (no firmware edit)

- [x] 1.1 Review the D12 site table against the code at `5ddba83e`: for the **short** scope
      (`PlaybackViewService_grain`) confirm the row-27 grain is the scope's whole body after the gate;
      for every **long** scope confirm the `< 71.6 min` reason; for every site record which existing
      session-capture rows fall inside the scope (D11) and confirm the scope placement after the
      no-work gate. Also record, per site, the legacy timers already inside it (`HotPathTelemetry`,
      `Diagnostics::Timing`, session-capture rows), and trace the HITL serial dispatch call chain to
      prove it runs outside all thirteen scopes (RED_TEAM R8). Record in `evidence/DECISION_REVIEW.md`;
      verify no site's only justification is its ceiling or window span, and that `Display_frame_phase`
      and `SnapshotPublish_step` remain long.
- [x] 1.2 Review [RED_TEAM.md](RED_TEAM.md) and [RETIREMENT.md](RETIREMENT.md) with the owner; verify
      every "Applied" finding is reflected in design/spec/tasks and TBD-4 (default-env promotion) has
      an owner decision or is explicitly left open. *(2026-10-31: findings applied in `fec18994`; TBD-4 left open by
      the owner until the slice-4 A/B; runtime arming moved to a follow-up by owner decision.)*

## 2. Slice 1 — runtime core, short/long primitives, aggregate and ring

- [x] 2.1 Write `evidence/EXECUTION_PACKET-slice1.md` from the design's slice-1 contract; verify every
      template field is filled or marked not-applicable with a reason.
- [x] 2.2 Add `include/Utils/RuntimeProfiler.h`: `ProfileId` enum (with a reserved calibration id),
      capacity and layout `static_assert`s (ids ≤ 64, depth 16, ring 256, `sizeof(ProfileStat) == 48`,
      ring record 24), the five report-only classes, `kCycleTrustUs`, the scope API (`setResult`,
      `setContext`, reference form) and no-op macros when `RUNTIME_PROFILE_SCOPES` is undefined; verify
      the native build compiles and a `static_assert` fails on purpose when the id count exceeds
      capacity (mutation check, then restore).
- [x] 2.3 Write `test/test_runtime_profiler` fixtures **before** the record path: inclusive/self split,
      LIFO order fault + poison + reset at depth 0, depth overflow without double counting,
      over-ceiling ring entry, ring overwrite with `dropped` and a `seq` gap, zero-duration call,
      unavailable source, entry under a faked non-zero `IPSR` (refused, `isrRefused` counted, stack
      untouched), optional result/context present and absent, the `kCycleTrustUs` boundary
      (below / at / above), a coarse duration larger than 32-bit cycles, and boot clear over a
      pre-filled pattern; verify the suite is RED on the discriminating assertions before 2.4. *(Discrimination shown by mutation —
      see `evidence/EXECUTION_PACKET-slice1.md`.)*
- [x] 2.4 Implement the record path in `src/Utils/RuntimeProfiler.cpp` (DMAMEM storage, clock shims
      `profileCyclesNow()` / `profileMicrosNow()`, refusal/order/poison/overflow guards, no I/O, no
      allocation); verify 2.3 turns green with `pio test -e native -f test_runtime_profiler`.
- [x] 2.5 Implement the short and long primitives with the D2 selection rule and the shared u64
      domain; verify the boundary and multi-period fixtures, and that a normal operation keeps cycle
      resolution.
- [x] 2.6 Implement `initAtBoot()` (clear, read `cpu_hz`, calibrate both primitives over 128–256
      samples into the calibration id, clear it, emit the boot row once) and call it at the 0.4
      position; verify native fixtures for sample count, min/average arithmetic, both variants, the
      `source=0` case, and that the calibration id leaves no aggregate residue.
- [x] 2.7 Build both envs: profile env at RAM1 `96,192` with padding ≥ 840; default and no-grain figures
      equal 0.3; verify `git diff --stat` touches only intended files and `HotPathTelemetry` has no diff.

## 3. Slice 2 — bounded commands, Tier-A registration, host decoder

- [x] 3.1 Write `evidence/EXECUTION_PACKET-slice2.md`; verify every field is filled or justified.
- [x] 3.2 Add `src/Utils/ProfileDiagRows.cpp` (names, row formats, command strings) and its accessor
      header; verify the profile build keeps RAM1 `96,192` and a native test proves every declared id
      returns a non-empty, unique name.
- [x] 3.3 Add the `DIAG,profile_` `PROGMEM` prefix to `isTierATextLine` under
      `#if defined(PIO_UNIT_TEST_NATIVE) || RUNTIME_PROFILE_SCOPES` (header list unchanged) and pin it
      in `test_capture_ring` and `test_capture_line_tier`; verify `check_marker_contract.py` reports
      EMITTED / TIER-A / FIT for **each** of `DIAG,profile_boot,`, `DIAG,profile_stat,`,
      `DIAG,profile_ring,`, `DIAG,profile_reset,` and `DIAG,profile_cmd,` (RED_TEAM R10), `--audit`
      exits 0, and the default env still equals 0.3.
- [x] 3.4 Implement `!PROFILE_STATS` and `!PROFILE_RING` (inside `#if RUNTIME_PROFILE_SCOPES`) with
      independent cursors, batch maxima 16 / 32, the pinned ring horizon and terminators carrying
      `count`, `resume`, `poisoned` and, for the ring, `dropped`/`range_start`/`range_end`/`gap`;
      verify a native fixture drains more than one batch with no row loss, a mid-dump breach is
      neither duplicated nor skipped, and an overwritten pinned range reports its gap.
- [x] 3.5 Implement the depth-zero gate for all three commands; verify a command invoked inside an
      instrumented scope emits `profile_cmd,refused,depth=<n>` and leaves table, ring, cursors and stack
      byte-identical.
- [x] 3.6 Implement `scripts/profile_report.py` and `scripts/test_profile_report.py` (complete capture,
      Tier-A eviction, `seq` gap, poisoned terminator, no profile rows); verify the suite passes with
      no MIDI and no serial port.
- [x] 3.7 Device leg (0.7 stimulus): flash the profile image, record HEX hash and boot proof, verify
      the boot row appears once with `source=1`, `cpu_hz=600000000` and both calibrations, and that
      repeated stats/ring requests drain in batches under a running transport; record
      `evidence/<date>-slice2-device.md`.

## 4. Slice 3 — the DEC-231 preparation vertical proof

- [x] 4.1 Write `evidence/EXECUTION_PACKET-slice3.md` pinning the four sites, their ceilings, the
      oracle from design § Slice 3 against the 0.8 reference, the `view_prewarm_step` agreement rule
      (profiler ≤ row, within 10 µs), and the `midi_gap`/`late_on` tolerance (within the 0.8 spread).
- [x] 4.2 Add long scopes to `Track::startRecording`, `Track::prewarmPlaybackForSlot`,
      `LoopPlaybackRuntime::prewarmViews` and the `stepViewPrewarm` call in
      `Track::stepViewPrewarmForSlot` (closed before `viewPrewarmStep`) — no control-flow, return-value
      or ownership edit; verify the diff is instrumentation-only and the owners' focused suites are
      unchanged.
- [x] 4.3 Add native assertions that the four paths increment their aggregate call counts, and that a
      prepared Record edge does not increment `PlaybackPrewarm_views`; verify with
      `pio test -e native -f test_runtime_profiler` plus the nearest owner suites.
- [x] 4.4 Run the 0.7 stimulus on the profile image and record: `ViewPrewarm_unit` ring rows against
      the same run's `view_prewarm_step us=`, the Record edge's aggregate `last_us` and absence of a
      ring record, `PlaybackPrewarm_views` count across the edge, `capture_prewarm_split views=0`, and
      `midi_gap`/`late_on`/`win_gap_overrun`; verify every oracle line or record the miss.
- [x] 4.5 Record the row-equivalence result in `evidence/<date>-retirement-equivalence.md`
      (RETIREMENT.md stage 0) for `capture_prewarm_split` (`views`/`total` only — the other reserves have
      no identity yet, R4) and `view_prewarm_step us=` (the `ready=1` event is kept, R3); verify it
      states the band, which numbers agree and any that do not. No row is removed in this change.

## 5. Slice 4 — remaining nine bodies, coverage and overhead proof

- [x] 5.1 Write `evidence/EXECUTION_PACKET-slice4.md` pinning the nine bodies, variants and ceilings
      from D12, the domain list and the A/B tolerance.
- [x] 5.2 Add one scope each to `PlaybackWindow_build`, `PlaybackViewService_grain`,
      `SnapshotPublish_step`, `SaveWorkItem_step`, `IdleOwner_unit`, `DisplayBase_gather`,
      `Display_frame_phase`, `LoadStep` and `OverdubStart_body`, each after its no-work gate; verify the
      diff is instrumentation-only and the short site's guarantee still holds.
- [x] 5.3 Add native aggregate-count assertions for those nine paths; verify the full native suite
      (`pio test -e native`) has no new failure against the 0.3 counts.
- [ ] 5.4 Coverage gate on device: one run exercising record, overdub, playback, a save and a slot
      load, then `!PROFILE_STATS` until `resume=0`; verify one identity per declared domain has a
      non-zero count, unexercised domains are named, and `ViewPrewarm_unit` reports `IdleOwner_unit`
      as parent when stepped from the idle owner.
- [ ] 5.5 Overhead A/B: profile image (thirteen scopes) vs the 0.3 default image, same stimulus;
      record both calibrations, `midi_gap`/`late_on` maxima and `win_gap_overrun`, and each site's
      legacy timers (R7); verify no new overrun and maxima within the 0.8 spread, or remove the
      offending site and record the decision. *(BLOCKED 2026-11-01: the open stop-edge wedge reproduces on the
      pre-change default image `909c930d…`, so no valid chain exists on either side; `IdleOwner_unit` was
      removed by decision after it measured as the poll.)* Append the same runs' `DIAG,timing`/`timing_max`,
      `idle_turn` and `persist_save,step` values beside their identities to
      `evidence/<date>-retirement-equivalence.md`, and record the TBD-4 promotion recommendation.
- [ ] 5.6 Record ring capacity use, `seq` gaps and batch counts from two real captures and close
      TBD-3; verify the decision is written before closeout.

## 6. Closeout

- [ ] 6.1 Add `docs/Guides/RUNTIME_PROFILING.md` (env, short vs long and when each is legal, selection
      rule, interrupt-time note, commands and batching/reset rules, class table, what is report-only)
      and a one-line pointer in `AGENTS.md`; verify it cites the spec and the Level-2 and
      `HotPathTelemetry` non-goals.
- [ ] 6.2 Add the "profiling ceilings are not execution bounds" note to
      `docs/Authority/Architecture/ExecutionBounds.md` without changing a row; verify the row count is
      unchanged and the note cites the 0.2 `DEC-###`.
- [ ] 6.3 Update `docs/Runtime/PROJECT_STATE.md`, `docs/Runtime/CURRENT_WORK.md` and
      `docs/DELIVERABLE_TRACKING.md` with the landed layer, device evidence and remaining named work
      (the `retire-duplicate-timing-rows` follow-up per RETIREMENT.md stages 1–5, including
      `Diagnostics::Timing` and `HotPathTelemetry`); verify each newest entry names this change and the
      gates reached.
- [ ] 6.4 Re-check every task's evidence and check off only those that pass; verify no device claim
      rests on a native or build-only run.
- [ ] 6.5 Archive only after slices 1–4 pass their native, build and device gates; verify
      `openspec validate runtime-profiling-layer --strict` passes and the delta spec is applied.
