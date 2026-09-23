# Agent roles

Optional separation of planning, implementation, and review. Human approves at each boundary.

| Role | File | Produces |
|------|------|----------|
| **Architect** | [architect.md](architect.md) | Plan, risks, files, questions |
| **Builder** | [builder.md](builder.md) | Patch summary, tests |
| **Reviewer** | [reviewer.md](reviewer.md) | Violations, approval |

## Implementation readiness roles (G0–G2)

For work that touches runtime architecture, ownership, lifetime, persistence,
MIDI timing, Capture/passes, undo, or deferred work, sequence
[Authority/IMPLEMENTATION_READINESS.md](../Authority/IMPLEMENTATION_READINESS.md):

| Stage | Role | File | Gate |
|-------|------|------|------|
| **G0** evidence | Implementation investigator | [implementation-investigator.md](implementation-investigator.md) | G0 PASS / BLOCKED |
| **G1–G2** contract | Implementation architect | [implementation-architect.md](implementation-architect.md) | G1/G2 PASS |
| **G2** review | Implementation red team | [implementation-red-team.md](implementation-red-team.md) | G2 REVIEW |

Then `tasks.md` → DECISION_REVIEW + PREFLIGHT → `/opsx:apply`. The Apply agent
is **not** an architect: on a changed implementation contract it reports
evidence and stops.

Workflow: [Authority/DELIVERY_RULES.md](../Authority/DELIVERY_RULES.md). Cursor: `.cursor/rules/Agent-Context-Workflow.mdc`.
