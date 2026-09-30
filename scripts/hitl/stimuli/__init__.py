"""HITL stimulus registry: versioned live-MIDI recipes, not a second generator."""

from hitl.stimuli.attach import (
    print_human_checklist,
    print_stimulus_banner,
    run_stimulus,
    stimulus_report_fields,
    write_headered_snapshot,
)
from hitl.stimuli.fingerprint import (
    format_fingerprint_text,
    format_hitl_header,
    parse_hitl_fields,
    prepend_hitl_header,
)
from hitl.stimuli.registry import (
    BENCH_SLOTS,
    BENCH_TRACK,
    STIM_BASE_2_2_01,
    STIM_BASE_2_FIXTURE_01,
    STIM_BASE_2_ONE_OD_01,
    STIM_EDIT_OD_DURING_NE_01,
    STIM_NE_OD_COMPOSE_01,
    STIM_NE_OD_COMPOSE_02,
    STIM_NE_OD_COMPOSE_03,
    STIM_NE_OD_COMPOSE_04,
    STIM_NE_RESELECT_STACKED_01,
    STIM_OCCUPY_PAINT_01,
    authorization_for,
    format_stimulus_list_lines,
    get_stimulus,
    list_stimuli,
    stimulus_for_base_preset,
    stimulus_for_scenario,
    temporary_variant,
)
from hitl.stimuli.session import (
    get_run_stimulus_override,
    resolve_run_stimulus,
    set_run_stimulus_override,
)
from hitl.stimuli.authorization import (
    CLI_SOURCE,
    AuthorizationRefused,
    authorize_targets,
)
from hitl.stimuli.spec import Authorization, StimulusSpec
from hitl.stimuli.validate import format_human_checklist, validate_stimulus

__all__ = [
    "Authorization",
    "AuthorizationRefused",
    "BENCH_SLOTS",
    "BENCH_TRACK",
    "CLI_SOURCE",
    "STIM_BASE_2_2_01",
    "STIM_BASE_2_FIXTURE_01",
    "STIM_BASE_2_ONE_OD_01",
    "STIM_EDIT_OD_DURING_NE_01",
    "STIM_NE_OD_COMPOSE_01",
    "STIM_NE_OD_COMPOSE_02",
    "STIM_NE_OD_COMPOSE_03",
    "STIM_NE_OD_COMPOSE_04",
    "STIM_NE_RESELECT_STACKED_01",
    "STIM_OCCUPY_PAINT_01",
    "StimulusSpec",
    "authorization_for",
    "authorize_targets",
    "format_fingerprint_text",
    "format_hitl_header",
    "format_human_checklist",
    "format_stimulus_list_lines",
    "get_run_stimulus_override",
    "get_stimulus",
    "list_stimuli",
    "parse_hitl_fields",
    "prepend_hitl_header",
    "print_human_checklist",
    "print_stimulus_banner",
    "resolve_run_stimulus",
    "run_stimulus",
    "set_run_stimulus_override",
    "stimulus_for_base_preset",
    "stimulus_for_scenario",
    "stimulus_report_fields",
    "temporary_variant",
    "validate_stimulus",
    "write_headered_snapshot",
]
