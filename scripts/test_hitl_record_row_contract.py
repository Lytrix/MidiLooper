#!/usr/bin/env python3
"""Every per-track phase-stats row must carry the keys the baseline report reads.

`legacy_record_baseline.run()` builds `per_track_stats` rows on two different
paths -- the `--record-only` seed row (used by the `edit_minimal`,
`edit_overdub_during_note_edit` and `note_edit_reselect_stacked` presets) and the
full record/overdub row -- and later reads a fixed set of those keys with
`row["..."]` (no default) while assembling the report. When a producer row drops
one of those keys the report builder raises `KeyError` *after* the device leg has
already run, so the run loses its report and the composed preset never starts.

This test pairs the consumer and the producers in the source, so a future edit
that drops a required key from either row fails here instead of on hardware.
"""

from __future__ import annotations

import ast
import sys
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

SOURCE = _SCRIPT_DIR / "hitl" / "legacy_record_baseline.py"

# The consumer loop is identified by this key: only the report loop reads it.
_CONSUMER_ANCHOR_KEY = "record_clock_pulses_seen"


def _tree() -> ast.Module:
    return ast.parse(SOURCE.read_text(encoding="utf-8"))


def _row_key(node: ast.AST) -> str | None:
    if (
        isinstance(node, ast.Subscript)
        and isinstance(node.value, ast.Name)
        and node.value.id == "row"
        and isinstance(node.slice, ast.Constant)
        and isinstance(node.slice.value, str)
    ):
        return node.slice.value
    return None


def _consumer_keys(tree: ast.Module) -> set[str]:
    """Keys the report loop reads unconditionally from each `per_track_stats` row."""
    for node in ast.walk(tree):
        if not isinstance(node, ast.For):
            continue
        if not (isinstance(node.iter, ast.Name) and node.iter.id == "per_track_stats"):
            continue
        keys = {k for k in (_row_key(sub) for sub in ast.walk(node)) if k is not None}
        if _CONSUMER_ANCHOR_KEY in keys:
            return keys
    raise AssertionError(
        f"no `for row in per_track_stats:` loop reading row[{_CONSUMER_ANCHOR_KEY!r}] found"
    )


def _producer_rows(tree: ast.Module) -> list[tuple[int, set[str]]]:
    """`(line, literal keys)` for every `per_track_stats.append({...})` row."""
    rows: list[tuple[int, set[str]]] = []
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call):
            continue
        func = node.func
        if not (
            isinstance(func, ast.Attribute)
            and func.attr == "append"
            and isinstance(func.value, ast.Name)
            and func.value.id == "per_track_stats"
        ):
            continue
        if not node.args or not isinstance(node.args[0], ast.Dict):
            raise AssertionError(
                f"line {node.lineno}: per_track_stats.append() is not a dict literal; "
                "this contract test cannot inspect it"
            )
        keys = {
            key.value
            for key in node.args[0].keys
            if isinstance(key, ast.Constant) and isinstance(key.value, str)
        }
        rows.append((node.lineno, keys))
    return rows


class PerTrackRowKeyContractTests(unittest.TestCase):
    def test_every_producer_row_carries_the_report_keys(self) -> None:
        tree = _tree()
        required = _consumer_keys(tree)
        self.assertTrue(
            required,
            "the report loop reads no literal row keys; the contract test lost its anchor",
        )
        rows = _producer_rows(tree)
        self.assertGreaterEqual(
            len(rows), 2, "expected both the record-only and the full record/overdub row builders"
        )
        for lineno, keys in rows:
            missing = sorted(required - keys)
            self.assertFalse(
                missing,
                f"legacy_record_baseline.py:{lineno} builds a per_track_stats row without "
                f"{missing}, which the report loop reads with row[\"...\"] (KeyError after the "
                "device leg has already run)",
            )

    def test_record_only_row_names_the_phase_clock_counts(self) -> None:
        """The `--record-only` seed row reports its own phase activation evidence."""
        rows = {lineno: keys for lineno, keys in _producer_rows(_tree())}
        record_only = [
            (lineno, keys)
            for lineno, keys in rows.items()
            if "overdub_notes_sent" in keys and "second_overdub_notes_sent" in keys
        ]
        self.assertTrue(record_only, "no record-only per_track_stats row found")
        for lineno, keys in record_only:
            self.assertIn("record_clock_pulses_seen", keys, f"line {lineno}")


if __name__ == "__main__":
    unittest.main()
