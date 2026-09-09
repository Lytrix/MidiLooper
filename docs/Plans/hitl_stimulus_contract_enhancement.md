# HITL stimulus contract

**Status:** Stage 2 in tree. Stage 3–4 not started.  
**Kind:** enhancement  
**Does not start:** firmware changes, YAML fixture files, `.mid` playback, occupy/base rewrite, compose Stage 2

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

## Later

| Stage | What |
|---|---|
| 3 | Optional YAML view of the same registry; TEMP variant CLI |
| 4 | `--list-stimuli` in everyday agent prompts; more BASE-N primitives |

## Do not

- Generate or load `.mid` files
- Copy a scenario to make a stimulus
- Edit `STIM-NE-OD-COMPOSE-01` to chase a new bug — add `STIM-NE-OD-COMPOSE-01-TEMP-A`, then promote only if it becomes the reproducer
- Treat missing `#HITL` headers on pre-fingerprint captures as a firmware FAIL
