# Design plans (`docs/Plans/`)

**Not implementation authority.** Historical and proposed design work lives here. For what to build **now**, use [`Runtime/CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md) only. For deliverable-level shipped vs next, see [`DELIVERABLE_TRACKING.md`](../DELIVERABLE_TRACKING.md) (not a second work queue).

Authority order: [`Authority/README.md`](../Authority/README.md) → OpenSpec → [`Guides/`](../Guides/) → this folder → code.

---

## Conventions

| Pattern | Meaning |
|---------|---------|
| `*_bugfix.md` | Root-cause fix with capture evidence |
| `*_refinement.md` | Scoped refactor or enhancement |
| `*_handoff.md` | Session handoff (may be superseded) |
| `*_enhancement.md` | Larger feature or tooling work |
| `*.plan.md` | Cursor export from `~/.cursor/plans/` |

Add `Status: Active | Done | FROZEN` at the top when a plan's lifecycle matters. Do not bulk-edit old files merely to add status.

**New human-written plans:** `docs/Plans/<topic>_<kind>.md` (see [FEATURE_PLANS.md](../FEATURE_PLANS.md)).

**Cursor exports:** `cp ~/.cursor/plans/*.plan.md docs/Plans/` when you want them versioned.

---

## Archive policy (Phase 2)

When a plan is no longer active, `git mv` into `docs/Plans/archive/`:

| Subfolder | Contents |
|-----------|----------|
| `cursor-exports/` | Historical `*.plan.md` (43 archived 2026-08-08) |
| `handoff/` | Superseded `*_handoff.md` (10 archived 2026-08-08) |
| `bugfix/` | `Status: FROZEN` bugfixes (2 archived 2026-08-08) |
| `refinements/` | Historical `docs/Refinements/` logs (9 archived 2026-08-08) |

**Retained at `Plans/` root (live references):** `multi-loop_leds_and_droid_lfo_3a62f325.plan.md`, `dual-tick_view_override_architecture_856310b1.plan.md`, `exclude_led_channels_from_all_notes_off_1fca1ecd.plan.md`, `reduce_undo_and_lazy_loop_b89758a6.plan.md`.

Root rule: keep a plan at `docs/Plans/` only if `CURRENT_WORK.md` points to it, or it is an explicitly active near-term effort. See [`docs_folder_hygiene_refinement.md`](docs_folder_hygiene_refinement.md).

---

## Cross-cutting pointers

| Document | Role |
|----------|------|
| [phase-3-multi-loop.md](phase-3-multi-loop.md) | Phase 3 multi-loop requirements (jam capture not shipped) |
| [loop_layer_history_persistence_architecture.md](loop_layer_history_persistence_architecture.md) | DEC-035 — Loop persists content only; Layer A next |
| [runtime_scheduling_admission_model_architecture.md](runtime_scheduling_admission_model_architecture.md) | Runtime timing-telemetry contract (interval reservation deferred) |
| [runtime_scheduling_owner_boundary_admission_refinement.md](runtime_scheduling_owner_boundary_admission_refinement.md) | Owner-Boundary Gate roadmap (O–T–R–C–A–P) |
| [runtime_scheduler_lcr_consumer_grooming_refinement.md](runtime_scheduler_lcr_consumer_grooming_refinement.md) | Consumer rule + A/B/C stalkers; Slice 1 rem split, Slice 2 idle one-source (wrap-held edge append) |
| [refactor_priority_backlog.md](refactor_priority_backlog.md) | Cross-cutting refactor priority (P1/P2/P3) |
| [codebase_hygiene_technical_debt_review.md](codebase_hygiene_technical_debt_review.md) | Hygiene backlog + item 18 archive remainder |
| [docs_folder_hygiene_refinement.md](docs_folder_hygiene_refinement.md) | Docs folder cleanup plan |

Living guides and refinements index: [Documentation index](../README.md).
