# S2.2/S2.3 — each class measured separately, and the §2 envelope reported as it stands

**Status:** measurement and report only. No firmware, no `src/`/`include/` change, no build, no device action.
**S2.2** measures `P`/`C`/`D`/`I` separately and refuses to size the specialized classes by the playback target;
**S2.3** charges §2's envelope with source-derived holder maxima and reports the result — including the case where
it does not fit, which is what §2 asks for: *"If the conservative sum does not fit, report that result: do not
shrink cardinalities silently, claim a proved N, or pre-provision extra finalizers."*

`P` (the playback window lease) is derived in [`S2C1-PLAYBACK-WINDOW-LEASE.md`](S2C1-PLAYBACK-WINDOW-LEASE.md);
this document charges it beside the other classes. Everything is in KiB over the **device ABI**
(`sizeof(MidiEvent) = 20`, measured in `evidence/2026-10-01-s2c1-abi-measurement/`).

## 1. The budget, and its measured floor

The envelope is `IMPLEMENTATION_CONTRACT.md` §2:
`fixed + N*P + Cmax*C + Dmax*D + Imax*I + Omax*O + canonical + roots + undo + visual + standby + reserve <= PSRAM`.

| | value | provenance |
|---|---|---|
| the pool | **8 MiB** | the external pool is 8 MiB (`docs/Guides/INTERNAL_HEAP_AND_EXTERNAL_MEMORY.md`; the pool walk is measured at 593 ms *on 8 MiB*) |
| the **measured ceiling** | **< 8,192 KB** | the 24-bar heap runs are the close of the range: `MEM_EXT_REFUSED bytes=8192 … live=8527KB` and `external live 8144 KiB / 1176 blocks` immediately before an allocator `FATAL`. The reserve term is what keeps the envelope below this |
| boot `live` on a **full library** ("fixed" anchor) | **1,801 KB** (1,949 settled) | S2b.3 census, `untracked=0`/`unknown=0`; `canonical ≈ 0` here because committed slots are **descriptors only** at boot (S2b), so no event bodies are resident |
| settled `live` at **N = 8 with one take's runtime** | **6,505 KB** | S4 gates holder census; margin to the pool **1,687 KB** |

**Reading caution that changes conclusions if ignored:** `chunks_free` counts *ids*, not backable capacity — the
census can read 320 free chunks while every append is refused (S2b.2's finding). `live` is the figure to read here.

## 2. What each class holds — and what is charged once

**The shared backing is charged once, not per class.** One slot runtime owns exactly one
`RuntimePlaybackViewSet`, and P, C and D all read it: the views and bridges are the playback representation, while
the two capture stagings are live-ingress at RECORDING and the frozen role at punch-out. §2's rule —
*"Charge shared backing once, report which holders share it"* — is therefore applied literally:

| allocation | bytes (device) | who shares it |
|---|---|---|
| one `RuntimePlaybackViewSet`: rolling views 210 + bridge bases 140 + bridges 360 + capture stagings 121 | **831 KiB whole-take / 331 KiB windowed** | **P** (views, bases, bridges) **+ C** (the live staging) **+ D** (the frozen staging role) |

The **incremental** charges beyond that shared set:

| class | per holder | composition (each cited to `ExecutionBounds.md`'s resident-store table and its lifetime classes) |
|---|---|---|
| **C** — live-capture extension | **383 KiB** | the six prewarms its own lifetime class (b) requires *during RECORDING*: `canonicalPassInputs` 144, `frozenCompanionPasses` 96, `frozenCompanionApply` 66, `frozenCompanionReconstruction` 64, `frozenCompanionPairLookup` 10, `frozenCompanionSpans` 2 — plus the transfer horizon ~1.1. The live event bodies themselves are `canonical` (the chunk pool), not C, per §2's "until S6, `canonical` includes all resident event bodies" |
| **D** — deferred-finalization extension | **308 KiB** | its lifetime class (c), capacity that must already exist at arm: `frozenCaptureCompanions` 80, `frozenCompanionBridgeMap` 68, `preparedCompanionHistory_.rows` 80, and the `passes.editPasses` canonical reserve 80 (which becomes canonical history thereafter — charged once, reported) |
| **O** — inbound switch preparation | **≤ 1 windowed P per in-flight switch (331 KiB)** | the inbound slot's *own* runtime must exist **in addition to** the outbound's before the switch lands (S4b.2's ordering, S4b.7's off-clock `SlotProvision`). S4b.2 measured the turn's own cost as **1 pool chunk**, because the provisioned slot reuses nothing it already held — the conservative charge is the whole view set, and the measured one is far smaller |
| **I** — interactive | **not sized here** | edit / undo / display. §2 fixes `Imax = 1`, but the byte figure belongs to its owners (the visual envelope is S4e/S2.8; deferred undo work and its pinned roots stay charged under `undo` for their entire lifetime) |

**The specialized classes are not sized by the playback target.** C (383 KiB) and D (308 KiB) are the doc's own
lifetime classes; neither is a multiple of P, and `P`'s derived capacity (S2.1) does not move them.

## 3. Holder maxima, source-derived

| term | max | source |
|---|---|---|
| `N` | **8** today (8 tracks × 1 sounding slot) | design; the target is 16/32/48/64 |
| `Cmax` | **8** = `Config::NUM_TRACKS` | §2; one live capture per track (`Track::hasLiveCaptureInterval`, `include/Track.h:425`) |
| `Dmax` | **8** = `Config::NUM_TRACKS` | §2, for the current admission guard (`Track::hasPendingDeferredOverdubStop`, `include/Track.h:419`) — one admitted stop per track. The descriptor ceiling 32 is explicitly **not** evidence of 32 finalizers |
| `Imax` | **1** | §2 |
| `Omax` | **8** = `Config::NUM_TRACKS` | §2; at most one inbound preparation per track (`SlotStateMachine::hasPendingSlotSwitch`, `include/SlotStateMachine.h:47`), and a replaced target retires its preparation before another is provisioned (S4b.1) |

## 4. The conservative sum **does not fit**

Every class at its maximum at the same time, with the windowed `P`:

```
fixed      1,801
N*P        8 x    331 =  2,648
Cmax*C     8 x    383 =  3,065
Dmax*D     8 x    308 =  2,464
Omax*O     8 x    331 =  2,648
------------------------------------------------
sum                    12,626 KiB   vs   8,192 KiB pool      ->  OVER by 4,434 KiB
```

With the whole-take `P` it is 20,626 KiB. **Reported, not resolved**, exactly as §2 requires: the cardinalities are
not shrunk, no `N` is claimed proved, and no extra finalizers are pre-provisioned. The reason is structural and
worth stating plainly: `Cmax*C + Dmax*D + Omax*O = 8,177 KiB` is on its own the entire pool, so the conservative
sum cannot fit **at any N** — it is not the playback term that overflows it.

## 5. The representative cases (what the device actually runs) all fit

| concurrent state | KiB | vs 8,192 |
|---|---|---|
| N = 8 sounding, nothing else | **4,449** | fits, margin 3,743 |
| + 1 live capture | **4,832** | fits, margin 3,360 |
| + 1 capture + 1 finalizer | **5,140** | fits, margin 3,052 |
| + 1 capture + 1 finalizer + 1 in-flight switch | **5,471** | fits, margin 2,721 |

The last row is the overlap case the design calls the transient headroom (*"the transfer overlap is the transient
headroom in the budget arithmetic; it is not slack"*), and it is the state the S4 gates census measured rising into
and settling out of (6,505 KB settled at N = 8 with one take's runtime resident — consistent in magnitude, and
higher than the projection because the library's chunk content is resident too).

## 6. `N` at today's capability, and the headroom — **provisional, bounded by phase F**

* **Today: N = 8** (8 tracks × 1 sounding slot), which is the design's stated capability, not a derived bound.
* **At today's windowed `P`:** the measured margin (8,192 − 6,505 = 1,687 KB) buys `1,687 / 331 ≈ 5` more holders
  → **N ≈ 13** sounding slots, if nothing else grows. **N = 16 already costs 5,296 KiB** of `N*P` alone (65 % of
  the pool), and 32/48/64 take 10,592 / 15,888 / 21,184 KiB — so at today's ceiling those targets are unreachable
  **for the playback term**, whatever the other terms do.
* **At the derived window `P`** (S2.1: a 2-bar window at typical density = **27.9 KiB**), the same targets cost
  **447 / 894 / 1,340 / 1,787 KiB** for N = 16/32/48/64 — so the playback term stops being the binding term and
  `N` is then bounded by `canonical` + `roots` + `undo` + `visual` + `standby` + `reserve`, which is why those
  terms must be owned before any `N` is asserted. **This is the shape that needs a smaller `P`** (S2.4's note), and
  it is why the `N × P` claim stays provisional until phase F lands.

## 7. Owed, named (not claimed)

* **The device validation §2 names:** *"simultaneous live capture/finalization on separate tracks, switching on
  multiple tracks, and target replacement during preparation. A single-track trace cannot prove the device
  budget."* The nearest evidence today is the S4b/S4gates leg, which is **one track** — so this is owed, and it is
  the reason this document reports rather than proves.
* **The terms owned elsewhere:** `canonical` (all resident event bodies until S6), `roots` (whole-loop
  playback/checkpoint roots until phase F — note this is the term `design.md` Revision 11 puts at ~325 KB per
  loop), `undo`, `visual` (S4e/S2.8), `standby` (S4d), `reserve`. None is hidden inside `reserve` here; none is
  invented.
* **The product limit** (S2.1 §5) and **`I`'s byte figure** are the other two inputs this report cannot supply.
* **No red-proof applies** (no code changed), and the class separation itself is already pinned where it lives:
  the lease's role vocabulary and its fixtures (S3.1, S4.4's named-holder case), cited rather than duplicated.

## Measured against the owner's requirement (2026-10-02)

The owner's target — **all 64 slots, 64 bars, 3 notes per 16th** — was measured against this budget rather than
argued: **393,216 events** (6,144 per slot), **1,536 chunks against `POOL_CHUNK_COUNT = 512` (3.0x, a structural
limit)**, raw committed payload **7,698 KiB = 94% of the pool and 1,455 KiB over post-boot free**, and a snapshot
representation that duplicates the committed events at **2.5-9x the pool**. The total is **56,338 KiB against an
8,192 KiB pool** (over by 48,146), and dropping *every* view and snapshot term still leaves ~11,900 KiB. The
conclusion above — "cannot fit at any N" — therefore holds for this density, and four levers are needed together;
the only single one of the right order is the removal of the snapshot's duplication of committed events
(-23,040..-73,920). Full arithmetic, citations and the levers:
[`evidence/2026-10-02-64x64x48-capacity/ANALYSIS.md`](evidence/2026-10-02-64x64x48-capacity/ANALYSIS.md);
task **S2.11**.
