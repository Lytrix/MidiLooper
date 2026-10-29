# Apply evidence — reboot undo/redo consistency

Baseline `7c3f69ff`, branch `feature/unified-rolling-playback-path`. Planning docs were already dirty; pre-existing untracked session metadata/export files preserved. No device operation or source-retention repair.

## Fixture-first execution

- Existing `pio test -e native -f test_live_capture_display_boundary`: **43/43 PASS**, 2.07 s, before changes.
- New `test_reboot_undo_redo` reuses the existing real display/storage/capture/undo/playback closure. `HARNESS_REAL_STORAGE_LOOP_UNDO` removes only StorageLoopIo's native counter substitution by compiling that inclusion's target branch. Actual `TrackUndo::rebuildSlotFromLoopContent` runs inside apply. Neither reconstruction nor adoption is a success stub.
- Both source and parsed staging owners are destroyed before playback. Source roots, where required, are built by the real committed-pass publication request/worker **before serialization**, not injected into the restore target.
- First runnable attempt had one fixture setup error: successive direct Loop commits did not supply a settled root. Corrected by an explicit production rebuild request/drain for the snapshot-bearing source. This is source construction, not a restore repair.
- `pio test -e native -f test_reboot_undo_redo`: **4 actual cases, 2 PASS / 2 FAIL**. First-cycle passes-only and accepted-root tests pass. Same-window display fails `Expected 2 Was 3`. Loaded rootless playback emits 6 events at revision1/one role, 4 after undo at revision2/two roles, **0 after redo at revision3/two roles** (`Expected 6 Was 0`). Pass-payload/refcount assertions pass even at silence. PlatformIO's parser adds a signal/error wrapper around nonzero Unity failure-count exits; raw binary reports 4 tests/2 failures, not 5 actual test cases.

## Display prerequisite discovered during Apply

`rebuildVisualCacheIdleSlice` calls `visualCacheGatherCoversPlaybackRevision(..., false)` after the final dirty slice: rootless PassesResolve output is never stamped current, even when the explicit pass-state refresh completed successfully. Thus the planned revision-qualified replacement would still refuse correct notes. User explicitly approved including this prerequisite after the finding was reported.

Tightened contract: stamp the existing revision only on a completed, allocation-successful pass-state refresh from actual PassesResolve with no rejected-overlay provenance, after all dirty bars clear. No new state, queue, source role, runtime owner or persistence field. Track the actual successful fallback, not the initial optimistic `fallbackGatherComplete` default. Detect projection refusal inside the pass resolve as well as its final copy. Partial/denied/rejected output remains unqualified. Add an independent pre-fix freshness assertion and negative coverage.

Redo still needs its separate loaded-slot lifetime/alias contract; two-role saturation is now reproduced on actual loaded content but does not justify reapplying the rejected admission release.

## Lightweight preflight and historical reuse

Authority/project intent/ownership/readiness and the active artifacts were loaded. Search terms: undo, redo, restored, retained source, pass-state refresh, display qualification. Reuse decision YES: Loop cache builder/qualification and DisplayManager existing compose/swap/key path. Relevant historical boundaries: DEC-188 length-invariant refresh, DEC-199 source by currency, DEC-205 rejected admission release and DEC-206 focused scope. No reassessment trigger for an owner/lifetime/format change; no source-retention code changed. Rootless cache certification was explicitly approved by the user after the missing prerequisite was exposed. No new timing guarantee or supported-envelope change is asserted.

## Display RED → GREEN and refusal review

- Raw five-case binary before firmware edits: first-cycle rooted/rootless 2 PASS; stale frame `Expected 2 Was 3`, missing stamp `Expected 2 Was 0`, redo silence `Expected 6 Was 0` — 3 FAIL.
- After cache stamp and same-window adoption, display and stamp cases PASS; redo silence remains FAIL.
- Qualification tests: current empty interval succeeds; stale/invalid/out-of-bounds and partial 24-bar refresh fail without content; completed empty viewport replaces the old note; denied copy preserves frame/key and resource recovery publishes survivors; denied cache refresh cannot certify empty projection.
- Independent review found a late wrap-projection denial hole. The deterministic allocator-boundary fixture REDs before the guard at `denied wrap completion must leave its slice retryable`. Guard added, fixture now PASS including frame/key preservation and exact canonical wrapped source after recovery. Native countdown/attempt observations are test-only; no target fields or allocator policy change.
- Added rejected stale-root overlay, undo-after-resolve/in-flight immutability, cross-track/clear, and clock stop/restart with a queued save and unchanged in-memory card. Full scrolled-window/slot-switch/load/save-active-item matrix remains separate from those narrower controls.

## Playback characterization, not a repair

With an unrelated snapshot-bearing control track advancing at every same clock tick, the control emits its six events through each observed failure. Rootless target: revision1/one retained role emits6; undo revision2/two roles emits4; redo revision3/two roles emits0. Its actual chunks still have positive reference counts and readable original events; alias token is unchanged across these gestures. Thus this failure is source admission/selection, not evidence that undo deleted payloads.

Accepted-root target: first undo allocates role1; redo role2 serves6; the next undo reaches revision4 with both roles retained and still serves6 rather than4. The third-revision refusal is not rootless-only: the initial root shifts when the slot exhausts roles. Both acceptance tests intentionally stay RED and stop at the first wrong cycle; the declared eight-cycle loop is not claimed verified past that failure. Control-track MIDI channels are explicitly configured because the shared native TrackManager starts all tracks with channel1; the first multitrack attempt conflated channel routing with silence and was corrected without changing its output oracle.

## Firmware build

`pio run -e teensy41-capture-serial` **SUCCESS**, 52.698 s. RAM1 variables **96,192**, ITCM code **391,992** + padding **1,224**, locals **34,880**; padding floor840, headroom384. These match the baseline approved RAM1 bank/variables line. RAM2 variables170,112, EXTRAM11,776. HEX SHA-256 **`2c054f1cf22f204773df7d411a74aa8aa6c1f92339263c46ad2c7f4ee59e9321`**, retained with ELF/build log/dirty-tree metadata in `captures/reboot-undo-display-2c054f1c/`. **Not uploaded.** Subsequent changes are native fixture bodies only, not target behavior.

The initial full-native command was terminated by the shell's120-second timeout before all suites ran, so it is not suite evidence. A new full run uses a longer limit and is recorded separately below when complete.

## Final native evidence and open gates

`pio test -e native` completed in **426.700 s**, exit1, 136 processed suites. PlatformIO reports **3628 cases / 4 failed / 7 skipped / 3616 succeeded**. Its extra signal/error wrapper is not an actual Unity test: the new binary reports exactly **18 tests / 14 PASS / 4 FAIL / 0 ignored**. Existing suites contribute3602 passes and7 skips; no existing suite fails. Required focused suites are present and PASS: runtime working-set ownership, live display boundary, storage loop I/O, continuous capture. The four failures are all explicit new acceptance/characterization cases:

- `test_saved_slot_repeated_undo_redo_keeps_playing`: first redo expects6 target events, gets0, with control track still emitting6 and original target payloads readable.
- `test_saved_root_repeated_undo_redo_keeps_playing`: second undo expects4 target events, gets6; both retained roles are occupied, control still correct.
- `test_restored_record_replacement_commits_the_new_take_without_source_maintenance_loss`.
- `test_restored_truncated_record_replacement_preserves_the_accepted_note`.

Both Record fixtures accept the new note, admit a stop job, then retire without a committed pass. Their cause is **UNKNOWN** (could be an unmet fixture/owner interleaving); they are **not** attributed to this display patch or silently accepted. A controlled pre-change comparison used a `git archive 7c3f69ff` tree in the approved temporary directory with a separate `PLATFORMIO_BUILD_DIR`. The same actual production Record/storage/undo bodies were unchanged; current test harness/link support and API declarations were copied for compilation only. The isolated runner registers only first-cycle rooted/rootless controls plus the two Record cases. Source/staging destruction controls **2 PASS**, the same Record assertions **2 FAIL** before the display patch as well. The first isolated compile lacked the gitlinked font library; after providing that unchanged read-only dependency, the behavioral result is reproducible. No primary worktree reset/revert occurred. This establishes a pre-existing fixture/behavior boundary, not a product cause.

Final focused `pio test -e native -f test_reboot_undo_redo` has the same18 actual cases/4 failures. Raw native exit is4 (Unity failure count), not a CPU crash. All fourteen display/restore controls, including the late-wrap review regression, PASS. The literal eight-cycle playback acceptance stops at its first wrong cycle; neither the remaining cycles nor a redo repair is claimed passed.

Logs, raw output, baseline comparison, summary, build, ELF and HEX are retained together at `captures/reboot-undo-display-2c054f1c/`. OpenSpec strict validation and `git diff --check` PASS. No upload, serial command, physical SD operation, commit, push or archive occurred. Device/timing acceptance is NOT RUN. No delivered-behavior tracking row was changed because nothing shipped.

**Apply pause:** display repair and its existing-owner prerequisite are implemented. Full verification remains open: repeated playback-source turnover needs a concrete safe loaded-slot contract before production edits; Record fixture attribution and the remaining complete interleaving matrix are not closed. No fresh universal source role, admission-path release, reference-accounting change or persistence model has been approved.
