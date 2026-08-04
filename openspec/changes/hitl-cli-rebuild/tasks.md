# Tasks — hitl-cli-rebuild

**Prerequisite:** Read [`design.md`](design.md), [`proposal.md`](proposal.md), [`docs/plans/hitl_cli_rebuild_enhancement.md`](../../../docs/plans/hitl_cli_rebuild_enhancement.md).

## Phase 0 — Inventory + doc scaffold

- [x] 0.1 Legacy helper inventory table in migration plan
- [x] 0.2 `openspec/changes/hitl-cli-rebuild/` — proposal, design, tasks
- [x] 0.3 Documentation map in design.md
- [x] 0.4 `capture_transitions.py` consolidation note in inventory

**Exit:** zero production code changes.

## Phase 1 — Foundation

- [x] 1.1 Extract Part I → `docs/Architecture/HITL_ARCHITECTURE.md`
- [x] 1.2 `HitlConfig`, `bootstrap.py`, `session.py`, contexts, types
- [x] 1.3 `foundation_runner.py` (no `ScenarioRunner`), `reporting.py` skeleton
- [x] 1.4 `layered_registry.py` — `LayeredScenarioSpec`, `PresetSpec`, verifier lookup
- [x] 1.5 `actions/*`, `serial/protocol.py` (ST + RECS)
- [x] 1.6 `flows/record_seed.py` (+ skeleton flows)
- [x] 1.7 `test_hitl_import_gate.py`, `test_hitl_actions.py`, `test_verification_result.py`, `test_serial_protocol.py`, `test_foundation_runner.py`

**Exit:** `pio test -e native` green; Python HITL tests pass; `HITL_ARCHITECTURE.md` canonical. Legacy `runner.py` unchanged until Phase 3.

## Phase 2 — Core scenarios (UIP 5.5 gate)

- [x] 2.1 `record_seed`, `record_overdub`, `edit_minimal`, `long_loop_display_window`, `slot_queued_start`
- [x] 2.2 Presets `base`, `edit_minimal`, `uip_5_5` + `--layered` CLI on `host_midi_hitl`
- [x] 2.3 Mode B HITL PASS on `uip_5_5`; check off UIP **5.5**

## Phase 3 — Layered presets: `base` + `edit_full` only

**Scope lock (2026-08-04):** Active layered presets are **only** `base` (`record_overdub`) and `edit_full`. Do **not** wire additional layered presets in this change.

- [x] 3.1 Trim layered registry/presets to `base` + `edit_full`; park other scenario ports (legacy CLI / non-layered remain available where registered)
- [x] 3.2 Stabilize layered `edit_full` (bridge → `legacy_edit_baseline.run_edit_baseline`; Mode B managed capture; host unit tests for session wiring)
- [ ] 3.3 Device HITL PASS: `--layered --preset base` and `--layered --preset edit_full`
- [ ] 3.4 Protocol regex grep gate for code **owned by** `base` / `edit_full` paths (not a full legacy delete yet)

**Out of scope for this change (re-add later or via other OpenSpec):**

- Layered ports of `revision_*`, `load_save_*`, `fader_motor_*`, `edit_minimal`, `uip_5_5`, `edit_overdub_during_note_edit`, `note_edit_select_dependent_faders`, …
- Deleting `legacy_*` / `host_midi_automation_*` monoliths (blocked until `edit_full` no longer depends on `legacy_edit_baseline`)
- Dropping the `edit_full` preset (kept; full overlap matrix hardening may still use D14 later)

## Phase 4 — Capture bundles + long-lived docs

- [ ] 4.1 Capture bundles (`schema_version`, immutable corpus) for `base` + `edit_full` passes
- [ ] 4.2 `HITL_DEVELOPER_GUIDE.md`, `HITL_REGRESSION_WORKFLOW.md`
- [ ] 4.3 Update `HITL_TEST_SCENARIOS.md` (active layered presets = `base`, `edit_full`); slim `HITL-Test-Flow.mdc`
- [ ] 4.4 `baseline_loop_inventory.py` corpus index; bundle writer tests

## Phase 5 — Cleanup + archive

- [ ] 5.1 Remove superseded helpers only when no longer required by `base` / `edit_full` (`midi_io`, `edit_controls`, `capture_transitions` as applicable)
- [ ] 5.2 Dependency audit; import gates green
- [ ] 5.3 Archive OpenSpec change + migration plan
- [ ] 5.4 Update `CURRENT_WORK.md` / `PROJECT_STATE.md`
