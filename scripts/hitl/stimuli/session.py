"""Run-scoped stimulus override for TEMP variants.

A TEMP override is never registered as canonical. Scenarios keep their
canonical constant and resolve through this module at write/verify time.
"""

from __future__ import annotations

from hitl.stimuli.spec import StimulusSpec

_run_override: StimulusSpec | None = None


def set_run_stimulus_override(spec: StimulusSpec | None) -> None:
    global _run_override
    _run_override = spec


def get_run_stimulus_override() -> StimulusSpec | None:
    return _run_override


def resolve_run_stimulus(canonical: StimulusSpec) -> StimulusSpec:
    override = _run_override
    if override is None:
        return canonical
    if override.stimulus_id == canonical.stimulus_id:
        return override
    if override.parent_id == canonical.stimulus_id:
        return override
    return canonical
