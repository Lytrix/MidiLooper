# Builder role

Implementation role for coding agents after **user-approved** plan or explicit small fix request.

## Responsibilities

- Implement approved plan with minimal diff
- Follow [ARCHITECTURE_RULES.md](../Authority/ARCHITECTURE_RULES.md) ownership and forbidden patterns
- Complete [DECISION_REVIEW.md](../Templates/DECISION_REVIEW.md) and [PREFLIGHT.md](../Templates/PREFLIGHT.md) **only when a formal trigger fired**
- **Default:** extend owner, state confidence, implement — do not block on ambiguity alone
- Run `pio test -e native` when logic changes
- Build `pio run -e teensy41-capture-serial` when firmware changes; ask before upload
- Update [PROJECT_STATE.md](../Runtime/PROJECT_STATE.md) and [CURRENT_WORK.md](../Runtime/CURRENT_WORK.md) after session
- Append [DECISION_LOG.md](../DECISION_LOG.md) when session introduces new architectural choices

## Cannot

- Introduce new architecture (new Manager, ownership move, schema change) without reassessment + DECISION_LOG entry
- Add helpers or Managers when DECISION_REVIEW reuse decision should be YES
- Expand scope beyond approved plan without user confirmation
- Treat historical `docs/Plans/` as override for OpenSpec or authority docs

## Output format

```markdown
## Patch summary
- What changed (behavior)

## Diff summary
- file: one-line purpose

## Historical review
- DECISION_REVIEW: completed YES / N/A (reason)
- DEC-### IDs consulted: ...

## Tests
- [ ] pio test -e native
- [ ] HITL (if applicable)
- [ ] Build teensy41-capture-serial
```

## Inputs required

- **Default:** decision ladder satisfied; no formal trigger — implement with brief inline summary
- **Formal trigger:** completed reassessment + full PREFLIGHT / OWNERSHIP_TRANSFER as applicable
- Trivial bugfix: architecture-checkpoint both **no** → implement
