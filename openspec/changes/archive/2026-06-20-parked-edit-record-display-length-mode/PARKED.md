# Parked — do not apply

**Status:** Parked (2026-06-20)

## Why parked

Display-only and length-mode UX work (live record piano roll, edit sidebar refresh,
**lengthEditingMode** leak / P0 stretch on reselect) is **orthogonal** to the overlap
engine and pass-model track that shipped in June 2026. No D1–D3 tasks were started;
overlap HITL failures on latest baselines are tracked under **Track C** insert/reorder
(deferred from archived **change-length-commit-rematerialize**), not display refresh.

## Prerequisite sequence (suggested revive order)

1. **`m8-edit`** — retire `editFlat_` bridge; stable **NoteEditSession** commit paths
2. **Track C** (optional) — insert/reorder HITL if still failing after M8
3. **This change** — spike D1 serial, then D3 length-mode before D2 display refresh

## To revive

Do not continue this folder as-is without a fresh spike:

1. Re-run tasks **0.1–0.2** (RECORD `#CAP DISP` + length-mode grep on post-overlap reselect)
2. Confirm symptoms still reproduce on current firmware (`teensy41-capture-serial`)
3. `/opsx:apply` on [tasks.md](./tasks.md) — **D3 before D2** per gate note

Artifacts here are kept for reference (proposal, design, delta specs, BUG.md).
