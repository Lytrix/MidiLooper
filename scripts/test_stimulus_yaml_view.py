#!/usr/bin/env python3
"""Host tests for the HITL stimulus YAML view (`hitl/stimuli/yaml_view.py`).

Regression cover for a latent defect found on 2026-09-26 while authoring the S4f.3 retention stimulus: the
exporter emitted **invalid YAML** for every list-ish `StimulusSpec` field, because `dataclasses.asdict`
preserves tuple fields as tuples and `dump_yaml`'s mapping branch tested `isinstance(item, list)` only. The
symptom was `overdubs: - name: "OD1"` (a block sequence starting on its key's line, which YAML forbids) and a
second `must_prove` entry detached to column 0.

No YAML parser is available in this environment, so the rules checked here are the two structural properties
the defect violated, asserted over **every** registered spec and over the full registry export:

  1. a non-empty block sequence never starts on its mapping key's line;
  2. every block-sequence entry is indented under its key.

A positive control keeps this from passing vacuously on a spec with no sequences.

Run: .venv/bin/python scripts/test_stimulus_yaml_view.py
"""
import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from hitl.stimuli.registry import list_stimuli  # noqa: E402
from hitl.stimuli.yaml_view import export_registry_yaml, export_spec_yaml  # noqa: E402

# `key: - item` -- a block sequence beginning on its parent mapping key's line.
INLINE_SEQUENCE_RE = re.compile(r"^\s*[A-Za-z_][A-Za-z0-9_]*:\s+-\s")
# A block-sequence entry line that is not indented at all.
COLUMN_ZERO_ENTRY_RE = re.compile(r"^-\s")
# A sequence entry (to be checked for indentation).
ENTRY_RE = re.compile(r"^(\s*)-\s")


class StimulusYamlViewTests(unittest.TestCase):
    def documents(self):
        yield "registry", export_registry_yaml()
        for spec in list_stimuli():
            yield spec.stimulus_id, export_spec_yaml(spec)

    def test_no_block_sequence_starts_on_its_keys_line(self):
        for name, text in self.documents():
            for i, line in enumerate(text.splitlines(), start=1):
                if line.lstrip().startswith("#"):
                    continue
                self.assertIsNone(
                    INLINE_SEQUENCE_RE.match(line),
                    f"{name}:{i}: block sequence inlined after its key (invalid YAML): {line!r}",
                )

    def test_every_block_sequence_entry_is_indented_under_its_key(self):
        for name, text in self.documents():
            for i, line in enumerate(text.splitlines(), start=1):
                if line.lstrip().startswith("#"):
                    continue
                self.assertIsNone(
                    COLUMN_ZERO_ENTRY_RE.match(line),
                    f"{name}:{i}: block-sequence entry detached to column 0 (not a value of any key): "
                    f"{line!r}",
                )

    def test_sequences_are_actually_emitted_as_block_entries(self):
        """Positive control: the fields that were broken must still round-trip as indented entries."""
        text = export_spec_yaml(list_stimuli()[0])
        for key in ("overdubs:", "must_prove:", "human_checks:"):
            self.assertIn(key, text, f"{key} missing from the exported view")
        indented_entries = [l for l in text.splitlines() if ENTRY_RE.match(l) and l.startswith(" ")]
        self.assertGreaterEqual(len(indented_entries), 3,
                                f"expected indented sequence entries, found {indented_entries}")
        # The specific historical symptom, asserted directly.
        self.assertNotIn('overdubs: - name:', text)
        # `must_prove`'s entries must all sit under it, never back at column 0.
        lines = text.splitlines()
        for idx, line in enumerate(lines):
            if line.strip() == "must_prove:":
                following = [l for l in lines[idx + 1:] if l.strip()]
                self.assertTrue(following and following[0].startswith("  - "),
                                f"must_prove's first entry is not indented under it: {following[:2]}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
