# Execution packet — slice 1 (runtime core)

From [`docs/Templates/EXECUTION_PACKET.md`](../../../../docs/Templates/EXECUTION_PACKET.md). Task 2.1.

## Identity and scope

- **Tasks:** 2.1–2.7. Contract: [spec](../specs/runtime-profiling/spec.md), design D2–D4, D6, D7, D13,
  D14; DEC-239.
- **Status:** native verified; build/RAM1 verified; device NOT RUN (device leg is task 3.7, slice 2).
- **Owner / entry:** `RuntimeProfiler` (`include/Utils/RuntimeProfiler.h`,
  `src/Utils/RuntimeProfiler.cpp`); device boundary `src/Utils/RuntimeProfilerPlatform.cpp`; names and
  the boot-row format `src/Utils/ProfileDiagRows.cpp` + `include/Utils/ProfileDiagRows.h`; one call
  `RuntimeProfiler::initAtBoot()` in `setup()` (`src/main.cpp`). No production scope site.
- **Harness:** new `test/test_runtime_profiler` (includes the two portable TUs, defines
  `RuntimeProfiler::Platform` with fake clocks).
- **Preconditions present:** P1–P6 (CURRENT_WORK entry, DEC-239, PREFLIGHT baselines, boot hook, profile
  env + budget alias, linker rule).
- **Deviation from tasks.md, recorded:** `ProfileDiagRows.cpp` was created in slice 1 (task 3.2 lists
  it in slice 2) because the boot row (task 2.6) needs a carved format literal. Slice 2 extends the
  same TU; nothing else moves.
- **Non-goals:** any production scope, any serial command, Tier-A registration (slice 2), any change
  to `HotPathTelemetry` / `Diagnostics::Timing`.
- **Commit boundary:** independently buildable; default and no-grain envs link to the baseline figures.

## Oracle (fixed in the test, before acceptance)

| Case | Discriminating assertion |
|---|---|
| boot clear | RAM2 state pre-filled with `0xA5` reads zero after `initAtBoot()` |
| calibration | 256 samples; with 7 cycles per read, short/long min = avg = 21; calibration id has no residue; one boot row |
| unavailable source | `cpu_hz=0` → `source=0` row emitted once; short and long both refused (`refused=2`, `calls=0`) |
| every exit path | early `return false` and final `return true` both timed; a zero-length call is recorded as 0 |
| inclusive/self | parent 1,615 µs inclusive, 115 µs self with a long and a short child |
| result/context | none → `0xFF`; supplied → stored and carried in the ring; reference form reads the variable at exit |
| ceiling entry | = ceiling is not a breach; > ceiling enters the ring with seq 1 and the immediate parent |
| ring overwrite | 260 breaches → 256 kept, `dropped=4`, oldest seq 5, newest 260 |
| source boundary | wall Δ = trust − 1 → exact cycles; Δ = trust and trust + 1 → coarse, cycles saturated |
| short primitive | ignores the wall clock |
| coarse child | 10 s child: `maxUs=10,000,000`, inclusive 6·10⁹ cycles (> u32), parent self 150 µs |
| order fault | out-of-order delete → fault, poisoned, no durations; reset refused at depth 1, allowed at 0 |
| depth overflow | 18 nested under a root: `depthOverflow=3`, no double counting (self total 21 µs) |
| exception context | entry with `IPSR ≠ 0` refused, `isrRefused=1`, outer frame untouched, interrupt time included in the outer scope |
| reset | seq stays monotonic across reset |
| names/ceilings | every id has a unique, non-empty name; every ceiling < 1 s; owner-turn = 4,000 µs |

## Controls and coverage

- Seams: `RuntimeProfiler::Platform::{cycles, micros, ipsr, cpuHz, emitRow}` only. No production branch
  for tests.
- Native `DMAMEM` expands to nothing (host zero-initialization), which is why the boot-clear case
  pre-fills the state first.
- Not testable natively: the real DWT read, `IPSR`, flash placement and the direct-first serial sink —
  covered by the build/ELF checks below and the slice-2 device leg.

## Completion record

- **Fixture discrimination (mutation checks, 2026-10-31):** the suite passed on its first run, so the
  RED step was demonstrated by mutation instead, each restored afterwards:
  - removing the boot `memset` → 6 cases fail (`Expected 0 Was 2779096485`);
  - overflow scope always reporting to the top frame → depth-overflow case fails (`Expected 12600 Was 12000`);
  - disabling the coarse path → boundary and coarse-child cases fail (`Expected 6158279 Was 20`);
  - disabling the IPSR guard → exception-context case fails (`Expected 1 Was 0`);
  - adding an id past capacity → `static_assert` "ProfileId exceeds the table capacity" (task 2.2).
- **Native focused:** `pio test -e native -f test_runtime_profiler` → 16/16 before review; **21/21** after
  the review fixes (`evidence/REVIEW-slice1.md`), with three more mutations caught (unmasked ring head →
  SIGBUS; overflow order check removed; boundary `>=` → `>`).
- **Native full:** see § Native full run below.
- **Build / RAM1 (task 2.7):**

  | Env | RAM1 line | Note |
  |---|---|---|
  | `teensy41-capture-serial-profile` | variables 96,192 / code 391,928 + padding 1,288 / locals 34,880 | profiler code at `0x600a…–0x600ac5…` (FLASH), `RuntimeProfiler::detail::gState` 9,536 B at `0x202234e0` (OCRAM2/`DMAMEM`), name and format literals in `.text.progmem` |
  | `teensy41-capture-serial` | identical to P3 | see "default unchanged" below |
  | `teensy41-capture-serial-no-grain` | identical to P3 | |

  Profile HEX SHA-256 after review fixes `7dcc2523d676a136bd17b9e96212e9f467c3b907f4e7512ed732858dda1cd596`;
  both profiler `CSWTCH` tables in FLASH. Default ELF vs pristine: only `__rtc_localtime` differs (veneers
  excluded), section headers identical.

- **Default image unchanged — method corrected.** The P3 HEX hash cannot serve as the oracle: every
  link stamps `__rtc_localtime` (the core's build-time RTC seed) and GNU ld emits the 1,443 long-branch
  veneers in a varying order, so two links of the *same* tree give different hashes (observed:
  `1e1cf1a8…` and `50631bd5…` for this tree, `102471187…` and `153e39c2…` for pristine sources). The
  oracle used instead: `arm-none-eabi-nm -n` of the default ELF with this slice vs a pristine-source
  build, veneers excluded → **one** differing symbol, `__rtc_localtime`; section headers
  (`objdump -h`) identical; RAM1 line identical. This is recorded as the gate for later slices.
- **HotPathTelemetry / Diagnostics::Timing:** no diff (`git diff --stat` touches only the files above).
- **Device verification:** NOT RUN in slice 1 (task 3.7).
- **Independent review:** accept with fixes; all six fixed — `evidence/REVIEW-slice1.md`.
- **Eligible for checkoff:** 2.1–2.7.

## Native full run

- Before review fixes: `pio test -e native` → **3,798 cases: 8 skipped, 3,790 succeeded** (baseline 3,782 + 16), exit 0.
- After review fixes: `pio test -e native` → **3,803 cases: 8 skipped, 3,795 succeeded** (baseline 3,782 + 21), exit 0, no `[ERRORED]` suite.
