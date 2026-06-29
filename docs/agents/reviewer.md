# Reviewer role

Verification role before merge, push, or "done" — human or agent.

## Responsibilities

- Verify changes respect [ARCHITECTURE_RULES.md](../00-authority/ARCHITECTURE_RULES.md) ownership
- **Reject** full PREFLIGHT/DECISION_REVIEW skip only when a [formal trigger](../ARCHITECTURE_REASSESSMENT.md#formal-triggers) fired and reassessment was not done
- **Reject** if implementation violates [DECISION_LOG.md](../DECISION_LOG.md) without a superseding entry + reassessment
- Detect duplicated helpers, parallel state, display writes from business logic
- Confirm tests listed in builder output were run or scheduled
- Check OpenSpec `tasks.md` and [DELIVERABLE_TRACKING.md](../DELIVERABLE_TRACKING.md) updated when scope shipped
- Enforce naming per `.cursor/rules/Naming-Vocabulary-Teensy-Looper.mdc`

## Must reject implementation when

| Condition | Action |
|-----------|--------|
| Equivalent abstraction already exists | REQUEST CHANGES — extend owner |
| Equivalent helper already exists | REQUEST CHANGES — reuse or justify in DECISION_LOG |
| Ownership duplicated | REQUEST CHANGES — route through owner |
| Historical decision ignored | REQUEST CHANGES — cite DEC-### or supersede |
| Migration path missing | REQUEST CHANGES — document in preflight + log |
| No historical review for non-trivial firmware | REQUEST CHANGES — **only if** formal trigger fired and review skipped |
| Parallel permanent ownership / shadow Manager | REQUEST CHANGES — [ownership transfer protocol](../00-authority/ARCHITECTURE_RULES.md#ownership-transfer-protocol) or extend owner |
| Compat layer without removal trigger | REQUEST CHANGES — max lifetime required |
| OpenSpec vs architecture conflict without reassessment | REQUEST CHANGES — see [authority conflict rules](../00-authority/README.md#conflict-resolution) |

## Cannot

- Approve ownership or schema changes without documented reassessment
- Approve bypass of undo/save constraints in [LOOP_MIDI_STORAGE_AND_VALIDATION.md](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md)
- Approve "challenge by implementation" of logged decisions without supersede entry

## Output format

```markdown
## Violations
- (none) or list with file:line reference

## Historical decision check
- DECISION_REVIEW completed: YES / NO
- DEC-### compliance: (list IDs checked)

## Recommendations
- ...

## Approval
APPROVE / REQUEST CHANGES
```

## Review focus areas

| Area | Check |
|------|-------|
| Historical reuse | DECISION_LOG + extension point before new class/helper |
| Ownership | Mutations only through owner module |
| Hot paths | No full validate / SD write on record stop |
| Undo | Snapshot clone, routing order |
| Display | Read-only from domain; draw in DisplayManager |
| Tests | Native suite for logic; HITL when touching capture/edit |
| Docs | New decisions appended to DECISION_LOG at closeout |
