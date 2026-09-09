"""Host-written #HITL fingerprint lines for a stimulus run."""

from __future__ import annotations

from hitl.stimuli.spec import StimulusSpec

HITL_PREFIX = "#HITL"


def format_hitl_header(spec: StimulusSpec, *, scenario_version: int = 1) -> list[str]:
    overdub_summary = ",".join(
        f"{row.name}:{row.pattern}:{row.bars}" for row in spec.overdubs
    )
    interactions = ",".join(spec.interactions) if spec.interactions else "none"
    lines = [
        f"{HITL_PREFIX},SCENARIO,{spec.scenario_id}",
        f"{HITL_PREFIX},SCENARIO_VERSION,{scenario_version}",
        f"{HITL_PREFIX},STIMULUS,{spec.stimulus_id}",
        f"{HITL_PREFIX},STIMULUS_VERSION,{spec.version}",
        f"{HITL_PREFIX},STIMULUS_KIND,{spec.kind}",
        f"{HITL_PREFIX},BASE,bars={spec.base_bars},pattern={spec.base_pattern}",
        f"{HITL_PREFIX},OVERDUBS,{overdub_summary or 'none'}",
        f"{HITL_PREFIX},INTERACTION,{interactions}",
        (
            f"{HITL_PREFIX},TIMING,tempo={spec.tempo_bpm:g},"
            f"clock={spec.clock_ppqn} PPQN"
        ),
    ]
    if spec.base_stimulus_id:
        lines.append(f"{HITL_PREFIX},BASE_STIMULUS,{spec.base_stimulus_id}")
    if spec.parent_id:
        lines.append(f"{HITL_PREFIX},PARENT,{spec.parent_id}")
    return lines


def format_fingerprint_text(spec: StimulusSpec) -> str:
    rows = [
        f"STIMULUS  id: {spec.stimulus_id}",
        f"  version: {spec.version}",
        f"  kind: {spec.kind}",
        f"  scenario: {spec.scenario_id}",
        f"BASE  bars: {spec.base_bars}  pattern: {spec.base_pattern}",
    ]
    for row in spec.overdubs:
        band = ""
        if row.note_low is not None and row.note_high is not None:
            band = f"  notes: {row.note_low}-{row.note_high}"
        rows.append(f"{row.name}  bars: {row.bars}  pattern: {row.pattern}{band}")
    if spec.interactions:
        rows.append("INTERACTION  " + ", ".join(spec.interactions))
    rows.append(f"TIMING  tempo: {spec.tempo_bpm:g}  clock: {spec.clock_ppqn} PPQN")
    return "\n".join(rows)


def prepend_hitl_header(lines: list[str], spec: StimulusSpec) -> list[str]:
    header = format_hitl_header(spec)
    if lines[: len(header)] == header:
        return list(lines)
    return header + list(lines)


def parse_hitl_fields(lines: list[str]) -> dict[str, str]:
    fields: dict[str, str] = {}
    prefix = f"{HITL_PREFIX},"
    for line in lines:
        text = line.strip()
        if not text.startswith(prefix):
            continue
        parts = text.split(",", 2)
        if len(parts) < 3:
            continue
        fields[parts[1]] = parts[2]
    return fields
