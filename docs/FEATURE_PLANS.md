# Feature plans — conventions and pointers

**Not the implementation queue.** For what to build **now**, use [`Runtime/CURRENT_WORK.md`](Runtime/CURRENT_WORK.md). For deliverable-level shipped vs next, use [`DELIVERABLE_TRACKING.md`](DELIVERABLE_TRACKING.md).

| Document | Role |
|----------|------|
| [`CURRENT_WORK.md`](Runtime/CURRENT_WORK.md) | **Sole implementation priority** — now / not now |
| [`DELIVERABLE_TRACKING.md`](DELIVERABLE_TRACKING.md) | Shipped vs next at deliverable level (not a second queue) |
| [`FEATURES.md`](FEATURES.md) | Technical capability checklist (reference — verify against Guides) |
| **This file** | Plan naming conventions; Phase 3 / multi-loop pointer |
| [`Plans/README.md`](Plans/README.md) | Design history conventions; archive policy |
| [`README.md`](README.md) | Documentation index (Guides, Plans, Authority, Runtime) |

**Cursor-generated `.plan.md` files** are archived under [`docs/Plans/archive/cursor-exports/`](Plans/archive/cursor-exports/). **Phase 3 (multi-loop) requirements:** [Plans/phase-3-multi-loop.md](Plans/phase-3-multi-loop.md) — **Scope 1:** 8 loops per track; save/data design still anticipates **more slots** and **large per-loop storage**. Copy from `~/.cursor/plans/` when you add new exports.

## Conventions

- One topic per file. Prefer descriptive names (kebab-case for new files; older files keep their historical names).
- When a plan is superseded, **keep the old file** and add a new one or append a dated section at the top (e.g. `## 2025-03-14 — revision`).
- For full Cursor plan exports, prefer committing copies under **`docs/Plans/`** (see [Plans/README.md](Plans/README.md)).
- New **Guides**: add under `docs/Guides/` and a row in [README.md](README.md). New **refinement / implementation logs**: active work in `docs/Plans/`; historical in `docs/Plans/archive/refinements/`.
