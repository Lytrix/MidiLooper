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


_SCENARIO_ONLY_FLAGS = frozenset(
    {
        "--dwell-ms",
        "--toggle-dwell-ms",
        "--toggle-cycles",
        "--seed-serial-log",
        "--post-seed-settle-ms",
        "--edit-enter-timeout-s",
        "--skip-sweep",
    }
)


def strip_scenario_only_legacy_args(legacy: list[str]) -> list[str]:
    """Remove flags consumed by non-baseline HITL scenarios before baseline dispatch."""
    out: list[str] = []
    index = 0
    while index < len(legacy):
        token = legacy[index]
        if token in _SCENARIO_ONLY_FLAGS:
            if token == "--skip-sweep":
                index += 1
                continue
            if index + 1 < len(legacy) and not legacy[index + 1].startswith("-"):
                index += 2
                continue
            index += 1
            continue
        out.append(token)
        index += 1
    return out
