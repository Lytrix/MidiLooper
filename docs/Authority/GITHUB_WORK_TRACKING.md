# GitHub Work Tracking

**Purpose:** Define how GitHub Issues and Projects represent decided work without becoming a second architecture or execution system.

**Way of working (WOW):** This document. The **[Work](https://github.com/users/Lytrix/projects/1)** project is the **feature-level roadmap**; plans and runtime docs hold implementation detail; Issues and PRs appear only when something is actually being implemented, reviewed, or merged.

**Related:** [WORKFLOW_LIFECYCLE.md](WORKFLOW_LIFECYCLE.md), [ARCHITECTURE_RULES.md](ARCHITECTURE_RULES.md), [DELIVERY_RULES.md](DELIVERY_RULES.md).

**Authority:** Subordinate to [ARCHITECTURE_RULES.md](ARCHITECTURE_RULES.md) and [DELIVERY_RULES.md](DELIVERY_RULES.md). Coordination only — does not replace [`CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md).

---

## 1. Role of GitHub

GitHub serves two coordinated roles:

| Surface | Answers |
|---------|---------|
| **Work project** | What capabilities and architectural milestones are planned, in progress, or parked? |
| **Issues / PRs** | What concrete implementation is being reviewed or merged right now? |

A GitHub **Issue** answers:

> **What decided piece of work is being implemented, reviewed, or verified?**

Create an Issue when implementation starts — not when an idea first appears, and not for every planned capability on the board.

It does not answer:

* What is the architecture?
* What is the normative behavior?
* What should be worked on immediately?
* How must an architecture migration be performed?

Those responsibilities remain with the repository's existing authority, delivery, and runtime documents.

---

## 1.1 Three levels of work visibility

Planned architecture and features are spread across plans, runtime docs, decisions, and implementation notes. Use **three levels** so the project can answer: *What are we actually building, and what remains to be done?*

```text
Work project (capability / milestone cards)
    → What capabilities are planned?

Plans / design docs + runtime docs
    → How are we going to solve them?

Issues / PRs
    → What are we implementing right now?
```

| Level | Holds | Does not hold |
|-------|--------|----------------|
| **Work project** | Meaningful capabilities and architectural milestones; Status NOW / NEXT / PARKED; short link to the authoritative plan | Hypotheses, capture IDs, stage checklists, RC layers, device gate tables |
| **Plans / runtime docs** | Hypotheses, measurements, experiments, stages, captures, device gates, implementation steps, decisions that change during investigation | Normative SHALL/MUST (OpenSpec specs after archive) |
| **Issues / PRs** | Concrete implementation under review or merge; verification checklist for that slice | Whole-architecture design; backlog of every future idea |

**Populate the Work project** with feature/milestone cards. A single active slice (for example overdub participant discovery) must **not** be the only card — that misrepresents the rest of the planned work you are actively reasoning about.

Keep the **Issues list** free of premature implementation tickets. Use **draft project items** (no Issue) for capabilities that are planned but not yet in implementation.

**Do not** use an empty project board as the default. An empty board hides planned work that is not yet stable enough to be a GitHub Issue but is still part of the product architecture.

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

## 6. GitHub Project — feature-level roadmap

Use **one** Project: **[Work](https://github.com/users/Lytrix/projects/1)** (owner `Lytrix`, number `1`, linked to `Lytrix/MidiLooper`).

The Work project is a **central capability overview**, not a development-task tracker. Each card is a **meaningful capability or architectural milestone** with a short body linking to the authoritative plan or architecture doc. Implementation detail stays in plans and [`CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md).

### Card titles and descriptions

**Titles** must read in plain language — what the capability **is**, not internal shorthand alone.

| Put in **title** | Put in **description** (not title alone) |
|------------------|--------------------------------------------|
| Capability or outcome in words | `DEC-###`, Layer A–D, Stage N, Phase N, §4.8–4.10, OpenSpec change id |
| Who/what is affected when helpful | Plan path; shipped vs remainder; authorized vs recorded-only |

**Do not** use only a few words or codes as the title, for example:

- `Layer B–C`, `Layer D`, `Stage 4–6`, `Stage 3b`, `Phase 5`, `DEC-040`, `§4.8–4.10`, `D13–D15`

Those belong in the **description** under a **`Refs:`** line (or equivalent) for fast lookup, together with the plain-language summary.

**Good title:** `Loop content history — clear-as-unlink and bounded replay`  
**Description (excerpt):** `Refs: DEC-035 Layers B–C; Stages 4–6. Recorded in architecture; firmware not authorized.`

**Bad title:** `Loop content history Layers B–C` or `DEC-035 Stages 4–6`

Same rule for Issues when they represent a capability (Task/Feature), not only for draft project cards.

### Card types on the board

| Card kind | When to use | GitHub object |
|-----------|-------------|---------------|
| **Capability / milestone** | Planned or active architectural slice; may span many commits and stages | **Draft project item** (preferred) or linked Feature Issue when implementation is underway |
| **Implementation** | A specific Bug/Task under active development or review | **Issue** (+ PR), linked to the board when work starts |

Prefer **draft items** for capabilities that do not yet need an Issue. Promote to an Issue when you open a branch and need review/merge identity.

### Status (board columns)

| Status | Meaning |
|--------|---------|
| **NOW** | Active architectural slice; should appear in [`CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md) § Now implementing |
| **NEXT** | Decided capability ready or likely after current NOW work |
| **PARKED** | Valid deferred capability — visible without entering execution |

Flow:

```text
PARKED → NEXT → NOW → CURRENT_WORK → (optional Issue) → branch → PR → merge → update card / close Issue
```

The Project must **not** become a second detailed execution queue. [`CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md) remains execution authority for stages, captures, and device gates.

### Optional fields

Add only when useful:

| Field        | Purpose |
| ------------ | ------- |
| **Priority** | P0 / P1 / P2 / P3 |
| **Area**     | Note Edit / Playback / Storage / Display / MIDI / HITL / etc. |

**Type** is the GitHub Issue type (Feature / Bug / Task) when an Issue exists — not a parallel Project taxonomy.

Avoid creating labels for information that should be a structured field.

Use labels only for genuinely orthogonal cross-cutting information.

Do not create a large taxonomy before it is needed. Do not adopt Epic → Story → Task hierarchy initially.

### 6.1 Capability roadmap (maintain on Work)

Keep **draft milestone cards** on the board. **Title** = plain language (§ Card titles and descriptions). **Description** = `Refs:` line with DEC/Layer/Stage/Phase numbers, then shipped/remainder, then plan link. Status reflects **capability** progress, not every RC or capture.

| Status | Title (board) | Refs (description line) | Plan |
|--------|---------------|-------------------------|------|
| **NOW** | Overdub participant discovery — present at start tick | Phase 3 fill disable next; DEC-040 (skip PLAYING/STOPPED/MUTED HITL) | [`overdub_participant_loop_content_architecture.md`](../Plans/overdub_participant_loop_content_architecture.md) |
| **NEXT** | NOTE_EDIT hydrate from prepared loop content | overlap = selected/mover LinearSpan; DEC-037 amendment | [`note_edit_hydrate_enhancement.md`](../Plans/note_edit_hydrate_enhancement.md) |
| **NEXT** | Playback gather — horizon and LCR consume (remainder) | Stage 1 hooks **shipped**; Stages 2–3 open | [`playback_gather_lcr_consume_enhancement.md`](../Plans/playback_gather_lcr_consume_enhancement.md) |
| **NEXT** | Loop length change during overdub | queued; no firmware until CURRENT_WORK | [`overdub_loop_length_during_overdub_enhancement.md`](../Plans/overdub_loop_length_during_overdub_enhancement.md) |
| **NEXT** | Host HITL CLI rebuild | layered `base` + `edit_full`; OpenSpec `hitl-cli-rebuild` | [`hitl_cli_rebuild_enhancement.md`](../Plans/hitl_cli_rebuild_enhancement.md) |
| **PARKED** | Streaming loop resolution — range-first load | DEC-035 **Layer D**; Stage 7; not started | [`loop_layer_history_persistence_architecture.md`](../Plans/loop_layer_history_persistence_architecture.md) |
| **PARKED** | Derived editing state replaces in-session undo stack | DEC-035 **Stage 3b**; new DEC required; not started | [`loop_layer_history_persistence_architecture.md`](../Plans/loop_layer_history_persistence_architecture.md) |
| **PARKED** | Loop content history — clear-as-unlink and bounded replay | DEC-035 **Layers B–C**; **Stages 4–6**; recorded, firmware not authorized | [`loop_layer_history_persistence_architecture.md`](../Plans/loop_layer_history_persistence_architecture.md) |
| **PARKED** | Set-revision overlay loop picker | `set-revision-persistence` **§4.8–4.10**; overlay **§4.1–4.7 shipped** | [`set_revision_persistence_handoff.md`](../Plans/set_revision_persistence_handoff.md) |
| **PARKED** | Crash recovery — longest valid prefix load | DEC-020 **Phase 5**; not started | [`continuous_runtime_persistence_phase5_recovery_handoff.md`](../Plans/continuous_runtime_persistence_phase5_recovery_handoff.md) |
| **PARKED** | Display unification — shared visual cache remainder | Stages **1–2, 5–9 shipped**; Stages **3–4** + paint gap open | [`note_edit_visual_cache_display_unification_refinement.md`](../Plans/note_edit_visual_cache_display_unification_refinement.md) |
| **PARKED** | Runtime scheduling — owner-boundary gate follow-up | grooming / pressure slices **shipped**; interval reservation not authorized | [`runtime_scheduling_owner_boundary_admission_refinement.md`](../Plans/runtime_scheduling_owner_boundary_admission_refinement.md) |
| **PARKED** | Jam arrangement capture into slots | **D13–D15**; slot infrastructure **shipped** on `dev` | [`phase-3-multi-loop.md`](../Plans/phase-3-multi-loop.md); [`ROADMAP.md`](../Runtime/ROADMAP.md) |

Update this table when a capability moves column or a new architectural milestone is decided. Do not duplicate stage checklists here.

### 6.2 Shipped — archive or remove board cards

These are **not** PARKED work. Remove the card from **Work** when the capability is fully delivered, or keep a one-line **SHIPPED** note in the card body until archived. Partial delivery stays on the board with **shipped / remainder** called out in §6.1 (Display, loop picker, jam capture, playback gather).

| Capability | Evidence | Board |
|------------|----------|-------|
| Loop content history Layer A (DEC-035) | PR [#34](https://github.com/Lytrix/MidiLooper/pull/34); OpenSpec `loop-content-history/` | Archive |
| LoopContentResolution prototype (DEC-037 core) | PR [#35](https://github.com/Lytrix/MidiLooper/pull/35); consumers remain in NEXT rows | Archive |
| Set-revision overlay §4.1–4.7 | [`set_revision_persistence_handoff.md`](../Plans/set_revision_persistence_handoff.md) | No card (picker §4.8–4.10 only) |
| Multi-loop slot infrastructure | [`phase-3-multi-loop.md`](../Plans/phase-3-multi-loop.md) | No card (jam capture only) |
| StorageManager TU extraction | PR [#17](https://github.com/Lytrix/MidiLooper/pull/17); Issue [#16](https://github.com/Lytrix/MidiLooper/issues/16) | **Archive** stale Issue cards |
| Codebase consistency Phase 4 / LR | PRs [#22](https://github.com/Lytrix/MidiLooper/pull/22)–[#26](https://github.com/Lytrix/MidiLooper/pull/26); Issue [#18](https://github.com/Lytrix/MidiLooper/issues/18) | **Archive** stale Issue cards |
| Memory pressure 1A/1B/2B; consumer grooming 1b–2c | [`memory_pressure_reclaim_refinement.md`](../Plans/memory_pressure_reclaim_refinement.md); grooming plans | Covered by scheduling PARKED card body |
| Playback gather Stage 1 lateness hooks | [`playback_gather_lcr_consume_enhancement.md`](../Plans/playback_gather_lcr_consume_enhancement.md) | NEXT card (Stages 2–3 remain) |
| DEC-036 Layer D 3b overdub entry (no full visual rebuild) | [`loop_layer_d_overdub_rebuild_architecture.md`](../Plans/loop_layer_d_overdub_rebuild_architecture.md) | No card (distinct from Layer D range-first load) |

**Verified still PARKED (not shipped):** range-first load (DEC-035 Layer D / Stage 7); derived undo stack (Stage 3b); clear-as-unlink + bounded replay (Layers B–C / Stages 4–6, recorded only); crash prefix load (DEC-020 Phase 5); loop picker (§4.8–4.10); jam capture (D13+); owner-boundary gate firmware.

---

## 7. GitHub versus repository documents

| Question                                  | System                                   |
| ----------------------------------------- | ---------------------------------------- |
| What capabilities are we building?      | **Work project** (§1.1, §6.1)            |
| What work is being implemented now?       | GitHub Issues (when opened) + PRs        |
| How are work items related?               | GitHub sub-issues (implementation only)  |
| How do I filter/group capabilities?       | GitHub Project Status / Area / Priority  |
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

### Owner-Boundary Gate at maintenance time

When touching **runtime scheduling**, **deferred idle maintenance**, **persistence save**, **display resolve**, or **overdub/playback hot paths** — even for small fixes or hygiene — skim [`runtime_scheduling_owner_boundary_admission_refinement.md`](../Plans/runtime_scheduling_owner_boundary_admission_refinement.md) and the [scheduling investigation log](../Plans/archive/refinements/runtime_scheduling_timing_envelope_investigation.md). Ask whether the change adds or extends an unbounded owner before interval reservation is authorized. This is a **maintenance-run checkpoint**, not a NOW board card.

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
* create an Issue for every planned capability (use a **draft project card** instead);
* create an issue for every tiny implementation step;
* create one GitHub Issue per RC;
* use GitHub labels as a replacement for architecture;
* duplicate OpenSpec requirements in the issue;
* copy an entire plan into the issue or project card body;
* use GitHub status as a replacement for `CURRENT_WORK.md`;
* create a second backlog in Markdown;
* migrate all historical plans into GitHub merely for completeness;
* adopt Epic → Story → Task hierarchy initially;
* skip a short-lived branch/PR for reviewable refinements by defaulting to commits on `dev`;
* auto-sync GitHub and `CURRENT_WORK.md`;
* leave the **Work project empty** while substantial capabilities exist only in plans;
* put a **single active slice** on the board as if it were the whole product roadmap.

GitHub should reduce coordination overhead, not create another documentation system.
