# OpenSpec integration overview

OpenSpec drives **timeline post-M7** work in this repo. Commands and rules:
`.cursor/rules/OpenSpec-Workflow.mdc`.

## Layout

```
openspec/
├── config.yaml              # Project context for AI artifacts
├── specs/                   # Source of truth (archived requirements)
│   ├── timeline-epochs/     # M1–M7 shipped epoch model (→ timeline-takes after M8)
│   └── multi-loop-slots/    # 8 slots per track (shipped)
└── changes/
    ├── m8-rename/           # ACTIVE — vocabulary rename (apply first)
    ├── m8-edit/             # Edit op-lists + NoteEditSession (after rename)
    └── archive/
        └── 20260617-parked-jam-recording-d13/   # D13 deferred (see PARKED.md)
```

## Deliverable sequence (authoritative)

From `timeline_data_model_refactor_9cbaeb60.plan.md` §1:

| Order | Item | OpenSpec |
|-------|------|----------|
| Done | M1–M7 epoch cutover | Baseline in `openspec/specs/timeline-epochs/` |
| **Next** | **M8 rename** — Take / Capture / TakeCommitted | **`m8-rename`** → `/opsx:apply` |
| Then | **M8 edit** — Edit op-lists, NoteEditSession | **`m8-edit`** (blocked on rename) |
| Then | Pool / playback hardening | Propose `pool-budget` when M8 ships |
| Then | JamRecorder prototype (JamAction) | Propose `jam-recorder` (not parked D13 folder) |
| Later | M10 Jam entity + Scenes | New change after jam-recorder |
| Last | D13 arrangement MIDI capture | After R1–R5 resolved |

**Parked:** `archive/20260617-parked-jam-recording-d13/` — Phase 3 D13 was started too
early; timeline plan uses JamRecorder + actions first.

## Getting started in Cursor

1. `nvm use 20`
2. Restart Cursor (slash commands in `.cursor/commands/opsx-*.md`)
3. Read `openspec/changes/m8-rename/` (proposal → design → tasks)
4. `/opsx:apply` to implement **rename first**
5. After rename merges: `openspec/changes/m8-edit/` → `/opsx:apply`
6. `/opsx:archive` when native + smoke gates pass

## Relationship to `docs/plans/`

| Layer | Role |
|-------|------|
| Timeline plan (`~/.cursor/plans/timeline_data_model_refactor_*.plan.md`) | Milestone order M1–M10 |
| `docs/plans/phase-3-multi-loop.md` | Brownfield jam requirements (D13–D15) |
| `docs/DELIVERABLE_TRACKING.md` | Shipped vs next overview |
| `openspec/specs/` | Normative SHALL/MUST specs |
| `openspec/changes/m8-rename/` | Active rename tasks (first) |
| `openspec/changes/m8-edit/` | Edit model tasks (second) |

## CLI maintenance

```bash
nvm use 20
npm install -g @fission-ai/openspec@latest
openspec update --tools
openspec list
openspec validate --all
```
