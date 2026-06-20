# OpenSpec integration overview

OpenSpec drives **timeline post-M7** work in this repo. Commands and rules:
`.cursor/rules/OpenSpec-Workflow.mdc`.

## Layout

```
openspec/
├── config.yaml              # Project context for AI artifacts
├── specs/                   # Source of truth (archived requirements)
│   ├── timeline-epochs/     # Capture/stop/undo (updated for passes)
│   ├── timeline-passes/     # passes[] model (timeline-pass-model)
│   ├── overlap-hidden-note-select/  # filterSelectableDisplayNotes (closed)
│   ├── note-edit-modification-session/  # overlap engine A1 + B1
│   └── multi-loop-slots/    # 8 slots per track (shipped)
└── changes/
    ├── m8-edit/             # ACTIVE — Edit op-lists + NoteEditSession
    └── archive/
        ├── 2026-06-18-m8-rename/
        ├── 2026-06-19-overlap-hidden-note-select/
        ├── 2026-06-20-note-edit-hitl-focus-restore/
        ├── 2026-06-20-timeline-pass-model/
        ├── 2026-06-20-m8-pass-vocabulary/
        ├── 2026-06-20-note-edit-modification-session/
        ├── 2026-06-20-note-edit-focus-reads/
        ├── 2026-06-20-edit-focus-selection-drift/
        ├── 2026-06-20-note-move-pitch-overlap-flaky/
        ├── 2026-06-20-lengthen-overlap-neighbor-restore/
        ├── 2026-06-20-change-length-commit-rematerialize/
        ├── 2026-06-20-parked-edit-record-display-length-mode/
        └── 20260617-parked-jam-recording-d13/
```

## Deliverable sequence (authoritative)

From `timeline_data_model_refactor_9cbaeb60.plan.md` §1:

| Order | Item | OpenSpec |
|-------|------|----------|
| Done | M1–M7 epoch cutover | Baseline in `openspec/specs/timeline-epochs/` |
| **Next** | **M8 edit** — Edit op-lists, NoteEditSession | **`m8-edit`** → `/opsx:apply` |
| Done | **timeline-pass-model** — passes[] / recordPass / editPass | archived `2026-06-20-timeline-pass-model` |
| Done | **note-edit-modification-session** — overlap A1 + B1 | archived `2026-06-20-note-edit-modification-session` |
| Done | **note-edit-focus-reads** — focus.last / retire movingNote | archived `2026-06-20-note-edit-focus-reads` |
| Done | **edit-focus-selection-drift** — recheck; superseded by hitl-focus-restore | archived `2026-06-20-edit-focus-selection-drift` |
| Done | **change-length-commit-rematerialize** — Track A+B HITL (`203729`) | archived `2026-06-20-change-length-commit-rematerialize` |
| Parked | **edit-record-display-length-mode** — display + length-mode UX | `archive/2026-06-20-parked-edit-record-display-length-mode/` |
| Then | Pool / playback hardening | Propose `pool-budget` when M8 ships |
| Then | JamRecorder prototype (JamAction) | Propose `jam-recorder` (not parked D13 folder) |
| Later | M10 Jam entity + Scenes | New change after jam-recorder |
| Last | D13 arrangement MIDI capture | After R1–R5 resolved |

**Parked:** `archive/20260617-parked-jam-recording-d13/` — Phase 3 D13 was started too
early; timeline plan uses JamRecorder + actions first.

## Getting started in Cursor

1. `nvm use 20`
2. Restart Cursor (slash commands in `.cursor/commands/opsx-*.md`)
3. Read `openspec/changes/m8-edit/` (proposal → design → tasks)
4. `/opsx:apply` to implement **m8-edit**
5. `/opsx:archive` when native + smoke gates pass

## Relationship to `docs/plans/`

| Layer | Role |
|-------|------|
| Timeline plan (`~/.cursor/plans/timeline_data_model_refactor_*.plan.md`) | Milestone order M1–M10 |
| `docs/plans/phase-3-multi-loop.md` | Brownfield jam requirements (D13–D15) |
| `docs/DELIVERABLE_TRACKING.md` | Shipped vs next overview |
| `openspec/specs/` | Normative SHALL/MUST specs |
| `openspec/changes/m8-edit/` | Active M8 edit tasks |
| `openspec/specs/timeline-passes/` | Normative passes[] requirements |
| `openspec/specs/note-edit-modification-session/` | Overlap engine + pre-commit B1 |
| `openspec/specs/change-length-commit-rematerialize/` | ChangeLength rematerialize Track A+B |
| `openspec/changes/archive/2026-06-20-parked-edit-record-display-length-mode/` | Parked — live record display, length-mode lifecycle |
| `openspec/changes/archive/2026-06-18-m8-rename/` | Archived Take/Capture rename |

## CLI maintenance

```bash
nvm use 20
npm install -g @fission-ai/openspec@latest
openspec update --tools
openspec list
openspec validate --all
```
