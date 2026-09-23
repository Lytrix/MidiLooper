---
name: implementation-investigator
description: Read-only architectural discovery before solution design on the MidiLooper Teensy firmware. Traces entry points, call paths, state transitions, owners, lifetimes, deferred continuations, publication and persistence handoffs, undo interaction, hot-path bound class, and RAM1/PSRAM consequences, then classifies findings as VERIFIED, ASSUMPTION, UNKNOWN, or CONFLICT and returns a G0 verdict. Use when starting a change that touches runtime architecture, ownership, lifetime, persistence, MIDI timing, Capture/passes, undo, or deferred work, and before writing a design or generating implementation tasks.
---

# Implementation investigator (G0)

Read-only discovery that satisfies
[`docs/Authority/IMPLEMENTATION_READINESS.md`](../../../docs/Authority/IMPLEMENTATION_READINESS.md)
**G0**. Role summary: [`docs/Agents/implementation-investigator.md`](../../../docs/Agents/implementation-investigator.md).

**Do not modify firmware, OpenSpec artifacts, decisions, or plans.** This call
produces evidence and a verdict, not a patch.

## 1. Load authority first

Read, in order:

1. [`docs/Authority/README.md`](../../../docs/Authority/README.md) — hierarchy + conflict resolution
2. [`docs/Authority/NAMING.md`](../../../docs/Authority/NAMING.md)
3. [`docs/Authority/ARCHITECTURE_RULES.md`](../../../docs/Authority/ARCHITECTURE_RULES.md)
4. [`docs/Authority/IMPLEMENTATION_READINESS.md`](../../../docs/Authority/IMPLEMENTATION_READINESS.md)
5. [`docs/Runtime/PROJECT_STATE.md`](../../../docs/Runtime/PROJECT_STATE.md)
6. The authoritative scope in [`docs/Runtime/CURRENT_WORK.md`](../../../docs/Runtime/CURRENT_WORK.md) § Now implementing
7. The relevant `openspec/specs/` entry
8. Relevant `DEC-###` entries — **grep** `docs/DECISION_LOG.md`, never full-read it (>210 KB)

## 2. Trace the actual implementation

For the proposed change, trace and record:

| # | Trace |
|---|---|
| 1 | Entry points |
| 2 | Call paths |
| 3 | State transitions |
| 4 | Current state owners (against the ownership table) |
| 5 | Object/resource lifetimes |
| 6 | Deferred continuations |
| 7 | Publication/handoff paths |
| 8 | Persistence interaction |
| 9 | Undo/redo interaction |
| 10 | Display/playback consumers |
| 11 | Hot-path/timing interaction + declared bound class |
| 12 | RAM1 / PSRAM / LoopPool consequences |
| 13 | Existing native and HITL coverage |
| 14 | Alternate paths that bypass the obvious implementation |

Do not propose a fix unless it is needed to explain an architectural conflict.

## 3. Classify every important finding

| Class | Meaning |
|---|---|
| **VERIFIED** | Directly supported by code, spec, or device evidence |
| **ASSUMPTION** | Plausible but not established |
| **UNKNOWN** | Requires further investigation |
| **CONFLICT** | Proposal contradicts implementation or authority |

Look specifically for **hidden implementation work** that would otherwise appear
as a subtask during coding. That is the failure mode this gate exists to prevent:
Stage 1 becoming "Stage 1 + three discovered prerequisites + a compatibility fix
+ a new resource model".

## 4. Ground claims in real sources

- Tempo bounds: the clamp in `ClockManager::setBpmFloat` — do not assert a numeric
  envelope from memory, and check every setter path against it.
- Counts: `Config::NUM_TRACKS`, `Config::MAX_LOOPS_PER_TRACK` in `include/Globals.h`.
- Activation: `activeLoopIndex` is the **sole** authority for which slot is active.
- Working sets: **window-sized** — resident cost must not scale with loop length
  or recorded-slot count.
- The product capacity target (sounding-lease envelope) lives in `CURRENT_WORK.md`
  and is a **target**, not shipped behavior.

## 5. Output

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

Do not give implementation approval. G0 does not choose the solution.
