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

- [x] 1.1 Write the first G2 execution packet using `docs/Templates/EXECUTION_PACKET.md`: pin dependency closure, exact observation fields/events, fixed memory/work budgets, transition oracles and baseline linker/timing gates; verify every required field has an existing authoritative owner and no unresolved ownership decision.
      **2026-09-30 — written.** [G2-EXECUTION-PACKET.md](G2-EXECUTION-PACKET.md) is the change-level packet: it pins the
      fields every slice shares — dependency closure (design §Dependency resolved + the runtime change's
      `SHARED-CAPTURE-FINALIZATION.md`), the **observation vocabulary** (each row family with its emitting owner and the
      one place that decodes it), the **budgets** (managed ceiling 360 s / cancellation grace 30 s / per-action
      deadlines / the RAM1 free-for-locals floor **840** with the bank-arithmetic rule as the mechanism / the fixed-buffer
      ring with "loss is detectable" as a gate), the **gates** (`pio test -e native`, the `teensy41-capture-serial` build,
      upload only on approval, the mandated HITL entry points) and where each **transition oracle** is fixed (oracle
      links, recipe transitions in design decision 5, the S22 latch transitions, the 3.3 bundle rule) — then audits every
      required field for an owner and carries one compact section per remaining slice (4.1–4.4, 5.1–5.3, 6.1–6.4) with the
      template's fields. **Audit result:** all fields owned except one, which is now a **recorded decision** rather than
      an open question — the run-scoped **authorization manifest** belongs to the stimulus registry (it already declares
      each profile's recipe and limits, and `stimulus list` is the mandated discovery path), with the drivers' explicit
      CLI targets kept as the compatibility path and the record of what a run was given; 4.1 implements it. The per-turn
      **observation work number** is deliberately unset: its mechanism owner exists (capture-gated bounded service) and
      6.2 is the measurement gate that gives it a value. Landed slices keep their own artifacts, linked from the packet
      rather than restated. Documentation only: no code, firmware, build or device action; `openspec validate --strict`
      and `git diff --check` clean.
- [x] 1.2 Preserve minimal attributed excerpts from the named failed device runs with provenance and expected non-PASS results; add replay cases for seed contamination, wrong slot/track, duplicate stop, stale boot and missing metadata; verify no case passes with the current evidence gaps.
- [x] 1.3 Add controllable-time native fixtures running production `MidiButtonProcessor` and real DROID mappings, covering short/double/triple/long thresholds, interleaved controls, held notes and USB/DROID routing; verify a mutation restoring premature follow-up presses or momentary-button substitution is caught.

## 2. Implement bounded identity observations

Stage owner: existing serial command/capture service and operation owners. Preconditions: packet, decision review and production fixtures complete. Postcondition: read-only live responses plus loss-detectable transition evidence. Invariants: no SD/content scan, blocking flush, new Manager or active-slot authority. Non-goal: runtime acceptance behavior changes. Commit boundary: instrumented firmware plus parser/tests.

- [~] 2.1 Implement the capture-gated bounded observation request/reply and coherent identity snapshot in existing serial/capture owners; verify malformed/busy requests, disabled capture, partial writes, stalled reader and per-turn work/storage bounds in native tests.
      **2026-09-30 — the correlation this task deferred to 2.2 is now in place.** The bounded observation still
      serves on demand (device-confirmed 2026-09-28) and `test_library_observation_session` covers the
      incremental/bounded lifecycle with its per-slot bounds (O01–O10: a permanently waiting request still
      yields a diagnostic, generation/epoch changes invalidate or restart, denied SD admission degrades fields
      not the report, union of both content representations) plus the `busy` slot-payload refusal. What is
      **not** yet mapped clause-by-clause to this task's own list is malformed-request handling *inside the
      firmware handler*, the disabled-capture path and the stalled-**reader** case (the malformed/busy request
      refusals that do exist are host-side, in `capture_session.py`, pinned by task 2.3's suite). The
      identity-snapshot **correlation** the task hands to 2.2 is landed on both sides: the oracle reads the
      intervals' own rows (2026-09-30 host note under 2.2) and the latch's own sequenced rows exist with
      counters (2026-09-30 firmware note under 2.2).
      **2026-09-28 device-confirmed:** the bounded observation serves on demand. A new
      stopped-only `!LIB_OBSERVE_STATUS` diagnostic (emitted directly, bypassing the capture
      ring) reads `phase=3,ready=1,collected=64,emitted=64` after the request, and the managed
      capture returns the complete 66-row `LIBOBS,B…E` family. The earlier "does not serve"
      reading was a ring-flush visibility artifact: the host now drains while it waits and
      verified `closed=True, rows=131, slots=64`. Remaining for 2.1 is the acceptance work
      (malformed/busy, disabled capture, partial writes, stalled reader, per-turn bounds in
      native tests) and the identity-snapshot correlation 2.2 builds on. See the execution
      review.
      **2026-09-28 review follow-up — the drain reaches firmware, and SD-only slots are
      populated:** `ManagedCaptureSession.drain_capture_ring` wrote a bare `!TRACE_DRAIN`, which
      `capture_session.py` refuses as `malformed request` and never forwarded (the sender still
      reported success), so a completed observation could stay invisible; the drain is now a
      framed request with fresh 32-char fence tokens and `!TRACE_DRAIN` is allowlisted
      (read-only ring flush). Separately, `library_observation.observation_window` classified
      occupancy on `has_committed_content` alone, so a verified SD payload with `k=0` read as
      empty and scoped preparation skipped the clear; occupancy is now the tri-state
      `slot_content_state` (populated if either representation is present, empty only when both
      were observed absent, `None` otherwise) and the census report preserves `None`.
      `test_hitl_command_forwarding` **26/26**, `test_accepted_sequence` **52/52**,
      `test_library_observation_classifier` **18/18**.
      [Closure-boundaries review follow-ups](../runtime-working-set-ownership/S4F18-CLOSURE-BOUNDARIES-EXECUTION.md).
- [~] 2.2 Add compact sequenced gesture/accepted-stop/start/switch/publication/retirement evidence at actual owners, including record and overdub interval identity; verify accepted-latch consumption between polls remains provable, required event loss is detectable, and unrelated slot/track events cannot substitute.
      **PARTIAL — both halves landed, three clauses still owe device evidence (2026-09-30).** *Read half:* the
      accepted-sequence oracle decides the overdub leg's L1/L5/L6/L7 and a new `overdub_interval_identity` link
      from the intervals' own rows (start admission `DIAG,reentry,admit`, accepted-stop binding
      `DIAG,reentry,bound`, execution `DIAG,stop_state`, retirement `DIAG,canonical_release`, plus the button
      action's per-interval `Loop N:` request rows), and it re-graded `captures/s4f22-dec096-od-13` to
      `ACCEPTED_SEQUENCE_PASS` with no device action. *Write half (firmware, instrumentation only):* one
      compact Tier-A one-shot row per accepted-latch transition
      (`DIAG,latch,<accept|repeat|wait|park_stall|consume>,<track>,<slot>,<interval>,<destination>,<outcome>`)
      with the latch's own identity, `RecordFamilyStats` counters, a Tier-A classification, and a read-back by
      the existing stopped-only `!CAP_IDENTITY_STATS`. **Owed:** (a) the `accept` → `consume` rows are not yet
      exercised on hardware — the family reads back at `entered=0` and every attempted recipe either admitted
      both stops or aborted in host tooling (packet §6/§6.1), so the remaining route is host tooling, not
      firmware; (b) sequenced *gesture/switch* rows (a slot press still leaves only its in-ring `DISP` row and
      a human `Loop N:` line); (c) the counters are now read back by the leg itself and **gated** by the oracle
      (`family_accounting`: a refused append or a displaced row inside the capture fails the leg's setup class,
      a pre-capture loss does not, and a capture with no read-back adds no check -- see packet §7), but that
      read-back is not yet part of the on-demand *observation poll* (`LIBOBS`), so consumption between polls
      still rests on the ring rows plus the read-back rather than on one snapshot.
      **2026-09-30 (host) — the "required event loss is detectable" clause is now enforced, not just possible.**
      The family's counters were counted and read back but unused: the managed retrieval's parser matched only
      `(RECA|RECS)` and required exactly the record trio (so `DIAG,latch` escaped its refusal/displacement gate,
      and a `[^,]+` family name mis-filed `DIAG,latch` as `DIAG`), its regression check assumed every family had
      a baseline, and the accepted-sequence driver never asked for the read-back at all. All three are fixed, and
      the oracle's new `family_accounting` check gates the verdict on the leg's own accounting. Evidence:
      `test_accepted_sequence` **90/90** (7 new, including one pinning a verbatim device row),
      `test_managed_record_drain` **21/21**, 15 neighbouring Python suites green from the repository root, full
      native **2,643 / 2,636 succeeded / 7 skipped / 0 failed** on an unchanged firmware tree, four mutations
      red-proved and restored byte-identically, and `captures/s4f22-dec096-od-13` re-graded unchanged. Packet §7.
      **2026-09-30 (host; the read half of this task's "including record and overdub interval identity").** The
      overdub leg already *has* identity-bearing rows at its owners -- start admission `DIAG,reentry,admit`, the
      accepted-stop binding edge `DIAG,reentry,bound` (S4f.3), the execution `DIAG,stop_state` 6→7 / 7→5|1, the
      retirement `DIAG,canonical_release`, and the button action's own per-interval `Loop N:` request rows -- so
      `scripts/hitl/accepted_sequence_probe.py` no longer force-unknowns L1/L5/L6/L7 on that leg: each link is
      decided on the interval's own rows, the binding must name the start's interval, two intervals may not share
      one identity ("unrelated slot/track events cannot substitute"), and the new `overdub_interval_identity` link
      is `ok` only with both intervals' start + accepted stop + retirement. Also verified in the same pass: the
      families are Tier-A, so a *lost* one is already the `overflow_free` setup gate rather than a silent gap, and
      absence of an overdub row reads `unknown` (a gap to fix) while a contradiction reads `FAIL`. Evidence:
      `scripts/test_accepted_sequence.py` **83/83**, `scripts/test_accepted_sequence_corpus.py` **3/3** with
      unchanged expectations, 18 neighbouring host suites **361/361**, four mutations red-proved and restored
      byte-identically, a new line-by-line device excerpt fixture
      (`scripts/hitl/fixtures/accepted_sequence/s4f22-dec096-od-13-excerpt.{log,json}`), and a device-free re-grade
      of `captures/s4f22-dec096-od-13` to `ACCEPTED_SEQUENCE_PASS`. **Still open in this task:** the sequenced
      gesture/switch/publication evidence and the "accepted-latch consumption between polls remains provable" half
      -- the `ODUB,stage` / `ODUB,stop` families and the `#CAP,ST,Track,…` edges still carry no interval or track
      identity, and nothing yet ties a *consumed* accepted latch to the poll that could have missed it. That half
      needs firmware rows (upload + device verification) and is deliberately not done here.
      **2026-09-30 (firmware, host-verified) — the latch's own sequenced identity evidence landed.** The
      remaining half above is now built as instrumentation only, no behaviour change: `Track::latchAcceptedStop`
      and `Track::consumeAcceptedStopObligation` (now taking the consumption *reason*) and the S2b.6 park emit
      one compact Tier-A one-shot row per latch transition through `SC_ACCEPTED_LATCH` —
      `DIAG,latch,<accept|repeat|wait|park_stall|consume>,<track>,<slot>,<interval>,<destination>,<outcome>` —
      carrying the identity the latch holds, so a row can be attributed on a multi-track run instead of inferred
      from position. `repeat` is the documented same-identity no-op, and `consume` names the reason
      (`admitted`, `superseded`, `sync_stop`, `transport_stop`), which is exactly what makes an
      accept-plus-consume pair inside one poll gap **provable after the fact**. Loss is detectable: the family
      has `RecordFamilyStats` counters and is reported by the existing stopped-only `!CAP_IDENTITY_STATS` as
      `RING,record_stats,DIAG,latch,…`. Evidence: `test_continuous_capture` **106/106** (two new fixtures: the
      accept → repeat → wait → one `consume(admitted)` chain on one identity, and exactly-once consumption) and
      `test_capture_ring` **12/12** (family counters, the read-back, the identity in the row text, a refused
      append counted, and the Tier-A classification); full `pio test -e native` **2,643 cases / 2,636 succeeded /
      7 skipped / 0 failed**; `teensy41-capture-serial` **SUCCESS** with the RAM1 budget gate passing on an
      unchanged line (`variables 96,192 / ITCM 393,216 (code 391,288 + padding 1,928) / locals 34,880`, floor
      840); five mutations red-proved and restored byte-identically. Packet and transition oracle:
      [S22-LATCH-EVIDENCE-PACKET.md](S22-LATCH-EVIDENCE-PACKET.md). **Still owed:** the device leg (an upload,
      then a retained stop showing `accept` → (wait) → `consume(admitted)` with one identity), the sequenced
      *gesture/switch* rows (a slot press still leaves only its in-ring `DISP` row and a human `Loop N:` line),
      and tying the counters to a *poll* the way the on-demand observation request does.
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
      **2026-09-28 review follow-up:** the ring drain now uses this same framed allowlist
      (`!TRACE_DRAIN`), because the bare command the drain previously wrote was refused as
      malformed and never reached the device. `test_hitl_command_forwarding` **26/26** pins the
      framed drain and the allowlist.
- [x] 2.4 Run native tests and build `teensy41-capture-serial`; record image/config hashes and code/RAM1 deltas against the packet's floor and fixed buffer ceiling. Do not check off on a skipped fixture or failed resource gate; instrumentation latency measurement remains task 6.2.
      **DONE 2026-09-30 (final tree, after section 2's instrumented firmware landed).** `pio test -e native`
      **2,643 cases / 2,636 succeeded / 7 skipped / 0 failed** (the 7 skips are all environment/opt-in gated and
      none is in this section's scope: the `test_serial_midi_input_read` suite needs a device, the three
      offline-recovery suites skip their two `Isolated recovery ABI build required` cases each, and
      `test_copied_slots_decode` / the parked `175026` diagnostic need their own env/opt-in). Section 2's own
      suites are green — `test_capture_ring` 12/12, `test_continuous_capture` 106/106,
      `test_library_observation_session` green. `pio run -e teensy41-capture-serial` **SUCCESS**, RAM1 budget
      post-action on an **unchanged** line: `variables 96,192 / ITCM 393,216 (code 391,288 + padding 1,928) /
      locals 34,880`, approved floor 840 — delta zero, so no re-base; FLASH `code 1,064,912 / data 96,376`.
      Image identity for the record: `firmware.hex` sha256 `577841d6…`, with the new emitter confirmed present
      in the ELF (`arm-none-eabi-nm`: `DebugSessionCapture::interactionLatchTransition`). `git diff --check`
      clean; `openspec validate --strict` valid for this change and its three siblings. Latency measurement
      stays task 6.2 as this task says.

## 3. Build response-driven actions and bounded lifecycle

Stage owner: actions, narrow verify wait facade, session/managed capture and reporting. Preconditions: versioned observation protocol available. Postcondition: dependent actions require matching responses and aborts retain evidence. Invariants: flow modules stateless, no duplicate parser, no hidden retry. Commit boundary: reusable host action/lifecycle slice.

- [x] 3.1 Add correlated gesture and operation waits with deterministic monotonic time; verify send/recognize/accept/execute remain distinct, pending switches block follow-ups, and the old wrong-slot completion mutation fails regression tests.
      **2026-09-30 — done, both halves: the layer and the two device drivers that consume it.** `scripts/hitl/actions/correlation.py`
      owns the four stages (`Stage`, `CorrelatedGesture`, `StageRules`, `Wire`, `PendingSwitchGate`): only the next pending
      stage advances, so an execute-only row cannot satisfy recognition or acceptance, and a stage the caller declared no
      rule for is not awaited at all; every acceptable row must carry the identity **its own wire family** uses
      (`Loop 4:` vs `DISP,3,` vs `,RECA,3,`), so a foreign slot's row cannot substitute; a row at or below the device
      clock observed before the press is drained history (`hitl.device_time`'s rule, reused for both row families through
      the new `newest_row_clock_us`); a row that answers *differently* is `contradicted` while a missing one stays a gap;
      and `now`/`sleep` are injected, so every bounded wait is testable without a device. There is no send path in the
      wait at all. The press helpers in `hitl/actions/{buttons,slots}.py` own SEND (`correlate=` / `before_device_us=`,
      the firmware's own classification names), so no flow declares its own gesture. **The drivers now use it:**
      `hitl_accepted_sequence.py`'s `_await_slot_disp` (selection, arm and recording receipts) is a `CorrelatedGesture`
      over the caller's vocabulary with `_slot_disp_rows` reduced to a pure row source, `_slot_ready_state` applies the
      floor itself as the state read it is, and `_switch_pending` takes its rule from `PendingSwitchGate` (the leg's own
      window and abort policy stay with the leg, so a queue row for the target ends it even if the switch later landed);
      `hitl_retention_handoff.py`'s seed waits became `_await_seed_receipt` with `recognize` = the device's own
      `Button press: Loop N (…)` row for this slot, `accept` = that slot's `DISP` arm row or `Loop N: Start Recording`,
      `execute` = its `DISP` recording row, `,RECA,<slot0>,` or `Recording started`, and a `conflict`/`refusal`
      vocabulary naming the two losses the device has actually produced (a merged `Loop N (double)`, and a press read as
      `Loop N: Stop Recording` / `Loop N: Queued playback switch`). The track-state family, which names no slot, is no
      longer accepted as this seed's receipt, and the seed's failure now reads
      `slot N never started recording: blocked at accept (…)` instead of a bare timeout. **Evidence:** new
      `test_hitl_correlated_actions` **23/23**; `test_retention_handoff_plan` **30/30** (7 new);
      `test_accepted_sequence` **91/91** (1 new) with the corpus **3/3** unchanged; `test_hitl_device_time` **16/16**
      (1 new); neighbouring suites green together — **244 tests / 9 suites**; twelve mutations red-proved across the two
      halves and every file restored byte-identically; the two unrelated pre-existing suite failures re-confirmed
      identical with this slice's files stashed. Host tooling only: no firmware row, build or device action.
      [Correlated-actions evidence](CORRELATED-ACTIONS-EXECUTION.md).
- [x] 3.2 Make cancellation suppress every subsequent test MIDI edge, release held notes safely, complete worker cancellation before capture release and preserve persistence-aware teardown; inject timeout/exception at every action boundary and verify partial report, nonzero exit and no post-abort press.
      **2026-09-30 — done.** `scripts/hitl/actions/cancellation.py` owns the three properties: every test edge goes through
      one `GuardedMidiOut`, which **refuses to send once the run is cancelled** (counting the refusal and raising
      `RunCancelled`), **releases the notes it was holding** (`note_off` per held note plus `All Sound Off`/`All Notes
      Off` per channel the run touched), and permits the teardown in an explicitly counted phase (`teardown_call`) so the
      shutdown's transport stop still goes out while nothing else does. Cancellation is cooperative and observable
      (`CancelScope.check`, `cancellable_sleep`, `wait_for(cancel=…)`, first reason wins) and the run in progress is
      registered (`begin_run`/`active_cancel_scope`/`cancel_active_run`) because the watchdog lives in `managed_capture`
      while the driver owns the scenario; `GuardedMidiOut.run()` is the outer context a driver adopts to absorb the
      cancel, release, record and unregister — with the driver's `DeviceSession` inner, so the drain runs first.
      **`managed_capture._run_with_watchdog` now cancels, waits a bounded grace (`HITL_CANCEL_GRACE_S`, default 30 s) for
      the scenario to leave the wire, and only then returns** — and its caller's context releases the capture child after
      that (no run scope registered keeps the earlier immediate-release behaviour). **Both drivers** register the run, open
      through the guard, keep their shutdown inner, make their waits cancel-aware (raising their run's own reason instead
      of a false "no receipt") and report `result = "CANCELLED"` + `cancel_reason` + the guard's own `midi` record in the
      same partial report they write on every other exit (nonzero). **Evidence:** new `scripts/test_hitl_cancellation.py`
      **21/21**; `test_managed_capture` **13/13** (the watchdog cancels, joins the scenario's own stop, refuses the
      post-cancel send and releases exactly the held note); `test_accepted_sequence` **93/93** (a cancelled run writes
      `CANCELLED`, sends nothing after the cancel but the release, still drains, and releases the capture **after** the
      shutdown); `test_retention_handoff_plan` **33/33**; neighbouring suites together — **405 tests / 15 suites**; seven
      mutations red-proved and every file restored byte-identically. Host tooling only: no firmware, build or device
      action. [Cancellation evidence](CANCELLATION-EXECUTION.md).
- [x] 3.3 Use the managed watchdog/metadata path and immutable versioned report schema; verify setup failures, protocol absence, teardown failure and verify-only replay without ports. Resolve the import gate only for the narrow architecture-approved observation-wait facade and run the import-gate suite.
      **2026-09-30 — done.** `hitl/reporting.py` owns the **sealed bundle**: `BUNDLE_SCHEMA_VERSION = 2` with a fixed
      section list, a sha256 `digest` over everything else, and a **verdict derived from the bundle's own evidence** by
      one rule (`derive_verdict`) that the reader re-runs — every recorded predicate ok **and** a teardown result present
      and ok, with a caller's success claim required to be backed by both (`shutdown failure SHALL prevent overall
      success`, enforced rather than restated). `write_report_bundle` seals **once** (atomic rename, refuses to
      overwrite, and checks version/digest/sections before writing), so a rerun cannot quietly replace a run's evidence.
      **Identity comes from the managed metadata path**: `hitl/run_metadata.run_identity` is now the one builder behind
      both `metadata.json` and a bundle's `identity` section. **Verify-only replay without ports** is
      `scripts/hitl_report_replay.py --bundle <path>`: it reads the bundle and re-derives the verdict with no MIDI or
      serial port (pinned by patching `mido.open_output`/`serial.Serial` to raise), exits 0/2/3 (pass / intact non-pass /
      not trustworthy), and states its own limit honestly — the digest is integrity, not a signature, so what it cannot
      be fooled by is a verdict that no longer matches its evidence. The oracle seals the acceptance verdict itself:
      `hitl/accepted_sequence_probe.py --emit-bundle … [--run-report …]` turns its checks into predicates (`UNKNOWN`
      counted as not-ok) and carries the run's shutdown result, so a pass needs a proven drain; setup failure, protocol
      absence, teardown failure and a legitimate non-pass are each their own recorded shape. **The import gate was
      resolved without widening it**: the observation wait is now one owner, `hitl/actions/observation.py::await_markers`
      (bounded, cancel-aware, ring-flush on the caller's cadence, injectable clock), which both drivers wrap — and it
      lives under `hitl.actions`, a prefix flows may already import, so the forbidden prefixes are unchanged and the
      gate suite pins that (facade under an allowed prefix, a flow importing it passes, nothing relaxed).
      **Evidence:** new `test_hitl_report_bundle` **14/14**, new `test_hitl_observation_wait` **7/7**,
      `test_hitl_import_gate` **5/5** (3 new), `test_accepted_sequence` **97/97** (4 new), `test_run_metadata` **6/6**
      (unchanged contract after the extraction), neighbouring suites green together, **six mutations** red-proved with
      every file restored byte-identically. Host tooling only: no firmware, build or device action.
      [Bundle/replay evidence](REPORT-BUNDLE-EXECUTION.md).

## 4. Prepare and drive both continuous-play recipes

Stage owner: stateless flows, registered scenarios and compatibility wrapper. Preconditions: target authorization manifest and fresh device identity are required run inputs. Postcondition: correct record/overdub fixtures and continuous-play measured windows. Invariants: no reboot, unrelated clear, global Stop in window, or pause hook. Commit boundary: scenario integration slice.

- [~] 4.1 Implement scoped stopped preparation through production clear gestures, correlated clear completion, RAM/SD census and successful persistence; verify SD residue, wrong target, failed save and unauthorized targets stop before further mutation, and repeats do not reset the board.
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
      **2026-09-28 review follow-up — completion is evidence, not elapsed time:** a recognized
      long press is only acknowledgement (the firmware logs it before deciding), so the settle
      path no longer returns `ALREADY_EMPTY` after a delay. `clear_slot` now takes an
      `emptiness_reader` and reports `ALREADY_EMPTY` only when it independently confirms the
      target empty; otherwise `UNCONFIRMED` (never success). The driver supplies a fresh census
      tri-state, so a lost refusal/switch is no longer read as successful preparation. The
      census itself is also SD-aware now (an SD-only slot is populated; unknown evidence stays
      unknown), so preparation clears it instead of skipping. `test_accepted_sequence` **52/52**
      including the confirmed/populated/unavailable cases.
      **2026-09-30 — the host half of the target authorization manifest landed; the device legs still owe
      evidence.** Task 4's precondition names a *target authorization manifest* as a required run input, and
      the packet routed it to the stimulus registry (it already declares each profile's recipe, and
      `stimulus list` is the mandated discovery path). It is now declared and enforced:
      `Authorization` (`scripts/hitl/stimuli/spec.py`) carries the 1-based track, the slots a run may mutate
      and the **provenance** of the grant (an empty scope is a read-only profile; a violation names the
      target *and* the declared scope); the registry declares the scope once for the two profiles these legs
      drive (track 8, slots 4/5 — the bench target the recorded `s4f13-*`/`s4f22-dec096-*` runs used,
      narrowed from the user's standing authorization of 2026-09-30) and `stimulus list` shows
      `targets=…`/`targets=undeclared` per profile; `hitl/stimuli/authorization.py` resolves
      `--authorization <stimulus-id>|cli` into the record a run is driven under and refuses a target the
      manifest does not cover. Both drivers resolve it **before the board is reset, before the census and
      before any gesture** and publish the refusal as `UNAUTHORIZED_TARGET` naming the target and the
      authorized scope; the explicit CLI targets stay the compatibility path (`cli`) and are recorded as
      such rather than silently unchecked. The record also reaches the sealed bundle: the oracle copies the
      run's `authorization` record into `reporting.build_report_bundle`'s section (spec scenario "Target
      outside the run's authorization"). Evidence: `test_hitl_stimulus_authorization` **24/24** (shape,
      declarations, the three checker inputs, and the drivers' pre-gesture refusal with an ordering sentinel
      that fails loudly instead of flashing the board), `test_accepted_sequence` **98/98** (the bundle
      carries the authorization), the 19-suite neighbouring sweep green, `pio test -e native` 2,643 cases
      (7 skipped, 2,636 succeeded), 6 mutations red-proved with byte-identical restores. Firmware untouched,
      so the build/RAM1 gates are not implicated by this slice. **Remaining for 4.1:** the device legs —
      target selection, the RAM/SD census and persistence-completion proof on a repeat run without a reset,
      which need an explicit per-session upload/target authorization. See
      [AUTHORIZATION-MANIFEST-EXECUTION.md](AUTHORIZATION-MANIFEST-EXECUTION.md).
- [ ] 4.2 Implement record recipe with genuinely empty SD-aware A/B, real record mode, switch-carried B start and one stop per interval; verify production dispatch and host scheduling with delayed observations, premature retirement and wrong-mode routes. A populated-A mutation must fail.
- [ ] 4.3 Implement overdub recipe using confirmed seeds and the proven single capture-switch gesture, followed by B's recognized overdub start and stop; verify no separate A stop, no playback re-affirmation and no wait for predecessor retirement in the critical window.
- [x] 4.4 Register both scenarios and route the standalone CLI through the same runner; verify legacy argument translation, explicit unsupported combinations, scenario listing, identical verdicts via both entry points and unchanged base/edit_full behavior.
      **2026-09-30 — registration, translation, refusals, listing and verdict identity landed; the flow
      extraction is named as the remainder.** Both legs are registered in the existing registry
      (`accepted_sequence_rec`, `accepted_sequence_od`, plus the `accepted_sequence` preset) with the
      **offline oracle** as their verifier, so `--verify-only` through the runner re-derives the acceptance
      verdict from a log without a device; `ScenarioSpec` gained the declarative `owns_capture` field (the
      runner steps out of the way instead of wrapping a capture-owning sequence in a second capture on one
      port) and `--layered` with a classic-registry id is now a named refusal instead of a `KeyError`.
      `hitl/scenarios/accepted_sequence.py` translates the runner's targets plus an allowlist of wrapper flags
      into one readable argv per run, defaults the run's **authorization to the registered profile's own
      manifest** (task 4.1: `STIM-BASE-2-2-01` for the record leg, `STIM-BASE-2-ONE-OD-01` for the overdub
      leg — stricter than the wrapper's `cli` default), and refuses by name every combination the leg cannot
      honour (a mismatched `--leg`; a missing track/first/second target or note channel; an ambiguous first
      target; the same target twice; two different statements of the channel; a channel that is not the
      track's own; an unknown flag; a stray positional) — before any device action. **Evidence:**
      `test_accepted_sequence_scenario` **32/32**, `test_accepted_sequence` **98/98** and corpus **3/3**, the
      full host sweep **1,018 tests / 75 suites** with only the seven pre-existing unrelated failures
      (`test_foundation_runner` and `test_layered_verifiers` re-confirmed identical with this slice's files
      stashed), `pio test -e native` 2,643 cases (7 skipped, 2,636 succeeded), **10 mutations red-proved** with
      byte-identical restores. The verdict identity is two-sided on the retained fixtures: the excerpt is
      `ACCEPTED_SEQUENCE_PASS` through both the standalone oracle CLI and `runner --verify-only`, and
      `s4f13-od-20260927_015145.log` is `INSUFFICIENT_EVIDENCE` through both. **Remainder (4.4b):** the design's
      stronger form — the leg sequence in a stateless flow with the driver reduced to a wrapper — needs the
      ~700-line body moved out of `hitl_accepted_sequence.py` under the flow import gate, and it must first
      answer *who owns the family-counter read-back* (the driver reaches `hitl.serial`, which neither a flow nor
      a scenario may import) rather than answer it during the move. See
      [SCENARIO-REGISTRATION-EXECUTION.md](SCENARIO-REGISTRATION-EXECUTION.md).
      **2026-09-30 — 4.4b landed; the task is closed.** The leg sequence moved into the stateless flow
      `hitl/flows/accepted_sequence.py` (`drive_leg`: census → scoped stopped preparation → seeded takes →
      the gesture plan → the final stop and the counted read-back), the session/authorization/report moved
      into the host run scope `hitl/accepted_sequence_run.py` (`RunRequest` → `run_accepted_sequence`), and
      `scripts/hitl_accepted_sequence.py` is now an **argument-compatible wrapper** (same flags, same exit
      codes, same artifact layout) that builds a `RunRequest` and delegates. Both entry points therefore
      reach one implementation: `host_midi_hitl.py run` → the registered scenario (intent + the runner→CLI
      translation) → the same run scope → the same flow, and the standalone CLI → the run scope directly.
      **The ownership question 4.4a named is answered:** the stopped-only per-family counter read-back is the
      **session's** (`ManagedCaptureSession.identity_stat_lines`, which owns the request, its fence and the
      decode in `hitl.serial.protocol`), so the flow calls one accessor instead of importing `hitl.serial`
      itself — the layer that is closed to both a flow and a scenario. **How the move was verified as a
      move:** every one of the 23 moved functions was diffed against `git show HEAD:` and 22 are
      byte-identical (the 23rd differs only in one docstring word, `driver` → `flow`); `_Ctx` deliberately
      gained a `press_ms` parameter so there is one context stand-in rather than two. The two real drifts
      the diff caught were fixed and are now pinned: the seed's arm gate must accept the printed
      `Loop N: Start Recording` row and a `RECORDING` state (measured `od-10`: the `DISP` row can lag the
      Serial row by seconds), and the census report's keys are the census's own 0-based firmware
      `(track, slot)` pairs, not this run's 1-based CLI numbering. **Evidence:** `test_accepted_sequence`
      **100/100** (two new: the printed-row arm receipt, and a run that dies after the census still leaving
      its milestone report — `od-08`'s lesson), `test_accepted_sequence_scenario` **38/38** (6 new: the
      sequence lives in the flow and not in the CLI, the wrapper's argument surface is unchanged, the flow
      imports nothing the layer may not, importing the flow loads no verify/reporting layer, the counter
      read-back is the session's accessor, and the milestone hook is called twice), `test_hitl_stimulus_authorization`
      **24/24** (repointed to the run scope's owner, same refusals), the full host sweep **1,025 tests** with
      only the seven pre-existing unrelated failures, `pio test -e native` 2,643 cases (7 skipped, 2,636
      succeeded), **7 mutations red-proved** with byte-identical restores (two of them *survived the first
      run* — the seed's arm wires and the milestone write — which is why those two tests exist now).
      **Rejected alternative, recorded:** routing the wrapper literally through `hitl.runner.main` would
      re-parse an already-parsed, argument-compatible CLI and relocate the run's artifacts (the scenario
      writes `<out>/accepted-sequence-<leg>`), changing a verified contract for no functional gain; the
      chosen shape reaches the same implementation through the same translation instead.

## 5. Complete acceptance oracle and regression gates

Stage owner: verify/reporting. Preconditions: complete per-phase identities and action trace. Postcondition: complete PASS conjunction and diagnostic non-success classifications. Invariants: existing runtime/content/timing gates retained, no guessed identity or waived event loss. Commit boundary: oracle/corpus slice.

<!-- 2026-09-29 device-reliability slice (host automation; evidenced, not checked off).
Fixed, each with a fixture and a device observation:
  * `ensure_transport_clock` confirmed a caller's own Start by **toggling** transport when the
    receipt was late, which stopped the transport the start had begun (`s4f22-base-device-03`
    start 25.086/toggle 25.603; `s4f22-step7-d2` start 53.527/recording 53.974/press 54.526).
    Confirmation after Start is now observation-only and a missing receipt is a **named failure**
    (`scripts/test_transport_clock_observation.py`). A three-attempt campaign afterwards reports
    `transport_press_count = 0`.
  * `_await_successor_start_receipt` pressed anyway on timeout. A press without the predecessor's
    stop receipt is a *different gesture* -- with the track stopped it starts a new capture -- and
    produced a phantom one-bar take that the verifier then scored as the record leg
    (`s4f22-step8-e2`). The leg is now **refused by name** with no press.
  * The record leg was scored from the **last** candidate at three layers: phase pairing accepted
    only `RECORDING -> STOPPED_RECORDING` (so a leg that resolved `RECORDING -> STOPPED` was not a
    pair at all), `extract_phase_sessions` took `record_sessions[-1]`, and
    `_verify_record_loop_length` took `_last_recs_row`. Replaying `s4f22-step8-e2` now selects the
    intended 65.011 -> 70.204 leg (`length=2304`) instead of the stray 72.697 -> 73.795
    (`length=768`); RECS rows and stored-grid ticks are scoped to the phase window.
  * Readiness: the attach gate accepted any `#`-prefixed row, so 40 `#DMA_XFER` lines passed with
    no capture rows. Readiness now comes from a fenced `!STOP_STATUS` **this run asks for**
    (`state=STOPPED`, idle stop queue); line volume is diagnostics, and the volume rule applies
    only to a run that owns the boot (an attaching run joins an idle device that emits nothing).
  * Attribution: `_send_short_press` (clock helper) and `hitl.midi_io`'s primitives record every
    outgoing control action with its caller; the report carries `transport_press_count` and
    `control_actions`.
  * Setup latency: the clear no longer waits for the deferred-save idle receipt (120 s configured,
    240 s floored) -- it aborted setup when the boot restore was slow and never made the clear more
    likely to succeed. The device is driven straight after boot; the firmware owns the refusal.
  * Setup latency (PX1, 2026-09-29): the *boot wait itself* is gone. `capture_session.py` takes
    `--boot-reset-mode auto|force|never` and, in `auto`, asks the device read-only whether it is
    already stopped: a stopped device is attached with **no** reset and the settle ends on that
    evidence (0.1 s in `s4f22-step11-h3`), while a device that is not stopped gets a `!REBOOT` whose
    firmware acknowledgement is read instead of a blind 10 s window (refusal named:
    `!REBOOT is transport-gated`, `s4f22-step11-h1`). The `s4f22-step10-g2` refusal below is fixed:
    silence from the transport-gated probe is resolved with **one** idempotent MIDI Stop
    (`mido.Message("stop")`, port resolved via `hitl.midi_io._find_midi_port`), then one more probe,
    then a refusal by name. A device this run had to repair is not handed over as-is: the repair makes
    the stream untrustworthy *by position* (`s4f22-step11-h2`: ring-drained `BAR` rows stamped
    850-1864 s after a 1855 s `state=STOPPED` reply; the scenario's stop press then *started* the
    transport and the clear could not drain), so that case owns a fresh boot
    (`ManagedCaptureSession.restart_with_boot_reset`). `s4f22-step11-h3` then ran clear -> record ->
    overdub -> second overdub with `precondition_failures: []`, a CONFIRMED EMPTY clear and no
    readiness refusal (verdict `INCONCLUSIVE_CAPTURE` on the pre-existing ring/REVT loss, not setup).
Open: an **attaching** run (`--no-boot-reset`, the acceptance contract's same-boot campaign) still
answers "is the transport running?" from **line position**, so a drained backlog of old `BAR`/`DFRAME`
rows can authorize a wrong phase and there is no reboot to clear it (`s4f22-step11-h2`). **Fixed**: the
phase decisions now read device time (`scripts/hitl/device_time.py`, `filter_live_lines`), the arm-time
answer compares the drained `ST,...PLAYING` record with the `Transport stopped` logger line in device
time, and the call site anchors at its track baseline; h2's prefix replay flips those decisions, and
`s4f22-step11-h6` no longer printed the stale conclusion. Residual: with a large drain lag the newest
transport row can itself be late — the transport-gated `!STOP_STATUS` is the authoritative answer and
is not yet wired into these decisions.
  * **Content-evidence decision (2026-09-29):** the `record_stored_revt_missing` /
    `overdub_stored_span_too_short` / `persistence_stage_missing:result` issues are evidence-attribution,
    not loss. `git log -S` puts the REVT bound (`ts > recs_stop_ts`, a **flush** clock) at `21b8ca8f`,
    against the function's own `373566a7` docstring; **fixed** by bounding the take by its row span
    (9-run replay: h5/h6 now 32 rows + `grid_ok`, a1/b1/e3/base-01/e1/h3 unchanged, `s4f22-step8-e2`
    4 -> 0 by design). The SEVT stored-span set cannot be fixed the same way: its blocks sit outside
    every phase window (pre-run restoration walks), so it needs a take/leg id in telemetry or a
    redefined check — **awaiting a direction**, as is accepting `already_pending` for the persistence
    `result` (risk: a save that never settles would read as passing). Firmware protection for
    `CaptureRecordType::Revt`/`SEVT,` is **preventative**: no in-run loss is measured (h5 `shed=4`,
    `overflow=0`, `wipe=0`; h3's `shed=73 217`/`tierA_evict=7 966` accumulated pre-run). Full proposal:
    `docs/Runtime/CURRENT_WORK.md`.
  * Clear refusals are named and retried (PX1, second half). The **force** reset path settled in 3.7 s
    (`boot_loaded`) and passed readiness on the first probe, but `s4f22-step11-h4` then died at its clear
    with `clear_not_confirmed` and no cause: the firmware had printed `Clear refused: deferred save
    temporarily blocked by post_load_holdoff; slot left untouched, save still owed`
    (`MidiButtonActions.cpp`), a **temporary** refusal distinct from `Clear aborted`, and the transition
    never retried it because it only retries a *disproved* request. `_serial_clear_refusal` now reads the
    reason, the clear waits for the save-idle receipt (30 s bound) and presses once more
    (`CLEAR_REFUSAL_RETRIES` 2), and a surviving refusal is reported as `clear_refused_<reason>`.
    `s4f22-step11-h5` then reported `waiting_commit_gate`, waited, and reached **CONFIRMED** EMPTY ->
    record -> two overdubs, exit 0 (`INCONCLUSIVE_CAPTURE (SETUP_UNCONFIRMED)` on the pre-existing
    `record_stored_revt_missing` / persistence-evidence loss, not setup). 4 new host cases.
Open separately: the capture ring lost 384 571 bytes to zeros with self-consistent bookkeeping
(`s4f22-step8-e3`: `head_bad op=4 type=0 plen=0`, zero prefix, `(tail-used) mod 1 MiB = head`);
`RING,overlap` in `poolAlloc` is armed to identify whether an event-pool block is handed the
ring's PSRAM, and `RING,head_bad` names any advance off a record boundary.
-->
- [ ] 5.1 Replace global counters/human-log inference with phase-scoped correlated input, accepted obligation, execution and retirement predicates for both modes; verify every missing/reordered/duplicate/wrong-identity edge, legacy deny-plus-latch versus true refusal, overlap miss, transport stop and clock change yields the specified non-PASS result.
- [ ] 5.2 Integrate existing content parity and timing verifiers plus independent shutdown result; verify a failing content, timing or teardown result vetoes otherwise complete acceptance. Replay the preserved failure corpus and synthetic complete chains with deterministic expected verdicts.
- [ ] 5.3 Run focused Python suites, architecture/import gates and full `pio test -e native`; record mutation results for former queue, timing and attribution bugs. Keep host success distinct from hardware qualification.

## 6. Qualify on hardware and close evidence

Stage owner: existing managed HITL entry point. Preconditions: successful identified capture-serial build, registered/reused stimulus discovery, explicitly authorized target set, pinned clock/display configuration, and all preceding gates. Postcondition: six qualifying bundles on one boot or truthful incomplete status. Invariants: no manual golden pass, no automatic reboot, no silent profile change or unbounded retries. Commit boundary: measured evidence and documentation closeout.

- [ ] 6.1 Run `.venv/bin/python scripts/host_midi_hitl.py stimulus list`, select existing stimuli or declared temporary variants, and pin a finite density-profile/attempt budget in the execution packet; verify authorization and read-only initial census before any clear or seed.
- [ ] 6.2 Measure bounded response latency and instrumentation overhead on the identified display/clock configuration under the supported working set; compare against existing timing gates and the packet budget. Stop qualification if observations perturb the required behavior or required evidence is lost.
- [ ] 6.3 Run three consecutive complete PASS attempts per record and overdub leg without manual correction or reboot, with response-confirmed scoped preparation between attempts; also verify the mixed record-A → overdub-B overlap sequence from runtime design Revision 23; preserve every failed attempt and reset consecutive counts on failure. Verify same boot, complete content/timing/identity chains and persistence-aware shutdown; a missed natural window stays NOT_EXERCISED.
- [ ] 6.4 Link verified immutable bundles to S4f.13 — accepted-sequence acceptance and check it off only if its full runtime gate is met; update architecture/usage docs, PROJECT_STATE, CURRENT_WORK, deliverable tracking and decision status to match shipped behavior. Run strict OpenSpec validation for both changes; archive neither while required evidence is missing.
