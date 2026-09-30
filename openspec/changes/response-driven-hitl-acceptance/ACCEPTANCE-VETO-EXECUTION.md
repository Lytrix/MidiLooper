# Execution record — the conjunction's external halves and its vetoes (5.2)

Slice: `response-driven-hitl-acceptance` task 5.2. Landed 2026-09-30. Host tooling only; firmware untouched, no
build, no device action.

Authority: design decision 6 — *"Require matched input → recognition → accepted obligation → execution →
publication/retirement, continuous transport, **content parity and the existing timing gates**"*, *"Shutdown
result remains an independent field and can veto overall success"*, and *"PASS only for the complete
conjunction"*; plus task 5.2's own clauses (integrate, verify each vetoes, replay the corpus with deterministic
expected verdicts).

## What landed

| Clause | State |
|---|---|
| Integrate the existing content-parity verifier | **landed** — `hitl.library_manifest.compare_manifests`' own report (its `pass`/`verdict` plus the unexplained manifest/lifecycle differences and count differences) |
| Integrate the existing timing verifier | **landed** — `hitl.verify.midi_boundary_timing`, through the owner's own `midi_boundary_timing_issues(summary)`, so a newly-named issue is a veto without a second edit |
| Independent shutdown result | **landed** — the run's `hitl.device_shutdown` record, read as the `teardown` check |
| Each failure vetoes an otherwise complete acceptance | **landed** — content/timing failures are `ACCEPTED_SEQUENCE_FAIL`; an incomplete drain is a **setup** failure (`SETUP_INCOMPLETE`, "not verdict-grade"); each is pinned by its own test |
| Replay the preserved corpus with deterministic verdicts | **unchanged and re-run** — the retained device fixtures keep their verdicts (three `INSUFFICIENT_EVIDENCE`, one `SETUP_INCOMPLETE`), now *because* the new checks are evidence gaps rather than because the oracle did not ask |
| Synthetic complete chains | **landed** — the synthetic chains and the retained excerpt pass when the conjunction is supplied: `SUPPLIED_EVIDENCE` stands for "the owners' reports", and the runner route reads the same evidence |

**How it is wired.** `hitl/acceptance_evidence.py` is the adapter layer: three functions that read each owner's own
report and answer `(ok, detail)` with `ok ∈ {True, False, None}`. It does **not** re-implement a check — the
timing path literally calls the timing owner's issue function, and the teardown path reads the field
`hitl.reporting.derive_verdict` already asks for. The oracle (`analyse(..., inputs=…)`) grades the three as
checks: `content_parity` (kind `content`), `timing_gates` (kind `timing`) and `teardown` (kind `setup`), each with
the owner's own detail string as its evidence. `None` (not supplied) is `UNKNOWN` — so an older capture can still
be graded, but cannot pass.

**The CLI and the runner route read the same evidence.** The oracle gains `--content-report` and
`--timing-report` (the teardown comes from `--run-report`'s own `shutdown`), and the scenario's
`_EVIDENCE_VALUE_FLAGS` carries all four so `runner --verify-only` produces the same verdict as the standalone
CLI. `test_accepted_sequence_scenario` now pins that agreement three ways on the retained excerpt: supplied ⇒
`ACCEPTED_SEQUENCE_PASS` through both routes, not supplied ⇒ `INSUFFICIENT_EVIDENCE` through both, and the
never-staged capture ⇒ a non-pass through both.

## Two real defects this slice found (not caused by it)

1. **A real run's teardown record could never satisfy the pass rule.** `ShutdownResult.as_dict()` omitted `ok`,
   while `hitl.reporting.derive_verdict` requires `teardown["ok"]` for a pass — so a sealed bundle from a
   perfectly drained run was recorded as a teardown failure. Fixed in the owner (`as_dict()` now carries its own
   verdict), and pinned both ways: the new record satisfies `derive_verdict`, the pre-fix shape is refused.
2. **Running the oracle as a script shadowed pyserial.** A direct `python scripts/hitl/accepted_sequence_probe.py`
   puts `scripts/hitl` on `sys.path[0]`, where `scripts/hitl/serial/` answers `import serial` — so any verifier
   that transitively needs the serial stack failed to import in the CLI (the tests never saw it, because they
   import the module). The probe now drops that entry, and the timing adapter imports its owner lazily and reports
   an unimportable owner as `UNKNOWN` with the reason, never as a pass.

## Evidence

- `scripts/test_accepted_sequence.py` **140/140**, of which 8 are new (`VetoTests`): a failing content comparison
  vetoes; a content report with no verdict is a gap; a timing issue vetoes and *names the owner's own issue*
  (`capture_ring_overflow`, `unexpected_midi_input_call_site`); an incomplete drain is a setup failure; absent
  evidence can never pass; the real-run teardown record satisfies the bundle rule while the pre-fix shape does
  not; and the three adapters are readers of each owner's own report.
- Two existing pins updated rather than added, both for the honest reason: the retained excerpt now grades
  `INSUFFICIENT_EVIDENCE` on its own (it is a 12-row cut that proves the chain), and still grades
  `ACCEPTED_SEQUENCE_PASS` once the conjunction is supplied — the same conditional anchor, now explicit.
- CLI check on the retained device excerpt: supplied ⇒ `ACCEPTED_SEQUENCE_PASS` with `content_parity`,
  `timing_gates` and `teardown` all `ok`; a failing content report ⇒ `ACCEPTED_SEQUENCE_FAIL`, quoting the
  owner's own difference (`{'track': 7, 'slot': 4, 'field': 'hasData'}`).
- Neighbours green together: corpus 3/3 (every retained fixture keeps its verdict), scenario 38/38,
  authorization 24/24, `test_retention_handoff_plan` 33/33, `test_hitl_correlated_actions` 23/23.
- Full host sweep **1,067 tests / 75 suites**, with only the seven pre-existing unrelated failures.
  (`test_midi_clock` failed once mid-sweep and passed on re-run, both standalone three times and in the repeated
  sweep: recorded as an observed flake, not a regression.)
- `git diff --check` clean, `openspec validate --strict` valid.

## What this slice did not do

- **No run has yet carried the three reports end-to-end.** The owners produce them, the oracle consumes them, and
  6.3 is the run where a live leg's bundle contains all three (and where the *content* half is the per-interval
  inventory the content pipeline records, rather than the library-level manifest comparison used here).
- **5.3** — the focused/import gates, the full `pio test -e native` record and the mutation results for the former
  queue/timing/attribution bugs are its own task.
