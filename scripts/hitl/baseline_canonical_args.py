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


def canonical_edit_minimal_base_legacy_args() -> list[str]:
    """2-bar record-only base seed for edit_minimal (proven baseline record path, no overdub)."""
    return [
        "--record-bars",
        "2",
        "--record-only",
        "--edit-record-fixture",
        "--no-fixed-grid-notes",
        "--start-transport",
        "--boot-settle-ms",
        "10000",
        "--state-sync-timeout-ms",
        "8000",
        "--serial-grace-ms",
        "3000",
        "--phase-wait-ms",
        "500",
        "--final-wait-ms",
        "3000",
        "--press-ms",
        "120",
        "--track",
        "5",
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
        "--boot-settle-ms",
        "--serial-grace-ms",
        "--clear-press-ms",
        "--no-start-transport",
        "--stop-press-advance-clocks",
        "--use-fixture-record",
    }
)


def strip_scenario_only_legacy_args(legacy: list[str]) -> list[str]:
    """Remove flags consumed by non-baseline HITL scenarios before baseline dispatch."""
    out: list[str] = []
    index = 0
    while index < len(legacy):
        token = legacy[index]
        if token in _SCENARIO_ONLY_FLAGS:
            if token in (
                "--skip-sweep",
                "--no-start-transport",
                "--use-fixture-record",
            ):
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
