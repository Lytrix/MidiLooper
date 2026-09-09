"""Attach a stimulus fingerprint to a host snapshot and report payload."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from hitl.stimuli.fingerprint import format_fingerprint_text, prepend_hitl_header
from hitl.stimuli.spec import StimulusSpec
from hitl.stimuli.validate import format_human_checklist


def stimulus_report_fields(spec: StimulusSpec) -> dict[str, Any]:
    return {
        "id": spec.stimulus_id,
        "version": spec.version,
        "kind": spec.kind,
        "must_prove": list(spec.must_prove),
    }


def write_headered_snapshot(path: Path, lines: list[str], spec: StimulusSpec) -> list[str]:
    headered = prepend_hitl_header(lines, spec)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(headered) + "\n", encoding="utf-8")
    return headered


def print_stimulus_banner(spec: StimulusSpec) -> None:
    print(format_fingerprint_text(spec))


def print_human_checklist(spec: StimulusSpec, check: dict[str, Any]) -> None:
    cases = check.get("cases")
    if isinstance(cases, dict) and cases:
        machine_total = f"{sum(1 for case in cases.values() if case.get('ok'))}/{len(cases)}"
    else:
        machine_total = "1/1" if check.get("ok") else "0/1"
    print(
        format_human_checklist(
            spec,
            machine_ok=bool(check.get("ok")),
            machine_total=machine_total,
        )
    )
