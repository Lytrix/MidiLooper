# Runtime session state

Operational split — do not mix planning with execution instructions.

| File | Purpose | Agent load |
|------|---------|------------|
| [PROJECT_STATE.md](PROJECT_STATE.md) | Execution context: branch, active OpenSpec, constraints, blockers, decisions | **Required first** |
| [CURRENT_WORK.md](CURRENT_WORK.md) | Exact implementation scope: now / not now / completion | **Required before planning** |
| [ROADMAP.md](ROADMAP.md) | Future milestones and dependencies | **Optional** — never sole authority for implementation |

**Decision history:** [DECISION_LOG.md](../DECISION_LOG.md) (append-only)

## Loading rules

```text
Before planning or coding:
  1. PROJECT_STATE.md
  2. CURRENT_WORK.md

ROADMAP.md — context only; never implement solely from roadmap
```

Session closeout: update PROJECT_STATE + CURRENT_WORK; append DECISION_LOG when design discussed. See [SESSION_CLOSEOUT.md](../templates/SESSION_CLOSEOUT.md).

Cursor rule: [Agent-Context-Workflow.mdc](../../.cursor/rules/Agent-Context-Workflow.mdc)
