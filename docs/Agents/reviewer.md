# Reviewer role

Verification role before merge, push, or "done" — human or agent.

## Responsibilities

- Review the [G2 execution packet](../Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice) as well as the patch: independent oracle, production path, controlled interleavings, required case presence and actual evidence. Reject premature checkoff or assertions weakened to fit implementation.

- Verify changes respect [ARCHITECTURE_RULES.md](../Authority/ARCHITECTURE_RULES.md) ownership
- **Reject** full PREFLIGHT/DECISION_REVIEW skip only when a [formal trigger](../ARCHITECTURE_REASSESSMENT.md#formal-triggers) fired and reassessment was not done
- **Reject** if implementation violates [DECISION_LOG.md](../DECISION_LOG.md) without a superseding entry + reassessment
- Detect duplicated helpers, parallel state, display writes from business logic
- Confirm required tests were run and passed with case/count evidence; scheduled tests are outstanding gates, not completion
- Check OpenSpec `tasks.md` and [DELIVERABLE_TRACKING.md](../DELIVERABLE_TRACKING.md) updated when scope shipped
- Enforce naming per [NAMING.md](../Authority/NAMING.md)

## Bounded lifecycle review

Use one focused independent review for non-trivial lifecycle changes as part of
existing verification, not another approval stage. Review the acceptance criterion
and production path, not only whether individual functions look tidy.

- Pin the slice's base/checkpoint and changed files, including relevant untracked
  files. Do not repeatedly review the entire accumulated dirty tree. Read unchanged
  callers/dependencies where necessary to establish reachable behavior.
- Trace entry → preparation → publication → consumption → retirement. For each
  waiting/refusal outcome, identify **the owner that changes the condition and the
  production path that runs that owner**. Waiting with no producer is a defect,
  not successful fail-closed behavior. Check retry/wakeup and cancellation too.
- Exercise relevant cold/restored, empty, stale-generation/source, cancelled and
  contended states. Verify that tests do not manufacture prerequisites production
  never supplies (for example, injecting a ready root into a restored-slot test).
- Check boolean contracts: queries such as `isReady()` must not advance state.
  A command's `bool` is fine for a genuine success/failure contract; when callers
  must distinguish waiting, progress, completion and rejection, use explicit named
  outcomes/reasons rather than ambiguous `false` or extra output flags.
- Assess boolean mode arguments where they hide different operations/lifetimes;
  prefer a clear existing mode or named operation when justified. Do not split
  functions merely to eliminate booleans. Many `can...()` wrappers are not a
  simplification if callers still have to reconstruct the same state machine.
- Prefer one owner and explicit lifecycle state for mutually exclusive phases.
  Keep independent facts as booleans; do not mechanically replace them with enums.
  New flags/helpers must not duplicate state or patch over a missing transition.
- Each blocking finding needs a location, a reachable failure scenario or explicit
  authority/invariant violation, consequence, smallest in-scope repair, and a
  verification case. Do not invent findings. Style preferences and speculative
  pattern suggestions are non-blocking and normally deferred.
- Do not impose argument-count limits, ban output parameters, or demand polymorphism
  instead of enum/switch logic. Respect allocation, ownership and execution bounds.
- After concrete findings are fixed, retest the affected behavior and required
  regression gates. Do not restart broad reviews or agent debates unless the user
  requests more. Report review coverage and missing evidence honestly, including
  if an independent review could not be performed; do not claim it passed.

Follow the [critical-path rule](../Authority/WORKFLOW_LIFECYCLE.md#critical-path-and-debugging-boundary)
when a finding belongs to another task: record/defer it rather than silently expand scope.

## Must reject implementation when

| Condition | Action |
|-----------|--------|
| Equivalent abstraction already exists | REQUEST CHANGES — extend owner |
| Equivalent helper already exists | REQUEST CHANGES — reuse or justify in DECISION_LOG |
| Ownership duplicated | REQUEST CHANGES — route through owner |
| Historical decision ignored | REQUEST CHANGES — cite DEC-### or supersede |
| Migration path missing | REQUEST CHANGES — document in preflight + log |
| No historical review for non-trivial firmware | REQUEST CHANGES — **only if** formal trigger fired and review skipped |
| Parallel permanent ownership / shadow Manager | REQUEST CHANGES — [ownership transfer protocol](../Authority/ARCHITECTURE_RULES.md#ownership-transfer-protocol) or extend owner |
| Compat layer without removal trigger | REQUEST CHANGES — max lifetime required |
| OpenSpec vs architecture conflict without reassessment | REQUEST CHANGES — see [authority conflict rules](../Authority/README.md#conflict-resolution) |

## Cannot

- Approve ownership or schema changes without documented reassessment
- Approve bypass of undo/save constraints in [LOOP_MIDI_STORAGE_AND_VALIDATION.md](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md)
- Approve "challenge by implementation" of logged decisions without supersede entry

## Output format

```markdown
## Violations
- (none) or blocking findings: location, scenario/invariant, consequence, repair, verification case

## Historical decision check
- DECISION_REVIEW completed: YES / NO
- DEC-### compliance: (list IDs checked)

## Recommendations
- Non-blocking findings; deferred work stays separate from required repairs.

## Coverage
- Slice/base reviewed, production paths checked, checks performed and remaining evidence gaps.

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
| Naming | [NAMING.md](../Authority/NAMING.md) checklist — no new synonyms; verb conventions; Resolution not Pipeline for sync algorithms |

## Naming review checklist

From [NAMING.md](../Authority/NAMING.md) § Architecture review checklist:

- [ ] No unnecessary new terminology introduced
- [ ] Existing architectural vocabulary reused (concept boundaries)
- [ ] File names describe responsibilities
- [ ] Public APIs follow established verb conventions
- [ ] NAMING.md updated if a new architectural concept is approved
- [ ] Pipeline / Queue / Flow not used for synchronous deterministic algorithms
