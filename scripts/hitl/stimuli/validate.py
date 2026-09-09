"""First-stage checks: was the declared live-MIDI experiment performed?"""

from __future__ import annotations

from typing import Any

from hitl.stimuli.fingerprint import parse_hitl_fields
from hitl.stimuli.spec import StimulusSpec
from hitl.verify.note_edit_overdub_compose import (
    parse_live_capture_note_ons,
    parse_note_edit_open_visual_notes,
    parse_playing_to_overdubbing,
    _move_marker_index,
)


def _check(name: str, ok: bool, detail: str) -> dict[str, Any]:
    return {"name": name, "ok": ok, "detail": detail}


def validate_stimulus_header(
    lines: list[str],
    expected: StimulusSpec,
    *,
    require_header: bool = False,
) -> dict[str, Any]:
    fields = parse_hitl_fields(lines)
    issues: list[str] = []
    if "STIMULUS" not in fields:
        if require_header:
            issues.append("stimulus_header_missing")
        return {
            "ok": not issues,
            "header_present": False,
            "issues": issues,
            "fields": fields,
            "warning": "pre_fingerprint_capture" if not require_header else None,
        }
    if fields.get("STIMULUS") != expected.stimulus_id:
        issues.append(
            f"stimulus_id_mismatch expected={expected.stimulus_id} "
            f"observed={fields.get('STIMULUS')}"
        )
    version_raw = fields.get("STIMULUS_VERSION")
    if version_raw != str(expected.version):
        issues.append(
            f"stimulus_version_mismatch expected={expected.version} "
            f"observed={version_raw}"
        )
    if fields.get("SCENARIO") != expected.scenario_id:
        issues.append(
            f"scenario_mismatch expected={expected.scenario_id} "
            f"observed={fields.get('SCENARIO')}"
        )
    return {
        "ok": not issues,
        "header_present": True,
        "issues": issues,
        "fields": fields,
        "warning": None,
    }


def validate_compose_must_prove(lines: list[str], spec: StimulusSpec) -> dict[str, Any]:
    """Serial evidence that the compose recipe actually ran."""
    checks: list[dict[str, Any]] = []
    opened = parse_note_edit_open_visual_notes(lines)
    checks.append(
        _check(
            "note_edit_entered",
            opened is not None,
            f"visual_notes={opened['visual_notes']}" if opened else "missing",
        )
    )

    overdubs = []
    if opened is not None:
        overdubs = [
            row for row in parse_playing_to_overdubbing(lines) if row["index"] > opened["index"]
        ]
    checks.append(
        _check(
            "in_edit_overdub_active",
            bool(overdubs),
            f"count={len(overdubs)}",
        )
    )

    live_after = overdubs[0]["index"] if overdubs else (opened["index"] if opened else 0)
    in_edit = next((row for row in spec.overdubs if row.name == "IN_EDIT_OD"), None)
    low = in_edit.note_low if in_edit and in_edit.note_low is not None else 12
    high = in_edit.note_high if in_edit and in_edit.note_high is not None else 23
    live_ons = parse_live_capture_note_ons(lines, after_index=live_after, low=low, high=high)
    checks.append(
        _check(
            "live_capture_notes_present",
            bool(live_ons),
            f"count={len(live_ons)} band={low}-{high}",
        )
    )

    move_index = _move_marker_index(
        lines, after_index=live_ons[0]["index"] if live_ons else live_after
    )
    checks.append(
        _check(
            "selected_note_moved",
            move_index is not None,
            f"index={move_index}",
        )
    )

    issues = [row["name"] for row in checks if not row["ok"]]
    return {
        "ok": not issues,
        "issues": issues,
        "checks": checks,
    }


def validate_stimulus(
    lines: list[str],
    expected: StimulusSpec,
    *,
    require_header: bool = False,
) -> dict[str, Any]:
    header = validate_stimulus_header(
        lines, expected, require_header=require_header
    )
    recipe: dict[str, Any] = {"ok": True, "issues": [], "checks": [], "skipped": True}
    if header.get("header_present"):
        recipe = (
            validate_compose_must_prove(lines, expected)
            if expected.scenario_id == "note_edit_overdub_compose"
            else {"ok": True, "issues": [], "checks": []}
        )
        recipe["skipped"] = False
    issues = list(header.get("issues") or []) + [
        f"must_prove:{name}" for name in (recipe.get("issues") or [])
    ]
    return {
        "ok": not issues,
        "issues": issues,
        "header": header,
        "must_prove": recipe,
        "stimulus_id": expected.stimulus_id,
        "version": expected.version,
    }


def format_human_checklist(spec: StimulusSpec, *, machine_ok: bool, machine_total: str) -> str:
    boxes = "\n".join(f"[ ] {item}" for item in spec.human_checks)
    verdict = "PASS" if machine_ok else "FAIL"
    return (
        f"HITL REQUIRED\n"
        f"Scenario: {spec.scenario_id}\n"
        f"Stimulus: {spec.fingerprint_key()}\n"
        f"Machine: {verdict} ({machine_total})\n"
        f"\n"
        f"Please verify physically:\n"
        f"{boxes}\n"
        f"\n"
        f"Everything else has been machine-checked."
    )
