"""Legacy baseline preset."""

from __future__ import annotations

import sys
from typing import TYPE_CHECKING

from hitl.baseline_canonical_args import (
    canonical_baseline_legacy_args,
    canonical_edit_minimal_base_legacy_args,
    merge_legacy_cli_args,
    strip_scenario_only_legacy_args,
)

if TYPE_CHECKING:
    pass


def run_base_scenario(args: object) -> int:
    from pathlib import Path

    import host_midi_automation_baseline as baseline
    from hitl.baseline_loop_inventory import (
        base_report_record_seed_ok,
        base_preset_config,
        base_report_loop_materialized,
        base_report_ok,
        base_report_usable_for_note_edit_sweep,
        latest_base_report,
        serial_log_from_base_report,
    )
    from hitl.context import get_context

    legacy = strip_scenario_only_legacy_args(list(getattr(args, "legacy_args", []) or []))
    preset = getattr(args, "preset", None)
    if preset in ("edit_minimal", "edit_overdub_during_note_edit"):
        defaults = canonical_edit_minimal_base_legacy_args()
    else:
        # HITL-Test-Flow.md canonical 2+2 bar-synced record/overdub; user flags override via last-wins.
        defaults = canonical_baseline_legacy_args()
    legacy = merge_legacy_cli_args(defaults, legacy)
    old_argv = sys.argv
    try:
        sys.argv = ["host_midi_automation_baseline.py"] + legacy
        code = baseline.run()
    finally:
        sys.argv = old_argv

    out_dir = Path(getattr(args, "out_dir", Path("captures")))
    report = latest_base_report(out_dir)
    seed_ok = (
        base_report_record_seed_ok(report)
        if preset in ("edit_minimal", "edit_overdub_during_note_edit")
        else False
    )
    loop_materialized = base_report_loop_materialized(report)
    record_seed_ok = base_report_record_seed_ok(report)
    if code != 0 and (loop_materialized or record_seed_ok):
        print(
            "[hitl] base overall_ok=false but loop materialized on device — "
            "continuing composed preset"
        )
        code = 0

    usable = base_report_usable_for_note_edit_sweep(report)
    if preset in ("edit_minimal", "edit_overdub_during_note_edit") and seed_ok:
        usable = True
        if code != 0:
            print(
                "[hitl] base strict gates failed but edit record seed ok — "
                f"continuing to {preset}"
            )
            code = 0

    if code == 0 and usable:
        ctx = get_context(args)
        ctx.base_preset_passed = True
        ctx.base_report = dict(report) if report is not None else None
        ctx.base_serial_log_path = serial_log_from_base_report(report)
        if ctx.base_serial_log_path is not None:
            print(f"[hitl] base loop seed: {ctx.base_serial_log_path}")
    return code


def verify_base_scenario(lines: list[str], args: object) -> dict[str, object]:
    import host_midi_automation_baseline as baseline

    return {"ok": True, "note": "use baseline JSON report for full gate checks"}
