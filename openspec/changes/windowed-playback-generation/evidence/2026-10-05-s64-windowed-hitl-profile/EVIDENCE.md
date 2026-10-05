# Task 6.4 — the windowed HITL profile: registered and authorized, not yet exercised

Evidence for `EXECUTION.md` in this directory. Task 6.4 asks for a dedicated **windowed** HITL profile through the
existing content-verification pipeline, and states its own separation: *"Profile/oracle preparation may precede rolling
implementation; registration, authorization, exercise and successful qualification remain distinct."* This slice
delivers the **registration and authorization** half, host-tooling only. No firmware, no build, no upload, no device
action.

## What is registered

`STIM-WINDOW-CONTENT-01@v1` in `scripts/hitl/stimuli/registry.py`, bound to the scenario id `windowed_content_parity`
and visible through the canonical surface:

```
STIM-WINDOW-CONTENT-01@v1  windowed_content_parity  scenario:windowed_content_parity  targets=track 8 slots 4,5  Task 6.4's windowed leg: a 16-bar loop with a committed overdub revision, played with the transport running across window boundaries -- F-7's content parity across a boundary, F-8's root-demoted residency swept over loop lengths, and the AC-8 readiness receipts under transport
```

- **Recipe shape** (what makes it a profile of its own): `base_bars=16` with one 16-bar `OD1` overdub, against the lane's
  two-bar span — the served window is a strict sub-interval and the loop carries a second committed revision.
  `base_stimulus_id` is `None`: no registered profile shares this shape, and naming one would claim a provenance the
  tree does not support.
- **Interactions:** `transport_running_across_window_boundary`, `loop_length_sweep_4_16`, `windowed_playback_under_transport`.
- **Declared oracle (`must_prove`)** — four terms, each naming an outcome the firmware or the pipeline already produces:
  `content_parity_across_window_boundary` (F-7), `root_demoted_residency_over_loop_lengths` (F-8), and
  `readiness_receipts_under_transport` + `windowed_service_coverage_under_transport` (AC-8's `#CAP,<us>,DIAG,win_readiness,…`
  receipts, emitted *directly when USB has room* because the ring is retained under transport, and the
  `#CAP,<us>,DIAG,win_gap,…` coverage blocks).
- **Authorization:** the declared manifest is the bench scope — `track 8 slots 4,5`, source the standing 2026-09-30
  grant as narrowed to that bench target. A session needing a different scope **forks** the profile rather than widening
  the canonical declaration (the registry's own rule).

## What is deliberately absent

The scenario that would **drive** this leg is not registered (`hitl/registry.py`), and `PRESET_SCENARIOS` carries no
entry for it, so `resolve_scenarios(preset="windowed_content_parity")` raises `ValueError` and
`list_scenarios()` does not contain `windowed_content_parity`. That is the point of the split: **registration and
authorization are done; exercise is not**, and no run can select this leg and be read as though it had been exercised.
The pin below asserts that absence on purpose, so it turns red the day the driver lands.

## The pin, and its red-proofs

`scripts/test_hitl_stimulus_contract.py::test_the_windowed_leg_has_a_registered_profile_and_no_driver_yet` asserts the
identity/version/kind/generator, the recipe shape, the four declared-oracle terms exactly, the declared scope's text and
provenance, and the driver's absence.

| ID | Change (host tooling) | Result |
|---|---|---|
| **MR-6.4a** | the declared scope is widened (`slots=(4,5)` → `(4,5,6)`) | the pin fails: `'track 8 slots 4,5' != 'track 8 slots 4,5,6'`; every other test in the suite stays green |
| **MR-6.4b** | the recipe shape is moved back to an ordinary 2-bar loop (`base_bars=16` → `2`) | the pin fails: `16 != 2`; every other test stays green |

`scripts/hitl/stimuli/registry.py` was restored **byte-identically** after each; the recorded pre-mutation hash for the
uncommitted file is `e90a637e35bf3cfb…` (`sha256`), and the file matched it again after the restore and again before the
commit.

## Verification

Host suites that touch the registry (run from `scripts/` with `PYTHONPATH=.`):

| Suite | Result |
|---|---|
| `test_hitl_stimulus_contract` | 26 tests, OK |
| `test_hitl_stimulus_authorization` | 24 tests, OK |
| `test_stimulus_yaml_view` | 3 tests, OK |
| `test_accepted_sequence_scenario` | 40 tests, OK |
| `test_accepted_sequence` | 194 tests, OK |
| `test_note_edit_overdub_compose_serial_verify` | 45 tests, OK |
| `test_baseline_canonical_args` | 3 tests, OK |
| `test_content_deduction` | 15 tests, OK |

- **Invocation note, recorded because it cost a false alarm:** `python -m unittest scripts.test_baseline_canonical_args`
  fails with `ModuleNotFoundError: No module named 'hitl'` — a path artifact of the `scripts.` module prefix, not the
  change. Run from `scripts/` with `PYTHONPATH=.`, the same suite is **3 tests, OK**, as tabled above.
- `pio test -e native` — **2796 cases / 2789 succeeded / 7 skipped / 0 failed** (the C++ tree is untouched; run for the
  record).
- `openspec validate windowed-playback-generation --strict` — valid. `git diff --check` — clean.
- **Device: NOT RUN.** No upload was requested or performed, so no device behaviour is claimed.

## Not claimed

- That the profile has been **used**: no scenario/driver exists yet, so F-7, F-8 and the AC-8 receipts are **not**
  verified — the profile declares the oracle those legs will be read against.
- Any **qualification**: 6.4 stays **open** until the profile is exercised (registration, authorization, exercise and
  qualification are separate in the task's own words), and 6.5's hardware half stays **NOT RUN** (it needs this profile
  **and** an authorized upload).
- Any change to a canonical stimulus: none was edited, and none is edited to chase a condition.
- Anything about production behaviour: no firmware, platform or host-driver line was changed; this slice is a
  registration plus its pin.

## Next

1. The **driver** for `windowed_content_parity`: a scenario (`run`/`verify`) plus the `PRESET_SCENARIOS` entry, so the
   profile can be exercised — then this pin's absence clause becomes false, which is its purpose.
2. Task **6.5**'s hardware half under a dedicated HITL preset, after **upload authorization**.
