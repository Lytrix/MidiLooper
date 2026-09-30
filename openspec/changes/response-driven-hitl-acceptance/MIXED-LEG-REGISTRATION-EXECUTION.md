# Execution record — the mixed leg's registration and stimulus profile (task 5.1, registration half)

Slice: `response-driven-hitl-acceptance` task 5.1's *Mixed record predecessor and overdub successor* leg,
registration half. Landed 2026-10-01. Host tooling only: no firmware file touched, no build, no upload, no
device action.

Authority: task 5.1's mixed-leg clause. The leg's flow half (`leg_gestures("mixed")`, its own preparation branch,
the composed record + overdub steps) and its grading mode (`analyse(leg="mixed")`: A read with the record
vocabulary beside B with the overdub families) landed 2026-09-30 and are recorded in
[MIXED-LEG-EXECUTION.md](MIXED-LEG-EXECUTION.md). What was still open there — and what this slice closes — is
that the leg was **drivable through the wrapper CLI only**: no registered scenario reached it, and its stimulus
profile (a different preparation from either existing leg's) was not declared, so a mixed run could not be
authorized the way 4.1 requires (by a profile's declared scope).

## What landed

| Site | What it now says |
|---|---|
| `hitl.scenarios.accepted_sequence.LEG_FOR_SCENARIO` | `accepted_sequence_mixed → "mixed"`, beside the record and overdub legs. The leg still comes from the **registration**, never from `--leg`, so a `--leg od` through the mixed scenario stays a named `UnsupportedCombination`. |
| `hitl.scenarios.accepted_sequence.PROFILE_FOR_SCENARIO` | `accepted_sequence_mixed → STIM-BASE-2-MIXED-01`: the run's authorization is the profile's declared scope, exactly as for the other two legs. |
| `hitl.registry` | `ScenarioSpec("accepted_sequence_mixed")` — the same `run`/`verify` pair (the stateless flow + the offline oracle), `owns_capture=True`, so the runner steps out of managed capture for it; and the `accepted_sequence` preset now drives **three** legs in order (rec, od, mixed). |
| `hitl.stimuli.registry` | The `STIM-BASE-2-MIXED-01` profile, registered in `_STIMULI` and exported from `hitl.stimuli`. |

## The profile's declaration, and why it is its own

The requirement *Scoped same-boot fixture preparation* ends with a constraint the mixed leg is the first to
exercise literally: "Record and overdub fixtures MUST have different verified content preconditions." The mixed
leg's two targets are exactly that pair — slot A is the **record** target and must be verified empty *and idle*
before the recipe's start press (`require_idle`; an armed or recording target would consume the press), and slot
B is the **overdub** target and must carry committed, verified content whose seed receipt is confirmed before it
is overdubbed (`seed_take`). So the profile is declared rather than borrowed:

- `scenario_id="accepted_sequence_mixed"`, `kind=canonical`, `version=1`;
- `generator="flow:accepted_sequence(mixed)"` — its predecessor/successor shape is the driver's, not a legacy
  argument recipe's, and the field is descriptive (the list view prints it);
- `description` states both preconditions by name (the record predecessor "empty and idle", the successor's
  "committed content verified"), so the declaration cannot drift from what the driver prepares;
- `authorization=_bench_authorization()` — the same declared bench scope (track 8, slots 4 and 5) the other two
  legs use, with its provenance quoted. No target is invented at run time.

The run's own stimulus id is unchanged: `_report_stimulus_id("mixed")` already named
`STIM-BASE-2-MIXED-01-TEMP-ACCEPTED-SEQ-MIXED`, the **temp variant forked from the profile** — which is now a
registered profile rather than a name it would take. Its comment (and the wrapper's provenance paragraph) were
corrected to say so.

## Evidence

- `test_accepted_sequence_scenario` — the registration tests now cover **three** legs: each is registered with
  the oracle as its verifier and declared capture-owning; each names a registered profile whose declared scope
  covers the bench targets; the preset's order is `(rec, od, mixed)`; the listing shows all three; and
  `PROFILE_FOR_SCENARIO`/`LEG_FOR_SCENARIO` are pinned **verbatim**, so a leg cannot be pointed at another leg's
  profile without failing.
- The runner-route pin (`test_the_runner_route_reaches_the_mixed_leg_with_its_own_profile`) drives
  `accepted_sequence_argv` for the mixed scenario: `--leg mixed` under `--authorization STIM-BASE-2-MIXED-01`,
  out-dir `…/accepted-sequence-mixed`, and a `--leg od` through the same scenario refused by name. That is
  precisely the gap the previous slice named.
- `test_hitl_stimulus_contract` — a new pin on the profile itself: identity, kind, version, generator, the
  declared target text (`track 8 slots 4,5`), the two preconditions stated in the description, and that it is
  *not* either other leg's profile.
- Suites green together: the scenario registration/translation suite **40/40** (two new pins),
  `test_hitl_stimulus_contract`, `test_hitl_stimulus_authorization` (the bench-scope loop now includes the
  mixed profile), `test_stimulus_yaml_view` (the export carries the new declaration), `test_hitl_import_gate`
  **5/5** — 97 tests in that group, `test_accepted_sequence` **151/151** unchanged, and the whole host sweep
  **1,085 tests / 75 suites** with only the six pre-existing failing suites (none of which imports this
  change's modules).
- **6 mutations red-proved** with byte-identical, sha256-verified restores (`mutate_reg.py`): the `ScenarioSpec`
  not registered; the mixed leg dropped from the preset; the leg dropped from `LEG_FOR_SCENARIO`; the leg pointed
  at the *record* profile; the profile not registered in `_STIMULI`; and the profile declaring **no**
  authorization. Each failed the registration suite; nothing survived.

## What remains for the mixed leg

1. the **per-interval content parity** edge — the spec's gates closed end to end on one *driven* capture, with
   the content-pipeline inventory for **both** intervals (the half 5.2 integrated at library level);
2. the **device leg**, with the rest of 6.x, under a per-session upload/target authorization.

Registration makes the leg reachable and authorized through the runner; it does not qualify the hardware, and no
slice of this change has driven the board.
