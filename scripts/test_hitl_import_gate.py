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


class ObservationFacadeGateTests(unittest.TestCase):
    """The one thing a flow may use for an observation wait, and how the gate was resolved for it (task 3.3).

    The task allows resolving the import gate "only for the narrow architecture-approved observation-wait
    facade". It did not have to be widened at all: the wait lives under `hitl.actions`, a prefix the flow layer
    may already import, so the forbidden list is unchanged and no scenario needed a new permission to wait for
    rows the device emits on its own. These tests pin exactly that.
    """

    def test_the_facade_lives_under_a_prefix_flows_may_import(self) -> None:
        facade = _FLOWS_DIR.parent / "actions" / "observation.py"
        self.assertTrue(facade.is_file(), "the observation-wait facade is the action-layer module")
        name = "hitl.actions.observation"
        self.assertTrue(
            any(name == prefix or name.startswith(prefix + ".") for prefix in _FLOW_ALLOWED_PREFIXES),
            f"{name} is not under a flow-allowed prefix",
        )
        self.assertEqual([], _violations({name}, _FLOW_FORBIDDEN_PREFIXES))

    def test_a_flow_that_imports_the_facade_passes_the_gate(self) -> None:
        source = ("from hitl.actions.observation import await_markers\n"
                  "from hitl.actions import cancellation, correlation, slots\n")
        tree = ast.parse(source)
        imports: set[str] = set()
        for node in ast.walk(tree):
            if isinstance(node, ast.ImportFrom) and node.module:
                imports.add(node.module)
            elif isinstance(node, ast.Import):
                imports.update(alias.name for alias in node.names)
        self.assertEqual([], _violations(imports, _FLOW_FORBIDDEN_PREFIXES))

    def test_nothing_was_widened_for_the_facade(self) -> None:
        self.assertEqual(
            ("hitl.verify", "hitl.serial", "hitl.scenarios", "hitl.reporting", "hitl.legacy_"),
            _FLOW_FORBIDDEN_PREFIXES,
        )


if __name__ == "__main__":
    unittest.main()
