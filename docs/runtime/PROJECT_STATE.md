# Project state (execution context)

**Agents: load first** with [CURRENT_WORK.md](CURRENT_WORK.md). Overwrite frequently — **operational only**, no future milestones (those live in [ROADMAP.md](ROADMAP.md)).

Last updated: 2026-07-09 (slot boot + split focus landed uncommitted)

---

## Current branch

`dec-023-recovery` — slot boot/split focus + playhead-after-undo (uncommitted)

## Shipped on branch (recent)

| Commit / session | Focus | Docs |
|--------|--------|------|
| *(uncommitted)* | Slot boot exhaustive queue + split focus (DEC-021 amend, DEC-025) | DECISION_LOG, Loops.md |
| *(uncommitted)* | Boot USB-host defer; boot-load LED suppress; slot-switch crash hardening | — |
| *(uncommitted)* | Phase 2 persistence queue + Phase 3 transport-gate removal | DEC-020 |
| *(uncommitted)* | M5 boot load restore (DEC-021/022) recovered | DEC-021, DEC-022 |
| `cdd9c2b` | 16-bar stop→PLAY fix; capture ring; playback extmem materialize | DEC-018 |
| *(prior)* | Phase A step 3 + Phase B — one materialize per revision | handoff |
| `d635296` | Partial Phase A — PLAYING defer, REVT bounds; boot + 16-bar smoke | handoff |
| `eea4734` | Serial evidence index (archived captures) | plan |
| `894d2ea` | Runtime architecture docs + DEC-016 | Architecture/ |
| `d05e736` | Display boot/play OLED freeze | — |
| `b1260ce` | Cold buffers → external memory pool | INTERNAL_HEAP guide |

## Active OpenSpec

| Change | Focus |
|--------|--------|
| **`continuous-runtime-persistence`** | Phase 0–4 shipped (native); Phase 5 recovery **next**; Phase 4 HITL gate pending |
| **`runtime-derived-representation-heap`** | M5 Steps 1–1b–2 shipped (`adoptPersistedSnapshot`); lazy load + 64+64 HITL pending |
| **`slot-selection-focus`** | **Shipped (firmware)** — orchestrator + footer extension; manual §8 pending; [`slot_selection_focus_implementation_handoff.md`](../plans/slot_selection_focus_implementation_handoff.md) |
| **`unified-interval-projection`** | Phases 1–4 shipped; native PASS; **5.5 HITL deferred** (DEC-017) until runtime Phase A→C |
| **`edit-session-action-geometry`** | **Paused** — derived overlap pipeline; blocked until UIP Phases 1–5 |
| `set-revision-persistence` | Set revisions, overlay browser — core shipped; loop picker **4.8–4.10** paused |
| `workspace-session-persistence` | Current workspace session model |
| `load-save-overlay-display-regression` | Overlay display fixes |
| `save-status-display` | Deferred save status on display |
| `long-loop-piano-roll-window` | 16-bar piano-roll window + overview; future `Timeline` projection consumer |
| `note-edit-tick-coordinates-and-audition` | Geometry wrap regression: tick spaces + session-store playback preview |
| `linear-loop-tick-storage` | Canonical linear storage + projection alias — prerequisite for UIP |

**Side fix (2026-07-03):** `note-edit-tick-coordinates-and-audition` — long-loop pitch edit regression at loop wrap; handoff [note_edit_geometry_wrap_regression_bugfix.md](../plans/note_edit_geometry_wrap_regression_bugfix.md). HITL pending.

**Archived (2026-07-03):** `note-edit-fader-feedback-regression` → `openspec/changes/archive/2026-07-03-note-edit-fader-feedback-regression/`; normative spec `openspec/specs/note-edit-fader-feedback/`. Phase 12–13 + RC11 §8.3–8.4 deferred.

**Archived (2026-07-03):** `note-edit-stable-note-id` → `openspec/changes/archive/2026-07-03-note-edit-stable-note-id/`; normative spec `openspec/specs/note-edit-stable-note-id/`.

**Handoffs (shipped):** [note_edit_fader_feedback_next_steps_handoff.md](../plans/note_edit_fader_feedback_next_steps_handoff.md), [note_edit_stable_note_id_phase_a_handoff.md](../plans/note_edit_stable_note_id_phase_a_handoff.md)

Parked (not active): `currentset-savedset-storage-layout`

## Current constraints

- Record/overdub stop: no full validate; deferred SD save only
- Default build env: `teensy41-capture-serial`; upload only after user confirms
- Base module (encoder + 4 GPIO) capable in principle; DROID is extension only
- Governance docs do not change firmware by themselves
- Do not implement from [ROADMAP.md](ROADMAP.md) — scope is [CURRENT_WORK.md](CURRENT_WORK.md) only
- **`edit-session-action-geometry` firmware blocked** until `unified-interval-projection` Phases 1–5 complete

## Current blockers

- `edit-session-action-geometry` → blocked by `unified-interval-projection`
- `currentset-savedset-storage-layout` parked until revision model stable — see CURRENT_WORK

## Accepted decisions (summary)

Full log: [DECISION_LOG.md](../DECISION_LOG.md).

- Authority: `docs/00-authority/` → OpenSpec → Guides → plans → code
- Gesture-first; five gestures per button
- passes[] per slot (v4+); NoteEditSession for live edit RAM
- Current auto-save; Save appends Set revision without clearing Current
- Overlay confirm: Edit short + encoder short only (DEC-001)
- Persistence RAM model: `StorageSession` on `StorageManager` — request/held/dispatched revision load (DEC-012, **shipped**)

## Current architecture notes

- Persistence owner: `StorageManager` — no parallel save Manager without reassessment (DEC-008)
- **`StorageSession`** (DEC-012): job RAM in `Internal.cpp`; overlay in `Overlay.cpp`; FSM in `WorkspaceSave.cpp`, `RevisionCommit.cpp`, `RevisionLoad.cpp`; orchestrator in `StorageManager.cpp`
- HITL serial line buffer in RAM (FLASHMEM `processHitlSerialCommands` cannot access DMAMEM on IMXRT1062)
- **Capture-serial RAM budget (2026-07-04):** note-edit overlap code in FLASHMEM (`NoteEditMem.h`); deferred `#DBG`/`REVT`/`PERF` via 96 KB PSRAM ring in `DebugSessionCapture` — see [capture_serial_ram1_recovery_extmem_debug_enhancement.md](../plans/capture_serial_ram1_recovery_extmem_debug_enhancement.md)
- Normative specs: `openspec/specs/revision-load/`, `storage-session-jobs/`, `storage-session-layout/`
- **Next persistence layout:** `transport.bin` / `global.bin` — not started (post DEC-012)
- **Interval projection:** `IntervalProjection` engine owns all wrap math — **`include/Utils/IntervalProjection.h`** + **`src/Utils/IntervalProjection.cpp`**; Phases 1–5.1–5.4 + 5.6 shipped (grep gate on D7 consumers); `Track.projectionCycleStartTick` + queued start at grid; `PlaybackCursor` retired; **5.5 HITL** pending
- `ButtonManager` / GPIO dormant (DEC-005)
- M8 edit + pool-budget archived to `openspec/specs/`
