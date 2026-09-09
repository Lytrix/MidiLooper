"""First-stage checks: was the declared live-MIDI experiment performed?"""

from __future__ import annotations

from typing import Any

from hitl.capture_transitions import _count_capture_transitions
from hitl.stimuli.fingerprint import parse_hitl_fields
from hitl.stimuli.spec import StimulusSpec
from hitl.verify.note_edit_overdub_compose import (
    parse_live_capture_note_ons,
    parse_note_edit_open_visual_notes,
    parse_playing_to_overdubbing,
    _case_same_pitch_hold_emits_overlap,
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

    if "same_pitch_hold_overlap" in spec.must_prove:
        after = move_index if move_index is not None else live_after
        overlap = _case_same_pitch_hold_emits_overlap(lines, after_index=after)
        checks.append(
            _check(
                "same_pitch_hold_overlap",
                bool(overlap.get("ok")),
                f"hide_or_shorten={overlap.get('hide_or_shorten')}",
            )
        )

    issues = [row["name"] for row in checks if not row["ok"]]
    return {
        "ok": not issues,
        "issues": issues,
        "checks": checks,
    }


def _recording_seen(lines: list[str]) -> bool:
    if any(",RECA," in line and "#CAP," in line for line in lines):
        return True
    if any("Recording started" in line for line in lines):
        return True
    latest = None
    for line in lines:
        if "#CAP," not in line or ",ST,Track," not in line:
            continue
        parts = line[line.find("#CAP,"):].split(",")
        if len(parts) >= 5:
            latest = parts[4].strip()
    return latest == "RECORDING" or any(
        ",ST,Track," in line and ",RECORDING" in line for line in lines
    )


def validate_overdub_count_must_prove(lines: list[str], spec: StimulusSpec) -> dict[str, Any]:
    counts = _count_capture_transitions(lines)
    overdub_entries = counts.get(("PLAYING", "OVERDUBBING"), 0)
    checks = [
        _check("base_recorded", _recording_seen(lines), f"overdub_entries={overdub_entries}"),
    ]
    if spec.must_prove and spec.must_prove[0] == "seed_recorded":
        checks[0]["name"] = "seed_recorded"
    for index, row in enumerate(spec.overdubs, start=1):
        name = spec.must_prove[index] if index < len(spec.must_prove) else f"committed_{row.name.lower()}"
        checks.append(_check(name, overdub_entries >= index, f"count={overdub_entries} need>={index}"))
    issues = [row["name"] for row in checks if not row["ok"]]
    return {"ok": not issues, "issues": issues, "checks": checks}


def validate_occupy_must_prove(lines: list[str], spec: StimulusSpec) -> dict[str, Any]:
    recipe = validate_overdub_count_must_prove(lines, spec)
    undo = sum(1 for line in lines if "Overdub undone" in line)
    redo = sum(1 for line in lines if "Overdub redone" in line)
    undo_ok = undo >= 2 and redo >= 2
    checks = list(recipe.get("checks") or [])
    checks.append(_check("undo_redo", undo_ok, f"undo={undo} redo={redo}"))
    issues = [row["name"] for row in checks if not row["ok"]]
    return {"ok": not issues, "issues": issues, "checks": checks}


def validate_edit_overdub_must_prove(lines: list[str], spec: StimulusSpec) -> dict[str, Any]:
    counts = _count_capture_transitions(lines)
    overdub_entries = counts.get(("PLAYING", "OVERDUBBING"), 0)
    entered = any("entered note edit mode" in line for line in lines) or (
        parse_note_edit_open_visual_notes(lines) is not None
    )
    session_undo = any("EditSession undo" in line for line in lines)
    session_redo = any("EditSession redo" in line for line in lines)
    checks = [
        _check("pre_edit_overdub", overdub_entries >= 1, f"count={overdub_entries}"),
        _check("note_edit_entered", entered, "open" if entered else "missing"),
        _check("in_edit_overdub_active", overdub_entries >= 2, f"count={overdub_entries}"),
        _check("session_undo_redo", session_undo and session_redo, f"undo={session_undo} redo={session_redo}"),
    ]
    issues = [row["name"] for row in checks if not row["ok"]]
    return {"ok": not issues, "issues": issues, "checks": checks}


def validate_recipe(lines: list[str], spec: StimulusSpec) -> dict[str, Any]:
    if spec.scenario_id == "note_edit_overdub_compose":
        return validate_compose_must_prove(lines, spec)
    if spec.scenario_id == "overdub_occupy_paint":
        return validate_occupy_must_prove(lines, spec)
    if spec.scenario_id == "edit_overdub_during_note_edit":
        return validate_edit_overdub_must_prove(lines, spec)
    if spec.scenario_id == "base":
        return validate_overdub_count_must_prove(lines, spec)
    return {"ok": True, "issues": [], "checks": []}


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
        recipe = validate_recipe(lines, expected)
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
