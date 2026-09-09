"""YAML view of the Python stimulus registry.

This module dumps the in-memory registry. It does not load YAML to define
canonical stimuli. Python `registry.py` remains the only authority.
"""

from __future__ import annotations

from dataclasses import asdict
from pathlib import Path
from typing import Any

from hitl.stimuli.registry import list_stimuli
from hitl.stimuli.spec import StimulusSpec


def spec_to_mapping(spec: StimulusSpec) -> dict[str, Any]:
    return asdict(spec)


def registry_mapping() -> dict[str, Any]:
    return {
        "authority": "scripts/hitl/stimuli/registry.py",
        "note": "This file is a view. Do not treat it as the stimulus source of truth.",
        "stimuli": [spec_to_mapping(spec) for spec in list_stimuli()],
    }


def dump_yaml(value: Any, indent: int = 0) -> str:
    pad = "  " * indent
    if value is None:
        return "null"
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int) and not isinstance(value, bool):
        return str(value)
    if isinstance(value, float):
        return repr(value)
    if isinstance(value, str):
        return _quote_string(value)
    if isinstance(value, tuple):
        return dump_yaml(list(value), indent)
    if isinstance(value, list):
        if not value:
            return "[]"
        lines = []
        for item in value:
            if isinstance(item, (dict, list)):
                item_yaml = dump_yaml(item, indent + 1)
                if item_yaml.startswith("{") or item_yaml.startswith("["):
                    lines.append(f"{pad}- {item_yaml}")
                else:
                    first, *rest = item_yaml.splitlines()
                    lines.append(f"{pad}- {first.lstrip()}")
                    lines.extend(rest)
            else:
                lines.append(f"{pad}- {dump_yaml(item)}")
        return "\n".join(lines)
    if isinstance(value, dict):
        if not value:
            return "{}"
        lines = []
        for key, item in value.items():
            key_text = _quote_string(str(key)) if _needs_quotes(str(key)) else str(key)
            if isinstance(item, dict):
                if not item:
                    lines.append(f"{pad}{key_text}: {{}}")
                else:
                    lines.append(f"{pad}{key_text}:")
                    lines.append(dump_yaml(item, indent + 1))
            elif isinstance(item, list):
                if not item:
                    lines.append(f"{pad}{key_text}: []")
                else:
                    lines.append(f"{pad}{key_text}:")
                    lines.append(dump_yaml(item, indent + 1))
            else:
                lines.append(f"{pad}{key_text}: {dump_yaml(item)}")
        return "\n".join(lines)
    return _quote_string(str(value))


def export_registry_yaml() -> str:
    return (
        "# HITL stimulus catalog view. Authority: scripts/hitl/stimuli/registry.py\n"
        + dump_yaml(registry_mapping())
        + "\n"
    )


def export_spec_yaml(spec: StimulusSpec) -> str:
    header = (
        f"# Temporary or exported stimulus view. Not a registry authority.\n"
        f"# id: {spec.stimulus_id}  kind: {spec.kind}  parent: {spec.parent_id or '-'}\n"
    )
    return header + dump_yaml(spec_to_mapping(spec)) + "\n"


def write_yaml(path: Path, text: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


def _needs_quotes(text: str) -> bool:
    if text == "":
        return True
    if text[:1] in ":-?#,&*!|>%@`'\"" or text[-1:] in ":":
        return True
    if any(ch in text for ch in "\n:#{}[],&*!|>%@`'\""):
        return True
    if text.lower() in {"true", "false", "null", "yes", "no"}:
        return True
    return False


def _quote_string(text: str) -> str:
    escaped = text.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'
