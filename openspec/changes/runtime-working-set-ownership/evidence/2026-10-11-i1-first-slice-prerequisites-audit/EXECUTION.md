# I1 — first-slice prerequisite audit (audit-only) — execution packet

## Identity and scope

- **Packet:** I1 (audit-only) of [`docs/Plans/openspec_iterative_closeout_plan.md`](../../../../../docs/Plans/openspec_iterative_closeout_plan.md),
  iteration manifest. Plan status: guidance and handoff only — not implementation authority, not firmware
  authorization, not a checkoff.
- **Task clauses opened (only these):** `S2b.0 **First-slice prerequisites (same commit boundary).**`,
  `S4f.8 Make the waits observable as **scheduling**, not refusal`, `S4f.9 Prove no new capacity is implied`,
  all in [`tasks.md`](../../tasks.md).
- **Contract cited by those clauses:** [`IMPLEMENTATION_CONTRACT.md`](../../IMPLEMENTATION_CONTRACT.md) §1
  (executable first slice: boot/load canonical admission) — cited, not duplicated.
- **Evidence packets the packet names, opened and checked against the map:**
  [`evidence/2026-10-10-edge-effect-boot/EXECUTION.md`](../2026-10-10-edge-effect-boot/EXECUTION.md) and
  [`evidence/2026-10-11-record-return-recovery/EXECUTION.md`](../2026-10-11-record-return-recovery/EXECUTION.md).
- **Revision:** branch `feature/unified-rolling-playback-path` @ **`37a63011`**, dirty tree with **45**
  pre-existing unrelated entries (modified/deleted/untracked) — **all preserved**. This packet added only this
  evidence folder. No rollback, reset or stash was performed, and no file outside this folder was modified by
  the audit itself.
- **Owner / entry:** none — this packet mutates no production path. The audited owners are named per clause in
  [CLAUSE-SYMBOL-TEST-CARRIER.md](CLAUSE-SYMBOL-TEST-CARRIER.md).
- **Mutation: NONE.** No firmware line, host-tooling line, test, fixture, threshold, baseline, marker or
  stimulus. No build, no upload, no reset, no SD action, no device contact, no Git commit/push/PR.
- **Explicit non-goals:** checking `S2b.0`/`S4f.8`/`S4f.9`; repairing a gap the audit finds; re-running or
  re-scoping a device gate; promoting native results to timing/device/deadline claims; entering I2–I8.

### Why this packet has a G2 execution packet at all

I1 changes no production behavior, so it is not gated by G2 in the usual direction. It is written because the
plan requires it ("open only the task clause it names ... and write its G2 execution packet before changing any
production behavior"), and because the useful output of an audit is exactly a **frozen oracle**: what the audit
may and may not claim. Per [`IMPLEMENTATION_READINESS.md`](../../../../../docs/Authority/IMPLEMENTATION_READINESS.md) § G2
execution packet (proportionality clause), the packet is short by design and states why existing checks suffice
rather than inventing a fixture.

## Historical decision review

Searches run against `docs/DECISION_LOG.md` (grep, no full read): accepted stop, stop latch, finalizer wait,
descriptor ring, stop denial, boot/per-slot reservation, and the ids named by the clauses (`DEC-073`, `DEC-078`,
`DEC-079`, `DEC-080`, `DEC-093`, `DEC-096`, `DEC-107`, `DEC-108`).

Binding context found, and how it constrains the audit:

- **DEC-078 item 10** — "The wait is observable as scheduling. `accepted_stop_finalizer_wait` counts waits —
  attributed to the holding owner — and is a **retry/scheduling counter**, not a user-visible denial"; the
  pre-fix counterfactual is `successor_stop_denied_capacity` + `reentryDecision("deny", …)`; the park terminal
  is a fault/escalation state, not cancellation. Item 9 states the capacity half verbatim ("An accepted request
  needs no new capacity -- it is one small piece of state per live interval, not a stored stopped interval -- so
  the descriptor-ring finding stands"), and item 7 restates the ring rule. **This is the authority behind S4f.8
  and S4f.9**; the audit measures the clauses against these two sentences only.
- **DEC-079** — `IMPLEMENTATION_CONTRACT.md` is the binding contract for the first-slice points; "Boot removal and
  its Stop-only retry/liveness prerequisites ship in one slice". That is the sentence `S2b.0` restates.
- **DEC-080** — per-slice transition oracles, production-path fixtures and evidence-backed completion; the audit
  therefore refuses to accept a document's statement as code and reads every symbol in the tree.
- **DEC-107** (bounded retirement wait), **DEC-108** (window-owned publication/readiness) — cited by the
  record-return packet; used here only as context for the deferred-finalization owner the clauses share.

No authority conflict was found between the clauses and `ARCHITECTURE_RULES.md` `PROJECT_INTENT.md`. No ownership,
lifetime, transition or budget question is opened, so no reassessment and no `DEC-###` append is required or made.

## Oracle frozen before any read of the clauses was turned into a claim

1. For each of the three clauses, name its production **symbol**(s), the **fixture** that asserts it, and the
   durable **carrier** that records it. A term with no symbol, no test or no carrier is reported as a **gap**,
   with the command or file that shows it — never smoothed by a document that merely describes the behavior.
2. A row may cite a clause text, a fixture name or a symbol. It may **not** claim a clause's acceptance, and it
   may not convert a checked sibling task into evidence for an unchecked one.
3. Characterization is exactly the manifest's three commands, reported by the totals the runs themselves print.
   A suite that passes on the current dirty tree is a **characterization of the tree**, not an acceptance of any
   clause and not a device/timing claim.
4. Where a task text's recorded measurement disagrees with the assertion enforced in code, both numbers are
   recorded and **code is named as the enforcement point** — no document overrides a compiler/device measurement.
5. Checkboxes, thresholds, baselines and tests are untouched. The deliverable is the map plus a run record.

## Verification recipe and what was actually run

| # | command (manifest order) | result (this tree @ `37a63011` + preserved dirty work) |
|---|---|---|
| 1 | `pio test -e native -f test_shared_capture_finalization` | **PASSED** — 85 cases / 85 succeeded, 1.632 s, exit 0 |
| 2 | `pio test -e native -f test_runtime_working_set_ownership` | **PASSED** — 190 cases / 190 succeeded, 4.484 s, exit 0 |
| 3 | `pio test -e native` | **PASSED** — **3,071 cases / 3,064 succeeded / 7 skipped / 0 failed**, 112 suites, 93.477 s, exit 0 |

Suite 1 is also the focused command of the record-return packet named above, and suite 2 is the suite the
edge-effect-boot packet names. Run 3's totals are **identical to the owner-approved reconciliation closeout's
recorded baseline** (3,071 total / 3,064 succeeded / 7 skipped / 0 failed), so this run reproduces the recorded
baseline rather than discovering a new state.

Raw logs (session scratch, not repo artifacts): `i1/focused-shared-capture-finalization.log`
(`dfb4eb8049a1b5ed242b560a92422041e0b904ead0e748ca9ab4389bc5291751`),
`i1/focused-runtime-working-set-ownership.log`
(`378222b02af3afb27b6d8da2cbf74752d7c45dd22e3938fbb4d1cc18c72ceb4e`),
`i1/full-native.log` (`4b1938f9c10418e20bcb6bfbb9d39aca473499f6550615f549aea2921cfc996b`).

**Not run, deliberately:** `pio run -e teensy41-capture-serial` (no firmware change), any HITL run, any stimulus
list/creation, any upload/reset. `docs/Authority/DELIVERY_RULES.md` requires the native suite before finishing;
no firmware change means the build gate has nothing to guard, and the plan forbids promoting native evidence.

## Verification discipline honoured

- No stimulus was authored, edited or listed-for-renewal: the audit drives no device and no MIDI.
- No marker, no new instrumentation, no host-tooling change. The counters and rows the clauses name already
  exist; where an endpoint is genuinely absent (G2/G3 in the map), it is **named as a gap with an owner**, not
  converted into an invented PASS.
- The managed entry point, image pinning and drain rules were not entered because no device contact occurred.
- One focused read-only pass, no broad review loop: the packet's own numbers and every cited symbol were
  re-read at `37a63011` before being written down; no test, baseline or threshold was weakened to make a row
  look complete.

## Completion record

- **Delivered:** [CLAUSE-SYMBOL-TEST-CARRIER.md](CLAUSE-SYMBOL-TEST-CARRIER.md) — a clause→symbol→test→carrier
  map for `S2b.0` (with its S3.0/S4f.0–S4f.5/S4.0 prerequisites mapped individually), `S4f.8` and `S4f.9`, plus
  five named gaps (**G1**–**G5**) and an explicit "what this map does not say" section.
- **Checkboxes written: none.** `S2b.0`, `S4f.8` and `S4f.9` remain `[ ]`. `tasks.md`, `specs/`, `design.md`,
  `ARCHITECTURE-REVIEW.md` and `IMPLEMENTATION_CONTRACT.md` are unmodified by this packet.
- **Findings, in one line each.** **G1:** `S2b.0`'s "same commit boundary" already shipped (its `S2b.1`
  reservation removal is checked DONE 2026-09-23) yet the clause has no completion record of its own, while
  `S4f.1`/`S4f.2` stay `[ ]` on members the tasks delegate to later stages — a recording debt, not a code gap.
  **G2:** `Track::acceptedStopWaitCount()` is read by no fixture, so "counts waits" is an internal guard rather
  than evidence. **G3:** no fixture asserts the trailing `,track=` attribution now appended to the
  `ODUB,stop` rows, so "names the holding owner" is unasserted at the wire level. **G4:** the S3.0 clause text
  still carries the 2026-09-23 pin values (40 / 2,736 / 6,344 / 984) while the enforced assertions read
  32 / 29,032 / 7,632 (target) / 992 — the pin is maintained in code, the clause text is stale. **G5:** the
  device-side attribution of `S4f.8` and the device-side capacity claim of `S4f.9` are not qualifiable natively.
- **Device status:** NOT RUN, and no device claim is made. Hardware/upload/reset/SD authority is per session and
  was not requested or exercised here.
- **Closeout:** runtime state/current-work entries updated with the audit's scope and result; the plan's handoff
  block replaced (not diffed). `DELIVERABLE_TRACKING.md` intentionally unchanged — an audit ships no capability.
  No `DEC-###` append (no decision taken), no overlap/ledger/playback-order guide change (none of that logic was
  touched), and no commit/push/PR.
