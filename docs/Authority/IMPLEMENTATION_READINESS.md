# Implementation readiness

Gate for firmware work: prevent architecture, ownership, lifetime, timing, and
integration discovery from being deferred into implementation.

Authority: subordinate to [PROJECT_INTENT.md](PROJECT_INTENT.md), alongside
[ARCHITECTURE_RULES.md](ARCHITECTURE_RULES.md). This document governs whether
firmware work may start; it does not restate ownership or bounds.

Applies to firmware changes that affect runtime behavior, ownership, persistence,
MIDI timing, display source selection, Capture/passes, undo, or deferred work.

---

## Relationship to the existing gates

G0–G2 is a **documented precondition on the existing reassessment trigger** — it
is **not** a new stop condition and does not add a second gate to the
[decision ladder](ARCHITECTURE_RULES.md#progress-bias-and-decision-ladder).

| | Rule |
|---|---|
| Which threshold fires | A [formal trigger](../ARCHITECTURE_REASSESSMENT.md#formal-triggers) still decides whether a [reassessment](../ARCHITECTURE_REASSESSMENT.md) is required. |
| What G0–G2 adds | The **evidence and contract** for whatever the ladder already routes to implementation. |
| Session limit | Unchanged — max [one reassessment per session](../ARCHITECTURE_REASSESSMENT.md#session-limit). |
| When the ladder says **Implement** | Still implement. Fill the contract, state confidence, proceed. Do not stop because G0 surfaced an UNKNOWN that does not change an owner, lifetime, or invariant. |

The rule this enforces is narrow: **the Apply agent is not an architect.** If
`/opsx:apply` meets something that would alter the approved implementation
contract, it reports the evidence and stops — it does not solve it.

---

## G0 — Evidence

Before solution design, establish the current implementation.

Required:

- Trace every known entry path into the behavior.
- Identify current owner(s) of affected state — per the
  [ownership table](ARCHITECTURE_RULES.md#ownership-rules).
- Identify state transitions involved.
- Identify synchronous and deferred continuations.
- Identify publication/persistence handoffs.
- Identify existing tests and HITL coverage.
- Identify relevant `DEC-###` entries and normative specs.
- Identify hot-path participation and the owner's **declared bound class**
  ([ExecutionBounds.md](Architecture/ExecutionBounds.md)).
- Identify RAM1 / PSRAM / LoopPool consequences where applicable.

Every important architectural claim must be grounded in existing code,
specification, decision, or device evidence. Unknown behavior is labelled
**UNKNOWN**. G0 does not choose the solution.

---

## G1 — Architecture

Before implementation tasks are generated, resolve:

- owner before / during / after the operation
- lifetime of new or affected state
- transfer/handoff protocol
- preconditions, postconditions, invariants
- failure/cancellation behavior
- interaction with deferred work
- interaction with slot/track/session changes
- persistence and undo/redo consequences
- real-time and supported-envelope consequences
- compatibility consequences

Architecture-changing UNKNOWNs block G1.

Any new **owner, lease, queue, persistent state, runtime state transition,
snapshot lifetime, synchronous runtime operation, or persistence representation**
requires an explicit architecture decision.

### Supported envelope

State envelope claims against the real terms, and do not invent bounds:

| Term | Source of truth |
|---|---|
| Tempo range | `ClockManager::setBpmFloat` clamp; validate any new setter against it |
| Track / slot counts | `Config::NUM_TRACKS`, `Config::MAX_LOOPS_PER_TRACK` (`include/Globals.h`) |
| Working-set scaling | [ARCHITECTURE_RULES.md](ARCHITECTURE_RULES.md#runtime-working-sets-are-role-owned-and-memory-scales-with-active-roles) — resident cost is **window-sized**, must not scale with loop length or recorded-slot count |
| Which slot is active | `activeLoopIndex` is the **sole** authority; a second authority is a defect |
| Product capacity target | [CURRENT_WORK.md](../Runtime/CURRENT_WORK.md) § Now implementing — current sounding-lease target and explicitly deferred capabilities; do not cache a numeric target here |

The target envelope is a **product target**, not current code: `NUM_TRACKS` is 8
today and the 16-track part is an open item. Envelope work must say which it is
claiming. Do not encode a target from a roadmap as though it were shipped
behavior, and do not treat `CURRENT_WORK.md` as normative spec.

---

## G2 — Implementation Contract

Each implementation stage must specify:

```text
Owner:
Primary files:
Preconditions:
Mutation:
Postconditions:
Must preserve:
Explicit non-goals:
Validation:
Commit boundary:
```

Tasks must be implementation work, not architecture discovery. These are
**forbidden** as implementation tasks:

- investigate how…
- determine whether…
- decide which…
- choose an ownership model…
- figure out lifetime…
- explore whether…

Such work belongs in G0/G1.

---

## G2 execution packet — required for each implementation slice

A slice is one independently reviewable behavior change with a reachable verification gate.
Use [EXECUTION_PACKET.md](../Templates/EXECUTION_PACKET.md), or the equivalent section in an existing
OpenSpec design/review artifact. Cite the approved contract; do not duplicate it. This strengthens G2/G3,
not a new reassessment or permission gate. It applies to future steps of existing changes as well as new work.

Before changing production behavior, pin:

1. **Scope and dependency closure:** task IDs, owner/API, primary files, prerequisites already present,
   prerequisites included in the same slice, explicit non-goals and the commit boundary. No cyclic gate,
   absent dependency assumed implemented, or unrelated later stage pulled in silently.
2. **Transition oracle:** initial state, input sequence, observation after each action/service grain,
   and final state. Include interval/slot/generation identity and resource ownership when applicable.
   State exactly which request is consumed, retained, superseded or faulted and by which owner.
3. **Outcomes at the boundary:** map existing return values/state/diagnostics to accepted, waiting,
   completed, superseded, stale and fault outcomes as applicable. Do not add a universal enum or framework.
   `false`, `void`, or an unconditional “ok” must not conceal whether a request survives.
4. **Controllable interleavings:** for deferred/resource work, native fixtures must control time/ticks,
   owner stepping and admission refusal/recovery. Prefer existing seams; add test-only adapters only at
   clock, allocator, I/O or observation boundaries. Exercise the production entry/worker/commit path,
   not a copied state machine. On-path domain stubs that return success cannot establish acceptance.
5. **Independent observable invariants:** content/identity/refcounts, no forbidden allocation, exactly-once
   commit, unchanged published generation until commit, as applicable. Compare with explicit expected
   content/trace or an independently derived oracle, not the implementation's own output as its expectation.
6. **Verification recipe and evidence:** exact commands, test/scenario IDs, baseline revision plus dirty-tree
   context, expected suite/case presence, pass/fail criteria, hardware-only claims and evidence destination.
   Numeric budgets come from the approved contract or a prior measurement step; an unset value is not a pass.

### Proportionality

For a local deterministic logic fix, a short packet and an existing focused regression can satisfy this.
For ownership, deferred work, persistence or request scheduling, transition and fault/interleaving fixtures
are required. Do not write implementation-mirroring tests or require new tests for documentation-only,
mechanical or reversible low-impact edits: state why existing checks are sufficient. Measurement slices
record reproducible inputs/metrics and distinguish observations from guarantees; they do not promise a
bit-for-bit hardware timing trace. Native control of logical ordering does not prove MIDI timing on device.

### Fixture-first sequence

Freeze the expected behavior before the production mutation. First reuse/extend the harness and write the
focused acceptance cases. A minimal compile/link seam or API skeleton may land in the same slice when needed;
it is not permission to implement the behavior before fixing its oracle. Demonstrate the relevant pre-fix
behavioral failure when runnable; a missing symbol/build error is not red behavioral evidence. If a new API
prevents a pre-fix run, record that limitation and the discriminating assertion, then verify the production
path once linked. Existing passing coverage needs no artificial red test.

Use fixed input traces and explicit service stepping instead of sleeps or probabilistic stress. A bounded
drain fails with the last owner/phase state when it exhausts its grain limit. Seeds must be recorded if fuzz
coverage is added. A known failing characterization is reported by test ID and intended assertion; it never
makes the whole suite green or excuses new failures. Remove its exception once the implementing slice lands.

### Evidence-backed completion

Record evidence in the change's review/packet artifact. Distinguish **specified**, **fixture implemented**,
**native verified**, **device verified**, and **blocked**; none implies the next. Check a task only when its
own listed acceptance passes. A parent stage stays open while any required device gate is outstanding.
Capture exit status, suite/case counts, skipped/errored cases, build RAM1/bank evidence where relevant, and
HITL stimulus/evidence IDs. Missing/skipped mandatory coverage, compilation errors, “0 failures” with missing
suites, logs without asserted outcomes, and a planned command are not passes. Reuse unchanged evidence only
when its code/contract scope still matches; explain applicability after subsequent edits.

The agent may fill packet details that follow from the approved contract (test names, fixtures, local seams,
commands) and continue. An owner/lifetime/transition/budget change returns through the existing G3 rules.
Never weaken an assertion, suppress an exception, change a baseline/threshold or declare a case out of scope
merely to pass. Supply the failing trace and the conflicting contract instead. Review checks the oracle as
well as the patch. No fresh user confirmation is required for routine steps already authorized.

---

## G3 — Apply

Firmware implementation may begin only when G0–G2 are satisfied. During
implementation, **STOP and return to design** when evidence requires:

- ownership change
- lifetime-model change
- new persistent state
- new lease/resource
- new queue/deferred mechanism
- new runtime state transition
- changed persistence format
- synchronous work added to a timing-sensitive path
- supported-envelope change
- violation of an existing `DEC-###`
- materially different call path from the approved design

Apply each slice through its [G2 execution packet](#g2-execution-packet--required-for-each-implementation-slice):
fix the oracle → establish controlled fixture → implement → focused checks → required broad/device checks →
record evidence → check off. Continue authorized slices in dependency order; do not repeatedly request approval.
Complete reachable checks and clearly leave hardware/dependency gates open when unavailable.

Local implementation discoveries that do **not** require returning to design:

- helper naming
- local refactoring inside approved ownership
- fixture mechanics
- diagnostic placement
- equivalent implementation detail with an unchanged contract

The distinction is the point: stopping because a helper is needed is noise.
Stopping because a supposedly single finalisation lease turns out to need a
second lifetime owner is the gate working.

---

## Related

- [ARCHITECTURE_RULES.md](ARCHITECTURE_RULES.md) — ownership, bounds, decision ladder
- [README.md](README.md#conflict-resolution) — doc-vs-doc and doc-vs-code resolution
- [DELIVERY_RULES.md](DELIVERY_RULES.md) — OpenSpec process and verification matrix
- [Templates/PREFLIGHT.md](../Templates/PREFLIGHT.md), [Templates/DECISION_REVIEW.md](../Templates/DECISION_REVIEW.md)
- Agent roles: [Agents/implementation-investigator.md](../Agents/implementation-investigator.md), [Agents/implementation-architect.md](../Agents/implementation-architect.md), [Agents/implementation-red-team.md](../Agents/implementation-red-team.md)
