# Task 6.4 — the windowed HITL profile: registration and the declared oracle

Execution packet, written **before** the edits. Authority:
[IMPLEMENTATION_READINESS.md](../../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice),
[`docs/Templates/EXECUTION_PACKET.md`](../../../../docs/Templates/EXECUTION_PACKET.md), task 6.4's own wording
(*"Profile/oracle preparation may precede rolling implementation; registration, authorization, exercise and successful
qualification remain distinct"*), [`docs/Guides/HITL_CONTENT_VERIFICATION.md`](../../../../docs/Guides/HITL_CONTENT_VERIFICATION.md)
and `design.md`'s content-verification row (*"fed the windowed/demoted cases (new stimulus profile), not replaced"*).

## Identity and scope

- **Slice:** task 6.4's **registration and authorization** half, host-tooling only — a dedicated **windowed** HITL
  stimulus profile, registered in the canonical registry, carrying its **target authorization manifest**, its recipe
  shape and its **declared oracle**. No firmware, no build, no upload, no device action.
- **Owner steer:** *"then prepare 6.4 (registered device profile) → 6.5 (controlled device qualification)"*.
- **Primary files:** `scripts/hitl/stimuli/registry.py`, `scripts/hitl/stimuli/__init__.py`,
  `scripts/test_hitl_stimulus_contract.py`, and this directory.
- **Non-goals, stated rather than implied:** **no** scenario/driver (`run`/`verify`), **no** `PRESET_SCENARIOS` entry,
  **no** device run and **no** F-7/F-8/AC-8 qualification claim; no canonical stimulus is edited to chase a condition;
  no production line touched; nothing about 6.5's hardware half is claimed.

## Why a profile of its own, and not a reuse

The legs this profile exists for are about a **shape** no registered profile has: the loop is far **longer** than the
served window (16 bars against a two-bar span, so a window is a strict sub-interval), it carries a **second committed
revision**, and the transport keeps **running across** the boundaries — the only condition under which a boundary can be
crossed at all. That is `S4C45` F-7 (*content parity across a window boundary*) and F-8 (*length-independent root
residency*, whose own text says a single bar count cannot be the oracle). The AC-8 receipt the profile also declares
(`#CAP,<us>,DIAG,win_readiness,...`) is emitted *directly when USB has room* precisely because the ring is retained
under transport, so it too needs a running transport to be observed where it is produced.

## Oracle fixed before behavior edits

| Item | Declared value | Why this value |
|---|---|---|
| id / version / kind | `STIM-WINDOW-CONTENT-01` @ v1, `canonical` | the registry's own convention (`STIM-<TOPIC>-<…>-NN`); a temporary variant is the experiment path |
| scenario id | `windowed_content_parity` | the leg is addressed by the scenario it *is*; no preset resolves it yet (see below) |
| generator | `scenario:windowed_content_parity` | the `scenario:…` form the stage-2 profiles already use |
| recipe shape | `base_bars=16`, one 16-bar `OD1` overdub, `base_pattern="WINDOW-LONG-16"` | loop ≫ window span, with a second revision |
| interactions | `transport_running_across_window_boundary`, `loop_length_sweep_4_16`, `windowed_playback_under_transport` | the F-8 sweep needs **more than one** loop length; the transport must run |
| `must_prove` | `content_parity_across_window_boundary`, `root_demoted_residency_over_loop_lengths`, `readiness_receipts_under_transport`, `windowed_service_coverage_under_transport` | each names an outcome the firmware/pipeline already produce: F-7's parity verdict, F-8's per-slot residency, AC-8's `DIAG,win_readiness` rows under transport, and the `DIAG,win_gap` coverage blocks |
| authorization | `track 8 slots 4,5`, source = the standing 2026-09-30 grant narrowed to the bench target | the registry's own rule: a session needing a different bench scope **forks** the profile rather than widening the canonical declaration |
| `base_stimulus_id` | `None` | no registered profile shares this recipe shape; naming one would be a provenance claim the tree does not support |

- **The half that is deliberately absent:** the profile's `scenario_id` is bound in `SCENARIO_STIMULUS`, but the
  scenario is **not** registered in `hitl/registry.py` and `PRESET_SCENARIOS` carries **no** entry, so
  `resolve_scenarios(preset="windowed_content_parity")` raises. That is 6.4's own separation, made checkable: this
  slice registers and authorizes; it does not exercise.
- **Preserved invariants:** canonical stimuli untouched; the registry remains the authorization authority; the
  fingerprint/header format unchanged; no production or firmware line edited.

## Controls and coverage

- **Reused seams:** `StimulusSpec` / `OverdubRecipe` / `Authorization` (the declared manifest), `stimulus_for_scenario`,
  `get_stimulus`, `list_stimuli` / the `stimulus list` CLI, `list_scenarios`, `resolve_scenarios`,
  `export_registry_yaml`.
- **The pin:** `test_the_windowed_leg_has_a_registered_profile_and_no_driver_yet` asserts the identity, the recipe
  shape, the declared oracle, the declared scope, and that the driver's absence is still true (the reminder that turns
  red when the driver lands).
- **Red-proofs planned:** MR-6.4a widens the declared scope (slots 4,5 → 4,5,6); MR-6.4b moves the recipe shape back to
  the ordinary 2 bars. Each must fail that pin and be restored **byte-identically**.

## Verification recipe

- Host suites that touch the registry, run from `scripts/` with `PYTHONPATH=.`: `test_hitl_stimulus_contract`,
  `test_hitl_stimulus_authorization`, `test_stimulus_yaml_view`, `test_accepted_sequence_scenario`,
  `test_accepted_sequence`, `test_note_edit_overdub_compose_serial_verify`, `test_baseline_canonical_args`,
  `test_content_deduction`.
- `pio test -e native` (unchanged tree for C++/firmware; run for the record);
  `openspec validate windowed-playback-generation --strict`; `git diff --check`.
- **Device: NOT RUN** — no upload was requested or performed.

## Completion record

- [x] Profile registered and authorized, oracle declared: `STIM-WINDOW-CONTENT-01@v1` (`windowed_content_parity`),
  16-bar loop + one 16-bar revision, declared scope `track 8 slots 4,5`, four `must_prove` terms naming F-7 / F-8 / AC-8
  outcomes. `base_stimulus_id=None` (no provenance is claimed).
- [x] Pin added (`test_the_windowed_leg_has_a_registered_profile_and_no_driver_yet`); **MR-6.4a** (scope widened to
  4,5,6) and **MR-6.4b** (`base_bars` 16 → 2) each red-prove it, with the pin the **only** failing test in both runs and
  the file restored byte-identically (`sha256 e90a637e35bf3cfb…` before and after).
- [x] Host suites green (26 / 24 / 3 / 40 / 194 / 45 / 3 / 15 tests); native **2796 / 2789 succeeded / 7 skipped / 0
  failed**; `openspec validate … --strict` valid; `git diff --check` clean.
- [x] Docs: `EVIDENCE.md` in this directory; task record updated.
- [x] Task checkoff: **6.4 stays open** — registration and authorization are done, **exercise and qualification are not**
  (no driver, no device run), exactly as the task separates them.
