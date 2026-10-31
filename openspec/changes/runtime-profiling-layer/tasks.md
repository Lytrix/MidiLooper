# Tasks — runtime profiling layer (Level 1)

Commit boundary for every group: the working branch (`feature/unified-rolling-playback-path` at
`ebaa6ebe`), never `dev`, one reviewable commit per group, staged by hunk — the tree carries
unrelated modified files and they must not be swept into these commits. No PR unless requested.

## 1. Authorization and contract gates (no firmware edit)

- [ ] 1.1 Add a `## Now implementing` scope entry to `docs/Runtime/CURRENT_WORK.md` naming this
      change, its twelve sites, its non-goals and the fact that `HotPathTelemetry` is untouched;
      verify the entry sits at the top of that section and that no other active declaration is
      overwritten.
- [ ] 1.2 Fill `docs/Templates/DECISION_REVIEW.md` into
      `openspec/changes/runtime-profiling-layer/evidence/DECISION_REVIEW.md` after searching
      `docs/DECISION_LOG.md`, `openspec/specs/` and `docs/Plans/` for: profiler, DWT, cycle counter,
      RAII, telemetry, instrumentation, span timing; verify it records the reused owners
      (`ClockPhysicalReferencePlatform` as the boot contract, `IngressCost`'s validity *shape*,
      `StopDiagRows.cpp` carve-out), and states why `HotPathTelemetry` is not migrated in this change.
- [ ] 1.3 Append one `DEC-###` entry recording: the short/long validity contract (a ceiling is never
      evidence of interval validity), the report-only ceiling classes, the depth-zero command gate and
      batch/horizon rules, and the dedicated profile environment; verify it cites DEC-043 point 4 and
      DEC-214 and states that no `ExecutionBounds.md` row is added.
- [ ] 1.4 Fill `docs/Templates/PREFLIGHT.md` into `evidence/PREFLIGHT.md` (branch, tree state, baseline
      HEAD `ebaa6ebe`, unrelated modified files, required gates); verify it names the exact build and
      test commands for slices 1–4 and the device stimulus for slice 3.
- [ ] 1.5 Review the design's D12 site table against the code: for every **short** scope, confirm a hard
      structural cap that keeps it below 7.16 s (or move it to long — as already done for
      `PlaybackWindow_build` and `DisplayBase_gather`), and for every **long** scope confirm the
      `< 71.6 min` structural reason; verify the result is recorded in `evidence/DECISION_REVIEW.md`
      and that no site's only justification is its ceiling or its window span.
- [ ] 1.6 Add `[env:teensy41-capture-serial-profile]` to `platformio.ini` extending
      `teensy41-capture-serial` with `-D RUNTIME_PROFILE_SCOPES=1`; verify `pio run -e
      teensy41-capture-serial-profile` builds and that the default env's linked figures are unchanged
      (`scripts/check_ram1_budget.py` output recorded).

## 2. Slice 1 — runtime core, short/long primitives, aggregate and ring

- [ ] 2.1 Write `evidence/EXECUTION_PACKET-slice1.md` from the design's slice-1 contract (owner, files,
      pre/post, invariants, non-goals, oracle, commands, evidence path); verify every template field is
      filled or marked not-applicable with a reason.
- [ ] 2.2 Add `include/Utils/RuntimeProfiler.h` with the `ProfileId` enum, capacity `static_assert`s
      (ids ≤ 64, depth 16, ring 256), the five report-only ceiling classes, the optional-result API
      (`setResult` + reference form), and macros that expand to a no-op statement when
      `RUNTIME_PROFILE_SCOPES` is undefined; verify the native build compiles and a `static_assert`
      fails on purpose when the id count is raised past capacity (mutation check, then restore).
- [ ] 2.3 Write `test/test_runtime_profiler` fixtures **before** the record path: inclusive/self split
      for parent+child, strictly-LIFO order fault, depth overflow, over-ceiling ring entry, ring
      overwrite with `dropped` and a `seq` gap, zero-duration call, unavailable source, refused
      measurement, optional result present and absent, short-vs-long selection, and a coarse duration
      larger than 32-bit cycles that must not truncate; verify the suite is RED for the expected
      discriminating assertions (`Expected …, Was …`) before 2.4.
- [ ] 2.4 Implement the record path in `src/Utils/RuntimeProfiler.cpp` (DMAMEM table/ring/depth stack,
      fake clock shims `profileCyclesNow()` / `profileMicrosNow()`, refusal and order-fault guards, no
      I/O, no allocation); verify 2.3 turns green and `pio test -e native -f test_runtime_profiler`
      passes.
- [ ] 2.5 Implement the **short** primitive (DWT only) and the **long** primitive (DWT + `micros()`
      bracket, coarse-sourced when the cycle delta spans one period, normalized into the shared u64
      duration domain); verify a native fixture proves a forced multi-period operation reports the
      wall-clock duration and marks it coarse, that the cycle 32-bit field is never presented as the
      duration, and that a normal operation keeps cycle resolution.
- [ ] 2.6 Implement the boot calibration: 128–256 empty short scopes with no serial inside the loop,
      reporting sample count, minimum and average cycles (and microseconds at the reported rate);
      verify a native fixture covers the sample count, the min/average arithmetic and the
      source-unavailable case (all scopes refused, boot row still emitted once with `source=0`).
- [ ] 2.7 Build both envs and record RAM1/ITCM: profile env at `ram1_variables 96,192` and padding at or
      above the 840 floor; default env figures identical to `scripts/ram1_budget.json`; verify
      `git diff --stat` touches only the intended files and that `HotPathTelemetry` has no diff.

## 3. Slice 2 — bounded commands, Tier-A registration, host decoder

- [ ] 3.1 Write `evidence/EXECUTION_PACKET-slice2.md` (owner, command table, row contract, batch sizes,
      cursor semantics, oracle, commands, evidence path); verify every field is filled or justified.
- [ ] 3.2 Add `src/Utils/ProfileDiagRows.cpp` (names + row formats) plus the one linker carve-out line
      and the name accessor header; verify the target build keeps `ram1_variables 96,192` and a native
      test proves every declared id returns a non-empty, unique name.
- [ ] 3.3 Add the `DIAG,profile_` classifier prefix to `isTierATextLine` in
      `DebugSessionCapture.cpp` **only** (the header list stays unchanged) and pin the resulting
      classification in `test_capture_ring` and `test_capture_line_tier`; verify
      `python3 scripts/check_marker_contract.py 'DIAG,profile_stat,'` reports EMITTED / TIER-A / FIT and
      `--audit` exits 0.
- [ ] 3.4 Implement `!PROFILE_STATS` and `!PROFILE_RING` with independent persisted cursors, batch
      maxima of 16 (stats) and 32 (ring), a ring sequence horizon pinned at the first batch
      (`range_start`/`range_end`, new records excluded), and a terminator carrying `count`, `dropped`,
      `resume` and the reported gap; verify a native fixture drains a table larger than one batch in
      the expected number of requests with no row loss, and that a breach arriving mid-dump is neither
      duplicated nor skipped and an overwritten pinned range reports its `seq` gap.
- [ ] 3.5 Implement the **depth-zero gate** for all three commands: a command received while any scope
      is active emits `profile_cmd,refused,depth=<n>` and changes no table, ring or cursor state, and
      the host retries later; verify with a native fixture that a command invoked inside an
      instrumented scope records nothing for its own work and leaves the depth stack untouched.
- [ ] 3.6 Implement `scripts/profile_report.py` (aggregate table, breach timeline, **immediate
      enclosing profiled scope**, `seq` gap/overwrite detection, pinned-range accounting, explicit
      unavailable/incomplete states) and `scripts/test_profile_report.py` covering a complete capture,
      a capture with Tier-A eviction, a capture with a `seq` gap and a capture with no profile rows;
      verify the suite passes and the tool runs with no MIDI and no serial port.
- [ ] 3.7 Device leg: flash the profile image, verify the boot row appears once with the calibration
      numbers (and `source=1`, `cpu_hz=600000000`), that repeated stats/ring requests drain in batches
      under a running transport, and that a command issued while a scope is active is refused with its
      depth; record the capture path in `evidence/<date>-slice2-device.md`.

## 4. Slice 3 — the DEC-230 vertical proof

- [ ] 4.1 Write `evidence/EXECUTION_PACKET-slice3.md` pinning the three sites, their long variant and
      ceilings, the oracle (first admission breach vs restamp clean), the tolerance for `midi_gap` /
      `late_on`, and the evidence path.
- [ ] 4.2 Add one long scope each to `Track::startRecording`, `Track::prewarmPlaybackForSlot` and
      `LoopPlaybackRuntime::prewarmViews` — no control-flow, return-value or ownership edit;
      `prewarmViews` supplies its existing `bool` through `setResult`; verify the diff is
      instrumentation-only and the focused suites for those owners are unchanged.
- [ ] 4.3 Add native assertions that the three production paths increment their aggregate call count
      (durations are not asserted; ring and validity logic are covered by slice 1); verify with
      `pio test -e native -f test_runtime_profiler` plus the nearest existing owner suites.
- [ ] 4.4 Run the DEC-230 device shape on the profile image and record: the first Record admission's
      `profile_ring` rows for the three identities, the restamp's absence of all three,
      `capture_prewarm_split`'s `views`/`total` in the same run, and `midi_gap`/`late_on` in the record
      window. Verify the breach ordering matches DEC-230 within the ring's microsecond rounding and
      that no new `win_gap_overrun` appears.
- [ ] 4.5 Record the `capture_prewarm_split` equivalence result and the retirement condition (keep the
      row until the DEC-230 repair lands); verify `evidence/<date>-slice3-device.md` states which
      numbers agree and any that do not.

## 5. Slice 4 — remaining nine bodies, coverage and overhead proof

- [ ] 5.1 Write `evidence/EXECUTION_PACKET-slice4.md` pinning the nine remaining bodies, their variant
      and ceiling from D12, the domain coverage list and the pre-declared A/B tolerance.
- [ ] 5.2 Add one scope each to `PlaybackWindow_build`, `PlaybackViewService_grain`,
      `SnapshotPublish_step`, `SaveWorkItem_step`, `IdleOwner_unit`, `DisplayBase_gather`,
      `Display_frame_build`, `LoadStep` and `OverdubStart_body` — every scope after its cheap no-work
      gate, never around the poll entry; verify the diff is instrumentation-only and that each short
      site's guarantee still holds after the gate is accounted for.
- [ ] 5.3 Add native aggregate-count assertions for those nine paths and verify the full native suite
      (`pio test -e native`) reports no new failure, with the case/skip counts recorded.
- [ ] 5.4 Run the coverage gate on device: one run exercising record, overdub, playback, a save and a
      slot load, then repeated `!PROFILE_STATS` requests until `resume=0`; verify at least one identity
      per declared domain has a non-zero call count and unexercised domains are named as such.
- [ ] 5.5 Run the overhead A/B (profile image with all twelve scopes vs the pre-change default image,
      same stimulus) and record the boot calibration, `midi_gap`/`late_on` maxima and
      `win_gap_overrun` counts; verify no new overrun and maxima within the pre-declared spread, or
      remove the offending site and record the decision.
- [ ] 5.6 Record the first two real captures' ring capacity, `seq` gaps and batch counts and close
      TBD-3 (256 capacity and 16/32 batch maxima confirmed or re-based); verify the decision is written
      before closeout.

## 6. Closeout

- [ ] 6.1 Add `docs/Guides/RUNTIME_PROFILING.md` (profile env, short vs long and when each is legal,
      the three commands and their batching/reset rules, the class table, what is report-only, what is
      not) and a one-line pointer in `AGENTS.md`'s rule map; verify the guide cites the spec and states
      the Level-2 and `HotPathTelemetry`-consolidation non-goals.
- [ ] 6.2 Add the "profiling ceilings are not execution bounds" note to
      `docs/Authority/Architecture/ExecutionBounds.md` without adding or reclassifying a row; verify
      the file's row count is unchanged and the note cites the `DEC-###` from 1.3.
- [ ] 6.3 Update `docs/Runtime/PROJECT_STATE.md`, `docs/Runtime/CURRENT_WORK.md` and
      `docs/DELIVERABLE_TRACKING.md` with the landed layer, the device evidence and the remaining named
      work (including the deferred `HotPathTelemetry` consolidation); verify each file's newest entry
      names this change and the exact gates reached.
- [ ] 6.4 Re-check every task's own verification evidence in the change's `evidence/` files and check
      off only those that pass; verify no device claim rests on a native or build-only run.
- [ ] 6.5 Archive the change only after slices 1–4 pass their native, build and device gates; verify
      `openspec validate runtime-profiling-layer --strict` passes and the delta spec is applied.
