## Dependency resolved — shared firmware finalization is the existing requirement (2026-09-27)

The runtime intent already requires record A to finalize while a successor captures (runtime
design Revision 23 item 6). The inline record route is an implementation gap. The user requests
its firmware contract; [shared capture finalization](../runtime-working-set-ownership/SHARED-CAPTURE-FINALIZATION.md)
and runtime tasks S4f.15–S4f.22 now specify the missing implementation. Separate weaker record
and overdub oracles are not adopted. This supersedes the earlier pending-choice block.

Keep the full overlap oracle for both modes, and exercise the existing mixed record-A →
overdub-B sequence. Common-mode instrumentation must follow that contract's identity/lifetime
fields. The preceding host gesture/corpus work stays valid; device acceptance depends on the
firmware gates. Specifying the contract does not claim the firmware has changed.

## Context

See [proposal.md](proposal.md) for motivation and scope. This is the automation prerequisite for S4f.13 — accepted-sequence acceptance, not evidence that that device gate passes. The user specifies DROID MIDI controls, no transport Stop inside the acceptance sequence, no manual reference pass, and repeatable clearing without reboot. Stopped initial preparation is allowed.

### Grounded discovery (G0)

VERIFIED call paths and evidence:

- `scripts/hitl_accepted_sequence.py::_run_plan` owns raw gestures, sleeps, seed setup and human-log waits outside the modular framework. It seeds A for the record leg; `MidiButtonActions::handleToggleRecord` then starts overdub on populated A. Its global Stop also breaks the intended continuous-play window.
- `MidiButtonActions::handleToggleRecordForSlot` handles capture-switch before SD-aware playback-switch. A separate stop changes the branch. Selected slot is not proof of the active slot. `TrackManager::slotHasLoopContent(..., true)` includes SD payloads; committed-pass emptiness alone is insufficient. “Switch gesture” is an intent log, not completion.
- `MidiButtonProcessor` classifies note-on/off through per-channel/note timing. Short gestures wait for a tap window (default 300 ms), triple classification can require 400 ms, and ordinary long press is 600 ms. `_send_short_press` only sends edges; it provides no recognition acknowledgment. `GS` is emitted at recognition, but is not Tier-A and may be shed.
- `Track::latchAcceptedStop` retains an accepted obligation for deferred execution. The overdub path can emit legacy denial vocabulary before latching; strings alone cannot decide rejection. `RECA`/`RECS` do not identify overdub intervals. The current oracle explicitly cannot pass its one-press identity link.
- `captures/s4f13-od-20260927_015145`, `s4f13-od3-20260927_015939`, `s4f13-od4-20260927_020114` and `s4f13-carrier-20260927_020437` contain queued/reaffirmed switches or incomplete evidence. The later OD attempt has two starts but a short predecessor window. These are failure-corpus inputs, not golden runs.
- `StorageManager::processHitlSerialCommands` already owns serial command dispatch; its line buffer is 48 bytes. `capture_session.py` owns the serial descriptor. `!TRACE_FENCE`, `!STOP_STATUS` and content dumps are stopped-only; they cannot serve as live acceptance observations. `!LIB_OBSERVE` supports bounded library attribution.
- `run_with_managed_capture` provides watchdog/metadata behavior that direct `ManagedCaptureSession` use omits. `DeviceSession` owns persistence-aware shutdown. Reports must survive setup exceptions and timeout paths, not just expected driver aborts.
- [HITL architecture](../../../docs/Authority/Architecture/HITL_ARCHITECTURE.md) already assigns actions, protocol decoding, observation waits, sessions and reporting. The import gate currently rejects the documented narrow observation-wait facade; fix that inconsistency without allowing verifier implementation imports throughout flows.

ASSUMPTION TO MEASURE: a declared, legal fixture provides enough natural finalization overlap for a recognized B stop while A still owns the relevant resource. If it does not, return NOT_EXERCISED and preserve the evidence. Hardware scheduler delays or pause hooks are excluded.

UNKNOWN but bounded: observation latency, telemetry overhead and repeatability on the identified working display configuration. Measurement failure blocks qualification; it does not permit weaker acceptance.

## Goals / Non-Goals

**Goals:** a composable, response-driven driver, production gesture recognition tests, trustworthy interval attribution, scoped same-boot setup, immutable replayable reports, and repeatability qualification.

**Non-goals:** new runtime state owners, new transport behavior, a general scenario engine, rewriting all legacy baselines, SD quarantine, automatic reset/upload recovery, display/DMA repair, or artificial hardware overlap. Native controllable-time fixtures may hold dependencies for deterministic tests; hardware acceptance must use natural execution.

## Decisions

### 1. Use existing framework owners

Register separate record and overdub scenarios in the existing registry. Scenario modules declare intent; stateless flows compose actions and narrow observation waits. All mutable run state and cancellation live in `HitlSession`/`ActionContext`. `serial/protocol.py` decodes versioned observations once; `verify/` owns predicates and the offline oracle; reporting owns output. The standalone driver becomes an argument-compatible wrapper to the registered runner, with unsupported legacy combinations rejected explicitly.

Do not migrate `base` or `edit_full` sequences as part of this work. The older `hitl-cli-rebuild` phase lock is unchanged; this sibling owns the additional scenario. Permit only the documented wait facade in the import gate. Alternative rejected: another standalone scheduler would preserve the duplicated timing/parsing problems.

### 2. Distinguish input, acceptance and execution

Each action records host monotonic send/release times and a unique local action ID. Match it to a device gesture sequence with channel, note, press classification, device timestamp and the current run fence. Do not infer one press from one stop-completion row. There is one outstanding gesture per affected control until its recognition is known; unexpected physical input invalidates attribution rather than being absorbed into the script.

Operation predicates name track, slot, interval and execution generation. Accepted intent, bound execution, capture start, switch completion, publication and retirement are distinct edges. A start requires actual live interval identity and the intended mode. A pending slot switch blocks subsequent capture gestures until matching completion; timeout aborts with no second press. Never accept another slot’s playback-start row.

Pulse widths and firmware-defined classification thresholds remain legitimate timed input parameters. They are pinned to the firmware configuration and exercised with production processor tests. Sleeps are not completion evidence. Observation deadlines are failure bounds, not instructions to advance. Do not serialize unrelated controls merely to add safety delays.

### 3. Add bounded read-only evidence through existing owners

Use the existing capture child for allowlisted observation requests; never open a second serial descriptor. Add a capture-gated `!HITL_OBS <16-hex-nonce> <track0> <slot0>` request, which fits the existing command line limit. One request is outstanding per session. The reply is versioned and correlates nonce, capture/boot identity, device time, transport epoch, active/selected/pending slot, mode, interval and accepted-stop identity. Host run IDs map to the nonce; firmware need not store host strings.

Read scalar state from existing owners on the main loop, copy a coherent observation, then emit bounded fragments carrying one observation sequence. No scan of passes, notes, slots, SD, or PSRAM; no blocking flush, heap allocation, or control mutation. Extend the existing serial/capture service with a fixed response buffer and `availableForWrite()`-bounded progress. Limit command ingestion and response work per turn; a stalled host must not stall MIDI. Reject malformed/out-of-range requests and report busy rather than accumulate a queue.

Snapshots cannot prove short-lived edges. Add compact capture-gated events at the actual recognition/acceptance/execution/retirement owners, carrying a sequence and the same interval identity. Do not promote a large stream blindly to a blocking Tier-A path. Detect loss using sequence continuity/overflow diagnostics; missing required edges prevent PASS. Reliable response delivery and event loss detection are separate obligations. Existing `GS` can be reused only with its attribution and loss contract made explicit.

Fixed storage must be budgeted in the execution packet before implementation (proposed reply storage ceiling 512 bytes, each encoded fragment at most 192 bytes; no retained content). Record linker/code/RAM1 delta against the current build and existing project floor; FLASH-resident cold helpers where appropriate. No new Manager and no second active-slot authority. `activeLoopIndex` remains authoritative. If the budget fails, simplify encoding before adding capacity. Polling rate is bounded and measured under the maximum supported working set; instrumentation stays gated off outside capture.

Alternative rejected: parsing human logs cannot prove identity, while stopped-only probes or synchronous dumps would alter the tested execution.

### 4. Separate preparation, measured execution and final verification

Preparation accepts explicit track/slot targets and a run-scoped authorization manifest. Obtain library attribution before writing. Clear only those targets using the real mapped DROID long-press path. Await recognized clear, matching clear completion, RAM emptiness and successful deferred persistence; then re-observe the SD-aware inventory to rule out old payload resurrection. A failed save or unavailable identity leaves SETUP_INCOMPLETE; do not reboot, retry clear blindly, quarantine, or substitute a different slot. Unrelated content must retain its inventory identity.

Prepare at stopped transport when needed, without resetting the board. Record and overdub fixtures differ: record A/B must be valid record targets in the SD-aware sense; overdub A/B must have confirmed committed seed content. Preserve any separately declared playback support fixture within the authorized targets. Starting and switching captures must leave global transport running. No assumption that a particular slot number is always empty.

Once PLAYING and a fresh observation fence are confirmed, begin the measured window. Clear, seed, global Stop, load, undo and teardown actions are forbidden inside it. Initial preparation and final stopped-only content dumps are outside the window and are reported explicitly. Consecutive runs retain the same boot identity; they may enter stopped preparation between windows as the user permits, without rebooting. A device disconnect or reset invalidates the repeat campaign.

### 5. Define the two recipes by transitions

**Record:** prepare empty record targets; establish running transport; start a real record Capture on A and observe its mode/interval before sending notes. A single B slot gesture while A captures must accept A’s stop and carry the record start to B. Observe the committed switch and B’s new record interval. Send B content, then issue one legal B stop gesture. Prove B’s stop acceptance occurs while A’s relevant predecessor resource remains owned and later executes exactly once. If the branch produces playback or overdub, stop the test as wrong setup/route, not a successful substitute.

**Overdub:** seed B and A, confirm both durable, establish running transport and start A’s overdub. One B slot gesture accepts A’s stop and switches. After matching B activation, one record-control gesture starts B’s overdub; after its actual start, inject content and issue one B slot stop gesture. No separate A stop before the switch. Prove the same predecessor-overlap and exact-once obligations with overdub interval identities.

Setup waits must settle fixtures. Critical-window waits await the minimum causal response needed for the next legal gesture; they must not wait for predecessor retirement. Device timestamps establish the overlap, not host receive times. If the predecessor retires first, report NOT_EXERCISED. Predeclare a small bounded set of density profiles using existing stimulus variants; never grow sleeps, density or retry count until something passes. A changed profile starts a new identified attempt and keeps all earlier results.

### 6. Make verdicts truthful and replayable

Record a phase boundary and identity for every predicate. Exclude seeds and prior legs from acceptance counts. Require matched input → recognition → accepted obligation → execution → publication/retirement, continuous transport, content parity and the existing timing gates. A compatibility `deny` diagnostic followed by the same interval’s accepted latch and execution is not itself a rejected input; missing acceptance evidence is insufficient evidence, and a true rejection remains FAIL. Document this reconciliation with DEC-078 — accept-and-latch in the parent task; do not silently exempt all denial rows.

Outcomes: SETUP_INCOMPLETE for invalid fixture/identity, NOT_EXERCISED for a clean missed overlap, INSUFFICIENT_EVIDENCE for lost/stale/ambiguous required observations, ACCEPTED_SEQUENCE_FAIL for an observed contract violation, PASS only for the complete conjunction. All non-PASS results exit nonzero. Shutdown result remains an independent field and can veto overall success.

Bundle firmware image/build/config/stimulus hashes, host revision plus dirty diff hash, boot/capture/run identity, target authorization, initial/final inventories, action trace, observation log, predicate evidence references, content comparisons, timing and teardown. Runtime bundles are immutable; verify-only requires no ports. Replaying the same evidence yields the same verdict. No fresh-boot-banner requirement for a same-boot run: use fresh nonce and sequence boundaries against a pinned image and unchanged boot identity instead.

### 7. Bound failure and qualify repetition

Use the managed runner watchdog and a shared cancellation latch checked at every action edge. On timeout or exception prevent further MIDI, release any held test notes safely, write a partial report, and invoke the existing persistence-aware shutdown while serial remains open. Do not close capture underneath an active worker. Join/cancel work before release; teardown failure stays visible. The default managed deadline remains 360 seconds; each action also has a finite deadline. A failure does not trigger automatic reset or another press.

Offline qualification includes deterministic monotonic time, serial delays/reordering/duplicates, missing acknowledgments, cancellation at each action boundary, wrong identities, SD residue, failed persistence and the preserved device failures. Native fixtures must run the actual `MidiButtonProcessor` with production button mapping and controllable time; the existing momentary-button clear fixture cannot prove short/double/long classification. Test the same-button boundaries and interleaved controls on both sides of thresholds, including note-off and USB/DROID dispatch parity.

Hardware qualification requires three consecutive complete PASS results per leg on the same boot, identified display/clock configuration and authorized targets, with no operator correction. Record all attempts; any failure resets that leg’s consecutive count. This demonstrates bounded repeatability, not universal timing guarantees. Use registered/reused stimulus discovery first; temporary variants are not promoted without the user accepting the reproducer.

## Risks / Trade-offs

- Response arrival may be later than the natural overlap → minimal causal waits, device timestamps, bounded observation overhead and NOT_EXERCISED; never manufacture a window.
- Current image has little code/RAM1 slack → fixed buffers, cold code placement and build-floor gate before hardware.
- Read-only telemetry can still change timing → capture-gated bounded service, compare MIDI/timing budgets with and without observations on the same configuration.
- Clearing may complete in RAM before persistence → await successful durable completion and an SD-aware census; preserve serial through shutdown.
- Corrected attribution will make old apparent passes fail → retain old bundles and version reports; never backfill identities from guesses.

## Adversarial review (G2 design review)

**BLOCKER resolved in contract — snapshot-only evidence misses transient acceptance.** The latch can be consumed between polls. Decision 3 requires sequenced owner events as well as snapshots; replay with the acceptance event removed cannot pass.

**BLOCKER resolved in contract — “wait for stable” can erase the acceptance window.** Decision 5 distinguishes fixture stabilization from critical causal responses and makes missed overlap NOT_EXERCISED. Native scheduling and real timestamps must prove the boundary.

**BLOCKER resolved in contract — populated record fixture takes overdub route.** Decision 4 separates record/overdub RAM/SD preconditions; production dispatch tests pin actual mode.

**TIGHTENING incorporated — cancellation worker outliving capture.** Decision 7 makes cancellation suppress all later action edges and requires worker completion before descriptor release. Inject timeout during every wait and pulse.

**COVERED — alternate/compatibility paths:** standalone wrapper uses the same registered scenario; no alternative parser. **COVERED — asynchronous completion, stale references and overlapping operations:** scalar snapshots plus owner event identity, no borrowed content pointers; interval/track/slot/epoch correlation. **COVERED — slot/track switches and record/overdub reentry:** distinguish selected/active/pending and predecessor/successor; unexpected track change fails attribution. **COVERED — transport/clock transitions:** continuous transport and pinned clock source/tempo are window invariants; changed epoch/source invalidates evidence. **COVERED — undo, clear, load, save and persistence in flight:** preparation only, positive persistence completion, no implicit content recovery. **COVERED — long loops and maximum working set:** observation cost constant in content length, bounded main-loop service, existing timing gates unchanged. **COVERED — RAM1/PSRAM and lifetime:** fixed scalar telemetry, no ownership transfer or PSRAM traversal. **COVERED — synchronous work/MIDI starvation:** no flush/SD read in live probe and per-turn bounded work. **COVERED — display/playback generations:** pinned display configuration and authoritative runtime identities; no claim of visual correctness from display counters. **COVERED — full validate/second activation authority:** neither introduced.

G2 REVIEW: PASS WITH TIGHTENING. Design blockers above are resolved in this document. Each implementation slice still needs its own execution packet, controllable production-path fixture and measured resource results before checkoff. This review is not a hardware or implementation pass.

## Migration Plan

Implement protocol/evidence and deterministic regression support before replacing gesture sequencing. Integrate the registered scenarios and wrapper next, then qualify on hardware. Keep legacy entry-point compatibility but fail explicitly if the required observation protocol is absent; no fallback to sleeps. Rollback is selecting the prior harness/image outside a run, retaining evidence and leaving acceptance open, never an automatic reset or erased fixture.

## Open Questions

Only measured values remain: observation latency/overhead and which predeclared density profile exercises the natural overlap reliably. A failed measurement blocks the corresponding gate; changing the runtime acceptance contract requires a separate design decision.
