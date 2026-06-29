# Project state (execution context)

**Agents: load first** with [CURRENT_WORK.md](CURRENT_WORK.md). Overwrite frequently — **operational only**, no future milestones (those live in [ROADMAP.md](ROADMAP.md)).

Last updated: 2026-06-29 (StorageSession Tier 3 partial)

---

## Current branch

`load-save-sets-loops`

## Active OpenSpec

| Change | Focus |
|--------|--------|
| **`storage-session-state-refactor`** | DEC-012 Tier 3 — FSM TU split, overlay TU, job struct migration, API renames |
| `set-revision-persistence` | Set revisions, overlay browser, revision commit/load |
| `workspace-session-persistence` | Current workspace session model |
| `load-save-overlay-display-regression` | Overlay display fixes |
| `save-status-display` | Deferred save status on display |
| `long-loop-piano-roll-window` | 16-bar piano-roll window + overview |

Parked (not active): `currentset-savedset-storage-layout`

## Current constraints

- Record/overdub stop: no full validate; deferred SD save only
- Default build env: `teensy41-capture-serial`; upload only after user confirms
- Base module (encoder + 4 GPIO) capable in principle; DROID is extension only
- Governance docs do not change firmware by themselves
- Do not implement from [ROADMAP.md](ROADMAP.md) — scope is [CURRENT_WORK.md](CURRENT_WORK.md) only

## Current blockers

- `currentset-savedset-storage-layout` parked until revision model stable — see CURRENT_WORK

## Accepted decisions (summary)

Full log: [DECISION_LOG.md](../DECISION_LOG.md).

- Authority: `docs/00-authority/` → OpenSpec → Guides → plans → code
- Gesture-first; five gestures per button
- passes[] per slot (v4+); NoteEditSession for live edit RAM
- Current auto-save; Save appends Set revision without clearing Current
- Overlay confirm: Edit short + encoder short only (DEC-001)
- Persistence RAM model: `StorageSession` on `StorageManager` — request/held/dispatched revision load (DEC-012)

## Current architecture notes

- Persistence owner: `StorageManager` — no parallel save Manager without reassessment (DEC-008)
- **`StorageSession`** (struct, DEC-012): job RAM in `Internal.cpp`; overlay routing + load request/dirty-prompt dispatch in `Overlay.cpp`; FSM bodies in `WorkspaceSave.cpp`, `RevisionCommit.cpp`, `RevisionLoad.cpp`; orchestrator + boot paths in `StorageManager.cpp`
- `ButtonManager` / GPIO dormant; revive via shared Actions layer, not duplicate MIDI paths (DEC-005)
- M8 edit + pool-budget archived to `openspec/specs/` — not active implementation work
