# Preflight — runtime-profiling-layer

From [`docs/Templates/PREFLIGHT.md`](../../../../docs/Templates/PREFLIGHT.md), lightweight mode (no formal
reassessment trigger: additive observation layer, no ownership movement). Tasks 0.3 and 0.4.
Recorded 2026-10-31.

## Problem

Add a compile-time-tagged profiler (profile env only) so timing questions are answered by a dump, not
a new bespoke row and reflash. No behavior change in any image; the default images stay identical.

## Domain

Observability / session capture (Tier-A rows, RAM1 budget), with read-only scopes in Playback,
Persistence, Display and the idle owner. Decision review: [DECISION_REVIEW.md](DECISION_REVIEW.md).

## Branch and tree

| Item | Value |
|---|---|
| Branch | `feature/unified-rolling-playback-path` |
| Firmware baseline HEAD | `5ddba83e` (docs-only commits `fec18994` and later on top) |
| Tree at baseline build | clean except this change's own docs (`CURRENT_WORK.md`, `DECISION_LOG.md`, change folder) |
| Unrelated modifications | none |

## Baseline figures (P3 — taken before any firmware edit)

| Env | RAM1 line (`check_ram1_budget.py`) | HEX SHA-256 |
|---|---|---|
| `teensy41-capture-serial` | variables **96,192** / ITCM 393,216 (code **391,928** + padding **1,288**) / locals 34,880; floor 840 (448 B headroom) | `2c7bb5d340395ae54ad65f5c310de058cc7a04a8ff383c62dd1462efc67927fc` |
| `teensy41-capture-serial-no-grain` | variables 96,192 / ITCM 393,216 (code 391,928 + padding 1,288) / locals 34,880 | `22d5638f4208d49124fcd8d47ace5b9f45373467a6bee81bb8a2f9daa1b6176d` |

The default hash `2c7bb5d3…` is the image `CURRENT_WORK.md` records as running on the board (DEC-238
"shipped off"), so the P8 reference capture can run without a reflash once boot is re-verified. A copy
of the HEX is retained in the session scratchpad for the slice-4 A/B.

**Native baseline:** 3,782 cases / 8 skipped / 3,774 succeeded ([§ Native baseline](#native-baseline)).

**Headroom warning:** padding is 1,288 B, only 448 B above the 840 floor. The profile env must not add
ITCM code: the record path, primitives and dumps are `FLASHMEM`; scope bodies are inline only for the
two counter reads and a call into a `FLASHMEM` record function.

## Boot hook (P6)

`setup()` in `src/main.cpp`, in order:

1. `Serial.begin`, wait for USB, crash-report handling.
2. `emitBootResetCapture(hadCrashReport)` (`SESSION_CAPTURE`) — first capture row.
3. Restore-fault breadcrumb handling.
4. `MemoryPool::globalMidiEventPool.init()`, `LoopEventStore::initPool()`.
5. `trackManager.allocateLoopsEarly()`, **`trackManager.prewarmPlaybackRuntime()`** — the first call
   that can reach an instrumented owner (`prewarmPlaybackForSlot` / `prewarmViews`).
6. Later: `trackManager.setup()`, display boot screen, `looper.setup()` (SD + load → `LoadStep`), clock.

**`RuntimeProfiler::initAtBoot()` goes immediately after `emitBootResetCapture(...)`** (step 2), inside
`#if RUNTIME_PROFILE_SCOPES`: Serial and the capture ring exist (the boot row can be emitted
direct-first) and no instrumented owner has run yet. Calibration (≤ 2 × 256 empty scopes, ~100 µs) is
negligible at boot.

## Build and test commands

| Slice | Commands |
|---|---|
| all | `pio test -e native` (background, in parallel with the device path) |
| 1 | `pio test -e native -f test_runtime_profiler`; `pio run -e teensy41-capture-serial-profile`; `pio run -e teensy41-capture-serial`; `pio run -e teensy41-capture-serial-no-grain` (figures must equal the table above) |
| 2 | slice 1 + `pio test -e native -f test_capture_ring -f test_capture_line_tier`; `python3 scripts/check_marker_contract.py 'DIAG,profile_…,'` ×5 and `--audit`; `.venv/bin/python -m pytest scripts/test_profile_report.py`; upload `pio run -e teensy41-capture-serial-profile -t upload` |
| 3–4 | slice 2 + owner rig ([HITL_TEST_RECIPE.md](HITL_TEST_RECIPE.md)); `scripts/parse_overdub_chain_timing.py <prev>.log <new>.log --windows` |

## Required gates

- Default and no-grain RAM1/ITCM and HEX hash identical to the table (slices 1–2: flag absent ⇒ no
  byte change; the hash check is the strict form).
- Profile env at variables 96,192 and padding ≥ 840.
- Native: no new failure against the baseline count.
- Device legs: exact HEX hash + `boot_reset,crash=0` + `BOOT,load,ok`; run validity per the timing
  handoff.

## Native baseline

`pio test -e native` at the baseline tree (2026-10-31): **3,782 test cases: 8 skipped, 3,774 succeeded**, exit 0, no `[ERRORED]` suite (5 min 20 s). Slice gates compare against these counts.
