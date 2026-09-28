> **Current closure order (2026-09-28):** follow the
> [shared closure plan](../runtime-working-set-ownership/S4F-CLOSURE-PLAN.md).
> Boot/census progress does not close the live identity protocol. The current Record
> recipe still seeds A and uses global Stop; correct it before qualification.
> A single boot anchor must serve repeated attempts without per-attempt resets.

Firmware dependency: runtime [shared capture finalization](../runtime-working-set-ownership/SHARED-CAPTURE-FINALIZATION.md)
and S4f.16–S4f.22. Preserve overlap acceptance for record and overdub; do not weaken the record
oracle to accommodate today's inline path. Existing completed host fixtures remain valid.

## 1. Pin evidence and production gesture fixtures

Stage owner: HITL session/protocol and existing MIDI button processor. Preconditions: design discovery and adversarial review read; prior dirty driver work preserved. Postcondition: deterministic fixtures expose former failures. Invariants: no runtime behavior change, no device access. Commit boundary: fixture/contract slice.

- [ ] 1.1 Write the first G2 execution packet using `docs/Templates/EXECUTION_PACKET.md`: pin dependency closure, exact observation fields/events, fixed memory/work budgets, transition oracles and baseline linker/timing gates; verify every required field has an existing authoritative owner and no unresolved ownership decision.
- [x] 1.2 Preserve minimal attributed excerpts from the named failed device runs with provenance and expected non-PASS results; add replay cases for seed contamination, wrong slot/track, duplicate stop, stale boot and missing metadata; verify no case passes with the current evidence gaps.
- [x] 1.3 Add controllable-time native fixtures running production `MidiButtonProcessor` and real DROID mappings, covering short/double/triple/long thresholds, interleaved controls, held notes and USB/DROID routing; verify a mutation restoring premature follow-up presses or momentary-button substitution is caught.

## 2. Implement bounded identity observations

Stage owner: existing serial command/capture service and operation owners. Preconditions: packet, decision review and production fixtures complete. Postcondition: read-only live responses plus loss-detectable transition evidence. Invariants: no SD/content scan, blocking flush, new Manager or active-slot authority. Non-goal: runtime acceptance behavior changes. Commit boundary: instrumented firmware plus parser/tests.

- [ ] 2.1 Implement the capture-gated bounded observation request/reply and coherent identity snapshot in existing serial/capture owners; verify malformed/busy requests, disabled capture, partial writes, stalled reader and per-turn work/storage bounds in native tests.
      **2026-09-28 device-confirmed:** the bounded observation serves on demand. A new
      stopped-only `!LIB_OBSERVE_STATUS` diagnostic (emitted directly, bypassing the capture
      ring) reads `phase=3,ready=1,collected=64,emitted=64` after the request, and the managed
      capture returns the complete 66-row `LIBOBS,B…E` family. The earlier "does not serve"
      reading was a ring-flush visibility artifact: the host now drains while it waits and
      verified `closed=True, rows=131, slots=64`. Remaining for 2.1 is the acceptance work
      (malformed/busy, disabled capture, partial writes, stalled reader, per-turn bounds in
      native tests) and the identity-snapshot correlation 2.2 builds on. See the execution
      review.
- [ ] 2.2 Add compact sequenced gesture/accepted-stop/start/switch/publication/retirement evidence at actual owners, including record and overdub interval identity; verify accepted-latch consumption between polls remains provable, required event loss is detectable, and unrelated slot/track events cannot substitute.
- [x] 2.3 Forward allowlisted requests through `capture_session.py` and decode responses once in `serial/protocol.py`; verify one serial owner, one outstanding request, nonce mismatch, partial/coalesced lines, duplicates, delayed delivery and reset/epoch changes using transport fakes.
      **DONE 2026-09-28:** `capture_session.py` gained an opt-in `--command-stdin` channel that
      refuses anything outside the read-only allowlist (and any malformed request) by name, and
      frames an accepted request with two distinct 32-char `!TRACE_FENCE` tokens;
      `serial/protocol.py` owns request formatting, receipt parsing, exact fence-row matching,
      the fenced window and the self-delimiting `LIBOBS,B`…`LIBOBS,E` family; and
      `ManagedCaptureSession` owns `stdin=PIPE`, a one-outstanding-request guard,
      `send_command`/`complete_command`, `fenced_response` and `library_observation`.
      Host suite **23/23** covering every listed case, plus the device-learned 32-char token
      rule. Device-confirmed: the request was forwarded and both fences acknowledged in the
      session log. The `LIBOBS` family did not settle within 60 s on that idle boot, so the
      decoder returned loss rather than an empty census — observation *serving* is task 2.1/2.2.
      [Execution evidence](COMMAND-FORWARDING-EXECUTION.md).
- [ ] 2.4 Run native tests and build `teensy41-capture-serial`; record image/config hashes and code/RAM1 deltas against the packet's floor and fixed buffer ceiling. Do not check off on a skipped fixture or failed resource gate; instrumentation latency measurement remains task 6.2.

## 3. Build response-driven actions and bounded lifecycle

Stage owner: actions, narrow verify wait facade, session/managed capture and reporting. Preconditions: versioned observation protocol available. Postcondition: dependent actions require matching responses and aborts retain evidence. Invariants: flow modules stateless, no duplicate parser, no hidden retry. Commit boundary: reusable host action/lifecycle slice.

- [ ] 3.1 Add correlated gesture and operation waits with deterministic monotonic time; verify send/recognize/accept/execute remain distinct, pending switches block follow-ups, and the old wrong-slot completion mutation fails regression tests.
- [ ] 3.2 Make cancellation suppress every subsequent test MIDI edge, release held notes safely, complete worker cancellation before capture release and preserve persistence-aware teardown; inject timeout/exception at every action boundary and verify partial report, nonzero exit and no post-abort press.
- [ ] 3.3 Use the managed watchdog/metadata path and immutable versioned report schema; verify setup failures, protocol absence, teardown failure and verify-only replay without ports. Resolve the import gate only for the narrow architecture-approved observation-wait facade and run the import-gate suite.

## 4. Prepare and drive both continuous-play recipes

Stage owner: stateless flows, registered scenarios and compatibility wrapper. Preconditions: target authorization manifest and fresh device identity are required run inputs. Postcondition: correct record/overdub fixtures and continuous-play measured windows. Invariants: no reboot, unrelated clear, global Stop in window, or pause hook. Commit boundary: scenario integration slice.

- [ ] 4.1 Implement scoped stopped preparation through production clear gestures, correlated clear completion, RAM/SD census and successful persistence; verify SD residue, wrong target, failed save and unauthorized targets stop before further mutation, and repeats do not reset the board.
      **2026-09-28 partial:** `scripts/hitl/flows/stopped_preparation.py` clears one
      authorized target through its own loop-button long press and classifies only the
      firmware's evidence (`CLEARED`, `ALREADY_EMPTY`, `REFUSED`, `ABORTED`,
      `NOT_SELECTED`, `UNCONFIRMED`); a press shorter than the long-press discriminator is
      refused. It is integrated into the reproducer before each seed and aborts with
      `PREPARATION_FAILED` before any record gesture. Host **42** accepted-sequence tests
      pass. On device the record leg cleared its target with `MIDI: Clear selected slot 4`;
      the overdub leg correctly reported `ALREADY_EMPTY` for the empty target and
      `NOT_SELECTED` (long press switched playback) for the populated unselected one.
      Measured firmware bounds the remaining work: only the *selected* slot clears, a
      short press is a record toggle (it armed and started the transport) and so cannot
      select, and same-button presses inside the tap window degrade a long press. Open:
      the target-selection step, the RAM/SD census, persistence-completion proof and the
      repeat-without-reboot qualification. See the execution review.
      **2026-09-28 census-driven preparation (device-verified):** the read-only census now
      drives preparation. `library_observation.occupied_slots` maps a **complete** observation to
      per-slot occupancy (an incomplete window is unknown, never empty); the driver runs the
      census first, skips the clear for an empty target, selects a filled target before its
      long-press clear, and retries a `REFUSED`/`ABORTED` clear with a bounded wait because that
      is the firmware's explicit "save in flight" refusal. Device run (track 8, A=4, B=5, no
      upload, no manual step): census 64 slots / 61 occupied; slot 5 cleared on attempt 0; slot 4
      aborted once then cleared on attempt 1; both seeds committed; `DROVE_LEG`. Oracle
      `SETUP_INCOMPLETE` with named causes (no fresh boot, 2 ring-shed rows, overdub identity =
      task 2.2) and L2/L3/L4 green. Remaining: a fresh-boot shed-free run of both legs and the
      mixed sequence. See the execution review.
- [ ] 4.2 Implement record recipe with genuinely empty SD-aware A/B, real record mode, switch-carried B start and one stop per interval; verify production dispatch and host scheduling with delayed observations, premature retirement and wrong-mode routes. A populated-A mutation must fail.
- [ ] 4.3 Implement overdub recipe using confirmed seeds and the proven single capture-switch gesture, followed by B's recognized overdub start and stop; verify no separate A stop, no playback re-affirmation and no wait for predecessor retirement in the critical window.
- [ ] 4.4 Register both scenarios and route the standalone CLI through the same runner; verify legacy argument translation, explicit unsupported combinations, scenario listing, identical verdicts via both entry points and unchanged base/edit_full behavior.

## 5. Complete acceptance oracle and regression gates

Stage owner: verify/reporting. Preconditions: complete per-phase identities and action trace. Postcondition: complete PASS conjunction and diagnostic non-success classifications. Invariants: existing runtime/content/timing gates retained, no guessed identity or waived event loss. Commit boundary: oracle/corpus slice.

- [ ] 5.1 Replace global counters/human-log inference with phase-scoped correlated input, accepted obligation, execution and retirement predicates for both modes; verify every missing/reordered/duplicate/wrong-identity edge, legacy deny-plus-latch versus true refusal, overlap miss, transport stop and clock change yields the specified non-PASS result.
- [ ] 5.2 Integrate existing content parity and timing verifiers plus independent shutdown result; verify a failing content, timing or teardown result vetoes otherwise complete acceptance. Replay the preserved failure corpus and synthetic complete chains with deterministic expected verdicts.
- [ ] 5.3 Run focused Python suites, architecture/import gates and full `pio test -e native`; record mutation results for former queue, timing and attribution bugs. Keep host success distinct from hardware qualification.

## 6. Qualify on hardware and close evidence

Stage owner: existing managed HITL entry point. Preconditions: successful identified capture-serial build, registered/reused stimulus discovery, explicitly authorized target set, pinned clock/display configuration, and all preceding gates. Postcondition: six qualifying bundles on one boot or truthful incomplete status. Invariants: no manual golden pass, no automatic reboot, no silent profile change or unbounded retries. Commit boundary: measured evidence and documentation closeout.

- [ ] 6.1 Run `.venv/bin/python scripts/host_midi_hitl.py stimulus list`, select existing stimuli or declared temporary variants, and pin a finite density-profile/attempt budget in the execution packet; verify authorization and read-only initial census before any clear or seed.
- [ ] 6.2 Measure bounded response latency and instrumentation overhead on the identified display/clock configuration under the supported working set; compare against existing timing gates and the packet budget. Stop qualification if observations perturb the required behavior or required evidence is lost.
- [ ] 6.3 Run three consecutive complete PASS attempts per record and overdub leg without manual correction or reboot, with response-confirmed scoped preparation between attempts; also verify the mixed record-A → overdub-B overlap sequence from runtime design Revision 23; preserve every failed attempt and reset consecutive counts on failure. Verify same boot, complete content/timing/identity chains and persistence-aware shutdown; a missed natural window stays NOT_EXERCISED.
- [ ] 6.4 Link verified immutable bundles to S4f.13 — accepted-sequence acceptance and check it off only if its full runtime gate is met; update architecture/usage docs, PROJECT_STATE, CURRENT_WORK, deliverable tracking and decision status to match shipped behavior. Run strict OpenSpec validation for both changes; archive neither while required evidence is missing.
