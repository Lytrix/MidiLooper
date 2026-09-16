# Task 2.3 ownership checkpoint — 2026-09-16

This is an implementation checkpoint, not task completion. The accepted direction
is in [the continuous-capture plan](CONTINUOUS-CAPTURE-IMPLEMENTATION-PLAN.md).
The user authorized that direction and requested continuation of task 2.3.
The behavior and ownership audit below do not authorize dropping its later gates.

## Starting state and historical review

HEAD: `d120e0c2b731c5453ae1e1a31393709ca7de46e3`. Existing changes were preserved.
The starting tracked diff was retained at `/tmp/task23-start.patch`, SHA-256
`b3774b76c5d7a83d0838255db73098a80879eebb5b1fe91b181909876b4f71fe`.
The existing untracked implementation plan was also preserved. No matching local
capture-serial firmware hex was available in the configured build cache at the
start; that is not evidence about which firmware is on the device.

Review inputs: PROJECT_STATE, CURRENT_WORK, authority README, architecture,
delivery and naming rules; the active proposal/design/specs/tasks; decision-review,
preflight and ownership-transfer templates; LOOP_MIDI_STORAGE_AND_VALIDATION.
Searches for capture, stop, queued intervals and ownership covered DECISION_LOG,
the active change, archived timeline-epochs/timeline-passes and historical handoffs.
DEC-020 preserves chunk/persistence ownership, DEC-043 requires complete bounded
units, DEC-059/060/061 preserve companion semantics, and DEC-063 governs protected
undo publication. None authorizes resetting a newer capture at old-job cleanup.

Reuse decision: YES. Extend LoopEventStore for the exclusive range cut, then
parameterize the existing Track stop worker. No second Loop, canonical resolver,
playback worker or scheduler is introduced. The initial storage slice changes no
cross-module owner or persistence format. The broader approved migration below
remains incomplete and the production admission guards remain necessary.

## Owner and release checklist

- `Loop::capture.store`: currently one producer, cleared by `beginCapture`.
  The implemented storage primitive transfers its directory exclusively to a
  read-only `LoopEventStore::CaptureRange`, exchanging a pre-reserved empty tail.
  A range may be released in bounded chunk units only after all its readers retire.
  Track/Loop capture lifecycle has NOT been connected to this primitive.
- `Loop::capturePreview`, capture append counters and `Track::pendingNotes`:
  currently singleton live state. They must stay with live ingress; stop-time
  closures must be job-owned before reentry. `pendingNotes.clear()` in deferred
  cleanup is still a concrete live-state hazard.
- `RuntimePlaybackViewSet::captureStaging`: currently shared by live input and
  the frozen stop. It cannot be reset for B while A borrows it. The FIFO head
  needs a bounded adapter from its sealed range; input reservations must move
  to independently prewarmed ingress ownership. This is not implemented.
- `Track::pendingStopAdmission_`: currently one stop tick, epoch, destination,
  continuation and recycle receipt. It must refer to the queue head's interval;
  completing it must not unconditionally transition newer live capture to PLAYING.
- `Loop::pendingCapturePass_`, pending changes and prepared companion history:
  currently tied to the active Loop/capture session. Borrowing shared worker
  scratch must be explicitly job-qualified; successor semantic resolution must
  wait for its causal predecessor. Range storage alone does not establish this.
- Overdub session undo, Track's before snapshot, session-exit snapshot handoff
  and prepared transfer receipts: retain originating interval/session/slot until
  their actual consumer accepts them. No old-job cleanup may consume a newer
  session receipt. The existing protected-undo work remains required.
- Selected bridge/base/cursor: retain current ownership and immutability while
  the alternate candidate prepares. Raw ranges are not audible publications.
- Transport Stop: must retarget every queued job by epoch and preserve accepted
  input for STOPPED recovery. Current singleton retargeting is not queue coverage.

## Range-cut choice and evidence

Choose pre-reserved tail rotation, not shared offsets over a mutable producer.
`detachChunksTo` scans/seals/transfers the whole chunk list. Its behavior cannot
be used at the action edge. `cloneShared`, mutable rewrite and the public raw
chunk mutation APIs also do not provide a shared immutable-prefix contract.

`reserveCaptureTail` reserves a full 512-entry chunk-ID directory plus one empty
pool chunk, cold. `sealCaptureRange` checks both reserved owners, seals only the
old tail (earlier chunks were sealed during append), swaps the directories and
scalar extent, and advances ownership generations. The read-only range records
its exact final chunk offset. It exposes no mutable store or chunk-ID list.
Readers validate the range generation and pool lifetime. Old rewrite cursors
validate store ownership generation before touching either interval. Active
release processes at most the requested number of references. Destruction is a
cold teardown operation, not an active release path.

Native tests cover 0/1/255/256/257/769 events, A and B retained while C appends,
partial and full tails, one-reference release, reuse/stale read denial, stale
rewrite denial, physical pool exhaustion, and pool reset with reused chunk IDs.
A reserved cut and first successor append work with no free pool chunks.
Source/destination directory capacity is unchanged by a cut; no event array is
copied and no pool chunk is allocated at that boundary.

This primitive is not full note-lifetime reservation: a first tail does not
reserve arbitrary later NoteOff, wrap, bridge or stop-finalization capacity.
Cold vector reservation still has InternalHeapFirstAllocator's existing fatal
heap-exhaustion policy; fallible descriptor admission is an unresolved integration
gate. Do not claim the pool-exhaustion fixture proves heap-failure handling.
Each prewarmed directory has 1,024 payload bytes; each spare consumes one existing
EventChunk. No deployed live-plus-queued capacity or device memory admission is
declared by this isolated slice. Those require the actual job receipt layout and
simultaneous ingress/head/selected-source reservation accounting.

## Public-route reproducer

`test_continuous_capture` compiles the actual MidiButtonActions, TrackManager,
Track lifecycle/input, TrackUndo session start, Loop and runtime implementations.
It injects musical input through `MidiHandler::handleMidiMessage`. Native clocks
and physical I/O are boundaries; unrelated domain routes fail explicitly if called.
No reentry guard, capture acceptance, stop admission or undo-session start is mocked.
The existing interpreted Button A callback is used; gesture timing is unchanged.

A first-interval control passes. The service-paused A/B/C test fails because B
never captures its immediate notes. Independent manager and Track entry tests also
fail, including A-held-C4/B-C4. These are intentional failing requirements, not
skipped or rewritten into assertions of the existing bug. They remain in the
native suite. They do not yet prove queue membership, job cleanup, predecessor
resolution, stop epochs or audible parity; those require the integration above.

## Migration and remaining gates

Next bind one fixed-capacity descriptor ring to the existing owners above,
including pre-reserved note lifetimes and undo/snapshot receipts. Adapt the one
worker graph to its FIFO head; only then remove handler, manager and Track guards.
No production compatibility path is removed in this slice. The singleton capture
barrier may be retired only when task 2.3's public-route and job-isolation tests
pass; all task 5.3 HITL and task 5.4 removal gates remain unchanged.

Task 2.3 stays unchecked. No upload, device-state change, archive or commit was
performed. The complete reentry interaction is not fixed by this checkpoint.


## Verification checkpoint

- Final focused native run: 209 passing range, persistence, store and playback
  prewarm tests (`/tmp/task23-final-focused.log`).
- Full native run before the final persistence-prewarm correction: 2,136 passed,
  one skipped, three intentional reentry failures (`/tmp/task23-all-native.log`).
  The suite is red; the public-route behavior is not implemented.
- Strict OpenSpec validation and `git diff --check` pass.
- Capture-serial firmware compilation/linking reaches the size gate, which fails:
  RAM1 variables 92,128, code 426,024, padding 32,728, local-variable space
  **−26,592 bytes** (`/tmp/task23-firmware-build.log`). No upload was attempted.

- An isolated reconstruction of the starting tracked tree builds successfully:
  RAM1 local-variable space 6,176 bytes (`/tmp/task23-baseline-build.log`). The
  added checks crossed the 416 KiB ITCM boundary by 40 bytes, rounding RAM1 code
  allocation up by 32 KiB. Placing the existing bounded rewrite maintenance
  worker in flash restores the gate without changing ingress placement.
- Final capture-serial firmware build **passes**, with RAM1 variables 92,128,
  code 425,512, padding 472 and **6,176 bytes** for local variables; RAM2 free
  474,208, EXTRAM variables 9,312 (`/tmp/task23-firmware-final.log`). Firmware hex
  SHA-256: `0f607215ff5768b0fc50f4f812dbc84ecdbdfa3c7a839585dfcd59e0873d0787`.
  This identifies a local build only. Flash-worker device timing remains an
  explicit HITL gate; no device was flashed.


## Worker integration checkpoint — stopped-interval receipt

The next production slice replaces Track's standalone pending admission with
`CaptureStopJob`. Admission transfers the live pending-note map by constant-work
swap and records the owning Loop pointer/id, slot, phase, geometry and interval
identity. The one existing worker resolves its Loop/runtime from that receipt;
reserved closure no longer discovers its owner through the active selection.
Changed owner/geometry is denied before mutation, preserving the pending job.
Transport-stop retargeting and grain telemetry likewise select the job's slot.

Pending-note reclamation removes one job-owned map node per turn after history
recycling. It never clears the live map. Completion transitions Track state only
when the job's interval and slot still own it. Existing Loop session/preview
cleanup remains inside the guard: this is NOT proof that newer Loop capture can
coexist yet. No runtime worker, scheduler, Loop or Track was cloned.

Native integration tests run the production empty, paired-note and held-note
stop pipelines to completion. Targeted fixtures inject selection/geometry
changes, a newer live-note map and interval identity to verify closure target,
reclamation and Track-state isolation. These injections test ownership only;
they do not substitute for the still-failing public reentry cases.

The live-plus-queued ring, independent staging/capture/preview, undo/snapshot
receipts and predecessor resolution are still required. All three public reentry
guards remain. Raw CaptureRange is not yet wired to the worker source. Task 2.3
remains unchecked; there was no upload, commit or archive.

Verification for this integration slice: 266 passing tests across the two
non-overlapping affected-suite runs; the three original immediate-reentry cases
remain failing (`/tmp/task23-job-focused.log`, `/tmp/task23-job-extra.log`). Seven
production-route/ownership tests pass. Strict OpenSpec validation and diff checks
pass. Capture-serial builds with 5,600 bytes RAM1 local headroom (576 bytes below
the prior checkpoint), RAM2 free 474,208 and EXTRAM variables 9,312. ITCM has only
24 bytes before its next bank boundary; further firmware changes must recheck
that gate. Local hex SHA-256:
`dc6c2cc97e19f4a92c48bb0585c290f9afaa4ac1e1cbb0eb26f6428c8660a12d`.
No upload or device timing validation was performed.


## Canonical output ownership checkpoint — 2026-09-16

Production stop preparation now copies the frozen canonical order into a distinct
`LoopPlaybackRuntime::canonicalCaptureOutput`, using physically reserved pool
chunks. It no longer rewrites the raw capture chunks in place. The worker copies
at most 32 rows per turn, then releases one copied raw source chunk per turn.
Prepared-pass staging takes the explicit output Capture; the receipt records
whether its transfer owns live capture retirement. External-source joint and
ordinary commits preserve live phase, counters, rows and preview. Preview cleanup
also accepts an explicit owner, but production session/preview handoff is still
singleton and has NOT been migrated to a queue.

Each prewarmed runtime reserves eight physical output chunks (2,048 events) and a
512-entry fixed chunk-credit directory (1,024 payload bytes), plus the output
store's prewarmed 512-entry chunk-ID directory. The runtime resides in its existing
external allocation. Setup/load prewarm establishes the credits. Start rejects
before changing capture if the output credits are unavailable. Post-commit cleanup
or an otherwise eligible VisualCache turn replenishes one credit and returns.
Full-pool refill does not block completion of accepted data; later admission stays
explicitly denied until credits can be replenished. This is not a declared queued
interval capacity or a proof of fallible heap allocation or on-device prewarm.

Active transfer verifies directory capacity before consuming the prepared row.
Empty transfer preserves that row. Oversized transfer denies without sealing or
moving its source. Transfer with sufficient prewarmed storage requires no free
pool chunk and succeeds when the pool is full. Legacy cold transfer keeps its
existing pool policy.

Tests cover raw sealed-source immutability during production copy, ordinary and
joint external-source commit, full-pool output/transfer, maximum 2,048-row copy,
minimum-length filtering, stale generation and replaced output rejection, credit
refill, and transport Stop during a partial copy through the production stop
entry point. The latter verifies STOPPED destination and canonical rows through
commit; physical silence and later cleanup are separate test/HITL obligations.

The three service-paused reentry tests remain failing. No FIFO, causal successor
source, per-job preview/session/undo/snapshot receipt or raw-range cut is wired
into the production punch boundary. Generic runtime reset still discards shared
capture staging; a queued head must own that lifetime independently before the
outer guards can be removed. The partial-copy transport-Stop test does not prove
arbitrary runtime reset recovery. The new raw chunk release path currently relies
on the retained single-job ingress guard. Task 2.3 stays unchecked.


Verification for the canonical-output checkpoint:

- Full native: **2,155 passed, one skipped, three reentry failures**
  (`/tmp/task23-all-cow-final.log`). The suite remains red; this is not acceptance.
- Maximum-capacity/stale-output focused suite: **163 passed**
  (`/tmp/task23-copy-capacity.log`).
- Strict OpenSpec validation and `git diff --check` pass.
- Capture-serial build passes (`/tmp/task23-cow-release-build.log`): RAM1
  variables 92,704, code 425,976, padding 8, local headroom **5,600 bytes**;
  RAM2 free 474,208. `PendingStopAdmission::begin` and rewrite setup are placed
  in flash to avoid crossing another ITCM bank; ingress placement is unchanged.
  The final source-only comment clarification does not alter this binary.
- Local firmware hex SHA-256:
  `ca82de96af55ac72e41049ba7220d17294e9dde2463f43c91ea7afac25674869`.
  No upload: this does not identify the firmware currently on the device.


## Pending-job lifetime across playback reset — 2026-09-16

The admitted job now lends its PendingStopAdmission to its runtime until cleanup.
`LoopPlaybackRuntime::reset` returns false while that receipt is pending, retargets
it to STOPPED, and rebinds the frozen canonical command epoch without discarding
staging, canonical-copy output, order cursors or companion preparation. The MIDI
playback caller yields when reset is denied. Track cache invalidation likewise
sets the output barrier and requests STOPPED recovery before changing playback
generation. Terminal STOPPED cleanup performs the deferred reset after releasing
the receipt. Ordinary unowned cache resets retain their prior behavior.

This supersedes the generic-runtime-reset loss recorded at the preceding
checkpoint for an admitted singleton job. It does not establish FIFO lifetime,
slot destruction/clear ownership, causal successor capture or multiple simultaneous
sessions. The source/geometry checks remain required and unchanged.

Native regression injects reset after a partial 32/300-row copy, through both the
runtime and Track invalidation route, and repeats reset to check idempotent stop
epochs. It verifies all 300 accepted rows commit and cleanup releases the borrower.
The fixture deliberately advances interval identity to keep physical MIDI silence
outside that test; on-device silence remains a verification obligation.

A capture-only Tier-A `DIAG,capture_prewarm` line reports zero-based track/slot,
requested full-view mode, canonical-output readiness, reserved output chunk count,
free event-pool chunks and internal heap free bytes at prewarm. It performs no
PSRAM allocator-chain walk and adds no per-event logging. `ready` covers the
canonical output reservation only; it does not claim all bridge/view resources
are admitted. Existing startup MemoryMonitor samples report actual PSRAM headroom.

Before expanding physical interval reservations, verify this footprint on hardware:
the prior 2026-09-14 HITL boot failed from eager prewarm exhausting PSRAM. Static
RAM1 sizing and host tests do not establish available device heap after loading the
user's saved slots. This is an intermediate reservation/timing checkpoint, not
acceptance of immediate reentry. The three reentry failures remain open.


Reset-checkpoint verification:

- Every stop phase is covered by reset injection with 300 CC rows and a held
  NoteOn: exactly 302 rows survive, including the reserved terminal Off, and the
  runtime borrower is released only at cleanup.
- Full native: **2,157 passed, one skipped, three known reentry failures**
  (`/tmp/task23-reset-all.log`). No failures are hidden or waived.
- Strict OpenSpec validation and `git diff --check` pass.
- Initial build crossed the ITCM bank by 56 bytes. Placing playback-reset setup
  in flash restores the size gate; MIDI ingress/emission placement is unchanged.
- Final capture-serial build passes (`/tmp/task23-reset-build-final.log`): RAM1
  variables 92,704, code 425,928, padding 56; local headroom **5,600 bytes**.
  RAM2 free 474,208; EXTRAM variables 9,312.
- Local firmware hex SHA-256:
  `d926209f15fd02f37a28371fb45fbf34e70e2a91fc86d523b34e30b50b017a35`.
  No upload or device mutation occurred. Request confirmation before uploading
  for the intermediate FOCUSED-STOP-HITL.md memory/timing procedure.


Upload follow-up (2026-09-16, user confirmed): capture-serial upload succeeded
(`/tmp/task23-reset-upload.log`). The uploaded hex SHA-256 remains
`d926209f15fd02f37a28371fb45fbf34e70e2a91fc86d523b34e30b50b017a35`.
Teensy MIDI returned at `/dev/cu.usbmodem154944801`. Serial evidence is
`captures/task23-reset-prewarm/session_20260916_133639.log`; summary is `result.json`
in that directory. Boot/load reports `ok`. All complete captured output-prewarm
records report ready=1 and eight credits; after restored-slot admission, free
pool chunks=440 and internal heap free=339,968 bytes. The log starts mid-line,
so the complete startup/PSRAM sample is missing: **INCONCLUSIVE_CAPTURE**, not
HITL PASS. Record/overdub/Stop tests await an explicitly selected empty scratch
track/slot. No slot was cleared and no musical/control test input was sent.


## 2026-09-16 authorized track 5 / slot 1 hardware checkpoint

Firmware on device: hex SHA-256
`d926209f15fd02f37a28371fb45fbf34e70e2a91fc86d523b34e30b50b017a35`.
User authorized upload and named track 5, slot 1 as the scratch location. The
normal probe did not clear it; subsequent interruption recipes cleared only that
authorized scratch slot. No other slot was cleared or workspace quarantined.

- `captures/task23-reset-normal/result.json`: two normal overdub stops verified
  through DeferredCleanup, no scored timing issues or capture overflow. This run
  had no correlated emission rows and is not a full audible-deadline proof.
- `captures/task23-reset-interruption/interruption-reverified.json`: focused
  single-track Stop precedence/silence **PASS**. Device Stop arrived 9,406 us after
  job entry; all-notes-off followed in 3,031 us; stopped cleanup completed after
  142,109 us. More than 15.9 seconds of post-cleanup device observation and actual
  USB MIDI output show no late NoteOn or return to PLAYING.
- The original interruption report was INCONCLUSIVE_CAPTURE because its verifier
  expected the obsolete visible OVERDUB_STOPPING-to-STOPPED edge. The corrected
  host verifier requires same track/slot/capture generation, a PLAYING-intent
  pre-Stop grain, a newer command epoch, and STOPPED DeferredCleanup completion
  before another job entry; earlier completion or another slot cannot qualify.
  Original logs/results are preserved; reevaluation has a separate filename.
  The broader baseline still reports incomplete expected musical/display markers;
  only the explicitly focused interruption verdict passes.
- `captures/task23-reset-restart/restart-result.json`: **PASS**, three complete
  playback cycles contain every expected base/overdub/held note, without extra,
  duplicated or unmatched notes. This is content survival, not deadline safety.
- `captures/task23-reset-dense-interruption/interruption-result.json`:
  **INCONCLUSIVE_CAPTURE**. The dense attempt lost USB, reconnected at
  13:44:47.654881 and showed a new boot. The captured boot marker says crash=0;
  no retained diagnostic identifies the reset cause. Stop/commit evidence was
  lost, so the greater-than-one-chunk device check has NOT passed. User confirmed the reset was spontaneous, after an overdub note needed to
  resolve a record-pass note. This identifies the observed trigger, not the
  underlying cause; allocator exhaustion and stack exhaustion remain unproven.

Full prewarm for the target slot reported output-ready=1 with eight credits;
internal heap samples were 294,912 and 73,728 bytes in the normal/interruption
runs respectively. This does not measure complete PSRAM headroom. The startup
capture was partial; retain the earlier INCONCLUSIVE_CAPTURE label.

Host regression tests: 24 pass across stop intervention, restart survival and
timeline-probe suites. Firmware unchanged during these host verifier edits.
Task 2.3 remains unfinished (FIFO/session owners and three reentry failures), and
neither the dense gate nor full task 5.3 matrix is accepted by this checkpoint.


**2026-09-16 overlap reboot investigation / human verification checkpoint:**
User confirms the dense-run reboot was spontaneous after an overdub note resolved
a record-pass note. A new production-route native test exercises Shorten of the
record-pass note (Off at hold-start minus one) and stop completion; it passes.
Focused native result: 12 pass, the same 3 unfinished reentry requirements fail
(`/tmp/task23-overlap-final.log`). This does not reproduce or explain the device
fault; the capture-only overlap diagnostics are absent from the native fixture.

Found and fixed a crash-evidence defect: the installed Teensy core's
`CrashReport::printTo` clears its retained report. Startup previously printed to
Serial, then tried to print again to SD and sampled the now-cleared boolean.
Consequently the dense-run `crash=0` cannot exclude a recorded core fault. Startup
now snapshots the boolean before printing and renders once to a sink that writes
both Serial and `crashlog.txt` when SD opens. No claim that this fixes the reboot.

Capture-serial build and upload pass (`/tmp/task23-crash-report-build-final.log`,
`/tmp/task23-crash-report-upload.log`). Uploaded hex SHA-256:
`11bbc435befcd5dba617678bdd29ce4ecb5d9bbf65d04b792613a5190a460e73`.
RAM1 local headroom remains 5,600 bytes; startup sink methods reside in flash.
Post-reboot dense capture reported PSRAM 4,158 KB free / 4,024 KB used at startup;
that is not the memory state at the failure.

Next HITL: reproduce the observed record-pass/overdub overlap on authorized track
5 / slot 1 with the serial reader in `captures/task23-overlap-crash-report`.
No automated MIDI stimulus is running. Preserve the reconnect and startup output;
if Serial misses it, the one-pass crash sink can retain the report on SD.
Task 2.3, the dense gate, and full task 5.3 remain open.


**Dense-test interpretation clarification:** the failed hardware run was explicitly
intended to exercise more than one 256-event chunk. It did not complete, so native
chunk tests cannot close that device gate. The user's observed overlap is a clue,
not an established alternative cause. The retained log does not establish that
the first-to-second chunk transition caused the reboot, nor that deferred
canonical-copy processing was reached. Preserve both boundary and overlap paths
in the next reproduction. Serial capture is now open in
`captures/task23-overlap-crash-report/session_20260916_135500.log` (exec session
28933); the prior 135431 attempt could not open the sandboxed serial device and
was stopped. No automated MIDI stimulus is running.


**2026-09-16 user correction / automated rerun:** the dense chunk test is an
agent-operated HITL recipe, not a required manual reproduction. The standalone
serial reader was stopped, and the exact earlier dense recipe was rerun against
track 5 / slot 1 with the diagnostic firmware. Evidence directory:
`captures/task23-dense-crash-report-rerun`; runner output:
`/tmp/task23-dense-crash-report-rerun.log`. Do not require manual gestures to
repeat an already automated test. Result pending.


**Automated dense rerun result (2026-09-16 13:57):** identical recipe on track 5 /
slot 1 rebooted again on diagnostic firmware
`11bbc435befcd5dba617678bdd29ce4ecb5d9bbf65d04b792613a5190a460e73`.
`captures/task23-dense-crash-report-rerun/session_20260916_135633.log` records
Overdubbing start at 188.453 s, last pre-reboot output at 190.100 s, reconnect at
13:57:21.424792, and `boot_reset,crash=1`. This confirms a retained core crash
report, unlike the previous destructive boolean read. Serial reconnected after
the early crash text, so the fault address/type are not in this capture; the
new sink attempted to preserve them in SD `crashlog.txt` (file not yet read).
The runner finished and its serial/MIDI owners closed. Focused verdict remains
INCONCLUSIVE_CAPTURE with incomplete_capture/no_device_stop_inside_stopping;
the dense hardware gate has not passed. Chunk-boundary causation remains unproven.
No manual reproduction is required to repeat this automated recipe.


**Crash diagnosis and targeted diagnostic-stack change (2026-09-16):**
The stopped-only `!CRASH_LOG` command retrieves the last 4 KiB of `crashlog.txt`
without modifying it, into the host capture. Retrieval succeeded:
`captures/task23-dense-crash-report-rerun/crash-log-retrieval.log` contains a report
at system time 13:57:08 matching the rerun: PC=0x6358C, CFSR=0x82 (DACCVIOL),
MMFAR=0x20016A3F, identified by the core as the stack boundary. The saved matching
ELF `/tmp/task23-crashed-11bbc435.elf` maps PC to `_svfprintf_r`, with a failing byte
store during numeric formatting. This establishes a stack-boundary access fault;
it does not identify chunk rollover as the cause or reconstruct the caller stack.

The overlap observation path stacked two 128-ID sets and a 256-byte formatting
buffer above noteOn's PendingNote. Moved only these diagnostic temporaries to
foreground-only RAM2 scratch, cleared on every use. No note/session ownership or
resolution semantics change. Compiled snapshotOverlapHoldCandidates local stack
allocation falls from 1,372 to 76 bytes (register saves excluded in both figures).
RAM1 headroom remains 5,600 bytes; RAM2 free becomes 472,928 bytes.

Build/upload pass (`/tmp/task23-overlap-stack-build-final.log`,
`/tmp/task23-overlap-stack-upload.log`). Firmware hex SHA-256:
`e8a6b46d45576f7c96e4d6d3423beb267abdabde3c67b0cad4b3da0564417d86`.
Identical dense recipe is running under `captures/task23-dense-stack-fix`.
No acceptance claim pending its result.


**Post-fix device results (2026-09-16):**
- Exact original recipe, `captures/task23-dense-stack-fix`: no reboot; canonical
  copy phase and terminal DeferredCleanup complete. Copy phase has 19 observed
  turns, maximum 104 us, including raw-source retirement turns. The 578 grain
  field is capture generation, NOT a measured row count. Do not derive chunk
  count from it. Stop arrived 47,320 us before job entry, so the focused
  interruption verifier correctly remains inconclusive for this run.
- Same dense musical stimulus with only intervention delay changed from 305 to
  380 ms, `captures/task23-dense-stack-fix-stop-window/interruption-result.json`:
  focused single-track Stop precedence/silence PASS. Device Stop arrived
  82,042 us after job entry, all-notes-off followed after 3,039 us, stopped cleanup
  completed 168,895 us after Stop, and 18,976,636 us of subsequent observation
  contain no late NoteOn. No reboot occurred.
- Both broad baseline reports remain INCONCLUSIVE_CAPTURE for their reported
  setup/content/display/persistence marker issues. These are not waived. Neither
  run proves restart survival, layered playback, full deadline coverage, or the
  remaining FIFO/reentry requirements. Both runner/capture owners have exited.

The matching crash evidence plus two post-change no-reboot dense runs support
keeping the reduced-stack diagnostic scratch. Task 2.3 remains open. Next
integration work remains fixed FIFO and isolated session/preview/undo ownership;
full dense content/count and timing acceptance still needs explicit evidence.


**2026-09-16 dense content/timing verification and descriptor integration:**

Read-only `!CAPTURE_CONTENT` prints selected stopped-loop raw passes and effective
canonical events into the serial capture. It denies active capture, transport or
pending stop; it does not edit the slot. The last successful upload for this
readback has hex SHA-256
`f8565f07728784bb114ee14370432916957d59b9a4a6c296658ef34aad18af8f`.

`captures/task23-dense-content-retry/input-review.json` verifies 568 stored overdub
events (three 256-event chunks): 188 NoteOns, 188 matching Offs, 192 CCs. Note
coordinates/values match the original dense capture exactly; CC values match in
order. Five input pairs (pitches 19, 101, 121, 52, 63) lasted 7, 6, 7, 7, 1 ticks
and are excluded by the existing enabled eight-tick minimum. Held pitch 83 closes
at the original stop phase 2835. No unexplained missing/extra retained note rows.
The record pass has 192 events; effective content has 760. Source checks and
capture acquisition scripts are retained beside the evidence. This does not
prove CC tick equality or full effective companion semantic parity.

`captures/task23-dense-stack-fix-stop-window/dense-timing-review.json` shows no
capture loss/malformed stop rows but is NOT a timing PASS: one CanonicalOrder grain
crossed its recorded service deadline, another began overdue; two captured clock
samples were 3,899 and 1,885 us late during PLAYING. CanonicalCopy observed maximum
102 us. There are zero correlated emission rows, so audible deadline safety is
unproven regardless of the coarse verifier's empty issue list.

Restored playback verification is incomplete: unclocked and explicitly clocked
restart probes received no target-channel musical events, so no full cycles could
be compared to effective canonical rows. See `captures/task23-dense-content-clocked`
(content-review.json and service-status.log). Transport and output-policy/readiness
cause are unresolved; do not call this content-playback PASS or infer data loss.
Selecting an already selected slot toggles playback, so acquisition explicitly
stops again before the content request. It never clears/records new data.

The stop receipt now lives in a cold-prewarmed external-memory-first ring of four
stable descriptors on Track. The existing worker borrows the head; cleanup clears
its runtime pin before advancing to the next descriptor. Completed slots are
reused without copying job-owned maps. The ARM allocation is 616 bytes/track
(4,928 bytes for eight tracks), excluding map nodes and allocator overhead;
RAM1 static headroom improves from 5,600 to 6,752 bytes. These four metadata slots
are NOT a declaration that four simultaneous captures are supported. Admission
still retains the singleton staging/session guard. Missing prewarm rejects before
opening input. No full staging buffers or worker graphs were duplicated.

Native sequential rotation test covers stable receipt address, pin lifetime,
head wrap/reuse, and existing snapshot handoff service between sessions. Three
public reentry requirements still fail. Raw-range/staging/preview/companion/undo
and predecessor isolation remain required before allowing multiple queued jobs.

Descriptor build passed (`/tmp/task23-fifo-descriptor-build.log`). Attempted upload
failed: no Teensy found and serial port absent (`/tmp/task23-fifo-descriptor-upload.log`).
The descriptor migration is NOT verified on device. User has been asked to
reconnect/press PROGRAM. No serial/MIDI owner is active. Broader native validation
and final build results follow below; task 2.3 and full hardware gates stay open.

**2026-09-16 dense verification / FIFO descriptor slice:** stored dense overdub
content matches input (568 events, three chunks, five expected short-pair removals).
Timing is NOT passed: a stop ordering grain crossed a service deadline and two
late clock samples are retained. Restored musical playback produced no target
channel rows in the probes; output policy/readiness remains under investigation.
Stable external Track descriptors replace the singleton stop receipt; sequential
rotation passes, but staging/session/predecessor isolation and three reentry tests
remain open. Full native: 2,159 pass, one skipped, three expected reentry failures;
final focused admission/rotation: 14 pass and the same three failures. Build passes
with 6,752 bytes RAM1 headroom. See active FOCUSED-STOP-HITL.md for evidence and
latest upload status; no task checkbox closes.


**2026-09-16 latest device state / verification blocker (14:29):**
The serial port reappeared after the first upload failure. Final descriptor build
and retry upload succeeded (`/tmp/task23-fifo-final-build.log`,
`/tmp/task23-fifo-final-upload.log`), hex SHA-256:
`a9a7217fc99a0eca5a1cbee199179211822046201707f9327f1018a043ca2066`.
This is the last successfully flashed firmware; earlier f856 readback was on the
previous firmware. It includes the final reject-before-capture descriptor guard.
Full native run: 2,159 pass, one skipped, three existing reentry failures
(`/tmp/task23-fifo-all-native.log`). Final focused run after the guard test:
14 pass and the same three failures (`/tmp/task23-fifo-final-focused.log`).
Strict OpenSpec validation and diff checks pass. No commit or archive performed.

Read-only post-upload status shows track index 4/slot 0 unmuted, audible and
enabled. The barrier=1 was sampled after transport Stop and is expected; it does
not explain earlier no-output playback. Boot output reservation prewarm succeeds
with eight credits, 440 free chunks and heap_free=339968; full-view readiness is
not implied. Descriptor allocation is cold and source/session migration remains
incomplete.

A fresh dense hardware recipe on this build cleared only the authorized track 5 /
slot 1. It then lost the serial device during INITIAL RECORDING, before overdub
or intervention. Last retained device output is 98.445 seconds. No reconnect or
new crash marker was captured. Evidence: `captures/task23-dense-fifo-descriptors/`
and `/tmp/task23-dense-fifo-descriptors.log`, verdict INCONCLUSIVE_CAPTURE. Do not
call this the same overlap crash, a proven FIFO regression, or a successful
hardware validation. The runner and serial/MIDI owners have finished; no serial
port is currently visible. User reconnection/program-button help was requested;
no answer confirming manual disconnect has been received.

Next: reconnect, retain startup/crash evidence for this disappearance, resolve
restored playback output and the measured deadline failures, then finish sealed
range/live reservation/session/predecessor isolation before multiple FIFO admission.
No additional FIFO buffers/guards were loosened to bypass these verification gaps.
Task 2.3 remains incomplete (12/20 overall).
