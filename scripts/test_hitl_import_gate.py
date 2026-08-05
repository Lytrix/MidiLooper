#!/usr/bin/env python3
"""Import gate tests for layered HITL flows and scenarios."""

from __future__ import annotations

import ast
import sys
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

_PROJECT_ROOT = _SCRIPT_DIR.parent
_FLOWS_DIR = _SCRIPT_DIR / "hitl" / "flows"
_SCENARIOS_STUB = _SCRIPT_DIR / "hitl" / "scenarios" / "layered_stubs.py"

_FLOW_ALLOWED_PREFIXES = (
    "hitl.actions",
    "hitl.context",
    "hitl.config",
    "hitl.flows",
)

_FLOW_FORBIDDEN_PREFIXES = (
    "hitl.verify",
    "hitl.serial",
    "hitl.scenarios",
    "hitl.reporting",
    "hitl.legacy_",
)

_SCENARIO_ALLOWED_PREFIXES = (
    "hitl.flows",
    "hitl.context",
    "hitl.config",
    "hitl.types",
    "hitl.layered_registry",
)

_SCENARIO_FORBIDDEN_PREFIXES = (
    "hitl.serial",
    "hitl.midi_io",
    "hitl.transport_clock",
    "hitl.actions",
    "hitl.verify",
    "hitl.legacy_",
)


def _imports_in_file(path: Path) -> set[str]:
    tree = ast.parse(path.read_text(encoding="utf-8"))
    imports: set[str] = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            for alias in node.names:
                imports.add(alias.name)
        elif isinstance(node, ast.ImportFrom):
            if node.module:
                imports.add(node.module)
    return imports


def _violations(imports: set[str], forbidden: tuple[str, ...]) -> list[str]:
    return sorted(
        name
        for name in imports
        if any(name == prefix or name.startswith(prefix + ".") for prefix in forbidden)
    )


class ImportGateTests(unittest.TestCase):
    def test_flow_modules_respect_import_gate(self) -> None:
        for path in sorted(_FLOWS_DIR.glob("*.py")):
            if path.name == "__init__.py":
                continue
            imports = _imports_in_file(path)
            violations = _violations(imports, _FLOW_FORBIDDEN_PREFIXES)
            self.assertEqual(violations, [], f"{path.name} forbidden imports: {violations}")

    def test_layered_scenario_stub_respects_import_gate(self) -> None:
        imports = _imports_in_file(_SCENARIOS_STUB)
        violations = _violations(imports, _SCENARIO_FORBIDDEN_PREFIXES)
        self.assertEqual(violations, [], f"layered_stubs forbidden imports: {violations}")


if __name__ == "__main__":
    unittest.main()
