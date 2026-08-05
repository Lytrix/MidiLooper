# Design — hitl-cli-rebuild

**Date:** 2026-08-04  
**Status:** Phase 3 in progress — active layered presets locked to `base` + `edit_full`  
**Architecture (canonical after Phase 1):** [`docs/Architecture/HITL_ARCHITECTURE.md`](../../../docs/Architecture/HITL_ARCHITECTURE.md)  
**Staging copy (until archive):** [`docs/plans/hitl_cli_rebuild_enhancement.md`](../../../docs/plans/hitl_cli_rebuild_enhancement.md) Part I

---

## Documentation map

Split by purpose and lifetime:

```text
docs/Architecture/HITL_ARCHITECTURE.md     long-lived — what the system is
docs/Guides/HITL_DEVELOPER_GUIDE.md        long-lived — how to extend
docs/Guides/HITL_REGRESSION_WORKFLOW.md    long-lived — regression investigation
docs/Guides/HITL_TEST_SCENARIOS.md         long-lived — scenario catalogue

openspec/changes/hitl-cli-rebuild/         temporary — this migration

.cursor/rules/HITL-Test-Flow.mdc           short agent rules + links

docs/plans/hitl_cli_rebuild_enhancement.md  temporary — archive Phase 5
```

| Document | Responsibility |
|----------|----------------|
| `HITL_ARCHITECTURE.md` | Layering, invariants, ownership, corpus format, §12 philosophy |
| `HITL_DEVELOPER_GUIDE.md` | Run HITL, add scenario/flow/action/verifier, checklist |
| `HITL_REGRESSION_WORKFLOW.md` | Known-good comparison, immutable bundles, agent policy |
| `HITL_TEST_SCENARIOS.md` | Presets and scenarios |
| `HITL-Test-Flow.mdc` | Canonical commands; points to docs (no architecture duplication) |

---

## Layering (summary)

```text
CLI → cli.py → bootstrap.py → runner.py → Scenario.run → Flows → Actions → MIDI
verify/ + serial/protocol.py (parallel)
reporting.py → capture bundles
registry.py → ScenarioSpec, PresetSpec, verifier lookup
```

**No `ScenarioRunner`.** `runner.py` calls `spec.run(ctx)` directly.

---

## Core types (summary)

- `ScenarioContext` — no `verify_only` (runner owns execution policy)
- `ScenarioResult` — `observations`, `markers`, `artifacts` (no `exit_code`)
- `VerificationResult` + `VerificationFailure` — actionable diagnostics
- Capture bundle — `schema_version: 1` in `metadata.json` and `report.json`

---

## Helper inventory

Full table: [`docs/plans/hitl_cli_rebuild_enhancement.md`](../../../docs/plans/hitl_cli_rebuild_enhancement.md) § Phase 0.

**`capture_transitions.py` consolidation:** merge into `verify/transitions.py` + `serial/protocol.py` parsers; delete duplicate `_latest_track_state`, `_wait_for_transition_count`, `_wait_for_state_entry_count` after port.

---

## Presets (active — Phase 3 scope lock)

Only these layered presets are in scope for the remainder of this change:

| Preset | Scenarios | Notes |
|--------|-----------|--------|
| `base` | `record_overdub` | Record/overdub baseline regression gate |
| `edit_full` | `edit_full` | Full note-edit overlap suite (still bridged to `legacy_edit_baseline` until extract/delete is safe) |

**Parked / not layered in this change:** `edit_minimal`, `uip_5_5`, `revision_*`, `load_save_*`, `fader_motor_*`, and other Phase 2–3 ports. Re-add as separate work after `base` + `edit_full` are green on device.

Phase 2 still completed `edit_minimal` / `uip_5_5` historically; they are **not** active layered targets going forward under this scope lock.

---

## Delete list (deferred past Phase 3 device gate)

Do **not** delete while `edit_full` still calls `legacy_edit_baseline`:

- `scripts/hitl/legacy_record_baseline.py`
- `scripts/hitl/legacy_edit_baseline.py`
- `scripts/host_midi_automation_baseline.py`
- `scripts/host_midi_automation_edit_baseline.py`
- `scripts/hitl/scenarios/base.py`
- `scripts/hitl/scenarios/edit_full.py`
- `scripts/hitl/scenarios/edit_record_prelude.py`

**Keep** the `edit_full` preset (no longer planned for drop in this change).
