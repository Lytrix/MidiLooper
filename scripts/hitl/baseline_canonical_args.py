"""Canonical host_midi_automation_baseline CLI flags (HITL-Test-Flow.md)."""

from __future__ import annotations


def canonical_baseline_legacy_args() -> list[str]:
    """Bar-synced 2+2 record/overdub baseline; user legacy args should be appended to override."""
    return [
        "--record-bars",
        "2",
        "--overdub-bars",
        "2",
        "--no-fixed-grid-notes",
        "--start-transport",
        "--overdub-start-delay-bars",
        "0",
        "--overdub-start-delay-beats",
        "1",
        "--phase-wait-ms",
        "500",
        "--final-wait-ms",
        "3000",
        "--press-ms",
        "120",
        "--undo-redo-delay-ms",
        "3000",
    ]


def merge_legacy_cli_args(defaults: list[str], overrides: list[str]) -> list[str]:
    """Defaults first; argparse last-wins lets overrides replace duplicate flags."""
    return list(defaults) + list(overrides)
