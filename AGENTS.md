# MidiLooper — agent entry

Teensy 4.1 MIDI looper firmware. C++17, PlatformIO. This file is the always-on
baseline for coding agents (DeepSeek Harness, Codex, Claude Code). It is a
**pointer and invariant layer**: authority lives in the docs it links, and the
repo's rule is "cite, do not duplicate".

Authority order, and the conflict-resolution table for doc-vs-doc and doc-vs-code
disagreements: [`docs/Authority/README.md`](docs/Authority/README.md). Read that
before resolving any conflict. Highest wins:

`PROJECT_INTENT` → `ARCHITECTURE_RULES` → `NAMING` → OpenSpec → Guides → Plans → Code.

Cursor-rule full text lives in `.cursor/rules/*.mdc` and is **not** auto-loaded
here; read the relevant file on demand using the map at the bottom of this file.

## Before coding (always)

1. [`docs/Runtime/PROJECT_STATE.md`](docs/Runtime/PROJECT_STATE.md) — execution
   context, active OpenSpec list.
2. [`docs/Runtime/CURRENT_WORK.md`](docs/Runtime/CURRENT_WORK.md) —
   implementation scope. **Do not implement outside § Now implementing.** If the
   task is not there, ask the user or update the doc first.
3. [`docs/Authority/PROJECT_INTENT.md`](docs/Authority/PROJECT_INTENT.md) and
   [`docs/Authority/ARCHITECTURE_RULES.md`](docs/Authority/ARCHITECTURE_RULES.md).
4. Search [`docs/DECISION_LOG.md`](docs/DECISION_LOG.md) for the topic — use the
   `grep` tool, never a full read (the file is >210 KB). Run
   [`docs/Templates/DECISION_REVIEW.md`](docs/Templates/DECISION_REVIEW.md)
   before firmware edits.
5. Detect the domain in [`docs/AGENT_CONTEXT_MAP.md`](docs/AGENT_CONTEXT_MAP.md)
   and load its **Required** docs. Orientation only:
   [`docs/Plans/runtime_process_building_blocks_overview.md`](docs/Plans/runtime_process_building_blocks_overview.md).
6. Decision ladder: extend the existing owner → reuse an extension point →
   follow the logged `DEC-###` → run a reassessment only on a
   [formal trigger](docs/ARCHITECTURE_REASSESSMENT.md) → otherwise **implement**.

7. Before each implementation slice, follow the
   [G2 execution packet](docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice):
   pin scope and transition oracle, establish controllable production-path fixtures where required,
   and record verification evidence before checkoff. Use [EXECUTION_PACKET.md](docs/Templates/EXECUTION_PACKET.md).
   This applies to future steps of existing changes; scale checks to risk, with no extra approval gate.

**Default: continue implementation.** Stop only on a formal reassessment
trigger or an authority conflict (OpenSpec vs architecture). State the preferred
path and confidence when alternatives exist; do not block on vagueness.
`docs/Runtime/ROADMAP.md` and `docs/Plans/**` are never implementation authority.

## Non-negotiables

- **Firmware env:** `teensy41-capture-serial` for build and upload unless the
  user explicitly names another env in this session.
- **Upload:** after a *successful* firmware build, **ask** before flashing the
  board. Exception: while evaluating the change with a dedicated HITL preset,
  upload and run without asking.
- **Device SD is user data:** quarantine (`!QUARANTINE_WORKSPACE`), delete or
  rename on the card only with explicit permission in the same session. The
  firmware has no restore command, so quarantine is one-way in practice — see
  [`Destructive-SD-Operations.mdc`](.cursor/rules/Destructive-SD-Operations.mdc).
- **Host tests:** run `pio test -e native` before push, merge, or finishing a PR.
- **HITL:** a firmware change observable over serial needs a **registered or
  reused stimulus** (`scripts/hitl/stimuli/`). List before adding:
  `.venv/bin/python scripts/host_midi_hitl.py stimulus list`. Never edit a
  canonical stimulus to chase a new condition; create a temporary variant, and
  promote to a new `STIM-…-02` only when the user accepts it as the reproducer.
- **HITL entry points:** run the authoritative entry point (baseline preset, or
  the `scripts/hitl_stop_interruption.py` wrapper); **never** execute a captured
  `command.json`. Managed runs are bounded by `HITL_MANAGED_RUN_TIMEOUT_S`
  (default 360 s). Recipe and evidence expectations:
  [`.agents/skills/hitl-content-verification`](.agents/skills/hitl-content-verification/SKILL.md);
  semantics:
  [`docs/Guides/HITL_CONTENT_VERIFICATION.md`](docs/Guides/HITL_CONTENT_VERIFICATION.md).
- **Instrumentation:** performance instrumentation stays gated behind session
  capture or an explicit debug gate.
- **Naming:** [`docs/Authority/NAMING.md`](docs/Authority/NAMING.md) is canonical
  — reuse `pass`, `Capture`, `editPass`, `NoteEditSession`, slot, overdub, jam,
  `JamAction`, HITL baseline. Action + scope naming.
- **Prose:** refer to functions by name; line numbers are for navigation and
  citations only. In every chat mention, pair stage/task/gate/decision IDs with a
  short name, including repeats and link labels: **S2b — boot/load admission**,
  **DEC-046 — round-robin playback admission**. Follow
  [short names beside IDs](docs/Authority/NAMING.md#short-names-beside-ids-in-chat).
- **Git:** small doc-only hygiene → direct commit on `dev`, no PR. Firmware,
  refactor and **host-tooling** stages (scripts, probes, host modules, their
  tests) → commit on the **working branch**, never `dev`: `dev` holds the last
  stable version and day-to-day integration is the active long-lived branch
  (currently `feature/runtime-working-set-ownership`). PR only on explicit
  request. No auto-PR; a large-work direct commit on `dev` needs an explicit
  exception.

## Closeout (always)

- Update [`docs/Runtime/PROJECT_STATE.md`](docs/Runtime/PROJECT_STATE.md) and
  [`docs/Runtime/CURRENT_WORK.md`](docs/Runtime/CURRENT_WORK.md).
- Update [`docs/DELIVERABLE_TRACKING.md`](docs/DELIVERABLE_TRACKING.md) when
  something ships, and the active OpenSpec `tasks.md`.
- A behavioral change is **not closed** while durable docs still describe the old
  behavior — see
  [`docs/Authority/DOCUMENTATION_CLOSEOUT.md`](docs/Authority/DOCUMENTATION_CLOSEOUT.md).
- Append a `DEC-###` entry when `ARCHITECTURE_RULES.md` requires one, and run
  [`docs/Templates/SESSION_CLOSEOUT.md`](docs/Templates/SESSION_CLOSEOUT.md)
  when design or exclusions were discussed.
- If overlap-resolve, ledger, occupy-catch-up, or playback-order logic changed,
  update [`docs/Guides/OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md`](docs/Guides/OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md)
  and/or [`docs/Guides/OVERDUB_LEDGER_NOTE_EVALUATIONS.md`](docs/Guides/OVERDUB_LEDGER_NOTE_EVALUATIONS.md).

Delivery and verification matrix:
[`docs/Authority/DELIVERY_RULES.md`](docs/Authority/DELIVERY_RULES.md).
Optional roles: [`docs/Agents/architect.md`](docs/Agents/architect.md),
[`docs/Agents/builder.md`](docs/Agents/builder.md),
[`docs/Agents/reviewer.md`](docs/Agents/reviewer.md).

## Cursor-rule map (read on demand)

| Situation | Rule file in `.cursor/rules/` |
|---|---|
| Context loading before/after coding | `Agent-Context-Workflow.mdc` |
| Build env and upload defaults | `Teensy-Capture-Serial-Default-Build.mdc`, `Teensy-Upload-After-Build.mdc` |
| Tests before push/merge/PR | `Testing-Workflow.mdc` |
| Implementing firmware + HITL | `HITL-Implementation-Evaluation.mdc`, `HITL-Test-Flow.mdc`, `HITL-Edit-Test-Flow.mdc` |
| Capture / stop / undo, passes, slots | `Loop-MIDI-Storage-And-Validation.mdc` |
| Overlap-resolve and ledger owners | `Overdub-Overlap-And-Ledger-Evaluations.mdc` |
| Multi-stage bugfix | `Multi-Stage-Bugfix-Workflow.mdc` |
| Authoring or reviewing a plan | `Plan-Authoring.mdc`, `Plan-Pre-Implementation-Review.mdc` |
| Deterministic implementation and completion evidence | `Implementation-Execution-Contract.mdc` |
| OpenSpec phase gates and workflow | `OpenSpec-Phase-Gate.mdc`, `OpenSpec-Workflow.mdc` |
| Git / PR decisions | `Git-Commit-And-PR-Workflow.mdc` |
| Vocabulary and naming | `Naming-Vocabulary-Teensy-Looper.mdc` |
| Translation-unit splits | `Mechanical-TU-Split-Workflow.mdc` |
| RAM1 linker overflow | `Teensy-RAM1-Bank-Overflow.mdc` |
| Device-SD quarantine / delete / rename | `Destructive-SD-Operations.mdc` |

## Skills

`openspec-*` skills load automatically from `.agents/skills/`. Prefer
`openspec-explore` or `openspec-propose` for planning, `openspec-apply-change`
for implementation, and archive only after the native (plus HITL where required)
gates pass. For hardware content parity or a dense HITL rerun, use
`hitl-content-verification`.

## Untrusted, generated, and disposable areas

- `captures/` — gitignored, ~3.2 GB across 2 700+ session logs. Never read a
  whole capture log; read the tail or use a `scripts/parse_*.py` helper. The
  `grep` tool skips it via ripgrep's ignore rules, but the `glob` tool runs with
  `--no-ignore --hidden` and will surface its files.
- `compile_commands.json` — generated by PlatformIO; not authored by hand.
- `.pio/`, `.venv/`, `.venv_bom/` — build and tooling state, not source.
- `openspec/changes/archive/` and `docs/Plans/archive/` — historical; verify
  against code before relying on them, and never treat them as a live queue.

## Environment

OpenSpec CLI requires Node.js >= 20 (`nvm use 20`). Host tests use the PlatformIO
`native` env; suites that include full firmware or the PSRAM sketch are listed
under `test_ignore` for `native` and need `-e teensy41` or hardware.
