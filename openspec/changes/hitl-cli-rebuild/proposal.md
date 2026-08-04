## Why

~8.4k lines in [`legacy_record_baseline.py`](../../../scripts/hitl/legacy_record_baseline.py) and [`legacy_edit_baseline.py`](../../../scripts/hitl/legacy_edit_baseline.py), circular imports, and `sys.argv` injection in [`scenarios/base.py`](../../../scripts/hitl/scenarios/base.py) / [`edit_full.py`](../../../scripts/hitl/scenarios/edit_full.py) make HITL hard to extend and unreliable for regression investigation.

Rebuild `host_midi_hitl` as a layered framework with explicit ownership, a regression corpus, and executable scenario specifications. **UIP OpenSpec task 5.5** is the first device gate (`uip_5_5` preset).

## What changes

- Delete legacy monoliths and CLI shims after porting scenarios
- New layers: `actions/`, `flows/`, `verify/`, `serial/protocol.py`, `registry.py`, `runner.py`, `reporting.py`
- Declarative presets compose scenarios (no `--record-only` flags)
- Capture bundles on pass (`schema_version`, immutable corpus)
- Long-lived docs: `HITL_ARCHITECTURE.md`, `HITL_DEVELOPER_GUIDE.md`, `HITL_REGRESSION_WORKFLOW.md`

## Architecture authority

| Phase | Authority |
|-------|-----------|
| During migration | This change + [`docs/plans/hitl_cli_rebuild_enhancement.md`](../../../docs/plans/hitl_cli_rebuild_enhancement.md) (staging Part I) |
| After Phase 1 | [`docs/Architecture/HITL_ARCHITECTURE.md`](../../../docs/Architecture/HITL_ARCHITECTURE.md) |
| After Phase 5 | Archive this change; architecture + guides only |

Major post-migration HITL architecture changes: OpenSpec change + DEC per §12.5 in architecture doc.

## Non-goals

- Legacy compatibility shims
- `edit_full` overlap matrix (deferred to `edit-session-action-geometry`)
- Firmware / `#CAP` contract changes
- `ScenarioRunner`, plugin system, generic execution framework (until concrete need)

## Gates

| Phase | Gate |
|-------|------|
| 1 | `pio test -e native` green; import gates pass |
| 2 | Mode B HITL PASS on preset `uip_5_5`; UIP **5.5** checked off |
| 3 | No `legacy_*` / `host_midi_automation_*` in `scripts/` |
| 4 | Capture bundles + long-lived guides |
| 5 | Migration artifacts archived |

## References

- Migration plan (staging architecture): [`docs/plans/hitl_cli_rebuild_enhancement.md`](../../../docs/plans/hitl_cli_rebuild_enhancement.md)
- Design + doc map: [`design.md`](design.md)
- Tasks: [`tasks.md`](tasks.md)
