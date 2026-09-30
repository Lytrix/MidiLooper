# G2 execution packet — response-driven-hitl-acceptance (task 1.1)

Authority: [IMPLEMENTATION_READINESS.md §G2](../../../docs/Authority/IMPLEMENTATION_READINESS.md) and its
[template](../../../docs/Templates/EXECUTION_PACKET.md). This is the change-level packet: it pins the fields
every slice shares (dependency closure, observation vocabulary, budgets, oracles, baseline gates), audits each
required field for an authoritative owner, and carries one compact section per **remaining** slice. Slices that
already landed keep their own artifacts, which this packet links rather than restates:

| Landed slice | Its own packet/artifact |
|---|---|
| 2.1–2.2 bounded identity observations, latch rows | [S22-LATCH-EVIDENCE-PACKET.md](S22-LATCH-EVIDENCE-PACKET.md) |
| 2.3 command forwarding / protocol decode | [COMMAND-FORWARDING-EXECUTION.md](COMMAND-FORWARDING-EXECUTION.md) |
| 3.1 correlated gesture stages | [CORRELATED-ACTIONS-EXECUTION.md](CORRELATED-ACTIONS-EXECUTION.md) |
| 3.2 bounded cancellation | [CANCELLATION-EXECUTION.md](CANCELLATION-EXECUTION.md) |
| 3.3 sealed bundles, replay, observation-wait facade | [REPORT-BUNDLE-EXECUTION.md](REPORT-BUNDLE-EXECUTION.md) |

G0 discovery and the G2 design review live in [design.md](design.md); the whole-change dependency statement is
its first section. This packet adds no contract: where a field already has an owner it links to it, and the
audit below is the only place that judges ownership.

## Identity and scope

- **Packet/task IDs**: this change (`response-driven-hitl-acceptance`), slices 4.1–4.4, 5.1–5.3 and 6.1–6.4.
  Approved contract: this change's `specs/hitl-automation/spec.md`; design + G2 review in [design.md](design.md);
  runtime dependency [SHARED-CAPTURE-FINALIZATION.md](../runtime-working-set-ownership/SHARED-CAPTURE-FINALIZATION.md).
- **Status**: specified (this packet) — no slice below is fixture-implemented, native-verified or device-verified
  by *this* document. Per-slice status is recorded in each slice's completion record.
- **Owner and production entry → deferred worker → commit/retirement**: the *HITL* side has no deferred worker;
  its production path is the real MIDI control surface (`slots.press_*` → `hitl.midi_io` pulses → the device's
  `MidiButtonProcessor`), and the firmware side of every identity it reads is owned by the runtime change's
  admission/bind/publish/retire chain (S4f.16–S4f.22). The packet's job is to keep the two apart: **no slice
  here may invent firmware behaviour**.
- **Primary files and existing harness**: `scripts/hitl_accepted_sequence.py`, `scripts/hitl_retention_handoff.py`,
  `scripts/hitl_accepted_sequence_probe.py`, `scripts/hitl/flows/*`, `scripts/hitl/actions/*`,
  `scripts/hitl/{managed_capture,reporting,run_metadata,library_observation,device_shutdown}.py`,
  `scripts/hitl/stimuli/*`, `scripts/hitl/layered_cli.py`; host suites `pio test -e native` plus the Python suites
  named per slice.
- **Preconditions present**: bounded read-only observations (2.1/2.2), one protocol decoder (2.3), the action
  layer with stages, cancellation and a sealed bundle (3.1–3.3), the oracle with phase-scoped links, the driver
  with slot-scoped receipts, registered stimuli with a discovery CLI.
- **External blockers**: device access (upload, `--boot-reset`, scratch-slot content) is *user-authorized per
  session*; the runtime change's firmware gates (S4f.17–S4f.22) are the dependency for any device acceptance.
- **Explicit non-goals**: no new firmware row, no new Manager/authority, no sleep-based pass, no
  reboot-to-clean, no quarantine, no automatic retry-until-pass, no re-grading of old bundles with guessed
  identities.
- **Commit boundary**: one slice per commit on the working branch (`feature/runtime-working-set-ownership`);
  each slice is independently buildable and its docs close with it.

## Observation vocabulary (fields and events, with their owners)

A predicate may read only these, and each has an owner; a slice that needs something else records the decision
in its own section rather than inventing a field.

| Vocabulary | Owner (authority) | Where it is decoded/read |
|---|---|---|
| `#CAP,<us>,DISP,<slot0>,<STATE>,…` (focus/arm/record display) | firmware `DebugSessionCapture` (display rows) | `hitl_accepted_sequence._slot_disp_state`, `_slot_disp_rows` |
| `Loop N: …` human rows (`Start Recording`, `Stop Recording`, `Queued/Reaffirmed playback switch`, `Switch gesture from slot M`, `Stop Overdub`) | `MidiButtonActions` / `MidiButtonManager` (directly printed) | driver + oracle regexes; `hitl.device_time.human_line_device_us` |
| `Button press: <desc> (<kind>)` | `MidiButtonManager` classification | recognition stage in `hitl.actions.correlation` |
| `RECA,<slot0>,<tick>` / `RECS,<kind>,<slot0>,…` / `RID,` | firmware record receipts (`CaptureLineTier` one-shots) | `hitl/serial/protocol.py`, oracle |
| `DIAG,reentry,{admit|bound|deny},…`, `DIAG,reentry_window_open,…`, `DIAG,canonical_release,…`, `DIAG,stop_state,…` | runtime S4f.16–S4f.22 owners (admission, binding, window, retirement) | oracle links L2–L4 + `overdub_interval_identity` |
| `DIAG,latch,<…>` (accepted-stop latch) | `Track::latchAcceptedStop` / `consumeAcceptedStopObligation` / S2b.6 park | [S22 packet §3](S22-LATCH-EVIDENCE-PACKET.md) |
| `ST,Track,<from>,<to>`, `PERS,…`, `SAVE,idle,…` | firmware state/persistence owners | oracle + `stopped_preparation`, `device_shutdown` |
| `LIBOBS,B…E` census family, `RING,record_stats,…` read-back | capture-gated request/reply owner (2.1/2.3) | `hitl/serial/protocol.py`, `managed_capture.py` |
| Identity of a row's *age* (`#CAP` stamp, human prefix) | `hitl/device_time` (the one owner of "is this news?") | every wait and the oracle |

## Budgets and gates (fixed before behaviour work)

| Budget/gate | Value | Owner |
|---|---|---|
| Managed run ceiling | 360 s default, `HITL_MANAGED_RUN_TIMEOUT_S` override | `hitl/managed_capture.resolve_managed_run_timeout` |
| Cancellation grace after the ceiling | 30 s default, `HITL_CANCEL_GRACE_S` override | `hitl/managed_capture.resolve_cancel_grace` |
| Every action's own deadline | finite per action (waits take explicit `timeout_s`; the CLI exposes them) | action layer (`correlation.wait_for`, `observation.await_markers`, drivers' flags) |
| RAM1 free-for-locals floor | approved floor **840 bytes** of ITCM padding; the mechanism (bank arithmetic) is the rule | floor recorded here and in [S22 packet](S22-LATCH-EVIDENCE-PACKET.md); mechanism: `.cursor/rules/Teensy-RAM1-Bank-Overflow.mdc` |
| Capture ring / buffer ceiling | fixed-buffer design; "loss is detectable" is a gate (counters + read-back) | [S22 packet §7](S22-LATCH-EVIDENCE-PACKET.md), `CaptureLineTier` tiers |
| Observation work per turn | **numeric budget GAP by design** — the *mechanism* owner is the capture-gated bounded service; the *number* is measured in 6.2 | 6.2 (measurement), design decision 7 (gate: observations must not perturb the required behaviour) |
| Native suite | `pio test -e native` (2,643 cases at the 3.3 revision) | [DELIVERY_RULES.md](../../../docs/Authority/DELIVERY_RULES.md) |
| Firmware build + upload | `pio run -e teensy41-capture-serial`; upload only on explicit approval | DELIVERY_RULES.md, AGENTS.md |
| Host-side acceptance entry points | baseline preset, or the `scripts/hitl_stop_interruption.py` wrapper; never a captured `command.json` | AGENTS.md, `.agents/skills/hitl-content-verification` |

## Ownership audit (every required field, and the one open decision)

| Required field | Authoritative owner | Verdict |
|---|---|---|
| Dependency closure | [design.md](design.md) §Dependency resolved + runtime `SHARED-CAPTURE-FINALIZATION.md` | **owned** |
| Observation fields/events | table above (firmware emitters + one decoder) | **owned** |
| Memory budget | RAM1 floor + rule for the mechanism | **owned** |
| Work budget | managed ceiling/grace owned; per-turn observation number deliberately unset until 6.2 | **owned** (measurement gate is 6.2 itself) |
| Transition oracles | per slice: oracle links (5.x), recipe transitions (design decision 5), latch transitions (S22 §3), bundle rule (3.3) | **owned** |
| Baseline linker/timing gates | DELIVERY_RULES.md + the RAM1 floor + the mandated HITL entry points | **owned** |
| Stimulus selection and promotion | `scripts/hitl/stimuli/` registry, `host_midi_hitl.py stimulus list`, AGENTS.md promotion rule | **owned** |
| **Run-scoped authorization manifest** | `hitl/stimuli/spec.py` (`Authorization`), declared per profile in `registry.py`, surfaced by `stimulus list`, enforced by `hitl/stimuli/authorization.py`; CLI targets remain the compatibility path | **owned** (host half landed 2026-09-30; 4.1's device legs open) |
| Target identity for acceptance | driver CLI + `--prepare-only`; the report records the targets it was given | **owned** |
| Report/bundle schema | `hitl/reporting.py` (`BUNDLE_SCHEMA_VERSION`, `derive_verdict`) | **owned** |

**Decision — where the run-scoped authorization manifest lives (2026-09-30, confidence medium-high).** The
manifest is the stimulus registry's: a stimulus already declares the recipe it drives (`base_bars`,
`overdubs`, `interactions`, `must_prove`, tempo, clock) and `stimulus list` is the mandated discovery path, so
the *authorized targets and limits* belong beside them, declared once per profile and read by the scenario. The
drivers' explicit CLI targets stay as the compatibility path and as the record of what a run was actually
given, and the report keeps them. Primary evidence: task 4's own precondition line names the manifest as a
required run input, design decision 4 names it run-scoped, and the registry is the only owner that already
carries per-run declarations. This is a *shape* decision inside the approved contract, not a new contract; 4.1
implements it, and the alternative (CLI-only authorization) remains acceptable if the registry grows an
authorization block without breaking `stimulus list`. **Landed 2026-09-30** as 4.1's host half: the
declaration (`spec.py`), the per-profile scope (`registry.py`), the resolution and refusal
(`hitl/stimuli/authorization.py`), the drivers' pre-reset guard and the bundle's `authorization` section.

## Slice 4.1 — scoped stopped preparation

- **Scope**: clear only explicitly authorized track/slot targets through the production mapped long press;
  require the firmware's own recognized press, a matching clear completion, an independent emptiness read and a
  *successful persistence* result; then re-observe the SD-aware census. Non-goals: reboot, blind retry,
  quarantine, substitute slot, clear of an unauthorized target.
- **Oracle fixed before edits** (all four already exist and are tested): `stopped_preparation.clear_slot`
  returning `CLEARED | ALREADY_EMPTY | REFUSED | ABORTED | NOT_SELECTED | WRONG_TARGET | UNCONFIRMED`;
  `library_observation.occupied_slots` (incomplete window = unknown, never empty); `device_shutdown`'s verified
  idle; the driver's `PreparationFailure` (fail-closed *before* any further gesture).
- **Controls/coverage**: injected `now`/`sleep`/`lines_reader`/`emptiness_reader`; the long-press
  discriminator; a short press refused by name; a filled non-selected target → `NOT_SELECTED`; SD residue →
  `SETUP_INCOMPLETE`; persistence failure → retry-bounded `ABORTED`/`REFUSED` then abort.
- **Verification recipe**: `PYTHONPATH=scripts .venv/bin/python -m unittest scripts.test_accepted_sequence
  scripts.test_hitl_capture_integrity scripts.test_device_shutdown scripts.test_library_observation_classifier`
  (counts recorded per run); device leg only after the user authorizes targets in the session.
- **Completion record**: the **host half landed 2026-09-30** — the run-scoped authorization manifest is
  declared (`Authorization` in `hitl/stimuli/spec.py`, declared per profile in `registry.py`, shown by
  `stimulus list`), resolved into the run's record (`hitl/stimuli/authorization.py`) and enforced by both
  drivers *before* the reset, the census and any gesture (`result=UNAUTHORIZED_TARGET`, naming target and
  scope), and the record reaches the sealed bundle. Evidence: `test_hitl_stimulus_authorization` 24/24,
  `test_accepted_sequence` 98/98, 6 mutations red-proved, `pio test -e native` 2,643 (7 skipped). Firmware
  untouched. The **device legs remain open** (`[~]` in `tasks.md`): target selection, the RAM/SD census and
  persistence-completion proof on a repeat run without a reset, each needing per-session authorization.
  Record: [AUTHORIZATION-MANIFEST-EXECUTION.md](AUTHORIZATION-MANIFEST-EXECUTION.md).

## Slice 4.2 — record recipe

- **Scope**: SD-aware empty A/B; real record mode observed before notes; one B slot gesture carries A's stop
  *and* B's record start; one legal stop per interval; production dispatch and host scheduling only.
- **Oracle fixed before edits**: the transition sequence of design decision 5 (record), the oracle's links
  L1–L7, `family_accounting`, and `derive_verdict` for the sealed result. A *populated-A* variant must FAIL
  (the mutation).
- **Controls/coverage**: delayed observations, premature retirement, wrong-mode route (playback/overdub instead
  of record) must stop the leg as SETUP/route failure; continuous transport is a window invariant.
- **Verification recipe**: `test_accepted_sequence` + `test_accepted_sequence_corpus`, then the registered
  scenario through the layered runner once 4.4 lands.
- **Completion record**: to be filled.

## Slice 4.3 — overdub recipe

- **Scope**: confirmed seeds for A and B, one switch gesture, B's recognized overdub start, one B stop; no
  separate A stop, no playback re-affirmation, no wait for predecessor retirement inside the critical window.
- **Oracle fixed before edits**: the overdub chain already graded by the oracle (`DIAG,reentry,admit|bound`,
  `stop_state` 6→7 / 7→5|1, `canonical_release`, the `Loop N:` request rows) and `overdub_interval_identity`.
- **Controls/coverage**: a late successor start reads NOT_EXERCISED, never a violation; a second acceptance or
  execution is a FAIL; seeds must not count as acceptance actions.
- **Verification recipe**: as 4.2, plus `test_hitl_retention_handoff_plan` for the seed receipts.
- **Completion record**: to be filled.

## Slice 4.4 — registration and the standalone CLI

- **Scope**: register both recipes as scenarios, route the standalone CLI through the same runner, translate
  legacy arguments, refuse unsupported combinations by name, list scenarios.
- **Oracle fixed before edits**: identical verdicts through both entry points; unchanged base/edit_full
  behaviour; the import gate (flows may import `hitl.actions`, `hitl.context`, `hitl.config`, `hitl.flows`
  only).
- **Controls/coverage**: legacy translation table, unknown/unsupported combination, scenario listing,
  verify-only (no ports) path.
- **Verification recipe**: `test_hitl_import_gate`, the layered suites, and a `--list-scenarios` smoke.
- **Completion record**: **partly landed 2026-09-30.** Registration, translation, refusals, listing, verdict
  identity and unchanged classic behaviour are in (`test_accepted_sequence_scenario` 32/32, 10 mutations
  red-proved, the excerpt and a non-pass fixture agreeing through both entry points). **Open (4.4b):** the
  standalone driver *becomes* a wrapper to the runner, which first needs the leg body moved into a stateless
  flow and an owner chosen for the family-counter read-back under the flow import gate (`hitl.serial` is
  closed to both a flow and a scenario). Record:
  [SCENARIO-REGISTRATION-EXECUTION.md](SCENARIO-REGISTRATION-EXECUTION.md).

## Slice 5.1–5.3 — oracle and gates

- **Scope**: 5.1 phase-scoped predicates for **both** modes (input → recognition → accepted obligation →
  execution → retirement) with every missing/reordered/duplicate/wrong-identity edge, legacy
  deny-plus-latch versus true refusal, overlap miss, transport stop and clock change producing the specified
  non-PASS; 5.2 content-parity + timing verifiers + independent shutdown result vetoing otherwise complete
  acceptance, with the preserved corpus replayed; 5.3 focused + gate suites, full `pio test -e native`, and
  mutation results for the former queue/timing/attribution bugs.
- **Oracle fixed before edits**: the verdict vocabulary and the link names are already fixed by the oracle
  module and its corpus; each new predicate is added with its own failing case first.
- **Verification recipe**: `test_accepted_sequence` + corpus, the new bundle suite, `test_hitl_import_gate`,
  `pio test -e native`.
- **Completion record**: to be filled.

## Slice 6.1–6.4 — hardware qualification

- **Scope**: 6.1 stimulus selection (registered first; temporary variants only with a declared reason);
  6.2 bounded response-latency and instrumentation-overhead measurement against the packet budget (this is
  where the per-turn number above gets its value); 6.3 three consecutive PASS attempts per leg on one boot plus
  the mixed record-A → overdub-B sequence, all attempts retained; 6.4 link the sealed bundles to S4f.13 and
  close the docs.
- **Precondition**: explicit user authorization for targets, upload and `--boot-reset` in the session.
- **Device-only claim**: everything in this section; nothing here may be inferred from host evidence.
- **Verification recipe**: `.venv/bin/python scripts/host_midi_hitl.py stimulus list`, the mandated entry point,
  then `scripts/hitl_report_replay.py --bundle <path>` on each sealed verdict.
- **Completion record**: to be filled; a missed natural window stays NOT_EXERCISED.

## Completion record (this packet's own)

- **Checks run for this document**: none required — it is a specification, not a slice. `openspec validate
  response-driven-hitl-acceptance --strict` is run with the commit that adds it, and `git diff --check` is
  clean. **NOT RUN / NOT APPLICABLE**: native tests, build, RAM1, device (no behaviour changed).
- **Oracle/contract unchanged**: yes — no file outside this change's artifacts is touched by 1.1.
- **Fixture implemented**: NOT APPLICABLE (documentation slice).
- **Task IDs eligible for checkoff**: **1.1** (this packet exists with every required field audited and the one
  open ownership decision recorded and routed to 4.1).
- **Stage gates still open**: 4.1–4.4, 5.1–5.3, 6.1–6.4, and the `[~]` device legs of 2.1/2.2.
