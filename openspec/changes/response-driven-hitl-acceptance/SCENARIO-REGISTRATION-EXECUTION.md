# Execution record — scenario registration and the compatibility wrapper (4.4)

Slice: `response-driven-hitl-acceptance` task 4.4. Landed 2026-09-30. Host tooling only; firmware untouched.
Authority: [design.md](design.md) decision 4 ("Register separate record and overdub scenarios in the existing
registry … the standalone driver becomes an argument-compatible wrapper … with unsupported legacy
combinations rejected explicitly") and task 4.4's own clause.

## What landed, and what this slice deliberately did not

The task names five verifiable clauses. **Four of them are landed and pinned here; the fifth — the leg
sequence itself living in a stateless flow, with the driver reduced to a wrapper — is not, and is named as
the remainder.** The direction of travel is therefore *reported* to the runner, not yet *moved* into it:

| Clause | State |
|---|---|
| Register both scenarios | **landed** — `accepted_sequence_rec`, `accepted_sequence_od`, plus the `accepted_sequence` preset |
| Legacy argument translation | **landed** — `accepted_sequence_argv`, with an allowlist and refusals |
| Explicit unsupported combinations | **landed** — nine named refusals, each tested |
| Scenario listing | **landed** — `host_midi_hitl.py run --list-scenarios` |
| Identical verdicts via both entry points | **landed** — the oracle is the registered verifier; equal verdicts on the retained fixtures through the CLI too |
| Unchanged base/edit_full behaviour | **landed** — resolution, preset keys and the capture decision pinned; the two pre-existing failures re-confirmed identical |
| The standalone driver *becomes* a wrapper to the runner | **open** — requires the sequence to move into a stateless flow first (see the remainder below) |

## The registration

- `hitl/registry.py` gains two scenarios and a `accepted_sequence` preset. Both carry the **offline oracle**
  (`hitl.scenarios.accepted_sequence.verify_accepted_sequence`) as their verifier, so `--verify-only` through
  the runner re-derives the acceptance verdict from a log without a device.
- `ScenarioSpec` gains `owns_capture` (default `False`): a scenario whose sequence owns its own session
  declares it, and `runner._should_use_managed_capture` then steps the runner out of the way instead of
  wrapping the run in a second capture on the same port. The declaration is a property of the scenario, not a
  list of ids the runner happens to know.
- A run through the runner is authorized by the **profile's own manifest** (task 4.1):
  `accepted_sequence_rec` → `STIM-BASE-2-2-01`, `accepted_sequence_od` → `STIM-BASE-2-ONE-OD-01`. This is
  *stricter* than the standalone CLI's `cli` default, and an explicit `--authorization` still wins.
- `--layered` with a classic-registry id is now a **named refusal** in `foundation_runner` instead of a
  `KeyError` mid-run (the response-driven legs are classic-registry scenarios).

## The translation, and the refusals

`accepted_sequence_argv` produces one readable argv per run: the derived flags (`--leg`, `--track`,
`--slot-a` from `--loop-slot`, `--slot-b`, `--authorization`, a per-leg `--out-dir`), the allowlisted
wrapper flags verbatim (`--bars`, `--notes`, `--press-ms`, `--settle-s`, the per-phase deadlines, the retry
bounds, `--capture-port`), `--no-clear-targets`/`--boot-reset` when asked, and the upload decision (the
runner owns the upload when `--build-upload` was given, an explicit `--upload`/`--no-upload` wins).

Named refusals, all before any device action: a `--leg` that disagrees with the scenario; a missing `--track`,
`--loop-slot`, `--slot-b` or `--midi-channel`; a `--slot-a` that contradicts `--loop-slot`; the same target
twice; two different statements of the note channel; a note channel that is not the track's own (the leg
injects note input on the channel the firmware maps to the track it drives — `channel = track - 1`, the same
value `_run_plan` uses, so a different channel would record into an unprepared target); an unknown flag
(named, never silently dropped); a stray positional.

## Evidence

| Gate | Result |
|---|---|
| New `scripts/test_accepted_sequence_scenario.py` | **32/32** (registration, listing, translation, nine refusals, verdict identity, the scenario-layer import pin, unchanged classic behaviour) |
| `test_accepted_sequence` | **98/98**; corpus **3/3** |
| Full host sweep | **1,018 tests / 75 suites**, only the **seven pre-existing unrelated failures** |
| `test_foundation_runner`, `test_layered_verifiers` | re-confirmed **identical with this slice's files stashed** (`AssertionError: 2 != 0`; `'Namespace' object has no attribute 'overdub_step_clocks'`) |
| `pio test -e native` | **2,643 cases / 2,636 succeeded / 7 skipped / 0 failed** |
| Mutations | **10 red-proved**, every file restored byte-identically (sha256 before/after) |
| `openspec validate response-driven-hitl-acceptance --strict`, `git diff --check` | clean |

The verdict-identity check is two-sided on the retained fixtures: the excerpt (with its own provenance as
`metadata` and the best-case base report) is `ACCEPTED_SEQUENCE_PASS` through **both** the standalone oracle
CLI and `runner --verify-only`, and `s4f13-od-20260927_015145.log` is `INSUFFICIENT_EVIDENCE` through both.
The oracle keeps its own exit-code vocabulary (0/2/3/4) while the runner reports pass/non-pass (0/2); what is
pinned as identical is the *verdict* and the fact of passing, which is what 4.4 asks for. The verifier reads
the oracle's own evidence inputs (`--metadata`, `--base-report`) from the runner's arguments, because without
them a route would report a weaker verdict than the oracle on the same log and the two entry points would
disagree for a reason that has nothing to do with the log.

## 4.4b — the flow extraction (landed 2026-09-30)

The design's stronger form is now the implementation: the leg sequence lives in a **stateless flow**
(`hitl/flows/accepted_sequence.py`, entry `drive_leg`), the session/authorization/report live in a host
run scope (`hitl/accepted_sequence_run.py`, `RunRequest` → `run_accepted_sequence`), and
`scripts/hitl_accepted_sequence.py` is an **argument-compatible wrapper** (same flags, exit codes and
artifact layout) that builds a `RunRequest` and delegates.

**The ownership question is answered.** The stopped-only per-family counter read-back is the **session's**:
`ManagedCaptureSession.identity_stat_lines` owns the request, its fence and the decode (which stays in
`hitl.serial.protocol`, the serial owner), so the flow calls one accessor instead of importing `hitl.serial`
— the layer that is closed to both a flow and a scenario, and the reason the move could not simply carry
`_read_family_counters` along. `read_family_counters` in the flow is now a thin, non-fatal caller of that
accessor.

**Both entry points reach one implementation.** `host_midi_hitl.py run` → the registered scenario (intent +
the runner→CLI translation) → the wrapper → the run scope → the flow; the standalone CLI → the run scope →
the flow. The scenario is what owns the translation (`accepted_sequence_argv`), so neither path can drift.

**Rejected alternative, recorded:** routing the wrapper literally through `hitl.runner.main` would re-parse
an already-parsed, argument-compatible CLI and relocate the run's artifacts (the scenario writes
`<out>/accepted-sequence-<leg>`), changing a verified contract for no functional gain.

**How the move was verified as a move.** Every one of the 23 moved functions was diffed against
`git show HEAD:scripts/hitl_accepted_sequence.py`: 22 are byte-identical, and the 23rd differs only in one
docstring word (`driver` → `flow`). `_Ctx` deliberately gained a `press_ms` parameter so there is one
context stand-in rather than two. The diff found two *real* drifts, both fixed and now pinned by tests:

* the seed's arm gate must accept the printed `Loop N: Start Recording` row and a `RECORDING` state, not just
  an `ARMED` `DISP` row (measured `od-10`: the ring can lag the Serial row by seconds);
* the report's census keys are the census's own 0-based firmware `(track, slot)` pairs, not the run's
  1-based CLI numbering.

**Evidence.** `test_accepted_sequence` **100/100** (two new: the printed-row arm receipt, and a run that dies
after the census still leaving its milestone report — `od-08`'s lesson), `test_accepted_sequence_scenario`
**38/38** (six new flow-layer pins: the sequence lives in the flow and not in the CLI, the wrapper's argument
surface is unchanged, the flow imports nothing the layer may not, importing the flow loads no
verify/reporting layer, the counter read-back is the session's accessor, and the milestone hook is called
twice), `test_hitl_stimulus_authorization` **24/24** (repointed to the run scope's owner; the refusals are the
same), the full host sweep **1,025 tests** with only the seven pre-existing unrelated failures,
`pio test -e native` **2,643 cases (7 skipped, 2,636 succeeded)**, and **7 mutations red-proved** with
byte-identical restores. Two of those mutations *survived the first run* — the seed's arm wires and the
milestone write — which is exactly why those two tests exist now; the gap they exposed was real, not
hypothetical.
