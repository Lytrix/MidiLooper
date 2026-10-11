# I2 — timing-table and measurement correction packet

## Authorized scope

Owner requested correction of the reviewed findings and recorded table on2026-10-11, branch `feature/unified-rolling-playback-path`, HEAD37a63011 plus preserved dirty tree. Existing metric owner `hitl.measure.envelope_timings`; documentary owners DECLARED-LIMITS, DEC-117 and holder budget. No numerical threshold, firmware, stimulus, device, upload, SD or acceptance-checkbox change. The already-flashed counting-only6250us bar remains untouched.

## Frozen oracle before host edit

`measure_deferred_owner_service_advance` must score `eligiblePredecessorMax`, not raw `maxDeltaTicks`. A fixture containing an idle158-tick row and an eligible6-tick row returns6, with158 retained only diagnostically. A true eligible120-tick overrun remains120 and preserves its overrun count. A capture with no eligible observations is unproven, not measured zero. Existing fixture construction will name the eligible field explicitly rather than deriving it from the implementation's result.

No new cycles, physical-arrival timestamps, physical output identities or consecutive-wait observations are fabricated. Existing signed target rows remain declared but unmeasured/partial where their inputs do not establish the metric. Component distributions, old threshold counts, busiest logical-group jitter/throughput and partial burst counts remain diagnostic, not physical/cycle qualification.

## Documentary completion oracle

Both current decision tables must preserve the recorded limits and agree on instrument/status. Combined p99 uses the owed direct cycle histogram and has no7/4/3 proxy score. Input-only exception outcomes distinguish plain PASS from analytical qualification with explicitly accepted input exceptions; unmeasured required data and non-waivable fail-class events cannot become either. Historical cohort rates identify capture/image/window/metric/bar.158-tick idle-gap failure is withdrawn in favor of eligible6/zerooverruns, without full qualification. Approximate scheduling conversion uses60000000us/min. Budget records active-set approval separately from incomplete pricing/unproved timing. Advisory quotations are not relabelled as user signoff.

## Checks

Focused `PYTHONPATH=scripts .venv/bin/python -m unittest scripts.test_envelope_timings`; related host measurement/qualification tests located before broader batch. Replay streamed win_gap rows from `captures/envdist-t8s4-01/base_20261010_213036_serial.log` through the production reader. Validate corrected document links and table parity, strict active-spec validation, diff hygiene and byte-identical unrelated-code diffs. No firmware build/native/HITL required for this host/doc correction; no push/merge/PR.

## Completion

Completed locally; instrumentation and qualifying hardware remain owed, and S7.5a — continuity envelope stays unchecked.

- Baseline regression:6 service-advance cases,4 behavioral failures before reader edit (158/90raw read as eligible and idle-only data labelled measured).
- Final `PYTHONPATH=scripts .venv/bin/python -m unittest scripts.test_envelope_timings scripts.test_windowed_content_parity_verify`:106tests, OK,2.617s. The measurement suite alone has56tests. Genuine eligible120tick overrun remains120; denied-block totals explicitly do not claim consecutive fairness.
- Production-reader replay of streamed envdist `win_gap` rows:32rows,6eligible ticks,158raw diagnostic, permitted64,0overruns/0overrunMax,157eligible turns/23progress. No full qualifier PASS follows.
- Historical row checks:15component histogram windows each;14meet the100sample diagnostic floor. Floor-valid gap worst p99 bucket[24576,24831]us; below-floor first gap[26112,26367]us/2samples. Floor-valid input p99upper255us; below-floor first input[512,767]us/3samples. These remain component diagnostics, not combined-cycle results.
- DEC-117 now cites the canonical detailed table rather than duplicating the invalid formula; corrected scope, status, actual-window provenance and distinct analytical outcomes. Runtime/active-task/sibling/plan pointers updated, no checkbox.
- Strict active-spec validation and diff/link/preservation checks are recorded at the final closeout checkpoint. No new firmware native/build/HITL run: source/bar unchanged and no push/merge/PR requested.
- Remaining blockers are measurements/qualification, not missing numerical decisions. A/B remains separately authorized attribution work, not automatic exception adjudication or instrumentation replacement.
- Final closeout: strict runtime-working-set-ownership and windowed-playback-generation validation passed; `git diff --check` clean. Current table links/whitespace/status checked, S7.5a remains unchecked. Firmware constant/source/native telemetry-test hashes and every unrelated tracked source/include/scripts/test diff are byte-identical to the initial snapshot. No staged changes, commit, push or device action.
