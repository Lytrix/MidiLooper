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
- `RuntimePlaybackViewSet::captureStaging`: **split 2026-09-16** into two
  independently prewarmed roles in the same owner (`captureStagings[2]` with
  `liveStagingIndex`/`frozenStagingIndex`). Live input always writes the *live*
  role; the one admitted stop job borrows the *frozen* role. Punch-out freezes
  the live role and exchanges the two indexes in constant work; job cleanup
  resets only its own retired frozen role. A successor interval therefore owns a
  physically separate ingress buffer. The FIFO head still needs a bounded
  adapter from its sealed raw range — raw-range sealing is not wired yet — and
  more than one simultaneous job still exceeds the two roles and stays guarded.
- `Track::pendingStopAdmission_`: currently one stop tick, epoch, destination,
  continuation and recycle receipt. It must refer to the queue head's interval;
  completing it must not unconditionally transition newer live capture to PLAYING.
- `Loop::pendingCapturePass_`, pending changes and prepared companion history:
  currently tied to the active Loop/capture session. **Pending note changes are
  now job-owned** (2026-09-16, see below): swapped into `CaptureStopJob.pendingChanges`
  at admission with a job-identified read borrow for display, so the worker
  resolves against its own set while a successor keeps an independent prewarmed
  set. `pendingCapturePass_` and the prepared companion history are still tied to
  the active session; borrowing shared worker scratch must be explicitly
  job-qualified and successor semantic resolution must wait for its causal
  predecessor.
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


## Dual capture-staging roles — ingress/staging isolation checkpoint, 2026-09-16

Implements the "input reservations must move to independently prewarmed ingress
ownership" step named above. The runtime's single `captureStaging` is now two
independently prewarmed roles in the same owner.

**Owner and lifetime statement**

- Owner stays `LoopPlaybackRuntime` / `RuntimePlaybackViewSet`; no new module,
  Manager, worker graph or scheduler. No canonical, persistence or SD ownership
  moves.
- `captureStagings[2]` with `liveStagingIndex` / `frozenStagingIndex`. The live
  role is written only by live ingress (`Track::recordMidiEvents` admission,
  `Track::noteOn` reservation, the cross-wrap continuation path, and the
  `startOverdubbing` reset). The frozen role is read only by the existing stop
  worker and its canonical/companion/bridge helpers.
- `Track::beginDeferredOverdubStop` freezes the live role, then exchanges the two
  indexes in constant work (`LoopPlaybackRuntime::exchangeCaptureStagingRoles`).
  The admitted job therefore owns the rows it captured; the previous frozen role
  becomes the successor interval's clean ingress buffer.
- `Track::processDeferredOverdubStopGrain` resets only its own retired frozen
  role after `TrackPreparedStopCommit::completeDeferredCleanup`. It never touches
  the live role.
- A physical NoteOff for an already-open note selects the frozen role while a job
  is pending and the live role otherwise; it never crosses roles.
- Weak references retired with the job: the frozen role's `captureGeneration`,
  reservations and rows are cleared at cleanup; nothing retains a pointer to it
  across the exchange.

**Admission direction (DEC-067, 2026-09-17).**

- **START requires the live role to be free; STOP requires the deferred/frozen role
  to be free.** Supporting one finalizing predecessor plus one independently
  recording successor (depth 2 in the capture lifecycle, not depth 2 in the
  deferred-stop queue).
- A resource blocks the transition that first requires ownership of it. Overdub
  START is admitted on the live staging role, the stop-descriptor ring and the
  interval's own memory admission. The finalization resources — canonical capture
  output, active overdub transfer admission, and session-exit snapshot handoff
  capacity — are checked by `Track::beginDeferredOverdubStop`, not by
  `Track::startOverdubbing`; none of them is used before the successor's own stop
  edge. The stop admission is additionally the single-owner check
  (`captureStopOwner`).
- **Depth-2 capture concurrency does not imply depth-2 finalization working sets.**
  At most one interval may own the bounded canonical/transfer finalization
  resources; another interval may coexist only in the live capture role. That is
  why a successor START is legal while its STOP can be denied, and it is why the
  canonical working set stays single (the alternative — one canonical output per
  interval — would make two finalization working sets concurrent).
- The successor-stop denial is atomic and non-destructive: no stop descriptor, no
  consumed events/notes/reservations, and the interval stays the live recording
  interval with its event count, pending notes, reservation/NoteId identity,
  capture extent, preview state and interval identity unchanged.
- Evidence for this direction: `captures/successor-reentry-19` / `-20` (the device
  refused successor starts with `canonical copy chunks are not reserved` while the
  predecessor finalized) and `captures/successor-reentry-13` / `-14` / `-17`
  (successor starts only after `bridge_ready`); [DEC-067](../../../docs/DECISION_LOG.md#dec-067--finalization-resource-admission-is-stop-directed).

**Physical cost — measured on device**

- One additional `CaptureAdmissionStaging` per `RuntimePlaybackViewSet`:
  128 fixed note reservations plus one additional 2,048-event
  `FixedPlaybackEventStorage`.
- `RuntimePlaybackViewSet::fixedExternalBytes()` and its native assertion count
  both stagings, so the accounting is explicit.
- Device boot walk (build `fb4ab3a9…`, `captures/task23-staging-prewarm/result.json`):
  PSRAM free 5,143,968 B before boot staging prewarm -> 3,859,680 B after, i.e.
  **1,284,288 B for 8 slots (160,536 B/slot)**. That matches two 2,048-event
  stagings per slot (~167,936 B) and is ~2x one staging, so both roles are
  physically prewarmed on every boot slot. No internal-heap fallback occurred
  (heap_free 352,256 -> 344,064 B), every slot reports `ready=1` with 8 credits,
  and the pool is still **46.1 % free (3.86 MB of 8.38 MB)** after boot prewarm.
- This is boot staging-only prewarm. The selected slot's full view/bridge prewarm
  plus loaded dense content still needs its own measurement; the general
  "re-measure before removing the reentry guards" requirement stays.

**Verification**

- New native `test_playback_prewarm::test_capture_staging_roles_exchange_without_sharing_physical_storage`
  proves distinct physical buffers, constant-work role exchange, the frozen job
  retaining its rows, and successor ingress rows surviving the job's retired
  staging being reset.
- New native `test_continuous_capture::test_stop_job_and_live_ingress_own_distinct_staging`
  proves the same through the production Button A / Track punch-out route: role
  exchange happens at punch-out and a successor ingress row survives the job's
  full canonical work, commit and cleanup.
- `test_playback_prewarm` 164/164; `test_continuous_capture` 18 pass with the same
  three intentional public-reentry failures.
- Full native: 2,176 cases — 2,171 passed, one skipped, the same three reentry
  failures.
- `teensy41-capture-serial` build **passes**: RAM1 variables 91,552, code 425,752,
  padding 232, local-variable headroom **6,752 bytes**; RAM2 free 472,928;
  EXTRAM variables 9,312.
- Device boot prewarm admission **PASS** (`captures/task23-staging-prewarm/`,
  `result.json`): uploaded instrumented build `fb4ab3a9…`, clean reset
  (`boot_reset,crash=0`), all 8 slots prewarmed with `ready=1`/8 credits, no
  `[WARN]`/`[ERROR]`, no staging-unavailable or low-heap line, and 3.86 MB PSRAM
  still free. The instrumented build adds only one SESSION_CAPTURE-gated boot
  sample (`DIAG,capture_prewarm,phase=before|after`) and keeps the 6,752-byte RAM1
  headroom.

**Still required (unchanged guards)**

FIFO/queue-head descriptor binding, sealed raw-range wiring, causal predecessor
and source identity, per-job session/preview/undo/snapshot receipts, the three
public reentry cases, deadline/audibility evidence, and device PSRAM admission.
Task 2.3 stays unchecked; no upload, commit or archive was performed in this
checkpoint.


## FIFO/queue-head binding checkpoint — 2026-09-16

The ring is now the explicit admission authority rather than a decorative
container.

- `Track::captureStopJobTail()` is the admission target (head + count, null when
  full); `Track::captureStopJobHead()` is the worker's stable descriptor (null
  when empty). `beginDeferredOverdubStop` admits into the tail and reports a
  distinct `queue_full` stop-stage outcome when the ring is full instead of
  treating exhaustion as latency. `processDeferredOverdubStopGrain` binds the head
  **once** for the whole grain and no longer re-resolves the ring mid-step; the
  existing `captureStopJob()` accessor remains for callers that have already
  proven a pending job (transport-Stop retarget, `!STOP_STATUS`).
- `!STOP_STATUS` now reports `queue=<depth>/<capacity>` so ring depth is visible
  on device without new tag families.
- Behaviour is unchanged while only one job is admitted: with `count == 1` the
  tail *is* the head. The binding is what makes a later multi-job admission
  well-defined.
- Native: new `test_stop_ring_admits_at_tail_and_binds_worker_to_head` and
  `test_full_stop_ring_denies_admission_and_preserves_accepted_capture` (a full
  ring denies and consumes no accepted event, pending note or descriptor).
  `test_continuous_capture` 20 pass with the same three intentional public-reentry
  failures.

## Sealed raw-range wiring — audit and decision required

`LoopEventStore::sealCaptureRange` is landed and tested (DEC-064) but still has no
production caller. The audit for wiring it found a concrete blocker that makes
this an ownership decision, not a mechanical follow-up.

**Why the punch-out seal is not a drop-in**

1. `Loop::capture.store` is not only the worker's input. During the deferred stop
   it is still read by:
   - `mergeCaptureStoreIntoWindowedEvents` / `mergeCaptureStoreIntoMaterializedEvents`
     (Loop), which merge the producer into the display window whenever
     `captureActive()` — and which call `ensureCaptureEventsSorted()`, i.e. they
     **mutate the producer to sort it**;
   - `Track::appendCaptureNoteOffAtPhase`, which scans the producer to recover the
     `NoteId` of an open note so a physical NoteOff can close the pending job's
     reservation.
2. `sealCaptureRange` swaps the whole directory out, so the producer becomes
   empty. Sealing at punch-out without a read fallback therefore empties the
   live-capture overlay for the entire finalization window and breaks
   physical-closure note identity.
3. `Loop::beginCapture` calls `capture.store.clear()`, which frees a pre-reserved
   successor tail; a sealed producer needs a "reset extent, keep the spare"
   operation.
4. The seal is only *needed* when the producer must be reused, i.e. when a
   successor capture starts — and that route is still guarded.

**APPROVED design — sealed capture ownership transfer (2026-09-16)**

Ownership model:

```text
before punch-out                     after admitted stop
────────────────                     ───────────────────
Loop::capture.store                  Loop::capture.store        CaptureStopJob(head)
      │                                    │                          │
      └── owns active producer             │ successor producer       └── OWNS sealed rawRange
          extent                           │
                                           └── Loop holds a job-identified
                                               non-owning borrow ─────────────┘
                                               (overlay materialization + NoteOff identity)
```

- `Loop` owns the producer; the admitted head job owns the sealed interval as
  `CaptureStopJob::rawRange`. `Loop` holds only a **job-identified** non-owning
  borrow. DisplayManager is unchanged: the display asks `Loop` for effective
  capture material and never learns whether it lives in the producer or an
  admitted stop descriptor.
- **Authority rule:** once an admitted stop job owns a sealed capture range, that
  range is the authoritative source for the stopped interval until the job
  releases it. `Loop` may borrow it for capture-overlay materialization and
  NoteOff identity recovery but must not mutate, reorder, clear or release it.
- **Immutability:** the borrowed range is never sorted in place. Ordering
  operations copy into scratch and sort the scratch (`mergeCaptureStore…`
  previously sorted the producer in place).
- **One read source:** all consumers resolve capture material through a single
  internal `Loop` accessor that exposes the producer source and the finalizing
  (borrowed) source **separately**, so mutual exclusivity is not encoded as a
  permanent architectural truth. Today the guarded lifecycle means at most one is
  non-empty, and the merge helpers merge both when present.
- **Borrow lifetime follows job identity:** a job may clear or release the borrow
  only when the binding still identifies that exact job (`&job.admission`), so
  job A's cleanup can never clear job B's binding.
- **NoteOff closure:** a physical NoteOff belonging to a reservation created
  before sealing resolves its `NoteId` against the reservation's owning interval
  (the borrowed sealed range), even though the producer extent is empty.
- **`beginCapture()` boundary:** `resetCaptureExtentPreservingReservedStorage()`
  resets producer-visible capture state while preserving storage reserved for the
  successor producer and never touching an extent transferred to an admitted job.
  (Named for reserved *storage*, not the FIFO admission tail.)
- **Prewarm:** one reserved producer directory at setup/load, plus each job
  descriptor's `rawRange.reserveTail()` cold.
- **Release:** the worker retires the job's `CaptureRange` in bounded grains when
  the job owns it, else the legacy producer path.
- **No transfer on denial:** queue-full or otherwise denied admission performs no
  seal and no ownership transfer.
- **Sequencing:** wire this slice *before* removing the successor-capture guard,
  and do not unguard continuous successor capture in the same change. The
  multi-interval HITL payoff follows once the ownership layer passes on its own.

**Rejected alternative:** seal lazily at the successor start. It needs no display
change, but it is unreachable until the reentry guard is removed, so it would be
dead code that leaves the ownership invariant unproven.


## Sealed raw-range ownership — implementation checkpoint, 2026-09-16

Wired. Ownership now matches the approved model; DisplayManager is unchanged and
the successor-capture guard is still in place.

**Ownership and lifetime**

- `CaptureStopJob` owns `LoopEventStore::CaptureRange rawRange` plus
  `ownsRawRange`. `Task::beginDeferredOverdubStop` seals the producer into it and
  binds the Loop's job-identified read borrow at punch-out.
- `Loop` holds `borrowedSealedCapture_` + `borrowedSealedCaptureOwner_`.
  `releaseSealedCaptureRangeBorrow(owner)` clears only when the binding still
  identifies that exact owner, so a stale job cannot unbind a successor.
- `Loop::captureReadSource()` exposes the **producer** and the **finalizing**
  sources separately (mutual exclusivity is not encoded). `copyCaptureIntervalEvents`
  merges both into a scratch copy and sorts **only the scratch**; a borrowed sealed
  extent is never mutated, reordered, cleared or released.
- `Loop::findLastCaptureNoteOnIdentity` resolves a closure's NoteId from the
  finalizing extent first and the producer second, so a physical/synthetic closure
  whose source NoteOn now lives only in the job-owned extent still resolves.
- `Track::retireJobCaptureSourceGrain` releases the job-owned extent in bounded
  grains when the job owns it, else the legacy producer path. On completion it
  drops the borrow (identity-guarded) and re-arms that descriptor's reserved
  destination directory for a future admission. If re-arm fails, the next
  admission denies explicitly instead of reserving at the action edge.

**LoopEventStore contract**

- `canSealCaptureRange(destination)` is the non-mutating precondition check.
  Admission proves it before any mutation, so a denied admission performs **no
  seal and no ownership transfer**; the seal step after admission is
  precheck-proven and has no fallible successor.
- `resetCaptureExtentPreservingReservedStorage()` lets `beginCapture` reset the
  producer without releasing the successor directory a cut left behind.
  `beginCapture`/`discardCapture` fall back to `clear()` when no reserved
  directory exists.
- The seal preconditions were corrected for lazily-allocated producers: a
  producer with no capture lifetime (allocated by ordinary append) is a valid cut
  source; only a *stale* non-zero lifetime denies, and the producer is not
  required to hold a 512-entry directory of its own (the destination supplies
  it). At seal the producer re-adopts the pool lifetime, and the destination keeps
  the lifetime that lets `releaseGrain` free the transferred chunks.
- An **empty** capture owns no extent, so it skips the seal and stays on the
  legacy producer path.

**Design note the tests pin:** the stop-time *synthetic reserved closure* is
appended after the seal, so it lands in the successor producer extent while the
sealed extent keeps the captured rows. This is exactly why the read source must
expose both sources rather than "sealed-or-producer".

**ITCM/bank placement:** all new Loop/LoopEventStore/Track functions are
`FLASHMEM` (`LOOP_COLD_MEM` / `LOOP_EVENT_STORE_COLD_MEM` / `TRACK_COLD_MEM`), and
`Loop::beginCapture` / `Loop::discardCapture` moved to flash so the build stays in
13 ITCM banks. `teensy41-capture-serial`: RAM1 variables 91,552, code 425,832,
padding 152, local headroom **6,752**; RAM2 free 472,928.

**Verification**

- New native regressions: `test_seal_transfers_extent_and_successor_capture_preserves_it`,
  `test_read_source_yields_same_rows_before_and_after_sealing` (row parity plus
  byte-identical sealed extent after an ordering merge),
  `test_sealed_extent_resolves_closure_note_id`,
  `test_sealed_range_borrow_release_is_owner_identified`, and the extended
  `test_full_stop_ring_denies_admission_and_preserves_accepted_capture` (denial
  performs no seal/transfer). Three existing tests were migrated from producer
  ownership to the job-owned extent.
- `test_continuous_capture` 24 pass with the same three intentional public-reentry
  failures; full native 2,182 cases — 2,177 passed, one skipped, the same three
  failures.
- Device: the four destination directories per track add cold pool reservations.
  **Measured PASS** (build `41b30d18…`, `captures/task23-sealed-range-prewarm/result.json`,
  clean reset): boot prewarm PSRAM free 5,142,912 -> 3,856,320 B (160,824 B/slot,
  unchanged from the dual-staging build, i.e. the range directories are not PSRAM
  smalloc), internal heap 352,256 -> 311,296 B (-32 KB = 32 x 1 KB directories),
  and pool chunks 512 -> 416. The +4 chunks/slot over the previous build are
  exactly the four `CaptureRange::reserveTail()` destinations per track. All eight
  slots report `ready=1` with 8 credits, zero warnings, 416 free chunks and 46.0 %
  of the 8.38 MB pool still free after prewarm. Reserving only the head descriptor
  and re-arming per rotation would save 24 chunks and ~24 KB; not adopted because
  the measured cost is small and the head-only scheme adds a rotation-dependent
  admission window.

**Still open:** per-job session/preview/undo/snapshot receipts, causal predecessor
and source identity, the three public reentry cases, and unguarding successor
capture (explicitly out of scope for this change). Task 2.3 stays unchecked.


## Job-owned pending note changes — checkpoint, 2026-09-16

First piece of the session/preview/undo receipt slice, and a concrete hazard the
worker previously had: `beginFrozenCanonicalTail` and the admission-sizing path
read `Loop::pendingNoteChanges()`, the *live* session's Add/Shorten/Hide intents.
A successor interval would have appended to that same vector while the predecessor
finalized.

- `CaptureStopJob` owns `PendingNoteChangeVec pendingChanges`. At admission the
  live set is **swapped** into the job (constant work) and the live session keeps
  the job's prewarmed empty vector, so a successor interval starts with an
  independent set and neither the swap nor the next interval allocates.
  `prewarmCaptureStopJobs` reserves `kRecordsPerBridge` capacity per descriptor.
- `Loop::pendingNoteChanges()` is now the single readable accessor: it returns the
  job's owned set while a job-identified borrow is bound, else the live member.
  Writers keep using the live member directly, so a successor's edits can never
  enter the predecessor's set.
- The borrow is job-identified and released with the job
  (`releasePendingNoteChangesBorrow(&job.admission)`), matching the sealed-range
  borrow rule: a stale job cannot drop a successor's binding.
- The worker now calls
  `beginFrozenCanonicalTail(frozenSource, job.pendingChanges, targetSource)`, so
  canonical-tail resolution uses the interval's own intents.

**Verification:** new native `test_stop_job_owns_pending_note_changes` (production
route: a real Shorten intent moves to the job, the readable accessor is the
borrowed job copy, and after cleanup the live set is independent and empty) and
`test_pending_change_borrow_release_is_owner_identified`. The existing
`test_overdub_shortens_record_pass_note_and_completes_stop` now exercises the
job-owned path end to end. `test_continuous_capture` 26 pass with the same three
intentional public-reentry failures; full native 2,184 cases — 2,179 passed, one
skipped, the same three; `teensy41-capture-serial` builds with 6,752 bytes RAM1
headroom (unchanged).

**Still open in this slice:** the overdub session/undo before-snapshot, the
session-exit snapshot handoff receipt, and the prepared-companion history.

## Job-owned capture preview — checkpoint, 2026-09-16

The deferred cleanup seals the interval's preview (`retainCapturePreviewAtSeal`),
but it previously consumed the **live** `Loop::capturePreview`, which a successor's
`beginCapture` clears. A successor would therefore have made the predecessor's
deferred seal retain the wrong rows.

- `CaptureStopJob` owns `CapturePreview preview`. Admission transfers the live
  preview by constant work (`CapturePreview::swapWith`: per-vector swap, no row
  copy, no allocation) and binds a job-identified read borrow.
- `Loop::capturePreviewView()` is the single readable accessor: the borrowed job
  preview while bound, otherwise the live member. It is **out of line in flash** —
  inlining the branch at 40+ display call sites cost a whole 32 KB RAM1 bank.
  Writers (capture ingress, wrap-held marking) keep using the `capturePreview`
  member.
- `TrackPreparedStopCommit::completeDeferredCleanup` now takes the job's owned
  preview, so `loop.completePreparedCapturePassDeferredCleanup(job.preview)` seals
  this interval's rows.
- 45 read-only display/Edit call sites were migrated to `capturePreviewView()`
  (verified: no display/Edit code writes the preview; `applyCapturePlayheadTails`
  takes `const CapturePreview&`).
- The earlier per-descriptor `reserve`/`prewarm` capacity calls were dropped. The
  transfers are allocation-free either way, and preview/pending-change growth from
  empty matches the baseline boot behaviour; the prewarms only added ~640 bytes of
  new `vector::reserve` / struct-move ITCM code and pushed the build into a 14th
  ITCM bank.

**Verification:** new native `test_stop_job_owns_capture_preview` (production
route: the job owns the rows, the view is the borrowed copy, and after cleanup the
live preview is a different, empty buffer) and
`test_capture_preview_borrow_release_is_owner_identified`.
`test_continuous_capture` 28 pass with the same three intentional public-reentry
failures; full native 2,186 cases — 2,181 passed, one skipped, the same three;
`teensy41-capture-serial` builds with **6,752 bytes** RAM1 headroom (13 ITCM banks).

## Job-owned overdub-session undo baseline — checkpoint, 2026-09-16

`TrackUndo::pushOverdubPassAdded` (reached from `pushOverdubSessionOnStop` on the
stop-commit path) built the `OverdubPassAdded` entry's `beforePublishedSnapshot`
from the live `Track::overdubSessionBeforePublished_`, which
`TrackUndo::beginOverdubSession` sets at session open. A successor session would
have overwritten it, so the predecessor's undo entry would have recorded the
successor's baseline.

- `CaptureStopJob` owns `ImmutableLoopSnapshotRef beforePublished` plus
  `hasBeforePublished`. Admission **moves** the live baseline into the job and
  clears the live member, so the successor session's `beginOverdubSession` records
  its own baseline independently.
- `TrackUndo::pushOverdubPassAdded` now prefers the head job's owned baseline
  (TrackUndo is a friend of Track), falls back to the live session's, and only
  then captures a fresh snapshot. An unconsumed baseline is retired with the job
  at cleanup so it can never leak into a later interval.
- Ownership stays inside the existing owners: no new module, no schema change,
  no DisplayManager change.

**Verification:** new native `test_stop_job_owns_overdub_session_undo_baseline`
(production route: the job owns the baseline, the live member is clear, a later
successor baseline leaves the job's intact, and the flag is gone after cleanup).
`test_continuous_capture` 29 pass with the same three intentional public-reentry
failures; full native 2,187 cases — 2,182 passed, one skipped, the same three;
`teensy41-capture-serial` builds with **6,752 bytes** RAM1 headroom.

**Still open in this slice:** the session-exit snapshot handoff receipt and the
prepared-companion history.

## Deferred-stop receipt wiring — audit and decision required, 2026-09-16

Audit of the two remaining receipt items found that the *consumer* side is not
wired, which changes what "job-owned" has to mean.

**Finding 1 — the normal (deferred) overdub stop publishes no `OverdubPassAdded`
undo entry.** `pushOverdubPassAdded` (the only reader of the overdub-session undo
baseline) is reached only from `Track::finalizeCommitSideEffects`, which is called
only from `Track::commitCaptureForStop` — the *fault fallback* taken when
`beginDeferredOverdubStop` denies admission. `processDeferredOverdubStopGrain`
uses `TrackPreparedStopCommit::commit`, which passes `undo = nullptr`
(`commitWithUndo` / `commitStoppedWithUndo` have no callers). This matches the
standing note that "the protected undo publication is not yet connected to the
production stop pipeline".

Consequence for the previous slice: the job-owned undo baseline is correct
preparation but is currently **unreachable in production** — the job branch
requires a pending job, while the fallback that pushes the entry only runs when no
job was admitted. Wiring the deferred publication is what makes it load-bearing.

**Finding 2 — the session-exit receipt cannot be job-owned.** The handoff receipt
is deliberately *retained past* the job: `handoffSessionExitSnapshot` fills the
Loop singleton at DeferredCleanup, and the **idle snapshot publisher** consumes it
much later (`LoopSnapshotPublish.cpp` clears `receipt.pending` after
`queueSessionExitSnapshot`), while `LoopSnapshotPlayback.cpp` reads it for the
stale-root overlay in between. Job-scoped ownership would drop it too early.

**Required design (decision needed).**

1. Connect the protected undo publication to the deferred path per DEC-063 /
   [`UNDO-PUBLICATION-REASSESSMENT.md`](UNDO-PUBLICATION-REASSESSMENT.md): the
   worker must run the **already-prewarmed but unused** resumable machinery —
   `beginCollectOverdubSessionUndoPasses` / `stepCollectOverdubSessionUndoPasses` /
   `requiredSessionUndoCollectionGrains`, then
   `TrackUndo::prepareOverdubSessionEntry` with the **job's** baseline and slot,
   then `TrackUndoPublication::begin` + `stepPrepare` + `publishAtCommit` at the
   atomic commit. This adds bounded phases, admission counters and contract rows
   before `PendingStopPhase::AtomicTransfer`.
2. Make the session-exit receipt **Loop-owned but multi-slot** (a small ring sized
   like the job ring), not job-owned. Lifetime: written by the committing job,
   read by playback's stale-root overlay, consumed by the idle publisher; a free
   slot — not an unconsumed singleton — is what `canHandoffSessionExitSnapshot()`
   should test, so a successor is not serialized behind the publisher.

Open questions the decision must settle: the receipt ring size, and what playback's
stale-root overlay should do with **more than one** pending receipt (today it reads
exactly one). Both are behaviour-visible and belong in a recorded decision rather
than an improvised implementation.

**Status:** audited, not implemented. No code changed for these two items.

### Implementation blockers found while starting the undo wiring (2026-09-16)

Decision taken for this slice: wire the deferred protected undo publication first;
receipt-ring size 4; overlay every pending receipt. Starting the implementation
surfaced two blockers that need an explicit shape before touching the stop-commit
path.

1. **The collection needs its own admission budget.** The bounded collection cannot
   ride the existing canonical-preparation credits: `consumeCanonicalPreparationGrain()`
   requires `canonicalPreparationQuantum != 0`, and `canonicalCredits` is 0 for a
   STOPPED or empty destination — so a STOPPED overdub stop would stall forever.
   The canonical budget also cannot absorb a larger estimate without shifting
   `stepPreparation`/`retireCompletedPreparation` accounting. Recommended shape: a
   dedicated `remainingUndoCollectionGrains` / `undoCollectionQuantum` pair with its
   own `consume…` guard, sized from
   `Loop::requiredSessionStopUndoCollectionGrains(rows, companions, finalCompanions,
   quantum)`, and a `StopAdmissionContract` row so a saturated estimate denies
   admission explicitly.
2. **The protected transaction has a post-commit lifecycle that must be drained.**
   `TrackUndoPublication::begin` leaves `protectedState()` true until the
   `ReleaseRedo → AwaitReclaim → Trim → Complete` chain finishes, and other undo
   writers are excluded while protected. Publishing from the deferred commit
   without also running that drain would let the *first* overdub publish and block
   every later one. So this slice must include bounded drain phases in the worker's
   DeferredCleanup stage, not just `publishAtCommit()`. Placement also matters:
   `TrackUndoPublication` carries an `UndoEntry` plus retirement storage, so a
   per-Track member costs DTCM (only 6,752 bytes free) — it should be
   externally allocated like `captureStopJobs_`, or job-ring resident if its
   lifetime is proven to end with the job.

Neither blocker is conceptual; both are shape decisions. No code was changed on the
stop-commit path.

### Increment A landed — bounded undo collection with a job-owned prepared entry

Approved shape (dedicated phase + dedicated grain class + contract estimate).
Implemented the collection half; the publication half is increment B.

- `PendingStopPhase::UndoCollection` runs on the prepared PLAYING route only,
  between `CompanionHistory` and `CompanionBridge`, because
  `beginCollectOverdubStopUndoPasses` requires the prepared companion receipt to be
  current. The STOPPED/empty routes skip companion preparation entirely, so
  `retireSkippedCompanionPreparation` now also retires the collection estimate and
  those routes never enter the phase.
- New `PendingStopAdmission::remainingUndoCollectionGrains` /
  `undoCollectionQuantum` with its own `consumeUndoCollectionGrain()`, included in
  `finalizationComplete()`, and sized from
  `Loop::requiredSessionStopUndoCollectionGrains(...)` — so a saturated estimate
  denies admission instead of stalling a phase (blocker 1 resolved).
- `Track::stepDeferredUndoCollection` runs the resumable collection in bounded
  grains, then builds the entry with `TrackUndo::prepareOverdubSessionEntry` from
  the **job's** `slot` and `beforePublished`. The collection's private outputs
  (`undoPassIds` / `undoCompanionIds`) are pre-reserved in
  `prewarmCaptureStopJobs`; the API never grows them, and an unreserved output is
  reported as a denied collection rather than silent corruption.
- `Loop::reclaimUnreferencedDisabledEditPasses` moved to flash so the new phase does
  not push ITCM into a 14th bank.

**Verification:** new native `test_deferred_stop_prepares_job_owned_undo_entry`
(production route: the deferred stop collects, builds an `OverdubPassAdded` entry
with the job's slot/loop and the job's own baseline pointer). `test_continuous_capture`
30 pass with the same three intentional public-reentry failures; full native 2,188
cases — 2,183 passed, one skipped, the same three; `teensy41-capture-serial` builds
with **6,752 bytes** RAM1 headroom (13 banks, code 425,592).

**Increment B (next):** allocate `TrackUndoPublication` externally,
`begin` + `stepPrepare` it from the prepared entry, flip the atomic transfer to
`commitWithUndo` / `commitStoppedWithUndo`, and add the bounded
`ReleaseRedo → AwaitReclaim → Trim → Complete` drain in DeferredCleanup so
`protectedState()` cannot block the next interval's publication (blocker 2).

### Increment B landed — the deferred stop publishes its protected undo entry

The normal deferred overdub stop now records an `OverdubPassAdded` entry. This
closes the long-standing "protected undo publication not connected to the
production stop pipeline" gap for the prepared PLAYING route.

- `TrackUndoPublication` is **externally allocated** (`ProtectedUndoDeleter`, same
  pattern as `captureStopJobs_`) because it carries an `UndoEntry` plus retirement
  storage and must outlive the job through its drain. Prewarmed cold by
  `prewarmProtectedUndo()` from `prewarmCaptureStopJobs`.
- **The undo stack's own entry capacity is part of the same cold admission**
  (DEC-063): `prewarmProtectedUndo()` reserves `Config::ABSOLUTE_MAX_UNDO_ENTRIES`
  (512) entries. This is a *publication precondition* — `TrackUndoPublication::begin`
  rejects a transaction whose cursor has reached capacity, so without it the
  publication silently fell back to the plain commit. **Device PSRAM admission for
  this reservation must be measured** (external-first, up to 512 `UndoEntry` rows
  per track).
- `stepDeferredUndoPublication` runs `begin` + bounded `stepPrepare` inside the
  existing `UndoCollection` phase (its own admitted turns), so no extra phase was
  needed and no phase transition was rerouted.
- The atomic transfer proves publishability first
  (`canPublish(stack, loopId, pendingCapturePass().id)`) and only then calls
  `advanceAtomicTransferWithUndo`; otherwise it takes the plain commit and the
  transaction is restored, never left half-open.
- `stepProtectedUndoDrain` runs one bounded quantum per DeferredCleanup turn:
  `stepReleaseRedo` → `beginTrimAfterReclaim`/`stepTrim` → `finish`, plus a
  `beginRestore`/`stepRestore` + `recoverPreparedEntry` path for a prepared but
  unpublished transaction. `protectedState()` is Idle again before the job retires,
  so the next interval can publish.
- Three defects were found and fixed by the native regression while wiring this: an
  early `return true` that skipped the prepare turns, a `begin`-failure path that
  looped on `stepPrepare` forever, and a restore path that could never reach
  `finish()` because the private entry was still held.

**Verification:** `test_deferred_stop_prepares_job_owned_undo_entry` now also runs
the stop to completion and asserts (a) the transaction returned to Idle and (b)
exactly one `OverdubPassAdded` entry for the job's slot is on the global stack.
`test_continuous_capture` 30 pass with the same three intentional public-reentry
failures; full native 2,188 cases — 2,183 passed, one skipped, the same three;
`teensy41-capture-serial` builds with **6,752 bytes** RAM1 headroom (13 banks, code
425,208).

**Known deviations recorded:** the bounded disabled-pass reclaim continuation is
still unwired (DEC-063), so trim pressure is measured without it — that can only
trim at least as much as the fully wired policy would. The STOPPED/empty routes
still publish no protected entry because they skip companion preparation; the
legacy fault-fallback path keeps its cold `pushOverdubSessionOnStop`.

### Increments A/B boot abort — root-caused, fixed, device-verified (2026-09-16)

The memory check found the increment A/B firmware did not boot; a hardware bisect
then isolated and fixed it. The branch tip is flashable again.

**Symptoms.** Build `a7f10733…` (increment B, 512-entry per-track undo-stack
prewarm) emitted only `#CAP,3534734,DIAG,boot_reset,crash=1` before the USB serial
port dropped mid-boot. Build `8c03dcfc…` (same code, prewarm bounded to 32 entries)
behaved the same and the port disappeared entirely, so the undo-stack prewarm was
not the cause. The device was restored from commit `82a42221`
(`captures/task23-recover-verify/result.json`).

**Bisect (one commit per flash, boot + stream checked each time).**

| commit | content | result |
|---|---|---|
| `7a275d1f` | undo-baseline ownership | boots, 21,386 B, `crash=0` |
| `0f398715` | **increment A, job-owned undo collection** | **no boot, 0 B** |
| `162df28f` | increment B, protected undo publication | no boot, 0 B |

**Root cause.** `prewarmCaptureStopJobs` reserved
`RuntimePlaybackViewConfig::kRecordsPerBridge` (2048) rows for each job's
`undoPassIds` and `undoCompanionIds`. Both are
`std::vector<..., InternalHeapFirstAllocator<...>>` (`include/EditPass.h`), and the
stop ring holds four entries per track across eight tracks:
`32 x 2 x 2048 x 4 B = 512 KB` demanded from an internal heap with only ~340 KB
free at boot. The allocation aborted before the sketch could stream.

**Fix.** `kUndoCollectionReserveRows = 64` bounds that reserve
(`std::min` with `kRecordsPerBridge`), i.e. 16 KB total. A session with more than
64 wraps simply publishes no prepared undo entry and takes the plain commit — the
stop stays correct.

**Device verification of the fixed tip.** Build `91e6b38c…`: 22,091 bytes streamed,
zero warnings, prewarm samples `PSRAM 5,141,952 → 3,775,872`, `heap 352,256 →
290,816`, chunks `512 → 416` (`captures/task23-undo-prewarm-fix/result.json`). That
is roughly **+79 KB PSRAM and +20 KB internal heap** over the reference build — the
increment A/B footprint, with PSRAM still 45 % free and ~284 KB of internal heap
left. Its `crash=1` is the retained report from the earlier faults.

**Lesson recorded:** a per-entry cold reserve multiplied by the ring capacity and
the track count must be sized against the *allocator's* heap tier, not against the
policy maximum. Both this and the earlier 512-entry undo prewarm were caught only
by the boot sample.

### Session-exit handoff receipt ring — implemented (2026-09-16)

Resolves "Finding 2" from the deferred-stop receipt audit: the session-exit receipt
cannot be job-owned, so it is **Loop-owned and multi-slot**.

- `SessionExitSnapshotHandoff` is now a Loop-owned FIFO ring of
  `kSessionExitReceiptCapacity = 4`, `ExternalMemoryFirstAllocator`-backed and
  cold-prewarmed by `Loop::prewarmSessionExitSnapshotHandoff()` (which replaced the
  single-slot `sessionExitSnapshotHandoff_` unique pointer). Index 0 via
  `sessionExitReceiptAt(0)` is the oldest pending receipt.
- The gate is a **free slot**, not an unconsumed singleton:
  `canHandoffSessionExitSnapshot()` is `sessionExitReceipts_ != nullptr &&
  sessionExitReceiptCount_ < kSessionExitReceiptCapacity`. "One receipt per open
  session" is enforced *inside* `handoffSessionExitSnapshot()` through
  `sessionExitHandedOff_`, which the handoff sets after it consumes the session
  identities and `openOverdubSession()` clears. So a successor session may hand off
  while an older receipt is still unconsumed — the serialization the singleton
  imposed is gone — and a repeat handoff for the *same* session is still refused
  (it would otherwise queue an empty successor).
- The idle publisher consumes exactly one receipt per turn: with
  `allowStartConstruction == false` it retains the head and reports pending work;
  otherwise it queues the oldest receipt, clears it, and advances the head/count.
- Playback's stale-root overlay now folds **every** pending receipt (both `passIds`
  and companion ids) instead of a single one, so a successor's stale-root
  suppression does not depend on publisher progress.
- The ring is released by `Loop::~Loop()` through a dedicated
  `SessionExitReceiptsDeleter` defined beside the destructor in
  `LoopSnapshotLifetime.cpp`.

**Native:** new
`test_session_exit_receipt_ring_is_fifo_and_not_serialized` (four handoffs across
four sessions, capacity refusal on the fifth, FIFO receipt order, successor gate
open while a receipt is pending). The handoff-contract assertions in
`test_playback_prewarm` and
`test_loop_take_survival::test_prepared_stop_handoff_preserves_restart_before_and_after_publication`
were updated from "unconsumed receipt closes the gate" to the ring contract.
`test_playback_prewarm` 165/165 and `test_loop_take_survival` pass; full native
2,189 cases — 2,184 passed, 1 skipped, the same three intentional public-reentry
failures. `teensy41-capture-serial` builds with **6,752 bytes** RAM1 headroom
(13 banks, code 425,208).

**Memory:** the ring adds no boot-footprint growth beyond the increment A/B
prewarm already measured device-side (build `91e6b38c…`, PSRAM 5,141,952 →
3,775,872 B, internal heap 352,256 → 290,816 B); this slice only changes gating and
consumption. The staging admission that successor-capture unguarding depends on is
still to be measured on the device in that separate change.

**Still open in 2.3:** causal predecessor/source identity per job and
prepared-companion history qualification, then unguarding successor capture (a
separate change) to close the three public reentry cases.

## Prepared companion history interval qualification — checkpoint, 2026-09-16

The last item of the session/preview/undo/snapshot receipt slice. The Loop-owned
`preparedCompanionHistory_` is borrowed worker scratch that outlives a single grain,
so a successor interval could observe, advance, commit, recycle or discard a
predecessor's in-flight preparation.

- The identity is **`PendingStopAdmission::intervalToken()`** — a value
  (`frozenGeneration` in the high 32 bits, `stopTick` in the low 32), not an object
  address. A copy of an admission made for credit probing or retargeting still
  identifies the same interval, while a reused ring descriptor gets a fresh token
  because `begin` refreshes both fields. Zero is reserved for "no admitted
  interval", i.e. the live session path.
- `Loop::preparedCompanionHistoryOwnedBy(owner)` is the single guard. `Cold` and
  `Prewarmed` hold no receipt, so they stay owner-agnostic and any caller may begin
  (subject to the existing phase machine) or observe the horizon; every in-flight
  phase requires an exact token match.
- `beginPreparedCompanionHistory` records the token; recycle completion and
  `discardPreparedCompanionHistory` clear it. Qualified entry points:
  `stepPreparedCompanionHistory`, `preparedCompanionHistoryReady`,
  `preparedCompanionHistory`, `preparedCompanionHistoryIds`,
  `preparedCompanionCaptureVerified`, `preparedCompanionHistoryCanStep`,
  `preparedCompanionHistoryMatches`, `commitPreparedCompanionHistoryAtomic`,
  `canCommitPreparedCaptureAndCompanions`,
  `commitPreparedCaptureAndCompanionsAtomic`,
  `beginRecyclePreparedCompanionHistory`, `stepRecyclePreparedCompanionHistory`,
  `discardPreparedCompanionHistory`, and `beginCollectOverdubStopUndoPasses`.
  Defaults keep the live-session path source-compatible.
- The deferred stop worker (`Track::processDeferredOverdubStopGrain` binds the FIFO
  head once, so `job.admission` **is** the `stop` reference), the stop-commit
  helpers and the undo-collection receipt all pass `stop.intervalToken()`.

**Verification:** new native `test_prepared_companion_history_is_job_qualified`
(successor token and live-session token are refused for advance/read/commit/recycle,
a stale discard leaves the owner's work intact, the receipt is consumed only by its
owner, and the horizon is reusable — with a new token — once recycling ends). The
`test_track_prepared_stop_publishes_matching_canonical_and_audible_sources` and
`test_companion_history_recycle_denies_source_changes_without_touching_canonical_rows`
fixtures now begin the preparation with the admission that consumes it; every stale
unqualified consumer in them was qualified rather than deleted, because the
qualification is what makes those calls correctly return "not mine". Full native
2,190 cases — 2,185 passed, 1 skipped, the same three intentional public-reentry
failures; `teensy41-capture-serial` builds with **6,752 bytes** RAM1 headroom. The
`Loop` member lives in the externally allocated pool, so the token adds no RAM1
variable cost.

**Note:** the shared worker scratch item from the ownership list is now closed;
`pendingCapturePass_` remains session-tied and is the next thing to qualify if a
second simultaneous job is ever admitted.

## Causal predecessor/source identity — increment A, 2026-09-16

The audit behind this slice found that a deferred stop re-derives its predecessor
source **live** at every consumer turn:

- `Track::beginDeferredOverdubStop` computes
  `PlaybackViewSourceIdentity baseSource{loop.playbackRevision, playbackGeneration,
  loop.publishedSnapshotIdentity()}` for credit sizing only, and throws it away.
- The worker (`Track::processDeferredOverdubStopGrain`, CanonicalOrder/CanonicalPrefix)
  recomputes `baseSource`, `targetSource`, `frozenSource` and `currentSource` from the
  live Loop when the grain runs.
- `TrackPreparedStopCommit::commitPreparedStop` recomputes
  `base{loop.playbackRevision, generation, loop.publishedSnapshotIdentity()}`.
- `TrackPreparedStopCommit::requiresBridgeRefresh` / `advanceAtomicTransfer` compare
  the live published identity through the runtime's frozen admission.

So a successor interval that published while a predecessor finalized could change
what the predecessor resolved against. Requirement (f) of task 2.3 — bind successor
jobs to causal predecessor/source identity and epoch-qualified publication.

**Increment A (landed): the identity is recorded and pinned.** `CaptureStopJob`
carries `causalBase` (`PlaybackViewSourceIdentity`), `causalBaseSnapshot` (an
`ImmutableLoopSnapshotRef` that keeps `causalBase.snapshotIdentity` alive), and
`causalBaseRecorded`. Admission resolves and pins it **before** the raw-range
ownership transfer and denies with `causal_base_unavailable` if a non-null published
identity cannot be pinned (the transfer must have no fallible step after it).
Retirement clears all three so a reused ring descriptor cannot carry a stale
predecessor. New native
`test_deferred_stop_records_and_pins_its_causal_predecessor_source`.

**Increment A changes no behavior:** the consumers above still read live state.
Increment B (next) switches the worker's `baseSource`/`targetSource`/`frozenSource`/
`currentSource` and the atomic commit's `base` to the recorded identity, and decides
the epoch-qualified publication rule for the case where the live revision has
legitimately advanced (the STOPPED-recovery route included). Until then the recorded
identity is evidence, not authority.

Verification for increment A: full native 2,191 cases — 2,186 passed, 1 skipped, the
same three intentional public-reentry failures; `teensy41-capture-serial` builds with
**6,752 bytes** RAM1 headroom (the job descriptor lives in the externally allocated
ring, so the pinned ref adds no RAM1 variables).

## Causal predecessor/source identity — increment B audit and decision required, 2026-09-16

Increment B is "make the recorded identity authoritative". The audit of the
publication path found that this cannot be landed as a pure refactor, because the
resolution side and the publication side currently use *different* sources and the
drift case has no defined outcome.

**What consumes what today.**

| Step | Source used |
|---|---|
| `beginDeferredOverdubStop` credit sizing | live `baseSource` (now also recorded as `causalBase`) |
| worker `CanonicalOrder`/`CanonicalPrefix` (`baseSource`, `targetSource`, `frozenSource`, `currentSource`) | live `loop.playbackRevision` / `publishedSnapshotIdentity()` **at the turn the grain runs** |
| `beginFrozenCompanionBridge(baseSource, targetSource, …)` → `frozenCaptureSourceAdmission` | the same live-derived pair |
| `commitPreparedStop` `base` / `target` | live again, then compared against `frozenCaptureSourceAdmission` (deny bits 16/32); `matchesCanonicalOwner` compares live revision to the canonical owner (bit 128) |
| `requiresBridgeRefresh` / `canSelectFrozenCaptureBridge` | bridge **epoch** only (`frozenGeneration`, `transportCommandEpoch`, candidate, prepared target) — it does **not** compare the live playback revision |

**Finding (latent hang).** If the live `playbackRevision` advances between bridge
construction and the atomic transfer, the bridge epoch checks still pass, so
`requiresBridgeRefresh` is false and nothing rebuilds; `commitPreparedStop` then
denies on bits 16/32/128 on every grain, and the worker stays in `AtomicTransfer`
retrying forever. A source drift in that window is therefore a stop hang, not a
rebuild. This is unreachable today only because the session-exit publication is
deliberately deferred until after the job retires and a successor interval cannot
capture yet; it becomes reachable exactly when successor capture is unguarded
(task 2.3's final slice).

**Decision required — the supersession rule.** What must a deferred stop do when the
published root is no longer the causal predecessor it recorded?

- **(A) Retire without a successor window (recommended).** Keep the recorded base
  authoritative for resolution, and at the atomic transfer require the live root to
  still be the recorded predecessor. If it is not, commit the accepted capture pass
  through the plain/STOPPED-recovery route, publish **no** audible bridge, and record
  a distinct stage (`causal_base_superseded`). Rationale: the only writers that can
  advance the live root inside this window are the interval's own wrap/session-exit
  publication, which already contains its content — publishing a bridge against a
  newer root would duplicate that content and publishing against the stale root is
  the forbidden stale PLAYING publication. Accepted input is preserved either way.
- **(B) Adapt to the live root.** Rebuild canonical inputs and the bridge against the
  current published state and publish that (today's intent). Preserves an audible
  bridge for the interval, but resolves it against a source it did not succeed and
  can duplicate content the newer root already published.
- **(C) Hold and retry** — rejected: that is the latent hang above.

Both (A) and (B) need the epoch check moved/added: the atomic transfer must compare
the live root with the recorded predecessor, and either outcome must be terminal for
the grain. (A) also needs the superseded route named in the stop phases and the
`StopAdmissionContract`, and it must be proven not to omit accepted input
(acceptance case 7) or to publish a stale PLAYING successor (case 8).

Per `ARCHITECTURE_RULES.md` this changes publication semantics and needs a recorded
`DEC-###` entry and user approval before implementation. **Status: option (A)
approved by the user (2026-09-16).**

### Increment B implemented — the recorded identity is the supersession reference

The implementation falsified one premise of option (A) as first written and the
final shape is narrower, so it is recorded here rather than inferred from code:

- **Resolution must stay live.** `RuntimeCanonicalPassInputs::CurrentSource`
  compares its owner **by address** (`&current_->playbackRevision ==
  &owner.playbackRevision`), so an owner built from a *copy* of the revision can
  never match; and the canonical prefix/order/rewrite phases need one coherent live
  epoch (with a recorded base they simply never complete, which stalls the stop
  before staging). Measurement also showed the live revision and root do **not**
  change during a normal finalization: after the first grain both equal the
  recorded admission values. So the recorded `causalBase` is the **supersession
  reference**, not the resolver — the canonical pipeline keeps resolving live, as
  before this increment.
- **The rule is applied where a successor window would be published.** Two checks,
  one at the start of the `CompanionBridge` phase (after staging, before any bridge
  is built) and one at the start of `AtomicTransfer` (for a publication that
  happens later). When `causalBaseCurrent` is false the interval retires the ranked
  preparation credit and commits through
  `TrackPreparedStopCommit::commitSupersededCausalBase`: the accepted capture pass
  is committed (staged in `PrepareTransfer`, so accepted input is preserved) and
  **no bridge is selected or published**, with `SC_ODUB_STOP_STAGE(…,
  "causal_base_superseded")` as the stage evidence. A prepared protected undo entry
  for the committed pass is published in the same turn.
- **The unpublished horizon is restored, not stranded.** `beginRecycle…` refuses
  by design here (its receipt guard requires the recorded revision to equal the live
  one, which is exactly what drifted), so the route calls the new
  `Loop::restoreSupersededPreparedCompanionHistory(owner)`: allocation-free, it
  drops the copied rows/snapshot ids and re-syncs the horizon to the current
  canonical history, returning it to `Prewarmed`. If even that refuses (reserved
  storage no longer fits) the horizon is discarded and the next capture's prewarm
  rebuilds it.
- `PendingStopAdmission::retireSupersededPreparation()` is the explicit "this
  interval publishes no successor window" bookkeeping: it zeroes the ranked,
  canonical and undo-collection credits so `finalizationComplete()` holds and
  DeferredCleanup can retire the job instead of stalling.

**Verification.** `test_deferred_stop_records_and_pins_its_causal_predecessor_source`
now drives the whole approved rule: the record equals the admission-time source and
survives a later publication; after the supersession the stop completes, the
accepted capture pass is present, `bridgeSelected == kNoBridge`, the horizon is back
at `Prewarmed`, and the retired descriptor carries no receipt. Full native 2,191
cases — 2,186 passed, 1 skipped, the same three intentional public-reentry failures;
`teensy41-capture-serial` builds with **6,752 bytes** RAM1 headroom.

**Deviations recorded.** The superseded route publishes the capture pass only: the
companion (pending-change) rows are not published by it, because the newer root that
superseded this interval is expected to already carry them; if that assumption is
ever violated the interval's edits would be dropped, so a future successor-capture
change must re-verify it. The route is not reachable by any current public input
(successor capture stays guarded), so this is forward-looking coverage.

## Successor-capture reentry unguarding — preflight, 2026-09-17

Opens the follow-up change `successor-capture-reentry-unguarding`, which removes the
three admission guards that still refuse a successor interval while a predecessor
finalizes. Preflight items 1.1–1.3 of that change are recorded here.

### Declared capacity (directional)

**START capacity requires the live admission role to be free; STOP capacity requires
the deferred/frozen admission role to be free.** The supported envelope is **depth 2
in capture lifecycle, not depth 2 in deferred-stop queueing**: one finalizing
predecessor plus one independently recording successor. Two simultaneously deferred
capture intervals are **not** supported by this change.

```text
A recording
-> stop A                      A owns the frozen/deferred role
B start                        admitted into the live role
B recording                    B events accepted exactly
B stop while A owns frozen     denied (queue_full / storage_denied); B stays the live
                               recording interval, no events/notes/reservations or
                               stop descriptor consumed
A retires                      frozen role becomes available
B's subsequent stop request    admitted; the same interval transfers into the frozen
                               role exactly once; the live role becomes free
C start                        now legal
```

Resource justification: `RuntimePlaybackViewSet` has exactly two
`CaptureAdmissionStaging` roles (`liveStagingIndex` / `frozenStagingIndex`), punch-out
freezes the live role and exchanges the indexes, and a frozen role is held until its
job retires because it owns the admission rows (reserved closures) the deferred worker
consumes; `runtime->captureStopOwner` additionally admits one job at a time. The stop
ring's four descriptors are therefore not a four-deep *frozen* capacity.

**Decision (review Option 1, 2026-09-17): re-scope the reproducer, do not expand to
N-job queueing.**
`test_button_a_three_intervals_accept_notes_with_service_paused` pressed Button A
three times with zero worker turns and asserted that C captures "while A and B remain
queued" — i.e. two frozen jobs plus a live third interval. It becomes
`test_button_a_successor_stop_denied_until_deferred_role_available`, asserting the
envelope above (A deferred, B start admitted, B capture exact, B stop denied and
non-destructive, B's subsequent stop admitted after A retires, C start then legal).
The original three-deep case is retained as a **documented follow-up capacity
case** — N-job deferred queueing needs additional admission roles, a per-job drain
order and its own PSRAM admission measurement — and is not a skipped red test in the
current suite.

### Reentry-path site inventory (1.1)

Classified by the question the site actually asks. "Keep" means the pending-deferred
exclusion is semantically correct there; "Replace/re-site" means it must stop
standing in for an unrelated question.

| Site | Question | Verdict |
|---|---|---|
| `Track::startOverdubbing` (`TrackOverdubLifecycle.cpp`): `canonicalCaptureOutputAdmitted`, `hasActiveOverdubTransferAdmission`, `canHandoffSessionExitSnapshot`, `runtime->captureStopOwner` | May ingress enter **the live interval**, and can that interval later freeze? | **Replace.** The storage/ownership preconditions stay as *start* preconditions for the successor's own interval; the predecessor-pending implications must not block a successor start (frozen role ≠ live role; the handoff ring holds four). |
| `MidiButtonActions::handleToggleRecord` | May ingress enter the live interval? | **Replace.** Start is admitted; a *stop* that exceeds capacity must surface the explicit denial instead of returning silently. |
| `TrackManager::anyTrackRecordingOrOverdubbing` | Is **cross-track maintenance** excluded? | **Split.** Needs a live-capture-activity predicate and a deferred-maintenance predicate; callers choose. Global redefinition would change unrelated cross-track behavior. |
| `TrackManager::setActiveLoopIndex` (TrackManagerSlotMatrix) | Must this slot change finalize first? | **Re-site.** The live interval's capture matters; a pending predecessor does not need to block the live interval's own slot change. |
| `Track::hasPendingDeferredOverdubStop` (`TrackStateFacade.cpp`) | Definition | **Keep.** The predicate is unchanged; only call-site semantics move. |
| `TrackCaptureInput.cpp` tick-phase projection (`isOverdubbing() \|\| hasPendingDeferredOverdubStop() \|\| …`) | Which projection cycle applies? | **Keep.** A pending predecessor legitimately puts the live interval in the mid-cycle projection. |
| `TrackCaptureInput.cpp` closure-identity role pick (`hasPendingDeferredOverdubStop() ? captureStagingIfPrewarmed() : liveCaptureStagingIfPrewarmed()`, both `appendCaptureNoteOffAtPhase` and `appendReservedCaptureNoteOffAtPhase`) | Which role owns this open reservation? | **Replace.** Resolve in the role that actually owns the open NoteId: a live successor's note must resolve in the live role even while a predecessor job pends (otherwise B's closure is matched against A's frozen reservations). |
| `TrackCaptureInput.cpp` ingress admission (`liveCaptureStagingIfPrewarmed`, lines ~738/~908) | Where does new live ingress write? | **Keep** — already the live role after the dual-role split; must stay that way. |
| `TrackPlaybackWindowBuild.cpp` `invalidatePlaybackMergedMidiEvents` | May caches be invalidated? | **Keep**, with D6: a pending job retargets to STOPPED and must not mutate the live successor's state. |
| `TrackPlaybackWindowBuild.cpp` `tryReleasePlaybackMergedMidiEventsMemory` | May memory be released? | **Keep.** Deferred work retains its generation-qualified scratch. |
| `TrackPlaybackHotPath.cpp` emission gates | May playback emit? | **Keep.** |
| `TrackDeferredMaintenance.cpp` idle-maintenance gates | May idle maintenance run? | **Keep.** |
| `TrackSlotPool.cpp` slot-mutation gate | May the slot pool mutate this slot? | **Keep.** |
| `StorageManager` persistence gates (`PersistenceAdmit`, `Internal`, `MidPassChunkPersist`) | May playback/persistence proceed? | **Keep.** |
