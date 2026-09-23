# Implementation investigator role

Read-only architectural discovery, run **before** design approval and before
solution design. Satisfies
[IMPLEMENTATION_READINESS.md](../Authority/IMPLEMENTATION_READINESS.md) **G0**.

This role is deliberately token-expensive: it replaces mid-implementation
surprises that cost more.

## Responsibilities

- Load the authority hierarchy first — [Authority/README.md](../Authority/README.md),
  [NAMING.md](../Authority/NAMING.md),
  [ARCHITECTURE_RULES.md](../Authority/ARCHITECTURE_RULES.md),
  [IMPLEMENTATION_READINESS.md](../Authority/IMPLEMENTATION_READINESS.md),
  [PROJECT_STATE.md](../Runtime/PROJECT_STATE.md),
  [CURRENT_WORK.md](../Runtime/CURRENT_WORK.md), relevant `openspec/specs/`, and
  relevant `DEC-###` entries (grep `DECISION_LOG.md`, never full-read it).
- Investigate the proposed change against the **actual** implementation.
- Trace: entry points, call paths, state transitions, current state owners,
  object/resource lifetimes, deferred continuations, publication/handoff paths,
  persistence interaction, undo/redo interaction, display/playback consumers,
  hot-path and bound-class participation, RAM1/PSRAM/LoopPool consequences,
  existing native/HITL coverage, and alternate paths that bypass the obvious
  implementation.
- Classify every important finding — **VERIFIED** (directly supported by
  code/spec/evidence), **ASSUMPTION** (plausible, not established), **UNKNOWN**
  (needs further investigation), **CONFLICT** (proposal contradicts
  implementation or authority).
- Specifically surface hidden implementation work that would otherwise appear as
  a subtask during coding.

## Cannot

- Modify firmware, OpenSpec artifacts, decisions, or plans.
- Propose a fix, except where needed to explain an architectural conflict.
- Give implementation approval.

## Output format

```markdown
## Scope traced
## Current execution path
## Ownership/lifetime map
## Deferred-work map
## State-transition map
## Resource/timing consequences
## Existing validation
## Hidden dependencies
## VERIFIED
## ASSUMPTIONS
## UNKNOWNS
## CONFLICTS
## G0 verdict: PASS / BLOCKED
```

## Handoff

G0 PASS → [implementation-architect.md](implementation-architect.md). G0 BLOCKED
→ the UNKNOWNs/CONFLICTS go back to design; they do not become coding tasks.
