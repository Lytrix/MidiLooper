# Runtime session state

Operational split — do not mix planning with execution instructions.

| File | Purpose | Agent load |
|------|---------|------------|
| [PROJECT_STATE.md](PROJECT_STATE.md) | Newest status entry; deeper status sections are historical; branch from Git, active OpenSpec from approved task | **Bounded read only** |
| [CURRENT_WORK.md](CURRENT_WORK.md) | Applicable authorized scope under `## Now implementing` | **Read scope declarations only; reconcile conflicts** |
| [ROADMAP.md](ROADMAP.md) | Future milestones and dependencies | **Optional** — never sole authority |
| [AGENT_CONTEXT_MAP.md](../AGENT_CONTEXT_MAP.md) | Domain-specific source pointers and required sections | **Relevant domain only** |

**Decision history:** [DECISION_LOG.md](../DECISION_LOG.md) (append-only)

## Loading rules

Before planning or coding:

1. Read the newest `PROJECT_STATE.md` entry at the top; verify branch from Git. Treat older status sections as historical, not current branch/OpenSpec authority.
2. Find `CURRENT_WORK.md` § Now implementing; reconcile its scope declarations with the user-approved task and that task's current OpenSpec `tasks.md`. Do not implement outside applicable authorized scope; clarify conflicts.
3. Load only relevant domain sections; search historical logs on demand.

`ROADMAP.md` is context only; never implement solely from roadmap.

Historical entries remain searchable; older entries stay in place and extracted historical scope notes are indexed under [`archive/`](archive/). Do not read them as routine context; search/open them for a specific question.

Session closeout: update PROJECT_STATE + CURRENT_WORK; append DECISION_LOG when design discussed. See [SESSION_CLOSEOUT.md](../Templates/SESSION_CLOSEOUT.md).

Cursor rule: [Agent-Context-Workflow.mdc](../../.cursor/rules/Agent-Context-Workflow.mdc)
