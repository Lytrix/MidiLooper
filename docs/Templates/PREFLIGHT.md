# Preflight template

**Two modes:**

| Mode | When | Required |
|------|------|----------|
| **Lightweight** | No [formal trigger](../ARCHITECTURE_REASSESSMENT.md#formal-triggers) — extend owner / reuse extension point | Short inline summary in chat (owner, change, tests) — **then implement** |
| **Full** | Formal trigger fired | Complete this template before more production code |

Default is lightweight. Full preflight is **not** required for bug fixes, display-only work, methods on existing owner, tests, or refactors without ownership movement.

---

## Problem

_What user-visible or test-visible behavior changes? One paragraph._

## Domain

_From [AGENT_CONTEXT_MAP.md](../AGENT_CONTEXT_MAP.md): e.g. Persistence, Note edit, Display._

## Similar historical decisions

_Required for **full** preflight; for lightweight mode — quick `rg` on DECISION_LOG only._

Complete [DECISION_REVIEW.md](DECISION_REVIEW.md) or fill inline:

### Search locations

- [ ] [`docs/DECISION_LOG.md`](../DECISION_LOG.md)
- [ ] `docs/Plans/` (handoffs / enhancements for this domain)
- [ ] Active OpenSpec — `openspec/changes/<name>/` per [PROJECT_STATE.md](../Runtime/PROJECT_STATE.md)

### Relevant findings

_DEC-### IDs, spec paths, plan links — or "none"._

—

### Existing owner

_Which class/module owns mutation for this scope?_

### Existing extension point

_Method, Actions, EditState, policy, spec — or "none (needs review)"._

### Reuse possible

**YES** / **NO**

### If no — why not

_Explicit reason; cite ignored decision ID if applicable._

### Architecture review required

**YES** / **NO**

---

## Loaded docs

- [ ] `docs/Runtime/PROJECT_STATE.md`
- [ ] `docs/Runtime/CURRENT_WORK.md` (confirm task is in § Now implementing)
- [ ] `docs/DECISION_LOG.md` (relevant entries)
- [ ] `docs/Authority/PROJECT_INTENT.md`
- [ ] `docs/Authority/ARCHITECTURE_RULES.md`
- [ ] `docs/Authority/DELIVERY_RULES.md` (if OpenSpec / timeline / persistence)
- [ ] `openspec/changes/<name>/tasks.md` (if active change)
- [ ] _Domain required docs (list paths):_

## Extension point (implementation)

_Same as above if already filled; otherwise complete._

## Ownership change (if applicable)

Required when mutable scope moves between modules — [OWNERSHIP_TRANSFER.md](OWNERSHIP_TRANSFER.md).

| Field | Answer |
|-------|--------|
| Current owner | |
| Target owner | |
| Transfer needed | YES / NO |
| If YES — compat removal trigger | |

## Files affected

_List expected paths (estimate). >3 files → check [ARCHITECTURE_REASSESSMENT.md](../ARCHITECTURE_REASSESSMENT.md)._

## New abstractions

| Name | Kind (class/function/module) | Justification |
|------|------------------------------|---------------|
| _none_ | | |

_If any row is non-none: confirm DECISION_REVIEW reuse decision is NO with rationale._

## Persistence impact

_None / Current workspace dirty flags / new SD field / revision catalog — describe._

## Undo impact

_None / capture-pass / NoteEditPassClosed / session undo — describe._

## Migration required

_No / firmware-only / SD format bump / host tests only._

## Architecture review required (summary)

**YES** / **NO**

_If YES, link to reassessment output (limitation, alternatives, recommendation)._

## Authority conflict check (mandatory)

Per [Authority/README.md](../Authority/README.md#conflict-resolution):

| Source | Conflicts with architecture or intent? |
|--------|--------------------------------------|
| OpenSpec / tasks | YES / NO |
| Proposed new class/helper | YES / NO |

**If any YES:** **STOP** — no implementation until reassessment approved and docs updated.

---

## Context summary (required)

_After loading docs, 5–10 bullets: owner, constraints, active OpenSpec, decision-log IDs, tests to run._
