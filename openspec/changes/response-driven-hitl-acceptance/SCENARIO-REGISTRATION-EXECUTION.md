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

## Remainder (4.4b) — named, with its ownership question

The design's stronger form is that the sequence lives in a **stateless flow** and the standalone driver
becomes a wrapper around the registered runner. That is a mechanical extraction of the leg body from
`scripts/hitl_accepted_sequence.py` (~700 lines: the plan, the correlated waits, the scoped preparation, the
census, the family read-back, the report assembly) into `hitl/flows/`, with the scenario declaring intent and
the driver reduced to argument translation — and it has one genuine design question that must be answered
first, not during the move: **who owns the family-counter read-back under the flow import gate**, since the
driver's `_read_family_counters` reaches the serial layer a flow may not import (`hitl.serial`) while a
scenario may not either. Until that owner is chosen, moving the body would either break the gate or leave two
owners of one observation. The legs' device behaviour is unchanged by this slice: the wrapper still drives
the sequence exactly as before, which is why every one of the 98 acceptance tests is untouched.
