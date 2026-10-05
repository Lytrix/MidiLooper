# HITL test recipe (fill in before writing code)

Companion: [hitl_authoring_traps.md](../Agents/hitl_authoring_traps.md). Pairs with
[EXECUTION_PACKET.md](EXECUTION_PACKET.md); do not duplicate its fields.

| Field | Fill in |
|---|---|
| Acceptance criterion (one sentence) | |
| Reused stimulus / flow (`stimulus list`, `scripts/hitl/flows/`) | |
| New stimulus? If yes, variant name (never edit a canonical one) | |
| Gestures used (only helpers from `scripts/hitl/actions/`; constants from `control_constants.py`) | |
| Expected markers | one row each: tag, tier (A / other), max rate, emitted-from (file:function) |
| Marker tier test | `test_capture_line_tier` case added: yes / n.a. |
| `check_marker_contract.py` output (EMITTED / TIER-A / FIT) | paste the OK lines |
| RAM1 impact | instrumentation gated? padding before -> after (from build output) |
| Native test that fails without the change | suite + case |
| Boot-census / smoke check | command, expected line |
| Full scenario entry point | baseline preset or wrapper script; timeout |
| Verdict rule | which marker values pass; what "missing" vs "displaced" means |
| Evidence to record | log tail path, parse helper used |

Order of work: native test -> build + RAM1 line -> smoke check -> full scenario.
