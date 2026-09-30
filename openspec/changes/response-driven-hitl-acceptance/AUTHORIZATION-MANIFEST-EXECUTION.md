# Execution record — run-scoped target authorization manifest (4.1, host half)

Slice: `response-driven-hitl-acceptance` task 4.1, host half. Landed 2026-09-30. Firmware untouched.
Authority: [design.md](design.md) decision 4 ("Preparation accepts explicit track/slot targets and a
run-scoped authorization manifest"), task 4's precondition ("target authorization manifest … required run
inputs"), the packet's [ownership audit](G2-EXECUTION-PACKET.md) decision routing the manifest to the
stimulus registry, and [spec.md](specs/hitl-automation/spec.md) requirement *Scoped same-boot fixture
preparation* (scenario *Target outside the run's authorization*).

## Why this is a slice and not a footnote

Task 4.1's own clause is *"verify SD residue, wrong target, failed save and **unauthorized targets stop
before further mutation**"*. Before this slice there was nothing to check a target *against*: the drivers
took `--track/--slot-a/--slot-b` and used them, so "authorized" existed only in prose (a docstring, a help
string, and the destructive-operations rule). A run pointed at the wrong slot cleared it.

## What landed

**The declaration lives with the recipe** (`hitl/stimuli/spec.py`, `registry.py`).

- `Authorization(track, slots, source, notes)` — 1-based user-facing numbers, range-checked, duplicate
  slots refused, and `source` **required**: an authorization nobody can trace is not one. An empty `slots`
  is a deliberate read-only profile, so "this profile may not mutate anything" is expressible.
- `violations(track=…, slots=…)` names the offending target *and* the declared scope
  (`slot 7 is not authorized on track 8 (authorized slots: 4,5)`); `allows` mirrors it.
- `StimulusSpec.authorization` is optional. A profile that declares nothing cannot authorize a run: the
  checker refuses it by name instead of falling back to "no declaration, no check".
- `registry.py` declares the scope once for the two profiles these legs drive — track 8, slots 4/5, the
  bench target the recorded `s4f13-*`/`s4f22-dec096-*` runs actually used, narrowed from the user's standing
  authorization of 2026-09-30 (`BENCH_TRACK`/`BENCH_SLOTS`/`BENCH_SCOPE_SOURCE`, and `_bench_authorization()`
  so the two declarations cannot drift). A session that needs another bench scope forks the profile
  (`temporary_variant(..., authorization=…)`) rather than widening the canonical declaration.
- `stimulus list` — the mandated discovery path — now shows `targets=track 8 slots 4,5` or
  `targets=undeclared` per profile, and the YAML view exports `authorization:` as an indented block
  sequence (the exported `slots` is a sequence inside a mapping, which the view's own regression suite
  checks structurally for every spec).

**The enforcement is one module** (`hitl/stimuli/authorization.py`).

- `authorize_targets(value, track=…, slots=…)` returns the record the run is driven under:
  a **manifest** record (source, stimulus id, fingerprint, the declaration echoed, the run's targets) or
  the **`cli`** record, which says explicitly that no manifest was consulted and this run's scope is only
  as wide as its arguments.
- Refusals are records, not tracebacks: a target outside the manifest, an id the registry does not know,
  and a profile that declares nothing all raise `AuthorizationRefused` carrying the violation text and the
  record, because the refusal is the evidence the slice exists to produce.

**Both drivers stop before the board** (`hitl_accepted_sequence.py`, `hitl_retention_handoff.py`).

- The authorization is resolved at the top of `main`, **before** the latch-refusing reset, the re-upload,
  the read-only census and any MIDI gesture; a refusal writes `result = "UNAUTHORIZED_TARGET"` with the
  refused target and the authorized scope into the same report the driver writes on every other exit, and
  exits 3.
- `--authorization <stimulus-id>|cli` (default `cli`) is the new run input; `cli` keeps every existing
  invocation working and records itself as the compatibility path.

**The record reaches the sealed bundle** (`hitl/accepted_sequence_probe.py`,
`hitl/reporting.py::build_report_bundle`). The oracle copies the run's `authorization` record into the
bundle's `authorization` section, so a sealed verdict names the scope it was driven under.

## Evidence

| Gate | Result |
|---|---|
| New `scripts/test_hitl_stimulus_authorization.py` | **24/24** (shape/ranges, registry declarations + listing + YAML view, the three checker inputs, both drivers' refusal) |
| `test_accepted_sequence` | **98/98** (1 new: the sealed bundle carries the authorization) |
| Neighbouring suites (`_corpus`, stimulus contract, YAML view, report bundle, import gate, retention plan, correlated actions, cancellation, command forwarding) | green |
| Full host sweep (`scripts/test_*.py`) | **987 tests / 74 suites**, only the **seven pre-existing unrelated failures**. The count is the sum of each suite's own `Ran N tests` line, so it is a floor: `test_foundation_runner`, `test_layered_verifiers` (both pre-existing failures) and `test_note_edit_fader_motor_timing` print none. |
| `pio test -e native` | **2,643 cases / 2,636 succeeded / 7 skipped / 0 failed** |
| Mutations | **6 red-proved**, every file restored byte-identically (sha256 before/after) |
| `openspec validate response-driven-hitl-acceptance --strict`, `git diff --check` | clean |

The driver tests are **two-sided**: the same targets that a manifest refuses reach the device step on the
`cli` path, so the manifest is what decides rather than a blanket refusal. The refusal tests raise a sentinel
in place of the reset step, so a revision that lost the guard fails loudly instead of flashing the board
inside a host test. Firmware is untouched by this slice, so the `teensy41-capture-serial` build and the
RAM1 floor are not implicated.

## What 4.1 still owes (device legs, needing per-session authorization)

Target selection, the RAM/SD census, the persistence-completion proof and the repeat-without-reboot
clause — each of which requires an explicit per-session upload/target authorization before it is run.
