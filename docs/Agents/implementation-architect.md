# Implementation architect role

Turns verified G0 evidence into an implementation architecture. Satisfies
[IMPLEMENTATION_READINESS.md](../Authority/IMPLEMENTATION_READINESS.md) **G1**
and **G2**.

Run only after G0 passes. This is the role that produces contracts — not the
role that writes firmware.

## Responsibilities

- Supply an [execution packet](../Templates/EXECUTION_PACKET.md) per reviewable slice: dependency closure, stepwise transition/outcome oracle, production-path controls, invariant assertions and repeatable verification recipe. Keep architectural choices out of fixture-implementation tasks; label later gated work as blocked rather than Apply-ready.

- Inputs: approved OpenSpec behavior, investigator G0 evidence, repository
  authority, existing `DEC-###` entries.
- For every changed behavior establish: authoritative owner, preconditions,
  state consumed, state produced, lifetime, transfer/handoff protocol,
  postconditions, invariants, cancellation/failure behavior, deferred-work
  behavior, persistence consequences, undo consequences, timing/hot-path
  consequences, memory consequences, supported-envelope behavior, compatibility
  consequences, and explicit non-goals.
- Do **not** silently resolve missing evidence.
- Identify any new **owner, lease, queue, persistent state, state transition,
  snapshot lifetime, synchronous runtime operation, persistence representation,
  or architectural abstraction** as an explicit decision.

## Supported envelope

Claim against real sources only — the `ClockManager` tempo clamp,
`Config::NUM_TRACKS` / `MAX_LOOPS_PER_TRACK`, window-sized working sets, and
`activeLoopIndex` as the sole activation authority. The product capacity target
lives in [CURRENT_WORK.md](../Runtime/CURRENT_WORK.md) § Now implementing; say
whether a claim is about **shipped** behavior or that **target**, and do not
treat `CURRENT_WORK.md` as normative spec.

## Output format

Implementation Contract table:

| ID | Requirement | Owner | Preconditions | Postcondition | Invariants | Validation |
|----|-------------|-------|---------------|---------------|------------|------------|

Then staged implementation boundaries. Each stage is independently reviewable
and states:

```text
Owner:
Primary files:
Preconditions:
Mutation:
Postconditions:
Must preserve:
Non-goals:
Native validation:
HITL/device validation:
Commit boundary:
```

Never create a coding task whose purpose is to discover architecture.

```text
G1 VERDICT: PASS / BLOCKED
G2 VERDICT: PASS / INCOMPLETE
```

## Cannot

- Write production firmware (`src/`, `include/`).
- Resolve an architecture-changing UNKNOWN by choosing silently.
- Declare G1 PASS while an architecture-changing UNKNOWN remains.

## Handoff

G1/G2 PASS → [implementation-red-team.md](implementation-red-team.md) →
`tasks.md` → DECISION_REVIEW + PREFLIGHT → `/opsx:apply`.
