# Architecture reassessment gates

**Default: continue implementation.** Reassess only when a [formal trigger](#formal-triggers) fires. See [progress bias and decision ladder](00-authority/ARCHITECTURE_RULES.md#progress-bias-and-decision-ladder).

This document does not replace the mandatory bugfix checkpoint in `.cursor/rules/architecture-checkpoint-bugfix.mdc` — run both when applicable.

---

## Formal triggers

Pause and run a **design review** when **one or more** are true:

| Trigger | Examples in this repo |
|---------|----------------------|
| **New owner introduced** | New `*Manager`, new module owning mutable domain state |
| **Ownership transfer required** | Logic moves between modules — [ownership transfer protocol](00-authority/ARCHITECTURE_RULES.md#ownership-transfer-protocol) |
| **Persistent state model changes** | New canonical model for overlay index, dirty flags, pass timeline — not a field on existing owner |
| **Storage schema changes** | Loop v4+ layout, CurrentSet version, REVPK revision pack |
| **>3 files changed AND ownership crossed** | e.g. `Track` + `StorageManager` + `DisplayManager` with responsibility shift — not mere call-site edits |
| **Undo semantics change** | New undo kind, reorder `handleUndo`, snapshot shape |
| **Timing model changes** | Tick quantization, `updateAllTracks` contract, clock pulse handling |
| **OpenSpec conflicts architecture** | Spec requires structure forbidden by ARCHITECTURE_RULES or PROJECT_INTENT — [conflict resolution](00-authority/README.md#conflict-resolution) |
| **No reusable extension point found** | After ladder steps 1–3, only path is new owner or duplicated state |

### Explicitly not triggers

Do **not** stop for these alone — implement (extend owner / reuse extension point):

| Not a trigger | Examples |
|---------------|----------|
| Adding method on existing owner | `StorageManager::markCurrentSetLoopSlotDirty` |
| Adding state to existing owner | New field on `SetBrowserOverlayPolicy` |
| Private helper on owner | Static helper in `DisplayManager.cpp` |
| UI rendering updates | Piano roll, overlay layout in `DisplayManager` |
| Display-only changes | Read getters; draw in `update()` |
| Simple persistence fields | Dirty flag, catalog row — owned by `StorageManager` |
| Bug fixes | Within existing owner boundaries |
| Tests | Native / HITL additions |
| Refactor with no ownership movement | Rename, extract private function same module |

### Do not stop for

- Architecture feels unclear (research → propose path → continue)
- Multiple valid solutions (pick one; note in PR if needed)
- Code seems old
- Helper duplication *suspected* without evidence of parallel ownership

---

## Required output when a trigger fires

Before more production code, produce:

| Field | Content |
|-------|---------|
| **Reason triggered** | Which formal trigger(s) |
| **Current architecture** | Owner, extension points, constraints |
| **Proposed evolution** | Extend vs transfer vs spec change |
| **Migration impact** | Files, tests, SD, HITL |
| **Recommendation** | Single preferred path |
| **Approval required** | YES for hard triggers; note if user already approved in CURRENT_WORK |

Also fill standard reassessment fields when useful: alternatives, risk, rollback.

---

## Session limit

**Maximum one formal reassessment per implementation session.**

If reassessment is **rejected** or user chooses “extend owner” → **return to implementation mode** immediately. Do not loop replanning.

---

## Severity guide

| Level | Triggers | Action |
|-------|----------|--------|
| **Hard stop (no code until approval)** | New owner, ownership transfer, schema change, undo routing, authority conflict, no extension point | Reassessment + approval + docs |
| **Implement with brief note** | Everything else in scope of CURRENT_WORK | Decision ladder step 5; inline preflight OK |

---

## Review checklist

_Use when a formal trigger fired — not for every task._

- [ ] Conflicts with [PROJECT_INTENT](00-authority/PROJECT_INTENT.md) litmus tests?
- [ ] Authority conflict resolved per [00-authority/README](00-authority/README.md#conflict-resolution)?
- [ ] Violates [ARCHITECTURE_RULES](00-authority/ARCHITECTURE_RULES.md) forbidden patterns?
- [ ] Ownership transfer proposal if scope moves? ([OWNERSHIP_TRANSFER](../templates/OWNERSHIP_TRANSFER.md))
- [ ] OpenSpec `tasks.md` updated when applicable?
- [ ] [DECISION_REVIEW](../templates/DECISION_REVIEW.md) + DECISION_LOG for firmware?
- [ ] Native / HITL tests identified?
- [ ] `PROJECT_STATE.md` and `CURRENT_WORK.md` updated?

---

If reassessment produced rejected alternatives, append to [DECISION_LOG.md](../DECISION_LOG.md) ([SESSION_CLOSEOUT.md](../templates/SESSION_CLOSEOUT.md)).

## Escalation

If reassessment blocks progress:

1. Document open questions in OpenSpec `proposal.md` or ask user **once**.
2. Do **not** land a workaround in production paths to "unblock."
3. If user declines redesign → extend existing owner and implement.

Related: [templates/PREFLIGHT.md](templates/PREFLIGHT.md), [agents/architect.md](agents/architect.md).
