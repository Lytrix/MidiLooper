# Source-turnover continuation — 2026-10-29

## Authorized boundary

The user requested continuation, then explicitly approved the same-inventory descriptor-reuse contract (DEC-208). Baseline remains `7c3f69ff` plus the earlier uncommitted display checkpoint. Prior changes and untracked session/export files are preserved. No commit, upload, serial command or physical SD operation.

## G0 attribution and fixture corrections

- `tryReleaseRetainedPlaybackSource` has no production caller. Explicitly exercising that existing operation outside admission on a loaded rootless slot frees one role but leaves all live stored payloads readable with positive references; global alias advances. Redo then serves6. This is a characterization, not permission to restore DEC-205 or an explanation of its device regression.
- Record jobs were rejected as stale because the fixture wrote arbitrary LoopId700 instead of the Track-owned slot ID. Source serialization now uses `track.loopIdForSlot(0)`, and apply asserts body/slot identity consistency.
- With the corrected identity, the real stop worker stages both accepted rows, normalizes/transfers/commits them, and retires. The old test then queried effective content before its ordinary publication owner was ready. It now independently checks raw committed rows/IDs/ticks first, then drives publication and checks the unchanged expected two effective rows. Both normal and truncated Record controls PASS without Record production changes. Prior claims of unexplained Record loss are superseded, not treated as product evidence.
- Source admission, state toggles and consumers are the existing Track/Loop/TrackUndo owners. Reuse changes only a quiescent descriptor's identity/role; its actual chunk ownership stays put. Snapshot construction/persistence/display remain separate continuations. No new deferred work, owner, role, pointer lifetime or SD representation is added.

## Fixture-first and implementation evidence

Before the reuse mutation, the corrected new suite had24 actual cases:20 PASS/4 FAIL. The original rooted/rootless repeated-playback oracles still fail; explicit quiescent reuse and refusal-recovery also fail with `Admitted` expected versus `RoleCapacityReached`. Existing reader/inventory refusal negatives PASS. No compilation error is counted as a behavioral RED.

The repair shares the original pure retirement eligibility terms with the existing release method. On full capacity only, admission requires current request revision/generation, body/slot identity, an older Retiring role, no current reader/capture/session/finalization/build/gather/coverage-source borrow, no complete current/ready/retained view naming that revision, and identical ordered capture/edit pins and chunk IDs. It leaves pins/owned refs intact, updates revision/generation/role and uses the existing index publication. **No release call, refcount adjustment or third descriptor.** Matching is a linear allocation-free metadata comparison in the existing admission owner; no timing guarantee is inferred.

Focused command:

`pio test -e native -f test_reboot_undo_redo -f test_runtime_working_set_ownership -f test_continuous_capture -f test_shared_capture_finalization -f test_live_capture_display_boundary -f test_storage_loop_io`

Result: **644/644 PASS**, exit0,56.189s. New suite: **27/27 PASS**, including:

- eight full rooted/rootless undo/redo cycles with exact target MIDI tuples and literal NoteIds, concurrent control playback, intact original payloads, stable alias/refcounts and exactly two source roles;
- index/source revision coherence after reuse;
- refusal/recovery for live capture/session/finalization and retained gather;
- actual builder begin/fill/order followed by ready publication and handoff, each protecting the old revision;
- new capture/edit ID membership, same IDs with newly cloned chunk bodies, and noncurrent request identity refusal;
- fresh Record+successor overdub and both restored Record controls;
- original display empty/refusal/wrap/in-flight/track-clear controls.

## Independent review

Focused source-lifecycle reviewer `ses_e77a7f7a4ffeCBzhQtwiUkWnFo`: **APPROVE**, no concrete violation. Checked the new source admission paths, shared retirement predicate, handoff/index currency, exact inventory matching and fixture oracles. No builds/tests were run by the reviewer. Specific coverage limitations remain: dedicated bridge-build/coverage-source/retained-view/body-ID-mismatch negatives, same-count inventory reorder, long-loop rolling reuse. Guards inspected are not equivalent to those tests. The earlier display review remains separate.

## Verification limits

Device timing/content/display acceptance is NOT RUN. Native success does not authorize a flash or a test on occupied user slots. Dedicated source-reuse negatives identified by review and long-loop rolling-source reuse remain recorded limitations, not universal lifecycle qualification.

## Full native and remaining display matrix

`pio test -e native`: **3636 cases / 3629 succeeded / 7 skipped / 0 failed**, exit0, **403.617s**. All six required focused suites and the27-case reboot suite are present. The7 skips are the existing isolated recovery ABI cases, not hidden reboot acceptance.

Three additional test-only cases were then added to close the specifically planned display interleavings:

- 24-bar scrolled viewport preserves the old interval/revision honestly while a rootless current source is unavailable, then adopts the correct new interval/content when the production publisher completes;
- actual Track active-slot switch and stopped same-slot loaded-body replacement reject the previous frame/key and select the new slot/root correctly;
- global undo admits real save work, the existing persistence worker starts an active item while stopped, and a completed piano-roll frame leaves its active state, writer count, slice count and fake-card bytes unchanged.

`pio test -e native -f test_reboot_undo_redo`: **30/30 PASS**, exit0, after those additions. The full-native result above is the earlier27-case version, not a fabricated3639-case single run. It remains applicable to the unchanged firmware and all unchanged legacy suites; the final30-case run covers the entire expanded reboot fixture, including the helper's new slot parameter. No production file changed after the full-native run. Logs/commands distinguish these checks rather than folding counts together.

## Firmware evidence

Incremental `pio run -e teensy41-capture-serial`: **SUCCESS**,69.839s. RAM1 variables96,192; ITCM code391,992 + padding1,224; locals34,880; floor840/headroom384; RAM2 variables170,112; EXTRAM11,776. All equal the approved baseline RAM1 line. HEX SHA-256 `04ff4a3782504d5799bdd4af49a690a40c67897c324f6629306f2adc17c7a64d`, retained with ELF/build/dirty-tree context at `captures/reboot-undo-reuse-04ff4a37/`. **Not uploaded.**

Clean verification: `pio run -e teensy41-capture-serial -t clean`, then `pio run -e teensy41-capture-serial` — **SUCCESS**,103.903s, unchanged approved RAM1/bank line above. Clean HEX SHA-256 **`a1f4f131e683f17d67093c6f401aaab156d70401c482aa938a078a9045fa5910`**; retained with ELF, clean/build logs, full-native log, final30-case log and dirty-tree metadata/diff at **`captures/reboot-undo-reuse-a1f4f131/`**. This hash differs from the incremental image; no byte-identical binary claim is made. The clean retained image is the upload candidate. **Neither image has been uploaded.**

## Completion boundary

All implementation/native/build/review tasks are complete for the approved repair. The remaining tasks are upload/stimulus/target authorization and device acceptance. The prepared recipe records that the canonical baseline's occupied default slots are not authorized scratch space. Request upload permission for the clean candidate before any board action; do not automatically run Record/clear/content tests on user slots.

Source review's specific broader negative/rolling qualifications remain limitations beyond the demonstrated same-inventory case, not failures hidden by a threshold change. No claim of device timing, universal source reclamation, shipped behavior or archive is made. Runtime/guide/decision/tasks are updated; DELIVERABLE_TRACKING is unchanged because no firmware has shipped.
