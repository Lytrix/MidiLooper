# Documentation Closeout

**Purpose:** Ensure every implemented logic or functionality change leaves the repository's durable documentation consistent with the resulting system.

**Related:** [WORKFLOW_LIFECYCLE.md](WORKFLOW_LIFECYCLE.md), [DELIVERY_RULES.md](DELIVERY_RULES.md), [ARCHITECTURE_RULES.md](ARCHITECTURE_RULES.md), [GITHUB_WORK_TRACKING.md](GITHUB_WORK_TRACKING.md).

**Authority:** Subordinate to [ARCHITECTURE_RULES.md](ARCHITECTURE_RULES.md) and [DELIVERY_RULES.md](DELIVERY_RULES.md).

---

## 1. Core invariant

> **No behavioral change is complete while durable documentation still describes the old behavior.**

A separate implementation plan is **not required for every task**.

Documentation closeout is mandatory; the specific documentation artifact depends on the change.

---

## 2. Determine documentation impact

Before closing an implementation task, ask:

### A. Did logic change?

Examples:

* state transitions;
* resolver behavior;
* ownership;
* calculations;
* mutation rules;
* lifecycle behavior.

If yes, perform documentation closeout.

### B. Did observable functionality change?

Examples:

* user-visible behavior;
* MIDI behavior;
* editing semantics;
* playback;
* persistence;
* display behavior.

If yes, perform documentation closeout.

### C. Did an architecture or ownership contract change?

If yes:

* follow [ARCHITECTURE_RULES.md](ARCHITECTURE_RULES.md);
* update the relevant authority;
* record `DEC-###` in [DECISION_LOG.md](../DECISION_LOG.md);
* complete reassessment/ownership transfer requirements where applicable ([ARCHITECTURE_REASSESSMENT.md](../ARCHITECTURE_REASSESSMENT.md), [OWNERSHIP_TRANSFER.md](../Templates/OWNERSHIP_TRANSFER.md)).

### D. Did normative requirements change?

If yes:

* update the applicable OpenSpec change;
* use `/opsx:sync` if implementation/specification drifted;
* archive so accepted requirements land in `openspec/specs/`.

See [DELIVERY_RULES.md](DELIVERY_RULES.md) and `.cursor/rules/OpenSpec-Workflow.mdc`.

---

## 3. Choose the owning documentation

Use the artifact that owns the resulting truth.

| Change                                                    | Primary record                                                                     |
| --------------------------------------------------------- | ---------------------------------------------------------------------------------- |
| Accepted normative requirement                            | `openspec/specs/`                                                                  |
| Architecture / ownership                                  | [`docs/Authority/`](README.md) + [DECISION_LOG.md](../DECISION_LOG.md)             |
| Current behavior / how-to                                 | [`docs/Guides/`](../Guides/)                                                       |
| Overdub overlap-resolve evaluations                       | [`OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md`](../Guides/OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md) — update in the same change as resolver / pending / seal / source-view covering identities |
| Overdub ledger evaluations                                | [`OVERDUB_LEDGER_NOTE_EVALUATIONS.md`](../Guides/OVERDUB_LEDGER_NOTE_EVALUATIONS.md) — update in the same change as ledger / occupy catch-up / playback-order |
| Implementation transition                                 | OpenSpec design/tasks or [`docs/Plans/`](../Plans/)                                |
| Small behavioral change without a dedicated authority doc | Appropriate existing guide/authority document, or a concise retained change record |
| Explicit future decision                                  | [DECISION_LOG.md](../DECISION_LOG.md), plan, or GitHub PARKED item ([GITHUB_WORK_TRACKING.md](GITHUB_WORK_TRACKING.md)) |

Do not create a new document if an existing authoritative document already owns the concept.

Placement summary also lives in [DELIVERY_RULES.md](DELIVERY_RULES.md) § Documentation placement.

---

## 4. Update behavior, not merely intent

A common failure mode is leaving a plan describing what was intended while the living documentation continues to describe the previous implementation.

After implementation, ask:

> "If a new agent read only the current authority and guides, would it understand the behavior I just shipped?"

If not, documentation is incomplete.

Plans may remain as historical transition records. They do not become authoritative merely because they describe the implementation.

See authority hierarchy: [Authority README](README.md).

---

## 5. OpenSpec changes

For OpenSpec work:

1. Implement the task.
2. Update `tasks.md`.
3. Verify behavior.
4. Determine whether implementation caused specification drift.
5. Run `/opsx:sync` when needed.
6. Ensure accepted normative requirements are represented correctly.
7. Run `/opsx:archive` when all gates pass.

The existing delivery rules define this lifecycle ([DELIVERY_RULES.md](DELIVERY_RULES.md)).

Do not leave normative requirements only in `docs/Plans/`.

---

## 6. Architecture changes

If implementation changed ownership, lifecycle responsibility, or another architectural contract:

1. Confirm the architecture trigger was handled.
2. Confirm reassessment was completed where required.
3. Confirm migration/compatibility/removal strategy exists.
4. Confirm human approval where required.
5. Append the `DEC-###` decision ([DECISION_LOG.md](../DECISION_LOG.md)).
6. Update the authority documentation under [`docs/Authority/`](README.md).
7. Ensure stale ownership descriptions are removed or explicitly superseded.

Ownership changes must not be silently encoded in code. [ARCHITECTURE_RULES.md](ARCHITECTURE_RULES.md) explicitly requires the ownership-transfer protocol and decision record.

---

## 7. Small tasks

A small task may have no separate plan.

Example:

```text
GitHub:
TASK — Rename playback "walk" to "cursor"

Implementation:
- rename methods
- update callers
- tests

Documentation:
- update naming guide because the vocabulary is architectural
```

([NAMING.md](NAMING.md) owns architectural vocabulary.)

The task is fully documented even though no standalone implementation plan was necessary.

Another example:

```text
GitHub:
BUG — Note edit fails to re-enter overlap resolution

Implementation:
- fix driver state reconstruction
- add regression test

Documentation:
- update the relevant note-edit behavior/authority guide
- retain the GitHub issue as the bug record
```

The rule is **documentation completeness**, not plan creation.

### Multi-RC bugfixes

* Update the bugfix plan / freeze state **per RC**.
* Update authority/guides when that RC changes the behavior or contract they describe.
* Otherwise complete durable authority/guide closeout at parent Bug close ([GITHUB_WORK_TRACKING.md](GITHUB_WORK_TRACKING.md) § Closing).

---

## 8. Future refinements

If an investigation concludes that a refinement should happen later:

```text
Decision:
Separate visibility from geometry.

Status:
Deferred future refinement.

Reason:
Current behavioral migration is complete and this refinement
is orthogonal to the remaining scope.
```

Preserve that decision ([DECISION_LOG.md](../DECISION_LOG.md) and/or GitHub PARKED per [GITHUB_WORK_TRACKING.md](GITHUB_WORK_TRACKING.md)).

When the refinement becomes committed work:

```text
future decision
    ↓
GitHub Task / Feature
    ↓
plan or OpenSpec as appropriate
    ↓
CURRENT_WORK
```

Do not silently turn deferred decisions into current work.

---

## 9. Documentation closeout checklist

Before closing a logic/functionality task:

```text
[ ] Implementation verified
[ ] Tests / HITL complete as required
[ ] Existing behavior documentation inspected
[ ] Changed behavior documented
[ ] Normative OpenSpec updated if applicable
[ ] Architecture docs updated if applicable
[ ] DEC-### appended if required
[ ] OpenSpec tasks checked off if applicable
[ ] OpenSpec synced/archived if applicable
[ ] PROJECT_STATE updated
[ ] CURRENT_WORK updated
[ ] DELIVERABLE_TRACKING updated if shipped/scope changed
[ ] Overdub overlap-resolve / ledger evaluation catalogs updated if those owners changed
[ ] GitHub issue/sub-issues ready to close
```

Not every checkbox applies to every task.

The agent should explicitly identify which documentation was updated and which categories were not applicable.

Also see [SESSION_CLOSEOUT.md](../Templates/SESSION_CLOSEOUT.md) for design-heavy chats and [DELIVERY_RULES.md](DELIVERY_RULES.md) § Agent session hygiene.

---

## 10. Closeout report

A completed task should be able to summarize documentation closeout concisely:

```text
Documentation closeout:
- Behavior: updated `docs/Guides/...`
- OpenSpec: not applicable
- Architecture: unchanged
- DECISION_LOG: unchanged
- CURRENT_WORK: updated
- DELIVERABLE_TRACKING: unchanged
```

For a larger change:

```text
Documentation closeout:
- OpenSpec: synced and archived
- Authority: updated `docs/Authority/...`
- Decision: DEC-042 appended
- Guide: updated `docs/Guides/...`
- CURRENT_WORK: updated
- DELIVERABLE_TRACKING: updated
```

This makes the documentation decision auditable without requiring a large report.

---

## 11. Anti-patterns

Do not:

* require a full plan merely to satisfy documentation hygiene;
* close a behavioral change while guides still describe old behavior;
* copy normative requirements into multiple documents;
* leave architecture decisions only in chat;
* treat a plan as current authority after implementation;
* create a new document when an existing authoritative document owns the concept;
* delete historical plans merely because the resulting behavior is now documented elsewhere.

The goal is **one durable source of truth per concept**, not maximum documentation volume.
