---
name: implementation-red-team
description: Adversarial review that tries to falsify a proposed MidiLooper implementation contract before it becomes code. Attacks alternate call paths, deferred/asynchronous completion, stale snapshot or root references, ownership transfer, overlapping operations, slot/track switches mid-completion, transport stop, clock-source transitions, undo/redo, load/save, persistence in flight, long loops, maximum active working set, RAM1 pressure, accidental synchronous work, and MIDI starvation, reporting each as BLOCKER, TIGHTENING, or COVERED. Use after a design or implementation contract is drafted and before generating tasks or applying a change, especially for changes to runtime architecture, ownership, lifetime, persistence, or deferred work.
---

# Implementation red team (G2)

Adversarial architecture review that satisfies
[`docs/Authority/IMPLEMENTATION_READINESS.md`](../../../docs/Authority/IMPLEMENTATION_READINESS.md)
**G2**. Role summary: [`docs/Agents/implementation-red-team.md`](../../../docs/Agents/implementation-red-team.md).

**Do not implement the design.** Do not improve unrelated code. Do not rewrite
the plan merely because you prefer another design.

## 1. Assume the contract is wrong

Assume the proposed implementation contract contains a hidden regression, and
try to falsify it against the repository. Read the contract and the G0/G1
evidence, then attack.

## 2. Attack surface

Work down this list explicitly; it is the shape of this firmware's real
regressions:

- alternate call paths that bypass the new one
- asynchronous / deferred completion
- stale parent / root / snapshot references
- ownership transfer, and leases released while a live owner remains
- overlapping logical operations
- slot switch during deferred completion
- track switch during deferred completion
- record → stop → new record
- overdub → stop → new overdub
- transport stop
- external / internal clock transitions
- undo / redo
- clear / load / save
- persistence still in flight
- long loops
- the maximum supported active working set
- RAM1 pressure
- PSRAM / resource lifetime
- synchronous work accidentally introduced on a timing-sensitive path
- MIDI timing / starvation
- display and playback seeing different generations
- compatibility / fallback paths bypassing the new path

Also check the two standing contracts: a full validate on record/overdub stop,
and any new second authority for which slot is active (`activeLoopIndex` owns it).

## 3. Report every issue

```markdown
SEVERITY: BLOCKER / TIGHTENING / COVERED
EVIDENCE:
FAILED ASSUMPTION:
REQUIRED CHANGE:
VALIDATION THAT WOULD CATCH IT:
```

| Severity | Requires |
|---|---|
| **BLOCKER** | A concrete reachable path or a violated invariant |
| **TIGHTENING** | A real gap without a demonstrated failure |
| **COVERED** | Attacked, and the contract holds — say why |

**Do not classify hypothetical possibilities as blockers.** If you cannot name
the path or the invariant, it is TIGHTENING at most. This rule is what keeps the
review useful instead of an endless list of theoretical problems.

## 4. Verdict

```text
G2 REVIEW:
PASS
PASS WITH TIGHTENING
BLOCKED
```

List the exact changes required before Apply. BLOCKED findings return to the
architect role as design input — they do not become coding tasks.
