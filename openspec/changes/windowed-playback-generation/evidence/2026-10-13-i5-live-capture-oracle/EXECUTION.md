# I5 — test-only live-Capture production oracle

## Scope and contract

Status: fixture implemented; ordinary native verified; three gated expected RED prerequisites remain. Branch `feature/unified-rolling-playback-path` @ `26125ceb`, substantial pre-existing dirty work preserved. User explicitly authorized the test-only slice and CURRENT_WORK scope update.

Authority: task 3.15a — one-reader production oracle; [normative Record scenarios](../../specs/windowed-playback-generation/spec.md); [investigation handoff](../../design.md#s315-investigation-handoff), S1a — fixture-first proposal, subordinate to the spec. DEC-116 — one rolling path and DEC-108 — windowed publication remain unchanged. No new decision or behavior contract.

Owner/path: real `Track::startRecording` / accepted `noteOn` and `noteOff` → `Track::stepPlaybackViewMaintenance` → `TrackManager::updateAllTracks` → real Track MIDI reader/send and ledger. Real `Track::stopRecording`, existing deferred stop dispatch and Snapshot owner are reused. Harness: `test/test_support/track_harness.inc`; primary test file: `test/test_runtime_working_set_ownership/test_runtime_working_set_ownership.cpp`.

Mutation: tests/docs only. No firmware, new scheduler, fake emitter, test-written published view, numeric service change, upload, device/SD action or Git delivery. All existing compatibility registrations remain. Parent 3.15a–f — all-route migration, 3.10 — bounded service and 3.7b — emitter retirement remain unchecked.

## Frozen transition oracle

- Existing private-build cases remain independent GREEN controls: epoch-0 grants complete a private silent Capture candidate without changing raw current; accepted input cancels that candidate; real stop retains job/range and raw shell.
- `test_record_capture_initial_window_reaches_unified_reader`: start empty Record at tick 0; at most 64 real Track owner grants must publish complete silent Capture current with exact token/generation and provisional `[0,768)` / 768 geometry, no raw/first-return authority. First epoch-1 TrackManager edge must select its physical window, no MIDI/ledger soundings and duplicate-edge silence. Existing accepted On@700/Off@740 assertions require immutable silent current, two-row successor, exact On@1468/Off@1508 and ledger closures. Missing initial publication is expected RED; downstream assertions are NOT RUN when this gate fails.
- `test_live_capture_current_survives_accepted_input_and_is_superseded_by_the_frozen_ready`: independently start/prepare silent live current; accepted On@780/Off@1008 advances Capture token but preserves its served storage. Real two-bar stop (1536 ticks) differs from provisional geometry, so it must not relabel the stale current; a bounded owner continuation must prepare source-qualified frozen successor while job/range remain held, then actual edge adoption must emit the independent bar-two pair once and retain the source through deferred retirement. Initial publication absence is expected RED; no synthetic current is installed to reach downstream assertions.
- `test_live_capture_current_at_nonzero_clock_epoch_is_not_claimed`: start empty Record with epoch 1; 64 owner grants must satisfy the same eventual published-Capture contract. Current epoch-0-only branch is explicitly expected RED, not a defect fixed by this slice and not device readiness.

Boundary outcomes: private completion is not publication; missing current/ready is waiting/unimplemented, never fallback PASS. Accepted input supersedes only private work; a mismatched stop geometry owes corrected frozen coverage. Nonzero epoch remains unclaimed. Numeric 64-grant limits are existing logical fixture bounds, not device service guarantees.

## Verification recipe

Baseline and extended flagged: `PLATFORMIO_BUILD_FLAGS='-DUNIFIED_RECORD_CAPTURE_INITIAL_ORACLE' pio test -e native -f test_runtime_working_set_ownership`. Record exact case IDs, failures and earliest assertion; Unity harness ERRORED is reported separately. Unflagged focused ownership/finalization/prewarm, then `pio test -e native`; strict change validation and `git diff --check`.

Controls: temporarily withhold the private-build test's actual owner call; existing private-build completion must turn RED, then restore byte-identically. Reader-removal and published-current supersession controls are NOT RUN until initial publication is reachable; do not modify firmware to force them. Builds/RAM1 and device qualification NOT APPLICABLE to this test-only slice; installed firmware unchanged. Pre-slice test copy and dirty diff are in session scratch `i5-baseline/`; command logs in session scratch are temporary, results recorded here.

## Completion record

- Baseline flagged command: 193 passed, one failed assertion (`test_record_capture_initial_window_reaches_unified_reader`: `owner=2 complete=1 published=0 raw=1 len=768 loopLen=0 sourceGen=0`), plus one harness ERRORED/SIGHUP entry; exit 1. Existing three private-build/stop controls GREEN.
- Extended flagged command: 193 passed, three failed assertions, plus one harness ERRORED/SIGQUIT entry; exit 1. Initial-current failure unchanged; `test_live_capture_current_survives_accepted_input_and_is_superseded_by_the_frozen_ready` fails at the explicit missing-published-current prerequisite; `test_live_capture_current_at_nonzero_clock_epoch_is_not_claimed` fails at its named epoch-1 non-claim. Later source/stop/edge/MIDI assertions are compiled but **NOT RUN**. No content/reader/lifetime PASS is inferred.
- Withheld-owner mutation: only the private-completion test's `stepPlaybackViewMaintenance` call replaced by a no-op; 192 passed / four failed / harness ERRORED with SIGILL, exit 1. Its additional failure is exactly `only Track window-owner grants may finish a silent Capture candidate`. Restored call returns whole-file Edit anchor `241A`, identical to the pre-mutation extended file; temporary mutation absent. No production file edited.
- Ordinary focused command: `pio test -e native -f test_runtime_working_set_ownership -f test_shared_capture_finalization -f test_playback_prewarm` → 489/489 passed, exit 0.
- Full native: `pio test -e native` → 3,106 total / 3,099 succeeded / 7 skipped / 0 failed, 114 PASSED suites, exit 0.
- `openspec validate windowed-playback-generation --strict` valid; `git diff --check` clean. Registration comparison with pre-slice copy confirms none removed and exactly two gated cases added.
- One focused independent read-only review: no concrete in-scope defects; tick/geometry/source-retention assertions match normative Record scenarios and existing frozen-return oracle. No second review or broader readiness verdict.
- Builds, upload, device/reset/SD/content actions: NOT RUN, not required for tests/docs-only scope. Default rolling/compatibility firmware unchanged. No new decision, behavioral guide/authority change, archive, commit, push or PR.

Fixture delivery does not satisfy S1a — full GREEN transition gate or check off task 3.15a — all-route production oracle. Next is a separately authorized coherent initial-publication/three-role/stop-lifetime implementation contract; native epoch-0 preparation is not device-epoch readiness. Matching-stop preservation, source release after successor obligations, remaining-work controls, reader-removal/supersession RED controls and all-route/device qualification remain open.

Final restored gated rerun: same 193 passed / three named expected assertion failures / harness ERRORED-SIGQUIT, exit 1; all three existing private-build/stop controls passed. Session closeout: runtime state/scope, deliverable tracking, task evidence and latest plan handoff updated; no checkbox or new decision/exclusion policy. Ordinary native artifact restored after this gated run.
