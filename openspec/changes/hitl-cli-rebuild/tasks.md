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

## Phase 3 — Port remainder + delete legacy

- [x] 3.1 Port `two_overdub_undo_redo`, `edit_overdub_during_note_edit`, `note_edit_select_dependent_faders`, `current_set_incremental_save` (layered registry + bridge)
- [ ] 3.2 Wire `revision_*`, `load_save_*`, `fader_motor_*` to new contexts
- [ ] 3.3 Delete legacy monoliths + shims; drop `edit_full` preset
- [ ] 3.4 Protocol regex grep gate passes

## Phase 4 — Capture bundles + long-lived docs

- [ ] 4.1 Capture bundles (`schema_version`, immutable corpus)
- [ ] 4.2 `HITL_DEVELOPER_GUIDE.md`, `HITL_REGRESSION_WORKFLOW.md`
- [ ] 4.3 Update `HITL_TEST_SCENARIOS.md`; slim `HITL-Test-Flow.mdc`
- [ ] 4.4 `baseline_loop_inventory.py` corpus index; bundle writer tests

## Phase 5 — Cleanup + archive

- [ ] 5.1 Remove superseded helpers (`midi_io`, `edit_controls`, `capture_transitions`)
- [ ] 5.2 Dependency audit; import gates green
- [ ] 5.3 Archive OpenSpec change + migration plan
- [ ] 5.4 Update `CURRENT_WORK.md` / `PROJECT_STATE.md`
