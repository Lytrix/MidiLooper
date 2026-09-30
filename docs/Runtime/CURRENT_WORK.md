# Current work (implementation scope)

**2026-09-30 — `response-driven-hitl-acceptance` task 4.1's host half landed: the run-scoped target authorization manifest is declared, enforced and recorded, so an unauthorized target stops a run before it mutates anything (host tooling; no firmware, no build, no device action).** Task 4's precondition names a *target authorization manifest* as a required run input, and the packet had already routed it to the stimulus registry (which declares each profile's recipe, and whose `stimulus list` is the mandated discovery path). **The declaration lives with the recipe.** `hitl/stimuli/spec.py` gains `Authorization` — the 1-based track, the slots a run may mutate, and the **provenance** of the grant (required, because an authorization nobody can trace is not one); an empty scope is a deliberate read-only profile, every number is range-checked, and `violations()` names the offending target *and* the declared scope. `StimulusSpec.authorization` is optional, so a profile that declares nothing cannot authorize a run — the checker refuses it by name rather than falling back to "no declaration, no check". `registry.py` declares the scope once for the two profiles these legs drive (track 8, slots 4/5 — the bench target the recorded `s4f13-*`/`s4f22-dec096-*` runs actually used, narrowed from the user's standing authorization of 2026-09-30), exposes it through `authorization_for`, and `stimulus list` now shows `targets=track 8 slots 4,5` / `targets=undeclared` per profile; a session that needs another bench scope forks the profile (`temporary_variant(..., authorization=…)`) instead of widening the canonical one, and the YAML view exports it as an indented block sequence. **The enforcement is the new module.** `hitl/stimuli/authorization.py` resolves `--authorization <stimulus-id>|cli` into the record the run is driven under: a manifest that does not cover the run's targets, an id the registry does not know, and a profile with no declaration are each an `AuthorizationRefused` carrying the record — never a traceback, because the refusal has to be legible evidence. The explicit CLI targets stay the compatibility path (`cli`, the default) and are recorded as *such* rather than silently unchecked. **Both drivers stop before the board.** `hitl_accepted_sequence.py` and `hitl_retention_handoff.py` resolve the authorization at the top of `main` — before the latch-refusing reset, before the re-upload, before the read-only census and before any MIDI gesture — write `result = "UNAUTHORIZED_TARGET"` with the refused target and the authorized scope into the same report they write on every other exit, and exit 3. **The record reaches the sealed bundle:** the oracle copies the run's `authorization` record into `reporting.build_report_bundle`'s section, so a sealed verdict names the scope it was driven under instead of leaving that section empty (new spec scenario *Target outside the run's authorization*). **Evidence:** new `scripts/test_hitl_stimulus_authorization.py` **24/24** (shape and range rules, the registry declarations and listing, the three checker inputs, and the drivers' refusal — with a reset-step sentinel that makes a revision which lost the guard fail loudly instead of flashing the board in a host test), `test_accepted_sequence` **98/98** (the bundle carries the authorization), the neighbouring 19-suite set green together, the full host sweep **987 tests / 74 suites** with only the seven pre-existing unrelated failures, `pio test -e native` **2,643 cases / 2,636 succeeded / 7 skipped / 0 failed**, **six mutations** red-proved with every file restored byte-identically, `openspec validate response-driven-hitl-acceptance --strict` valid and `git diff --check` clean. Firmware untouched, so the `teensy41-capture-serial` build and RAM1 gates are not implicated by this slice. **4.1 stays `[~]`:** its device legs (target selection, the RAM/SD census, persistence-completion proof and a repeat without a reset) still need a per-session upload/target authorization.

**2026-09-30 — `response-driven-hitl-acceptance` task 1.1: the change-level G2 execution packet is written, and the one open ownership question in it is now a recorded decision (documentation only; no code, firmware, build or device action).** [G2-EXECUTION-PACKET.md](../../openspec/changes/response-driven-hitl-acceptance/G2-EXECUTION-PACKET.md) pins the fields every remaining slice shares — the **dependency closure** (design §Dependency resolved plus the runtime change's `SHARED-CAPTURE-FINALIZATION.md`), the **observation vocabulary** as a table of row families with their emitting owners and the single place that decodes each (DISP rows, the `Loop N:` human lines, the `Button press:` classification row, `RECA`/`RECS`/`RID`, the runtime's `DIAG,reentry|reentry_window_open|canonical_release|stop_state` identities, the latch family, `ST,Track`/`PERS`/`SAVE`, the `LIBOBS`/`RING` read-backs, and `hitl.device_time`'s rule for a row's age), the **budgets** (managed ceiling 360 s, cancellation grace 30 s, finite per-action deadlines, the RAM1 free-for-locals floor **840** with the bank-arithmetic rule as the mechanism, the fixed-buffer ring with "loss is detectable" as a gate), the **gates** (`pio test -e native`, the `teensy41-capture-serial` build, upload only on approval, the mandated HITL entry points) and where each **transition oracle** is fixed (the oracle's links, the recipe transitions in design decision 5, the S22 latch transitions, the 3.3 bundle rule) — then audits every required field for an authoritative owner and carries one compact section per remaining slice (4.1–4.4, 5.1–5.3, 6.1–6.4) in the template's shape. **The audit's one finding is recorded rather than left open:** the run-scoped **authorization manifest** has no owner today, and the packet decides it belongs to the stimulus registry (which already declares each profile's bars, overdubs, interactions, `must_prove`, tempo and clock, and whose `stimulus list` is the mandated discovery path), with the drivers' explicit CLI targets kept as the compatibility path and as the record of what a run was given — 4.1 implements it; confidence medium-high, and it is a shape decision inside the approved contract, not a new contract. The per-turn **observation work number** stays deliberately unset: the mechanism owner exists (the capture-gated bounded service) and 6.2 is the measurement gate that produces the number. The landed slices keep their own artifacts, which the packet links instead of restating. `openspec validate response-driven-hitl-acceptance --strict` valid and `git diff --check` clean.

**2026-09-30 — `response-driven-hitl-acceptance` task 3.3 closed: a run's verdict is now a sealed, versioned bundle that replays without a device, and the observation wait has one owner outside the gate (host tooling; no firmware, no build, no device action).** Three clauses, each landed. **(1) Immutable versioned report schema.** `hitl/reporting.py` now owns the **sealed bundle** next to the progress report: `BUNDLE_SCHEMA_VERSION = 2` with a fixed section list, a sha256 `digest` over everything else, `write_report_bundle` sealing **once** (atomic rename, refuses to overwrite, checks version/digest/sections before writing), and one rule — `derive_verdict`, re-run by the reader — for the verdict: every recorded predicate ok **and** a teardown result present and ok, with a caller's success claim required to be backed by both, so "shutdown failure SHALL prevent overall success" is enforced rather than restated and a claim its own evidence denies is recorded as `bundle_verdict` instead of carried. **(2) The managed metadata path and verify-only replay without ports.** `hitl/run_metadata.run_identity` is the one identity builder behind both `metadata.json` and a bundle's `identity` section; `scripts/hitl_report_replay.py --bundle <path>` reads a sealed bundle and re-derives its verdict with no MIDI or serial port (pinned by patching `mido.open_output`/`serial.Serial` to raise), exiting 0 for a pass, 2 for an intact non-pass and 3 for a bundle that cannot be trusted (unknown version, content that no longer hashes, or a verdict its evidence denies) — and it states its own limit honestly: a digest is integrity, not a signature, so what it defeats is a bundle that *silently* disagrees with itself. The oracle seals the acceptance verdict: `hitl/accepted_sequence_probe.py --emit-bundle … [--run-report …]` turns each check into a predicate (`UNKNOWN` counted as not-ok, because an absent required edge is not a pass) and carries the run's `shutdown`, so a pass needs a proven drain; setup failure, protocol absence, teardown failure and a legitimate non-pass are each a recorded shape. **(3) The import gate, resolved without widening it.** The *observation* wait (rows the device emits on its own) is now one owner — `hitl/actions/observation.py::await_markers`, bounded, cancel-aware, ring-flush on the caller's cadence, injectable clock — wrapped by both drivers' `_await_markers`/`_wait_for_any`; it lives under `hitl.actions`, a prefix flows may already import, so no forbidden prefix was relaxed and the gate suite now pins that (facade under an allowed prefix, a flow importing it passes, nothing else changed). **Evidence:** new `test_hitl_report_bundle` **14/14** and `test_hitl_observation_wait` **7/7**, `test_hitl_import_gate` **5/5** (3 new), `test_accepted_sequence` **97/97** (4 new: a sealed pass, an `UNKNOWN` check refusing a success claim, a pass without a proven drain refused, a setup failure keeping its own verdict), `test_run_metadata` **6/6** (unchanged contract after the extraction), and the neighbouring suites green together; **six mutations** red-proved with every file restored byte-identically. Follow-up, explicitly not this task: the layered runner and the `--verify-only` path still write their own progress report, and the pre-existing `test_foundation_runner`/`test_layered_verifiers` failures — both `'Namespace' object has no attribute 'overdub_step_clocks'` in the legacy bridge's verify path — are unrelated and re-confirmed identical with this slice's files stashed. [REPORT-BUNDLE-EXECUTION.md](../../openspec/changes/response-driven-hitl-acceptance/REPORT-BUNDLE-EXECUTION.md).

**2026-09-30 — `response-driven-hitl-acceptance` task 3.2 closed: an abandoned managed run can no longer press, leaves no note sounding, and still drains before the capture is released (host tooling; no firmware, no build, no device action).** The watchdog bounded a *run*, but not the scenario inside it: on a ceiling breach `_run_with_watchdog` returned, its caller's context released the capture child, and the scenario thread kept working the wire with the serial port being torn down underneath it. `scripts/hitl/actions/cancellation.py` now owns the three properties. **Nothing is pressed after the abort**: every test edge goes through one `GuardedMidiOut`, which refuses to send once the run is cancelled, counts the refusal and raises `RunCancelled` (so the scenario *leaves* instead of computing on); `cancellable_sleep` bounds the time between two sends. **Held notes are released**: a run stopped between a `note_on` and its `note_off` leaves the device sounding a note, so the guard remembers what it put down and releases it — one `note_off` per held note plus `All Sound Off`/`All Notes Off` per channel it touched — and the release is idempotent. **The teardown is not a test gesture**: stopping the transport and draining must still happen, so it runs in an explicitly permitted phase (`teardown_call`) whose edges are counted apart, which is what lets a report say that nothing but teardown went out after the cancel. Cancellation is cooperative and observable (`CancelScope.check` with a first-reason-wins scope, `wait_for(cancel=…)`, `begin_run`/`active_cancel_scope`/`cancel_active_run` so the watchdog can reach a scope the driver owns), and `GuardedMidiOut.run()` is the outer context a driver adopts around its `DeviceSession` — absorb the cancel, release, record, unregister — with the drain still inner, so both drivers adopted it without re-indenting a body. **Ordering, which is the point:** `_run_with_watchdog` now cancels the registered run, waits a bounded grace (`HITL_CANCEL_GRACE_S`, default 30 s) for it to leave the wire, reports whether it stopped, and only then returns — the capture child is released after that; with no run registered there is nothing to cancel, so the earlier immediate release is kept and a scenario that ignores cancellation is abandoned with a named warning. **Both drivers** register the run, open through the guard, keep their shutdown inner, make their waits cancel-aware (raising the run's own reason rather than reporting a false "no receipt") and write `result = "CANCELLED"` + `cancel_reason` + the guard's own `midi` record into the same partial report they write on every other exit (nonzero). **Evidence:** new `scripts/test_hitl_cancellation.py` **21/21**, `test_managed_capture` **13/13** (the watchdog cancels a registered run, joins the scenario's own stop before returning, refuses the post-cancel send and releases exactly the held note), `test_accepted_sequence` **93/93** (a cancelled run writes `CANCELLED`, puts no further test edge on the wire except the release, still drains, and releases the capture after the shutdown), `test_retention_handoff_plan` **33/33**, neighbouring suites together — **405 tests / 15 suites**; **seven mutations** red-proved and every file restored byte-identically. Open in this section: 3.3 (managed metadata path, immutable versioned report schema, verify-only replay, the narrow observation-wait facade). [CANCELLATION-EXECUTION.md](../../openspec/changes/response-driven-hitl-acceptance/CANCELLATION-EXECUTION.md).

**2026-09-30 — `response-driven-hitl-acceptance` task 3.1 closed: the reusable action layer correlates a gesture's four stages, and both device drivers now wait through it (host tooling; no firmware, no build, no device action).** The action layer's press helpers were fire-and-forget (`_send_short_press` and return), so every driver re-derived inline whether the device had answered *that* press and how far it got — with its own substring set, its own deadline and, in the retention seed, markers that named no slot at all. `scripts/hitl/actions/correlation.py` owns SEND/RECOGNIZE/ACCEPT/EXECUTE: only the next pending stage advances (an execute-only row cannot satisfy recognition or acceptance, and a stage the caller declared no rule for is not awaited); every acceptable row must carry the identity **its own wire family** uses (the human `Loop 4:` label, the in-ring `DISP,3,` index and the `,RECA,3,` field are three different spellings of one slot), so a foreign slot's row cannot substitute; a row at or below the device clock observed *before* the press is drained history (`hitl.device_time`'s rule, reused for both row families through the new `newest_row_clock_us`); a row that answers *differently* is `contradicted` while a missing one stays a gap — the acceptance oracle's own absence-versus-contradiction split from this session's section 2; and `now`/`sleep` are injected, so a bounded wait is testable without real time and without a device. There is no send path in the wait at all, so nothing re-presses. The pending-switch clause is `PendingSwitchGate`, whose pending/released vocabulary is the firmware's own rows (`MidiButtonActions.cpp`: `Queued playback switch` including the filled-slot form, `Reaffirmed playback switch`, `Switch gesture from slot`) and whose attribution refuses an unattributed `Playback started` for naming no slot (`TrackTransportControl.cpp`'s `logTrackEvent` row). `hitl/actions/{buttons,slots}.py` gained keyword-only `correlate=`/`before_device_us=` and call `correlation.record_send`, so SEND is written where the note goes on the wire, with the firmware's own classification name (`short`/`double`/`triple`, plus `long`); every existing caller is unchanged. **The drivers consume it:** `hitl_accepted_sequence.py`'s `_await_slot_disp` (the selection, arm and recording receipts) is a `CorrelatedGesture` over the caller's vocabulary, `_slot_disp_rows` is now a pure row source with no freshness rule of its own, `_slot_ready_state` applies the floor itself as the *state* read it is, and `_switch_pending` keeps the leg's own window and abort policy (`a queue row for the target ends the leg even if the switch later landed`, now pinned) while taking its rule from the gate. `hitl_retention_handoff.py`'s seed waits became `_await_seed_receipt`: `recognize` is the device's own `Button press: Loop N (…)` row for **this** slot, `accept` is that slot's `DISP` arm row or `Loop N: Start Recording`, `execute` is its `DISP` recording row, `,RECA,<slot0>,` or `Recording started`, and `conflict`/`refusal` name the two losses the device has actually produced (a merged `Loop N (double)` from two taps inside the tap window, and a press the firmware read as `Loop N: Stop Recording` or `Loop N: Queued playback switch`); the slot-less `ST,Track` family is no longer accepted as this seed's receipt, and the seed's failure reads `slot N never started recording: blocked at accept (…)` instead of a bare timeout. **Evidence:** new `scripts/test_hitl_correlated_actions.py` **23/23**, `test_retention_handoff_plan` **30/30** (7 new), `test_accepted_sequence` **91/91** (1 new) with its corpus **3/3** unchanged, `test_hitl_device_time` **16/16** (1 new), and neighbouring suites green together — **244 tests / 9 suites**; **twelve mutations** red-proved across the two halves with every file restored byte-identically; the two unrelated pre-existing failures (`test_layered_verifiers`, `test_occupy_paint_serial_verify`) re-confirmed identical with this slice's files stashed. Open in this section: 3.2 (cancellation) and 3.3 (watchdog, versioned report schema, verify-only replay, the narrow observation-wait facade). [CORRELATED-ACTIONS-EXECUTION.md](../../openspec/changes/response-driven-hitl-acceptance/CORRELATED-ACTIONS-EXECUTION.md).

**2026-09-30 — task 2.2's “loss is detectable” clause is now a gate (host tooling; no firmware change).** The latch family was counted and read back on hardware, but nothing used the counters: `scripts/hitl/serial/protocol.py::record_evidence_stats` matched only `(RECA|RECS)` and demanded `set(families) == {RECA, RECS, RID}`, so `DIAG,latch` was invisible to the managed retrieval's own refusal/displacement gate; `managed_capture.py`'s regression check assumed every family had a baseline; and the accepted-sequence driver never asked for the read-back at all. All three are fixed: every family the reply carries is reported (the trio must still be complete, duplicates and malformed counters are still refused, and the family tag's own comma is handled — a `[^,]+` name matched `DIAG` and mis-filed the row), the regression check compares only families the earlier snapshot carried, and the leg asks for the read-back itself after its final transport stop (stopped-only, fenced, bounded, non-fatal). The oracle's new `family_accounting` check (setup kind) fails when a family's `rej`/`evict` **rose** inside the capture or its final `entered != enq + rej`; a loss already present in both snapshots is **not** a finding (the counters are per boot, so a pre-capture loss is outside the window and failing the leg for it would invent a verdict), and a capture with no read-back adds no check. **Evidence:** `test_accepted_sequence` **90/90** (7 new, one of them pinning a verbatim device row from `captures/base_20260930_164240_serial.log` so the regex stays tied to the printed form), `test_managed_record_drain` **21/21** (1 new), 15 neighbouring Python suites green (from the repository root; `test_hitl_serial_timing` assumes cwd = repo root), full native **2,643 / 2,636 succeeded / 7 skipped / 0 failed** on an unchanged firmware tree, four mutations red-proved and restored byte-identically, and `captures/s4f22-dec096-od-13` re-graded unchanged (`ACCEPTED_SEQUENCE_PASS`). Packet §7. Still owed in 2.2: the latch rows on hardware and sequenced gesture/switch rows.

**2026-09-30 — the latch's own sequenced identity evidence landed (firmware, instrumentation only; host-verified, device leg owed).** The remaining half of `response-driven-hitl-acceptance` task 2.2 was that the DEC-078 accepted stop could be **accepted and consumed inside one poll gap with neither edge recorded** — the only wait row that existed (`ODUB,stop,admission,…,accepted_stop_finalizer_wait`) names no slot, so a multi-track run could not attribute it, and `RecordFamilyStats` covered only `RECA,`/`RECS,`. Now `Track::latchAcceptedStop`, `Track::consumeAcceptedStopObligation` (which takes the consumption reason) and the S2b.6 park emit one compact Tier-A one-shot row per transition through `SC_ACCEPTED_LATCH`: `DIAG,latch,<accept|repeat|wait|park_stall|consume>,<track>,<slot>,<interval>,<destination>,<outcome>`, with the identity the latch holds on every edge, `repeat` for the documented same-identity no-op, and `consume` naming its reason (`admitted`, `superseded`, `sync_stop`, `transport_stop`) — so an accept-plus-consume pair inside one poll gap is **provable after the fact** and another slot's rows cannot substitute. Loss is detectable: the family has `RecordFamilyStats` counters, is Tier-A, and is reported by the existing stopped-only `!CAP_IDENTITY_STATS` as `RING,record_stats,DIAG,latch,…`. **No behaviour change**: no guard, ordering or state transition moved; the 104 pre-existing latch fixtures pass untouched. Evidence: `test_continuous_capture` **106/106** (two new fixtures), `test_capture_ring` **12/12** (family counters, read-back, identity in the row text, refused append counted, Tier-A classification), full `pio test -e native` **2,643 cases / 2,636 succeeded / 7 skipped / 0 failed**, `teensy41-capture-serial` **SUCCESS** with the RAM1 gate on an **unchanged** line (`variables 96,192 / ITCM 393,216 (code 391,288 + padding 1,928) / locals 34,880`, floor 840; `interactionLatchTransition` confirmed linked in the ELF), and five mutations red-proved and restored byte-identically. Packet/oracle: `openspec/changes/response-driven-hitl-acceptance/S22-LATCH-EVIDENCE-PACKET.md`. **Owed:** the device leg (upload + a retained stop showing `accept` → (wait) → `consume(admitted)`), the gesture/switch rows, and a poll-shaped readout of the counters. **Device status:** the image is flashed (sha256 `577841d6…`) and the family is verified **on hardware** by the stopped-only read-back (`RING,record_stats,DIAG,latch,entered=0,enq=0,rej=0,tx=0,evict=0` in `captures/session_20260930_164133.log` / report `…164240.json`) — present and counted, with `entered=0` because both overdub stops were admitted immediately, so no latch existed. Two attempts at task 7.2's successor-reentry reproducer both aborted at the **host** guard `overdub start refused: no predecessor stop-completion receipt` with `entered=0` for the whole boot (their logs carry no `successor_stop_denied_capacity`, no `DIAG,reentry,*` and no `ODUB,stop,` row, so the new emitter never ran and cannot be implicated). The `accept → consume` rows still need a stop that is actually **denied**, and both documented recipes for that (the retention handoff driver, the stop-interruption wrapper) clear a named scratch slot, which needs content authorization in this session. Packet §6 records the attempts.

**2026-09-30 — the accepted-sequence oracle now reads the overdub leg's own identity rows (host tooling; no device action, no firmware change).**
`scripts/hitl/accepted_sequence_probe.py` force-unknown'd L1/L5/L6/L7 on the overdub leg because its only stop
vocabulary was `RECA`/`RECS`, which the *record* lifecycle emits — and on this leg A and B are seeded by a record
first, so those rows are the **seeds'** stops (the false acceptance the corpus fixture pins). The intervals do have
their own identity rows, emitted by owners that already exist: the **start admission**
(`DIAG,reentry,admit,<tick>,<interval>,<slot>,<depth>`), the **accepted-stop binding** edge
(`DIAG,reentry,bound,…` — the S4f.3 edge whose own comment says a denied request and its later binding "can be
correlated instead of inferred"), the **execution** (`DIAG,stop_state,<track>,<slot>,6,7,…` and `…,7,5|1,…`), the
**retirement** (`DIAG,canonical_release,<slot>,<generation>,…`) and the button action's own per-interval
**request** rows (`Loop N: Stop Overdub`, `Loop T: Switch gesture from slot S` — in this leg's shape that switch
*is* A's stop). Each link is now decided on those rows, the binding must name the same interval the start did, A
and B may not share one identity, and the new `overdub_interval_identity` link is `ok` only when both intervals are
identified by their own start, accepted stop and retirement.
**Absence is a gap, never a violation**: those families are only as complete as the capture (a cut excerpt, an
evicted Tier-A row), so a missing overdub row reads `unknown`/`INSUFFICIENT_EVIDENCE` — the four pinned
`s4f13-od*` corpus fixtures keep their recorded verdicts — while a *contradiction* (a second acceptance, a second
execution, a binding naming another interval, two intervals claiming one identity) is a `FAIL`. Two rules the runs
themselves dictated: a successor that begins **after** A released reads `SETUP_INCOMPLETE` with "missing overlap is
NOT_EXERCISED" — S4f.22's own device-leg text, not a chain violation — and the DEC-070 retarget fallback
(`companion path stalled; raw pass only`) is **reported** in `evidence` plus a limitation but is not a link of this
oracle (its closure condition is `successor-capture-reentry-unguarding` 5.11), so a PASS here must not be read as
"the fallback is gone".
**Evidence.** `test_accepted_sequence` **83/83** (17 new: the complete overdub chain, the seeds' rows proving
nothing, a missing successor start as a gap, a start after the release as NOT_EXERCISED, a second acceptance, a
second execution, a binding for another interval, two intervals sharing an identity, two requests for one
interval, no request rows at all, the fallback reported-not-fatal, the slot-base conversion, plus four
device-excerpt checks); corpus **3/3** with unchanged expectations; 18 neighbouring host suites **361/361**
(capture integrity, command forwarding, managed record drain, capture transitions, device shutdown, device time,
midi-clock, retention handoff/probe, clear-before-record, edit-mode precondition, boot settle, serial timing,
transport-clock observation, hitl actions, managed capture, serial close drain). Four mutations red-proved on the
real oracle and restored byte-identically: the interval-consistency gate, the duplicate-execution gate, the
switch-source request count, and the "missing start is a gap" rule. A new device fixture
(`scripts/hitl/fixtures/accepted_sequence/s4f22-dec096-od-13-excerpt.{log,json}`) pins the driven run's own rows,
each excerpt line verbatim against its recorded source line, with the excerpt's sha256 and the load-bearing check
that removing the successor start from it stops being a pass.
**Re-graded with no device action:** `captures/s4f22-dec096-od-13` `INSUFFICIENT_EVIDENCE`(4) →
**`ACCEPTED_SEQUENCE_PASS`(0)**, every link on the run's own rows (A interval 3: start 55.763 s, accepted
57.035 s, retired 58.772 s; B interval 4: start 58.214 s, executed 59.142 s, retired 59.194 s — B began 0.56 s
inside A's window), and `captures/s4f22-dec096-od-01` → `SETUP_INCOMPLETE`(3) with the cause **named** (B's start
136 ms *after* A's release: no overlap, NOT_EXERCISED) instead of an unattributable unknown; both captures' own
`accepted-sequence-review.json` were rewritten from their unchanged logs.
**Not done, and not implied:** S4f.22 still owes a content-parity run and a leg arranged to overlap, and no
firmware row was added — the `ODUB,stage`/`ODUB,stop` families and the `#CAP,ST,Track,…` edges still carry no
interval identity, which is the remaining half of `response-driven-hitl-acceptance` task 2.2.

**2026-09-30 — the S4f.22 accepted-sequence driver's device path is corrected, and the OD leg now **drives** on hardware (`captures/s4f22-dec096-od-13`).** Host tooling only (`scripts/hitl_accepted_sequence.py`, `scripts/test_accepted_sequence.py`); firmware unchanged (`firmware_hex_sha256 8d07faec…` = the DEC-096 image). Ten device attempts produced the fixes, each attribution taken from the device's own rows: the census is retried on a bounded set and an unknown target **refuses before any gesture** (a lost census made the driver long-press a filled, non-selected target, which `CLEAR_TRACK_FOR_SLOT`'s not-selected branch answers by *starting playback*, `od-02`); a decode failure or a crash stays **named and reported** (an `LIBOBS,E` row read mid-write raised `int('')` inside the decoder and killed `od-08` with no report at all — the report is now written at every milestone); the census budget is sized for the **boot restore** (`od-06`/`od-07`: the focus track's slots are restored at boot, so the census read 8, then 9, then 14 of 64 known); a receipt is qualified by the **gesture's own device clock**, not by file position or by matching the gesture's press row (`od-03`/`od-04`: the ring flushes in batches, and a reverse text search made the floor *later* than the gesture); receipt waits **flush the capture ring** themselves (`od-05`'s `RECS,stop` reached the log only on a later drain); the commit mark goes in **before** the commit press (`od-09`: the device emits `RECS,stop,<slot>` at the stop edge, 4.6 s before its own `Transport stopped` row); seed readiness is read from the **live human rows** and an already-armed target is **never pressed again** (`od-10`: the clear's selection press arms an empty target, and the extra press was read as `Stop Recording`, discarding the take); the leg reports the **device's own deferral** (`od-12`: `record admission deferred while a stop obligation is live for slot 3`, whose publication landed 40 s after the stop) and waits for that publication before its first gesture (`--predecessor-publication-timeout-s`). **Result (`od-13`, `DROVE_LEG`):** both clears (B through the documented `post_load_holdoff` refusal retries, A through an `ABORTED` retry), both seeds armed/recorded/committed/settled, `leg_predecessor_publication settled`, and the full plan `select_track → a_overdub_start (16 notes) → b_switch → b_overdub_start (1 note) → b_stop_request`, shutdown `complete`, no latch. Oracle: every setup gate **ok** (fresh boot, identity pinned to this commit, overflow-free), `L2_no_capacity_denial` **ok**, `L3_a_finalization_window` **ok** (`DIAG,reentry_window_open,3,1049,1536`), `L4_a_capacity_released` **ok** (`DIAG,canonical_release,3,32,1536,8`) — the retention links green for the first time in these reruns — with `L1`/`L5`/`L6`/`L7` and `overdub_interval_identity` **unknown**: the oracle's own wording, *"RECA/RECS do not identify overdub intervals; correlated overdub start, accepted-stop and retirement evidence for A and B is still required"* (the sibling change's identity task, not this driver's gates). `test_accepted_sequence` **66/66**, 15 neighbouring host suites **372/372** together, four mutations red-proved and restored byte-identically, `git diff --check` clean, `openspec validate runtime-working-set-ownership --strict` valid. **S4f.22 stays unchecked**: the leg drove but its acceptance needs the overdub-identity evidence, and no content parity was run. See [§ Now implementing](#now-implementing).
**2026-09-30 — HITL musical timing: the device's USB MIDI clock, read and driven (DEC-097).** The tree treated "this board emits no USB MIDI clock out" as measured fact (`transport_clock.py`, `serial_transport.py`, `edit_controls.py`, both legacy baselines, the baseline guide and `test_transport_clock_observation.py`). It is false. Measured on the connected board: **48.00 Hz = exactly 24 PPQN at 120.00 BPM**, 384/384 pulses per 8 s, jitter 0.72–1.55 ms stdev, realtime `Start` ~0.31 s after a transport press — and the host can also **drive** it: the device slaves (0 clock pulses of its own while driven) and resumes its own clock 0.53 s after release. The real constraints are narrower: clock exists only while the device's own transport runs (`ClockManager::updateInternalClock`), and a bare realtime `Start`/`Continue` without clock leaves the device in `CLOCK_EXTERNAL` where it emits none (measured: collapsed to ~0.75 Hz). **Landed:** `scripts/hitl/midi_clock.py` (`MidiClockReader` — bulk drain, phase-locked index that survives a dropout and a post-deschedule burst, `bpm()` from the clock, `wait_pulse`/`wait_bars`; `MidiClockMaster` — absolute-deadline 24 PPQN with a spin tail and an `echo_reader` for the device-slaved verdict), `scripts/test_midi_clock.py` **10/10**, `scripts/hitl_midi_clock_probe.py` **device PASS** (`captures/midi-clock-probe/midi_clock_probe.json`: 6 bars with a constant offset within ±1 ms and ≤7 ms drift, drive 2.8 ms mean / ≤19 ms max per pulse with ≤3 ms cumulative drift over 4 bars), guide `docs/Guides/HITL_MIDI_CLOCK.md`, decision **DEC-097**, and the premise corrected in all six offending files plus `HOST_MIDI_AUTOMATION_BASELINE.md`. **Not yet wired:** `stream_pattern_for_bars` still paces by the wall-clock proxy, and the proxy's tempo source (`#CAP,BPM`) is never emitted in the mandated build (`SESSION_CAPTURE_BPM` defaults to 0); converting that path to `wait_pulse`/`wait_bars` is the next slice. Driving is not a tempo control — it moves the device's live tempo by ±1–3 BPM, so an exact pin still needs the CC102 fader (DEC-086).
**2026-09-30 — HITL clock pacing closeout: host mode works, `LOOP_EDIT` is proved by causing an ack, and the phase print tells the truth.** Three items on the DEC-097 clock work, all landed. **(a) `--clock-source host` now runs the base preset to `Result: PASS`** (`captures/session_20260930_142350.log`, report `captures/host_midi_automation_baseline_20260930_142629.json`): record 32/32, loop length 1536/1536, stored `REVT` count=32 **delta 48–48** bad 0/2, overdub 16/16 and second overdub 8/8 with stored `SEVT` spans of 2 bars each, first-note latency 12/2 clocks (max 12/12). Two things had to be true for that, both host-side. **The host drive must be continuous:** the device's slaving is a *timeout, not a latch* (`ClockManager::checkClockSource` leaves `CLOCK_EXTERNAL` 500 ms after the last pulse and the device resumes its own internal clock), so a host that only sent pulses inside a phase did not own the grid across the gaps — `MidiClockMaster.start_pump` now runs one 24 PPQN thread on one absolute schedule for as long as the transport runs and the phases wait on the pulses it owes (`wait_pulse`); `stop()`/`start()` own the realtime transport with it. **The arm flow must be the host's, unconditionally:** realtime `Stop` → Record press → `ARMED` (confirmed in the run) → realtime `Start` + pump → RECORDING. Gating that stop on serial evidence of a running transport was the bug: a slaved device emits none of the master-side `BPM`/`BAR` rows, so the evidence was absent exactly when the stop was needed, the arm press landed on a running sequencer, the track jumped `STOPPED → RECORDING` (skipping ARMED), a stray first take appeared (`captures/session_20260930_141936.log`: two `RECA` rows 7.4 s apart, the first stopping as `stopToStopped` at 2304 raw ticks against the requested 1536), and that extra take pushed the scoring take's `REVT` rows out of the span the stored-grid check bounds (`INCONCLUSIVE_CAPTURE (EVIDENCE_MISSING)`). **(b) `LOOP_EDIT` is proved by causing a session event, not by sampling.** The device announces a mode only when a session opens (`ControlSurfaceManager::sendEditSessionMidi`), so a late-attaching capture never holds the boot's row and the previous "select the loop row" establish path could not work (a row select does not open a session; the short edit press is `CYCLE_EDIT_MODE`, so where it lands depends on the mode being proved). `ensure_loop_edit_before_record` now holds the edit button **long** (its bound `EXIT_EDIT_MODE`, which always ends in `LOOP_EDIT`) and requires the ack that press leaves; unproven refuses by name with the `--boot-reset` remedy. Device proof in both runs: `[hitl] edit mode unproven in this capture; long edit press to land in LOOP_EDIT` → `[hitl] LOOP_EDIT proven by the ack the long edit press left`, against `captures/session_20260930_142350.log`'s `Long press detected: duration=663 >= effectiveLongPress=600` → `Button press: Edit Mode (long)` → `MIDI Encoder: Long press - exited edit mode`. **(c)** the record phase's bar-sync print no longer says `0.0 BPM` before a full tempo window has been observed; it says `tempo not yet learned` (`MidiClockReader.bpm()` is legitimately None at a phase start). **Regression:** `--clock-source device` re-run on the same tree passes (`captures/session_20260930_142518.log`: record 32/32, loop 1536/1536, `REVT` count=31 delta 47–49 bad 0/2, first-note 3/3). Host suites green: `test_midi_clock` **18/18** (four new: pump continuity, `start(bpm=)` starts the pump, `stop()` ends it, and a pumped phase waits on the pump instead of sending), `test_edit_mode_precondition` **9/9** (rewritten for the long-press contract), `test_clear_before_record_precondition` **39/39**, plus transport-clock 5/5, serial-timing 33/33, capture-transitions 61/61, accepted-sequence 54/54. Device left stopped (0.00 Hz), as found. Guide `docs/Guides/HITL_MIDI_CLOCK.md` updated (drive contract, arm ordering, the new `Wired, and how it is verified` section replacing the stale `Not yet wired`), the `LOOP_EDIT` section of `docs/Guides/HITL_CONTENT_VERIFICATION.md` corrected to the long-press proof. Seven unrelated pre-existing suite failures (`test_6_3_machinery_closeout_gate`, `test_compatibility_consumer_inventory`, `test_edit_session_state_serial_verify`, `test_foundation_runner`, `test_layered_verifiers`, `test_occupy_paint_serial_verify`, `test_revision_load_serial_debug`) confirmed identical with these changes stashed.
**2026-09-30 — the base preset is not repeatably green: the successor-overdub refusal is now queued in OpenSpec, not in prose.** Re-running the mandated entry point three times today (`base`, `--track 5 --loop-slot 1 --midi-channel 1`) produced one fail-closed refusal, and it is **not** host-clock or harness work: `captures/session_20260930_143325.log` logs `[4361.991] [WARN] [TRACK] Overdub stop retarget: companion path stalled; raw pass only` — the DEC-070 **bounded retarget fallback** (`src/Track/TrackOverdubLifecycle.cpp`, `retargetedFromPlaying` stalled across `CanonicalTail..CompanionBridge` for `kRetargetStallGrainLimit` grains) — after which the second overdub's start was refused (`no stop-completion receipt within 8.0s`, report `captures/host_midi_automation_baseline_20260930_143422.json`, `INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)`, issue `second_overdub_first_note_missing`). The same command then passed twice (device clock `captures/…143545.json`, host clock `captures/…143655.json`, both `Result: PASS`, 26/26 assertions, `issues=[]`), so the run needs a **rate**, not one datum. Ownership and the current state info live in the OpenSpec change that already owns this contract — `successor-capture-reentry-unguarding`: **task 5.11 is REOPENED** (its closure condition *is* "no run logs `companion path stalled`" and this run logs it) with the full evidence note, and the open device gate **6.2** now carries a dated status paragraph saying its "admitted successor stop after A retires" clause cannot be ticked while a run can fail on it. Mechanism pinned for the fix decision: `Reentry released slot=` is emitted only by a `DeferredIdleOwner::VisualCache` maintenance turn once the retired slot's `captureStopOwner` is clear and its canonical output/chunks are re-admitted (`TrackDeferredMaintenance.cpp`), and the harness gate accepts `Reentry released slot=` / `Record pass undo reset for slot` / `PERS,result,` within 8.0 s (`SUCCESSOR_ADMISSION_RECEIPT_MARKERS`), so the open question is whether the fallback delays that release edge past the window or whether its raw-only completion no longer satisfies the successor-admission contract. `openspec validate successor-capture-reentry-unguarding --strict` passes with the notes in place.
**2026-09-30 — issue 3 root cause and fix: a deferred overdub stop published its pass without its undo entry on the routes that skip `CompanionHistory`/`UndoCollection`.** The sidebar `U:` level (`TrackUndo::undoDepthForLoop`) counted only the record pass while the loop was live (`U:01` after recording plus overdubs, `U:03` after a reboot), and the base check `undo_redo_after_overdub_stop` logged `0 counts` against `expected_min: 1`. Three production routes skip those phases: the **clean-stop jump** (`PrepareTransfer` with `SealOutcome::SkippedEmpty` — the interval's take was already committed by its wraps, so nothing is staged at the stop; `stop_phase` trail `1,6,10,11`, the dominant device route), the **STOPPED destination without verified companion preparation** (`TrackPreparedStopCommit`'s transport-Stop fallback, which published no entry by design), and the **superseded causal base** (its variant runs before what was then the audible-bridge handoff). The user's "3-bar record loses the undo levels" is the same defect reached through timing: a late record start is quantized up to 3 bars, which shifts the later steps so the overdub stop lands as a clean stop. Two further defects kept the handoff from working once it existed: `job.undoPublicationReady` held `begin`'s *return* while the transaction is only opened in `DetachRedo` (so the gate skipped the prepare turn), and `TrackUndoPublication::canPublish` required a *staged* pass identity a clean stop never has.
**Fix (per the user's decision: deferred overdub route only, publish at pass publication, once per job, full-route machinery intact):** `Loop::gatherSessionExitReceiptUndoIdentities` (new, `LoopOverdubSession.cpp`) reads the interval's **own** session-exit receipt (DEC-068) in `collectOverdubSessionUndoPasses` shape, so the receipt type stays Loop-private; `Track::prepareDeferredOverdubUndoEntryFromReceipt` builds the canonical `OverdubPassAdded` entry (`pushOverdubSessionOnStop` shape) from that receipt plus any staged final pass; the handoff now sits at the **top of the `AtomicTransfer` phase**, once per job (`undoPublicationStarted`), and drives the protected transaction to `Ready` before any variant runs, so the audible bridge, the stopped recovery, the **no-staged** case and the superseded case each publish what was prepared (`commitSupersededCausalBase` now takes the stack as its publishability witness); `job.undoPublicationReady` means "publishable (`Ready`)"; `TrackUndoPublication::canPublish(…, kInvalidPassId)` means "this turn commits no pass of its own". Fail-open where the interval names no identity (publish nothing, always retire), fail-closed on a refused `begin` (retain the entry and retry) — the full route's existing policy.
**Evidence.** Fixture `test_stopped_overdub_stop_publishes_one_undo_entry` (four legs: stopped, second stop, superseded causal base, clean stop whose identity comes only from its receipt). Seven mutations red-proved, each restored byte-identically; removing the prepare call site also stalls `test_stopped_publication_requests_own_slot_once`, which proves the fail-open guard is load-bearing. `pio test -e native`: **2,637 cases / 2,630 succeeded / 7 skipped / 0 failed**; `pio run -e teensy41-capture-serial`: **SUCCESS**, RAM1 padding **1,928 ≥ 840**. Device A/B on the clean route with an identical command: `captures/s4f20-odundo-07` (pre-fix) trail `1,6,10,11` and `undo_entries` 5→5 with no short-route row, `captures/s4f20-odundo-10-1` (fixed) the same trail, 5→6 and one `src=deferred_short_route` row; full-route control `captures/s4f20-odundo-09` shows 5→6 and `MIDI: Undo (entries=3)`, the `U:03` a reboot reports. `10-1`'s scenario report is not a clean pass (its capture closed while the run was still finishing; no ring discard, fresh boot), so its undo-depth evidence is the device-log fact. Recorded as **DEC-095**; plan `docs/Plans/overdub_stop_undo_level_short_route_bugfix.md`. Device procedure note: a running transport refuses `!REBOOT` and then the harness's attach-freshness check refuses to start, so clear it first (two `stop` messages with the port held open, ~6 s wait) before `--boot-reset`.

**2026-09-29 — issue 2 root cause: the deferred Record publication never queued the record session-exit snapshot (fixed).**
The device symptom (Record-pass bar-3 notes disappear from the display after the first overdub stop and return
when the second overdub pass is undone) is a **snapshot-membership** defect, not a Shorten/Hide transform and not
content loss. The production Record stop publishes through the deferred route (**S4f.17 — common interval and
stop admission** / **S4f.21 — Record-stop caller migration**); `Track::stepDeferredRecordPublication` mirrored the
inline stop's `RecordPassAdded`/display/scratch/persistence side effects but **not**
`Loop::queueSessionExitSnapshot(RecordStop, …)`, which still lives at the end of the inline
`Loop::commitPendingCapturePass` — a path no production Record stop reaches any more (`commitCaptureForStop` is
now overdub-only). Consequence: the committed Record pass is never enumerated into the pending publication box, so
the box publishes the **live-record candidate** alone. That candidate by design stops at the completed-bar
frontier (`MidiServiceDrain::liveRecordCompletedTicks` — 1536 of 2304 in the reported take), RC1's
`handoffPendingSnapshotLength` extends only its *declared length* to the sealed length, and the successor
overdub's own exit queue stamps the root with the **current** committed-pass coverage, so
`publishedSnapshotIsCurrent()` is true for content that is 6 Record notes short. Consumers that serve a current
root — the visual-cache slice rebuild (`VCACHE,idle_src,SnapshotNotes`) and the overdub source view
(`DIAG,lcr,src,from=snapshot`) — then lose the Record pass's last bar; a later pass-stack change (the overdub
undo, or transport stop) advances `committedPassRevision`, the root stops being current, and the
passes-authoritative rebuild restores the notes exactly as reported.
Evidence, `captures/session_20260929_225529.log`: `snapshot_unit,kind=publish,q=0,a=1` @40.135 s;
`VCACHE,slice_clean,notes,42` and `DIAG,lcr,src,notes=42` (26 Record + 16 OD1) against
`DIAG,lcr,pairrows,at=passes,count=32`; 50 after OD2; `notes,48` after the 64.8 s undo; the STOPPED frame dump
still lists notes 75–79 at 1573–1765. Stored/effective content and persistence were correct throughout, and the
native 3-bar fixture could not see it because it drives the inline `Loop::commitCapturePass`, which queues inside
the same call.
**Fix:** `Track::stepDeferredRecordPublication` queues the record session-exit publication for its own slot with
the mode-correct reason (destination Playing → `RecordStop`, Stopped → `RecordStopToStopped`), restoring inline
parity; `include/Track.h`'s contract comment now names it. `Loop::queueSessionExitSnapshot` additionally takes a
pending **session-exit receipt** only for an *overdub* exit: a receipt is an overdub session's identity handoff
(DEC-068), so a Record exit always enumerates its own just-committed pass instead of a stale receipt's id (or
nothing, when the legacy one-shot handoff left it unfilled) while the watermark advanced.
**Fixtures:** `test_deferred_record_publication_queues_the_record_session_exit_snapshot`
(`test_shared_capture_finalization`, red-proves the call: removing it leaves `pendingSnapshotSessionExitQueued()`
false and no current root), plus `test_record_session_exit_with_a_truncated_candidate_keeps_the_take_tail` and
`test_record_session_exit_ignores_a_pending_overdub_receipt` (`test_loop_take_survival`, pinning the Loop-side
invariant that a handoff-extended candidate still publishes the take's tail note, and the receipt selection).
**Decision:** **DEC-094** — a pass commit queues its session-exit publication on every route, and the coverage
watermark may not advance over a pass the box did not enumerate.
**Residual (recorded, not fixed here):** the enabler is `queueSessionExitSnapshot` writing
`box.coveredPassRevision = committedPassRevision()` unconditionally while no guard requires the box to have
enumerated the committed inventory; a general completeness gate changes *when* a session-exit root may publish
and needs its own fixtures (a wrong predicate would produce a permanent publication gap, the RC3 class).
Plan, full evidence table and non-goals:
[`record_session_exit_snapshot_queue_deferred_publication_bugfix.md`](../Plans/record_session_exit_snapshot_queue_deferred_publication_bugfix.md);
slice record:
[`S4F17-CUTOVER-PACKET.md`](../../openspec/changes/runtime-working-set-ownership/S4F17-CUTOVER-PACKET.md)
§ "S4f.20 — the deferred Record publication queues the record session-exit snapshot".
**Verification, measured:** focused `test_shared_capture_finalization` **55/55**,
`test_loop_take_survival` **143/143**, both together **198/198**; full `pio test -e native`
**2 636 cases / 2 629 succeeded / 7 skipped / 0 failed**; `pio run -e teensy41-capture-serial`
**SUCCESS** with ITCM 393,216 (code 391,288 + padding **1,928**) and locals 34,880 — padding unchanged
from the pre-slice tree, floor 840. Three mutations, each restored byte-for-byte: removing the new call fails
**only** the deferred fixture; removing the Loop-side queue fails the Loop fixtures; restoring the unfiltered
receipt selection fails **only** the receipt fixture.
No upload and no device action in this slice at that point (device run recorded below).

**2026-09-29 — device run of the S4f.20 session-exit queue fix: base HITL PASS with a complete published root, matched A/B against the pre-fix image.**
Uploaded the fixed image and ran the authoritative base HITL (`host_midi_hitl.py run --preset base
--track 5 --loop-slot 1 --midi-channel 1 --boot-reset`, registered stimulus `STIM-BASE-2-2-01`, reused
unchanged). Runs are pinned by `metadata.json`: `git_commit 300af1fa` + `git_dirty true` (this fix
uncommitted), fixed image `firmware_hex_sha256 0e7d549e…`, A/B image `df665ce5…`.
**Fix confirmed at the mechanism level:** on the fixed image a `DIAG,snapshot_unit,kind=publish` with
`a=1` lands **0.3 s after the record stop and before the first overdub start** (`captures/s4f20-record-exit-snapshot-02`,
24.459 s vs `Live Overdub` 24.862 s), and the root after the first overdub stop is `a=2` (record + OD1) —
the record session-exit publication the deferred route never queued.
**Reported symptom reproduced by the A/B:** the same preset on the image *without* that queue call
(`captures/s4f20-ab-prefix-02`, one-line mutation, source restored byte-identically afterwards) publishes a
frontier-truncated root: its snapshot-derived source view holds **26 of the pass's 32** record notes
(pitches 48–73 at ticks 282–1530; the omitted 74–79 sit at 1530–1770 against the completed-bar frontier
1536), so the snapshot-derived display after the first overdub stop reads `visual=42` = 26 + 16 instead of
48 — the record's last bar missing, exactly the reported class. The fixed image no longer truncates at the
frontier.
**Matched A/B — the reported symptom is fixed on device.** Three runs on the same preset/stimulus, two of
them the same take shape (3-bar, notes crossing the completed-bar frontier 1536):

| Run | Image | Take / frontier-crossing content | Published root | Snapshot-derived display after the first overdub stop | Verdict |
|---|---|---|---|---|---|
| `captures/s4f20-ab-prefix-02` | pre-fix (one-line queue call removed) | 2304, notes to 1770 | **26/32** record notes — omitted 74–79 at 1530–1770 = exactly the frontier set | `visual=42` (26 + 16) — record's last bar missing | `INCONCLUSIVE_CAPTURE`, `overall_ok=false` |
| `captures/s4f20-lencheck-01` | **fixed** | 2304, notes to 1744 | **32/32** (`lcr,src,from=snapshot,notes=32`, `srcspan count=32`), **48** after OD1 = 32 + 16 | `DISP after overdub: frame=48 visual=32`; `srcspan count=48` | **PASS**, `overall_ok=true` |
| `captures/s4f20-record-exit-snapshot-02` | fixed | 1536 (2-bar take, content to 1494) | 28/32 (see below) | `visual=44` | `INCONCLUSIVE_CAPTURE` on the documented residuals |

So the fix's *content* effect is demonstrated on hardware, not only its mechanism: with the queue call the
frontier-crossing record pass is published whole (32/32; `CLN reconcile record: sent=32`;
`overdub_ok=True transport_ok=True`), without it the same shape publishes 26/32 and the display loses the
last bar — the user-visible symptom, and the recorded "no disappearing bar-3 notes / no shortened note at
the end of bar 2" observation matches the fixed run's numbers.

**Correction — the 28/32 observation is *not* a standing defect (my earlier wording over-claimed).** The
fixed 2-bar run (`…-snapshot-02`) held 28 of 32 record notes (missing pitches 76–79), which the
completed-bar frontier cannot explain (its content ends at 1339, under its 1536 frontier) — but the fixed
3-bar run reproduces neither the shape nor the shortfall (32/32), so this is **one unreproduced
observation in 2 fixed runs**, not a second confirmed defect. Its distinguishing feature is the host
pacing state of that run: the host's tick model reported `recStart=343 → stop 698` (355 ticks) while the
device actually ran raw 1891 ticks — a **~5× divergence** of the wall-clock tempo proxy (all three runs
printed `Bar sync via wall-clock tempo proxy (120.0 BPM) — Teensy clock not on USB MIDI in`). Kept open as
"28/32 seen once"; attribution candidates stay the note-pair resolution across fragment caps or a
loop-phase/rotation projection, and the tree's `vch`/sameid instrumentation emitted **no rows** in these
builds, so its gate or call site needs checking before it can attribute this.

**Take length is host stop-press timing — this firmware slice cannot change it.** The *same fixed image*
produced a 2-bar take (raw 1891) and a 3-bar take (raw 1997) on consecutive runs; the length is stamped in
the stop **admission** (`Track::beginDeferredRecordStopForSlot` → `computeRecordStopLengthTicks(raw,
captureMaximumTick)`), i.e. before `stepDeferredRecordPublication` and its new queue call. The flip is the
device's bar quantization: `quantizeTransportRecordLength` snaps to the nearest bar while the overshoot is
within `ticksPerBar/2` = 384 ticks, else up — raw 1891 − 1536 = 355 ≤ 384 → **1536**; raw 1997 − 1536 = 461
> 384 → **2304**. Every run so far stops ~0.46–0.63 bar late, i.e. right on that edge, so the 2-vs-3-bar
outcome is the fixture's stop-press timing, not a firmware effect: the *original* 3-bar runs (raw 1989,
2020) and the newest ones (1891, 1997, 2022) sit on the same edge, and no device-side change here moves it.
The instrument for it is the host's strict MIDI-clock mode (`--require-midi-clock`, refuse wall-clock
pacing), which needs the device's clock on USB MIDI in — the existing open item, unchanged by this slice.

**Verdicts and gates:** the fixed 3-bar run is a **PASS** (`overall_ok=true`); the other two end
`INCONCLUSIVE_CAPTURE (SETUP_UNCONFIRMED)` on the *documented* pre-existing residuals
(`persistence_stage_missing:result` — the `already_pending` decision; `overdub_stored_span_too_short` /
`second_overdub_stored_span_too_short` — the SEVT leg-attribution decision), not on this change.
Everything this change could regress is green in every run: record grid `count=32 delta 47-49 bad=0`,
`CLN reconcile record: sent=32`, overdub `16/16`, second overdub `8/8`, `VERIFY display … overdub_ok=True
transport_ok=True`, `MIDI-boundary timing … overflow_free=True call_sites=True`, `min free RAM2 77 824 B`.
No content loss at any point: the passes hold the record notes. The device carries the **fixed** image
(re-uploaded after the A/B; padding 1,928, floor 840). Evidence dirs:
`captures/s4f20-record-exit-snapshot-0{1,2}`, `captures/s4f20-ab-prefix-0{1,2}`,
`captures/s4f20-lencheck-01` (the `-01` variants of the first two are refused short-stream attaches).

**2026-09-29 — `vch` geometry trace added for issue 2.** The existing session-capture `DIAG,lcr,vch,miss` now reports same-NoteId actual geometry (`sameid_n/sameid_s/sameid_e`) when prepared and native display representations disagree. This directly distinguishes a true Shorten/Hide transform from a prepared-window reconstruction mismatch. Native `test_loop_take_survival`: **141/141 passed**; `teensy41-capture-serial` build: **SUCCESS**, RAM1 padding **1,928 bytes**. No upload or firmware behavior change was performed after this instrumentation.

**2026-09-29 — strict MIDI-clock HITL mode implemented.** `legacy_record_baseline.py` now supports `--require-midi-clock` / `HITL_REQUIRE_MIDI_CLOCK=1`: focused runs refuse serial/wall-clock pacing and require incoming MIDI Clock pulses. Serial RECA/BAR receipts remain validation anchors; no arbitrary sleep is used as musical timing. `TransportTickAnchor` treats the fresh RECA tick as the phase origin, and the host tests cover stale-BAR rejection. Host verification: **88/88 passed** plus syntax validation. A strict invocation reached the device's MIDI-clock-active path; its later failure was the unrelated no-clear slot-observation guard. Device execution with a real clock-feedback path remains required.

**2026-09-29 — refinement of issue 2 classification.** The device observation is split: Record bar-3 notes reappear when transport stops, so their temporary absence is a playback/display-window refresh artifact, not confirmed content loss. The persistent defect is narrower: the Record-pass note crossing the bar-2 boundary is shortened after the first overdub stop and remains wrong. The native 3-bar fixture preserves that crossing note, so the remaining target is the device playback/display handoff path, not Loop canonical storage or the stopped display owner.

**2026-09-29 — issue 2 focused reproduction is green in native.** Added `test_three_bar_record_first_overdub_stop_keeps_record_display_geometry` to `test_loop_take_survival`: a 2304-tick/3-bar Record take with a note crossing the 1536-tick bar boundary, followed by a first Overdub commit/stop and snapshot drain. The Record crossing note, Record bar-3 note, and overdub note all remain present with exact geometry. Focused suite: **141/141 passed**. This does not clear the device observation; it narrows it to device-path/setup state or a path not represented by the Loop/display owner fixture. No firmware edit made.

**2026-09-29 — S4f.22 issue classification from the fresh device run.** Four observations are pinned before diagnosis: **(1) setup timing** — Record started at tick 379 and stopped at tick 2373, so the take is 2304 ticks/3 bars although the fixture expected 2; the note stream was started too late. **(2) candidate firmware defect** — after the first overdub stop (`Reentry window open` → `Reentry released`), the display loses Record-pass material from bar 3 onward and shortens a note crossing bar 2; this requires a focused playback/display reproduction and is not dismissed as setup. **(3) setup sequencing** — after the second overdub, the log has no `MIDI Button A: Stop Overdub`; the next gesture is consumed as `Overdub session undo`. **(4) setup consequence** — the intended global undo/redo never reaches global `U:` history; the subsequent double/triple gestures operate on the overdub session, so the observed `U:02`/missing `U:03` cannot yet diagnose global undo numbering. No firmware change made from these observations.

**2026-09-29 — S4f.22 device baseline and content parity: scenario pass, parity blocker.** Uploaded the approved `teensy41-capture-serial` image and ran the authoritative baseline on track 5 / slot 1 / MIDI channel 1 with a forced fresh boot. Managed baseline: **exit 0 / Result PASS / overall_ok=true**; RECA 2, RECS 8; record grid 32/32; persistence `result=ok`; timing `overflow_free=true`, `call_sites=true`; RAM2 minimum 77,824 B. Read-only content dump/playback verification then **FAILED** inventory parity on every cycle (`inventory_equal=false`), while `storage_matches_effective=true` for 152 effective rows and `max_bar_refs=72` (`capacity_path_exercised=false`). S4f.22 remains open: do not reinterpret this as a firmware contradiction or weaken the content oracle; the next work is to diagnose the playback-capture mismatch.

**2026-09-29 — S4f.22 host integration gate verified.** Focused Python transition/integrity/drain tests: **98/98 passed**. Focused native `test_capture_ring` + `test_shared_capture_finalization`: **62/62 passed**. OpenSpec validation passed. The host-side S4f.22 contract, mutation, capture-accounting, and deferred Record oracle paths are verified; the S4f.22 checkbox remains open only for the separate response-driven device acceptance, which is still parked and must not be substituted by host evidence.

**2026-09-29 — S4f runtime verification resumed after the scope reset.** Reused the existing production implementation; no firmware edit was needed in this continuation. Focused `test_shared_capture_finalization`: **54/54 passed**. Full `pio test -e native`: **2,625 passed / 7 skipped** on the final run. `pio run -e teensy41-capture-serial`: **SUCCESS**, RAM1 padding **1,928 bytes** (approved floor 840). This verifies the current native/build path for S4f.17–S4f.21; parent task checkboxes remain open where their integration/device acceptance still requires separate evidence. HITL/content-evidence remains parked.

**2026-09-29 — runtime scope reset: S4f implementation is primary; HITL acceptance is parked.** The active implementation scope is the runtime OpenSpec `runtime-working-set-ownership`, specifically **S4f.17 — common interval and stop admission**, **S4f.18 — bounded Record preparation**, **S4f.19 — first-bar and onward playback handoff**, **S4f.20 — atomic publication and retirement**, **S4f.21 — Record-stop caller migration**, and **S4f.22 — shared finalization integration gates**. Native fixtures and the `teensy41-capture-serial` build are the next verification gates. Response-driven HITL/content-evidence work remains recorded, but is parked and must not block these runtime slices. Do not use an unstable HITL run to redefine a native/runtime verdict; device acceptance resumes only when a named S4f gate requires it.

**2026-09-29 — content-evidence decision proposal: one host fix landed, two decisions open.**

**Decision requested.** **Accept the landed REVT take-bounding fix and approve its release**; pick a
direction for SEVT leg attribution, which the current telemetry cannot support. `already_pending`
semantics and firmware evidence protection stay separate follow-up decisions (see *Next decisions*).

Findings, each classified, with the acceptance criteria the fix has to meet:

1. **REVT was bounded by the flush clock, not the phase clock (VERIFIED, host; regression).**
   `_extract_revt_note_on_ticks` kept a `#CAP,<us>,REVT,…` row only when
   `RECA_ts <= us <= RECS_stop_ts`, but `<us>` is when `flushOneRevtRecord` wrote it (`micros()` at
   flush) — which the function's own docstring has said since `373566a7` ("REVT emission is deferred
   (idle flush), so lines can appear after RECS,stop"). `git log -S` attributes the bound to
   **`21b8ca8f`**.

   **What "take-bounding" means operationally.** `_record_take_row_index` is the row index of the first
   row matching `^#CAP,<us>,RECA,` (strict, so a `RING,record_stats,RECA,…` row is not a take).
   `_next_record_take_row_index` is the first such row appearing **after** a
   `^#CAP,<us>,RECS,(stop|stopToStopped),` row, else `None`. The considered rows are
   `lines[take_start : take_end)` and their `tick` field is collected. The window is therefore in
   **log order** (the ring's FIFO), not device time: a deferred flush stays inside its take, and a
   successor take's rows fall outside.

   **Acceptance criteria, validated by a three-extractor replay** (`pre-21b8ca8f` unbounded, the
   current bound, this rule) over all nine stored runs — the fix must return the pre-regression tick set
   wherever a take has no successor, and exclude a successor's rows:

   | run | pre-`21b8ca8f` | current | fixed | criterion |
   |---|---|---|---|---|
   | `s4f22-step4-a1`, `s4f22-step5-b1`, `s4f22-step8-e3` | 32 | **0** | 32 | regression repaired (their *recorded reports* predate `21b8ca8f`; re-running them today fails too) |
   | `s4f22-step11-h5`, `s4f22-step11-h6` | 32 | **0** | 32 | repaired (32 REVT rows at 72.97 s / 1836.2 s vs stops 36.65 s / 1807.35 s) |
   | `s4f22-step3-base-01`, `s4f22-step8-e1`, `s4f22-step11-h3` | 0 | 0 | 0 | correctly still missing: no rows delivered |
   | `s4f22-step8-e2` | 4 | 0 | 0 | successor excluded (`21b8ca8f`'s intent); those 4 rows follow the *stray* take's `RECA` |

   **Successor-take residual, for acceptance:** a take whose stored rows are flushed *after* a successor
   take started is scored `revt_missing`. Accept it, because the failure direction is safe and bounded —
   a false negative becomes an evidence-loss verdict (`INCONCLUSIVE_CAPTURE`), never a pass, while the
   alternative (keeping successor rows) produced a false *pass* on foreign evidence. Measured 0 false
   negatives in the nine stored runs; the only differing case (e2) is the intended exclusion. Settling
   it properly needs a take id on the REVT row — the same telemetry gap as SEVT below.
2. **The overdub stored-span mismatch is not a missing window (VERIFIED, host; direction open).** The
   first-overdub call passes no `after_ts`/`before_ts`, but passing the leg's phase window cannot fix it:
   the rows are emitted by *walks*, so their `#CAP,<us>` is a walk clock, and the SEVT blocks in these
   runs sit entirely **outside** the phase windows — `s4f22-step11-h5`: blocks at 9.2–18.1 s and
   72.9–73.3 s against windows 37.8–43.0 s / 51.6–56.8 s; `s4f22-step11-h6`: blocks at 1792.6–1798.8 s
   against 1809.0–1814.7 s / 1822.8–1835.9 s. Those rows are the **pre-run** (boot/attach-era)
   restoration verification, which is also why h6's apparent pass (`32` rows spanning exactly 2 bars)
   is accidental — it scored restored-library evidence as the run's leg, and why h5's count (64) sees
   the span go to 3 bars. **Decision needed:** either (a) the firmware stamps a take/leg identity on
   `SEVT` (and `Revt`) so the harness can scope by it, or (b) the check is redefined to verify the last
   complete walk's set under an explicit invariant ("the loop's full stored inventory") rather than "the
   leg's notes". **Recommendation: (a)**, the firmware-stamped take/leg identity — it is the only option
   that keeps the check proving what it claims (the leg's own stored note-ons). Treat (b) as a
   **deliberate reduction**: it would stop proving leg attribution and prove only that *some* complete
   walk's inventory is consistent — exactly the accidental pass `s4f22-step11-h6` shows today. Interim
   (host-only, no semantics lost): when the SEVT set is dominated by rows from outside every phase
   window, report the check as **evidence unavailable** (`INCONCLUSIVE_CAPTURE`) instead of a span
   violation, so a pre-run walk cannot be scored as the run's leg in either direction.
3. **A deduplicated save request is demanded to produce its own `result` (VERIFIED, host; decision
   open).** All seven in-sequence `PERS,request` rows of `s4f22-step11-h5` are `outcome: already_pending`
   (the post-boot commit holdoff), and the handoff verifier requires `result` for the request it saw →
   `persistence_stage_missing:result`. **Risk of accepting `already_pending` as success:** a run whose
   save never settles would read as passing — the `result` row is the only proof the write reached SD,
   so the relaxed form must still require *some* later `PERS,result,…,ok` plus a settled drain, and must
   keep the missing case as evidence loss, not as a contradiction.
4. **The ring's delivery position, not its content, is the variable (VERIFIED, device).**
   `s4f22-step11-h5`: `attempts=13339, ok=13339, tx=13335, shed=4, overflow=0, tierA_evict=0, wipe=0` —
   numerically clean, stored rows 36 s late. `s4f22-step11-h3`: `ok−tx = 58 294` **constant** over 20
   samples/29 s with `shed=73 217` and `tierA_evict=7 966` accumulated earlier (the long pre-run session
   with no reader). `s4f22-step11-h6`'s abort is the separate intermittent ring wipe (`wipe=0` in
   h3/h5). **So: no *in-run* evidence loss is measured in these runs.**
5. **`Revt` records and `SEVT,` text are unprotected and unaccounted for (VERIFIED from code).**
   `appendCaptureRecord` derives tier/critical from `text`, which is `nullptr` for
   `CaptureRecordType::Revt`; the flush's pressure loop stops only at a **tier-A text** head, so a Revt
   record can be discarded with only a generic timing-shed note; `isTierALine` has no `SEVT,` entry.
   Neither family has a counter (`recordFamilyStats` is text-keyed), which is exactly why h3's absent
   REVT rows cannot be attributed to a discard path. Same defect class already fixed for
   `DIAG,reentry*`. **Urgency: preventative, not urgent** — these runs show no measured loss; the fix's
   value is turning an unattributable loss into a reported one.

**Next decisions, owners and the evidence each needs.**

| Decision | Decider / technical owner | Evidence before acting |
|---|---|---|
| SEVT attribution direction, (a) firmware take/leg id vs (b) redefined invariant | **Eelke Jager** (repository owner, `Lytrix/MidiLooper`) decides; the check's technical owner acts in the `response-driven-hitl-acceptance` §5 verify/reporting role, and the (a) half in the `docs/Agents/builder.md` role over `Utils/DebugSessionCapture.cpp` + `Track/TrackDeferredMaintenance.cpp` | for (a): the same seam that already queues `Revt` per take (`deferredRecordRevtsSlot`) is the natural id source; the upload needs explicit approval. For (b): a written statement of what the check would no longer prove (leg attribution) and who accepts that loss |
| `Revt`/`SEVT,` tier protection + per-family counters | **Eelke Jager** decides; technical owner acts in the `docs/Agents/builder.md` role over the capture telemetry | an **in-run** loss demonstration for these families — a run where `RING,record_stats`/`RING,stats` deltas rise for them while their rows are absent. The counters do not exist yet, so the honest sequence is counters first (no semantics), measure, then decide protection. A policy decision that unattributable absence is unacceptable is the alternative trigger |
| `already_pending` accepted in the persistence handoff | **Eelke Jager** decides; technical owner acts in the §5 verify/reporting role | a run whose in-sequence save **settles inside the measured sequence** (a `PERS,result,…,ok` for an in-sequence request) — i.e. after the post-boot commit holdoff stops deferring (`post_load_holdoff` / `waiting_commit_gate`, recorded open in the PX1 note) |
| Successor-take residual of the REVT span rule | **Eelke Jager** decides (record-leg owner named by `21b8ca8f`) | none needed to accept: the failure direction is evidence-loss, not a pass (0 false negatives in nine runs). Revisit with the same take/leg id as SEVT (a) |

*One maintainer authors every commit in this repository, so each row's decider is the same person; the role
named per row says which hat the work is done in (`docs/Agents/README.md`).*

**Release state, release instrument, rollback (tested).** **Nothing is released yet:** `1358a1cd`,
`24af74e4` and `bd86f20a` are local commits on `feature/runtime-working-set-ownership`, which has **no
upstream** — nothing is pushed, so "approve release" means *approval to push the branch and open the PR
into `dev`*, not sign-off on an already-published release. That instrument follows
[`Git-Commit-And-PR-Workflow.mdc`](../../.cursor/rules/Git-Commit-And-PR-Workflow.mdc); it is a
verdict-semantics change, so it is not a direct-commit hygiene change.

**Rollback, tested rather than asserted.** Reverting only part of the chain **does not apply**: with a
two-commit revert, `git revert --no-commit 1358a1cd` conflicts on `docs/Runtime/CURRENT_WORK.md`,
`docs/Runtime/PROJECT_STATE.md` and (modify/delete) `scripts/test_hitl_stored_record_grid.py`, and even
newest-first `24af74e4 1358a1cd` still conflicts on `CURRENT_WORK.md`, because the later docs commit
rewrote the same block. The procedure that applies cleanly — tested in a scratch worktree at this tip —
reverts **every commit of this change, newest first**: the three fix commits *and* the commit carrying
this paragraph, because the docs edits touch the same blocks as `bd86f20a`:

```bash
git revert --no-commit 1358a1cd^..HEAD     # this docs commit, bd86f20a, 24af74e4, 1358a1cd
# equivalently, by SHA (newest first): the docs commit, bd86f20a, 24af74e4, 1358a1cd
```

and the result was checked: the code file is byte-identical to the pre-fix commit
(`git diff bcfd0ed7 -- scripts/hitl/legacy_record_baseline.py` empty), it compiles, and the reverted
extractor restores the old behaviour (`s4f22-step11-h5`/`h6`/`step4-a1` → `revt_missing: true`,
`note_on_count: 0`). The revert also removes the gate and its fixtures, which is correct: they belong to
this change. Rollback is **host-only** — no firmware, device state or persistence — so it needs no device
run and cannot invalidate a capture.

**Pre-existing suite failures, linked to a baseline record.** Measured by toggling *only* this change in
the same checkout and environment (the same `captures/`): with the change → `Ran 793 tests`,
`FAILED (failures=7, errors=1)`; with `bd86f20a 24af74e4 1358a1cd` reverted in place → `Ran 784 tests`
(the 9 grid fixtures are this change's), `FAILED (failures=7, errors=1)`, and the **identical eight
names**: `test_all_6_3_host_gates_pass`, `test_materialize_to_event_vector_only_in_machinery_owners`,
`test_pass_when_od1_stop_uses_odub_enter_without_st`,
`test_record_overdub_capture_verifier_uses_base_report_config`,
`test_session_exit_gap_predicate_only_in_visual_hold_owners`,
`test_uses_last_complete_recording_window`, `test_verify_only_record_overdub_writes_report`, and ERROR
`test_enter_fails_when_cycle_before_select`. A sibling `Ran 14 tests`, `FAILED (failures=2)` subprocess
suite (spawned by the `6_3` gate) repeats two of them. To regenerate: apply the revert above, run
`python -m unittest discover -s scripts -p 'test_*.py' -t scripts`, then `git revert --quit && git reset
--hard bd86f20a`. An earlier attempt to measure this in a worktree at the branch's pre-session commit was
**abandoned as confounded** — the suite writes into `captures/`, whose presence differs outside the main
checkout.

**Reproducing the verification.** The acceptance gate is
`python scripts/hitl_replay_stored_record_grid.py` → a table per capture with the criteria; **exit 0** with
`9 capture(s) meet the accepted criteria`, exit 1 on a violation (checked by feeding it a wrong expected
count: `COUNTS DIFFER FROM THE ACCEPTED TABLE`). Its criteria: the fix equals the pre-regression set for a
take without a successor, excludes a successor's rows, and never adds a tick the pre-regression extractor
did not see.

**Capture-absent is not a pass.** Because the captures are gitignored, a fresh clone has none, so the two
modes are explicit: the default skips and prints
`NON-BLOCKING: … developer convenience only and is NOT release evidence` (**exit 0**), and release
validation uses `--require-captures`, which lists every missing capture and exits **2** (verified: exit 2
with a missing set, exit 0 with all nine present). Partial presence in the default mode prints
`OK (partial) … not release evidence`.
The repository has **no CI configuration** (no `.github/workflows`), so the strict mode is a *manual
release step* inside the release instrument above; the label plus the exit codes are what keep an absent
fixture set from ever reading as evidence.

The consumer of `revt_missing` is the single inline gate
`if stored_record_grid.get("revt_missing"): issues.append("record_stored_revt_missing")`, which
`scripts/test_hitl_stored_record_grid.py` (9 cases) pins directly; the suite counts and their linked
baseline are above. `test_rotation_marks_incomplete` additionally flaked once under the full-suite load and
passes 2/2 isolated (`python -m unittest scripts.test_hitl_capture_integrity`, 17 cases — a 2 s polling
deadline in a background collector, unrelated); the isolated reruns are **not** part of the 793. A
*verdict*-level offline re-score is not available: `--verify-only` runs only the scenario's stimulus
validation (its own code says "use baseline JSON report for full gate checks") and the legacy baseline
cannot be re-pointed at a log without risking device presses — so the extractor, not the verdict, is the
pinned unit.

**Order:** (1) landed; **approve its release**; (2) and (3) await the decisions above, and the SEVT
decision is required *before* any SEVT change; (5) after (2)/(3) show whether loss still occurs.



**2026-09-29 — the transport-proxy phase decisions now read device time, not the follow index.**
Scope: `scripts/hitl/device_time.py` (new), `scripts/hitl/serial_transport.py`,
`scripts/hitl/legacy_record_baseline.py`. Why: capture telemetry goes through the firmware's fixed
evidence ring, which flushes in batches, so a record drained **late** lands after newer rows and
`lines[after_index:]` reads it as current. `captures/s4f22-step11-h2` measured 343 `BAR` and 10 453
`DIAG` rows out of device order (drained stamps 850-1864 s after a 1855 s `state=STOPPED` reply); the
run decided "transport running" from a tail of 40/40 drained rows, skipped its start presses, then
pressed a "stop" onto a stopped device, which *started* it, and its clear could not drain.

* `filter_live_lines` drops a stamped row older than the newest stamp already seen, keeps unstamped
  directly-printed rows, and restarts the watermark at a boot row (`micros()` restarts with `!REBOOT`).
* `serial_lines_show_transport_activity` filters by it before the tail scan;
  `_transport_running_before_record_arm` compares a drained `ST,…PLAYING` record with the
  `Transport stopped` logger line in **device time** (`human_line_device_us`), with the old position
  comparison kept as the clock-less fallback; the baseline's call site passes `track_baseline_len`.

Evidence: prefix replay of h2 — where the old rule said "activity present" the new rule says no, with
40/40 drained rows in the tail, while genuinely live tails (h3/h6) agree; device `s4f22-step11-h6`
(attach to a log already holding 19 026 lines) no longer printed the stale conclusion and reached
`CONFIRMED` / `Clear completed`. Host: `scripts/test_hitl_device_time.py`, 15 cases.

**Residual, recorded:** with a large drain lag the newest transport row can itself be late, so the
filter can answer "no current activity" while the transport runs; the authoritative answer is the
transport-gated `!STOP_STATUS` (a fresh fenced reply *is* proof the transport stopped), which the
readiness gate already uses and these phase decisions do not yet.

**2026-09-29 — the HITL boot decision, replacing the boot wait (PX1).**
Scope: host HITL automation only (`scripts/capture_session.py`, `scripts/hitl/managed_capture.py`,
`config.py`, `runner.py`, `layered_cli.py`). A managed run starts the scenario as soon as the device is
driveable, instead of after a fixed post-open window:

* `capture_session.py` takes `--boot-reset-mode auto|force|never` (default `auto`, `--boot-reset` is the
  legacy alias for `force`). `auto` asks once, read-only (`!STOP_STATUS`, written directly like the
  reset request): a stopped device is attached without a reset, otherwise the capture requests
  `!REBOOT` and reads the firmware's acknowledgement (`host-requested reboot`, `--reset-ack-s`) instead
  of waiting for a banner that a refused request never produces.
* The post-open settle is evidence-driven (`settle_end_reason`): `attach_no_reset` immediately, or
  `boot_loaded` on `DIAG,boot_reset` + `BOOT,load,ok`; `--boot-wait` is only the ceiling for a device
  that never gets there.
* `establish_attach_readiness` resolves the transport-gated silence: one idempotent MIDI Stop
  (`mido.Message("stop")`, port resolved through `hitl.midi_io._find_midi_port`), then one more probe,
  then a refusal by name.
* `run_with_managed_capture` drives a confirmed-stopped device straight away; a device it had to repair,
  or one that still will not answer, gets `ManagedCaptureSession.restart_with_boot_reset()` — a second
  capture that owns a forced boot (**why** in the h2 entry below).
* `_prepare_deferred_save_before_clear`'s docstring and dead `wait_for_deferred_save_idle_if_active`
  import follow the shipped behavior; its two stale expectations in
  `scripts/test_clear_before_record_precondition.py` now pin "reported, not waited on".

Evidence: `captures/s4f22-step11-h1` (9 s of blind retries replaced by a named refusal),
`captures/s4f22-step11-h2` (the repair, and the stale-backlog misread it caused),
`captures/s4f22-step11-h3` (0.1 s attach settle, `precondition_failures: []`, CONFIRMED EMPTY clear,
run verdict `INCONCLUSIVE_CAPTURE` on the pre-existing ring/REVT loss). Open: the attaching-run
transport proxy still reads line position, not device time, for a drained backlog. No firmware change.

Second half of the same slice, from the `--boot-reset` (force) device run: a fresh boot settles in
3.7 s and readiness passes immediately, but `s4f22-step11-h4` then died at its *clear* with
`clear_not_confirmed` and no cause. The firmware had named it — `Clear refused: deferred save
temporarily blocked by post_load_holdoff; slot left untouched, save still owed` — a **temporary**
refusal (distinct from `Clear aborted`), which the transition never retried because it only retries a
*disproved* request. Now: `_serial_clear_refusal` reads the reason, the clear waits for the owner of
that condition (the save-idle receipt, 30 s bound) and presses once more (`CLEAR_REFUSAL_RETRIES` 2),
and a surviving refusal is returned as `clear_refused_<reason>`. `s4f22-step11-h5` then reported
`clear refused by the firmware (waiting_commit_gate)` → waited → **CONFIRMED** EMPTY → record + two
overdubs, exit 0 (its `INCONCLUSIVE_CAPTURE (SETUP_UNCONFIRMED)` verdict is the pre-existing
`record_stored_revt_missing` / persistence-evidence loss, not setup).

**2026-09-29 — the record-leg misattribution, fixed at the phase pairing.**
`record_loop_length_mismatch` / `record_note_span_too_short` were never a firmware defect. The phase
pairing accepted only `RECORDING -> STOPPED_RECORDING`, so a take that resolved to a full transport
stop (`RECORDING -> STOPPED`, the stop-to-STOPPED leg) was not a pair at all; a later **stray take**
then became the only candidate, and both `extract_phase_sessions` (`record_sessions[-1]`) and
`_verify_record_loop_length` (`_last_recs_row`) took the **last** one. Replaying
`captures/s4f22-step8-e2` through the fix selects the intended **65.011 -> 70.204** leg
(`length=2304`) instead of 72.697 -> 73.795 (`length=768`). Three changes with fixtures: pair
`RECORDING -> STOPPED` as a leg end; select the first complete pair (matching the overdub
convention); scope the RECS rows and stored-grid ticks to the record phase window, with an empty
window reported as evidence loss rather than permission to score a foreign take. Together with the
successor-start refusal -- which removes the stray take at its source -- and the clock fix, both
record-leg assertions should now read the intended take. **Open: the ring reclamation evidence
(unchanged; e3's zeros) and the bounded same-boot campaign.**

**2026-09-29 — correction: the "Record length/span" defect was a misattributed second take.**
e2's trace (`captures/s4f22-step8-e2`): the intended record leg completed **correctly** --
`Recording stopped (to STOPPED) @ tick 2366 (recStart=382 length=2304)`, two bars -- at 70.20 s.
The `overdub start` phase then pressed transport while the track was STOPPED (71.69), which started
an empty-slot capture, and `Recording started` (72.70) produced a **second, unplanned one-bar
take**; the 73.80 `Stop Recording` press ended *that* take at `length=768`, and the verifier scored
it as the record leg -- hence `record_loop_length_mismatch` 768/1536 and
`record_note_span_too_short`. Immediately before, the harness had printed
`[warn] overdub start: no stop-completion receipt within 8.0s; pressing anyway`, i.e. it pressed
**without the predecessor's stop-completion receipt**: the same press-without-receipt pattern the
clock fix removed, now in the successor-start path (`_await_successor_start_receipt`). So there is
**no Record-length firmware defect**. Open instead: (i) a missing successor start receipt must be a
**named failure**, never a blind press; (ii) the record assertions must read the take belonging to
the record phase window, not the last `RECA`/`RECS` pair. Ring investigation stays open. The
control ledger now covers the baseline's own presses, so the next run attributes every transport
press to its caller.

**2026-09-29 — toggle defect verified gone on device; the ring wipe is content loss, not a stray advance.**
Three same-boot attempts after the fix (`captures/s4f22-step8-e1/e2/e3`): **`transport_press_count`
is 0 in all three**, so the clock helper no longer toggles transport to confirm a start -- the d2
sequence cannot recur. The attach readiness gate passed every attempt (no refusal). Caveat on the
ledger: it currently covers only presses made *through the clock helper* (`transport_clock._send_short_press`);
the baseline's own presses go through its own helper and are not yet in it, so `0` proves "no
clock-helper toggle", not "no transport press at all". Routing the baseline's press primitives
through the same ledger is the next attribution step.

No attempt passed. **e1** (1 970 rows) never started an overdub and reported
`record_loop_length_mismatch`, `record_note_span_too_short`,
`persistence_loop_persist_not_sliced`. **e2** (10 794 rows) repeated `ARMED->RECORDING` four times
and reported the same length/span mismatch plus short stored overdub spans. A **second independent
defect** is therefore open beside the ring: the Record take's length/span is wrong. **e3** lost
384 571 bytes to one wipe, with the decisive detail:

`RING,head_bad,1,op=4,head=724595,tail=60590,used=384571,type=0,plen=0,prefix=0000...0000`

`(tail - used) mod 1 MiB = 724595 = head`, so the ring's bookkeeping is **self-consistent** and the
head points at **zeros**, not at a torn row. The ring believes it holds 384 KB that was never
written or was zeroed under it -- content loss by reclamation/reuse, not by a bad advance, which also
means the earlier `op` reasoning does not apply. Both the ring (`extmem_malloc`, 1 MiB, freed
nowhere) and the event-pool blocks (`LoopEventStore::poolAlloc` -> `extmem_malloc`, zeroed per block
on open) come from the same external heap, so the next step is to verify they cannot overlap: check
whether any unguarded `extmem_free`/`std::free` misclassification can free a region inside the
ring's 1 MiB, and whether a pool block can be opened over it. Ring investigation stays **open**.

**2026-09-29 — the extra transport press, named and removed; readiness now proven from a fresh reply.**
The d2 trace showed an unexplained extra Global Transport press between the start and the intended
Record stop (start 53.527, recording 53.974, press 54.526, Record stop 59.248 -> armed a new take),
which is the short take and the missing PLAYING in one sequence. Cause: `ensure_transport_clock`
bounded its wait for the *caller's own* start acknowledgement and then fell through to
`ensure_midi_clock`, whose press is a **toggle** -- so a late receipt stopped the transport the
start had just begun (same signature as `s4f22-base-device-03`: start 25.086, toggle 25.603). Fixes
committed: (1) confirmation after Start is **observation-only** and a missing receipt is a **named
failure**, never a re-press, pinned by `scripts/test_transport_clock_observation.py`; (2) attach
readiness comes from a **fenced `!STOP_STATUS` this run asks for** (`state=STOPPED` plus an idle
stop queue), with line count demoted to diagnostics -- the old gate accepted 40 `#DMA_XFER` lines
with no capture rows and could not tell a live device from a backlog; (3) `_send_short_press`
records every control action with its caller, and the run report carries `transport_press_count`
and `control_actions`, so an unexpected transport press is attributable. **The ring-wipe
investigation stays open**: no `head_bad` in two runs does not clear it, and the detector checks
plausible payload length, not full record-boundary integrity. Next: the bounded same-boot campaign,
preserving every attempt and the existing content/timing gates.

**2026-09-29 — head-boundary detector armed; the wipe did not reproduce, and setup variance is
now the dominant failure.** Two same-boot attempts with `RING,head_bad` armed
(`captures/s4f22-step7-d1/d2`) produced **no `head_bad` row and no wipe**. The failures were
elsewhere: **d1** attached to a device whose session log held 15 lines, aborted with
`!STOP_STATUS: response fence missing`, and reported `SPARSE_CAPTURE` with
`clear_to_empty: clear_not_confirmed`; **d2** recorded but never entered overdub
(`abort: record stop did not reach PLAYING`) with `record_loop_length_mismatch`,
`record_note_span_too_short` and 42 shed rows. So the detector neither fired nor refuted, and
the same registered preset has now produced a PASS (`s4f22-step5-b1`), a wipe
(`s4f22-step5-b2`/`b3`, `s4f22-step6-c2`) and a no-record attach (`d1`). Per-run device-state
variance -- not one ring defect -- is what blocks repeatability. Next step: fail fast when the
attach has no usable stream (d1's 15-line log should abort at setup, not after the scenario),
and verify the device's settled state before the measured window, instead of reporting a sparse
capture afterwards. `RING,head_bad` stays armed meanwhile: it will name the operation if the
non-boundary head recurs.

**2026-09-29 — the ring wipe, named: the head lands mid-record, not on a boundary.**
`RING,wipe_detail` (added this session) from `captures/s4f22-step6-c2`:
`head=990370 tail=993518 used=3148 type=50 plen=11317 prefix="295,4294967295,1"`. The 16
bytes at the head are the middle of a text row, not a header, so the head had been
advanced by a total that did not match the record it sat on; the next flush then read
text as a header, saw `total > used`, and reset the ring (3 148 bytes). In that same boot
every append succeeded (`attempts == ok` = 28 472) and tier-A eviction, overflow, critical
rejection and shed stayed at 0 or 4 -- this is a bookkeeping/torn-record defect, **not**
capacity and **not** eviction. A same-boot attach is only where it becomes visible, because
the ring then carries the previous attempt's backlog. Guarding the nested append against a
flush removed one way to move the head under a flush; **at least one way remains**. If it is
not found by inspection, the next instrumentation is a bounded rolling trace of the last
ring operations (kind, args, head/tail/used before and after) dumped with the wipe, which
names the operation that advanced into the record. Repeatability remains **not proven**:
`captures/s4f22-step5-b1` PASS, `b2`/`b3` and `s4f22-step6-c1`/`c2` INCONCLUSIVE.

**2026-09-29 — S4f.22 device legs: the Record leg passes; capture loss is still under attribution.**
Uploaded the candidate (`teensy41-capture-serial`, RAM1 padding 2,072) and ran the
registered base preset. Attempt **a1** (fresh upload + boot reset) is a **PASS**: integrity
COMPLETE, `issues []`, all five transition checks green, drain complete (RECA 1 / RECS 7 /
RID 1 reconciled) and **32 `REVT` rows** present. Two defects the runs demonstrated are
fixed: the queued stored-note scan borrowed chunk ids the producer then freed (it read
nothing, `REVT` 0), now pinned for the deferral; and the REVT slice drains during playback
through the bounded chunk walk. The boot-restore save completes ~29 s after a real boot, so
the scoped-clear precondition passes and is no longer the blocker.

Repeat attempts on the same boot did **not** pass: **a2** `INCONCLUSIVE_CAPTURE` with
`RING,wiped,1,15981` (integrity INCOMPLETE, now fail-loud) and **a3** `FAIL`
(`second_overdub_first_note_missing`). The new stopped-only `RING,stats` reports
attempts == ok (16 794 / 31 102 cumulative), `tx` below it (a backlog the reader never
drained) and one wipe. A nested append during a flush can no longer move the head (guard +
fixture red-proved), but the wipe reproduced anyway, so its remaining cause is
**unattributed**. Loss totals (shed / tier-A evicted / overflow / critical rejected) are now
cumulative so the next run can attribute it instead of reading counters a notice already
cleared. **Repeatability — three consecutive passes per leg on one boot — is NOT proven.**

**2026-09-29 — evidence reliability before the next device attempt (step 2).**
The in-flight S4f.21/S4f.22 slices were reviewed and checkpointed in six commits.
Three evidence-reliability defects were then fixed, all host-verified:

* the Record REVT scan drains while a loop is playing (bounded chunk walk, 32
  events/turn); the full-merge fallback and the stored-MIDI verification still
  require a fully idle transport;
* the ring reserves 32 KiB that ordinary traffic cannot consume, so a take's
  `RECA`/`RECS`/`RID` rows cannot be displaced by another take's;
* a missing telemetry row is no longer reported as a firmware contradiction:
  `EVIDENCE_MISSING` / `INCONCLUSIVE_CAPTURE` with
  `assertions.evidence_gap_issues`, while a missing note *event*, a length
  mismatch and a short span/count/bar set stay contradictions.

Full native **2,628 cases / 2,621 succeeded / 7 skipped**; `teensy41-capture-serial`
**SUCCESS**, RAM1 padding **2,072** (floor 840), variables 96,192, locals 34,880.
Python suites 15/16/52/55/26/10 green; both new native fixtures red-proved by
mutation. No upload and no device acceptance is claimed yet.

**2026-09-29 — S4f.21 — Record playback invalidation retarget defect fixed.**
Run 08's two immediate `Record stop retarget` rows led to a native reproduction:
`completeAdmittedRecordStop` launches playback through a cache reset, but
`invalidatePlaybackMergedMidiEvents` treated every pending job as a transport
Stop. The track returned PLAYING while its Record job already targeted STOPPED
and had suppressed MIDI output. The new fixture fails on that destination before
the fix. Ordinary invalidation now preserves a Record job's scratch, destination
and output barrier; explicit Stop and overdub recovery retain their existing paths.
The fixture also asserts an audible barrier state and a live same-slot overdub
successor while Record still finalizes. Focused 156/156; full native 2,619 passed /
7 skipped; capture-serial build PASS, padding 2,072, variables 96,192, locals 34,880.
Not uploaded or device-verified in that slice. Clear authorization for all slots on
all tracks was granted on 2026-09-29; that slice made no device mutation. [Cutover packet](../../openspec/changes/runtime-working-set-ownership/S4F17-CUTOVER-PACKET.md#s4f21--record-playback-invalidation-is-not-transport-stop-2026-09-29).


**2026-09-29 — S4f.22 — same-session Record evidence drain.**
Run 08 counter evidence reports seven RECS enqueues with zero transmissions,
refusals or evictions. The baseline previously waited only for host serial quiet
before stopping its reader. End-of-leg cleanup now runs before MIDI/reader close:
idempotent MIDI Stop, stopped-only fenced replies, repeated bounded trace drains,
selected finalizer-idle check and transmission-delta reconciliation against this
same log. Counter snapshots use their exact fence endpoint; historical tx counts
do not require reboot. Failed retrieval remains incomplete and preserves the
original leg abort. This does not add Stop within the measured sequence or prove
persistence completion. Device-exercised read-only on 2026-09-29:
`captures/s4f22-base-device-17` reports `record_evidence_drain.complete=true` with
RECA 1 / RECS 7 / RID 1 reconciled against that same log. The device loss mechanism
itself remains unproven, and that follow-up uploaded nothing. [Cutover packet](../../openspec/changes/runtime-working-set-ownership/S4F17-CUTOVER-PACKET.md#s4f22--same-session-record-evidence-drain-2026-09-29).


**2026-09-29 — S4f.22 — Record evidence loss accounting, native verified.**
The real capture-ring fixture disproves the previous attribution to critical
reentry/release traffic: those diagnostics are Tier-A, not critical, and cannot
evict a critical Record row. `DebugSessionCapture` now counts RECA/RECS emitter
entries, enqueue successes/refusals, transmissions and evictions alongside RID;
read-only `!CAP_IDENTITY_STATS` returns all three families. Critical append
refusal (including ring allocation failure) latches a safe-flush notice that makes
host `capture_integrity` INCOMPLETE. No retention policy changed. Four real-ring
fixtures pass, including USB backpressure and actual critical-to-critical eviction;
removing the refusal latch fails the allocation-failure fixture. Host integrity
16/16 and forwarding/identity 40/40; full native 2,618 passed / 7 skipped;
capture-serial build PASS, padding 1,992 (floor 840), variables 96,192, locals 34,880.
No upload, SD mutation or device acceptance. The actual device loss mechanism
remains unproven; next compare stopped before/after family snapshots around the
registered baseline. [Execution record](../../openspec/changes/runtime-working-set-ownership/S4F17-CUTOVER-PACKET.md#s4f22--record-evidence-loss-accounting-2026-09-29).


**2026-09-29 — S4f.17 S17.1 + S17.2 — production Record admission and stop routing landed.**
`stopRecording` and `stopRecordingToStopped` now admit the interval through
`beginDeferredRecordStopForSlot` and keep their destination behaviour at the stop edge in
`Track::completeAdmittedRecordStop` (origin alignment, truncation rewind, loop phase,
`TRACK_STOPPED_RECORDING` → PLAYING/STOPPED, and the record-stop playback handoff). A denied stop
is retained as an accepted obligation and admitted later by `driveAcceptedInteraction`, which now
dispatches on the request's capture mode, so a Record obligation runs the Record chain. The inline
`prepareRecordStop` + `commitCaptureForStop` Record route is gone from both entry points; the
pass, the `RecordPassAdded` undo entry and the once-only persistence admission come from
`stepDeferredRecordPublication`. Fixtures converted from the inline oracle now verify the deferred
chain's **content**: the three `recordNormalizationInventory` modes, the larger-than-staging take
(2 056 rows), the filtered-empty geometry reset, the exact held-note closures, and the
working-set transport-stop successor take. They exposed and the slice fixes three defects that the
synchronous route had hidden — the deferred normalization closed open tails at loop end instead of
the stop playhead's loop phase (wrong published rows); the deferred-grain prologue required the
live loop length to equal the job's, parking a completed job forever after an all-filtered take or
a successor reset; and the S4f.19 continuity gate waited on a frozen window build only the playback
owner can advance, deadlocking retirement on an empty take. The Record admission also declares its
bounded preparation estimate, read from O(1) scalars. **Strict matrix green: 45/45** (R→R, R→OD,
OD→R, OD→OD, with and without `S4F16_REQUIRE_CONTRACT`); full native **2 612 cases, 2 605
succeeded, 7 skipped**; capture-serial build **PASS, RAM1 padding 2 040** (floor 840). Mutations
red-proven: the inline Record commit (7 cases), the dropped stop-edge length stamp (8 cases), the
loop-end tail-close policy (3 cases), the job-owned-geometry exemption (1 case).
**2026-09-29 — S4f.21 S17.3a — switch guard for a retained Record stop.** `finalizeCaptureAndSelectSlot`'s
Record branch now mirrors the overdub branch's slot-scoped rule: an **admitted** Record job carries
its own identity and does not hold the active slot, so the switch commits and a different-slot
successor is not serialized behind predecessor cleanup; an **accepted-but-unbound** Record stop
refuses the switch and leaves the active slot unchanged. This closed a silent content-loss
regression introduced by S17.2 — the Record branch switched unconditionally, which advanced
`captureInterval_` past the retained interval, after which `driveAcceptedInteraction` judged the
obligation superseded and dropped the take. Fixture
`test_record_switch_respects_a_retained_stop_and_permits_an_admitted_one`; the guard-removal
mutation fails it. Full native **2 613 cases, 2 606 succeeded, 7 skipped**; strict matrix 45/45;
capture-serial **PASS, RAM1 padding 1 976**.

**2026-09-29 — S4f.21 S17.3b — job-qualified Record-stop geometry result.** `Track::recordStopGeometryForSlot`
reports `Final` (no stop in flight for that slot; the committed length is authoritative) or
`PendingStop` (an admitted job or a retained obligation still owns the slot's geometry). It is
mode-neutral and slot-scoped. `stopRecordingTrack`, `finalizeCaptureAndSelectSlot`'s Record branch
and `handleTransportStop` (there is no separate `stopAllTracks` symbol) now adopt the geometry
**once**, only when final; until then they retain a per-track obligation
(`pendingRecordGeometryMask` + `pendingRecordGeometrySlot`) consumed by
`servicePendingRecordStopGeometry` at the next dispatch boundary, and auto-align uses the new
slot-qualified `Track::setLoopLengthForSlot` because the obligation's slot may no longer be active.
This closes a real defect: the stop edge stamps the *predicted* length, so the old synchronous read
adopted a `masterLoopLength` from a take the minimum-length policy then deleted, and auto-aligned
the emptied interval with it. Fixtures
`test_record_stop_adopts_published_geometry_not_the_stop_edge_prediction` and
`test_filtered_record_take_publishes_no_master_length`; reverting the transport continuation to the
stop-edge read fails both with the defect message. `scripts/ram1_budget.json` re-based deliberately
(+32 B DTCM for the obligation, offset by moving the continuation to FLASH: padding 1,976 → 1,992;
`itcm_bytes` and `padding_floor` unchanged), reason recorded in the cutover packet.
Full native **2 613 cases, 2 606 succeeded, 7 skipped**; strict matrix 45/45; capture-serial
**PASS**.

**2026-09-29 — S4f.20 residuals (S17.4).** The deferred Record publication now queues the REVT stored
note-on scan the inline `finalizeCommitSideEffects` queued on commit, with the queue made
**slot-qualified** (`deferredRecordRevtsPending` bool → `deferredRecordRevtsSlot`, the same byte) so
it scans the publishing interval rather than `getActiveLoop()`. `invalidateCaches()` parity is
proven against the consumer contract rather than assumed: `Loop::notifyCommittedContentChanged`
(`markPassDerivedStale`) already covers the Loop's playbackOrder/visual/noteCache/eventIndex
invalidation, so the gap was the Track-level work — `CommittedMidiScratch::invalidate`, the
note-edit selected-slot invalidation and the REVT queue — now applied by
`Track::invalidateCommittedContentForSlot(slot, queueRecordRevts)` against the job's slot. Fixtures
`test_deferred_record_publication_queues_revts_for_its_own_slot` and
`test_deferred_record_publication_invalidates_the_track_committed_scratch`; removing either wiring
fails its fixture. Strict matrix **47/47**; full native **2 617 cases, 2 610 succeeded, 7 skipped**;
capture-serial PASS, RAM1 padding 1,992. The suite is now an **enforced** gate: it no longer reads
`S4F16_REQUIRE_CONTRACT`, so ordinary suite success is the strict result.

**2026-09-29 — §3 gates (S17.5).** The finalization suite is an **enforced** gate: it no longer reads
`S4F16_REQUIRE_CONTRACT`, so the R→R/R→OD/OD→R/OD→OD rotation oracle and every other assertion are
unconditional and the ordinary suite is the strict result. Nine mutations are red-proven (inline
Record commit reintroduced; stop-edge length stamp dropped; loop-end tail close; the old
live-length equality clause; retirement during a frozen-window build; the missing switch guard; the
stop-edge prediction read; the missing deferred REVT queue; the missing committed-scratch
invalidation). Accounting: compile-time pins `sizeof(Track) == 384` and `sizeof(CaptureStopJobs) ==
28936` (active under `SESSION_CAPTURE`+`PERF_TELEMETRY`) plus the post-link RAM1 gate —
`variables 96,192 / ITCM 393,216 (code 391,224 + padding 1,992) / locals 34,880`, floor 840.
Strict matrix **47/47**; full native **2 617 cases, 2 610 succeeded, 7 skipped**;
`teensy41-capture-serial` **SUCCESS**.

**2026-09-29 — §3 S17.5a — transport stop during a Record finalization.** The retarget path the
packet flagged ("`runtime->reset`, retargetFrozenCommandEpoch, the DEC-070 raw-only fallback …
assume a job exists for overdub") is now mode-aware: `requestDeferredOverdubStopToStopped` returns
after the destination change for a Record job instead of running the overdub recovery
(`restoreSupersededPreparedCompanionHistory` / `discardPreparedCompanionHistory` /
`retargetStoppedRecovery`) against roles a Record job never populated, and "transport stop wins" is
resolved at that edge (`TRACK_STOPPED` + silence + playback-state reset). Before this a transport
stop during a Record finalization left the track in `TRACK_PLAYING` while the job's destination said
STOPPED. The playback-runtime reset path deliberately does *not* force the transport state — its
retained window is still the sounding source (S4f.19) — which the playing-inventory fixture pins.
Fixture `test_transport_retarget_of_a_record_job_resolves_stopped`; removing the resolution fails it.

**§3 exit breadth.** Transport Stop is covered across the deferred stop's phases by
`test_continuous_capture` (canonical-copy, mid-flight playing retarget, live successor, B03
retarget, accept-then-transport-stop) plus this change's Record retarget fixture; clock-source
classification by `test_clock_transport_master`.

**2026-09-29 — §3 S17.5b — load/clear underneath a finalizing stop (two real defects).** An admitted
job whose interval identity was gone used to park forever on the deferred-grain prologue's LoopId
guard, holding its raw range, its capture-stop owner and its ring entry so no later stop on the track
could be admitted; `supersedeDeferredStopJob` now applies the S4f.4 transition-3 supersede rule to a
job that *did* bind, releasing everything it owns (window, staging role, borrows, owned pass chunks,
private canonical output, sealed extent one grain per turn) without touching the live interval. And
`Track::clear()` now supersedes a job that owns an unpublished take for the slot, instead of letting
the worker publish the cleared take back into the slot the user emptied — an explicit supersede, not
a LoopId bump, because clear preserves identity by design. Fixtures
`test_replaced_loop_during_a_record_finalization_retires_the_job` and
`test_slot_clear_during_a_record_finalization_does_not_republish`, plus the overdub counterpart
renamed to `test_stale_publication_is_superseded_without_persistence` (it previously encoded parking
as the expected behaviour).

**2026-09-29 — §3 S17.5c — the transport-not-running leg of a retained Record stop.** The clock source
reaches this path through `isTransportRunning()` (false both for a stopped internal transport and a
quiet external clock). `test_retained_record_stop_admits_stopped_once_the_transport_is_not_running`
retains a PLAYING-destination Record stop behind another interval's job, stops the transport before
the drive, and pins DEC-078 item 3: the stop admits STOPPED, the take still publishes and the
transport resolves `TRACK_STOPPED`; replacing the re-derivation with the stored destination fails it.
§3's exit list now has no unexercised item this environment can reach (the clock *classification* is
covered by `test_clock_transport_master`). No device acceptance is claimed from host results.
[Cutover packet](../../openspec/changes/runtime-working-set-ownership/S4F17-CUTOVER-PACKET.md).

**2026-09-28 — S4f.19 §2 — first-bar and onward playback handoff: delta work complete.**
Landed and verified (rounds 2-11): deferred return-window bind with content projection and an
atomic refusal; the native `MidiSendCapture` wire boundary; the ownership/audibility predicate
split; hot-path serve of the bound window and of onward windows built from the job's own frozen
source; the crossing-note actual endpoint; the wrap-while-finalizing fix; and continuity-gated
retirement (waits for an in-flight frozen build, releases the range-derived window before the
first release grain, keeps the copy-owning bar-1 window). Exit fixtures green: incomplete
preparation, sub-bar stop, remapped notes, held notes, traversal beyond bar one, wrap while
finalizing, stale source replacement, production-entry emission and publication/replacement
parity. Focused **269/269**; full native **2,603 passed / 7 skipped**; capture-serial PASS,
RAM1 padding **888**.
**Open, both outside S4f.19's own scope:** successor audibility (a successor record holds the
track in `TRACK_RECORDING`, so the adopt paths' `isPlaying()` skips them and the layered entry
needs a committed bridge — needs a decision to extend the layered path or to defer the
predecessor's sound until the successor's capture ends); and multi-track audibility (blocked on
the committed-bridge emission path, task 2.4). S4f.19 stays unchecked.
[Playback-handoff packet](../../openspec/changes/runtime-working-set-ownership/S4F19-PLAYBACK-HANDOFF-PACKET.md).


**2026-09-28 — S4f.19 round 2 — refusal atomicity, send-capture boundary, admission predicate.**
`projectRawCoverageToLoopLength` now preflights the held-note closure capacity with a
non-mutating pass and refuses `false` with the window **byte-identical** (fixture fills the view
role to capacity with held notes and compares every row + coverage before/after, twice;
mutation fails it). `MidiSendCapture` is a native-only boundary recording every event that
reaches `MidiHandler::sendMidiEvent`. `Track::hasAudiblePlaybackSource(slot)` is the shared
admission predicate for both playback entry points, fixture-verified in the raw → bound →
retired states and for another slot. A hot-path prerequisite was found and recorded:
`playCommittedLoopMidi`'s stale-runtime branch calls `reset(true)`, which refuses while a
pending job owns the window, so the interval cannot yet be served — it is the emission slice's
first item. **Next:** the emission assertions (expected NoteOn/NoteOff sequence, stale/unready
emits nothing, publication/replacement boundary, note crossing bar one). Focused 240/240; full
native 2,597 passed / 7 skipped; capture-serial build PASS with RAM1 padding **888** (one idle
diagnostic moved to FLASH to keep the new call site inside the floor).
[Playback-handoff packet](../../openspec/changes/runtime-working-set-ownership/S4F19-PLAYBACK-HANDOFF-PACKET.md).

**2026-09-28 — S4f.19 §2 — G0 complete; S19.2 (deferred return-window bind) landed.**
Read-only trace: PASS with named prerequisites; the deferred interval currently emits nothing
(`playMidiEvents` early-returns without `hasCommittedPasses()`), and its retained bar-1 window
was neither bound nor reachable. **Landed:** `LoopPlaybackRuntime::bindDeferredRecordReturnWindow`
binds the retained bar-1 window to the job's sealed source generation and final geometry, and
`RuntimePlaybackView::projectRawCoverageToLoopLength` transforms the raw bar (overflow NoteOn
remap, overflow non-note drop, held-note closure at the loop end, canonical re-order, coverage
clamped to the prepared-bar/final-loop intersection); `hasBoundRecordReturnWindowForPendingJob`
is the pending-owner eligibility predicate. Fixtures verify the transformed content, the
stale/unsealed refusals and the predicate; mutations fail assertions; RAM1 padding 840.
**Open:** S19.1 MIDI-entry admission (held until a native fixture can observe emission), onward
windows from the owned frozen source, and the continuity-gated retirement.
[Playback-handoff packet](../../openspec/changes/runtime-working-set-ownership/S4F19-PLAYBACK-HANDOFF-PACKET.md).

**2026-09-28 — review follow-ups on `2c71b1a3`.** Four defects fixed: the transfer-row capacity
admission is now failure-aware (`LoopEventStore::prewarmCommittedChunkIdCapacity`, exact-block
probe before growth; RAM1 padding 840; native refusal hook); the HITL census is SD-aware and
tri-state so an SD-only slot is populated and unknown stays unknown; the capture-ring drain is a
framed allowlisted `!TRACE_DRAIN` instead of a bare command the owner refuses; and scoped clear
preparation requires confirmed emptiness or explicit completion instead of treating a recognized
press plus elapsed time as success. Focused **79/79**; full native **2,592 passed / 7 skipped**;
capture-serial build PASS padding **840**; host Python suites green.
[Closure-boundaries evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-CLOSURE-BOUNDARIES-EXECUTION.md).

**2026-09-28 — S4F closure-plan Step 1 — preparation/publication boundaries repaired.**
The six source-verified boundary defects in
[S4F-CLOSURE-PLAN](S4F-CLOSURE-PLAN.md) §1 are closed: turn-local ephemeral seal (interleaved
second-track persistence admission proved); resumable staging credits with a sticky fatal deny;
resumable normalization pressure with a sticky deny that keeps the role bound (no filter-marker
re-read); the `RecordEmpty` terminal for all-dropped/all-filtered takes with inline no-content
semantics and a preserved live successor; owned transfer continuation after the store empties,
with the prewarmed ownership row sized from the take beyond the fixed 8-chunk overdub row; and
publication preflight before the irreversible commit. Focused 63/63; full native 2,591 passed /
7 skipped; capture-serial build PASS with RAM1 padding 840; six mutations red-prove the fixes.
Still open: production Record admission and the production bind site (S4f.17/S4f.21), S4f.19
playback handoff, S4f.20's deferred revts validation and `invalidateCaches()` parity.
[Closure-boundaries evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-CLOSURE-BOUNDARIES-EXECUTION.md).

**2026-09-28 — closure refinement after `bd7b3bba`.**
Source review found remaining preparation/publication boundary defects beyond the
known production-dispatch gap: finite staging credits with restart over partial
output, global ephemeral-seal scope spanning turns, normalization restart after
mutation, empty-result/transfer continuations, and geometry validation after pass
publication. Strict shared-finalization rerun still fails R→R/R→OD/OD→R (OD→OD passes).
The record HITL recipe still seeds A and sends global Stop, so the recorded missing
successor is not an isolated firmware-cause proof. Close boundaries first, then
playback and common admission/callers; finish live identity, correct recipes and
same-boot provenance before qualification. No firmware changes or new device run.
[Current closure plan](../../openspec/changes/runtime-working-set-ownership/S4F-CLOSURE-PLAN.md).

**2026-09-28 — HITL continuation — verdict-grade device sessions; L5 named as the runtime gap.**
Post-commit work closed two structural oracle gaps and corrected the census. `capture_session.py
--boot-reset` now owns a real boot inside the capture via a new stopped-only `!REBOOT` command (a
1200-baud touch is not honoured on this board — measured), so `fresh_boot` passes; the driver
writes `metadata.json` (git commit + firmware hex sha256) so `identity_pinned` passes; and the
census decodes only the requested fenced family, distinguishing a lost window from an unsettled
sweep (`device_settled`), with the selection press restricted to census-confirmed filled targets.
Record leg run (`20260928-rec-verdict3`): census 64 slots / 22 occupied / settled, slot cleared
after one `post_load_holdoff` refusal, `DROVE_LEG`; oracle `ACCEPTED_SEQUENCE_FAIL` with L1/L2/L3/L4
green — including a device-timestamped deferral window — and **L5 the named gap: no successor
start**, i.e. the production record stop is still inline (S4f.19/S4f.21). Host 47/47.
[HITL execution review](../../openspec/changes/response-driven-hitl-acceptance/EXECUTION-REVIEW.md).

**2026-09-28 — goal-round closeout (12/12) — objective not complete; precise handoff below.**
Whole tree green: focused **57/57**; host HITL suites 46/23/23/15; full `pio test -e native`
**2,585 passed / 7 skipped**; capture-serial build PASS with RAM1 padding **840**; both
OpenSpec changes strict-valid; no leftover mutation; nothing committed.
**Landed and verified this session:** firmware S4f.18 bounded Record preparation end to end
(canonical ordering, identity completion, union workspace, source staging, closure merge,
per-track preparation role, pass transfer) plus S4f.20 atomic publication, bounded retirement and
job-owned Record geometry; HITL 2.3 allowlisted fenced command forwarding; 2.1's on-demand
observation device-confirmed via a new `!LIB_OBSERVE_STATUS` diagnostic (earlier negative
corrected); 4.1's census-driven preparation device-verified with no manual step
(`DROVE_LEG` twice, including a fresh-boot run whose clear was refused once by
`post_load_holdoff` and retried to `CLEARED`).
**Open — firmware:** S4f.19 first-bar/onward playback handoff; S4f.20's deferred revts validation,
`invalidateCaches()` parity and the production Record bind site; S4f.17 production Record
admission; S4f.21 caller migration; S4f.22 integration gates; S4f.13 device acceptance.
**Open — HITL:** 2.2 identity correlation; 2.1 acceptance coverage; the 4.1 fresh-boot/shed-free
both-leg and mixed-sequence runs (the oracle needs the boot banner inside the attached capture
window, and every run must be ring-shed-free); 5.x oracle integration; 6.x qualification.
[HITL execution review](../../openspec/changes/response-driven-hitl-acceptance/EXECUTION-REVIEW.md),
[firmware evidence index](../../openspec/changes/runtime-working-set-ownership/S4F18-ROLE-ADMISSION-PACKET.md).

**2026-09-28 — response-driven HITL 4.1 — census-driven preparation device-verified.**
The read-only census now drives target preparation: an empty target skips the clear, a filled
target is selected then long-pressed, and the firmware's "deferred save in flight" refusal is
retried after a bounded wait. Device run on track 8 (A=4, B=5, no upload, no manual step):
census 64 slots / 61 occupied; slot 5 cleared on attempt 0; slot 4 aborted once then cleared on
attempt 1; both seeds committed; `DROVE_LEG`. Oracle `SETUP_INCOMPLETE` with named causes (no
fresh boot, 2 ring-shed rows, overdub identity = task 2.2) and L2/L3/L4 green. Host 46/46.
Remaining: a fresh-boot shed-free both-leg run plus the mixed sequence, and 2.2's identity.
[HITL execution review](../../openspec/changes/response-driven-hitl-acceptance/EXECUTION-REVIEW.md).

**2026-09-28 — response-driven HITL 2.1 — on-demand observation device-confirmed (earlier negative corrected).**
A new stopped-only `!LIB_OBSERVE_STATUS` diagnostic (emitted directly, bypassing the capture ring)
shows `phase=3,ready=1,collected=64,emitted=64` after `!LIB_OBSERVE`, and the managed capture now
returns the complete `LIBOBS,B…E` family — `closed=True, rows=131, slots=64` with real per-slot
occupancy. The earlier "does not serve" result was a ring-flush visibility artifact; the host
drains while it waits. Host **23/23**; full native and capture-serial build re-run; the diagnostic
image is flashed. Remaining for 2.1 is acceptance coverage and 2.2's identity correlation.
[HITL execution review](../../openspec/changes/response-driven-hitl-acceptance/EXECUTION-REVIEW.md).

**2026-09-28 — response-driven HITL 2.1 investigation — the on-demand observation does not serve.**
Attributed negative result, device-reproduced read-only. With task 2.3's transport in place, a
managed session forwards `!LIB_OBSERVE` and the device answers
`[StorageManager] library observation requested` with both request fences in the log — so the
command reaches the handler. But no `LIBOBS` family is emitted on a **settled, long-running
boot**: 30 s and 60 s read windows, including a run draining every 2 s, saw none. The code path
should serve (unconditional `SESSION_CAPTURE` idle-owner service tail, `beginCollection` sets
Collecting, no production reset), and the one run that did emit was captured while the boot
restore scan was active — hypothesis: serving is tied to in-flight restore work. Next: expose
the observation's per-turn counters behind the capture gate to separate "never scheduled" from
"emitted but not flushed". Task 2.1 remains open; task 2.3 stays complete. No SD mutation,
upload or acceptance claim.
[HITL execution review](../../openspec/changes/response-driven-hitl-acceptance/EXECUTION-REVIEW.md).

**2026-09-28 — response-driven HITL 2.3 — allowlisted command forwarding verified.**
`capture_session.py` gained an opt-in `--command-stdin` channel: it refuses malformed requests
and anything outside the read-only allowlist by name, and frames an accepted request with two
distinct 32-char `!TRACE_FENCE` tokens — a device-learned constraint, since 8-char tokens are
silently ignored by the firmware. `serial/protocol.py` now owns request formatting, receipt
parsing, exact fence-row matching, the fenced window and the self-delimiting
`LIBOBS,B`…`LIBOBS,E` family; `ManagedCaptureSession` owns `stdin=PIPE`, a
one-outstanding-request guard and the response helpers. Host suite **23/23**; the other HITL
suites re-ran green (42/3/23/15); capture-serial build and full native (2,585 passed /
7 skipped) confirm the firmware tree is unchanged. Device-confirmed: the forwarded request and
both fences appear in the session log. The `LIBOBS` family did not settle within 60 s on that
idle boot, so the decoder returned loss rather than an empty census; observation serving is
task 2.1/2.2 and the next HITL slice. No SD mutation, upload or acceptance claim.
[Command-forwarding evidence](../../openspec/changes/response-driven-hitl-acceptance/COMMAND-FORWARDING-EXECUTION.md).

**2026-09-28 — S4f.20 — job-owned Record geometry receipt verified.**
`CaptureStopJob` now owns the interval's before/after `UndoLoopGeometry` and LoopId, captured at
the Record bind by `Track::captureRecordGeometryReceipt` (which consumes the Track's inline
baseline so a successor cannot rewrite it). `TrackUndo::pushRecordPassAdded` gained a job-owned
overload, and the deferred publication fails closed without a matching receipt and stamps the
after geometry from the job's own loop. The reviewed ring pin is re-based 28808 -> 28936 B
(+32 B per job). The fixture seeds a successor record baseline after the bind and proves the
published entry still carries the job's geometry; two mutations fail assertions (reading the live
baseline publishes the seeded value; a missing receipt denies). Focused **33/33**; full native
**2,585 passed / 7 skipped**; capture-serial build passes with RAM1 padding **840**. S4f.20 stays
unchecked for the deferred revts validation, `invalidateCaches()` parity and the production bind
site. No dispatch, upload, device acceptance or task checkoff.
[Geometry evidence](../../openspec/changes/runtime-working-set-ownership/S4F20-RECORD-GEOMETRY-EXECUTION.md).

**2026-09-28 — S4f.20 — mode-correct Record atomic publication and retirement verified.**
The deferred Record route now publishes end to end: `stepDeferredRecordPublication` performs the
bounded canonical handoff with a local receipt, then the job-slot-keyed effects
(`RecordPassAdded` undo entry, display staleness, once-only persistence) at the new
`RecordPublished` phase; `stepDeferredRecordRetirement` releases the frozen extent in bounded
grains and returns the ring entry. Chain: `ReservedClosure -> RecordSourceStaging ->
CanonicalOrder -> RecordPrepared -> RecordPassReady -> RecordPublished -> retired`. The inline
side-effect owner was evaluated and rejected (it drags the note-edit surface in, and two of its
Record effects are not job-correct). Focused **68/68** (load 35 + shared-finalization 33);
mutations M1/M2/M3 fail assertions; full native **2,585 passed / 7 skipped**; capture-serial build
passes with RAM1 padding **840** and the ring pin unchanged at 28,808 B. S4f.20 stays unchecked:
job-owned Record geometry (needs the production bind site), the deferred revts validation and
`invalidateCaches()` parity remain. No dispatch, upload, device acceptance or task checkoff.
[Publication evidence](../../openspec/changes/runtime-working-set-ownership/S4F20-RECORD-PUBLICATION-EXECUTION.md).

**2026-09-28 — S4f.20 — bounded Record pass transfer verified.**
The normalized private output is staged into the job-owned transfer receipt
(`beginPreparedCapturePassFromActiveAdmission` + `stepPreparedCapturePass`) through the same
mode-neutral mechanism the overdub route uses, with a new terminal
`PendingStopPhase::RecordPassReady`. The chain is
`ReservedClosure -> RecordSourceStaging -> CanonicalOrder -> RecordPrepared -> RecordPassReady`.
Nothing is published: the atomic publication transaction (pass + `RecordPassAdded` undo with
job-owned geometry + audible-source handoff + once-only persistence + retirement) consumes that
terminal and is S4f.20 completion. Focused shared-finalization **33/33**; mutations M1 (ready
phase) and M3 (receipt re-begun every grain) fail assertions, and M2 (the phase guard) is
recorded as individually unobservable rather than claimed. Full native **2,585 passed /
7 skipped**; capture-serial build passes with RAM1 padding **840** and the ring pin unchanged at
28,808 B. No dispatch, upload, device acceptance or task checkoff.
[Pass-transfer evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-RECORD-PASS-TRANSFER-EXECUTION.md).

**2026-09-28 — S4f.18 — per-track Record preparation role and normalization step verified.**
The admission slice landed. `RecordPreparation::Role` (composition cursor + 24,576 B union
scratch, bound by job slot/LoopId/interval/generation) lives in the externally allocated
per-track ring; `sizeof(CaptureStopJobs)` is re-based 3072 -> 28,808 B (8 x 25,736 B =
205,888 B PSRAM; RAM1 padding stays **840**). `stepDeferredRecordNormalization` validates
mode/phase/owner/geometry/runtime generation/closure, tops up credits, binds once and drives
one bounded grain over the merged store, advancing to the new terminal `RecordPrepared`.
The Record worker branch dispatches closure -> staging -> normalization -> prepared. The
integrated fixture proves the pre-normalization order (Off, Off, On, On) becomes canonical
(On, On, Off, Off) with identities preserved; mismatched-binding and terminal-phase denials
leave the store untouched. Focused shared-finalization **33/33**, with preparation and
staging **57/57**; two mutations fail assertions. Full native **2,585 passed / 7 skipped**;
capture-serial build passes. No dispatch of record admission, upload, device acceptance or
task checkoff; publication (S4f.20) is next.
[Preparation-role evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-PREPARATION-ROLE-EXECUTION.md).

**2026-09-28 — S4f.18 — Record closure merge and staging phase verified.**
The frozen take is staged into the same private output after the terminal closure Offs (the
composition orders the store, so append order is not semantic), so the closure consumer is
unchanged. `PendingStopPhase::RecordSourceStaging` was appended to the enum; the Record
closure step now sets its own next phase in both completion places (the generic
`consumeClosureGrain` default is the overdub route); and `stepDeferredRecordSourceStaging`
validates owner/generation/closure/raw-range, tops up credits, binds the per-track cursor
under a receipt and drives one bounded grain to `CanonicalOrder`. Focused
shared-finalization **32/32**, affected suites **166/166**; two mutations fail assertions.
Full native **2,584 passed / 7 skipped** (the full-suite gate caught a missing harness
include in three firmware-boundary suites, now fixed). Capture-serial build passes with RAM1
padding **840**; the reviewed ring size pin is re-based 2984 -> 3072 B (+88 B per track,
externally allocated). No dispatch, upload, device acceptance or task checkoff.
[Closure-merge evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-CLOSURE-MERGE-EXECUTION.md).

**2026-09-28 — S4f.18 — bounded Record source staging verified; packet corrected.**
G0 reconnaissance falsified the admission packet's premise: the closure consumer requires an
empty output store and writes only terminal held-note Offs, so the take rows were never
staged. Corrected sequence: (1) source staging, (2) closure merge, (3) per-track preparation
role + normalization, (4) publication. Step 1 landed as `RecordSourceStaging::Cursor`, which
copies the frozen `CaptureRange` one bounded row per unit through the existing
`projectRecordStopTick` policy under an ephemeral seal — no allocation, no directory growth,
fail-closed on a stale source or exhausted credits. Focused **7/7**; two mutations fail
assertions. Full native **2,583 passed / 7 skipped**; capture-serial build passes with RAM1
padding **840** and no linked ITCM growth. No behavior dispatch, upload, device acceptance or
task checkoff.
[Source-staging evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-SOURCE-STAGING-EXECUTION.md).

**2026-09-28 — S4f.18 — preparation workspace unioned; role-admission packet pinned.**
Wrap repair and minimum-length filtering run in disjoint phases, so the composition
`Workspace` is now a union scratch role of **24,576 B** (was 40,960), pinned by a
`static_assert`, and `FilterReset` re-discards the tail cursor. Focused composition
**17/17**; capture-serial build passes with RAM1 padding **840** and no linked ITCM growth;
full native **2,576 passed / 7 skipped**. The next slice is pinned in the
[role-admission packet](../../openspec/changes/runtime-working-set-ownership/S4F18-ROLE-ADMISSION-PACKET.md):
one externally allocated 25,696 B preparation role per track (8 total, 205,568 B PSRAM),
bound by job identity, driving one bounded grain and advancing only to the future
publication phase. No behavior edit, no upload, no task checkoff; resource admission and the
shared worker/publication handoff remain open.

**2026-09-28 — S4f.18 — bounded Record identity completion verified.**
`LoopEventStore::assignPrivateNoteId` writes one NoteId only to an unmarked private
NoteOn row that has none, and refuses an overwrite, a non-note row, a missing ordinal or
an invalid id. `RecordPreparationCursor` completes missing NoteOn identities in canonical
order between the sort and wrap repair (matching `sealCapture`) through a borrowed
`NoteIdSource`, and still denies `IdentityIncomplete` without one. Focused composition
**17/17**, with the private filter/order and record tail/minimum suites **47/47**; full
native **2,576 passed / 7 skipped**. Capture-serial build passes with RAM1 padding **840**
and no linked ITCM growth (the role is not dispatched). ARM cursor 1,104 -> 1,120 B;
workspace 40,960 B unchanged. Three mutations fail assertions and all sources were restored
byte-for-byte. Resource admission and the shared worker/publication handoff remain open;
record stop is still inline. No upload, device acceptance, new decision or parent checkoff.
[Identity execution evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-IDENTITY-EXECUTION.md).

**2026-09-28 — response-driven HITL 4.1 (partial) — scoped stopped preparation device-exercised.**
`scripts/hitl/flows/stopped_preparation.py` clears one authorized target through the
production loop-button long press and classifies only firmware evidence (`CLEARED`,
`ALREADY_EMPTY`, `REFUSED`, `ABORTED`, `NOT_SELECTED`, `UNCONFIRMED`); it is integrated
before every seed in `scripts/hitl_accepted_sequence.py` and aborts with
`PREPARATION_FAILED` before any record gesture. Host accepted-sequence **42**, corpus **3**,
retention plan **23**, shutdown **15**. On the flashed image (`bed90c04` + dirty,
hex `451000aa…`, RAM1 padding 840) the record leg cleared slot 4
(`MIDI: Clear selected slot 4`) and drove; its oracle is `SETUP_INCOMPLETE` (no fresh boot)
with L4/L5 failing — the known inline-record gap. The overdub leg reported `ALREADY_EMPTY`
for empty slot 5 and `NOT_SELECTED` for populated slot 4, aborting before mutation.
Measured bounds for the rest of 4.1: only the selected slot clears, a short press is a
record toggle (it armed and started the transport), and same-button presses inside the tap
window degrade a long press (900 ms requested, 458 ms measured). No upload between legs,
no manual clear; task 4.1 open.
[Execution review](../../openspec/changes/response-driven-hitl-acceptance/EXECUTION-REVIEW.md).

**2026-09-28 — S4f.18 — bounded Record preparation composition verified.**
`RecordPreparationCursor` now composes the existing private owners over one job-owned
prepared store: canonical (tick, NoteOff < NoteOn < other) order, reserved wrap repair
with a canonical re-order, then minimum-length decisions and private compaction, all in
bounded per-grain units. Missing NoteOn identity, a stale preparation generation and a
missing reservation deny by name; the legacy store helpers' inventory is matched over
eight randomized differential rounds. Focused **12/12**, existing primitives **30/30**,
full native **2,571 passed / 7 skipped**. Capture-serial build passes with RAM1 padding
**840**; the role is not dispatched, so no linked ITCM growth. ARM `Workspace` 40,960 B /
cursor 1,104 B, caller-owned; order cursor 864 -> 872 B for the opt-in canonical key.
Four mutations fail assertions and all sources were restored byte-for-byte. Bounded
missing-identity completion, resource admission and the shared worker/publication handoff
remain open. No upload, device acceptance, new decision or parent checkoff.
[Composition execution evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-COMPOSITION-EXECUTION.md).

**2026-09-28 — S4f.18 — bounded private canonical filtering verified.**
The store now applies exact rejected-row pairs to private prepared output and
compacts retained rows in bounded turns without a take-sized bitmap or copy.
Ten fixtures cover real minimum-policy parity, long inputs, frozen-source isolation,
late persistence/borrowing, stale receipts and tail retirement; six counterexamples
are rejected. Final focused **24/24**, full native **2,559 passed / 7 skipped**.
Capture-serial build and strict OpenSpec validation pass; RAM1 padding **840 bytes**.
ARM cursor **56 bytes**, no store or resident-role growth. Record ordering/policy
composition, role admission and production integration remain open. No upload,
device acceptance, new decision or parent checkoff.
[Filter execution evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-FILTER-EXECUTION.md).

**2026-09-28 — S4f.18 — bounded stable private-chunk ordering verified.**
The store owner now sorts private dense prepared chunks in place through bounded
binary comparisons and rotation swaps, preserving equal-tick event identities.
No whole-take event/index scratch; ARM cursor **864 bytes**, no store growth.
Owner/pool checks, exclusive references and non-allocating persistence-state checks
protect the work. Frozen raw input stays untouched beside separate sorted output.
Six fixtures cover long/mixed inputs, metadata and denial; four mutations fail.
Final native **2,549 passed / 7 skipped**; focused ordering/persistence **15/15**. Firmware
build passes with RAM1 padding **840 bytes**, restored after moving the newly used
state accessor to FLASH. OpenSpec validates. No production Record route switch,
upload, acceptance or parent checkoff. Canonical filtering and private-output
publication/persistence integration remain open.
[Execution evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-ORDER-EXECUTION.md).


**2026-09-28 — S4f.18 — canonical compaction retirement bound corrected.**
The existing ordered-source rewrite erased surplus chunk IDs from the middle of
its directory, shifting the remaining suffix in one maintenance grain. It now
releases/pops one tail reference, preserving every retained directory entry.
New fixtures reproduce the old shift and catch wrong-reference retirement while
pinning exact final rows, empty/partial/full chunk cuts and stale-owner denial.
Focused **196/196**; full native **2,543 passed / 7 skipped**. Capture-serial build passes
with RAM1 padding **840 bytes**; strict OpenSpec validation passes. No layout
change, upload, HITL acceptance or parent checkoff. The active finalizer retains
its canonical-copy route. Record ordering, canonical
filter writing and production integration remain open.
[Execution evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-COMPACTION-EXECUTION.md).


**2026-09-28 — S4f.18 — bounded minimum-length decisions verified.**
`CaptureIncrementalSanity::MinimumLengthCursor` consumes one ordered row per call
and retains short-pair source ordinals until acknowledged. Eight fixtures preserve
strict thresholds, retriggers independent of NoteId, channel separation, stale
receipts and retry behavior; 64 differential cases match the real helper's retained
inventory. All 2,048 MIDI keys / 4,096 rows pass, and three policy mutations fail.
ARM cursor **48 bytes**, borrowed workspace **16,384 bytes**; no resident allocation
or production dispatch added. Full native **2,541 passed / 7 skipped**; capture-serial build
and strict OpenSpec validation pass, RAM1 padding **840 bytes**. This is a decision
primitive: sorting, canonical compaction and Record admission/playback integration
remain open. No upload, hardware acceptance, new decision or parent checkoff.
[Execution evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-MINIMUM-EXECUTION.md).


**2026-09-28 — S4f.18 — bounded Record tail-repair prerequisite verified.**
`LoopStopFinalize::TailRepairCursor` now preserves the existing synthetic-Off
policy through one-row input, bounded lane reset/enumeration and acknowledged
output retry. Six fixtures include all 2,048 MIDI keys / 4,096 source rows,
80 real-helper differential cases and three caught policy mutations. ARM cursor
is **48 bytes**; caller-owned scratch is **24,576 bytes**, with no resident field
or production Record dispatch added. Full native **2,533 passed / 7 skipped**; capture-serial
build passes with RAM1 padding **840 bytes** and unchanged firmware footprint.
OpenSpec strict validation passes. Record sorting, minimum-length filtering,
role admission and playback/caller integration remain open. No upload, HITL
acceptance, task checkoff or new decision; connected parent tasks remain open.
[Execution evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-TAIL-EXECUTION.md).


**2026-09-28 — S4f.18 — resumable job-owned pass staging verified.**
The deferred worker now constructs its pass directly in the job, one chunk-reference
move or ordering swap per turn. Partial receipts cannot be borrowed or published;
the Loop producer slot stays available to a successor. A 282-row fixture proves
exact content, live successor survival, one undo/persistence handoff and cursor
retirement. It also exposed unused undo estimates blocking atomic transfer: those
retire only after successful collection and protected publication preparation;
denied collection retains its obligation and refused publication retries its entry.
Focused **31/31**; three mutations rejected; full native **2,527 passed / 7 skipped**.
Capture-serial build passes, RAM1 padding **840 bytes**. The externally allocated
four-job ring grows **2,728 → 2,984 bytes**; Track/Loop/runtime sizes stay unchanged.
Record normalization/admission and connected playback/caller work remain open.
No parent checkoff, upload or device acceptance.
[Staging evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-STAGING-EXECUTION.md).

**2026-09-28 — S4f.18 — bounded prepared-pass chunk transfer verified.**
The existing store-to-committed-directory transition supports resumable reference
moves and order-restoring swaps. Each grain respects its quantum, allocates no
chunks or directory storage, and preserves source/destination ownership on denial.
It requires source/persistence prewarming and rejects stale owner/pool receipts.
Fully prewarmed synchronous callers reuse it; legacy callers retain their path.
Focused **40/40**, five mutations rejected, full native **2,525 passed / 7 skipped**.
Cursor is **64 bytes ARM / 96 bytes native**; no new Track/job/runtime field.
Capture-serial build and layout pins pass; RAM1 padding remains **840 bytes**.
Record normalization and worker integration remain open; no upload, hardware
acceptance or parent task checkoff.
[Transfer evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-TRANSFER-EXECUTION.md).

**2026-09-28 — S4f.18 — Record normalization oracle verified.**
Five real-stop fixtures pin overflow NoteOn projection, tail repair, minimum-length
policy, filtered-empty geometry and all 2,056 rows of a Record take beyond overdub
staging. Playing and stopped destinations produce the same expected inventory.
The shared suite now links real playback reanchoring and ledger clearing through
an independent harness opt-in; other suites retain their existing boundaries.
Focused **29/29**; four policy/capacity mutations are rejected; full native
**2,518 passed / 7 skipped**. No production firmware changed, build/upload or
hardware timing claim. Bounded normalization/pass construction and connected
admission/playback integration remain open; no parent task checkoff.
[Normalization evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-NORMALIZATION-EXECUTION.md).

**2026-09-28 — S4f.18 — job-owned Record closure output verified.**
A bounded consumer appends one held-note Off per grain to reserved canonical output,
using the job's saved NoteId and geometry. It preserves a live successor, retains
obligations on denial, and rejects stale raw/output/job identities. Five new fixtures
include 257 held notes and exactly-once retry; four guard-removal mutations fail.
Full native **2,513 passed / 7 skipped**; focused **24/24**; capture-serial build passes.
The owner+generation receipt adds **8 bytes per allocated playback runtime** (6,352 bytes
on ARM); Track/job layouts and static RAM remain unchanged, RAM1 padding **840 bytes**.
Record still finalizes inline. Normalization, pass construction, admission and playback
handoff remain open; no parent checkoff, upload or device acceptance.
[Record output evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-OUTPUT-EXECUTION.md).

**2026-09-27 — S4f.20 — deferred publication persistence handoff implemented.**
Successful deferred transfers now use the existing persistence helper with their validated
job slot, including stopped and superseded-source paths. Cleanup does not replay the request.
A stale Loop object identity also blocks publication. Four transition fixtures cover these
paths with a live successor and a different selected track; the load fixture observes its
legitimate outgoing request separately from real storage admission tests.
Full native: **2,501 passed / 7 skipped**; final publication suite: **15/15**.
Strict contract now has three record-related failures; OD→OD passes with persistence.
Final capture-serial build passed with unchanged RAM1
padding **840 bytes** and no field growth. Full regression/strict contract evidence is recorded in
[the execution packet](../../openspec/changes/runtime-working-set-ownership/S4F20-PERSISTENCE-EXECUTION.md).
No upload or device acceptance. Record still finalizes inline; S4f.17–S4f.21 — connected
shared finalization remain open, including record preparation, playback, undo and callers.

**2026-09-27 — connected S4f.17–S4f.21 implementation in progress; identity/length prerequisites verified.**
Record and overdub starts now share interval stamping; invalid Record starts reject before
slot reset. Accepted stops bind LoopId and Capture mode and supersede changed identities.
Record-length calculation uses a maintained maximum tick, reusing the existing Loop scalar.
Production fixtures cover duplicate/invalid starts, pool-denied preflight/recovery, stale
LoopId/mode, and out-of-order/removal length cases. Record stop still publishes inline;
bounded source preparation, first-bar/onward handoff, publication/undo and caller migration
remain open. Full native: **2,497 passed / 7 skipped**; focused: **11/11**.
Capture-serial build passed, RAM1 padding **840 bytes**. Strict contract retains four
expected implementation-gap failures. No task completion or device acceptance claimed.
[Connected execution evidence](../../openspec/changes/runtime-working-set-ownership/S4F17-EXECUTION.md).

**2026-09-27 — S4f.17 — common admission preflight found a sequencing dependency.**
Record admission cannot be enabled independently of the bounded record worker, source handoff,
record publication and manager continuations assigned to S4f.18–S4f.21. The existing worker
requires overdub staging; managers still assume the void record stop published synchronously.
[Dependency trace and proposed implementation boundary](../../openspec/changes/runtime-working-set-ownership/S4F17-EXECUTION.md).
Recommended: implement the connected firmware path, preserving individual verification gates.
User approved the connected S4f.17–S4f.21 implementation. Work is in progress; each task retains its verification gate and remains open until verified.

**2026-09-27 — S4f.16 — production finalization fixtures complete.**
Real record stop/cold helpers, deferred worker, undo and storage admission now share the drain
suite's production closure. Four record/overdub rotation baselines pass; explicit contract mode
fails on missing record deferred ownership, missing record interval stamp, and missing deferred
worker persistence handoff. These are implementation gaps, not accepted alternative behavior.
Full native **2,490 passed / 7 skipped**; final focused **4/4**; strict OpenSpec valid.
[Fixture evidence and bounds](../../openspec/changes/runtime-working-set-ownership/S4F16-EXECUTION.md).
Next: S4f.17 — common interval and stop admission. S4f.20 — publication/undo/persistence must
also close the exposed persistence gap. No firmware behavior or device changes;
S4f.13 — accepted-sequence acceptance remains open.

**2026-09-27 — shared record/overdub finalization was already intended; firmware contract added.**
Audit confirms runtime proposal role budgeting, accepted-interaction specs and design Revision 23
item 6 already require record A to finalize alongside a live successor. The earlier recommendation
to weaken the record oracle is withdrawn. The implementation gap is record's inline route and
its missing common interval, source, geometry and undo integration.
[Shared capture finalization contract](../../openspec/changes/runtime-working-set-ownership/SHARED-CAPTURE-FINALIZATION.md),
design Revision 25, DEC-093 — shared capture finalization and S4f.15–S4f.22 now define the
firmware work. Existing Track/job/worker owners are reused; first-bar and onward coverage,
mode-correct undo/persistence and all four record/overdub transitions are required. Both
OpenSpec changes strictly validate. Contract only: no firmware/device changes or new test run.
The prior HITL fixture results remain valid; S4f.13 — accepted-sequence acceptance stays open.
At that handoff the next slice was S4f.16 — production finalization fixtures; its completion is recorded above.

**2026-09-27 — response-driven HITL implementation started: production input regressions verified.**
Tasks 1.2 — historical failure corpus and 1.3 — production gesture fixtures are complete
(**2/21**). Four attributed failed-run excerpts remain non-PASS. Real button timing has
12 native cases; USB and DROID dispatch both reach the real long-press clear action in
native storage fixtures. A momentary-dispatch mutation fails 11 assertions. Python accepted
sequence/corpus: **33 passed**; full native: **2,486 passed / 7 skipped**.
Task 1.1 — instrumentation contract is blocked: record-stop seals/publishes inline; the
planned predecessor capture-stop window belongs to overdub. The user has been asked whether
to separate the record/overdub oracles or retain record overlap with firmware scope. No
acceptance criterion changed pending that choice. No firmware edit or device action.
Evidence: [execution review](../../openspec/changes/response-driven-hitl-acceptance/EXECUTION-REVIEW.md).
S4f.13 — accepted-sequence acceptance remains open.

**2026-09-27 — response-driven HITL proposal for S4f.13 — accepted-sequence acceptance.**
The user specifies DROID MIDI controls, continuously playing acceptance windows, no manual
reference pass, and scoped clearing/repetition without reboot. Code and existing failed
captures ground the new [response-driven HITL change](../../openspec/changes/response-driven-hitl-acceptance/proposal.md).
Its design separates gesture recognition, accepted intent and execution; adds bounded
identity observations; verifies RAM/SD preparation and persistence; and requires production
button-timing regressions plus three consecutive device passes per leg on one boot.
All planning artifacts validate; implementation and device qualification remain open.
Earlier host-only driver edits are preserved. No device access, clear or upload in this
planning slice. DEC-092 — response-driven HITL records the user constraints.

**2026-09-27 — S4f.13 — accepted-sequence driver corrected, host verified only.**
The overdub leg now reuses the proven retention prefix: seed B then A, start A’s overdub,
press B once to stop A and switch, start B’s overdub, press B once to stop it. The former
separate A stop removed the capturing-state precondition and entered the SD-aware playback
queue. Slot 8 is a separate record successor in the retention run, not a substitute for an
overdub target. Unexpected queued/reaffirmed playback aborts before the next gesture;
unattributed Playback-started rows cannot discharge that guard. Abort results survive managed
shutdown into the JSON report and nonzero exit. Both intervals receive notes; starts and seed
commits are confirmed. Host tests: accepted sequence 30, retention plan 23, shutdown 15;
old guard mutation fails the new driver regression. OpenSpec strict validation passes.
The oracle no longer treats wrong-slot/late starts or window-open-as-release as acceptance;
record-only RECA/RECS cannot establish overdub identity, and successful stop counts cannot
prove input press counts. Those evidence links explicitly remain unknown. No firmware,
upload, device run, SD clear, push or PR. S4f.13 — accepted-sequence acceptance remains open.
Record: active change `ARCHITECTURE-REVIEW.md`, “S4f.13 — accepted-sequence driver correction”.

**2026-09-27 — S4f.3's no-concession device gate CLOSED: `RETENTION_PASS`, exit 0, zero concessions.** The
re-run the first campaign called for, on the same authorized targets (**track 8, A=4, B=5, C=8**) with both
fixes in the flashed image (`675a115a`), identity `git_commit 675a115a8225` / `firmware_hex_sha256
e76e1818e069`. **20/20 checks `ok`, `concessions: []`**, every link on its own evidence: L1
`DIAG,reentry_window_open,3,1117,2304`; the exposure gate **bounded at 531.1 ms** from A's window opening to
the successor's denial; L2 the **device-timestamped** `DIAG,reentry,deny,1321,2,4,1` (slot 4, interval 2)
rather than a fallback to `UNKNOWN`; L3 the retained row for slot 7 between denial and release; L4
`canonical_release,3,40,2304,8`; L5 `DIAG,reentry,bound,1717,2,4,0` on the **denied** interval; L6 exactly one
`RECA,7` after the release; L7 note 67 in storage **and** effective agreeing on the full MIDI fields; L8 a
clean bounded follow-up. Content pipeline: declared `{'effective#0': 2, 'record#0': 2}` match parsed,
`storage_matches_effective: true`. **Both fixes are visible in the result** — the `stop_job_pending` identity
emission is what turned L2 from `UNKNOWN` into an identified denial, and the bounded gate reports 531.1 ms
where the old release-turn formula would have said 34.8 ms. **Three host defects were found by running and
fixed rather than worked around:** `exit_code` was a computed property that `report_dict` never serialized, so
every review file omitted the authoritative code; the oracle's CLI is **0-based** for slots *and* tracks while
the HITL driver's is 1-based (passing the driver's numbers makes a correct run look like it had no denial at
all — now documented in the docstring, in each argument's help, and converted explicitly for the denial row);
and the first cut of the exposure fix subtracted host seconds-since-capture-start from device micros-since-boot,
producing a *negative* window, so the width is now reported ungated instead. **S4.1's lease-release row is
still owed, and its cross-track route is now retired as structurally impossible:** a lease is acquired only
when `prewarmPlaybackForSlot` gets `prepareCapture == true` (the record arm and the boot-cold overdub start —
boot's pass uses `false`), and the cross-track sweep excludes the capturing track, so only a **same-track
switch after retirement** can fire it. Record:
[`ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)
§ "S4f.3 no-concession device gate — CLOSED" and § "S4.1's lease-release device row: the cross-track route is
a structural dead end". Oracle tests 43/43; full native 2,469 passed / 7 skipped; capture-serial SUCCESS with
`padding 856` byte-identical; device shutdown `complete`, no latch.

**2026-09-27 — first authorized device campaign for S4.1's lease row and S4f.3's no-concession run: executed
on the authorized targets, `SETUP_INCOMPLETE`, nothing accepted.** The two device legs the change still owes
were attempted in one session, under the user's explicit authorization. A read-only library census
(`hitl_boot_census.py --library-observe`, which resets by re-upload and then sends only `!LIB_OBSERVE`) came
first and changed the plan: the device holds **60/64 committed slots** and **no track has three empty slots**
(track 5, the documented scratch track, has none), so the user authorized clearing exactly one slot. The run
used **track 8, A=4, B=5, C=8**; every clear was identity-grade (the firmware's own `(active_slot=N)`, not a
bare `Track cleared` marker) and a second census re-verified all three targets empty before any gesture.
Identity `git_commit 5849d620ebc5` / `firmware_hex_sha256 adbb079fd552`; fresh boot, no ring eviction, complete
tail, tempo 120.0 BPM verified by read-back, seed content confirmed. **The device drove the whole chain** — A's
overdub, the switch to B, B's overdub, B's stop denied on capacity, C's switch/start retained, capacity
released, C started once and recorded — and the oracle finds all eight links present **in order**, but returns
`SETUP_INCOMPLETE` (exit 3), so **no acceptance is claimed**. Three findings are recorded instead of worked
around: (1) `capacity_denied_window_wide_enough` reports **34.8 ms** against its 150 ms floor, but it is the
last check still taking its edges from the **deferred `#CAP` rows** although the same file's documented rule is
to prefer the timely human rows — both releases flushed in one burst 48 µs apart, and the human rows put the
window opens **1.56 s** apart, so the check cannot currently tell "too narrow" from "flushed late";
(2) `Track::stopOverdubbing`'s `headJob->admission.pending` branch logs **no slot/interval** while its `else`
branch does, so L2's identity link is missing for exactly the denial reason this scenario produces;
(3) **zero `DIAG,ws_release` rows** in a completed A/B/C chain — measured and consistent with the G0 trace, so
S4.1's device row is still owed and the route that would fire it is now named (a start on another track while a
lease is still held, memory pressure, or a same-track switch *after* retirement). A **reading trap is recorded
because it cost the first interpretation**: the run log carries `Selected track 8` (1-based, user-facing) beside
`[TRACK] Track 7 armed` (the arm site logs the 0-based `trackIndex`); only the census diff settles which track
was written, and it shows the writes landed exactly where authorized. Host-tooling hygiene landed with it: the
driver's `--no-clear`/`--census` help text claimed targets are cleared by default (they are not) and an
unreachable `if False:` copy of the census guard was removed. Full record, timeline, reproduction commands:
[`ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)
§ "S4.1 device leg + S4f.3 no-concession run". Host tests green (`test_retention_handoff_plan` 23,
`test_retention_probe` 42); device shutdown verified `complete`, no latch.

**2026-09-26 — F4 / DEC-091: a failed workspace completion is discharged, not retried at the queue head.**
Firmware change, found while building the S4.1 persistence fixture. A persistent failure in the workspace
completion (`patchLastActiveUnix`, the bundle CRC refresh, `workspace.bin`) requeued the `FinalizeWorkspace`
item **at the queue head**; since those sub-steps are deterministic the retry could never succeed, so every
other persistence item waited behind it and `hasDeferredSaveWork()` stayed true for the rest of the boot —
which refuses the save-new-set pre-flight and makes the read-only slot-payload observation answer `busy`
(`LibraryObservationSession`). The failure is now **terminal**: `abandonPersistenceWorkItem`
(FLASH-resident) reports it once (`PERS,result,…,"failed"`, `failedAtMs`) and lands the item in the queue's
existing tombstone via `markItemPersisted`, leaving the per-slot dirty set — the retry the persistence
contract names. Fixture `test_f4_a_failed_completion_is_discharged_and_releases_the_queue`
(`test_storage_drain_restore`) holds the card in write-refusal through the whole assertion window and asserts
the discharge, the reported failure, the guard clearing **and staying clear**, the preserved dirty set, and
the completing retry; **red-proved** by restoring the front-requeue (*Expected 0 Was 1 … queue=1*), with a
**false green in the fixture's first cut recorded** (a single-turn fault self-heals on the retry, which is
what the requeue is for). **RAM1:** `code 392,360 / padding 856`, byte-identical — the named queue API the
first cut added cost 256 B of ITCM and was **withdrawn rather than widening the floor**. **Two consequences
recorded rather than hidden:** the blocking drain now reports `Failed` instead of
`TemporarilyBlocked/waiting_commit_gate` in the mixed shape where the completion fails *and* a restore is
commit-gated (the S2b.7 parked-instance fixture was re-premised on the drain's ownership transfer and pins
the corrected verdict), and the same front-requeue shape remains for **body/bundle** item failures where the
retry is the content-durability intent (a separate decision). Focused 15/15; full native **2,469 passed /
7 skipped**; capture-serial SUCCESS. **Device evidence owed:** an SD fault path with no fault injection.
Record: [`ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)
§ "F4 — a failed workspace completion is discharged"; decision: `DEC-091`.



**2026-09-26 — S4.1 working-set lease release on a same-track switch: verified, no firmware change.** The
last behavior leg of S4.1. Three fixtures in `test_runtime_working_set_ownership` (23/23) drive the
production switch commit (`TrackManager::finalizeCaptureAndSelectSlot` → `Track::resetPlaybackStateForSlot`
→ `invalidatePlaybackMergedMidiEvents` → `releaseEligiblePlaybackRuntimeSlots` → `releaseSlot(slot,
generation)`): the retired slot is released with its generation advanced and its pre-switch identity
consumed while a live-owner-protected sibling is untouched (control: retire, switch back, the other slot
goes); a live finalization refuses the release (`noLiveOwnerForSlot` false, `releaseSlot` → `RefusedBusy`,
lease and generation unchanged) while the switch still commits (an admitted stop does not hold the active
slot); and after the production grain retires the stop, the **next** same-track switch releases the retired
slot. **Two corrections:** (1) the route that releases it is *not* the retirement —
`TrackPreparedStopCommit::completeDeferredCleanup` clears `captureStopOwner` and arms the *capacity* marker
but releases no working set, so the next playback reset does it; (2) the earlier "needs an emission" clause
was stale: `DIAG,ws_release,<slot>,<generation>,<outcome>` already exists and is Tier-A (`bbedfeac`).
**Red-proved in three stages:** predicate-only (case A fails, B/C survive on `reset()`'s own refusal),
predicate + refusal (case B fails *Expected 6 Was 1 … RefusedBusy*, plus four pre-existing fixtures), and
also the track-level early return (case C fails) — the live-finalization protection is **triple-layered**.
`src/` untouched; focused 23/23; full native **2,468 passed / 7 skipped**; capture-serial SUCCESS,
`padding 856` (floor 840). S4.1 is **checked**, with its device leg recorded as an evidence obligation: the
lease-release row has an emission and needs a run + oracle, not instrumentation. Record:
[`ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)
§ "S4.1 — working-set lease release on a same-track slot switch".



**2026-09-26 — S4.1 persistence identity during handoff: CLOSED AS VERIFIED, no firmware change.** The
`DeferredSaveJobStages` footer read that S4.1 carried as a possible defect is the **contract**: the footer
records the workspace focus at **footer-write time**, while the interval's slot stays the **content**
identity (its body is a `LoopPersist` item keyed by `(track, slot)`, cleared only by its own token-verified
write; the bundle rename is the publish point). Four cases in `test_storage_drain_restore` drive the real
deferred worker paused with the footer due and unwritten (`stage == Footer`, `footerWriteStage ==
SelectedTrack`, cursor 0), an A→B handoff through `TrackManager::setSelectedSlotIndex(..., SyncPlayback::Yes)`,
then a reload through `StorageManager::loadCurrentWorkspaceFromSd`: A's and B's content, loop identities and
the **restored focus** are asserted per slot, with a no-switch control (restores A), a handoff *after* the
footer was written (the selection route's own requeue lands it), and an SD write fault at the footer
(nothing published — the durable bundle stays byte-identical — and the retry lands the handoff).
**Red-proved** by pinning the footer's two reads to the interval's slot: the three handoff cases fail on the
restored focus (`restored active=1 selected=1 (expected 3)`) while the control passes. The first red-proof
attempt was a **false green** and is recorded: pinning only `Slot`-keyed writes left the handoff's own
requeued `WorkspaceFooter` write reading live state, which repaired the footer — a real robustness property
of the route. `src/` is untouched (`git diff` empty); full native **2,465 passed / 7 skipped**;
capture-serial SUCCESS, `padding 856` (floor 840). Recorded as **DEC-090**; packet:
[`ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)
§ "S4.1 — persistence identity during handoff". **Still open in S4.1:** the working-set **lease** release on
a same-track slot switch (assert it by name; the emission is a separate decision) — named as the next slice
in [NEXT-STEPS.md](../../openspec/changes/runtime-working-set-ownership/NEXT-STEPS.md), together with a
newly found `FinalizeWorkspace` retry-policy question (a persistently failing completion retries at the
queue head and blocks the queue behind it; observed while building this fixture, not fixed here).



**2026-09-26 — approved load-time undo implemented; host recovery passes.**
DEC-088 — load-time undo reconstruction supersedes the earlier zero-after-load
policy. All 122 actions derive from saved passes on load/reload with empty undo
headers; all 60 nonempty slots pass Undo/Redo, and background commits preserve
runtime Redo. Redo starts empty and is created by Undo. The isolated runner verifies
hydration before/during/after restore. Candidate reconstructed-undo-04 preserves all
65 original files plus a 628-byte bundle (443,431 bytes total). Capture-serial build
passes with RAM1 padding 840, unchanged. No upload/device/card action; standalone
reader and incident hold remain. Hardware recovery is still unverified. Evidence:
active-change `evidence/2026-09-26-workspace-loss/UNDO-LOAD-FIX.md`.

**Verification complete:** full native 2,461 passed / 5 skipped; focused native 68
passed; recovery load/reload 122 actions and all 60 nonempty slot roundtrips;
Python 16 passed; OpenSpec validation passes. Capture-serial firmware SUCCESS,
RAM1 padding 840. No device actions. See UNDO-LOAD-FIX.md and preserved logs.


**2026-09-26 — correction: reconstructed undo after reload needs a policy change.**
The prior “next fix” diagnosis missed DEC-057 — snapshot body format and the active
working-set spec: LSV1 load deliberately starts with undo depth zero. The older
content-history reconstruction spec has a newer snapshot delta that changes it.
The 122-action host recovery remains valid; it is not authorization to reverse the
load contract. Proposed change: derive per-slot undo from sufficient saved passes,
redo empty, saved snapshot authoritative, no persisted undo stack. Approval is
required before that normal-load behavior changes. No firmware/device action.
See active-change `evidence/2026-09-26-workspace-loss/UNDO-LOAD-POLICY-REVIEW.md`.


**2026-09-26 — undo reconstructed and host-tested; reload blocks installation.**
Existing C++ content-history owner derives 122 actions (60 record, 11 overdub,
51 boundary changes) across all 60 nonempty loops. Full Undo → Redo preserves
materialized MIDI bytes, loop lengths and published note identities/content on all
60; four empty slots have no actions. Diagnostic bundle: 8,846 bytes. Production
load/hydration fixture retains only 9 actions after reload, so the wrapper exits 2
and the candidate is not safe to install. Original files byte-identical; firmware
unchanged; no device access; hold remains. Original cross-slot chronology, clear
snapshots and old redo branch remain unrecovered. Evidence: active-change
`evidence/2026-09-26-workspace-loss/UNDO-RECONSTRUCTION.md`.


**2026-09-26 — workspace reconstruction passes host load/restore acceptance.**
Candidate `captures/midilooper-folder-copy-20260926-195228/reconstructed-03/candidate/`
preserves 65 original files and adds a 628-byte runtime bundle (total 443,431 bytes).
All 64 slots restore; 7,840 capture-event records match source byte-for-byte, 57 snapshot
inventories match. Explicit replacement settings: stopped/120 BPM, first nonempty
slot per track selected/enabled, unmuted, fresh global undo. Source decode 3/3;
reconstruction 2/2; affected ordinary-native regression 12 passed/2 explicit skips;
Python 16 passed. Reusable C++ writer is host-tested only; automatic device recovery
is not implemented. No SD-card writes/upload/device access. Hold remains latched.
Evidence: active-change `evidence/2026-09-26-workspace-loss/WORKSPACE-RECONSTRUCTION.md`.
S1.6c.2 — capacity revalidation remains blocked on actual hardware recovery/measurement.


**2026-09-26 — host production decoder audit complete.** All 64 copied current
slots decode using an isolated Teensy-compatible event-layout adapter: 7,840 raw
MIDI events; 57 effective snapshots with 2,766 note identities. Three nonempty slots
use raw pass format; four slots empty. Direct Mac decoding was invalid because
MidiEvent is 32 bytes on Mac versus 20 on Teensy. Firmware unchanged. Three focused
native tests pass through the repeatable host wrapper; no device access. Evidence:
active change `evidence/2026-09-26-workspace-loss/PRODUCTION-DECODE-AUDIT.md`.
Runtime metadata reconstruction/load validation remains next; no restore candidate
approved, normal firmware hold unchanged, S1.6c.2 — capacity revalidation blocked.


**2026-09-26 — offline recovery audit complete; no device access.** All 64 current
slot files pass the firmware checksum/footer checks; 60 have nonzero loop lengths.
60/64 lengths match the preserved pre-loss census; differences are track 8 slots
4–7. Saved revisions hold at most 22 nonzero-length slots, so current files remain
the primary recovery candidate. Full note decoding and compatible workspace metadata
are not yet validated; no restore candidate is approved. S1.6c.2 — capacity
revalidation remains blocked. Evidence: active change's
`evidence/2026-09-26-workspace-loss/OFFLINE-RECOVERY-AUDIT.md`. 16 host tests pass.
Normal firmware/recovery hold stays latched; standalone reader remains installed.


**2026-09-26 — USB folder copy COMPLETE, authorized reader installed.**
Copied only `/MidiLooper` through the standalone read-only reader with the card left in place:
**1,007 file entries / 6,837,184 bytes**, transfer CRC32 and independent local SHA-256/length/CRC
verification all pass. Output: `captures/midilooper-folder-copy-20260926-195228/download-05/`;
report: `captures/midilooper-folder-copy-20260926-195228/verification.json`.
Contains 65 current files (64 slots plus workspace metadata), 73 saved-set files, and historical
`current.bad.*` trees. This proves surviving files, not their semantic validity or recoverability.
One duplicate path, `current.bad.211210/workspace.bin`, has distinct 154-byte and 77-byte entries;
both are preserved (second under `duplicate_entries/000854.bin`), not overwritten. Initial transfers
are retained as incomplete evidence; USB per-line acknowledgements fixed unreliable bulk sending.
Reader firmware SHA256 `a915b18c5a12175ef7c25dddcc8932840418fd61018ca4201ade467e78cacce2`;
9 host tests pass. No SD writes, normal-looper boot, recovery or restore occurred. Reader remains
installed; normal-looper upload/recovery hold stays latched. User's explicit approval covered this
reader installation and folder copy only. Recovery analysis can now use the host copy.

**2026-09-26 — standalone USB reader installed with explicit user approval; copy pending USB port.**
User approved replacing/resetting the running firmware solely to read `/MidiLooper`. Standalone
`pio run -d tools/midilooper-reader -e teensy41-capture-serial -t upload` succeeded. macOS
sees USB Serial `16c0:0483`, serial `15494480`, but has not exposed a `/dev/cu.usbmodem*`
port. No export command was sent and no SD access occurred through the reader. Asked the user
to allow the accessory if macOS prompts. Installation evidence:
`captures/midilooper-folder-copy-20260926-195228/installation.json`. The normal-looper
upload/recovery hold remains latched; do not restore the normal firmware automatically.

**2026-09-26 — USB folder reader prepared locally; no device access.** User requests only
`/MidiLooper` over USB, leaving the card in place and unrelated data excluded. Added
`tools/midilooper-reader/`: separate read-only utility firmware (no looper boot recovery/save
code) plus a streaming Mac downloader with file CRC32, SHA-256 manifest, path checks and
incomplete-transfer handling. Seven host tests and the standalone Teensy build pass.
NOT uploaded or device-verified; incident hold remains set. Installing the reader replaces
and resets the running firmware and needs explicit approval. Folder-only copying supersedes
full-card imaging as the requested approach, without authorizing upload, repair or device access.
Contract and commands: `tools/midilooper-reader/README.md`.

**2026-09-26 — INCIDENT: the device workspace no longer loads; host shutdown defect fixed (host-only; no device access).** The device comes up empty: `CurrentSet meta missing .../current/temp/runtime.bundle.bin`, `Current workspace load failed`, `Boot recovery chain exhausted; starting empty`, `BOOT,load,fail`, `free_chunks=480/512`, all 64 slots empty — where the same library read **58 committed / ~386-403 free** minutes earlier, and the post-loss observation is *complete* (64/64 slots, `incomplete_window: false`), so it is not an artifact. **Cause, and it is this session's tooling:** the deliberate clear of the authorized target admitted a deferred workspace save, and `--prepare-only` returned immediately, closing the serial port mid-write (that session's last line is `PERS,work,...,TrackMeta,track:7,start,ok` with no completion); the next run's upload then reset the board on top of it. **Established:** the load failure and the empty runtime. **NOT established:** that the slot payloads are erased, or the exact failure chain — a load failure is not an erasure, and the card may still hold every payload and saved set. **Fixed (host-only):** `scripts/hitl/device_shutdown.py` — canonical stop → drain → stop → drain, completion **verified** through the existing idle owner (never slept), judged from the log **tail** (anchoring at "now" reads an empty suffix as idle and reported success before the save was admitted — a real defect in the first cut, caught by its own test), disconnection/timeout named apart, incomplete shutdowns **latched**, and `DeviceSession` draining on *every* exit (normal, early return, exception) rather than per-`return`. The driver and `hitl_boot_census` both refuse an upload/reset while latched; the hold is currently set with the incident context. 15 host tests use the incident's own preserved logs as the decisive fixture (clear-run reads active, clean run reads idle — a fixed sleep could not tell them apart); a false green in the structural test was found by red-proving it and fixed. Evidence preserved with per-file sha256, commands and build identities under `openspec/changes/runtime-working-set-ownership/evidence/2026-09-26-workspace-loss/` (byte-exact via `.gitattributes`), and the read-only SD imaging procedure awaits authorization. **Device validation of the shutdown fix is PENDING** — it is host-tested only and cannot be device-validated while the hold is latched; its tests support the correction, they do not demonstrate it on hardware. **S1.6c.2 — capacity revalidation is BLOCKED (hard):** it needs the loaded N=56 library; an empty-library run cannot replace that experiment. No boot, no card access, no quarantine, no load, no rebuild.


**2026-09-26 — S4.1 reconciliation: the A/B/C run is the release acceptance, and the mid-pass journal resolver is fixed.** Owner correction, verified against the code: the accepted-switch retention run is not merely *related to* S4.1 — it is S4.1's device acceptance. "The slot leaves the active set" is A switching to B while its deferred finalization holds the occupancy; "the guard reports no live owner" is the release marker's `captureStopOwner == nullptr` clause, which is the `ownsLiveFinalization` term of `noLiveOwnerForSlot` (S3.2); it is identity-qualified (`canonical_release,4,40,6144,8` names the retiring owner's slot **and generation**); and **C's start is what performs the guarded working-set release** — `startRecordingTrack` → `releaseBackgroundPlaybackMergedMidiEventsMemory` → `releaseEligiblePlaybackRuntimeSlots` → `releaseSlot(slot, generation)` — followed by C's reuse, which is S4.3 verbatim. **S4.1's "rerun blocker" text was stale:** the per-interval ledger clear is converted (`clearStoppingIntervalLedger()` clears its own slot; the comment names the old global sweep as the blocker), `resetAll` is deleted and replaced, the only remaining `clearAllLedgers()` caller is the deliberate `sendAllNotesOff` sweep, and the slot-threaded persistence reads are converted. **Genuinely still wrong and now fixed:** the mid-pass journal resolver read `getActiveLoopIndex()` for a **stop in flight**, so an interval pinned to slot 5 with the active slot moved to 7 was journalled under 7; the selected-track fallback keyed a queued chunk to whatever slot was selected when the drain ran. The rule moved to its owner as `Track::captureWorkSlot()` (with `Track::pinnedStopSlot()`), refuses rather than guesses, and the resolver is now a thin loop over it. Fixture `test_s4_1_capture_work_slot_uses_the_pinned_interval_slot` asserts 1 while the active index is asserted to be 3 (so a rule that read it cannot pass), plus a refusal control and a live-capture control; **red-proved** (*Expected 1 Was 3*). Full native **2,459 passed / 1 skipped**; firmware SUCCESS, `code 392,376 / padding 840` (floor; the rule is FLASH-resident). **Recorded, not quietly dropped:** the working-set lease release is currently *inferred* from the sweep mechanism rather than asserted by name (the oracle asserts L4's finalization-capacity release); and `DeferredSaveJobStages`' footer read is a **design question** — the deferred save walks *live* state per stage, so capturing only the footer at admission would make the saved image internally inconsistent, and a snapshot-at-admission save owes its own `DEC`. Record: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § "S4.1 reconciliation".


**2026-09-26 — S4f.3 device acceptance re-run after the Tier-A fix: `RETENTION_PASS`, exit 0, zero concessions.** Run `captures/s4f3-retention-20260926_181732`, identity **`git_commit 5629e7e4f96d` / `firmware_hex_sha256 da59a7eeb3e0`** (a firmware hash distinct from every earlier run, so this is the promoted image). **20/20 checks `ok`, `concessions` empty.** The point of the run is that the rows whose absence forced `INSUFFICIENT_EVIDENCE` at 17:56 are now present in a completed run: `DIAG,reentry_window_open,4,1121,6144`, `DIAG,canonical_release,4,40,6144,8`, `DIAG,reentry,deny,1311,2,5,1` and `DIAG,reentry,bound,1706,2,5,0` — while `capture_integrity` again reports no ring eviction, the signature that the evidence was being *shed*, not evicted. **The corrected L5 ordering holds:** the bind (59,698,903 us) precedes the release record (59,723,633 us) by 24.7 ms, and the release is now read as an informational maintenance observation rather than the availability edge. Window **1550.3 ms**; C's retained request was **staged** inside it with no start executed before the release, and exactly one `RECA,6` follows the release. **L7 is loop-scoped and the run shows why:** the whole-playback review returns FAIL/`inventory_equal=False` on all 36 cycles because track 8 slots 1–4 still hold committed content whose notes sound alongside the loop under test (reported as `EXTRA`); the deciding evidence is the dump's own completeness (`declared counts {'effective#0': 2, 'record#0': 2}` match parsed) with note 67 in **both** storage and effective rows agreeing on tick/type/channel/note/velocity, `storage_matches_effective=True`. **Staging was verified, and the clear is attributable:** targets cleared as a separate deliberate step (never silently), then a read-only census re-verified eligibility — committed count **61 → 58**, exactly the three slots — and the run was gated on that census with `--no-clear`. **Driver bug found by running and fixed (`5fc74bf9`):** the prepare reported `clear_5_wrong_slot` for all three targets while the device had cleared exactly the right slots (target 5 → `active_slot=4`, 6 → 5, 7 → 6) — the firmware identity fields are 0-based and the driver's `slot_number` is 1-based, compared raw; the presses were always correct and only the verification was wrong. Invisible before the identity existed, since any marker was accepted — which is how a preparation could report success on a slot it never cleared. Also recorded: the driver's live `transport_stopped` returned false while the take committed and L7 read a complete dump, so the live check is a convenience, not a gate. **Scope closed: retention plus successor capture at that precise scope.** **Still open and not claimed:** A/B content preservation (the dump covers the active slot only), the `companion path stalled; raw pass only` fallback's content consequences, and MIDI-output playback parity, which is separate from this loop-scoped parity. Record: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § "The rerun after the Tier-A fix".


**2026-09-26 — the identity-grade evidence was being *shed* by design; logging fixed, no device run.** The unexplained `INSUFFICIENT_EVIDENCE` on the verified-empty rerun is explained: **it was never a deferral or flush problem.** `CaptureLineTier::isTierALine` decides what the timing-critical shed may drop, and it drops non-Tier-A diagnostics **silently, by design, without reporting a loss** — and none of the three identity-grade tags were in its allowlist (`grep` count 0 for `DIAG,reentry,`, `DIAG,reentry_window_open,`, `DIAG,canonical_release,`). So the shed removed exactly the rows the acceptance is computed from, while `capture_integrity` correctly reported no *eviction*: shedding is not eviction. **The three tags are now Tier-A** (Tier-A is displaced only by Tier-A; these are a handful of one-shot edge rows per run), costing **no RAM1** — the predicate is called by a `FLASHMEM` function, so `code 392,376 / padding 840` is unchanged at the approved floor. **A latent bug in that very fix was caught before shipping:** the comparison was first written with a hand-counted length of 24 for `"DIAG,canonical_release,"`, whose `strlen` is **23**; including the NUL makes `strncmp` match only a tag that ends there, so the real row would never have matched and the evidence would have been shed again with the same silent signature. It now uses `strlen(...)`, pinned by a boundary test and red-proved (restoring 24 fails it). **The timely edges now carry identity as well**, so attribution no longer depends on the ring: `Overdub stop refused: <reason> slot=<n> interval=<n>`, `Track cleared @ tick <n> (active_slot=<n>)` (appended through `logTrackEvent`'s existing format parameter, so the established prefix survives every existing substring matcher), and both `Clear ignored — … (track <t> slot <n>)` forms. **Consequence — preparation is auditable:** `_clear_slot` used to accept *any* `Track cleared`/`Clear ignored` marker, which is exactly how a preparation reported success on a slot it never cleared; it now parses the identity and returns pass / fail (a clear naming a **different** slot → retry, not success) / unknown, recorded as `identity_grade: false` rather than silently counted as proof. Bases are explicit because mixing them is the trap: `active_slot=` and the `Clear ignored` field are 0-based (matching `#CAP` identity and the census), while `MIDI: Clear selected slot N` is 1-based and deliberately not used for identity. **Also fixed:** the oracle's wire-form docstring documented `DIAG,canonical_release` with 3 fields where the emitter and regex use 4, and a **stale host test** (`..._blocks_clear_without_retry`) supplied two sequencer probes for a retry path that makes three, erroring on `StopIteration` regardless of this change (confirmed pre-existing by stashing); it is replaced by two cases asserting the current, stricter behaviour. **Verification:** firmware SUCCESS (`padding 840`), all four new strings in the fresh ELF; native **2,457 succeeded / 1 skipped** (up 2); oracle 39 green (up 3), gesture plan 22 (up 7), clear precondition 28, capture transitions 48. Both new guards red-proved by mutation. **The device gate does not move:** retention plus successor capture still needs a rerun reaching `RETENTION_PASS` with zero concessions; what changed is that the previously-unexplained `INSUFFICIENT_EVIDENCE` should no longer be reachable for this reason. A/B preservation, the raw-only companion fallback and playback-output parity remain open. Record: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § "The evidence edges are Tier-A now".


**2026-09-26 — S4f.3 device acceptance PASSES without concessions.** The repeat run (`captures/s4f3-retention-20260926_172941`) returns **`RETENTION_PASS`, exit 0, zero concessions**: fresh boot and pinned identity, A's finalizer outstanding, B's stop denied **with slot and interval**, C's request retained, capacity released **naming A's slot and generation** (`canonical_release,4,40,6144,8`), the same B interval bound (correlated to the denied interval 2), C started exactly once, C's dump declaring the expected note 67 in **both** storage and effective rows, and a clean bounded follow-up. The capacity-denied window measured **1376.5 ms** and the **denied request was staged inside it** — C started afterwards, which is the expected retention behaviour — so the natural window suffices to stage the denial and the pause hook stays unimplemented. **L7's instrument changed and why it is not a weakening:** the whole-playback review compares a one-loop dump against the entire audible output, which cannot hold while other slots sound (the playback carried dozens of unrelated notes); the check now reads storage↔effective parity from the dump **for the loop under test** (both `record`/`overdub` and `effective` rows must contain the expected notes and agree), which is strictly more specific, and a supplied review is reported alongside rather than deciding. **Still open, not closed by this pass:** A's and B's *content* is unverified (both committed nonzero 6144-tick takes), and the `companion path stalled; raw pass only` fallback reproduced, so predecessor preservation is not claimed. Also landed: the release attribution fix (slot **and** identity, free in RAM1 — the earlier "64 bytes" claim was a confounded measurement and is retracted), and the subagent's discriminating native regression `test_the_successor_take_commits_with_content_after_a_transport_stop` (native 2,455 passed / 1 skipped). Record: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § the device acceptance passes without concessions.


**2026-09-26 — S4f.3: the concession loophole closed and the evidence gaps filled (no device run).** Conceded links no longer yield a pass: **`RETENTION_PARTIAL` (exit 5)** is its own verdict and `RETENTION_PASS` (**exit 0**) is reserved for the complete, unconceded chain — `--allow-setup-issue` now counts as a concession too, since allowlisting a report issue is an operator claim rather than evidence (a partial result must not close the device gate). **L5 closed:** `beginDeferredOverdubStopForSlot` now emits `reentryDecision("bound", tick, interval, slot, depth)` on the admission-committed edge, reusing the existing emitter and wire format, so a denial and its later binding correlate on the **same interval identity** instead of being inferred; a `bound` record for the denied interval *before* the release is a `RETENTION_FAIL`. **RAM1 floor preserved and measured:** `padding` byte-identical at **840** with FLASH `+104 bytes`, because the function is `TRACK_COLD_MEM` (FLASH-resident) and never enters the ITCM segment the floor measures — no recovery was needed. **L3:** the retained row now names track *and* slot, and the oracle requires both plus positional ordering (recorded as positional, since a human log row has no timestamp). **L7:** now requires the **existing content pipeline** — the `!CAPTURE_CONTENT` dump for the successor slot's real pitches plus `content-review.json` parity; a review alone is parity-only and insufficient. **36 host tests green**, including the complete unconceded chain reaching `RETENTION_PASS`/exit 0 with zero concessions; firmware SUCCESS. **Step 3, the first authorized no-concession device run, was NOT performed:** it needs a host sequence for the real gestures, and that flow does not exist (`queued_switch.py` is a Phase-1 skeleton; the action layer has no per-slot record press or slot/track select). Building it is host work with device round-trips, so it is recorded as the next step rather than rushed. Record: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § S4f.3 (the concession loophole closed).


**2026-09-26 — S4f.3 retention oracle tightened (still no device run).** The first oracle could falsely report `RETENTION_PASS`; the owner reproduced two cases and both are now refused: C starting **before** A releases capacity (was a pass → now `RETENTION_FAIL`, exit 2, failing L3 and L6) and removal of B's **identity-specific** denial with the generic `successor_stop_denied_capacity` row left in place (was a pass → now `INSUFFICIENT_EVIDENCE`, exit 4). The oracle now demands an **ordered chain** — L1 A's finalizer outstanding → L2 B's stop denied **with slot and interval** → L3 C's request retained → L4 capacity released → L5 that same B stop bound → L6 C starts exactly once **after the release** → L7 C recorded the expected MIDI → L8 bounded follow-up with no duplicates — where any missing or reordered link is never a pass. New verdict `INSUFFICIENT_EVIDENCE` (exit 4) separates "staged but a link lacks telemetry" from `SETUP_INCOMPLETE` (exit 3, "this run is not evidence"), and integrity gates now block a pass on missing build identity, ring overflow, a truncated log, unconfirmed setup transitions, and any base-report issue that is not explicitly allowlisted. **Telemetry gaps are now reported rather than inferred:** L5's identity link is unavailable (`captureInterval_` is emitted only by `reentryDecision`, while stop-tier records carry `frozenGeneration` = `staging.captureGeneration`, a different identity space), L7 is count-level because no per-note capture record has a caller, and L3 rests on a human log line — each passes only via an explicit, recorded **concession**. **Corrections:** exit-code separation does not establish that out-of-range notes were generator artefacts (an allowlisting is recorded as an operator claim), and the earlier "no finalizer-pause control is required" is **withdrawn** — the 1.335 s window is a timing observation from a session with no boot banner, so the hook stays a contingency. Evidence: 29 host tests (one negative per way to lose a link), CLI reproductions at exit 2 and 4, and the real log returning `SETUP_INCOMPLETE` (exit 3) while still measuring the window; three oracle bugs found by its own tests were fixed. Record: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § S4f.3 (the retention oracle tightened).


**2026-09-26 — S4f.3 device-retention stimulus and oracle prepared (NOT run).** Per owner direction, prepared only: no device run, no target slot assumed authorized. **Assessment — telemetry and exposure already exist, so no new instrumentation and no pause hook is needed.** The requirement is decidable from existing records keyed on slot and interval identity: `DIAG,reentry_window_open,<slot>,…` (DEC-067 `successor_start_window_open` = the predecessor's finalizer admitted and outstanding, named by slot), `DIAG,canonical_release,<slot>,…` (the denied window closes), `DIAG,reentry,deny/deny_resource,<tick>,<interval>,<slot>,<depth>` plus `ODUB,stop,admission,…,successor_stop_denied_capacity`, and `RECA,<slot>,<tick>` (the start, so exactly-once is a count) — never the 21 µs `OVERDUB_STOPPING` state that defeated the interruption fixture. **Measured window:** real device records `reentry_window_open,0,16387,1536` (136,629,769 µs) → `canonical_release,0,1536,8` (137,965,158 µs) = **1,335,389 µs (1.335 s)** for a 2-bar loop, comfortably wider than two host gestures; that session has no boot banner, so the figure is a timing observation to reconfirm under a fresh boot. **Oracle:** `scripts/hitl/retention_probe.py` → `retention-review.json`, verdicts and exit codes keeping setup distinct from retention (`SETUP_INCOMPLETE` 3, `RETENTION_NOT_EXERCISED` 2, `RETENTION_FAIL` 2, `RETENTION_PASS` 0), with `fresh_boot`, `tempo_verified` (read-back required), `seed_content_on_a`, `finalizer_admitted_on_a`, `capacity_released_after_a`, `capacity_denied_window_wide_enough`, `successor_stop_admission_denied` (inside A's window), `successor_started_exactly_once`, `activation_invariant_held`, `no_stop_obligation_replay`. Validated without a device: **11/11 host tests** (`scripts/test_retention_probe.py`) and a real-log run that returns `SETUP_INCOMPLETE` while still measuring the window. **Stimulus:** `STIM-BASE-2-ONE-OD-01-TEMP-RETENTION-A` authored via the sanctioned `stimulus temp` fork, not registered, sidecar under the change directory; pass criteria live in the oracle. **Setup gap:** the three targets (A/B/C on one track) are **not yet authorized** — the documented scratch slot is a single slot (track 5 / slot 1). **Adjacent defect, reported not fixed:** `scripts/hitl/stimuli/yaml_view.py` `dump_yaml` emits invalid YAML (dict branch tests `isinstance(item, list)` while `StimulusSpec` fields are tuples). Record: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § S4f.3 (device-retention stimulus and oracle).


**2026-09-26 — S4f.3 HITL attempt — no pass, and the new path is uncovered.** Flashed the verified image (`firmware_hex_sha256 14a1aea7…`, `git_commit e743b97c…`) and ran the registered entry points per the hitl-content-verification skill (fresh upload; documented scratch slot **track 5 / slot 1**). The stop-interruption wrapper reached the fixture (`PLAYING->OVERDUBBING: 1`, record `32/32`, persist `ok=True`, `OVERDUB_STOPPING: 1`, intervention sent) but returned `INCONCLUSIVE_CAPTURE` / `no_device_stop_inside_stopping`; the log shows the `OVERDUBBING -> OVERDUB_STOPPING -> PLAYING` window is **21 µs** because the admitted stop defers finalization (DEC-078), so the fixture's premise is architecturally mismatched rather than flaky, and no delay value can hit it. The base leg's `overdub_unmatched_open_notes` / `overdub_out_of_range_notes` are the skill's setup verdicts. The baseline preset (`STIM-BASE-2-2-01`) returned `INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)` with `RECORDING: 0` — the known base-preset setup race already attributed to the still-open selection fixture — at a correct tempo (`record loop length 1536/1536`). **Tempo must be re-pinned per run**: the device was found at 160.0 BPM and the DEC-086 fader pinned it back to 120.0. **No registered stimulus covers this slice's behaviour**, so these runs are regression context only, and the owner declined a temporary variant this session — the retention path is **host-verified only**. Record: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § S4f.3 (HITL attempt).


**2026-09-25 — S4f.3 (PARTIAL 3) — accepted-switch retention under unavailable stop admission.** A switch/start requested while the predecessor's stop **cannot be admitted** is no longer dropped: `TrackManager::retainRecordStartForSlot` records it in the **existing** manager pending-start owner (no new owner, no new storage) without moving the audible set, and `handleToggleRecordForSlot` calls it on the uncommitted path. `finalizeCaptureAndSelectSlot` now performs **explicit supersession scoped to the finalized slot** (`clearQueuedRecordingTrack(trackIndex, captureSlot)`, plus dropping the retained request's phase reference when it is that slot) instead of clearing the whole track's pending-start state before it knows the switch will commit. Three corrections came with it: the overdub guard is **slot-scoped** (`hasLiveCaptureStopJobForSlot` / `hasAcceptedInteractionForSlot`) because `hasPendingDeferredOverdubStop()` is **track-wide** and would let another slot's job stand in for this interval's unadmitted stop; re-entering `stopOverdubbing()` on an already-admitted stop is avoided because it **re-latches** (its guard fires when a job is already pending) and would block the very switch being made; and **the activation hazard was found and fixed** — `handlePendingRecordStart` activated with the unconditional `Track::setActiveLoopIndex`, moving the active slot underneath an unadmitted predecessor stop, so it now uses `TrackManager::setActiveLoopIndex` and **explicitly asserts** that a failed activation leaves the slot unchanged with the predecessor's stop in flight. Evidence: `test_a_blocked_switch_and_start_survive_until_its_prerequisite_stop_binds` (A's stop held, B overdubbing with its stop capacity-denied, C's switch/start retained through 2,304 dispatches, capacity released, B's stop binds, C starts and records, no double execution), **red-proved three ways** with distinct failure lines, and a **false green found and removed** (the replaced fixture returned early from a characterization branch in the default gate while holding nothing). Full native `2,448 passed / 1 skipped`; firmware SUCCESS. **RAM1 is now exactly on its approved floor** (`code 392,376 / padding 840`; 272 bytes spent, gating the diagnostic recovered none because the strings are FLASH-resident) — the next slice here must free RAM1 or revisit the floor. **Immediate repeated handoffs (B→C while A finalizes) remain explicitly open**: retention is proven, immediate execution is the separate finalizer-capacity question. Record: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § S4f.3.


**2026-09-25 — S4f.3 (PARTIAL 2) — the switch's own gesture now carries the start.** S4b.1's contract check 4 is met for the **committed** switch: `TrackManager::finalizeCaptureAndSelectSlot` **returns whether it committed** (false on the invalid-argument guard and on the S4f.5 `hasAcceptedUnexecutedInteraction()` rule), and `MidiButtonActions::handleToggleRecordForSlot`'s capture-switch branch uses that commit point to queue the requested record start for a **record target** via `queueRecordingTrack(trackIdx, slotIndex, Config::INVALID_LOOP_SLOT)`. The phase reference is deliberately **none**: `refSlotPhaseForQueue(track, previousSlot)` would return the predecessor's slot (which always has a length here) and so make **A's mutable loop timing an accidental dependency of B's start** — forbidden by name in the owner-provided contract; with no reference the dispatcher uses the transport's bar grid, as the contract requires for an empty or cleared target. Fixture `test_the_switch_gesture_carries_the_start_without_a_second_press` (renamed; the second press is gone) asserts the request exists after **one** gesture, drives the dispatcher with no further press, and asserts the target records while A still finalizes — **red-proved** by disabling the carrier (`1 failed, 11 succeeded`), and the two-gesture acceptance fixture still passes in that run, so the fixture **discriminates** the carrier instead of restating the acceptance. Green in both gate modes; full native `2,448 passed / 1 skipped`; firmware SUCCESS with `code 392,040 → 392,104 / padding 1,176 → 1,112` (above the 840-byte floor). **Still open:** (a) the **uncommitted** switch is the reachable remainder, not a hypothetical — `Track::stopOverdubbing` **denies** admission on capacity (`hasPendingDeferredOverdubStop() || finalizationResourcesBusy`, DEC-067), so `finalizeCaptureAndSelectSlot` returns `false` and the press is **dropped**, not retained; carrying it needs the switch's own retained carrier with its prerequisite-stop identity, and it lands on the finalizer-capacity question this change keeps open; (b) the unconditional pending-record clear at the top of `finalizeCaptureAndSelectSlot` — the carrier survives it only because it queues *after* the call, so it must be scoped to the finalized slot when the retained switch lands. Record: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § S4f.3.

**2026-09-25 — S4f.6 — accepted-start carrier landed; S4b.1's acceptance now passes end-to-end.** The successor starts while the predecessor finalizes. `TrackManager::startRecordingTrack` returns `bool` (every refusal returns `false`), the refusal is **slot-scoped** via `Track::hasStopInFlightForSlot(uint8_t)` (reads `captureStopJobHead()->slot` and `interactionLatch()->slot`), and `handlePendingRecordStart` sets the target active **first** and consumes the request **only on success** — so a refusal retains it instead of discarding it. **Retraction:** the § S4b.1 path review said the refusal "strands the request: retained but never executed"; the committed dispatcher consumed **unconditionally** (`startRecordingTrack` returned `void` and the consumption lines followed it), so a refusal **discarded** it. Symptom unchanged, mechanism corrected. The S4b.1 acceptance fixture now asserts the requirement **unconditionally** — both characterization branches deleted, and the fallback press removed from the dispatch loop so a start that only happens because the fixture pressed again cannot pass — **red-proved** by restoring the pre-S4f.6 `hasStopInFlight()` guard (`2 failed, 10 succeeded`). `test_an_unadmitted_stop_retains_the_switch_and_start_requests` was **renamed** to `..._retains_the_switch_request_without_a_second_gesture`: the old name **overclaimed** — it only ever proved the switch (confirmed by a probe turning its refused-branch early return into an unconditional failure) — and it now drives a start after the latch-admitted stop while labelling that press as a second gesture. Full native `2,448 passed / 1 skipped`; firmware SUCCESS with RAM1 byte-identical (`variables 96,160 / code 392,040 / padding 1,176 / locals 34,912`), so the carrier costs no RAM1. **Still open, and named rather than implied:** (a) **S4f.3's carrier** — the switch's own gesture does not carry the requested start, so S4b.1's contract check 4 ("... without another gesture") holds only for the switch half; (b) **new finding:** `finalizeCaptureAndSelectSlot` clears all pending-record state at its **top**, before it knows the switch will commit, so a switch currently **discards** a retained start request for any slot — the landed fixture cannot see this because it commits the switch before queuing the start; (c) cross-track and cleared/populated target coverage, and check 6's "cannot execute twice" leg; (d) repeated B→C while A finalizes stays a **finalizer-capacity** case, not `Omax`. Record: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § S4f.6.

**2026-09-25 — S4f.3/S4b.1 (third pass) — root cause of "B does not start" isolated.** S4f.3's first half is in (an admitted stop releases the active slot; S4f.5's unadmitted refusal kept), and the acceptance fixture now drives the production routes (arm entry, queued-start dispatch at a bar boundary, note in the record domain), which narrowed the failure to one mechanism: **B is overdubbing with `loopLengthTicks == 0`**. `startOverdubbing` guards on `trackState == TRACK_OVERDUBBING && getActiveLoop().capture.phase == Overdub` and returns silently; after the switch commits, the track still reports overdubbing while the successor is empty, so the successor never takes the **record** route that establishes its length, and the append has no loop to write into. This is a state-coherence defect, not the carrier: an overdub needs an existing loop, and the predecessor's finalization is already carried by the deferred job (`job.slot`, the latch, `captureStopOwner`), not by the track's live state flag. Next change: on switch commit, when the target has no committed content and no live interval, leave the overdubbing state so record admission runs (candidate sites: `finalizeCaptureAndSelectSlot` after commit, or `startOverdubbing`'s guard, which currently no-ops silently). Default gate 12/12; `S4B_ACCEPTANCE=1` red with the exact state. Record: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § S4f.3/S4b.1 third pass.

**2026-09-25 — S4b.1 (second pass) — fixture tightened after review; unadmitted-stop case passes.** The acceptance fixture now requires an **admitted** finalizer (`hasPendingDeferredOverdubStop` and *not* accepted-unexecuted) and reaches B through `MidiButtonActions::handleToggleRecordForSlot` plus the real MIDI handler against an **empty** target, so it proves the gesture and incoming MIDI reach B rather than that a buffer accepts an append; the characterization branch is to be **deleted** when S4f.3 lands, not left conditional. The admitted-and-paused case stays **red** (blocked on S4f.3), but the separate **unadmitted-stop** case `test_an_unadmitted_stop_retains_the_switch_and_start_requests` **passes**: the switch and start are retained and execute once admission becomes possible, without a second gesture. Driving the gesture crosses storage/display boundaries the harness kept fail-closed; they are observable for switch suites (grouped under `HARNESS_OBSERVABLE_SD_REFRESH` / `HARNESS_OBSERVABLE_SLOT_CHANGE`), and linking the real edit-depart TU was tried and rejected as too heavy. **Open:** S4f.3, the cross-track switch, and repeated B→C while A finalizes — which depends on **finalizer capacity**, not `Omax` alone. Full native `2,448 passed / 1 skipped`; RAM1 byte-identical. Record: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § S4b.1.

**2026-09-25 — S4b.1 — the switch acceptance is now executable, and it is blocked on S4f.3.** The user's exact sequence (A overdubbing → A's stop admitted with the finalizer **paused** → switch to B → B starts and records MIDI while A still finalizes) is `test_switching_starts_the_new_take_while_the_previous_take_still_finalizes` in `test_runtime_working_set_ownership`, driven through the production switch (`TrackManager::finalizeCaptureAndSelectSlot`) and the real MIDI input route, asserting B's recorded events. **It fails at the switch**: the path returns early while the stop is accepted-unexecuted or in flight, so the active slot stays on A and B never starts. That guard is deliberate — S4f.5 requires the active slot not to move under an **unadmitted** stop, and the contract keeps it until **S4f.3** supplies the switch's accepted-interaction carrier. Removing the guard made the acceptance pass but fails `test_s4f5_accepted_stop_blocks_start_and_switch_until_it_executes`, so it was reverted. The fixture asserts the current characterization by default and runs the requirement, currently red, under `S4B_ACCEPTANCE=1`. **Also open:** the cross-track switch, and repeated B→C switches while A finalizes (needs the `Omax` bounded overlap policy). **Correction:** S4.4's "envelope stricter than the model" claim is retracted — the census mislabeled an outgoing finalizer as `LiveCapture`. Full native `2,447 passed / 1 skipped`. Record: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § S4b.1.

**2026-09-25 — S4.4 — a declared-cap breach is reported, not enforced stricter than the model.** `Track::reportEnvelopeBreachIfAny(slot, role)` reads the per-slot lease census (no storage) and reports `#MEM_DENIED,reason=cap_reached,class=N` through the new `MemoryMonitor::reportDeclaredCapDenied` — a reason deliberately distinct from an allocation failure, so "declared cardinality exceeded" is never read as "the pool ran out". It does **not** refuse, and that is the finding: the first cut *enforced* the declared caps at the activation entry and broke the suite, because **the declared envelope is stricter than the resource model** (per-slot capture roles are supported; 85 continuous-capture fixtures arm a second slot). Enforcing the declaration would reject supported flows, so enforcement was reverted and only the observation kept; the enforcing caps stay the lease's own, covered by `test_working_set_lease`. **Open:** nothing is refused, so the refuse-vs-queue question is unanswered and needs either a cap the resource model also enforces or an architecture decision on per-track cardinality; the emitted line is a device observation. Fixture `test_a_declared_envelope_breach_is_reported_and_does_not_block_activation`. Full native `2,446 passed / 1 skipped`; RAM1 byte-identical. Record: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § S4.4.

**2026-09-25 — S4.3 — reactivating a released slot reuses its capacity.** Reuse here is capacity retained at a stable address, not a free/realloc cycle: `releaseSlot` retires the slot's working set while `runtimeBySlot_` keeps the `LoopPlaybackRuntime`, so the next activation re-admits into the same object and its generation/role state advances instead of accumulating. `test_reactivating_a_released_slot_reuses_its_capacity` runs four release/re-activate cycles against a baseline activation control and asserts the lease is unheld after the sweep, the generation advanced, the **same runtime address** returns, the role set is unchanged, and `LoopEventStore::freeChunkCount()` is stable. **Red-proven** with the sweep disabled. The entry already has a production caller: `TrackManager::startRecordingOnTrack` → `releaseBackgroundPlaybackMergedMidiEventsMemory` sweeps every other track, and the memory-pressure path drives it too. **Recorded limit:** native has no PSRAM, so `ExternalPoolAccounting` cannot report external bytes — reuse is observed structurally and the byte-level device claim stays open with the S2.2/S2.3 items. **Still open:** an explicit sweep on a same-track slot switch. Full native `2,445 passed / 1 skipped`. Record: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § S4.3.

**2026-09-25 — S4.1/S4.2 (release entry) — the `(slot, generation)` release exists.** `TrackPlaybackRuntime::releaseSlot(slot, generation, preserveLedger)` is the single release entry, reporting `NotHeld` / `StaleGeneration` / `RefusedBusy` / `Released`; `slotGeneration(slot)` is the identity a caller presents and `Track::releaseEligiblePlaybackRuntimeSlots` applies it per slot behind `noLiveOwnerForSlot`. The two memory-pressure sites (`releasePlaybackMergedMidiEventsMemory`, `invalidatePlaybackMergedMidiEvents`) now call it, and the predicate-free `TrackPlaybackRuntime::resetAll` was deleted with its last caller. The per-slot runtime generation now advances on release, making the lease's release path live for the first time. No new state — the identity is the S3.1 lease generation, so `sizeof(TrackPlaybackRuntime)` stays 32 and RAM1 is byte-identical. Both fixtures are **red-proven**. Full native `2,444 passed / 1 skipped`. **Open:** `DeferredSaveJobStages`' footer read, and no caller yet runs the sweep on a role transition (S4.3/S4.4); the two generation identities remain separate where a finalizing interval must keep its lease. Record: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § S4.1/S4.2.

**2026-09-25 — S4.2 (finalization identity) — generation-qualified `captureStopOwner`.** `captureStopOwner` was a raw pointer cleared by address identity, and ring entries are reused at stable addresses, so a predecessor's late cleanup could null a successor's marker. `LoopPlaybackRuntime::captureStopOwnerGeneration` now records the owner's identity beside the pointer, and `setCaptureStopOwner(admission, generation)` / `clearCaptureStopOwner(admission, generation)` are the only writers — the clear is address- and generation-qualified, a mismatch being a rejected no-op. Set site passes the admission's `frozenGeneration`; the clear site is `TrackPreparedStopCommit::completeDeferredCleanup`. Fixture `test_a_late_cleanup_cannot_clear_a_successor_finalization_marker` covers the A-releases / C-reuses / late-A sequence with the matching-identity control and is **red-proven**. Cost is zero: the field fit existing padding, so `sizeof(LoopPlaybackRuntime)` stays 6,344 and RAM1 is byte-identical. **Still open:** the per-slot runtime generation is not bumped on release (that belongs with S4.1's `(slot, generation)` release entry, since releasing at the stop edge would drop a finalizing interval's lease), so the lease's release methods remain uncalled. Record: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § S4.2.

**2026-09-25 — S4.1 (first slice) — an interval's stop no longer clears a sibling slot's ledger.** The release surface was track-global: while a predecessor finalizes on one slot, a sibling slot's stop swept **every** allocated slot's ledger, erasing the predecessor's open-note suppression state. `Track::clearStoppingIntervalLedger()` is now the named per-interval owner and both converted stop routes call it (`handleNoteEditFold`'s `endInPlaying` branch and `stopOverdubbing`'s fallback); `Track::sendAllNotesOff` keeps the deliberate track-global sweep, which now loops the per-slot `LoopPlaybackRuntime::clearLedger()`. Fixture `test_a_stop_clears_only_its_own_slots_ledger` is **red-proven** against the pre-fix behavior — and the first version of that fixture was a false green (it drove the ordinary stop route, which takes the deferred admission path and never reached a converted site), recorded rather than quietly replaced. Full native `2,441 passed / 1 skipped`; capture-serial builds with `variables` unchanged (`code 391,912 → 391,960`). **Remainder, still open:** the track-global `resetAll` conversions under memory pressure (`releasePlaybackMergedMidiEventsMemory`, `invalidatePlaybackMergedMidiEvents`, which also discard the refusal), the `DeferredSaveJobStages` footer read, and the `(slot, generation)` release surface with the generation carried onto `captureStopOwner` (S4.2) — which is why the S3.1 lease release methods are still uncalled. Packet and record: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § S4.1.

**2026-09-25 — S3 gate — acceptance suite `test_runtime_working_set_ownership` landed; gate still open on device items.** The suite is the stage's acceptance over the production admission paths (it deliberately does not restate the unit suites): boot/load leaves a committed slot with no working set while the record arm on the same slot establishes `PlaybackWindow | LiveCapture`; a repeated admission does not churn the held set or generation; release is refused by each owner kind and allowed once it retires (latch, in-flight handover, and a stop admitted through the production entry on an interval opened by `startOverdubbing`); selection acquires nothing. Linked as a firmware-boundary suite in `scripts/native_test_link.py` using the continuous-capture stub set. 4 cases green; full native `2,440 passed / 1 skipped` (98 suites); `test_playback_prewarm` unchanged; firmware RAM1 byte-identical (test-only). **The gate is not closed:** the S2b device gates (S2b.3 post-restore half, S2b.5, the transferred S2b.7 legs) and the S2.2/S2.3 current-root per-class budget are device measurements. Record: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § S3 gate.

**2026-09-25 — S3.3 — selection acquires nothing (fixture only).** `test_s3_3_selection_and_browsing_acquire_no_working_set` browses three committed slots three times through `SlotStateMachine::setSelectedSlotIndex` — the mechanism the UI gesture ends in — and asserts every slot still holds `WorkingSetRole::None`, generation 0, and no slot runtime at all, so selection cannot depend on working-set availability. The positive control is the record arm setting `PlaybackWindow | LiveCapture`, plus a repeated admission that must leave the held set unchanged. The observation needs no new state: the first cut added churn counters and the `sizeof(LoopPlaybackRuntime) == 6,344` pin rejected it, so the counters were dropped rather than growing the per-slot runtime for test observation — firmware RAM1 is byte-identical to S3.1. Recorded boundary: the manager entry `setSelectedSlotIndex` also calls `StorageManager::prioritizeLoopSlotRestoreForFocus`, a fail-closed off-path stub in this suite (same boundary an existing test records); the storage half of a switch belongs to the load/restore suites. `test_continuous_capture` 101/101; full native `2,436 passed / 1 skipped` (clean build). Packet and record: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § S3.3.

**2026-09-25 — S3.2 — liveness predicates named per owner kind, with a discriminating matrix.** `Track::noLiveOwnerForSlot` now reads as four named terms — `hasLiveCaptureStopJobForSlot` (ring live window only), `hasAcceptedInteractionForSlot` (S4f.1 latch), `ownsLiveFinalization` (`captureStopOwner`) and `ownsInFlightPreparedHandover` (`preparedPairRequestPending`) — with identical meaning, order and callers, so release is neither widened nor narrowed. Two owner kinds remain **inexpressible** and are recorded in place with their stages: the job-owned undo root (S5.2a) and pending deferred reconstruction (S5.2e). The published prepared pair stays deliberately *not* a term, since it is the slot's playback authority while it sounds. New fixture `test_s3_2_liveness_predicates_are_discriminating_per_owner_kind` holds each kind alone, asserts the composite refuses only for it, and pins two inverse controls (an unowned slot allows release; a slot holding only a published pair plus an admitted working set allows release). `test_continuous_capture` 101/101; full native **2,520 passed / 1 skipped**; RAM1 flat; capture-serial builds. Packet and record: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § S3.2.

**2026-09-25 — S3.1 — `WorkingSetLease` vocabulary landed and routed to the real admission points.** `WorkingSetLeaseSlot` (per-slot, carried by the externally allocated `LoopPlaybackRuntime`) and `WorkingSetLeaseTable<MaxSlots>` (multi-slot census) give the runtime one identity vocabulary over a declared cap: O(1), allocation-free, reporting `CapReached` / `InvalidRole` / `NotHeld` / `StaleGeneration` instead of growing or asserting. Both cold admission points now record `PlaybackWindow | LiveCapture` — the record arm in `prewarmPlaybackForSlot` and `admitOverdubStartWorkingSet` for a boot-cold slot — and the boot pass moved into `Track::prewarmBootPlaybackRuntimeSlots()` so the slot's owner holds the admission predicate. No resource call moved and capture admission is not newly gated, so behavior is unchanged (`test_playback_prewarm`, full native **2,434 passed / 1 skipped**). RAM1: `variables` flat, `code 391,960 → 391,912`, `padding 1,256 → 1,304`; `sizeof(Track) 384` and `sizeof(LoopPlaybackRuntime) 6,344` unchanged — the per-track `TrackPlaybackRuntime` placement was rejected by its pin, because 16 B there costs 128 B of DTCM in the 8-way array. **Still open:** nothing releases yet, so the lease is write-only and the generation never advances; S4.1's release calls `releaseAllWorkingSetLeases()` and S4.2 reads `workingSetGeneration()`. Packet and completion record: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § S3.1.

**2026-09-25 — S1.6c.2c — loop-edit tempo fader shipped (DEC-086, device-verified).** Fader 4 on the loop-edit control channel: **ch16 CC102, one BPM per step over 60–187 BPM**, applied through `ClockManager::setBpmFloat`; session entry sends the current tempo back so the motor parks at the whole-BPM value. Mapping law is the dependency-free `TempoMapping` header (native `test_tempo_mapping` 10/10). Device: CC60 → **120.0 BPM exactly** (`captures/tempo-pin-120b/`), and CC32/100/64 → 104.3/172.8/136.5 under the first 72–200 law (`captures/tempo-fader-01/`, probe `scripts/hitl_tempo_fader_probe.py`); RAM1 unchanged. Reason: the 24-bar capacity fixture could not pin the device tempo, which is the 85.4-vs-120 BPM defect below. Control-surface scope added at the owner's request, recorded as its own decision.

**2026-09-25 — S1.6c.2b — no-clear selection fixture repaired (host-only, committed).** The no-clear record path skipped the clear *and* the selection, because slot selection ran only inside `_run_clear_before_record`; the recorded `snapshot-reuse-24bar` run therefore armed track8/slot1 against a configured slot 4. `_select_loop_slot_before_record_arm` now stops the transport, selects the Loops row and retires the select-started ghost capture, and the arm step proves the slot from a bounded wait on the target's DISP row (`_await_observed_record_slot`) before pressing arm; the edit path shares the helper and passes `select_loop_slot=False`. Selecting under a running transport starts a ghost capture whose later stop leaves ARMED and strands the take, so the transport stop is part of the fix. 17 focused tests green; host script suite unchanged at its 8 pre-existing failures. **Device gate status:** the corrected 24-bar run reached RECORDING but the fixture stream still did not start (1 note-on, 0.6 s take, `record stop did not reach PLAYING`), and the authorized witness slot 4 was left populated by the failed runs — the guard correctly refuses. Resume only after the owner re-verifies an empty target and with the tempo pinned via DEC-086. See [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § S1.6c.2 fixture repair / support / remaining gate.

**2026-09-25 — S1.6c.2 — snapshot construction reuse implemented, device gate open.** Candidate-private pitch/page/membership edits now persist across one construction (DEC-085), with cached tail/count updates and cached-event sort-state reset. Native **2,402 passed / 1 skipped**, focused snapshot **42/42**, target build green; RAM1 unchanged (code391,368 + padding1,848; variables96,160; locals34,912). Image `94b09b51…`. Fresh N=56 census: external payload **7,006,576 → 3,238,256 bytes**, snapshot slabs **151 → 36**; all 64 observed slot records match the earlier census (both lack consistent-view attestation). This saves **3,768,320 bytes (~3.59 MiB)** without deleting content or shrinking the capture ring.
Stopped-only follow-up (`captures/snapshot-reuse-24bar/stopped-heap.log`): internal arena356,800B, allocated chunk bytes199,392B, free chunks157,408B, largest free chunk122,768B, unused tail12,288B; external payload5,246,288B, free3,100,380B, largest request3,046,620B. Both scans complete. This disproves interpreting the *stopped* 12KiB tail as total available heap. It does not retroactively measure free lists at the earlier live refusals; tail-based snapshot admission remains the next hypothesis to verify without live scans. No leak/fragmentation verdict from this one stopped reading.


The stress rerun is **INVALID_TARGET_INTERRUPTED**, not acceptance: configured track8/slot4 but the no-clear harness armed selected **track8/slot1**, recorded/overdubbed and persisted `slot:7:0`. Interrupted and explicitly stopped; `STOP_STATUS` confirms STOPPED/pending=0. No clear/delete/rename or cleanup. User reports device85.4BPM, explaining host24-bar timing yielding17devicebars (13,056ticks). No fatal/external refusal, but seven late `snapshot_reserve/heap_low` rows and minimum heap tail12KiB mean overall heap correctness remains open. Host precondition now fails before gestures unless the latest device-time DISP since track selection verifies the configured slot; host tests56/56. No further content runs until the selection fixture is corrected. Role-specific reservations remain unchanged pending their transition/admission contract. Evidence: `captures/snapshot-reuse-preflight/`, `captures/snapshot-reuse-24bar/`, `captures/snapshot-reuse-image/`. Recorded in local runtime checkpoints; capacity acceptance remains open.


**2026-09-25 — S1.6c.1 — known owner lifetime fixes delivered in the working tree:** whole-take release now frees origin/rank side arrays (24,576 bytes of previously retained payload); draw-source compose reuses pending-paint backing, filters slide inclusion in place, and retires obsolete paint before allocating a larger copy. Selected-bridge authority and compose/paint separation preserved. Red→green regressions; focused 301/301, full native **2,399 passed / 1 skipped**; capture-serial SUCCESS, RAM budgets unchanged. Final image `10695e07…`. **Capacity is not fixed:** the same N=56 24-bar HITL (`captures/heap-ownerfix-24bar-02`) still fatals ~47.62 s into RECORD on a 6,096-byte DisplayNote vector reserve, before overdub. S1.6c — owner correction/capacity revalidation remains open. No live heap scan or content-clearing workaround. Board recovered: N=56, chunks403, target empty, zero differences in compared content fields across 64 slots. Edits uncommitted.


**2026-09-25 — S1.6b — original workload rerun:** same effective N=56 24-bar record/24-bar overdub configuration, user track 8 slot 4, no CLEAR, image `a71b50eb…`. `captures/heap-rerun-24bar-01/` reproduces fatal exhaustion ~32.08 s into RECORD: 8192-byte DisplayNote vector growth request, caller `0x6008c919`, external live 8144 KiB / 1176 blocks, reported internal tail 8 KiB. No overdub or record stop reached; harness SERIAL_INCOMPLETE. No heap scan during the workload. Stopped preflight scan complete; no same-boot after-sample because firmware aborted. Recovery returns N=56 / 403 free chunks, target empty, zero differences in 10 compared content fields across 64 slots (not a byte/write guarantee; observation consistent_view=false is not a new stability proof). Exact display container and leak/fragmentation remain unresolved. Evidence: active runtime review packet and `captures/heap-rerun-24bar-01/result-summary.json`. No firmware/script changes in this rerun; OpenSpec gates remain unchanged.


**2026-09-25 — S1.6d — stopped heap diagnostics:** added capture-only `!HEAP_STATS` under MemoryMonitor. No periodic/live allocator inspection or allocation probes. Refuses transport, active tracks/capture/stop work, deferred storage/prewarm, selected external clock, and pulses received within the last second. Raw allocator chunk/gap metrics, unused internal tail, completeness and scan duration are separate. DEC-084 records the explicit stopped/clock-quiet exception. Native **2,394 passed / 1 skipped**, focused diagnostic **5/5**, capture-serial build green (RAM1 variables 96,160; code 391,368; padding 1,848; locals 34,912). Initial stopped readings complete in ~49 ms; final stopped/clock-refusal device gate passed in `captures/stopped-heap-03/` (exact refusal, no scan). S1.6d is checked. Work remains uncommitted. This adds evidence, not a malloc leak verdict or the S1.6c — owner correction; capacity gates remain open.


**2026-09-25 — S1.6 heap correctness:** physical external accounting now excludes internal
fallback, balances tags on address reuse, and reuses saturated-table tombstones. Three native
red regressions → 5/5; full native 2,389 passed / 1 skipped; capture-serial green and flashed,
RAM1 unchanged. Capacity sweeps remain paused. The N=56 canonical 24-bar run still fails in
recording: 5,216-byte request from `DisplayManager::resolveDisplayNotesByDrawSource`, resolved
against pinned ELF, external live8,142KB with no accounting misses. This is not attribution of
the older 8KB request or proof of a leak. S1.6a is complete; S1.6b allocation/lifetime attribution
and S1.6c owner fix remain open, as do S1.5 overdub refusal and S2b.3 workload-peak budget.
See active runtime-working-set-ownership ARCHITECTURE-REVIEW.md § S1.6 and
`captures/heap-attribution-24bar-01/allocation-attribution.json`. Earlier lower-N/reclassify
recommendations are superseded by this heap-correctness priority. Work is uncommitted.
Recovery confirmed N=56 / 403 free chunks, zero observed content-field differences across
64 slots, and the failed target still empty. Board recovered; no clear/delete/rename.


**2026-09-24 working state — supersedes the 2026-09-23 preamble; the newest session entries are in
[§ Now implementing](#now-implementing).**

Three facts define what may be implemented right now:

1. **`runtime-working-set-ownership` has no *currently eligible* native task left.** Native is complete and
   green including S2b.8 and S2b.7 (full native **2,352 / 1 skipped / 0 failures**, 2026-09-24). Open:
   S1.1's per-owner census, S1.3's device slope, S1.4's undo/root baseline, S1.5's device refusal point,
   S2b.3's post-restore baseline, S2b.5's device gate, the content PASS, D0.2's B07 allocation census (not
   measurable natively), and phases B–E. **Every one of those is a device leg**, and the stage order is not
   bypassed merely because hardware evidence is blocked.
2. **The restored-library instability now has an owner, and that change is CLOSED and ARCHIVED: Packet A is
   complete and Packet B's complete-manifest gate (2.2) is DEVICE VERIFIED PASS** (`restored-library-stability`,
   archived 2026-09-24). **Packet A — observation-only firmware plus read-only analysis — 1.0–1.5 COMPLETE and
   device-verified read-only 2026-09-24**; **2.2 — complete-manifest stability — PASS across three consecutive
   boots 2026-09-24**, 0 differences in the observed fields over 64/64 slots, observed manifest sha256
   `4e451a07d0a94c18…`, no destructive command issued; **2.1 not required for the reused fixture**;
   **2.3, Packet C and 4.2 are TRANSFERRED to `runtime-working-set-ownership` S2b.7 — drain-driven restore
   advancement** (unchecked there, independently reportable), so the library closeout has no remaining
   mandatory task and does not gate on them. The change existed
   because the set of slots **observed** committed after a reboot was not reproducible **as measured**,
   which blocks fact 1's device legs. Its first
   finding is already recorded: the previous blocker attribution (SD catalog reconcile) does not hold —
   `sets/index.bin` is a saved-set *folder counter* (`SetIndex{nextSequence}`), the shrinking boots report
   `reconcile_warnings=0`, `DIAG,lcr,skip,restore` is a content-resolution line, and
   `Track N loaded state: EMPTY` appears on stable boots too. **The read-only attribution finds no loss
   cause anywhere and identifies the shrinking count as window-scoped**; the library manifest — 36 payload
   slots / 8 initial-playback slots / 8 committed — **reproduces exactly across three consecutive boots**;
   contract **DEC-083**. It
   distinguishes **catalog omission / restore refusal / intentional eviction / content loss**, and needed
   per-slot authorization before any CLEAR.
   **S1.3 — slope-fixture definition (2026-09-24, sixteenth session, read-only):** the "28 unloaded" premise
   is **falsified** — all 36 payload slots are restored, 8 pass-backed and **28 published-root-backed** and
   resident; the controlled increasing-population fixture is defined.
   **S1.3 — representation correction (2026-09-24, seventeenth session):** the observation now reports the
   two representations separately and counts loaded slots as their **union**; "28 unloaded" is durably
   corrected to **28 root-backed**, and the three-boot gate's `committed_at_checkpoint=8` is recorded as the
   pass-backed subset it was. The existing library was revalidated once with the corrected telemetry (task 3)
   and the pinned residency measurements (task 4) follow. The sweep remains the unmeasured part.
3. **The restore-liveness change is ARCHIVED, not proposed.** `deferred-slot-restore-liveness` was archived
   2026-09-24 as `2026-09-24-deferred-slot-restore-liveness` with a reduced, accurately described scope. It
   delivered diagnosable preparation and named waits under transport, request identity with four terminals,
   pre-publication stale-identity rejection, the post-restore census checkpoint, a content PASS, and CLEAR's
   permitted completion branch. It did **not** deliver drain-driven restore advancement — now
   `runtime-working-set-ownership` **S2b.7**, bound by
   `resumable-midi-boundary-playback-maintenance` **task 3.3**, whose commit gate must not be relaxed — nor a
   runtime set change (now **S2b.8**, landed 2026-09-24 as external resets). The commit gate is **global**
   (`hasLoadLoopJobCommitPending()` is true for *either* job instance), so one request at commit blocks every
   other request's preparation (`waiting_job_capacity`); a blocked CLEAR refuses by name, leaves the slot
   untouched and the save owed; and the finite **100 ms post-load holdoff** is reported by name rather than
   waited out in the drain. No implementation work remains gated on applying that change.

**Scope note — landed work that is NOT tracked by an OpenSpec change.** The persistence-immediacy and SD
temp-write work of 2026-09-23 landed on plan docs and code only:
[`persistence_immediacy_clear_latency_refinement.md`](../Plans/persistence_immediacy_clear_latency_refinement.md)
(the one-chunk-per-turn mid-pass flush, the queued-capture-chunk invocation reason, DEC-081) and
[`sd_temp_write_truncation_refinement.md`](../Plans/sd_temp_write_truncation_refinement.md) (the
`openForRewrite` fix and the self-healing `reconcileSetIndexOnSd`). `deferred-slot-restore-liveness` (now
archived) addressed the **restore**-liveness half of the same defect cluster in part; **no change yet covers the save/immediacy
half**, and the restored-library instability itself remains unowned. That gap is recorded, not papered over: a plan is not implementation authority
([`docs/Authority/README.md`](../Authority/README.md) conflict table).

**Branch note.** `feature/runtime-working-set-ownership` is **738 commits ahead of `origin/dev`** with no
upstream configured, which is well beyond the repo's short-lived `feature/` convention. Any new change
should be cut from `dev` (or the branch strategy decided) before firmware work starts; nothing in this
document assumes the current branch.

**2026-09-23 `runtime-working-set-ownership` P0 landed on `feature/runtime-working-set-ownership` — S0 verified, S1.2 red characterization implemented; no firmware change.**
Implementation has started on the branch named by the owner, cut from the planning line (the change does not
exist on `dev`). S0's decisions were **verified against their landing sites, not re-decided**; S1.2's red
scaling fixture now exists in `test/test_continuous_capture` and fails exactly as the law predicts. Full
packet and evidence: [ARCHITECTURE-REVIEW.md](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) § P0; the
numbers are repeated in `tasks.md` S1.2 and in [PROJECT_STATE.md](PROJECT_STATE.md).

- **Red characterization (S1.2, invariants 1/25/34).** At a constant active set, boot admission reserves
  **96 chunks with 1 recorded slot and 120 with 4** — exactly **8 chunks per admitted boot slot**
  (`PassConfig::ACTIVE_OVERDUB_TRANSFER_CHUNKS`, the `canonicalCaptureOutput` reservation) — and resident
  working sets go **8 → 11**. `WORKING_SET_RED=1 pio test -e native -f test_continuous_capture` reports
  **2 failed, 78 succeeded**; the default gate keeps only the well-formedness case and stays green. The
  gate is removed when S2b.1 (with the S3/S4 release) lands.
- **Gates run:** full native **2,263 / 1 skipped / 2,262 succeeded / 0 failures**; `teensy41-capture-serial`
  build SUCCESS with RAM1 padding **872**. **No device run** — S1.1's per-owner census, S1.3's slope and
  S1.4's undo/canonical-pin baseline remain open, so the S1 gate is not closed.
- **First production behavior landed: the accepted-stop latch (S4f.0/S4f.1-Stop/S4f.2/S2b.6-partial).**
  DEC-078's product rule now holds for the stop: one press under a denied canonical-copy target is
  **accepted and retained**, then executes exactly once when the target returns — no second press. New
  default-gate fixture **B02** proves the sequence (retained → exactly one job bound to the interval's
  slot → exactly one completion). **S3.0 measured a real regression before it shipped:** the
  `unique_ptr` form of the latch cost 64 B of RAM1 and took padding 872 → **856**, below the documented
  floor; moving it into the already-externally-allocated ring object restored
  `padding 872 / locals free 34,944`. Full native **2,264 / 1 skipped / 0 failures**. **Boot/load
  reservation removal (S2b.1) is deliberately NOT in this step**; S4f.1/S4f.2/S2b.6 are partial and
  S4f.5, S4f.3, S4f.4's epoch, S4.0 and S2b.2–S2b.5 remain open.

- **S2b.1 + S2b.2 landed: boot/load no longer reserves canonical-copy capacity or a transfer horizon per
  committed slot, and the residency law is now green in the default gate.** The three former red
  characterizations (S1.2 a/b and B01) pass and the `WORKING_SET_RED` gate is deleted. A loaded slot's
  overdub start admits its own reserve set (views scratch, transfer horizon, prepared-companion-history
  horizon) without gating START on the canonical copy, and the accepted stop's park acquires that copy;
  the retire block drains the store so a retiring job never leaves it populated.
  **Reported deviation:** RAM1 `padding` is **856 vs the recorded 872 line** (16 B of linker alignment,
  `code + padding` constant, **no bank cross**, `locals free` unchanged) — flagged for the architect
  rather than redefined. Full native **2,268 / 1 skipped / 0 failures**; firmware build SUCCESS.

- **Next:** the remaining D0.2 controls (dispatch pause-and-step; linked load and transport-retarget
  paths; B07's allocation census, which is not measurable natively) → S2b.0/S2b.1, the first behavior
  change, which is expected to turn all three red cases green.

- **D0.2/D0.3 partial, same session (test file only).** P1's controls now exist: test-controlled
  transport state, reservation denial/recovery by real pool-credit exhaustion, and a read-only ownership
  census. **B01 is red as designed** — with 4 committed slots and one active role, the 3 inactive slots
  hold **8 canonical-copy credits each (24 total)** where the law requires zero
  (`WORKING_SET_RED=1 pio test -e native -f test_continuous_capture` → 3 failed, 78 succeeded). Still
  outstanding and recorded rather than implied: the dispatch control, the linked load/transport-retarget
  paths, and B07's allocation census. **No production behavior has changed.**

**2026-09-23 — chat reference readability.** Agent replies now pair every stage/task/gate/decision ID
with a short descriptive name, including repeats and link labels. Canonical rule:
[NAMING — short names beside IDs](../Authority/NAMING.md#short-names-beside-ids-in-chat), linked from
AGENTS.md and the always-applied naming rule. Presentation only; no runtime or task-status change.


**2026-09-23 — deterministic implementation workflow strengthened (DEC-080).**
[Implementation readiness](../Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice)
now requires a proportional execution packet for every future slice, including steps of existing changes:
fixed transition oracle, controlled production-path fixture, repeatable recipe and actual evidence before
checkoff. AGENTS.md, Cursor rule, OpenSpec config and agent roles share that authority.
The current plan's [ARCHITECTURE-REVIEW.md](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)
adds P0–P8 and S2b cases B01–B07; D0 tasks prepare controls/fixtures before behavior changes. Fixtures are
**specified only**, not implemented or run. No firmware or hardware action; no implementation task checked off.


**2026-09-23 — runtime-working-set-ownership implementation handoff corrected (DEC-079).**
The five targeted review findings are resolved in the active OpenSpec's
[IMPLEMENTATION_CONTRACT.md](../../openspec/changes/runtime-working-set-ownership/IMPLEMENTATION_CONTRACT.md)
and task/spec text: self-contained S2b with pulled-forward Stop/liveness prerequisites, per-holder
cardinality budget, job-retaining regeneration, S4c contract-only with phase F explicitly uncreated,
and stop/switch/start composition with explicit supersession. S0/S1 → S2b remains the first slice;
its gate uses current resident content and a synthetic descriptor fixture, not a 60-long-slot library.
Current whole-loop playback roots stay accounted until F. No firmware, build, upload or device run;
all implementation checkboxes remain open. This supersedes earlier handoff wording, not device evidence.


> **From now on, prove audible content with the `hitl-content-verification` pipeline, not the
> base-run verifier.** `host_midi_hitl.py run --preset base` is still the content-creator, but its
> own playback verdict is not the check: the managed harness aborted four consecutive runs on
> setup-state races (`successor-wrap-48/49/50`: `transport still active after stop; clear blocked`
> then `Record stop did not reach PLAYING`, each with `loop length 0` and no host note ever reaching
> the device) while the device was fine. What actually proves playback is
> `.agents/skills/hitl-content-verification/scripts/dump_and_capture.py` +
> `verify_playback_content.py`, which records the device's MIDI output and compares it to the
> committed inventory row for row. DEC-071 is device-verified that way; the "Result:
> INCONCLUSIVE_CAPTURE (SETUP_UNCONFIRMED)" that the base report still prints is a setup-oracle
> verdict and is not evidence against the content.

> **Implementation scope:** the authoritative scope is
> [§ Now implementing](#now-implementing). Everything above that section is a
> reverse-chronological session journal (newest first); read it for evidence, not
> for scope. `AGENTS.md` step 2 points here.

**2026-09-19 `runtime-working-set-ownership`: the first change through G0 → G1 → G2 — and the G2 red team
returned BLOCKED, so the design input landed instead of firmware.** G0 (six read-only traces) verified the law
violation with citations: `runtimeBySlot_` is per slot, lazily allocated and **never released on slot
deactivation**; a sounding slot hard-requires a resident whole-loop root
(`tryGatherPublishedSnapshotEventsInWindow` returns false without one); a stop meeting a busy owner is **dropped**
and must be pressed again (`successor_stop_denied_capacity`, `reentryDecision("deny", …)`); undo entries pin
snapshot residency with no demotion path; and no test anywhere measures residency against recorded-slot count.
G0 also found the open decisions already recorded as DEC-076 and a stale scope envelope (fixed below).

G1 turned that into an architecture (14 decisions: occupancy leases keeping the O(1) hot-path index,
generation-qualified identity, per-interval finalization, provision-at-cold-boundary/bind-at-MIDI-boundary,
window-derived sizing, the acceptance latch, standby/visual/cold-undo/persistence tiers, header-only work-item
vocabulary). The G2 red team then **falsified eleven paths** — most importantly that `finalizeCaptureAndSelectSlot`
calls the void `stopOverdubbing()`, ignores a denial and moves `activeLoopIndex` anyway (B1); that the latch would
execute a PLAYING stop after transport stopped (B2); that `beginDeferredOverdubStop` is active-slot-keyed (B3);
that undo/redo is a second active-slot authority (B4); that the clock-path commit allocates via `trySlot` (B5);
that release/liveness is track-scoped while resources are slot-scoped and a missing runtime stalls forever (B6);
that **boot reserves 8 pool chunks (41,056 B) per committed slot** — 480/512 at the 60-slot target, reintroducing
the RC-MEM-5 state (B7); that a load can wedge a pinned finalization (B8); that the persistence guards and mid-pass
resolver do not know an accepted stop (B9); that cold-undo descriptors keep canonical chunks pinned and ClearSlot
has no snapshot-free reconstruction (B10); and that the contract's own fields add ~1.4 KB of DTCM against 872 B of
usable padding (B11).

Landed as design input, **no firmware change**: **DEC-078** (accepted — every interaction accepted, finalization
**per interval**, amending DEC-067 and DEC-076 point 2 and withdrawing DEC-076's "finalization is a queue"
bullet); `design.md` **Revision 18** (the eleven findings, the latch, cardinality/liveness table, invariants
32–36, placement rules, cold-undo pinning, caps); `tasks.md` restructured (S2.9 **extracted** to the sibling
change `windowed-playback-generation`, new **S2b** boot/load stage, per-slot liveness S4.0, slot-parameterised
acceptance S4f, R1–R11 corrections); and the stale 32-lease envelope corrected here.

**2026-09-19 — APPROVED by the owner (review of revision (4)), with two corrections that are now landed.** The
corrections: (1) rename `finalization_lease_stop_denied` → **`accepted_stop_finalizer_wait`**, because "denied"
described the removed architecture and would mislead a future trace into reading a DEC-078 violation; (2) define
**`accepted_stop_park_terminal`** as a fault/escalation state of the execution mechanism that **never cancels** the
accepted interaction — the stop stays authoritative until executed, explicitly superseded, or the session is
destroyed/reset (spec scenario + S4f.10(g)). Also landed from the review: **transaction-aware cold-undo eviction**
(remove the oldest prefix that leaves the oldest retained descriptor independently reconstructable, not merely the
oldest entry); the **execution order S0/S1 → S2b → S3/S4 → S4f → S5 → decoupling**, so the 32/64 envelope does not
block fixing today's residency defect; and the **mandatory continuous device session** (S7.6: load ~60 slots →
play → record A → STOP → switch B → START → STOP B once while A finalizes → B stops once → switch C), requiring
0 denied interactions, 0 second presses, 0 lost capture, 0 wrong-slot publication, 0 stale-generation release,
0 playback interruption, bounded MIDI gap, and steady-state RAM independent of cold recorded slots. Design
revisions stop here unless implementation contradicts the contract; the next actions are the ownership-transfer
signature, `DECISION_REVIEW` + `PREFLIGHT`, then S0/S1 followed by S2b.

**2026-09-17 OpenSpec 5.12 / D8 LANDED and device-verified: a staged capture pass is owned by its
interval (DEC-072), and the `CompanionHistory` checkpoint that had been red for two days is green.**
The 5.5 blocker was that one `Loop` scalar, `pendingCapturePass_`, served two intervals: a deferred
predecessor staged its pass at `PrepareTransfer`, a successor's `beginCapture` discarded it
unconditionally, the predecessor's `beginPreparedCompanionHistory` was then refused forever by its own
`!hasPendingCapturePass_` precondition, and its job held `captureStopOwner` and the frozen role for
the rest of the session. Guarding the discard only preserved the `AlreadyPending` collision at the
successor's own staging, so the fix moves ownership instead:

- `CaptureStopJob::pendingPass` + `ownsPendingPass`; the job stages into the Loop slot and
  immediately takes the receipt (`Loop::takePendingCapturePass`, chunk-list swap, non-allocating) in
  the same non-yielding `PrepareTransfer` grain. `beginCapture` keeps its unconditional
  producer-local clear, and a deferred job never depends on the slot afterwards.
- `Loop::transferCapturePass` / `canTransferCapturePassWithoutAllocation` /
  `commitPreparedCapturePassAtomic(transferred, pass)` plus
  `commitPreparedCaptureAndCompanionsAtomic(transferred, owner, pass)` for a specified receipt; the
  Loop-slot forms are thin wrappers, so the live path is unchanged.
- `PreparedCompanionHistory::capturePass` is bound by address at
  `beginPreparedCompanionHistory` and validated by `preparedCompanionHistorySourceCurrent`, so a
  successor can neither destroy nor rebind the predecessor's verification source. A caller that names
  no receipt still resolves the Loop's own staged pass (the live producer path and fixtures); a
  deferred job names `&job.pendingPass` unconditionally at its one production call site, so a missed
  ownership transition denies instead of silently reading a successor's receipt.
- Reader sweep in the same commit: the undo identity in `stepDeferredUndoCollection` and the two
  `canPublish` sites; `requiresBridgeRefresh`; `commitStopped[WithUndo]` / `commitWithUndo` /
  `commitSupersededCausalBase`; `stopPathChunkRefCount`/`stopPathEventCount` and
  `collectStopPathStorageStats`/`logOverdubStopStage` (the deferred debt-probe call site passes
  `Track::ownedPendingCapturePass()`); `#STOP_HISTORY` also reports `pass_id`; and the job releases and
  clears its receipt at retirement.
- RAM1: `CaptureStopJob` now owns a `PendingCapturePass`, so the implicit
  `Track::CaptureStopJobs::CaptureStopJobs()` became non-trivial and, emitted into ITCM, cost one
  32 KB FlexRAM bank (`locals 6,400 -> -26,368`). Defined out-of-line and FLASH-resident, the build is
  back to RAM1 variables 91,904 / code 425,816 / padding 168 / **locals 6,400**.

Evidence. `ONLY_COMPANION=1 pio test -e native -f test_continuous_capture` green (the former red
reproducer); `REENTRY_RED=1 pio test -e native -f test_continuous_capture` 57/57; full native 2,221
cases / 1 skipped / 0 failures; `test_playback_prewarm` 173/173. New
`test_simultaneous_interval_owned_staged_passes` asserts the invariant directly: job A owns pass A
with the Loop slot empty, B stages a distinct pass (different identity, chunk storage and session
index), A's transfer appends exactly one canonical pass by pointer move with the canonical row vector
unchanged in capacity and address, B's receipt is untouched, and B then consumes B exactly once.
Device, managed HITL `base` (8-bar record / 2-bar overdub, temporary `d8-ownership` stimulus variant),
[`captures/successor-ownership-06`](../../captures/successor-ownership-06/session_20260917_230432.log):
**`Result: PASS`**, `overlap=True denied_stop=True retirement_ok=true` — 50.903 s A's stop admitted;
51.392 s B starts 1,122 ms before A's release; 51.935 s B's quick stop refused
(`Overdub stop refused: stop_job_pending phase=9 dest=0`) with B still recording; 52.514 s A
`Reentry released` while B stays live; 52.979 s B's own stop admitted and 52.996 s B's job retired.
0 `FATAL` / `#MEM_DENIED` / `#MEM_EXT_REFUSED`, no `stop_job_pending phase=7`, no
`companion path stalled`, `overflow_free=true`, display `overdub_ok`/`transport_ok` true.

Harness correction in the same change: `_verify_successor_reentry` accepted only the DEC-067 denial
wording (`Overdub stop deferred: deferred/frozen admission role is occupied`), so every
current-firmware probe reported `successor_reentry_stop_not_denied` while the device log carried the
refusal (`successor-ownership-01`/`-03`). It now accepts both wordings (`REENTRY_DENIAL_MARKERS`) with
a regression test; `scripts/test_capture_transitions.py` 42/42. OpenSpec 5.5 stays open for its own
remaining work (the complete parameterized checkpoint set, and promoting
`test_adversarial_reentry_at_a_companion_checkpoint` into the default gate — it is still behind
`REENTRY_RED`).

**2026-09-17 multi-pass overdub playback is device-verified (DEC-071), and the base-run harness
was the thing that was broken.** The user's report was "only the wrapped part of the overdub plays";
the shape is a *multi-pass* overdub (8-bar record, 8-bar overdub starting at bar 3, so the wrap
commits one pass and the stop commits a second). `Loop::tryGatherPublishedSnapshotEventsInWindow`
enumerated queued passes but not **wrap-journaled** ones, so the wrap pass was in neither the
published root nor the stale-root overlay once its session closed -- the display resolved it from
`passes`, playback never emitted it. Fixed in `2b948d2a`.

- Native: `test_stale_root_overlay_keeps_a_journaled_wrap_pass_after_session_close` fails without
  the fix (`Expected 1 Was 0`); full native 2,215 / 1 skipped / 0 failures, RAM1 locals 6,400.
- Device, the reported shape (`captures/content-verify-52` + `captures/content-verify-52-playback`):
  storage `384 + 157 == 541`; **all five playback cycles `inventory_equal: true`,
  `exact_order: true`, 0 missing, 0 extra**; per-bar effective
  `{0:38, 1:48, 2:62, 3:72, 4:72, 5:72, 6:72, 7:72, 8:33}` -- the pre-wrap bars are in the audible
  output. (That record sealed at 6,912 ticks = 9 bars for an 8-bar request: the known stop-time
  quantisation slop, unrelated.)
- The base-run verifier also had two real defects, both fixed: `_prepare_deferred_save_before_clear`
  toggled the transport blind and blocked clear before the note stream (`c1456fa9`), and
  `dump_and_capture.py` opened MIDI after the serial open, which resets the board and removes the
  ports (`7e9810ee`).
- Capture observability: `DIAG,stop_grain` was 71% of a run's `#CAP` bytes and overflowed the ring,
  evicting the `snapshot_unit` rows the run is read for. It is now sampled 1-in-16 and the ring is
  1 MiB; `[env:teensy41-capture-serial-no-grain]` (`SESSION_CAPTURE_GRAIN=0`) drops the family for
  acceptance runs. A runtime serial flag was tried and reverted: it cost 4 KB of the RAM1 local
  allowance (2,304 left) even factored out as a FLASHMEM helper, so the switch is a build flag.
- **RESOLVED 2026-09-17 by OpenSpec 5.12 / D8 (DEC-072)** -- kept for the diagnosis: the OpenSpec 5.5
  `CompanionHistory` checkpoint used to be red. A predecessor paused there never re-admitted its
  companion work once a successor started, so it held `captureStopOwner` and the frozen role for the
  rest of the session -- the same wedge as the device's `Overdub stop refused: stop_job_pending
  phase=7 dest=1`. Measured then: the loop's prepared companion horizon was **unowned**
  (`preparedCompanionHistoryInFlight == false`) and parked at `Prewarmed`, so the driver's
  `preparedCompanionHistoryCanStep` was false while `beginPreparedCompanionHistory` was re-entered
  every turn; a `prewarmPreparedCompanionHistory` reset of an in-flight preparation and a transport
  command-epoch mismatch were both ruled out by measurement. The reproducer
  (`test_adversarial_reentry_at_a_companion_checkpoint`) is green now; it is still gated behind
  `REENTRY_RED=1`, and promoting it into the default gate is 5.5's remaining work.
- Root cause, located on the third turn and fixed by DEC-072: the successor's own
  `Track::startOverdubbing` -> `Loop::beginCapture` discarded the predecessor's staged pass with an
  unconditional `discardPendingCapturePass()`. `pendingCapturePass_` was one Loop scalar shared by
  both intervals, so the predecessor's companion admission (`!hasPendingCapturePass_`) was refused
  forever, the work stayed at `Prewarmed`, and the job held `captureStopOwner`. A guarded
  `beginCapture` (discard only a pass that retires the live capture) made the `CompanionHistory`
  checkpoint retire but broke the successor's own staging at `PrepareTransfer` (`AlreadyPending`),
  so the landed fix gives the staged pass an owner: the job takes it in the same `PrepareTransfer`
  grain, binds it in the companion history by address and consumes it at its own `AtomicTransfer`.
  See the DEC-072 entry at the top of this journal. Iterate with
  `ONLY_COMPANION=1 pio test -e native -f test_continuous_capture`.
- Ignored per the owner: one deterministic coalesced note-off on the 5-bar shape
  (`content-verify-51-playback`, tick 128 / ch 4 / note 78 / velocity 0).

**2026-09-17 pacing FIXED and verified; the successor device case is blocked by a firmware
guard, not the harness.** [`deadline-pacing-01`](../../captures/deadline-pacing-01/session_20260917_133833.log)
after a fresh flash: `record loop length (final ticks): 6144/6144 (raw 6380)`, **Result: PASS** -
an 8-bar request now records an 8-bar loop, with the assertion that caught the 10-bar records
still strict. Item 2 is closed.

Successor-reentry device case: the probe and its oracle are implemented and the presses now fire
from the main flow (a background thread's mido sends never reached the device - no
`MIDI Button A` line), but the device never re-enters OVERDUBBING after the probe press.
The reason is a firmware gate, not the harness: `TrackManager::startOverdubbingTrack`
(`src/TrackManager/TrackManagerCaptureQueue.cpp:212`) returns early while
`track.hasPendingDeferredOverdubStop()`, logging "overdub admission deferred while finalization
is pending". That manager/Button-A successor guard is the one task 2.3 deliberately left in
place, so the *button* path cannot open a successor interval while the predecessor finalizes -
even though the Track-level admission the native tests exercise does permit it. The probe will
be the device gate for that case the moment the guard is removed (the next RC); nothing further
is owed on the harness side.

**2026-09-17 the over-run is per-step sleep drift, not a bar-grid problem - and the BAR signal
is not available during a take.** Two measurements closed this out:

- The session log for [`bar-anchor-08`](../../captures/bar-anchor-08/session_20260917_133506.log)
  contains only **4 `BAR` rows** (bars 0-3, i.e. before the note stream starts) out of a take that
  ran 10 bars, and no `ST,Track,...,RECORDING` row either. The device/capture tier simply stops
  delivering `BAR` rows once notes flow, so a device-bar end condition can only ever see the
  first few bars - BAR-anchored pacing is not viable in this capture configuration without a
  firmware tier/emission change.
- With that ruled out, the remaining cause is host-side and measured: the wall-clock streamer
  paced with `time.sleep(seconds_per_clock)` **per step**, which adds the per-iteration MIDI send
  and loop overhead on top of the 20.83 ms interval - a nominal 16 s (8-bar) stream ran ~20 s and
  the device therefore recorded 10 bars. The streamer now paces to an **absolute deadline**
  (`start + n * seconds_per_clock`), so the total is `target_clocks * seconds_per_clock`
  regardless of per-step cost.

The device verification of that change ([`bar-anchor-09`](../../captures/bar-anchor-09/session_20260917_133556.log)) came back
`SPARSE_CAPTURE` with a capture-ring overflow, so it produced no length evidence; that is the
third time in this stretch that runs went sparse on a long-running device, and a re-flash has
restored healthy evidence twice. Re-verify the deadline pacing after a fresh flash, then run the
successor probe.

**2026-09-17 BAR probe visible at last: the device is already 3 bars in, and no BAR rows arrive
during the stream.** After making the parser prefix-tolerant and instrumenting every per-track
stats block, `bar-anchor-07` reports `record_device_bar_start = 3`, `record_device_bar_latest =
-1`, `relative_bars = 0`. Two facts follow: the device's bar counter is at **3** when the record
begins (so the host's note stream starts ~3 bars into the device's count, and a stream-relative
target of 8 lands 10-11 bars later), and the probe sees **no BAR rows at all** during the stream -
the capture snapshot the harness polls does not advance while it is streaming, so the device-bar
end condition never fires and the stream falls back to its duration. The next step is to find out
why the collector's snapshot is frozen during streaming (managed-capture follow path buffering),
or to read the BAR rows from the session log file directly; the assertion is already truthful, so
the run correctly fails `record_loop_length_mismatch` until then.

**2026-09-17 the record-length assertion was vacuous (fixed).** `_verify_record_loop_length`
accepted `actual == bar_aligned`, but the firmware's `final_length` *is* the bar-aligned
quantisation of `raw_length`, so that clause was true for any raw length and the requested bar
count was effectively unenforced - which is why an 8-bar request recording 10 bars passed
`bar-anchor-04` while earlier runs failed (it depended only on whether the raw length landed
inside the final bar). The check now compares against the request, keeping the intended one
bar of stop-time quantisation slop; the proxy-timed tolerance is unchanged. Verified on the
observed values (6,144 passes; 6,912 slop passes; 7,680 is a mismatch; proxy-timed 7,680 is
reported). With a truthful assertion, the remaining pacing fix is to anchor the device-BAR
baseline at the **record-arm** boundary: the device begins counting at the arm/transport,
about two bars before the host's first streamed note, so an 8-bar note stream still yields a
10-bar loop.

**2026-09-17 BAR-anchored stream stop landed; the record is still ~2 bars over.**
`stream_pattern_for_bars` now ends on the device's own `BAR,<tick>,<index>` (advanced by
`target_bars` from a lazily captured baseline) and both the record and overdub paths pass a
probe over the capture tail; `_latest_device_bar_index` is unit-tested (parser cases) and the
probe reports `device_bar_start/latest/relative_bars` in its timing dict. Device results:
8,448-9,216 ticks (11-12 bars) on the clock-count path before, **7,680 ticks (10 bars) after**
- better, but still 2 bars over the 6,144 (8-bar) target, and `record_loop_length_mismatch`
remains. Next: surface the probe counters in the per-track report to see whether the baseline
is captured a bar or two late (the stream starts mid-bar and the first probe call lands 0.5 s
in), then close the residual. Two earlier runs in this stretch were `SPARSE_CAPTURE` on a
degraded device; a re-flash restored healthy evidence, so the harness itself is not the cause
there.

**2026-09-17 successor reentry unguarded through all three routes; one ST line per transition.**
OpenSpec `successor-capture-reentry-unguarding`, the change the parent task 2.3 split out. Its
preflight (section 1) was already resolved; the predecessor work this round needed was 2.3's
remaining assertion breadth, which 5.2 owns - so 5.2 landed first, then 3.1-3.3 and 4.1.

- **5.2 isolation (closes 2.3):** new `test_continuous_capture` case
  `test_deferred_predecessor_completion_leaves_successor_interval_untouched` asserts that a
  predecessor's deferred completion during a successor's capture leaves the successor's rows,
  pending notes, preview, live-role reservations and track state untouched and forces no
  transition on it; `test_track_reentry_does_not_depend_on_manager_guard` gained the same
  completion-during-capture phase.
- **3.1:** `TrackManager::startOverdubbingTrack` no longer refuses a new interval because a
  deferred stop is pending. START is bounded by the live admission role, which
  `Track::startOverdubbing` proves with every storage/ownership precondition; the cross-track
  maintenance predicates still consult `hasPendingDeferredOverdubStop()` unchanged.
- **3.2 / 3.3:** `test_manager_reentry_with_a_held_same_pitch_note` passes (A's held same-pitch
  closure retained, B's physical Off closes B's note only), and the step gate ran green.
- **4.1:** `MidiButtonActions::handleToggleRecord` no longer swallows the press while a
  finalization is pending, so Button A admits the successor on the same terms as the other
  routes; the resolved reproducer
  `test_button_a_successor_stop_denied_until_deferred_role_available` passes, reading its
  finalized output from the job's sealed range (an admitted transfer empties the live store).
- **Firmware telemetry:** `Track::setState` emitted the `ST,Track` capture line twice (once via
  `logger.logStateTransition` -> `SC_STATE`, once directly). The duplicate is removed, so each
  transition appears once and the host's dedupe is now only a compatibility guard.

Evidence: full native **2196 cases, 1 skipped, 0 failures** - the Track, manager and Button A
reentry cases are green for the first time; `test_continuous_capture` 37/37;
`teensy41-capture-serial` RAM1 code 425560 / padding 424 / locals 6464. Device
([`successor-reentry-13`](../../captures/successor-reentry-13/session_20260917_134731.log)): the
successor interval **now opens on hardware** - `PLAYING->OVERDUBBING: 2`,
`OVERDUBBING->PLAYING: 2`, `intervals={'OVERDUBBING': 2, 'OVERDUB_STOPPING': 2}` and the record
still lands at 6,144 ticks. The probe's *overlap* verdict is not yet captured: its press landed
+1.7-1.9 s after the stop against a ~1.4 s predecessor window (the confirmation wait consumed
the window; the press-then-probe reorder landed but that run came back sparse).

Still open in that change: 4.2 (three-route agreement test), 5.1 and 5.3-5.6 (capacity,
transport-stop ordering, supersession, adversarial mid-grain timing, reentry telemetry), 6.2
(device acceptance) and 6.3 (closeout).

**2026-09-17 deferred-stop confirmation fixed, and the record over-run root-caused to
clock-count pacing.** Two more host-side oracle items, plus the real reason the record
length keeps missing.

- `_serial_has_overdubbing_stopped` accepted only the human `Overdubbing stopped` marker or
  the direct `OVERDUBBING -> PLAYING` edge - neither of which the deferred path emits - so
  **every** deferred stop ran out the full state-sync budget as `EVIDENCE_UNAVAILABLE`. That
  is the same defect family as the earlier counting/pairing fixes, and it is why the
  successor probe fired 8,638 ms after the stop it was meant to overlap. It now also accepts
  `OVERDUBBING -> OVERDUB_STOPPING` and `OVERDUB_STOPPING -> PLAYING`, so a deferred stop
  confirms promptly.
- The probe additionally bounds its own stop-confirmation wait to 1.2 s (a timeout there is
  already non-fatal), so its presses can land inside the predecessor's window.
- **Root cause of the 8-bar -> 10-34 bar record over-run, now identified:** in device-clock
  mode `resolve_wall_tempo_for_proxy` returns `None` (by design - it only resolves a tempo for
  the serial proxy), so `stream_pattern_for_bars` takes its `in_port` clock-message path and
  paces by **counting clock messages** against `target_bars * MIDI_CLOCKS_PER_BAR = 8 * 96 =
  768`. The device's bar cadence is exactly **2.00 s/bar at 384 ticks/s** (measured from its
  own `BAR,<tick>,<index>` rows), so 768 host clocks should be 16 s; the observed streams ran
  23-68 s (11-34 device bars), i.e. far fewer than 96 clock messages per device bar reach the
  host. An attempt to force the clock path explicitly (`wall_clock_tempo_bpm=None`) made it
  worse - 20 bars, because the stream then ran to its `max_seconds_guard` - and is reverted
  with that evidence recorded at the choice site.
  The fix is to stop the stream on the **device's own `BAR` index** (or measure clocks-per-bar
  empirically at the start and scale the target), not to count host clock messages or a host
  seconds-per-bar estimate. Until then the strict length check keeps flagging these runs, and
  it is right to.
- Consequence for the successor probe: the two most recent probe runs were inconclusive
  (`SPARSE_CAPTURE` - the over-long stream floods the relevant evidence), so the
  successor-reentry device case is **blocked behind the `BAR`-anchored stop**, not by the
  probe's own design.

**2026-09-17 successor-reentry probe: instrumented and unit-tested, device evidence still
open (two device attempts, both diagnosed).** The probe and its oracle are implemented
(`--successor-reentry-delay-ms` / `--successor-reentry-stop-delay-ms`, default off;
`_verify_successor_reentry` reads the deduped transition stream plus
`ODUB,stop,bridge_ready` and requires the successor's `PLAYING -> OVERDUBBING` edge before
the predecessor's completion, with the quick stop producing no stop edge before it). Four
host tests cover the branches; `test_capture_transitions` passes 30/30.

Device attempts:
- [`successor-reentry-07`](../../captures/successor-reentry-07/session_20260917_130811.log):
  the probe fired **8,638 ms** after the predecessor's stop (window was 1,816 ms), because it
  ran after `request_state_transition` returned - the deferred stop reads as
  `EVIDENCE_UNAVAILABLE` for the whole state-sync window. Fixed by timing the presses from
  the stop *press* on a timer.
- [`successor-reentry-08`](../../captures/successor-reentry-08/session_20260917_131145.log):
  the timer version **raced the harness's own blocking confirmation** - no
  `OVERDUBBING -> OVERDUB_STOPPING` edge was emitted at all, the successor-reentry block
  reported `None` throughout, and the run ended ARMED (inconclusive). So the probe needs its
  own observation path (or a bounded `--state-sync-timeout-ms` for probe runs) instead of
  racing `request_state_transition`. That is the next step for this case.

**Also reproduced while probing: the record stream is paced longer than requested.** The
device recorded 11 bars (8,448 ticks) and 12 bars (9,216 ticks) for an 8-bar (6,144-tick)
request; the stream ran 23.0 s against a 16 s request at the device's own 2.0 s/bar cadence
(`BAR` markers every 2.0 s, `EMPTY -> RECORDING` 254.13 s -> `RECORDING -> STOPPED_RECORDING`
277.10 s). The device is quantizing correctly for what it received - the host stream and stop
moment are the problem. This is the remaining half of the wall-clock item: when the device's
own `BAR`/`BPM` markers are available, the stream duration and the stop moment should follow
them rather than a host-side seconds-per-bar estimate. Until that is fixed, the strict
record-length check will keep flagging these runs, and it is right to.

**2026-09-17 bridge tail profiled: it is cadence-bound, not work-bound.** Goal step 3, from
[`c3-derived-reserve-01`](../../captures/c3-derived-reserve-01/session_20260917_124359.log) -
the deferred Playing stop, `ODUB,stop,bridge_base` 54.929612 s to `ODUB,stop,bridge_ready`
55.997826 s = **1,068 ms**, profiled from the firmware's own `DIAG,stop_grain` /
`DIAG,stop_phase` telemetry (`stopGrainTiming` reports `startUs`/`endUs` per grain, so the
grain duration is `endUs - startUs`):

| phase (`PendingStopPhase`) | grains | grain work | max grain | phase span | crossed |
|---|---|---|---|---|---|
| 5 `CanonicalTail` | 11 | 1.1 ms | 115 us | 18 ms | 5 |
| 7 `CompanionHistory` | 4 | 0.5 ms | 204 us | 8 ms | 0 |
| 8 `UndoCollection` | 3 | 0.2 ms | 82 us | 2 ms | 0 |
| **9 `CompanionBridge`** | **466** | **65.4 ms** | 314 us | **1,057 ms** | **29** |
| 10 `AtomicTransfer` | 1 | 0.2 ms | 201 us | - | 0 |
| 11 `DeferredCleanup` | 5 | 0.6 ms | 394 us | 6 ms | 1 |
| total | 490 | **68.0 ms** | | 1,108 ms | 35 |

So **94% of the tail is idle between grains**: the bridge phase issues 466 grains of ~140 us
each at a **1.2-3.2 ms cadence** (roughly one grain per main-loop turn), i.e. ~300-800
grains/s against a ~7,000 grains/s work capacity. `CompanionBridge` owns 466 of the 490
grains and 65.4 ms of the 68.0 ms of work; the firmware's own deadline counter marks 29 of
them as crossing slack (~6%). `CompanionHistory` is now **4 grains** - C3's work shaping is
directly visible, since the same stop previously paid ~2,030 `Phase::Trim` units there.

Conclusion for the DEC question: the remaining stop-to-final-output tail is **not** the
bridge map/compile algorithm, so an optimization DEC would be about the deferred-stop
**owner's turn cadence / per-turn grain budget**, and it must be argued against the
MIDI-boundary budget (grains run after MIDI service, must not stack, and `overflow_free` is
a standing gate). That is a scheduling decision with a deadline analysis, not a knob tweak;
no DEC is proposed yet - this profile is the input to one.

**2026-09-17 HITL oracle repaired: the deferred stop is now first-class in the assertions.**
Host-side only; no firmware change. The firmware was ahead of its oracle: every deferred
overdub stop looked like a firmware regression.

Four oracle defects, all in the host, all now fixed:

1. `_effective_transition_actual` required the direct `OVERDUBBING -> PLAYING` edge, so the
   legitimate deferred shape counted as zero stops. It now sums the direct edge with the
   deferred completion hop (`OVERDUB_STOPPING -> PLAYING`); each completed stop produces
   exactly one of the two shapes.
2. `_pair_overdub_sessions` collected only `OVERDUBBING -> PLAYING/STOPPED` as an overdub
   stop, so a deferred stop left the phase session open - and `missing_phase_boundaries`,
   `overdub_first_note_missing` and a zero overdub note-on count all followed from that one
   gap. It now also accepts `OVERDUBBING -> OVERDUB_STOPPING` (the interval's last admissible
   input, and the earliest matching stop) and `OVERDUB_STOPPING -> PLAYING/STOPPED`.
3. `_extract_overdub_memory_note_count` matched the direct-edge marker string only.
4. The firmware reports one track state change from **two observers** (the Track state
   facade and the logger hook), so every `ST,Track` edge appears twice within microseconds.
   Counting both doubled every transition row (`PLAYING->OVERDUBBING: 4` for two real
   entries) and, worse, let the phase pairing bind a successor interval's start to the
   predecessor's stop - which manufactured a bogus second-overdub window and a false
   `second_overdub_out_of_range_notes` firmware contradiction. `hitl.serial.protocol` now
   exposes `track_transitions()`, collapsing an identical `(from, to)` edge that repeats
   inside 5 ms, and both the counters and the phase pairing read from it.

Wall-clock proxy: the proxy cannot predict the device's own bar grid, so a requested bar
count is not an authoritative length there. The runner records `transport_timing_source`;
when it is `wall_clock_proxy`, a longer bar-aligned length is reported
(`record_loop_length_proxy_timed` timing warning) instead of raised as
`record_loop_length_mismatch`. Without the proxy the strict check is unchanged, and a
non-bar-aligned length is a mismatch in both modes.

Evidence:
- `scripts/test_capture_transitions.py` gains five regression tests (deferred hop counting,
  deferred phase pairing, immediate pairing unchanged, proxy-timed length reported,
  non-bar-aligned still rejected): **26/26 pass**; `test_hitl_serial_timing` 30/30 and
  `test_hitl_transport_reset` 25/25 stay green.
- Device [`oracle-fixed-01`](../../captures/oracle-fixed-01/session_20260917_125305.log):
  **Result: PASS**. `OVERDUBBING->PLAYING: 1 (ST 0, min 1)` - the deferred hop counted; the
  overdub phase now counts **16/16** notes (was 0/0); the 9,216-tick proxy-paced length is a
  report, not a mismatch; the deferred stop, SEVT content, cleanup and both display checks
  pass. The only remaining note is the advisory `overdub_first_note_too_late` warning
  (19 clocks against 16 with the BAR bump) - a host stream/ST skew, not a failure.
- Replayed on `successor-reentry-01`: `PLAYING->OVERDUBBING` 4 -> 2, effective
  `OVERDUBBING->PLAYING` 2, and the second overdub pairs to its own window.

**Successor-reentry device case: still open, and now measured precisely.** With the repaired
oracle and a fresh device, the canonical second-overdub run **passes**
([`successor-reentry-06`](../../captures/successor-reentry-06/session_20260917_130431.log)):
`PLAYING->OVERDUBBING: 2 (min 2)`, `OVERDUBBING->PLAYING: 2 (ST 0, min 2)` (both stops
counted through the deferred hop), second overdub 8/8 notes, record 128/128, both SEVT spans
`count_ok/span_ok/bars_ok`, both display checks green. But it is **sequential, not
overlapping**: the measured timeline is OD1 start 34,072.9 ms -> OD1 stop 41,064.8 ms
(deferred, `bridge_base` 41,178.6, `bridge_ready` 42,269.1) -> **OD2 start 49,713.4 ms**, an
**8,648 ms gap** against a ~1.2 s deferred window. Removing the post-stop undo/redo did not
change that gap, so the second-overdub start is bar/grid-scheduled rather than
stop-relative - the canonical knob cannot express an overlapping successor.

What the case needs, concretely: a millisecond-scheduled successor press - after the first
overdub stop press, press the same button again after a bounded delay (~200-400 ms) without
waiting for a bar boundary, then optionally press stop again inside the successor's own
pending window to reach the denied-stop branch - plus a verification that a
`PLAYING -> OVERDUBBING` edge lands **before** the predecessor's `bridge_ready`/`prefix_reuse`
completion (or that the phases overlap), and a `!STOP_STATUS queue=depth/capacity` read.
That is a small, self-contained addition to the legacy runner (a new delay flag and one extra
button action) plus a verifier predicate; it is the next harness increment. Native already
covers both branches
(`test_button_a_successor_stop_denied_until_deferred_role_available`,
`test_successor_stop_denied_by_occupied_deferred_capacity_is_non_destructive`).

**Device-state hazard found while doing this (not caused by the oracle work): a deferred save
can wedge, and it then blocks every subsequent HITL run.** After an aborted run the clear
ritual reports
`[StorageManager] ERROR: Persistence drain stuck after 256 iterations (max 256, expected ~2362 slice steps, ~158848 SD bytes)`
and the firmware aborts the clear (`Clear waiting for deferred save completion` ->
`Clear aborted: could not complete deferred save first`), so no run can clear its slot.
Increasing the harness boot settle to 45 s did not help. The device also does **not** reset on
serial open in this setup - uptime climbed to 1,195 s across the aborted runs, and a 1200-baud
touch did not reset it. **Recovery: re-flash (`pio run -e teensy41-capture-serial -t upload`,
which hard-resets); after that the device booted at 6.8 s uptime and the next run passed.**
Two follow-ups worth a decision: make the harness force or verify a fresh boot before a run
that matters, and look at why the blocking drain makes *zero* progress in that state
(158 KB pending, `chunks_free=362`, `snap=3389KB`) rather than merely being slow.

**2026-09-17 C3 increment 2 LANDED and device-measured: the companion-row reserve is derived
from the loop, and the deferred stop is twice as fast to its prepared window.** The reserve
half of DEC-066, plus the measurement increment 1 could not get (that run took the
transport-stop path; this one took the deferred Playing path).

What changed:
- `Loop::companionRowHorizonBound()` derives the row bound from the loop's canonical payload
  event count plus the open-note reservation capacity as slack, clamped to
  `kRecordsPerBridge`. Rationale, from the per-owner measurement: a companion row exists only
  for a Delete/Length transform, `upsertSourceTransform` keeps one such transform per
  **source** note identity, and every source note contributes at least one payload event - so
  payload count is a sound, generous bound that still reaches the ceiling for a dense loop.
- `RuntimeCaptureCompanions::begin` now admits against the **eligible rows** the frozen change
  set will produce, not the whole change vector: the vector also carries the interval's `Add`
  entries, which become the capture pass and never become rows. Without this, a derived
  reserve could deny a stop whose captured engine is dense but whose transform set is small.
- `RuntimeCaptureCompanions::ensureCapacity` grows the row reserve at a cold point only, and
  never shrinks it, so a later denser interval still fits a smaller earlier grant.
  `Track::prewarmPlaybackForSlot` grants the derived bound to both companion owners;
  `Track::startOverdubbing` re-grants it after a commit could have grown the canonical
  content. Both grants are tag-bracketed; a failed grant reports
  `#MEM_DENIED op=overdub_start,reason=prewarm_failed`.

Measured against the step-E baseline on the same loaded-set 8-bar recipe
([`c3-derived-reserve-01`](../../captures/c3-derived-reserve-01/session_20260917_124359.log)):

| metric | step-E baseline | C3 increment 1+2 |
|---|---|---|
| arm delta (1 s accounting line) | 1,469 KB | **1,315 KB (-154 KB, -10.5%)** |
| `tag 2` (views/bridges + frozen companions) | 790 KB | **715 KB (-75 KB)** |
| companion-row grants (tag 6 / row reserve) | 80 KB | derived; ~0-5 KB on this loop |
| stop press -> `ODUB,stop,bridge_base` | 220 ms | **112 ms (-49%)** |
| stop press -> `ODUB,stop,bridge_ready` | 1,127 ms | 1,180 ms (unchanged; the bridge map/compile dominates, exactly as the per-owner analysis predicted) |
| allocations inside the stop window | 0 blocks | **0 blocks** (dLive/dBlocks = 0 through `bridge_ready`) |
| SEVT / canonical | ok | **ok** (overdub 16/15, count/span/bars ok; record 128/128) |
| `FATAL` / `#MEM_DENIED` / `#MEM_EXT_REFUSED` | 0/0/0 | **0/0/0** |

The companion path completed (`bridge_ready ... context_retained`), so the derived grant was
sufficient on this content - it did not silently fall back. Minimum internal heap over the
run was 60 KB and 140 KB in the stop window, against the 48-65 KB of the step-E runs.

Fixtures for the three classes (native, `test_playback_prewarm`):
`test_capture_companions_reserve_bounds_eligible_rows` (a 4-row reserve admits a change vector
of 8 Adds + 1 transform and produces exactly one row; five transforms over four rows is a real
denial), `test_companion_row_horizon_scales_with_content_up_to_the_ceiling` (300 payload
events -> bound 428; payloads at the ceiling -> bound 2048, so the full envelope is retained),
and `test_companion_history_prep_units_scale_with_demand_not_reserve` (one produced row over a
512-row grant reaches Ready inside 32 quantum units). `pio test -e native`: **2196 cases,
2 pre-existing failures, 1 skipped, 2192 succeeded**.

Remaining, recorded rather than fixed: the event-horizon owners
(`canonicalPassInputs`, `companionPasses`, `apply`, `reconstruction`, views/bridges) are
ceiling-bound because their interval term *is* the staging admission bound and their reserve is
taken at the arm; shrinking them would either cap the interval (rejected) or move the reserve to
the seal and make it fallible (a stop-contract change). The maximum-envelope case is
fixture-covered on a dense loop, not device-covered: this device's loaded content derives a
small bound, so the ceiling path was exercised in the fixture rather than on hardware.

**2026-09-17 C3 increment 1 LANDED: per-episode work shaping in the prepared companion
history (DEC-066).** The measured per-owner capacity analysis is in
[DECISION_LOG DEC-066](../DECISION_LOG.md) - six distinct determinants, so one shared
"interval horizon" constant is provably wrong, and the event-horizon owners cannot shrink
below their ceiling soundly because their interval term *is* the staging admission bound
and their reserve is taken at the arm. What landed is the work half, which needed no
capacity change:

- `Loop::beginPreparedCompanionHistory` now sizes the working row vector to this interval's
  actual demand (`work.rows.resize(work.targetCount)`), inside the same grant, so
  `Phase::Trim` has nothing to pop. Previously each stop paid
  `grantedCompanionCapacity - actualCompanionRows` quantum units of pure trimming - about
  **2,030 units**, i.e. ~64 prep turns at `kDeferredStopPreparationQuantum = 32`.
- The row-fit check is now **capacity**-based (`work.rows.capacity() < historyCount +
  rows.size()`) instead of size-based, so a later, larger interval is still admitted against
  the same reserve after a small interval shrank the size.
- `Phase::RecycleGrow` restores the usable horizon with one batched resize instead of one
  row per quantum unit.

Evidence. New fixture
`test_companion_history_prep_units_scale_with_demand_not_reserve` (`test_playback_prewarm`)
prepares one companion row over a **512-row grant** and requires `Ready` inside 32 quantum
units, then asserts the grant is untouched for a later interval; the old walk needed ~511
units, so the bound is a real regression guard. Full native suite:
**2194 cases, 2 failed (pre-existing), 1 skipped, 2190 succeeded** - the case count moved by
exactly the one fixture added. Device
([`c3-trim-shaping-01`](../../captures/c3-trim-shaping-01/session_20260917_123329.log),
rebuilt and flashed): **0 `FATAL`, 0 `#MEM_DENIED`, 0 `#MEM_EXT_REFUSED`**, overdub content
verified (16/16 on/off, SEVT `count_ok/span_ok/bars_ok`), and both display checks pass
(`overdub_ok=True`, `transport_ok=True`).

**Honest gap in this increment:** that device run took the *transport-stop* path
(`OVERDUBBING -> OVERDUB_STOPPING -> STOPPED`), so it did **not** exercise the deferred
overdub-to-PLAYING preparation and therefore gives no before/after on the
stop -> `bridge_base` (220 ms) / `bridge_ready` (1127 ms) baseline. The latency win is
proven at the unit level (fixture) and still needs a run that takes the deferred Playing
path; that measurement, plus the reserve-shaping half of DEC-066, is the remaining C3 work.

**2026-09-17 C3 baseline pinned (before any capacity shaping).** Three facts the
demand-shaped-capacity decision and its device comparison must build on, all verified from
the step-E end-to-end log:

1. **The deferred-stop latency baseline**, from
   [`stepE-e2e-01`](../../captures/stepE-e2e-01/session_20260917_121721.log): stop press
   50.666 s -> `OVERDUBBING`/`OVERDUB_STOPPING`/`PLAYING` state transition at 50.667 s
   (+19 us, so audibility is not gated on the deferred work) -> `ODUB,stop,bridge_base`
   50.887 s (**+220 ms**) -> `bridge_begin` 50.894 s -> `bridge_ready ... context_retained`
   51.795 s (**+1127 ms**). The comparison must separate the companion preparation (before
   `bridge_base`) from the bridge map plus compile (the ~900 ms after `bridge_begin`).
2. **The real horizon cost is `Phase::Trim`, not a slice emitter.**
   `Loop::prewarmPreparedCompanionHistory` resizes `work.rows` to
   `passes.editPasses.size() + companionCapacity` (`src/Loop/LoopPreparedCompanionHistory.cpp`),
   and `Loop::stepPreparedCompanionHistory`'s `Phase::Trim` pops **one unused row per
   quantum unit** until `rows.size() == targetCount`. `kDeferredStopPreparationQuantum` is
   32 (`src/Track/TrackOverdubLifecycle.cpp:34`), so a 2048-row horizon with ~16 frozen
   companion rows pays ~2030 units, i.e. **~64 prep turns of pure trimming** per stop. That
   is derivable and removable without any capacity change: `beginPreparedCompanionHistory`
   already knows `targetCount`, so the working vector can be sized to it (allocation-free,
   capacity unchanged) and `Trim` becomes a no-op safety net.
3. **The ~65 `DIAG,prepare,slice,prep` lines are not this owner.**
   `LoopContentResolution`'s `Phase::RebuildPrepare` emits them, with `resume` =
   `prepMerged.size()` and a bounded per-slice event budget
   (`src/LoopContentResolution.cpp:2258-2331`); that cost is content-driven and belongs to
   the canonical/visual rebuild after the stop, not to the horizon.

Open question the per-owner measurement must still answer before the DEC: which owners'
worst-case demand is `canonicalContent + intervalBound` (and therefore saturates at 2048
unless the interval admission bound itself is reduced - a policy ceiling the user has
explicitly ruled out) versus which are genuinely canonical-only and can therefore shrink
soundly. The event-horizon workers look like the former; the playback view/bridge group
(~710 KB of the ~1.47 MB arm) is the case that decides whether C3 can deliver a material
arm-time memory win or only the latency win.

**2026-09-17 step E IMPLEMENTED: capture staging is now admission-provisioned, and the
loaded-set 8-bar recipe completes with no denial, no refusal and no fault.** DEC-065.
**Status: validated on device (user-accepted 2026-09-17)** - the timing invariant was
preserved while steady-state memory moved from boot lifetime to admission lifetime
(+1.19 MB boot headroom, both roles reserved before capture mutation, zero
allocations inside the stop interval, the formerly failing record->overdub path
completing with 0 fatal/denial/refusal, native coverage unchanged).
Separate follow-up, deliberately not part of this change: the harness's
`OVERDUBBING->PLAYING` edge expectation and its clock/`overdub_first_note`
mismatches need repairing against the deferred state machine.
Code: boot (`Track::prewarmPlaybackForSlot(s, false)`) no longer reserves either staging
role; the record arm keeps reserving both through `prewarmViews`, and `Track::startOverdubbing`
now reserves both on its own cold turn (tag-bracketed) **before** any mutation, replacing the
"staging must already exist" rejection with the same reserve-then-require order the arm uses.
On failure it reports `#MEM_DENIED op=overdub_start,reason=prewarm_failed`.

Measured on the loaded device
([`stepE-staging-admission-01`](../../captures/stepE-staging-admission-01/session_20260917_121455.log),
[`stepE-e2e-01`](../../captures/stepE-e2e-01/session_20260917_121721.log), both rebuilt and
flashed from this tree):

- **Boot headroom: PSRAM free after prewarm 3,772,224 -> 4,989,312 B (+1,189 KB)**, and
  `tag 3` reads **0 KB at boot** (was 1334 KB). The ninth slot's phase went from 156 KB to
  7.9 KB (horizon plus canonical copy directory only).
- **Arm delta is now 1,469 KB** (was 1,240 KB before staging moved in; the difference is the
  two stagings plus the runtime struct, `tag 3 = 148 KB`), and the tag vector at the arm is
  `80/790/148/0/7/80` - stop jobs, views/bridges, staging, horizon, and the history reserve
  that previously failed, all resident. Pool headroom at the arm is ~1.19 MB.
- **The arm no longer needs the heap.** The remaining blocker after the boot change was the
  point-of-use gate, not capacity: the companion-history reserve was denied `reason=heap_low,
  need=81920B, heap_free=76KB` with `pool_refused=0/0B` and ~1.27 MB of pool free. The 80 KB
  vector is `InternalHeapFirstAllocator` (malloc, then extmem), so the fallback would have
  served it - the heap-floor proxy was refusing an available operation. `admitOperationMemory`
  now treats the probe as the **authority** for `CompanionHistoryReserve` (the probe tests
  exactly the malloc-then-extmem pair the allocator will use), so the heap floor no longer
  vetoes a request the pool can serve. Denial is still explicit when the probe fails or the
  pool refused within its window.
- **End-to-end on the same recipe: 0 `FATAL`, 0 `#MEM_DENIED`, 0 `#MEM_EXT_REFUSED`.**
  Record: 128 note-ons captured, loop stopped and published. Overdub: opened, 16 events
  captured, `SEVT` verification drained (count/bars ok), and the deferred stop completed
  `OVERDUBBING -> OVERDUB_STOPPING -> PLAYING` at 50.667 s with no allocation failure
  (`#CAP,50667213,ST,Track,OVERDUB_STOPPING,PLAYING`, `ODUB,stop,bridge_ready,...,context_retained`).
  Heap floor over the run 48 KB, pool live peak 7,489 KB of 8,182 KB.
- **Host-side gates, not firmware failures:** the harness reports
  `ST OVERDUBBING->PLAYING: 0` because the deferred design routes through
  `OVERDUB_STOPPING`, and it flags `overdub_first_note_missing` plus a 7,680-tick (10-bar)
  loop against the requested 8 bars - the run's own config line says it fell back to a
  wall-clock tempo proxy because the Teensy clock was not on USB MIDI in. These are the
  known host/gate-defect class and are not part of this change.

Residuals, and one attribution this entry originally got wrong. **CORRECTED 2026-09-17:**
the ~65 `DIAG,prepare,slice,prep` lines in the e2e log are **not** the companion-history
horizon. They are `LoopContentResolution`'s `Phase::RebuildPrepare` canonical
materialize/merge (`src/LoopContentResolution.cpp:2258-2331`), where `resume` is
`prepMerged.size()` and each slice spends `kDeviceGateEventsPerSlice` events - so that
cost is **content-driven** (the 7,680-tick loop's passes), not horizon-driven, and it
starts after the stop while the canonical/visual rebuild runs.

The companion-history prep does still walk unused tail rows, but by a different mechanism
that is derivable rather than log-labelled: prewarm resizes `work.rows` to
`historyCount + companionCapacity` (`src/Loop/LoopPreparedCompanionHistory.cpp:74`) and
`Phase::Trim` pops **one row per quantum unit** until `rows.size() == targetCount`
(`:195-198`), where `targetCount = historyCount + rows.size()` of the actual frozen
companion rows (`:123`). So each stop pays `companionCapacity - actualCompanionRows` units -
around 2,030 for a 2048 horizon with an 8-note overdub - inside the deferred preparation.
Its share of the measured 1.13 s stop-to-bridge-ready (50.666 s stop -> 51.795 s
`bridge_ready`) is **not yet measured**, so C3's latency argument is currently analytical,
not measured; the device comparison must separate the prep from the bridge map/compile
before claiming it. What is already certain is the memory side.

The seven companion/lookup workers and the history rows remain owned by the record arm
(DEC-065 scope boundary), so a loaded-slot overdub still uses the degraded
canonical-prefix/companion path until a record arm upgrades the slot.

**2026-09-17 step E: the 1334 KB is slot-provisioned boot staging, and it is the headroom
the arm needs.** `TrackManager::prewarmPlaybackRuntime` (`src/TrackManager.cpp:59-80`) calls
`prewarmPlaybackForSlot(s, false)` at boot for every slot that is enabled or has committed
passes, and that call reserves **both** capture-admission stagings plus the slot runtime
itself - capture-only, no views, no workers. The step-D run shows the cost directly in its
own boot lines: phase 1 prewarms 8 slots and PSRAM free falls `5,140,416 -> 3,772,224`
(**1,336 KB**), phase 2 adds one more slot and falls `3,766,404 -> 3,606,348` (**156 KB**),
i.e. **~157 KB per slot** (2 x 61,952 staging + the runtime struct) plus ~10 KB per *track*
for the stop-jobs ring (`tag 1 = 80 KB` over 8 tracks). So `tag 3 = 1334 KB` is a
per-slot, boot-resident reservation for slots that may never record, not an admission
requirement: staging is only needed by a slot that records.

The step-E decision is therefore **slot-provisioned -> admission-provisioned**: move the
staging reserve from boot to the arm (the same cold allocation window the arm already uses
for the 1.24 MB view/worker set), keeping the frozen role reserved at the same moment so
punch-out still never allocates. That returns ~1.33 MB at boot - more than the 1.24 MB arm
increment that currently fails - and it makes the arm's true requirement explicit instead
of paying it for every content slot at boot. The counter-obligation to re-check is the
documented capture-only boot ordering, which uses staging readiness
(`ExecutionBounds.md`, "staging-only boot"): that contract must be restated in terms of
"staging is reserved when the slot is admitted for capture" rather than "at boot". This is
a design conclusion for the user's decision, not an implemented change; it needs a DEC with
the C3 capacity decision because both change a gated execution bound.

**2026-09-17 step D: the loaded-set 8-bar recipe now denies instead of hanging, measured.**
Run [`stepD-arm-delta-01`](../../captures/stepD-arm-delta-01/session_20260917_115207.log)
(rebuilt and flashed from this tree; `--record-bars 8`, one overdub, track 4 slot 1, the
device's own loaded workspace). Before this RC the same recipe ended in
`[ExternalMemoryFirstAllocator] FATAL` and a silent hang; now:

```
#MEM_ADMIT,5411558,op=boot_probe,need=16384B,heap_free=184KB,pool_refused=0/0B   <- positive control
#MEM_EXT_REFUSED,14399844,bytes=81920,count=1,live=8095KB,blocks=637,heap_free=76KB,
  tag=6,tag_live=0KB,tags=80/790/1334/0/7/0/0/0/0/10/2/64/66/68/96/144/0
#MEM_DENIED,14399858,op=companion_history_reserve,reason=reserve_failed,need=81920B
#MEM_DENIED,14399880,op=record_start,reason=prewarm_failed,need=0B
```

- **Zero `FATAL`, zero `Recording started`, device alive**, and playback continued
  (`STOPPED_RECORDING->PLAYING: 4`); the harness then aborts its own run because RECORDING
  is not confirmed, which is the intended consequence of a denied arm.
- **The arm delta is measured at 1,240 KB** on the 1 s accounting line (`dLive=+1240KB,
  dBlocks=+104, dHeapFree=-76KB`) - exactly the audit's prediction of tag 2's 790 KB plus
  the seven separately-tagged prewarms' 450 KB, and the whole set is retained because only
  the *last* reserve failed. The refusal line's full tag vector confirms every other
  contributor was already resident (`tag 6 = 0` at the first request).
- **Full arm requirement on a fresh slot = 1,320-1,400 KB**: 1,240 KB of views/workers plus
  the two `2048 x 40` history vectors, which is what the 80 KB refusal was.
- Headroom at denial: pool `8095/8182 KB`, heap **76 KB** on the first attempt and 64 KB on
  the second, `heap_min` 64 KB over the run - far above the 32 KB floor, so this denial is
  "the request exceeds what remains", not a floor heuristic. The two attempts 8.3 s apart
  produce identical refusals, which is the idempotence the slot prewarm is supposed to have.
- Honest residual: a denied arm **keeps** the 1,240 KB it did establish (it is the same
  idempotent slot prewarm), so on a device in this state every further arm attempt fails the
  same way and no slot can start a record until memory is freed or the boot reservation is
  reduced - which is exactly why step E matters for the fix, not just for footprint.

**2026-09-17 step C design: the stop working set cannot be shared - the levers are
capacity shaping and a single deferred-stop working set.** Design only; implementation
needs a DEC because the interval horizon is a gated execution bound
(`ARCHITECTURE_RULES.md` row referencing DEC-043). Grounding numbers are the measured
device sizes in
[ExecutionBounds](../Authority/Architecture/ExecutionBounds.md#record-arm-reservation-buffers-bytes-and-lifetimes-2026-09-17).

**What the design must preserve.** Every reserve is taken on a cold user-action turn (the
arm) so that the MIDI-tick deferred-stop grains and the atomic transfer allocate nothing
and no vector in them can grow; the second aborting allocator (`InternalHeapFirstAllocator`)
must never be reachable from a grain. Any proposal that reserves at punch-out makes the
*stop* fallible, which is worse than denying the arm, so it is out.

**C1 - one arena with typed views across the seven workers: rejected.** The seven are live
simultaneously (`apply::begin` binds five children at once; `Reconstruct` borrows
`spans`+`pair`; the overlap walk reads reconstruction's live `notes_`; the merge reads apply
while writing `companionPasses` and `canonicalPassInputs.normalizedScratch`), and the same
workers are driven by live canonical-prefix service while playing/overdubbing, so they are
not stop-only either. Aliasing would not be phase-exclusive by construction.

**C2 - alias `frozenCompanionBridgeMap.records_` (68 KB) onto the apply group: possible but
marginal.** It is the one pair whose live windows do not obviously overlap (the map is
built after the merge completes), but its `prepared_`/`baseOrigins_`/`base_` borrows must
survive through bridge compile and it can be re-entered via `requiresBridgeRefresh`. Saving
is at most 68 KB of 1.24 MB (~5%) for a proof that adds a new lifetime invariant, so it is
not worth taking alone - only as part of C3.

**C3 - shape the capacity from the admitted interval horizon instead of the global
maximum.** Every 2048 comes from three admission constants: `kEventsPerView`,
`kRecordsPerBridge` and `kCaptureStagingEvents` (all 2048). They are *limits*, not content:
the arrays are sized so that the worst case a user can add in one interval never has to
grow. The arm therefore reserves ~1.24 MB even for a loop holding a few hundred events.
This is the only lever that moves the number materially. Two sub-shapes:
- **(a) derive from the slot's committed shape.** `min(horizon, committedEvents + intervalHorizon)`
  and `min(2048, committedRows + 1)`. On its own this saves nothing for a long take,
  because `committedEvents + intervalHorizon` saturates at the horizon - which is exactly
  the measurement D should capture per slot.
- **(b) reduce the interval horizon itself.** Halving the three constants to 1024 nearly
  halves the whole reservation (staging 116 -> 58 KB/slot, view set 727 -> ~370 KB, the
  seven workers 450 -> ~225 KB, history pair 160 -> 80 KB). The cost is a product limit:
  a single interval could then admit at most 1024 captured events and the history at most
  1024 rows. For scale, a dense 8-bar 4/4 pass is a few hundred events, so 1024 is still
  ~2x a dense 8-bar take, and 2048 is ~4x. **This is the decision to take.**

**C4 - one deferred-stop working set per device instead of per slot.** The stop-only
buffers are per slot runtime today: `frozenCaptureCompanions` 80 KB + `bridgeMap` 68 KB +
the second 80 KB `EditPass` history vector + the frozen staging role 58 KB. Deferred-stop
admission is per *track* (`beginDeferredOverdubStop` is gated by that runtime's
`captureStopOwner`, `TrackOverdubLifecycle.cpp:209-241`), so two tracks can be stopping at
once; a device-wide singleton would restrict that to one concurrent deferred stop (and the
frozen staging role would need a device-wide owner). Benefit: ~228 KB per armed non-stopping
slot and ~58 KB per slot at boot, i.e. it attacks step E's 1334 KB too. Cost: a real
behaviour restriction and a new global owner, which needs its own DEC.

**Recommendation.** Take **C3(b) first** (a capacity decision that is user-facing and
reversible via the constants), with C3(a) as the derivation mechanism so a small loop
reserves only what it can use; keep C2 as a follow-up only if the arena is being built
anyway; treat C4 as the step-E answer rather than part of this stop-storage change. The
727 KB view set is playback infrastructure, not stop scratch, so it is deliberately not
redesigned here - it is E's question (slot-provisioned vs admission-provisioned).

**What D must measure to settle the choice:** the arm's new delta and post-arm headroom on
the loaded-set recipe, plus the per-slot `committedEvents`/`committedRows` at the moment of
the arm, so C3(a)'s saturation claim ("a long take already needs the full horizon") is
measured rather than assumed.

**2026-09-17 step B audit: the arm reservation is byte-exactly accounted, and the seven
workers are one concurrent working set - not phase-exclusive scratch.** Full table with
write/read windows and lifetime classes:
[ExecutionBounds](../Authority/Architecture/ExecutionBounds.md#record-arm-reservation-buffers-bytes-and-lifetimes-2026-09-17).
What the audit established, and the corrections it forced:

- **Device element sizes were measured, not assumed** (device toolchain, env flags):
  `MidiEvent` **20**, `EditPass` 40, `Pass`/`SealedPass` 8, `Origin` 4, `DisplayNote` 16,
  `Action` 16, `PlaybackBridgeRecord` 28, `MidiEventVec` 12. Every buffer then reconciles
  exactly with the measured tags: `canonicalPassInputs` 147,456 B = 144 KB (its eight
  arrays, not the three the architecture doc still described), `companionPasses` 98,304,
  `bridgeMap` 69,632, `apply` 67,584, `reconstruction` 65,536, `pairLookup` 10,240,
  `spans` 2,048 - the 450 KB group; `frozenCaptureCompanions` 81,920 and views+bases
  358,400 + bridges 368,640 = 727,040, which with the companions is exactly tag 2's 790 KB.
- **The seven workers are live simultaneously and are NOT stop-only.** `apply::begin` binds
  `candidate_`, `pair_`, `deletion_`, `spans_`, `reconstruction_`, `overlaps_` at once;
  `Reconstruct` borrows spans+pair; the overlap walk reads reconstruction's live `notes_`;
  and the merge reads apply data while writing `companionPasses`' alternate buffers and
  `canonicalPassInputs.normalizedScratch`. Worse for a "stop working storage" framing:
  `Track::processDeferredCanonicalPrefix` runs the *same* workers while a track is merely
  playing or overdubbing (`TrackDeferredMaintenance.cpp:855-877`), so their capacity is
  required **during RECORDING**, not only from punch-out. The only plausible disjoint pair
  is `bridgeMap.records_` (68 KB) against the apply group, and its `baseOrigins_`/
  `prepared_` borrows must survive to bridge compile and can be re-entered via
  `requiresBridgeRefresh`; even then an arena is sized to the max, not the sum.
- **Tag-scoping corrections that change the arm arithmetic.** Tag 2 is not
  "prewarmViews total": the inner prewarms are tagged 10-16, so `prewarmViews` costs
  **1,240 KB** (790 + 450), and the earlier "~340 KB `viewSet->prewarm()` plus
  `frozenCaptureCompanions`" was a double-subtraction. Tags 1 (80 KB), 3 (1334 KB) and
  5 (7 KB) are **boot aggregates** over tracks/enabled slots - not per-arm costs, so the
  "2.3 MB" and "1.8 MB" arm figures are both withdrawn. The arm increment is ~1.24 MB plus
  the two 80 KB history vectors on first use.
- **The two 80 KB history vectors are a permanent rotating pair.** At commit,
  `publishPreparedCompanionHistory` swaps `preparedCompanionHistory_.rows` into
  `passes.editPasses`, so both 2048-row `EditPass` vectors stay resident (160 KB) and the
  pair's capacity becomes canonical history; only the *content* is stop-only.
- **The earlier `0xE00 == 128 * sizeof(MidiEvent)` attribution was wrong** and is corrected
  in place here and in `PROJECT_STATE.md`. With `sizeof(MidiEvent) = 20`, `0xE00` does not
  decode to element 128 of a `MidiEvent` store; the 28-byte stride is
  `PlaybackBridgeRecord`, and the faulting object is unidentified. The gather-destination
  guard stays as defence in depth; the reproduced allocator-exhaustion hang is unaffected.

Consequence for step C: a single shared buffer cannot absorb the 450 KB group, so the
reduction has to come from capacity shaping (2048-row horizons sized from actual content
with explicit admission denial) and from revisiting whether a full view set (727 KB) must
be provisioned per armed slot - which is step E's question. Recorded per-buffer lifetimes
the design must respect: live-ingress capacity (`liveCaptureStaging`, `capture.store`,
`pendingNoteChanges_`, `overdubPasses`), MIDI-tick service capacity (`canonicalPassInputs`,
`companionPasses`, `apply`, `reconstruction`, `spans`, `pairLookup`), stop-only capacity
(`frozenCaptureCompanions`, `bridgeMap`, the history pair), and published/borrowed
artifacts that cannot be repurposed (views, selected bridge, `bridgeBases`, history rows,
transfer-horizon rows).

**2026-09-17 step F: the transport-start 81920-byte owner is the deferred record arm, not
a separate transport path.** Identification only - no firmware change, and no transport
admission gate is added (the user kept this out of the RC). The chain is: a record request
while the clock is stopped is *parked*, not started - `TrackManager::startRecordingTrack`
sets `pendingRecord[i]` and `TRACK_ARMED` and returns
(`src/TrackManager/TrackManagerCaptureQueue.cpp:53-77`) - and the transport start releases
it through `ClockManager::toggleTransport` -> `TrackManager::updateAllTracks` ->
`TrackManager::handlePendingRecordStart` -> the same `Track::startRecording` ->
`Track::prewarmPlaybackForSlot(..., prepareCapture=true)` ->
`Loop::prewarmPreparedCompanionHistory`. There, two same-size reserves are the only 81920
requests on the path: `passes.editPasses.reserve(rowLimit)` (the canonical side) and
`preparedCompanionHistory_.rows.resize(rowCount)` (the candidate side), both
`2048 * sizeof(EditPass)` with `sizeof(EditPass) = 40` on device. There is no other
81920-byte allocation reachable from a transport start: playback start
(`Track::startPlaying`) and `prewarmPlaybackForSlot(..., prepareCapture=false)` never touch
the `EditPass` row horizon, and boot prewarm deliberately uses `prepareCapture=false`, which
is why the *first* record command is what pays it.

Consequence worth recording: because it is the same arm, step A already gates it. The
"transport start has no admission gate" concern is closed for this allocation - a pending
record start on an exhausted device now emits `#MEM_DENIED op=record_start` and leaves the
track armed instead of aborting. Boot prewarm (`src/TrackManager.cpp:59-80`) still
establishes the transfer horizon without this row horizon, unchanged.

Log-only evidence cannot separate the two same-size reserves (both are in the same
admission-gated call and the fixed build prints two identical `need=81920B` lines), so the
two are recorded as one owner: `Loop::prewarmPreparedCompanionHistory`. Distinguishing them
would need a per-reserve tag, not a new gate.

**2026-09-17 step A landed: a failed required prewarm now denies the record arm.**
The correction below left the arm gated only at the companion-history reserve; every
other arm-time prewarm was still advisory. `Track::prewarmPlaybackForSlot` returned
`void`, so its caller could not distinguish "reserved" from "failed and ignored", and
`Track::startRecording` proceeded into the take reset and RECORDING regardless. The
device therefore still reached the deferred stop with an unestablished prerequisite -
the deferred risk the point-of-use probe only converted from an immediate death.

`prewarmPlaybackForSlot` now returns whether every required reserve was established, and
`startRecording` consults it as its **first statement** - before the take reset, the
baseline stamp, the pre-roll move and the state transition. A denied take leaves the pass
timeline, the armed pre-roll, the pending-note set, the capture baseline and the track
state exactly as they were. The denial is explicit and distinct from the headroom gate:

```
#MEM_DENIED,<us>,op=companion_history_reserve,reason=reserve_failed,need=81920B,...
#MEM_DENIED,<us>,op=record_start,reason=prewarm_failed,need=0B,...
```

A **missing runtime slot** is now a denial too: without it there is no staging and no
canonical output, so the take cannot be admitted on the terms the reserve exists to
guarantee. That is a deliberate tightening for a slot whose `editPasses` exceed the
bridge row horizon (previously such a take was admitted with no reserves).

Evidence. Device ([`arm-admission-01`](../../captures/arm-admission-01/session_20260917_113447.log)):
exactly one `op=companion_history_reserve`, exactly one `op=record_start`, **zero**
`Recording started`, zero `FATAL`, device alive. `native`: **2193 cases, 2 failed (both
pre-existing unguarded-reentry failures), 1 skipped, 2189 succeeded**, including the new
`test_record_admission_denied_when_required_prewarm_fails`, which asserts the denial is
non-destructive *and* that the same call still arms when the reserve is available - so the
assertions cannot pass by the state machine refusing everything. The continuous-capture
fixture now links `TrackMidiEventRouting.cpp` / `CommittedMidiScratch.cpp` because the
admitted arm reaches `Track::invalidateCaches`; only the edit-session neighbours stay
off-path stubs.

Stale claim corrected in place: the earlier note that a boundary probe could not be made
predictive is superseded for this arm - the required prewarm's own result *is* the
prediction, and acting on it needs no margin heuristic.

**2026-09-17 CORRECTION: the record arm is NOT refusal-tolerant - it aborts too.**
The failure-semantics entry above left the record boundary ungated on the reasoning that
"the record completed through 834 refusals". That conflated two different things: those
834 refusals are chunk retries **during** recording, which really are tolerated; the
**record arm** has its own fatal allocation. Measured twice
([`attrib-tags-02`](../../captures/attrib-tags-02/session_20260917_105922.log),
[`attrib-tags-03`](../../captures/attrib-tags-03/session_20260917_110118.log)):

```
[15.341] Button press: Loop 1 (short)
[15.361] Loop 1: Start Recording
#MEM_EXT_REFUSED,15616436,bytes=81920,count=1,live=8095KB,blocks=637,heap_free=56KB
[InternalHeapFirstAllocator] FATAL: both internal heap and external memory are exhausted!
```

Note `count=1`: this is the **first** refusal of the run, so a gate consulting
"has the pool refused recently?" would not have denied it, and the heap (56 KB) is above
the 32 KB floor — an 80 KB request simply exceeds what remains. A record-arm gate would
need a margin derived from that measured 80 KB, which is a real trade-off (it would deny
records on healthy devices with under ~112 KB of heap), so it needs the user's decision
rather than a unilateral threshold.

**Record-arm reservation attributed: capture staging dominates.** The record arm's first
action is `LoopPlaybackRuntime::prewarmViews()`, whose first line is
`prewarmCaptureStaging()`; the boot tag measurement puts `prewarmCaptureStaging` at
**1334 KB retained** (`tags=[84, 0, 1334, 0, 8, 0, 0]`, tag 3), matching the +1240 KB the
arm adds. The class profile from
[`attrib-record-start-03`](../../captures/attrib-record-start-03/session_20260917_103705.log)
splits consistently: **+17 blocks of ~47 KB (792 KB)** is `FixedPlaybackEventStorage`'s
raw arrays (2048-entry `events`/`order`/`phases`/`mergeScratch`/`retained`, ~57 KB each,
across the live and frozen roles), and **+82 blocks of ~4.8 KB (390 KB)** is the companion
and lookup prewarms in the same function (`frozenCompanionPairLookup`,
`frozenCompanionSpans`, `frozenCompanionReconstruction`, `frozenCompanionApply`,
`frozenCompanionBridgeMap`, `frozenCompanionPasses`, `canonicalPassInputs`, each sized to
`kEventsPerView`/`kRecordsPerBridge`). The first part is measured; the second is inference
from the prewarm list plus the class shape, not yet individually tagged.

**2026-09-17 point-of-use reserve denial WORKS - the record arm no longer kills the
device, and the overdub gate is re-verified on the current build.** The failed arm probes
above were taken *far* from the allocation they were predicting. The fix is to check at the
**point of use** instead: `Loop::prewarmPreparedCompanionHistory` now pre-checks the two
`EditPassVec` reserves (the `passes.editPasses` row horizon and the `work.rows` horizon)
immediately before each allocation, through
`MemoryMonitor::admitOperationMemory(OperationKind::CompanionHistoryReserve, bytes)`, and
returns the `false` its contract already allows instead of reaching
`InternalHeapFirstAllocator::abort()`. Nothing allocates between the probe and the real
reserve, so there the probe *is* predictive.

Verified ([`point-of-use-reserve-01`](../../captures/point-of-use-reserve-01/session_20260917_112246.log)):

```
#MEM_DENIED,...,op=companion_history_reserve,reason=reserve_failed,need=81920B,heap_free=76KB
#MEM_DENIED,...,op=companion_history_reserve,reason=reserve_failed,need=81920B,heap_free=64KB
#MEM_DENIED,...,op=overdub_start,reason=heap_low,need=16384B,heap_free=28KB
#MEM_DENIED,...,op=overdub_start,reason=heap_low,need=16384B,heap_free=28KB
```

**No `FATAL`, device alive (290 KB of serial in a 6 s probe), and because the arm survived,
the run reached the overdub where the round-1 gate denied again** - so the original
acceptance criterion now holds on the current build, not just on the build that first
proved it. The only refusals in the run are 6 x 32768 (the tolerated chunk retry storm) and
2 x 81920 (the two reserve probes).

Residuals to be honest about:

- The arm **continues** after the denied reserve, because the callers ignore
  `prewarmPreparedCompanionHistory`'s result; `Recording started` appears twice in that log.
  The prewarm exists so "stop grains never grow vectors or relocate canonical history", so
  on this path that invariant is not held and a later stop can still grow or abort. Denial
  at the point of use trades an immediate death for a deferred risk - better, not complete.
  **SUPERSEDED 2026-09-17:** the arm no longer continues; `prewarmPlaybackForSlot` reports
  its result and `Track::startRecording` denies before any mutation (see the step-A entry at
  the top of this journal). The deferred risk is closed for this route.
- The probe's own failed `extmem_malloc` goes through the `--wrap` accounting and therefore
  **registers as a pool refusal**, which is why the first denial already reads
  `pool_refused=1/81920B`. Harmless for the gate (it only reinforces `poolRefusing`) but it
  means the refusal counter counts probe attempts as well as real ones.

**2026-09-17 NEGATIVE RESULT: a boundary probe cannot make the record arm safe; reverted.**
Tried the "reserve required memory, and if reservation fails the operation does not start"
pattern from the user's plan, at the record-arm boundary, using a direct allocate-then-release
probe rather than an estimate. Two attempts, both failed, both reverted:

| probe | result |
|---|---|
| single block, the measured largest request (81920 bytes) | **passed**, arm still aborted at that same 80 KB allocation ([`reserve-gate-01`](../../captures/reserve-gate-01/session_20260917_111603.log): refusal at `heap_free=76KB, tag=6`) |
| 16-block profile of the arm's shapes (~307 KB total) | **passed**, arm still aborted identically ([`reserve-shape-01`](../../captures/reserve-shape-01/session_20260917_111749.log)) |

Why: the arm allocates roughly **960 KB across ~100 blocks** (tag 1 `80 + tag 2 `790 + tag 5 `7
+ tag 6 `80), so a single-block probe validates headroom that the arm's own earlier
allocations then consume, and a 16-block probe still under-reserves by 3x. Probing the
aggregate as one block would be fragmentation-hostile and would deny records that could
succeed; probing enough blocks to be faithful is no longer a probe.

So a per-boundary **probe** cannot predict this operation, and the honest options are:

1. **Shrink the arm's footprint** — the named levers are the seven companion/lookup
   prewarms at **450 KB measured** (`canonicalPassInputs` 144, `companionPasses` 96,
   `companionBridgeMap` 68, `companionApply` 66, `companionReconstruction` 64,
   `pairLookup` 10, `spans` 2) and capture staging at **1334 KB per slot**, which is
   prewarmed per track/slot and is the largest single line item in the whole budget.
   This is the recommended path.
2. **Hold a real reservation** rather than probe: acquire the arm's blocks up front and
   release them to the owner instead of back to the pool. That is a design change with
   ownership consequences, not an admission check.
3. **Coarse aggregate margin gate** (~1 MB at the record arm). It would deny records on a
   loaded device, which is arguably correct since the arm would abort there anyway, but it
   is a user-visible capability change and needs the user's call.

Reverted rather than left in place: a probe that cycles ~300 KB through the pool on every
record arm, and still does not prevent the abort, is worse than no gate at all.

**2026-09-17 record-arm reservation MEASURED per contributor - and it is NOT staging.**
The refusal line now dumps the whole tag vector, which matters because the process dies
there, so one arm gives every contributor's retained size. Measured
([`attrib-arm-vector-01`](../../captures/attrib-arm-vector-01/session_20260917_111029.log)):

```
tags (KB) = 80 / 790 / 1334 / 0 / 7 / 0 / 0 / 0 / 0 / 10 / 2 / 64 / 66 / 68 / 96 / 144 / 0
tag ids:    1    2    3     4   5   6   7   8   9   10  11  12   13   14   15   16
```

| contributor | measured |
|---|---|
| 1 `prewarmCaptureStopJobs` | 80 KB (boot aggregate over 8 tracks) |
| 2 `prewarmViews` tagged portion | **790 KB** |
| 3 `prewarmCaptureStaging` (cumulative, mostly boot) | 1334 KB |
| 5 `prewarmActiveOverdubTransferHorizon` | 7 KB (boot aggregate) |
| 6 `prewarmPreparedCompanionHistory` | **0 KB - this is the call that aborts** |
| 10 `frozenCompanionPairLookup` | 10 KB |
| 11 `frozenCompanionSpans` | 2 KB |
| 12 `frozenCompanionReconstruction` | 64 KB |
| 13 `frozenCompanionApply` | 66 KB |
| 14 `frozenCompanionBridgeMap` | 68 KB |
| 15 `frozenCompanionPasses` | 96 KB |
| 16 `canonicalPassInputs` | **144 KB** |
| 4 `prewarmCanonicalCaptureOutput`, 7 `getPlaybackOrder`, 8 `resetPassTimeline`, 9 `prepareFirstRecordingBar` | 0 KB |

**CORRECTED 2026-09-17 (step B audit, see
[ExecutionBounds](../Authority/Architecture/ExecutionBounds.md#record-arm-reservation-buffers-bytes-and-lifetimes-2026-09-17)):
tag 2 is not "prewarmViews total".** The seven inner prewarms are tagged 10-16, so
`prewarmViews` costs **1,240 KB** = tag 2's 790 KB + those 450 KB, and the earlier
sentence calling "~340 KB `viewSet->prewarm()` plus `frozenCaptureCompanions`"
double-subtracted the 450 KB group. Tag 2's 790 KB is exactly views+bases 350 KB +
bridges 360 KB + `frozenCaptureCompanions` 80 KB. Tags 1/3/5 are **boot
aggregates** over tracks or enabled slots (`TrackManager::prewarmPlaybackRuntime`
prewarms every enabled, non-empty slot with `prepareCapture=false`), so they are
not per-arm costs: the arm's incremental cost is 790 + 450 + the two 80 KB history
vectors (once) + 0 for staging/stop-jobs/horizon. Device element sizes were
measured with the device toolchain (`sizeof(MidiEvent) = 20`, not 28) and every
buffer reconciles exactly with these tags.

**This corrects the entry above.** Staging does dominate the *boot* footprint (1334 KB,
and tag 3 barely moves on the arm because that slot's staging was already prewarmed), but
the arm itself is dominated by **`prewarmViews` at 790 KB**, of which the seven
companion/lookup prewarms are **450 KB measured** (`canonicalPassInputs` 144,
`companionPasses` 96, `companionBridgeMap` 68, `companionApply` 66,
`companionReconstruction` 64, `pairLookup` 10, `spans` 2) and **790 KB** is
`viewSet->prewarm()` plus `frozenCaptureCompanions.prewarm(kRecordsPerBridge)` (350 KB
views+bases, 360 KB bridges, 80 KB companion rows). The
round-4 sentence calling the ~390 KB companion group "inference" is now measurement, and
its size was under-estimated by ~60 KB.

**CORRECTED 2026-09-17 (step B): the "~340 KB" in the sentence above was wrong; it is 790
KB, and the seven prewarms are *not* a subset of it.** See
[ExecutionBounds](../Authority/Architecture/ExecutionBounds.md#record-arm-reservation-buffers-bytes-and-lifetimes-2026-09-17).

So arming one record on a slot costs roughly **1.24 MB of new reservation**, not 2.3 MB:
790 KB tagged `prewarmViews` + the 450 KB seven-prewarm group, plus the two 80 KB
companion-history vectors the first time, plus 0 for staging, stop jobs and the transfer
horizon, which are already boot-resident. The 1334 KB figure is the *boot aggregate* of
both staging roles over every enabled slot (~116 KB per slot), and the 80 KB stop-jobs
figure is a boot aggregate over eight tracks - neither is a per-arm cost, so adding them
to the arm double-counts. Against the ~1.3 MB a loaded device has free at boot, the 1.24 MB
arm is still the whole reason a device with several slots loaded cannot arm a record - and
`prewarmPreparedCompanionHistory`'s 80 KB row horizon is simply the last
allocation in that sequence, which is why it is the one that aborts.

**2026-09-17 the 80 KB fatal request is NAMED and measured: the companion-history row
horizon.** The `#MEM_EXT_REFUSED` line now carries the active bracket tag, so a request
that kills the process still identifies the step that made it. On the record arm
([`attrib-arm-tag-01`](../../captures/attrib-arm-tag-01/session_20260917_110509.log)):

```
[16.961] Loop 1: Start Recording
#MEM_EXT_REFUSED,17217439,bytes=81920,count=1,live=8095KB,blocks=637,heap_free=56KB,tag=6,tag_live=0KB
[InternalHeapFirstAllocator] FATAL: both internal heap and external memory are exhausted!
```

`tag=6` is `TagCompanionHistory` and `tag_live=0KB` says the failing allocation was that
call's **first**. So the arm completed `prewarmCaptureStopJobs`, `prewarmViews` (with its
staging), `prewarmCanonicalCaptureOutput` and `prewarmActiveOverdubTransferHorizon`, and
died in **`Loop::prewarmPreparedCompanionHistory()`**.

That resolves the size exactly: `PreparedCompanionHistory::rows` is an `EditPassVec`
(`std::vector<EditPass, InternalHeapFirstAllocator<EditPass>>`, matching the aborting
allocator), all four of `EditPass`'s enums are `: uint8_t`, so `sizeof(EditPass) = 40`, and
the reserve is `passes.editPasses.size() + companionCapacity == kRecordsPerBridge = 2048`
rows: **2048 x 40 = 81920 bytes, exactly the request**.

So it is the **designed row horizon, not an overflow and not a doubling** — the round-4
hypothesis that `81920 = 4096 x 20` was a doubled 2048 horizon is **withdrawn**. Nothing
overran `kRecordsPerBridge`; the arm simply asks for one more 80 KB block when the pool is
at 8095/8182 KB and the heap at 56 KB, and neither can supply it.

**Record-arm reservation, now attributed step by step** (order is
`prewarmPlaybackForSlot`): capture stop jobs, then `prewarmViews` (staging, plus the seven
companion/lookup prewarms), then canonical capture
output, then the overdub-transfer horizon, then the 80 KB companion-history rows, then the
playback order. **CORRECTED 2026-09-17 (step B):** the "1.8 MB" here is superseded by the
measured 1.24 MB arm increment (tag 2's 790 KB + the 450 KB seven-prewarm group + the two
80 KB history vectors), because the staging, stop-job and horizon figures quoted in this
paragraph are boot aggregates, not arm costs. See
[ExecutionBounds](../Authority/Architecture/ExecutionBounds.md#record-arm-reservation-buffers-bytes-and-lifetimes-2026-09-17) -
read that section, not the sums in this entry, for the arm reservation.

**2026-09-17 narrowing the 80 KB `InternalHeapFirstAllocator` request (offline).** The
fatal transport-start request is 81920 bytes. Element sizes were computed on the host:
`CausingTargetPair` 8, `ConstrainedNoteGeometry` 20, `PendingNoteChange` 20, `NoteId` /
`PassId` / `EditPassId` / `MidiEvent*` / 32-bit `size_t` 4, `uint16_t` 2, and
`EditSessionInteraction` **36** (its `NoteBaseline` is 12 bytes, not 8 — an earlier
hand-count of 28 was wrong). So possible element counts are 10240 x 8, 4096 x 20,
20480 x 4 or 40960 x 2, and `EditSessionInteraction` plus `MidiEvent` are **eliminated**
because neither divides 81920.

**CORRECTED 2026-09-17:** the parenthetical that `MidiEvent` is "28 bytes by the
snapshot-gather stride in `0x6002C97C`" is wrong - `sizeof(MidiEvent)` is **20 bytes** on
device, measured by instantiating the headers with the
`teensy41-capture-serial` toolchain (`midi::MidiType` is `: uint8_t`). `0xE00` is therefore
*not* `128 * sizeof(MidiEvent)`; the stride that makes `0xE00` exactly 128 elements is 28,
which is `sizeof(PlaybackBridgeRecord)`. Which object owned that address is not
established by the stride, so the DACCVIOL attribution built on the old arithmetic (a
snapshot-gather fill writing element 128 through a lost destination base) is
**unverified** and is corrected in place below. The landed gather-destination guard stays
useful regardless; the reproduced allocator-exhaustion hang is unaffected.

No `reserve()` in the tree takes 4096/8192/10240 literally, so the allocation is almost
certainly a **`std::vector` doubling**, and `81920 = 4096 x 20` is exactly the doubling of
a **2048-row horizon** (`RuntimePlaybackViewConfig::kRecordsPerBridge = 2048`). The
obvious 2048-row candidate, `Loop::prewarmPreparedCompanionHistory`, is not claimed *by
this entry*: at this point its guard was thought to preclude growth. **SUPERSEDED
2026-09-17:** it *is* the owner, and the request is the designed row horizon rather than a
doubling (see the entry above and the step-F entry at the top).

**Device is offline as of this note** (`/dev/cu.usbmodem154944801` not enumerated): the
transport-start abort left it hung, and it no longer answers the loader handshake — the
Teensy Loader reports *"Please press the PROGRAM MODE BUTTON on your Teensy to upload
your sketch."* Reading the record-arm tags and any further device verification needs a
physical PROGRAM MODE press / power cycle.

**2026-09-17 there are TWO aborting allocators, and the cliff is reached during
prepare, so the fatal allocation lands on whichever operation comes first.**
`ExternalMemoryFirstAllocator` (extmem -> malloc -> abort) is the one the failure-semantics
gate covers. `InternalHeapFirstAllocator` (malloc -> extmem -> abort, line ~82 of
`include/Utils/InternalHeapFirstAllocator.h`) is a **second** abort site, and it fires on
paths with no gate. Observed in
[`attrib-tags-01`](../../captures/attrib-tags-01/session_20260917_104406.log), right
after a Global Transport press:

```
[23.219] Button press: Global Transport (short)
[23.219] [MIDI] OUT MIDI Start (DIN=1 USB=1)
#MEM_EXT_REFUSED,23485180,bytes=81920,count=1,live=8104KB,blocks=643,heap_free=56KB
[InternalHeapFirstAllocator] FATAL: both internal heap and external memory are exhausted!
```

An **80 KB** request with 56 KB of heap free and the pool at 8104 KB. It aborts even
though the pooling heap had 56 KB — the request is simply larger than the remainder, so
a headroom gate would have caught it exactly as the overdub one does.

**This reshapes the remaining work.** The device reaches the cliff during the harness's
*prepare* phase, so the first allocation that cannot be served lands on a different
operation run to run: a transport start here, the record arm in
[`attrib-record-start-03`](../../captures/attrib-record-start-03/session_20260917_103705.log).
A gate at one mutating boundary therefore only fixes that boundary, which is the case for
gating **every** mutating boundary or for making the allocators themselves non-fatal on
audited paths.

**Tag-scoped attribution landed and is validated on the boot path.** `beginTag`/`endTag`
brackets the five steps of `Track::prewarmPlaybackForSlot`, and the tag is stored per
shadow entry so a free decrements the tag that allocated the block. The 1 s line gained
`tags=` (net KB for tags 1-7). Boot samples read `tags=[80, 0, 1334, 0, 8, 0, 0]` — i.e.
`prewarmCaptureStaging` retains **1334 KB**, `prewarmCaptureStopJobs` 80 KB,
`prewarmActiveOverdubTransferHorizon` 8 KB. Boot takes the staging tag
(`prepareCapture == false`); the record arm takes the `prewarmViews` tag, which is the one
still unread. The brackets sit in a `TRACK_COLD_MEM` unit so they cost **zero ITCM**
(`RAM1 code` unchanged at 425256); the 12-byte shadow entries cost 32 KB of RAM2
(`variables` 149664, 374 KB still free).

**Still open:** read the record-arm tags (the run must get past the transport-start
abort), gate the transport-start boundary, and decide whether the allocators themselves
should be non-fatal on audited paths.

**2026-09-17 record-start reservation attributed to the first record arm, with a
size profile and two candidates eliminated.** Sampling moved to 1 s with an
`ExternalPoolAccounting` **live-bytes-by-size-class** breakdown (`cls=` live KB and
`clsn=` live block counts for <=128 B, <=1 KB, <=8 KB, <=32 KB, <=128 KB, >128 KB),
plus `chunks_free=` (`LoopEventStore::freeChunkCount()`) and `snap=` (`snapshotAllocByteCount()`),
both already-existing O(1) counters. Evidence:
[`attrib-record-start-03`](../../captures/attrib-record-start-03/session_20260917_103705.log).

The whole reservation lands in **one 1 s interval** and reads:

```
t=15.08  dLive=+1240KB  dBlocks=+104   dHeap=-168KB
         class <=8KB:   +82 blocks = +390KB   (~4.8 KB each)
         class <=32KB:   +4 blocks =  +58KB   (~14.5 KB each)
         class <=128KB: +17 blocks = +792KB   (~47 KB each)
         chunks_free unchanged (384)   snap=1819KB unchanged
```

Correlating with the control-event log, that interval contains `Loop 1: Start
Recording` at 14.55 and `Recording started @ tick 484` at 14.80 — the harness's
**first arm on the freshly selected slot**, inside its clear ritual. The actual 10-bar
record at 22.91 adds **nothing** (`dLive` stays within +1-2 KB per interval). So the
~1.24 MB is a **one-time first-record-arm / slot-prewarm cost, not a per-record
cost**.

The **-168 KB heap drop lands in the same 1 s interval** as the pool jump, so the
objective's "+1.26 MB pool / +168 KB heap" pair is a **single event: the first record
arm**, and the coarse 5 s sampling in
[`hang-8bar-memacct-01`](../../captures/hang-8bar-memacct-01/session_20260917_100202.log)
could not separate them. Two candidates are **eliminated**: it is not LoopEventStore
chunk storage (`chunks_free` never moves), and not snapshot slabs (`snap` holds at
1819 KB and does not rise across the jump). Naming the individual buffers needs per-site
tagging; the profile above (17 x ~47 KB, 82 x ~4.8 KB) is the input to that.

Reading note, so nobody re-derives the wrong thing: a first pass mis-mapped the
adjacent `snap=` and `heap_free=` fields in the log analyser and briefly recorded the
snapshot as *released* at the jump. The corrected reading is the one above; verify the
field mapping before trusting any delta from that line.

Also landed: the accounting functions (`recordPoolAllocation` / `recordPoolFree` /
`recordPoolRefusal` / `reset` / `logExternalPoolAccounting`) are now **`FLASHMEM`**.
They were in ITCM and growing them crossed the FlexRAM bank boundary again; moving them
to flash **freed 688 bytes of ITCM** (`RAM1 code` 425944 -> 425256, `padding` 40 -> 728,
`free for local variables` 6624). The emitter being flash-resident is what makes adding
fields to the 1 s line free in ITCM terms.

**Still open:** name the individual buffers behind the 17 x ~47 KB and 82 x ~4.8 KB
allocations, and decide whether any unaudited boundary needs its own gate.

**2026-09-17 failure-semantics fix landed: overdub admission denies instead of
aborting (verified before/after on the same recipe).** `MemoryMonitor::admitOperationMemory`
is a pre-emptive admission gate called from `Track::startOverdubbing` **before** the first
mutation, so a denial is a clean refusal with nothing to unwind. It reads only O(1)
signals: the internal-heap free counters and the external pool's *refusal* record
(`ExternalPoolAccounting::counters().poolLastRefusalMs`, set by the `--wrap` wrapper).
Deliberately not the live-byte counter, which over-reports and cannot size headroom.
Denies when heap free is below a 32 KB floor plus a per-operation margin (16 KB for
overdub start, 8 KB for record start / snapshot reserve) or when the pool refused an
allocation within 2 s. Emits `#MEM_DENIED` with operation, reason, required bytes, heap
free, and the pool refusal count/size/age. The operation is passed as a numeric
`OperationKind` and the gate is `FLASHMEM`, so the call site carries no literal and the
whole slice cost **zero ITCM** (`code:425944 + padding:40 = 425984`, still 13 banks;
`free for local variables` 6656).

**Acceptance, same 8-bar track 4 slot 1 recipe:** before,
`[ExternalMemoryFirstAllocator] FATAL` and a silent device
([`hang-8bar-refusal-03`](../../captures/hang-8bar-refusal-03/session_20260917_101436.log));
after,
[`hang-8bar-admit-01`](../../captures/hang-8bar-admit-01/session_20260917_101926.log) logs
two `#MEM_DENIED`s, **no `FATAL`**, no `Overdubbing started` line, and the device stayed
responsive (487 KB of serial in a 6 s probe against silence before) with the loop playing
until the harness's own transport stop. Not a single ownership, reservation or state
change happened, because the gate sits before all of them.

**Discrimination proven on-device** with a one-shot boot probe that evaluates the same
requirement as the overdub gate:

```
#MEM_ADMIT,  op=boot_probe,   need=16384B, heap_free=216KB, pool_refused=0/0B
#MEM_DENIED, op=overdub_start, need=16384B, heap_free=4KB,  pool_refused=715/32768B, since_refusal=1ms
```

So the denial is a threshold crossing, not a gate that always refuses.

**Deviation from the written plan, deliberate:** the allocator itself was *not* changed to
return null. The reach audit showed `ExternalMemoryFirstAllocator` backs
`SessionMidiEventVec`, `DisplayNoteVec`, `PlaybackOrderVec`, `PresentNoteVec`,
`NoteSpanVec`, the undo vectors and the note-edit map allocator across ~29 files, most of
which assume infallible allocation; a global null return would convert the abort into
unchecked null dereferences. The gate achieves the objective's outcome (operation-level
denial, transport untouched) without that risk. **Residual:** an allocation failure
*outside* a gated boundary can still reach `abort()`. Record start was audited and
deliberately left ungated, on the reasoning that it was refusal-tolerant because the record
completed through 834 refusals. **That reasoning was wrong** — those refusals are
mid-record chunk retries, and the record *arm* has its own fatal allocation; see the
CORRECTION entry above.

**Observed consequence, not a regression:** on this device the canonical 2-bar recipe also
drives the pool to ~99% after its record, so the gate denies its overdub too
([`admit-regression-2bar-01`](../../captures/admit-regression-2bar-01/session_20260917_102138.log)).
Previously that path aborted; now it refuses and keeps playing. The capability gap is
real and is what defect 2 must close — the gate makes it visible instead of fatal.

Landing note: `test_continuous_capture` compiles `TrackOverdubLifecycle.cpp`, so the gate
needed a native stub in `MemoryMonitor.cpp`'s `PIO_UNIT_TEST_NATIVE`/host sections (it
always admits there). Without it that suite ERRORED at link and the run silently reported
2157 cases instead of 2192 — worth watching, because a dropped suite looks like a clean
pass. With the stub: native **2192 cases, 2188 passed, 1 skipped, the same two known
unguarded reentry failures**.

**Still open:** attribute the record-start reservation (+1.26 MB pool / +168 KB heap) to
named contributors, and decide whether any unaudited boundary needs its own gate.

**2026-09-17 failure-semantics audit: the pool was already denying allocations for most
of the record; one 5 KB request with no denial handling is fatal.** Added
`#MEM_EXT_REFUSED` to `__wrap_extmem_malloc` (distinct-size logging plus a
`refused=count/largest` field on the 5 s accounting line). Evidence:
[`hang-8bar-refusal-03`](../../captures/hang-8bar-refusal-03/session_20260917_101436.log).

What the refusals show on the same 10-bar record plus overdub recipe:

- A **32768-byte** request is refused **354 -> 834 times** while RECORDING, at pool
  `live~8143KB` (99.5%) and heap `4KB` free, roughly 25 refusals/second. **That path is
  already tolerant** — the record completes 7680 ticks and the transport keeps running,
  so a refusal-tolerant consumer already exists in this codebase.
- The **fatal** allocation is **5120 bytes**, refused once at `count=842`, and the
  `FATAL` follows within ~60 ms of `Overdubbing started @ tick 433`.

So exhaustion is not a surprise at the overdub: the device had been denying allocations
for most of the record and coped. One specific admission path has no denial handling and
reaches `ExternalMemoryFirstAllocator::abort()`. The failing request is small, which
means a headroom check at the **overdub admission boundary** would deny it cleanly long
before the retry storms.

Reach audit (why the allocator cannot simply return nullptr): `ExternalMemoryFirstAllocator`
backs `SessionMidiEventVec`, `DisplayNoteVec`, `PlaybackOrderVec`, `PresentNoteVec`,
`NoteSpanVec`, `UndoEntryVec`/`SessionUndoEntry` vectors and the note-edit
`ExternalMemoryUnorderedMapAllocator` maps, across ~29 files. Most callers assume
infallible allocation, so a global null return would convert the abort into unchecked
null dereferences. The semantics change must therefore stay on an audited path.

Still open: name the 5120-byte consumer. Arithmetic narrows it to 320 x `DisplayNote`
(16 B) or 256 x `NoteSpan` (20 B); `PresentNote` (12 B) and `MidiEvent` (28 B) do not
divide it. Per-instantiation probing is unaffordable here (only ~104 bytes of ITCM
headroom left), so pin it with a host-side sizeof check rather than firmware code.

**2026-09-17 pool accounting landed via linker wrappers; the hang is a record-start
reservation with the pool already 84% full.** The `--wrap` route works: `-D
EXTERNAL_POOL_WRAP=1` plus `-Wl,--wrap=extmem_malloc,--wrap=extmem_free` in
`env:teensy41-capture-serial`, with `__wrap_extmem_malloc/__wrap_extmem_free` in
`src/Utils/ExternalPoolAccounting.cpp`. It costs **no call-site code** — the ITCM grew
528 bytes and padding absorbed all of it, so the FlexRAM bank count is unchanged
(`free for local variables` 6720 → **6688**). `extmem_free` takes no size, so the
wrapper shadows pointer → bytes in an 8192-entry table in RAM2 (64 KB; RAM2 variables
51360 → 116896) and **counts** anything it cannot shadow rather than silently dropping
it — a first cut at 512 entries saturated immediately (`untracked=2047 unknown=1909`),
which is why the table is large and why both counters are on every line. Emitted every
5 s straight to serial (`MemoryMonitor::logExternalPoolAccounting`), deliberately *not*
gated on transport idle.

**Timeline, 10-bar record plus overdub on scratch track 4 slot 1
([`hang-8bar-memacct-01`](../../captures/hang-8bar-memacct-01/session_20260917_100202.log)):**

| point | pool live | blocks | internal heap free |
|---|---|---|---|
| idle before the test | 6836 KB of 8182 (**84%**) | 522 | 172 KB |
| record start (arm + clear) | **8095 KB (99%)** | 637 | **4 KB** |
| through the 10-bar record | 8095 → 8137 KB (flat) | 669 | 4 KB |
| overdub start → `FATAL` | — | — | — |

So the record phase does **not** leak: `live` is flat while `alloc` climbs 8500 → 9987 KB
and `freed` 404 → 1849 KB, i.e. heavy churn at the ceiling. The damage is done at
**record start**, which reserves ~1.26 MB of pool and ~168 KB of heap on top of an
already 84%-occupied pool. The **overdub start** is then the first allocation that
cannot be served, `ExternalMemoryFirstAllocator` hits `abort()`, and the device goes
silent with no crash record.

Two independent defects fall out of this, in priority order:

1. **`abort()` turns a transient allocation failure into an unrecoverable silent hang.**
   Both pools exhausted is a recoverable condition for a looper — drop the operation,
   log it, keep the transport alive. This is what makes the failure mode so bad, and it
   is fixable without touching the footprint at all.
2. **Idle occupancy leaves no headroom.** 84% of the pool is live at rest (prewarmed
   staging, view sets, snapshot slabs per slot), so a long record cannot fit. Either the
   steady-state footprint comes down or the record start needs less.

Calibration caveat: absolute `live` over-reports against the core's own stats
(`live=6836 KB` at a point where `[Memory]` reports `used=4661 KB`), while
`untracked`/`unknown` are 0, so the accounting is internally consistent but the absolute
figure is not yet trustworthy — read **deltas**. Native 2192 cases unchanged.

**2026-09-17 external-pool pseudo-counting: design sound, blocked by zero ITCM headroom.**
To see what grows during a long record plus overdub without probing the pool, the
pseudo-counting route is the right one — the pool cannot be queried from the transport
thread, and the repo already does exactly this for snapshots
(`snapshotAllocByteCount()` → `#CAP,DIAG,snapshot_alloc`). Built the same shape for the
external pool: O(1) counters (`liveBytes` / `highWaterBytes` / `liveBlocks` /
`cumulativeAllocated` / `cumulativeFreed`, plus a fallback-bytes counter for requests
that ask for PSRAM and get the internal heap), fed from the external-first allocators
that know the byte count on **both** allocate and deallocate, emitted as a short
direct-serial line every 5 s so it stays visible while PLAYING/OVERDUBBING where the
`#CAP` ring never flushes.

**It does not fit, and the reason is structural.** The committed tree sits exactly on
the FlexRAM bank boundary: `code:425224 + padding:760 = 425984 = 13 * 32768`. Padding
absorbs any growth up to roughly 760 bytes and then the next byte of ITCM code costs a
**whole 32768-byte DTCM bank**, so the accounting's ~4 KB of call sites (one call per
allocator instantiation, and there are hundreds) produced
`code:429304, padding:29448, free for local variables:-26080` and a hard
`Error program exceeds memory space`. Making the record functions out of line halved
the growth and still overflowed. **Remaining ITCM budget on this tree: ~700 bytes.**
Reverted; `teensy41-capture-serial` builds again at **6720**.

Options, in preference order: (1) link-time `-Wl,--wrap=extmem_malloc,--wrap=extmem_free`
so the counters cost **no** call-site code at all — one small FLASHMEM wrapper covers all
57 raw sites, and `extmem_free` needs no size because live *block* count plus cumulative
allocated bytes is enough for a trend; (2) free ITCM first by moving ~4 KB of cold code
to flash (the diagnostic/emitter units are the honest donors, since the accounting would
be paid for out of diagnostic code); (3) sample only counters that already exist
(chunk usage, snapshot bytes/nodes/slabs, heap free/min-ever) from one small emitter,
which fits the ~700-byte budget but misses the container-vector consumers.

**2026-09-17 8-bar record + overdub reproduces the hang: allocator exhaustion, not
a spin.** Temporary variant of `STIM-BASE-2-ONE-OD-01` (`--record-bars 8
--overdub-bars 2 --second-overdub-bars 0`) on scratch track 4 / slot 1, which was
verified empty first (`!CAPTURE_CONTENT` → `length=0`, zero record and effective rows)
so nothing musical was cleared. Evidence:
`captures/hang-8bar-track4-slot1-01/session_20260917_094002.log`.

The device recorded an 11-bar loop (8448 ticks; the wall-clock tempo proxy overshot 8
bars), stopped at 152.142 s, started the overdub at 153.369 s (`Overdubbing started @
tick 768`), and **about five seconds later printed
`[ExternalMemoryFirstAllocator] FATAL: both external memory and internal heap are
exhausted!` and went permanently silent** — 0 bytes in a 6 s probe with the USB port
still enumerated. That is the musician's hang, reproduced on the bench.

Two things this settles:

- **The hang is `abort()`, and it leaves no crash record.** `crashlog.txt` was unchanged
  at 394,625 bytes after the event and the next boot reported `crash=0`. `abort()` does
  not raise a Teensy `CrashReport`, which is why this class of failure has always read
  as a silent hang over serial and why "USB dropped" was the only host-visible signal.
  It is not a spin loop, and the earlier spin/`DACCVIOL` framing was wrong for this
  event.
- **Memory is already deep in the red before the record starts.**
  `[Memory] heap free=68 used=393 total=461 KB min_ever=68 KB` at 127.7 s uptime, against
  `heap free=280 used=181` at 6.3 s in the 00:39 session: roughly **212 KB of internal
  heap consumed in about two minutes** with four boot-playback slots loaded, before this
  test's own record. The 11-bar record plus its overdub then finished the job.

Harness verdict was `INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)`, abort reason *"serial
heartbeat lost (20.0s since last meaningful line)"*. Worth noting for the harness: the
device kept emitting `CC ignored` DEBUG spam while the heartbeat filter saw nothing
meaningful, so the reported abort preceded the real terminal event by ~5 s; the
allocator line is the authoritative one. The follow-on DACCVIOL this session started
from is most likely the same pressure family — element 128 written through a lost
vector header is what a corrupted header looks like, and pressure is what corrupts it.

**Next:** trace what grows the external pool and the internal heap across a long record
plus overdub. The periodic 60 s `[Memory]` line is far too coarse and this run logged no
PSRAM free/used at all; the useful capture is a bounded per-phase memory sample
(record start/stop, overdub start, each deferred grain) with the allocator's own
pool counters.

**2026-09-17 `!RESTORE_WORKSPACE` — the missing inverse of quarantine (shipped to
the device).** The workspace quarantine had no restore path, so it was one-way from
the device's point of view: nothing in the firmware read a `.bad.` path back. Added a
stopped-transport HITL command in `StorageManagerHitlSerial.cpp` with three forms —
`!RESTORE_WORKSPACE list` (inventory only), `!RESTORE_WORKSPACE <stamp>`, and a bare
`!RESTORE_WORKSPACE` (newest). It **renames only**: the live tree is parked as
`.bad.<now>` rather than deleted, so the operation is reversible in both directions.
Landing it re-exposed the same RAM1 trap as the instrumentation work — plain
`Serial.print`/`printf` literals in that unit tip DTCM `.rodata` by a whole 4096-byte
block (`free for local variables` 6720 → 2624 in one step, not gradually); every new
string is now `printf_P` + `PSTR` and headroom stays **6720**.

**Ground truth from the first inventory:** `/MidiLooper` carries **20 quarantined
`current` trees and 21 `checkpoints` trees**, with stamps from 2314 ms to 1476949 ms —
i.e. accumulated across many boots, most of them predating this session, several from
the ~2-7 s boot window. Quarantine touches only `current` and the checkpoints;
`/MidiLooper/sets/` (saved loops and revisions) is never in scope, so **no musical
content was ever at risk**. The stamp is device `millis()` at rename time and cannot be
derived from the host.

**How the session's tree was found, and the trap:** the stamp the quarantine script
printed (`121898`) did **not** exist on the card. That line was almost certainly stale
buffered serial output from an earlier user-run quarantine, not this session's — the
script prints whatever the port delivers, and 20 historical trees mean it has been run
many times. This session's quarantine actually produced `1476949`; the first restore
consumed it into the live tree, and the second attempt (chasing the phantom `121898`)
parked it as `current.bad.35610`. Restoring `35610` reproduces the pre-quarantine
boot signature — `Boot focus track=3` (display track 4, the track the musician was on)
with `Queuing boot playback loop slot restore 4 pending` — against the unrelated
`975846` tree that booted `focus track=4` with nothing pending. Identification must use
the boot signature, never the name. Rule, ask template and the `list`-first procedure:
[`Destructive-SD-Operations.mdc`](../../.cursor/rules/Destructive-SD-Operations.mdc).

**2026-09-17 overdub-start fault attributed; snapshot-gather destination guarded
(instrumentation only, not flashed for acceptance).** The 2026-09-17 00:39 manual
session (`captures/session_20260917_003908.log`) that ended right after
`[TRACK] Overdubbing started @ tick 624` is a **hard fault with auto-reboot**, not a
hang: the SD crashlog for that boot reads `Code was executing from address
0x6002C97C`, `CFSR: 82 (DACCVIOL)`, `Accessed Address: 0xE00`. Resolved against the
flashed ELF that is `Loop::tryGatherPublishedSnapshotEventsInWindow`
(`src/Loop/LoopSnapshotPlayback.cpp`). **CORRECTED 2026-09-17 (step B):** the arithmetic
that follows is wrong. `sizeof(MidiEvent)` is **20 bytes** on device (measured with the
device toolchain), so `0xE00` is *not* `128 * sizeof(MidiEvent)`; the stride that makes
`0xE00` exactly 128 elements is 28, which is `sizeof(PlaybackBridgeRecord)`, so the
faulting instruction's object is not identified. The original claim - the faulting
instruction is the 28-byte `MidiEvent` block store and `0xE00 == 128 * sizeof(MidiEvent)`,
i.e. a fill writing element 128 through a **null destination base** - and the
"lost vector header, not exhaustion" conclusion drawn from it are **unverified**.
`ExternalMemoryFirstAllocator` still cannot return null (extmem, then `malloc`, then
`abort`), which is why the guard diagnostic was worth shipping, but it is now defence in
depth rather than a fix for an attributed fault. USB stayed enumerated across the reboot and
`capture_session.py` only reports a reconnect on a read error, so the fault was
invisible; two independent blind spots (no `#CAP` flush while PLAYING/OVERDUBBING,
and no CDC liveness check) hid it. Retrieved report and the RAM2 map are in
`captures/crashlog_retrieval_20260917_0049.log`.

Diagnostic landed (SESSION_CAPTURE-gated): a destination-storage guard at the gather
entry that reports and returns a gather miss instead of faulting, plus a
`displayManager` tail canary — `DisplayManagerInternal::liveDisplayEventBuffer` is
linked flush against the end of `displayManager` in `.bss.dma`, a section that is
NOLOAD and never zeroed, so any overrun there lands on that 12-byte vector header.
Reported a real regression while landing it: the first cut used `Serial.printf`, which
put the format strings in DTCM `.rodata` and cost **4128 bytes** of RAM1, dropping
`free for local variables` from 6752 to 2624 — below the documented floor of 4000 —
and the device then crash-looped at boot with `IACCVIOL` executing from `0x2001792C`
(`Accessed Address: 0x200175C4 (Stack problem)`). Moved to `printf_P` + `PSTR`, out of
line and FLASHMEM: headroom is now **6720** for a 32-byte cost. Device run
`captures/dma-canary-reentry-04`: record plus four OVERDUBBING entries, `crash=0`, no
canary clobber and no lost destination — the crash did **not** reproduce under the
canonical `STIM-BASE-2-ONE-OD-01`-shaped fixture. HITL was blocked first by a
reproducible `Persistence drain stuck after 256 iterations` in the Clear gate (the
docs already classify the sync-drain stuck detector as a progress-accounting gap);
cleared with the supported, non-destructive `!QUARANTINE_WORKSPACE`
(`/MidiLooper/current` → `/MidiLooper/current.bad.121898`, checkpoints likewise; loop
slots untouched). Full native **2192 cases — 2188 passed, 1 skipped, and exactly the
two known unguarded manager/Button A reentry failures**, i.e. unchanged from the
`640cc02d` baseline; `teensy41-capture-serial` builds with **6720** bytes RAM1
headroom. The instrumentation is on the device and is protective: a recurrence logs
`#GATHER_DEST_CORRUPT` (destination address, `data`/`size`/`capacity`) or
`#DMA_STAGING_CORRUPT` (canary plus the neighbour's header) instead of faulting.
Next: reproduce on the manual sequence that actually crashed — long loop, overdub
started about one second after the record stop — since the canonical fixture did not.
**User's repro conditions (2026-09-17, authoritative from the musician's side):** the
fault shows up when **several tracks already hold playing loops** and you then record
a loop of **8 bars or more with overdub**. That is a materially different shape from
`STIM-BASE-2-ONE-OD-01` (2 bars, one track, 32 notes, no other track playing) and is
the condition any next attempt must reproduce: seed loops on multiple tracks and keep
them playing while recording an 8+ bar pass, then overdub immediately after the stop.
Likely coupling: more simultaneously playing slots means more per-slot playback
windows, so more concurrent gather targets and more resident snapshot/bridge state —
a direction that **used to be** argued from `0xE00 == 128 * sizeof(MidiEvent)`. That
arithmetic is withdrawn (see the correction above: `sizeof(MidiEvent) = 20`), so the
coupling is a hypothesis without a stride argument; it still does not affect the
reproduction recipe or the allocator-exhaustion finding.
**2026-09-16 task 2.3 causal predecessor/source identity — increment B (approved
superseded rule).** The user approved option (A): a deferred stop whose recorded
causal predecessor is no longer the published root must retire without publishing a
successor window. Implementing it falsified one premise of the audit sketch — the
canonical pipeline compares its owner identity **by address** against the live Loop
scalars and its ordering phases need one coherent live epoch, so resolution must stay
live (a recorded base makes the phases never complete, stalling before staging; a
measurement confirmed the live revision and root are unchanged during a normal
finalization). The recorded `causalBase` is therefore the **supersession reference**:
`causalBaseCurrent` is evaluated at the start of `CompanionBridge` (before any bridge
is built) and at the start of `AtomicTransfer`, and when false the interval retires
its ranked credit through `PendingStopAdmission::retireSupersededPreparation()` and
commits via `TrackPreparedStopCommit::commitSupersededCausalBase` — the staged
accepted capture pass is committed, no bridge is selected or published
(`causal_base_superseded`), and a prepared protected undo entry is published in the
same turn. The unpublished horizon returns to `Prewarmed` via the new allocation-free
`Loop::restoreSupersededPreparedCompanionHistory(owner)` (the recycle receipt guard
cannot admit it by design), with discard-and-reprewarm as fallback. Verified by the
superseded assertions in
`test_deferred_stop_records_and_pins_its_causal_predecessor_source`; full native 2,191
cases — 2,186 passed, 1 skipped, the same three intentional public-reentry failures;
`teensy41-capture-serial` builds with 6,752 bytes RAM1 headroom. Recorded deviation:
the superseded route publishes the capture pass only, relying on the newer root to
already carry the companion rows. Remaining in 2.3: successor-capture unguarding.

**2026-09-16 task 2.3 causal predecessor/source identity — increment A.** An audit for
requirement (f) found that a deferred stop re-derives its predecessor source **live** at
every consumer turn: admission computes `baseSource` for credit sizing and discards it,
the worker recomputes `baseSource`/`targetSource`/`frozenSource`/`currentSource` when the
grain runs, and the atomic commit recomputes `base` from `loop.playbackRevision` +
`loop.publishedSnapshotIdentity()`. A successor that published during finalization could
therefore change what the predecessor resolved against. `CaptureStopJob` now records
`causalBase` (the admission-time `PlaybackViewSourceIdentity`), pins its root with an
`ImmutableLoopSnapshotRef`, and sets `causalBaseRecorded`; the pin happens before the
raw-range ownership transfer (admission denies `causal_base_unavailable` when a non-null
published identity cannot be pinned) and retirement clears the receipt. Increment A
records only — consumers still read live state, so behavior is unchanged — and increment B
(next) switches the worker and the atomic commit to the recorded identity, deciding the
epoch-qualified publication rule including STOPPED recovery. New native
`test_deferred_stop_records_and_pins_its_causal_predecessor_source`; full native 2,191
cases — 2,186 passed, 1 skipped, the same three intentional public-reentry failures;
`teensy41-capture-serial` builds with 6,752 bytes RAM1 headroom.

**2026-09-16 task 2.3 prepared companion history interval qualification.** The last
item of the session/preview/undo/snapshot receipt slice: the Loop-owned
`preparedCompanionHistory_` is borrowed worker scratch that outlives one grain, so a
successor interval could observe, advance, commit, recycle or discard a
predecessor's in-flight preparation. Every consumer now carries the owning interval
identity, `PendingStopAdmission::intervalToken()` — a value (frozenGeneration +
stopTick), not an object address, so a copy of an admission made for credit probing
or retargeting still identifies the same interval while a reused ring descriptor
gets a fresh token; zero means the live session path. `preparedCompanionHistoryOwnedBy`
treats Cold/Prewarmed as receipt-free and owner-agnostic; `begin` records the token
and recycle completion/discard clear it. The deferred stop worker, the stop-commit
helpers and `beginCollectOverdubStopUndoPasses` pass `stop.intervalToken()`; default
arguments keep the live path source-compatible. New native
`test_prepared_companion_history_is_job_qualified`; the two Track-level fixtures in
`test_playback_prewarm` now begin the preparation with the admission that consumes it
and had their stale unqualified consumers qualified rather than deleted. Full native
2,190 cases — 2,185 passed, 1 skipped, the same three intentional public-reentry
failures; `teensy41-capture-serial` builds with 6,752 bytes RAM1 headroom (the Loop
member lives in the externally allocated pool, so the token adds no RAM1 variables).

**2026-09-16 task 2.3 session-exit handoff receipt ring.** The session-exit
`SessionExitSnapshotHandoff` receipt is no longer a single-slot unique pointer: it is
a Loop-owned FIFO ring of `kSessionExitReceiptCapacity = 4`, external-memory-first
allocated and cold-prewarmed by `Loop::prewarmSessionExitSnapshotHandoff()`. This
resolves the "Finding 2" blocker from the deferred-stop receipt audit — the receipt
outlives the stop job, so it cannot be job-owned. `canHandoffSessionExitSnapshot()` is
now a *free-slot* gate; "one receipt per open session" moved inside
`handoffSessionExitSnapshot()` behind `sessionExitHandedOff_` (set by the handoff,
cleared by `openOverdubSession()`), so a successor session is never serialized behind
an unconsumed publisher turn while a repeat handoff for the same session is still
refused. The idle publisher consumes exactly the oldest receipt per turn and retains
it when construction is disallowed; playback's stale-root overlay folds every pending
receipt (passes and companion ids) rather than one. New native
`test_session_exit_receipt_ring_is_fifo_and_not_serialized`; the old
"unconsumed receipt closes the gate" assertions in `test_playback_prewarm` and
`test_loop_take_survival` were rewritten to the ring contract, not deleted. Native:
`test_playback_prewarm` 165/165, `test_loop_take_survival` pass, full suite 2,189
cases — 2,184 passed, 1 skipped, the same three intentional public-reentry failures.
`teensy41-capture-serial` builds with 6,752 bytes RAM1 headroom (13 banks, code
425,208). No new boot allocation, so the increment A/B device memory check (build
`91e6b38c…`) still covers this slice.

**2026-09-16 task 2.3 increments A/B boot abort — bisected, root-caused, fixed.**
The memory check found the increment A/B firmware would not boot (`a7f10733…` emitted
only `boot_reset,crash=1` before the USB port dropped; the 32-entry-prewarm variant
`8c03dcfc…` behaved the same). A one-commit-per-flash hardware bisect isolated it to
`0f398715` (increment A): `7a275d1f` boots (21386 B, crash=0) while `0f398715` and
`162df28f` produce 0 bytes. Root cause: `prewarmCaptureStopJobs` reserved
`kRecordsPerBridge` (2048) rows for each job's `undoPassIds`/`undoCompanionIds`, both
`InternalHeapFirstAllocator` vectors — 4 ring entries × 8 tracks × 2 lists × 2048 × 4 B
= 512 KB demanded from a ~340 KB internal heap at boot, so the allocation aborted
before the sketch could stream. Fixed by bounding that reserve to
`kUndoCollectionReserveRows = 64` (16 KB); a session longer than that publishes no
prepared undo entry and takes the plain commit. The fixed tip is flashed and verified:
build `91e6b38c…`, 22091 bytes, 0 warnings, prewarm `PSRAM 5,141,952 -> 3,775,872`,
`heap 352,256 -> 290,816` — about +79 KB PSRAM and +20 KB internal heap over the
reference (`captures/task23-undo-prewarm-fix/result.json`). Native: 2188 cases, 2183
passed, one skipped, the same three reentry failures. Lesson recorded: a per-entry cold
reserve multiplied by ring capacity and track count must be sized against the
allocator's heap tier, not the policy maximum — both this and the 512-entry undo
prewarm were caught only by the boot sample.

**2026-09-16 task 2.3 deferred protected undo publication wired (increment B).**
The normal deferred overdub stop now records its `OverdubPassAdded` entry, closing
the long-standing "protected undo publication not connected to the production stop
pipeline" gap for the prepared PLAYING route. `TrackUndoPublication` is externally
allocated (same pattern as `captureStopJobs_`) and cold-prewarmed by
`prewarmProtectedUndo()`, which also reserves the undo stack's own entry capacity
(`Config::ABSOLUTE_MAX_UNDO_ENTRIES`, 512) — that is a publication precondition,
because `begin` rejects a transaction whose cursor reached capacity; its device
PSRAM admission must be measured. `stepDeferredUndoPublication` runs `begin` +
bounded `stepPrepare` inside the existing `UndoCollection` phase (no new phase). The
atomic transfer proves `canPublish(...)` first and only then calls
`advanceAtomicTransferWithUndo`; otherwise it takes the plain commit and the
transaction is restored rather than left half-open. `stepProtectedUndoDrain` runs one
bounded quantum per DeferredCleanup turn (`stepReleaseRedo` → `beginTrimAfterReclaim`/
`stepTrim` → `finish`, plus a `beginRestore`/`stepRestore` + `recoverPreparedEntry`
path), so `protectedState()` is Idle again before the job retires.

Evidence: `test_deferred_stop_prepares_job_owned_undo_entry` now also runs the stop to
completion and asserts the transaction returned to Idle and exactly one
`OverdubPassAdded` entry for the job's slot is published. `test_continuous_capture`
30 pass with the same three intentional reentry failures; full native 2,188 cases —
2,183 passed, one skipped, the same three; `teensy41-capture-serial` builds with
6,752 bytes RAM1 headroom (code 425,208). Known deviations: the DEC-063 bounded
disabled-pass reclaim continuation is still unwired (trim pressure is measured
without it), and STOPPED/empty routes publish no protected entry because they skip
companion preparation.

**2026-09-16 task 2.3 deferred undo collection, job-owned prepared entry (increment A).**
The deferred overdub stop now collects its session undo identities in bounded grains
and builds the `OverdubPassAdded` entry from the job's own slot and baseline. New
`PendingStopPhase::UndoCollection` runs on the prepared PLAYING route between
`CompanionHistory` and `CompanionBridge` (the collection needs the current prepared
companion receipt); the STOPPED/empty routes skip companion preparation and never
enter it, and `retireSkippedCompanionPreparation` retires the estimate. A dedicated
`remainingUndoCollectionGrains`/`undoCollectionQuantum` class (sized from
`Loop::requiredSessionStopUndoCollectionGrains`) is included in
`finalizationComplete()`, so a saturated estimate denies admission instead of
stalling a phase. `Track::stepDeferredUndoCollection` drives the resumable collection
and calls `TrackUndo::prepareOverdubSessionEntry`; the collection's private outputs
are pre-reserved in `prewarmCaptureStopJobs` (the API never grows them, and an
unreserved output is a denied collection, not silent corruption).
`Loop::reclaimUnreferencedDisabledEditPasses` moved to flash to hold 13 ITCM banks.

Evidence: new native `test_deferred_stop_prepares_job_owned_undo_entry` (production
route: entry kind/slot/loop from the job, baseline pointer identical to the job's).
`test_continuous_capture` 30 pass with the same three intentional reentry failures;
full native 2,188 cases — 2,183 passed, one skipped, the same three;
`teensy41-capture-serial` builds with 6,752 bytes RAM1 headroom. Increment B (next):
externally allocate `TrackUndoPublication`, begin/stepPrepare from the prepared entry,
flip the atomic transfer to `commitWithUndo`/`commitStoppedWithUndo`, and drain
`ReleaseRedo → AwaitReclaim → Trim → Complete` in bounded DeferredCleanup turns.

**2026-09-16 task 2.3 deferred-stop receipt audit — design decision required.**
Auditing the last two receipt items found the consumer side is not wired, which
changes what "job-owned" must mean. (1) The normal deferred overdub stop publishes
**no** `OverdubPassAdded` entry: `pushOverdubPassAdded` is reached only from
`Track::finalizeCommitSideEffects` via the fault fallback `commitCaptureForStop`,
while `processDeferredOverdubStopGrain` calls `TrackPreparedStopCommit::commit` with
`undo = nullptr` (`commitWithUndo`/`commitStoppedWithUndo` have no callers) — the
standing "protected undo publication not yet connected to the production stop
pipeline" gap. So the job-owned undo baseline from the previous slice is correct
preparation but currently unreachable in production, and wiring the deferred
publication is what makes it load-bearing. (2) The session-exit receipt cannot be
job-owned: it is deliberately retained past the job and consumed later by the idle
snapshot publisher while playback reads it for the stale-root overlay, so it needs a
Loop-owned multi-slot ring; `canHandoffSessionExitSnapshot()` should test a free slot
rather than an unconsumed singleton. Open questions: receipt ring size and the
overlay's behaviour with more than one pending receipt. See
CONTINUOUS-CAPTURE-OWNERSHIP.md ("Deferred-stop receipt wiring — audit and decision
required"). No code was changed for these two items.

**2026-09-16 task 2.3 job-owned overdub-session undo baseline.** The stop-commit
path (`TrackUndo::pushOverdubSessionOnStop` -> `pushOverdubPassAdded`) built the
`OverdubPassAdded` entry's `beforePublishedSnapshot` from the live
`Track::overdubSessionBeforePublished_`, which `TrackUndo::beginOverdubSession`
sets at session open — so a successor session would have overwritten the
predecessor's baseline. `CaptureStopJob` now owns
`ImmutableLoopSnapshotRef beforePublished` / `hasBeforePublished`; admission moves
the live baseline into the job and clears the live member, and the consumer prefers
the head job's owned baseline (then the live session's, then a fresh capture). An
unconsumed baseline is retired with the job.

Evidence: new native `test_stop_job_owns_overdub_session_undo_baseline` (job owns
the baseline, live member clear, a later successor baseline leaves the job's
intact, flag gone after cleanup); `test_continuous_capture` 29 pass with the same
three intentional reentry failures; full native 2,187 cases — 2,182 passed, one
skipped, the same three; `teensy41-capture-serial` builds with 6,752 bytes RAM1
headroom. Still open in this slice: the session-exit snapshot handoff receipt and
the prepared-companion history.

**2026-09-16 task 2.3 job-owned capture preview.** The deferred cleanup seals the
interval's preview, but it consumed the *live* `Loop::capturePreview`, which a
successor's `beginCapture` clears — so a successor would have made the predecessor's
deferred seal retain the wrong rows. `CaptureStopJob` now owns `CapturePreview
preview`, transferred at admission by constant work (`CapturePreview::swapWith`,
per-vector swap, no copy and no allocation) with a job-identified read borrow.
`Loop::capturePreviewView()` is the single readable accessor — the borrowed job
preview while bound, otherwise the live member — and is deliberately out of line in
flash, because inlining the branch at 40+ display call sites cost a whole 32 KB RAM1
bank. Writers keep the `capturePreview` member; 45 read-only display/Edit sites were
migrated (no display/Edit code writes the preview). `TrackPreparedStopCommit::completeDeferredCleanup`
now takes the job's owned preview, so the deferred seal retains this interval's rows.

Evidence: new native `test_stop_job_owns_capture_preview` (production route) and
`test_capture_preview_borrow_release_is_owner_identified`; `test_continuous_capture`
28 pass with the same three intentional reentry failures; full native 2,186 cases —
2,181 passed, one skipped, the same three; `teensy41-capture-serial` builds with
6,752 bytes RAM1 headroom (13 ITCM banks). The per-descriptor capacity prewarms were
dropped: the transfers are allocation-free regardless, and they only added ~640 bytes
of new ITCM template code (14th bank). Still open in this slice: the overdub
session/undo before-snapshot, the session-exit snapshot handoff receipt, and the
prepared-companion history.

**2026-09-16 task 2.3 job-owned pending note changes.** First piece of the
session/preview/undo receipt slice. The worker previously read the *live* session's
`Loop::pendingNoteChanges()` while resolving the canonical tail, so a successor
interval would have mixed its Add/Shorten/Hide intents into the predecessor's
finalization. `CaptureStopJob` now owns `PendingNoteChangeVec pendingChanges`:
admission swaps the live set into the job in constant work and leaves the live
session the job's prewarmed empty vector, so a successor starts independent and
neither the swap nor the next interval allocates. `Loop::pendingNoteChanges()` is
the single readable accessor — it returns the job's owned set while a
job-identified borrow is bound, otherwise the live member — and writers keep using
the live member directly. The borrow is released with the job and is
identity-guarded, and the worker now resolves
`beginFrozenCanonicalTail(frozenSource, job.pendingChanges, targetSource)`.

Evidence: new native `test_stop_job_owns_pending_note_changes` (production route,
real Shorten intent, borrow read, independent live set after cleanup) and
`test_pending_change_borrow_release_is_owner_identified`; the existing
shorten-and-complete stop test now exercises the job-owned path.
`test_continuous_capture` 26 pass with the same three intentional reentry
failures; full native 2,184 cases — 2,179 passed, one skipped, the same three;
`teensy41-capture-serial` builds with 6,752 bytes RAM1 headroom. Still open in this
slice: `capturePreview` ownership across the stop, the overdub session/undo
before-snapshot, the session-exit snapshot handoff receipt, and the
prepared-companion history. Task 2.3 stays unchecked.

**2026-09-16 task 2.3 sealed raw-range ownership wired (approved transfer).**
`CaptureStopJob` now owns `LoopEventStore::CaptureRange rawRange`.
`Track::beginDeferredOverdubStop` proves `LoopEventStore::canSealCaptureRange`
before any mutation, seals the producer extent at punch-out and binds a
job-identified read borrow on `Loop`; a denied admission performs no seal and no
transfer, and an empty capture (no extent) skips the seal. `Loop::captureReadSource()`
exposes the producer and finalizing extents separately (mutual exclusivity is not
encoded) and `copyCaptureIntervalEvents` orders only a scratch copy, so the borrowed
sealed extent is never mutated; `findLastCaptureNoteOnIdentity` resolves closure
NoteId from the finalizing extent first. `Track::retireJobCaptureSourceGrain` retires
the owned extent in bounded grains, drops the borrow only while it still identifies
that job, and re-arms the descriptor's reserved destination (failure → explicit
denial at the next admission). `resetCaptureExtentPreservingReservedStorage` lets
`beginCapture` keep the successor directory a cut left behind. DisplayManager is
unchanged; the successor-capture guard stays in place deliberately.

Evidence: five new native regressions (seal transfer + successor capture
preservation, read-source row parity with byte-identical sealed extent after an
ordering merge, closure identity from the sealed extent, owner-identified borrow
release, denial performs no transfer) plus three migrated ownership assertions.
`test_continuous_capture` 24 pass with the same three intentional public-reentry
failures; full native 2,182 cases — 2,177 passed, one skipped, the same three;
`teensy41-capture-serial` builds with 6,752 bytes RAM1 headroom. All new functions
are FLASHMEM and `Loop::beginCapture`/`discardCapture` moved to flash to hold 13
ITCM banks. Device boot prewarm admission **PASS** on this build (`41b30d18…`,
`captures/task23-sealed-range-prewarm/result.json`): PSRAM free 5,142,912 ->
3,856,320 B (160,824 B/slot, unchanged from the dual-staging build), internal heap
352,256 -> 311,296 B, pool chunks 512 -> 416 — the +4 chunks/slot are exactly the
four `CaptureRange::reserveTail()` destinations per track; all slots `ready=1` with
8 credits and zero warnings. Task 2.3 stays unchecked.

**2026-09-16 task 2.3 FIFO/queue-head binding landed; raw-range wiring needs an ownership decision.**
The stop-descriptor ring is now the explicit admission authority: `captureStopJobTail()` is the
admission target (null when full, reported as a distinct `queue_full` stop stage), the worker
binds `captureStopJobHead()` once per grain instead of re-resolving the ring mid-step, and
`!STOP_STATUS` now reports `queue=<depth>/<capacity>`. Behaviour is unchanged while one job is
admitted (tail == head); the binding is what makes multi-job admission well-defined. New native
`test_stop_ring_admits_at_tail_and_binds_worker_to_head` and
`test_full_stop_ring_denies_admission_and_preserves_accepted_capture`; full native 2,178 cases —
2,173 passed, one skipped, the same three reentry failures; `teensy41-capture-serial` builds with
6,752 bytes RAM1 headroom.

The sealed raw-range audit then found a real blocker: during the deferred stop, `Loop::capture.store`
is still read by the Loop display-window merge helpers (`mergeCaptureStoreIntoWindowedEvents` /
`...MaterializedEvents`, which also *sort* it in place) and by `Track::appendCaptureNoteOffAtPhase`
for physical-closure note identity; `Loop::beginCapture` also frees a pre-reserved successor tail.
Sealing at punch-out without a Loop-internal read fallback would empty the live-capture overlay for
the whole finalization window. The recommended design (Loop-owned producer + job-owned
`CaptureStopJob::rawRange` + a Loop-internal producer-or-sealed read fallback, so DisplayManager is
untouched) is recorded in CONTINUOUS-CAPTURE-OWNERSHIP.md. Because it moves capture-interval
ownership between `Loop` and the admitted Track job, the architecture ownership-transfer protocol
applies and it needs approval before implementation. Task 2.3 stays unchecked; no upload occurred
in this increment.

**2026-09-16 task 2.3 ingress/staging isolation — dual capture-admission roles landed.**
Split the runtime's single `RuntimePlaybackViewSet::captureStaging` into two independently
prewarmed roles in the same owner (`captureStagings[2]` with `liveStagingIndex` /
`frozenStagingIndex`). Live ingress (`Track::recordMidiEvents` admission, `Track::noteOn`
reservation, the cross-wrap continuation, and the `startOverdubbing` reset) writes only the
live role; the one admitted stop job borrows the frozen role. `Track::beginDeferredOverdubStop`
freezes the live role and exchanges the two indexes in constant work
(`LoopPlaybackRuntime::exchangeCaptureStagingRoles`), so the job owns the rows it captured and
the successor interval gets a physically separate, clean ingress buffer.
`Track::processDeferredOverdubStopGrain` resets only its own retired frozen role at cleanup.
No new owner/module, no canonical/persistence/SD ownership change, no action-edge copy; the
exchange is two index bytes. Physical NoteOffs of already-open notes select the frozen role
while a job is pending and the live role otherwise.

Evidence: new native `test_playback_prewarm::test_capture_staging_roles_exchange_without_sharing_physical_storage`
and `test_continuous_capture::test_stop_job_and_live_ingress_own_distinct_staging` (production
Button A punch-out route). Focused: `test_playback_prewarm` 164/164; `test_continuous_capture`
18 pass with the same three intentional public-reentry failures. Full native 2,176 cases —
2,171 passed, one skipped, the same three reentry failures. `teensy41-capture-serial` build
passes: RAM1 local-variable headroom 6,752 bytes, RAM2 free 472,928, ITCM padding 232.
Cost: one extra 2,048-event staging buffer per fully-prewarmed runtime slot (external-first),
now counted by `RuntimePlaybackViewSet::fixedExternalBytes()`. Boot prewarm admission was
measured on device (`captures/task23-staging-prewarm/result.json`, build `fb4ab3a9…`): PSRAM
free 5,143,968 → 3,859,680 B across the 8 boot slots (160,536 B/slot — two stagings per slot,
no internal-heap fallback), all slots `ready=1` with 8 credits, no warnings, 46.1 % of the
8.38 MB pool still free after prewarm. The measurement used a capture-only extra boot line
(`DIAG,capture_prewarm,phase=before|after`, SESSION_CAPTURE-gated, no behavior change).
Task 2.3 stays unchecked — FIFO/queue-head binding, sealed raw-range wiring, causal
predecessor/source, per-job session/preview/undo/snapshot receipts, the selected-slot
full-prewarm/dense-content footprint and the three reentry cases remain open. See the active
change's CONTINUOUS-CAPTURE-OWNERSHIP.md and DEC-064 follow-up.

**2026-09-16 SEVT device verification — operative cause is the deferred overdub-stop completion, not #1.**
Re-evaluation requested at the SPLIT gate ("re-evaluate whether #1 is necessary from fresh
evidence"). A session-capture-gated decision trace was added to the SEVT lifecycle
(`DIAG,stored_verify,queue_skip_loop|queue_skip_unarmed|queue_accepted|drain_reset_loop|
drain_reset_heap|drain_reset_no_events|drain_emit_start|drain_complete`) and run on device.

- **Evidence (fresh boot, `captures/sevt-diag-stop/session_20260916_191231.log`):** a
  transport Stop while the selected track is OVERDUBBING produced **zero** `stored_verify`
  markers — no `armed`, no `queue_*`, no `drain_*`. The `DIAG,stop_grain` trace advances the
  deferred job to its terminal phase and `PERS`/`SAVE` complete afterwards, so the dump is not
  "cancelled": **it is never requested.**
- **Causal chain:** `Track::stopOverdubbingToStopped()` normally takes
  `beginDeferredOverdubStop(...)` and returns; the overdub pass is staged and later finalised
  through `TrackPreparedStopCommit::completeDeferredCleanup` ->
  `Loop::handoffSessionExitSnapshot(OverdubStopToStopped)`, which never reaches
  `Track::finalizeCommitSideEffects`. The only call to `queueDeferredStoredMidiVerification()`
  is inside `finalizeCommitSideEffects`, and it is reached from the *synchronous* fallback
  `commitCaptureForStop(OverdubStopToStopped, ...)` at `TrackOverdubLifecycle.cpp`. The
  deferred path — the normal one — has no equivalent call.
- **#1 as previously framed does not fix this.** Arming before state dispatch in
  `TrackManager::handleTransportStop()` only makes an *existing* queue call succeed; on this
  path there is no queue call to arm. Keep #1 frozen/closed and re-scope to the missing call.
- **#2 remains valid but is not the operative cause here** either: it protects a *queued* dump
  from an ineligible re-queue. In this flow nothing was ever queued. Keep the landed fix and
  its native regressions; do not bundle it with the new scope.
- **What did emit SEVT (`164505`)**: its `armed,1`/`armed,0` pair comes from
  `Track::stopPlaying()`, i.e. a transport stop while the selected track was *PLAYING*, not
  overdubbing. `stopPlaying()` re-arms and `rearmDeferredStoredMidiVerification()` calls the
  queue. That path is why the good run scored SEVT=511 and why runs that stop *during*
  overdubbing score zero.
- **Fix landed (was the re-scoped #1):** `Track::processDeferredOverdubStopGrain` now calls
  `queueDeferredStoredMidiVerification()` immediately after a successful
  `TrackPreparedStopCommit::completeDeferredCleanup`, so the deferred completion requests the
  dump at the same point the synchronous `finalizeCommitSideEffects` route does. One owner per
  commit route; the non-deferred fallback is untouched. Native regression
  `test_deferred_overdub_stop_completion_queues_stored_verification` in
  `test_continuous_capture` fails before the fix and passes after (the STOPPED destination
  itself cannot be driven to completion natively because it calls `silenceTrackMidiOutput`, so
  the shared completion is asserted on the PLAYING destination).
- **Device verification (deterministic, sanctioned stop-reproduction recipe
  `scripts/hitl_stop_interruption.py`):** SEVT=98 (`captures/sevt-fix-verify1`) and SEVT=196
  (`captures/sevt-fix-verify2`), each with `queue_accepted` -> `drain_emit_start` ->
  `drain_complete`. Verify2 also shows the one-shot contract intact: a second cycle
  re-arms/queues/emits, and the following redundant stop logs `queue_skip_unarmed` without
  destroying a dump.
- **The plain canonical `--preset base` still cannot demonstrate SEVT, for host reasons only:**
  on an empty slot its Loop-row select starts recording and the following clear wipes it
  (`skipped_empty`, `captures/sevt-fix-verify4`), and on a seeded slot it aborts at the
  overdub-stop press and tears the capture down
  (`captures/sevt-fix-rerun10`). Restoring `sevt_gate_required = True` therefore still needs
  either a canonical host that observes past the stop press, or an accepted decision that the
  stop-interruption recipe is the SEVT gate.
- **Temporary artifacts still in the tree (remove together after the gate is restored):**
  temporary C (host advisory SEVT gate) and the committed session-capture SEVT decision
  markers `bf8fb6a4`.

**2026-09-16 stored-MIDI verification lifecycle — bugfix #2 landed, #1 frozen (SPLIT gate).**
RCA for the missing `VERIFY stored overdub span (SEVT ticks)` produced two independent
causes, so the fix was split rather than bundled:

- **#2 (landed):** `Track::queueDeferredStoredMidiVerification` called
  `resetDeferredStoredMidiVerification()` *before* the eligibility guards, so a redundant
  or ineligible call (arm already consumed, or loop not representable) cleared an
  already-pending dump. Invariant now explicit: **an ineligible queue call must not
  destroy an already-queued verification dump.** The guards are evaluated first; the
  reset/replace happens only once the call is accepted. The arm/pending bookkeeping is no
  longer `SESSION_CAPTURE`-only so native tests can drive the lifecycle. Native regression:
  `test_redundant_stored_verification_queue_keeps_pending_dump` and
  `test_ineligible_stored_verification_queue_keeps_pending_dump` in
  `test_continuous_capture` (fails before the fix, passes after; that TU had no native
  coverage before). Firmware `teensy41-capture-serial` build SUCCESS.
- **#1 (frozen, gate OPEN):** transport-stop arming. `Track::stopOverdubbingToStopped()`
  never re-arms the one-shot dump; `Track::stopPlaying()` is the only re-arm. Ordering is
  resolved: the overdub branch reaches `commitCaptureForStop` -> `finalizeCommitSideEffects`
  -> `queueDeferredStoredMidiVerification()` **synchronously inside** the
  `TrackManager::handleTransportStop` loop, so any arm must be established **before state
  dispatch**, restricted to `isOverdubbing() || hasPendingDeferredOverdubStop()`, preserving
  the existing PLAYING re-arm. Do **not** implement until the rerun below says it is needed.
- **Cause #2 may already explain the 175335 failure** (it logged two `ODUB,stop,prefix_reuse`
  finalize attempts). Next: rerun the 175335-style canonical base on the #2 firmware and
  see whether SEVT appears without touching transport arming. If it does, #1 becomes a
  separate robustness question; if not, proceed to the #1 gate with cleaner evidence.
- **Temporary C stays until deterministic device verification.** The host gate reports a
  missing SEVT dump under `VERIFY advisory` (`sevt_gate_required = False`) rather than
  failing the run. Restore it to required only after the rerun deterministically emits SEVT.

**2026-09-16 dense snapshot representability + fragment transaction (openspec 2.3 /
immutable-loop-snapshot-passes 3.7):** the captured record/overdub pair is now a
native snapshot regression (`test_dense_device_capture_snapshot_preserves_every_committed_row`,
fixture `dense_capture_fixture.h`) that failed `Expected 734 Was 710`. Root cause:
bar 2 needs 260 event references, above the fixed 16-page (256-reference) per-bar
membership ceiling; `LoopSnapshotConstruction` failed at `appendCurrentReference`,
and because `appendNextPassChangeFragment` had already advanced
`PendingPassChangeProgress`, the failed fragment was dropped and a truncated
candidate published. Two bugs: representability and atomicity (DEC-047 item 6).
Fix: explicit `kSnapshotMembershipMaxRefsPerBar = 4096` envelope with derived
page-count limit applied equally to event-bar and note-bar membership and lazily
allocated pages; page counters widened to `uint16_t`; fragment incorporation is now
transactional (build on a working copy, commit authoritative progress only after
successful incorporation, discard the partial candidate on failure); failure reason
distinguishes transient/allocation retry from persistent representability, which
parks publication after two consecutive failures (`DIAG,snapshot_fragment`,
capture-gated). DEC-047 amended and ExecutionBounds rows 21/22 updated. New focused
tests: 257-reference boundary, partial-apply retry exactly-once, and publication
parking. Focused `test_loop_take_survival` 132/132; full native 2166 passed / one
skipped / three known task-2.3 reentry failures; `teensy41-capture-serial` SUCCESS
with RAM1 free for locals 6752 (unchanged).

**2026-09-16 hardware verification (representability PASS):** uploaded build
`5bb90b47bdae299d630be640…` and reran the dense record/overdub recipe plus the
content-dump/playback capture (`captures/task23-dense-snapshot-fix-record`,
`captures/task23-dense-snapshot-fix-playback`). Stored 766 effective rows
(record 190 + overdub 576) with bar 2 at **270 refs (> the old 256 ceiling)**.
Four restart cycles each played 765 rows with **0 duplicates**; the only absent
row is the pre-existing terminal loop-boundary Off (`NoteId=0`, tick 3839), which
is ledger/pairing semantics rather than the capacity defect. Exact ordering still
differs (pre-existing). So the 24-row omission class is fixed on hardware.

**2026-09-16 dense re-stress (representability re-confirmed):** after a fresh
upload/reset, `captures/task23-dense-fix-rerun3` stored 732 effective rows with
bar 2 at **260 refs (> 256)** and the focused interruption checkpoint **PASS**
(stop +10.1 ms, all-notes-off 3.0 ms, 19.0 s silence). Four restart cycles played
730 rows with **0 duplicates**; the two absent rows are ledger/pairing semantics,
not capacity — the loop-boundary Off (`NoteId=0`, tick 3839) and one of two
coincident same-pitch Offs (`NoteId` 26/138, tick 1669). The base preset's own
setup verdict stayed `SETUP_UNCONFIRMED` (`overdub_unmatched_open_notes`), which
is the dense generator leaving notes held at stop, unrelated to the snapshot fix.

**2026-09-16 HITL harness exit hardening:** the managed-capture runner could leave
a `capture_session.py` child holding the USB serial port if the scenario stalled
or the harness was killed (observed after the dense verification run). Fix:
`run_with_managed_capture` now runs the scenario behind a wall-clock watchdog
(`HITL_MANAGED_RUN_TIMEOUT_S`, default 360 s; `0` disables) and returns 3 while
the context stops the capture child; `capture_session.py` exits when its owner
dies via a parent-death pipe (`--owner-fd`) with a reparent fallback; `stop()`
closes the pipe and escalates SIGINT→SIGKILL. Host coverage:
`scripts/test_managed_capture.py` 11/11 (watchdog return/abort/disabled/exception
+ owner-pipe orphan exit).

**2026-09-16 latest checkpoint (14:29):** descriptor build a9a7217f uploaded on
retry; dense hardware test then lost USB during initial recording, before overdub.
No reboot marker or fault cause retained; serial port absent. Native full run has
2,159 pass / one skipped / three known reentry failures; final focused guard run
has 14 pass / the same three failures. Stored dense content verification on the
prior fixture passed its scoped input comparison; audible timing and restored
playback remain open. Device reconnection is required to continue hardware
investigation. No capture reader is running. See active FOCUSED-STOP-HITL.md.

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

**2026-09-16 dense reboot fix verified:** matching fault report recovered into
crash-log-retrieval.log: stack-boundary DACCVIOL in _svfprintf_r. Overlap diagnostic
scratch moved to RAM2; local frame reduced 1,372 -> 76 bytes. Two dense automated
runs now complete without reboot; adjusted Stop-window run passes focused silence
(18.9 s observation). Broad baseline/content/timing checks remain inconclusive;
no FIFO/reentry/task-2.3 completion. Firmware hash and details are in active
FOCUSED-STOP-HITL.md. All serial/MIDI runners are closed.

**2026-09-16 automated dense rerun:** same track-5/slot-1 recipe reproduced the
reboot; corrected boot marker is crash=1. Serial missed the early fault text;
SD crashlog has not been read. Runner/capture finished. The dense gate remains
open; no manual reproduction is required. See task23-dense-crash-report-rerun
and active FOCUSED-STOP-HITL.md.

**2026-09-16 overlap reboot follow-up:** user confirms spontaneous reboot during
record-pass/overdub overlap. Native Shorten/stop regression passes; root cause
remains unresolved. Fixed destructive double-read of Teensy CrashReport: startup
now preserves the crash flag and renders once to Serial and SD. Build/upload pass,
with unchanged 5,600-byte RAM1 headroom. Human overlap reproduction on track 5 /
slot 1 is the next checkpoint; serial capture is in task23-overlap-crash-report.
See active FOCUSED-STOP-HITL.md for firmware hash and evidence limits.


**2026-09-16 device checkpoint (track 5 / slot 1, authorized):** verified firmware
uploaded. Two normal stop cleanups, focused transport-Stop silence, and three
restart content cycles pass. The denser run disconnected/rebooted before its stop
trace drained and is INCONCLUSIVE_CAPTURE; reset cause is unknown. No dense or
full task-5.3 acceptance. Updated host verifier recognizes pending-job completion
after visible PLAYING and passes 24 regression tests. Original reports and a
separate reevaluation are preserved under captures/task23-reset-*. See the active
FOCUSED-STOP-HITL.md checkpoint for exact build identity and limits. FIFO/reentry
implementation remains unfinished; no task checkbox closes.


**2026-09-16 pending-job reset protection / intermediate HITL checkpoint:**
An admitted stop job now retains its runtime staging and canonical-copy output
across cache reset. Reset retargets STOPPED and yields; Track invalidation preserves
the borrowed generation until terminal recovery. Native coverage exercises partial
copy and every stop phase, including held-note closure and receipt release.
A Tier-A prewarm record exposes output credits and pool/heap headroom. Verify
boot/load memory and single-job copy/Stop timing on hardware before expanding the
FIFO reservations: prior eager prewarm exhausted device PSRAM. This checkpoint is
not task-2.3 acceptance; the same three reentry fixtures remain red, and FIFO,
causal-source and preview/session/undo/snapshot migration remain unfinished.
No upload has been performed. Exact results and build identity are in the active
change's CONTINUOUS-CAPTURE-OWNERSHIP.md; the procedure is in FOCUSED-STOP-HITL.md.

**2026-09-16 task 2.3 worker-owner integration:** The existing stop worker now
uses a stopped-interval receipt for Loop/slot/phase/geometry and its own pending
notes. Closures target the saved owner; one-node retirement never clears live
notes, and completion cannot transition a newer Track interval. Production
empty, paired and held-note stops complete in native tests. Public A/B/C reentry
still fails: raw capture, staging, preview and undo/snapshot session migration
and the fixed FIFO remain open. Affected tests: 266 pass, the three known reentry
cases fail. Capture-serial builds with 5,600 bytes RAM1 local headroom; no upload.
Task 2.3 is not complete. See the
[ownership checkpoint](../../openspec/changes/resumable-midi-boundary-playback-maintenance/CONTINUOUS-CAPTURE-OWNERSHIP.md).



**2026-09-16 task 2.3: public-route reproducer and raw range foundation:**
The actual Button A → TrackManager → Track route and MidiHandler musical input
now run in a native fixture. First-interval capture/stop passes; A/B/C reentry,
manager same-pitch held-note reentry and direct Track reentry remain intentional
failing requirements. None of the three admission guards has been removed.

LoopEventStore now supports pre-reserved exclusive capture-range rotation,
read receipts, incremental release and stale rewrite/pool-lifetime rejection.
The focused range/persistence/event-store/playback-prewarm run passes 209 tests.
The full native run had 2,136 passes, one skip and only the three expected reentry
failures (before the final persistence-prewarm correction). This API
is not yet wired to Track; it does not fix immediate reentry. Job-owned staging,
closure credits, pending changes, undo/snapshot receipts, causal FIFO resolution,
cleanup isolation and device verification remain open. See the
[ownership checkpoint](../../openspec/changes/resumable-midi-boundary-playback-maintenance/CONTINUOUS-CAPTURE-OWNERSHIP.md).
Capture-serial build passes with 6,176 bytes RAM1 local-variable space after
placing the bounded rewrite maintenance worker in flash. Device timing remains
unverified. Task 2.3 stays unchecked; no firmware upload or archive was performed.

**2026-09-16 continuous preparation across punch-in/out:**
Canonical preparation now runs during PLAYING as well as OVERDUBBING, using the
existing VisualCache owner and otherwise-unused wrap-append turns. Existing
record, stop, wrap-append and source-span priority remains. A confirmed newest
pass append with unchanged companion history preserves completed normalized
passes and resumes the first unfinished/new pass; snapshot identity publication
at the same content revision also preserves completed normalization. Unproven
source changes or changed history retain the existing safe invalidation path.

The companion merge now retains final per-pass output, in its original order,
in the canonical owner's existing raw scratch buffer. One extra prewarmed pass
descriptor array is required (16 KiB at 2,048 entries on Teensy); no additional
event buffer or active allocation is introduced. Each existing merge addition
also writes its normalized scratch entry, within the same Q-entry grain.
Successful stop cleanup adopts these results with pointer swaps after matching
source/worker generations, including when the committed companions changed
history. Reselecting current descriptors/history is resumable; completed pass
normalization is not replayed. Selected bridge/base and audible cursor remain
untouched. A scratch-owner receipt prevents another builder from being mistaken
for the retained result. `prefix_reuse` / `prefix_restart` records describe the
low-frequency ownership transitions rather than logging every prework grain.

Task 2.3 and HITL acceptance stay open. This does not claim instant reentry:
frozen-tail/bridge/finalization work remains, and new history during a wrap may
invalidate normalization for all potentially affected passes. The provisional
6,240-grain baseline remains unchanged; added bounded stores need device timing
validation alongside rapid punch-in/out, wraps and original-Off preservation.



Continuous-preparation validation: full native suite 2,130 passed / one skipped;
final focused suite 194 passed, including superseded scratch-owner rejection.
Post-commit adopted per-pass events match fresh canonical replay across the
existing stop variants, including companion edits and the large-capture fixture.
Strict OpenSpec and diff checks pass. Capture-serial build succeeds with RAM1
local headroom 6,176 bytes, unchanged. Upload and one retry failed: the Teensy
loader reported no boards on USB and requested the physical program button;
no `cu.usbmodem` port was present. This firmware is built but NOT uploaded or
HITL-validated. Reconnect/program-button action is needed before retrying.

**2026-09-16 live canonical-base prework and stop estimate correction:**
The `003056` manual capture exposed a small-tail estimate incorrectly used as
candidate capacity. Stop now estimates from the prewarmed capture owner and the
prepared canonical base (or its conservative full capacity when unavailable).
The pass estimate includes record, existing overdubs and the incoming capture.
It no longer infers capacity from a rolling emission window or final tail count.

Live overdub and Stop share `stepCanonicalBase`: prefix normalization followed
by the existing pass merge and view fill/order worker, one child per owner turn.
The live VisualCache turn can prepare the immutable candidate bridge base while
recording the overdub. Paint dirtiness alone does not block this work while
visual reconstruction is disabled; wrap append and source-span work retain
priority. Source/storage changes invalidate prework; current playback and the
selected bridge remain untouched. Stop resumes partial work or reuses a ready
base. Frozen-tail, companion and cleanup work still remains after Stop, so this
is not proof of immediate reentry or a realtime deadline pass. Task 2.3 and the
HITL gates remain open; the provisional 6,240-grain baseline is unchanged.



Validation: full native suite 2,126 passed / one skipped; final focused suite
189 passed after the prefix-owner validation adjustment. Strict OpenSpec and
`git diff --check` pass. Capture-serial build and upload succeeded; serial
`/dev/cu.usbmodem154944801` returned. RAM1 local headroom remains 6,176 bytes.
The device upload finished at 00:44:27 local time on 2026-09-16. Manual reentry
and deadline acceptance remain open.

**2026-09-16 bridge boundary reconciliation implementation:**
`refreshFrozenCompanionBridge` now invokes the shared compiler's
`rebindBoundary` rather than resetting bridge storage. Records, mapping, base
ranks, sorted base order and complete insertion order survive. The existing
merge scratch retains the complete insertion permutation after sorting; steady
compaction no longer destroys the order needed by another boundary binding.
Reconciliation runs only inheritance, affected-record checks, closure resolution
and cycle-index output in the existing entry quantum. No allocation, copy of
canonical events, or sort is introduced during this path. Original carried Offs
are enabled only by a matching live closure and disappear after it closes.

Admission checks the relevant subset of the ledger using fixed pitch/identity
summaries built from all changed records and inherited closures. Identity hash
collisions conservatively request reconciliation; they cannot bypass protection.
Changes to unrelated notes no longer invalidate preparation. Changes to affected
notes, cycle or original-Off deadline retain the closure-only reconciliation
path; no note protection or command-epoch check is removed. These smaller steps
still require device service/reservation validation; no unconditional deadline
or maximum stop-latency claim is made.

Native coverage compares repeated bindings against fresh compilation over six
cycles, changes unrelated notes every preparation turn, verifies unchanged map
and bridge storage generations, and preserves an inherited original Off during
a second source change. Capture refresh telemetry now says `closure_only`.
The provisional 6,240-grain baseline remains unchanged. Full native validation
reports 2,124 passed and one skipped; final focused validation passes 153 tests.
Firmware build and upload retry succeeded with 6,176 bytes RAM1 local headroom;
serial returned. Task 2.3 and hardware acceptance remain open.


**2026-09-16 repeated bridge refresh blocks the third overdub:**
`captures/session_20260916_000904.log` confirms another overdub starts at
61.028 s. Its stop at 69.606 s remains pending. The retained trace contains
23 completed bridges immediately followed by refresh, from 72.194397 through
75.511650 s, at intervals of 144,372–158,456 us. Compilation captures a live
ledger/cycle/phase; admission checks exact ledger equality and the retained
Off horizon. Normal emission advances these while compilation runs, and
`refreshFrozenCompanionBridge` resets and recompiles the whole bridge. Repeated
ready/refresh is therefore not bounded progress toward admission. The first
stop finishes; the second stop does not, so later Button A presses correctly
retain the pending transaction. Ring overflow prevents a complete timing verdict.

Required correction: separate stable canonical mapping/ordering from bounded
current-boundary closure reconciliation, preserving progress under continuous
NoteOn/Off emission. Do not remove ledger validation, pause playback, wait for
silence, or increase the quantum as a substitute for a progress proof. Add a
moving-ledger regression with multiple consecutive source changes and verify
original Offs exactly once before treating another upload as a fix. This is
diagnosis and an implementation requirement, not implemented or device-verified.
Task 2.3 remains open.


**2026-09-16 post-finalization reentry starvation:**
`captures/session_20260916_000034.log` shows the populated bridge ready at
30.256907 s and phase-10 cleanup completed at 30.354501 s. Button A reaches
Live Overdub at 31.709, 35.371 and 37.227 s, then the combined transfer/snapshot
admission guard rejects it. Code inspection and a failing native reproducer
identify `serviceBridgeWindow` discarding a completed but unpublished future
window. It immediately rebuilds that window on subsequent turns, starving the
existing snapshot receipt consumer and leaving its admission slot occupied.
The Complete branch now releases the builder with `discard(&target)`, preserving
the ready/build destination. No snapshot admission policy is relaxed. The native
regression fails before this change and passes after, including repeated idle
yields, immutable future windows and the existing multi-cycle MIDI continuity
fixture (152 focused tests pass). Admission warnings now distinguish transfer
capacity from an occupied snapshot receipt. Full native validation: 2,123 passed, one skipped. Build/upload succeeded
with 6,176 bytes RAM1 local headroom; serial returned. Device reentry remains
unverified; task 2.3 and HITL/deadline gates remain open.


**2026-09-15 manual clean-stop routing regression:**
`captures/session_20260915_235602.log` records wrap commit at 48.314 s, Stop
Overdub at 48.428 s, and `bridge_begin` denial at device 48,438,934 us
(mask 1652 / 0x0674). Later Button A presses reach the finalization-pending
handler. The empty post-wrap candidate bypasses canonical/bridge preparation,
but the AtomicTransfer refresh guard incorrectly required a bridge for every
PLAYING destination and routed it back to CompanionBridge. The guard now
requires an actual pending capture pass. Empty stops proceed to existing
cleanup; populated PLAYING commits retain mandatory bridge preflight and
transport Stop retains precedence. Shared native routing coverage exercises
all three cases. The capture has ring overflow, so it cannot establish deadline
acceptance. Full native suite: 2,123 passed, one skipped. Firmware build/upload pass with
6,176 bytes RAM1 local headroom; serial port re-enumerated. Manual device
confirmation and task 2.3 remain open.


**2026-09-15 production closure ownership and rolling-window integration:**
The stop producer now builds its complete immutable canonical bridge base in
one of two external-memory source slots paired with selected/candidate bridges.
These bases are separate from the three rolling emission windows and survive
canonical/history worker cleanup. This removes the manual `partial_open_ledger`
trigger from production without weakening that guard for an unproved partial
base. There is no short/long-loop threshold and no new event-count admission test.

`RuntimePlaybackBridgeStream::source()` exposes the same compiled cycle cursor
to the existing coverage fill/order worker. `serviceBridgeWindow` schedules one
setup/fill/order/promotion turn and uses the existing three-view lifecycle.
Ordinary PLAYING consumes matching cycle-qualified windows. During source handoff
or delayed refill it continues the already compiled bridge cursor, with bounded
index acknowledgement to prevent replay when switching representations. This
compatibility path does not gather, sort or wait for a window. It remains until
reservation/HITL gates permit removal under task 5.4.

Admission uses the latest serviced phase and cycle. A changed live ledger or a
passed original-Off boundary refreshes only bridge compilation from the completed
mapping; canonical preparation remains intact. A second source change inherits
still-live original closure obligations from the selected bridge through the
existing inheritance/copy/mask phases. Its extra copy/sort estimate is separate
from the unchanged provisional 6,240-grain baseline, never a stop-admission ceiling.
Two source bases add 192,512 accounted fixed external bytes per fully prewarmed
runtime on the native layout; allocation remains in prewarm. Runtime reset is
placed in the existing cold flash section to avoid crossing an ITCM bank.

Native coverage now includes the production base producer, cleanup survival,
live-ledger refresh, rejection at an already serviced Off boundary, delayed window
refill with exact MIDI continuity, and an additional source edit before the
original Off. Device reentry and timing are not yet verified for this integration.
Validation: full native suite reports 2,123 passed and one skipped; the capture
firmware build passes with 6,176 bytes RAM1 local headroom. Logs are
`/tmp/finish-closure-all-native-final.log` and
`/tmp/finish-closure-firmware-final.log`. Upload completed successfully (`/tmp/finish-closure-upload.log`), but the
post-upload check found no `usbmodem` port; boot/runtime remain unconfirmed.
Task 2.3 and the full HITL/reservation gates remain open.


**Earlier checkpoint, superseded by the production integration above: initial-window closure inheritance foundation:**
`RuntimePlaybackBridgeBuild::beginInitialWindow` projects a completed immutable
closure context into an initial canonical window using the existing successor
Inherit/CarryOffs/cycle-mask phases. The context is preparation-only and need not
be audibly selected. It is generation-bound until compilation completes; the
initial ledger and cycle/phase must match. No new allocation or independent
closure algorithm is introduced. The native regression starts with an Off outside
the first window, crosses two successors, and checks original Off exactly once
then revised steady playback, changed-ledger denial and retired-context denial.
All 149 native prewarm tests pass; strict OpenSpec validation passes.

Production integration is NOT complete: no production caller currently invokes
`beginSuccessor`, and the active consumer still resolves full-loop views. A durable
initial closure-context producer, source lifetime through cleanup, current-phase
admission and shared runtime successor scheduling/handoff must be connected before
removing `partial_open_ledger`. The guard remains; this patch is not uploaded and
must not be presented as fixing the manual reentry failure. Task 2.3 remains open.


**2026-09-15 post-upload manual reentry: bridge closure guard confirmed:**
`captures/manual-reentry-after-fix-status-20260915.log` retains phase-8
CompanionBridge grains for track 5/slot 1, generation 7, PLAYING destination.
At device time 27,654,516 us, `bridge_compile` reports `partial_open_ledger`,
ledger count 10, coverage length 768 ticks. `RuntimePlaybackBridgeBuild::begin`
explicitly rejects a partial canonical window with a nonempty playback ledger
because closure obligations beyond that window are not yet owned across rotation.
Thus this run progresses beyond CompanionHistory but still cannot finish normal
stop or admit the next overdub. This is playback ledger state, not evidence of
held physical input notes. At retrieval the device was already STOPPED with no
pending stop and matching history generations (7); no transport command or Clear
was sent for retrieval. Ring overflow was reported, so only retained evidence is
claimed. Next implementation must carry closure obligations through shared rolling
coverage, not remove the guard or wait for silence. Manual reentry and task 2.3
remain open. The preceding fix was uploaded successfully before this retest.


**2026-09-15 companion-history begin binding fix (partial task 2.3):**
`beginPreparedCompanionHistory` now validates existing reserved row/ID storage
against current history and binds its storage generation/history count at begin.
A wrap's `saveNoteEditPass` publication can therefore advance canonical history
between prewarm and stop without permanently rejecting unused preparation
storage. In-flight generation checks remain strict; no worker is silently
restarted and no canonical-only publication bypass is restored. Setup remains
constant-time and does not resize/reserve containers.

The native regression failed at begin before the fix. It now verifies wrap
metadata retention, stable reserved buffers, second preparation after recycling,
capacity rejection and rejection of a canonical mutation after begin. All 148
prewarm tests pass. Capture firmware builds with 6,176 bytes RAM1 headroom.
This fixes a reproduced code path consistent with the manual phase-7 stall;
the original overflowed trace does not prove its exact initial invalidation.
Firmware has not been uploaded for this fix. Repeat the manual populated-wrap
DROID test before claiming device reentry is fixed; task 2.3 remains open.


**2026-09-15 manual DROID reentry regression remains open:**
`captures/manual-overdub-reentry-diagnostic-20260915.log` records first overdub
start at 155.001 s, a populated wrap at 157.426 s, stop request at 161.360 s,
and Button A at 163.858 s rejected with "Overdub finalization continues in
playback". Later retained grains stall in phase 7 (CompanionHistory), including
after transport Stop. This differs from the prior phase-8 bridge stall and
supersedes any inference that automated two-pass success proves manual reentry.
The user subsequently issued Stop/restart and long-press Clear; the read-only
status capture (`captures/manual-overdub-reentry-status-20260915.log`) therefore
cannot establish the original invalidation: it shows pending phase 7, prewarmed
history storage generation 5 versus current 9, and invalid canonical inputs.
Investigate prewarmed history invalidation across populated wrap publication;
do not bypass joint publication or simply relabel the visible state. The capture
ring overflowed, so missing early worker diagnostics are not evidence of success.
Task 2.3 and HITL acceptance remain open.

**2026-09-15 running-cycle bridge anchors (partial task 2.3):** Track's window,
bridge and atomic anchors now use `LoopPlaybackRuntime::currentCycleCursor`
instead of cycle zero. Serviced ordinary/overdub wraps advance that same runtime
counter when no bridge stream is selected. This prevents a newly published
bridge from beginning with next/steady-cycle masks because the runtime has
already advanced. Native closure regression runs at initial cycles 0 and 3,
checking the original cross-wrap Off emits once and retires; 147 tests pass.
Host probe/transition tests: 27 pass. The probe can send a late note in each pass
to exercise populated stop tails after a longer overdub interval.

Build/upload passed with 6,176 bytes RAM1 headroom (ITCM code 425,784 bytes).
Transport projection reanchoring now uses the existing cold flash section.
`captures/stop-cycle-wrapped-late-two-pass-20260915/result.json` passes: two
eight-second overdubs with late notes, two normal cleanups, no atomic-preflight
denials, final transport stopped. Service/emission row counts remain zero.
The phase component still uses the frozen stop phase. Current serviced-phase
admission, rolling coverage, protected undo, bounded cleanup and deadline proof
remain open; this is not completion of task 2.3 or a real-time reservation.

**2026-09-15 mandatory joint publication and Add-only revision fixed (task 2.3):**
the caller formerly fell through from a failed joint canonical/bridge preflight
to a canonical-only PLAYING transfer. Atomic dispatch now lives in the existing
`TrackPreparedStopCommit` helper, and a populated PLAYING denial leaves the
candidate pending. This exposed a target-revision error: pending Add changes
were counted as companion edits, preparing +2 while canonical commit uses +1.
The reserved tail now derives that revision from the existing bounded row
worker's completed count before preparing derived passes. No new input scan.

The production-dispatch/Add-only native regression passes with all 147 prewarm
tests. Build/upload passed with 6,176 bytes RAM1 headroom. The two-populated-pass
checkpoint `captures/stop-joint-revision-two-pass-20260915/result.json` passes:
two normal cleanup completions, no atomic-preflight diagnostic denials, no probe
errors, final transport stopped. Earlier cleanup-only passes did not establish
joint publication because the fallback was still reachable. Traces still lack
service/emission correlation; no MIDI deadline acceptance is claimed.
Next task 2.3 work: replace the stop-time handoff anchor with the continuously
serviced cycle/phase cursor and integrate rolling coverage without delayed launch;
protected undo and bounded cleanup remain open as well.

**2026-09-15 second-stop window ownership fixed (partial task 2.3):** the
selected bridge's canonical base remained in the ready slot after publication,
and cleanup left its completed window receipt attached. The next prefix could
finish, but its window request denied against that old receipt. A native second
window reproducer failed before the correction and passes now. Bridge publication
uses the existing preflighted ready-to-current lifecycle handoff; cleanup discards
the window receipt while preserving the now-current audible base. The infrequent
lifecycle handoff uses the existing flash-placement macro, avoiding a new ITCM
bank and preserving 6,176 bytes RAM1 local headroom.

Uploaded two-populated-overdub checkpoint
`captures/stop-window-handoff-two-pass-20260915/result.json` passes the strengthened
gate: two normal cleanup completions before final transport Stop, no probe errors.
The cleanup grains took 287/299 µs; complete stop-to-cleanup intervals were
240,823/244,832 µs. Neither duration is an emission-deadline verdict: this trace
has zero service/emission rows. Native playback-prewarm and take-survival suites
pass 147 + 124 tests. The failure below is superseded by this checkpoint.
Task 2.3 remains open for full current-phase/rolling handoff, protected undo and
bounded cleanup integration; task 4.1/5.3 real-time gates remain open.

**2026-09-15 normal-stop cleanup continuation (partial task 2.3):** removed
normal-stop silence, clearing of every playback ledger and cursor reanchoring.
The existing `TrackPreparedStopCommit` owner now performs terminal canonical
bookkeeping, retaining the selected audible bridge and retiring both completed
bridge and frozen-tail workers. Native tests exercise snapshot-handoff retry,
held-note Off emission after cleanup and next-prefix preparation using that
production body. Playback-prewarm: 147 passed; host probe/transition: 27 passed.
Capture build/upload passed with 6,176 bytes RAM1 headroom.

The tightened two-pass HITL is **not a pass**. In
`captures/normal-stop-cleanup-tail-two-pass-20260915/result.json`, both visible
OVERDUB_STOPPING→PLAYING edges occur, but only the first normal cleanup completes
(224 µs cleanup grain; this is not total stop latency). The second stays in
CanonicalPrefix through the one-second observation and completes only after
transport Stop retargets it. The prior state-only probe incorrectly passed the
same pattern in `captures/normal-stop-cleanup-two-pass-20260915/`; the probe now
requires completed PLAYING-destination cleanup for every pass. There are no
service/emission rows in these traces, so no audible/deadline verdict is possible.
Next: distinguish the second prefix builder's denial from canonical-window
readiness/ownership blocking, then rerun the same checkpoint before advancing
rolling current-phase handoff integration. Task 2.3 and all wider gates stay open.

**2026-09-15 successor-window closure integration (partial task 2.3):** the
shared bridge compiler now carries canonical row ranks and effective-cycle masks
into a contiguous successor window. An inherited held-note Off is copied in
bounded work, emits only in the window/cycle containing its due phase, and is
removed from steady insertion playback. The compiler rejects a retired or
changed predecessor before consuming more input. The existing 6,240-grain
reference estimate is preserved.

Validation was last run before the host usage limit blocked a fresh invocation;
the prior focused run passed 270 cases and the capture build retained 6,176
bytes of RAM1 local headroom. `git diff --check` and strict OpenSpec validation
pass. Production Track selection/refill and device HITL remain open; do not
upload this unverified successor integration.

**2026-09-15 successor closure integration (partial tasks 2.3–2.4):** the
existing RuntimePlaybackBridgeBuild now has a same-source successor entry point.
It checks contiguous coverage/cycle boundaries and a stable selected predecessor,
then inherits owned closures in bounded grains. A copied Off is inserted only in
the window containing its original due phase and only in its original cycle.
Steady insertion ordering compacts already-read indexes so closure-only Offs cannot
repeat in later loops; the existing MIDI cursor consumes the resulting streams.

Typed shared view fills now retain each row's complete canonical merge rank.
Compiled bridges retain canonical cycle masks across the complete rank horizon,
so later windows inherit gating decisions without reconstructing them from NoteIds.
Masks shift at cycle advance and survive windows containing no events. Replaced
old base rows cannot overwrite the effective canonical row's mask. Both bridge and
view buffer generations protect builders and cursors against reuse. All new slabs
are prewarmed; the existing 6,240-grain reference-case estimate remains unchanged.

Native prewarm/take-survival: 270 passed. Fixtures verify original Off emission,
no repeated closure in steady cycles, carry through two windows at quanta 1/32,
shortening the revised Off before the old closure, stale predecessor/source denial,
and canonical ranks surviving source-worker reuse. Capture firmware build passed:
RAM1 local headroom remains 6,176 bytes. This is shared compiler/cursor integration,
not completed production Track dispatch or a device normal-stop verdict.

Still required before enabling rolling Track selection: initial partial-window
closure context for already-open notes (its guard remains); coverage-qualified
cursor consumption across wrapped windows; atomic current-phase view/bridge handoff;
ordinary successor refill; and normal-stop cleanup without silence/reanchor.
No upload, task checkbox closure or HITL acceptance is claimed.

**2026-09-15 bridge-owned held-note closure state (partial task 2.3):** the
existing bridge compiler now stores its affected-open-note flags, closure time
and next-On phase in the bridge's fixed ledger-sized storage. When the existing
closure walk finds the original Off, it also copies that event into the bridge.
The copied closure survives compiler discard and base-window reuse. The old
base index remains only for the current compiled stream; it is not a transferable
closure receipt. No alternate pairing or closure-resolution algorithm was added.
Bridge reset retires closure metadata by count without clearing every payload;
compilation rejects a changed ledger count. Unresolved or incomplete receipts
are not exposed by the closure accessor.

Native prewarm/take-survival: 266 passed, including the unchanged cross-wrap MIDI
trace, all 128 ledger slots, base reuse, bridge reuse and changed-ledger rejection.
Capture build passes with 6,176 bytes RAM1 local headroom. Successor inheritance,
closures outside the initial window, current-phase selection and rolling playback
consumption remain required; the partial-window held-note guard stays in place.
This is ownership preparation, not completed cross-window playback or a normal-stop
fix. No upload or HITL acceptance; task 2.3 remains open.

**2026-09-15 ready-window bridge integration (partial task 2.3):** frozen
companion bridge preparation now borrows the exact prepared canonical ready
window, falling back to the existing full current base when needed. Mapping and
compilation no longer re-resolve through the audible-current index. Admission
checks that the preparation anchor lies inside this base coverage. Retirement,
provenance loss and coverage changes invalidate the borrowed base, including
completed candidates. Physical views now advance a storage generation on reuse;
mappers, bridge compilation, selection and stream cursors reject stale indexes
even when source identity and coverage repeat. The stopped diagnostic identifies
`prepared_window` separately from a missing canonical base.

The integrated native fixture covers prefix -> ready window -> frozen tail ->
bridge compilation -> the existing read-only MIDI cursor, with nonwrapped,
wrapped and empty coverage and equal-phase Off-before-On output. It verifies that
retiring the unrelated old current does not invalidate the ready base, while
retiring/reusing the borrowed base does. Audible Track selection remains guarded:
held-note closure carry, current-phase admission and following-window playback
are not complete, so this is not a normal-stop or HITL pass. No firmware upload.
Native prewarm/take-survival: 265 passed; capture firmware build passed with
6,176 bytes RAM1 local headroom. Strict OpenSpec validation and diff checks pass.

**2026-09-15 stop-owner canonical window preparation (partial task 2.3):**
CanonicalPrefix now prepares a one-bar canonical base when the current view has
no usable full canonical provenance. It merges the normalized prefix through the
existing RuntimeCompanionPasses worker, fills/orders a spare view through the shared
RuntimePlaybackViewBuild, and publishes an immutable ready view without changing
audible current. Fill, order and publication resume on separate turns. The pass
worker is released before frozen-tail processing; copied origins survive its reuse.
A busy shared builder stays pending, and source/storage changes cancel only the
owned preparation. Both initial-ready and post-record no-ready lifecycle shapes
are covered. No new event buffers or active allocations were introduced.

Native prewarm/take-survival: 265 passed. Capture build passes with 6,176 bytes of
RAM1 local headroom. This does not fix normal-stop liveness yet: the bridge selector
and PLAYING consumer still require the full-loop compatibility base. Current-phase
admission and held-note closure carry through subsequent window rotation remain
required before enabling the new ready base. No upload, HITL verdict or task closure.

**2026-09-15 canonical window implementation (partial task 2.3):** the existing
RuntimePlaybackViewFill and RuntimePlaybackViewBuild now accept typed canonical
coverage-scoped sources, copy events and pass provenance together, and keep fill
and order on separate bounded turns. Whole-source length need not fit the view;
only retained rows consume destination capacity. The existing companion bridge
mapper now filters the prepared per-pass result to that same coverage and checks
the borrowed base's coverage, publication, storage and provenance receipt on every
turn. The bridge compiler accepts a matching partial coverage with an empty ledger;
open-note obligations remain denied until carried across subsequent window rotation.
No raw/projected source is silently accepted as canonical provenance.

Native verification covers nonwrapped, wrapped and empty windows, one-entry quanta,
source larger than destination, provenance surviving worker reuse, per-pass MIDI
parity, changed coverage and unsupported held-note closure rejection. Final focused
prewarm suite: 138 passed; edit suite: 124 passed. Capture firmware build passed.
This is shared-worker implementation, NOT completed Track integration: stop-owner
base-window preparation, current-phase admission, closure-carrying rotation and the
ordinary PLAYING consumer must be connected together before enabling normal stop.
The new pieces were not uploaded; the device retains the verified display fix and
transport-Stop recovery. Normal-stop device verification and task 2.3 stay open.

**2026-09-15 companion-bridge checkpoint:** user confirmed first-record display
responsiveness after the background-cache dispatch correction. The remaining
normal-stop stall is now explicitly attributed on device:
`ODUB,stop,bridge_base,...,missing_full_loop_base`. A first recording retains a
one-bar raw playback view; the compatibility bridge mapper, selection preflight,
and hot-path consumer still require a full-loop view with canonical provenance.
Do not fix this by synchronously materializing a full-loop base or weakening
provenance/coverage checks. Task 2.3 must integrate coverage-aware canonical
successor preparation with the existing window lifecycle and task 2.4 consumer,
including current-phase cursor/ledger and subsequent-window handoffs.

Transport-Stop recovery from CompanionBridge is repaired: completed history skips
audible bridge work and calls the existing stopped canonical/companion commit
body, shared with its undo-aware entry point. Frozen-command/owner checks and
history cleanup remain enforced. Native stop/prewarm and take-survival suites:
259 tests passed, including no-published-base recovery and stale-command rejection.
Capture build/upload passed. Device `captures/bridge-stopped-recovery-20260915/`
shows OVERDUB_STOPPING -> STOPPED, retained correlated state/grain evidence, no
trace drops and no ring overflow. The normal-stop verifier still fails, correctly;
this is recovery evidence, not normal-stop, silence-matrix, or realtime acceptance.
The device was left STOPPED. No task checkbox closes.

**2026-09-15 first-record display regression:** user confirmed the screen now
continues updating in OVERDUB_STOPPING, but reported multi-second frame gaps during
the first two recording bars. A recording-only mode in the existing stop probe
isolates that interval from the stalled stop trace flood. Before the scheduler
fix, stopped background tracks rebuilt 56/353/1429-row display caches during the
recording owner turns: idle maintenance reached 281,346 us, a sampled display frame
538,356 us, and the window MIDI-service gap 281,883 us. The realtime dispatcher now
services realtime tracks only; pending stopped-track wrap work cannot block the
active display. The after-run sampled frames were 14,260–15,942 us, with no reported
long idle-maintenance span. Both captures are overflow-free. The after window still
has a 180,003 us MIDI-service maximum around recording entry; this is not deadline
acceptance. Evidence: `captures/record-only-background-fix-20260915/comparison.json`
and both linked session logs. Capture build/upload succeeded; six existing host
probe tests pass. The recording sequence completed and transport ended STOPPED.
CompanionBridge stop liveness remains open; no OpenSpec acceptance gate closes.
A stopped-only `!TRACE_DRAIN` diagnostic retrieves retained logs on explicit request.

**2026-09-15 stop/display stall investigation:** device `!TRACE_FENCE` answered
while the interface appeared frozen: main-loop liveness was retained. A stopped-only
`!STOP_STATUS` query now exposes the selected track's stop and history worker phases.
`captures/normal-stop-status-20260915/stopped-status.log` proves a stall in
CompanionHistory, with canonical tail complete. Recording start prewarmed resources
before resetting their canonical generation; the reset now precedes prewarm, and
snapshot cleanup retains the reserved companion-ID destination. The next device
report (`captures/normal-stop-resources-fix-20260915/stopped-status.log`) proves
matching generations but history Trim parked with zero canonical estimates.
Preparation/recycle counters now saturate as estimates rather than denying further
valid worker turns. Completed workers retire unused estimates without padding turns.
The display owner reuses its sliced paint continuation during OVERDUB_STOPPING.
Native prewarm/edit suites passed 259 tests after resource fixes; final continuation
prewarm/survival suites passed 259 tests (edit suite also passed 124). Capture firmware
built/uploaded. The next device run advanced through history to CompanionBridge
(phase 8), with history Ready and bridge Idle; normal stop still fails. Evidence:
`captures/normal-stop-continuous-display-20260915/stopped-status.log`. The current
full-loop bridge requirement and transport-stop retarget need further investigation.
The user-observed rerun also failed normal-stop verification:
`captures/normal-stop-display-user-verify-20260915/result.json`. Transport Stop was
sent for cleanup; completion was not established. Screen responsiveness still needs
the user observation. No task 2.3, timing reservation, or HITL acceptance gate closes.

**Current DRY direction:** first recording reuses the existing playback view,
fill/order, coverage lifecycle, bridges and MIDI cursor. The separate recording
window/store/stream prototype was removed. Shared coverage-scoped fill and retained return coverage are tested
(129 focused tests); Track source/lifecycle integration and original-boundary readiness
remain open. Raw coverage and preparation-only rotation now use the shared
workers; pre-roll now receives the new recording origin before ingestion.
Earlier prototype test counts are historical, not acceptance of the
shared recording path. No delayed launch or new recording rejection is authorized.

**2026-09-15 shared first-record refresh:** record entry installs an empty raw
bar-1 view through the normal playback lifecycle before capture begins. The
same fill/order workers can borrow the chunked `capture.store` through a
generation-checked source descriptor; one bounded grain refreshes the retained
window during recording, and an append invalidates an in-flight build for a
clean restart. Record stop binds final geometry before the existing audible
handoff, while cache reset preserves the retained raw view. Focused native
coverage/prewarm tests pass 132/132 and both Teensy builds pass. Deferred
maintenance is routed to FLASH to preserve the flexRAM bank boundary; the
capture-serial image now has 6,496 bytes of RAM1 local headroom. Device HITL
and the remaining parity/admission tasks are still open.

The recovered capture image was uploaded for track 5 / slot 1 HITL. The
transport-Stop interruption verifier passed focused precedence and silence
checks; the normal-stop rerun did not confirm the overdub-stop transition and
remains inconclusive. No HITL acceptance gate is closed.


**2026-09-15 first-record direction corrected by user:** no delayed launch is
acceptable. Prepare rolling one-bar playback windows during recording, with
bar 1 established at record start and retained for the existing final-bar return
boundary. Extend the currently overdub-only staged input path; prove late input,
held-note closure, final-length binding, and subsequent window handoffs without
rewinding transport or replaying historical events. The verified first-record
snapshot delay/catch-up defect remains unfixed. The deferred-launch proposal is
rejected history. See the active `resumable-midi-boundary-playback-maintenance`
design and specs; task and HITL gates remain open.


**2026-09-15 device-clock stop timeline ready:** capture-only telemetry now
records every `OVERDUB_STOPPING` grain with frozen identity, requested
destination, preceding actual MIDI service/due/deadline and exact grain span.
It queues following service and NoteOn/Off emission records from the timing
path for safe main-loop capture, and reports any trace loss. The host report
rejects malformed, dropped, or uncorrelated timeline evidence. Native focused
telemetry/tier: 43 passed; host timing/stop tests: 40 passed. Capture build:
flash code 947,864; RAM1 variables 91,808; RAM1 code 425,544; locals 6,496;
EXTRAM 2,240 bytes. This prepares the next focused normal-stop and transport
Stop HITL; Task 4.1 remains open for the non-stop ownership/result fields.

**2026-09-15 forced wrap Stop PASS:** track 5/slot 1 reached
`OVERDUB_STOPPING`; MIDI Stop arrived 8.689 ms later. The prior replay during
CC123 fan-out was repaired with an ISR-visible per-track output barrier. All
Notes Off followed in 83 us, STOPPED in 173.607 ms, and the 18.9 s observation
contained no target-channel NoteOn. Evidence:
`captures/resumable-handoff-wrap-forced4-barrier-hitl/`. Native focused suites:
242 passed; host fixture tests: 38 passed; capture build RAM1 locals: 6,496
bytes. Task 2.3 remains open for its complete bridge/publication/parity scope;
layered and deadline HITL gates remain open. See `FOCUSED-STOP-HITL.md`.

**2026-09-14 restart omission repaired and HITL PASS:** the prepared-stop path
now hands prewarmed session identities to the idle snapshot owner before close;
the stale-root overlay reads that receipt immediately. Companion publication
updates both note and MIDI-event membership. Three complete restart cycles each
played all 49 expected notes exactly once with matching NoteOffs, after a focused
Stop-interruption PASS. Evidence: `captures/resumable-handoff-admission-hitl/`
and `captures/resumable-handoff-restart-hitl/`. Firmware uploaded; transport stopped.
Native full suite 2,083 passed/1 skipped, final focused suite 124 passed, host tests
38 passed; RAM1 locals 6,496 bytes. Tasks remain 11/19. An earlier wrap-adjacent
run took legacy stop and is inconclusive; reproduce that admission fallback next
with the new capture-only denial diagnostics. Protected cleanup/undo, fully
bounded publisher/overlay work, layered parity and timing acceptance remain open.
See `FOCUSED-STOP-HITL.md` for scope and separate baseline verdicts.

**2026-09-14 restart survival FAIL:** after the focused interruption PASS, three
complete restart cycles emitted only base pitches 48–79; overdub pitches 24–39
and held 83 were absent. Canonical/display evidence still reports 49 notes while
playback's overlay has 32. Prioritize task 2.3's reserved bounded session-exit
snapshot handoff before clearing session identity; the prepared-stop production
path currently omits it. Do not insert the unbounded legacy queue call into an
active grain. Layered HITL is deferred until single-slot survival passes.
Evidence: `captures/resumable-stop-restart-survival/restart-result.json` and
`FOCUSED-STOP-HITL.md`. Transport stopped; firmware unchanged; tasks 11/19.

**2026-09-14 focused interruption PASS:** repaired concrete-output-port fixture
and stale-PLAYING skip; actual MIDI Stop observed 9.253 ms into OVERDUB_STOPPING,
All Notes Off after 70 us, then STOPPED and 9.49 s without resumed target notes.
Evidence: `captures/resumable-stop-hitl-strict-wrapper/interruption-result.json`.
This closes only the single-track interruption/silence checkpoint. Restart
survival, layered playback, broader baseline and full timing acceptance remain
open. Tasks remain 11/19; see `FOCUSED-STOP-HITL.md` for scope and next gates.

**2026-09-14 MIDI deadline correlation checkpoint:** uploaded capture-only retained
send/due/deadline timestamps and per-stop-phase summaries. Native 2,080 passed,
1 skipped. A normal stopping interval measured 97.760 ms; one canonical-order
turn crossed its entry service deadline (248 us duration, 33 us slack). Another
run retained 24.678 ms clock and 26.049 ms NoteOff lateness. This is partial
hardware evidence, not deadline acceptance. Scratch clears succeeded with the
revised host precondition; the timed interruption fixture was subsequently repaired and its focused
checkpoint passed (see the newer entry above). See `FOCUSED-STOP-HITL.md`; tasks remain 11/19 complete.

**2026-09-14 protected stop continuation:** bounded undo publication/restore,
STOPPED-only joint recovery, disabled-pass reclamation and session-close helpers
now have native coverage. Production playback honors the latched STOPPED
emission policy; full joint stop integration remains open (11/19 tasks).
See the change's `BRIDGE-PROGRESS.md` and `FOCUSED-STOP-HITL.md` for the focused
Stop regression checkpoint and explicit remaining integration. Upload and a focused HITL attempt were approved and performed on 2026-09-14.
The initial boot exhaustion is fixed: boot now reserves staging only; full
bridge readiness remains separately guarded. Device startup and normal UI are
confirmed, with 4,401 KB free PSRAM. Native: 2,077 passed, 1 skipped. The exact
managed-log binding is fixed. Normal stopping was observed; the interruption
retest remains INCONCLUSIVE_CAPTURE after a deferred-save clear refusal. See
`FOCUSED-STOP-HITL.md` for evidence and remaining gates.

**Highest operational priority.** Defines what to implement **now**. Load with [PROJECT_STATE.md](PROJECT_STATE.md) before planning or coding.

Last updated: 2026-09-12 (§6.6.5 GAP-8b reverted; §6.6.7 stage 7 in progress)

Authority hygiene: execution bound class is a gated property — [ExecutionBounds.md](../Authority/Architecture/ExecutionBounds.md), [DEC-043](../DECISION_LOG.md#dec-043-execution-bound-class-is-a-gated-architectural-property). Architecture-checkpoint question 3 is a hard stop for new unbounded interval work. No interval reservation / `RuntimeWorkBudget::admit`.

---

### response-driven-hitl-acceptance task 3.3 — sealed report bundles, device-free replay, observation-wait facade (2026-09-30)

`hitl/reporting.py` now owns the **sealed bundle** next to the progress report: `BUNDLE_SCHEMA_VERSION = 2`, a fixed
section list, a sha256 digest, and a verdict derived from the bundle's own evidence by one rule (`derive_verdict`) the
reader re-runs — every predicate ok **and** a teardown result present and ok, with a caller's success claim required to
be backed by both. `write_report_bundle` seals once (atomic rename, refuses to overwrite); `run_metadata.run_identity` is
the one identity builder behind `metadata.json` and the `identity` section; `scripts/hitl_report_replay.py --bundle …`
re-derives the verdict with no MIDI or serial port (0/2/3: pass / intact non-pass / not trustworthy) and states its own
limit (a digest is integrity, not a signature). The oracle seals the acceptance verdict via
`accepted_sequence_probe.py --emit-bundle [--run-report]` (checks → predicates, `UNKNOWN` as not-ok, the run's shutdown
carried in). The observation wait is one owner now — `hitl/actions/observation.py::await_markers`, which both drivers
wrap — and it sits under `hitl.actions`, so the **import gate was resolved without widening it** (pinned by the gate
suite). Host-only: no firmware, no build, no device action. Evidence: new `test_hitl_report_bundle` 14/14,
`test_hitl_observation_wait` 7/7, `test_hitl_import_gate` 5/5, `test_accepted_sequence` 97/97, `test_run_metadata` 6/6,
neighbouring suites green together, six mutations red-proved and restored byte-identically. Follow-up (not this task):
the layered runner and `--verify-only` still write their own progress report, and the pre-existing
`test_foundation_runner`/`test_layered_verifiers` failures (a missing `overdub_step_clocks` attribute in the legacy
bridge's verify path) are unrelated and unchanged. See
[REPORT-BUNDLE-EXECUTION.md](../../openspec/changes/response-driven-hitl-acceptance/REPORT-BUNDLE-EXECUTION.md).

## Now implementing

### response-driven-hitl-acceptance task 3.2 — bounded cancellation (2026-09-30)

An abandoned managed run used to keep pressing while the capture child was being released. `hitl/actions/cancellation.py`
now owns the three properties — a `GuardedMidiOut` that refuses every test edge once the run is cancelled (and raises
`RunCancelled`), releases the notes it was holding (per note, plus a channel silence per touched channel), and permits the
teardown in a counted phase (`teardown_call`) so the shutdown's transport stop still goes out; a registered run scope
(`begin_run`/`active_cancel_scope`/`cancel_active_run`) reachable by the watchdog; and `run()`, the outer context a driver
adopts around its `DeviceSession` (absorb the cancel, release, record, unregister). Waits and sleeps are cancel-aware
(`check`, `cancellable_sleep`, `wait_for(cancel=…)`). `managed_capture._run_with_watchdog` stays cancelled-first: cancel,
bounded grace (`HITL_CANCEL_GRACE_S`), then return — the capture is released after that. Both drivers register the run, open
through the guard, and report `CANCELLED` + `cancel_reason` + the guard's `midi` record in the same partial report they
write on any other exit. Host-only: no firmware, no build, no device action. Evidence: new `test_hitl_cancellation` 21/21,
`test_managed_capture` 13/13, `test_accepted_sequence` 93/93, `test_retention_handoff_plan` 33/33, **405 tests / 15 suites**
altogether, seven mutations red-proved and restored byte-identically. Open in this section: 3.3 (managed metadata path,
immutable versioned report schema, verify-only replay, the narrow observation-wait facade). See
[CANCELLATION-EXECUTION.md](../../openspec/changes/response-driven-hitl-acceptance/CANCELLATION-EXECUTION.md).

### response-driven-hitl-acceptance task 3.1 — correlated gesture stages in the action layer (2026-09-30)

The reusable action layer now names what it sent and waits for the device's own answer, instead of each driver
re-deriving both inline. `scripts/hitl/actions/correlation.py` owns SEND/RECOGNIZE/ACCEPT/EXECUTE (`Stage`,
`CorrelatedGesture`, `StageRules`, `Wire`, `PendingSwitchGate`): only the next pending stage advances, so an
execute-only row cannot satisfy recognition or acceptance; every acceptable row must carry the identity **its own
wire family** uses (`Loop 4:` vs `DISP,3,`), so a foreign slot cannot substitute; a row stamped before the device
clock observed at the press is drained history (`hitl.device_time`, reused not re-derived); a row that answers
differently is `contradicted` while a missing one stays a gap (the acceptance oracle's own split); and
`now`/`sleep` are injected, so every bounded wait is testable without a device. The press helpers in
`hitl/actions/{buttons,slots}.py` record SEND themselves (`correlate=` / `before_device_us=`, classification names
from `MidiButtonManager`), so no flow has to declare its gesture; existing callers stay fire-and-forget. The
pending-switch clause is `PendingSwitchGate` over the firmware's own rows, with attribution refusing an
unattributed `Playback started`. Host-only: no firmware, no build, no device action. **The two device drivers now
consume it** and task 3.1 is closed: `hitl_accepted_sequence.py`'s `_await_slot_disp` is a `CorrelatedGesture`
over the caller's vocabulary (with `_slot_disp_rows` reduced to a pure row source, `_slot_ready_state` applying
the floor as the state read it is, and `_switch_pending` taking its rule from the gate while keeping the leg's
window and abort policy), and `hitl_retention_handoff.py`'s seed waits became `_await_seed_receipt`, whose
`recognize` stage is the device's own `Button press: Loop N (…)` row, whose `accept` stage no longer takes the
slot-less `ST,Track` family, and whose failure message names the blocking stage. Evidence: new
`test_hitl_correlated_actions` **23/23**, `test_retention_handoff_plan` **30/30**, `test_accepted_sequence`
**91/91** (corpus **3/3** unchanged), `test_hitl_device_time` **16/16**, neighbouring suites together
**244/244** across 9 suites, twelve mutations red-proved and restored byte-identically. Open in this section:
3.2 (cancellation) and 3.3 (watchdog, versioned report schema, verify-only replay, the narrow observation-wait
facade). See
[CORRELATED-ACTIONS-EXECUTION.md](../../openspec/changes/response-driven-hitl-acceptance/CORRELATED-ACTIONS-EXECUTION.md).

### S4f.17–S4f.21 — connected shared capture finalization, native-first (2026-09-29)

Continue the runtime OpenSpec implementation in dependency order. **S4f.17 — common interval and stop admission** is the production admission/bind and caller-migration track; **S4f.18 — bounded Record preparation** is the preparation/publication worker; **S4f.19 — first-bar and onward playback handoff** is the playback continuity track; **S4f.20 — atomic publication and retirement** owns the remaining deferred Record residuals; and **S4f.21 — Record-stop caller migration** closes the production continuations. Use the existing execution packets and production fixtures. The next gates are focused native tests, full `pio test -e native`, and the `teensy41-capture-serial` build. Do not wait for an unstable device/HITL run to advance a host-verifiable gate.

### S4f.22 — shared finalization integration gates (after the runtime slice)

Run the contract matrix, mutation proofs, and integration checks only after the S4f.17–S4f.21 runtime path is complete. Device acceptance is a later, named gate; no upload or destructive device operation is implied by this scope.

### S4f.22 acceptance follow-up — slot gesture during overdub finalization

Reopened for DEC-096 at the named S4f.22 device gate. During pending overdub finalization, one slot-button event routes populated targets to playback selection/queue and empty targets to record arming; no clear or session undo. Owner remains `MidiButtonActions` plus existing `TrackManager` queues; no new state/owner. Host fixtures, full native suite, and `teensy41-capture-serial` build passed (RAM1 padding 1,928, floor 840). User authorized track 8 slots 3–5; the driver performed identity-grade clears only on those slots.

**Device status (not accepted):** firmware image `8d07faec…e05f`, source `f0c4b26d` plus dirty working changes. Read-only census after clearing found slots 3–5 had zero committed events/passes and length 0, though verified payload files remain. OD→OD (`captures/s4f22-dec096-od-01`) drove, but the oracle returned `SETUP_INCOMPLETE` (three ring-shed rows; overdub interval identity/L1/L5/L6/L7 unknown; only finalization window/release L3/L4 observed). R→R (`captures/s4f22-dec096-rec-01`) aborted before recording after a slot retry became `Loop 3 (double)`/LOOP_EDIT. Mixed (`captures/s4f22-dec096-mixed-01`) seeded slot 4, then slot 3's seed gesture changed loop geometry in startup LOOP_EDIT rather than recording. The partial mixed attempt leaves its seeded content on authorized scratch slot 4; slots 3–5 remain test scratch only. No content-parity run; S4f.22 remains unchecked. Device artifacts are incomplete setup evidence, not firmware acceptance. Further HITL iteration is stopped here; do not repeat the seed driver blindly.

**2026-09-30 — the accepted-sequence oracle's `overflow_free` gate no longer conflates a timing shed with a ring loss.** The firmware drops only non-Tier-A diagnostics in the playback flush instead of blocking the transport on USB I/O, and its own comment records that as *neither* loss kind; `scripts/hitl/serial_collector.py` has always marked the same three session-invalidating rows `CaptureIntegrity.INCOMPLETE`, and `scripts/hitl/verify/midi_boundary_timing.py` has never counted a shed. This oracle's `RING_LOSS_RE` grouped `RING,shed` with `RING,tier_a_evicted`, so **every** fresh-boot leg returned `SETUP_INCOMPLETE` on a shed row alone — recorded as over-strict on 2026-09-27 in `tasks.md` and the S4f.17 packet, and re-confirmed for `captures/s4f22-dec096-od-01`. The gate now fails only on `tier_a_evicted` / `overflow` / `critical_rejected` / `wiped`, keeps the shed visible as a non-fatal `ring_shed_diagnostics_only` row, and attaches the rows as `evidence.ring_shed`. The same oracle gained a second classification fix in this pass: **an absent L3 finalization window (or L4 release) is a staging outcome — "the acceptance was not exercised" — not a chain violation**, matching its own L3 wording and the packet's NOT_EXERCISED classification, so a NOT_EXERCISED run is no longer labelled a firmware contradiction. Re-graded with **no device action**: `s4f22-dec096-od-01` moves `SETUP_INCOMPLETE`(3) → `INSUFFICIENT_EVIDENCE`(4) with L3/L4 still the only observed links, and the four pinned `s4f13-od*` corpus fixtures stay non-passes (`od-01`/`od3`/`carrier` move to `INSUFFICIENT_EVIDENCE`, `od4` stays `SETUP_INCOMPLETE` because its window/release never appeared). Host: `test_accepted_sequence` **54/54**, `test_accepted_sequence_corpus` **3/3**, plus integrity 17/17, command-forwarding 26/26, managed-record-drain 20/20, capture-transitions 61/61 and device-shutdown 15/15; `git diff --check` clean. No firmware change was needed: the defect was host-side classification.

**2026-09-30 — clear-first HITL rerun attempted; blocked in the driver's census precondition, not in the firmware.** `captures/s4f22-dec096-od-02` (`scripts/hitl_accepted_sequence.py --leg od --track 8 --slot-a 4 --slot-b 5`, i.e. the driver's **default clear-first** path, no `--no-clear-targets`) uploaded `8d07faec…e05f` (`git_commit=f0c4b26d`, firmware hash unchanged, dirty working tree) and then failed at the *clear*, before any leg gesture. The initial read-only census was lost, so `known_occupied` was `None` and the driver deliberately did **not** pre-select the filled, non-selected target; slot 4's long press was therefore answered `Loop 4: Queued playback switch (filled slot)` with `#CAP,68054720,ST,Track,STOPPED,PLAYING` (the switch *started* the transport instead of clearing), and the follow-up census never answered either (`census_lost`), so `stopped_preparation.clear_slot` returned `UNCONFIRMED` — fail-closed: no clear, no seed, no take, `PREPARATION_FAILED`. The device then did not reach verified persistence idle inside the 30 s cap (`SHUTDOWN_INCOMPLETE_TIMEOUT`), which **latched** `captures/.device-shutdown-incomplete.json` (`context: accepted-sequence-od`) and correctly refused the next upload/reset. A read-only canonical check afterwards (`captures/dec096-recovery-check-01`, no upload and no reset) reached **verified persistence idle** — `2 stop+drain cycle(s)`, tail `#CAP,379738843,SAVE,idle,0`, `FinalizeWorkspace … done,ok` — so the device is known good again and the latch is the only gate before another reset. The proven identity-grade clear path for a filled target is `scripts/hitl_retention_handoff.py --prepare-only` (it cleared track 8 slots 3/4/5 in `captures/s4f22-dec096-prepare-02` with no census precondition). No S4f.22 acceptance, no content parity, no task checkoff. Driver-numbered slot 4 still holds its `f0c4b26d`-era seed.

**2026-09-30 — the accepted-sequence driver's device path is corrected: every gesture is now gated on the device's own slot-scoped evidence.** The three failed legs were reconstructed and each cause is now a *named* failure instead of a silent press, so a rerun can be attributed even when it does not complete. Scope: `scripts/hitl_accepted_sequence.py` + `scripts/test_accepted_sequence.py` — host tooling only, no firmware, no device action, nothing uploaded.

* **The census is retried, then refuses before any gesture.** A `census_lost` window is a delivery outcome, not a property of the library (`od-02`: the first request was lost, the *next* answered 64 slots and settled), so `_run_census` retries on a bounded set (`--census-attempts`, default 3) and, when the target's occupancy is still unknown, raises **before pressing anything**. That closes `od-02`'s failure mode at the source: the old code long-pressed a filled, non-selected target, which `CLEAR_TRACK_FOR_SLOT`'s not-selected branch answers with `Loop N: long-press start (not playing)` — it *starts playback*, never a clear. (The `Queued playback switch` wording in the earlier note is the short-press sibling; the long press is what the driver sends.)
* **A filled target is selected and the selection is confirmed before the clear.** The pre-select press must be named by the device's own `#CAP,<us>,DISP,<slot0>,…` row for that slot within `--arm-timeout-s`; without it the driver refuses by name instead of clearing a slot it may not actually have selected.
* **The seed press must produce a slot-scoped arm receipt.** `mixed-01`'s two `Loop 3 (short)` presses (39.520 s and 42.647 s) left **no** `DISP` row for that slot at all: a predecessor's in-flight Record job held the selection, so the firmware swallowed the gesture. The driver now requires `DISP,<slot0>,ARMED` (or `RECORDING`) for **that** slot, over a bounded press set (`DEFAULT_SEED_PRESS_ATTEMPTS = 3` — the replay of `mixed-01` shows press 1 silent, press 2 only moving focus to `DISP,2,STOPPED`, so the arm takes the third once the target really is selected), and never falls back to starting the transport, which recorded nothing and hid the absent take. The `RECORDING` transition is read the same way.
* **Repeated presses of one button stay outside the firmware's tap window** (`_PressSpacing`, ≥ `DEFAULT_TAP_WINDOW_S` per note). `rec-01` sent two `Loop 3` taps 0 ms apart, which the firmware reported as `Button press: Loop 3 (double)` = `OVERDUB_FOR_SLOT`, so the seed's record press never happened.
* **Every seed abort is a `PreparationFailure`** (caught, reported) — `rec-01` and `mixed-01` raised `RuntimeError` and died with **no** `accepted-sequence.json` at all, which is why their cause had to be reconstructed from the log.
* The seed's post-commit finalization receipt (`Reentry released slot=<n>` / `Record pass undo reset for slot <n>` / `PERS,result,` within `--seed-settle-timeout-s`) is **recorded, not gated**: it makes the release latency attributable, while the *next* seed's own selection/arm receipts are its gates.

**Evidence.** `scripts/test_accepted_sequence.py` **63/63** (9 new: unknown-census refusal, swallowed selection, swallowed seed press with no transport start, the working select-then-arm retry, slot-scoped/drained `DISP` filtering, the census retry, and three tap-window cases); four mutations red-proved and restored byte-identically (census refusal disabled → only the census fixture fails; arm gate disabled → only the swallowed-seed fixture; press spacing removed → only its gap assertion; selection gate disabled → only its fixture). Neighbouring host suites green together: accepted-sequence corpus, capture integrity, command forwarding, managed-record drain, capture transitions, device shutdown, device time, hitl actions, boot settle, clear-before-record precondition, edit-mode precondition, midi-clock, retention handoff/probe — **369/369**. `git diff --check` clean. The new gates were also replayed against the real logs: the selecting press (`Loop 4 (short)` 17.021 s) holds `#CAP,17202864,DISP,3,STOPPED,…` while the swallowed presses hold no arm row. One flake seen once and **not** from this change: `test_midi_clock`'s `test_pumped_host_phase_waits_on_the_pump_instead_of_sending` failed 1/8 runs when run alone (0/8 with these suites first) — a real-time pump test, recorded rather than hidden.
**Device status 2026-09-30 — the OD leg now drives on hardware;** `captures/s4f22-dec096-od-13` is `DROVE_LEG` on commit `cf6c1853` (firmware `8d07faec…` = the DEC-096 image): both clears (B through the documented `post_load_holdoff` refusal retries, A through an `ABORTED` retry), both seeds armed/recorded/committed/settled, the leg's full plan driven, shutdown `complete`, `captures/.device-shutdown-incomplete.json` **not** latched. The oracle passes every setup gate (fresh boot, identity pinned, overflow-free — the one `RING,shed` reported as the non-fatal diagnostic row it is), `L2_no_capacity_denial`, `L3_a_finalization_window` (`DIAG,reentry_window_open,3,1049,1536`) and `L4_a_capacity_released` (`DIAG,canonical_release,3,32,1536,8`), and reports `L1`/`L5`/`L6`/`L7` and `overdub_interval_identity` as **unknown** with its own wording: *"RECA/RECS do not identify overdub intervals; correlated overdub start, accepted-stop and retirement evidence for A and B is still required."* **Still owed:** that overdub-identity evidence (the sibling change's identity work, not this driver's gates) and a content-parity run; S4f.22 stays unchecked for those. The ten attempts that produced the fix, each with the device row that named it, are in the entry above. Further device iteration needs approval (upload/`--boot-reset`).
**Identity status 2026-09-30 (host, no device action) — the overdub-identity half is now read, and the same run passes its chain.** The oracle force-unknown'd L1/L5/L6/L7 because its only stop vocabulary was `RECA`/`RECS`, which belong to the *record* seeds on this leg. The intervals' own rows were already being emitted: `DIAG,reentry,admit` (start), `DIAG,reentry,bound` (accepted stop), `DIAG,stop_state` (6→7 entry, 7→5|1 terminal), `DIAG,canonical_release` (retirement), plus the button action's per-interval `Loop N:` request rows for L7. Re-graded with **no device action** on the unchanged log, `captures/s4f22-dec096-od-13` is **`ACCEPTED_SEQUENCE_PASS`(0)** — A interval 3 (start 55.763 s, accepted 57.035 s, retired 58.772 s) and B interval 4 (start 58.214 s, executed 59.142 s, retired 59.194 s), B beginning 0.56 s inside A's window — and its `accepted-sequence-review.json` was rewritten in place. `captures/s4f22-dec096-od-01` re-grades to `SETUP_INCOMPLETE`(3) with a **named** cause (B started 136 ms *after* A's release: no overlap, NOT_EXERCISED) instead of an unknown. **Still owed for S4f.22's acceptance:** a content-parity run, and a leg whose successor start lands inside A's window (the arrangement is racy: A's window ran 1.05 s in `od-01` against the driver's ~1.2 s switch→record cadence, 1.74 s in `od-13`). No firmware row was added; the `ODUB,stage`/`ODUB,stop` families still carry no interval identity (the rest of task 2.2).


### Parked — response-driven HITL/content-evidence follow-up

The response-driven HITL change and the S4f.22 content-evidence investigations remain recorded for later device validation. They are not the current implementation blocker. Do not alter runtime semantics to compensate for unstable HITL evidence; reopen this work only for a named acceptance gate or a separately approved host fix.



### S4f.17–S4f.21 — connected shared capture finalization (authorized; 2026-09-27)

User-requested firmware contract is recorded in the existing runtime OpenSpec:
[shared capture finalization](../../openspec/changes/runtime-working-set-ownership/SHARED-CAPTURE-FINALIZATION.md).
S4f.15 — contract and S4f.16 — production finalization fixtures are complete.
S4f.17–S4f.22 remain open. **S17.1 + S17.2 are landed (2026-09-29): the production Record stop no
longer finalizes inline.** Both Record stop entry points admit a deferred job
(`beginDeferredRecordStopForSlot`), run their bounded transport edge
(`Track::completeAdmittedRecordStop`) and return; a denied stop is retained and admitted later by
`driveAcceptedInteraction`, which now dispatches on the retained request's capture mode; the pass,
its `RecordPassAdded` undo entry and its persistence admission come from the worker's publication.
The strict matrix — R→R, R→OD, OD→R, OD→OD — is green (45/45 with and without
`S4F16_REQUIRE_CONTRACT`), full native is 2 612 cases / 2 605 succeeded / 7 skipped, and the
capture-serial build passes with RAM1 padding 2 040 (floor 840).
[Cutover packet](../../openspec/changes/runtime-working-set-ownership/S4F17-CUTOVER-PACKET.md).
Next: S17.3 — convert `stopRecordingTrack`, `finalizeCaptureAndSelectSlot` and `stopAllTracks`
from reading the stop-edge-stamped length to consuming a job-qualified geometry-ready result,
protecting a same-slot destructive restart while permitting a different-slot successor. Then
S17.4 — the S4f.20 residuals — and S17.5's enforced gate and mutations. No weaker record oracle
is authorized. Response-driven HITL tasks remain the automation dependency.

S4f.18 — frozen-source consumer prerequisite is implemented: the existing bounded playback
fill reads exclusive raw chunks, projects record-stop ticks and denies stale or failed reads.
Full native **2,504 passed / 7 skipped**; capture-serial build and layout pins pass with
RAM1 padding **840 bytes**. No admission switch or task checkoff. Next complete bounded
held-note/record-pass preparation and integrate the generation-correct playback handoff.
[Consumer evidence and remaining gates](../../openspec/changes/runtime-working-set-ownership/S4F18-SOURCE-EXECUTION.md).

The Record held-note identity prerequisite is now verified: accepted input stamps the existing
pending-note entry, and physical/stop Offs preserve that identity after raw ownership moves.
Full native **2,508 passed / 7 skipped**, focused **19/19**, capture-serial build passes with
RAM1 padding **840 bytes**. S4f.18 — bounded record preparation remains open for job-owned
closure output, full normalization and pass construction; Record still finalizes inline.
[Closure receipt evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-CLOSURE-EXECUTION.md).

Job-owned closure output is now verified with one reserved row per grain, unchanged
successor capture, and owner/generation-qualified retry. Full native **2,513 passed /
7 skipped**, focused **24/24**; build passes at **840 bytes** RAM1 padding. Per-slot
runtime grows by eight extmem bytes for the output receipt. Record normalization,
pass construction and production integration remain open; no admission switch.
[Output consumer evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-OUTPUT-EXECUTION.md).

Normalization's real-stop content oracle now covers both destinations, overflow,
minimum-length policy and a 2,056-row take beyond overdub staging. **29/29** focused,
**2,518 passed / 7 skipped** full native; four mutations fail. Production bounded
normalization/pass construction remain the next implementation work. No firmware
behavior changed or task closed by this fixture checkpoint.
[Normalization evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-NORMALIZATION-EXECUTION.md).

Prepared canonical output can now transfer its chunk directory in bounded moves/
swaps through the existing store owner, with no event copy or reference churn.
Fully prewarmed synchronous callers share the implementation. **40/40** focused,
**2,525 passed / 7 skipped** native; capture-serial build keeps **840 bytes** RAM1
padding. The 64-byte ARM cursor adds no resident Track/job/runtime field.
Full normalization and Record worker phase integration remain open.
[Transfer evidence](../../openspec/changes/runtime-working-set-ownership/S4F18-TRANSFER-EXECUTION.md).

### Response-driven HITL for S4f.13 — accepted-sequence acceptance (2026-09-27)

Active next-step contract: [response-driven-hitl-acceptance](../../openspec/changes/response-driven-hitl-acceptance/tasks.md).
Proposal, design, spec delta and tasks exist; fixture tasks 1.2 and 1.3 are verified.
The shared record/overdub overlap contract is resolved by the runtime firmware contract above;
its implementation gates remain open. See the sibling
[execution review](../../openspec/changes/response-driven-hitl-acceptance/EXECUTION-REVIEW.md).
The first slice pins bounded identity evidence and deterministic production gesture fixtures,
then adds read-only instrumentation before replacing the driver. Record and overdub remain
separate continuously playing recipes; stopped target preparation and final dumps are outside
the measured window. No manual golden pass or reboot is required. Target-specific destructive
authorization is still a run input. Parent acceptance stays open until complete device evidence.

### S4f.13 — accepted-sequence driver correction (2026-09-27)

User-authorized host-only correction: reuse the retention driver’s capture-switch stop for the overdub leg, seed both overdub targets, prevent further gestures after an unexpected playback queue, and preserve an abort report through managed shutdown. Correct false-positive acceptance evidence without changing firmware or accepting the device gate. No device writes or clears are part of this slice.

### Continuation after recovery (2026-09-26)

DMA fault investigation/repair is parked until after the whole active OpenSpec by
explicit user instruction. No display implementation slice is active. Recommended
next scope is S4.1 — persistence identity during handoff: investigate the footer's
consistency contract and establish a controllable native fixture before edits.
See [NEXT-STEPS.md](../../openspec/changes/runtime-working-set-ownership/NEXT-STEPS.md).


### Automatic runtime-bundle boot repair (2026-09-26; approved)

**2026-09-26 — recovery firmware uploaded with explicit user authorization.**
`teensy41-capture-serial` upload SUCCESS; SHA256
`4b13749ab9d97b0699fb02f813a3a9a26b7475a60391e19ffa784c21cec93255`
matches the host-verified image. Confirmed standalone read-only reader before upload;
archived and cleared the incident hold for this approved recovery installation.
Device reports runtime metadata repaired, `BOOT,load,ok`, and a settled checkpoint:
`admission_complete=1`, `interactive_ready=1`, `undo_hydrated=1`,
`transport_stopped=1`, `deferred_prewarm_idle=1`, `free_chunks=391`.
Serial held open through verified persistence idle, then closed. No content/clear
stimuli. This establishes successful recovery boot, not per-loop content parity,
hardware Undo/Redo roundtrips or second-boot acceptance; those remain open.
Evidence: active-change workspace-loss `authorized-recovery-upload/`.


**2026-09-26 — automatic boot metadata repair implemented and host-verified.**
DEC-089 — boot metadata repair validates the complete workspace before generating
runtime metadata; preserves source files and corrupt originals; blocks saves after
refusal. Actual boot owner restores the copied 64-slot library and derives 122 undo
actions; healthy reboot retains them. Six isolated cases include 276 interruption
cuts. Full native 2,461 passed / 7 skipped; Python 16; capture-serial SUCCESS with
RAM1 padding 856 (floor 840). No upload/device action; hardware acceptance and the
incident hold remain open. Evidence: active-change
`evidence/2026-09-26-workspace-loss/BOOT-REPAIR.md`.


User approved the proposed inspect → validate slots → separate candidate → checked
publication → normal deferred load sequence. Implement in StorageManager, before
cleanup. Preserve original slot/workspace bytes and broken metadata; failure blocks
normal operation/persistence rather than saving an empty workspace. Test interruption
at write/rename boundaries using native SD injection and the preserved folder copy.
No actual device actions/upload; host acceptance plus capture-serial build only.
DEC-089 and the execution packet specify the conservative recovery prerequisites.


### Approved load-time undo reconstruction (2026-09-26)

User approved changing the zero-after-load contract: derive undo from saved passes,
redo empty on load; an in-session Undo creates Redo. Implement in existing load
commit/TrackUndo owners, preserve loaded root authority, and protect already-loaded
slots' redo during later background slot commits. No persisted undo payload or new
format. Native recovery/interleaving acceptance plus full native and capture-serial
build; no hardware action, incident hold remains. DEC-088 and packet in
ARCHITECTURE-REVIEW.md supersede the prior pending-policy status below.


### Undo load policy review (2026-09-26)

User requested the next load/undo fix. Evidence is now pinned in
UNDO-LOAD-POLICY-REVIEW.md. Existing zero-after-load requirements conflict with
preserving derived history; production implementation waits for explicit approval
of the proposed changed behavior. Host review and correction only; device hold stays.


### Offline undo reconstruction acceptance (2026-09-26)

**Host testing complete; persistent recovery blocked:** 122 actions roundtrip across
60 nonempty slots, but reload retains 9. Follow-up is the deferred load/undo-history
publication boundary; no device installation or firmware change in this slice.

User requests testing and reconstruction where possible. Reuse copied current files
and host-only candidate; exercise target load undo hooks (native normally bypasses
them), deferred undo hydration, derive history using existing content-history owner,
and prove undo/redo state/content restoration. Report original cross-slot chronology,
clear snapshots or old redo branch as unknown unless independently recoverable.
No production firmware or device writes authorized by this slice. Do not present
an in-memory rebuilt history as boot-persistent without a reload test.


### Host workspace reconstruction and load acceptance (2026-09-26)

User explicitly requests reconstruction and testing. Preserve every copied slot and
workspace.bin byte-for-byte; add missing runtime.bundle.bin to a new host candidate.
C++ writer uses existing serialization owners. Defaults are explicit: idle/stopped,
120 BPM, first nonempty slot selected/enabled on each track, unmuted, fresh undo.
Test missing/corrupt bundle refusal and candidate acceptance through production
load path on in-memory SD, including bounded restore completion/content counts.
No device access or recovery writes. Firmware recovery is feasibility/design only.


### Offline production decoder audit (2026-09-26)

Next authorized host-only slice: feed the 64 verified current slot bodies through
production StorageLoopIo decoders; record success, pass counts and snapshot note
identities. Reject truncated controls. Inspect required runtime metadata without
constructing or installing guessed settings. No device access.

### Offline recovery audit (2026-09-26)

Incident-support prerequisite for S1.6c.2 — capacity revalidation. Inspect only the
verified `download-05` host copy; verify transfer hashes, stored checksums and
container boundaries, and report candidate metadata without claiming loadability.
No device access, source-backup mutation, recovery write or normal firmware upload.
Oracle: reject corrupt/truncated containers; distinguish structural integrity from
semantic restore validation. Focused host tests and a reproducible JSON report.

### USB folder reader — local preparation only (2026-09-26)

User requests a simple program reading only `/MidiLooper` over USB, with the SD card left in
the Teensy and unrelated card data excluded. Prepare a separate read-only utility firmware and
host downloader under `tools/midilooper-reader/`; no looper runtime changes, upload, reset or device
access in this slice. Existing USB firmware has no general file export command. The standalone
utility deliberately omits looper startup/recovery/persistence code. Keep the incident hold set.
Contract, verification and pending deployment: `tools/midilooper-reader/README.md`.

**2026-09-25: authorized S1.6c.2 — snapshot construction reuse.** Implement candidate-private index-page reuse and prove allocation scaling, immutable parent/sibling isolation, failure cleanup and device memory reduction. Then assess role-specific capture reservations under the existing phase dependencies. Packet in the active runtime ARCHITECTURE-REVIEW.md. No inactive-root demotion, ring resizing or allocator replacement in this slice.


**S1.6c — known owner repairs (user requested):** retire RuntimePlaybackView origin/rank arrays with whole-take storage; reuse the existing pending-paint allocation as draw-source compose scratch, including in-place slide filtering. Preserve selected bridge authority, composed/pending paint separation and all note/window semantics. Revalidate native then the original N=56 24-bar HITL before making capacity claims.


**S1.6d — stopped heap diagnostics:** user-authorized explicit !HEAP_STATS inspection only at a stopped, clock-quiet, storage-idle checkpoint. No direct allocator scans in live telemetry. Contract in runtime-working-set-ownership ARCHITECTURE-REVIEW.md.


**2026-09-25 priority — S1.6 heap correctness.** Implement physical-pool accounting and bounded allocation-failure attribution under runtime-working-set-ownership before more capacity sweeps. Existing owner/routing/admission thresholds remain unchanged. Owner-lifetime repair follows identified caller evidence; S1.5 and S2b.3 stay open. Packet: active change ARCHITECTURE-REVIEW.md, S1.6.


**2026-09-25 — S1.5 device refusal-point attempted on the N=56 library: REFUSED IN THE RECORD PHASE; the overdub reserve admission was never reached. Clause stays OPEN.**

Owner direction: continue S1.5 with the HITL skill; the target **user track 8 slot 4** was authorized in
this session (verified empty). Pinned image `b289af53…`, git `1ccff16a`. Fixture
`STIM-BASE-2-2-01-TEMP-S15-24BAR`: the RWSO-24CAP canonical base shape with
`--record-bars 24 --overdub-bars 24` and `--no-clear-before-record` (the target was already empty, so no
CLEAR was issued). Pre-flight (read-only `!LIB_OBSERVE`, fresh boot): complete 64/64 observation,
payload inventory 59, initial playback 8, committed at checkpoint 56 (pass-backed 56 / root-backed 53),
`chunks_free 403`, `live 6846 KB`, `slab_used 4774 KB`, `snap 4196 KB`, `heap_free 84 KB`, target `(8,4)`
empty.

**The take.** The device entered RECORDING at 89.169 s. From **91.961 s** `snapshot_reserve` denied every
~1 s (`need=40960B`, `reason=heap_low`); `heap_free` decayed **68 → 64 → 60 → 56 → 52 → 40 → 32 → 0 KB**
over **39** denials. At **130.538 s** the allocator refused the 8,192-byte unnamed request with
`heap_free=0KB`, `tag=0`, `live=8527KB`, `chunks_free=391`, `snap=4196KB`, and the fatal exhaustion line
fired; the log ends there — no crash report, no reboot. The host aborted with `record_stop_unobserved` /
`INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)`, which is the setup oracle, not the capacity verdict.

**Classification per the slice's execution packet.** `Overdubbing started` = **0** and
`op=overdub_start` denials = **0**, so the **overdub reserve admission was never reached**. This is the
**S1.3 record-phase hard-failure class** (internal-heap `snapshot_reserve` → `MEM_EXT_REFUSED` 8,192 B,
`tag=0`, `heap_free=0`), now reproduced with the canonical 24-bar base workload rather than only the dense
1-note-per-clock probe. It is **not** the S1.5 overdub-reserve refusal. The boundary is the internal heap,
not the chunk pool (391 of 512 free) and not the snapshot arena (`snap` unchanged).

**Reading.** At N = 56 the 24-bar **record prerequisite** is refused, so *"the smallest N at which a
24-bar overdub is refused"* is not isolated there. The S1.5 bracket is now **(2, 56]**: the scenario was
admitted at the 2-slot RWSO-24CAP library (2026-09-23) and refused at the 56-slot library here; S1.3's
2-bar slope recorded its first named denial at N = 55. **S1.5's refusal clause stays open.**

**Peak figures (S2b.3 input, not an S1.5 checkoff).** Boot `live` 6846 KB / `chunks_free` 403; peak at
the refusal `live` 8527 KB / `chunks_free` 391 / `heap_free` 0. The ~191-chunk peak *reservation* figure
still has no telemetry — no `capture_prewarm` rows are emitted during a take and the reserve admission
was not reached.

**Library integrity.** A fresh recovery boot reports the **identical** observation (payload 59, initial
playback 8, committed 56 = 56/53) with **0 content-field differences across all 64 slots**, target `(8,4)`
still empty, `free_chunks 403`, `live 6842 KB`, all readiness fields attested. No clear/delete/rename or
existing-slot overwrite; only the authorized target was addressed.

**Next bounded step (not started).** Isolate the **overdub** reserve admission, which needs a 24-bar loop
present at an N with internal-heap headroom — an authorized target at a **lower** library N (record the
24-bar loop, then overdub it), or an explicit decision to treat the N = 56 record-phase refusal as the
S1.5 / S2b.3 capacity boundary. No firmware change, no new owner/policy/exception, so no `DEC-###` is owed.
Evidence `captures/s15-24bar-t8s4-01/refusal-analysis.json`, `captures/s15-preflight-01/`,
`captures/s15-recovery-02/`; full record
[`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)
§ S1.5 — device refusal-point result and execution packet. Work remains uncommitted on
`feature/runtime-working-set-ownership` (HEAD `1ccff16a`).

**2026-09-25 — S1.3 residency slope COMPLETE: dense probe reached hard allocation failure.**
The fixed-role two-bar slope remains N=10..56 (46 verified additions). A separate dense
four-bar probe at N=56, user track 8 slot 4, supplied a one-clock note cadence across pitches
0–127. Recording started; at 41.448245 s device time, before record stop, the allocator refused
8,192 bytes with 4 KB internal heap free (`tag=0`) and emitted the fatal exhaustion line.
This is the measured failure point for this library/workload, not a universal smallest N or a
64-bar capacity claim. The untagged allocating caller is not identified. The last sampled free
chunk count was 393: do not classify this as chunk-count exhaustion.

The failed take had no verified persisted payload; the fixture withheld its survival reboot.
Separate crash recovery and a final read-only boot confirm the original 56 loaded slots,
unchanged other observed content fields, matching boot roles, and an empty failed target.
Board is stopped at track 6 slot 3. No clear/delete/rename or existing-slot overwrite.

S1.3 is checked: measured slope plus hard failure/error, alongside the previously completed
synthetic 60-descriptor characterization. S1.5 — 24-bar overdub refusal, S1.4 — undo/pin baseline,
S2b.3 — workload-peak budget and the other device gates remain open. No S3 — lease ownership start.
Host tests **42/42**, OpenSpec/diff checks pass. No firmware edits/build this slice; pinned image
`b289af534577e25b57c52146b27f66c245c97f43a5028fc10a37b9a8a9bbd208` reused.
Evidence: `captures/s13-dense-evidence/pressure-result.json`, `captures/s13-dense-t8s4-01/`,
`captures/s13-dense-recovery-01/result.json`; existing slope in `captures/s13-slope-evidence/extended-slope.json`.
Work remains uncommitted on `feature/runtime-working-set-ownership` (HEAD `1ccff16a`).

**Completed execution scope — S1.3 dense pressure probe (user authorized):** preserve the verified
56-slot library. Target user track 8 slot 4 only after a fresh complete observation proves it
empty. Reuse pinned firmware, single serial reader and empty-anchor arm/cancel preflight.
Four bars, 120 BPM, one note per MIDI clock, one-clock gate, pitches 0–127 cyclically;
leave the last clock for the final Off so no note is held at stop. This is a separate workload,
not another point on the homogeneous two-bar slope. Stop on first hard refusal/abort or
verification failure; retain temporary snapshot denials separately. No clear or overwrite.
Reboot only after full SD/RAM parity; no firmware policy change. Restore stopped focus 6/3.

<!-- S13_EXTENDED_PROGRESS_BEGIN -->
**Historical checkpoint — superseded by dense-probe completion above: S1.3 extended sweep N=10..56.**
46 two-bar additions passed exact 62-event / 1,536-tick SD/RAM survival before and after reboot.
All boot roles stayed fixed; no other observed content fields changed; 45 repeated intermediate
owner baselines matched exactly. Free chunks 449->403; snapshot telemetry 2,187->4,196 KB;
slab-used 2,491->4,774 KB; tracked live 4,221->6,846 KB. Tracked live/high includes the core
`extmem_malloc` internal fallback and is NOT a physical PSRAM-free oracle.

First named denial occurred at N=55 while recording the 56th slot (user track 8 slot 3):
`snapshot_reserve / heap_low`, 40,960-byte margin + 32,768-byte floor, heap 48 then 44 KB;
17 denial lines, zero wrapper allocation refusals. Publication deferred; the take persisted in
legacy format and survived reboot exactly. This is a temporary publication-admission boundary,
not a hard residency/canonical failure. S1.3 remained open at that earlier checkpoint;
S1.5 — 24-bar overdub refusal and S2b.3 — workload-peak reserve remain OPEN. S3 is not started.

Growth stopped at that boundary. The supervisor initially advanced because survival passed;
the next target (track 8 slot 4) was interrupted during preflight before recording. The host
runner now returns a stop status on named memory denials even if survival succeeds. Final fresh
read-only boot confirms 56 loaded, same observed content/roles, stopped focus track 6 slot 3.
No clearing/deleting/renaming or populated-slot overwrite. User authorization for empty slots
was sufficient; this stop is a measured boundary, not exhausted authorization.

Evidence: `captures/s13-slope-evidence/extended-slope.json`, `extended-episode-peaks.json`,
`captures/s13-empty-anchor-t8s3-01/`, `captures/s13-sweep-closeout-01/`. Host tests 40/40;
OpenSpec validates. Firmware unchanged this slice (SHA256 `b289af53…`, code 391,304 / padding
1,912); prior native evidence 2,384 passed / 1 skipped remains applicable, not newly rerun.
Changes remain uncommitted on `feature/runtime-working-set-ownership`.
<!-- S13_EXTENDED_PROGRESS_END -->

**2026-09-25 — S1.3 extended-empty-slot authorization:** user explicitly approved extending
measurement to other verified-empty slots, preserving existing recordings. Reuse the same
pinned firmware, two-bar stimulus and event-parity gates. Start with track 5 slots 2–8, then
other empty slots. Populated selection anchors use the verified path; an originally empty
selection requires the explicit host arm/cancel preflight (no Start, clocks or notes; unchanged
record-identity count and all observed content fields) before any take. Stop on failure. Restore BOTH the target track's original
selected slot and global focus track 6 slot 3 before each survival reboot. Compare all boot
footer roles, loaded sets and existing observed content fields. Stop at first refusal or fixture
failure. A cross-track capture-admission failure is classified separately from boot/load residency;
it is not automatically the increasing-N capacity boundary. No clear/delete/rename or overwrite.


**2026-09-25 — S1.3 fixed-role growth MEASURED, range 10–15 loaded slots.**
Five two-bar takes into user track 6 slots 4–8 passed exact SD/RAM event-and-length parity
before and after reboot (62 rows / 1,536 ticks each). All ten boot checkpoints used the same
stopped transport and boot playback footer; focus was restored to track 6 slot 3. Each loaded-set
comparison added only its intended target and lost none; existing observed content fields stayed
unchanged. Four repeated intermediate baselines reproduced every reported owner total exactly.
Payload inventory stayed **36**; content union/pass count **10 -> 15**, root count **8 -> 13**.

Post-restore free chunks: **449, 448, 447, 446, 445, 444** at N=10..15. Cold reservation walk
remained **512 -> 480**, all walked credits zero (not workload peak). Snapshot telemetry grew
**2,187 -> 2,410 KB**, slab-used **2,491 -> 2,745 KB**, live **4,221 -> 4,799 KB**; heap_free
stayed 84 KB. Live increments are **65, 352, 64, 33, 64 KB**: retain the N=11->12 jump,
not a single linear per-slot attribution. Subsequent code/capture attribution identifies the 64->65 canonical-chunk backing-block boundary
(328,448 bytes); see the extended review entry.
No observed MEM_EXT_REFUSED, MEM_DENIED, FATAL or RING overflow in the ten measurement logs.

**Stopped because all approved empty scratch slots are now populated, not because of capacity.**
S1.3 and S1.5 remain unchecked: failure point not reached, two-bar workload only. S2b.3 remains
open because workload peak reservation is unmeasured; S3 does not start. Next device growth needs
explicit authorization for additional verified-empty slots outside the prior scratch range (the final
manifest lists candidates), or permission to recycle a named scratch subset. No clear/delete/rename
or recording outside track 6 slots 4–8 occurred this slice. Board left stopped, focus track 6 slot 3.

Evidence: `captures/s13-slope-evidence/slope.json`; detailed runs `s13-slope-slot4-04` and
`s13-slope-slot5-01` through `s13-slope-slot8-01`. Full native **2,384 passed / 1 skipped / 0 failures**;
decoder 51/51; host 35/35; OpenSpec/diff checks pass. Firmware unchanged from the prior green
capture-serial image (SHA256 `b289af53…`, code 391,304 / padding 1,912); only host tools/tests/docs
changed this slice. Work remains uncommitted on `feature/runtime-working-set-ownership`.


**2026-09-25 — S1.3 fixed-role growth sweep (authorized continuation).**
Reuse the verified two-bar single-session stimulus and SD parity gate. Add one verified-empty
track-6 scratch slot per cold boot, starting at user slot 4; never overwrite populated content.
Return selected track/slot to track 6 slot 3 before reboot and wait for persistence admission
through the existing SD-page oracle. At the next boot require identical transport footer roles,
all post-restore readiness fields, and complete LIBOBS; compare the full loaded set and retained
slot content signatures, not payload inventory. Verify the new payload/RAM after reboot against
its pre-reboot bytes/events. Record owner telemetry at the same settled pre-interaction checkpoint.
Stop at the first named resource refusal, fixture failure, eviction, or authorized-range exhaustion;
do not relabel a host/fixture failure as a capacity boundary. No firmware changes or clearing.
G2 oracle: N -> N+1, only the target added, zero losses or existing content-field changes, roles
unchanged. Reboot gate: stored events match RAM and return-to-focus storage admission succeeds.
Evidence is one directory per attempt with upload identity, command logs, source hashes and raw
observations. This measures the two-bar workload, not the 64-bar capacity envelope or peak capture
reservation. S1.3 stays open if the first genuine resource boundary cannot be established.

**2026-09-25 — S1.3 controlled one-slot survival PASS (uncommitted branch work).**
Track 6 slot 3 / `(5,2)` was verified empty, then received one two-bar take. Take identity
matched start/stop/commit/admitted target. Two full SD reads were identical; the production
legacy parser with an explicit Teensy event-layout adapter decoded **62 events / 1,536 ticks**,
exactly equal to stopped RAM. Only then was the unchanged firmware re-uploaded for the survival
boot. Restored RAM and two new SD reads match all events and the original file hash. Loaded
union **9 -> 10** (passes 9 -> 10, roots 8 -> 8), payload inventory stays 36 because an existing
empty payload gained content. Settled free chunks **450 -> 449**; after-boot attested checkpoint
449. No clear/delete/rename, no non-scratch recording. Evidence:
`captures/s13-controlled-01/` and `captures/s13-controlled-survival-01/`.
Verification: full native **2,383 passed / 1 skipped / 0 failures**; host **29/29**;
firmware build and OpenSpec validation pass.
This closes the one-slot fixture blocker, not S1.3's multi-point slope or S1.5's refusal point.
Next: define fixed-focus/active-role cohorts within the remaining approved scratch slots, compare
complete content sets (detect eviction), and measure through the first classified failure. The
selected/initial-playback slot changed in this fixture, so its one-chunk difference is descriptive,
not an isolated per-slot slope. S2b.3 still needs workload peak reservation; S3 remains gated.


**2026-09-25 — S1.3 controlled-baseline and SD readback:** user authorized continuation.
Implement a SESSION_CAPTURE-only, stateless 64-byte current-slot SD page reader under existing
StorageManager admission; host reassembles two matching reads and uses production native parsers.
No live slot adoption, reset/retry, runtime leases, new queue, or SD mutation. Then perform a
controlled baseline and one verified-empty scratch-slot take; survival reboot requires stored-event
parity first. Initial diagnostic reset may establish the baseline, not count as survival evidence.

Native evidence recorded: each denied/cancelled slot retains 16,384 fixed event-capacity units
and 8 canonical credits; same-slot retry reuses measured buffers, three slots total 49,152 units.
Full native: **2,379 passed / 1 skipped / 0 failures**; focused 99/99; host 23/23.
See `captures/s13-native-cancel-retention/`. No production cleanup fix or device claim.


**2026-09-25 — S1.3 native attribution:** the real record-admission and manager-cancel paths
retain fixed event capacity after denied admission on each distinct slot. Reattempting the same
slot reuses the measured buffers. This is a characterization of current ownership, not a cleanup
fix or proof that all allocations are stable. The shared test clock now matches production's
stopped-clock arming rule. Hardware writes/reboots remain paused; no stage gate changes.


**2026-09-25 continuation — S1.3 device attempts stopped at a named prewarm refusal.**
Host fixture now handles selection-created ARMED, explicit MIDI transport, clocked start/stop waits,
and final direct take attestation. 23 host tests pass. `captures/s13-takeover-take-05/` records
record_start/prewarm_failed and companion_history_reserve/pool_refusing; no notes, stopped afterward.
No new populated slot proved. Next: native attribution of prewarm residency across arm/cancel;
no more growth attempts until that boundary is understood. Stored SD events remain unverified;
CAPTURE_CONTENT is RAM only. No firmware edits or stage/gate checkoffs.


**2026-09-25 — S1.3 takeover: repair the single-session identity fixture before any further device write.**
Scope: host orchestration and evidence correctness; the unfinished synchronous legacy-only
payload probe is withdrawn. Continue with an empty-target preflight and the registered ARMED ->
transport-start sequence; one take may use verified-empty track 6 slot 3. Stored-payload event parity
and reboot verification remain open; no slope gate is closed.



**2026-09-24 (seventeenth session) — `runtime-working-set-ownership` S1.3 — the content-representation correction: the on-demand library observation now reports pass-backed and published-root content separately and counts loaded slots as their union. The corrected telemetry then CONTRADICTS the "28 root-backed" premise: only 9 of the 36 verified payload slots hold content.**

Owner direction: **fix the observation's representation gap first, then run the residency measurements** — the
restore trace already removed the presumed restore-instability blocker, so no restore investigation was
repeated. **S2b.7 — drain hardware confirmation stays separate.** One thing at a time; the immediate
implementation is the small observation fix, not the lease pool and not another sub-change.

- **Landed (firmware + host, native green, firmware build SUCCESS, device-flashed).** `LibraryObservation::SlotInput` /
  `SlotResult` carry `hasPassContent` and `hasRootContent` separately; `hasCommittedContent` is their
  **union**, and both `stateIsPopulated` and the observed-absence rule read the union. `observeSlot`
  observes `hasRootContent` as "a published root **is installed**" (`Loop::publishedSnapshot() != nullptr`)
  with `rootIsCurrent` (`Loop::publishedSnapshotIsCurrent()`) reported **beside** it — an installed-but-stale
  root is still content because playback reads it through the stale-root overlay, so currency never defines
  the content bit. **Invalidation now includes the root publication identity** — a per-slot FNV-1a
  hash of the root object's identity, its publication revision, the committed-pass revision and the currency
  bit, re-checked before emission, because `contentRevisionIdentity` reads the pass side only and a replaced
  root was previously invisible. The check is **guarded by `loopsAllocated()`**: `Track::getLoop` calls
  `ensureLoopsAllocated()` on its const overload, so an unguarded check would allocate a track between
  slices and invalidate records for the observation's own side effect (the O01 regression that caught it).
- **Wire.** Slot record `k` (union) + `kp`/`kr` (representations) + `kc` (root current); `LIBOBS,E`
  `m` (union) + `P`/`R`. Still inside `kMaxCaptureTextBytes` (192 B), with the tier test's width copies
  updated to match the emitter. Host `scripts/hitl/library_observation.py` decodes all of them and exposes
  `content_representation_observed` (a pre-correction capture reports the two subsets as **absent**, never
  as two false flags); `library_manifest.py` compares both representations and both counts, so a
  representation change fails the manifest even when the union is unchanged.
- **Tests.** Classifier: pass-only / root-only / both / empty plus the union counts and
  invalidation-clears-both-bits. Session **O09**: a current root is content without passes, and **replacing
  the root between slices invalidates the record** under an unchanged request identity and an unchanged pass
  identity. Session **O10**: the end record reports the union with one of each representation beside it.
  Host: 3 parser cases and 1 comparator case. **Full native 2,378 / 1 skipped / 0 failures** (baseline
  2,374); `teensy41-capture-serial` **SUCCESS** at `variables 96,192 / code 391,160 / padding 2,056 /
  locals 34,880` (the +384 B of per-slot root-identity state lands in `padding`, above the 840 B floor).
- **Durable corrections.** "28 unloaded" is withdrawn. The **device measurement then went further**: the
  corrected telemetry shows **9** content-bearing slots, not 28 root-backed ones, so the
  "28 root-backed" reading recorded at the sixteenth session is **CONTRADICTED** (evidence in the
  Measurements block below). The three-boot `restored-library-stability` gate stands as **repeatability of
  the fields it measured only** — it never covered whether a *new* payload survives a reboot, and the slope
  attempt below shows one that did not. Durable prose repeating the old wording is corrected in this same
  session (this file, `PROJECT_STATE.md`, `DELIVERABLE_TRACKING.md`, `tasks.md` and
  `ARCHITECTURE-REVIEW.md`). The two representations are also now distinguished on the wire (`k`/`kp`/`kr`/
  `kc`), and an installed-but-not-current root counts as content rather than as empty.
- **Pinned before measuring (task 4).** Authorized slots to empty/populate: **track 5 slot 1 and track 6
  slots 1–8 only**, each *selected and enabled* so boot loads it; **fixed enabled/selected roles** (transport
  stopped, no record/overdub, one active role held constant); **equal content per added slot** (same recipe,
  same bar count); **one payload format per series** (legacy *or* LSV1, never mixed); and
  **separate cost classes** — canonical reservation, canonical payload/pool chunks, snapshot roots, runtime
  directory. Failure point = the first refusal **inside the authorized population**; reaching the range's end
  with no refusal is **not** a capacity failure — report the measured range and leave **S1.5 — refusal point**
  open. Full pin: [`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)
  § *Pinned fixture parameters*.
- **Measurements (task 3, then task 4) — LANDED, and they change the picture.** Firmware flashed after a
  re-plug (the first upload lost the board off the USB bus).
  - **Task 3 — revalidation, two independent read-only boots** (`captures/s13-corrected-02/`,
    `captures/s13-corrected-03/`; `complete=1`, `turns=64`, `representation_observed=true`, both identical):
    **payload 36, initial-playback 8, restore-completions 0 (window-scoped), `committed_at_checkpoint` 9
    (`pass_backed` 9, `root_backed` 7)**. The content-bearing set is the same in both boots — `(0,0) (1,0)
    (1,1) (1,2) (2,0) (3,0) (4,0) (5,0) (5,1)` — and **every other payload slot has `passes=0`,
    `loop_length_ticks=0`, `committed_event_count=0` and no published root at all** (`kr=0` and `kc=0`; the
    currency instrumentation was added precisely to separate an absent root from a stale one).
  - **This contradicts the trace's per-slot premise** ("8 pass-backed, 28 published-root-backed"). The
    publish path installs a root whenever it adopts, so the correction falsifies the inference: **27 of the
    36 token-verified payload slots hydrate nothing**, and the corrected loaded count is **9, not 36** —
    `has_committed_content` is now the accurate count of a 9-slot working set, not an under-count of a
    36-slot one.
  - **Owner census (S1.1) on the same boot**: settled `chunks_free 450`, `slab_live 78`, `slab_used
    2,455 KB`, `slab_unused 40 KB`, `snap 2,155 KB`, `tag_total 404 KB` — flat 173→209 s, the same owners
    the trace recorded. The **owner total** holds; the **per-slot** attribution does not.
  - **S2b.3 — post-restore budget**: `post_restore_census free_chunks=450` vs required
    `peak_capture_reservation + 16 = 16` → **met**; reservation walk `512 → 480` (enabled) then `480 → 480`
    (library), `credits=0` on all 16 rows.
  - **Task 4 — S1.3 slope: BLOCKED at the first step, no refusal.** `--track 6 --slots 8 --first-slot 1
    --record-bars 4` (`captures/s13-slope-01/`) saved (6,1) (×4 `saved successfully`), rebooted, and the
    boot reports `Track 6 loaded state: EMPTY`, **N=36 (no new payload)**, with the owner census **identical
    to before the save**. The next reset did not take. So the saved scratch payload did not survive the
    reboot — the obstacle the sixteenth-session handoff recorded as removed, which repeatability of an
    untouched manifest never covered. **No `#MEM_EXT_REFUSED`, no `refused` terminal, `chunks_free` never
    near the reserve**: **S1.5 — refusal point stays open and the measured range is 0 added slots.** This is
    a fixture-path blocker, **not** a capacity failure.
  - **Recommended next step (not taken here):** resolve the save/restore survival of a *new* scratch
    payload; that is a state-changing investigation (write + reboot), not another read-only trace, so it is
    recorded as the blocker rather than started under the observation fix. Full evidence:
    [`ARCHITECTURE-REVIEW.md`](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)
    § *S1.3 — observability correction* → *Task 3* / *Task 4*.
- **Recorded-slot → persistence-target correctness (eighteenth session, second round) — bounded to that
  question only; no sub-change, no wider sweep, no clearing.** The record stop **admits a `LoopPersist` for
  the adjacent slot**: `captures/s13-target-02/` gates a take on track 6 loop 2 and the deferred worker's
  telemetry names `slot:5:0` (track 6, slot 1) with 7 payload slices; `captures/s13-survival-01/` shows the
  same key on the earlier run. **Instrumented and gated.** A capture-gated `RID` record carries the whole
  chain (start slot → stop slot → slot passed to `finalizeCommitSideEffects` → admitted persist key) once
  per take and is **critical-tier** because a Tier-A ring displaced it; one production resolver
  (`resolveRecordStopPersistTarget`) is now shared by both persist sites and the native regression;
  `scripts/hitl/record_stop_persist_target.py` (10/10) + `scripts/hitl_record_stop_target.py` **withhold the
  reboot** on a mismatch, and the native case
  `test_record_stop_persist_target_follows_the_recorded_slot` covers recording into a non-zero slot with the
  adjacent slot already holding content. Native **2,379 / 1 skipped / 0 failures**.
  **RESOLVED by one uninterrupted serial session** (`scripts/hitl_identity_one_session.py`): baseline
  counters → take → transport stop + stopped confirmation → second request, all on one connection before
  the close. Baseline `entered=0 enq=0 rej=0 tx=0 evict=0`; after the take `entered=2 enq=2 rej=0 tx=1
  evict=0`, last identity `take=1 t=5 ss=1 sp=1 cs=1 pt=5 ps=1 ev=a`. So **the emitter executes** (two
  entries = start + admission), **nothing was gated, disabled or rejected** (`rej=0`), and one of the two
  rows is enqueued but never drained (`enq=2 tx=1`) — the capture-visibility defect, localised to the
  ring's drain, which no longer blocks the identity question. **The chain correlates, so the
  "wrong target" attribution is withdrawn for this take:** the take recorded on `t=5 ss=1`, the stop
  passed `cs=1`, the admitted key was `pt=5 ps=1`, and the counters report the same slot — the persist
  target followed the recorded slot exactly. Combined with the same log showing **no `LoopPersist` for
  the take**, the earlier `LoopPersist slot:5:0` is **unattributed pending work from an earlier deferred
  save, not this take's admission**. (`slot` is zero-based in these records while the harness names slots
  1-based, so `ps=1` is the pinned user-facing "loop 2" — the mismatch was in my labelling.) Keeping the
  connection open sidesteps the command-channel stall, so **no retained-counter design is needed**.
  **Still open:** the payload path and the payload write are unverified for `(5,1)` (the take's
  `LoopPersist` did not run; `PERS` covers the deferred worker only), so the reboot stays withheld.
  Zero payload-read errors still only support **"no reported read failures"**, not "every empty payload
  was intentional". **Native/build gates are green; device identity verification remains open.**
- **2026-09-25 takeover correction — payload oracle remains unimplemented.** The unfinished
  `!SLOT_PAYLOAD` probe was withdrawn: it could not decode LSV1 and performed synchronous SD reads
  outside the storage scheduler. Its draft is backed up under `/tmp/runtime-takeover-20260925/`.
  The host fixture again streams notes and stops the take, requires fresh start/admission identities,
  uses the requested track channel, and fails closed on missing evidence. Seven production-orchestration
  host tests pass. No new device evidence, reboot, clearing, or payload claim. Next: establish a
  non-destructive recording fixture using the existing registered arm workflow, then verify stored
  event parity through the existing persistence reader before any reboot.
- **S2b.3 — post-restore budget stays open with its full arithmetic.** A settled boot's `capture_prewarm`
  walk is the **cold** reservation count, not peak capture reservation, so `450 ≥ peak + 16` was never
  actually evaluated; the term is the peak reservation **during the specified capture workload**, on a
  verified growing fixture. No checkoff from a boot marker.

**2026-09-24 — UNRESOLVED OBSERVATION recorded under `runtime-working-set-ownership` S4f (accepted interactions): the ordinary Playing stop route under repeated press. No firmware edit, no device run, no solution adopted.**

Full record: [`runtime-working-set-ownership/tasks.md`](../../openspec/changes/runtime-working-set-ownership/tasks.md) § S4f — *unresolved observation*. Short form: five `Overdub stop refused: stop_job_pending` rows in `captures/session_20260924_183229.log` (**CompanionBridge** ×3, **CanonicalTail** ×1, **CanonicalPrefix** ×1, all `dest=0` **Playing**); the row is emitted **after** `Track::latchAcceptedStop`, so it is not a denial verdict; the flashed revision is unidentified (no banner or `firmware_hex_sha256`); the 1,279-row telemetry tail to ~2255 s is continued execution, not evidence of a freeze; and `!STOP_STATUS` could not have answered the window at all (transport-gated twice, ring-head only, **no latch field**). Next: S4f.11 / S4f.13 on identified firmware with acceptance, job identity, worker progress and scheduler-service evidence. **Do not generalize the DEC-070 retarget fallback on this evidence** — its raw-only commit drops the display representation and the protected undo.

**2026-09-24 (sixteenth session) — `runtime-working-set-ownership` S1.3 — slope-fixture definition: the read-only trace FALSIFIES the "28 unloaded" premise and defines the controlled increasing-population fixture. No device run, no firmware edit, no sweep.**

Owner direction: continue directly in `runtime-working-set-ownership` (no new sub-change), start read-only,
and keep **S2b.7 — drain hardware confirmation** separate.

- **All 36 payload slots are loaded, not 8.** Boot offers every payload slot; all 36 reach
  `terminal/committed` (the RRQ log matches the observation's request/terminal columns exactly). The 8/28
  split is a **representation** split: `commitLoadLoopJobPublish` → `applyLoadedLoopSlotBodyCommit` branches
  on `usesLoopSnapshotBodyFormatV1`. **Legacy** payloads go through `Loop::adoptPersistedSnapshot` (moves
  `snapshot.passes` into `Loop::passes`) → `has_committed_content=1` (8); **LSV1** payloads install a
  **published snapshot root** (`loop.storePublishedSnapshot`) and leave `passes` empty →
  `has_committed_content=0` (28) even though playback reads that root
  (`Loop::tryGatherPublishedSnapshotEventsInWindow`).
- **The "unloaded" 28 are resident.** `#MEM_OWNERS` over the boot: `slab_live` 0 → 78, `slab_used` 0 →
  2,455 KB, `snap` 0 → 2,155 KB, `chunks_free` 480 → 453, then flat after ~12.8 s; the `capture_prewarm` walk
  reports `credits=0` on all 16 rows (canonical reservation flat, S2b.1), so payload/root residency is what
  grows.
- **The four required slots absent from the pass-backed set:** `(2,1)`/`(4,0)` are root-backed (loaded); the
  gap is representational. `(6,0)`/`(7,0)` are `enabledMask=0x1` but payload-absent and never offered
  (`Track 6/7 loaded state: EMPTY`) — a payload-less enabled slot, not a restore failure.
- **Observation accuracy:** payload/enumeration/request/terminal/initial-playback/window are accurate;
  `has_committed_content` / `committed_at_checkpoint` / `a,h,l,v` are **pass-backed only** and
  **under-report** root-backed slots — **not spec-conformant**; recorded as an observability finding owned by
  this change, and the fixture does not depend on it.
- **Fixture defined (not run):** fix one active role (transport stopped, no record/overdub), vary `N_payload`
  one slot per boot inside the approved scratch range (track 5 slot 1, track 6 slots 1–8), and report
  `N_payload` / `N_pass` / `N_root` separately with reservation, payload, root and directory residency; the
  failure point is the first refusal. **The slope run and failure point remain unmeasured.** Full trace:
  `runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § *S1.3 — slope-fixture definition* and
  `docs/Runtime/PROJECT_STATE.md` (sixteenth session).
- **Separate:** **S2b.7 — drain hardware confirmation** (the three transferred unchecked tasks) is untouched.

**2026-09-24 (fifteenth session) — `restored-library-stability` Packet B **2.2 — complete-manifest stability** DEVICE VERIFIED: PASS across three consecutive boots; **2.1** not exercised (nothing cleared); Packet C untouched.**

Owner direction: take **2.2 — complete-manifest stability** (compare the *entire* expected manifest across
three consecutive boots), **reuse the existing device captures first**, clear nothing merely to repeat
evidence already available, and keep **Packet C — S2b.7 drain hardware confirmation** independently
reportable and out of this handoff's critical path.

**Reuse first: the existing five `LIBOBS` captures settle two eligible boots, not three.** New read-only
tooling (`scripts/hitl/library_manifest.py`, `scripts/hitl_library_manifest_stability.py`, tests
`scripts/test_library_manifest_stability.py` **14/14**) compared them
(`captures/s13-libobs-stability-existing.json`): `full-02` and `incomplete-03` are eligible fresh boots;
`full-01` is the truncated pre-fix long-form wire; `incomplete-02` is an incomplete window; and
`incomplete-01` is **not a boot** — no `boot_reset`, no banner, `checkpoint_reached=False`, a mid-session
attach at ~72 s uptime on the **same boot** as `full-02` (its manifest sha equals `full-02`'s). So the
missing boots were captured rather than relabelling a same-boot observation as a boot.

**The gate.** Expected manifest recorded from the existing `full-02` (reuse); three consecutive fresh
read-only boots captured back-to-back (`captures/s13-libobs-stab-01/02/03`, `boot_reset` + banner,
`complete=1`, upload reset before each, only `!LIB_OBSERVE` sent, 0 `FATAL`, no destructive command).
**All three match in full — 0 manifest / lifecycle / settled-count differences over 64/64 slots — and all
four complete-manifest boots share the *observed* manifest sha256 `4e451a07d0a94c18…`**; 36 payload /
8 initial-playback / 8 committed; `turns=64`, `restarts=0`. **Recorded precisely: no differences in the
observed fields; no destructive commands issued** — an identical field record (or an identical file hash)
does not prove unchanged bytes or the absence of a write, so the write statement rests on the command log
and the manifest statement on the observed fields. The **36-vs-8 gap is dispositioned** (36 payload-present
/ 8 in the **pass** representation / 8 initial-playback-required; the two 8-sets differ, overlap 4; the
remaining 28 are payload-present and **published-root-backed**, distinct from the other 28 payload-absent
slots), and the runtime slope
fixture varies the **loaded** population and reports the two representations separately. Report
`captures/s13-libobs-stability-3boot.json`, identity `captures/s13-libobs-stability-3boot.identity.json`
(`git_commit 74cadc7b`, `firmware_hex_sha256 ede704b614b7e9cd…`). No firmware edit → no G2 packet; recipe is
`hitl-content-verification` § 9. **2.1 is closed as not required for the reused fixture** (nothing cleared);
**2.3 (S2b.7 condition), Packet C and 4.2 are TRANSFERRED to `runtime-working-set-ownership` S2b.7** (recorded
there unchecked), so the closeout checklist has no remaining mandatory library task and the change is
**archived**. **[Answered 2026-09-24 (sixteenth session, inferred): the "28 unloaded" premise was said to be
falsified — they were read as published-root-backed and resident; the S1.3 fixture is defined. CONTRADICTED
2026-09-24 (seventeenth session, on device): the corrected per-slot telemetry reports 9 content-bearing
slots of 36 verified payloads, so this gate's `committed_at_checkpoint=8` was a subset only of those and its
three-boot
result is repeatability of the fields measured, not validation of that count.]** Full record:
[`restored-library-stability` (archived)](../openspec/changes/archive/2026-09-24-restored-library-stability/ARCHITECTURE-REVIEW.md)
§ Packet B and `docs/Runtime/PROJECT_STATE.md` (fifteenth session).

**2026-09-24 (fourteenth session) — `restored-library-stability` Packet A 1.0–1.4 landed (observation-only firmware + read-only analysis); 1.5 open on upload authorization.**

Owner direction: proceed with Packet A. **1.0** execution packets for 1.2/1.3 recorded in the change's
`ARCHITECTURE-REVIEW.md` before any firmware edit. **1.1** measurement corrections recorded with citations,
and the live claims in `runtime-working-set-ownership` S1.3/S2b.3/the S2b gate corrected; the historical
paragraphs in this file and `PROJECT_STATE.md` carry an explicit correction marker. **1.2** the pure
classifier (`include/StorageManager/LibraryObservation.h`, `src/StorageManager/LibraryObservation.cpp`):
observed fields, a **current state set**, a **nullable cause** from six values, `empty` never a cause —
13 native cases green (`test/test_library_observation/`) plus a 13-test Python mirror, with a
mutation-proof (the `unknown` fallback → `content loss` fails L01/L02, then reverts byte-identical).
**1.3** the bounded on-demand observation
(`src/StorageManager/LibraryObservationSession.cpp`): owned by `StorageManager`, serviced at the
**unconditional observation tail in `main`** (never the admission-gated load/restore frame), requested by
`!LIB_OBSERVE`, emitted as the distinct `LIBOBS` record family; **2 slots / 2 SD probes / 2 records per
invocation**, SD-session admission respected, `(generation, epoch)` **plus a content revision identity**
re-checked immediately before each record is emitted, bounded epoch restarts; **8 native cases green**
through the real service hook (O01–O08), two mutation-proofs reverted byte-identical; **full native
2,373 / 1 skipped / 0 failures**; `teensy41-capture-serial` SUCCESS keeping the **12-bank** ITCM line
(`code 391,160 + padding 2,056`, locals 34,880; floor 840) — the first build without flash placement
crossed a 13th bank and the placement was fixed rather than the budget re-based; the observation costs
**+80 B of ITCM**. **1.4** read-only re-analysis
(`scripts/hitl_library_observation_report.py` → `captures/s13-library-observation.json`; the sweep parser
now records `library_observation` + `n_committed_slots_window_scoped`): **no loss cause anywhere in either
sweep**; N is window-scoped (27/27/26 on `s13-slope-02` closing ~12.85–12.93 s vs 29/29/29 on
`s13-slope-final` closing ~14.6–15.4 s); the other three quantities are `None` with the missing fields
named. **1.5 DEVICE VERIFIED (read-only)** — `teensy41-capture-serial` uploaded (owner-authorized; the only
reliable reset) and `scripts/hitl_boot_census.py --library-observe` run on
`/dev/cu.usbmodem154944801`; the only command sent is `!LIB_OBSERVE`, no CLEAR/rename/quarantine/delete and
no transport, 0 `FATAL`. **Result** (`captures/s13-libobs-full-02/`): **payload inventory 36, initial
playback-required 8, restore completions in the window 0, committed at checkpoint 8**; window
`[13,017,777, 13,312,720]` µs, `turns=64`, `admission_complete=1`, `every_initial_observed=1`,
`consistent_view=0` (cross-turn, marked), `complete=1`. **36 `populated` / 28 `empty` and no loss cause
anywhere**; the initial-playback set is one slot per track — `(0,0) (1,2) (2,1) (3,0) (4,0) (5,0) (6,0)
(7,0)` — all committed, with payloads on tracks 0–5 and none on tracks 6–7 (the recorded
`Track 6/7 loaded state: EMPTY`, now per slot). The incomplete-window case
(`captures/s13-libobs-incomplete-02/`, early close 0.15 s) reports **incomplete with all four counts
`None`**. **The first device run found a real defect:** the `LIBOBS` record used full enum names, overran
the 192-byte capture cap and was **silently truncated**, so a completed observation read `complete=False`;
the wire form is now compact codes with one/two-character keys, with the host parser and a locking test
updated, and the run repeated. **Packet A (1.0–1.5) is complete**; Packets B/C and the 4.x
handoff/closeout remain open. Nothing outside § Now implementing was implemented.

**Same-session follow-up — DEC-083 and the capture-tier fix.** The contract is recorded as **DEC-083 — the
restored-library inventory is a bounded on-demand observation, separate from the settled census**. The
truncation the first device run found is locked by test: `LIBOBS` is **Tier-A** in
`CaptureLineTier::isTierALine`, with `test_library_observation_lines_fit_capture_text_budget` asserting
worst-case records ≤ 191 bytes (`test_capture_line_tier` 15/15; full native re-run **2,374 / 1 skipped / 0 failures**; capture-serial RAM1 unchanged at `code 391,160 + padding 2,056`). Durable docs updated:
`docs/Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md` § *The on-demand library observation*, a `BOOT_LOAD.md`
key-files row, and `.agents/skills/hitl-content-verification` § 9.

**2026-09-24 (eleventh session) — S2b.8 — set-change epoch / external resets — LANDED native-only in `runtime-working-set-ownership`; documentation reconciled first.**
Owner direction: take S2b.8 before S2b.7, with reconciliation first. **Reconciled:** `tasks.md` no longer
links the archived `deferred-slot-restore-liveness` as a live sibling, and the transfer (S1.1/S1.3/S1.4/
S1.5/S2b.3/S2b.5 plus the new S2b.7/S2b.8) is recorded in the change. **Landed:** undo-clear and redo-clear
are **decided to be external resets** and advance the restore-request generation (DEC-082 clause 1, no new
DEC); `noteRestoreSetEpoch` marks `superseded` under each request's **original** identity *before* advancing
the epoch and emits its `RRQ` transition (two real defects the audit found); the dead non-emitting
`invalidateAllRestoreRequestsAsSuperseded` was removed. **Not faked:** no runtime set-change entry point
exists, so none was invented (the set boundary is boot's full reset); "a different owner committing that
slot" already terminates as `unnecessary` + pre-publication rejection and was not re-wired. **Evidence:**
full native **2,348 / 1 skipped / 0 failures**; `test_storage_manager_load` 35/35,
`test_drain_reentry_callback_safety` 8/8; capture-serial SUCCESS `code 391,080 + padding 2,136` (1,296 B
above the 840 floor); both fixes proven discriminating by removal. **No device run** — nothing
hardware-observable. **Next: S2b.7 — drain-driven restore advancement** (characterize advancement vs a
correctly named wait; commit gate not relaxed). Packet: `ARCHITECTURE-REVIEW.md` § S2b.8.

**2026-09-24 (thirteenth session) — the restored-library instability now has an owner: `restored-library-stability` PROPOSED, planning complete, Packet A (observation-only firmware + read-only analysis) starts immediately.**
Owner direction: give the instability an owner rather than another drain test, because it blocks the
measurements and the later runtime stages. **Opened** `openspec/changes/restored-library-stability/`
(proposal + design + `lazy-slot-hydration` spec delta + tasks; `openspec validate` clean; **no implementation
started**). **Scope:** own the instability; report an observed **current state** (`empty` / `populated` /
`pending`) **separately** from a **nullable historical cause** (**catalog omission /
restore refusal / intentional eviction / content loss**, plus `ambiguous`,
`unknown`), with evidence and stated precedence (`empty` is never a cause; `pending` is current state, not a
cause); add a per-slot
**observed-state** inventory (payload + token, boot enumeration, request state/wait/terminal with full
identity, committed content, selection) reporting **four distinct quantities** with an explicit observation
**window**, collected through a **bounded on-demand observation** with a declared owner and a service point
that runs **independently of normal load admission**, SD-read admission and an invalidation rule covering a
request-generation change, a set-epoch change **and a content revision change** (a publication installs
content under the same request identity, so it changes neither) — never carrying content fields forward; define **one** repeatable fixture inside the already-approved
scratch range (track 5 slot 1, track 6 slots 1–8), observation-only firmware first, with any CLEAR needing **per-slot owner
authorization** and stability proven by a **complete per-slot manifest comparison**; and carry the **S2b.7
device confirmation** as an independently reportable packet. **Handoff, not absorption:** when the library is
stable, `runtime-working-set-ownership` finishes S1
and the S2b device gates, then starts S3 — this change does not start them.
**First findings, already recorded (all read-only) and they correct the previous attribution:**
`MidiLooper/sets/index.bin` is `SavedSetCatalog::SetIndex{ uint32_t nextSequence; }` — a saved-set folder
counter, **not** a slot catalog; the boots that appeared to shrink (`captures/s13-slope-02/`) report
**`reconcile_warnings=0`**; `DIAG,lcr,skip,restore` is a **content-resolution** line
(`TrackDeferredMaintenance` / `LoopContentResolution`), not a library marker; `Track 6/7 loaded state: EMPTY`
appears on the **stable** `N=29` boots too. Also: both sweeps' `library_rows` are **all-zero** (16 rows,
`phases: 0`), so no per-slot inventory was ever captured — the gap Packet A closes.
**G2 implementation-contract review 2026-09-24: BLOCKED, then corrected — four contract fixes.** (1)
`n_committed_slots` is a **window-scoped count of observed `RRQ … committed` completions**, background
restores included — **not** the boot playback set, so the earlier explanation for `N=29` is **withdrawn** and
subtracting a playback set from the payload set does **not** establish loss; four quantities + the window are
now reported and incomplete windows are tested. (2) `emitPostRestoreCensusIfReady` is once-per-boot and needs
`bootInteractiveReady()` (empty queue, no live session, both jobs inactive), so a waiting request suppresses
it — a **bounded on-demand snapshot** independent of those gates is now required, with incremental collection,
snapshot consistency and capture framing, while the settled census stays for settled memory figures. (3) A
forced one-of-four classification could not be truthful — observation is now separate from a **nullable**
attribution, with full request identity, completeness and no single-snapshot historical claim. (4) One scratch
slot surviving three boots does not establish stability — a **complete per-slot manifest comparison** is now
required and any unexplained difference is a failure.
**G2 second round 2026-09-24: still BLOCKED on two remaining contracts, now corrected — no broader redesign.**
(a) The classifier had **contradictory mandatory outcomes** (an absent payload with no request and no history had
to be both `empty/not applicable` and `unknown`); the axes are now explicit and separate — observed **current
state** (`empty` / `populated` / `pending`, a set) versus a **nullable historical cause** — `empty` is never a
cause, a prior inventory establishes only the disappearance (cause stays `unknown` without an explicit event),
and a single test with exactly the overlapping input is required. (b) The incremental observation had **no
ownership or invalidation contract**; it is now owned by `StorageManager`, serviced at a declared **existing**
per-turn service point (no new thread/timer), respects the existing **SD-session admission**, and **invalidates or
restarts** the affected observation on a request-generation advance or a set-epoch change — never mixing fields
from different generations, never retaining a content reference across slices, with unavailable fields left
`unknown` while the waiting request is still named, plus a native **mutation-between-slices** test. Two
tightenings: task 3.1 defines the **progress measurement interval as inside the drain call**
(`restoreProgressCounter()` sampled immediately before entry and after return), so an urgent-save discharge alone
does **not** check off S2b.7; and Packet A is renamed **observation-only firmware and read-only analysis** with an
unambiguous execution-packet requirement (mandatory for 1.2/1.3, not for the pure analysis tasks).
**G2 third round 2026-09-24: one remaining BLOCKER — request identity does not identify the content version.**
`commitLoadLoopJobPublish()` publishes restored content **under the same `(requestGeneration, setEpoch)`** that
later records its `committed` terminal, so an observation spanning that publication could combine
pre-publication fields with post-publication content without any invalidation firing; ordinary content changes
had the same gap. The invalidation key now also carries a **content revision identity**: a slot's observation is
invalidated or restarted when its committed content changes — including a publication under an unchanged request
identity and any ordinary commit (capture / overdub / edit / clear) — content fields are never carried forward,
and a native **publication-between-slices** test (generation and epoch deliberately unchanged) must prove no
mixed record is accepted. Identity choice (design D2): re-read committed pass count, highest committed pass id,
loop length and committed event count at each slice — `Loop::nextPassId_` alone is rejected because `SlotReset`,
the boot load and the edit-pass restore reset it. Two tightenings: the declared service point must run
**independently of normal load admission** — the unconditional observation tail in `main` (the idle-owner
position that already calls `processHitlSerialCommands` / `emitPostRestoreCensusIfReady`), **not** the
admission-gated deferred load/restore frame — and its test must drive the **actual service hook** with load
admission blocked, not a directly invoked collector; and `pending` was removed from the historical-cause
vocabulary (it is current state).
**Observation-only attribution may proceed; the corrected
measurement definitions come first, because they may explain part of the apparent instability without a
firmware fix.**
**Wording recorded:** `runtime-working-set-ownership` has **no *currently eligible* native task** left; the
stage order is not bypassed because hardware evidence is blocked. Packets:
`restored-library-stability/proposal.md`, `design.md`, `tasks.md`.

**2026-09-24 (twelfth session) — S2b.7 DELIVERED: the blocking drain now SERVICES the restore it waits on.**
Owner direction: continue S2b.7, i.e. deliver the advancement. **Measured first, then implemented** (the
measurement is kept in the task text so the two are not confused): with another track playing, a restore
mid-preparation holding the SD session and a save owed, the pre-change drain returned `TemporarilyBlocked` /
`slot_load_active` with progress **1 → 1** and phase **1 → 1** — reported, never advanced. **Landed:**
`StorageManager::serviceDeferredRestoreForDrain()` runs the **same** selection/admission + bounded step the
deferred-load frame uses, and the drain calls it once per iteration while
`deferredSaveBlockedByActiveSlotLoadSd()`; a **termination clause** (D4 point 6) reports the named wait as soon
as the restore is gated instead of burning the budget. No new owner, loader, queue or admission policy, and
**the commit gate is untouched** (task 3.3 still owns the bound). D4 point 1 (parked instance) needed no new
code: the existing selection path already promotes it — an explicit promote was written, **proven redundant by
removal**, and deleted. **Behavior now:** another track playing → progress **1 → 8**, `TemporarilyBlocked` /
**`waiting_commit_gate`**, nothing published, request preserved, save owed; focused slot with the gate
admitting → the drain commits the restore and discharges the save; parked-only session → serviced, nothing
published. **Evidence:** `test_storage_drain_restore` **10/10**, `test_drain_reentry_callback_safety` **8/8**
(now links `DeferredJobScheduler.cpp` — a build fix the real drain required), full native **2,352 / 1 skipped /
0 failures**, capture-serial **SUCCESS** `code 391,080 + padding 2,136`. **Discriminating by removal:**
disabling the drain's service step fails exactly the three advancement assertions; reverted byte-identical.
**Device: NOT RUN** — every claim is native; no device class claimed. **S2b.7 is checked**; no native task
remains open in this change. Packet: `ARCHITECTURE-REVIEW.md` § S2b.7.

**2026-09-24 (eleventh session) — S2b.8 DELIVERED (external resets); S2b.7 first MEASURED as absent.**
S2b.8: undo-clear/redo-clear are decided to be external resets and advance the restore-request generation;
`noteRestoreSetEpoch` now records `superseded` under each request's **original** identity before advancing the
epoch and emits its `RRQ` transition; the dead non-emitting `invalidateAllRestoreRequestsAsSuperseded` was
removed. S2b.7 was then measured (not delivered at that point): the drain named `slot_load_active` and did not
advance the restore. Evidence: full native 2,348 → 2,350; `test_storage_manager_load` 35/35;
`test_drain_reentry_callback_safety` 8/8. Packets: `ARCHITECTURE-REVIEW.md` § S2b.8 and § S2b.7.

**2026-09-24 (tenth session) — `deferred-slot-restore-liveness` ARCHIVED (reduced scope); runtime measurements transferred to `runtime-working-set-ownership`.**
Per owner direction this was a **scope correction, not another sweep**. Delivered and archived: preparation
under transport; request identity with named waits + four terminals + stale-identity rejection; the census
checkpoint; content PASS; CLEAR's permitted completion branch (A3.7 both shapes). **Transferred unchecked**
to `runtime-working-set-ownership`: S1.1, S1.3, S1.4, S1.5, S2b.3, S2b.5 and the new **S2b.7** (drain-driven
restore advancement — explicitly not proven) and **S2b.8** (set-change epoch). A3.1/A3.3 closed with the
advancement clause **explicitly deferred** to S2b.7; A3.8's claim corrected so it no longer covers that open
prerequisite; A2.1 closed with the set-epoch residual deferred to S2b.8; A2.7's recording completed. `DEC-082`
records the ruling; `docs/DELIVERABLE_TRACKING.md` carries the archived scope. **Next: resume
`runtime-working-set-ownership` implementation**, whose remaining measurements and the deferred behavior are
all visible and mandatory there.


**2026-09-24 (ninth session) — A3.7 (both shapes) and A3.8 are CLOSED; A5.1's slope is the last open mandatory item.**
Owner direction accepted the priority-bypass completion as the **completes** branch. Narrow record: **CLEAR
completed through workspace-save priority while a started restore remained commit-gated**
(`captures/a37fix-04/`: `track2/slot1/gen0/epoch0` observed `running` 8.246 s → unresolved `waiting_commit_gate`
at the 27.070 s edge → committed 35.201 s; channel 6 emits 9 note events before and 6 after the drain,
target channel 5 emits 0 after; outcome `State saved successfully (v4)`). The analyser was corrected to
iterate **chronologically** (device-time sorted, not file order), key transitions by the **full request
identity**, require an **observed `running`**, exclude the control channel from playback attribution, and gate
on the outcome (a capture-block refusal is not shape (b)); `a37fix-01/03/05` are INCONCLUSIVE and not counted.
**A3.8 closed:** native 2,344 / 1 skipped / 0 failures, A3.1–A3.6 suites green (`test_storage_manager_load`
33/33, `test_drain_reentry_callback_safety` 6/6, `test_storage_drain_restore` 6/6,
`test_persistence_sync_drain_budget` 15/15), RAM1 `code 391,064 + padding 2,152`, A3.7 evidence for both
shapes. **Not claimed:** drain-driven restore advancement or active-transport commit safety —
`resumable-midi-boundary-playback-maintenance` **task 3.3** stays open. **A5.1 remains the last open mandatory
item** (slope blocked on the scratch range); archive stays gated.


**2026-09-24 (eighth session) — the two A3.7 shape-(b) review issues are resolved; the gate stays open by owner direction.**
(1) *Why the drain completed with a commit-waiting restore:* not an absent session —
`deferredSaveBlockedByActiveSlotLoadSd()` is `SlotLoadSession::isActive() || loadLoopJobHasOpenSdFile()` and a
commit-waiting job keeps its session. The scheduler's slot-load gate is **bypassed for an urgent owed
workspace save**: `processDeferredSaveState` returns early only when the gate is live **and** there is no
in-flight work **and** `!shouldPrioritizeWorkspaceSaveDrain()`, and the latter is true when the save is
`pending` with queued/in-flight work (the capture logs `PERS,request,…,sync_drain_already_pending` +
`FinalizeWorkspace`). So the run shows the **completes** branch under an urgent-save priority, not a drain
forced to wait on the restore. (2) *The analyser is corrected:* every check orders by **device time**
(`#CAP,<micros>`; ring flush means file order ≠ time order), playback is attributed by **MIDI output channel**
(`MO`) rather than the untracked `ST,Track`, a non-target channel must emit notes **before and after** the
drain, and the blanket stop exclusion is gone. Re-analysis of `captures/a37fix-04/`: channel 6 emits 9 notes
before and 6 after the drain, target channel 5 emits 0 after. **A3.7 and A3.8 stay open** — the owner's
reading of the urgent-save-priority distinction is the remaining question. No firmware changed; route off;
track 3 untouched.


**2026-09-24 (seventh session) — the A3.7 shape-(b) fixture is built and a passing run is recorded (pending owner confirmation).**
`scripts/hitl_a37_shapeb_fixture.py` runs shape (b) with **reload-on-clear disabled** and gates its verdict
on the four preconditions, through the intended boot-restore entry point. Passing run
`captures/a37fix-04/`: all four held — target content 2 bars before the press; `RRQ,1,0` (track 2 slot 1)
`running` 8.246 s → `waiting_commit_gate` 10.637 s → committed only at 35.201 s (after the 27.070 s CLEAR);
`Clear waiting for deferred save completion`; `Playback started` at 9.849 s with no intervening stop — and
the drain **completed** (`State saved successfully (v4)` → `Clear selected slot 1`), the "completes" branch
A3.7 allows. **Fixture bug fixed:** `#CAP` lines flush from a PSRAM ring, so file order is not device-time
order; every check now orders by device time. **Caveats:** the restore was in the named
`waiting_commit_gate` wait rather than holding the SD session (owner's call whether that satisfies "restore
in flight"), precondition 4's evidence is sequence-based, and runs `a37fix-01/02/03/05` were `INCONCLUSIVE`
with the failing precondition named and are not counted. No firmware changed this session; the route stays
disabled and track 3 was not touched.


**2026-09-24 (sixth session) — read-only trace: `committed` does not imply nonempty content; reload-on-clear stays disabled.**
The "restored yet empty" mismatch is answered **read-only** (no device mutation; track 3 untouched).
`commitLoadLoopJobPublish` records `Committed` unconditionally after `applyLoadedLoopSlotBodyCommit`, and
`Track::hasDataInSlot` → `Loop::hasData()` = `hasCommittedPasses() || loopLengthTicks > 0 ||
!capture.store.empty()`, so a `committed` terminal followed by `hasDataInSlot == false` is **consistent** —
it is an empty payload that published successfully. Traced on the same slot and identity
(track index 2 / slot index 1): sweep persisted `LoopPersist,slot:2:1`; boots restored `RRQ,2,1,0,0`
(gen 0) committed; `cr-probe-01` CLEAR at 20.772 s was a real clear; its persistence rewrote the payload at
23.71 s; the route-enabled reload `RRQ,2,1,1,0` (gen 1) committed at 29.927 s; `a37b2-01` then restored an
empty-but-committed slot. **Owner of the condition: the reload-on-clear route**, which stays **disabled** —
CLEAR unexpectedly restoring old SD content would violate its behaviour. One datum is left open and not
smoothed over: a 29.760 s `loop length CC feedback: 4 bars (3072 ticks)` disagrees with the later empty
state and needs a content/length line for the named slot (read-only). **Fix claim narrowed:** the oversized
bar-index allocation is **bounded**; the **producer** and **adopted-content integrity** (the invalid event
stays in its chunk for every other reader) remain unresolved. Full trace:
`deferred-slot-restore-liveness/REVIEW.md` § read-only trace.


**2026-09-24 (fifth session) — `DEC-082` records the restore-liveness ruling; A5.4 closed; the change stays open on A3.7 shape (b).**
[`DEC-082`](../DECISION_LOG.md#dec-082--the-restore-request-is-the-unit-identity-named-waits-preparation-under-transport-and-a-three-way-drain)
records the ruling in four clauses with **per-clause evidence**, and marks the drain-advance **device
integration (A3.7 shape (b)) as NOT VERIFIED** inside its own evidence table, so the decision record does
not pre-claim it. A5.4 is checked off; A5.5's closeout table now lists A5.2/A4.3/A5.4 taken+green, A4.2's
malformed half the one optional-unrun, A5.3 partial, and **A3.8 and A5.1 open**. Also: the "restored yet
empty" mismatch is **not a defect** (the probe's CLEAR wrote an empty payload and the reload committed it),
but it names a route quirk — after a CLEAR the stale request makes `needsSlotLoad` true and re-requests the
just-emptied slot. **Waiting on the owner:** enable reload-on-clear in the change, and authorise clearing a
defined scratch subset for the shape-(b) target. No further sweeps or SD cleanup were run.


**2026-09-24 (fourth session) — the corrupt-tick raise is intermittent, and the shape-(b) fixture could not hold its condition.**
Re-running the a37b sequence with the guard produced **no** raise (no refusal, no `#MEM_EXT_REFUSED`, no
FATAL, `captures/prod-probe-01/`), so the corruption is **intermittent/state-dependent**, not deterministic
— and that run left the reload only `queued` (the scratch track was still overdubbing), which is not a
restore in flight. A second construction (`scripts/hitl_a37_shapeb_v2.py`, route temporarily enabled,
reverted byte-identical) aimed to put a `LoadLoopJob` in flight at the save-owed edge, but its reload target
was already RAM-empty at the first CLEAR (`Clear ignored — selected slot is empty`), so no reload queued;
re-selecting it then started an empty record (`length=0`) and the final CLEAR found no owed save
(`captures/a37b2-01/`). **Conclusion:** closing A3.7 shape (b) needs the reload-on-clear route enabled
**deliberately in the change** (with the guard) plus a target whose committed content is verified at the
CLEAR edge — an owner decision on the route, not another script iteration. It also surfaced that a slot
restored as `RRQ,2,1,0,0,committed` read as empty to `hasDataInSlot`; that disagreement needs understanding
first. Device disclosure: track 3 slot 2 was already empty; track 5 slot 1 (approved scratch) holds a failed
empty-record attempt.


**2026-09-24 (third session) — the oversized bar-index allocation is bounded at its owner (`LoopEventStore`); the producer and adopted-content integrity remain unresolved, and `deferred-slot-restore-liveness` stays open.**
`22369620 = 4 × 5,592,405` and `22369612 = 4 × 5,592,403` are a 4-byte-element vector: `LoopEventStore`'s
`barFirstIndices_` (`std::vector<size_t, InternalHeapFirstAllocator<size_t>>`). `appendToTailChunk` and
`rebuildBarIndex` both sized it from `event.tick / BAR_TICKS` with **no bound**, and
`floor(0xFFFFFFFF / 768) = 5,592,405` matches the observed count exactly — a tick near `UINT32_MAX`, i.e. a
torn/uninitialised value, not content. The index is rebuilt from chunk events after `clear()`/SD-load
staging, so one corrupt tick sizes it in a single `resize`. **Fixed at the owner:** a 4096-bar ceiling
(`MAX_INDEXED_TICKS = 4096 × BAR_TICKS`, far above the 64-bar target), `append` refusing a larger tick as
`LoopEventStoreAppendDeny::TickOutOfRange` (named and logged, deliberately not silently clamped), and a
defensive skip in `rebuildBarIndex` for adopted content. Reproducer red→green
`test_out_of_range_tick_cannot_size_the_bar_index`; full native **2,344 / 1 skipped / 0 failures**;
capture-serial SUCCESS `code 391,064 + padding 2,152`. Recorded in
`runtime-working-set-ownership/ARCHITECTURE-REVIEW.md` § P1. **Open:** the corrupt tick's **producer** is
not identified, and the obvious hypothesis is now **falsified** — with the route temporarily enabled, a
bounded probe (`captures/cr-probe-01/`) cleared three populated slots and the reloads committed cleanly
with no `out-of-range tick` refusal and no FATAL. The trigger is **intermittent**: re-running the
full a37b sequence with the guard in place raised nothing at all (no refusal, no FATAL,
`captures/prod-probe-01/`) and left the reload `queued` rather than in flight, so it did not exercise
A3.7 shape (b) either. The next step is to catch the raise — repeat, add allocation-site context, or
instrument the SD-load staging path (the only `rebuildBarIndex` caller outside `append`). Side effect disclosed: clear-then-reload re-commits the slot, so the probe did not
remove the track 3 slots 2-4 content. The reload route stays off in
the change, so `deferred-slot-restore-liveness` A3.7 shape (b) and archive remain open. Its
"clear-then-reload is the only reachable route" wording is corrected to "the route tried".


**2026-09-24 (second session) — census checkpoint finished and verified; A2.1/A1.8 closed; A3.7 shape (b) blocked.**
The **S1.3/S2b.3 post-restore census checkpoint** is done and fresh-boot device-verified
(`captures/census-final-02/`): one timestamped Tier-A
`#CAP,<micros>,DIAG,post_restore_census,…` marker whose readiness set now includes
**deferred-prewarm idle** (`main`'s prewarm request moved into `TrackManager`) and which reports
`free_chunks=451`, `heap_free=77,824`, all five readiness fields `1`, and S2b.3's `>= peak + 16`
condition met with **0** reconcile failures. `scripts/hitl_boot_census.py` records the readiness
attestation and the timestamped form, so a degenerate marker is a recorded failure. A4.2's
healthy-index half is green from the same log; the malformed half stays optional unrun.
**A2.1/A1.8 closed** (one residual recorded): `waiting_capture` / `waiting_job_capacity` are named,
publication revalidates the captured request identity before the token reset, a user CLEAR advances
the generation and records `superseded`, and `RestoreStateProbe` reports state/wait/terminal for the
head and both instances. Residual: no production set-epoch wire. Evidence: full native
**2,343 / 1 skipped / 0 failures**; `test_storage_manager_load` **33/33**; capture-serial SUCCESS
`code 391,000 + padding 2,216`. **A3.7 shape (b) re-attempted and still OPEN:** the only reachable
"restore in flight" route is clear-then-reload, which trips
`#MEM_EXT_REFUSED bytes=22369620` → external-pool FATAL (pool is 8,380,416 B); the route is therefore
not enabled by this change, the allocation defect is a `runtime-working-set-ownership` finding, and a
plain-take control records cleanly. **This change is not archive-ready (A5.5): shape (b) is mandatory
and untaken.** **A5.2 is CLOSED:** the base preset was the blocker, not the device — a direct clock+note
stream (`scripts/hitl_record_content.py`) records cleanly and the skill pipeline passes on the current
firmware (`captures/cc-03-playback/`: `result: PASS`, storage equality true,
`CAPTURE_CONTENT,begin,track=5,slot=0,length=1536`, **11/11 cycles `inventory_equal` and `exact_order`**,
0 missing/extra/unexplained, 0 overflow; parity PASS, not a capacity re-stress). The base-preset failure
(`captures/cpv-final/`) stays a separately tracked harness/device interaction and must not be generalised.
**A5.1 partial:** the updated sweep reports the post-restore budget stable at `N=29`,
`post_restore_free_chunks=452` of 512 across three fresh boots with 0 reconcile failures; the slope itself
is not measured because N is the boot playback set (library-independent), so recording into an unselected
scratch slot cannot raise it — the fixture must select/enable each recorded slot before the reboot.
**[WITHDRAWN 2026-09-24, G2 review: "N is the boot playback set" is wrong — `n_committed_slots` is a
window-scoped count of observed `RRQ … committed` completions with background restores included, so it is
neither the playback-required set nor the library; see `restored-library-stability` design D4.]** (An
earlier note blaming the sweep's track selection is withdrawn: the 0-based walk indices were misread and
the log shows `Selected track 6`.) Details: `deferred-slot-restore-liveness/REVIEW.md` § 2026-09-24
(second session).

**S1.3 slope — capacity failure point found, outside the approved range (disclosed).** A second sweep into
empty slots (`captures/s13-slope-02/`, track 3 slots 2–4) grew the payload set while the committed
boot-playback set **shrank** (N 27→27→26): each recording evicted other committed payload slots (track 6
lost slots 7–8, then 6, then 4–5). The marker's `None` is a window artifact (sweep closes at 10 s, marker
lands ~11.8 s), withdrawn as evidence. The approved range (track 5
slot 1, track 6 slots 1–8) was fully populated, so the sweep wrote to **track 3 slots 2–4, outside it**; no
SD delete/rename and no CLEAR were used, but removal needs explicit permission. **Growth sweeps are stopped**
pending an extended scratch range or clear permission.


**2026-09-24 — A3.5 RESTRUCTURED (topology conflict found by the device gate) and A3.7 shape (a) PASSED.**
The A3.7 device run surfaced a real conflict: A3.5's holdoff wait called
`midiHandler.handleMidiInput()` from inside the drain, and the HITL timing verifier
(`scripts/hitl/verify/midi_boundary_timing.py`) requires every input drain to live in `src/main.cpp` with
exactly **nine** call sites — backed by `ARCHITECTURE_RULES.md`/`NAMING.md`'s definition of the interval
path. The drain runs *on* that path, so servicing MIDI there moved an input drain into the interval; the
gate reported `unexpected_midi_input_call_site`. **Restructured (option 1):** the drain no longer waits the
holdoff out or services MIDI — it detects the finite holdoff as the sole blocker, classifies it, and returns
the named `post_load_holdoff` immediately, preserving the restore and the owed save. The holdoff expires on
its own 100 ms deadline while `main` services MIDI; the caller refuses by name (A3.4) and the next attempt
completes the save. The wait helper and its bound cases were removed; the call-site scanner now skips
whole-line comments. Verified: topology gate **9/9 no extras**, `test_storage_drain_restore` **6/6**, full
native **2,337 / 0 failures**, capture-serial **SUCCESS** `code 390,936 + padding 2,280`.
**Device gate A3.7 shape (a) "nothing else sounding" PASSED** (`captures/a37-clear-05-fresh/`, fresh boot,
24-bar take): `Clear waiting for deferred save completion` → **`State saved successfully (v4)`** →
`Clear selected slot 1` in <243 ms, **0** `Clear aborted` / **0** `Persistence drain stuck` / **0** `drain
blocked`, timing verifier `call_site_gate_ok: True` and `overflow_free: True`. **Shape (b) "another track
playing" completed but the save was already done at press time — recorded partial, not claimed.** New
driver: `scripts/hitl_clear_drain_a37.py`. Incidental: the `base` preset stops a 2-bar take to `EMPTY`
instead of `PLAYING` (present before this session's changes), so it is not a Packet C regression.

**2026-09-24 — A3.6 DEC-081/capture preservation LANDED (Packet C native work complete).**
Three regression cases in `test_storage_drain_restore` against the real deferred-save scheduler: audible
playback is **not** a persistence gate (a `TRACK_PLAYING` track leaves
`isCaptureRecordingForPersistence()` false and the classifier names no denial — DEC-081 holds); a
recording track **parks the multi-frame writers** (nothing queued, no write started, the owed save stays
owed); and the **mid-pass one-chunk flush still runs during a take** (a real sealed chunk is serviced,
queue depth drops, multi-frame writers stay at zero, and the take path returns before A3.5's holdoff wait).
**Harness defect fixed:** the in-memory SD stub classified the raw mode argument, so the seal journal's
`FILE_WRITE | O_CREAT` fell through and got an invalid handle — the mid-pass flush was unobservable
natively. The stub now classifies by the flags that carry the meaning (`O_AT_END` = append, `O_CREAT` =
create). Full native **2,340 / 1 skipped / 0 failures**; capture-serial **SUCCESS**
`code 391,080 + padding 2,136`. **Packet C's native work is complete; only the A3.7/A3.8 device gate
remains, which needs the board.**

**2026-09-24 — A3.5 holdoff MIDI servicing LANDED (next: A3.6, then the A3.7/A3.8 device gate).**
The finite post-load commit holdoff is now **waited out, not aborted**. One declaration
(`LoadLoopBudget::PostLoadCommitSaveHoldoffMs`, 100 ms) drives both the deadline and the wait;
`postLoadCommitSaveHoldoffRemainingMs()` reads the remainder without clearing it; and
`serviceMidiWhilePostLoadCommitHoldoffBlocks(maxWaitMs)` waits while **servicing MIDI**
(`midiHandler.handleMidiInput()` + `yield()`), bounded by the caller's remaining wall clock. The drain
charges real elapsed time against `maxWallClockUs` and waits only when the pure
`holdoffWaitFitsInDrainBudget(remaining, budget)` holds; otherwise A3.2's classifier reports the named
`post_load_holdoff`. **Evidence:** `test_persistence_sync_drain_budget` **18/18** (the bound);
`test_storage_drain_restore` **4/4** with the **real** drain — a fitting holdoff is waited out and an
injected DIN message is consumed during the wait, while the production 100 ms holdoff against the minimum
drain wall clock (~99 ms) is not entered (message unserviced, holdoff armed). Red-proven by disabling the
wait. Full native **2,337 / 1 skipped / 0 failures**; capture-serial **SUCCESS**
`code 391,080 + padding 2,136`. **Harness seams added (see `REVIEW.md` § A3.5):** a controllable native
clock (`nativeMillisNow`/`nativeMillisAdvance`, `micros()` derived) and an injectable MIDI inbox shared by
every `NativeMidiPort` instance (`MIDI_CREATE_INSTANCE` **defines** a copy, which produced a false
negative until found); `yield()` advances the mock clock so a bounded yielding wait terminates.
**A3.6 — preserve DEC-081/capture behavior is next; the A3.7/A3.8 device gate remains open.**

**2026-09-24 — A3.4a drain re-entry and callback safety LANDED (next: A3.5 holdoff MIDI servicing).**
The plan-correction entry below is now implemented. One scoped owner
(`StorageManagerInternal::PersistenceDrainScope` + `isPersistenceDrainInProgress()`) is acquired by
`drainPersistenceWorkBlockingDetailed` before any shared mutation; a nested call returns
`TemporarilyBlocked("drain_in_progress")` (bool wrapper false) without changing the outer owner, and the
destructor is the single release path for Completed/Blocked/Failed alike. Callback audit landed
pre-side-effect: `handleClearTrack` (before the stop), `handleUndo`/`handleRedo`,
`handleUndoClearTrack`/`handleRedoClearTrack`, `saveNewSetInternal`, `copySavedSetIntoCurrent`.
Transport/record/note-edit/load-save-mode stay unguarded by design. New suite
`test_drain_reentry_callback_safety` **6/6** drives a CLEAR through the **real** MIDI dispatch route with a
free-guard control and a held-guard refusal (zero mutations, zero silences); red-proven by forcing the
predicate false. `test_storage_manager_load` **29/29**; full native **2,332 / 1 skipped / 0 failures**;
capture-serial **SUCCESS** `code 391,896 + padding 1,320` (first build failed the 840 floor at 728, so
`handleClearTrack` moved to FLASH — 480 B better than the A3.4 line, no re-base).
**A3.5 — holdoff MIDI servicing is now unblocked**: the holdoff poll may service MIDI, because a CLEAR
arriving in the wait is refused by name instead of re-entering the drain. **A3.7/A3.8 device gate and
A3.6 remain open.**

**2026-09-23 — restore-liveness plan correction (documentation only).**
The final review's callback re-entry finding is addressed in
[`2026-09-24-deferred-slot-restore-liveness/tasks.md`](../../openspec/changes/archive/2026-09-24-deferred-slot-restore-liveness/tasks.md)
**A3.4a — drain re-entry and callback safety**, required before **A3.5 — holdoff MIDI servicing**.
The contract adds a scoped owner guard, pre-side-effect refusal of conflicting callbacks, live eligibility
and target revalidation, and production-dispatch fixtures. CLEAR gates consistently permit named refusals
and reject unexplained aborts. The final falsification tightenings are also incorporated: explicit
queued/running/waiting/terminal states and the actual `saveNewSetInternal()` caller, with corresponding
acceptance fixtures. Firmware and verification tasks remain unimplemented/unrun.

**Execution workflow:** use [ARCHITECTURE-REVIEW.md](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md) packets and tasks D0 for every upcoming slice (DEC-080); acceptance fixtures precede production behavior changes.
**Next OpenSpec slice (transferred, not a sibling change):** `runtime-working-set-ownership` **S2b.7** —
drain-driven restore advancement. The change that was proposed here, `deferred-slot-restore-liveness`, was
**archived 2026-09-24** with a reduced scope; its undelivered drain-advancement clause became S2b.7 and its
set-change residual became S2b.8 (landed 2026-09-24). Its five live mechanisms (C1/C2/C4/C5/C6; C3 withdrawn)
are settled. Applying a restore under transport remains bounded by **`resumable-midi-boundary-playback-maintenance`
task 3.3** (bounded Commit), which is a named prerequisite and must not be relaxed here.

**DEC-081 landed 2026-09-23 (owner direction): audible playback never gates persistence.**
**DEVICE-VERIFIED on the discriminating case** (recorded in
[DEC-081](../DECISION_LOG.md#dec-081--audible-playback-never-gates-persistence-and-never-stops-for-it) and
[PROJECT_STATE.md](PROJECT_STATE.md); this entry previously read "not demonstrated" and was stale). The "all
tracks silent" gate is superseded by *never require stopping, always keep playing*:
`isTransportActiveForPersistence()` became `isCaptureRecordingForPersistence()` (recording only), the
drain-gate reason became `BlockedByCapture`, and the bounded **300 us** active slice budget is deliberately
retained during playback (the idle fallback is 3,000 us). Build (`code 392,328 / padding 888`) + full native
2,289 / 1 skipped / 0 failures + harness suites green. **Device:** with **five tracks audibly playing** and a
deferred save genuinely owed, the clear's blocking drain completed — `Clear waiting for deferred save
completion` → `Draining persistence work queue to SD card...` → **`State saved successfully (v4).`** →
`Clear ignored — track is empty`, in **<833 ms**, with **0** `Clear aborted` / `Persistence drain stuck` /
`Persistence drain blocked` where the old gate gave 33 stuck iterations then an abort; the other tracks kept
playing and the same build reports `overflow_free=True`. Standing observation (not a regression):
`DIAG,persist_save` still reaches ~40 ms maxima, because a single persist *grain* is not bounded by the
slice budget, only the loop is.

**Scope added 2026-09-23 (owner direction): persistence immediacy and CLEAR latency — BOTH HALVES LANDED AND DEVICE-VERIFIED.**
Owner asked that *a recording persist as soon as possible* (so a hang or power loss right after preserves
part of the take) and that *CLEAR be immediate*. Findings and status:
[`docs/Plans/persistence_immediacy_clear_latency_refinement.md`](../Plans/persistence_immediacy_clear_latency_refinement.md).
The incremental mechanism already existed (`LoopEventStore::sealChunk` → `admitSealedChunk` →
`MidPassChunkPersist`, emitting `PERS,mid_pass`) but sat behind the transport gate. **Landed:** a
one-chunk-per-turn FLASH-resident flush in `DeferredSaveScheduler.cpp` plus its own queued-capture-chunk
invocation reason in `main.cpp` (`runDeferredSaveState` no longer needs a pending deferred save), so a take
is durable while it is recorded. **Device:** **13 `PERS,mid_pass` rows**, including inside live
`OVERDUBBING` (584,590,680 · 584,617,581 us) and `PLAYING` (598,393,317 · 598,402,101 us) windows, each
`ok:t4:s0:cNN:n256` for the slot under test, with `overflow_free=True`. The earlier "0 mid_pass" was a
`--record-only` fixture artifact — that mode never commits a pass, so its chunks are released and never
admitted. **CLEAR:** its slowness was that CLEAR silences only the **selected** track while
`isTransportActiveForPersistence()` required **all** tracks silent, so the drain could never finish when
another track sounded. The per-slot-supersession fix first proposed for it was **superseded by DEC-081**
(the owner ruled that the gate comes off rather than that every track must stop); CLEAR is now
device-verified completing in <833 ms with five tracks audibly playing. The two decisions compose: the
mid-pass flush is what keeps a live take durable, and DEC-081 is what lets CLEAR drain without silencing
the device. Both landed on plan docs + code only — see the scope note at the top of this file.

**Scope added 2026-09-23 (owner direction): SD temp-write truncation fix — LANDED, reconcile warning gone.**
The device session was blocked by `could not reconcile MidiLooper/sets/index.bin`; the cause is that Teensy's
`SD.open(path, FILE_WRITE)` **appends** (`O_RDWR|O_CREAT|O_AT_END`) and `FILE_WRITE_BEGIN` does not
truncate (`O_TRUNC` is never set), so a temp left by a failed write corrupts every later one — and the
native SD stub modelled create-or-truncate, which hid the whole class. Fixed via
`CurrentSetStorage::openForRewrite` at the 16 from-scratch sites plus a self-healing
`reconcileSetIndexOnSd`, with the stub corrected and a red-proven regression test. Plan and evidence:
[`docs/Plans/sd_temp_write_truncation_refinement.md`](../Plans/sd_temp_write_truncation_refinement.md).
**Device:** 0 reconcile warnings on every post-fix boot that reset the board, a recorded loop body persists
and restores across a reboot, and pre-existing content stays faithful. **What still blocks the content PASS
and S1.3's slope sweep is NOT this fix but the restore-liveness defect** (`Track 6 loaded state: EMPTY`,
`DIAG,lcr,skip,restore`, a census that changes between boots, and the CLEAR drain that aborts) — now
proposed as the OpenSpec change `deferred-slot-restore-liveness`, whose `design.md` records six mechanisms
(C1–C6) for it.

**Current handoff:** for `runtime-working-set-ownership`, follow [IMPLEMENTATION_CONTRACT.md](../../openspec/changes/runtime-working-set-ownership/IMPLEMENTATION_CONTRACT.md) (2026-09-23, DEC-079) for its slices and prerequisites; phase F is planned but uncreated. **Its remaining work is: S2b.7 (native, drain-driven restore advancement) and otherwise device-gated** — the restore-liveness change that was meant to unblock the device legs was **archived 2026-09-24** with a reduced scope, so the restored-library instability confound remains and those legs are re-run only on a stable library (see `tasks.md` S1.3/S2b.3 and the S2b gate).

**Progress (2026-09-23, DEVICE SESSION — RWSO-24CAP): the S2b/S1.5 hardware gate is taken, the restored library
survives a reboot, and a clear-drain blocker was worked around.** Flashed HEAD `26e96539`
(`teensy41-capture-serial`, hex `673e20ca…`) and ran a new temporary stimulus
`STIM-BASE-2-2-01-TEMP-RWSO-24CAP` — the canonical base recipe with `--record-bars 24 --overdub-bars 24` — on the
authorized scratch slot track 5 / slot 1.

**(1) Zero per-committed-slot reservation at boot, on hardware.** `free_chunks` pre-restore **480 of 512** (was
400), default enabled set **32** chunks (was 96), and the library restore walk `480 → 480` with
`credits=0,ready=0,full=0` on all eight `(track,slot)` rows — the S2b.3/S1.5 device claim.
**(2) A 24-bar overdub is admitted.** `record loop length 18432/18432`, `Overdubbing started` ×2,
`op=overdub_start` denials **0**, 0 `#MEM_DENIED`/`#MEM_EXT_REFUSED`/`FATAL`, SEVT `bars=24/24`, display
`overdub_ok`/`transport_ok` true — against the pre-change RC-MEM-5 leg's zero starts and 4 denials.
**(3) CORRECTION, and then a withdrawal of my own correction:** the dump after a serial-open reset reports
`CAPTURE_CONTENT,begin,track=5,slot=0,length=18432`, so a slot can come back byte-exact right after its own
save+reset. A growth attempt (`STIM-…-TEMP-RWSO-GROWTH`, track 6) then **falsified** the "library can grow"
generalisation: after a second forced reboot the catalog does not reconcile (`could not reconcile
MidiLooper/sets/index.bin`), `Track 6 loaded state: EMPTY` (and track 7), `DIAG,lcr,skip,restore`, and the
census's non-zero-slot set changed between boots (`{track=1,slot=2, track=4,slot=1}` → `{track=1,slot=2}`).
Both `dump_and_capture` runs still report `track=5,slot=0,length=18432`, and track 5 **+ slot 1** reads
`24 bars (18432 ticks)` — so this slot survives; an earlier "3 bars" reading of mine was a wrong-slot read and is
withdrawn. **The S1.3 slope sweep stays blocked on the SD catalog reconcile** (the restored *set* is unstable
across boots), not on scratch-slot approval. **[Attribution CORRECTED 2026-09-24: the named obstacle is
withdrawn. `sets/index.bin` is `SavedSetCatalog::SetIndex{ uint32_t nextSequence; }`, a saved-set folder
counter, so a reconcile failure cannot by itself omit a slot; the shrinking boots report
`reconcile_warnings=0`; `DIAG,lcr,skip,restore` is a content-resolution line; and `Track 6 loaded state:
EMPTY` appears on stable boots too. The instability is real and now owned, unattributed, by
`restored-library-stability` Packet A — see the 2026-09-24 session entry at the top of this file and that
change's `ARCHITECTURE-REVIEW.md` § Task 1.1.]** Also: a boot census needs a **forced** reboot, and repeated runs
without one accumulate residency (`live` 4,658 KB → **8,170 KB**, heap pinned at its 52 KB floor).
**(4) Content: not a formal PASS, but the two "missing" rows are proven present.** `storage_matches_effective:
true` and all 24 bars dense (1,746 rows) but `inventory_equal: false` with **2 unexplained missing rows in bar 0**
(`[128,5,76,0]`, `[176,5,74,101]`). Checking each against both anchor occurrences finds **one matching occurrence
per cycle for each** (`note76` Off at `anchor − 80 ms` in both cycles; `note74` On at `anchor + 1 ms` and
`+ 44 ms`), so playback carries both and the verifier's cycle-local inventory does not bind them. A third 160 s
capture went `INCONCLUSIVE_CAPTURE` (no recurring anchor) because the restored set drifts between boots — so the
formal bar stays unmet, confounded by the same catalog defect. `capacity_path_exercised: false` (densest bar 84
refs): parity, not a capacity re-stress.
**Blocker found + workaround:** the clear aborted twice on `Persistence drain stuck after 33 iterations` because
`processDeferredSaveState` returns immediately while transport is active and the drain has no transport-gate
classification (generic `Stuck`); the host saw no `PERS,*`/`SAVE,*` marker at all in that window. Workaround:
stop the global transport and idle until the owed save drains, then run. Pre-existing, outside this change.
Evidence: [ARCHITECTURE-REVIEW.md](../../openspec/changes/runtime-working-set-ownership/ARCHITECTURE-REVIEW.md)
§ Device session — RWSO-24CAP.

**Progress (2026-09-23, S1.5 clause 3 — native): the dense-24-bar refusal point is answered, and the seal
puzzle is diagnosed.** Test-only; no `src/` change. The three-round blocker was upstream: a dense 24-bar
construction did not seal, and two explanations were withdrawn. **The branch is identified** —
`Loop::sealCapture` runs the hot-stop min-length removal **before** its empty check, so note pairs closer than
`Config::DEFAULT_NOTE_MIN_LENGTH_TICKS` (8) are all removed, the live store empties, and the seal returns
`FailedValidation` from the **`capture.store.empty()` branch**. Asserted signature: events present before the
seal, one tail chunk held (free 511), released by the failure (free **512**, nothing retained); the control at
exactly 8 ticks seals `Ok` and keeps its chunk. The "staging-order" rule is withdrawn too — all three
append/prewarm orders seal with pairs >= 8 ticks apart.

With a valid construction (864 pairs = 1,728 events = **7 chunks** per dense 24-bar loop across the 8 × 8
matrix): boot costs 32 chunks, and at **N = 63** free chunks are **39 -> 31**, so the 24-bar reserve admission
is **admitted at every N up to 63 — the smallest refusing N lies outside the supported envelope** (108,864
events, 83 % of the ceiling, 31 chunks free, 7 above `CHUNK_RESERVE + ACTIVE_OVERDUB_TRANSFER_CHUNKS`).
**Red-proven:** with the pre-S2b.1 per-slot reservation restored the same construction refuses at **N = 30**
(free = 6), so this is a capacity result of S2b.1, not a property of the pool. New default-gate cases
`test_s1_5_the_supported_dense_library_still_admits_the_24_bar_reserve` and
`test_s1_5_a_pair_below_the_min_length_empties_the_store_and_fails_the_seal`. `test_continuous_capture`
**97/97**; full native **2,287 / 1 skipped / 0 failures**; firmware SUCCESS at `code 392,312 / padding 904`
(unchanged). **Still open:** S1.5's **device** refusal point (byte-limited PSRAM, not the 512-id ceiling) and
the boot `live` figure against the ~191-chunk peak reservation, which is not reproducible natively.

**Progress (2026-09-23, S1.3 clause 2 — native): the synthetic 60-descriptor characterization landed, and it
red-proves the law at the contract's envelope.** Test-only; no `src/` change. New
`measureWorkingSetResidenceAcrossTracks(N)` spans the 8x8 slot matrix (the single-track helper could not
express N > 8), and two default-gate cases in `test_continuous_capture` show canonical **reservation flat at
40 chunks / 205,280 B from N = 8 to N = 60** with one active role, payload and the runtime directory reported
separately (8 -> 60 chunks / 41,056 -> 307,920 B; 15 -> 60 directory slots / 95,160 -> 380,640 B, not asserted
flat), zero canonical credits and zero horizons on all 59 inactive descriptors, and the pool reserve intact
(100 of 512 used). Making the removed S2b.1 reservation unconditional again turns both cases red
(`reserved(8)=152` vs `reserved(60)=452`; pool exhausts at `free=0`), then was reverted. Full native
**2,285 / 1 skipped / 0 failures**; `teensy41-capture-serial` SUCCESS at `code 392,312 / padding 904`
(unchanged, `src/` untouched). **S1.3's device slope remains blocked** on the restored scratch slot not
surviving a reboot; the other device legs (S1.5 refusal-N, S1.4 pass->chunk) are unchanged.

**Session closeout (2026-09-23).** The P0 and P1 **native** scopes are complete and verified: full native
**2,281 cases / 1 skipped / 0 failures**, all four firmware envs passing their per-env RAM1 budgets, and RAM1
`padding` **improved 872 → 1,096**. Four real defects were found by fixtures rather than review (a start guard
that ignored the latch; a switch that moved the active slot under an unadmitted stop; a load that discarded a
live owner's queued request; and a stale-interval park that would have wedged a track). Two of my own findings
were corrected in place, including a retraction. **What remains is hardware:** S1.1/S1.3/S1.4/S1.5 and the S2b
device gate, with baseline `83be2e19` pinned and proven plus the reproduction recipe in
`ARCHITECTURE-REVIEW.md`; S3.0's pins for later-stage members; and phases B–F. Full detail:
`PROJECT_STATE.md` session-closeout entry.

**Progress (2026-09-23, P0):** S0 is verified and S1.2's red scaling characterization is implemented and
recorded — native legs only, **no firmware change and no device run**. Next slice: D0.2's P1/S2b harness
controls, then S2b.1. The S1 gate's device legs stay open.

**Related, designed not implemented: `capture-tier-a-budget-profiles`** — the instrumentation change that keeps a
15–20 minute session inside the 1 MiB capture ring (per-family byte budgets, edge/sample/stream classes, critical
reserve, per-family coverage, and a host rule that degraded evidence cannot pass unconditionally). It is the
instrument this architecture's measurements depend on.

**2026-09-18 — ACTIVE IMPLEMENTATION SCOPE: WORKING-SET OWNERSHIP / HEAP PRESSURE
(`runtime-working-set-ownership`).** The overdub live-capture one-frame paint flicker is **CLOSED
without a fix at the user's direction** ("close this bug, we have a bigger plan to solve the memory
heap issue") and folded into this scope. What the flicker pass established before closing is worth
keeping, because it is evidence *for* this owner rather than a separate defect:

- The dip is real and already detectable: the on-change `DISP` family (emitted at `Transfer` from
  `resolvedPaintNotes()`, the vector `Draw` used) records it as a **decrease**. Across
  `captures/tidyup-base-track6-slot1-r2/session_20260918_221953.log` there are exactly two decreases —
  the `23.001 s` clear (32 → 0) and **`49.972 s`: `OVERDUBBING` 75 → 74 → 75 at `50.019 s`**, one
  painted frame, one row fewer, immediately after a MIDI event. The row is lost in the compose.
- The same capture shows why the paint's own source state cannot advance: **52**
  `#MEM_DENIED,op=snapshot_reserve,reason=heap_low,need=40960B,heap_free=68KB,pool_refused=0/0B` rows,
  continuously reported (1 s window) from **27.64 s to 79.06 s**, i.e. across the whole overdub. DEC-075
  makes that denial non-fatal, so the published root legitimately stays where it was — under the arm's
  heap state.
- Handed over invariant: *a note present in the composed paint set before and after a note-on/note-off
  must be painted in every frame in between.* Re-measure after the lease/publication repair by scanning
  for `DISP` decreases in the overdub window (no new firmware telemetry needed); if the dip survives a
  healthy gate, re-open
  [`overdub_live_capture_one_frame_paint_flicker_bugfix.md`](../Plans/overdub_live_capture_one_frame_paint_flicker_bugfix.md)
  as a paint-path RC with `DisplayManager::resolveDisplayNotesByDrawSource` as the first owner.
- The tidy-up exoneration stands (P1–P6 behavior-preserving; base commit `4aacd18e` reproduced the blink
  too), so `runtime-code-tidyup` stays out of scope. **No firmware change was made for the flicker.**

**2026-09-18 — the architectural target under that scope is WORKING-SET OWNERSHIP, not more per-slot
trimming (`runtime-working-set-ownership`, requested as a `DEC-###`).** The record-arm exit reported on
`captures/session_20260918_171613.log` is diagnosed (`immutable-loop-snapshot-passes` 5.13: an
all-or-nothing ~845 KB arm prewarm chain against ~822 KB free, failing at `tag=12`'s 64 KiB request) but is
**reclassified as a symptom**. The real mismatch is the scaling law: runtime working sets and whole-loop
derived state are owned **per recorded slot, for the slot's lifetime**, so the 8 MB pool supports on the order of
**four** resident slot bundles while the target is **~60 recorded slots at up to 64 bars**.

Quantified: 64 slots exist (8 tracks x 8); one admitted slot's tagged runtime total is ~1.47 MB and its arm
chain ~845 KB; whole-loop derived state is ~325 KB per loop (83 snapshot slabs / ~2.6 MB for a loaded
workspace); the resident event store holds ~28 sixty-four-bar loops at the observed density
(131,072-event ceiling / ~4,600 events per 64-bar loop). So both the runtime pools and the canonical store are
slot-scaled where they must be active-scaled or streamed.

The change: derived runtime views and capture companion storage become **owned by active runtime roles**, with
inactive slots retaining only canonical/persistence state and bounded metadata; working sets are acquired on
activation, reused across slots, and released when no live playback/capture/deferred owner remains. Acceptance is
a **measured scaling curve** -- load 60+ long slots, activate A, activate B, deactivate A, activate C -- required
to be flat in recorded slots and stepped in active slots, with C reusing A's capacity. **Envelope (corrected
2026-09-19, DEC-078): today it is 8 tracks x 1 sounding slot = N derived, not 2-per-track x 16 = 32** — the
"slots as virtual tracks" capability does not exist and the memory law must not *preclude* 16/32/48/64, but no
task here implements it and no acceptance claims it (`NUM_TRACKS = 8`,
`openspec/changes/runtime-working-set-ownership/tasks.md` S0.5). Finalization is **per interval**, not one
device-wide role; live capture is 1 per track; switching is an accepted interaction (DEC-078). **The looper must
never stop playing** -- switching, record/overdub and undo/redo all happen while playing with no MIDI timing
degradation. Window-derived lease sizing (RC-MEM-2's outstanding item) is **promoted from safeguard to
prerequisite S2**, and the whole-root decoupling is extracted to the sibling change
`windowed-playback-generation` (phase F), so **N is provisional** until that lands. **Boot/load is an admission
boundary** (S2b): today boot prewarms every committed slot and reserves 8 pool chunks each (41,056 B), which at
~60 slots consumes 480/512 chunks before any activation. Three quantities are now separated:
library residency (~60 slots), logical active slots (the product envelope), and resident working-set leases
(must fit PSRAM). Role tiers replace one "active": COLD / SELECTED / PREPARED-ACTIVE / CAPTURE-ACTIVE / DEFERRED,
with **selection staying cheap** so browsing 60 slots causes no lease churn. Leases are
**generation-qualified** (`slot + generation + role`) so late cleanup from a released slot cannot touch
recycled storage. The **cold undo policy** is pinned as a product decision before any flat-cold-memory claim.
S1's scaling assertion is a **red characterisation** that must fail today, and its device leg records the slope
to the current failure point (the resident store holds only ~28 sixty-four-bar loops) rather than an
unreachable 60-slot baseline. Canonical residency is a **separate gated phase** with its own DEC amendment, and
the cold contract is descriptor-based rather than chunk-reference-based. Staged S0..S7 in
`openspec/changes/runtime-working-set-ownership/tasks.md`; S0 is decisions only and S1 is measurement only,
neither with behaviour change.
RC-MEM-2's outstanding item and §5.13's three fixes are demoted to safeguards (G.1-G.3) under it. RC-MEM-2 and
RC-MEM-5 are **not** reverted -- they bought headroom and removed dead reservation.

**2026-09-18 — F3 REOPENED as a defect: a busy owner must not reject an interaction (DEC-076 item 9).**
The product rule is now invariant 31 and task stage S4f: **every user interaction is accepted, and *when* it
executes is the pipeline's decision** -- *"I never want a denied stop or a user to need to press twice."* `TrackOverdubLifecycle.cpp` currently answers a stop it cannot finalize at once with
`successor_stop_denied_capacity` + `reentryDecision("deny", ...)`, keeps the interval recording, and its own comment
states the consequence -- *"A later stop request is admitted once the predecessor retires"*. That re-press is the
defect, not the contract. There is no latch today: `hasPendingDeferredOverdubStop()` reflects an *admitted* job, not
a remembered request. Required: accept the interaction unconditionally and let the playhead-scheduled MVCC
pipeline decide when it executes against the playhead deadline and the earliest valid interval boundary, so
`captureStopOwner` / the overdub transfer / the canonical output / the session-exit handoff are **scheduling states**
that delay it (counted as waits, not denials); apply the same rule to record/overdub start, slot and track switching
and undo/redo. The accepted request is one small piece of state per live interval -- there is one live capture at a
time -- so it is **not** a stored stopped interval and needs none of the ring capacity the descriptor-ring finding
correctly denied. Acceptance is S7.4f: one press completes the stop. The open read is closed: the re-drive path
already exists (`stopOverdubbingToStopped`, `invalidatePlaybackMergedMidiEvents` both re-issue
`requestDeferredOverdubStopToStopped()`) but it only covers an already-admitted job, because a rejection stores
nothing -- so S4f extends that latch to the accepted-but-not-yet-executed request instead of adding a mechanism.
Per-action budgets are recorded as declared expectations to measure (S4f.6: stop overdub ~microseconds, cold
undo/redo ~1-2 s, load loop ~seconds), and **progress presentation is deferred** (S4f.7) behind a two-part gate:
measured latency **and** a mitigation review showing the budget cannot be met by scheduling, prewarm or windowing.
When taken it displays an *accepted* action, never a denial and never a cue to press again.

**2026-09-18 — 5.12 FIXED: the second overdub no longer erases the first from the display (`immutable-loop-snapshot-
passes` 5.12).** The sealed capture handoff was the only carrier of a closed overdub's rows while the committed cache
could not account for them, and `openOverdubSession` cleared it, so starting the second overdub took the first
overdub off the OLED until a resolve ran -- the user's "it removes the entire previous overdub from the display; it
returns at the end". It could not simply be left in place: the same handoff feeds lap merge at seal, occupy/hold,
draw-source gather reuse and the od2 clean adopt, and four of those five consumers must keep seeing the current
session alone (the `001647`, `120321`, `214900` and od2 regressions are exactly that lesson). So the fix is a second,
**display-only** representation.

The instrumented run settled the one distinction that had blocked four earlier attempts: gating the display union on
"not covered" is wrong, because exact-span coverage can never hold for a row the clean-adopt rule *replaced*
(`010217`). The probe (a deliberately failing assertion encoding the state, since PlatformIO captures native test
stdout so `printf` is not a channel) returned 262 = 0x106 for od2: retired tier populated with exactly 1 row, cache
clean, and `retiredSealedDisplayCovered()` false -- the retired wrap-shaped successor (`pitch 39, 2976 -> 2784`)
against the committed linear variant (`2976 -> 3055`) of the same identity.

Final design, two predicates answering two different questions:
- `retiredSealedDisplayNeededForFrame()` -- the display gate: true while the cache is dirty, or while any retired
  row's *identity* (`note` + `startTick`) is represented nowhere in the committed cache. Identity rather than span, so
  a superseded or already-present row stops being painted.
- `retiredSealedDisplayCovered()` -- the release rule, unchanged (exact membership), called from the maintenance
  point via `releaseRetiredSealedDisplayIfCovered()`.

The union sits in `applyDrawSourceOverlays` -- the production counterpart of the host helper, called last in all three
branches of `resolveDisplayNotesByDrawSource`, after the `shouldRetainUnpublishedVisualCacheOverStaleGather` assign
that would otherwise clobber it -- and is mirrored in the test helper. Retire happens at session **close**
(`closeOverdubSession` and the stepped close's `Phase::Finish`), while `openOverdubSession` keeps clearing the live
handoff exactly as before, so the `001647` guard holds by construction; pass-disable clears both tiers (the 5.8a
guarantee) and `resetPassTimeline` clears both.

Evidence: `test_loop_take_survival` **140/140**, including the new
`test_second_overdub_keeps_the_first_overdub_on_the_display` and all four historical guards; full native **2,262 cases
/ 1 skipped / 0 failures**; `teensy41-capture-serial` SUCCESS with RAM1 unchanged at `variables 96,128 /
code 392,344 / padding 872 / locals free 34,944`. Device gate owed on the 24-bar two-overdub shape.

**2026-09-18 — RC-MEM-7 landed: the capture ring degrades instead of invalidating, so a long run no longer aborts on
ring capacity (`immutable-loop-snapshot-passes` 5.11).** The ring's own policy is that Tier-A text may only be
displaced by newer Tier-A text and that this "keeps the newest windows and cannot wedge the ring", but both that
designed degradation and genuine evidence loss set one `overflowPending` flag, and the host treats `RING,overflow`
as session-invalidating. So the intended behaviour was indistinguishable from real loss, and
`captures/rcemem5-lazy-pool` aborted at ~150 s of a 24-bar two-overdub run purely on ring capacity after the pool fix
had made the run itself healthy.

`CaptureLineTier::evictionKindFor` / `incomingMayDisplace` now classify displacement in the header-only tier helper
(host-testable without Arduino) and the ring keeps three separate accounts: `TimingShed` (routine, unchanged),
`TierAText` (degradation, emitted as `#CAP,<us>,RING,tier_a_evicted,<count>,first=<us>`), and `CriticalText` (the only
fatal kind, still `RING,overflow`). Tier-A degradation drains at the 256-record budget too, since every unsent record
is one the ring may displace again. Because the firmware emits `RING,overflow` only for critical loss, the existing
fatal harness clause stops firing for Tier-A thinning **without being weakened**; the harness parses the new notice
into `tier_a_evicted_count`/`tier_a_evicted_first_us`/`tier_a_degraded` and raises an advisory
(`capture_tier_a_evicted:<n>`) so a timing verdict is down-weighted rather than read as clean.

Device (`captures/rcemem7-tier-degrade`, same 24-bar record + two-overdub shape): the run no longer aborts on ring
capacity -- `capture_ring_overflow` is absent and the verdict is a substantive `FAIL (FIRMWARE_CONTRADICTION)`
against the harness oracle instead of `INCONCLUSIVE_CAPTURE`, with both overdubs stored correctly (`count=192/191`
and `152/7`, `count_ok/span_ok/bars_ok` all true) and `DISP overdub_ok=True transport_ok=True`. The advisory measures
the thinning at **`capture_tier_a_evicted:92655`** records.

**That number sizes the next step.** ~92.7k Tier-A records beyond 1 MiB over ~162 s of transport puts Tier-A at
roughly **30-35 KB/s**, so the 1 MiB ring covers only ~30 s of active transport and no affordable ring holds a
multi-hour run. Evidence *completeness* therefore needs either a Tier-A volume profile (a long-run divisor on the
per-tick span families, the `DIAG,droid_queue` gate's shape) or the SD sink; *survivability* is what landed here.
Which Tier-A families may be sampled is a policy call about evidence, so it is deliberately not made yet -- the
mechanism, the measurement and the advisory that make it decidable are in place.

Native `test_capture_line_tier` 13/13 (truth table: a Tier-A head is `TierAText` and non-fatal; a critical head is
`CriticalText` even though it is also Tier-A; `incomingMayDisplace` refuses every case where ordinary diagnostics
would evict Tier-A or critical evidence). Full native **2,261 cases / 1 skipped / 0 failures**; harness
`test_capture_transitions.py` 48/48 and `test_baseline_canonical_args.py` 3/3; `teensy41-capture-serial` SUCCESS with
RAM1 `variables 96,128 / code 392,344 / padding 872 / locals free 34,944` (three counters, +32 B).

**2026-09-18 — RC-MEM-5 landed: the chunk pool grows in blocks, and the "cannot activate overdub on a 24-bar loop"
symptom is FIXED on device (`immutable-loop-snapshot-passes` 5.11).** `initPool` no longer reserves all 512 chunks
(2,566 KB) at boot. The chunk array is now a lazily opened block table (`POOL_CHUNKS_PER_BLOCK = 64`,
`POOL_BLOCK_COUNT = 8`), opened by `allocChunk` on the first id that lands in each block; the **512-chunk admission
ceiling is unchanged** and the flat id space is untouched, because chunk ids cross `PersistenceQueue::onChunkFreed`,
the committed chunk-id lists and the storage I/O format. Boot now backs `ceil(used chunks / 64)` blocks: at the
observed 104-143 used chunks that is 2-3 blocks (656-984 KB) instead of 2,566 KB.

Device (`captures/rcemem5-lazy-pool`, 24-bar record + 24-bar overdub, track 5 slot 1, same shape as the A/B legs):
- **`Overdubbing started` twice** (76.582 s, 138.665 s). The fixed-pool legs managed **zero** starts against 4
  `op=overdub_start` denials -- this is the user-reported regression, now gone.
- `op=overdub_start` denials **0**; `snapshot_reserve` denials 129 -> **6**; **0 `FATAL`**, **0 `#MEM_EXT_REFUSED`**
  (the fixed-pool `before` leg aborted with `[InternalHeapFirstAllocator] FATAL` at 104 s).
- Boot pool `live` **3,837 -> 1,913 KB** (-1,924 KB).
- The mechanism RC-MEM-6 predicted, confirmed: with the pool no longer falling back to `std::malloc`, `heap_free`
  held **80 KB through both overdubs** (44 KB only at 162 s), so `OverdubStart`'s 48 KB requirement was admitted.
  Peak chunk use 143/512, `chunks_free` never approached its old floor, `pin1` 0-1 and slab `unused` 30-60 KB
  throughout (the arena remains ~98 % packed, so 5.10's re-aim stands).

Native `test_pool_backs_storage_only_as_content_reaches_it` pins the block form: 0 blocks at boot, 1 block after a
single event, 2 after crossing the boundary, ids still contiguous across it, backed bytes far below the full array,
and `freeChunkCount() + usedChunkCount() == POOL_CHUNK_COUNT` unchanged. Full native **2,259 cases / 1 skipped /
0 failures**; `teensy41-capture-serial` SUCCESS with RAM1 `variables 96,096 / code 392,344 / padding 872 / locals
free 34,976` (the block table is 8 pointers, +32 B, still 12 ITCM banks).

**Owed in the same RC:** the `SESSION_CAPTURE` ring. This run aborted on `firmware capture ring overflow` -- the
1 MiB `kCaptureRingBytes` can no longer hold a 24-bar two-overdub run now that both overdubs actually execute. The
content itself was correct (`stored overdub span count=192/191`, `count_ok/span_ok/bars_ok` all true), so the ring is
now the last fixed store and the last diagnostic limit rather than a functional one. Ring policy (size per session,
or shed-shape rather than overflow-abort) is the remaining half of RC-MEM-5.

**2026-09-18 — the fixed boot stores are now measured, and the overdub-start denial behind "cannot activate overdub
on a 24-bar loop" is diagnosed (`immutable-loop-snapshot-passes` 5.11, RC-MEM-5/-MEM-6).** Two measurements re-rank
the remaining work, both taken before changing anything.

*The chunk pool is ~33 % used at its observed peak.* Across every capture on this device the `[Memory] ext`
`chunks_free` minimum is **345** (`captures/bar-anchor-08`/`-09`), i.e. the worst observed occupancy of the
512-chunk pool is **167 chunks = 837 KB** of a **2,566 KB** reservation; the next worst are 164, 158, 152, 151 and
150. So at least **1,729 KB of the pool has never been reached in any captured run**. Recorded as RC-MEM-5 with the
recommended form: **grow on demand, do not lower the ceiling** -- keep `POOL_CHUNK_COUNT` 512 as the admission
ceiling and allocate the `EventChunk` array in blocks (64 chunks = 328 KB) on the first id that lands in each block,
so a 167-chunk peak backs ~3 blocks instead of 8. The flat id space must stay intact (chunk ids cross
`PersistenceQueue::onChunkFreed`, the committed chunk-id lists and the storage I/O format), so this is a block table
plus one `chunkAt(id)` accessor behind the 31 `pool_[...]` sites in `src/LoopEventStore.cpp`, with the
`poolUsed_`/`poolLifecycle_`/`poolChunkRefCount_` arrays staying flat. A lower fixed ceiling is explicitly *not* the
fix: 167 is an observation, not a bound.

*The admission gate is self-blinding once the heap floor binds.* Found in the RC-MEM-2 A/B and recorded as
RC-MEM-6: at the steady state the internal heap sat at **44 KB free for ~100 s**, so `OverdubStart` (required 16 KB
+ 32 KB floor = 48 KB) and `SnapshotReserve` (40 KB + 32 KB = 72 KB) were denied while `pool_refused=0/0B`
throughout (129 + 4 denials in one 150 s run, `since_refusal=UINT32_MAX`). **Because the gate denies before the
request reaches the pool, the pool-refusal signal the same gate consults can never fire**, so "pool is fine" and the
heap-floor denial coexist indefinitely. The device is at a genuine ceiling (pool `live` ~8.27 MB), so the denial is
not wrong; the recovery path is. Fix direction is footprint, not gate policy: RC-MEM-5 and RC-MEM-1R are the unblock
(pool headroom stops the `ExternalMemoryFirstAllocator` -> `std::malloc` fallback that put RAM2 at 44 KB), plus giving
the gate a *measured* floor derived from the heap high-water admitted operations actually consume -- the same
evidence-based shape as `companionRowHorizonBound` -- rather than a constant that can wedge a session. Lowering the
constant without that measurement is not acceptable: it exists to keep the `abort()`-on-exhaustion path unreachable
(DEC-075's defect class).

Both are recorded, not implemented; the next change is a choice between them.

**2026-09-18 — RC-MEM-2 landed: a long-loop slot no longer reserves the whole-take playback bundle it can never
publish (`immutable-loop-snapshot-passes` 5.11).** The invariant is now enforced by one owner answer rather than
implied by two independent decisions: *a playback-view owner may reserve capacity only for a representation that can
actually become authoritative for the current loop/horizon.* `Loop::fullTakePlaybackViewAdmissible()` returns false
past `DisplayWindowUtils::kMaxDetailedWindowBars` -- exactly where the windowed policy takes over -- and
`Track::prewarmPlaybackForSlot` passes that answer into
`LoopPlaybackRuntime::prewarmViews(capacity, fullTakeAdmissible)` ->
`RuntimePlaybackViewSet::prewarm(reserveWholeTake)`. The three rolling views and both capture stagings are always
reserved (the windowed lanes and capture admission need them); the two bridges, their two immutable bases and the
prepared pair are reserved only when a whole-take view can actually be published. `wholeTakeReserved` makes the
refusal explicit instead of incidental: `beginCandidateBridge`/`selectCandidateBridge` deny before touching bridge
storage, `publishStoppedOracleView(const RuntimeCompanionPasses&, ...)` and
`publishStoppedCanonicalOracleView` check it, and the pair is skipped because its only publisher requires
`isFullLoopMergedPlaybackWindow`. The downgrade is not irreversible: a slot admitted windowed-only upgrades in place
at the next cold record-arm/overdub-start boundary if the loop has since become short, so the bridge prewarm stays
reachable and admitted staging is never reset.

Dead reservation removed: 2 bridges x 2048 x 90 B + 2 bases x 2048 x 35 B = **512,000 B**, plus the prepared pair's
2048-row buffer (~59 KB) = **~558 KB per armed long-loop slot**. `fixedExternalBytes()` now reports only the
reserved part, so that figure is the slot's real reservation. Deliberately *not* claimed as reclaimable: the three
views (215,040 B), the capture staging (118,784 B) and the companion/canonical owners (~450 KB) -- those are not view
bundles and the windowed lanes and deferred stop path still require them.

Evidence: native `test_playback_prewarm` 177/177, including four new fixtures -- the predicate flipping exactly at
the threshold; a windowed-only slot having `capacity() == 0` for both bridges and both bases, no prepared pair, and
`fixedExternalBytes()` equal to the views-plus-staging arithmetic exactly; a differential proving the same
`beginCandidateBridge()` is admitted on a whole-take slot and refused on a windowed-only one while the `MidiEvent*`
stopped-oracle overload (the **windowed** publisher) still publishes a partial coverage; and an in-place upgrade
re-arming bridges and pair without resetting admitted staging. Full native **2,256 cases / 1 skipped / 0 failures**;
`teensy41-capture-serial` SUCCESS with RAM1 byte-identical to the census state (`variables 96,064 / code 392,152 /
padding 1,064 / locals free 35,008`), i.e. the change is cold-path code and cost no FlexRAM bank.

*Release path added in the same change.* A record arm on an **empty** slot has `loopLengthTicks == 0`, so
`fullTakePlaybackViewAdmissible()` is legitimately true and the bundle is reserved; `prewarmViews(..., false)` at a
later cold admission now **releases** it instead of keeping it for the session --
`RuntimePlaybackViewSet::releaseWholeTakeViews()` returns the bridge/base backing (refusing while a bridge is
selected, since that bridge is the authoritative representation) and the prepared pair is cleared. So the 715 KB is
not held for the whole session after a record that turned out long.

*Device A/B (clean worktree `c0aadc0f` census-only vs `5ba7d109` census+RC-MEM-2; 24-bar record + 24-bar overdub,
track 5 slot 1, base preset).* `before`: arm delta +1,372 KB with `tags=91/715/148/0/7/0/58KB`, then exhaustion --
`[InternalHeapFirstAllocator] FATAL` at 104 s, `live=8374KB, heap_free=16KB`, `#MEM_EXT_REFUSED bytes=23040`.
`after`: the same 715 KB reservation, 0 `FATAL`, 0 refusals, heap floor 44 KB -- **not** attributed to this RC, since
the reservation was identical in both legs and the difference is workspace-state variance. The release itself could
not be measured in this shape and the reason is recorded: the follow-up cold admission that would trigger it (the
overdub start on the now-24-bar loop) was denied on the **internal-heap floor first**
(`#MEM_DENIED,op=overdub_start,reason=heap_low,need=16384B,heap_free=44KB`, 4 times against 129
`op=snapshot_reserve` denials), so `prewarmViews(..., false)` was never reached. A shape whose long-loop cold
admission succeeds is owed to measure `tag2 ViewPrewarm` 715 -> ~215 KB; fixing that heap floor is a separate RC, and
it now binds *before* pool pressure does -- the opposite of the original crash.

The same run settled 5.10's question on hardware: `slab_used` 2,453-2,857 KB of a 2,496-2,912 KB payload
(`slab_unused` 42-54 KB, `pin1` 0-1) for the entire session, i.e. the arena is ~98 % packed with essentially no
pinning. Still out of scope: deriving the per-window capacity (2048) from the resolved window, and prewarming only
the armed slot instead of every enabled slot.

**2026-09-18 — the snapshot slab arena is instrumented, and its "reclaim ~3 MB" premise is measured to be FALSE
(`immutable-loop-snapshot-passes` 5.10/5.11).** The user's question was why live memory is far larger than the loop
data and grows on its own when the architecture's intent is to resolve short windows and queues instead of whole
loops. Answer: **the bytes are live content, not reclaimable allocator slack** — so the generation-local arena that
was going to be Step 1 would have cost a rewrite and returned almost nothing. Only the *instrument* landed.

*The measurement that falsified the plan.* The six `#CAP,...,DIAG,snapshot_alloc,nodes=,bytes=,slabs=` rows in
`captures/ab24-nogate/session_20260918_142527.log` (one per completed freeze) give `nodes=16,798, bytes=2,205,184,
slabs=78` at 12.60 s, where `bytes` is `sum(sizeof(Type))`. Compiling the node types with the device toolchain gives
`SnapshotMembership<Ref>` = **140 B** (16 reference slots), `SnapshotBarPage<Ref>` = 256 B (32 slots),
`SnapshotSpanRoot` = 1,024 B (128 pitch directories), `SnapshotPitchDirectory`/`SnapshotEventRoot` = 12 B,
`LoopSnapshotNote` = 20 B, `LoopSnapshotEvent` = 32 B, `LoopSnapshot` = 36 B -- so the 131.3 B average `sizeof` is
the 140 B membership page. Adding one `allocate_shared` control block per node (~20 B) gives ~2.54 MB against
78 x 32,736 B = 2.553 MB of payload capacity: **99.5 % packed**. `alloc=13,371 KB` against `freed=5,087 KB` confirms
the existing `liveCount == 0` slab release already returns whole slabs during churn; the residual live set is
referenced snapshot content. The `<=32K` class is therefore *not* "mostly-empty slabs stranded by interleaved
generations", which is what the audit assumed when it called this the largest single win.

*What the ~3 MB actually is.* ~2.6 MB is the loaded workspace's published snapshots (83 slabs at 16.3 s, before any
record) and ~448 KB is the 24-bar record (83 -> 97 slabs, **18.7 KB per completed bar**). The record creep is the
live-record lane republishing a full-content snapshot once per completed bar (`liveRecordCompletedTicks`,
`src/Track/TrackDeferredMaintenance.cpp:994-1003`), where each generation's own new pages stay live because the
newest root shares its predecessors' subtrees by design. At ~152 B per node and ~3 nodes per event that is ~450 B of
immutable snapshot per MIDI event, on top of the chunk pool holding the same events.

*Instrument implemented.* `SnapshotSlabCensus` + `snapshotSlabCensus()` (`include/LoopSnapshot.h`,
`src/Loop/LoopSnapshot.cpp`): a doubly-linked live-slab registry in the 32 KB slab header (no RAM1 cost, O(1) unlink
so a retiring slab stays cheap on the MIDI edge), reporting live slabs, used/unused bytes and the pinning counts
(`pin1`/`pin24`). It is emitted in the once-per-second `[Memory] ext` tick line and in the `DIAG,snapshot_alloc` row,
next to the historical cumulative `snap=` so the two can no longer be confused -- the earlier reading of `snap` as a
live figure is what overstated the arena. `pin1` is the regression signal for the invariant that a generation must
not be pinned by one surviving node.

*Re-aimed target.* The lever is node count per reference, not the allocator: the 140 B membership page with 16 slots
is the dominant node and path-copy churn builds a fresh chain per changed bar/list (raise refs-per-page or shrink the
page); the per-completed-bar whole-loop republication is what makes a record grow (carry only the resolved window);
and the chunk pool plus the snapshot hold the same content twice. A narrow win inside the original scope -- windowing
the live-record publication -- is bounded at ~150 KB for a 24-bar loop, not 3 MB, and must not be sold as the
footprint fix.

*Also recorded as RCs (5.11).* RC-MEM-2 per-slot bundles at the 2048 ceiling (~1.16 MB/slot) with the measured
evidence that a 24-bar loop never publishes a fixed view at all, because the publisher requires
`coverage.lengthTicks == loopLengthTicks` and playback therefore takes the 2-bar merged window; RC-MEM-3
`overUndoMemoryPressure` cannot see pool pressure and its `PREFERRED_UNDO_DEPTH` arm is unreachable in effect;
RC-MEM-4 the compatibility replay moved to `LoopContentResolution::resolveDisplayNotes/resolveWindow/resolveState(
const LoopPasses&, ...)` and to `LoopPasses::materializeToEventVector`, and the §6.1 gate that was supposed to prove
it is marked `[x]` but **fails 2 of 14 tests at HEAD** and never scans those overloads; RC-MEM-5 the fixed boot
stores; RC-CHUNK-1 `stepRewriteFromOrderedSource` releases a chunk reference without `tryFreeChunk`, so the chunk is
never returned to the pool; RC-CHUNK-2 `tryAssignCommittedChunkIds` copies ids without `retainChunkReference`.
Verified caller-less and therefore describing paths that no longer run: `advanceFallbackGatherSession` (the budgeted
fallback gather, whose `eventBudget` is unread), `prewarmSelectedDisplayVisualCache`, `resolveDisplayNotesCommitted`,
`resolveDisplayNotesLiveCapture`, `commitLoopPasses`, `liveEventCount`, `materializedLoopEventsForNoteEditFocus` --
so `DerivedViews.md:93/141-146/193/195` and `Display.md:36` owe corrections. Device SD state is user data; no
quarantine, delete or rename was performed.

**2026-09-18 — the post-wrap device reset is fixed at the publication boundary (DEC-075,
`immutable-loop-snapshot-passes` 5.9, RC-PUB-1..3).** The RC below is implemented; the footprint is untouched.
`Loop::stepPendingSnapshotPublish` now admits every turn that could start, step or freeze a candidate through
`OperationKind::SnapshotReserve` (dead code until now) with a margin of one slab plus 8 KB, and a denied turn writes
nothing -- the publication stays pending and the previous root stays selected. An `Allocation` failure now parks the
fragment transiently instead of rebuilding it every turn, and the park is released only after admission has denied
while parked and then admits again; the freeze-failure branch counts through the same path. The gate's only per-turn
caller made the repeat `#MEM_DENIED` report a flood risk, so that report is now bounded to one per second per shape
while the decision stays exact on every call. **RC-PUB-3 was corrected rather than implemented as worded:** the slab
allocator's `abort()` is both unreachable (`allocateSnapshotShared` pre-reserves `128 + align(sizeof(Type))` first, so
pressure surfaces as the already-non-fatal `snapshotSlabOpen` failure) and not convertible to a null return (device
`-fno-exceptions`; a custom deleter would put a control block per node on the internal heap), so the abort stays as
the documented invariant check. The allocator that actually aborted is the shared `ExternalMemoryFirstAllocator`,
whose abort-instead-of-deny semantics remain the wider defect above. Evidence: two new fixtures in
`test_loop_take_survival` -- `test_publication_turn_waits_for_admitted_headroom` (a denied turn leaves the wrap
journal, published root, its revision, the in-flight flag and the failure count unchanged; the same publication then
completes) and `test_allocation_failure_parks_publication_until_admission_recovers` (park, no attempt while admitted,
release after an observed denial, then complete and current). Full native **2249 / 1 skipped / 0 failures**;
`teensy41-capture-serial` SUCCESS with RAM1 byte-identical to DEC-074 (`variables 91,968 / code 425,560 / padding
424 / locals free 6,336`). **Device evidence 2026-09-18 (parked path):** `captures/rcpub-gate-05` (git `dc9a1107`
clean, hex `7f6bd8e3…`, base preset, track 5 slot 1, TEMP stimulus `STIM-BASE-2-2-01-TEMP-A`, `--record-bars 8
--overdub-bars 8 --overdub-start-delay-bars 2`) reports **0 `FATAL`, 0 `#MEM_EXT_REFUSED`**, a device streaming to the
end, and **49 `#MEM_DENIED`, all `op=snapshot_reserve`, `reason=heap_low`, `need=40960B`, `heap_free=68KB`,
`pool_refused=0/0B`** — the heap, not the pool, was binding, which is the crash's condition. The parked state is
direct: `snapshot_unit`, `snapshot_fragment`, `snapshot_slab_fail` and `snapshot_alloc` are **all 0**, so no
construction was started, stepped, frozen or published while denied and no incomplete prefix could become
authoritative; `heap_free` held at 68 KB (min = median = max over 73 samples), `snap` stayed 2214 KB, and pool `live`
went 6507 -> 7881 KB at record-arm staging then flat. The denial report is rate-bounded on device as designed (~1/s
over the 50 s window). The crash log is the counterfactual: the same boundary without the gate drained heap 108 -> 0 KB
and `snap` 2409 -> 2543 KB in six seconds, then `abort()`.
**Owed before this closes:** (a) the **wrap** case specifically — this run's denials landed during `RECORDING` (the
live-record window shares the construction path) and the harness aborted before the wrap on a 1 MiB `#CAP` ring shed
(`Result: INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)`, `second overdub transition not confirmed`), the
`droid_queue`/volume issue already recorded above; (b) that publication **resumes and completes** once the heap
recovers — the run ended while still denied, so resume is native-proven only; (c) RC-PUB-2's park/release has no device
occurrence, which is the intended outcome since the gate prevents the allocation attempt that would park. The 24-bar
repro is also out of reach: `TICKS_PER_BAR = 768` makes the crashing loop 24 bars while `--record-bars` accepts
`[1, 2, 4, 8]`, so a faithful re-run needs that cap raised. `kSnapshotPublicationReserveBytes` (72 KB of required
internal heap) remains the tunable if a healthy device parks; the DEC-073 RC1 control settles at 104 KB and this run
was denied at 68 KB.

**2026-09-18 — the blocking `saveState` drain's wedge is FIXED and device-verified, after a RAM1 bank reduction unblocked
it (`immutable-loop-snapshot-passes` 5.7.6; commits `12fdf38d` -> reverted `d3290020` -> re-landed `0a036c63`, on
linker change `ea9918ac`).**

*Root cause.* The two base-preset runs that aborted at the harness's clear precondition (`captures/rcpub-gate-01`,
`-02`) with `Persistence drain stuck after 33 iterations (max 33, expected ~1 slice steps, ~0 SD bytes)` were a
**monotonic work-entry table**. `src/StorageManager/SyncDrainBudget.cpp:66` sets `finalizeSliceSteps` only when
`pending && queueDepth == 0 && writingWorkItemCount == 0 && !itemActive`, so `expectedSliceSteps = 1` with 0 SD bytes
pinned the state to `pending` alone with nothing to step; `maxSliceSteps = 1*3/2 + 32 = 33` and `maxStuckIterations =
min(64, 33) = 33` match the log exactly. `stepPersistenceWorkItem` returns success on that state while leaving
`pending` set, and `maybeAdmitFinalizeWorkspaceAfterDrain` discarded `admitWork`'s result, so the drain could
neither advance nor terminate. Why admission was refused: no path ever returns an entry to `NotScheduled`
(`admitWork`/`markItemPersisted`/`requeueWritingEntryByIndex` only write `Queued`/`Writing`/`Persisted`) and
`LoopPersist` is keyed by `LoopId`, which is fresh for every new loop, so each clear/record cycle permanently
consumes one of the 256 slots -- once exhausted, admission fails for every key including `FinalizeWorkspace`.
Eliminated: PSRAM exhaustion (`refused=0/0B` with 1.6 MB free) and the transport-flush deferral
(`deferDispatchUntilMs` is only ever written 0; `deferWorkspaceSaveDispatchDuringPlayback` has no callers). Zero
`PERS` rows in the wedged runs against 13 `PERS,work` in a working run of the same capture configuration placed the
failure in admission, not in the finalize slice.

*Fix.* `admitWork` reclaims a `Persisted` tombstone when the table is full (lossless -- nothing reads `workState`,
`visitScheduledWorkItems` skips tombstones, and a fresh admission reproduces the re-queue shortcut), never touching
`Queued`/`Writing`; `maybeAdmitFinalizeWorkspaceAfterDrain` reports whether work is available; the drain fails fast
with `SyncDrainFailureReason::AdmissionRefused` and a PROGMEM diagnostic naming `pending`/`queue`/`writing`/`active`.
Native `test_exhausted_entry_table_still_admits_the_finalize` and
`test_full_table_reclaims_a_tombstone_and_never_a_queued_item` fail on the pre-fix behaviour (verified by disabling
the reclaim) and pass after; suite 15/15; full native **2252 / 1 skipped / 0 failures**.

*Why it needed the RAM1 bank reduction first.* The fix costs 4 KB of DTCM (`variables 91,968 -> 96,064`) and at 13
ITCM banks that left `locals free` 2,240, which **hung the boot** during the workspace load (`captures/drain-fix-01`/
`-02`: 19-21 serial lines, stopping at `BOOT,scan,start`, no `Memory]` ticks, no `#CAP`, USB enumerated, no crash
record). Controls proved it was the 4 KB and not the logic: the pre-fix firmware booted and streamed **1136 rows in
20 s** with the same SD state, and a **semantically neutral edit** (`volatile int probe = 0; (void)probe;`) to either
`PersistenceWorkQueue.cpp` or `SyncDrainSaveState.cpp` gave the same 96,064 / 2,240 and the same hang (**105 rows in
35 s, no `BOOT,load,ok`**) with none of the fix's logic. With no movable DTCM left (every symbol >= 128 B is Teensy
core/USB, newlib/libstdc++, or the hot `midiHandler`/`trackManager`; the app's large state is already `DMAMEM`), the
lever was ITCM: `ea9918ac` routes ten cold TUs (LoadLoopJob, CurrentSetBootLoad, RevisionCommit,
CurrentWorkspaceStorage, RevisionLoad, LoopInternalColdHelpers, SetBrowserRead, StorageManagerHitlSerial,
DebugSessionCapture, NoteEditSessionUndoStack) to FLASH in `linker/imxrt1062_t41_lcr.ld`. `code + padding` 425,984 ->
393,216 = **13 -> 12 ITCM banks**, `free for local variables` 6,336 -> **39,104**, and with the fix re-landed on top
`variables 96,064` still leaves **35,008**.

*Device evidence (`captures/drain-fix-03`, git `0a036c63`, base preset track 5 slot 1):* the clear precondition is
**satisfied** (`Clear completed (serial log); precondition satisfied` -- previously `Clear aborted: could not complete
deferred save first`), with **0 `Persistence drain stuck`**, 0 `admission_refused`, 0 `no admittable work`,
0 `FATAL`, 0 `#MEM_EXT_REFUSED`, **3,287 `#CAP` rows** and **12 `PERS,work`** rows (the wedged runs had 0). Boot probe
of the same build: 1,023 rows in 30 s, `BOOT,load,ok`. The run then closed on the harness's
`second overdub transition not confirmed` / `INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)`, the separate ring-shed /
8-bar-cap issue above, not the drain. **Caveat:** `padding` is now 1,256 B, so the next ~1.2 KB of new ITCM code
crosses back to 13 banks and returns the allowance to ~7 KB; taking the next bank means moving another ~32 KB of cold
code. Recorded in [`Teensy-RAM1-Bank-Overflow.mdc`](../../.cursor/rules/Teensy-RAM1-Bank-Overflow.mdc).

**2026-09-18 follow-up: `DIAG,droid_queue` is now off in the default capture env, which removed the 4 KB snap itself.**
The literal that triggered the snap was the emitter's own format string -- `"#CAP,%lu,DIAG,droid_queue,%u,%lu,%lu\r\n"` in
RAM1 `.rodata`. Turning the family off for the default env (`-D SESSION_CAPTURE_DROID_QUEUE=0` in
`[env:teensy41-capture-serial]`, with `teensy41-capture-serial-investigation-diag` now setting `=1` as the explicit debug
gate, and the acceptance env keeping its explicit `=0`) drops ~5,636 rows/run of ring pressure **and** un-snaps the
alignment unit: with the drain fix in tree the build now reads `variables 91,968` / `code 392,152` / `padding 1,064` /
`free for local variables` **39,104** instead of 96,064 / 35,008. So the same fix that had to be reverted at 2,240 now
lands with the largest allowance this tree has had. This is the documented "a new capture format string can cost 4 KB"
hazard in [`Teensy-RAM1-Bank-Overflow.mdc`](../../.cursor/rules/Teensy-RAM1-Bank-Overflow.mdc), and it is why removing a
high-volume *emitter* is a RAM1 lever as well as a ring-budget one.

**2026-09-18 — RC recorded for the post-wrap device reset: the publication boundary neither admits memory nor parks
an allocation failure (`immutable-loop-snapshot-passes` 5.9, RC-PUB-1..3).** User report: *the stress run crashed a
few bars after the overdub wrap, overdubbing the existing overdub*. `captures/session_20260918_101615.log` is memory
exhaustion, not a fault: `boot_reset,crash=0` on the next boot, then
`[ExternalMemoryFirstAllocator] FATAL: both external memory and internal heap are exhausted!` and
`#CAPTURE_RECONNECT`. Sequence: `Overdub wrap committed @ tick 18432` at 114.519 s; then the pool's `<=32K` class
gains one 32768-byte block per second (96 -> 102 blocks) while the internal heap goes 108 -> 72 -> 36 -> 0 KB; the
pool refuses `bytes=32768` at 119.465 s and then `bytes=8224`; `abort()`. Attribution: 32768 is `kSnapshotSlabBytes`
and exactly `snapshotSlabOpen`'s `extmem_malloc` request (`sizeClassFor(32768)` is class 3), and nothing else in the
tree allocates 32 KB blocks in a repeating pattern. The overdub did not leak -- `live` is flat at 8201-8203 KB for
the 45 s before the wrap while the boot reports `pool=8182 KB` -- so the wrap publication simply reached an
already-full pool on a 12-bar record over a loaded ~1.5-1.7 MB workspace root. Control:
`captures/rc1-coalescing-device` (the DEC-073 RC1 run) shows the same ~8-slab post-wrap burst, then settles with
0 `FATAL` and completes its LSV1 `loop_snapshot_tail`, which makes the ~256 KB cost design rather than a leak. Not
RC2: that boot ran no persistence at all (15 `PERS` lines, all boot-time `GlobalMeta`/`FinalizeWorkspace`; the
deferred save is only requested at overdub stop, which never came). RCs recorded in the owning change and
cross-referenced from `successor-capture-reentry-unguarding` 5.9:
- **RC-PUB-1 (primary)** -- `MemoryMonitor::OperationKind::SnapshotReserve` is dead code (zero call sites; that boot
  logged one `#MEM_ADMIT`, the boot probe, and zero `#MEM_DENIED`) and `kSnapshotIdleReserveYieldEnabled` is parked
  false, so `stepPendingSnapshotPublish` runs ungated while OVERDUBBING. Admit at `startNextPendingConstruction` and
  again before the freeze, using the point-of-use probe form (`CompanionHistoryReserve`'s pattern, where the probe is
  the authority) sized to one slab plus the fragment working set; on denial start nothing and leave the work pending.
- **RC-PUB-2** -- `noteFragmentIncorporationFailure` parks only `LoopSnapshotFailureReason::MembershipCapacity`; an
  `Allocation` failure retries the exact fragment forever, and the freeze-failure branch in
  `stepPendingSnapshotPublish` does not count a failure at all. Each retry copies the fragment's
  `LoopSnapshotChangeSet` (a default-allocator vector, i.e. internal heap) into a fresh `LoopSnapshotConstruction`,
  which is what drains the heap into `abort()`. Park `Allocation` too, and count the freeze branch.
- **RC-PUB-3** -- `SnapshotSlabAllocator::allocate` still calls `abort()` when `snapshotSlabAllocate` returns null;
  route it to the existing non-fatal failed-construction path.
Verify: rerun the shape (loaded workspace, 12-bar record, live overdub past the wrap) and require 0 `FATAL`,
0 `#MEM_EXT_REFUSED` and a live device, with either a completed wrap publication or a parked one (previous root
selected, `hasPendingSnapshotPublish()` true) and no `snapshot_fragment,fail` storm; plus a native injected-slab
`Allocation` failure asserting park-then-complete. The steady-state footprint half stays with the memory-pressure /
DEC-065 track above ("idle occupancy leaves no headroom"). No code changed.

**2026-09-18 — RC1 landed: snapshot currency is committed-pass coverage, not a revision stamp (DEC-073).**
The RC above is now fixed for the publisher. `PublishedSnapshotBox`/`PendingSnapshotPublishBox` carry
`coveredPassRevision`; `Loop::publishedSnapshotIsCurrent()` is `coveredPassRevision == committedPassRevision()`
(never `box.revision == playbackRevision`); the box advances its watermark only where it actually enumerates
(`queueSessionExitSnapshot`, both `queueAllCommittedPassesInto` callers, `enqueueLiveOverdubPasses` when it queues a
pass), the session-exit publish passes that watermark, wrap candidates keep their explicit `targetRevision` as both
stamp and coverage, and the undo/redo captured-root selection carries the coverage it was captured at
(`UndoEntry::before/afterPublishedCoverage`, the job-owned and live before-roots,
`TrackUndo::selectPublishedSnapshotForEntry(loop, ref, coverage)`). A captured ref supplied without coverage is
conservatively not current, so a pre-redo root can no longer be re-stamped current. The two reproducers in
`test/test_loop_take_survival` (before-construction and mid-construction coalescing) were red before and are green
after: suite 137/137, full native **2246 / 1 skipped / 0 failures**, `teensy41-capture-serial` SUCCESS (RAM1
variables 91,968 / code 425,560 / padding 424 / locals free 6,336; FLASH code 1,007,080). **Device read done
2026-09-18** on the DEC-071 multi-pass shape with the recipe's undo/redo (firmware
`teensy41-capture-serial-no-grain`, hex `f2f1d8bd…`, tree `d9eb419a` + this change):
`captures/rc1-coalescing-reboot-playback` is a fresh boot (`boot_reset,crash=0` at 3.0 s) and reads
**`result: PASS`, `storage_matches_effective: true`, `384 + 154 + 38 + 24 = 600` effective rows, all five cycles
`inventory_equal: true` / `exact_order: true`, 0 missing, 0 extra, `max_bar_refs=84`, 0 `RING,overflow`** -- the
24-row second-overdub pass that `content-verify-58` lost is audible after reboot. The base run
(`captures/rc1-coalescing-device`) shows the post-redo republish (`q=0,a=4` at 95.474 s) as the root the LSV1
tail gathers at 99.640 s; its only report issue is `persistence_stage_missing:result`, the run window closing while
the now-complete 600-row tail writes (device log: `PERS,result,…ok` at 111.14 s). **RC2 landed (DEC-074):** the LSV1
body admission requires a settled root (`hasSettledPublishedSnapshotForSave`: coverage-current **and** no publication
pending); `decideLoopPersistBodyFormat` resolves the per-loop format once at `Header` and pins it
(`loopWriteUsesLsv1`), so a preamble can no longer be left without its tail; an unsettled loop writes the
passes-authoritative **legacy** body (LSV1's readers reject a missing `LoopSnapshot` section, so a rootless LSV1 body
is not expressible); and a bounded settle re-admission (128 writer turns, nothing written while waiting) lets a
transient pending republish finish first. `test_storage_loop_io` 47/47 (two fixtures; the admission one is red on the
pre-RC2 predicate), full native green, `teensy41-capture-serial` RAM1 locals free **6,336** (a new telemetry tag had
cost 4 KB via `ALIGN(4096)` churn; dropped). **Device read closed 2026-09-18** (firmware `86fd8f07…`, env
`teensy41-capture-serial-no-grain`): the save took the settled LSV1 path (`tail_gather_begin`, `LoopSnapshotTail`
1389 steps, `q=0` publishes), 0 `RING,overflow`, 0 `FATAL`, post-reboot `storage_matches_effective: true`; the
verify's 4 unexplained wrap rows are a setup artifact (that session was recorded at 240 BPM instead of 120, giving
a 17-bar record for an 8-bar request). The two blocked attempts' ring overflow is fixed by
`-D SESSION_CAPTURE_LCR=0`. Remaining: 6.1/6.2/6.3 and the owning change's RC-PUB-1..3.

**2026-09-18 — RC for the post-reboot overdub loss: the LSV1 body save gathers a root that can predate
the current committed passes.** User report: *after reboot the second overdub isn't loaded (or
stored)*. Investigated on the canonical `base` run (`captures/gus-sanity-01`) plus a post-reboot
dump/capture (`captures/reboot-01-playback`); details and the fix options are recorded as an **RC in
`successor-capture-reentry-unguarding` task 5.9**.

Short version: after the recipe's `Undo`/`Redo`, `PassesResolve` reports **57** display rows (the
committed passes are complete — post-reboot `!CAPTURE_CONTENT` is record 94 + overdub 32 + overdub
32 = effective 142), but the deferred save's LSV1 tail gather (`tail_gather_begin` at 154.269 s) had
captured a coalesced publication (`snapshot_unit publish` at 153.941 s) that encodes the **undo**
state — the loaded body root resolves to **48** rows and playback is missing the 24-39 overdub range.
`writeLoopPersisted` / the gather require neither `publishedSnapshotIsCurrent()` nor
`!hasPendingSnapshotPublish()`, and the loader installs the body root as current with no reconcile
(the boot hydration lane is gated off). Concrete mechanism: the session-exit publish calls
`storePublishedSnapshot(box.unpublishedCandidate)` with the default revision, stamping the candidate
with the **current** `playbackRevision` regardless of which committed-pass state it was built from,
so `publishedSnapshotIsCurrent()` cannot lie-proof "content covered that revision"; and the queue
accounting (`q`/`a`) advanced `4 → 0` / `0 → 5` while the published candidate still resolved to the
undo-shaped 48 rows. **Recommended fix (owner call): (b) publisher coalescing/coverage correctness
FIRST, then (a) a persistence safety gate; do NOT implement load-time reconciliation.** (b) pins
*queue consumption and candidate coverage advance together*, with a carried `coveredPassRevision` so
"current" means content-equivalent to the committed-pass authority. Acceptance adds: immediately
before save admission the selected root and `PassesResolve` must already agree. The stale uncommitted
edit that had marked 5.9 closed and ticked 6.1 was reverted.

**2026-09-18 — the undo stack silently lost its publication capacity, so a second overdub stopped
adding a GUS level; fixed (`successor-capture-reentry-unguarding` 8.3).** The user report: *the undo
does not get to +3, it stays on 2; after stop I cannot undo/redo this overdub a second time, it
undoes the first overdub.*

Root cause: `TrackUndo::eraseUndoEntriesForSlot` filters the stack into a local vector and
move-assigns it. The move takes the local's buffer, so a rebuild smaller than the protected prewarm
(a record stop on a cleared slot leaves it nearly empty) dropped the stack's reserved capacity.
`prewarmProtectedUndo` reserves `kUndoStackProtectedCapacity` (32) exactly so the deferred
publication never has to grow the stack at its atomic publish; with the reserve gone,
`TrackUndoPublication::begin` rejected at `cursor >= capacity`, and a stop whose entry was rejected
still committed its pass. The device diagnostic added in `stepDeferredUndoPublication` caught it
directly: `DIAG,gus,publish_begin,ok=1,phase=1,cursor=2,size=2,cap=4` (`gus-undo-07`).

Fix (`433fea68`): re-reserve the rebuilt vector to `kUndoStackProtectedCapacity` before the
move-assign; the constant moved to `GlobalUndoStack.h` so `prewarmProtectedUndo` and the rebuild
share one value. `stepDeferredUndoPublication` / `stepProtectedUndoDrain` also emit session-capture
`DIAG,gus,publish_begin` / `DIAG,gus,drain` rows.

Evidence. New native fixture `test_erase_preserves_the_protected_publication_capacity` (mirror of the
rebuild in `test_global_undo_slot_scope`) fails without the fix; full native **2,244 cases / 1
skipped / 0 failures**; `teensy41-capture-serial` SUCCESS with RAM1 locals **6,400** unchanged.
Device gate via the skill's canonical entry point (`host_midi_hitl.py run --preset base`, two overdub
sessions + recipe undo/redo), `captures/gus-sanity-01`, **`Result: PASS`**: `MIDI: Undo (entries=3)`
— the depth the report expected — `DIAG,gus,publish_begin,…,cap=32` for both sessions (was 4),
`DIAG,gus,…,passIds_n=2,src=deferred` per entry, and `DISP across undo/redo: 56 → 48 → 57`.
The 8-bar/4-bar variant of the recipe is currently unreliable on the host MIDI path (`MIDI in
messages 0`, record sealing at ~475 ticks across `-08`..`-11`), which is why the canonical 2+2 preset
is the gate here.

**2026-09-18 — the deferred stop left the display serving pre-commit notes; fixed
(`successor-capture-reentry-unguarding` 8.2).** The user report: *the second overdub disappeared on the
display after overdub stop to playing, and reappeared at the loop wrap.*

Root cause: the synchronous stop marks the committed pass's display ranges
(`finalizeCommitSideEffects` → `Loop::markAffectedDisplayCacheRanges`), but the deferred terminal
commit only called `Loop::completePreparedCaptureAndCompanionsAtomic` →
`notifyCommittedContentChanged` → `markPassDerivedStale`, which sets `visualCacheDirty` but
deliberately leaves `dirtyBars` as the previous rebuild left it. After a `slice_clean` that set is
empty, so `rebuildVisualCacheIdleSlice` took its `slice_nodirty` early return and cleared the stale
flag **without rebuilding** — the cache kept the pre-commit note set until something else marked bars
(the wrap's `markAffectedDisplayCacheRanges`), which is exactly the "reappeared at loop wrap".
Device `captures/gus-undo-04` shows it: after the second overdub stop the frame fell **176 → 144 at
74.3 s** with `VCACHE,slice_nodirty notes,128`.

Fix (`a91cb719`): `markDeferredCommitDisplayRanges` marks the committed pass plus the interval's
prepared companion ids in every deferred terminal commit (`commitPreparedStop`,
`commitPreparedStopped`, the two transport-stop recovery paths of `advanceAtomicTransfer[WithUndo]`,
and `commitSupersededCausalBase`).

Evidence. New native fixture `test_deferred_stop_commit_marks_the_display_ranges_of_the_new_pass`
seeds the clean/empty-dirty precondition, drives the deferred stop, and requires the cache to be
stale with a non-empty dirty set; it fails without the fix. Full native **2,242 cases / 1 skipped /
0 failures**; `test_continuous_capture` 76/76; `teensy41-capture-serial` SUCCESS with RAM1 locals
**6,400** unchanged. Device `captures/gus-undo-06` (managed `base`, two overdub sessions, firmware
from `a91cb719`): the frame holds **176 from the second stop through the undo** (`VCACHE,stale_all
dsz,9 dcnt,9` then `slice_clean notes,176`), the undo drops it to 144 and the redo restores 176
(`DISP across undo/redo: before=176 after_undo=144 after_redo=176`) — no intermediate post-stop
drop. Oracle robustness: the undo/redo display clause now bounds the post-redo sample at the next
undo, so an `undo_redo_cycles > 1` run cannot report a false redo failure (run `-06` ran three
cycles); `scripts/test_capture_transitions.py` 48/48.

**2026-09-18 — the GUS recorded only the last pass of an overdub session, so one undo removed a
partial overdub; fixed (`successor-capture-reentry-unguarding` 8.1, DEC-068 follow-up).** The user
report: *the GUS is setup wrong — it undoes the last wrapped overdub pass, not the entire overdub
session (all overdub passes)*.

Root cause, localised with a deferred-stop reproducer: `Loop::captureSessionExitTail` runs at the
deferred stop **admission** and swaps this interval's wrap identities out of the shared loop
(`overdubSessionPassIds_` / `overdubSessionCompanionIds_`) into its own session-exit receipt, so a
successor opens a clean session (DEC-068). The deferred **undo** collection, however, read the live
session lists at its later `UndoCollection` phase — by then they are empty and may belong to a
successor — so `collectOverdubSessionUndoPasses` contributed nothing and the entry carried only the
final pass appended by `includeFinalPass`. The native reproducer showed it directly:
`limit=0 passCount=1 entryPassIds=0`, i.e. the wrap pass was never in the entry, and
`appendOverdubCapturePassIds` therefore returned one id.

Fix: the deferred Pass/Companions phases now read the interval's own **session-exit receipt**.
`Loop::SessionUndoCollection` gains index-aligned receipt source pointers plus the bound
`receiptIdentity`, which also replaces the live `overdubSessionUndoGeneration_` guard in
`stepCollectOverdubSessionUndoPasses` / `collectedOverdubSessionUndoPassesCurrent` (a successor
opening its session must not deny the predecessor's own collection).
`beginCollectOverdubSessionUndoPasses` takes an optional receipt source;
`beginCollectOverdubStopUndoPasses` takes the receipt ring index (default
`kInvalidSessionExitReceipt` keeps the live-session source for existing callers); and
`Track::stepDeferredUndoCollection` passes `job.admission.sessionExitReceiptRing`. The deferred route
also now emits the same `#CAP,DIAG,gus,…` row as the synchronous route (with `src=deferred`), so the
entry's pass count is device-visible.

Evidence. New native fixture `test_deferred_stop_undo_entry_carries_every_session_pass`
(`test_continuous_capture`) drives a wrapping overdub through the deferred stop and asserts the
published entry carries the wrap pass and the final pass, and that `appendOverdubCapturePassIds`
yields both; it fails without the fix (`Expected 2 Was 0`). `test_continuous_capture` 75/75; full
native **2,241 cases / 1 skipped / 0 failures** (was 2,240); `pio run -e teensy41-capture-serial`
SUCCESS with RAM1 variables 91,904 / code 425,496 / padding 488 / **locals 6,400** unchanged.

Device gate (managed `base`, `captures/gus-undo-01`, firmware from `7a4c8bfc`,
`teensy41-capture-serial-no-grain`, DEC-071 shape + recipe undo/redo): `undo=1 redo=1`,
`overflow_free=True`, and the new deferred telemetry
`#CAP,66172706,DIAG,gus,slot,kind=1,passId=3,passIds_n=2,src=deferred` — the entry carries the wrap
pass **and** the final stop pass. The same run's `DISP across undo/redo: before=189 after_undo=124
after_redo=188` shows a single undo now removes the whole overdub (~65 display rows) and the redo
restores it, against the partial `194 → 179 → 193` of the pre-fix `display-undo-03`. That run's base
report is `FAIL (FIRMWARE_CONTRADICTION)` on `record_stored_note_grid` (4 bad deltas of budget 2) and
`overdub_stored_span span_ok=False` (`last_tick=6810` against a loop that sealed at 8 bars / 6144
ticks) — the known pacing/loop-length and re-verification-window artifacts of this recipe, not the
GUS entry. A second attempt (`gus-undo-02`) is `INCONCLUSIVE_CAPTURE (SETUP_UNCONFIRMED)` (`undo=0
redo=0`, transport STOPPED at the stop press), so it is not evidence either way. Owed: scope the
stored-verification oracle to the pre-undo window so the redo's re-verification drain cannot fail the
base run, and (for the base PASS) the record's stop-time quantisation slop (8 vs 9 bars).

**Follow-up gap, fixed in `c380c030`.** The user reported the symptom again — *the undo only shows 1
while there should be 2; undo removes the notes from the display but they are still audible* — which
is the same partial-undone shape from a path the receipt-sourced collection never sees.
`beginDeferredOverdubStop` took `captureSessionExitTail` **before** the causal-base, credit,
raw-range and freeze checks that can still deny admission. When one denied, `stopOverdubbing` fell
back to the synchronous commit, whose `pushOverdubSessionOnStop` read the now-empty live session and
recorded only the final pass. The tail capture now runs once admission is committed (just before
`Reentry window open`, so a successor still cannot observe the shared session); an invalid ring is no
longer a post-transfer denial — it logs `session_exit_tail_denied` and the deferred commit uses the
legacy one-shot handoff. Device `captures/gus-undo-03` (managed `base`, firmware `7708d605…` from
`c380c030`, git clean) is a clean **`Result: PASS`** — **`MIDI: Undo (entries=2)`** (the depth the
report expected), `#CAP,65963077,DIAG,gus,slot,kind=1,passId=3,passIds_n=2,src=deferred`, and
**`DISP across undo/redo: before=194 after_undo=128 after_redo=192`** (one undo removes the whole
overdub, the redo restores it), with `stored overdub span 64/63 count_ok/span_ok/bars_ok` and 0
`RING,overflow`. Full native 2,241 / 1 skipped / 0 failures; RAM1 locals 6,400 unchanged. If a run
still shows a one-pass entry, capture the `DIAG,gus` row's `src` (`deferred` = the collection path;
absent = the synchronous fallback) and `MIDI: Undo (entries=…)`.

Oracle robustness in the same stretch: a two-overdub run (`captures/gus-undo-04`, `entries=3`,
`passIds_n=0` for each single-pass session, correctly a legacy `passId`-only entry) showed the
pre-undo frame still ramping (`DISP across undo/redo: before=144 after_undo=160 after_redo=176`), so
`_verify_undo_redo_display` produced a spurious `display_undo_kept_the_overdub`. The pre-undo sample
is now the **steady peak** of the window rather than its last row; replayed on runs `-01`/`-03`/`-04`
all three pass, and `scripts/test_capture_transitions.py` is 47/47. That run's base report is also
`persistence_stage_missing:result` — the deferred save did not finish inside the harness window, the
same timing artifact as `display-undo-01`.

**2026-09-18 — the 5.8 undo/redo fix made playback correct but left the OLED painting the undone
overdub; fixed (`immutable-loop-snapshot-passes` 5.8a).** The regression is the display half of the
same defect family, and it was *introduced into view* by 5.8: once playback correctly stops emitting
the disabled pass, the display is the only tier still holding it.

Root cause (device `captures/content-verify-65`, the DEC-071 shape with the recipe's undo/redo):
`Loop::retainCapturePreviewAtSeal` keeps the just-sealed pass's closed rows in
`sealedCapturePreviewNotes_` as a display bridge, and `sealedCapturePreviewIsCovered()` proves the
bridge may be released by testing each row against the **committed** `visualCache`. A global undo of
an `OverdubPassAdded` entry disables the pass through `Loop::setCapturePassState`, which removes its
rows from the committed representation — so coverage can never hold again and
`releaseSealedCapturePreviewIfCovered()` never releases. `applyDrawSourceOverlays` unions the
uncovered sealed rows into every frame, and the OLED therefore kept painting the removed overdub
forever. The telemetry is exact: `DFRAME` 194 before `Undo` → **193** after, `DISP` `frame=193`
against `visual=180` for the whole window, and the redo leaves it at 193 because it never left;
`VCACHE,slice_clean notes,180` is the record-only committed cache and the extra **13** rows are the
sealed handoff (`194 = 181 + 13` before, `193 = 180 + 13` after). `resetPassTimeline` already names
this hazard and clears the handoff; `setCapturePassState` is the same state change reached from the
undo/redo path and was the one that missed it.

Fix (`src/Loop/LoopCapture.cpp`): when a record or overdub pass transitions to
`CapturePassState::Disabled`, call the existing `clearSealedCapturePreview()` hook before
`notifyCommittedContentChanged()`. Enabling a pass does not clear it — the preceding undo has
already released the bridge, and the re-enabled rows resolve from the committed pass set.

Evidence. New native fixture
`test_disabling_a_capture_pass_releases_the_sealed_draw_source_handoff`
(`test_loop_take_survival`): it drives the real undone/redone frame through the host twin of the
draw-source gather and asserts the disabled pass's note is gone after the undo and present again
after the redo; it fails without the fix (`TEST_ASSERT_FALSE(loop.hasSealedCapturePreview())`,
`Expected FALSE Was TRUE`). Full native **2,240 cases / 1 skipped / 0 failures** (was 2,239);
`pio run -e teensy41-capture-serial` SUCCESS with RAM1 variables 91,904 / code 425,496 / padding 488
/ **locals 6,400** (unchanged).

Harness, in the same commit: the base runner's display check only asserted a *non-empty* frame after
the overdub stop, so this regression was invisible to it. New `_verify_undo_redo_display` compares the
`DISP` frame note count before the undo, between undo and redo, and after the redo, and emits
`display_undo_kept_the_overdub` / `display_redo_did_not_restore_the_overdub`; the redo clause is the
discriminator (the buggy frame was 193 both before and after the redo). Run on the real buggy log
(`content-verify-65`) it reports `frame_before_undo=194`, `frame_after_undo=193`, `frame_after_redo=193`
and the issue, exactly as intended; `scripts/test_capture_transitions.py` 46/46.

**Device gate CLOSED:** `captures/display-undo-03` (managed `base`, git `9eda0984` clean, firmware
`47e05029…`, `teensy41-capture-serial-no-grain`, the DEC-071 shape with the recipe's undo/redo) is
**`Result: PASS`** with `undo=1 redo=1`, 0 `RING,overflow`, `overflow_free=True`, persistence
`result=ok` (45.5 ms), display `overdub_ok=True transport_ok=True`, and **`DISP across undo/redo:
before=194 after_undo=179 after_redo=193`** — the frame drops the overdub on `Undo` and restores it on
`Redo`, against the buggy `194 → 193 → 193`. (A first run, `display-undo-01`, showed the same direction
`200 → 193 → 201` but reported `persistence_stage_missing:result` because the recipe omitted
`--deferred-save-wait-ms 120000`; a re-run without a hard reset then came back
`INCONCLUSIVE_CAPTURE`/`RING,overflow` off the un-drained device, and the re-flash + exact recipe
produced `display-undo-03`. The stale-device recovery is the documented one.)

**2026-09-18 — successor change 5.9 + 5.11 CLOSED, and the defect they found is fixed
(`immutable-loop-snapshot-passes` 5.8).** The user report was concrete: *the overdub wrap creates a second pass
which the display shows but which is not audible, and the GUS is missing its entry.* Two independent holes left
the published root not representing the committed passes (the display resolves from `passes`, playback from the
root):

1. **Undo/redo** — `TrackUndo::applyPublishedSnapshotSelect` selected `undo ? before : after` and did nothing
   else. The session-exit after-attach (Pin A) is asynchronous, so a redo whose after-root never landed kept the
   pre-undo root; the entry's pass restore then bumped `playbackRevision`, making that root stale, and the
   stale-root overlay has no unpublished receipt left to fold the passes back in. Live capture (MIDI only, no
   serial open, so no reset — not save/load): `playback_build` eventCount **384 → 576 → 384 after `Undo` → still
   384 after `Redo`** against 576 effective.
2. **Eligibility** — `Track::processDeferredIdleMaintenance` admitted snapshot idle work only for
   `liveRecordPastFirstBar || hasInFlightSnapshotConstruction() || hasWrapJournal`, so a publication *queued*
   with none of those (a non-wrapping overdub stop, or the undo/redo rebuild) never started. This is the half
   that explains a plain **stop → play again** losing the last pass.

Fix (commit `31933183`): new `Loop::requestCommittedPassesRepublish()` publishes every active committed pass on
the **normal** session-exit lane (the boot hydration lane is gated out in-session by the deliberate
`allowLoadedSnapshotHydration` stub) and **does not clear the published root**, so the previous source stays
audible until the replacement lands; `applyPublishedSnapshotSelect` calls it after **both** an undo and a redo of
a `RecordPassAdded`/`OverdubPassAdded` entry; the eligibility clause now also admits
`pendingSnapshotSessionExitQueued()`; the shared enumeration is factored into `queueAllCommittedPassesInto`.

**Device gate closed:** `captures/content-verify-65` (DEC-071 shape with the recipe's undo/redo; git `31933183`
clean, firmware `f82ee9b9…`, acceptance build) is a clean base **PASS**, and
`captures/content-verify-65-playback/content-review.json` is **`result: PASS`** — `storage_matches_effective:
true` (record 384 + overdub 157 + 35 == effective 576), **all five cycles `inventory_equal: true`,
`exact_order: true`, 0 missing, 0 extra**, `max_bar_refs: 72`, `capacity_path_exercised: false` (parity proof,
not a capacity re-stress). Same run: 0 `companion path stalled`, 0 `raw pass only`, 0 `RING,overflow`, 0 `FATAL`,
0 `MEM_DENIED`, deferred save `ok`. Full native **2,239 / 1 skipped / 0 failures**; `teensy41-capture-serial`
SUCCESS with RAM1 variables 91,904 / code 425,496 / padding 488 / **locals 6,400**. **Owed follow-ups:** the
native redo-requeue fixture (recorded in 5.8), and the load-time reconcile for loops already saved with a stale
root (the fix applies to new activity only).


**2026-09-18 — successor 5.9 RUN ON DEVICE and it FAILS: a committed overdub pass never reaches the MIDI
output.** The DEC-071 multi-pass shape was created on the flashed tree (`content-verify-58`: record 8 bars /
overdub 8 bars from bar 3, track 4 slot 1, `teensy41-capture-serial`), and the base run is a clean **PASS**:
`record loop length 6912/6144` (the known 9-bar seal for an 8-bar request), `ST PLAYING->OVERDUBBING: 2`,
`OVERDUBBING->PLAYING: 2`, `stored overdub span count=64/63 count_ok=True`, display `overdub_ok=True
transport_ok=True`, `persistence handoff result=ok`, undo=1 redo=1, `overflow_free=True`, firmware
`d298f9ea…` (git `4fde5c03`, clean). So four of 5.9's five checks hold in one clean run. The fifth does not:
`verify_playback_content.py` on `captures/content-verify-58-playback` reports
`storage_matches_effective: true` (record 384 + overdub 157 + 35 + 24 = effective 600) but **every one of
five cycles has `inventory_equal: false`, `rows=572`, `missing_count=28` (27 unexplained + 1 residual)**.
The loss is deterministic and device-side (the identical multiset in all five cycles):

- the **whole second-overdub pass `overdub,2`** is inaudible — 24 rows (NoteOn/NoteOff notes 12–19 at ticks
  4975–6503 plus CC74 0,9,…,63), present in `passes` and in `effective`, absent from the output;
- **3 rows of the first overdub's wrap continuation** are also lost: one copy each of the wrap-crossing pair
  `(144,4,28,98)`@6911 / `(128,4,28,0)`@95 and `(176,4,74,84)`.

DEC-071's own claim still holds here (the pre-wrap `overdub,0` pass and its continuation are otherwise
audible), so this is the same **class** as DEC-071 — a committed pass missing from the playback source —
with a different pass. The same 24-row loss reproduced on `content-verify-53-playback`. **5.9 stays open;
5.11 stays open with it** (its own clauses — no `companion path stalled`, save completes, four checks in one
run — do hold on run 58). Next step: a fix plus a re-verification, not a tick.

**Root cause localised 2026-09-18 (device, two independent resets): the persisted representation can lag the persisted passes, and playback follows it.** `content-verify-59` (acceptance build, single-overdub DEC-071 shape) is a clean base PASS, yet playback emits only the record pass: `raw_counts` record 384 + overdub 157 + 35 = effective 576, `rows=384`, **192 missing on every cycle**, and a second independent reset+dump (`content-verify-59b-playback`) reproduces exactly 384/192. So the device's *passes* are complete while the playback source after restore is not. `src/StorageLoopIo.cpp` `writeLoopPersisted` serializes the loop body's note/event snapshot from `loop.publishedSnapshot()` with no check that the published root is current with the committed passes, and `applyLoadedLoopSlotBodyCommit` installs that body snapshot as the root (`storePublishedSnapshot(loadedLoopSnapshot, 0)`) with no reconcile against the restored passes. After a reboot there are no unpublished passes for the stale-root overlay to fold in, so the gather serves the stale root and the committed overdub is permanently silent while present in storage/effective. **Correction (healthy-ring telemetry):** `content-verify-59`'s log puts `PERS,request` at 78.6 s, ~10 s after the last `snapshot_unit,kind=publish` (68.6 s) -- the save did **not** overtake the publication, so this is not a save/publish ordering race. And because `loopUsesLoopSnapshotBodyFormatV1ForSave` requires a current root, a stale root would have written a **legacy** file whose load installs **no** root (silence), not record-only playback. The persisted body was therefore LSV1 with a root that was *current at save but already missing the overdub*: the prime suspect is the **publisher's candidate completeness**. History check: all the guards in that area (`f139b2a7`, `e87272a8`, `4fcfbcb3`, `2b948d2a`) are still in the tree, and nothing in the save/snapshot/playback path changed between the last known-good run (content-verify-52 on `7e9810ee`) and now except 2.4 slices 1-2. What *was* deliberately retired earlier and no longer masks an incomplete root is the 6.3 compatibility machinery (`gatherCommittedEvents*`, C1-a gather bridges) plus the LSV1 load that installs the body root with no rebuild/fallback (`dde9e64a`); the comment in `TrackPlaybackWindowBuild.cpp` still promises a 'chunk-ref merge of committed passes (DEC-016)' fallback that does not exist in code. **Fix options (owner decision -- it changes the persistence/publication contract):** (a) gate the loop-persist admission on `!loop.hasPendingSnapshotPublish() && loop.publishedSnapshotIsCurrent()` with a bounded re-admit; (b) serialize the body from the committed passes instead of the live root (content-sized, but the save is already sliced); (c) reconcile at load (rebuild the root from the restored passes when they disagree).

**ROOT CAUSE LOCALISED 2026-09-18 (live capture, no reset): the global REDO of an overdub entry restores the passes but not the published root, so the redone overdub is permanently inaudible.** A MIDI-only capture (no serial open, so no reset: `scripts/hitl/live_playback_capture.py`) proves the loss is *live*, not save/load -- the live capture has 0 rows for the overdub notes (24-39) exactly like the post-reset one. The live gather telemetry pins the moment: `playback_build` eventCount 384 (rev 1/2) -> 576 (rev 3, after the session-exit publish at 68.85 s) -> **384 (rev 4) after the `Undo` at 78.53 s** -> **still 384 (overlay, rev 5) after the `Redo` at 81.67 s**, while the dump's `passes`/`effective` keep 576 the whole time. Code: `TrackUndo::applyPublishedSnapshotSelect` (`src/TrackUndo.cpp:148`) selects `undo ? beforePublishedSnapshot : afterPublishedSnapshot` and is a **no-op when the redo's `afterPublishedSnapshot` is empty**; the redo's pass restore bumps `playbackRevision` afterwards, which makes the pre-undo root stale, and the stale-root overlay has no unpublished receipt left to fold the redone passes in. The application order is correct (root selected after the restore) and the attach machinery from `8aa29307` is all present -- registration (`ensureSessionExitPublishedUndoAttachRegistered`), handler (`TrackUndo::attachAfterPublishedSnapshot`, which only accepts `RecordPassAdded`/`OverdubPassAdded` with a matching id and loopId) and the publisher's call at `LoopSnapshotPublish.cpp:2194`. Nothing guarantees the after-root is captured before an undo can consume the entry, and nothing republishes on a redo that lacks it -- that hole is the defect. Next: decide the fix (capture/attach the after-root before the entry can be undone, or republish on a redo whose after-root is missing) and re-verify the DEC-071 shape with undo/redo in the recipe.

**Observability fix landed in the same session** (`SESSION_CAPTURE_DROID_QUEUE=0`, folded into the existing acceptance env `teensy41-capture-serial-no-grain`): `DIAG,droid_queue` was ~5.6k rows/run and, with the per-slice `PERS,work` trace, overflowed the 1 MiB ring. With it dropped, the same DEC-071 run fell from 20,800 to **9,237** `#CAP` lines and came back a clean PASS (`content-verify-59`), so the base-run oracle is trustworthy again. RAM1 unchanged at locals 6,400 for both envs.

**Observability blocker found in the same session (base-run oracle, not content).** The 8-bar recipe sheds
the 1 MiB `#CAP` ring, and the dominant consumer is `DIAG,droid_queue` (~5.6k rows/run, `MidiHandler.cpp`
`SC_DROID_FEEDBACK_QUEUE`) plus `PERS,work` (~3.5k) — **neither is dropped by
`teensy41-capture-serial-no-grain`**, which only drops `DIAG,stop_grain`. Four runs
(`content-verify-54..57`) came back `INCONCLUSIVE_CAPTURE (SETUP_UNCONFIRMED)` because the harness never
delivered the overdub-stop press (the device log has no `Overdubbing stopped`; the only Record/Overdub
presses were the double-undo and triple-redo), so the live pass was never committed and the take collapsed
to a single overdub pass. The two clean runs both sealed at 6912 ticks with ~12.5k `#CAP` lines; the
degraded ones sealed at 6144 with ~20.8k. A volume mitigation for `droid_queue` (sampling as
`DIAG,stop_grain` now is, or an acceptance env) is needed before the base report can carry the four checks
in a clean run again.

**2026-09-17 — successor change task 5.5 CLOSED: the adversarial mid-grain checkpoint set is complete
and runs in the default gate.** 5.5 was the only remaining host-only item of
`successor-capture-reentry-unguarding`, the one D8 (DEC-072) directly unblocked, and the one still
hiding coverage behind `REENTRY_RED=1`. What landed:

- **The missing "after the raw-range stage" checkpoint:** `CanonicalRewrite`
  (`test_adversarial_reentry_at_a_canonical_rewrite_checkpoint`) — the raw range is sealed and its
  canonical order is built, the canonical copy is being rewritten, and no staged pass exists yet, so
  this checkpoint proves a successor's capture start does not disturb the predecessor's frozen
  canonical claim either.
- **One checkpoint beyond the named set, because the phase enum is not execution order:**
  `PrepareTransfer` dispatches before `CanonicalTail`, and the grain that enters `PrepareTransfer` also
  stages the pass and takes the receipt (`Loop::takePendingCapturePass`), so that state cannot be named
  by a phase threshold. It is expressed as *the turn the job first owns its staged pass*
  (`test_adversarial_reentry_at_the_staged_pass_ownership_checkpoint`, stepping until
  `jobPass(track) != nullptr`) — exactly the state a successor's `beginCapture` used to destroy.
- **`REENTRY_RED` is gone:** the `CompanionHistory` checkpoint is in the default suite. The env var is
  now a no-op; `ONLY_COMPANION=1` remains as the focused single-case runner.
- **The checkpoints are tight:** the helper asserts each phase checkpoint by **equality** (a checkpoint
  that silently ran past its phase would not be the state it claims), and the ownership checkpoint
  asserts it pauses on `CanonicalTail`, the phase that follows the non-yielding staging grain. Five
  checkpoints total: `CanonicalOrder`, `CanonicalRewrite`, `CompanionHistory`, staged-pass ownership,
  `AtomicTransfer`.

Evidence: `test_continuous_capture` 74/74 (all five in the default gate), `ONLY_COMPANION=1` 1/1,
`REENTRY_RED=1` 74/74 (redundant), `test_playback_prewarm` 173/173, full native **2,239 / 1 skipped /
0 failures**, `teensy41-capture-serial` SUCCESS with RAM1 variables 91,904 / code 425,496 / padding 488
/ locals 6,400 (test-only increment), both `openspec validate --strict` checks valid. **Next in that
change:** 5.9 (the `hitl-content-verification` audibility proof), then 5.11 on its run, then 6.2 as a
device session, then the 6.1 re-run and 6.3 closeout.

**2026-09-17 — parent change task 2.4 (active clock path consumes prepared views): slices 1 and 2 are
landed and the slice-3 decision is made (not required); 2.4 ticks on the 5.3 device measurement.** This is
the active code work on `resumable-midi-boundary-playback-maintenance`, alongside the successor change's
own pick-up order (best next there: 5.5). Where it stands:

- **2.4 slice 1 (`4bd368ca`).** `openspec/changes/resumable-midi-boundary-playback-maintenance/ARCHITECTURE-REVIEW.md`
  is new (the change had no phase-gate artifact) and carries the per-phase gate for 2.4 -- ownership / state
  transition / execution bound all **NO**, reuse **YES** -- plus the architecture-checkpoint answers.
  `Track::resetPlaybackOrderHotRebuildCount()` / `playbackOrderHotRebuildCount()` count content-sized
  order builds (heavy merged build, `rebuildPlaybackOrder`, full `reanchorPlaybackIndex`) that run while
  `anyTimingCriticalTrackActive()`. The counter is in **RAM2/DMAMEM**: a 4-byte DTCM addition here costs
  4 KB of RAM1 local allowance through the alignment edge, and a `Diagnostics::Counter` slot was rejected
  for the same reason. Two native tests prove the detector fires on active transport and stays clean for
  the STOPPED-oracle build.
- **2.5a parity oracle (`f44e0077`, `f76fa22f`) -- prerequisite, not 2.5 completion; 2.5 still finishes
  after 2.4.** Differential harness in `test_continuous_capture`: `DifferentialRow`/`DifferentialTrace`
  capture (tick, kind, channel, note, value) in **emission order**; `consumePairOverRange` drives a
  prepared `(mergedEvents, playbackOrder)` pair through `advancePlaybackCursor`; `differentialTracesEqual`
  compares ordered rows. Fixtures landed in the owner's priority order: plain pair parity, the permanent
  **negative control**, **seal admitted into the pair** (the transition that invalidated literal
  request+hold: the stale pair cannot emit the admitted rows, a full replacement does, and the streams
  differ), **same-tick Off-before-On**, and **the anchor/cursor seed is part of the pair**.
- **Finding that narrowed 2.4.** `Loop::tryGatherPublishedSnapshotEventsInWindow` returns false when there
  is no published root and the merged consumer then holds empty, so the post-commit publish gap is not a
  second active rebuild case; the reachable active rebuild is concentrated in seal admission.
- **2.4 slice 2 — LANDED 2026-09-17 (immutable pair + atomic publication); the measurement decides
  against slice 3.** Pinned invariant: *playback never observes `mergedEvents` and `playbackOrder`
  from different generations; a pair becomes consumable only when both members and their cursor/reanchor
  state are complete for the same source generation.* `PreparedPlaybackPair`
  (`include/TrackPlaybackRuntime.h`) carries `generation`, the live source identity, the full-loop
  `coverage`, a pointer to a dedicated prewarmed `FixedPlaybackEventStorage`, `eventCount` and the
  `cursorSeed` resolved against the publication phase. `LoopPlaybackRuntime::publishPreparedPlaybackPair`
  fills that buffer and replaces the whole descriptor in one assignment inside a non-yielding turn; the
  off-clock owner is `Track::publishPreparedPlaybackPairFromWorkingWindow` /
  `Track::servicePendingPreparedPlaybackPair`, serviced one preparation per turn from
  `runDeferredLoadAndDisplayFrame` (`main.cpp`). `resolvePreparedPairConsume`
  (`src/Track/TrackPlaybackWindowBuild.cpp`, shared with the fixtures) is the clock path's entire
  decision: consume a current pair, otherwise record exactly one request and keep consuming the
  published P0. `playCommittedLoopMidi` no longer calls `::rebuildPlaybackOrder` /
  `reanchorPlaybackIndex` for the full-loop merged path; the pair's identity `order` feeds the existing
  fixed-stream adapter and the new seed is copied on the first tick after publication (a wrap rewinds to
  0). `Loop::hasPendingOverdubSealAdmission()` is the non-allocating source-change signal for the seal
  receipt. The long-loop windowed gather and the cold first audible turn keep the in-place builder,
  which the slice-1 detector still counts. Measurement (native, one bar = 768 ticks = 24 MIDI clocks):
  `prepared_pair_request_count` 1, `prepared_pair_publish_count` 2, **`prepared_pair_hold_ticks` 8**,
  **`prepared_pair_max_latency_ticks` 7**, `prepared_pair_rebuild_units` = the replacement window's rows
  (4 -> 6 in the fixture), `playbackOrderHotRebuildCount` 0 across the decision loop and 1 for the
  off-clock order rebuild. Against the audibility edge: 8 ticks is ~1% of the one-bar budget (768) and
  exactly one MIDI clock, i.e. far inside the next 16th-note step (48) — **slice 3 (b), a bounded
  incremental order update, is not required for 2.4**. Evidence: `test_continuous_capture` 67/67,
  `REENTRY_RED=1` 68/68, `ONLY_COMPANION=1` 1/1, `test_playback_prewarm` 173/173, full native **2,232 / 1 skipped /
  0 failures**, RAM1 variables 91,904 / code 425,496 / padding 488 / **locals 6,400**. Remaining before ticking 2.4: the same counters on device (5.3); 5.4 stays blocked.
- **2.5a real-gather fixtures LANDED 2026-09-17; the view/bridge matrix is what remains of 2.5.**
  `buildFixtureRoot` in `test_continuous_capture` now builds a published snapshot root carrying both
  independent snapshot roots (note membership *and* the MIDI-event membership the playback gather
  reads, mirroring the publisher: an Off at/beyond the loop length clamps to `loopLength - 1`, a wrap
  note splits into head+tail with the same `NoteId`), and `assertPairMatchesSynchronousPathFromAnchor`
  anchors both consumers at the pair's own `cursorSeed` so the comparison is about the pair rather than
  about where each cursor started. The four owner-priority gathering fixtures exist: wrap + loop-edge
  note, source change mid-window (hold, 8-tick latency, added row audible), long-loop window (the
  documented `pairApplicable == false` boundary plus the windowed stream against an independently
  phase-sorted full-gather window), and the record-stop publication gap (an empty gather publishes no
  pair, then the session-exit root lands and the pair matches the synchronous path). Harness note:
  `consumePairOverRange` starts at `fromTick + 1`, so a phase-0 row is never emitted by either
  consumer; fixtures keep their plain note after tick 0. **Still owed for 2.5:** the view/bridge
  scenarios — record stop, phase-zero wrap admission, pre-seal invalidation, rejected-On/later-Off,
  accepted-On/matching-Off, wrap-held closure, append-and-stop, the explicit deferred-stop trace,
  promotion before wrap, max phase advance, denied refill, three-buffer rotation, source-change
  preemption near handoff, source prep midway through two coverage windows, second-source bridge
  replacement, coverage fault, layered slots, slot switch, first/second revision changes. Evidence:
  `test_continuous_capture` 71/71, `REENTRY_RED=1` 72/72, `ONLY_COMPANION=1` 1/1,
  `test_playback_prewarm` 173/173, full native **2,236 / 1 skipped / 0 failures**, RAM1 unchanged
  (variables 91,904 / code 425,496 / padding 488 / **locals 6,400**; test-only increment).

**2026-09-17 — OpenSpec 5.12 / D8 interval-owned staged pass LANDED (DEC-072), and the 5.5
`CompanionHistory` blocker is cleared.** A deferred job now owns its staged capture pass from its own
`PrepareTransfer` grain (`CaptureStopJob::pendingPass` + `ownsPendingPass`,
`Loop::takePendingCapturePass`), the companion history binds that receipt by address, and the job's
terminal transfer consumes it directly (`Loop::transferCapturePass` and the explicit-pass overloads).
`beginCapture` keeps its unconditional producer-local clear; no deferred reader falls back to
`Loop::pendingCapturePass_`. Native: `ONLY_COMPANION=1` green, `REENTRY_RED=1` 57/57, full native
2,221 / 1 skipped / 0 failures; RAM1 locals 6,400. Device:
[`captures/successor-ownership-06`](../../captures/successor-ownership-06/session_20260917_230432.log)
managed HITL `base`, `Result: PASS`, `overlap=True denied_stop=True retirement_ok=true` (A retires
inside B's live interval; B then stages and transfers its own pass). Host oracle corrected to accept
the current `Overdub stop refused: stop_job_pending` denial wording. **Next (reviewed 2026-09-17 — the
pick-up order is recorded at the top of that change's `tasks.md`):** the open items are now only 5.5,
5.9, 5.11, 6.1, 6.2 and 6.3 (4.3 closed), and the best next step is **5.5** — the only host-only item,
the one D8 directly unblocked, and the one still hiding coverage behind `REENTRY_RED=1`: add the
missing "after the raw-range stage" checkpoint (`CanonicalRewrite`; `ReservedClosure` is the
alternative), then delete the `REENTRY_RED` gate so the case runs in the default suite. Then plan
**5.9 + 5.11 + 6.2 as one device session** (5.9 still needs the `hitl-content-verification` audibility
proof for "all overdubbed bars stay audible"; 5.11 closes on that run; 6.2 still needs a successor
that *carries rows* plus a boot-prewarm headroom read), then re-run **6.1** and finish **6.3** (which
includes ticking the parent change's task 2.3, already annotated with the deferred three-deep capacity
case).

**Also reviewed 2026-09-17: the parent change `resumable-midi-boundary-playback-maintenance` re-checked, task 2.3
closed.** Its own recorded acceptance list (two public reentry cases + device evidence + the deferred three-deep
capacity case named) is satisfied, so 2.3 is ticked with that evidence. Seven of its items stay open and now carry
inline status: **2.4 is genuinely open** -- `playCommittedLoopMidi` still runs `ensurePlaybackMergedMidiEventsBuilt` +
`rebuildPlaybackOrder` + `reanchorPlaybackIndex` synchronously when `fixedView == nullptr` on the active path, and no
instrumented test fails on it -- and it blocks 5.4; 2.5 still needs the differential trace harness; 3.3 is one clause
short (the complete atomic Commit-body measurement); 3.4/4.1 have landed behaviour but unevidenced verify clauses; 5.3
is a device matrix; 5.4 cannot land before 2.4. The recommended order for that change is recorded at the top of its
`tasks.md`.

**2026-09-17 — transport-stop retarget re-route (DEC-070, implemented + device-verified; openspec
5.3/5.7/5.8).** The user's stuck state machine after the HITL runs was a deferred job admitted for
PLAYING, retargeted to Stopped by `stopOverdubbingToStopped` and left on the PLAYING phase path: its
companion work is bound to the pre-stop transport command epoch, so every grain denied silently, the
job parked in `CompanionHistory` (`Overdub stop refused: stop_job_pending phase=7 dest=1`) and it kept
`captureStopOwner`, the frozen staging role and its session-exit receipt — every later stop refused
until reboot. `requestDeferredOverdubStopToStopped` now re-routes that job on the same edge: retire the
companion/undo estimates a STOPPED-admitted job never contracts, drop the half-prepared companion
horizon, continue at `AtomicTransfer` and retire through deferred cleanup. Native
`test_transport_stop_retarget_retires_a_midflight_playing_job` fails without it; full native 2,213 /
1 skipped / 0 failures; RAM1 locals 6,400. Device `captures/successor-wrap-40`: `Transport stopped`
74.873 s with the successor's job pending, `Reentry released` 77.053 s, then two admitted enter/stop
cycles (97.1/99.2 s, 105.6/107.7 s) with no refusal. Diagnosis instrumentation (one-shot
`Deferred stop stalled: <reason>`, `Overdub stop refused: <resource>` + parked phase) stays in place.
**Follow-up regression (user, 2026-09-17):** after the re-route the first overdubbed notes vanish from the *display* while remaining audible, with only the wrapped notes visible. Cause: the re-route discarded the prepared companion horizon, which carries the note-edit representation the display resolves from. The re-route now keeps a verified horizon (published by `commitStopped`) and drops only an incomplete one, and logs `Overdub stop retarget: companions=<published|raw_only> phase=<n>` so the next reproduction names the sub-case. Native capture suite 50/50; RAM1 locals 6,400. **Reproduced on device (`captures/successor-wrap-41`, temporary `--post-save-play-wait-ms` play-from-stopped step): `overdub_ok=False`, `frame=178 … visual=124 buffer=178`, and `Overdub stop retarget: companions=raw_only phase=6` — the job was still at `PrepareTransfer`, so its companion horizon was never prepared and the raw-only commit dropped the transforms the display resolves the overdubbed notes from. **Implemented:** `retargetedFromPlaying` keeps the companion preparation on the retargeted STOPPED route (published via `commitStopped`), with a bounded 512-grain per-phase fallback that commits the raw pass alone if it truly cannot complete, so the slot never parks. Native 2,213 / 1 skipped / 0 failures; RAM1 locals 6,400. Device `captures/successor-wrap-42` flips the display verifier to `overdub_ok=True` (from `False` in `-41`), but reports `transport_ok=False` alongside a `capture_ring_overflow`/`SERIAL_INCOMPLETE`, so the post-stop window check needs a clean re-run (openspec 5.9).


**2026-09-17 — the wrap is the third owner of the slot's canonical preparation (DEC-069; hardening
landed, revision gate awaiting the owner's choice).** The user's manual three-overdub session
[`captures/session_20260917_172651.log`](../../captures/session_20260917_172651.log) fails on the
DEC-068 firmware: OD1 (570.855 s) wraps and retires normally, OD2 (586.564 s) stops at 590.687 s
and **never emits `Reentry released`**, OD3 (591.518 s) wraps at 593.456 s and then every NoteOff
for the notes held across that wrap is rejected (`Capture admission rejected: unmatched NoteOff
ch=4 note=72`, nine times), and OD3's own stop at 598.245 s is refused as
`deferred/frozen admission role is occupied` for the rest of the session.

Root cause, proven natively: `Track::commitOverdubWrap` commits the live interval's next capture
pass, and a pass commit advances `Loop::playbackRevision`. The pending job's
`RuntimeCanonicalPassInputs` frozen binding is pinned to the revision it was admitted against
(`valid()` compares the live `playbackRevision`), so the advance makes `ready()` false forever,
`CanonicalTail` denies, `completeDeferredCleanup` never runs, `runtime.captureStopOwner` stays
owned, and no later stop of the live interval can be admitted. The live interval's queued wrap
publication also stays behind the unresolved predecessor publication (FIFO), which is why the
third overdub never shows at the wrap and its notes sound lengthened into the previous overdub.
Instrumented native probe of the failing interaction: `revBefore=1 revAfter=2`, frozen binding
intact, `ready()` false, job in `CanonicalTail` for 20,000 grains.

Landed here (independent of the gate): the wrap no longer calls `resumeCanonicalAfterAppend`
(whose `resumeCurrentAfterAppend` resets the frozen capture binding) nor discards the frozen
order/rewrite builders while `hasPendingDeferredOverdubStop()` — the same DEC-067 ownership rule
the start and stop paths already follow. Two new native cases
(`test_successor_wrap_keeps_the_predecessors_frozen_builders`,
`test_successor_wrap_keeps_a_held_note_reservation_for_its_physical_note_off`) fail without the
guard; full native 2211 cases / 1 skipped / 0 failures; `pio run -e teensy41-capture-serial`
SUCCESS with RAM1 locals 6,464. **DEC-069 Option A is implemented** (owner's choice): `maybeCommitOverdubWrap` records the owed
commit in `Track::overdubWrapCommitPending_` and seals nothing while a job is pending; the per-tick
playback turn retries it once the ring is empty, and the interval's own start/stop consume the flag.
The straddling rows stay in the producer, so the held note's reservation is never reset and the
live interval's `unmatched NoteOff` rejections disappear with it. Three native cases cover the gate
(two fail without it); full native 2,212 / 1 skipped / 0 failures; RAM1 locals 6,464; no new
human-log literal (one added string in that translation unit costs 4,096 bytes of DTCM, so the
deferral is observed through marker ordering). **Device acceptance closed** (`captures/successor-wrap-36`, firmware at the Option A commit): the
successor's crossing of tick 6144 was deferred, `Overdub wrap committed @ tick 6460` follows it
(only the deferred retry can report a later tick than the crossing), the predecessor's
`Reentry released` follows at 60.534 s — the outcome the old path made impossible — the successor's
own stop is admitted at 62.736 s, and the run has no `unmatched NoteOff` and no
`Capture admission rejected` row. Regression `captures/successor-wrap-33` (DEC-068 flags) is a clean
harness PASS (`overlap=True`, `denied_stop=True`, both retirements). Two route notes for the
reproducer: the successor must own rows *before* its crossing, which the canonical preset cannot
supply once the predecessor's stream ends, so the acceptance used a temporary
`--successor-reentry-stream-bars` flag (default 0, canonical behavior unchanged, promotion to a
registered `STIM-…-02` pending); and the predecessor's stop phase must sit ~0.5 bar before loop end
(`A_stop_phase ~= (A_start_phase + delay_bars + overdub_bars) mod record_bars`, with A starting
~1 bar after the record stop). Earlier attempts are recorded as inconclusive, not negative:
`successor-wrap-30` used `--record-bars 1`, which never establishes a loop (0/768).


**2026-09-17 — reentry session ownership — LANDED (DEC-068, device PASS
`captures/successor-reentry-27`).** Each interval now owns its session-exit tail: A's is captured
at its stop admission (swap, not copy), released with its own pass id at its own commit, and the
publisher consumes only ready tails FIFO; a successor opens a fresh session instead of joining the
draining one, so a retiring job cannot end the successor's session or hand off its identities.
Device: A's release 51.620 s, B's admitted stop 52.098 s, **B's own retirement 52.992 s**
(`retirement_ok`), with `overlap_ok`/`denied_stop_ok` true and the run's own result **PASS** — the
first clean reentry run. Six native cases in `test_continuous_capture` cover the decision's gates;
full native 2209, 1 skipped, 0 failures. The defect this replaced, for the record:

**2026-09-17 — reentry session ownership: a retiring job consumed the live successor's
session (RESOLVED by DEC-068).** Tasks 5.1(d)/(e) landed (`test_successor_same_interval_transfers_exactly_once`,
`test_start_request_while_successor_is_live_is_not_a_second_interval`; net full-native 2205,
1 skipped) and the (d) case immediately exposed a job-locality defect in the same family as
the two DEC-067 findings. With A's deferred job pending and B live,
`TrackPreparedStopCommit::completeDeferredCleanup` (the phase every deferred stop ends in)
unconditionally calls `loop.handoffSessionExitSnapshot(...)` and `loop.closeOverdubSession()`:

1. the receipt is built from the **current shared session state**, which under reentry is the
   successor's, so the retiring interval's session-exit publication carries the wrong pass ids;
2. `closeOverdubSession()` closes **B's** session while B is still recording — the new case
   pins it (`loop.hasOverdubSession()` false for a live interval, asserted as the diagnostic);
3. B's own `DeferredCleanup` then cannot hand off (`!hasOverdubSession()`), so B's job reaches
   phase 11 (`DeferredCleanup`) and never retires: its descriptor leaks and its session-exit
   publication never queues. Reachable on device in exactly the DEC-067 sequence
   (`captures/successor-reentry-25`: A released at 52.195 s while B had been live since 49.986 s).

Root cause: `Loop::beginOverdubSession` early-returns when `hasOverdubSession()`, so a successor
interval **joins the predecessor's session** instead of owning one, while `sessionExitHandedOff_`
and the receipt ring are written as if a session belonged to one interval. Fix shape (needs a
DEC, since it changes when a session-exit receipt is created): capture the interval's receipt at
its own stop admission (before any successor state exists), consume only that receipt in the
cleanup, close the session only when the job still owns the live interval (`ownsTrackState`),
and let a successor open its own session/index while the predecessor's is still draining.

**2026-09-17 — successor-capture reentry: finalization admission is stop-directed (DEC-067).**
User decision after the device exposed the mismatch: *"I would fix the gates to match the
architecture, rather than narrow the architecture to preserve an accidental reservation
lifetime ... a resource should block the transition that first requires ownership of that
resource."* Landed and device-verified — see item 2 below for the audit table, the markers,
the native cases and the `successor-reentry-25` sequence. Next in this scope: the three-route
agreement case (4.2), the remaining 5.1 breadth (interval transfer exactly once, the
"start while B is still live" denial), 5.3–5.6, then 6.1/6.3 closeout once the shed-capture
observability gap is named. Explicitly not taken: option B (a canonical output per interval —
it would make two finalization working sets concurrent) and option A (narrowing the declared
capacity).

**2026-09-17 — first-record audible launch from the prepared bar-1 window (user-directed;
leads the successor-reentry device step).** User: *"we explicitly had a plan to build a
prepared first bar to be ready to be used when record stop is executed to directly be used as
playback. that should be leading and remove the decision for empty."* The authority is the
approved contract in
[`resumable-midi-boundary-playback-maintenance/design.md`](../../openspec/changes/resumable-midi-boundary-playback-maintenance/design.md)
§ *First-record window preparation preserves the existing launch boundary* and the 2026-09-15
user correction in its `tasks.md`: at record start the slot runtime establishes prewarmed bar-1
playback coverage, accepted input keeps it current, the retained raw bar is bound to final loop
geometry at the stop, and **the complete bar-1 source/order/cursor/ledger bundle is selected at
the existing transport-clock boundary with no global rewind, no omitted notes and no delayed
catch-up**. The measured defect this replaces: after a first-record stop playback is empty for
`~620 ms` (the session-exit publication gap, 6.3.1 hold-empty) and then flushes the missed grid
steps as a catch-up burst. Device evidence: `captures/deadline-pacing-01`
(`STOPPED_RECORDING→PLAYING` at 39.8156 s, first `snapshot_unit,kind=publish` at 40.4297 s,
`playback_build,snapshot,...,currentPhase=473` at 40.4304 s, first `MO` at 40.4355 s).

- **S1. Preparation must be re-armable across takes — LANDED (`b6dd67bf`).** A take that ends
  without the record-stop handoff (transport Stop during RECORDING, empty/skipped commit) left
  the prepared raw bar-1 view retained, so the *next* take's `prepareFirstRecordingBar` was
  denied and the prepared-first-bar path never existed. Device evidence: `First-record bar-1
  playback preparation unavailable` in `deadline-pacing-01`, `successor-reentry-13`,
  `bar-anchor-02` and `-06`, each immediately after a `RECS,stopToStopped,...` empty take. The
  new take's preparation boundary releases the stale window
  (`releaseFirstRecordingBarReturnWindow`) and the denial warning names the failing
  precondition.
- **S2. Select the bound bar-1 bundle as the audible source — LANDED, device-verified
  (`first-record-launch-04`).** `playCommittedLoopMidi` had two fixed sources and both require
  `publishedSnapshotIdentity()`, so the bound raw bar-1 view was never audible and the legacy
  merged path held empty (`GatherMissHoldEmpty`) until the root published. Now: the bound window
  is flagged (`RuntimePlaybackView::firstRecordReturnWindow`) so `LoopPlaybackRuntime::reset`
  preserves it across the launch's playback-cache resets; the hot path adopts it while
  `isPlaybackSessionExitPublicationGap()` holds and the playhead is inside its coverage
  (`adoptFirstRecordingBarReturnWindow`, DIAG `first_record_adopt,<loopLength>,<retainedEvents>`);
  the published root replaces it through the source identity. **The bundle includes its cursor**:
  adoption selects the first event after the launch phase, because the emission otherwise treated
  the launch turn (`prevTickInLoop == tickInLoop`) as a loop-start catch-up and dumped the
  already-played bar-1 prefix in bursts (seen in `first-record-launch-02`: notes 48–52 within
  400 µs). Device verdict, canonical 2-bar `base` PASS: stop 31.0072 s → PLAYING 31.0186 s →
  `BAR,346` → `first_record_adopt,1536` 31.0216 s → first `MO` 31.0994 s (**93 ms** after PLAYING),
  then note-ons at **125.0/125.0/125.0/122.4 ms** = the 16th grid, no burst and no omission
  (pre-fix `deadline-pacing-01`: first note 620 ms after the stop, then a catch-up burst).
- **S2b. The window's retained extent must cover the launch phase — OPEN.** The service refresh is
  incremental (`serviceFirstRecordingBar`, one fill/order grain per VisualCache owner turn), so at
  the stop the window can hold only a *prefix* of bar 1 (`first-record-launch-02` held 5 of 16
  notes). The cursor reanchor makes that prefix skip instead of burst, but notes between the
  prefix end and the root's arrival are then **omitted** rather than delayed — the same audibility
  as the old hold-empty for that take, not a regression, and not yet the design's "no omitted
  notes". Close it by keeping the prepared window current through the last accepted input (owner
  turn budget) or by a bounded drain at the stop, and carry the bar-1 closure obligations
  (`LedgerBoundarySummary`) so a note whose Off lies in a later bar is closed by the window itself.
- **S3. Coverage continuation — PLANNED.** Cover each following bar without omission or delayed
  refill (bar-1→bar-2 handoff for multi-bar takes) and verify an 8-bar take on device. The 2-bar
  canonical take is already covered: the root publishes ~0.4-0.6 s into bar 1 (2 s), well before
  the window's coverage ends.

Explicitly not in scope for these slices: C1-a compat-gather restoration (6.3.1 stays retired),
DEC-045 idle publication scheduling, and any new fixed user-visible capacity.

**2026-09-17 — HITL oracle repair RC (user-directed), then the bridge tail.** C3/DEC-066 is
closed as validated, so the next priority is trustworthy device evidence rather than more
memory work. In this order:

- **1b. Record-stream pacing — OPEN (found while probing).** The host streams longer than
  requested (23.0 s for an 8-bar/16 s request at the device's own 2.0 s/bar cadence), so the
  device legitimately records 11-12 bars. When the device's `BAR`/`BPM` markers are available,
  the stream duration and stop moment must follow them rather than a host seconds-per-bar
  estimate. This is the remaining half of item 2 and it keeps flagging strict length checks.
- **1. Oracle repair — LANDED.** Deferred stop (`OVERDUBBING -> OVERDUB_STOPPING -> PLAYING`)
  is now first-class in the transition counts and the phase pairing; duplicate observer ST
  edges are collapsed; a wall-clock-proxied stop reports its length instead of failing it.
  Device `oracle-fixed-01` = **PASS**. See the oracle entry below.
- **2. Successor-reentry device case — LANDED, DEVICE-VERIFIED (DEC-067; `successor-reentry-25`).**
  Path: the probe could not hit the predecessor's deferred window because the deferred stop
  floods the serial stream the harness parses (`-18`: +400 ms requested, +1535 ms measured),
  which exposed a firmware contract mismatch: `Track::startOverdubbing` gated on three
  *finalization* resources (canonical copy target, active transfer admission, session-exit
  receipt) that the interval does not use before its own stop edge, so the declared depth-2
  capacity was unreachable (`-19`/`-20`: `Overdub start rejected: canonical copy chunks are
  not reserved` at +500/+909/+1116/+1515 ms against `bridge_ready` +1458/+1681 ms).
  User decision: **C** — fix the gates to match the architecture (DEC-067). Landed:
  START keeps only the live staging role, the stop-descriptor ring and the interval's own
  memory admission; `beginDeferredOverdubStop` owns the finalization checks (each with its own
  denial reason); the start path no longer discards a live job's frozen order/rewrite builders;
  a stop denied for those reasons is now refused atomically instead of falling into the legacy
  synchronous stop. Two edges are emitted for the harness (`Reentry window open slot=<n>` and
  `Reentry released` on the prompt human channel, plus `DIAG,reentry_window_open` and
  `DIAG,canonical_release` on the evidence tier), and the probe/oracle drive and verify the
  sequence from them.
  Device `captures/successor-reentry-25` (managed HITL, `base`, 8-bar record / 2-bar overdub,
  build identity in its `metadata.json`), re-verified on the current firmware as
  `captures/successor-reentry-26` (48.857 s `Reentry window open`, 49.399 s B START 2,011 ms
  before A's release, 49.942 s B's stop refused, 51.410 s `Reentry released`, 52.109 s B's stop
  admitted; oracle PASS): 44.253 s A `Overdubbing started`; 49.403 s
  `Reentry window open`; 49.986 s **B START, 583 ms after A's stop and 2,209 ms before A's
  finalization released**; 50.500 s **B's stop refused** (`Overdub stop deferred: deferred/frozen
  admission role is occupied`) with B still recording; 52.195 s `Reentry released`; 52.778 s
  **B's stop admitted**. Oracle: `overlap_ok=True`, `denied_stop_ok=True`,
  `evidence_source=human_markers`, no successor issue in the report.
  Caveat: because the 8-bar deferred run sheds `#CAP` tiers (`RING,shed`), the *baseline*
  verifier's unrelated record/persistence checks report `INCONCLUSIVE_CAPTURE` in the same run;
  the reentry gate itself is proven from the prompt channel. Closing 6.2 fully needs that
  observability gap addressed (a separate item: the shed loses the `ST`/`REVT` rows the
  baseline checks need).
- **2b. Device-state hazard (new, needs a decision).** An aborted run can wedge the deferred
  workspace save (`Persistence drain stuck after 256 iterations`, 158 KB pending); the clear
  ritual then aborts and **every** later run fails its preconditions until the device is
  re-flashed (the device does not reset on serial open, and a 1200-baud touch did not reset
  it). Recovery is `pio run -t upload`. Decide whether the harness should force/verify a fresh
  boot, and whether the firmware's blocking drain deserves its own look.
- **3. Bridge map/compile profiling — DONE (profile below).** The 1.07 s tail is
  **cadence-bound, not work-bound**: 466 of 490 deferred-stop grains are the
  `CompanionBridge` phase, they cost only 65.4 ms of the 1,068 ms window, and they issue at a
  1.2-3.2 ms cadence (~one grain per main-loop turn). `CompanionHistory` is down to 4 grains,
  so C3's work shaping is visible in the profile. Any optimization would target the owner's
  turn cadence/per-turn grain budget and must be argued against the MIDI-boundary budget -
  that is the DEC question, and no DEC is proposed until the successor-reentry device case
  lands.
- **Explicitly not pursuing:** C4 (device-wide deferred-stop working set), event-horizon
  ceiling reduction, seal-phase reserve.

**2026-09-17 — memory-exhaustion admission RC: close record-arm semantics, then footprint
(user-directed, seven steps).** Both boundaries now deny explicitly at the point of the
failure (overdub: `admitOperationMemory`; record arm: the required prewarm's own result),
so no operation reaches a fatal allocator. The remaining work is to make the *reservation*
itself smaller and better attributed, in this order:

- **A. Record-arm admission closure — LANDED.** `prewarmPlaybackForSlot` returns its
  result and `Track::startRecording` denies before any mutation. Do not regress to
  "advisory prewarm".
- **RC CLOSED (2026-09-17, user-accepted):** the exhaustion RC's acceptance criterion -
  *before: fatal/silent device; after: explicit operation denial with playback continuing* -
  is met by [`stepD-arm-delta-01`](../../captures/stepD-arm-delta-01/session_20260917_115207.log).
  A successful record+overdub in the constrained state was explicitly **not** a prerequisite;
  it became E's device gate instead, and E now passes it
  ([`stepE-e2e-01`](../../captures/stepE-e2e-01/session_20260917_121721.log): 0 denial, 0
  refusal, 0 FATAL, record + overdub + deferred stop to PLAYING).
- **B. View/history lifetime audit — DONE.** All buffers byte-accounted against the measured
  tags; the seven prewarms are one concurrently-live working set, not phase-exclusive, and
  live canonical-prefix service needs them during RECORDING. Table and citations:
  [ExecutionBounds](../Authority/Architecture/ExecutionBounds.md#record-arm-reservation-buffers-bytes-and-lifetimes-2026-09-17).
  Corrections it forced (device `sizeof(MidiEvent)=20`, 1.24 MB arm, boot-aggregate tags,
  the 0xE00 stride argument) are applied in place.
- **C. Bounded shared/reusable stop working storage — DESIGN DONE, DECISION OPEN.** Sharing
  the seven workers is disproved (C1); the levers are capacity shaping from the admitted
  interval horizon (C3, halves ~1.24 MB -> ~0.62 MB if the interval horizon halves) and one
  device-wide deferred-stop working set (C4, ~228 KB per armed slot, restricts concurrent
  deferred stops). Implementation needs a DEC either way. Full text: the step-C entry below.
- **D. Loaded-set / 8-bar rerun — DONE.** `captures/stepD-arm-delta-01`: zero FATAL, zero
  `Recording started`, device alive, playback continuing, one explicit
  `op=companion_history_reserve` + one `op=record_start` denial, arm delta measured at
  1,240 KB, headroom 64-76 KB heap at the refusal. Boot probe `#MEM_ADMIT` in the same run.
- **E. The 1334 KB steady staging — IMPLEMENTED (DEC-065).** Boot no longer reserves
  staging; the record arm and `Track::startOverdubbing` do, reserve-then-require, before any
  mutation. Boot PSRAM headroom +1,189 KB; arm delta 1,469 KB; the loaded-set 8-bar recipe
  now runs end to end with zero denials, refusals and faults. Also fixed the point-of-use
  gate that was denying the 80 KB `InternalHeapFirstAllocator` reserve while ~1.27 MB of pool
  was free. **C3 is now the user-directed next lever**: demand-shaped capacity per owner
  under the existing 2,048 maximum (never a smaller fixed cap), whose certain benefit is the
  arm-time working set. Its latency side is the `Phase::Trim` walk in
  `Loop::stepPreparedCompanionHistory` (one unused row per quantum unit), not the ~65
  `prepare,slice,prep` lines - those are `LoopContentResolution`'s content-driven canonical
  rebuild. C4 is explicitly **not** being taken.
- **F. Transport-start 81920-byte owner — DONE (identification only, no gate).** It is the
  deferred record arm released by `handlePendingRecordStart`; step A already gates it.

Wait: step 5 of the goal phrasing used "record-arm exhaustion is not yet fully an
operation-level denial" - that is now closed (A). Overdub exhaustion was already
operation-level.

**2026-09-16 — task 2.3 continuous queued capture: ingress/staging ownership isolation (user-directed).**
Continue the user-approved `CONTINUOUS-CAPTURE-IMPLEMENTATION-PLAN.md` sequence for
OpenSpec `resumable-midi-boundary-playback-maintenance` task 2.3. The next named missing
owner was the capture admission staging shared between live ingress and the frozen stop job.

Landed this session: `RuntimePlaybackViewSet` now holds two independently prewarmed
capture-admission roles (`captureStagings[2]`, `liveStagingIndex`/`frozenStagingIndex`).
Live ingress writes only the live role; the one admitted stop job borrows the frozen role.
Punch-out freezes the live role and exchanges the two indexes in constant work
(`LoopPlaybackRuntime::exchangeCaptureStagingRoles`); job cleanup resets only its own retired
frozen role. No new owner/module, no canonical/persistence/SD change, no action-edge copy.

Next in this scope, in order: (1) **FIFO/queue-head binding landed** — the ring is
now the admission authority (`captureStopJobTail()` admits, `queue_full` is an
explicit denial, the worker binds `captureStopJobHead()` once per grain,
`!STOP_STATUS` reports `queue=depth/capacity`); (2) **sealed raw-range ownership
landed** — the approved transfer is wired: `CaptureStopJob::rawRange` owns the
stopped extent, `beginDeferredOverdubStop` proves `canSealCaptureRange` before any
mutation and seals at punch-out, `Loop` keeps a job-identified read borrow whose
read source exposes producer and finalizing extents separately, ordering is
scratch-only, closure NoteId resolves from the finalizing extent, and
`retireJobCaptureSourceGrain` retires the owned extent in grains and re-arms the
descriptor. DisplayManager is unchanged and the successor-capture guard is
deliberately still in place. Then (3) **job-owned session/preview/undo/snapshot
receipts — pending note changes, `capturePreview` and the undo baseline landed**
(the worker resolves against `job.pendingChanges`, seals `job.preview`, and records
the undo entry against `job.beforePublished`; the live session keeps independent
state and the Loop's readable accessors serve the job-identified borrows);
**the session-exit snapshot handoff receipt landed as a Loop-owned FIFO ring of 4**
(`prewarmSessionExitSnapshotHandoff`, free-slot gate, one receipt per open session
enforced inside the handoff, publisher consumes the oldest per turn, playback's
stale-root overlay folds every pending receipt) **and the prepared companion history
is interval-qualified** (`PendingStopAdmission::intervalToken()` on every in-flight
consumer, owner-agnostic Cold/Prewarmed), which closes the session/preview/undo/
snapshot receipt slice. Then (4)
causal predecessor and source identity per job; (5) only then remove the Button A,
manager and Track reentry guards and turn the three public reentry cases green —
explicitly not in the same change as the ownership layer. Device PSRAM admission
was measured for the staging change and **passes** (160,536 B/slot,
3.86 MB of 8.38 MB free); the sealed-range build adds four destination directories
per track (+4 pool chunks/slot, −32 KB internal heap) and its boot-prewarm admission
was measured and **passes** (`captures/task23-sealed-range-prewarm/result.json`);
the ring adds no new boot allocation, so the increment A/B device memory check
(build `91e6b38c…`) still covers it.
Task 2.3 and the task-5.3 HITL gates stay open.

**2026-09-16 — canonical base SEVT observation (host/gate defect; firmware RCA closed).**
Firmware RCA **CLOSED / fix verified** (`842166a7`); SEVT lifecycle fix **SHIPPED**. Transport
pre-dispatch arming proposal **REJECTED** as unnecessary for this RCA; reset-before-guard
retained as independent hardening. The remaining defect is host-side: the canonical `base`
managed run cannot observe a legitimate firmware SEVT result.

Goal: allow the managed capture to survive the overdub-stop operation through deferred
completion and the SEVT drain.

Acceptance criteria (pinned before editing the host):

- `base` reaches a genuine overdub commit on a known non-empty loop.
- The overdub-stop press does **not** terminate observation.
- Capture stays alive through `queue_accepted` -> `drain_emit_start` -> `drain_complete`.
- SEVT inventory is evaluated only after that completion or a bounded timeout.
- Missing SEVT becomes a **required** failure again once this works deterministically.
- `scripts/hitl_stop_interruption.py` stays an *additional* interruption-path test, never the
  canonical SEVT authority.
- No firmware behavior changes bundled.

Two host failures, kept separate until diagnosed:

1. **Seeded-slot abort at the overdub-stop press** — the primary blocker, because it prevents
   observing a legitimate firmware result (`captures/sevt-fix-rerun10`).
2. **Empty-slot `skipped_empty`** — likely a fixture/precondition defect: if SEVT verification
   requires committed overdub content, canonical setup must establish that precondition rather
   than accidentally entering record on the Loop-row select and then clearing it
   (`captures/sevt-fix-verify4`).

**Diagnosis (recorded before editing the shared fixture; evidence `captures/sevt-host-probe`).**
The seeded-slot path cannot produce canonical SEVT with a host-only change, because the one-shot
arm is consumed before any overdub content exists:

- `Playback stopped` (the pre-clear transport stop) -> `armed,1` -> `queue_accepted` -> the dump
  is queued against the *pre-existing* seed content.
- `Track cleared` empties the loop -> the idle drain logs `drain_reset_loop` and discards the
  pending dump.
- The overdub stop's deferred completion then logs `queue_skip_unarmed` — the new owner fires,
  but the arm is already spent. This is the designed one-shot contract, not a defect.
- Separately, the run tears the capture down at the press because the canonical
  `PLAYING_AFTER_OVERDUB_STOP` request has no terminal-state confirmation for that landing.

The **empty-slot path is therefore the primary one to fix**, and it matches the fixture-defect
reading: `captures/sevt-fix-verify4` (fresh boot, cleaned slot) has **zero** `stored_verify`
markers, i.e. the arm is still intact — nothing consumed it. Its observed failure is the
precondition, not SEVT: the Loop-row select starts a recording, the pre-clear transport stop
ends it, and the clear long press then fails to confirm `EMPTY`
(`[error] Preconditions failed on track 5: clear step did not confirm EMPTY state`), so the run
aborts before any genuine record; the record seal reported `skipped_empty`.

Consequence for the plan: fix the empty-slot precondition (establish "empty, stopped, ready to
record" without a clear long press) and the observation window together. Do **not** try to make
the seeded-slot path emit canonical SEVT — that needs the arm/drain semantics the user has
closed. Record the seeded-slot limitation rather than working around it.

**Precondition + observation fix landed as `95680fbe` (host-only, no firmware behavior
change).** `_selected_slot_provably_empty` accepts the newest of the ST/DISP evidence (a fresh
`EMPTY` edge beats a lagging DISP row) and `_await_selected_slot_empty` polls it instead of
reading DISP once; the clear request's `already_satisfied` now also accepts a provably-empty
*selected* slot. A deferred overdub stop whose state stays unobservable past the state-sync
timeout is now `EVIDENCE_UNAVAILABLE` and no longer aborts the run (only a genuine disproof
does), and the canonical final transport stop is followed by a bounded
(`STORED_VERIFICATION_DRAIN_TIMEOUT_S = 20 s`) wait for the drain's terminal marker before the
take is scored. Regressions: `test_provably_empty_accepts_state_edge_before_slot_disp_row`,
`test_stored_verification_drain_wait_requires_terminal_marker`.

**Device status:** `captures/sevt-host-verify2` is one full canonical pass — clear
`ALREADY_SATISFIED requested=EMPTY state=EMPTY`, both overdub stops logged "continuing so the
stored-MIDI verification drain stays observable" instead of aborting, the final transport stop
ran, and the capture covers `queue_accepted` -> `drain_emit_start` -> `drain_complete` with
**SEVT=112** and the stored-content contract satisfied (`count_ok/span_ok/bars_ok`,
`count=16/15`, `bars=3/2`). Repeatability is **not** yet established:
`captures/sevt-host-verify3` aborted at the clear precondition because the select-started ghost
recording was still live when the clear ran (DISP `RECORDING` length still climbing), and the
earlier deferred-save stall also recurs. So temporary C stays, `sevt_gate_required` stays
`False`, and the next step is making the empty-slot ghost-capture stop deterministic (the
bounded stop/await loop) before repeating the pass.

**Remaining blocker after that (host/scenario, not firmware): the drain needs idle.** `processDeferredStoredMidiVerification` is gated on
`!isAudiblePlaybackActive() && !isRecording() && !isStoppedRecording()`, and `--preset base`
finishes on an overdub stop that returns to PLAYING. The dump therefore stays pending; the run
then tears down at the overdub-stop confirmation timeout. Measured on the working
stop-interruption run (`sevt-fix-verify1`): `queue_accepted` 1.4 ms after the stop, but
`drain_emit_start` -> `drain_complete` spans **~10.2 s** across five phases, which exceeds the
8 s `state_sync_timeout_ms`. So even a STOPPED landing loses the dump unless the host keeps
observing.

Resolved: the user approved finishing the canonical take on a transport stop. `base` already
sets `--stop-after-overdub` by default, so no scenario flag changed — the run simply never
reached that stop because the overdub-stop confirmation aborted it. Making that outcome
non-fatal (above) is what lets the existing final stop run, which is why no new ending was
added to `canonical_baseline_legacy_args()`.

Guard: success is **not** `drain_complete` alone. The host must retain the stored-content
contract (expected stored span/inventory), so the gate cannot silently degrade into "the
diagnostic machinery ran".

Closure (one commit, after repeated device passes): restore `sevt_gate_required = True`, remove
temporary C / advisory wording, and decide the fate of `bf8fb6a4`'s markers — keep them as
permanent `SESSION_CAPTURE` diagnostics if they stay cheap; they are gated out of production and
have proven useful for exactly this class of HITL diagnosis.

**2026-09-13 — `resumable-midi-boundary-playback-maintenance` (user-selected).**
DEC-060 records user approval of canonical per-pass companion application for
combined capture preparation. Preserve immutable pass membership and apply rows
independently before the canonical merge; flattened admission is not the oracle.
The bounded per-pass owner now applies and merges immutable input descriptors
with pass/event origins and generation/capacity guards. Per-pass mapping now
validates an explicit published-base provenance table and feeds the ranked bridge
compiler/cursor. Full-loop view fill now constructs and owns the table alongside
events from a completed effective-pass baseline. A generation-checked sealed-pass
reader now copies raw chunks in bounded event/chunk visits without allocation
or partial-result exposure (2026-09-14; event-store tests 34/34). Production canonical pass
lifetime integration remains open. Bounded history preparation
now covers Create/Delete/Length/Pitch/Velocity/NoteRange, including contiguous row
collapse and Create identity reactivation. DEC-061 records the approved stable
Create sort correction. A runtime-owned continuation now connects frozen
sealed-pass descriptors through history application to separate effective-pass
inputs, with a generation-bound companion consumer. A direct LoopPasses overload
now selects canonical passes and filters active history in bounded grains;
a frozen-source entry point now checks live revision/generation/epoch, loop
length and capture state even on completed-result access. The typed companion
consumer now also binds the prepared row owner, rejecting same-generation row
reuse and mismatched capture owners. A frozen-overdub entry now orders capture
with private indexes and normalizes it as the final pass without changing the
shared audible arrays. Companion continuation now preserves historical Delete/
Create membership across the stage boundary through a bounded prefix scan.
End-to-end native bridge traces now cover 24 canonical history/capture variants,
a four-cycle open-note capture trace, and epoch cancellation/rebuild in mapping
and compilation. Ranked mapping/compilation now also feeds runtime admission
with live owner/generation checks retained through final selection preflight.
Executable uint64 bounds now compose ranked preparation, mapping and compilation;
the conservative full-horizon result exceeds the existing stop budget, so
completed sealed prefixes can now be reused without selection/read/history replay.
A runtime-owned prefix continuation now binds live revision/generation/length,
latches denial until the source changes, separates cleanup from restart, and
yields ownership to a frozen tail. The VisualCache owner now schedules at most
one prefix child across all tracks, with Q=32 and a global early return. Stop,
recording and existing active visual work retain priority; eligible overdub
tracks rotate without allocating missing runtime storage. A runtime coordinator now extends the owned prefix
through frozen-tail normalization, row preparation and canonical per-pass
companions in separate grains. It binds all child generations and retains
live source/epoch checks through ranked bridge preflight. Production stop routing,
atomic admission, admitted-tail/service proof and HITL remain open. Ranked-tail
bounds now accept an explicit maximum pass count and published-base event count;
C=2048/P=2/B=2048 with no history/companions still needs 7648 additional grains
at Q=32. Invalid quantum, out-of-staging event counts and overflowing legacy
contract totals now deny before stop admission. The order builder now checks
retained adjacency during fill and bypasses merge/copy only when the exact
comparator proves the input already ordered (64 grains for 2048 rows at Q=32).
Unordered inputs retain the merge path; worst-case bounds and production stop
limits are unchanged. Frozen-tail normalization now imports completed canonical
capture indexes into its private array in Q-bounded steps. Shared-order reuse
invalidates an in-flight import; after copying, audible ordering is independent.
Canonical and audible order bindings now also record that storage generation,
so discarding a later audible order cannot revive an older canonical completion.
Stale bindings deny resume/rewrite/readiness; tail admission uses private sorting
until a current canonical order is explicitly rebuilt.
When canonical order is already complete/current, capture normalization has a
196-grain bound at C=2048,H=0,Q=32 instead of 1604. The initial canonical sort is
still charged. The executable prepared-order aggregate now gives 6240 additional
grains for C=2048/P=2/B=2048,H=R=0,Q=32. The general 7648-grain aggregate and
legacy order/rewrite credits are unchanged. DEC-062 now approves a separate
provisional 6240-grain ranked-preparation allowance for continued integration.
Determine realtime reservations and elapsed stop latency with HITL once the
complete path can run; the old 3072 is not a product latency ceiling.
`PendingStopAdmission` now accepts an optional uint64 ranked-work bound at begin,
validates the 6240 allowance and combined counter capacity before mutation,
and tracks ranked credits separately. Finalization requires all three counters
empty; transport Stop preserves credits and changes epoch/destination. Existing
production callers still request zero ranked credits pending typed tail/bridge
routing; exhausting credits does not replace prepared-source readiness checks.
Reserved tail/bridge step entry points now bind the pending stop's frozen
generation, epoch and admitted quantum, charge one credit before one child,
and deny exhausted/stale reservations without advancing it. Completed readiness
queries consume no credit. Native end-to-end coverage uses these entries;
production stage dispatch, setup charging and atomic publication remain next.
A STOPPED-only typed oracle publisher now owns canonical baseline events and
pass origins together, upgrading flat same-identity views before bridge mapping.
It preserves the existing view on invalid baseline/coverage preflight and needs
no allocation. Track's all-stopped merged-event preparation now invokes canonical
baseline construction using idle prewarmed workers. It publishes typed origins
only when the complete canonical event sequence matches the existing oracle;
otherwise it retains flat publication. Temporary workers are released on success
or denial, without cancelling an already-owned prefix. This is synchronous
STOPPED throughput, never an active grain. Production ranked-stop stage dispatch
and atomic canonical/audible handoff remain open. A joint audible preflight now
requires AtomicTransfer phase, exhausted stop counters, matching command epoch,
PLAYING destination, explicit base/target identities and ready bridge/ledger/
cursor state. It is read-only and does not establish canonical companion-row
publication. Final bridge selection also rejects phase >= loop length instead
of accepting it through modulo coverage. Canonical companion staging is still
required before enabling the combined production commit. Loop-owned companion
history staging now prewarms a separate EditPass vector while STOPPED and copies
history metadata, Create payload events and prepared companion rows in Q-bounded
units. Prospective IDs do not advance nextPassId; canonical rows/revisions stay
unchanged. Source/storage/row-owner/ID changes hide the candidate. Logical discard
retains storage. The history handoff now swaps prebuilt canonical rows and the
snapshot companion-ID list, advances IDs/revision, and retains an immutable ID
receipt for the existing undo owner. Snapshot clear/replacement and append guards
reject stale candidates. Old history stays allocated until STOPPED prewarm.
A joint canonical helper now preflights the reserved capture pass and history,
transfers capture, then publishes history without a stale revision recheck.
It retires capture using scalar writes and preserves the one-revision-per-seal
convention. Track's prepared-stop entry now preflights exact canonical/companion owners,
source identities, stop epoch/credits/destination, cursor and ledger, then commits
canonical data and publishes the bridge without a second fallible selection.
Native fixtures verify matching identities and retained selected-bridge storage.
Loop's prepared-history continuation now verifies every staged sealed capture
event against the raw frozen canonical order in bounded event/chunk visits.
The joint handoff requires this proof; same-count payload/order mismatches and
missing/extra events deny publication. The rewrite cursor also binds its original
store. Stop admission now reserves canonical preparation separately from the 6240
ranked grains, rejects combined counter overflow before mutation, and requires
both counters exhausted for commit. An admitted step checks Q, frozen epoch and
owners before charging exactly one canonical grain; completed queries are free.
STOPPED prewarm now reserves both swap sides. After receipt consumption, a
bounded recycler retains allocations and restores preparation within the remaining
row/snapshot horizon. Two consecutive commits reuse the same buffers; exhausted
capacity or intervening canonical/mailbox changes deny reuse. Stop admission now
reserves post-commit history recycling separately, checks the combined credit
limit, and permits consumption only in DeferredCleanup after preparation finishes.
The joint commit requires and preserves that reservation; transport Stop also
preserves it. The native joint fixture charges setup and recycling while retaining
canonical revision and selected-bridge data. Track's recycling entries now bind
that committed Loop and preparation generation, charge receipt-consumed setup
separately, and enforce the admitted Q for each later grain. Wrong owner or
replacement preparation cannot consume the old reservation. Completed queries
remain free only while canonical/mailbox identity stays current. Loop now offers
bounded session undo-ID collection into caller-reserved private lists, preserving
the legacy cursor prefix and duplicate/order semantics. Session mutations invalidate
paused and completed collection. The maximum 2048-row/2048-companion collection
fits 193 grains at Q=32, separate from ranked preparation and recycling. The
collector now also appends the final capture and prepared companion receipt in
bounded stages, preserving the legacy final-pass inclusion and duplicate rules.
Receipt generation/lifetime and canonical identity guard paused and completed
collection. TrackUndo now prepares a private UndoEntry by setting metadata,
retaining the supplied before-snapshot and swapping the bounded ID outputs;
single-pass entries retain the legacy scalar representation. Preparation consumes
the collection without releasing vector buffers or existing snapshot roots.
Global publication and redo/trim retirement still need bounded integration.
DEC-063's accepted lifetime helpers now drain immutable snapshot graphs, persisted
snapshot/chunk payloads and complete UndoEntry resources in bounded units. A
private-stack trim continuation releases each entry before rechecking physical
pressure and shifts surviving entries one per unit. Shared snapshot readers remain
unchanged in per-grain native checks. Focused suites pass 37/37 and 43/43; full native passes 2069 with 1 skipped.
Firmware passes with RAM1 code 425736; strict OpenSpec and whitespace checks pass. These helpers require exclusive unpublished ownership
beyond a no-fail point; production locking, attachments, commands, reservations and
joint dispatch remain open. See the current BRIDGE-PROGRESS lifetime section.
A native pressure-feedback reproducer now proves that retaining removed roots
can change the surviving undo prefix (10 versus 8 entries in the controlled case).
The [undo publication reassessment](../../openspec/changes/resumable-midi-boundary-playback-maintenance/UNDO-PUBLICATION-REASSESSMENT.md)
records the accepted DEC-063 protected transaction with bounded reclamation.
The latest deferred Undo/Redo request wins per affected Track and executes once
after finalization under the destination policy; Transport Stop stays immediate.
Feedback trimming remains unchanged. Implementation and native proof precede
runnable HITL. Focused pool-budget validation passes **18/18**; this review changed no
firmware behavior. Production dispatch,
receipt/undo sequencing and active snapshot-box refill remain open. This canonical staging work is separate from the
6240 ranked-preparation allowance. A required live storage-generation guard now rejects timeline
reset/revision reuse, disabled-history reclamation and deferred edit appends
that can relocate rows without a playback revision change. The marker advances
before reset releases chunks, reclamation moves rows, or append reallocates
history, and remains bound across frozen-tail handoff. Native lifetime and
scheduler policy coverage do not establish an on-device latency bound.
Empty-history normalization and empty-companion merging now bypass
the apply worker and its extra copies, with corresponding tighter bounds.
Per-pass bridge mapping now uses a bounded sorted origin index and binary
search instead of quadratic scans; its maximum Q=32 bound is 2564 grains.
Production atomic transfer/routing, remaining combined-history/companion parity, duplicate
mergeSequence parity, production lifetime enforcement, capture freezing,
and admission remain open. See the
[Create reassessment](../../openspec/changes/resumable-midi-boundary-playback-maintenance/CREATE-HISTORY-REASSESSMENT.md).
See
[scope reassessment](../../openspec/changes/resumable-midi-boundary-playback-maintenance/COMPANION-SCOPE-REASSESSMENT.md).
Task 3.5 is complete: existing MIDI polls and save exclusions cover deferred
OVERDUB_STOPPING. Task 2.3 now has bounded bridge compilation and a production
selected-bridge consumer with native parity, retained cross-wrap closure, and
safe changed-pair instance tests. Frozen ordering now resumes across owner
turns with generation checks and charged filtered rows; frozen capture/companion
bridge filling and compilation now have runtime-owned continuation and an
epoch-bound source admission with read-only publication preflight. Canonical
companion payloads now prepare in runtime-owned, prewarmed storage and share
legacy Hide/Shorten encoding. DEC-059 explicitly corrects the pairing reference
to stable Off-before-On runs within the filtered channel/pitch lane, preserving
LIFO and wrap fallback. The bounded NoteOff lookup passes revised-reference
differential tests. Bounded wrap-head lookup and complete Delete index selection
now match the actual resolver, including retained rows and inferred loop length.
Length overlap actions now walk in bounded quanta, keeping all shortenings
before deletions and holding each action until consumed. Reconstruction wrap
queries now run in two bounded passes and match the distinct reconstruction
reference rules. Complete stack/span/projection/dedupe reconstruction now
produces a prewarmed, source-bound pitch snapshot with no prefix exposure.
Bounded Delete/Length application now executes those actions on a private
candidate, including final ordering and sequential frozen batches with base
provenance. Bounded companion bridge mapping now produces records plus final
canonical ranks, with apply-generation invalidation. Ranked bridge compilation
now owns those ranks through insertion/base ordering and cursor merge, preserving
unchanged-row ties and cross-wrap original closures. Combined capture insertion
and production routing remain open. Aggregate
stop-grain/latency proof, companion bridge publication, and production
capture-side bridge creation,
canonical/audible handoff, and stop recovery remain open.
See [bridge progress](../../openspec/changes/resumable-midi-boundary-playback-maintenance/BRIDGE-PROGRESS.md)
for validation and the remaining integration gates. No firmware upload or
device timing closure is claimed.

**Parallel — [`record_stop_snapshot_publication_gap_display_bugfix.md`](../Plans/record_stop_snapshot_publication_gap_display_bugfix.md) (2026-09-12).**
**Stage 6C-A + 6C-B SHIPPED** — capacity extension + `kExtraBars = 2`. Native 18/25-bar fixtures PASS; device freeze-scaling [`144434`](../../captures/session_20260912_144434.log). **6B DEFERRED**. **GAP-8 SHIPPED** (§6.6.4) — device [`191442`](../../captures/session_20260912_191442.log). **GAP-8b REVERTED** (§6.6.5) — not on branch after `3c9dcd4a`; deferred with GAP-8c design. **GAP-8c REVERTED** (§6.6.6) — [`204310`](../../captures/session_20260912_204310.log). **NOW:** **Stage 7** (§6.6.7) — `missed_preview` / `compose_miss` post-publish; anchor [`130411`](../../captures/session_20260912_130411.log); plan [`record_stop_stage_7_preview_compose_parity_bugfix.md`](../Plans/record_stop_stage_7_preview_compose_parity_bugfix.md).

**RC-P1 SHIPPED (2026-09-12).** Resumable `LoopSnapshotTail` + A4.2 `writeLoopSnapshotPayloadByteSlice` quota fix. Device A4 PASS [`164947`](../../captures/session_20260912_164947.log): `loop_snapshot_tail,done` → `payload_done` → `SAVE,completed`. Plan frozen:
[`deferred_persist_lsv1_edittail_slice_bugfix.md`](../Plans/deferred_persist_lsv1_edittail_slice_bugfix.md). RC-P2 **DEFER**.

**OpenSpec §6.6 partial closeout** — **SHIPPED:** 6C-B, RC-P1-SORT, RC-P1.3, GAP-8 (6.6.4). **REVERTED/DEFERRED:** GAP-8b (6.6.5), GAP-8c (6.6.6). **NOW:** stage 7 (6.6.7). Tracking: [`tasks.md` §6.6](../../openspec/changes/immutable-loop-snapshot-passes/tasks.md#66-post-closeout-polish-linked-plans--non-blocking).

**OpenSpec 6.4 SHIPPED (2026-09-12).** Sign-off on device [`164947`](../../captures/session_20260912_164947.log)
(LSV1 tail + `SAVE,completed`); RAM1 **6848**; native **1707/1707** at RC-P1 close — gates not re-run.
HITL `base` re-run waived; 6.3.7 anchor [`030210`](../../captures/session_20260912_030210.log) unchanged.

**Now — OpenSpec 6.5 (archive/sync).** Runtime docs, deliverable tracking, spec sync + archive.
Parent plan:
[`immutable_loop_snapshot_6_3_compatibility_machinery_removal_refinement.md`](../Plans/immutable_loop_snapshot_6_3_compatibility_machinery_removal_refinement.md) (**SHIPPED**).

**Prior — OpenSpec 6.3.7 SHIPPED (2026-09-12).**
`isPlaybackSessionExitPublicationGap` retained for visual hold-only (6.3.2); dead B1
`session_exit_compat` telemetry removed. Host closeout gates finalized
(`test_6_3_machinery_closeout_gate.py`). Native **1699/1705**.

**Prior — OpenSpec 6.3.6 SHIPPED (2026-09-12).**
`copyEffectiveCommittedEvents` snapshot-only; LCR `Phase::Materialize` removed;
`materializeToEventVector` confined to `LoopPasses` (oracle). Native **1699/1705**; RAM1
**6880**. HITL `base` PASS [`030210`](../../captures/session_20260912_030210.log) /
report [`030255`](../../captures/host_midi_automation_baseline_20260912_030255.json).

**Prior — OpenSpec 6.3.5 SHIPPED (2026-09-12).**
Capture construction replay retired — `gatherConstructionCommittedEvents` snapshot-only;
`copyCommittedPassReplayEvents` removed from `LoopCapture`. Native **1699/1705**; RAM1
**6880**. HITL `base` core PASS [`024434`](../../captures/session_20260912_024434.log) /
report [`024519`](../../captures/host_midi_automation_baseline_20260912_024519.json) — persist
`result` stage flake (5.7 class), not 6.3.5.

**Prior — OpenSpec 6.3.4 SHIPPED (2026-09-12).**
Retired `gatherCommittedEvents*` / `mergeMaterializedPassesWithCapture`; callers use
`copyEffectiveCommittedEvents*`. Native **1699/1705**; RAM1 **6880**. HITL `base` core PASS
[`023806`](../../captures/session_20260912_023806.log) / report
[`023847`](../../captures/host_midi_automation_baseline_20260912_023847.json) — persist
`result` stage flake (5.7 class), not 6.3.4.

**Prior — OpenSpec 6.3.3 SHIPPED (2026-09-12).**
Removed `passesMaterializedStore_`, `materializeEditViewFromPasses`, `midiEvents()` /
`getMidiEvents()`; `copyEffectiveCommittedEvents` → `copyCommittedPassReplayEvents`;
`CommittedMidiScratch` snapshot-only. Native **1830/1835**; RAM1 **6880**. HITL `base` PASS
[`022843`](../../captures/session_20260912_022843.log) / report [`022928`](../../captures/host_midi_automation_baseline_20260912_022928.json).
First attempt [`022633`](../../captures/session_20260912_022633.log) failed on persistence
`already_pending` wedge (`persistence_stage_missing:result`) — same 5.7 flake class, not 6.3.3.

**Prior — OpenSpec 6.3.2 SHIPPED (2026-09-12).**
Session-exit gap visual continuity — cache no-op + draw-source
`shouldHoldSessionExitPublicationGapHandoff`. HITL `base` PASS
[`020309`](../../captures/session_20260912_020309.log). Plan:
[`immutable_loop_snapshot_6_3_2_post_record_visual_continuity_refinement.md`](../Plans/immutable_loop_snapshot_6_3_2_post_record_visual_continuity_refinement.md).

**OpenSpec 4b.3** (documented, not closed): [`030210`](../../captures/session_20260912_030210.log)
(post-6.3.7 HITL) confirms owner split — `ODUB,stop,display` 4.3/7.1 ms; first scored
post-OD PLAYING `midi_gap` 43.7/44.2 ms (`persist_save` ~43 ms + `late_on`/`late_clk` on
OD2; first-OD `track_order` ~17 ms). Bound ref [`233642`](../../captures/session_20260901_233642.log)
OD2 **6.9 ms** when `persist_save` absent. Keep open.

**Prior — OpenSpec 6.3.1 C1-a hold-empty (2026-09-12).**
Retired B1/B3/B4 `gatherCommittedEvents*` and B2/B5 gap `materializeEditViewFromPasses`;
DEC-045 idle-stepped session-exit publish unchanged.

**Prior — OpenSpec 6.2 compatibility consumer retirement (2026-09-12).**
B1–B8 consumer compat retired; construction APIs remain until 6.3.2+.
Plan: [`immutable_loop_snapshot_6_2_compatibility_retirement_refinement.md`](../Plans/immutable_loop_snapshot_6_2_compatibility_retirement_refinement.md).

**Prior — OpenSpec 5.7 + 5.7.5 (2026-09-12).**
Boot-drain stall fix: prioritize workspace save drain bypasses slot-load SD gate
between work items, transport defer on new items, and `otherSdIoActive` writer
policy. HITL `base` PASS [`012147`](../../captures/session_20260912_012147.log)
(`loop_persist=217 sliced=True`). Plan:
[`immutable_loop_snapshot_5_7_persistence_work_queue_stall_hardening_refinement.md`](../Plans/immutable_loop_snapshot_5_7_persistence_work_queue_stall_hardening_refinement.md).

**Prior — OpenSpec 6.2.1b B1 C1-a (2026-09-11).**
HITL `base` PASS
[`231439`](../../captures/session_20260911_231439.log).

**Prior — OpenSpec 6.2.1 C1 inventory (2026-09-11).**
Tool: [`analyze_b1_c1_gather_miss_inventory.py`](../../scripts/analyze_b1_c1_gather_miss_inventory.py);
[`190525`](../../captures/session_20260911_190525.log) gap **759 ms**.

**Prior — Immutable OpenSpec 6.1 SHIPPED / APPROVED (2026-09-11).**

**Prior — Immutable OpenSpec 5.6 SHIPPED (2026-09-11).**
Large-loop persist progress, slice bounds, active snapshot during PLAYING save,
stop-path admit-only, timing-critical call-site gates. HITL `base` PASS
[`203803`](../../captures/session_20260911_203803.log)
`VERIFY deferred persist large-loop bounds` `windows=5 progress=True max_pers=44624us
max_save=44931us active=True stop=True ok=True`. Report
[`203843`](../../captures/host_midi_automation_baseline_20260911_203843.json). Plan:
[`immutable_loop_snapshot_5_6_large_loop_persist_bounds_refinement.md`](../Plans/immutable_loop_snapshot_5_6_large_loop_persist_bounds_refinement.md).

**Prior — Immutable OpenSpec 5.5 SHIPPED (2026-09-11).**
Persist execute stays on `StorageManager::processDeferredSaveState`; HITL `base`
[`200657`](../../captures/session_20260911_200657.log)
`VERIFY deferred persist nonblocking` `loop_persist=295 sliced=True drain=0 mo=600`.
Plan:
[`immutable_loop_snapshot_5_5_deferred_persistence_ownership_refinement.md`](../Plans/immutable_loop_snapshot_5_5_deferred_persistence_ownership_refinement.md).

**Prior — Immutable OpenSpec 5.4 SHIPPED.**
Native matrix, HITL `base` [`181945`](../../captures/session_20260911_181945.log),
reboot `LoadLoopJob` [`182529`](../../captures/session_20260911_182529.log), SD LSV1 byte probe
[`192637`](../../captures/session_20260911_192637.log) after routing fix
[`deferred_persist_loopid_track_routing_bugfix.md`](../Plans/deferred_persist_loopid_track_routing_bugfix.md).
Plan: [`immutable_loop_snapshot_5_4_lsv1_verification_refinement.md`](../Plans/immutable_loop_snapshot_5_4_lsv1_verification_refinement.md).
Authority: [DEC-057](../DECISION_LOG.md#dec-057--loopsnapshot-body-format-version-1-lsv1-dual-write).

**Prior — Immutable OpenSpec 5.2 APPROVED (docs/decision only).**
[DEC-057](../DECISION_LOG.md#dec-057--loopsnapshot-body-format-version-1-lsv1-dual-write):
LoopSnapshot body format version 1; `LSV1` + `formatVersion == 1`; dual-write.
Plan:
[`immutable_loop_snapshot_5_2_storage_layout_approval_refinement.md`](../Plans/immutable_loop_snapshot_5_2_storage_layout_approval_refinement.md).

**Prior — Immutable OpenSpec 5.1 SHIPPED.**
`beforePublishedSnapshot` / `afterPublishedSnapshot` on GUS; deferred after-attach
by originating `UndoEntryId`; pass/ClearSlot restore retained. Native **1841/1**;
RAM1 **6880**; HITL `base` PASS [`150709`](../../captures/session_20260911_150709.log).
Plan:
[`immutable_loop_snapshot_5_1_undo_identity_select_refinement.md`](../Plans/immutable_loop_snapshot_5_1_undo_identity_select_refinement.md).

**Prior — Immutable OpenSpec 4.6 Stage 6 SHIPPED (task 4.6 closed).**
`noteEditFocusMaterializePublishedSnapshotIdentity_` + key match; §5 exclusion
documented (undo/load remain 5.1–5.6). Native **1839/1**; RAM1 **6976**; HITL
`base` PASS [`141547`](../../captures/session_20260911_141547.log). Plan:
[`immutable_loop_snapshot_4_6_snapshot_identity_freshness_refinement.md`](../Plans/immutable_loop_snapshot_4_6_snapshot_identity_freshness_refinement.md).

**Prior — Immutable OpenSpec 4.6 Stage 5 SHIPPED.**
`CommittedMidiScratch` for `legacyMidiEventsFromCommitted`. HITL `base` PASS
[`140021`](../../captures/session_20260911_140021.log).

**Prior — Overdub snapshot bar-crossing membership Stage 5 COMPLETE.**
RC-A hand-off + Stage 4 triangulation + full consumer unwind (6→5→3→1→2)
**SHIPPED** (2026-09-11). Final HITL [`114744`](../../captures/session_20260911_114744.log).
Plan:
[`overdub_snapshot_bar_crossing_membership_bugfix.md`](../Plans/overdub_snapshot_bar_crossing_membership_bugfix.md).

**Prior — Overdub snapshot membership Stage 2 RC-A + Stage 4 SHIPPED.**
`handoffLiveRecordProgressToCommittedPass`; HITL
[`102837`](../../captures/session_20260911_102837.log).

**Prior — Overdub live wrap Stage 2b SHIPPED** (shared NoteId head/tail collapse).
Device [`193615`](../../captures/session_20260910_193615.log): STOPPED
`frame`=`reconstruct`=58; wrap pitches keep On→loopEnd + head. HITL `base`
PASS [`194531`](../../captures/session_20260910_194531.log). Plan:
[`overdub_live_wrap_playhead_tail_display_bugfix.md`](../Plans/overdub_live_wrap_playhead_tail_display_bugfix.md).

**Prior — Immutable OpenSpec 4.6 Stage 4 SHIPPED** (NOTE_EDIT committed-base cache
identity). HITL `base` PASS [`172224`](../../captures/session_20260910_172224.log).
Plan: [`immutable_loop_snapshot_4_6_snapshot_identity_freshness_refinement.md`](../Plans/immutable_loop_snapshot_4_6_snapshot_identity_freshness_refinement.md).

**Prior — LOOP_EDIT length/start fader ignored during RECORD/OVERDUB — SHIPPED.**
`766d1b33`; HITL base PASS [`171420`](../../captures/session_20260910_171420.log).
Plan: [`loop_edit_length_fader_during_capture_bugfix.md`](../Plans/loop_edit_length_fader_during_capture_bugfix.md).

**Prior — Immutable OpenSpec 4.6 Stage 3 — overdub span-cache identity SHIPPED.**
Stage 3: `overdubSourceSpanCachePublishedIdentity_` + reusable match;
native A→B@R; RAM1 **7008**; native **1823/1**; HITL `base` PASS
[`155756`](../../captures/session_20260910_155756.log); occupy machine PASS
[`160337`](../../captures/session_20260910_160337.log).

**Prior — Immutable OpenSpec 4.6 Stage 2 — DrawSourceGatherKey identity SHIPPED.**
Device gate [`154842`](../../captures/session_20260910_154842.log) (`base`).
Host: first-note latency is advisory; serial-follow does not advance offset on
partial lines (was dropping `RECS,stage,publish`).

**Prior — Immutable OpenSpec 4.6 Stage 1 — VisualCache identity stamp SHIPPED.**
Commit `125f8941`; device gates [`151433`](../../captures/session_20260910_151433.log)
(`base`), [`151700`](../../captures/session_20260910_151700.log) (occupy).

**Prior — Immutable OpenSpec 4.5 — SHIPPED.** HITL `base` PASS
([`122812`](../../captures/session_20260910_122812.log)); `overdub_occupy_paint`
PASS ([`123445`](../../captures/session_20260910_123445.log)); compose `02`
PASS **14/14** ([`142119`](../../captures/note_edit_overdub_compose_20260910_142119_serial.log))
after RC1+RC-A. Plan:
[`immutable_loop_snapshot_4_5_cross_gate_verify_refinement.md`](../Plans/immutable_loop_snapshot_4_5_cross_gate_verify_refinement.md).

**Prior — NOTE_EDIT apply PassMaterialize deferral — RC-A shipped.**
Selector Snapshot|VisualCache only; stale VisualCache legal on the request.
Commit `33f21150`. Plan:
[`note_edit_apply_passmaterialize_deferral_bugfix.md`](../Plans/note_edit_apply_passmaterialize_deferral_bugfix.md).

**Prior — NOTE_EDIT F1 select display lag — RC1 shipped (device PASS).**
One `resolveFader1SelectTarget` per inbound F1 pitchbend.
[`095539`](../../captures/session_20260910_095539.log) 77/87 `DNTE`;
[`100844`](../../captures/session_20260910_100844.log) 150/149. Do not
restore PLAYING `timed_burst`. Plan:
[`note_edit_fader1_select_display_lag_bugfix.md`](../Plans/note_edit_fader1_select_display_lag_bugfix.md).

**Prior — NOTE_EDIT control-plane RC3 PLAYING motor ValueOnly — shipped.**
`shouldRunBlockingMotorBurst` is false while PLAYING or OVERDUBBING.
Device [`232026`](../../captures/session_20260909_232026.log) `sequence=value_only`
judged 3. Do not edit compose `01`/`02`/`03`/`04`. Plan:
[`note_edit_control_plane_record_display_stall_bugfix.md`](../Plans/note_edit_control_plane_record_display_stall_bugfix.md).

**Prior — Immutable OpenSpec parent 4.4 closed — inventory only.** Sub-slices
4.4a–4.4h and 4.5a already shipped. Plan:
[`immutable_loop_snapshot_4_4_closeout_refinement.md`](../Plans/immutable_loop_snapshot_4_4_closeout_refinement.md).

**Prior — NOTE_EDIT close must not invent mover Length to loop end — RC1 open.**
[`192140`](../../captures/session_20260909_192140.log) `ChangeLength`
noteId=58 `80–2303` after in-edit overdub. Extend
`shouldRejectMoverPreCommitRow` (inclusive `loopLength - 1`, same skip as
overlap RC3). Do not edit compose `01`/`02`/`03`/`04`. Plan:
[`note_edit_record_first_note_loopend_display_bugfix.md`](../Plans/note_edit_record_first_note_loopend_display_bugfix.md).

**Prior — NOTE_EDIT exit persist parked mover — shipped.** Close commits
parked Visible `currentSpan` ≠ `committedSpan` and does not drop
`applyOwnedEditPassRows` when canonical is empty. Living guide:
[`MOVE_NOTE_LOGIC.md`](../Guides/MOVE_NOTE_LOGIC.md) § Commit / macro
seal. Compose `02` [`225149`](../../captures/host_midi_hitl_note_edit_overdub_compose_20260909_225149.json)
**13/13**. Do not edit compose `01`/`02`/`03`/`04`. Plan:
[`note_edit_exit_persist_selected_mover_bugfix.md`](../Plans/note_edit_exit_persist_selected_mover_bugfix.md).

**Prior — NOTE_EDIT exit during overdub drops the live piano roll — RC1+RC2 device PASS.**
[`223532`](../../captures/session_20260909_223532.log): close while
OVERDUBBING keeps `frame,65` (not `56`); first STOPPED `DISP,0` is
`2304/65` (not `6144`/`50688`). Compose `02` [`222902`](../../captures/session_20260909_222902.log)
**12/12**. Do not edit compose `01`/`02`/`03`/`04`. Plan:
[`note_edit_overdub_exit_piano_roll_revert_bugfix.md`](../Plans/note_edit_overdub_exit_piano_roll_revert_bugfix.md).

**Reject same-pitch NoteOn while pending — RC1 shipped.** `202226`
On@435 then On@520 then Off@529 left `48@435` unpaired; `203112` paints
`DNTE,48,435,435,1869`. `Track::noteOn` rejects the second On so Off@529
closes 435 (length 94). Not replace-on-retrigger. Not MIDI-input reject.
Do not edit compose `01`/`02`/`03`/`04`. Plan:
[`capture_pending_same_pitch_note_on_reject_bugfix.md`](../Plans/capture_pending_same_pitch_note_on_reject_bugfix.md).

**Prior — NOTE_EDIT control-plane record + display stall — RC6 implementing.**
RC2 shipped (`3fc5744c`). RC4/RC5 in tree (not bundled). RC5 motors
accepted on [`190408`](../../captures/session_20260909_190408.log).
RC6: capture-session mover stays in `projectNoteEditDisplayNotes` after
overlap Hide + Pitch (`190408` type=5 of note 94 @2481, no `DNTE`).
Do not edit compose `01`/`02`/`03`/`04`. Plan:
[`note_edit_control_plane_record_display_stall_bugfix.md`](../Plans/note_edit_control_plane_record_display_stall_bugfix.md).

**Prior — NOTE_EDIT reselect moved note on stacked sixteenth — shipped.**
F1 picks among every note start in the aimed sixteenth (`161839` off-grid
`628` on grid `624`; `153550` exact-tick stack). Device PASS
[`164516`](../../captures/session_20260909_164516.log)
`note_idx` 3 then 4 at dest `5@240`. Native **1806 succeeded, 1 skipped**.
RAM1 locals **7008**. Plan:
[`note_edit_reselect_moved_stacked_note_bugfix.md`](../Plans/note_edit_reselect_moved_stacked_note_bugfix.md).

**Prior — NOTE_EDIT select/pitch during overdub hang — shipped.** `145634` full-loop
gather + `delay()` motor burst. Device PASS
[`152705`](../../captures/session_20260909_152705.log)
`sequence=value_only`; HITL-04 machine PASS 4/4
[`152745`](../../captures/host_midi_hitl_note_edit_overdub_compose_20260909_152745.json).
Native **1804 succeeded, 1 skipped**. RAM1 locals **7008**. Plan:
[`note_edit_overdub_select_pitch_hang_bugfix.md`](../Plans/note_edit_overdub_select_pitch_hang_bugfix.md).

**Prior — Compose Shorten hold + reopen select — implementing.** RC1 clips
NOTE_EDIT Shorten onto `overdubSourceViewNotes_` and keeps rematerialize
Shorten against a longer visual. RC2 seals rematerialize-open current
state and lets sixteenth select hit an off-grid note in that step.
HITL `STIM-NE-OD-COMPOSE-04` / preset
`note_edit_overdub_compose_last_bar_reopen` (do not edit `01`/`02`/`03`).
Exit/re-enter is short press. NOTE_EDIT close arms loop-fader ignore
before LOOP_EDIT inbound routing.
Plan:
[`note_edit_overdub_compose_shorten_hold_select_bugfix.md`](../Plans/note_edit_overdub_compose_shorten_hold_select_bugfix.md).

**Prior — Compose last-bar beat-4 Shorten HITL — shipped.** Host-only. Stimulus
`STIM-NE-OD-COMPOSE-03` v1. Preset
`note_edit_overdub_compose_last_bar_shorten`. Device PASS
[`133714`](../../captures/session_20260909_133714.log) /
[`133744`](../../captures/host_midi_hitl_note_edit_overdub_compose_20260909_133744.json):
live On **2113**, F2 to **2112**, Shorten `noteId=56` `2100–2111`. Do not
edit `01` or `02`. Plan:
[`note_edit_overdub_compose_last_bar_shorten_hitl_enhancement.md`](../Plans/note_edit_overdub_compose_last_bar_shorten_hitl_enhancement.md).

**Prior — First record-pass note display vs loop-end Off — parked.** Watch
HITL [`132708`](../../captures/session_20260909_132708.log) did not
reproduce. [`123953`](../../captures/session_20260909_123953.log) store pair
is `SEVT` `N,81` `F,135`. Plan:
[`note_edit_record_first_note_loopend_display_bugfix.md`](../Plans/note_edit_record_first_note_loopend_display_bugfix.md).

**Prior — STIM-NE-OD-COMPOSE-02 promoted.** Everyday compose bind. `01` stays
paint-only (live 12–23). TEMP-A is not registered. Device proof remains
[`123953`](../../captures/session_20260909_123953.log).

**Prior — NOTE_EDIT + overdub paint/overlap compose Stage 2 device PASS.**
Firmware `f7656b1e`. TEMP-A [`123953`](../../captures/session_20260909_123953.log)
HideNote against a live Capture 48 while the apply scratch stayed empty.
Do not change `STIM-NE-OD-COMPOSE-01`. Plan:
[`note_edit_overdub_paint_overlap_compose_bugfix.md`](../Plans/note_edit_overdub_paint_overlap_compose_bugfix.md).

**Prior — HITL stimulus contract Stage 4 shipped.** Everyday catalog:
`host_midi_hitl.py stimulus list`. BASE-N primitives:
`STIM-BASE-2-2-01`, `STIM-BASE-2-ONE-OD-01`, `STIM-BASE-2-FIXTURE-01`.

**Prior — NOTE_EDIT + overdub paint/overlap compose Stage 1 device PASS.**
[`112612`](../../captures/session_20260909_112612.log) open visual **48**,
in-edit overdub `DFRAME` **53 → 63** with 18 live notes. Required stimulus:
`STIM-NE-OD-COMPOSE-01` v1.
Prior FAIL [`110622`](../../captures/session_20260909_110622.log).

**Prior — NOTE_EDIT no-edit empty-store bake.** Shipped.
[`002450`](../../captures/session_20260909_002450.log)
`NoteEditPass close skip empty-store bake`. Plan:
[`note_edit_close_empty_store_bake_bugfix.md`](../Plans/note_edit_close_empty_store_bake_bugfix.md).

**Prior — DEC-056 stale-root companion admission.** Shipped. RAM1 locals
**7008**. Native **1790 succeeded, 1 skipped**. Current-root close
[`000237`](../../captures/session_20260909_000237.log) `root_rev` 4→6.
Miss-path [`000740`](../../captures/session_20260909_000740.log): open
`current=0` `root_rev=8`; close `skip stale-parent publish rows=3`;
source `id=93 p=85 s=1728 e=2016` at `root_rev=16` `play_rev=19`.
Plan:
[`note_edit_close_stale_snapshot_publish_bugfix.md`](../Plans/note_edit_close_stale_snapshot_publish_bugfix.md).

**Prior — Hydrate 4a mover-only seed.** Shipped. Current-root open does
not rematerialize. First focus rebuild seeds the selected mover only.
Move/Pitch PASS [`233644`](../../captures/session_20260908_233644.log),
[`000237`](../../captures/session_20260909_000237.log),
[`000740`](../../captures/session_20260909_000740.log). Session store
stays apply scratch until overlay project. Not 4b–5.
Plans:
[`note_edit_hydrate_enhancement.md`](../Plans/note_edit_hydrate_enhancement.md),
[`note_edit_selectedtick_lcr_resolution_architecture.md`](../Plans/note_edit_selectedtick_lcr_resolution_architecture.md),
[`note_edit_overlay_delta_architecture.md`](../Plans/note_edit_overlay_delta_architecture.md),
[`note_edit_overlay_delta_refinement.md`](../Plans/note_edit_overlay_delta_refinement.md).

**Prior — Hydrate 2–3 device functional PASS [`231008`](../../captures/session_20260908_231008.log).**
Prepared hit: `noteMembership(pitch, bar)` for hold bars. Miss:
`collectAnalyzeOverlapNoteIds` on the already-built visual cache.
`NOTE_EDIT_OPEN` `current=1` `root_rev=4` **75**. First Move
`UNDO_WARM,push,e00,621,75,3` then `candidates_hit`. Hit window (72):
resolve med **8743** µs, scope med **4**. Miss after snapshot not current
(258): resolve med **22206** µs, scan **72–73**. Hide/Shorten/Restore
fire on both paths. Reconstruct med **6411** µs remains. Fast-fader
`hostdrain` peak **1443580** µs is the miss-window leftover, not a
neighborhood or candidate-source fail. Hydrate 4a is the authorized
leftover. Do not start hydrate 4b–5.
Plans:
[`note_edit_hydrate_enhancement.md`](../Plans/note_edit_hydrate_enhancement.md),
[`note_edit_selectedtick_lcr_resolution_architecture.md`](../Plans/note_edit_selectedtick_lcr_resolution_architecture.md),
[`note_edit_overlay_delta_architecture.md`](../Plans/note_edit_overlay_delta_architecture.md),
[`note_edit_overlay_delta_refinement.md`](../Plans/note_edit_overlay_delta_refinement.md).

**Prior — DEC-054 Stage 1 firmware in tree — device recapture failed cadence.**
Select undo warm coalesces, settles 80 ms, and runs after a completed
display frame. [`204458`](../../captures/session_20260908_204458.log)
early select `DFRAME` is in band; back-and-forth select is 3.1–4.6 s /
30 frames (7 warms / 363 ms `focus_snap` in one window). Option B is
not the proposed closeout if DEC-055 is accepted. Plans:
[`note_edit_display_playing_stall_architecture.md`](../Plans/note_edit_display_playing_stall_architecture.md),
[`note_edit_display_playing_stall_refinement.md`](../Plans/note_edit_display_playing_stall_refinement.md).
Do not start Stage 2, hydrate Stage 4a, raise `N`, `T_RT_reserve`, 4.5,
or 4.6.

**Prior — OpenSpec 4.4h — display fallback/identity — Stage 1 shipped.**
`rebuildDisplayNotesInWindow` on a current root copies span notes
(`tryCopyCurrentSnapshotDisplayNotesInWindow`). Overlay gather is
rejected. Live-capture merge keeps the existing gather. RAM1 locals
**6848**; native **1774 succeeded, 1 skipped**. Device
[`201106`](../../captures/session_20260908_201106.log): second overdub
open `from=snapshot` `root_rev=5` `play_rev=5` `notes=114`. NOTE_EDIT
open `current=1` `root_rev=6` **137** / `only_*=0`. `DFRAME` **137**
after second stop through session end. First overdub open
`from=visual` is before `pending=0`. Do not start hydrate Stage 4a,
raise `N`, 4.5, or 4.6.
Plan:
[`display_fallback_identity_snapshot_refinement.md`](../Plans/display_fallback_identity_snapshot_refinement.md).

**Prior — OpenSpec 4.4g — analysis/validation leftover oracles — shipped.**
`validateAndCleanupMidiEvents` uses `Loop::copyIdleValidateEvents`
(current-root span-filtered snapshot events). Overdub analysis
current-root gather already skips pass flatten (4.4a). SEVT / `take_only`
unchanged. RAM1 locals **6848**; native **1773 succeeded, 1 skipped**.
Device [`193700`](../../captures/session_20260908_193700.log): second
overdub open `from=snapshot` `root_rev=7` `play_rev=7` `notes=159`.
[`194039`](../../captures/session_20260908_194039.log): continuation
NOTE_EDIT open `current=1` `root_rev=8` **177** / `only_*=0`. Close
`edits=3`, no `live capture baked`.
[`194438`](../../captures/session_20260908_194438.log): Stage 1 idle
validate at 600.015 s on current `root_rev=1` (record-to-STOPPED + 60 s).
Log-only 2 orphans, 116 events remaining; `DFRAME` **58**. Both overdub
opens `from=snapshot` on a current root.
Plan:
[`analysis_validation_oracle_snapshot_refinement.md`](../Plans/analysis_validation_oracle_snapshot_refinement.md).

**Prior — OpenSpec 4.4f — leftover NOTE_EDIT oracles.** Current-root open
skips `assignMissingNoteIds(loop.midiEvents())`. E: rows undo/redo fallback
uses `Loop::tryCopyNoteEditOpenedSnapshotEvents` (span NoteIds; drops
event-only 64). Pass rematerialize stays the no-current-root fallback.
`take_only` unchanged. RAM1 locals **6848**; native **1772 succeeded, 1 skipped**.
Device [`191346`](../../captures/session_20260908_191346.log): both
`current=1` opens `only_*=0` (120 then 119).
[`192155`](../../captures/session_20260908_192155.log): `current=0` open
keeps pass rematerialize (`only_store=5`); three `current=1` `root_rev=41`
opens are **141** / `only_*=0`.
Plan:
[`note_edit_leftover_oracle_snapshot_refinement.md`](../Plans/note_edit_leftover_oracle_snapshot_refinement.md).

**Prior — Wrap-head vs last-Off pairing RC1–RC3** — accepted without
174646 recapture. Native shipped; [`185428`](../../captures/session_20260908_185428.log)
RC3 analog PASS. Plan:
[`wrap_head_last_off_pairing_bugfix.md`](../Plans/wrap_head_last_off_pairing_bugfix.md).

**Prior — 4.4e Stage 3 + close bake closed.**

**Prior — NOTE_EDIT close bake — snapshot baseline:**
Empty-id close no longer treats snapshot-store vs pass rematerialize as
live-capture rows. Device PASS
[`174646`](../../captures/session_20260908_174646.log): no
`live capture baked`; empty third exit has no `NoteEditPassClosed`;
reopen stays `current=1` `root_rev=13` notes **126**. Plan:
[`note_edit_close_bake_snapshot_bugfix.md`](../Plans/note_edit_close_bake_snapshot_bugfix.md).

**Prior — OpenSpec 4.4e Stage 3 — commit rematerialize from the same root:**
`commitEditAction` persist/replay/session-store reload and
`materializedLoopEventsForNoteEditFocus` use `Loop::copyNoteEditCommitBase`:
pinned open-root span-filtered snapshot events plus this session’s edit
rows. Device PASS
[`173531`](../../captures/session_20260908_173531.log): both `current=1`
opens `only_*=0` (99 then 97). `overlap_baseline_diff` Delete noteId=56
and noteId=11 are on that root. `take_only` 227 vs rematerialize 196 is
capture-only. `current=0` `only_store=1` id `80` is not this gate.
RAM1 locals **6848**; native **1767 succeeded, 1 skipped**.

**Prior — OpenSpec 4.4e Stage 2 — NOTE_EDIT event membership (same root):**
`Loop::rematerializeEditView` on a current published root loads
`EditSession.store` from that root’s event objects whose `NoteId`s are in
`resolveDisplayNotes(*snapshot)` span membership. Device PASS
[`171444`](../../captures/session_20260908_171444.log): both `current=1`
opens `only_visual=0` `only_store=0` (116 then 114). `current=0` opens
after `playback_rev=16` vs `root_rev=11` stay on `passes.materialize`.
Plan:
[`note_edit_committed_oracle_snapshot_bugfix.md`](../Plans/note_edit_committed_oracle_snapshot_bugfix.md).

**Prior — NOTE_EDIT close — NoteRange snapshot reuses pitch:**
`appendEditRowChange` does not write `row.pitch` for `NoteRange`.
Close publish reuses the parent pitch, or the earlier same-`NoteId`
change when Pitch then NoteRange share one `tryPublishNoteEditSnapshot`.
RAM1 locals **6848**; native **1765 succeeded, 1 skipped**. Device PASS
[`163849`](../../captures/session_20260908_163849.log): moved note
`DNTE,63,1584,1584,287`; no pitch-0 `DNTE`; reopen **95**.
Plan:
[`note_edit_snapshot_note_range_pitch_bugfix.md`](../Plans/note_edit_snapshot_note_range_pitch_bugfix.md).

**Prior — OpenSpec 4.4e Stage 1 — NOTE_EDIT visual baseline from current snapshot:**
`resolveDisplayNotes(*snapshot)` copies note-span membership
(`appendSnapshotNotesForBarRange`). Event-flat reconstruct is not the
display interpreter (`155257` 99 vs 97). RAM1 locals **6848**; native
**1763 succeeded, 1 skipped**. Device PASS
[`162207`](../../captures/session_20260908_162207.log):
STOPPED / spans / `stored_notes` / open visual **98**. Wrap-held append
after current-root spans is skipped. Do not start Stage 2/3.
Plan:
[`note_edit_committed_oracle_snapshot_bugfix.md`](../Plans/note_edit_committed_oracle_snapshot_bugfix.md).

**Prior — OpenSpec 4.5a — post-stop snapshot publication convergence:**
Closed. `refreshViewportAfterOverdubStop` holds the last OVERDUBBING
composed frame (`why=stop,from=hold`); no pass-gather on the stop stack.
Drain + `sessionExitQueued` inserts an extra Snapshot turn after every
non-Snapshot owner. First new committed source is the current published
`LoopSnapshot`. Device
[`145633`](../../captures/session_20260908_145633.log): display child 0,
`post_stop` `root_rev == playback_rev`, next open `from=snapshot`.
RAM1 locals **6848**; native **1759 succeeded, 1 skipped**. Plan:
[`overdub_stop_snapshot_publication_convergence_refinement.md`](../Plans/overdub_stop_snapshot_publication_convergence_refinement.md).

**Prior — OpenSpec 4.4c — span-cache snapshot provenance:**
Closed. `overdubSourceSpanCacheReady()` stays a storage/coverage check.
Reuse at open/hold/prewarm requires `overdubSourceSpanCacheReusable()`,
which matches the current published revision when a root is current.
Fallback prewarm at the same `playbackRevision` no longer hides that
root (`135337`). OpenSpec 4.4c. `DEC-052`. RAM1 locals **6848**; native
**1757 succeeded, 1 skipped**.

**Next within 4.4:** NOTE_EDIT, overdub analysis, validation, and remaining
display identity/fallback caller migration remain open. Each sub-slice
requires the existing phase gate and its own rollback/verification evidence.

**Prior — OpenSpec 3.5 / 3.6 closed (yield proof parked):**
3.5 wrap-journal publication is in tree. 3.6 native wrap-journal parity and
device wrap-retain / reserve telemetry
[`101815`](../../captures/session_20260908_101815.log) /
[`104353`](../../captures/session_20260908_104353.log) stand. Remaining-
interval yield device proof is parked: `kSnapshotIdleReserveYieldEnabled`
is false after [`115043`](../../captures/session_20260908_115043.log)
(`remaining=0` until STOPPED). `allowSnapshotIdleReserve`,
`DIAG,snapshot_idle,yield`, and `!SNAPSHOT_YIELD` stay compiled.
`T_RT_safety_margin` stays unfrozen. N = 2. Plan:
[`snapshot_publication_reserve_admission_refinement.md`](../Plans/snapshot_publication_reserve_admission_refinement.md).

**Prior — OpenSpec 3.5 / 3.6 wrap publication notes:**
3.5 firmware is in tree: settled wraps queue bounded mutation journals;
session-exit starts at most one journal-capped fragment; publication waits
until journals, queued passes, construction, and companions complete.
`queueLoadedSnapshotPublish()` stays off after restore (`023222` /
[`024731`](../../captures/session_20260908_024731.log)). This slice does
**not** reconstruct a root from every loaded pass. Task 3.6 native wrap-journal
parity is in tree. Device wrap-retain recapture
[`101815`](../../captures/session_20260908_101815.log): both overdub stops
were still `seal=skipped`, but journals were kept (`exit=1`), `fragment`
advanced (`0/3→2/3`, `0/2→1/2`), and `root_rev` caught `playback_rev`
(`pending=0` `root_rev=3` `playback_rev=3`). Prior fail class
[`094930`](../../captures/session_20260908_094930.log)/`094228` discarded
in-flight wrap journals and left `root_rev` at 1. Host HITL overall still
FAIL for known non-wrap issues (`dispatch`, 2304-vs-1536, SEVT window,
second-overdub first-note). Focused native **83/83**. `T_RT_reserve` policy
is [DEC-053](../DECISION_LOG.md#dec-053--t_rt_reserve-is-ceiling-plus-per-turn-midi-boundary)
(`T_RT_reserve_ceiling` 10,000 µs; per-turn `reserve_us`; margin not
frozen). Capture-gated `DIAG,snapshot_idle,reserve` is in
`InvestigationCaptureLog` (FLASHMEM): child emit arms one pending start;
`handleMidiInput()` enter pairs `reserve_us`. Unpaired overwrite emits
`reserve_us=0`. Device recapture
[`104353`](../../captures/session_20260908_104353.log): 1864 `reserve`
lines, `reserve_us` 102–4779 µs (none above the 10 ms ceiling), wrap
`pending=0` `root_rev=3`, both overdub `seal=skipped` still captured with
full `seal_fx_*` sequence. Serial close drain 309 trailing lines. Remaining-
interval admission is now in tree on `MidiServiceDrain` /
`Track::processDeferredIdleMaintenance`: two-unit cost must fit
`min(10000, remaining − margin)` or the turn yields. Capture-gated
`DIAG,snapshot_idle,yield` plus `!SNAPSHOT_YIELD` one-shot inject.
`SESSION_CAPTURE_SNAPSHOT_IDLE=1` is on `teensy41-capture-serial` for this
gate. Snapshot idle cardinality is **N = 2**; do not revert to 1. STOPPED
timings are excluded from the 3.6 ceiling. [`110034`](../../captures/session_20260908_110034.log)
live `reserve_us` max 5871 µs (zero over 10 ms); the 36 / 42 / 95 ms
samples are after `PLAYING → STOPPED`. Host: RAM1 locals **6848**; focused admission/yield native PASS;
full native **1753/1754** (1 skipped). Device yield HITL
[`115043`](../../captures/session_20260908_115043.log):
`snapshot_reserve_admission.py` PASS (`yield_count=6389`, first yield
`RECORDING` `root_rev=0`, complete `root_rev=9` `pending=0`). Every live
yield has `remaining=0`; the first `snapshot_idle` construction is after
`PLAYING → STOPPED`; `pending=0` is that STOPPED publish. No yield after
STOPPED. Host base still FAIL for the known non-slice checks
(`record_first_note_too_late`, 2304-vs-1536, `dispatch`, SEVT window).
Yield reject is parked (`kSnapshotIdleReserveYieldEnabled` is false).
`DEC-047`. `DEC-053`.

**Prior — Loaded hydration stability — device PASS [`024731`](../../captures/session_20260908_024731.log):**
Title exit waits for boot restore only. Adopt/restore do not auto-queue
loaded snapshot publication. After title, Stage 2.1 fallback fills from
passes (`root_rev,0`). Zero `ld=1`. Session-exit publication unchanged.
Plan: [`loaded_hydration_stability_bugfix.md`](../Plans/loaded_hydration_stability_bugfix.md).

**Prior — Visual-cache Stage 2.1 — snapshot membership copy:**
When `publishedSnapshotIsCurrent()`, `rebuildVisualCacheIdleSlice` copies
`LoopSnapshotNote` membership for the dirty bar range. Fallback / prepared
fill stays when no current root exists. Host: RAM1 locals **6848**. Native
**1727/1728** (1 skipped).

**Prior — Overdub MIDI boundary — Stage 4a device attribution [`235930`](../../captures/session_20260907_235930.log):**
Tags emit. Residual ≤ 24 µs; slot ≤ 26 µs. `tracks` ≈ `clk` ≈ `usbclk`.
OD1 peak `tracks` 13149 µs (T4 9007 + T6 3932) under `service_period`
20824. Dominant attributed span on that peak is `track_order` 7974 µs,
not rebuild multiplicity. Ring A is not authorized from this log.
`playCommittedLoopMidi` stays consumer, not optimization owner. Plan:
[`overdub_midi_boundary_timing_refinement.md`](../Plans/overdub_midi_boundary_timing_refinement.md).

**Prior — Overdub-stop first PLAYING frame parity — HITL PASS [`230846`](../../captures/session_20260907_230846.log):**
The first `OVERDUBBING → PLAYING` display frame now preserves the effective
committed handoff while deferred save starts. `refreshViewportAfterOverdubStop`
finalizes from `LoopContentResolution::resolveDisplayNotes` after cache
adoption, and `resolveDisplayNotesByDrawSource` preserves that
revision-matched handoff before normal source/cache rebuilding resumes. The
cache is a reuse seed, not committed-content authority. RAM1 locals **6848**;
focused native **162/162**; full native **1724/1725** (1 skipped). HITL
`DISP` and `dnte` frame/effective counts match on the first and settled frames
(`98`/`98`). RC closed.
Plan: [`overdub_display_snapshot_handoff_bugfix.md`](../Plans/overdub_display_snapshot_handoff_bugfix.md).

**Prior — First STOPPED D# next-On flash — HITL PASS [`225234`](../../captures/session_20260907_225234.log) ([`220319`](../../captures/session_20260907_220319.log) /
[`222432`](../../captures/session_20260907_222432.log) /
[`223121`](../../captures/session_20260907_223121.log)):**
`src,effective` is correct while `src,frame` still uses stale cache geometry.
The `f53cc59a` companion-replay workaround regressed to all-bars lengthening
in [`223121`]. It is reverted. [`224901`] showed the preserved overdub-stop
handoff still bypassed the direct resolver. STOPPED committed display now
calls `LoopContentResolution::resolveDisplayNotes` before that handoff,
reusing the same final-state code as `src,effective`. `af65a70e` remains
because it fixed the deferred effective/rebuild pairing path. RAM1 locals
**6848**. Focused native **232/232**; full native **1724/1725** (1 skipped).
HITL [`225234`] passes: `src,effective` and `src,frame` have matching
11-row D# geometry and `DISP` counts. RC closed.
Plan: [`overdub_stopped_frame_last_occupy_bugfix.md`](../Plans/overdub_stopped_frame_last_occupy_bugfix.md).

**Prior — Overdub same-start A loss and D# Off pairing ([`183043`](../../captures/session_20260907_183043.log) / [`185822`](../../captures/session_20260907_185822.log) / [`192322`](../../captures/session_20260907_192322.log) / [`195409`](../../captures/session_20260907_195409.log) / [`204112`](../../captures/session_20260907_204112.log) / [`211440`](../../captures/session_20260907_211440.log)):**
Stage 1–3, RC-A, overlay, and pending-paint device PASS [`211440`].
Stop D# Off pairing host in tree: deferred visual-cache fallback/full
rebuild, last-resort committed/window reconstruct (capture inactive), and
`dnte,src,effective` use `LoopContentResolution::resolveDisplayNotes` so
committed D# Offs `768→1440`, `1536→2208`, `2304→2976` survive flatten.
`Track::finalizePendingNotes` still closes one genuinely held final D#.
RC-B unmatched NoteOff admission is not started. Native pin
`test_visual_cache_keeps_dsharp_committed_offs_after_rebuild_211440`.
`teensy41-capture-serial` RAM1 locals **6848**. HITL recapture open. Plans:
[`overdub_same_start_note_pairing_bugfix.md`](../Plans/overdub_same_start_note_pairing_bugfix.md),
[`overdub_bar1_compose_miss_bugfix.md`](../Plans/overdub_bar1_compose_miss_bugfix.md),
[`overdub_overlay_identity_bugfix.md`](../Plans/overdub_overlay_identity_bugfix.md),
[`overdub_pending_paint_frame_drop_bugfix.md`](../Plans/overdub_pending_paint_frame_drop_bugfix.md),
[`overdub_stop_dsharp_noteoff_pairing_bugfix.md`](../Plans/overdub_stop_dsharp_noteoff_pairing_bugfix.md).

**Prior — Overdub occupy wrap `norowid` / currentTick temp Off ([`174151`](../../captures/session_20260907_174151.log)):**
Wrap cursor walk offers `(prev, L-1]` then `[0, S]` to
`applyPlaybackEvent`. Occupy span cache / last-pass merge recover
omitted committed NoteOn identities with that same untagged-Off / LIFO
rule (reconstruct can defer Off@`L-1`). Incomplete `visualCache` is a
cache miss, not absence. Paint tails stay downstream. Host pins PASS.
Native **1704/1705** (1 skipped). RAM1 locals **6848**. HITL recapture
open. Plan: [`overdub_occupy_wrap_norowid_noteoff_bugfix.md`](../Plans/overdub_occupy_wrap_norowid_noteoff_bugfix.md).

**Prior — Overdub display snapshot handoff ([`170537`](../../captures/session_20260907_170537.log)):**
Compose the paint window from a complete sealed committed source, then
cache for reuse. `visualCache.notes` is not committed-content authority.
Just-sealed overdub rows join the committed prefix before Hide/Shorten.
A smaller current snapshot must not replace a complete handoff (`id=112`).
MIDI-boundary Stage 3/4 stays out of scope.
Focused native PASS (`test_display_window_utils`, `test_loop_take_survival`,
`test_pending_note_change`, `test_overdub_source_view`). Full native PASS:
1700 succeeded, 1 skipped.
`teensy41-capture-serial` RAM1 locals **6848**. Full native + HITL open.
Plan: [`overdub_display_snapshot_handoff_bugfix.md`](../Plans/overdub_display_snapshot_handoff_bugfix.md).

**Prior — Overdub MIDI boundary timing — Stage 2 (USB batch composition):**
`MidiBatchComposition` plus `RuntimeTimingTelemetry` window tags
`usb_batch`, `usb_bkt_size`, `usb_bkt_clk`, `usb_bkt_disp`. Dispatch
order is unchanged. Native **1689/1690** (1 skipped). RAM1 locals **6848**.
Slave-clock [`170537`](../../captures/session_20260907_170537.log):
`clockrate` 47–48, `service_period` ≈ 20824 µs, `late_clk` = 0.
`usb_bkt_clk` all in 1–8; `clk_batch` max 3 (OD1/OD2 max 1); saturated 0.
OD2 `usbclk`/`tracks` 26.3 ms; `extclk_arrival` 66.6 ms / overCount 240.
Stage 3 FIFO is not authorized — no large clock batch. Internal-clock
occupy [`170249`](../../captures/session_20260907_170249.log) HITL **FAIL**.
Plan: [`overdub_midi_boundary_timing_refinement.md`](../Plans/overdub_midi_boundary_timing_refinement.md).

**Prior — Stage 1 (clock attribution):**
`extclk_arrival`, `extclk_dispatch`, `clk_batch`, `service_period` on
`RuntimeTimingTelemetry`. Occupy HITL [`165045`](../../captures/session_20260907_165045.log)
emits those tags (`service_period=2666`; clock-batch fields 0). Native
**1686/1687**. RAM1 locals **6848**. Stages 3–4 not started.

**Prior — One-frame drop after each next bar ([`142336`](../../captures/session_20260907_142336.log)) — device PASS [`143701`](../../captures/session_20260907_143701.log):**
After `004710` pending-first ordering, `applyCapturePlayheadTails`
still matched open holds by pitch+start only and rewrote a closed
same-start sibling. Capture preview also dropped assigned `NoteId`s
because append fed the raw input event into `applyCaptureEventToPreview`.
Tails now prefer `NoteId`; append feeds the stored `toAppend` event.
Host pins `test_same_pass_playhead_tails_keep_closed_bar_four_end` and
`test_append_capture_preview_keeps_assigned_note_id`. Device
[`143701`](../../captures/session_20260907_143701.log): shortened long
rows stay painted across each next `BAR` (`hidden,0`).
Plan: [`overdub_wrap_sealed_shorten_paint_bugfix.md`](../Plans/overdub_wrap_sealed_shorten_paint_bugfix.md).

**Prior — Incoming long note hidden until NoteOff ([`004710`](../../captures/session_20260906_004710.log)):**
Overlap resolve writes pending transforms on NoteOn, but paint applied tails
before pending. A same-start closed row absorbed the open-hold tail update,
then pending Hide/Shorten removed that row, so the long incoming note was
missing until NoteOff. Paint now applies pending first and tails second in
both live resolve and draw-source gather. Host pin
`test_pending_then_tails_keeps_incoming_long_same_start_004710`.
Device: incoming long notes must appear at NoteOn and grow to `currentTick`
while overlapping earlier shorter notes.
Plan: [`overdub_wrap_sealed_shorten_paint_bugfix.md`](../Plans/overdub_wrap_sealed_shorten_paint_bugfix.md).

**Prior — Earlier remnant dropped the new long occupy ([`003622`](../../captures/session_20260906_003622.log)):**
`002708` covering-occupy drop treated the older longer remnant
(`768–1344`) as the occupy that covers the new long Add (`816–1344`).
Paint kept the short keeper and removed the long Add. An ungrown open
hold on pitch 69 also dropped later A notes for a frame. Covering now
requires the earliest row to be the shortest keeper at its start, and
the hold must actually cover the inner. Host pins
`test_earlier_remnant_does_not_drop_new_long_occupy_003622` and
`test_ungrown_earlier_hold_does_not_drop_later_same_pitch_003622`.
`002708` inners under a true long keeper still drop. Device: long occupy
stays; record A notes do not vanish for a frame each bar.
Plan: [`overdub_wrap_sealed_shorten_paint_bugfix.md`](../Plans/overdub_wrap_sealed_shorten_paint_bugfix.md).

**Prior — Long occupy leaves inner shortened notes ([`002708`](../../captures/session_20260906_002708.log)):**
After a second pass, a long occupy (`864–1344`) covers already-shortened
inners (`1056–1200`, `1296–1335`). Stop effective hid them; the frame
still painted them. Same-start Hide drop kept `1296–1335` as a shorter
keeper because the occupy starts earlier. Hide now drops an inner when
an earlier same-pitch occupy covers it. Hide absorbs a later Length of
the same NoteId. Host pin
`test_long_occupy_hides_inner_shortened_notes_002708`. Device: a long
occupy that covers several shortened notes must not show those inners
on the next `DISP` after the occupy NoteOn, including after wrap.
Plan: [`overdub_wrap_sealed_shorten_paint_bugfix.md`](../Plans/overdub_wrap_sealed_shorten_paint_bugfix.md).

**Prior — Pending Shorten overwritten by sealed Length ([`002200`](../../captures/session_20260906_002200.log)):**
After wrap 2, occupy of already-sealed `73@768–1247` at `hs=1056` writes
pending `768–1055`. Paint applied pending first, then sealed Length
`1247`, so `painted_pending_shorten` never logged 73 and OLED kept the
older end until stop. Stop effective/frame already have `768–1055`.
Sealed overlay now applies first; this-lap pending wins. Host pin
`test_pending_shorten_wins_over_older_sealed_length_002200`. Device:
after two wraps, a further occupy Shorten must show `holdStart-1` on the
next `DISP` before the next `BAR`.
Plan: [`overdub_wrap_sealed_shorten_paint_bugfix.md`](../Plans/overdub_wrap_sealed_shorten_paint_bugfix.md).

**Prior — Same-start Hide remnant ([`001145`](../../captures/session_20260906_001145.log)):**
After several occupy Shortens, a later same-start occupy Hides the
previous Add (`63@1152–1440` `hs=1152`). Hide removes that NoteId, but
the closed remnant stayed because the new occupy is an open hold and
shortest-wins never dominates it. That remnant is the old note that
returns before the new occupy lengthens. Hide now drops closed rows
whose end is at least the hidden source end. Shorter keepers
(`201956` 768–864) and open holds stay. Host pins
`test_same_start_hide_drops_closed_remnant_keeps_open_hold_001145` and
`test_same_start_hide_drops_remnant_when_hold_present_001145`. A Hide
whose target was never in the frame still leaves that row (`RC3`). Device: a
same-start Hide must not show the hidden source on the next `DISP`
before the occupy Add grows.
Plan: [`overdub_wrap_sealed_shorten_paint_bugfix.md`](../Plans/overdub_wrap_sealed_shorten_paint_bugfix.md).

**Prior — Later occupy Shorten remnant ([`000402`](../../captures/session_20260906_000402.log)):**
`painted_pending_shorten` already rewrites later NoteIds (id 73 after
id 71; id 77 after id 75). OLED still shows the original length until
overdub stop because `drawAllNotes` also draws a longer same-pitch+start
sealed-preview remnant. Sibling drop now removes closed remnants at
named Hide/Shorten pitch+starts, including the live-capture suffix.
Open holds stay. Occupying Adds start at hold, not at the source start.
Gather upsert during capture is unchanged (`150650`). Host pins
`test_pending_shorten_drops_suffix_longer_remnant_000402` and
`test_later_occupy_shorten_drops_suffix_remnant_000402`. Device: every
occupy Shorten, not only the first, must show `holdStart-1` on the next
`DISP` before the next `BAR`, before and after wrap.
Plan: [`overdub_wrap_sealed_shorten_paint_bugfix.md`](../Plans/overdub_wrap_sealed_shorten_paint_bugfix.md).

**Prior — Wrap sealed Shorten paint ([`235546`](../../captures/session_20260905_235546.log)):**
First sealed Shorten stayed after wrap; later ones did not. RC-C:
`recoverMissingSealedShortenSources` skipped a Length companion when
snapshot/source-view/visual missed that NoteId. Fallback is the sealed
Length row itself (`74@1536–1967` stayed; `76@2304–2735` must recover).
Host pin `test_wrap_seal_keeps_later_painted_shorten_235546`. Prior RC-A
suffix apply and RC-B recover owner stay.

**Wrap-held head occupy Shorten ([`225200`](../../captures/session_20260905_225200.log)) — Stage 1:**
Pin Failure A passed: wrap-held `69@4560→760` id 96 stays in source view
and `collectOverdubSourceHoldParticipantIds(48, 69)` contains it. Failure
B was occupancy: `startTick < holdStart` wrote Hide (`4560 < 48` is
false). `resolveOccupancyAtNoteOn` now Shortens a wrap-held row present
at a head hold (`hold > 0`, `hold != start`) to `hold-1`.
`composedShortenDegradesToHide` keeps wrap-held Shorten (`end < start`).
Linear Shorten/Hide and `hs=0` Hide are unchanged. Host pin
`test_wrap_held_record_row_occupies_head_225200`. Focused
`test_pending_note_change` + `test_overdub_source_view` PASS.
`teensy41-capture-serial` RAM1 locals **6848**. Native **1669/1672**
(pre-existing playhead-tails fail; one skipped). Device recapture:
post-wrap occupy of that wrap-held row at `hs=48` must be `a>=1` and
`painted_pending_shorten` before the next `BAR`. `63@hs=0` `n=0,a=0`
stays correct. Do not start Stage 2 (stop-hop foreign frame).
Plan: [`overdub_wrap_head_occupy_shorten_bugfix.md`](../Plans/overdub_wrap_head_occupy_shorten_bugfix.md).

**Prior — Wrap NoteOn Shorten recovery ([`223435`](../../captures/session_20260905_223435.log)):**
Post-wrap1 occupies wrote pending Shorten (`as=1536,ae=2208,hs=1824` /
`hs=1872`) but the next paint omitted `painted_pending_shorten`. Later
same-lap `as=2304,ae=2976,hs=2592` painted id 72; stop sealed
`oh shorten=2`. Recovery required VisualCache to still hold the same
`NoteId`. After wrap both compose and VisualCache can miss that identity
while source-view still has the row. `classifyPendingShortenSource` now
recovers whenever compose lacks the target and an authoritative row
exists. VisualCache identity remains `authoritative_absence` evidence
only. Host pins in `test_display_window_utils` and
`test_pending_note_change`. `teensy41-capture-serial` RAM1 locals
**6848**. Native **1668/1671** (pre-existing playhead-tails fail; one
skipped). Device recapture required: first post-wrap Shorten hold must
paint before the next `BAR`.
Plan: [`overdub_paint_wrap_noteon_shorten_bugfix.md`](../Plans/overdub_paint_wrap_noteon_shorten_bugfix.md).
Parent: [`overdub_paint_noteon_shorten_paint_bugfix.md`](../Plans/overdub_paint_noteon_shorten_paint_bugfix.md).

**Prior — reported unfixed undo/reappear after stop ([`221758`](../../captures/session_20260905_221758.log)):**
User reports that a previously undone overdub pass reappears after
overdub stop. That capture boots empty and contains **no undo or redo**.
Not this wrap-recovery RC. Blocked on a capture that contains the undo
and the reappearing notes.

**Prior — Overdub paint NoteOn Shorten on the first paint ([`221758`](../../captures/session_20260905_221758.log), paint gate met):**
[`215658`](../../captures/session_20260905_215658.log) wrote pending Shorten
at NoteOn (`as=768,ae=1440,hs=1104`) but the next paint still matched
VisualCache geometry. Live capture now invokes
`recoverMissingPendingShortenSources`, then applies pending on the paint
copy only. Compose stays original. Device: first occupy Shorten
`65928297` `63@2304–2928` `hs=2784` → `painted_pending_shorten` `65977618`
id **114** `2304–2783` before `BAR,19200,25`; row stays through the next
BAR. Same-start Hides do not emit. No `shorten_src` required. Parent
source recovery and Stage 1+2 stay closed.
Host: focused suites PASS; `teensy41-capture-serial` RAM1 locals **6848**;
native **1667/1670** (pre-existing playhead-tails fail).
Plan: [`overdub_paint_noteon_shorten_paint_bugfix.md`](../Plans/overdub_paint_noteon_shorten_paint_bugfix.md).
Parent: [`overdub_paint_shorten_source_recovery_bugfix.md`](../Plans/overdub_paint_shorten_source_recovery_bugfix.md).

**Prior — pending Shorten source recovery ([`212700`](../../captures/session_20260905_212700.log)):**
Compose can drop a Shorten target when the selected current snapshot
membership omits a row that VisualCache still has (`old,84,4560,4608` →
`new,0,0,0`). Hide can look correct from that absence; Shorten cannot.
`resolveDisplayNotesByDrawSource` recovers the authoritative committed
row by `NoteId` before the paint copy. VisualCache is mismatch evidence
only — never copied. Authoritative absence stays absent. Hide is not
recovered. Stage 1 paint-copy and Stage 2 wrap-append stay closed.
Host: focused suites PASS; `teensy41-capture-serial` RAM1 locals **6848**;
native **1663/1666** (pre-existing playhead-tails fail). Device recapture
superseded by [`215658`](../../captures/session_20260905_215658.log).
Plan: [`overdub_paint_shorten_source_recovery_bugfix.md`](../Plans/overdub_paint_shorten_source_recovery_bugfix.md).
Parent Stage 1+2: [`overdub_paint_noteon_pending_handoff_bugfix.md`](../Plans/overdub_paint_noteon_pending_handoff_bugfix.md).

**Prior — NoteOn pending handoff + wrap-append source guard:**
Stage 1 ([`202821`](../../captures/session_20260905_202821.log)): NoteOn
`pendingNoteChanges_` advances the live paint representation. Compose stays
untransformed; the next paint consumes a pending-painted copy. Overview does
not read a clean VisualCache while pending/overlay exists. Resolver, ledger,
and VisualCache publication stay unchanged.
Stage 2 ([`203406`](../../captures/session_20260905_203406.log)):
`applyDeferredVisualCacheWrapAppend` appends only when the deferred
`playbackRevision` still matches. A mismatch obtains the current-source
frame (`append_skip`, then `full` if the cache is not already current).
Host: `teensy41-capture-serial` RAM1 locals **6848**; native **1660/1661**
(pre-existing playhead-tails fail). Device recapture open.
Plan: [`overdub_paint_noteon_pending_handoff_bugfix.md`](../Plans/overdub_paint_noteon_pending_handoff_bugfix.md).
Do not start Bug 4, Bug 1, or Bug 3 until device recapture closes.

**Prior — Bug 2 occupy On paint named ([`202821`](../../captures/session_20260905_202821.log)):**
Occupy `#CAP,30465195` `63` `as=1536 ae=2208 hs=2016` → next draw-source
`after_pending` and `after_tails` are `id=73` end **2015**. Forks A, B
(at that On), and C are false. First-loop `id=0` + tails growth is the
open-hold skip. Do not patch `applyCapturePlayheadTails`. Plan:
[`overdub_occupy_paint_191913_findings_bugfix.md`](../Plans/overdub_occupy_paint_191913_findings_bugfix.md).

**Prior — Stage 0 RC3 NoteId Off pair reverted ([`191913`](../../captures/session_20260905_191913.log)):**
Restore exact pre-`44f47e94` reconstruct LIFO. Keep `175026` `52@664`
(E=144 vs occupy 363) as a parked finding, not a production pairing
contract. Diagnostic `diagnostic_only_reconstruct_175026_tagged_off_pairing`
is `TEST_IGNORE`. Do not add NoteId pairing fallbacks. 2304 is a legal
loop. Documented order after this pin: Bug 4 → 1 → 2 → 3. Plan:
[`overdub_occupy_paint_191913_findings_bugfix.md`](../Plans/overdub_occupy_paint_191913_findings_bugfix.md).

**Prior — RC3 tagged-Off reconstruct pair (reverted) ([`175026`](../../captures/session_20260905_175026.log) `52@664`):**
`44f47e94` paired tagged Offs by `NoteId`. That fixed one reconstruct
mismatch and is reverted. Parked diagnostic documents LIFO=144 vs
desired 376. Plan:
[`overdub_paint_shorten_live_stop_bugfix.md`](../Plans/overdub_paint_shorten_live_stop_bugfix.md).

**Prior — RC1 pending overlay eligibility ([`172734`](../../captures/session_20260905_172734.log)):**
`applyPendingHideAndShortenToNotes` skips only open capture holds
(`capturePreviewNoteIsOpenHold`). Closed prefix and sealed/closed suffix
rows take pending Hide/Shorten by `NoteId`. `liveCaptureBegin` stays the
sibling-drop / Hide-by-overlap prefix end. No pitch+start fallback.
Host `test_pending_shorten_172734_open_hold_eligibility_matrix`. Device
recapture required: live frame must show this-pass Shorten (`63@680→1207`)
before the next BAR.

**Prior — Capture-serial volume + undo labels ([`213738`](../../captures/session_20260901_213738.log)):**
`SESSION_CAPTURE_SNAPSHOT_IDLE`, `SESSION_CAPTURE_BPM`, and
`SESSION_CAPTURE_DRAW_SRC` default **0** on `teensy41-capture-serial`.
Re-enable independently or via `teensy41-capture-serial-investigation-diag`.
`DISP` / `DFRAME` / `DIAG,clockrate` / `ODUB,stop,*` / `ST` / `VCACHE` stay on.
`TrackUndo::undoForLoop` / `redoForLoop` log `Record undone` /
`Overdub undone` (and the other `UndoEntryKind` names) instead of calling
every non-clear apply `Overdub undone`. HITL still matches `Overdub undone`
/ `Overdub redone` for `OverdubPassAdded`. Not this slice: undo paint
(RC5b keep-larger) or OD1-stop `stale_range` visual shrink. **3.6 `T_RT_reserve`** stays open under [DEC-053](../DECISION_LOG.md#dec-053--t_rt_reserve-is-ceiling-plus-per-turn-midi-boundary);
turn `SESSION_CAPTURE_SNAPSHOT_IDLE=1` back on when emitting `reserve_us`.

**Prior — RC1 occupy-paint closed; RC2 / RC3 withdrawn ([`172213`](../../captures/session_20260905_172213.log)):**
RC1d-d sealed closed Off shipped. Occupy-paint verifier **PASS** [`172322`](../../captures/host_midi_hitl_occupy_paint_20260905_172322.json): `rc1b.mismatches=[]`. **RC2 / RC3 withdrawn** — [`140729`](../../captures/session_20260903_140729.log) `77@336` / `69@5328` cannot be remade. Do not start sealed upsert or admission-miss from occupy-paint. Plan: [`overdub_paint_effective_frame_parity_bugfix.md`](../Plans/overdub_paint_effective_frame_parity_bugfix.md).

**Prior — occupy hold shared capture phase ([`162652`](../../captures/session_20260905_162652.log)):**
`Track::snapshotOverlapHoldCandidates` and `Track::collectOverlapHoldPlaybackNoteOn` compute `holdStart` via `Track::capturePhaseTick`, the same frame as capture storage and source-view ticks. Origin-0 `tickPhaseInLoop(..., 0, loopLength)` is off by `startLoopTick` and produced missed occupy, wrong-frame Shorten, and spurious occupy when transport was already running. Host pin `test_occupy_nonzero_start_loop_shortens_at_storage_hold`. HITL occupy_paint required in both transport conditions (`startLoopTick == 0` and nonzero). Plan: [`overdub_occupy_hold_phase_frame_bugfix.md`](../Plans/overdub_occupy_hold_phase_frame_bugfix.md).

**Prior — RC1d-c occupy HITL recapture [`162325`](../../captures/session_20260905_162325.log) (`fedcc25f`):**
Flashed `teensy41-capture-serial`. Occupy holds PASS: OD1 `shorten=0 hide=0`; OD2 `shorten=3 hide=11`, `from=snapshot`, visual 22, GUS 2/2. Dumps reconstruct 39 / effective 28 / frame 26. No two-length. `52@2172` absent. HITL FAIL is one 1-tick key: `52@2180` f=46 e=47 (E=R=47). Plan: [`overdub_paint_effective_frame_parity_bugfix.md`](../Plans/overdub_paint_effective_frame_parity_bugfix.md).

**Prior — RC1d-c first-STOPPED playhead tail ([`153848`](../../captures/session_20260905_153848.log) `52@2172`):**
Last OVERDUBBING playhead tail freezes inclusive 2230 on `liveDisplayNotes`. RC1b skip keeps that prefix after stop. `refreshViewportAfterOverdubStop` rewrites stop-phase tails from sealed closed Off (2231). Host pins PASS. `teensy41-capture-serial` RAM1 locals **6848**. Native **1647/1649** (pre-existing playhead-tails fail). Do not change RC1b skip, events, or E=59.

**Prior — RC1d Loop pin (investigation only, [`153848`](../../captures/session_20260905_153848.log) `52@2172`):**
Host `test_occupy_153848_52_2172_first_inclusive_2230_pin` PASS. Effective Off@2231 / dump 59. Draw-source prefix, sealed union, and companion overlay stay inclusive 2231. No pending/Length companion on the Add at 2230. **Not RC1d-a. Not RC1d-b.**

**Prior — RC1c occupy HITL recapture [`153848`](../../captures/session_20260905_153848.log):**
Flashed `565b87cb` `teensy41-capture-serial`. Occupy holds PASS: OD1 `shorten=0 hide=0`; OD2 `shorten=3 hide=11`, `from=snapshot`, visual 22, GUS 2/2. Dumps reconstruct 39 / effective 28 / frame 26. No two-length. No `F == R` remnant (`48@666` E=F=396 vs R=744). HITL FAIL is one 1-tick key: `52@2172` f=58 e=59. First run [`153705`](../../captures/session_20260905_153705.log) dropped USB before dumps. Extra-short hold-1 keys (268 / 164) are absent. RC2 / RC3 stay gated.

**Prior — RC1c window gather per-layer apply ([`144743`](../../captures/session_20260905_144743.log) / [`152012`](../../captures/session_20260905_152012.log)):**
`copyEffectiveCommittedEventsInRange` applies Active Note rows on each committed chunk list, then merges. Host `test_occupy_144743_48_616_materialize_vs_window_vs_pending` expects window gather **400**, On@885 absent, Off@1016 kept. `teensy41-capture-serial` RAM1 locals **6848**. Native **1644/1646** — pre-existing `test_same_pass_playhead_tails_keep_closed_bar_four_end` (does not call window gather).

**Prior — occupy HITL recapture [`152012`](../../captures/session_20260905_152012.log):**
Flashed `teensy41-capture-serial` (no RC1c firmware). Occupy holds PASS: OD1 `shorten=0 hide=0`; OD2 `shorten=3 hide=11`, `from=snapshot`, visual 22, GUS 2/2. RC1c FAIL — 9 `E != F`, no two-length; dumps reconstruct 39 / effective 28 / frame 25. Representative `48@666` frame 739 / effective 399 (same reconstruct-remnant class as [`144743`](../../captures/session_20260905_144743.log) `48@616`).

**Prior — occupy present-at-hold RC4 ([`134436`](../../captures/session_20260905_134436.log)):**
`resolveOccupancyAtNoteOn` may transform a ledger-selected note only when that note is present at the hold phase. Filter before geometric fallback. Do not change Hide-absorbs-Shorten. Do not retune snapshot fill. Host `a04e2bd5`.

**Prior — effective vs frame RC1b ([`154310`](../../captures/session_20260903_154310.log)):**
After stop, draw-source sealed union does not append a linear row whose pitch+start already exists in the committed prefix. During capture, suffix upsert stays (`150650`). RC1 overlay list stays. Named Hide/Shorten sibling drop now includes closed suffix remnants (`000402`); open holds and gather upsert stay. Do not add gather `keepShortest`. Occupy HITL phase windows close OD2 on `Overdubbing stopped` / `ODUB,stop,enter` when `ST,OVERDUBBING→PLAYING` is missing ([`133818`](../../captures/session_20260905_133818.log)).

**Prior — effective vs frame RC1 ([`140729`](../../captures/session_20260903_140729.log)):**
Display overlay appends sealed companion ids across wraps so frame realizes already-correct effective Hide/Shorten. `lastOverdubSealCompanionIds_` stays last-wrap-only for admission. Overlay list is a transitional bridge, not a second effective owner. Device first-STOPPED two-length FAIL [`154310`](../../captures/session_20260903_154310.log) is RC1b (sealed suffix), not an RC1 revert.

**Prior — RC-D2 `resolveUs` 8 ms (measured only):**
[`122730`](../../captures/session_20260903_122730.log) OVERDUBBING `DFRAME resolveUs` min 2149 / median 8724 / max 15285 (16 of 31 ≥ 8000). Do not start an index. `#41` Stage 2 / off-clock seal stay out. Plan: [`display_draw_source_reuse_empty_bar_bugfix.md`](../Plans/display_draw_source_reuse_empty_bar_bugfix.md).

**Prior — Shorten same-start sibling drop ([`123236`](../../captures/session_20260903_123236.log), [`140729`](../../captures/session_20260903_140729.log)):**
Display Hide/Shorten apply drops a longer same-pitch+start closed sibling of the kept short row, including suffix remnants (`000402`). Device **PASS** for the original prefix-only slice: first STOPPED `src,frame` has no two-length pair (`63@0` reconstruct 144/576 → frame 144 only). Host `88d24b6c`. Not that RC: `77@336` frame 48 vs effective 16; `63@1680` frame 288 while effective omits it. Do not add gather `keepShortest`. Plan: [`overdub_paint_shorten_same_start_sibling_bugfix.md`](../Plans/overdub_paint_shorten_same_start_sibling_bugfix.md).

**Prior — RC-S retain+merge ([`120321`](../../captures/session_20260903_120321.log), [`122730`](../../captures/session_20260903_122730.log)):**
`Loop::retainCapturePreviewAtSeal` retains uncovered sealed rows, then merges this pass’s closed preview via `upsertDisplayNoteAtPitchStart`. Device **PASS**: wrap 2 `sealed` 63→159 (159 > lap-2 preview 106), `frame` 316→311, not visual+latest-lap (`225`). Live `hidden` vs stop `DIAG,oh hide` are different counters (122730 hidden=4 vs hide=1); not a reuse owner. Host `030e1a08`. Snapshot completeness is not a substitute.

**Prior — draw-source reuse RC-D3 ([`101721`](../../captures/session_20260903_101721.log), [`115237`](../../captures/session_20260903_115237.log), [`120321`](../../captures/session_20260903_120321.log)):**
Reuse copies `liveDisplayCommittedPrefix_` and applies overlays/pending on a frame-local copy. [`115237`](../../captures/session_20260903_115237.log) `hidden` 0→2 before wrap 1. `teensy41-capture-serial` already has `SESSION_CAPTURE_DRAW_SRC=1`. Display stays `resolveDisplayNotesByDrawSource`. Do not promote `liveDisplayNotes` to committed-source authority.

**Prior — occupy snapshot source ([DEC-052](../DECISION_LOG.md#dec-052--occupy-consumes-the-draw-source-predicate)):**
`rebuildOverdubSourceSpanCache` consumes `shouldGatherPublishedSnapshotForDrawSource`. Predicate true → exclusive published-`LoopSnapshot` fill (`from=snapshot`). Predicate false → prepared → prior → covering visual → passes unchanged. Plan: [`overdub_occupy_snapshot_source_refinement.md`](../Plans/overdub_occupy_snapshot_source_refinement.md). Native occupy fixtures in `test_overdub_source_view` PASS. `teensy41-capture-serial` RAM1 locals **6848**. Host `test_occupy_paint_serial_verify` PASS. Full native **1628/1630** — one pre-existing fail in `test_same_pass_playhead_tails_keep_closed_bar_four_end` (does not call occupy fill). Device: `overdub_occupy_paint` OD2 open `from=snapshot` when paint `snap!=0`; `occupy_od2_snap_not_zero` is paint/occupy divergence only. Stop totals emit `#CAP,DIAG,oh` so `add`/`shorten`/`hide` fit `kMaxCaptureTextBytes` (192); [`150143`](../../captures/session_20260902_150143.log) `overlap_hold` clipped `hide`. OVERDUBBING reconstruct stays off. Stage 4 stays closed. `#41` Stage 2 stays out.

**Prior — occupy prepared source pin ([`134815`](../../captures/session_20260902_134815.log)):**
`srcskip` `not_finished` because the LCR device gate is STOPPED-only and occupy-paint never idles STOPPED with committed passes (`sess=0 prep=0`). Opens were `from=visual`. [`113651`](../../captures/session_20260902_113651.log) `from=prepared,notes=8` is leftover LCR on a restamped finished session (`prep=3072` at OD1 open), not occupy-paint owner. Plan: [`overdub_occupy_prepared_source_incomplete_bugfix.md`](../Plans/overdub_occupy_prepared_source_incomplete_bugfix.md).

**Prior — occupy-paint HITL + second-overdub stale snapshot ([`092344`](../../captures/session_20260902_092344.log)):**
Preset `overdub_occupy_paint`: record seeds loop length (pitch 72). First overdub writes the half-bar phrase (48/52/56/60). Second overdub occupies that phrase: same-start 56 (Hide), +1/4 on 48/52 (Shorten + wrap), then −1/2 from that (+1/4 − 1/2 = −1/4 vs source) Hide + wrap. Host verifier is `verify_occupy_paint` (OD1 must not Shorten/Hide; occupy gates are OD2). Plan: [`overdub_paint_second_overdub_stale_snapshot_bugfix.md`](../Plans/overdub_paint_second_overdub_stale_snapshot_bugfix.md). Persist/`usbdisp` cadence stays a follow-up. OVERDUBBING reconstruct stays off. Stage 4 stays closed. RC2b stays parked. Source-view wrap-held [`013709`](../../captures/session_20260902_013709.log) stays a separate plan.

**Prior — source-view wrap-held wrong head Off ([`013709`](../../captures/session_20260902_013709.log)):**
Occupy `39@3024→448` at `hs=288` while storage WRAP is `3024→64`. Effective `39@288` len **2784**. A 4-event reconstruct does not emit `3024→448` (Off@64 blocked by intervening On@288; 3024 finishes to 3071). Pin the source-view merge / collapse / companion path next. Plan: [`overdub_source_view_wrap_held_wrong_head_off_bugfix.md`](../Plans/overdub_source_view_wrap_held_wrong_head_off_bugfix.md). Equal-tick consume shipped `932a58d6`.

**Prior — sealed survives current snapshot ([`012714`](../../captures/session_20260902_012714.log), recapture [`013709`](../../captures/session_20260902_013709.log)):**
`sealedCapturePreviewIsCovered` uses `visualCache.builtFromRevision` only. Sealed unions only while capture or the overdub session is active. `openOverdubSession` clears leftover record sealed. Keep-larger during a live session is reverted. Plan: [`overdub_paint_sealed_survives_current_snapshot_bugfix.md`](../Plans/overdub_paint_sealed_survives_current_snapshot_bugfix.md).

**Prior — stop gather keeps adopted linear ends ([`011906`](../../captures/session_20260902_011906.log)):**
`!captureActive` was too wide: wrap seal still has an overdub session, so gather fell back to record `visual=65`. Session flag stays. Keep-larger reverted after `012714`. Plan: [`overdub_paint_stop_snapshot_wrap_end_bugfix.md`](../Plans/overdub_paint_stop_snapshot_wrap_end_bugfix.md).

**Prior — closed occupy + stale snapshot paint ([`005439`](../../captures/session_20260902_005439.log)):**
Device PASS. Occupy frame `63→82`. Wrap 2 frame **131→132** with `snap=0`. Overdub stop published (`adopt_partial` **144**, DISP **157**). Log ends mid-`LoopPersist` (`SAVE,in_progress`) after capture Ctrl+C; no HardFault line. Plans: [`overdub_paint_closed_preview_suffix_hide_bugfix.md`](../Plans/overdub_paint_closed_preview_suffix_hide_bugfix.md), [`overdub_paint_stale_snapshot_after_wrap_bugfix.md`](../Plans/overdub_paint_stale_snapshot_after_wrap_bugfix.md). Reconstruct stays off. Stage 4 stays closed. RC2b stays parked.

**Prior — overdub paint capture handoff ([`234726`](../../captures/session_20260901_234726.log)):**
Reconstruct stays off. While capture is active, `gatherDrawSourceBars` unions `capturePreview` across the window. Plan: [`overdub_paint_capture_handoff_bugfix.md`](../Plans/overdub_paint_capture_handoff_bugfix.md).

**Prior — OD2 visual reconstruct gated during overdub ([`233642`](../../captures/session_20260901_233642.log)):**
Stage 3 `load_frame` is one phase, so VisualCache idle fitted during OVERDUBBING. DEC-045 gate is `allowVisualCacheReconstructSlice`. Device [`234726`](../../captures/session_20260901_234726.log) held `visual` **63** until stop `adopt_partial` **95**.

**Prior — display frame Stage 3 host shipped:**
[`display_frame_phase_split_windowed_paint_refinement.md`](../Plans/display_frame_phase_split_windowed_paint_refinement.md).
`DisplayManager::update` runs one paint phase per admitted LoadAndDisplay turn. Device [`233642`](../../captures/session_20260901_233642.log): `load_frame` **4.7–5.4 ms**; `DFRAME` wall p50 **12.8 ms**; transfer **4496 µs**.

**OpenSpec 4b.1 / 4b.2 / 4b.2c:** closed under Ring B round-robin
([`immutable-loop-snapshot-passes/tasks.md`](../../openspec/changes/immutable-loop-snapshot-passes/tasks.md);
[`playback_window_round_robin_admission_refinement.md`](../Plans/playback_window_round_robin_admission_refinement.md)).
**4b.1** pass = one owner per cursor turn (no true same-turn snapshot/persist
stack). **4b.2** pass = first-record start after leaving bar 1 + live append
on the five-owner cursor. **4b.2c** is closed: capture
[`232633`](../../captures/session_20260826_232633.log) shows one in-flight
record construction (`construction=1`, `queued=0`, `applied=0`, `exit=1`,
`inflight=1`) through PLAYING/OVERDUBBING, then the final `pending=0` turn
publishes `root=0x7030ad1c` with `root_rev=1`/`playback_rev=1`. No later
PLAYING/OVERDUBBING `snapshot_idle` occurs. The apparent violation was the
first-root construction interval, not stacked successor overdub passes.
`DEC-045` remains the historical baseline; `DEC-047` supersedes its post-root
successor restriction only for the activated `3.5`/`3.6` slice. **4b.3**
remains open; exclude `T0/S6` when
scoring post-stop gaps. A capture-only `track_update` profiler now reports
per-track maxima and over-budget counts from `TrackManager::updateAllTracks`;
attribution from [`000834`](../../captures/session_20260827_000834.log):
PLAYING/OVERDUBBING `tracks`/`usbclk` peak is **11.3 ms** (T4 11114 µs,
T6 6520 µs); post-stop `usbdisp` **116.7 ms** is `usbtrans` 116729 µs,
not `updateAllTracks`. RAM1 locals **4640**.

**Snapshot slab packing (`DEC-048`):** `allocateSnapshotShared` bump-allocates
into 32 KB slabs owned by `Loop`. One mutable span root and one event root
are reused per `LoopSnapshotConstruction`. Slab deny fails construction.
Native fixture `test_snapshot_one_bar_node_count_and_unchanged_membership_share`
covers one-bar node counts and successor membership sharing. Native
**1484/1484**. `teensy41-capture-serial` RAM1 locals **5920**.

**Active OpenSpec slice:** task `3.5` firmware is in tree on
`feature/overdub-wrap-snapshot-publication`. Settled wraps queue bounded
mutation-journal fragments; `LoopSnapshotConstruction` uses persistent bar
cursors and fixed-capacity membership pages (16 refs × 8 pages). NoteOff
pairing copies the NoteOn `NoteId` before promotion. PLAYING/OVERDUBBING may
step wrap journals after the first root. Failed fragments are retained.
Task `3.6` remains open: native parity extensions plus the measured
`T_RT_reserve` device gate. Capture
[`173826`](../../captures/session_20260827_173826.log) showed successor
construction after the first wrap (`construction=2`, `root_rev=1`) but
`queued`/`applied` are pass-id fields and cannot tell wrap-journal fragments
from pass reconstruction. `snapshot_idle` now also emits `journals`,
`fragment=i/n`, and `parent_rev` (published revision when the front journal
parent is the selected root; `0` when the parent is an unpublished
candidate). RAM1 admission is restored without USB-host ownership changes:
`teensy41-capture-serial` now reports RAM1 locals **7008** (variables 91296,
code 420652, padding 5332). The follow-up admission fix now allows queued
session-exit snapshot work after the wrap journals drain, and allows the
bounded Snapshot owner to run when tracks are stopped but the transport clock
is still running. Focused native admission coverage passes; full native and
device recapture remain open. Construction now persists membership-copy,
mutation, append, and event-sort progress at one fixed-capacity membership
page per admitted unit; native snapshot/take-survival coverage passes and the
device recapture [`202635`](../../captures/session_20260827_202635.log) still
fails `T_RT_reserve`: `snapshot_idle` reached 79.169 ms because session-exit
start reconstructed a whole queued pass in one admitted unit. Existing journal
caps now also bound that start: `startNextPendingConstruction` walks committed
pass events through a persistent `CommittedEventRange` cursor and begins at
most one journal-capped change fragment. Capture
[`204136`](../../captures/session_20260827_204136.log) then published that
wrap candidate as soon as `journals=0` while `queued=3` session-exit passes
remained, so playback selected an incomplete snapshot and dropped the first
record pass. Wrap completion now keeps the candidate unpublished until queued
passes, construction progress, and companion changes complete. Device
recapture remains open.

**Prior — display watches parked after [`204043`](../../captures/session_20260901_204043.log)**
Not Now implementing. Reopen only if a later capture matches a gate below.
Closed on `204043` (do not reopen): DEC-051 seal-at-0; stop hide re-admit
of `63@768→1151`; wrap-held paint from the flatten `WRAP` scanner;
snapshot-current hide seam; same-start occupy vs reconstruct stacks.
Parked gates:
| Watch | Reopen if a future capture shows | Plan |
|------|----------------------------------|------|
| RC2b wrap-held rewrite | `draw_src` / frame turns a closed same-`NoteId` into wrap-held (`2296→672`, `2304→2976` / `576` / `1008`) | [`overdub_wrap_companion_and_pairing_bugfix.md`](../Plans/overdub_wrap_companion_and_pairing_bugfix.md) |
| `79@240` dual Off | compose/effective end **272** and drawn/frame end **288** with `SEVT` `F@272` and `F@288` | same + [`OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md`](../Guides/OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md) |
| Stopped-frame miss | `69@3024` (or another pitch+start) in `SEVT` + effective, absent from stopped frame | [`overdub_occupy_same_start_stop_hide_readmit_bugfix.md`](../Plans/overdub_occupy_same_start_stop_hide_readmit_bugfix.md) |
| Held-note retention | pitch-64 (or equivalent) live hold 40/48 vs MIDI ~624 after seal | [`display_seal_handoff_capture_retention_bugfix.md`](../Plans/display_seal_handoff_capture_retention_bugfix.md) |
Do not patch reconstruct pairing, `applyCapturePlayheadTails` from the red
host pin, or `keepShortestEndAtPitchStart` on the draw-source gather unless
a gate above fires.

**Prior — same-start occupy paint is last overdub + occupy, not a remnant**
Closed as intended on [`204043`](../../captures/session_20260901_204043.log).
Reconstruct still stacks every same-start length (occupy needs both
`noteId`s). Frame/effective follow the last COORD hold, then occupy
Shorten/Hide: `63@0` last COORD `0→576`, later `336→576` Shortens to
exclusive end − 1 = **335**; `768→95` is the same rule (`864−1`).
`1104`/`1536`/`2304`/`69@48` keep the last overdub length (240 / 232 /
240 / 48), not reconstruct's shorter sibling. Do not reopen to force
`0→192` or keep-shortest.

**Prior — same-start Hide must not re-admit the record row at overdub stop**
Device **PASS** [`204043`](../../captures/session_20260901_204043.log):
frame `63@768` len 95; no post-stop `draw_src`; after snapshot-current
`DFRAME` stays 81. Host fixtures shipped. Plan:
[`overdub_occupy_same_start_stop_hide_readmit_bugfix.md`](../Plans/overdub_occupy_same_start_stop_hide_readmit_bugfix.md).

**Prior — DEC-051 overdub wrap seals at loop end ([`193232`](../../captures/session_20260901_193232.log))**
Stage 1 host clock shipped. Device **PASS**
[`201956`](../../captures/session_20260901_201956.log) and
[`204043`](../../captures/session_20260901_204043.log) (wraps at phase 0,
no `WRAP,2304,96`, no `DNTE 0→3072`, SEVT 125/125 linear pairs).
Stage 2 stays gated (no `0→L`). Plans:
[`overdub_wrap_bar4_note_off_session_seal_bugfix.md`](../Plans/overdub_wrap_bar4_note_off_session_seal_bugfix.md),
[`overdub_wrap_loop_end_commit_refinement.md`](../Plans/overdub_wrap_loop_end_commit_refinement.md).

**Prior — overdub wrap RC2b ([`192435`](../../captures/session_20260901_192435.log))**
RC5b device **PASS**: first **U:** `Overdub undone`, `DISP` 75→63. Classic
`2296→672` was not in [`204043`](../../captures/session_20260901_204043.log).
Host tails pin stays **red** (`2976` → `2296`); no tails firmware from that
pin. Parked — reopen only on the capture gates in the parked-watches table
above. RC3 overlay re-admit is closed on `204043`. Plan:
[`overdub_wrap_companion_and_pairing_bugfix.md`](../Plans/overdub_wrap_companion_and_pairing_bugfix.md).

**Prior — display seal handoff RC1 (`153517` coverage over-claim)**
Idle OverlayRejected fallback was stamping `visualCache.builtFromRevision =
playbackRevision` while the gather still carried the stale snapshot's content
(8 vs 10). Retention released and the painter flashed previous-pass notes.
`visualCacheGatherCoversPlaybackRevision` forbids that stamp; `slice_keep` also
no longer claims coverage. Release predicate unchanged.
Committed `c74b8e21`.
Plan: [`display_seal_handoff_coverage_and_wrap_pairing_bugfix.md`](../Plans/display_seal_handoff_coverage_and_wrap_pairing_bugfix.md).

**Prior — display seal handoff (capture retention)**
Recapture [`150650`](../../captures/session_20260901_150650.log) confirms the wrap
publication fix: `root_rev` catches `playback_rev` within 150–220 ms at every wrap
(1, 3, 5, 7, 9, 11, 13, 15, 17), phase-0 holds resolve (`hs=0` reports `a=1`,
`as=0`, `ae=576` from the second pass), and `DIAG,draw_src,diff` is empty. The
STOPPED frame still differs from effective on exactly the two held pitch-64 rows:
frame `s0 len40` / `s768 len48` against effective `624`/`624`, where MIDI input
gives NoteOn storage 0 → NoteOff 624 and NoteOn 768 → NoteOff 1392.

Root cause is a **capture → committed-tier lifetime gap**, not cache arbitration:
the cache's last rebuild before the frame was `slice_clean` at `52023305`, four
seconds before those notes existed, and there is no `slice_keep` in the capture.
The pass sealed at `56135261`, the frame was painted at `56215949`, and `root_rev`
17 published at `56288500` with the covering slice at `56311186`.
`commitPendingCapturePass` cleared `capturePreview` at the seal, so the rows left
the Capture tier before either committed tier carried them.

`Loop` retains closed preview rows at the seal
(`retainCapturePreviewAtSeal`) and releases them only when the committed
representation the painter selects proves coverage
(`sealedCapturePreviewIsCovered`): only `visualCache.builtFromRevision` proves
coverage. A current published snapshot is not membership (`012714` wrap 2).
A later wrap must not `clear()` uncovered sealed rows (`120321`); retain
keeps them and merges the new closed preview.
`resolveDisplayNotesByDrawSource` unions the retained rows into the committed
prefix through `upsertDisplayNoteAtPitchStart`, which replaces only at equal
pitch+start and never acts as a general newer-wins rule.
`keepShortestEndAtPitchStart` is untouched. Retention is a lifetime refinement of
the existing Capture tier: `Loop` owns the rows, display only consumes them,
`VisualCache` only reports coverage, and `DrawSourcePath` is unchanged.
Plan: [`display_seal_handoff_capture_retention_bugfix.md`](../Plans/display_seal_handoff_capture_retention_bugfix.md).
Host gates: `teensy41-capture-serial` RAM1 locals **6848**, native **1566/1566**.
Parked — [`204043`](../../captures/session_20260901_204043.log) has no pitch-64
holds. Reopen only if a later capture shows the held-note retention gate.

**Prior — overdub wrap snapshot publication (tasks `3.5`/`3.6`)**
Capture [`134803`](../../captures/session_20260901_134803.log) proved
occupancy converges (`src,reconstruct` 13 pitch-64 rows → `src,effective` 4:
`0→576`, `768→576`, `1536→512`, `2304→576`) while the drawn frame kept the
raw 144-length rows, because `root_rev` stayed **1** against `playback_rev`
3…11 for the whole overdub. `remainingSessionExitPublishWork` gates wrap
publication on `companionsApplied`, which only ever becomes true on the
session-exit path, so no wrap candidate could ever publish.
`remainingWrapPublishWork` now scopes the companion condition to
`sessionExitQueued`, and each wrap journal carries the `targetRevision`
stamped by `recordLastOverdubSeal` so a candidate is never selected for a
revision it does not contain. Host gates: `teensy41-capture-serial` RAM1
locals **6848**, native **1559/1559**. Device recapture open.

**Prior — snapshot-first bar painter (`DEC-050`)**
Occupancy resolves at NoteOn into `pendingNoteChanges_`
(`resolveOccupancyAtNoteOn`). Paint-only `pendingPaintHides_` is removed.
`upsertSourceTransform` is monotonically restrictive. Deferred stored-MIDI
verification emits `DIAG,dnte,src,effective` after raw reconstruct. Stage B
paint uses `resolveDisplayNotesByDrawSource`: Snapshot when
`publishedSnapshotIsCurrent`, otherwise VisualCache, plus Capture union on
live bars; pending transforms then playhead tails. Stage C logs
`DIAG,draw_src,diff` against the old compose path and is read-only — it saves
and restores compose frame bookkeeping around the comparison. Compose is not
deleted yet. Open on the VisualCache tier: `keepShortestEndAtPitchStart`
(idle slice and wrap adopt) keeps the shortest dual-storage sibling, so that
tier still contradicts effective geometry when the snapshot is not current.

**Prior — visual cache wrap-stale (Stage 1)**
([#41](https://github.com/Lytrix/MidiLooper/issues/41),
[`display_visual_cache_snapshot_reuse_refinement.md`](../Plans/display_visual_cache_snapshot_reuse_refinement.md)):
Stage 1 device PASS
[`202745`](../../captures/session_20260831_202745.log). Three wraps on a
7-bar loop (`live=5376`). No wrap `VCACHE,stale` with `dsz=0` while
`notes>0`. Only `lcr,src` is `why=open,from=visual` at overdub start. First
wrap `DISP` `183,111` → next frame `184,180`. Second wrap first paint
`249,246` (not a hold of 180). Contrast [`201556`](../../captures/session_20260831_201556.log)
`visual=206` / `frame=134`.
**Stage 1b — wrap-pair length:** [`202745`](../../captures/session_20260831_202745.log)
`DNTE` after `OVERDUBBING → STOPPED` shows the same chords at three
lengths (39/45/51 @ 240 as 208, 256, and 304). NOTE_EDIT paint reconstructs
with wrap pairing off and drops the extras. Visual-cache merge of a sealed
wrap pass now uses merged reconstruct; `appendOverdubPassWrapPairedNotes`
skips a longer sibling of an existing pitch+start. Device recapture is
required before calling 1b closed. Native **1514/1514**. RAM1 locals **6944**.
**Stage 1c — wrap-held display:** [`204132`](../../captures/session_20260831_204132.log)
second wrap occupy already has `39@192–592` and `39@336–592`. STOPPED DNTE
is short (`192/16`, `336/112`). NOTE_EDIT open
`rebuildVisualCacheFromPasses` then `VCACHE,full` 291 notes and focus
`presence=0`. Visual-cache wrap append is wrap-held heads/tails only.
Source view keeps full wrap pairing.
**Stage 1d — no wrap-tick visual merge:** shipped `d35d64a7`. Device FAIL
[`205917`](../../captures/session_20260831_205917.log): wrap 2 idle
`slice_clean` `dsz=0` while longs stay in `visualCache` (`DISP`
`243,243`). Overlay fill is not current committed.
**Stage 1e — idle-slice current fill:**
[`display_visual_cache_idle_slice_current_fill_bugfix.md`](../Plans/display_visual_cache_idle_slice_current_fill_bugfix.md).
`rebuildVisualCacheIdleSlice` discards Overlay (`flat` only; no fallback
reset) and fills from prepared LCR or budgeted
`gatherCommittedEventsInWindow`. Playback Overlay contract unchanged.
Native **1518/1518**. RAM1 locals **6944**. Device recapture required
(`205917` shape: wrap 2 first neighborhood must match committed; wrap 3
must not be the first shorten).
**Stage 1f — STOPPED paint current fill:**
[`display_visual_cache_stopped_paint_current_fill_bugfix.md`](../Plans/display_visual_cache_stopped_paint_current_fill_bugfix.md).
`resolveDisplayNotesCommitted` last-resort discards Overlay and falls
through to `gatherCommittedEvents`. Playback Overlay and
`rebuildDisplayNotesInWindow` unchanged. Native **1519/1519**. RAM1
locals **6944**. Device FAIL
[`220337`](../../captures/session_20260831_220337.log): first 8-bar
STOPPED `DISP` **276 / 344** (no `paint_src`); wrap/stop
`stale_range` is `dcnt=8` on an 8-bar loop; PLAYING Snapshot idle then
**334 / 334**. Third stop `OverlayRejected` then **80 / 91**. Play is
still the first match.
**Stage 1g — wrap dirty span:**
[`display_visual_cache_wrap_dirty_span_bugfix.md`](../Plans/display_visual_cache_wrap_dirty_span_bugfix.md).
`markTickSpanDirty` marks wrap-held (`endTick < startTick`) On/Off bars
only. Linear pass spans stay inclusive. Native **1521/1521**. RAM1
locals **6944**. Device [`221925`](../../captures/session_20260831_221925.log):
4-bar loop, wrap `dcnt=4` from linear 39/45/51 in every bar (not a
wrap-interior fail). Wrap 1 `slice_clean` `63→102`, wrap 2 `102→124`.
Wrap 3 idle `OverlayRejected`/`src,None` — no `slice_clean`. First
STOPPED `DISP` **128 / 137** (no `paint_src`). Stop `overlap_hold`
`hide=8`. `DNTE` stacks `39@48` and `39@432` at **144 and 256**. MIDI 47
(B2) is absent. 1g does not own hide or wrap-pair length.
Recapture [`222349`](../../captures/session_20260831_222349.log) (same 4-bar
loop, later overdub): B3 is the only pitch held across stop (`MI` ON
`325318691`, seal `325433453`, OFF `325506187`); `CLN,wrap_synth,0`.
Each wrap first `DISP` drops (`102→67`, `119→101`, `135→121`, `157→136`,
`162→150`) then `slice_clean` restores. First slot0 STOPPED `DISP`
**159 / 31**; `paint_src,OverlayRejected` then **159 / 166**. No `DNTE` /
`SEVT` / `WRAP` dump in this file — length-to-loop-end is not in the log.
**Stage 1h — held-note wrap pair:**
[`display_visual_cache_held_note_wrap_pair_bugfix.md`](../Plans/display_visual_cache_held_note_wrap_pair_bugfix.md).
`appendCanonicalSpansFromMidiRange` stack fallback assigns `Off@0` only
when the last On is wrap-held (`startTick >= tailStart` and
`isPreferredWrapTailForHeadOffInSpanBuild`). Mid-loop B3 `1856` keeps
`F@1912` / `F@1920`. `finalizePendingNotes` still closes at playhead.
Native **1523/1523**. RAM1 locals **6944**.
**Stage 1i — wrap/stop first paint:**
[`display_visual_cache_wrap_stop_first_paint_bugfix.md`](../Plans/display_visual_cache_wrap_stop_first_paint_bugfix.md).
Dirty overdub layer holds before snapshot window gather. STOPPED assigns
dirty nonempty `visualCache` when the composed handoff is gone (slot
mismatch). Last-resort Overlay reject stays for empty cache.
Native **1525/1525**. RAM1 locals **6944**. Device FAIL
[`230400`](../../captures/session_20260831_230400.log): wrap 1 `105→66`
(cache 63). Hold is committed-prefix only; sealed capture is gone. Next: 1k.
[`225243`](../../captures/session_20260831_225243.log) first STOPPED
**147 / 198** kept RC5a handoff (same slot); the STOPPED hold did not run.
**Stage 1j — stop handoff playhead tail:**
[`display_visual_cache_stop_handoff_playhead_tail_bugfix.md`](../Plans/display_visual_cache_stop_handoff_playhead_tail_bugfix.md).
`applyCapturePlayheadTails` writes `liveDisplayPendingPaintNotes_` only.
`liveDisplayNotes` stays at capture-preview lengths so RC5a does not freeze
`endTick = currentTick` (`225243` D#2 ON@624 → stop phase 704). Unmatched
`N@2928` / `F@0` → `loopLength` is a later reconstruct RC.
Native **1526/1526**. RAM1 locals **6944**.
Device [`231953`](../../captures/session_20260831_231953.log) dump gate
PASS: `DIAG,dnte,src,reconstruct,count,221`, `src,frame,count,160`,
`WRAP` 144, `SEVT` 707. First STOPPED **160 / 148** matches the frame
dump. Stop `COORD` storage **1168**; frame dump has no note ending at
1168 (1j playhead freeze not on that frame).
**Stage 1k — wrap compose hold:**
[`display_visual_cache_wrap_compose_hold_bugfix.md`](../Plans/display_visual_cache_wrap_compose_hold_bugfix.md).
Grow compares cache to `max(committedCount, composed liveDisplayNotes)`.
**Stage 1l — Off@0 leftover tail pair:**
[`display_visual_cache_off_zero_tail_pair_bugfix.md`](../Plans/display_visual_cache_off_zero_tail_pair_bugfix.md).
Leftover tail-window On + leftover `F@0` wrap-splits to `L-1` (not
`tryPairWrappedTailOn`). Mid-loop linear pairs stay linear (1h).
**Stage 1b / occupy hide on hold (`003616`):**
[`overdub_occupy_hide_on_hold_current_tick_bugfix.md`](../Plans/overdub_occupy_hide_on_hold_current_tick_bugfix.md).
Occupy On / playback cross upserts a **paint-only** Hide
(`pendingPaintHides_`) so the earlier note drops on the currentTick frame.
**Wrap-seal RC (`005757`):**
[`overdub_wrap_seal_occupy_hide_deletes_shorten_bugfix.md`](../Plans/overdub_wrap_seal_occupy_hide_deletes_shorten_bugfix.md).
`commitOverdubWrapAtSessionStart` must not seal occupy Hide as Delete.
OverlapNoteOff accumulate is Shorten / Length again. NOTE_EDIT
`OverlapNoteOff` stays Shorten. Parent unique-key revert:
[`overdub_occupy_same_start_shorter_hides_longer_bugfix.md`](../Plans/overdub_occupy_same_start_shorter_hides_longer_bugfix.md).
**NoteOff OverlapNoteOn (`010926` RC1):**
[`overdub_wrap_occupy_noteoff_shorten_not_delete_bugfix.md`](../Plans/overdub_wrap_occupy_noteoff_shorten_not_delete_bugfix.md).
`accumulatePendingNoteChangesFromSourceNotes` Shortens OverlapNoteOn to
occupy exclusive end − 1. CompleteCover still Hides / Deletes.
**norowid paint + wrap cache overlay (`010926` RC2):**
`upsertPendingHidesForOccupiedNoteIds` falls back to present-at-hold
source-view rows when ledger ids have no source-view row. Idle slice,
`adopt_partial`, wrap append, and grown-cache assign apply pending paint
so wrap first `DISP` does not restore occupied rows. Focused native
pending **72/72**. Native **1539/1539**. RAM1 locals **6848**. Device
recapture required (`010926` shape).
**NoteOn paint + capture mirror (`104129` / `110022` / `111343`):**
[`overdub_occupy_on_noteon_paint_immediate_bugfix.md`](../Plans/overdub_occupy_on_noteon_paint_immediate_bugfix.md).
Geometric present-at-hold fallback when ledger occupy is empty (`n=0`).
Over-budget overdub resolve still mirrors `capturePreview` and playhead
tails, including after a committed-layer rebuild. Wrap adopt: paint Hide
on committed prefix only; same-start dedupe does not drop longer live
capture over shorter committed sibling. Compose counts clamp to the
vector after hide/dedupe; `liveDisplayNotes.resize` never grows
(`111343` `DNTE,0,0,0,0`). Overdub playhead tails stay on over-budget
frames (`112806`). Native **1544/1544**. RAM1 locals **6848**.
Device recapture required. Stop reconstruct still shows unshortened
first-overdub E4 `760–1920` beside later `768–144` (`overlap_hold`
`shorten=0`).
**Stage 1m — stop compose adopt (`000013`):**
[`display_visual_cache_stop_compose_adopt_bugfix.md`](../Plans/display_visual_cache_stop_compose_adopt_bugfix.md).
Other tracks STOPPED first and cleared `liveDisplayNotes` (141→31). Adopt
compose onto the outgoing loop when it is larger than that loop's
`visualCache` on a short loop. Wrap-hold adopts so cache tracks compose
(wraps 8–11). Full-loop idle must not replace a larger cache with a
smaller slice (`slice_keep`, Prepared 95→80). Occupy `hide=2` is not this
slice. Device recapture: first STOPPED on the overdub loop ≥ last
OVERDUBBING compose; later PLAYING must not fall to 80; cache must not
freeze at 95 while compose grows.
Native **1537/1537**. RAM1 locals **6848**. Occupy hide is in tree
(source-view reconstruct keeps both `noteId`s; adopt/idle keep the shortest
display end). Device recapture required for [`002145`](../../captures/session_20260901_002145.log)
`norow` 75/76 and pitch-63 frame 96+288.
Device [`002145`](../../captures/session_20260901_002145.log) (4-bar `len=3072`):
**1k PASS** — every wrap first OVERDUBBING `frame` ≥ last-before (odub1 wrap 1 `71→72`; odub2 wrap 1 `94→95`).
**1m PASS** — odub1 last `88/80` → `adopt_partial` **88** → first STOPPED **88/88**. Odub2 last `145/144` → `adopt_partial` **145** → first PLAYING **145/145**. Cache tracks compose (`88→94→103→113→120→128→136→144`). Settled STOPPED **145**, not 80. Global stop still paints other slot-0 loops (`32`, `42`, `50688/1429`) then returns to **145**.
**1b** reconstruct **PASS** (0 same-start stacks, count 73). Frame dump **FAIL** on that firmware: pitch **63** at `0/384/768/1152/1536/1920/2304/2688` still has lengths **96 and 288** (frame 88).
**1l not in this log** — no `SEVT` `F@0`.
Stage 2 is unblocked and not started. Do
not start all-track idle fan-out,
`T4 track_update`, N / pages knobs, or OpenSpec `3.5` / `3.6` checkoff.

**Shipped — live-record skip parent note lookup**
([`live_record_snapshot_start_skip_parent_note_lookup_bugfix.md`](../Plans/live_record_snapshot_start_skip_parent_note_lookup_bugfix.md)):
`fromCaptureStore` fragments do not call `findNoteAtPitch`. `openNotes` is
the sole pairing mechanism; the parent is not an identity source. Session-exit
keeps the lookup. Device [`192422`](../../captures/session_20260831_192422.log):
32 live-record starts, `note_lookup=0`; after-first `start` 167–233 µs.
The [`190654`](../../captures/session_20260831_190654.log) lookup-driven
`start` growth (375→1007 µs) is gone.

**Shipped — live-record snapshot start bound**
([`live_record_snapshot_start_bounded_fragment_bugfix.md`](../Plans/live_record_snapshot_start_bounded_fragment_bugfix.md)):
RECORDING `startNextPendingConstruction` walks `capture.store` from
`appliedLiveRecordEventIndex` through `tick < includeUntil` and emits one
journal-capped fragment via `appendNextPassChangeFragment`. No full-store
`copyEventsTo` / reconstruct. `openNotes` persist across fragments and bars.
Row-22 admission stays one start XOR one step. Native take-survival live-record
fixtures pass. Native **1507/1507**. RAM1 locals **6944**. Device non-growth
capture is required before calling the RC closed. Do not freeze
`T_RT_reserve`. Do not raise N.

**Shipped — overdub-stop incremental admission (`DEC-049`)**
([`overdub_stop_incremental_admission_bugfix.md`](../Plans/overdub_stop_incremental_admission_bugfix.md)):
`stopOverdubbing` reanchors traversal and keeps a still-valid full-loop
merged stream. The next `playCommittedLoopMidi` may admit the Rank-6 seal
receipt incrementally. `playback_build` labels `snapshot` / `overlay` /
`compatibility` from the branch actually taken. While a publication
backlog is present, Snapshot idle is 2 of every 6 turns. Native **1503/1503**. RAM1 locals **6944**.
Device [`152513`](../../captures/session_20260831_152513.log): T4 16-bar
Stage 2 PASS. Device [`153113`](../../captures/session_20260831_153113.log):
T4 11-bar Stage 3 drain PASS — 16.197 s after the last stop,
`pending=0` / `root=0x703fadf0` / `root_rev=7` / `playback_rev=7` /
`publish=9`, then `playback_build,snapshot`. Device test: Snapshot idle
passes `stepPendingSnapshotPublish(2)`
(`MidiServiceDrain::kSnapshotIdleConstructionUnitsPerTurn`). Recapture
[`161532`](../../captures/session_20260831_161532.log): T4 16-bar, last
stop `incremental` `rev=8`, then 1.824 s / one `BAR` to
`pending=0` / `root_rev=8` / `playback_rev=8`. That drain already had
`root_rev=1`; it is not the empty-root 16.197 s class of
[`153113`](../../captures/session_20260831_153113.log). `start+step`
same-turn count is 61; parent max **13.350 ms** (`start=12891` +
`step=423`); 7 parents ≥ 8.82 ms. Drain-window parent max **4.371 ms**.
Device test now: `stepPendingSnapshotPublish(2)` and
`kSnapshotMembershipMaxPages = 16`. Recapture
[`165637`](../../captures/session_20260831_165637.log) (N=2, 16 pages):
T4 16-bar, last stop `incremental` `rev=9`, then **1.885 s** to
`pending=0` / `root_rev=9` / `playback_rev=9` with `root_rev=1` already
present. `start+step` = 58; parent max **12.138 ms**; 3 parents ≥ 8.82 ms.
Drain parent max **4.413 ms**. Compare
[`153113`](../../captures/session_20260831_153113.log) (N=1, 8 pages),
[`161532`](../../captures/session_20260831_161532.log) (N=2, 8 pages),
and [`164448`](../../captures/session_20260831_164448.log) (N=1, 16 pages).
`164448` + tail [`165027`](../../captures/session_20260831_165027.log):
T4 is 16 bars / 12288 ticks from full-loop `incremental`. `start+step` = 0.
Last stop `rev=13` @ 559.185 s; publish @ 608.693 s (**49.508 s**)
`pending=0` / `root=0x70498e30` / `root_rev=13` / `playback_rev=13` /
`publish=8`. `165027` has a 40.114 s gap with no `snapshot_idle` between
`journals=13` `fragment=9/18` and `journals=0` `applied=3`. Drain parent
max 4.330 ms in `164448`; tail parents ≤ 0.493 ms. 4 live-record parents
≥ 8.82 ms (start only, max 10.887 ms). ExecutionBounds still documents 8.
Do not freeze `T_RT_reserve`. Do not check off OpenSpec `3.5`/`3.6`.

**Shipped — wrap-tick derived rebuild**
([`overdub_wrap_derived_rebuild_clock_interval_bugfix.md`](../Plans/overdub_wrap_derived_rebuild_clock_interval_bugfix.md)):
Device recapture [`135510`](../../captures/session_20260831_135510.log):
wrap child line `total=41851,seal=36025,pass_apply=676,src_append=5150`;
`track_update[4]=41902` in that window (not the [`005527`](../../captures/session_20260828_005527.log)
77.618 ms class); no `why=wrap`; Rank 6 gather `playback_build` 7547 µs at
`57711324` is the next-tick owner this slice removes. Native **1496/1496**.
RAM1 locals **6944**.

**Immediate Stage 1**
([`snapshot_publication_timing_bugfix.md`](../Plans/snapshot_publication_timing_bugfix.md)):
`MidiServiceDrain::allowSnapshotIdleStep()` now admits pending session-exit,
in-flight, or wrap-journal Snapshot work in `STOPPED` even when
`visualCacheNeedsFirstPaint` is true. `STOPPED` ends active MIDI ownership;
it does not mean pending snapshot work is complete. Native policy fixture
`test_stopped_pending_snapshot_wins_over_first_paint` covers that
combination. Device
recapture [`214832`](../../captures/session_20260827_214832.log) does **not**
close Stage 1: wrap journals drained with `queued=4` and `root=0x0`, then
STOPPED first-paint `VCACHE` ran and no `STOPPED` `snapshot_idle` appeared.
[`215420`](../../captures/session_20260827_215420.log) also holds completeness
(`root=0x0`) and also has zero `STOPPED` `snapshot_idle` across a 16 s hold;
that hold is not first-paint (`VCACHE` `slice_clean` `notes=166` `dirty=0`
before stop, no post-stop `VCACHE`).
[`215845`](../../captures/session_20260827_215845.log) repeats that on a
120 s STOPPED hold: last PLAYING idle still `pending=1` `queued=3`
`journals=2`, then persist/`idle_maint` only, no `STOPPED` `snapshot_idle`.
STOPPED Snapshot admission now scans every slot and selects a track with
pending publication instead of the visual idle cursor. Device recapture
[`221054`](../../captures/session_20260827_221054.log) **closes Stage 1**:
first STOPPED `snapshot_idle` at +0.070 s, candidate stayed `root=0x0` until
+77.765 s, then `pending=0` published `root=0x70400ac8` / `root_rev=10`.

**Immediate Stage 2 leftover named**
([`snapshot_publication_timing_bugfix.md`](../Plans/snapshot_publication_timing_bugfix.md)):
Device recapture [`225659`](../../captures/session_20260827_225659.log)
closes the 224842 leftover: record-stop PLAYING idle at 30.164 s is
**20.984 ms** with `discard=15262` (`discardPendingIfLoopLengthChanged`),
`start=5704`, `enqueue=5`, exclusive leftover **13 µs**. Exclusive leftover
after `enqueue`/`discard` is ≤ 39 µs on every paired row. `enqueue` max is
14 µs. `emit` max is 52 µs and sits after the parent `us` stamp.
STOPPED session-exit `start`/`pass_change` reaches **19.996 / 19.968 ms**.
`step` max is 5.992 ms. Completeness held (`root=0x0` until the final
`pending=0` row). Publication closed at 183.121 s:
`root=0x70431844` / `root_rev=8` / `finish=671` / `publish=10`.
First STOPPED idle +0.066 s. Child-line budget held (31701/31701 complete
child rows). The Stage 2 falsification review is recorded in the plan.
Session-exit length handoff device PASS
[`003015`](../../captures/session_20260828_003015.log): first PLAYING
idle after record stop is `discard=47` (was `15262` on
[`225659`](../../captures/session_20260827_225659.log)). The candidate
stayed `root=0x0` until `pending=0` / `root=0x703248cc` / `root_rev=1`.
RECORDING used 2669 idles / 234 ms CPU, not a prefix rebuild. Native
fixtures cover the 9216/21504 fit pair and the applied-ticks overflow
discard. Host gates after inner timings: capture-serial RAM1 locals
**6944**. For
`startNextPassChangeFragment`, device recapture
[`004119`](../../captures/session_20260828_004119.log) names the 21 ms
STOPPED session-exit as `note_lookup=18293` on `pass_change=21050`
(86.9%). Every ≥10 ms fragment is 82–88% `LoopSnapshot::findNote`.
Caller-scoped pitch lookup device PASS
[`005527`](../../captures/session_20260828_005527.log): `note_lookup`
max **857 µs** (was 18293 on
[`004119`](../../captures/session_20260828_004119.log)); `pass_change`
max **3.219 ms**. Tail
[`005819`](../../captures/session_20260828_005819.log) publishes
`pending=0` / `root=0x70413fec` / `root_rev=10`. Same-pitch scan and
refill remain. No `step` rewrite is
justified yet. Host gates: capture-serial RAM1 locals **6944**; native
**1495/1495**.
Per-turn reserve measurement remains necessary, but freezing the minimum
observed reserve is rejected as a guarantee. Do not remove the +16
live-record bars. Do not check off OpenSpec `3.5`/`3.6`.

Direct consumer migration, persistence, and snapshot-identity undo remain
later phases. Decision:
[`DEC-047`](../DECISION_LOG.md#dec-047--bounded-overdub-wrap-snapshot-publication).

**Separate capture finding:** [`214703`](../../captures/session_20260826_214703.log)
confirmed `CLN,overdub,wrap_synth,3` after `finalizePendingNotes` reported no
pending notes/off events. Stored data contained existing head offs such as
`F@112` plus synthetic `F@6143` (`loopLengthTicks - 1`) for the same notes.
Track this in [`overdub_wrap_note_off_capture_bugfix.md`](../Plans/overdub_wrap_note_off_capture_bugfix.md);
do not fold it into 4b.2c.

**Immediate slice (side track):** Slice 1A measure gate **FAIL** on
[`213537`](../../captures/session_20260826_213537.log). Decomposition
[`playback_window_prepare_algorithm_decomposition_investigation.md`](../Plans/playback_window_prepare_algorithm_decomposition_investigation.md)
**D1** is **closed** — native + device PASS
[`221655`](../../captures/session_20260826_221655.log): switch track 0→1
while `chan resume=2560`, **0** `reset,dirty`, resume `chan 2568` on return,
then `Complete` at 178.738 s. Next LCR-prepare step would be **D2 design**,
but maintained-LCR prepare is not the snapshot north star; do **not** start
D2 firmware or 1B unless re-authorized. Parent remains
[`playback_window_build_owner_boundary_refinement.md`](../Plans/playback_window_build_owner_boundary_refinement.md)
([#38](https://github.com/Lytrix/MidiLooper/issues/38)).

G1 sizing is **satisfied** (doc-only). `representativeRebuildWall` = 358 µs
from [`202654`](../../captures/session_20260826_202654.log) `T4/S0`
`path=snapshot`; `prefetchTicks` = **272**; horizon stays playhead-centred
(`H_fwd` = 768). `T0/S6` compatibility is excluded. Slice 4 / Ring A firmware
is not authorized. Residual **4b.1b** (indivisible
`NoteUtils::reconstructDisplayNotes` in `Loop::rebuildVisualCacheIdleSlice`)
stays **parked**. Do not make a cooperative reconstruct state machine.
STOPPED/PLAYING post-overdub long-tail stays out of scope.

Ring B/RC2 shipped: five deferred owners rotate through one unconditional
cursor turn (snapshot step, visual-cache slice, wrap append, deferred
persistence, load/display). Visual-dependent turns skip only while wrapped
append is pending. RC2 refuses uncovered visual-cache reads on the overdub
paint path and falls back to committed-pass reconstruction when a dirty cache
cannot cover the overlap source span. Device PASS
[`183452`](../../captures/session_20260826_183452.log) / reconfirm
[`185141`](../../captures/session_20260826_185141.log): wrap `from=passes`,
`DISP` 286→466 / 271→437. First-overdub `midi_gap` 222.596→146.802 ms vs
[`175350`](../../captures/session_20260826_175350.log); later steady 24.325 ms
vs 366 ms. Reconstruct timings for 4b.1b live on the owner-boundary plan.

OpenSpec **4b.2** remains the first-take `loopLengthTicks` behavior:
snapshot idle starts from `capture.store` only after the playhead has left bar
1, then appends each later completed bar when the unpublished parent still has
tick room. Live RECORDING does not rebuild the settled prefix when the parent
is too short; the bar still being written is not included.
[`114012`](../../captures/session_20260826_114012.log) started at
`record_length=768` while bar 1 was still being written.
[`120020`](../../captures/session_20260826_120020.log) started after bar 1
and did not restart `snapshot_idle` during RECORDING; `DISP` still jumped
on every quantized `record_length` step. First-take display length stays
the continuous playhead.
[`121754`](../../captures/session_20260826_121754.log) rebuilt the whole
settled prefix on each later length step (2611 RECORDING `snapshot_idle`
steps, BPM drift, post-stop `midi_gap` 361 ms).
[`125527`](../../captures/session_20260826_125527.log) kept unpublished
snapshot in-flight through OVERDUBBING with repeated 2.6–27.4 ms
`snapshot_idle` turns and `midi_gap` spikes (35 ms, 432 ms); cadence
alternation is in tree for device verify.
[`130244`](../../captures/session_20260826_130244.log) still stacked
unpublished snapshot with long PLAYING/OVERDUBBING visual idle reconstruct
bursts (`idle_reconstruct` ~145–217 ms; `midi_gap` 415 ms OVERDUBBING,
242 ms PLAYING, 451 ms STOPPED). In tree: unpublished snapshot contention
keeps visual fallback constrained and cadence-gated (1-bar constrained
slice on every 4th eligible turn) while snapshot updates stay active.
[`131338`](../../captures/session_20260826_131338.log) still showed PLAYING
stalls (`midi_gap` 195 ms and 184 ms) with first-PLAYING stacked
`idle_maint`/snapshot/persist and later `usbdisp`/`load_frame`/`late_on`
windows. Refinement in tree: unpublished visual fallback constraint now
applies whenever unpublished snapshot work is pending (not only in-flight),
and live-record snapshot append starts now admit at most one completed bar
of new range per start to keep setup cost small.
[`131852`](../../captures/session_20260826_131852.log) confirms the parked
owners are still active: `snapshot_idle` and `PERS,work` still share turns
(`snapshot_idle us=26823` at 147.341 s then `PERS,work payload_slice=20806`,
plus repeated alternation while pending), OVERDUBBING still reaches
`midi_gap=236090` with `usbdisp=143649` / `load_frame=25638`, and STOPPED
reaches `midi_gap=430552` with `usbdisp=538063`, `load_frame=401498`,
`idle_maint=337246` while `ODUB,stop,display` remains 21.9-50.7 ms. The
same capture records `timing_max,DisplayResolveLiveCapture,215619`. In tree:
post-stop lane now blocks deferred save on turns where snapshot already ran;
device recapture is still required and 4b.1 remains open.
[`132627`](../../captures/session_20260826_132627.log) no longer shows
`PERS,work` while `snapshot_idle` is active (same-turn overlap not observed in
that run), but 4b.3/4b.4 owners remain: post-overdub PLAYING still reached
`midi_gap=175230` (`usbdisp=121603`, `load_frame=14728`, `late_on=184364`),
STOPPED reached `midi_gap=198666` (`usbdisp=319516`, `load_frame=191331`,
`late_on=30491`), and `timing_max,DisplayResolveLiveCapture` remained high
(`157754`). In tree for 4b.3: post-overdub drain now inserts MIDI polls during
`runDeferredLoadAndDisplayFrame` (after display and load-job slices) so
`load_frame` does not monopolize an entire drain turn before the next USB
dispatch. Device recapture required.
[`133517`](../../captures/session_20260826_133517.log) still showed
`snapshot_idle`/`PERS,work` interleaving while pending (e.g.
`snapshot_idle us=7360` then `PERS,work 86`), plus OVERDUBBING windows with
`midi_gap=200549` (`usbdisp=129828`, `load_frame=23600`, `late_on=26036`) and
`midi_gap=196173` (`usbdisp=68686`, `load_frame=15178`, `late_on=96834`,
`persist_save=32056`). Post-overdub PLAYING windows in that run were
26-52 ms class after stop-display lines (display 19.6-51.4 ms). In tree for
4b.1: lane arbitration now includes RECORDING alongside PLAYING/OVERDUBBING
for pending snapshot work, and persist lane toggles now use
RECORDING/PLAYING/OVERDUBBING pending snapshot detection (not transport-only).
Device recapture required.
[`134438`](../../captures/session_20260826_134438.log) showed no
`PERS,work` entries (`0` matches) while `snapshot_idle` remained active, so
same-turn overlap was not observed in that run. 4b.3 remains open:
post-overdub PLAYING still reached `midi_gap=242938`
(`usbdisp=149450`, `load_frame=53120`, `late_on=69979`) after
`ODUB,stop,display=44222`, and STOPPED still reached `midi_gap=323973` with
`loop_rem,idle_reconstruct=148417`, `loop_rem,idle_maint=180315`,
`idle_maint=319996`, and `load_frame=38737`. 4b.4 remains open:
`timing_max,DisplayResolveLiveCapture=225601`. In tree for 4b.3:
`MidiHandler::handleMidiInput` now caps per-call dispatch batch size to `32`
(from `128`) while `anyPlayingMidiDrainAfterOverdubStop()` is active, so
post-overdub drain turns cannot spend a full 128-message USB dispatch burst in
one `usbdisp` slice. `pio run -e teensy41-capture-serial` and
`pio test -e native` PASS; device recapture required.
[`135317`](../../captures/session_20260826_135317.log) kept `PERS,work=0`
while `snapshot_idle` remained active (4b.1 still open until repeated holds).
4b.3 remains open: first post-stop cycle stayed bounded at
`ODUB,stop,display=15428`, but later `midi_gap=181392` occurred in
OVERDUBBING after PLAYING re-entered overdub; later cycles were
`midi_gap=42033` and `22132`. 4b.4 remains open:
`timing_max,DisplayResolveLiveCapture=163008`. In tree for 4b.4:
`rebuildOverdubCommittedDisplayLayer` now avoids snapshot gather+reconstruct on
the overdub paint path when `visualCache` already has notes (falls through to
held-layer/window-filter branches), bounding this owner before recapture.
`pio run -e teensy41-capture-serial` and `pio test -e native` PASS
(`RAM1 free for local variables: 4608`).
[`140201`](../../captures/session_20260826_140201.log) kept `PERS,work=0`
while `snapshot_idle` remained active. 4b.3 improved but remains open:
post-stop PLAYING `midi_gap` reduced to `48361` (from `142534` in `135317`),
but OVERDUBBING still reached `midi_gap=133468` in first-bars windows. 4b.4
improved strongly in this run: `timing_max,DisplayResolveLiveCapture=7107`
(from `163008`). The same capture showed overdub reopen source drift
(`DIAG,lcr,src,why=open` notes `0`/`165`/`228` while nearby PLAYING `DISP`
totals were `240`/`253`/`308`) and a stop-path restamp miss
(`seal_publish_span_cache_restamp ... invalidated_missing_id` with
`capture hot stop verify: non-canonical storage (check=16)`). In tree:
`Loop::commitPendingCapturePass` now invalidates source-span cache on restamp
miss, and `Loop::rebuildOverdubSourceSpanCache` now seeds from a clean
`visualCache` when prepared spans miss and no prior cache seed exists.
`pio run -e teensy41-capture-serial` and `pio test -e native` PASS; device
recapture required.
[`141034`](../../captures/session_20260826_141034.log) confirms 4b.4 stays
bounded (`timing_max,DisplayResolveLiveCapture=8608`), so 4b.4 is closed.
4b.3 remains open: first-overdub windows still show repeated
`load_frame`+`usbdisp`+`late_on` lag signatures around bars 4-8, and the
wrap-reopen source drop (`src,why=open ... notes=0`) remains under active fix.
[`141838`](../../captures/session_20260826_141838.log) **reopens 4b.4** and names
two root causes — design session doc
[`playback_window_round_robin_admission_refinement.md`](../Plans/playback_window_round_robin_admission_refinement.md),
**no firmware authorized** (architecture checkpoint answers yes on ownership,
transitions, and execution bound; needs `DEC-046`).
**RC1 (bar 4–8 stalls):** one `TrackManager::updateAllTracks` call ran **344 ms**
inside clock dispatch (`midi_input`/`clk`/`tracks`/`usbdev`/`usbdisp`/`usbclk`
all `344xxx,3`; `midi_gap` only `26165`), producing `late_event,off,264136,4608`.
`PlaybackMergedMidiEventsRebuild=41` over 22 bar edges is **two** content-sized
`ensurePlaybackMergedMidiEventsBuilt` rebuilds admitted in the same interval,
because the reuse test expires exactly one bar after the last rebuild for every
enabled slot (9 enabled here). ~314 ms is unattributed: `recordPlaybackRebuild`
emits the **last** pending sample, not the max, and `cer_range`/`gather_*` are
compiled out at `SESSION_CAPTURE_INVESTIGATION_DIAG=0`.
**RC2 (overdub notes vanish at wrap):** wrap only records a pass id, no publish
happens between the record seal (52.157 s) and the stop seal (91.592 s),
`visualCache` is left dirty record-only (`VCACHE,stale,notes,270 … dcnt,18`),
`deferredDerivedViewMaintenance` excludes OVERDUBBING so the dirty bars never
drain, and the **4b.4 guard** `loop.visualCache.notes.empty()` in
`rebuildOverdubCommittedDisplayLayer` blocks the snapshot gather after
`visualCacheCoversFollowWindow` already failed — the exact condition task 4.4
forbids above. `DISP` `visualNotes` stays at `270` until
`STOPPED,…,504,504` at 106.118 s. Same stale cache also seeded overlap-resolve:
`srcafter,why=wrap,from=cache,after=240` against true content `452`.
**RC3 (separate, not bundled):** `snapshot_idle` burned ~1.0 s CPU per 2 s wall
from 16 s to 77 s (2992 turns) and published nothing.
**Review round 1 outcome:** architecture direction and the RC1/RC2/RC3
separation accepted; four changes applied to the design doc before `DEC-046`.
(1) **No bound reclassification** — Ring A caps admission multiplicity, which is
not an execution-bound class, so `ExecutionBounds.md` rows 1 and 2 both stay
`Unbounded` and no fourth class is introduced (DEC-043). (2) `prefetchTicks` is
**derived**, not chosen: `(slots-1) x clockInterval + worstCaseSingleRebuild +
margin`, binding at the **highest** supported tempo (`ClockManager` clamp
`[20,300]`). The originally proposed `TICKS_PER_BAR/4` is short at 120 BPM / 9
slots (500 vs 511 ms) and short by 2x at 300 BPM — gate **G1** blocks Slice 4
until the per-rebuild worst case is measured. (3) RC2 promoted to invariant
**R** (a derived representation may satisfy a query only when its coverage
corresponds to the requested interval and revision); the three repairs are its
enforcement points, and the `rebuildOverdubSourceSpanCache` repair is a
**semantic** gate independent of the display gate. (4) Missed-prefetch policy is
explicit (invariant **A1**): keep the existing immutable window, **never** a
synchronous rebuild on the clock path, and count
`DIAG,playback_window_starved` as an architecture failure. Slice order is now
measurement closure (0+1) → representation repair (2a/2b) → admission (3, 4).
**Review round 2 outcome:** sizing basis narrowed, rings unchanged. Ring A's
population is now the **playback-relevant** destinations whose window needs
preparation — a dynamic demand-driven set, not all enabled `(track, slot)` pairs
(invariant **A2**: no playback-relevant destination needing refresh may be
indefinitely displaced). A **design envelope** (16 tracks x 2 active slots,
≤ 240 BPM) is stated separately from absolute capacity; the 64-destination /
300 BPM calculation is **withdrawn as a blocking gate** and deferred to capacity
documentation. G1 is now representative envelope sizing, not worst-case
characterization. Investigating the reopened horizon question found
`ensurePlaybackMergedMidiEventsBuilt` centres the window on the playhead
(`winStart = playhead - winLen/2`), so the **forward horizon is already 1 bar**
(768 ticks) and 1-bar total content would halve it — the available lever is
window **bias**, not size. Within the envelope (`H_fwd`=1000 ms at 240 BPM) the
arbitration term is 73 / 156 / 323 ms at 8 / 16 / 32 eligible destinations, so
the design space is comfortable to ~16 even against the stricter
`lead <= H_fwd/2` target. Open architectural question of this phase is now
**what constitutes a playback-relevant destination** (`updateAllTracks` gates
only on `engineShouldRun(slotEnabled[i][s])` today, independent of track state),
plus horizon/bias.
**Review round 3 outcome:** four tightenings applied; sizing basis accepted.
The `(N-1) x T_clock` arbitration term is now labelled a **conservative** bound
for continuously-due destinations, not the expected steady state (self-staggering
is verified in Slice 4, not assumed), and the 32-destination row is the normal
upper envelope rather than a Ring A design target. **A1** is split into invariant
("under the design envelope, a playback-relevant destination must have its
replacement ready before its current window expires") and failure policy
(existing immutable window stays selected; no synchronous rebuild on the clock
path), so the failure branch is not unreachable by definition. Ring B's owner-3
dependency skip is given **normative non-priority semantics**: a skip consumes
the cursor position and transfers no turn. Content window **stays at 2 bars**;
only bias is open. **2a is not shippable ahead of Ring B** (Slice 3) — it lands
with native correctness tests and its device gate closes after Ring B, unless
recapture shows the restored gather path is unexercised (no
`DisplayCaptureFullGather` growth, `timing_max,DisplayResolveLiveCapture` inside
the 4b.4-closed class of 8608 us).
**Design session (round 4) — playback relevance derived; last blocking item
closed.** Finding: **no track-state check exists anywhere on the
playback-window path.** `PlaybackMidiOutput::engineShouldRun` is
`return slotEnabled;`, `Track::getEffectivePlaybackTick` returns the global
`currentTick` regardless of state, and `playMidiEvents` /
`playMidiEventsForSlot` return early only on `isStoppedRecording()` and
`!hasCommittedPasses() || loopLengthTicks == 0` — then
`playCommittedLoopMidi` calls `ensurePlaybackMergedMidiEventsBuilt` *before*
the `mergedEvents.empty()` return. So any enabled slot with committed content
rebuilds its 2-bar window once per bar forever, playing or not.
`141838` confirms it: only **one** track ever changes state (all nine
`ST,Track` lines), nine slots were enabled at boot, and
`DIAG,playback_build` window starts advance exactly one bar per ~2 s
(`0, 768, 1536, 2304, …`) from 15.409 s — record start. No spurious emission:
MO on every channel except the recorded channel 5 is 9–10, matching the nine
transition-driven all-notes-off broadcasts. Pure waste, which is why it never
surfaced as a correctness bug.
**Obligation** exists in `TRACK_PLAYING` and `TRACK_OVERDUBBING`; not in
`EMPTY`/`ARMED` (nothing started) or `STOPPED_RECORDING` (already excluded in
code). **`TRACK_STOPPED` is the leak.** Mute/solo/slot-mute do **not** remove
obligation — `PlaybackMidiOutput.h` documents that they suppress send, not the
engine, and the engine must keep cursor and ledger coherent for unmute.
Active-vs-layered selects `PlaybackEmitPolicy`, not obligation.
Predicate: `slotEnabled && trackPlaybackEngineRunning && hasCommittedPasses &&
loopLengthTicks > 0`, owned by `TrackManager` in
`TrackManagerPlaybackPolicy.cpp` as the engine-side counterpart of
`isTrackAudible` — no new module.
**Queued-destination prewarm keeps its commit-boundary exemption** (open item 3
resolved): `updateAllTracks` already refuses to commit a slot switch while
`!isPlaybackMergedMidiEventsReadyForSlot(targetSlot)` on a long loop, and
prewarm runs off the clock path via `ensurePlaybackMergedEventsForSlot`.
**Remaining open:** item **1a** — whether `TRACK_RECORDING` joins the engine-
running set for *other* enabled slots on the same track (product decision), and
item **2** — window bias (needs the LoopEnd-launch/clock-catch-up backward
margin requirement). Content window stays at 2 bars.
**`DEC-046` ACCEPTED 2026-08-26** as architecture direction — it does **not**
authorize firmware Slices 1–4. Item 1a resolved: **`TRACK_RECORDING` is included**
(layered-slot playback during recording is intentional), so `TRACK_RECORDING`
joins PLAYING and OVERDUBBING in `trackPlaybackEngineRunning`; the recorded loop
stays excluded by lack of committed passes. Only item 2 (window bias) remains
open, affecting `H_fwd`/G1 but not the admission model.
**Slice 1 firmware is in tree (user authorized this slice):**
`recordPlaybackRebuild` keeps the maximum duration and that sample's
track/slot/path, plus a rebuild count. Capture line is
`DIAG,playback_build,<path>,<us>,<winStart>,<winLen>,<rev>,<track>,<slot>,<count>`,
where `<path>` is `snapshot`, `compatibility`, or `note_edit`. The path is
captured at `Track::ensurePlaybackMergedMidiEventsBuilt`; the one-shot remains
deferred and `<count>` remains an aggregate of rebuild calls before emission.
Device recapture is required to attribute the six `cer_edit` signatures to
snapshot versus compatibility playback.
Ring B/RC2 is **closed**. RC2 device gate 4b.2b is closed by
[`183452`](../../captures/session_20260826_183452.log). Residual 4b.1b is
owned by the owner-boundary plan and parked.
**Slice 0 RAM1 blocker found and fixed (2026-08-26).**
`teensy41-capture-serial-investigation-diag` built at RAM1 **locals 512** —
a FAIL against the 4000 floor despite PlatformIO `SUCCESS`, so the board was not
flashed. ELF section diff attributed the cost to `.data` +1024 / `.bss` +3072
with ITCM unchanged (424796+1188 = 424860+1124 = 425984), and the confirmed
cause was `SESSION_CAPTURE_INVESTIGATION_DIAG` format strings **not**
flash-placed: `LoopMaterialization.cpp` and `EditApply.cpp` already had
`LOOP_DIAG_FMT`/`EDIT_DIAG_FMT` = `PSTR(...)`, but `LoopVisualCache.cpp`
(4 literals) and `LoopPendingNoteChange.cpp` (6 literals) had none, so their
`#CAP` literals landed in DTCM — contrary to
`Instrumentation-Session-Capture-Gating`. Fix adds
`LOOP_VCACHE_DIAG_FMT` / `LOOP_NOTECHANGE_DIAG_FMT` on the same pattern.
Result: investigation-diag RAM1 variables 97792 → **93696**, locals 512 →
**4608** — identical to `teensy41-capture-serial`, so the investigation build now
costs **no** DTCM over the default capture build. Flash absorbed the strings
(`data` 78692 → 79796). Both envs PASS; `pio test -e native` **1475/1475**.
Checkpoint: ownership no, transitions no, unbounded interval work no.
**Slice 0 DONE — attribution closed.**
[`153941`](../../captures/session_20260826_153941.log) on the investigation-diag
env reproduced the stall larger (`tracks=393818`, `midi_input=393845`, same
chain) and attributed it end to end. Of one `updateAllTracks` at **393818 us**:
`cer_range` **367412** (93%) → `edit` (`applyNoteEditPassSequence`) **343625**
(94% of that) → `ulen` **341642** → **`recon` 339073**. The windowed CER
traversal is only `append=23511`; `collect`+`sort` are 276 us. So **92% of the
rebuild is note reconstruction inside the edit projection.**
`cer_rows` is constant in all 50 non-empty samples: `active=217, comp=217`
(every Active row an overdub companion), `del=185`, `ulen=32` — 217 rows
replayed to emit **164** events in a 2-bar window. F11 proven: edit-projection
cost scales with EditPass count, not window size. `recon` is 98–99% of `ulen`
in every sample; `nlen` drives it (18→339 ms, 16→287 ms, 12→170 ms) but the
same `nlen=18` also measured 68989 us, so per-row cost is content-dependent
(~19 ms/row worst).
**Compatibility path, not snapshot path:** `cer_range` starts at 52.014 s,
before this session's long record at 61.168 s, and every publish belongs to the
one transitioning track — so the loop carrying 18 chunk lists and 217 companion
rows is a **loaded slot with no published root**, reaching
`copyEffectiveCommittedEventsInRange` via `gatherCommittedEventsInWindow`.
**New root cause RC4 (separate, not bundled):** the length-update path re-runs
note reconstruction per row on the compatibility gather. RC1 is admission
multiplicity; RC4 is unit cost. Against the envelope (`H_fwd_wall`=1000 ms at
240 BPM) a 367 ms rebuild clears `L < H_fwd` but **violates the `L <= H_fwd/2`
target from 16 eligible destinations upward** — admission scheduling alone only
just fits, with no margin. Candidate resolution is already in the architecture
direction (DEC-044/045): a published snapshot carries edits applied, so the
compatibility gather and this whole cost do not exist for a slot with a
published root. Whether loaded slots publish on restore is the open design
question.
[`160244`](../../captures/session_20260826_160244.log) **proves RC4 unit cost is
environment-dependent, not input-dependent.** Six recurring `cer_edit` input
signatures repeat in three rounds; five with byte-identical
`ndel/nlen/lgth/shrt/ev` cost **7.3x-9.7x** more in round 2 (long overdub,
`snapshot_idle` 48.4% of window) than in rounds 1 and 3 (28.9% and 0%).
`cer_range` splits into an uncontended band **29.5-98.9 ms** and a contended band
**306-363 ms**, so **367 ms is pathological, not representative** — `G1` must
consume the uncontended figure. Mechanism is **not** proven: wall-clock inflation
from interleaving (48.4% load alone gives ~2x, not 9x) versus
`InternalHeapFirstAllocator` spill to PSRAM (`min_ever` fell 340->120 KB, which is
pressure but not exhaustion). Discriminate before optimizing.
**Weighting between RC1 and RC4 revised:** `tracks` max 391.1 ms against a single
362.9 ms rebuild leaves no room for multiplicity as the main term, and observed
multiplicity is **pairs** (median `cer_range` spacing 1998 ms; only 8 gaps under
50 ms, all 11.9-16.8 ms). Ring A's benefit here is bounded by ~2x while RC4 is
~9x. Ring A stays architecturally correct but must not be presented as the fix
for the 391 ms.
**Slice 1 destination attribution closed by [`161834`](../../captures/session_20260826_161834.log)
and [`162522`](../../captures/session_20260826_162522.log).** The 94
`playback_build` maxima in `161834` identify four destinations: `T0/S6` (57
lines, max 166.325 ms), `T4/S0` (34, max 25.439 ms), `T6/S0` (2, max 10.965
ms), and `T2/S2` (1, max 9.406 ms). `162522` directly pairs all 15 repeated
`cer_edit` compatibility gathers with `T0/S6`; its maximum is 360.754 ms.
`T0/S6` is the dominant long-loop stream.
The telemetry `<count>` is rebuild calls before the next one-shot emit, not
distinct destinations per bar edge; no eligible-set count is inferred from it.
The small `lists=1/2, rows=0` gathers are separate edit-free destinations.
**RC4 mechanism remains open after `162522`:** `snapshot_idle` lines occur
after the inflated gather, so snapshot construction is not concurrent inside
the `g_reconstructUs` span. `165024` now rules out the measured
`InternalHeapFirstAllocator` spill route and nested clock dispatch: all 16
`cer_heap` rows report `spill=0`, `ext=0`, `clk=0`, and `iclk=0`. Higher-
priority interrupt elapsed time and `ExternalMemoryFirstAllocator`
service/cache effects remain unmeasured. `cer_recon` now splits
internal-heap `filt` from PSRAM-first `span`/`proj` and records
IntervalTimer ISR `itickus`.
[`171159`](../../captures/session_20260826_171159.log) closes that split:
`filt` is unchanged on the 7.9x `nlen=18` pair (`36` / `36`); `span` and
`proj` take the inflation; `itickus=0` on all 11 rows. Question 3 is closed
from code: `adoptPersistedSnapshot` / `restorePassesSnapshot` do not publish.
The new source-tagged telemetry is device-validated by
[`202654`](../../captures/session_20260826_202654.log): 82 emitted
`playback_build` samples classify as 74 `compatibility` and 8 `snapshot`
(no `note_edit`). The long-loop `T0/S6` stream remains compatibility, reaching
325.541 ms, while snapshot-backed `T4/S0` samples remain below 1 ms
(maximum 358 us). This confirms the loaded-slot compatibility path and the
published-root snapshot path coexist in one later capture.
RC4 question 5 is closed as `T0/S6` compatibility-only: the eight `lgth`
rows belong to that loaded slot's companion replay, not to a published
snapshot. Ignore further `T0/S6` lengthen-branch forensics.
**G1 satisfied:** `representativeRebuildWall` = 358 µs (`T4/S0` snapshot
maximum in `202654`); `prefetchTicks` = 272; `H_fwd` = 768. The same
capture's non-`T0/S6` compatibility ceiling is 11.859 ms and still fits
that lead. Ring A firmware remains unauthorized.
[`205447`](../../captures/session_20260826_205447.log) confirms leaving
`T0/S6` for `T0/S5`: S5 rebuild is 334 µs and S6 drops out of
`playback_build`. The same timestamp is a 377 ms `idle_maint` / `midi_gap`
stall after `VCACHE,stale_all`, not an S5 rebuild cost. RC4 conclusions
are in
[`playback_window_edit_projection_unit_cost_investigation.md`](../Plans/playback_window_edit_projection_unit_cost_investigation.md).
**RC4 sequencing decided (user, 2026-08-26):** investigation is **authorized now**
in [`playback_window_edit_projection_unit_cost_investigation.md`](../Plans/playback_window_edit_projection_unit_cost_investigation.md);
the **firmware fix is a separate change** and must not enter the Ring A slice.
Owner is proven: `applyChangeLengthById` reconstructs **only on the lengthen
branch** via `reconstructNotesForPitch`, which per row reallocates, rescans every
event, and re-runs the full canonical-span pipeline — `lgth=8` rows carry all
339 ms (~42 ms each) while `shrt=10` shorten rows contribute nothing.
Order: Slice 1 closure → RC4 investigation → measure RC4 resolution → **size G1
from the post-resolution cost** → Ring B/RC2 independently → Ring A last. Do
**not** lock the prefetch lead at 367 ms: that is ~35 clock dispatch periods at
240 BPM, and two samples with identical row counts differ 5x (339 vs 69 ms), so
representativeness is unestablished.
RC2 device gate 4b.2b is closed by
[`183452`](../../captures/session_20260826_183452.log). Do **not** check off
4b.3 until device verify.
Do not close **4b.1**. Do **not** split `atomicRenameTempFile`. 4b.0 wrap/hold
remains device PASS [`092755`](../../captures/session_20260826_092755.log).

Phase 3 task **4.4 display** remains in tree (not closed). Paint and visual-cache fill read
published `LoopSnapshot` events through
`tryGatherPublishedSnapshotEventsInWindow` (plus live capture when needed),
then `reconstructDisplayNotes`. A nonempty dirty `visualCache` prefix does
not authorize the paint window — `visualCacheCoversFollowWindow` must pass
or the window gathers from the snapshot after capture ends
([`235954`](../../captures/session_20260825_235954.log)). Live overdub
keeps the PLAYING committed handoff only while the paint window stays
inside the last gather ([`001048`](../../captures/session_20260826_001048.log)
`frame=0`; [`001907`](../../captures/session_20260826_001907.log) held the
first 16-bar slice on a 21-bar loop). Follow exit or wrap invalidate
snapshot-gathers once. A remainder of at most two gather margins is
included in the first gather so a 20-bar loop does not omit the last
2 bars and then gather on the MIDI path
([`002634`](../../captures/session_20260826_002634.log): window at 3072
at 117.1 s, last-bar notes at 119.2 s, `DisplayResolve` 81–196 ms).
After overdub stop, `ODUB,stop,display` holds the composed frame and does
not snapshot-gather. Device PASS [`004934`](../../captures/session_20260826_004934.log):
display **8.8 ms** / **12.2 ms** (was 97–154 ms on
[`003459`](../../captures/session_20260826_003459.log)). PLAYING after first
publish is **7.4–8.4 ms** `midi_gap`. Ring B/RC2 host implementation is now
in tree; RC2 device gate 4b.2b is closed by
[`183452`](../../captures/session_20260826_183452.log). 4b.3 post-overdub
load-frame drain polls remain separately device-verification work.
Compatibility gather remains
when no published root exists. Do not CER-gather on the overdub paint
path. Task **4.3** stays skipped. NOTE_EDIT, overdub analysis,
validation, 4.5, 4.6, and Phase 5 are **not selected**.

After the first published `LoopSnapshot` exists, PLAYING and OVERDUBBING idle
do not `step` successor construction. PLAYING visual stays off unless the
cache is empty. First-record starts during RECORDING after the playhead has
left bar 1 and continues on PLAYING (`DEC-045` 4b.2). Device PASS
[`233308`](../../captures/session_20260825_233308.log): four overdubs after
record; first publish at 97.50 s; **no** `snapshot_idle` from then until
STOPPED; PLAY4 BPM 119.966–120.049.

Phase 3 tasks 4.1–4.2 remain complete. Playback preparation reads the
published `LoopSnapshot` through `ensurePlaybackMergedMidiEventsBuilt`,
records snapshot identity for freshness, and falls back to compatibility
gather when no snapshot is selected. Cursor, order, ledger, and horizon stay
on `Track` / `LoopPlaybackRuntime`. `playbackEventPhase` stays the hot-path
scalar.

**Authorized exception:** queued-destination visual-cache invalidation — port of the
repair authorized on `feature/overdub-stop-lag-stage-c`. Owner:
`DisplayManager::invalidateForSlotChange`. Plan:
[`playback_preparation_queued_destination_visual_cache_invalidation_bugfix.md`](../Plans/playback_preparation_queued_destination_visual_cache_invalidation_bugfix.md).
Firmware in tree; host gates next. Does not change snapshot ownership or LCR.

**Branch:** `feature/immutable-loop-snapshot-passes`, forked from
`546169f66658b87c54e6166b79eba74628aa3d1e`.

**OpenSpec:** [`immutable-loop-snapshot-passes`](../../openspec/changes/immutable-loop-snapshot-passes/)
**Plan:** [`loop_pass_snapshot_effective_state_refinement.md`](../Plans/loop_pass_snapshot_effective_state_refinement.md)
**Active wrap slice:** [`overdub_wrap_source_snapshot_migration_refinement.md`](../Plans/overdub_wrap_source_snapshot_migration_refinement.md) — OpenSpec tasks `3.5`/`3.6` are closed; parent **4.4** closed (4.4a–4.4h + inventory).
**Applied Phase 0 spec patch:** [`loop_snapshot_pitch_bar_span_index_refinement.md`](../Plans/loop_snapshot_pitch_bar_span_index_refinement.md) — logical structure selected. Phase 1 native/offline model is complete. Phase 2 publication adapters, Phase 3 playback snapshot reads (4.1–4.2), stateless snapshot queries (4.3), and display/NOTE_EDIT/analysis/validation current-root migration (4.4) are complete. Next: **4.5** verify or **4.6** snapshot-identity freshness.

Phase 0 documentation now includes the project-scoped
[`MIDI_PROTOCOL_GUIDE.md`](../Guides/MIDI_PROTOCOL_GUIDE.md), based on
M2-104-UM v1.1.2, with a read-only audit of the current MIDI 1.0 logic. The
2026-08-25 reference-root refinement separates `LoopPass` from `LoopSnapshot`,
requires a persistent reference root, defines reachability and provisional
versus global snapshots, and loads only the latest saved snapshot after reboot.
The later 2026-08-25 pitch/bar amendment selects the logical indexes. The Phase
1 native measurement selects the sparse fixed-capacity paged directory
experiment; page capacity and memory characteristics remain measured
parameters. Formal reassessment and approval are recorded in
[`ARCHITECTURE-REASSESSMENT.md`](../../openspec/changes/immutable-loop-snapshot-passes/ARCHITECTURE-REASSESSMENT.md).
Later-phase gates are recorded in
[`PREFLIGHT.md`](../../openspec/changes/immutable-loop-snapshot-passes/PREFLIGHT.md)
and
[`DECISION_REVIEW.md`](../../openspec/changes/immutable-loop-snapshot-passes/DECISION_REVIEW.md).
The Phase 1 model is native/offline only; no production runtime behavior or
consumer selection changed.

The maintained `LoopContentResolution` lifecycle and its playback-readiness
continuation are not pursued on this branch. The existing `LoopContentResolution`
prototype remains historical compatibility context only; the proposed snapshot
model must not create a replacement maintained effective-state owner.

Source-level replace-on-retrigger pairing is deferred. This branch retains the
current `Track::recordMidiEvents()` / `pendingNotes` behavior and existing LIFO
pairing. The deferred future bugfix may add an internal synthetic NoteOff before
replacing an open same-channel/same-pitch capture entry; it is outside the
immutable snapshot Phase 0 scope.

## Historical base work

The sections below record work present at the fork point. They are not the active
execution queue for this branch and require explicit reauthorization before use.

### Slice 0 — docs + scaffolding delete

**Owner:** docs + `Loop` companion-projection members (`companionProjectionEvents_`, `rebuildCompanionProjectionForCurrentRevision`, `tryCopyCompanionProjectedWindow`, `clearCompanionProjectionCache`)

**Invariant:** Dead paired-projection scaffolding is gone. Interval gather still uses windowed `applyNoteEditPassSequence` (predictable-but-slow baseline). One derived-authority direction is documented; no second consumer cache.

**Status:** Shipped — native **1429/1429**. Dead scaffolding removed; gather still windowed `applyNoteEditPassSequence`.

### Slice 1A — cooperative prepare (next)

**Owner:** existing LCR device-gate prepare path only.

**Invariant:** Same prepare semantics; persistent progress; bounded work units; yield/resume without repeating completed work. Instrument `prepare,slice,*`. If F1–F7 cannot be subdivided without algorithm decomposition, **stop and report** (failure is valid).

**Do not in 1A:** optimize prepare, redesign algorithm, add cache, wire playback consume, enforce 60% × T_clock timer.

**Timing gate:** At 170 BPM, one MIDI clock interval is **14.71 ms**. The
**8.82 ms** value (60% of that interval) is only an experimental operating
target; no guaranteed maintenance reservation has been established. Measure the
proposed eight-candidate unit separately, including its indivisible path-copy,
index, replacement/tombstone, and allocation work. Treat p95 above 8.82 ms as
outside the initial target and any unit above 25 ms as an investigation failure;
these are not hard MIDI guarantees.

### Paired companion consume reverted (closed context)

**Evidence:** [`163135`](../../captures/session_20260822_163135.log) — `paired_proj` **0** / `win_apply` **33**; multi-second `idle_gather`; USB reconnect. Decision: `PairedCompanionState` reverted; tip cut at Stage C. Overlap never used paired projection (`ActiveNoteLedger` / `overdubSourceViewNotes_`).

### STOPPED undo / track-switch first-paint

**Evidence:** [`183229`](../../captures/session_20260821_183229.log) device PASS. Prior fail [`161804`](../../captures/session_20260821_161804.log) / [`165107`](../../captures/session_20260821_165107.log) undo `idle_maint` 850 ms. Plan: [`runtime_scheduling_owner_boundary_inventory_investigation.md`](../Plans/runtime_scheduling_owner_boundary_inventory_investigation.md) §34 / §36 Stage C.

**Owner:** `DisplayManager::invalidateForSlotChange` / `Track::processDeferredIdleMaintenance` → `Loop::rebuildVisualCacheIdleSlice`

**Invariant:** STOPPED undo and track switch keep `visualCache.notes` and fill through budgeted idle slices with wrap-append deferred. No wipe. No unbounded gather-plus-append in one STOPPED turn. No companion full replay on STOPPED.

**Status:** Device PASS [`183229`](../../captures/session_20260821_183229.log) — undo `stale_all` notes=2988, first `DISP` visual=2988, first post-undo `idle_maint` 224 ms; 68-bar switch notes=3040 / visual=3040. Native **1429/1429**. Closed.

### Overdub companion admission and source-span prewarm

**Evidence:** [`162928`](../../captures/session_20260821_162928.log) empty open + 136.77 s freeze. Device PASS [`165107`](../../captures/session_20260821_165107.log): 5/5 opens `notes` 1319–1394; 34/34 consume `idsel>0`; max `tracks` **190 ms**. Residual `norow` 1–4 with working select is parked.

**Owner:** `Track::processDeferredIdleMaintenance` → `Loop::prewarmOverdubSourceSpanCache` (Stage A empty-projection gather path removed with dead companion-projection scaffolding)

**Invariant:** Clock-path gather must not run full companion replay. Empty companion projection is not a successful gather. First overdub open hydrates a non-empty source view from a cache filled after boot first-paint, not from a same-turn `slice_clean` prewarm.

**Status:** Stage A+B device PASS [`165107`](../../captures/session_20260821_165107.log). §36 closed.

### Boot title `boot_commit` display stall

**Evidence:** [`161804`](../../captures/session_20260821_161804.log) device PASS. Prior gather hangs: [`161148`](../../captures/session_20260821_161148.log) (`boot_commit=70438665`, first `DISP` visual=0), [`154353`](../../captures/session_20260821_154353.log) (`boot_commit=75457996`, visual=0).

**Owner:** `runDeferredIdleMaintenance` / `runDeferredLoadAndDisplayFrame` / `TrackManager::onBootSlotLoadComplete`

**Invariant:** First piano roll filters the focus visual cache. `onBootSlotLoadComplete` must not clear `visualCache.notes`.

**Stage change shipped:** Boot idle stays on the focus track; `boot_commit` waits until that cache is clean; boot complete no longer calls `invalidateForSlotChange`. Guide: [`BOOT_LOAD.md`](../Guides/BOOT_LOAD.md).

**Status:** Device PASS [`161804`](../../captures/session_20260821_161804.log) — focus `slice_clean` 2638 notes at 10.9 s; USB at 11.484 s; first `DISP` visual=**2638** / 1224 window notes; `boot_commit=288557` (289 ms). `DFRAME` continues at ~15 ms.

### Boot title startup stall hardening

**Evidence:** [`161410`](../../captures/session_20260820_161410.log)

**Owner:** `Looper::setup`

**Invariant:** Startup does not stall at the title screen when SD bootstrap is transiently unavailable; boot continues without requiring repeated manual reboots.

**Stage change shipped:** `Looper::setup` now initializes SD with bounded retries (`3` attempts, `50 ms` delay). If SD remains unavailable, boot takes an explicit empty-runtime fallback path and skips `StorageManager::loadState` instead of blocking startup.

**Status:** Native verification pending.

### Overdub lag plan — baseline reset and reprioritized execution

**Evidence:** [`210319`](../../captures/session_20260820_210319.log) (new baseline), [`231845`](../../captures/session_20260820_231845.log), [`232951`](../../captures/session_20260820_232951.log), plus prior attribution captures [`165534`](../../captures/session_20260820_165534.log), [`165956`](../../captures/session_20260820_165956.log), [`170724`](../../captures/session_20260820_170724.log), [`174941`](../../captures/session_20260820_174941.log)

**Owner set (current priority order):**
1. `Track::stopOverdubbing` stop-path handoff (`ODUB,stop,display` envelope)
2. `Loop::sealPendingNoteChangesToEditPasses` + `TrackUndo::pushOverdubSessionOnStop` (dominant `seal_fx_*` contributors)
3. Post-stop scheduler debt window (`processDeferredIdleMaintenance`, `runDeferredLoadAndDisplayFrame`, `StorageManager::processDeferredSaveState`)

**Invariant:** Prioritize only runtime-blocking windows: stop display handoff and first post-stop scheduler stall. Stop commit semantics remain unchanged; stop return must not wait on newly discovered deferred debt.

**Runtime-stall baseline (from [`210319`](../../captures/session_20260820_210319.log)):**
- **Published stop cycles with complete first post-stop window:** 6
- **`ODUB,stop,display` elapsed:** **56.451-71.803 ms** (median **65.288 ms**)
- **First post-stop `DIAG,midi_gap`:** **78.634-145.373 ms** (median **107.569 ms**)
- First post-stop decomposition:
  - `idle_maint`: **78.619-145.357 ms** (median **101.576 ms**) — dominant
  - `load_frame`: **27.553-42.889 ms** (median **33.548 ms**)
  - `persist_save`: **13.295-122.704 ms** (median **47.168 ms**)
- Published stop `seal` duration (same 6 complete cycles): **45.461-61.129 ms** (median **56.006 ms**)
- `seal_fx_*` maxima in the same run:
  - `seal_fx_seal_pending_changes`: **41.310 ms** (dominant synchronous owner)
  - `seal_fx_push_overdub_undo`: **13.243 ms** (secondary synchronous spike)
  - `seal_fx_mark_display_ranges`: **2.828 ms**; remaining `seal_fx_*` are minor
- Additional published cycle without a complete first post-stop window in this capture: `display=39.965 ms`, `seal=29.498 ms` (tracked separately from the 6-cycle target sample).
- Two `seal=skipped` cycles still show large first gaps (**672.708-711.300 ms**) with small display (~11 ms), proving deferred scheduler debt can dominate independently of stop display.

**Reprioritized execution (based on measured stalls):**
1. **P1 — Stop display envelope trim (synchronous):** keep attribution on `seal_fx_seal_pending_changes` and `seal_fx_push_overdub_undo`; reduce and bound the `ODUB,stop,display` envelope before any broad scheduler tuning.
2. **P2 — Post-stop debt causality proof (deferred):** classify first-gap contributors as **pre-existing debt**, **stop-created debt**, **stop-amplified debt**, or **newly scheduled after stop**.
3. **P3 — Scheduler policy only after causality:** if P2 finds architectural stop-triggered invalidation/rebuild, fix that owner cause first; do not mask it with slice caps.
4. **P4 — Background arbitration:** prevent large `idle_maint` + `persist_save` stacking from consuming the same post-stop runtime window once causality is closed.

**Runtime scheduling roadmap linkage:** [`runtime_scheduling_owner_boundary_admission_refinement.md`](../Plans/runtime_scheduling_owner_boundary_admission_refinement.md)
- P1 remains pre-contract synchronous owner attribution on `Track::stopOverdubbing` / seal owners; it does not start interval reservation.
- P2 contributes Stage R owner-boundary evidence for post-stop debt classification (`pre-existing`, `stop-created`, `stop-amplified`, `newly scheduled after stop`).
- P3 enforces Owner-Boundary Gate ordering: if stop-triggered unnecessary invalidation/rebuild is found, fix that owner cause before slice caps or arbitration tuning.
- P4 is scheduling policy input only after owner causality is closed; interval reservation remains unauthorized until roadmap Stage C/A gates are explicitly satisfied.

**Runtime targets (for this overdub-lag slice):**
1. **Stop handoff target:** `ODUB,stop,display` is measured as a bounded owner envelope with dominant contributors attributed (`seal_fx_seal_pending_changes`, `seal_fx_push_overdub_undo`) and trimmed without changing stop commit semantics.
2. **First post-stop opportunity target:** first post-stop `DIAG,midi_gap` is reduced by owner-cause closure, not by masking unknown debt; each major contribution is classified as `pre-existing debt`, `stop-created debt`, `stop-amplified debt`, or `newly scheduled after stop`.
3. **Stop/deferred boundary target:** stop return does not wait on completion of deferred maintenance debt discovered in P2.
4. **Arbitration target:** long `idle_maint`, `load_frame`, and `persist_save` work does not stack into the same first post-stop runtime window after P4 policy work.
5. **Scheduling-contract target carryover:** no multi-second PLAYING/transport-active `DIAG,midi_gap` windows; interval reservation remains out of scope for this slice.
6. **Musical correctness gate carryover (when lateness hooks are present in the same evidence run):** `late_event_count == 0` and `max_lateness_us <= 0` for note-on, note-off, and clock.

**Target-aligned baseline checkpoints (same [`210319`](../../captures/session_20260820_210319.log) evidence):**
1. **Stop handoff target baseline:** complete published stop-display envelope is **56.451-71.803 ms** (median **65.288 ms**); dominant synchronous contributors remain `seal_fx_seal_pending_changes` (max **41.310 ms**) and `seal_fx_push_overdub_undo` (max **13.243 ms**).
2. **First post-stop opportunity target baseline:** first post-stop `DIAG,midi_gap` on complete published cycles is **78.634-145.373 ms** (median **107.569 ms**) with decomposition windows above.
3. **Stop/deferred boundary target baseline:** two `seal=skipped` cycles still show small stop-display windows (~**11 ms**) with large first post-stop gaps (**672.708-711.300 ms**), confirming deferred debt can dominate without large synchronous display cost.
4. **Arbitration target baseline:** in the 6 complete published windows, `load_frame` remains **27.553-42.889 ms** every cycle and `persist_save` reaches **47+ ms** in 4/6 windows (max **122.704 ms**), so stacked post-stop work remains an active scheduling risk.
5. **Scheduling-contract carryover baseline:** run-wide `DIAG,midi_gap` in this capture reaches **2.855 s** (3 windows over 1 s), so the no-multi-second-gap carryover target is not yet met globally.
6. **Musical correctness gate carryover baseline:** lateness hooks are present; this run reports non-zero late note windows (`late_on`: 5 windows, 23 events total, max lateness **42.458 ms**; `late_off`: 4 windows, 13 events total, max lateness **29.551 ms**; `late_clk`: 0 events).

**P1 re-measure after sorted transform lookup ([`213848`](../../captures/session_20260820_213848.log)):**
- Sample shape: **9** stop cycles, **7** published cycles with complete first post-stop windows, **0** skipped-complete cycles.
- **`ODUB,stop,display` (published + complete):** **26.751-134.121 ms** (median **58.246 ms**) — median improved vs baseline, but one long-tail outlier remains.
- **Published `seal` (same 7 complete cycles):** **18.250-125.144 ms** (median **48.153 ms**).
- **First post-stop `DIAG,midi_gap` (published + complete):** **43.875-189.583 ms** (median **104.331 ms**).
- First post-stop decomposition (published + complete):
  - `idle_maint`: **32.233-189.565 ms** (median **104.313 ms**)
  - `load_frame`: **27.147-36.773 ms** (median **31.289 ms**)
  - `persist_save`: **13.960-55.248 ms** (median **45.047 ms**)
- `seal_fx_*` maxima in the same run:
  - `seal_fx_seal_pending_changes`: **48.354 ms** (higher max than baseline)
  - `seal_fx_push_overdub_undo`: **6.650 ms** (lower max than baseline)
  - `seal_fx_mark_display_ranges`: **3.818 ms**
- Dominant outlier cycle: `ODUB,stop,display=134.121 ms`, `seal=125.144 ms`, `seal_fx_seal_pending_changes=48.354 ms`, `seal_fx_mark_display_ranges=3.818 ms` (same stop cycle).
- Carryover checks in the same run: run-wide `DIAG,midi_gap` max **2.890 s** (5 windows over 1 s); lateness remains non-zero (`late_on` max **64.220 ms**, 46 events; `late_off` max **87.453 ms**, 30 events; `late_clk` zero).

**P1 re-measure after publish-owner telemetry ([`215223`](../../captures/session_20260820_215223.log)):**
- Sample shape: **8** stop cycles, **7** published cycles with complete first post-stop windows, **0** skipped-complete cycles.
- **`ODUB,stop,display` (published + complete):** **32.702-124.432 ms** (median **50.174 ms**).
- **Published `seal` (same 7 complete cycles):** **23.529-115.351 ms** (median **42.042 ms**).
- **First post-stop `DIAG,midi_gap` (published + complete):** **50.388-160.096 ms** (median **102.466 ms**).
- First post-stop decomposition (published + complete):
  - `idle_maint`: **38.602-160.081 ms** (median **102.448 ms**)
  - `load_frame`: **27.039-36.331 ms** (median **30.616 ms**)
  - `persist_save`: **16.108-85.121 ms** (median **46.960 ms**)
- New publish-owner telemetry maxima in the same run:
  - `seal_fx_publish_pending_pass`: **62.448 ms**
  - `seal_publish_span_cache_restamp`: **62.323 ms**
  - `seal_publish_promote_pass`: **9 us**
  - `seal_publish_finalize_state`: **10 us**
  - `seal_publish_clear_capture`: **13 us**
- Existing `seal_fx_*` maxima in the same run:
  - `seal_fx_seal_pending_changes`: **44.809 ms**
  - `seal_fx_push_overdub_undo`: **69 us**
  - `seal_fx_mark_display_ranges`: **3.991 ms**
- Dominant outlier cycle attribution: `ODUB,stop,display=124.432 ms` with `seal=115.351 ms`, split primarily across `seal_publish_span_cache_restamp=62.323 ms` and `seal_fx_seal_pending_changes=44.809 ms`; publish owner is now directly measured.
- Carryover checks in the same run: run-wide `DIAG,midi_gap` max **1.907 s** (1 window over 1 s, 1 over 500 ms); lateness remains non-zero (`late_on` max **53.490 ms**, 36 events; `late_off` max **48.415 ms**, 27 events; `late_clk` zero).

**P2 re-measure after global post-overdub drain-window clamp ([`230507`](../../captures/session_20260820_230507.log)):**
- Sample shape: **10** stop cycles, **10** published cycles, **10** handoff rows, **6** unique first post-stop windows.
- **`ODUB,stop,display` (published):** **34.471-58.055 ms** (median **49.813 ms**).
- **Published `seal`:** **24.092-47.049 ms** (median **39.534 ms**).
- **First post-stop `DIAG,midi_gap` (unique windows):** **87.209-168.113 ms** (median **108.276 ms**).
- First post-stop decomposition (same unique windows):
  - `idle_maint`: **87.178-168.095 ms** (median **108.244 ms**)
  - `load_frame`: **29.352-34.071 ms** (median **32.430 ms**)
  - `persist_save`: **0.001 ms** (flat)
- Debt classification in the same run:
  - `ODUB,stop,debt_handoff`: **10/10 stop_created_debt**
  - `DIAG,odub_debt visual`: **10/10 stop_created_debt**
  - `DIAG,odub_debt save`: **9/10 pre_existing_debt**, **1/10 stop_created_debt**
  - Clamp contract check: `idle_slice=1` and `idle_bars=1` in **10/10** rows
- Delta vs pre-clamp reference [`225521`](../../captures/session_20260820_225521.log):
  - First post-stop `midi_gap` median **160.499 -> 108.276 ms**; max **506.253 -> 168.113 ms**
  - First post-stop `idle_maint` median **147.522 -> 108.244 ms**; max **498.002 -> 168.095 ms**
- Remaining synchronous stop owners in this run:
  - `seal_publish_span_cache_restamp`: **2.357-20.746 ms** (median **15.792 ms**)
  - `seal_fx_seal_pending_changes`: **2.709-23.835 ms** (median **17.319 ms**)

**P3/P4 re-measure after stop-owner trim ([`231845`](../../captures/session_20260820_231845.log), [`232951`](../../captures/session_20260820_232951.log), [`233633`](../../captures/session_20260820_233633.log), [`235214`](../../captures/session_20260820_235214.log)):**
- Sample shape in both captures: **12** stops = **9 published** + **3 skipped**.
- Published synchronous stop owners stay bounded:
  - [`231845`](../../captures/session_20260820_231845.log): `ODUB,stop,display` median **21.993 ms** (max **37.466 ms**), `seal_publish_span_cache_restamp` median **2.988 ms** (max **5.391 ms**), `seal_fx_seal_pending_changes` median **1.226 ms** (max **1.524 ms**).
  - [`232951`](../../captures/session_20260820_232951.log): `ODUB,stop,display` median **24.824 ms** (max **36.010 ms**), `seal_publish_span_cache_restamp` median **3.898 ms** (max **5.343 ms**), `seal_fx_seal_pending_changes` median **1.090 ms** (max **1.601 ms**).
- Remaining long-tail is deferred-window, not synchronous stop owner:
  - [`232951`](../../captures/session_20260820_232951.log) skipped stops: `ODUB,stop,display` **9.829-9.991 ms**, but first post-stop `DIAG,midi_gap` **595.365-603.237 ms**.
  - First post-stop unique windows in [`232951`](../../captures/session_20260820_232951.log): `midi_gap` median **134.773 ms**, max **603.237 ms**; `idle_maint` median **134.753 ms**, max **603.218 ms**; `persist_save` median **17.333 ms**, max **55.075 ms**.
- Debt causality remains classified and clamped (`ODUB,stop,debt_handoff`: **10 stop_created_debt**, **2 pre_existing_debt**; `idle_slice=1`, `idle_bars=1` in **12/12** rows).
- [`233633`](../../captures/session_20260820_233633.log) confirms the remaining long-tail owner is `idle_append` during post-stop PLAYING debt windows after `VCACHE,stale_all` (undo path), not synchronous stop publish owners:
  - `DIAG,midi_gap=850.101 ms` aligns with `DIAG,loop_rem,idle_append=797.766 ms` + `DIAG,loop_rem,idle_maint=844.487 ms`.
  - `DIAG,midi_gap=817.292 ms` aligns with `DIAG,loop_rem,idle_append=769.842 ms` + `DIAG,loop_rem,idle_maint=817.272 ms`.
- [`235214`](../../captures/session_20260820_235214.log) validates the Stage D arbitration + append guard on overdub windows:
  - Sample shape: **6** stops, **6 published**, **0 skipped**, `RING,overflow=0`.
  - First post-stop unique windows: `DIAG,midi_gap` **32.692-152.781 ms** (median **101.382 ms**), `idle_maint` **32.691-152.759 ms** (median **101.358 ms**), `persist_save` **0.001-0.188 ms** (median **0.001 ms**).
  - Compared with [`233633`](../../captures/session_20260820_233633.log): skipped windows removed (**2 -> 0**) and first post-stop gap max reduced (**850.101 -> 152.781 ms**).
- Remaining run-wide multi-second outliers in [`235214`](../../captures/session_20260820_235214.log) are not post-stop overdub windows: `DIAG,midi_gap` **1908-1910 ms** aligns with `loop_rem,boot_commit` **1632 ms** + `loop_rem,load_frame` **1642 ms** while state is `STOPPED`; keep this as separate non-overdub scheduling follow-up.
- **Stage D implementation scope (shipped):** defer `StorageManager::processDeferredSaveState` admission while post-overdub drain is active and deferred save work is pending, and skip wrap-pair append in that same stressed drain context, so `idle_maint` + `persist_save` + `idle_append` do not stack in one post-stop drain turn.

**Stage changes shipped:**  
- `applyPendingHideAndShortenToNotes` now applies `Shorten`/`Hide` transforms by rebuilding the note vector once, avoiding repeated in-place erase scans for each pending change.  
- `saveNoteEditPass` gained `deferDerivedInvalidate` for batch callsites.  
- `sealPendingNoteChangesToEditPasses` now defers derived invalidation during companion row inserts and performs a single `playbackRevision` + `notifyCommittedContentChanged` publish after the batch.
- Companion sealing now performs one heap-reserve admission check for the entire companion batch and skips repeated per-row heap checks once the batch is admitted.
- Companion sealing now reserves `passes.editPasses` capacity for the full companion batch before row insertion, removing vector growth churn from the stop path.
- Added one batch timing line per companion seal (`DIAG,seal_companion_batch`) so each stop cycle records rows/sealed/duration directly.
- Added Tier-A stop sub-stage timing lines (`DIAG,odub_stop`) for `companion_seal`, `undo_push`, `mark_range`, `publish_prepared`, and `persist_request` inside `finalizeCommitSideEffects`, so long-tail stop cycles can be attributed to one owner step even under ring pressure.
- Companion per-row telemetry is now capped (first 8 `DIAG,seal_companion` rows) to avoid line-format/append churn during heavy stop cycles while preserving batch-level timing visibility (`DIAG,seal_companion_batch`).
- `markAffectedDisplayCacheRanges` now marks companion row spans first, then does one note-cache pass against the companion target-note set, instead of scanning `visualCache.notes` once per companion row.
- `applyPendingNoteChangesToOverdubSourceView` Add-note merge batching is restored (user-directed) so pending Add notes are merged in one pass before `Shorten`/`Hide` projection updates.
- `applyPendingHideAndShortenToNotes` now keeps Shorten/Hide transforms sorted by `noteId` and uses bounded lookup, removing repeated linear transform scans from the overdub-stop seal path while preserving Shorten/Hide outcomes.
- Added `SESSION_CAPTURE`-gated stop publish telemetry for the previously uninstrumented overdub-stop publish owner (`Loop::commitPendingCapturePass`) with cold/Flash placement (`LOOP_COLD_MEM`): `seal_fx_publish_pending_pass`, `seal_publish_promote_pass`, `seal_publish_span_cache_restamp`, `seal_publish_finalize_state`, `seal_publish_clear_capture`.
- `commitPendingCapturePass` span-cache restamp now hard-bounds stop-path work: when the committed pass exceeds the stop restamp event budget (`2048` events), restamp is deferred (`seal_publish_span_cache_restamp=deferred_event_budget`) and revision restamp is skipped so heavy rebuild work moves out of the synchronous stop envelope.
- `commitPendingCapturePass` now skips full committed-pass note reconstruction when no `NoteOn` id is missing from the span cache, and reconstructs only when missing ids remain after fast eligibility filtering.
- `mergeDisplayNotesIntoOverdubSourceView` and overdub-source span-cache add paths now use sorted `NoteId` indices for bounded membership checks instead of repeated per-candidate linear scans.
- Added one-shot post-stop causality probe lines: `ODUB,stop,debt_handoff` plus `DIAG,odub_debt` on the first PLAYING idle-maintenance turn after overdub stop, classifying visual/save debt as `pre_existing_debt`, `stop_created_debt`, `stop_amplified_debt`, or `newly_scheduled_after_stop` using pre-stop vs post-stop dirty/pending snapshots.
- Causality capture [`224752`](../../captures/session_20260820_224752.log) confirms stop-created visual debt in the first PLAYING maintenance turn (`ODUB,stop,debt_handoff=stop_created_debt`, 10/10 stops). Follow-up scheduling trim: while `playingMidiDrainAfterOverdubStopActive()` is true, idle visual cache slices are capped to 1 bar and mixed-dirty long-loop fallback stays neighborhood-bounded (no `UINT32_MAX` distance override in that drain window).
- Drain-window clamp now applies to all tracks in the same loop turn (via `main()` `postOverdubPlayingMidiDrain` -> `Track::processDeferredIdleMaintenance(..., postOverdubDrainWindowActive)`), so non-owner tracks cannot run broader PLAYING idle visual slices while a post-overdub drain window is active.
- Drain-window policy now restricts PLAYING idle visual maintenance to the overdub-stop owner track while the post-overdub drain window is armed; non-owner tracks defer their visual idle slices until the owner drain window clears, preventing unrelated pre-existing debt from consuming the same first post-stop MIDI turn.
- Behavior scope guard: this drain-window policy is stop-only. It is armed by `stopOverdubbing` (`armPlayingMidiDrainAfterOverdubStop`) and reset by `startOverdubbing`; the clamp gate applies only to PLAYING/STOPPED_RECORDING maintenance. OVERDUBBING and overdub-wrap commit paths keep normal note-update processing (`commitOverdubWrapAtSessionStart` still seals companions with source-view update enabled).
- Added post-stop arbitration gate in `main()` via `MidiServiceDrain::allowDeferredSave`: while the post-overdub drain window is active and deferred save work is pending, `StorageManager::processDeferredSaveState` is deferred for that loop turn so `idle_maint` and `persist_save` do not stack in the same drain-window slot.
- Added post-stop visual slice append guard in `Track::processDeferredIdleMaintenance`: for the full post-overdub drain window, `Loop::rebuildVisualCacheIdleSlice` runs without wrap-pair append (`allowOverdubWrapAppend=false`) so expensive `idle_append` work does not stack into stressed post-stop runtime windows.
- Added constrained fallback gather mode for the full post-overdub drain window: `Loop::rebuildVisualCacheIdleSlice(..., constrainFallbackGather=true)` removes pad bars and skips active edit-row projection in fallback gather windows, so post-stop fallback slices stay bounded when prepared windows are unavailable.
- Added no-progress constrained-slice backoff in `Track::processDeferredIdleMaintenance`: when a constrained fallback slice does not reduce dirty bars, the next constrained slice is deferred for `250 ms` (`overdubDrainFallbackRetryAtMs_`) to prevent repeated high-cost fallback churn in adjacent loop turns.
- Added non-overdub STOPPED follow-up optimization in `StorageManagerInternal::enqueueRemainingLoopSlotRestores`: boot background-fill now reuses boot-scanned in-RAM slot payload flags and skips per-slot SD reprobe during full enqueue, removing one-turn `SD.exists`/token-scan churn from `boot_commit`/`load_frame`.
- Further optimized `enqueueRemainingLoopSlotRestores` bulk admission: per-entry priority computation and per-entry queue sort are now deferred during full boot enqueue, then applied once via final `reprioritizeDeferredLoopSlotRestoreEntries()`, reducing boot-commit scheduler churn without changing restore ordering semantics.
- `enqueueRemainingLoopSlotRestores` now admits background-fill queue entries incrementally across loop turns (`kBootAdmissionEntriesPerTurn=4`) and only finalizes reprioritize/sort after admission completes, so STOPPED boot does not pay full remaining-slot admission in one `boot_commit` / `load_frame` window.
- Background `LoadLoopJob` stepping now enforces a transport-clock-safe resume policy: Low (non-focus) load jobs are disallowed while global transport clock is running and are parked/deferred for retry later, so slot-load catch-up cannot compete with active MIDI timing service.
- STOPPED deferred-maintenance scheduling now uses cancel/resume policy in `main()`: when all tracks are STOPPED, run `Track::processDeferredIdleMaintenance` for one track per turn (round-robin), and skip STOPPED maintenance entirely while global transport clock is running.
- Realtime visual-cache slices now defer wrap-pair append (`idle_append`) while playback/capture states are active; deferred append is resumed in STOPPED idle (`Loop::applyDeferredVisualCacheWrapAppend`) so MIDI service keeps priority during PLAYING/OVERDUBBING while append work restarts later.
- Realtime visual-cache gather restart cooldown (`activeVisualSliceRetryAtMs_`) was rolled back after reboot evidence in [`012714`](../../captures/session_20260821_012714.log) to remove the newest strict throttle while retaining append defer/resume and post-overdub drain-window guards.
- New capture attribution from [`010055`](../../captures/session_20260821_010055.log): the two ~500 ms active-window outliers are deferred-maintenance owners, not stop-path owners — `DIAG,midi_gap=534.305 ms` aligns with `DIAG,idle_maint=519.178 ms`, and `DIAG,midi_gap=564.640 ms` aligns with `DIAG,idle_maint=535.469 ms` + `DIAG,playback_build=85.417 ms`.
- Follow-up capture [`011853`](../../captures/session_20260821_011853.log) confirms active-window outliers moved away from `idle_append`: the remaining ~500 ms windows align with `idle_gather` + `idle_maint` (`midi_gap=582.155 ms` with `idle_maint=567.506 ms`; `midi_gap=582.887 ms` with `idle_maint=568.300 ms`), while the only >500 ms `idle_append` (`949.485 ms`) occurs in STOPPED with immediate `VCACHE,append_resume`.
- Added resumable window traversal in `CommittedEventRange::appendWindowBudgeted` (cursor + per-slice event budget) and rewired fallback gather in `Loop::rebuildVisualCacheIdleSlice` to use a Loop-owned gather session keyed by `(windowStart, windowLength, playbackRevision, includeActiveEditRows)`.
- Fallback gather now has explicit invalidation semantics: any window/revision/mode mismatch resets the session; cache invalidation/rebuild paths call `resetFallbackGatherSession`; incomplete slices return before reconstruction so runtime turns stay bounded.
- Added active/stopped fallback budgets in `Track::processDeferredIdleMaintenance` (`96` events during post-overdub drain, `192` during other realtime maintenance, `768` in STOPPED idle) and capture diagnostics (`DIAG,lcr,vch,gather_progress|gather_sort|gather_edit`) for phase-cost attribution.
- RAM1 remained within budget after this slice by reusing cold sort helpers and removing additional `std::sort` template instantiations from the realtime path (`teensy41-capture-serial` locals headroom restored).
- Validation capture [`015645`](../../captures/session_20260821_015645.log) confirms bounded overdub-stop owners and isolates one active fallback-gather churn outlier:
  - Overdub stop (7 published cycles): `ODUB,stop,display` **20.700-33.276 ms** (median **25.352 ms**), `ODUB,stop,seal` **11.567-21.813 ms** (median **14.569 ms**), `seal_publish_span_cache_restamp` **3.410-5.500 ms**, `seal_fx_seal_pending_changes` **1.310-1.935 ms**.
  - Active (`PLAYING`/`OVERDUBBING`) `DIAG,midi_gap`: **28.734-827.234 ms** (median **34.755 ms**); single long outlier `827.234 ms` maps to `DIAG,idle_maint=826.425 ms` + `DIAG,load_frame=23.535 ms` in `PLAYING` (`#CAP,31432460` report block).
  - Outlier window proof (`#CAP,30217106` → `#CAP,31417617`): **122** consecutive `DIAG,lcr,vch,gather_progress,ev=192,total=192,done=0,resume=0` lines immediately precede the outlier, with no `gather_sort`/`gather_edit` completion in that suffix, proving active-window fallback gather restart/no-resume churn under the current session key.
- Follow-up owner fix attempt (in-flight session window hold) was reverted in the same session after startup instability report; keep active behavior on the last known-stable fallback-session semantics while root-causing a safer anti-reset strategy.
- `applyNoteEditPassSequence` now prefilters `Update/Delete` rows by currently present target `NoteId`s while preserving row order, and maintains the present-id set through `Create/Delete` so fallback gather avoids repeated no-op target scans in dense active edit-row sessions.
- `applyNoteEditPassSequence` Phase A planner is now active with strict barrier semantics: only contiguous `Update` rows for the same `targetNoteId` + same property collapse, and only for `Velocity`/`Pitch` updates; any other row remains an execution barrier.
- Added executor-equivalence coverage for the Phase A planner in `test_edit_apply` (`test_sequence_phasea_collapse_contiguous_velocity_and_pitch_equivalence`, `test_sequence_phasea_barrier_with_length_keeps_equivalence`, `test_sequence_phasea_randomized_equivalence_against_baseline`), validated against the non-collapsing baseline executor over deterministic randomized scenarios.
- `applyNoteEditPassSequence` and `planContiguousCollapsedRows` are now `NOTE_EDIT_MEM` to keep Phase A planner logic out of RAM1/ITCM on Teensy (`teensy41-capture-serial` locals headroom preserved).
- Phase B cache slice shipped in `applyNoteEditPassSequence`: added per-sequence NoteOn index reuse (`NoteOnIndexLookupCache`) with conservative invalidation semantics. Cache entries are positional indices and are cleared after structural row actions (`Create`, `Delete`, `NoteRange`, `Length`) and after any cache-stale skip path, so reuse is limited to ordering-safe windows (`Pitch`/`Velocity` updates and unchanged vector topology).
- Internal note-apply helpers now use cache-aware lookup (`findNoteOnByIdCached`) while preserving public behavior (`findNoteOnById` remains linear and unchanged for external callers); structural helpers also clear cache after in-function erase/sort paths (`applyDeleteNoteById`, `applyMoveNoteById`, `applyChangeLengthById`, `applyAddNote`).
- Added Phase B targeted equivalence coverage in `test_edit_apply` (`test_sequence_phaseb_length_then_same_note_update_equivalence`, `test_sequence_phaseb_length_with_interleaved_note_updates_equivalence`) to pin length-mutation invalidation safety before and after interleaved note updates.
- Validation capture [`082858`](../../captures/session_20260821_082858.log) confirms stop owners remain bounded after Phase A while active fallback edit work remains the outlier owner:
  - Overdub stop (11 published cycles): `ODUB,stop,display` **17.031-28.116 ms** (median **22.602 ms**), `ODUB,stop,seal` **9.855-17.730 ms** (median **14.207 ms**), `seal_publish_span_cache_restamp` **3.292-6.048 ms**, `seal_fx_seal_pending_changes` **0.440-1.999 ms**, `seal_fx_mark_display_ranges` **3.189-6.092 ms**.
  - Active (`PLAYING` + `OVERDUBBING`) `DIAG,midi_gap`: **29.052-856.062 ms** (median **73.630 ms**). Peak active report `#CAP,90758349` maps to `DIAG,idle_maint=854.985 ms` and `DIAG,lcr,vch,gather_edit,us=826242`.
  - Second active outlier `#CAP,95758554` (`midi_gap=600.711 ms`) maps to `DIAG,idle_maint=585.338 ms` with concurrent deferred work (`persist_save=11.345 ms`).
  - Phase A reduces worst `gather_edit` from **1,083,993 us** (`015645`) to **826,242 us** (`082858`), but `gather_edit` remains large enough to dominate active runtime stalls; cache-oriented Phase B remains required.
  - Non-active outliers stay outside the overdub-stop owner boundary (`midi_gap=23,391,338 us` at `#CAP,35316786` aligns with boot/load `loop_rem,boot_commit` and `loop_rem,load_frame` in STOPPED startup windows).
- Validation capture [`084213`](../../captures/session_20260821_084213.log) confirms the Phase B cache slice shifts fallback edit cost out of active runtime windows:
  - Overdub stop (10 published cycles): `ODUB,stop,display` **17.509-47.266 ms** (median **23.202 ms**), `ODUB,stop,seal` **9.558-36.016 ms** (median **13.686 ms**), `seal_publish_span_cache_restamp` **2.826-5.960 ms**, `seal_fx_seal_pending_changes` **1.149-2.038 ms**.
  - Active (`PLAYING` + `OVERDUBBING`) `DIAG,midi_gap`: **26.828-79.941 ms** (median **37.399 ms**, 21 samples), down from `082858` active max **856.062 ms** and median **73.630 ms**.
  - The two largest `DIAG,lcr,vch,gather_edit` events (`1,070,494 us` at `#CAP,164959999`; `464,307 us` at `#CAP,163097840`) occur in `STOPPED` state and map to `DIAG,loop_rem,idle_gather` / `DIAG,loop_rem,idle_maint`, not active MIDI-service windows.
  - The largest active `midi_gap` windows (`#CAP,90939663` at **79.941 ms**, `#CAP,75906773` at **78.345 ms**) show `persist_save` + `load_frame` stacking while `DIAG,idle_maint` stays low (about **5.3 ms**), and do not include active `gather_edit` outliers.
  - One stop-cycle display outlier (`ODUB,stop,display=47.266 ms`) is attributed to synchronous stop sub-stage `seal_fx_mark_display_ranges=26.886 ms` in the same cycle (`#CAP,111344311` series).
- Validation capture [`085458`](../../captures/session_20260821_085458.log) shows stop-path owners remain bounded but active runtime regressed back to idle-maintenance ownership:
  - Overdub stop (7 published cycles): `ODUB,stop,display` **20.501-33.291 ms** (median **24.063 ms**), `ODUB,stop,seal` **11.776-21.663 ms** (median **13.630 ms**), `seal_publish_span_cache_restamp` **4.019-6.123 ms**, `seal_fx_seal_pending_changes` **0.621-2.038 ms**.
  - Active (`PLAYING` + `OVERDUBBING`) `DIAG,midi_gap`: **23.825-2,893.664 ms** (median **36.546 ms**, 18 samples), vs `084213` active max **79.941 ms**.
  - Largest active windows map to `idle_maint` (not persist-save stacking): `#CAP,44306112` `midi_gap=2,893.664 ms` with `idle_maint=2,864.114 ms`, `load_frame=30.239 ms`, `persist_save=0.234 ms`; `#CAP,44282997` `midi_gap=2,865.744 ms` with `idle_maint=2,864.092 ms`, `load_frame=26.220 ms`, `persist_save=0.197 ms`.
  - OVERDUBBING outliers also map to fallback gather/edit cost: `#CAP,60224592` `midi_gap=842.661 ms` aligns with `idle_maint=840.200 ms`; immediately preceding owner lines show `DIAG,lcr,vch,gather_edit,us=813088` (`#CAP,58974437`) and `DIAG,loop_rem,idle_gather=817.671 ms`.
  - Additional active gather/edit spikes are present in OVERDUBBING (`gather_edit` up to **813.088 ms** and **753.577 ms**) with paired `loop_rem,idle_gather` / `loop_rem,idle_maint` windows, confirming the remaining owner is active fallback gather-edit churn.
- Active owner follow-up shipped: `main()` now gates `StorageManager::processDeferredSaveState` behind same-turn `load_frame` duration (`MidiServiceDrain::allowDeferredSaveAfterLoadFrame`) so timing-critical turns defer save when `load_frame >= 15 ms`; existing post-overdub drain-window save gate remains in effect. Native + `teensy41-capture-serial` build passed; recapture pending.
- Overdub-stop companion sealing now skips source-view mutation in the stop owner path (`sealPendingNoteChangesToEditPasses(!overdubStop)`), preserving the same persisted companions while avoiding source-view/spancache projection work during synchronous stop handoff.
- `sealPendingNoteChangesToEditPasses` is explicitly `LOOP_COLD_MEM` so this stop-only companion owner stays out of RAM1/ITCM budget.
- `commitPendingCapturePass` wrap-pair restamp guard now prefilters with head/tail window key sets before fallback scan, and missing-id collection batches then deduplicates once before reconstruction, reducing restamp bookkeeping churn in `seal_publish_span_cache_restamp`.
- Wrapped head/tail detection during span-cache restamp now uses bounded linear scan without the previous 17x128 stack table, removing large fixed stack footprint from the overdub-stop publish owner.
- `Loop::commitCapturePass`, `Loop::sealCapture`, and `Loop::finalizeCaptureWrapWindowAtStop` are explicitly `LOOP_COLD_MEM` to keep overdub-stop owner code on flash and restore RAM1 headroom in `teensy41-capture-serial`.
- `Track::stopOverdubbing` and the in-edit `handleNoteEditFold` stop path now use `SC_REC_FLUSH_PENDING_REVTS(128)` with Tier-A ODUB stop markers, preserving stop-stage observability without reintroducing broad flush churn in the synchronous owner path.
- `CaptureLineTier::isTierALine` now treats `ODUB,stop` lines as Tier-A so stop-stage telemetry remains visible even with deterministic `flush(8)` stop budgets under ring pressure.
- Companion-only projection fast path shipped in `Loop` window consumers: `copyEffectiveCommittedEventsInRange` and `advanceFallbackGatherSession` copy a ready Overdub companion cache only. A miss, including an empty projection, uses windowed apply. Full companion rebuild is not admitted from gather (Stage A; [`162928`](../../captures/session_20260821_162928.log)).

**Status:** Native **1399/1399** and `teensy41-capture-serial` build **PASS**. New baseline capture [`210319`](../../captures/session_20260820_210319.log) replaces older stop-lag envelopes for planning.

### Overdub-start begin-capture long-delay restoration — validated

**Evidence:** [`165534`](../../captures/session_20260820_165534.log), [`165956`](../../captures/session_20260820_165956.log)

**Owner:** `Loop::rebuildOverdubSourceSpanCache`, `Track::startOverdubbing` (`ODUB,stage,begin_capture`).

**Invariant:** Overdub start stays deterministic while an overdub session is active: if prepared spans are unavailable, start/wrap does not force a full-loop materialize on the timing-critical path.

**Root cause proved in captures:** `ODUB,stage,begin_capture` spiked to about **2.2 s** (`2194189 us`, `2248843 us`) in [`165956`](../../captures/session_20260820_165956.log), and `Live Overdub -> Overdubbing started` was delayed by **2143-2249 ms** on early starts. The same pattern appears in [`165534`](../../captures/session_20260820_165534.log) (~2102/2189 ms). This directly maps to `rebuildOverdubSourceSpanCache` falling back to `passes.materializeToEventVector` when prepared spans are unavailable.

**Stage change shipped:** Restored overdub-session cache fallback in `rebuildOverdubSourceSpanCache`: when prepared spans are unavailable during an active overdub session, reuse prior cache notes instead of forcing full-loop materialization on start/wrap. Non-session behavior remains unchanged (full materialize is still allowed outside active overdub sessions).

**Status:** Native **1397/1397**. `teensy41-capture-serial` build **PASS**. New baseline [`210319`](../../captures/session_20260820_210319.log): `ODUB,stage,begin_capture` stays bounded at **1851-6443 us** (median **2070 us**), confirming start stalls are no longer the active bottleneck.

**Historical outlier context:** [`170724`](../../captures/session_20260820_170724.log) contains a one-cycle stop-path outlier (`ODUB,stop,seal=310980 us`, `display=320406 us`). The new planning baseline is [`210319`](../../captures/session_20260820_210319.log) runtime windows above (published stop display 56.451-71.803 ms; first post-stop MIDI Input Gap 78.634-145.373 ms).

### Overdub-start reboot — reverted to baseline, blocked on instrumentation

**Evidence:** [`162146`](../../captures/session_20260820_162146.log), [`132145`](../../captures/session_20260820_132145.log), [`143518`](../../captures/session_20260820_143518.log)

**Owner:** `Track::startOverdubbing`, `TrackManager::startOverdubbingTrack`, `DebugSessionCapture` capture ring.

**Symptom:** USB drops immediately after `[TRACK] Overdubbing started @ tick N`, the final statement of `Track::startOverdubbing`. Reproduces on first and second overdub start.

**Not caused by the 2026-08-20 removals.** The reboot is present in [`132145`](../../captures/session_20260820_132145.log) and [`143518`](../../captures/session_20260820_143518.log), before any of them. Commits `4af158a`, `39b8e1d`, `983262d`, `98699b6`, `6ff4142` are reverted; the tree is byte-identical to `28cf6e3`. Pass accumulation is also ruled out — the first capture of the day already reached `pass,99` and the last reached `pass,101`.

**Capture blind spot (why eight successive fixes all "made no difference"):** two independent mechanisms starve overdub telemetry out of the ring.

| Mechanism | Owner | Effect |
|---|---|---|
| Flush budget `<= 8` takes the drop-only branch | `DebugSessionCapture::flushCaptureBuffer` | Discards up to 8 non-Tier-A head records and returns without transmitting. The overdub `SC_REC_FLUSH_PENDING_REVTS(8)` sites delete telemetry instead of sending it. |
| Tier-B append refused while ring is full and head is Tier-A | `DebugSessionCapture::appendCaptureRecord` | `ODUB,stage`, `MI`, `MO`, `LED` never enter the ring. `DIAG,lcr,` and `VCACHE,` are Tier-A per `CaptureLineTier::isTierALine`. |
| `formatPhaseLine` 1 s rate limit never engages | `LoopContentResolution` device-gate slice machine | `stepChanged` is true on every slice because the machine alternates `idx` ↔ `pair`, so the Tier-A `DIAG,lcr,phase` line emits per slice, not per second. |

In [`162146`](../../captures/session_20260820_162146.log) the last `#CAP,…,MI,…` is at 9.387 s and the last `LED` at 14.89 s, while `DIAG,lcr,phase` keeps emitting to 33.38 s (355 lines). The record button press at 35.198 s produced a text log line but **no** `MI` capture line, so Tier-B appends were already being refused when overdub started. `ODUB,stage,manager_enter` / `manager_done` are therefore absent for instrumentation reasons, not because that code did not run.

**Fault reason never observed.** `setup()` prints `CrashReport` before `logger.setup(LOG_DEBUG)`, and every reconnect boot in the 2026-08-20 captures begins at `Logger initialized with level: 3`. The pre-logger boot output is absent from all of them. The firmware also writes `CrashReport` to `crashlog.txt` on SD (`main.cpp` `setup()`); that file is the authoritative record and has not been read.

**Next:** read SD `crashlog.txt` to classify the fault (allocator abort vs hard fault vs watchdog) before any further code change. Do not attribute the reboot to a removal again without an `ODUB,stage` line or a crash record.

**Status:** Native **1397/1397**. `teensy41-capture-serial` RAM1 code **424572** / locals **4768**. Not flashed.

### Cleanup branch telemetry/fader compile-gating — shipped stage

**Branch:** `chore/cleanup-codebase-tidiness`

**Owner:** `MidiHandler::mirrorUsbFaderProbePassthrough`, `RuntimeTimingTelemetry` callsites, overdub stop telemetry callsites.

**Invariant:** Plain `teensy41` builds do not execute telemetry-only timing work (`micros()`/heap snapshots/local telemetry structs) when the owning capture or perf feature is disabled; fader USB-host helper logic is compiled only with `MIDI_USB_FADER_PROBE_PASSTHROUGH`.

**Status:** **Verified** `pio test -e native`, `pio run -e teensy41`, `pio run -e teensy41-capture-serial` with zero compiler warnings.

### Overdub session index reboot undo — HITL PASS

**Plan:** [`overdub_session_index_reboot_undo_bugfix.md`](../Plans/overdub_session_index_reboot_undo_bugfix.md)  
**Evidence:** [`003854`](../../captures/session_20260820_003854.log)

**Owner:** `Loop::openOverdubSession`, `deriveContentUndoUnits`, `StorageLoopIo` `OSI1`.

**Invariant:** One overdub session is one `OverdubPassAdded` before and after reboot. Wraps share `overdubSessionIndex` (same grouping role as `editPassIndex`). Missing `OSI1` stays one unit per pass.

**Status:** **HITL PASS** [`003854`](../../captures/session_20260820_003854.log). Native **1397/1397**. RAM1 code **425548** / locals **4768**. Three wraps + stop; reboot `Undo (entries=2)` `kind=1` `undo_count=179`; `DISP` 112→14; redo restores 112.

### Overdub-stop handoff flash

**Plan:** [`display_overdub_stop_handoff_flash_bugfix.md`](../Plans/display_overdub_stop_handoff_flash_bugfix.md)  
**Evidence:** [`000553`](../../captures/session_20260820_000553.log)

**Owner:** `DisplayManager::resolveDisplayNotesCommitted`, `preferPreservedOverdubStopHandoff`.

**Invariant:** While `visualCacheDirty` after overdub stop, the revision-matched composed frame stays paint authority. Follow re-filters that frame; it does not replace it with pre-commit `visualCache.notes`.

**Status:** **HITL PASS** [`001925`](../../captures/session_20260820_001925.log). Native **1391/1391**. RAM1 code **425276** / locals **4768**. Evidence [`000553`](../../captures/session_20260820_000553.log) remains the failing baseline (655→583 at `vis=2166`).

### Display window follow readiness

**Plan:** [`display_window_follow_readiness_bugfix.md`](../Plans/display_window_follow_readiness_bugfix.md)  
**Evidence:** [`232337`](../../captures/session_20260819_232337.log)

**Owner:** `DisplayManager::resolveDisplayNotesCommitted`, `resolveDisplayNotesLiveCapture`, `resolveWindowedDisplayNotes`, `visualCacheCoversWindow`.

**Invariant:** Long-loop committed paint filters `visualCache.notes` for the paint window plus follow margin/lookahead. A dirty cache does not keep a 16-bar `overdubSourceViewNotes` frame as display authority.

**Status:** **HITL PASS** [`000553`](../../captures/session_20260820_000553.log) — `verify_follow_window_readiness` ok (0 stale holds). Native **1390/1390**. RAM1 code **425276** / locals **4768**. Evidence [`232337`](../../captures/session_20260819_232337.log) remains the failing baseline.

### Overdub participant discovery (occupy / source-view RC closed)

Occupy source-view resolver geometry ([`overdub_occupy_source_view_keeps_resolver_geometry_bugfix.md`](../Plans/overdub_occupy_source_view_keeps_resolver_geometry_bugfix.md)) and wrap display D2-D ([`overdub_wrap_source_view_display_drops_committed_bugfix.md`](../Plans/overdub_wrap_source_view_display_drops_committed_bugfix.md)) are **FROZEN** — HITL [`174246`](../../captures/session_20260819_174246.log). Native **1384/1384**. Next on this branch: 64-bar `from=span` HITL **parked** (DEC-041); see § Parked below.

### Overdub ledger completion — shipped (Stages 2–4)

**Plan:** [`overdub_ledger_completion_enhancement.md`](../Plans/overdub_ledger_completion_enhancement.md)

**Owner:** `Loop::ensureOverdubSourceNotesForHold`, `Loop::accumulatePendingNoteChangesForIncomingNote`.

**Invariant:** Source-view rebuild and long-loop note-off hold hydration both read the source span cache (`from=cache`) with prepared spans when available and full-loop resolved spans otherwise; source-view path no longer calls `resolveWindow`.

**Status:** Native **1387/1387**. Baseline cost anchor [`202256`](../../captures/session_20260819_202256.log) (32× `from=win`, `merged=0`). Device check [`221834`](../../captures/session_20260819_221834.log): hold path `from=cache` with no `hold,miss`. Overdub-entry stall RC: companion sealing now restamps source-span cache revision only for active overdub sessions with full companion seal success, preventing stale-stamp rebuilds on the next overdub entry while preserving non-session rebuild behavior. Cold-start mitigation originally called `Loop::prewarmOverdubSourceSpanCache` from STOPPED idle after visual cache cleanup. `754db31` removed that caller because same-turn prewarm after `slice_clean` hung [`160519`](../../captures/session_20260820_160519.log). Stage B restores the caller on a later clean-cache turn after `boot_commit`. Device PASS [`165107`](../../captures/session_20260821_165107.log) after [`162928`](../../captures/session_20260821_162928.log) opened `notes=0` without it.

### Consume id-resolution completeness — FROZEN (Stages 1–2)

**Plan:** [`overdub_consume_id_resolution_completeness_bugfix.md`](../Plans/overdub_consume_id_resolution_completeness_bugfix.md)

**Owner:** `Loop::accumulatePendingNoteChangesForIncomingNote`. All holds → `effectiveOverlapNoteIds` (incoming ids + geometric union + long-loop hold fill), then `appendNotesForIds` only. `collectConsumeWindow` **removed**.

**Invariant:** Every geometric consume participant resolves via `appendNotesForIds`; no window scan.

**Status:** Native **1387/1387**. HITL Stage 1 [`195016`](../../captures/session_20260819_195016.log); Stage 2 [`201457`](../../captures/session_20260819_201457.log): zero `DIAG,consume` / `scanadd` / `norow`; occupy **86/86** `eq=1`. RAM1 code **425964** / locals **4768**.

**Closed predecessor:** [`overdub_consume_ledger_merge_enhancement.md`](../Plans/overdub_consume_ledger_merge_enhancement.md) Stage 1A/1B attribution — selection-change rejected on [`191133`](../../captures/session_20260819_191133.log).

### Consume candidate attribution — Stage 1A/1B COMPLETE (observability only, superseded)

**Plan:** [`overdub_consume_ledger_merge_enhancement.md`](../Plans/overdub_consume_ledger_merge_enhancement.md)

**Owner:** `Loop::accumulatePendingNoteChangesForIncomingNote`. Selection behavior **unchanged** — this does **not** start the parked consume merge.

**Invariant:** consume candidate selection is measured, not changed: every candidate is attributed to the occupy-id lookup, the window scan, or late JIT materialization.

**Status:** Counters + `DIAG,consume,select` shipped. **1B detail diagnostics shipped:** `DIAG,consume,norowid` (ids that failed to resolve to a source-view row) and `DIAG,consume,scanadd` (scan-added candidate ids with `in_ids=0|1`). **Consume merge selection change REJECTED** — HITL [`191133`](../../captures/session_20260819_191133.log): 3 of 5 attributed holds had a **non-empty** occupy set where the window scan was the only path to a participant (2× `idsel=1`, 1× `norow=1`). Follow-up HITL [`193024`](../../captures/session_20260819_193024.log): `norow` did not reproduce (`norow=0`, `norowid=0`), while `scanadd` persisted (18 lines, all `in_ids=0`). Empty-ids fallback remains load-bearing. Native **1386/1386**. RAM1 code **425964** / locals **4768**.

**Stage 1A guard check:** `overlapNoteIds` is written by `snapshotOverlapHoldCandidates` (open at `holdStart`) **and** `collectOverlapHoldPlaybackNoteOn` (NoteOns during the hold, from `sendMidiEvent` before the `playbackEmitMidiOutput_` gate). `appendNotesForIds` cannot reach a note absent from `overdubSourceViewNotes_`, so the long-loop JIT branch is the only path to a JIT-merged ahead note — proven by `test_consume_attribution_counts_late_note_for_jit_ahead_candidate`. Blocking additive candidates on non-empty ids therefore changes long-loop consume, which a 1-bar gate cannot observe.

**Long-loop JIT class did not reproduce:** `why=hold` **140** with long loops present (`live=18432`, `live=52224`), yet every line is `jit=0` / `late=0`. Reachable in native, not the device problem.

**Closed 2026-08-19.** Stage 1A delivered its purpose: the attribution evidence decided the selection question. Stages 1 and 2 (selection change) are **rejected** — do **not** block additive scan candidates on non-empty ids. The `norow` class is **tagged for investigation** in § Parked, no plan opened.

### Occupy source view keeps resolver geometry — FROZEN

**Plan:** [`overdub_occupy_source_view_keeps_resolver_geometry_bugfix.md`](../Plans/overdub_occupy_source_view_keeps_resolver_geometry_bugfix.md)  
**Closed:** 2026-08-19 — **Gate 5A** shipped; extra covering `a>n` **0** [`161349`](../../captures/session_20260819_161349.log). Display/source-view fix: sibling D2-D [`174246`](../../captures/session_20260819_174246.log). **Gate 5B withdrawn.** Do not reopen without new `a>n` or pin [`121141`](../../captures/session_20260819_121141.log) `n=1 a=2`.

### Overdub wrap display drop — FROZEN

**Plan:** [`overdub_wrap_source_view_display_drops_committed_bugfix.md`](../Plans/overdub_wrap_source_view_display_drops_committed_bugfix.md)  
**Closed:** 2026-08-19 — D2-D revision-freshness guard in `committedPlaybackNoteOnIdentityValid`. HITL [`174246`](../../captures/session_20260819_174246.log): DISP monotonic, `identity_invalid` **0**, ledger **95/95**. Lane **N** diagnostic only. Layer 1 `prep=0` [`170838`](../../captures/session_20260819_170838.log) **parked**.

### Occupy missing open identity (`n=1 a=2` extra covering span)

**Plan:** [`overdub_occupy_missing_open_identity_bugfix.md`](../Plans/overdub_occupy_missing_open_identity_bugfix.md)  
**Parent:** [`overdub_occupy_leftover_identity_bugfix.md`](../Plans/overdub_occupy_leftover_identity_bugfix.md) — leftover HITL **PASS** [`111819`](../../captures/session_20260819_111819.log)  
**Pin:** [`111819`](../../captures/session_20260819_111819.log) L948 pitch 24 `hs=72` `n=1 a=2` covering 5893 `0–743` + 5901 `0–168`

**Invariant:** A source-view / prepared identity counts as present-at-hold only if that `noteId` has a NoteOn in the complete committed playback stream.

**Status:** Native **PASS** 1374/1374. RAM1 **425948** / **4768**. HITL [`121141`](../../captures/session_20260819_121141.log): 5893-class **MET**. Remaining `n=1 a=2` **2 FAIL** — extra covering ids **have** a merged NoteOn (`on=1`). This RC’s root cause is invalid for those FAILs. **STOP** — do not widen the identity filter. Successor: source-view resolver geometry above.

**Does not reopen:** occupy catching up when `occupyPhase <= lastTick`; `playMidiEvents` from occupy; advancing `lastTick` from USB; Off stamping; FIFO; option B; leftover ledger erase; `isPlaybackCatchUpWindow` equal-tick contract.

### Occupy leftover identity after rematerialize

**Plan:** [`overdub_occupy_leftover_identity_bugfix.md`](../Plans/overdub_occupy_leftover_identity_bugfix.md)  
**Parent:** [`overdub_occupy_clock_duplicate_off_ledger_bugfix.md`](../Plans/overdub_occupy_clock_duplicate_off_ledger_bugfix.md) — HITL [`104654`](../../captures/session_20260819_104654.log) duplicate-Off apply **not** this FAIL  
**Pin:** [`104654`](../../captures/session_20260819_104654.log) 19× `n=1 a=0` pitch 12 `lid=5701` `lst=96`

**Invariant:** An open ledger identity is retained iff its NoteOn identity exists in a complete rebuilt committed playback stream.

**Native:** **PASS** 1371/1371. RAM1 **425932** / **4768**.  
**HITL [`111819`](../../captures/session_20260819_111819.log):** leftover **MET**. `n=1 a=0` **0**. `n=0 a=1` **0**. `led == n` **1/1**. `5701` gone. 11 `ledger,erase`. Remaining 1 mismatch is `n=1 a=2` pitch 24 `hs=72` (two covering spans, one Entry) — not this FAIL.

**Does not reopen:** occupy catching up when `occupyPhase <= lastTick`; `playMidiEvents` from occupy; advancing `lastTick` from USB; Off stamping; FIFO; option B; clearing the whole ledger; `isPlaybackCatchUpWindow` equal-tick contract; duplicate-Off ledger apply.

### Occupy clock duplicate Off skips ledger

**Plan:** [`overdub_occupy_clock_duplicate_off_ledger_bugfix.md`](../Plans/overdub_occupy_clock_duplicate_off_ledger_bugfix.md)  
**Parent:** [`overdub_occupy_duplicate_open_identity_bugfix.md`](../Plans/overdub_occupy_duplicate_open_identity_bugfix.md) — HITL [`103234`](../../captures/session_20260819_103234.log) `led == n` **MET**  
**Pin:** [`103234`](../../captures/session_20260819_103234.log) L6674 pitch 24 `n=3 a=2`; two `Off@71`

**Invariant:** A second committed NoteOff (or NoteOn) at the same phase, pitch, and type still applies to `ActiveNoteLedger`; only the MIDI wire is deduped.

**HITL [`104654`](../../captures/session_20260819_104654.log):** `led == n` **41/41**. `n=0 a=1` **0**. Pin two-Off dumps **0**. Leftover counts **FAIL**: `n=1 a=0` 18→19 (all pitch 12 `lid=5701` `lst=96`); `n=3 a=2` 1→2 (nested On@240 pair + leftover, not two Off@71). `n=2 a=1` 20→10. Do not reopen this apply. Successor: leftover identity above.

**Does not reopen:** occupy catching up when `occupyPhase <= lastTick`; `playMidiEvents` from occupy; advancing `lastTick` from USB; Off stamping; `rebuildPlaybackOrder`; duplicate `noteOn` no-op push.

### Occupy duplicate open identity (catch-up then clock)

**Plan:** [`overdub_occupy_duplicate_open_identity_bugfix.md`](../Plans/overdub_occupy_duplicate_open_identity_bugfix.md)  
**Parent:** [`overdub_occupy_catchup_open_note_stack_bugfix.md`](../Plans/overdub_occupy_catchup_open_note_stack_bugfix.md) — HITL [`101319`](../../captures/session_20260819_101319.log) extra-open remains  
**Pin:** [`101319`](../../captures/session_20260819_101319.log) L2318 `n=2` `led=4`

**Invariant:** An open playback identity occupies at most one ledger Entry. A second NoteOn with that `noteId` does not push; `applyPlaybackEvent` still returns true so clock can emit.

**HITL [`103234`](../../captures/session_20260819_103234.log):** `led == n` **42/42 MET**. `n=0 a=1` **0**. Extra unique ids remain (`n=2 a=1` 25→20). `n=1 a=0` rose 3→18 — successor above (clock duplicate Off skips ledger), not exclusive-end. All mismatches `cu=0`.

**Does not reopen:** occupy catching up when `occupyPhase <= lastTick`; `playMidiEvents` from occupy; advancing `lastTick` from USB; Off stamping; `rebuildPlaybackOrder`.

### Occupy catch-up per-phase Off then On (open-NoteOn stack)

**Plan:** [`overdub_occupy_catchup_open_note_stack_bugfix.md`](../Plans/overdub_occupy_catchup_open_note_stack_bugfix.md)  
**Parent:** [`overdub_occupy_active_note_ledger_cardinality_refinement.md`](../Plans/overdub_occupy_active_note_ledger_cardinality_refinement.md) — nested HITL **MET** [`095902`](../../captures/session_20260819_095902.log)  
**Pin:** [`095902`](../../captures/session_20260819_095902.log) pitch 12 `hs=336` `n=3 a=1` (ended 5218/5224 still open)

**Invariant:** `applyOpenClosedInterval` applies in-interval events in phase order, Off before On at each phase. An untagged Off can close a NoteOn that started in the same `(lastTick, occupy]` window.

**HITL [`101319`](../../captures/session_20260819_101319.log):** extra-open **not met** (`n>a` 35 vs 40 on [`095902`](../../captures/session_20260819_095902.log); `n=2 a=0` 15→1; `n=2 a=1` 6→25). One span-start `n=0 a=1` (pitch 23 `hs=384`). All 37 mismatches `cu=0`. Native fixture still holds. Do not widen catch-up; next is clock-path.

**Does not reopen:** Off stamping; option B; clock equal-phase Off before On; `playMidiEvents` from occupy; folding capture into `mergedMidiEvents`; occupy catching up when `occupyPhase <= lastTick`; `rebuildPlaybackOrder`.

### Occupy open-NoteOn ledger cardinality — DEC-042 (nested HITL MET)

**Plan:** [`overdub_occupy_active_note_ledger_cardinality_refinement.md`](../Plans/overdub_occupy_active_note_ledger_cardinality_refinement.md)  
**Decision:** [DEC-042](../DECISION_LOG.md#dec-042-same-pitch-active-note-identity-is-a-cardinality-problem-not-an-off-identity-problem)  
**Parent:** [`overdub_occupy_unmatched_off_ledger_investigation.md`](../Plans/overdub_occupy_unmatched_off_ledger_investigation.md) — geometry proven [`092336`](../../captures/session_20260819_092336.log)

**Invariant:** `ActiveNoteLedger` holds every open playback NoteOn. Occupy walks `forEachActive`. Untagged Off → LIFO; identified Off → exact or orphan (no LIFO fallback). `noteId()` is compatibility newest-on-lane only.

**HITL [`095902`](../../captures/session_20260819_095902.log):** nested `n=0 a=1` **0 / MET**. Structural `n=2 a=2` **2**; leftover `n=1 a=2` **1** (pitch 24 same-tick `On@456` pair). Unmasked extra-open: `n=3 a=1` **18**, `n=2 a=0` **15**. All 44 mismatches `cu=0`. Extra-open is the catch-up stack RC above — not a DEC-042 fail.

**Does not reopen:** Off stamping; option B; clock equal-phase Off before On; `playMidiEvents` from occupy; folding capture into `mergedMidiEvents`.

### Occupy unmatched Off vs ledger — observability (geometry proven)

**Investigation:** [`overdub_occupy_unmatched_off_ledger_investigation.md`](../Plans/overdub_occupy_unmatched_off_ledger_investigation.md)  
**Successor:** cardinality refinement above.

### Occupy clock same-tick Off before On — shipped

**Plan:** [`overdub_occupy_clock_same_tick_off_before_on_bugfix.md`](../Plans/overdub_occupy_clock_same_tick_off_before_on_bugfix.md)  
**Parent (catch-up two-pass shipped, gate not met):** [`overdub_occupy_same_tick_off_before_on_bugfix.md`](../Plans/overdub_occupy_same_tick_off_before_on_bugfix.md)  
**Pin:** [`235314`](../../captures/session_20260818_235314.log) — **4 `n=0 a=1`**; **0 `n=1 a=0`**; span-start L4811 / L4899 are USB-after-clock  
**HITL PASS:** [`001021`](../../captures/session_20260819_001021.log) — **0 `n=0 a=1`**; **0 `n=1 a=0`**; `hs=` on 82/82 occupies. L2715 `240–336` @ 240 is `n=1 a=1`.

**Invariant:** Clock playback at equal phase applies Off before On so abutting same-pitch replacement last-writes the new On. Occupy still reads `Entry.noteId`. Catch-up skip bound unchanged (`occupyPhase <= lastTick` still skips).

**HITL gate:** **met.** Occupy 12 @ `hs=0` sounding `n=1 a=1`. Do not treat `n=1 a=2` as this FAIL. 235314 interiors L4750/L4294 were not in this capture; `n=0 a=1` is 0 so unmatched-Off is not opened.

**Successor (observability):** [`overdub_occupy_unmatched_off_ledger_investigation.md`](../Plans/overdub_occupy_unmatched_off_ledger_investigation.md) — FIFO stamping rejected; geometry now **proven** as a nested same-pitch pair ([`090050`](../../captures/session_20260819_090050.log)).

**Does not reopen:** occupy repairing clock when `occupyPhase == lastTick`; `playMidiEvents` from occupy; folding capture into `mergedMidiEvents`; changing `applyPlaybackLedgerEvent`. Catch-up apply order vs stack: [`overdub_occupy_catchup_open_note_stack_bugfix.md`](../Plans/overdub_occupy_catchup_open_note_stack_bugfix.md).

### Occupy same-tick Off before On — catch-up interval

**Plan:** [`overdub_occupy_same_tick_off_before_on_bugfix.md`](../Plans/overdub_occupy_same_tick_off_before_on_bugfix.md)  
**Parent (interval catch-up shipped, gate not met):** [`overdub_occupy_on_tick_clock_catchup_bugfix.md`](../Plans/overdub_occupy_on_tick_clock_catchup_bugfix.md)  
**Pin:** [`233247`](../../captures/session_20260818_233247.log) — **11 `n=0 a=1`**; **0 `n=1 a=0`**; `hs=` on 100/100 occupies  
**HITL FAIL:** [`235314`](../../captures/session_20260818_235314.log) — **4 `n=0 a=1`**; **0 `n=1 a=0`**; `hs=` on 133/133 occupies. Remaining span-start fails were clock equal-tick — successor HITL **PASS** [`001021`](../../captures/session_20260819_001021.log). Global two-pass vs open-NoteOn stack: [`overdub_occupy_catchup_open_note_stack_bugfix.md`](../Plans/overdub_occupy_catchup_open_note_stack_bugfix.md).  
**USB `playMidiEvents` catch-up:** **reverted** [`214856`](../../captures/session_20260818_214856.log) — do **not** call `playMidiEvents` from occupy

**Invariant:** When reconstructing committed ledger state over `(lastTickInLoop, occupyPhase]`, equal-tick replacement resolves Off before On. Occupy still does not advance playback. Apply order is per-phase Off then On in `CommittedPlaybackLedgerCatchUp::applyOpenClosedInterval` ([`overdub_occupy_catchup_open_note_stack_bugfix.md`](../Plans/overdub_occupy_catchup_open_note_stack_bugfix.md)). Clock owns cursor, `nextEventIndex`, `lastTickInLoop`, send, wrap, and capture.

**HITL gate:** leftover `n=1 a=0` **met**. `n=0 a=1` not met (4) — successor above.

**Does not reopen:** wrap-S `(prev, S]`; loop-head Q16; folding capture into `mergedMidiEvents`; occupy fallback; DisplayManager patch; setting `lastTickInLoop` or `nextEventIndex` from USB; changing `applyPlaybackLedgerEvent`.

### Occupy on-tick before clock interval — ledger catch-up

**Plan:** [`overdub_occupy_on_tick_clock_catchup_bugfix.md`](../Plans/overdub_occupy_on_tick_clock_catchup_bugfix.md)  
**Parent (FROZEN leftover met):** [`overdub_occupy_merged_capture_ledger_bugfix.md`](../Plans/overdub_occupy_merged_capture_ledger_bugfix.md)  
**Pin:** [`231038`](../../captures/session_20260818_231038.log) — 6 `n=0 a=1` pitch 12; `n=1 a=0` = 0  
**HITL FAIL:** [`233247`](../../captures/session_20260818_233247.log) — interval catch-up in tree; leftover `n=1 a=0` met; remaining 11 `n=0 a=1` are successor above.

**Invariant:** USB occupy may advance the committed ledger to the USB phase; it must **not** advance playback. Occupy consumes existing committed-only `mergedMidiEvents` via `catchUpCommittedPlaybackLedgerToPhase` (`(lastTickInLoop, occupyPhase]`). Skip wrap-crossing (`shouldCommitOverdubWrap`) and `occupyPhase <= lastTickInLoop`. Interval is trusted **after** Off-before-On in that interval.

**Does not reopen:** wrap-S `(prev, S]`; loop-head Q16; folding capture into `mergedMidiEvents`; occupy fallback; DisplayManager patch; setting `lastTickInLoop` or `nextEventIndex` from USB.

### Occupy merged-capture ledger — FROZEN

**Plan:** [`overdub_occupy_merged_capture_ledger_bugfix.md`](../Plans/overdub_occupy_merged_capture_ledger_bugfix.md)  
**Parent (FROZEN):** [`overdub_occupy_capture_stream_ledger_bugfix.md`](../Plans/overdub_occupy_capture_stream_ledger_bugfix.md)  
**Pin (pre-fix):** [`224719`](../../captures/session_20260818_224719.log) — 8 `n=0 a=1`; 6 `n=1 a=0`  
**HITL leftover met:** [`231038`](../../captures/session_20260818_231038.log) — **0 `n=1 a=0`**. Remaining 6 `n=0 a=1` are successor above.

**Invariant:** Playback `runtime.mergedMidiEvents` is committed-only. Occupy reads `ActiveNoteLedger` written from that representation, wrap-pass, and loop-head. Live capture echo uses `playbackCursorAdvanceSendCapture` and must not last-write that ledger. Do not fold capture into `mergedMidiEvents` again.

### Occupy capture-stream ledger — FROZEN

**Plan:** [`overdub_occupy_capture_stream_ledger_bugfix.md`](../Plans/overdub_occupy_capture_stream_ledger_bugfix.md)  
Emit-only `playbackCursorAdvanceSendCapture` shipped. Remaining writer was live capture folded into `mergedMidiEvents` — successor above. Pin [`221334`](../../captures/session_20260818_221334.log). HITL FAIL [`224719`](../../captures/session_20260818_224719.log).

### Occupy `n=0 a=1` off tick 0 — display vs ledger (USB catch-up reverted)

**Investigation:** [`overdub_occupy_off_tick_display_investigation.md`](../Plans/overdub_occupy_off_tick_display_investigation.md)  
**Bugfix (reverted):** [`overdub_occupy_after_wrap_s_interval_bugfix.md`](../Plans/overdub_occupy_after_wrap_s_interval_bugfix.md)  
**Pin:** [`203948`](../../captures/session_20260818_203948.log) occupy 12 @ storage **96** `n=0 a=1 b=1` (`204277855`) after wrap 2 (S=**64**)  
**HITL FAIL:** [`214856`](../../captures/session_20260818_214856.log) — catch-up did not occupy the named spans; `playMidiEvents` from occupy **reverted**

**Display:** OVERDUBBING paint is `resolveDisplayNotesLiveCapture` from `overdubSourceViewNotes` + `capturePreview`. Occupy is not a paint input. Do not patch DisplayManager.

**Named write (native only):** wrap-committed On@96 is not in wrap-S `(prev, S]`. Clock `playCommittedLoopMidi` must walk `(S, occupyTick]` after wrap reanchor at S. Do **not** call `playMidiEvents` from occupy (wrap can commit on USB NoteOn). Occupy stays a reader. CAP `as=`/`ae=` stays. Successor for USB-before-clock occupy is ledger catch-up above.

**214856 `n=0 a=1` spans (not On@96):** 30 `528–544`; 12 `0–192`; 12 `144–232`; 12 `720–767`; 24 `216–408`. Wraps at storage **8**.

**Does not reopen:** wrap-S `(prev, S]`; loop-head Q16 (HITL PASS below). No occupy fallback.

### Loop-head playback ledger after wrap — HITL PASS

**Plan:** [`overdub_loop_head_playback_ledger_bugfix.md`](../Plans/overdub_loop_head_playback_ledger_bugfix.md)  
**Investigation:** [`overdub_loop_head_playback_ledger_investigation.md`](../Plans/overdub_loop_head_playback_ledger_investigation.md)  
**Fail pin:** [`185831`](../../captures/session_20260818_185831.log) occupy 60 @ storage **0** `n=0 a=1 b=1` (`54243271`)  
**HITL PASS:** [`203948`](../../captures/session_20260818_203948.log) occupy 12 @ storage **0** `n=1 a=1 b=1` (`214037034`, `232043429`)

**Invariant:** Overdub wrap seal does not run Q16 min-length. Wrap-committed NoteOn @ 0 stays in `lastCommittedPassId()` so the existing 0-clock `atLoopStart` walk writes `ActiveNoteLedger` before occupy.

**Owner:** `Loop::sealCapture` (`CommitReason::OverdubWrap` skips `removePairsShorterThanNoteMinLength`). Occupy stays a reader. Do not add a loop-head catch-up.

**Native:** 1350/1350.

**Does not reopen:** wrap-S `(prev, S]` (HITL PASS below). No occupy fallback. `n=0 a=1` off storage 0 in [`203948`](../../captures/session_20260818_203948.log) (12 @ 96 after wrap 2) is outside this invariant.

### Wrap-tick ledger catch-up from committed pass — HITL PASS

**Plan:** [`overdub_wrap_committed_pass_playback_bugfix.md`](../Plans/overdub_wrap_committed_pass_playback_bugfix.md)  
**HITL PASS:** [`185831`](../../captures/session_20260818_185831.log) wrap at storage **696**, occupy 71 @ **704** `n=1 a=1 b=1`. Fail pin remains [`180844`](../../captures/session_20260818_180844.log) wrap 15.

### Occupy — lookup `Entry.noteId`

**Plan:** [`overdub_present_at_tick_jit_enhancement.md`](../Plans/overdub_present_at_tick_jit_enhancement.md)  
**Architecture:** [`overdub_present_at_tick_jit_architecture.md`](../Plans/overdub_present_at_tick_jit_architecture.md)  
**Decision:** [DEC-041](../DECISION_LOG.md#dec-041-occupy-present-at-s-jit-not-full-loop-lcr-mat) — `collectOverdubNoteOnParticipantIds` reads `ActiveNoteLedger` at `currentTick`; do **not** say occupy = ledger  
**Evidence:** [`132806`](../../captures/session_20260818_132806.log), [`213401`](../../captures/session_20260817_213401.log), [`152940`](../../captures/session_20260813_152940.log), [`152745`](../../captures/session_20260818_152745.log)

**Owners:** `Loop` / `LoopContentResolution` = `LoopPasses`. `playCommittedLoopMidi` writes `LoopPlaybackRuntime::ledger` via `applyPlaybackLedgerEvent` on committed-only `mergedMidiEvents` / wrap-pass / loop-head. Capture emit does not write. `sendMidiEvent` emits. Occupy reads every open identity via `forEachActive` ([DEC-042](../DECISION_LOG.md#dec-042-same-pitch-active-note-identity-is-a-cardinality-problem-not-an-off-identity-problem)).

**Product:** zero or more open NoteOns per `(channel, pitch)`. No `length` on `Entry`. `LoopPasses` may still overlap (`max_same_pitch=322`). `sendMidiEvent` does not create those entries.

**Stage 0:** **Yes** — `PresentNote.noteId` and playback `evt.noteId` are both `MidiEvent.noteId`. Not a 1:1 pitch lookup.

**Now:** occupy lookup **shipped** (`forEachActive` on the lane). CAP `from=ledger`. `noteId()` is compatibility newest-only. Consume stays on `overdubSourceView`. Do not copy `PresentNoteVec`. No `length`. Wrap-S ledger catch-up **HITL PASS** [`185831`](../../captures/session_20260818_185831.log). Loop-head storage-0 **HITL PASS** [`203948`](../../captures/session_20260818_203948.log). Clock equal-tick Off-before-On HITL **PASS** [`001021`](../../captures/session_20260819_001021.log). Open-NoteOn cardinality HITL gate open.

**Parked:** `evaluateOccupyOverlap`; wait-STOPPED-for-`lcr,mat`; consume merge; “occupy = ledger” ownership transfer.

[`132806`](../../captures/session_20260818_132806.log): Stage 1c **held**. Do not wait STOPPED for `lcr,mat`. Do not reconstruct on the note.

### 64-bar source-view identity — Stage 1c shipped; `from=span` HITL parked

Membership, length identity, and dirty/save stall **shipped** ([`overdub_participant_64bar_source_view_identity_and_note_off_fill_bugfix.md`](../Plans/overdub_participant_64bar_source_view_identity_and_note_off_fill_bugfix.md)). [`132806`](../../captures/session_20260818_132806.log) Stage 1c held; 64-bar `from=span` not met (PLAYING during `prep`). That HITL is **parked** (DEC-041). Do not wait STOPPED for `lcr,mat`. Do not start consume merge.

### Overdub participant discovery — notes present at S (Phase 4 1-bar PASS)

**Plan:** [`overdub_participant_loop_content_architecture.md`](../Plans/overdub_participant_loop_content_architecture.md)  
**Parent:** [`consumer_window_budget_ownership_architecture.md`](../Plans/consumer_window_budget_ownership_architecture.md)  
**Evidence:** [`213401`](../../captures/session_20260817_213401.log)  
**Owner (shipped occupy):** `Loop::collectOverdubNoteOnParticipantIds` (`forEachActive` on the lane). Not a smaller source window.

**PresentNote** = LCR query snapshot at S (many `NoteId`s; no `endTick`). **ActiveNote** = open `ActiveNoteLedger::Entry` (several per `(channel, pitch)`). MIDI out = `midiHandler.sendMidiEvent`. Identity field: `noteId` on `Entry`; do not copy `PresentNoteVec`.

**Phase 4 1-bar HITL PASS** [`123803`](../../captures/session_20260818_123803.log): `collectConsumeWindow` skips `ensureOverdubSourceNotesForHold` when `loopLen <= overdubSourceWindowLengthTicks()`. Track 6 (768): **`why=hold` = 0**; occupy 102/130 `from=prep` `a=1,b=1` `eq=1`; consume still Hide (`hide` 3–11). Track 0 (50688): 36 `why=hold` remain (1 `merged=1`); consume still Add/Hide (`empty_sets=0`). `late_clk=0`. Native `test_note_off_skips_hold_fill_when_source_view_covers_loop`.

**Parked — 64-bar occupy via full-loop `from=span`:** [`122848`](../../captures/session_20260818_122848.log) `a=1,b=0` was RC1 (fixed). [`125542`](../../captures/session_20260818_125542.log) `a=1,b=0` = 0. Wait-for-`lcr,mat` **parked** ([DEC-041](../DECISION_LOG.md#dec-041-occupy-present-at-s-jit-not-full-loop-lcr-mat)). Empty occupy is **not** “no participants.” Consume stays on `overdubSourceView`. Do not start consume merge.

**Phase 3 1-bar HITL PASS** [`121933`](../../captures/session_20260818_121933.log): occupied 48/74 `a=1,b=1`; no note-on `why=hold`. Production occupy is now `Entry.noteId`.

Phase 0b **done:** `PresentNote` alone is not enough for RC8 LinearSpan (`endTick` dropped; present on `NoteSpan` / `DisplayNote`). Observation B now walks prepared `NoteSpan`s with `displayNotePresentAtHold` (same function A uses). `notePresentAt` is unchanged (playback / checkpoint fill). Prepared miss still returns false; no cold `resolveState` on note-on. PLAYING / STOPPED / MUTED participant HITL **skipped** ([DEC-040](../DECISION_LOG.md#dec-040-skip-playingstoppedmuted-overdub-participant-hitl)): overdub only runs in OVERDUBBING.

Phase 1 **device capture** [`002447`](../../captures/session_20260818_002447.log) (`b9b9336`): 1-bar (768). 31 `lcr,part`. **`eq=0` = 0.** After wrap 1 and wrap 2, occupied holds are `a=1,b=1` (`ao=0`,`bo=0`). [`232510`](../../captures/session_20260817_232510.log) wrap-2/3 `b=k` extras are gone. Enter before first wrap is still `from=miss` (6). Hold fill still `from=win` on that capture. Production occupy is now present-at-S (Phase 3).

**Phase 2a** `76623cd` + **O:** `a384a47`. Device [`011009`](../../captures/session_20260818_011009.log) **PASS** for wrap-undo source-view refresh: `why=undo` on peel, post-undo A `a=1`, hold `merged=0`. **Phase 2 B collect** uses RC8 hold on prepared spans. Production consume still A.

**013327 wrap-crossing fill shipped** (native A==B). Device [`021716`](../../captures/session_20260818_021716.log) pre-wrap `a=0,b=2` at storage 64 is finished opens on prepared `NoteSpan`s — MIDI reconstruct (`finishOpenNotes=false`) never produced them. **Source-view span fill** `b94bd2b` ([`overdub_participant_source_view_span_membership_bugfix.md`](../Plans/overdub_participant_source_view_span_membership_bugfix.md)). Device [`024225`](../../captures/session_20260818_024225.log) empty `from=span,notes=0` wiped RC12 display. **Display wipe PASS** [`025337`](../../captures/session_20260818_025337.log): `why=open,from=win,notes=1`; `DISP` `PLAYING` 1 → `OVERDUBBING` 1; prepared `eq=0` = 0. [`025916`](../../captures/session_20260818_025916.log) / [`030219`](../../captures/session_20260818_030219.log) hit `COORD` storage 64 **before** first wrap, but those holds are `from=miss,a=0` on a 1-note loop (`notes=1`); 030219 pitches are 86/60 (021716's pitches). After wrap, storage 64 is `eq=1`. **030219 RC12 flood:** wrap 1 `from=span,ev=8,notes=34` vs `vch notes=4` (no `lcr,mat` this capture). Span copy now keeps only window NoteOn ids. **RC12 flood PASS** [`030958`](../../captures/session_20260818_030958.log) / [`032228`](../../captures/session_20260818_032228.log): wrap `lcr,src notes` equals `vch notes` (032228 all 7 wraps delta 0). Hide-all-spans native `7760811`. 032228 wrap 6–7 `bo=1` is B overcount of Disabled wrap-layer companions; B collect now skips index orphans, Disabled-pass companion restore, and Disabled restore when an Active companion already targets that id. Occupy stays A. Device [`034455`](../../captures/session_20260818_034455.log) **PASS**: 64 `from=prep` `eq=1`, **`eq=0` = 0** after undo then more wraps. Wrap 1-frame undo flash: span copy now uses the same Disabled-companion restore guards as B collect; session undo/redo invalidates the live display cache. Device [`040236`](../../captures/session_20260818_040236.log) **PASS** (flicker gone; 103 `from=prep` `eq=1`, `eq=0` = 0). 021716 storage-64-before-wrap is **not** a blocking gate. **Phase 3 in tree:** note-on occupy is `Loop::collectOverdubNoteOnParticipantIds` — prepared present-at-S when ready, else the source-view walk. `Track::snapshotOverlapHoldCandidates` no longer calls `ensureOverdubSourceNotesForHold`. CAP `a` is source-view without fill; production occupy is `b` when `from=prep`. Native: `test_note_on_occupy_*` (64-bar NOTE ON outside the 16-bar window still occupies from prepared spans; miss does not fill). Do not delete `overdubSourceView`. Do not start the Experiment 1 1/2/4/8/16 rebuild series as production policy. NOTE_EDIT overlap is a sibling consumer.

### Consumer window budget — Experiment 1 (detach landed; series not next)

**Plan:** [`consumer_window_budget_ownership_architecture.md`](../Plans/consumer_window_budget_ownership_architecture.md)  
**Owner:** `Loop::kOverdubSourceWindowBars` / `overdubSourceWindowLengthTicks`. Default **16**. CAP `lcr,src` includes `bars=`.

Detach from display 16 is in tree. A timing-passing clamp is evidence, not policy. Successor is notes present at tick `S` (above), not a smaller geometric window. Do not shrink 2-bar gather.

### Playback gather Stage 1 — MIDI deadline lateness hooks (measurement landed)

**Plan:** [`playback_gather_lcr_consume_enhancement.md`](../Plans/playback_gather_lcr_consume_enhancement.md)  
**Owner:** `RuntimeTimingTelemetry`. No geometry change. No LCR consume.

Hot path accumulates `late_on` / `late_off` / `late_clk` (on time = sent before the next tick / one-tick clock window). ISR stores stay in ITCM (`noteClockPulse` region). Main loop `maybeEmit` drains a first-late one-shot (`DIAG,late_event`) and a gather rebuild one-shot (`DIAG,playback_build`), then the 5 s Tier-A `DIAG,late_*` window. No per-event `#CAP`. Native 1329/1329. `teensy41-capture-serial` links (RAM1 code 425852, locals 4768).

[`213401`](../../captures/session_20260817_213401.log): 2-bar `playback_build` 3–6 ms; 64-bar stall is source/hold on USB. Do not start Stage 2 stamp redesign, Problem B length, or LCR consume from this file.

### Display undo / wrap / pitch-move ghosts (RC-W1, RC-N1, RC-U1)

**Plan:** [`note_edit_display_undo_overdub_wrap_bugfix.md`](../Plans/note_edit_display_undo_overdub_wrap_bugfix.md)  
**Evidence:** [`144703`](../../captures/session_20260817_144703.log), [`144939`](../../captures/session_20260817_144939.log)

**Firmware committed** (`f0b0e66` / `142b95b` / `324ffdd` / `4aabf1c`). [`152627`](../../captures/session_20260817_152627.log): RC-W1 / RC-U1 **HITL PASS**. [`155450`](../../captures/session_20260817_155450.log): NOTE_EDIT exit `slice_clean`/`DFRAME` **5** (not [`153213`](../../captures/session_20260817_153213.log) `DISP 8`). [`153213`](../../captures/session_20260817_153213.log) exit 8 was stacked persist `60@64` (consume), not wrap live-cache.

Wrap invalidates live display cache; pitch commit retires persist-twin overlay and home pitch in `visualCache`; undo/redo uses idle visual-cache slices (`refreshVisualCacheAfterPassStateChange`). Do not reopen RC11/RC12. Do not change DEC-038.2 session undo grain.

### Wrap-crossing overdub consume — **shipped** (on device)

**Plan:** [`overdub_wrap_crossing_hold_head_consume_bugfix.md`](../Plans/overdub_wrap_crossing_hold_head_consume_bugfix.md)  
**Firmware:** `e1f57c5`. **HITL PASS** [`155450`](../../captures/session_20260817_155450.log).

One wrap-crossing incoming note occupies `[S, L) ∪ [0, E)` as a single hold. NOTE_EDIT tick 64 is `1/2` (86 + `60@64–240`); [`153213`](../../captures/session_20260817_153213.log) was `1/3`. `overlap_hold` `hide=1` `add=1`. Do not reopen RC11/RC12. Do not patch `applyNoteEditPass`.

### Loop length during overdub (queued — do not start firmware)

**Plan:** [`overdub_loop_length_during_overdub_enhancement.md`](../Plans/overdub_loop_length_during_overdub_enhancement.md)  
**Evidence:** [`140355`](../../captures/session_20260817_140355.log) @ 25.541 s — LOOP_EDIT CC `ch=15 cc=2 value=80` set 768 → 62208 ticks during overdub; MIDI length feedback lagged until ~38 s.  
**Parent authority:** [`overdub_lifecycle_representation_authority.md`](../Plans/overdub_lifecycle_representation_authority.md)

Do **not** start Stage 1–4 firmware until this file is explicitly in implementation (user approved the plan + tasks only). Do not reopen RC11 consume or RC12 `appendOverdubPassDisplayNotes` skip.

**RC11/RC12 FROZEN:** [`overdub_overlap_hold_display_cache_bugfix.md`](../Plans/overdub_overlap_hold_display_cache_bugfix.md) — HITL Gate 1–4 PASS [`140355`](../../captures/session_20260817_140355.log). Native 1294/1294. Commits `2e8f480` / `e1ebcbb`.

**RC10:** live capture paint applies pending Hide/Shorten on a copy.  
**RC9:** overdub transport stop finalizes pending before `sendAllNotesOff`.  
**RC8–RC6:** see parent [`overdub_overlap_hold_same_start_bugfix.md`](../Plans/overdub_overlap_hold_same_start_bugfix.md).

### NOTE_EDIT UNDO_WARM + commit-recon (investigation)

**Plan:** [`note_edit_undo_warm_missing_recon_investigation.md`](../Plans/note_edit_undo_warm_missing_recon_investigation.md)  
**Evidence:** [`143144`](../../captures/session_20260816_143144.log), [`145518`](../../captures/session_20260816_145518.log)

**Parked — wrap-move persist:** [`201446`](../../captures/session_20260816_201446.log) live wrap-move is linear (`EditSessionAction` 2832–3408, `DNTE` length **576**). Deselect persist LIFO-pairs false Off@2688 and leaves wrap Off@96; reselect `DNTE` length **336**. That failure is partly the current rematerialize / full-loop session-store structure. Do **not** add more LIFO / wrap-off persist patches in that structure. Re-evaluate after NOTE_EDIT hydrate if the 336/288 shorten remains. Do not patch `applyNoteEditPass` identity. Stage 2 / C5 / B2b / Layer D parked.

**Next Layer C (parked C5):** `OverlapCandidateLookup::appendNotesForIds` in overlay only — not overdub source-view / hold ids. Not reconstruct. Not empty-pair resolve.

### DEC-037 — LoopContentResolution parallel prototype

**Parked:** overlay loop picker (`set-revision-persistence` §4.8–4.10) — WIP stashed on `feature/set-revision-loop-picker`.

**Active:** native `LoopContentResolution` prototype. Do **not** optimize `materializeToEventVector` again. Do not wire resolution onto MIDI/display until three gates pass.

**Plan:** [`loop_event_sourced_resolution_architecture.md`](../Plans/loop_event_sourced_resolution_architecture.md)  
**6D investigation:** [`loop_content_resolution_incremental_commit_maintenance_refinement.md`](../Plans/loop_content_resolution_incremental_commit_maintenance_refinement.md)  
**6E overdub evaluation:** [`loop_content_resolution_overdub_state_evaluation_refinement.md`](../Plans/loop_content_resolution_overdub_state_evaluation_refinement.md) — **6E.1–6E.5 PASS**; [DEC-038](../DECISION_LOG.md#dec-038-overdub-wrap-commit-and-session-undo) **038.1 HITL PASS**; **038.2 landed** (one `OverdubPassAdded` `passIds` + STK3).  
**Handoff:** [`loop_content_resolution_stage9_handoff.md`](../Plans/loop_content_resolution_stage9_handoff.md)  
**Authority:** [DEC-037](../DECISION_LOG.md#dec-037-loop-content-resolution-parallel-prototype)

**Now:** LED lookup Stage 1 **PASS** [`114736`](../../captures/session_20260816_114736.log) — [`post_undo_led_lookup_resumable_source_refinement.md`](../Plans/post_undo_led_lookup_resumable_source_refinement.md). No LED gather rem; BAR→LED 3.7 ms; PLAYING `clockrate` 47–48. Remaining PLAYING `midi_gap` 110–127 ms is idle/load, not lookup. Stage 2 **rejected**. Do **not** re-arm drain. Playback gather is a separate work path ([`playback_gather_lcr_consume_enhancement.md`](../Plans/playback_gather_lcr_consume_enhancement.md)) — do not start from this LED slice.

**Scheduler prep:** [`runtime_scheduler_lcr_consumer_grooming_refinement.md`](../Plans/runtime_scheduler_lcr_consumer_grooming_refinement.md). **Slice 1–4c device PASS.** Slice 4d firmware landed; [`143144`](../../captures/session_20260816_143144.log) moved NOTE_EDIT session work to the investigation above. **Slice 1b device PASS [`225626`](../../captures/session_20260816_225626.log)** — dirty PLAYING `midi_gap` = `idle_maint` 79–96 ms; child rem is `idle_append` only (72–82% of parent). **Slice 2b device PASS [`232423`](../../captures/session_20260816_232423.log)** — unprepared interior idle slices skip `appendOverdubPassDisplayNotes`; wrap-edge (bar 0 / last bar) still appends. 6B stops 3–7: no `idle_append` rem, `midi_gap` 24–32 ms, `clockrate` 47. **Slice 2c device PASS [`233323`](../../captures/session_20260816_233323.log)** — `stale_range` `dcnt` 4 / 5 / **1** / 4; cache notes rise; coverage `0–63`. Do not start 4e. Do not grain idle. Do not fold NOTE_EDIT hydrate into grooming — that is its own work path below. Do not optimize `LoadLoopJob` from PLAYING paint.

**FinalizeWorkspace slice** shipped (`48bd36f`). **LoopPersist finalize** relanded — boot **PASS** [`213246`](../../captures/session_20260816_213246.log); one PLAYING `persist_save` rem **206 ms** @ 25.683 s (later jobs no rem ≥ 50 ms).

**Parked:** 915 ms boot `load_frame` [`032803`](../../captures/session_20260816_032803.log) — `commitLoadLoopJobPublish` / `runDeferredLoadAndDisplayFrame`; not a freeze; not the revert.

**6A.1 HITL PASS** [`025651`](../../captures/session_20260816_025651.log) `match=1` `pmatch=1`. STOPPED `midi_gap` **72 ms** at LCR complete is a different sample (`6a,nat` 30 ms + `loop_rem,idle_maint` 58 ms); after that, max **13.5 ms**.

**Does not start:** Track A overlay, Stage 3b GUS replacement, interval reservation, RC-J patches, deleting `materializeToEventVector`. PLAYING `midi_gap` in [`192334`](../../captures/session_20260815_192334.log) was FinalizeWorkspace (sliced). Post-stop gap owner is idle visual cache, not LCR. NOTE_EDIT hydrate (below). Playback gather ([`playback_gather_lcr_consume_enhancement.md`](../Plans/playback_gather_lcr_consume_enhancement.md)).

### NOTE_EDIT hydrate (queued — own work path)

**Work identity:** [`note_edit_hydrate_enhancement.md`](../Plans/note_edit_hydrate_enhancement.md)  
**Architecture:** [`note_edit_selectedtick_lcr_resolution_architecture.md`](../Plans/note_edit_selectedtick_lcr_resolution_architecture.md) — DEC-037 amendment 2026-08-16; overlap retarget 2026-08-17; architecture **PASS**; stages **PASS WITH AMENDMENTS**.  
**Sibling:** [`overdub_participant_loop_content_architecture.md`](../Plans/overdub_participant_loop_content_architecture.md) §5 NOTE_EDIT sibling.

Overlap uses the **same participant query as overdub**, keyed by **selected/mover LinearSpan**, not a window around `selectedTick`. Select stays neighborhood navigation (`tickEvents` / `spanBoundaries` around `selectedTick`), not `resolveState`. Paint stays `visualCache` + `NoteEditCurrentState`. Stages 4a/4b/4c split.

Do **not** start firmware from “prepared LCR around `selectedTick`.” That mapping is withdrawn for overlap.

**Not** remaining `loop-content-resolution` 6.4 firmware. **Not** grooming Slice 5. **Not** `lazy-slot-hydration`. **Not** a resumable open-until-ready session. **Not** playback gather ([`playback_gather_lcr_consume_enhancement.md`](../Plans/playback_gather_lcr_consume_enhancement.md)).

**6C/6D for this consumer:** 6C consume-when-ready **closed** [`205928`](../../captures/session_20260815_205928.log) / [`210508`](../../captures/session_20260815_210508.log) (3b stays). 6D.4 **HITL PASS** same captures; not all of LCR live.

Wrap-move persist is **parked** (current-structure issue) — it is not a start gate. Do not start firmware until this file is in § Now implementing. Do not full-replace `sessionMidiEvents()` for audition. Do not resume wrap-move persist patches from this path.

### Playback gather (Stage 1 hooks — in Now implementing)

**Work identity:** [`playback_gather_lcr_consume_enhancement.md`](../Plans/playback_gather_lcr_consume_enhancement.md) — DEC-037 amendment 2026-08-17. Stage 1 measurement hooks landed (accumulators + rare one-shots; no per-event serial). Stage 2–3 not started. 2-bar geometry unchanged. Does not start Owner-Boundary Gate / interval reservation. **Not** remaining OpenSpec 6.3 firmware. **Not** hydrate. **Not** LED lookup.

### DEC-036 Layer D 3b — overdub entry without display reconstruct (shipped)

**Plan:** [`loop_layer_d_overdub_rebuild_architecture.md`](../Plans/loop_layer_d_overdub_rebuild_architecture.md)

**D0:** **PASS** [`035414`](../captures/session_20260814_035414.log) — 6.78 s `begin_capture`; source-view problem, not overdub FSM.  
**Device FAIL [`042909`](../captures/session_20260814_042909.log):** undo **14.3 s / 14.6 s** (`VCACHE,full`); overdub **7.1 s**.  
**Device PASS [`045556`](../captures/session_20260814_045556.log):** `slice_clean` 1809 notes then overdub `begin_capture` **2214 µs**.  
**Device PASS [`112909`](../captures/session_20260814_112909.log):** undo **3 ms** (`MIDI: Undo` 143.169 → `Overdub undone` 143.172, `kind=3`); no `VCACHE,full`. Boot `load_frame` ~9.9 s remains D3/D4.  
**OpenSpec:** [`loop-effective-event-source`](../../openspec/changes/loop-effective-event-source/) closeout **4.1/4.2**. Successor: DEC-037 (post-commit rebuild / pass-list walks).

**Does not start:** Track A overlay, Stage 3b GUS replacement, interval reservation, RC-J patches.

### NOTE_EDIT on lengthened loop + overdub entry (shipped this session)

**Fix 1:** `openNoteEditSession` stops active overdub (`stopOverdubbing` → PLAYING) before rematerialize so live capture is committed and editable. **RC2:** always `rebuildVisualCacheFromPasses` on NOTE_EDIT open. Short-loop overdub-stop full rebuild removed in grooming Slice 4 — keep existing notes (`020910` / `021959` stale was adopt_partial).

**Fix 2:** Lengthened loop (4-bar loop, 2-bar content) — NOTE_EDIT select skips detailed-window note filter; bracket clamps to committed content span; select nav slots trimmed past content; nav length extends to painted note tail when overdub exceeds bar-aligned content. Fixture: [`015731`](../../captures/session_20260814_015731.log).

**Fix 3 (Stage 2) device PASS [`024428`](../../captures/session_20260814_024428.log):** after reboot, selected slot 5 undoes immediately — `Undo (entries=17)` kind=4 `NoteEditPassClosed`, then `Undo (entries=16)` kind=1 `OverdubPassAdded`. `rebuildSlotFromLoopContent` keeps the tip on the selected slot so later LoadLoopJob restores do not steal it ([`024004`](../../captures/session_20260814_024004.log) was the FAIL).

### Overdub lost on NOTE_EDIT exit — device PASS

**Device PASS [`025322`](../../captures/session_20260814_025322.log):** overdub → NOTE_EDIT (`visual_notes=102`, `session_events=234`) → pitch/range on note 194 → exit `NoteEditPass replaced … rows=1 saved=1` (not 19 Deletes). Commit flats stay 234. After later overdub, STOPPED `DISP` **103** — not the boot record-only **91**. Contrast [`014553`](../../captures/session_20260814_014553.log) `rows=19` / `DISP` 48→32.

### OLED first-frame mismatch — RC3 device PASS

**RC3 PASS [`014553`](../../captures/session_20260814_014553.log)** — dcache flush before SPI + STOPPED boot restore paints. Plan: [`oled_dma_partial_frame_bugfix.md`](../Plans/oled_dma_partial_frame_bugfix.md).

### Loop content-only history (DEC-035 Layer A) — archived

**Shipped + archived 2026-08-14.** Stage 3 + follow-up device PASS [`030147`](../../captures/session_20260814_030147.log) / [`032227`](../../captures/session_20260814_032227.log). OpenSpec `openspec/changes/archive/2026-08-14-loop-content-history-persistence/`; normative `openspec/specs/loop-content-history/`. Do not start Stage 3b without new DEC.

Plan: [`loop_layer_history_persistence_architecture.md`](../Plans/loop_layer_history_persistence_architecture.md). Task [#33](https://github.com/Lytrix/MidiLooper/issues/33). Bug [#32](https://github.com/Lytrix/MidiLooper/issues/32) stall closed.

### Overdub-stop MIDI dump during PLAYING

LoadLoopJob PLAYING skip is device-proven in [`105505`](../../captures/session_20260813_105505.log). `UndoStacks` stall closed Stage 3 [`030147`](../../captures/session_20260814_030147.log). SlotMeta bundle on overdub stop closed follow-up [`032227`](../../captures/session_20260814_032227.log) — `LoopPersist` only ~139 ms. Scheduling: [`runtime_scheduling_owner_boundary_admission_refinement.md`](../Plans/runtime_scheduling_owner_boundary_admission_refinement.md). Do not patch RC-J.

### Real-time incremental work (RECORD/OVERDUB) — post–RC-C + S0 timing telemetry

**Now: S0e follow-through RC-K1–K3 shipped and device-verified.** Targeted fix of the overdub note-off cost attributed by S0e. Do not implement interval reservation, add extra `handleMidiInput()` call sites, patch RC-J, or chase overdub display frame-skip. Plan: [`realtime_incremental_work_overdub_note_change_bugfix.md`](../Plans/realtime_incremental_work_overdub_note_change_bugfix.md). Investigation [§31n](../Plans/archive/refinements/runtime_scheduling_timing_envelope_investigation.md#31n-s0e--split-overdub-note-off-path-observation-only) / [§31o](../Plans/archive/refinements/runtime_scheduling_timing_envelope_investigation.md#31o-s0e-follow-through--rc-k1--rc-k2--rc-k3). Evidence: [`204221`](../../captures/session_20260812_204221.log) (`notechg` 274 ms = `noterecon` 177 ms + `notepair` 98 ms).

**Baseline (proof of current stability):** [`191356`](../../captures/session_20260812_191356.log) on `752273d` (RC-H reverted). Multiple overdubs including overdub-over-overdub; display did not freeze (`slice_clean` covers the whole loop after every stop; final STOPPED paints the 16-bar window of a clean 1482-note cache). `clockrate` held **47–48** through PLAYING/OVERDUB (no dropped-clock / half-tempo). `midisvc` did **not** hold: 99–133 ms, then **218–221 ms** on the long overdub, 110–125 ms on later overdubs, against `clk`/`tracks` 4–10 ms. RECORD stays 0.3–0.8 ms. PLAYING↔OVERDUB `msi` spikes 237–433 ms. Post-stop clock drop is RC-J (48→24→0 and 48→36→0) — Owner-Boundary Gate, not S0b.

**S0e exit:** attributed in [`204221`](../../captures/session_20260812_204221.log) — `usbnote` is `notechg`; `noterecon` 177 ms is quadratic reconstruct dedup; `notepair` 98 ms is the per-candidate `noteIdHasChannel` scan. `noteappend` is 59 µs.

**S1 RC-K1 (shipped):** `reconstructNotesImpl` tracks seen `(note, startTick, endTick)` in an ordered set instead of `std::any_of` over the accepted list. Same key, same insertion order. Native `test_noteutils_reconstruct` PASS including many-identical-geometry collapse.

**S1 RC-K1b (shipped):** the ordered set allocated one PSRAM tree node per note. Boot visual-cache rebuild of 1430 notes measured 1.38 s ([`215128`](../../captures/session_20260812_215128.log) / [`215357`](../../captures/session_20260812_215357.log)). Dedup now ranks into one vector, `qsort`+unique, restore first-seen order. Native `test_noteutils_reconstruct` PASS. `teensy41-capture-serial` fits RAM1.

**S1 RC-K2 (shipped):** `accumulatePendingNoteChangesForIncomingNote` no longer scans source events for channel. Loop notes are loop-scoped (DEC-033). Native `test_pending_shorten_ignores_recorded_channel` PASS.

**S1 RC-K3 (shipped):** `overdubSourceViewNotes_` is reconstructed once in `establishOverdubSourceView` and cleared in `clearOverdubSourceView`. Note-off reads the member. Native `test_overdub_source_view` PASS.

**S1 RC-K1–K3 verified [`223033`](../../captures/session_20260812_223033.log):** the note-off gate is met — `noterecon` 0 in every window, `notechg` and `notepair` peak at 3.6 ms then hold 0.75–2.0 ms (were 274 / 98 ms).

**S1 RC-L1 (shipped, verified [`225803`](../../captures/session_20260812_225803.log)):** per-span `ProjectedIntervalVec` allocation in `projectDisplayNotes` / `projectNoteIntervals`. Four overdubs on a grown 64-bar loop: `begin_capture` 77 / 80 / 79 / 83 ms (was 1.378 s, and 58 → 410 ms as passes accumulated in [`223033`](../../captures/session_20260812_223033.log)). First USB note 108–242 ms after PLAYING→OVERDUBBING (was 1.76 s). `noterecon` 0; `notechg`/`notepair` 1.09 ms; `usbnote` 1.20 ms; overdub `clockrate` 47–48; overdub `midisvc` 4–14 ms. Remaining ~80 ms is the synchronous gather+reconstruct floor.

**Shipped (on `feature/overdub-participant-discovery`):** playback-observation overlap gates 0–4 + wrap-crossing consume `e1f57c5`. Plan: [`overdub_playback_observation_overlap_refinement.md`](../Plans/overdub_playback_observation_overlap_refinement.md). `PendingNote.overlapNoteIds` collection is wired. Note-off consumes the set via `appendNotesForIds` on `overdubSourceViewNotes_`, then geometry + `[S, E)`. Empty set is Add only. PLAYING idle prebuild reverted (`73f0489`). Option B stays withdrawn ([`021304`](../../captures/session_20260813_021304.log) 292 ms/note-off). Collection-wired overdub in [`154823`](../../captures/session_20260813_154823.log): `noterecon=0`, `notechg=2993`; 11.7 s post-stop stall is `LoopUndoHistory`, not this path.

**Gate 0:** `OverlapNoteIdSet` fixed capacity 128; overflow does not grow. Native PASS. Idle `stored_notes` measured in [`152940`](../../captures/session_20260813_152940.log): track 0 slot 4 (68 bars) `notes=1903 unique=1903 max_same_pitch=322`; track 6 `max_same_pitch=195`. Both exceed 128. Do not raise capacity without a decision. This diagnosis taxes the MIDI event runtime. Gate 0 count is recorded in [`152940`](../../captures/session_20260813_152940.log). Keep `maybeLogStoredNoteCount` as the total-notes / `max_same_pitch` inventory.

**Gate 1:** Production selection is normalized note geometry + `[S, E)` intersection (`existingNoteOverlapsIncomingHold`). `OverlapNoteIdObservation` is test/diagnostic only. Split-chunk and prior Shorten/Hide companion fixtures landed. 021304 same-pitch count still open on Gate 0.

**Gate 2 (native landed, device open):** enabled slots keep advancing playback; mute/solo/slot-mute suppress send on that track's MIDI channel and output ports (USB/DIN/USB Host). Track mute sends CC 123 on `midiChannel` only. Native proves cursor/wrap advance while MIDI send is suppressed, unmute does not resend crossed events, and mute does not clear the ledger. Device still owed: mute mid-note silences that channel on the output ports.

**Gate 3 (native landed):** empty candidate set does not look up spans and does not call `gatherCommittedEvents` / `reconstructDisplayNotes` on note-off. `OverlapCandidateLookup` copies from an already-available note list only. RC-K3 note-off still reads `overdubSourceViewNotes_`. `fullMaterializeCount` stays 0 after the work counter is reset, including when companion edit rows exist.

**Gate 4 (native landed):** `appendNotesForIds` is one pass over the available `DisplayNote` list and stops at the last matching id. It does not call `findLinearNoteSpanForNoteId` and does not build an index. 3714-note fixture with 3 ids examines 30 notes, not 3714×3.

**Hold-candidate collection (wired):** `PendingNote.overlapNoteIds` snapshots already-sounding same-pitch ids at incoming note-on and inserts playback note-on ids while the hold is open. Offs do not erase. Native: `test_overlap_hold_candidates`.

**Note-off consumes overlapNoteIds (wired):** `accumulatePendingNoteChangesForIncomingNote` looks up the set in `overdubSourceViewNotes_` (`appendNotesForIds`) and applies geometry + `[S, E)`. Empty set skips lookup (Add only; Gate 3). Native: `test_pending_note_change`.

**Wrap-crossing hold consume (head + tail, one hold) — shipped `e1f57c5`, HITL PASS [`155450`](../../captures/session_20260817_155450.log):** wrap-head `[0, E)` is the same incoming hold as the tail `[S, loopLength)`, not a second note-off. [`170449`](../../captures/session_20260813_170449.log) `hide=14` was the double-hold. NOTE_EDIT tick 64 is one 60 plus other pitches (`1/2`), not [`153213`](../../captures/session_20260817_153213.log) `1/3`. Plan: [`overdub_wrap_crossing_hold_head_consume_bugfix.md`](../Plans/overdub_wrap_crossing_hold_head_consume_bugfix.md).

**NOTE_EDIT mover wrap-length jump (RC1 device PASS in [`200154`](../../captures/session_20260813_200154.log)):** no `2351` / `2975`. Mover **22** `DNTE` stays **95** while overlap runs on many neighbors. Note **14** `ChangeLength` `2256–2304` stays on the wrap-stub plan. Plan: [`note_edit_mover_wrap_length_jump_bugfix.md`](../Plans/note_edit_mover_wrap_length_jump_bugfix.md).

**NOTE_EDIT leave-restore painted span (RC1 native shipped; [`201948`](../../captures/session_20260813_201948.log) device):** 76 Hide/Restore is `840–863` (not 2160). Note **5** first-select is already `DNTE` **1535** — cache span is `720–2255`, not painted `720–767`. Stop on a second owner here. Mover **100** length stays **144**; 39.397 commit still saves only 14 `2256–2304` (wrap-stub plan). Plan: [`note_edit_overlap_leave_restore_painted_span_bugfix.md`](../Plans/note_edit_overlap_leave_restore_painted_span_bugfix.md).

**NOTE_EDIT note-off pairing LIFO (native shipped; [`213920`](../../captures/session_20260813_213920.log) multi-overlap device PASS):** @45.971 seals `canonical=7 pre_commit=7` — Delete 4/3/2/115, Length 1 `0–95`, mover 77 `96–788` pitch 60. Cache 22→18. [`213533`](../../captures/session_20260813_213533.log) @104.240 emitted the same five actions, then left lane 60 before deselect so the @108.711 commit carried only the mover rows. Plan: [`note_edit_note_off_pairing_lifo_bugfix.md`](../Plans/note_edit_note_off_pairing_lifo_bugfix.md).

**NOTE_EDIT edit-pass replay row payload (native shipped; [`211832`](../../captures/session_20260813_211832.log) device PASS):** `Length 115 48→287` @35.640 and `Length 115 1008→1103` @55.510 both seal and hold. Replay no longer rewrites a later row's span from an earlier row for the same `targetNoteId`. Plan: [`note_edit_replay_row_payload_bugfix.md`](../Plans/note_edit_replay_row_payload_bugfix.md).

**NOTE_EDIT overlap shorten commit seal (native shipped; device gate open):** the deselect commit dropped the overlap `Length` row because `participatingNoteVisibleOverlapTailInProgress` fired on a parked mover, so the shorten either vanished (display reverted) or landed one commit late against an unrelated focus ([`204700`](../../captures/session_20260813_204700.log) @166.809 `canonical=1 apply_owned=2` → @166.848 `Length 10 960–1247`). Skip removed from `buildCommitOverlapRowsFromCurrentState`; predicate stays in leave-restore. Slice B of the resolver contracts plan is **withdrawn** — [`225025`](../../captures/session_20260807_225025.log) `len=287` was correct. Decisions: seal at the user-triggered commit; host tail stays truncated. Plan: [`note_edit_overlap_shorten_commit_seal_bugfix.md`](../Plans/note_edit_overlap_shorten_commit_seal_bugfix.md).

**NOTE_EDIT Length replay loop-boundary (native shipped; device gate open):** persisted `ChangeLength` 14 `2256–2304` was replayed as a wrap; same-pitch notes shortened to 2255. Owner: `applyChangeLengthById`. Plan: [`note_edit_length_replay_loop_boundary_bugfix.md`](../Plans/note_edit_length_replay_loop_boundary_bugfix.md). Do not fold note 5 cache pairing. No note ends at 2255 in [`204700`](../../captures/session_20260813_204700.log) / [`205054`](../../captures/session_20260813_205054.log); the wrap stub paints `DNTE,71,2256,…,48`.

**NOTE_EDIT wrap-stub commit (RC3 native shipped; device gate open):** RC2 device FAIL in [`193838`](../../captures/session_20260813_193838.log) / [`201948`](../../captures/session_20260813_201948.log) — 14 still `ChangeLength` `2256–2304` because cache has a non-zero span. RC3 skips loop-end Length unless painted end is `loopLength`. Plan: [`note_edit_overlap_action_drop_and_wrap_stub_bugfix.md`](../Plans/note_edit_overlap_action_drop_and_wrap_stub_bugfix.md). Do not fold note 5 cache pairing.

**NOTE_EDIT / LOOP_EDIT display split (Stages 8–9 shipped; [`192007`](../../captures/session_20260813_192007.log) device):** 3-bar / 60-note loop (not 181114’s 64). First NOTE_EDIT `DISP` **59/60**. Hide/Restore of **45** is `1440–1511` (not 2160). **76** as overlap target still Hide/Restore `840–2160`. No MoveNote of 76; select at tick 840 rebuilds 76 (no 168 reset). `GEOM_APPLY,resolve` 16.8–52.3 ms. Paint gap and undo-warm stay open. Plan: [`note_edit_visual_cache_display_unification_refinement.md`](../Plans/note_edit_visual_cache_display_unification_refinement.md).

**Withdrawn-path cleanup (removed):** Option A slice tests, Option B windowed matrix, `gatherOverdubSourceView*InWindow`, and `gatherCommittedNoteEventsForPitch`. `maybeLogStoredNoteCount` kept.

**RC-L2 (withdrawn helper removed):** `gatherCommittedNoteEventsForPitch` is gone. Production note-off uses `overlapNoteIds` + `overdubSourceViewNotes_`. `test_overdub_source_view` now covers establish/clear only.

**RC-L3 (shipped, HITL verify open) — stored-MIDI verification off the stop path:** `emitStoredMidiVerification` ran inside MIDI button dispatch at overdub stop and cost 342.7 ms / 325.5 ms in [`013917`](../../captures/session_20260813_013917.log) (flush itself under 1 ms): a full `mergeActiveCapturePasses`, one `SEVT` line per note event over 3554 events, the capped wrap-pair scan, then a second full `reconstructNotes` for at most 32 `DNTE` lines. The ring overflowed at both stops, so most `SEVT` lines were dropped and the `seal` / `finalize` / `set_state` stage lines were evicted with them. Now `queueDeferredStoredMidiVerification` only sets a flag; `processDeferredStoredMidiVerification` drains from `processDeferredIdleMaintenance` in 64-event slices (16 while `hasDeferredSaveWork`), flattening once on the first slice and phasing notes → wrap pairs → display notes. Functions are `TRACK_COLD_MEM` — in ITCM they pushed RAM1 past a 32 KB block boundary.

**RC-L3b (shipped, device verify open) — one-shot + idle-only drain:** deferring during PLAYING still blocked MIDI for seconds because the first slice does a full merge ([`020631`](../../captures/session_20260813_020631.log) `msi` 2.95 s). Verification is now **one overdub stop per boot** (`#CAP,DIAG,stored_verify,armed,0` when disarmed; reboot to re-arm for another HITL evidence run). Drain runs only when transport is fully idle (same gate as REVT), not during PLAYING/OVERDUB. Second+ overdub stops in the same session skip verification so manual retests are not penalized.

**Architecture:** [`realtime_incremental_work_capture_overdub_architecture.md`](../Plans/realtime_incremental_work_capture_overdub_architecture.md)  
**Scheduling contract:** [`runtime_scheduling_admission_model_architecture.md`](../Plans/runtime_scheduling_admission_model_architecture.md)  
**Scheduling roadmap:** [`runtime_scheduling_owner_boundary_admission_refinement.md`](../Plans/runtime_scheduling_owner_boundary_admission_refinement.md) (O–T–R–C–A–P; interval reservation not authorized)  
**S0 (shipped code):** `RuntimeTimingTelemetry` — Tier-A `DIAG,midi_gap` / `midi_input` / `clk` / `tracks` / `clockrate` (5 s); observation only. Historical captures used `DIAG,msi` / `midisvc` for the same two measurements. Native `test_runtime_timing_telemetry` PASS. Playback gather Stage 1 adds `DIAG,late_on` / `late_off` / `late_clk` (5 s) plus `DIAG,late_event` / `DIAG,playback_build` one-shots — not a line per MIDI event. Do not start Stage 2/3 from this scheduling slice.  
**S0 device runs:** [`141815`](../../captures/session_20260812_141815.log), [`144323`](../../captures/session_20260812_144323.log) — DIAG timing lines lost across the whole capture pass; two root causes fixed (see [investigation §31a](../Plans/archive/refinements/runtime_scheduling_timing_envelope_investigation.md#31a-s0-device-runs--first-results-2026-08-12)):
- **RC-S0a** `isTierATextLine` skipped two commas, so Tier-A classification was inert and the ring evicted every DIAG window. Parse extracted to `CaptureLineTier::isTierALine` (`test_capture_line_tier` PASS); Tier-A may now only be displaced by Tier-A.
- **RC-S0b** `MemoryMonitor::logStatus()` external-pool walk blocked the loop **593 ms** and lost external MIDI clock. Walk is now `setup()`-only (`logStatus(true)`); runtime reports `pool_size` (O(1)). Guide exemption removed.

**S0 run [`145555`](../../captures/session_20260812_145555.log):** DIAG timing windows survive through RECORD + both overdubs. `midisvc` 0.9–1.1 ms during RECORD vs **129–149 ms** during PLAYING/OVERDUB (762.5 ms at overdub entry), against `clk`/`tracks` ≤ 8.83 ms — `handleMidiInput()` duration is the dominant term and clock dispatch is not. **RC-S0c:** windows before 177 s were lost because `flushCaptureBuffer` never transmits Tier-A under the timing-critical budget; fix is a bounded Tier-A transmit allowance (roadmap O2 / [investigation §31b](../Plans/archive/refinements/runtime_scheduling_timing_envelope_investigation.md#31b-s0-run-145555--first-capture-phase-envelope)).  
**Regression vs [`9678c3d`](../Plans/archive/refinements/runtime_scheduling_timing_envelope_investigation.md#31d-regression-vs-9678c3d--display-lag-and-transition-feel):** overdub display lag is the new `resolveDisplayNotesLiveCapture` budget bailout — measured resolve is 23–31 µs·10³ against a 5000 µs budget, so `reuseLastValidFrame` holds a stale frame for most of both overdub passes. Transition feel is the Clock dispatch reorder (Clock no longer transport-first).  
**Shipped (observation only):** compose sub-step instrumentation — `DisplayCommittedRebuildTime`, `DisplayCaptureReplaceTime`, `DisplayCaptureSyncTime`, plus the previously dead `DisplayCaptureGatherTime` / `DisplayCaptureFullGather` slots and branch counters `DisplayCommittedWindowFilter` / `DisplayCommittedFullAssign`. Native 1034/1034; `teensy41-capture-serial` builds.  
**S0 run [`152948`](../../captures/session_20260812_152948.log):** resolve attributed — `rebuildDisplayNotesInWindow` 25.7 ms × 22 calls (the spikes) and `filterDisplayNotesByWindowInclusion` 5.48 ms × 622 calls (the sustained over-budget). `replaceCaptureLayer` 0.10 ms, `synchronizeCaptureLayer` 0.01 ms — not factors. See §31e.  
**RC-D (new, outranks the above):** the OLED repaints every loop iteration — ~14 ms frame period against a 30 ms cadence, ~93 % of loop time in `DisplayManager::update()` from boot. `invalidateProjectedNoteEditDisplayCache` is raised by the general `invalidateLiveDisplayCache` path with no note-edit precondition, but `markNoteEditDisplayPainted` only clears it when `isNoteEditActive()`, so the ungated `maybeUpdateDisplayForNoteEditSelection` path latches on permanently.  
**RC-D fix (shipped):** `markNoteEditDisplayPainted()` now runs unconditionally at the end of the normal paint path in `DisplayManager::update`. Note-edit behaviour unchanged (the guard was already true there). Residual: the load/save overlay branch returns before the ack by design, so the latch can still spin while that overlay is open. Native 1034/1034; `teensy41-capture-serial` builds.  
**RC-D verified [`155132`](../../captures/session_20260812_155132.log):** 32.7 fps (was 70–78); display 45 % of loop time (was ~93 %); idle `msi` 15.3 ms (was ~34 ms); RECORD `midisvc` 604–905 µs. See §31f.
**Still open — dominant path (historical):** `midisvc` 126–146 ms through PLAYING/OVERDUB was later attributed S0b–S0e (`usbnote` / `notechg`) and reduced by RC-K1–K3.
**RC-E fix verified [`165636`](../../captures/session_20260812_165636.log):** `adopt_partial` reports partial coverage and idle slices backfill the whole loop in ~0.6 s (359 → 1 352 notes, then 327 → 1 417). Permanent starvation gone.
**RC-F fix shipped:** clean-cache overdub filter now gathers the paint window widened by `kWindowedGatherMarginBars = 2` and records it — the committed layer follows auto-follow instead of freezing. Verified in [`172405`](../../captures/session_20260812_172405.log): `wNotes` holds at 300–313 across the second overdub.
**RC-F follow-up shipped:** the first fix let the staleness predicate fire during the post-commit dirty window, where every rebuild took the 27.9 ms full-gather branch (30 gathers, 70 of 241 frames over budget, tails on 6 of 241) — that was the bar-boundary stutter and the "notes only appear after a bar". The resolve path no longer gathers: while `visualCacheDirty`, the committed layer is held and recovery is left to `processDeferredIdleMaintenance`; `committedWindowStale` requires a clean cache, and `committedLayerCleanCacheReady` rebuilds once when idle finishes. See §31g-3.
**RC-H reverted (RC-I):** the overdub-stop handoff preserve (`liveOverdubStopHandoffActive_`) pinned the partial adopted frame permanently — [`183429`](../../captures/session_20260812_183429.log): `adopt_partial notes=394`, then `slice_clean notes=1105`, but `DFRAME 394` held from 209.6 s through 214.2 s. The 174843 window content was density-correct; static viewport at STOPPED is expected. Reverted to pre-RC-H behaviour; `preservedHandoffAuthority` covers the ~0.6 s dirty window. **Verified [`191356`](../../captures/session_20260812_191356.log):** no freeze across two overdub clusters; idle `slice_clean` covers the loop; STOPPED paints the 16-bar window of the completed cache.
**RC-J (open, Owner-Boundary Gate G6):** post-stop persistence stall — `timingCriticalTrackActive` goes false at STOPPED while the external clock still streams. Reproduced in [`191356`](../../captures/session_20260812_191356.log): first stop `clockrate` 48→24→0 `msi` 467 ms; final stop 48→36→0 `msi` 365 ms. Do not patch ad hoc; do not widen `timingCriticalTrackActive`.
**RC-G fix shipped:** `updateOverviewCaptureDensity` builds a per-bar × 8-pitch-band mask incrementally from the capture preview (O(new notes) per frame), and `drawOverviewStrip` renders O(loop bars) from it when no clean `visualCache` exists. Record overview now shows the whole loop. Device verify pending for both.
**Remaining findings:** S0e attributed in [`204221`](../../captures/session_20260812_204221.log); RC-K1–K3 / RC-L1 device-verified in [`225803`](../../captures/session_20260812_225803.log). RC-S0c still owed (roadmap O2). RC-J is Owner-Boundary G6, not S0b. Display frame-skip / rolling-window during overdub is RC-F follow-up + investigation §31d.
**Overdub display lag:** `DisplayResolveLiveCapture` 33.6 / 33.8 ms during the second overdub against a 5 000 µs budget — the §31d bailout skips `replaceCaptureLayer` and the tails, so live overdub notes are not composed.
**Also open:** display resolve is under budget only by margin — the window filter measures 4 595 µs against a 5 000 µs line, and the gather still peaks at 26.3 ms. A 140 ms post-stop `msi` stall with `midisvc` at ~0.1 ms is uninvestigated.  
**RC-C device:** [`115913`](../../captures/session_20260812_115913.log) — timing PASS; display D1–D2 FAIL.  
**Regression:** [`122003`](../../captures/session_20260812_122003.log) — dual idle slice caused MIDI lag / clock lost (reverted).  
**Now:** Device re-measure after RC-K1b is closed on [`225803`](../../captures/session_20260812_225803.log). Remaining scheduling work is the owner-boundary roadmap, not interval reservation. Plan [`realtime_incremental_work_overdub_note_change_bugfix.md`](../Plans/realtime_incremental_work_overdub_note_change_bugfix.md).

### Long record onset display freeze — [`012342`](../../captures/session_20260812_012342.log)

**Plan:** [`long_record_onset_display_freeze_bugfix.md`](../Plans/long_record_onset_display_freeze_bugfix.md) — RC-A shipped; RC-C promoted via architecture doc above.  
**Evidence:** fail [`012342`](../../captures/session_20260812_012342.log); interim PASS [`013747`](../../captures/session_20260812_013747.log).

### Stage 5 — memory / persistence pressure

**5a (closed):** 5a-1/5a-2 shipped; 5a-3 **`pool_alloc` proof abandoned** (2026-08-12).  
**5b:** boot deferred save + clear gating — **parked**.

### Codebase consistency & maintainability — Phase 4 + LR complete

**GitHub:** [#18](https://github.com/Lytrix/MidiLooper/issues/18) · **Plan:** [`codebase_consistency_phase4_extraction_boundaries_refinement.md`](../Plans/codebase_consistency_phase4_extraction_boundaries_refinement.md)  
**Shipped:** TrackManager #22, NoteEditGeometryApply #23, DisplayNoteResolve #24, NoteEditFocus header #25, Phase LR #26. **Next:** close #18; pick next queue item (HITL CLI Phase 3 or persistence — see table below).

### StorageManager TU extraction — shipped (PR #17)

**GitHub:** [#16](https://github.com/Lytrix/MidiLooper/issues/16) — closed · [PR #17](https://github.com/Lytrix/MidiLooper/pull/17) merged to `dev`

### HITL CLI rebuild — Phase 3 (`base` + `edit_full` only)

**OpenSpec:** [`openspec/changes/hitl-cli-rebuild/`](../../openspec/changes/hitl-cli-rebuild/)  
**Plan:** [`docs/Plans/hitl_cli_rebuild_enhancement.md`](../Plans/hitl_cli_rebuild_enhancement.md)

| Phase | Status |
|-------|--------|
| 0–2 — foundation, scenarios | **Done** (2026-08-04) |
| 3 — layered **`base`** + **`edit_full`** | **In progress** — 3.2 bridge stabilized; next: 3.3 device PASS |
| 4–5 — corpus docs, archive | Pending |

**Layered presets (active):** `base` (`record_overdub`), `edit_full`. Do **not** wire `edit_minimal`, `revision_*`, `load_save_*`, `fader_motor_*`, etc. in this change.

**Phase 3 exit:** Mode B device PASS for `--layered --preset base` and `--layered --preset edit_full`.

### Persistence — pick one track (not all parallel)

**Guides:** [`RUNTIME_STORAGE_AND_PERSISTENCE.md`](../Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md) · **Handoffs:** [`set_revision_persistence_handoff.md`](../Plans/set_revision_persistence_handoff.md), [`continuous_runtime_persistence_phase5_recovery_handoff.md`](../Plans/continuous_runtime_persistence_phase5_recovery_handoff.md)  
**Work-queue baseline (shipped):** [`current_set_persist_work_item_queue_enhancement.md`](../Plans/current_set_persist_work_item_queue_enhancement.md)

| Track | OpenSpec / plan | Remaining | When to pick |
|-------|-----------------|-----------|--------------|
| **A — Overlay loop picker** | `set-revision-persistence` §4.8–4.10 | **Parked** — WIP stashed on `feature/set-revision-loop-picker` | User requests overlay milestone |
| **B — Crash recovery** | `continuous-runtime-persistence` Phase 5 | `.sealj` / slot **prefix load**, quarantine tail, native fixtures | After Layer D gate; orthogonal to overlay |
| **C — Admit API migration** | [#18](https://github.com/Lytrix/MidiLooper/issues/18) Phase 1.3 | `admitLoopSlotPersist` → `admitLoopPersist(LoopId)` at domain call sites | Hygiene with #18 closeout |
| **D — Layer D overdub rebuild** | DEC-035 Stages 6–7 / DEC-037 | **3b shipped** — successor [`loop_event_sourced_resolution_architecture.md`](../Plans/loop_event_sourced_resolution_architecture.md) | Active prototype |
| **E — Parked** | overlay hang, 3.9 failsafe | [`persistence_overlay_large_slot_focus_restore_bugfix.md`](../Plans/persistence_overlay_large_slot_focus_restore_bugfix.md); set-revision §3.9 | Investigation only |

**DeferredJobScheduler Phase B:** **Archived** [`2026-07-19-deferred-job-scheduler`](../../openspec/changes/archive/2026-07-19-deferred-job-scheduler/). Specs: `deferred-job-scheduler/`, `lazy-slot-hydration`.

**DEC-020 note:** Phases 0–4 **shipped** on `dev`. Phase 5 was historically **paused** pending wrap-fix validation — that does not block overlay track A; confirm with user before starting B if wrap HITL is still open (see § Parked wrap investigation below).

---

## Recently shipped (2026-08)

| Slice | Decision / commit | Evidence |
|-------|-------------------|----------|
| Overdub session index reboot undo | DEC-038 amendment; `overdubSessionIndex` + `OSI1` | [`overdub_session_index_reboot_undo_bugfix.md`](../Plans/overdub_session_index_reboot_undo_bugfix.md); HITL [`003854`](../../captures/session_20260820_003854.log); native 1397/1397 |
| Overdub wrap source-view D2-D | Revision guard in `committedPlaybackNoteOnIdentityValid` | [`overdub_wrap_source_view_display_drops_committed_bugfix.md`](../Plans/overdub_wrap_source_view_display_drops_committed_bugfix.md); HITL [`174246`](../../captures/session_20260819_174246.log); native 1384/1384 |
| Occupy source-view Gate 5A + RC close | Equal-tick Off-before-On; sibling D2-D closes pin | [`overdub_occupy_source_view_keeps_resolver_geometry_bugfix.md`](../Plans/overdub_occupy_source_view_keeps_resolver_geometry_bugfix.md); HITL [`161349`](../../captures/session_20260819_161349.log), [`174246`](../../captures/session_20260819_174246.log) |
| Overdub pass overlap (G2) | DEC-031/032; archived `2026-08-12-overdub-pass-overlap-resolution` | Native 1016/1016; OLED PASS [`010000`](../../captures/session_20260812_010000.log); specs synced; merge PR pending |
| Live-record tick-0 NoteOn blip | `8de682c` | Native 999/999; device PASS [`182949`](../../captures/session_20260811_182949.log); pre-fix [`182528`](../../captures/session_20260811_182528.log) |
| Note edit current state | DEC-029; `3e9253e` | Native 969/969; [`PHASE8_CLOSEOUT`](../../openspec/changes/archive/2026-08-08-note-edit-current-state/PHASE8_CLOSEOUT.md); HITL [`112202`](../../captures/session_20260808_112202.log), [`115120`](../../captures/session_20260808_115120.log), [`032118`](../../captures/session_20260808_032118.log) |
| StorageManager TU remaining trim | PR [#17](https://github.com/Lytrix/MidiLooper/pull/17) → `dev` (2026-08-08) | [#16](https://github.com/Lytrix/MidiLooper/issues/16); [`storagemanager_translation_unit_extraction_refinement.md`](../Plans/storagemanager_translation_unit_extraction_refinement.md) |
| Codebase consistency Phase 4 — DisplayNoteResolve | PR [#24](https://github.com/Lytrix/MidiLooper/pull/24) → `dev` (2026-08-10) | Native 969/969; [#18](https://github.com/Lytrix/MidiLooper/issues/18) |
| Codebase consistency Phase LR — NoteMovementUtils shims | PR [#26](https://github.com/Lytrix/MidiLooper/pull/26) → `dev` (2026-08-10) | Native 969/969; [#18](https://github.com/Lytrix/MidiLooper/issues/18) |
| Codebase consistency Phase 4 — NoteEditFocus header | PR [#25](https://github.com/Lytrix/MidiLooper/pull/25) → `dev` (2026-08-10) | Native 969/969; [#18](https://github.com/Lytrix/MidiLooper/issues/18) |
| Codebase consistency Phase 4 — TrackManager TU | PR [#22](https://github.com/Lytrix/MidiLooper/pull/22) → `dev` (2026-08-08) | Native 969/969; manual [`225737`](../../captures/session_20260808_225737.log); [#18](https://github.com/Lytrix/MidiLooper/issues/18) |
| Codebase consistency Phase 1 — authority | PR [#19](https://github.com/Lytrix/MidiLooper/pull/19) merged to `dev` (2026-08-08) | Native 969/969; HITL [`163904`](../../captures/session_20260808_163904.log); [#18](https://github.com/Lytrix/MidiLooper/issues/18) |
| Playing move/length audition | `15c5750` | [`113626`](../../captures/session_20260808_113626.log), [`115120`](../../captures/session_20260808_115120.log); [bugfix doc](../Plans/note_edit_playing_move_audition_bugfix.md) |
| Resolver §12 orthogonal state | DEC-030; `7af8671` | Native 969/969; HITL [`112202`](../../captures/session_20260808_112202.log) @88.669 |
| Edit-session-action-geometry archive | 2026-08-05 | [`openspec/specs/edit-session-action-geometry/`](../../openspec/specs/edit-session-action-geometry/) |
| Docs folder hygiene Phase 3 | 2026-08-08 | Root doc roles; `PROJECT_INTENT` redirect; `FEATURES` / `FEATURE_PLANS` banners |
| Docs folder hygiene Phase 2d | 2026-08-08 | 9 `Refinements/` logs → `Plans/archive/refinements/` |
| Docs folder hygiene Phase 2c | 2026-08-08 | 2 FROZEN `*_bugfix.md` → `Plans/archive/bugfix/` |
| Docs folder hygiene Phase 2b | 2026-08-08 | 10 `*_handoff.md` → `Plans/archive/handoff/` |
| Docs folder hygiene Phase 2a | 2026-08-08 | 43 `*.plan.md` → `Plans/archive/cursor-exports/`; 4 retained at root |
| Docs folder hygiene Phase 1.5 | 2026-08-08 | PascalCase `docs/` folders (`Authority/`, `Plans/`, `Runtime/`, `Agents/`, `Templates/`) |
| Docs folder hygiene Phase 1 | 2026-08-08 | Four-bucket `docs/README.md`; slim `Plans/README.md`; `HITL_ARCHITECTURE` → `Authority/Architecture/` — [`docs_folder_hygiene_refinement.md`](../Plans/docs_folder_hygiene_refinement.md) |

Normative specs: `note-edit-current-state`, `note-edit-modification-session`, `note-edit-session-undo`, `internal-heap-external-memory-routing`, `edit-session-action-geometry`.

---

## Parked / closed (queue references)

### Edit HITL from scratch — parked

**Plan:** [`docs/Plans/m8_edit_note_edit_hitl_automation_refinement.md`](../Plans/m8_edit_note_edit_hitl_automation_refinement.md)

Deferred from **edit-session-action-geometry** Phase 5 (D14 full matrix). Interim smoke: `edit_minimal` preset only.

### Firmware ownership / lifetime review — closed (2026-08-06)

**Plan:** [`docs/Plans/firmware_ownership_lifetime_review.md`](../Plans/firmware_ownership_lifetime_review.md) — P0/P1 **done**; Phase 5 recovery and layered **`base`** HITL **parked**.

### Note edit undo after reboot — done

**Plan:** [`.cursor/plans/note_edit_undo_reboot_58ef9394.plan.md`](../../.cursor/plans/note_edit_undo_reboot_58ef9394.plan.md) — HITL **PASS** [`session_20260805_212234`](../../captures/session_20260805_212234.log).

### Hygiene — codebase debt review — complete

**Plan:** [`docs/Plans/codebase_hygiene_technical_debt_review.md`](../Plans/codebase_hygiene_technical_debt_review.md) — safe hygiene **done** on `chore/codebase-hygiene-sprint1`.

### Note edit control-surface split — complete

**Plan:** [`docs/Plans/note_edit_control_surface_split_refinement.md`](../Plans/note_edit_control_surface_split_refinement.md) — Phases 0–8 on `dev`.

---

### OpenSpec — slot-performance-interaction Phase −1 (2026-07-19)

Rename-only merge-cache vocabulary (no behavior change). Remaining phases stay queued.

| Item | Status |
|------|--------|
| −1.1–−1.4 rename struct/field/APIs | Done |
| −1.5 native tests | Done (663) |

### Recently closed — Deferred job scheduler (Phase B)

**Archived:** `2026-07-19-deferred-job-scheduler`  
**Specs synced:** `deferred-job-scheduler` (new), `lazy-slot-hydration` (gate note).

| Step | Status |
|------|--------|
| B.1 thin `runFrame` | PASS [`231510`](../../captures/session_20260718_231510.log) |
| B.2 `stepSubmittedLoadJobs` | Done |
| B.3 select then step + `LoadLoopSelectionPolicy` | Done |
| B.4 native + device | PASS [`022107`](../../captures/session_20260719_022107.log) |

### Prioritized boot load isolation (merged to `dev`)

Shipped via PR #4 on `feature/memory-pressure-reclaim`.

---

### Recently closed — Unified commit lazy slot load (Phase A)

**Archived:** `2026-07-18-unified-commit-lazy-slot-load`  
**Specs synced:** `lazy-slot-hydration`, `loop-commit-semantics`, `multi-loop-slots`, `playback-runtime-prewarm`, `timeline-passes`.

| Item | Status |
|------|--------|
| A.6 parse split + A.7 PSRAM headroom fix | **PASS** [`230145`](../../captures/session_20260718_230145.log) |
| 6.2-device interactive | **PASS** [`224607`](../../captures/session_20260718_224607.log) |
| Parked | 6.1 DERIVED_READY; 6.3 undo/import Commit docs |

---

### Recently closed — Slot queue LOOP_EDIT depart

**Plan:** [`docs/Plans/slot_queue_loop_edit_depart_bugfix.md`](../Plans/slot_queue_loop_edit_depart_bugfix.md)

| Item | Status |
|------|--------|
| No mid-play geometry write on LOOP_EDIT depart | **PASS** |
| Device gate (queue slot, LoopEnd commit, no hang) | **PASS** [`004331`](../../captures/session_20260718_004331.log), reconfirmed [`010126`](../../captures/session_20260718_010126.log) |
| Queued-launch musical-time countdown | **In tree** — OLED field; LoopEnd commits present in `010126` |

---

### Paused — Memory pressure reclaim (M6 follow-on)

**Branch:** `feature/memory-pressure-reclaim`  
**Plan:** [`docs/Plans/memory_pressure_reclaim_refinement.md`](../Plans/memory_pressure_reclaim_refinement.md)

| Phase | Scope | Status |
|-------|--------|--------|
| **1A** | Advisory FSM + `#CAP,DIAG,pressure` | **Shipped** (`f09f547`) — validated in [`session_20260714_233620.log`](../../captures/session_20260714_233620.log) |
| **1B** | Low reclaim (`try*` owner APIs; background-first) | **Shipped** — pending manual gate |
| 2 | Critical undo trim + persistence inversion | Paused |
| 3 | Replace scattered heap thresholds | Paused |
| 4 | Manual gate 215312; idle defer; OpenSpec archive | Paused |

**Manual gate (1B):** Re-run stress session; confirm reclaim under Low without playback glitches / dropped MIDI on selected track.

---

## Recently landed (M6 Ph 1–2 — merged to `dev`)

**Commits:** `50e0f6e`…`11025ca` on `dev`

| Item | Status |
|------|--------|
| Phase A metrics | Shipped |
| Phase 1 playback window (DEC-016 chunk merge) | Shipped |
| Phase 2 display stale-while-revalidate | Shipped |
| Record-start headroom + live-record display | Shipped |
| Native | **608/608** |
| Manual | **PASS** no crash — [`session_20260714_215312.log`](../../captures/session_20260714_215312.log) (64-bar, 7-track overdub; 42 append failures remain) |

**Plan:** [`multi_track_playback_pressure_closure_refinement.md`](../Plans/multi_track_playback_pressure_closure_refinement.md)

---

## Recently landed (persistence work queue — on `dev`)

**Branch:** `feature/persistence-work-queue` — [`current_set_persist_work_item_queue_enhancement.md`](../Plans/current_set_persist_work_item_queue_enhancement.md)

| Phase | Status |
|-------|--------|
| B1–B4 | Work queue admission, scheduler, monolith retire |
| B4 follow-up | Sync-drain budget, clear-slot SD-restore, urgent save / mid-pass defer |
| B5 | **604/604** native; HITL **PASS** [`host_midi_automation_serial_20260714_153240.log`](../../captures/host_midi_automation_serial_20260714_153240.log) |

---

## Recently landed on `dev` (pre-queue)

### Bugfix: record-stop MIDI flood — **restore baseline playback, pending HITL**

**Evidence:** [`session_20260713_165557.log`](../../captures/session_20260713_165557.log) — 571 note-ons over 5 pitches in 704 ticks; BPM collapse; `RING,overflow`.

**Fix:** Restored `901c4d9` playback send/anchor (`reanchorPlaybackProjection`, inline `atLoopStart`, `loop.midiEvents()` window). Kept display storage-frame playhead + persistence grace.

**Plan:** [`docs/Plans/record_stop_playback_hang_bugfix.md`](../Plans/record_stop_playback_hang_bugfix.md) (phase 4c)

| Gate | Status |
|------|--------|
| Native | **562/562** |
| `teensy41-capture-serial` build | **SUCCESS** (2026-07-13) |
| HITL / manual | **User** — record→stop→PLAYING; no MO flood; playhead bar 0 |

---

### Bugfix: record-stop coordinate frame (phase 4) — **superseded by 4c flood fix**

---

### Bugfix: overdub wrap note-off pairing — **implemented, pending HITL**

**Follow-up to capture ownership refactor** ([`overdub_wrap_note_off_pairing_bugfix.md`](../Plans/overdub_wrap_note_off_pairing_bugfix.md)). Fixes wrong on/off pairing when notes are held across loop wrap.

**Changes:** `capturePhaseTick` / `appendCaptureNoteOffAtPhase`; finalize clears pending only after append; canonical `wrappedHeadOff`; stop diagnostics; 13 native tests in `test_capture_note_off_rules`.

| Gate | Status |
|------|--------|
| Native | **554/554** |
| `teensy41-capture-serial` build | **SUCCESS** |
| HITL | **User** — 132536 scenario: tail wrap pairs tail on before wrap, not N@0 |

---

### Bugfix: overdub wrap note-off capture ownership — **implemented, pending HITL**

**Scope:** Single close pipeline for open notes at record/overdub stop; playback read-only (no mid-wrap capture mutation). Aligns live capture with `buildCanonicalSpansFromMidi` wrapped linear storage.

**Changes:**
- Removed `closeOpenNotesAtLoopWrap`, `flushPendingNotesIntoCapture`, `removeCaptureNoteOffAt`
- `finalizePendingNotes(currentTick)` on all record/overdub stop paths (playhead close, not L-1)
- `Loop::sealCapture` passes playhead `openTailCloseTick` to `finalizeWrapWindowOnStore`
- Removed `recordMidiEvents` note-off repair (tick bump, L-1 removal)
- New native suite `test_capture_note_off_rules` (7 tests)

**Plan:** [`docs/Plans/overdub_wrap_note_off_capture_bugfix.md`](../Plans/overdub_wrap_note_off_capture_bugfix.md)

**Deferred:** playback wrap `double_on` ([`session_20260709_224935.log`](../../captures/session_20260709_224935.log)) — separate plan; do not touch `playMidiEventsForSlot` / shared `projectionCycleStartTick`.

| Gate | Status |
|------|--------|
| Native | **554/554** |
| `teensy41-capture-serial` build | **SUCCESS** |
| HITL wrap + capture | **User** — re-run 125437 scenario: balanced SEVT per pitch, no ch4 ghost offs, no lag regression |

---

### Bugfix: loop-wrap playback lag regression — REVERTED to `bc98491` (shipped `901c4d9`)

**Decision:** `19aa47a` ("Fix loop-wrap playback retrigger") and both follow-up wrap-tail rewrites (full-order scan; cursor drain) are **reverted**. `Track::playMidiEvents`, `Track::playMidiEventsForSlot`, `IntervalProjection` (`shouldPlaybackEmitWrapTailEvent` / `shouldPlaybackCrossEvent` helpers), and their tests are restored to `bc98491` — the state the user confirmed "worked perfectly." `closeOpenNotesAtLoopWrap` on overdub playback wrap is restored.

**Proof the wrap fix regressed playback (not the active track):** during the active track's overdub, background **slots** flooded ~1 MO per main-loop frame via `playMidiEventsForSlot`, starving the loop and freezing display ≈2 s ("lag around loop wrap"). MO by channel — good [`session_20260713_115434.log`](../../captures/session_20260713_115434.log) vs bad [`session_20260713_122107.log`](../../captures/session_20260713_122107.log): ch5 (active) 212→223 unchanged; ch4 (slot) 486→**3620**; ch4 ~1 ms-gap events 278→**3422**. `19aa47a` introduced it (ch4 486→2488); cursor drain worsened it (→3622).

**Deferred (not reintroduced yet):** original wrap **double-on** ([`session_20260709_224935.log`](../../captures/session_20260709_224935.log) — MO `double_on` at BAR, 54-bar loop). Any future fix must NOT touch per-frame slot playback / the shared one-per-track `projectionCycleStartTick`.

**Out of scope:** slot/bar queued seek `sendAllNotesOff`; unify pending-note flush at record/overdub stop; stop FSM changes.

| Gate | Status |
|------|--------|
| Native | **542/543** (known empty `test_capture_note_off_rules` suite) |
| `teensy41-capture-serial` build | **SUCCESS** |
| HITL wrap + stop | **User** — compare to 115434: no per-frame slot MO flood, no BAR bunching, no ~120 ms post-stop DISP lag |

---

### OpenSpec: [`continuous-runtime-persistence`](../../openspec/changes/continuous-runtime-persistence/) (DEC-020)

**Branch:** `dev` — Phases 0–4 shipped; **Phase 5 not started**

| Phase | Status |
|-------|--------|
| **0** Diagnostics | **Complete** |
| **1** Chunk lifecycle | **Complete** |
| **2** Persistence queue | **Complete** |
| **3** Cooperative scheduler | **Complete** — overdub-stop HITL passed (`f0ee520`) |
| **4** Mid-pass persistence | **Shipped** — native + **HITL passed** [`session_20260709_171043.log`](../../captures/session_20260709_171043.log) (64+64); boot restore [`session_20260709_171951.log`](../../captures/session_20260709_171951.log) |
| **5** Recovery | **Not started** — longest valid prefix load + quarantine tail — [**handoff**](../Plans/continuous_runtime_persistence_phase5_recovery_handoff.md) — **CURRENT_WORK pick track B** |
| **6** Full 64+64 HITL | **Mostly evidenced** (same log) — archive checklist + `oldestDirtyChunkAge` review remain |

| Gate | Owner | Status |
|------|-------|--------|
| Native | Agent | `pio test -e native` — **969/969** (2026-08-10) |
| `test_storage_loop_io` | Agent | Run before Phase 4 archive sign-off |
| Phase 4 HITL (64+64 record/overdub) | **User** | **PASS** — [`session_20260709_171043.log`](../../captures/session_20260709_171043.log) |
| Phase 4 HITL (cold-boot restore) | **User** | **PASS** — [`session_20260709_171951.log`](../../captures/session_20260709_171951.log) |
| Phase 5 native fixture | Agent | Prefix load from truncated `.sealj` / slot — **not started** |

**HITL policy:** stop/crash scenarios — user captures serial manually (`capture_session.py`); agent does not loop HITL. On crash, user bisects and shares log before stop-path patches.

| Doc | Role |
|-----|------|
| Agent map | [RUNTIME_STORAGE_AND_PERSISTENCE.md](../Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md) |
| **Phase 5 handoff** | [continuous_runtime_persistence_phase5_recovery_handoff.md](../Plans/continuous_runtime_persistence_phase5_recovery_handoff.md) |
| OpenSpec | [continuous-runtime-persistence](../../openspec/changes/continuous-runtime-persistence/) |

---

## Landed on branch (recovery merge — no further agent work unless HITL fails)

| Area | Commit / note | HITL |
|------|----------------|------|
| NOTE_EDIT move/pitch bracket | `736fa33` | User: `session_20260709_155307` replay |
| Split-focus slot switching | `0f086f4` (DEC-025) | User: preview slot + loop-end commit |
| Slot short-press / deferred restore | `f6b496a` | User |
| Playhead + undo geometry | `346f7ce` | User: `session_20260708_233241` |
| Loop-scoped undo (DEC-024 Ph 1) | `b1d3259` | Native only |
| Boot load + slot scan | `50ad01b` | User: cold boot ×5 |
| Slot clear + arm state | `2a565aa` | User |

---

## Parked

### DMA OLED frozen frame — after runtime-working-set-ownership

User explicitly defers this fault until the active OpenSpec is complete. Blocking
SPI is the installed, visually working comparator, not a proven timing-equivalent
replacement. Default builds still enable DMA. Reproducer/config and boundaries:
[NEXT-STEPS.md](../../openspec/changes/runtime-working-set-ownership/NEXT-STEPS.md).


### Later — bound deferred wrap-append ([`183229`](../../captures/session_20260821_183229.log))

**Owner:** `Loop::applyDeferredVisualCacheWrapAppend`  
**Evidence:** `idle_append=1122618` at 44.5 s. Stage C moved wrap-append off the undo/switch turn; the later turn is still unbounded.  
**Plan:** [`runtime_scheduling_owner_boundary_inventory_investigation.md`](../Plans/runtime_scheduling_owner_boundary_inventory_investigation.md) §37. Do not start until a new slice is opened.

### Later — attribute 5 s STOPPED idle ([`183229`](../../captures/session_20260821_183229.log))

**Owner:** unknown until attributed.  
**Evidence:** `idle_maint=5028348` at 49.7 s between track 6 and 7; no `idle_gather` / `idle_append` child. Investigation first.  
**Plan:** §37. Do not start until a new slice is opened.

### Tagged for investigation — occupy id resolves to no source-view note (`norow`) — superseded

**Superseded by:** [`overdub_consume_id_resolution_completeness_bugfix.md`](../Plans/overdub_consume_id_resolution_completeness_bugfix.md) Stage 1 — hold fill before id lookup. Native fixture `test_consume_id_resolution_norow_repaired_by_hold_fill`. HITL gate open post-flash.

### OpenSpec: [`unified-capture-commit-owner`](../../openspec/changes/unified-capture-commit-owner/) (DEC-023)

Phases 1–3 prototype **reverted** at `40db4df` (boot bisect). Storage boot recovery helpers (`37f6b00`) landed. Retry Track/Loop slices only after stable boot + user approval.

### Derived note overlap (`edit-session-action-geometry`) — **archived 2026-08-05**

Pipeline shipped; normative [`openspec/specs/edit-session-action-geometry/`](../../openspec/specs/edit-session-action-geometry/). Phase 5 HITL matrix **parked** — see Edit HITL plan above. Historical handoff: [`derived_note_overlap_logic_handoff.md`](../Plans/derived_note_overlap_logic_handoff.md).

### Prior: [`runtime-derived-representation-heap`](../../openspec/changes/runtime-derived-representation-heap/)

M6 Ph 1–2 on `dev`; pressure reclaim + Ph 3–4 in [`memory_pressure_reclaim_refinement.md`](../Plans/memory_pressure_reclaim_refinement.md).

---

## Do not start without decision

- DEC-023 capture-commit Track slices (after Phase 5 or explicit user go)
- `currentset-savedset-storage-layout`
- D13 jam-recording (ROADMAP — post JamRecorder)


2026-09-16 task-2.3 integration checkpoint: production canonical stop preparation
now uses a separately reserved output Capture instead of mutating raw capture
chunks. Explicit-source transfer/preview APIs and full-pool admission checks are
covered by native tests. The service-paused Button A/manager/Track reentry tests
remain red: FIFO, live/head staging, preview/session/undo/snapshot ownership,
causal sources and generic-reset lifetime isolation are not implemented. Keep
2.3 and HITL open; see the active change's CONTINUOUS-CAPTURE-OWNERSHIP.md for
bounds, evidence and limitations. No firmware upload or device action occurred.
**DEC-070 probe-driven fix implemented:** release the retained tail cache at the retarget, keep the companion/undo estimates, walk to `commitStoppedWithUndo`, bounded fallback as a safety net only. Native 2,213 / 1 skipped / 0 failures; RAM1 locals 6,400; `captures/successor-wrap-44` overflowed the capture ring so the 5.9 display/save/undo acceptance still needs one clean run.
**DEC-070 second attempt (both caches released) still falls short** (`captures/successor-wrap-45`: `companions=kept` then `companion path stalled; raw pass only` 0.76 s later). The remaining denier is `beginFrozenCanonicalTail`'s `captureAttached()` guard against the predecessor's retained canonical inputs; clearing it invalidates the canonical prefix, which needs playback turns. The fix belongs at retention time: skip `retainCommittedCanonicalTail` in `completeDeferredCleanup` when a successor interval is already live (`job.interval != captureInterval_`). 5.9 stays open.
**DEC-070 retention guard (5.11) implemented** (`3079d8d9`): `completeDeferredCleanup` retains the committed canonical tail only while the job is still the live interval. Native green; `captures/successor-wrap-46` shows the save completing (`persistence handoff result=ok`) but the retarget still stalls (`companion path stalled; raw pass only`, `overdub_ok=False`), so the blocking `captureAttached()` belongs to the live interval's own canonical inputs, not to the predecessor's retention. Next: instrument where that attachment is set. 5.9 open.

### OLED frozen-first-frame investigation (2026-09-26)

User confirms the normal piano roll with the initial 20-bar track-1 content remains
frozen while MIDI responds. Live serial attached at 115200 without reboot (uptime
continues). Original `DISP` length 15360; later display records length 768 and
STOPPED → PLAYING → OVERDUBBING → STOPPED while DMA frames continue increasing,
with no reported timeout/rejection. These counters prove software progress, not
updated pixels on the panel. Root cause remains UNKNOWN. Existing snapshot
preconditions and recovery are not patched on this evidence.

Built the existing blocking-SPI comparator (`MIDILOOPER_DISPLAY_DMA=0`) using the
same capture-serial environment and unchanged source in an isolated build directory.
Build SUCCESS; not uploaded. Configuration, log and image hash:
`captures/display-blocking-diagnostic-20260926/`. Serial capture remains open in
`captures/recovery-display-hang-20260926/`; latest user-triggered save completed.
Hardware comparison requires explicit upload approval per repository rule; this is
a diagnostic variant, not a production scheduling fix or a closed display gate.

**2026-09-26 — blocking-SPI diagnostic uploaded with explicit user approval.**
Persistence idle verified before reset. Capture-serial upload SUCCESS using
`captures/display-blocking-diagnostic-20260926/platformio.ini`; actual device
`#DMA_XFER` shows init=0, dma_frames=0, increasing blocking_frames. Serial capture
remains open. Visual outcome awaiting user; no display fix or timing gate claimed.
Recovery source unchanged. Image/config identity is pinned in that capture directory.

**2026-09-26 — blocking-SPI visual comparison PASS (user observed).**
User confirms “Yes, the screen updates now.” Same recovery/renderer source, existing
`MIDILOOPER_DISPLAY_DMA=0` switch only. Device reached load=ok, restore admission
complete, undo hydrated and transport stopped. This isolates the frozen-screen
behavior to the DMA display route; it does not establish the precise DMA defect.
Blocking SPI remains installed as a diagnostic workaround; no real-time timing
acceptance or permanent DMA fix claimed. Serial capture remains open.
Evidence: `captures/display-blocking-diagnostic-20260926/metadata.json` and session log.
