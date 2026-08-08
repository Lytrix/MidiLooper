# Work Lifecycle

**Purpose:** Define how ideas, observations, investigations, decisions, GitHub work items, plans, OpenSpec changes, implementation, verification, and documentation relate.

**Scope:** Coordination and lifecycle only. This document does not override architecture authority, OpenSpec rules, or delivery verification rules.

**Authority:** Subordinate to [ARCHITECTURE_RULES.md](ARCHITECTURE_RULES.md) and [DELIVERY_RULES.md](DELIVERY_RULES.md).

---

## Related documents

| Document | Role |
|----------|------|
| [ARCHITECTURE_RULES.md](ARCHITECTURE_RULES.md) | Ownership, decision ladder, ownership transfer |
| [DELIVERY_RULES.md](DELIVERY_RULES.md) | OpenSpec delivery, verification gates, session hygiene |
| [../ARCHITECTURE_REASSESSMENT.md](../ARCHITECTURE_REASSESSMENT.md) | Formal reassessment triggers |
| [../DECISION_LOG.md](../DECISION_LOG.md) | Accepted / superseded decisions |
| [../Runtime/PROJECT_STATE.md](../Runtime/PROJECT_STATE.md) | Execution context — load first |
| [../Runtime/CURRENT_WORK.md](../Runtime/CURRENT_WORK.md) | Immediate execution scope (sole implementation queue) |
| [../DELIVERABLE_TRACKING.md](../DELIVERABLE_TRACKING.md) | Deliverable-level shipped vs next |
| [../Templates/PREFLIGHT.md](../Templates/PREFLIGHT.md) | Planning template before implementation |
| [../Templates/DECISION_REVIEW.md](../Templates/DECISION_REVIEW.md) | Historical decision review before firmware |
| [../Templates/OWNERSHIP_TRANSFER.md](../Templates/OWNERSHIP_TRANSFER.md) | Ownership move proposal |
| [../Templates/SESSION_CLOSEOUT.md](../Templates/SESSION_CLOSEOUT.md) | Design-session closeout checklist |
| [../Plans/openspec_integration_overview.md](../Plans/openspec_integration_overview.md) | OpenSpec navigation |
| [../Guides/HITL_TEST_SCENARIOS.md](../Guides/HITL_TEST_SCENARIOS.md) | HITL presets / verifiers (when HITL is a gate) |
| `.cursor/rules/OpenSpec-Workflow.mdc` | OpenSpec Cursor workflow |
| `.cursor/rules/Multi-Stage-Bugfix-Workflow.mdc` | Multi-RC bugfix discipline (RC ≠ GitHub structure) |
| [GITHUB_WORK_TRACKING.md](GITHUB_WORK_TRACKING.md) | Issue types, Project NOW/NEXT/PARKED, branches/PRs → `dev`, Bug-after-understanding, RC vs sub-issue rules |
| [../BRANCHING.md](../BRANCHING.md) | `dev` integration; short-lived `feature/` / `refactor/` branches |
| [DOCUMENTATION_CLOSEOUT.md](DOCUMENTATION_CLOSEOUT.md) | Which durable artifact owns resulting truth after a behavioral change |
| `.cursor/rules/HITL-Test-Flow.mdc` / `HITL-Edit-Test-Flow.mdc` | HITL command contracts |

---

## 1. Core principle

Work does not begin with an issue.

Work begins with **discovery**.

A discovery may be an:

* idea;
* observed problem;
* bug symptom;
* optimization opportunity;
* architectural concern;
* behavioral inconsistency;
* user-facing feature need;
* investigation result.

Discovery is exploratory. It is not automatically a committed work item.

The progression is:

```text
Discovery
    ↓
Investigation
    ↓
Understanding
    ↓
Decision
    ↓
Work item
    ↓
Plan / OpenSpec when required
    ↓
Current execution
    ↓
Implementation
    ↓
Verification
    ↓
Documentation closeout
    ↓
Ship / close
```

The important transition is **Decision**.

The system should not create structure merely because something was noticed. First understand what the observation means and decide what should happen.

---

## 2. Discovery

Discovery captures something worth considering.

Examples:

* "This overlap behavior looks inconsistent."
* "This code duplicates state."
* "This operation may be causing the timing regression."
* "Separating these two concepts would simplify the model."
* "Users may need this capability."
* "This could reduce memory pressure."

At this stage:

* investigate freely;
* gather evidence;
* inspect existing architecture and specifications;
* do not prematurely create implementation structure;
* do not treat a hypothesis as a confirmed bug or architecture decision.

Discovery may happen in conversation, investigation notes, captures, tests, or existing documentation.

---

## 3. Investigation

Investigation determines what is actually happening.

Typical activities:

* reproduce the behavior;
* inspect the relevant owner;
* trace state transitions;
* inspect existing OpenSpec requirements;
* inspect [DECISION_LOG.md](../DECISION_LOG.md);
* inspect current guides and authority documents;
* compare competing explanations;
* run targeted tests or HITL scenarios;
* identify the first divergence between expected and actual behavior.

Investigation should reduce uncertainty.

Do not redesign merely because the code is unfamiliar or because multiple solutions are possible. Follow the architecture decision ladder in [ARCHITECTURE_RULES.md](ARCHITECTURE_RULES.md).

---

## 4. Understanding

Investigation should produce an explicit understanding of the problem or opportunity.

Examples:

```text
Observed:
A moved note does not re-enter overlap resolution after deselection.

Understanding:
The current edit-driver state does not correctly reconstruct
the state required to invoke the overlap resolver.
```

or:

```text
Observed:
Presence, visibility, and shortening are represented through
overlapping concepts.

Understanding:
These are orthogonal dimensions and the current representation
couples them unnecessarily.
```

Understanding is not yet implementation approval.

---

## 5. Decision

Every meaningful discovery should reach a decision.

The outcome is not simply "action" or "no action".

Typical decisions are:

| Decision                | Meaning                                                               |
| ----------------------- | --------------------------------------------------------------------- |
| **Fix**                 | Existing behavior is incorrect and should be corrected                |
| **Feature**             | New user-facing capability should be implemented                      |
| **Refinement**          | Existing behavior/model should be improved                            |
| **Optimization**        | Existing behavior should remain correct while implementation improves |
| **Architecture change** | Ownership/model/contract needs reassessment                           |
| **Defer**               | Valid work, intentionally postponed                                   |
| **Reject**              | Investigated and deliberately not pursued                             |

A deferred optimization or refinement is still a decision and should not disappear merely because it is not current work.

---

## 6. Architecture gate

Before deciding how to implement work, apply [ARCHITECTURE_RULES.md](ARCHITECTURE_RULES.md).

The architecture decision ladder is:

1. Existing owner → extend.
2. Existing extension point → reuse.
3. Historical decision applies → follow it.
4. Formal reassessment trigger → reassess ([ARCHITECTURE_REASSESSMENT.md](../ARCHITECTURE_REASSESSMENT.md)).
5. Otherwise → implement.

Lightweight architecture checks inform the decision; they are not themselves reasons to stop implementation.

If an ownership transfer is required, follow [OWNERSHIP_TRANSFER.md](../Templates/OWNERSHIP_TRANSFER.md) and the ownership-transfer protocol in [ARCHITECTURE_RULES.md](ARCHITECTURE_RULES.md).

An ownership transfer requires the architecture reassessment, migration strategy, compatibility/removal strategy, approval, and `DEC-###` recording required by [ARCHITECTURE_RULES.md](ARCHITECTURE_RULES.md).

---

## 7. Work item

Once the decision produces committed or intentionally planned work, create or associate a GitHub Issue.

The GitHub issue is the **work identity**, not the architecture authority and not necessarily the implementation plan.

Use:

* **Feature** for a substantial capability or initiative;
* **Bug** for confirmed incorrect behavior;
* **Task** for implementation, refactoring, migration, optimization, or other engineering work.

Do not create a GitHub issue merely because an observation exists.

Do not force exploratory hypotheses into Bug issues.

For multi-RC bug hunts: create a Bug only after **confirmed understanding** (failure + owner + invariant + why wrong). RCs stay plan/commit-level unless independently actionable — see `.cursor/rules/Multi-Stage-Bugfix-Workflow.mdc` and [GITHUB_WORK_TRACKING.md](GITHUB_WORK_TRACKING.md).

---

## 8. Determine the required planning artifact

Not every task requires the same planning mechanism.

### Small implementation

A small, well-understood change may use:

```text
GitHub Issue
    ↓
CURRENT_WORK
    ↓
implementation
```

No separate plan is required.

### Small behavioral change

Use:

```text
GitHub Issue
    ↓
short implementation note / issue description
    ↓
CURRENT_WORK
    ↓
implementation
```

The resulting behavior must still be documented during documentation closeout.

### Substantial feature or refinement

Use an appropriate detailed plan under [`docs/Plans/`](../Plans/) and/or OpenSpec.

### Normative timeline or data-model change

Use OpenSpec according to [DELIVERY_RULES.md](DELIVERY_RULES.md).

### Architecture / ownership change

Use the architecture reassessment and ownership-transfer process, with OpenSpec or a detailed migration plan as required.

The question is not:

> "Does every issue need a plan?"

The question is:

> "What planning and specification artifact is appropriate for this change?"

Before firmware on non-trivial work, use [PREFLIGHT.md](../Templates/PREFLIGHT.md) and [DECISION_REVIEW.md](../Templates/DECISION_REVIEW.md) when [DELIVERY_RULES.md](DELIVERY_RULES.md) / Agent-Context-Workflow require them.

---

## 9. OpenSpec integration

OpenSpec is used when the change needs an explicit normative, testable contract.

For **bugs**: OpenSpec is required when investigation becomes a substantial migration/refinement toward a defined normative end-state — not merely because observable behavior or a sensitive path changed. Local corrections remain plan/commit-level under the GitHub Bug.

Existing OpenSpec workflow remains authoritative (see [DELIVERY_RULES.md](DELIVERY_RULES.md) and `.cursor/rules/OpenSpec-Workflow.mdc`):

```text
/opsx:explore
    ↓
/opsx:propose
    ↓
design / specs / tasks
    ↓
/opsx:apply
    ↓
verification
    ↓
/opsx:sync
    ↓
/opsx:archive
```

Use `/opsx:explore` when the problem is not yet sufficiently understood.

Use `/opsx:propose` once the change is sufficiently understood and decided.

Use `/opsx:apply` for implementation.

Use `/opsx:sync` when implementation and specification have drifted.

Use `/opsx:archive` after verification gates pass.

Normative SHALL/MUST requirements belong in `openspec/specs/` after archive, not only in a plan.

Do not duplicate existing brownfield authority into OpenSpec; link to it and state only the relevant delta (e.g. [LOOP_MIDI_STORAGE_AND_VALIDATION.md](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md)).

---

## 10. Current execution

[`CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md) defines the immediate execution scope.

It answers:

> What should be worked on now?

It does not replace:

* GitHub's work inventory;
* OpenSpec requirements;
* architecture decisions;
* implementation plans;
* deliverable history.

Before implementation, check:

* [`PROJECT_STATE.md`](../Runtime/PROJECT_STATE.md);
* [`CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md);
* relevant GitHub Issue;
* applicable OpenSpec change/spec;
* relevant architecture and behavior documentation.

The existing delivery rules already require `PROJECT_STATE.md` and `CURRENT_WORK.md` to establish active scope for OpenSpec work.

GitHub Project **NOW** may feed `CURRENT_WORK`; GitHub must not become a second detailed execution queue (see [GITHUB_WORK_TRACKING.md](GITHUB_WORK_TRACKING.md)).

---

## 11. Implementation

Implement the decided work within the established architecture.

Do not use implementation as a substitute for an unresolved architecture decision.

If implementation reveals that the decision or specification is wrong:

```text
implementation discovery
        ↓
reassess understanding
        ↓
update decision/specification
        ↓
continue implementation
```

For OpenSpec work, update `tasks.md` as work progresses.

For architecture changes, follow the required reassessment and decision-record process.

---

## 12. Verification

Verification proves that the implemented behavior matches the intended behavior.

Use the verification gates defined by [DELIVERY_RULES.md](DELIVERY_RULES.md).

Typical gates include:

* native tests (`pio test -e native`);
* firmware build (`teensy41-capture-serial` unless opted out);
* HITL baseline (`.cursor/rules/HITL-Test-Flow.mdc`);
* HITL edit flow (`.cursor/rules/HITL-Edit-Test-Flow.mdc`);
* persistence/manual overlay verification;
* long-record scenarios.

Firmware upload requires explicit user confirmation after a successful build.

Verification is not complete merely because the code compiles.

For parent Bugs: prefer a two-step close — implementation complete → **Fixed — pending verification** → named native/HITL → documentation closeout → close (details in [GITHUB_WORK_TRACKING.md](GITHUB_WORK_TRACKING.md)).

---

## 13. Documentation closeout

Every implemented change that changes logic, behavior, functionality, architecture, data model, or externally observable behavior requires documentation closeout.

The invariant is:

> **No behavioral change is complete while the durable documentation still describes the old behavior.**

A separate plan is not mandatory for every change.

Instead, determine which artifact owns the resulting truth:

```text
Behavior / contract
        ↓
appropriate durable authority
```

Possible destinations include:

* `openspec/specs/`;
* [`docs/Authority/`](README.md);
* [`docs/Guides/`](../Guides/);
* [`DECISION_LOG.md`](../DECISION_LOG.md);
* a retained implementation plan under [`docs/Plans/`](../Plans/);
* a concise GitHub issue record when repository documentation does not require a separate artifact.

For multi-RC bugfixes: update the bugfix plan / freeze state **per RC**; update authority/guides when that RC changes the behavior or contract they describe; otherwise complete durable closeout at parent Bug close.

See [DOCUMENTATION_CLOSEOUT.md](DOCUMENTATION_CLOSEOUT.md).

---

## 14. Closeout

Before closing a work item:

1. Verify the implementation.
2. Verify the relevant documentation reflects the resulting behavior.
3. Update OpenSpec `tasks.md` where applicable.
4. Run `/opsx:sync` and `/opsx:archive` where applicable.
5. Update [`PROJECT_STATE.md`](../Runtime/PROJECT_STATE.md).
6. Update [`CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md).
7. Update [`DELIVERABLE_TRACKING.md`](../DELIVERABLE_TRACKING.md) when something ships or scope changes.
8. Append `DEC-###` when a design decision requires one ([DECISION_LOG.md](../DECISION_LOG.md)).
9. Close the GitHub Issue/sub-issue.
10. Leave plans and decision records as historical context rather than deleting them.

This follows the existing session hygiene requirements in [DELIVERY_RULES.md](DELIVERY_RULES.md). For design-heavy chats without a merge, also run [SESSION_CLOSEOUT.md](../Templates/SESSION_CLOSEOUT.md).

---

## 15. Lifecycle summary

```text
DISCOVER
    │
    ▼
INVESTIGATE
    │
    ▼
UNDERSTAND
    │
    ▼
DECIDE
    │
    ├── Reject ───────────────→ record decision
    │
    ├── Defer ────────────────→ preserve future work
    │
    └── Fix / Feature / Refinement / Optimization
                │
                ▼
          GITHUB ISSUE
                │
                ▼
       ARCHITECTURE GATE
                │
                ▼
      PLAN / OPENSPEC AS NEEDED
                │
                ▼
          CURRENT_WORK
                │
                ▼
          IMPLEMENT
                │
                ▼
      BRANCH + PR → dev
      (default; see GITHUB_WORK_TRACKING §10)
                │
                ▼
           VERIFY
                │
                ▼
     DOCUMENTATION CLOSEOUT
                │
                ▼
             SHIP
                │
                ▼
        CLOSE / ARCHIVE
```

Default delivery for firmware/docs under a tracked Issue: short-lived branch off `dev`, PR into `dev` ([GITHUB_WORK_TRACKING.md](GITHUB_WORK_TRACKING.md) § Branches and pull requests). Direct commits on `dev` only by explicit exception.

---

## 16. Non-goals

This lifecycle does not introduce:

* a second work queue;
* a `plans/INDEX.md`;
* a duplicate OpenSpec system;
* mandatory plans for every task;
* mandatory GitHub issues for every observation;
* a requirement to rewrite historical documentation;
* a replacement for architecture authority;
* a replacement for `CURRENT_WORK.md`.

The purpose is to connect existing systems, not add another one.
