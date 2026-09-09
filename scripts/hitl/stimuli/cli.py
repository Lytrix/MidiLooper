"""Stimulus CLI: YAML view + TEMP sidecar. Does not register variants."""

from __future__ import annotations

import argparse
from pathlib import Path

from hitl.stimuli.registry import get_stimulus, temporary_variant
from hitl.stimuli.spec import StimulusSpec
from hitl.stimuli.yaml_view import export_registry_yaml, export_spec_yaml, write_yaml


def add_stimulus_subparser(sub: argparse._SubParsersAction) -> None:
    stim = sub.add_parser(
        "stimulus",
        help="List, export, or fork HITL stimuli (YAML is a view)",
    )
    stim_sub = stim.add_subparsers(dest="stimulus_command", required=True)
    export_p = stim_sub.add_parser(
        "export-yaml",
        help="Dump the in-memory registry as YAML (view only, not authority)",
    )
    export_p.add_argument("--out", type=Path, default=None)
    stim_sub.add_parser(
        "list",
        help="List canonical stimulus ids, generators, and versions",
    )
    temp_p = stim_sub.add_parser(
        "temp",
        help="Write a TEMP variant sidecar without registering it",
    )
    temp_p.add_argument("parent_id", help="Canonical stimulus id to fork")
    temp_p.add_argument("suffix", help="TEMP suffix, e.g. A → STIM-…-TEMP-A")
    temp_p.add_argument("--reason", default="", help="Why the canonical recipe is insufficient")
    temp_p.add_argument("--out-dir", type=Path, default=Path("captures"))


def write_temporary_sidecar(
    parent_id: str,
    suffix: str,
    *,
    out_dir: Path,
    reason: str = "",
) -> tuple[StimulusSpec, Path]:
    spec = temporary_variant(parent_id, suffix)
    path = Path(out_dir) / "stimuli" / f"{spec.stimulus_id}.yaml"
    text = export_spec_yaml(spec)
    if reason:
        text = f"# reason: {reason}\n" + text
    write_yaml(path, text)
    return spec, path


def default_temp_parent_id(scenario_ids: list[str]) -> str | None:
    from hitl.stimuli.registry import stimulus_for_scenario

    found: str | None = None
    for scenario_id in scenario_ids:
        spec = stimulus_for_scenario(scenario_id)
        if spec is not None:
            found = spec.stimulus_id
    return found


def apply_run_temp_override(args: argparse.Namespace, scenario_ids: list[str]) -> int:
    from hitl.stimuli.session import set_run_stimulus_override

    set_run_stimulus_override(None)
    suffix = getattr(args, "stimulus_temp_suffix", None)
    if not suffix:
        return 0
    parent_id = getattr(args, "stimulus_temp_parent", None) or default_temp_parent_id(
        scenario_ids
    )
    if not parent_id:
        print("[hitl] --stimulus-temp-suffix needs a parent stimulus on this preset")
        return 2
    try:
        get_stimulus(parent_id)
    except KeyError as exc:
        print(f"[hitl] {exc}")
        return 2
    reason = getattr(args, "stimulus_temp_reason", "") or ""
    out_dir = Path(getattr(args, "out_dir", Path("captures")))
    spec, path = write_temporary_sidecar(
        parent_id, suffix, out_dir=out_dir, reason=reason
    )
    set_run_stimulus_override(spec)
    print(
        f"[hitl] TEMP stimulus {spec.stimulus_id} (not registered); sidecar {path}"
    )
    return 0


def run_stimulus_command(args: argparse.Namespace) -> int:
    command = getattr(args, "stimulus_command", None)
    if command == "list":
        from hitl.stimuli.registry import format_stimulus_list_lines

        for line in format_stimulus_list_lines():
            print(line)
        return 0
    if command == "export-yaml":
        text = export_registry_yaml()
        out = getattr(args, "out", None)
        if out is None:
            print(text, end="")
            return 0
        write_yaml(Path(out), text)
        print(f"[hitl] wrote registry YAML view: {out}")
        return 0
    if command == "temp":
        try:
            spec, path = write_temporary_sidecar(
                args.parent_id,
                args.suffix,
                out_dir=Path(args.out_dir),
                reason=getattr(args, "reason", "") or "",
            )
        except KeyError as exc:
            print(f"[hitl] {exc}")
            return 2
        print(f"Created {spec.stimulus_id} (not registered)")
        print(f"Parent: {spec.parent_id} v{spec.version}")
        print(f"Kind: {spec.kind}")
        print(f"Wrote {path}")
        print(
            "Run the parent scenario with --stimulus-temp-suffix "
            f"{args.suffix} --stimulus-temp-parent {spec.parent_id}"
        )
        return 0
    print("[hitl] unknown stimulus command")
    return 2
