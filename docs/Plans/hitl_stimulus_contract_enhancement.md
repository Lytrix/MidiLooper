# HITL stimulus contract

**Status:** Stage 3 in tree. Stage 4 not started.  
**Kind:** enhancement  
**Does not start:** firmware changes, YAML-as-authority, `.mid` playback, occupy/base rewrite, compose Stage 2

Live USB MIDI stays the test mechanism. This layer names the
**input recipe** so a capture can prove which experiment ran.

## Nouns (HITL host only)

| Term | Meaning | Owner today |
|---|---|---|
| **Preset** | Bundle of scenario phases (`--preset note_edit_overdub_compose`) | `registry.PRESET_SCENARIOS` |
| **Scenario** | Runtime phase that sends MIDI and reads serial | `scripts/hitl/scenarios/` |
| **Flow / canonical args** | Existing MIDI generators | `scripts/hitl/flows/`, `baseline_canonical_args.py` |
| **Stimulus** | Versioned, named recipe: which generators, bars, interactions, and `must_prove` checks. Not a new MIDI sender. | `scripts/hitl/stimuli/` |

Do not replace scenario or preset. Do not add a firmware `Stimulus` type.

## Architecture

```text
plan required_stimulus
        │
        ▼
stimulus registry  (id + version + kind)
        │
        ▼
scenario uses existing Python MIDI generators
        │
        ▼
#HITL header in the host serial snapshot + JSON report
        │
        ▼
stimulus validation  →  firmware assertions  →  human checklist
```

`#HITL,...` lines are **host-written**. They go on the scenario serial
snapshot and the JSON report. They are not injected into the raw Teensy
`#CAP` session log (device parsers stay firmware-only).

## Stage 1 (this slice)

- Registry + fingerprint + validation helpers
- Canonical `STIM-BASE-2-ONE-OD-01` v1 (2-bar record + one overdub)
- Canonical `STIM-NE-OD-COMPOSE-01` v1 (that seed + NOTE_EDIT + in-edit overdub + move)
- Compose scenario prepends `#HITL` lines, runs stimulus validation, prints the human checklist
- Plan `required_stimulus` on the compose bugfix
- Agent rule: do not change a canonical stimulus unless it is proven insufficient; TEMP variants only

Version starts at **1**. That is the first formalization of the existing
compose HITL, not a rewrite of the MIDI pattern.

## Stage 2 (this slice)

Register existing recipes. Generators stay where they are.

| Stimulus | Scenario | Generator |
|---|---|---|
| `STIM-BASE-2-2-01` v1 | `base` | `canonical_baseline_legacy_args` |
| `STIM-OCCUPY-PAINT-01` v1 | `overdub_occupy_paint` | `canonical_occupy_paint_legacy_args` |
| `STIM-EDIT-OD-DURING-NE-01` v1 | `edit_overdub_during_note_edit` | `scenario:edit_overdub_during_note_edit` |

Host snapshots for occupy / edit-overdub / default `base` now start with `#HITL`. The compose seed still uses `STIM-BASE-2-ONE-OD-01`. Record-only edit seeds are not labeled as BASE-2-2.

## Stage 3 (this slice)

YAML is a **view** of `scripts/hitl/stimuli/registry.py`. The CLI does not
load YAML to define canonical stimuli. TEMP variants write a sidecar under
`captures/stimuli/` and are never registered.

```bash
.venv/bin/python scripts/host_midi_hitl.py stimulus export-yaml [--out PATH]
.venv/bin/python scripts/host_midi_hitl.py stimulus temp STIM-NE-OD-COMPOSE-01 A
.venv/bin/python scripts/host_midi_hitl.py run --preset note_edit_overdub_compose \
  --stimulus-temp-suffix A
```

`--stimulus-temp-suffix` forks the last bound scenario stimulus on that
preset (compose, not the seed). Pass `--stimulus-temp-parent` to fork a
different canonical id.

## Later

| Stage | What |
|---|---|
| 4 | `--list-stimuli` in everyday agent prompts; more BASE-N primitives |

## Do not

- Generate or load `.mid` files
- Copy a scenario to make a stimulus
- Edit `STIM-NE-OD-COMPOSE-01` to chase a new bug — add `STIM-NE-OD-COMPOSE-01-TEMP-A`, then promote only if it becomes the reproducer
- Treat missing `#HITL` headers on pre-fingerprint captures as a firmware FAIL
- Load YAML as a second stimulus authority
- Register a TEMP variant in `registry.py`

## Pre-implementation review

### Ready
- `temporary_variant` already copies a canonical spec and does not register it
- `runner.main` only handled `run`; a `stimulus` subparser can sit beside it
- No PyYAML in the repo; stdlib YAML dump is enough for a view

### Resolved (user / code)
| Topic | Decision |
|---|---|
| YAML authority | View only. Python registry remains the source of truth |
| TEMP CLI | Sidecar under `captures/stimuli/`; not registered |
| Run attach | `--stimulus-temp-suffix` overrides the matching parent only |
| Default parent | Last scenario on the preset that has a bound stimulus |

### Open before coding
None.

### Proceed?
- YES — host-only Stage 3; no firmware, no compose Stage 2, no Stage 4
