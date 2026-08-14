# Delivery rules

How work is proposed, implemented, verified, and recorded. Subordinate to [PROJECT_INTENT.md](PROJECT_INTENT.md) and [ARCHITECTURE_RULES.md](ARCHITECTURE_RULES.md).

Lifecycle coordination (discovery → decision → work item → closeout): [WORKFLOW_LIFECYCLE.md](WORKFLOW_LIFECYCLE.md). GitHub work inventory: [GITHUB_WORK_TRACKING.md](GITHUB_WORK_TRACKING.md). Documentation closeout: [DOCUMENTATION_CLOSEOUT.md](DOCUMENTATION_CLOSEOUT.md).

Cursor rule (detail): `.cursor/rules/OpenSpec-Workflow.mdc`. Runtime: [PROJECT_STATE.md](../Runtime/PROJECT_STATE.md), [CURRENT_WORK.md](../Runtime/CURRENT_WORK.md), [ROADMAP.md](../Runtime/ROADMAP.md).

---

## Work types

| Type | Source of truth | Completion signal |
|------|-----------------|-------------------|
| **Timeline / data model** | OpenSpec `openspec/changes/<name>/` → archive to `openspec/specs/` | `/opsx:archive` + native tests + HITL when specified |
| **Feature / phase design** | `docs/Plans/*.md` handoffs and `FEATURE_PLANS.md` | Tasks in plan or OpenSpec `tasks.md` marked done |
| **Bugfix / refinement** | `docs/Plans/*_refinement.md` or OpenSpec BUG specs | Summary updated; regression test if applicable |
| **Shipped vs next overview** | [DELIVERABLE_TRACKING.md](../DELIVERABLE_TRACKING.md) | Row state updated when firmware ships |

Historical `docs/Plans/*.plan.md` exports are **context** — they do not override OpenSpec or architecture rules.

---

## OpenSpec setup

| Item | Location / command |
|------|-------------------|
| CLI | Node.js **≥ 20** (`nvm use 20`), `openspec --version` |
| Project config | `openspec/config.yaml` |
| Archived requirements | `openspec/specs/` (after `/opsx:archive`) |
| Active work | `openspec/changes/<name>/` (proposal, design, specs, `tasks.md`) |
| Navigation | [docs/Plans/openspec_integration_overview.md](../Plans/openspec_integration_overview.md) |
| Slash commands | Restart Cursor after `openspec init` / `openspec update` |

### Cursor commands

| Command | Use |
|---------|-----|
| `/opsx:propose <idea>` | New change — proposal, specs, design, tasks |
| `/opsx:explore` | Spike / investigate before proposing |
| `/opsx:apply` | Implement `tasks.md` for the active change |
| `/opsx:sync` | Reconcile specs with implementation drift |
| `/opsx:archive` | Merge delta specs into `openspec/specs/`, archive change folder |

### Agent procedure for OpenSpec work

1. Read [PROJECT_STATE.md](../Runtime/PROJECT_STATE.md) and [CURRENT_WORK.md](../Runtime/CURRENT_WORK.md) for **active scope** (not ROADMAP alone).
2. Open `openspec/changes/<name>/tasks.md` for the change you are implementing.
3. Cite brownfield docs in proposals; do **not** duplicate normative text (see below).
4. Implement via `/opsx:apply` or equivalent manual task completion.
5. Verify per domain (native + HITL matrix below).
6. `/opsx:sync` if spec drifted, then `/opsx:archive` when gates pass.
7. Update `PROJECT_STATE.md`, `CURRENT_WORK.md`, `DELIVERABLE_TRACKING.md`, and checked-off `tasks.md`.

---

## Locked milestone sequence (timeline track)

M1–M7 are **shipped**. Post-M7 order is fixed — do not reorder without explicit project decision:

| Order | Item | Notes |
|-------|------|--------|
| Done | **M8 edit** — Edit op-lists, NoteEditSession | Archived to `openspec/specs/` |
| Done | **pool-budget** groups 1–6 + session editRows undo (§9) | Spec `note-edit-session-undo` |
| **Now** | Persistence / overlay / display polish | Active: `set-revision-persistence`, `workspace-session-persistence`, overlay/display changes |
| Next | **Hardening** — playback-window polish | Propose when persistence track stable |
| Then | **JamRecorder** — JamAction / D3 | **Not** D13 MIDI capture |
| Then | **M10** — Jam entity + Scenes + SD | After jam-recorder |
| Last | **D13 arrangement capture** | Only after R1–R5 locked in [phase-3-multi-loop.md](../Plans/phase-3-multi-loop.md) |

### Hard guards (attention)

| Guard | Action |
|-------|--------|
| **Do not start D13-style `jam-recording`** before JamRecorder + M10 path | Parked ref: `openspec/changes/archive/20260617-parked-jam-recording-d13/` |
| **Do not treat Phase 3 jam capture as shipped** | Slots infrastructure yes; capture no — see [DELIVERABLE_TRACKING.md](../DELIVERABLE_TRACKING.md) |
| **Parked changes** | Read `PARKED.md` in change folder before reviving (e.g. `currentset-savedset-storage-layout`, `edit-record-display-length-mode` archive copy) |
| **Reopened bugs** | `edit-record-display-length-mode` — spike RC1–RC4 in `openspec/changes/archive/2026-06-24-edit-record-display-length-mode/BUG.md` |

### Active changes (check PROJECT_STATE + CURRENT_WORK first)

As of 2026-08-14, Layer A persistence change:

- `loop-content-history-persistence` (DEC-035)

Older non-archived folders with `tasks.md` (check PROJECT_STATE + CURRENT_WORK first):

- `set-revision-persistence`
- `workspace-session-persistence`
- `load-save-overlay-display-regression`
- `save-status-display`
- `long-loop-piano-roll-window`

Parked: `currentset-savedset-storage-layout`

---

## Brownfield docs (cite, do not duplicate)

When writing OpenSpec proposals, designs, or agent plans, **link** these instead of copying sections wholesale:

| Doc | Use for |
|-----|---------|
| `~/.cursor/plans/timeline_data_model_refactor_9cbaeb60.plan.md` | M1–M10 milestone order |
| [DELIVERABLE_TRACKING.md](../DELIVERABLE_TRACKING.md) | Shipped vs next |
| [Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) | Capture/stop/undo constraints |
| [Guides/DEFERRED_RUNTIME_PERSISTENCE.md](../Guides/DEFERRED_RUNTIME_PERSISTENCE.md) | Deferred save FSM |
| [plans/phase-3-multi-loop.md](../Plans/phase-3-multi-loop.md) | Jam requirements (D13–D15 deferred) |
| [ARCHITECTURE_RULES.md](ARCHITECTURE_RULES.md) | Ownership and forbidden patterns |

Normative SHALL/MUST requirements belong in `openspec/specs/` after archive — not in `docs/Plans/` alone.

---

## Naming (OpenSpec + code)

Follow [NAMING.md](NAMING.md) and global **action + scope** naming.

Reuse domain terms in specs and tasks: **passes**, **Capture**, **editPass**, **NoteEditSession**, slot, overdub, jam, **JamAction**, HITL baseline.

Do not introduce new top-level domain nouns without user approval (see architecture rules).

---

## Verification gates

| Gate | When | Command / doc |
|------|------|---------------|
| **Native unit tests** | Before push, merge, or PR | `pio test -e native` |
| **Firmware build** | After firmware changes | `pio run -e teensy41-capture-serial` |
| **Teensy upload** | Only after user confirms | `pio run -e teensy41-capture-serial -t upload` |
| **HITL baseline** | Record/overdub/undo regressions | `.cursor/rules/HITL-Test-Flow.mdc` |
| **HITL edit** | Note edit / overlap regressions | `.cursor/rules/HITL-Edit-Test-Flow.mdc` |

### Verification by change domain

| Domain | Typical gates |
|--------|----------------|
| Timeline / capture / undo | `pio test -e native` (`test_loop_event_store`, `test_loop_stop_finalize`, …) + HITL baseline |
| Note edit / overlap | Native `test_edit_apply` + HITL edit baseline; child BUG specs in archive when applicable |
| Persistence / SD / overlay | Native persistence tests + manual overlay flow; deferred save HITL when touching save path |
| Display-only | Build compile-check; HITL when behavior couples to record/edit timing |
| Long record / memory | HITL 48/64 record scenarios per `long-record-memory-headroom` spec |

Upload to Teensy **only** when user confirms after a successful firmware build.

---

## Key archived specs (reference)

Agents debugging timeline behavior should read `openspec/specs/` before plans:

| Spec area | Folder (under `openspec/specs/`) |
|-----------|-----------------------------------|
| Capture / stop / undo epochs | `timeline-epochs/` |
| passes[] model | `timeline-passes/` |
| Overlap / note edit session | `note-edit-modification-session/`, `note-edit-session-undo/` |
| Change length commit | `change-length-commit-rematerialize/` |
| Long record / deferred save | `long-record-memory-headroom/` |
| Multi-loop slots | `multi-loop-slots/` |

Full archive index: `openspec/changes/archive/`.

---

## Agent session hygiene

After each implementation session, update:

1. [docs/Runtime/PROJECT_STATE.md](../Runtime/PROJECT_STATE.md) — branch, active OpenSpec, constraints
2. [docs/Runtime/CURRENT_WORK.md](../Runtime/CURRENT_WORK.md) — now implementing / not implementing / completion
2. [docs/DECISION_LOG.md](../DECISION_LOG.md) — **append** structured `DEC-###` entry if design was discussed ([SESSION_CLOSEOUT.md](../Templates/SESSION_CLOSEOUT.md))
3. [DELIVERABLE_TRACKING.md](../DELIVERABLE_TRACKING.md) — when something ships or scope changes
4. OpenSpec `tasks.md` — check off completed items
5. Preflight artifact — if the session started from [PREFLIGHT.md](../Templates/PREFLIGHT.md), note outcome in commit or PR

Before closing a design-heavy chat without merging: run session closeout so exclusions are not lost in the thread.

---

## Documentation placement

| Content | Location |
|---------|----------|
| Authority (why, architecture, delivery) | `docs/Authority/` |
| Current behavior | `docs/Guides/` |
| Implementation logs | `docs/Plans/archive/refinements/` (historical); `docs/Refinements/README.md` index |
| Design exports and handoffs | `docs/Plans/` |
| Normative requirements (accepted) | `openspec/specs/` |
| Session runtime | `docs/Runtime/` — PROJECT_STATE, CURRENT_WORK, ROADMAP |

After any logic/behavior change: [DOCUMENTATION_CLOSEOUT.md](DOCUMENTATION_CLOSEOUT.md) — choose the owning artifact; plans are not authority.
