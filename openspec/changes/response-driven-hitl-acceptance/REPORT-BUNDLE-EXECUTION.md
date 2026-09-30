# Versioned report bundles, device-free replay, and the observation-wait facade (task 3.3)

Host-only slice: no firmware, no build, no device action. Authority is the change's own spec —
[Bounded cancellation and immutable evidence](specs/hitl-automation/spec.md): *reports SHALL pin
firmware/configuration/stimulus/host identities, target authorization, action and observation traces, predicate
evidence, inventories, timing, content and teardown results*, *shutdown failure SHALL prevent overall success* —
and its scenario *Verify-only replay*: *when an immutable bundle is verified again without device ports, the same
versioned predicates produce the same verdict* — plus the task's own import-gate clause.

## What a bundle is, and what it is not

`hitl/reporting.py` now owns two different documents, and the difference is the point:

* the **progress** report (`write_scenario_report`) is rewritten as a run advances — deliberately, because a run
  killed mid-scenario must still leave what it knew — so it is *not* evidence of a finished run;
* the **sealed bundle** (`build_report_bundle` / `write_report_bundle`) is one versioned, digest-pinned record
  per run, written **once** (atomic rename, refuses to overwrite), whose verdict is derived from its own
  evidence by a rule a reader can re-run with no device attached.

`BUNDLE_SCHEMA_VERSION = 2` with a fixed section list
(`run_id, kind, status, claims_success, passed, failures, identity, authorization, actions, observations,
predicates, inventories, timing, content, teardown, evidence, notes, digest`); `digest` is the sha256 of the
canonical JSON of everything else. `write_report_bundle` checks the version, the digest and the section list
before sealing, so a caller cannot seal a shape this schema does not describe.

**The verdict rule is the spec's, in one place** (`derive_verdict`, re-used by the reader): every recorded
predicate ok, *and* a teardown result present and ok, *and* — when the caller claims success — that claim backed
by both. A caller that reports a success over a failed predicate or an incomplete drain gets `passed=False` with
`bundle_verdict` named, so the bundle states the inconsistency instead of carrying it.

**Identity comes from the managed metadata path.** `run_metadata.run_identity` is now the one builder behind
both `metadata.json` and a bundle's `identity` section, so the two can never disagree about which commit and
which firmware image produced the evidence. `write_run_metadata` is a thin writer over it (its own contract and
its tests are unchanged).

## Replay

`scripts/hitl_report_replay.py --bundle <run-bundle.json>` reads a sealed bundle and **re-runs the same rule**
over the same recorded predicates and teardown, opening no port and changing nothing:

| Exit | Meaning |
|---|---|
| 0 | intact, and (with `--expect passed`, the default) a pass |
| 2 | intact but not a pass (`--expect any` makes this a 0: a legitimate non-pass is a successful verification) |
| 3 | not trustworthy: unknown schema version, content that does not hash to its digest, or a recorded verdict its own evidence denies |

The guarantee is stated honestly in the reader's own docstring: a digest is integrity, not a signature. An
editor who rewrites *everything* the rule reads — predicate, verdict, failures and digest — produces a
consistent bundle that no reader without a trusted store can tell apart. What cannot be produced is a bundle
that still **claims the old verdict over new evidence**: the derivation is re-run, not read, so flipping a
predicate is caught whether or not the digest was recomputed to match.

**The oracle seals the acceptance verdict.** `hitl/accepted_sequence_probe.py --emit-bundle <path>
[--run-report <driver.json>]` turns its own `Analysis` into a bundle: one predicate per check, `UNKNOWN` counted
as not-ok (an absent required edge is not a pass), and the run's `shutdown` result carried in so that
`shutdown failure SHALL prevent overall success` is enforced at the bundle level rather than restated. Without
the driver's report the bundle records "no teardown result" and cannot be a pass — which is the honest reading:
a verdict whose drain was never proven is not a pass, however green its checks.

## The observation-wait facade, and the import gate

Both drivers carried their own copy of the *observation* wait (waiting for rows the device emits on its own —
commitment, persistence handoff, reentry release); the gesture wait was consolidated in task 3.1 and this is the
other half. `hitl/actions/observation.py` is now the one owner: `await_markers(rows, markers, *, timeout_s,
poll_s, cancel, on_poll, drain_every_s, now, sleep)` — bounded, cancel-aware, ring-flush on the caller's cadence,
and with time injectable; the caller keeps its own row source. `hitl_accepted_sequence._await_markers` and
`hitl_retention_handoff._wait_for_any` are now thin wrappers over it (their signatures, cancellation semantics
and result contracts are unchanged).

**The import gate did not have to be widened.** The facade lives under `hitl.actions`, a prefix flows may
already import, and `test_hitl_import_gate` now pins that: the facade sits under a flow-allowed prefix, a
flow-shaped module importing it passes the gate, and the forbidden prefixes
(`hitl.verify`, `hitl.serial`, `hitl.scenarios`, `hitl.reporting`, `hitl.legacy_`) are **unchanged** — so the
resolution is scoped to this facade and nothing else.

## Evidence

| Suite | Result | What is new |
|---|---|---|
| `scripts/test_hitl_report_bundle.py` | **14/14** | schema/version/digest pinned; a success claim over a failed predicate and a failed drain both refused; no teardown recorded refused; sealing once (and nothing half-written); an unknown version and a non-bundle refused; a pass replays as a pass with `mido.open_output`/`serial.Serial` patched to raise; a legitimate non-pass replays as a non-pass; an edit that keeps the digest *and* an edit that re-pins it are both caught; the replay tool's exit codes (0/2/3) |
| `scripts/test_hitl_observation_wait.py` | **7/7** | a marker already present; a bound that is honoured; the row source re-read every pass; the flush cadence; cancellation stopping at once; an injected clock throughout |
| `scripts/test_hitl_import_gate.py` | **5/5** | the facade under an allowed prefix; a flow importing it passes; nothing widened |
| `test_accepted_sequence.py` | **97/97** | the oracle seals a pass, refuses to claim success over an `UNKNOWN` check, refuses a pass without a proven drain, and keeps a setup failure's own verdict with its check named |
| `test_run_metadata.py` | **6/6** | unchanged contract after `run_identity` was extracted |

Neighbouring suites green together: `test_hitl_cancellation` 21/21, `test_hitl_correlated_actions` 23/23,
`test_hitl_actions` 2/2, `test_hitl_device_time` 16/16, `test_hitl_command_forwarding` 26/26,
`test_managed_record_drain` 21/21, `test_hitl_serial_timing` 33/33, `test_retention_probe` 43/43,
`test_retention_handoff_plan` 33/33, `test_managed_capture` 13/13, `test_device_shutdown` 15/15,
`test_capture_transitions` 61/61, `test_accepted_sequence_corpus` 3/3.

Six mutations, each red-proved and restored byte-identically (sha256 before and after every file):

| Mutation | Tests that caught it |
|---|---|
| the derivation ignores the teardown result | shutdown-failure prevention |
| the derivation ignores the caller's success claim | the claim inconsistency, shutdown prevention |
| the digest does not cover the verdict/identity | the digest-keeping edit, the seal check |
| sealing overwrites an existing bundle | seal-once |
| the reader trusts the recorded verdict | silent-disagreement, legitimate non-pass replay |
| an `UNKNOWN` check counts as ok | the oracle's unknown-check case |

## Not done, and not implied

* **The layered scenarios still write their own report.** `write_scenario_report` is untouched (it is the
  progress document, and its `schema_version: 1` is that document's, not the bundle's). Migrating the layered
  runner and the `--verify-only` path onto bundles is follow-up work; the pre-existing `test_foundation_runner`
  and `test_layered_verifiers` failures (both `'Namespace' object has no attribute 'overdub_step_clocks'` in the
  legacy bridge's verify path) are **unrelated to this slice** and re-confirmed identical with its files
  stashed.
* **No bundle is written by the drivers yet** in a device run: the acceptance driver's report is unchanged, and
  the sealed verdict comes from running the oracle with `--emit-bundle`. Nothing here needs a device, and no
  device run is claimed.
* No firmware row, build or device action is part of this slice, and none is implied.
