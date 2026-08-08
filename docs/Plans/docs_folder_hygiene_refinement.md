# Docs folder hygiene — refinement plan

**Status:** Proposed — **Phase 1 Done** (2026-08-08); **Phase 1.5 Done** (2026-08-08)  
**Goal:** Reduce `docs/` clutter without creating a second live queue, duplicate index, or new documentation-management mechanism.  
**Supersedes:** Hygiene review item 18 remainder ([`codebase_hygiene_technical_debt_review.md`](codebase_hygiene_technical_debt_review.md) §18).

---

## Problem

- `docs/Plans/` has ~192 files in a flat folder (bugfixes, handoffs, refinements, 47 Cursor `*.plan.md` exports).
- Top-level `docs/` folder names mix styles: `Authority/`, `Guides/`, `Plans/`, `Runtime/`, `Agents/`, `Templates/`.
- Agents may treat historical plans as implementation authority.
- Documentation currently has some ambiguity between **current work**, **deliverable tracking**, **living guides**, and **historical design material**.

The goal is to clarify these boundaries without introducing another index, queue, manifest, or documentation workflow.

---

## Principles

| Do | Don't |
|----|--------|
| One **live work queue**: [`Runtime/CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md) | Add `Plans/INDEX.md` or another work manifest |
| Use [`DELIVERABLE_TRACKING.md`](../DELIVERABLE_TRACKING.md) for deliverable-level state, not implementation priority | Treat `DELIVERABLE_TRACKING.md` as a second implementation queue |
| Keep authority docs under `Authority/` (after Phase 1.5) | Leave competing authority trees or sort-prefix folder names (`00-`) |
| Guides = living behavior/how-to documentation | Merge Guides into plans |
| Plans = proposed or historical design/implementation history | Treat historical plans as current implementation authority |
| `git mv` + repository-wide reference check + link fix when archiving | Bulk rename or rewrite historical content |
| Add/normalize `Status:` only when useful or when a file is already being touched | Bulk-edit historical plans merely to add metadata |
| Preserve historical documents unless there is a specific reason to remove them | Rewrite or delete historical design docs |

### Documentation authority rule

Documentation should describe **authority, current work, living behavior, or history**. It must not create another coordination mechanism.

The four primary buckets are:

1. **Authority** — architecture/contracts that define what the system is.
2. **Runtime** — what should be worked on now.
3. **Guides** — current behavioral and how-to documentation.
4. **Plans** — proposed or historical design/implementation work.

Other documentation may exist as supporting/reference material, but it must not independently define current implementation priority.

### Work-queue rule

- `docs/Runtime/CURRENT_WORK.md` = **what should be worked on now** (after Phase 1.5; today `docs/Runtime/`).
- `docs/DELIVERABLE_TRACKING.md` = **deliverable-level state and completion tracking**.
- Neither plans, READMEs, backlogs, nor indexes may independently define current implementation priority.

---

## Phase 1 — Navigation

**Status:** **Done** (2026-08-08)

**Outcome:** Clear entry points and authority boundaries, with no broken links.

### 1.1 Clarify `docs/README.md`

Add a short four-bucket navigation section to [`docs/README.md`](../README.md):

- `Authority/` — authoritative architecture/contracts
- `Runtime/` — current implementation queue
- `Guides/` — living behavior/how-to documentation
- `Plans/` — proposed and historical design work

Point **“what to build now” only at `Runtime/CURRENT_WORK.md`**.

Also make clear that `DELIVERABLE_TRACKING.md` tracks deliverable state and is not a second work queue.

Do not create another index or manifest.

### 1.2 Slim `docs/Plans/README.md`

Reduce [`docs/Plans/README.md`](README.md) to:

- folder conventions;
- archive policy;
- links to `CURRENT_WORK.md` and `DELIVERABLE_TRACKING.md`;
- a short explanation that plans are not implementation authority;
- only genuinely cross-cutting pointers.

Keep only cross-cutting pointers such as:

- `phase-3-multi-loop`;
- `refactor_priority_backlog`;
- hygiene review.

Remove long per-topic file lists. They duplicate navigation information and will become stale.

### 1.3 Consolidate the architecture location

Move:

`docs/Architecture/HITL_ARCHITECTURE.md`

to:

`docs/Authority/Architecture/HITL_ARCHITECTURE.md`

Update all inbound references, including known references in:

- `hitl-cli-rebuild`;
- `hitl_cli_rebuild_enhancement.md`.

Before finishing, perform a repository-wide search for the old path and filename.

After references are fixed, remove the now-empty `docs/Architecture/` directory.

**Default:** delete the obsolete directory rather than leaving a redirect stub.

Only leave a one-line redirect stub if an external dependency, tooling requirement, or other concrete repository constraint requires the old path to remain.

### 1.4 Remove `.DS_Store`

Add:

`docs/.DS_Store`

to `.gitignore` if not already covered.

Delete the tracked copy.

Verify there are no other tracked `.DS_Store` files that should also be ignored.

### Phase 1 verification

Run:

```text
rg 'docs/Architecture/HITL' .
rg 'HITL_ARCHITECTURE\.md' .
git diff --check
git status
```

Confirm:

- no stale references remain;
- `docs/README.md` is readable in <2 minutes;
- `docs/Plans/README.md` is concise;
- there is only one current-work queue.

---

## Phase 1.5 — Uniform top-level folder names

**Status:** **Done** (2026-08-08)

**Outcome:** All primary `docs/` subfolders use consistent **PascalCase**; `00-authority/` renamed to `Authority/`.

### Target layout (top-level `docs/` only)

| Current | Target | Notes |
|---------|--------|--------|
| `Authority/` | `Authority/` | Drop `00-` prefix; nested `Architecture/` unchanged |
| `Plans/` | `Plans/` | Includes `Plans/archive/` from Phase 2 |
| `Runtime/` | `Runtime/` | `CURRENT_WORK.md`, `PROJECT_STATE.md`, `ROADMAP.md` |
| `Agents/` | `Agents/` | architect / builder / reviewer |
| `Templates/` | `Templates/` | PREFLIGHT, SESSION_CLOSEOUT, etc. |
| `Guides/` | `Guides/` | Already PascalCase — no change |
| `Refinements/` | `Refinements/` | Already PascalCase — no change |

Root-level `docs/*.md` files (`README.md`, `DECISION_LOG.md`, …) stay at `docs/` — only **directories** are renamed.

### Procedure

1. **`git mv` each directory** (one logical rename per folder; **one commit** for the whole phase).
2. **Case-only renames** (`plans` → `Plans`, etc.): on case-insensitive filesystems use a two-step move, e.g. `git mv docs/Plans docs/Plans_tmp` then `git mv docs/Plans_tmp docs/Plans`.
3. **Repository-wide reference update** — search and fix paths in:
   - `docs/` (including relative links inside moved trees);
   - `.cursor/rules/` and `.cursor/plans/`;
   - `openspec/` (`config.yaml`, changes, archives);
   - root `README.md`;
   - firmware comments that cite doc paths (`include/` — rare).
4. **Update four-bucket table** in `docs/README.md` and slim `docs/Plans/README.md` to use new paths.
5. **Redirect stubs** — update `docs/PROJECT_INTENT.md` to point at `Authority/PROJECT_INTENT.md` (not `Authority/`).
6. Do **not** rewrite document bodies beyond path/link fixes.

### Verification

```text
rg '00-authority|docs/Plans/|docs/Runtime/|docs/Agents/|docs/Templates/' . \
  --glob '!docs/Plans/docs_folder_hygiene_refinement.md'
rg 'docs/Plans/|docs/Authority/|docs/Runtime/' docs/README.md
git diff --check
pio test -e native   # only if include/ comments or test paths changed
```

Confirm no stale lowercase folder paths remain in live docs, rules, or OpenSpec config.

### Risks / constraints

- **Large diff** (~100+ files) — path substitution only; keep as a dedicated commit, not bundled with Phase 2 archive moves.
- **External bookmarks** — outside repo; acceptable; Git history preserves old paths.
- **Do not rename** `openspec/`, `captures/`, `scripts/`, or `.cursor/` directory names in this phase — `docs/` only.

---

## Phase 2 — Archive layout

**Prerequisite:** Phase 1.5 complete (paths below use `docs/Plans/`).

**Outcome:** `docs/Plans/` root contains only active or explicitly retained near-term working plans.

Target structure:

```text
docs/Plans/
  README.md
  archive/
    cursor-exports/
    handoff/
    bugfix/
    refinements/
  <active plans at root>
```

### Root-plan rule

A plan belongs at `docs/Plans/` root only if:

- `CURRENT_WORK.md` points to it; or
- it represents an explicitly active implementation effort; or
- it is intentionally retained as a near-term working plan.

“Recently touched” alone is **not** sufficient reason to keep a plan at the root.

Everything else belongs in the appropriate archive category.

---

### Batch 2a — Cursor exports

**Scope:** Move historical `*.plan.md` to `docs/Plans/archive/cursor-exports/`.

**Safety gate:** Do not blindly move every `*.plan.md`.

Before moving each batch, verify that the files are not referenced by:

- `CURRENT_WORK.md`;
- authority documentation;
- active plans;
- living guides;
- other live documentation;
- repository tooling that expects the path.

A Cursor export that is still intentionally used as an active working document may remain at the root.

Historical exports that have no live dependency can be moved mechanically.

**Procedure** (per batch):

1. Identify the files to archive.
2. Search repository-wide for their filenames/paths.
3. `git mv` the files.
4. Fix all affected references.
5. Search again for stale references.
6. Run `git diff --check`.
7. Inspect the resulting diff before committing.

Do not rewrite the contents merely because they are being archived.

**Exit:** Normally `ls docs/Plans/*.plan.md` should be empty. Exceptions require an explicit reason that the file remains an active working document.

---

### Batch 2b — Superseded handoffs

**Scope:** Archive `*_handoff.md` when superseded by shipped work.

**Gate:** `Status: Done` or explicit superseded/completed note near the top; not linked from `CURRENT_WORK.md`. If lifecycle status is ambiguous, resolve before moving. Do **not** bulk-edit handoffs solely to add a `Status:` field.

**Destination:** `docs/Plans/archive/handoff/`

---

### Batch 2c — Closed bugfix plans

**Scope:** Archive `*_bugfix.md` when the relevant RC layer is closed.

**Gate:** `Status: FROZEN` per [Multi-Stage-Bugfix-Workflow](../../.cursor/rules/Multi-Stage-Bugfix-Workflow.mdc). If clearly complete but lacks status, determine whether adding status is useful before archiving — do not auto-rewrite.

**Destination:** `docs/Plans/archive/bugfix/`

---

### Batch 2d — Refinements

**Scope:** Review `docs/Refinements/`. If historical, prefer `docs/Plans/archive/refinements/`. If retaining `docs/Refinements/` for discoverability, add a short README/banner identifying it as historical. No mass content rewrite.

---

### Per-batch verification

For every archive batch:

```text
git mv ...
rg '<old filename/path>' .
rg 'docs/Plans/' .
git diff --check
git status
```

Fix references before committing. **One commit per batch.**

---

## Phase 3 — Optional root trim

Only if confusion remains after Phases 1–2.

### 3.1 `docs/PROJECT_INTENT.md`

Keep if useful; minimal redirect if inbound refs exist; remove after fixing refs if it duplicates authority. Do not create another source of project priority.

### 3.2 `FEATURES.md` / `FEATURE_PLANS.md`

Clarify role in `docs/README.md` (checklist vs historical vs queue). If either acts as implementation queue, redirect to `CURRENT_WORK.md`. No merge unless clear benefit.

### 3.3 `CURRENT_WORK.md` history

Consider `docs/Runtime/HISTORY.md` only if `CURRENT_WORK.md` becomes hard to use. Defer otherwise.

---

## Out of scope

- OpenSpec archive (`openspec/changes/archive/`) — separate process.
- Mass content rewrite of old plans.
- Deleting historical design documents merely because they are old.
- Deleting capture references or DEC-### anchors.
- Creating a new plan index, manifest, backlog, or documentation queue.
- Reopening architectural decisions that are already settled.

---

## Suggested order

1. **Phase 1** — low-risk navigation and authority clarification. **Done** (2026-08-08).
2. **Phase 1.5** — uniform PascalCase `docs/` folder names; `Authority/` → `Authority/`.
3. **Phase 2a** — historical Cursor exports (under `docs/Plans/archive/`).
4. **Phase 2b–2d** — archive handoffs, bugfixes, and refinements as lifecycle is clear.
5. **Phase 3** — only if confusion remains.

Do not combine Phase 1.5 with Phase 2 archive batches. Do not combine Phase 1 with large-scale archival.

---

## Completion criteria

The cleanup is successful when:

- `docs/README.md` clearly explains where authority, current work, guides, and plans live (PascalCase paths after Phase 1.5).
- `CURRENT_WORK.md` is the sole source of current implementation priority.
- `DELIVERABLE_TRACKING.md` has a clearly separate deliverable-tracking role.
- `docs/Authority/` is the unambiguous authority tree (no `00-` prefix).
- `docs/Plans/README.md` does not maintain a duplicate file index.
- `docs/Plans/` root contains only active or explicitly retained near-term plans.
- Historical Cursor exports are archived without unnecessary content changes.
- No broken or stale references were introduced.
- Historical documents remain recoverable and traceable through Git.
- No second documentation queue or manifest has been introduced.

---

## Tracking

When a phase ships:

1. Briefly note the completed phase in `CURRENT_WORK.md`.
2. Mark the corresponding phase **Done** in this plan.
3. Do not create a separate tracking/index file.

The cleanup plan itself is implementation history; `CURRENT_WORK.md` remains the only source for what should be worked on next.
