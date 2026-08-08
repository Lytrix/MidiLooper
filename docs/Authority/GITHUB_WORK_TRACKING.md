# GitHub Work Tracking

**Purpose:** Define how GitHub Issues and Projects represent decided work without becoming a second architecture or execution system.

**Related:** [WORKFLOW_LIFECYCLE.md](WORKFLOW_LIFECYCLE.md), [ARCHITECTURE_RULES.md](ARCHITECTURE_RULES.md), [DELIVERY_RULES.md](DELIVERY_RULES.md).

**Authority:** Subordinate to [ARCHITECTURE_RULES.md](ARCHITECTURE_RULES.md) and [DELIVERY_RULES.md](DELIVERY_RULES.md). Coordination only — does not replace [`CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md).

---

## 1. Role of GitHub

GitHub is the project's **work inventory and relationship system**.

A GitHub Issue answers:

> **What decided piece of work exists?**

It does not answer:

* What is the architecture?
* What is the normative behavior?
* What should be worked on immediately?
* How must an architecture migration be performed?

Those responsibilities remain with the repository's existing authority, delivery, and runtime documents.

---

## 2. Issue types

Use a small taxonomy.

| Type        | Use                                                                   |
| ----------- | --------------------------------------------------------------------- |
| **Feature** | Larger capability or initiative                                       |
| **Bug**     | Confirmed incorrect behavior                                          |
| **Task**    | Engineering work, refactoring, migration, optimization, or refinement |

Do not create additional issue types unless a recurring distinction cannot be represented adequately by these three.

### Parent type test

> Is the parent describing something that is **wrong**, or something we have **decided to change**?

| Answer | Type |
|--------|------|
| Wrong (confirmed functional failure) | **Bug** |
| Deliberate migration / refinement / engineering | **Task** |
| New capability | **Feature** |

Do not label an architectural migration as a Bug merely because the old architecture causes undesirable complexity.

---

## 3. Discovery is not automatically an issue

Do not create an issue for every:

* idea;
* hypothesis;
* observation;
* investigation;
* architectural question.

First investigate sufficiently to make a decision.

Example:

```text
Observation:
"Overlap seems wrong after deselection."

Investigation:
Trace selection → current state → focus → driver → resolver.

Finding:
The edit driver does not reconstruct the required state.

Decision:
Fix the driver contract.

GitHub:
Bug — Re-enter overlap resolution after deselection
```

The issue represents the **decided work**, not the original uncertainty.

### When a capture becomes a Bug

Create a **Bug** after the first **confirmed understanding** of the functional failure — not at first capture, and not only after the fix is designed.

Minimum understanding:

* failure confirmed incorrect;
* owner identified;
* invariant identified;
* reason it is wrong can be stated.

Minimum useful Bug body:

```text
## Failure
## Owner
## Invariant
## Why this is wrong
## Evidence
## Current investigation
```

(The fix path may remain unknown.)

RC / multi-stage investigation continues under that Bug — see [§5.1](#51-multi-rc-bugfix-and-sub-issues) and `.cursor/rules/Multi-Stage-Bugfix-Workflow.mdc`.

---

## 4. Future work

Future work is still a decision.

For example:

```text
Decision:
Separate visibility from geometry as a future refinement.
```

This should not be forgotten merely because it is deferred.

When it is sufficiently defined, represent it as a GitHub Task or Feature and place it in Project status **PARKED** (or **NEXT** when likely soon).

Do not put future work into [`CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md) until it becomes current execution scope.

---

## 5. Parent issues and sub-issues

Sub-issues provide hierarchy; they do not define a special type of work.

A parent can represent an initiative or stage:

```text
Feature
└── Task — Stage 1
    ├── Task
    ├── Bug
    └── Task
```

A stage may therefore simply be a **Task used as a parent**.

Example:

```text
Feature — Note Edit Resolver Authority
│
├── Task — Stage 1: participating-note authority
├── Task — Stage 2: resolver authority migration
├── Bug  — Sticky participation regression
└── Task — Stage 5: orthogonal state refinement
    ├── Task — Separate visibility from geometry
    └── Task — Remove obsolete display latch
```

Do not create a sub-issue for every numbered implementation step.

Create a child issue when the work has meaningful independent:

* completion;
* verification;
* blocking state;
* ownership;
* or historical value.

### 5.1 Multi-RC bugfix and sub-issues

RCs are part of the **debugging method**, not automatically separate units of work.

**Default:** RCs stay plan/commit-level under one parent Bug. Do not create one GitHub Issue per RC.

```text
GitHub Bug
    │
    ├── RC1  (plan + commit)
    ├── RC2  (plan + commit)
    ├── RC3  FROZEN (plan; optional parent comment/checklist)
    └── RC4  confirmed fix
```

**Promote** an RC to a GitHub sub-issue (or sibling Bug/Task) only when investigation establishes **independently actionable** work with its own lifecycle.

If RC investigation reveals a separate functional failure, create a **sibling Bug** (or Task for deliberate migration) rather than stretching the original Bug’s meaning.

When a Bug investigation becomes a broader ownership/model migration, create or link a **Task** for that migration; keep the original Bug as the functional-failure record.

**Frozen RC:** plan remains authoritative. At most, a lightweight parent-Issue comment or checklist entry. No sub-issue required for the freeze itself.

---

## 6. GitHub Project fields

Use **one** Project: **[Work](https://github.com/users/Lytrix/projects/1)** (owner `Lytrix`, number `1`, linked to `Lytrix/MidiLooper`).

### Status (board columns)

| Status | Meaning |
|--------|---------|
| **NOW** | Eligible for execution; may appear in [`CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md) |
| **NEXT** | Decided work ready or likely after current work |
| **PARKED** | Valid deferred work — visible without entering execution |

Flow:

```text
PARKED → NEXT → NOW → CURRENT_WORK → implement → close
```

The Project must **not** become a second detailed execution queue. [`CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md) remains execution authority for what is being implemented now.

### Optional fields

Add only when useful:

| Field        | Purpose |
| ------------ | ------- |
| **Priority** | P0 / P1 / P2 / P3 |
| **Area**     | Note Edit / Playback / Storage / Display / MIDI / HITL / etc. |

**Type** is the GitHub Issue type (Feature / Bug / Task), not a parallel Project taxonomy.

Avoid creating labels for information that should be a structured field.

Use labels only for genuinely orthogonal cross-cutting information.

Do not create a large taxonomy before it is needed. Do not adopt Epic → Story → Task hierarchy initially.

---

## 7. GitHub versus repository documents

| Question                                  | System                                   |
| ----------------------------------------- | ---------------------------------------- |
| What work exists?                         | GitHub Issues                            |
| How are work items related?               | GitHub sub-issues                        |
| How do I filter/group work?               | GitHub Project (NOW / NEXT / PARKED)     |
| What should I work on now?                | [`CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md) |
| What is the architecture?                 | [ARCHITECTURE_RULES.md](ARCHITECTURE_RULES.md) + authority docs |
| What was decided architecturally?         | [`DECISION_LOG.md`](../DECISION_LOG.md) |
| What are normative accepted requirements? | `openspec/specs/`                        |
| What is the proposed change?              | OpenSpec change / plan                   |
| What does the system currently do?        | Guides / authority docs                  |
| What happened during implementation?      | Plan/history/issue context               |
| What shipped?                             | [`DELIVERABLE_TRACKING.md`](../DELIVERABLE_TRACKING.md) |
| How does discovery become work?           | [WORKFLOW_LIFECYCLE.md](WORKFLOW_LIFECYCLE.md) |

No system should silently take over another system's responsibility.

---

## 8. Link GitHub and repository artifacts

For substantial work, link the GitHub Issue from the relevant plan or OpenSpec change.

For example:

```text
GitHub: #184
```

The GitHub Issue should not duplicate the complete plan.

Prefer:

```text
GitHub Issue
    ↓
work identity
    ↓
OpenSpec / plan
    ↓
implementation detail
```

rather than copying the same design into both locations.

Prefer issue bodies that state **Current / Desired / Why / Verification** (or link to the plan/OpenSpec that defines the end-state). Avoid issues that only say “Fix X” with no intended outcome.

---

## 9. GitHub does not bypass architecture governance

Creating or approving a GitHub issue does not authorize an architecture change.

If the work triggers architecture reassessment:

```text
GitHub Issue
    ↓
architecture trigger
    ↓
ARCHITECTURE_REASSESSMENT.md
    ↓
decision / alternatives / migration
    ↓
DEC-###
    ↓
OpenSpec / plan
    ↓
implementation
```

Ownership changes must follow the ownership-transfer protocol in [ARCHITECTURE_RULES.md](ARCHITECTURE_RULES.md) and [OWNERSHIP_TRANSFER.md](../Templates/OWNERSHIP_TRANSFER.md).

See also [ARCHITECTURE_REASSESSMENT.md](../ARCHITECTURE_REASSESSMENT.md).

---

## 10. Branches and pull requests

GitHub Issues are **work identity**. Branches and PRs are how decided firmware/docs work lands on `dev`.

Branch model authority: [`docs/BRANCHING.md`](../BRANCHING.md) — default integration branch is **`dev`**.

### Default for refinements, Tasks, and Features

```text
dev
  └── feature/<scope>  or  refactor/<scope>   (short-lived)
        └── PR → dev
```

| Question | Answer |
|----------|--------|
| Do I need a **PR**? | **Default yes** for reviewable firmware or docs changes that ship under a tracked Issue. |
| Does a **PR** require a branch? | **Yes.** Source branch ≠ `dev`. There is no PR from `dev` → `dev`. |
| Does every **commit** need a PR? | No — but if you skip the PR, you are committing directly on `dev` by explicit choice, not by default. |
| Does every **Issue** need its own branch? | No. Multiple small slices under one Task may share one short-lived branch/PR, or use one PR per shippable slice. |

Naming:

* `feature/<scope>` — product / capability work
* `refactor/<scope>` — behavior-preserving structural work (TU extraction, DRY, hygiene)
* `bugfix/<scope>` — optional for Bugs when a dedicated branch helps review

Link the Issue from the PR (`Fixes #N` / `Refs #N`) so Project **Work** and git history stay traceable. A mechanical `closes #N` is not required on every PR (see [§11 Closing](#11-closing-a-github-issue)).

### When a PR is optional

Direct commits on `dev` are allowed only when you **explicitly** choose that path (e.g. tiny doc-only fix with no review need). They do not replace Issue identity or documentation closeout.

Do **not** treat “small refinement” as automatic permission to skip a branch/PR. Prefer the short-lived branch → PR → `dev` path for TU extraction and similar Tasks.

### Relation to Project status

```text
Issue on Project NOW
    ↓
CURRENT_WORK
    ↓
branch off dev
    ↓
implement + verify
    ↓
PR → merge to dev
    ↓
docs closeout → close Issue (or leave open for remaining checklist)
```

Project **NOW** / **NEXT** / **PARKED** does not create branches. Agents create the branch when implementation starts under CURRENT_WORK.

---

## 11. Closing a GitHub issue

A work item is not complete merely because the code is merged.

### Two-step close (especially Bugs)

```text
Implementation ships (RCs may ship while Bug stays open)
        ↓
Fixed — pending verification
        ↓
Named native / HITL verification
        ↓
Documentation closeout
        ↓
Close Bug
```

**Close invariant:** A parent Bug closes only after the functional failure is **verified** fixed and required documentation closeout is complete.

Related warts must not block closure if they are explicitly separated into another Bug or PARKED Task.

### Close checklist

Before closing:

* implementation is verified;
* required tests/HITL are complete;
* OpenSpec tasks are complete where applicable;
* OpenSpec is synced/archived where applicable;
* current behavior documentation is correct ([DOCUMENTATION_CLOSEOUT.md](DOCUMENTATION_CLOSEOUT.md));
* architectural decisions are recorded;
* [`CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md) is updated;
* [`DELIVERABLE_TRACKING.md`](../DELIVERABLE_TRACKING.md) is updated when appropriate.

Then close the issue.

A PR/commit does not need a mechanical `closes #123` in every case, but the work must remain **traceable** to the Issue.

---

## 12. Enforcement

**Mode: both** — agent and human review apply the same gate.

Before substantial or behavior-changing implementation:

* establish the relevant GitHub work item (or existing parent scope);
* intended outcome;
* applicable plan/OpenSpec when required;
* current execution scope in [`CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md);
* short-lived branch off `dev` when a PR is the delivery path ([§10](#10-branches-and-pull-requests)).

Tiny changes may ride an existing tracked Task; the requirement is **traceability to decided work**, not a new Issue for every edit.

Human review should return work when there is no traceable work item, scope drifted without Issue update, required docs/architecture gates were skipped, or a reviewable refinement landed on `dev` without an agreed direct-commit exception.

---

## 13. OpenSpec and Bugs

OpenSpec is **not** required for every Bug.

Use OpenSpec when a bug investigation becomes a **substantial migration/refinement** toward a defined normative end-state ([DELIVERY_RULES.md](DELIVERY_RULES.md), [WORKFLOW_LIFECYCLE.md](WORKFLOW_LIFECYCLE.md) § OpenSpec).

Without OpenSpec, the minimum durable record is: GitHub Bug + bugfix plan/investigation history + required documentation closeout + verification evidence.

---

## 14. Anti-patterns

Do not:

* create a Bug for an uninvestigated hypothesis;
* create an issue for every tiny implementation step;
* create one GitHub Issue per RC;
* use GitHub labels as a replacement for architecture;
* duplicate OpenSpec requirements in the issue;
* copy an entire plan into the issue;
* use GitHub status as a replacement for `CURRENT_WORK.md`;
* create a second backlog in Markdown;
* migrate all historical plans into GitHub merely for completeness;
* adopt Epic → Story → Task hierarchy initially;
* skip a short-lived branch/PR for reviewable refinements by defaulting to commits on `dev`;
* auto-sync GitHub and `CURRENT_WORK.md`.

GitHub should reduce coordination overhead, not create another documentation system.
