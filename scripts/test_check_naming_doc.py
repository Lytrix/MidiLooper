#!/usr/bin/env python3
"""Tests for `scripts/check_naming_doc.py` — the NAMING.md scope-drift guard.

Each rule is exercised against a synthetic document, plus the repository's own
`docs/Authority/NAMING.md` (which must stay clean).
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

import check_naming_doc as guard  # noqa: E402

_REAL_DOC = guard.DEFAULT_DOC

_HEADER = "# Doc\n\n## Section A\n\n"

_TABLE = "| Term | Meaning |\n|------|---------|\n"


def _rules(findings) -> list:
    return [finding.rule for finding in findings]


def _write(root: Path, name: str, body: str) -> None:
    target = root / name
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(body, encoding="utf-8")


class RealDocumentTest(unittest.TestCase):
    def test_repository_document_is_clean(self) -> None:
        findings = guard.check_text(_REAL_DOC.read_text(encoding="utf-8"), _REAL_DOC)
        self.assertEqual([finding.render() for finding in findings], [])

    def test_cli_exit_code_is_zero(self) -> None:
        self.assertEqual(guard.main(["--path", str(_REAL_DOC)]), 0)


class BoundClassRuleTest(unittest.TestCase):
    def test_definition_row_is_reported(self) -> None:
        text = (
            _HEADER
            + _TABLE
            + "| **Structurally bounded** | Code enforces a fixed maximum amount of work and storage |\n"
        )
        with tempfile.TemporaryDirectory() as tmp:
            findings = guard.check_text(text, Path(tmp) / "NAMING.md")
        self.assertEqual(_rules(findings), ["BOUND-CLASS"])

    def test_pointer_row_is_allowed(self) -> None:
        text = (
            _HEADER
            + _TABLE
            + "| **Structurally bounded** | see [ExecutionBounds.md](Architecture/ExecutionBounds.md) § Bound classes |\n"
        )
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            _write(root, "Architecture/ExecutionBounds.md", "# Bounds\n\n## Bound classes\n")
            findings = guard.check_text(text, root / "NAMING.md")
        self.assertEqual(findings, [])


class DuplicateDefinitionRuleTest(unittest.TestCase):
    def test_repeated_definition_is_reported(self) -> None:
        definition = (
            "Measurements provide an engineering limit but code does not enforce it anywhere on the interval path"
        )
        text = (
            "# Doc\n\n## Section A\n\n"
            + _TABLE
            + f"| **Alpha** | {definition} |\n"
            + "\n## Section B\n\n"
            + _TABLE
            + f"| **Beta** | {definition} |\n"
        )
        with tempfile.TemporaryDirectory() as tmp:
            findings = guard.check_text(text, Path(tmp) / "NAMING.md")
        self.assertEqual(_rules(findings), ["DUPLICATE"])

    def test_distinct_definitions_are_allowed(self) -> None:
        text = (
            "# Doc\n\n## Section A\n\n"
            + _TABLE
            + "| **Alpha** | One committed content row on the loop passes store at record stop |\n"
            + "\n## Section B\n\n"
            + _TABLE
            + "| **Beta** | A consumer-owned structure with its own storage revision and build policy |\n"
        )
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(guard.check_text(text, Path(tmp) / "NAMING.md"), [])


class CitationRuleTest(unittest.TestCase):
    def test_uncited_contract_sentence_is_reported(self) -> None:
        text = _HEADER + "A read SHALL never be torn while the playhead moves.\n"
        with tempfile.TemporaryDirectory() as tmp:
            findings = guard.check_text(text, Path(tmp) / "NAMING.md")
        self.assertEqual(_rules(findings), ["CITATION"])

    def test_cited_contract_sentence_passes(self) -> None:
        text = (
            _HEADER
            + "A read SHALL never be torn while the playhead moves — see [Snapshots.md](Snapshots.md) § Publication.\n"
        )
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            _write(root, "Snapshots.md", "# Snapshots\n\n## Publication\n")
            findings = guard.check_text(text, root / "NAMING.md")
        self.assertEqual(findings, [])

    def test_policy_value_without_citation_is_reported(self) -> None:
        text = _HEADER + "The cap is 10,000 µs per idle turn.\n"
        with tempfile.TemporaryDirectory() as tmp:
            findings = guard.check_text(text, Path(tmp) / "NAMING.md")
        self.assertEqual(_rules(findings), ["CITATION"])


class SectionCitationRuleTest(unittest.TestCase):
    def test_missing_target_is_reported_once(self) -> None:
        text = _HEADER + "See [gone.md](gone.md) § Anything.\n"
        with tempfile.TemporaryDirectory() as tmp:
            findings = guard.check_text(text, Path(tmp) / "NAMING.md")
        self.assertEqual(_rules(findings), ["SECTION"])
        self.assertIn("does not exist", findings[0].detail)

    def test_unresolvable_heading_is_reported(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            _write(root, "Owner.md", "# Owner\n\n## Real section\n")
            findings = guard.check_text(_HEADER + "See [Owner.md](Owner.md) § Imaginary section.\n", root / "NAMING.md")
        self.assertEqual(_rules(findings), ["SECTION"])

    def test_prefix_heading_and_self_reference_resolve(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            _write(root, "Owner.md", "# Owner\n\n## Readiness deadline and window size\n")
            text = (
                _HEADER
                + "Contract: [Owner.md](Owner.md) § Readiness deadline.\n"
                + "Registered terms may be added later — see § Section A above.\n"
            )
            findings = guard.check_text(text, root / "NAMING.md")
        self.assertEqual([finding.render() for finding in findings], [])


class AuditTest(unittest.TestCase):
    def test_cross_section_labels_are_listed(self) -> None:
        text = (
            "# Doc\n\n## Section A\n\n"
            + _TABLE
            + "| **Alpha** | first claim |\n"
            + "\n## Section B\n\n"
            + _TABLE
            + "| **Alpha** | second claim |\n"
        )
        labels = [label for label, _ in guard.audit_duplicate_labels(text)]
        self.assertEqual(labels, ["Alpha"])

    def test_column_headers_are_not_labels(self) -> None:
        text = "# Doc\n\n## Section A\n\n" + _TABLE + "| **Alpha** | claim |\n"
        self.assertEqual(guard.audit_duplicate_labels(text), [])

    def test_repository_audit_names_the_known_restatements(self) -> None:
        labels = [label for label, _ in guard.audit_duplicate_labels(_REAL_DOC.read_text(encoding="utf-8"))]
        self.assertIn("LoopSnapshot", labels)
        self.assertNotIn("Term", labels)


if __name__ == "__main__":
    unittest.main()
