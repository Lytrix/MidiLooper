"""HITL stimulus registry: versioned live-MIDI recipes, not a second generator."""

from hitl.stimuli.fingerprint import (
    format_fingerprint_text,
    format_hitl_header,
    parse_hitl_fields,
    prepend_hitl_header,
)
from hitl.stimuli.registry import (
    STIM_BASE_2_ONE_OD_01,
    STIM_NE_OD_COMPOSE_01,
    get_stimulus,
    list_stimuli,
    stimulus_for_scenario,
    temporary_variant,
)
from hitl.stimuli.spec import StimulusSpec
from hitl.stimuli.validate import format_human_checklist, validate_stimulus

__all__ = [
    "STIM_BASE_2_ONE_OD_01",
    "STIM_NE_OD_COMPOSE_01",
    "StimulusSpec",
    "format_fingerprint_text",
    "format_hitl_header",
    "format_human_checklist",
    "get_stimulus",
    "list_stimuli",
    "parse_hitl_fields",
    "prepend_hitl_header",
    "stimulus_for_scenario",
    "temporary_variant",
    "validate_stimulus",
]
