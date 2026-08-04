---
name: HITL CLI rebuild
overview: Temporary migration plan for HITL CLI rebuild — architecture extracts to HITL_ARCHITECTURE.md; UIP 5.5 is one gate.
todos:
  - id: p0-inventory
    content: "Phase 0 — Migration: legacy helper inventory, doc map, OpenSpec hitl-cli-rebuild stub (no production code)"
    status: completed
  - id: p1-foundation
    content: "Phase 1 — Migration: extract HITL_ARCHITECTURE.md, bootstrap, runner, actions, flows, protocol, import gates"
    status: completed
  - id: p2-flows-scenarios
    content: "Phase 2 — Migration: core scenarios + uip_5_5 preset + device HITL (UIP 5.5 gate)"
    status: in_progress
  - id: p3-port-delete
    content: "Phase 3 — Migration: port remaining scenarios; delete legacy"
    status: pending
  - id: p4-docs-reports
    content: "Phase 4 — Migration: capture bundles, HITL_DEVELOPER_GUIDE + REGRESSION_WORKFLOW, slim HITL-Test-Flow.mdc"
    status: pending
  - id: p5-cleanup
    content: "Phase 5 — Migration: archive hitl-cli-rebuild + this plan; dependency audit"
    status: pending
isProject: false
---

# HITL CLI rebuild

**Problem:** ~8.4k lines in [`legacy_record_baseline.py`](../../scripts/hitl/legacy_record_baseline.py) + [`legacy_edit_baseline.py`](../../scripts/hitl/legacy_edit_baseline.py), circular imports, `sys.argv` injection in [`base.py`](../../scripts/hitl/scenarios/base.py) / [`edit_full.py`](../../scripts/hitl/scenarios/edit_full.py).

**Goal:** One CLI, phased **migration**, delete legacy. Successful HITL runs become a **regression corpus**.

**Document role:** This file is the **temporary migration plan** (implementation schedule + staging architecture). It is **not** the permanent architecture reference — see [Documentation structure](#documentation-structure) below.

**Staging note:** Part I below is the source for [`docs/Architecture/HITL_ARCHITECTURE.md`](../Architecture/HITL_ARCHITECTURE.md). Extract it in Phase 1; contributor and regression guides follow in Phase 4. Archive this plan when migration completes (Phase 5).

---

# Documentation structure

Split HITL documentation by **purpose** and **lifetime**:

```text
docs/
    Architecture/
        HITL_ARCHITECTURE.md          ← long-lived: what the system is (from Part I)
    Guides/
        HITL_DEVELOPER_GUIDE.md       ← long-lived: how to extend (scenarios, flows, verifiers)
        HITL_REGRESSION_WORKFLOW.md   ← long-lived: regression investigation process
        HITL_TEST_SCENARIOS.md        ← long-lived: scenario/preset catalogue (existing)

openspec/changes/hitl-cli-rebuild/    ← temporary: migration tasks, design notes, phase gates

.cursor/rules/
    HITL-Test-Flow.mdc                ← short agent rules; links to docs above (no duplication)

docs/plans/hitl_cli_rebuild_enhancement.md   ← THIS FILE — temporary; archive after Phase 5
```

| Document | Responsibility | Lifetime |
|----------|----------------|----------|
| **HITL_ARCHITECTURE.md** | System design, invariants, layering, ownership, corpus format | Long-lived |
| **HITL_DEVELOPER_GUIDE.md** | Running HITL, adding scenarios/flows/actions/verifiers, common mistakes, §12 checklist | Long-lived |
| **HITL_REGRESSION_WORKFLOW.md** | Known-good comparison, verify-only replay, immutable bundles, agent policy | Long-lived |
| **HITL_TEST_SCENARIOS.md** | Scenario and preset catalogue | Long-lived |
| **hitl-cli-rebuild** (OpenSpec) | Phase tasks, migration inventory, implementation gates | Temporary → archive |
| **hitl_cli_rebuild_enhancement.md** | Staging architecture + migration phases (this doc) | Temporary → archive |
| **HITL-Test-Flow.mdc** | Concise behavioural rules for agents; points to long-lived docs | Long-lived (slim) |

### HITL_ARCHITECTURE.md (extract from Part I)

Permanent reference — *what the system is*, not how it was migrated:

- Goals, design philosophy (§12)
- Layering, dependency rules, single-owner + one obvious place
- Context hierarchy, flows, actions, verification, reporting
- Regression corpus invariants (§11.1–11.3, §11.6)
- Extension principles (§5.5–5.6, §12.3 checklist)
- Architectural evolution policy (§12.5) — link to [`DECISION_LOG.md`](../DECISION_LOG.md)

### HITL_DEVELOPER_GUIDE.md (new in Phase 4)

*How to extend* — practical contributor workflow:

- Running HITL (Mode A/B), build env
- Creating a scenario (executable spec pattern)
- Writing a flow, action, verifier
- Reports and capture bundles
- Where code goes (ownership table)
- Common mistakes, architectural checklist (§12.3)
- When to open an OpenSpec change / append DEC for HITL structural changes (§12.5)

### HITL_REGRESSION_WORKFLOW.md (new in Phase 4)

Extract §11.2, §11.4–11.5, §11.6, verification diagnostics (§6):

- Regression-first workflow diagram
- Locate known-good bundle → compare → explain difference → then fix
- Immutable evidence rules
- Before changing firmware / verification / scenarios (policy tables)

### HITL-Test-Flow.mdc (slim in Phase 4)

Keep **short** — canonical run commands + behavioural rules only. Do not duplicate architecture.

Agent/contributor pointers:

- Read `HITL_ARCHITECTURE.md` before structural changes
- Major HITL architecture changes: OpenSpec change + DEC — do not silently edit `HITL_ARCHITECTURE.md` (§12.5)
- Read `HITL_DEVELOPER_GUIDE.md` when adding scenarios or verifiers
- Read `HITL_REGRESSION_WORKFLOW.md` when investigating regressions
- Preserve ownership and dependency rules; compose existing flows
- Treat capture bundles as historical evidence; never weaken verification to pass a regression
- Prefer behavioural comparison against known-good captures before modifying firmware

---

This document has two parts:

1. **Part I — Architecture (staging)** — copied to `HITL_ARCHITECTURE.md` in Phase 1; maintained there after extraction.
2. **Part II — Migration** — phases 0–5; moves to `openspec/changes/hitl-cli-rebuild/` as implementation authority during migration.

---

# Part I — Architecture (staging → HITL_ARCHITECTURE.md)

## 1. Stable invariants vs implementation

**Guiding principle:** Do not introduce a layer until multiple independent responsibilities naturally require it.

### Stable invariants (long-lived — survive refactors)

- Dependencies flow **downward only**.
- Every concept has **exactly one owner** (see §2).
- **Flows are stateless** — no mutable runtime state in flow modules.
- **Protocol parsing** exists in exactly one place (`serial/protocol.py`).
- **Time waits** live in `actions/`; **observation waits** live in `verify/`.
- Scenarios express intent; they do not parse `#CAP` or send MIDI.
- **Extensibility:** new scenarios compose existing **flows** first; add new **actions** only when the hardware interaction is genuinely new.
- **No `ScenarioRunner` initially** — `runner.py` calls `spec.run(ctx)` directly. Introduce a dedicated execution layer later only if retries, tracing, cancellation, timeouts, or middleware accumulate.
- **Regression knowledge** — successful HITL runs are project knowledge, not temporary output (see §11).
- **Executable specifications** — scenarios describe expected behaviour via flow composition, not hardware steps (see §5.6).
- **Deterministic scenarios** — same firmware + config + inputs → same observable behaviour (see §12.4).
- **Immutable corpus** — never overwrite successful bundles; corrections create new bundles (see §11.6).
- **Boring architecture** — no speculative abstractions (see §12.1).
- **Architectural evolution** — after migration, major HITL architecture changes go through OpenSpec / DEC, not silent edits to `HITL_ARCHITECTURE.md` (see §12.5).

### Implementation (may evolve)

Module names and file layout below are **today's mapping**, not part of the contract. Refactors may rename or split files without changing invariants.

Examples: `bootstrap.py`, `reporting.py`, `flows/record_seed.py`, per-scenario files under `scenarios/`, capture bundle directory layout under `captures/`.

---

## 2. Layering

```text
CLI (host_midi_hitl.py)
 ↓
cli.py                    ← argparse only
 ↓
bootstrap.py              ← HitlSession + ActionContext + ScenarioContext
 ↓
runner.py                 ← preset loop, verify-only mode, exit codes, reporting
 ↓
Scenario                  ← spec.run(ctx) → ScenarioResult
 ↓
Flows                     ← stateless semantic operations
 ↓
Actions                   ← device MIDI + time-based waits
 ↓
midi_io / transport_clock
 ↓
serial/protocol.py        ← decode #CAP (no pass/fail)
 ↓
verify/                   ← observation waits + assertions
 ↓
reporting.py              ← JSON from ScenarioResult + VerificationResult
 ↓
Firmware
```

```mermaid
flowchart TB
  CLI[host_midi_hitl.py]
  Bootstrap[bootstrap.py]
  Runner[runner.py]
  Scenario[scenarios]
  Flows[flows]
  Actions[actions]
  Protocol[serial/protocol.py]
  Verify[verify]
  Report[reporting.py]
  Registry[registry.py]
  CLI --> Runner
  Bootstrap --> Runner
  Registry --> Runner
  Runner --> Scenario
  Scenario --> Flows
  Flows --> Actions
  Runner --> Verify
  Verify --> Protocol
  Runner --> Report
```

**Dependency rule:** imports flow downward only.

---

## 3. Single-owner principle

Every concept has exactly one owner.

**One obvious place:** every capability should have one obvious module to implement it. When adding functionality, extend the existing owner — avoid secondary helper modules and duplicate implementations (e.g. no `capture_helpers.py`, `capture_common.py`, `capture_utils.py` when `verify/capture.py` already owns capture assertions).

| Concept | Owner module |
|---------|----------------|
| CLI argument parsing | `cli.py` |
| Runtime assembly (ports, contexts) | `bootstrap.py` |
| Open resources (MIDI, serial collector) | `HitlSession` in `session.py` |
| Device-time runtime state (markers, ports ref) | `ActionContext` in `context.py` |
| Scenario identity + artifact paths (no execution mode) | `ScenarioContext` in `context.py` |
| Protocol decoding (`#CAP` regex) | `serial/protocol.py` only |
| Time-based waits (sleep, hold, clock poll) | `actions/` |
| Observation waits (transitions, capture complete) | `verify/` |
| Device operations (press, encoder, transport) | `actions/` |
| Semantic multi-step operations | `flows/` |
| Pass/fail assertions | `verify/` |
| Scenario wiring | `scenarios/` (via `ScenarioSpec.run`) |
| Scenario definitions, presets, verifier lookup | `registry.py` |
| JSON report + capture bundle output | `reporting.py` |
| Regression corpus layout + metadata | `reporting.py` (bundle writer) |
| Preset loop, verify-only mode, exit codes, timeouts | `runner.py` |

---

## 4. Context hierarchy

### `HitlSession` (`session.py`)

Owns open MIDI ports, optional `SerialCaptureCollector`, serial line snapshot accessor. No scenario metadata.

### `ActionContext` (`context.py`)

Passed to **flows** and **actions** only:

```python
@dataclass
class ActionContext:
    session: HitlSession
    config: HitlConfig
```

Convenience properties: `out_port`, `in_port`, `collector`, `markers` (markers live on session).

### `ScenarioContext` (`context.py`)

Passed to **scenarios** only; wraps `ActionContext`. **No execution-mode flags** — `verify_only` is owned by `runner.py`.

```python
@dataclass
class ScenarioContext:
    action: ActionContext
    scenario_id: str
    out_dir: Path
    started_at: datetime
    # report_writer hook optional; scenarios do not call reporting directly
```

Scenarios call `flows.record_seed(ctx.action)` — flows never see report paths or CLI execution policy.

---

## 5. Invariants (detail)

### 5.1 Flows are stateless

> Flows never own mutable runtime state.

All mutable runtime state lives in `HitlSession` and `ActionContext` / session markers. Flow functions compose actions; they do not store scenario-local globals or module-level caches.

### 5.2 Waiting responsibilities

| Kind | Owner | Examples |
|------|-------|----------|
| **Time waits** | `actions/` | `sleep_ms`, button hold duration, MIDI clock poll timeout |
| **Observation waits** | `verify/` (during run, via serial tail poll helpers used by flows through a narrow `verify/wait.py` facade if needed) | wait until `ST` transition count, `RECS` row, capture complete |

**Rule:** Actions never parse `#CAP` lines. Verifiers never send MIDI. If a flow must block until firmware reaches a state, it calls a **verify wait helper** (observation), not an action.

### 5.3 Protocol parsing

> Every `#CAP` / `ST` / `RECS` / `DISP` / `MO` regex exists in exactly one module: `serial/protocol.py`.

Verifiers import parsers; they do not duplicate regex.

### 5.4 Scenarios hide protocol

Scenarios express intent:

```python
def run(scenario_ctx: ScenarioContext) -> ScenarioResult:
    flows.record_overdub(scenario_ctx.action)
    return ScenarioResult(
        observations={},
        markers=scenario_ctx.action.session.markers,
        artifacts={},
    )
```

`runner.py` coordinates run → verify → report and derives process exit codes:

```python
if config.verify_only:
    lines = read_serial_log(config)
    verification = registry.verify(spec.verifier_id, lines, scenario_ctx)
    result = None
else:
    result = spec.run(scenario_ctx)
    lines = scenario_ctx.action.session.snapshot_lines()
    verification = registry.verify(spec.verifier_id, lines, scenario_ctx)
exit_code = reporting.write_and_exit_code(spec, result, verification, scenario_ctx)
```

### 5.5 Extensibility

> New scenarios should normally be created by composing existing flows rather than introducing new device actions.

Add new **actions** only when the hardware interaction itself is genuinely new (e.g. a new button class or encoder gesture). This keeps the action layer stable and encourages reuse.

### 5.6 Scenarios are executable specifications

> A scenario describes **what behaviour is expected**, not **how** it is implemented.

Scenarios read like executable specifications — they compose **flows** that express user intent and expected firmware behaviour. Implementation details belong in flows and actions, never in scenarios.

Good:

```python
def run(ctx: ScenarioContext) -> ScenarioResult:
    flows.record_seed(ctx.action)
    flows.enter_note_edit(ctx.action)
    flows.move_note(ctx.action)
    flows.exit_note_edit(ctx.action)
    return ScenarioResult(observations={}, markers=ctx.action.session.markers, artifacts={})
```

Avoid:

```python
press(Button.RECORD)
sleep_ms(150)
rotate_encoder(4)
press(Button.OK)
```

---

## 6. Core types

### `HitlConfig` (`config.py`)

Immutable run configuration: track, channel, bars, ports, timing, serial mode A/B, loop_slot, **`verify_only`**. Built by `cli.py`. Execution policy stays in config + runner, not in `ScenarioContext`.

### `ScenarioResult` (`types.py`)

Domain-oriented output from a scenario run. **No `exit_code`** — the runner derives process exit from `ScenarioResult` + `VerificationResult`.

```python
@dataclass
class ScenarioResult:
    observations: dict[str, object]   # scenario-specific counters, notes sent, etc.
    markers: list[str]                # serial marker lines collected during run
    artifacts: dict[str, Path]          # e.g. serial_path, snapshot paths
```

### `VerificationResult` (`verify/result.py`)

Replace ad-hoc `dict` returns. Verification accelerates debugging — every failure should answer: what failed, what was expected, what was observed, and where to investigate next.

```python
@dataclass
class VerificationFailure:
    check: str                        # e.g. "ST transition after record stop"
    expected: str
    observed: str
    serial_line: int | None = None    # first divergence in serial.log
    known_good_bundle: str | None = None
    suggestion: str | None = None     # e.g. "Compare ST sequence with known-good bundle"

@dataclass
class VerificationResult:
    passed: bool
    warnings: list[str]
    failures: list[VerificationFailure]   # actionable diagnostics, not bare strings
    metrics: dict[str, object] = field(default_factory=dict)
    attachments: dict[str, object] = field(default_factory=dict)

    @property
    def ok(self) -> bool:
        return self.passed and not self.failures
```

Example console output on failure:

```text
FAIL  scenario: record_overdub
  Expectation: RECORDING → PLAYING after STOP
  Observed:    RECORDING → STOPPED
  Known-good:  captures/2026-07-31_uip_5_5
  First divergence: serial line 421
  Suggestion: Compare ST transition sequence with known-good bundle
```

All verifiers return `VerificationResult`. `reporting.py` serializes failures into `report.json`.

### Registry (`registry.py`)

**Single owner** for:

| Responsibility | Type / API |
|----------------|------------|
| Scenario definitions | `ScenarioSpec` (id, description, tags, verifier_id, `run` callable) |
| Preset definitions | `PresetSpec` (scenario_ids, tags) |
| Verifier lookup | `get_verifier(verifier_id)` / `verify(verifier_id, lines, ctx)` |

```python
@dataclass(frozen=True)
class ScenarioSpec:
    id: str
    description: str
    tags: tuple[str, ...]
    verifier_id: str | None          # e.g. "capture", "playback", "display"
    run: Callable[[ScenarioContext], ScenarioResult] | None = None
    # Future metadata (no pipeline change required):
    # expected_runtime_s, timeout_s, hardware_requirements, prerequisites, ...
```

`ScenarioSpec` intentionally allows metadata expansion (timeouts, hardware requirements, tags, prerequisites) without changing the execution pipeline — `runner.py` reads fields as they are added.

Presets reference scenario ids; registry resolves `ScenarioSpec` + verifier factory:

```python
PRESETS: dict[str, PresetSpec] = {
    "base": PresetSpec(scenario_ids=("record_overdub",), tags=("record",)),
    "uip_5_5": PresetSpec(
        scenario_ids=("record_seed", "edit_minimal", "long_loop_display_window", "slot_queued_start"),
        tags=("uip", "gate"),
    ),
}
```

---

## 7. Module layout (implementation — may evolve)

```text
scripts/
  host_midi_hitl.py
  hitl/
    config.py
    cli.py
    bootstrap.py              # HitlSession + ActionContext + ScenarioContext (narrow)
    session.py
    context.py                # ActionContext, ScenarioContext
    types.py                  # ScenarioResult, shared dataclasses
    runner.py                 # preset loop: run → verify → report → exit code
    reporting.py              # JSON reports (not in bootstrap)
    registry.py               # ScenarioSpec, PresetSpec, verifier lookup
    actions/
      buttons.py
      encoder.py
      transport.py              # time waits + clock poll
      track.py
    flows/
      record_seed.py
      record_overdub.py
      clear_slot.py
      queued_switch.py
      note_edit_smoke.py
      long_loop_display.py
    serial/
      protocol.py
    verify/
      result.py
      capture.py
      playback.py
      display.py
      transitions.py            # observation waits + assertions
    scenarios/
      record_seed.py
      record_overdub.py
      edit_minimal.py
      long_loop_display_window.py
      slot_queued_start.py
```

**`bootstrap.py`** (not `factory.py`): create config-backed session and contexts only. No report generation, no verify dispatch, no CLI parsing.

---

## 8. Layer dependencies (positive rules)

Each layer may depend only on what it needs. Everything else is disallowed; `test_hitl_import_gate.py` enforces this.

| Layer | May depend on |
|-------|----------------|
| **Scenarios** | `flows`, `context` (ScenarioContext), `config`, `registry` (types only) |
| **Flows** | `actions`, `context` (ActionContext), `config` |
| **Actions** | `midi_io`, `transport_clock`, `control_constants`, `ActionContext` |
| **Verify** | `serial.protocol`, `VerificationResult`, `HitlConfig`, `ScenarioContext` (read-only) |
| **Runner** | `bootstrap`, `registry`, `reporting`, `verify`, `config`, `scenarios` (via registry) |
| **Reporting** | `ScenarioResult`, `VerificationResult`, `ScenarioSpec` |

Implicit rule: lower layers never import higher layers (`actions` does not import `flows`; `verify` does not import `actions`).

### Import gate (`test_hitl_import_gate.py`)

Mirror the table above for scenarios and flows first (highest regression risk). Expand to actions/verify in Phase 1.

---

## 9. Composition (no scenario flags)

| Flow | Composes |
|------|----------|
| `flows.record_seed(action_ctx)` | clear → LOOP_EDIT precondition → arm → stream → stop |
| `flows.record_overdub(action_ctx)` | `record_seed` → overdub(s) → undo/redo |
| `flows.note_edit_smoke(action_ctx)` | enter NOTE_EDIT → fixture edits → exit |

Presets compose **scenarios**, not CLI flags:

| Preset | Scenarios |
|--------|-----------|
| `base` | `record_overdub` |
| `edit_minimal` | `record_seed`, `edit_minimal` |
| `uip_5_5` | `record_seed`, `edit_minimal`, `long_loop_display_window`, `slot_queued_start` |

`base` id retained for [HITL-Test-Flow.mdc](../../.cursor/rules/HITL-Test-Flow.mdc).

---

## 10. Orchestration flow (`runner.py`)

`runner.py` owns: preset iteration, **verify-only mode**, timeout policy (future), cancellation (future), metrics (future), reporting, and **process exit codes**.

```python
for spec in resolved_scenarios:
    scenario_ctx = bootstrap.scenario_context(config, spec.id, ...)
    if config.verify_only:
        lines = read_serial_log(config)
        result = None
        verification = registry.verify(spec.verifier_id, lines, scenario_ctx)
    else:
        result = spec.run(scenario_ctx)
        lines = scenario_ctx.action.session.snapshot_lines()
        verification = registry.verify(spec.verifier_id, lines, scenario_ctx)
    exit_code = reporting.write_scenario(spec, result, verification, scenario_ctx)
```

**No `ScenarioRunner`.** If future requirements (retries, tracing, execution middleware, timeout wrappers) accumulate, introduce a dedicated execution layer then — with a well-defined purpose, not speculatively.

---

## 11. Regression knowledge & corpus

The HITL framework is both a **validation system** and a **behavioural archive** of the firmware. Over time it answers: what did correct behaviour look like, when did it change, and is the failure in firmware or in verification?

### 11.1 Invariant

> **Successful HITL runs are project knowledge, not temporary output.**

Every successful preset/scenario execution should produce artifacts that let future investigations answer:

- What did correct firmware behaviour look like?
- Which serial transitions occurred?
- Which timings were observed?
- Which reports were produced?
- Which firmware revision and git commit generated them?
- Which scenario configuration produced these results?

### 11.2 Regression-first workflow

Preferred order when investigating a regression:

```text
Regression reported
        ↓
Locate most recent successful capture for same preset/scenario
        ↓
Replay verification against archived capture (optional — verify-only mode)
        ↓
Run same scenario on current firmware
        ↓
Compare reports → serial output → transitions → timings
        ↓
Explain behavioural difference
        ↓
Determine root cause → fix implementation (or strengthen verification)
```

**First question:** *How does today's behaviour differ from the last known-good behaviour?*

Compare behaviour against the regression corpus **before** weakening verification or adding ad-hoc debugging logic.

```text
Known-good capture → New failing capture → Compare observations → Identify differences → Then change code or verification
```

### 11.3 Capture bundle (stable format)

Treat capture artifacts as part of the public architecture. On **pass**, `reporting.py` writes a complete bundle (not report JSON alone):

```text
captures/
    2026-08-04_142115_uip_5_5/
        report.json       # VerificationResult + ScenarioResult summary per scenario
        serial.log        # full #CAP session (copied or symlinked from Mode B capture)
        metadata.json     # preset, scenario_ids, git_commit, firmware_version, timestamp, mode
        config.json       # HitlConfig snapshot (ports, bars, track, timing)
        firmware.txt      # build env / version string when available
```

Example `metadata.json`:

```json
{
  "schema_version": 1,
  "preset": "uip_5_5",
  "scenario_ids": ["record_seed", "edit_minimal", "long_loop_display_window", "slot_queued_start"],
  "git_commit": "...",
  "firmware_version": "...",
  "timestamp": "...",
  "mode": "Mode B"
}
```

`report.json` also carries `schema_version`. Treat capture bundles as a **public project format** — extend via additive fields; bump version on breaking changes. Older bundles must remain readable.

**Corpus growth:** `captures/` accumulates known-good behaviour over time — bisecting regressions, validating optimisations preserved external behaviour, and auditing verification rule changes against history.

### 11.6 Immutable evidence

The regression corpus is immutable historical evidence:

- **Never overwrite** a successful capture bundle.
- Every successful run creates a **new** bundle directory.
- Corrections create additional bundles — do not modify previous ones.

This preserves behavioural history for regression analysis and git bisect investigations.

**Migration note:** flat `session_*.log` files from [`capture_session.py`](../../scripts/capture_session.py) remain valid during transition; new runs use bundle directories. `.current_session` can point at bundle `serial.log` or a shim path until Mode B is updated.

### 11.4 Developer & agent guidance

When a scenario has previously passed:

| Situation | Policy |
|-----------|--------|
| **Before changing firmware** | Search for prior successful runs of the same preset/scenario; compare against most recent known-good bundle; explain behavioural differences before proposing fixes |
| **Before changing verification** | Confirm prior expectation is genuinely wrong; compare multiple historical captures; prefer **strengthening** over weakening; never remove assertions solely to make a regression pass |
| **Before new scenarios** | Compose existing flows; new actions only for new hardware; reuse existing verifiers |

**Evidence priority:** report JSON, serial logs, captured artifacts, and metadata are primary evidence — not memory or abbreviated console output.

Document these rules in [`HITL_REGRESSION_WORKFLOW.md`](../Guides/HITL_REGRESSION_WORKFLOW.md) (Phase 4) and reinforce via slim [HITL-Test-Flow.mdc](../../.cursor/rules/HITL-Test-Flow.mdc).

### 11.5 Long-term objective

The corpus becomes executable documentation of expected behaviour — supporting questions such as:

- What did the firmware do when this feature originally shipped?
- When did this behaviour change?
- Which commit introduced the regression?
- Is verification incorrect, or has firmware behaviour changed?
- Does the current build still satisfy the historical behavioural contract?

---

## 12. Architectural philosophy

Long-term principles — not migration tasks. The framework optimises for: clear ownership, semantic composition, deterministic behaviour, executable specifications, preserved behavioural history, evidence-driven debugging, and incremental evolution.

### 12.1 Prefer boring architecture

Introduce abstractions only after multiple independent responsibilities demonstrate a stable pattern. Avoid speculative architecture.

**Do not build (unless concrete need emerges):**

- `ScenarioRunner` (until retries/middleware/tracing justify it)
- Legacy compatibility layer
- Plugin system
- Generic execution framework
- Service locator

Evolve from concrete requirements, not anticipated future complexity.

### 12.2 Domain vs infrastructure

| Domain (behavioural logic) | Infrastructure (execution) |
|----------------------------|----------------------------|
| Scenarios | Runner |
| Flows | CLI |
| Verification rules | Bootstrap |
| | Reporting |
| | Serial / MIDI transport |

Domain layers (scenarios, flows, verify) must not depend on **how** scenarios are executed. This allows future execution environments (CLI, CI, IDE tooling, automated regression) without changing behavioural logic.

### 12.3 Architectural decision checklist

Before introducing a new abstraction or module:

1. Does an existing owner already exist?
2. Can this be expressed by composing existing flows?
3. Is this domain logic or infrastructure?
4. Does this introduce a second owner?
5. Does this abstraction solve **at least two** independent use cases?

If the answer to (5) is no, extend the existing design.

### 12.4 Deterministic scenarios

Given the same firmware revision, configuration, scenario, and inputs, scenarios should produce the same observable behaviour.

Avoid:

- Hidden timing dependencies
- Random waits
- Persistent state leakage between scenarios
- Dependence on order of prior scenario execution without explicit setup

Deterministic scenarios greatly increase the long-term value of the regression corpus.

### 12.5 Architectural evolution

After migration completes, **major** changes to the long-lived HITL architecture should be introduced through **architectural decision records** (OpenSpec change + append to [`DECISION_LOG.md`](../DECISION_LOG.md)), not by silently editing [`HITL_ARCHITECTURE.md`](../Architecture/HITL_ARCHITECTURE.md).

| Artifact | Role |
|----------|------|
| **`HITL_ARCHITECTURE.md`** | Describes the **current** system — what it is today |
| **OpenSpec change** (`openspec/changes/<name>/`) | Proposed delta: design, tasks, specs when behaviour or contracts change |
| **`DECISION_LOG.md` (DEC-###)** | Records **why** the architecture changed — durable rationale |

**Workflow for major HITL architecture changes:**

1. `/opsx:propose` (or equivalent) — design + tasks before implementation.
2. Implement; update `HITL_ARCHITECTURE.md` to reflect the **new** steady state.
3. Append DEC to `DECISION_LOG.md` with context, alternatives considered, and consequences.
4. Archive OpenSpec change when complete (`/opsx:archive`).

**Major** (requires DEC + OpenSpec): new layers or owners, dependency-rule changes, capture bundle schema breaks, verification contract changes, new top-level modules.

**Minor** (architecture doc edit only): clarifications, diagrams, typos, module renames that preserve invariants, additive bundle metadata with schema version bump documented inline.

This keeps `HITL_ARCHITECTURE.md` trustworthy six months later: the doc shows what exists; DECs explain how and why it got there.

---

# Part II — Migration

Migration phases are **scheduling only**. Implementation authority during migration: `openspec/changes/hitl-cli-rebuild/tasks.md` (created Phase 0). When complete, Part I lives in [`HITL_ARCHITECTURE.md`](../Architecture/HITL_ARCHITECTURE.md); this doc and the OpenSpec change are archived.

## Phase 0 — Inventory + doc scaffold (no production code)

**Status:** complete (2026-08-04). OpenSpec: [`openspec/changes/hitl-cli-rebuild/`](../../openspec/changes/hitl-cli-rebuild/).

1. Helper inventory (tables below).
2. `openspec/changes/hitl-cli-rebuild/` — `proposal.md`, `tasks.md`, `design.md`, `.openspec.yaml`.
3. Documentation map in OpenSpec `design.md`.
4. `capture_transitions.py` consolidation targets documented.

### Record / capture (`legacy_record_baseline.py` → new layers)

| Legacy helper | Target owner |
|---------------|--------------|
| `_send_record_arm_press`, `_wait_for_recording_started` | `actions/buttons` + `flows/record_seed` |
| `_ensure_clear_to_empty`, `_track_cleared_for_record`, `_can_skip_clear_before_record` | `flows/clear_slot` |
| `_stop_transport_before_clear`, `_restart_transport_after_clear` | `actions/transport` |
| `_stream_pattern_for_seconds`, `_stream_dense_chromatic`, `_run_overdub_pass` | `flows/record_overdub` |
| `_send_global_undo`, `_send_global_redo`, `_undo_redo_pair` | `actions/buttons` + `flows/record_overdub` |
| `_extract_recs_lengths`, `_verify_record_loop_length`, `_verify_stored_record_note_grid` | `serial/protocol.py` + `verify/capture` |
| `_extract_sevt_*`, `_verify_stored_overdub_note_span`, `_verify_overdub_wrap_storage` | `serial/protocol.py` + `verify/capture` |
| `_expected_transition_expectations`, `_recording_transition_baseline` | `verify/transitions` |
| `_build_serial_verification` | `runner.py` + `verify/*` (split per verifier) |
| `_extract_disp_snapshots`, `_verify_display_snapshots` | `serial/protocol.py` + `verify/display` |
| `_extract_persistence_rows`, `_summarize_persistence_handoff` | `verify/capture` (optional metrics) |

### Note edit (`legacy_edit_baseline.py` → new layers)

| Legacy helper | Target owner |
|---------------|--------------|
| `_ensure_clear_to_empty`, `_ensure_recording_started` | `flows/clear_slot`, `flows/record_seed` |
| `_stream_fixture_record`, `_fader1_select_*`, `_fader2_move_to_sixteenth_step` | `flows/note_edit_smoke` + `actions/encoder` |
| `_notelen_delete_or_create_note`, `_toggle_length_edit_mode` | `actions/buttons` + `flows/note_edit_smoke` |
| `_extract_revt_notes`, `_wait_for_revt_count` | `serial/protocol.py` + `verify/wait` |
| `_verify_session_state_enter`, `_verify_live_record_display` | `verify/capture` + `verify/display` |
| `_verify_note_length_integrity`, `_verify_edit_move_display` | `verify/playback` |
| `_parse_position_edits`, `_parse_moved_note_events`, `_parse_final_notes` | `serial/protocol.py` + `verify/playback` |
| Edit-specific serial substring helpers (`_serial_contains*`, `_line_is_scoped_edit_pass_*`) | `verify/playback` (use protocol parsers) |

### Shared / duplicated (`capture_transitions.py` + both legacy files)

| Legacy helper | Target owner | Note |
|---------------|--------------|------|
| `_count_capture_transitions`, `_count_capture_state_entries` | `verify/transitions` | merge with legacy ST waits |
| `_wait_for_transition_count`, `_wait_for_state_entry_count` | `verify/wait.py` facade | observation waits only |
| `_latest_track_state`, `_parse_disp_track_state` | `serial/protocol.py` | **single** parser; delete duplicates in legacy |
| `_parse_cap_micros` | `serial/protocol.py` | timestamp helper for line-index diagnostics |

### Scenario shims (delete Phase 3)

| File | Replacement |
|------|-------------|
| `scenarios/base.py` (`sys.argv` injection) | `registry` + `runner.py` |
| `scenarios/edit_full.py` | dropped (deferred to `edit-session-action-geometry`) |
| `scenarios/edit_record_prelude.py` | `flows/record_seed` composed in presets |
| `host_midi_automation_baseline.py` | `host_midi_hitl.py` |
| `host_midi_automation_edit_baseline.py` | `host_midi_hitl.py` |

### Port in Phase 2 vs Phase 3

| Phase | Scenarios |
|-------|-----------|
| **2** | `record_seed`, `record_overdub`, `edit_minimal`, `long_loop_display_window`, `slot_queued_start` |
| **3** | `two_overdub_undo_redo`, `edit_overdub_during_note_edit`, `note_edit_select_dependent_faders`, `current_set_incremental_save`, `revision_*`, `load_save_*`, `fader_motor_*` |

**Exit:** inventory + OpenSpec stub committed; zero production code changes.

---

## Phase 1 — Foundation

**Status:** complete (2026-08-04).

1. **Extract Part I** → [`docs/Architecture/HITL_ARCHITECTURE.md`](../Architecture/HITL_ARCHITECTURE.md).
2. `HitlConfig`, `bootstrap.py`, `session.py`, `ActionContext`, `ScenarioContext`, `ScenarioResult`, `VerificationResult` + `VerificationFailure`.
3. `foundation_runner.py` orchestration (`run_layered_preset`); `reporting.py` skeleton.
4. `layered_registry.py` — `LayeredScenarioSpec`, `PresetSpec`, verifier lookup (parallel to legacy `registry.py`).
5. `actions/*`, `serial/protocol.py` (ST + RECS).
6. Skeleton `flows/*` + `scenarios/layered_stubs.py`.
7. Host tests: `test_hitl_import_gate.py`, `test_hitl_actions.py`, `test_verification_result.py`, `test_serial_protocol.py`, `test_foundation_runner.py`.

**Exit:** `pio test -e native` green; Python HITL tests pass via `.venv/bin/python -m unittest` in `scripts/`. Legacy `runner.py` unchanged until Phase 3.

---

## Phase 2 — Core scenarios (UIP 5.5 gate)

| Scenario | Flow | Verifier |
|----------|------|----------|
| `record_seed` | `scenarios/layered` → legacy `run_base_scenario` | `verify.capture` |
| `record_overdub` | `scenarios/layered` → legacy `run_base_scenario` | `verify.capture` ([HITL-Test-Flow](../../.cursor/rules/HITL-Test-Flow.mdc)) |
| `edit_minimal` | `scenarios/layered` → legacy `run_edit_minimal_scenario` | `verify.playback` |
| `long_loop_display_window` | `scenarios/layered` → legacy `run_long_loop_display_window` | `verify.display` |
| `slot_queued_start` | `scenarios/layered` (slot short-press) | `verify.playback` |

**Status:** code landed (2026-08-04); managed dual-session default; device PASS on `uip_5_5` pending.

**CLI:** `host_midi_hitl.py run --layered --preset uip_5_5 …` — spawns `capture_session.py` + HITL (no manual second terminal). Legacy path unchanged without `--layered`.

**Exit:** device HITL PASS on preset `uip_5_5`; check off UIP **5.5**.

---

## Phase 3 — Port remainder + delete legacy

Port: `two_overdub_undo_redo`, `edit_overdub_during_note_edit`, `note_edit_select_dependent_faders`, `current_set_incremental_save`; wire `revision_*`, `load_save_*`, `fader_motor_*` to new contexts.

**Delete:** `legacy_*`, `host_midi_automation_*`, `scenarios/base.py`, `edit_full.py`, `edit_record_prelude.py`.

**Drop:** `edit_full` preset.

**Exit:** `rg 'legacy_record_baseline|legacy_edit_baseline|host_midi_automation' scripts/` → empty; protocol regex grep gate passes.

---

## Phase 4 — Tests, capture bundles, long-lived docs

- **Capture bundles:** on pass, `reporting.py` writes bundle per §11.3 (`report.json`, `serial.log`, `metadata.json`, `config.json`, `firmware.txt`).
- **Stable report schema:** `schema_version` in `metadata.json` and `report.json` (start at `1`); extend via additive fields only.
- **Verify-only replay:** `--verify-serial-log` against archived bundle `serial.log` (document in `HITL_REGRESSION_WORKFLOW.md`).
- Rewrite [`baseline_loop_inventory.py`](../../scripts/hitl/baseline_loop_inventory.py) to index corpus bundles.
- **New guides:**
  - [`HITL_DEVELOPER_GUIDE.md`](../Guides/HITL_DEVELOPER_GUIDE.md) — extend, run, ownership, checklist
  - [`HITL_REGRESSION_WORKFLOW.md`](../Guides/HITL_REGRESSION_WORKFLOW.md) — §11 workflow + policies
- Update [`HITL_TEST_SCENARIOS.md`](../Guides/HITL_TEST_SCENARIOS.md) for new scenario/preset catalogue.
- **Slim** [HITL-Test-Flow.mdc](../../.cursor/rules/HITL-Test-Flow.mdc) — canonical commands + short agent rules; link to long-lived docs (no architecture duplication).
- Cross-link from [`hitl_modular_scenarios_enhancement.md`](hitl_modular_scenarios_enhancement.md) → architecture + developer guide.
- Host tests for report/bundle writer and metadata fields.
- Sync `HITL_ARCHITECTURE.md` if implementation drifted from Part I staging.

---

## Phase 5 — Post-migration cleanup + archive

After functional migration and UIP 5.5 gate:

- Remove obsolete helpers left in `midi_io`, `edit_controls`, `capture_transitions` superseded by new layers.
- Delete stale comments referencing legacy scripts.
- Run import / dependency analysis; confirm gates still pass.
- Remove empty placeholder modules.
- Verify ownership table in `HITL_ARCHITECTURE.md` still accurate.
- **Archive migration artifacts:**
  - `openspec/changes/hitl-cli-rebuild/` → `openspec/changes/archive/`
  - `docs/plans/hitl_cli_rebuild_enhancement.md` → `docs/plans/archive/` (or delete after OpenSpec archive; retain pointer in `HITL_ARCHITECTURE.md` changelog)
- Update `docs/runtime/CURRENT_WORK.md` / `PROJECT_STATE.md` — HITL rebuild complete.

**Exit:** no `TODO(migration)` markers; `test_hitl_import_gate.py` green; long-lived docs are sole HITL authority; migration plan archived.

---

## What we are not doing

- Legacy compatibility layer.
- `edit_full` overlap matrix (deferred to `edit-session-action-geometry`).
- Firmware / `#CAP` contract changes.
- God modules (`bootstrap`, `flows` package).
- Speculative abstractions per §12.1 (`ScenarioRunner`, plugin system, generic execution framework, service locator).

---

## Session order

1. Phase 0 → Phase 1 → Phase 2 (UIP 5.5) → Phase 3 → Phase 4 → Phase 5.
