## Why

~8.4k lines in [`legacy_record_baseline.py`](../../../scripts/hitl/legacy_record_baseline.py) and [`legacy_edit_baseline.py`](../../../scripts/hitl/legacy_edit_baseline.py), circular imports, and `sys.argv` injection in [`scenarios/base.py`](../../../scripts/hitl/scenarios/base.py) / [`edit_full.py`](../../../scripts/hitl/scenarios/edit_full.py) make HITL hard to extend and unreliable for regression investigation.

Rebuild `host_midi_hitl` as a layered framework with explicit ownership, a regression corpus, and executable scenario specifications. **UIP OpenSpec task 5.5** is the first device gate (`uip_5_5` preset).

## What changes

- Layered foundation + runner (`actions/`, `flows/`, `verify/`, `serial/protocol.py`, registries, reporting)
- **Active layered presets (scope lock):** `base` (`record_overdub`) and `edit_full` only
- Declarative presets compose scenarios (no `--record-only` flags)
- Capture bundles on pass (`schema_version`, immutable corpus) for those presets
- Long-lived docs: `HITL_ARCHITECTURE.md`, `HITL_DEVELOPER_GUIDE.md`, `HITL_REGRESSION_WORKFLOW.md`
- Legacy monolith delete only after `edit_full` no longer depends on `legacy_edit_baseline`

## Architecture authority

| Phase | Authority |
|-------|-----------|
| During migration | This change + [`docs/plans/hitl_cli_rebuild_enhancement.md`](../../../docs/plans/hitl_cli_rebuild_enhancement.md) (staging Part I) |
| After Phase 1 | [`docs/Architecture/HITL_ARCHITECTURE.md`](../../../docs/Architecture/HITL_ARCHITECTURE.md) |
| After Phase 5 | Archive this change; architecture + guides only |

Major post-migration HITL architecture changes: OpenSpec change + DEC per §12.5 in architecture doc.

## Non-goals

- Wiring additional layered presets (`edit_minimal`, `revision_*`, `load_save_*`, `fader_motor_*`, `uip_5_5`, …) in this change
- Dropping the `edit_full` preset
- Immediate delete of `legacy_*` / `host_midi_automation_*` while `edit_full` still bridges to them
- Firmware / `#CAP` contract changes
- `ScenarioRunner`, plugin system, generic execution framework (until concrete need)

**Note:** Deeper overlap-matrix fixture work (live `base` + 2× overdub capture, per-interaction presets) may still land later under `edit-session-action-geometry` D14; this change keeps a working layered `edit_full` bridge.

## Gates

| Phase | Gate |
|-------|------|
| 1 | `pio test -e native` green; import gates pass |
| 2 | Mode B HITL PASS on preset `uip_5_5`; UIP **5.5** checked off (historical) |
| 3 | Layered presets = `base` + `edit_full` only; both PASS on device (Mode B) |
| 4 | Capture bundles + long-lived guides for `base` / `edit_full` |
| 5 | Migration artifacts archived (legacy delete only if no longer required) |

## References

- Migration plan (staging architecture): [`docs/plans/hitl_cli_rebuild_enhancement.md`](../../../docs/plans/hitl_cli_rebuild_enhancement.md)
- Design + doc map: [`design.md`](design.md)
- Tasks: [`tasks.md`](tasks.md)
