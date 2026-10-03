# Slice 3.10 (increment 11) — the service-gap sampler is written and proved natively, and **blocked by the held RAM1 pin**

**Status:** 2026-10-03. **LANDED — see "Landing" at the end.** The instrumentation was first refused by the RAM1 budget
guard (`variables 100,288` vs the approved `96,192`, **+4,096 B**) and was therefore reverted rather than re-basing a pin
the project deliberately holds; the guard's cause was the DTCM `ALIGN` page being **within a few bytes of its boundary**.
An **unrelated concurrent workstream on this branch (a logging/telemetry optimisation) freed ITCM** — the ITCM padding went
from **888 to 3,128** — and the same instrumentation then linked with `variables` back at exactly the approved `96,192`.
No pin was re-based, and no routing change was needed.

## What was implemented (native-green before the guard refused the link)

- `DebugSessionCapture::windowServiceGapImmediate(maxDeltaTicks, opportunities, phase)` + the `#ifndef`-guarded
  **`SC_WINDOW_SERVICE_GAP`** macro in both regions (the `SC_WINDOW_READINESS` pattern) — emitted directly when USB has
  room, ring as fallback, row `#CAP,<micros>,DIAG,win_gap,<maxDeltaTicks>,<opportunities>,<phase>`.
- The sample taken at the **top of every lane opportunity** (`stepPlaybackViewMaintenance`), so a turn that serves
  nothing still counts: the gap is about *when* the lane is served. State `sGapLastPhase` / `sGapMaxDelta` /
  `sGapSampleCount`, **`DMAMEM`** by DEC-108 §13b's placement rule; cadence one row per `static constexpr
  kServiceGapSampleInterval = 16` **opportunities**; each row reports the **max gap since the previous row** (the
  running-max idiom `TelemetryCadence` documents, so a bound read from the series is faithful rather than sampled).
- `DIAG,win_gap,` classified **Tier-A** (`isTierATextLine`), because it is produced under transport — the regime that
  sheds non-Tier-A diagnostics.
- Fixture `test_window_service_gap_sample_reports_the_measured_phase_advance_per_opportunity`: drives a known phase
  progression (a quiet 1 000 with one 5 000 spike), primes the process-wide cadence counter, and asserts the max, the
  **per-row reset** (a quiet block reports 1 000), the opportunity count, and one row per declared interval.
  **Ownership suite 74/74.** Three mutations red-proved with byte-identical restores: no running max →
  `Expected 5000 Was 1000`; no per-row reset → `Expected 5000 Was 91400`; interval 16 → 8 → `Expected 1 Was 2`.

## Why it did not land: the RAM1 guard, and it was right to refuse

`[RAM1 budget] FAILED -- the approved RAM1 budget changed: variables changed: 100,288 B vs approved 96,192 B (+4,096 B)`,
with **`code` unchanged** (392,328) and `locals` down to 30,784 ⇒ exactly **one `ALIGN` page of DTCM `.data`/`.rodata`**,
the trap the RAM1 playbook documents.

**Diagnostics run (each a rebuild):**
| Test | Result |
|---|---|
| tier literal written into the **header** tier list | `100,288` ✗ |
| that literal removed | **`96,192` ✓** (restored) |
| literal moved into the **single TU** instead | `100,288` ✗ |
| the `inline constexpr` interval folded to `static constexpr` | `100,288` ✗ |

Conclusion: the DTCM section sits within a few bytes of an `ALIGN` boundary, so **any** addition of this scale tips it —
shaving the addition is not a fix. (It also means the image was already one small string away from tipping, which is the
playbook's own fragility warning.)

## The fix, and the decision it needs (not taken here)

Route that TU's read-only data to FLASH in `linker/imxrt1062_t41_lcr.ld`, exactly as lines 98 / 99 / 113 already do for
three other translation units (`*…o(.rodata.str* .rodata.*.str* …)`). That **frees** more DTCM than the row costs, so
`variables` lands **below** 96,192 — and because the guard compares the approved value exactly, this requires
**re-basing `scripts/ram1_budget.json` in the same commit**. That pin is deliberately held project-wide (the standing
instruction is that the approved line stays 96,192 and is never re-based silently), so the choice is the user's:
re-base downward with the routing fix, or find ~4 KB of DTCM to retire first.

## State left behind

- **Everything reverted**: `include/Utils/DebugSessionCapture.h`, `src/Utils/DebugSessionCapture.cpp`,
  `include/Utils/CaptureLineTier.h`, `src/Track/TrackPlaybackWindowBuild.cpp`, and the two test files are back at
  `24134bd8`. Ownership suite **73/73**; `pio run -e teensy41-capture-serial` back at the approved
  `variables 96,192 / code 392,328 + padding 888` (48 B headroom).
- The implementation is described above in enough detail to re-apply in one pass once the pin decision is made.
- **Unrelated concurrent edit preserved:** `docs/Runtime/CURRENT_WORK.md` carries a 29-line block *"Timing-path findings
  1–4 — the tick edge, the DROID ring, and the per-note trace (2026-10-02)"* that is **not mine**; it is deliberately
  left uncommitted and untouched by this increment.


## Landing (2026-10-03, after the concurrent logging work)

Re-applied unchanged and it now fits, with **no re-base and no linker change**:

| Check | Result |
|---|---|
| `pio run -e teensy41-capture-serial` | **SUCCESS** — `variables 96,192` (**exactly the approved line**), `code 390,088 + padding 3,128`, `locals 34,880`; **2,288 B of headroom above the floor** (was **48 B**), floor not re-based |
| `test_runtime_working_set_ownership` | **76/76** |
| `test_capture_ring` | **16/16** (the Tier-A classification and its exact boundary, through the production predicate) |
| full `pio test -e native` | **2715 cases: 7 skipped, 2708 succeeded, 0 failed** |
| mutations, re-proved on the landed code, byte-identical restores (sha256 `71bfb33b…a2d33c`) | **M101** no running max → `Expected 5000 Was 1000`; **M102** no per-row reset → `Expected 5000 Was 91400`; **M103** interval 16 → 8 → `Expected 1 Was 2` |

**What the earlier blocker turned out to be.** Not a routing problem at all: the DTCM `.data`/`.rodata` section sat within a
few bytes of an `ALIGN` page boundary, so *any* addition of that scale tipped it by a whole page — which is why removing
one literal restored the line and moving it to a single TU did not. The concurrent workstream's ITCM savings gave the
page enough slack for the row, so the measurement instrumentation cost **zero net DTCM**.

**What this unblocks.** The review's step (1) is done: the unit now has an instrument. Next: the HITL leg with the
**stimulus's own targets** (`track 8`, slots 4–5) to collect the `DIAG,win_gap` series under a running transport — and
then the unit is measured, the supported `H × K` contention envelope declared, the derived admission re-based and
enabled, in that order.
