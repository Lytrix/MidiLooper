# HITL pass-validation corrections

## Authorized scope and owners

Owner requested implementation of the read-only analysis findings. Host tooling only, existing boot handoff, note-edit scenario/verifier, seed inventory and managed evidence retrieval owners. No firmware, new instrumentation, canonical stimulus, numerical envelope, upload, reset, MIDI gesture, slot overwrite, SD operation or Git delivery.
Active change: runtime-working-set-ownership, schema spec-driven; parent seeded edit/continuity acceptance remains open. Existing live-state/canonical teardown spec applies. Existing dirty work is preserved.

Contract: require an applied empty selection (note_idx == -1), the intended source move to the occupied destination, and both expected identities. Normalize paired immediate/deferred events without treating arrival order as occurrence order or double-counting twins. Missing identity/order evidence is named incomplete, never a guessed PASS. Qualify seed capture, committed intended shape/content and cleanup before edit gestures. Boot timer-only release and stale rows cannot establish app-screen readiness. Optional zero-observation cases are UNMEASURED, not PASS. Pending stop-job evidence remains a blocker; fence diagnostic must not conceal it.
No new runtime owner or firmware state/bound/resource change; host memory only. Directories/queues/undo/persistence on device unchanged. Ordinary owner extension, no formal reassessment trigger.

## Frozen regression oracle before corrections

- Nonempty applied note at the requested empty tick must fail deselect.
- A Move action with wrong destination/source identity must fail, even if two other destination indexes exist.
- Immediate move/deselect/reselect trace and that trace with delayed duplicate empty/select twins must yield the same core result and counts.
- Conflicting/ambiguous or unqualified event identity/order must fail closed, not synthesize a historical apply.
- Recorded intended seed must have complete evidence and successful cleanup; run09 with incomplete drain/pending stop queue remains unusable; requested two-bar fixture cannot be silently three bars.
- Boot setup+commit+later non-frame row remains unproven; stale timestamped DFRAME cannot complete current boot; valid current-boot app-paint witness completes; no-banner attach behavior and existing bounds preserved.
- Missing motor/highlight/retention observations reported UNMEASURED; core scenario proof does not qualify them.
- Pending stop job at deadline retains its latest phase/queue fields in diagnostic alongside any missing fence, and never completes merely because Record counters balance.

## Existing evidence / qualification boundary

Review ran108 focused tests green despite missing counterexamples. Saved run07 snapshot now passes core under old direct-row changes but judged motor/held=0 and retention fields null. Synthetic nonempty deselect and wrong-destination move both falsely PASS; delayed duplicate deselect changes PASS to FAIL, both channels double-count notes. Timer-only boot and stale DFRAME both falsely complete. Run09 seed helper accepts INCONCLUSIVE_CAPTURE; Record counters balanced but STOP_STATUS still pending1/phase16/queue1/4 ~30s later, only PERS,result predates recording, no current save completion. Do not label that merely a report-window gap or cured capture loss.

## Verification

Fixture-first tests in existing/new host suites; scratch logs for baselines/red/full checks. Focused modules include boot handoff, reselect guards/stimulus, seed precondition and managed Record drain. Full host unittest discovery baseline and final comparison; identify pre-existing failures rather than weakening them. `pio test -e native` compatibility. Strict change validation/diff hygiene. One bounded independent review of completed changes, then fix/retest concrete findings only. No device test in this slice; saved-run qualification reports must retain provenance and distinguish core proof from aggregate completeness.

## Completion

Local implementation verified; hardware acceptance remains open.

- Boot correction:33 focused tests,14 RED failures/subtests before source edit,33/33 GREEN. No elapsed-time-only release; well-formed DFRAME must follow both handoffs and timestamped commit/current banner. App-paint path evidence is not completed-DMA/physical-pixel proof; firmware lacks boot-generation IDs, so timestamp-compatible stale rows cannot be distinguished absolutely.
- Selection correction:14 adversarial cases initially12failures/1missing-status error; repaired. Require applied empty idx=-1 and actual intended Move source/destination/pitch/current focus ID, then both distinct current IDs with corresponding selected pitch/start geometry. Direct occurrence channel is authoritative when present, suppressing deferred twins/count inflation; untagged deferred-only chronology does not qualify.
- Live verdict includes current-leg snapshot boundary; offline replay starts at current NOTE_EDIT entry and refuses ambiguous multiple-Move windows. Offline layout first requires observed committed RECS/REVT inventory, configured channel, exact requested length and source/destination pitches; layout fallback cannot vouch for missing observations.
- Optional motor/held/highlight/retention cases carry PASS/FAIL/UNMEASURED and nullable ok. Core proof reports PASS_CORE separately from qualification_complete; zero observations never certify broader gates.
- Seed correction:59 tests initially26 failures/subtests;64/64 after complete fixtures/current data qualification. Record-only seed requires complete capture, exact committed requested length/content, current successful save result and complete Record drain/two canonical persistence drains. Cached context/overall_ok cannot bypass it; generic full-loop legacy seed behavior remains unchanged.
- Drain diagnostic: two missing-field REDs repaired; latest stop_job_state/last_stop_status remains visible even when fence failure ends retrieval. Existing deadline/completion predicate unchanged; no firmware finalizer repair claimed.
- Focused final command: `PYTHONPATH=scripts .venv/bin/python -m unittest test_hitl_seed_qualification test_clear_before_record_precondition test_managed_record_drain test_hitl_reselect_validation test_hitl_stimulus_contract test_note_edit_reselect_guard test_hitl_boot_handoff test_edit_select_wait` —203/203,exit0.
- Full host discovery:1556tests /6failures+1error. Same seven cases reproduce against HEAD production modules in an isolated process: legacy EditSession recursion; machinery closeout/2compatibility inventories; foundation/layered verifier Namespace defects; occupy-paint legacy boundary. Broad sweep is NOT a green suite; unrelated failures not absorbed or hidden. Intermediate full run1550 had identical failure set; six review controls account for final count increase.
- Native compatibility: `pio test -e native` —3072total/3065succeeded/7skipped/0failed,exit0. No firmware edits/build/upload/device/SD action in this slice.
- Saved run07 and run09 seeds both rejected:2304ticks vs intended1536; run09 additionally incomplete capture/drain/current persistence result and pending stop queue. Final standalone replay of both remains INCONCLUSIVE_CAPTURE/observed_seed_inventory_unproven. Earlier core-only replay found missing selected geometry; no historical report was rewritten or promoted to device PASS.
- One focused review REQUEST CHANGES found stale prior-session proof reuse and manufactured fallback seed geometry. Both were regression-RED-proved, repaired and retested; no second review/approval claimed. Added live boundary, latest-entry/ambiguity guard and observed-seed admission; valid observed-seed/current-identity positive replay passes PASS_CORE. Final focused/broad checks above include these repairs.
- Strict runtime change validation valid and diff hygiene clean before documentary closeout. Scope/status/guide/spec/task updated. No new architectural owner/firmware bound/schema decision; no DEC addition. No commit/push/PR requested.

## Remaining device requirements

No clean composed hardware PASS claimed. Need separately authorized canonical run with pinned image/clock/tempo and exact2bar committed seed, observable current selected identity/geometry, real move/deselect/reselect plus successful final teardown. Missing non-Tier-A witnesses may still refuse qualification; do not manufacture them or increase bounds to force a pass. Run09's pending phase16 job/current save remains a genuine unresolved readiness finding, not merely a report-window/fence artifact. Parent seeded-edit/continuity tasks remain unchecked. Recipe: [HITL_TEST_RECIPE.md](HITL_TEST_RECIPE.md).
