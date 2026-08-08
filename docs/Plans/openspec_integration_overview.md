# OpenSpec integration overview

Navigation map for OpenSpec in this repo. **Not the live work queue** — check [`docs/Runtime/PROJECT_STATE.md`](../Runtime/PROJECT_STATE.md) each session.

| Need | Go to |
|------|--------|
| Active changes right now | [CURRENT_WORK.md](../Runtime/CURRENT_WORK.md) + [PROJECT_STATE.md](../Runtime/PROJECT_STATE.md) |
| Future sequencing | [ROADMAP.md](../Runtime/ROADMAP.md) (informational only) |
| Process, guards, verification | [DELIVERY_RULES.md](../Authority/DELIVERY_RULES.md) |
| Cursor always-on rules | `.cursor/rules/OpenSpec-Workflow.mdc`, `.cursor/rules/Agent-Context-Workflow.mdc` |
| Authority order | [Authority/README.md](../Authority/README.md) |

OpenSpec drives **timeline post-M7** work plus **persistence / overlay** tracks on the current branch. Slash commands: `.cursor/commands/opsx-*.md` (restart Cursor after `openspec init` / `openspec update`).

---

## Layout

```
openspec/
├── config.yaml                 # Project context for AI artifacts
├── specs/                      # Normative requirements (archived behavior)
│   ├── timeline-epochs/        # Capture/stop/undo epochs
│   ├── timeline-passes/        # passes[] — recordPass, overdubPass, editPass
│   ├── note-edit-modification-session/
│   ├── note-edit-session-undo/
│   ├── note-edit-session-state/
│   ├── change-length-commit-rematerialize/
│   ├── overlap-hidden-note-select/
│   ├── long-record-memory-headroom/
│   ├── internal-heap-external-memory-routing/  # NOTE_EDIT cold buffers, split-tier undo admission (2026-07)
│   ├── note-edit-fader-feedback/               # DROID motor sync when kNoteEditFaderFeedbackEnabled
│   ├── storage-loop-io/
│   ├── loop-event-pool-admission/
│   ├── pass-reclaim/
│   ├── undo-memory-trim/
│   ├── playback-runtime-prewarm/
│   ├── capture-state-guards/
│   ├── multi-loop-slots/
│   ├── loop-temporal-persistence/
│   └── hitl-automation/
└── changes/
    ├── set-revision-persistence/          # ACTIVE — Set revisions, overlay, REVPK
    ├── workspace-session-persistence/     # ACTIVE — Current workspace session
    ├── load-save-overlay-display-regression/
    ├── save-status-display/
    ├── long-loop-piano-roll-window/
    ├── currentset-savedset-storage-layout/  # PARKED — see PARKED.md
    └── archive/                           # Shipped or superseded changes
        ├── 2026-06-24-m8-edit/
        ├── 2026-06-22-pool-budget/
        ├── 2026-06-22-loop-ownership-hardening/
        ├── 2026-06-23-long-record-memory-headroom/
        ├── 2026-06-24-edit-record-display-length-mode/
        ├── 2026-06-20-timeline-pass-model/
        └── …                              # full list: ls openspec/changes/archive/
```

**Rule:** Implement from `openspec/changes/<name>/tasks.md`. After gates pass: `/opsx:archive` → requirements move into `openspec/specs/`.

---

## Active changes (Jun 2026)

Refresh via `ls openspec/changes/` (exclude `archive/`) and [PROJECT_STATE.md](../Runtime/PROJECT_STATE.md).

| Change | Focus | Handoff / notes |
|--------|--------|-----------------|
| **`set-revision-persistence`** | Set revisions (REVPK), overlay browser, revision commit/load, recovery boot | [`set_revision_persistence_handoff.md`](set_revision_persistence_handoff.md) |
| **`workspace-session-persistence`** | Current workspace session model, CurrentSet persistence | [`workspace_session_persistence_handoff.md`](workspace_session_persistence_handoff.md) |
| `load-save-overlay-display-regression` | Overlay display regressions | — |
| `save-status-display` | Deferred save status on OLED | [`DEFERRED_RUNTIME_PERSISTENCE.md`](../Guides/DEFERRED_RUNTIME_PERSISTENCE.md) |
| `long-loop-piano-roll-window` | 16-bar detailed window + overview strip | [`long_loop_piano_roll_overview_enhancement.md`](long_loop_piano_roll_overview_enhancement.md) |

**Parked:** `currentset-savedset-storage-layout` — superseded by revision model; read `PARKED.md` before reviving.

**Recommended apply order (persistence track):** `set-revision-persistence` tasks → `workspace-session-persistence` → overlay/display fixes → archive when native (+ manual overlay) gates pass.

---

## Deliverable sequence (timeline — locked)

From `~/.cursor/plans/timeline_data_model_refactor_9cbaeb60.plan.md` §1. Do not reorder without project decision. Detail: [DELIVERY_RULES.md](../Authority/DELIVERY_RULES.md).

| Order | Item | OpenSpec / status |
|-------|------|-------------------|
| Done | M1–M7 epoch cutover | `openspec/specs/timeline-epochs/` |
| Done | **timeline-pass-model** — passes[] | archive `2026-06-20-timeline-pass-model` → `timeline-passes/` |
| Done | **M8 edit** — NoteEditSession, edit op-lists | archive `2026-06-24-m8-edit` |
| Done | **note-edit-modification-session** — overlap A1 + B1 | archive → spec |
| Done | **note-edit-focus-reads**, **change-length-commit-rematerialize** | archived → specs |
| Done | **pool-budget** — admission, reclaim, session editRows undo (§9) | archive `2026-06-22-pool-budget` → `note-edit-session-undo/`, pool specs |
| Done | **loop-ownership-hardening**, **long-record-memory-headroom** | archived → specs |
| **Now** | **Persistence / overlay / display** | Active changes table above |
| Next | **Hardening** — playback-window polish | Propose when persistence track archives |
| Then | **JamRecorder** — JamAction / D3 | Propose `jam-recorder` — **not** D13 MIDI capture |
| Then | **M10** — Jam entity + Scenes + SD | New change after jam-recorder |
| Last | **D13 arrangement capture** | After R1–R5 in [phase-3-multi-loop.md](phase-3-multi-loop.md) |

### Hard guards

| Guard | Reference |
|-------|-----------|
| Do **not** start D13-style `jam-recording` before JamRecorder + M10 | `openspec/changes/archive/20260617-parked-jam-recording-d13/` |
| Phase 3 jam **capture** is not shipped | [DELIVERABLE_TRACKING.md](../DELIVERABLE_TRACKING.md) |
| **edit-record-display-length-mode** reopened — spike in archive `BUG.md` | archive `2026-06-24-edit-record-display-length-mode/` |

---

## Getting started in Cursor

1. `nvm use 20` — `openspec --version`
2. Read [PROJECT_STATE.md](../Runtime/PROJECT_STATE.md) and [CURRENT_WORK.md](../Runtime/CURRENT_WORK.md) — pick the active change
3. Open `openspec/changes/<name>/` (proposal → design → `tasks.md`)
4. Load brownfield guides cited in the change — **cite, do not duplicate** (see below)
5. `/opsx:apply` or implement `tasks.md` manually
6. `pio test -e native`; HITL when the change touches capture/edit/undo (see DELIVERY_RULES verification matrix)
7. `/opsx:sync` if spec drifted; `/opsx:archive` when gates pass
8. Update PROJECT_STATE, CURRENT_WORK, DELIVERABLE_TRACKING, and checked-off tasks

---

## Relationship to `docs/`

| Layer | Role |
|-------|------|
| [Authority/](../Authority/README.md) | Intent → architecture → delivery (wins over plans) |
| [PROJECT_STATE.md](../Runtime/PROJECT_STATE.md) | Active OpenSpec, constraints |
| [CURRENT_WORK.md](../Runtime/CURRENT_WORK.md) | Implementation scope |
| [ROADMAP.md](../Runtime/ROADMAP.md) | Future milestones (not authority) |
| [DELIVERABLE_TRACKING.md](../DELIVERABLE_TRACKING.md) | Shipped vs next in firmware |
| `openspec/specs/` | Normative SHALL/MUST (after archive) |
| `openspec/changes/<name>/` | Active deltas until archive |
| `docs/Plans/*.md` | Handoffs and design exports — **context**, not authority |
| [LOOP_MIDI_STORAGE_AND_VALIDATION.md](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) | Capture/stop/undo constraints — cite in proposals |
| [DEFERRED_RUNTIME_PERSISTENCE.md](../Guides/DEFERRED_RUNTIME_PERSISTENCE.md) | Deferred save FSM — persistence changes |
| [phase-3-multi-loop.md](phase-3-multi-loop.md) | Jam roadmap (D13–D15 deferred) |
| Timeline plan (`~/.cursor/plans/timeline_data_model_refactor_*.plan.md`) | M1–M10 milestone order |

---

## Normative specs index (`openspec/specs/`)

Use these when debugging behavior — before trusting older `docs/Plans/` exports.

| Spec folder | Topic |
|-------------|--------|
| `timeline-epochs/` | Capture/stop/undo epochs |
| `timeline-passes/` | passes[] materialize / merge |
| `note-edit-modification-session/` | Overlap engine |
| `note-edit-session-undo/` | Session editRows undo (pool-budget §9) |
| `note-edit-session-state/` | NoteEditSessionState geometry |
| `change-length-commit-rematerialize/` | ChangeLength commit path |
| `overlap-hidden-note-select/` | filterSelectableDisplayNotes |
| `long-record-memory-headroom/` | PSRAM-first buffers, deferred save |
| `internal-heap-external-memory-routing/` | NOTE_EDIT cold buffers, baseline map scope, split-tier session undo admission |
| `note-edit-fader-feedback/` | DROID outbound/inbound when `kNoteEditFaderFeedbackEnabled` |
| `storage-loop-io/` | SD v4 loop slot I/O |
| `loop-event-pool-admission/` | Chunk/heap admission |
| `pass-reclaim/` | reclaimUnreferencedDisabledPasses |
| `undo-memory-trim/` | PREFERRED_UNDO_DEPTH trim |
| `playback-runtime-prewarm/` | Slot playback prewarm |
| `capture-state-guards/` | Overdub/slot guards |
| `multi-loop-slots/` | 8 slots per track |
| `loop-temporal-persistence/` | Loop temporal SD fields |
| `hitl-automation/` | Host MIDI HITL scenarios |

---

## CLI maintenance

```bash
nvm use 20
npm install -g @fission-ai/openspec@latest
openspec update --tools
openspec list
openspec validate --all
```

---

## Cursor commands (quick reference)

| Command | Use |
|---------|-----|
| `/opsx:propose <idea>` | New change folder |
| `/opsx:explore` | Spike before proposing |
| `/opsx:apply` | Implement active `tasks.md` |
| `/opsx:sync` | Reconcile spec drift |
| `/opsx:archive` | Merge into `openspec/specs/`, move to `archive/` |
